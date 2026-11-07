"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), fxp_audit.py 하위 명령 모듈이 import 해서 쓴다(단독 실행 없음).
실행 명령: 없음(python -B Tools/FluidFX/fxp/fxp_audit.py <하위 명령> --out <json> [--md <md>] [--root <저장소 사본>] 이 run_module 로 부른다).
출력: 하위 명령 JSON(정렬 키, 최상위 command·inputs{루트 상대 경로: sha256|null}·problems·summary)과 md(끝 줄 SUMMARY …), 임시 파일 → fsync → os.replace. 종료 0 성공 / 1 검사 실패(출력은 씀) / 2 입력 오류(출력 안 씀).
상태: 2026-11-07 FX-P X1a 뼈대(runbook-fxp-v3 3.1): 입력 기록기·원자적 쓰기·절대 경로 검사·p_plans/check_live2_coverage import·input_ 접두 영역·일지 합침 PD-6·fxp_static 인용 검사. 시험 python -B Tools/FluidFX/fxp/check_fxp_fixtures.py.
"""
import hashlib
import importlib
import json
import os
import re
import sys
from pathlib import Path

PERF_TOOL_RELATIVE = Path("Tools/FluidFX/perf/p_plans.py")
P_SUMMARY_PATH = "Saved/AgentOps/20261106/p/p-summary.json"
FXP_STATIC_PATH = "Tools/FluidFX/fxp/fxp_static.json"
ITEMS_STATIC_PATH = "Tools/FluidFX/fxp/items_static.json"
FXP_OUTPUT_DIR = "Saved/FluidFX/fxp"
APPROVALS_PATH = "Saved/FluidFX/approval/approvals.json"
APPROVAL_DIR = "Saved/FluidFX/approval"
MAIN_QUEUE_PATH = "Tools/FluidFX/calib/batch_queue.json"
CALIB_PLANS_DIR = "Tools/FluidFX/calib/plans"
RETIRED_PLANS_DIR = "Tools/FluidFX/calib/retired"
APPROVAL_ONLY_PATH = "Tools/FluidFX/calib/approval-only.json"
STRUCTURAL_GAPS_PATH = "Tools/FluidFX/calib/structural-gaps.json"
PERF_CANDIDATES_PATH = "Saved/FluidFX/calib/perf-candidates.json"
INPUT_PLAN_PREFIX = "input_"
INPUT_AREA = "input"
JOURNAL_PENDING = "pending"
JOURNAL_FILE_PREFIX = "journal-"
BATCH_DIRECTORY_PREFIX = "batch-"
BATCH_DIRECTORY_NAME = "batch"
EXIT_OK = 0
EXIT_CHECK = 1
EXIT_INPUT = 2


def find_repo_root(start):
    for candidate in (start, *start.parents):
        if (candidate / PERF_TOOL_RELATIVE).is_file():
            return candidate
    raise RuntimeError(f"저장소 루트를 못 찾음({PERF_TOOL_RELATIVE} 없음): {start}")


REPO_ROOT = find_repo_root(Path(__file__).resolve().parent)


class InputError(Exception):
    pass


class Outcome:
    def __init__(self, document, markdown, summary, problems=(), extra_files=None):
        self.document = document
        self.markdown = list(markdown)
        self.summary = summary
        self.problems = list(problems)
        self.extra_files = dict(extra_files or {})


class Context:
    def __init__(self, root, args, reads):
        self.root = root
        self.args = args
        self.reads = reads


def is_text(value):
    return isinstance(value, str) and bool(value.strip())


def normalize_relative(root, value):
    text = str(value).replace("\\", "/")
    candidate = Path(text)
    if candidate.is_absolute():
        try:
            return candidate.resolve().relative_to(Path(root).resolve()).as_posix()
        except ValueError:
            return candidate.as_posix()
    while text.startswith("./"):
        text = text[2:]
    return text


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


class InputRecorder:
    def __init__(self, root):
        self.root = Path(root)
        self.hashes = {}
        self.cache = {}

    def relative(self, value):
        return normalize_relative(self.root, value)

    def exists(self, value):
        return (self.root / self.relative(value)).is_file()

    def read_bytes(self, value, required=True):
        relative = self.relative(value)
        if relative in self.cache:
            data = self.cache[relative]
            if data is None and required:
                raise InputError(f"입력 없음: {relative}")
            return data
        target = self.root / relative
        if not target.is_file():
            if required:
                raise InputError(f"입력 없음: {relative}")
            self.hashes.setdefault(relative, None)
            self.cache[relative] = None
            return None
        data = target.read_bytes()
        self.hashes[relative] = sha256_bytes(data)
        self.cache[relative] = data
        return data

    def read_text(self, value, required=True):
        data = self.read_bytes(value, required)
        if data is None:
            return None
        try:
            return data.decode("utf-8-sig")
        except UnicodeDecodeError as error:
            raise InputError(f"UTF-8 아님: {self.relative(value)} ({error})")

    def read_json(self, value, required=True):
        text = self.read_text(value, required)
        if text is None:
            return None
        try:
            return json.loads(text)
        except ValueError as error:
            raise InputError(f"JSON 읽기 실패: {self.relative(value)} ({error})")

    def glob(self, pattern):
        return sorted({path.relative_to(self.root).as_posix() for path in self.root.glob(pattern) if path.is_file()})

    def snapshot(self):
        return {key: self.hashes[key] for key in sorted(self.hashes)}


def load_tool_module(relative_directory, module_name):
    directory = str(REPO_ROOT / relative_directory)
    if directory not in sys.path:
        sys.path.insert(0, directory)
    return importlib.import_module(module_name)


def p_plans():
    return load_tool_module("Tools/FluidFX/perf", "p_plans")


def live2_coverage():
    return load_tool_module("Tools/FluidFX", "check_live2_coverage")


def plan_stem(value):
    name = str(value).replace("\\", "/").rsplit("/", 1)[-1]
    return name[:-5] if name.endswith(".json") else name


def base_plan(value):
    return p_plans().base_plan_id(plan_stem(value))


def plan_area(value):
    base = base_plan(value)
    area = p_plans().plan_area(base)
    if area is None and base.startswith(INPUT_PLAN_PREFIX):
        return INPUT_AREA
    return area


def sheet_areas():
    return tuple(p_plans().SHEET_AREAS)


def natural_key(text):
    return tuple((0, int(part), "") if part.isdigit() else (1, 0, part) for part in re.split(r"(\d+)", text) if part)


def journal_batch_name(relative):
    parts = relative.split("/")
    parent, stem = parts[-2] if len(parts) > 1 else "", plan_stem(parts[-1])
    if parent == BATCH_DIRECTORY_NAME:
        return stem[len(JOURNAL_FILE_PREFIX):] if stem.startswith(JOURNAL_FILE_PREFIX) else stem
    return parent[len(BATCH_DIRECTORY_PREFIX):] if parent.startswith(BATCH_DIRECTORY_PREFIX) else parent


def journal_first_hold(document):
    times = [hold["acquired_at"] for hold in document.get("holds") or [] if isinstance(hold, dict) and is_text(hold.get("acquired_at"))]
    return min(times) if times else None


class JournalEntry:
    def __init__(self, path, document):
        self.path = path
        self.document = document
        self.batch = journal_batch_name(path)
        self.first_hold = journal_first_hold(document)

    def order_key(self):
        return (self.first_hold is None, self.first_hold or "", natural_key(self.batch), self.path)


class JournalMerge:
    def __init__(self, entries, final_by_base, final_by_stem, seen_stems):
        self.entries = entries
        self.final_by_base = final_by_base
        self.final_by_stem = final_by_stem
        self.seen_stems = seen_stems

    def order(self):
        return [{"journal": entry.path, "batch": entry.batch, "first_hold": entry.first_hold} for entry in self.entries]


def journal_record(entry, item, stem):
    runs = item.get("runs") if isinstance(item.get("runs"), dict) else {}
    return {"status": item["status"], "reason": item.get("reason") if isinstance(item.get("reason"), str) else "", "journal": entry.path, "batch": entry.batch,
            "stem": stem, "runs": {key: runs[key] for key in sorted(runs) if isinstance(runs[key], str)}}


def merge_journals(reads):
    entries = []
    for relative in reads.glob(p_plans().PB2_FXP_SOURCE_GLOBS[0]):
        document = reads.read_json(relative)
        if not isinstance(document, dict) or not isinstance(document.get("items"), list):
            raise InputError(f"일지 형식(items 목록 필요): {relative}")
        entries.append(JournalEntry(relative, document))
    entries.sort(key=JournalEntry.order_key)
    final_by_base, final_by_stem, seen_stems = {}, {}, set()
    for entry in entries:
        for item in entry.document["items"]:
            if not isinstance(item, dict) or not is_text(item.get("plan")) or not is_text(item.get("status")):
                raise InputError(f"일지 항목 형식(plan·status 필요): {entry.path}")
            stem = plan_stem(item["plan"])
            seen_stems.add(stem)
            if item["status"] == JOURNAL_PENDING:
                continue
            record = journal_record(entry, item, stem)
            final_by_base[base_plan(stem)] = record
            final_by_stem[stem] = record
    return JournalMerge(entries, final_by_base, final_by_stem, seen_stems)


def load_fxp_static(reads):
    static = reads.read_json(FXP_STATIC_PATH)
    if not isinstance(static, dict) or not isinstance(static.get("follow_ups"), list) or not isinstance(static.get("user_items"), list):
        raise InputError(f"{FXP_STATIC_PATH} 형식(follow_ups·user_items 목록 필요)")
    source = static.get("quote_source")
    if not isinstance(source, dict) or not is_text(source.get("file")) or not isinstance(source.get("line"), int):
        raise InputError(f"{FXP_STATIC_PATH} quote_source{{file, line}} 필요")
    for entry in static["follow_ups"] + static["user_items"]:
        if not isinstance(entry, dict) or not is_text(entry.get("id")):
            raise InputError(f"{FXP_STATIC_PATH} 항목에 id 필요")
    return static


def read_source_line(reads, file, line):
    text = reads.read_text(file, required=False)
    if text is None or not isinstance(line, int) or line < 1:
        return None
    lines = text.splitlines()
    return lines[line - 1] if line <= len(lines) else None


def check_static_quotes(reads, static):
    source = static["quote_source"]
    line = read_source_line(reads, source["file"], source["line"])
    problems = []
    for entry in static["user_items"]:
        quote = entry.get("quote")
        if quote is None:
            continue
        if not is_text(quote) or line is None or quote not in line:
            problems.append(f"FQ 인용 불일치: {entry['id']} '{quote}'가 {source['file']}:{source['line']}의 부분 문자열이 아님")
    return problems


def static_ids(static):
    return {entry["id"] for entry in static["follow_ups"]} | {entry["id"] for entry in static["user_items"]}


def check_evidence_line(reads, evidence):
    line = read_source_line(reads, evidence.get("file"), evidence.get("line"))
    return line is not None and is_text(evidence.get("expect")) and evidence["expect"] in line


def markdown_cell(value):
    if value is None:
        return "—"
    return str(value).replace("|", "/").replace("\n", " ")


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def markdown_bytes(lines, summary):
    return ("\n".join(list(lines) + ["", summary]) + "\n").encode("utf-8")


def write_atomic(path, data):
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.parent / f".{target.name}.tmp-{os.getpid()}"
    with open(temporary, "wb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, target)


def absolute_root_markers(root):
    markers = set()
    for path in {Path(root).resolve(), REPO_ROOT}:
        native = str(path)
        markers |= {native.lower(), native.replace("\\", "/").lower(), native.replace("\\", "\\\\").lower()}
    return sorted(marker for marker in markers if len(marker) > 3)


def contains_absolute_root(data, root):
    text = data.decode("utf-8", errors="replace").lower()
    return any(marker in text for marker in absolute_root_markers(root))


def strip_root(text, root):
    if not isinstance(text, str):
        return text
    result = text
    for marker in sorted(absolute_root_markers(root), key=len, reverse=True):
        result = re.sub(re.escape(marker) + r"[\\/]*", "", result, flags=re.IGNORECASE)
    return result


def add_common_arguments(parser):
    parser.add_argument("--root", default=None, help="저장소 사본 루트(fixture용, 기본 저장소 루트)")
    parser.add_argument("--out", required=True, help="출력 JSON")
    parser.add_argument("--md", default=None, help="출력 md(끝 줄 SUMMARY)")


def resolve_root(value):
    return Path(value).resolve() if value else REPO_ROOT


def configure_console():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")


def build_outputs(name, outcome, reads, problems):
    summary = f"SUMMARY {outcome.summary} problems={len(problems)}"
    document = dict(outcome.document)
    document.update({"command": name, "inputs": reads.snapshot(), "problems": list(problems), "summary": summary})
    return summary, json_bytes(document), markdown_bytes(outcome.markdown, summary)


def run_module(name, module, args):
    configure_console()
    root = resolve_root(args.root)
    if not root.is_dir():
        print(f"입력 오류({name}): --root 폴더 없음 {args.root}", file=sys.stderr)
        return EXIT_INPUT
    reads = InputRecorder(root)
    try:
        outcome = module.build(Context(root, args, reads))
    except InputError as error:
        print(f"입력 오류({name}): {error}", file=sys.stderr)
        return EXIT_INPUT
    problems = list(outcome.problems)
    summary, json_data, markdown_data = build_outputs(name, outcome, reads, problems)
    extra_outputs = {key: value for key, value in outcome.extra_files.items()}
    if any(contains_absolute_root(data, root) for data in (json_data, markdown_data, *extra_outputs.values())):
        problems.append("출력에 절대 경로가 있다(3.1: 루트 상대만)")
        summary, json_data, markdown_data = build_outputs(name, outcome, reads, problems)
    write_atomic(args.out, json_data)
    if args.md:
        write_atomic(args.md, markdown_data)
    for path, data in sorted(extra_outputs.items()):
        write_atomic(path, data)
    for problem in problems:
        print(f"문제: {problem}")
    print(summary)
    return EXIT_CHECK if problems else EXIT_OK
