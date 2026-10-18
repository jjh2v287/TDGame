"""실행 환경: TDGame 언리얼 에디터 안(C++ ATDArenaEncounter가 컴파일된 뒤). 원형 보스 아레나 맵과 조우 정의 에셋을 만든다.
실행 명령: python Tools/run_in_editor.py Tools/MonsterAI/editor_make_boss_arena.py  (맵을 다시 만들 때는 먼저 -c "import os; os.environ['TD_BOSS_ARENA_REBUILD']='1'")
출력: /Game/MonsterAI/Maps/LV_TDBossArena, /Game/MonsterAI/Encounters/DA_TDArena_DragonLair, 재질 3종, 결과 JSON 한 줄
상태: 현행(M3-36). 맵이 있으면 정의 에셋 웨이브만 갱신하고 맵은 보존한다. 종은 SPECIES 표에서 바꾼다(중간 보스 Centaur_Hunter·보스 Dragon_Boss).
"""
import json
import math
import os

import unreal


ARENA_PATH = "/Game/MonsterAI/Maps/LV_TDBossArena"
DEFINITION_DIRECTORY = "/Game/MonsterAI/Encounters"
DEFINITION_NAME = "DA_TDArena_DragonLair"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
MATERIAL_DIRECTORY = "/Game/MonsterAI/Maps"
FLOOR_MATERIAL_PATH = MATERIAL_DIRECTORY + "/M_TDBossArenaFloor"
WALL_MATERIAL_PATH = MATERIAL_DIRECTORY + "/M_TDBossArenaWall"
GATE_MATERIAL_PATH = MATERIAL_DIRECTORY + "/M_TDBossArenaGate"

ARENA_RADIUS_CM = 1400.0
WALL_SEGMENT_COUNT = 32
WALL_HEIGHT_CM = 300.0
WALL_THICKNESS_CM = 60.0
CORRIDOR_LENGTH_CM = 1500.0
CORRIDOR_WIDTH_CM = 600.0
GAP_HALF_ANGLE_DEGREES = 12.0
ENTRY_ZONE_INSET_CM = 350.0
CHECKPOINT_OUTSET_CM = 600.0

SPECIES = {
    "minion_a": "/Game/MonsterAI/Species/DA_TDMonster_Goblin.DA_TDMonster_Goblin",
    "minion_b": "/Game/MonsterAI/Species/DA_TDMonster_Hyena.DA_TDMonster_Hyena",
    "mid_boss": "/Game/MonsterAI/Species/DA_TDMonster_CentaurHunter.DA_TDMonster_CentaurHunter",
    "boss": "/Game/MonsterAI/Species/DA_TDMonster_MountainDragon.DA_TDMonster_MountainDragon",
}
MID_BOSS_NAME = "켄타우로스 사냥꾼"
MID_BOSS_MARKS = [0.5]
BOSS_NAME = "산룡"
BOSS_MARKS = [0.7, 0.4, 0.25]
REWARD_TEXT = "보상: 산룡의 비늘 ×3 · 금화 1,200 (표시 전용)"


def create_matte_material(asset_path, color):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(asset_path):
        material = assets.load_asset(asset_path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"기존 에셋이 Material이 아닙니다: {asset_path}")
        return material

    asset_directory, asset_name = asset_path.rsplit("/", 1)
    if not assets.does_directory_exist(asset_directory) and not assets.make_directory(asset_directory):
        raise RuntimeError(f"재질 폴더 생성 실패: {asset_directory}")
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, asset_directory, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"재질 생성 실패: {asset_path}")

    editing = unreal.MaterialEditingLibrary
    base_color = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -240, -120)
    roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -240, 0)
    metallic = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -240, 120)
    if base_color is None or roughness is None or metallic is None:
        raise RuntimeError(f"재질 노드 생성 실패: {asset_path}")
    base_color.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
    roughness.set_editor_property("r", 0.85)
    metallic.set_editor_property("r", 0.0)
    for expression, material_property in (
        (base_color, unreal.MaterialProperty.MP_BASE_COLOR),
        (roughness, unreal.MaterialProperty.MP_ROUGHNESS),
        (metallic, unreal.MaterialProperty.MP_METALLIC),
    ):
        if not editing.connect_material_property(expression, "", material_property):
            raise RuntimeError(f"재질 연결 실패: {asset_path}, {material_property}")
    errors = editing.recompile_material(material)
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: {asset_path}, {list(errors)}")
    if not assets.save_loaded_asset(material):
        raise RuntimeError(f"재질 저장 실패: {asset_path}")
    return material


