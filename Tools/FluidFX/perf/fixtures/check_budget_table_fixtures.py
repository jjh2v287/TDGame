"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터 불필요. 판정기 Tools/FluidFX/fx4b_budget_judge.py와 같은 폴더의 예산 표 fixture(fx4b_water_budget.json·smoke_example_budget.json)를 읽는다.
실행 명령: python -B Tools/FluidFX/perf/fixtures/check_budget_table_fixtures.py [판정기 경로(변이 시험용, 기본 Tools/FluidFX/fx4b_budget_judge.py)]
출력: 임시 폴더에 가짜 실행 폴더를 만들어 일반 예산 표 양성(B7-P1~P5)·음성(B7-N1~N9) 시험을 돌리고 시험별 통과·불일치 줄과 합계, 종료 코드(0=모두 통과). 저장소에는 아무것도 쓰지 않는다.
상태: 2026-10-30 CAL-0 B7 일반 예산 표 fixture. FX-4b 86개 회귀는 Saved/AgentOps/20261024/fx4b-acc/budget-fixtures/run_fixtures.py <판정기 경로> --reuse로 따로 돌린다.
"""
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
JUDGE_PATH = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERE.parents[1] / "fx4b_budget_judge.py"
SMOKE_TABLE_PATH = HERE / "smoke_example_budget.json"
WATER_TABLE_PATH = HERE / "fx4b_water_budget.json"

FRAME = "FrameTime"
GAME_THREAD = "GameThreadTime"
STEP_COUNT = "TDFluid/StepCount"
SOLVER = "GPU/TDFluidGpuSolver"
TRANSLUCENCY = "GPU/Translucency"
COLUMNS = ["EVENTS", FRAME, GAME_THREAD, STEP_COUNT, SOLVER, TRANSLUCENCY]
DISCARD_ROWS = 60
DATA_ROWS = 120
ROUNDS = 3
POSITIONS = ((1, "A"), (2, "B"), (3, "B"), (4, "A"))
GPU_TEXT = "<TDFluidSolverBackend.GPU: 1>"
SCENE_TARGETS = {"S1": ["TDGen_SmokeA"], "S2": ["TDGen_SmokeA", "TDGen_SmokeB"]}
BASE_TRANSLUCENCY_MS = 0.1
GENERIC_MET = "영역 성능 예산 충족(proposed): 화면 승인만 남는다"
GENERIC_UNMET_PREFIX = "미달 행의 비용 손잡이 채택을 백업 값으로 되돌리고"


def load_judge():
    spec = importlib.util.spec_from_file_location("fx4b_budget_judge_under_test", JUDGE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_rows(side, options):
    awake = side == "A"
    junk = {FRAME: 40.0, GAME_THREAD: 9.0, STEP_COUNT: 7.0, SOLVER: 9.0, TRANSLUCENCY: 9.0}
    smoke = options["smoke_d"] if awake else 0.0
    data = {FRAME: 16.0 + (options["frame_d"] if awake else 0.0), GAME_THREAD: 8.0, STEP_COUNT: float(options["steps_a"]) if awake else 0.0,
            SOLVER: 0.7 * smoke, TRANSLUCENCY: BASE_TRANSLUCENCY_MS + 0.3 * smoke}
    return [junk] * DISCARD_ROWS + [data] * DATA_ROWS


def render_csv(rows, columns):
    lines = [",".join(columns)]
    lines.extend(",".join("" if name == "EVENTS" else f"{row[name]:.6f}" for name in columns) for row in rows)
    lines.append("[platform],Windows")
    return "\n".join(lines) + "\n"


def make_actors(scene_id, side, options):
    labels = SCENE_TARGETS[scene_id]
    targets = [{"label": label, "awake": side == "A", "grid_resolution": 128, "backend": options["last_target_backend"] if label == labels[-1] else GPU_TEXT} for label in labels]
    return targets + [{"label": "TDGen_OtherFluid", "awake": False, "grid_resolution": 64, "backend": GPU_TEXT}]


def smoke_scenes(**overrides):
    scenes = {"S1": {"smoke_d": 0.30, "frame_d": 0.6, "steps_a": 1, "drop_a": [], "last_target_backend": GPU_TEXT},
              "S2": {"smoke_d": 0.40, "frame_d": 0.8, "steps_a": 2, "drop_a": [], "last_target_backend": GPU_TEXT}}
    for scene_id, change in overrides.items():
        scenes[scene_id].update(change)
    return scenes


def write_run(root, name, scenes):
    run_dir = root / name
    (run_dir / "csv").mkdir(parents=True)
    session_scenes = []
    for scene_id, options in scenes.items():
        blocks = []
        for round_number in range(1, ROUNDS + 1):
            for position, side in POSITIONS:
                slot = f"{scene_id}_r{round_number}_{position}{side}"
                columns = [column for column in COLUMNS if not (side == "A" and column in options["drop_a"])]
                (run_dir / "csv" / f"{slot}.csv").write_text(render_csv(make_rows(side, options), columns), encoding="utf-8")
                blocks.append({"block_id": slot, "slot": slot, "side": side, "attempt": 0, "status": "ok", "kind": "block", "csv": f"csv/{slot}.csv", "actors": make_actors(scene_id, side, options)})
        session_scenes.append({"id": scene_id, "error": None, "shader_gate": {"passed": True}, "blocks": blocks})
    (run_dir / "session.json").write_text(json.dumps({"run_token": name, "scenes": session_scenes}), encoding="utf-8")
    (run_dir / "plan.json").write_text(json.dumps({"discard_frames": DISCARD_ROWS, "scenes": [{"id": scene_id, "meta": {}} for scene_id in scenes]}), encoding="utf-8")
    return run_dir


def write_table_variant(root, name, change):
    data = json.loads(SMOKE_TABLE_PATH.read_text(encoding="utf-8"))
    change(data)
    path = root / f"{name}.json"
    path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
    return path


def load_table(module, path):
    table, errors = module.load_budget_table(path)
    if errors:
        raise RuntimeError(f"예산 표 오류 {path.name}: {errors}")
    return table


def judge(module, run_dir, table, mode="judge", reference_ms=None):
    context, error = module.load_context(run_dir, table)
    if error is not None:
        raise RuntimeError(error)
    result = module.clean(module.run_judge(context, reference_ms) if mode == "judge" else module.run_pilot(context, reference_ms))
    (module.format_judge if mode == "judge" else module.format_pilot)(result)
    return result


def run_cli(*args):
    return subprocess.run([sys.executable, "-B", str(JUDGE_PATH), *[str(argument) for argument in args]], capture_output=True, text=True, encoding="utf-8", errors="replace")


def dig(value, path):
    for part in path.split("."):
        if isinstance(value, list):
            value = len(value) if part == "length" else (value[int(part)] if part.isdigit() and int(part) < len(value) else None)
        elif isinstance(value, dict):
            value = value.get(part)
        else:
            return None
    return value


def matches(actual, expected):
    if callable(expected):
        return bool(expected(actual))
    if isinstance(expected, bool) or isinstance(actual, bool) or expected is None:
        return actual is expected
    if isinstance(expected, (int, float)) and isinstance(actual, (int, float)):
        return abs(actual - expected) <= 1e-6
    return actual == expected


def contains(text):
    return lambda actual: isinstance(actual, str) and text in actual


def compare(case_id, result, expectations):
    failures = [f"{case_id}: {path} 기대 {getattr(expected, '__name__', expected)!r} 실제 {dig(result, path)!r}" for path, expected in expectations.items() if not matches(dig(result, path), expected)]
    return len(expectations), failures


def check_table_identity(module, root):
    water, errors = module.load_budget_table(WATER_TABLE_PATH)
    builtin = {key: value for key, value in module.DEFAULT_BUDGET_TABLE.items() if key != "source_file"}
    loaded = {key: value for key, value in (water or {}).items() if key != "source_file"}
    failures = [f"B7-P1: 파일 표 오류 {errors}"] if errors else []
    failures += [] if loaded == builtin else ["B7-P1: fx4b_water_budget.json 정규화 결과가 내장 FX-4b 표와 다르다"]
    failures += [f"B7-P1: 내장 표 오류 {found}" for found in [module.find_table_errors(module.FX4B_BUDGET_TABLE)] if found]
    return 3, failures


def check_smoke_pass(module, root, smoke):
    run_dir = write_run(root, "pass", smoke_scenes())
    result = judge(module, run_dir, smoke)
    checked, failures = compare("B7-P2", result, {
        "rows.S1G.verdict": "통과", "rows.S1G.outcome.interval.upper": 0.3, "rows.S1G.stage_rounds": 3, "rows.S1G.metric": "smoke_gpu", "rows.S1G.source": "가상 한도",
        "rows.S1G.limit_ms": 0.5, "rows.S1G.label": "연기 GPU d", "rows.S1F.verdict": "통과", "rows.S1F.outcome.interval.upper": 0.6, "rows.S1F.label": "켬-끔 프레임 시간 중앙값 d",
        "rows.S2G.verdict": "통과", "rows.S2G.outcome.interval.lower": 0.4, "rows.S1T.verdict": "통과", "rows.S1T.outcome.interval.upper": 8.0, "rows.S1T.label": "게임 스레드 A A p50",
        "rows.S1T.round_table.0.B": None, "reference": None, "budget_verdict": "충족(proposed)", "consequence": GENERIC_MET,
        "budget_table.area": "smoke_example", "records.S2.records.pairs": 6, "blocks.S1.0.pdh_valid": None, "warnings.length": 0})
    edge = judge(module, write_run(root, "edge", smoke_scenes(S1={"smoke_d": 0.5})), smoke)
    edge_checked, edge_failures = compare("B7-P4", edge, {"rows.S1G.verdict": "통과", "rows.S1G.outcome.interval.upper": 0.5, "budget_verdict": "충족(proposed)"})
    referenced = load_table(module, write_table_variant(root, "reference-s1", lambda data: data.update(reference={"scene": "S1", "metric": "smoke_gpu", "ratio_range": [0.8, 1.5]})))
    reference_result = judge(module, run_dir, referenced, reference_ms=0.4)
    reference_checked, reference_failures = compare("B7-P5", reference_result, {
        "reference.scene": "S1", "reference.status": "유효", "reference.named_a_p50": 0.4, "reference.pdh_valid_a_blocks": "6/6", "budget_verdict": "충족(proposed)"})
    far = judge(module, run_dir, referenced, reference_ms=0.2)
    far_checked, far_failures = compare("B7-N8", far, {"reference.status": "범위 밖", "rows.S1G.verdict": "판정 불가", "rows.S1G.reasons.0.detail": contains("(a) S1 세션 무효"), "budget_verdict": "판정 불가"})
    return checked + edge_checked + reference_checked + far_checked, failures + edge_failures + reference_failures + far_failures


def check_smoke_cli(module, root, smoke):
    run_dir = root / "pass"
    failures = []
    output = root / "cli-judge.json"
    completed = run_cli("judge", run_dir, "--budget-table", SMOKE_TABLE_PATH, "--out", output)
    if completed.returncode != 0 or "예산 표: smoke_example" not in completed.stdout or "기준 장면: 없음" not in completed.stdout:
        failures.append(f"B7-P3: CLI judge 종료 {completed.returncode} 출력 {completed.stdout[:160]!r} {completed.stderr[:160]!r}")
    elif json.loads(output.read_text(encoding="utf-8")) != judge(module, run_dir, load_table(module, SMOKE_TABLE_PATH)):
        failures.append("B7-P3: CLI --out JSON이 모듈 결과와 다르다")
    pilot = run_cli("pilot", run_dir, "--budget-table", SMOKE_TABLE_PATH, "--out", root / "cli-pilot.json")
    if pilot.returncode != 0:
        failures.append(f"B7-P3: CLI pilot 종료 {pilot.returncode} {pilot.stderr[:160]!r}")
        return 2, failures
    checked, pilot_failures = compare("B7-P3", json.loads((root / "cli-pilot.json").read_text(encoding="utf-8")), {
        "ready": True, "scenes.S1.missing_columns.연기 GPU.length": 0, "scenes.S1.missing_columns.프레임 시간.length": 0, "scenes.S2.blocks.0.pdh_valid": None, "pdh_invalid_blocks.length": 0})
    return 2 + checked, failures + pilot_failures


def check_smoke_negatives(module, root, smoke):
    cases = (
        ("B7-N1", write_run(root, "over", smoke_scenes(S1={"smoke_d": 0.7})), smoke,
         {"rows.S1G.verdict": "미달", "rows.S1G.outcome.interval.lower": 0.7, "rows.S1F.verdict": "통과", "budget_verdict": "미충족", "consequence": contains(GENERIC_UNMET_PREFIX)}),
        ("B7-N2", write_run(root, "missing", smoke_scenes(S1={"drop_a": [TRANSLUCENCY]})), smoke,
         {"rows.S1G.verdict": "판정 불가", "rows.S1G.reason": "필수 열 없음", "rows.S1G.reasons.0.detail": contains(f"A:{TRANSLUCENCY}"), "rows.S1F.verdict": "통과", "budget_verdict": "판정 불가"}),
        ("B7-N3", root / "pass", load_table(module, write_table_variant(root, "water-check-on", lambda data: data.update(water_path_check=True))),
         {"rows.S1G.verdict": "판정 불가", "rows.S1G.reason": "유효성", "rows.S1G.reasons.0.detail": contains("(d) 물 경로"), "budget_verdict": "판정 불가"}),
        ("B7-N4", root / "pass", load_table(module, write_table_variant(root, "single-awake", lambda data: data["scenes"]["S2"].pop("awake_step_count"))),
         {"rows.S2G.verdict": "판정 불가", "rows.S2G.reason": "유효성", "rows.S2G.reasons.0.detail": contains("(c) 스텝 수"), "rows.S1G.verdict": "통과", "budget_verdict": "판정 불가"}),
        ("B7-N9", write_run(root, "second-target-cpu", smoke_scenes(S2={"last_target_backend": "<TDFluidSolverBackend.CPU: 0>"})), smoke,
         {"rows.S2G.verdict": "판정 불가", "rows.S2G.reason": "유효성", "rows.S2G.reasons.0.detail": contains("TDGen_SmokeB: backend"), "rows.S1G.verdict": "통과"}),
    )
    checked, failures = 0, []
    for case_id, run_dir, table, expectations in cases:
        case_checked, case_failures = compare(case_id, judge(module, run_dir, table), expectations)
        checked += case_checked
        failures += case_failures
    pilot_checked, pilot_failures = compare("B7-N2", judge(module, root / "missing", smoke, "pilot"), {"ready": False, "scenes.S1.missing_columns.연기 GPU.0": f"A:{TRANSLUCENCY}"})
    return checked + pilot_checked, failures + pilot_failures


def set_row_metric(data):
    data["rows"][0]["metric"] = "nope"


def rename_limit(data):
    data["rows"][0]["limt_ms"] = data["rows"][0].pop("limit_ms")


def add_moving_ratio(data):
    data["rows"][0]["moving_p95_ratio"] = 1.5


def duplicate_row(data):
    data["rows"].append(dict(data["rows"][0]))


TABLE_ERROR_CASES = (
    ("unknown-metric", set_row_metric, "지표 'nope'이 없다"),
    ("typo-key", rename_limit, "모르는 키 limt_ms"),
    ("no-water-flag", lambda data: data.pop("water_path_check"), "water_path_check"),
    ("interval-moving", add_moving_ratio, "walk 행에만"),
    ("no-grid-key", lambda data: data["scenes"]["S1"].pop("grid"), "grid 필요"),
    ("builtin-redefined", lambda data: data["metrics"].update(named_gpu={"columns": [SOLVER]}), "내장 지표 이름과 겹친다"),
    ("duplicate-row", duplicate_row, "겹친다"),
    ("bad-backend", lambda data: data["scenes"]["S1"].update(backend="Gpu"), "backend 'Gpu'"),
)


def check_table_errors(module, root):
    failures = []
    for name, change, expected in TABLE_ERROR_CASES:
        path = write_table_variant(root, name, change)
        table, errors = module.load_budget_table(path)
        if table is not None or not any(expected in error for error in errors):
            failures.append(f"B7-N5: {name} 기대 오류 '{expected}' 실제 {errors}")
    completed = run_cli("judge", root / "pass", "--budget-table", root / "unknown-metric.json")
    if completed.returncode != 2 or "예산 표 오류" not in completed.stderr:
        failures.append(f"B7-N5: CLI 표 오류 종료 {completed.returncode} {completed.stderr[:160]!r}")
    return len(TABLE_ERROR_CASES) + 1, failures


def check_reference_arguments(root):
    failures = []
    forbidden = run_cli("judge", root / "pass", "--budget-table", SMOKE_TABLE_PATH, "--ref-ms", "1.0")
    if forbidden.returncode != 2:
        failures.append(f"B7-N6: reference 없는 표에 --ref-ms 종료 {forbidden.returncode}(기대 2)")
    required = run_cli("judge", root / "pass", "--budget-table", WATER_TABLE_PATH)
    if required.returncode != 2:
        failures.append(f"B7-N7: reference 있는 표에 기준 ms 없음 종료 {required.returncode}(기대 2)")
    return 2, failures


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    module = load_judge()
    smoke = load_table(module, SMOKE_TABLE_PATH)
    checked, failures = 0, []
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        steps = (lambda: check_table_identity(module, root), lambda: check_smoke_pass(module, root, smoke), lambda: check_smoke_cli(module, root, smoke),
                 lambda: check_smoke_negatives(module, root, smoke), lambda: check_table_errors(module, root), lambda: check_reference_arguments(root))
        for step in steps:
            try:
                step_checked, step_failures = step()
            except Exception as error:
                step_checked, step_failures = 1, [f"예외 {type(error).__name__}: {error}"]
            checked += step_checked
            failures += step_failures
    print(f"일반 예산 표 fixture 검사 {checked}개, 불일치 {len(failures)}개 (판정기 {JUDGE_PATH})")
    for failure in failures:
        print("  불일치", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
