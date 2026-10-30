"""실행 환경: TDGame 언리얼 에디터 안(W-6 밀집 물 C++ 통합·재시작 뒤, editor_make_fluid_dense_water_stage.py 로 무대 6개를 만든 다음). 요청의 라벨마다 그 무대 맵을 열고 물 액터의 BakeDenseWaterCache(CPU 256 이하 풀이를 simulated_seconds 만큼 돌려 수렴시킨 깊이·면 속도)로 캐시 DA 를 굽는다. 밀집 창은 고정이라 폰을 따라가지 않는 액터만 받는다(refute-w6 M4).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_bake_fluid_dense_water_cache.py (PowerShell, L-repo-01. 요청 Saved/FluidFX/fluid_dense_water_cache_request.json = {"labels": ["Creek", "Lake", "Sea", "Tank", "Compare"], "simulated_seconds": 90, "overwrite": true, "verify": false})
출력: /Game/FluidFX/DenseWaterCaches/DWC_TD_DenseWaterStage_<라벨>, 액터 dense_water_cache 지정·맵 저장, Saved/FluidFX/fluid_dense_water_cache.json(해상도·부피·젖음 칸·체크섬·창 일치·정본 판 대조). verify 참이면 임시 객체에 다시 구워 저장본과 체크섬·배열만 비교하고 아무것도 저장하지 않는다. 결과는 run_in_editor.py 출력 본문으로 판정한다(L-editor-73)
상태: 미검증(2026-10-29 파동 3 L3-S2 작성: py_compile·가짜 unreal 실행만, 에디터 실행 전). 라벨은 Creek 처럼 짧게도, TDGen_DenseCreek·TDGen_DenseCompareDense 처럼 길게도 받는다. 캐시 체크섬은 창·설정·구역·장애물만 덮고 지형은 빠지므로(spec-w6 위험 9) 무대를 다시 만들면 이 굽기를 다시 한다.
"""
import hashlib
import json
import math
import os

import unreal

CACHE_DIR = "/Game/FluidFX/DenseWaterCaches"
CACHE_NAME_PREFIX = "DWC_TD_DenseWaterStage_"
WATER_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidWaterActor"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_dense_water_cache_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_dense_water_cache.json")
STAGE_REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_dense_water_stage.json")
TUNING_RELATIVE_PATH = ("Content", "FluidFX", "PresetData", "TDFluidPresetTuning.json")
REQUIRED_KEYS = ("labels", "simulated_seconds")
MAX_SIMULATED_SECONDS = 3600.0
DEFAULT_WET_THRESHOLD_CM = 1.0
WINDOW_MATCH_TOLERANCE_CM = 1.0
LABEL_PREFIX = "TDGen_"
DENSE_WORD = "Dense"

REQUIRED_UNREAL_NAMES = (
    "TDFluidDenseWaterCacheAsset",
    "TDFluidWaterActor.bake_dense_water_cache",
    "DataAssetFactory",
    "AssetToolsHelpers",
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "LevelEditorSubsystem",
    "EditorActorSubsystem",
    "UnrealEditorSubsystem",
)
CACHE_FIELD_NAMES = (
    "resolution",
    "center_cm",
    "extent_cm",
    "terrain_reference_z_cm",
    "depth_cm",
    "face_velocity_x_cm_per_sec",
    "face_velocity_y_cm_per_sec",
    "simulated_seconds",
    "source_checksum",
    "source_level",
)
ARRAY_FIELD_NAMES = ("depth_cm", "face_velocity_x_cm_per_sec", "face_velocity_y_cm_per_sec")


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def is_finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def normalize_label(text):
    name = text[len(LABEL_PREFIX):] if text.startswith(LABEL_PREFIX) else text
    name = name[len(DENSE_WORD):] if name.startswith(DENSE_WORD) and len(name) > len(DENSE_WORD) else name
    return name[:-len(DENSE_WORD)] if name.endswith(DENSE_WORD) and len(name) > len(DENSE_WORD) else name


