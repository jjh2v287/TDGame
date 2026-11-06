"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, git 명령은 runs의 hash_paths 대조에만, 에디터 불필요, PowerShell 또는 Git Bash). FluidFX P 단계(runbook-p-v3 5절 T3) 계획·잠금·실행 선택·요약 도구: p_areas.json 검사(T3.1), 영역별 표·시운전·사다리 계획과 sheet_areas.json 생성·대조(T3.2), 현재 잠금·잠금 목록(T3.3), 측정 전 점검(T3.4), 실행 고르기 PR-10(T3.5), 사다리 필요(T3.6), 판정 SP 고르기 PR-5·C6(T3.7), 영역 상태(T3.8), P 요약(T3.9).
실행 명령: python -B Tools/FluidFX/perf/p_plans.py build --ledger <LG v2> --areas <p_areas.json> --out-dir Tools/FluidFX/perf/p [--check] | current-lock --stage L0|L1 [--area A] [--next] | lock-list --stage L0 --areas <p_areas.json> | lock-list --stage L1 --area A --area-dir <AD> | preflight --lock <L0> --holder <이름> --out <json> | runs --plan <계획> --lock <현재 잠금> --out <AD>/<단계>-runs.json | need-ladder --area A --area-dir <AD> --out <AD>/ladder-need.json | select-sp --area A --area-dir <AD> --out-plan <AD>/judge-plan.json --out <AD>/sp-choice.json | status --area A --p-dir <P> | summary --p-dir <P> --areas <p_areas.json> --approvals <approvals.json> --out <p-summary.json> --md <p-summary.md>, 모든 하위 명령에 [--root <저장소 사본>](fixture용, 기본 저장소 루트)
출력: build = <out-dir>/<영역>/{table,pilot1[,ladder,ladder-table]}.json·sheet_areas.json(같은 입력 = 같은 바이트), --check = 문제 목록, current-lock = 루트 상대 잠금 경로 한 줄, lock-list = 잠글 파일 한 줄에 하나(하위 명령 출력은 표준 오류), preflight = {lock, holder, checks, ok}, runs = 종료 0·4에서만 {plan, lock, lock_created_at, first_lock, l0_lock, exit, reason, selected, attempts, counted_failures, runs}, need-ladder = {area, needed, reason, strong_fail, pilot_judge}, select-sp = judge-plan.json(먼저)·{area, inputs, scenes}, status = done|todo, summary = md(먼저)·{areas{영역: overall·counts·table_sha256·rows·lines·…}, pb2_214, pb2_fxp_sources, fxp_items, …}; 결함 파일 <P>/defects/D<n>.md의 '사후 결함 행: <행 ID…>' 줄만 사후 결함, 승인 파일 = {areas: {영역: {decision 승인|거부, date, sheet_json, sheet_json_sha256, text}}}. 모든 쓰기는 원자적. 종료 0 성공 / 2 입력·검사 실패·--out 있음 / 3 L1 잠금 이미 있음(lock-list)·측정 필요(runs) / 4 runs 측정 불가(실행) / 5 runs 잠금 verify·hash_paths 다름(재잠금) / 6 runs 첫 잠금 전 실행(종착)
상태: 2026-11-06 T3.1~T3.9 구현, 반박 r1 M1 반영(sheet_areas 채택 귀속 = expect.json adoptions[번호].scenes·levels ∩ 그 채택 compare 장면, 입력에 expect 추가). 시험 python -B Tools/FluidFX/perf/p/check_p_fixtures.py(build·current-lock·lock-list·preflight·runs·need-ladder·select-sp·status·summary 사례). 실제 p_areas.json·PIE 실행·T2 판정 출력과의 실물 대조는 아직 없음. 2026-11-07 AM7 L1~L3 반영(runs ②는 session 있는 후보만, 열 결함 이름은 '열 결함: a, b'·'(열 결함: …)' 두 형식, summary pb2_fxp_sources).
"""
import argparse
import ast
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "Tools" / "FluidFX"))
import prereg_lock

EXIT_OK = 0
EXIT_INPUT = 2
EXIT_LOCK_EXISTS = 3

AREAS = ("fire", "smoke", "fog", "water", "cloud", "input", "track", "viscous")
SHEET_AREAS = AREAS + ("foliage",)
P_DIR = "Saved/AgentOps/20261106/p"
T_DIR = "Tools/FluidFX/perf/p"
LOCKS_DIR = f"{P_DIR}/locks"
PREP_DIR = "Saved/AgentOps/20261106/p-prep"
LEDGER_PATH = f"{PREP_DIR}/p-ledger-v2.md"
TOOL_PATH = "Tools/FluidFX/perf/p_plans.py"
PIE_PROFILE_PATH = "Tools/FluidFX/pie_profile_fluid.py"
PREREG_LOCK_PATH = "Tools/FluidFX/prereg_lock.py"
HOLD_PATH = "Saved/AgentOps/build_hold.json"
HOLD_EXPIRY_SECONDS = 3 * 3600
CANONICAL_FILES = {"Content/FluidFX/PresetData/TDFluidPresetTuning.json": "9ea0339795e64446ce8962f4816512a2c5284ce8ad69662ebb94481045b95d0b",
                   "Content/FluidFX/PresetData/TDFluidMaterialLook.json": "22a5987c9f5e2fd24f99cfc63b4940af952b9f0e754f331e31d2595b1ea4d73c"}
ACCEPTED_OVERLAY_PATH = "Saved/FluidFX/calib/accepted/overlay.json"
ACCEPTED_OVERLAY_SHA_PREFIX = "2165c922"
ACCEPTED_LAST_OPERATION = "d_w11"
EDITOR_DLLS = tuple(f"Binaries/Win64/UnrealEditor-{name}.dll" for name in ("TDGame", "TDFluidGpu", "TDGameEditor", "TDWorldGen"))
GLOBAL_EDITOR_INI = Path("UnrealEngine") / "5.8" / "Saved" / "Config" / "WindowsEditor" / "EditorSettings.ini"
PROJECT_EDITOR_INIS = ("Config/DefaultEditorSettings.ini", "Saved/Config/WindowsEditor/EditorSettings.ini")
THROTTLE_PATTERN = re.compile(r"^\s*bThrottleCPUWhenNotForeground\s*=\s*(\S+)", re.IGNORECASE | re.MULTILINE)
EDITOR_IMAGE_NAME = "UnrealEditor.exe"
NVIDIA_SMI_COMMAND = ["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"]
BUILD_GATE_CODE = "import json, sys; sys.path.insert(0, sys.argv[1]); import pie_profile_fluid; print(json.dumps(pie_profile_fluid.check_fx7p_build_gate()))"

L0_LEDGER_FILES = (f"{PREP_DIR}/runbook-p-v3.md", LEDGER_PATH, f"{P_DIR}/dev-10.md")
L0_TOOL_FILES = ("Tools/FluidFX/perf/p_plans.py", "Tools/FluidFX/fx4b_budget_judge.py", "Tools/FluidFX/fx7pr_judge.py", PIE_PROFILE_PATH,
                 "Tools/FluidFX/pie_check_fluid.py", "Tools/FluidFX/pie_showcase_fluid.py", "Tools/uemcp.py", "Tools/run_in_editor.py", "Tools/build_and_test.py",
                 PREREG_LOCK_PATH, "Tools/FluidFX/make_approval_sheet.py",
                 "Tools/FluidFX/perf/p/check_p_fixtures.py", "Tools/FluidFX/perf/fixtures/check_budget_table_fixtures.py",
                 "Saved/AgentOps/20261024/fx4b-acc/budget-fixtures/run_fixtures.py")
L0_OPTIONAL_FIXTURE_FILES = tuple(f"{T_DIR}/fixtures_t{lane}.py" for lane in (1, 2, 4))
L0_HISTORY_FILES = tuple(f"{PREP_DIR}/{name}" for name in (
    "runbook-p-v1.md", "runbook-p-v1-amend1.md", "runbook-p-v2.md", "runbook-p-v2-amend2.md", "runbook-p-v2-amend3.md", "runbook-p-v3-amend4.md",
    "runbook-p-v3-amend5.md", "runbook-p-v3-amend6.md", "runbook-p-v3-amend7.md", "runbook-p-v3.r0.md", "runbook-p-v3.r1.md", "runbook-p-v3.r2.md", "check-rtsum.md", "check-v3.md", "check-am4-v3.md",
    "check-am4-lg.md", "check-am5-v3.md", "check-am5-lg.md", "p-ledger.md", "fact-runtime.md", "fact-code.md", "ledger-sweep.md"))
L1_REQUIRED_FILES = ("judge-plan.json", "sp-choice.json", "pilot1-runs.json", "ladder-need.json")
L1_OPTIONAL_FILES = ("pilot1-judge.json", "ladder-runs.json", "ladder-judge.json")
L0_LOCK_PATTERN = re.compile(r"^L0(?:r([1-9]\d*))?\.json$")
L1_LOCK_PATTERN = re.compile(r"^L1-([a-z]+)-([1-9]\d*)\.json$")

EXIT_MEASURE_NEEDED = 3
EXIT_RUNS_FINAL = 4
EXIT_RELOCK = 5
EXIT_TERMINAL = 6
FX7P_RUNS_DIR = "Saved/FluidFX/fx7p"
RUN_SESSION_NAME = "session.json"
RUN_PLAN_NAME = "plan.json"
RUN_RESULT_NAME = "result.json"
RUN_CSV_DIRECTORY = "csv"
RUN_LOG_SUFFIXES = (".log", ".txt")
RUN_TOKEN_PATTERN = re.compile(r"(\d{14})$")
RUN_TOKEN_FORMAT = "%Y%m%d%H%M%S"
WARM_BLOCK_PATTERN = re.compile(r"_warm\d+$")
HOLD_FAILURE_PATTERNS = (re.compile(r"보류 파일이 없다\("), re.compile(r"보류 파일을 읽지 못했다: "), re.compile(r"보류 owner .*가 --holder .*와 다르다"),
                         re.compile(r"보류가 [\d.]+시간 지나 다른 세션이 무시한다: 해제 뒤 다시 획득한다"))
A13_FRAME_P50_MS = 300.0
CONDITION_TOLERANCE = 1e-6
COUNTED_FAILURE_LIMIT = 2
ATTEMPT_LIMIT = 3
UNMEASURABLE_RUN = "측정 불가(실행)"
UNMEASURABLE_ATTEMPTS = "측정 불가(실행: 시도 3회)"

LADDER_NEED_NAME = "ladder-need.json"
PILOT_RUNS_NAME = "pilot1-runs.json"
PILOT_JUDGE_NAME = "pilot1-judge.json"
LADDER_RUNS_NAME = "ladder-runs.json"
LADDER_JUDGE_NAME = "ladder-judge.json"
JUDGE_RUNS_NAME = "judge-runs.json"
JUDGE_NAME = "judge.json"
SP_CHOICE_NAME = "sp-choice.json"
JUDGE_REPEATS = 5
PILOT_PAIR_COUNT = 2
REGIME_PRESET_UNDECIDABLE = "판정 불가(체제)"

SUMMARY_COUNT_KEYS = ("통과", "미달", "판정 불가", "측정 불가", "출하 밖")
FINAL_VERDICTS = ("통과", "미달")
OPEN_VERDICTS = ("판정 불가", "측정 불가")
ALL_AREAS_MARK = "각"
TABLE_MISMATCH = "판정 불가(표 불일치)"
DERIVED_RULE = "B17"
PROPOSED_PASS = "충족(proposed)"
PROPOSED_PASS_LABEL = "[제안] 기준 충족(동등 증명 아님)"
DEFECTS_DIRECTORY = "defects"
DEFECT_FILE_PATTERN = re.compile(r"^D([1-9]\d*)\.md$")
POST_HOC_LINE_PATTERN = re.compile(r"^\s*(?:[-*]\s+)?(?:\*\*)?사후 결함 행\s*:\s*(?:\*\*)?\s*(.+)$", re.MULTILINE)
COLUMN_DEFECT_PATTERN = re.compile(r"^열 결함: (.+)$|\(열 결함: ([^)]+)\)")
CORE_COLUMN_DEFECT_TEXT = "열 결함 의심"
APPROVAL_DECISIONS = ("승인", "거부")
APPROVAL_PENDING = "승인 대기"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
BASIS_LABELS = {"live2_image": "LIVE-2 이미지", "spec_proposal": "[제안] 동등 증명 아님(P-11)"}
BASIS_COUNT_KEYS = ("LIVE-2 이미지", "[제안] 동등 증명 아님(P-11)", "자유 문안", "근거 미표기", "계획 없음")
PB2_COLUMNS_NOT_IN_VERDICT = ("검증만", "둔감", "채택 없음", "conflict", "deferred")
PB2_FXP_SOURCE_GLOBS = ("Saved/FluidFX/calib/batch*/journal*.json", "Saved/FluidFX/calib/batch*/queue-*.json")
OPPOSITE_BACKEND_REASONS = ("출하 프리셋 CPU", "SM5")
SECTION7_LABELS = (("⑫ 미승인 위에서 측정", ("fog", "cloud", "water", "track", "viscous", "input"), "채택 있는 영역"),
                   ("㉒ 미승인 위에서 측정", ("water",), "#17"),
                   ("㊻ 미결(#13 유지) 위에서 측정", ("cloud",), "#13"),
                   ("유지(재현 근거 없음)", ("cloud",), "㊿ #3·#6·#11·#13"),
                   ("유지(재현 근거 없음)", ("track",), "㊿ #9·#12"),
                   ("유지(재현 근거 없음)", ("water",), "㊿ #17"),
                   ("판정 불가(구조) ㉓", ("water",), "수중"),
                   ("판정 불가(구조) ㉓", ("cloud",), "g4 painter"),
                   ("deferred ㉘", ("foliage",), "k2 반감기"),
                   ("㉝ fx9 hvol W 전 제거(FX-P 출하 배치 장면 계획 후보)", ("smoke",), ""),
                   ("D-60 미결(GPU 물 기본 꺼짐·판정 보류)", ("water",), ""),
                   ("PF 환산 ×2.3~3·상한 비교(증명 아님)", AREAS, "모든 성능 판정"))
FXP_FIXED_ITEMS = ({"id": "㉝", "area": "smoke", "text": "fx9 hvol W 전 제거 → FX-P 출하 배치 장면 계획 후보(V3 7절)"},
                   {"id": "D-60", "area": "water", "text": "GPU 물 기본 꺼짐·판정 보류 → 물 GPU는 D-60 뒤 재판정(PR-11)"},
                   {"id": "P-29", "area": "fog", "text": "포그 격자 미측정(P 밖, V3 9.1 Q16)"},
                   {"id": "WA6 ㉙", "area": "water", "text": "WA6는 FX-P 앞 별도(PB2:202, PR-15)"})
PENDING_JUDGEMENTS = ("W-2s-a 젖은 띠·레이스", "RingStamp·PointSource·TileMap 띠 폭", "MeshArray 2.78 cm", "W-3 RA1 3916(명세 4500~6000)", "W-1b L26-l·L26-w")

FINAL_DIR = "Saved/FluidFX/calib/final"
VERDICT_PATH = f"{FINAL_DIR}/verdict-w11.json"
EXPECT_PATH = f"{FINAL_DIR}/expect.json"
CALIB_PLANS_DIR = "Tools/FluidFX/calib/plans"
FRAMES_DIR = "Saved/FluidFX/calib/frames"
PHOTO_PATTERN = re.compile(r"^00_schedule_\d+ms\.png$")
PLAN_AREA_PREFIXES = (("campfire_", "fire"), ("fx8n_", "fire"), ("fx9_", "smoke"), ("fx15a_", "smoke"), ("fx16a_", "smoke"), ("fx16b_", "smoke"),
                      ("river_", "water"), ("w1b_", "water"), ("w3_", "water"), ("w5_", "water"), ("w6_", "water"), ("fog_", "fog"), ("cloud_", "cloud"),
                      ("fx5_", "input"), ("track_", "track"), ("foliage_", "foliage"), ("viscous_", "viscous"))
PLAN_VARIANT_SUFFIXES = ("-wab", "-p20", "-w")
SHEET_MINIMUM_SCENES = 2
SHEET_CAP_WITHOUT_ADOPTION = 4

SCREEN_PERCENTAGE = "r.ScreenPercentage"
OVERRIDE_FPS = "t.OverrideFPS"
FOG_SCATTERING = "r.TDFogScattering"
OVERRIDE_PIE_SCREEN_PERCENTAGE = "r.Editor.Viewport.OverridePIEScreenPercentage"
PIE_PROFILE_CVAR_COUNT = 8
PIE_PROFILE_RECORDED_COUNT = 20
PILOT_SCREEN_PERCENTAGE = 100
LADDER_SCREEN_PERCENTAGES = (130, 160, 200)
LONG_BLOCK_SCENES = {"FI1": 1200, "FI2": 1200}
REQUIRED_COLUMNS = ("FrameTime", "GameThreadTime", "GPUTime")
B5_STEP = {"action": "sleep", "targets": "others", "awake": False}
PLAN_STAGES = {"pilot1": {"repeats": 1, "unstable_repeats": 1}, "ladder": {"repeats": 1, "unstable_repeats": 1}}
PLAN_KEYS = {"run_label", "cvars", "recorded_variables", "hash_paths", "block_frames", "discard_frames", "order", "settle_seconds", "initial_warmup_seconds",
             "fresh_pie_after_gpu", "repeats", "unstable_repeats", "write_fx7p_evidence", "scenes"}
PLAN_SCENE_KEYS = {"id", "level", "targets", "setup", "a", "b", "meta", "gpu_clock", "required_columns", "metric", "block_frames"}
OUTPUT_FILE_NAMES = ("table.json", "pilot1.json", "ladder.json", "ladder-table.json")
SHEET_FILE_NAME = "sheet_areas.json"

STEP_KEYS = {"sleep": {"targets", "awake"}, "visibility": {"targets", "parts", "visible"}, "light": {"targets", "visible"}, "console": {"commands", "restore"},
             "camera": {"mode", "target", "target_label", "distance_cm", "pitch", "yaw", "height_cm"}, "pawn": {"speed", "x", "y", "z", "goal_x", "goal_y"},
             "view": {"arm_cm", "aspect_ratio", "constrain_aspect"}, "wait": {"seconds"}, "actor": {"labels", "hidden", "niagara"}}
FORBIDDEN_ACTIONS = ("configure", "spawn", "despawn", "transmittance")
PART_NAMES = ("cards", "volumes", "embers")
NIAGARA_STATES = {"deactivate": "activate", "activate": "deactivate"}
TOGGLE_BASES = ("L", "Ls", "Lb", "C", "X", "Xn")
TOGGLE_ACTOR_EXTRAS = ("X", "Xn")
TOGGLE_ALLOWED_ACTIONS = {"L": {"sleep", "visibility", "light"}, "Ls": {"sleep"}, "Lb": {"visibility", "light"}, "C": {"console"}, "X": {"actor"}, "Xn": {"actor"}}
TOGGLE_REQUIRED_ACTIONS = {"L": {"sleep", "visibility"}, "Ls": {"sleep"}, "Lb": {"visibility"}, "C": {"console"}, "X": {"actor"}, "Xn": {"actor"}}
SLEEP_TOGGLES = ("L", "Ls", "Lb")
AWAKE_SIDES = ("A", "both", "none")
BACKENDS = ("CPU", "GPU")
METRIC_NAMES = ("ft", "gt", "rt", "ng")
CPU_GRID_RANGE = (16, 256)
EPSILON = 1e-9

TOP_KEYS = ("version", "ledger", "areas_order", "cvar_defaults", "record_only_variables", "items", "derived_rows", "scenes")
LEDGER_KEYS = ("path", "sha256")
CVAR_DEFAULT_KEYS = ("name", "value", "source", "rows")
ITEM_KEYS = ("id", "source", "summary", "limit", "area", "disposition", "reason", "rows", "record")
RECORD_KEYS = ("value", "file", "date", "canonical_sha", "before_w")
DERIVED_KEYS = ("id", "area", "from", "rule", "items")
SCENE_KEYS = ("id", "area", "level", "targets", "toggle", "toggle_summary", "awake_sides", "awake_step_count", "backend", "grid", "backend_basis",
              "water_layer", "block_frames", "camera_source", "hash_paths", "steps", "evidence", "meta", "rows")
STEPS_KEYS = ("setup", "a", "b")
EVIDENCE_KEYS = ("step_count", "columns", "parts", "light", "console", "actor")
ACTOR_EVIDENCE_KEYS = ("hidden", "niagara")
META_KEYS = ("count", "note")
ROW_KEYS = ("id", "metric", "parts_off", "limit_ms", "limit_formula", "items", "note")
PART_KEYS = ("part", "ms", "item")
DISPOSITIONS = ("행", "행 공유", "측정 불가", "출하 밖", "기록")
REASONED_DISPOSITIONS = ("측정 불가", "출하 밖", "기록")
ROW_DISPOSITIONS = ("행", "행 공유")
NO_LIMIT_TEXTS = ("—", "-", "")

ITEM_HEADERS = ("ID", "출처(파일:줄)", "요지", "한도", "영역", "처분")
ROW_HEADERS = ("행 ID", "영역", "맵", "장면 ID", "대상 라벨", "토글", "aw", "parts_off", "지표", "backend·grid", "한도 ms(조성식)", "증거", "출처 줄 ID")
CVAR_HEADERS = ("CVar", "확정 기본값")
EVIDENCE_HEADERS = ("쓰임", "후보 이름 목록")
SECTION_PATTERN = re.compile(r"^##\s+(\d+)\.")
ITEM_ID_PATTERN = re.compile(r"^S\d{3}$")
SCENE_ID_PATTERN = re.compile(r"^[A-Z]{2}\d+[a-z]?$")
ROW_ID_PATTERN = re.compile(r"[A-Z]{2}\d+[a-z]?/(?:ft|gt|rt|ng)")
SCENE_TOKEN_PATTERN = re.compile(r"[A-Z]{2}\d+[a-z]?")
LABEL_PATTERN = re.compile(r"TDGen_[A-Za-z0-9_]+")
LEVEL_PATTERN = re.compile(r"/Game/[A-Za-z0-9_/]+")
FIRST_NUMBER_PATTERN = re.compile(r"-?\d+(?:\.\d+)?")
SHARE_PATTERN = re.compile(r"(\d+(?:\.\d+)?)(?:×(\d+))?·(S\d{3})")
GRID_TERM_PATTERN = re.compile(r"^(?:(CPU|GPU)\s+)?(\d+)²(?:×(\d+))?$")
MISSING_COUNT_PATTERN = re.compile(r"남은 누락\s*(\d+)")
NUMBER_TEXT_PATTERN = re.compile(r"^-?\d+(?:\.\d+)?$")
LEDGER_DISPOSITION_KEYWORDS = ("측정 불가", "출하 밖", "기록")

TABLE_METRICS = {
    "ft": {"label": "FrameTime 쌍 차분", "columns": ["FrameTime"], "pair": "difference", "pdh": False, "backends": [], "pixel_scale": True, "p95_record": True},
    "gt": {"label": "GameThreadTime 쌍 차분", "columns": ["GameThreadTime"], "pair": "difference", "pdh": False, "backends": []},
    "rt": {"label": "RTSUM 쌍 차분(EventWait 제외)", "columns": [], "pair": "difference", "pdh": False, "backends": [],
           "sum_prefix": {"include": "Exclusive/RenderThread/", "exclude": "Exclusive/RenderThread/EventWait"}},
    "ng": {"label": "GPU/TDFluidGpuSolver A p50", "columns": ["GPU/TDFluidGpuSolver"], "pair": "a_only", "pdh": False, "backends": ["GPU"], "p95_record": True},
}
TABLE_CONSEQUENCES = {"충족(proposed)": "[제안] 기준 충족(동등 증명 아님): 화면 승인만 남는다(PR-11)",
                      "미충족": "손잡이 채택 공집합(RC 5절): 사용자 결정 (가) 성능 레버 (나) 예산 개정 (다) 측정 체제(D-59 (e)), FX-P '성능 미달'(PR-11)",
                      "판정 불가": "부분이면 FX-P '성능 부분(a/b/c/d/e)', 측정 불가면 FX-P 앞 측정 경로 명세, 판정 불가 사유는 FX-P 앞에서 해소(PR-11)"}


def normalize_text(text):
    return " ".join(str(text).split())


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def is_text(value):
    return isinstance(value, str) and bool(value.strip())


def is_text_list(value):
    return isinstance(value, list) and all(is_text(entry) for entry in value)


def is_positive_integer(value):
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def is_power_of_two(value):
    return value > 0 and value & (value - 1) == 0


def format_cvar_value(value):
    return str(int(value)) if float(value).is_integer() else repr(float(value))


def resolve_path(root, text):
    path = Path(text)
    return path if path.is_absolute() else Path(root) / path


def relative_posix(root, path):
    resolved = Path(path).resolve()
    try:
        return resolved.relative_to(Path(root).resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def same_path_text(left, right):
    return left.replace("\\", "/").lower() == right.replace("\\", "/").lower()


def same_path(left, right):
    return str(Path(left).resolve()).lower() == str(Path(right).resolve()).lower()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def write_atomic(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.parent / f".{path.name}.tmp-{os.getpid()}"
    with open(temporary, "wb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def read_json_file(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8")), None
    except (OSError, UnicodeDecodeError, ValueError) as error:
        return None, f"{type(error).__name__}: {error}"


def print_error(*parts):
    print(*parts, file=sys.stderr, flush=True)


def run_child(command, root):
    environment = dict(os.environ, PYTHONIOENCODING="utf-8")
    try:
        completed = subprocess.run(command, cwd=str(root), capture_output=True, env=environment, timeout=600)
    except (OSError, subprocess.SubprocessError) as error:
        return None, "", f"{type(error).__name__}: {error}"
    return completed.returncode, completed.stdout.decode("utf-8", errors="replace"), completed.stderr.decode("utf-8", errors="replace")


def forward_child_output(label, stdout, stderr):
    for text in (stdout, stderr):
        for line in text.splitlines():
            if line.strip():
                print_error(f"[{label}] {line}")


def strip_parenthesized(text):
    depth, kept = 0, []
    for character in text:
        if character == "(":
            depth += 1
            continue
        if character == ")" and depth:
            depth -= 1
            continue
        if depth == 0:
            kept.append(character)
    return "".join(kept)


def top_level_parenthesized(text):
    contents, depth, start = [], 0, None
    for index, character in enumerate(text):
        if character == "(":
            if depth == 0:
                start = index + 1
            depth += 1
            continue
        if character == ")" and depth:
            depth -= 1
            if depth == 0:
                contents.append(text[start:index])
    return contents


def first_parenthesized(text):
    if not text.startswith("("):
        return None, text
    depth = 0
    for index, character in enumerate(text):
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                return text[1:index], text[index + 1:]
    return None, text


def first_token(text):
    match = re.match(r"[^\s(]+", text.strip())
    return match.group(0) if match else ""


def read_ledger_sections(text):
    sections, current = {}, None
    for number, line in enumerate(text.splitlines(), 1):
        if line.startswith("## "):
            match = SECTION_PATTERN.match(line)
            current = int(match.group(1)) if match else None
            if current is not None:
                sections[current] = []
            continue
        if current is not None:
            sections[current].append((number, line))
    return sections


def split_table_row(line):
    text = line.strip()
    if len(text) < 2 or not (text.startswith("|") and text.endswith("|")):
        return None
    return [normalize_text(cell.replace("\\|", "|")) for cell in re.split(r"(?<!\\)\|", text[1:-1])]


def is_separator_row(cells):
    return all(re.fullmatch(r":?-{3,}:?", cell) for cell in cells)


def read_tables(lines):
    tables, current = [], None
    for number, line in lines:
        cells = split_table_row(line)
        if cells is None:
            current = None
            continue
        if current is None:
            current = {"header": cells, "rows": []}
            tables.append(current)
            continue
        if not is_separator_row(cells):
            current["rows"].append((number, cells))
    return tables


def read_table_records(lines, headers, where):
    table = next((candidate for candidate in read_tables(lines) if all(name in candidate["header"] for name in headers)), None)
    if table is None:
        return [], [f"LG v2 {where}: 머리글 {'|'.join(headers)} 표가 없다(B16)"]
    index = {name: table["header"].index(name) for name in headers}
    records, problems = [], []
    for number, cells in table["rows"]:
        if len(cells) != len(table["header"]):
            problems.append(f"LG v2 {where} 줄 {number}: 칸 수 {len(cells)} ≠ 머리글 {len(table['header'])}")
            continue
        records.append({"line": number, **{name: cells[index[name]] for name in headers}})
    return records, problems


def read_record_only_names(lines):
    for _, line in lines:
        marker = line.find("**기록만:**")
        if marker < 0:
            continue
        return [first_token(segment) for segment in line[marker + len("**기록만:**"):].split(" · ") if first_token(segment)]
    return None


def parse_ledger_text(text):
    sections = read_ledger_sections(text)
    problems = [f"LG v2 {number}절이 없다" for number in (1, 2, 4, 5, 6) if number not in sections]
    items, item_problems = read_table_records(sections.get(1, []), ITEM_HEADERS, "1절")
    rows, row_problems = read_table_records(sections.get(2, []), ROW_HEADERS, "2절")
    cvars, cvar_problems = read_table_records(sections.get(4, []), CVAR_HEADERS, "4절")
    evidence, evidence_problems = read_table_records(sections.get(5, []), EVIDENCE_HEADERS, "5절")
    record_only = read_record_only_names(sections.get(4, []))
    if record_only is None:
        problems.append("LG v2 4절: '**기록만:**' 줄이 없다")
    section6 = "\n".join(line for _, line in sections.get(6, []))
    return {"items": items, "rows": rows, "cvars": cvars, "evidence": evidence, "record_only": record_only or [], "section6": section6,
            "problems": problems + item_problems + row_problems + cvar_problems + evidence_problems}


def parse_disposition(text):
    for kind in REASONED_DISPOSITIONS:
        if not text.startswith(kind):
            continue
        reason, tail = first_parenthesized(text[len(kind):])
        if reason is None:
            return {"kind": None, "problem": f"처분 없음: '{kind}' 뒤에 (사유)가 없거나 괄호 짝이 없다"}
        if tail.strip():
            return {"kind": None, "problem": f"처분 둘: '{kind}(…)' 뒤에 '{normalize_text(tail)}'"}
        return {"kind": kind, "rows": [], "reason": normalize_text(reason)}
    if not text.startswith("행 "):
        return {"kind": None, "problem": f"처분 없음 또는 알 수 없는 처분 '{text}'"}
    outside = strip_parenthesized(text[2:])
    if any(keyword in outside for keyword in LEDGER_DISPOSITION_KEYWORDS):
        return {"kind": None, "problem": f"처분 둘: 행 처분 괄호 밖에 다른 처분 낱말 '{normalize_text(outside)}'"}
    head = outside.split(";")[0]
    rows = ROW_ID_PATTERN.findall(head)
    if not rows:
        return {"kind": None, "problem": "처분 없음: '행' 뒤에 행 ID가 없다"}
    leftover = ROW_ID_PATTERN.sub(" ", head).replace("·", " ").split()
    if leftover not in ([], ["공유"]):
        return {"kind": None, "problem": f"행 처분에 알 수 없는 말 {leftover}"}
    return {"kind": "행 공유" if leftover else "행", "rows": rows, "reason": None}


def ledger_toggle_symbol(cell):
    text = cell.strip()
    if text.startswith("setup"):
        _, separator, rest = text.partition(";")
        if not separator:
            return None
        text = rest.strip()
    match = re.match(r"[^:(\s]+", text)
    return match.group(0) if match else None


def parse_toggle(symbol):
    if not isinstance(symbol, str) or not symbol:
        return None
    parts = symbol.split("+")
    base, extras = parts[0], parts[1:]
    if base not in TOGGLE_BASES or len(extras) > 1:
        return None
    if extras and (extras[0] not in TOGGLE_ACTOR_EXTRAS or base not in SLEEP_TOGGLES):
        return None
    return base, extras[0] if extras else None


def convert_ledger_grid(cell, fluid_labels):
    text = normalize_text(strip_parenthesized(cell))
    if text.startswith("없음"):
        return {"backend": None, "grid": {}, "count": None}, None
    backend, sizes, count = None, [], None
    for term in (part.strip() for part in text.split("+")):
        match = GRID_TERM_PATTERN.match(term)
        if not match:
            return None, f"grid 항 '{term}'을 읽을 수 없다"
        term_backend = match.group(1) or backend
        if term_backend is None:
            return None, "grid 첫 항에 CPU·GPU가 없다"
        if backend is not None and term_backend != backend:
            return None, "grid 항마다 backend가 다르다"
        backend = term_backend
        sizes.append(int(match.group(2)))
        if match.group(3):
            count = int(match.group(3))
    if count is not None:
        if len(sizes) != 1 or len(fluid_labels) != 1:
            return None, "grid ×n은 항 하나·유체 라벨 하나에만 쓴다(F7)"
        return {"backend": backend, "grid": {fluid_labels[0]: sizes[0]}, "count": count}, None
    if len(sizes) != len(fluid_labels):
        return None, f"grid 항 {len(sizes)}개 ≠ 유체 대상 라벨 {len(fluid_labels)}개(a²+b² = 대상 라벨 순서, F7)"
    return {"backend": backend, "grid": dict(zip(fluid_labels, sizes)), "count": None}, None


def evaluate_formula(text):
    total = 0.0
    for term in text.split("+"):
        product = 1.0
        for factor in term.split("×"):
            if not NUMBER_TEXT_PATTERN.match(factor.strip()):
                return None
            product *= float(factor.strip())
        total += product
    return total


def split_limit_cell(cell):
    number_match = FIRST_NUMBER_PATTERN.search(cell)
    number = float(number_match.group(0)) if number_match else None
    _, separator, rest = cell.partition("=")
    formula = normalize_text(rest.split("(")[0]) if separator else ""
    return number, formula, [normalize_text(content) for content in top_level_parenthesized(cell)]


def ledger_share_totals(cell):
    totals = {}
    for value, multiplier, item in SHARE_PATTERN.findall(cell):
        totals[item] = totals.get(item, 0.0) + float(value) * (int(multiplier) if multiplier else 1)
    return totals


def has_limit(limit_text):
    return normalize_text(limit_text) not in NO_LIMIT_TEXTS


def level_map_path(level):
    return "Content/" + level[len("/Game/"):] + ".umap"


def expand_ledger_maps(cell):
    expanded = re.sub(r"~([A-Za-z0-9_]+)", r"/Game/FluidFX/Maps/LV_TDFluid\1", cell)
    return LEVEL_PATTERN.findall(expanded)


def step_targets(step):
    return step.get("targets", "scene")


def parse_console_commands(commands):
    pairs = []
    for command in commands:
        parts = str(command).split()
        if len(parts) != 2 or not NUMBER_TEXT_PATTERN.match(parts[1]):
            return None
        pairs.append((parts[0], float(parts[1])))
    return pairs


def find_missing_unknown_keys(item, keys, where):
    if not isinstance(item, dict):
        return [f"{where}: 객체가 아니다"]
    problems = [f"{where}: 키 {key} 없음" for key in keys if key not in item]
    problems.extend(f"{where}: 허용 밖 키 {key}" for key in item if key not in keys)
    return problems


def find_targets_problems(where, targets):
    if targets in ("scene", "others"):
        return []
    if is_text_list(targets) and targets:
        return []
    return [f"{where}: targets는 \"scene\" 또는 라벨 목록"]


def find_step_value_problems(where, action, step):
    problems = find_targets_problems(where, step_targets(step)) if action in ("sleep", "visibility", "light") else []
    if action == "sleep" and not isinstance(step.get("awake"), bool):
        problems.append(f"{where}: sleep awake는 참·거짓")
    if action in ("visibility", "light") and not isinstance(step.get("visible"), bool):
        problems.append(f"{where}: {action} visible은 참·거짓")
    if action == "visibility" and not (isinstance(step.get("parts"), list) and step["parts"] and all(part in PART_NAMES for part in step["parts"])):
        problems.append(f"{where}: visibility parts는 {PART_NAMES} 중 비지 않은 목록")
    if action == "console":
        if not (is_text_list(step.get("commands")) and step["commands"]):
            problems.append(f"{where}: console commands는 비지 않은 문자열 목록")
        if "restore" in step and not isinstance(step["restore"], bool):
            problems.append(f"{where}: console restore는 참·거짓")
    if action == "actor":
        if not (is_text_list(step.get("labels")) and step["labels"]):
            problems.append(f"{where}: actor labels는 비지 않은 라벨 목록")
        if "hidden" not in step and "niagara" not in step:
            problems.append(f"{where}: actor에 hidden·niagara 중 하나가 필요하다")
        if "hidden" in step and not isinstance(step["hidden"], bool):
            problems.append(f"{where}: actor hidden은 참·거짓")
        if "niagara" in step and step["niagara"] not in NIAGARA_STATES:
            problems.append(f"{where}: actor niagara는 deactivate·activate")
    if action == "wait" and not (is_number(step.get("seconds")) and step["seconds"] >= 0):
        problems.append(f"{where}: wait seconds는 0 이상 숫자")
    if action == "camera" and not ("mode" in step or "target" in step or "target_label" in step):
        problems.append(f"{where}: camera에 mode·target·target_label 중 하나가 필요하다")
    return problems


def find_step_problems(where, steps):
    if not isinstance(steps, list):
        return [f"{where}: 단계 목록이 아니다"]
    problems = []
    for index, step in enumerate(steps):
        label = f"{where}[{index}]"
        if not isinstance(step, dict) or not isinstance(step.get("action"), str):
            problems.append(f"{label}: action 있는 객체가 아니다")
            continue
        action = step["action"]
        if action in FORBIDDEN_ACTIONS:
            problems.append(f"{label}: 금지 단계 {action}(PR-2 덧씌움, 3.3)")
            continue
        allowed = STEP_KEYS.get(action)
        if allowed is None:
            problems.append(f"{label}: 허용 밖 동작 {action}(T1 ④)")
            continue
        if "tick" in step:
            problems.append(f"{label}: tick 키는 없다(E2)")
        unknown = sorted(set(step) - allowed - {"action", "tick"})
        if unknown:
            problems.append(f"{label}: 허용 밖 키 {unknown}(T1 ④)")
        problems.extend(find_step_value_problems(label, action, step))
    return problems


def find_evidence_schema_problems(where, evidence):
    problems = find_missing_unknown_keys(evidence, EVIDENCE_KEYS, where)
    if problems:
        return problems
    if not isinstance(evidence["step_count"], bool):
        problems.append(f"{where}.step_count: 참·거짓")
    if not is_text_list(evidence["columns"]) or len(set(evidence["columns"])) != len(evidence["columns"]):
        problems.append(f"{where}.columns: 겹치지 않는 열 이름 목록")
    parts = evidence["parts"]
    if not isinstance(parts, dict) or not all(is_text(label) and isinstance(names, list) and names and all(name in PART_NAMES for name in names) for label, names in parts.items()):
        problems.append(f"{where}.parts: {{라벨: [{'·'.join(PART_NAMES)}]}}")
    if not isinstance(evidence["light"], bool):
        problems.append(f"{where}.light: 참·거짓")
    console = evidence["console"]
    if not isinstance(console, dict) or not all(is_text(name) and isinstance(values, dict) and set(values) == {"A", "B"} and all(is_number(value) for value in values.values())
                                                for name, values in console.items()):
        problems.append(f"{where}.console: {{CVar: {{A: 숫자, B: 숫자}}}}")
    actor = evidence["actor"]
    if actor is None:
        return problems
    actor_problems = find_missing_unknown_keys(actor, ACTOR_EVIDENCE_KEYS, f"{where}.actor")
    if actor_problems:
        return problems + actor_problems
    if not all(isinstance(actor[key], bool) for key in ACTOR_EVIDENCE_KEYS):
        problems.append(f"{where}.actor: hidden·niagara는 참·거짓")
    return problems


def find_row_schema_problems(where, row):
    problems = find_missing_unknown_keys(row, ROW_KEYS, where)
    if problems:
        return problems
    if not (isinstance(row["id"], str) and ROW_ID_PATTERN.fullmatch(row["id"])):
        problems.append(f"{where}: id는 <장면>/<ft|gt|rt|ng>")
    if row["metric"] not in METRIC_NAMES:
        problems.append(f"{where}: metric은 {METRIC_NAMES} 중 하나")
    if not isinstance(row["parts_off"], list):
        problems.append(f"{where}: parts_off는 목록")
    else:
        for index, part in enumerate(row["parts_off"]):
            part_where = f"{where}.parts_off[{index}]"
            part_problems = find_missing_unknown_keys(part, PART_KEYS, part_where)
            if part_problems:
                problems.extend(part_problems)
                continue
            if not is_text(part["part"]) or not (is_number(part["ms"]) and part["ms"] > 0) or not (isinstance(part["item"], str) and ITEM_ID_PATTERN.match(part["item"])):
                problems.append(f"{part_where}: part 문자열·ms 양수·item S###")
    if not (is_number(row["limit_ms"]) and row["limit_ms"] > 0):
        problems.append(f"{where}: limit_ms는 양수")
    if not is_text(row["limit_formula"]):
        problems.append(f"{where}: limit_formula는 비지 않은 문자열")
    if not (is_text_list(row["items"]) and row["items"] and all(ITEM_ID_PATTERN.match(item) for item in row["items"])):
        problems.append(f"{where}: items는 S### 비지 않은 목록")
    if row["note"] is not None and not isinstance(row["note"], str):
        problems.append(f"{where}: note는 문자열 또는 null")
    return problems


def find_scene_schema_problems(where, scene):
    problems = find_missing_unknown_keys(scene, SCENE_KEYS, where)
    if problems:
        return problems
    if not (isinstance(scene["id"], str) and SCENE_ID_PATTERN.match(scene["id"])):
        problems.append(f"{where}: id 형식(예 FI1·FI5a)")
    if scene["area"] not in AREAS:
        problems.append(f"{where}: area {scene['area']!r}는 {AREAS} 중 하나")
    if not (isinstance(scene["level"], str) and scene["level"].startswith("/Game/")):
        problems.append(f"{where}: level은 /Game/ 경로")
    if not is_text_list(scene["targets"]) or len(set(scene["targets"])) != len(scene["targets"]):
        problems.append(f"{where}: targets는 겹치지 않는 라벨 목록")
    for key in ("toggle", "toggle_summary", "backend_basis", "camera_source"):
        if not is_text(scene[key]):
            problems.append(f"{where}: {key}는 비지 않은 문자열")
    if scene["awake_sides"] not in AWAKE_SIDES:
        problems.append(f"{where}: awake_sides는 {AWAKE_SIDES} 중 하나")
    if not (isinstance(scene["awake_step_count"], int) and not isinstance(scene["awake_step_count"], bool) and scene["awake_step_count"] >= 0):
        problems.append(f"{where}: awake_step_count는 0 이상 정수")
    if scene["backend"] not in BACKENDS + (None,):
        problems.append(f"{where}: backend는 CPU·GPU·null")
    if not (isinstance(scene["grid"], dict) and all(is_text(label) and is_positive_integer(size) for label, size in scene["grid"].items())):
        problems.append(f"{where}: grid는 {{라벨: 양의 정수}}")
    if not isinstance(scene["water_layer"], bool):
        problems.append(f"{where}: water_layer는 참·거짓")
    if scene["block_frames"] is not None and not is_positive_integer(scene["block_frames"]):
        problems.append(f"{where}: block_frames는 양의 정수 또는 null")
    if not is_text_list(scene["hash_paths"]) or len(set(scene["hash_paths"])) != len(scene["hash_paths"]):
        problems.append(f"{where}: hash_paths는 겹치지 않는 경로 목록")
    steps_problems = find_missing_unknown_keys(scene["steps"], STEPS_KEYS, f"{where}.steps")
    problems.extend(steps_problems)
    if not steps_problems:
        for side in STEPS_KEYS:
            problems.extend(find_step_problems(f"{where}.steps.{side}", scene["steps"][side]))
    problems.extend(find_evidence_schema_problems(f"{where}.evidence", scene["evidence"]))
    meta_problems = find_missing_unknown_keys(scene["meta"], META_KEYS, f"{where}.meta")
    problems.extend(meta_problems)
    if not meta_problems:
        count = scene["meta"]["count"]
        if count is not None and not (isinstance(count, int) and not isinstance(count, bool) and count >= 0):
            problems.append(f"{where}.meta.count: 0 이상 정수 또는 null")
        if scene["meta"]["note"] is not None and not isinstance(scene["meta"]["note"], str):
            problems.append(f"{where}.meta.note: 문자열 또는 null")
    if not isinstance(scene["rows"], list) or not scene["rows"]:
        problems.append(f"{where}: rows는 비지 않은 목록")
        return problems
    for index, row in enumerate(scene["rows"]):
        problems.extend(find_row_schema_problems(f"{where}.rows[{index}]", row))
    return problems


def find_item_schema_problems(where, item):
    problems = find_missing_unknown_keys(item, ITEM_KEYS, where)
    if problems:
        return problems
    if not (isinstance(item["id"], str) and ITEM_ID_PATTERN.match(item["id"])):
        problems.append(f"{where}: id는 S###")
    for key in ("source", "summary", "limit", "area"):
        if not is_text(item[key]):
            problems.append(f"{where}: {key}는 비지 않은 문자열")
    if item["disposition"] not in DISPOSITIONS:
        problems.append(f"{where}: 처분 없음 또는 처분 둘: disposition {item['disposition']!r}는 {DISPOSITIONS} 중 하나")
    if item["reason"] is not None and not isinstance(item["reason"], str):
        problems.append(f"{where}: reason은 문자열 또는 null")
    if not is_text_list(item["rows"]):
        problems.append(f"{where}: rows는 행 ID 목록")
    if item["record"] is not None:
        record_problems = find_missing_unknown_keys(item["record"], RECORD_KEYS, f"{where}.record")
        problems.extend(record_problems)
        if not record_problems and item["record"]["before_w"] not in (True, False, None):
            problems.append(f"{where}.record.before_w: 참·거짓·null")
    return problems


def find_areas_schema_problems(document):
    problems = find_missing_unknown_keys(document, TOP_KEYS, "p_areas")
    if problems:
        return problems
    if document["version"] != 1:
        problems.append("p_areas: version은 1")
    ledger_problems = find_missing_unknown_keys(document["ledger"], LEDGER_KEYS, "p_areas.ledger")
    problems.extend(ledger_problems)
    if not ledger_problems and not (is_text(document["ledger"]["path"]) and re.fullmatch(r"[0-9a-f]{64}", str(document["ledger"]["sha256"]))):
        problems.append("p_areas.ledger: path 문자열·sha256 64자")
    if document["areas_order"] != list(AREAS):
        problems.append(f"p_areas.areas_order ≠ V3 1절 $AREAS {list(AREAS)}")
    if not isinstance(document["cvar_defaults"], list):
        problems.append("p_areas.cvar_defaults: 목록이 아니다")
    else:
        for index, entry in enumerate(document["cvar_defaults"]):
            entry_problems = find_missing_unknown_keys(entry, CVAR_DEFAULT_KEYS, f"p_areas.cvar_defaults[{index}]")
            problems.extend(entry_problems)
            if not entry_problems and not (is_text(entry["name"]) and is_number(entry["value"]) and is_text(entry["source"]) and is_text_list(entry["rows"])):
                problems.append(f"p_areas.cvar_defaults[{index}]: name·source 문자열, value 숫자, rows 문자열 목록")
        names = [entry.get("name") for entry in document["cvar_defaults"] if isinstance(entry, dict)]
        if len(set(names)) != len(names):
            problems.append("p_areas.cvar_defaults: name이 겹친다")
    if not is_text_list(document["record_only_variables"]) or len(set(document["record_only_variables"])) != len(document["record_only_variables"]):
        problems.append("p_areas.record_only_variables: 겹치지 않는 CVar 이름 목록")
    for key, finder in (("items", find_item_schema_problems), ("scenes", find_scene_schema_problems)):
        if not isinstance(document[key], list) or not document[key]:
            problems.append(f"p_areas.{key}: 비지 않은 목록")
            continue
        for index, entry in enumerate(document[key]):
            problems.extend(finder(f"p_areas.{key}[{index}]", entry))
    if not isinstance(document["derived_rows"], list):
        problems.append("p_areas.derived_rows: 목록이 아니다")
    else:
        for index, derived in enumerate(document["derived_rows"]):
            where = f"p_areas.derived_rows[{index}]"
            derived_problems = find_missing_unknown_keys(derived, DERIVED_KEYS, where)
            problems.extend(derived_problems)
            if not derived_problems and not (isinstance(derived["id"], str) and SCENE_ID_PATTERN.match(derived["id"]) and derived["area"] in AREAS
                                             and isinstance(derived["from"], str) and ROW_ID_PATTERN.fullmatch(derived["from"]) and is_text(derived["rule"])
                                             and is_text_list(derived["items"]) and derived["items"]):
                problems.append(f"{where}: id 장면 형식·area 영역·from 행 ID·rule 문자열·items 비지 않은 목록")
    if problems:
        return problems
    for key, label in (("items", "항목 ID"), ("scenes", "장면 ID")):
        identifiers = [entry["id"] for entry in document[key]]
        problems.extend(f"p_areas: {label} {identifier} 겹침" for identifier in sorted({value for value in identifiers if identifiers.count(value) > 1}))
    return problems


def derived_row_id(derived):
    return f"{derived['id']}/{derived['from'].split('/')[1]}"


def collect_rows(document):
    rows = {}
    for scene in document["scenes"]:
        for row in scene["rows"]:
            rows.setdefault(row["id"], (scene, row))
    return rows


def effective_item_rows(item, derived_by_row):
    rows = list(item["rows"])
    rows.extend(derived_by_row[row]["from"] for row in item["rows"] if row in derived_by_row)
    return rows


def find_item_rule_problems(document, known_rows, derived_by_row):
    problems = []
    for item in document["items"]:
        where = f"항목 {item['id']}"
        disposition = item["disposition"]
        if disposition == "기록" and has_limit(item["limit"]):
            problems.append(f"{where}: 한도 있는 항목은 기록 불가(한도 '{item['limit']}', 3.2 ①)")
        if disposition in ROW_DISPOSITIONS and not item["rows"]:
            problems.append(f"{where}: {disposition} 처분에 rows가 없다")
        if disposition not in ROW_DISPOSITIONS and item["rows"]:
            problems.append(f"{where}: 행·행 공유만 rows를 가진다({disposition})")
        if disposition in REASONED_DISPOSITIONS and not is_text(item["reason"]):
            problems.append(f"{where}: {disposition} 처분에 사유가 없다")
        if disposition == "기록" and item["record"] is None:
            problems.append(f"{where}: 기록 처분에 record가 없다")
        if disposition != "기록" and item["record"] is not None:
            problems.append(f"{where}: 기록 밖 처분에 record가 있다")
        for row in item["rows"]:
            if row not in known_rows:
                problems.append(f"{where}: 없는 행 인용 {row}")
                continue
            if item["id"] not in known_rows[row]["items"]:
                problems.append(f"{where}: 행 미인용(양방향): {row}의 items에 {item['id']}가 없다")
    return problems


def find_row_rule_problems(document, items_by_id, derived_by_row):
    problems = []
    for scene in document["scenes"]:
        for row in scene["rows"]:
            where = f"행 {row['id']}"
            if row["id"] != f"{scene['id']}/{row['metric']}":
                problems.append(f"{where}: id ≠ <장면 {scene['id']}>/<지표 {row['metric']}>")
            if row["metric"] == "ng" and scene["backend"] != "GPU":
                problems.append(f"{where}: ng는 선언 GPU만(장면 backend {scene['backend']})")
            formula_value = evaluate_formula(row["limit_formula"])
            if formula_value is None:
                problems.append(f"{where}: 조성식 '{row['limit_formula']}'을 읽을 수 없다(숫자·+·× 만)")
            elif abs(formula_value - row["limit_ms"]) > EPSILON:
                problems.append(f"{where}: 조성식 값 {formula_value:g} ≠ limit_ms {row['limit_ms']:g}")
            share_total = sum(part["ms"] for part in row["parts_off"])
            if abs(share_total - row["limit_ms"]) > EPSILON:
                problems.append(f"{where}: parts_off 몫 합 {share_total:g} ≠ limit_ms {row['limit_ms']:g}(3.2 ③)")
            for part in row["parts_off"]:
                if part["item"] not in row["items"]:
                    problems.append(f"{where}: parts_off 출처 {part['item']}가 행 items에 없다")
            for item_id in row["items"]:
                item = items_by_id.get(item_id)
                if item is None:
                    problems.append(f"{where}: 없는 항목 {item_id} 인용")
                    continue
                if row["id"] not in effective_item_rows(item, derived_by_row):
                    problems.append(f"{where}: 행 미인용(양방향): 항목 {item_id}의 rows에 {row['id']}가 없다")
    return problems


def find_derived_rule_problems(document, scene_rows, items_by_id):
    problems = []
    scene_ids = {scene["id"] for scene in document["scenes"]}
    for derived in document["derived_rows"]:
        where = f"파생 행 {derived['id']}"
        if derived["id"] in scene_ids:
            problems.append(f"{where}: 장면 ID와 겹친다")
        if derived["from"] not in scene_rows:
            problems.append(f"{where}: from {derived['from']}이 장면 행에 없다")
        for item_id in derived["items"]:
            item = items_by_id.get(item_id)
            if item is None:
                problems.append(f"{where}: 없는 항목 {item_id} 인용")
            elif derived_row_id(derived) not in item["rows"]:
                problems.append(f"{where}: 행 미인용(양방향): 항목 {item_id}의 rows에 {derived_row_id(derived)}가 없다")
    return problems


def step_identity(step):
    action = step["action"]
    targets = json.dumps(step_targets(step), sort_keys=True, ensure_ascii=False)
    if action == "visibility":
        return (action, targets, tuple(step.get("parts") or ())), step.get("visible")
    if action == "sleep":
        return (action, targets), step.get("awake")
    if action == "light":
        return (action, targets), step.get("visible")
    if action == "console":
        pairs = parse_console_commands(step.get("commands") or []) or []
        return (action, tuple(name for name, _ in pairs)), tuple(value for _, value in pairs)
    if action == "actor":
        return (action, tuple(step.get("labels") or ())), (step.get("hidden"), step.get("niagara"))
    return (action, json.dumps(step, sort_keys=True, ensure_ascii=False)), None


def expected_a_value(identity, b_value, defaults):
    action = identity[0]
    if action in ("sleep", "visibility", "light"):
        return True
    if action == "console":
        return tuple(defaults.get(name) for name in identity[1])
    if action == "actor":
        hidden, niagara = b_value
        return (None if hidden is None else not hidden, NIAGARA_STATES.get(niagara) if niagara is not None else None)
    return b_value


def is_valid_b_value(identity, value):
    action = identity[0]
    if action in ("sleep", "visibility", "light"):
        return value is False
    if action == "console":
        return all(entry == 0 for entry in value)
    if action == "actor":
        hidden, niagara = value
        return hidden in (True, None) and niagara in ("deactivate", None)
    return False


def find_toggle_problems(scene, defaults):
    where = f"장면 {scene['id']}"
    parsed = parse_toggle(scene["toggle"])
    if parsed is None:
        return [f"{where}: 토글 {scene['toggle']!r}는 {TOGGLE_BASES} 또는 L·Ls·Lb+X·Xn(3.4)"]
    base, extra = parsed
    allowed = TOGGLE_ALLOWED_ACTIONS[base] | ({"actor"} if extra else set())
    required = TOGGLE_REQUIRED_ACTIONS[base] | ({"actor"} if extra else set())
    problems = []
    for side in ("a", "b"):
        actions = {step["action"] for step in scene["steps"][side]}
        if actions - allowed:
            problems.append(f"{where}: 토글 {scene['toggle']} {side}에 허용 밖 단계 {sorted(actions - allowed)}(3.4)")
        if required - actions:
            problems.append(f"{where}: 토글 {scene['toggle']} {side}에 {sorted(required - actions)} 단계가 없다(3.4)")
    actor_kind = extra or (base if base in TOGGLE_ACTOR_EXTRAS else None)
    expected_actor_keys = {"X": {"hidden"}, "Xn": {"niagara"}}.get(actor_kind, set())
    for side in ("a", "b"):
        for step in scene["steps"][side]:
            if step["action"] == "actor" and set(step) - {"action", "labels"} != expected_actor_keys:
                problems.append(f"{where}: {actor_kind} 토글 actor 키는 labels·{'·'.join(sorted(expected_actor_keys)) or '-'}만(E2), 받은 키 {sorted(set(step) - {'action'})}")
    b_identities = sorted((step_identity(step) for step in scene["steps"]["b"]), key=lambda entry: repr(entry[0]))
    a_identities = sorted((step_identity(step) for step in scene["steps"]["a"]), key=lambda entry: repr(entry[0]))
    if [identity for identity, _ in a_identities] != [identity for identity, _ in b_identities]:
        problems.append(f"{where}: a = b 속성 반대가 아니다(단계 대상·속성 집합이 다르다, 3.4)")
        return problems
    for (identity, a_value), (_, b_value) in zip(a_identities, b_identities):
        if not is_valid_b_value(identity, b_value):
            problems.append(f"{where}: b 단계 {identity[0]} 값 {b_value!r}가 끔이 아니다(3.4)")
        elif a_value != expected_a_value(identity, b_value, defaults):
            problems.append(f"{where}: a 단계 {identity[0]} 값 {a_value!r} ≠ b의 반대 {expected_a_value(identity, b_value, defaults)!r}(3.4)")
    return problems


def expected_evidence(scene):
    base, _ = parse_toggle(scene["toggle"])
    parts = {}
    for step in scene["steps"]["b"]:
        if step["action"] != "visibility":
            continue
        labels = step_targets(step) if isinstance(step_targets(step), list) else scene["targets"]
        for label in labels:
            parts[label] = sorted(set(parts.get(label, [])) | set(step["parts"]))
    a_console = dict(pair for step in scene["steps"]["a"] if step["action"] == "console" for pair in parse_console_commands(step["commands"]) or [])
    b_console = dict(pair for step in scene["steps"]["b"] if step["action"] == "console" for pair in parse_console_commands(step["commands"]) or [])
    actor_steps = [step for step in scene["steps"]["b"] if step["action"] == "actor"]
    return {"step_count": base in SLEEP_TOGGLES, "parts": parts, "light": any(step["action"] == "light" for step in scene["steps"]["b"]),
            "console": {name: {"A": a_console.get(name), "B": value} for name, value in b_console.items()},
            "actor": {"hidden": any("hidden" in step for step in actor_steps), "niagara": any("niagara" in step for step in actor_steps)} if actor_steps else None}


def find_evidence_rule_problems(scene):
    where = f"장면 {scene['id']}"
    evidence = scene["evidence"]
    expected = expected_evidence(scene)
    problems = []
    if evidence["step_count"] != expected["step_count"]:
        problems.append(f"{where}: evidence.step_count {evidence['step_count']} ≠ 토글 {scene['toggle']}(L·Ls·Lb만 참)")
    if {label: sorted(names) for label, names in evidence["parts"].items()} != expected["parts"]:
        problems.append(f"{where}: evidence.parts {evidence['parts']} ≠ b visibility 단계 {expected['parts']}")
    if evidence["light"] != expected["light"]:
        problems.append(f"{where}: evidence.light {evidence['light']} ≠ b light 단계 유무")
    console = {name: {"A": float(values["A"]), "B": float(values["B"])} for name, values in evidence["console"].items()}
    if console != expected["console"]:
        problems.append(f"{where}: evidence.console {evidence['console']} ≠ a/b console 단계 {expected['console']}")
    if evidence["actor"] != expected["actor"]:
        problems.append(f"{where}: evidence.actor {evidence['actor']} ≠ b actor 단계 {expected['actor']}")
    base, _ = parse_toggle(scene["toggle"])
    if (base == "C") != bool(evidence["columns"]):
        problems.append(f"{where}: evidence.columns는 C 토글에만 있다(5절 EC)")
    return problems


def expected_awake_sides(base, targets):
    if base in ("L", "Ls"):
        return "A"
    if base == "Lb":
        return "both"
    return "both" if targets else "none"


def find_scene_rule_problems(root, scene, defaults):
    where = f"장면 {scene['id']}"
    problems = find_toggle_problems(scene, defaults)
    parsed = parse_toggle(scene["toggle"])
    if parsed is not None:
        problems.extend(find_evidence_rule_problems(scene))
        expected_sides = expected_awake_sides(parsed[0], scene["targets"])
        if scene["awake_sides"] != expected_sides:
            problems.append(f"{where}: awake_sides {scene['awake_sides']} ≠ 토글 {scene['toggle']}·targets로 정한 {expected_sides}(3.4)")
    expected_count = 0 if scene["awake_sides"] == "none" else len(scene["targets"])
    if scene["awake_step_count"] != expected_count:
        problems.append(f"{where}: awake_step_count {scene['awake_step_count']} ≠ 깨어 스텝하는 대상 수 {expected_count}")
    if (scene["awake_sides"] == "none") != (not scene["targets"]):
        problems.append(f"{where}: aw none ⇔ targets [](3.3)")
    if set(scene["grid"]) != set(scene["targets"]):
        problems.append(f"{where}: grid 라벨 {sorted(scene['grid'])} ≠ targets {sorted(scene['targets'])}(F7, 라이브 유체만)")
    if (scene["backend"] is None) != (not scene["targets"]):
        problems.append(f"{where}: backend null ⇔ 유체 대상 없음")
    for label, size in scene["grid"].items():
        if scene["backend"] == "GPU" and not is_power_of_two(size):
            problems.append(f"{where}: GPU 격자 {label} {size}는 2의 거듭제곱(FC F2)")
        if scene["backend"] == "CPU" and not CPU_GRID_RANGE[0] <= size <= CPU_GRID_RANGE[1]:
            problems.append(f"{where}: CPU 격자 {label} {size}는 {CPU_GRID_RANGE[0]}~{CPU_GRID_RANGE[1]}(FC F2)")
    if scene["block_frames"] != LONG_BLOCK_SCENES.get(scene["id"]):
        problems.append(f"{where}: block_frames {scene['block_frames']} ≠ {LONG_BLOCK_SCENES.get(scene['id'])}(FI1·FI2 1200, 그 밖 null, PR-3)")
    camera_count = sum(step["action"] == "camera" for step in scene["steps"]["setup"])
    if camera_count != 1 or any(step["action"] == "camera" for side in ("a", "b") for step in scene["steps"][side]):
        problems.append(f"{where}: 카메라 단계는 setup에 1개(지금 setup {camera_count}개, 3.3)")
    for side in STEPS_KEYS:
        for index, step in enumerate(scene["steps"][side]):
            label = f"{where} steps.{side}[{index}]"
            if step_targets(step) == "others":
                problems.append(f"{label}: targets \"others\"는 생성기가 붙이는 B5만(visibility others 금지)")
            if step["action"] in ("sleep", "visibility", "light") and isinstance(step_targets(step), list) and not set(step_targets(step)) <= set(scene["targets"]):
                problems.append(f"{label}: {step['action']} 대상이 targets 밖")
            if step["action"] != "console":
                continue
            pairs = parse_console_commands(step["commands"])
            if pairs is None:
                problems.append(f"{label}: console 명령은 '<CVar> <숫자>'")
                continue
            for name, _ in pairs:
                if name == SCREEN_PERCENTAGE:
                    problems.append(f"{label}: r.ScreenPercentage 단계는 생성기가 붙인다(C5)")
                elif name not in defaults:
                    problems.append(f"{label}: console CVar {name}이 cvar_defaults(LG v2 4절)에 없다(A15)")
            if side == "setup" and step.get("restore") is not True:
                problems.append(f"{label}: setup console restore 참 아님(E3)")
    map_path = level_map_path(scene["level"])
    if not (Path(root) / map_path).is_file():
        problems.append(f"{where}: 맵 파일 {map_path}이 없다")
    problems.extend(f"{where}: hash_paths 파일 {path}이 없다" for path in scene["hash_paths"] if not (Path(root) / path).is_file())
    return problems


def find_rule_problems(root, document):
    items_by_id = {item["id"]: item for item in document["items"]}
    scene_rows = collect_rows(document)
    derived_by_row = {derived_row_id(derived): derived for derived in document["derived_rows"]}
    known_rows = {row_id: row for row_id, (_, row) in scene_rows.items()}
    known_rows.update({row_id: {"items": derived["items"]} for row_id, derived in derived_by_row.items()})
    all_row_ids = [row["id"] for scene in document["scenes"] for row in scene["rows"]] + list(derived_by_row)
    problems = [f"행 ID {row_id} 겹침" for row_id in sorted({row_id for row_id in all_row_ids if all_row_ids.count(row_id) > 1})]
    defaults = {entry["name"]: float(entry["value"]) for entry in document["cvar_defaults"]}
    if FOG_SCATTERING in defaults:
        problems.append(f"cvar_defaults: {FOG_SCATTERING}는 cvars에 넣지 않는다(기본 1, 3.3)")
    if OVERRIDE_FPS in defaults and defaults[OVERRIDE_FPS] != 0:
        problems.append(f"cvar_defaults: {OVERRIDE_FPS}는 0(PR-2 U15)")
    problems.extend(f"영역 {area}에 장면이 없다" for area in AREAS if not any(scene["area"] == area for scene in document["scenes"]))
    problems.extend(find_item_rule_problems(document, known_rows, derived_by_row))
    problems.extend(find_row_rule_problems(document, items_by_id, derived_by_row))
    problems.extend(find_derived_rule_problems(document, scene_rows, items_by_id))
    for scene in document["scenes"]:
        problems.extend(find_scene_rule_problems(root, scene, defaults))
    return problems


def ledger_scene_cells(ledger_rows):
    cells = {}
    for record in ledger_rows:
        scene_id = record["행 ID"].split("/")[0]
        cells.setdefault(scene_id, record)
    return cells


def resolve_ledger_toggle_cell(record, records_by_id):
    cell = record["토글"]
    if not cell.startswith("="):
        return cell, None
    target = first_token(cell[1:])
    referenced = records_by_id.get(target)
    if referenced is None or referenced["토글"].startswith("="):
        return None, f"토글 '{cell}'이 가리키는 행이 없거나 다시 '='이다"
    if referenced["행 ID"].split("/")[0] != record["행 ID"].split("/")[0]:
        return None, f"토글 '{cell}'이 다른 장면 행을 가리킨다('=' = 같은 장면·단계)"
    return referenced["토글"], None


def find_ledger_item_problems(document, ledger):
    problems = []
    ledger_items = {record["ID"]: record for record in ledger["items"]}
    area_items = {item["id"]: item for item in document["items"]}
    only_ledger = sorted(set(ledger_items) - set(area_items))
    only_areas = sorted(set(area_items) - set(ledger_items))
    if only_ledger or only_areas:
        problems.append(f"S### 집합 다름: LG v2에만 {only_ledger}, p_areas에만 {only_areas}")
    for record in ledger["items"]:
        if not ITEM_ID_PATTERN.match(record["ID"]):
            problems.append(f"LG v2 1절 줄 {record['line']}: ID {record['ID']!r}는 S###")
    duplicates = sorted({identifier for identifier in ledger_items if [record["ID"] for record in ledger["items"]].count(identifier) > 1})
    problems.extend(f"LG v2 1절: ID {identifier} 겹침" for identifier in duplicates)
    for identifier in sorted(set(ledger_items) & set(area_items)):
        record, item = ledger_items[identifier], area_items[identifier]
        for header, key in (("출처(파일:줄)", "source"), ("요지", "summary"), ("한도", "limit"), ("영역", "area")):
            if normalize_text(item[key]) != record[header]:
                problems.append(f"LG v2 칸 불일치: {identifier} {key} p_areas '{item[key]}' ≠ LG '{record[header]}'")
        parsed = parse_disposition(record["처분"])
        if parsed["kind"] is None:
            problems.append(f"LG v2 1절 {identifier}: {parsed['problem']}")
            continue
        if item["disposition"] != parsed["kind"]:
            problems.append(f"LG v2 칸 불일치: {identifier} 처분 p_areas '{item['disposition']}' ≠ LG '{parsed['kind']}'")
        if item["rows"] != parsed["rows"]:
            problems.append(f"LG v2 칸 불일치: {identifier} rows p_areas {item['rows']} ≠ LG {parsed['rows']}")
        if parsed["reason"] is not None and normalize_text(item["reason"] or "") != parsed["reason"]:
            problems.append(f"LG v2 칸 불일치: {identifier} 사유 p_areas '{item['reason']}' ≠ LG '{parsed['reason']}'(PR-9)")
    return problems


def find_ledger_scene_row_problems(record, scene, row, records_by_id, first_records):
    where = f"LG v2 칸 불일치: {record['행 ID']}"
    problems = []
    if record["영역"] != scene["area"]:
        problems.append(f"{where} 영역 LG '{record['영역']}' ≠ p_areas '{scene['area']}'")
    if first_token(record["장면 ID"]) != scene["id"]:
        problems.append(f"{where} 장면 ID 첫 토큰 '{first_token(record['장면 ID'])}' ≠ '{scene['id']}'")
    if scene["level"] not in expand_ledger_maps(record["맵"]):
        problems.append(f"{where} 맵 칸 '{record['맵']}'에 level {scene['level']}이 없다")
    cell_labels = LABEL_PATTERN.findall(record["대상 라벨"])
    missing_labels = [label for label in scene["targets"] if label not in cell_labels]
    missing_labels += [label for side in ("a", "b") for step in scene["steps"][side] if step["action"] == "actor" for label in step["labels"] if label not in cell_labels]
    if missing_labels:
        problems.append(f"{where} 대상 라벨 칸에 {sorted(set(missing_labels))}이 없다")
    fluid_labels = [label for label in dict.fromkeys(cell_labels) if label in scene["targets"]]
    if fluid_labels != scene["targets"] and not missing_labels:
        problems.append(f"{where} targets 순서 {scene['targets']} ≠ 대상 라벨 칸 순서 {fluid_labels}(F7 a²+b²)")
    toggle_cell, toggle_problem = resolve_ledger_toggle_cell(record, records_by_id)
    if toggle_problem:
        problems.append(f"{where} {toggle_problem}")
    elif ledger_toggle_symbol(toggle_cell) != scene["toggle"]:
        problems.append(f"{where} 토글 첫 기호 LG '{ledger_toggle_symbol(toggle_cell)}' ≠ p_areas '{scene['toggle']}'(AM6 K4)")
    ledger_sides = {"—": "none"}.get(first_token(record["aw"]), first_token(record["aw"]))
    if ledger_sides != scene["awake_sides"]:
        problems.append(f"{where} aw LG '{record['aw']}' ≠ p_areas '{scene['awake_sides']}'")
    if first_token(record["지표"]) != row["metric"]:
        problems.append(f"{where} 지표 LG '{record['지표']}' ≠ p_areas '{row['metric']}'")
    converted, grid_problem = convert_ledger_grid(record["backend·grid"], fluid_labels)
    if grid_problem:
        problems.append(f"{where} backend·grid: {grid_problem}")
    else:
        if converted["backend"] != scene["backend"]:
            problems.append(f"{where} backend LG {converted['backend']} ≠ p_areas {scene['backend']}")
        if converted["grid"] != scene["grid"]:
            problems.append(f"{where} grid(F7 변환) LG {converted['grid']} ≠ p_areas {scene['grid']}")
        if converted["count"] is not None and scene["meta"]["count"] != converted["count"]:
            problems.append(f"{where} grid ×{converted['count']} ≠ meta.count {scene['meta']['count']}(F7)")
    number, formula, notes = split_limit_cell(record["한도 ms(조성식)"])
    if number is None or abs(number - row["limit_ms"]) > EPSILON:
        problems.append(f"{where} 한도 첫 숫자 LG {number} ≠ limit_ms {row['limit_ms']}")
    if formula != normalize_text(row["limit_formula"]):
        problems.append(f"{where} 조성식 LG '{formula}' ≠ limit_formula '{row['limit_formula']}'")
    for note in notes:
        if note not in normalize_text(row["note"] or ""):
            problems.append(f"{where} 한도 칸 표기 '({note})'가 row.note에 없다")
    ledger_shares = ledger_share_totals(record["parts_off"])
    area_shares = {}
    for part in row["parts_off"]:
        area_shares[part["item"]] = area_shares.get(part["item"], 0.0) + part["ms"]
    if set(ledger_shares) != set(area_shares) or any(abs(ledger_shares[key] - area_shares[key]) > EPSILON for key in ledger_shares):
        problems.append(f"{where} parts_off 출처별 몫 LG {ledger_shares} ≠ p_areas {area_shares}")
    ledger_items = [entry.strip() for entry in record["출처 줄 ID"].split("·") if entry.strip()]
    if ledger_items != row["items"]:
        problems.append(f"{where} 출처 줄 ID LG {ledger_items} ≠ items {row['items']}")
    problems.extend(find_ledger_evidence_marker_problems(where, record["증거"], scene))
    if first_records.get(scene["id"]) is record:
        if toggle_cell is not None and normalize_text(scene["toggle_summary"]) != toggle_cell:
            problems.append(f"{where} toggle_summary ≠ LG 토글 칸")
        if normalize_text(scene["backend_basis"]) != record["backend·grid"]:
            problems.append(f"{where} backend_basis ≠ LG backend·grid 칸")
    return problems


def find_ledger_evidence_marker_problems(where, cell, scene):
    problems = []
    markers = {"BC(": ("CPU", True), "BG(": ("GPU", True)}
    for marker, (backend, step_count) in markers.items():
        if marker in cell and (scene["backend"] != backend or scene["evidence"]["step_count"] != step_count):
            problems.append(f"{where} 증거 {marker}…) ↔ backend {scene['backend']}·step_count {scene['evidence']['step_count']}")
    for backend in BACKENDS:
        if f"B4 (ii) {backend}" in cell and scene["backend"] != backend:
            problems.append(f"{where} 증거 B4 (ii) {backend} ↔ backend {scene['backend']}")
    actor = scene["evidence"]["actor"] or {"hidden": False, "niagara": False}
    if re.search(r"\bTX\b(?!n)", cell) and not actor["hidden"]:
        problems.append(f"{where} 증거 TX ↔ evidence.actor.hidden 거짓")
    if "TXn" in cell and not actor["niagara"]:
        problems.append(f"{where} 증거 TXn ↔ evidence.actor.niagara 거짓")
    if re.search(r"\bEC\b", cell) and not scene["evidence"]["columns"]:
        problems.append(f"{where} 증거 EC ↔ evidence.columns 비어 있음")
    return problems


def find_ledger_derived_problems(record, derived, scene_rows):
    where = f"LG v2 칸 불일치: {record['행 ID']}(파생)"
    problems = []
    if record["영역"] != derived["area"]:
        problems.append(f"{where} 영역 LG '{record['영역']}' ≠ derived '{derived['area']}'")
    from_scene = derived["from"].split("/")[0]
    if from_scene not in record["장면 ID"] or derived["rule"] not in record["장면 ID"]:
        problems.append(f"{where} 장면 ID 칸 '{record['장면 ID']}'에 from 장면 {from_scene}·rule {derived['rule']}이 없다")
    ledger_items = [entry.strip() for entry in record["출처 줄 ID"].split("·") if entry.strip()]
    if ledger_items != derived["items"]:
        problems.append(f"{where} 출처 줄 ID LG {ledger_items} ≠ derived items {derived['items']}")
    from_row = scene_rows.get(derived["from"])
    if from_row is None:
        return problems
    number, _, _ = split_limit_cell(record["한도 ms(조성식)"])
    if number is None or abs(number - from_row[1]["limit_ms"]) > EPSILON:
        problems.append(f"{where} 한도 첫 숫자 LG {number} ≠ from {derived['from']} limit_ms {from_row[1]['limit_ms']}")
    if first_token(record["지표"]) != from_row[1]["metric"]:
        problems.append(f"{where} 지표 LG '{record['지표']}' ≠ from 지표 {from_row[1]['metric']}")
    return problems


def find_ledger_row_problems(document, ledger):
    problems = []
    scene_rows = collect_rows(document)
    derived_by_id = {derived["id"]: derived for derived in document["derived_rows"]}
    records_by_id = {}
    for record in ledger["rows"]:
        if record["행 ID"] in records_by_id:
            problems.append(f"LG v2 2절: 행 ID {record['행 ID']} 겹침")
        records_by_id[record["행 ID"]] = record
    derived_records = [record for record in ledger["rows"] if first_token(record["장면 ID"]) == "없음"]
    scene_records = [record for record in ledger["rows"] if first_token(record["장면 ID"]) != "없음"]
    first_records = ledger_scene_cells(scene_records)
    for record in scene_records:
        entry = scene_rows.get(record["행 ID"])
        if entry is None:
            problems.append(f"LG v2 2절 행 {record['행 ID']}이 p_areas에 없다")
            continue
        problems.extend(find_ledger_scene_row_problems(record, entry[0], entry[1], records_by_id, first_records))
    for record in derived_records:
        derived = derived_by_id.get(record["행 ID"].split("/")[0])
        if derived is None or derived_row_id(derived) != record["행 ID"]:
            problems.append(f"LG v2 2절 파생 행 {record['행 ID']}이 p_areas derived_rows에 없다(S12)")
            continue
        problems.extend(find_ledger_derived_problems(record, derived, scene_rows))
    ledger_ids = set(records_by_id)
    problems.extend(f"p_areas 행 {row_id}이 LG v2 2절에 없다" for row_id in scene_rows if row_id not in ledger_ids)
    problems.extend(f"p_areas 파생 행 {derived_row_id(derived)}이 LG v2 2절에 없다" for derived in document["derived_rows"] if derived_row_id(derived) not in ledger_ids)
    return problems


def ledger_evidence_columns(ledger):
    mapping = {}
    for record in ledger["evidence"]:
        if "EC" not in record["쓰임"].split():
            continue
        names = re.findall(r"`([^`]+)`", record["후보 이름 목록"])
        for scene_id in SCENE_TOKEN_PATTERN.findall(record["쓰임"]):
            mapping.setdefault(scene_id, [])
            mapping[scene_id].extend(name for name in names if name not in mapping[scene_id])
    return mapping


def find_ledger_table_problems(document, ledger):
    problems = []
    columns = ledger_evidence_columns(ledger)
    for scene in document["scenes"]:
        expected = columns.get(scene["id"], [])
        if sorted(scene["evidence"]["columns"]) != sorted(expected):
            problems.append(f"LG v2 칸 불일치: 장면 {scene['id']} evidence.columns {scene['evidence']['columns']} ≠ 5절 후보 {expected}")
    ledger_cvars = {}
    for record in ledger["cvars"]:
        name = record["CVar"].strip("`")
        match = FIRST_NUMBER_PATTERN.search(record["확정 기본값"])
        ledger_cvars[name] = float(match.group(0)) if match else None
    area_cvars = {entry["name"]: float(entry["value"]) for entry in document["cvar_defaults"]}
    if set(ledger_cvars) != set(area_cvars):
        problems.append(f"LG v2 칸 불일치: cvar_defaults 이름 {sorted(area_cvars)} ≠ 4절 {sorted(ledger_cvars)}")
    problems.extend(f"LG v2 칸 불일치: cvar_defaults {name} {area_cvars[name]:g} ≠ 4절 {ledger_cvars[name]}"
                    for name in sorted(set(ledger_cvars) & set(area_cvars)) if ledger_cvars[name] != area_cvars[name])
    record_only = sorted(name.split(".")[-1] for name in document["record_only_variables"])
    if record_only != sorted(ledger["record_only"]):
        problems.append(f"LG v2 칸 불일치: record_only_variables 끝 이름 {record_only} ≠ 4절 기록만 {sorted(ledger['record_only'])}")
    missing_counts = [int(value) for value in MISSING_COUNT_PATTERN.findall(ledger["section6"])]
    if "독립 대조" not in ledger["section6"] or not missing_counts:
        problems.append("LG v2 6절: '독립 대조'·'남은 누락 <수>' 기록이 없다(C10)")
    elif any(missing_counts):
        problems.append(f"LG v2 6절: 남은 누락 {max(missing_counts)} > 0(C10·L0 전 0)")
    return problems


def find_ledger_problems(document, ledger, ledger_relative, ledger_sha):
    problems = [problem for problem in ledger["problems"]]
    if not same_path_text(document["ledger"]["path"], ledger_relative):
        problems.append(f"p_areas.ledger.path {document['ledger']['path']} ≠ --ledger {ledger_relative}")
    if document["ledger"]["sha256"] != ledger_sha:
        problems.append(f"p_areas.ledger.sha256 {document['ledger']['sha256'][:12]}… ≠ LG v2 {ledger_sha[:12]}…")
    problems.extend(find_ledger_item_problems(document, ledger))
    problems.extend(find_ledger_row_problems(document, ledger))
    problems.extend(find_ledger_table_problems(document, ledger))
    return problems


def read_pie_profile_literal(root):
    path = Path(root) / PIE_PROFILE_PATH
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, SyntaxError) as error:
        return None, f"PP FX7P_DEFAULTS 읽기 실패: {type(error).__name__}: {error}"
    for node in tree.body:
        if not (isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == "FX7P_DEFAULTS" for target in node.targets)):
            continue
        try:
            value = ast.literal_eval(node.value)
        except ValueError as error:
            return None, f"PP FX7P_DEFAULTS가 리터럴이 아니다: {error}"
        if not isinstance(value, dict):
            return None, "PP FX7P_DEFAULTS가 사전이 아니다"
        return value, None
    return None, "PP에 FX7P_DEFAULTS가 없다"


def read_pie_profile_defaults(root):
    value, problem = read_pie_profile_literal(root)
    if problem:
        return None, problem
    cvars, recorded = value.get("cvars"), value.get("recorded_variables")
    if not (is_text_list(cvars) and len(cvars) == PIE_PROFILE_CVAR_COUNT and is_text_list(recorded) and len(recorded) == PIE_PROFILE_RECORDED_COUNT):
        return None, f"PP FX7P_DEFAULTS cvars {PIE_PROFILE_CVAR_COUNT}·recorded_variables {PIE_PROFILE_RECORDED_COUNT}이 아니다(PP:968-973)"
    return {"cvars": list(cvars), "recorded_variables": list(recorded)}, None


def compose_cvars(pie_cvars, cvar_defaults):
    commands = list(pie_cvars) + [f"{OVERRIDE_FPS} 0"]
    names = [command.split()[0] for command in commands]
    problems = []
    for entry in cvar_defaults:
        if entry["name"] in names:
            existing = commands[names.index(entry["name"])].split()
            if len(existing) != 2 or float(existing[1]) != float(entry["value"]):
                problems.append(f"cvar_defaults {entry['name']} {entry['value']} ≠ 계획 cvars '{' '.join(existing)}'")
            continue
        commands.append(f"{entry['name']} {format_cvar_value(entry['value'])}")
        names.append(entry["name"])
    return commands, problems


def compose_recorded(pie_recorded, cvar_defaults, record_only):
    names = list(pie_recorded) + [entry["name"] for entry in cvar_defaults] + [OVERRIDE_FPS, FOG_SCATTERING, OVERRIDE_PIE_SCREEN_PERCENTAGE] + list(record_only)
    return list(dict.fromkeys(names))


def normalize_step(step):
    return {"action": step["action"], **{key: step[key] for key in sorted(step) if key != "action"}}


def screen_percentage_step(screen_percentage):
    return {"action": "console", "commands": [f"{SCREEN_PERCENTAGE} {screen_percentage}"], "restore": True}


def plan_hash_paths(scenes):
    paths = [level_map_path(scene["level"]) for scene in scenes] + list(CANONICAL_FILES) + list(EDITOR_DLLS)
    paths += [path for scene in scenes for path in scene["hash_paths"]]
    return list(dict.fromkeys(paths))


def build_plan_scene(scene, screen_percentage, scene_id):
    entry = {"id": scene_id, "level": scene["level"], "targets": list(scene["targets"]),
             "setup": [dict(B5_STEP)] + [normalize_step(step) for step in scene["steps"]["setup"]] + [screen_percentage_step(screen_percentage)],
             "a": [normalize_step(step) for step in scene["steps"]["a"]], "b": [normalize_step(step) for step in scene["steps"]["b"]],
             "meta": {"screen_percentage": screen_percentage, "count": scene["meta"]["count"], "backend": scene["backend"], "grid": dict(scene["grid"]),
                      "toggle": scene["toggle"], "rows": [row["id"] for row in scene["rows"]]},
             "gpu_clock": scene["backend"] == "GPU", "required_columns": list(REQUIRED_COLUMNS), "metric": "FrameTime"}
    if scene["block_frames"] is not None:
        entry["block_frames"] = scene["block_frames"]
    return entry


def build_plan(context, area, stage, scene_entries):
    plan = {"run_label": f"p-{area}-{stage}", "cvars": list(context["cvars"]), "recorded_variables": list(context["recorded"]),
            "hash_paths": plan_hash_paths([scene for scene, _, _ in scene_entries]),
            "block_frames": 300, "discard_frames": 60, "order": "ABBA", "settle_seconds": 3.0, "initial_warmup_seconds": 90.0, "fresh_pie_after_gpu": True,
            **PLAN_STAGES[stage], "write_fx7p_evidence": False,
            "_p": {"generator": TOOL_PATH, "stage": stage, "area": area, "p_areas": context["areas_relative"], "p_areas_sha256": context["areas_sha"],
                   "ledger": context["ledger_relative"], "ledger_sha256": context["ledger_sha"]},
            "scenes": [build_plan_scene(scene, screen_percentage, scene_id) for scene, screen_percentage, scene_id in scene_entries]}
    return plan


def build_table_scene(scene):
    return {"backend": scene["backend"], "grid": dict(scene["grid"]), "target_labels": list(scene["targets"]), "awake_step_count": scene["awake_step_count"],
            "toggle": scene["toggle"], "awake_sides": scene["awake_sides"], "water_layer": scene["water_layer"],
            "regime": any(row["metric"] == "ft" for row in scene["rows"]),
            "evidence": {key: scene["evidence"][key] for key in EVIDENCE_KEYS}, "source": scene["level"]}


def build_table(area, scene_entries, with_rows):
    rows = []
    if with_rows:
        rows = [{"row": row["id"], "scene": scene["id"], "metric": row["metric"], "limit_ms": row["limit_ms"], "ledger_row": row["id"],
                 "source": "·".join(row["items"]), "basis": row["limit_formula"], "note": row["note"]} for scene, _, _ in scene_entries for row in scene["rows"]]
    return {"version": 1, "area": area, "title": f"P {area} 성능 판정 표", "water_path_check": False, "reference": None,
            "scenes": {scene_id: build_table_scene(scene) for scene, _, scene_id in scene_entries},
            "metrics": json.loads(json.dumps(TABLE_METRICS)), "rows": rows, "records": [], "consequences": dict(TABLE_CONSEQUENCES)}


def build_area_outputs(context, area):
    scenes = [scene for scene in context["document"]["scenes"] if scene["area"] == area]
    pilot_entries = [(scene, PILOT_SCREEN_PERCENTAGE, scene["id"]) for scene in scenes]
    outputs = {f"{area}/table.json": json_bytes(build_table(area, pilot_entries, True)),
               f"{area}/pilot1.json": json_bytes(build_plan(context, area, "pilot1", pilot_entries))}
    ft_scenes = [scene for scene in scenes if any(row["metric"] == "ft" for row in scene["rows"])]
    if not ft_scenes:
        return outputs
    ladder_entries = [(scene, percentage, f"{scene['id']}_sp{percentage}") for percentage in LADDER_SCREEN_PERCENTAGES for scene in ft_scenes]
    outputs[f"{area}/ladder.json"] = json_bytes(build_plan(context, area, "ladder", ladder_entries))
    outputs[f"{area}/ladder-table.json"] = json_bytes(build_table(area, ladder_entries, False))
    return outputs


def plan_area(plan_id):
    return next((area for prefix, area in PLAN_AREA_PREFIXES if plan_id.startswith(prefix)), None)


def base_plan_id(stem):
    changed = True
    while changed:
        changed = False
        for suffix in PLAN_VARIANT_SUFFIXES:
            if stem.endswith(suffix):
                stem, changed = stem[:-len(suffix)], True
    return stem


def adoption_order(key):
    return (0, int(key), "") if key.isdigit() else (1, 0, key)


def photo_count(directory):
    if not directory.is_dir():
        return 0
    return sum(1 for entry in directory.iterdir() if entry.is_file() and PHOTO_PATTERN.match(entry.name))


def first_photo_scene(root, plan_id):
    plan, error = read_json_file(Path(root) / CALIB_PLANS_DIR / f"{plan_id}.json")
    if error or not isinstance(plan, dict):
        return None, f"0e 계획 {plan_id} 읽기 실패: {error}"
    for scene in plan.get("scenes", []):
        name = scene.get("name") if isinstance(scene, dict) else None
        if isinstance(name, str) and photo_count(Path(root) / FRAMES_DIR / plan_id / name) >= 1:
            return name, None
    return None, None


def before_path(root, plan_id, scene_name):
    directory = Path(root) / FRAMES_DIR / plan_id / scene_name
    return f"{FRAMES_DIR}/{plan_id}/{scene_name}" if photo_count(directory) >= 1 else None


def read_judged_adoptions(root):
    try:
        expect_bytes = (Path(root) / EXPECT_PATH).read_bytes()
        expect = json.loads(expect_bytes.decode("utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        return None, None, f"sheet_areas: {EXPECT_PATH} 읽기 실패: {type(error).__name__}: {error}"
    judged_adoptions = expect.get("adoptions") if isinstance(expect, dict) else None
    if not isinstance(judged_adoptions, dict):
        return None, None, f"sheet_areas: {EXPECT_PATH} adoptions가 객체가 아니다"
    return judged_adoptions, sha256_bytes(expect_bytes), None


def compare_scene_sources(root, key, adoption, problems):
    sources = {}
    for evidence in adoption.get("evidence") or []:
        if not str(evidence).endswith("compare.json"):
            continue
        compare, error = read_json_file(Path(root) / evidence)
        if error or not isinstance(compare, dict):
            problems.append(f"sheet_areas: 채택 {key} 증거 {evidence} 읽기 실패: {error}")
            continue
        for check in compare.get("checks") or []:
            scene_name = check.get("scene") if isinstance(check, dict) else None
            if isinstance(scene_name, str):
                sources.setdefault(scene_name, evidence)
    return sources


def zero_e_scene_level(root, plan_id, scene_name):
    sweep, error = read_json_file(Path(root) / FRAMES_DIR / plan_id / "sweep.json")
    scenes = sweep.get("scenes") if not error and isinstance(sweep, dict) else None
    for scene in scenes if isinstance(scenes, list) else []:
        if isinstance(scene, dict) and scene.get("name") == scene_name and isinstance(scene.get("level"), str):
            return scene["level"]
    return None


def attach_adoption_scene(root, selected, plan_id, scene_name, key, evidence):
    existing = next((entry for entry in selected if entry["plan"] == plan_id and entry["scene"] == scene_name), None)
    if existing is not None:
        if key not in existing["adoptions"]:
            existing["adoptions"].append(key)
        return
    after_directory = Path(evidence).parent / scene_name
    after = after_directory.as_posix() if (Path(root) / after_directory).is_dir() else None
    selected.append({"plan": plan_id, "scene": scene_name, "source": "adoption", "adoptions": [key],
                     "before": before_path(root, plan_id, scene_name), "after": after})


def collect_adoption_scenes(root, verdict, judged_adoptions):
    problems, scenes_by_area, adopted_areas = [], {area: [] for area in SHEET_AREAS}, set()
    adoptions = verdict.get("adoptions") if isinstance(verdict, dict) else None
    if not isinstance(adoptions, dict):
        return scenes_by_area, adopted_areas, ["sheet_areas: verdict-w11 adoptions가 객체가 아니다"]
    for key in sorted(adoptions, key=adoption_order):
        adoption = adoptions[key]
        item_plan = ((adoption or {}).get("state_source") or {}).get("item_plan")
        if not isinstance(item_plan, str):
            problems.append(f"sheet_areas: 채택 {key}에 state_source.item_plan이 없다")
            continue
        plan_id = base_plan_id(Path(item_plan).stem)
        area = plan_area(plan_id)
        if area is None:
            problems.append(f"sheet_areas: 채택 {key} 계획 {plan_id}의 영역 접두어를 모른다")
            continue
        adopted_areas.add(area)
        judged = judged_adoptions.get(key) if isinstance(judged_adoptions.get(key), dict) else {}
        judged_scenes = [name for name in judged.get("scenes") or [] if isinstance(name, str)]
        judged_levels = [level for level in judged.get("levels") or [] if isinstance(level, str)]
        sources = compare_scene_sources(root, key, adoption, problems)
        if sources and not judged_scenes:
            problems.append(f"sheet_areas: 채택 {key}에 compare.json 근거가 있으나 {EXPECT_PATH} adoptions.{key}.scenes가 없다")
        problems.extend(f"sheet_areas: 채택 {key} 판정 장면 {name}이 compare.json checks에 없다" for name in judged_scenes if name not in sources)
        for scene_name, evidence in sources.items():
            if scene_name not in judged_scenes:
                continue
            level = zero_e_scene_level(root, plan_id, scene_name)
            if level not in judged_levels:
                problems.append(f"sheet_areas: 채택 {key} 장면 {plan_id}/{scene_name} 0e level {level!r} ∉ {EXPECT_PATH} adoptions.{key}.levels {judged_levels}")
                continue
            attach_adoption_scene(root, scenes_by_area[area], plan_id, scene_name, key, evidence)
    return scenes_by_area, adopted_areas, problems


def build_sheet_areas(root):
    verdict_path = Path(root) / VERDICT_PATH
    try:
        verdict_bytes = verdict_path.read_bytes()
        verdict = json.loads(verdict_bytes.decode("utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        return None, [f"sheet_areas: {VERDICT_PATH} 읽기 실패: {type(error).__name__}: {error}"]
    judged_adoptions, expect_sha, expect_problem = read_judged_adoptions(root)
    if expect_problem:
        return None, [expect_problem]
    scenes_by_area, adopted_areas, problems = collect_adoption_scenes(root, verdict, judged_adoptions)
    plans_directory = Path(root) / CALIB_PLANS_DIR
    plan_ids = sorted(path.stem for path in plans_directory.glob("*.json")) if plans_directory.is_dir() else []
    if not plan_ids:
        problems.append(f"sheet_areas: {CALIB_PLANS_DIR}에 계획이 없다")
    unknown = [plan_id for plan_id in plan_ids if plan_area(plan_id) is None]
    problems.extend(f"sheet_areas: 0e 계획 {plan_id}의 영역 접두어를 모른다" for plan_id in unknown)
    areas = {}
    for area in SHEET_AREAS:
        selected = scenes_by_area[area]
        cap = SHEET_MINIMUM_SCENES if area in adopted_areas else SHEET_CAP_WITHOUT_ADOPTION
        for plan_id in (plan_id for plan_id in plan_ids if plan_area(plan_id) == area):
            if len(selected) >= cap:
                break
            if not (Path(root) / FRAMES_DIR / plan_id).is_dir():
                continue
            scene_name, error = first_photo_scene(root, plan_id)
            if error:
                problems.append(f"sheet_areas: {error}")
                continue
            if scene_name is None or any(entry["plan"] == plan_id and entry["scene"] == scene_name for entry in selected):
                continue
            selected.append({"plan": plan_id, "scene": scene_name, "source": "fill", "adoptions": [], "before": before_path(root, plan_id, scene_name), "after": None})
        note = f"대표 {len(selected)}개(0e 짝 없음)" if len(selected) < SHEET_MINIMUM_SCENES else None
        areas[area] = {"has_adoption": area in adopted_areas, "cap": cap, "scenes": selected, "note": note}
    reshoot = list(dict.fromkeys(entry["plan"] for area in SHEET_AREAS for entry in areas[area]["scenes"] if entry["after"] is None))
    document = {"version": 1, "generator": TOOL_PATH,
                "inputs": {"verdict": {"path": VERDICT_PATH, "sha256": sha256_bytes(verdict_bytes)}, "expect": {"path": EXPECT_PATH, "sha256": expect_sha},
                           "plans_dir": CALIB_PLANS_DIR, "frames_dir": FRAMES_DIR},
                "rule": "6.2 ① W11 채택 판정 장면(번호 순, 채택별 장면 = expect.json adoptions[번호].scenes 중 0e sweep level ∈ levels·그 채택 compare.json checks에 있는 장면, T4 도출과 같은 규칙) ② 2개 미만이면 0e 계획 알파벳 순·계획당 첫 사진 장면, 채택 없는 영역은 4개까지 ④ reshoot = W11 짝 없는 장면의 계획",
                "areas_order": list(SHEET_AREAS), "areas": areas, "reshoot": reshoot}
    return document, problems


def load_build_context(root, ledger_text, areas_text):
    ledger_path, areas_path = resolve_path(root, ledger_text), resolve_path(root, areas_text)
    try:
        ledger_bytes = ledger_path.read_bytes()
        ledger = parse_ledger_text(ledger_bytes.decode("utf-8"))
    except (OSError, UnicodeDecodeError) as error:
        return None, [f"LG v2 읽기 실패: {type(error).__name__}: {error}"]
    try:
        areas_bytes = areas_path.read_bytes()
        document = json.loads(areas_bytes.decode("utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        return None, [f"p_areas 읽기 실패: {type(error).__name__}: {error}"]
    problems = find_areas_schema_problems(document)
    if problems:
        return None, problems
    defaults, pie_problem = read_pie_profile_defaults(root)
    if pie_problem:
        return None, [pie_problem]
    context = {"root": Path(root), "document": document, "ledger": ledger, "ledger_relative": relative_posix(root, ledger_path), "ledger_sha": sha256_bytes(ledger_bytes),
               "areas_relative": relative_posix(root, areas_path), "areas_sha": sha256_bytes(areas_bytes), "pie_defaults": defaults}
    context["cvars"], cvar_problems = compose_cvars(defaults["cvars"], document["cvar_defaults"])
    context["recorded"] = compose_recorded(defaults["recorded_variables"], document["cvar_defaults"], document["record_only_variables"])
    problems = cvar_problems + find_ledger_problems(document, ledger, context["ledger_relative"], context["ledger_sha"]) + find_rule_problems(root, document)
    return context, problems


def generate_outputs(context):
    outputs = {}
    for area in AREAS:
        outputs.update(build_area_outputs(context, area))
    sheet, problems = build_sheet_areas(context["root"])
    if sheet is not None:
        outputs[SHEET_FILE_NAME] = json_bytes(sheet)
    return outputs, problems


def existing_output_files(out_dir):
    out_dir = Path(out_dir)
    found = [out_dir / SHEET_FILE_NAME] if (out_dir / SHEET_FILE_NAME).is_file() else []
    if not out_dir.is_dir():
        return found
    for directory in sorted(entry for entry in out_dir.iterdir() if entry.is_dir()):
        found.extend(directory / name for name in OUTPUT_FILE_NAMES if (directory / name).is_file())
    return found


def find_byte_problems(out_dir, outputs):
    problems = []
    for relative, data in outputs.items():
        path = Path(out_dir) / relative
        if not path.is_file():
            problems.append(f"재생성 대조: {relative}이 없다")
        elif path.read_bytes() != data:
            problems.append(f"재생성 대조: {relative} 바이트가 다르다")
    for path in existing_output_files(out_dir):
        relative = path.relative_to(out_dir).as_posix()
        if relative not in outputs:
            problems.append(f"재생성 대조: 생성 밖 파일 {relative}(ft 없는 영역 사다리 등)")
    return problems


def find_plan_scene_problems(where, scene, stage):
    problems = [f"{where}: 허용 밖 장면 키 {key}(T1 ④)" for key in scene if key not in PLAN_SCENE_KEYS]
    setup = scene.get("setup") if isinstance(scene.get("setup"), list) else []
    meta = scene.get("meta") if isinstance(scene.get("meta"), dict) else {}
    percentage = meta.get("screen_percentage")
    if not setup or setup[0] != B5_STEP:
        problems.append(f"{where}: 첫 단계 B5 아님({B5_STEP})")
    last = setup[-1] if setup else {}
    is_sp_step = isinstance(last, dict) and last.get("action") == "console" and isinstance(last.get("commands"), list) and len(last["commands"]) == 1 \
        and str(last["commands"][0]).split()[0] == SCREEN_PERCENTAGE
    if not is_sp_step:
        problems.append(f"{where}: SP 단계 없음(setup 끝 r.ScreenPercentage console, C5)")
    elif last != screen_percentage_step(percentage):
        problems.append(f"{where}: SP ≠ meta(setup '{last['commands'][0]}' restore {last.get('restore')} 대 meta.screen_percentage {percentage})")
    allowed_percentages = (PILOT_SCREEN_PERCENTAGE,) if stage == "pilot1" else LADDER_SCREEN_PERCENTAGES
    if percentage not in allowed_percentages:
        problems.append(f"{where}: meta.screen_percentage {percentage}는 {stage}에서 {allowed_percentages}")
    for side in STEPS_KEYS:
        steps = scene.get(side) if isinstance(scene.get(side), list) else []
        for index, step in enumerate(steps):
            if not isinstance(step, dict):
                continue
            action = step.get("action")
            if action in FORBIDDEN_ACTIONS:
                problems.append(f"{where}.{side}[{index}]: 금지 단계 {action}(3.3)")
            if action == "visibility" and step.get("targets") == "others":
                problems.append(f"{where}.{side}[{index}]: visibility targets \"others\" 금지(B5)")
            if side == "setup" and action == "console" and step.get("restore") is not True:
                problems.append(f"{where}.{side}[{index}]: setup console restore 참 아님(E3)")
            if action in STEP_KEYS and set(step) - STEP_KEYS[action] - {"action"}:
                problems.append(f"{where}.{side}[{index}]: 허용 밖 키 {sorted(set(step) - STEP_KEYS[action] - {'action'})}(T1 ④)")
    return problems


def find_plan_problems(relative, plan, context):
    where = f"계획 {relative}"
    if not isinstance(plan, dict):
        return [f"{where}: 객체가 아니다"]
    stage = "ladder" if relative.endswith("ladder.json") else "pilot1"
    problems = [f"{where}: 허용 밖 최상위 키 {key}(T1 ④)" for key in plan if key not in PLAN_KEYS and not key.startswith("_")]
    if plan.get("write_fx7p_evidence") is not False:
        problems.append(f"{where}: write_fx7p_evidence 거짓 아님(1절)")
    if not isinstance(plan.get("_p"), dict):
        problems.append(f"{where}: _p 없음(T2 P 방식 판별)")
    cvars = plan.get("cvars") if isinstance(plan.get("cvars"), list) else []
    missing_pie = [command for command in context["pie_defaults"]["cvars"] if command not in cvars]
    if missing_pie:
        problems.append(f"{where}: FX7P 기본 cvars 빠짐 {missing_pie}(PP:968-969)")
    if f"{OVERRIDE_FPS} 0" not in cvars:
        problems.append(f"{where}: {OVERRIDE_FPS} 0 없음(PR-2 U15)")
    if any(str(command).split()[0] == FOG_SCATTERING for command in cvars):
        problems.append(f"{where}: {FOG_SCATTERING}는 cvars에 없다(3.3)")
    missing_defaults = [command for command in context["cvars"] if command not in cvars]
    if missing_defaults:
        problems.append(f"{where}: 만지는 CVar 기본값 빠짐 {missing_defaults}(A15)")
    recorded = plan.get("recorded_variables") if isinstance(plan.get("recorded_variables"), list) else []
    missing_recorded = [name for name in context["recorded"] if name not in recorded]
    if missing_recorded:
        problems.append(f"{where}: recorded_variables 빠짐 {missing_recorded}(3.3)")
    unrecorded_cvars = [str(command).split()[0] for command in cvars if str(command).split() and str(command).split()[0] not in recorded]
    if unrecorded_cvars:
        problems.append(f"{where}: cvars 이름이 recorded_variables에 없다 {unrecorded_cvars}(runs conditions 대조 불가, B14)")
    hash_paths = plan.get("hash_paths") if isinstance(plan.get("hash_paths"), list) else []
    scenes = plan.get("scenes") if isinstance(plan.get("scenes"), list) else []
    maps = [level_map_path(scene["level"]) for scene in scenes if isinstance(scene, dict) and isinstance(scene.get("level"), str) and scene["level"].startswith("/Game/")]
    missing_maps = [path for path in maps if path not in hash_paths]
    if missing_maps:
        problems.append(f"{where}: 맵 해시 빠짐 {missing_maps}(3.3 hash_paths)")
    missing_fixed = [path for path in list(CANONICAL_FILES) + list(EDITOR_DLLS) if path not in hash_paths]
    if missing_fixed:
        problems.append(f"{where}: 정본·DLL 해시 빠짐 {missing_fixed}(3.3)")
    if not scenes:
        problems.append(f"{where}: scenes가 없다")
    for index, scene in enumerate(scenes):
        if isinstance(scene, dict):
            problems.extend(find_plan_scene_problems(f"{where} 장면 {scene.get('id', index)}", scene, stage))
    return problems


def find_disk_plan_problems(out_dir, context):
    problems = []
    for path in existing_output_files(out_dir):
        if path.name not in ("pilot1.json", "ladder.json"):
            continue
        relative = path.relative_to(out_dir).as_posix()
        plan, error = read_json_file(path)
        if error:
            problems.append(f"계획 {relative}: JSON 파싱 실패 {error}")
            continue
        problems.extend(find_plan_problems(relative, plan, context))
    return problems


def run_build_check(root, ledger_text, areas_text, out_text):
    out_dir = resolve_path(root, out_text)
    context, problems = load_build_context(root, ledger_text, areas_text)
    if context is None:
        return None, None, problems
    outputs, sheet_problems = generate_outputs(context)
    problems = problems + sheet_problems
    problems += find_byte_problems(out_dir, outputs)
    problems += find_disk_plan_problems(out_dir, context)
    return context, outputs, problems


def build_command(arguments):
    root = Path(arguments.root)
    out_dir = resolve_path(root, arguments.out_dir)
    if arguments.check:
        _, _, problems = run_build_check(root, arguments.ledger, arguments.areas, arguments.out_dir)
        for problem in problems:
            print(problem)
        print_error(f"build --check: 문제 {len(problems)}개")
        return EXIT_INPUT if problems else EXIT_OK
    context, problems = load_build_context(root, arguments.ledger, arguments.areas)
    outputs = {}
    if context is not None:
        outputs, sheet_problems = generate_outputs(context)
        problems = problems + sheet_problems
        for relative, data in outputs.items():
            if relative.endswith(("pilot1.json", "ladder.json")):
                problems.extend(find_plan_problems(relative, json.loads(data.decode("utf-8")), context))
    if problems:
        for problem in problems:
            print(problem)
        print_error(f"build: 문제 {len(problems)}개, 쓰지 않음")
        return EXIT_INPUT
    for relative, data in outputs.items():
        write_atomic(out_dir / relative, data)
        print(f"썼음: {relative_posix(root, out_dir / relative)}")
    return EXIT_OK


class LockReadError(Exception):
    pass


def read_lock_document(path):
    path = Path(path)
    lock, error = read_json_file(path)
    if error or not isinstance(lock, dict) or not isinstance(lock.get("files"), dict):
        raise LockReadError(f"잠금 {path.name} 파싱 실패(잘린 JSON?): {error or 'files 없음'}")
    try:
        return lock, prereg_lock.parse_moment(str(lock.get("created_at")))
    except (TypeError, ValueError) as error:
        raise LockReadError(f"잠금 {path.name} created_at 형식 오류: {error}")


def read_lock_moment(path):
    return read_lock_document(path)[1]


def list_l0_locks(root):
    directory = Path(root) / LOCKS_DIR
    entries = []
    if not directory.is_dir():
        return entries
    for path in directory.iterdir():
        match = L0_LOCK_PATTERN.match(path.name)
        if match and path.is_file():
            entries.append((int(match.group(1) or 0), path, read_lock_moment(path)))
    return sorted(entries, key=lambda entry: entry[0])


def list_l1_locks(root, area):
    directory = Path(root) / LOCKS_DIR
    entries = []
    if not directory.is_dir():
        return entries
    for path in directory.iterdir():
        match = L1_LOCK_PATTERN.match(path.name)
        if match and path.is_file() and match.group(1) == area:
            entries.append((int(match.group(2)), path, read_lock_moment(path)))
    return sorted(entries, key=lambda entry: entry[0])


def current_l0_lock(root):
    locks = list_l0_locks(root)
    return locks[-1] if locks else None


def current_l1_lock(root, area, l0_moment):
    candidates = [entry for entry in list_l1_locks(root, area) if entry[2] >= l0_moment]
    return candidates[-1] if candidates else None


def next_l0_path(root):
    locks = list_l0_locks(root)
    name = "L0.json" if not locks else f"L0r{locks[-1][0] + 1}.json"
    return Path(root) / LOCKS_DIR / name


def next_l1_path(root, area):
    locks = list_l1_locks(root, area)
    number = locks[-1][0] + 1 if locks else 1
    return Path(root) / LOCKS_DIR / f"L1-{area}-{number}.json"


def resolve_lock_path(root, arguments, current_l0):
    if arguments.stage == "L0":
        if arguments.next:
            return next_l0_path(root)
        return current_l0[1] if current_l0 else None
    if current_l0 is None:
        return None
    if arguments.next:
        return next_l1_path(root, arguments.area)
    current_l1 = current_l1_lock(root, arguments.area, current_l0[2])
    return current_l1[1] if current_l1 else None


def current_lock_command(arguments):
    root = Path(arguments.root)
    if arguments.stage == "L1" and arguments.area not in AREAS:
        print_error(f"current-lock: L1에는 --area {AREAS} 중 하나가 필요하다")
        return EXIT_INPUT
    try:
        current_l0 = current_l0_lock(root)
        path = resolve_lock_path(root, arguments, current_l0)
    except LockReadError as error:
        print_error(f"current-lock: {error}")
        return EXIT_INPUT
    if path is None:
        print_error(f"current-lock: 현재 {arguments.stage} 잠금이 없다" + ("(L0 없음)" if arguments.stage == "L1" and current_l0 is None else ""))
        return EXIT_INPUT
    print(relative_posix(root, path))
    return EXIT_OK


def run_check_plan(root, plan_path):
    code, stdout, stderr = run_child([sys.executable, "-B", str(Path(root) / PIE_PROFILE_PATH), "--check-plan", relative_posix(root, plan_path)], root)
    forward_child_output(f"check-plan {relative_posix(root, plan_path)}", stdout, stderr)
    return code == 0


def find_listed_file_problems(root, relative):
    path = Path(root) / relative
    if not path.is_file():
        return [f"잠글 파일 없음: {relative}"]
    if path.stat().st_size <= 0:
        return [f"잠글 파일 크기 0: {relative}"]
    if path.suffix.lower() == ".json":
        _, error = read_json_file(path)
        if error:
            return [f"잠글 JSON 파싱 실패(잘린 JSON?): {relative}: {error}"]
    return []


def collect_l0_files(root, context, outputs, out_dir):
    files = list(L0_LEDGER_FILES) + [context["areas_relative"]]
    files += [relative_posix(root, Path(out_dir) / relative) for relative in [SHEET_FILE_NAME] + [key for key in outputs if key != SHEET_FILE_NAME]]
    files += list(L0_TOOL_FILES) + [path for path in L0_OPTIONAL_FIXTURE_FILES if (Path(root) / path).is_file()]
    files += list(EDITOR_DLLS) + list(CANONICAL_FILES)
    for relative, data in outputs.items():
        if relative.endswith(("pilot1.json", "ladder.json")):
            files += json.loads(data.decode("utf-8"))["hash_paths"]
    files += list(L0_HISTORY_FILES)
    return list(dict.fromkeys(files))


def lock_list_l0(root, areas_text):
    out_dir = Path(root) / T_DIR
    context, outputs, problems = run_build_check(root, LEDGER_PATH, areas_text, T_DIR)
    for problem in problems:
        print_error(f"lock-list L0: {problem}")
    if problems or context is None:
        print_error(f"lock-list L0: 재생성 대조·--check 문제 {len(problems)}개")
        return EXIT_INPUT
    plans = [out_dir / relative for relative in outputs if relative.endswith(("pilot1.json", "ladder.json"))]
    failed = [relative_posix(root, plan) for plan in plans if not run_check_plan(root, plan)]
    if failed:
        print_error(f"lock-list L0: check-plan 실패 {failed}")
        return EXIT_INPUT
    files = collect_l0_files(root, context, outputs, out_dir)
    file_problems = [problem for relative in files for problem in find_listed_file_problems(root, relative)]
    if file_problems:
        for problem in file_problems:
            print_error(f"lock-list L0: {problem}")
        return EXIT_INPUT
    for relative in files:
        print(relative)
    return EXIT_OK


def find_l1_consistency_problems(area_dir):
    problems = []
    pilot_runs, _ = read_json_file(area_dir / "pilot1-runs.json")
    ladder_need, _ = read_json_file(area_dir / "ladder-need.json")
    if isinstance(pilot_runs, dict) and pilot_runs.get("exit") == 0 and not (area_dir / "pilot1-judge.json").is_file():
        problems.append("pilot1-runs exit 0인데 pilot1-judge.json이 없다(2.6 시운전 분석 전)")
    if isinstance(ladder_need, dict) and ladder_need.get("needed") is True and not (area_dir / "ladder-runs.json").is_file():
        problems.append("ladder-need needed 참인데 ladder-runs.json이 없다(2.6 사다리 전)")
    ladder_runs, _ = read_json_file(area_dir / "ladder-runs.json") if (area_dir / "ladder-runs.json").is_file() else (None, None)
    if isinstance(ladder_runs, dict) and ladder_runs.get("exit") == 0 and not (area_dir / "ladder-judge.json").is_file():
        problems.append("ladder-runs exit 0인데 ladder-judge.json이 없다(2.6 사다리 분석 전)")
    return problems


def lock_list_l1(root, area, area_dir_text):
    if area not in AREAS:
        print_error(f"lock-list L1: --area {AREAS} 중 하나가 필요하다")
        return EXIT_INPUT
    if not area_dir_text:
        print_error("lock-list L1: --area-dir이 필요하다")
        return EXIT_INPUT
    try:
        current_l0 = current_l0_lock(root)
        current_l1 = current_l1_lock(root, area, current_l0[2]) if current_l0 else None
    except LockReadError as error:
        print_error(f"lock-list L1: {error}")
        return EXIT_INPUT
    if current_l0 is None:
        print_error("lock-list L1: 현재 L0 잠금이 없다")
        return EXIT_INPUT
    area_dir = resolve_path(root, area_dir_text)
    expected_dir = Path(root) / P_DIR / current_l0[1].stem / area
    if not same_path(area_dir, expected_dir):
        print_error(f"lock-list L1: --area-dir {relative_posix(root, area_dir)} ≠ 현재 L0 폴더 {relative_posix(root, expected_dir)}")
        return EXIT_INPUT
    if current_l1 is not None:
        print_error(f"lock-list L1: 현재 L1 잠금이 이미 있다({relative_posix(root, current_l1[1])})")
        return EXIT_LOCK_EXISTS
    names = list(L1_REQUIRED_FILES) + [name for name in L1_OPTIONAL_FILES if (area_dir / name).is_file()]
    files = [relative_posix(root, area_dir / name) for name in names]
    problems = [problem for relative in files for problem in find_listed_file_problems(root, relative)]
    problems += [] if problems else find_l1_consistency_problems(area_dir)
    if not problems and not run_check_plan(root, area_dir / "judge-plan.json"):
        problems.append("judge-plan.json check-plan 실패(S3)")
    if problems:
        for problem in problems:
            print_error(f"lock-list L1: {problem}")
        return EXIT_INPUT
    for relative in files:
        print(relative)
    return EXIT_OK


def lock_list_command(arguments):
    root = Path(arguments.root)
    if arguments.stage == "L0":
        if not arguments.areas:
            print_error("lock-list L0: --areas가 필요하다")
            return EXIT_INPUT
        return lock_list_l0(root, arguments.areas)
    return lock_list_l1(root, arguments.area, arguments.area_dir)


class SystemProbes:
    def count_editor_processes(self):
        try:
            completed = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {EDITOR_IMAGE_NAME}", "/FO", "CSV", "/NH"], capture_output=True, timeout=60)
        except (OSError, subprocess.SubprocessError) as error:
            return None, f"tasklist 실패: {error}"
        lines = completed.stdout.decode("utf-8", errors="replace").splitlines()
        count = sum(1 for line in lines if line.strip().lower().startswith(f'"{EDITOR_IMAGE_NAME.lower()}"'))
        return count, f"{EDITOR_IMAGE_NAME} {count}개"

    def query_gpu(self):
        try:
            completed = subprocess.run(NVIDIA_SMI_COMMAND, capture_output=True, timeout=60)
        except (OSError, subprocess.SubprocessError) as error:
            return False, f"nvidia-smi 실행 실패: {error}"
        text = completed.stdout.decode("utf-8", errors="replace").strip()
        return completed.returncode == 0 and bool(text), f"종료 {completed.returncode}: {text.splitlines()[0] if text else '출력 없음'}"

    def global_editor_ini(self):
        base = os.environ.get("LOCALAPPDATA")
        return Path(base) / GLOBAL_EDITOR_INI if base else None


def read_throttle_values(path):
    try:
        return THROTTLE_PATTERN.findall(Path(path).read_text(encoding="utf-8", errors="replace"))
    except OSError:
        return None


def check_canonical(root):
    mismatched = []
    for relative, expected in CANONICAL_FILES.items():
        path = Path(root) / relative
        actual = sha256_bytes(path.read_bytes()) if path.is_file() else None
        if actual != expected:
            mismatched.append(f"{relative} {str(actual)[:12]}… ≠ {expected[:12]}…")
    return not mismatched, "; ".join(mismatched) or "정본 2개 sha 일치(P0 ①)"


def check_accepted(root):
    path = Path(root) / ACCEPTED_OVERLAY_PATH
    if not path.is_file():
        return False, f"{ACCEPTED_OVERLAY_PATH} 없음"
    data = path.read_bytes()
    overlay, error = read_json_file(path)
    if error or not isinstance(overlay, dict):
        return False, f"파싱 실패: {error}"
    history = overlay.get("history") if isinstance(overlay.get("history"), list) else []
    last_operation = history[-1].get("op") if history and isinstance(history[-1], dict) else None
    is_empty = overlay.get("version") == 2 and overlay.get("assignments") == [] and overlay.get("actor_props") == []
    is_matching = sha256_bytes(data).startswith(ACCEPTED_OVERLAY_SHA_PREFIX)
    detail = f"version {overlay.get('version')}·assignments {len(overlay.get('assignments') or [])}·actor_props {len(overlay.get('actor_props') or [])}·history 끝 {last_operation}·sha {sha256_bytes(data)[:8]}"
    return is_empty and last_operation == ACCEPTED_LAST_OPERATION and is_matching, detail


def check_hold(root, holder):
    path = Path(root) / HOLD_PATH
    if not path.is_file():
        return [("hold_owner", False, f"{HOLD_PATH} 없음"), ("hold_age", False, "보류 없음")]
    hold, error = read_json_file(path)
    owner = hold.get("owner") if isinstance(hold, dict) else None
    age = time.time() - path.stat().st_mtime
    return [("hold_owner", owner == holder, f"owner {owner!r} 대 --holder {holder!r}" + (f"(파싱 실패 {error})" if error else "")),
            ("hold_age", age <= HOLD_EXPIRY_SECONDS, f"{age / 3600:.2f}시간(한도 3시간)")]


def check_build_gate(root):
    code, stdout, stderr = run_child([sys.executable, "-B", "-c", BUILD_GATE_CODE, str(Path(root) / "Tools" / "FluidFX")], root)
    lines = [line for line in stdout.splitlines() if line.strip()]
    if code != 0 or not lines:
        forward_child_output("build-gate", stdout, stderr)
        return False, f"check_fx7p_build_gate 실행 실패(종료 {code})"
    try:
        gate = json.loads(lines[-1])
    except ValueError as error:
        return False, f"check_fx7p_build_gate 출력 파싱 실패: {error}"
    return gate.get("passed") is True, json.dumps(gate, ensure_ascii=False)[:400]


def check_global_ini(probes):
    path = probes.global_editor_ini()
    if path is None:
        return False, "LOCALAPPDATA가 없어 전역 EditorSettings.ini를 찾을 수 없다"
    values = read_throttle_values(path) if path.is_file() else []
    if values is None:
        return False, f"{path.as_posix()} 읽기 실패"
    is_throttled = any(value.strip().lower() == "true" for value in values)
    return not is_throttled, f"{path.as_posix()} bThrottleCPUWhenNotForeground {values or '없음'}(True면 실패, A13)"


def record_project_ini(root):
    parts = []
    for relative in PROJECT_EDITOR_INIS:
        path = Path(root) / relative
        values = read_throttle_values(path) if path.is_file() else None
        parts.append(f"{relative}: {values if values else '없음'}")
    return True, "기록만: " + "; ".join(parts)


def check_current_l0(root, lock_path):
    try:
        current = current_l0_lock(root)
    except LockReadError as error:
        return False, str(error)
    if current is None:
        return False, "현재 L0 잠금이 없다"
    return same_path(current[1], lock_path), f"--lock {relative_posix(root, lock_path)} 대 현재 L0 {relative_posix(root, current[1])}"


def check_l0_verify(root, lock_path):
    code, stdout, stderr = run_child([sys.executable, "-B", str(Path(root) / PREREG_LOCK_PATH), "verify", "--lock", relative_posix(root, lock_path)], root)
    forward_child_output("prereg_lock verify", stdout, stderr)
    return code == 0, f"prereg_lock verify 종료 {code}"


def run_preflight(root, lock_text, holder, probes):
    root = Path(root)
    lock_path = resolve_path(root, lock_text)
    editor_count, editor_detail = probes.count_editor_processes()
    checks = [("canonical_sha", *check_canonical(root)), ("accepted_empty_v2", *check_accepted(root)), *check_hold(root, holder),
              ("build_gate", *check_build_gate(root)), ("nvidia_smi", *probes.query_gpu()), ("editor_single", editor_count == 1, editor_detail),
              ("a13_global_ini", *check_global_ini(probes)), ("project_ini_record", *record_project_ini(root)),
              ("l0_current", *check_current_l0(root, lock_path)), ("l0_verify", *check_l0_verify(root, lock_path))]
    entries = [{"name": name, "ok": bool(ok), "detail": detail} for name, ok, detail in checks]
    return {"lock": relative_posix(root, lock_path), "holder": holder, "checks": entries, "ok": all(entry["ok"] for entry in entries)}


def preflight_command(arguments, probes=None):
    root = Path(arguments.root)
    out_path = resolve_path(root, arguments.out)
    if out_path.exists():
        print_error(f"preflight: --out {relative_posix(root, out_path)}이 이미 있다(C1)")
        return EXIT_INPUT
    report = run_preflight(root, arguments.lock, arguments.holder, probes or SystemProbes())
    write_atomic(out_path, json_bytes(report))
    for entry in report["checks"]:
        print(f"{'통과' if entry['ok'] else '실패'} {entry['name']}: {entry['detail']}")
    print(f"preflight {'통과' if report['ok'] else '실패'}: {relative_posix(root, out_path)}")
    return EXIT_OK if report["ok"] else EXIT_INPUT


def canonical_sha(value):
    return sha256_bytes(json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))


def merge_pie_plan(defaults, plan):
    config = json.loads(json.dumps(defaults))
    for key, value in plan.items():
        if isinstance(value, dict) and isinstance(config.get(key), dict):
            config[key] = {**config[key], **value}
            continue
        config[key] = value
    return config


def format_moment(moment):
    return moment.isoformat(timespec="seconds")


def lock_has_file(lock, relative):
    return any(same_path_text(key, relative) for key in lock["files"])


def locked_file_sha(lock, relative):
    return next((sha for key, sha in lock["files"].items() if same_path_text(key, relative)), None)


def list_all_locks(root):
    directory = Path(root) / LOCKS_DIR
    if not directory.is_dir():
        return []
    entries = []
    for path in directory.iterdir():
        if path.is_file() and (L0_LOCK_PATTERN.match(path.name) or L1_LOCK_PATTERN.match(path.name)):
            lock, moment = read_lock_document(path)
            entries.append({"path": path, "lock": lock, "moment": moment})
    return sorted(entries, key=lambda entry: (entry["moment"], entry["path"].name))


def identify_lock_series(root, lock_path):
    if not same_path(lock_path.parent, Path(root) / LOCKS_DIR):
        return None
    if L0_LOCK_PATTERN.match(lock_path.name):
        return "L0", None
    match = L1_LOCK_PATTERN.match(lock_path.name)
    if match and match.group(1) in AREAS:
        return "L1", match.group(1)
    return None


def resolve_runs_locks(root, lock_path):
    series = identify_lock_series(root, lock_path)
    if series is None:
        return None, f"--lock {relative_posix(root, lock_path)}는 {LOCKS_DIR}의 L0·L0r<n>·L1-<영역>-<n> 잠금이 아니다"
    current_l0 = current_l0_lock(root)
    if current_l0 is None:
        return None, "현재 L0 잠금이 없다"
    current = current_l0 if series[0] == "L0" else current_l1_lock(root, series[1], current_l0[2])
    if current is None or not same_path(current[1], lock_path):
        current_text = relative_posix(root, current[1]) if current else "없음"
        return None, f"--lock {relative_posix(root, lock_path)}는 그 계열({series[0]}) 현재 잠금이 아니다(현재 {current_text})"
    lock, moment = read_lock_document(lock_path)
    l0_lock, _ = read_lock_document(current_l0[1])
    return {"stage": series[0], "area": series[1], "path": lock_path, "lock": lock, "moment": moment,
            "l0_path": current_l0[1], "l0_lock": l0_lock, "l0_moment": current_l0[2]}, None


def find_plan_cvar_expectations(merged):
    recorded = merged.get("recorded_variables") if isinstance(merged.get("recorded_variables"), list) else []
    expectations, problems = [], []
    for command in merged.get("cvars") or []:
        parts = str(command).split()
        if len(parts) != 2 or not NUMBER_TEXT_PATTERN.match(parts[1]):
            problems.append(f"계획 cvars '{command}'는 '이름 숫자'가 아니다(conditions 대조 불가)")
            continue
        if parts[0] not in recorded:
            problems.append(f"계획 cvars {parts[0]}가 recorded_variables에 없다(conditions 대조 불가, B14)")
            continue
        expectations.append((parts[0], float(parts[1])))
    return expectations, problems


def git_blob_hash(root, relative, cache):
    if relative in cache:
        return cache[relative]
    try:
        completed = subprocess.run(["git", "hash-object", relative], cwd=str(root), capture_output=True, timeout=300)
        value = completed.stdout.decode("utf-8", errors="replace").strip() if completed.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        value = None
    cache[relative] = value or None
    return cache[relative]


def read_run_session(run_dir):
    path = run_dir / RUN_SESSION_NAME
    if not path.is_file():
        return None, None, ""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
        session = json.loads(text)
    except OSError:
        return None, None, ""
    except ValueError:
        return None, None, text
    is_valid = isinstance(session, dict) and is_text(session.get("plan")) and is_text(session.get("started")) \
        and isinstance(session.get("asset_hashes"), dict) and isinstance(session.get("scenes"), list)
    if not is_valid:
        return None, None, text
    try:
        return session, prereg_lock.parse_moment(session["started"]), text
    except ValueError:
        return None, None, text


def read_folder_token_moment(run_dir):
    match = RUN_TOKEN_PATTERN.search(run_dir.name)
    if match is None:
        return None
    try:
        return datetime.strptime(match.group(1), RUN_TOKEN_FORMAT).astimezone()
    except ValueError:
        return None


def count_run_blocks(run_dir):
    directory = run_dir / RUN_CSV_DIRECTORY
    if not directory.is_dir():
        return 0
    return sum(1 for path in directory.glob("*.csv") if path.is_file() and not WARM_BLOCK_PATTERN.search(path.stem))


def read_run_plan_sha(run_dir):
    plan, error = read_json_file(run_dir / RUN_PLAN_NAME)
    return None if error else canonical_sha(plan)


def find_hold_failure(run_dir, session_text):
    texts = [session_text]
    for path in sorted(run_dir.iterdir()):
        if path.is_file() and path.suffix.lower() in RUN_LOG_SUFFIXES:
            try:
                texts.append(path.read_text(encoding="utf-8", errors="replace"))
            except OSError:
                continue
    for text in texts:
        for pattern in HOLD_FAILURE_PATTERNS:
            match = pattern.search(text)
            if match:
                return match.group(0)
    return None


def collect_run_candidates(root, plan_relative, merged_sha):
    directory = Path(root) / FX7P_RUNS_DIR
    if not directory.is_dir():
        return []
    candidates = []
    for run_dir in sorted(path for path in directory.iterdir() if path.is_dir()):
        session, started, text = read_run_session(run_dir)
        if session is not None:
            if same_path_text(relative_posix(root, resolve_path(root, session["plan"])), plan_relative):
                candidates.append({"run_dir": run_dir, "session": session, "started": started, "text": text})
            continue
        started = read_folder_token_moment(run_dir)
        if started is None or read_run_plan_sha(run_dir) != merged_sha or count_run_blocks(run_dir) < 1:
            continue
        candidates.append({"run_dir": run_dir, "session": None, "started": started, "text": text})
    return sorted(candidates, key=lambda candidate: (candidate["started"], candidate["run_dir"].name))


def find_asset_hash_mismatch(root, session, merged, cache):
    hashes = session["asset_hashes"]
    for relative in merged.get("hash_paths") or []:
        recorded, current = hashes.get(relative), git_blob_hash(root, relative, cache)
        if recorded is None or current is None or recorded != current:
            return f"hash_paths {relative}: 실행 {recorded} ≠ 현재 {current}"
    return None


def find_incomplete_reason(run_dir, session, scene_ids):
    if session.get("environment_error"):
        return f"session environment_error: {normalize_text(session['environment_error'])[:160]}"
    result, error = read_json_file(run_dir / RUN_RESULT_NAME)
    if error or not isinstance(result, dict):
        return f"result.json 없음·파싱 실패({error or '객체 아님'})"
    if result.get("environment_error"):
        return f"result environment_error: {normalize_text(result['environment_error'])[:160]}"
    scenes = result.get("scenes")
    present = set(scenes) if isinstance(scenes, dict) else {entry.get("id") for entry in scenes if isinstance(entry, dict)} if isinstance(scenes, list) else set()
    missing = [scene_id for scene_id in scene_ids if scene_id not in present]
    return f"result.json에 계획 장면 없음 {missing}" if missing else None


def find_conditions_mismatch(session, expectations, scene_ids):
    for record in session["scenes"]:
        if not isinstance(record, dict) or record.get("id") not in scene_ids:
            continue
        conditions = record.get("conditions") if isinstance(record.get("conditions"), dict) else {}
        variables = conditions.get("variables")
        if not isinstance(variables, dict):
            if record.get("blocks"):
                return f"{record['id']}: 블록이 있는데 setup 전 conditions 기록이 없다"
            continue
        for name, expected in expectations:
            value = variables.get(name)
            if not is_number(value) or abs(float(value) - expected) > CONDITION_TOLERANCE:
                return f"{record['id']}: {name} {value!r} ≠ 계획 {expected:g}(setup 전 conditions, B14)"
    return None


def find_a13_block(session):
    for record in session["scenes"]:
        blocks = record.get("blocks") if isinstance(record, dict) and isinstance(record.get("blocks"), list) else []
        for block in blocks:
            stats = block.get("stats") if isinstance(block, dict) and isinstance(block.get("stats"), dict) else {}
            frame = stats.get("FrameTime") if isinstance(stats.get("FrameTime"), dict) else {}
            gpu = stats.get("GPUTime") if isinstance(stats.get("GPUTime"), dict) else None
            if is_number(frame.get("p50")) and frame["p50"] >= A13_FRAME_P50_MS:
                return f"{block.get('block_id')}: FrameTime p50 {frame['p50']} ≥ {A13_FRAME_P50_MS:g} ms"
            if gpu is not None and is_number(gpu.get("max")) and gpu["max"] <= 0:
                return f"{block.get('block_id')}: GPUTime 열 전부 0"
    return None


def classify_session_run(root, candidate, context, cache):
    run_dir, session = candidate["run_dir"], candidate["session"]
    if read_run_plan_sha(run_dir) != context["merged_sha"]:
        return "계획 다름", "실행 plan.json 정렬 sha ≠ merge(잠긴 계획)(④)", None
    mismatch = find_asset_hash_mismatch(root, session, context["merged"], cache)
    if mismatch:
        return None, mismatch, EXIT_RELOCK
    hold = find_hold_failure(run_dir, candidate["text"] + "\n" + json.dumps(session, ensure_ascii=False))
    if hold:
        return "보류", f"보류 실패 문구 '{hold}'(E7, 셈 없음)", None
    blocks = count_run_blocks(run_dir)
    if blocks < 1:
        return "블록 0", "CSV 블록 0(셈 없음)", None
    incomplete = find_incomplete_reason(run_dir, session, context["scene_ids"])
    if incomplete:
        return "미완", incomplete, None
    mismatch = find_conditions_mismatch(session, context["expectations"], context["scene_ids"])
    if mismatch:
        return "조건 불일치", mismatch, None
    a13 = find_a13_block(session)
    if a13:
        return "A13", a13, None
    return "선택", f"유효 완료(블록 {blocks})", None


def classify_folder_run(candidate):
    hold = find_hold_failure(candidate["run_dir"], candidate["text"])
    if hold:
        return "보류", f"session 없음·파싱 안 됨, 보류 실패 문구 '{hold}'(셈 없음)"
    return "미완", f"session 없음·파싱 안 됨, plan.json sha 같음·블록 {count_run_blocks(candidate['run_dir'])}(F5)"


def decide_runs(root, context):
    attempts, counted, entries, cache = 0, 0, [], {}
    for candidate in context["candidates"]:
        entry = {"run_dir": relative_posix(root, candidate["run_dir"]), "started": format_moment(candidate["started"]), "class": None, "detail": ""}
        if candidate["session"] is not None and candidate["started"] < context["first_moment"]:
            entry.update({"class": "첫 잠금 전", "detail": f"시작 < 첫 잠금 {format_moment(context['first_moment'])}(②)"})
            entries.append(entry)
            return EXIT_TERMINAL, f"② 첫 잠금 전에 시작한 실행 {entry['run_dir']}(종착, PR-10)", None, attempts, counted, entries
        if candidate["started"] < context["moment"]:
            entry.update({"class": "이전 잠금", "detail": f"시작 < 현재 잠금 {format_moment(context['moment'])}(③)"})
            entries.append(entry)
            continue
        if candidate["session"] is None:
            run_class, detail = classify_folder_run(candidate)
            stop = None
        else:
            run_class, detail, stop = classify_session_run(root, candidate, context, cache)
        if stop == EXIT_RELOCK:
            entry.update({"class": "hash_paths 다름", "detail": detail})
            entries.append(entry)
            return EXIT_RELOCK, f"⑤ {detail}: 재잠금 PR-12 (나)", None, attempts, counted, entries
        entry.update({"class": run_class, "detail": detail})
        entries.append(entry)
        if run_class in ("미완", "조건 불일치", "A13", "선택"):
            attempts += 1
        if run_class in ("미완", "조건 불일치"):
            counted += 1
        if run_class == "선택":
            session, folder_token = candidate["session"], RUN_TOKEN_PATTERN.search(candidate["run_dir"].name)
            token = session.get("run_token") if is_text(session.get("run_token")) else folder_token.group(1) if folder_token else None
            selected = {"run_dir": entry["run_dir"], "run_token": token, "started": entry["started"]}
            return EXIT_OK, f"가장 이른 유효 완료(시도 {attempts})", selected, attempts, counted, entries
        if counted >= COUNTED_FAILURE_LIMIT:
            return EXIT_RUNS_FINAL, UNMEASURABLE_RUN, None, attempts, counted, entries
        if attempts >= ATTEMPT_LIMIT:
            return EXIT_RUNS_FINAL, UNMEASURABLE_ATTEMPTS, None, attempts, counted, entries
    return EXIT_MEASURE_NEEDED, f"측정 필요(유효 완료 없음, 시도 {attempts}·셈 있는 실패 {counted})", None, attempts, counted, entries


def prepare_runs_context(root, arguments):
    plan_path = resolve_path(root, arguments.plan)
    plan_relative = relative_posix(root, plan_path)
    plan, error = read_json_file(plan_path)
    if error or not isinstance(plan, dict) or not isinstance(plan.get("scenes"), list):
        return None, f"--plan {plan_relative} 읽기 실패: {error or 'scenes 없음'}"
    locks, problem = resolve_runs_locks(root, resolve_path(root, arguments.lock))
    if problem:
        return None, problem
    if not lock_has_file(locks["lock"], plan_relative):
        return None, f"--plan {plan_relative}이 --lock {relative_posix(root, locks['path'])} files에 없다"
    defaults, problem = read_pie_profile_literal(root)
    if problem:
        return None, problem
    merged = merge_pie_plan(defaults, plan)
    expectations, problems = find_plan_cvar_expectations(merged)
    if problems:
        return None, "계획 결함: " + "; ".join(problems)
    first = next((entry for entry in list_all_locks(root) if lock_has_file(entry["lock"], plan_relative)), None)
    if first is None:
        return None, f"{plan_relative}을 files에 가진 잠금이 없다"
    merged_sha = canonical_sha(merged)
    scene_ids = [scene.get("id") for scene in merged["scenes"] if isinstance(scene, dict)]
    return {**locks, "plan_path": plan_path, "plan_relative": plan_relative, "merged": merged, "merged_sha": merged_sha, "expectations": expectations,
            "scene_ids": scene_ids, "first_path": first["path"], "first_lock": first["lock"], "first_moment": first["moment"],
            "candidates": collect_run_candidates(root, plan_relative, merged_sha)}, None


def verify_runs_locks(root, context):
    paths = [context["path"]] + ([context["l0_path"]] if context["stage"] == "L1" else [])
    failed = [relative_posix(root, path) for path in paths if not check_l0_verify(root, path)[0]]
    return failed


def runs_command(arguments):
    root = Path(arguments.root)
    out_path = resolve_path(root, arguments.out)
    if out_path.exists():
        print_error(f"runs: --out {relative_posix(root, out_path)}이 이미 있다(C1)")
        return EXIT_INPUT
    try:
        context, problem = prepare_runs_context(root, arguments)
    except LockReadError as error:
        context, problem = None, str(error)
    if problem:
        print_error(f"runs: {problem}")
        return EXIT_INPUT
    failed = verify_runs_locks(root, context)
    if failed:
        print(f"runs 종료 {EXIT_RELOCK}: 잠금 verify 실패 {failed}(C3, 재잠금 PR-12 (나))")
        return EXIT_RELOCK
    code, reason, selected, attempts, counted, entries = decide_runs(root, context)
    for entry in entries:
        print(f"{entry['class']} {entry['run_dir']} ({entry['started']}): {entry['detail']}")
    print(f"runs 종료 {code}: {reason}" + (f" → {selected['run_dir']}" if selected else ""))
    if code not in (EXIT_OK, EXIT_RUNS_FINAL):
        return code
    report = {"plan": context["plan_relative"], "lock": relative_posix(root, context["path"]), "lock_created_at": context["lock"].get("created_at"),
              "first_lock": {"path": relative_posix(root, context["first_path"]), "created_at": context["first_lock"].get("created_at")},
              "l0_lock": {"path": relative_posix(root, context["l0_path"]), "created_at": context["l0_lock"].get("created_at")},
              "exit": code, "reason": reason, "selected": selected, "attempts": attempts, "counted_failures": counted, "runs": entries}
    write_atomic(out_path, json_bytes(report))
    print(f"썼음: {relative_posix(root, out_path)}")
    return code


def read_optional_json(path):
    if not Path(path).is_file():
        return None, None
    value, error = read_json_file(path)
    return (value, None) if not error and isinstance(value, dict) else (None, error or "객체가 아니다")


def file_sha_or_none(path):
    return sha256_bytes(Path(path).read_bytes()) if Path(path).is_file() else None


def judge_scene_entries(document):
    scenes = document.get("scenes") if isinstance(document, dict) else None
    if isinstance(scenes, dict):
        return {str(key): value for key, value in scenes.items() if isinstance(value, dict)}, None
    if not isinstance(scenes, list):
        return None, "scenes가 객체·목록이 아니다"
    entries = {}
    for entry in scenes:
        identifier = (entry.get("scene") or entry.get("id")) if isinstance(entry, dict) else None
        if not is_text(identifier):
            return None, "scenes 항목에 scene·id가 없다"
        entries[identifier] = entry
    return entries, None


def find_header_defect_scenes(entries):
    return sorted(scene_id for scene_id, entry in entries.items() if entry.get("header_defects"))


def count_pairs(entry):
    pairs = entry.get("pairs")
    if isinstance(pairs, list):
        return len(pairs)
    return pairs if is_positive_integer(pairs) else 0


def read_regime(entry, where):
    regime = entry.get("regime")
    if regime is None:
        return None, None
    if not isinstance(regime, dict) or not isinstance(regime.get("strong"), bool) or not isinstance(regime.get("basic"), bool):
        return None, f"{where}: regime에 strong·basic 불리언이 없다"
    return regime, None


def load_judge_scenes(path, label):
    document, error = read_optional_json(path)
    if document is None:
        return None, f"{label} 읽기 실패: {error or '없음'}"
    entries, problem = judge_scene_entries(document)
    if problem:
        return None, f"{label}: {problem}"
    defects = find_header_defect_scenes(entries)
    if defects:
        return None, f"{label}: 핵심 열 header_defects 있음 {defects}(E8·S2, 멈춤·보고)"
    return entries, None


def read_runs_exit(path, label):
    document, error = read_optional_json(path)
    if document is None:
        return None, f"{label} 읽기 실패: {error or '없음'}"
    if document.get("exit") not in (EXIT_OK, EXIT_RUNS_FINAL):
        return None, f"{label} exit {document.get('exit')!r}는 0·4가 아니다"
    return document["exit"], None


def find_strong_failures(entries):
    failures, problems = [], []
    for scene_id, entry in sorted(entries.items()):
        regime, problem = read_regime(entry, scene_id)
        if problem:
            problems.append(problem)
        elif regime is not None and not regime["strong"]:
            failures.append(scene_id)
    return failures, problems


def check_area_arguments(root, arguments):
    if arguments.area not in AREAS:
        return None, None, f"--area는 {AREAS} 중 하나"
    area_dir = resolve_path(root, arguments.area_dir)
    if not area_dir.is_dir():
        return None, None, f"--area-dir {relative_posix(root, area_dir)} 폴더가 없다"
    out_path = resolve_path(root, arguments.out)
    if out_path.exists():
        return None, None, f"--out {relative_posix(root, out_path)}이 이미 있다(C1)"
    return area_dir, out_path, None


def decide_ladder_need(root, area_dir):
    pilot_exit, problem = read_runs_exit(area_dir / PILOT_RUNS_NAME, PILOT_RUNS_NAME)
    if problem:
        return None, problem
    judge_path = area_dir / PILOT_JUDGE_NAME
    if not judge_path.is_file():
        if pilot_exit == EXIT_OK:
            return None, f"{PILOT_RUNS_NAME} exit 0인데 {PILOT_JUDGE_NAME}이 없다(시운전 분석 전)"
        return {"needed": False, "reason": f"{PILOT_JUDGE_NAME} 없음({PILOT_RUNS_NAME} exit 4, 시운전 판정 없음)", "strong_fail": [], "pilot_judge": None}, None
    entries, problem = load_judge_scenes(judge_path, PILOT_JUDGE_NAME)
    if problem:
        return None, problem
    failures, problems = find_strong_failures(entries)
    if problems:
        return None, "; ".join(problems)
    reason = f"SP100 강 미달 ft 장면 {failures}(PR-5)" if failures else "SP100 강 미달 ft 장면 없음(regime null 장면 제외)"
    return {"needed": bool(failures), "reason": reason, "strong_fail": failures,
            "pilot_judge": {"path": relative_posix(root, judge_path), "sha256": file_sha_or_none(judge_path)}}, None


def need_ladder_command(arguments):
    root = Path(arguments.root)
    area_dir, out_path, problem = check_area_arguments(root, arguments)
    decision = None
    if not problem:
        decision, problem = decide_ladder_need(root, area_dir)
    if problem:
        print_error(f"need-ladder: {problem}")
        return EXIT_INPUT
    write_atomic(out_path, json_bytes({"area": arguments.area, **decision}))
    print(f"need-ladder {arguments.area}: needed {str(decision['needed']).lower()} · {decision['reason']}")
    return EXIT_OK


def ladder_scene_status(entries, scene_id, percentage):
    ladder_id = f"{scene_id}_sp{percentage}"
    entry = entries.get(ladder_id)
    if entry is None or entry.get("error") or count_pairs(entry) < PILOT_PAIR_COUNT:
        return (False, False), None
    regime, problem = read_regime(entry, ladder_id)
    if problem:
        return None, problem
    return ((regime["strong"], regime["basic"]) if regime else (False, False)), None


def choose_ladder_percentage(scene_id, pilot_regime, ladder_entries):
    statuses = {}
    for percentage in LADDER_SCREEN_PERCENTAGES:
        statuses[percentage], problem = ladder_scene_status(ladder_entries, scene_id, percentage)
        if problem:
            return None, problem
    strong = [percentage for percentage in LADDER_SCREEN_PERCENTAGES if statuses[percentage][0]]
    if strong:
        return {"sp": strong[0], "basis": f"SP100 강 미달 → 강을 넘는 가장 낮은 SP{strong[0]}(PR-5)", "preset": None}, None
    basic = ([PILOT_SCREEN_PERCENTAGE] if pilot_regime["basic"] else []) + [percentage for percentage in LADDER_SCREEN_PERCENTAGES if statuses[percentage][1]]
    if basic:
        return {"sp": basic[0], "basis": f"강 통과 SP 없음 → 기본을 넘는 가장 낮은 SP{basic[0]}(PR-5)", "preset": None}, None
    return {"sp": PILOT_SCREEN_PERCENTAGE, "basis": "SP100·130·160·200 모두 기본 미달(사다리 장면 오류·쌍 부족 포함, S11)", "preset": REGIME_PRESET_UNDECIDABLE}, None


def decide_scene_percentage(scene_id, is_ft, pilot_entries, ladder):
    if not is_ft:
        return {"sp": PILOT_SCREEN_PERCENTAGE, "basis": "ft 행 없음(체제 관문 없음, PR-5)", "preset": None}, None
    if pilot_entries is None:
        return {"sp": PILOT_SCREEN_PERCENTAGE, "basis": "시운전 측정 불가(실행) → SP100(PR-5)", "preset": None}, None
    entry = pilot_entries.get(scene_id)
    if entry is None or entry.get("error"):
        return {"sp": PILOT_SCREEN_PERCENTAGE, "basis": "시운전 장면 오류·기록 없음 → SP100(PR-5)", "preset": None}, None
    if count_pairs(entry) < PILOT_PAIR_COUNT:
        return {"sp": PILOT_SCREEN_PERCENTAGE, "basis": f"시운전 쌍 부족({count_pairs(entry)} < {PILOT_PAIR_COUNT}) → SP100(PR-5)", "preset": None}, None
    regime, problem = read_regime(entry, scene_id)
    if problem:
        return None, problem
    if regime is None:
        return {"sp": PILOT_SCREEN_PERCENTAGE, "basis": "시운전 regime 없음(측정 불가) → SP100(PR-5)", "preset": None}, None
    if regime["strong"]:
        return {"sp": PILOT_SCREEN_PERCENTAGE, "basis": "SP100 강 통과(PR-5)", "preset": None}, None
    if ladder["exit"] == EXIT_RUNS_FINAL:
        return {"sp": PILOT_SCREEN_PERCENTAGE, "basis": "SP100 강 미달·사다리 측정 불가(실행) → ft 행만 측정 불가(C6)", "preset": UNMEASURABLE_RUN}, None
    return choose_ladder_percentage(scene_id, regime, ladder["entries"])


def read_ladder_inputs(area_dir, needs_ladder):
    if not needs_ladder:
        return {"exit": None, "entries": {}}, None
    ladder_exit, problem = read_runs_exit(area_dir / LADDER_RUNS_NAME, LADDER_RUNS_NAME)
    if problem:
        return None, f"SP100 강 미달 장면이 있는데 {problem}"
    if ladder_exit == EXIT_RUNS_FINAL:
        return {"exit": ladder_exit, "entries": {}}, None
    entries, problem = load_judge_scenes(area_dir / LADDER_JUDGE_NAME, LADDER_JUDGE_NAME)
    if problem:
        return None, problem
    return {"exit": ladder_exit, "entries": entries}, None


def build_judge_plan(area, pilot_plan, choices):
    plan = json.loads(json.dumps(pilot_plan))
    plan["run_label"] = f"p-{area}-judge"
    plan["repeats"], plan["unstable_repeats"] = JUDGE_REPEATS, JUDGE_REPEATS
    if isinstance(plan.get("_p"), dict):
        plan["_p"]["stage"] = "judge"
    for scene in plan["scenes"]:
        percentage = choices[scene["id"]]["sp"]
        if percentage == PILOT_SCREEN_PERCENTAGE:
            continue
        scene["setup"][-1] = screen_percentage_step(percentage)
        scene["meta"]["screen_percentage"] = percentage
    return plan


def decide_sp_choices(root, area, area_dir):
    pilot_path = Path(root) / T_DIR / area / "pilot1.json"
    pilot_plan, error = read_optional_json(pilot_path)
    if pilot_plan is None or not isinstance(pilot_plan.get("scenes"), list):
        return None, None, f"{relative_posix(root, pilot_path)} 읽기 실패: {error or '없음'}"
    problems = [problem for scene in pilot_plan["scenes"] for problem in find_plan_scene_problems(f"시운전 계획 장면 {scene.get('id')}", scene, "pilot1")]
    if problems:
        return None, None, "; ".join(problems)
    pilot_exit, problem = read_runs_exit(area_dir / PILOT_RUNS_NAME, PILOT_RUNS_NAME)
    if problem:
        return None, None, problem
    pilot_entries = None
    if pilot_exit == EXIT_OK:
        pilot_entries, problem = load_judge_scenes(area_dir / PILOT_JUDGE_NAME, PILOT_JUDGE_NAME)
        if problem:
            return None, None, problem
    failures, problems = find_strong_failures(pilot_entries or {})
    if problems:
        return None, None, "; ".join(problems)
    ladder, problem = read_ladder_inputs(area_dir, bool(failures))
    if problem:
        return None, None, problem
    choices = {}
    for scene in pilot_plan["scenes"]:
        is_ft = any(str(row).endswith("/ft") for row in scene["meta"].get("rows", []))
        choice, problem = decide_scene_percentage(scene["id"], is_ft, pilot_entries, ladder)
        if problem:
            return None, None, problem
        choices[scene["id"]] = choice
    names = [PILOT_RUNS_NAME, PILOT_JUDGE_NAME, LADDER_NEED_NAME, LADDER_RUNS_NAME, LADDER_JUDGE_NAME]
    inputs = {relative_posix(root, pilot_path): file_sha_or_none(pilot_path)}
    inputs.update({relative_posix(root, area_dir / name): file_sha_or_none(area_dir / name) for name in names})
    return {"area": area, "inputs": inputs, "scenes": choices}, build_judge_plan(area, pilot_plan, choices), None


def select_sp_command(arguments):
    root = Path(arguments.root)
    area_dir, out_path, problem = check_area_arguments(root, arguments)
    choice = judge_plan = None
    if not problem:
        choice, judge_plan, problem = decide_sp_choices(root, arguments.area, area_dir)
    if problem:
        print_error(f"select-sp: {problem}")
        return EXIT_INPUT
    plan_path = resolve_path(root, arguments.out_plan)
    write_atomic(plan_path, json_bytes(judge_plan))
    write_atomic(out_path, json_bytes(choice))
    for scene_id, entry in choice["scenes"].items():
        print(f"{scene_id}: SP{entry['sp']} · {entry['basis']}" + (f" · {entry['preset']}" if entry["preset"] else ""))
    print(f"썼음: {relative_posix(root, plan_path)} · {relative_posix(root, out_path)}")
    return EXIT_OK


def list_l0_folders(p_dir):
    folders = [path for path in Path(p_dir).iterdir() if path.is_dir() and L0_LOCK_PATTERN.match(f"{path.name}.json")]
    return sorted(folders, key=lambda path: int(L0_LOCK_PATTERN.match(f"{path.name}.json").group(1) or 0))


def read_area_completion(area_dir):
    runs, _ = read_optional_json(area_dir / JUDGE_RUNS_NAME)
    if runs is None:
        return False, None, None
    if runs.get("exit") == EXIT_RUNS_FINAL:
        return True, runs, None
    selected = runs.get("selected") if isinstance(runs.get("selected"), dict) else {}
    token = selected.get("run_token")
    if runs.get("exit") != EXIT_OK or not is_text(token):
        return False, runs, None
    judge, _ = read_optional_json(area_dir / JUDGE_NAME)
    if judge is not None and judge.get("run_token") == token:
        return True, runs, judge
    return False, runs, judge


def find_done_folders(p_dir, area):
    return [folder for folder in list_l0_folders(p_dir) if read_area_completion(folder / area)[0]]


def status_command(arguments):
    root = Path(arguments.root)
    if arguments.area not in AREAS:
        print_error(f"status: --area는 {AREAS} 중 하나")
        return EXIT_INPUT
    p_dir = resolve_path(root, arguments.p_dir)
    if not p_dir.is_dir():
        print_error(f"status: --p-dir {relative_posix(root, p_dir)} 폴더가 없다")
        return EXIT_INPUT
    done = find_done_folders(p_dir, arguments.area)
    if len(done) > 1:
        print_error(f"status: {arguments.area} 끝난 폴더가 둘 이상 {[folder.name for folder in done]}(SS2·C4)")
        return EXIT_INPUT
    print("done" if done else "todo")
    return EXIT_OK


class SummaryInputError(Exception):
    pass


def verdict_category(label):
    if label in FINAL_VERDICTS:
        return label
    return next((category for category in OPEN_VERDICTS if label.startswith(category)), None)


def judge_row_label(judge_row, row_id):
    verdict, reason = judge_row.get("verdict"), judge_row.get("reason")
    if verdict in FINAL_VERDICTS:
        return verdict
    if verdict not in OPEN_VERDICTS:
        raise SummaryInputError(f"judge.json 행 {row_id} verdict {verdict!r}는 {FINAL_VERDICTS + OPEN_VERDICTS} 중 하나가 아니다")
    if not is_text(reason):
        return verdict
    return reason if reason.startswith(verdict) else f"{verdict}({reason})"


def judge_row_entries(document):
    rows = document.get("rows")
    if isinstance(rows, dict):
        return {str(key): value for key, value in rows.items() if isinstance(value, dict)}
    if isinstance(rows, list):
        return {entry["row"]: entry for entry in rows if isinstance(entry, dict) and is_text(entry.get("row"))}
    raise SummaryInputError("judge.json rows가 객체·목록이 아니다")


def read_sp_presets(area_dir, area):
    choice, error = read_optional_json(area_dir / SP_CHOICE_NAME)
    if choice is None:
        raise SummaryInputError(f"{area}: {SP_CHOICE_NAME} 읽기 실패: {error or '없음'}")
    scenes = choice.get("scenes") if isinstance(choice.get("scenes"), dict) else None
    if scenes is None:
        raise SummaryInputError(f"{area}: {SP_CHOICE_NAME} scenes가 객체가 아니다")
    presets = {}
    for scene_id, entry in scenes.items():
        preset = entry.get("preset") if isinstance(entry, dict) else None
        if preset is not None and verdict_category(str(preset)) not in OPEN_VERDICTS:
            raise SummaryInputError(f"{area}: {SP_CHOICE_NAME} {scene_id} preset {preset!r}는 판정 불가·측정 불가가 아니다")
        if preset is not None:
            presets[scene_id] = preset
    return presets, scenes


def read_area_inputs(root, p_dir, areas_path, area, folder):
    area_dir = folder / area
    _, runs, judge = read_area_completion(area_dir)
    lock_path = Path(p_dir) / "locks" / f"{folder.name}.json"
    try:
        lock, _ = read_lock_document(lock_path)
    except LockReadError as error:
        raise SummaryInputError(f"{area}: 폴더 {folder.name}의 L0 잠금: {error}")
    table_relative = relative_posix(root, Path(areas_path).parent / area / "table.json")
    locked_sha = locked_file_sha(lock, table_relative)
    if locked_sha is None:
        raise SummaryInputError(f"{area}: L0 잠금 {lock_path.name}에 표 {table_relative}가 없다")
    presets, choices = read_sp_presets(area_dir, area)
    inputs = {"area": area, "folder": relative_posix(root, folder), "area_dir": area_dir, "lock_path": relative_posix(root, lock_path), "table": table_relative,
              "locked_sha": locked_sha, "runs": runs, "runs_exit": runs.get("exit"), "presets": presets, "choices": choices, "judge": judge,
              "judge_rows": {}, "judged_table": None, "table_matches": True}
    if inputs["runs_exit"] == EXIT_RUNS_FINAL:
        reason = runs.get("reason")
        if not (is_text(reason) and reason.startswith("측정 불가")):
            raise SummaryInputError(f"{area}: {JUDGE_RUNS_NAME} exit 4 reason {reason!r}가 측정 불가 사유가 아니다")
        return inputs
    table = judge.get("budget_table") if isinstance(judge.get("budget_table"), dict) else {}
    inputs["judged_table"] = {"path": table.get("path"), "sha256": table.get("sha256"), "area": table.get("area")}
    inputs["table_matches"] = table.get("sha256") == locked_sha and is_text(table.get("path")) and same_path_text(relative_posix(root, resolve_path(root, table["path"])), table_relative) \
        and table.get("area") == area
    inputs["judge_rows"] = judge_row_entries(judge)
    return inputs


def read_post_hoc_defects(root, p_dir, known_rows):
    directory = Path(p_dir) / DEFECTS_DIRECTORY
    mapping, listing = {}, []
    if not directory.is_dir():
        return mapping, listing
    files = sorted((path for path in directory.iterdir() if path.is_file() and DEFECT_FILE_PATTERN.match(path.name)),
                   key=lambda path: int(DEFECT_FILE_PATTERN.match(path.name).group(1)))
    for path in files:
        name = path.stem
        rows = [row for match in POST_HOC_LINE_PATTERN.finditer(path.read_text(encoding="utf-8", errors="replace")) for row in ROW_ID_PATTERN.findall(match.group(1))]
        unknown = [row for row in rows if row not in known_rows]
        if unknown:
            raise SummaryInputError(f"결함 {path.name} '사후 결함 행'에 없는 행 {unknown}")
        if not rows:
            continue
        listing.append({"defect": name, "path": relative_posix(root, path), "rows": list(dict.fromkeys(rows))})
        for row in rows:
            mapping.setdefault(row, name)
    return mapping, listing


def finalize_row(base, label, rule):
    category = verdict_category(label)
    if category is None:
        raise SummaryInputError(f"행 {base['row']} 판정 '{label}'의 범주를 모른다")
    return {**base, "verdict": category, "label": label, "rule": rule}


def copy_judge_fields(base, judge_row):
    for key in ("interval", "d", "d_prime", "sp", "viewport", "rounds", "pairs", "p95_record", "backend", "rt_control"):
        base[key] = judge_row.get(key)
    base["judge_verdict"], base["judge_reason"] = judge_row.get("verdict"), judge_row.get("reason")
    return base


def decide_scene_row(inputs, scene, row, post_hoc):
    base = {"row": row["id"], "scene": scene["id"], "metric": row["metric"], "limit_ms": row["limit_ms"], "items": list(row["items"]), "count": scene["meta"]["count"],
            "sp_choice": (inputs["choices"].get(scene["id"]) or {}).get("sp"), "interval": None, "d": None, "d_prime": None, "sp": None, "viewport": None,
            "rounds": None, "pairs": None, "p95_record": None, "backend": None, "rt_control": None, "judge_verdict": None, "judge_reason": None}
    if inputs["runs_exit"] == EXIT_RUNS_FINAL:
        return finalize_row(base, inputs["runs"]["reason"], f"{JUDGE_RUNS_NAME} exit 4(S11 ①)")
    judge_row = inputs["judge_rows"].get(row["id"])
    if judge_row is None:
        raise SummaryInputError(f"{inputs['area']}: judge.json에 행 {row['id']}이 없다")
    copy_judge_fields(base, judge_row)
    preset = inputs["presets"].get(scene["id"])
    if preset and row["metric"] == "ft":
        return finalize_row(base, preset, f"{SP_CHOICE_NAME} preset(S11 ②)")
    if not inputs["table_matches"]:
        return finalize_row(base, TABLE_MISMATCH, "C3 표 sha(S11 ③)")
    if row["id"] in post_hoc:
        return finalize_row(base, f"판정 불가(사후 결함 {post_hoc[row['id']]})", "사후 결함(PR-12)")
    return finalize_row(base, judge_row_label(judge_row, row["id"]), "judge.json")


def decide_derived_row(inputs, derived, row_results, post_hoc):
    row_id = derived_row_id(derived)
    source = row_results[derived["from"]]
    base = {"row": row_id, "scene": None, "metric": derived["from"].split("/")[1], "limit_ms": source["limit_ms"], "items": list(derived["items"]), "count": None,
            "sp_choice": None, "from": derived["from"]}
    if derived["rule"] != DERIVED_RULE:
        raise SummaryInputError(f"파생 행 {row_id} rule {derived['rule']!r}는 {DERIVED_RULE}만 안다")
    if inputs["runs_exit"] == EXIT_RUNS_FINAL:
        return finalize_row(base, inputs["runs"]["reason"], f"{JUDGE_RUNS_NAME} exit 4(S11 ①)")
    if not inputs["table_matches"]:
        return finalize_row(base, TABLE_MISMATCH, "C3 표 sha(S11 ③)")
    if row_id in post_hoc:
        return finalize_row(base, f"판정 불가(사후 결함 {post_hoc[row_id]})", "사후 결함(PR-12)")
    if source["verdict"] == "통과":
        return finalize_row(base, "통과", f"FO3 {derived['from']} 통과(B17)")
    return finalize_row(base, f"판정 불가(HVOL 대안 측정 불가; {derived['from']} {source['label']})", "FO3(B17)")


def area_ledger_lines(document, area):
    return [item for item in document["items"] if item["area"] in (area, ALL_AREAS_MARK)]


def classify_ledger_line(item, row_results):
    if item["disposition"] == "기록":
        return None
    if item["disposition"] in ("출하 밖", "측정 불가"):
        return item["disposition"]
    verdicts = [row_results[row]["verdict"] for row in item["rows"]]
    if all(verdict == "통과" for verdict in verdicts):
        return "통과"
    return next(category for category in ("미달", "판정 불가", "측정 불가") if category in verdicts)


def format_overall_counts(counts, row_lines, include_failures):
    parts = [f"통과 {counts['통과']}"] + ([f"미달 {counts['미달']}"] if include_failures else []) + [f"행 {row_lines}", f"판정 불가 {counts['판정 불가']}",
                                                                                                  f"측정 불가 {counts['측정 불가']}", f"출하 밖 {counts['출하 밖']}"]
    return " / ".join(parts)


def summarize_area_lines(document, area, row_results, area_row_ids):
    counts, row_lines, entries = {key: 0 for key in SUMMARY_COUNT_KEYS}, 0, []
    for item in area_ledger_lines(document, area):
        line_class = classify_ledger_line(item, row_results)
        entries.append({"id": item["id"], "area": item["area"], "source": item["source"], "summary": item["summary"], "limit": item["limit"], "disposition": item["disposition"],
                        "reason": item["reason"], "rows": list(item["rows"]), "class": line_class or "기록(분모 밖)"})
        if line_class is None:
            continue
        counts[line_class] += 1
        row_lines += item["disposition"] in ROW_DISPOSITIONS
    has_failure = counts["미달"] > 0 or any(row_results[row]["verdict"] == "미달" for row in area_row_ids)
    all_rows_pass = all(row_results[row]["verdict"] == "통과" for row in area_row_ids)
    if has_failure:
        overall = f"미충족({format_overall_counts(counts, row_lines, True)})"
    elif row_lines >= 1 and counts["통과"] == row_lines and counts["판정 불가"] == counts["측정 불가"] == 0 and all_rows_pass:
        overall = PROPOSED_PASS
    else:
        overall = f"부분({format_overall_counts(counts, row_lines, False)})"
    return overall, counts, row_lines, entries


def collect_column_defects(rows):
    names, core_rows = [], []
    for row in rows:
        reason = str(row.get("judge_reason") or "")
        for bare, enclosed in COLUMN_DEFECT_PATTERN.findall(reason):
            names.extend(name.strip() for name in (bare or enclosed).split(", ") if name.strip() and CORE_COLUMN_DEFECT_TEXT not in name)
        if CORE_COLUMN_DEFECT_TEXT in reason:
            core_rows.append(row["row"])
    return {"evidence_names": sorted(set(names)), "core_rows": core_rows}


def read_adoption_counts(root):
    verdict, error = read_optional_json(Path(root) / VERDICT_PATH)
    if verdict is None or not isinstance(verdict.get("adoptions"), dict):
        raise SummaryInputError(f"PB2:214 열 빠짐: {VERDICT_PATH} 읽기 실패·adoptions 없음({error or '없음'})")
    counts = {area: [] for area in SHEET_AREAS}
    for key in sorted(verdict["adoptions"], key=adoption_order):
        item_plan = ((verdict["adoptions"][key] or {}).get("state_source") or {}).get("item_plan")
        area = plan_area(base_plan_id(Path(item_plan).stem)) if is_text(item_plan) else None
        if area is None:
            raise SummaryInputError(f"PB2:214 열 빠짐: {VERDICT_PATH} 채택 {key} 계획 {item_plan!r}의 영역을 모른다")
        counts[area].append(key)
    return counts, file_sha_or_none(Path(root) / VERDICT_PATH)


def judge_plan_segment(judge_path):
    parts = judge_path.replace("\\", "/").split("/")
    return parts[parts.index("runs") + 1] if "runs" in parts[:-1] else None


def read_k_judges(root):
    expect, error = read_optional_json(Path(root) / EXPECT_PATH)
    if expect is None or not isinstance(expect.get("rows"), list):
        raise SummaryInputError(f"K 판정 경로: {EXPECT_PATH} 읽기 실패·rows 없음({error or '없음'})")
    by_area = {area: [] for area in SHEET_AREAS}
    for index, row in enumerate(expect["rows"]):
        judge = ((row or {}).get("source") or {}).get("judge") if isinstance(row, dict) else None
        segment = judge_plan_segment(judge) if is_text(judge) else None
        area = plan_area(base_plan_id(segment)) if segment else None
        if area is None:
            raise SummaryInputError(f"K 판정 경로: {EXPECT_PATH} rows[{index}].source.judge {judge!r}의 영역을 모른다")
        if judge not in by_area[area]:
            by_area[area].append(judge)
    return {area: sorted(paths) for area, paths in by_area.items()}, file_sha_or_none(Path(root) / EXPECT_PATH)


def count_band_basis(root, judge_paths):
    counts = {key: 0 for key in BASIS_COUNT_KEYS}
    for plan_id in sorted({judge_plan_segment(path) for path in judge_paths}):
        plan, _ = read_optional_json(Path(root) / CALIB_PLANS_DIR / f"{plan_id}.json")
        if plan is None:
            counts["계획 없음"] += 1
            continue
        metrics = plan.get("metrics") if isinstance(plan.get("metrics"), dict) else {}
        for target in metrics.get("targets") or []:
            basis = target.get("basis") if isinstance(target, dict) else None
            counts[BASIS_LABELS.get(basis, "자유 문안" if is_text(basis) else "근거 미표기")] += 1
    return counts


def read_approvals(root, approvals_path):
    if not approvals_path.is_file():
        return None, None
    approvals, error = read_optional_json(approvals_path)
    if approvals is None or not isinstance(approvals.get("areas"), dict):
        raise SummaryInputError(f"--approvals {relative_posix(root, approvals_path)} 형식 오류(areas 객체 필요): {error}")
    return approvals["areas"], file_sha_or_none(approvals_path)


def decide_approval(root, approvals, approvals_path, area):
    current_path = approvals_path.parent / area / "current.json"
    current, _ = read_optional_json(current_path)
    evidence = relative_posix(root, current_path)
    if approvals is None:
        return {"state": APPROVAL_PENDING, "date": None, "evidence": evidence, "note": "승인 파일 없음(F8)"}
    entry = approvals.get(area)
    if entry is None:
        return {"state": APPROVAL_PENDING, "date": None, "evidence": evidence, "note": "승인 기록 없음"}
    if not (isinstance(entry, dict) and entry.get("decision") in APPROVAL_DECISIONS and is_text(entry.get("date")) and is_text(entry.get("sheet_json_sha256"))):
        raise SummaryInputError(f"--approvals {area}: decision(승인|거부)·date·sheet_json_sha256이 필요하다")
    current_sha = current.get("sheet_json_sha256") if current else None
    if current_sha != entry["sheet_json_sha256"]:
        return {"state": APPROVAL_PENDING, "date": None, "evidence": evidence, "note": f"승인 sha {entry['sheet_json_sha256'][:8]} ≠ 최신 판 sha {str(current_sha)[:8]}(A19)"}
    return {"state": f"{entry['decision']} {entry['date']}", "date": entry["date"], "evidence": entry.get("sheet_json") or evidence, "note": "최신 판 sha 일치(A19)"}


def area_section7_labels(area):
    return [f"{label}({note})" if note else label for label, areas, note in SECTION7_LABELS if area in areas]


def build_pb2_columns(area, adoptions, overall, approval):
    columns = {"영역": area, "채택 수": len(adoptions)}
    columns.update({name: None for name in PB2_COLUMNS_NOT_IN_VERDICT})
    columns.update({"성능": overall, "승인 상태": approval["state"], "날짜": approval["date"], "근거 경로": approval["evidence"],
                    "표기": PROPOSED_PASS_LABEL if overall == PROPOSED_PASS else overall})
    return columns


def collect_pb2_fxp_sources(root):
    return sorted({relative_posix(root, path) for pattern in PB2_FXP_SOURCE_GLOBS for path in Path(root).glob(pattern) if path.is_file()})


def build_area_summary(root, area, document, inputs, row_results, extra):
    scene_rows = [row["id"] for scene in document["scenes"] if scene["area"] == area for row in scene["rows"]]
    derived_rows = [derived_row_id(derived) for derived in document["derived_rows"] if derived["area"] == area]
    area_rows = scene_rows + derived_rows
    overall, counts, row_lines, lines = summarize_area_lines(document, area, row_results, area_rows)
    rows = [row_results[row] for row in area_rows]
    approval = decide_approval(root, extra["approvals"], extra["approvals_path"], area)
    entry = {"area": area, "folder": inputs["folder"] if inputs else None, "l0_lock": inputs["lock_path"] if inputs else None,
             "l1_lock": (inputs["runs"] or {}).get("lock") if inputs else None,
             "judge_runs": {"exit": inputs["runs_exit"], "reason": inputs["runs"].get("reason"), "selected": inputs["runs"].get("selected")} if inputs else None,
             "overall": overall, "counts": counts, "row_lines": row_lines, "blocking_unmeasurable": counts["측정 불가"],
             "table_sha256": inputs["locked_sha"] if inputs else None,
             "table_check": {"locked": inputs["locked_sha"], "judged": inputs["judged_table"], "matches": inputs["table_matches"]} if inputs else None,
             "rows": rows, "lines": lines, "records": [{"id": item["id"], **item["record"]} for item in area_ledger_lines(document, area) if item["record"]],
             "column_defects": collect_column_defects(rows), "post_hoc_defects": [defect for defect in extra["defects"] if set(defect["rows"]) & set(area_rows)],
             "approval": approval, "labels": area_section7_labels(area), "k_judges": extra["k_judges"][area], "band_basis": count_band_basis(root, extra["k_judges"][area])}
    entry["pb2_214"] = build_pb2_columns(area, extra["adoptions"][area], overall, approval)
    return entry


def collect_fxp_items(document, areas, defects):
    unmeasurable = [{"area": area, "id": line["id"], "reason": line["reason"]} for area in SHEET_AREAS for line in areas[area]["lines"] if line["class"] == "측정 불가"
                    and line["disposition"] == "측정 불가"]
    unmeasurable += [{"area": area, "row": row["row"], "reason": row["label"]} for area in SHEET_AREAS for row in areas[area]["rows"] if row["verdict"] == "측정 불가"]
    opposite = [{"id": item["id"], "area": item["area"], "reason": item["reason"]} for item in document["items"]
                if item["disposition"] == "출하 밖" and any(text in str(item["reason"]) for text in OPPOSITE_BACKEND_REASONS)]
    unassigned = [{"id": item["id"], "area": item["area"], "disposition": item["disposition"], "reason": item["reason"]} for item in document["items"]
                  if item["area"] not in SHEET_AREAS + (ALL_AREAS_MARK,)]
    return {"fixed": [dict(item) for item in FXP_FIXED_ITEMS], "post_hoc_defects": defects, "unmeasurable": unmeasurable, "opposite_backend": opposite, "unassigned_items": unassigned}


def build_summary(root, p_dir, areas_path, document, approvals_path, folders):
    known_rows = set(collect_rows(document)) | {derived_row_id(derived) for derived in document["derived_rows"]}
    post_hoc, defects = read_post_hoc_defects(root, p_dir, known_rows)
    area_inputs = {area: read_area_inputs(root, p_dir, areas_path, area, folders[area]) for area in AREAS}
    row_results = {}
    for scene in document["scenes"]:
        for row in scene["rows"]:
            row_results[row["id"]] = decide_scene_row(area_inputs[scene["area"]], scene, row, post_hoc)
    for derived in document["derived_rows"]:
        row_results[derived_row_id(derived)] = decide_derived_row(area_inputs[derived["area"]], derived, row_results, post_hoc)
    adoptions, verdict_sha = read_adoption_counts(root)
    k_judges, expect_sha = read_k_judges(root)
    approvals, approvals_sha = read_approvals(root, approvals_path)
    extra = {"approvals": approvals, "approvals_path": approvals_path, "defects": defects, "k_judges": k_judges, "adoptions": adoptions}
    areas = {area: build_area_summary(root, area, document, area_inputs.get(area), row_results, extra) for area in SHEET_AREAS}
    return {"version": 1, "generator": TOOL_PATH,
            "inputs": {"p_areas": {"path": relative_posix(root, areas_path), "sha256": file_sha_or_none(areas_path)},
                       "verdict": {"path": VERDICT_PATH, "sha256": verdict_sha}, "expect": {"path": EXPECT_PATH, "sha256": expect_sha},
                       "approvals": {"path": relative_posix(root, approvals_path), "sha256": approvals_sha}},
            "rule": "행 판정 순서(앞이 이김, S11): judge-runs 4 → sp-choice preset(ft 행) → C3 표 sha → 사후 결함 → judge.json·FO3, 영역 종합 A10·B18",
            "areas_order": list(SHEET_AREAS), "areas": areas,
            "pb2_214": [areas[area]["pb2_214"] for area in SHEET_AREAS],
            "pb2_214_note": f"{'·'.join(PB2_COLUMNS_NOT_IN_VERDICT)}는 {VERDICT_PATH}에 키가 없어 null(FX-P가 K 일지로 채움)",
            "pb2_fxp_sources": collect_pb2_fxp_sources(root),
            "fxp_items": collect_fxp_items(document, areas, defects),
            "pending_judgements": {"source": "PB1:223·PB2:230", "items": list(PENDING_JUDGEMENTS), "rule": "해당 계획 결과나 승인 시트로 닫고 남은 것만 넘긴다(P에서 닫지 않음)"},
            "approval_rule": "승인 대기 / 승인 <날짜> / 거부 <날짜>, 승인·거부는 승인 sha = 최신 판 current.json sheet_json_sha256일 때만(A19)"}


def format_cell(value):
    if value is None:
        return "—"
    if isinstance(value, str):
        return value.replace("|", "/")
    return json.dumps(value, ensure_ascii=False).replace("|", "/")


def render_summary_markdown(summary):
    lines = ["# P 요약(영역 성능 판정·승인 상태)", "", f"- 입력 p_areas {format_cell(summary['inputs']['p_areas']['sha256'])[:12]} · 승인 파일 {format_cell(summary['inputs']['approvals']['sha256'])[:12]}",
             f"- {summary['rule']}", "- 표기: PF 환산 ×2.3~3·상한 비교(증명 아님), '동등 확인' 없음(A14)", "", "## 영역 종합", "",
             "| 영역 | 종합 | 통과 | 미달 | 판정 불가 | 측정 불가 | 출하 밖 | 막는 측정 불가 | 표 sha | 승인 |", "|---|---|---|---|---|---|---|---|---|---|"]
    for area in summary["areas_order"]:
        entry = summary["areas"][area]
        counts = entry["counts"]
        lines.append(f"| {area} | {format_cell(entry['overall'])} | " + " | ".join(str(counts[key]) for key in SUMMARY_COUNT_KEYS)
                     + f" | {entry['blocking_unmeasurable']} | {format_cell(entry['table_sha256'])[:8]} | {format_cell(entry['approval']['state'])} |")
    lines += ["", "## 행별 판정", "", "| 행 | 판정 | 규칙 | 구간 | d | d′ | 한도 ms | S### | SP | p95 | count |", "|---|---|---|---|---|---|---|---|---|---|---|"]
    for area in summary["areas_order"]:
        for row in summary["areas"][area]["rows"]:
            lines.append(f"| {row['row']} | {format_cell(row['label'])} | {format_cell(row['rule'])} | {format_cell(row.get('interval'))} | {format_cell(row.get('d'))} | "
                         f"{format_cell(row.get('d_prime'))} | {format_cell(row['limit_ms'])} | {'·'.join(row['items'])} | {format_cell(row.get('sp'))} | "
                         f"{format_cell(row.get('p95_record'))} | {format_cell(row.get('count'))} |")
    lines += ["", "## PB2:214 영역별 상태 표", "", "| " + " | ".join(summary["pb2_214"][0]) + " |", "|" + "---|" * len(summary["pb2_214"][0])]
    lines += ["| " + " | ".join(format_cell(value) for value in columns.values()) + " |" for columns in summary["pb2_214"]]
    lines += ["", f"- {summary['pb2_214_note']}",
              f"- 5열 FX-P 집계(AM7 L3): 원본 {len(summary['pb2_fxp_sources'])}개(pb2_fxp_sources, {' · '.join(PB2_FXP_SOURCE_GLOBS)})",
              "", "## 열 결함 이름(E8)·사후 결함", ""]
    for area in summary["areas_order"]:
        defects = summary["areas"][area]["column_defects"]
        if defects["evidence_names"] or defects["core_rows"]:
            lines.append(f"- {area}: 증거 열 {defects['evidence_names']} · 핵심 열 결함 의심 행 {defects['core_rows']}")
    lines += [f"- {defect['defect']}: {'·'.join(defect['rows'])} ({defect['path']})" for defect in summary["fxp_items"]["post_hoc_defects"]]
    lines += ["", "## 7절 표기·K 판정 경로", ""]
    for area in summary["areas_order"]:
        entry = summary["areas"][area]
        lines.append(f"- {area}: {' · '.join(entry['labels']) or '—'} · K {len(entry['k_judges'])}개 · 띠 basis {format_cell(entry['band_basis'])}")
    lines += ["", "## FX-P 항목", ""]
    lines += [f"- {item['id']}({item['area']}): {item['text']}" for item in summary["fxp_items"]["fixed"]]
    lines += [f"- 측정 불가: {format_cell(item)}" for item in summary["fxp_items"]["unmeasurable"]]
    lines += [f"- 반대 backend 줄: {format_cell(item)}" for item in summary["fxp_items"]["opposite_backend"]]
    lines += [f"- 영역 밖 항목: {format_cell(item)}" for item in summary["fxp_items"]["unassigned_items"]]
    lines += ["", f"## 남은 판정 대기({summary['pending_judgements']['source']})", ""] + [f"- {item}" for item in summary["pending_judgements"]["items"]]
    return ("\n".join(lines) + "\n").encode("utf-8")


def collect_summary_folders(p_dir):
    folders, problems = {}, []
    for area in AREAS:
        done = find_done_folders(p_dir, area)
        if len(done) != 1:
            problems.append(f"{area}: 끝난 폴더 {len(done)}개 {[folder.name for folder in done]}(정확히 하나여야 한다, C4)")
            continue
        folders[area] = done[0]
    return folders, problems


def summary_command(arguments):
    root = Path(arguments.root)
    out_path, md_path = resolve_path(root, arguments.out), resolve_path(root, arguments.md)
    if out_path.exists():
        print_error(f"summary: --out {relative_posix(root, out_path)}이 이미 있다(C1)")
        return EXIT_INPUT
    p_dir, areas_path = resolve_path(root, arguments.p_dir), resolve_path(root, arguments.areas)
    if not p_dir.is_dir():
        print_error(f"summary: --p-dir {relative_posix(root, p_dir)} 폴더가 없다")
        return EXIT_INPUT
    document, error = read_optional_json(areas_path)
    problems = find_areas_schema_problems(document) if document is not None else [f"--areas 읽기 실패: {error or '없음'}"]
    if not problems:
        folders, problems = collect_summary_folders(p_dir)
    if problems:
        for problem in problems:
            print_error(f"summary: {problem}")
        return EXIT_INPUT
    try:
        summary = build_summary(root, p_dir, areas_path, document, resolve_path(root, arguments.approvals), folders)
    except SummaryInputError as error:
        print_error(f"summary: {error}")
        return EXIT_INPUT
    write_atomic(md_path, render_summary_markdown(summary))
    write_atomic(out_path, json_bytes(summary))
    for area in SHEET_AREAS:
        print(f"{area}: {summary['areas'][area]['overall']} · {summary['areas'][area]['approval']['state']}")
    print(f"썼음: {relative_posix(root, md_path)} · {relative_posix(root, out_path)}")
    return EXIT_OK


def build_parser():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--root", default=str(REPO_ROOT), help="저장소 루트(fixture 사본용, 기본 이 파일의 저장소)")
    parser = argparse.ArgumentParser(description="FluidFX P 계획·잠금 도구(runbook-p-v3 T3)")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", parents=[common])
    build.add_argument("--ledger", required=True)
    build.add_argument("--areas", required=True)
    build.add_argument("--out-dir", required=True)
    build.add_argument("--check", action="store_true")
    current = commands.add_parser("current-lock", parents=[common])
    current.add_argument("--stage", required=True, choices=("L0", "L1"))
    current.add_argument("--area")
    current.add_argument("--next", action="store_true")
    lock_list = commands.add_parser("lock-list", parents=[common])
    lock_list.add_argument("--stage", required=True, choices=("L0", "L1"))
    lock_list.add_argument("--areas")
    lock_list.add_argument("--area")
    lock_list.add_argument("--area-dir")
    preflight = commands.add_parser("preflight", parents=[common])
    preflight.add_argument("--lock", required=True)
    preflight.add_argument("--holder", required=True)
    preflight.add_argument("--out", required=True)
    runs = commands.add_parser("runs", parents=[common])
    runs.add_argument("--plan", required=True)
    runs.add_argument("--lock", required=True)
    runs.add_argument("--out", required=True)
    need_ladder = commands.add_parser("need-ladder", parents=[common])
    need_ladder.add_argument("--area", required=True)
    need_ladder.add_argument("--area-dir", required=True)
    need_ladder.add_argument("--out", required=True)
    select_sp = commands.add_parser("select-sp", parents=[common])
    select_sp.add_argument("--area", required=True)
    select_sp.add_argument("--area-dir", required=True)
    select_sp.add_argument("--out-plan", required=True)
    select_sp.add_argument("--out", required=True)
    status = commands.add_parser("status", parents=[common])
    status.add_argument("--area", required=True)
    status.add_argument("--p-dir", required=True)
    summary = commands.add_parser("summary", parents=[common])
    summary.add_argument("--p-dir", required=True)
    summary.add_argument("--areas", required=True)
    summary.add_argument("--approvals", required=True)
    summary.add_argument("--out", required=True)
    summary.add_argument("--md", required=True)
    return parser


COMMAND_HANDLERS = {"build": build_command, "current-lock": current_lock_command, "lock-list": lock_list_command, "preflight": preflight_command,
                    "runs": runs_command, "need-ladder": need_ladder_command, "select-sp": select_sp_command, "status": status_command, "summary": summary_command}


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace", newline="\n")
    arguments = build_parser().parse_args()
    return COMMAND_HANDLERS[arguments.command](arguments)


if __name__ == "__main__":
    sys.exit(main())
