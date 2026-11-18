"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, PowerShell 에서 실행: /Game 경로가 들어간 촬영 도구를 부른다). 3D 안개 1c 탐색 촬영 실행기: 실행 이름 목록(또는 --index 의 written 실행, 2회차는 index_round2.json)을 차례로 (촬영 → 로그 보관 → 유효 확인, 무효면 한 번 다시)로 돌린다.
실행 명령: python -B Tools/FluidFX/fog3d_calib_run.py <실행 이름...> [--index Saved/FluidFX/fog3d_calib/index.json] [--navigation-wait-seconds 5 | --original-navigation-wait] [--progress-dir <폴더>] [--no-retry] (python 앞에 PYTHONUTF8=1 은 쓰지 않아도 된다), 점검만: --self-test
출력: <진행 폴더>/progress.json(실행마다 시작·끝 시각·종료 코드·시도 횟수·내비 대기 줄 수·실행의 console_restore 가 있으면 그 엔진 설정이 원래 값으로 돌아왔는지 확인), <진행 폴더>/done.flag(끝 표시: 전체 끝났을 때만 생김), 실행마다 Saved/FluidFX/fog3d_calib/<이름>/capture_stdout_<시도>.txt. 종료 코드 0 모두 유효 / 1 무효·실패 실행 있음 / 2 입력 문제.
상태: 촬영 도구 파일은 고치지 않고 pie_check_fluid.EDITOR_NAVIGATION_WAIT_SECONDS 값만 이 프로세스 안에서 바꾼 뒤 pie_lookdev_fluid.main 을 부른다. 실제 에디터 촬영에서 바뀐 값이 쓰였는지는 내비 대기 줄 수와 실행 시간으로 확인한다.
"""
import argparse
import io
import json
import sys
import tempfile
import time
import traceback
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_report as report_module  # noqa: E402
import fog3d_calib_variants as variants_module  # noqa: E402

CALIBRATION_ROOT = variants_module.CALIBRATION_ROOT
LEVEL_PATH = variants_module.STAGE_LEVEL_PATH
DEFAULT_NAVIGATION_WAIT_SECONDS = 5.0
NAVIGATION_STATE_MARKER = "TD_NAV_STATE"
ENGINE_CVAR_MARKER = "TD_ENGINE_CVARS"
MAXIMUM_ATTEMPTS = 2
EXIT_ALL_VALID = 0
EXIT_SOME_FAILED = 1
EXIT_INPUT_PROBLEM = 2


class InputProblem(Exception):
    pass


class TeeStream:
    def __init__(self, *streams):
        self.streams = streams

    def write(self, text):
        for stream in self.streams:
            stream.write(text)
        return len(text)

    def flush(self):
        for stream in self.streams:
            stream.flush()


def now_text():
    return datetime.now().isoformat(timespec="seconds")


def load_capture_modules(navigation_wait_seconds):
    import pie_check_fluid
    import pie_lookdev_fluid
    if pie_lookdev_fluid.pie_check_fluid is not pie_check_fluid:
        raise InputProblem("촬영 도구가 읽는 pie_check_fluid 모듈이 이 프로세스의 것과 다르다(대기 상수를 바꿀 수 없다)")
    original = pie_check_fluid.EDITOR_NAVIGATION_WAIT_SECONDS
    if navigation_wait_seconds is not None:
        pie_check_fluid.EDITOR_NAVIGATION_WAIT_SECONDS = navigation_wait_seconds
    return pie_check_fluid, pie_lookdev_fluid, original


def build_capture_arguments(name):
    run_directory = CALIBRATION_ROOT / name
    return ["pie_lookdev_fluid.py", "--variants", (run_directory / "variants.json").as_posix(), "--level", LEVEL_PATH, "--fresh-pie", "--stop-pie",
            "--output-dir", (run_directory / variants_module.CAPTURE_SUBDIRECTORY).as_posix(), "--run-id", f"fog3d_calib_{name}"]


def run_capture(lookdev_module, name, attempt):
    run_directory = CALIBRATION_ROOT / name
    run_directory.mkdir(parents=True, exist_ok=True)
    stdout_path = run_directory / f"capture_stdout_{attempt}.txt"
    original_argv, original_stdout = sys.argv, sys.stdout
    exit_code = None
    with open(stdout_path, "w", encoding="utf-8", newline="\n") as handle:
        sys.argv = build_capture_arguments(name)
        sys.stdout = TeeStream(original_stdout, handle)
        try:
            exit_code = lookdev_module.main()
        except SystemExit as error:
            exit_code = error.code if isinstance(error.code, int) else 1
        except Exception:
            traceback.print_exc(file=sys.stdout)
            exit_code = -1
        finally:
            sys.stdout.flush()
            sys.stdout, sys.argv = original_stdout, original_argv
    text = stdout_path.read_text(encoding="utf-8", errors="replace")
    return exit_code, text.count(NAVIGATION_STATE_MARKER), stdout_path


def snapshot_log(name):
    return report_module.main(["--snapshot-log", name])


def check_validity(name):
    run_file = CALIBRATION_ROOT / name / "run.json"
    if run_file.is_file() and json.loads(run_file.read_text(encoding="utf-8")).get("valid_conditions", {}).get("dense"):
        import fog3d_calib_clips as clips_module
        dense = clips_module.check_dense_run(CALIBRATION_ROOT / name)
        return {"valid": dense["valid"], "usable": dense.get("log", {}).get("usable"), "reasons": dense["reasons"], "submitted": dense.get("log", {}).get("submitted"),
                "rejected": dense.get("log", {}).get("rejected"), "fog3d_warning_or_error_lines": dense.get("log", {}).get("fog3d_warning_or_error_lines"),
                "capture_delay_max_seconds": dense.get("capture_delay_max_seconds"), "shots_over_limit": dense.get("shots_over_limit"), "walk_end_seconds": dense.get("walk_end_seconds")}
    options = report_module.parse_arguments(["--runs", name, "--no-images"])
    try:
        report, _, _, _ = report_module.analyze_run(CALIBRATION_ROOT / name, options)
    except (report_module.InputProblem, OSError, KeyError, ValueError) as error:
        return {"valid": False, "usable": None, "reasons": [f"보고 계산 오류: {error}"]}
    return {"valid": bool(report["valid"]), "usable": report.get("usable"), "reasons": list(report["invalid_reasons"]),
            "submitted": report["log"].get("submitted"), "rejected": report["log"].get("rejected"), "fog3d_warning_or_error_lines": report["log"].get("fog3d_warning_or_error_lines")}


def restore_expectations(name):
    run_file = CALIBRATION_ROOT / name / "run.json"
    if not run_file.is_file():
        return {}
    expectations = {}
    for command in json.loads(run_file.read_text(encoding="utf-8")).get("console_restore", []):
        parts = command.split()
        if len(parts) == 2:
            expectations[parts[0]] = float(parts[1])
    return expectations


def read_engine_cvars(pie_check_module, names):
    getters = ", ".join(f"'{name}': unreal.SystemLibrary.get_console_variable_float_value('{name}')" for name in names)
    code = "import unreal, json; print('" + ENGINE_CVAR_MARKER + " ' + json.dumps({" + getters + "}))"
    return pie_check_module.parse_marker(pie_check_module.run_editor_code(code), ENGINE_CVAR_MARKER)


def check_engine_cvars(pie_check_module, expected):
    values = read_engine_cvars(pie_check_module, list(expected))
    if "error" in values:
        return {"values": values, "restored": None}
    restored = all(abs(values.get(key, -1.0) - value) < 1e-4 for key, value in expected.items())
    return {"values": values, "restored": restored}


def write_progress(progress_directory, record):
    progress_directory.mkdir(parents=True, exist_ok=True)
    (progress_directory / "progress.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def resolve_names(arguments):
    names = list(arguments.names or [])
    if arguments.index:
        index = json.loads(Path(arguments.index).read_text(encoding="utf-8"))
        names += [entry["name"] for entry in index["runs"] if entry.get("status") == "written" and entry["name"] not in names]
    if not names:
        raise InputProblem("실행 이름 또는 --index 가 필요하다")
    for name in names:
        if not (CALIBRATION_ROOT / name / "variants.json").is_file():
            raise InputProblem(f"{name}: variants.json 이 없다(fog3d_calib_variants.py 로 먼저 만든다)")
    return names


def run_all(arguments):
    names = resolve_names(arguments)
    progress_directory = Path(arguments.progress_dir)
    done_flag = progress_directory / "done.flag"
    progress_directory.mkdir(parents=True, exist_ok=True)
    if done_flag.exists():
        done_flag.unlink()
    wait_seconds = None if arguments.original_navigation_wait else arguments.navigation_wait_seconds
    pie_check_module, lookdev_module, original_wait = load_capture_modules(wait_seconds)
    record = {"started_at": now_text(), "finished_at": None, "navigation_wait_seconds_used": pie_check_module.EDITOR_NAVIGATION_WAIT_SECONDS,
              "navigation_wait_seconds_original": original_wait, "names": names, "runs": []}
    write_progress(progress_directory, record)
    failed = []
    for name in names:
        entry = {"name": name, "started_at": now_text(), "finished_at": None, "attempts": []}
        record["runs"].append(entry)
        for attempt in range(1, MAXIMUM_ATTEMPTS + 1):
            attempt_record = {"attempt": attempt, "started_at": now_text()}
            entry["attempts"].append(attempt_record)
            write_progress(progress_directory, record)
            started = time.monotonic()
            exit_code, navigation_lines, stdout_path = run_capture(lookdev_module, name, attempt)
            attempt_record.update({"capture_exit_code": exit_code, "navigation_state_lines": navigation_lines, "capture_wall_seconds": round(time.monotonic() - started, 1), "stdout": stdout_path.relative_to(ROOT).as_posix()})
            attempt_record["snapshot_exit_code"] = snapshot_log(name)
            attempt_record["validity"] = check_validity(name)
            expected_cvars = restore_expectations(name)
            if expected_cvars:
                attempt_record["engine_cvars"] = check_engine_cvars(pie_check_module, expected_cvars)
            attempt_record["wall_seconds"] = round(time.monotonic() - started, 1)
            attempt_record["finished_at"] = now_text()
            write_progress(progress_directory, record)
            validity = attempt_record["validity"]
            if exit_code == 0 and validity["valid"] and validity["usable"] is not False:
                break
            if arguments.no_retry:
                break
        entry["finished_at"] = now_text()
        last = entry["attempts"][-1]
        entry["final_valid"] = bool(last["capture_exit_code"] == 0 and last["validity"]["valid"] and last["validity"]["usable"] is not False)
        if not entry["final_valid"]:
            failed.append(name)
        write_progress(progress_directory, record)
        print(f"== {name}: {'유효' if entry['final_valid'] else '무효·실패'} 시도 {len(entry['attempts'])}회, 마지막 {last['wall_seconds']}초, 내비 대기 줄 {last['navigation_state_lines']}", flush=True)
    record["finished_at"] = now_text()
    record["failed_names"] = failed
    write_progress(progress_directory, record)
    done_flag.write_text(f"done {now_text()} failed={','.join(failed) or '-'}\n", encoding="utf-8", newline="\n")
    return EXIT_SOME_FAILED if failed else EXIT_ALL_VALID


def check(condition, label, failures):
    if not condition:
        failures.append(label)
    print(("통과 " if condition else "실패 ") + label)


def run_self_test():
    failures = []
    import pie_check_fluid
    import pie_lookdev_fluid
    check(pie_lookdev_fluid.pie_check_fluid is pie_check_fluid, "촬영 도구가 읽는 pie_check_fluid 가 같은 모듈 객체", failures)
    check(isinstance(pie_check_fluid.EDITOR_NAVIGATION_WAIT_SECONDS, float) and pie_check_fluid.EDITOR_NAVIGATION_WAIT_SECONDS >= 100.0, "촬영 도구 쪽 원래 대기 상수는 그대로(120초)", failures)
    _, _, original = load_capture_modules(DEFAULT_NAVIGATION_WAIT_SECONDS)
    check(pie_check_fluid.EDITOR_NAVIGATION_WAIT_SECONDS == 5.0 and original == 120.0, "이 프로세스 안에서만 대기 상수 5초로 바뀜(원래 120)", failures)
    pie_check_fluid.EDITOR_NAVIGATION_WAIT_SECONDS = original
    source = Path(pie_check_fluid.__file__).read_text(encoding="utf-8")
    check("while time.monotonic() - started < EDITOR_NAVIGATION_WAIT_SECONDS" in source, "대기 상수가 wait_for_editor_navigation 안에서 모듈 전역으로 읽힌다", failures)
    commands = build_capture_arguments("base_a")
    check("--fresh-pie" in commands and "--stop-pie" in commands and commands[commands.index("--run-id") + 1] == "fog3d_calib_base_a" and commands[commands.index("--level") + 1] == LEVEL_PATH, "촬영 인수가 run.json 의 촬영 명령과 같은 구성", failures)
    with tempfile.TemporaryDirectory() as directory:
        global CALIBRATION_ROOT
        saved_root = CALIBRATION_ROOT
        CALIBRATION_ROOT = Path(directory)
        (Path(directory) / "t_restore").mkdir()
        (Path(directory) / "t_restore" / "run.json").write_text(json.dumps({"console_restore": ["r.VolumetricFog.GridSizeZ 64", "r.VolumetricFog.HistoryWeight 0.9", "x"]}), encoding="utf-8")
        check(restore_expectations("t_restore") == {"r.VolumetricFog.GridSizeZ": 64.0, "r.VolumetricFog.HistoryWeight": 0.9} and restore_expectations("t_none") == {}, "되돌림 기대값을 run.json 의 console_restore 에서 읽는다", failures)
        CALIBRATION_ROOT = saved_root
    tee_buffer = io.StringIO()
    TeeStream(io.StringIO(), tee_buffer).write("abc")
    check(tee_buffer.getvalue() == "abc", "Tee 가 두 곳에 쓴다", failures)
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else EXIT_SOME_FAILED


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="3D 안개 1c 탐색 촬영 실행기")
    parser.add_argument("names", nargs="*", help="실행 이름들(차례대로)")
    parser.add_argument("--index", help="색인 파일의 written 실행을 이름 뒤에 이어 붙인다")
    parser.add_argument("--navigation-wait-seconds", type=float, default=DEFAULT_NAVIGATION_WAIT_SECONDS)
    parser.add_argument("--original-navigation-wait", action="store_true", help="대기 상수를 바꾸지 않는다(120초 그대로)")
    parser.add_argument("--progress-dir", default=str(CALIBRATION_ROOT / "progress"))
    parser.add_argument("--no-retry", action="store_true", help="무효여도 다시 찍지 않는다")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argument_list)


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    arguments = parse_arguments(argument_list)
    if arguments.self_test:
        return run_self_test()
    try:
        return run_all(arguments)
    except (InputProblem, OSError, ValueError, KeyError) as error:
        print("입력 문제:", error)
        return EXIT_INPUT_PROBLEM


if __name__ == "__main__":
    sys.exit(main())
