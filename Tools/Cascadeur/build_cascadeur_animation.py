"""시스템 Python에서 실행: 텍스트 요청을 옮긴 동작 명세(*.motion.json)로 UE 캐릭터 FBX 내보내기 → Cascadeur 퀵 리깅 → 키 풀이·Cascadeur 씬 제작 → 3방향 캡처 → FBX 내보내기(라이선스 허용 시) → UE AnimSequence(루트모션) 가져오기를 단계별로 실행한다.
실행: python Tools/Cascadeur/build_cascadeur_animation.py --spec AnimationSources/Cascadeur/<이름>.motion.json [--stages character,keys,scene,capture,export,import] [--rebuild-character] [--replace]  (PowerShell 권장, L-repo-01. UE 단계는 python Tools/ue_editor.py ensure 선행)
출력: Saved/Cascadeur/Characters/(FBX·리그 .casc·기준 자세), Saved/Cascadeur/<이름>/(keys·solve·captures·result.json), AnimationSources/Cascadeur/<이름>.casc·.fbx, /Game/Characters/Mannequins/Anims/Sword/<이름>
상태: 현행
"""
import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools/Cascadeur"))
sys.path.insert(0, str(ROOT / "Tools/AnimationAuthoring"))
import build_motion_keys  # noqa: E402
import cascadeur_bridge as bridge  # noqa: E402
from ue_editor import CASCADEUR_EXE  # noqa: E402

STAGES = ("character", "keys", "scene", "capture", "export", "import")
DEFAULT_CHARACTER = {"mesh": "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple", "skeleton": "/Game/Characters/Mannequins/Meshes/SK_Mannequin",
                     "template": "UE5", "weapon_profile": "Tools/Cascadeur/weapons/SM_Sword.json", "destination_folder": "/Game/Characters/Mannequins/Anims/Sword"}
VIEWS = {
    "front": {"camera_position": [-150.0, 210.0, 400.0], "camera_target": [0.0, 105.0, 30.0]},
    "side": {"camera_position": [430.0, 150.0, 30.0], "camera_target": [0.0, 105.0, 30.0]},
    "top": {"camera_position": [0.0, 470.0, -250.0], "camera_target": [0.0, 90.0, 30.0]},
}
CAPTURE_SIZE = 480
TOOL_TAG = "Tools/Cascadeur/build_cascadeur_animation.py"


def character_paths(character):
    mesh_name = character["mesh"].rsplit("/", 1)[-1]
    folder = ROOT / "Saved/Cascadeur/Characters"
    if character.get("rig_scene"):
        rig = ROOT / character["rig_scene"]
        return {"fbx": None, "rig": rig, "rest": folder / f"{rig.stem}_rest_pose.json", "user_rig": True}
    return {"fbx": folder / f"{mesh_name}.fbx", "rig": folder / f"{mesh_name}_rig.casc", "rest": folder / f"{mesh_name}_rest_pose.json", "user_rig": False}


def require(record, label):
    result = bridge.require_success(record)
    print(f"[{label}] {json.dumps(result, ensure_ascii=False)[:600]}")
    return result


def stage_character(character, rebuild):
    paths = character_paths(character)
    report = {}
    if paths["user_rig"]:
        if not paths["rig"].is_file():
            raise SystemExit(f"Rig scene not found: {paths['rig']}")
        if rebuild or not paths["rest"].is_file() or paths["rest"].stat().st_mtime < paths["rig"].stat().st_mtime:
            bridge.require_success(bridge.call("csc_motion.load_scene_copy_for_bridge", {"scene_path": paths["rig"].as_posix()}))
            rest = require(bridge.call("csc_inspect.read_pose", {"frames": [0]}), "rest")
            paths["rest"].parent.mkdir(parents=True, exist_ok=True)
            paths["rest"].write_text(json.dumps(rest), encoding="utf-8")
        report["rig_scene"] = str(paths["rig"])
        return report
    if rebuild or not paths["fbx"].is_file():
        from smoke_test import TDMcpAnimationClient
        if paths["fbx"].exists():
            paths["fbx"].unlink()
        report["export"] = TDMcpAnimationClient().call(".TDBlenderAnimationTools.export_fbx", asset_path=character["mesh"], output_file=str(paths["fbx"]))
    if rebuild or not paths["rig"].is_file():
        template = Path(CASCADEUR_EXE).parent / "resources/autorig_templates" / f"{character['template']}.qrigcasc"
        report["rig"] = require(bridge.call("csc_character.build_rigged_scene", {"fbx_path": paths["fbx"].as_posix(), "scene_path": paths["rig"].as_posix(),
                                                                                 "template_path": template.as_posix()}, 600), "rig")
    if rebuild or not paths["rest"].is_file():
        bridge.require_success(bridge.call("csc_motion.open_scene_tab_for_bridge", {"scene_path": paths["rig"].as_posix()}))
        rest = require(bridge.call("csc_inspect.read_pose", {"frames": [0]}), "rest")
        paths["rest"].write_text(json.dumps(rest), encoding="utf-8")
    return report


