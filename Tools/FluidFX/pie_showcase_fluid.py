"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태). LV_TDFluidTestArena PIE에서 걷기 물 자국·더미 타격과 처치 피·진흙·경사로 물 흐름 4장면을 연출하며 게임 화면을 연속 HighResShot으로 찍는다.
실행 명령: python Tools/FluidFX/pie_showcase_fluid.py [--stage arena|smoke|fire] [--stop-pie] [--frame-interval 0.4] (PowerShell, 에디터 앞창 유지: 백그라운드 PIE는 3fps로 제한된다 L-editor-06)
출력: Saved/FluidFX/showcase/<시각>/frame_###.png, Docs/Validation/FluidFX/D-48-showcase-<날짜>.gif·-sheet.png(--stage smoke 는 D-48-showcase-smoke-<날짜>.gif·-sheet.png, LV_TDFluidSmokeStage; --stage fire 는 Saved/FluidFX/showcase/D-49-fx3-fire-<날짜>.gif·-sheet.png, LV_TDFluidFireStage 장면 3개), 기본은 끝난 뒤 PIE를 켜 둔다. 종료 코드 0 성공/1 프레임 부족/2 환경 문제
상태: 현행(2026-10-18). --stage smoke(FX-1·FX-2)·fire(FX-3 횃불·모닥불·폭발) 검증됨. 폰 이동 속도는 set_max_move_speed(Mover 폰, CharacterMovement 없음), 카메라 암은 불 장면에서 450cm
"""
import argparse
import json
import sys
import time
from datetime import date, datetime
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import pie_check_fluid  # noqa: E402
import run_in_editor  # noqa: E402
import uemcp  # noqa: E402

RUN_TOKEN = datetime.now().strftime("%Y%m%d-%H%M%S")
FRAME_DIRECTORY = ROOT / "Saved" / "FluidFX" / "showcase" / RUN_TOKEN
GIF_PATH = ROOT / "Docs" / "Validation" / "FluidFX" / f"D-48-showcase-{date.today().isoformat()}.gif"
SHEET_PATH = ROOT / "Docs" / "Validation" / "FluidFX" / f"D-48-showcase-{date.today().isoformat()}-sheet.png"
SMOKE_GIF_PATH = ROOT / "Docs" / "Validation" / "FluidFX" / f"D-48-showcase-smoke-{date.today().isoformat()}.gif"
SMOKE_SHEET_PATH = ROOT / "Docs" / "Validation" / "FluidFX" / f"D-48-showcase-smoke-{date.today().isoformat()}-sheet.png"
SMOKE_LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFluidSmokeStage"
FIRE_LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFluidFireStage"
FIRE_OUTPUT_DIRECTORY = ROOT / "Saved" / "FluidFX" / "showcase"
FIRE_GIF_PATH = FIRE_OUTPUT_DIRECTORY / f"D-49-fx3-fire-{date.today().isoformat()}.gif"
FIRE_SHEET_PATH = FIRE_OUTPUT_DIRECTORY / f"D-49-fx3-fire-{date.today().isoformat()}-sheet.png"
FIRE_FRAME_INTERVAL = 0.25
FIRE_GIF_FRAME_MS = 250
FIRE_TORCH_CORNERS = ((-300.0, -300.0), (300.0, -300.0), (300.0, 300.0), (-300.0, 300.0), (-300.0, -300.0))
FIRE_TORCH_LEG_FRAMES = 10
FIRE_CAMPFIRE_START = (-212.0, 1246.0)
FIRE_CAMPFIRE_GOAL = (354.0, 1812.0)
FIRE_CAMPFIRE_WALK_SPEED_CM_PER_SEC = 400.0 / 3.0
FIRE_EXPLOSION_PAWN = (-177.0, 3023.0)
FIRE_EXPLOSION_HIT_OFFSET = (-55.0, 0.0, -35.0)
FIRE_EXPLOSION_TRIGGER_FRAME = 4
FIRE_CAMERA_ARM_LENGTH_CM = 450.0
DEFAULT_WALK_SPEED_CM_PER_SEC = 600.0
RESULT_MARKER = "TD_FLUID_SHOWCASE_RESULT"
SHOT_SIZE = "960x540"
GIF_WIDTH = 720
GIF_FRAME_MS = 280
FRAME_FILE_WAIT_SECONDS = 8.0
EXIT_TOO_FEW_FRAMES = 1
EXIT_ENVIRONMENT = 2

SCENES = (
    {"name": "walk", "title": "걷기: 플레이어를 따라가는 물 자국", "frames": 10},
    {"name": "combat", "title": "타격·처치: 원소별 피격 자국과 사망 자국", "frames": 12},
    {"name": "mud", "title": "진흙: 움직이는 브러시가 남기는 눌림", "frames": 8},
    {"name": "ramp", "title": "경사로: 지형 고도로 내리막 흐름", "frames": 10},
)

SMOKE_SCENES = (
    {"name": "smoke_hard_walk", "title": "HARD 연기: 폰이 가로질러 걷기", "frames": 12},
    {"name": "smoke_soft_walk", "title": "SOFT 연기: 폰이 가로질러 걷기", "frames": 12},
    {"name": "smoke_light", "title": "정지한 폰 옆에서 도는 포인트 라이트", "frames": 8},
    {"name": "smoke_push", "title": "밀기 스탬프: 음수 밀도와 바깥 속도로 연기 뚫기", "frames": 10},
)

FIRE_SCENES = (
    {"name": "fire_torch_walk", "title": "횃불을 든 폰: 600cm 정사각 경로", "frames": 40, "interval": FIRE_FRAME_INTERVAL},
    {"name": "fire_campfire", "title": "모닥불: 폰이 3초에 불을 통과(화면 위·중앙·아래 구도)", "frames": 32, "interval": FIRE_FRAME_INTERVAL},
    {"name": "fire_explosion", "title": "폭발 블록: 조각 궤적과 고리 섬광", "frames": 28, "interval": FIRE_FRAME_INTERVAL},
)

STAGES = {
    "arena": {"scenes": SCENES, "gif_path": GIF_PATH, "sheet_path": SHEET_PATH, "level_path": None, "frame_ms": GIF_FRAME_MS},
    "smoke": {"scenes": SMOKE_SCENES, "gif_path": SMOKE_GIF_PATH, "sheet_path": SMOKE_SHEET_PATH, "level_path": SMOKE_LEVEL_PATH, "frame_ms": GIF_FRAME_MS},
    "fire": {"scenes": FIRE_SCENES, "gif_path": FIRE_GIF_PATH, "sheet_path": FIRE_SHEET_PATH, "level_path": FIRE_LEVEL_PATH, "frame_ms": FIRE_GIF_FRAME_MS},
}

EDITOR_CODE = r'''
import json
import math
import traceback

import unreal

PARAMS = json.loads(__PARAMS__)
RESULT_MARKER = "TD_FLUID_SHOWCASE_RESULT"


def game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def player_pawn_and_controller(world):
    return unreal.GameplayStatics.get_player_pawn(world, 0), unreal.GameplayStatics.get_player_controller(world, 0)


def teleport_pawn(world, x, y):
    pawn, controller = player_pawn_and_controller(world)
    if controller is not None:
        controller.stop_movement()
    location = pawn.get_actor_location()
    pawn.set_actor_location(unreal.Vector(x, y, location.z), False, True)
    return pawn, controller


def damage_targets(world):
    targets = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageTarget))
    return sorted(targets, key=lambda actor: (actor.get_actor_location().x, actor.get_actor_location().y))


def damage_spec(amount, element):
    spec = unreal.TDDamageSpec()
    spec.set_editor_property("amount", amount)
    spec.set_editor_property("element", element)
    return spec


def brush_stamp(x, y, z, velocity_x, radius, density, channel):
    stamp = unreal.TDFluidBrushStamp()
    stamp.set_editor_property("world_location", unreal.Vector(x, y, z))
    stamp.set_editor_property("world_velocity", unreal.Vector(velocity_x, 0.0, 0.0))
    stamp.set_editor_property("radius_cm", radius)
    stamp.set_editor_property("density_strength", density)
    stamp.set_editor_property("velocity_strength", 1.0 if velocity_x else 0.0)
    stamp.set_editor_property("channel", channel)
    return stamp


def fluid_subsystem(world):
    return unreal.TDFluidLiveSubsystem.get_fluid_live_subsystem(world)


def action_shot(world):
    unreal.SystemLibrary.execute_console_command(world, "HighResShot " + PARAMS["size"] + " filename=" + PARAMS["path"])
    return {"requested": PARAMS["path"]}


def action_walk_begin(world):
    pawn, controller = teleport_pawn(world, -450.0, -260.0)
    goal = unreal.Vector(-450.0, 700.0, pawn.get_actor_location().z)
    unreal.AIHelperLibrary.simple_move_to_location(controller, goal)
    return {"goal": [goal.x, goal.y]}


def action_combat_begin(world):
    teleport_pawn(world, 250.0, -360.0)
    return {"targets": len(damage_targets(world))}


def action_combat_hit(world):
    targets = [actor for actor in damage_targets(world) if actor.get_component_by_class(unreal.TDCombatComponent).is_alive()]
    if not targets:
        return {"hit": None}
    index = PARAMS["hit_index"]
    elements = [unreal.TDDamageElement.FIRE, unreal.TDDamageElement.PHYSICAL, unreal.TDDamageElement.FROST]
    target = targets[index % len(targets)]
    is_kill = PARAMS.get("kill", False)
    amount = 100000.0 if is_kill else 25.0
    applied = unreal.TDCombatLibrary.try_apply_damage(target, damage_spec(amount, elements[index % len(elements)]))
    return {"hit": target.get_actor_label(), "applied": applied, "kill": is_kill}


def action_mud_begin(world):
    teleport_pawn(world, -750.0, -420.0)
    return {}


def action_mud_stamp(world):
    step = PARAMS["step"]
    x = -1050.0 + step * 85.0
    received = fluid_subsystem(world).inject_combat_brush_stamp(brush_stamp(x, -760.0, 6.0, 500.0, 90.0, 1.2, unreal.TDFluidBrushChannel.CHANNEL_R_MUD))
    return {"x": x, "received": received}


def action_ramp_begin(world):
    teleport_pawn(world, 820.0, 470.0)
    received = fluid_subsystem(world).inject_combat_brush_stamp(brush_stamp(1300.0, 820.0, 90.0, 0.0, 170.0, 1.6, unreal.TDFluidBrushChannel.CHANNEL_A_WATER))
    return {"received": received}


def smoke_stamp(x, y, z, velocity_x, velocity_y, radius, density, radial_velocity):
    stamp = unreal.TDFluidBrushStamp()
    stamp.set_editor_property("world_location", unreal.Vector(x, y, z))
    stamp.set_editor_property("world_velocity", unreal.Vector(velocity_x, velocity_y, 0.0))
    stamp.set_editor_property("radius_cm", radius)
    stamp.set_editor_property("density_strength", density)
    stamp.set_editor_property("velocity_strength", 1.0)
    stamp.set_editor_property("radial_velocity_cm_per_sec", radial_velocity)
    stamp.set_editor_property("channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    return stamp


def action_smoke_walk_begin(world):
    pawn, controller = teleport_pawn(world, PARAMS["start_x"], PARAMS["start_y"])
    goal = unreal.Vector(PARAMS["goal_x"], PARAMS["goal_y"], pawn.get_actor_location().z)
    unreal.AIHelperLibrary.simple_move_to_location(controller, goal)
    return {"goal": [goal.x, goal.y]}


def action_smoke_stand_begin(world):
    teleport_pawn(world, PARAMS["x"], PARAMS["y"])
    return {}


def action_smoke_push_stamp(world):
    step = PARAMS["step"]
    substamps = PARAMS["substamps"]
    subsystem = fluid_subsystem(world)
    received = 0
    for sub_index in range(substamps):
        progress = (step + (sub_index + 1) / substamps) * PARAMS["advance_cm"]
        x = PARAMS["start_x"] + progress
        y = PARAMS["center_y"] + PARAMS["sway_cm"] * math.sin(progress * 0.012)
        stamp = smoke_stamp(x, y, 40.0, PARAMS["push_speed"], 0.0, PARAMS["radius"], -1.0, PARAMS["radial_speed"])
        if subsystem.inject_combat_brush_stamp(stamp):
            received += 1
    return {"received": received, "of": substamps}


FIRE_SCENE_LABELS = {
    "torch": ("TDGen_TorchFire",),
    "campfire": ("TDGen_CampfireFire", "TDGen_CampfireSmoke"),
    "explosion": ("TDGen_ExplosionFire", "TDGen_ExplosionSmoke"),
}


def action_fire_scene_begin(world):
    awake_labels = set(FIRE_SCENE_LABELS[PARAMS["scene"]])
    states = {}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor):
        is_awake = actor.get_actor_label() in awake_labels
        actor.get_editor_property("live_component").set_simulation_awake(is_awake)
        states[actor.get_actor_label()] = is_awake
    return {"awake": states}


def action_fire_move(world):
    pawn, controller = player_pawn_and_controller(world)
    if PARAMS.get("start_x") is not None:
        teleport_pawn(world, PARAMS["start_x"], PARAMS["start_y"])
    if PARAMS.get("goal_x") is None:
        return {"placed": True}
    goal = unreal.Vector(PARAMS["goal_x"], PARAMS["goal_y"], pawn.get_actor_location().z)
    extent = unreal.Vector(80.0, 80.0, 250.0)
    has_navigation = unreal.NavigationSystemV1.project_point_to_navigation(world, goal, None, None, extent) is not None
    if not has_navigation:
        pawn.set_actor_location(goal, False, True)
        return {"goal": [goal.x, goal.y], "mode": "teleport"}
    unreal.AIHelperLibrary.simple_move_to_location(controller, goal)
    return {"goal": [goal.x, goal.y], "mode": "navigation"}


def action_fire_walk_speed(world):
    pawn, controller = player_pawn_and_controller(world)
    if not hasattr(pawn, "set_max_move_speed"):
        return {"error": "폰에 set_max_move_speed 가 없다"}
    pawn.set_max_move_speed(PARAMS["speed"])
    return {"max_move_speed": pawn.get_max_move_speed()}


def action_fire_camera(world):
    pawn, controller = player_pawn_and_controller(world)
    boom = pawn.get_component_by_class(unreal.SpringArmComponent)
    if boom is None:
        return {"error": "폰에 스프링 암이 없다"}
    boom.set_editor_property("target_arm_length", PARAMS["arm_length"])
    return {"target_arm_length": boom.get_editor_property("target_arm_length")}


def action_fire_trigger(world):
    emitters = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidFragmentEmitterActor))
    if not emitters:
        return {"error": "조각 방출 액터가 없다"}
    source = emitters[0].get_editor_property("source_breakable_actor")
    if source is None:
        return {"error": "조각 방출 액터에 파괴 액터가 연결되지 않았다"}
    center = source.get_actor_location()
    hit = unreal.Vector(center.x + PARAMS["hit_dx"], center.y + PARAMS["hit_dy"], center.z + PARAMS["hit_dz"])
    emitters[0].trigger_break_at(hit)
    return {"hit": [hit.x, hit.y, hit.z]}


def action_finish(world):
    teleport_pawn(world, 250.0, 0.0)
    return {}


def run():
    world = game_world()
    if world is None:
        return {"error": "PIE 게임 월드가 없다"}
    handler = globals().get("action_" + PARAMS["action"])
    if handler is None:
        return {"error": "알 수 없는 action: " + PARAMS["action"]}
    return handler(world)


try:
    result = run()
except Exception:
    result = {"error": traceback.format_exc()[-600:]}
print(RESULT_MARKER, json.dumps(result, ensure_ascii=False, default=str))
'''


def editor_action(action, **fields):
    params = {"action": action, **fields}
    outcome = run_in_editor.run_code(EDITOR_CODE.replace("__PARAMS__", repr(json.dumps(params))))
    if not outcome["connected"]:
        return {"error": "에디터 원격 실행에 연결하지 못했다"}
    for line in outcome["lines"]:
        index = line.find(RESULT_MARKER)
        if index >= 0:
            try:
                return json.loads(line[index + len(RESULT_MARKER):].strip().splitlines()[0])
            except ValueError:
                return {"error": "결과 해석 실패: " + line[-200:]}
    return {"error": "결과 표식 없음: " + " | ".join(outcome["lines"])[-300:]}


def capture_frame(frame_paths, frame_interval, compensate=False):
    path = FRAME_DIRECTORY / f"frame_{len(frame_paths):03d}.png"
    started = time.monotonic()
    editor_action("shot", size=SHOT_SIZE, path=path.as_posix())
    frame_paths.append(path)
    elapsed = time.monotonic() - started if compensate else 0.0
    time.sleep(max(frame_interval - elapsed, 0.0))


def run_scene(scene, frame_interval, frame_paths, log):
    start_index = len(frame_paths)
    name = scene["name"]
    if name == "walk":
        log.append(editor_action("walk_begin"))
        for _ in range(scene["frames"]):
            capture_frame(frame_paths, frame_interval)
    elif name == "combat":
        log.append(editor_action("combat_begin"))
        capture_frame(frame_paths, frame_interval)
        for hit_index in range(scene["frames"] - 1):
            should_kill = hit_index == scene["frames"] - 4
            if hit_index % 2 == 0 or should_kill:
                log.append(editor_action("combat_hit", hit_index=hit_index // 2, kill=should_kill))
            capture_frame(frame_paths, frame_interval)
    elif name == "mud":
        log.append(editor_action("mud_begin"))
        for step in range(scene["frames"]):
            log.append(editor_action("mud_stamp", step=step))
            capture_frame(frame_paths, frame_interval)
    elif name == "ramp":
        log.append(editor_action("ramp_begin"))
        for _ in range(scene["frames"]):
            capture_frame(frame_paths, frame_interval)
    elif name == "smoke_hard_walk":
        log.append(editor_action("smoke_walk_begin", start_x=-1100.0, start_y=0.0, goal_x=-200.0, goal_y=0.0))
        for _ in range(scene["frames"]):
            capture_frame(frame_paths, frame_interval)
    elif name == "smoke_soft_walk":
        log.append(editor_action("smoke_walk_begin", start_x=1100.0, start_y=0.0, goal_x=200.0, goal_y=0.0))
        for _ in range(scene["frames"]):
            capture_frame(frame_paths, frame_interval)
    elif name == "smoke_light":
        log.append(editor_action("smoke_stand_begin", x=0.0, y=0.0))
        for _ in range(scene["frames"]):
            capture_frame(frame_paths, frame_interval)
    elif name == "smoke_push":
        log.append(editor_action("smoke_stand_begin", x=-650.0, y=-330.0))
        for step in range(scene["frames"]):
            log.append(editor_action(
                "smoke_push_stamp", step=step, substamps=6, advance_cm=55.0, start_x=-950.0, center_y=0.0,
                sway_cm=110.0, push_speed=520.0, radius=110.0, radial_speed=380.0,
            ))
            capture_frame(frame_paths, frame_interval)
    elif name.startswith("fire_"):
        run_fire_scene(scene, frame_paths, log)
    return list(range(start_index, len(frame_paths)))


def run_fire_scene(scene, frame_paths, log):
    name = scene["name"]
    interval = scene["interval"]
    log.append(editor_action("fire_camera", arm_length=FIRE_CAMERA_ARM_LENGTH_CM))
    if name == "fire_torch_walk":
        log.append(editor_action("fire_scene_begin", scene="torch"))
        log.append(editor_action("fire_walk_speed", speed=DEFAULT_WALK_SPEED_CM_PER_SEC))
        for frame in range(scene["frames"]):
            if frame % FIRE_TORCH_LEG_FRAMES == 0:
                leg = frame // FIRE_TORCH_LEG_FRAMES
                start = FIRE_TORCH_CORNERS[0] if leg == 0 else (None, None)
                goal = FIRE_TORCH_CORNERS[min(leg + 1, len(FIRE_TORCH_CORNERS) - 1)]
                log.append(editor_action("fire_move", start_x=start[0], start_y=start[1], goal_x=goal[0], goal_y=goal[1]))
            capture_frame(frame_paths, interval, True)
        return
    if name == "fire_campfire":
        log.append(editor_action("fire_scene_begin", scene="campfire"))
        log.append(editor_action("fire_walk_speed", speed=FIRE_CAMPFIRE_WALK_SPEED_CM_PER_SEC))
        log.append(editor_action(
            "fire_move", start_x=FIRE_CAMPFIRE_START[0], start_y=FIRE_CAMPFIRE_START[1],
            goal_x=FIRE_CAMPFIRE_GOAL[0], goal_y=FIRE_CAMPFIRE_GOAL[1],
        ))
        for _ in range(scene["frames"]):
            capture_frame(frame_paths, interval, True)
        log.append(editor_action("fire_walk_speed", speed=DEFAULT_WALK_SPEED_CM_PER_SEC))
        return
    log.append(editor_action("fire_scene_begin", scene="explosion"))
    log.append(editor_action("fire_move", start_x=FIRE_EXPLOSION_PAWN[0], start_y=FIRE_EXPLOSION_PAWN[1], goal_x=None, goal_y=None))
    for frame in range(scene["frames"]):
        if frame == FIRE_EXPLOSION_TRIGGER_FRAME:
            log.append(editor_action(
                "fire_trigger", hit_dx=FIRE_EXPLOSION_HIT_OFFSET[0], hit_dy=FIRE_EXPLOSION_HIT_OFFSET[1], hit_dz=FIRE_EXPLOSION_HIT_OFFSET[2],
            ))
        capture_frame(frame_paths, interval, True)


def wait_for_frames(frame_paths):
    deadline = time.monotonic() + FRAME_FILE_WAIT_SECONDS
    while time.monotonic() < deadline and not all(path.exists() and path.stat().st_size > 0 for path in frame_paths):
        time.sleep(0.5)
    return [path for path in frame_paths if path.exists() and path.stat().st_size > 0]


def open_stage_level(level_path):
    code = (
        "import unreal, json\n"
        "def open_stage():\n"
        "    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)\n"
        "    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)\n"
        f"    exists = unreal.EditorAssetLibrary.does_asset_exist('{level_path}')\n"
        "    pie_running = editor.get_game_world() is not None\n"
        f"    loaded = exists and not pie_running and (editor.get_editor_world().get_path_name().startswith('{level_path}') or levels.load_level('{level_path}'))\n"
        f"    print('{pie_check_fluid.RESULT_MARKER}', json.dumps({{'exists': exists, 'pie_running': pie_running, 'loaded': bool(loaded), 'world': editor.get_editor_world().get_path_name()}}))\n"
        "open_stage()\n"
    )
    return pie_check_fluid.parse_marker(pie_check_fluid.run_editor_code(code), pie_check_fluid.RESULT_MARKER)


def build_gif(saved_frames, gif_path=GIF_PATH, frame_ms=GIF_FRAME_MS):
    images = []
    for path in saved_frames:
        with Image.open(path) as image:
            height = round(image.height * GIF_WIDTH / image.width)
            images.append(image.convert("RGB").resize((GIF_WIDTH, height), Image.LANCZOS).quantize(colors=128))
    gif_path.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(gif_path, save_all=True, append_images=images[1:], duration=frame_ms, loop=0, optimize=True)


def build_sheet(scene_frame_indices, frame_paths, sheet_path=SHEET_PATH):
    last_frames = [frame_paths[indices[-1]] for indices in scene_frame_indices if indices and frame_paths[indices[-1]].exists()]
    if not last_frames:
        return False
    tiles = [Image.open(path).convert("RGB") for path in last_frames]
    tile_width, tile_height = tiles[0].size
    columns = 2
    rows = (len(tiles) + columns - 1) // columns
    sheet = Image.new("RGB", (tile_width * columns, tile_height * rows))
    for index, tile in enumerate(tiles):
        sheet.paste(tile.resize((tile_width, tile_height)), ((index % columns) * tile_width, (index // columns) * tile_height))
        tile.close()
    sheet.save(sheet_path)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", choices=tuple(STAGES), default="arena", help="arena=기존 4장면(기본), smoke=볼륨 연기 무대 LV_TDFluidSmokeStage, fire=불·폭발 무대 LV_TDFluidFireStage")
    parser.add_argument("--stop-pie", action="store_true", help="끝난 뒤 PIE를 끈다(기본은 켜 둔다)")
    parser.add_argument("--frame-interval", type=float, default=0.4)
    arguments = parser.parse_args()
    print("경고: 측정 동안 UnrealEditor 창을 앞창으로 두고 건드리지 않는다(L-editor-06).", flush=True)
    FRAME_DIRECTORY.mkdir(parents=True, exist_ok=True)

    stage = STAGES[arguments.stage]
    level_status = open_stage_level(stage["level_path"]) if stage["level_path"] else pie_check_fluid.open_arena_level()
    if not level_status.get("loaded"):
        print("레벨을 열지 못했다:", json.dumps(level_status, ensure_ascii=False))
        return EXIT_ENVIRONMENT
    try:
        session = uemcp.connect()
    except (OSError, ValueError, KeyError) as error:
        print("MCP 서버에 연결하지 못했다:", error)
        return EXIT_ENVIRONMENT
    environment = {"navigation_ready": pie_check_fluid.wait_for_editor_navigation()}
    if not pie_check_fluid.start_pie(session, environment) or not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
        print("PIE를 시작하지 못했다:", json.dumps(environment, ensure_ascii=False)[:300])
        return EXIT_ENVIRONMENT
    time.sleep(3.0)

    frame_paths = []
    scene_log = []
    scene_frame_indices = []
    for scene in stage["scenes"]:
        print("장면", scene["name"], "-", scene["title"], flush=True)
        scene_frame_indices.append(run_scene(scene, arguments.frame_interval, frame_paths, scene_log))
    scene_log.append(editor_action("finish"))
    saved_frames = wait_for_frames(frame_paths)
    print(f"프레임 {len(saved_frames)}/{len(frame_paths)} 저장 -> {FRAME_DIRECTORY.relative_to(ROOT).as_posix()}", flush=True)
    errors = [entry["error"] for entry in scene_log if isinstance(entry, dict) and "error" in entry]
    for error in errors[:5]:
        print("오류:", error[-300:])

    if arguments.stop_pie:
        pie_check_fluid.stop_pie(session, environment)
    if len(saved_frames) < len(frame_paths) // 2:
        print("프레임이 절반 미만이라 GIF를 만들지 않았다")
        return EXIT_TOO_FEW_FRAMES
    build_gif(saved_frames, stage["gif_path"], stage["frame_ms"])
    has_sheet = build_sheet(scene_frame_indices, frame_paths, stage["sheet_path"])
    print("GIF", stage["gif_path"].relative_to(ROOT).as_posix(), "시트", stage["sheet_path"].relative_to(ROOT).as_posix() if has_sheet else "없음")
    print("PIE", "종료" if arguments.stop_pie else "켜 둠 — 에디터 창에서 직접 조작 가능(왼클릭 이동, Esc로 PIE 종료)")
    return 0 if not errors else EXIT_TOO_FEW_FRAMES


if __name__ == "__main__":
    sys.exit(main())
