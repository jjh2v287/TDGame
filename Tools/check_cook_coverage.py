"""시스템 Python으로 실행: 런타임 C++(`Source/`·`Plugins/*/Source/`, 에디터 모듈·Tests 폴더·WITH_EDITOR 블록·주석 제외)의 `/Game` 문자열 경로마다 쿠킹 보장 근거(DefaultGame.ini 패키징 섹션의 항상 쿠킹 폴더·런타임 설정 ini 값·ConstructorHelpers 시작 로드·그 두 근거에 속한 패키지의 직접 참조·예외 목록)를 판정하고, 근거 없는 경로·쿠킹 제외 폴더 안 경로·사라진 예외 목록 항목을 실패로 보고한다(OP-33, L-build-07). 이어붙이기·Printf·Format·Combine 경로와 없는 패키지 이름의 접두는 폴더 단위로 판정한다.
실행: python Tools/check_cook_coverage.py [--list-all] [--report <JSON 경로>] [--root <프로젝트 루트>] (PowerShell 또는 Git Bash, 에디터·쿠킹 불필요)
출력: 표준 출력(UNCOVERED·NEVER_COOK·MISSING·STALE 줄과 SUMMARY 한 줄), --report 지정 시 JSON(관례 Saved/AgentOps/<YYYYMMDD>/cook_coverage.json). 종료 코드 0 위반 없음 / 1 UNCOVERED·NEVER_COOK·STALE / 2 입력 오류
상태: 현행
"""
import argparse
import json
import re
import sys
from pathlib import Path

import check_sync_loads as sync_loads

PROJECT_ROOT = Path(__file__).resolve().parents[1]
PACKAGE_SUFFIXES = (".uasset", ".umap")
RUNTIME_CONFIG_FILES = ("DefaultEngine.ini", "DefaultGame.ini")
PACKAGING_SECTION = "/Script/UnrealEd.ProjectPackagingSettings"
PRINTED_CATEGORIES = ("UNCOVERED", "NEVER_COOK", "MISSING")
CATEGORIES = ("ALWAYS_COOK", "CONFIG", "STARTUP", "REFERENCED", "ACCEPTED", "MISSING", "NEVER_COOK", "UNCOVERED")
STALE_GUIDANCE = "예외 목록 항목이 더 이상 없음: 목록에서 지운다"

GAME_PATH_LITERAL = re.compile(r'(?<![A-Za-z0-9_])"(/Game(?:/[^"]*)?)"')
RAW_GAME_PATH_LITERAL = re.compile(r'R"([^ ()\\\t\v\f\n]{0,16})\((/Game(?:/[^)]*)?)\)\1"')
CONCATENATION_AFTER_LITERAL = re.compile(r"[\s)]*\+")
ARGUMENT_AFTER_LITERAL = re.compile(r"[\s)]*,")
CONSTRUCTOR_HELPERS_USE = re.compile(r"\bConstructorHelpers\s*::\s*F(Object|Class)Finder")
CONFIG_GAME_PATH = re.compile(r"/Game/[A-Za-z0-9_/.\-]+")
INI_SECTION = re.compile(r"^\[(.+)\]$")
COOK_DIRECTORY_ENTRY = re.compile(r'^([+-])DirectoriesTo(AlwaysCook|NeverCook)=\(Path="?([^")]+)"?\)')

ACCEPTED_ENTRIES = [
    ("Source/TDGame/Combat/Damage/TDMegaMagicDamageExamples.cpp", "/Game/MegaMagicVFXBundle/VFX/", "예시 주문 생성: 런타임 호출은 WITH_EDITOR 대체 경로와 명령렛뿐, 쿠킹 게임의 VFX는 MegaMagic DA 소프트 참조로 쿠킹"),
]


def cut_line_comment(line):
    quote = None
    index = 0
    while index < len(line):
        character = line[index]
        if quote is not None and character == "\\":
            index += 2
            continue
        if quote is not None and character == quote:
            quote = None
        elif quote is None and character in "\"'":
            quote = character
        elif quote is None and line.startswith("//", index):
            return line[:index]
        elif quote is None and line.startswith("/*", index):
            comment_end = line.find("*/", index + 2)
            if comment_end < 0:
                return line[:index]
            line = line[:index] + " " + line[comment_end + 2:]
            continue
        index += 1
    return line


def is_prefix_use(code, literal_end, next_line):
    rest = code[literal_end:]
    if CONCATENATION_AFTER_LITERAL.match(rest):
        return True
    if not rest.strip() and next_line.lstrip().startswith("+"):
        return True
    return "Combine" in code and ARGUMENT_AFTER_LITERAL.match(rest) is not None


def find_literals_in_code(code, next_line):
    literals = [(match.group(1), is_prefix_use(code, match.end(), next_line)) for match in GAME_PATH_LITERAL.finditer(code)]
    return literals + [(match.group(2), is_prefix_use(code, match.end(), next_line)) for match in RAW_GAME_PATH_LITERAL.finditer(code)]


