"""실행 환경: 시스템 Python 3, 백그라운드 실행(스윕 1회가 600초를 넘는다). 에디터가 TDGame 을 열 수 있고 Saved/AgentOps/build_hold.json 보류를 얻을 수 있어야 한다. 하위 도구(calib_sweep·calib_judge·prereg_lock·fluid_tuning_patch·ue_editor·run_in_editor·build_and_test, 헤드리스 UnrealEditor-Cmd)는 모두 하위 프로세스로 부르고 정본은 쓰지 않는다(spec-cal0-v2 1.5 G-BATCH, 일지 C10).
실행 명령: python Tools/FluidFX/calib_batch.py --queue Tools/FluidFX/calib/batch_queue.json --holder <보류 이름> [--journal Saved/FluidFX/calib/batch/journal.json] [--overlay Saved/FluidFX/calib/accepted/overlay.json] [--locks-dir Saved/FluidFX/calib/locks] [--block K1]... [--resume] [--revisit-deferred] (PowerShell). 큐 = {"test_classes": {"path", "sha256"}, "blocks": [{"id", "plans": [{"plan": 프로젝트 상대 경로, "chain": 사슬 이름|null, "stages": ["screen"] 또는 ["screen", "confirm"], "estimate_seconds"?: {단계: 초}}]}]}(계획 경로는 큐 전체에서 한 번씩), 시험 분류표 = {"numeric_pinned": [헤드리스 시험 경로], "timing_record_only": [벽시계 시간 기준 시험 경로]}(둘 다 블록 헤드리스에서 실패해도 기록만). 일지가 있으면 --resume 일 때만, 큐 sha256 이 같을 때만 이어 한다. --revisit-deferred 는 고른 블록의 deferred 항목 confirm 을 상한 없이 돌린다(K10) | 최종 모드: --queue <final_dir>/final_queue.json(mode final) --holder w-final --journal <큐 final.journal> [--resume](새 플래그 없음, --block·--revisit-deferred 는 입력 오류, --overlay·--locks-dir 는 읽지도 쓰지도 않음)
출력: 일지(C10 원자적 다시 쓰기. runs = {단계: 실행 폴더, lock: 잠금, verify<n>: 사슬 재검증, chain_verify: 사슬 요약(사슬 마지막 항목, 재검증 판정 불가로 중단되면 interrupted{plan, reason} 을 남겨 재개 때 반복하지 않음, 실행 실패면 남기지 않음), headless: 블록 헤드리스 폴더(블록 마지막 항목)}), 일지 폴더의 batch.log(시작 처리·명령·상태 한 줄씩), runs/<계획>/<단계>-<n>/ 와 <단계>-<n>.log, chains/<사슬>.json, blocks/<블록>-<n>/(materialize 사본·headless.log·headless.json(판정기 verdict·verdict_code·header_sync_pending)). 종료 코드 0 고른 블록 처리 끝·failed 없음 / 1 failed 항목 있음 / 2 큐·일지·계획 입력 오류 / 3 시작 처리(보류 회수→ensure→t.OverrideFPS 0→정본 재적재→보류 획득) 실패. 최종 모드: 일지 final_batch{edition, phase, restart, anchor_sha256, w_anchor_sha256, starts[{at, ok, reason}], outcome{event E2|E3|E3_repeat|F0_shortfall|start_failed, item, reason, at}|null, f0_gate{self_checks, sample, required, passed}|null}·항목 final{kind verify|ab, attempts[{step, stage, opportunity, try, folder, log, started_at, exit, state}], adoptions_first·adoptions·official_first·official_rerun·self_check·rerun 또는 ab_id·trigger_state·decision, 덧씌움·perf-candidates 앞뒤 sha, artifact_sha256}(명세 Saved/AgentOps/20261101/w-runbook/spec-final-verify.md 4.11), runs/<계획>/(verify|screen|confirm)-<n>/(sweep.json·verify.json·compare.json·screen.json·judge.json·check-ab.json). 최종 모드 종료 코드 0 모든 항목 끝 상태 / 1 E3·E3_repeat(자기 검증 불일치) / 2 입력 오류·E2 / 3 시작 처리 실패(E1, start_failed 포함) / 4 F0 결원(F0_shortfall)
상태: 가짜 하위 프로세스 시험만(calib_offline_test T98·T99, 2026-10-30), 실제 에디터 배치 미실행. 하위 도구의 새 인수(--stage·--overlay·--lock·--screen-result·--screen-sweep·--verify-against·accept·overlay-remove --overlay·materialize 의 TDFluidPresetTuning.json·TDFluidMaterialLook.json 출력)는 명세 0.4 계약 기준이며 병합 뒤 이름·경로 맞물림 확인(실제 실행 미검증). 단계 2배 상한은 단계 실행 하나(하위 프로세스) 전체의 시간 제한으로 집행한다. 판정 종료 1(스윕 무효·대조 실패·판정 불가)은 undetermined 로 채택 없음(no_adopt)·둔감(insensitive)과 구분한다, 재검증 판정은 종료 0 통과, 종료 1 은 verify.json 대조 통과(control_ok)·verify.passed 거짓일 때만 실패이고 대조 표류·판정 불가는 사슬 중단(되돌림 없음). confirm 직전 screen.json overlay_sha256 ≠ 현재 덧씌움이면 locked 로 되돌려 재screen 한다(deferred 재방문은 상한 없이). 2026-11-01 블록 헤드리스 설계 단언 실패가 TDGame.FluidFX.PresetData.StructDefaultsMatchHeader 하나뿐이고 그 시험 BeginEvents~EndEvents 의 Error 줄이 모두 "Expected '<Globals|StructDefaults>.<구조체>.<필드> equals the header default …' to be true." 꼴이며 그 경로가 모두 덧씌움 assignments json_path 이면 통과(헤더 기본값은 W 단계에서 같이 옮김, headless.json header_sync_pending·batch.log '헤더 기본값 동기 대기(W 단계)'), 다른 꼴 줄·덧씌움 밖 경로·다른 설계 단언 실패가 섞이면 되돌림(calib_offline_test K10 A1~A4, 실제 배치 미실행). 2026-11-01 2차: 사슬 재검증은 덧씌움 assignments·actor_props 에 같은 폴더 <앞 구성원 stem>-p20.json(P-20 2라운드) 채택이 있는 앞 구성원을 재검증에서 빼고(일지 reason 에 '사슬 <이름> 재검증 제외: P-20 2라운드 채택이 있는 1라운드 계획은 재검증하지 않는다: <경로>', batch.log '<계획> 사슬 재검증 | 제외 | …' 한 줄) 나머지 구성원만 재검증한다(calib_offline_test K10 C2, 실제 배치 미실행). 되돌림·채택 순서의 is_from_item 은 그대로 파일 stem·계획 id 규칙 2026-11-05 최종 모드(큐 mode final, 명세 spec-final-verify 4절·개정 G): calib_final_offline_test 의 T100~T115·T160~T188 calib_batch 부분을 가짜 하위 프로세스로만 시험했고 실제 배치는 돌리지 않았다.
"""
import argparse
import hashlib
import json
import math
import os
import re
import statistics
import subprocess
import sys
import time
from collections import Counter
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))

import build_and_test  # noqa: E402
import check_fluid_value_independence as value_independence  # noqa: E402
import fluid_tuning_patch  # noqa: E402
import prereg_lock  # noqa: E402

SWEEP_TOOL = ROOT / "Tools" / "FluidFX" / "calib_sweep.py"
JUDGE_TOOL = ROOT / "Tools" / "FluidFX" / "calib_judge.py"
LOCK_TOOL = ROOT / "Tools" / "FluidFX" / "prereg_lock.py"
PATCH_TOOL = ROOT / "Tools" / "FluidFX" / "fluid_tuning_patch.py"
EDITOR_TOOL = ROOT / "Tools" / "ue_editor.py"
REMOTE_TOOL = ROOT / "Tools" / "run_in_editor.py"
HOLD_TOOL = ROOT / "Tools" / "build_and_test.py"
HOLD_FILE = ROOT / "Saved" / "AgentOps" / "build_hold.json"
DEFAULT_JOURNAL = ROOT / "Saved" / "FluidFX" / "calib" / "batch" / "journal.json"
DEFAULT_OVERLAY = ROOT / "Saved" / "FluidFX" / "calib" / "accepted" / "overlay.json"
DEFAULT_LOCKS_DIR = ROOT / "Saved" / "FluidFX" / "calib" / "locks"
EMPTY_OVERLAY = {"version": 2, "assignments": [], "actor_props": [], "history": []}
FIXED_STEP_RESET_CODE = "import unreal; unreal.SystemLibrary.execute_console_command(None, 't.OverrideFPS 0')"
CANONICAL_RELOAD_CODE = ("import unreal; errors = unreal.get_engine_subsystem(unreal.TDFluidPresetTuningSubsystem).reload_preset_tuning(); "
                         "print('reload_errors', errors); assert errors == 0, errors")
HEADLESS_FILTER = "TDGame.FluidFX"
HEADER_DEFAULTS_TEST = "TDGame.FluidFX.PresetData.StructDefaultsMatchHeader"
HEADER_DEFAULT_ERROR = re.compile(r"Error: Expected '((?:Globals|StructDefaults)\.\w+\.\w+) equals the header default .* "
                                  r"\(a header change does not move presets; update the tuning file on purpose\)' to be true\.(?: \[[^\]]*\])?")
MATERIALIZED_TUNING = "TDFluidPresetTuning.json"
MATERIALIZED_LOOK = "TDFluidMaterialLook.json"
QUEUE_STAGE_SETS = (["screen"], ["screen", "confirm"])
DEFAULT_REAL_DT = 0.0255
SHOT_SECONDS = 0.35
STEP_OVERHEAD_SECONDS = 10.0
SCENE_OVERHEAD_SECONDS = 130.0
STEP_LIMIT_FACTOR = 2.0
REPLACE_ATTEMPTS = 4
REPLACE_RETRY_SECONDS = 2.0
VERIFY_PASSED, VERIFY_FAILED, VERIFY_UNDETERMINED, VERIFY_NOT_RUN = "passed", "failed", "undetermined", "not_run"
BLOCK_LIMIT_FACTOR = 1.25
P20_SUFFIX = "-p20"
TOOL_TIMEOUT_SECONDS = 1800
REASON_TAIL_CHARACTERS = 600


class InputError(Exception):
    pass


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json_atomic(path, document):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(document, ensure_ascii=False, indent=1), encoding="utf-8")
    for attempt in range(1, REPLACE_ATTEMPTS + 1):
        try:
            os.replace(temporary, path)
            return
        except PermissionError:
            if attempt == REPLACE_ATTEMPTS:
                raise
            time.sleep(REPLACE_RETRY_SECONDS)


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def now_text():
    return datetime.now().isoformat(timespec="seconds")


def project_path(text):
    path = Path(text)
    return path if path.is_absolute() else ROOT / path


def display_path(path):
    path = Path(path)
    return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.as_posix()


def p20_descendant_text(plan_text):
    path = Path(plan_text)
    return display_path(project_path(path.with_name(f"{path.stem}{P20_SUFFIX}{path.suffix}")))


def source_plan_text(entry):
    source = str((entry.get("from") or {}).get("plan", ""))
    return display_path(project_path(source)) if source else ""


def python_command(script, *arguments):
    return [sys.executable, str(script), *[str(argument) for argument in arguments]]


def run_tool(command, log_path, timeout):
    log_path = Path(log_path)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    returncode = None
    with open(log_path, "w", encoding="utf-8", errors="replace") as log_file:
        try:
            returncode = subprocess.run(command, stdout=log_file, stderr=subprocess.STDOUT, cwd=ROOT, timeout=timeout,
                                        env=dict(os.environ, PYTHONIOENCODING="utf-8")).returncode
        except subprocess.TimeoutExpired:
            pass
    return returncode, log_path.read_text(encoding="utf-8", errors="replace")[-REASON_TAIL_CHARACTERS:]


def queue_entries(queue):
    return [(block, entry) for block in queue.get("blocks", []) for entry in block.get("plans", [])]


