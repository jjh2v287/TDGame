"""시스템 Python으로 실행: Source/와 Plugins/*/Source/의 C++에서 동기 에셋 로드 호출을 찾아, 허용 범위(에디터 모듈, WITH_EDITOR·WITH_EDITORONLY_DATA 블록, Tests 폴더 파일의 자동화 시험 블록, 기준 목록) 밖의 새 지점과 사라진 기준 목록 항목을 실패로 보고한다(OP-33).
실행: python Tools/check_sync_loads.py [--list-all] [--report <JSON 경로>] [--root <프로젝트 루트>] (PowerShell 또는 Git Bash, 에디터 불필요)
출력: 표준 출력(NEW·STALE 목록과 SUMMARY 한 줄), --report 지정 시 JSON(관례 Saved/AgentOps/<YYYYMMDD>/sync_loads.json). 종료 코드 0 위반 없음 / 1 NEW 또는 STALE / 2 입력 오류
상태: 현행
"""
import argparse
import json
import re
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".inl"}
EDITOR_MODULE_TYPES = {"Editor", "EditorNoCommandlet", "EditorAndProgram", "UncookedOnly", "Developer"}
ALWAYS_ALLOWED_GUARD_NAMES = {"WITH_EDITOR", "WITH_EDITORONLY_DATA"}
TEST_FILE_ALLOWED_GUARD_NAMES = {"WITH_DEV_AUTOMATION_TESTS", "WITH_AUTOMATION_TESTS"}
RAW_STRING_PREFIXES = {"R", "LR", "uR", "UR", "u8R"}
DIGIT_CHARACTERS = "0123456789"
HIT_CATEGORIES = ("EDITOR", "GUARDED", "BASELINE", "NEW")
STALE_GUIDANCE = "기준 목록 항목이 더 이상 없음: 목록에서 지운다"

COMMENT_OR_LITERAL_START = re.compile(r"//|/\*|[\"']")
RAW_STRING_OPENING = re.compile(r'"([^ ()\\\t\v\f\n]{0,16})\(')
NON_NEWLINE_CHARACTER = re.compile(r"[^\n]")
WHITESPACE_RUN = re.compile(r"\s+")
DIRECTIVE = re.compile(r"\s*#\s*(\w+)(.*)", re.DOTALL)
ALLOWED_GUARD_TERM = re.compile(r"([A-Za-z_]\w*)(?:\s*==\s*1|\s*!=\s*0)?")

SYNC_LOAD_PATTERNS = [
    ("load-synchronous", re.compile(r"(\.|->)\s*LoadSynchronous\s*(<[^;()]*>)?\s*\(")),
    ("load-object", re.compile(r"\b(Static)?LoadObject\s*[<(]")),
    ("load-class", re.compile(r"\b(Static)?LoadClass\s*[<(]")),
    ("try-load", re.compile(r"(\.|->)\s*TryLoad(Class\s*<[^>]*>)?\s*\(")),
    ("constructor-helpers", re.compile(r"\bConstructorHelpers\s*::\s*F(Object|Class)Finder")),
    ("load-package", re.compile(r"\bLoadPackage\s*\(")),
    ("asset-data-get-asset", re.compile(r"(\.|->)\s*GetAsset\s*\(|(\.|->)\s*FastGetAsset\s*\(\s*true\b")),
    ("flush-async-loading", re.compile(r"\bFlushAsyncLoading\s*\(")),
    ("request-sync-load", re.compile(r"\bRequestSyncLoad\s*\(")),
    ("load-asset-blocking", re.compile(r"\bLoad(Class)?Asset_Blocking\s*\(")),
    ("wait-until-complete", re.compile(r"(\.|->)\s*WaitUntilComplete\s*\(")),
    ("level-streaming-block", re.compile(r"\b(FlushLevelStreaming|BlockTillLevelStreamingCompleted)\s*\(|\bbShouldBlockOnLoad\s*=\s*true\b")),
    ("object-library-load", re.compile(r"\bLoadAssetsFrom(Paths?|AssetData)\s*\(")),
]

