"""실행 환경: 시스템 Python 3(에디터 불필요, compare·replay 는 calib_judge 측정 경로의 numpy·Pillow·scipy 필요, git 필요, PowerShell — /Game 인수 때문에 Git Bash 금지).
실행 명령: python -B Tools/FluidFX/calib_final_verify.py w-expect|copies|test-owner|replay|expect|anchor|pin-journal [--user-decision <결정 가리킴> --e2-evidence <옛 일지>]|compare|check-ab|verdict|rollback|r2-spec ...(인수는 하위 명령 도움말, 명세 Saved/AgentOps/20261101/w-runbook/spec-final-verify.md)
출력: w-expect.json, 사본·copies.json, test_owner.json, replay 대조, expect.json·최종 모드 큐, anchor.json·Docs/Validation/FluidFXCalib/w-anchor.json(pin-journal 은 같은 파일에 journal_id·restart, e1 재시작이면 archive·e3_evidence, E2 뒤 재등록이면 user_decision·e2_evidence 를 더한다, 커밋은 부모), compare.json, check-ab 보고, verdict.json, W12 덧씌움 사본·preview-r2·r2 w-expect·rollback.json, r2 명세. 종료 코드 0 통과 / 1 판정 실패·되돌림·사용자 결정·미완·멈춤 사건(copies --check·replay 는 다름) / 2 입력 오류·닻·sha·스윕 관문(E2) / 3 결정론 관문(compare·check-ab)
상태: 2026-11-05 명세 개정 F 반영, 같은 날 개정 G(spec-patch-g: expect·w-expect·test-owner 관문 실패는 표준 오류에만 쓰고 --out 산출물을 덮지 않음, 잠금 단계 interrupted 는 E2 아님, r2 대상 covered 부모의 덮는 채택도 r2 대상), 2026-11-06 개정 H(spec-patch-h: e1 F0 실행 3개 미만이면 expect·anchor·verdict 종료 2, verdict 는 일지 journal_id 에 묶이지 않은 final_dir 잠금·일지가 모르는 실행 폴더·끝난 스윕의 interrupted 재추첨을 관문 실패로, events 의 E2 를 outcome 없이도 E2 로, f0_gate passed 거짓(기록 또는 다시 계산)이면 전 채택 미완), 개정 I(spec-patch-i: pin-journal, verdict 는 git HEAD w-anchor·이력에서 이 판·회차에 처음 고정된 journal_id = 이 일지·재시작 고정 순서·횟수 1·기록 폴더·옛 일지 id 를 관문 journal 로, f0_gate 가 없어도 F0 항목이 모두 끝났으면 산출물로 관문을 다시 계산해 결원·불일치면 전 채택 미완, 시도 끝 산출물 sha 를 관문 artifacts 로, anchor_commit_time 은 지금 닻을 처음 담은 커밋). 오프라인 하네스 calib_final_offline_test.py 와 실제 R5 산출물 읽기 전용 건식 실행(replay·expect), 개정 I git 함수는 스크래치 임시 저장소 실제 git 확인만 했고 실제 W 실행 전이다. 2026-11-06 개정 J(spec-patch-j): w_anchor_history 는 git log --all --full-history, 판·회차 고정 사슬은 첫 고정 + 앞 일지가 E2(사건 기록 E2 또는 일지 손상)로 끝난 뒤의 user_decision·e2_evidence 재등록만 받고 그 밖의 다른 journal_id 고정이 이력 어디에 있어도 관문 journal, verdict 는 F0 표본 + 끝나지 않은 F0 항목 < 3(결원 확정)이면 start_failed·E2·사건 없는 끊김이어도 전 채택 미완(E2 는 settled 빔). 하네스 개정 J 시험(가짜 git, 실제 임시 git 저장소 1건)만 했다. 2026-11-06 개정 K(spec-patch-k): 재등록 e2_evidence 는 일지로 읽혀야 한다(JSON·키 모양·NUL 없음, 아니면 pin-journal·verdict 거부), verdict 의 결원 확정 관문 실패는 끝난 일지(outcome start_failed·E2 또는 판정 도구 실패 E2 기록)에만 쓰고 사건 없이 끊긴 일지는 끝 상태 아님 2. 하네스 개정 K 시험(가짜 git)만 했다.
"""
import argparse
import copy
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))
import calib_judge  # noqa: E402
import fluid_material_look  # noqa: E402
import fluid_tuning_patch  # noqa: E402
import prereg_lock  # noqa: E402

ROOT = prereg_lock.ROOT
TOOL_NAME = "calib_final_verify"
TOOL_VERSION = 1
SPEC_VERSION = 4
EXPECT_VERSION = 4
TOOL_PATH = "Tools/FluidFX/calib_final_verify.py"
BATCH_PATH = "Tools/FluidFX/calib_batch.py"
PATCH_TOOL_PATH = "Tools/FluidFX/fluid_tuning_patch.py"
TEST_CLASSES_PATH = "Tools/FluidFX/calib/test_classes.json"
RULE_DOCUMENT_DIR = "Saved/AgentOps/20261101/w-runbook"
RULE_DOCUMENTS = tuple(f"{RULE_DOCUMENT_DIR}/{name}" for name in (
    "runbook-v3.md", "runbook-v3-amend.md", "runbook-v3-amend-b.md", "runbook-v3-amend-c.md", "runbook-v3-amend-d.md", "runbook-v3-amend-e.md", "runbook-v3-amend-f.md", "spec-final-verify.md"))
CANONICAL_BEFORE_SHA256 = {"tuning": "39b6c6e0fab13cf30700ec157d11969cf5372440d9e905fb0151d764befd3774", "look": "aadc10d9156173089ee7fdc723d8fba83ebfebfa491067618095471402eb110b"}
FX9_REMOVED_PATH = "Look:Materials./Game/FluidFX/Materials/MI_TD_FluidHeterogeneousVolume_Smoke.Scalars.VolExtrudeHeightCm"
MEASURE_TOOL_FILES = ("Tools/FluidFX/pie_lookdev_fluid.py", "Tools/FluidFX/measure_fx_look.py", "Tools/FluidFX/pie_probe_fluid_water.py")
DEFAULT_W_ANCHOR = "Docs/Validation/FluidFXCalib/w-anchor.json"
W_ANCHOR_BASE_KEYS = ("anchor", "edition", "sha256")
W_ANCHOR_PIN_KEYS = ("journal_id", "restart")
W_ANCHOR_RESTART_KEYS = ("archive", "e3_evidence")
W_ANCHOR_REDO_KEYS = ("user_decision", "e2_evidence")
JOURNAL_ID_PATTERN = re.compile(r"^[0-9a-f]{32}$")
USER_DECISION_PATTERN = re.compile(r"D-[0-9A-Za-z]+|\d{4}-\d{2}-\d{2} \d{2}:\d{2}")
FOG_GENERATOR = "Tools/FluidFX/editor_make_fluid_fog_stage.py"
FOG_STAGE_REPORT = "Saved/FluidFX/fluid_fog_stage.json"
FOG_LIGHT_LABEL = "LabGreenLight"
FOG_LIGHT_BEFORE = 4.0
FOG_LIGHT_ADOPTED = 8.0
HEADER_SYNC_TEST = "TDGame.FluidFX.PresetData.StructDefaultsMatchHeader"
F0_REQUIRED_SELF_CHECKS = 3
EDITIONS = ("e1", "r2")
EDITION_PHASES = {"e1": "r1", "r2": "r2"}
PHASE_EDITIONS = {"w11": "e1", "w13": "e1", "r2": "r2", "r2-w13": "r2"}
TESTS_REQUIRED_KEYS = {"e1": ("w11", "w13"), "r2": ("r2", "r2-w13")}
TEST_FAMILY_PHASES = {"w11": None, "w13": ("w13", "w11"), "r2": None, "r2-w13": ("r2-w13", "r2")}
BLOCK_ORDER = ("F0", "F1", "F2")
VERIFY_STAGES = ["verify"]
AB_STAGES = ["screen", "confirm"]
OPPORTUNITIES = ("first", "rerun")
AB_TRIGGERS = ("always_record", "on_fail")
STATE_SEVERITY = {"pass": 0, "fail": 1, "undetermined": 2, "incomplete": 3}
STOP_EVENTS = ("E3", "E3_repeat", "F0_shortfall", "start_failed")
VERIFY_TERMINAL = ("verified", "incomplete")
AB_TERMINAL = ("ab_done", "ab_skipped")
JUDGE_STEPS = ("official", "compare", "judge", "check_ab")
ARTIFACT_OUTPUT_COMMANDS = ("w-expect", "test-owner", "expect")
LOCK_CREATE_STEPS = ("lock_create", "judge_lock_create")
LOCK_VERIFY_STEPS = ("lock_verify", "judge_lock_verify")
SCENE_PATH_KEYS = ("measure", "sources")
WORK_PLACEHOLDER = "<work>"
CHAIN_SUSPECT_ADOPTION = "4"
TEST_MACRO_PATTERN = re.compile(r"(?:IMPLEMENT_SIMPLE_AUTOMATION_TEST|IMPLEMENT_COMPLEX_AUTOMATION_TEST|BEGIN_DEFINE_SPEC)\s*\(\s*\w+\s*,\s*\"([^\"]+)\"")
TEST_NAME_PREFIX = "TDGame.FluidFX"
TEST_FILE_SUFFIXES = (".cpp", ".h")
RUN_FOLDER_PATTERN = re.compile(r"^(verify|screen|confirm)-\d+$")
FOG_LIGHT_VALUE_PATTERN = re.compile(r"\"volumetric_scattering_intensity\":\s*([-+0-9.eE]+)")
LOOK_NIAGARA_TABLE = fluid_tuning_patch.LOOK_PREFIX + "NiagaraSystems"
EXIT_OK = 0
EXIT_FAILED = 1
EXIT_INPUT = 2
EXIT_DETERMINISM = 3
INPUT_ERRORS = (OSError, ValueError, KeyError, TypeError, prereg_lock.LockError, calib_judge.JudgeError, fluid_tuning_patch.PatchError, fluid_tuning_patch.InputRejected)
MISSING = object()


class GateFailure(Exception):
    def __init__(self, gate, problems, exit_code=EXIT_INPUT):
        super().__init__("; ".join(problems))
        self.gate = gate
        self.problems = list(problems)
        self.exit_code = exit_code


def require(condition, gate, message, exit_code=EXIT_INPUT):
    if not condition:
        raise GateFailure(gate, [message], exit_code)


def require_none(problems, gate, exit_code=EXIT_INPUT):
    if problems:
        raise GateFailure(gate, problems, exit_code)


def json_text(value):
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def canonical_text(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))


def project_path(text):
    return prereg_lock.project_path(str(text))


def normalized(text):
    return prereg_lock.relative_text(project_path(text))


def is_file(text):
    return project_path(text).is_file()


def load_json(text):
    return json.loads(project_path(text).read_text(encoding="utf-8"))


def load_json_or_none(text):
    return load_json(text) if text and is_file(text) else None


def sha_of(text):
    return prereg_lock.file_sha256(project_path(text))


def sha_or_none(text):
    return sha_of(text) if is_file(text) else None


def bytes_sha(data):
    return hashlib.sha256(data).hexdigest()


def crlf_sha(text):
    data = project_path(text).read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
    return bytes_sha(data)


def write_bytes(text, data):
    path = project_path(text)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def write_json(text, value):
    write_bytes(text, json_text(value).encode("utf-8"))


def require_saved_output(text):
    fluid_tuning_patch.require_under_saved(project_path(text), ROOT)
    return normalized(text)


def now_text():
    return datetime.now().astimezone().isoformat()


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def same_value(first, second):
    if is_number(first) and is_number(second):
        return float(first) == float(second)
    return canonical_text(first) == canonical_text(second)


def adoption_sort_key(text):
    return (0, int(text), text) if str(text).isdigit() else (1, 0, str(text))


def sorted_adoptions(values):
    return sorted(values, key=adoption_sort_key)


def parent_text(text):
    return str(PurePosixPath(text).parent)


def file_mtime(text):
    return datetime.fromtimestamp(project_path(text).stat().st_mtime).astimezone()


def is_look_path(json_path):
    return fluid_tuning_patch.is_look_path(json_path)


def entry_id(entry):
    if "json_path" in entry:
        return entry["json_path"]
    return fluid_tuning_patch.describe_actor_prop(entry)


def adoption_entry_id(adoption):
    if "json_path" in adoption:
        return adoption["json_path"]
    return fluid_tuning_patch.describe_actor_prop(adoption["actor_prop"])


def owner_key(adoption):
    if "actor_prop" in adoption:
        actor_prop = adoption["actor_prop"]
        return f"actor_prop:{actor_prop.get('level')}/{actor_prop.get('label')}"
    json_path = adoption["json_path"]
    if is_look_path(json_path):
        table_key, package, _, _ = fluid_tuning_patch.parse_look_path(json_path)
        return f"{table_key}.{package}"
    section, name, struct_name, _ = fluid_tuning_patch.parse_json_path(json_path)
    if section == fluid_tuning_patch.GLOBALS_SECTION:
        return f"{section}.{struct_name}"
    return f"{section}.{name}"


def overlay_entry_map(overlay):
    entries = {}
    for entry in overlay["assignments"] + overlay["actor_props"]:
        identifier = entry_id(entry)
        if identifier in entries:
            raise ValueError(f"덧씌움에 같은 항목이 둘이다: {identifier}")
        entries[identifier] = entry
    return entries


def read_overlay(text):
    return fluid_tuning_patch.read_overlay(project_path(text))


def look_value(look, json_path):
    table_key, package, section, name = fluid_tuning_patch.parse_look_path(json_path)
    table = look.get(table_key[len(fluid_tuning_patch.LOOK_PREFIX):]) or {}
    return ((table.get(package) or {}).get(section) or {}).get(name, MISSING)


def tuning_value(tuning, json_path):
    section, kind_name, struct_name, field_name = fluid_tuning_patch.parse_json_path(json_path)
    if section == fluid_tuning_patch.GLOBALS_SECTION:
        return tuning[section][struct_name][field_name]
    if section == fluid_tuning_patch.KINDS_SECTION:
        return fluid_tuning_patch.effective_value(tuning, kind_name, struct_name, field_name)
    raise ValueError(f"{json_path}: Kinds·Globals·Look 경로만 값을 읽는다")


def document_value(pair, json_path):
    if is_look_path(json_path):
        return look_value(pair["look"], json_path)
    return tuning_value(pair["tuning"], json_path)


def read_canonical_pair(directory):
    folder = normalized(directory)
    tuning_text = f"{folder}/{fluid_tuning_patch.TUNING_FILE_NAME}"
    look_text = f"{folder}/{fluid_tuning_patch.LOOK_FILE_NAME}"
    return {"dir": folder, "tuning_path": tuning_text, "look_path": look_text, "tuning": load_json(tuning_text), "look": load_json(look_text),
            "tuning_sha256": sha_of(tuning_text), "look_sha256": sha_of(look_text)}


def final_directory(spec):
    return normalized(spec["final_dir"])


def derived_paths(final_dir, edition):
    return {"spec": f"{final_dir}/final-spec.json", "copies": f"{final_dir}/copies.json", "expect": f"{final_dir}/expect.json", "queue": f"{final_dir}/final_queue.json",
            "anchor": f"{final_dir}/anchor.json", "journal": f"{final_dir}/batch-{edition}/journal.json", "overlay_empty": f"{final_dir}/overlay-empty.json",
            "overlay_actor_props": f"{final_dir}/overlay-actor-props.json"}


def run_lock_path(final_dir, identifier, edition):
    return f"{final_dir}/locks/{identifier}-{edition}-prereg-lock.json"


def judge_lock_path(final_dir, run_id, edition):
    return f"{final_dir}/locks/{run_id}-judge-{edition}-prereg-lock.json"


def judge_file_paths(final_dir, overlay_text):
    return [TOOL_PATH, f"{final_dir}/expect.json", BATCH_PATH, TEST_CLASSES_PATH, normalized(overlay_text), f"{final_dir}/final-spec.json"]


def archive_directories(final_dir):
    folder = project_path(final_dir)
    return sorted(normalized(path) for path in folder.glob("aborted-*") if path.is_dir()) if folder.is_dir() else []


def anchor_commit_problems(path_text):
    relative = normalized(path_text)
    listed = subprocess.run(["git", "ls-files", "--error-unmatch", "--", relative], cwd=ROOT, capture_output=True, text=True)
    if listed.returncode != 0:
        return [f"w-anchor {relative} 이 git 에 추적되지 않는다"]
    clean = subprocess.run(["git", "diff", "--quiet", "HEAD", "--", relative], cwd=ROOT, capture_output=True, text=True)
    return [] if clean.returncode == 0 else [f"w-anchor {relative} 에 커밋되지 않은 변경이 있다"]