def validate_request(request):
    if not isinstance(request, dict):
        return None, ["요청이 JSON 객체가 아님"]
    problems = [f"키 없음: {key}" for key in REQUIRED_KEYS if key not in request]
    if problems:
        return None, problems
    labels = request["labels"]
    if not isinstance(labels, list) or not labels or not all(isinstance(label, str) and label for label in labels):
        problems.append("labels 는 비어 있지 않은 문자열 목록")
    seconds = request["simulated_seconds"]
    if not is_finite_number(seconds) or not 0.0 < seconds <= MAX_SIMULATED_SECONDS:
        problems.append(f"simulated_seconds 는 0 초과 {MAX_SIMULATED_SECONDS:.0f} 이하의 수")
    for key in ("overwrite", "verify"):
        if key in request and not isinstance(request[key], bool):
            problems.append(f"{key} 는 true/false")
    if problems:
        return None, problems
    normalized_labels = [normalize_label(label) for label in labels]
    if len(set(normalized_labels)) != len(normalized_labels):
        return None, ["labels 에 같은 라벨이 두 번 있음"]
    normalized = dict(request)
    normalized["labels"] = normalized_labels
    normalized.setdefault("overwrite", False)
    normalized.setdefault("verify", False)
    return normalized, []


def read_json(relative_path, description):
    path = project_path(*relative_path)
    if not os.path.isfile(path):
        raise RuntimeError(f"{description} 없음: {path}")
    with open(path, encoding="utf-8") as source_file:
        return json.load(source_file)


def file_sha256(relative_path):
    with open(project_path(*relative_path), "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def read_stage_records(requested_labels):
    stage_report = read_json(STAGE_REPORT_RELATIVE_PATH, "무대 보고(editor_make_fluid_dense_water_stage.py 먼저)")
    if not isinstance(stage_report, dict) or stage_report.get("status") != "ok":
        raise RuntimeError("무대 보고가 ok 가 아님: 무대를 다시 만드세요")
    records = {record["cache_label"]: record for record in stage_report.get("scenes", []) if record.get("cache_label")}
    missing = [label for label in requested_labels if label not in records]
    if missing:
        raise RuntimeError(f"무대 보고에 캐시 라벨 없음: {', '.join(missing)} (있는 라벨 {sorted(records)})")
    return stage_report, records


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


def find_unsaved_package_names():
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    return sorted({package.get_name() for package in dirty})


def ensure_directory(directory_path):
    if unreal.EditorAssetLibrary.does_directory_exist(directory_path):
        return
    if not unreal.EditorAssetLibrary.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def open_level(level_path):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not unreal.EditorAssetLibrary.does_asset_exist(level_path):
        raise RuntimeError(f"레벨 없음(무대 먼저): {level_path}")
    if not levels.load_level(level_path):
        raise RuntimeError(f"레벨 열기 실패: {level_path}")
    return levels


def find_water_actor(actor_label):
    water_class = unreal.load_class(None, WATER_ACTOR_CLASS_PATH)
    if water_class is None:
        raise RuntimeError("물 액터 클래스 없음(재빌드와 에디터 재시작 필요): " + WATER_ACTOR_CLASS_PATH)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    matches = [actor for actor in actors if actor.get_actor_label() == actor_label and unreal.MathLibrary.class_is_child_of(actor.get_class(), water_class)]
    if len(matches) != 1:
        raise RuntimeError(f"물 액터가 하나가 아님: {actor_label} ({len(matches)}개)")
    return matches[0]


def require_fixed_dense_actor(actor):
    if actor.get_editor_property("should_follow_player_pawn"):
        raise RuntimeError(f"밀집 창은 고정이라 폰을 따라가는 액터는 굽지 않는다(M4): {actor.get_actor_label()}")
    configuration = actor.get_editor_property("live_component").get_editor_property("simulation_config")
    if not configuration.get_editor_property("enable_dense_water"):
        raise RuntimeError(f"enable_dense_water 가 꺼진 액터: {actor.get_actor_label()} (프리셋 종류·정본 확인)")
    return configuration


def create_cache_asset(asset_name):
    ensure_directory(CACHE_DIR)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidDenseWaterCacheAsset)
    cache = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, CACHE_DIR, unreal.TDFluidDenseWaterCacheAsset, factory)
    if cache is None:
        raise RuntimeError(f"캐시 DA 생성 실패: {CACHE_DIR}/{asset_name}")
    return cache


def load_or_create_cache(asset_path, asset_name, overwrite):
    assets = unreal.EditorAssetLibrary
    if not assets.does_asset_exist(asset_path):
        return create_cache_asset(asset_name), True
    if not overwrite:
        raise RuntimeError(f"기존 캐시 보존: {asset_path} (overwrite 또는 verify)")
    cache = assets.load_asset(asset_path)
    if not isinstance(cache, unreal.TDFluidDenseWaterCacheAsset):
        raise RuntimeError(f"기존 에셋 형식 불일치: {asset_path}")
    return cache, False