def load_species(key):
    species = unreal.EditorAssetLibrary.load_asset(SPECIES[key])
    if species is None:
        raise RuntimeError(f"종 에셋 없음: {key} = {SPECIES[key]}")
    return species


def make_entry(species_key, count, anchor, distance_cm, spread_degrees, boss_name=None, boss_marks=None):
    entry = unreal.TDArenaSpawnEntry()
    entry.set_editor_property("species", load_species(species_key))
    entry.set_editor_property("count", count)
    entry.set_editor_property("anchor", anchor)
    entry.set_editor_property("distance_cm", distance_cm)
    entry.set_editor_property("spread_degrees", spread_degrees)
    if boss_name:
        entry.set_editor_property("shows_boss_health_bar", True)
        entry.set_editor_property("boss_display_name", unreal.Text(boss_name))
        entry.set_editor_property("boss_phase_health_marks", list(boss_marks or []))
    return entry


def make_wave(title, delay_seconds, entries):
    wave = unreal.TDArenaWave()
    wave.set_editor_property("title", unreal.Text(title))
    wave.set_editor_property("start_delay_seconds", delay_seconds)
    wave.set_editor_property("entries", entries)
    return wave


def build_definition():
    assets = unreal.EditorAssetLibrary
    asset_path = f"{DEFINITION_DIRECTORY}/{DEFINITION_NAME}"
    if assets.does_asset_exist(asset_path):
        definition = assets.load_asset(asset_path)
    else:
        if not assets.does_directory_exist(DEFINITION_DIRECTORY) and not assets.make_directory(DEFINITION_DIRECTORY):
            raise RuntimeError(f"정의 폴더 생성 실패: {DEFINITION_DIRECTORY}")
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.TDArenaEncounterDefinition)
        definition = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            DEFINITION_NAME, DEFINITION_DIRECTORY, unreal.TDArenaEncounterDefinition, factory
        )
    if not isinstance(definition, unreal.TDArenaEncounterDefinition):
        raise RuntimeError(f"조우 정의 생성 실패: {asset_path}")

    around = unreal.TDArenaSpawnAnchor.AROUND_CHALLENGER
    center = unreal.TDArenaSpawnAnchor.ARENA_CENTER
    waves = [
        make_wave("졸개 웨이브 1", 2.5, [make_entry("minion_a", 4, around, 800.0, 120.0)]),
        make_wave("졸개 웨이브 2", 3.0, [
            make_entry("minion_a", 3, around, 700.0, 90.0),
            make_entry("minion_b", 3, around, 1000.0, 160.0),
        ]),
        make_wave("중간 보스", 3.0, [make_entry("mid_boss", 1, center, 0.0, 0.0, MID_BOSS_NAME, MID_BOSS_MARKS)]),
        make_wave("산룡의 둥지", 3.5, [make_entry("boss", 1, center, 0.0, 0.0, BOSS_NAME, BOSS_MARKS)]),
    ]
    definition.set_editor_property("encounter_id", "DragonLair")
    definition.set_editor_property("display_name", unreal.Text("산룡의 둥지"))
    definition.set_editor_property("waves", waves)
    definition.set_editor_property("victory_reward_text", unreal.Text(REWARD_TEXT))
    definition.set_editor_property("failure_reset_seconds", 3.0)
    definition.set_editor_property("banner_seconds", 3.0)
    definition.set_editor_property("victory_banner_seconds", 6.0)
    if not assets.save_loaded_asset(definition, only_if_is_dirty=False):
        raise RuntimeError(f"조우 정의 저장 실패: {asset_path}")
    return definition


def ensure_editor_is_clean():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    if dirty:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(package.get_name() for package in dirty))


