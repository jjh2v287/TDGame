"""시스템 Python 3에서 실행(UnrealEditor GUI는 --allow-editor 일 때만 허용): TDFogScattering 플러그인을 RunUAT BuildPlugin 으로 공용 트리 바이너리를 건드리지 않고 격리 빌드하는 래퍼(SF-1 명세 6·7절). 다른 UBT·UAT·cl·link·UnrealEditor 가 30초 연속 없을 때까지 기다리고 Saved/AgentOps/build_hold.json 을 존중한다(Tools/build_and_test.py 함수 재사용).
실행: python Tools/FogScattering/build_fog_scattering_plugin.py --plugin <경로>.uplugin [--label 이름] [--package-dir 폴더] [--holder 이름] [--allow-editor] [--wait-seconds 1800] [--timeout-minutes 40] [--engine Engine폴더] [--dry-run] (PowerShell)
출력: Saved/AgentOps/<YYYYMMDD>/ssfs/buildplugin-<label>/(패키지)와 그 옆 buildplugin-<label>.log·buildplugin-<label>.json(종료 코드·소요 초·error 와 C4996 이외 warning C 줄 요약 최대 40줄·C4996 개수·로그 경로). 종료 코드 0 = 빌드 성공 / 1 = 빌드 실패·시간 초과 / 2 = 입력 오류 / 3 = 대기 시간 초과
상태: 2026-10-23 SF-1 측정 장치 빌더 작성, --dry-run 만 검증(실제 BuildPlugin 미실행, 성공 판정은 RunUAT 종료 코드 0)
"""
import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ENGINE_DIR = r"C:\Program Files\Epic Games\UE_5.8\Engine"
BUILD_AND_TEST_PATH = ROOT / "Tools" / "build_and_test.py"
COMPILER_IMAGES = ("cl.exe", "link.exe")
EDITOR_ENTRY_SUFFIX = " unrealeditor.exe"
QUIET_SECONDS = 30.0
POLL_SECONDS = 5.0
SUMMARY_LINE_LIMIT = 40
ERROR_PATTERN = re.compile(r"(: error |: fatal error |error C\d+|error LNK|LNK\d{4}|ERROR:|BUILD FAILED)", re.IGNORECASE)
WARNING_PATTERN = re.compile(r"warning C\d+")
IGNORED_WARNING_CODE = "C4996"
EXIT_FAILED = 1
EXIT_INPUT = 2
EXIT_WAIT_TIMEOUT = 3


def load_build_module():
    specification = importlib.util.spec_from_file_location("build_and_test", BUILD_AND_TEST_PATH)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def list_compiler_processes():
    found = []
    for image in COMPILER_IMAGES:
        output = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {image}", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
        found += [f"{line.split(chr(34) + ',' + chr(34))[1]} {image}" for line in output.splitlines() if line.lower().startswith(f'"{image}"')]
    return found


def list_blocking(build, holder, allow_editor):
    blocking = build.list_blocking_processes()
    if allow_editor:
        blocking = [entry for entry in blocking if not entry.lower().endswith(EDITOR_ENTRY_SUFFIX)]
    blocking += list_compiler_processes()
    active_hold = build.read_active_hold(holder)
    if active_hold:
        blocking.append(active_hold)
    return blocking


def wait_for_quiet_machine(build, wait_seconds, holder, allow_editor):
    deadline = time.time() + wait_seconds
    quiet_since = None
    last_report = None
    while time.time() < deadline:
        blocking = list_blocking(build, holder, allow_editor)
        if blocking:
            quiet_since = None
            if blocking != last_report:
                print("대기:", ", ".join(blocking), flush=True)
                last_report = blocking
        else:
            quiet_since = quiet_since if quiet_since is not None else time.time()
            if time.time() - quiet_since >= QUIET_SECONDS:
                return True
        time.sleep(POLL_SECONDS)
    print("대기 시간 초과:", ", ".join(list_blocking(build, holder, allow_editor)), flush=True)
    return False