def to_plain(value):
    if hasattr(value, "x") and hasattr(value, "y"):
        return [float(value.x), float(value.y)]
    if isinstance(value, (int, float, str, bool)) or value is None:
        return value
    try:
        return list(value)
    except TypeError:
        return str(value)


def read_cache_fields(cache):
    fields = {}
    for name in CACHE_FIELD_NAMES:
        try:
            fields[name] = to_plain(cache.get_editor_property(name))
        except Exception:
            fields[name] = None
    return fields


def read_wet_threshold_cm(configuration):
    try:
        value = float(configuration.get_editor_property("dense_water_wet_threshold_cm"))
    except Exception:
        return DEFAULT_WET_THRESHOLD_CM
    return value if value > 0.0 else DEFAULT_WET_THRESHOLD_CM


def summarize_cache(fields, wet_threshold_cm):
    resolution = fields["resolution"]
    depth = fields["depth_cm"]
    if not isinstance(resolution, int) or not depth:
        raise RuntimeError("캐시 필드를 읽지 못함(UPROPERTY 지정자 확인): resolution·depth_cm")
    if len(depth) != resolution * resolution:
        raise RuntimeError(f"깊이 배열 길이 {len(depth)} != 해상도² {resolution * resolution}")
    extent = fields["extent_cm"]
    cell_area = (extent[0] / resolution) * (extent[1] / resolution)
    return {"resolution": resolution, "extent_cm": extent, "center_cm": fields["center_cm"], "volume_cm3": float(sum(depth)) * cell_area,
            "wet_cells": sum(1 for value in depth if value > wet_threshold_cm), "wet_threshold_cm": wet_threshold_cm, "max_depth_cm": float(max(depth)),
            "face_velocity_x_length": len(fields["face_velocity_x_cm_per_sec"] or []), "face_velocity_y_length": len(fields["face_velocity_y_cm_per_sec"] or []),
            "simulated_seconds": fields["simulated_seconds"], "source_checksum": fields["source_checksum"], "terrain_reference_z_cm": fields["terrain_reference_z_cm"]}


def compare_window_with_actor(fields, configuration, actor):
    sim_extent = configuration.get_editor_property("sim_extent_cm")
    location = actor.get_actor_location()
    center, extent = fields["center_cm"], fields["extent_cm"]
    if center is None or extent is None:
        return {"matches": False, "reason": "center_cm·extent_cm 읽기 실패"}
    center_delta = max(abs(center[0] - location.x), abs(center[1] - location.y))
    extent_delta = max(abs(extent[0] - sim_extent.x), abs(extent[1] - sim_extent.y))
    return {"matches": center_delta <= WINDOW_MATCH_TOLERANCE_CM and extent_delta <= WINDOW_MATCH_TOLERANCE_CM, "center_delta_cm": center_delta, "extent_delta_cm": extent_delta}


def reference_text(value):
    if value is None:
        return ""
    resolved = value.get() if hasattr(value, "get") else value
    if resolved is not None and hasattr(resolved, "get_path_name"):
        return resolved.get_path_name()
    return str(value)


def assign_cache_to_actor(actor, cache, asset_name):
    actor.set_editor_property("dense_water_cache", cache)
    stored_text = reference_text(actor.get_editor_property("dense_water_cache"))
    if asset_name not in stored_text:
        raise RuntimeError(f"dense_water_cache 다시 읽기 불일치: {actor.get_actor_label()} -> {stored_text}")
    return stored_text


def call_bake(actor, cache, simulated_seconds):
    if not actor.bake_dense_water_cache(cache, float(simulated_seconds)):
        raise RuntimeError(f"BakeDenseWaterCache 가 실패를 돌려줌: {actor.get_actor_label()}")


