"""실행 환경: 시스템 Python 3, 에디터·빌드 불필요
실행 명령: python Tools/FluidFX/check_live2_coverage.py [--coverage <대응표 JSON>] [--roadmap <roadmap-final.md>] [--stage <단계 ID>]
출력: 표준 출력(FAIL 줄, 원천별 개수, 로드맵 2절 행 ID 집합 대조(기본 로드맵이 없을 때만 건너뜀), 대응 없는 항목, 행별 대응 개수 또는 --stage 단계 범위 행의 항목, 마지막 SUMMARY 줄). 종료 코드 0 실패 없음 / 1 실패 / 2 입력 오류
상태: 현행(FX-7d, 2026-10-19). 대응표 Docs/FluidFX/live2_coverage.json 은 LIVE-2 설명서 항목의 ID·짧은 이름만 둔다(D-49, P-35)
"""
import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_COVERAGE_PATH = PROJECT_ROOT / "Docs" / "FluidFX" / "live2_coverage.json"
DEFAULT_ROADMAP_PATH = PROJECT_ROOT / "Saved" / "AgentOps" / "20261019" / "fluidfx" / "parity2" / "roadmap-final.md"
EXPECTED_ITEM_COUNTS = {"setup": 304, "parameter": 333, "actor": 24}
TOP_LEVEL_KEYS = {"rows", "items"}
ROW_KEYS = {"id", "name"}
ITEM_KEYS = {"source", "id", "name", "row", "not_applicable"}
MAX_TEXT_LENGTH = 80
TABLE_SECTION_HEADING = "## 2. LIVE-2 전 영역 대조표"
TABLE_GROUP_HEADING = re.compile(r"### ([A-Z])\. ")
TABLE_ROW_ID = re.compile(r"[A-Z]\d+[a-z]?")
STAGE_LIST_HEADING = "### 3.2 단계 목록"
STAGE_ID_COLUMN = 1
STAGE_SCOPE_COLUMN = 3
ROW_ID_RANGE = re.compile(r"(?<![A-Za-z0-9_-])([A-T])(\d+)~\1?(\d+)(?![A-Za-z0-9_])")
ROW_ID_TOKEN = re.compile(r"(?<![A-Za-z0-9_-])([A-T]\d+[ab]?)(?![A-Za-z0-9_])")


class CoverageInputError(Exception):
    pass