def load_queue(queue_path):
    if not queue_path.exists():
        raise InputError(f"큐 없음: {queue_path}")
    queue = read_json(queue_path)
    test_classes = queue.get("test_classes") or {}
    classes_path = project_path(test_classes.get("path", ""))
    if not test_classes.get("path") or not classes_path.is_file():
        raise InputError(f"test_classes.path 파일 없음: {test_classes.get('path')}")
    if file_sha256(classes_path) != test_classes.get("sha256"):
        raise InputError(f"시험 분류표 sha256 이 큐에 동결한 값과 다르다: {classes_path}")
    plans = [entry.get("plan") for _, entry in queue_entries(queue)]
    if not plans:
        raise InputError("큐에 계획이 없다")
    duplicates = sorted(plan for plan, count in Counter(plans).items() if count > 1)
    if duplicates:
        raise InputError(f"큐에 같은 계획이 두 번: {duplicates}")
    for block, entry in queue_entries(queue):
        if not block.get("id"):
            raise InputError("블록 id 없음")
        if not entry.get("plan") or not project_path(entry["plan"]).is_file():
            raise InputError(f"{block['id']}: 계획 파일 없음 {entry.get('plan')}")
        if entry.get("stages") not in QUEUE_STAGE_SETS:
            raise InputError(f"{entry['plan']}: stages 는 {QUEUE_STAGE_SETS} 중 하나")
    return queue


def open_journal(journal_path, queue, queue_sha256, is_resume):
    if not is_resume:
        if journal_path.exists():
            raise InputError(f"일지가 이미 있다(--resume 으로 이어 하거나 옮긴다): {journal_path}")
        items = [{"plan": entry["plan"], "block": block["id"], "chain": entry.get("chain"), "status": "pending", "runs": {},
                  "attempts": 0, "wall_seconds": 0.0, "estimate_seconds": 0.0, "reason": ""} for block, entry in queue_entries(queue)]
        return {"queue_sha256": queue_sha256, "items": items, "real_dt": {}, "holds": []}
    if not journal_path.exists():
        raise InputError(f"이어 할 일지 없음: {journal_path}")
    journal = read_json(journal_path)
    if journal.get("queue_sha256") != queue_sha256:
        raise InputError("큐 sha256 이 일지에 동결한 값과 다르다(큐를 바꾸면 새 일지)")
    return journal


def automation_error_lines(log_text, test_path):
    lines = []
    is_inside = False
    for line in log_text.splitlines():
        begin = build_and_test.EVENTS_BEGIN.search(line)
        if begin:
            is_inside = begin.group(1) == test_path
            continue
        if build_and_test.EVENTS_END.search(line):
            is_inside = False
            continue
        if is_inside and "Error:" in line:
            lines.append(line.split("LogAutomationController:", 1)[-1].strip())
    return lines


def header_sync_pending(verdict, verdict_code, log_text, overlay_paths):
    if verdict_code != 1 or verdict["outside_allowlist"] != [HEADER_DEFAULTS_TEST]:
        return []
    matches = [HEADER_DEFAULT_ERROR.fullmatch(line) for line in automation_error_lines(log_text, HEADER_DEFAULTS_TEST)]
    if not matches or not all(matches):
        return []
    paths = sorted({match.group(1) for match in matches})
    return paths if set(paths) <= set(overlay_paths) else []


def level_name(scene):
    return str(scene.get("level", "")).rsplit("/", 1)[-1].split(".")[0]


def knob_end_count(param):
    values = param.get("values") or []
    if values and all(isinstance(value, (int, float)) and not isinstance(value, bool) for value in values):
        return min(2, len(values))
    return max(1, len(values) - 1)


def stage_step_count(plan, scene, stage):
    fixed_counts = {"frame": 1, "verify": 3, "confirm": 5}
    if stage in fixed_counts:
        return fixed_counts[stage]
    ends = sum(knob_end_count(param) for param in plan.get("params", []) if scene.get("name") in param.get("scenes", [scene.get("name")]))
    if not ends:
        return 0
    return ends + max(3, math.ceil(ends / 2) + 1)


def param_scene_names(plan):
    all_names = [scene.get("name") for scene in plan.get("scenes", [])]
    touched = set()
    for param in plan.get("params", []):
        touched.update(param.get("scenes") or all_names)
    return touched


def estimate_stage_seconds(plan, stage, real_dt_by_level, confirm_scenes=None):
    run_scenes = None
    if stage == "confirm":
        run_scenes = set(confirm_scenes) if confirm_scenes is not None else param_scene_names(plan)
    total = 0.0
    for scene in plan.get("scenes", []):
        if run_scenes is not None and scene.get("name") not in run_scenes:
            continue
        steps = stage_step_count(plan, scene, stage)
        if not steps:
            continue
        real_dt = real_dt_by_level.get(level_name(scene), DEFAULT_REAL_DT)
        shots = scene.get("shots") or {}
        count = int(shots.get("count", 1))
        game_seconds = float(scene.get("settle", 0)) + (count - 1) * float(shots.get("interval", 0))
        total += SCENE_OVERHEAD_SECONDS + steps * (game_seconds * 60 * real_dt + count * SHOT_SECONDS + STEP_OVERHEAD_SECONDS)
    return total


