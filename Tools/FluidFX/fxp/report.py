"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), fxp_audit.py report 가 import 해서 build(context)를 부른다.
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py report --runbook <실행서> --defs <정의 묶음> --date <YYYY-MM-DD> --out <json> [--md <md>] [--public-md <공개 md>] [--root <저장소 사본>](runbook-fxp-v3 3.8·4절, defs Report).
출력: Saved/FluidFX/fxp 고정 입력 9개(areas·items·golden·compare·rows·levels·r13·package-min·package-fluid)·fxp_static.json·x0-runbook.sha256 을 묶은 최종 동등 감사 JSON·md·공개 md(OP-24 제목·교훈·명령 줄). 종료 0 / 1(실행서·defs sha 불일치·입력 problems·연결 불일치·4.1~4.7 재검산·고정 문구·FU·FQ 표가 실행서 5·6절과 다름·D-49) / 2(입력 빠짐·형식·날짜).
상태: 2026-11-07 FX-P X6(B6) 구현, fix1(빌더 A) FU·FQ 원래 줄 대조(실행서 5·6절 표 줄·id별 조각) 추가. 시험 python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(fixtures_report.py).
"""
import itertools
import re

import common

OUTPUT_DIRECTORY = common.FXP_OUTPUT_DIR
INPUT_DOCUMENTS = (("areas", "areas", None), ("items", "items", None), ("golden", "golden", None), ("compare", "compare", None), ("rows", "rows", None),
                   ("levels", "levels", None), ("r13", "r13", None), ("package-min", "package", "min"), ("package-fluid", "package", "fluid"))
SHA_RECORD_PATH = f"{OUTPUT_DIRECTORY}/x0-runbook.sha256"
TITLE_PREFIX = "# FX-P 최종 동등 감사 ("
LESSON_LINE = "교훈: –(이 감사가 새로 정한 교훈 없음, 새 함정은 부모 장부가 Docs/Lessons에 L-ID로 기록)"
COMMAND_LINE = "명령: python -B Tools/FluidFX/fxp/fxp_audit.py report (FX-P 실행서 X6)"
DATE_PATTERN = re.compile(r"\d{4}-\d{2}-\d{2}")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
D49_NUMBER_PATTERN = re.compile(r"LIVE-2[^|\n]{0,40}\d+(\.\d+)?\s*(ms|fps|FPS|%)")
IMAGE_PATTERN = re.compile(r"\.(?:png|jpe?g|bmp|gif|tga|exr|hdr|webp|tiff?|dds)\b", re.IGNORECASE)
LIVE2_REFERENCE = "live2_reference"
PF_LINE_PATTERN = re.compile(r"PF:\d+")
DIGIT_PATTERN = re.compile(r"\d")
LIVE2_NAME = "LIVE-2"
COMMON_AREA = "common"
STATUS_PRESENT = "있음"
STATUS_ORDER = ("있음", "부분", "없음", "다름", "해당 없음")
EQUIVALENT_LABEL = "동등 확인(성능은 [제안] 예산 기준, 동일 이상 아님)"
EQUIVALENT_MARK = "동등 확인"
APPROVED_PREFIX = "승인 "
REJECTED_PREFIX = "거부"
UNMET_PREFIX = "미충족("
PARTIAL_PREFIX = "부분("
PR11_UNMET = "FX-P 성능 미달"
PR11_PARTIAL_PREFIX = "FX-P 성능 부분("
PR11_PASS = "화면 승인만"
PR11_NO_INPUT = "판정 불가(성능 입력 없음)"
COMPARE_SAME_MACHINE = "P-4 미답"
COMPARE_NOTATION = "PF 환산 ×2.3~3·상한 비교(증명 아님)·제안 예산 대비·상한 꺼짐(P-22)"
COMPARE_FORBIDDEN = ("동일 이상", "LIVE-2 이하")
COMPARE_LABEL_ORDER = ("통과", "미달", "판정 불가", "그 밖")
R13_LIVE2 = "스트레스 정량 비공개(PF:206)"
R13_KIND_PREFIX = {"p_quote": "P 판정 인용(", "structural": "구조 격차(기존 측정 인용·보정 전, FX-7p ", "no_stage": "무대 없음(R13s 대기)"}
BASIS_REASONS = (("목표 [제안]", "spec_proposal"), ("자유 문안(출처 확인 불가)", "free_text"), ("근거 미표기", "unmarked"))
CELL_COLUMNS = ("influence", "verify_only", "insensitive", "no_adopt", "conflict", "removed_after_adoption")
OTHER_CELLS = (("undetermined", "판정 불가"), ("failed", "실패"), ("blocked_not_run", "미실행"), ("retired", "폐기"), ("unclassified", "분류 없음"))
MARKER_LABELS = (("default_off", "기본 꺼짐"), ("approval_open", "승인 대기"), ("performance", "성능"), ("test_failing", "test_failing"),
                 ("test_unverified", "test_unverified"), ("blocked_calib", "blocked_calib"), ("user_decision_open", "user_decision_open"), ("common", "공통(영역 승인 밖)"))
ITEM_CLASS_LABELS = (("closed", "닫힘"), ("record", "기록"), ("follow_up", "후속(FU)"), ("user", "사용자(FQ)"), ("unknown", "분류 없음"))
UNMEASURABLE_GROUPS = ("비대상", "기능 공백", "기타")
STATIC_PHRASE_KEYS = ("header_template", "golden_line_template", "environment_note")
STATIC_TABLES = (("FU", "5", "follow_up_rows", "follow_ups", "item"), ("FQ", "6", "user_item_rows", "user_items", "ask"))
SECTION_HEADING_PATTERN = re.compile(r"## (\d+)\. ")
TABLE_SEPARATOR_PATTERN = re.compile(r"\|(?:\s*:?-+:?\s*\|)+\s*")
BUNDLE_NUMBER_PATTERN = re.compile(r"(\d+)(?:~(\d+))?")
RECORD_ONLY_PREFIX_PATTERN = re.compile(r"\(기록만[^)]*\) ")
BUNDLE_SEPARATOR = " · "
RANGE_JOINER = "·"
CHECK_LABELS = (("runbook_sha", "실행서·defs sha 대조(마지막 기록 줄)"), ("input_problems", "입력 출력의 problems"), ("chain", "연결 대조(앞 단계가 읽은 보고 입력 sha)"),
                ("runbook_phrases", "고정 문구가 실행서에 그대로"), ("static_tables", "FU·FQ 원래 줄 = 실행서 5·6절 표 줄, id별 항목 = 원래 줄 조각"), ("areas", "4.1 동등·4.7 PR-11 재검산"), ("compare", "4.2 비교표 표기"), ("r13", "4.3 R13 표기"),
                ("rows", "4.1 행 동등 재검산"), ("levels", "4.4 excluded 근거"), ("golden", "4.6 골든 줄"), ("d49", "D-49 공개 md 검사"))


def add_arguments(parser):
    parser.add_argument("--runbook", required=True, help="실행서 runbook-fxp-<판>.md(sha 기록 마지막 줄과 대조)")
    parser.add_argument("--defs", required=True, help="정의 묶음 fxp-defs-<판>.ps1(sha 기록 마지막 줄과 대조)")
    parser.add_argument("--date", required=True, help="보고 날짜 YYYY-MM-DD(제목)")
    parser.add_argument("--public-md", default=None, help="공개 md(OP-24 제목·교훈·명령 줄, D-49 검사) = Docs/Validation 사본의 원본")


def short(value, size=8):
    return value[:size] if isinstance(value, str) else "없음"


def cell(value):
    return common.markdown_cell(value)


def require(document, key, kind, label):
    value = document.get(key) if isinstance(document, dict) else None
    if not isinstance(value, kind):
        raise common.InputError(f"{label}: {key} 형식")
    return value


def document_path(name):
    return f"{OUTPUT_DIRECTORY}/{name}.json"


def runbook_phrases(p):
    return (EQUIVALENT_LABEL, p.PROPOSED_PASS_LABEL, p.APPROVAL_PENDING, COMPARE_NOTATION, COMPARE_SAME_MACHINE, R13_LIVE2, *R13_KIND_PREFIX.values(),
            PR11_UNMET, PR11_PARTIAL_PREFIX, PR11_PASS, PR11_NO_INPUT, *(label for label, _ in BASIS_REASONS), "부분(막는 측정 불가", COMMAND_LINE, TITLE_PREFIX)


def read_input_documents(reads):
    documents = {}
    for name, command, tag in INPUT_DOCUMENTS:
        path = document_path(name)
        document = reads.read_json(path)
        if not isinstance(document, dict) or document.get("command") != command:
            raise common.InputError(f"{path}: {command} 출력이 아님(command)")
        if tag is not None and document.get("tag") != tag:
            raise common.InputError(f"{path}: tag {tag} 아님")
        for key, kind in (("inputs", dict), ("problems", list), ("summary", str)):
            require(document, key, kind, path)
        documents[name] = document
    return documents


def read_static(reads):
    static = common.load_fxp_static(reads)
    for key in STATIC_PHRASE_KEYS:
        if not common.is_text(static.get(key)):
            raise common.InputError(f"{common.FXP_STATIC_PATH}: {key} 문장 없음")
    for _, _, rows_key, _, text_key in STATIC_TABLES:
        rows = static.get(rows_key)
        if not isinstance(rows, list) or not all(isinstance(row, dict) and all(common.is_text(row.get(key)) for key in ("no", text_key, "source")) for row in rows):
            raise common.InputError(f"{common.FXP_STATIC_PATH}: {rows_key} 형식(no·{text_key}·source 글자 목록)")
    return static


def parse_sha_line(number, line):
    fields = line.split()
    if len(fields) < 4 or not SHA256_PATTERN.fullmatch(fields[1]) or not SHA256_PATTERN.fullmatch(fields[2]):
        raise common.InputError(f"{SHA_RECORD_PATH} {number}번째 줄 형식(번호 실행서sha defs sha HEAD 실행서)")
    return {"index": fields[0], "runbook_sha256": fields[1], "defs_sha256": fields[2], "head": fields[3], "runbook": fields[4] if len(fields) > 4 else None}


def decode_text(data, path):
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise common.InputError(f"UTF-8 아님: {path} ({error})")


def read_runbook_record(reads, args):
    lines = [line for line in reads.read_text(SHA_RECORD_PATH).splitlines() if line.strip()]
    if not lines:
        raise common.InputError(f"{SHA_RECORD_PATH}: 기록 줄 없음")
    history = [parse_sha_line(number, line) for number, line in enumerate(lines, 1)]
    runbook_path, defs_path = reads.relative(args.runbook), reads.relative(args.defs)
    runbook_bytes, defs_bytes = reads.read_bytes(runbook_path), reads.read_bytes(defs_path)
    current = {"runbook": runbook_path, "runbook_sha256": common.sha256_bytes(runbook_bytes), "defs": defs_path, "defs_sha256": common.sha256_bytes(defs_bytes)}
    last = history[-1]
    record = {"lines": len(history), "history": history, "last": last, "current": current,
              "runbook_match": last["runbook_sha256"] == current["runbook_sha256"], "defs_match": last["defs_sha256"] == current["defs_sha256"]}
    return record, decode_text(runbook_bytes, runbook_path)


def check_runbook_record(record):
    last, current, problems = record["last"], record["current"], []
    if not record["runbook_match"]:
        problems.append(f"실행서 sha 불일치: 기록 마지막 줄 {short(last['runbook_sha256'])} ≠ {current['runbook']} {short(current['runbook_sha256'])}(3·4절 수정 = 새 판 + RecordRunbookSha)")
    if not record["defs_match"]:
        problems.append(f"정의 묶음 sha 불일치: 기록 마지막 줄 {short(last['defs_sha256'])} ≠ {current['defs']} {short(current['defs_sha256'])}")
    return problems


def check_runbook_phrases(runbook_text, static, p):
    phrases = [static[key] for key in STATIC_PHRASE_KEYS] + list(runbook_phrases(p))
    return [f"고정 문구가 실행서에 없음: '{phrase}'(새 판이면 보고·fxp_static 갱신)" for phrase in phrases if phrase not in runbook_text]


def runbook_table_lines(runbook_text, section):
    lines, inside = [], False
    for line in runbook_text.splitlines():
        heading = SECTION_HEADING_PATTERN.match(line)
        if heading:
            inside = heading.group(1) == section
        elif inside:
            lines.append(line.rstrip())
    start = next((index + 1 for index, line in enumerate(lines) if TABLE_SEPARATOR_PATTERN.fullmatch(line)), None)
    if start is None:
        return []
    rows = []
    for line in lines[start:]:
        if not line.startswith("|"):
            break
        rows.append(line)
    return rows


def bundle_groups(number_text):
    groups = []
    for part in number_text.split(BUNDLE_SEPARATOR):
        match = BUNDLE_NUMBER_PATTERN.fullmatch(part)
        if match is None:
            return None
        first, last = int(match.group(1)), int(match.group(2) or match.group(1))
        if last < first:
            return None
        groups.append(list(range(first, last + 1)))
    return groups


def bundle_renderings(group, texts):
    if len(group) == 1:
        return texts
    return [RANGE_JOINER.join(texts), BUNDLE_SEPARATOR.join(f"{number} {text}" for number, text in zip(group, texts))]


def check_bundle_row(row, label, by_id, text_key):
    groups = bundle_groups(row["no"])
    if groups is None:
        return [], [f"{label} 원래 줄 번호 형식(n · n~m): '{row['no']}'"]
    ids = [f"{label}-{number}" for group in groups for number in group]
    if any(identifier not in by_id for identifier in ids):
        return ids, []
    prefix = RECORD_ONLY_PREFIX_PATTERN.match(row[text_key])
    problems = []
    if any((by_id[identifier].get("record_only") is True) != (prefix is not None) for identifier in ids):
        problems.append(f"{label} 원래 줄 '{row['no']}' 기록만 표지: 줄 앞 '(기록만…)' {'있음' if prefix else '없음'} ≠ {' · '.join(ids)} record_only")
    body = row[text_key][prefix.end():] if prefix else row[text_key]
    choices = itertools.product(*(bundle_renderings(group, [str(by_id[f"{label}-{number}"].get(text_key)) for number in group]) for group in groups))
    renderings = [BUNDLE_SEPARATOR.join(choice) for choice in choices]
    if body not in renderings:
        problems.append(f"{label} 원래 줄 '{row['no']}' 조각 ≠ id별 {text_key}: 줄 '{body}' ≠ {' · '.join(ids)} 이음 '{renderings[0]}'")
    return ids, problems


def check_static_tables(runbook_text, static):
    problems = []
    for label, section, rows_key, entries_key, text_key in STATIC_TABLES:
        rows, entries = static[rows_key], static[entries_key]
        static_lines = [f"| {row['no']} | {row[text_key]} | {row['source']} |" for row in rows]
        runbook_lines = runbook_table_lines(runbook_text, section)
        problems += [f"{label} 원래 줄이 실행서 {section}절 표에 없음: '{line}'" for line in static_lines if line not in runbook_lines]
        problems += [f"실행서 {section}절 표 줄이 fxp_static {rows_key}에 없음: '{line}'" for line in runbook_lines if line not in static_lines]
        by_id, covered = {entry["id"]: entry for entry in entries}, []
        for row in rows:
            ids, row_problems = check_bundle_row(row, label, by_id, text_key)
            covered += ids
            problems += row_problems
        entry_ids = [entry["id"] for entry in entries]
        if covered != entry_ids:
            only_rows = [identifier for identifier in covered if identifier not in entry_ids]
            only_entries = [identifier for identifier in entry_ids if identifier not in covered]
            problems.append(f"{label} id 목록 다름: 원래 줄에만 {only_rows} · {entries_key}에만 {only_entries}(둘 다 비면 순서·중복)")
    return problems


def check_input_problems(documents):
    return [f"입력 {name}.json problems {len(document['problems'])}개(그 단계가 종료 1 상태)" for name, document in documents.items() if document["problems"]]


def check_chain(documents, own):
    problems = []
    for name, document in documents.items():
        for key, recorded in sorted(document["inputs"].items()):
            if key in own and key != document_path(name) and recorded != own[key]:
                problems.append(f"연결 불일치: {name}.json 이 읽은 {key} sha {short(recorded)} ≠ 지금 {short(own[key])}({name} 단계를 다시 돌린다)")
    rows, areas_path = documents["rows"], document_path("areas")
    areas_input = rows.get("areas_input")
    if areas_input != areas_path:
        recorded = rows["inputs"].get(areas_input) if isinstance(areas_input, str) else None
        if recorded != own[areas_path]:
            problems.append(f"연결 불일치: rows.json 영역 입력 {areas_input} sha {short(recorded)} ≠ {areas_path} {short(own[areas_path])}")
    return problems


def cross_module_warnings(documents, own):
    seen = {}
    for name, document in documents.items():
        for key, recorded in document["inputs"].items():
            if key not in own:
                seen.setdefault(key, {})[name] = recorded
    warnings = []
    for key in sorted(seen):
        if len(set(seen[key].values())) > 1:
            warnings.append(f"모듈 사이 입력 sha 다름: {key} — " + " · ".join(f"{name} {short(seen[key][name])}" for name in sorted(seen[key])))
    return warnings


def is_approved(approval):
    date = approval.get("date")
    return common.is_text(date) and approval.get("state") == f"{APPROVED_PREFIX}{date}"


def recompute_confirmed(record, p):
    basis = record["basis"]
    targets = basis.get("targets")
    all_live2 = isinstance(targets, int) and targets > 0 and basis.get("live2_image") == targets
    return is_approved(record["approval"]) and record["performance"] == p.PROPOSED_PASS and all_live2


def expected_reason_parts(record, p):
    approval, performance, basis = record["approval"], record["performance"], record["basis"]
    parts = []
    if not is_approved(approval):
        rejected = common.is_text(approval.get("date")) and str(approval.get("state") or "").startswith(REJECTED_PREFIX)
        parts.append(REJECTED_PREFIX if rejected else p.APPROVAL_PENDING)
    if performance is not None and performance.startswith(UNMET_PREFIX):
        parts.append("미충족")
    elif performance is not None and performance.startswith(PARTIAL_PREFIX):
        parts.append(f"부분(막는 측정 불가 {record.get('blocking_unmeasurable')})")
    elif performance != p.PROPOSED_PASS:
        parts.append("판정 불가")
    parts += [f"{label} {basis[key]}" for label, key in BASIS_REASONS if basis.get(key)]
    return parts


def check_equivalence_label(area, record, confirmed, p):
    label = record["equivalence"].get("label")
    if confirmed:
        return [] if label == EQUIVALENT_LABEL else [f"4.1 표기({area}): 동등 확인인데 '{label}'"]
    if record["performance"] == p.PROPOSED_PASS:
        return [] if label == p.PROPOSED_PASS_LABEL else [f"4.1 표기({area}): 성능만 충족이면 '{p.PROPOSED_PASS_LABEL}', 지금 '{label}'"]
    problems = [f"4.1 표기({area}): 동등 아닌데 '{EQUIVALENT_MARK}' 표기"] if EQUIVALENT_MARK in str(label) else []
    missing = [part for part in expected_reason_parts(record, p) if part not in str(label).split(" · ")]
    if missing:
        problems.append(f"4.1 표기({area}): 사유 빠짐 '{' · '.join(missing)}'(지금 '{label}')")
    return problems


def pr11_label(performance, p):
    if performance is None:
        return PR11_NO_INPUT
    if performance.startswith(UNMET_PREFIX) and performance.endswith(")"):
        return PR11_UNMET
    if performance.startswith(PARTIAL_PREFIX) and performance.endswith(")"):
        return f"{PR11_PARTIAL_PREFIX}{performance[len(PARTIAL_PREFIX):-1]})"
    if performance == p.PROPOSED_PASS:
        return PR11_PASS
    return None


def check_area(area, record, p):
    confirmed = recompute_confirmed(record, p)
    stated = record["equivalence"].get("confirmed")
    problems = [] if stated is confirmed else [f"4.1 재검산({area}): areas confirmed={stated} ≠ 재검산 {confirmed}(승인 ∧ 성능 충족(proposed) ∧ basis 전부 live2_image)"]
    problems += check_equivalence_label(area, record, stated is True and confirmed, p)
    expected = pr11_label(record["performance"], p)
    if expected is None:
        problems.append(f"4.7 성능 형식 모름({area}): '{record['performance']}'")
    elif record["pr11"] != expected:
        problems.append(f"4.7 PR-11 귀결({area}): '{record['pr11']}' ≠ '{expected}'")
    return confirmed, problems


def validate_area_record(area, record):
    label = f"areas.json areas.{area}"
    for key, kind in (("approval", dict), ("basis", dict), ("equivalence", dict), ("cells", dict), ("pr11", str), ("adoption_entries", int),
                      ("adopted_plans", int), ("plans", int), ("deferred_items", list)):
        require(record, key, kind, label)
    if "performance" not in record or not (record["performance"] is None or isinstance(record["performance"], str)):
        raise common.InputError(f"{label}: performance 형식(글자 또는 null)")


def area_row(area, record, confirmed):
    cells = record["cells"]
    row = {name: len(cells.get(name) or []) for name in CELL_COLUMNS}
    row.update({"area": area, "adoption_entries": record["adoption_entries"], "adopted_plans": record["adopted_plans"], "plans": record["plans"],
                "deferred_plans": len(cells.get("deferred") or []), "deferred_items": len(record["deferred_items"]),
                "other": {label: len(cells.get(name) or []) for name, label in OTHER_CELLS if cells.get(name)},
                "performance": record["performance"], "blocking_unmeasurable": record.get("blocking_unmeasurable"), "pr11": record["pr11"],
                "approval": {key: record["approval"].get(key) for key in ("state", "date", "evidence")},
                "basis": {key: record["basis"].get(key) for key in ("live2_image", "spec_proposal", "free_text", "unmarked", "targets")},
                "equivalence": record["equivalence"].get("label"), "confirmed": confirmed})
    return row


def build_areas_section(document, p):
    order = require(document, "areas_order", list, "areas.json")
    records = require(document, "areas", dict, "areas.json")
    rows, problems, recomputed, confirmed = [], [], [], []
    for area in order:
        record = records.get(area)
        if not isinstance(record, dict):
            raise common.InputError(f"areas.json: areas.{area} 없음")
        validate_area_record(area, record)
        recomputed_confirmed, area_problems = check_area(area, record, p)
        problems += area_problems
        both = recomputed_confirmed and record["equivalence"].get("confirmed") is True
        recomputed += [area] if recomputed_confirmed else []
        confirmed += [area] if both else []
        rows.append(area_row(area, record, both))
    return {"order": order, "rows": rows, "recomputed_confirmed": recomputed, "confirmed": confirmed}, problems


def other_text(other):
    count = sum(other.values())
    return f"{count}({' · '.join(f'{label} {value}' for label, value in other.items())})" if other else "0"


def basis_text(basis):
    return f"live2 {basis['live2_image']} · [제안] {basis['spec_proposal']} · 자유 문안 {basis['free_text']} · 미표기 {basis['unmarked']}(목표 {basis['targets']})"


def render_areas(section):
    lines = ["## 1. 영역별 상태 (areas, 3.2·4.1·4.7)", "",
             "| 영역 | 채택(고유) | 영향 | 검증만 | 둔감 | 채택 없음 | conflict | 채택 뒤 제거 | deferred | 그 밖 | 성능 판정 | 막는 측정 불가 | PR-11 귀결 | 승인 상태·날짜·근거 경로 | basis 분포 | 동등 표기 | 칸별 계획 |",
             "|" + "---|" * 17]
    for row in section["rows"]:
        approval = " · ".join(cell(row["approval"][key]) for key in ("state", "date", "evidence"))
        lines.append(f"| {row['area']} | {row['adoption_entries']}({row['adopted_plans']}) | {row['influence']} | {row['verify_only']} | {row['insensitive']} | {row['no_adopt']} | "
                     f"{row['conflict']} | {row['removed_after_adoption']} | 계획 {row['deferred_plans']} · 항목 {row['deferred_items']} | {other_text(row['other'])} | "
                     f"{cell(row['performance'])} | {cell(row['blocking_unmeasurable'])} | {cell(row['pr11'])} | {approval} | {basis_text(row['basis'])} | {cell(row['equivalence'])} | {row['plans']}계획 |")
    rows = section["rows"]
    total = {key: sum(row[key] for row in rows) for key in ("adoption_entries", "adopted_plans", "plans", "deferred_plans", "deferred_items", *CELL_COLUMNS)}
    other_total = sum(sum(row["other"].values()) for row in rows)
    lines.append(f"| 합계 | {total['adoption_entries']}({total['adopted_plans']}) | {total['influence']} | {total['verify_only']} | {total['insensitive']} | {total['no_adopt']} | "
                 f"{total['conflict']} | {total['removed_after_adoption']} | 계획 {total['deferred_plans']} · 항목 {total['deferred_items']} | {other_total} | — | — | — | — | — | "
                 f"동등 확인 {len(section['confirmed'])}영역 | {total['plans']}계획 |")
    lines += ["", f"- 칸별 계획 ID·판정 파일·deferred 항목: {document_path('areas')[:-5]}.md", ""]
    return lines


def flatten_strings(value):
    if isinstance(value, str):
        return [value]
    if isinstance(value, dict):
        return [text for item in value.values() for text in flatten_strings(item)]
    if isinstance(value, list):
        return [text for item in value for text in flatten_strings(item)]
    return []


def live2_column_has_value(text):
    return DIGIT_PATTERN.search(PF_LINE_PATTERN.sub("", str(text)).replace(LIVE2_NAME, "")) is not None


def check_compare_row(row):
    label = f"{row.get('area')} {row.get('row')}"
    problems = []
    if row.get("live2_same_machine") != COMPARE_SAME_MACHINE:
        problems.append(f"4.2 LIVE-2 같은 기계({label}): '{row.get('live2_same_machine')}' ≠ '{COMPARE_SAME_MACHINE}'")
    if row.get("notation") != COMPARE_NOTATION:
        problems.append(f"4.2 표기({label}): '{row.get('notation')}'")
    if live2_column_has_value(row.get("live2_public")):
        problems.append(f"4.2 공개·환산 열({label}): PF 줄 참조 밖의 숫자")
    texts = flatten_strings(row)
    problems += [f"4.2 금지 표현({label}): '{phrase}'" for phrase in COMPARE_FORBIDDEN if any(phrase in text for text in texts)]
    return problems


def interval_text(value):
    if not isinstance(value, dict):
        return "—"
    interval = value.get("interval") if isinstance(value.get("interval"), dict) else {}
    return f"{value.get('median')} ({interval.get('lower')}~{interval.get('upper')}, {interval.get('state')})"


def limit_sources_text(sources):
    return "; ".join(f"{source.get('item')}:{source.get('source')}" for source in sources or [] if isinstance(source, dict)) or "—"


def build_compare_section(document):
    rows = require(document, "rows", list, "compare.json")
    problems, table = [], []
    for row in rows:
        if not isinstance(row, dict):
            raise common.InputError("compare.json: rows 항목 형식")
        problems += check_compare_row(row)
        table.append({"area": row.get("area"), "row": row.get("row"), "metric": row.get("metric"), "d": interval_text(row.get("d")), "d_prime": interval_text(row.get("d_prime")),
                      "p95_record": row.get("p95_record_median"), "limit_ms": row.get("limit_ms"), "limit_sources": limit_sources_text(row.get("limit_sources")),
                      "p_label": row.get("p_label"), "live2_same_machine": row.get("live2_same_machine"), "live2_public": row.get("live2_public"), "notation": row.get("notation")})
    labels = document.get("label_counts") if isinstance(document.get("label_counts"), dict) else {}
    return {"rows": table, "label_counts": labels}, problems


def label_counts_text(counts):
    known = [f"{label} {counts.get(label, 0)}" for label in COMPARE_LABEL_ORDER]
    return " · ".join(known + [f"{label} {counts[label]}" for label in sorted(counts) if label not in COMPARE_LABEL_ORDER])


def render_compare(section):
    lines = ["## 2. 같은 기계 비교표 (compare, 4.2)", "",
             f"- 값은 P 요약에서 옮기기만 한다. LIVE-2 같은 기계 = {COMPARE_SAME_MACHINE}(PD-11), 대비 판정 경로 없음",
             "- 공개·환산 열 = PF 줄 참조만(D-49)",
             "- 집계(제안 예산 대비): " + label_counts_text(section["label_counts"]), "",
             "| 영역 | 행 | 지표 | d 중앙(구간) | d′ | p95 기록 | 한도 | 한도 원천(S###:줄) | 제안 예산 대비(P 판정) | LIVE-2 같은 기계 | LIVE-2 공개·환산(PF 줄) | 표기 |",
             "|" + "---|" * 12]
    for row in section["rows"]:
        lines.append("| " + " | ".join(cell(row[key]) for key in ("area", "row", "metric", "d", "d_prime", "p95_record", "limit_ms", "limit_sources", "p_label",
                                                                    "live2_same_machine", "live2_public", "notation")) + " |")
    return lines + [""]


def check_r13_row(row):
    name, label, kind = row.get("name"), row.get("label"), row.get("kind")
    prefix = R13_KIND_PREFIX.get(kind)
    problems = []
    if prefix is None or not isinstance(label, str) or not label.startswith(prefix) or not label.endswith(")"):
        problems.append(f"4.3 표기({name}): '{label}'(종류 {kind})는 표기 셋 밖")
    if row.get("live2") != R13_LIVE2:
        problems.append(f"4.3 LIVE-2 열({name}): '{row.get('live2')}' ≠ '{R13_LIVE2}'")
    return problems


def r13_value_text(row):
    parts = [f"{entry.get('row')} d {entry.get('d_median')} ({entry.get('interval_lower')}~{entry.get('interval_upper')}) · 한도 {entry.get('limit_ms')} · {entry.get('label')}"
             for entry in row.get("p_rows") or [] if isinstance(entry, dict)]
    parts += [f"{entry.get('name')} {entry.get('value')}" for entry in row.get("values") or [] if isinstance(entry, dict)]
    return " / ".join(parts) or "—"


def build_r13_section(document):
    rows = require(document, "rows", list, "r13.json")
    problems, table = [], []
    for row in rows:
        if not isinstance(row, dict):
            raise common.InputError("r13.json: rows 항목 형식")
        problems += check_r13_row(row)
        table.append({"id": row.get("id"), "name": row.get("name"), "basis": row.get("basis"), "values": r13_value_text(row), "label": row.get("label"), "live2": row.get("live2")})
    return {"rows": table}, problems


def render_r13(section):
    lines = ["## 3. R13 감사 (r13, 4.3·PD-12b)", "", "- 새 측정·한도 판정 없음, 값은 P 요약·기존 측정 인용 줄에서 옮기기만", "",
             "| 종 | 근거 | 값 원천 | 표기 | LIVE-2 |", "|---|---|---|---|---|"]
    lines += [f"| {cell(row['name'])} | {cell(row['basis'])} | {cell(row['values'])} | {cell(row['label'])} | {cell(row['live2'])} |" for row in section["rows"]]
    return lines + [""]


def check_row_equivalence(records, recomputed_areas, stated_count):
    expected = sorted(str(record.get("id")) for record in records
                      if record.get("status") == STATUS_PRESENT and record.get("area") != COMMON_AREA and record.get("area") in recomputed_areas)
    flagged = sorted(str(record.get("id")) for record in records if record.get("equivalent") is True)
    problems = []
    if expected != flagged:
        extra, missing = sorted(set(flagged) - set(expected)), sorted(set(expected) - set(flagged))
        problems.append(f"4.1 행 동등 재검산: 거짓 동등 {len(extra)}({' · '.join(extra[:10])}) · 빠진 동등 {len(missing)}({' · '.join(missing[:10])})")
    if stated_count != len(expected):
        problems.append(f"4.1 행 동등 수: rows.json equivalent_rows {stated_count} ≠ 재검산 {len(expected)}")
    return len(expected), problems


def build_rows_section(document, recomputed_areas):
    records = require(document, "rows", list, "rows.json")
    distribution = require(document, "distribution", dict, "rows.json")
    areas = require(document, "areas", dict, "rows.json")
    w8 = require(document, "w8", dict, "rows.json")
    if not all(isinstance(record, dict) for record in records):
        raise common.InputError("rows.json: rows 항목 형식")
    equivalent, problems = check_row_equivalence(records, recomputed_areas, document.get("equivalent_rows"))
    markers = document.get("markers") if isinstance(document.get("markers"), dict) else {}
    pass_list = document.get("pass_list") if isinstance(document.get("pass_list"), dict) else {}
    files = [{"file": entry.get("file"), "results": entry.get("results"), "not_success": len(entry.get("not_success") or [])}
             for entry in pass_list.get("files") or [] if isinstance(entry, dict)]
    by_area = {area: {"rows": value.get("rows"), "status": value.get("status") or {}, "demoted": value.get("demoted"), "approval": value.get("approval"),
                      "equivalent_rows": value.get("equivalent_rows")} for area, value in sorted(areas.items()) if isinstance(value, dict)}
    section = {"rows": len(records), "distribution": {key: distribution.get(key) or {} for key in ("ledger", "final")}, "demoted": list(document.get("demoted") or []),
               "markers": markers, "w8": {"closed": w8.get("closed") is True, "rows": w8.get("rows") or {}}, "equivalent_rows": equivalent,
               "pass_list": {"mode": pass_list.get("mode"), "files": files}, "areas": by_area}
    return section, problems


def render_rows(section):
    ledger, final = section["distribution"]["ledger"], section["distribution"]["final"]
    known = [key for key, _ in MARKER_LABELS]
    marker_parts = [f"{label} {section['markers'].get(key, 0)}" for key, label in MARKER_LABELS] + [f"{key} {section['markers'][key]}" for key in sorted(section["markers"]) if key not in known]
    files = " · ".join(f"{entry['file']} 결과 {entry['results']}·성공 아님 {entry['not_success']}" for entry in section["pass_list"]["files"]) or "없음"
    lines = ["## 4. 대조표 행 재판정 (rows, 3.4·4.4)", "",
             f"- 장부 {section['rows']}행, 상태(장부 → 강등 뒤): " + " · ".join(f"{status} {ledger.get(status, 0)} → {final.get(status, 0)}" for status in STATUS_ORDER),
             f"- PD-16 강등 {len(section['demoted'])}: {' · '.join(section['demoted']) or '없음'}",
             "- 표지(상태와 따로, 구현 ≠ 출하 동등): " + " · ".join(marker_parts),
             f"- 통과 목록 {cell(section['pass_list']['mode'])}: {files}",
             f"- 행 동등(강등 뒤 있음 ∧ 영역 동등, 공통 행 제외): {section['equivalent_rows']}행",
             f"- W-8 {'닫힘' if section['w8']['closed'] else '열림'}: " + " · ".join(f"{row} = {label}" for row, label in sorted(section["w8"]["rows"].items())), "",
             "| 영역 | 행 | 있음 | 부분 | 없음 | 다름 | 해당 없음 | 강등 | 승인 | 동등 행 |", "|" + "---|" * 10]
    for area, value in section["areas"].items():
        lines.append(f"| {area} | {cell(value['rows'])} | " + " | ".join(str(value["status"].get(status, 0)) for status in STATUS_ORDER)
                     + f" | {cell(value['demoted'])} | {cell(value['approval'])} | {cell(value['equivalent_rows'])} |")
    return lines + ["", f"- 행별 상태·앵커·표지: {document_path('rows')[:-5]}.md", ""]


def check_excluded(excluded):
    problems = []
    for level_id, entry in sorted(excluded.items()):
        if not isinstance(entry, dict) or not common.is_text(entry.get("evidence")) or not common.is_text(entry.get("reason")) or entry.get("valid") is not True:
            problems.append(f"4.4 excluded 근거({level_id}): 근거·사유가 없거나 levels 판정 valid 아님")
    return problems


def build_levels_section(document):
    counts = require(document, "counts", dict, "levels.json")
    excluded = require(document, "excluded", dict, "levels.json")
    levels = require(document, "levels", list, "levels.json")
    names = {entry.get("id"): entry.get("name") for entry in levels if isinstance(entry, dict)}
    excluded_rows = [{"id": level_id, "name": names.get(level_id), "kind": entry.get("kind"), "reason": entry.get("reason"), "evidence": entry.get("evidence"),
                      "follow_up": entry.get("follow_up")} for level_id, entry in sorted(excluded.items()) if isinstance(entry, dict)]
    not_loose = [{"id": entry.get("id"), "name": entry.get("name")} for entry in levels
                 if isinstance(entry, dict) and entry.get("target") is True and not entry.get("excluded") and entry.get("loose") is not True]
    keys = ("levels", "targets", "scope", "loose", "strict_mean", "strict_items", "items", "excluded", "manual_refs", "unknown_ids", "aliased")
    return {"counts": {key: counts.get(key) for key in keys}, "excluded": excluded_rows, "not_loose": not_loose}, check_excluded(excluded)


def render_levels(section):
    counts = section["counts"]
    lines = ["## 5. 58레벨 대응 (levels, 3.5·4.4)", "",
             f"- 레벨 {counts['levels']}(대상 {counts['targets']}, 범위 {counts['scope']} = 대상 − excluded {counts['excluded']})",
             f"- 느슨(맵 ≥1) {counts['loose']}/{counts['scope']} · 엄격 평균 {counts['strict_mean']}(셋업 항목 {counts['strict_items']}/{counts['items']})",
             f"- manual 참조 {counts['manual_refs']} · 모르는 ID {counts['unknown_ids']} · 별칭 {counts['aliased']}",
             "- 맵 없는 범위 안 레벨: " + (" · ".join(f"{entry['id']} {entry['name']}" for entry in section["not_loose"]) or "없음"), "",
             "| excluded 레벨 | 이름 | 종류 | 사유 | 근거 | 후속 |", "|---|---|---|---|---|---|"]
    lines += [f"| {cell(row['id'])} | {cell(row['name'])} | {cell(row['kind'])} | {cell(row['reason'])} | {cell(row['evidence'])} | {cell(row['follow_up'])} |" for row in section["excluded"]]
    return lines + ["", f"- 레벨별 맵·셋업 항목: {document_path('levels')[:-5]}.md", ""]


def yes_no(value):
    return {True: "예", False: "아니오"}.get(value, "—")


def build_package_section(documents):
    entries, legend, records = [], {}, {}
    for name in ("package-min", "package-fluid"):
        document = documents[name]
        checks = [entry for entry in document.get("checks") or [] if isinstance(entry, dict)]
        legend.update({entry.get("id"): entry.get("title") for entry in checks if entry.get("id") not in legend})
        cook = document.get("cook") if isinstance(document.get("cook"), dict) else {}
        launch = document.get("launch") if isinstance(document.get("launch"), dict) else {}
        records = records or (document.get("records") if isinstance(document.get("records"), dict) else {})
        entries.append({"tag": document.get("tag"), "verdict": document.get("verdict"), "branch": document.get("branch"),
                        "checks": {entry.get("id"): entry.get("status") for entry in checks}, "cook_ok": cook.get("ok"), "launch_present": launch.get("present")})
    return {"entries": entries, "legend": legend, "records": records}


def render_package(section):
    ids = sorted(section["legend"])
    lines = ["## 6. 패키지 첫 읽기 (package, 3.7·PD-13)", "",
             "- 점검: " + (" · ".join(f"{check_id} {section['legend'][check_id]}" for check_id in ids) or "없음"),
             "- 기록만: " + (" · ".join(f"{key} {cell(value)}" for key, value in sorted(section["records"].items())) or "없음"), "",
             "| 패키지 | 판정 | " + " | ".join(ids) + " | 쿠킹 성공 | 기동 결과 |", "|" + "---|" * (len(ids) + 4)]
    for entry in section["entries"]:
        lines.append(f"| {cell(entry['tag'])} | {cell(entry['verdict'])} | " + " | ".join(cell(entry["checks"].get(check_id)) for check_id in ids)
                     + f" | {yes_no(entry['cook_ok'])} | {yes_no(entry['launch_present'])} |")
    return lines + [""]


def build_items_section(document):
    items = require(document, "items", list, "items.json")
    class_counts = document.get("class_counts") if isinstance(document.get("class_counts"), dict) else {}
    entries = (document.get("unmeasurable") or {}).get("entries") if isinstance(document.get("unmeasurable"), dict) else []
    groups = {}
    for entry in entries or []:
        if isinstance(entry, dict):
            groups[entry.get("group")] = groups.get(entry.get("group"), 0) + 1
    table = [{"id": item.get("id"), "class": item.get("class"), "title": item.get("title"), "status": item.get("status"), "refs": list(item.get("refs") or [])}
             for item in items if isinstance(item, dict)]
    links = {}
    for key in ("follow_ups", "user_items"):
        links.update({entry.get("id"): list(entry.get("linked_items") or []) for entry in document.get(key) or [] if isinstance(entry, dict)})
    return {"items": table, "class_counts": class_counts, "unmeasurable": {str(key): value for key, value in groups.items()}, "links": links}


def render_items(section):
    groups = section["unmeasurable"]
    group_parts = [f"{group} {groups.get(group, 0)}" for group in UNMEASURABLE_GROUPS] + [f"{group} {groups[group]}" for group in sorted(groups) if group not in UNMEASURABLE_GROUPS]
    lines = ["## 7. 넘어온 항목 (items, 3.3)", "",
             "- 분류: " + " · ".join(f"{label} {section['class_counts'].get(key, 0)}" for key, label in ITEM_CLASS_LABELS),
             f"- 측정 불가 {sum(groups.values())}: " + " · ".join(group_parts), "",
             "| 항목 | 분류 | 제목 | 상태 | 연결 |", "|---|---|---|---|---|"]
    lines += [f"| {cell(item['id'])} | {cell(item['class'])} | {cell(item['title'])} | {cell(item['status'])} | {cell(' · '.join(item['refs']) or None)} |" for item in section["items"]]
    return lines + ["", f"- 근거 줄 대조: {document_path('items')[:-5]}.md", ""]


def build_static_tables(static, links):
    follow_ups = [{"id": entry["id"], "item": entry.get("item"), "source": entry.get("source"), "linked_items": links.get(entry["id"], [])} for entry in static["follow_ups"]]
    user_items = [{"id": entry["id"], "ask": entry.get("ask"), "source": entry.get("source"), "record_only": entry.get("record_only") is True,
                   "linked_items": links.get(entry["id"], [])} for entry in static["user_items"]]
    return follow_ups, user_items


def render_static_tables(follow_ups, user_items):
    lines = ["## 8. 후속 후보 FU (5절, FX-Q 제안·PD-2)", "", "| FU | 항목 | 원천 | 연결 항목 |", "|---|---|---|---|"]
    lines += [f"| {entry['id']} | {cell(entry['item'])} | {cell(entry['source'])} | {cell(' · '.join(entry['linked_items']) or None)} |" for entry in follow_ups]
    lines += ["", "## 9. 사용자 대기 FQ (6절, FX-P를 막지 않음·PD-18)", "", "| FQ | 묻는 것 | 근거 | 기록만 | 연결 항목 |", "|---|---|---|---|---|"]
    lines += [f"| {entry['id']} | {cell(entry['ask'])} | {cell(entry['source'])} | {yes_no(entry['record_only'])} | {cell(' · '.join(entry['linked_items']) or None)} |"
              for entry in user_items]
    return lines + [""]


def fill(template, replacements, key):
    text = template
    for old, new in replacements:
        if old not in text:
            raise common.InputError(f"{common.FXP_STATIC_PATH}: {key} 자리 표시 '{old}' 없음")
        text = text.replace(old, new)
    return text


def build_golden_line(static, golden, areas_document):
    adoptions = require(golden, "adoptions", list, "golden.json")
    discrete = require(golden, "discrete_adoptions", list, "golden.json")
    golden_path = require(golden, "golden_path_adoptions", list, "golden.json")
    line = fill(static["golden_line_template"], (("<n>", str(len(adoptions))), ("<k>", str(len(discrete))), ("<m>", str(len(golden_path)))), "golden_line_template")
    problems = [] if golden.get("golden_line") == line else [f"4.6 골든 줄: golden.json '{golden.get('golden_line')}' ≠ 개수로 채운 '{line}'"]
    totals = areas_document.get("totals") if isinstance(areas_document.get("totals"), dict) else {}
    if totals.get("adoption_entries") != len(adoptions):
        problems.append(f"4.6 골든 채택 수 {len(adoptions)} ≠ areas 채택 {totals.get('adoption_entries')}(영향 f5 제외 채택이어야 한다)")
    types = golden.get("type_counts") if isinstance(golden.get("type_counts"), dict) else {}
    discrete_types = sum(types.get(name, 0) for name in ("열거", "불", "정수"))
    if types and discrete_types != len(discrete):
        problems.append(f"4.6 골든 열거·불·정수 {len(discrete)} ≠ 형식 집계 {discrete_types}")
    return line, problems


def d49_problems(lines):
    problems = []
    for number, line in enumerate(lines, 1):
        if D49_NUMBER_PATTERN.search(line):
            problems.append(f"D-49 공개 md {number}줄: LIVE-2 뒤 40자 안 수치·단위")
        if LIVE2_REFERENCE in line.lower():
            problems.append(f"D-49 공개 md {number}줄: 비교 원본 폴더 이름")
        if IMAGE_PATTERN.search(line):
            problems.append(f"D-49 공개 md {number}줄: 이미지 확장자")
    return problems


def render_inputs(record, own, documents, chain_count, warnings):
    lines = ["## 10. 입력·실행서 sha", "", f"- 실행서 sha 기록 {record['lines']}줄(마지막 줄만 대조, HEAD는 기록만)", "", "| 줄 | 실행서 | 실행서 sha | defs sha | HEAD |", "|---|---|---|---|---|"]
    lines += [f"| {cell(entry['index'])} | {cell(entry['runbook'])} | {short(entry['runbook_sha256'])} | {short(entry['defs_sha256'])} | {short(entry['head'])} |" for entry in record["history"]]
    current = record["current"]
    lines += ["", f"- 대조: 실행서 {current['runbook']} {short(current['runbook_sha256'])} {'일치' if record['runbook_match'] else '불일치'} · "
                  f"정의 묶음 {current['defs']} {short(current['defs_sha256'])} {'일치' if record['defs_match'] else '불일치'}", "",
              "| 입력 | sha256(앞 12자) | 단계 요약 |", "|---|---|---|"]
    summaries = {document_path(name): document["summary"] for name, document in documents.items()}
    lines += [f"| {key} | {short(own[key], 12)} | {cell(summaries.get(key))} |" for key in sorted(own)]
    lines += ["", f"- 연결 대조(앞 단계 출력이 읽은 보고 입력 sha = 지금): 불일치 {chain_count}", f"- 모듈 사이 입력 sha 다름(경고, 종료에 안 셈): {len(warnings)}"]
    lines += [f"  - {warning}" for warning in warnings]
    return lines + [""]


def render_checks(check_counts):
    lines = ["## 11. 보고 검사", "", "| 검사 | 문제 |", "|---|---|"]
    lines += [f"| {label} | {check_counts[key]} |" for key, label in CHECK_LABELS]
    return lines + [""]


def summary_text(areas, compare, r13, rows, levels, package, items, static, record, check_counts, warnings):
    branches = {entry["tag"]: entry["branch"] for entry in package["entries"]}
    counts = levels["counts"]
    return (f"areas={len(areas['rows'])} equivalence_confirmed={len(areas['confirmed'])} compare_rows={len(compare['rows'])} r13_rows={len(r13['rows'])} "
            f"ledger_rows={rows['rows']} demoted={len(rows['demoted'])} w8={'closed' if rows['w8']['closed'] else 'open'} equivalent_rows={rows['equivalent_rows']} "
            f"levels={counts['levels']} targets={counts['targets']} loose={counts['loose']}/{counts['scope']} strict_mean={counts['strict_mean']} "
            f"package_min={branches.get('min')} package_fluid={branches.get('fluid')} items={len(items['items'])} follow_ups={len(static['follow_ups'])} "
            f"user_items={len(static['user_items'])} runbook_sha={short(record['last']['runbook_sha256'])} sha_lines={record['lines']} "
            f"stale={check_counts['chain']} input_mismatch={len(warnings)} d49={check_counts['d49']}")


def build(context):
    reads, args, p = context.reads, context.args, common.p_plans()
    if not DATE_PATTERN.fullmatch(args.date or ""):
        raise common.InputError(f"--date 형식 YYYY-MM-DD: {args.date}")
    documents = read_input_documents(reads)
    static = read_static(reads)
    record, runbook_text = read_runbook_record(reads, args)
    own = reads.snapshot()
    checks = {"runbook_sha": check_runbook_record(record), "input_problems": check_input_problems(documents), "chain": check_chain(documents, own),
              "runbook_phrases": check_runbook_phrases(runbook_text, static, p), "static_tables": check_static_tables(runbook_text, static)}
    warnings = cross_module_warnings(documents, own)
    areas, checks["areas"] = build_areas_section(documents["areas"], p)
    compare, checks["compare"] = build_compare_section(documents["compare"])
    r13, checks["r13"] = build_r13_section(documents["r13"])
    rows, checks["rows"] = build_rows_section(documents["rows"], set(areas["recomputed_confirmed"]))
    levels, checks["levels"] = build_levels_section(documents["levels"])
    golden_line, checks["golden"] = build_golden_line(static, documents["golden"], documents["areas"])
    package = build_package_section(documents)
    items = build_items_section(documents["items"])
    follow_ups, user_items = build_static_tables(static, items["links"])
    header = fill(static["header_template"], (("동등 확인 n영역", f"동등 확인 {len(areas['confirmed'])}영역"), ("<8자>", short(record["last"]["runbook_sha256"])),
                                              ("기록 n줄", f"기록 {record['lines']}줄")), "header_template")
    title = f"{TITLE_PREFIX}{args.date}, claude)"
    body = [header, "", f"- 골든: {golden_line}", f"- 환경 편차(4.5): {static['environment_note']}",
            "- 동등 규칙(4.1): 승인(A19) ∧ 성능 충족(proposed) ∧ 영역 목표 basis 전부 live2_image, 행 동등 = 강등 뒤 있음 ∧ 영역 동등(공통 행 제외)", ""]
    body += render_areas(areas) + render_compare(compare) + render_r13(r13) + render_rows(rows) + render_levels(levels) + render_package(package)
    body += render_items(items) + render_static_tables(follow_ups, user_items)
    body += render_inputs(record, own, documents, len(checks["chain"]), warnings)
    public_head = [title, LESSON_LINE, COMMAND_LINE, ""]
    checks["d49"] = d49_problems(public_head + body)
    check_counts = {key: len(checks[key]) for key, _ in CHECK_LABELS}
    body += render_checks(check_counts)
    problems = [problem for key, _ in CHECK_LABELS for problem in checks[key]]
    summary = summary_text(areas, compare, r13, rows, levels, package, items, static, record, check_counts, warnings)
    public_bytes = common.markdown_bytes(public_head + body, f"SUMMARY {summary} problems={len(problems)}")
    document = {"version": 1, "date": args.date, "title": title, "header": header, "golden_line": golden_line, "environment_note": static["environment_note"],
                "runbook_record": record, "areas": areas, "compare": compare, "r13": r13, "rows": rows, "levels": levels, "package": package,
                "items": {key: items[key] for key in ("items", "class_counts", "unmeasurable")}, "follow_ups": follow_ups, "user_items": user_items,
                "sources": {name: {"path": document_path(name), "summary": value["summary"]} for name, value in documents.items()},
                "checks": check_counts, "warnings": warnings, "public_md": {"sha256": common.sha256_bytes(public_bytes), "lines": public_bytes.decode("utf-8").count("\n")},
                "rule": {"inputs": "3.8 고정 목록(glob 아님, fxp-audit* 제외), 빠지면 2", "sha": "x0-runbook.sha256 마지막 줄의 실행서·defs sha만 대조, HEAD는 기록만",
                         "chain": "앞 단계 출력의 inputs 중 이 보고도 읽은 파일 sha는 지금과 같아야 한다(rows 영역 입력은 sha로)",
                         "static_tables": "fxp_static follow_up_rows·user_item_rows 줄 '| no | 항목 | 원천 |' = 실행서 5절·6절 표 줄(양방향), id별 항목 = 원래 줄 조각(' · ' 묶음, n~m 은 '·' 이음 또는 'n 항목' 나열, 줄 앞 '(기록만…)' = record_only)",
                         "d49": "공개 md: LIVE-2 뒤 40자 안 수치·단위, 비교 원본 폴더 이름, 이미지 확장자 금지"}}
    extra = {args.public_md: public_bytes} if args.public_md else {}
    return common.Outcome(document, [title, ""] + body, summary, problems, extra)