def w_anchor_history(path_text):
    relative = normalized(path_text)
    listed = subprocess.run(["git", "log", "--all", "--full-history", "--reverse", "--topo-order", "--format=%H %cI", "--", relative], cwd=ROOT, capture_output=True, text=True)
    history = []
    for line in listed.stdout.splitlines():
        commit, _, committed_at = line.strip().partition(" ")
        shown = subprocess.run(["git", "show", f"{commit}:{relative}"], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if shown.returncode != 0:
            continue
        try:
            history.append({"committed_at": committed_at, "content": json.loads(shown.stdout)})
        except ValueError:
            continue
    return history


def is_same_anchor(content, current):
    return isinstance(content, dict) and isinstance(current, dict) and all(content.get(key) == current.get(key) for key in W_ANCHOR_BASE_KEYS)


def anchor_commit_time(path_text):
    current = load_json(path_text)
    for entry in w_anchor_history(path_text):
        if is_same_anchor(entry.get("content"), current):
            return prereg_lock.parse_moment(entry["committed_at"])
    raise ValueError(f"w-anchor {path_text} 의 지금 닻(anchor·edition·sha256)을 담은 커밋을 git 이력에서 찾지 못했다")


def w_anchor_key_problems(w_anchor):
    if not isinstance(w_anchor, dict):
        return ["w-anchor 가 JSON 객체가 아니다"]
    keys = set(w_anchor)
    if keys == set(W_ANCHOR_BASE_KEYS):
        return []
    if is_redo_pin(w_anchor):
        keys -= set(W_ANCHOR_REDO_KEYS)
    pin, restart_pin = set(W_ANCHOR_BASE_KEYS + W_ANCHOR_PIN_KEYS), set(W_ANCHOR_BASE_KEYS + W_ANCHOR_PIN_KEYS + W_ANCHOR_RESTART_KEYS)
    is_pin = keys == pin and w_anchor.get("restart") == 0
    is_restart_pin = keys == restart_pin and w_anchor.get("restart") == 1 and w_anchor.get("edition") == "e1"
    if (is_pin or is_restart_pin) and isinstance(w_anchor.get("journal_id"), str) and JOURNAL_ID_PATTERN.match(w_anchor["journal_id"]):
        return []
    return ["w-anchor 키가 anchor·edition·sha256(pin-journal 뒤에는 journal_id·restart, e1 재시작이면 archive·e3_evidence, E2 뒤 재등록이면 user_decision·e2_evidence 까지)이 아니다"]


def is_redo_pin(content):
    if not isinstance(content, dict) or not all(isinstance(content.get(key), str) and content[key].strip() for key in W_ANCHOR_REDO_KEYS):
        return False
    return bool(USER_DECISION_PATTERN.search(content["user_decision"]))


def pin_round(content):
    if not isinstance(content, dict) or "journal_id" not in content or w_anchor_key_problems(content):
        return None
    return content["edition"], content["restart"]


def round_registrations(pins, round_key):
    registrations = []
    for entry in pins:
        content = entry["content"]
        if pin_round(content) == round_key and content["journal_id"] not in {registration["content"]["journal_id"] for registration in registrations}:
            registrations.append(entry)
    return registrations


def stopped_journal_or_none(evidence_text):
    data = project_path(evidence_text).read_bytes()
    try:
        journal = None if b"\x00" in data else json.loads(data.decode("utf-8"))
    except ValueError:
        return None
    final_batch = journal.get("final_batch") if isinstance(journal, dict) else None
    is_valid = (isinstance(final_batch, dict) and isinstance(journal.get("items"), list) and isinstance(final_batch.get("journal_id"), str)
                and isinstance(final_batch.get("events"), list) and isinstance(final_batch.get("starts"), list))
    return journal if is_valid else None


def e2_event_moments(journal):
    final_batch = journal["final_batch"]
    events = [final_batch.get("outcome"), *final_batch["events"]]
    return [event.get("at") for event in events if isinstance(event, dict) and event.get("event") == "E2"]


def stopped_journal_problems(evidence_text, previous_id, round_key, committed_at):
    label = f"재등록 e2_evidence {evidence_text}"
    if not is_file(evidence_text):
        return [f"{label} 이 없다(앞 일지가 E2 로 끝났음을 산출물로 보일 수 없다, 개정 J)"]
    old = stopped_journal_or_none(evidence_text)
    if old is None:
        return [f"{label} 을 일지로 읽지 못했다(JSON·키 모양·NUL — 재등록 증거는 읽히는 일지여야 한다, 깨진 일지는 위협 모형 밖이라 W 를 멈춘다, 개정 K4)"]
    final_batch = old["final_batch"]
    problems = []
    if final_batch["journal_id"] != previous_id or (final_batch.get("edition"), final_batch.get("restart")) != round_key:
        problems.append(f"{label} 의 journal_id·판·회차 ≠ git 이력에서 바로 앞에 고정된 {previous_id}·{round_key}(개정 J)")
    if all(isinstance(item, dict) and item.get("status") in VERIFY_TERMINAL + AB_TERMINAL for item in old["items"]):
        problems.append(f"{label} 의 모든 항목이 끝 상태다(측정이 끝난 뒤 덧붙은 E2 는 W 중단이 아니다, 개정 J)")
    moments = e2_event_moments(old)
    if not moments:
        return problems + [f"{label} 이 E2 로 끝나지 않았다(사건 기록에 E2 없음 — E2 아닌 결과 뒤 새 일지 = 다시 추첨, 개정 J)"]
    known = [prereg_lock.parse_moment(moment) for moment in moments if moment]
    if committed_at and known and min(known) > prereg_lock.parse_moment(committed_at):
        problems.append(f"{label} 의 E2 시각 {min(known).isoformat()} > 재등록 고정 커밋 시각 {committed_at}(앞 일지 E2 → 사용자 결정 → 새 고정 순서, 개정 J)")
    return problems


def registration_problems(registrations, round_key):
    problems = []
    for index, entry in enumerate(registrations):
        content = entry["content"]
        if index == 0:
            if is_redo_pin(content):
                problems.append(f"git 이력에서 판·회차 {round_key} 에 처음 고정된 journal_id {content['journal_id']} 가 재등록(user_decision·e2_evidence) 모양이다(앞 고정이 이력에 없다, 개정 J)")
            continue
        if not is_redo_pin(content):
            problems.append(f"git 이력에서 판·회차 {round_key} 에 처음 고정된 journal_id {registrations[0]['content']['journal_id']} 뒤에 사용자 결정(user_decision)·E2 증거(e2_evidence) "
                            f"없는 다른 journal_id {content['journal_id']} 고정이 있다(결과 뒤 새 일지 = 다시 추첨, 개정 I·J)")
            continue
        problems += stopped_journal_problems(normalized(content["e2_evidence"]), registrations[index - 1]["content"]["journal_id"], round_key, entry.get("committed_at"))
    return problems


def restart_pin_problems(pins, final_dir):
    restart_indexes = [index for index, entry in enumerate(pins) if pin_round(entry["content"]) == ("e1", 1)]
    if not restart_indexes:
        return []
    first = pins[restart_indexes[0]]["content"]
    problems = []
    if len([entry for entry in round_registrations(pins, ("e1", 1)) if not is_redo_pin(entry["content"])]) > 1:
        problems.append("git 이력에 e1 재시작 일지 고정이 2개 이상이다(W당 재시작 1회, E2 뒤 사용자 결정 재등록은 빼고 센다, 개정 I·J)")
    earlier = round_registrations(pins[:restart_indexes[0]], ("e1", 0))
    if not earlier:
        problems.append("git 이력에서 e1 재시작 일지 고정 앞에 e1 restart 0 일지 고정이 없다(순서, 개정 I)")
    problems += registration_problems(earlier, ("e1", 0))
    archives = archive_directories(final_dir)
    if len(archives) != 1 or normalized(first.get("archive") or "-") != archives[0]:
        problems.append(f"재시작 고정의 archive {first.get('archive')} ≠ 지금 기록 폴더 {archives}(개정 I)")
        return problems
    evidence = normalized(first.get("e3_evidence") or "-")
    if evidence != f"{archives[0]}/batch-e1/journal.json":
        return problems + [f"재시작 고정의 e3_evidence {first.get('e3_evidence')} ≠ {archives[0]}/batch-e1/journal.json(개정 I)"]
    old_id = ((load_json_or_none(evidence) or {}).get("final_batch") or {}).get("journal_id")
    if earlier and old_id != earlier[-1]["content"]["journal_id"]:
        problems.append("기록 폴더 옛 일지 journal_id ≠ git 이력에서 재시작 고정 앞에 마지막으로 등록된 e1 restart 0 journal_id(옛 일지를 바꿔 넣음, 개정 I·J)")
    return problems


def journal_pin_problems(w_anchor, history, journal, final_dir):
    final_batch = journal.get("final_batch") or {}
    journal_id, round_key = final_batch.get("journal_id"), (final_batch.get("edition"), final_batch.get("restart"))
    if not isinstance(journal_id, str) or not JOURNAL_ID_PATTERN.match(journal_id):
        return ["일지 final_batch.journal_id 가 32자리 16진이 아니다(개정 I)"]
    problems = []
    if w_anchor.get("journal_id") != journal_id or pin_round(w_anchor) != round_key:
        problems.append(f"HEAD w-anchor 에 이 일지의 journal_id·판·회차 {round_key} 고정이 없다(측정 전 pin-journal 뒤 커밋, 개정 I)")
    pins = [entry for entry in history if pin_round(entry.get("content")) is not None]
    registrations = round_registrations(pins, round_key)
    if journal_id not in {entry["content"]["journal_id"] for entry in registrations}:
        problems.append(f"git 이력에 판·회차 {round_key} 의 이 일지 journal_id 고정이 없다(개정 I)")
    problems += registration_problems(registrations, round_key)
    if round_key == ("e1", 1):
        problems += restart_pin_problems(pins, final_dir)
    return problems


def pinned_w_anchor(w_anchor, journal, final_dir, redo=None):
    final_batch = journal.get("final_batch") or {}
    pinned = {key: w_anchor[key] for key in W_ANCHOR_BASE_KEYS}
    pinned.update(journal_id=final_batch.get("journal_id"), restart=final_batch.get("restart"))
    if final_batch.get("restart") == 1:
        archives = archive_directories(final_dir)
        archive = archives[0] if len(archives) == 1 else None
        pinned.update(archive=archive, e3_evidence=f"{archive}/batch-e1/journal.json" if archive else None)
    if redo:
        pinned.update(redo)
    return pinned


def check_anchor(w_anchor_text, edition, required_paths):
    if not is_file(w_anchor_text):
        return [f"w-anchor 없음: {w_anchor_text}"]
    try:
        w_anchor = load_json(w_anchor_text)
    except ValueError as error:
        return [f"w-anchor 형식 오류: {error}"]
    key_problems = w_anchor_key_problems(w_anchor)
    if key_problems:
        return key_problems
    problems = list(anchor_commit_problems(w_anchor_text))
    if w_anchor["edition"] != edition:
        problems.append(f"w-anchor edition {w_anchor['edition']} ≠ 기대 판 {edition}")
    anchor_text = w_anchor["anchor"]
    if not is_file(anchor_text):
        return problems + [f"닻 없음: {anchor_text}"]
    if sha_of(anchor_text) != w_anchor["sha256"]:
        problems.append("anchor.json sha ≠ w-anchor sha256")
    anchor = load_json(anchor_text)
    if anchor.get("edition") != edition:
        problems.append(f"anchor.json edition {anchor.get('edition')} ≠ 기대 판 {edition}")
    files = anchor.get("files") or {}
    for path, recorded in sorted(files.items()):
        if not is_file(path):
            problems.append(f"닻 파일 없음: {path}")
        elif sha_of(path) != recorded:
            problems.append(f"닻 파일 sha 다름: {path}")
    problems.extend(f"닻에 필요한 파일이 없다: {normalized(path)}" for path in required_paths if normalized(path) not in files)
    return problems


def matching_knob_params(plan, adoption):
    params = plan.get("params") or []
    if "json_path" in adoption:
        json_path = adoption["json_path"]
        return [param for param in params if param.get("json_path") == json_path or json_path in (param.get("json_paths") or [])]
    target = adoption["actor_prop"]
    return [param for param in params if isinstance(param.get("actor_prop"), dict)
            and all(param["actor_prop"].get(key) == target.get(key) for key in ("label", "property", "component"))
            and target.get("level") in fluid_tuning_patch.plan_levels(plan, param.get("scenes"))]


def single_knob_param(plan, adoption, plan_text):
    params = matching_knob_params(plan, adoption)
    require(len(params) == 1, "copies", f"{plan_text}: 채택 {adoption_entry_id(adoption)} 에 맞는 손잡이가 {len(params)}개다(정확히 1개여야 함)")
    return params[0]


def removed_base_paths(original, copied):
    if canonical_text({key: value for key, value in original.items() if key != "params"}) != canonical_text({key: value for key, value in copied.items() if key != "params"}):
        raise ValueError("계획 사본이 params 밖에서 원본과 다르다")
    original_params, copied_params = original.get("params") or [], copied.get("params") or []
    if len(original_params) != len(copied_params):
        raise ValueError("계획 사본의 params 수가 원본과 다르다")
    removed = []
    for original_param, copied_param in zip(original_params, copied_params):
        if canonical_text(original_param) == canonical_text(copied_param):
            continue
        without_base = {key: value for key, value in original_param.items() if key != "base"}
        if "base" in original_param and is_look_path(original_param.get("json_path")) and canonical_text(without_base) == canonical_text(copied_param):
            removed.append(original_param["json_path"])
            continue
        raise ValueError(f"계획 사본 param {original_param.get('json_path')} 이 base 제거 밖에서 원본과 다르다")
    return removed


def plan_copy_document(plan, preview_look, plan_text):
    require("draft_status" not in plan, "copies", f"{plan_text}: draft_status 가 있는 계획은 사본을 만들지 않는다")
    copied = copy.deepcopy(plan)
    removed = []
    for param in copied.get("params") or []:
        json_path = param.get("json_path")
        if "base" not in param or not is_look_path(json_path):
            continue
        current = look_value(preview_look, json_path)
        if current is MISSING or same_value(param["base"], current):
            continue
        del param["base"]
        removed.append(json_path)
    require(removed, "copies", f"{plan_text}: 지울 base 가 없다(계획 사본이 필요 없음)")
    require(removed_base_paths(plan, copied) == removed, "copies", f"{plan_text}: 원본 ≠ 사본 + 지운 키")
    return copied


def before_value_of(adoption, entry, knob, before):
    if "actor_prop" in adoption:
        require("base" in knob, "copies", f"{adoption_entry_id(adoption)}: actor_prop 손잡이에 base 가 없다")
        return knob["base"]
    json_path = adoption["json_path"]
    if not is_look_path(json_path):
        return tuning_value(before["tuning"], json_path)
    require("base" in entry, "copies", f"{json_path}: 동결본 Look 항목에 base 가 없다")
    current = look_value(before["look"], json_path)
    require(current is MISSING or same_value(current, entry["base"]), "copies", f"{json_path}: 동결본 base {entry['base']} ≠ canonical-before 값 {current}")
    return entry["base"]


def wab_copy_document(spec, ab, entries, before, preview):
    adoption = spec["adoptions"][ab["for_adoption"]]
    entry = entries[adoption_entry_id(adoption)]
    scene_plan_text, knob_plan_text = normalized(ab["scene_plan"]), normalized(ab["knob_plan"])
    scene_plan, knob_plan = load_json(scene_plan_text), load_json(knob_plan_text)
    knob = single_knob_param(knob_plan, adoption, knob_plan_text)
    before_value = before_value_of(adoption, entry, knob, before)
    require(not same_value(before_value, entry["value"]), "copies", f"{ab['id']}: 채택 전 값 {before_value} = 채택값 {entry['value']}")
    param = copy.deepcopy(knob)
    param["values"] = [before_value]
    if "actor_prop" in adoption:
        param["base"] = entry["value"]
    elif is_look_path(adoption["json_path"]) and look_value(preview["look"], adoption["json_path"]) is not MISSING:
        param.pop("base", None)
    if scene_plan_text != knob_plan_text:
        param.pop("scenes", None)
    copied = copy.deepcopy(scene_plan)
    copied["params"] = [param]
    copied["id"] = PurePosixPath(normalized(ab["plan"])).stem
    inputs = {scene_plan_text: sha_of(scene_plan_text), knob_plan_text: sha_of(knob_plan_text), normalized(spec["overlay"]): sha_of(spec["overlay"]),
              before["tuning_path"]: before["tuning_sha256"], before["look_path"]: before["look_sha256"], preview["look_path"]: preview["look_sha256"]}
    return copied, inputs


def build_copies(spec_text):
    spec = load_json(spec_text)
    final_dir = final_directory(spec)
    paths = derived_paths(final_dir, spec.get("edition"))
    overlay_text = normalized(spec["overlay"])
    overlay = read_overlay(overlay_text)
    entries = overlay_entry_map(overlay)
    before = read_canonical_pair(spec["canonical_before_dir"])
    preview = read_canonical_pair(spec["preview_dir"])
    outputs = {}
    empty = {"actor_props": [], "assignments": [], "history": [], "version": fluid_tuning_patch.OVERLAY_VERSION}
    outputs[paths["overlay_empty"]] = (json_text(empty).encode("utf-8"), {})
    actor_props = dict(empty, actor_props=copy.deepcopy(overlay["actor_props"]))
    outputs[paths["overlay_actor_props"]] = (json_text(actor_props).encode("utf-8"), {overlay_text: sha_of(overlay_text)})
    for plan_copy in spec.get("plan_copies") or []:
        plan_text, out_text = normalized(plan_copy["plan"]), require_saved_output(plan_copy["out"])
        copied = plan_copy_document(load_json(plan_text), preview["look"], plan_text)
        outputs[out_text] = (json_text(copied).encode("utf-8"), {plan_text: sha_of(plan_text), preview["look_path"]: preview["look_sha256"]})
    for ab in spec.get("ab") or []:
        out_text = require_saved_output(ab["plan"])
        copied, inputs = wab_copy_document(spec, ab, entries, before, preview)
        outputs[out_text] = (json_text(copied).encode("utf-8"), inputs)
    require(len(outputs) == 2 + len(spec.get("plan_copies") or []) + len(spec.get("ab") or []), "copies", "사본 출력 경로가 겹친다")
    document = {"tool": TOOL_NAME, "version": TOOL_VERSION, "spec_sha256": sha_of(spec_text),
                "outputs": [{"path": path, "sha256": bytes_sha(data), "inputs": inputs} for path, (data, inputs) in sorted(outputs.items())]}
    result = {path: data for path, (data, _) in outputs.items()}
    result[paths["copies"]] = json_text(document).encode("utf-8")
    return result


def check_copies_paths(spec_text, out_dir_text):
    spec = load_json(spec_text)
    final_dir = final_directory(spec)
    require(normalized(out_dir_text) == final_dir, "input", f"--out-dir {normalized(out_dir_text)} ≠ 명세 final_dir {final_dir}")
    require(normalized(spec_text) == f"{final_dir}/final-spec.json", "input", f"--spec 은 {final_dir}/final-spec.json 이어야 한다")
    require_saved_output(final_dir)


def copies_check_problems(spec_text):
    problems = []
    for path, data in sorted(build_copies(spec_text).items()):
        if not is_file(path):
            problems.append(f"사본 없음: {path}")
        elif project_path(path).read_bytes() != data:
            problems.append(f"사본 바이트 다름: {path}")
    return problems


def command_copies(arguments):
    check_copies_paths(arguments.spec, arguments.out_dir)
    if arguments.check:
        problems = copies_check_problems(arguments.spec)
        for line in problems:
            print("다름:", line, file=sys.stderr)
        print("copies 같음" if not problems else f"copies 다름 {len(problems)}")
        return EXIT_FAILED if problems else EXIT_OK
    outputs = build_copies(arguments.spec)
    for path, data in sorted(outputs.items()):
        write_bytes(path, data)
    print(f"copies {len(outputs)}개")
    return EXIT_OK


def build_w_expect(overlay_text, before_dir, preview_dir, builder_before_text):
    overlay = read_overlay(overlay_text)
    before = read_canonical_pair(before_dir)
    preview = read_canonical_pair(preview_dir)
    builder = load_json(builder_before_text)
    paths = sorted(entry["json_path"] for entry in overlay["assignments"])
    measured = [path for entry in overlay["assignments"] for path in entry.get("measured_under_base") or []]
    packages = sorted({fluid_tuning_patch.parse_look_path(path)[1] for path in paths + measured if is_look_path(path) and fluid_tuning_patch.parse_look_path(path)[0] == LOOK_NIAGARA_TABLE})
    ns_expect = {package: {"Floats": copy.deepcopy((preview["look"]["NiagaraSystems"].get(package) or {}).get("Floats") or {})} for package in packages}
    builder_diff, effective_diff = [], []
    for json_path in paths:
        if is_look_path(json_path):
            continue
        section, kind_name, struct_name, field_name = fluid_tuning_patch.parse_json_path(json_path)
        if section not in (fluid_tuning_patch.KINDS_SECTION, fluid_tuning_patch.GLOBALS_SECTION):
            continue
        before_value, after_value = tuning_value(before["tuning"], json_path), tuning_value(preview["tuning"], json_path)
        if section == fluid_tuning_patch.KINDS_SECTION and field_name in ((builder.get(kind_name) or {}).get(struct_name) or {}):
            builder_diff.append({"json_path": json_path, "kind": kind_name, "struct": struct_name, "field": field_name, "before": before_value, "after": after_value})
        if not same_value(before_value, after_value):
            effective_diff.append({"json_path": json_path, "before": before_value, "after": after_value})
    return {"tool": TOOL_NAME, "version": TOOL_VERSION,
            "inputs": {"overlay": normalized(overlay_text), "overlay_sha256": sha_of(overlay_text), "before": {"tuning": before["tuning_sha256"], "look": before["look_sha256"]},
                       "preview": {"tuning": preview["tuning_sha256"], "look": preview["look_sha256"]}, "builder_before": normalized(builder_before_text),
                       "builder_before_sha256": sha_of(builder_before_text)},
            "written": {"tuning": preview["tuning_sha256"], "look": preview["look_sha256"]}, "ns_expect": ns_expect, "builder_diff": builder_diff,
            "effective_diff": effective_diff, "look_leaf_diff": [path for path in paths if is_look_path(path)]}


def command_w_expect(arguments):
    out_text = require_saved_output(arguments.out)
    document = build_w_expect(arguments.overlay, arguments.before_dir, arguments.preview_dir, arguments.builder_before)
    write_json(out_text, document)
    print(f"w-expect builder_diff {len(document['builder_diff'])} effective_diff {len(document['effective_diff'])} look_leaf_diff {len(document['look_leaf_diff'])}")
    return EXIT_OK


def collect_tests(source_root):
    tests_dir = project_path(source_root) / "Tests"
    tests = {}
    for path in sorted(tests_dir.rglob("*")):
        if not path.is_file() or path.suffix not in TEST_FILE_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in TEST_MACRO_PATTERN.finditer(text):
            name = match.group(1)
            if name.startswith(TEST_NAME_PREFIX):
                tests[name] = {"file": normalized(path), "text": text, "line": text.count("\n", 0, match.start(1)) + 1}
    return tests


def adoption_tokens(entry):
    if "json_path" not in entry:
        return [fluid_tuning_patch.describe_actor_prop(entry).rsplit(".", 1)[-1], entry["label"]]
    json_path = entry["json_path"]
    if is_look_path(json_path):
        _, package, _, name = fluid_tuning_patch.parse_look_path(json_path)
        return [name.rsplit(".", 1)[-1], package.rsplit("/", 1)[-1]]
    section, kind_name, _, field_name = fluid_tuning_patch.parse_json_path(json_path)
    return [field_name] + ([kind_name] if section == fluid_tuning_patch.KINDS_SECTION else [])


def token_line(text, token):
    index = text.find(token)
    return text.count("\n", 0, index) + 1


def test_class_of(name, test_classes):
    if name in (test_classes.get("timing_record_only") or []):
        return "timing_record_only"
    if name in (test_classes.get("numeric_pinned") or []):
        return "numeric_pin"
    if name in (test_classes.get("numeric_pin_when_failures_match") or {}):
        return "numeric_pin_when_failures_match"
    return "design"


def build_test_owner(overlay_text, test_classes_text, source_root):
    entries = overlay_entry_map(read_overlay(overlay_text))
    test_classes = load_json(test_classes_text)
    rows = []
    for name, test in sorted(collect_tests(source_root).items()):
        row = {"test": name, "file": test["file"], "class": test_class_of(name, test_classes), "owners": [], "evidence": [f"{test['file']}:{test['line']}"]}
        if name == HEADER_SYNC_TEST:
            rows.append(dict(row, special="header_sync"))
            continue
        evidence = set()
        for identifier, entry in sorted(entries.items()):
            tokens = adoption_tokens(entry)
            if all(token in test["text"] for token in tokens):
                row["owners"].append(identifier)
                evidence.update(f"{test['file']}:{token_line(test['text'], token)}" for token in tokens)
        row["evidence"] += sorted(evidence)
        rows.append(row)
    return {"tool": TOOL_NAME, "version": TOOL_VERSION, "overlay_sha256": sha_of(overlay_text), "test_classes_sha256": sha_of(test_classes_text), "rows": rows}


def command_test_owner(arguments):
    out_text = require_saved_output(arguments.out)
    document = build_test_owner(arguments.overlay, arguments.test_classes, arguments.source_root)
    write_json(out_text, document)
    print(f"test_owner 시험 {len(document['rows'])}개, 소유 있음 {sum(1 for row in document['rows'] if row['owners'])}개")
    return EXIT_OK


def judge_scene_map(judge):
    return {scene["name"]: scene for scene in judge.get("scenes") or []}


def source_rows(run_id, adoption, lineage, kind, judge, judge_text, scene_names):
    scenes = judge_scene_map(judge)
    field = "candidate_medians" if kind == "confirm" else "control_reference"
    rows = []
    for name in scene_names:
        require(name in scenes, "expect", f"{judge_text}: 출처 판정에 장면 {name} 이 없다")
        scene = scenes[name]
        values = scene.get(field) or {}
        for target in scene["targets"]:
            metric = target["metric"]
            noise = scene["noise"].get(metric) or {}
            drift_floor = noise.get("drift_floor")
            rows.append({"run": run_id, "adoption": adoption, "lineage": lineage, "scene": name, "metric": metric, "expected": values.get(metric), "noise": noise.get("noise"),
                         "drift_floor": drift_floor, "tolerance": target["tolerance"], "series": scene["series"],
                         "allowed": calib_judge.drift_allowance(drift_floor, target["tolerance"], scene["series"]),
                         "source": {"kind": kind, "judge": judge_text, "judge_sha256": sha_of(judge_text), "field": field, "lock_sha256": judge.get("lock_sha256"),
                                    "plan_sha256": judge.get("plan_sha256")}})
    return rows


def all_scene_rows(judge, kind, judge_text):
    return source_rows(None, None, None, kind, judge, judge_text, [scene["name"] for scene in judge.get("scenes") or []])


def prior_from_rows(rows):
    scenes = {}
    for row in rows:
        scene = scenes.setdefault(row["scene"], {"name": row["scene"], "candidate_medians": {}, "noise": {}})
        scene["candidate_medians"][row["metric"]] = row["expected"]
        scene["noise"][row["metric"]] = {"noise": row["noise"], "drift_floor": row["drift_floor"]}
    prior = {"scenes": list(scenes.values())}
    prior_table = {scene["name"]: {metric: {"noise": entry.get("noise"), "median_floor": entry.get("drift_floor")} for metric, entry in scene["noise"].items()} for scene in prior["scenes"]}
    return prior, prior_table


def rejudge(sweep, plan, rows, work_dir_text):
    prior, prior_table = prior_from_rows(rows)
    work_dir = project_path(work_dir_text)
    work_dir.mkdir(parents=True, exist_ok=True)
    scene_results = calib_judge.judge_sweep_scenes(sweep, plan, work_dir, prior_table, judged_names={row["scene"] for row in rows})
    return scene_results, calib_judge.verify_checks(scene_results, prior)


def path_forms(directories):
    forms = set()
    for text in directories:
        path = project_path(text)
        forms.update({str(path), path.as_posix(), normalized(path), str(text)})
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


def strip_scene_paths(scenes, work_directories):
    forms = path_forms(work_directories)
    return [replace_work_paths({key: value for key, value in scene.items() if key not in SCENE_PATH_KEYS}, forms) for scene in scenes or []]


def scene_differences(official_scenes, comparator_scenes, work_directories):
    official, comparator = strip_scene_paths(official_scenes, work_directories), strip_scene_paths(comparator_scenes, work_directories)
    if [scene.get("name") for scene in official] != [scene.get("name") for scene in comparator]:
        return ["장면 목록이 다르다"]
    differences = []
    for first, second in zip(official, comparator):
        for key in sorted(set(first) | set(second)):
            if canonical_text(first.get(key)) != canonical_text(second.get(key)) or (key in first) != (key in second):
                differences.append(f"{first.get('name')}.{key}")
    return differences


def adoption_states(rows, scenes, checks):
    control = {scene["name"]: scene.get("control_ok") is True for scene in scenes or []}
    by_key = {(check["scene"], check["metric"]): check for check in checks or []}
    states = {}
    for adoption in sorted_adoptions({row["adoption"] for row in rows}):
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


def replay_case(kind, input_text, differences):
    return {"kind": kind, "input": normalized(input_text), "input_sha256": sha_of(input_text), "equal": not differences, "differences": differences}


def replay_verify(verify_text, case_dir):
    verify_document = load_json(verify_text)
    against_text = normalized(verify_document["verify"]["against"])
    sweep = load_json(verify_document["sweep"])
    plan = load_json(sweep["plan"])
    rows = all_scene_rows(load_json(against_text), "confirm", against_text)
    work_dir = f"{case_dir}/compare-judge"
    scene_results, checks = rejudge(sweep, plan, rows, work_dir)
    differences = scene_differences(verify_document["scenes"], scene_results, [f"{parent_text(normalized(verify_text))}/judge", work_dir])
    if canonical_text(verify_document["verify"]["checks"]) != canonical_text(checks):
        differences.append("checks")
    return replay_case("verify", verify_text, differences)


def replay_self(judge_text, case_dir):
    judge_text = normalized(judge_text)
    judge = load_json(judge_text)
    sweep_text = normalized(judge["sweep"])
    sweep = load_json(sweep_text)
    plan = load_json(sweep["plan"])
    rows = all_scene_rows(judge, "confirm", judge_text)
    comparator_dir, official_dir = f"{case_dir}/compare-judge", f"{case_dir}/judge"
    scene_results, checks = rejudge(sweep, plan, rows, comparator_dir)
    project_path(official_dir).mkdir(parents=True, exist_ok=True)
    official = calib_judge.judge_verify_document(sweep, project_path(sweep_text), plan, project_path(judge["lock"]), judge["lock_sha256"], judge_text, project_path(official_dir))
    differences = scene_differences(official["scenes"], scene_results, [official_dir, comparator_dir])
    if canonical_text(official["verify"]["checks"]) != canonical_text(checks):
        differences.append("checks")
    return replay_case("self", judge_text, differences)


def replay_identity(screen_text, case_dir):
    screen_text = normalized(screen_text)
    screen = load_json(screen_text)
    sweep = load_json(screen["screen_sweep"])
    plan = load_json(sweep["plan"])
    _, checks = rejudge(sweep, plan, all_scene_rows(screen, "screen", screen_text), f"{case_dir}/compare-judge")
    differences = [f"{check['scene']}.{check['metric']}" for check in checks
                   if not is_number(check["value"]) or not is_number(check["expected"]) or abs(check["value"] - check["expected"]) != 0]
    return replay_case("identity", screen_text, differences)


def command_replay(arguments):
    out_dir = require_saved_output(arguments.out_dir)
    cases = []
    for kind, inputs, function in (("verify", arguments.verify, replay_verify), ("self", arguments.self_judge, replay_self), ("identity", arguments.identity, replay_identity)):
        for index, text in enumerate(inputs or []):
            cases.append(function(text, f"{out_dir}/{kind}-{index + 1}"))
    require(cases, "input", "replay 대상이 없다")
    write_json(f"{out_dir}/replay.json", {"tool": TOOL_NAME, "version": TOOL_VERSION, "cases": cases})
    for case in cases:
        print(f"{case['kind']} {case['input']}: {'같음' if case['equal'] else '다름 ' + ', '.join(case['differences'][:6])}")
    return EXIT_OK if all(case["equal"] for case in cases) else EXIT_FAILED


def scene_rule_parts(plan, name):
    scene = next((item for item in plan.get("scenes") or [] if item.get("name") == name), None)
    metrics = plan.get("metrics") or {}
    files = {key: sha_or_none(scene[key]) for key in prereg_lock.SCENE_FILE_KEYS if scene and isinstance(scene.get(key), str)}
    return {"scene": scene, "targets": [target for target in metrics.get("targets") or [] if target.get("scene", name) == name],
            "derived": [entry for entry in metrics.get("derived") or [] if entry.get("scene", name) == name], "files": files}


def binding_problems(plan_text, source, judge):
    binding = source.get("binding") or {}
    kind = binding.get("kind")
    judge_plan_sha = judge.get("plan_sha256")
    label = f"{source.get('adoption')} {kind}"
    if kind == "same":
        return [] if sha_of(plan_text) == judge_plan_sha else [f"{label}: 계획 sha ≠ 출처 judge plan_sha256"]
    if kind == "eol":
        problems = [] if crlf_sha(plan_text) == judge_plan_sha else [f"{label}: 계획 CRLF sha ≠ 출처 judge plan_sha256"]
        note = load_json(binding["eol_note"])
        if not any(normalized(entry.get("plan", "")) == plan_text for entry in note.get("plans") or []):
            problems.append(f"{label}: eol-note 에 {plan_text} 이 없다")
        return problems
    if kind == "copy":
        source_plan = normalized(binding["source_plan"])
        problems = [] if sha_of(source_plan) == judge_plan_sha else [f"{label}: source_plan sha ≠ 출처 judge plan_sha256"]
        try:
            removed = removed_base_paths(load_json(source_plan), load_json(plan_text))
        except ValueError as error:
            return problems + [f"{label}: {error}"]
        return problems + ([] if removed else [f"{label}: 사본이 원본에서 지운 base 가 없다"])
    if kind == "cross_plan":
        source_plan = normalized(binding["source_plan"])
        problems = [] if judge_plan_sha in (sha_of(source_plan), crlf_sha(source_plan)) else [f"{label}: source_plan(또는 CRLF) sha ≠ 출처 judge plan_sha256"]
        run_plan, other_plan = load_json(plan_text), load_json(source_plan)
        for name in source.get("scenes") or []:
            if canonical_text(scene_rule_parts(run_plan, name)) != canonical_text(scene_rule_parts(other_plan, name)):
                problems.append(f"{label}: 장면 {name} 의 scene·targets·derived·영역 틀 sha 가 두 계획에서 다르다")
        return problems
    return [f"{label}: 모르는 binding.kind"]


def source_lock_path(judge):
    if judge.get("lock"):
        return judge["lock"]
    sweep_text = judge.get("screen_sweep") or judge.get("sweep")
    return load_json(sweep_text).get("lock") if sweep_text and is_file(sweep_text) else None


def lineage_problems(source, judge):
    lock_text = source_lock_path(judge)
    if not lock_text or not is_file(lock_text):
        return [f"{source['adoption']}: 출처 잠금 {lock_text} 이 없다(계보 기계 검사 불가)"]
    lock_files = load_json(lock_text).get("files") or {}
    changed = [path for path in MEASURE_TOOL_FILES if lock_files.get(path) != sha_or_none(path)]
    lineage = source.get("lineage")
    if not changed or lineage == "scene_changed" or (lineage == "clean" and source.get("lineage_evidence")):
        return []
    return [f"{source['adoption']}: 측정 도구 {', '.join(changed)} sha 가 출처 잠금과 다른데 lineage {lineage} 근거 없음"]


def build_run_entry(spec, run, entries, final_dir, edition, influences):
    run_id, block = run["id"], run.get("block")
    require(block in ("F0", "F1"), "expect", f"{run_id}: block {block} 은 F0·F1 이어야 한다")
    plan_text = normalized(run["plan"])
    require(is_file(plan_text), "expect", f"{run_id}: 계획 없음 {plan_text}")
    is_f0 = block == "F0"
    require(is_f0 == bool(run.get("lock")) and is_f0 == bool(run.get("verify_against")), "expect", f"{run_id}: F0 만 lock·verify_against 를 둔다")
    overlay_text = normalized(run["overlay"])
    require(is_file(overlay_text), "expect", f"{run_id}: 실행 덧씌움 없음 {overlay_text}")
    sources, rows = [], []
    for source in run.get("sources") or []:
        adoption, kind = source.get("adoption"), source.get("kind")
        judge_text = normalized(source["judge"])
        require(is_file(judge_text), "expect", f"{run_id}: 출처 judge 없음 {judge_text}")
        judge = load_json(judge_text)
        if kind == "confirm":
            require(adoption in spec["adoptions"], "expect", f"{run_id}: confirm 출처 {adoption} 이 adoptions 에 없다")
            origin = entries[adoption_entry_id(spec["adoptions"][adoption])].get("from") or {}
            require(sha_of(judge_text) == origin.get("judge_sha256"), "expect", f"{run_id}: {adoption} 출처 judge sha ≠ 동결본 from.judge_sha256")
            adopt = judge.get("adopt") or {}
            require(adopt.get("decision") is True and adopt.get("variant") == origin.get("variant"), "expect", f"{run_id}: {adoption} 출처 adopt 결정·변형이 동결본과 다르다")
        else:
            require(kind == "screen" and adoption in influences, "expect", f"{run_id}: 출처 {adoption} kind {kind} 는 confirm(채택) 또는 screen(영향)이어야 한다")
        require_none(binding_problems(plan_text, source, judge), "expect")
        require_none(lineage_problems(source, judge), "expect")
        require(source.get("lineage") in ("clean", "scene_changed", "other_after"), "expect", f"{run_id}: {adoption} lineage {source.get('lineage')} 모름")
        rows.extend(source_rows(run_id, adoption, source["lineage"], kind, judge, judge_text, source.get("scenes") or []))
        sources.append({"adoption": adoption, "kind": kind, "judge": judge_text, "judge_sha256": sha_of(judge_text), "binding": copy.deepcopy(source["binding"])})
    require(rows, "expect", f"{run_id}: 행이 없다")
    lock_text = normalized(run["lock"]) if is_f0 else run_lock_path(final_dir, run_id, edition)
    entry = {"block": block, "plan": plan_text, "plan_sha256": sha_of(plan_text), "overlay": overlay_text, "overlay_sha256": sha_of(overlay_text), "lock": lock_text,
             "lock_sha256": sha_of(lock_text) if is_f0 else None, "judge_lock": judge_lock_path(final_dir, run_id, edition) if is_f0 else None,
             "verify_against": normalized(run["verify_against"]) if is_f0 else None, "sources": sources}
    return entry, rows


def check_source_coverage(spec, run, entries):
    exceptions = {(item.get("adoption"), item.get("scene")) for item in spec.get("source_scene_exceptions") or [] if item.get("reason")}
    for source in run.get("sources") or []:
        if source.get("kind") != "confirm":
            continue
        judge = load_json(source["judge"])
        source_plan_text = judge["plan"]
        source_plan = load_json(source_plan_text)
        knob = matching_knob_params(source_plan, spec["adoptions"][source["adoption"]])
        require(len(knob) == 1, "expect", f"{run['id']}: {source['adoption']} 출처 계획 {source_plan_text} 에서 손잡이를 정확히 하나 찾지 못했다")
        scope = set(knob[0].get("scenes") or [scene.get("name") for scene in source_plan.get("scenes") or []])
        judged = {scene["name"] for scene in judge.get("scenes") or []}
        missing = sorted(name for name in judged & scope if name not in (source.get("scenes") or []) and (source["adoption"], name) not in exceptions)
        require(not missing, "expect", f"{run['id']}: {source['adoption']} 손잡이 장면 {', '.join(missing)} 이 행에 없다(source_scene_exceptions 에도 없음)")


def scene_level_map(run_entries):
    levels = {}
    for run_id, run in run_entries.items():
        for scene in load_json(run["plan"]).get("scenes") or []:
            levels[(run_id, scene.get("name"))] = scene.get("level")
    return levels


def p22_preview_equal(spec, preview):
    for plan_text in spec["p22"]["plans"]:
        for item in load_json(plan_text).get("base_tuning") or []:
            current = document_value(preview, item["json_path"])
            if current is MISSING or not same_value(current, item["value"]):
                return False
    return True


def check_tests_required(spec, edition):
    tests_required = spec.get("tests_required") or {}
    keys = TESTS_REQUIRED_KEYS[edition]
    require(set(tests_required) == set(keys), "expect", f"tests_required 키 {sorted(tests_required)} ≠ {list(keys)}")
    for key in keys:
        labels = tests_required[key]
        require(isinstance(labels, list) and all(isinstance(label, str) for label in labels) and len(set(labels)) == len(labels), "expect", f"tests_required.{key} 는 중복 없는 문자열 목록이어야 한다")
    require(set(tests_required[keys[0]]) <= set(tests_required[keys[1]]), "expect", f"tests_required.{keys[1]} 가 {keys[0]} 를 포함하지 않는다")


def f0_shortage_problems(runs, edition):
    count = sum(1 for run in runs.values() if run.get("block") == "F0")
    if edition != "e1" or count >= F0_REQUIRED_SELF_CHECKS:
        return []
    return [f"e1 F0 실행 {count}개 < 자기 검증 표본 하한 {F0_REQUIRED_SELF_CHECKS}(D-W28·개정 H, 결과 전 사전 등록 결함)"]


def build_expect(spec_text, w_anchor_text):
    spec_text = normalized(spec_text)
    spec = load_json(spec_text)
    require(spec.get("version") == SPEC_VERSION, "expect", f"명세 version {spec.get('version')} ≠ {SPEC_VERSION}")
    edition = spec.get("edition")
    require(edition in EDITIONS, "expect", f"명세 edition {edition} ∉ {EDITIONS}")
    require("restart" not in spec, "expect", "명세에 restart 키가 있다(개정 E2: 재시작 번호는 기록 폴더 수로 정함)")
    final_dir = final_directory(spec)
    restart = len(archive_directories(final_dir))
    require(restart in (0, 1) and (edition == "e1" or restart == 0), "expect", f"기록 폴더 aborted-* 가 {restart}개다(e1 은 0·1, r2 는 0)")
    paths = derived_paths(final_dir, edition)
    require(spec_text == paths["spec"], "expect", f"--spec 은 {paths['spec']} 이어야 한다")
    before = read_canonical_pair(spec["canonical_before_dir"])
    require({"tuning": before["tuning_sha256"], "look": before["look_sha256"]} == CANONICAL_BEFORE_SHA256, "expect", "canonical-before sha ≠ CANONICAL_BEFORE_SHA256")
    overlay_text = normalized(spec["overlay"])
    entries = overlay_entry_map(read_overlay(overlay_text))
    require(FX9_REMOVED_PATH not in entries, "expect", "동결본에 fx9 경로가 있다(D-I)")
    adoption_ids = {number: adoption_entry_id(adoption) for number, adoption in spec["adoptions"].items()}
    require(len(set(adoption_ids.values())) == len(adoption_ids) and set(adoption_ids.values()) == set(entries), "expect", "adoptions entry_id 집합 ≠ 동결본 항목 집합")
    require_none(copies_check_problems(spec_text), "expect")
    preview = read_canonical_pair(spec["preview_dir"])
    influences = list(spec.get("influences") or [])
    runs, rows = {}, []
    for run in spec.get("runs") or []:
        require(run["id"] not in runs, "expect", f"실행 id {run['id']} 가 겹친다")
        runs[run["id"]], run_rows = build_run_entry(spec, run, entries, final_dir, edition, influences)
        rows.extend(run_rows)
        check_source_coverage(spec, run, entries)
    require_none(f0_shortage_problems(runs, edition), "expect")
    fallback_runs, fallback_rows = {}, []
    for covered in spec.get("covered") or []:
        fallback = covered["fallback"]
        require(fallback["id"] not in runs and fallback["id"] not in fallback_runs, "expect", f"fallback 실행 id {fallback['id']} 가 겹친다")
        fallback_runs[fallback["id"]], run_rows = build_run_entry(spec, fallback, entries, final_dir, edition, influences)
        fallback_rows.extend(run_rows)
        check_source_coverage(spec, fallback, entries)
        require(covered["by_run"] in runs and covered["adoption"] in spec["adoptions"] and covered["by_adoption"] in spec["adoptions"], "expect", f"covered {covered['adoption']}: by_run·by_adoption 이 없다")
    row_adoptions = {row["adoption"] for row in rows}
    covered_adoptions = {covered["adoption"] for covered in spec.get("covered") or []}
    if edition == "e1":
        uncovered = sorted_adoptions(number for number in spec["adoptions"] if number not in row_adoptions | covered_adoptions)
        require(not uncovered, "expect", f"rows·covered 에 없는 채택: {', '.join(uncovered)}")
    require(set(influences) <= row_adoptions, "expect", "rows 에 없는 영향 id 가 있다")
    levels = scene_level_map(dict(runs, **fallback_runs))
    copies_document = load_json(paths["copies"])
    copy_shas = {output["path"]: output["sha256"] for output in copies_document["outputs"]}
    covered_parents = {covered["adoption"]: covered for covered in spec.get("covered") or []}
    ab_entries = []
    for ab in spec.get("ab") or []:
        require(ab["for_run"] in runs, "expect", f"{ab['id']}: for_run {ab['for_run']} 이 runs 에 없다")
        require(ab["for_adoption"] in spec["adoptions"], "expect", f"{ab['id']}: for_adoption {ab['for_adoption']} 이 adoptions 에 없다")
        run_keys = {row["adoption"] for row in rows if row["run"] == ab["for_run"]}
        require(ab["trigger_adoption"] in run_keys, "expect", f"{ab['id']}: trigger_adoption {ab['trigger_adoption']} 이 {ab['for_run']} 행에 없다")
        parent = covered_parents.get(ab["for_adoption"])
        require(parent is None or ab.get("cross") or ab["trigger_adoption"] == parent["by_adoption"], "expect", f"{ab['id']}: covered 부모 자기 wab trigger_adoption ≠ by_adoption")
        require(ab.get("trigger") in AB_TRIGGERS, "expect", f"{ab['id']}: trigger {ab.get('trigger')} 모름")
        plan_text = normalized(ab["plan"])
        require(copy_shas.get(plan_text) == sha_of(plan_text), "expect", f"{ab['id']}: 사본 sha ≠ copies.json")
        overlay_ab = normalized(ab["overlay"])
        identifier = adoption_ids[ab["for_adoption"]]
        ab_entries.append({"id": ab["id"], "plan": plan_text, "plan_sha256": sha_of(plan_text), "overlay": overlay_ab, "overlay_sha256": sha_of(overlay_ab),
                           "lock": run_lock_path(final_dir, ab["id"], edition), "for_run": ab["for_run"], "for_adoption": ab["for_adoption"],
                           "trigger_adoption": ab["trigger_adoption"], "trigger": ab["trigger"], "cross": bool(ab.get("cross")), "entry_id": identifier,
                           "before_value": load_json(plan_text)["params"][0]["values"][0], "adopted_value": entries[identifier]["value"]})
    p22 = None
    if spec.get("p22"):
        require(p22_preview_equal(spec, preview), "expect", "p22_pre 거짓: p22 계획 base_tuning 값 ≠ preview 값(R-W12 (b))")
        p22 = {"plans": [normalized(path) for path in spec["p22"]["plans"]], "ns_package": spec["p22"]["ns_package"], "runs": list(spec["p22"]["runs"]), "p22_pre": True}
    check_tests_required(spec, edition)
    adoptions = {}
    for number, adoption in spec["adoptions"].items():
        identifier = adoption_ids[number]
        entry = entries[identifier]
        own = [row for row in rows + fallback_rows if row["adoption"] == number]
        if "actor_prop" in adoption:
            adoption_levels = [adoption["actor_prop"]["level"]]
        elif is_look_path(identifier):
            adoption_levels = list(entry.get("levels") or [])
        else:
            adoption_levels = sorted({levels.get((row["run"], row["scene"])) for row in own if levels.get((row["run"], row["scene"]))})
        covered = covered_parents.get(number)
        adoptions[number] = {"entry_id": identifier, "kind": "actor_prop" if "actor_prop" in adoption else "assignment", "owner": owner_key(adoption), "levels": adoption_levels,
                             "scenes": sorted({row["scene"] for row in own}), "adopted_value": entry["value"],
                             "covered_by": {"by_run": covered["by_run"], "by_adoption": covered["by_adoption"]} if covered else None}
    w_expect_text, test_owner_text = normalized(spec["w_expect"]), normalized(spec["test_owner"])
    expect = {"tool": TOOL_NAME, "version": EXPECT_VERSION, "edition": edition, "restart": restart, "phase": EDITION_PHASES[edition], "spec": spec_text,
              "spec_sha256": sha_of(spec_text), "final_dir": final_dir, "overlay": overlay_text, "overlay_sha256": sha_of(overlay_text),
              "canonical_before": {"tuning": before["tuning_sha256"], "look": before["look_sha256"]}, "canonical_after": {"tuning": preview["tuning_sha256"], "look": preview["look_sha256"]},
              "copies_sha256": sha_of(paths["copies"]), "w_expect": {"path": w_expect_text, "sha256": sha_of(w_expect_text)},
              "test_owner": {"path": test_owner_text, "sha256": sha_of(test_owner_text)}, "adoptions": adoptions, "influences": influences, "runs": runs, "rows": rows,
              "covered": copy.deepcopy(spec.get("covered") or []), "fallback_runs": fallback_runs, "fallback_rows": fallback_rows, "ab": ab_entries, "p22": p22,
              "tests_required": copy.deepcopy(spec["tests_required"])}
    return expect, paths, spec, normalized(w_anchor_text)


def build_queue(expect, paths, spec, w_anchor_text):
    blocks = {block: [] for block in BLOCK_ORDER}
    for run_id, run in expect["runs"].items():
        item = {"plan": run["plan"], "stages": list(VERIFY_STAGES), "chain": None, "run": run_id, "lock": run["lock"], "overlay": run["overlay"]}
        if run["block"] == "F0":
            item.update(judge_lock=run["judge_lock"], verify_against=run["verify_against"])
        blocks[run["block"]].append(item)
    for ab in expect["ab"]:
        blocks["F2"].append({"plan": ab["plan"], "stages": list(AB_STAGES), "chain": None, "lock": ab["lock"], "overlay": ab["overlay"],
                             "ab": {key: ab[key] for key in ("id", "for_run", "for_adoption", "trigger_adoption", "trigger", "cross")}})
    plans = [item["plan"] for items in blocks.values() for item in items]
    require(len(plans) == len(set(plans)), "expect", "큐에 같은 계획 경로가 둘 이상 나온다")
    files = judge_file_paths(expect["final_dir"], spec["overlay"])
    return {"mode": "final", "test_classes": {"path": TEST_CLASSES_PATH, "sha256": sha_of(TEST_CLASSES_PATH)},
            "final": {"edition": expect["edition"], "phase": expect["phase"], "restart": expect["restart"], "expect": paths["expect"], "journal": paths["journal"],
                      "anchor": paths["anchor"], "w_anchor": w_anchor_text, "judge_files": {path: sha_of(path) for path in files}},
            "blocks": [{"id": block, "plans": items} for block, items in blocks.items() if items]}


def command_expect(arguments):
    out_text, queue_text = require_saved_output(arguments.out), require_saved_output(arguments.queue_out)
    expect, paths, spec, w_anchor_text = build_expect(arguments.spec, arguments.w_anchor)
    require(out_text == paths["expect"] and queue_text == paths["queue"], "expect", f"--out·--queue-out 은 {paths['expect']}·{paths['queue']} 이어야 한다")
    write_json(paths["expect"], expect)
    queue = build_queue(expect, paths, spec, w_anchor_text)
    write_json(paths["queue"], queue)
    print(f"expect 행 {len(expect['rows'])} 실행 {len(expect['runs'])} A/B {len(expect['ab'])} restart {expect['restart']} sha {sha_of(paths['expect'])}")
    return EXIT_OK


def archive_resolver(final_dir, archive):
    prefixes = (f"{final_dir}/batch-e1/", f"{final_dir}/locks/")
    anchor_text = f"{final_dir}/anchor.json"

    def resolve(text):
        relative = normalized(text)
        if relative == anchor_text or relative.startswith(prefixes):
            return f"{archive}/{relative[len(final_dir) + 1:]}"
        return relative
    return resolve


def identity_resolver(text):
    return normalized(text)


def item_final(item):
    return item.get("final") or {}


def sweep_attempts(final, stage, opportunity):
    return [attempt for attempt in final.get("attempts") or [] if attempt.get("step") == "sweep" and attempt.get("stage") == stage and attempt.get("opportunity") == opportunity]


def is_environment_failure(attempt):
    return attempt.get("exit") in (None, 1) and attempt.get("state") != "running"


def opportunity_outcome(final, stage, opportunity):
    attempts = sweep_attempts(final, stage, opportunity)
    succeeded = [attempt for attempt in attempts if attempt.get("exit") == 0]
    if succeeded:
        return "done", normalized(succeeded[-1]["folder"])
    if len(attempts) >= 2 and all(is_environment_failure(attempt) for attempt in attempts):
        return "infeasible", None
    return "open", None


def run_rows(expect, run_id):
    return [row for row in expect["rows"] + expect.get("fallback_rows", []) if row["run"] == run_id]


def self_check(rows, verify_document, compare_document, folder_text):
    work_directories = [f"{folder_text}/judge", f"{folder_text}/compare-judge"]
    official_checks = (verify_document.get("verify") or {}).get("checks")
    same_checks = canonical_text(official_checks) == canonical_text(compare_document.get("checks"))
    same_scenes = canonical_text(strip_scene_paths(verify_document.get("scenes"), work_directories)) == canonical_text(strip_scene_paths(compare_document.get("scenes"), work_directories))
    same_states = adoption_states(rows, verify_document.get("scenes"), official_checks) == compare_document.get("adoptions")
    return same_checks and same_scenes and same_states


def opportunity_documents(final, opportunity, resolve, is_f0):
    outcome, folder = opportunity_outcome(final, "verify", opportunity)
    if outcome != "done":
        return outcome, folder, None, None, None
    sweep = load_json(f"{resolve(folder)}/sweep.json")
    compare_document = load_json(f"{resolve(folder)}/compare.json")
    verify_document = load_json(f"{resolve(folder)}/verify.json") if is_f0 else None
    return outcome, folder, sweep, compare_document, verify_document


def recompute_verify_item(expect, item, resolve):
    final = item_final(item)
    run_id = final["run"]
    run = expect["runs"][run_id]
    rows = run_rows(expect, run_id)
    keys = sorted_adoptions({row["adoption"] for row in rows})
    is_f0 = run["block"] == "F0"
    problems, opportunities = [], {}
    for opportunity in OPPORTUNITIES:
        outcome, folder, sweep, compare_document, verify_document = opportunity_documents(final, opportunity, resolve, is_f0)
        entry = {"outcome": outcome, "folder": folder}
        if outcome == "done":
            compare_states = {key: "undetermined" for key in keys} if sweep.get("invalid_count") else adoption_states(rows, compare_document.get("scenes"), compare_document.get("checks"))
            if compare_states != compare_document.get("adoptions"):
                problems.append(f"{run_id} {opportunity}: compare.json adoptions ≠ R-W4 다시 계산")
            entry.update(compare_states=compare_states, states=compare_states)
            if is_f0:
                official_states = adoption_states(rows, verify_document.get("scenes"), (verify_document.get("verify") or {}).get("checks"))
                entry.update(official_states=official_states, states=official_states, self_check=self_check(rows, verify_document, compare_document, folder))
        opportunities[opportunity] = entry
    first, second = opportunities["first"], opportunities["rerun"]
    mismatch = any(entry.get("self_check") is False for entry in opportunities.values())
    decision, rerun, first_states = None, 0, None
    if first["outcome"] == "infeasible":
        decision = {key: "incomplete" for key in keys}
    elif first["outcome"] == "done":
        first_states = first["states"]
        rerun = 1 if "undetermined" in first_states.values() else 0
        decision = {}
        for key in keys:
            if first_states.get(key) in ("pass", "fail"):
                decision[key] = first_states[key]
            elif second["outcome"] == "done":
                decision[key] = second["states"].get(key)
            elif second["outcome"] == "infeasible":
                decision[key] = "incomplete"
            else:
                decision[key] = None
    is_complete = decision is not None and None not in decision.values()
    status = "self_check_mismatch" if mismatch else None
    if status is None and is_complete:
        status = "incomplete" if decision and all(value == "incomplete" for value in decision.values()) else "verified"
    sources = {key: {"run": run_id, "item_plan": item.get("plan"), "opportunity": "first" if first_states and first_states.get(key) in ("pass", "fail") else
                     ("rerun" if second["outcome"] == "done" else None)} for key in keys}
    return {"run": run_id, "is_f0": is_f0, "opportunities": opportunities, "first_states": first_states, "decision": decision if is_complete else None, "rerun": rerun,
            "status": status, "mismatch": mismatch, "problems": problems, "sources": sources,
            "self_check": {opportunity: opportunities[opportunity].get("self_check") for opportunity in OPPORTUNITIES} if is_f0 else None}


def f0_self_checks(expect, journal, resolve):
    results = {}
    for item in journal.get("items") or []:
        final = item_final(item)
        if final.get("kind") != "verify" or expect["runs"].get(final.get("run"), {}).get("block") != "F0":
            continue
        results[final["run"]] = recompute_verify_item(expect, item, resolve)["self_check"]
    return results


def restart_justification_problems(final_dir, expect, archive):
    old_journal_text, old_anchor_text = f"{archive}/batch-e1/journal.json", f"{archive}/anchor.json"
    if not is_file(old_journal_text) or not is_file(old_anchor_text):
        return [f"기록 폴더 {archive} 에 batch-e1/journal.json 과 anchor.json 이 모두 있어야 한다"], old_journal_text, old_anchor_text
    old = load_json(old_journal_text)
    final_batch = old.get("final_batch") or {}
    problems = []
    if (final_batch.get("outcome") or {}).get("event") != "E3":
        problems.append("옛 일지 final_batch.outcome.event 가 E3 이 아니다")
    resolve = archive_resolver(final_dir, archive)
    problems += [f"옛 일지 산출물 {path} 을 바꿔 읽은 {resolve(path)} 의 sha 가 artifact_sha256 과 다르거나 없다"
                 for item in old.get("items") or [] for path, recorded in sorted((item_final(item).get("artifact_sha256") or {}).items()) if sha_or_none(resolve(path)) != recorded]
    checks = f0_self_checks(expect, old, resolve)
    if not any(value is False for result in checks.values() for value in (result or {}).values()):
        problems.append("옛 일지 F0 자기 검증을 다시 계산하면 불일치가 없다(재시작 정당성 없음)")
    if sha_of(old_anchor_text) != final_batch.get("anchor_sha256"):
        problems.append("옛 anchor.json sha ≠ 옛 일지 final_batch.anchor_sha256")
    return problems, old_journal_text, old_anchor_text


def fog_generator_light():
    lines = [line for line in project_path(FOG_GENERATOR).read_text(encoding="utf-8").splitlines() if f"\"label\": \"{FOG_LIGHT_LABEL}\"" in line]
    if len(lines) != 1:
        return len(lines), None
    match = FOG_LIGHT_VALUE_PATTERN.search(lines[0])
    return 1, float(match.group(1)) if match else None


def is_fog_light_entry(identifier):
    return identifier.startswith("actor_prop:") and FOG_LIGHT_LABEL in identifier


def w12_check(r1_final_dir, r2_w_expect_text):
    rollback_text = f"{r1_final_dir}/r2/rollback.json"
    keep_text = f"{r1_final_dir}/overlay-keep.json"
    if not is_file(rollback_text):
        return {}, [f"rollback 보고 없음: {rollback_text}"]
    rollback = load_json(rollback_text)
    results = {"verdict_sha256": is_file(rollback.get("verdict", "")) and sha_of(rollback["verdict"]) == rollback.get("verdict_sha256"),
               "keep_sha256": is_file(keep_text) and sha_of(keep_text) == rollback.get("keep_sha256")}
    frozen = overlay_entry_map(read_overlay(load_json(f"{r1_final_dir}/final-spec.json")["overlay"]))
    reverted = {item["entry_id"] for item in load_json(rollback["verdict"]).get("revert") or []} if results["verdict_sha256"] else set()
    keep = overlay_entry_map(read_overlay(keep_text)) if is_file(keep_text) else {}
    results["kept_set"] = results["verdict_sha256"] and set(keep) == set(frozen) - reverted
    results["kept_values"] = all(canonical_text(keep[identifier]) == canonical_text(frozen.get(identifier)) for identifier in keep)
    count, value = fog_generator_light()
    if any(is_fog_light_entry(identifier) for identifier in reverted):
        report = load_json_or_none(FOG_STAGE_REPORT) or {}
        written = (load_json_or_none(r2_w_expect_text) or {}).get("written") or {}
        results["fog_generator_before"] = count == 1 and value == FOG_LIGHT_BEFORE
        results["fog_stage_regenerated"] = is_file(FOG_STAGE_REPORT) and file_mtime(FOG_STAGE_REPORT) > prereg_lock.parse_moment(rollback["finished_at"])
        results["fog_stage_preset_sha256"] = report.get("preset_tuning_sha256") == written.get("tuning")
    else:
        results["fog_generator_adopted"] = count == 1 and value == FOG_LIGHT_ADOPTED
    return results, [f"W12 검사 거짓: {name}" for name, passed in sorted(results.items()) if not passed]


def copies_input_plans(spec):
    plans = [plan_copy["plan"] for plan_copy in spec.get("plan_copies") or []]
    plans += [ab[key] for ab in spec.get("ab") or [] for key in ("scene_plan", "knob_plan")]
    return [normalized(path) for path in plans]


def anchor_file_list(spec_text, spec, expect, queue, paths, previous_files):
    files = set(RULE_DOCUMENTS) | set(prereg_lock.JUDGE_TOOL_FILES) | {BATCH_PATH, TOOL_PATH, TEST_CLASSES_PATH}
    files |= {spec_text, paths["expect"], paths["queue"], paths["copies"], normalized(spec["overlay"])}
    for output in load_json(paths["copies"])["outputs"]:
        files.add(output["path"])
        files.update(output["inputs"])
    for directory in (spec["canonical_before_dir"], spec["preview_dir"]):
        pair = read_canonical_pair(directory)
        files.update({pair["tuning_path"], pair["look_path"]})
    files |= {normalized(spec["w_expect"]), normalized(spec["test_owner"])}
    files |= {normalized(path) for path in spec.get("pie_files") or []}
    plans = [item["plan"] for block in queue["blocks"] for item in block["plans"]] + copies_input_plans(spec) + [run["plan"] for run in expect["fallback_runs"].values()]
    for plan_text in plans:
        files.update(prereg_lock.required_files(plan_text))
    for run in expect["runs"].values():
        if run["block"] == "F0":
            files |= {run["lock"], run["verify_against"]}
    if spec["edition"] == "r2":
        files |= {f"{expect['final_dir']}/rollback.json", f"{parent_text(expect['final_dir'])}/write-canonical-r2.json"}
        files |= set(previous_files)
    return sorted(normalized(path) for path in files)


def r2_superset_problems(r1_final_dir, r2_paths, previous_files, files):
    problems = [f"r1 닻 항목 {path} 이 r2 닻에 같은 sha 로 없다" for path, recorded in sorted(previous_files.items()) if files.get(path) != recorded]
    allowed = {f"{r1_final_dir}/overlay-keep.json", f"{r1_final_dir}/r2/w-expect.json", f"{r1_final_dir}/r2/rollback.json", f"{r1_final_dir}/write-canonical-r2.json",
               r2_paths["spec"], r2_paths["copies"], r2_paths["expect"], r2_paths["queue"]}
    if is_file(r2_paths["copies"]):
        allowed |= {output["path"] for output in load_json(r2_paths["copies"])["outputs"]}
    preview_prefix = f"{r1_final_dir}/preview-r2/"
    problems += [f"r2 닻의 새 경로 {path} 가 허용 목록 밖이다(개정 E3)" for path in sorted(files) if path not in previous_files and path not in allowed and not path.startswith(preview_prefix)]
    return problems


def command_anchor(arguments):
    spec_text = normalized(arguments.spec)
    spec = load_json(spec_text)
    edition = spec.get("edition")
    require(edition in EDITIONS, "input", f"명세 edition {edition} 모름")
    final_dir = final_directory(spec)
    paths = derived_paths(final_dir, edition)
    require(spec_text == paths["spec"], "input", f"--spec 은 {paths['spec']} 이어야 한다")
    require_saved_output(paths["anchor"])
    require(is_file(paths["expect"]) and is_file(paths["queue"]), "input", "expect.json·큐가 없다")
    expect, queue = load_json(paths["expect"]), load_json(paths["queue"])
    require(expect.get("spec_sha256") == sha_of(spec_text), "input", "expect.spec_sha256 ≠ 명세 sha")
    require_none(f0_shortage_problems(expect["runs"], edition), "input")
    require(normalized(queue["final"]["expect"]) == paths["expect"], "input", "큐 final.expect ≠ expect 경로")
    require_none([f"큐 judge_files sha 다름: {path}" for path, recorded in sorted(queue["final"]["judge_files"].items()) if sha_or_none(path) != recorded], "input")
    require(sha_or_none(queue["test_classes"]["path"]) == queue["test_classes"]["sha256"], "input", "큐 test_classes sha 다름")
    require_none(copies_check_problems(spec_text), "copies")
    restart = expect["restart"]
    previous_journal, previous_anchor, w12 = None, None, None
    require(not is_file(paths["journal"]), "after_results", f"{paths['journal']} 이 있다(결과 뒤 닻을 다시 만들 수 없다, D4)")
    if edition == "e1" and restart == 1:
        archives = archive_directories(final_dir)
        require(len(archives) == 1, "restart", f"기록 폴더가 {len(archives)}개다")
        leftover = [run["judge_lock"] for run in expect["runs"].values() if run["block"] == "F0" and is_file(run["judge_lock"])]
        require(not leftover, "restart", f"옛 F0 판정 잠금이 locks/ 에 남아 있다: {', '.join(leftover)}")
        problems, previous_journal, old_anchor_text = restart_justification_problems(final_dir, expect, archives[0])
        require_none(problems, "restart")
        previous_anchor = {"path": old_anchor_text, "sha256": sha_of(old_anchor_text)}
    previous_files = {}
    if edition == "r2":
        require_none(check_anchor(arguments.w_anchor, "e1", []), "anchor")
        w_anchor = load_json(arguments.w_anchor)
        previous_anchor = {"path": normalized(w_anchor["anchor"]), "sha256": w_anchor["sha256"]}
        previous_files = load_json(previous_anchor["path"])["files"]
        w12, problems = w12_check(parent_text(final_dir), normalized(spec["w_expect"]))
        require_none(problems, "w12")
    listed = anchor_file_list(spec_text, spec, expect, queue, paths, previous_files)
    require_none([f"닻 파일 없음: {path}" for path in listed if not is_file(path)], "files")
    files = {path: sha_of(path) for path in listed}
    if edition == "r2":
        require_none(r2_superset_problems(parent_text(final_dir), paths, previous_files, files), "superset")
    w_anchor_out = normalized(arguments.w_anchor_out)
    if w_anchor_out != DEFAULT_W_ANCHOR:
        require_saved_output(w_anchor_out)
    anchor = {"tool": TOOL_NAME, "version": TOOL_VERSION, "edition": edition, "restart": restart, "previous_journal": previous_journal, "previous_anchor": previous_anchor,
              "w12_check": w12, "files": files}
    write_json(paths["anchor"], anchor)
    anchor_sha = sha_of(paths["anchor"])
    write_json(w_anchor_out, {"anchor": paths["anchor"], "edition": edition, "sha256": anchor_sha})
    print(anchor_sha)
    return EXIT_OK


def is_unmeasured_journal(journal):
    final_batch = journal.get("final_batch") or {}
    return final_batch.get("starts") == [] and all(not item_final(item).get("attempts") for item in journal.get("items") or [])


def command_pin_journal(arguments):
    spec_text = normalized(arguments.spec)
    spec = load_json(spec_text)
    edition = spec.get("edition")
    require(edition in EDITIONS, "input", f"명세 edition {edition} 모름")
    final_dir = final_directory(spec)
    paths = derived_paths(final_dir, edition)
    require(spec_text == paths["spec"], "input", f"--spec 은 {paths['spec']} 이어야 한다")
    expect = load_json(paths["expect"])
    require_none(check_anchor(arguments.w_anchor, edition, [paths["expect"], paths["queue"]]), "anchor")
    w_anchor = load_json(arguments.w_anchor)
    require(set(w_anchor) == set(W_ANCHOR_BASE_KEYS), "pin", "w-anchor 에 이미 일지 고정(journal_id)이 있다. 한 판·회차에 고정은 한 번이다(개정 I)")
    require(is_file(paths["journal"]), "journal", f"일지 없음: {paths['journal']}(먼저 calib_batch --init-journal)")
    journal = load_json(paths["journal"])
    final_batch = journal.get("final_batch") or {}
    require(is_unmeasured_journal(journal), "journal", "일지가 이미 시작·측정됐다(pin-journal 은 --init-journal 직후, 측정 전에만)")
    require((final_batch.get("edition"), final_batch.get("restart")) == (edition, expect["restart"]), "journal", "일지 판·restart ≠ 명세·expect")
    require(final_batch.get("anchor_sha256") == w_anchor["sha256"] == sha_of(paths["anchor"]), "journal", "일지 anchor_sha256 ≠ w-anchor sha256·지금 anchor.json")
    require((arguments.user_decision is None) == (arguments.e2_evidence is None), "input", "--user-decision 과 --e2-evidence 는 함께 준다(E2 뒤 재등록, 개정 J)")
    redo = {"user_decision": arguments.user_decision, "e2_evidence": normalized(arguments.e2_evidence)} if arguments.user_decision is not None else None
    pinned = pinned_w_anchor(w_anchor, journal, final_dir, redo)
    require_none(w_anchor_key_problems(pinned), "pin")
    history = w_anchor_history(arguments.w_anchor) + [{"committed_at": None, "content": pinned}]
    require_none(journal_pin_problems(pinned, history, journal, final_dir), "pin")
    w_anchor_out = normalized(arguments.w_anchor_out)
    if w_anchor_out != DEFAULT_W_ANCHOR:
        require_saved_output(w_anchor_out)
    write_json(w_anchor_out, pinned)
    print(f"{final_batch['journal_id']} {sha_of(w_anchor_out)}")
    return EXIT_OK


def lock_files_problems(lock_text, required_paths, gate):
    is_matching, problems, lock = prereg_lock.verify_lock(project_path(lock_text))
    if not is_matching:
        return lock, [f"{gate}: 잠금 불일치 {lock_text}: {'; '.join(problems)}"]
    files = lock.get("files") or {}
    return lock, [f"{gate}: 잠금 {lock_text} files 에 {path} 가 없다" for path in required_paths if normalized(path) not in files]


def expect_judge_files(expect):
    return judge_file_paths(expect["final_dir"], expect["overlay"]) + [f"{expect['final_dir']}/anchor.json"]


def compare_run(arguments):
    expect_text = normalized(arguments.expect)
    expect = load_json(expect_text)
    run_id = arguments.run
    run = expect["runs"].get(run_id)
    require(run is not None, "input", f"실행 {run_id} 가 expect 에 없다")
    is_f0 = run["block"] == "F0"
    require(bool(arguments.judge_lock) == is_f0, "input", "F0 는 --judge-lock 필수, F1 은 금지")
    lock_text = normalized(arguments.lock)
    require(lock_text == run["lock"], "input", f"--lock {lock_text} ≠ expect lock {run['lock']}")
    require_none(check_anchor(arguments.w_anchor, expect["edition"], [expect_text]), "anchor")
    gate_lock_text = normalized(arguments.judge_lock) if is_f0 else lock_text
    require(not is_f0 or gate_lock_text == run["judge_lock"], "lock", f"--judge-lock ≠ 파생 경로 {run['judge_lock']}", EXIT_DETERMINISM)
    gate_lock, problems = lock_files_problems(gate_lock_text, expect_judge_files(expect), "lock")
    require_none(problems, "lock", EXIT_DETERMINISM)
    sweep_text = normalized(arguments.sweep)
    sweep = load_json(sweep_text)
    require(prereg_lock.parse_moment(gate_lock["created_at"]) <= prereg_lock.parse_moment(sweep["started_at"]), "lock", "잠금 created_at > 스윕 started_at")
    try:
        prereg_lock.check_sweep_lock(sweep, project_path(lock_text))
    except prereg_lock.LockError as error:
        raise GateFailure("sweep", [str(error)])
    sweep_problems = []
    if sweep.get("lock_sha256") != sha_of(lock_text) or (is_f0 and sweep.get("lock_sha256") != run["lock_sha256"]):
        sweep_problems.append("스윕 lock_sha256 ≠ --lock sha(F0 는 expect lock_sha256)")
    if sweep.get("stage") != "verify":
        sweep_problems.append(f"스윕 stage {sweep.get('stage')} ≠ verify")
    for key, expected in (("plan", run["plan"]), ("plan_sha256", run["plan_sha256"]), ("overlay_sha256", run["overlay_sha256"]),
                          ("canonical_tuning_sha256", expect["canonical_after"]["tuning"]), ("canonical_look_sha256", expect["canonical_after"]["look"])):
        if (normalized(sweep.get(key, "")) if key == "plan" else sweep.get(key)) != expected:
            sweep_problems.append(f"스윕 {key} ≠ expect")
    require_none(sweep_problems, "sweep")
    rows = run_rows(expect, run_id)
    keys = sorted_adoptions({row["adoption"] for row in rows})
    anchor_text = f"{expect['final_dir']}/anchor.json"
    report = {"tool": TOOL_NAME, "version": TOOL_VERSION, "run": run_id, "edition": expect["edition"],
              "inputs": {"sweep": sweep_text, "sweep_sha256": sha_of(sweep_text), "lock": lock_text, "lock_sha256": sha_of(lock_text),
                         "judge_lock": gate_lock_text if is_f0 else None, "judge_lock_sha256": sha_of(gate_lock_text) if is_f0 else None,
                         "expect": expect_text, "expect_sha256": sha_of(expect_text), "anchor_sha256": sha_of(anchor_text)},
              "binding": [], "lock_file_differences": [], "invalid_count": sweep.get("invalid_count") or 0}
    if sweep.get("invalid_count"):
        report.update(scenes=[], checks=[], adoptions={key: "undetermined" for key in keys}, decomposition_ok=None, state="undetermined")
        return report, EXIT_FAILED
    binding_failures, differences = check_compare_binding(expect, run, sweep_lock_files(lock_text), report)
    require_none(binding_failures, "binding", EXIT_DETERMINISM)
    report["lock_file_differences"] = differences
    plan = load_json(run["plan"])
    try:
        scene_results, checks = rejudge(sweep, plan, rows, arguments.work_dir)
        decomposition_ok = decomposition_check(sweep, plan, rows, arguments.work_dir, checks)
    except calib_judge.JudgeError as error:
        raise GateFailure("rejudge", [str(error)], EXIT_DETERMINISM)
    allowed = {(row["scene"], row["metric"]): row["allowed"] for row in rows}
    rejudge_problems = [f"{check['scene']}.{check['metric']}: allowed {check['allowed']} ≠ expect {allowed.get((check['scene'], check['metric']))}"
                        for check in checks if (check["scene"], check["metric"]) not in allowed or check["allowed"] != allowed[(check["scene"], check["metric"])]]
    if {(check["scene"], check["metric"]) for check in checks} != set(allowed):
        rejudge_problems.append("checks (장면, 지표) 집합 ≠ 이 실행 행 집합")
    if decomposition_ok is False:
        rejudge_problems.append("분해 검사 실패: 출처별 다시 판정 checks 합집합 ≠ 합친 checks")
    require_none(rejudge_problems, "rejudge", EXIT_DETERMINISM)
    states = adoption_states(rows, scene_results, checks)
    report.update(scenes=scene_results, checks=checks, adoptions=states, decomposition_ok=decomposition_ok, state=worst_state(states.values()))
    return report, EXIT_OK if report["state"] == "pass" else EXIT_FAILED


def sweep_lock_files(lock_text):
    return load_json(lock_text).get("files") or {}


def check_compare_binding(expect, run, run_lock_files, report):
    problems, differences = [], []
    allowed = set(prereg_lock.JUDGE_TOOL_FILES) | set(expect_judge_files(expect)) | {run["plan"]}
    for source in run["sources"]:
        judge_text = source["judge"]
        judge = load_json(judge_text)
        source_problems = [] if sha_of(judge_text) == source["judge_sha256"] else [f"{source['adoption']}: 출처 judge sha ≠ expect"]
        source_problems += binding_problems(run["plan"], source, judge)
        source_lock = source_lock_path(judge)
        if not source_lock or not is_file(source_lock):
            source_problems.append(f"{source['adoption']}: 출처 잠금 {source_lock} 없음")
        else:
            source_files = load_json(source_lock).get("files") or {}
            source_allowed = allowed | {normalized(judge["plan"])} | ({normalized(source["binding"]["source_plan"])} if source["binding"].get("source_plan") else set())
            changed = sorted(path for path in set(source_files) | set(run_lock_files) if source_files.get(path) != run_lock_files.get(path))
            differences.extend({"adoption": source["adoption"], "path": path} for path in changed)
            source_problems += [f"{source['adoption']}: 출처 잠금과 실행 잠금 files 차이 {path} 가 허용 밖이다" for path in changed if path not in source_allowed]
        report["binding"].append({"adoption": source["adoption"], "kind": source["binding"].get("kind"), "ok": not source_problems})
        problems.extend(source_problems)
    return problems, differences


def decomposition_check(sweep, plan, rows, work_dir_text, merged_checks):
    groups = {}
    for row in rows:
        groups.setdefault((row["adoption"], row["source"]["judge"]), []).append(row)
    if len(groups) < 2:
        return None
    union = set()
    for index, group in enumerate(groups.values()):
        _, checks = rejudge(sweep, plan, group, f"{work_dir_text}/source-{index + 1}")
        union.update(canonical_text(check) for check in checks)
    return union == {canonical_text(check) for check in merged_checks}


def command_compare(arguments):
    out_text = require_saved_output(arguments.out)
    try:
        report, exit_code = compare_run(arguments)
    except GateFailure as failure:
        write_json(out_text, {"tool": TOOL_NAME, "version": TOOL_VERSION, "run": arguments.run, "gate": failure.gate, "problems": failure.problems})
        for line in failure.problems:
            print(f"관문 {failure.gate}: {line}", file=sys.stderr)
        return failure.exit_code
    write_json(out_text, report)
    print(f"compare {arguments.run}: {report['state']} {report['adoptions']}")
    return exit_code


def ab_condition_results(expect, ab, stage, result, sweep, lock_text):
    after = expect["canonical_after"]
    lock = load_json(lock_text)
    params = sweep.get("params") or []
    first = params[0] if params else {}
    return {"result_canonical_tuning": result.get("canonical_tuning_sha256") == after["tuning"], "result_canonical_look": result.get("canonical_look_sha256") == after["look"],
            "sweep_canonical_tuning": sweep.get("canonical_tuning_sha256") == after["tuning"], "sweep_canonical_look": sweep.get("canonical_look_sha256") == after["look"],
            "result_overlay_sha256": result.get("overlay_sha256") == ab["overlay_sha256"], "sweep_overlay_sha256": sweep.get("overlay_sha256") == ab["overlay_sha256"],
            "result_plan_sha256": result.get("plan_sha256") == ab["plan_sha256"], "sweep_plan_sha256": sweep.get("plan_sha256") == ab["plan_sha256"],
            "result_lock_sha256": result.get("lock_sha256") == sha_of(lock_text),
            "lock_before_sweep": bool(lock.get("created_at")) and bool(sweep.get("started_at")) and prereg_lock.parse_moment(lock["created_at"]) <= prereg_lock.parse_moment(sweep["started_at"]),
            "sweep_stage": sweep.get("stage") == stage, "single_param": len(params) == 1,
            "base_value": len(params) == 1 and same_value(first.get("base_value"), ab["adopted_value"]),
            "values": len(params) == 1 and isinstance(first.get("values"), list) and len(first["values"]) == 1 and same_value(first["values"][0], ab["before_value"])}


def check_ab_document(expect_text, expect, ab_id, stage, result_text, sweep_text, lock_text, w_anchor_text):
    ab = next((item for item in expect["ab"] if item["id"] == ab_id), None)
    require(ab is not None, "input", f"A/B {ab_id} 가 expect 에 없다")
    require(stage in AB_STAGES, "input", f"--stage {stage} 모름")
    require_none(check_anchor(w_anchor_text, expect["edition"], [expect_text]), "anchor")
    lock_text = normalized(lock_text)
    require(lock_text == ab["lock"], "lock", f"--lock ≠ 파생 경로 {ab['lock']}", EXIT_DETERMINISM)
    _, problems = lock_files_problems(lock_text, expect_judge_files(expect), "lock")
    require_none(problems, "lock", EXIT_DETERMINISM)
    conditions = ab_condition_results(expect, ab, stage, load_json(result_text), load_json(sweep_text), lock_text)
    require_none([f"측정 조건 거짓: {name}" for name, passed in sorted(conditions.items()) if not passed], "conditions")
    return {"tool": TOOL_NAME, "version": TOOL_VERSION, "ab": ab_id, "stage": stage,
            "inputs": {"result": normalized(result_text), "result_sha256": sha_of(result_text), "sweep": normalized(sweep_text), "sweep_sha256": sha_of(sweep_text),
                       "lock": lock_text, "lock_sha256": sha_of(lock_text)}, "conditions": conditions}


def command_check_ab(arguments):
    out_text = require_saved_output(arguments.out)
    expect_text = normalized(arguments.expect)
    try:
        report = check_ab_document(expect_text, load_json(expect_text), arguments.ab, arguments.stage, arguments.result, arguments.sweep, arguments.lock, arguments.w_anchor)
    except GateFailure as failure:
        write_json(out_text, {"tool": TOOL_NAME, "version": TOOL_VERSION, "ab": arguments.ab, "gate": failure.gate, "problems": failure.problems})
        for line in failure.problems:
            print(f"관문 {failure.gate}: {line}", file=sys.stderr)
        return failure.exit_code
    write_json(out_text, report)
    print(f"check-ab {arguments.ab} {arguments.stage}: 조건 {len(report['conditions'])}개 참")
    return EXIT_OK


def journal_attempts(journal):
    return [(index, attempt) for index, item in enumerate(journal.get("items") or []) for attempt in item_final(item).get("attempts") or []]


def judge_tool_failure(journal):
    for index, attempt in journal_attempts(journal):
        step, exit_code, state = attempt.get("step"), attempt.get("exit"), attempt.get("state")
        if step in LOCK_CREATE_STEPS + LOCK_VERIFY_STEPS and state in ("done", "timeout") and exit_code != 0:
            return {"event": "E2", "item": index, "reason": f"{step} 종료 {exit_code}"}
        if step == "sweep" and (exit_code == 2 or exit_code not in (0, 1, None)):
            return {"event": "E2", "item": index, "reason": f"sweep 종료 {exit_code}"}
        if step in JUDGE_STEPS and state in ("done", "timeout") and exit_code not in (0, 1):
            return {"event": "E2", "item": index, "reason": f"{step} 종료 {exit_code}({state})"}
    final_batch = journal.get("final_batch") or {}
    for event in [final_batch.get("outcome") or {}, *(final_batch.get("events") or [])]:
        if event.get("event") == "E2":
            return {"event": "E2", "item": event.get("item"), "reason": event.get("reason")}
    return None


def ab_by_id(expect):
    return {ab["id"]: ab for ab in expect["ab"]}


def queue_items(queue):
    return {normalized(item["plan"]): item for block in queue["blocks"] for item in block["plans"]}


def item_lock_problems(item, queue_item, queue, w_anchor, expect):
    final = item_final(item)
    problems = []
    locks = []
    if final.get("kind") == "verify" and queue_item.get("judge_lock"):
        is_matching, lines, _ = prereg_lock.verify_lock(project_path(queue_item["lock"]))
        if not is_matching:
            problems.append(f"원래 잠금 불일치 {queue_item['lock']}: {'; '.join(lines)}")
        locks.append(queue_item["judge_lock"])
    else:
        locks.append(queue_item["lock"])
    sweep_starts = [attempt["started_at"] for attempt in final.get("attempts") or [] if attempt.get("step") == "sweep" and attempt.get("started_at")]
    anchor_text = f"{expect['final_dir']}/anchor.json"
    for lock_text in locks:
        if not is_file(lock_text):
            if sweep_starts:
                problems.append(f"잠금 없음 {lock_text}")
            continue
        is_matching, lines, lock = prereg_lock.verify_lock(project_path(lock_text))
        if not is_matching:
            problems.append(f"잠금 불일치 {lock_text}: {'; '.join(lines)}")
            continue
        files = lock.get("files") or {}
        problems += [f"잠금 {lock_text} 판정 파일 {path} sha ≠ 큐 judge_files" for path, recorded in sorted(queue["final"]["judge_files"].items()) if files.get(path) != recorded]
        if files.get(anchor_text) != w_anchor["sha256"]:
            problems.append(f"잠금 {lock_text} 의 anchor.json sha ≠ w-anchor sha256")
        created = prereg_lock.parse_moment(lock["created_at"])
        problems += [f"잠금 {lock_text} created_at > 스윕 started_at {start}" for start in sweep_starts if created > prereg_lock.parse_moment(start)]
    return problems


def item_artifact_problems(item):
    final = item_final(item)
    problems = [f"산출물 sha 다름 또는 없음: {path}" for path, recorded in sorted((final.get("artifact_sha256") or {}).items()) if sha_or_none(path) != recorded]
    problems += [f"끝난 시도 {attempt.get('step')} 의 산출물 {attempt['output']} 이 없거나 sha 가 시도 끝 기록과 다르다(개정 I)" for attempt in final.get("attempts") or []
                 if attempt.get("output_sha256") and sha_or_none(attempt.get("output") or "-") != attempt["output_sha256"]]
    sweeps = [attempt for attempt in final.get("attempts") or [] if attempt.get("step") == "sweep"]
    for attempt in sweeps:
        is_completed = bool(attempt.get("folder")) and is_completed_sweep(normalized(attempt["folder"]))
        if attempt.get("state") == "interrupted" and is_completed:
            problems.append(f"끝난 스윕(sweep.json invalid_count 0)을 interrupted 로 세고 다시 찍었다(다시 추첨, 개정 H): {attempt['folder']}")
        if attempt.get("state") == "recovered" and (attempt.get("exit") != 0 or not is_completed):
            problems.append(f"recovered 스윕인데 sweep.json 이 끝난 결과(invalid_count 0)가 아니거나 종료가 0 이 아니다: {attempt.get('folder')}")
    if final.get("kind") == "verify":
        if len(sweeps) > 4 or sum(1 for attempt in sweeps if attempt.get("exit") == 0) > 2:
            problems.append("verify 항목 sweep 시도 횟수 한도(시도 ≤ 4, 종료 0 ≤ 2) 초과")
    else:
        for stage in AB_STAGES:
            if sum(1 for attempt in sweeps if attempt.get("stage") == stage) > 2:
                problems.append(f"A/B {stage} sweep 시도 > 2")
    return problems


def run_folders_in(final_dir, edition):
    base = project_path(final_dir)
    folders = set()
    for child in sorted(base.iterdir()) if base.is_dir() else []:
        if not child.is_dir() or child.name.startswith("aborted-") or (edition == "e1" and child.name == "r2"):
            continue
        folders.update(normalized(path) for path in child.rglob("*") if path.is_dir() and RUN_FOLDER_PATTERN.match(path.name) and path.parent.parent.name == "runs")
    return folders


def journal_lock_paths(journal, queue):
    by_plan = queue_items(queue)
    paths = set()
    for item in journal.get("items") or []:
        steps = {attempt.get("step") for attempt in item_final(item).get("attempts") or []}
        queue_item = by_plan.get(normalized(item.get("plan", ""))) or {}
        if steps & set(LOCK_CREATE_STEPS + LOCK_VERIFY_STEPS):
            paths |= {normalized(queue_item[key]) for key in ("lock", "judge_lock") if queue_item.get(key)}
    return paths


def journal_binding_problems(expect, queue, journal):
    journal_id = (journal.get("final_batch") or {}).get("journal_id")
    if not isinstance(journal_id, str) or not journal_id:
        return ["일지 final_batch.journal_id 가 없다(잠금·실행 폴더를 일지에 묶지 못한다)"]
    referenced = journal_lock_paths(journal, queue)
    locks_dir = project_path(f"{expect['final_dir']}/locks")
    problems = []
    for path in sorted(locks_dir.iterdir()) if locks_dir.is_dir() else []:
        text = normalized(path)
        if not path.is_file():
            continue
        if text not in referenced:
            problems.append(f"일지가 쓰지 않은 잠금 {text} 이 final_dir 에 있다(결과 뒤 다시 추첨 의심, 개정 H)")
            continue
        try:
            note = load_json(text).get("note")
        except (OSError, ValueError, AttributeError):
            note = None
        if not isinstance(note, str) or f"일지 {journal_id}" not in note:
            problems.append(f"잠금 {text} note 에 이 일지 id 가 없다(다른 일지가 만든 잠금, 개정 H)")
    return problems


def is_completed_sweep(folder_text):
    try:
        sweep = load_json_or_none(f"{folder_text}/sweep.json")
    except (OSError, ValueError):
        return False
    return isinstance(sweep, dict) and bool(sweep.get("scenes")) and sweep.get("invalid_count") == 0


def folder_set_problems(journal, final_dir, edition):
    recorded = {}
    for index, attempt in journal_attempts(journal):
        if attempt.get("step") != "sweep" or not attempt.get("folder"):
            continue
        folder = normalized(attempt["folder"])
        if attempt.get("exit") is None and not is_file(f"{folder}/sweep.json") and not project_path(folder).is_dir():
            continue
        recorded[folder] = index
    on_disk = run_folders_in(final_dir, edition)
    problems = []
    for folder in sorted(on_disk ^ set(recorded)):
        index = recorded.get(folder)
        if index is None:
            stem = PurePosixPath(folder).parent.name
            index = next((position for position, item in enumerate(journal.get("items") or []) if PurePosixPath(item.get("plan", "")).stem == stem), None)
        problems.append((index, f"디스크 실행 폴더 ≠ 일지 sweep 시도 folder: {folder}"))
    return problems


def referenced_folders(item):
    final = item_final(item)
    folders = [attempt["folder"] for attempt in final.get("attempts") or [] if attempt.get("folder")]
    folders += [value for key, value in (item.get("runs") or {}).items() if key in ("verify", "screen", "confirm") and value]
    return folders


def target_state(expect, decision_states, statuses, key):
    covered = next((item for item in expect["covered"] if item["adoption"] == key), None)
    pairs = [(covered["by_run"], covered["by_adoption"])] if covered else sorted({(row["run"], key) for row in expect["rows"] if row["adoption"] == key})
    states = []
    for run_id, adoption in pairs:
        if statuses.get(run_id) == "incomplete":
            states.append("incomplete")
            continue
        state = (decision_states.get(run_id) or {}).get(adoption)
        if state is None:
            return None
        states.append(state)
    return worst_state(states)


def ab_documents(item, stage):
    outcome, folder = opportunity_outcome(item_final(item), stage, None)
    if outcome != "done":
        return outcome, None, None, None
    name = "screen.json" if stage == "screen" else "judge.json"
    return outcome, folder, load_json(f"{folder}/{name}"), load_json(f"{folder}/sweep.json")


def recompute_ab_item(expect, item, queue_item, decision_states, statuses):
    final = item_final(item)
    ab = ab_by_id(expect)[final["ab_id"]]
    trigger_state = (decision_states.get(ab["for_run"]) or {}).get(ab["trigger_adoption"])
    if statuses.get(ab["for_run"]) == "incomplete":
        trigger_state = "incomplete"
    for_state = target_state(expect, decision_states, statuses, ab["for_adoption"])
    result = {"ab": ab, "trigger_state": trigger_state, "decision": None, "status": None, "problems": [], "evidence": []}
    if trigger_state is None or for_state is None:
        return result
    if "incomplete" in (trigger_state, for_state) or (ab["trigger"] == "on_fail" and trigger_state == "pass"):
        return dict(result, decision="ab_skipped", status="ab_skipped")
    outcome, folder, screen, sweep = ab_documents(item, "screen")
    if outcome != "done":
        return dict(result, decision="ab_incomplete", status="ab_done") if outcome == "infeasible" else result
    result["evidence"].append(f"{folder}/screen.json")
    conditions = ab_condition_results(expect, ab, "screen", screen, sweep, queue_item["lock"])
    result["problems"] += [f"{ab['id']} screen 측정 조건 거짓: {name}" for name, passed in sorted(conditions.items()) if not passed]
    if not all(scene.get("control_ok") for scene in screen.get("scenes") or []):
        return dict(result, decision="ab_undetermined", status="ab_done")
    if screen.get("insensitive") or not screen.get("best"):
        return dict(result, decision="ab_keep", status="ab_done")
    outcome, folder, judge, sweep = ab_documents(item, "confirm")
    if outcome != "done":
        return dict(result, decision="ab_incomplete", status="ab_done") if outcome == "infeasible" else result
    result["evidence"].append(f"{folder}/judge.json")
    result["confirm"] = judge
    conditions = ab_condition_results(expect, ab, "confirm", judge, sweep, queue_item["lock"])
    result["problems"] += [f"{ab['id']} confirm 측정 조건 거짓: {name}" for name, passed in sorted(conditions.items()) if not passed]
    if not all(scene.get("control_ok") for scene in judge.get("scenes") or []):
        return dict(result, decision="ab_undetermined", status="ab_done")
    adopt = judge.get("adopt") or {}
    if adopt.get("decision") and adopt.get("variant") != "v01":
        result["problems"].append(f"{ab['id']}: confirm adopt variant {adopt.get('variant')} ≠ v01(P-24)")
    return dict(result, decision="ab_v01" if adopt.get("decision") else "ab_keep", status="ab_done")


def decide_adoption(key, state, trigger, decision):
    if trigger is None:
        if state == "pass":
            return "keep", ["R-W8"], "통과"
        if state == "incomplete":
            return "incomplete", ["R-W20"], "최종 검증 미완 — 실행 불가"
        return "user_decision", ["R-W8"], "최종 검증 실패·판정 불가 — 자기 A/B 없음"
    if state == "incomplete" and decision == "ab_skipped":
        return "incomplete", ["R-W20"], "최종 검증 미완 — 실행 불가"
    if state == "pass" and trigger == "on_fail" and decision == "ab_skipped":
        return "keep", ["R-W8"], "통과"
    if state == "pass" and trigger == "always_record":
        if decision in ("ab_keep", "ab_undetermined"):
            return "keep", ["R-W9"], f"통과(기록용 A/B {decision})"
        if decision == "ab_v01":
            return "user_decision", ["R-W9"], "재현했으나 채택 전 값이 더 좋음"
        if decision == "ab_incomplete":
            return "incomplete", ["R-W20", "P-17"], "최종 검증 미완 — 기록용 A/B 실행 불가"
    if state in ("fail", "undetermined"):
        if decision == "ab_v01":
            return "revert", ["R-W8"], "A/B 확정 — 채택 전 값이 유의하게 나음"
        if decision == "ab_keep" and key == CHAIN_SUSPECT_ADOPTION:
            return "user_decision", ["R-W9", "A3"], "사슬 의심(㉛) — 뒤 안개 채택 f7(17:33) → f2(17:12) → f1(17:02) 순 / 재현 근거 없음(유지)"
        if decision == "ab_keep" and state == "fail":
            return "user_decision", ["R-W8", "A3"], "재현 실패 — W 경로 점검 / 재현 근거 없음(유지)"
        if decision == "ab_keep":
            return "user_decision", ["R-W8", "A3"], "최종 검증 판정 불가 — A/B 유지 / 재현 근거 없음(유지)"
        if decision == "ab_undetermined":
            return "user_decision", ["D-W26", "A3"], "최종 검증 판정 불가 — A/B 판정 불가 / 재현 근거 없음(유지)"
        if decision == "ab_incomplete":
            return "incomplete", ["R-W20"], "최종 검증 미완 — A/B 실행 불가"
    raise GateFailure("recompute", [f"{key}: 결정 상태 {state} 와 자기 wab {trigger} {decision} 조합은 결정 표에 없다(A1 다시 계산 불일치)"])


def constraint_resolved(judge):
    for scene in (judge or {}).get("scenes") or []:
        medians = ((scene.get("variants") or {}).get("v01") or {}).get("medians") or {}
        for target in scene.get("targets") or []:
            if target.get("role") != calib_judge.ROLE_CONSTRAINT:
                continue
            if not calib_judge.is_within_band((scene.get("control_reference") or {}).get(target["metric"]), target) and calib_judge.is_within_band(medians.get(target["metric"]), target):
                return True
    return False


def decide_influence(key, state, cross, recomputed, target_run):
    decision = recomputed["decision"] if recomputed else None
    if state == "pass" and decision in ("ab_skipped", None):
        return []
    if state == "incomplete" and decision in ("ab_skipped", None):
        return [(key, "incomplete", ["R-W20"], "최종 검증 미완 — 실행 불가")]
    if state in ("fail", "undetermined"):
        if cross is None:
            return [(key, "user_decision", ["R-W10"], "영향 장면 실패 — 교차 A/B 없음")]
        if decision == "ab_v01" and constraint_resolved(recomputed.get("confirm")):
            return [(cross["for_adoption"], "revert", ["R-W10"], f"교차 A/B 확정 — {key} 제약 위반 해소")]
        if decision == "ab_v01":
            return [(cross["for_adoption"], "user_decision", ["R-W10"], f"목표 충돌({key} 대 {target_run})")]
        if decision == "ab_keep":
            return [(key, "user_decision", ["R-W10"], "원인 미상 — W 경로 점검")]
        if decision == "ab_undetermined":
            return [(key, "user_decision", ["R-W10"], "최종 검증 판정 불가 — A/B 판정 불가")]
        if decision == "ab_incomplete":
            return [(key, "incomplete", ["R-W20"], "최종 검증 미완 — A/B 실행 불가")]
    raise GateFailure("recompute", [f"{key}: 영향 상태 {state} 와 교차 A/B {decision} 조합은 결정 표에 없다(A1 다시 계산 불일치)"])


def label_of_tests_file(text):
    name = PurePosixPath(normalized(text)).name
    require(name.endswith("-tests.json"), "tests", f"--tests {text}: 파일 이름이 <label>-tests.json 이 아니다")
    return name[:-len("-tests.json")]


def tests_inputs(arguments, required_labels):
    inputs = {}
    for text in arguments.tests or []:
        label = label_of_tests_file(text)
        require(label not in inputs, "tests", f"시험 label {label} 입력이 겹친다")
        inputs[label] = {"path": normalized(text), "base_error": None}
    for text in arguments.tests_base_error or []:
        label, separator, evidence = text.partition("=")
        require(separator and evidence, "tests", f"--tests-base-error {text}: <label>=<근거 파일> 꼴이어야 한다")
        require(label not in inputs, "tests", f"시험 label {label} 입력이 겹친다")
        require(is_file(evidence), "tests", f"--tests-base-error {label}: 근거 파일 없음 {evidence}")
        inputs[label] = {"path": None, "base_error": normalized(evidence)}
    require(set(inputs) == set(required_labels), "tests", f"시험 입력 label {sorted(inputs)} ≠ 필수 {sorted(required_labels)}")
    return inputs


def error_lines(events):
    return [event for event in events or [] if isinstance(event, str) and event.startswith("Error")]


def evaluate_tests(arguments, expect, phase, adoption_by_entry, outcomes, w_defects, test_classes):
    required = expect["tests_required"][phase]
    inputs = tests_inputs(arguments, required)
    family = TEST_FAMILY_PHASES[phase]
    family_labels = set(expect["tests_required"][family[0]]) - set(expect["tests_required"][family[1]]) if family else set()
    owner_rows = {row["test"]: row for row in load_json(expect["test_owner"]["path"])["rows"]}
    all_owners = sorted_adoptions({adoption_by_entry[owner] for row in owner_rows.values() for owner in row["owners"] if owner in adoption_by_entry})
    report = {}
    phrases_by_test = test_classes.get("numeric_pin_when_failures_match") or {}
    for label, entry in sorted(inputs.items()):
        document = load_json(entry["path"]) if entry["path"] else None
        is_base_error = entry["base_error"] is not None or bool(document and (document.get("incomplete") or document.get("crashed")))
        report[label] = {"path": entry["path"], "base_error": entry["base_error"] or (entry["path"] if is_base_error else None), "failed": []}
        if is_base_error:
            require(label in family_labels, "tests", f"시험 label {label} 기반 오류는 W 를 멈췄어야 한다(W4·W10 계열)")
            for key in all_owners:
                outcomes.setdefault(key, []).append(("incomplete", ["D3"], "W13 시험 기반 오류 2회", [report[label]["base_error"]]))
            continue
        for result in document.get("results") or []:
            if result.get("result") == "Success":
                continue
            name = result.get("path")
            row = owner_rows.get(name) or {"class": "design", "owners": []}
            test_class = row["class"]
            if test_class == "numeric_pin_when_failures_match":
                errors = error_lines(result.get("events"))
                phrases = phrases_by_test.get(name) or []
                test_class = "numeric_pin" if errors and all(any(phrase in line for phrase in phrases) for line in errors) else "design"
            owners = sorted_adoptions(adoption_by_entry[owner] for owner in row["owners"] if owner in adoption_by_entry)
            if test_class in ("timing_record_only", "numeric_pin"):
                outcome = "recorded"
            elif name == HEADER_SYNC_TEST:
                outcome = "w_defect"
                w_defects.append({"test": name, "label": "W 결함 — 헤더 기본값 동기 시험 실패(P-27)", "evidence": [entry["path"]]})
            elif len(owners) == 1:
                outcome = "revert"
                outcomes.setdefault(owners[0], []).append(("revert", ["R-W15"], f"설계 단언 실패 — {name}", [entry["path"]]))
            else:
                outcome = "user_decision"
                for key in owners + [None]:
                    outcomes.setdefault(key, []).append(("user_decision", ["R-W15"], f"설계 단언 실패 — {name}(소유 {len(owners)}개)", [entry["path"]]))
            report[label]["failed"].append({"test": name, "class": test_class, "owners": owners, "outcome": outcome})
    return report


def evaluate_user_approved(text, expect, adoption_by_entry, outcomes):
    items = load_json(text)
    require(isinstance(items, list), "user_approved", "--user-approved 는 목록이어야 한다")
    for item in items:
        identifier = item.get("json_path") or (fluid_tuning_patch.describe_actor_prop(item["actor_prop"]) if isinstance(item.get("actor_prop"), dict) else None)
        require(identifier in adoption_by_entry, "user_approved", f"사용자 승인 {identifier} 이 채택에 대응하지 않는다")
        require(item.get("rule") == "user_approved" and item.get("evidence") and is_file(item["evidence"]), "user_approved", f"사용자 승인 {identifier}: rule user_approved·근거 파일 필요")
        outcomes.setdefault(adoption_by_entry[identifier], []).append(("revert", ["user_approved"], "사용자 승인 되돌림", [normalized(item["evidence"])]))


def evaluate_p22(expect, ns_dump_text, decision_states):
    p22 = expect["p22"]
    require(p22.get("p22_pre") is True, "p22", "expect p22_pre 가 참이 아니다")
    require(ns_dump_text, "p22", "--ns-dump 가 필요하다(expect p22 있음)")
    dump = load_json(ns_dump_text)
    floats = {}
    for variable in ((dump.get("sections") or {}).get("UserVariables") or {}).get("userVariables") or []:
        if ((variable.get("type") or {}).get("classStructOrEnum") or {}).get("refPath") == "/Script/Niagara.NiagaraFloat":
            floats[variable.get("name")] = ((variable.get("defaultValue") or {}).get("value") or {}).get("value")
    expected = (load_json(expect["w_expect"]["path"])["ns_expect"].get(p22["ns_package"]) or {}).get("Floats") or {}
    problems = [f"P-22 NS 덤프 {name}: {floats.get(name)} ≠ 기대 {value}" for name, value in sorted(expected.items())
                if not is_number(floats.get(name)) or fluid_material_look.to_float32(floats[name]) != fluid_material_look.to_float32(value)]
    require_none(problems, "p22")
    return {"p22_pre": True, "ns_dump_equal": True, "runs": {run_id: decision_states.get(run_id) for run_id in p22["runs"]}}


def r2_targets(expect, reverted_ids):
    reverted = {number for number, info in expect["adoptions"].items() if info["entry_id"] in reverted_ids}
    run_plans = dict(expect["runs"], **expect.get("fallback_runs", {}))
    levels = scene_level_map(run_plans)
    all_rows = expect["rows"] + expect.get("fallback_rows", [])
    profiles = {number: {"scenes": set(info["scenes"]), "levels": set(info["levels"]), "owner": info["owner"]} for number, info in expect["adoptions"].items()}
    for influence in expect["influences"]:
        own = [row for row in all_rows if row["adoption"] == influence]
        profiles[influence] = {"scenes": {row["scene"] for row in own}, "levels": {levels.get((row["run"], row["scene"])) for row in own} - {None}, "owner": None}
    relations = {(item["adoption"], item["by_adoption"]) for item in expect["covered"]} | {(item["by_adoption"], item["adoption"]) for item in expect["covered"]}
    targets = []
    for key, profile in profiles.items():
        if key in reverted:
            continue
        for other_key in reverted:
            other = profiles[other_key]
            if profile["scenes"] & other["scenes"] or profile["levels"] & other["levels"] or (profile["owner"] and profile["owner"] == other["owner"]) or (key, other_key) in relations:
                targets.append(key)
                break
    for item in expect["covered"]:
        if item["adoption"] in targets and item["by_adoption"] not in reverted and item["by_adoption"] not in targets:
            targets.append(item["by_adoption"])
    runs = set()
    for key in targets:
        covered = next((item for item in expect["covered"] if item["adoption"] == key), None)
        if covered:
            runs.add(covered["fallback"]["id"] if covered["by_adoption"] in reverted else covered["by_run"])
            continue
        runs.update(row["run"] for row in expect["rows"] if row["adoption"] == key)
    return {"adoptions": sorted_adoptions(key for key in targets if key in expect["adoptions"]), "runs": sorted(runs),
            "influences": sorted(key for key in targets if key in expect["influences"])}


def measured_keys(expect):
    keys = {row["adoption"] for row in expect["rows"]} | {item["adoption"] for item in expect["covered"]}
    return sorted_adoptions(keys)


def verdict_report_base(arguments, expect):
    return {"tool": TOOL_NAME, "version": TOOL_VERSION, "phase": arguments.phase, "edition": PHASE_EDITIONS.get(arguments.phase), "restart": (expect or {}).get("restart"),
            "inputs": {}, "stop": None, "gate": None, "problems": [], "self_check": {}, "adoptions": {}, "revert": [], "user_decision": [], "incomplete": [], "w_defect": [],
            "post_hoc": [], "settled": {}, "tests": {}, "p22": None, "r2_targets": None}


def incomplete_all(expect, reason, rules):
    return [{"adoption": key, "reason": reason, "rules": rules, "evidence": []} for key in (measured_keys(expect) if expect else [])]


def verdict_gate_report(arguments, expect, failure):
    report = verdict_report_base(arguments, expect)
    report.update(gate=failure.gate, problems=failure.problems, incomplete=incomplete_all(expect, f"verdict 관문 {failure.gate} 실패", ["B1"]))
    return report


def verdict_inputs(arguments, expect, phase, edition):
    require(expect is not None, "input", "expect.json 을 읽지 못했다")
    require(expect.get("edition") == edition, "input", f"phase {phase} 는 판 {edition}, expect 판 {expect.get('edition')}")
    require_none(f0_shortage_problems(expect["runs"], edition), "input")
    final_dir = expect["final_dir"]
    paths = derived_paths(final_dir, edition)
    require(normalized(arguments.expect) == paths["expect"] and normalized(arguments.queue) == paths["queue"], "input", "--expect·--queue 가 파생 경로와 다르다")
    queue = load_json(paths["queue"])
    require(normalized(arguments.journal) == normalized(queue["final"]["journal"]) == paths["journal"], "input", "--journal ≠ 큐 final.journal")
    require(not arguments.user_approved or phase == "w13", "input", "--user-approved 는 w13 에서만 받는다")
    require(expect.get("p22") is None or arguments.ns_dump, "input", "expect p22 가 있으면 --ns-dump 필수")
    return paths, queue, load_json(paths["journal"])


def verdict_anchor_and_time(arguments, expect, paths, journal, edition):
    required = [paths["expect"], paths["queue"], expect["spec"], expect["w_expect"]["path"], expect["test_owner"]["path"]]
    require_none(check_anchor(arguments.w_anchor, edition, required), "anchor")
    anchor = load_json(paths["anchor"])
    for argument, path in ((arguments.anchor_spec_sha, expect["spec"]), (arguments.anchor_expect_sha, paths["expect"]), (arguments.anchor_queue_sha, paths["queue"])):
        require(argument is None or anchor["files"].get(path) == argument, "anchor", f"--anchor-*-sha 가 닻의 {path} sha 와 다르다")
    commit_time = anchor_commit_time(arguments.w_anchor)
    starts = [prereg_lock.parse_moment(attempt["started_at"]) for _, attempt in journal_attempts(journal) if attempt.get("started_at")]
    require(not starts or commit_time <= min(starts), "time", "w-anchor 커밋 시각 > 일지 첫 시도 started_at")
    if edition == "e1" and expect["restart"] == 0:
        write_canonical = f"{expect['final_dir']}/write-canonical.json"
        require(is_file(write_canonical) and commit_time <= file_mtime(write_canonical), "time", "w-anchor 커밋 시각 > W3 write-canonical.json mtime(또는 없음)")
    return anchor, commit_time


def verdict_journal_problems(expect, paths, queue, journal, anchor, edition):
    final_batch = journal.get("final_batch") or {}
    problems = []
    if journal.get("queue_sha256") != sha_of(paths["queue"]):
        problems.append("일지 queue_sha256 ≠ 큐 sha")
    for key in ("edition", "restart"):
        values = {final_batch.get(key), queue["final"].get(key), expect.get(key), anchor.get(key)}
        if len(values) != 1:
            problems.append(f"{key} 가 일지·큐·expect·anchor 에서 다르다: {sorted(map(str, values))}")
    if final_batch.get("anchor_sha256") != sha_of(paths["anchor"]):
        problems.append("일지 final_batch.anchor_sha256 ≠ 지금 anchor.json sha")
    problems += journal_binding_problems(expect, queue, journal)
    journal_dir = parent_text(paths["journal"])
    for index, item in enumerate(journal.get("items") or []):
        if item_final(item).get("edition") != edition:
            problems.append(f"항목 {index} final.edition ≠ {edition}")
        problems += [f"항목 {index} 실행 폴더 {folder} 가 일지 폴더 밖이다(판 섞임)" for folder in referenced_folders(item) if not normalized(folder).startswith(journal_dir + "/")]
    outcome = final_batch.get("outcome") or {}
    starts = final_batch.get("starts") or []
    if outcome.get("event") == "start_failed" and (len(starts) < 2 or any(start.get("ok") is not False for start in starts[-2:])):
        problems.append("outcome start_failed 인데 마지막 두 시작이 모두 실패가 아니다")
    gate = final_batch.get("f0_gate") or {}
    if outcome.get("event") == "F0_shortfall" and (gate.get("passed") is not False or len(gate.get("sample") or []) >= F0_REQUIRED_SELF_CHECKS):
        problems.append("outcome F0_shortfall 인데 f0_gate 가 결원을 보이지 않는다")
    if edition == "e1" and expect["restart"] == 1:
        archives = archive_directories(expect["final_dir"])
        if len(archives) != 1:
            problems.append(f"기록 폴더 수 {len(archives)} ≠ 1")
        else:
            problems += restart_justification_problems(expect["final_dir"], expect, archives[0])[0]
    return problems


def evaluate_items(expect, queue, journal, w_anchor):
    by_plan = queue_items(queue)
    item_problems = {}
    verify_results, ab_results = {}, {}
    decision_states, statuses = {}, {}
    for index, item in enumerate(journal.get("items") or []):
        queue_item = by_plan.get(normalized(item.get("plan", "")))
        if queue_item is None:
            item_problems.setdefault(index, []).append(("journal", f"항목 {index} 계획 {item.get('plan')} 이 큐에 없다"))
            continue
        item_problems.setdefault(index, []).extend(("locks", line) for line in item_lock_problems(item, queue_item, queue, w_anchor, expect))
        item_problems[index].extend(("artifacts", line) for line in item_artifact_problems(item))
        if item_final(item).get("kind") != "verify" or item.get("status") == "failed":
            continue
        try:
            result = recompute_verify_item(expect, item, identity_resolver)
        except INPUT_ERRORS as error:
            item_problems[index].append(("recompute", f"항목 {index} 산출물로 다시 계산하지 못했다: {type(error).__name__}: {error}"))
            continue
        verify_results[index] = result
        item_problems[index].extend(("recompute", line) for line in result["problems"] + verify_journal_differences(item, result))
        if result["decision"] is not None and item.get("status") in VERIFY_TERMINAL:
            decision_states[result["run"]] = result["decision"]
            statuses[result["run"]] = item.get("status")
    for index, item in enumerate(journal.get("items") or []):
        if item_final(item).get("kind") != "ab" or item.get("status") == "failed" or any(gate == "journal" for gate, _ in item_problems.get(index, [])):
            continue
        try:
            result = recompute_ab_item(expect, item, by_plan[normalized(item["plan"])], decision_states, statuses)
        except INPUT_ERRORS as error:
            item_problems[index].append(("recompute", f"항목 {index} A/B 산출물로 다시 계산하지 못했다: {type(error).__name__}: {error}"))
            continue
        ab_results[index] = result
        item_problems[index].extend(("recompute", line) for line in result["problems"] + ab_journal_differences(item, result))
    return item_problems, verify_results, ab_results, decision_states, statuses


def verify_journal_differences(item, result):
    final = item_final(item)
    status = item.get("status")
    problems = []
    if status == "failed":
        return problems
    if result["status"] is not None and status != result["status"] and status in VERIFY_TERMINAL + ("self_check_mismatch",):
        problems.append(f"{result['run']}: 일지 상태 {status} ≠ 다시 계산 {result['status']}")
    if status in VERIFY_TERMINAL + ("self_check_mismatch",) and result["status"] is None:
        problems.append(f"{result['run']}: 일지는 끝 상태 {status} 인데 산출물로 끝 상태를 다시 계산하지 못한다")
    if result["is_f0"] and canonical_text(final.get("self_check") or dict.fromkeys(OPPORTUNITIES)) != canonical_text(result["self_check"]):
        problems.append(f"{result['run']}: 일지 self_check {final.get('self_check')} ≠ 파일 다시 계산 {result['self_check']}")
    if not result["is_f0"] and final.get("self_check") is not None:
        problems.append(f"{result['run']}: F1 self_check 는 null 이어야 한다")
    if result["mismatch"]:
        return problems
    if result["first_states"] is not None and final.get("adoptions_first") != result["first_states"]:
        problems.append(f"{result['run']}: 일지 adoptions_first ≠ 다시 계산")
    if result["decision"] is not None and final.get("adoptions") != result["decision"]:
        problems.append(f"{result['run']}: 일지 adoptions(결정 상태) ≠ 다시 계산")
    if result["first_states"] is not None and final.get("rerun", 0) != result["rerun"]:
        problems.append(f"{result['run']}: 일지 rerun ≠ 다시 계산")
    if result["is_f0"]:
        for opportunity, key in (("first", "official_first"), ("rerun", "official_rerun")):
            official = result["opportunities"][opportunity].get("official_states")
            if official is not None and final.get(key) != official:
                problems.append(f"{result['run']}: 일지 {key} ≠ 공식 verify.json R-W4 다시 계산")
    return problems


def ab_journal_differences(item, result):
    final = item_final(item)
    status = item.get("status")
    if status == "failed":
        return []
    problems = []
    if result["status"] is None:
        if status in AB_TERMINAL:
            problems.append(f"{final.get('ab_id')}: 일지는 끝 상태 {status} 인데 다시 계산하지 못한다")
        return problems
    if status in AB_TERMINAL and (status != result["status"] or final.get("decision") != result["decision"]):
        problems.append(f"{final.get('ab_id')}: 일지 상태·결정 {status}·{final.get('decision')} ≠ 다시 계산 {result['status']}·{result['decision']}")
    if status in AB_TERMINAL and final.get("trigger_state") != result["trigger_state"]:
        problems.append(f"{final.get('ab_id')}: 일지 trigger_state ≠ 다시 계산")
    return problems


def is_f0_sample(result):
    if result["mismatch"]:
        return True
    return result["status"] == "verified" and "incomplete" not in result["decision"].values()


def f0_gate_recompute(expect, verify_results):
    self_checks, sample = {}, []
    for result in verify_results.values():
        if not result["is_f0"]:
            continue
        self_checks[result["run"]] = result["self_check"]
        if is_f0_sample(result):
            sample.append(result["run"])
    required = F0_REQUIRED_SELF_CHECKS if expect["edition"] == "e1" else 0
    sample = sorted(sample)
    passed = len(sample) >= required and all(value is not False for run in sample for value in (self_checks.get(run) or {}).values())
    return {"self_checks": self_checks, "sample": sample, "required": required, "passed": passed}


def f0_gate_problems(expect, journal, verify_results):
    recorded = (journal.get("final_batch") or {}).get("f0_gate")
    has_f0 = any(result["is_f0"] for result in verify_results.values())
    if recorded is None:
        started_after = [item for item in journal.get("items") or [] if item.get("block") != "F0" and item.get("status") not in (None, "pending")]
        return ["F0 관문 기록(f0_gate)이 없는데 F1·F2 항목이 시작됐다"] if has_f0 and started_after else []
    recomputed = f0_gate_recompute(expect, verify_results)
    return [] if canonical_text(recorded) == canonical_text(recomputed) else [f"일지 f0_gate {canonical_text(recorded)} ≠ 다시 계산 {canonical_text(recomputed)}"]


def is_f0_gate_failed(expect, journal, verify_results):
    recorded = (journal.get("final_batch") or {}).get("f0_gate")
    return (recorded or {}).get("passed") is False or not f0_gate_recompute(expect, verify_results)["passed"]


def is_f0_decided(journal):
    if (journal.get("final_batch") or {}).get("f0_gate") is not None:
        return True
    return all(item.get("status") in VERIFY_TERMINAL + ("self_check_mismatch",) for item in journal.get("items") or [] if item.get("block") == "F0")


def is_f0_shortfall_certain(expect, verify_results):
    gate = f0_gate_recompute(expect, verify_results)
    finished = {result["run"] for result in verify_results.values() if result["is_f0"] and result["status"] is not None}
    open_runs = [run_id for run_id, run in expect["runs"].items() if run.get("block") == "F0" and run_id not in finished]
    return len(gate["sample"]) + len(open_runs) < gate["required"]


def terminal_problems(journal):
    problems = []
    for index, item in enumerate(journal.get("items") or []):
        kind = item_final(item).get("kind")
        terminal = VERIFY_TERMINAL if kind == "verify" else AB_TERMINAL
        if item.get("status") not in terminal:
            problems.append(f"항목 {index} 상태 {item.get('status')} 가 끝 상태가 아니다")
    return problems


def merge_outcomes(outcomes):
    merged = {}
    for key, entries in outcomes.items():
        decisions = [entry[0] for entry in entries]
        decision = next((name for name in ("revert", "incomplete", "user_decision") if name in decisions), "keep")
        merged[key] = {"decision": decision, "rules": [rule for entry in entries for rule in entry[1]], "labels": [entry[2] for entry in entries],
                       "evidence": [path for entry in entries for path in entry[3]]}
    return merged


def adoption_outcomes(expect, keys, decision_states, statuses, verify_results, ab_results):
    outcomes, details = {}, {}
    ab_items = {result["ab"]["id"]: result for result in ab_results.values()}
    sources = {key: value for result in verify_results.values() for key, value in result["sources"].items()}
    compare_paths = {result["run"]: [f"{entry['folder']}/compare.json" for entry in result["opportunities"].values() if entry.get("folder")] for result in verify_results.values()}
    for key in keys:
        state = target_state(expect, decision_states, statuses, key)
        if state is None:
            continue
        covered = next((item for item in expect["covered"] if item["adoption"] == key), None)
        state_run = covered["by_run"] if covered else next((row["run"] for row in expect["rows"] if row["adoption"] == key), None)
        evidence = compare_paths.get(state_run, [])
        if key in expect["influences"]:
            cross = next((ab for ab in expect["ab"] if ab["cross"] and ab["trigger_adoption"] == key), None)
            recomputed = ab_items.get(cross["id"]) if cross else None
            if cross is not None and (recomputed is None or recomputed["decision"] is None):
                continue
            target_run = next((row["run"] for row in expect["rows"] if cross and row["adoption"] == cross["for_adoption"]), None)
            for target, decision, rules, label in decide_influence(key, state, cross, recomputed, target_run):
                outcomes.setdefault(target, []).append((decision, rules, label, evidence + (recomputed or {}).get("evidence", [])))
            details[key] = {"state": state, "state_source": sources.get(key), "ab": {"id": cross["id"], "decision": recomputed["decision"]} if cross and recomputed else None}
            continue
        wab = next((ab for ab in expect["ab"] if ab["for_adoption"] == key and not ab["cross"]), None)
        recomputed = ab_items.get(wab["id"]) if wab else None
        if wab is not None and (recomputed is None or recomputed["decision"] is None):
            continue
        decision, rules, label = decide_adoption(key, state, wab["trigger"] if wab else None, recomputed["decision"] if recomputed else None)
        outcomes.setdefault(key, []).append((decision, rules, label, evidence + (recomputed or {}).get("evidence", [])))
        details[key] = {"state": state, "state_source": sources.get(covered["by_adoption"] if covered else key), "ab": {"id": wab["id"], "decision": recomputed["decision"]} if recomputed else None}
    return outcomes, details


def fill_decision_lists(report, expect, merged, details, phase):
    for key in sorted_adoptions(set(merged) | set(details)):
        if key is None:
            continue
        entry = merged.get(key) or {"decision": "keep", "rules": [], "labels": [], "evidence": []}
        detail = details.get(key) or {"state": None, "state_source": None, "ab": None}
        decision = entry["decision"]
        if decision == "revert" and phase in ("r2", "r2-w13"):
            decision = "post_hoc"
        report["adoptions"][key] = dict(detail, decision=decision, rules=entry["rules"], labels=entry["labels"], evidence=entry["evidence"])
        info = expect["adoptions"].get(key) or {}
        if decision in ("revert", "post_hoc"):
            report["revert" if decision == "revert" else "post_hoc"].append({"adoption": key, "entry_id": info.get("entry_id"), "kind": info.get("kind"), "rules": entry["rules"],
                                                                            "labels": entry["labels"], "evidence": entry["evidence"]})
        elif decision == "incomplete":
            report["incomplete"].append({"adoption": key, "reason": "; ".join(entry["labels"]), "rules": entry["rules"], "evidence": entry["evidence"]})
        elif decision == "user_decision":
            report["user_decision"].append({"adoption": key, "label": "; ".join(entry["labels"]), "rules": entry["rules"], "evidence": entry["evidence"]})
    if None in merged:
        report["user_decision"].append({"adoption": None, "label": "; ".join(merged[None]["labels"]), "rules": merged[None]["rules"], "evidence": merged[None]["evidence"]})


def evaluate_verdict(arguments, expect):
    phase = arguments.phase
    require(phase in PHASE_EDITIONS, "input", f"phase {phase} 모름")
    edition = PHASE_EDITIONS[phase]
    paths, queue, journal = verdict_inputs(arguments, expect, phase, edition)
    report = verdict_report_base(arguments, expect)
    anchor, commit_time = verdict_anchor_and_time(arguments, expect, paths, journal, edition)
    w_anchor = load_json(arguments.w_anchor)
    report["inputs"] = {"expect": {"path": paths["expect"], "sha256": sha_of(paths["expect"])}, "journal": {"path": paths["journal"], "sha256": sha_of(paths["journal"])},
                        "queue": {"path": paths["queue"], "sha256": sha_of(paths["queue"])}, "anchor": {"path": paths["anchor"], "sha256": sha_of(paths["anchor"])},
                        "w_anchor": {"path": normalized(arguments.w_anchor), "sha256": sha_of(arguments.w_anchor), "commit_time": commit_time.isoformat()},
                        "tests": [{"label": label_of_tests_file(text), "path": normalized(text), "sha256": sha_of(text)} for text in arguments.tests or []]
                        + [{"label": text.partition("=")[0], "base_error": normalized(text.partition("=")[2])} for text in arguments.tests_base_error or []],
                        "user_approved": {"path": normalized(arguments.user_approved), "sha256": sha_of(arguments.user_approved)} if arguments.user_approved else None,
                        "ns_dump": {"path": normalized(arguments.ns_dump), "sha256": sha_of(arguments.ns_dump)} if arguments.ns_dump else None}
    journal_problems = verdict_journal_problems(expect, paths, queue, journal, anchor, edition)
    journal_problems += journal_pin_problems(w_anchor, w_anchor_history(arguments.w_anchor), journal, expect["final_dir"])
    require_none(journal_problems, "journal")
    e2 = judge_tool_failure(journal)
    require_none(copies_check_problems(expect["spec"]), "copies")
    item_problems, verify_results, ab_results, decision_states, statuses = evaluate_items(expect, queue, journal, w_anchor)
    for index, line in folder_set_problems(journal, expect["final_dir"], edition):
        if index is None and e2 is None:
            raise GateFailure("artifacts", [line])
        item_problems.setdefault(index, []).append(("artifacts", line))
    global_problems = f0_gate_problems(expect, journal, verify_results)
    outcome_event = ((journal.get("final_batch") or {}).get("outcome") or {}).get("event")
    mismatched = [result["run"] for result in verify_results.values() if result["mismatch"]]
    if outcome_event in ("E3", "E3_repeat") and not mismatched:
        global_problems.append(f"일지 outcome {outcome_event} 인데 자기 검증 불일치 항목이 없다")
    if mismatched and outcome_event not in ("E3", "E3_repeat"):
        global_problems.append(f"자기 검증 불일치 {mismatched} 인데 일지 outcome 이 E3 이 아니다")
    report["self_check"] = {result["run"]: result["self_check"] for result in verify_results.values() if result["is_f0"]}
    is_journal_ended = outcome_event == "start_failed" or e2 is not None
    is_gate_failed = (is_f0_gate_failed(expect, journal, verify_results) and is_f0_decided(journal)) or (is_journal_ended and is_f0_shortfall_certain(expect, verify_results))
    if e2 is None:
        failures = [line for index in sorted(item_problems, key=lambda value: (value is None, value or 0)) for _, line in item_problems[index]] + global_problems
        gates = [gate for index in item_problems for gate, _ in item_problems[index]]
        require_none(failures, gates[0] if gates else "recompute")
        if outcome_event not in STOP_EVENTS and not is_gate_failed:
            require_none(terminal_problems(journal), "terminal")
    keys = measured_keys(expect) + sorted(expect["influences"])
    keys = list(dict.fromkeys(keys))
    adoption_by_entry = {info["entry_id"]: number for number, info in expect["adoptions"].items()}
    if e2 is None and outcome_event in ("E3", "E3_repeat", "F0_shortfall"):
        return stop_report(report, expect, outcome_event, edition)
    if e2 is None and is_gate_failed:
        return stop_report(report, expect, "F0_shortfall", edition)
    test_classes = load_json(queue["test_classes"]["path"])
    outcomes, details = adoption_outcomes(expect, keys, decision_states, statuses, verify_results, ab_results)
    w_defects = []
    report["tests"] = evaluate_tests(arguments, expect, phase, adoption_by_entry, outcomes, w_defects, test_classes)
    report["w_defect"] = w_defects
    if arguments.user_approved:
        evaluate_user_approved(arguments.user_approved, expect, adoption_by_entry, outcomes)
    if expect.get("p22"):
        report["p22"] = evaluate_p22(expect, arguments.ns_dump, decision_states)
    if outcome_event == "start_failed" and e2 is None:
        for key in keys:
            if key not in details:
                outcomes.setdefault(key, []).append(("incomplete", ["B1", "개정 E5"], "시작 처리 연속 2회 실패 — 최종 검증 미완", []))
        report["stop"] = {"event": "start_failed"}
    if e2 is not None:
        return e2_report(report, expect, e2, keys, journal, item_problems, outcomes, {} if is_gate_failed else details, phase)
    merged = merge_outcomes(outcomes)
    fill_decision_lists(report, expect, merged, details, phase)
    for key in keys:
        if key not in report["adoptions"]:
            reason = "결정에 필요한 항목이 끝나지 않았다"
            report["adoptions"][key] = {"state": None, "state_source": None, "ab": None, "decision": "incomplete", "rules": ["R-W20"], "labels": [reason], "evidence": []}
            report["incomplete"].append({"adoption": key, "reason": reason, "rules": ["R-W20"], "evidence": []})
    if phase == "w13" and report["revert"]:
        report["r2_targets"] = r2_targets(expect, {item["entry_id"] for item in report["revert"]})
    is_clean = report["stop"] is None and not any(report[name] for name in ("revert", "user_decision", "incomplete", "w_defect", "post_hoc"))
    return report, EXIT_OK if is_clean else EXIT_FAILED


def stop_report(report, expect, event, edition):
    if event == "E3" and edition == "e1":
        require(expect["restart"] == 0, "journal", "restart 1 의 자기 검증 불일치는 E3_repeat 이어야 한다")
        report["stop"] = {"event": "E3", "next": "restart"}
        return report, EXIT_FAILED
    require(event != "E3_repeat" or (edition == "e1" and expect["restart"] == 1), "journal", "E3_repeat 는 e1·restart 1 에서만 난다")
    reasons = {"E3_repeat": ("D1 두 번째 E3", ["D1"]), "F0_shortfall": ("F0 결원(개정 E1·D-W28: 자기 검증 표본 3건 미만)", ["D-W28", "개정 E1"]),
               "E3": ("r2 E3 — r2 대상 미완", ["B5"])}
    reason, rules = reasons[event]
    report["stop"] = {"event": event}
    report["incomplete"] = incomplete_all(expect, reason, rules)
    for item in report["incomplete"]:
        report["adoptions"][item["adoption"]] = {"state": None, "state_source": None, "ab": None, "decision": "incomplete", "rules": rules, "labels": [reason], "evidence": []}
    return report, EXIT_FAILED


def e2_report(report, expect, e2, keys, journal, item_problems, outcomes, details, phase):
    report["stop"] = e2
    failing_runs = set()
    failing_abs = set()
    for index, problems in item_problems.items():
        if not problems or index is None:
            continue
        final = item_final(journal["items"][index])
        failing_runs.add(final.get("run"))
        failing_abs.add(final.get("ab_id"))
    merged = merge_outcomes(outcomes)
    for key in keys:
        detail = details.get(key)
        covered = next((item for item in expect["covered"] if item["adoption"] == key), None)
        state_run = covered["by_run"] if covered else next((row["run"] for row in expect["rows"] if row["adoption"] == key), None)
        wab = next((ab for ab in expect["ab"] if (ab["for_adoption"] == key and not ab["cross"]) or (ab["cross"] and ab["trigger_adoption"] == key)), None)
        is_settled = detail is not None and key in merged and state_run not in failing_runs and (wab is None or wab["id"] not in failing_abs)
        if is_settled:
            decision = merged[key]["decision"]
            report["settled"][key] = dict(merged[key], decision="post_hoc" if decision == "revert" and phase in ("r2", "r2-w13") else decision)
        else:
            report["incomplete"].append({"adoption": key, "reason": f"E2 {e2.get('reason')} — 판정 미확정", "rules": ["B1"], "evidence": []})
    return report, EXIT_INPUT


def command_verdict(arguments):
    out_text = require_saved_output(arguments.out)
    expect = load_json_or_none(arguments.expect)
    try:
        report, exit_code = evaluate_verdict(arguments, expect)
    except GateFailure as failure:
        report, exit_code = verdict_gate_report(arguments, expect, failure), failure.exit_code
    except INPUT_ERRORS as error:
        report, exit_code = verdict_gate_report(arguments, expect, GateFailure("input", [f"{type(error).__name__}: {error}"])), EXIT_INPUT
    write_json(out_text, report)
    for line in report["problems"]:
        print(f"관문 {report['gate']}: {line}", file=sys.stderr)
    print(f"verdict {arguments.phase}: stop {report['stop']} revert {len(report['revert'])} user_decision {len(report['user_decision'])} incomplete {len(report['incomplete'])} "
          f"w_defect {len(report['w_defect'])} post_hoc {len(report['post_hoc'])} 종료 {exit_code}")
    return exit_code


def run_patch_tool(arguments):
    completed = subprocess.run([sys.executable, "-B", str(project_path(PATCH_TOOL_PATH)), *arguments], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return completed.returncode, completed.stdout, completed.stderr


def first_json(text):
    return json.JSONDecoder().raw_decode(text.lstrip())[0]


def patch_step(arguments, label):
    exit_code, stdout, stderr = run_patch_tool(arguments)
    require(exit_code == 0, "rollback", f"{label}: fluid_tuning_patch 종료 {exit_code}: {stderr.strip()[-400:]}")
    return first_json(stdout) if stdout.strip() else {}


def canonical_now_sha():
    return {"tuning": prereg_lock.file_sha256(fluid_tuning_patch.canonical_tuning_path()), "look": prereg_lock.file_sha256(fluid_tuning_patch.canonical_look_path())}


def rollback_paths(final_dir):
    return {"keep": f"{final_dir}/overlay-keep.json", "preview": f"{final_dir}/preview-r2", "w_expect": f"{final_dir}/r2/w-expect.json", "backup": f"{final_dir}/canonical_backup_r2",
            "write_canonical": f"{final_dir}/write-canonical-r2.json", "report": f"{final_dir}/r2/rollback.json", "builder_before": f"{final_dir}/builder-before/preset_builder_dump.json"}


def command_rollback(arguments):
    spec_text = normalized(arguments.spec)
    spec = load_json(spec_text)
    final_dir = require_saved_output(final_directory(spec))
    paths = derived_paths(final_dir, "e1")
    outputs = rollback_paths(final_dir)
    require_none(check_anchor(arguments.w_anchor, "e1", [spec_text, paths["expect"]]), "anchor")
    verdict_text = normalized(arguments.verdict)
    require(sha_of(verdict_text) == arguments.verdict_sha, "verdict", "verdict sha ≠ --verdict-sha")
    verdict = load_json(verdict_text)
    require(verdict.get("phase") == "w13" and verdict.get("edition") == "e1" and verdict.get("stop") is None and verdict.get("gate") is None, "verdict", "verdict 는 w13·e1·stop 없음·gate 없음이어야 한다")
    require(((verdict.get("inputs") or {}).get("anchor") or {}).get("sha256") == sha_of(paths["anchor"]), "verdict", "verdict inputs.anchor sha ≠ 지금 anchor.json")
    reverted = sorted(verdict.get("revert") or [], key=lambda item: adoption_sort_key(item["adoption"]))
    require(reverted, "verdict", "verdict revert 가 비어 있다")
    frozen_text = normalized(spec["overlay"])
    frozen = overlay_entry_map(read_overlay(frozen_text))
    require_none([f"revert entry_id {item['entry_id']} 이 동결본에 없다" for item in reverted if item["entry_id"] not in frozen], "verdict")
    require_none([f"출력이 이미 있다: {path}" for key, path in outputs.items() if key in ("keep", "preview", "w_expect", "backup", "write_canonical", "report") and project_path(path).exists()], "outputs")
    expected_ids = sorted(set(frozen) - {item["entry_id"] for item in reverted})
    restore = patch_step(["restore", "--backup-dir", normalized(arguments.backup_dir)], "restore")
    require(canonical_now_sha() == CANONICAL_BEFORE_SHA256, "rollback", "restore 뒤 정본 sha ≠ CANONICAL_BEFORE_SHA256")
    write_bytes(outputs["keep"], project_path(frozen_text).read_bytes())
    removes = []
    for item in reverted:
        if item["kind"] != "assignment":
            continue
        reason = f"W 최종 검증 {'·'.join(item.get('rules') or [])}: {verdict_text}"
        result = patch_step(["overlay-remove", "--overlay", outputs["keep"], "--json-path", item["entry_id"], "--reason", reason], f"overlay-remove {item['entry_id']}")
        removes.append({"adoption": item["adoption"], "entry_id": item["entry_id"], "overlay_sha256": result.get("overlay_sha256")})
    actor_props = [item for item in reverted if item["kind"] == "actor_prop"]
    if actor_props:
        keep = fluid_tuning_patch.read_overlay(project_path(outputs["keep"]))
        removed_ids = {item["entry_id"] for item in actor_props}
        removed = [entry for entry in keep["actor_props"] if entry_id(entry) in removed_ids]
        keep["actor_props"] = [entry for entry in keep["actor_props"] if entry_id(entry) not in removed_ids]
        keep["history"].extend(fluid_tuning_patch.history_entries("remove", removed, f"W 최종 검증 actor_prop 되돌림: {verdict_text}", datetime.now().isoformat(timespec="seconds")))
        fluid_tuning_patch.write_overlay(project_path(outputs["keep"]), keep)
        removes += [{"adoption": item["adoption"], "entry_id": item["entry_id"], "overlay_sha256": sha_of(outputs["keep"])} for item in actor_props]
    keep_entries = overlay_entry_map(read_overlay(outputs["keep"]))
    require(sorted(keep_entries) == expected_ids, "rollback", "덧씌움 사본 항목 집합 A ≠ K")
    require_none([f"남긴 항목 {identifier} 이 동결본과 다르다" for identifier in expected_ids if canonical_text(keep_entries[identifier]) != canonical_text(frozen[identifier])], "rollback")
    materialize = patch_step(["materialize", "--overlay", outputs["keep"], "--out", outputs["preview"]], "materialize")
    w_expect = build_w_expect(outputs["keep"], spec["canonical_before_dir"], outputs["preview"], outputs["builder_before"])
    write_json(outputs["w_expect"], w_expect)
    if read_overlay(outputs["keep"])["assignments"]:
        written = patch_step(["write-canonical", "--from-overlay", outputs["keep"], "--backup-dir", outputs["backup"], "--acknowledge-header"], "write-canonical r2")
        write_json(outputs["write_canonical"], written)
        require(written.get("written_sha256") == w_expect["written"]["tuning"] and written.get("written_look_sha256") == w_expect["written"]["look"], "rollback", "write-canonical r2 sha ≠ r2 w-expect written")
        write_canonical = {"path": outputs["write_canonical"], "written_sha256": written.get("written_sha256"), "written_look_sha256": written.get("written_look_sha256")}
    else:
        now = canonical_now_sha()
        require(now == CANONICAL_BEFORE_SHA256 == w_expect["written"], "rollback", "빈 keep: 정본 sha ≠ CANONICAL_BEFORE_SHA256 ≠ r2 w-expect written")
        write_canonical = "skipped_empty"
        write_json(outputs["write_canonical"], {"write_canonical": "skipped_empty", "written_sha256": now["tuning"], "written_look_sha256": now["look"]})
    require_none(check_anchor(arguments.w_anchor, "e1", [spec_text, paths["expect"]]), "anchor_after")
    reverted_paths = [item["entry_id"] for item in reverted if item["kind"] == "assignment"]
    fog_reverted = any(is_fog_light_entry(item["entry_id"]) for item in actor_props)
    report = {"tool": TOOL_NAME, "version": TOOL_VERSION, "verdict": verdict_text, "verdict_sha256": sha_of(verdict_text), "anchor_sha256": sha_of(paths["anchor"]),
              "frozen_sha256": sha_of(frozen_text), "keep_sha256": sha_of(outputs["keep"]), "expected_ids": expected_ids, "actual_ids": sorted(keep_entries),
              "reverted": [{"adoption": item["adoption"], "entry_id": item["entry_id"], "kind": item["kind"], "rules": item.get("rules") or []} for item in reverted],
              "restore": restore, "removes": removes, "materialize": {"tuning_sha256": materialize.get("tuning_sha256"), "look_sha256": materialize.get("look_sha256")},
              "w_expect_sha256": sha_of(outputs["w_expect"]), "write_canonical": write_canonical,
              "followups": {"header_revert": any(path.startswith(fluid_tuning_patch.GLOBALS_SECTION + ".") for path in reverted_paths),
                            "fog_generator_value": FOG_LIGHT_BEFORE if fog_reverted else None, "fog_stage_regeneration": fog_reverted,
                            "reverted_look_assets": sorted({fluid_tuning_patch.parse_look_path(path)[1] for path in reverted_paths if is_look_path(path)})},
              "finished_at": now_text()}
    write_json(outputs["report"], report)
    print(f"rollback 되돌림 {len(reverted)} 남김 {len(expected_ids)} followups {report['followups']}")
    return EXIT_OK


def r2_overlay_path(r2_dir, text):
    return f"{r2_dir}/{PurePosixPath(normalized(text)).name}"


def adapt_r2_run(run, sources, context):
    adapted = copy.deepcopy(run)
    adapted["sources"] = copy.deepcopy(sources)
    adapted["overlay"] = r2_overlay_path(context["r2_dir"], run["overlay"])
    plan_text = normalized(run["plan"])
    plan_copy = context["plan_copy_outs"].get(plan_text)
    if plan_copy is None:
        return adapted
    original = normalized(plan_copy["plan"])
    removed = removed_base_paths(load_json(original), load_json(plan_text))
    if any(look_value(context["preview_look"], path) is not MISSING for path in removed):
        out = f"{context['r2_dir']}/plans/{PurePosixPath(plan_text).name}"
        context["plan_copies"][out] = {"plan": original, "out": out}
        adapted["plan"] = out
        return adapted
    adapted["plan"] = original
    for source in adapted["sources"]:
        require(load_json(source["judge"]).get("plan_sha256") == sha_of(original), "r2_spec", f"{run['id']}: 원 계획으로 돌아가는데 출처 judge plan_sha256 ≠ 원 계획 sha")
        source["binding"] = {"kind": "same"}
    return adapted


def build_r2_spec(spec, expect, verdict, final_dir, preview_dir):
    reverted_numbers = {item["adoption"] for item in verdict["revert"]}
    reverted_ids = {item["entry_id"] for item in verdict["revert"]}
    targets = r2_targets(expect, reverted_ids)
    require(canonical_text(targets) == canonical_text(verdict.get("r2_targets")), "r2_spec", "r2_targets 다시 계산 ≠ verdict r2_targets")
    target_keys = set(targets["adoptions"]) | set(targets["influences"])
    r2_dir = f"{final_dir}/r2"
    context = {"r2_dir": r2_dir, "preview_look": read_canonical_pair(preview_dir)["look"], "plan_copies": {},
               "plan_copy_outs": {normalized(item["out"]): item for item in spec.get("plan_copies") or []}}
    runs = []
    for run in spec.get("runs") or []:
        sources = [source for source in run.get("sources") or [] if source["adoption"] in target_keys]
        if sources and run["id"] in targets["runs"]:
            runs.append(adapt_r2_run(run, sources, context))
    covered, promoted = [], {}
    for item in spec.get("covered") or []:
        if item["adoption"] not in target_keys:
            continue
        fallback = item["fallback"]
        if item["by_adoption"] in reverted_numbers:
            runs.append(adapt_r2_run(fallback, [source for source in fallback.get("sources") or [] if source["adoption"] in target_keys], context))
            promoted[item["adoption"]] = {"fallback": fallback["id"], "by_run": item["by_run"]}
            continue
        kept = copy.deepcopy(item)
        kept["fallback"] = adapt_r2_run(fallback, fallback.get("sources") or [], context)
        covered.append(kept)
    ab_entries = []
    for ab in spec.get("ab") or []:
        if ab["for_adoption"] in reverted_numbers:
            continue
        if ab.get("cross") and ab["trigger_adoption"] not in targets["influences"]:
            continue
        if not ab.get("cross") and ab["for_adoption"] not in targets["adoptions"]:
            continue
        adapted = copy.deepcopy(ab)
        adapted["plan"] = f"{r2_dir}/plans/{PurePosixPath(normalized(ab['plan'])).name}"
        adapted["overlay"] = r2_overlay_path(r2_dir, ab["overlay"])
        if ab["for_adoption"] in promoted and not ab.get("cross"):
            adapted["for_run"] = promoted[ab["for_adoption"]]["fallback"]
            adapted["trigger_adoption"] = ab["for_adoption"]
        ab_entries.append(adapted)
    run_ids = {run["id"] for run in runs}
    p22 = None
    if spec.get("p22"):
        fallback_of = {value["by_run"]: value["fallback"] for value in promoted.values()}
        p22_runs = [fallback_of.get(run_id, run_id) for run_id in spec["p22"]["runs"]]
        p22_keys = {row["adoption"] for row in expect["rows"] if row["run"] in spec["p22"]["runs"]} | {item["adoption"] for item in expect["covered"] if item["by_run"] in spec["p22"]["runs"]}
        if run_ids & set(p22_runs) or target_keys & p22_keys:
            p22 = dict(copy.deepcopy(spec["p22"]), runs=[run_id for run_id in p22_runs if run_id in run_ids])
    globals_reverted = any(identifier.startswith(fluid_tuning_patch.GLOBALS_SECTION + ".") for identifier in reverted_ids)
    r2_tests = ["w10-rhi-r2"] + (["w4-header-r2"] if globals_reverted else [])
    return {"version": SPEC_VERSION, "edition": "r2", "final_dir": r2_dir, "overlay": f"{final_dir}/overlay-keep.json", "preview_dir": normalized(preview_dir),
            "canonical_before_dir": normalized(spec["canonical_before_dir"]), "w_expect": f"{r2_dir}/w-expect.json", "test_owner": normalized(spec["test_owner"]),
            "pie_files": list(spec.get("pie_files") or []), "adoptions": {number: copy.deepcopy(value) for number, value in spec["adoptions"].items() if number not in reverted_numbers},
            "influences": [key for key in spec.get("influences") or [] if key in targets["influences"]], "plan_copies": [value for _, value in sorted(context["plan_copies"].items())],
            "runs": runs, "covered": covered, "ab": ab_entries, "p22": p22,
            "source_scene_exceptions": [item for item in spec.get("source_scene_exceptions") or [] if item.get("adoption") in target_keys],
            "tests_required": {"r2": r2_tests, "r2-w13": r2_tests + ["w13-headless-r2", "w13-rhi-r2"]}}


def command_r2_spec(arguments):
    spec_text = normalized(arguments.spec)
    spec = load_json(spec_text)
    final_dir = require_saved_output(final_directory(spec))
    require_none(check_anchor(arguments.w_anchor, "e1", [spec_text]), "anchor")
    rollback_text = f"{final_dir}/r2/rollback.json"
    keep_text = f"{final_dir}/overlay-keep.json"
    require(is_file(rollback_text), "r2_spec", f"rollback 보고 없음: {rollback_text}")
    rollback = load_json(rollback_text)
    verdict_text = normalized(arguments.verdict)
    require(rollback.get("verdict_sha256") == sha_of(verdict_text) and rollback.get("keep_sha256") == sha_or_none(keep_text), "r2_spec", "rollback 보고의 verdict·keep sha 가 지금과 다르다")
    require(normalized(arguments.overlay_keep) == keep_text and normalized(arguments.preview_dir) == f"{final_dir}/preview-r2" and normalized(arguments.out_dir) == f"{final_dir}/r2",
            "input", "--overlay-keep·--preview-dir·--out-dir 가 파생 경로와 다르다")
    r2_spec = build_r2_spec(spec, load_json(f"{final_dir}/expect.json"), load_json(verdict_text), final_dir, arguments.preview_dir)
    write_json(f"{final_dir}/r2/final-spec.json", r2_spec)
    print(f"r2 명세 실행 {len(r2_spec['runs'])} A/B {len(r2_spec['ab'])} sha {sha_of(f'{final_dir}/r2/final-spec.json')}")
    return EXIT_OK


def build_parser():
    parser = argparse.ArgumentParser(description="W 최종 검증 사전 등록·비교기·판정(spec-final-verify)")
    commands = parser.add_subparsers(dest="command", required=True)
    definitions = {
        "w-expect": [("--overlay", True), ("--before-dir", True), ("--preview-dir", True), ("--builder-before", True), ("--out", True)],
        "copies": [("--spec", True), ("--out-dir", True)],
        "test-owner": [("--overlay", True), ("--test-classes", True), ("--source-root", True), ("--out", True)],
        "replay": [("--out-dir", True)],
        "expect": [("--spec", True), ("--out", True), ("--queue-out", True)],
        "anchor": [("--spec", True)],
        "pin-journal": [("--spec", True), ("--user-decision", False), ("--e2-evidence", False)],
        "compare": [("--expect", True), ("--run", True), ("--sweep", True), ("--lock", True), ("--judge-lock", False), ("--work-dir", True), ("--out", True)],
        "check-ab": [("--expect", True), ("--ab", True), ("--stage", True), ("--result", True), ("--sweep", True), ("--lock", True), ("--out", True)],
        "verdict": [("--phase", True), ("--expect", True), ("--journal", True), ("--queue", True), ("--out", True), ("--user-approved", False), ("--ns-dump", False),
                    ("--anchor-spec-sha", False), ("--anchor-expect-sha", False), ("--anchor-queue-sha", False)],
        "rollback": [("--spec", True), ("--verdict", True), ("--verdict-sha", True), ("--backup-dir", True)],
        "r2-spec": [("--spec", True), ("--verdict", True), ("--overlay-keep", True), ("--preview-dir", True), ("--out-dir", True)],
    }
    for name, options in definitions.items():
        command = commands.add_parser(name)
        for option, is_required in options:
            command.add_argument(option, required=is_required)
        command.add_argument("--w-anchor", default=DEFAULT_W_ANCHOR)
    commands.choices["copies"].add_argument("--check", action="store_true")
    commands.choices["anchor"].add_argument("--w-anchor-out", default=DEFAULT_W_ANCHOR)
    commands.choices["pin-journal"].add_argument("--w-anchor-out", default=DEFAULT_W_ANCHOR)
    commands.choices["replay"].add_argument("--verify", action="append", default=[])
    commands.choices["replay"].add_argument("--self", dest="self_judge", action="append", default=[])
    commands.choices["replay"].add_argument("--identity", action="append", default=[])
    commands.choices["verdict"].add_argument("--tests", action="append", default=[])
    commands.choices["verdict"].add_argument("--tests-base-error", action="append", default=[])
    return parser


COMMANDS = {"w-expect": command_w_expect, "copies": command_copies, "test-owner": command_test_owner, "replay": command_replay, "expect": command_expect,
            "anchor": command_anchor, "pin-journal": command_pin_journal, "compare": command_compare, "check-ab": command_check_ab, "verdict": command_verdict, "rollback": command_rollback,
            "r2-spec": command_r2_spec}


def report_gate_failure(arguments, failure):
    for line in failure.problems:
        print(f"관문 {failure.gate}: {line}", file=sys.stderr)
    out_text = getattr(arguments, "out", None)
    if not out_text or arguments.command in ARTIFACT_OUTPUT_COMMANDS:
        return
    try:
        write_json(require_saved_output(out_text), {"tool": TOOL_NAME, "version": TOOL_VERSION, "gate": failure.gate, "problems": failure.problems})
    except INPUT_ERRORS as error:
        print(f"관문 보고를 쓰지 못했다: {error}", file=sys.stderr)


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    arguments = build_parser().parse_args(argv)
    try:
        return COMMANDS[arguments.command](arguments)
    except GateFailure as failure:
        report_gate_failure(arguments, failure)
        return failure.exit_code
    except INPUT_ERRORS as error:
        report_gate_failure(arguments, GateFailure("input", [f"{type(error).__name__}: {error}"]))
        return EXIT_INPUT


if __name__ == "__main__":
    sys.exit(main())