class BatchRunner:
    def __init__(self, arguments, queue, journal):
        self.journal_path = project_path(arguments.journal)
        self.work_dir = self.journal_path.parent
        self.log_path = self.work_dir / "batch.log"
        self.overlay_path = project_path(arguments.overlay)
        self.locks_dir = project_path(arguments.locks_dir)
        self.holder = arguments.holder
        self.queue = queue
        self.journal = journal
        self.entries = {entry["plan"]: entry for _, entry in queue_entries(queue)}
        self.plans = {plan: read_json(project_path(plan)) for plan in self.entries}
        classes = read_json(project_path(queue["test_classes"]["path"]))
        self.numeric_pinned = set(classes.get("numeric_pinned", [])) | set(classes.get("timing_record_only", []))

    def save(self):
        write_json_atomic(self.journal_path, self.journal)

    def record(self, label, outcome, detail=""):
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.log_path, "a", encoding="utf-8") as log_file:
            log_file.write(f"{now_text()} {label} | {outcome} | {detail}".rstrip() + "\n")

    def tool(self, label, command, log_path, timeout=TOOL_TIMEOUT_SECONDS):
        code, tail = run_tool(command, log_path, timeout)
        self.record(label, "시간 초과" if code is None else f"종료 {code}", display_path(log_path))
        return code, tail

    def item_tool(self, item, label, command, log_path, timeout=TOOL_TIMEOUT_SECONDS):
        started = time.monotonic()
        code, tail = self.tool(f"{self.plan_name(item)} {label}", command, log_path, timeout)
        item["wall_seconds"] = round(item["wall_seconds"] + time.monotonic() - started, 3)
        return code, tail

    def set_status(self, item, status, reason):
        item["status"] = status
        item["reason"] = reason
        self.record(f"{self.plan_name(item)} 상태", status, reason[:200])

    def plan_name(self, item):
        return Path(item["plan"]).stem

    def run_dir_and_log(self, item, run_key):
        base = self.work_dir / "runs" / self.plan_name(item)
        number = 1
        while (base / f"{run_key}-{number}.log").exists() or (base / f"{run_key}-{number}").exists():
            number += 1
        return base / f"{run_key}-{number}", base / f"{run_key}-{number}.log"

    def stage_estimate(self, item, stage):
        override = (self.entries[item["plan"]].get("estimate_seconds") or {}).get(stage)
        if override is not None:
            return float(override)
        return estimate_stage_seconds(self.plans[item["plan"]], stage, self.journal["real_dt"], self.screened_best_scenes(item) if stage == "confirm" else None)

    def screened_best_scenes(self, item):
        if "screen" not in item["runs"]:
            return None
        screen_dir = project_path(item["runs"]["screen"])
        try:
            best = (read_json(screen_dir / "screen.json").get("best") or {}).get("variant")
            variants = {variant.get("id"): variant for variant in read_json(screen_dir / "sweep.json").get("variants", [])}
            scenes = variants[best].get("scenes") if best in variants else None
        except (OSError, ValueError, AttributeError, TypeError):
            return None
        if best not in variants:
            return None
        return [scene.get("name") for scene in self.plans[item["plan"]].get("scenes", [])] if scenes is None else scenes

    def item_estimate(self, item):
        return sum(self.stage_estimate(item, stage) for stage in self.entries[item["plan"]]["stages"])

    def lock_path(self, item):
        return project_path(item["runs"]["lock"])

    def missing_regions(self, item):
        return [scene["regions_template"] for scene in self.plans[item["plan"]].get("scenes", [])
                if scene.get("regions_template") and not project_path(scene["regions_template"]).exists()]

    def is_actionable(self, item, is_revisit):
        status = item["status"]
        if status in ("pending", "locked", "confirmed"):
            return True
        if status == "screened":
            return "confirm" in self.entries[item["plan"]]["stages"]
        if status == "needs_regions":
            return not self.missing_regions(item)
        return status == "deferred" and is_revisit

    def refresh_hold(self):
        try:
            if read_json(HOLD_FILE).get("owner") == self.holder:
                os.utime(HOLD_FILE)
        except (OSError, ValueError):
            return

    def start(self):
        start_dir = self.work_dir / "start"
        owners = list(dict.fromkeys(hold["owner"] for hold in self.journal["holds"] if not hold.get("released_at")))
        if not owners:
            self.record("보류 회수", "없음")
        for owner in owners:
            code, _ = self.tool(f"보류 회수 {owner}", python_command(HOLD_TOOL, "--release-hold", owner), start_dir / "release-hold.log")
            for hold in self.journal["holds"]:
                if hold["owner"] == owner and not hold.get("released_at"):
                    hold.update(released_at=now_text(), released_by=f"시작 회수(종료 {code})")
        self.save()
        steps = (("ensure", python_command(EDITOR_TOOL, "ensure"), "ensure.log"),
                 ("t.OverrideFPS 0", python_command(REMOTE_TOOL, "-c", FIXED_STEP_RESET_CODE), "fixed-step.log"),
                 ("정본 재적재", python_command(REMOTE_TOOL, "-c", CANONICAL_RELOAD_CODE), "canonical-reload.log"))
        for label, command, log_name in steps:
            code, tail = self.tool(label, command, start_dir / log_name)
            if code != 0:
                return f"{label} 종료 {code}: {tail}"
        code, tail = self.tool("보류 획득", python_command(HOLD_TOOL, "--acquire-hold", self.holder, "--reason", "calib_batch"), start_dir / "acquire-hold.log")
        if code != 0:
            return f"보류 획득 종료 {code}: {tail}"
        self.journal["holds"].append({"owner": self.holder, "acquired_at": now_text()})
        self.save()
        return None

    def finish(self):
        held = [hold for hold in self.journal["holds"] if hold["owner"] == self.holder and not hold.get("released_at")]
        if not held:
            return
        code, _ = self.tool("보류 해제", python_command(HOLD_TOOL, "--release-hold", self.holder), self.work_dir / "start" / "release-hold-end.log")
        for hold in held:
            hold.update(released_at=now_text(), released_by=f"끝(종료 {code})")
        self.save()

    def run(self, block_ids, is_revisit):
        if not self.overlay_path.exists():
            write_json_atomic(self.overlay_path, EMPTY_OVERLAY)
        for block in self.queue["blocks"]:
            if block_ids and block["id"] not in block_ids:
                continue
            for item in [item for item in self.journal["items"] if item["block"] == block["id"]]:
                self.advance(item, is_revisit)
                if item["chain"]:
                    self.verify_chain(item["chain"], is_revisit)
            self.check_block_headless(block["id"])

    def advance(self, item, is_revisit):
        item["estimate_seconds"] = round(self.item_estimate(item), 1)
        self.save()
        is_capped = item["status"] != "deferred"
        while self.is_actionable(item, is_revisit):
            status = item["status"]
            if status in ("pending", "needs_regions"):
                self.prepare(item)
            elif status == "locked":
                self.screen(item)
            elif status in ("screened", "deferred"):
                self.confirm(item, is_capped and status == "screened")
            else:
                self.accept(item)
            self.save()
            if status == "screened" and item["status"] == "deferred":
                return

    def run_stage(self, item, stage, extra_arguments, run_key=None):
        run_key = run_key or stage
        estimate = max(self.stage_estimate(item, stage), SCENE_OVERHEAD_SECONDS)
        limit = STEP_LIMIT_FACTOR * estimate
        failure = ""
        for attempt in (1, 2):
            run_dir, log_path = self.run_dir_and_log(item, run_key)
            item["runs"][run_key] = display_path(run_dir)
            item["attempts"] += 1
            self.refresh_hold()
            self.save()
            command = python_command(SWEEP_TOOL, "--plan", item["plan"], "--stage", stage, "--overlay", self.overlay_path, "--run-dir", run_dir, *extra_arguments)
            code, tail = self.item_tool(item, f"sweep {stage} {attempt}회", command, log_path, limit)
            if code == 0:
                self.update_real_dt(item, run_dir)
                return run_dir, ""
            if code is None:
                return None, f"{stage} 실행이 한도 {limit:.0f}초(추정 {estimate:.0f}초의 2배)를 넘어 중단(L-editor-06 앞창 확인): {tail}"
            failure = f"{stage} 종료 {code}({attempt}회): {tail}"
            if code == 2:
                return None, failure
            if attempt == 1:
                self.item_tool(item, "ensure", python_command(EDITOR_TOOL, "ensure"), log_path.with_suffix(".ensure.log"))
        return None, failure

    def update_real_dt(self, item, run_dir):
        for scene in self.plans[item["plan"]].get("scenes", []):
            level = level_name(scene)
            if level in self.journal["real_dt"]:
                continue
            values = [read_json(path).get("real_dt_mean") for path in sorted((Path(run_dir) / str(scene.get("name"))).rglob("*_schedule.json"))]
            values = [value for value in values if isinstance(value, (int, float)) and value > 0]
            if values:
                self.journal["real_dt"][level] = statistics.median(values)

    def prepare(self, item):
        missing = self.missing_regions(item)
        if not missing:
            self.lock(item)
            return
        if "frame" not in item["runs"]:
            run_dir, failure = self.run_stage(item, "frame", [])
            if run_dir is None:
                self.set_status(item, "failed", failure)
                return
        self.set_status(item, "needs_regions", "영역 틀 없음(frame 대조 이미지로 그린 뒤 --resume): " + ", ".join(missing))

    def lock(self, item):
        lock_path = self.locks_dir / f"{self.plan_name(item)}-{datetime.now():%Y%m%d}-prereg-lock.json"
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        log_path = self.work_dir / "runs" / self.plan_name(item) / "lock.log"
        code, tail = self.item_tool(item, "lock", python_command(LOCK_TOOL, "lock", "--out", lock_path, "--plan", item["plan"]), log_path)
        if code != 0:
            self.set_status(item, "failed", f"잠금 종료 {code}: {tail}")
            return
        item["runs"]["lock"] = display_path(lock_path)
        self.set_status(item, "locked", "")

    def judge(self, item, label, run_dir, output_name, extra_arguments):
        output_path = Path(run_dir) / output_name
        command = python_command(JUDGE_TOOL, "--sweep", Path(run_dir) / "sweep.json", "--lock", self.lock_path(item), "--output", output_path, *extra_arguments)
        code, tail = self.item_tool(item, f"judge {label}", command, Path(str(run_dir) + f".{label}.log"))
        return code, tail, output_path

    def screen(self, item):
        run_dir, failure = self.run_stage(item, "screen", ["--lock", self.lock_path(item)])
        if run_dir is None:
            self.set_status(item, "failed", failure)
            return
        code, tail, result_path = self.judge(item, "screen", run_dir, "screen.json", [])
        if code != 0 or not result_path.exists():
            self.set_status(item, "undetermined" if code == 1 else "failed", f"screen 판정 종료 {code}: {tail}")
            return
        result = read_json(result_path)
        if result.get("insensitive"):
            self.set_status(item, "insensitive", "screen 둔감(민감 손잡이 0)")
            return
        if not result.get("best"):
            self.set_status(item, "no_adopt", "screen best 없음")
            return
        self.set_status(item, "screened", f"screen best {result['best'].get('variant')}")

    def block_usage(self, block_id):
        members = [item for item in self.journal["items"] if item["block"] == block_id]
        return sum(item["wall_seconds"] for item in members), sum(self.item_estimate(item) for item in members)

    def confirm(self, item, is_capped):
        spent, estimate = self.block_usage(item["block"])
        if is_capped and spent > BLOCK_LIMIT_FACTOR * estimate:
            self.set_status(item, "deferred", f"블록 누적 {spent:.0f}초 > {BLOCK_LIMIT_FACTOR} × 블록 추정 {estimate:.0f}초: confirm 을 K10 으로")
            return
        screen_dir = project_path(item["runs"]["screen"])
        if read_json(screen_dir / "screen.json").get("overlay_sha256") != file_sha256(self.overlay_path):
            self.set_status(item, "locked", "덧씌움 바뀜, 재screen")
            return
        run_dir, failure = self.run_stage(item, "confirm", ["--lock", self.lock_path(item), "--screen-result", screen_dir / "screen.json"])
        if run_dir is None:
            self.set_status(item, "failed", failure)
            return
        code, tail, judge_path = self.judge(item, "confirm", run_dir, "judge.json", ["--screen-sweep", screen_dir / "sweep.json"])
        if code != 0 or not judge_path.exists():
            self.set_status(item, "undetermined" if code == 1 else "failed", f"confirm 판정 종료 {code}: {tail}")
            return
        adopt = read_json(judge_path).get("adopt") or {}
        if adopt.get("decision") is not True:
            self.set_status(item, "no_adopt", "adopt 거짓: " + "; ".join(str(reason) for reason in adopt.get("reasons", [])))
            return
        self.set_status(item, "confirmed", f"adopt {adopt.get('variant')}")

    def accept(self, item):
        judge_path = project_path(item["runs"]["confirm"]) / "judge.json"
        log_path = self.work_dir / "runs" / self.plan_name(item) / "accept.log"
        code, tail = self.item_tool(item, "accept", python_command(PATCH_TOOL, "accept", "--judge", judge_path, "--overlay", self.overlay_path), log_path)
        if code != 0:
            self.set_status(item, "failed", f"accept 종료 {code}: {tail}")
            return
        self.set_status(item, "adopted", item["reason"])
        self.clear_markers(item)

    def last_member(self, key, value):
        return [item for item in self.journal["items"] if item[key] == value][-1]

    def clear_markers(self, item):
        if item["chain"]:
            self.last_member("chain", item["chain"])["runs"].pop("chain_verify", None)
        self.last_member("block", item["block"])["runs"].pop("headless", None)

    def is_from_item(self, entry, item):
        source = str((entry.get("from") or {}).get("plan", ""))
        return bool(source) and Path(source).stem in (self.plan_name(item), str(self.plans[item["plan"]].get("id")))

    def adopted_p20_descendant(self, item):
        descendant = p20_descendant_text(item["plan"])
        overlay = read_json(self.overlay_path)
        entries = overlay.get("assignments", []) + overlay.get("actor_props", [])
        return descendant if any(source_plan_text(entry) == descendant for entry in entries) else None

    def skip_p20_parent(self, item, chain, descendant):
        note = f"사슬 {chain} 재검증 제외: P-20 2라운드 채택이 있는 1라운드 계획은 재검증하지 않는다: {descendant}"
        if note not in item["reason"]:
            item["reason"] = f"{item['reason']} / {note}" if item["reason"] else note
        self.record(f"{self.plan_name(item)} 사슬 재검증", "제외", note)
        self.save()

    def adopted_in_order(self, members):
        overlay = read_json(self.overlay_path)
        history = overlay.get("history", [])

        def adoption_rank(item):
            ranks = [index for index, entry in enumerate(history) if entry.get("op") == "add" and self.is_from_item(entry, item)]
            return max(ranks, default=-1), self.journal["items"].index(item)

        return sorted((item for item in members if item["status"] == "adopted"), key=adoption_rank)

    def revert(self, item, reason):
        overlay = read_json(self.overlay_path)
        json_paths = [entry["json_path"] for entry in overlay.get("assignments", []) if self.is_from_item(entry, item) and entry.get("json_path")]
        if not json_paths:
            self.record(f"{self.plan_name(item)} 되돌리기", "불가", "덧씌움에 이 계획의 json_path 없음(actor_props 는 손으로)")
            return False
        log_path = self.work_dir / "runs" / self.plan_name(item) / "overlay-remove.log"
        for json_path in json_paths:
            command = python_command(PATCH_TOOL, "overlay-remove", "--json-path", json_path, "--reason", reason, "--overlay", self.overlay_path)
            code, tail = self.item_tool(item, f"overlay-remove {json_path}", command, log_path)
            if code != 0:
                self.record(f"{self.plan_name(item)} 되돌리기", f"종료 {code}", tail[-200:])
                return False
        leftover = [entry for entry in overlay.get("actor_props", []) if self.is_from_item(entry, item)]
        self.set_status(item, "verify_failed", reason + (" / actor_props 남음: 손으로 빼기" if leftover else ""))
        self.clear_markers(item)
        self.save()
        return True

    def verify_item(self, item, round_number):
        run_key = f"verify{round_number}"
        run_dir, failure = self.run_stage(item, "verify", ["--lock", self.lock_path(item)], run_key)
        if run_dir is None:
            item["reason"] = f"사슬 재검증 실행 실패: {failure}"
            return VERIFY_NOT_RUN
        confirm_judge = project_path(item["runs"]["confirm"]) / "judge.json"
        code, tail, verify_path = self.judge(item, run_key, run_dir, "verify.json", ["--verify-against", confirm_judge])
        if code == 0:
            return VERIFY_PASSED
        report = read_json(verify_path) if code == 1 and verify_path.exists() else {}
        is_control_ok = all(scene.get("control_ok") for scene in report.get("scenes") or [{}])
        if code == 1 and is_control_ok and (report.get("verify") or {}).get("passed") is False:
            return VERIFY_FAILED
        item["reason"] = f"사슬 재검증 판정 종료 {code}{'(대조 표류·판정 불가)' if code == 1 else ''}: {tail}"
        return VERIFY_UNDETERMINED

    def write_chain_summary(self, last, chain, rounds, interrupted=None):
        summary_path = self.work_dir / "chains" / (chain.replace("/", "_").replace(".", "_") + ".json")
        write_json_atomic(summary_path, dict({"chain": chain, "rounds": rounds, "at": now_text()}, **({"interrupted": interrupted} if interrupted else {})))
        last["runs"]["chain_verify"] = display_path(summary_path)
        self.save()

    def verify_chain(self, chain, is_revisit):
        members = [item for item in self.journal["items"] if item["chain"] == chain]
        last = members[-1]
        if "chain_verify" in last["runs"] or any(self.is_actionable(item, is_revisit) for item in members):
            return
        rounds = []
        while True:
            adopted = self.adopted_in_order(members)
            if len(adopted) < 2:
                break
            outcomes = {}
            for earlier in adopted[:-1]:
                descendant = self.adopted_p20_descendant(earlier)
                if descendant:
                    self.skip_p20_parent(earlier, chain, descendant)
                    continue
                status = self.verify_item(earlier, len(rounds) + 1)
                self.save()
                if status in (VERIFY_NOT_RUN, VERIFY_UNDETERMINED):
                    self.record(f"사슬 {chain} 재검증", "중단", earlier["reason"][:200])
                    if status == VERIFY_UNDETERMINED:
                        self.write_chain_summary(last, chain, rounds, {"plan": self.plan_name(earlier), "reason": earlier["reason"][:600]})
                    return
                outcomes[self.plan_name(earlier)] = status == VERIFY_PASSED
            rounds.append(outcomes)
            failing = [name for name, passed in outcomes.items() if not passed]
            if not failing:
                break
            if not self.revert(adopted[-1], f"conflict: 사슬 {chain} 재검증 실패({', '.join(failing)}) → 가장 최근 채택 되돌림"):
                return
        self.write_chain_summary(last, chain, rounds)

    def run_headless_tests(self, output_dir):
        tuning_path = output_dir / "copy" / MATERIALIZED_TUNING
        log_path = output_dir / "headless.log"
        command = [build_and_test.EDITOR_CMD_EXE, build_and_test.UPROJECT, "-ExecCmds=Automation RunTests " + HEADLESS_FILTER,
                   "-TestExit=Automation Test Queue Empty", "-unattended", build_and_test.RHI_ARGUMENTS["null"], "-NoSplash", "-NoSound",
                   f"-abslog={log_path}", f"-TDFluidTuningFile={tuning_path}", f"-TDFluidLookFile={output_dir / 'copy' / MATERIALIZED_LOOK}"]
        code, _ = self.tool("헤드리스 시험", command, output_dir / "headless-process.log")
        summary = build_and_test.parse_test_log(str(log_path))
        summary["engine_exit_code"] = -1 if code is None else code
        return value_independence.judge(summary, value_independence.read_log_text(str(log_path)), str(tuning_path), self.numeric_pinned, True)

    def check_block_headless(self, block_id):
        last = self.last_member("block", block_id)
        while "headless" not in last["runs"]:
            adopted = self.adopted_in_order([item for item in self.journal["items"] if item["block"] == block_id])
            if not adopted:
                return
            number = 1
            while (self.work_dir / "blocks" / f"{block_id}-{number}").exists():
                number += 1
            output_dir = self.work_dir / "blocks" / f"{block_id}-{number}"
            code, tail = self.tool(f"블록 {block_id} materialize", python_command(PATCH_TOOL, "materialize", "--overlay", self.overlay_path, "--out", output_dir / "copy"),
                                   output_dir / "materialize.log")
            if code != 0:
                self.record(f"블록 {block_id} 헤드리스", "중단", f"materialize 종료 {code}: {tail[-200:]}")
                return
            verdict, verdict_code = self.run_headless_tests(output_dir)
            overlay_paths = [entry.get("json_path") for entry in read_json(self.overlay_path).get("assignments", [])]
            pending = header_sync_pending(verdict, verdict_code, value_independence.read_log_text(str(output_dir / "headless.log")), overlay_paths)
            write_json_atomic(output_dir / "headless.json", dict(verdict, verdict_code=verdict_code, header_sync_pending=pending, overlay_sha256=file_sha256(self.overlay_path)))
            if verdict_code not in (0, 1):
                self.record(f"블록 {block_id} 헤드리스", f"불완전 {verdict_code}", "다음 --resume 에서 다시")
                return
            if verdict_code == 0 or pending:
                last["runs"]["headless"] = display_path(output_dir)
                pending_text = f" / 헤더 기본값 동기 대기(W 단계): {', '.join(pending)}" if pending else ""
                self.record(f"블록 {block_id} 헤드리스", "통과", "기록만(숫자 고정·시간 측정) 실패: " + ", ".join(verdict["allowlisted_failures"]) + pending_text)
                self.save()
                return
            if not self.revert(adopted[-1], f"블록 {block_id} 헤드리스 설계 단언 실패({', '.join(verdict['outside_allowlist'][:5])}) → 블록의 가장 최근 채택 되돌림"):
                return


