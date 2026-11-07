"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), fxp_audit.py rows 가 import 해서 build(context)를 부른다.
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py rows (--pass-list <W13 결과 JSON> ... | --no-pass-list) [--areas <areas.json>] --out <json> [--md <md>] [--root <저장소 사본>](runbook-fxp-v3 3.4·4.4, defs Audit rows (PassListArgs)).
출력: 근거 장부 Docs/FluidFX/live2_evidence.json 행마다 앵커 해석·시험 통과 목록 대조(통과·test_failing·test_unverified)·PD-16 강등·표지·W-8·행 동등 JSON·md. 종료 0 / 1(스키마·행 집합·d52·해석 실패·근거 부족·note D-49·이미지·live2_reference·모르는 FQ) / 2 입력 오류.
상태: 2026-11-07 FX-P X2(B3) 구현. 시험 python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(fixtures_rows.py).
"""
import json
import re

import areas
import common

LEDGER_PATH = "Docs/FluidFX/live2_evidence.json"
ROADMAP_PATH = "Saved/AgentOps/20261019/fluidfx/parity2/roadmap-final.md"
AREAS_DEFAULT_PATH = f"{common.FXP_OUTPUT_DIR}/areas.json"
SOURCE_GLOBS = ("Source/**/*.cpp", "Source/**/*.h")
TEST_STRING = re.compile(r'"(TDGame(?:Editor)?\.[A-Za-z0-9_.]+)"')
TEST_STRING_MARK = b'"TDGame'
TEST_PREFIX_END = "."
ROW_ID = re.compile(r"^[A-Z]\d+[a-z]?$")
EXCLUDED_ROW_PREFIX = "T"
ROADMAP_SECTION_START = "## 2."
ROADMAP_SECTION_END = "## 3."
ROADMAP_STATUS_COLUMN = 3
STATUS_PRESENT, STATUS_PARTIAL, STATUS_DIFFERENT, STATUS_ABSENT, STATUS_NOT_APPLICABLE = "있음", "부분", "다름", "없음", "해당 없음"
STATUSES = (STATUS_PRESENT, STATUS_PARTIAL, STATUS_DIFFERENT, STATUS_ABSENT, STATUS_NOT_APPLICABLE)
STATUS_SUMMARY_KEYS = {STATUS_PRESENT: "present", STATUS_PARTIAL: "partial", STATUS_DIFFERENT: "different", STATUS_ABSENT: "absent", STATUS_NOT_APPLICABLE: "not_applicable"}
ROADMAP_STATUS = re.compile("^(" + "|".join(STATUSES) + ")")
LIVE2_UNSUPPORTED_LABEL = "해당 없음(LIVE-2 미지원)"
LIVE2_TOKEN = "LIVE-2"
UNSUPPORTED_TOKEN = "미지원"
W8_ROWS = ("L18b", "L19")
ROW_KEYS = ("id", "d52", "status", "stages", "default_off", "user_items", "anchors", "note")
ANCHOR_KEYS = ("symbols", "tests", "data", "plans", "judgments", "docs")
TEXT_LIST_ANCHORS = ("tests", "plans", "judgments")
PAIR_ANCHORS = {"symbols": ("file", "regex"), "data": ("file", "json_path"), "docs": ("file", "line")}
DEFAULT_OFF_KEYS = ("file", "regex")
AREA_BY_LETTER = {"B": "input", "D": "smoke", "E": "fire", "F": "fog", "G": "cloud", "H": "track", "I": "track", "J": "track", "K": "foliage", "L": "water",
                  "M": "viscous"}
COMMON_AREA = "common"
NOTE_LIMIT = 80
IMAGE_PATTERN = re.compile(r"\.(?:png|jpe?g|bmp|gif|tga|exr|hdr|webp|tiff?|dds)\b", re.IGNORECASE)
LIVE2_REFERENCE = "live2_reference"
BLOCKED_CELL = "blocked_not_run"
RESULT_SUCCESS = "Success"
TEST_PASS, TEST_FAILING, TEST_UNVERIFIED = "pass", "test_failing", "test_unverified"
TEST_STATES = (TEST_PASS, TEST_FAILING, TEST_UNVERIFIED)
MARKER_DEFAULT_OFF = "기본 꺼짐"
MARKER_PERFORMANCE_PREFIX = "성능 "
MARKER_BLOCKED = "blocked_calib"
MARKER_USER = "user_decision_open"
MARKER_COMMON = "공통(영역 승인 밖)"
PASS_LIST_ON, PASS_LIST_OFF = "on", "off"
RULES = {
    "present": "있음 = (심볼 ∧ 통과 시험) 또는 (데이터 ∧ (통과 시험 ∨ 판정)), 통과 시험 = 시험 앵커 전부 통과",
    "demotion": "장부 있음인데 시험 앵커 중 test_failing·test_unverified가 있으면 부분으로 강등 + 표지(종료 1 아님, PD-16)",
    "tests": "시험 앵커 = Source 의 \"TDGame…\"·\"TDGameEditor…\" 문자열 정확 일치('.' 끝은 접두 ≥1), 통과 목록 results 에 있고 전부 Success = 통과, Success 아닌 결과 = test_failing, 결과 없음 = test_unverified(시험 근거 = 확인 불가)",
    "evidence": "있음 장부는 (심볼 ∧ 시험) 또는 (데이터 ∧ (시험 ∨ 판정)) 앵커, 부분·다름은 앵커 ≥1 + note, 해당 없음은 docs ≥1(없으면 근거 부족)",
    "live2_unsupported": "해당 없음 ∧ docs 줄에 'LIVE-2'·'미지원' = 해당 없음(LIVE-2 미지원), L18b·L19 둘 다면 W-8 닫힘",
    "markers": "표지 = 기본 꺼짐·승인(영역)·성능(영역)·test_failing·test_unverified·blocked_calib(plans 중 areas.json 칸 blocked_not_run)·user_decision_open(user_items)·공통(영역 승인 밖)",
    "equivalence": "행 동등 = 강등 뒤 있음 ∧ 영역 동등(areas.json equivalence.confirmed, 4.1), 공통 행 제외",
    "d49": "note 80자 이하·'LIVE-2'를 뺀 뒤 숫자 0, 장부에 이미지 확장자·live2_reference 없음",
}


def add_arguments(parser):
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--pass-list", action="append", default=None, help="마지막 전체 헤드리스·RHI 결과 JSON(results[].path·result), 여러 번")
    group.add_argument("--no-pass-list", action="store_true", help="통과 목록 무효: 시험 앵커 전부 test_unverified")
    parser.add_argument("--areas", default=AREAS_DEFAULT_PATH, help=f"areas 출력 JSON(기본 {AREAS_DEFAULT_PATH})")


def is_text_list(value):
    return isinstance(value, list) and all(common.is_text(entry) for entry in value)


def is_line_number(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 1


def is_pair(value, keys):
    if not isinstance(value, dict) or set(value) != set(keys):
        return False
    return all(is_line_number(value[key]) if key == "line" else common.is_text(value[key]) for key in keys)


def split_table_cells(line):
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def read_roadmap_rows(reads):
    lines = reads.read_text(ROADMAP_PATH).split("\n")
    start = next((index for index, line in enumerate(lines) if line.startswith(ROADMAP_SECTION_START)), None)
    if start is None:
        raise common.InputError(f"{ROADMAP_PATH}에 '{ROADMAP_SECTION_START}' 절 없음")
    end = next((index for index in range(start + 1, len(lines)) if lines[index].startswith(ROADMAP_SECTION_END)), len(lines))
    rows = {}
    for line in lines[start:end]:
        if not line.startswith("|"):
            continue
        cells = split_table_cells(line)
        if not ROW_ID.match(cells[0]) or cells[0].startswith(EXCLUDED_ROW_PREFIX):
            continue
        match = ROADMAP_STATUS.match(cells[ROADMAP_STATUS_COLUMN]) if len(cells) > ROADMAP_STATUS_COLUMN else None
        rows[cells[0]] = match.group(1) if match else None
    if not rows:
        raise common.InputError(f"{ROADMAP_PATH} 2절에 대조표 행 없음")
    return rows


def pass_list_file_record(relative, document, entries):
    return {"file": relative, "results": len(entries), "not_success": sorted(entry["path"] for entry in entries if entry["result"] != RESULT_SUCCESS),
            "filters": document.get("filters") if is_text_list(document.get("filters")) else None,
            "rhi": document.get("rhi") if common.is_text(document.get("rhi")) else None,
            "incomplete": document.get("incomplete") if isinstance(document.get("incomplete"), bool) else None,
            "crashed": document.get("crashed") if isinstance(document.get("crashed"), bool) else None}


def read_pass_lists(reads, paths):
    if not paths:
        return None, {"mode": PASS_LIST_OFF, "files": []}
    results, files = {}, []
    for value in paths:
        relative = reads.relative(value)
        document = reads.read_json(relative)
        entries = document.get("results") if isinstance(document, dict) else None
        if not isinstance(entries, list) or not all(isinstance(entry, dict) and common.is_text(entry.get("path")) and common.is_text(entry.get("result"))
                                                     for entry in entries):
            raise common.InputError(f"통과 목록 형식(results[].path·result 필요): {relative}")
        for entry in entries:
            results.setdefault(entry["path"], []).append(entry["result"])
        files.append(pass_list_file_record(relative, document, entries))
    return results, {"mode": PASS_LIST_ON, "files": files}


def build_test_index(context):
    names = set()
    for pattern in SOURCE_GLOBS:
        for relative in context.reads.glob(pattern):
            if TEST_STRING_MARK in (context.root / relative).read_bytes():
                names.update(TEST_STRING.findall(context.reads.read_text(relative)))
    return sorted(names)


def read_areas(reads, path):
    relative = reads.relative(path)
    document = reads.read_json(relative)
    if not isinstance(document, dict) or not isinstance(document.get("areas"), dict) or not isinstance(document.get("plans"), dict):
        raise common.InputError(f"{relative} 형식(areas·plans 객체 필요)")
    return relative, document


def json_path_exists(value, parts):
    if not parts:
        return True
    if isinstance(value, dict):
        return any(".".join(parts[:size]) in value and json_path_exists(value[".".join(parts[:size])], parts[size:]) for size in range(len(parts), 0, -1))
    if isinstance(value, list) and parts[0].isdigit() and int(parts[0]) < len(value):
        return json_path_exists(value[int(parts[0])], parts[1:])
    return False


def test_state(names, results):
    if results is None:
        return TEST_UNVERIFIED
    found = [results[name] for name in names if name in results]
    if any(result != RESULT_SUCCESS for values in found for result in values):
        return TEST_FAILING
    return TEST_PASS if len(found) == len(names) else TEST_UNVERIFIED


class AnchorResolver:
    def __init__(self, context, test_index, results, areas_document):
        self.reads = context.reads
        self.test_index = test_index
        self.test_set = set(test_index)
        self.results = results
        self.plan_cells = {base: details.get("cell") for base, details in areas_document["plans"].items() if isinstance(details, dict)}
        self.texts = {}
        self.documents = {}
        self.patterns = {}

    def text(self, file):
        relative = self.reads.relative(file)
        if relative not in self.texts:
            try:
                self.texts[relative] = self.reads.read_text(relative, required=False)
            except common.InputError:
                self.texts[relative] = None
        return self.texts[relative]

    def pattern(self, regex):
        if regex not in self.patterns:
            try:
                self.patterns[regex] = re.compile(regex, re.MULTILINE)
            except re.error:
                self.patterns[regex] = None
        return self.patterns[regex]

    def regex_problem(self, entry):
        text = self.text(entry["file"])
        if text is None:
            return f"파일 없음 {entry['file']}"
        pattern = self.pattern(entry["regex"])
        if pattern is None:
            return f"정규식 오류 {entry['regex']}"
        return None if pattern.search(text) else f"{entry['file']}에 정규식 안 맞음 {entry['regex']}"

    def test_names(self, anchor):
        if anchor.endswith(TEST_PREFIX_END):
            return [name for name in self.test_index if name.startswith(anchor)]
        return [anchor] if anchor in self.test_set else []

    def test_state(self, names):
        return test_state(names, self.results)

    def data_problem(self, entry):
        relative = self.reads.relative(entry["file"])
        if relative not in self.documents:
            text = self.text(relative)
            try:
                self.documents[relative] = (True, json.loads(text)) if text is not None else (False, None)
            except ValueError:
                self.documents[relative] = (False, None)
        loaded, document = self.documents[relative]
        if not loaded:
            return f"JSON 파일 없음·읽기 실패 {entry['file']}"
        return None if json_path_exists(document, entry["json_path"].split(".")) else f"{entry['file']}에 경로 없음 {entry['json_path']}"

    def plan_file(self, plan):
        candidates = (f"{common.CALIB_PLANS_DIR}/{plan}.json", f"{common.RETIRED_PLANS_DIR}/{plan}.json")
        found = next((candidate for candidate in candidates if self.reads.exists(candidate)), None)
        if found is not None:
            self.reads.read_bytes(found)
        return found

    def plan_cell(self, plan):
        return self.plan_cells.get(common.base_plan(plan))

    def file_exists(self, file):
        if not self.reads.exists(file):
            return False
        self.reads.read_bytes(file)
        return True

    def doc_line(self, entry):
        text = self.text(entry["file"])
        if text is None:
            return None, f"문서 없음 {entry['file']}"
        lines = text.split("\n")
        if entry["line"] > len(lines):
            return None, f"{entry['file']} 줄 {entry['line']} 없음(끝 {len(lines)})"
        return lines[entry["line"] - 1].rstrip("\r"), None


def ledger_rows(ledger, problems):
    if not isinstance(ledger, dict):
        problems.append(f"스키마: {LEDGER_PATH} 최상위가 객체 아님")
        return []
    if set(ledger) != {"version", "rows"}:
        problems.append(f"스키마: {LEDGER_PATH} 최상위는 {{version, rows}}만")
    if ledger.get("version") != 1:
        problems.append(f"스키마: {LEDGER_PATH} version 은 1")
    if not isinstance(ledger.get("rows"), list):
        problems.append(f"스키마: {LEDGER_PATH} rows 는 목록")
        return []
    return ledger["rows"]


def row_schema_problems(row, position):
    if not isinstance(row, dict):
        return [f"스키마(#{position}): 행이 객체 아님"]
    label = row["id"] if common.is_text(row.get("id")) else f"#{position}"
    if set(row) != set(ROW_KEYS):
        return [f"스키마({label}): 열쇠 차이 {sorted(set(row) ^ set(ROW_KEYS))}"]
    problems = []
    if not common.is_text(row["id"]) or not ROW_ID.match(row["id"]):
        problems.append(f"스키마({label}): id 형식")
    for key in ("d52", "status"):
        if row[key] not in STATUSES:
            problems.append(f"스키마({label}): {key} '{row[key]}'는 5종 밖")
    for key in ("stages", "user_items"):
        if not is_text_list(row[key]):
            problems.append(f"스키마({label}): {key} 는 글자 목록")
    if not isinstance(row["note"], str):
        problems.append(f"스키마({label}): note 는 글자")
    if row["default_off"] is not None and not is_pair(row["default_off"], DEFAULT_OFF_KEYS):
        problems.append(f"스키마({label}): default_off 는 null 또는 {{file, regex}}")
    anchors = row["anchors"]
    if not isinstance(anchors, dict) or set(anchors) != set(ANCHOR_KEYS):
        problems.append(f"스키마({label}): anchors 열쇠는 {list(ANCHOR_KEYS)}")
        return problems
    for kind in TEXT_LIST_ANCHORS:
        if not is_text_list(anchors[kind]):
            problems.append(f"스키마({label}): anchors.{kind} 는 글자 목록")
    for kind, keys in PAIR_ANCHORS.items():
        if not isinstance(anchors[kind], list) or not all(is_pair(entry, keys) for entry in anchors[kind]):
            problems.append(f"스키마({label}): anchors.{kind} 는 {{{', '.join(keys)}}} 목록")
    return problems


def row_area(row_id):
    return AREA_BY_LETTER.get(row_id[0], COMMON_AREA)


def area_facts(areas_document, area, areas_relative):
    if area == COMMON_AREA:
        return None
    record = areas_document["areas"].get(area)
    if not isinstance(record, dict) or not isinstance(record.get("approval"), dict) or not isinstance(record.get("equivalence"), dict):
        raise common.InputError(f"{areas_relative} areas.{area} 형식(approval·equivalence 객체 필요)")
    return record


def d49_problems(row, row_id):
    problems = []
    note = row["note"]
    if len(note) > NOTE_LIMIT:
        problems.append(f"note D-49({row_id}): {len(note)}자 > {NOTE_LIMIT}")
    if re.search(r"\d", note.replace(LIVE2_TOKEN, "")):
        problems.append(f"note D-49({row_id}): 'LIVE-2'를 뺀 뒤 숫자 있음")
    text = json.dumps(row, ensure_ascii=False)
    if IMAGE_PATTERN.search(text):
        problems.append(f"이미지 D-49({row_id}): 이미지 확장자 참조")
    if LIVE2_REFERENCE in text:
        problems.append(f"live2_reference D-49({row_id}): live2_reference 참조")
    return problems


class RowFacts:
    def __init__(self):
        self.resolved = {kind: 0 for kind in ANCHOR_KEYS}
        self.tests = []
        self.plans = []
        self.doc_lines = []
        self.problems = []


def resolve_row_anchors(row, resolver):
    row_id, anchors, facts = row["id"], row["anchors"], RowFacts()
    for entry in anchors["symbols"]:
        detail = resolver.regex_problem(entry)
        facts.problems += [f"해석 실패({row_id}) symbols: {detail}"] if detail else []
        facts.resolved["symbols"] += 0 if detail else 1
    for anchor in anchors["tests"]:
        names = resolver.test_names(anchor)
        if not names:
            facts.problems.append(f"해석 실패({row_id}) tests: 소스 문자열 없음 {anchor}")
            continue
        facts.tests.append({"anchor": anchor, "names": len(names), "state": resolver.test_state(names)})
        facts.resolved["tests"] += 1
    for entry in anchors["data"]:
        detail = resolver.data_problem(entry)
        facts.problems += [f"해석 실패({row_id}) data: {detail}"] if detail else []
        facts.resolved["data"] += 0 if detail else 1
    for plan in anchors["plans"]:
        if resolver.plan_file(plan) is None:
            facts.problems.append(f"해석 실패({row_id}) plans: 계획 파일 없음 {plan}")
            continue
        facts.plans.append({"plan": plan, "cell": resolver.plan_cell(plan)})
        facts.resolved["plans"] += 1
    for file in anchors["judgments"]:
        found = resolver.file_exists(file)
        facts.problems += [] if found else [f"해석 실패({row_id}) judgments: 판정 파일 없음 {file}"]
        facts.resolved["judgments"] += 1 if found else 0
    for entry in anchors["docs"]:
        line, detail = resolver.doc_line(entry)
        facts.problems += [f"해석 실패({row_id}) docs: {detail}"] if detail else []
        facts.doc_lines += [] if detail else [line]
        facts.resolved["docs"] += 0 if detail else 1
    if row["default_off"] is not None:
        detail = resolver.regex_problem(row["default_off"])
        facts.problems += [f"해석 실패({row_id}) default_off: {detail}"] if detail else []
    return facts


def evidence_problems(row, anchors):
    row_id, status = row["id"], row["status"]
    count = {kind: len(anchors[kind]) for kind in ANCHOR_KEYS}
    if status == STATUS_PRESENT and not ((count["symbols"] and count["tests"]) or (count["data"] and (count["tests"] or count["judgments"]))):
        return [f"근거 부족({row_id}): 있음은 (심볼 ∧ 시험) 또는 (데이터 ∧ (시험 ∨ 판정)) 앵커 필요"]
    if status in (STATUS_PARTIAL, STATUS_DIFFERENT) and (not any(count.values()) or not row["note"].strip()):
        return [f"근거 부족({row_id}): {status}은 앵커 ≥1 + note 필요"]
    if status == STATUS_NOT_APPLICABLE and not count["docs"]:
        return [f"근거 부족({row_id}): 해당 없음은 docs 줄 필요"]
    return []


def decide_status(row, facts, evidence):
    status = row["status"]
    states = [test["state"] for test in facts.tests]
    if status != STATUS_PRESENT or facts.problems or evidence:
        return status, None
    tests_pass = bool(states) and all(state == TEST_PASS for state in states)
    resolved = facts.resolved
    meets = (resolved["symbols"] and tests_pass) or (resolved["data"] and (tests_pass or resolved["judgments"]))
    if meets and all(state == TEST_PASS for state in states):
        return status, None
    counts = [f"{state} {states.count(state)}" for state in (TEST_FAILING, TEST_UNVERIFIED) if states.count(state)]
    return STATUS_PARTIAL, " · ".join(counts)


def row_label(status, facts):
    if status == STATUS_NOT_APPLICABLE and any(LIVE2_TOKEN in line and UNSUPPORTED_TOKEN in line for line in facts.doc_lines):
        return LIVE2_UNSUPPORTED_LABEL
    return status


def row_markers(row, facts, area, record):
    states = {test["state"] for test in facts.tests}
    markers = [MARKER_DEFAULT_OFF] if row["default_off"] is not None else []
    if record is not None:
        approval = areas.approval_reason(record["approval"])
        performance = areas.performance_reason(record.get("performance"), record.get("blocking_unmeasurable"))
        markers += [approval] if approval else []
        markers += [f"{MARKER_PERFORMANCE_PREFIX}{performance}"] if performance else []
    markers += [state for state in (TEST_FAILING, TEST_UNVERIFIED) if state in states]
    markers += [MARKER_BLOCKED] if any(plan["cell"] == BLOCKED_CELL for plan in facts.plans) else []
    markers += [MARKER_USER] if row["user_items"] else []
    markers += [MARKER_COMMON] if record is None else []
    return markers


def evaluate_row(row, resolver, areas_document, areas_relative, known_user_items):
    row_id = row["id"]
    area = row_area(row_id)
    record = area_facts(areas_document, area, areas_relative)
    facts = resolve_row_anchors(row, resolver)
    evidence = evidence_problems(row, row["anchors"])
    problems = facts.problems + evidence + d49_problems(row, row_id)
    problems += [f"모르는 FQ({row_id}): {item}" for item in row["user_items"] if item not in known_user_items]
    status, demotion = decide_status(row, facts, evidence)
    equivalent = status == STATUS_PRESENT and record is not None and record["equivalence"].get("confirmed") is True
    result = {"id": row_id, "area": area, "d52": row["d52"], "ledger_status": row["status"], "status": status, "label": row_label(status, facts),
              "demoted": demotion is not None, "demotion": demotion, "markers": row_markers(row, facts, area, record), "equivalent": equivalent,
              "tests": facts.tests, "plans": facts.plans, "anchor_counts": {kind: len(row["anchors"][kind]) for kind in ANCHOR_KEYS},
              "stages": row["stages"], "user_items": row["user_items"], "note": row["note"]}
    return result, problems


def row_set_problems(rows, roadmap):
    problems, seen = [], {}
    for row in rows:
        seen[row["id"]] = seen.get(row["id"], 0) + 1
    problems += [f"스키마: 장부 행 중복 {row_id}" for row_id, count in sorted(seen.items()) if count > 1]
    problems += [f"행 집합: 장부에 없는 대조표 행 {row_id}" for row_id in roadmap if row_id not in seen]
    problems += [f"행 집합: 대조표 2절에 없는 장부 행 {row_id}" for row_id in sorted(seen) if row_id not in roadmap]
    problems += [f"d52 불일치({row['id']}): 장부 '{row['d52']}' 대 대조표 '{roadmap[row['id']]}'" for row in rows
                 if row["id"] in roadmap and row["d52"] != roadmap[row["id"]]]
    return problems


def status_counts(records, key):
    return {status: sum(1 for record in records if record[key] == status) for status in STATUSES}


def marker_counts(records):
    counts = {"default_off": MARKER_DEFAULT_OFF, "test_failing": TEST_FAILING, "test_unverified": TEST_UNVERIFIED, "blocked_calib": MARKER_BLOCKED,
              "user_decision_open": MARKER_USER, "common": MARKER_COMMON}
    result = {key: sum(1 for record in records if marker in record["markers"]) for key, marker in counts.items()}
    approval_markers = (common.p_plans().APPROVAL_PENDING, areas.REJECTED_PREFIX)
    result["approval_open"] = sum(1 for record in records if any(marker in approval_markers for marker in record["markers"]))
    result["performance"] = sum(1 for record in records if any(marker.startswith(MARKER_PERFORMANCE_PREFIX) for marker in record["markers"]))
    return result


def w8_state(records):
    labels = {record["id"]: record["label"] for record in records if record["id"] in W8_ROWS}
    closed = all(labels.get(row_id) == LIVE2_UNSUPPORTED_LABEL for row_id in W8_ROWS)
    return {"closed": closed, "rows": {row_id: labels.get(row_id) for row_id in W8_ROWS}}


def area_summaries(records, areas_document):
    summaries = {}
    for area in sorted({record["area"] for record in records}):
        members = [record for record in records if record["area"] == area]
        details = areas_document["areas"].get(area) if area != COMMON_AREA else None
        summaries[area] = {"rows": len(members), "status": status_counts(members, "status"), "demoted": sum(1 for record in members if record["demoted"]),
                           "equivalent_rows": sum(1 for record in members if record["equivalent"]),
                           "approval": details["approval"].get("state") if details else MARKER_COMMON,
                           "performance": details.get("performance") if details else None}
    return summaries


def summary_text(records, final, marker_total, w8, equivalent_rows, pass_mode):
    statuses = " ".join(f"{STATUS_SUMMARY_KEYS[status]}={final[status]}" for status in STATUSES)
    live2 = sum(1 for record in records if record["label"] == LIVE2_UNSUPPORTED_LABEL)
    markers = " ".join(f"{key}={marker_total[key]}" for key in ("default_off", "approval_open", "performance", "test_failing", "test_unverified", "blocked_calib",
                                                                 "user_decision_open", "common"))
    return (f"rows={len(records)} {statuses} live2_unsupported={live2} demoted={sum(1 for record in records if record['demoted'])} {markers} "
            f"w8={'closed' if w8['closed'] else 'open'} equivalent_rows={equivalent_rows} pass_list={pass_mode}")


def test_cell(record):
    states = [test["state"] for test in record["tests"]]
    return "/".join(str(states.count(state)) for state in TEST_STATES)


def render_markdown(records, ledger_counts, final, marker_total, w8, equivalent_rows, pass_info, area_rows, areas_relative):
    cell = common.markdown_cell
    pass_text = " · ".join(f"{entry['file']}(결과 {entry['results']}, Success 아님 {len(entry['not_success'])})" for entry in pass_info["files"])
    w8_text = " · ".join(f"{row_id} {cell(label)}" for row_id, label in w8["rows"].items())
    lines = [f"# FX-P 대조표 행 재판정 (근거 장부 {LEDGER_PATH}, runbook-fxp-v3 3.4·4.4)", "",
             "- 상태(구현)와 표지(출하·승인·성능)는 따로 적는다: 구현 ≠ 출하 동등(4.4).", f"- {RULES['present']}", f"- {RULES['demotion']}", f"- {RULES['tests']}",
             f"- 통과 목록: {pass_text}" if pass_info["files"] else "- 통과 목록: 없음(--no-pass-list, 시험 근거 = 확인 불가, FU-17)",
             f"- 영역 입력: {areas_relative}", f"- {RULES['equivalence']}: {equivalent_rows}행",
             f"- W-8: {'닫힘' if w8['closed'] else '열림'}({w8_text})", "",
             "## 상태 분포", "", "| 상태 | 장부 | 판정(강등 뒤) |", "|---|---|---|"]
    lines += [f"| {status} | {ledger_counts[status]} | {final[status]} |" for status in STATUSES]
    lines += [f"| 합계 | {sum(ledger_counts.values())} | {sum(final.values())} |",
              f"| 그중 {LIVE2_UNSUPPORTED_LABEL} | — | {sum(1 for record in records if record['label'] == LIVE2_UNSUPPORTED_LABEL)} |", "",
              "## 표지", "", "| 표지 | 행 수 |", "|---|---|"]
    lines += [f"| {key} | {value} |" for key, value in sorted(marker_total.items())]
    lines += ["", "## 영역별", "", "| 영역 | 행 | " + " | ".join(STATUSES) + " | 강등 | 승인 | 성능 | 동등 행 |", "|" + "---|" * (len(STATUSES) + 6)]
    for area, summary in area_rows.items():
        lines.append(f"| {area} | {summary['rows']} | " + " | ".join(str(summary["status"][status]) for status in STATUSES)
                     + f" | {summary['demoted']} | {cell(summary['approval'])} | {cell(summary['performance'])} | {summary['equivalent_rows']} |")
    lines += ["", "## 강등(PD-16)", ""]
    lines += [f"- {record['id']} 있음 → 부분: {record['demotion']}" for record in records if record["demoted"]] or ["- 없음"]
    lines += ["", "## 행별", "", "| 행 | 영역 | D-52 | 장부 | 판정 | 표지 | 시험(통과/실패/확인 불가) | 앵커(심볼·시험·데이터·계획·판정·문서) | note |", "|" + "---|" * 9]
    for record in records:
        anchors = "·".join(str(record["anchor_counts"][kind]) for kind in ANCHOR_KEYS)
        lines.append(f"| {record['id']} | {record['area']} | {record['d52']} | {record['ledger_status']} | {cell(record['label'])} | "
                     f"{cell(' · '.join(record['markers']) or None)} | {test_cell(record)} | {anchors} | {cell(record['note'] or None)} |")
    return lines


def build(context):
    reads, problems = context.reads, []
    roadmap = read_roadmap_rows(reads)
    static = common.load_fxp_static(reads)
    known_user_items = {entry["id"] for entry in static["user_items"]}
    areas_relative, areas_document = read_areas(reads, context.args.areas)
    results, pass_info = read_pass_lists(reads, None if context.args.no_pass_list else context.args.pass_list)
    rows = ledger_rows(reads.read_json(LEDGER_PATH), problems)
    valid_rows = []
    for position, row in enumerate(rows, 1):
        schema = row_schema_problems(row, position)
        problems += schema
        valid_rows += [] if schema else [row]
    problems += row_set_problems(valid_rows, roadmap)
    resolver = AnchorResolver(context, build_test_index(context), results, areas_document)
    records = []
    for row in valid_rows:
        record, row_problems = evaluate_row(row, resolver, areas_document, areas_relative, known_user_items)
        records.append(record)
        problems += row_problems
    ledger_counts, final = status_counts(records, "ledger_status"), status_counts(records, "status")
    marker_total, w8 = marker_counts(records), w8_state(records)
    equivalent_rows = sum(1 for record in records if record["equivalent"])
    area_rows = area_summaries(records, areas_document)
    document = {"version": 1, "ledger": LEDGER_PATH, "roadmap": ROADMAP_PATH, "areas_input": areas_relative, "pass_list": pass_info,
                "test_strings": len(resolver.test_index), "rows": records, "distribution": {"ledger": ledger_counts, "final": final},
                "live2_unsupported": sorted(record["id"] for record in records if record["label"] == LIVE2_UNSUPPORTED_LABEL),
                "demoted": [record["id"] for record in records if record["demoted"]], "markers": marker_total, "areas": area_rows, "w8": w8,
                "equivalent_rows": equivalent_rows, "rule": RULES}
    markdown = render_markdown(records, ledger_counts, final, marker_total, w8, equivalent_rows, pass_info, area_rows, areas_relative)
    return common.Outcome(document, markdown, summary_text(records, final, marker_total, w8, equivalent_rows, pass_info["mode"]), problems)
