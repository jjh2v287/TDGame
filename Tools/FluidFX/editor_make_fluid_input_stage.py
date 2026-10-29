"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 새 UCLASS 2개(UTDFluidFieldInputComponent·UTDFluidMeshFieldEditorLibrary)가 빌드돼 있어야 하므로 실행 전 python Tools/ue_editor.py restart(L-build-01). 먼저 make_fluid_field_input_textures.py, editor_make_fluid_particle_stage.py(교반기 BP), editor_make_fluid_smoke_stage.py(볼륨 연기 재질), editor_make_fluid_fire_stage.py(바닥 MI)가 끝나 있어야 하고, 재질 값 조각 병합은 모두 이 무대보다 먼저다(L-repo-27 ①).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_input_stage.py (PowerShell, L-repo-01). run_in_editor.py 는 스크립트가 실패해도 SUCCESS 를 낼 수 있으니 출력 본문의 TD_FLUID_INPUT_STAGE PASS|FAIL 줄로 판정한다(L-editor-73, 반박 S6).
출력: /Game/FluidFX/Textures/Input/T_TD_FluidFieldWindSwirl·DensityRings·MaskMaze(TC_VectorDisplacementmap·sRGB 끔·밉 없음·never_stream)·T_TD_FluidMeshField_Cube·Cylinder(BakeMeshSliceField), /Game/FluidFX/Destruction/GC_TD_FluidInputBricks, /Game/FluidFX/Presets/DA_TDFluid_InputBrushBench·InputFieldBench(기존 연기 볼륨 재질 슬롯 재사용, 새 재질 없음), /Game/FluidFX/Maps/LV_TDFluidInputStage(Z1 크기·Z2 궤도·Z3 정지·Z4 필드·Z5 메시 필드·Z6 조각 자국·Z7 다중 터치), Saved/FluidFX/fluid_input_stage.json(preset_tuning_sha256·material_look_sha256·텍스처 sha256·굽기 통계·장면 카메라). 기존 에셋은 Saved/FluidFX/fluid_input_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고 미저장 작업이 있으면 중단한다.
상태: 미검증(2026-10-29 FX-15b 에셋 도구 초안: py_compile·가짜 unreal 배치 검사만, 에디터 실행 전). 교반기 BP(회전 이동)로 움직임을 구성만 하므로 Z1 왕복은 원 궤도, Z3 정지는 창을 한 번 가로질러 빠져나가는 큰 궤도로 대신한다. MeshDistance 필드 컴포넌트는 굽기 반환값 field_size_cm 을 그대로 쓰고 소유 액터·메시 배율을 1로 둔다(반박 S12). 굽기 반환 status 의 성공 문자열은 BAKE_SUCCESS_STATUSES 로 판정하며 B6e 계약과 다르면 상수를 맞춘다.
"""
import hashlib
import importlib.util
import json
import math
import os
import traceback

import unreal

FLUID_ROOT = "/Game/FluidFX"
TEXTURE_DIR = f"{FLUID_ROOT}/Textures/Input"
DESTRUCTION_DIR = f"{FLUID_ROOT}/Destruction"
STAGE_MAP_PATH = f"{FLUID_ROOT}/Maps/LV_TDFluidInputStage"
BRICK_COLLECTION_NAME = "GC_TD_FluidInputBricks"
BRICK_COLLECTION_PATH = f"{DESTRUCTION_DIR}/{BRICK_COLLECTION_NAME}"
FIELD_COMPONENT_CLASS_PATH = "/Script/TDGame.TDFluidFieldInputComponent"
TEXTURE_SOURCE_RELATIVE_PATH = ("Saved", "FluidFX", "field_input_textures")
TEXTURE_SOURCE_REPORT_NAME = "field_input_textures.json"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_input_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_input_stage.json")
STAGE_FOLDER = "TDGen/FluidFX/InputStage"
REPORT_MARKER = "TD_FLUID_INPUT_STAGE"

ZONE_SPACING_CM = 3000.0
WINDOW_HALF_EXTENT_CM = 1000.0
WINDOW_MARGIN_CM = 100.0
EXPECTED_GRID_RESOLUTION = 256
EXPECTED_EXTENT_CM = 2000.0
LIVE_ACTOR_HEIGHT_CM = 1.0
BASIC_SHAPE_SIZE_CM = 100.0
FIELD_MARKER_HEIGHT_CM = 2.0
CAMERA_VIEW = (-60.0, 90.0)

MESH_SLICE_HEIGHT_CM = 0.0
MESH_RESOLUTION = 128
MESH_PADDING_CM = 50.0
BAKE_SUCCESS_STATUSES = ("ok", "success")

TEXTURE_SPECS = (
    {"key": "wind", "name": "T_TD_FluidFieldWindSwirl", "layout": "VELOCITY", "signed": True},
    {"key": "rings", "name": "T_TD_FluidFieldDensityRings", "layout": "DENSITY", "signed": False},
    {"key": "maze", "name": "T_TD_FluidFieldMaskMaze", "layout": "COLLISION_MASK", "signed": False},
)
MESH_FIELD_SPECS = (
    {"key": "cube", "mesh": "/Engine/BasicShapes/Cube", "name": "T_TD_FluidMeshField_Cube"},
    {"key": "cylinder", "mesh": "/Engine/BasicShapes/Cylinder", "name": "T_TD_FluidMeshField_Cylinder"},
)
PRESET_SPECS = (
    {"name": "InputBrushBench", "kind": "INPUT_BRUSH_BENCH"},
    {"name": "InputFieldBench", "kind": "INPUT_FIELD_BENCH"},
)
FRAGMENT_TRAIL_KIND_NAME = "INPUT_BRUSH_BENCH"

BRICK_COUNT = (4, 4, 4)
BRICK_SIZE_CM = 25.0
BLOCK_HEIGHT_CM = 50.0
BREAK_FORCE = 650.0
BREAK_FORCE_RADIUS = 260.0
BREAK_HIT_OFFSET_CM = (-55.0, 0.0, -35.0)

SIZE_BODIES = (
    {"name": "SmallBox", "mesh": "cube", "half_extent_cm": 50.0, "pivot_offset_cm": (0.0, 0.0), "orbit_radius_cm": 350.0, "start_angle_degrees": 0.0,
     "speed_cm_per_sec": 300.0, "height_cm": 50.0, "collides": True},
    {"name": "LargeBox", "mesh": "cube", "half_extent_cm": 100.0, "pivot_offset_cm": (0.0, 0.0), "orbit_radius_cm": 650.0, "start_angle_degrees": 180.0,
     "speed_cm_per_sec": 300.0, "height_cm": 100.0, "collides": True},
)
ORBIT_BODIES = (
    {"name": "FastBox", "mesh": "cube", "half_extent_cm": 50.0, "pivot_offset_cm": (0.0, 0.0), "orbit_radius_cm": 800.0, "start_angle_degrees": 0.0,
     "speed_cm_per_sec": 2000.0, "height_cm": 50.0, "collides": True},
)
PASSING_BODIES = (
    {"name": "PassingBox", "mesh": "cube", "half_extent_cm": 50.0, "pivot_offset_cm": (0.0, -1500.0), "orbit_radius_cm": 1500.0, "start_angle_degrees": 90.0,
     "speed_cm_per_sec": 600.0, "height_cm": 50.0, "collides": True, "leaves_window": True},
)
MESH_BODIES = (
    {"name": "CylinderShip", "mesh": "cylinder", "half_extent_cm": 50.0, "pivot_offset_cm": (0.0, 0.0), "orbit_radius_cm": 250.0, "start_angle_degrees": 0.0,
     "speed_cm_per_sec": 150.0, "height_cm": 50.0, "collides": False, "field": "cylinder"},
    {"name": "CubeSpinner", "mesh": "cube", "half_extent_cm": 50.0, "pivot_offset_cm": (-550.0, 0.0), "orbit_radius_cm": 0.0, "start_angle_degrees": 0.0,
     "spin_degrees_per_sec": 60.0, "height_cm": 50.0, "collides": False, "field": "cube"},
)
FIELD_ACTOR_SPECS = (
    {"name": "WindSwirl", "texture": "wind", "offset_cm": (-600.0, 0.0), "size_cm": 520.0},
    {"name": "DensityRings", "texture": "rings", "offset_cm": (0.0, 0.0), "size_cm": 520.0},
    {"name": "MaskMaze", "texture": "maze", "offset_cm": (600.0, 0.0), "size_cm": 520.0},
)
BRICK_BLOCK_EXTENT_CM = 100.0

ZONE_SPECS = (
    {"id": "Z1", "label": "InputSize", "layout": "bodies", "preset": "InputBrushBench", "bodies": SIZE_BODIES, "arm": 1500.0,
     "refs": ["B12 크기 비례 반경(반폭 50 대 100 상자 홈 폭 비)"], "motion_note": "원 궤도 두 개(교반기 BP는 왕복 이동이 없어 왕복 대신)"},
    {"id": "Z2", "label": "InputOrbit", "layout": "bodies", "preset": "InputBrushBench", "bodies": ORBIT_BODIES, "arm": 2200.0,
     "refs": ["B14 선분 간격·B12 앞당김(2000 cm/s 궤도, IMG 22 연속 홈)"], "motion_note": "반경 800 cm 원 궤도, 선속도 2000 cm/s"},
    {"id": "Z3", "label": "InputStop", "layout": "bodies", "preset": "InputBrushBench", "bodies": PASSING_BODIES, "arm": 2600.0,
     "refs": ["B15 잔류 스탬프(입력이 끊긴 뒤 휘도 반감 시간)"], "motion_note": "큰 궤도로 창을 한 번 가로질러 나간다(정지 대신 입력 끊김)"},
    {"id": "Z4", "label": "InputField", "layout": "fields", "preset": "InputFieldBench", "arm": 2400.0,
     "refs": ["B10 필드 입력(바람 소용돌이 속도·고리 밀도·미로 충돌)"], "motion_note": "정지 필드 3장"},
    {"id": "Z5", "label": "InputMesh", "layout": "mesh", "preset": "InputFieldBench", "bodies": MESH_BODIES, "arm": 1200.0,
     "refs": ["B6 단면 SDF 굽기(IMG 16·20 바위 둘레, 배 대리)"], "motion_note": "원기둥은 반경 250 cm 궤도, 큐브는 제자리 회전"},
    {"id": "Z6", "label": "InputFragment", "layout": "fragment", "preset": "InputBrushBench", "arm": 600.0,
     "refs": ["B9·N1 조각 자국(점+자국 방식, 불 대상 없이 연기만)"], "motion_note": "PIE 에서 trigger_break_at 으로 부순다"},
    {"id": "Z7", "label": "InputTouch", "layout": "touch", "preset": "InputBrushBench", "arm": 1500.0,
     "refs": ["B11 다중 터치(PIE 수동)"], "motion_note": "PIE 수동 입력"},
)

LIVE_ACTOR_NEW_PROPERTIES = (
    "interaction_object_types",
    "tracked_actor_tags",
    "excluded_actors",
    "interact_with_owner",
    "owner_bone_names",
    "owner_component_names",
    "pointer_input_mode",
    "mouse_brush_key",
)
FIELD_COMPONENT_REQUIRED_PROPERTIES = (
    "field_texture",
    "field_size_cm",
    "layout",
    "uses_signed_velocity_encoding",
    "uv_tile_count",
    "uv_scroll_per_sec",
    "uv_rotation_degrees",
)
TRAIL_NEW_PROPERTIES = (
    "fragment_input_mode",
    "footprint_density_strength",
    "footprint_velocity_strength",
    "footprint_hardness",
    "footprint_max_height_cm",
)
PRESET_EXTRA_PROPERTIES = ("default_channel", "plane_settings", "volume_render", "volume_material", "volume_mesh", "volume_height_cm")
EXTRA_REQUIRED_UNREAL_NAMES = (
    "StaticMeshActor",
    "StaticMesh",
    "Vector2D",
    "IntVector",
    "GeometryCollectionComponent",
    "CollisionEnabled.NO_COLLISION",
    "AssetImportTask",
    "TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP",
    "TextureMipGenSettings.TMGS_NO_MIPMAPS",
    "TextureAddress.TA_CLAMP",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "TDFluidPresetKind.INPUT_BRUSH_BENCH",
    "TDFluidPresetKind.INPUT_FIELD_BENCH",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetAsset.build_plane_settings_for_preset_kind",
    "TDFluidPresetAsset.build_volume_render_for_preset_kind",
    "TDFluidPresetAsset.build_fragment_trail_for_preset_kind",
    "TDFluidFieldInputComponent",
    "TDFluidFieldInputLayout.DENSITY",
    "TDFluidFieldInputLayout.VELOCITY",
    "TDFluidFieldInputLayout.COLLISION_MASK",
    "TDFluidFieldInputLayout.MESH_DISTANCE",
    "TDFluidMeshFieldEditorLibrary.bake_mesh_slice_field",
    "TDFluidPointerInputMode.MULTI_TOUCH",
    "TDFluidFragmentInputMode.POINTS_AND_FOOTPRINT",
    "TDFluidFragmentTrailSettings",
    "TDFluidFragmentTarget",
    "TDFluidFractureEditorLibrary.create_brick_geometry_collection",
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


def file_sha256_of_path(path):
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def body_degrees_per_sec(body):
    if body["orbit_radius_cm"] > 0.0:
        return math.degrees(body["speed_cm_per_sec"] / body["orbit_radius_cm"])
    return body["spin_degrees_per_sec"]


def body_local_pivot_offset(body):
    angle = math.radians(body["start_angle_degrees"])
    return (-body["orbit_radius_cm"] * math.cos(angle), -body["orbit_radius_cm"] * math.sin(angle))


def body_start_offset(body):
    angle = math.radians(body["start_angle_degrees"])
    return (body["pivot_offset_cm"][0] + body["orbit_radius_cm"] * math.cos(angle), body["pivot_offset_cm"][1] + body["orbit_radius_cm"] * math.sin(angle))


def body_path_extent(body):
    field_half_size = body["half_extent_cm"] + (MESH_PADDING_CM if body.get("field") else 0.0)
    reach = body["orbit_radius_cm"] + field_half_size * math.sqrt(2.0)
    pivot_x, pivot_y = body["pivot_offset_cm"]
    return (pivot_x - reach, pivot_x + reach, pivot_y - reach, pivot_y + reach)


def zone_center(index):
    return (index * ZONE_SPACING_CM, 0.0)


def validate_stage_layout():
    neighbor_free_limit = ZONE_SPACING_CM - WINDOW_HALF_EXTENT_CM
    in_window_limit = WINDOW_HALF_EXTENT_CM - WINDOW_MARGIN_CM
    problems = []
    for zone in ZONE_SPECS:
        for body in zone.get("bodies", ()):
            x_low, x_high, y_low, y_high = body_path_extent(body)
            if max(abs(x_low), abs(x_high)) >= neighbor_free_limit:
                problems.append(f"{zone['id']} {body['name']}: x 범위 {x_low:.0f}~{x_high:.0f} 이 이웃 구역 창에 닿는다")
            if body.get("leaves_window"):
                continue
            if max(abs(x_low), abs(x_high), abs(y_low), abs(y_high)) > in_window_limit:
                problems.append(f"{zone['id']} {body['name']}: 경로가 창({in_window_limit:.0f} cm) 밖으로 나간다")
    for field in FIELD_ACTOR_SPECS:
        half_size = field["size_cm"] * 0.5
        if max(abs(field["offset_cm"][0]) + half_size, abs(field["offset_cm"][1]) + half_size) > in_window_limit:
            problems.append(f"Z4 {field['name']}: 필드가 창({in_window_limit:.0f} cm) 밖으로 나간다")
    ordered = sorted(FIELD_ACTOR_SPECS, key=lambda field: field["offset_cm"][0])
    for left, right in zip(ordered, ordered[1:]):
        if left["offset_cm"][0] + left["size_cm"] * 0.5 > right["offset_cm"][0] - right["size_cm"] * 0.5:
            problems.append(f"Z4 {left['name']} 와 {right['name']}: 필드가 겹친다")
    if problems:
        raise RuntimeError("무대 배치 검사 실패: " + " | ".join(problems))


def read_texture_source_report():
    source_folder = project_path(*TEXTURE_SOURCE_RELATIVE_PATH)
    report_path = os.path.join(source_folder, TEXTURE_SOURCE_REPORT_NAME)
    if not os.path.isfile(report_path):
        raise RuntimeError(f"텍스처 원본 보고 없음(python Tools/FluidFX/make_fluid_field_input_textures.py 먼저): {report_path}")
    with open(report_path, encoding="utf-8") as report_file:
        report = json.load(report_file)
    if report.get("ok") is not True:
        raise RuntimeError("텍스처 원본 보고의 ok 가 거짓입니다(생성기 검사 실패): " + report_path)
    return source_folder, report


def find_missing_contract_problems(particle_stage, live_actor_class, field_component_class):
    problems = particle_stage.find_missing_properties(unreal.get_default_object(live_actor_class), particle_stage.ACTOR_REQUIRED_PROPERTIES + LIVE_ACTOR_NEW_PROPERTIES)
    problems += particle_stage.find_missing_properties(unreal.get_default_object(field_component_class), FIELD_COMPONENT_REQUIRED_PROPERTIES)
    problems += particle_stage.find_missing_properties(unreal.TDFluidFragmentTrailSettings(), TRAIL_NEW_PROPERTIES)
    problems += particle_stage.find_missing_properties(unreal.TDFluidPresetAsset.get_default_object(), particle_stage.PRESET_REQUIRED_PROPERTIES + PRESET_EXTRA_PROPERTIES)
    return problems


def import_input_texture(particle_stage, spec, source_folder, source_report):
    source_path = os.path.join(source_folder, spec["name"] + ".png")
    expected_sha256 = source_report["textures"][spec["name"]]["sha256"]
    if not os.path.isfile(source_path):
        raise RuntimeError(f"텍스처 원본 없음: {source_path}")
    source_sha256 = file_sha256_of_path(source_path)
    if source_sha256 != expected_sha256:
        raise RuntimeError(f"텍스처 원본 해시가 보고와 다릅니다(생성기를 다시 실행): {source_path}")
    path = f"{TEXTURE_DIR}/{spec['name']}"
    particle_stage.ensure_directory(TEXTURE_DIR)
    task = unreal.AssetImportTask()
    for name, value in (("filename", source_path), ("destination_path", TEXTURE_DIR), ("destination_name", spec["name"]), ("replace_existing", True), ("automated", True), ("save", False)):
        task.set_editor_property(name, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(path)
    if texture is None:
        raise RuntimeError(f"텍스처 가져오기 실패: {path}")
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    mismatches = []
    if texture.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP:
        mismatches.append("compression_settings")
    if texture.get_editor_property("srgb"):
        mismatches.append("srgb")
    if texture.get_editor_property("mip_gen_settings") != unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS:
        mismatches.append("mip_gen_settings")
    if not texture.get_editor_property("never_stream"):
        mismatches.append("never_stream")
    if mismatches:
        raise RuntimeError(f"텍스처 설정 확인 실패({path}): " + ", ".join(mismatches))
    particle_stage.save_asset(texture, path)
    return texture, {"asset": path, "layout": spec["layout"], "uses_signed_velocity_encoding": spec["signed"], "source_sha256": source_sha256,
                     "compression": str(texture.get_editor_property("compression_settings")), "srgb": False, "mip_gen_settings": str(texture.get_editor_property("mip_gen_settings")),
                     "never_stream": True, "size": read_texture_size(texture)}


def read_texture_size(texture):
    try:
        return [int(texture.blueprint_get_size_x()), int(texture.blueprint_get_size_y())]
    except Exception:
        return None


def parse_bake_result(raw_result):
    if raw_result is None:
        raise RuntimeError("BakeMeshSliceField 가 None 을 돌려줌(UE Python 은 실패 시 출력을 잃을 수 있다, L-editor-73)")
    try:
        result = json.loads(str(raw_result))
    except ValueError as error:
        raise RuntimeError(f"BakeMeshSliceField 반환이 JSON 이 아님: {raw_result!r}") from error
    if not isinstance(result, dict):
        raise RuntimeError(f"BakeMeshSliceField 반환이 객체가 아님: {raw_result!r}")
    return result


def bake_mesh_field(particle_stage, spec):
    mesh = unreal.EditorAssetLibrary.load_asset(spec["mesh"])
    if mesh is None:
        raise RuntimeError("굽기 대상 메시 없음: " + spec["mesh"])
    path = f"{TEXTURE_DIR}/{spec['name']}"
    particle_stage.ensure_directory(TEXTURE_DIR)
    result = parse_bake_result(unreal.TDFluidMeshFieldEditorLibrary.bake_mesh_slice_field(mesh, MESH_SLICE_HEIGHT_CM, MESH_RESOLUTION, MESH_PADDING_CM, path))
    status = str(result.get("status", "")).lower()
    if status not in BAKE_SUCCESS_STATUSES:
        raise RuntimeError(f"메시 단면 굽기 실패({spec['mesh']}): status={result.get('status')} message={result.get('message')}")
    field_size = result.get("field_size_cm")
    if not isinstance(field_size, (list, tuple)) or len(field_size) != 2 or min(field_size) <= 0.0:
        raise RuntimeError(f"굽기 반환 field_size_cm 이 올바르지 않음: {field_size}")
    texture = unreal.EditorAssetLibrary.load_asset(path)
    if texture is None:
        raise RuntimeError(f"굽기 뒤 텍스처 로드 실패: {path}")
    return texture, {"asset": path, "mesh": spec["mesh"], "status": result.get("status"), "field_size_cm": [float(field_size[0]), float(field_size[1])],
                     "min_distance_cm": result.get("min_distance_cm"), "max_distance_cm": result.get("max_distance_cm"), "inside_cells": result.get("inside_cells"),
                     "resolution": MESH_RESOLUTION, "padding_cm": MESH_PADDING_CM, "slice_height_local_cm": MESH_SLICE_HEIGHT_CM, "size": read_texture_size(texture)}


def create_brick_collection(particle_stage, cube):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(BRICK_COLLECTION_PATH) and not assets.delete_asset(BRICK_COLLECTION_PATH):
        raise RuntimeError(f"기존 지오메트리 컬렉션 삭제 실패(LV_TDFluidInputStage 를 닫고 다시 실행): {BRICK_COLLECTION_PATH}")
    particle_stage.ensure_directory(DESTRUCTION_DIR)
    brick_count = unreal.IntVector(BRICK_COUNT[0], BRICK_COUNT[1], BRICK_COUNT[2])
    collection = unreal.TDFluidFractureEditorLibrary.create_brick_geometry_collection(cube, DESTRUCTION_DIR, BRICK_COLLECTION_NAME, brick_count, BRICK_SIZE_CM)
    if collection is None:
        raise RuntimeError(f"벽돌 지오메트리 컬렉션 생성 실패: {BRICK_COLLECTION_PATH}")
    particle_stage.save_asset(collection, BRICK_COLLECTION_PATH)
    return collection


def configure_input_preset(particle_stage, spec, smoke_material, volume_mesh, volume_height_cm):
    kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
    preset, path = particle_stage.load_or_create_preset(spec["name"])
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind))
    preset.set_editor_property("plane_settings", unreal.TDFluidPresetAsset.build_plane_settings_for_preset_kind(kind))
    preset.set_editor_property("volume_render", unreal.TDFluidPresetAsset.build_volume_render_for_preset_kind(kind))
    preset.set_editor_property("volume_material", smoke_material)
    preset.set_editor_property("volume_mesh", volume_mesh)
    preset.set_editor_property("volume_height_cm", volume_height_cm)
    stored_material = preset.get_editor_property("volume_material")
    if preset.get_editor_property("preset_kind") != kind or stored_material is None or stored_material.get_path_name() != smoke_material.get_path_name():
        raise RuntimeError(f"프리셋 종류·볼륨 재질 지정 확인 실패: {path}")
    effective = preset.resolve_effective_config()
    extent = effective.get_editor_property("sim_extent_cm")
    if effective.get_editor_property("grid_resolution") != EXPECTED_GRID_RESOLUTION or abs(extent.x - EXPECTED_EXTENT_CM) > 0.01 or abs(extent.y - EXPECTED_EXTENT_CM) > 0.01:
        raise RuntimeError(f"프리셋 유효 구성이 {EXPECTED_GRID_RESOLUTION}격자·{EXPECTED_EXTENT_CM:.0f} cm 창이 아님(Kinds.{spec['name']} 값은 B1 JSON 이 먼저): {path}")
    particle_stage.save_asset(preset, path)
    return preset, path, effective


def add_instance_component(actor, component_class):
    subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = list(subobjects.k2_gather_subobject_data_for_instance(actor))
    if not handles:
        raise RuntimeError("액터 서브오브젝트 목록이 비었습니다: " + actor.get_actor_label())
    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", handles[0])
    params.set_editor_property("new_class", component_class)
    new_handle, failure_reason = subobjects.add_new_subobject(params)
    if str(failure_reason):
        raise RuntimeError(f"부품 추가 실패({actor.get_actor_label()}): {failure_reason}")
    component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(subobjects.k2_find_subobject_data_from_handle(new_handle))
    if component is None or actor.get_component_by_class(component_class) is None:
        raise RuntimeError("부품 확인 실패: " + actor.get_actor_label())
    return component


def configure_field_component(component, texture, size_cm, layout_name, uses_signed_encoding):
    component.set_editor_property("field_texture", texture)
    component.set_editor_property("field_size_cm", unreal.Vector2D(size_cm[0], size_cm[1]))
    component.set_editor_property("layout", getattr(unreal.TDFluidFieldInputLayout, layout_name))
    component.set_editor_property("uses_signed_velocity_encoding", uses_signed_encoding)
    component.set_editor_property("uv_tile_count", 1.0)
    component.set_editor_property("uv_scroll_per_sec", unreal.Vector2D(0.0, 0.0))
    component.set_editor_property("uv_rotation_degrees", 0.0)
    if component.get_editor_property("field_texture") is None or component.get_editor_property("layout") != getattr(unreal.TDFluidFieldInputLayout, layout_name):
        raise RuntimeError("필드 컴포넌트 텍스처·배치 지정 확인 실패: " + component.get_name())
    return {"layout": layout_name, "size_cm": [size_cm[0], size_cm[1]], "uses_signed_velocity_encoding": uses_signed_encoding,
            "texture": component.get_editor_property("field_texture").get_path_name()}


def spawn_live_actor(context, zone, center, folder):
    particle_stage, actors, created = context["particle_stage"], context["actors"], context["created"]
    preset, preset_path, effective = context["presets"][zone["preset"]]
    actor = actors.spawn_actor_from_class(context["live_actor_class"], unreal.Vector(center[0], center[1], LIVE_ACTOR_HEIGHT_CM), unreal.Rotator())
    particle_stage.finish_actor(actor, zone["label"], folder, created)
    particle_stage.configure_live_actor(actor, preset, effective)
    return actor, preset_path


def spawn_orbiting_body(context, zone, body, center, folder):
    particle_stage, actors, created = context["particle_stage"], context["actors"], context["created"]
    start_x, start_y = body_start_offset(body)
    stirrer = actors.spawn_actor_from_class(context["stirrer_class"], unreal.Vector(center[0] + start_x, center[1] + start_y, body["height_cm"]), unreal.Rotator())
    particle_stage.finish_actor(stirrer, f"{zone['label']}_{body['name']}", folder, created)
    mesh_component = stirrer.get_component_by_class(unreal.StaticMeshComponent)
    rotating = stirrer.get_component_by_class(unreal.RotatingMovementComponent)
    if mesh_component is None or rotating is None:
        raise RuntimeError("교반기에 메시·회전 이동 컴포넌트가 없습니다: " + stirrer.get_actor_label())
    scale = body["half_extent_cm"] * 2.0 / BASIC_SHAPE_SIZE_CM
    mesh_component.set_editor_property("static_mesh", context["meshes"][body["mesh"]])
    mesh_component.set_editor_property("relative_scale3d", unreal.Vector(scale, scale, scale))
    if not body["collides"]:
        mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    pivot_x, pivot_y = body_local_pivot_offset(body)
    rotating.set_editor_property("pivot_translation", unreal.Vector(pivot_x, pivot_y, 0.0))
    rotating.set_editor_property("rotation_rate", unreal.Rotator(roll=0.0, pitch=0.0, yaw=body_degrees_per_sec(body)))
    return stirrer


def describe_body(stirrer, body):
    return {"label": stirrer.get_actor_label(), "name": body["name"], "mesh": body["mesh"], "half_extent_cm": body["half_extent_cm"], "orbit_radius_cm": body["orbit_radius_cm"],
            "pivot_offset_cm": list(body["pivot_offset_cm"]), "start_angle_degrees": body["start_angle_degrees"], "degrees_per_sec": body_degrees_per_sec(body),
            "speed_cm_per_sec": body.get("speed_cm_per_sec"), "collides": body["collides"]}


def build_zone_bodies(context, zone, center, folder):
    actor, preset_path = spawn_live_actor(context, zone, center, folder)
    stirrers = [spawn_orbiting_body(context, zone, body, center, folder) for body in zone["bodies"]]
    return actor, {"preset": preset_path, "bodies": [describe_body(stirrer, body) for stirrer, body in zip(stirrers, zone["bodies"])]}


def spawn_field_marker(context, label, location, size_cm, folder):
    particle_stage, actors, created = context["particle_stage"], context["actors"], context["created"]
    marker = actors.spawn_actor_from_object(context["meshes"]["plane"], unreal.Vector(*location), unreal.Rotator())
    particle_stage.finish_actor(marker, label, folder, created)
    scale = size_cm / BASIC_SHAPE_SIZE_CM
    marker.set_actor_scale3d(unreal.Vector(scale, scale, 1.0))
    marker.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    marker.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    marker.set_actor_hidden_in_game(True)
    return marker


def build_zone_fields(context, zone, center, folder):
    actor, preset_path = spawn_live_actor(context, zone, center, folder)
    fields = []
    for field in FIELD_ACTOR_SPECS:
        texture_spec = next(spec for spec in TEXTURE_SPECS if spec["key"] == field["texture"])
        location = (center[0] + field["offset_cm"][0], center[1] + field["offset_cm"][1], FIELD_MARKER_HEIGHT_CM)
        marker = spawn_field_marker(context, f"{zone['label']}_{field['name']}", location, field["size_cm"], folder)
        component = add_instance_component(marker, context["field_component_class"])
        record = configure_field_component(component, context["input_textures"][field["texture"]], (field["size_cm"], field["size_cm"]), texture_spec["layout"], texture_spec["signed"])
        record.update({"label": marker.get_actor_label(), "name": field["name"], "center": [location[0], location[1]]})
        fields.append(record)
    return actor, {"preset": preset_path, "fields": fields}


def build_zone_mesh(context, zone, center, folder):
    actor, preset_path = spawn_live_actor(context, zone, center, folder)
    bodies = []
    for body in zone["bodies"]:
        stirrer = spawn_orbiting_body(context, zone, body, center, folder)
        baked_texture, bake_record = context["mesh_fields"][body["field"]]
        actor_scale = stirrer.get_actor_scale3d()
        if max(abs(actor_scale.x - 1.0), abs(actor_scale.y - 1.0), abs(actor_scale.z - 1.0)) > 0.0001 or abs(body["half_extent_cm"] * 2.0 - BASIC_SHAPE_SIZE_CM) > 0.001:
            raise RuntimeError(f"MeshDistance 필드를 싣는 몸체는 배율 1 이어야 합니다(반박 S12): {stirrer.get_actor_label()}")
        component = add_instance_component(stirrer, context["field_component_class"])
        field_record = configure_field_component(component, baked_texture, bake_record["field_size_cm"], "MESH_DISTANCE", False)
        record = describe_body(stirrer, body)
        record["field"] = field_record
        bodies.append(record)
    return actor, {"preset": preset_path, "bodies": bodies}


def build_zone_fragment(context, zone, center, folder):
    particle_stage, fire_stage, actors, created = context["particle_stage"], context["fire_stage"], context["actors"], context["created"]
    actor, preset_path = spawn_live_actor(context, zone, center, folder)
    block_location = unreal.Vector(center[0], center[1], BLOCK_HEIGHT_CM)
    breakable = particle_stage.finish_actor(actors.spawn_actor_from_class(context["breakable_class"], block_location, unreal.Rotator()), zone["label"] + "_Block", folder, created)
    geometry_component = breakable.get_component_by_class(unreal.GeometryCollectionComponent)
    if geometry_component is None:
        raise RuntimeError("파괴 액터에 지오메트리 컬렉션 컴포넌트가 없습니다.")
    geometry_component.set_rest_collection(context["brick_collection"])
    breakable.set_editor_property("break_force", BREAK_FORCE)
    breakable.set_editor_property("break_force_radius", BREAK_FORCE_RADIUS)
    emitter = particle_stage.finish_actor(actors.spawn_actor_from_class(context["emitter_class"], block_location, unreal.Rotator()), zone["label"] + "_Emitter", folder, created)
    trail = unreal.TDFluidPresetAsset.build_fragment_trail_for_preset_kind(getattr(unreal.TDFluidPresetKind, FRAGMENT_TRAIL_KIND_NAME))
    trail.set_editor_property("fragment_input_mode", unreal.TDFluidFragmentInputMode.POINTS_AND_FOOTPRINT)
    emitter.set_editor_property("source_breakable_actor", breakable)
    emitter.set_editor_property("smoke_target_actor", actor)
    emitter.set_editor_property("trail_settings", trail)
    target = fire_stage.make_fragment_target({"channel": "CHANNEL_R_MUD", "heat_scale": 0.0, "radius_scale": 1.0, "burst_radius_scale": 1.0, "lagged": False, "height_falloff": 0.0}, actor)
    emitter.set_editor_property("targets", [target])
    stored_mode = emitter.get_editor_property("trail_settings").get_editor_property("fragment_input_mode")
    if stored_mode != unreal.TDFluidFragmentInputMode.POINTS_AND_FOOTPRINT or len(emitter.get_editor_property("targets")) != 1 or emitter.get_editor_property("smoke_target_actor") != actor:
        raise RuntimeError("조각 방출 액터 지정 확인 실패(조각 방식·대상·연기 대상)")
    if not emitter.get_editor_property("targets")[0].get_editor_property("receives_trails"):
        raise RuntimeError("조각 대상이 자국을 받지 않음(receives_trails 거짓): " + actor.get_actor_label())
    hit_location = [block_location.x + BREAK_HIT_OFFSET_CM[0], block_location.y + BREAK_HIT_OFFSET_CM[1], block_location.z + BREAK_HIT_OFFSET_CM[2]]
    return actor, {"preset": preset_path, "breakable": breakable.get_actor_label(), "emitter": emitter.get_actor_label(), "geometry_collection": BRICK_COLLECTION_PATH,
                   "fragment_input_mode": "POINTS_AND_FOOTPRINT", "trail_kind": FRAGMENT_TRAIL_KIND_NAME, "fire_target": None, "targets": 1,
                   "break_trigger": {"how": "PIE 에서 emitter.trigger_break_at(위치)", "hit_location": hit_location}}


def build_zone_touch(context, zone, center, folder):
    actor, preset_path = spawn_live_actor(context, zone, center, folder)
    actor.set_editor_property("pointer_input_mode", unreal.TDFluidPointerInputMode.MULTI_TOUCH)
    if actor.get_editor_property("pointer_input_mode") != unreal.TDFluidPointerInputMode.MULTI_TOUCH:
        raise RuntimeError("포인터 모드 지정 확인 실패: " + actor.get_actor_label())
    return actor, {"preset": preset_path, "pointer_input_mode": "MULTI_TOUCH", "manual": "PIE 에서 터치 여러 점을 동시에 끌어 홈이 점마다 생기는지 본다"}


ZONE_BUILDERS = {"bodies": build_zone_bodies, "fields": build_zone_fields, "mesh": build_zone_mesh, "fragment": build_zone_fragment, "touch": build_zone_touch}


def build_camera(zone, center):
    awake = {"TDGen_" + other["label"]: other["id"] == zone["id"] for other in ZONE_SPECS}
    return {"pawn": [center[0], center[1]], "arm": zone["arm"], "view": list(CAMERA_VIEW), "awake": awake}


def load_stage_assets(fire_stage, smoke_stage):
    assets = unreal.EditorAssetLibrary
    paths = {
        "cube": fire_stage.CUBE_MESH_PATH,
        "plane": fire_stage.PLANE_MESH_PATH,
        "cylinder": fire_stage.CYLINDER_MESH_PATH,
        "floor_material": fire_stage.FLOOR_INSTANCE_PATH,
        "smoke_material": smoke_stage.SMOKE_SPECS[0]["instance_path"],
    }
    loaded = {name: assets.load_asset(path) if assets.does_asset_exist(path) else None for name, path in paths.items()}
    missing = [paths[name] for name, value in loaded.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 없음(불·연기 무대 생성기 먼저): " + ", ".join(missing))
    return loaded


def load_stage_classes(particle_stage, fire_stage):
    assets = unreal.EditorAssetLibrary
    classes = {
        "live_actor": unreal.load_class(None, particle_stage.FLUID_ACTOR_CLASS_PATH),
        "breakable": unreal.load_class(None, fire_stage.CLASS_PATHS["TDBreakableActor"]),
        "emitter": unreal.load_class(None, fire_stage.CLASS_PATHS["TDFluidFragmentEmitterActor"]),
        "field_component": unreal.load_class(None, FIELD_COMPONENT_CLASS_PATH),
        "stirrer": assets.load_blueprint_class(particle_stage.STIRRER_BLUEPRINT_PATH),
    }
    missing = [name for name, value in classes.items() if value is None]
    if missing:
        raise RuntimeError("클래스 없음(재빌드·에디터 재시작, 교반기는 editor_make_fluid_particle_stage.py 먼저): " + ", ".join(missing))
    return classes


def require_saved_asset_files(particle_stage, asset_paths):
    missing = [particle_stage.asset_disk_path(path, ".uasset") for path in asset_paths if not os.path.isfile(particle_stage.asset_disk_path(path, ".uasset"))]
    if missing:
        raise RuntimeError("저장 파일 확인 실패: " + ", ".join(missing))


def write_failure_report(particle_stage, state, error_text):
    report = {"ok": False, "stage": state["stage"], "error": error_text, "overwrite": state.get("overwrite"), "warnings": state.get("warnings", [])}
    return particle_stage.write_report(report, REPORT_RELATIVE_PATH)


def run(state):
    validate_stage_layout()
    particle_stage = load_module("td_fluid_particle_stage", "Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
    fire_stage = load_module("td_fluid_fire_stage", "Tools", "FluidFX", "editor_make_fluid_fire_stage.py")
    smoke_stage = load_module("td_fluid_smoke_stage", "Tools", "FluidFX", "editor_make_fluid_smoke_stage.py")
    state["particle_stage"] = particle_stage
    state["stage"] = "사전 점검"
    editor = particle_stage.require_editor_ready(tuple(particle_stage.REQUIRED_UNREAL_NAMES) + EXTRA_REQUIRED_UNREAL_NAMES)
    overwrite = particle_stage.read_overwrite_request(REQUEST_RELATIVE_PATH)
    state["overwrite"] = overwrite
    source_folder, source_report = read_texture_source_report()
    classes = load_stage_classes(particle_stage, fire_stage)
    contract_problems = find_missing_contract_problems(particle_stage, classes["live_actor"], classes["field_component"])
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))
    stage_assets = load_stage_assets(fire_stage, smoke_stage)
    texture_paths = [f"{TEXTURE_DIR}/{spec['name']}" for spec in TEXTURE_SPECS] + [f"{TEXTURE_DIR}/{spec['name']}" for spec in MESH_FIELD_SPECS]
    preset_paths = [f"{particle_stage.PRESET_DIR}/DA_TDFluid_{spec['name']}" for spec in PRESET_SPECS]
    particle_stage.guard_existing_assets(texture_paths + [BRICK_COLLECTION_PATH] + preset_paths + [STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)
    state["guard_passed"] = True
    state["warnings"] = []

    state["stage"] = "프리셋 정본 검사·다시 읽기"
    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()

    state["stage"] = "텍스처 가져오기"
    print("단계: 입력 텍스처 가져오기")
    input_textures, texture_records = {}, {}
    for spec in TEXTURE_SPECS:
        input_textures[spec["key"]], texture_records[spec["name"]] = import_input_texture(particle_stage, spec, source_folder, source_report)

    state["stage"] = "메시 단면 굽기"
    print("단계: 메시 단면 SDF 굽기")
    mesh_fields, mesh_field_records = {}, {}
    for spec in MESH_FIELD_SPECS:
        mesh_fields[spec["key"]] = bake_mesh_field(particle_stage, spec)
        mesh_field_records[spec["name"]] = mesh_fields[spec["key"]][1]

    state["stage"] = "벽돌 지오메트리 컬렉션"
    print("단계: 벽돌 지오메트리 컬렉션")
    brick_collection = create_brick_collection(particle_stage, stage_assets["cube"])

    state["stage"] = "프리셋"
    print("단계: 프리셋")
    presets = {spec["name"]: configure_input_preset(particle_stage, spec, stage_assets["smoke_material"], stage_assets["cube"], smoke_stage.VOLUME_HEIGHT_CM) for spec in PRESET_SPECS}

    state["stage"] = "레벨"
    print("단계: 레벨")
    levels, actors = particle_stage.open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(fire_stage.GAME_MODE_PATH))
    created = []
    floor_center = ((len(ZONE_SPECS) - 1) * ZONE_SPACING_CM * 0.5, 0.0)
    floor_scale = (len(ZONE_SPECS) * ZONE_SPACING_CM / 100.0 + 10.0, ZONE_SPACING_CM / 100.0 + 10.0, 0.5)
    particle_stage.place_floor_and_lights(actors, created, STAGE_FOLDER, floor_center, floor_scale, stage_assets["floor_material"])
    context = {"particle_stage": particle_stage, "fire_stage": fire_stage, "actors": actors, "created": created, "presets": presets,
               "live_actor_class": classes["live_actor"], "stirrer_class": classes["stirrer"], "breakable_class": classes["breakable"], "emitter_class": classes["emitter"],
               "field_component_class": classes["field_component"], "meshes": {"cube": stage_assets["cube"], "plane": stage_assets["plane"], "cylinder": stage_assets["cylinder"]},
               "input_textures": input_textures, "mesh_fields": mesh_fields, "brick_collection": brick_collection}
    zone_records = []
    for index, zone in enumerate(ZONE_SPECS):
        state["stage"] = "구역 " + zone["id"]
        center = zone_center(index)
        folder = f"{STAGE_FOLDER}/{zone['id']}"
        actor, details = ZONE_BUILDERS[zone["layout"]](context, zone, center, folder)
        record = {"zone": zone["id"], "label": actor.get_actor_label(), "refs": zone["refs"], "motion_note": zone["motion_note"], "center": list(center),
                  "kind": next(spec["kind"] for spec in PRESET_SPECS if spec["name"] == zone["preset"]), "camera": build_camera(zone, center)}
        record.update(details)
        zone_records.append(record)

    state["stage"] = "레벨 저장"
    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    require_saved_asset_files(particle_stage, texture_paths + [BRICK_COLLECTION_PATH] + preset_paths)
    if not os.path.isfile(particle_stage.asset_disk_path(STAGE_MAP_PATH, ".umap")):
        raise RuntimeError("저장 파일 확인 실패: " + STAGE_MAP_PATH)
    report = {"ok": True, "preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "map": STAGE_MAP_PATH, "overwrite": overwrite,
              "zone_spacing_cm": ZONE_SPACING_CM, "window_half_extent_cm": WINDOW_HALF_EXTENT_CM, "texture_source_seed": source_report.get("seed"),
              "textures": texture_records, "mesh_fields": mesh_field_records, "geometry_collection": BRICK_COLLECTION_PATH,
              "presets": {spec["name"]: {"path": presets[spec["name"]][1], "kind": spec["kind"], "default_channel": "CHANNEL_R_MUD"} for spec in PRESET_SPECS},
              "zones": zone_records, "created_actors": len(created), "warnings": state["warnings"], "saved": True}
    report_path = particle_stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def main():
    state = {"stage": "시작", "particle_stage": None, "overwrite": None, "guard_passed": False}
    try:
        run(state)
    except Exception as error:
        error_text = f"{type(error).__name__}: {error}"
        print(REPORT_MARKER, "FAIL", state["stage"], error_text)
        print(traceback.format_exc())
        if state.get("guard_passed"):
            print("실패 보고서: " + write_failure_report(state["particle_stage"], state, error_text))
        raise
    print(REPORT_MARKER, "PASS")


if __name__ == "__main__":
    main()
