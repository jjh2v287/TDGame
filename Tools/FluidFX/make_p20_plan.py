"""실행 환경: 시스템 Python 3(에디터 불필요, PowerShell 또는 Git Bash). 일괄 보정 P-20 탐욕 2라운드 계획을 1라운드 계획·screen 판정·채택 confirm 판정에서 결정론으로 만든다(plan-batch-v2 P-20: 손잡이 4개 이상 계획에서 screen 민감 손잡이가 2개 이상이면 채택 손잡이를 뺀 남은 민감 손잡이만 다시 screen → confirm, 1라운드 채택값은 덧씌움으로 v00 기준에 들어간다).
실행 명령: python Tools/FluidFX/make_p20_plan.py --plan Tools/FluidFX/calib/plans/<id>.json --screen <1라운드 screen.json> --confirm <1라운드 채택 confirm judge.json> [--out Tools/FluidFX/calib/plans/<id>-p20.json]
출력: 2라운드 계획 JSON(id <id>-p20, params = 1라운드 계획 params 중 screen keep 이면서 채택 안 된 것을 원래 순서·원래 객체 그대로, 장면·지표·base_tuning·base_tuning_ships·mode·repeats 등 나머지 키 그대로, note 앞에 P-20 출처). 표준 출력에 남은 손잡이 목록. 이미 있는 출력은 덮지 않는다. 종료 0 생성, 2 입력 거부.
상태: 2026-11-01 작성, 오프라인 확인만(1라운드 채택 6계획 생성 뒤 calib_sweep --dry-run 으로 계획 검사·덧씌움 겹침 없음 확인). 같은 날 반박(refute-p20)으로 LF 고정(.gitattributes eol=lf 라 체크아웃 뒤에도 계획 sha 가 같게)·손잡이 키는 calib_judge 의 param_keys·assignment_key 를 그대로 씀.
"""
import argparse
import copy
import hashlib
import json
import sys
from pathlib import Path

from calib_judge import assignment_key, canonical_text, param_keys

ROOT = Path(__file__).resolve().parents[2]
MIN_PARAM_COUNT = 4
MIN_SENSITIVE_COUNT = 2
ROUND_SUFFIX = "-p20"


class InputRejected(Exception):
    pass


def require(condition, message):
    if not condition:
        raise InputRejected(message)


def project_path(text):
    path = Path(text)
    return path if path.is_absolute() else ROOT / path


def relative_text(path):
    try:
        return Path(path).resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return Path(path).as_posix()


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise InputRejected(f"{relative_text(path)} 을 읽지 못했다: {error}") from error


def require_bound_judge(judge, label, plan_text, plan_sha256):
    require(judge.get("tool") == "calib_judge" and judge.get("version") == 2, f"{label}: calib_judge v2 판정이어야 한다")
    require(judge.get("plan") == plan_text, f"{label}: 계획 {judge.get('plan')!r} 이 {plan_text!r} 와 다르다")
    require(judge.get("plan_sha256") == plan_sha256, f"{label}: plan_sha256 이 지금 계획 파일과 다르다(계획이 바뀐 뒤의 판정)")


def find_remaining_params(plan, screen, confirm):
    plan_params = plan["params"]
    screen_params = screen.get("params") or []
    require(len(screen_params) == len(plan_params), f"screen params {len(screen_params)}개가 계획 params {len(plan_params)}개와 다르다")
    for index, (plan_param, screen_param) in enumerate(zip(plan_params, screen_params)):
        require(param_keys(plan_param) == param_keys(screen_param), f"params[{index}]: screen 과 계획의 손잡이 경로가 다르다")
    kept_keys = set(screen.get("keep") or [])
    kept = [index for index, param in enumerate(plan_params) if kept_keys & set(param_keys(param))]
    unmatched = kept_keys - {key for index in kept for key in param_keys(plan_params[index])}
    require(not unmatched, f"screen keep 이 계획 params 와 맞지 않는다: {', '.join(sorted(unmatched))}")
    adopt = confirm.get("adopt") or {}
    adopted_keys = {assignment_key(assignment) for assignment in adopt.get("assignments") or []}
    adopted = [index for index, param in enumerate(plan_params) if adopted_keys & set(param_keys(param))]
    require(adopted and all(index in kept for index in adopted), "confirm 채택 손잡이가 screen keep 안에 있어야 한다")
    return kept, adopted, [index for index in kept if index not in adopted]


