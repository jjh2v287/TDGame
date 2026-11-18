"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, 새 무대 LV_TDFog3DGroundStage 와 재질 M_TD_Fog3DVolumeGround 가 만들어진 뒤, PowerShell 에서 실행: /Game 경로가 들어간 촬영 도구를 부른다). 3D 안개 2a-3 화면 확인 촬영 실행기: 실행 이름 목록 또는 묶음을 차례로 (촬영 → 로그 보관 → 유효 확인, 무효면 한 번 더)로 돌린다.
실행 명령: python -B Tools/FluidFX/fog3d_ground_run.py <실행 이름...> | --batch <묶음...> [--navigation-wait-seconds 5 | --original-navigation-wait] [--no-retry] [--no-preflight] [--progress-dir <폴더>], 점검만: --self-test
출력: <진행 폴더>/progress.json(실행마다 시작·끝 시각·종료 코드·시도 수·유효 판정), <진행 폴더>/done.flag(전체가 끝났을 때만), 실행마다 Saved/FluidFX/fog3d_ground/<이름>/{capture/, capture_stdout_<시도>.txt, log_slice.txt, report/report.json}. 종료 코드 0 모두 유효 / 1 무효·실패 실행 있음 / 2 입력 문제.
상태: fog3d_calib_run 의 촬영 함수(run_capture)를 가져다 쓰되 그 파일은 고치지 않고 이 프로세스 안에서만 CALIBRATION_ROOT·LEVEL_PATH 모듈 전역과 내비 대기 상수를 바꾼다. 에디터 없이 --self-test(가짜 촬영 함수로 재시도·진행 기록·사전 점검)만 확인했다.
"""
import argparse
import json
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
for directory in (ROOT / "Tools", ROOT / "Tools" / "FluidFX"):
    if str(directory) not in sys.path:
        sys.path.insert(0, str(directory))
import fog3d_calib_report as rep  # noqa: E402
import fog3d_calib_run as base_run  # noqa: E402
import fog3d_ground_report as ground_report  # noqa: E402
import fog3d_ground_stage as stage  # noqa: E402
import fog3d_ground_variants as gv  # noqa: E402
import fog3d_visible_check as vc  # noqa: E402

DEFAULT_NAVIGATION_WAIT_SECONDS = base_run.DEFAULT_NAVIGATION_WAIT_SECONDS
MAXIMUM_ATTEMPTS = base_run.MAXIMUM_ATTEMPTS
EXIT_ALL_VALID = 0
EXIT_SOME_FAILED = 1
EXIT_INPUT_PROBLEM = 2
STAGE_MAP_FILE_RELATIVE_PATH = ("Content", "FluidFX", "Maps", "LV_TDFog3DGroundStage.umap")


class InputProblem(Exception):
    pass


def now_text():
    return datetime.now().isoformat(timespec="seconds")


def configure_base_run(root):
    base_run.CALIBRATION_ROOT = Path(root)
    base_run.LEVEL_PATH = stage.STAGE_MAP_PATH


def resolve_names(arguments, root):
    names = list(arguments.names or [])
    if arguments.batch:
        index_path = Path(root) / gv.INDEX_FILE_NAME
        if not index_path.is_file():
            raise InputProblem(f"색인이 없다: {index_path} (fog3d_ground_variants.py --batch 로 먼저 만든다)")
        index = json.loads(index_path.read_text(encoding="utf-8"))
        for batch in arguments.batch:
            members = index.get("batches", {}).get(batch)
            if members is None:
                raise InputProblem(f"색인에 없는 묶음: {batch}")
            names += [name for name in members if name not in names]
    if not names:
        raise InputProblem("실행 이름 또는 --batch 가 필요하다")
    for name in names:
        if not (Path(root) / name / "variants.json").is_file():
            raise InputProblem(f"{name}: variants.json 이 없다(fog3d_ground_variants.py 로 먼저 만든다)")
    return names


def preflight(names, root, skip_files):
    problems, warnings = [], []
    canonical_sha = gv.base.sha256_of_file(gv.base.CANONICAL_TUNING_PATH)
    for name in names:
        run_record = json.loads((Path(root) / name / "run.json").read_text(encoding="utf-8"))
        if run_record.get("level") != stage.STAGE_MAP_PATH:
            problems.append(f"{name}: 실행 기록의 맵이 {run_record.get('level')}")
        if run_record.get("canonical_tuning_sha256") != canonical_sha:
            warnings.append(f"{name}: 정본 JSON 이 변형을 만든 뒤 바뀌었다(다시 만들기 권장)")
        if run_record.get("follow") and not (Path(root) / name / "tuning.json").is_file():
            problems.append(f"{name}: tuning.json 이 없다")
    if not skip_files:
        for label, parts in (("무대 맵", STAGE_MAP_FILE_RELATIVE_PATH), ("땅 기준 재질", stage.GROUND_MATERIAL_FILE_RELATIVE_PATH)):
            if not ROOT.joinpath(*parts).is_file():
                problems.append(f"{label} 파일이 없다: {ROOT.joinpath(*parts).as_posix()}")
    return problems, sorted(set(warnings))


def progress_label(arguments):
    parts = list(arguments.batch or [])
    if arguments.names:
        parts.append("names_" + "_".join(arguments.names[:3]))
    return "+".join(parts) or "run"


def record_quality_values(name, root):
    values = ground_report.read_quality_values(Path(root) / name)
    run_path = Path(root) / name / "run.json"
    run_record = json.loads(run_path.read_text(encoding="utf-8"))
    run_record.setdefault("quality_cvar", {})["log_values"] = values
    run_path.write_text(json.dumps(run_record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return values


def write_progress(progress_directory, record):
    base_run.write_progress(progress_directory, record)


def judge_attempt(name, root):
    report, _, _ = ground_report.analyze_run(Path(root) / name, True)
    return {"valid": bool(report["valid"]), "usable": report.get("usable"), "reasons": list(report["invalid_reasons"]), "notes": list(report.get("notes", []))}


def run_all(arguments, capture_function=None, snapshot_function=None, load_modules=None):
    root = Path(arguments.out_root)
    names = resolve_names(arguments, root)
    problems, warnings = preflight(names, root, arguments.no_preflight)
    for line in warnings:
        print("경고:", line)
    if problems:
        raise InputProblem("사전 점검 실패: " + " | ".join(problems))
    configure_base_run(root)
    progress_directory = Path(arguments.progress_dir) if arguments.progress_dir else root / "progress" / progress_label(arguments)
    done_flag = progress_directory / "done.flag"
    progress_directory.mkdir(parents=True, exist_ok=True)
    if done_flag.exists():
        done_flag.unlink()
    wait_seconds = None if arguments.original_navigation_wait else arguments.navigation_wait_seconds
    pie_check_module, lookdev_module, original_wait = (load_modules or base_run.load_capture_modules)(wait_seconds)
    capture = capture_function or base_run.run_capture
    snapshot = snapshot_function or (lambda run_name: rep.snapshot_run_log(root / run_name, vc.DEFAULT_LOG_PATH))
    record = {"started_at": now_text(), "finished_at": None, "navigation_wait_seconds_used": getattr(pie_check_module, "EDITOR_NAVIGATION_WAIT_SECONDS", None),
              "navigation_wait_seconds_original": original_wait, "names": names, "preflight_warnings": warnings, "runs": []}
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
            exit_code, navigation_lines, stdout_path = capture(lookdev_module, name, attempt)
            attempt_record.update({"capture_exit_code": exit_code, "navigation_state_lines": navigation_lines, "capture_wall_seconds": round(time.monotonic() - started, 1), "stdout": str(stdout_path)})
            snapshot_count, has_summary = snapshot(name)
            attempt_record["log_lines"], attempt_record["log_has_summary"] = snapshot_count, has_summary
            attempt_record["quality_cvar_log_values"] = record_quality_values(name, root)
            attempt_record["validity"] = judge_attempt(name, root)
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
        print(f"== {name}: {'유효' if entry['final_valid'] else '무효·실패'} 시도 {len(entry['attempts'])}회, 마지막 {last['wall_seconds']}초, 내비 대기 줄 {last['navigation_state_lines']}" + ("" if entry["final_valid"] else " 이유: " + "; ".join(last["validity"]["reasons"])[:120]), flush=True)
    record["finished_at"] = now_text()
    record["failed_names"] = failed
    write_progress(progress_directory, record)
    done_flag.write_text(f"done {now_text()} failed={','.join(failed) or '-'}\n", encoding="utf-8", newline="\n")
    return EXIT_SOME_FAILED if failed else EXIT_ALL_VALID


def run_self_test():
    failures = []

    def check(condition, label, detail=""):
        if not condition:
            failures.append(label)
        print(("통과 " if condition else "실패 ") + label + (": " + detail if detail else ""))

    saved = (base_run.CALIBRATION_ROOT, base_run.LEVEL_PATH)
    canonical = gv.base.load_canonical_tuning()
    field_types = gv.infer_field_types(canonical)
    solids = stage.build_solids()
    specs = {spec["name"]: spec for spec in gv.all_specifications()}
    try:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            configure_base_run(root)
            capture_arguments = base_run.build_capture_arguments("s_F_side_on")
            check(capture_arguments[capture_arguments.index("--level") + 1] == "/Game/FluidFX/Maps/LV_TDFog3DGroundStage" and (root / "s_F_side_on" / "variants.json").as_posix() in capture_arguments, "촬영 인수가 새 맵과 새 폴더를 가리킴(촬영 도구 모듈 전역 덮어쓰기)")
            check(capture_arguments.count("--fresh-pie") == 1 and capture_arguments.count("--stop-pie") == 1, "새 PIE·끝나면 PIE 정지 인수")
            names = ["s_F_side_on", "w_W01_back_on", "r_R01_side_on"]
            for name in names:
                gv.materialize(specs[name], root, canonical, field_types, gv.base.sha256_of_file(gv.base.CANONICAL_TUNING_PATH), solids, [], False, True)
            gv.update_index(root, [specs[name] for name in names])
            arguments = parse_arguments(["--batch", "static", "--out-root", str(root), "--no-preflight", "--progress-dir", str(root / "progress")])
            check(resolve_names(arguments, root) == ["s_F_side_on"], "--batch 가 색인에서 이름을 찾음")
            problems, warnings = preflight(names, root, False)
            check(any("무대 맵" in line for line in problems) and any("땅 기준 재질" in line for line in problems) or ROOT.joinpath(*STAGE_MAP_FILE_RELATIVE_PATH).is_file(), "사전 점검: 무대 맵·재질 파일이 없으면 막음", "; ".join(problems)[:80])
            check(not preflight(names, root, True)[0] and not warnings, "사전 점검: 파일 검사를 끄면 통과, 정본 해시 일치")
            calls = {"count": 0}

            def fake_capture(lookdev_module, name, attempt):
                calls["count"] += 1
                if name == "w_W01_back_on" and attempt == 1:
                    return 1, 0, root / name / "capture_stdout_1.txt"
                ground_report.build_synthetic_run(root, specs[name], canonical, field_types, solids, 1.0)
                return 0, 0, root / name / f"capture_stdout_{attempt}.txt"

            class FakeModule:
                EDITOR_NAVIGATION_WAIT_SECONDS = 5.0

            def fake_modules(wait_seconds):
                return FakeModule, None, 120.0

            run_arguments = parse_arguments([*names, "--out-root", str(root), "--no-preflight", "--progress-dir", str(root / "progress")])
            exit_code = run_all(run_arguments, fake_capture, lambda name: (3, True), fake_modules)
            progress = json.loads((root / "progress" / "progress.json").read_text(encoding="utf-8"))
            check(exit_code == EXIT_ALL_VALID and calls["count"] == 4, "가짜 촬영: 실패한 실행만 한 번 더 찍어 모두 유효", f"촬영 호출 {calls['count']}")
            check([len(entry["attempts"]) for entry in progress["runs"]] == [1, 2, 1] and (root / "progress" / "done.flag").is_file(), "진행 기록: 시도 수 1·2·1, 끝 표시 파일")
            check(all(entry["final_valid"] for entry in progress["runs"]), "진행 기록: 모든 실행 최종 유효")
            check(parse_arguments(["--batch", "static"]).batch == ["static"] and progress_label(parse_arguments(["--batch", "static", "trial_off"])) == "static+trial_off" and progress_label(parse_arguments(["a", "b"])) == "names_a_b", "진행 기록 폴더 이름이 묶음마다 다름")
            check(json.loads((root / "w_W01_back_on" / "run.json").read_text(encoding="utf-8"))["quality_cvar"]["log_values"] == [-1, -1, -1], "실행 기록 quality_cvar 에 로그 값이 적힘")
            retry_arguments = parse_arguments(["w_W01_back_on", "--out-root", str(root), "--no-preflight", "--no-retry", "--progress-dir", str(root / "progress2")])
            calls["count"] = 0
            (root / "w_W01_back_on" / "capture").joinpath("00_g_schedule.json").unlink()
            failing = run_all(retry_arguments, lambda module, name, attempt: (1, 0, root / name / "x.txt"), lambda name: (0, False), fake_modules)
            check(failing == EXIT_SOME_FAILED, "촬영 실패 + 재시도 끔 → 종료 코드 1")
            try:
                resolve_names(parse_arguments(["nope", "--out-root", str(root)]), root)
                check(False, "없는 실행 이름은 입력 문제")
            except InputProblem:
                check(True, "없는 실행 이름은 입력 문제")
    finally:
        base_run.CALIBRATION_ROOT, base_run.LEVEL_PATH = saved
    check(base_run.LEVEL_PATH == "/Game/FluidFX/Maps/LV_TDFog3DStage", "시험이 끝나면 촬영 실행기 모듈 전역을 원래대로 되돌림")
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else EXIT_SOME_FAILED


def parse_arguments(argument_list):
    parser = argparse.ArgumentParser(description="3D 안개 2a-3 화면 확인 촬영 실행기")
    parser.add_argument("names", nargs="*", help="실행 이름들(차례대로)")
    parser.add_argument("--batch", nargs="+", help="색인의 묶음 이름들")
    parser.add_argument("--out-root", default=str(gv.GROUND_ROOT))
    parser.add_argument("--navigation-wait-seconds", type=float, default=DEFAULT_NAVIGATION_WAIT_SECONDS)
    parser.add_argument("--original-navigation-wait", action="store_true")
    parser.add_argument("--progress-dir")
    parser.add_argument("--no-retry", action="store_true")
    parser.add_argument("--no-preflight", action="store_true", help="무대 맵·재질 파일 존재 검사를 건너뛴다")
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
