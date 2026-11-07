"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드·쿠킹 불필요), fxp_audit.py package 가 늦은 import 로 부른다.
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py package --tag <min|fluid> (--cook-report <cook_<label>.json> --cook-before-sha <sha|none> --frames-report <frames_<label>.json> --frames-before-sha <sha|none> --staged-root Saved/StagedBuilds/Windows | --aborted <사유>) --out <json> [--md <md>] [--root <사본>](defs PackageRun·RunX5).
출력: 패키지 첫 읽기 점검(runbook-fxp-v3 3.7), 모든 분기 같은 스키마: verdict(미검증(실행 전 중단: …)·미검증(쿠킹 실패: …)·미검증(기동 결과 없음)·첫 읽기 확인·첫 읽기 실패(①~④))·branch·cook·launch·checks ①~④·records(TDFluidGpu 줄 수·GPU 솔버·셰이더 매핑 미검증). 종료 0 / 2 인수 오류만.
상태: 2026-11-07 FX-P X5a(B5) 구현, fixture fixtures_package.py(분기 5종·인수 오류·같은 바이트), 실제 쿠킹·기동 없이 기존 cook_*·frames_* 로 시험.
"""
import json
import re
from pathlib import Path

import common

PROJECT_NAME = "TDGame"
PRESET_DIRECTORY = "Content/FluidFX/PresetData"
PRESET_FILES = ("TDFluidPresetTuning.json", "TDFluidMaterialLook.json")
MANIFEST_NAME = "Manifest_NonUFSFiles_Win64.txt"
TAG_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]*$")
SHA_PATTERN = re.compile(r"^([0-9a-f]{64}|none)$")
COOK_SUCCESS_PATTERN = re.compile(r"ExitCode=0 \(Success\)|BUILD SUCCESSFUL", re.IGNORECASE)
TUNING_CATEGORY = "LogTDFluidPresetTuning"
TUNING_LOADED_PATTERN = re.compile(r"Preset tuning .* \(loaded\), material look .* \(loaded\): 0 errors")
TUNING_ERROR_MARK = "LogTDFluidPresetTuning: Error"
GPU_MODULE_MARK = "TDFluidGpu"
GPU_UNVERIFIED = "미검증(전용 로그 줄 없음)"
EXPECTED_LAUNCH_EXIT_CODE = 777003
UE_LOG_PREFIX = re.compile(r"^\[\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3}\]\[\s*\d+\]")
ABSOLUTE_PATH = re.compile(r"(?<![A-Za-z])[A-Za-z]:[\\/][^\s'\"<>|]*")
ABSOLUTE_PATH_MARK = "<절대 경로>"
OUTSIDE_ROOT_MARK = "<루트 밖>"
LINE_LIMIT = 300
ERROR_LINE_LIMIT = 5
STATUS_PASS = "통과"
STATUS_FAIL = "실패"
STATUS_SKIPPED = "미평가"
CHECK_TITLES = {"①": "매니페스트에 PresetData 두 JSON", "②": "스테이징 두 파일 sha = 원본", "③": "LogTDFluidPresetTuning loaded·0 errors 줄 ≥1",
                "④": "LogTDFluidPresetTuning: Error 0줄"}
STAGING_CHECKS = ("①", "②")
LOG_CHECKS = ("③", "④")
RUN_ARGUMENTS = ("cook_report", "cook_before_sha", "frames_report", "frames_before_sha", "staged_root")
STALE_REPORT = "결과 JSON sha가 실행 전과 같음"
MISSING_REPORT = "결과 JSON 없음"
BROKEN_REPORT = "결과 JSON 형식 오류"
BRANCH_ABORTED = "aborted"
BRANCH_COOK_FAILED = "cook_failed"
BRANCH_LAUNCH_MISSING = "launch_missing"
BRANCH_CONFIRMED = "confirmed"
BRANCH_FAILED = "failed"
VERDICT_CONFIRMED = "첫 읽기 확인"
VERDICT_LAUNCH_MISSING = "미검증(기동 결과 없음)"


def add_arguments(parser):
    parser.add_argument("--tag", required=True, help="package-<tag>.json 의 tag(min|fluid)")
    parser.add_argument("--aborted", default=None, help="실행 전 중단 사유(이때 실행 인수는 주지 않는다)")
    parser.add_argument("--cook-report", default=None, help="cook_single_map.py 결과 Saved/AgentOps/cook_<label>.json")
    parser.add_argument("--cook-before-sha", default=None, help="쿠킹 전 cook JSON sha256 또는 none")
    parser.add_argument("--frames-report", default=None, help="measure_game_frames.py 결과 Saved/AgentOps/frames_<label>.json")
    parser.add_argument("--frames-before-sha", default=None, help="기동 전 frames JSON sha256 또는 none")
    parser.add_argument("--staged-root", default=None, help="스테이징 루트 Saved/StagedBuilds/Windows")


def option_name(name):
    return "--" + name.replace("_", "-")


def validate_arguments(args):
    if not TAG_PATTERN.match(str(args.tag)):
        raise common.InputError(f"--tag 형식(소문자·숫자·-): {args.tag}")
    given = [option_name(name) for name in RUN_ARGUMENTS if getattr(args, name) is not None]
    if args.aborted is not None:
        if given:
            raise common.InputError(f"--aborted 와 실행 인수를 함께 줄 수 없음: {' '.join(given)}")
        if not common.is_text(args.aborted):
            raise common.InputError("--aborted 사유가 비었음")
        return
    missing = [option_name(name) for name in RUN_ARGUMENTS if getattr(args, name) is None]
    if missing:
        raise common.InputError(f"실행 인수 없음: {' '.join(missing)}(실행 전 중단이면 --aborted <사유>)")


def root_relative(root, value):
    base = Path(root).resolve()
    relative = common.normalize_relative(base, value)
    try:
        return (base / relative).resolve().relative_to(base).as_posix()
    except ValueError:
        return None


def argument_path(root, args, name):
    relative = root_relative(root, getattr(args, name))
    if relative is None:
        raise common.InputError(f"{option_name(name)} 가 루트 밖: {getattr(args, name)}")
    return relative


def argument_sha(args, name):
    value = str(getattr(args, name)).strip().lower()
    if not SHA_PATTERN.match(value):
        raise common.InputError(f"{option_name(name)} 형식(sha256 64자 또는 none): {getattr(args, name)}")
    return value


def clean_line(root, text):
    value = UE_LOG_PREFIX.sub("", str(text).strip())
    value = ABSOLUTE_PATH.sub(ABSOLUTE_PATH_MARK, common.strip_root(value, root))
    return " ".join(value.split())[:LINE_LIMIT]


def clean_optional(root, value):
    return None if value is None else clean_line(root, value)


def clean_scalar(root, value):
    if value is None or isinstance(value, (bool, int, float)):
        return value
    return clean_line(root, value)


def clean_path(root, value):
    if not common.is_text(value):
        return None
    relative = root_relative(root, value)
    return relative if relative is not None else OUTSIDE_ROOT_MARK


def powershell_text(value):
    if value is None:
        return ""
    if isinstance(value, bool):
        return "True" if value else "False"
    return str(value)


def read_optional(reads, relative):
    try:
        return reads.read_bytes(relative, required=False)
    except OSError:
        return None


def decode(data):
    return data.decode("utf-8-sig", errors="replace")


def sha_of(data):
    return common.sha256_bytes(data) if data is not None else None


def parse_report(data):
    try:
        document = json.loads(decode(data))
    except ValueError:
        return None
    return document if isinstance(document, dict) else None


def report_problem(data, refreshed, document):
    if data is None:
        return MISSING_REPORT
    if not refreshed:
        return STALE_REPORT
    if document is None:
        return BROKEN_REPORT
    return None


def read_report(reads, relative, before):
    data = read_optional(reads, relative)
    sha = sha_of(data)
    refreshed = sha is not None and sha != before
    document = parse_report(data) if refreshed else None
    state = {"evaluated": True, "report": relative, "before_sha256": before, "sha256": sha, "refreshed": refreshed}
    return state, report_problem(data, refreshed, document), document


def empty_cook(relative=None, before=None):
    return {"evaluated": False, "report": relative, "before_sha256": before, "sha256": None, "refreshed": None, "ok": None, "causes": [],
            "exit_code": None, "success_line": None, "seconds": None, "error_count": None, "warning_count": None, "error_lines": [], "map": None, "log": None}


def empty_launch(relative=None, before=None, reason=None):
    return {"evaluated": False, "skipped_reason": reason, "report": relative, "before_sha256": before, "sha256": None, "refreshed": None, "present": None,
            "missing_reason": None, "exit_code": None, "exit_code_expected": EXPECTED_LAUNCH_EXIT_CODE, "exit_code_is_expected": None, "crash_lines": [],
            "map": None, "log": None}


def cook_causes(document):
    causes = []
    exit_text = powershell_text(document.get("exit_code"))
    if exit_text != "0":
        causes.append(f"exit_code {exit_text or '없음'}")
    if not COOK_SUCCESS_PATTERN.search(powershell_text(document.get("success_line"))):
        causes.append("성공 줄 없음")
    return causes


def cook_records(root, document):
    problems = document.get("first_problems") if isinstance(document.get("first_problems"), list) else []
    return {"exit_code": clean_scalar(root, document.get("exit_code")), "success_line": clean_optional(root, document.get("success_line")),
            "seconds": clean_scalar(root, document.get("seconds")), "error_count": clean_scalar(root, document.get("error_count")),
            "warning_count": clean_scalar(root, document.get("warning_count")), "map": clean_optional(root, document.get("map")),
            "error_lines": [clean_line(root, line) for line in problems if isinstance(line, str) and "rror" in line], "log": clean_path(root, document.get("log"))}


def evaluate_cook(reads, root, relative, before):
    state, problem, document = read_report(reads, relative, before)
    cook = empty_cook(relative, before)
    cook.update(state)
    if problem is not None:
        cook.update({"ok": False, "causes": [problem]})
        return cook
    causes = cook_causes(document)
    cook.update(cook_records(root, document))
    cook.update({"ok": not causes, "causes": causes})
    return cook


def launch_records(root, document):
    exit_code = clean_scalar(root, document.get("exit_code"))
    crash = document.get("crash_lines") if isinstance(document.get("crash_lines"), list) else []
    return {"exit_code": exit_code, "exit_code_is_expected": exit_code == EXPECTED_LAUNCH_EXIT_CODE,
            "crash_lines": [clean_line(root, line) for line in crash], "map": clean_optional(root, document.get("map")), "log": clean_path(root, document.get("log"))}


def evaluate_launch(reads, root, relative, before):
    state, problem, document = read_report(reads, relative, before)
    launch = empty_launch(relative, before)
    launch.update(state)
    if problem is None:
        launch.update(launch_records(root, document))
        problem = None if common.is_text(document.get("log")) else "log null"
    launch.update({"present": problem is None, "missing_reason": problem})
    return launch, (document or {}).get("log")


def check_entry(check_id, status, detail, **facts):
    entry = {"id": check_id, "title": CHECK_TITLES[check_id], "status": status, "detail": detail}
    entry.update(facts)
    return entry


def skipped_checks(check_ids, reason):
    return [check_entry(check_id, STATUS_SKIPPED, reason) for check_id in check_ids]


def staged_entry(name):
    return f"{PROJECT_NAME}/{PRESET_DIRECTORY}/{name}"


def check_manifest(reads, staged_root):
    relative = f"{staged_root}/{MANIFEST_NAME}"
    data = read_optional(reads, relative)
    if data is None:
        return check_entry("①", STATUS_FAIL, f"매니페스트 없음: {relative}", manifest=relative)
    entries = {line.split("\t", 1)[0].strip().replace("\\", "/").lower() for line in decode(data).splitlines() if line.strip()}
    missing = [name for name in PRESET_FILES if staged_entry(name).lower() not in entries]
    if missing:
        return check_entry("①", STATUS_FAIL, f"매니페스트에 없음: {', '.join(missing)}", manifest=relative)
    return check_entry("①", STATUS_PASS, f"두 줄 있음({PROJECT_NAME}/{PRESET_DIRECTORY})", manifest=relative)


def copy_problem(source_sha, staged_sha):
    if staged_sha is None:
        return "스테이징 없음"
    if source_sha is None:
        return "원본 없음"
    if source_sha != staged_sha:
        return "sha 다름"
    return None


def check_staged_copies(reads, staged_root):
    files, problems = [], []
    for name in PRESET_FILES:
        source, staged = f"{PRESET_DIRECTORY}/{name}", f"{staged_root}/{staged_entry(name)}"
        source_sha, staged_sha = sha_of(read_optional(reads, source)), sha_of(read_optional(reads, staged))
        files.append({"name": name, "source": source, "source_sha256": source_sha, "staged": staged, "staged_sha256": staged_sha})
        problem = copy_problem(source_sha, staged_sha)
        if problem is not None:
            problems.append(f"{name} {problem}")
    if problems:
        return check_entry("②", STATUS_FAIL, " · ".join(problems), files=files)
    return check_entry("②", STATUS_PASS, "두 파일 sha 같음", files=files)


def read_game_log(reads, root, log_value):
    relative = root_relative(root, log_value)
    if relative is None:
        return None, "로그가 루트 밖"
    data = read_optional(reads, relative)
    if data is None:
        return None, f"로그 파일 없음: {relative}"
    return decode(data).splitlines(), None


def check_loaded_line(root, lines):
    matched = [line for line in lines if TUNING_CATEGORY in line and TUNING_LOADED_PATTERN.search(line)]
    first = clean_line(root, matched[0]) if matched else None
    if not matched:
        return check_entry("③", STATUS_FAIL, "loaded·0 errors 줄 없음", loaded_line_count=0, first_loaded_line=None)
    return check_entry("③", STATUS_PASS, f"{len(matched)}줄", loaded_line_count=len(matched), first_loaded_line=first)


def check_error_lines(root, lines):
    errors = [line for line in lines if TUNING_ERROR_MARK in line]
    shown = [clean_line(root, line) for line in errors[:ERROR_LINE_LIMIT]]
    if errors:
        return check_entry("④", STATUS_FAIL, f"Error {len(errors)}줄", error_line_count=len(errors), error_lines=shown)
    return check_entry("④", STATUS_PASS, "Error 0줄", error_line_count=0, error_lines=[])


def log_checks(root, lines, problem):
    if lines is None:
        return [check_entry(check_id, STATUS_FAIL, problem) for check_id in LOG_CHECKS]
    return [check_loaded_line(root, lines), check_error_lines(root, lines)]


def records(lines=None):
    count = None if lines is None else sum(1 for line in lines if GPU_MODULE_MARK in line)
    return {"tdfluidgpu_line_count": count, "gpu_solver_first_launch": GPU_UNVERIFIED, "shader_mapping": GPU_UNVERIFIED}


def result(tag, branch, verdict, cook, launch, checks, staged_root=None, aborted_reason=None, lines=None):
    return {"tag": tag, "branch": branch, "verdict": verdict, "aborted_reason": aborted_reason, "staged_root": staged_root, "cook": cook, "launch": launch,
            "checks": checks, "records": records(lines)}


def aborted_result(root, args):
    reason = clean_line(root, args.aborted)
    checks = skipped_checks(STAGING_CHECKS + LOG_CHECKS, "실행 전 중단")
    return result(args.tag, BRANCH_ABORTED, f"미검증(실행 전 중단: {reason})", empty_cook(), empty_launch(reason="실행 전 중단"), checks, aborted_reason=reason)


def run_result(reads, root, args):
    paths = {name: argument_path(root, args, name) for name in ("cook_report", "frames_report", "staged_root")}
    shas = {name: argument_sha(args, name) for name in ("cook_before_sha", "frames_before_sha")}
    staged_root = paths["staged_root"]
    cook = evaluate_cook(reads, root, paths["cook_report"], shas["cook_before_sha"])
    if not cook["ok"]:
        launch = empty_launch(paths["frames_report"], shas["frames_before_sha"], "쿠킹 실패로 기동 안 함")
        checks = skipped_checks(STAGING_CHECKS + LOG_CHECKS, "쿠킹 실패")
        return result(args.tag, BRANCH_COOK_FAILED, f"미검증(쿠킹 실패: {'·'.join(cook['causes'])})", cook, launch, checks, staged_root)
    staging = [check_manifest(reads, staged_root), check_staged_copies(reads, staged_root)]
    launch, log_value = evaluate_launch(reads, root, paths["frames_report"], shas["frames_before_sha"])
    if not launch["present"]:
        checks = staging + skipped_checks(LOG_CHECKS, "기동 결과 없음")
        return result(args.tag, BRANCH_LAUNCH_MISSING, VERDICT_LAUNCH_MISSING, cook, launch, checks, staged_root)
    lines, log_problem = read_game_log(reads, root, log_value)
    checks = staging + log_checks(root, lines, log_problem)
    failed = [check["id"] for check in checks if check["status"] != STATUS_PASS]
    if failed:
        return result(args.tag, BRANCH_FAILED, f"첫 읽기 실패({'·'.join(failed)})", cook, launch, checks, staged_root, lines=lines)
    return result(args.tag, BRANCH_CONFIRMED, VERDICT_CONFIRMED, cook, launch, checks, staged_root, lines=lines)


def shown(value):
    return "없음" if value is None or value == "" else str(value)


def cook_line(cook):
    if not cook["evaluated"]:
        return "평가 안 함(실행 전 중단)"
    if not cook["refreshed"]:
        return f"보고 {cook['report']} · 갱신 아니오 · 실패 원인 {'·'.join(cook['causes'])}"
    parts = [f"보고 {cook['report']}", f"갱신 {'예' if cook['refreshed'] else '아니오'}", f"exit_code {shown(cook['exit_code'])}",
             f"성공 줄 {shown(cook['success_line'])}", f"오류 {shown(cook['error_count'])}", f"경고 {shown(cook['warning_count'])}", f"소요 {shown(cook['seconds'])}"]
    if cook["causes"]:
        parts.append(f"실패 원인 {'·'.join(cook['causes'])}")
    return " · ".join(parts)


def launch_line(launch):
    if not launch["evaluated"]:
        return f"평가 안 함({launch['skipped_reason']})"
    exit_text = shown(launch["exit_code"])
    if launch["exit_code"] is not None:
        exit_text += "(777003 정상)" if launch["exit_code_is_expected"] else "(777003 아님)"
    parts = [f"보고 {launch['report']}", f"갱신 {'예' if launch['refreshed'] else '아니오'}", f"로그 {shown(launch['log'])}",
             f"exit_code {exit_text}", f"crash_lines {len(launch['crash_lines'])}"]
    if launch["missing_reason"]:
        parts.append(f"기동 결과 없음 원인 {launch['missing_reason']}")
    return " · ".join(parts)


def markdown_lines(document):
    cook, launch, facts = document["cook"], document["launch"], document["records"]
    lines = [f"# FX-P 패키지 점검 ({document['tag']})", "", f"- verdict: {document['verdict']}", f"- 분기: {document['branch']}"]
    if document["aborted_reason"] is not None:
        lines.append(f"- 실행 전 중단 사유: {document['aborted_reason']}")
    lines += [f"- 쿠킹: {cook_line(cook)}", f"- 기동: {launch_line(launch)}", "", "| 번호 | 점검 | 상태 | 근거 |", "|---|---|---|---|"]
    lines += [f"| {check['id']} | {common.markdown_cell(check['title'])} | {check['status']} | {common.markdown_cell(check['detail'])} |" for check in document["checks"]]
    lines += ["", f"- 기록만: TDFluidGpu 줄 {shown(facts['tdfluidgpu_line_count'])} · GPU 솔버 첫 기동 {facts['gpu_solver_first_launch']} · 셰이더 매핑 {facts['shader_mapping']}"]
    if cook["error_lines"]:
        lines += ["", "쿠킹 오류 줄(기록만, FU-14):"] + [f"- {common.markdown_cell(line)}" for line in cook["error_lines"]]
    if launch["crash_lines"]:
        lines += ["", "기동 crash_lines(기록만):"] + [f"- {common.markdown_cell(line)}" for line in launch["crash_lines"]]
    return lines


def summary_text(document):
    statuses = [check["status"] for check in document["checks"]]
    return (f"tag={document['tag']} branch={document['branch']} pass={statuses.count(STATUS_PASS)} fail={statuses.count(STATUS_FAIL)} "
            f"unevaluated={statuses.count(STATUS_SKIPPED)}")


def build(context):
    args, root = context.args, context.root
    validate_arguments(args)
    document = aborted_result(root, args) if args.aborted is not None else run_result(context.reads, root, args)
    return common.Outcome(document, markdown_lines(document), summary_text(document))
