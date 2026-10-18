"""에디터 안에서 실행: Tools/MonsterAI/species_table.json 의 retarget 절에 적힌 원천(예: UE5 Manny) 애니메이션을 대상 스켈레톤으로 IK 리타기팅해 몬스터용 AnimSequence(루트 모션 끔·루트 잠금)를 만들고 클립별 검사 수치를 출력한다.
실행: python Tools/run_in_editor.py Tools/MonsterAI/editor_retarget_monster_anims.py (PowerShell; Git Bash는 PYTHONIOENCODING=utf-8 을 앞에 붙인다). 일부만: -c "TD_ONLY_TARGETS=['Goblin']; TD_ONLY_CLIPS=['HitReact']; exec(open('C:/Project/TDGame/Tools/MonsterAI/editor_retarget_monster_anims.py', encoding='utf-8').read())"(둘 다 선택, 이때 바뀐 에셋만 저장)
출력: /Game/MonsterAI/Retarget/{IK_TD_*,RTG_TD_<원천>To<대상>}, /Game/MonsterAI/Animations/<대상>/AS_TD_<대상>_<클립>, 로그의 [TDTool] check 줄(길이·이상 플래그·접지 속도·타격 시각)
상태: 현행(M3-33, 대상·클립 목록은 표가 정본)

규칙: 에셋 메타데이터 TDGeneratedBy 가 이 도구 이름인 에셋만 갱신·덮어쓴다. 원본 에셋과 레벨은 저장하지 않는다.
"""
import importlib
import math
import os
import sys
import time

import unreal

t0 = time.time()
EAL = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
SOURCE = unreal.RetargetSourceOrTarget.SOURCE
TARGET = unreal.RetargetSourceOrTarget.TARGET

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
TABLE_TOOLS_DIR = os.path.join(PROJECT_DIR, "Tools", "MonsterAI")
if TABLE_TOOLS_DIR not in sys.path:
    sys.path.insert(0, TABLE_TOOLS_DIR)
import species_table

importlib.reload(species_table)

OWNER_TAG = "TDGeneratedBy"
OWNER_NAME = "editor_retarget_monster_anims"
RETARGET_FOLDER = "/Game/MonsterAI/Retarget"
TABLE = species_table.load_table()
SOURCES = TABLE["retarget"]["sources"]
CLIP_SPECS = TABLE["retarget"]["clips"]
TARGETS = TABLE["retarget"]["targets"]
ONLY_CLIPS = globals().get("TD_ONLY_CLIPS")
ONLY_TARGETS = globals().get("TD_ONLY_TARGETS")
BONE_ROLES = ("root", "pelvis", "head", "hand_l", "hand_r", "foot_l", "foot_r", "ball_l", "ball_r")


def log(msg):
    print(f"[TDTool {time.time() - t0:6.1f}s] {msg}")


def is_owned(asset):
    return EAL.get_metadata_tag(asset, OWNER_TAG) == OWNER_NAME


def mark_owned(asset):
    EAL.set_metadata_tag(asset, OWNER_TAG, OWNER_NAME)


def load_owned_or_create(folder, name, asset_class, factory):
    path = f"{folder}/{name}"
    if not EAL.does_asset_exist(path):
        asset = asset_tools.create_asset(name, folder, asset_class, factory)
        mark_owned(asset)
        return asset, True
    asset = EAL.load_asset(path)
    if not is_owned(asset):
        log(f"skip (not created by this tool): {path}")
        return None, False
    return asset, False