def stage_keys(spec_path, character, work):
    paths = character_paths(character)
    rest = json.loads(paths["rest"].read_text(encoding="utf-8-sig"))
    rest = rest.get("result", rest)
    weapon = json.loads((ROOT / character["weapon_profile"]).read_text(encoding="utf-8"))
    keys, report = build_motion_keys.solve_spec(rest, weapon, json.loads(spec_path.read_text(encoding="utf-8")))
    (work / "keys.json").write_text(json.dumps(keys), encoding="utf-8")
    (work / "keys.solve.json").write_text(json.dumps(report, indent=1, ensure_ascii=False), encoding="utf-8")
    worst_wrist = max(report, key=lambda row: row["wrist_degrees"]["r"]["swing"])
    lowest = min(report, key=lambda row: row["ground"]["lowest_height_cm"])
    return {"key_frames": len(keys["keys"]), "worst_wrist_swing": [worst_wrist["frame"], worst_wrist["wrist_degrees"]["r"]["swing"]],
            "lowest_foot": [lowest["frame"], lowest["ground"]["lowest_height_cm"]]}


def stage_scene(spec, character, work, scene_path):
    bridge.require_success(bridge.call("csc_motion.close_pipeline_tabs", {"close_paths": [(ROOT / "Saved/Cascadeur").as_posix() + "/", scene_path.as_posix()]}))
    result = require(bridge.call("csc_motion.build_animation_scene", {"rig_scene_path": character_paths(character)["rig"].as_posix(),
                                                                      "keys_path": (work / "keys.json").as_posix(), "output_scene_path": scene_path.as_posix()}, 900), "scene")
    result["ai_motion_generation"] = [require(bridge.call("csc_motion.run_ai_motion_generation", {"scene_path": scene_path.as_posix(), "first_frame": interval["frames"][0],
                                                                                                  "last_frame": interval["frames"][1]}, 900), "ai") for interval in spec.get("ai_motion_generation", [])]
    return result


def wait_for_file(path, timeout_seconds=20.0):
    deadline = time.time() + timeout_seconds
    last_size = -1
    while time.time() < deadline:
        if path.is_file():
            size = path.stat().st_size
            if size > 0 and size == last_size:
                return True
            last_size = size
        time.sleep(0.25)
    return False


def capture_view(view, frames, folder):
    bridge.require_success(bridge.call("csc_capture.prepare_capture", VIEWS[view]))
    images = []
    for frame in frames:
        bridge.require_success(bridge.call("csc_capture.set_frame", {"frame": frame}))
        path = folder / view / f"frame_{frame:04d}.png"
        if path.exists():
            path.unlink()
        bridge.require_success(bridge.call("csc_capture.render_image", {"path": path.as_posix(), "width": CAPTURE_SIZE, "height": CAPTURE_SIZE}))
        if not wait_for_file(path):
            raise RuntimeError(f"Render did not appear: {path}")
        images.append((frame, path))
    return images


