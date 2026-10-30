"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 구름 무대 LV_TDFluidCloudStage(지면 Z 0 = 구름 지면, 40 km 지형 = SM_TD_FluidCloudTerrainGrid256 × 40000 + 지형 MI(VSM Rigid) + 먼 바닥 200 km, 하늘 대기·주광·하늘광·높이 안개·수동 노출 PPV, 구름 액터 1(셋업 20), 대리 라이브 9(Z -20000 cm, 태그 TDFluidCloud_<셋업>), 교반기, 칠하기 액터 TDGen_CloudPainter·바람 막 액터 TDGen_CloudWindCurtain, 셋업마다 CineCamera TDGen_CloudCam_<셋업>)와 DA_TDFluid_Cloud<셋업> 9개를 만든다. 먼저 editor_make_fluid_cloud_material.py 와 editor_make_fluid_particle_stage.py(교반기 블루프린트)가 끝나 있어야 한다. 공용 함수는 editor_make_fluid_particle_stage.py 에서 가져온다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_cloud_stage.py (PowerShell, L-repo-01. 갱신은 Saved/FluidFX/fluid_cloud_stage_request.json 에 {"overwrite": true}, 6c 토네이도 HVOL 은 같은 파일에 "with_hvol": true 또는 인수 --with-hvol, 에셋 단계 맨 끝에서만 cross-plan P-05)
출력: /Game/FluidFX/Maps/LV_TDFluidCloudStage, /Game/FluidFX/Presets/DA_TDFluid_Cloud{Pillars,Vortex,Impulse,Aurora,Titan,Paint,Cyclone,StormWall,WrapField}, Saved/FluidFX/fluid_cloud_stage.json(preset_tuning_sha256·material_look_sha256, 셋업 20·카메라·원천·조절 액터, 지형 일치 단언, g14_hvol attached|deferred|skipped)
상태: 미검증(2026-10-26 FX-11r 작성: py_compile·가짜 unreal 가져오기만, 에디터 실행 전). 프리셋은 PresetKind 만 정하고 bOverridePresetDefaults 는 끈다(골든 불변). TerrainFollow > 0 인 슬롯이 있는 MI 는 CloudTerrainFootprintKm·HeightRangeKm 이 지형 MI StageTerrain* 과 같아야 하며 다르면 중단한다(refute M4). Titan 지우개(밀도 −1) 구동은 교반기 채널에 없어 보통 교반기로 두고 보고에 deferred 로 적는다. 무대 조명 값은 JSON 밖 무대 상수다(spec 2.7, 조명 담당 P-9). 2026-10-27 파동 2 에셋 단계: 에디터 실행 status ok(셋업 17·DA 6·액터 38, g14_hvol skipped), stage-report stale 0, 화면 판정 전. 고침: 높이 안개 속성 enable_volumetric_fog, 먼 바닥 축척 200000(200 km). 2026-10-29 FX-11c 무대 단계 작성(py_compile·가짜 unreal 가져오기만, 에디터 실행 전): 셋업 17 CycloneLive·18 StormWall·19 WrapField, 원천 3(DA 9개), Paint 종류 CLOUD_PAINT(원천 줄 Z -20000, 마우스 붓 끔, 카메라 목표 평지 사분면), 원천 역할 Coverage|Section, 모든 원천 DA 기본 채널 R, 조절 액터 2, 클래스·속성 점검.
"""
import importlib.util
import json
import math
import os
import sys

import unreal

FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
MESH_DIR = f"{FLUID_ROOT}/Meshes"
STAGE_MAP_PATH = f"{FLUID_ROOT}/Maps/LV_TDFluidCloudStage"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
CLOUD_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidCloudLayersActor"
PAINTER_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidCloudPainterActor"
WIND_CURTAIN_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidCloudWindCurtainActor"
PAINTER_REQUIRED_PROPERTIES = ("target_live_actor_tags",)
WIND_CURTAIN_REQUIRED_PROPERTIES = ("target_live_actor_tag", "eye_sink_target_live_actor_tag")
TERRAIN_GRID_PATH = f"{MESH_DIR}/SM_TD_FluidCloudTerrainGrid256"
TERRAIN_INSTANCE_PATH = f"{MATERIAL_DIR}/MI_TD_FluidCloudStageTerrain"
PLANE_MESH_PATH = "/Engine/BasicShapes/Plane"
HVOL_MASTER_PATH = f"{MATERIAL_DIR}/M_TD_FluidHeterogeneousVolume"
HVOL_TORNADO_NAME = "MI_TD_FluidHeterogeneousVolume_Tornado"
TORNADO_SECTION_PATH = f"{FLUID_ROOT}/Textures/T_TD_FluidCloudSection_Tornado"
PARTICLE_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_cloud_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_cloud_stage.json")
MATERIAL_LOOK_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidMaterialLook.json")
CENTIMETERS_PER_KILOMETER = 100000.0
TERRAIN_SCALE = 40000.0
FAR_FLOOR_SCALE = 200000.0
FAR_FLOOR_Z_CM = -20.0
SOURCE_DEPTH_CM = -20000.0
SOURCE_SPACING_CM = 3000.0
SOURCE_ROW = (-3000000.0, -3000000.0)
IMPULSE_DEGREES_PER_SEC = 360.0
EXPECTED_SETUP_COUNT = 20
DEFAULT_SLOT_ROLE = "Coverage"
SLOT_ROLE_ENUM_NAMES = {"Coverage": "COVERAGE", "Section": "SECTION"}
PILLAR_OFFSETS_CM = ((-180.0, 0.0), (90.0, 156.0), (90.0, -156.0))
SUN_ROTATION = (-12.0, 200.0)
EXPOSURE_BIAS = 0.0
SLOT_COUNT = 4
FOLDER = "TDGen/FluidFX/CloudStage"

DYNAMIC_SOURCES = (
    {"setup": "Pillars", "kind": "EXPLOSION_GROUND_DUST", "stirrers": 3},
    {"setup": "Vortex", "kind": "VORTEX", "stirrers": 1},
    {"setup": "Impulse", "kind": "SMOKE", "stirrers": 1, "degrees_per_sec": IMPULSE_DEGREES_PER_SEC},
    {"setup": "Aurora", "kind": "TILE_WAVES", "stirrers": 0},
    {"setup": "Titan", "kind": "SMOKE", "stirrers": 1, "eraser": "deferred"},
    {"setup": "Paint", "kind": "CLOUD_PAINT", "stirrers": 0},
    {"setup": "Cyclone", "kind": "CLOUD_CYCLONE", "stirrers": 0},
    {"setup": "StormWall", "kind": "CLOUD_STORM_WALL", "stirrers": 0},
    {"setup": "WrapField", "kind": "CLOUD_WRAP_FIELD", "stirrers": 0},
)
SETUP_SPECS = (
    {"name": "Pillars", "sources": {1: "Pillars"}, "camera": ((-12.0, 0.0, 0.3), (0.0, 0.0, 4.0))},
    {"name": "Vortex", "sources": {1: "Vortex"}, "camera": ((-12.0, 0.0, 0.3), (0.0, 0.0, 3.0))},
    {"name": "Impulse", "sources": {1: "Impulse"}, "camera": ((-30.0, 0.0, 1.0), (0.0, 0.0, 3.5))},
    {"name": "Aurora", "sources": {1: "Aurora"}, "camera": ((-15.0, 0.0, 0.3), (0.0, 0.0, 120.0))},
    {"name": "Titan", "sources": {1: "Titan"}, "camera": ((-12.0, -4.0, 0.4), (0.0, 0.0, 1.0))},
    {"name": "Paint", "sources": {1: "Paint"}, "camera": ((8.6, 8.6, 0.07), (6.0, 6.0, 3.1))},
    {"name": "Volcano", "sources": {}, "camera": ((-16.0, 3.0, 0.2), (0.0, 0.0, 4.0))},
    {"name": "VolcanoSky", "sources": {}, "camera": ((-16.0, 3.0, 0.2), (0.0, 0.0, 4.0))},
    {"name": "Mushroom", "sources": {}, "camera": ((-6.0, -6.0, 0.3), (12.0, 12.0, 4.0))},
    {"name": "MushroomShield", "sources": {}, "camera": ((-6.0, -6.0, 0.3), (12.0, 12.0, 4.0))},
    {"name": "SandWall", "sources": {}, "camera": ((5.0, 0.0, 0.1), (20.0, 0.0, 1.0))},
    {"name": "Nimbus", "sources": {}, "camera": ((-10.0, 0.0, 0.2), (0.0, 0.0, 2.0))},
    {"name": "Stratus", "sources": {}, "camera": ((-2.0, 0.0, 0.95), (2.0, 0.0, 1.0))},
    {"name": "Mammatus", "sources": {}, "camera": ((-4.0, 0.0, 0.3), (0.0, 0.0, 1.8))},
    {"name": "Tornado", "sources": {1: "Vortex"}, "camera": ((-12.0, 0.0, 0.3), (0.0, 0.0, 3.0))},
    {"name": "Cyclone", "sources": {}, "camera": ((-15.0, -15.0, 6.0), (0.0, 0.0, 1.0))},
    {"name": "SkyOnly", "sources": {}, "camera": ((0.0, 0.0, 0.2), (5.0, 0.0, 2.5))},
    {"name": "CycloneLive", "material": "MI_TD_FluidCloud_CycloneLive", "sources": {1: "Cyclone/Coverage"}, "camera": ((-15.0, -15.0, 6.0), (0.0, 0.0, 1.0))},
    {"name": "StormWall", "sources": {1: "StormWall/Section"}, "camera": ((12.0, 5.5, 0.15), (12.0, 12.0, 1.5))},
    {"name": "WrapField", "sources": {1: "WrapField/Coverage"}, "camera": ((40.0, 0.0, 12.0), (40.0, 0.01, 0.0))},
)
REQUIRED_UNREAL_NAMES = (
    "TDFluidCloudLayersActor", "TDFluidCloudPainterActor", "TDFluidCloudWindCurtainActor", "TDFluidCloudSetup", "TDFluidCloudSlotSource",
    "TDFluidCloudSlotTextureRole.COVERAGE", "TDFluidCloudSlotTextureRole.SECTION", "TDFluidBrushChannel.CHANNEL_R_MUD",
    "TDFluidCloudPainterProjection.LAYER_MID_HEIGHT", "TDFluidCloudPainterProjection.TERRAIN",
    "TDFluidPresetKind.CLOUD_PAINT", "TDFluidPresetKind.CLOUD_CYCLONE", "TDFluidPresetKind.CLOUD_STORM_WALL", "TDFluidPresetKind.CLOUD_WRAP_FIELD",
    "TDFluidPresetAsset", "TDFluidPresetKind", "SkyAtmosphere", "DirectionalLight", "SkyLight", "ExponentialHeightFog",
    "PostProcessVolume", "CineCameraActor", "StaticMeshActor", "ShadowCacheInvalidationBehavior.RIGID",
    "AutoExposureMethod.AEM_MANUAL", "RotatingMovementComponent", "MaterialInstanceConstant",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def load_particle_stage():
    module_path = project_path(*PARTICLE_STAGE_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("editor_make_fluid_particle_stage", module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"공용 무대 모듈 없음: {module_path}")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def read_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    request = {}
    if os.path.isfile(request_path):
        with open(request_path, encoding="utf-8") as request_file:
            loaded = json.load(request_file)
        request = loaded if isinstance(loaded, dict) else {}
    with_hvol = request.get("with_hvol") is True or "--with-hvol" in sys.argv
    return request.get("overwrite") is True, with_hvol


def kilometers_to_vector(point_km):
    return unreal.Vector(point_km[0] * CENTIMETERS_PER_KILOMETER, point_km[1] * CENTIMETERS_PER_KILOMETER, point_km[2] * CENTIMETERS_PER_KILOMETER)


def look_rotation(eye_km, target_km):
    delta = [target_km[index] - eye_km[index] for index in range(3)]
    horizontal = math.hypot(delta[0], delta[1])
    return unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(delta[2], max(horizontal, 1e-6))), yaw=math.degrees(math.atan2(delta[1], delta[0])))


def instance_path(setup_name):
    return f"{MATERIAL_DIR}/MI_TD_FluidCloud_{setup_name}"


def resolve_material_path(spec):
    if "material" in spec:
        return f"{MATERIAL_DIR}/{spec['material']}"
    return instance_path(spec["name"])


def parse_slot_source(source_text):
    source_name, _, role_name = source_text.partition("/")
    return source_name, role_name or DEFAULT_SLOT_ROLE


def source_tag(setup_name):
    return f"TDFluidCloud_{setup_name}"


def validate_stage_layout():
    problems = []
    names = [spec["name"] for spec in SETUP_SPECS]
    if len(names) != EXPECTED_SETUP_COUNT or len(set(names)) != EXPECTED_SETUP_COUNT:
        problems.append(f"셋업 {EXPECTED_SETUP_COUNT}개·이름 중복 없음이어야 함: {len(names)}")
    expected_order = ["Pillars", "Vortex", "Impulse", "Aurora", "Titan", "Paint"]
    if names[:6] != expected_order:
        problems.append("동적 셋업 순서(0~5)가 명세와 다름")
    dynamic = {source["setup"] for source in DYNAMIC_SOURCES}
    for spec in SETUP_SPECS:
        for slot, source in spec["sources"].items():
            if not 0 <= slot < SLOT_COUNT:
                problems.append(f"{spec['name']}: 슬롯 {slot} 범위 밖")
            source_name, role_name = parse_slot_source(source)
            if source_name not in dynamic:
                problems.append(f"{spec['name']}: 원천 {source_name} 가 대리 라이브에 없음")
            if role_name not in SLOT_ROLE_ENUM_NAMES:
                problems.append(f"{spec['name']}: 원천 역할 {role_name} 알 수 없음({'|'.join(SLOT_ROLE_ENUM_NAMES)})")
    if TERRAIN_SCALE * 100.0 / CENTIMETERS_PER_KILOMETER != 40.0:
        problems.append("지형 격자 축척이 40 km 가 아님")
    return problems


def find_terrain_contract_problems(materials):
    problems = []
    terrain = materials.get(TERRAIN_INSTANCE_PATH)
    if terrain is None:
        return [f"모양 정본에 지형 MI 없음: {TERRAIN_INSTANCE_PATH}"]
    terrain_footprint = terrain.get("Vectors", {}).get("StageTerrainFootprintKm")
    terrain_range = terrain.get("Vectors", {}).get("StageTerrainHeightRangeKm")
    for spec in SETUP_SPECS:
        entry = materials.get(resolve_material_path(spec))
        if entry is None:
            problems.append(f"모양 정본에 없음: {resolve_material_path(spec)}")
            continue
        scalars = entry.get("Scalars", {})
        follows = [slot for slot in range(SLOT_COUNT) if scalars.get(f"CloudSlot{slot}TerrainFollow", 0.0) > 0.0 and scalars.get(f"CloudSlot{slot}Source", 0.0) > 0.5]
        if not follows:
            continue
        vectors = entry.get("Vectors", {})
        if vectors.get("CloudTerrainFootprintKm") != terrain_footprint or vectors.get("CloudTerrainHeightRangeKm") != terrain_range:
            problems.append(f"{spec['name']}: 지형 추종 슬롯 {follows} 의 발자국·높이 범위가 지형 MI 와 다름")
    return problems


def configure_source_preset(stage, spec):
    kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
    preset, path = stage.load_or_create_preset("Cloud" + spec["setup"])
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind))
    stage.save_asset(preset, path)
    return preset, path, preset.resolve_effective_config()


def place_source(stage, actors, fluid_actor_class, stirrer_class, index, spec, preset_record, created):
    preset, preset_path, effective = preset_record
    location = (SOURCE_ROW[0] + index * SOURCE_SPACING_CM, SOURCE_ROW[1], SOURCE_DEPTH_CM)
    actor = actors.spawn_actor_from_class(fluid_actor_class, unreal.Vector(*location), unreal.Rotator())
    stage.finish_actor(actor, f"Cloud{spec['setup']}_Source", f"{FOLDER}/Sources", created)
    stage.configure_live_actor(actor, preset, effective)
    actor.set_editor_property("accept_mouse_brush", False)
    actor.set_editor_property("tags", [unreal.Name(source_tag(spec["setup"]))])
    stirrers = []
    offsets = PILLAR_OFFSETS_CM[:spec["stirrers"]] if spec["stirrers"] > 1 else ((0.0, 0.0),) * spec["stirrers"]
    for stirrer_index, offset in enumerate(offsets):
        center = (location[0] + offset[0], location[1] + offset[1])
        stirrer = stage.place_stirrer(actors, stirrer_class, center, f"Cloud{spec['setup']}_Stirrer{stirrer_index}", f"{FOLDER}/Sources", created)
        stirrer.set_actor_location(unreal.Vector(stirrer.get_actor_location().x, stirrer.get_actor_location().y, location[2] + stage.STIRRER_HEIGHT_CM), False, False)
        if "degrees_per_sec" in spec:
            rotating = stirrer.get_component_by_class(unreal.RotatingMovementComponent)
            rotating.set_editor_property("rotation_rate", unreal.Rotator(roll=0.0, pitch=0.0, yaw=spec["degrees_per_sec"]))
        stirrers.append(stirrer.get_actor_label())
    return {"setup": spec["setup"], "kind": spec["kind"], "preset": preset_path, "override_preset_defaults": False, "actor": actor.get_actor_label(),
            "tag": source_tag(spec["setup"]), "location_cm": list(location), "stirrers": stirrers, "mouse_brush": False,
            "default_channel": "CHANNEL_R_MUD", "eraser": spec.get("eraser", "none")}


def place_terrain(actors, created, stage):
    grid = unreal.EditorAssetLibrary.load_asset(TERRAIN_GRID_PATH)
    terrain_material = unreal.EditorAssetLibrary.load_asset(TERRAIN_INSTANCE_PATH)
    if grid is None or terrain_material is None:
        raise RuntimeError(f"구름 지형 격자·MI 없음(editor_make_fluid_cloud_material.py 먼저): {TERRAIN_GRID_PATH}, {TERRAIN_INSTANCE_PATH}")
    terrain = actors.spawn_actor_from_object(grid, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
    stage.finish_actor(terrain, "CloudTerrain", f"{FOLDER}/Stage", created)
    terrain.set_actor_scale3d(unreal.Vector(TERRAIN_SCALE, TERRAIN_SCALE, 1.0))
    component = terrain.static_mesh_component
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    component.set_material(0, terrain_material)
    component.set_editor_property("shadow_cache_invalidation_behavior", unreal.ShadowCacheInvalidationBehavior.RIGID)
    plane = unreal.EditorAssetLibrary.load_asset(PLANE_MESH_PATH)
    floor = actors.spawn_actor_from_object(plane, unreal.Vector(0.0, 0.0, FAR_FLOOR_Z_CM), unreal.Rotator())
    stage.finish_actor(floor, "CloudFarFloor", f"{FOLDER}/Stage", created)
    floor.set_actor_scale3d(unreal.Vector(FAR_FLOOR_SCALE, FAR_FLOOR_SCALE, 1.0))
    floor.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    floor.static_mesh_component.set_material(0, terrain_material)
    return {"terrain": terrain.get_actor_label(), "grid": TERRAIN_GRID_PATH, "scale": TERRAIN_SCALE, "material": TERRAIN_INSTANCE_PATH, "vsm": "Rigid", "far_floor_km": FAR_FLOOR_SCALE * 100.0 / CENTIMETERS_PER_KILOMETER}


def place_lighting(actors, created, stage):
    folder = f"{FOLDER}/Lighting"
    stage.finish_actor(actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator()), "CloudSkyAtmosphere", folder, created)
    sun = stage.finish_actor(actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 50000.0), unreal.Rotator(pitch=SUN_ROTATION[0], yaw=SUN_ROTATION[1])), "CloudSun", folder, created)
    light = sun.light_component
    light.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    for name in ("atmosphere_sun_light", "cast_shadows_on_clouds", "cast_cloud_shadows"):
        light.set_editor_property(name, True)
    sky = stage.finish_actor(actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 1000.0), unreal.Rotator()), "CloudSkyLight", folder, created)
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    fog = stage.finish_actor(actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator()), "CloudHeightFog", folder, created)
    fog.component.set_editor_property("enable_volumetric_fog", False)
    volume = stage.finish_actor(actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator()), "CloudExposure", folder, created)
    volume.set_editor_property("unbound", True)
    settings = volume.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
    volume.set_editor_property("settings", settings)
    return {"sun_rotation": list(SUN_ROTATION), "sun_flags": ["atmosphere_sun_light", "cast_shadows_on_clouds", "cast_cloud_shadows"], "sky_light": "real_time_capture",
            "height_fog_volumetric": False, "exposure": {"method": "manual", "bias": EXPOSURE_BIAS}}


def make_cloud_setups():
    setups = []
    for spec in SETUP_SPECS:
        setup = unreal.TDFluidCloudSetup()
        setup.set_editor_property("setup_name", unreal.Name(spec["name"]))
        setup.set_editor_property("cloud_material", unreal.EditorAssetLibrary.load_asset(resolve_material_path(spec)))
        sources = []
        for slot, source in sorted(spec["sources"].items()):
            source_name, role_name = parse_slot_source(source)
            slot_source = unreal.TDFluidCloudSlotSource()
            slot_source.set_editor_property("slot_index", slot)
            slot_source.set_editor_property("role", getattr(unreal.TDFluidCloudSlotTextureRole, SLOT_ROLE_ENUM_NAMES[role_name]))
            slot_source.set_editor_property("live_actor_tag", unreal.Name(source_tag(source_name)))
            sources.append(slot_source)
        setup.set_editor_property("slot_sources", sources)
        setups.append(setup)
    return setups


def place_cloud_actor(actors, created, stage):
    missing = [resolve_material_path(spec) for spec in SETUP_SPECS if not unreal.EditorAssetLibrary.does_asset_exist(resolve_material_path(spec))]
    if missing:
        raise RuntimeError("구름 MI 없음(editor_make_fluid_cloud_material.py 먼저): " + ", ".join(missing))
    cloud_class = unreal.load_class(None, CLOUD_ACTOR_CLASS_PATH)
    if cloud_class is None:
        raise RuntimeError("구름 액터 클래스 없음(재빌드와 에디터 재시작 필요): " + CLOUD_ACTOR_CLASS_PATH)
    cloud = actors.spawn_actor_from_class(cloud_class, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
    stage.finish_actor(cloud, "CloudLayers", f"{FOLDER}/Cloud", created)
    cloud.set_editor_property("setups", make_cloud_setups())
    cloud.set_editor_property("default_setup_index", 0)
    cloud.set_editor_property("takes_over_scene_cloud", False)
    stored = cloud.get_editor_property("setups")
    if len(stored) != len(SETUP_SPECS):
        raise RuntimeError(f"구름 셋업 설정 확인 실패: {len(stored)}")
    return cloud.get_actor_label()


def load_control_actor_class(stage, class_path, property_names):
    actor_class = unreal.load_class(None, class_path)
    if actor_class is None:
        raise RuntimeError("구름 조절 액터 클래스 없음(재빌드와 에디터 재시작 필요): " + class_path)
    missing = stage.find_missing_properties(unreal.get_default_object(actor_class), property_names)
    if missing:
        raise RuntimeError(f"구름 조절 액터 속성 없음(재빌드와 에디터 재시작 필요): {class_path} {', '.join(missing)}")
    return actor_class


def place_control_actors(actors, created, stage, painter_class, wind_curtain_class):
    folder = f"{FOLDER}/Cloud"
    painter_tags = [source_tag("Paint")]
    painter = actors.spawn_actor_from_class(painter_class, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
    stage.finish_actor(painter, "CloudPainter", folder, created)
    painter.set_editor_property("target_live_actor_tags", [unreal.Name(tag) for tag in painter_tags])
    stored_painter_tags = [str(tag) for tag in painter.get_editor_property("target_live_actor_tags")]
    if stored_painter_tags != painter_tags:
        raise RuntimeError(f"칠하기 액터 대상 태그 확인 실패: {stored_painter_tags}")
    wind_tag = source_tag("StormWall")
    eye_sink_tag = source_tag("Cyclone")
    wind_curtain = actors.spawn_actor_from_class(wind_curtain_class, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
    stage.finish_actor(wind_curtain, "CloudWindCurtain", folder, created)
    wind_curtain.set_editor_property("target_live_actor_tag", unreal.Name(wind_tag))
    wind_curtain.set_editor_property("eye_sink_target_live_actor_tag", unreal.Name(eye_sink_tag))
    stored_wind_tag = str(wind_curtain.get_editor_property("target_live_actor_tag"))
    stored_eye_sink_tag = str(wind_curtain.get_editor_property("eye_sink_target_live_actor_tag"))
    if (stored_wind_tag, stored_eye_sink_tag) != (wind_tag, eye_sink_tag):
        raise RuntimeError(f"바람 막 액터 대상 태그 확인 실패: {stored_wind_tag}, {stored_eye_sink_tag}")
    return {"painter": painter.get_actor_label(), "painter_tags": painter_tags, "wind_curtain": wind_curtain.get_actor_label(),
            "wind_curtain_tag": wind_tag, "eye_sink_tag": eye_sink_tag}


def place_cameras(actors, created, stage):
    cameras = {}
    for index, spec in enumerate(SETUP_SPECS):
        eye_km, target_km = spec["camera"]
        camera = actors.spawn_actor_from_class(unreal.CineCameraActor, kilometers_to_vector(eye_km), look_rotation(eye_km, target_km))
        stage.finish_actor(camera, f"CloudCam_{spec['name']}", f"{FOLDER}/Cameras", created)
        cameras[spec["name"]] = {"label": camera.get_actor_label(), "setup_index": index, "eye_km": list(eye_km), "target_km": list(target_km)}
    return cameras


def attach_tornado_hvol(stage, overwrite, look_materials):
    reasons = []
    if not unreal.EditorAssetLibrary.does_asset_exist(HVOL_MASTER_PATH):
        reasons.append(f"FX-9 마스터 없음: {HVOL_MASTER_PATH}")
    if getattr(unreal.TDFluidPresetKind, "VORTEX_HETEROGENEOUS", None) is None:
        reasons.append("종류 VORTEX_HETEROGENEOUS 없음")
    hvol_path = f"{MATERIAL_DIR}/{HVOL_TORNADO_NAME}"
    if hvol_path not in look_materials:
        reasons.append(f"모양 정본에 {HVOL_TORNADO_NAME} 없음(look-fragment-fx11r-hvol.json 재병합)")
    if reasons:
        return {"status": "deferred", "reasons": reasons}
    master = unreal.EditorAssetLibrary.load_asset(HVOL_MASTER_PATH)
    section_names = [str(name) for name in unreal.MaterialEditingLibrary.get_texture_parameter_names(master) if "CrossSection" in str(name)]
    if len(section_names) != 1:
        return {"status": "deferred", "reasons": [f"FX-9 단면 텍스처 매개변수를 하나로 정할 수 없음: {section_names}"]}
    if unreal.EditorAssetLibrary.does_asset_exist(hvol_path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {hvol_path}")
        instance = unreal.EditorAssetLibrary.load_asset(hvol_path)
    else:
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(HVOL_TORNADO_NAME, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    look_table = stage.load_module("fluid_material_look", "Tools", "FluidFX", "fluid_material_look.py").TDMaterialLookTable.read_project(unreal.SystemLibrary.get_project_directory())
    look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, hvol_path, master)
    section = unreal.EditorAssetLibrary.load_asset(TORNADO_SECTION_PATH)
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(instance, section_names[0], section)
    stored = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(instance, section_names[0])
    if stored is None or stored.get_path_name() != section.get_path_name():
        raise RuntimeError(f"{HVOL_TORNADO_NAME}: 단면 텍스처 연결 확인 실패")
    stage.save_asset(instance, hvol_path)
    preset, preset_path = stage.load_or_create_preset("CloudVortex")
    kind = unreal.TDFluidPresetKind.VORTEX_HETEROGENEOUS
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind))
    if not stage.has_editor_property(preset, "volume_material"):
        return {"status": "deferred", "reasons": ["프리셋에 volume_material 속성 없음"], "instance": hvol_path}
    preset.set_editor_property("volume_material", instance)
    stage.save_asset(preset, preset_path)
    return {"status": "attached", "instance": hvol_path, "section_parameter": section_names[0], "preset": preset_path, "kind": "VORTEX_HETEROGENEOUS"}


def main():
    stage = load_particle_stage()
    editor = stage.require_editor_ready(stage.REQUIRED_UNREAL_NAMES + REQUIRED_UNREAL_NAMES)
    layout_problems = validate_stage_layout()
    if layout_problems:
        raise RuntimeError("무대 배치 점검 실패: " + " | ".join(layout_problems))
    overwrite, with_hvol = read_request()
    preset_paths = [f"{PRESET_DIR}/DA_TDFluid_Cloud{spec['setup']}" for spec in DYNAMIC_SOURCES]
    stage.guard_existing_assets(preset_paths + [STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)
    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = stage.check_and_reload_preset_data()
    with open(project_path(*MATERIAL_LOOK_RELATIVE_PATH), encoding="utf-8") as look_file:
        look_materials = json.load(look_file).get("Materials", {})
    terrain_problems = find_terrain_contract_problems(look_materials)
    if terrain_problems:
        raise RuntimeError("지형 추종 계약 실패(refute M4): " + " | ".join(terrain_problems))
    fluid_actor_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    if fluid_actor_class is None:
        raise RuntimeError("유체 액터 클래스 없음: " + FLUID_ACTOR_CLASS_PATH)
    painter_class = load_control_actor_class(stage, PAINTER_ACTOR_CLASS_PATH, PAINTER_REQUIRED_PROPERTIES)
    wind_curtain_class = load_control_actor_class(stage, WIND_CURTAIN_ACTOR_CLASS_PATH, WIND_CURTAIN_REQUIRED_PROPERTIES)

    print("단계: 교반기·프리셋")
    stirrer_class, _ = stage.load_or_create_stirrer_blueprint()
    presets = {spec["setup"]: configure_source_preset(stage, spec) for spec in DYNAMIC_SOURCES}

    print("단계: 레벨")
    levels, actors = stage.open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(stage.GAME_MODE_PATH))
    created = []
    terrain_record = place_terrain(actors, created, stage)
    lighting_record = place_lighting(actors, created, stage)
    source_records = [place_source(stage, actors, fluid_actor_class, stirrer_class, index, spec, presets[spec["setup"]], created) for index, spec in enumerate(DYNAMIC_SOURCES)]
    cloud_label = place_cloud_actor(actors, created, stage)
    control_record = place_control_actors(actors, created, stage, painter_class, wind_curtain_class)
    cameras = place_cameras(actors, created, stage)
    player_start = actors.spawn_actor_from_class(unreal.PlayerStart, kilometers_to_vector((-12.0, 0.0, 0.3)), unreal.Rotator())
    stage.finish_actor(player_start, "CloudPlayerStart", f"{FOLDER}/Gameplay", created)

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    stage.require_saved_files(preset_paths, STAGE_MAP_PATH)
    hvol_record = attach_tornado_hvol(stage, overwrite, look_materials) if with_hvol else {"status": "skipped", "reasons": ["--with-hvol 없음(에셋 단계 맨 끝 A5-h 에서만)"]}

    report = {
        "status": "ok", "preset_tuning_sha256": tuning_sha256, "material_look_sha256": look_sha256, "map": STAGE_MAP_PATH, "overwrite": overwrite,
        "cloud_actor": cloud_label, "setup_count": len(SETUP_SPECS), "control_actors": control_record,
        "setups": [{"index": index, "name": spec["name"], "material": resolve_material_path(spec),
                    "sources": {str(slot): "/".join((source_tag(parse_slot_source(source)[0]), parse_slot_source(source)[1])) for slot, source in spec["sources"].items()},
                    "camera": cameras[spec["name"]]}
                   for index, spec in enumerate(SETUP_SPECS)],
        "sources": source_records, "terrain": terrain_record, "lighting": lighting_record, "terrain_contract": "TerrainFollow MI == MI_TD_FluidCloudStageTerrain",
        "g14_hvol": hvol_record["status"], "g14_hvol_detail": hvol_record, "created_actors": len(created), "saved": True,
    }
    report_path = stage.write_report(report, REPORT_RELATIVE_PATH)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


if __name__ == "__main__":
    main()
