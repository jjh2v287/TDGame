"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). FX-15a 솔버 옵션 무대 LV_TDFluidSolverOptionsStage(X축 30 m 간격 구역 3개: 0 기존 연기 DA_TDFluid_SmokeSoft 대 SmokeKernelPressure 를 Y축 10 m 간격·같은 교반기로 나란히 / 1 SmokeWindDrift + 교반기 / 2 SmokeAnchoredCauldron 을 교반기 BP_TDFluidStageStirrer 궤도 부모에 붙임)와 DA_TDFluid_<종류> 3개(build_config_for_preset_kind)를 만든다. 먼저 editor_make_fluid_smoke_stage.py(SmokeSoft 기준 DA)·editor_make_fluid_particle_stage.py(교반기 BP·공용 함수)가 성공해 있어야 하고, FX-15a C++ 통합·에디터 재시작 뒤에 돌린다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_solver_options_stage.py (PowerShell, L-repo-01. 기존 에셋을 갱신할 때만 Saved/FluidFX/fluid_solver_options_stage_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Presets/DA_TDFluid_SmokeKernelPressure·SmokeWindDrift·SmokeAnchoredCauldron, /Game/FluidFX/Maps/LV_TDFluidSolverOptionsStage, Saved/FluidFX/fluid_solver_options_stage.json(preset_tuning_sha256·material_look_sha256 = check_fluid_preset_data.py --stage-report 형식, 구역 중심·카메라 제안, 조절 정본 값 다시 읽기 대조 json_dependencies_differ)
상태: 미검증(2026-10-29 FX-15a L2 작성: py_compile 만, 에디터 실행 전). 판정은 run_in_editor.py 의 SUCCESS 가 아니라 출력 본문의 "status": "ok" 와 "보고서:" 줄로 한다(L-editor-73). 시작 점검(언리얼 Python 이름·구성 속성)이 실패하면 에셋을 바꾸기 전에 멈춘다. 새 조절 값은 DA 사본이 없고 JSON 종류 값이 정본이라 무대는 읽어 보고만 한다(DEV-FX9-11 선례). 구역 0 기준은 spec 의 DA_TDFluid_Smoke 가 없어 SmokeSoft 로 둔다. 교반기 궤도(반경 300 cm·초당 30도)는 무대 상수 [제안]. 블루프린트는 구성 전용. 2026-10-31: 가마솥 궤도 교반기 회전 속도가 피벗 쓰기 뒤 BP 구성 스크립트 재실행으로 유실되던(기본 90°/s) 것을 stage.set_component_properties(한 번에 쓰기·새 핸들로 다시 읽기, 다르면 FAIL)로 고침, 맵 재생성 필요.
"""
import importlib.util
import json
import os

import unreal


FLUID_ROOT = "/Game/FluidFX"
PRESET_DIR = f"{FLUID_ROOT}/Presets"
MAP_DIR = f"{FLUID_ROOT}/Maps"
STAGE_MAP_PATH = f"{MAP_DIR}/LV_TDFluidSolverOptionsStage"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_solver_options_stage_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_solver_options_stage.json")
PARTICLE_STAGE_RELATIVE_PATH = ("Tools", "FluidFX", "editor_make_fluid_particle_stage.py")
STAGE_FOLDER = "TDGen/FluidFX/SolverOptionsStage"

BASELINE_PRESET_NAME = "SmokeSoft"
VISUAL_PROPERTY_NAMES = ("volume_material", "volume_mesh", "volume_height_cm")
KIND_SPECS = (
    {"name": "SmokeKernelPressure", "kind": "SMOKE_KERNEL_PRESSURE"},
    {"name": "SmokeWindDrift", "kind": "SMOKE_WIND_DRIFT"},
    {"name": "SmokeAnchoredCauldron", "kind": "SMOKE_ANCHORED_CAULDRON"},
)

ZONE_SPACING_CM = 3000.0
PAIR_SPACING_CM = 1000.0
ACTIVATION_DISTANCE_CM = 2000.0
ACTOR_Z_CM = 1.0
FLOOR_MARGIN_CM = 2000.0
FLOOR_WIDTH_CM = 3000.0
CAULDRON_ORBIT_RADIUS_CM = 300.0
CAULDRON_DEGREES_PER_SEC = 30.0
CAMERA_VIEW = (-50.0, 0.0)

ZONES = (
    {"index": 0, "label": "KernelVsPoisson", "camera_arm_cm": 1800.0, "metrics": ["R·F 스윕", "Poisson 대 Kernel 나란히"]},
    {"index": 1, "label": "WindDrift", "camera_arm_cm": 900.0, "metrics": ["중심 이동 속도 / U"]},
    {"index": 2, "label": "AnchoredCauldron", "camera_arm_cm": 1200.0, "metrics": ["꼬리 길이"]},
)
ACTOR_SPECS = (
    {"label": "SolverBaseline", "preset": BASELINE_PRESET_NAME, "zone": 0, "offset": (0.0, -0.5 * PAIR_SPACING_CM), "mount": "ground"},
    {"label": "SolverKernelPressure", "preset": "SmokeKernelPressure", "zone": 0, "offset": (0.0, 0.5 * PAIR_SPACING_CM), "mount": "ground"},
    {"label": "SolverWindDrift", "preset": "SmokeWindDrift", "zone": 1, "offset": (0.0, 0.0), "mount": "ground"},
    {"label": "SolverAnchoredCauldron", "preset": "SmokeAnchoredCauldron", "zone": 2, "offset": (CAULDRON_ORBIT_RADIUS_CM, 0.0), "mount": "orbit"},
)
EXPECTED_CONFIG_VALUES = (
    ("SmokeKernelPressure", "pressure_method", "KERNEL_FEEDBACK"),
    ("SmokeWindDrift", "direct_velocity_cm_per_sec", (60.0, 0.0, 0.0)),
    ("SmokeAnchoredCauldron", "anchors_density_to_actor", True),
    ("SmokeAnchoredCauldron", "sim_area_motion_velocity_scale", 1.0),
    ("SmokeAnchoredCauldron", "grid_resolution", 128),
    ("SmokeAnchoredCauldron", "sim_extent_cm", (300.0, 300.0)),
)

REQUIRED_UNREAL_NAMES = (
    "get_engine_subsystem",
    "TDFluidPresetTuningSubsystem",
    "TDFluidPresetAsset",
    "TDFluidSimulationConfig",
    "TDFluidPresetKind.SMOKE_KERNEL_PRESSURE",
    "TDFluidPresetKind.SMOKE_WIND_DRIFT",
    "TDFluidPresetKind.SMOKE_ANCHORED_CAULDRON",
    "TDFluidPressureMethod.KERNEL_FEEDBACK",
    "TDFluidBrushChannel.CHANNEL_R_MUD",
    "EditorAssetLibrary.load_blueprint_class",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "DataAssetFactory",
    "RotatingMovementComponent",
    "AttachmentRule.KEEP_WORLD",
    "DirectionalLight",
    "SkyLight",
    "PlayerStart",
)
CONFIG_REQUIRED_PROPERTIES = (
    "pressure_method",
    "pressure_kernel_divergence_radius_texels",
    "pressure_kernel_passes_per_projection",
    "direct_velocity_cm_per_sec",
    "direct_velocity_in_world_space",
    "anchors_density_to_actor",
    "sim_area_motion_velocity_scale",
    "flow_amplitude_noise_damping_per_sec",
    "flow_amplitude_noise_scale_cm",
    "randomize_noise_offset_per_actor",
)
PRESET_REQUIRED_PROPERTIES = ("preset_kind", "override_preset_defaults", "default_channel", "simulation_config") + VISUAL_PROPERTY_NAMES
ACTOR_REQUIRED_PROPERTIES = ("live_component", "should_follow_player_pawn", "should_use_activation_volume", "activation_distance_cm", "accept_mouse_brush", "interaction_brush_channel")


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


def zone_center(index):
    return (index * ZONE_SPACING_CM, 0.0)


def preset_asset_path(name):
    return f"{PRESET_DIR}/DA_TDFluid_{name}"


def find_actor_spec_problems():
    problems = []
    zone_indices = {zone["index"] for zone in ZONES}
    for spec in ACTOR_SPECS:
        if spec["zone"] not in zone_indices:
            problems.append(f"구역 번호 없음: {spec['label']}")
    if [zone["index"] for zone in ZONES] != list(range(len(ZONES))):
        problems.append("구역 번호 순서 어긋남")
    pair = [spec for spec in ACTOR_SPECS if spec["zone"] == 0]
    if len(pair) != 2 or abs(pair[0]["offset"][1] - pair[1]["offset"][1]) != PAIR_SPACING_CM:
        problems.append("구역 0 비교 쌍 간격 어긋남")
    orbit = [spec for spec in ACTOR_SPECS if spec["mount"] == "orbit"]
    if len(orbit) != 1 or orbit[0]["preset"] != "SmokeAnchoredCauldron":
        problems.append("궤도 부모에 붙일 액터는 SmokeAnchoredCauldron 하나여야 함")
    return problems


def normalize_value(value):
    if hasattr(value, "x") and hasattr(value, "y"):
        components = [round(float(value.x), 3), round(float(value.y), 3)]
        if hasattr(value, "z"):
            components.append(round(float(value.z), 3))
        return tuple(components)
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return round(float(value), 4)
    return getattr(value, "name", str(value))


def values_match(actual, expected):
    if isinstance(expected, str):
        return expected in str(actual)
    if isinstance(expected, bool):
        return actual is expected
    if isinstance(expected, tuple):
        return isinstance(actual, tuple) and len(actual) == len(expected) and all(abs(left - right) <= 1e-3 for left, right in zip(actual, expected))
    return abs(float(actual) - float(expected)) <= 1e-3


def read_back_config_values(effective_by_name):
    records = []
    for preset_name, property_name, expected in EXPECTED_CONFIG_VALUES:
        actual = normalize_value(effective_by_name[preset_name].get_editor_property(property_name))
        records.append({"preset": preset_name, "property": property_name, "expected": expected, "actual": actual, "status": "ok" if values_match(actual, expected) else "differs"})
    return records


def check_start_contract(stage, fluid_actor_class, baseline):
    problems = stage.find_missing_properties(unreal.TDFluidSimulationConfig(), CONFIG_REQUIRED_PROPERTIES)
    problems = [f"TDFluidSimulationConfig.{name}" for name in problems]
    problems += [f"TDFluidPresetAsset.{name}" for name in stage.find_missing_properties(unreal.TDFluidPresetAsset.get_default_object(), PRESET_REQUIRED_PROPERTIES)]
    problems += [f"TDFluidLiveActor.{name}" for name in stage.find_missing_properties(unreal.get_default_object(fluid_actor_class), ACTOR_REQUIRED_PROPERTIES)]
    problems += [f"기준 DA.{name}" for name in stage.find_missing_properties(baseline, VISUAL_PROPERTY_NAMES)]
    return problems


def configure_kind_preset(stage, spec, baseline):
    kind = getattr(unreal.TDFluidPresetKind, spec["kind"])
    preset, path = stage.load_or_create_preset(spec["name"])
    preset.set_editor_property("preset_kind", kind)
    preset.set_editor_property("default_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)
    preset.set_editor_property("override_preset_defaults", False)
    preset.set_editor_property("simulation_config", unreal.TDFluidPresetAsset.build_config_for_preset_kind(kind))
    for property_name in VISUAL_PROPERTY_NAMES:
        preset.set_editor_property(property_name, baseline.get_editor_property(property_name))
    stored_material = preset.get_editor_property("volume_material")
    baseline_material = baseline.get_editor_property("volume_material")
    if baseline_material is not None and (stored_material is None or stored_material.get_path_name() != baseline_material.get_path_name()):
        raise RuntimeError(f"프리셋 볼륨 재질 복사 확인 실패: {path}")
    stage.save_asset(preset, path)
    return preset, path, preset.resolve_effective_config()


def place_orbiting_stirrer(stage, actors, stirrer_class, center, label, created):
    location = unreal.Vector(center[0] + CAULDRON_ORBIT_RADIUS_CM, center[1], ACTOR_Z_CM)
    stirrer = actors.spawn_actor_from_class(stirrer_class, location, unreal.Rotator())
    stage.finish_actor(stirrer, label, STAGE_FOLDER + "/Cauldron", created)
    motion = {"pivot_translation": unreal.Vector(-CAULDRON_ORBIT_RADIUS_CM, 0.0, 0.0), "rotation_rate": unreal.Rotator(roll=0.0, pitch=0.0, yaw=CAULDRON_DEGREES_PER_SEC)}
    stage.set_component_properties(stirrer, unreal.RotatingMovementComponent, motion)
    return stirrer


def configure_fluid_actor(stage, actor, preset, effective):
    stage.configure_live_actor(actor, preset, effective)
    actor.set_editor_property("should_use_activation_volume", True)
    actor.set_editor_property("activation_distance_cm", ACTIVATION_DISTANCE_CM)
    actor.set_editor_property("interaction_brush_channel", unreal.TDFluidBrushChannel.CHANNEL_R_MUD)


def place_fluid_actor(stage, actors, fluid_actor_class, stirrer_class, spec, presets, created):
    center = zone_center(spec["zone"])
    location = unreal.Vector(center[0] + spec["offset"][0], center[1] + spec["offset"][1], ACTOR_Z_CM)
    actor = actors.spawn_actor_from_class(fluid_actor_class, location, unreal.Rotator())
    stage.finish_actor(actor, spec["label"] + "_Fluid", f"{STAGE_FOLDER}/Zone{spec['zone']}", created)
    preset, effective = presets[spec["preset"]]
    configure_fluid_actor(stage, actor, preset, effective)
    if spec["mount"] == "orbit":
        stirrer = place_orbiting_stirrer(stage, actors, stirrer_class, center, spec["label"] + "_Stirrer", created)
        keep_world = unreal.AttachmentRule.KEEP_WORLD
        actor.attach_to_actor(stirrer, unreal.Name(""), keep_world, keep_world, keep_world, False)
        if actor.get_attach_parent_actor() is None:
            raise RuntimeError(f"궤도 부모 붙이기 확인 실패: {spec['label']}")
        return actor, stirrer
    stirrer_center = (location.x, location.y)
    stirrer = stage.place_stirrer(actors, stirrer_class, stirrer_center, spec["label"] + "_Stirrer", f"{STAGE_FOLDER}/Zone{spec['zone']}", created)
    return actor, stirrer


def make_zone_record(zone, actor_records):
    center = zone_center(zone["index"])
    return {
        "label": zone["label"],
        "center": list(center),
        "metrics": zone["metrics"],
        "actors": [record for record in actor_records if record["zone"] == zone["index"]],
        "camera": {"pawn": [center[0], center[1]], "arm": zone["camera_arm_cm"], "view": list(CAMERA_VIEW)},
    }


def main():
    stage = load_particle_stage()
    editor = stage.require_editor_ready(REQUIRED_UNREAL_NAMES)
    spec_problems = find_actor_spec_problems()
    if spec_problems:
        raise RuntimeError("무대 배치 자체 점검 실패: " + " | ".join(spec_problems))
    fluid_actor_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    if fluid_actor_class is None:
        raise RuntimeError("유체 액터 클래스 없음(재빌드와 에디터 재시작 필요): " + FLUID_ACTOR_CLASS_PATH)
    baseline_path = preset_asset_path(BASELINE_PRESET_NAME)
    baseline = unreal.EditorAssetLibrary.load_asset(baseline_path) if unreal.EditorAssetLibrary.does_asset_exist(baseline_path) else None
    if baseline is None:
        raise RuntimeError(f"기준 프리셋 없음(editor_make_fluid_smoke_stage.py 먼저): {baseline_path}")
    contract_problems = check_start_contract(stage, fluid_actor_class, baseline)
    if contract_problems:
        raise RuntimeError("C++ 계약 속성 없음(FX-15a 통합·재빌드·에디터 재시작 확인): " + ", ".join(contract_problems))
    stirrer_class = unreal.EditorAssetLibrary.load_blueprint_class(stage.STIRRER_BLUEPRINT_PATH)
    if stirrer_class is None:
        raise RuntimeError("교반기 블루프린트 없음(editor_make_fluid_particle_stage.py 먼저): " + stage.STIRRER_BLUEPRINT_PATH)

    overwrite = stage.read_overwrite_request(REQUEST_RELATIVE_PATH)
    preset_paths = [preset_asset_path(spec["name"]) for spec in KIND_SPECS]
    stage.guard_existing_assets(preset_paths + [STAGE_MAP_PATH], overwrite, REQUEST_RELATIVE_PATH)

    print("단계: 프리셋·모양 정본 검사·다시 읽기")
    tuning_sha256, look_sha256 = stage.check_and_reload_preset_data()

    print("단계: 프리셋")
    presets = {BASELINE_PRESET_NAME: (baseline, baseline.resolve_effective_config())}
    for spec in KIND_SPECS:
        preset, _, effective = configure_kind_preset(stage, spec, baseline)
        presets[spec["name"]] = (preset, effective)
    config_records = read_back_config_values({name: pair[1] for name, pair in presets.items()})

    print("단계: 레벨")
    levels, actors = stage.open_empty_level(STAGE_MAP_PATH)
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(GAME_MODE_PATH))
    created = []
    floor_length_cm = (len(ZONES) - 1) * ZONE_SPACING_CM + 2.0 * FLOOR_MARGIN_CM
    floor_center = (0.5 * (len(ZONES) - 1) * ZONE_SPACING_CM, 0.0)
    stage.place_floor_and_lights(actors, created, STAGE_FOLDER, floor_center, (floor_length_cm / 100.0, FLOOR_WIDTH_CM / 100.0, 0.5))
    actor_records = []
    for spec in ACTOR_SPECS:
        actor, stirrer = place_fluid_actor(stage, actors, fluid_actor_class, stirrer_class, spec, presets, created)
        location = actor.get_actor_location()
        parent = actor.get_attach_parent_actor()
        actor_records.append({
            "label": actor.get_actor_label(), "zone": spec["zone"], "preset": f"DA_TDFluid_{spec['preset']}", "mount": spec["mount"],
            "location": [round(location.x, 3), round(location.y, 3), round(location.z, 3)],
            "stirrer": stirrer.get_actor_label(),
            "attach_parent": parent.get_actor_label() if parent is not None else None,
        })

    print("단계: 레벨 저장")
    if not levels.save_current_level():
        raise RuntimeError(f"무대 레벨 저장 실패: {STAGE_MAP_PATH}")
    stage.require_saved_files(preset_paths, STAGE_MAP_PATH)
    report = {
        "status": "ok",
        "preset_tuning_sha256": tuning_sha256,
        "material_look_sha256": look_sha256,
        "map": STAGE_MAP_PATH,
        "overwrite": overwrite,
        "baseline_preset": baseline_path,
        "presets": preset_paths,
        "zones": {str(zone["index"]): make_zone_record(zone, actor_records) for zone in ZONES},
        "config_values": config_records,
        "json_dependencies_differ": [record for record in config_records if record["status"] != "ok"],
        "cauldron_orbit": {"radius_cm": CAULDRON_ORBIT_RADIUS_CM, "degrees_per_sec": CAULDRON_DEGREES_PER_SEC},
        "deviations": ["구역 0 기준 DA 는 spec 의 DA_TDFluid_Smoke 가 없어 SmokeSoft", "교반기 궤도 반경·속도는 무대 상수 [제안]", "새 DA 3개의 볼륨 재질·메시·높이는 기준 DA 에서 복사(보이는 모양만, 값은 JSON 종류 값)"],
        "created_actors": len(created),
        "saved": True,
    }
    report_path = stage.write_report(report, REPORT_RELATIVE_PATH)
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
