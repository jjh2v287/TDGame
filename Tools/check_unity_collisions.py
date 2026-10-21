"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터·빌드 불필요.
실행 명령: python -B Tools/check_unity_collisions.py [--root 프로젝트경로]
출력: 모듈(Source/<모듈>, Plugins/*/Source/<모듈>)마다 둘 이상의 .cpp 익명 네임스페이스에 같은 이름(const·constexpr 상수, 콘솔 변수, 함수 이름+인수, struct·class·enum)이 있으면 'COLLISION <모듈> <이름> -> <파일들>', 파일 범위 using namespace 는 'NOTE' 줄, 마지막 SUMMARY 줄. 종료 코드 0 충돌 없음 / 1 충돌 있음(통합 빌드 묶음이 바뀌면 재정의 오류, L-build-03).
상태: 2026-10-21 claude 작성. 'namespace' 단독 줄 다음 '{' 로 여는 익명 네임스페이스 안의 탭 한 단계 선언만 본다. 지금 트리 충돌 0, HEAD 0489e85b 의 TDFluidLiveActor.cpp·TDFluidLiveComponent.cpp MaximumShadowCapsuleCount 충돌과 합성 fixture(상수·함수·구조체) 검출 확인.
"""
import argparse
import collections
import re
import sys
from pathlib import Path

DECLARATION = re.compile(r"^\t(?:static\s+)?(?:inline\s+)?(?:constexpr|const)\s+[\w:<>, *&]+?\s+(\w+)\s*(?:\[|=|\{|;)")
CONSOLE_VARIABLE = re.compile(r"^\t(?:static\s+)?(?:TAutoConsoleVariable<[^>]+>|FAutoConsoleVariableRef|FAutoConsoleCommand)\s+(\w+)\s*\(")
TYPE = re.compile(r"^\t(?:struct|class|enum(?:\s+class)?)\s+(\w+)\s*(?::|\{|$)")
FUNCTION = re.compile(r"^\t(?:static\s+)?(?:inline\s+)?(?:const\s+)?[\w:<>*&, ]+?[\s*&]+(\w+)\s*\(([^)]*)\)\s*(?:const)?\s*$")
FILE_USING = re.compile(r"^using namespace\s+([\w:]+)\s*;")
KEYWORDS = {"if", "for", "while", "switch", "return", "else", "case"}


def find_modules(root):
    modules = collections.defaultdict(list)
    source_roots = [root / "Source"] + sorted((root / "Plugins").glob("*/Source"))
    for source_root in source_roots:
        if not source_root.is_dir():
            continue
        for module in sorted(path for path in source_root.iterdir() if path.is_dir()):
            modules[module.relative_to(root).as_posix()] = sorted(module.rglob("*.cpp"))
    return modules


def collect_anonymous_names(text):
    names = set()
    lines = text.replace("\r\n", "\n").split("\n")
    is_inside_anonymous = False
    for index, line in enumerate(lines):
        if line.strip() == "namespace" and index + 1 < len(lines) and lines[index + 1].strip() == "{":
            is_inside_anonymous = True
            continue
        if is_inside_anonymous and line.startswith("}"):
            is_inside_anonymous = False
            continue
        if not is_inside_anonymous:
            continue
        for pattern in (DECLARATION, CONSOLE_VARIABLE, TYPE):
            match = pattern.match(line)
            if match:
                names.add(match.group(1))
        match = FUNCTION.match(line)
        if match and match.group(1) not in KEYWORDS:
            names.add(match.group(1) + "(" + re.sub(r"\s+", " ", match.group(2)).strip() + ")")
    return names


def find_file_usings(text):
    return [match.group(1) for match in (FILE_USING.match(line) for line in text.replace("\r\n", "\n").split("\n")) if match]


def main():
    parser = argparse.ArgumentParser(description="통합 빌드 익명 네임스페이스 이름 충돌 검사(L-build-03)")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[1]))
    arguments = parser.parse_args()
    root = Path(arguments.root).resolve()
    collision_count = 0
    file_count = 0
    for module, files in find_modules(root).items():
        owners = collections.defaultdict(list)
        for path in files:
            text = path.read_text(encoding="utf-8", errors="replace")
            file_count += 1
            relative = path.relative_to(root).as_posix()
            for name in collect_anonymous_names(text):
                owners[name].append(relative)
            for namespace in find_file_usings(text):
                print(f"NOTE {module} using namespace {namespace} -> {relative}")
        for name, paths in sorted(owners.items()):
            if len(paths) > 1:
                collision_count += 1
                print(f"COLLISION {module} {name} -> {', '.join(paths)}")
    print(f"SUMMARY files={file_count} collisions={collision_count}")
    return 1 if collision_count else 0


if __name__ == "__main__":
    sys.exit(main())