def ensure_ik_rig(name, mesh_path):
    rig, is_new = load_owned_or_create(RETARGET_FOLDER, name, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    if rig is None:
        return None
    controller = unreal.IKRigController.get_controller(rig)
    controller.set_skeletal_mesh(EAL.load_asset(mesh_path))
    if is_new or not controller.get_retarget_chains():
        has_definition = controller.apply_auto_generated_retarget_definition()
        has_fbik = controller.apply_auto_fbik()
        log(f"{name}: auto retarget definition {has_definition}, auto FBIK {has_fbik}")
    log(f"{name}: chains {len(controller.get_retarget_chains())}, solvers {controller.get_num_solvers()}, root {controller.get_retarget_root()}")
    return rig


def ensure_retargeter(name, source_rig, target_rig, source_mesh_path, target_mesh_path):
    retargeter, _ = load_owned_or_create(RETARGET_FOLDER, name, unreal.IKRetargeter, unreal.IKRetargetFactory())
    if retargeter is None:
        return None
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    controller.set_ik_rig(SOURCE, source_rig)
    controller.set_ik_rig(TARGET, target_rig)
    controller.set_preview_mesh(SOURCE, EAL.load_asset(source_mesh_path))
    controller.set_preview_mesh(TARGET, EAL.load_asset(target_mesh_path))
    controller.remove_all_ops()
    controller.add_default_ops()
    controller.assign_ik_rig_to_all_ops(SOURCE, source_rig)
    controller.assign_ik_rig_to_all_ops(TARGET, target_rig)
    controller.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    op_names = [str(controller.get_op_name(i)) for i in range(controller.get_num_retarget_ops())]
    log(f"{name}: ops {op_names}")
    return retargeter


def retarget_clip(retargeter, source_mesh, target_key, target_mesh, clip):
    clip_spec = CLIP_SPECS[clip]
    source_path = clip_spec["anim"]
    source_name = source_path.split("/")[-1]
    output_path = species_table.retarget_output_path(TABLE, target_key, clip)
    output_folder, output_name = output_path.rsplit("/", 1)
    if not EAL.does_asset_exist(source_path):
        log(f"skip {output_name}: source missing {source_path}")
        return None
    if EAL.does_asset_exist(output_path) and not is_owned(EAL.load_asset(output_path)):
        log(f"skip {output_name}: existing asset not created by this tool")
        return None
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.set_editor_property("assets_to_retarget", [EAL.find_asset_data(source_path)])
    inputs.set_editor_property("source_mesh", source_mesh)
    inputs.set_editor_property("target_mesh", target_mesh)
    inputs.set_editor_property("ik_retarget_asset", retargeter)
    inputs.set_editor_property("search", source_name)
    inputs.set_editor_property("replace", output_name)
    inputs.set_editor_property("target_path", output_folder)
    inputs.set_editor_property("use_source_path", False)
    inputs.set_editor_property("include_referenced_assets", False)
    inputs.set_editor_property("overwrite_existing_files", True)
    unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    sequence = EAL.load_asset(output_path)
    if not isinstance(sequence, unreal.AnimSequence):
        log(f"FAIL {output_name}: batch retarget produced no sequence at {output_path}")
        return None
    sequence.set_editor_property("enable_root_motion", False)
    sequence.set_editor_property("force_root_lock", True)
    if clip_spec["kind"] == "idle":
        sequence.set_editor_property("loop", True)
    mark_owned(sequence)
    return sequence


def make_evaluation_options(mesh):
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", mesh)
    options.set_editor_property("should_retarget", True)
    return options


def find_present_bones(skeleton, bone_names):
    reference = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    available = {str(name) for name in unreal.AnimPoseExtensions.get_bone_names(reference)}
    resolved = {role: bone_names.get(role, role) for role in BONE_ROLES}
    return {role: bone for role, bone in resolved.items() if bone in available}


def sample_pose(sequence, options, time_seconds, present_bones):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, time_seconds, options)
    root = unreal.AnimPoseExtensions.get_bone_pose(pose, present_bones["root"], unreal.AnimPoseSpaces.WORLD) if "root" in present_bones else unreal.Transform()
    positions = {"root": root.translation}
    for role, bone in present_bones.items():
        if role == "root":
            continue
        world = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)
        positions[role] = unreal.MathLibrary.make_relative_transform(world, root).translation
    return positions


