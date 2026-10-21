"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터 불필요. 나이아가라 정규화 구조 덤프(editor_make_fluid_ember_niagara.py 출력) 또는 Sim Cache JSON(FNiagaraSimCacheJson::DumpToFile) 두 개를 비교한다.
실행 명령: python -B Tools/FluidFX/compare_niagara_dumps.py --structure A.dump.json B.dump.json [--expect-diff 절,절] [--report 경로] | --simcache A.json B.json [--tolerance 0.01] [--position-tolerance 0.5] [--report 경로]
출력: 절별 sha256 같음/다름(구조) 또는 프레임·입자(UniqueID 짝)·속성별 최대 차이(시뮬 캐시) 줄과 마지막 줄 JSON 요약. 종료 0 = 같음(--expect-diff 면 다른 절 집합이 정확히 그 목록), 1 = 다름, 2 = 입력 오류
상태: --structure 검증됨(2026-10-21 FX-8c 12차 N0-a 에디터 산출물: ReproA=B=C 11절 전부 같음, ReproScale --expect-diff UserVariables 종료 0). --simcache 는 오프라인 fixture 만 통과(에디터 Sim Cache 미대조)."""
import argparse
import hashlib
import json
import math
import pathlib
import sys

SIMCACHE_HEADER_KEYS = ("SystemAsset", "CacheGuid", "StartSeconds", "DurationSeconds", "NumFrames", "NumEmitters")
SYSTEM_INSTANCE_KEY = "SystemInstance"
VALUE_ARRAY_KEYS = ("Floats", "Halfs", "Ints")
UNIQUE_ID_SUFFIX = "uniqueid"
POSITION_SUFFIX = "position"
MAX_LISTED_DIFFERENCES = 20


class TDDumpInputError(Exception):
    pass


def canonical_json(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"), allow_nan=True)


def hash_section(value):
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def hash_sections(sections):
    return {name: hash_section(sections[name]) for name in sorted(sections)}


def hash_all_sections(section_hashes):
    return hashlib.sha256(canonical_json(section_hashes).encode("utf-8")).hexdigest()


def load_json(path):
    file_path = pathlib.Path(path)
    if not file_path.is_file():
        raise TDDumpInputError(f"파일 없음: {path}")
    try:
        return json.loads(file_path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise TDDumpInputError(f"JSON 읽기 실패: {path}: {error}")


def load_structure_sections(path):
    dump = load_json(path)
    sections = dump.get("sections") if isinstance(dump, dict) else None
    if not isinstance(sections, dict) or not sections:
        raise TDDumpInputError(f"sections 절이 없음: {path}")
    return sections


def compare_structure(path_a, path_b, expected_names):
    hashes_a = hash_sections(load_structure_sections(path_a))
    hashes_b = hash_sections(load_structure_sections(path_b))
    rows = []
    for name in sorted(set(hashes_a) | set(hashes_b)):
        hash_a = hashes_a.get(name, "")
        hash_b = hashes_b.get(name, "")
        rows.append({"section": name, "same": hash_a == hash_b, "a": hash_a, "b": hash_b})
    differing = [row["section"] for row in rows if not row["same"]]
    if expected_names is None:
        passed = not differing
    else:
        passed = sorted(differing) == sorted(expected_names)
    return {
        "mode": "structure",
        "a": str(path_a),
        "b": str(path_b),
        "all_a": hash_all_sections(hashes_a),
        "all_b": hash_all_sections(hashes_b),
        "sections": rows,
        "differing": differing,
        "expected_differing": expected_names,
        "passed": passed,
    }


def read_simcache_emitters(cache, path):
    if not isinstance(cache, dict) or not isinstance(cache.get("NumFrames"), (int, float)):
        raise TDDumpInputError(f"Sim Cache 머리(NumFrames)가 없음: {path}")
    emitters = {key: value for key, value in cache.items() if key not in SIMCACHE_HEADER_KEYS}
    for name, frames in emitters.items():
        if not isinstance(frames, list) or any(not isinstance(frame, dict) or "NumInstances" not in frame for frame in frames):
            raise TDDumpInputError(f"Sim Cache 프레임 형식 오류: {path}: {name}")
    return emitters


def read_attribute_values(attribute):
    for key in VALUE_ARRAY_KEYS:
        if key in attribute:
            return key, attribute[key]
    return "", []


def find_unique_ids(frame):
    for attribute in frame.get("Attributes", []):
        if str(attribute.get("Name", "")).lower().endswith(UNIQUE_ID_SUFFIX):
            _, values = read_attribute_values(attribute)
            return [value[0] if isinstance(value, list) else value for value in values]
    return None


def flatten_value(value):
    return [float(item) for item in value] if isinstance(value, list) else [float(value)]


def value_distance(value_a, value_b):
    items_a = flatten_value(value_a)
    items_b = flatten_value(value_b)
    if len(items_a) != len(items_b):
        return math.inf
    distance = 0.0
    for item_a, item_b in zip(items_a, items_b):
        if math.isnan(item_a) and math.isnan(item_b):
            continue
        if math.isnan(item_a) or math.isnan(item_b):
            return math.inf
        distance = max(distance, abs(item_a - item_b))
    return distance


def tolerance_for(attribute_name, value_key, tolerance, position_tolerance):
    if value_key == "Ints":
        return 0.0
    if attribute_name.lower().endswith(POSITION_SUFFIX):
        return position_tolerance
    return tolerance


def pair_particles(frame_a, frame_b, count, emitter_name):
    if emitter_name == SYSTEM_INSTANCE_KEY:
        return [(index, index) for index in range(count)], ""
    ids_a = find_unique_ids(frame_a)
    ids_b = find_unique_ids(frame_b)
    if ids_a is None or ids_b is None:
        return None, "UniqueID 속성 없음"
    if len(set(ids_a)) != count or len(set(ids_b)) != count:
        return None, f"UniqueID 수 ≠ 입자 수(A {len(set(ids_a))}, B {len(set(ids_b))}, 입자 {count})"
    if set(ids_a) != set(ids_b):
        return None, "UniqueID 집합 다름"
    index_b = {unique_id: index for index, unique_id in enumerate(ids_b)}
    return [(index, index_b[unique_id]) for index, unique_id in enumerate(ids_a)], ""


def compare_frame(emitter_name, frame_index, frame_a, frame_b, tolerance, position_tolerance, max_differences, differences):
    count = int(frame_a["NumInstances"])
    if count != int(frame_b["NumInstances"]):
        differences.append(f"{emitter_name}[{frame_index}] 입자 수 {count} ≠ {int(frame_b['NumInstances'])}")
        return
    if count == 0:
        return
    pairs, pairing_problem = pair_particles(frame_a, frame_b, count, emitter_name)
    if pairs is None:
        differences.append(f"{emitter_name}[{frame_index}] {pairing_problem}")
        return
    attributes_a = {str(item.get("Name")): item for item in frame_a.get("Attributes", [])}
    attributes_b = {str(item.get("Name")): item for item in frame_b.get("Attributes", [])}
    if set(attributes_a) != set(attributes_b):
        differences.append(f"{emitter_name}[{frame_index}] 속성 집합 다름: {sorted(set(attributes_a) ^ set(attributes_b))}")
        return
    for name in sorted(attributes_a):
        key_a, values_a = read_attribute_values(attributes_a[name])
        key_b, values_b = read_attribute_values(attributes_b[name])
        if key_a != key_b or len(values_a) != count or len(values_b) != count:
            differences.append(f"{emitter_name}[{frame_index}].{name} 값 배열 형식 다름")
            continue
        limit = tolerance_for(name, key_a, tolerance, position_tolerance)
        worst = max((value_distance(values_a[index_a], values_b[index_b]) for index_a, index_b in pairs), default=0.0)
        label = f"{emitter_name}.{name}"
        max_differences[label] = max(max_differences.get(label, 0.0), worst)
        if worst > limit:
            differences.append(f"{emitter_name}[{frame_index}].{name} 최대 차이 {worst:.6g} > {limit:g}")


def compare_simcache(path_a, path_b, tolerance, position_tolerance):
    cache_a = load_json(path_a)
    cache_b = load_json(path_b)
    emitters_a = read_simcache_emitters(cache_a, path_a)
    emitters_b = read_simcache_emitters(cache_b, path_b)
    differences = []
    notes = []
    if cache_a.get("SystemAsset") != cache_b.get("SystemAsset"):
        notes.append(f"SystemAsset 다름: {cache_a.get('SystemAsset')} / {cache_b.get('SystemAsset')}")
    if int(cache_a["NumFrames"]) != int(cache_b["NumFrames"]):
        differences.append(f"프레임 수 {int(cache_a['NumFrames'])} ≠ {int(cache_b['NumFrames'])}")
    if set(emitters_a) != set(emitters_b):
        differences.append(f"이미터 집합 다름: {sorted(set(emitters_a) ^ set(emitters_b))}")
    max_differences = {}
    for emitter_name in sorted(set(emitters_a) & set(emitters_b)):
        frames_a = emitters_a[emitter_name]
        frames_b = emitters_b[emitter_name]
        if len(frames_a) != len(frames_b):
            differences.append(f"{emitter_name} 프레임 배열 길이 {len(frames_a)} ≠ {len(frames_b)}")
            continue
        for frame_index, (frame_a, frame_b) in enumerate(zip(frames_a, frames_b)):
            compare_frame(emitter_name, frame_index, frame_a, frame_b, tolerance, position_tolerance, max_differences, differences)
    return {
        "mode": "simcache",
        "a": str(path_a),
        "b": str(path_b),
        "tolerance": tolerance,
        "position_tolerance": position_tolerance,
        "frames": int(cache_a["NumFrames"]),
        "max_differences": {key: (value if math.isfinite(value) else "inf") for key, value in sorted(max_differences.items())},
        "difference_count": len(differences),
        "differences": differences[:MAX_LISTED_DIFFERENCES],
        "notes": notes,
        "passed": not differences,
    }


def print_structure(result):
    for row in result["sections"]:
        print(("SAME " if row["same"] else "DIFF ") + row["section"] + f" {row['a'][:12]} {row['b'][:12]}")


def print_simcache(result):
    for label, value in result["max_differences"].items():
        print(f"MAX {label} {value}")
    for line in result["differences"]:
        print("DIFF " + line)
    for line in result["notes"]:
        print("NOTE " + line)


def parse_arguments(argv):
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--structure", action="store_true")
    mode.add_argument("--simcache", action="store_true")
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--expect-diff", default=None)
    parser.add_argument("--tolerance", type=float, default=0.01)
    parser.add_argument("--position-tolerance", type=float, default=0.5)
    parser.add_argument("--report", default="")
    return parser.parse_args(argv)


def main(argv=None):
    try:
        arguments = parse_arguments(argv)
    except SystemExit:
        return 2
    try:
        if arguments.structure:
            expected = None if arguments.expect_diff is None else [name for name in arguments.expect_diff.split(",") if name]
            result = compare_structure(arguments.a, arguments.b, expected)
            print_structure(result)
        else:
            result = compare_simcache(arguments.a, arguments.b, arguments.tolerance, arguments.position_tolerance)
            print_simcache(result)
    except TDDumpInputError as error:
        print(json.dumps({"passed": False, "input_error": str(error)}, ensure_ascii=False))
        return 2
    if arguments.report:
        report_path = pathlib.Path(arguments.report)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    summary_keys = ("mode", "passed", "differing", "expected_differing", "difference_count", "all_a", "all_b")
    print(json.dumps({key: result[key] for key in summary_keys if key in result}, ensure_ascii=False))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