def run_build(command, log_path, timeout_seconds):
    started_at = time.time()
    with open(log_path, "w", encoding="utf-8", errors="replace") as log_file:
        process = subprocess.Popen(command, stdout=log_file, stderr=subprocess.STDOUT)
        try:
            return_code = process.wait(timeout=timeout_seconds)
            has_timed_out = False
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True)
            process.wait()
            return_code = None
            has_timed_out = True
    return return_code, has_timed_out, round(time.time() - started_at, 1)


def summarize_log(log_path):
    text = Path(log_path).read_text(encoding="utf-8", errors="replace")
    lines = list(dict.fromkeys(line.strip() for line in text.splitlines() if line.strip()))
    errors = [line for line in lines if ERROR_PATTERN.search(line)]
    warnings = [line for line in lines if WARNING_PATTERN.search(line)]
    ignored_warnings = [line for line in warnings if IGNORED_WARNING_CODE in line]
    new_warnings = [line for line in warnings if IGNORED_WARNING_CODE not in line]
    return {"summary_lines": (errors + new_warnings)[:SUMMARY_LINE_LIMIT], "error_count": len(errors),
            "new_warning_count": len(new_warnings), "c4996_count": len(ignored_warnings),
            "build_successful_marker": "BUILD SUCCESSFUL" in text}


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="TDFogScattering RunUAT BuildPlugin 격리 빌드 래퍼")
    parser.add_argument("--plugin", required=True)
    parser.add_argument("--label", default="probe")
    parser.add_argument("--package-dir")
    parser.add_argument("--engine", default=os.environ.get("UE_ENGINE_DIR", DEFAULT_ENGINE_DIR))
    parser.add_argument("--holder")
    parser.add_argument("--allow-editor", action="store_true")
    parser.add_argument("--wait-seconds", type=int, default=1800)
    parser.add_argument("--timeout-minutes", type=float, default=40.0)
    parser.add_argument("--dry-run", action="store_true")
    arguments = parser.parse_args()

    plugin_path = Path(arguments.plugin).resolve()
    run_uat = Path(arguments.engine) / "Build" / "BatchFiles" / "RunUAT.bat"
    output_directory = ROOT / "Saved" / "AgentOps" / datetime.now().strftime("%Y%m%d") / "ssfs"
    package_dir = Path(arguments.package_dir).resolve() if arguments.package_dir else output_directory / f"buildplugin-{arguments.label}"
    log_path = package_dir.parent / f"buildplugin-{arguments.label}.log"
    result_path = package_dir.parent / f"buildplugin-{arguments.label}.json"
    command = [str(run_uat), "BuildPlugin", f"-Plugin={plugin_path}", f"-Package={package_dir}", "-TargetPlatforms=Win64", "-Rocket"]

    if arguments.dry_run:
        print(subprocess.list2cmdline(command))
        print(json.dumps({"dry_run": True, "log": str(log_path), "result": str(result_path), "allow_editor": arguments.allow_editor, "timeout_minutes": arguments.timeout_minutes}, ensure_ascii=False))
        return 0
    if not plugin_path.is_file() or not run_uat.is_file():
        print("입력 오류: uplugin 또는 RunUAT.bat 없음", plugin_path, run_uat)
        return EXIT_INPUT
    if plugin_path.parent in package_dir.parents:
        print("입력 오류: 패키지 폴더가 플러그인 폴더 안에 있다", package_dir)
        return EXIT_INPUT

    build = load_build_module()
    if not wait_for_quiet_machine(build, arguments.wait_seconds, arguments.holder, arguments.allow_editor):
        return EXIT_WAIT_TIMEOUT
    package_dir.parent.mkdir(parents=True, exist_ok=True)
    return_code, has_timed_out, elapsed_seconds = run_build(command, log_path, arguments.timeout_minutes * 60.0)
    result = {"command": subprocess.list2cmdline(command), "exit_code": return_code, "timed_out": has_timed_out, "elapsed_seconds": elapsed_seconds, "log": str(log_path)}
    result.update(summarize_log(log_path))
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=1))
    return 0 if return_code == 0 else EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