BASELINE_ENTRIES = [
    ("Source/TDGame/Characters/TDGameCharacter.cpp", "ConstructorHelpers::FObjectFinder<USkeletalMesh> DefaultPlayerMesh", "constructor", "CDO 기본 메시, 생성자 안 비동기는 엔진이 동기로 바꿈"),
    ("Source/TDGame/Combat/Damage/TDDamageTarget.cpp", "ConstructorHelpers::FObjectFinder<UStaticMesh> Cube", "constructor", "엔진 기본 도형, 시험장 더미"),
    ("Source/TDGame/Combat/Damage/TDMegaMagicDamageExamples.cpp", "LoadObject<UNiagaraSystem>", "authoring-factory", "커맨드릿·시험·에디터 전용 대체 경로만 호출"),
    ("Source/TDGame/MonsterAI/TDMonsterDefinitionLibrary.cpp", "SpellAssetsHandle->WaitUntilComplete", "gate-wait", "몬스터 정의 해석 전 규칙 데이터 상주(결정론, D-47)"),
    ("Source/TDGame/World/Streaming/TDSeamlessTravelSubsystem.cpp", "DungeonAtlasHandle->WaitUntilComplete", "gate-wait", "ResolveDestination 즉시 bool 반환 계약(D-47)"),
]


def stop_with_input_error(path, reason):
    print(f"ERROR {path}: {reason}")
    sys.exit(2)


def read_utf8_text(path):
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        stop_with_input_error(path, f"UTF-8로 읽을 수 없음 ({error})")


def read_descriptor_module_folders(descriptor_path, source_folder):
    try:
        descriptor = json.loads(read_utf8_text(descriptor_path))
    except json.JSONDecodeError as error:
        stop_with_input_error(descriptor_path, f"JSON 해석 실패 ({error})")
    return [(source_folder / module["Name"], module.get("Type", "")) for module in descriptor.get("Modules", []) if "Name" in module]


def collect_module_folders(root):
    project_descriptor = root / "TDGame.uproject"
    if not project_descriptor.is_file():
        stop_with_input_error(project_descriptor, "파일 없음")
    module_folders = read_descriptor_module_folders(project_descriptor, root / "Source")
    for plugin_descriptor in sorted(root.glob("Plugins/*/*.uplugin")):
        module_folders += read_descriptor_module_folders(plugin_descriptor, plugin_descriptor.parent / "Source")
    return module_folders


def collect_source_files(root):
    source_folder = root / "Source"
    if not source_folder.is_dir():
        stop_with_input_error(source_folder, "폴더 없음")
    source_folders = [source_folder] + sorted(folder for folder in root.glob("Plugins/*/Source") if folder.is_dir())
    source_files = [path for folder in source_folders for path in folder.rglob("*") if path.is_file() and path.suffix in SOURCE_SUFFIXES]
    return sorted(source_files, key=lambda path: path.relative_to(root).as_posix())


def is_editor_module_file(file_path, module_folders):
    containing_modules = [(len(folder.parts), module_type) for folder, module_type in module_folders if file_path.is_relative_to(folder)]
    if not containing_modules:
        return False
    return max(containing_modules)[1] in EDITOR_MODULE_TYPES


def strip_comments_and_literals(text):
    pieces = []
    index = 0
    while True:
        start_match = COMMENT_OR_LITERAL_START.search(text, index)
        if start_match is None:
            pieces.append(text[index:])
            return "".join(pieces)
        pieces.append(text[index:start_match.start()])
        replacement, index = blank_comment_or_literal(text, start_match.start(), start_match.group())
        pieces.append(replacement)