def build_round_two_plan(plan, plan_text, remaining, adopted, confirm):
    adopted_text = ", ".join(f"{assignment_key(assignment)}={canonical_text(assignment.get('value'))}" for assignment in confirm["adopt"]["assignments"])
    round_two = copy.deepcopy(plan)
    round_two["id"] = plan["id"] + ROUND_SUFFIX
    round_two["params"] = [copy.deepcopy(plan["params"][index]) for index in remaining]
    origin = (f"P-20 2라운드({plan_text} 에서 생성): 1라운드 채택 {adopted_text}(덧씌움으로 v00 기준), "
              f"남은 screen 민감 손잡이 {len(remaining)}개(1라운드 params 색인 {remaining}, 채택 색인 {adopted}).")
    round_two["note"] = origin + (" " + plan["note"] if plan.get("note") else "")
    return round_two


def make_round_two(plan_path, screen_path, confirm_path, out_path):
    plan_text = relative_text(plan_path)
    plan = read_json(plan_path)
    require(isinstance(plan.get("id"), str) and not plan["id"].endswith(ROUND_SUFFIX), "1라운드 계획만 받는다(id 가 -p20 이면 거부)")
    require(isinstance(plan.get("params"), list) and len(plan["params"]) >= MIN_PARAM_COUNT, f"P-20: 손잡이 {MIN_PARAM_COUNT}개 이상 계획만(지금 {len(plan.get('params') or [])}개)")
    plan_sha256 = file_sha256(plan_path)
    screen = read_json(screen_path)
    require(screen.get("stage") == "screen", "--screen 은 screen 단계 판정이어야 한다")
    require_bound_judge(screen, "--screen", plan_text, plan_sha256)
    confirm = read_json(confirm_path)
    require(confirm.get("stage") == "confirm" and (confirm.get("adopt") or {}).get("decision") is True, "--confirm 은 채택(adopt.decision 참)한 confirm 판정이어야 한다")
    require_bound_judge(confirm, "--confirm", plan_text, plan_sha256)
    require(confirm.get("lock_sha256") == screen.get("lock_sha256"), "screen 과 confirm 판정의 잠금이 다르다(같은 1라운드 실행이 아니다)")
    kept, adopted, remaining = find_remaining_params(plan, screen, confirm)
    require(len(kept) >= MIN_SENSITIVE_COUNT, f"P-20: screen 민감 손잡이 {MIN_SENSITIVE_COUNT}개 이상만(지금 {len(kept)}개)")
    require(remaining, "채택 손잡이를 빼면 남은 민감 손잡이가 없다")
    require(not out_path.exists(), f"출력 {relative_text(out_path)} 이 이미 있다(덮지 않는다)")
    round_two = build_round_two_plan(plan, plan_text, remaining, adopted, confirm)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(round_two, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return round_two, kept, adopted, remaining


def main():
    parser = argparse.ArgumentParser(description="P-20 탐욕 2라운드 계획 생성")
    parser.add_argument("--plan", required=True)
    parser.add_argument("--screen", required=True)
    parser.add_argument("--confirm", required=True)
    parser.add_argument("--out")
    arguments = parser.parse_args()
    plan_path = project_path(arguments.plan)
    out_path = project_path(arguments.out) if arguments.out else plan_path.with_name(plan_path.stem + ROUND_SUFFIX + ".json")
    try:
        round_two, kept, adopted, remaining = make_round_two(plan_path, project_path(arguments.screen), project_path(arguments.confirm), out_path)
    except InputRejected as error:
        print("입력 거부:", error)
        return 2
    print(f"생성 {relative_text(out_path)}: keep {len(kept)}, 채택 색인 {adopted}, 남은 색인 {remaining}")
    for param in round_two["params"]:
        print("  ", "+".join(param_keys(param)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
