"""실행 환경: TDGame 언리얼 에디터 안(FX-9 C++ UTDFluidHeightCacheAsset 통합·재시작 뒤). 요청 파일의 레벨을 열고(월드 파티션이면 액터를 먼저 불러온 뒤, 반박 S6) 지형 높이 캐시 DA 를 런타임과 같은 트레이스 규칙(BakeFromLineTraces)으로 굽는다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_bake_fluid_height_cache.py (요청 Saved/FluidFX/fluid_height_cache_request.json = {"level","name","center":[x,y],"extent":[x,y],"texel_cm":100,"top_z","bottom_z","overwrite":false,"verify":false,"assign_actor":"TDGen_HvolSlope"(선택)})
출력: /Game/FluidFX/HeightCaches/HC_TD_<레벨에서 LV_ 뺀 이름>_<name>, Saved/FluidFX/fluid_height_cache.json(체크섬·해상도·미스 칸 수·텍셀 중심 트레이스 대 해독 차이·양자화 한계·지정 액터). verify=true 면 임시 객체에 다시 구워 저장본 체크섬·배열만 비교하고 아무것도 저장하지 않는다(MT O6 risk)
상태: 2026-10-27 에디터 실행 통과(파동 2 A5-b: LV_TDFluidHvolStage 구역 4, 10×10, 트레이스 차 ≤ 양자화 한계, assign_actor 다시 읽기 일치). 액터 클래스 판정은 unreal.Class 에 is_child_of 가 없어 MathLibrary.class_is_child_of 를 쓴다. 순환(cross-plan A-02) 해소: 6d 무대를 먼저 만들고 이 스크립트가 assign_actor 로 구역 4 액터 TerrainHeightCache 를 지정·저장한다(proposed). 비교 트레이스는 WorldStatic 객체 질의, 무시 = 폰·유체 액터·태그 TDFluidTraceIgnore·TDFluidWaterSurface·TDFluidHeightCacheIgnore(반박 S1, 런타임 무시 집합은 묶음 C 정본을 따른다).
"""
import json
import math
import os
import re

import unreal


FLUID_ROOT = "/Game/FluidFX"
CACHE_DIR = f"{FLUID_ROOT}/HeightCaches"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_height_cache_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_height_cache.json")
FLUID_ACTOR_CLASS_PATH = "/Script/TDGame.TDFluidLiveActor"
IGNORE_TAGS = ("TDFluidTraceIgnore", "TDFluidWaterSurface", "TDFluidHeightCacheIgnore")
ENCODED_MISS = 0
ENCODED_MAX = 65535
MIN_TEXEL_CM = 10.0
MAX_TEXEL_CM = 1000.0
MAX_RESOLUTION = 4096
TRACE_SAMPLE_LIMIT = 256
NAME_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9]*$")
REQUIRED_KEYS = ("level", "name", "center", "extent", "texel_cm", "top_z", "bottom_z")

REQUIRED_UNREAL_NAMES = (
    "TDFluidHeightCacheAsset",
    "TDFluidHeightCacheAsset.bake_from_line_traces",
    "DataAssetFactory",
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "SystemLibrary.line_trace_single_for_objects",
    "ObjectTypeQuery.OBJECT_TYPE_QUERY1",
    "DrawDebugTrace.NONE",
    "Pawn",
)


def number_list(value, count):
    return isinstance(value, (list, tuple)) and len(value) == count and all(isinstance(item, (int, float)) and not isinstance(item, bool) and math.isfinite(item) for item in value)


def resolution_for(extent_cm, texel_cm):
    return int(math.ceil(extent_cm / texel_cm - 1e-9))