def find_game_path_literals(relative_path, text):
    allowed_names = sync_loads.collect_allowed_guard_names(relative_path)
    original_lines = text.split("\n")
    stripped_lines = sync_loads.strip_comments_and_literals(text).split("\n")
    branch_stack = []
    pending_directive = ""
    literals = []
    for line_index, (original_line, stripped_line) in enumerate(zip(original_lines, stripped_lines)):
        if '"' in stripped_line and not any(branch_stack):
            code = cut_line_comment(original_line)
            next_line = original_lines[line_index + 1] if line_index + 1 < len(original_lines) else ""
            literals += [(line_index + 1, literal, is_prefix, code.strip()) for literal, is_prefix in find_literals_in_code(code, next_line)]
        if not pending_directive and not stripped_line.lstrip().startswith("#"):
            continue
        pending_directive += stripped_line
        if pending_directive.rstrip().endswith("\\"):
            pending_directive = pending_directive.rstrip()[:-1] + " "
            continue
        sync_loads.apply_conditional_directive(branch_stack, pending_directive, allowed_names)
        pending_directive = ""
    return literals


def to_content_path(root, game_path):
    return root / "Content" / game_path[len("/Game/"):] if game_path.lower().startswith("/game/") else root / "Content"


def find_package_file(root, package_path):
    content_path = to_content_path(root, package_path)
    return next((content_path.with_name(content_path.name + suffix) for suffix in PACKAGE_SUFFIXES if content_path.with_name(content_path.name + suffix).is_file()), None)


def parent_game_path(game_path):
    return game_path[:game_path.rfind("/")] if game_path.count("/") > 1 else "/Game"


def classify_literal(root, literal, is_prefix):
    placeholder_index = min((literal.index(mark) for mark in "%{" if mark in literal), default=-1)
    if placeholder_index >= 0:
        return "directory", parent_game_path(literal[:placeholder_index])
    if literal.endswith("/"):
        return "directory", literal.rstrip("/")
    package_path = literal.split(".", 1)[0]
    is_package = find_package_file(root, package_path) is not None
    if not is_package and to_content_path(root, package_path).is_dir():
        return "directory", package_path
    if is_prefix or (not is_package and has_assets_on_disk(root, "directory", parent_game_path(package_path))):
        return "directory", parent_game_path(package_path)
    return "package", package_path


def has_assets_on_disk(root, kind, target):
    if kind == "package":
        return find_package_file(root, target) is not None
    directory = to_content_path(root, target)
    return directory.is_dir() and any(path.suffix in PACKAGE_SUFFIXES for path in directory.rglob("*"))


def is_under_any_directory(target, directories):
    lowered_target = target.lower()
    return any(lowered_target == directory.lower() or lowered_target.startswith(directory.lower() + "/") for directory in directories)


def read_ini_entries(path):
    section = ""
    entries = []
    for raw_line in sync_loads.read_utf8_text(path).splitlines():
        line = raw_line.strip()
        section_match = INI_SECTION.match(line)
        if section_match is not None:
            section = section_match.group(1)
        elif line and line[0] not in ";#":
            entries.append((section, line))
    return entries


def read_runtime_config_entries(root):
    config_folder = root / "Config"
    if not config_folder.is_dir():
        sync_loads.stop_with_input_error(config_folder, "폴더 없음")
    return {name: read_ini_entries(config_folder / name) for name in RUNTIME_CONFIG_FILES if (config_folder / name).is_file()}


def collect_cook_directories(config_entries, rule):
    directories = {}
    for section, line in config_entries.get("DefaultGame.ini", []):
        entry_match = COOK_DIRECTORY_ENTRY.match(line)
        if section != PACKAGING_SECTION or entry_match is None or entry_match.group(2) != rule:
            continue
        directory = entry_match.group(3).rstrip("/")
        if entry_match.group(1) == "+":
            directories[directory.lower()] = directory
        else:
            directories.pop(directory.lower(), None)
    return sorted(directories.values())


def collect_config_packages(config_entries):
    packages = set()
    for entries in config_entries.values():
        for section, line in entries:
            key = line.split("=", 1)[0]
            if not section.startswith("/Script/") or line[0] in "-!" or any(word in key for word in ("Editor", "Redirect", "DirectoriesTo")):
                continue
            packages |= {game_path.split(".", 1)[0] for game_path in CONFIG_GAME_PATH.findall(line)}
    return packages


def collect_guaranteed_root_packages(root, always_cook, never_cook, config_packages):
    root_files = set()
    for directory in always_cook:
        content_directory = to_content_path(root, directory)
        if content_directory.is_dir():
            root_files |= {path for path in content_directory.rglob("*") if path.suffix in PACKAGE_SUFFIXES}
    root_files |= {package_file for package_file in (find_package_file(root, package_path) for package_path in config_packages) if package_file is not None}
    never_cook_folders = [to_content_path(root, directory) for directory in never_cook]
    root_files = {path for path in root_files if not any(path.is_relative_to(folder) for folder in never_cook_folders)}
    return {path: path.read_bytes().lower() for path in sorted(root_files)}


