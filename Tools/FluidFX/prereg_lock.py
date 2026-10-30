"""실행 환경: 시스템 Python 3(에디터 불필요, PowerShell 또는 Git Bash). 보정 판정 규칙을 결과를 열기 전에 잠그는 sha256 사전 등록 잠금을 만들고 검증한다(spec-wave0-data-bridge-v2 3.9 6단계, 수기 선례 Saved/AgentOps/20261024/fx4b-acc/prereg-lock-budget.txt). 잠금 v2 는 닫힌 세계다(spec-cal0-v2 C8): 판정기가 읽는 필수 집합이 모두 잠겨 있어야 한다.
실행 명령: python Tools/FluidFX/prereg_lock.py lock --out Saved/FluidFX/calib/<id>-prereg-lock.json (--plan Tools/FluidFX/calib/plans/<id>.json [--file <경로>...] | --file <경로>...) [--note "<메모>"] | verify --lock <잠금 JSON> [--sweep Saved/FluidFX/calib/<run>/sweep.json]
출력: lock 은 잠금 JSON v2({tool, version 2, created_at 시간대 포함 초 단위 ISO 시각(내림이라 같은 초에 시작한 스윕을 늦은 잠금으로 오판하지 않음), note, files: {프로젝트 상대 경로: sha256}}, --plan 이면 필수 집합 = 계획·장면 영역 틀·목표·목표 안 live2_targets·장면 offline.command 의 .py 스크립트·판정 도구 7개에 --file 을 더함, 이미 있으면 덮어쓰지 않음), verify 는 파일별 일치·불일치를, --sweep 이면 닫힌 세계 위반(필수 파일 누락, v1 잠금 + stage 스윕, 스윕 lock_sha256 불일치·없음, 잠금 created_at > 스윕 started_at, 스윕 장면 regions_template·targets ≠ 계획)까지 표준 출력에. 종료 코드 0 일치 / 1 lock 입력 오류 / 2 잠금 없음·형식 오류·불일치·파일 없음·닫힌 세계 위반
상태: 오프라인 검사만(2026-10-25 잠금 생성·일치·한 바이트 변경 불일치·잠금 없음, 2026-10-30 CAL-0 B4b 잠금 v2·check_sweep_lock 이 calib_offline_test check_cal0_measure 통과. calib_judge.check_lock 은 아직 verify_lock 만 부른다).
"""
import argparse
import hashlib
import json
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JUDGE_TOOL_FILES = tuple(f"Tools/FluidFX/{name}.py" for name in ("calib_judge", "measure_fx_look", "prereg_lock", "pie_probe_fluid_water", "calib_sweep", "fluid_tuning_patch", "pie_lookdev_fluid"))
LIVE2_TARGETS_KEY = "live2_targets"
SCENE_FILE_KEYS = ("regions_template", "targets")
LOCK_VERSION = 2
EXIT_LOCK_INPUT = 1
EXIT_MISMATCH = 2


class LockError(Exception):
    pass


def project_path(text):
    path = Path(text)
    return path if path.is_absolute() else ROOT / path


def relative_text(path):
    resolved = Path(path).resolve()
    try:
        return resolved.relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def offline_scripts(scene):
    offline = scene.get("offline")
    command = offline.get("command") if isinstance(offline, dict) else None
    if not isinstance(command, list):
        return []
    return [part for part in command if isinstance(part, str) and part.endswith(".py") and "{" not in part]


def required_files(plan_path):
    plan = load_json(project_path(plan_path))
    files = [relative_text(project_path(plan_path))]
    for scene in plan.get("scenes", []):
        files.extend(relative_text(project_path(scene[key])) for key in SCENE_FILE_KEYS if isinstance(scene.get(key), str))
        if isinstance(scene.get("targets"), str):
            selection = load_json(project_path(scene["targets"]))
            if isinstance(selection.get(LIVE2_TARGETS_KEY), str):
                files.append(relative_text(project_path(selection[LIVE2_TARGETS_KEY])))
        files.extend(relative_text(project_path(script)) for script in offline_scripts(scene))
    files.extend(JUDGE_TOOL_FILES)
    return list(dict.fromkeys(files))


plan_lock_files = required_files


