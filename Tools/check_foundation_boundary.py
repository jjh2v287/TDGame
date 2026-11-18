"""시스템 Python으로 실행: Source/TDGame/Foundation/ 아래 .h/.cpp 의 #include 를 간접 포함까지 따라가, 허용 목록(프로젝트 Source 밖 헤더, Foundation/ 안, GameplayMessageRuntime) 밖의 프로젝트 헤더를 포함 사슬과 함께 보고한다(FD-01). 따옴표·꺾쇠 #include 를 같은 규칙으로 해석하며, 저장소 안 플러그인 헤더도 허용 모듈 밖이면 위반이다.
실행: python Tools/check_foundation_boundary.py [--root <저장소 루트>] [--mode report|fail] [--json] [--self-test] (PowerShell 또는 Git Bash, 에디터 불필요)
출력: 표준 출력(위반 목록과 SUMMARY 한 줄) 또는 --json. 종료 코드 0 보고 모드 또는 위반 없음 / 1 fail 모드에서 위반 / 2 --self-test 실패
상태: 현행
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[1]
FIXTURE_DIR = Path(__file__).resolve().parent / "fixtures" / "foundation_boundary"
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*(?:"([^"]+)"|<([^>]+)>)')
ALLOWED_MODULE_NAMES = {"GameplayMessageRuntime"}
FOUNDATION_RELATIVE = Path("Source/TDGame/Foundation")
SELF_TEST_CASES = {"clean": 0, "direct": 1, "transitive": 2, "missing": 0, "angle": 1}


def collect_include_roots(root):
    roots = []
    for source_directory in [root / "Source", *sorted(root.glob("Plugins/*/Source"))]:
        if not source_directory.is_dir():
            continue
        for module_directory in sorted(p for p in source_directory.iterdir() if p.is_dir()):
            roots.append(module_directory)
            public_directory = module_directory / "Public"
            if public_directory.is_dir():
                roots.append(public_directory)
    return roots


def read_includes(path):
    includes = []
    try:
        text = path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return includes
    for line_number, line in enumerate(text.splitlines(), 1):
        match = INCLUDE_PATTERN.match(line)
        if match:
            includes.append((line_number, match.group(1) or match.group(2)))
    return includes


def resolve_include(include_text, including_file, include_roots):
    for base in [including_file.parent] + include_roots:
        candidate = base / include_text
        if candidate.is_file():
            return candidate.resolve()
    return None


def module_name_of(path, root):
    parts = path.relative_to(root).parts
    for index, part in enumerate(parts):
        if part == "Source" and index + 1 < len(parts):
            return parts[index + 1]
    return ""


def is_inside(path, directory):
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def find_violations(root):
    root = root.resolve()
    foundation_directory = (root / FOUNDATION_RELATIVE).resolve()
    if not foundation_directory.is_dir():
        return None
    include_roots = collect_include_roots(root)
    violations = []
    seen = set()
    sources = sorted(p for p in foundation_directory.rglob("*") if p.suffix in (".h", ".cpp"))
    for source in sources:
        source = source.resolve()
        visited = {source}
        queue = [(source, [source])]
        while queue:
            current, chain = queue.pop(0)
            for line_number, include_text in read_includes(current):
                resolved = resolve_include(include_text, current, include_roots)
                if resolved is None:
                    continue
                if is_inside(resolved, foundation_directory):
                    if resolved not in visited:
                        visited.add(resolved)
                        queue.append((resolved, chain + [resolved]))
                    continue
                if module_name_of(resolved, root) in ALLOWED_MODULE_NAMES:
                    continue
                key = (source, resolved)
                if key in seen:
                    continue
                seen.add(key)
                violations.append({
                    "foundation_file": source.relative_to(root).as_posix(),
                    "forbidden_header": resolved.relative_to(root).as_posix(),
                    "line": line_number,
                    "included_from": current.relative_to(root).as_posix(),
                    "chain": [p.relative_to(root).as_posix() for p in chain] + [resolved.relative_to(root).as_posix()],
                })
    return violations


def run_self_test():
    failures = []
    for case_name, expected_count in SELF_TEST_CASES.items():
        result = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "--root", str(FIXTURE_DIR / case_name), "--mode", "fail", "--json"],
            capture_output=True, text=True, encoding="utf-8")
        expected_exit = 1 if expected_count else 0
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError:
            failures.append(f"{case_name}: JSON 출력 아님")
            print(f"FAIL {case_name}")
            continue
        problems = []
        actual_count = len(payload.get("violations", []))
        if actual_count != expected_count or result.returncode != expected_exit:
            problems.append(f"{case_name}: 기대 위반 {expected_count}·종료 {expected_exit}, 실제 위반 {actual_count}·종료 {result.returncode}")
        if case_name == "transitive" and max((len(v["chain"]) for v in payload["violations"]), default=0) != 3:
            problems.append("transitive: 간접 포함 사슬(길이 3)을 찾지 못함")
        if case_name == "missing" and not payload.get("no_foundation_code"):
            problems.append("missing: no_foundation_code 표시 없음")
        failures.extend(problems)
        print(("FAIL " if problems else "OK ") + case_name)
    print("SELF-TEST " + ("FAIL" if failures else "PASS"))
    for failure in failures:
        print(" ", failure)
    return 2 if failures else 0


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="Foundation 경계 검사(FD-01)")
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--mode", choices=["report", "fail"], default="report")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return run_self_test()
    violations = find_violations(arguments.root)
    if violations is None:
        if arguments.json:
            print(json.dumps({"no_foundation_code": True, "violations": []}, ensure_ascii=False))
        else:
            print("기반 코드 없음 (Source/TDGame/Foundation 폴더 없음)")
        return 0
    if arguments.json:
        print(json.dumps({"no_foundation_code": False, "violations": violations}, ensure_ascii=False, indent=1))
    else:
        for violation in violations:
            print(f"VIOLATION {violation['foundation_file']} -> {violation['forbidden_header']} (include {violation['included_from']}:{violation['line']})")
            print("  chain: " + " -> ".join(violation["chain"]))
        print(f"SUMMARY violations={len(violations)} mode={arguments.mode}")
    return 1 if violations and arguments.mode == "fail" else 0


if __name__ == "__main__":
    sys.exit(main())
