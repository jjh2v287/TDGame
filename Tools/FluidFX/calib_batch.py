"""실행 환경: 시스템 Python 3, 백그라운드 실행(스윕 1회가 600초를 넘는다). 에디터가 TDGame 을 열 수 있고 Saved/AgentOps/build_hold.json 보류를 얻을 수 있어야 한다. 하위 도구(calib_sweep·calib_judge·prereg_lock·fluid_tuning_patch·ue_editor·run_in_editor·build_and_test, 헤드리스 UnrealEditor-Cmd)는 모두 하위 프로세스로 부르고 정본은 쓰지 않는다(spec-cal0-v2 1.5 G-BATCH, 일지 C10).
실행 명령: python Tools/FluidFX/calib_batch.py --queue Tools/FluidFX/calib/batch_queue.json --holder <보류 이름> [--journal Saved/FluidFX/calib/batch/journal.json] [--overlay Saved/FluidFX/calib/accepted/overlay.json] [--locks-dir Saved/FluidFX/calib/locks] [--block K1]... [--resume] [--revisit-deferred] (PowerShell). 큐 = {"test_classes": {"path", "sha256"}, "blocks": [{"id", "plans": [{"plan": 프로젝트 상대 경로, "chain": 사슬 이름|null, "stages": ["screen"] 또는 ["screen", "confirm"], "estimate_seconds"?: {단계: 초}}]}]}(계획 경로는 큐 전체에서 한 번씩), 시험 분류표 = {"numeric_pinned": [헤드리스 시험 경로]}. 일지가 있으면 --resume 일 때만, 큐 sha256 이 같을 때만 이어 한다. --revisit-deferred 는 고른 블록의 deferred 항목 confirm 을 상한 없이 돌린다(K10)
출력: 일지(C10 원자적 다시 쓰기. runs = {단계: 실행 폴더, lock: 잠금, verify<n>: 사슬 재검증, chain_verify: 사슬 요약(사슬 마지막 항목), headless: 블록 헤드리스 폴더(블록 마지막 항목)}), 일지 폴더의 batch.log(시작 처리·명령·상태 한 줄씩), runs/<계획>/<단계>-<n>/ 와 <단계>-<n>.log, chains/<사슬>.json, blocks/<블록>-<n>/(materialize 사본·headless.log·headless.json). 종료 코드 0 고른 블록 처리 끝·failed 없음 / 1 failed 항목 있음 / 2 큐·일지·계획 입력 오류 / 3 시작 처리(보류 회수→ensure→t.OverrideFPS 0→정본 재적재→보류 획득) 실패
상태: 가짜 하위 프로세스 시험만(calib_offline_test T98·T99, 2026-10-30), 실제 에디터 배치 미실행. 하위 도구의 새 인수(--stage·--overlay·--lock·--screen-result·--screen-sweep·--verify-against·accept·overlay-remove --overlay·materialize 의 TDFluidPresetTuning.json·TDFluidMaterialLook.json 출력)는 명세 0.4 계약 기준이며 각 묶음 병합 전이라 미검증. 단계 2배 상한은 단계 실행 하나(하위 프로세스) 전체의 시간 제한으로 집행한다. 판정 종료 1 은 채택 없음, 재검증 판정 종료 0 통과·1 실패로 읽는다
"""
import argparse
import hashlib
import json
import math
import os
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
MATERIALIZED_TUNING = "TDFluidPresetTuning.json"
MATERIALIZED_LOOK = "TDFluidMaterialLook.json"
QUEUE_STAGE_SETS = (["screen"], ["screen", "confirm"])
DEFAULT_REAL_DT = 0.0255
SHOT_SECONDS = 0.35
STEP_OVERHEAD_SECONDS = 6.0
SCENE_OVERHEAD_SECONDS = 42.0
STEP_LIMIT_FACTOR = 2.0
BLOCK_LIMIT_FACTOR = 1.25
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
    os.replace(temporary, path)


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