def bake_label(label, record, request):
    asset_name = CACHE_NAME_PREFIX + label
    asset_path = f"{CACHE_DIR}/{asset_name}"
    levels = open_level(record["map"])
    actor = find_water_actor(record["label"])
    configuration = require_fixed_dense_actor(actor)
    wet_threshold_cm = read_wet_threshold_cm(configuration)
    cache, created = load_or_create_cache(asset_path, asset_name, request["overwrite"])
    call_bake(actor, cache, request["simulated_seconds"])
    fields = read_cache_fields(cache)
    if not fields["source_level"]:
        try:
            cache.set_editor_property("source_level", record["map"])
            fields["source_level"] = record["map"]
        except Exception:
            fields["source_level"] = None
    summary = summarize_cache(fields, wet_threshold_cm)
    if not unreal.EditorAssetLibrary.save_loaded_asset(cache, only_if_is_dirty=False):
        raise RuntimeError(f"캐시 저장 실패: {asset_path}")
    stored_reference = assign_cache_to_actor(actor, cache, asset_name)
    if not levels.save_current_level():
        raise RuntimeError(f"레벨 저장 실패: {record['map']}")
    return {"label": label, "mode": "bake", "asset": asset_path, "created": created, "map": record["map"], "actor": record["label"],
            "summary": summary, "window": compare_window_with_actor(fields, configuration, actor), "stored_reference": stored_reference, "saved": True}


def verify_label(label, record, request):
    asset_name = CACHE_NAME_PREFIX + label
    asset_path = f"{CACHE_DIR}/{asset_name}"
    saved = unreal.EditorAssetLibrary.load_asset(asset_path) if unreal.EditorAssetLibrary.does_asset_exist(asset_path) else None
    if not isinstance(saved, unreal.TDFluidDenseWaterCacheAsset):
        raise RuntimeError(f"비교할 저장 캐시 없음: {asset_path}")
    open_level(record["map"])
    actor = find_water_actor(record["label"])
    require_fixed_dense_actor(actor)
    transient = unreal.new_object(unreal.TDFluidDenseWaterCacheAsset)
    call_bake(actor, transient, request["simulated_seconds"])
    saved_fields, fresh_fields = read_cache_fields(saved), read_cache_fields(transient)
    differing_arrays = [name for name in ARRAY_FIELD_NAMES if saved_fields[name] != fresh_fields[name]]
    checksum_equal = saved_fields["source_checksum"] == fresh_fields["source_checksum"]
    return {"label": label, "mode": "verify", "asset": asset_path, "status": "ok" if checksum_equal and not differing_arrays else "differs",
            "saved_checksum": saved_fields["source_checksum"], "rebaked_checksum": fresh_fields["source_checksum"], "differing_arrays": differing_arrays, "saved": False}


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def main():
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음(W-6 통합 재빌드와 에디터 재시작 확인): " + ", ".join(missing_names))
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    request, problems = validate_request(read_json(REQUEST_RELATIVE_PATH, "요청 파일(키 " + ", ".join(REQUIRED_KEYS) + ")"))
    if problems:
        raise RuntimeError("요청 검사 실패: " + " | ".join(problems))
    stage_report, records = read_stage_records(request["labels"])
    unsaved_names = find_unsaved_package_names()
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))
    current_sha256 = file_sha256(TUNING_RELATIVE_PATH)
    warnings = []
    if stage_report.get("preset_tuning_sha256") != current_sha256:
        warnings.append("정본 JSON 이 무대 생성 뒤 바뀌었습니다: 무대를 다시 만들고 굽기를 다시 하세요")
    results = []
    for label in request["labels"]:
        print(f"단계: {'다시 굽기 비교(저장 안 함)' if request['verify'] else '굽기'} {label}")
        results.append(verify_label(label, records[label], request) if request["verify"] else bake_label(label, records[label], request))
    window_mismatches = [result["label"] for result in results if result["mode"] == "bake" and not result["window"]["matches"]]
    if window_mismatches:
        warnings.append("캐시 창이 액터 창과 다름(심기에서 거부될 수 있음): " + ", ".join(window_mismatches))
    differing = [result["label"] for result in results if result["mode"] == "verify" and result["status"] != "ok"]
    report = {"status": "differs" if differing else "ok", "mode": "verify" if request["verify"] else "bake", "request": request, "tuning_sha256": current_sha256,
              "stage_tuning_sha256": stage_report.get("preset_tuning_sha256"), "results": results, "differing_labels": differing, "warnings": warnings,
              "cache_load_expected": sum(records[label]["cache_load_expected"] for label in request["labels"])}
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        write_report({"status": "failed", "error": str(error)})
        raise


if __name__ == "__main__":
    run_with_failure_report()