def create_lock(file_texts, out_path, note=""):
    out_path = Path(out_path)
    if out_path.exists():
        raise LockError(f"{out_path}: 잠금이 이미 있다(사전 등록은 덮어쓰지 않는다)")
    files = {}
    for text in file_texts:
        path = project_path(text)
        if not path.is_file():
            raise LockError(f"잠글 파일이 없다: {text}")
        files[relative_text(path)] = file_sha256(path)
    lock = {"tool": "prereg_lock", "version": LOCK_VERSION, "created_at": datetime.now().astimezone().isoformat(timespec="seconds"), "note": note, "files": files}
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(lock, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return lock


def verify_lock(lock_path):
    lock_path = Path(lock_path)
    if not lock_path.is_file():
        return False, [f"잠금 없음: {lock_path}"], None
    try:
        lock = json.loads(lock_path.read_text(encoding="utf-8"))
    except ValueError as error:
        return False, [f"잠금 형식 오류: {error}"], None
    files = lock.get("files") if isinstance(lock, dict) else None
    if not isinstance(files, dict) or not files:
        return False, ["잠금에 files 가 없다"], lock
    problems = []
    for text, expected in sorted(files.items()):
        path = project_path(text)
        if not path.is_file():
            problems.append(f"파일 없음: {text}")
        elif file_sha256(path) != expected:
            problems.append(f"sha256 불일치: {text}")
    return not problems, problems, lock


def parse_moment(text):
    moment = datetime.fromisoformat(text)
    return moment if moment.tzinfo else moment.astimezone()


def normalized_path_text(text):
    return relative_text(project_path(text)) if isinstance(text, str) else text


def find_scene_path_problems(sweep, plan):
    plan_scenes = {scene.get("name"): scene for scene in plan.get("scenes", [])}
    problems = []
    for scene in sweep.get("scenes", []):
        plan_scene = plan_scenes.get(scene.get("name"))
        if plan_scene is None:
            problems.append(f"스윕 장면 {scene.get('name')} 이 계획에 없다")
            continue
        problems.extend(f"스윕 장면 {scene['name']} 의 {key} {scene.get(key)} ≠ 계획 {plan_scene.get(key)}"
                        for key in SCENE_FILE_KEYS if normalized_path_text(scene.get(key)) != normalized_path_text(plan_scene.get(key)))
    return problems


def find_record_problems(sweep, lock_path, lock):
    problems = []
    is_staged = bool(sweep.get("stage"))
    recorded_sha = sweep.get("lock_sha256")
    if recorded_sha is None and is_staged:
        problems.append("단계 스윕에 lock_sha256 이 없다(--lock 없이 실행)")
    elif recorded_sha is not None and recorded_sha != file_sha256(lock_path):
        problems.append(f"스윕 lock_sha256 {str(recorded_sha)[:12]}… ≠ 주어진 잠금 {file_sha256(lock_path)[:12]}…")
    created_at, started_at = lock.get("created_at"), sweep.get("started_at")
    if is_staged and (created_at is None or started_at is None):
        problems.append("단계 스윕 판정에는 잠금 created_at 과 스윕 started_at 이 모두 필요하다")
        return problems
    if created_at is None or started_at is None:
        return problems
    try:
        is_late = parse_moment(created_at) > parse_moment(started_at)
    except (TypeError, ValueError) as error:
        return problems + [f"시각 형식 오류: {error}"]
    if is_late:
        problems.append(f"잠금 created_at {created_at} > 스윕 started_at {started_at}(결과를 연 뒤 잠금)")
    return problems


def check_sweep_lock(sweep, lock_path):
    is_matching, problems, lock = verify_lock(lock_path)
    if not is_matching:
        raise LockError("; ".join(problems))
    if lock["files"].get(sweep["plan"]) != sweep["plan_sha256"]:
        raise LockError(f"잠금에 이 스윕의 계획 {sweep['plan']} (sha256 {sweep['plan_sha256'][:12]}…) 이 없다")
    if lock.get("version", 1) < LOCK_VERSION:
        if sweep.get("stage"):
            raise LockError(f"v1 잠금은 stage 없는 옛 스윕만 판정한다(이 스윕 stage {sweep['stage']})")
        return lock
    try:
        missing = [text for text in required_files(sweep["plan"]) if text not in lock["files"]]
        plan = load_json(project_path(sweep["plan"]))
    except (OSError, ValueError) as error:
        raise LockError(f"필수 집합을 계산할 수 없다: {error}")
    problems = ([f"필수 파일 누락: {', '.join(missing)}"] if missing else []) + find_scene_path_problems(sweep, plan) + find_record_problems(sweep, lock_path, lock)
    if problems:
        raise LockError("; ".join(problems))
    return lock


def verify_command(lock_text, sweep_text):
    lock_path = project_path(lock_text)
    if sweep_text is None:
        is_matching, problems, _ = verify_lock(lock_path)
    else:
        try:
            check_sweep_lock(load_json(project_path(sweep_text)), lock_path)
            is_matching, problems = True, []
        except (LockError, OSError, ValueError, KeyError, TypeError) as error:
            is_matching, problems = False, [str(error)]
    for line in problems:
        print("불일치:", line)
    print("잠금 일치" if is_matching else "잠금 불일치")
    return 0 if is_matching else EXIT_MISMATCH


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="보정 판정 사전 등록 sha256 잠금")
    commands = parser.add_subparsers(dest="command", required=True)
    lock_command = commands.add_parser("lock")
    lock_command.add_argument("--out", required=True)
    lock_command.add_argument("--plan")
    lock_command.add_argument("--file", action="append", default=[])
    lock_command.add_argument("--note", default="")
    verify_parser = commands.add_parser("verify")
    verify_parser.add_argument("--lock", required=True)
    verify_parser.add_argument("--sweep")
    arguments = parser.parse_args()
    if arguments.command == "verify":
        return verify_command(arguments.lock, arguments.sweep)
    try:
        file_texts = (required_files(arguments.plan) if arguments.plan else []) + arguments.file
        if not file_texts:
            raise LockError("--plan 또는 --file 이 필요하다")
        lock = create_lock(file_texts, project_path(arguments.out), arguments.note)
    except (LockError, OSError, ValueError) as error:
        print(f"잠금 입력 오류: {error}", file=sys.stderr)
        return EXIT_LOCK_INPUT
    print(json.dumps(lock, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