FINAL_STAGE_SETS = (["verify"], ["screen", "confirm"])
FINAL_EDITION_PHASES = {"e1": "r1", "r2": "r2"}
FINAL_BLOCK_ORDER = ("F0", "F1", "F2")
FINAL_QUEUE_KEYS = ("edition", "phase", "restart", "expect", "journal", "anchor", "w_anchor", "judge_files")
FINAL_AB_FIELDS = ("id", "for_run", "for_adoption", "trigger_adoption", "trigger", "cross")
FINAL_TOOL_PATH = "Tools/FluidFX/calib_final_verify.py"
FINAL_BATCH_PATH = "Tools/FluidFX/calib_batch.py"
FINAL_TEST_CLASSES_PATH = "Tools/FluidFX/calib/test_classes.json"
COMPARE_TOOL = ROOT / "Tools" / "FluidFX" / "calib_final_verify.py"
PERF_CANDIDATES_PATH = ROOT / "Saved" / "FluidFX" / "calib" / "perf-candidates.json"
SELF_PATH = Path(__file__).resolve()
F0_REQUIRED_SELF_CHECKS = 3
VERIFY_OPPORTUNITIES = ("first", "rerun")
AB_TRIGGERS = ("always_record", "on_fail")
SCENE_PATH_KEYS = ("measure", "sources")
WORK_PLACEHOLDER = "<work>"
STATE_SEVERITY = {"pass": 0, "fail": 1, "undetermined": 2, "incomplete": 3}
FINAL_EVENT_EXIT_CODES = {"E2": 2, "E3": 1, "E3_repeat": 1, "F0_shortfall": 4, "start_failed": 3}
FINAL_TERMINAL_STATUSES = ("verified", "incomplete", "ab_done", "ab_skipped")
ENVIRONMENT_EXIT_CODES = (None, 1)
FOG_GENERATOR = "Tools/FluidFX/editor_make_fluid_fog_stage.py"
FOG_STAGE_REPORT = "Saved/FluidFX/fluid_fog_stage.json"
FOG_LIGHT_LABEL = "LabGreenLight"
FOG_LIGHT_BEFORE = 4.0
FOG_LIGHT_ADOPTED = 8.0
FOG_LIGHT_VALUE_PATTERN = re.compile(r"\"volumetric_scattering_intensity\":\s*([-+0-9.eE]+)")
FINAL_ERRORS = (OSError, ValueError, KeyError, TypeError, prereg_lock.LockError, fluid_tuning_patch.PatchError)


class FinalStop(Exception):
    def __init__(self, event, reason, item_id=None):
        super().__init__(reason)
        self.event = event
        self.reason = reason
        self.item_id = item_id


def final_now_text():
    return datetime.now().astimezone().isoformat(timespec="seconds")


def relative_text(text):
    return prereg_lock.relative_text(prereg_lock.project_path(str(text)))


def parent_text(text):
    return relative_text(text).rsplit("/", 1)[0]


def is_file_text(text):
    return bool(text) and prereg_lock.project_path(str(text)).is_file()


def read_json_text(text):
    return read_json(prereg_lock.project_path(str(text)))


def sha_of_text(text):
    return prereg_lock.file_sha256(prereg_lock.project_path(str(text)))


def sha_or_none(text):
    return sha_of_text(text) if is_file_text(text) else None


def file_moment(text):
    return datetime.fromtimestamp(prereg_lock.project_path(str(text)).stat().st_mtime).astimezone()


def canonical_json_text(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))


def is_final_queue(queue_text):
    try:
        queue = read_json(project_path(queue_text))
    except (OSError, ValueError):
        return False
    return isinstance(queue, dict) and queue.get("mode") == "final"


def anchor_commit_problems(path_text):
    relative = relative_text(path_text)
    listed = subprocess.run(["git", "ls-files", "--error-unmatch", "--", relative], cwd=ROOT, capture_output=True, text=True)
    if listed.returncode != 0:
        return [f"w-anchor {relative} 이 git 에 추적되지 않는다"]
    clean = subprocess.run(["git", "diff", "--quiet", "HEAD", "--", relative], cwd=ROOT, capture_output=True, text=True)
    return [] if clean.returncode == 0 else [f"w-anchor {relative} 에 커밋되지 않은 변경이 있다"]


def anchor_commit_time(path_text):
    completed = subprocess.run(["git", "log", "-1", "--format=%cI", "--", relative_text(path_text)], cwd=ROOT, capture_output=True, text=True)
    text = completed.stdout.strip()
    if completed.returncode != 0 or not text:
        raise InputError(f"w-anchor {path_text} 의 커밋 시각을 git 에서 읽지 못했다")
    return prereg_lock.parse_moment(text)