def validate_request(request):
    if not isinstance(request, dict):
        return None, ["요청이 JSON 객체가 아님"]
    problems = [f"키 없음: {key}" for key in REQUIRED_KEYS if key not in request]
    if problems:
        return None, problems
    if not isinstance(request["level"], str) or not request["level"].startswith("/Game/") or "." in request["level"]:
        problems.append(f"level 은 /Game/ 패키지 경로: {request['level']}")
    if not isinstance(request["name"], str) or not NAME_PATTERN.match(request["name"]):
        problems.append(f"name 은 영문·숫자: {request['name']}")
    if not number_list(request["center"], 2):
        problems.append("center 는 숫자 2개")
    if not number_list(request["extent"], 2) or (number_list(request["extent"], 2) and min(request["extent"]) <= 0.0):
        problems.append("extent 는 양수 2개")
    texel = request["texel_cm"]
    if not isinstance(texel, (int, float)) or isinstance(texel, bool) or not MIN_TEXEL_CM <= texel <= MAX_TEXEL_CM:
        problems.append(f"texel_cm 는 {MIN_TEXEL_CM}~{MAX_TEXEL_CM}")
    for key in ("top_z", "bottom_z"):
        if not isinstance(request[key], (int, float)) or isinstance(request[key], bool) or not math.isfinite(request[key]):
            problems.append(f"{key} 는 유한한 숫자")
    for key in ("overwrite", "verify"):
        if key in request and not isinstance(request[key], bool):
            problems.append(f"{key} 는 true/false")
    if "assign_actor" in request and not (isinstance(request["assign_actor"], str) and request["assign_actor"]):
        problems.append("assign_actor 는 빈 문자열이 아닌 액터 이름")
    if problems:
        return None, problems
    if request["top_z"] <= request["bottom_z"]:
        problems.append("top_z 가 bottom_z 보다 커야 함")
    resolution = [resolution_for(value, texel) for value in request["extent"]]
    if max(resolution) > MAX_RESOLUTION:
        problems.append(f"해상도 {resolution} > {MAX_RESOLUTION}")
    if problems:
        return None, problems
    normalized = dict(request)
    normalized.setdefault("overwrite", False)
    normalized.setdefault("verify", False)
    normalized["resolution"] = resolution
    return normalized, []


def cache_asset_name(level_path, name):
    level_name = level_path.rsplit("/", 1)[-1]
    if level_name.startswith("LV_"):
        level_name = level_name[3:]
    return f"HC_TD_{level_name}_{name}"


def decode_height(value, min_z_cm, range_cm):
    if value == ENCODED_MISS:
        return None
    return min_z_cm + (value - 1) * range_cm / (ENCODED_MAX - 1)


def encode_height(z_cm, min_z_cm, range_cm):
    if z_cm is None:
        return ENCODED_MISS
    ratio = (z_cm - min_z_cm) / max(range_cm, 1e-6)
    return int(min(max(round(ratio * (ENCODED_MAX - 1)) + 1, 1), ENCODED_MAX))


def quantization_bound_cm(range_cm):
    return range_cm / (2.0 * (ENCODED_MAX - 1))


def texel_center(origin_cm, index, texel_cm):
    return origin_cm + (index + 0.5) * texel_cm