def contact_sheet(images, columns, output, title):
    thumbnail = 240
    rows = (len(images) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * thumbnail, rows * thumbnail + 28), (30, 30, 30))
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 6), title, fill=(255, 255, 255))
    for index, (frame, path) in enumerate(images):
        image = Image.open(path).convert("RGB").resize((thumbnail, thumbnail))
        x, y = (index % columns) * thumbnail, (index // columns) * thumbnail + 28
        sheet.paste(image, (x, y))
        draw.text((x + 6, y + 6), f"f{frame}", fill=(255, 220, 80))
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)
    return output


def stage_capture(spec, work, scene_path):
    bridge.require_success(bridge.call("csc_motion.open_scene_tab_for_bridge", {"scene_path": scene_path.as_posix()}))
    frame_count = int(spec["frame_count"])
    authored = sorted({int(key["frame"]) for key in spec["keys"]} | set(range(spec["events"]["hit_window_frames"][0] - 3, spec["events"]["hit_window_frames"][1] + 3)))
    folder = work / "captures"
    outputs = {}
    for view in ("front", "side"):
        images = capture_view(view, authored, folder)
        outputs[f"{view}_sheet"] = str(contact_sheet(images, 8, folder / f"{spec['name']}-{view}-sheet.png", f"{spec['name']} {view}"))
    top_images = capture_view("top", list(range(0, frame_count + 1)), folder)
    gif = folder / f"{spec['name']}-top.gif"
    frames = [Image.open(path).convert("P", palette=Image.ADAPTIVE).resize((CAPTURE_SIZE // 2, CAPTURE_SIZE // 2)) for _, path in top_images]
    frames[0].save(gif, save_all=True, append_images=frames[1:], duration=int(1000 / spec.get("fps", 30)), loop=0)
    outputs["top_gif"] = str(gif)
    outputs["top_sheet"] = str(contact_sheet([item for item in top_images if item[0] in authored], 8, folder / f"{spec['name']}-top-sheet.png", f"{spec['name']} top"))
    return outputs


def stage_export(scene_path, fbx_path):
    return require(bridge.call("csc_export.export_animation_fbx", {"scene_path": scene_path.as_posix(), "fbx_path": fbx_path.as_posix()}, 600), "export")


def run_in_editor(code, work, label):
    script = work / f"editor_{label}.py"
    script.write_text(code, encoding="utf-8")
    completed = subprocess.run([sys.executable, str(ROOT / "Tools/run_in_editor.py"), str(script)], capture_output=True, text=True, cwd=ROOT, encoding="utf-8", errors="replace")
    if completed.returncode != 0:
        raise RuntimeError(f"{label} failed: {completed.stdout[-1500:]} {completed.stderr[-500:]}")
    return [line for line in completed.stdout.splitlines() if "[TDTool]" in line]


def stage_import(spec, character, fbx_path, work, replace):
    from smoke_test import TDMcpAnimationClient
    sequence_path = f"{character['destination_folder']}/{spec['name']}"
    if replace:
        run_in_editor(
            "import os, unreal\n"
            f"path = '{sequence_path}'\n"
            "if unreal.EditorAssetLibrary.does_asset_exist(path):\n"
            "    asset = unreal.EditorAssetLibrary.load_asset(path)\n"
            f"    if unreal.EditorAssetLibrary.get_metadata_tag(asset, 'TDGeneratedBy') != '{TOOL_TAG}':\n"
            "        raise RuntimeError('Refusing to replace an asset this tool did not make: ' + path)\n"
            "    package = asset.get_outermost(); asset = None\n"
            "    unreal.EditorLoadingAndSavingUtils.unload_packages([package]); package = None\n"
            "    unreal.SystemLibrary.collect_garbage()\n"
            "    file_path = os.path.join(unreal.Paths.project_content_dir(), path[len('/Game/'):] + '.uasset')\n"
            "    if os.path.exists(file_path):\n"
            "        os.remove(file_path)\n"
            f"    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['{character['destination_folder']}'], True)\n"
            "unreal.log('[TDTool] removed ' + path)\n", work, "remove_previous")
    imported = TDMcpAnimationClient().call(".TDBlenderAnimationTools.import_animation_fbx", source_file=str(fbx_path), destination_folder=character["destination_folder"],
                                           asset_name=spec["name"], skeleton_path=character["skeleton"], sample_rate=int(spec.get("fps", 30)),
                                           import_uniform_scale=1.0, preserve_local_transform=True)
    configured = run_in_editor(
        "import json, unreal\n"
        f"sequence = unreal.EditorAssetLibrary.load_asset('{sequence_path}')\n"
        "sequence.set_editor_property('enable_root_motion', True)\n"
        "sequence.set_editor_property('root_motion_root_lock', unreal.RootMotionRootLock.REF_POSE)\n"
        "sequence.set_editor_property('force_root_lock', False)\n"
        f"unreal.EditorAssetLibrary.set_metadata_tag(sequence, 'TDGeneratedBy', '{TOOL_TAG}')\n"
        "saved = unreal.EditorAssetLibrary.save_loaded_asset(sequence, only_if_is_dirty=False)\n"
        "unreal.log('[TDTool] sequence ' + json.dumps({'root_motion': sequence.get_editor_property('enable_root_motion'), 'saved': saved,"
        " 'length': unreal.AnimationLibrary.get_sequence_length(sequence), 'frames': unreal.AnimationLibrary.get_num_frames(sequence)}))\n", work, "configure_sequence")
    return {"sequence": sequence_path, "import": imported, "configure": configured}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True)
    parser.add_argument("--stages", default=",".join(STAGES))
    parser.add_argument("--rebuild-character", action="store_true")
    parser.add_argument("--replace", action="store_true", help="replace the UE sequence if this tool made it")
    arguments = parser.parse_args()
    spec_path = (ROOT / arguments.spec).resolve() if not Path(arguments.spec).is_absolute() else Path(arguments.spec)
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    character = {**DEFAULT_CHARACTER, **spec.get("character", {})}
    stages = [stage.strip() for stage in arguments.stages.split(",") if stage.strip()]
    unknown = sorted(set(stages) - set(STAGES))
    if unknown:
        raise SystemExit(f"Unknown stages: {unknown}")
    work = ROOT / "Saved/Cascadeur" / spec["name"]
    work.mkdir(parents=True, exist_ok=True)
    scene_path = spec_path.with_suffix("").with_suffix(".casc")
    fbx_path = spec_path.with_suffix("").with_suffix(".fbx")
    result_path = work / "result.json"
    result = json.loads(result_path.read_text(encoding="utf-8")) if result_path.is_file() else {}
    result.update({"spec": str(spec_path), "scene": str(scene_path), "updated": time.strftime("%Y-%m-%d %H:%M:%S")})
    if any(stage in stages for stage in ("character", "scene", "capture", "export")):
        result["cascadeur"] = bridge.ensure_server()
    for stage in stages:
        started = time.time()
        if stage == "character":
            result[stage] = stage_character(character, arguments.rebuild_character)
        elif stage == "keys":
            result[stage] = stage_keys(spec_path, character, work)
        elif stage == "scene":
            result[stage] = stage_scene(spec, character, work, scene_path)
        elif stage == "capture":
            result[stage] = stage_capture(spec, work, scene_path)
        elif stage == "export":
            result[stage] = stage_export(scene_path, fbx_path)
            if not result[stage].get("exported"):
                result_path.write_text(json.dumps(result, indent=1, ensure_ascii=False), encoding="utf-8")
                print(json.dumps(result[stage], ensure_ascii=False))
                raise SystemExit("Stopped at export: " + result[stage].get("reason", "export failed"))
        elif stage == "import":
            if not fbx_path.is_file() or fbx_path.stat().st_mtime < scene_path.stat().st_mtime:
                raise SystemExit(f"No FBX newer than the Cascadeur scene to import (run the export stage first): {fbx_path}")
            result[stage] = stage_import(spec, character, fbx_path, work, arguments.replace)
        result[stage + "_seconds"] = round(time.time() - started, 1)
        result_path.write_text(json.dumps(result, indent=1, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({stage: result.get(stage) for stage in stages}, ensure_ascii=False, indent=1)[:4000])


if __name__ == "__main__":
    main()
