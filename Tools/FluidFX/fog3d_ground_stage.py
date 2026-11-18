"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행, PIE 꺼짐, 저장 안 된 변경 없음). 먼저 editor_make_fog3d_material.py 로 M_TD_Fog3DVolume 이 있어야 하고 구역 액터 ATDFog3DZoneActor 가 빌드된 에디터여야 한다. 3D 안개 2a-3 땅 따라가기 화면 확인 무대 LV_TDFog3DGroundStage 하나만 만든다(기존 맵·에셋은 고치지 않고 새 에셋은 이 맵 하나뿐).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/fog3d_ground_stage.py (PowerShell, L-repo-01. 맵이 이미 있으면 Saved/FluidFX/fog3d_ground_stage_request.json 에 {"overwrite": true} 가 있을 때만 비우고 다시 만든다). 에디터 없는 점검: python -B Tools/FluidFX/fog3d_ground_stage.py --self-test, 배치표 출력: --print-layout
출력: /Game/FluidFX/Maps/LV_TDFog3DGroundStage(구역 9칸 배치, 땅 메시마다 액터 태그 TDFluidTerrain, 구역 액터 TDGen_Fog3D_Zone 하나, 고정 카메라 TDGen_Fog3DG_Cam_*, 햇빛·하늘·높이 안개·고정 노출은 1단계 무대와 같음), Saved/FluidFX/fog3d_ground_stage.json(status·액터 목록·경계·태그·카메라)
상태: 에디터 실행 전: 미검증(py_compile·--self-test 만. 기울인 판의 pitch 부호·경계 상자 일치, CameraActor 속성 이름, 구역 높이 1000 이 안개 층을 자르지 않는지는 에디터에서 처음 확인한다).
"""
import hashlib
import importlib.util
import json
import math
import os
import re
import sys

try:
    import unreal
except ModuleNotFoundError:
    unreal = None


FLUID_ROOT = "/Game/FluidFX"
STAGE_MAP_PATH = f"{FLUID_ROOT}/Maps/LV_TDFog3DGroundStage"
MAP_DIR = f"{FLUID_ROOT}/Maps"
VOLUME_MATERIAL_PATH = f"{FLUID_ROOT}/Materials/M_TD_Fog3DVolume"
GROUND_MATERIAL_PATH = f"{FLUID_ROOT}/Materials/M_TD_Fog3DVolumeGround"
FLOOR_MATERIAL_PATH = f"{FLUID_ROOT}/Materials/M_TD_FluidFogStageFloor"
WALL_MATERIAL_PATH = f"{FLUID_ROOT}/Materials/M_TD_FluidFogStageWall"
FALLBACK_MATERIAL_PATH = "/Engine/BasicShapes/BasicShapeMaterial"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
CUBE_MESH_PATH = "/Engine/BasicShapes/Cube"
ZONE_ACTOR_CLASS_PATH = "/Script/TDGame.TDFog3DZoneActor"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fog3d_ground_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fog3d_ground_stage.json")
PRESET_TUNING_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidPresetTuning.json")
PRESET_CHECKER_RELATIVE_PATH = ("Tools", "check_fluid_preset_data.py")
ZONE_HEADER_RELATIVE_PATH = ("Source", "TDGame", "FluidFX", "TDFog3DZoneActor.h")
OLD_STAGE_SCRIPT_NAME = "editor_make_fog3d_stage.py"
GROUND_MATERIAL_FILE_RELATIVE_PATH = ("Content", "FluidFX", "Materials", "M_TD_Fog3DVolumeGround.uasset")

TERRAIN_TAG = "TDFluidTerrain"
LABEL_PREFIX = "TDGen_"
ZONE_LABEL = "Fog3D_Zone"
SUN_LABEL = "Fog3D_Sun"
ACTOR_FOLDER = "TDGen/FluidFX/Fog3DGroundStage"
CUBE_SIZE_CM = 100.0
BOUNDS_TOLERANCE_CM = 1.5
CELL_SIZE_CM = 15.625
CELL_ALIGN_TOLERANCE_CELLS = 0.02
FOG_LAYER_STACK_CM = 240.0
ZONE_TOP_MARGIN_CM = 100.0
DEFAULT_ZONE_EDGE_FADE_CM = 500.0
DEFAULT_ZONE_ENTRY_VERTICAL_MARGIN_CM = 200.0
MINIMUM_STATION_GAP_CM = 3000.0
STATION_HALF_EXTENT_CM = 1500.0
SLAB_HALF_EXTENT_CM = 11000.0
SLAB_THICKNESS_CM = 100.0
ZONE_CENTER_CM = (0.0, 0.0, 500.0)
ZONE_HALF_EXTENT_CM = (9500.0, 9500.0, 500.0)
PLAYER_START_CM = (0.0, 0.0, 100.0)
PAWN_SPAWN_HEIGHT_CM = 110.0
PAWN_RADIUS_CM = 40.0
MAX_STEP_UP_CM = 40.0
MAX_WALKABLE_DEGREES = 44.76
SLOPE_WINDOW_CM = 100.0
LINE_SAMPLE_STEP_CM = 5.0
PREWALK_CM = 150.0
CAMERA_FOV_DEGREES = 70.0
CAMERA_NAMES = ("side", "high")
RAMP_THICKNESS_CM = 200.0
PIT_HALF_EXTENT_CM = 800.0
PIT_FLOOR_TOP_CM = -100.0

SUN_LOCATION_CM = (0.0, 0.0, 2000.0)
SUN_ROTATION_DEGREES = {"pitch": -35.0, "yaw": 100.0}
SUN_INTENSITY = 4.0
SKY_LIGHT_INTENSITY = 0.3
HEIGHT_FOG_PROPERTIES = (
    ("fog_density", 0.002),
    ("enable_volumetric_fog", True),
    ("volumetric_fog_scattering_distribution", 0.35),
    ("volumetric_fog_distance", 12000.0),
)
ZONE_PROPERTY_NAMES = {"half_extent": "zone_half_extent_cm", "material": "volume_material", "volume": "zone_volume"}
ZONE_HEADER_NAMES = ("ZoneHalfExtentCm", "VolumeMaterial", "ZoneVolume")
LOOKDEV_COMMAND = "python -B Tools/FluidFX/fog3d_ground_variants.py --batch static && python -B Tools/FluidFX/fog3d_ground_run.py --batch static"

STATION_CENTERS_CM = {
    "F": (0.0, 0.0), "S30": (-6000.0, 0.0), "CL": (6000.0, 0.0),
    "S40": (-6000.0, 6000.0), "ST": (0.0, 6000.0), "CB": (6000.0, 6000.0),
    "SC": (-6000.0, -6000.0), "HO": (0.0, -6000.0), "S20": (6000.0, -6000.0),
}
STATION_TITLES = {
    "F": "평지 대조", "S30": "30도 비탈 능선(오르막·꼭대기 450·내리막)", "CL": "4.5m 절벽(위·아래 평지)", "S40": "40도 비탈", "ST": "계단(단 높이 20cm)",
    "CB": "턱 30·60·100cm", "SC": "30도 비탈 끝에 붙은 100cm 턱", "HO": "땅 표시 메시가 없는 구멍", "S20": "20도 비탈",
}
STATIC_PAWN_LOCAL_CM = {"F": (0.0, 0.0), "S30": (0.0, 0.0), "CL": (300.0, 0.0), "S40": (300.0, 0.0), "ST": (200.0, 0.0), "CB": (400.0, 0.0), "SC": (-1000.0, 0.0), "HO": (-1100.0, 0.0), "S20": (300.0, 0.0)}
CAMERA_LOCAL_CM = {
    "F": {"side": ((0.0, -1800.0, 250.0), (0.0, 0.0, 100.0)), "high": ((-1300.0, -1300.0, 700.0), (0.0, 0.0, 50.0))},
    "S30": {"side": ((0.0, -2400.0, 300.0), (0.0, 0.0, 200.0)), "high": ((-1800.0, -1800.0, 900.0), (0.0, 0.0, 150.0))},
    "CL": {"side": ((0.0, -2200.0, 350.0), (0.0, 0.0, 225.0)), "high": ((1500.0, -1500.0, 700.0), (0.0, 0.0, 200.0))},
    "S40": {"side": ((0.0, -2000.0, 250.0), (0.0, 0.0, 225.0)), "high": ((-1500.0, -1500.0, 800.0), (0.0, 0.0, 200.0))},
    "ST": {"side": ((300.0, -2000.0, 200.0), (300.0, 0.0, 100.0)), "high": ((-1200.0, -1300.0, 600.0), (200.0, 0.0, 100.0))},
    "CB": {"side": ((0.0, -2200.0, 150.0), (0.0, 0.0, 50.0)), "high": ((-1300.0, -1500.0, 500.0), (0.0, 0.0, 30.0))},
    "SC": {"side": ((0.0, -2200.0, 350.0), (0.0, 0.0, 300.0)), "high": ((-1500.0, -1500.0, 900.0), (0.0, 0.0, 300.0))},
    "HO": {"side": ((0.0, -2000.0, 150.0), (0.0, 0.0, -30.0)), "high": ((-1500.0, -1500.0, 700.0), (0.0, 0.0, -50.0))},
    "S20": {"side": ((0.0, -2200.0, 200.0), (0.0, 0.0, 150.0)), "high": ((-1500.0, -1500.0, 700.0), (0.0, 0.0, 150.0))},
}

REQUIRED_UNREAL_NAMES = (
    "get_engine_subsystem", "get_editor_subsystem", "load_class", "get_default_object", "Name",
    "TDFluidPresetTuningSubsystem", "TDFluidPresetTuningSubsystem.reload_preset_tuning", "TDFluidPresetTuningSubsystem.get_load_errors",
    "EditorAssetLibrary.load_blueprint_class", "EditorAssetLibrary.load_asset", "EditorAssetLibrary.does_asset_exist",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages", "EditorLoadingAndSavingUtils.get_dirty_content_packages", "SystemLibrary.get_project_directory",
    "LevelEditorSubsystem", "EditorActorSubsystem", "UnrealEditorSubsystem", "StaticMeshComponent", "ExponentialHeightFog", "ExponentialHeightFogComponent",
    "DirectionalLight", "SkyLight", "SkyAtmosphere", "PostProcessVolume", "PlayerStart", "WorldSettings", "Brush", "CameraActor",
    "AutoExposureMethod.AEM_MANUAL", "ComponentMobility.STATIC", "ComponentMobility.MOVABLE",
)


def make_box(label, station, x_range, y_range, z_range, tagged=True, material="floor"):
    center_x, center_y = STATION_CENTERS_CM[station]
    return {
        "kind": "box", "label": label, "station": station, "tagged": tagged, "material": material,
        "center": (center_x + 0.5 * (x_range[0] + x_range[1]), center_y + 0.5 * (y_range[0] + y_range[1]), 0.5 * (z_range[0] + z_range[1])),
        "size": (x_range[1] - x_range[0], y_range[1] - y_range[0], z_range[1] - z_range[0]),
    }


def make_world_box(label, station, x_range, y_range, z_range, tagged=True, material="floor"):
    return {
        "kind": "box", "label": label, "station": station, "tagged": tagged, "material": material,
        "center": (0.5 * (x_range[0] + x_range[1]), 0.5 * (y_range[0] + y_range[1]), 0.5 * (z_range[0] + z_range[1])),
        "size": (x_range[1] - x_range[0], y_range[1] - y_range[0], z_range[1] - z_range[0]),
    }


def ramp_run_cm(angle_degrees, rise_cm):
    return rise_cm / math.tan(math.radians(angle_degrees))


def make_ramp(label, station, angle_degrees, rise_cm, direction, foot_x_cm, half_width_cm):
    center_x, center_y = STATION_CENTERS_CM[station]
    theta = math.radians(angle_degrees)
    length = rise_cm / math.sin(theta)
    thickness = RAMP_THICKNESS_CM
    foot_world_x = center_x + foot_x_cm
    return {
        "kind": "ramp", "label": label, "station": station, "tagged": True, "material": "floor",
        "angle_deg": angle_degrees, "direction": direction, "foot_x": foot_world_x, "length": length, "run": length * math.cos(theta), "rise": rise_cm,
        "thickness": thickness, "yaw_deg": 0.0 if direction > 0 else 180.0,
        "center": (foot_world_x + direction * (0.5 * length * math.cos(theta) + 0.5 * thickness * math.sin(theta)), center_y, 0.5 * length * math.sin(theta) - 0.5 * thickness * math.cos(theta)),
        "size": (length, 2.0 * half_width_cm, thickness),
    }


def build_solids():
    hole_center = STATION_CENTERS_CM["HO"]
    hole = (hole_center[0] - PIT_HALF_EXTENT_CM, hole_center[0] + PIT_HALF_EXTENT_CM, hole_center[1] - PIT_HALF_EXTENT_CM, hole_center[1] + PIT_HALF_EXTENT_CM)
    edge = SLAB_HALF_EXTENT_CM
    slab_z = (-SLAB_THICKNESS_CM, 0.0)
    solids = [
        make_world_box("Floor_W", "SLAB", (-edge, hole[0]), (-edge, edge), slab_z),
        make_world_box("Floor_E", "SLAB", (hole[1], edge), (-edge, edge), slab_z),
        make_world_box("Floor_S", "SLAB", (hole[0], hole[1]), (-edge, hole[2]), slab_z),
        make_world_box("Floor_N", "SLAB", (hole[0], hole[1]), (hole[3], edge), slab_z),
        make_box("HO_PitFloor", "HO", (-PIT_HALF_EXTENT_CM, PIT_HALF_EXTENT_CM), (-PIT_HALF_EXTENT_CM, PIT_HALF_EXTENT_CM), (PIT_FLOOR_TOP_CM - SLAB_THICKNESS_CM, PIT_FLOOR_TOP_CM), tagged=False, material="floor"),
    ]
    ridge_run = ramp_run_cm(30.0, 450.0)
    solids += [
        make_ramp("S30_RampUp", "S30", 30.0, 450.0, 1, -300.0 - ridge_run, 1200.0),
        make_box("S30_Plateau", "S30", (-300.0, 300.0), (-1200.0, 1200.0), (-50.0, 450.0)),
        make_ramp("S30_RampDown", "S30", 30.0, 450.0, -1, 300.0 + ridge_run, 1200.0),
        make_ramp("S20_Ramp", "S20", 20.0, 300.0, 1, -ramp_run_cm(20.0, 300.0), 1200.0),
        make_box("S20_Landing", "S20", (0.0, 1200.0), (-1200.0, 1200.0), (-50.0, 300.0)),
        make_ramp("S40_Ramp", "S40", 40.0, 450.0, 1, -ramp_run_cm(40.0, 450.0), 1200.0),
        make_box("S40_Landing", "S40", (0.0, 1200.0), (-1200.0, 1200.0), (-50.0, 450.0)),
        make_box("CL_Upper", "CL", (-1300.0, 0.0), (-1300.0, 1300.0), (-50.0, 450.0)),
        make_ramp("SC_Ramp", "SC", 30.0, 450.0, 1, -ridge_run, 1200.0),
        make_box("SC_Curb100", "SC", (0.0, 1200.0), (-1200.0, 1200.0), (-50.0, 550.0)),
        make_box("CB_Curb30", "CB", (-850.0, -750.0), (-1200.0, 1200.0), (-50.0, 30.0)),
        make_box("CB_Curb60", "CB", (-50.0, 50.0), (-1200.0, 1200.0), (-50.0, 60.0)),
        make_box("CB_Curb100", "CB", (750.0, 850.0), (-1200.0, 1200.0), (-50.0, 100.0)),
        make_box("ST_Landing", "ST", (0.0, 1300.0), (-600.0, 600.0), (-50.0, 200.0)),
    ]
    for index in range(1, 11):
        start = -400.0 + 40.0 * (index - 1)
        solids.append(make_box(f"ST_Step{index:02d}", "ST", (start, start + 40.0), (-600.0, 600.0), (-50.0, 20.0 * index)))
    return solids


def actor_label(label):
    return LABEL_PREFIX + label


def camera_label(station, camera_name):
    return actor_label(f"Fog3DG_Cam_{station}_{camera_name}")


def solid_actor_label(solid):
    return actor_label("Fog3DG_" + solid["label"])


def surface_height(solid, x, y):
    center_x, center_y, _ = solid["center"]
    if solid["kind"] == "box":
        size = solid["size"]
        if abs(x - center_x) <= 0.5 * size[0] + 1e-9 and abs(y - center_y) <= 0.5 * size[1] + 1e-9:
            return solid["center"][2] + 0.5 * size[2]
        return None
    if abs(y - center_y) > 0.5 * solid["size"][1] + 1e-9:
        return None
    along = solid["direction"] * (x - solid["foot_x"])
    if along < -1e-9 or along > solid["run"] + 1e-9:
        return None
    return along * math.tan(math.radians(solid["angle_deg"]))


def ground_height(solids, x, y, tagged_only=False):
    best = None
    for solid in solids:
        if tagged_only and not solid["tagged"]:
            continue
        height = surface_height(solid, x, y)
        if height is not None and (best is None or height > best):
            best = height
    return best


def solid_aabb(solid):
    center = solid["center"]
    size = solid["size"]
    if solid["kind"] == "box":
        return (center[0] - 0.5 * size[0], center[1] - 0.5 * size[1], center[2] - 0.5 * size[2], center[0] + 0.5 * size[0], center[1] + 0.5 * size[1], center[2] + 0.5 * size[2])
    theta = math.radians(solid["angle_deg"])
    direction = solid["direction"]
    forward = (direction * math.cos(theta), math.sin(theta))
    up = (-direction * math.sin(theta), math.cos(theta))
    xs, zs = [], []
    for along in (-0.5 * size[0], 0.5 * size[0]):
        for lift in (-0.5 * size[2], 0.5 * size[2]):
            xs.append(center[0] + along * forward[0] + lift * up[0])
            zs.append(center[2] + along * forward[1] + lift * up[1])
    return (min(xs), center[1] - 0.5 * size[1], min(zs), max(xs), center[1] + 0.5 * size[1], max(zs))


def make_line(identifier, station, name, start_local, yaw, distance, priority, description, view_alpha_degrees=None):
    return {"id": identifier, "station": station, "name": name, "start_local": start_local, "yaw": float(yaw), "distance_cm": float(distance), "prewalk_cm": PREWALK_CM, "priority": priority, "description": description, "view_alpha_degrees": view_alpha_degrees}


RIDGE_RUN_CM = ramp_run_cm(30.0, 450.0)
WALK_LINES = (
    make_line("W01", "F", "flat", (-900.0, 0.0), 0, 600, 1, "평지 대조"),
    make_line("W02", "S30", "up30", (-300.0 - RIDGE_RUN_CM - 200.0, 0.0), 0, 600, 1, "30도 오르막: 평지에서 출발해 비탈 아래 끝을 지나 오른다"),
    make_line("W03", "S30", "down30", (100.0, 0.0), 0, 600, 1, "30도 내리막: 꼭대기 평지에서 출발해 모서리를 넘어 내려간다"),
    make_line("W04", "S30", "contour30", (-300.0 - RIDGE_RUN_CM + 450.0 / 2.0 / math.tan(math.radians(30.0)), -300.0), 90, 600, 1, "30도 등고선: 비탈 가운데 높이(225)를 옆으로 걷는다"),
    make_line("W05", "S20", "up20", (-ramp_run_cm(20.0, 300.0) - 250.0, 0.0), 0, 600, 1, "20도 오르막"),
    make_line("W06", "S40", "up40", (-ramp_run_cm(40.0, 450.0) - 250.0, 0.0), 0, 600, 1, "40도 오르막(걸을 수 있는 한계 안)"),
    make_line("W07", "S40", "down40", (300.0, 0.0), 180, 600, 2, "40도 내리막"),
    make_line("W08", "CL", "lower", (150.0, -300.0), 90, 600, 1, "절벽 아래 평지에서 벽면 150cm 옆으로 나란히"),
    make_line("W09", "CL", "upper", (-150.0, -300.0), 90, 600, 1, "절벽 위 평지에서 가장자리 150cm 옆으로 나란히"),
    make_line("W10", "CL", "edge", (-750.0, 0.0), 0, 600, 2, "절벽 위에서 가장자리를 향해 걷다 150cm 앞에서 멈춤"),
    make_line("W11", "CB", "over30", (-1250.0, 0.0), 0, 600, 2, "30cm 턱 넘기", 0.0),
    make_line("W12", "CB", "beside60", (-150.0, -300.0), 90, 600, 1, "60cm 턱 면에서 100cm 옆으로 나란히"),
    make_line("W13", "CB", "beside100", (650.0, -300.0), 90, 600, 1, "100cm 턱 면에서 100cm 옆으로 나란히"),
    make_line("W14", "SC", "beside100slope", (-100.0, -300.0), 90, 600, 1, "비탈 끝에 붙은 100cm 턱 면에서 100cm 옆(비탈 쪽)으로 나란히"),
    make_line("W15", "ST", "up20", (-700.0, 0.0), 0, 600, 1, "단 높이 20cm 계단 오르막", 26.6),
    make_line("W16", "ST", "down20", (250.0, 0.0), 180, 600, 2, "단 높이 20cm 계단 내리막", -26.6),
    make_line("W17", "HO", "holeedge", (-950.0, -300.0), 90, 600, 2, "구멍 가장자리 150cm 옆으로 나란히"),
    make_line("R01", "F", "runflat", (-1500.0, 0.0), 0, 3000, 1, "창 이동: 평지 30m 달리기"),
    make_line("R02", "S30", "runridge", (-1500.0, 0.0), 0, 3000, 1, "창 이동: 능선(오르막·평지·내리막)을 가로질러 30m 달리기"),
)


def find_line(identifier):
    for line in WALK_LINES:
        if line["id"] == identifier:
            return line
    raise KeyError(f"걷는 선 없음: {identifier}")


def line_world_point(line, distance_cm):
    center_x, center_y = STATION_CENTERS_CM[line["station"]]
    radians = math.radians(line["yaw"])
    return (center_x + line["start_local"][0] + distance_cm * math.cos(radians), center_y + line["start_local"][1] + distance_cm * math.sin(radians))


def line_start_world(line):
    return line_world_point(line, 0.0)


def line_end_world(line):
    return line_world_point(line, line["distance_cm"])


def line_end_slope_degrees(solids, line):
    if line["view_alpha_degrees"] is not None:
        return float(line["view_alpha_degrees"])
    end = line_world_point(line, line["distance_cm"])
    before = line_world_point(line, line["distance_cm"] - SLOPE_WINDOW_CM)
    end_height = ground_height(solids, end[0], end[1])
    before_height = ground_height(solids, before[0], before[1])
    return math.degrees(math.atan2(end_height - before_height, SLOPE_WINDOW_CM))


def line_start_height(solids, line):
    start = line_start_world(line)
    return ground_height(solids, start[0], start[1])


def static_pawn_world(station):
    center_x, center_y = STATION_CENTERS_CM[station]
    local = STATIC_PAWN_LOCAL_CM[station]
    return (center_x + local[0], center_y + local[1])


def camera_world(station, camera_name):
    center_x, center_y = STATION_CENTERS_CM[station]
    position, target = CAMERA_LOCAL_CM[station][camera_name]
    return (center_x + position[0], center_y + position[1], position[2]), (center_x + target[0], center_y + target[1], target[2])


def camera_rotation_degrees(position, target):
    delta_x, delta_y, delta_z = target[0] - position[0], target[1] - position[1], target[2] - position[2]
    return math.degrees(math.atan2(delta_z, math.hypot(delta_x, delta_y))), math.degrees(math.atan2(delta_y, delta_x))


def ring_points(x, y, radius):
    return [(x + radius * math.cos(math.radians(angle)), y + radius * math.sin(math.radians(angle))) for angle in range(0, 360, 45)]


def find_walk_line_problems(solids, line):
    problems = []
    name = line["id"]
    total = line["distance_cm"]
    steps = int(round(total / LINE_SAMPLE_STEP_CM))
    heights = []
    for index in range(steps + 1):
        distance = index * LINE_SAMPLE_STEP_CM
        point = line_world_point(line, distance)
        height = ground_height(solids, point[0], point[1])
        if height is None:
            problems.append(f"{name}: {distance:.0f}cm 지점에 바닥이 없다")
            return problems
        heights.append(height)
        for ring_x, ring_y in ring_points(point[0], point[1], PAWN_RADIUS_CM):
            ring_height = ground_height(solids, ring_x, ring_y)
            if ring_height is None or height - ring_height > MAX_STEP_UP_CM + 1e-6:
                problems.append(f"{name}: {distance:.0f}cm 지점 둘레 {PAWN_RADIUS_CM:.0f}cm 안에 낭떠러지·구멍")
                return problems
            if ring_height - height > MAX_STEP_UP_CM + 1e-6:
                problems.append(f"{name}: {distance:.0f}cm 지점 둘레 {PAWN_RADIUS_CM:.0f}cm 안에 오를 수 없는 벽({ring_height - height:.0f}cm)")
                return problems
    window = int(round(SLOPE_WINDOW_CM / LINE_SAMPLE_STEP_CM))
    worst = 0.0
    for index in range(0, len(heights) - window):
        worst = max(worst, abs(heights[index + window] - heights[index]))
    if math.degrees(math.atan2(worst, SLOPE_WINDOW_CM)) > MAX_WALKABLE_DEGREES:
        problems.append(f"{name}: {SLOPE_WINDOW_CM:.0f}cm 구간 최대 높이 변화 {worst:.1f}cm 가 걸을 수 있는 경사 {MAX_WALKABLE_DEGREES}도 를 넘음")
    prewalk_steps = int(round(line["prewalk_cm"] / LINE_SAMPLE_STEP_CM)) + 2
    if max(heights[:prewalk_steps + 1]) - min(heights[:prewalk_steps + 1]) > 0.5:
        problems.append(f"{name}: 미리 걷기 {line['prewalk_cm']:.0f}cm 구간이 평평하지 않다(시작점 순간이동 때 높이가 달라진다)")
    center_x, center_y = STATION_CENTERS_CM[line["station"]]
    for label, point in (("시작", line_start_world(line)), ("끝", line_end_world(line))):
        if abs(point[0] - center_x) > STATION_HALF_EXTENT_CM + 1e-6 or abs(point[1] - center_y) > STATION_HALF_EXTENT_CM + 1e-6:
            problems.append(f"{name}: {label} {point} 이 칸 반폭 {STATION_HALF_EXTENT_CM:.0f} 밖")
    return problems


def find_layout_problems(solids, edge_fade_cm=DEFAULT_ZONE_EDGE_FADE_CM):
    problems = []
    labels = [solid_actor_label(solid) for solid in solids]
    labels += [camera_label(station, name) for station in STATION_CENTERS_CM for name in CAMERA_NAMES]
    labels += [actor_label(ZONE_LABEL), actor_label(SUN_LABEL)]
    if len(set(labels)) != len(labels):
        problems.append("액터 라벨이 겹침")
    zone_xy = (ZONE_CENTER_CM[0] - ZONE_HALF_EXTENT_CM[0], ZONE_CENTER_CM[1] - ZONE_HALF_EXTENT_CM[1], ZONE_CENTER_CM[0] + ZONE_HALF_EXTENT_CM[0], ZONE_CENTER_CM[1] + ZONE_HALF_EXTENT_CM[1])
    zone_bottom, zone_top = ZONE_CENTER_CM[2] - ZONE_HALF_EXTENT_CM[2], ZONE_CENTER_CM[2] + ZONE_HALF_EXTENT_CM[2]
    if min(ZONE_HALF_EXTENT_CM[:2]) < edge_fade_cm:
        problems.append("구역 반크기가 가장자리 흐림 폭보다 작다")
    tagged = [solid for solid in solids if solid["tagged"]]
    lowest_tagged_top = min(solid_aabb(solid)[5] for solid in tagged if solid["station"] == "SLAB")
    if abs(lowest_tagged_top - zone_bottom) > 1e-6:
        problems.append(f"가장 낮은 땅(바닥 슬래브) 윗면 {lowest_tagged_top} ≠ 구역 아래면 {zone_bottom}")
    highest = max(solid_aabb(solid)[5] for solid in tagged)
    if zone_top < highest + FOG_LAYER_STACK_CM + ZONE_TOP_MARGIN_CM:
        problems.append(f"구역 윗면 {zone_top} < 가장 높은 땅 {highest} + 안개 층 {FOG_LAYER_STACK_CM} + 여유 {ZONE_TOP_MARGIN_CM}")
    slab_xy = (-SLAB_HALF_EXTENT_CM, -SLAB_HALF_EXTENT_CM, SLAB_HALF_EXTENT_CM, SLAB_HALF_EXTENT_CM)
    if not (slab_xy[0] <= zone_xy[0] and slab_xy[1] <= zone_xy[1] and zone_xy[2] <= slab_xy[2] and zone_xy[3] <= slab_xy[3]):
        problems.append("구역이 바닥 슬래브 밖으로 나감")
    fade_inner = (zone_xy[0] + edge_fade_cm, zone_xy[1] + edge_fade_cm, zone_xy[2] - edge_fade_cm, zone_xy[3] - edge_fade_cm)
    station_boxes = {}
    for solid in solids:
        if solid["station"] == "SLAB":
            continue
        bounds = solid_aabb(solid)
        if bounds[0] < fade_inner[0] or bounds[1] < fade_inner[1] or bounds[3] > fade_inner[2] or bounds[4] > fade_inner[3]:
            problems.append(f"{solid['label']}: 구역 가장자리 흐림 띠에 걸림")
        if solid["kind"] == "ramp" and solid["angle_deg"] > MAX_WALKABLE_DEGREES - 2.0:
            problems.append(f"{solid['label']}: 경사 {solid['angle_deg']}도 가 걸을 수 있는 한계에 너무 가깝다")
        center_x, center_y = STATION_CENTERS_CM[solid["station"]]
        if abs(bounds[0] - center_x) > STATION_HALF_EXTENT_CM or abs(bounds[3] - center_x) > STATION_HALF_EXTENT_CM or abs(bounds[1] - center_y) > STATION_HALF_EXTENT_CM or abs(bounds[4] - center_y) > STATION_HALF_EXTENT_CM:
            problems.append(f"{solid['label']}: 칸 반폭 {STATION_HALF_EXTENT_CM:.0f} 밖")
        merged = station_boxes.get(solid["station"])
        flat = (bounds[0], bounds[1], bounds[3], bounds[4])
        station_boxes[solid["station"]] = flat if merged is None else (min(merged[0], flat[0]), min(merged[1], flat[1]), max(merged[2], flat[2]), max(merged[3], flat[3]))
    names = sorted(station_boxes)
    for first_index, first in enumerate(names):
        for second in names[first_index + 1:]:
            a, b = station_boxes[first], station_boxes[second]
            gap_x = max(b[0] - a[2], a[0] - b[2], 0.0)
            gap_y = max(b[1] - a[3], a[1] - b[3], 0.0)
            gap = math.hypot(gap_x, gap_y)
            if gap < MINIMUM_STATION_GAP_CM:
                problems.append(f"칸 {first}·{second} 구조물 간격 {gap:.0f} < {MINIMUM_STATION_GAP_CM:.0f}")
    boxes = [solid for solid in solids if solid["kind"] == "box" and solid["station"] != "SLAB"]
    for first_index, first in enumerate(boxes):
        for second in boxes[first_index + 1:]:
            a, b = solid_aabb(first), solid_aabb(second)
            overlap = min(a[3], b[3]) - max(a[0], b[0]), min(a[4], b[4]) - max(a[1], b[1]), min(a[5], b[5]) - max(a[2], b[2])
            if all(value > 1e-6 for value in overlap) and first["station"] == second["station"] and not (first["tagged"] is False or second["tagged"] is False):
                problems.append(f"{first['label']}·{second['label']} 부피가 겹침")
    slab_area = sum(solid["size"][0] * solid["size"][1] for solid in solids if solid["station"] == "SLAB")
    expected_area = (2.0 * SLAB_HALF_EXTENT_CM) ** 2 - (2.0 * PIT_HALF_EXTENT_CM) ** 2
    if abs(slab_area - expected_area) > 1.0:
        problems.append(f"바닥 슬래브 넓이 {slab_area} ≠ 전체에서 구멍을 뺀 {expected_area}")
    hole_center = STATION_CENTERS_CM["HO"]
    for offset in ((0.0, 0.0), (PIT_HALF_EXTENT_CM - 1.0, PIT_HALF_EXTENT_CM - 1.0), (-PIT_HALF_EXTENT_CM + 1.0, 0.0)):
        if ground_height(solids, hole_center[0] + offset[0], hole_center[1] + offset[1], tagged_only=True) is not None:
            problems.append("구멍 안에 땅 표시 메시가 있다")
            break
    if ground_height(solids, hole_center[0], hole_center[1]) != PIT_FLOOR_TOP_CM:
        problems.append("구멍 바닥(태그 없음) 높이가 기대와 다르다")
    for solid in solids:
        if solid["station"] != "SLAB" and solid["label"] != "HO_PitFloor" and not solid["tagged"]:
            problems.append(f"{solid['label']}: 땅 구조물인데 태그가 없다")
    for line in WALK_LINES:
        problems += find_walk_line_problems(solids, line)
    for station in STATION_CENTERS_CM:
        if station not in STATIC_PAWN_LOCAL_CM:
            problems.append(f"칸 {station}: 정지 촬영 폰 자리가 없다")
            continue
        pawn = static_pawn_world(station)
        height = ground_height(solids, pawn[0], pawn[1])
        if height is None:
            problems.append(f"칸 {station}: 정지 촬영 폰 자리에 바닥이 없다")
        for name in CAMERA_NAMES:
            position, target = camera_world(station, name)
            camera_ground = ground_height(solids, position[0], position[1])
            if camera_ground is not None and position[2] < camera_ground + 100.0:
                problems.append(f"카메라 {station}/{name}: 땅 위 100cm 아래")
    return problems


def grid_phase_rows(solids):
    rows = []
    for solid in solids:
        if solid["station"] == "SLAB" or not solid["tagged"]:
            continue
        bounds = solid_aabb(solid)
        faces_x = [bounds[0], bounds[3]]
        faces_y = [bounds[1], bounds[4]]
        if solid["kind"] == "ramp":
            faces_x = [solid["foot_x"], solid["foot_x"] + solid["direction"] * solid["run"]]
        for axis, values in (("x", faces_x), ("y", faces_y)):
            for value in values:
                cells = value / CELL_SIZE_CM
                rows.append({"label": solid["label"], "axis": axis, "world_cm": round(value, 3), "cells": round(cells, 4), "phase": round(cells - math.floor(cells), 4)})
    return rows


def classify_phase(phase):
    if phase < CELL_ALIGN_TOLERANCE_CELLS or phase > 1.0 - CELL_ALIGN_TOLERANCE_CELLS:
        return "경계"
    if abs(phase - 0.5) < 0.05:
        return "칸 한가운데"
    return "어긋남"


def find_missing_header_names(header_text):
    return [name for name in ZONE_HEADER_NAMES if not re.search(r"\b" + name + r"\b", header_text)]


def project_path(*parts):
    if unreal is not None:
        return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)
    return os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), *parts)


def read_file_sha256(relative_parts):
    path = project_path(*relative_parts)
    if not os.path.isfile(path):
        raise RuntimeError(f"정본 파일 없음: {path}")
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def read_fog3d_settings():
    path = project_path(*PRESET_TUNING_RELATIVE_PATH)
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as tuning_file:
        tuning = json.load(tuning_file)
    settings = tuning.get("Globals", {}).get("TDFog3DSettings") if isinstance(tuning, dict) else None
    return settings if isinstance(settings, dict) else None


def resolve_layout_thresholds(settings):
    if settings is None:
        return DEFAULT_ZONE_EDGE_FADE_CM, DEFAULT_ZONE_ENTRY_VERTICAL_MARGIN_CM
    return float(settings.get("ZoneEdgeFadeCm", DEFAULT_ZONE_EDGE_FADE_CM)), float(settings.get("ZoneEntryVerticalMarginCm", DEFAULT_ZONE_ENTRY_VERTICAL_MARGIN_CM))


def find_missing_unreal_names():
    missing = []
    for dotted_name in REQUIRED_UNREAL_NAMES:
        target = unreal
        for part in dotted_name.split("."):
            target = getattr(target, part, None)
            if target is None:
                missing.append(dotted_name)
                break
    return missing


def has_editor_property(owner, property_name):
    try:
        owner.get_editor_property(property_name)
    except Exception:
        return False
    return True


def read_overwrite_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    return [name for name in names if name != STAGE_MAP_PATH] if overwrite else names


def load_preset_checker():
    checker_path = project_path(*PRESET_CHECKER_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("check_fluid_preset_data", checker_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"프리셋 정본 검사기 없음: {checker_path}")
    checker = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(checker)
    return checker


def reload_preset_tuning():
    tuning_path = project_path(*PRESET_TUNING_RELATIVE_PATH)
    before = read_file_sha256(PRESET_TUNING_RELATIVE_PATH)
    if read_fog3d_settings() is None:
        raise RuntimeError("프리셋 정본에 Globals.TDFog3DSettings 가 없습니다.")
    problems = load_preset_checker().check_tuning_file(tuning_path)
    if problems:
        raise RuntimeError("프리셋 정본 JSON 검사 실패(python Tools/check_fluid_preset_data.py): " + " | ".join(problems))
    tuning = unreal.get_engine_subsystem(unreal.TDFluidPresetTuningSubsystem)
    if tuning is None:
        raise RuntimeError("TDFluidPresetTuningSubsystem 없음(재빌드와 에디터 재시작 필요)")
    error_count = tuning.reload_preset_tuning()
    if error_count != 0:
        raise RuntimeError(f"프리셋 정본 다시 읽기 오류 {error_count}건: " + " | ".join(tuning.get_load_errors()))
    if read_file_sha256(PRESET_TUNING_RELATIVE_PATH) != before:
        raise RuntimeError("프리셋 정본 JSON 이 검사·다시 읽기 도중 바뀌었습니다. 다시 실행하세요.")
    return before


def ensure_directory(directory_path):
    if unreal.EditorAssetLibrary.does_directory_exist(directory_path):
        return
    if not unreal.EditorAssetLibrary.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def is_default_level_actor(actor):
    return isinstance(actor, unreal.WorldSettings) or type(actor) is unreal.Brush


def open_empty_stage_level(levels, actors):
    if not unreal.EditorAssetLibrary.does_asset_exist(STAGE_MAP_PATH):
        if not levels.new_level(STAGE_MAP_PATH):
            raise RuntimeError(f"무대 레벨 생성 실패: {STAGE_MAP_PATH}")
        return
    if not levels.load_level(STAGE_MAP_PATH):
        raise RuntimeError(f"기존 무대 레벨 열기 실패: {STAGE_MAP_PATH}")
    for actor in actors.get_all_level_actors():
        if is_default_level_actor(actor):
            continue
        if not actors.destroy_actor(actor):
            raise RuntimeError(f"기존 무대 레벨 액터 제거 실패: {actor.get_actor_label()}")


def pick_stage_material(assets, material_path):
    if assets.does_asset_exist(material_path):
        return assets.load_asset(material_path), material_path
    return assets.load_asset(FALLBACK_MATERIAL_PATH), FALLBACK_MATERIAL_PATH


def find_zone_volume_component(actor):
    try:
        component = actor.get_editor_property(ZONE_PROPERTY_NAMES["volume"])
        if component is not None:
            return component
    except Exception:
        pass
    components = list(actor.get_components_by_class(unreal.StaticMeshComponent))
    return components[0] if len(components) == 1 else None


def describe_bounds(actor):
    origin, extent = actor.get_actor_bounds(False)
    return [origin.x - extent.x, origin.y - extent.y, origin.z - extent.z, origin.x + extent.x, origin.y + extent.y, origin.z + extent.z]


def require_bounds(actor, label, expected):
    bounds = describe_bounds(actor)
    mismatches = [index for index in range(6) if abs(bounds[index] - expected[index]) > BOUNDS_TOLERANCE_CM]
    if mismatches:
        raise RuntimeError(f"경계 불일치 {label}: 실제 {[round(value, 2) for value in bounds]}, 기대 {[round(value, 2) for value in expected]}")
    return bounds


def destroy_actors(actors, created):
    for actor in created:
        try:
            actors.destroy_actor(actor)
        except Exception:
            pass


def find_unexpected_level_actors(actors, created):
    created_names = {actor.get_name() for actor in created}
    return sorted(actor.get_actor_label() for actor in actors.get_all_level_actors() if not is_default_level_actor(actor) and actor.get_name() not in created_names)


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def check_zone_actor_contract(zone_class):
    default_actor = unreal.get_default_object(zone_class)
    return [f"TDFog3DZoneActor.{name}" for name in (ZONE_PROPERTY_NAMES["half_extent"], ZONE_PROPERTY_NAMES["material"]) if not has_editor_property(default_actor, name)]


def place_solid(context, solid, finish):
    actors, cube = context["actors"], context["cube"]
    center = solid["center"]
    size = solid["size"]
    rotation = unreal.Rotator(roll=0.0, pitch=solid["angle_deg"] if solid["kind"] == "ramp" else 0.0, yaw=solid.get("yaw_deg", 0.0))
    actor = finish(actors.spawn_actor_from_object(cube, unreal.Vector(*center), rotation), solid_actor_label(solid))
    actor.set_actor_scale3d(unreal.Vector(size[0] / CUBE_SIZE_CM, size[1] / CUBE_SIZE_CM, size[2] / CUBE_SIZE_CM))
    component = actor.static_mesh_component
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    component.set_material(0, context["floor_material"] if solid["material"] == "floor" else context["wall_material"])
    if solid["tagged"]:
        actor.set_editor_property("tags", [unreal.Name(TERRAIN_TAG)])
        read_tags = [str(tag) for tag in actor.get_editor_property("tags")]
        if read_tags != [TERRAIN_TAG]:
            raise RuntimeError(f"태그 확인 실패 {solid['label']}: {read_tags}")
    return actor, require_bounds(actor, solid["label"], solid_aabb(solid))


def place_camera(context, finish, station, camera_name):
    position, target = camera_world(station, camera_name)
    pitch, yaw = camera_rotation_degrees(position, target)
    actor = finish(context["actors"].spawn_actor_from_class(unreal.CameraActor, unreal.Vector(*position), unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw)), camera_label(station, camera_name)[len(LABEL_PREFIX):])
    actor.camera_component.set_editor_property("field_of_view", CAMERA_FOV_DEGREES)
    read_fov = actor.camera_component.get_editor_property("field_of_view")
    if abs(read_fov - CAMERA_FOV_DEGREES) > 1e-3:
        raise RuntimeError(f"카메라 시야각 확인 실패 {station}/{camera_name}: {read_fov}")
    return {"label": actor.get_actor_label(), "position": list(position), "target": list(target), "pitch": pitch, "yaw": yaw, "fov": CAMERA_FOV_DEGREES}


def place_stage(context):
    actors, cube, created = context["actors"], context["cube"], context["created"]
    records = {}

    def finish(actor, label):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        full_label = label if label.startswith(LABEL_PREFIX) else actor_label(label)
        actor.set_actor_label(full_label)
        actor.set_folder_path(ACTOR_FOLDER)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, rotation=None):
        return finish(actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator()), label)

    solid_records = []
    for solid in context["solids"]:
        actor, bounds = place_solid(context, solid, finish)
        solid_records.append({"label": actor.get_actor_label(), "kind": solid["kind"], "station": solid["station"], "tagged": solid["tagged"], "bounds": bounds})
    records["solids"] = solid_records

    records["cameras"] = [place_camera(context, finish, station, name) for station in STATION_CENTERS_CM for name in CAMERA_NAMES]

    sun = spawn(unreal.DirectionalLight, SUN_LOCATION_CM, SUN_LABEL, unreal.Rotator(pitch=SUN_ROTATION_DEGREES["pitch"], yaw=SUN_ROTATION_DEGREES["yaw"]))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(SUN_INTENSITY)
    sky = spawn(unreal.SkyLight, (0.0, 0.0, 1500.0), "Fog3D_SkyLight")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(SKY_LIGHT_INTENSITY)
    spawn(unreal.SkyAtmosphere, (0.0, 0.0, 0.0), "Fog3D_SkyAtmosphere")
    height_fog = spawn(unreal.ExponentialHeightFog, (0.0, 0.0, 0.0), "Fog3D_HeightFog")
    fog_component = height_fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    applied_fog = {}
    for name, value in HEIGHT_FOG_PROPERTIES:
        fog_component.set_editor_property(name, value)
        applied_fog[name] = value
    records["height_fog"] = applied_fog

    post_process = spawn(unreal.PostProcessVolume, (0.0, 0.0, 0.0), "Fog3D_Exposure")
    post_process.set_editor_property("unbound", True)
    settings = post_process.settings
    for name, value in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL), ("auto_exposure_apply_physical_camera_exposure", False), ("auto_exposure_bias", 0.0)):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post_process.set_editor_property("settings", settings)

    spawn(unreal.PlayerStart, PLAYER_START_CM, "Fog3D_PlayerStart")

    zone = spawn(context["zone_class"], ZONE_CENTER_CM, ZONE_LABEL)
    volume_component = find_zone_volume_component(zone)
    if volume_component is None:
        raise RuntimeError("구역 액터의 ZoneVolume 컴포넌트를 찾지 못했습니다.")
    volume_component.set_editor_property("static_mesh", cube)
    if volume_component.get_editor_property("static_mesh") != cube:
        raise RuntimeError("ZoneVolume 메시 지정 확인 실패")
    zone.set_editor_property(ZONE_PROPERTY_NAMES["material"], context["volume_material"])
    zone.set_editor_property(ZONE_PROPERTY_NAMES["half_extent"], unreal.Vector(*ZONE_HALF_EXTENT_CM))
    if hasattr(zone, "rerun_construction_scripts"):
        zone.rerun_construction_scripts()
    read_extent = zone.get_editor_property(ZONE_PROPERTY_NAMES["half_extent"])
    if max(abs(read_extent.x - ZONE_HALF_EXTENT_CM[0]), abs(read_extent.y - ZONE_HALF_EXTENT_CM[1]), abs(read_extent.z - ZONE_HALF_EXTENT_CM[2])) > 1e-3:
        raise RuntimeError(f"구역 반크기 다시 읽기 불일치: {read_extent}")
    read_material = zone.get_editor_property(ZONE_PROPERTY_NAMES["material"])
    if read_material is None or not read_material.get_path_name().startswith(VOLUME_MATERIAL_PATH):
        raise RuntimeError(f"구역 재질 다시 읽기 불일치: {read_material}")
    zone_expected = (ZONE_CENTER_CM[0] - ZONE_HALF_EXTENT_CM[0], ZONE_CENTER_CM[1] - ZONE_HALF_EXTENT_CM[1], ZONE_CENTER_CM[2] - ZONE_HALF_EXTENT_CM[2],
                     ZONE_CENTER_CM[0] + ZONE_HALF_EXTENT_CM[0], ZONE_CENTER_CM[1] + ZONE_HALF_EXTENT_CM[1], ZONE_CENTER_CM[2] + ZONE_HALF_EXTENT_CM[2])
    records["zone"] = {"label": zone.get_actor_label(), "center": list(ZONE_CENTER_CM), "half_extent_cm": list(ZONE_HALF_EXTENT_CM), "volume_material": VOLUME_MATERIAL_PATH, "bounds": require_bounds(zone, ZONE_LABEL, zone_expected)}
    records["player_start"] = list(PLAYER_START_CM)
    return records


def main():
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    edge_fade_cm, _ = resolve_layout_thresholds(read_fog3d_settings())
    solids = build_solids()
    layout_problems = find_layout_problems(solids, edge_fade_cm)
    if layout_problems:
        raise RuntimeError("무대 배치 자체 점검 실패: " + " | ".join(layout_problems))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    overwrite = read_overwrite_request()
    if assets.does_asset_exist(STAGE_MAP_PATH) and not overwrite:
        print(json.dumps({"status": "기존 맵이 있어 아무것도 바꾸지 않았습니다", "existing_assets": [STAGE_MAP_PATH], "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True}}, ensure_ascii=False))
        raise RuntimeError("기존 맵 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 프리셋 정본 검사·다시 읽기")
    preset_tuning_sha256 = reload_preset_tuning()

    game_mode = assets.load_blueprint_class(GAME_MODE_PATH)
    cube = assets.load_asset(CUBE_MESH_PATH)
    volume_material = assets.load_asset(VOLUME_MATERIAL_PATH) if assets.does_asset_exist(VOLUME_MATERIAL_PATH) else None
    zone_class = unreal.load_class(None, ZONE_ACTOR_CLASS_PATH)
    required = {"게임 모드": game_mode, "큐브": cube, "3D 안개 재질(editor_make_fog3d_material.py 먼저)": volume_material, "구역 액터 클래스(빌드·에디터 재시작 필요)": zone_class}
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음: " + ", ".join(missing))
    contract_problems = check_zone_actor_contract(zone_class)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음: " + ", ".join(contract_problems))
    floor_material, floor_material_path = pick_stage_material(assets, FLOOR_MATERIAL_PATH)
    wall_material, wall_material_path = pick_stage_material(assets, WALL_MATERIAL_PATH)

    print("단계: 레벨")
    ensure_directory(MAP_DIR)
    open_empty_stage_level(levels, actors)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    created = []
    context = {"actors": actors, "cube": cube, "created": created, "zone_class": zone_class, "volume_material": volume_material, "solids": solids,
               "floor_material": floor_material, "wall_material": wall_material}
    try:
        records = place_stage(context)
        unexpected = find_unexpected_level_actors(actors, created)
        if unexpected:
            raise RuntimeError("저장 전에 무대에 예상 밖 액터가 있습니다(임시 물체 금지): " + ", ".join(unexpected))
    except Exception:
        destroy_actors(actors, created)
        raise

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    saved_map_path = project_path("Content", "FluidFX", "Maps", "LV_TDFog3DGroundStage.umap")
    if not os.path.isfile(saved_map_path):
        raise RuntimeError("저장 파일 확인 실패: " + saved_map_path)
    unsaved_after = find_unsaved_package_names(False)
    if unsaved_after:
        raise RuntimeError("저장 뒤에도 미저장 패키지가 있습니다: " + ", ".join(unsaved_after))

    report = {
        "status": "ok", "map": STAGE_MAP_PATH, "overwrite": overwrite, "preset_tuning_sha256": preset_tuning_sha256, "volume_material": VOLUME_MATERIAL_PATH,
        "floor_material": floor_material_path, "wall_material": wall_material_path, "zone_label": actor_label(ZONE_LABEL), "terrain_tag": TERRAIN_TAG, "actor_folder": ACTOR_FOLDER,
        "records": records, "created_actors": len(created), "tagged_actor_count": sum(1 for solid in solids if solid["tagged"]),
        "actors": [{"label": actor.get_actor_label(), "class": actor.get_class().get_name(), "bounds": describe_bounds(actor)} for actor in created],
        "lookdev_command": LOOKDEV_COMMAND, "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps({"status": report["status"], "created_actors": report["created_actors"], "tagged_actor_count": report["tagged_actor_count"]}, ensure_ascii=False))
    print("보고서: " + report_path)


RETEXTURE_REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fog3d_ground_stage_retexture.json")


def read_stage_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return {}
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return request if isinstance(request, dict) else {}


def summarize_stage_actors(actors):
    entries = {}
    for actor in actors.get_all_level_actors():
        component = getattr(actor, "static_mesh_component", None)
        material = component.get_material(0) if component is not None else None
        entries[actor.get_actor_label()] = {
            "bounds": describe_bounds(actor), "tags": [str(tag) for tag in actor.get_editor_property("tags")],
            "material": material.get_path_name() if material is not None else None,
        }
    return entries


def retexture_main():
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    unsaved_names = find_unsaved_package_names(False)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))
    if not levels.load_level(STAGE_MAP_PATH):
        raise RuntimeError(f"무대 레벨 열기 실패: {STAGE_MAP_PATH}")
    floor_material, floor_material_path = pick_stage_material(assets, FLOOR_MATERIAL_PATH)
    if floor_material_path != FLOOR_MATERIAL_PATH:
        raise RuntimeError("평지 바닥 재질 없음: " + FLOOR_MATERIAL_PATH)
    before = summarize_stage_actors(actors)
    by_label = {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()}
    changed = []
    for solid in build_solids():
        label = solid_actor_label(solid)
        actor = by_label.get(label)
        if actor is None:
            raise RuntimeError(f"조각 액터 없음: {label}")
        component = actor.static_mesh_component
        current = component.get_material(0)
        if current is not None and current.get_path_name().startswith(FLOOR_MATERIAL_PATH):
            continue
        component.set_material(0, floor_material)
        changed.append(label)
    after = summarize_stage_actors(actors)
    differences = [label for label in before if label not in after or before[label]["bounds"] != after[label]["bounds"] or before[label]["tags"] != after[label]["tags"]]
    if differences or len(before) != len(after):
        raise RuntimeError("재질 바꾸기 뒤 모양·태그·액터 수가 달라졌습니다: " + ", ".join(differences))
    wrong = [solid["label"] for solid in build_solids() if not (after[solid_actor_label(solid)]["material"] or "").startswith(FLOOR_MATERIAL_PATH)]
    if wrong:
        raise RuntimeError("재질 다시 읽기 불일치: " + ", ".join(wrong))
    tagged_after = sum(1 for entry in after.values() if TERRAIN_TAG in entry["tags"])
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    unsaved_after = find_unsaved_package_names(False)
    if unsaved_after:
        raise RuntimeError("저장 뒤에도 미저장 패키지가 있습니다: " + ", ".join(unsaved_after))
    report = {"status": "ok", "mode": "retexture_only", "map": STAGE_MAP_PATH, "actor_count_before": len(before), "actor_count_after": len(after),
              "tagged_count_before": sum(1 for entry in before.values() if TERRAIN_TAG in entry["tags"]), "tagged_count_after": tagged_after,
              "changed_labels": changed, "actors_after": after, "materials_before": {label: before[label]["material"] for label in changed}, "bounds_identical": True, "saved": True}
    report_path = project_path(*RETEXTURE_REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2)
    print(json.dumps({"status": "ok", "actors": len(after), "tagged": tagged_after, "changed": len(changed)}, ensure_ascii=False))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        if read_stage_request().get("retexture_only") is True:
            retexture_main()
            return
        main()
    except Exception as error:
        write_report({"status": "failed", "error": str(error)})
        raise


def load_old_stage_module():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), OLD_STAGE_SCRIPT_NAME)
    module_spec = importlib.util.spec_from_file_location("editor_make_fog3d_stage_reference", path)
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def print_layout():
    solids = build_solids()
    print("| 칸 | 중심(x,y) | 내용 | 걷는 선 |")
    print("|---|---|---|---|")
    for station, center in STATION_CENTERS_CM.items():
        lines = ", ".join(f"{line['id']}" for line in WALK_LINES if line["station"] == station)
        print(f"| {station} | ({center[0]:.0f}, {center[1]:.0f}) | {STATION_TITLES[station]} | {lines} |")
    print()
    print("| 구조물 | 종류 | 중심(x,y,z) | 크기 또는 기울기 | 태그 |")
    print("|---|---|---|---|---|")
    for solid in solids:
        center = solid["center"]
        if solid["kind"] == "ramp":
            detail = f"{solid['angle_deg']:.0f}도 길이 {solid['length']:.1f} 폭 {solid['size'][1]:.0f} 두께 {solid['thickness']:.0f} 올림 {solid['rise']:.0f} 방향 {'+x' if solid['direction'] > 0 else '-x'}"
        else:
            detail = f"{solid['size'][0]:.0f}×{solid['size'][1]:.0f}×{solid['size'][2]:.0f}"
        print(f"| {solid['label']} | {solid['kind']} | ({center[0]:.1f}, {center[1]:.1f}, {center[2]:.1f}) | {detail} | {'예' if solid['tagged'] else '아니오'} |")
    print()
    print("| 선 | 칸 | 시작(세계 x,y) | 방향 | 길이 | 끝 높이 기울기 | 우선 | 설명 |")
    print("|---|---|---|---|---|---|---|---|")
    for line in WALK_LINES:
        start = line_start_world(line)
        print(f"| {line['id']} | {line['station']} | ({start[0]:.1f}, {start[1]:.1f}, z {line_start_height(solids, line):.0f}) | {line['yaw']:.0f}도 | {line['distance_cm']:.0f} | {line_end_slope_degrees(solids, line):.1f}도 | {line['priority']} | {line['description']} |")


def run_self_test():
    results = []

    def record(label, passed, detail=""):
        results.append(passed)
        print(("PASS " if passed else "FAIL ") + label + (": " + detail if detail else ""))

    settings = read_fog3d_settings()
    edge_fade_cm, vertical_margin_cm = resolve_layout_thresholds(settings)
    solids = build_solids()
    problems = find_layout_problems(solids, edge_fade_cm)
    record("① 배치 점검(구역 높이·슬래브·칸 간격·겹침·구멍·태그·카메라·걷는 선 전부)", not problems, "; ".join(problems[:6]))

    tall = [dict(solid, center=(solid["center"][0], solid["center"][1], solid["center"][2] + (400.0 if solid["label"] == "SC_Curb100" else 0.0))) for solid in solids]
    record("③ 가장 높은 땅을 400cm 올림 → 구역 윗면 문제", any("구역 윗면" in line for line in find_layout_problems(tall, edge_fade_cm)))
    holeless = [dict(solid) for solid in solids if solid["label"] != "HO_PitFloor"]
    record("④ 구멍 바닥을 지움 → 구멍 바닥 문제", any("구멍 바닥" in line for line in find_layout_problems(holeless, edge_fade_cm)))
    tagged_pit = [dict(solid, tagged=True) if solid["label"] == "HO_PitFloor" else solid for solid in solids]
    record("⑤ 구멍 바닥에 태그를 달음 → 구멍 안 땅 문제", any("구멍 안에 땅" in line for line in find_layout_problems(tagged_pit, edge_fade_cm)))
    steep = [dict(solid, angle_deg=46.0) if solid["label"] == "S40_Ramp" else solid for solid in solids]
    record("⑥ 40도 비탈을 46도 로 바꿈 → 걸을 수 있는 한계 문제", any("한계" in line for line in find_layout_problems(steep, edge_fade_cm)))
    wall_on_line = solids + [make_box("Extra_Wall", "F", (-500.0, -450.0), (-100.0, 100.0), (0.0, 120.0))]
    record("⑦ 평지 걷는 선 위에 120cm 벽을 세움 → 벽 문제", any("벽" in line for line in find_walk_line_problems(wall_on_line, find_line("W01"))))
    cliff_line = make_line("X1", "CL", "fall", (-750.0, 0.0), 0, 800, 2, "절벽 끝을 지나 떨어짐")
    record("⑧ 절벽 가장자리를 넘는 선 → 낭떠러지 문제", any("낭떠러지" in line for line in find_walk_line_problems(solids, cliff_line)))

    down_slope = round(line_end_slope_degrees(solids, find_line("W03")), 1)
    up_slope = round(line_end_slope_degrees(solids, find_line("W02")), 1)
    record("⑨ 30도 오르막·내리막 끝 기울기 +30.0 / −30.0", abs(up_slope - 30.0) < 0.2 and abs(down_slope + 30.0) < 0.2, f"{up_slope} / {down_slope}")
    ridge_length = next(solid for solid in solids if solid["label"] == "S30_RampUp")["length"]
    record("⑩ 30도 비탈 길이 8m 이상(비탈면 따라)", ridge_length >= 800.0, f"{ridge_length:.1f}")

    ramp = next(solid for solid in solids if solid["label"] == "S30_RampUp")
    bounds = solid_aabb(ramp)
    record("⑪ 비탈 판 경계 상자: 윗면 끝 높이 450, 아래 끝은 바닥 속(−두께·cos30)", abs(bounds[5] - 450.0) < 0.01 and abs(bounds[2] + ramp["thickness"] * math.cos(math.radians(30.0))) < 0.01, str([round(value, 1) for value in bounds]))
    down_ramp = next(solid for solid in solids if solid["label"] == "S30_RampDown")
    record("⑫ 내리막 판은 −x 로 오른다(발끝에서 멀수록 높음)", surface_height(down_ramp, down_ramp["foot_x"] - 300.0, 0.0) > surface_height(down_ramp, down_ramp["foot_x"] - 100.0, 0.0))

    tagged_count = sum(1 for solid in solids if solid["tagged"])
    record("⑬ 태그를 다는 땅 메시 수(슬래브 4 + 구조물) / 태그 없음(구멍 바닥 1)", tagged_count == len(solids) - 1, f"{tagged_count} / {len(solids) - tagged_count}")

    rows = grid_phase_rows(solids)
    classes = {classify_phase(row["phase"]) for row in rows}
    record("⑭ 15.625cm 격자: 경계에 놓인 면과 어긋난 면이 모두 있음", "경계" in classes and "어긋남" in classes, f"{len(rows)}개 면, 분류 {sorted(classes)}")
    cliff_face = next(row for row in rows if row["label"] == "CL_Upper" and row["axis"] == "x" and abs(row["world_cm"] - 6000.0) < 1e-6)
    record("⑮ 절벽 모서리 x=6000 은 격자 경계(384칸 정수)", abs(cliff_face["cells"] - 384.0) < 1e-6, str(cliff_face["cells"]))

    header_path = project_path(*ZONE_HEADER_RELATIVE_PATH)
    if os.path.isfile(header_path):
        with open(header_path, encoding="utf-8", errors="replace") as header_file:
            missing_header_names = find_missing_header_names(header_file.read())
        record("⑯ TDFog3DZoneActor.h 에 ZoneHalfExtentCm·VolumeMaterial·ZoneVolume 선언", not missing_header_names, ", ".join(missing_header_names))
    else:
        record("⑯ TDFog3DZoneActor.h 아직 없음 → 계약 3절 이름 사용", True, "헤더 대조 건너뜀")

    old_stage = load_old_stage_module()
    same_environment = (old_stage.SUN_ROTATION_DEGREES == SUN_ROTATION_DEGREES and old_stage.SUN_INTENSITY == SUN_INTENSITY and old_stage.SKY_LIGHT_INTENSITY == SKY_LIGHT_INTENSITY
                        and tuple(old_stage.HEIGHT_FOG_PROPERTIES) == tuple(HEIGHT_FOG_PROPERTIES) and old_stage.SUN_LOCATION_CM == SUN_LOCATION_CM)
    record("⑰ 햇빛·하늘·높이 안개 값이 1단계 무대와 같음", same_environment)
    record("⑱ 새 경로 상수(맵·재질)와 1단계 무대 경로와 다름", STAGE_MAP_PATH == "/Game/FluidFX/Maps/LV_TDFog3DGroundStage" and GROUND_MATERIAL_PATH == "/Game/FluidFX/Materials/M_TD_Fog3DVolumeGround" and STAGE_MAP_PATH != old_stage.STAGE_MAP_PATH)
    record("⑲ 요청·보고 파일 이름이 1단계 무대와 다름", REQUEST_RELATIVE_PATH != old_stage.REQUEST_RELATIVE_PATH and REPORT_RELATIVE_PATH != old_stage.REPORT_RELATIVE_PATH)
    record("⑳ 햇빛·구역 라벨이 1단계 무대와 같음(기존 빛 키 재사용)", actor_label(SUN_LABEL) == "TDGen_Fog3D_Sun" and actor_label(ZONE_LABEL) == "TDGen_Fog3D_Zone" and old_stage.LABEL_PREFIX == LABEL_PREFIX)
    record("㉑ 태그 문자열 TDFluidTerrain", TERRAIN_TAG == "TDFluidTerrain")
    sample = {"status": "ok", "records": {"zone": {"center": list(ZONE_CENTER_CM)}}, "lookdev_command": LOOKDEV_COMMAND}
    record("㉒ 보고서 JSON 직렬화", json.loads(json.dumps(sample, ensure_ascii=False))["status"] == "ok")
    return 0 if all(results) else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv[1:]:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.exit(run_self_test())
    if "--print-layout" in sys.argv[1:]:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        print_layout()
        sys.exit(0)
    run_with_failure_report()