def distance(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


def find_pose_flags(samples, height):
    flags = []
    count = len(samples) - 1
    for fraction in (0.0, 0.25, 0.5, 0.75):
        pose = samples[int(round(fraction * count))]
        if any(math.isnan(c) for v in pose.values() for c in (v.x, v.y, v.z)):
            flags.append(f"nan@{fraction}")
        if "head" in pose and "pelvis" in pose and pose["head"].z < pose["pelvis"].z:
            flags.append(f"head_below_pelvis@{fraction}")
        for limb in ("hand_l", "hand_r", "foot_l", "foot_r"):
            if limb in pose and "pelvis" in pose and distance(pose[limb], pose["pelvis"]) > 1.2 * height:
                flags.append(f"{limb}_far@{fraction}")
        for foot in ("foot_l", "foot_r"):
            if foot in pose and pose[foot].z < -0.03 * height:
                flags.append(f"{foot}_buried@{fraction}")
    return flags


def measure_planted_foot_speed(samples, step, height):
    speeds = []
    for toe in ("ball_l", "ball_r"):
        if toe not in samples[0]:
            continue
        heights = [s[toe].z for s in samples]
        lowest = min(heights)
        for threshold in (0.003 * height, 0.006 * height, 0.012 * height):
            velocities = sorted(-(samples[i + 1][toe].y - samples[i - 1][toe].y) / (2 * step) for i in range(1, len(samples) - 1) if heights[i] < lowest + threshold)
            if len(velocities) >= 4:
                speeds.append(round(velocities[len(velocities) // 2]))
                break
    return speeds


def read_pelvis_y(sample):
    return sample["pelvis"].y if "pelvis" in sample else 0.0


def measure_hit_time(samples, times, step):
    best = None
    for hand in ("hand_l", "hand_r"):
        if hand not in samples[0]:
            continue
        for i in range(1, len(samples) - 1):
            speed = distance(samples[i + 1][hand], samples[i - 1][hand]) / (2 * step)
            if best is None or speed > best[2]:
                best = (hand, times[i], speed)
    if best is None:
        return None
    hand = best[0]
    reach_index = max(range(len(samples)), key=lambda i: samples[i][hand].y - read_pelvis_y(samples[i]))
    return {"hand": hand, "peak_speed_time": round(best[1], 3), "peak_speed": round(best[2]), "max_reach_time": round(times[reach_index], 3)}


def check_clip(sequence, mesh, kind, bone_names):
    height = mesh.get_bounds().box_extent.z * 2
    length = sequence.get_play_length()
    frames = max(2, int(length * 120))
    step = length / frames
    times = [length * i / frames for i in range(frames + 1)]
    options = make_evaluation_options(mesh)
    present_bones = find_present_bones(sequence.get_editor_property("skeleton"), bone_names)
    samples = [sample_pose(sequence, options, t, present_bones) for t in times]
    feet = [role for role in ("foot_l", "foot_r") if role in samples[0]]
    report = {
        "skeleton": sequence.get_editor_property("skeleton").get_name(),
        "length": round(length, 3),
        "root_motion": sequence.get_editor_property("enable_root_motion"),
        "root_lock": sequence.get_editor_property("force_root_lock"),
        "flags": find_pose_flags(samples, height),
        "min_foot_z": round(min(min(s[foot].z for foot in feet) for s in samples), 1) if feet else None,
        "pelvis_z": round(samples[0]["pelvis"].z, 1) if "pelvis" in samples[0] else None,
        "head_z": round(samples[0]["head"].z, 1) if "head" in samples[0] else None,
    }
    missing_roles = [role for role in BONE_ROLES if role not in present_bones]
    if missing_roles:
        report["missing_bones"] = missing_roles
    if kind == "locomotion":
        report["planted_foot_speed"] = measure_planted_foot_speed(samples, step, height)
        report["root_track_speed"] = round(distance(samples[-1]["root"], samples[0]["root"]) / length)
        report["pelvis_drift_y"] = round(read_pelvis_y(samples[-1]) - read_pelvis_y(samples[0]), 1)
    if kind == "attack":
        report["hit"] = measure_hit_time(samples, times, step)
    return report


def prepare_source_rig(source_key, source_rigs, to_save):
    if source_key in source_rigs:
        return source_rigs[source_key]
    source = SOURCES[source_key]
    rig = ensure_ik_rig(source["rig"], source["mesh"])
    source_rigs[source_key] = rig
    if rig is not None:
        to_save.append(rig)
    return rig


def main():
    source_rigs = {}
    to_save = []
    checks = []
    for target_key, target in TARGETS.items():
        if ONLY_TARGETS and target_key not in ONLY_TARGETS:
            continue
        source_key = target["source"]
        source = SOURCES[source_key]
        source_rig = prepare_source_rig(source_key, source_rigs, to_save)
        if source_rig is None:
            log(f"FAIL: {source['rig']} unavailable")
            continue
        target_rig = ensure_ik_rig(f"IK_TD_{target_key}", target["mesh"])
        retargeter = ensure_retargeter(f"RTG_TD_{source_key}To{target_key}", source_rig, target_rig, source["mesh"], target["mesh"]) if target_rig else None
        if retargeter is None:
            log(f"FAIL: {target_key} rig or retargeter unavailable")
            continue
        to_save += [target_rig, retargeter]
        source_mesh = EAL.load_asset(source["mesh"])
        target_mesh = EAL.load_asset(target["mesh"])
        for clip in target["clips"]:
            if ONLY_CLIPS and clip not in ONLY_CLIPS:
                continue
            sequence = retarget_clip(retargeter, source_mesh, target_key, target_mesh, clip)
            if sequence is None:
                continue
            to_save.append(sequence)
            checks.append((sequence, target_mesh, CLIP_SPECS[clip]["kind"], target.get("bone_names", {})))
    saved = EAL.save_loaded_assets(to_save, bool(ONLY_CLIPS or ONLY_TARGETS))
    log(f"saved {len(to_save)} assets: {saved}")
    for sequence, mesh, kind, bone_names in checks:
        log(f"check {sequence.get_path_name().split('.')[0]} {check_clip(sequence, mesh, kind, bone_names)}")


main()