def blank_comment_or_literal(text, start, token):
    if token == "//":
        end = text.find("\n", start)
        end = len(text) if end == -1 else end
        return " " * (end - start), end
    if token == "/*":
        end = text.find("*/", start + 2)
        end = len(text) if end == -1 else end + 2
        return NON_NEWLINE_CHARACTER.sub(" ", text[start:end]), end
    if token == "'" and start > 0 and text[start - 1] in DIGIT_CHARACTERS:
        return token, start + 1
    raw_string_opening = match_raw_string_opening(text, start) if token == '"' else None
    if raw_string_opening is not None:
        return blank_raw_string(text, raw_string_opening)
    return blank_quoted_literal(text, start, token)


def match_raw_string_opening(text, start):
    prefix_start = start
    while prefix_start > 0 and (text[prefix_start - 1].isalnum() or text[prefix_start - 1] == "_"):
        prefix_start -= 1
    if text[prefix_start:start] not in RAW_STRING_PREFIXES:
        return None
    return RAW_STRING_OPENING.match(text, start)


def blank_raw_string(text, raw_string_opening):
    closing = ")" + raw_string_opening.group(1) + '"'
    closing_start = text.find(closing, raw_string_opening.end())
    if closing_start == -1:
        return blank_literal_body('"', text[raw_string_opening.start() + 1:], False), len(text)
    end = closing_start + len(closing)
    return blank_literal_body('"', text[raw_string_opening.start() + 1:end - 1], True), end


def blank_quoted_literal(text, start, quote):
    index = start + 1
    while index < len(text):
        character = text[index]
        if character == quote:
            return blank_literal_body(quote, text[start + 1:index], True), index + 1
        if character == "\n":
            return blank_literal_body(quote, text[start + 1:index], False), index
        index += 2 if character == "\\" else 1
    return blank_literal_body(quote, text[start + 1:], False), len(text)


def blank_literal_body(quote, body, is_closed):
    return quote + "\n" * body.count("\n") + (quote if is_closed else "")


def collect_allowed_guard_names(relative_path):
    if "/Tests/" in relative_path:
        return ALWAYS_ALLOWED_GUARD_NAMES | TEST_FILE_ALLOWED_GUARD_NAMES
    return ALWAYS_ALLOWED_GUARD_NAMES


def find_matching_parenthesis(expression):
    depth = 0
    for index, character in enumerate(expression):
        depth += {"(": 1, ")": -1}.get(character, 0)
        if depth == 0:
            return index
    return -1


def strip_outer_parentheses(expression):
    expression = expression.strip()
    while expression.startswith("(") and find_matching_parenthesis(expression) == len(expression) - 1:
        expression = expression[1:-1].strip()
    return expression


def is_allowed_guard_term(term, allowed_names):
    term_match = ALLOWED_GUARD_TERM.fullmatch(strip_outer_parentheses(term))
    return term_match is not None and term_match.group(1) in allowed_names


def is_allowed_condition(condition, allowed_names):
    condition = strip_outer_parentheses(condition)
    if "||" in condition:
        return False
    return any(is_allowed_guard_term(term, allowed_names) for term in condition.split("&&"))


def apply_conditional_directive(branch_stack, directive_text, allowed_names):
    directive_match = DIRECTIVE.match(directive_text)
    if directive_match is None:
        return
    keyword = directive_match.group(1)
    condition = directive_match.group(2)
    if keyword == "if":
        branch_stack.append(is_allowed_condition(condition, allowed_names))
        return
    if keyword in ("ifdef", "ifndef"):
        branch_stack.append(False)
        return
    if not branch_stack:
        return
    if keyword == "elif":
        branch_stack[-1] = is_allowed_condition(condition, allowed_names)
        return
    if keyword in ("else", "elifdef", "elifndef"):
        branch_stack[-1] = False
        return
    if keyword == "endif":
        branch_stack.pop()


def match_sync_load_pattern(line):
    return next((pattern_id for pattern_id, pattern in SYNC_LOAD_PATTERNS if pattern.search(line)), None)


