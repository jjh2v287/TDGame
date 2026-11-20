"""실행 환경: 시스템 Python 3.12 + LLVM clang-cl(`ue_editor.LLVM_BIN`). 에디터·빌드 창·UBT 불필요(동료 세션이 빌드 창을 쥔 동안 사본 C++ 확인용).
실행 명령: python Tools/check_cpp_syntax.py <파일.cpp> [<파일.cpp> ...] [--include-root <사본 폴더/Source/<모듈>> ...] [--module TDGame] [--show 12] (PowerShell·Git Bash 모두 가능, 경로 인수는 파이썬이 그대로 넘긴다)
출력: 파일마다 "OK 초" 또는 clang-cl 오류 앞 --show 줄, 종료 코드 0 전부 통과 / 1 오류 있음 / 2 환경 문제(clang-cl·모듈 rsp 없음). 경고는 끈다(-Wno-everything), 문법·형·이름 오류만 본다.
상태: 현행(M3-42, 2026-11-20). 마지막 에디터 빌드가 남긴 `Intermediate/Build/Win64/x64/UnrealEditor/Development/<모듈>/<모듈>.Shared.rsp`(포함 경로)·Definitions·SharedDefinitions 를 쓰고, MSVC 전용 플래그와 PCH(.pch) 는 뺀다. --include-root 는 원래 Source 보다 먼저 찾는다(사본 헤더 우선). UHT 생성 헤더(*.generated.h)는 마지막 빌드 것이라, 사본 헤더에서 UCLASS·GENERATED_BODY 위 줄 수가 바뀌면 "unknown type name 'FID_..._PROLOG'" 류 가짜 오류가 난다: 그 경우 위쪽 줄 수를 맞춘 검사용 헤더 사본을 먼저 오는 --include-root 에 둔다(L-build-33).
"""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue_editor import ENGINE, LLVM_BIN, ROOT  # noqa: E402

DROPPED_FLAG_PREFIXES = ("/d2", "/dx", "/errorReport", "/experimental", "/sourceDependencies", "/Fo", "/Yu", "/Yc", "/Fp", "/c", "/Z7", "/Gw", "/Gy",
                         "/Ob", "/Ox", "/Ot", "/GF", "/bigobj", "/diagnostics", "/FC")
MSVC_COMPATIBILITY = "-fms-compatibility-version=19.44"
EXIT_ENVIRONMENT = 2


def module_paths(module):
    intermediate = Path(ROOT) / "Intermediate" / "Build" / "Win64" / "x64"
    shared_rsp = intermediate / "UnrealEditor" / "Development" / module / f"{module}.Shared.rsp"
    definitions = intermediate / "UnrealEditor" / "Development" / module / f"Definitions.{module}.h"
    shared_definitions = sorted((intermediate / "TDGameEditor" / "Development" / "UnrealEd").glob("SharedDefinitions.UnrealEd.*.h"))
    return shared_rsp, definitions, shared_definitions[0] if shared_definitions else None


def write_filtered_rsp(shared_rsp, include_roots, out_path):
    lines = shared_rsp.read_text(encoding="utf-8", errors="replace").splitlines()
    kept = [line for line in lines if not line.startswith(DROPPED_FLAG_PREFIXES)]
    roots = [f'/I "{Path(root).resolve().as_posix()}"' for root in include_roots]
    out_path.write_text("\n".join(roots + kept) + "\n", encoding="utf-8")


def check_file(clang, rsp_path, forced_includes, source, show):
    started = time.monotonic()
    command = [clang, "/Zs", "/TP", "/std:c++20", MSVC_COMPATIBILITY, f"@{rsp_path}"] + [f"/FI{path}" for path in forced_includes] + ["-Wno-everything", str(Path(source).resolve())]
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=os.path.join(ENGINE, "Engine", "Source"))
    seconds = time.monotonic() - started
    if completed.returncode == 0:
        print(f"OK {seconds:.0f}s {source}")
        return True
    errors = [line for line in (completed.stdout + completed.stderr).splitlines() if "error" in line]
    print(f"FAIL {seconds:.0f}s {source}")
    for line in errors[:show]:
        print("  " + line)
    return False


def main():
    parser = argparse.ArgumentParser(description="빌드 창 없이 C++ 파일을 엔진 헤더로 문법 검사(clang-cl /Zs)")
    parser.add_argument("files", nargs="+")
    parser.add_argument("--include-root", action="append", default=[])
    parser.add_argument("--module", default="TDGame")
    parser.add_argument("--show", type=int, default=12)
    arguments = parser.parse_args()
    clang = os.path.join(LLVM_BIN, "clang-cl.exe")
    shared_rsp, definitions, shared_definitions = module_paths(arguments.module)
    missing = [str(path) for path in (Path(clang), shared_rsp, definitions) if not path.exists()] + ([] if shared_definitions else ["SharedDefinitions.UnrealEd.*.h"])
    if missing:
        print("environment: missing " + ", ".join(missing))
        return EXIT_ENVIRONMENT
    rsp_path = Path(ROOT) / "Saved" / "AgentOps" / f"check_cpp_syntax_{arguments.module}.rsp"
    rsp_path.parent.mkdir(parents=True, exist_ok=True)
    write_filtered_rsp(shared_rsp, arguments.include_root, rsp_path)
    results = [check_file(clang, rsp_path, [shared_definitions, definitions], source, arguments.show) for source in arguments.files]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
