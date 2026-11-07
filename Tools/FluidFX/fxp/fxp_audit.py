"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), PowerShell 5.1 또는 Git Bash, PYTHONIOENCODING=utf-8.
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py areas|items|golden|compare|rows|levels|r13|package|report --out <json> [--md <md>] [--root <저장소 사본>] [하위 명령 인수](runbook-fxp-v3 3절, defs Audit·PackageRun·Report).
출력: 하위 명령 모듈(같은 폴더 <이름>.py, 고정표·늦은 import)의 JSON·md(common.run_module 규약: 정렬 키·inputs·SUMMARY 끝 줄·원자적 쓰기). 종료 0 성공 / 1 검사 실패 / 2 입력 오류·모르는 하위 명령·모듈 파일 없음.
상태: 2026-11-07 FX-P X1a 뼈대(B1). 하위 명령 9개 고정 등록, areas·items 구현, 나머지 모듈은 X1b~X6 빌더 몫(파일이 생기면 그대로 붙음). 시험 python -B Tools/FluidFX/fxp/check_fxp_fixtures.py.
"""
import argparse
import importlib
import sys
from pathlib import Path

TOOL_DIRECTORY = Path(__file__).resolve().parent
SUBCOMMAND_MODULES = {"areas": "areas", "items": "items", "golden": "golden", "compare": "compare", "rows": "rows", "levels": "levels", "r13": "r13",
                      "package": "package", "report": "report"}
EXIT_INPUT = 2


def print_usage():
    names = "|".join(SUBCOMMAND_MODULES)
    print(f"사용: python -B Tools/FluidFX/fxp/fxp_audit.py <{names}> --out <json> [--md <md>] [--root <저장소 사본>] [하위 명령 인수]", file=sys.stderr)


def load_subcommand_module(name):
    module_name = SUBCOMMAND_MODULES[name]
    if not (TOOL_DIRECTORY / f"{module_name}.py").is_file():
        return None
    if str(TOOL_DIRECTORY) not in sys.path:
        sys.path.insert(0, str(TOOL_DIRECTORY))
    return importlib.import_module(module_name)


def main(argv):
    sys.dont_write_bytecode = True
    if not argv or argv[0] not in SUBCOMMAND_MODULES:
        print_usage()
        return EXIT_INPUT
    name = argv[0]
    module = load_subcommand_module(name)
    if module is None:
        print(f"입력 오류: 하위 명령 {name}의 모듈 파일 없음({TOOL_DIRECTORY.name}/{SUBCOMMAND_MODULES[name]}.py)", file=sys.stderr)
        return EXIT_INPUT
    import common
    parser = argparse.ArgumentParser(prog=f"fxp_audit.py {name}", allow_abbrev=False)
    common.add_common_arguments(parser)
    if hasattr(module, "add_arguments"):
        module.add_arguments(parser)
    args = parser.parse_args(argv[1:])
    return common.run_module(name, module, args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
