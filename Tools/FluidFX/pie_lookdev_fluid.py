"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). FluidFX 무대 PIE 하나에서 변형(노출·조명·불빛·불꽃 카드/볼륨 재질 값·폰 위치·카메라 거리·깨어 있는 유체 액터·폭발 트리거)을 차례로 적용하고 정착 시간 뒤 게임 화면을 찍어 이름표 붙은 비교 격자를 만든다.
실행 명령: python Tools/FluidFX/pie_lookdev_fluid.py --variants <변형 JSON> [--level /Game/FluidFX/Maps/LV_TDFluidFireStage] [--settle-seconds 3] [--stop-pie] (PowerShell). 변형 JSON = {"base": {적용}, "variants": [{"name": "영문", "apply": {적용}, "settle": 초}]}, 적용 키 = awake·pawn·arm·exposure_ev100·lights·fire_light·flame·volume·sim(simulation_config 필드, apply_simulation_config)·emitter(source_emitter 필드)·move(내비 이동 목표)·speed·trigger
출력: Saved/FluidFX/lookdev/<시각>/<순번>_<이름>.png, grid.png, result.json. 종료 코드 0 전부 찍음/1 일부 실패/2 환경 문제
상태: 검증됨(2026-10-18 모닥불·횃불·폭발 변형 9회 실행, campfire_v1~v5·torch_v1·explosion_v1). PIE 런타임 값만 바꾸므로 에셋은 바뀌지 않는다. 고른 값은 재질·무대 생성 스크립트에 옮겨 적는다.
"""
import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import pie_check_fluid  # noqa: E402
import pie_showcase_fluid  # noqa: E402
import run_in_editor  # noqa: E402
import uemcp  # noqa: E402

RUN_TOKEN = datetime.now().strftime("%Y%m%d-%H%M%S")
OUTPUT_DIRECTORY = ROOT / "Saved" / "FluidFX" / "lookdev" / RUN_TOKEN
DEFAULT_LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFluidFireStage"
SHOT_SIZE = "1280x720"
GRID_TILE_WIDTH = 640
GRID_COLUMNS = 2
FRAME_FILE_WAIT_SECONDS = 10.0
EXIT_PARTIAL = 1
EXIT_ENVIRONMENT = 2

LOOKDEV_ACTIONS = r'''
def find_actor_by_label(world, label):
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        if actor.get_actor_label() == label:
            return actor
    return None


def set_material_values(material, values):
    applied = {}
    if not isinstance(material, unreal.MaterialInstanceDynamic):
        return {"error": "MID 아님: " + str(material)}
    for name, value in values.items():
        if isinstance(value, list):
            material.set_vector_parameter_value(name, unreal.LinearColor(*(value + [1.0] * (4 - len(value)))))
        else:
            material.set_scalar_parameter_value(name, float(value))
        applied[name] = value
    return applied


def apply_awake(world, labels):
    states = {}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor):
        is_awake = actor.get_actor_label() in labels
        actor.get_editor_property("live_component").set_simulation_awake(is_awake)
        states[actor.get_actor_label()] = is_awake
    return states


def apply_exposure(world, ev100):
    changed = []
    for volume in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PostProcessVolume):
        settings = volume.get_editor_property("settings")
        settings.set_editor_property("override_auto_exposure_min_brightness", True)
        settings.set_editor_property("override_auto_exposure_max_brightness", True)
        settings.set_editor_property("auto_exposure_min_brightness", ev100)
        settings.set_editor_property("auto_exposure_max_brightness", ev100)
        volume.set_editor_property("settings", settings)
        changed.append(volume.get_actor_label())
    return changed


def apply_component_values(world, mapping, component_property, apply_values):
    outcome = {}
    for label, values in mapping.items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome[label] = {"error": "액터 없음"}
            continue
        component = actor.get_editor_property(component_property)
        outcome[label] = apply_values(component, values)
    return outcome


def set_properties(component, values):
    for name, value in values.items():
        component.set_editor_property(name, value)
    return values


def set_mesh_material_values(component, values):
    if component is None:
        return {"error": "컴포넌트 없음"}
    return set_material_values(component.get_material(0), values)


def action_lookdev_apply(world):
    apply = PARAMS["apply"]
    outcome = {}
    if "awake" in apply:
        outcome["awake"] = apply_awake(world, set(apply["awake"]))
    if "pawn" in apply:
        teleport_pawn(world, apply["pawn"][0], apply["pawn"][1])
        outcome["pawn"] = apply["pawn"]
    if "arm" in apply:
        pawn, controller = player_pawn_and_controller(world)
        boom = pawn.get_component_by_class(unreal.SpringArmComponent)
        boom.set_editor_property("target_arm_length", apply["arm"])
        outcome["arm"] = boom.get_editor_property("target_arm_length")
    if "exposure_ev100" in apply:
        outcome["exposure"] = apply_exposure(world, apply["exposure_ev100"])
    for label, intensity in apply.get("lights", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("lights", {})[label] = "액터 없음"
            continue
        actor.get_component_by_class(unreal.LightComponentBase).set_intensity(intensity)
        outcome.setdefault("lights", {})[label] = intensity
    if "fire_light" in apply:
        outcome["fire_light"] = apply_component_values(world, apply["fire_light"], "fire_light_component", set_properties)
    if "flame" in apply:
        outcome["flame"] = apply_component_values(world, apply["flame"], "internal_surface_mesh", set_mesh_material_values)
    if "volume" in apply:
        outcome["volume"] = apply_component_values(world, apply["volume"], "volume_mesh_component", set_mesh_material_values)
    for label, values in apply.get("sim", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("sim", {})[label] = "액터 없음"
            continue
        component = actor.get_editor_property("live_component")
        config = component.get_editor_property("simulation_config")
        for name, value in values.items():
            config.set_editor_property(name, value)
        component.apply_simulation_config(config)
        outcome.setdefault("sim", {})[label] = values
    for label, values in apply.get("emitter", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("emitter", {})[label] = "액터 없음"
            continue
        emitter = actor.get_editor_property("source_emitter")
        for name, value in values.items():
            emitter.set_editor_property(name, value)
        actor.set_editor_property("source_emitter", emitter)
        outcome.setdefault("emitter", {})[label] = values
    if "move" in apply:
        PARAMS["start_x"] = None
        PARAMS["goal_x"], PARAMS["goal_y"] = apply["move"]
        outcome["move"] = action_fire_move(world)
    if "speed" in apply:
        pawn, controller = player_pawn_and_controller(world)
        pawn.set_max_move_speed(apply["speed"])
        outcome["speed"] = pawn.get_max_move_speed()
    if "trigger" in apply:
        PARAMS["hit_dx"], PARAMS["hit_dy"], PARAMS["hit_dz"] = apply["trigger"]
        outcome["trigger"] = action_fire_trigger(world)
    return outcome


def run():'''


def editor_action(action, **fields):
    code = pie_showcase_fluid.EDITOR_CODE.replace("\ndef run():", LOOKDEV_ACTIONS, 1)
    params = {"action": action, **fields}
    outcome = run_in_editor.run_code(code.replace("__PARAMS__", repr(json.dumps(params))))
    if not outcome["connected"]:
        return {"error": "에디터 원격 실행에 연결하지 못했다"}
    for line in outcome["lines"]:
        index = line.find(pie_showcase_fluid.RESULT_MARKER)
        if index >= 0:
            try:
                return json.loads(line[index + len(pie_showcase_fluid.RESULT_MARKER):].strip().splitlines()[0])
            except ValueError:
                return {"error": "결과 해석 실패: " + line[-200:]}
    return {"error": "결과 표식 없음: " + " | ".join(outcome["lines"])[-300:]}


def wait_for_file(path):
    deadline = time.monotonic() + FRAME_FILE_WAIT_SECONDS
    while time.monotonic() < deadline:
        if path.exists() and path.stat().st_size > 0:
            return True
        time.sleep(0.5)
    return False


def build_grid(shots, grid_path):
    tiles = []
    for name, path in shots:
        image = Image.open(path).convert("RGB")
        tile_height = int(image.height * GRID_TILE_WIDTH / image.width)
        tile = image.resize((GRID_TILE_WIDTH, tile_height))
        draw = ImageDraw.Draw(tile)
        draw.rectangle((0, 0, GRID_TILE_WIDTH, 22), fill=(0, 0, 0))
        draw.text((6, 4), name, fill=(255, 255, 255))
        tiles.append(tile)
    if not tiles:
        return False
    rows = (len(tiles) + GRID_COLUMNS - 1) // GRID_COLUMNS
    grid = Image.new("RGB", (GRID_TILE_WIDTH * GRID_COLUMNS, tiles[0].height * rows))
    for index, tile in enumerate(tiles):
        grid.paste(tile, ((index % GRID_COLUMNS) * GRID_TILE_WIDTH, (index // GRID_COLUMNS) * tiles[0].height))
    grid.save(grid_path)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--variants", required=True, type=Path)
    parser.add_argument("--level", default=DEFAULT_LEVEL_PATH)
    parser.add_argument("--settle-seconds", type=float, default=3.0)
    parser.add_argument("--stop-pie", action="store_true")
    arguments = parser.parse_args()
    specification = json.loads(arguments.variants.read_text(encoding="utf-8"))
    print("경고: 측정 동안 UnrealEditor 창을 앞창으로 두고 건드리지 않는다(L-editor-06).", flush=True)
    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)

    session = None
    environment = {}
    if not pie_check_fluid.wait_for_pie_state(True, 0.5):
        level_status = pie_showcase_fluid.open_stage_level(arguments.level)
        if not level_status.get("loaded"):
            print("레벨을 열지 못했다:", json.dumps(level_status, ensure_ascii=False))
            return EXIT_ENVIRONMENT
        try:
            session = uemcp.connect()
        except (OSError, ValueError, KeyError) as error:
            print("MCP 서버에 연결하지 못했다:", error)
            return EXIT_ENVIRONMENT
        environment["navigation_ready"] = pie_check_fluid.wait_for_editor_navigation()
        if not pie_check_fluid.start_pie(session, environment) or not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
            print("PIE를 시작하지 못했다:", json.dumps(environment, ensure_ascii=False)[:300])
            return EXIT_ENVIRONMENT
        time.sleep(3.0)

    result = {"level": arguments.level, "base": editor_action("lookdev_apply", apply=specification.get("base", {})), "variants": []}
    shots = []
    for index, variant in enumerate(specification["variants"]):
        outcome = editor_action("lookdev_apply", apply=variant.get("apply", {}))
        time.sleep(variant.get("settle", arguments.settle_seconds))
        path = OUTPUT_DIRECTORY / f"{index:02d}_{variant['name']}.png"
        editor_action("shot", size=SHOT_SIZE, path=path.as_posix())
        is_saved = wait_for_file(path)
        if is_saved:
            shots.append((variant["name"], path))
        result["variants"].append({"name": variant["name"], "apply": outcome, "saved": is_saved})
        print(variant["name"], "찍음" if is_saved else "실패", json.dumps(outcome, ensure_ascii=False)[:200], flush=True)

    has_grid = build_grid(shots, OUTPUT_DIRECTORY / "grid.png")
    (OUTPUT_DIRECTORY / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    if arguments.stop_pie:
        if session is None:
            session = uemcp.connect()
        pie_check_fluid.stop_pie(session, environment)
    print("격자", (OUTPUT_DIRECTORY / "grid.png").relative_to(ROOT).as_posix() if has_grid else "없음")
    return 0 if len(shots) == len(specification["variants"]) else EXIT_PARTIAL


if __name__ == "__main__":
    sys.exit(main())
