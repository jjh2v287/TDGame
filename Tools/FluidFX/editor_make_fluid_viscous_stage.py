"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 점성·용암 무대 LV_TDFluidViscousStage(FX-14 구역 9: 1 ViscousBloodSlide 3 m 궤도 교반기, 2 ViscousBloodSplash 조각 방출기 피 튐, 3 ViscousMudWorld 고정 창+굴림 공, 4 ViscousMudLocal 창이 움직이는 공을 따름, 5 ViscousBloodFlowDecal 15도 경사+데칼 출력 액터, 6 ViscousBloodFlowMesh 평지 WPO 표면+조각 방출기, 7 ViscousBloodBath 고리 장애물+교반기 3, 8 LavaWorld 별도 Y 줄 100 m 평면, 9 LavaFlow 평지 교반기 흐름+증기)와 DA 6개를 만든다. 실행 전 python Tools/ue_editor.py restart(새 UCLASS TDFluidSurfaceOutputActor, L-build-01), 모든 차선의 조각 병합(fluid_material_look.py merge) 뒤 재질 도구 editor_make_fluid_viscous_material.py, 불 무대(editor_make_fluid_fire_stage.py)와 연기 셋업 무대(editor_make_fluid_smoke_setup_stage.py)가 끝나 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_viscous_stage.py (PowerShell, L-repo-01). 시작 점검(TDFluidSurfaceOutputActor·TDFluidPresetKind.VISCOUS_MUD·LAVA_TILE·TDFluidAreaAddressMode.WRAP 등)이 하나라도 없으면 에셋을 바꾸기 전에 멈춘다. run_in_editor.py 가 SUCCESS 여도 본문 status 가 failed 면 실패로 판정한다(L-editor-73).
출력: /Game/FluidFX/Presets/DA_TDFluid_{ViscousMud,ViscousBlood,ViscousBloodDecal,LavaTile,LavaLocal,LavaFlow}(모두 override_preset_defaults 끔, 채널 ViscousBlood·ViscousBloodDecal 은 CHANNEL_G_BLOOD 나머지는 CHANNEL_R_MUD), /Game/FluidFX/Maps/LV_TDFluidViscousStage, Saved/FluidFX/fluid_viscous_stage.json(status·preset_tuning_sha256·material_look_sha256·구역별 라벨·카메라 제안·결정 기록). 기존 에셋은 Saved/FluidFX/fluid_viscous_stage_request.json 의 {"overwrite": true} 가 있을 때만 갱신하고 미저장 작업이 있으면 중단한다.
상태: 미검증(2026-10-29 FX-14 D-2 작성: py_compile 만, 에디터 실행 전). 경사 데칼 깊이는 C++ 출력 액터가 지형 범위로 잡는다고 가정하고(M4) 무대는 DecalBoxHeightCm 를 건드리지 않는다. 증기는 기존 DA_TDFluidSetupSteamPuff 를 쓴다(명세 표기 불 액터와 달리 선례 smoke setup stage 는 볼륨 연기 액터). 추종 방식·조각 방출기 파괴 계기는 보고에 실제 방식을 적는다[추정]. 2026-10-31: 교반기 회전 속도가 피벗 쓰기 뒤 BP 구성 스크립트 재실행으로 유실되던(기본 90°/s) 것을 particle_stage.set_component_properties(한 번에 쓰기·새 핸들로 다시 읽기, 다르면 FAIL)로 고침, 맵 재생성 필요.
"""
import importlib.util
import json
import math
import os

import unreal

STAGE_MAP_PATH = "/Game/FluidFX/Maps/LV_TDFluidViscousStage"
MATERIAL_DIR = "/Game/FluidFX/Materials"
GRID_MESH_PATH = "/Game/FluidFX/Meshes/SM_TD_FluidTrackGridUnit256"
STEAM_PRESET_PATH = "/Game/FluidFX/Presets/DA_TDFluid_SetupSteamPuff"
OUTPUT_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidSurfaceOutputActor"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_viscous_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_viscous_stage.json")
STAGE_FOLDER = "TDGen/FluidFX/ViscousStage"
FLOOR_MARGIN_CM = 1500.0
GROUND_LIFT_CM = 1.0
BASIC_SHAPE_HALF_SIZE_CM = 50.0

SLIDE_ORBIT_RADIUS_CM = 300.0
BALL_SCALE = 3.0
BALL_HEIGHT_CM = 60.0
BALL_ORBIT_RADIUS_CM = 300.0
BALL_DEGREES_PER_SEC = 45.0
FOLLOW_ORBIT_RADIUS_CM = 600.0
FOLLOW_DEGREES_PER_SEC = 45.0
SLOPE_PITCH_DEGREES = -15.0
SLOPE_SCALE = (16.0, 16.0, 0.2)
SLOPE_CENTER_HEIGHT_CM = 450.0
SLOPE_TRACE_TOLERANCE_CM = 5.0
SLOPE_TRACE_RANGE_CM = 1500.0
SLOPE_STIRRER_OFFSET_CM = -350.0
SLOPE_STIRRER_ORBIT_CM = 100.0
SLOPE_STIRRER_LIFT_CM = 25.0
BREAKABLE_HEIGHT_CM = 50.0
MESH_STIRRER_ORBIT_CM = 250.0
MESH_BREAKABLE_OFFSET_CM = -400.0
BATH_RING_COUNT = 16
BATH_RING_RADIUS_CM = 450.0
BATH_RING_SCALE = (1.9, 0.4, 0.6)
BATH_RING_HEIGHT_CM = 30.0
BATH_STIRRER_COUNT = 3
BATH_STIRRER_RING_RADIUS_CM = 200.0
BATH_STIRRER_ORBIT_CM = 110.0
LAVA_PLANE_SCALE = 100.0
LAVA_PLANE_LIFT_CM = 3.0
LAVA_FLOW_STIRRER_OFFSET_CM = -300.0
LAVA_FLOW_STIRRER_ORBIT_CM = 200.0
LAVA_FLOW_DEGREES_PER_SEC = 30.0
STEAM_OFFSET_CM = 500.0
STEAM_STIRRER_ORBIT_CM = 120.0

MATERIAL_NAMES = {
    "mud": "MI_TD_FluidViscous_Mud",
    "blood": "MI_TD_FluidViscous_Blood",
    "decal": "MI_TD_FluidViscousDecal_Blood",
    "lava": "MI_TD_FluidLava",
}
PRESET_SPECS = (
    {"name": "ViscousMud", "kind": "VISCOUS_MUD", "channel": "CHANNEL_R_MUD", "material": "mud", "has_mesh": True},
    {"name": "ViscousBlood", "kind": "VISCOUS_BLOOD", "channel": "CHANNEL_G_BLOOD", "material": "blood", "has_mesh": True},
    {"name": "ViscousBloodDecal", "kind": "VISCOUS_BLOOD", "channel": "CHANNEL_G_BLOOD", "material": None, "has_mesh": False},
    {"name": "LavaTile", "kind": "LAVA_TILE", "channel": "CHANNEL_R_MUD", "material": None, "has_mesh": False},
    {"name": "LavaLocal", "kind": "LAVA_LOCAL", "channel": "CHANNEL_R_MUD", "material": None, "has_mesh": False},
    {"name": "LavaFlow", "kind": "LAVA_LOCAL", "channel": "CHANNEL_R_MUD", "material": "lava", "has_mesh": True},
)
ZONE_SPECS = (
    {"zone": 1, "id": "ViscousBloodSlide", "layout": "slide", "center": (0.0, 0.0), "half_cm": 1000.0, "refs": ["IMG 4", "TS A7"], "camera": {"arm": 700.0, "view": (-35.0, 90.0)}},
    {"zone": 2, "id": "ViscousBloodSplash", "layout": "splash", "center": (2000.0, 0.0), "half_cm": 1000.0, "refs": ["TS B5"], "camera": {"arm": 700.0, "view": (-35.0, 90.0)}},
    {"zone": 3, "id": "ViscousMudWorld", "layout": "mud_world", "center": (4000.0, 0.0), "half_cm": 1000.0, "refs": ["IMG 4"], "camera": {"arm": 700.0, "view": (-35.0, 90.0)}},
    {"zone": 4, "id": "ViscousMudLocal", "layout": "mud_local", "center": (6000.0, 0.0), "half_cm": 1000.0, "refs": ["IMG 4"], "camera": {"arm": 700.0, "view": (-35.0, 90.0)}},
    {"zone": 5, "id": "ViscousBloodFlowDecal", "layout": "flow_decal", "center": (8000.0, 0.0), "half_cm": 1000.0, "refs": ["TS J6"], "camera": {"arm": 900.0, "view": (-25.0, 90.0)}},
    {"zone": 6, "id": "ViscousBloodFlowMesh", "layout": "flow_mesh", "center": (10000.0, 0.0), "half_cm": 1000.0, "refs": ["TS M4", "TS B5"], "camera": {"arm": 700.0, "view": (-35.0, 90.0)}},
    {"zone": 7, "id": "ViscousBloodBath", "layout": "bath", "center": (12000.0, 0.0), "half_cm": 1000.0, "refs": ["TS A6"], "camera": {"arm": 900.0, "view": (-45.0, 90.0)}},
    {"zone": 9, "id": "LavaFlow", "layout": "lava_flow", "center": (14000.0, 0.0), "half_cm": 1000.0, "refs": ["TS M3"], "camera": {"arm": 900.0, "view": (-30.0, 90.0)}},
    {"zone": 8, "id": "LavaWorld", "layout": "lava_world", "center": (0.0, 8000.0), "half_cm": 5000.0, "refs": ["TS A12", "TS M3"], "camera": {"arm": 1200.0, "view": (-25.0, 90.0)}},
)
FRAGMENT_TARGET_SPEC = {"channel": "CHANNEL_G_BLOOD", "heat_scale": 0.0, "radius_scale": 1.0, "burst_radius_scale": 1.0, "lagged": False, "height_falloff": 30.0}
FRAGMENT_TRAIL_KIND_NAME = "VISCOUS_BLOOD"
EXTRA_REQUIRED_UNREAL_NAMES = (
    "TDFluidSurfaceOutputActor",
    "TDFluidFragmentEmitterActor",
    "TDFluidFragmentTarget",
    "TDBreakableActor",
    "GeometryCollectionComponent",
    "TDFluidPresetKind.VISCOUS_MUD",
    "TDFluidPresetKind.VISCOUS_BLOOD",
    "TDFluidPresetKind.LAVA_TILE",
    "TDFluidPresetKind.LAVA_LOCAL",
    "TDFluidAreaAddressMode.WRAP",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "TDFluidBrushChannel.CHANNEL_G_BLOOD",
    "TDFluidPresetAsset.build_config_for_preset_kind",
    "TDFluidPresetAsset.build_plane_settings_for_preset_kind",
    "TDFluidPresetAsset.build_fragment_trail_for_preset_kind",
    "TDFluidPresetAsset.build_surface_render_for_preset_kind",
    "SystemLibrary.line_trace_single",
    "TraceTypeQuery.TRACE_TYPE_QUERY1",
    "DrawDebugTrace.NONE",
)
PRESET_EXTRA_PROPERTIES = ("default_channel", "plane_settings", "output_material", "surface_mesh")
LIVE_ACTOR_EXTRA_PROPERTIES = ("interaction_brush_channel", "follow_target_actor")
OUTPUT_ACTOR_PROPERTIES = ("source_actor", "tile_source_actor", "decal_material", "mesh_material", "output_mesh", "decal_sort_order")
EMITTER_PROPERTIES = ("source_breakable_actor", "trail_settings", "targets")
BREAKABLE_PROPERTIES = ("break_force", "break_force_radius")


def load_module(module_name, *relative_parts):
    module_path = os.path.join(unreal.SystemLibrary.get_project_directory(), *relative_parts)
    module_spec = importlib.util.spec_from_file_location(module_name, module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모듈 없음: {module_path}")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def object_path_of(value):
    if value is None:
        return ""
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    return str(value)


def refers_to(value, asset):
    if value is None or asset is None:
        return False
    stored = object_path_of(value)
    return stored == asset.get_path_name() or asset.get_path_name() in stored


def material_asset_path(key):
    return f"{MATERIAL_DIR}/{MATERIAL_NAMES[key]}"


def load_stage_assets(fire_stage):
    assets = unreal.EditorAssetLibrary
    paths = {
        "grid": GRID_MESH_PATH,
        "plane": fire_stage.PLANE_MESH_PATH,
        "cube": fire_stage.CUBE_MESH_PATH,
        "collection": fire_stage.GEOMETRY_COLLECTION_PATH,
        "steam_preset": STEAM_PRESET_PATH,
    }
    paths.update({"material_" + key: material_asset_path(key) for key in MATERIAL_NAMES})
    loaded = {name: assets.load_asset(path) if assets.does_asset_exist(path) else None for name, path in paths.items()}
    missing = [paths[name] for name, value in loaded.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 없음(재질 도구·불 무대·연기 셋업 무대 먼저): " + ", ".join(missing))
    if not isinstance(loaded["steam_preset"], unreal.TDFluidPresetAsset):
        raise RuntimeError("증기 프리셋 형식 불일치: " + STEAM_PRESET_PATH)
    return loaded


def load_stage_classes(particle_stage, fire_stage, mass_stage):
    classes = {
        "live_actor": unreal.load_class(None, particle_stage.FLUID_ACTOR_CLASS_PATH),
        "output_actor": unreal.load_class(None, OUTPUT_ACTOR_CLASS_PATH),
        "emitter": unreal.load_class(None, fire_stage.CLASS_PATHS["TDFluidFragmentEmitterActor"]),
        "breakable": unreal.load_class(None, fire_stage.CLASS_PATHS["TDBreakableActor"]),
    }
    missing = [name for name, value in classes.items() if value is None]
    if missing:
        raise RuntimeError("클래스 없음(재빌드 뒤 python Tools/ue_editor.py restart): " + ", ".join(missing))
    classes["obstacle"] = mass_stage.load_obstacle_component_class(particle_stage)
    return classes


def find_contract_problems(particle_stage, classes):
    problems = particle_stage.find_missing_properties(unreal.TDFluidPresetAsset.get_default_object(), particle_stage.PRESET_REQUIRED_PROPERTIES + PRESET_EXTRA_PROPERTIES)
    checks = (
        ("live_actor", particle_stage.ACTOR_REQUIRED_PROPERTIES + LIVE_ACTOR_EXTRA_PROPERTIES),
        ("output_actor", OUTPUT_ACTOR_PROPERTIES),
        ("emitter", EMITTER_PROPERTIES),
        ("breakable", BREAKABLE_PROPERTIES),
    )
    for class_key, property_names in checks:
        problems += particle_stage.find_missing_properties(unreal.get_default_object(classes[class_key]), property_names)
    return problems


def find_unmerged_materials(particle_stage):
    look_path = os.path.join(unreal.SystemLibrary.get_project_directory(), *particle_stage.MATERIAL_LOOK_RELATIVE_PATH)
    with open(look_path, encoding="utf-8") as look_file:
        table = json.load(look_file).get("Materials", {})
    return [material_asset_path(key) for key in MATERIAL_NAMES if material_asset_path(key) not in table]


def require_lava_tile_wrap():
    config = unreal.TDFluidPresetAsset.build_config_for_preset_kind(unreal.TDFluidPresetKind.LAVA_TILE)
    if config.get_editor_property("area_address_mode") != unreal.TDFluidAreaAddressMode.WRAP:
        raise RuntimeError("Kinds.LavaTile 구성의 감싸기(area_address_mode)가 WRAP 이 아님: 정본 JSON 미반영 또는 에디터 재시작 필요")


def configure_viscous_preset(context, spec):
    particle_stage = context["particle_stage"]
    kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
    preset, path = particle_stage.load_or_create_preset(spec["name"])
    material = context["assets"]["material_" + spec["material"]] if spec["material"] else None
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("default_channel", getattr(unreal.TDFluidBrushChannel, spec["channel"]))
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind))
    preset.set_editor_property("plane_settings", unreal.TDFluidPresetAsset.build_plane_settings_for_preset_kind(kind))
    preset.set_editor_property("output_material", material)
    preset.set_editor_property("surface_mesh", context["assets"]["grid"] if spec["has_mesh"] else None)
    effective = preset.resolve_effective_config()
    extent = effective.get_editor_property("sim_extent_cm")
    if extent.x <= 0.0 or extent.y <= 0.0:
        raise RuntimeError(f"유효 구성 창이 0: {path}")
    particle_stage.save_asset(preset, path)
    if preset.get_editor_property("preset_kind") != kind:
        raise RuntimeError(f"프리셋 종류 다시 읽기 불일치: {path} ≠ {spec['kind']}")
    if (preset.get_editor_property("output_material") is None) != (material is None) or (preset.get_editor_property("surface_mesh") is None) != (not spec["has_mesh"]):
        raise RuntimeError(f"프리셋 재질·메시 다시 읽기 불일치: {path}")
    if spec["kind"] == "LAVA_TILE" and effective.get_editor_property("area_address_mode") != unreal.TDFluidAreaAddressMode.WRAP:
        raise RuntimeError(f"LavaTile 유효 구성 감싸기 아님: {path}")
    return preset, path, effective


def describe_preset(spec, path, effective):
    surface = unreal.TDFluidPresetAsset.build_surface_render_for_preset_kind(getattr(unreal.TDFluidPresetKind, spec["kind"]))
    return {
        "path": path,
        "kind": spec["kind"],
        "default_channel": spec["channel"],
        "output_material": MATERIAL_NAMES[spec["material"]] if spec["material"] else None,
        "surface_mesh": GRID_MESH_PATH if spec["has_mesh"] else None,
        "grid_resolution": effective.get_editor_property("grid_resolution"),
        "uses_decal_output": bool(surface.get_editor_property("uses_decal_output")),
        "decal_box_height_cm": surface.get_editor_property("decal_box_height_cm"),
        "internal_plane_hidden_expected": spec["material"] is None and not spec["has_mesh"],
    }


def spawn_live_actor(context, label, preset_name, location, folder):
    particle_stage = context["particle_stage"]
    preset, _, effective = context["presets"][preset_name]
    actor = context["actors"].spawn_actor_from_class(context["classes"]["live_actor"], unreal.Vector(*location), unreal.Rotator())
    particle_stage.finish_actor(actor, label, folder, context["created"])
    particle_stage.configure_live_actor(actor, preset, effective)
    actor.set_editor_property("interaction_brush_channel", preset.get_editor_property("default_channel"))
    return actor


def place_orbit_stirrer(context, pivot, radius_cm, label, folder, height_cm=None, degrees_per_sec=None, scale=None):
    particle_stage = context["particle_stage"]
    stirrer_height = particle_stage.STIRRER_HEIGHT_CM if height_cm is None else height_cm
    stirrer = context["actors"].spawn_actor_from_class(context["stirrer_class"], unreal.Vector(pivot[0] + radius_cm, pivot[1], stirrer_height), unreal.Rotator())
    particle_stage.finish_actor(stirrer, label, folder, context["created"])
    motion = {"pivot_translation": unreal.Vector(-radius_cm, 0.0, 0.0)}
    if degrees_per_sec is not None:
        motion["rotation_rate"] = unreal.Rotator(roll=0.0, pitch=0.0, yaw=degrees_per_sec)
    particle_stage.set_component_properties(stirrer, unreal.RotatingMovementComponent, motion)
    if scale is not None:
        stirrer.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    return stirrer


def build_fragment_rig(context, label_prefix, location, target_actor, folder):
    particle_stage, fire_stage = context["particle_stage"], context["fire_stage"]
    actors, created, classes = context["actors"], context["created"], context["classes"]
    breakable = actors.spawn_actor_from_class(classes["breakable"], unreal.Vector(*location), unreal.Rotator())
    particle_stage.finish_actor(breakable, label_prefix + "_Block", folder, created)
    geometry_component = breakable.get_component_by_class(unreal.GeometryCollectionComponent)
    if geometry_component is None:
        raise RuntimeError("파괴 액터에 지오메트리 컬렉션 컴포넌트가 없습니다.")
    geometry_component.set_rest_collection(context["assets"]["collection"])
    breakable.set_editor_property("break_force", fire_stage.BREAK_FORCE)
    breakable.set_editor_property("break_force_radius", fire_stage.BREAK_FORCE_RADIUS)
    emitter = actors.spawn_actor_from_class(classes["emitter"], unreal.Vector(*location), unreal.Rotator())
    particle_stage.finish_actor(emitter, label_prefix + "_Fragments", folder, created)
    emitter.set_editor_property("source_breakable_actor", breakable)
    emitter.set_editor_property("trail_settings", unreal.TDFluidPresetAsset.build_fragment_trail_for_preset_kind(getattr(unreal.TDFluidPresetKind, FRAGMENT_TRAIL_KIND_NAME)))
    emitter.set_editor_property("targets", [fire_stage.make_fragment_target(FRAGMENT_TARGET_SPEC, target_actor)])
    stored_targets = emitter.get_editor_property("targets")
    if emitter.get_editor_property("source_breakable_actor") != breakable:
        raise RuntimeError("조각 방출 액터의 파괴 액터 지정 확인 실패: " + label_prefix)
    if len(stored_targets) != 1 or object_path_of(stored_targets[0].get_editor_property("target_actor")) != target_actor.get_path_name():
        raise RuntimeError("조각 방출 액터의 대상 배열 지정 확인 실패: " + label_prefix)
    return {
        "breakable": breakable.get_actor_label(),
        "emitter": emitter.get_actor_label(),
        "target_actor": target_actor.get_actor_label(),
        "trigger": {"call": "TDFluidFragmentEmitterActor.trigger_break_at (PIE 안 도구 호출)", "hit_offset_cm": list(fire_stage.EXPLOSION_HIT_OFFSET)},
    }


def spawn_output_actor(context, label, location, folder, scale=None, source=None, tile_source=None, decal_material=None, mesh_material=None, mesh=None):
    particle_stage = context["particle_stage"]
    output = context["actors"].spawn_actor_from_class(context["classes"]["output_actor"], unreal.Vector(*location), unreal.Rotator())
    particle_stage.finish_actor(output, label, folder, context["created"])
    if scale is not None:
        output.set_actor_scale3d(unreal.Vector(*scale))
    wanted = {"source_actor": source, "tile_source_actor": tile_source, "decal_material": decal_material, "mesh_material": mesh_material, "output_mesh": mesh}
    for property_name, value in wanted.items():
        if value is None:
            continue
        output.set_editor_property(property_name, value)
        if not refers_to(output.get_editor_property(property_name), value):
            raise RuntimeError(f"출력 액터 속성 다시 읽기 불일치: {label}.{property_name}")
    return output


def slope_top_height(slope_center, x):
    pitch = math.radians(-SLOPE_PITCH_DEGREES)
    half_thickness = BASIC_SHAPE_HALF_SIZE_CM * SLOPE_SCALE[2]
    return slope_center[2] - math.tan(pitch) * (x - slope_center[0]) + half_thickness / math.cos(pitch)


def trace_slope_height(world, slope_center, x, y):
    expected = slope_top_height(slope_center, x)
    start = unreal.Vector(x, y, expected + SLOPE_TRACE_RANGE_CM)
    end = unreal.Vector(x, y, expected - SLOPE_TRACE_RANGE_CM)
    hit = unreal.SystemLibrary.line_trace_single(world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    values = hit.to_tuple() if hit is not None else None
    if not values or not values[0]:
        raise RuntimeError(f"경사 트레이스 실패({x:.0f}, {y:.0f}): 런타임 지형 높이도 실패한다")
    traced = float(values[5].z)
    if abs(traced - expected) > SLOPE_TRACE_TOLERANCE_CM:
        raise RuntimeError(f"경사 트레이스 높이 불일치({x:.0f}, {y:.0f}): 트레이스 {traced:.1f}, 해석 {expected:.1f}")
    return {"x": x, "y": y, "traced_z": traced, "analytic_z": expected}


def build_zone_slide(context, zone, folder):
    center = zone["center"]
    actor = spawn_live_actor(context, zone["id"], "ViscousBlood", (center[0], center[1], GROUND_LIFT_CM), folder)
    stirrer = place_orbit_stirrer(context, center, SLIDE_ORBIT_RADIUS_CM, zone["id"] + "_Stirrer", folder)
    return {"primary_labels": [actor.get_actor_label()], "labels": [actor.get_actor_label(), stirrer.get_actor_label()], "preset": "ViscousBlood", "stirrer_orbit_radius_cm": SLIDE_ORBIT_RADIUS_CM}


def build_zone_splash(context, zone, folder):
    center = zone["center"]
    actor = spawn_live_actor(context, zone["id"], "ViscousBlood", (center[0], center[1], GROUND_LIFT_CM), folder)
    rig = build_fragment_rig(context, zone["id"], (center[0], center[1], BREAKABLE_HEIGHT_CM), actor, folder)
    return {"primary_labels": [actor.get_actor_label()], "labels": [actor.get_actor_label(), rig["breakable"], rig["emitter"]], "preset": "ViscousBlood", "fragment_rig": rig}


def build_zone_mud_world(context, zone, folder):
    center = zone["center"]
    actor = spawn_live_actor(context, zone["id"], "ViscousMud", (center[0], center[1], GROUND_LIFT_CM), folder)
    ball = place_orbit_stirrer(context, center, BALL_ORBIT_RADIUS_CM, zone["id"] + "_Ball", folder, BALL_HEIGHT_CM, BALL_DEGREES_PER_SEC, BALL_SCALE)
    return {"primary_labels": [actor.get_actor_label()], "labels": [actor.get_actor_label(), ball.get_actor_label()], "preset": "ViscousMud", "window": "고정", "ball_orbit_radius_cm": BALL_ORBIT_RADIUS_CM}


def build_zone_mud_local(context, zone, folder):
    center = zone["center"]
    ball = place_orbit_stirrer(context, center, FOLLOW_ORBIT_RADIUS_CM, zone["id"] + "_Ball", folder, BALL_HEIGHT_CM, FOLLOW_DEGREES_PER_SEC, BALL_SCALE)
    actor = spawn_live_actor(context, zone["id"], "ViscousMud", (center[0] + FOLLOW_ORBIT_RADIUS_CM, center[1], GROUND_LIFT_CM), folder)
    actor.set_editor_property("follow_target_actor", ball)
    if object_path_of(actor.get_editor_property("follow_target_actor")) != ball.get_path_name():
        raise RuntimeError("진흙 로컬 FollowTargetActor 다시 읽기 불일치")
    return {
        "primary_labels": [actor.get_actor_label()],
        "labels": [actor.get_actor_label(), ball.get_actor_label()],
        "preset": "ViscousMud",
        "window": "follow_target_actor = 궤도 교반기(반경 6 m, 45도/초)를 따라 창 중심이 이동[추정: 실제 추종 갱신은 PIE에서 확인]",
    }


def build_zone_flow_decal(context, zone, folder):
    center = zone["center"]
    world, actors, particle_stage, created = context["world"], context["actors"], context["particle_stage"], context["created"]
    slope_center = (center[0], center[1], SLOPE_CENTER_HEIGHT_CM)
    slope = mass_spawn_static_mesh(context, context["assets"]["cube"], slope_center, unreal.Rotator(pitch=SLOPE_PITCH_DEGREES), SLOPE_SCALE, zone["id"] + "_Slope", folder)
    center_probe = trace_slope_height(world, slope_center, center[0], center[1])
    stirrer_x = center[0] + SLOPE_STIRRER_OFFSET_CM
    stirrer_probe = trace_slope_height(world, slope_center, stirrer_x, center[1])
    actor = spawn_live_actor(context, zone["id"], "ViscousBloodDecal", (center[0], center[1], center_probe["traced_z"]), folder)
    stirrer = place_orbit_stirrer(context, (stirrer_x, center[1]), SLOPE_STIRRER_ORBIT_CM, zone["id"] + "_Stirrer", folder, stirrer_probe["traced_z"] + SLOPE_STIRRER_LIFT_CM)
    output = spawn_output_actor(context, zone["id"] + "_Output", (center[0], center[1], center_probe["traced_z"]), folder, source=actor, decal_material=context["assets"]["material_decal"])
    return {
        "primary_labels": [actor.get_actor_label()],
        "labels": [actor.get_actor_label(), slope.get_actor_label(), stirrer.get_actor_label(), output.get_actor_label()],
        "preset": "ViscousBloodDecal",
        "slope_pitch_degrees": SLOPE_PITCH_DEGREES,
        "slope_trace": [center_probe, stirrer_probe],
        "output_actor": output.get_actor_label(),
        "decal_depth_source": "C++ ATDFluidSurfaceOutputActor 가 지형 범위(TryGetSampledTerrainRangeCm)로 중심·깊이를 잡는다(M4). 무대는 DecalBoxHeightCm 를 정하지 않는다",
    }


def build_zone_flow_mesh(context, zone, folder):
    center = zone["center"]
    actor = spawn_live_actor(context, zone["id"], "ViscousBlood", (center[0], center[1], GROUND_LIFT_CM), folder)
    stirrer = place_orbit_stirrer(context, center, MESH_STIRRER_ORBIT_CM, zone["id"] + "_Stirrer", folder)
    rig = build_fragment_rig(context, zone["id"], (center[0] + MESH_BREAKABLE_OFFSET_CM, center[1], BREAKABLE_HEIGHT_CM), actor, folder)
    return {
        "primary_labels": [actor.get_actor_label()],
        "labels": [actor.get_actor_label(), stirrer.get_actor_label(), rig["breakable"], rig["emitter"]],
        "preset": "ViscousBlood",
        "terrain": "평지(M4: 내부 격자 평면은 액터 Z에 평평하게 붙으므로 경사를 쓰지 않는다)",
        "fragment_rig": rig,
    }


def build_zone_bath(context, zone, folder):
    center = zone["center"]
    actor = spawn_live_actor(context, zone["id"], "ViscousBlood", (center[0], center[1], GROUND_LIFT_CM), folder)
    labels = [actor.get_actor_label()]
    obstacle_labels = []
    for index in range(BATH_RING_COUNT):
        angle = 2.0 * math.pi * index / BATH_RING_COUNT
        location = (center[0] + BATH_RING_RADIUS_CM * math.cos(angle), center[1] + BATH_RING_RADIUS_CM * math.sin(angle), BATH_RING_HEIGHT_CM)
        segment = mass_spawn_static_mesh(context, context["assets"]["cube"], location, unreal.Rotator(yaw=math.degrees(angle) + 90.0), BATH_RING_SCALE, f"{zone['id']}_Ring{index + 1}", folder)
        context["mass_stage"].add_obstacle_component(segment, context["classes"]["obstacle"])
        obstacle_labels.append(segment.get_actor_label())
    for index in range(BATH_STIRRER_COUNT):
        angle = 2.0 * math.pi * index / BATH_STIRRER_COUNT
        pivot = (center[0] + BATH_STIRRER_RING_RADIUS_CM * math.cos(angle), center[1] + BATH_STIRRER_RING_RADIUS_CM * math.sin(angle))
        labels.append(place_orbit_stirrer(context, pivot, BATH_STIRRER_ORBIT_CM, f"{zone['id']}_Stirrer{index + 1}", folder).get_actor_label())
    return {"primary_labels": [actor.get_actor_label()], "labels": labels + obstacle_labels, "preset": "ViscousBlood", "obstacle_components": obstacle_labels, "ring_radius_cm": BATH_RING_RADIUS_CM}


def build_zone_lava_flow(context, zone, folder):
    center = zone["center"]
    actor = spawn_live_actor(context, zone["id"], "LavaFlow", (center[0], center[1], GROUND_LIFT_CM), folder)
    source_stirrer = place_orbit_stirrer(context, (center[0] + LAVA_FLOW_STIRRER_OFFSET_CM, center[1]), LAVA_FLOW_STIRRER_ORBIT_CM, zone["id"] + "_SourceStirrer", folder, None, LAVA_FLOW_DEGREES_PER_SEC)
    steam_preset = context["assets"]["steam_preset"]
    steam_center = (center[0] + STEAM_OFFSET_CM, center[1])
    steam = context["actors"].spawn_actor_from_class(context["classes"]["live_actor"], unreal.Vector(steam_center[0], steam_center[1], GROUND_LIFT_CM), unreal.Rotator())
    context["particle_stage"].finish_actor(steam, zone["id"] + "_Steam", folder, context["created"])
    context["particle_stage"].configure_live_actor(steam, steam_preset, steam_preset.resolve_effective_config())
    steam_stirrer = place_orbit_stirrer(context, steam_center, STEAM_STIRRER_ORBIT_CM, zone["id"] + "_SteamStirrer", folder)
    return {
        "primary_labels": [actor.get_actor_label(), steam.get_actor_label()],
        "labels": [actor.get_actor_label(), source_stirrer.get_actor_label(), steam.get_actor_label(), steam_stirrer.get_actor_label()],
        "preset": "LavaFlow",
        "terrain": "평지 + 교반기 구동 흐름(M4 proposed: 경사에서는 내부 평면이 묻히거나 뜬다)",
        "steam_preset": STEAM_PRESET_PATH,
        "tile_map": "출력 액터 없음 → TileMap1 미등록(용암 타일 합성은 LavaWorld 에서 확인)",
    }


def build_zone_lava_world(context, zone, folder):
    center = zone["center"]
    tile = spawn_live_actor(context, zone["id"] + "_Tile", "LavaTile", (center[0], center[1], GROUND_LIFT_CM), folder)
    local = spawn_live_actor(context, zone["id"] + "_Local", "LavaLocal", (center[0], center[1], GROUND_LIFT_CM), folder)
    local.set_editor_property("should_follow_player_pawn", True)
    output = spawn_output_actor(
        context, zone["id"] + "_Output", (center[0], center[1], LAVA_PLANE_LIFT_CM), folder, (LAVA_PLANE_SCALE, LAVA_PLANE_SCALE, 1.0),
        source=local, tile_source=tile, mesh_material=context["assets"]["material_lava"], mesh=context["assets"]["plane"],
    )
    return {
        "primary_labels": [tile.get_actor_label(), local.get_actor_label()],
        "labels": [tile.get_actor_label(), local.get_actor_label(), output.get_actor_label()],
        "preset": "LavaTile+LavaLocal",
        "output_actor": output.get_actor_label(),
        "plane_scale": [LAVA_PLANE_SCALE, LAVA_PLANE_SCALE, 1.0],
        "plane_lift_cm": LAVA_PLANE_LIFT_CM,
        "row_y_cm": center[1],
    }


ZONE_BUILDERS = {
    "slide": build_zone_slide,
    "splash": build_zone_splash,
    "mud_world": build_zone_mud_world,
    "mud_local": build_zone_mud_local,
    "flow_decal": build_zone_flow_decal,
    "flow_mesh": build_zone_flow_mesh,
    "bath": build_zone_bath,
    "lava_flow": build_zone_lava_flow,
    "lava_world": build_zone_lava_world,
}


def mass_spawn_static_mesh(context, mesh, location, rotation, scale, label, folder):
    return context["mass_stage"].spawn_static_mesh(context["actors"], context["particle_stage"], context["created"], mesh, location, rotation, scale, label, folder)


def compute_floor():
    min_x = min(zone["center"][0] - zone["half_cm"] for zone in ZONE_SPECS) - FLOOR_MARGIN_CM
    max_x = max(zone["center"][0] + zone["half_cm"] for zone in ZONE_SPECS) + FLOOR_MARGIN_CM
    min_y = min(zone["center"][1] - zone["half_cm"] for zone in ZONE_SPECS) - FLOOR_MARGIN_CM
    max_y = max(zone["center"][1] + zone["half_cm"] for zone in ZONE_SPECS) + FLOOR_MARGIN_CM
    return ((min_x + max_x) * 0.5, (min_y + max_y) * 0.5), ((max_x - min_x) / 100.0, (max_y - min_y) / 100.0, 0.5)


def attach_cameras(zone_records):
    every_primary = [label for record in zone_records for label in record["primary_labels"]]
    for record in zone_records:
        center, camera = record["center"], record["camera_spec"]
        record["camera"] = {
            "pawn": [center[0], center[1] - camera["arm"]],
            "arm": camera["arm"],
            "view": list(camera["view"]),
            "awake": {label: label in record["primary_labels"] for label in every_primary},
        }
        del record["camera_spec"]


def build_decisions():
    return [
        "M4 5: 경사 데칼 깊이는 C++ 출력 액터가 지형 범위로 잡는다(무대는 DecalBoxHeightCm 미지정)",
        "M4 6: ViscousBloodFlowMesh 는 평지(내부 격자 평면은 액터 Z에 평평하게 붙음)",
        "M4 9: LavaFlow 는 평지 + 교반기 구동 흐름(proposed)",
        f"M4 8: LavaWorld 는 별도 Y 줄({ZONE_SPECS[-1]['center'][1]:.0f} cm), 평면 바닥 위 {LAVA_PLANE_LIFT_CM:.0f} cm",
        "S7: DA default_channel 은 ViscousBlood·ViscousBloodDecal = CHANNEL_G_BLOOD, 나머지 CHANNEL_R_MUD",
        f"S8: 조각 방출기 TrailSettings = build_fragment_trail_for_preset_kind({FRAGMENT_TRAIL_KIND_NAME}) (종류별 블록이 없으면 StructDefaults), 대상 = TargetActor, 파괴 계기 = PIE 에서 emitter.trigger_break_at(파괴 액터 중심 + 오프셋)",
        "S9: 모든 차선 조각 병합 뒤에만 실행(모양 정본에 MI 4개가 없으면 시작 점검에서 멈춤)",
        "증기: 기존 DA_TDFluid_SetupSteamPuff 사용(명세의 불 액터 표기와 달리 선례는 볼륨 연기 라이브 액터)",
    ]


def write_failure_report(error):
    report_path = os.path.join(unreal.SystemLibrary.get_project_directory(), *REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump({"status": "failed", "error": str(error)}, report_file, ensure_ascii=False, indent=2)


def main():
    particle_stage = load_module("td_fluid_particle_stage", "Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
    fire_stage = load_module("td_fluid_fire_stage", "Tools", "FluidFX", "editor_make_fluid_fire_stage.py")
    mass_stage = load_module("td_fluid_mass_input_stage", "Tools", "FluidFX", "editor_make_fluid_mass_input_stage.py")
    editor = particle_stage.require_editor_ready(tuple(particle_stage.REQUIRED_UNREAL_NAMES) + EXTRA_REQUIRED_UNREAL_NAMES)
    classes = load_stage_classes(particle_stage, fire_stage, mass_stage)
    contract_problems = find_contract_problems(particle_stage, classes)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(재빌드와 에디터 재시작 필요): " + ", ".join(contract_problems))
    assets_by_name = load_stage_assets(fire_stage)
    overwrite = particle_stage.read_overwrite_request(REQUEST_RELATIVE_PATH)
    preset_paths = [f"{particle_stage.PRESET_DIR}/DA_TDFluid_{spec['name']}" for spec in PRESET_SPECS]
    particle_stage.guard_existing_assets(preset_paths + [STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)
    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = particle_stage.check_and_reload_preset_data()
    unmerged = find_unmerged_materials(particle_stage)
    if unmerged:
        raise RuntimeError("모양 정본에 MI 항목 없음(모든 차선 조각 병합 먼저, L-repo-27): " + ", ".join(unmerged))
    require_lava_tile_wrap()

    print("단계: 교반기·프리셋")
    stirrer_class, stirrer_created = particle_stage.load_or_create_stirrer_blueprint()
    context = {"particle_stage": particle_stage, "fire_stage": fire_stage, "mass_stage": mass_stage, "classes": classes, "assets": assets_by_name,
               "stirrer_class": stirrer_class, "created": [], "presets": {}}
    for spec in PRESET_SPECS:
        context["presets"][spec["name"]] = configure_viscous_preset(context, spec)

    print("단계: 레벨")
    levels, actors = particle_stage.open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(fire_stage.GAME_MODE_PATH))
    context["actors"], context["world"] = actors, world
    floor_center, floor_scale = compute_floor()
    particle_stage.place_floor_and_lights(actors, context["created"], STAGE_FOLDER, floor_center, floor_scale)
    zone_records = []
    for zone in ZONE_SPECS:
        print(f"단계: 구역 {zone['zone']} {zone['id']}")
        details = ZONE_BUILDERS[zone["layout"]](context, zone, f"{STAGE_FOLDER}/{zone['zone']}_{zone['id']}")
        record = {"zone": zone["zone"], "id": zone["id"], "layout": zone["layout"], "center": list(zone["center"]), "refs": zone["refs"], "camera_spec": zone["camera"]}
        record.update(details)
        zone_records.append(record)
    attach_cameras(zone_records)
    preset_records = {spec["name"]: describe_preset(spec, context["presets"][spec["name"]][1], context["presets"][spec["name"]][2]) for spec in PRESET_SPECS}
    warnings = []
    if not preset_records["ViscousBloodDecal"]["uses_decal_output"]:
        warnings.append("Kinds.ViscousBlood 의 SurfaceRender.bUsesDecalOutput 가 꺼져 있음: 출력 액터 데칼이 숨겨진다")

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    particle_stage.require_saved_files(preset_paths + [particle_stage.STIRRER_BLUEPRINT_PATH], STAGE_MAP_PATH)
    report = {
        "status": "ok",
        "preset_tuning_sha256": tuning_sha256,
        "material_look_sha256": look_sha256,
        "map": STAGE_MAP_PATH,
        "overwrite": overwrite,
        "created_stirrer_blueprint": stirrer_created,
        "presets": preset_records,
        "warnings": warnings,
        "materials": {key: material_asset_path(key) for key in MATERIAL_NAMES},
        "decisions": build_decisions(),
        "zones": zone_records,
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
        write_failure_report(error)
        raise


if __name__ == "__main__":
    run_with_failure_report()