def build_map(definition):
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    game_mode = assets.load_blueprint_class(GAME_MODE_PATH)
    cube = assets.load_asset("/Engine/BasicShapes/Cube")
    cylinder = assets.load_asset("/Engine/BasicShapes/Cylinder")
    encounter_class = unreal.load_class(None, "/Script/TDGame.TDArenaEncounter")
    required = {"게임 모드": game_mode, "큐브": cube, "원기둥": cylinder, "조우 클래스": encounter_class}
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음: " + ", ".join(missing))

    floor_material = create_matte_material(FLOOR_MATERIAL_PATH, (0.09, 0.08, 0.07))
    wall_material = create_matte_material(WALL_MATERIAL_PATH, (0.12, 0.11, 0.10))
    gate_material = create_matte_material(GATE_MATERIAL_PATH, (0.45, 0.08, 0.04))
    if not levels.new_level(ARENA_PATH):
        raise RuntimeError(f"맵 생성 실패: {ARENA_PATH}")
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    created = []

    def finish(actor, label, folder):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label("TDGen_" + label)
        actor.set_folder_path("TDGen/BossArena/" + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator())
        return finish(actor, label, folder)

    def place_mesh(mesh, location, scale, material, label, folder="Arena", yaw=0.0, mobility=unreal.ComponentMobility.STATIC):
        actor = actors.spawn_actor_from_object(mesh, unreal.Vector(*location), unreal.Rotator(yaw=yaw))
        finish(actor, label, folder)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        component = actor.static_mesh_component
        component.set_editor_property("mobility", mobility)
        component.set_material(0, material)
        return actor

    radius = ARENA_RADIUS_CM
    floor_diameter = 2.0 * (radius + 250.0)
    place_mesh(cylinder, (0, 0, -25), (floor_diameter / 100.0, floor_diameter / 100.0, 0.5), floor_material, "ArenaFloor")
    corridor_center = radius + CORRIDOR_LENGTH_CM * 0.5 - 100.0
    for label, sign in (("EntranceCorridor", -1.0), ("ExitCorridor", 1.0)):
        x = sign * corridor_center
        place_mesh(cube, (x, 0, -25), (CORRIDOR_LENGTH_CM / 100.0, CORRIDOR_WIDTH_CM / 100.0, 0.5), floor_material, label + "Floor")
        for side in (-1.0, 1.0):
            y = side * (CORRIDOR_WIDTH_CM * 0.5 + WALL_THICKNESS_CM * 0.5)
            place_mesh(cube, (x, y, WALL_HEIGHT_CM * 0.5), (CORRIDOR_LENGTH_CM / 100.0, WALL_THICKNESS_CM / 100.0, WALL_HEIGHT_CM / 100.0), wall_material, f"{label}Wall_{'L' if side < 0 else 'R'}")
        cap_x = sign * (radius + CORRIDOR_LENGTH_CM - 100.0)
        place_mesh(cube, (cap_x, 0, WALL_HEIGHT_CM * 0.5), (WALL_THICKNESS_CM / 100.0, CORRIDOR_WIDTH_CM / 100.0 + 1.2, WALL_HEIGHT_CM / 100.0), wall_material, label + "Cap")

    wall_radius = radius + 100.0
    segment_angle = 360.0 / WALL_SEGMENT_COUNT
    segment_length = 2.0 * math.pi * wall_radius / WALL_SEGMENT_COUNT * 1.06
    wall_count = 0
    for index in range(WALL_SEGMENT_COUNT):
        angle = (index + 0.5) * segment_angle
        distance_to_exit = min(angle, 360.0 - angle)
        distance_to_entrance = abs(angle - 180.0)
        if distance_to_exit < GAP_HALF_ANGLE_DEGREES or distance_to_entrance < GAP_HALF_ANGLE_DEGREES:
            continue
        radians = math.radians(angle)
        location = (wall_radius * math.cos(radians), wall_radius * math.sin(radians), WALL_HEIGHT_CM * 0.5)
        place_mesh(cube, location, (WALL_THICKNESS_CM / 100.0, segment_length / 100.0, WALL_HEIGHT_CM / 100.0), wall_material, f"RingWall_{index:02d}", yaw=angle)
        wall_count += 1

    gap_width = 2.0 * wall_radius * math.sin(math.radians(GAP_HALF_ANGLE_DEGREES)) + 120.0
    gate_scale = (WALL_THICKNESS_CM / 100.0 + 0.2, gap_width / 100.0, WALL_HEIGHT_CM / 100.0)
    entrance_gate = place_mesh(cube, (-wall_radius, 0, WALL_HEIGHT_CM * 0.5), gate_scale, gate_material, "EntranceGate", "Gates", mobility=unreal.ComponentMobility.MOVABLE)
    exit_gate = place_mesh(cube, (wall_radius, 0, WALL_HEIGHT_CM * 0.5), gate_scale, gate_material, "ExitGate", "Gates", mobility=unreal.ComponentMobility.MOVABLE)
    for gate in (entrance_gate, exit_gate):
        gate.static_mesh_component.set_editor_property("can_ever_affect_navigation", False)

    for index in range(6):
        radians = math.radians(30.0 + index * 60.0)
        lamp = spawn(unreal.PointLight, ((radius - 120.0) * math.cos(radians), (radius - 120.0) * math.sin(radians), 260), f"Brazier_{index}", "Lighting")
        lamp.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        lamp.light_component.set_intensity(400.0)
        lamp.light_component.set_attenuation_radius(900.0)
        lamp.light_component.set_cast_shadows(False)
        lamp.light_component.set_light_color(unreal.LinearColor(1.0, 0.45, 0.15, 1.0))

    sun = spawn(unreal.DirectionalLight, (0, 0, 1500), "ArenaSun", "Lighting", unreal.Rotator(pitch=-55, yaw=-35))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(4.0)
    sun.light_component.set_light_color(unreal.LinearColor(0.95, 0.85, 0.75, 1.0))
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), "ArenaAtmosphere", "Lighting")
    sky = spawn(unreal.SkyLight, (0, 0, 1000), "ArenaSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(0.7)

    post_process = spawn(unreal.PostProcessVolume, (0, 0, 0), "ArenaExposure", "Lighting")
    post_process.set_editor_property("unbound", True)
    settings = post_process.settings
    for name, value in (
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
        ("auto_exposure_apply_physical_camera_exposure", False),
        ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.35),
        ("vignette_intensity", 0.25),
    ):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post_process.set_editor_property("settings", settings)

    player_start_x = -(radius + CORRIDOR_LENGTH_CM - 400.0)
    spawn(unreal.PlayerStart, (player_start_x, 0, 96), "ArenaPlayerStart", "Gameplay")

    encounter = spawn(encounter_class, (0, 0, 0), "DragonLairEncounter", "Gameplay")
    encounter.set_editor_property("definition", definition)
    encounter.set_editor_property("arena_radius_cm", radius)
    encounter.set_editor_property("seal_gate_actors", [entrance_gate])
    encounter.set_editor_property("exit_gate_actors", [exit_gate])
    encounter.entry_zone.set_editor_property("relative_location", unreal.Vector(-radius + ENTRY_ZONE_INSET_CM, 0, 200))
    encounter.checkpoint_arrow.set_editor_property("relative_location", unreal.Vector(-radius - CHECKPOINT_OUTSET_CM, 0, 100))

    navigation = spawn(unreal.NavMeshBoundsVolume, (0, 0, 200), "ArenaNavigation", "Gameplay")
    half_length = radius + CORRIDOR_LENGTH_CM + 100.0
    navigation.set_actor_scale3d(unreal.Vector(half_length / 100.0, (radius + 300.0) / 100.0, 4))
    nav_origin, nav_extent = navigation.get_actor_bounds(False)
    if nav_extent.x < radius + CORRIDOR_LENGTH_CM - 100.0 or nav_extent.y < radius + 200.0:
        raise RuntimeError(f"내비게이션 브러시 범위 검증 실패: {nav_extent}")
    for nav_mesh in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
        nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        nav_mesh.set_editor_property("is_world_partitioned", False)

    if not levels.save_current_level():
        raise RuntimeError(f"맵 저장 실패: {ARENA_PATH}")
    return {
        "created_actors": len(created),
        "ring_wall_segments": wall_count,
        "gate_width_cm": round(gap_width, 1),
        "player_start": [player_start_x, 0, 96],
        "checkpoint": [-radius - CHECKPOINT_OUTSET_CM, 0, 100],
        "entry_zone_center": [-radius + ENTRY_ZONE_INSET_CM, 0, 200],
        "navigation_extent": [nav_extent.x, nav_extent.y, nav_extent.z],
    }


def main():
    assets = unreal.EditorAssetLibrary
    ensure_editor_is_clean()
    should_rebuild = os.environ.get("TD_BOSS_ARENA_REBUILD") == "1"
    definition = build_definition()
    map_report = {"map_kept": True}
    if assets.does_asset_exist(ARENA_PATH) and should_rebuild:
        if not assets.delete_asset(ARENA_PATH):
            raise RuntimeError(f"기존 맵 삭제 실패: {ARENA_PATH}")
    if not assets.does_asset_exist(ARENA_PATH):
        map_report = build_map(definition)
    print(json.dumps({
        "map": ARENA_PATH,
        "definition": definition.get_path_name(),
        "waves": len(definition.get_editor_property("waves")),
        "arena_radius_cm": ARENA_RADIUS_CM,
        "species": SPECIES,
        **map_report,
        "saved": True,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
