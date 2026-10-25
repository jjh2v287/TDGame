"""실행 환경: 시스템 Python 3(에디터 불필요, PowerShell 또는 Git Bash). 보정 판정 규칙을 결과를 열기 전에 잠그는 sha256 사전 등록 잠금을 만들고 검증한다(spec-wave0-data-bridge-v2 3.9 6단계, 수기 선례 Saved/AgentOps/20261024/fx4b-acc/prereg-lock-budget.txt).
실행 명령: python Tools/FluidFX/prereg_lock.py lock --out Saved/FluidFX/calib/<id>-prereg-lock.json (--plan Tools/FluidFX/calib/plans/<id>.json | --file <경로>...) [--note "<메모>"] | verify --lock <잠금 JSON>
출력: lock 은 잠금 JSON({created, note, files: {프로젝트 상대 경로: sha256}}, --plan 이면 계획·영역 틀·목표 선택·LIVE-2 목표·판정기·지표 도구를 함께 잠금, 이미 있으면 덮어쓰지 않음), verify 는 파일별 일치·불일치를 표준 출력에. 종료 코드 0 일치 / 1 lock 입력 오류 / 2 잠금 없음·형식 오류·불일치·파일 없음
상태: 오프라인 검사만(2026-10-25 잠금 생성·일치·한 바이트 변경 불일치·잠금 없음).
"""
import argparse
import hashlib
import json
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JUDGE_TOOL_FILES = ("Tools/FluidFX/calib_judge.py", "Tools/FluidFX/measure_fx_look.py", "Tools/FluidFX/prereg_lock.py")
LIVE2_TARGETS_KEY = "live2_targets"
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


def plan_lock_files(plan_path):
    plan = json.loads(Path(plan_path).read_text(encoding="utf-8"))
    files = [relative_text(plan_path)]
    for scene in plan.get("scenes", []):
        for key in ("regions_template", "targets"):
            if isinstance(scene.get(key), str):
                files.append(relative_text(project_path(scene[key])))
        if isinstance(scene.get("targets"), str):
            selection = json.loads(project_path(scene["targets"]).read_text(encoding="utf-8"))
            if isinstance(selection.get(LIVE2_TARGETS_KEY), str):
                files.append(relative_text(project_path(selection[LIVE2_TARGETS_KEY])))
    files.extend(JUDGE_TOOL_FILES)
    return list(dict.fromkeys(files))


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
    lock = {"tool": "prereg_lock", "created": datetime.now().isoformat(timespec="seconds"), "note": note, "files": files}
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
    verify_command = commands.add_parser("verify")
    verify_command.add_argument("--lock", required=True)
    arguments = parser.parse_args()
    if arguments.command == "verify":
        is_matching, problems, _ = verify_lock(project_path(arguments.lock))
        for line in problems:
            print("불일치:", line)
        print("잠금 일치" if is_matching else "잠금 불일치")
        return 0 if is_matching else EXIT_MISMATCH
    try:
        file_texts = (plan_lock_files(project_path(arguments.plan)) if arguments.plan else []) + arguments.file
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