def is_referenced_by_root_package(root, target, root_packages):
    target_file = find_package_file(root, target)
    exact_reference = re.compile(re.escape(target.lower().encode()) + rb"(?![a-z0-9_\-/])")
    return any(exact_reference.search(package_bytes) for path, package_bytes in root_packages.items() if path != target_file)


def find_accepted_entry(relative_path, literal, accepted_usage):
    for entry_index, (accepted_path, accepted_literal, _) in enumerate(ACCEPTED_ENTRIES):
        if accepted_path == relative_path and accepted_literal == literal:
            accepted_usage[entry_index] = True
            return True
    return False


def categorize_literal(root, relative_path, literal, kind, target, code, context, accepted_usage):
    if find_accepted_entry(relative_path, literal, accepted_usage):
        return "ACCEPTED"
    if is_under_any_directory(target, context["never_cook"]):
        return "NEVER_COOK"
    if kind == "directory" and any(is_under_any_directory(directory, [target]) for directory in context["never_cook"]):
        return "NEVER_COOK"
    if is_under_any_directory(target, context["always_cook"]):
        return "ALWAYS_COOK"
    if kind == "package" and target.lower() in {package.lower() for package in context["config_packages"]}:
        return "CONFIG"
    if kind == "package" and CONSTRUCTOR_HELPERS_USE.search(code):
        return "STARTUP"
    if not has_assets_on_disk(root, kind, target):
        return "MISSING"
    if kind == "package" and is_referenced_by_root_package(root, target, context["root_packages"]):
        return "REFERENCED"
    return "UNCOVERED"


def collect_categorized_literals(root, source_files, module_folders, context, accepted_usage):
    entries = []
    for file_path in source_files:
        relative_path = file_path.relative_to(root).as_posix()
        if "/Tests/" in relative_path or sync_loads.is_editor_module_file(file_path, module_folders):
            continue
        for line_number, literal, is_prefix, code in find_game_path_literals(relative_path, sync_loads.read_utf8_text(file_path)):
            kind, target = classify_literal(root, literal, is_prefix)
            category = categorize_literal(root, relative_path, literal, kind, target, code, context, accepted_usage)
            entries.append({"path": relative_path, "line": line_number, "literal": literal, "kind": kind, "target": target, "category": category})
    return entries


def write_report(report_path, summary, entries):
    try:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps({"summary": summary, "entries": entries}, ensure_ascii=False, indent=1), encoding="utf-8")
    except OSError as error:
        sync_loads.stop_with_input_error(report_path, f"보고서를 쓸 수 없음 ({error})")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--list-all", action="store_true", help="모든 경로를 범주와 함께 출력")
    parser.add_argument("--report", type=Path, help="JSON 보고서 경로(관례 Saved/AgentOps/<YYYYMMDD>/cook_coverage.json)")
    parser.add_argument("--root", type=Path, default=PROJECT_ROOT, help="프로젝트 루트(기본: 이 스크립트의 상위 폴더)")
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    config_entries = read_runtime_config_entries(root)
    always_cook = collect_cook_directories(config_entries, "AlwaysCook")
    never_cook = collect_cook_directories(config_entries, "NeverCook")
    config_packages = collect_config_packages(config_entries)
    context = {
        "always_cook": always_cook,
        "never_cook": never_cook,
        "config_packages": config_packages,
        "root_packages": collect_guaranteed_root_packages(root, always_cook, never_cook, config_packages),
    }
    module_folders = sync_loads.collect_module_folders(root)
    source_files = sync_loads.collect_source_files(root)
    accepted_usage = [False] * len(ACCEPTED_ENTRIES)
    entries = collect_categorized_literals(root, source_files, module_folders, context, accepted_usage)
    stale_entries = [entry for entry, is_used in zip(ACCEPTED_ENTRIES, accepted_usage) if not is_used]

    for entry in entries:
        if arguments.list_all or entry["category"] in PRINTED_CATEGORIES:
            print(f"{entry['category']} {entry['path']}:{entry['line']} {entry['kind']} {entry['target']}")
    for accepted_path, accepted_literal, _ in stale_entries:
        print(f"STALE {accepted_path}: {accepted_literal} ({STALE_GUIDANCE})")
    summary = {"paths": len(entries)}
    for category in CATEGORIES:
        summary[category.lower()] = sum(1 for entry in entries if entry["category"] == category)
    summary["stale"] = len(stale_entries)
    summary["root_packages"] = len(context["root_packages"])
    print("SUMMARY " + " ".join(f"{key}={value}" for key, value in summary.items()))

    if arguments.report is not None:
        write_report(arguments.report, summary, entries)
    return 1 if summary["uncovered"] or summary["never_cook"] or summary["stale"] else 0


if __name__ == "__main__":
    sys.exit(main())