def read_json_file(path):
    if not path.is_file():
        raise CoverageInputError(f"대응표 파일 없음: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise CoverageInputError(f"JSON 해석 실패: {path}: {error}") from error


def is_filled_text(value):
    return isinstance(value, str) and value.strip() != ""


def find_text_failures(label, field_name, value):
    if not is_filled_text(value):
        return [f"{label}: {field_name} 이 비었거나 문자열이 아님"]
    if len(value) > MAX_TEXT_LENGTH:
        return [f"{label}: {field_name} 이 {len(value)}자(상한 {MAX_TEXT_LENGTH}자, 설명문 유입 의심)"]
    return []


def find_key_failures(label, entry, allowed_keys):
    if not isinstance(entry, dict):
        return [f"{label}: 객체가 아님"]
    failures = [f"{label}: 허용되지 않은 키 '{key}'" for key in sorted(set(entry) - allowed_keys)]
    failures += [f"{label}: 키 '{key}' 없음" for key in sorted(allowed_keys - set(entry))]
    return failures


def is_usable_row(row):
    return isinstance(row, dict) and ROW_KEYS <= set(row) and all(isinstance(row[field_name], str) for field_name in ROW_KEYS)


def is_usable_item(item):
    if not isinstance(item, dict) or not ITEM_KEYS <= set(item):
        return False
    if not all(isinstance(item[field_name], str) for field_name in ("source", "id", "name")):
        return False
    return all(item[field_name] is None or isinstance(item[field_name], str) for field_name in ("row", "not_applicable"))


def find_row_failures(rows):
    failures = []
    row_ids = set()
    for index, row in enumerate(rows):
        label = f"rows[{index}]"
        key_failures = find_key_failures(label, row, ROW_KEYS)
        failures += key_failures
        if not is_usable_row(row):
            failures += [] if key_failures else [f"{label}: id·name 이 문자열이 아님"]
            continue
        label = f"행 {row['id']}"
        failures += find_text_failures(label, "id", row["id"])
        failures += find_text_failures(label, "name", row["name"])
        if row["id"] in row_ids:
            failures.append(f"{label}: 행 ID 중복")
        row_ids.add(row["id"])
    return failures, row_ids


def find_mapping_failures(label, item, row_ids):
    has_row = item["row"] is not None
    has_reason = item["not_applicable"] is not None
    if has_row and has_reason:
        return [f"{label}: row 와 not_applicable 이 둘 다 채워짐"]
    if not has_row and not has_reason:
        return [f"{label}: 대응 없음(row·not_applicable 둘 다 비어 있음)"]
    if has_reason:
        return find_text_failures(label, "not_applicable", item["not_applicable"])
    if item["row"] not in row_ids:
        return [f"{label}: 행 ID '{item['row']}' 이 rows 에 없음"]
    return []


def find_item_failures(items, row_ids):
    failures = []
    seen_item_keys = set()
    for index, item in enumerate(items):
        label = f"items[{index}]"
        key_failures = find_key_failures(label, item, ITEM_KEYS)
        failures += key_failures
        if not is_usable_item(item):
            failures += [] if key_failures else [f"{label}: source·id·name 은 문자열, row·not_applicable 은 문자열 또는 null 이어야 함"]
            continue
        label = f"{item['source']} {item['id']}"
        if item["source"] not in EXPECTED_ITEM_COUNTS:
            failures.append(f"{label}: 원천 '{item['source']}' 은 허용 값이 아님")
        failures += find_text_failures(label, "id", item["id"])
        failures += find_text_failures(label, "name", item["name"])
        if (item["source"], item["id"]) in seen_item_keys:
            failures.append(f"{label}: 항목 ID 중복")
        seen_item_keys.add((item["source"], item["id"]))
        failures += find_mapping_failures(label, item, row_ids)
    return failures


def count_items_by_source(items):
    return Counter(item["source"] for item in items)


def find_count_failures(source_counts):
    return [
        f"원천 {source}: 항목 {source_counts.get(source, 0)}개(기대 {expected_count}개)"
        for source, expected_count in EXPECTED_ITEM_COUNTS.items()
        if source_counts.get(source, 0) != expected_count
    ]


def validate_coverage(coverage):
    if not isinstance(coverage, dict):
        return ["최상위가 객체가 아님"], [], []
    failures = [f"최상위: 허용되지 않은 키 '{key}'" for key in sorted(set(coverage) - TOP_LEVEL_KEYS)]
    rows = coverage.get("rows")
    items = coverage.get("items")
    if not isinstance(rows, list) or not isinstance(items, list):
        return failures + ["최상위: rows·items 가 배열이 아님"], [], []
    row_failures, row_ids = find_row_failures(rows)
    failures += row_failures
    failures += find_item_failures(items, row_ids)
    usable_items = [item for item in items if is_usable_item(item)]
    failures += find_count_failures(count_items_by_source(usable_items))
    return failures, [row for row in rows if is_usable_row(row)], usable_items


def read_roadmap_lines(roadmap_path):
    if not roadmap_path.is_file():
        raise CoverageInputError(f"로드맵 파일 없음: {roadmap_path}")
    return roadmap_path.read_text(encoding="utf-8").splitlines()


def read_table_row_ids(roadmap_lines, roadmap_path):
    if TABLE_SECTION_HEADING not in roadmap_lines:
        raise CoverageInputError(f"로드맵에 '{TABLE_SECTION_HEADING}' 절이 없음: {roadmap_path}")
    table_row_ids = []
    group_letters = []
    for line in roadmap_lines[roadmap_lines.index(TABLE_SECTION_HEADING) + 1:]:
        if line.startswith("## "):
            break
        group_match = TABLE_GROUP_HEADING.match(line)
        if group_match:
            group_letters.append(group_match.group(1))
            continue
        cells = line.split("|")
        if line.startswith("|") and len(cells) > 2 and TABLE_ROW_ID.fullmatch(cells[1].strip()):
            table_row_ids.append(cells[1].strip())
    if not table_row_ids:
        raise CoverageInputError(f"로드맵 '{TABLE_SECTION_HEADING}' 절에서 표 행 ID 를 찾지 못함: {roadmap_path}")
    letters_with_rows = {row_id[0] for row_id in table_row_ids}
    return table_row_ids + [letter for letter in group_letters if letter not in letters_with_rows]


def find_row_set_failures(row_ids, table_row_ids):
    missing_row_ids = [row_id for row_id in dict.fromkeys(table_row_ids) if row_id not in row_ids]
    extra_row_ids = [row_id for row_id in dict.fromkeys(row_ids) if row_id not in table_row_ids]
    return (
        [f"행 집합: 로드맵 2절 행 '{row_id}' 이 rows 에 없음" for row_id in missing_row_ids]
        + [f"행 집합: rows 의 행 '{row_id}' 이 로드맵 2절에 없음" for row_id in extra_row_ids]
    )


def read_stage_scopes(roadmap_lines, roadmap_path):
    if STAGE_LIST_HEADING not in roadmap_lines:
        raise CoverageInputError(f"로드맵에 '{STAGE_LIST_HEADING}' 절이 없음: {roadmap_path}")
    stage_scopes = {}
    for line in roadmap_lines[roadmap_lines.index(STAGE_LIST_HEADING) + 1:]:
        if line.startswith("###"):
            break
        cells = line.split("|")
        if not line.startswith("|") or len(cells) <= STAGE_SCOPE_COLUMN:
            continue
        stage_id = cells[STAGE_ID_COLUMN].strip()
        if stage_id in ("ID", "") or set(stage_id) <= {"-"}:
            continue
        stage_scopes[stage_id] = cells[STAGE_SCOPE_COLUMN].strip()
    return stage_scopes


def expand_scope_row_ids(scope_text):
    row_ids = []
    for match in ROW_ID_RANGE.finditer(scope_text):
        letter, first_number, last_number = match.group(1), int(match.group(2)), int(match.group(3))
        row_ids += [f"{letter}{number}" for number in range(first_number, last_number + 1)]
    row_ids += ROW_ID_TOKEN.findall(ROW_ID_RANGE.sub(" ", scope_text))
    return list(dict.fromkeys(row_ids))


def format_item(item):
    return f"    {item['source']} {item['id']} | {item['name']}"


def print_row_counts(rows, items):
    mapped_counts = Counter(item["row"] for item in items if item["row"] is not None)
    print("행별 대응 개수:")
    for row in rows:
        print(f"  {row['id']} {row['name']}: {mapped_counts.get(row['id'], 0)}")


def print_stage_items(stage_id, scope_text, rows, items):
    row_names = {row["id"]: row["name"] for row in rows}
    scope_row_ids = expand_scope_row_ids(scope_text)
    known_row_ids = [row_id for row_id in scope_row_ids if row_id in row_names]
    unknown_tokens = [row_id for row_id in scope_row_ids if row_id not in row_names]
    print(f"단계 {stage_id} 범위 문구: {scope_text}")
    print(f"범위 행 {len(known_row_ids)}개: {', '.join(known_row_ids) if known_row_ids else '없음(행 ID 미기재)'}")
    if unknown_tokens:
        print(f"대조표에 없는 범위 토큰(무시): {', '.join(unknown_tokens)}")
    stage_item_count = 0
    for row_id in known_row_ids:
        row_items = [item for item in items if item["row"] == row_id]
        stage_item_count += len(row_items)
        print(f"  {row_id} {row_names[row_id]}: {len(row_items)}")
        for item in row_items:
            print(format_item(item))
    print(f"단계 {stage_id} 대응 항목 {stage_item_count}개")


def print_report(coverage_path, row_set_note, failures, rows, items):
    source_counts = count_items_by_source(items)
    unmapped_items = [item for item in items if item["row"] is None and item["not_applicable"] is None]
    not_applicable_items = [item for item in items if item["row"] is None and item["not_applicable"] is not None]
    print(f"대응표: {coverage_path}")
    print(f"행 {len(rows)}개, 항목 {len(items)}개")
    print(f"행 집합 대조: {row_set_note}")
    print("원천별 개수: " + ", ".join(f"{source} {source_counts.get(source, 0)}/{expected}" for source, expected in EXPECTED_ITEM_COUNTS.items()))
    for failure in failures:
        print(f"FAIL {failure}")
    print(f"대응 없는 항목 {len(unmapped_items)}건")
    for item in unmapped_items:
        print(format_item(item))
    print(f"해당 없음 처리 {len(not_applicable_items)}건")
    for item in not_applicable_items:
        print(f"{format_item(item)} | {item['not_applicable']}")
    return unmapped_items, not_applicable_items


def is_roadmap_required(arguments):
    return arguments.roadmap is not None or arguments.stage is not None


def run_check(arguments):
    coverage_path = Path(arguments.coverage)
    roadmap_path = Path(arguments.roadmap) if arguments.roadmap is not None else DEFAULT_ROADMAP_PATH
    failures, rows, items = validate_coverage(read_json_file(coverage_path))
    roadmap_lines = None
    row_set_state = "skipped"
    row_set_note = f"건너뜀(기본 로드맵 없음: {roadmap_path})"
    if is_roadmap_required(arguments) or roadmap_path.is_file():
        roadmap_lines = read_roadmap_lines(roadmap_path)
        table_row_ids = read_table_row_ids(roadmap_lines, roadmap_path)
        failures += find_row_set_failures([row["id"] for row in rows], table_row_ids)
        row_set_state = "checked"
        row_set_note = f"로드맵 2절 행 {len(table_row_ids)}개와 비교({roadmap_path})"
    stage_scope = None
    if arguments.stage:
        stage_scopes = read_stage_scopes(roadmap_lines, roadmap_path)
        if arguments.stage not in stage_scopes:
            raise CoverageInputError(f"단계 ID '{arguments.stage}' 없음. 가능한 값: {', '.join(stage_scopes)}")
        stage_scope = stage_scopes[arguments.stage]
    unmapped_items, not_applicable_items = print_report(coverage_path, row_set_note, failures, rows, items)
    if stage_scope is None:
        print_row_counts(rows, items)
    else:
        print_stage_items(arguments.stage, stage_scope, rows, items)
    print(f"SUMMARY failures={len(failures)} unmapped={len(unmapped_items)} not_applicable={len(not_applicable_items)} rows={len(rows)} items={len(items)} row_set={row_set_state}")
    return 1 if failures else 0


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--coverage", default=str(DEFAULT_COVERAGE_PATH))
    parser.add_argument("--stage")
    parser.add_argument("--roadmap")
    arguments = parser.parse_args()
    try:
        return run_check(arguments)
    except CoverageInputError as error:
        print(f"입력 오류: {error}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