def estimate_stage_seconds(plan, stage, real_dt_by_level):
    total = 0.0
    for scene in plan.get("scenes", []):
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
        self.numeric_pinned = set(read_json(project_path(queue["test_classes"]["path"])).get("numeric_pinned", []))

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
        return estimate_stage_seconds(self.plans[item["plan"]], stage, self.journal["real_dt"])

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
        while self.is_actionable(item, is_revisit):
            status = item["status"]
            if status in ("pending", "needs_regions"):
                self.prepare(item)
            elif status == "locked":
                self.screen(item)
            elif status in ("screened", "deferred"):
                self.confirm(item, status == "screened")
            else:
                self.accept(item)
            self.save()
            if status == "screened" and item["status"] == "deferred":
                return

    def run_stage(self, item, stage, extra_arguments, run_key=None):
        run_key = run_key or stage
        estimate = max(self.stage_estimate(item, stage), SCENE_OVERHEAD_SECONDS)
        failure = ""
        for attempt in (1, 2):
            run_dir, log_path = self.run_dir_and_log(item, run_key)
            item["runs"][run_key] = display_path(run_dir)
            item["attempts"] += 1
            self.refresh_hold()
            self.save()
            command = python_command(SWEEP_TOOL, "--plan", item["plan"], "--stage", stage, "--overlay", self.overlay_path, "--run-dir", run_dir, *extra_arguments)
            code, tail = self.item_tool(item, f"sweep {stage} {attempt}회", command, log_path, STEP_LIMIT_FACTOR * estimate)
            if code == 0:
                self.update_real_dt(item, run_dir)
                return run_dir, ""
            if code is None:
                return None, f"{stage} 실행이 추정 {estimate:.0f}초의 2배를 넘어 중단(L-editor-06 앞창 확인): {tail}"
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
            self.set_status(item, "no_adopt" if code == 1 else "failed", f"screen 판정 종료 {code}: {tail}")
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
        run_dir, failure = self.run_stage(item, "confirm", ["--lock", self.lock_path(item), "--screen-result", screen_dir / "screen.json"])
        if run_dir is None:
            self.set_status(item, "failed", failure)
            return
        code, tail, judge_path = self.judge(item, "confirm", run_dir, "judge.json", ["--screen-sweep", screen_dir / "sweep.json"])
        if code != 0 or not judge_path.exists():
            self.set_status(item, "no_adopt" if code == 1 else "failed", f"confirm 판정 종료 {code}: {tail}")
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
            return None
        confirm_judge = project_path(item["runs"]["confirm"]) / "judge.json"
        code, tail, _ = self.judge(item, run_key, run_dir, "verify.json", ["--verify-against", confirm_judge])
        if code in (0, 1):
            return code == 0
        item["reason"] = f"사슬 재검증 판정 종료 {code}: {tail}"
        return None

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
                outcomes[self.plan_name(earlier)] = self.verify_item(earlier, len(rounds) + 1)
                self.save()
                if outcomes[self.plan_name(earlier)] is None:
                    self.record(f"사슬 {chain} 재검증", "중단", earlier["reason"][:200])
                    return
            rounds.append(outcomes)
            failing = [name for name, passed in outcomes.items() if not passed]
            if not failing:
                break
            if not self.revert(adopted[-1], f"conflict: 사슬 {chain} 재검증 실패({', '.join(failing)}) → 가장 최근 채택 되돌림"):
                return
        summary_path = self.work_dir / "chains" / (chain.replace("/", "_").replace(".", "_") + ".json")
        write_json_atomic(summary_path, {"chain": chain, "rounds": rounds, "at": now_text()})
        last["runs"]["chain_verify"] = display_path(summary_path)
        self.save()

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
            write_json_atomic(output_dir / "headless.json", dict(verdict, verdict_code=verdict_code, overlay_sha256=file_sha256(self.overlay_path)))
            if verdict_code not in (0, 1):
                self.record(f"블록 {block_id} 헤드리스", f"불완전 {verdict_code}", "다음 --resume 에서 다시")
                return
            if verdict_code == 0:
                last["runs"]["headless"] = display_path(output_dir)
                self.record(f"블록 {block_id} 헤드리스", "통과", "숫자 고정 실패 기록만: " + ", ".join(verdict["allowlisted_failures"]))
                self.save()
                return
            if not self.revert(adopted[-1], f"블록 {block_id} 헤드리스 설계 단언 실패({', '.join(verdict['outside_allowlist'][:5])}) → 블록의 가장 최근 채택 되돌림"):
                return


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
