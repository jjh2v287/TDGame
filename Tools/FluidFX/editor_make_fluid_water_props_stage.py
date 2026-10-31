"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 물 소품 무대 LV_TDFluidWaterPropsStage(X축 30 m 간격 구역 9개: 0 수조·1 분수·2 가마솥 최소/일반·3 물탱크·4 액체 구 3·5 근거리 폭포+웅덩이·6 원경 폭포 흰/파랑·7 물 머금은 모래 흐름·8 수중 전환 구덩이)와 DA_TDFluid_{WaterPool,Fountain,Cauldron,Waterfall,LiquidSphereClear,LiquidSphereTurbulent,BloodOrb,WaterySandFlow} 8개를 만든다. 먼저 editor_make_fluid_water_props_material.py(마스터 5·MI 10)가 끝나 있어야 하고, W-5 C++(새 종류 8·WorldFixedVertical·ATDFluidWaterPropActor) 통합·에디터 재시작 뒤에 돌린다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_water_props_stage.py (PowerShell, L-repo-01. 기존 에셋을 갱신하거나 구역을 고를 때는 Saved/FluidFX/fluid_water_props_stage_request.json 에 {"overwrite": true, "zones": [0,1,2,3,4,5,6,7,8]})
출력: /Game/FluidFX/Presets/DA_TDFluid_<종류> 8, /Game/FluidFX/Maps/LV_TDFluidWaterPropsStage, Saved/FluidFX/fluid_water_props_stage.json(preset_tuning_sha256·material_look_sha256 = check_fluid_preset_data.py --stage-report 형식, 구역별 cd_ids·카메라·탐침 좌표·액터·blocked, 빌더 값 대조 json_dependencies_differ). 미저장 작업이 있으면 중단한다.
상태: 미검증(2026-10-29 W-5 묶음 M 작성: py_compile·가짜 unreal 배치 점검만, 에디터 실행 전, 보정 JSON 4개는 CAL-0 전 초안). DA 는 종류만 정하고 bOverridePresetDefaults 는 끈다(값은 JSON 종류 값). 블루프린트는 쓰지 않는다. 수역 상자·무한 후처리 볼륨은 구역 8 에만, 경계 후처리 볼륨은 구역 0 에만 둔다(안개 겹침 방지). 구덩이·폭포·모래 치수는 무대 상수이며 종류 값과 다르면 json_dependencies_differ 에 기록만 한다.
"""
import importlib.util
import json
import math
import os

import unreal

FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
MESH_DIR = f"{FLUID_ROOT}/Meshes"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
MAP_DIR = f"{FLUID_ROOT}/Maps"
STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidWaterPropsStage"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
LIVE_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
PROP_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidWaterPropActor"
PARTICLE_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_water_props_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_water_props_stage.json")
STAGE_FOLDER = "TDGen/FluidFX/WaterPropsStage"
UNDERWATER_TAG = "TDFluidUnderwater"
WATER_REGION_TAG = "TDFluidWaterRegion"

ZONE_SPACING_CM = 3000.0
ZONE_COUNT = 9
ZONE_HALF_WIDTH_CM = 1200.0
FLOOR_THICKNESS_CM = 50.0
BASIC_SHAPE_SIZE_CM = 100.0
PLAYER_START = (-1500.0, -900.0, 96.0)
VIEW_PITCH_DEGREES = -60.0
ACTIVATION_DISTANCE_CM = 2000.0
HIDDEN_SIM_Z_CM = -5000.0
POST_PROCESS_PRIORITY = 10.0

TANK_SIZE_CM = 600.0
TANK_HEIGHT_CM = 120.0
TANK_WALL_CM = 10.0
TANK_WATER_DEPTH_CM = 100.0
TANK_RAMP_RUN_CM = 250.0
TANK_RAMP_WIDTH_CM = 200.0
TANK_RAMP_THICKNESS_CM = 10.0
TANK_PILLAR_HEIGHT_CM = 200.0
TANK_PILLAR_SIZE_CM = 300.0
TANK_PLATE_THICKNESS_CM = 20.0
TANK_PPV_BLEND_RADIUS_CM = 0.0
BASIN_RADIUS_CM = 250.0
BASIN_FLOOR_CM = 10.0
BASIN_WALL_HEIGHT_CM = 40.0
BASIN_WATER_OFFSET_CM = 20.0
BASIN_SPOUT_RADIUS_CM = 30.0
BASIN_SPOUT_HEIGHT_CM = 50.0
CAULDRON_RADIUS_CM = 75.0
CAULDRON_WALL_HEIGHT_CM = 60.0
CAULDRON_WALL_CM = 8.0
CAULDRON_OFFSET_Y_CM = 220.0
RING_SEGMENTS_SMALL = 12
RING_SEGMENTS_LARGE = 24
SPHERE_SPACING_Y_CM = 400.0
SPHERE_PEDESTAL_RADIUS_CM = 60.0
SPHERE_PEDESTAL_HEIGHT_CM = 120.0
SPHERE_MESH_SCALE = 2.0
SPHERE_WINDOW_CM = 300.0
CLIFF_SIZE_CM = (300.0, 800.0, 900.0)
CLIFF_FRONT_OFFSET_CM = 90.0
POOL_SURFACE_Z_CM = 30.0
POOL_WINDOW_CM = 600.0
POOL_CENTER_OFFSET_X_CM = -210.0
POOL_WALL_CM = 20.0
POOL_WALL_HEIGHT_CM = 60.0
WATERFALL_WINDOW_CM = (500.0, 800.0)
WATERFALL_LEAN_DEGREES = 10.0
WATERFALL_COUPLING_FORWARD_CM = 80.0
WATERFALL_SOURCE_LENGTH_CM = 300.0
FAR_CARD_DISTANCE_CM = 8000.0
FAR_CARD_SIZE_CM = (800.0, 2500.0)
FAR_CARD_OFFSET_X_CM = 500.0
SAND_SLOPE_DEGREES = 20.0
SAND_SLOPE_SIZE_CM = (800.0, 900.0, 20.0)
SAND_SLOPE_CENTER_Z_CM = 200.0
SAND_WINDOW_CM = 800.0
SAND_SOURCE_RATIO_Y = 0.35
PIT_SIZE_CM = 600.0
PIT_DEPTH_CM = 300.0
PIT_RAMP_RUN_CM = 500.0
PIT_RAMP_WIDTH_CM = 150.0
PIT_RAMP_OFFSET_Y_CM = -225.0
PIT_RAMP_THICKNESS_CM = 20.0
PIT_SURFACE_Z_CM = 0.0
UNDERWATER_EYE_OFFSET_CM = 150.0
ZONE_FLOOR_SIZE_CM = 2400.0

MATERIAL_INSTANCES = {
    "lake": "MI_TD_FluidWater_Lake",
    "sheet": "MI_TD_FluidWaterfallSheet_Default",
    "far_white": "MI_TD_FluidWaterfallFar_White",
    "far_blue": "MI_TD_FluidWaterfallFar_Blue",
    "sphere_clear": "MI_TD_FluidLiquidSphere_Clear",
    "sphere_turbulent": "MI_TD_FluidLiquidSphere_Turbulent",
    "sphere_blood": "MI_TD_FluidLiquidSphere_BloodOrb",
    "underwater_lake": "MI_TD_FluidUnderwater_Lake",
    "underwater_tank": "MI_TD_FluidUnderwater_Tank",
    "tank_wall": "MI_TD_FluidWaterTankWall",
    "sand": "MI_TD_Fluid_WaterySand",
}
ASSET_PATHS = {key: f"{MATERIAL_DIR}/{name}" for key, name in MATERIAL_INSTANCES.items()}
ASSET_PATHS["grid_unit"] = f"{MESH_DIR}/SM_TD_FluidWaterGridUnit"
SHAPE_PATHS = {
    "cube": "/Engine/BasicShapes/Cube",
    "plane": "/Engine/BasicShapes/Plane",
    "sphere": "/Engine/BasicShapes/Sphere",
    "cylinder": "/Engine/BasicShapes/Cylinder",
}

PRESET_SPECS = (
    {"name": "WaterPool", "kind": "WATER_POOL", "channel": "CHANNEL_A_WATER", "output": "lake", "mesh": "grid_unit", "window": (POOL_WINDOW_CM, POOL_WINDOW_CM)},
    {"name": "Fountain", "kind": "FOUNTAIN", "channel": "CHANNEL_A_WATER", "output": "lake", "mesh": "grid_unit", "window": (2.0 * BASIN_RADIUS_CM, 2.0 * BASIN_RADIUS_CM)},
    {"name": "Cauldron", "kind": "CAULDRON", "channel": "CHANNEL_A_WATER", "output": "lake", "mesh": "grid_unit", "window": (2.0 * CAULDRON_RADIUS_CM, 2.0 * CAULDRON_RADIUS_CM)},
    {"name": "Waterfall", "kind": "WATERFALL", "channel": "CHANNEL_A_WATER", "output": "sheet", "mesh": "plane", "window": WATERFALL_WINDOW_CM},
    {"name": "LiquidSphereClear", "kind": "LIQUID_SPHERE_CLEAR", "channel": "CHANNEL_A_WATER", "primary": "sphere_clear", "primary_tag": "TDFluidLiquidSphereClear", "window": (SPHERE_WINDOW_CM, SPHERE_WINDOW_CM)},
    {"name": "LiquidSphereTurbulent", "kind": "LIQUID_SPHERE_TURBULENT", "channel": "CHANNEL_A_WATER", "primary": "sphere_turbulent", "primary_tag": "TDFluidLiquidSphereTurbulent", "window": (SPHERE_WINDOW_CM, SPHERE_WINDOW_CM)},
    {"name": "BloodOrb", "kind": "BLOOD_ORB", "channel": "CHANNEL_A_WATER", "primary": "sphere_blood", "primary_tag": "TDFluidBloodOrb", "window": (SPHERE_WINDOW_CM, SPHERE_WINDOW_CM)},
    {"name": "WaterySandFlow", "kind": "WATERY_SAND_FLOW", "channel": "CHANNEL_R_MUD", "output": "sand", "mesh": "plane", "window": (SAND_WINDOW_CM, SAND_WINDOW_CM)},
)
SPHERE_SPECS = (
    {"preset": "LiquidSphereClear", "label": "Clear", "offset_y": -SPHERE_SPACING_Y_CM, "material": "sphere_clear"},
    {"preset": "LiquidSphereTurbulent", "label": "Turbulent", "offset_y": 0.0, "material": "sphere_turbulent"},
    {"preset": "BloodOrb", "label": "BloodOrb", "offset_y": SPHERE_SPACING_Y_CM, "material": "sphere_blood"},
)
ZONES = (
    {"index": 0, "label": "Tank", "cd_ids": ["S.0.23.1", "S.0.23.2"], "refs": ["TK1", "IMG 21"], "presets": ("WaterPool",), "assets": ("lake", "grid_unit", "tank_wall", "underwater_tank")},
    {"index": 1, "label": "Fountain", "cd_ids": [], "refs": [], "presets": ("Fountain",), "assets": ("lake", "grid_unit")},
    {"index": 2, "label": "Cauldron", "cd_ids": [], "refs": [], "presets": ("Cauldron",), "assets": ("lake", "grid_unit")},
    {"index": 3, "label": "WaterTank", "cd_ids": [], "refs": [], "presets": ("WaterPool",), "assets": ("lake", "grid_unit", "tank_wall")},
    {"index": 4, "label": "LiquidSpheres", "cd_ids": [], "refs": ["SP1"], "presets": ("LiquidSphereClear", "LiquidSphereTurbulent", "BloodOrb"), "assets": ("sphere_clear", "sphere_turbulent", "sphere_blood")},
    {"index": 5, "label": "Waterfall", "cd_ids": ["S.0.17.2", "S.0.23.8"], "refs": ["WF1", "WF2"], "presets": ("Waterfall", "WaterPool"), "assets": ("lake", "grid_unit", "sheet")},
    {"index": 6, "label": "FarWaterfalls", "cd_ids": ["SE.0.23.9.1", "SE.0.23.9.2"], "refs": ["WF3"], "presets": (), "assets": ("far_white", "far_blue")},
    {"index": 7, "label": "WaterySand", "cd_ids": ["SE.0.23.10"], "refs": ["SD1"], "presets": ("WaterySandFlow",), "assets": ("sand",)},
    {"index": 8, "label": "UnderwaterPit", "cd_ids": [], "refs": ["MC:225", "UW1", "UW2", "UW3", "IMG 21"], "presets": ("WaterPool",), "assets": ("lake", "grid_unit", "underwater_lake")},
)
BUILDER_EXPECTATIONS = (
    ("Waterfall", "plane", "orientation", "contains", "WORLD_FIXED_VERTICAL"),
    ("Waterfall", "water_prop", "enable_outflow_coupling", "eq", True),
    ("Waterfall", "config", "buoyancy_cm_per_sec2", "lt0", None),
    ("Fountain", "water_prop", "enable_source", "eq", True),
    ("Cauldron", "water_prop", "enable_source", "eq", True),
    ("Waterfall", "water_prop", "enable_source", "eq", True),
    ("WaterySandFlow", "water_prop", "enable_source", "eq", True),
    ("WaterySandFlow", "config", "terrain_slope_gravity", "gt0", None),
)
BUILDER_FUNCTIONS = {
    "config": "build_config_for_preset_kind",
    "plane": "build_plane_settings_for_preset_kind",
    "water_prop": "build_water_prop_for_preset_kind",
}
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorAssetLibrary.load_blueprint_class",
    "EditorActorSubsystem",
    "LevelEditorSubsystem",
    "UnrealEditorSubsystem",
    "TDFluidPresetAsset",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetAsset.build_plane_settings_for_preset_kind",
    "TDFluidPresetKind.WATER_POOL",
    "TDFluidPresetKind.FOUNTAIN",
    "TDFluidPresetKind.CAULDRON",
    "TDFluidPresetKind.WATERFALL",
    "TDFluidPresetKind.LIQUID_SPHERE_CLEAR",
    "TDFluidPresetKind.LIQUID_SPHERE_TURBULENT",
    "TDFluidPresetKind.BLOOD_ORB",
    "TDFluidPresetKind.WATERY_SAND_FLOW",
    "TDFluidPlaneOrientation.WORLD_FIXED_VERTICAL",
    "TDFluidWaterPropActor",
    "TDFluidBrushChannel.CHANNEL_A_WATER",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "BlendableLocation",
    "PostProcessVolume",
    "WeightedBlendable",
    "WeightedBlendables",
    "MathLibrary.get_right_vector",
    "DirectionalLight",
    "SkyLight",
    "PlayerStart",
    "AutoExposureMethod.AEM_MANUAL",
    "ComponentMobility.STATIC",
    "CollisionEnabled.NO_COLLISION",
)
PRESET_REQUIRED_PROPERTIES = (
    "preset_kind", "override_preset_defaults", "simulation_config", "default_channel", "output_material", "surface_mesh",
    "primary_output_materials", "primary_output_actor_tag",
)
ACTOR_REQUIRED_PROPERTIES = (
    "live_component", "should_follow_player_pawn", "should_use_activation_volume", "activation_distance_cm", "accept_mouse_brush",
    "interaction_brush_channel", "sample_terrain_elevation",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def zone_center(index):
    return (index * ZONE_SPACING_CM, 0.0)


def rotation_axes(pitch, yaw, roll):
    sin_pitch, cos_pitch = math.sin(math.radians(pitch)), math.cos(math.radians(pitch))
    sin_yaw, cos_yaw = math.sin(math.radians(yaw)), math.cos(math.radians(yaw))
    sin_roll, cos_roll = math.sin(math.radians(roll)), math.cos(math.radians(roll))
    axis_x = (cos_pitch * cos_yaw, cos_pitch * sin_yaw, sin_pitch)
    axis_y = (sin_roll * sin_pitch * cos_yaw - cos_roll * sin_yaw, sin_roll * sin_pitch * sin_yaw + cos_roll * cos_yaw, -sin_roll * cos_pitch)
    axis_z = (-(cos_roll * sin_pitch * cos_yaw + sin_roll * sin_yaw), cos_yaw * sin_roll - cos_roll * sin_pitch * sin_yaw, cos_roll * cos_pitch)
    return axis_x, axis_y, axis_z


def waterfall_plane_axes(lean_degrees=WATERFALL_LEAN_DEGREES):
    lean = math.radians(lean_degrees)
    axis_u = (0.0, 1.0, 0.0)
    axis_v = (math.sin(lean), 0.0, math.cos(lean))
    axis_n = (axis_u[1] * axis_v[2] - axis_u[2] * axis_v[1], axis_u[2] * axis_v[0] - axis_u[0] * axis_v[2], axis_u[0] * axis_v[1] - axis_u[1] * axis_v[0])
    return axis_u, axis_v, axis_n


def waterfall_actor_height_cm(lean_degrees=WATERFALL_LEAN_DEGREES):
    return POOL_SURFACE_Z_CM + 0.5 * WATERFALL_WINDOW_CM[1] * math.cos(math.radians(lean_degrees))


def waterfall_coupling_anchor_x_cm(center_x, lean_degrees=WATERFALL_LEAN_DEGREES, forward_cm=WATERFALL_COUPLING_FORWARD_CM):
    _, axis_v, axis_n = waterfall_plane_axes(lean_degrees)
    bottom_x = center_x - 0.5 * WATERFALL_WINDOW_CM[1] * axis_v[0]
    return bottom_x - axis_n[0] * forward_cm


def sand_slope_surface_z_cm(offset_y_cm):
    thickness_lift = 0.5 * SAND_SLOPE_SIZE_CM[2] / math.cos(math.radians(SAND_SLOPE_DEGREES))
    return SAND_SLOPE_CENTER_Z_CM + thickness_lift + offset_y_cm * math.tan(math.radians(SAND_SLOPE_DEGREES))


def pit_ramp_geometry():
    angle = math.degrees(math.atan2(PIT_DEPTH_CM, PIT_RAMP_RUN_CM))
    length = math.hypot(PIT_DEPTH_CM, PIT_RAMP_RUN_CM)
    center_offset_x = -0.5 * PIT_SIZE_CM + 0.5 * PIT_RAMP_RUN_CM
    return angle, length, center_offset_x, -0.5 * PIT_DEPTH_CM


def tank_ramp_geometry():
    angle = math.degrees(math.atan2(TANK_HEIGHT_CM, TANK_RAMP_RUN_CM))
    length = math.hypot(TANK_HEIGHT_CM, TANK_RAMP_RUN_CM)
    center_offset_x = -0.5 * TANK_SIZE_CM - 0.5 * TANK_RAMP_RUN_CM
    return angle, length, center_offset_x, 0.5 * TANK_HEIGHT_CM


def camera_record(center, offset_xy, arm_cm, pitch=VIEW_PITCH_DEGREES, yaw=90.0):
    return {"pawn": [center[0] + offset_xy[0], center[1] + offset_xy[1]], "arm": arm_cm, "view": [pitch, yaw]}


def validate_stage_layout():
    problems = []
    if [zone["index"] for zone in ZONES] != list(range(ZONE_COUNT)):
        problems.append("구역 번호가 0~8 연속이 아님")
    names = [spec["name"] for spec in PRESET_SPECS]
    if len(names) != 8 or len(set(names)) != 8:
        problems.append("DA 이름은 8개이고 중복이 없어야 함")
    used_presets = {name for zone in ZONES for name in zone["presets"]}
    if used_presets != set(names):
        problems.append("배치하지 않은 DA: " + ", ".join(sorted(set(names) - used_presets)))
    for spec in PRESET_SPECS:
        if ("output" in spec) == ("primary" in spec):
            problems.append(f"{spec['name']}: 출력 재질 슬롯은 output 과 primary 중 정확히 하나여야 함")
        for key in (spec.get("output"), spec.get("mesh"), spec.get("primary")):
            if key is not None and key not in ASSET_PATHS and key not in SHAPE_PATHS:
                problems.append(f"{spec['name']}: 알 수 없는 에셋 키 {key}")
    for zone in ZONES:
        for key in zone["assets"]:
            if key not in ASSET_PATHS:
                problems.append(f"구역 {zone['index']}: 알 수 없는 에셋 키 {key}")
    if ZONE_SPACING_CM < 2.0 * ZONE_HALF_WIDTH_CM + 600.0:
        problems.append("구역 간격이 반폭의 두 배 + 600 cm 미만(이웃 구역 활성 거리 침범)")
    if ZONE_SPACING_CM <= ACTIVATION_DISTANCE_CM + 0.5 * WATERFALL_WINDOW_CM[1]:
        problems.append("구역 간격이 활성 거리 + 폭포 창 반폭 이하(이웃 구역도 깨어남)")
    tank_left = 0.5 * TANK_SIZE_CM + TANK_RAMP_RUN_CM
    if tank_left > ZONE_HALF_WIDTH_CM:
        problems.append("구역 0 경사로가 구역 폭 밖")
    tank_angle, tank_length, tank_center_x, tank_center_z = tank_ramp_geometry()
    ramp_top = tank_center_z + 0.5 * tank_length * math.sin(math.radians(tank_angle))
    if abs(ramp_top - TANK_HEIGHT_CM) > 1.0e-6 or TANK_WATER_DEPTH_CM >= TANK_HEIGHT_CM:
        problems.append("수조 경사로 꼭대기가 벽 높이와 다르거나 수면이 벽 높이 이상")
    if BASIN_RADIUS_CM > ZONE_HALF_WIDTH_CM or CAULDRON_OFFSET_Y_CM + CAULDRON_RADIUS_CM > ZONE_HALF_WIDTH_CM:
        problems.append("분수 수반 또는 가마솥이 구역 폭 밖")
    if 2.0 * CAULDRON_OFFSET_Y_CM < 4.0 * CAULDRON_RADIUS_CM + 100.0:
        problems.append("가마솥 창이 서로 겹침")
    if BASIN_SPOUT_HEIGHT_CM + BASIN_FLOOR_CM <= BASIN_FLOOR_CM + BASIN_WATER_OFFSET_CM:
        problems.append("분수 기둥이 수면 아래")
    if SPHERE_SPACING_Y_CM < SPHERE_WINDOW_CM or SPHERE_SPACING_Y_CM < SPHERE_MESH_SCALE * BASIC_SHAPE_SIZE_CM + 100.0:
        problems.append("액체 구 시뮬 창 또는 구 메시가 서로 겹침")
    if abs(SPHERE_SPECS[2]["offset_y"]) + 0.5 * SPHERE_WINDOW_CM > ZONE_HALF_WIDTH_CM:
        problems.append("액체 구 시뮬 창이 구역 폭 밖")
    axis_u, axis_v, axis_n = waterfall_plane_axes()
    sheet_top_offset = 0.5 * WATERFALL_WINDOW_CM[1] * axis_v[0]
    if sheet_top_offset + 10.0 > CLIFF_FRONT_OFFSET_CM:
        problems.append("폭포 판 윗끝이 절벽 앞면을 뚫음")
    if axis_n[0] <= 0.0:
        problems.append("폭포 평면 법선 N 이 +X(절벽 안쪽)가 아님")
    if waterfall_actor_height_cm() + 0.5 * WATERFALL_WINDOW_CM[1] * axis_v[2] > CLIFF_SIZE_CM[2]:
        problems.append("폭포 창 윗끝이 절벽보다 높음")
    if WATERFALL_WINDOW_CM[0] > CLIFF_SIZE_CM[1]:
        problems.append("폭포 창이 절벽 폭보다 넓음")
    pool_east_edge = POOL_CENTER_OFFSET_X_CM + 0.5 * POOL_WINDOW_CM
    pool_west_edge = POOL_CENTER_OFFSET_X_CM - 0.5 * POOL_WINDOW_CM
    anchor_x = waterfall_coupling_anchor_x_cm(0.0)
    if pool_east_edge > CLIFF_FRONT_OFFSET_CM:
        problems.append("웅덩이 창이 절벽 안으로 들어감")
    if not pool_west_edge + 60.0 < anchor_x < pool_east_edge - 60.0:
        problems.append("낙수 결합 위치가 웅덩이 창 가장자리 60 cm 안에 없음")
    if 0.5 * WATERFALL_SOURCE_LENGTH_CM + 120.0 > 0.5 * POOL_WINDOW_CM:
        problems.append("낙수 결합 폭이 웅덩이 폭 밖")
    far_x, far_y, far_z = rotation_axes(0.0, 0.0, -90.0)
    if far_y[2] < 1.0 - 1.0e-9 or far_x[0] < 1.0 - 1.0e-9 or far_z[1] > -1.0 + 1.0e-9:
        problems.append("원경 카드 회전: 로컬 Y 가 세계 +Z 이고 법선이 −Y(카메라 쪽)이어야 함")
    if 2.0 * FAR_CARD_OFFSET_X_CM < FAR_CARD_SIZE_CM[0]:
        problems.append("원경 카드 두 장이 겹침")
    if FAR_CARD_DISTANCE_CM < 8000.0 - 1.0e-9:
        problems.append("원경 카드가 80 m 안")
    slope_x, slope_y, slope_z = rotation_axes(0.0, 0.0, -SAND_SLOPE_DEGREES)
    if slope_y[2] <= 0.0 or slope_z[1] >= 0.0:
        problems.append("모래 흐름 경사가 −Y 로 내려가지 않음(S9)")
    if SAND_SLOPE_SIZE_CM[1] * math.cos(math.radians(SAND_SLOPE_DEGREES)) < SAND_WINDOW_CM or SAND_SLOPE_SIZE_CM[0] < SAND_WINDOW_CM:
        problems.append("모래 경사판 투영이 시뮬 창보다 작음")
    if sand_slope_surface_z_cm(-0.5 * SAND_WINDOW_CM) <= 0.0:
        problems.append("모래 경사판 아래 끝이 바닥 아래")
    if SAND_SOURCE_RATIO_Y + 0.12 > 0.5 or SAND_SOURCE_RATIO_Y <= 0.0:
        problems.append("모래 원천이 창 윗부분이 아님")
    if PIT_SIZE_CM < POOL_WINDOW_CM or PIT_SIZE_CM > 2.0 * ZONE_HALF_WIDTH_CM:
        problems.append("구덩이가 수조 창보다 작거나 구역보다 큼")
    pit_angle, pit_length, pit_center_x, pit_center_z = pit_ramp_geometry()
    pit_ramp_low_z = pit_center_z - 0.5 * pit_length * math.sin(math.radians(pit_angle))
    if abs(pit_ramp_low_z + PIT_DEPTH_CM) > 1.0e-6 or abs(PIT_RAMP_OFFSET_Y_CM) + 0.5 * PIT_RAMP_WIDTH_CM > 0.5 * PIT_SIZE_CM:
        problems.append("구덩이 경사로가 바닥에 닿지 않거나 구덩이 폭 밖")
    if abs(PIT_RAMP_OFFSET_Y_CM) - 0.5 * PIT_RAMP_WIDTH_CM < 0.0:
        problems.append("구덩이 경사로가 폰 위치(중앙)를 덮음")
    if PIT_SURFACE_Z_CM <= -PIT_DEPTH_CM or UNDERWATER_EYE_OFFSET_CM <= 0.0:
        problems.append("구덩이 수면 또는 눈높이 설정 오류")
    return problems


def build_zone_probes(index, center):
    cx, cy = center
    if index == 0:
        surface = TANK_WATER_DEPTH_CM
        return {"surface_z": surface, "water_center": [cx, cy, surface], "camera_under": [cx, cy, 0.5 * surface], "camera_over": [cx, cy, surface + UNDERWATER_EYE_OFFSET_CM]}
    if index == 1:
        surface = BASIN_FLOOR_CM + BASIN_WATER_OFFSET_CM
        return {"surface_z": surface, "source_point": [cx, cy, surface]}
    if index == 2:
        surface = BASIN_FLOOR_CM + 0.5 * CAULDRON_WALL_HEIGHT_CM
        return {"surface_z": surface, "minimal_center": [cx, cy - CAULDRON_OFFSET_Y_CM, surface], "general_center": [cx, cy + CAULDRON_OFFSET_Y_CM, surface]}
    if index == 3:
        surface = TANK_PILLAR_HEIGHT_CM + TANK_WATER_DEPTH_CM
        return {"surface_z": surface, "water_center": [cx, cy, surface]}
    if index == 4:
        centers = {spec["label"]: [cx, cy + spec["offset_y"], SPHERE_PEDESTAL_HEIGHT_CM + 0.5 * SPHERE_MESH_SCALE * BASIC_SHAPE_SIZE_CM] for spec in SPHERE_SPECS}
        return {"sphere_centers": centers, "sim_z": HIDDEN_SIM_Z_CM}
    if index == 5:
        axis_u, axis_v, axis_n = waterfall_plane_axes()
        actor_z = waterfall_actor_height_cm()
        lower_quarter = -0.375 * WATERFALL_WINDOW_CM[1]
        return {
            "window_center": [cx, cy, actor_z],
            "window_bottom_center": [cx - 0.5 * WATERFALL_WINDOW_CM[1] * axis_v[0], cy, POOL_SURFACE_Z_CM],
            "fall_lower_quarter_center": [cx + lower_quarter * axis_v[0], cy, actor_z + lower_quarter * axis_v[2]],
            "coupling_anchor": [waterfall_coupling_anchor_x_cm(cx), cy, POOL_SURFACE_Z_CM],
            "pool_center": [cx + POOL_CENTER_OFFSET_X_CM, cy, POOL_SURFACE_Z_CM],
            "pool_surface_z": POOL_SURFACE_Z_CM,
            "cliff_front_x": cx + CLIFF_FRONT_OFFSET_CM,
            "plane_axes": {"u": list(axis_u), "v": list(axis_v), "n": list(axis_n)},
        }
    if index == 6:
        card_z = 0.5 * FAR_CARD_SIZE_CM[1]
        return {"white_card_center": [cx - FAR_CARD_OFFSET_X_CM, cy + FAR_CARD_DISTANCE_CM, card_z], "blue_card_center": [cx + FAR_CARD_OFFSET_X_CM, cy + FAR_CARD_DISTANCE_CM, card_z], "distance_cm": FAR_CARD_DISTANCE_CM}
    if index == 7:
        half = 0.5 * SAND_WINDOW_CM
        source_y = SAND_SOURCE_RATIO_Y * SAND_WINDOW_CM
        return {
            "slope_degrees": SAND_SLOPE_DEGREES,
            "downhill_direction": [0.0, -1.0, 0.0],
            "slope_top_center": [cx, cy + half, sand_slope_surface_z_cm(half)],
            "slope_bottom_center": [cx, cy - half, sand_slope_surface_z_cm(-half)],
            "source_line_center": [cx, cy + source_y, sand_slope_surface_z_cm(source_y)],
        }
    return {
        "surface_z": PIT_SURFACE_Z_CM,
        "pit_floor_z": -PIT_DEPTH_CM,
        "camera_under": [cx, cy, -PIT_DEPTH_CM + UNDERWATER_EYE_OFFSET_CM],
        "camera_over": [cx - 0.5 * PIT_SIZE_CM - 400.0, cy, PIT_SURFACE_Z_CM + UNDERWATER_EYE_OFFSET_CM],
        "region_box_top_z": PIT_SURFACE_Z_CM,
    }


def build_zone_camera(index, center):
    if index == 5:
        return camera_record(center, (-900.0, 0.0), 900.0, -15.0, 0.0)
    if index == 6:
        return camera_record(center, (0.0, 0.0), 1500.0, -5.0, 90.0)
    if index == 7:
        return camera_record(center, (0.0, -700.0), 800.0, -35.0, 90.0)
    if index == 8:
        return camera_record(center, (0.0, 0.0), 150.0, -10.0, 0.0)
    if index == 4:
        return camera_record(center, (0.0, -900.0), 1200.0, -30.0, 90.0)
    if index == 3:
        return camera_record(center, (0.0, -500.0), 1400.0, -35.0, 90.0)
    if index == 2:
        return camera_record(center, (0.0, -300.0), 600.0)
    if index == 1:
        return camera_record(center, (0.0, -450.0), 900.0)
    return camera_record(center, (0.0, -500.0), 900.0)


def load_module(module_name, *relative_parts):
    module_path = project_path(*relative_parts)
    module_spec = importlib.util.spec_from_file_location(module_name, module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모듈 없음: {module_path}")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def read_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False, list(range(ZONE_COUNT))
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    if not isinstance(request, dict):
        return False, list(range(ZONE_COUNT))
    requested = request.get("zones")
    if requested is None:
        return request.get("overwrite") is True, list(range(ZONE_COUNT))
    zones = sorted({int(value) for value in requested})
    invalid = [value for value in zones if value < 0 or value >= ZONE_COUNT]
    if invalid or not zones:
        raise RuntimeError(f"요청 zones 값이 0~{ZONE_COUNT - 1} 밖이거나 비었음: {requested}")
    return request.get("overwrite") is True, zones


def preset_asset_path(name):
    return f"{PRESET_DIR}/DA_TDFluid_{name}"


def preset_spec_by_name(name):
    return next(spec for spec in PRESET_SPECS if spec["name"] == name)


def zone_by_index(index):
    return next(zone for zone in ZONES if zone["index"] == index)


def object_path_text(value):
    if value is None:
        return ""
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    if hasattr(value, "export_text"):
        return str(value.export_text())
    return str(value)


def normalize_value(value):
    if hasattr(value, "x") and hasattr(value, "y"):
        components = [value.x, value.y] + ([value.z] if hasattr(value, "z") else [])
        return tuple(round(float(item), 3) for item in components)
    if hasattr(value, "name") and not isinstance(value, (bool, int, float, str)):
        return str(value.name)
    if isinstance(value, float):
        return round(value, 6)
    return value


def evaluate_expectation(check, expected, actual):
    if check == "eq":
        return actual == expected
    if check == "gt0":
        return isinstance(actual, (int, float)) and actual > 0
    if check == "lt0":
        return isinstance(actual, (int, float)) and actual < 0
    return expected in str(actual).upper()


def collect_builder_records():
    records = []
    for preset_name, builder_key, field, check, expected in BUILDER_EXPECTATIONS:
        kind = getattr(unreal.TDFluidPresetKind, preset_spec_by_name(preset_name)["kind"])
        builder = getattr(unreal.TDFluidPresetAsset, BUILDER_FUNCTIONS[builder_key], None)
        if builder is None:
            records.append({"preset": preset_name, "builder": builder_key, "field": field, "check": check, "expected": expected, "actual": None, "status": "builder_missing"})
            continue
        try:
            actual = normalize_value(builder(kind).get_editor_property(field))
        except Exception as error:
            records.append({"preset": preset_name, "builder": builder_key, "field": field, "check": check, "expected": expected, "actual": None, "status": f"read_failed: {error}"})
            continue
        records.append({"preset": preset_name, "builder": builder_key, "field": field, "check": check, "expected": expected, "actual": actual, "status": "ok" if evaluate_expectation(check, expected, actual) else "json_differs"})
    for spec in PRESET_SPECS:
        kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
        try:
            extent = normalize_value(unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind).get_editor_property("sim_extent_cm"))
        except Exception as error:
            records.append({"preset": spec["name"], "builder": "config", "field": "sim_extent_cm", "check": "stage_constant", "expected": list(spec["window"]), "actual": None, "status": f"read_failed: {error}"})
            continue
        matches = abs(extent[0] - spec["window"][0]) < 0.5 and abs(extent[1] - spec["window"][1]) < 0.5
        records.append({"preset": spec["name"], "builder": "config", "field": "sim_extent_cm", "check": "stage_constant", "expected": list(spec["window"]), "actual": list(extent), "status": "ok" if matches else "json_differs"})
    return records


def describe_underwater_material(material_instance):
    record = {"path": object_path_text(material_instance), "blendable_location": "", "material_domain": "", "contract_ok": False}
    try:
        parent = material_instance.get_editor_property("parent")
        record["blendable_location"] = str(parent.get_editor_property("blendable_location"))
        record["material_domain"] = str(parent.get_editor_property("material_domain"))
    except Exception as error:
        record["read_error"] = str(error)
        return record
    record["contract_ok"] = "BEFORE_DOF" in record["blendable_location"].upper() and "POST_PROCESS" in record["material_domain"].upper()
    return record


def find_blocked_zones(selected_zones, existing_assets):
    blocked = {}
    for index in selected_zones:
        zone = zone_by_index(index)
        missing = [ASSET_PATHS[key] for key in zone["assets"] if key not in existing_assets]
        if missing:
            blocked[str(index)] = ["에셋 없음(editor_make_fluid_water_props_material.py 먼저): " + path for path in missing]
    return blocked


def configure_preset(context, spec):
    particle_stage, assets = context["particle_stage"], context["assets"]
    kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
    preset, path = particle_stage.load_or_create_preset(spec["name"])
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("default_channel", getattr(unreal.TDFluidBrushChannel, spec["channel"]))
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind))
    if "output" in spec:
        preset.set_editor_property("output_material", assets[spec["output"]])
        preset.set_editor_property("surface_mesh", assets[spec["mesh"]] if spec["mesh"] in assets else context["shapes"][spec["mesh"]])
        preset.set_editor_property("primary_output_materials", [])
    else:
        preset.set_editor_property("output_material", None)
        preset.set_editor_property("surface_mesh", None)
        preset.set_editor_property("primary_output_materials", [assets[spec["primary"]]])
        preset.set_editor_property("primary_output_actor_tag", unreal.Name(spec["primary_tag"]))
    if preset.get_editor_property("preset_kind") != kind or preset.get_editor_property("override_preset_defaults"):
        raise RuntimeError(f"프리셋 종류·덮어쓰기 끔 확인 실패: {path}")
    if "output" in spec and spec["output"] in assets and assets[spec["output"]].get_path_name() not in object_path_text(preset.get_editor_property("output_material")):
        raise RuntimeError(f"프리셋 출력 재질 다시 읽기 불일치: {path}")
    if "primary" in spec:
        stored = list(preset.get_editor_property("primary_output_materials"))
        if len(stored) != 1 or assets[spec["primary"]].get_path_name() not in object_path_text(stored[0]):
            raise RuntimeError(f"프리셋 Primary 슬롯 재질 다시 읽기 불일치: {path}")
        if str(preset.get_editor_property("primary_output_actor_tag")) != spec["primary_tag"]:
            raise RuntimeError(f"프리셋 Primary 슬롯 태그 다시 읽기 불일치: {path}")
    particle_stage.save_asset(preset, path)
    return preset, path, preset.resolve_effective_config()


def finish_actor(context, actor, label):
    context["particle_stage"].finish_actor(actor, f"{context['zone_label']}_{label}", f"{STAGE_FOLDER}/{context['zone_label']}", context["created"])
    context["zone_actors"].append(actor.get_actor_label())
    return actor


def place_mesh(context, shape, location, size_cm, label, material_key=None, rotation=None, has_collision=True, casts_shadow=True, tags=(), hidden_in_game=False):
    actor = context["actors"].spawn_actor_from_object(context["shapes"][shape], unreal.Vector(*location), rotation or unreal.Rotator())
    finish_actor(context, actor, label)
    actor.set_actor_scale3d(unreal.Vector(*(value / BASIC_SHAPE_SIZE_CM for value in size_cm)))
    component = actor.static_mesh_component
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    if material_key is not None:
        component.set_material(0, context["assets"][material_key])
    if not has_collision:
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_collision_profile_name("NoCollision")
    if not casts_shadow:
        component.set_cast_shadow(False)
    if tags:
        actor.set_editor_property("tags", [unreal.Name(tag) for tag in tags])
    if hidden_in_game:
        actor.set_actor_hidden_in_game(True)
    return actor


def place_ring_wall(context, center, base_z, radius_cm, height_cm, thickness_cm, segments, label, material_key=None, casts_shadow=True):
    arc_length = 2.0 * math.pi * radius_cm / segments * 1.04
    for segment in range(segments):
        angle = 2.0 * math.pi * segment / segments
        location = (center[0] + radius_cm * math.cos(angle), center[1] + radius_cm * math.sin(angle), base_z + 0.5 * height_cm)
        place_mesh(context, "cube", location, (arc_length, thickness_cm, height_cm), f"{label}{segment:02d}", material_key, unreal.Rotator(yaw=math.degrees(angle) + 90.0), casts_shadow=casts_shadow)


def place_disc(context, center, base_z, radius_cm, height_cm, label):
    return place_mesh(context, "cylinder", (center[0], center[1], base_z + 0.5 * height_cm), (2.0 * radius_cm, 2.0 * radius_cm, height_cm), label)


def place_zone_floor(context, center, label="Floor"):
    return place_mesh(context, "cube", (center[0], center[1], -0.5 * FLOOR_THICKNESS_CM), (ZONE_FLOOR_SIZE_CM, ZONE_FLOOR_SIZE_CM, FLOOR_THICKNESS_CM), label)


def place_glass_tank(context, center, base_z, label):
    half = 0.5 * TANK_SIZE_CM - 0.5 * TANK_WALL_CM
    height_z = base_z + 0.5 * TANK_HEIGHT_CM
    for name, offset, size in (
        ("WallNorth", (0.0, half), (TANK_SIZE_CM, TANK_WALL_CM, TANK_HEIGHT_CM)),
        ("WallSouth", (0.0, -half), (TANK_SIZE_CM, TANK_WALL_CM, TANK_HEIGHT_CM)),
        ("WallEast", (half, 0.0), (TANK_WALL_CM, TANK_SIZE_CM - 2.0 * TANK_WALL_CM, TANK_HEIGHT_CM)),
        ("WallWest", (-half, 0.0), (TANK_WALL_CM, TANK_SIZE_CM - 2.0 * TANK_WALL_CM, TANK_HEIGHT_CM)),
    ):
        place_mesh(context, "cube", (center[0] + offset[0], center[1] + offset[1], height_z), size, f"{label}{name}", "tank_wall", casts_shadow=False)


def spawn_fluid_actor(context, preset_name, location, label, uses_prop_actor, mouse_brush, uses_activation, sample_terrain=False, rotation=None):
    actor_class = context["classes"]["prop" if uses_prop_actor else "live"]
    actor = context["actors"].spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator())
    finish_actor(context, actor, label)
    preset, preset_path, effective = context["presets"][preset_name]
    spec = preset_spec_by_name(preset_name)
    live_component = actor.get_editor_property("live_component")
    live_component.set_editor_property("active_preset", preset)
    live_component.set_editor_property("should_init_system_state_from_preset", True)
    live_component.set_editor_property("simulation_config", effective)
    actor.set_editor_property("should_follow_player_pawn", False)
    actor.set_editor_property("should_use_activation_volume", uses_activation)
    actor.set_editor_property("activation_distance_cm", ACTIVATION_DISTANCE_CM)
    actor.set_editor_property("accept_mouse_brush", mouse_brush)
    actor.set_editor_property("interaction_brush_channel", getattr(unreal.TDFluidBrushChannel, spec["channel"]))
    actor.set_editor_property("sample_terrain_elevation", sample_terrain)
    context["fluid_records"].append({
        "label": actor.get_actor_label(), "preset": f"DA_TDFluid_{preset_name}", "kind": spec["kind"], "class": "TDFluidWaterPropActor" if uses_prop_actor else "TDFluidLiveActor",
        "zone": context["zone_index"], "location": [round(float(value), 3) for value in location], "activation_volume": uses_activation, "sample_terrain_elevation": sample_terrain,
    })
    return actor


def place_post_process_volume(context, center_z, size_cm, label, material_key, unbound, blend_weight, blend_radius_cm):
    volume = context["actors"].spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(*context["center_xy"], center_z), unreal.Rotator())
    finish_actor(context, volume, label)
    volume.set_actor_scale3d(unreal.Vector(*(value / 200.0 for value in size_cm)))
    volume.set_editor_property("unbound", unbound)
    volume.set_editor_property("blend_weight", blend_weight)
    volume.set_editor_property("priority", POST_PROCESS_PRIORITY)
    volume.set_editor_property("blend_radius", blend_radius_cm)
    volume.set_editor_property("tags", [unreal.Name(UNDERWATER_TAG)])
    blendable = unreal.WeightedBlendable()
    blendable.set_editor_property("weight", 1.0)
    blendable.set_editor_property("object", context["assets"][material_key])
    blendables = unreal.WeightedBlendables()
    blendables.set_editor_property("array", [blendable])
    settings = volume.get_editor_property("settings")
    settings.set_editor_property("weighted_blendables", blendables)
    volume.set_editor_property("settings", settings)
    stored = list(volume.get_editor_property("settings").get_editor_property("weighted_blendables").get_editor_property("array"))
    if len(stored) != 1 or ASSET_PATHS[material_key] not in object_path_text(stored[0].get_editor_property("object")):
        raise RuntimeError(f"후처리 볼륨 블렌더블 다시 읽기 불일치(settings 사본 되쓰기 확인): {label}")
    if bool(volume.get_editor_property("unbound")) != unbound or abs(float(volume.get_editor_property("blend_weight")) - blend_weight) > 1.0e-6:
        raise RuntimeError(f"후처리 볼륨 unbound·BlendWeight 다시 읽기 불일치: {label}")
    context["ppv_records"].append({"label": volume.get_actor_label(), "unbound": unbound, "blend_weight": blend_weight, "priority": POST_PROCESS_PRIORITY, "blend_radius_cm": blend_radius_cm, "tag": UNDERWATER_TAG, "material": ASSET_PATHS[material_key], "zone": context["zone_index"]})
    return volume


def build_zone_tank(context, center):
    place_zone_floor(context, center)
    place_glass_tank(context, center, 0.0, "Tank")
    angle, length, offset_x, offset_z = tank_ramp_geometry()
    place_mesh(context, "cube", (center[0] + offset_x, center[1], offset_z), (length, TANK_RAMP_WIDTH_CM, TANK_RAMP_THICKNESS_CM), "Ramp", rotation=unreal.Rotator(pitch=angle))
    spawn_fluid_actor(context, "WaterPool", (center[0], center[1], TANK_WATER_DEPTH_CM), "Pool", False, True, True)
    volume = place_post_process_volume(context, 0.5 * TANK_WATER_DEPTH_CM, (TANK_SIZE_CM, TANK_SIZE_CM, TANK_WATER_DEPTH_CM), "TankUnderwaterVolume", "underwater_tank", False, 1.0, TANK_PPV_BLEND_RADIUS_CM)
    return {"underwater_material": describe_underwater_material(context["assets"]["underwater_tank"]), "bounded_volume": volume.get_actor_label()}


def build_zone_fountain(context, center):
    place_zone_floor(context, center)
    place_disc(context, center, 0.0, BASIN_RADIUS_CM, BASIN_FLOOR_CM, "BasinFloor")
    place_ring_wall(context, center, 0.0, BASIN_RADIUS_CM, BASIN_WALL_HEIGHT_CM, 20.0, RING_SEGMENTS_LARGE, "BasinWall")
    place_disc(context, center, BASIN_FLOOR_CM, BASIN_SPOUT_RADIUS_CM, BASIN_SPOUT_HEIGHT_CM, "Spout")
    spawn_fluid_actor(context, "Fountain", (center[0], center[1], BASIN_FLOOR_CM + BASIN_WATER_OFFSET_CM), "Fountain", True, True, True)
    return {}


def build_zone_cauldron(context, center):
    place_zone_floor(context, center)
    surface = BASIN_FLOOR_CM + 0.5 * CAULDRON_WALL_HEIGHT_CM
    for name, offset_y, uses_prop in (("Minimal", -CAULDRON_OFFSET_Y_CM, False), ("General", CAULDRON_OFFSET_Y_CM, True)):
        pot_center = (center[0], center[1] + offset_y)
        place_disc(context, pot_center, 0.0, CAULDRON_RADIUS_CM, BASIN_FLOOR_CM, f"{name}PotFloor")
        place_ring_wall(context, pot_center, 0.0, CAULDRON_RADIUS_CM, CAULDRON_WALL_HEIGHT_CM, CAULDRON_WALL_CM, RING_SEGMENTS_SMALL, f"{name}PotWall")
        spawn_fluid_actor(context, "Cauldron", (pot_center[0], pot_center[1], surface), f"{name}Cauldron", uses_prop, True, True)
    return {"same_preset": "DA_TDFluid_Cauldron", "minimal_class": "TDFluidLiveActor", "general_class": "TDFluidWaterPropActor"}


def build_zone_water_tank(context, center):
    place_zone_floor(context, center)
    place_mesh(context, "cube", (center[0], center[1], 0.5 * (TANK_PILLAR_HEIGHT_CM - TANK_PLATE_THICKNESS_CM)), (TANK_PILLAR_SIZE_CM, TANK_PILLAR_SIZE_CM, TANK_PILLAR_HEIGHT_CM - TANK_PLATE_THICKNESS_CM), "Pillar")
    place_mesh(context, "cube", (center[0], center[1], TANK_PILLAR_HEIGHT_CM - 0.5 * TANK_PLATE_THICKNESS_CM), (TANK_SIZE_CM + 20.0, TANK_SIZE_CM + 20.0, TANK_PLATE_THICKNESS_CM), "Plate")
    place_glass_tank(context, center, TANK_PILLAR_HEIGHT_CM, "Tank")
    spawn_fluid_actor(context, "WaterPool", (center[0], center[1], TANK_PILLAR_HEIGHT_CM + TANK_WATER_DEPTH_CM), "Pool", False, True, True)
    return {}


def build_zone_spheres(context, center):
    place_zone_floor(context, center)
    sphere_mesh_size = SPHERE_MESH_SCALE * BASIC_SHAPE_SIZE_CM
    records = []
    for spec in SPHERE_SPECS:
        preset_spec = preset_spec_by_name(spec["preset"])
        sphere_xy = (center[0], center[1] + spec["offset_y"])
        place_disc(context, sphere_xy, 0.0, SPHERE_PEDESTAL_RADIUS_CM, SPHERE_PEDESTAL_HEIGHT_CM, f"{spec['label']}Pedestal")
        sphere = place_mesh(context, "sphere", (sphere_xy[0], sphere_xy[1], SPHERE_PEDESTAL_HEIGHT_CM + 0.5 * sphere_mesh_size), (sphere_mesh_size,) * 3, f"{spec['label']}Sphere", spec["material"], tags=(preset_spec["primary_tag"],))
        sim = spawn_fluid_actor(context, spec["preset"], (sphere_xy[0], sphere_xy[1], HIDDEN_SIM_Z_CM), f"{spec['label']}Sim", False, False, False)
        records.append({"preset": f"DA_TDFluid_{spec['preset']}", "tag": preset_spec["primary_tag"], "sphere": sphere.get_actor_label(), "sim_actor": sim.get_actor_label(), "sphere_material": ASSET_PATHS[spec["material"]]})
    return {"spheres": records, "tag_rule": "종류마다 태그 1, 레벨당 종류별 구 1개(FX-9 위험 6)"}


def build_zone_waterfall(context, center):
    place_zone_floor(context, center)
    place_mesh(context, "cube", (center[0] + CLIFF_FRONT_OFFSET_CM + 0.5 * CLIFF_SIZE_CM[0], center[1], 0.5 * CLIFF_SIZE_CM[2]), CLIFF_SIZE_CM, "Cliff")
    pool_center = (center[0] + POOL_CENTER_OFFSET_X_CM, center[1])
    half = 0.5 * POOL_WINDOW_CM
    wall_z = 0.5 * POOL_WALL_HEIGHT_CM
    place_mesh(context, "cube", (pool_center[0] - half, pool_center[1], wall_z), (POOL_WALL_CM, POOL_WINDOW_CM + POOL_WALL_CM, POOL_WALL_HEIGHT_CM), "PoolWallWest")
    place_mesh(context, "cube", (pool_center[0], pool_center[1] + half, wall_z), (POOL_WINDOW_CM, POOL_WALL_CM, POOL_WALL_HEIGHT_CM), "PoolWallNorth")
    place_mesh(context, "cube", (pool_center[0], pool_center[1] - half, wall_z), (POOL_WINDOW_CM, POOL_WALL_CM, POOL_WALL_HEIGHT_CM), "PoolWallSouth")
    pool = spawn_fluid_actor(context, "WaterPool", (pool_center[0], pool_center[1], POOL_SURFACE_Z_CM), "Pool", False, True, True)
    fall = spawn_fluid_actor(context, "Waterfall", (center[0], center[1], waterfall_actor_height_cm()), "Fall", True, False, True)
    fall.set_editor_property("coupling_receiver_actor", pool)
    receiver = fall.get_editor_property("coupling_receiver_actor")
    if receiver is None or receiver.get_actor_label() != pool.get_actor_label():
        raise RuntimeError("폭포 coupling_receiver_actor 다시 읽기 불일치: 앞 WaterPool 이어야 함")
    return {"fall_actor": fall.get_actor_label(), "coupling_receiver_actor": receiver.get_actor_label(), "plane_orientation": "WorldFixedVertical", "actor_yaw_degrees": 0.0}


def build_zone_far_waterfalls(context, center):
    place_zone_floor(context, center)
    rotation = unreal.Rotator(roll=-90.0)
    right = unreal.MathLibrary.get_right_vector(rotation)
    if right.z < 1.0 - 1.0e-6:
        raise RuntimeError(f"원경 카드 회전 단언 실패: 로컬 Y 가 세계 +Z 가 아님({right.x:.3f}, {right.y:.3f}, {right.z:.3f})")
    cards = []
    for name, offset_x, material_key in (("White", -FAR_CARD_OFFSET_X_CM, "far_white"), ("Blue", FAR_CARD_OFFSET_X_CM, "far_blue")):
        card = place_mesh(context, "plane", (center[0] + offset_x, center[1] + FAR_CARD_DISTANCE_CM, 0.5 * FAR_CARD_SIZE_CM[1]), (FAR_CARD_SIZE_CM[0], FAR_CARD_SIZE_CM[1], BASIC_SHAPE_SIZE_CM), f"Far{name}", material_key, rotation, has_collision=False, casts_shadow=False)
        cards.append({"label": card.get_actor_label(), "material": ASSET_PATHS[material_key], "size_cm": list(FAR_CARD_SIZE_CM)})
    return {"cards": cards, "local_y_world_z": right.z}


def build_zone_sand(context, center):
    place_zone_floor(context, center)
    slope = place_mesh(context, "cube", (center[0], center[1], SAND_SLOPE_CENTER_Z_CM), SAND_SLOPE_SIZE_CM, "Slope", rotation=unreal.Rotator(roll=-SAND_SLOPE_DEGREES))
    surface = sand_slope_surface_z_cm(0.0)
    actor = spawn_fluid_actor(context, "WaterySandFlow", (center[0], center[1], surface), "Sand", True, True, True, sample_terrain=True)
    return {"slope_actor": slope.get_actor_label(), "sand_actor": actor.get_actor_label(), "slope_degrees": SAND_SLOPE_DEGREES, "downhill": "-Y (S9: 원천 SourceWindowRatio·속도는 세계 XY)"}


def build_zone_pit(context, center):
    half = 0.5 * PIT_SIZE_CM
    ring_thickness = PIT_DEPTH_CM + FLOOR_THICKNESS_CM
    ring_z = -0.5 * ring_thickness
    outer = ZONE_FLOOR_SIZE_CM
    side_depth = 0.5 * (outer - PIT_SIZE_CM)
    place_mesh(context, "cube", (center[0], center[1] + half + 0.5 * side_depth, ring_z), (outer, side_depth, ring_thickness), "RingNorth")
    place_mesh(context, "cube", (center[0], center[1] - half - 0.5 * side_depth, ring_z), (outer, side_depth, ring_thickness), "RingSouth")
    place_mesh(context, "cube", (center[0] + half + 0.5 * side_depth, center[1], ring_z), (side_depth, PIT_SIZE_CM, ring_thickness), "RingEast")
    place_mesh(context, "cube", (center[0] - half - 0.5 * side_depth, center[1], ring_z), (side_depth, PIT_SIZE_CM, ring_thickness), "RingWest")
    place_mesh(context, "cube", (center[0], center[1], -PIT_DEPTH_CM - 0.5 * FLOOR_THICKNESS_CM), (PIT_SIZE_CM, PIT_SIZE_CM, FLOOR_THICKNESS_CM), "PitFloor")
    angle, length, offset_x, offset_z = pit_ramp_geometry()
    place_mesh(context, "cube", (center[0] + offset_x, center[1] + PIT_RAMP_OFFSET_Y_CM, offset_z), (length, PIT_RAMP_WIDTH_CM, PIT_RAMP_THICKNESS_CM), "Ramp", rotation=unreal.Rotator(pitch=-angle))
    pool = spawn_fluid_actor(context, "WaterPool", (center[0], center[1], PIT_SURFACE_Z_CM), "Pool", False, True, True)
    region = place_mesh(context, "cube", (center[0], center[1], PIT_SURFACE_Z_CM - 0.5 * PIT_DEPTH_CM), (PIT_SIZE_CM, PIT_SIZE_CM, PIT_DEPTH_CM), "WaterRegion", has_collision=False, casts_shadow=False, tags=(WATER_REGION_TAG,), hidden_in_game=True)
    volume = place_post_process_volume(context, PIT_SURFACE_Z_CM - 0.5 * PIT_DEPTH_CM, (PIT_SIZE_CM, PIT_SIZE_CM, PIT_DEPTH_CM), "UnderwaterVolume", "underwater_lake", True, 0.0, 0.0)
    return {"pool_actor": pool.get_actor_label(), "water_region_box": region.get_actor_label(), "water_region_tag": WATER_REGION_TAG, "infinite_volume": volume.get_actor_label(), "underwater_material": describe_underwater_material(context["assets"]["underwater_lake"])}


ZONE_BUILDERS = {
    0: build_zone_tank, 1: build_zone_fountain, 2: build_zone_cauldron, 3: build_zone_water_tank, 4: build_zone_spheres,
    5: build_zone_waterfall, 6: build_zone_far_waterfalls, 7: build_zone_sand, 8: build_zone_pit,
}


def place_lighting_and_start(context):
    context["zone_label"] = "Stage"
    context["zone_actors"] = []
    context["zone_index"] = -1
    actors = context["actors"]
    sun = finish_actor(context, actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 2000), unreal.Rotator(pitch=-50, yaw=150)), "Sun")
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(4.0)
    sky = finish_actor(context, actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1500), unreal.Rotator()), "SkyLight")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(0.3)
    exposure = finish_actor(context, actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator()), "Exposure")
    exposure.set_editor_property("unbound", True)
    settings = exposure.get_editor_property("settings")
    for name, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("auto_exposure_apply_physical_camera_exposure", False), ("auto_exposure_bias", 0.0)):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    exposure.set_editor_property("settings", settings)
    finish_actor(context, actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(*PLAYER_START), unreal.Rotator()), "PlayerStart")


def main():
    particle_stage = load_module("editor_make_fluid_particle_stage", *PARTICLE_STAGE_RELATIVE_PATH)
    editor = particle_stage.require_editor_ready(REQUIRED_UNREAL_NAMES)
    layout_problems = validate_stage_layout()
    if layout_problems:
        raise RuntimeError("무대 배치 자체 점검 실패: " + " | ".join(layout_problems))
    overwrite, selected_zones = read_request()
    needed_presets = sorted({name for index in selected_zones for name in zone_by_index(index)["presets"]}, key=lambda name: [spec["name"] for spec in PRESET_SPECS].index(name))
    preset_paths = [preset_asset_path(name) for name in needed_presets]
    particle_stage.guard_existing_assets(preset_paths + [STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)

    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()
    live_actor_class = unreal.load_class(None, LIVE_ACTOR_CLASS_PATH)
    prop_actor_class = unreal.load_class(None, PROP_ACTOR_CLASS_PATH)
    if live_actor_class is None or prop_actor_class is None:
        raise RuntimeError("유체·물 소품 액터 클래스 없음(W-5 C++ 통합·재빌드·에디터 재시작 필요): " + LIVE_ACTOR_CLASS_PATH + " " + PROP_ACTOR_CLASS_PATH)
    contract_problems = particle_stage.find_missing_properties(unreal.TDFluidPresetAsset.get_default_object(), PRESET_REQUIRED_PROPERTIES)
    contract_problems += particle_stage.find_missing_properties(unreal.get_default_object(prop_actor_class), ACTOR_REQUIRED_PROPERTIES + ("coupling_receiver_actor",))
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))

    assets_library = unreal.EditorAssetLibrary
    existing_assets = {key for key, path in ASSET_PATHS.items() if assets_library.does_asset_exist(path)}
    blocked = find_blocked_zones(selected_zones, existing_assets)
    built_zones = [index for index in selected_zones if str(index) not in blocked]
    if not built_zones:
        raise RuntimeError("만들 수 있는 구역이 없음(blocked): " + json.dumps(blocked, ensure_ascii=False))
    game_mode = assets_library.load_blueprint_class(GAME_MODE_PATH)
    shapes = {key: assets_library.load_asset(path) for key, path in SHAPE_PATHS.items()}
    missing_shapes = [SHAPE_PATHS[key] for key, value in shapes.items() if value is None]
    if game_mode is None or missing_shapes:
        raise RuntimeError("게임 모드 또는 기본 도형 없음: " + GAME_MODE_PATH + " " + ", ".join(missing_shapes))
    assets = {key: assets_library.load_asset(ASSET_PATHS[key]) for key in existing_assets}

    print("단계: 프리셋")
    particle_stage.ensure_directory(PRESET_DIR)
    context = {"particle_stage": particle_stage, "assets": assets, "shapes": shapes, "classes": {"live": live_actor_class, "prop": prop_actor_class}}
    built_presets = sorted({name for index in built_zones for name in zone_by_index(index)["presets"]}, key=lambda name: [spec["name"] for spec in PRESET_SPECS].index(name))
    context["presets"] = {name: configure_preset(context, preset_spec_by_name(name)) for name in built_presets}
    builder_records = collect_builder_records()

    print("단계: 레벨")
    levels, actors = particle_stage.open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    context.update({"actors": actors, "created": [], "fluid_records": [], "ppv_records": []})
    place_lighting_and_start(context)
    zone_records = {}
    for index in built_zones:
        zone = zone_by_index(index)
        center = zone_center(index)
        context.update({"zone_label": zone["label"], "zone_index": index, "zone_actors": [], "center_xy": center})
        details = ZONE_BUILDERS[index](context, center)
        zone_records[str(index)] = {
            "label": zone["label"], "center": list(center), "cd_ids": zone["cd_ids"], "refs": zone["refs"], "presets": [f"DA_TDFluid_{name}" for name in zone["presets"]],
            "camera": build_zone_camera(index, center), "probes": build_zone_probes(index, center), "actors": list(context["zone_actors"]), "details": details,
        }

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    particle_stage.require_saved_files([preset_asset_path(name) for name in built_presets], STAGE_MAP_PATH)
    report = {
        "status": "ok" if not blocked else "partial",
        "preset_tuning_sha256": tuning_sha256,
        "material_look_sha256": look_sha256,
        "map": STAGE_MAP_PATH,
        "overwrite": overwrite,
        "requested_zones": selected_zones,
        "built_zones": built_zones,
        "blocked": blocked,
        "presets": {name: {"path": context["presets"][name][1], "kind": preset_spec_by_name(name)["kind"], "override_preset_defaults": False} for name in built_presets},
        "zones": zone_records,
        "fluid_actors": context["fluid_records"],
        "post_process_volumes": context["ppv_records"],
        "builder_values": builder_records,
        "json_dependencies_differ": [record for record in builder_records if record["status"] != "ok"],
        "stage_constants": {"zone_spacing_cm": ZONE_SPACING_CM, "tank_ppv_blend_radius_cm": TANK_PPV_BLEND_RADIUS_CM, "post_process_priority": POST_PROCESS_PRIORITY, "hidden_sim_z_cm": HIDDEN_SIM_Z_CM},
        "created_actors": len(context["created"]),
        "saved": True,
    }
    report_path = particle_stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        report_path = project_path(*REPORT_RELATIVE_PATH)
        os.makedirs(os.path.dirname(report_path), exist_ok=True)
        with open(report_path, "w", encoding="utf-8") as report_file:
            json.dump({"status": "failed", "error": str(error)}, report_file, ensure_ascii=False, indent=2)
        raise


if __name__ == "__main__":
    run_with_failure_report()
