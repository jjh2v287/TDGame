"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). W-1b 물 부유물·물보라 무대 맵 2개를 만든다: 바다 WP1 LV_TDFluidWaterParticleSeaStage(40 m 평바닥 -250 cm, 수면 판 3x3·Sea 물 액터, 교반기 2개(수직 공전 반경 120 cm·Pitch 120도/초, 수평 스침 반경 150 cm·Yaw 180도/초), 물리 공 3개)와 여울 WP2 LV_TDFluidWaterParticleRapidsStage(직선 30 m·폭 6 m·기울기 3 %·수심 35 cm 강 액터, 유속 350, 장애물 바위 2개, River 물 액터). 맵마다 입자 액터 ATDFluidWaterParticleActor 1개를 두고 맵이 둘인 이유는 물 액터 둘째가 영구 휴면이 되기 때문이다(X-18). 먼저 editor_make_fluid_water_material.py·editor_make_fluid_water_particle_assets.py 와 나이아가라 3종 생성(editor_make_fluid_driven_niagara.py)이 끝나 있어야 하고 모양 조각 병합(A0)이 모든 무대보다 앞서야 한다(L-repo-27).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_water_particle_stage.py (PowerShell, L-repo-01). 맵마다 요청 파일이 따로다: Saved/FluidFX/fluid_water_particle_sea_stage_request.json·fluid_water_particle_rapids_stage_request.json 의 {"overwrite": true} 가 있을 때만 그 맵과 프리셋을 갱신하고, 없으면 그 맵은 기존 에셋을 보존하며 건너뛴다. 교반기 블루프린트 BP_TDFluidStageStirrer 가 없으면 editor_make_fluid_particle_stage.py 의 함수로 만든다.
출력: /Game/FluidFX/Presets/DA_TDFluid_WaterParticleSea·DA_TDFluid_WaterParticleRiver, /Game/FluidFX/Maps/LV_TDFluidWaterParticleSeaStage·LV_TDFluidWaterParticleRapidsStage, Saved/FluidFX/fluid_water_particle_sea_stage.json·fluid_water_particle_rapids_stage.json(preset_tuning_sha256·material_look_sha256, 장면별 cd_ids·niagara_system·kind·카메라 제안). 판정은 출력 본문의 TD_WATER_PARTICLE_STAGE_WP1·WP2 PASS|KEPT|FAIL 줄로 한다(L-editor-73).
상태: 미검증(2026-10-29 W-1b D 작성: py_compile 과 가짜 unreal 시험만, 에디터 실행 전). 입자 액터 속성 이름(water_actor 등)은 C2 의 C++ 이름 snake 변환 가정이라 첫머리에서 점검하고, 강 배치·물 액터 설정·프리셋 보조는 editor_make_fluid_water_stage.py 의 함수를 importlib 로 불러 쓴다(그 파일 최상위 실행은 __main__ 가드 안). 교반기 회전은 CDO 에 SCS 컴포넌트가 없어(L-editor-65) 배치한 액터에서 설정한다. 스크립트가 맵마다 시작 때 check_fluid_preset_data 와 같은 검사와 정본 다시 읽기를 한다. 노출·카메라 값은 초안이다. 2026-10-31: 교반기 피벗이 회전 속도 쓰기 뒤 BP 구성 스크립트 재실행으로 유실되고(바다 맵에 피벗 없음) 다시 읽기가 버려진 옛 핸들을 읽어 통과하던 것을 particle_stage.set_component_properties(한 번에 쓰기·새 핸들로 다시 읽기, 다르면 FAIL)로 고침, WP1 맵 재생성 필요.
"""
import importlib.util
import json
import math
import os

import unreal

FLUID_ROOT = "/Game/FluidFX"
MAP_DIR = f"{FLUID_ROOT}/Maps"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
MESH_DIR = f"{FLUID_ROOT}/Meshes"
NIAGARA_DIR = f"{FLUID_ROOT}/Niagara"
PARTICLE_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
WATER_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_water_stage.py")
PARTICLE_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidWaterParticleActor"
PARTICLE_ACTOR_PROPERTIES = ("water_actor", "surface_material_source_actor", "debris_system", "whitewater_system", "splash_system", "contact_data_channel")
SYSTEM_PATHS = {
    "debris_system": f"{NIAGARA_DIR}/NS_TD_FluidWaterDebris",
    "whitewater_system": f"{NIAGARA_DIR}/NS_TD_FluidWaterWhitewater",
    "splash_system": f"{NIAGARA_DIR}/NS_TD_FluidWaterSplash",
}
DATA_CHANNEL_PATH = f"{NIAGARA_DIR}/DataChannels/NDC_TD_FluidWaterContact"
SEA_MATERIAL_PATH = f"{MATERIAL_DIR}/MI_TD_FluidWater_Sea"
RIVER_MATERIAL_PATH = f"{MATERIAL_DIR}/MI_TD_FluidWater_River"
STONE_MATERIAL_PATH = f"{MATERIAL_DIR}/MI_TD_FluidWaterStone"
SAND_MATERIAL_PATH = f"{MATERIAL_DIR}/MI_TD_FluidWaterSand"
TILE_MESH_PATH = f"{MESH_DIR}/SM_TD_FluidWaterGrid64"
SURFACE_MESH_PATH = f"{MESH_DIR}/SM_TD_FluidWaterGridUnit"
RIBBON_MESH_PATH = "/Engine/BasicShapes/Plane"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
SPHERE_MESH_PATH = "/Engine/BasicShapes/Sphere"
LIVE2_REFS = ["C12", "L26", "L5"]

SEA_SURFACE_Z_CM = 0.0
SEA_FLOOR_TOP_Z_CM = -250.0
SEA_FLOOR_SIZE_CM = 4000.0
SEA_FLOOR_THICKNESS_CM = 50.0
SEA_TILE_SIZE_CM = 1600.0
SEA_TILE_COUNT = 3
SEA_STIRRER_SPECS = (
    {"label": "WaterParticleStirrerVertical", "center_xy": (-500.0, 400.0), "radius_cm": 120.0, "rotation": {"pitch": 120.0}, "motion": "수직 공전(수면 교차)"},
    {"label": "WaterParticleStirrerGraze", "center_xy": (500.0, 400.0), "radius_cm": 150.0, "rotation": {"yaw": 180.0}, "motion": "수평 스침"},
)
SEA_BALL_X_CM = (-300.0, 0.0, 300.0)
SEA_BALL_Y_CM = -200.0
SEA_BALL_HEIGHT_CM = 300.0
SEA_BALL_SCALE = 0.3
SEA_BALL_COLLISION_PROFILE = "PhysicsActor"
SEA_CAMERA = {"pawn": [0.0, 0.0], "arm": 900.0, "view": [-35.0, 30.0]}

RAPIDS_LENGTH_CM = 3000.0
RAPIDS_WIDTH_CM = 600.0
RAPIDS_SLOPE = 0.03
RAPIDS_DEPTH_CM = 35.0
RAPIDS_FLOW_SPEED_CM_PER_SEC = 350.0
RAPIDS_SPLINE_SPACING_CM = 300.0
RAPIDS_GUIDE_SAMPLE_SPACING_CM = 50.0
RAPIDS_RIBBON_SEGMENT_LENGTH_CM = 400.0
RAPIDS_BED_WIDTH_CM = 1400.0
RAPIDS_BED_THICKNESS_CM = 50.0
RAPIDS_BED_END_MARGIN_CM = 150.0
RAPIDS_LENGTH_TOLERANCE_RATIO = 0.03
RAPIDS_ROCK_ABOVE_WATER_CM = 30.0
RAPIDS_ROCK_EMBED_CM = 15.0
RAPIDS_ROCK_SPECS = (
    {"name": "RockA", "x_cm": -600.0, "lateral_cm": 0.0, "scale_xy": (1.6, 1.4), "yaw_degrees": 0.0},
    {"name": "RockB", "x_cm": 300.0, "lateral_cm": -150.0, "scale_xy": (1.2, 1.1), "yaw_degrees": 35.0},
)
RAPIDS_CAMERA = {"pawn": [0.0, 0.0], "arm": 1200.0, "view": [-40.0, 200.0]}
PLAYER_START_XY_CM = {"WP1": (-1200.0, -600.0), "WP2": (-1200.0, -700.0)}

STAGE_SPECS = (
    {"scene": "WP1", "kind": "SEA", "map_path": f"{MAP_DIR}/LV_TDFluidWaterParticleSeaStage", "preset_name": "DA_TDFluid_WaterParticleSea",
     "water_label": "WaterParticleSea", "folder": "TDGen/FluidFX/WaterParticleSeaStage", "output_material_path": SEA_MATERIAL_PATH,
     "request": ("Saved", "FluidFX", "fluid_water_particle_sea_stage_request.json"), "report": ("Saved", "FluidFX", "fluid_water_particle_sea_stage.json")},
    {"scene": "WP2", "kind": "RIVER", "map_path": f"{MAP_DIR}/LV_TDFluidWaterParticleRapidsStage", "preset_name": "DA_TDFluid_WaterParticleRiver",
     "water_label": "WaterParticleRapids", "folder": "TDGen/FluidFX/WaterParticleRapidsStage", "output_material_path": RIVER_MATERIAL_PATH,
     "request": ("Saved", "FluidFX", "fluid_water_particle_rapids_stage_request.json"), "report": ("Saved", "FluidFX", "fluid_water_particle_rapids_stage.json")},
)
REQUIRED_UNREAL_NAMES = (
    "get_engine_subsystem",
    "TDFluidPresetTuningSubsystem",
    "TDFluidPresetAsset",
    "TDFluidPresetKind",
    "TDFluidPlaneSettings",
    "TDFluidPlaneOrientation.WORLD_FACING",
    "TDFluidBrushChannel.CHANNEL_A_WATER",
    "EditorAssetLibrary.load_blueprint_class",
    "EditorAssetLibrary.load_asset",
    "BlueprintFactory",
    "Blueprint",
    "DataAssetFactory",
    "RotatingMovementComponent",
    "StaticMeshComponent",
    "SplineComponent",
    "SplineCoordinateSpace.WORLD",
    "ComponentMobility.MOVABLE",
    "ComponentMobility.STATIC",
    "CollisionEnabled.NO_COLLISION",
    "AutoExposureMethod.AEM_HISTOGRAM",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "DirectionalLight",
    "SkyLight",
    "SkyAtmosphere",
    "PostProcessVolume",
    "PlayerStart",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def load_module(module_name, *relative_parts):
    module_path = project_path(*relative_parts)
    module_spec = importlib.util.spec_from_file_location(module_name, module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모듈 없음: {module_path}")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def write_report(report, relative_path):
    report_path = project_path(*relative_path)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def load_required_assets(assets, paths):
    loaded = {name: assets.load_asset(path) for name, path in paths.items()}
    missing = [paths[name] for name, value in loaded.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 없음(물 재질·나이아가라 3종·접촉 채널 생성 먼저): " + ", ".join(missing))
    return loaded


def load_optional_asset(assets, path):
    return assets.load_asset(path) if assets.does_asset_exist(path) else None


def find_contract_problems(particle_stage, water_stage):
    problems = []
    particle_class = unreal.load_class(None, PARTICLE_ACTOR_CLASS_PATH)
    if particle_class is None:
        problems.append("클래스 " + PARTICLE_ACTOR_CLASS_PATH)
    else:
        problems += particle_stage.find_missing_properties(unreal.get_default_object(particle_class), PARTICLE_ACTOR_PROPERTIES)
    for class_name, property_names in water_stage.RIVER_OBJECT_CONTRACTS.items():
        actor_class = unreal.load_class(None, water_stage.CLASS_PATHS[class_name])
        if actor_class is None:
            problems.append("클래스 " + water_stage.CLASS_PATHS[class_name])
            continue
        problems += [f"{class_name}.{name}" for name in particle_stage.find_missing_properties(unreal.get_default_object(actor_class), property_names)]
    for kind_name in {spec["kind"] for spec in STAGE_SPECS}:
        if not hasattr(unreal.TDFluidPresetKind, kind_name):
            problems.append("TDFluidPresetKind." + kind_name)
    return problems


def configure_water_preset(spec, water_stage, surface_material, surface_mesh):
    preset_path = f"{water_stage.PRESET_DIR}/{spec['preset_name']}"
    preset = water_stage.load_or_create_preset(spec["preset_name"], preset_path)
    preset.set_editor_property("preset_kind", getattr(unreal.TDFluidPresetKind, spec["kind"]))
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_A_WATER)
    plane = unreal.TDFluidPlaneSettings()
    plane.set_editor_property("orientation", unreal.TDFluidPlaneOrientation.WORLD_FACING)
    plane.set_editor_property("camera_depth_bias_cm", 0.0)
    plane.set_editor_property("center_offset_along_up_ratio", 0.0)
    preset.set_editor_property("plane_settings", plane)
    preset.set_editor_property("output_material", surface_material)
    preset.set_editor_property("surface_mesh", surface_mesh)
    preset.set_editor_property("translucency_sort_priority", 0)
    effective = preset.resolve_effective_config()
    extent = effective.get_editor_property("sim_extent_cm")
    window_cm = 2.0 * water_stage.WINDOW_HALF_CM
    problems = []
    if not effective.get_editor_property("enable_water_layer"):
        problems.append("enable_water_layer")
    if abs(extent.x - window_cm) > 0.01 or abs(extent.y - window_cm) > 0.01:
        problems.append("sim_extent_cm")
    if spec["kind"] == "RIVER" and int(effective.get_editor_property("grid_resolution")) != 192:
        problems.append("grid_resolution")
    if problems:
        raise RuntimeError(f"유효 구성이 명세와 다릅니다(PJ 프리셋 통합·에디터 재시작 필요): {preset_path} {problems}")
    water_stage.save_asset(preset, preset_path)
    summary = {name: effective.get_editor_property(name) for name in water_stage.RIVER_EFFECTIVE_REPORT_NAMES if water_stage.has_editor_property(effective, name)}
    return preset, preset_path, effective, summary


def make_spawners(particle_stage, actors, created, folder):
    def spawn(actor_class, location, label, subfolder, rotation=None):
        actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator())
        return particle_stage.finish_actor(actor, label, f"{folder}/{subfolder}", created)

    def spawn_object(asset, location, label, subfolder, rotation=None):
        actor = actors.spawn_actor_from_object(asset, unreal.Vector(*location), rotation or unreal.Rotator())
        return particle_stage.finish_actor(actor, label, f"{folder}/{subfolder}", created)

    return spawn, spawn_object


def spawn_lighting(water_stage, spawn):
    sun = spawn(unreal.DirectionalLight, (0, 0, 1500), "WaterParticleSun", "Lighting", unreal.Rotator(pitch=water_stage.SUN_ROTATION[0], yaw=water_stage.SUN_ROTATION[1]))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(water_stage.SUN_LUX)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), "WaterParticleSkyAtmosphere", "Lighting")
    sky = spawn(unreal.SkyLight, (0, 0, 1000), "WaterParticleSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(1.0)
    water_stage.configure_post_process(spawn(unreal.PostProcessVolume, (0, 0, 0), "WaterParticleExposure", "Lighting"))
    return water_stage.EXPOSURE_EV100


def spawn_static_box(spawn_object, cube_mesh, center, scale, label, material, rotation=None):
    box = spawn_object(cube_mesh, center, label, "Stage", rotation)
    box.set_actor_scale3d(unreal.Vector(*scale))
    component = box.static_mesh_component
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    if material is not None:
        component.set_material(0, material)
    return box


def spawn_player_start(spawn, scene, ground_z_cm, clearance_cm):
    x_cm, y_cm = PLAYER_START_XY_CM[scene]
    spawn(unreal.PlayerStart, (x_cm, y_cm, ground_z_cm + clearance_cm), "WaterParticlePlayerStart", "Gameplay")
    return [x_cm, y_cm, ground_z_cm + clearance_cm]


def spawn_particle_actor(spawn, resources, water, surface_source, label):
    particle_class = unreal.load_class(None, PARTICLE_ACTOR_CLASS_PATH)
    actor = spawn(particle_class, (0.0, 0.0, 0.0), label, "Particles")
    actor.set_editor_property("water_actor", water)
    actor.set_editor_property("surface_material_source_actor", surface_source)
    for property_name in SYSTEM_PATHS:
        actor.set_editor_property(property_name, resources[property_name])
    actor.set_editor_property("contact_data_channel", resources["contact_data_channel"])
    unset = [name for name in ("water_actor", "surface_material_source_actor") + tuple(SYSTEM_PATHS) + ("contact_data_channel",) if actor.get_editor_property(name) is None]
    if unset:
        raise RuntimeError("입자 액터 속성 설정 확인 실패: " + ", ".join(unset))
    return actor


def place_orbiting_stirrer(particle_stage, spawn_actor, stirrer_class, stirrer_spec, center_z_cm):
    radius_cm = stirrer_spec["radius_cm"]
    center_x_cm, center_y_cm = stirrer_spec["center_xy"]
    stirrer = spawn_actor(stirrer_class, (center_x_cm + radius_cm, center_y_cm, center_z_cm), stirrer_spec["label"], "Stirrers")
    rate = unreal.Rotator(roll=0.0, pitch=stirrer_spec["rotation"].get("pitch", 0.0), yaw=stirrer_spec["rotation"].get("yaw", 0.0))
    particle_stage.set_component_properties(stirrer, unreal.RotatingMovementComponent, {"rotation_rate": rate, "pivot_translation": unreal.Vector(-radius_cm, 0.0, 0.0)})
    degrees_per_sec = max(abs(rate.pitch), abs(rate.yaw))
    return {"label": stirrer.get_actor_label(), "motion": stirrer_spec["motion"], "radius_cm": radius_cm, "degrees_per_sec": degrees_per_sec,
            "crossing_speed_cm_per_sec": radius_cm * math.radians(degrees_per_sec), "center": [center_x_cm, center_y_cm, center_z_cm]}


def spawn_physics_balls(spawn_object, sphere_mesh):
    records = []
    for index, x_cm in enumerate(SEA_BALL_X_CM):
        ball = spawn_object(sphere_mesh, (x_cm, SEA_BALL_Y_CM, SEA_SURFACE_Z_CM + SEA_BALL_HEIGHT_CM), f"WaterParticleBall{index}", "Balls")
        ball.set_actor_scale3d(unreal.Vector(SEA_BALL_SCALE, SEA_BALL_SCALE, SEA_BALL_SCALE))
        component = ball.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        component.set_collision_profile_name(SEA_BALL_COLLISION_PROFILE)
        component.set_editor_property("generate_overlap_events", True)
        component.set_simulate_physics(True)
        component.set_enable_gravity(True)
        records.append({"label": ball.get_actor_label(), "location": [x_cm, SEA_BALL_Y_CM, SEA_SURFACE_Z_CM + SEA_BALL_HEIGHT_CM], "diameter_cm": 100.0 * SEA_BALL_SCALE})
    return records


def spawn_sea_tiles(water_stage, spawn_object, tile_mesh, sea_material):
    low_cm = -0.5 * SEA_TILE_SIZE_CM * SEA_TILE_COUNT
    first_tile = None
    for column in range(SEA_TILE_COUNT):
        for row in range(SEA_TILE_COUNT):
            location = (low_cm + SEA_TILE_SIZE_CM * (column + 0.5), low_cm + SEA_TILE_SIZE_CM * (row + 0.5), SEA_SURFACE_Z_CM)
            tile = spawn_object(tile_mesh, location, f"WaterParticleSeaTile_{column}_{row}", "WaterTiles")
            component = tile.static_mesh_component
            component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            component.set_cast_shadow(False)
            component.set_material(0, sea_material)
            water_stage.add_actor_tag(tile, water_stage.TAG_WATER_SURFACE)
            water_stage.add_actor_tag(tile, water_stage.TAG_WATER_REGION)
            if first_tile is None:
                first_tile = tile
    return first_tile, {"tile_count": SEA_TILE_COUNT * SEA_TILE_COUNT, "size_cm": SEA_TILE_SIZE_CM, "extent_cm": SEA_TILE_SIZE_CM * SEA_TILE_COUNT, "surface_z_cm": SEA_SURFACE_Z_CM}


def build_sea_scene(context):
    water_stage, resources = context["water_stage"], context["resources"]
    spawn, spawn_object = context["spawn"], context["spawn_object"]
    exposure_ev100 = spawn_lighting(water_stage, spawn)
    floor_center = (0.0, 0.0, SEA_FLOOR_TOP_Z_CM - 0.5 * SEA_FLOOR_THICKNESS_CM)
    floor_scale = (SEA_FLOOR_SIZE_CM / 100.0, SEA_FLOOR_SIZE_CM / 100.0, SEA_FLOOR_THICKNESS_CM / 100.0)
    spawn_static_box(spawn_object, resources["cube"], floor_center, floor_scale, "WaterParticleSeaFloor", resources.get("sand"))
    first_tile, tile_record = spawn_sea_tiles(water_stage, spawn_object, resources["tile_mesh"], resources["surface_material"])
    water = spawn(resources["water_actor_class"], (0.0, 0.0, SEA_SURFACE_Z_CM), context["spec"]["water_label"], "Water")
    water_properties = water_stage.configure_water_actor(water, context["preset"], context["effective"])
    stirrers = [place_orbiting_stirrer(context["particle_stage"], spawn, context["stirrer_class"], stirrer_spec, SEA_SURFACE_Z_CM) for stirrer_spec in SEA_STIRRER_SPECS]
    balls = spawn_physics_balls(spawn_object, resources["sphere"])
    player_start = spawn_player_start(spawn, "WP1", SEA_FLOOR_TOP_Z_CM, water_stage.PLAYER_START_CLEARANCE_CM)
    particle_actor = spawn_particle_actor(spawn, resources, water, first_tile, "WaterParticleSeaParticles")
    return {
        "camera": SEA_CAMERA, "water": water, "particle_actor": particle_actor, "water_properties": water_properties, "exposure_ev100": exposure_ev100,
        "layout": {"floor_top_z_cm": SEA_FLOOR_TOP_Z_CM, "floor_size_cm": SEA_FLOOR_SIZE_CM, "surface_z_cm": SEA_SURFACE_Z_CM, "tiles": tile_record,
                   "stirrers": stirrers, "balls": balls, "player_start": player_start, "surface_material_source": first_tile.get_actor_label()},
    }


def rapids_surface_z_cm(x_cm):
    return -RAPIDS_SLOPE * x_cm


def rapids_bed_z_cm(x_cm):
    return rapids_surface_z_cm(x_cm) - RAPIDS_DEPTH_CM


def compute_rapids_bed_slab():
    pitch_radians = math.atan(-RAPIDS_SLOPE)
    normal = (-math.sin(pitch_radians), 0.0, math.cos(pitch_radians))
    half_thickness = 0.5 * RAPIDS_BED_THICKNESS_CM
    top_center = (0.0, 0.0, rapids_bed_z_cm(0.0))
    center = tuple(top_center[axis] - half_thickness * normal[axis] for axis in range(3))
    length = RAPIDS_LENGTH_CM / math.cos(pitch_radians) + 2.0 * RAPIDS_BED_END_MARGIN_CM
    return {"center": center, "pitch_degrees": math.degrees(pitch_radians), "scale": (length / 100.0, RAPIDS_BED_WIDTH_CM / 100.0, RAPIDS_BED_THICKNESS_CM / 100.0)}


def build_rapids_spline_points():
    point_count = int(round(RAPIDS_LENGTH_CM / RAPIDS_SPLINE_SPACING_CM)) + 1
    low_x_cm = -0.5 * RAPIDS_LENGTH_CM
    return [(low_x_cm + RAPIDS_LENGTH_CM * index / (point_count - 1), 0.0, rapids_surface_z_cm(low_x_cm + RAPIDS_LENGTH_CM * index / (point_count - 1)), 1.0) for index in range(point_count)]


def configure_rapids_river_actor(water_stage, river, points, river_material, ribbon_mesh):
    spline = river.get_editor_property("river_spline")
    water_stage.apply_river_spline_points(spline, points)
    river.set_editor_property("river_width_cm", RAPIDS_WIDTH_CM)
    river.set_editor_property("flow_speed_cm_per_sec", RAPIDS_FLOW_SPEED_CM_PER_SEC)
    river.set_editor_property("conserve_discharge_across_width", True)
    river.set_editor_property("guide_sample_spacing_cm", RAPIDS_GUIDE_SAMPLE_SPACING_CM)
    river.set_editor_property("build_ribbon_surface", True)
    river.set_editor_property("ribbon_mesh", ribbon_mesh)
    river.set_editor_property("ribbon_material", river_material)
    river.set_editor_property("ribbon_segment_length_cm", RAPIDS_RIBBON_SEGMENT_LENGTH_CM)
    tags = water_stage.add_actor_tag(river, water_stage.TAG_WATER_SURFACE)
    if water_stage.TAG_WATER_REGION in tags:
        raise RuntimeError("강 리본 액터에 수역 태그가 붙으면 안 됩니다")
    expected_length = math.hypot(RAPIDS_LENGTH_CM, RAPIDS_SLOPE * RAPIDS_LENGTH_CM)
    actual_length = float(spline.get_spline_length())
    if abs(actual_length - expected_length) > RAPIDS_LENGTH_TOLERANCE_RATIO * expected_length:
        raise RuntimeError(f"스플라인 길이 불일치: 기대 {expected_length:.0f} 실제 {actual_length:.0f}")
    return {"point_count": int(spline.get_number_of_spline_points()), "length_cm": actual_length, "expected_length_cm": expected_length, "tags": tags}


def compute_rapids_rock(spec):
    surface_z = rapids_surface_z_cm(spec["x_cm"])
    top_z = surface_z + RAPIDS_ROCK_ABOVE_WATER_CM
    bottom_z = rapids_bed_z_cm(spec["x_cm"]) - RAPIDS_ROCK_EMBED_CM
    height = top_z - bottom_z
    return {"name": spec["name"], "location": (spec["x_cm"], spec["lateral_cm"], bottom_z + 0.5 * height), "scale": (spec["scale_xy"][0], spec["scale_xy"][1], height / 100.0),
            "yaw_degrees": spec["yaw_degrees"], "top_z_cm": top_z, "surface_z_cm": surface_z}


def spawn_rapids_rocks(water_stage, spawn_object, sphere_mesh, stone_material):
    records = []
    for spec in RAPIDS_ROCK_SPECS:
        geometry = compute_rapids_rock(spec)
        rock = spawn_object(sphere_mesh, geometry["location"], "WaterParticle" + spec["name"], "Rocks", unreal.Rotator(yaw=geometry["yaw_degrees"]))
        rock.set_actor_scale3d(unreal.Vector(*geometry["scale"]))
        component = rock.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        if stone_material is not None:
            component.set_material(0, stone_material)
        tags = water_stage.add_actor_tag(rock, water_stage.TAG_OBSTACLE)
        records.append({"name": spec["name"], "location": list(geometry["location"]), "scale": list(geometry["scale"]), "top_z_cm": geometry["top_z_cm"],
                        "surface_z_cm": geometry["surface_z_cm"], "tags": tags})
    return records


def build_rapids_scene(context):
    water_stage, resources = context["water_stage"], context["resources"]
    spawn, spawn_object = context["spawn"], context["spawn_object"]
    exposure_ev100 = spawn_lighting(water_stage, spawn)
    slab = compute_rapids_bed_slab()
    spawn_static_box(spawn_object, resources["cube"], slab["center"], slab["scale"], "WaterParticleRapidsBed", resources.get("sand"), unreal.Rotator(pitch=slab["pitch_degrees"]))
    river = spawn(resources["river_actor_class"], (0.0, 0.0, 0.0), "WaterParticleRapidsRiver", "Water")
    spline_report = configure_rapids_river_actor(water_stage, river, build_rapids_spline_points(), resources["surface_material"], resources["ribbon_mesh"])
    water = spawn(resources["water_actor_class"], (0.0, 0.0, 0.0), context["spec"]["water_label"], "Water")
    water_properties = water_stage.configure_water_actor(water, context["preset"], context["effective"])
    rocks = spawn_rapids_rocks(water_stage, spawn_object, resources["sphere"], resources.get("stone"))
    player_start = spawn_player_start(spawn, "WP2", rapids_bed_z_cm(PLAYER_START_XY_CM["WP2"][0]), water_stage.PLAYER_START_CLEARANCE_CM)
    particle_actor = spawn_particle_actor(spawn, resources, water, river, "WaterParticleRapidsParticles")
    return {
        "camera": RAPIDS_CAMERA, "water": water, "particle_actor": particle_actor, "water_properties": water_properties, "exposure_ev100": exposure_ev100,
        "layout": {"length_cm": RAPIDS_LENGTH_CM, "width_cm": RAPIDS_WIDTH_CM, "slope": RAPIDS_SLOPE, "depth_cm": RAPIDS_DEPTH_CM, "flow_speed_cm_per_sec": RAPIDS_FLOW_SPEED_CM_PER_SEC,
                   "surface_z_cm": {"start": rapids_surface_z_cm(-0.5 * RAPIDS_LENGTH_CM), "end": rapids_surface_z_cm(0.5 * RAPIDS_LENGTH_CM)}, "bed_slab": slab,
                   "spline": spline_report, "rocks": rocks, "player_start": player_start, "surface_material_source": river.get_actor_label()},
    }


SCENE_BUILDERS = {"WP1": build_sea_scene, "WP2": build_rapids_scene}


def collect_resource_paths(spec):
    paths = {"surface_material": spec["output_material_path"], "surface_mesh": SURFACE_MESH_PATH, "cube": CUBE_MESH_PATH, "sphere": SPHERE_MESH_PATH}
    paths.update(SYSTEM_PATHS)
    paths["contact_data_channel"] = DATA_CHANNEL_PATH + "." + DATA_CHANNEL_PATH.rsplit("/", 1)[1]
    paths.update({"tile_mesh": TILE_MESH_PATH} if spec["scene"] == "WP1" else {"ribbon_mesh": RIBBON_MESH_PATH})
    return paths


def load_stage_resources(water_stage, spec):
    assets = unreal.EditorAssetLibrary
    resources = load_required_assets(assets, collect_resource_paths(spec))
    resources["sand"] = load_optional_asset(assets, SAND_MATERIAL_PATH)
    resources["stone"] = load_optional_asset(assets, STONE_MATERIAL_PATH)
    resources["water_actor_class"] = unreal.load_class(None, water_stage.CLASS_PATHS["TDFluidWaterActor"])
    resources["river_actor_class"] = unreal.load_class(None, water_stage.CLASS_PATHS["TDFluidRiverActor"])
    resources["game_mode"] = assets.load_blueprint_class(water_stage.GAME_MODE_PATH)
    missing = [name for name in ("water_actor_class", "river_actor_class", "game_mode") if resources[name] is None]
    if missing:
        raise RuntimeError("필수 클래스 없음: " + ", ".join(missing))
    return resources


def build_stage(spec, particle_stage, water_stage, editor):
    overwrite = particle_stage.read_overwrite_request(spec["request"])
    preset_path = f"{water_stage.PRESET_DIR}/{spec['preset_name']}"
    existing = [path for path in (preset_path, spec["map_path"]) if unreal.EditorAssetLibrary.does_asset_exist(path)]
    if existing and not overwrite:
        print(f"기존 에셋 보존({spec['scene']}): {', '.join(existing)} (갱신은 {os.path.join(*spec['request'])} 에 {{\"overwrite\": true}})")
        return "KEPT", ""
    unsaved = particle_stage.find_unsaved_package_names(overwrite)
    if unsaved:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved))
    print(f"단계({spec['scene']}): 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()
    resources = load_stage_resources(water_stage, spec)
    stirrer_class, stirrer_created = (particle_stage.load_or_create_stirrer_blueprint() if spec["scene"] == "WP1" else (None, False))

    print(f"단계({spec['scene']}): 프리셋")
    preset, preset_path, effective, effective_summary = configure_water_preset(spec, water_stage, resources["surface_material"], resources["surface_mesh"])

    print(f"단계({spec['scene']}): 레벨")
    particle_stage.ensure_directory(MAP_DIR)
    levels, actors = particle_stage.open_empty_level(spec["map_path"])
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", resources["game_mode"])
    created = []
    spawn, spawn_object = make_spawners(particle_stage, actors, created, spec["folder"])
    built = SCENE_BUILDERS[spec["scene"]]({"spec": spec, "particle_stage": particle_stage, "water_stage": water_stage, "resources": resources, "preset": preset,
                                          "effective": effective, "stirrer_class": stirrer_class, "spawn": spawn, "spawn_object": spawn_object})

    print(f"단계({spec['scene']}): 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {spec['map_path']}")
    particle_stage.require_saved_files([preset_path], spec["map_path"])
    water_label = built["water"].get_actor_label()
    camera = dict(built["camera"], awake={water_label: True})
    report = {
        "status": "ok", "scene": spec["scene"], "map": spec["map_path"], "overwrite": overwrite, "preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256,
        "preset": preset_path, "preset_effective": effective_summary, "created_stirrer_blueprint": stirrer_created, "exposure_ev100": built["exposure_ev100"],
        "water_actor": water_label, "water_actor_properties": built["water_properties"], "particle_actor": built["particle_actor"].get_actor_label(),
        "layout": built["layout"],
        "scenes": [{"scene": spec["scene"], "cd_ids": [], "live2_refs": LIVE2_REFS, "kind": spec["kind"], "output_material": spec["output_material_path"],
                    "niagara_system": list(SYSTEM_PATHS.values()), "data_channel": DATA_CHANNEL_PATH, "preset": preset_path, "camera": camera}],
        "created_actors": len(created), "saved": True,
    }
    report_path = particle_stage.write_report(report, spec["report"])
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)
    return "PASS", ""


def main():
    particle_stage = load_module("td_fluid_particle_stage", *PARTICLE_STAGE_RELATIVE_PATH)
    water_stage = load_module("td_fluid_water_stage", *WATER_STAGE_RELATIVE_PATH)
    editor = particle_stage.require_editor_ready(REQUIRED_UNREAL_NAMES)
    contract_problems = find_contract_problems(particle_stage, water_stage)
    if contract_problems:
        raise RuntimeError("C++ 계약 이름 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))
    outcomes = []
    for spec in STAGE_SPECS:
        try:
            status, detail = build_stage(spec, particle_stage, water_stage, editor)
        except Exception as error:
            status, detail = "FAIL", f"{type(error).__name__}: {error}"
            particle_stage.write_report({"status": "failed", "scene": spec["scene"], "error": detail}, spec["report"])
        outcomes.append((spec["scene"], status, detail))
        print(f"TD_WATER_PARTICLE_STAGE_{spec['scene']}", status, detail)
        if status == "FAIL":
            break
    if any(status == "FAIL" for _, status, _ in outcomes):
        raise RuntimeError("무대 생성 실패: " + "; ".join(f"{scene} {detail}" for scene, status, detail in outcomes if status == "FAIL"))


if __name__ == "__main__":
    main()