def sample_indices(resolution, limit=TRACE_SAMPLE_LIMIT):
    stride = max(1, int(math.ceil(math.sqrt(resolution[0] * resolution[1] / max(limit, 1)))))
    return [(x, y) for y in range(stride // 2, resolution[1], stride) for x in range(stride // 2, resolution[0], stride)][:limit]


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


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


def read_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        raise RuntimeError(f"요청 파일 없음: {request_path} (키 {', '.join(REQUIRED_KEYS)})")
    with open(request_path, encoding="utf-8") as request_file:
        return json.load(request_file)


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
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None or world.get_outermost().get_name() != level_path:
        if not unreal.EditorAssetLibrary.does_asset_exist(level_path):
            raise RuntimeError(f"레벨 없음(6d 무대 먼저): {level_path}")
        if not levels.load_level(level_path):
            raise RuntimeError(f"레벨 열기 실패: {level_path}")
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def load_partition_actors():
    library = getattr(unreal, "WorldPartitionBlueprintLibrary", None)
    if library is None:
        return {"world_partition": False}
    try:
        descs = library.get_actor_descs()
    except Exception:
        return {"world_partition": False}
    if not descs:
        return {"world_partition": False}
    guids = [desc.guid for desc in descs if desc.native_class is None or "HLOD" not in desc.native_class.get_name()]
    if guids:
        library.load_actors(guids)
    return {"world_partition": True, "loaded_actor_descs": len(guids), "total_actor_descs": len(descs)}


def collect_ignored_actors(world):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    fluid_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    ignored = []
    for actor in actors:
        tags = {str(tag) for tag in actor.tags}
        if isinstance(actor, unreal.Pawn) or (fluid_class is not None and unreal.MathLibrary.class_is_child_of(actor.get_class(), fluid_class)) or tags & set(IGNORE_TAGS):
            ignored.append(actor)
    return ignored


def call_bake(cache, world, request):
    center, extent = request["center"], request["extent"]
    arguments = (float(request["texel_cm"]), float(request["top_z"]), float(request["bottom_z"]))
    for vector_type in (unreal.Vector2D, unreal.Vector):
        try:
            if vector_type is unreal.Vector:
                return cache.bake_from_line_traces(world, unreal.Vector(center[0], center[1], 0.0), unreal.Vector(extent[0], extent[1], 0.0), *arguments)
            return cache.bake_from_line_traces(world, unreal.Vector2D(center[0], center[1]), unreal.Vector2D(extent[0], extent[1]), *arguments)
        except TypeError as error:
            last_error = error
    raise RuntimeError(f"BakeFromLineTraces 인수 형식 불일치(묶음 C 서명 확인): {last_error}")


def read_cache_fields(cache):
    fields = {}
    for name in ("origin_cm", "resolution", "texel_cm", "encoding_min_z_cm", "encoding_range_cm", "source_checksum", "encoded_heights"):
        try:
            fields[name] = cache.get_editor_property(name)
        except Exception:
            fields[name] = None
    resolution = fields["resolution"]
    if resolution is not None and hasattr(resolution, "x"):
        fields["resolution"] = [int(resolution.x), int(resolution.y)]
    elif isinstance(resolution, int):
        fields["resolution"] = [resolution, resolution]
    origin = fields["origin_cm"]
    if origin is not None and hasattr(origin, "x"):
        fields["origin_cm"] = [float(origin.x), float(origin.y)]
    if fields["encoded_heights"] is not None:
        fields["encoded_heights"] = [int(value) for value in fields["encoded_heights"]]
    return fields


def trace_height(world, x, y, top_z, bottom_z, ignored):
    hit = unreal.SystemLibrary.line_trace_single_for_objects(world, unreal.Vector(x, y, top_z), unreal.Vector(x, y, bottom_z), [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], False, ignored, unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return None
    values = hit.to_tuple()
    return float(values[5].z) if values[0] else None


def compare_with_traces(world, fields, request, ignored):
    resolution, origin, heights = fields["resolution"], fields["origin_cm"], fields["encoded_heights"]
    if not resolution or origin is None or heights is None or fields["encoding_range_cm"] is None:
        return {"status": "fields_unreadable", "note": "EncodedHeights 등 UPROPERTY 를 Python 에서 읽지 못함(묶음 C 지정자 확인)"}
    texel = float(fields["texel_cm"] or request["texel_cm"])
    min_z, range_cm = float(fields["encoding_min_z_cm"]), float(fields["encoding_range_cm"])
    differences, miss_mismatch = [], 0
    for x_index, y_index in sample_indices(resolution):
        decoded = decode_height(heights[y_index * resolution[0] + x_index], min_z, range_cm)
        traced = trace_height(world, texel_center(origin[0], x_index, texel), texel_center(origin[1], y_index, texel), request["top_z"], request["bottom_z"], ignored)
        if (decoded is None) != (traced is None):
            miss_mismatch += 1
            continue
        if decoded is not None:
            differences.append(abs(decoded - traced))
    bound = quantization_bound_cm(range_cm)
    worst = max(differences) if differences else 0.0
    return {
        "status": "ok" if miss_mismatch == 0 and worst <= bound + 0.01 else "differs",
        "samples": len(differences) + miss_mismatch,
        "max_difference_cm": worst,
        "mean_difference_cm": sum(differences) / len(differences) if differences else 0.0,
        "miss_mismatch": miss_mismatch,
        "quantization_bound_cm": bound,
        "miss_texels": sum(1 for value in heights if value == ENCODED_MISS),
    }


def load_or_create_cache(asset_path, asset_name, overwrite):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(asset_path):
        if not overwrite:
            raise RuntimeError(f"기존 캐시 보존: {asset_path} (overwrite 또는 verify)")
        cache = assets.load_asset(asset_path)
        if not isinstance(cache, unreal.TDFluidHeightCacheAsset):
            raise RuntimeError(f"기존 에셋 형식 불일치: {asset_path}")
        return cache, False
    ensure_directory(CACHE_DIR)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDFluidHeightCacheAsset)
    cache = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, CACHE_DIR, unreal.TDFluidHeightCacheAsset, factory)
    if cache is None:
        raise RuntimeError(f"캐시 DA 생성 실패: {asset_path}")
    return cache, True


def assign_cache_to_actor(label, cache):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    fluid_class = unreal.load_class(None, FLUID_ACTOR_CLASS_PATH)
    matches = [actor for actor in actors if actor.get_actor_label() == label]
    if len(matches) != 1 or fluid_class is None or not unreal.MathLibrary.class_is_child_of(matches[0].get_class(), fluid_class):
        raise RuntimeError(f"assign_actor 대상 유체 액터가 하나가 아님: {label} ({len(matches)}개)")
    matches[0].set_editor_property("terrain_height_cache", cache)
    stored = matches[0].get_editor_property("terrain_height_cache")
    stored_path = stored.get_path_name() if hasattr(stored, "get_path_name") else (str(stored.export_text()) if hasattr(stored, "export_text") else str(stored))
    if stored is None or cache.get_path_name() not in stored_path:
        raise RuntimeError(f"TerrainHeightCache 다시 읽기 불일치: {label}")
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("레벨 저장 실패(assign_actor)")
    return label


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def main():
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음(FX-9 묶음 C 통합·재시작 확인): " + ", ".join(missing_names))
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    request, problems = validate_request(read_request())
    if problems:
        raise RuntimeError("요청 검사 실패: " + " | ".join(problems))
    unsaved_names = find_unsaved_package_names()
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 레벨·월드 파티션")
    world = open_level(request["level"])
    partition = load_partition_actors()
    ignored = collect_ignored_actors(world)
    asset_name = cache_asset_name(request["level"], request["name"])
    asset_path = f"{CACHE_DIR}/{asset_name}"

    if request["verify"]:
        print("단계: 다시 굽기 비교(저장 안 함)")
        saved = unreal.EditorAssetLibrary.load_asset(asset_path)
        if not isinstance(saved, unreal.TDFluidHeightCacheAsset):
            raise RuntimeError(f"비교할 저장 캐시 없음: {asset_path}")
        transient = unreal.new_object(unreal.TDFluidHeightCacheAsset)
        call_bake(transient, world, request)
        saved_fields, fresh_fields = read_cache_fields(saved), read_cache_fields(transient)
        report = {
            "status": "ok" if saved_fields["source_checksum"] == fresh_fields["source_checksum"] else "differs",
            "mode": "verify",
            "asset": asset_path,
            "saved_checksum": saved_fields["source_checksum"],
            "rebaked_checksum": fresh_fields["source_checksum"],
            "arrays_equal": saved_fields["encoded_heights"] == fresh_fields["encoded_heights"] if saved_fields["encoded_heights"] is not None else None,
            "partition": partition,
            "saved": False,
        }
        print(json.dumps(report, ensure_ascii=False, default=str))
        print("보고서: " + write_report(report))
        return

    print("단계: 굽기")
    cache, created = load_or_create_cache(asset_path, asset_name, request["overwrite"])
    bake_result = call_bake(cache, world, request)
    if bake_result is False:
        raise RuntimeError("BakeFromLineTraces 가 실패를 돌려줌")
    fields = read_cache_fields(cache)
    if not unreal.EditorAssetLibrary.save_loaded_asset(cache, only_if_is_dirty=False):
        raise RuntimeError(f"캐시 저장 실패: {asset_path}")
    trace_comparison = compare_with_traces(world, fields, request, ignored)
    assigned = assign_cache_to_actor(request["assign_actor"], cache) if request.get("assign_actor") else ""
    report = {
        "status": "ok",
        "mode": "bake",
        "asset": asset_path,
        "created": created,
        "request": request,
        "partition": partition,
        "ignored_actor_count": len(ignored),
        "ignore_tags": list(IGNORE_TAGS),
        "resolution": fields["resolution"],
        "origin_cm": fields["origin_cm"],
        "source_checksum": fields["source_checksum"],
        "trace_comparison": trace_comparison,
        "assigned_actor": assigned,
        "saved": True,
    }
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + write_report(report))


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        write_report({"status": "failed", "error": str(error)})
        raise


if __name__ == "__main__":
    run_with_failure_report()