def w_anchor_history(path_text):
    relative = relative_text(path_text)
    listed = subprocess.run(["git", "log", "--format=%H", "--", relative], cwd=ROOT, capture_output=True, text=True)
    history = []
    for commit in listed.stdout.split():
        shown = subprocess.run(["git", "show", f"{commit}:{relative}"], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if shown.returncode != 0:
            continue
        try:
            history.append(json.loads(shown.stdout))
        except ValueError:
            continue
    return history


def canonical_disk_sha():
    return {"tuning": file_sha256(fluid_tuning_patch.canonical_tuning_path()), "look": file_sha256(fluid_tuning_patch.canonical_look_path())}


def perf_candidates_sha():
    return file_sha256(PERF_CANDIDATES_PATH) if Path(PERF_CANDIDATES_PATH).is_file() else None


def adoption_sort_key(text):
    return (0, int(text), text) if str(text).isdigit() else (1, 0, str(text))


def run_rows_of(expect, run_id):
    return [row for row in expect["rows"] + expect.get("fallback_rows", []) if row["run"] == run_id]


def adoption_keys(rows):
    return sorted({row["adoption"] for row in rows}, key=adoption_sort_key)


def adoption_states(rows, scenes, checks):
    control = {scene["name"]: scene.get("control_ok") is True for scene in scenes or []}
    by_key = {(check["scene"], check["metric"]): check for check in checks or []}
    states = {}
    for adoption in adoption_keys(rows):
        own = [row for row in rows if row["adoption"] == adoption]
        outcomes = [(by_key.get((row["scene"], row["metric"])) or {}).get("passed") for row in own]
        if not all(control.get(row["scene"], False) for row in own) or any(outcome is None for outcome in outcomes):
            states[adoption] = "undetermined"
        elif any(outcome is False for outcome in outcomes):
            states[adoption] = "fail"
        else:
            states[adoption] = "pass"
    return states


def worst_state(states):
    values = list(states)
    return max(values, key=lambda state: STATE_SEVERITY[state]) if values else None


def work_path_forms(folder_text):
    forms = set()
    for text in (f"{folder_text}/judge", f"{folder_text}/compare-judge"):
        path = prereg_lock.project_path(text)
        forms.update({str(path), path.as_posix(), relative_text(path), text})
    return sorted(forms, key=len, reverse=True)


def replace_work_paths(value, forms):
    if isinstance(value, str):
        for form in forms:
            value = value.replace(form, WORK_PLACEHOLDER)
        return value
    if isinstance(value, list):
        return [replace_work_paths(item, forms) for item in value]
    if isinstance(value, dict):
        return {key: replace_work_paths(item, forms) for key, item in value.items()}
    return value


def stripped_scenes(scenes, forms):
    return [replace_work_paths({key: value for key, value in scene.items() if key not in SCENE_PATH_KEYS}, forms) for scene in scenes or []]


def is_self_check_equal(rows, verify_document, compare_document, folder_text):
    forms = work_path_forms(folder_text)
    official_checks = (verify_document.get("verify") or {}).get("checks")
    is_same_checks = canonical_json_text(official_checks) == canonical_json_text(compare_document.get("checks"))
    is_same_scenes = canonical_json_text(stripped_scenes(verify_document.get("scenes"), forms)) == canonical_json_text(stripped_scenes(compare_document.get("scenes"), forms))
    is_same_states = adoption_states(rows, verify_document.get("scenes"), official_checks) == compare_document.get("adoptions")
    return is_same_checks and is_same_scenes and is_same_states


def sweep_attempts_of(final, stage, opportunity):
    return [attempt for attempt in final["attempts"] if attempt["step"] == "sweep" and attempt["stage"] == stage and attempt["opportunity"] == opportunity]


def done_folder(attempts):
    done = [attempt for attempt in attempts if attempt["exit"] == 0]
    return done[-1]["folder"] if done else None


def identity_resolver(text):
    return relative_text(text)


def archive_resolver(final_dir, archive):
    prefixes = (f"{final_dir}/batch-e1/", f"{final_dir}/locks/")
    anchor_text = f"{final_dir}/anchor.json"

    def resolve(text):
        relative = relative_text(text)
        if relative == anchor_text or relative.startswith(prefixes):
            return f"{archive}/{relative[len(final_dir) + 1:]}"
        return relative
    return resolve


def item_self_checks(expect, item, resolve):
    final = item["final"]
    rows = run_rows_of(expect, final["run"])
    results = {}
    for opportunity in VERIFY_OPPORTUNITIES:
        folder = done_folder(sweep_attempts_of(final, "verify", opportunity))
        if folder is None:
            results[opportunity] = None
            continue
        verify_document = read_json_text(f"{resolve(folder)}/verify.json")
        compare_document = read_json_text(f"{resolve(folder)}/compare.json")
        results[opportunity] = is_self_check_equal(rows, verify_document, compare_document, folder)
    return results


def overlay_entries(overlay_text):
    overlay = fluid_tuning_patch.read_overlay(prereg_lock.project_path(str(overlay_text)))
    entries = {}
    for entry in overlay["assignments"] + overlay["actor_props"]:
        identifier = entry["json_path"] if "json_path" in entry else fluid_tuning_patch.describe_actor_prop(entry)
        entries[identifier] = entry
    return entries


def fog_generator_light():
    lines = [line for line in prereg_lock.project_path(FOG_GENERATOR).read_text(encoding="utf-8").splitlines() if f"\"label\": \"{FOG_LIGHT_LABEL}\"" in line]
    if len(lines) != 1:
        return None
    match = FOG_LIGHT_VALUE_PATTERN.search(lines[0])
    return float(match.group(1)) if match else None


def is_fog_light_entry(identifier):
    return identifier.startswith("actor_prop:") and FOG_LIGHT_LABEL in identifier


def w12_problems(r1_final_dir, r2_w_expect_text):
    rollback_text = f"{r1_final_dir}/r2/rollback.json"
    keep_text = f"{r1_final_dir}/overlay-keep.json"
    if not is_file_text(rollback_text):
        return [f"D5 W12 검사: rollback 보고 없음 {rollback_text}"]
    rollback = read_json_text(rollback_text)
    verdict_text = rollback.get("verdict") or ""
    if not is_file_text(verdict_text) or sha_of_text(verdict_text) != rollback.get("verdict_sha256"):
        return ["D5 W12 검사: rollback verdict 파일 sha ≠ verdict_sha256"]
    if sha_or_none(keep_text) != rollback.get("keep_sha256"):
        return ["D5 W12 검사: overlay-keep sha ≠ rollback keep_sha256"]
    frozen = overlay_entries(read_json_text(f"{r1_final_dir}/final-spec.json")["overlay"])
    reverted = {item["entry_id"] for item in read_json_text(verdict_text).get("revert") or []}
    keep = overlay_entries(keep_text)
    problems = []
    if set(keep) != set(frozen) - reverted:
        problems.append("D5 W12 검사: 남긴 항목 집합 A ≠ K(동결본 − 되돌림)")
    problems += [f"D5 W12 검사: 남긴 항목 {identifier} 값 ≠ W1 동결본" for identifier in sorted(keep) if canonical_json_text(keep[identifier]) != canonical_json_text(frozen.get(identifier))]
    light = fog_generator_light()
    if not any(is_fog_light_entry(identifier) for identifier in reverted):
        if light != FOG_LIGHT_ADOPTED:
            problems.append(f"D5 W12 검사: #18 을 남겼는데 안개 무대 생성기 {FOG_LIGHT_LABEL} 값 {light} ≠ {FOG_LIGHT_ADOPTED}")
        return problems
    if light != FOG_LIGHT_BEFORE:
        problems.append(f"D5 W12 검사: #18 을 되돌렸는데 안개 무대 생성기 {FOG_LIGHT_LABEL} 값 {light} ≠ {FOG_LIGHT_BEFORE}")
    if not is_file_text(FOG_STAGE_REPORT) or file_moment(FOG_STAGE_REPORT) <= prereg_lock.parse_moment(rollback["finished_at"]):
        problems.append("D5 W12 검사: 안개 무대 보고 mtime ≤ rollback finished_at(무대 재생성 기록 없음)")
    written = (read_json_text(r2_w_expect_text).get("written") or {}) if is_file_text(r2_w_expect_text) else {}
    report = read_json_text(FOG_STAGE_REPORT) if is_file_text(FOG_STAGE_REPORT) else {}
    if report.get("preset_tuning_sha256") != written.get("tuning"):
        problems.append("D5 W12 검사: 안개 무대 보고 preset_tuning_sha256 ≠ r2 w-expect written.tuning")
    return problems


def require_no_problems(problems, title):
    if problems:
        raise InputError(f"{title}: " + " | ".join(problems[:12]))


def check_final_queue_format(queue):
    final = queue.get("final")
    if queue.get("mode") != "final" or not isinstance(final, dict):
        raise InputError("최종 모드 큐가 아니다(mode final 과 final 객체가 필요하다)")
    missing = [key for key in FINAL_QUEUE_KEYS if key not in final]
    if missing:
        raise InputError(f"큐 final 에 키가 없다: {missing}")
    edition = final["edition"]
    if FINAL_EDITION_PHASES.get(edition) != final["phase"]:
        raise InputError(f"판 {edition} 과 phase {final['phase']} 가 맞지 않다(e1 → r1, r2 → r2)")
    if final["restart"] not in (0, 1) or (edition == "r2" and final["restart"] != 0):
        raise InputError(f"restart {final['restart']} 는 0·1 이어야 한다(r2 는 0)")
    block_ids = [block.get("id") for block in queue.get("blocks") or []]
    if not block_ids or block_ids != [block for block in FINAL_BLOCK_ORDER if block in block_ids]:
        raise InputError(f"블록 id 는 {FINAL_BLOCK_ORDER} 순서로 한 번씩만 나와야 한다: {block_ids}")
    plans = [entry.get("plan") for _, entry in queue_entries(queue)]
    if not plans or None in plans:
        raise InputError("큐에 계획이 없다")
    duplicates = sorted(plan for plan, count in Counter(plans).items() if count > 1)
    if duplicates:
        raise InputError(f"큐에 같은 계획이 두 번: {duplicates}")


def check_judge_files(final, final_dir, expect_text, spec_text):
    judge_files = final["judge_files"]
    if not SELF_PATH.is_file() or file_sha256(SELF_PATH) != judge_files.get(FINAL_BATCH_PATH):
        raise InputError(f"자기 sha({SELF_PATH}) ≠ 큐 judge_files[{FINAL_BATCH_PATH}](D-D)")
    spec = read_json_text(spec_text)
    derived = {FINAL_TOOL_PATH, expect_text, FINAL_BATCH_PATH, FINAL_TEST_CLASSES_PATH, relative_text(spec["overlay"]), spec_text}
    if set(judge_files) != derived:
        raise InputError(f"큐 judge_files 키 ≠ 판정 파일 6 파생 집합: {sorted(set(judge_files) ^ derived)}")
    changed = [path for path, recorded in sorted(judge_files.items()) if sha_or_none(path) != recorded]
    if changed:
        raise InputError(f"판정 파일 sha 가 큐에 동결한 값과 다르다: {changed}")


def check_test_classes(queue):
    classes = queue.get("test_classes") or {}
    if not classes.get("path") or sha_or_none(classes["path"]) != classes.get("sha256"):
        raise InputError(f"시험 분류표 sha256 이 큐에 동결한 값과 다르다: {classes.get('path')}")


def required_anchor_paths(queue_text, expect_text, queue):
    paths = {queue_text, expect_text}
    for _, entry in queue_entries(queue):
        paths.update(relative_text(entry[key]) for key in ("plan", "overlay", "verify_against") if entry.get(key))
        if entry.get("judge_lock"):
            paths.add(relative_text(entry["lock"]))
    return paths


def final_anchor_problems(context):
    final, w_anchor_text, anchor_text = context["final"], context["w_anchor_text"], context["anchor_text"]
    if not is_file_text(w_anchor_text):
        return [f"w-anchor 없음: {w_anchor_text}"], None
    w_anchor = read_json_text(w_anchor_text)
    if not isinstance(w_anchor, dict) or set(w_anchor) != {"anchor", "edition", "sha256"}:
        return ["w-anchor 키가 정확히 anchor·edition·sha256 이 아니다"], None
    problems = list(anchor_commit_problems(w_anchor_text))
    if w_anchor["edition"] != final["edition"]:
        problems.append(f"w-anchor edition {w_anchor['edition']} ≠ 큐 {final['edition']}")
    if relative_text(w_anchor["anchor"]) != anchor_text:
        problems.append(f"w-anchor anchor {w_anchor['anchor']} ≠ 큐 final.anchor {anchor_text}")
    if not is_file_text(anchor_text):
        return problems + [f"닻 없음: {anchor_text}"], None
    if sha_of_text(anchor_text) != w_anchor["sha256"]:
        problems.append("anchor.json sha ≠ w-anchor sha256")
    anchor = read_json_text(anchor_text)
    if anchor.get("edition") != final["edition"] or anchor.get("restart") != final["restart"]:
        problems.append(f"anchor.json edition·restart {anchor.get('edition')}·{anchor.get('restart')} ≠ 큐 {final['edition']}·{final['restart']}")
    files = anchor.get("files") or {}
    problems += [f"닻 파일 없음 또는 sha 다름: {path}" for path, recorded in sorted(files.items()) if sha_or_none(path) != recorded]
    problems += [f"닻에 필요한 파일이 없다: {path}" for path in sorted(required_anchor_paths(context["queue_text"], context["expect_text"], context["queue"])) if path not in files]
    return problems, anchor


def check_anchor_time(context):
    if context["final"]["edition"] != "e1" or context["final"]["restart"] != 0:
        return
    write_canonical = f"{context['final_dir']}/write-canonical.json"
    if not is_file_text(write_canonical):
        raise InputError(f"W3 정본 쓰기 기록 없음: {write_canonical}")
    if anchor_commit_time(context["w_anchor_text"]) > file_moment(write_canonical):
        raise InputError("w-anchor 커밋 시각 > W3 write-canonical.json mtime(D4·P-04: 닻은 W3 정본 쓰기 전에 커밋)")


def old_journal_problems(final_dir, expect, previous_journal_text):
    archive = parent_text(parent_text(previous_journal_text))
    if parent_text(archive) != final_dir or not archive.rsplit("/", 1)[-1].startswith("aborted-") or previous_journal_text != f"{archive}/batch-e1/journal.json":
        return [f"재시작 정당성: 옛 일지가 {final_dir}/aborted-<시각>/batch-e1/journal.json 이 아니다: {previous_journal_text}"], None
    old = read_json_text(previous_journal_text)
    problems = []
    if ((old.get("final_batch") or {}).get("outcome") or {}).get("event") != "E3":
        problems.append("재시작 정당성: 옛 일지 final_batch.outcome.event 가 E3 이 아니다")
    resolve = archive_resolver(final_dir, archive)
    problems += [f"재시작 정당성: 옛 일지 산출물 {path} 를 바꿔 읽은 {resolve(path)} 의 sha 가 artifact_sha256 과 다르거나 없다"
                 for item in old.get("items") or [] for path, recorded in sorted(((item.get("final") or {}).get("artifact_sha256") or {}).items()) if sha_or_none(resolve(path)) != recorded]
    f0_items = [item for item in old.get("items") or [] if (item.get("final") or {}).get("kind") == "verify" and expect["runs"].get(item["final"].get("run"), {}).get("block") == "F0"]
    checks = [item_self_checks(expect, item, resolve) for item in f0_items]
    if not any(False in result.values() for result in checks):
        problems.append("재시작 정당성: 옛 일지 F0 자기 검증을 파일로 다시 계산하면 불일치가 없다(D1)")
    return problems, old


def restart_problems(context, anchor):
    final_dir, expect = context["final_dir"], context["expect"]
    previous_journal_text = anchor.get("previous_journal")
    if not is_file_text(previous_journal_text):
        return [f"재시작 정당성: anchor previous_journal(기록 폴더 옛 일지) 없음: {previous_journal_text}"]
    problems, old = old_journal_problems(final_dir, expect, relative_text(previous_journal_text))
    if old is None:
        return problems
    previous_anchor = anchor.get("previous_anchor") or {}
    previous_anchor_text = previous_anchor.get("path")
    if not is_file_text(previous_anchor_text):
        return problems + [f"W11 닻 확인: 옮긴 anchor.json 없음: {previous_anchor_text}"]
    previous_sha = sha_of_text(previous_anchor_text)
    if previous_sha != previous_anchor.get("sha256") or previous_sha != (old.get("final_batch") or {}).get("anchor_sha256"):
        problems.append("W11 닻 확인(개정 F1): 옮긴 anchor.json sha ≠ anchor previous_anchor.sha256 또는 옛 일지 final_batch.anchor_sha256")
    committed = {"anchor": f"{final_dir}/anchor.json", "edition": "e1", "sha256": previous_sha}
    history = [entry for entry in w_anchor_history(context["w_anchor_text"]) if isinstance(entry, dict)]
    if not any(set(entry) == set(committed) and relative_text(entry["anchor"]) == committed["anchor"] and entry["edition"] == "e1" and entry["sha256"] == previous_sha for entry in history):
        problems.append("W11 닻 확인(개정 F1): w-anchor git 이력에 옮긴 anchor.json sha 를 담은 e1 커밋이 없다")
    old_files = read_json_text(previous_anchor_text).get("files") or {}
    new_files = anchor.get("files") or {}
    excluded = {FINAL_TOOL_PATH, f"{final_dir}/expect.json", f"{final_dir}/final_queue.json"}
    for path in sorted((set(old_files) | set(new_files)) - excluded):
        if path not in new_files:
            problems.append(f"재시작 범위(개정 E2·F1): 옮긴 W11 닻에만 있는 경로 {path}")
        elif path not in old_files:
            problems.append(f"재시작 범위(개정 E2·F1): 새 닻에만 있는 경로 {path}")
        elif old_files[path] != new_files[path]:
            problems.append(f"재시작 범위(개정 E2·F1): 세 파일 밖 {path} sha 가 옮긴 W11 닻과 다르다")
    return problems


def r2_start_problems(context, anchor):
    final_dir = context["final_dir"]
    r1_final_dir = parent_text(final_dir)
    previous = anchor.get("previous_anchor") or {}
    previous_text = previous.get("path")
    if not previous_text or relative_text(previous_text) != f"{r1_final_dir}/anchor.json" or sha_or_none(previous_text) != previous.get("sha256"):
        return ["r2 닻(개정 E3): previous_anchor 가 r1 anchor.json 이 아니거나 그 sha 가 지금과 다르다"]
    previous_files = read_json_text(previous_text).get("files") or {}
    files = anchor.get("files") or {}
    problems = [f"r2 닻(개정 E3): r1 닻 항목 {path} 이 r2 닻에 같은 sha 로 없다" for path, recorded in sorted(previous_files.items()) if files.get(path) != recorded]
    allowed = {f"{r1_final_dir}/overlay-keep.json", f"{final_dir}/w-expect.json", f"{final_dir}/rollback.json", f"{r1_final_dir}/write-canonical-r2.json",
               f"{final_dir}/final-spec.json", f"{final_dir}/copies.json", f"{final_dir}/expect.json", f"{final_dir}/final_queue.json"}
    if is_file_text(f"{final_dir}/copies.json"):
        allowed |= {relative_text(output["path"]) for output in read_json_text(f"{final_dir}/copies.json").get("outputs") or []}
    preview_prefix = f"{r1_final_dir}/preview-r2/"
    problems += [f"r2 닻(개정 E3): 새 경로 {path} 가 허용 목록 밖이다" for path in sorted(files) if path not in previous_files and path not in allowed and not path.startswith(preview_prefix)]
    r2_spec = read_json_text(f"{final_dir}/final-spec.json")
    return problems + w12_problems(r1_final_dir, relative_text(r2_spec["w_expect"]))


def verify_entry_problems(entry, block_id, expect, final_dir, edition, label):
    run_id = entry.get("run")
    run = expect["runs"].get(run_id)
    if run is None:
        return [f"{label}: run {run_id} 이 expect.runs 에 없다"]
    problems = []
    if run["block"] != block_id:
        problems.append(f"{label}: 블록 {block_id} ≠ expect {run['block']}")
    if relative_text(entry["plan"]) != relative_text(run["plan"]) or relative_text(entry["overlay"]) != relative_text(run["overlay"]):
        problems.append(f"{label}: plan·overlay 가 expect 실행과 다르다")
    if sha_or_none(entry["overlay"]) != run["overlay_sha256"]:
        problems.append(f"{label}: overlay sha ≠ expect overlay_sha256")
    if block_id != "F0":
        if entry.get("judge_lock") or entry.get("verify_against"):
            problems.append(f"{label}: F1 항목에 judge_lock·verify_against 가 있다")
        if relative_text(entry["lock"]) != f"{final_dir}/locks/{run_id}-{edition}-prereg-lock.json":
            problems.append(f"{label}: 실행 잠금 {entry['lock']} ≠ 파생 경로(잠금 이름에 판 {edition})")
        return problems
    if not entry.get("judge_lock") or not entry.get("verify_against"):
        return problems + [f"{label}: F0 항목에 judge_lock·verify_against 가 없다"]
    if relative_text(entry["judge_lock"]) != f"{final_dir}/locks/{run_id}-judge-{edition}-prereg-lock.json":
        problems.append(f"{label}: 판정 잠금 {entry['judge_lock']} ≠ 파생 경로(잠금 이름에 판 {edition})")
    if relative_text(entry["lock"]) != relative_text(run["lock"]) or relative_text(entry["verify_against"]) != relative_text(run["verify_against"]):
        problems.append(f"{label}: F0 원래 잠금·verify_against 가 expect 와 다르다")
    return problems


def ab_entry_problems(entry, block_id, expect, seen_runs, final_dir, edition, label):
    ab = entry.get("ab") or {}
    expected = next((item for item in expect["ab"] if item["id"] == ab.get("id")), None)
    if expected is None:
        return [f"{label}: A/B {ab.get('id')} 가 expect.ab 에 없다"]
    problems = []
    if block_id != "F2":
        problems.append(f"{label}: A/B 는 F2 블록에만 둔다")
    if set(ab) != set(FINAL_AB_FIELDS) or any(ab[key] != expected[key] for key in FINAL_AB_FIELDS):
        problems.append(f"{label}: ab 필드가 expect.ab 와 다르다")
    if ab.get("trigger") not in AB_TRIGGERS:
        problems.append(f"{label}: trigger {ab.get('trigger')} 모름")
    if ab.get("for_run") not in seen_runs:
        problems.append(f"{label}: ab.for_run {ab.get('for_run')} 이 큐에서 앞에 나온 verify 항목이 아니다")
    if relative_text(entry["plan"]) != relative_text(expected["plan"]) or relative_text(entry["overlay"]) != relative_text(expected["overlay"]):
        problems.append(f"{label}: plan·overlay 가 expect.ab 와 다르다")
    if sha_or_none(entry["overlay"]) != expected["overlay_sha256"]:
        problems.append(f"{label}: overlay sha ≠ expect overlay_sha256")
    if relative_text(entry["lock"]) != f"{final_dir}/locks/{ab.get('id')}-{edition}-prereg-lock.json":
        problems.append(f"{label}: 실행 잠금 {entry['lock']} ≠ 파생 경로(잠금 이름에 판 {edition})")
    return problems


def final_item_problems(context):
    problems, seen_runs = [], set()
    for block, entry in queue_entries(context["queue"]):
        label = f"{block['id']} {entry.get('plan')}"
        if entry.get("stages") not in FINAL_STAGE_SETS:
            problems.append(f"{label}: stages 는 {FINAL_STAGE_SETS} 중 하나")
            continue
        if not is_file_text(entry["plan"]):
            problems.append(f"{label}: 계획 파일 없음")
        if not entry.get("lock") or not entry.get("overlay"):
            problems.append(f"{label}: lock·overlay 없음")
            continue
        if entry["stages"] == FINAL_STAGE_SETS[0]:
            problems += verify_entry_problems(entry, block["id"], context["expect"], context["final_dir"], context["final"]["edition"], label)
            seen_runs.add(entry.get("run"))
            continue
        problems += ab_entry_problems(entry, block["id"], context["expect"], seen_runs, context["final_dir"], context["final"]["edition"], label)
    return problems


def load_final_context(arguments, queue_path):
    queue = read_json(queue_path)
    check_final_queue_format(queue)
    final = queue["final"]
    expect_text = relative_text(final["expect"])
    final_dir = parent_text(expect_text)
    spec_text = f"{final_dir}/final-spec.json"
    check_judge_files(final, final_dir, expect_text, spec_text)
    check_test_classes(queue)
    if relative_text(arguments.journal) != relative_text(final["journal"]) or relative_text(final["journal"]) != f"{final_dir}/batch-{final['edition']}/journal.json":
        raise InputError(f"--journal 은 큐 final.journal({final['journal']})이어야 한다")
    if arguments.block or arguments.revisit_deferred:
        raise InputError("최종 모드에서는 --block·--revisit-deferred 를 받지 않는다")
    expect = read_json_text(expect_text)
    context = {"queue": queue, "queue_text": relative_text(queue_path), "final": final, "final_dir": final_dir, "expect_text": expect_text, "expect": expect,
               "anchor_text": relative_text(final["anchor"]), "w_anchor_text": relative_text(final["w_anchor"])}
    if context["anchor_text"] != f"{final_dir}/anchor.json":
        raise InputError(f"큐 final.anchor 는 {final_dir}/anchor.json 이어야 한다")
    problems, anchor = final_anchor_problems(context)
    require_no_problems(problems, "닻 대조(C2·D2)")
    check_anchor_time(context)
    if final["edition"] == "e1" and final["restart"] == 1:
        require_no_problems(restart_problems(context, anchor), "재시작 시작 검사(개정 E2·F1)")
    if final["edition"] == "r2":
        require_no_problems(r2_start_problems(context, anchor), "r2 시작 검사(개정 E3·E4·D5)")
    require_no_problems(final_item_problems(context), "큐 항목")
    context["anchor_sha256"] = sha_of_text(context["anchor_text"])
    context["w_anchor_sha256"] = sha_of_text(context["w_anchor_text"])
    return context


def new_final_item(block_id, entry, edition):
    if "ab" in entry:
        final = {"kind": "ab", "ab_id": entry["ab"]["id"], "edition": edition, "trigger_state": None, "decision": None, "reason": "", "attempts": [],
                 "overlay_sha256_before": None, "overlay_sha256_after": None, "perf_candidates_sha256_before": None, "perf_candidates_sha256_after": None, "artifact_sha256": {}}
    else:
        final = {"kind": "verify", "run": entry["run"], "edition": edition, "attempts": [], "adoptions_first": {}, "adoptions": {}, "official_first": {}, "official_rerun": {},
                 "self_check": dict.fromkeys(VERIFY_OPPORTUNITIES) if block_id == "F0" else None, "rerun": 0, "overlay_sha256_before": None, "overlay_sha256_after": None,
                 "artifact_sha256": {}}
    return {"plan": entry["plan"], "block": block_id, "chain": None, "status": "pending", "runs": {}, "attempts": 0, "wall_seconds": 0.0, "estimate_seconds": 0.0,
            "reason": "", "final": final}


def open_final_journal(journal_path, context, queue_sha256, is_resume):
    final = context["final"]
    if not is_resume:
        if journal_path.exists():
            raise InputError(f"일지가 이미 있다(--resume 으로 이어 하거나 옮긴다): {journal_path}")
        items = [new_final_item(block["id"], entry, final["edition"]) for block, entry in queue_entries(context["queue"])]
        final_batch = {"edition": final["edition"], "phase": final["phase"], "restart": final["restart"], "anchor_sha256": context["anchor_sha256"],
                       "w_anchor_sha256": context["w_anchor_sha256"], "starts": [], "outcome": None, "f0_gate": None}
        return {"queue_sha256": queue_sha256, "items": items, "real_dt": {}, "holds": [], "final_batch": final_batch}
    if not journal_path.exists():
        raise InputError(f"이어 할 일지 없음: {journal_path}")
    journal = read_json(journal_path)
    if journal.get("queue_sha256") != queue_sha256:
        raise InputError("큐 sha256 이 일지에 동결한 값과 다르다(큐를 바꾸면 새 일지)")
    final_batch = journal.get("final_batch") or {}
    if final_batch.get("anchor_sha256") != context["anchor_sha256"]:
        raise InputError("일지 final_batch.anchor_sha256 ≠ 지금 anchor.json sha")
    if (final_batch.get("edition"), final_batch.get("restart")) != (final["edition"], final["restart"]):
        raise InputError("일지 final_batch.edition·restart ≠ 큐")
    for item in journal["items"]:
        for attempt in item["final"]["attempts"]:
            if attempt["state"] == "running":
                attempt["state"] = "interrupted"
    return journal


class FinalBatchRunner(BatchRunner):
    def __init__(self, arguments, queue, journal, context):
        super().__init__(arguments, queue, journal)
        self.context = context
        self.expect = context["expect"]
        self.final = queue["final"]
        self.final_batch = journal["final_batch"]
        self.current_item = None

    def item_id(self, item):
        return item["final"].get("run") or item["final"].get("ab_id")

    def item_log(self, item, name):
        return self.work_dir / "runs" / self.plan_name(item) / f"{name}.log"

    def mismatch_event(self):
        return "E3_repeat" if self.final["edition"] == "e1" and self.final["restart"] == 1 else "E3"

    def release_journal_holds(self, start_dir):
        owners = list(dict.fromkeys(hold["owner"] for hold in self.journal["holds"] if not hold.get("released_at")))
        if not owners:
            self.record("보류 회수", "없음")
        for owner in owners:
            code, _ = self.tool(f"보류 회수 {owner}", python_command(HOLD_TOOL, "--release-hold", owner), start_dir / "release-hold.log")
            for hold in self.journal["holds"]:
                if hold["owner"] == owner and not hold.get("released_at"):
                    hold.update(released_at=now_text(), released_by=f"시작 회수(종료 {code})")
        self.save()

    def start(self):
        starts = self.final_batch["starts"]
        starts.append({"at": final_now_text(), "ok": None, "reason": ""})
        self.save()
        start_dir = self.work_dir / "start"
        self.release_journal_holds(start_dir)
        code, tail = self.tool("보류 획득", python_command(HOLD_TOOL, "--acquire-hold", self.holder, "--reason", "calib_batch"), start_dir / "acquire-hold.log")
        if code != 0:
            return self.fail_start(f"보류 획득 종료 {code}: {tail}")
        self.journal["holds"].append({"owner": self.holder, "acquired_at": now_text()})
        self.save()
        steps = (("ensure", python_command(EDITOR_TOOL, "ensure"), "ensure.log"),
                 ("t.OverrideFPS 0", python_command(REMOTE_TOOL, "-c", FIXED_STEP_RESET_CODE), "fixed-step.log"),
                 ("정본 재적재", python_command(REMOTE_TOOL, "-c", CANONICAL_RELOAD_CODE), "canonical-reload.log"))
        for label, command, log_name in steps:
            code, tail = self.tool(label, command, start_dir / log_name)
            if code != 0:
                return self.fail_start(f"{label} 종료 {code}: {tail}")
        try:
            self.require_canonical(None)
        except FinalStop as stop:
            starts[-1].update(ok=False, reason=stop.reason)
            return self.stop_batch(stop)
        starts[-1]["ok"] = True
        self.save()
        return None

    def fail_start(self, reason):
        starts = self.final_batch["starts"]
        starts[-1].update(ok=False, reason=reason[-REASON_TAIL_CHARACTERS:])
        if len(starts) >= 2 and starts[-2].get("ok") is False:
            self.final_batch["outcome"] = {"event": "start_failed", "item": None, "reason": "시작 처리 연속 2회 실패(B1 재시도 1회 뒤 다시)", "at": final_now_text()}
        self.save()
        self.record("시작 처리", "실패", reason[:200])
        return FINAL_EVENT_EXIT_CODES["start_failed"]

    def require_canonical(self, item):
        expected = self.expect["canonical_after"]
        if canonical_disk_sha() == expected:
            return
        command = python_command(REMOTE_TOOL, "-c", CANONICAL_RELOAD_CODE)
        if item is None:
            self.tool("정본 재적재(sha 다름)", command, self.work_dir / "start" / "canonical-reload-sha.log")
        else:
            self.run_attempt(item, "canonical_reload", command, self.item_log(item, "canonical-reload"))
        if canonical_disk_sha() != expected:
            raise FinalStop("E2", "정본 sha ≠ 마지막 정본 쓰기 기록(expect canonical_after), 재적재 1회 뒤에도 다르다(B2)")

    def run_attempt(self, item, step, command, log_path, stage=None, opportunity=None, try_number=None, folder=None, timeout=TOOL_TIMEOUT_SECONDS):
        attempt = {"step": step, "stage": stage, "opportunity": opportunity, "try": try_number, "folder": relative_text(folder) if folder else None,
                   "log": relative_text(log_path), "started_at": final_now_text(), "exit": None, "state": "running"}
        item["final"]["attempts"].append(attempt)
        self.save()
        label = " ".join(str(part) for part in (step, stage, opportunity, f"{try_number}회" if try_number else None) if part)
        code, tail = self.item_tool(item, label, command, log_path, timeout)
        attempt.update(exit=code, state="timeout" if code is None else "done")
        self.save()
        return attempt, tail

    def lock_step(self, item, step, command, lock_text):
        attempt, tail = self.run_attempt(item, step, command, self.item_log(item, step))
        if attempt["exit"] != 0 or not is_file_text(lock_text):
            raise FinalStop("E2", f"{step} 종료 {attempt['exit']}({attempt['state']}): {tail}")
        item["final"]["artifact_sha256"][relative_text(lock_text)] = sha_of_text(lock_text)

    def prepare_locks(self, item, entry):
        identifier, suffix = self.item_id(item), self.final["edition"]
        file_arguments = [part for path in [*self.final["judge_files"], self.final["anchor"]] for part in ("--file", path)]
        if item["block"] == "F0":
            self.lock_step(item, "lock_verify", python_command(LOCK_TOOL, "verify", "--lock", entry["lock"]), entry["lock"])
            if is_file_text(entry["judge_lock"]):
                self.lock_step(item, "judge_lock_verify", python_command(LOCK_TOOL, "verify", "--lock", entry["judge_lock"]), entry["judge_lock"])
            else:
                command = python_command(LOCK_TOOL, "lock", "--out", entry["judge_lock"], "--note", f"{identifier} {suffix} 판정 잠금", *file_arguments)
                self.lock_step(item, "judge_lock_create", command, entry["judge_lock"])
            item["runs"].update(lock=entry["lock"], judge_lock=entry["judge_lock"])
        elif is_file_text(entry["lock"]):
            self.lock_step(item, "lock_verify", python_command(LOCK_TOOL, "verify", "--lock", entry["lock"]), entry["lock"])
            item["runs"]["lock"] = entry["lock"]
        else:
            command = python_command(LOCK_TOOL, "lock", "--out", entry["lock"], "--plan", item["plan"], "--note", f"{identifier} {suffix} 최종 검증 실행 잠금", *file_arguments)
            self.lock_step(item, "lock_create", command, entry["lock"])
            item["runs"]["lock"] = entry["lock"]
        if item["status"] == "pending":
            self.set_status(item, "locked", "")
        self.save()

    def measured_folder(self, item, entry, stage, opportunity, extra_arguments):
        final = item["final"]
        while True:
            attempts = sweep_attempts_of(final, stage, opportunity)
            folder = done_folder(attempts)
            if folder:
                return folder
            if len(attempts) >= 2:
                return None
            self.require_canonical(item)
            self.refresh_hold()
            run_dir, log_path = self.run_dir_and_log(item, stage)
            limit = STEP_LIMIT_FACTOR * max(self.stage_estimate(item, stage), SCENE_OVERHEAD_SECONDS)
            command = python_command(SWEEP_TOOL, "--plan", item["plan"], "--stage", stage, "--overlay", entry["overlay"], "--run-dir", run_dir, "--lock", entry["lock"],
                                     *extra_arguments)
            attempt, tail = self.run_attempt(item, "sweep", command, log_path, stage, opportunity, len(attempts) + 1, run_dir, limit)
            item["attempts"] = sum(1 for each in final["attempts"] if each["step"] == "sweep")
            if attempt["exit"] == 0:
                sweep_text = f"{attempt['folder']}/sweep.json"
                if not is_file_text(sweep_text):
                    raise FinalStop("E2", f"sweep {stage} 종료 0 인데 {sweep_text} 이 없다")
                final["artifact_sha256"][sweep_text] = sha_of_text(sweep_text)
                self.update_real_dt(item, run_dir)
                self.save()
                continue
            if attempt["exit"] not in ENVIRONMENT_EXIT_CODES:
                raise FinalStop("E2", f"sweep {stage} 종료 {attempt['exit']}: {tail}")
            if attempt["try"] == 1:
                self.run_attempt(item, "ensure", python_command(EDITOR_TOOL, "ensure"), log_path.with_suffix(".ensure.log"), stage, opportunity)

    def judge_step(self, item, step, stage, opportunity, folder, command, output_text, allowed_exits):
        final = item["final"]
        for attempt in final["attempts"]:
            is_same = (attempt["step"], attempt["stage"], attempt["opportunity"], attempt["folder"]) == (step, stage, opportunity, folder)
            if is_same and attempt["state"] == "done" and attempt["exit"] in allowed_exits and is_file_text(output_text):
                return attempt["exit"]
        attempt, tail = self.run_attempt(item, step, command, prereg_lock.project_path(f"{folder}.{step}.log"), stage, opportunity, None, folder)
        if attempt["exit"] not in allowed_exits or not is_file_text(output_text):
            raise FinalStop("E2", f"{step} {stage} 종료 {attempt['exit']}({attempt['state']}): {tail}")
        final["artifact_sha256"][output_text] = sha_of_text(output_text)
        self.save()
        return attempt["exit"]

    def require_unchanged_overlay(self, final, overlay_text):
        final["overlay_sha256_after"] = sha_of_text(overlay_text)
        self.save()
        if final["overlay_sha256_after"] != final["overlay_sha256_before"]:
            raise FinalStop("E2", f"항목 덧씌움 앞뒤 sha 가 다르다(D2): {overlay_text}")

    def verify_opportunity(self, item, entry, opportunity, rows):
        folder = self.measured_folder(item, entry, "verify", opportunity, [])
        if folder is None:
            return "infeasible", None
        final = item["final"]
        is_f0 = item["block"] == "F0"
        if is_f0:
            command = python_command(JUDGE_TOOL, "--sweep", f"{folder}/sweep.json", "--lock", entry["lock"], "--output", f"{folder}/verify.json",
                                     "--verify-against", entry["verify_against"])
            self.judge_step(item, "official", "verify", opportunity, folder, command, f"{folder}/verify.json", (0, 1))
            item["runs"]["verify_official"] = f"{folder}/verify.json"
        judge_lock_arguments = ["--judge-lock", entry["judge_lock"]] if is_f0 else []
        command = python_command(COMPARE_TOOL, "compare", "--expect", self.final["expect"], "--run", final["run"], "--sweep", f"{folder}/sweep.json", "--lock", entry["lock"],
                                 *judge_lock_arguments, "--work-dir", f"{folder}/compare-judge", "--out", f"{folder}/compare.json", "--w-anchor", self.final["w_anchor"])
        self.judge_step(item, "compare", "verify", opportunity, folder, command, f"{folder}/compare.json", (0, 1))
        item["runs"].update(verify=folder, compare=f"{folder}/compare.json")
        compare_document = read_json_text(f"{folder}/compare.json")
        if not is_f0:
            self.save()
            return "done", compare_document.get("adoptions") or {}
        verify_document = read_json_text(f"{folder}/verify.json")
        official_key = "official_first" if opportunity == "first" else "official_rerun"
        final[official_key] = adoption_states(rows, verify_document.get("scenes"), (verify_document.get("verify") or {}).get("checks"))
        is_equal = is_self_check_equal(rows, verify_document, compare_document, folder)
        final["self_check"][opportunity] = is_equal
        self.save()
        if not is_equal:
            self.set_status(item, "self_check_mismatch", f"{opportunity} 기회 공식 verify.json 대 비교기 compare.json 자기 검증 불일치(E3)")
            raise FinalStop(self.mismatch_event(), f"{final['run']} {opportunity} 자기 검증 불일치")
        return "done", final[official_key]

    def advance_verify(self, item):
        entry = self.entries[item["plan"]]
        final = item["final"]
        rows = run_rows_of(self.expect, final["run"])
        keys = adoption_keys(rows)
        item["estimate_seconds"] = round(self.item_estimate(item), 1)
        if final["overlay_sha256_before"] is None:
            final["overlay_sha256_before"] = sha_of_text(entry["overlay"])
        self.prepare_locks(item, entry)
        first_outcome, first_states = self.verify_opportunity(item, entry, "first", rows)
        decision = {key: "incomplete" for key in keys}
        if first_outcome == "done":
            final["adoptions_first"] = first_states
            final["rerun"] = 1 if "undetermined" in first_states.values() else 0
            self.save()
            rerun_outcome, rerun_states = self.verify_opportunity(item, entry, "rerun", rows) if final["rerun"] else (None, None)
            decision = {}
            for key in keys:
                if first_states.get(key) in ("pass", "fail"):
                    decision[key] = first_states[key]
                elif rerun_outcome == "done":
                    decision[key] = rerun_states.get(key)
                else:
                    decision[key] = "incomplete"
        final["adoptions"] = decision
        self.require_unchanged_overlay(final, entry["overlay"])
        incomplete = [key for key, state in decision.items() if state == "incomplete"]
        status = "incomplete" if decision and len(incomplete) == len(decision) else "verified"
        self.set_status(item, status, f"최종 검증 미완 채택: {', '.join(incomplete)}" if incomplete else "")

    def decision_states(self):
        states, statuses = {}, {}
        for item in self.journal["items"]:
            final = item["final"]
            if final["kind"] == "verify" and item["status"] in ("verified", "incomplete"):
                states[final["run"]] = final["adoptions"]
                statuses[final["run"]] = item["status"]
        return states, statuses

    def target_state(self, key, states, statuses):
        covered = next((item for item in self.expect["covered"] if item["adoption"] == key), None)
        pairs = [(covered["by_run"], covered["by_adoption"])] if covered else sorted({(row["run"], key) for row in self.expect["rows"] if row["adoption"] == key})
        found = []
        for run_id, adoption in pairs:
            if statuses.get(run_id) == "incomplete":
                found.append("incomplete")
                continue
            state = (states.get(run_id) or {}).get(adoption)
            if state is None:
                return None
            found.append(state)
        return worst_state(found)

    def ab_judge(self, item, entry, stage, folder, result_text, extra_arguments):
        command = python_command(JUDGE_TOOL, "--sweep", f"{folder}/sweep.json", "--lock", entry["lock"], "--output", result_text, *extra_arguments)
        exit_code = self.judge_step(item, "judge", stage, None, folder, command, result_text, (0, 1))
        command = python_command(COMPARE_TOOL, "check-ab", "--expect", self.final["expect"], "--ab", entry["ab"]["id"], "--stage", stage, "--result", result_text,
                                 "--sweep", f"{folder}/sweep.json", "--lock", entry["lock"], "--out", f"{folder}/check-ab.json", "--w-anchor", self.final["w_anchor"])
        self.judge_step(item, "check_ab", stage, None, folder, command, f"{folder}/check-ab.json", (0,))
        item["runs"].update({stage: folder, f"check_ab_{stage}": f"{folder}/check-ab.json"})
        if perf_candidates_sha() != item["final"]["perf_candidates_sha256_before"]:
            raise FinalStop("E2", "perf-candidates 기록 발생(D2): 최종 모드 A/B 판정이 perf-candidates.json 을 바꿨다")
        self.save()
        return exit_code

    def ab_decision(self, item, entry):
        screen_folder = self.measured_folder(item, entry, "screen", None, [])
        if screen_folder is None:
            return "ab_incomplete"
        screen_text = f"{screen_folder}/screen.json"
        if self.ab_judge(item, entry, "screen", screen_folder, screen_text, []) == 1:
            return "ab_undetermined"
        screen = read_json_text(screen_text)
        if screen.get("insensitive") or not screen.get("best"):
            return "ab_keep"
        if item["status"] != "screened":
            self.set_status(item, "screened", "screen best 있음, confirm 으로")
        if screen.get("overlay_sha256") != sha_of_text(entry["overlay"]):
            raise FinalStop("E2", "confirm 전 screen.json overlay_sha256 ≠ 항목 덧씌움 sha(VL S-7)")
        confirm_folder = self.measured_folder(item, entry, "confirm", None, ["--screen-result", screen_text])
        if confirm_folder is None:
            return "ab_incomplete"
        judge_text = f"{confirm_folder}/judge.json"
        if self.ab_judge(item, entry, "confirm", confirm_folder, judge_text, ["--screen-sweep", f"{screen_folder}/sweep.json"]) == 1:
            return "ab_undetermined"
        adopt = read_json_text(judge_text).get("adopt") or {}
        if adopt.get("decision") is not True:
            return "ab_keep"
        if adopt.get("variant") != "v01":
            raise FinalStop("E2", f"confirm adopt variant {adopt.get('variant')} ≠ v01(P-24)")
        return "ab_v01"

    def finish_ab(self, item, entry, status, decision, reason):
        final = item["final"]
        self.require_unchanged_overlay(final, entry["overlay"])
        final["perf_candidates_sha256_after"] = perf_candidates_sha()
        final.update(decision=decision, reason=reason)
        self.set_status(item, status, reason)

    def advance_ab(self, item):
        entry = self.entries[item["plan"]]
        final = item["final"]
        ab = entry["ab"]
        item["estimate_seconds"] = round(self.item_estimate(item), 1)
        if item["status"] == "pending":
            final["overlay_sha256_before"] = sha_of_text(entry["overlay"])
            final["perf_candidates_sha256_before"] = perf_candidates_sha()
        states, statuses = self.decision_states()
        trigger_state = "incomplete" if statuses.get(ab["for_run"]) == "incomplete" else (states.get(ab["for_run"]) or {}).get(ab["trigger_adoption"])
        target_state = self.target_state(ab["for_adoption"], states, statuses)
        final["trigger_state"] = trigger_state
        self.save()
        if trigger_state is None or target_state is None:
            raise FinalStop("E2", f"{ab['id']}: 촉발 채택 {ab['trigger_adoption']}·대상 채택 {ab['for_adoption']} 의 결정 상태를 일지에서 찾지 못했다")
        if "incomplete" in (trigger_state, target_state):
            self.finish_ab(item, entry, "ab_skipped", "ab_skipped", "대상 미완")
            return
        if ab["trigger"] == "on_fail" and trigger_state == "pass":
            self.finish_ab(item, entry, "ab_skipped", "ab_skipped", "촉발 아님(on_fail, 결정 상태 pass)")
            return
        self.prepare_locks(item, entry)
        decision = self.ab_decision(item, entry)
        self.finish_ab(item, entry, "ab_done", decision, f"촉발 상태 {trigger_state} → {decision}")

    def pass_f0_gate(self):
        if self.final_batch.get("f0_gate") is not None:
            return
        f0_items = [item for item in self.journal["items"] if item["block"] == "F0"]
        if not f0_items:
            return
        self.current_item = None
        self_checks, sample, mismatched = {}, [], []
        for item in f0_items:
            run_id = item["final"]["run"]
            checks = item_self_checks(self.expect, item, identity_resolver)
            self_checks[run_id] = checks
            if False in checks.values():
                mismatched.append((item, checks))
                sample.append(run_id)
            elif item["status"] == "verified" and "incomplete" not in item["final"]["adoptions"].values():
                sample.append(run_id)
        sample.sort()
        required = F0_REQUIRED_SELF_CHECKS if self.final["edition"] == "e1" else 0
        passed = len(sample) >= required and not mismatched
        self.final_batch["f0_gate"] = {"self_checks": self_checks, "sample": sample, "required": required, "passed": passed}
        for item, checks in mismatched:
            item["final"]["self_check"] = checks
            self.set_status(item, "self_check_mismatch", "F0 관문에서 파일로 다시 계산한 자기 검증이 불일치(E3)")
        self.save()
        self.record("F0 관문", "통과" if passed else "멈춤", f"표본 {', '.join(sample) or '없음'} / 필요 {required}")
        if mismatched:
            raise FinalStop(self.mismatch_event(), "F0 관문 자기 검증 불일치", mismatched[0][0]["final"]["run"])
        if not passed:
            raise FinalStop("F0_shortfall", f"F0 자기 검증 표본 {len(sample)}건 < {required}건(개정 E1·D-W28)")

    def stop_batch(self, stop):
        item = self.current_item if stop.event in ("E2", "E3", "E3_repeat") else None
        if stop.event == "E2" and item is not None:
            self.set_status(item, "failed", stop.reason[-REASON_TAIL_CHARACTERS:])
        item_id = stop.item_id or (self.item_id(item) if item else None)
        self.final_batch["outcome"] = {"event": stop.event, "item": item_id, "reason": stop.reason[-REASON_TAIL_CHARACTERS:], "at": final_now_text()}
        self.save()
        self.record("사건", stop.event, f"{item_id or '-'} {stop.reason[:200]}")
        return FINAL_EVENT_EXIT_CODES[stop.event]

    def run(self):
        blocks = self.queue["blocks"]
        items = {item["plan"]: item for item in self.journal["items"]}
        has_f0 = any(block["id"] == "F0" for block in blocks)
        try:
            for block in blocks:
                if block["id"] != "F0" and has_f0:
                    self.pass_f0_gate()
                for entry in block["plans"]:
                    item = items[entry["plan"]]
                    if item["status"] in FINAL_TERMINAL_STATUSES:
                        continue
                    self.current_item = item
                    if "ab" in entry:
                        self.advance_ab(item)
                    else:
                        self.advance_verify(item)
                    self.save()
                    self.current_item = None
            if has_f0:
                self.pass_f0_gate()
        except FinalStop as stop:
            return self.stop_batch(stop)
        except FINAL_ERRORS as error:
            return self.stop_batch(FinalStop("E2", f"최종 모드 처리 오류 {type(error).__name__}: {error}"))
        return 0

    def print_final_summary(self):
        counts = Counter(item["status"] for item in self.journal["items"])
        print("상태:", ", ".join(f"{status} {count}" for status, count in sorted(counts.items())))
        for item in self.journal["items"]:
            print(f"  {item['block']} {self.item_id(item)} {Path(item['plan']).stem}: {item['status']} {item['reason'][:160]}")
        outcome = self.final_batch.get("outcome")
        if outcome:
            print(f"사건: {outcome['event']} {outcome.get('item') or '-'} {outcome.get('reason') or ''}")


def run_final_mode(arguments):
    queue_path = project_path(arguments.queue)
    try:
        context = load_final_context(arguments, queue_path)
        journal = open_final_journal(project_path(arguments.journal), context, file_sha256(queue_path), arguments.resume)
        runner = FinalBatchRunner(arguments, context["queue"], journal, context)
    except (InputError, *FINAL_ERRORS) as error:
        print("입력 오류:", error)
        return 2
    outcome = journal["final_batch"].get("outcome")
    if outcome and outcome.get("event") in FINAL_EVENT_EXIT_CODES:
        runner.print_final_summary()
        return FINAL_EVENT_EXIT_CODES[outcome["event"]]
    runner.save()
    try:
        code = runner.start()
        if code is None:
            code = runner.run()
    finally:
        runner.finish()
    runner.print_final_summary()
    return code



def main():
    parser = argparse.ArgumentParser(description="FluidFX 보정 일괄 실행기(B8)")
    parser.add_argument("--queue", required=True)
    parser.add_argument("--holder", required=True)
    parser.add_argument("--journal", default=str(DEFAULT_JOURNAL))
    parser.add_argument("--overlay", default=str(DEFAULT_OVERLAY))
    parser.add_argument("--locks-dir", default=str(DEFAULT_LOCKS_DIR))
    parser.add_argument("--block", action="append", default=[])
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--revisit-deferred", action="store_true")
    arguments = parser.parse_args()
    if is_final_queue(arguments.queue):
        return run_final_mode(arguments)
    try:
        queue_path = project_path(arguments.queue)
        queue = load_queue(queue_path)
        unknown_blocks = sorted(set(arguments.block) - {block["id"] for block in queue["blocks"]})
        if unknown_blocks:
            raise InputError(f"큐에 없는 블록: {unknown_blocks}")
        journal = open_journal(project_path(arguments.journal), queue, file_sha256(queue_path), arguments.resume)
        runner = BatchRunner(arguments, queue, journal)
    except (InputError, OSError, ValueError, KeyError) as error:
        print("입력 오류:", error)
        return 2
    runner.save()
    start_error = runner.start()
    if start_error:
        print("시작 처리 실패:", start_error)
        return 3
    try:
        runner.run(arguments.block, arguments.revisit_deferred)
    finally:
        runner.finish()
    chosen = [item for item in journal["items"] if not arguments.block or item["block"] in arguments.block]
    counts = Counter(item["status"] for item in chosen)
    print("상태:", ", ".join(f"{status} {count}" for status, count in sorted(counts.items())))
    for item in chosen:
        print(f"  {item['block']} {Path(item['plan']).stem}: {item['status']} {item['reason'][:160]}")
    return 1 if counts.get("failed") else 0


if __name__ == "__main__":
    sys.exit(main())