def find_sync_load_hits(relative_path, text):
    allowed_names = collect_allowed_guard_names(relative_path)
    original_lines = text.split("\n")
    stripped_lines = strip_comments_and_literals(text).split("\n")
    branch_stack = []
    pending_directive = ""
    hits = []
    for line_number, (original_line, stripped_line) in enumerate(zip(original_lines, stripped_lines), start=1):
        pattern_id = match_sync_load_pattern(stripped_line)
        if pattern_id is not None:
            hits.append((line_number, pattern_id, stripped_line, any(branch_stack), original_line.strip()))
        if not pending_directive and not stripped_line.lstrip().startswith("#"):
            continue
        pending_directive += stripped_line
        if pending_directive.rstrip().endswith("\\"):
            pending_directive = pending_directive.rstrip()[:-1] + " "
            continue
        apply_conditional_directive(branch_stack, pending_directive, allowed_names)
        pending_directive = ""
    return hits


def categorize_hit(relative_path, stripped_line, is_editor_file, is_guarded, baseline_usage):
    if is_editor_file:
        return "EDITOR"
    if is_guarded:
        return "GUARDED"
    collapsed_line = WHITESPACE_RUN.sub(" ", stripped_line)
    for entry_index, (baseline_path, baseline_text, _, _) in enumerate(BASELINE_ENTRIES):
        if baseline_usage[entry_index] or baseline_path != relative_path or baseline_text not in collapsed_line:
            continue
        baseline_usage[entry_index] = True
        return "BASELINE"
    return "NEW"


def collect_categorized_hits(root, source_files, module_folders, baseline_usage):
    hits = []
    for file_path in source_files:
        relative_path = file_path.relative_to(root).as_posix()
        is_editor_file = is_editor_module_file(file_path, module_folders)
        for line_number, pattern_id, stripped_line, is_guarded, code in find_sync_load_hits(relative_path, read_utf8_text(file_path)):
            category = categorize_hit(relative_path, stripped_line, is_editor_file, is_guarded, baseline_usage)
            hits.append({"path": relative_path, "line": line_number, "pattern": pattern_id, "category": category, "code": code})
    return hits


def write_report(report_path, summary, hits):
    try:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps({"summary": summary, "hits": hits}, ensure_ascii=False, indent=1), encoding="utf-8")
    except OSError as error:
        stop_with_input_error(report_path, f"보고서를 쓸 수 없음 ({error})")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--list-all", action="store_true", help="모든 적중을 범주와 함께 출력")
    parser.add_argument("--report", type=Path, help="JSON 보고서 경로(관례 Saved/AgentOps/<YYYYMMDD>/sync_loads.json)")
    parser.add_argument("--root", type=Path, default=PROJECT_ROOT, help="프로젝트 루트(기본: 이 스크립트의 상위 폴더)")
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    module_folders = collect_module_folders(root)
    source_files = collect_source_files(root)
    baseline_usage = [False] * len(BASELINE_ENTRIES)
    hits = collect_categorized_hits(root, source_files, module_folders, baseline_usage)
    stale_entries = [entry for entry, is_used in zip(BASELINE_ENTRIES, baseline_usage) if not is_used]

    for hit in hits:
        if arguments.list_all or hit["category"] == "NEW":
            print(f"{hit['category']} {hit['path']}:{hit['line']} {hit['pattern']}: {hit['code']}")
    for baseline_path, baseline_text, _, _ in stale_entries:
        print(f"STALE {baseline_path}: {baseline_text} ({STALE_GUIDANCE})")
    summary = {"files": len(source_files), "hits": len(hits)}
    for category in HIT_CATEGORIES:
        summary[category.lower()] = sum(1 for hit in hits if hit["category"] == category)
    summary["stale"] = len(stale_entries)
    print("SUMMARY " + " ".join(f"{key}={value}" for key, value in summary.items()))

    if arguments.report is not None:
        write_report(arguments.report, summary, hits)
    return 1 if summary["new"] or summary["stale"] else 0


if __name__ == "__main__":
    sys.exit(main())
