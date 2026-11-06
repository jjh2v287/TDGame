"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), 에디터·빌드·unreal 모듈 불필요. Tools/FluidFX/fx4b_budget_judge.py의 P 방식(runbook-p-v3 5절 T2)을 모듈로 불러 가짜 P 실행 폴더(session.json·plan.json·csv/)와 가짜 P 표로 판정하고, _p 판별·--out·종료 코드는 하위 프로세스로 부른다. 판정기 옆 fx7pr_judge.py가 있어야 한다(체제 관문 재사용).
실행 명령: python -B Tools/FluidFX/perf/p/fixtures_t2.py [판정기 경로(변이 시험용, 기본 Tools/FluidFX/fx4b_budget_judge.py)] [--keep]
출력: 표준 출력에 사례마다 '통과|실패 <이름>: <근거>' 한 줄과 끝 줄 'fixtures_t2: 통과 n/m', 종료 0 전부 통과/1 하나라도 실패. 가짜 실행 폴더·표·--out 파일은 Saved/AgentOps/20261106/p/tools/t2/tmp/<시각>/ 아래에 만들고 --keep이 없으면 끝에 지운다
상태: 2026-11-06 작성(P T2 차선). 실제 PIE 실행 폴더로는 확인하지 않았다(P fire 시운전이 확인)
"""
import copy
import csv
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import traceback
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
ARGUMENTS = [argument for argument in sys.argv[1:] if not argument.startswith("--")]
KEEP = "--keep" in sys.argv
JUDGE_PATH = Path(ARGUMENTS[0]).resolve() if ARGUMENTS else ROOT / "Tools" / "FluidFX" / "fx4b_budget_judge.py"
TMP_ROOT = ROOT / "Saved" / "AgentOps" / "20261106" / "p" / "tools" / "t2" / "tmp"
RUN_TOKEN = "20261106120000"
DISCARD = 2
FRAMES = 12
LEVEL = "/Game/FluidFX/Maps/LV_TDFluidFixtureStage"
LABEL = "TDGen_FixtureFluid"
OPSP = "r.Editor.Viewport.OverridePIEScreenPercentage"
STEP_COUNT = "TDFluid/StepCount"
SOLVER = "GPU/TDFluidGpuSolver"
WETNESS_COLUMNS = ["TDFluid/GameThread/WaterWetness", "TDFluid/AllWorkers/WaterWetness"]
SPLAT = "TDFluid/MassInputSplatCalls"
DATA_POINTS = "TDFluid/MassInputDataChannelPoints"
OTHERS_SLEEP = {"action": "sleep", "targets": "others", "awake": False}
TABLE_METRICS = {
    "ft": {"label": "FrameTime 쌍 차분", "columns": ["FrameTime"], "pair": "difference", "pdh": False, "backends": [], "pixel_scale": True, "p95_record": True},
    "gt": {"label": "GameThreadTime 쌍 차분", "columns": ["GameThreadTime"], "pair": "difference", "pdh": False, "backends": []},
    "rt": {"label": "RTSUM 쌍 차분(EventWait 제외)", "columns": [], "pair": "difference", "pdh": False, "backends": [],
           "sum_prefix": {"include": "Exclusive/RenderThread/", "exclude": "Exclusive/RenderThread/EventWait"}},
    "ng": {"label": "GPU/TDFluidGpuSolver A p50", "columns": ["GPU/TDFluidGpuSolver"], "pair": "a_only", "pdh": False, "backends": ["GPU"], "p95_record": True},
}
DEFAULT_MODEL = {"frame": {"A": 10.5, "B": 10.0}, "game": {"A": 3.2, "B": 3.0}, "gpu": {"A": 8.0, "B": 7.5}, "control": {"A": 0.5, "B": 0.4},
                 "rt_other": {"A": 1.55, "B": 1.6}, "wait": {"A": 50.0, "B": 1.0}, "solver": {"A": 0.3, "B": None}}
SCENE_DEFAULTS = {"toggle": "Ls", "targets": [LABEL], "aw": None, "awake_step_count": None, "backend": "CPU", "grid": None, "water_layer": False,
                  "rows": [("ft", 1.0)], "rounds": 3, "sp": 100, "sp_readback": None, "viewport": [1920, 1080], "variables": {}, "drop_variables": [],
                  "setup": [], "setup_readback": {}, "parts": {}, "light": False, "light_labels": None, "console": {}, "evidence_columns": [], "x_labels": None,
                  "model": None, "step_count": None, "mutate": None, "short_columns": None, "actors": None, "actor_backend": {}, "actor_grid": {},
                  "side_steps": None, "drop_conditions_viewport": False, "block_status": None}
RESULTS = []


def check(name, condition, detail=""):
    RESULTS.append(bool(condition))
    print(f"{'통과' if condition else '실패'} {name}: {detail}")


def load_judge():
    spec = importlib.util.spec_from_file_location("fx4b_budget_judge_t2", JUDGE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def resolve_value(entry, side, round_number, position):
    value = entry.get(side) if isinstance(entry, dict) else entry
    return value(round_number, position) if callable(value) else value


def frame_values(value):
    if isinstance(value, list):
        return [value[0]] * DISCARD + value
    return [value] * (DISCARD + FRAMES)


def write_csv(path, columns, short_columns=0):
    names = list(columns)
    series = {name: frame_values(columns[name]) for name in names}
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(names)
        for index in range(DISCARD + FRAMES):
            row = [repr(float(series[name][index])) for name in names]
            writer.writerow(row[:len(row) - short_columns] if short_columns else row)


def symbols_of(scene):
    return scene["toggle"].split("+")


def complete_scene(scene_id, spec):
    scene = {**copy.deepcopy(SCENE_DEFAULTS), **copy.deepcopy(spec), "id": scene_id}
    symbols = symbols_of(scene)
    base = symbols[0]
    scene["targets"] = list(scene["targets"])
    scene["grid"] = scene["grid"] if scene["grid"] is not None else ({label: 128 for label in scene["targets"]} if scene["backend"] else {})
    scene["awake_step_count"] = scene["awake_step_count"] if scene["awake_step_count"] is not None else len(scene["targets"])
    scene["aw"] = scene["aw"] or ("A" if base in ("L", "Ls") else ("both" if scene["targets"] else "none"))
    scene["x_labels"] = scene["x_labels"] or list(scene["targets"])
    scene["light_labels"] = scene["light_labels"] or list(scene["targets"])
    scene["model"] = {**DEFAULT_MODEL, **(scene["model"] or {})}
    scene["regime"] = any(metric == "ft" for metric, _ in scene["rows"])
    scene["evidence"] = {"step_count": base in ("L", "Ls", "Lb"), "columns": list(scene["evidence_columns"]), "parts": scene["parts"], "light": scene["light"],
                         "console": scene["console"], "actor": {"hidden": "X" in symbols, "niagara": "Xn" in symbols} if ("X" in symbols or "Xn" in symbols) else None}
    return scene


def build_toggle_steps(scene, side):
    awake = side == "a"
    symbols = symbols_of(scene)
    steps = []
    if symbols[0] in ("L", "Ls"):
        steps.append({"action": "sleep", "targets": "scene", "awake": awake})
    for label, parts in scene["parts"].items():
        steps.append({"action": "visibility", "targets": [label], "parts": parts, "visible": awake})
    if scene["light"]:
        steps.append({"action": "light", "targets": scene["light_labels"], "visible": awake})
    if scene["console"]:
        steps.append({"action": "console", "commands": [f"{name} {values['A' if awake else 'B']:g}" for name, values in scene["console"].items()]})
    if "X" in symbols:
        steps.append({"action": "actor", "labels": scene["x_labels"], "hidden": not awake})
    if "Xn" in symbols:
        steps.append({"action": "actor", "labels": scene["x_labels"], "niagara": "activate" if awake else "deactivate"})
    return steps


def build_plan_scene(scene):
    setup = [dict(OTHERS_SLEEP)] + scene["setup"] + [{"action": "console", "commands": [f"r.ScreenPercentage {scene['sp']}"], "restore": True}]
    return {"id": scene["id"], "level": LEVEL, "targets": scene["targets"], "setup": setup, "a": build_toggle_steps(scene, "a"), "b": build_toggle_steps(scene, "b"),
            "meta": {"screen_percentage": scene["sp"], "count": None, "backend": scene["backend"], "grid": scene["grid"], "toggle": scene["toggle"],
                     "rows": [f"{scene['id']}/{metric}" for metric, _ in scene["rows"]]},
            "gpu_clock": scene["backend"] == "GPU", "required_columns": ["FrameTime", "GameThreadTime", "GPUTime"], "metric": "FrameTime"}


def build_table_scene(scene):
    return {"backend": scene["backend"], "grid": scene["grid"], "target_labels": scene["targets"], "awake_step_count": scene["awake_step_count"], "toggle": scene["toggle"],
            "awake_sides": scene["aw"], "water_layer": scene["water_layer"], "regime": scene["regime"], "evidence": scene["evidence"], "source": LEVEL}


def describe_setup_result(scene, step):
    if step["action"] != "console":
        return {"action": step["action"], "result": {"matched": 1} if step["action"] == "camera" else {"awake": False, "actors": []}}
    readback = {}
    for command in step["commands"]:
        name, value = command.split()
        readback[name] = float(value)
        if name == "r.ScreenPercentage":
            readback[name] = float(scene["sp_readback"] if scene["sp_readback"] is not None else value)
        elif name in scene["setup_readback"]:
            readback[name] = scene["setup_readback"][name]
    return {"action": "console", "result": {"readback": readback}}


def default_step_count(scene, side):
    if scene["aw"] == "none":
        return None
    if scene["aw"] == "A":
        return scene["awake_step_count"] if side == "A" else 0
    return scene["awake_step_count"]


def default_columns(scene, side, round_number, position):
    values = {key: resolve_value(entry, side, round_number, position) for key, entry in scene["model"].items()}
    columns = {"FrameTime": values["frame"], "GameThreadTime": values["game"], "GPUTime": values["gpu"], "RenderThreadTime": 0.001,
               "Exclusive/RenderThread/SkyAtmosphere": values["control"], "Exclusive/RenderThread/EventWait": values["wait"], "Exclusive/RenderThread/EventWaitForTasks": values["wait"]}
    step = scene["step_count"](side, round_number, position) if scene["step_count"] else default_step_count(scene, side)
    if step is not None:
        columns[STEP_COUNT] = step
    if scene["backend"] == "GPU" and values["solver"] is not None:
        columns[SOLVER] = values["solver"]
    if side == "A":
        for name in dict.fromkeys(column.rsplit("/", 1)[-1] for column in scene["evidence_columns"]):
            first = next(column for column in scene["evidence_columns"] if column.rsplit("/", 1)[-1] == name)
            columns[first] = 1.0
    columns["Exclusive/RenderThread/Other"] = values["rt_other"]
    if scene["mutate"]:
        scene["mutate"](columns, side, round_number, position)
    return columns


def default_actors(scene, side):
    actors = []
    for label in scene["targets"]:
        visible = {"cards": True, **{part: side == "A" for part in scene["parts"].get(label, [])}}
        lights = [side == "A"] if scene["light"] and label in scene["light_labels"] else []
        actors.append({"label": label, "awake": scene["aw"] == "both" or side == "A", "backend": scene["actor_backend"].get(label, f"TDFluidSolverBackend.{scene['backend'] or 'CPU'}"),
                       "grid_resolution": scene["actor_grid"].get(label, scene["grid"].get(label, 128)), "visible": visible, "lights_visible": lights})
    return actors


def default_side_steps(scene, side):
    steps = []
    symbols = symbols_of(scene)
    if symbols[0] in ("L", "Ls"):
        steps.append({"action": "sleep", "result": {"awake": side == "A", "actors": scene["targets"]}})
    if scene["console"]:
        steps.append({"action": "console", "result": {"readback": {name: float(values[side]) for name, values in scene["console"].items()}}})
    matched = len(scene["x_labels"])
    if "X" in symbols:
        steps.append({"action": "actor", "result": {"labels": scene["x_labels"], "matched": matched, "hidden": matched if side == "B" else 0}})
    if "Xn" in symbols:
        steps.append({"action": "actor", "result": {"labels": scene["x_labels"], "matched": matched, "ns_inactive": matched if side == "B" else 0}})
    return steps


def build_record(scene, plan_scene, run_dir):
    variables = {"r.ScreenPercentage": 100.0, OPSP: 1.0, "TD.FluidFX.WaterGpu": 0.0, **scene["variables"]}
    for name in scene["drop_variables"]:
        variables.pop(name, None)
    conditions = {"variables": variables} if scene["drop_conditions_viewport"] else {"variables": variables, "viewport": scene["viewport"]}
    steps = [describe_setup_result(scene, step) for step in plan_scene["setup"] if step["action"] != "wait"]
    record = {"id": scene["id"], "kind": "pair", "level": LEVEL, "meta": plan_scene["meta"], "steps": steps, "conditions": conditions, "blocks": [], "error": None,
              "rounds": scene["rounds"]}
    for round_number in range(1, scene["rounds"] + 1):
        for position, side in enumerate("ABBA", 1):
            block_id = f"{scene['id']}_r{round_number}_{position}{side}"
            columns = default_columns(scene, side, round_number, position)
            short = scene["short_columns"](side, round_number, position) if scene["short_columns"] else 0
            write_csv(run_dir / "csv" / f"{block_id}.csv", columns, short)
            actors = scene["actors"](side, default_actors(scene, side)) if scene["actors"] else default_actors(scene, side)
            side_steps = scene["side_steps"](side, default_side_steps(scene, side)) if scene["side_steps"] else default_side_steps(scene, side)
            status = scene["block_status"](side, round_number, position) if scene["block_status"] else "ok"
            record["blocks"].append({"block_id": block_id, "scene": scene["id"], "csv": f"csv/{block_id}.csv", "kind": "block", "side": side, "slot": block_id,
                                     "attempt": 0, "status": status, "clock_mhz": None, "actors": actors, "side_steps": side_steps})
    return record


def build_case(work, case_id, scene_specs, with_p=True):
    run_dir = work / "runs" / f"p-fixture-{case_id}-{RUN_TOKEN}"
    plan = {"run_label": f"p-fixture-{case_id}", "block_frames": FRAMES, "discard_frames": DISCARD, "order": "ABBA", "repeats": 5, "unstable_repeats": 5,
            "write_fx7p_evidence": False, "scenes": []}
    if with_p:
        plan["_p"] = {"generator": "Tools/FluidFX/perf/p/fixtures_t2.py", "stage": "judge", "area": "fixture"}
    session = {"run_token": RUN_TOKEN, "plan": f"fixtures_t2/{case_id}.json", "scenes": []}
    table = {"version": 1, "area": "fixture", "title": f"T2 fixture {case_id}", "water_path_check": False, "reference": None, "scenes": {},
             "metrics": copy.deepcopy(TABLE_METRICS), "rows": [], "records": [], "consequences": {}}
    for scene_id, spec in scene_specs.items():
        scene = complete_scene(scene_id, spec)
        plan_scene = build_plan_scene(scene)
        plan["scenes"].append(plan_scene)
        session["scenes"].append(build_record(scene, plan_scene, run_dir))
        table["scenes"][scene_id] = build_table_scene(scene)
        table["rows"].extend({"row": f"{scene_id}/{metric}", "scene": scene_id, "metric": metric, "limit_ms": limit, "ledger_row": f"{scene_id}/{metric}"} for metric, limit in scene["rows"])
    write_json(run_dir / "plan.json", plan)
    write_json(run_dir / "session.json", session)
    table_path = work / "tables" / f"{case_id}.json"
    write_json(table_path, table)
    return run_dir, table_path


def analyze(module, run_dir, table_path, mode="judge"):
    table, errors = module.load_budget_table(table_path, p_mode=True)
    if errors:
        raise RuntimeError(f"표 오류 {errors}")
    context, error = module.load_context(run_dir, table)
    if error:
        raise RuntimeError(error)
    result = module.run_p_pilot(context) if mode == "pilot" else module.run_p_judge(context)
    cleaned = module.clean(result)
    (module.format_p_pilot if mode == "pilot" else module.format_p_judge)(cleaned)
    return cleaned


def run_cli(*arguments):
    return subprocess.run([sys.executable, "-B", str(JUDGE_PATH), *[str(argument) for argument in arguments]], capture_output=True, text=True, encoding="utf-8", errors="replace")


def row_of(result, row_id):
    return result["rows"][row_id]


def describe_row(row):
    return f"{row['verdict']}({row['reason']}) d {row['interval']} d′ {(row['d_prime'] or {}).get('interval')} 라운드 {row['rounds']}"


def expect_row(result, row_id, verdict, reason=None, name=None):
    row = row_of(result, row_id)
    matched = row["verdict"] == verdict and (reason is None or row["reason"] == reason)
    check(name or f"{row_id} {verdict}({reason})", matched, describe_row(row) + (f" 사유들 {[item['reason'] for item in row['reasons']]}" if not matched else ""))
    return row


def judge_case(module, work, case_id, scene_specs, mode="judge"):
    run_dir, table_path = build_case(work, case_id, scene_specs)
    return analyze(module, run_dir, table_path, mode)


def alternating(first, second):
    return lambda round_number, position: first if position == 1 else second


def check_p_mode(module, work):
    result = judge_case(module, work, "p_cloud_x", {"CL1": {"toggle": "X", "targets": [], "backend": None, "x_labels": ["TDGen_CloudLayers"], "rows": [("ft", 2.5)]}})
    expect_row(result, "CL1/ft", "통과", name="P 방식 구름 X target_labels []·awake 0 통과")
    result = judge_case(module, work, "p_aw_both", {"FI5b": {"toggle": "Xn", "targets": ["TDGen_EmberB_Campfire"], "x_labels": ["TDGen_EmberB_Campfire"], "rows": [("ft", 1.0)]}})
    expect_row(result, "FI5b/ft", "통과", name="P 방식 aw both(Xn) 통과")
    fire = {"toggle": "L", "targets": ["TDGen_CampfireFire", "TDGen_CampfireSmoke"], "grid": {"TDGen_CampfireFire": 128, "TDGen_CampfireSmoke": 96},
            "parts": {"TDGen_CampfireFire": ["cards", "embers"], "TDGen_CampfireSmoke": ["volumes"]}, "light": True, "light_labels": ["TDGen_CampfireSmoke"],
            "rows": [("ft", 4.95), ("gt", 2.0), ("rt", 0.15)]}
    result = judge_case(module, work, "p_fi1", {"FI1": fire})
    for row_id in ("FI1/ft", "FI1/gt", "FI1/rt"):
        expect_row(result, row_id, "통과", name=f"P 방식 FI1 128/96 {row_id} 통과")
    pilot = judge_case(module, work, "p_fi1_pilot", {"FI1": fire}, mode="pilot")
    scene = pilot["scenes"]["FI1"]
    check("P 시운전 FI1 백엔드 증거·체제 강·핵심 열 결함 0", scene["backend_evidence"]["ok"] and scene["regime"]["strong"] and not scene["header_defects"] and scene["error"] is None and scene["pairs"] == 6,
          f"backend {scene['backend_evidence']['problems']} regime {scene['regime']['strong']} 결함 {scene['header_defects']} 쌍 {scene['pairs']}")
    check("P 시운전 출력 키", all(key in scene for key in ("regime", "evidence", "backend_evidence", "header", "header_defects", "viewport", "sp", "pairs", "error")) and pilot["run_token"] == RUN_TOKEN
          and pilot["budget_table"]["sha256"], f"키 {sorted(scene)}")
    tr3 = {"toggle": "L", "targets": ["TDGen_SandDust"], "backend": "GPU", "grid": {"TDGen_SandDust": 256}, "actor_grid": {"TDGen_SandDust": 192}, "parts": {"TDGen_SandDust": ["volumes"]}}
    result = judge_case(module, work, "p_tr3", {"TR3": tr3})
    expect_row(result, "TR3/ft", "통과", name="P 방식 TR3 설정 192 → 실효 256 통과")
    result = judge_case(module, work, "p_tr3_bad", {"TR3": {**tr3, "actor_grid": {"TDGen_SandDust": 100}}})
    expect_row(result, "TR3/ft", "판정 불가", "백엔드 증거", name="P 방식 설정 100 → 실효 128 ≠ 256 판정 불가(백엔드 증거)")
    run_dir, table_path = build_case(work, "p_order_plain", {"FI1": fire}, with_p=False)
    plain = run_cli("judge", run_dir, "--budget-table", table_path)
    check("_p 없는 계획 + P 표 → 기존 표 검사 종료 2(_p 판별이 표 검사보다 먼저)", plain.returncode == 2 and "grid" in plain.stderr, f"종료 {plain.returncode} {plain.stderr[-160:]!r}")
    run_dir, table_path = build_case(work, "p_order_p", {"FI1": fire})
    with_p = run_cli("judge", run_dir, "--budget-table", table_path)
    check("_p 있는 계획 + 같은 P 표 → 종료 0", with_p.returncode == 0 and "[judge P]" in with_p.stdout, f"종료 {with_p.returncode} {with_p.stderr[-160:]!r}")


def check_pixel_cases(module, work):
    small = [1600, 900]
    result = judge_case(module, work, "a1_pass", {"FT": {"viewport": small, "rows": [("ft", 1.0)]}})
    row = expect_row(result, "FT/ft", "통과", name="A1 d·d′ 둘 다 통과")
    check("A1 d′ 환산 = 2073600/(1600×900)", row["d_prime"] is not None and abs(row["d_prime"]["scale"] - 1.44) < 1e-9 and abs(row["d_prime"]["interval"]["upper"] - 0.72) < 1e-6, f"{row['d_prime']}")
    straddle = {"viewport": small, "rounds": 5, "rows": [("ft", 1.0)], "model": {"frame": {"A": alternating(10.6, 10.75), "B": 10.0}}}
    result = judge_case(module, work, "a1_straddle", {"FT": straddle})
    row = expect_row(result, "FT/ft", "판정 불가", "걸침", name="A1 d 통과·d′ 걸침 → 5라운드에도 걸침 판정 불가(걸침)")
    check("A1 걸침은 5라운드까지 간다", len(row["trace"]) == 2 and row["trace"][1]["rounds"] == 5 and row["d_prime"]["interval"]["state"] == "걸침" and row["interval"]["state"] == "통과", f"{row['trace']}")
    split = {"viewport": small, "rows": [("ft", 1.0)], "model": {"frame": {"A": alternating(10.75, 10.8), "B": 10.0}}}
    result = judge_case(module, work, "a1_split", {"FT": split})
    expect_row(result, "FT/ft", "판정 불가", "화소 환산 갈림", name="A1 d 통과·d′ 미달 → 판정 불가(화소 환산 갈림)")
    result = judge_case(module, work, "a1_no_viewport", {"FT": {"drop_conditions_viewport": True, "rows": [("ft", 1.0), ("gt", 1.0)]}})
    expect_row(result, "FT/ft", "판정 불가", "화소 기록 없음", name="A1 뷰포트 없음 → ft 판정 불가(화소 기록 없음)")
    expect_row(result, "FT/gt", "통과", name="A1 뷰포트 없음은 gt 행에 영향 없음")
    result = judge_case(module, work, "a1_no_opsp", {"FT": {"drop_variables": [OPSP]}})
    expect_row(result, "FT/ft", "판정 불가", "화소 기록 없음", name="PR-8 ⑧ OPSP 기록 없음 → 판정 불가(화소 기록 없음)")
    result = judge_case(module, work, "sp160", {"FT": {"sp": 160, "rows": [("ft", 1.0)]}})
    row = expect_row(result, "FT/ft", "통과", name="SP160 conditions 100·meta 160·되읽기 160 통과")
    check("SP160 d′ 환산은 meta 160(conditions 100 아님)", abs(row["d_prime"]["scale"] - 1 / 2.56) < 1e-9 and row["sp"] == {"meta": 160, "readback": 160.0}, f"{row['d_prime']['scale']} {row['sp']}")
    result = judge_case(module, work, "sp100", {"FT": {"rows": [("ft", 1.0)]}})
    row = expect_row(result, "FT/ft", "통과", name="SP100 단계 통과(C5)")
    check("SP100 되읽기 기록", row["sp"] == {"meta": 100, "readback": 100.0} and row["viewport"] == [1920, 1080], f"{row['sp']} {row['viewport']}")
    result = judge_case(module, work, "sp_mismatch", {"FT": {"sp_readback": 130, "rows": [("ft", 1.0), ("gt", 1.0)]}})
    expect_row(result, "FT/ft", "판정 불가", "화소 기록", name="SP 되읽기 130 ≠ meta 100 → ft 판정 불가(화소 기록)")
    expect_row(result, "FT/gt", "통과", name="SP 되읽기 ≠ meta는 ft만(E4)")


def check_regime_cases(module, work):
    one_side = {"model": {"gpu": {"A": 8.0, "B": 3.8}}}
    result = judge_case(module, work, "regime_one_side", {"FT": one_side})
    expect_row(result, "FT/ft", "판정 불가", "체제", name="체제 한 측만 기본 통과 → 판정 불가(체제)")
    pilot = judge_case(module, work, "regime_one_side_pilot", {"FT": one_side}, mode="pilot")
    regime = pilot["scenes"]["FT"]["regime"]
    check("시운전 체제 A 기본·B 미달·종합 미달", regime["A"]["basic"] and not regime["B"]["basic"] and not regime["basic"] and not regime["strong"], f"{regime}")
    weak = judge_case(module, work, "regime_basic_only", {"FT": {"model": {"gpu": {"A": 4.4, "B": 4.2}}}}, mode="pilot")["scenes"]["FT"]["regime"]
    check("시운전 체제 기본 통과·강 미달(중앙값 1.2)", weak["basic"] and not weak["strong"], f"{weak['A']['median']} {weak['B']['median']}")
    bad_round_four = lambda round_number, position: 2.0 if round_number == 4 else 7.5
    late = {"rounds": 5, "model": {"frame": {"A": lambda round_number, position: 11.2 if (round_number, position) == (1, 1) else 10.5, "B": 10.0}, "gpu": {"A": 8.0, "B": bad_round_four}}}
    result = judge_case(module, work, "regime_five_rounds", {"FT": late})
    row = expect_row(result, "FT/ft", "판정 불가", "체제", name="체제 5라운드 행은 5라운드 블록으로 재확인 → 판정 불가(체제)")
    check("체제 재확인 라운드 1~5·구간은 5라운드 통과", row["regime"]["rounds"] == [1, 2, 3, 4, 5] and row["rounds"] == 5 and row["interval"]["state"] == "통과", f"{row['regime']['rounds']} {row['rounds']}")
    early = {"rounds": 5, "model": {"gpu": {"A": 8.0, "B": bad_round_four}}}
    result = judge_case(module, work, "regime_three_rounds", {"FT": early})
    row = expect_row(result, "FT/ft", "통과", name="체제 3라운드 판정 행은 라운드 1~3 블록만(4라운드 결함 무관)")
    check("체제 재확인 라운드 1~3", row["regime"]["rounds"] == [1, 2, 3], f"{row['regime']['rounds']}")


def check_backend_cases(module, work):
    gpu = {"backend": "GPU", "grid": {LABEL: 512}, "rows": [("ft", 1.0), ("ng", 0.5)]}
    result = judge_case(module, work, "backend_gpu", {"GS": gpu})
    expect_row(result, "GS/ft", "통과", name="백엔드 GPU·A Solver >0 통과")
    row = expect_row(result, "GS/ng", "통과", name="비물 ng(GPU/TDFluidGpuSolver A p50 0.3 ≤ 0.5) 통과")
    check("ng 구간 = A p50", row["interval"]["lower"] == 0.3 and row["interval"]["upper"] == 0.3 and row["p95_record"]["median"] == 0.3, f"{row['interval']} {row['p95_record']}")
    result = judge_case(module, work, "backend_cpu_absent", {"CS": {}})
    expect_row(result, "CS/ft", "통과", name="백엔드 CPU·A Solver 열 없음 통과")
    water = {"water_layer": True, "actor_backend": {LABEL: "TDFluidSolverBackend.GPU"}, "variables": {"TD.FluidFX.WaterGpu": 0.0}, "grid": {LABEL: 192}, "rows": [("gt", 3.15)]}
    result = judge_case(module, work, "backend_water_layer", {"WA": water})
    expect_row(result, "WA/gt", "통과", name="백엔드 물 층·설정 Gpu·WaterGpu 0 → 실효 CPU 통과")
    result = judge_case(module, work, "backend_cpu_solver", {"CS": {"mutate": lambda columns, side, round_number, position: columns.update({SOLVER: 0.2}) if side == "A" else None}})
    expect_row(result, "CS/ft", "판정 불가", "백엔드 증거", name="백엔드 CPU 선언·A Solver p50 >0 → 판정 불가(백엔드 증거)")
    result = judge_case(module, work, "backend_actor_cpu", {"GS": {**gpu, "actor_backend": {LABEL: "TDFluidSolverBackend.CPU"}}})
    expect_row(result, "GS/ft", "판정 불가", "백엔드 증거", name="백엔드 GPU 선언·대상 설정 Cpu → 판정 불가(백엔드 증거)")
    console_scene = {"toggle": "C", "backend": "GPU", "grid": {LABEL: 512}, "console": {"TD.FluidFX.MassInput": {"A": -1, "B": 0}}, "evidence_columns": [SPLAT],
                     "model": {"solver": {"A": None, "B": None}}, "rows": [("gt", 1.0)]}
    result = judge_case(module, work, "backend_c_scene", {"IN": console_scene})
    expect_row(result, "IN/gt", "판정 불가", "백엔드 증거", name="C 장면(대상 라이브 유체) GPU 선언·A Solver 없음 → 판정 불가(백엔드 증거)")
    result = judge_case(module, work, "backend_auto", {"GS": {**gpu, "actor_backend": {LABEL: "TDFluidSolverBackend.AUTO"}}})
    expect_row(result, "GS/ft", "통과", name="백엔드 설정 Auto 통과(B4 (i))")


def check_readback_cases(module, work):
    cloud = {"toggle": "X", "targets": [], "backend": None, "x_labels": ["TDGen_CloudLayers"], "variables": {"TD.FluidFX.CloudSetup": -1.0},
             "setup": [{"action": "console", "commands": ["TD.FluidFX.CloudSetup 6", "TD.FluidFX.CloudViewSampleScale 2"], "restore": True},
                       {"action": "camera", "target_label": "TDGen_CloudCam_Volcano"}], "rows": [("ft", 2.5)]}
    result = judge_case(module, work, "readback_cl1", {"CL1": cloud})
    expect_row(result, "CL1/ft", "통과", name="되읽기 CL1 CloudSetup 6·conditions -1 통과(⑦)")
    result = judge_case(module, work, "readback_cl1_bad", {"CL1": {**cloud, "setup_readback": {"TD.FluidFX.CloudSetup": 5.0}}})
    expect_row(result, "CL1/ft", "판정 불가", "설정 되읽기", name="되읽기 setup CloudSetup 5 ≠ 6 → 판정 불가(설정 되읽기)")
    wetness = {"toggle": "C", "aw": "both", "grid": {"TDGen_CoastStageWaterSea": 192}, "targets": ["TDGen_CoastStageWaterSea"], "water_layer": True,
               "console": {"TD.FluidFX.WaterWetness": {"A": 1, "B": 0}}, "evidence_columns": list(WETNESS_COLUMNS), "rows": [("gt", 0.15)], "model": {"game": {"A": 3.1, "B": 3.0}}}
    result = judge_case(module, work, "readback_toggle_ok", {"WA3": wetness})
    expect_row(result, "WA3/gt", "통과", name="C 토글 되읽기 = 선언·후보 하나(GameThread) 통과")
    bad_toggle = lambda side, steps: [{"action": "console", "result": {"readback": {"TD.FluidFX.WaterWetness": 1.0}}}]
    result = judge_case(module, work, "readback_toggle_bad", {"WA3": {**wetness, "side_steps": bad_toggle}})
    expect_row(result, "WA3/gt", "판정 불가", "설정 되읽기", name="C 토글 B 되읽기 1 ≠ 선언 0 → 판정 불가(설정 되읽기)")


def check_step_count_cases(module, work):
    base = {"toggle": "L", "parts": {LABEL: ["volumes"]}}
    cases = (("l_a_zero", {"step_count": lambda side, round_number, position: 0}, "판정 불가", "토글 증거", "L A측 StepCount 0 → 판정 불가(토글 증거)"),
             ("l_cap", {"step_count": lambda side, round_number, position: 2 if side == "A" else 0}, "판정 불가", "토글 증거", "L A측 StepCount 2 > awake_step_count 1 → 판정 불가(토글 증거)"),
             ("l_a_missing", {"step_count": lambda side, round_number, position: None if side == "A" else 0}, "판정 불가", "토글 증거", "L A측 StepCount 열 없음 → 판정 불가(토글 증거)"),
             ("l_b_missing", {"step_count": lambda side, round_number, position: 1 if side == "A" else None}, "통과", None, "L B측 StepCount 열 없음 = 0 통과"))
    for case_id, extra, verdict, reason, name in cases:
        result = judge_case(module, work, case_id, {"LS": {**base, **extra}})
        expect_row(result, "LS/ft", verdict, reason, name=name)
    result = judge_case(module, work, "lb_both", {"FI5a": {"toggle": "Lb", "parts": {LABEL: ["embers"]}}})
    expect_row(result, "FI5a/ft", "통과", name="Lb aw both StepCount 두 측 1·embers 가시 통과")
    result = judge_case(module, work, "lb_part_bad", {"FI5a": {"toggle": "Lb", "parts": {LABEL: ["embers"]},
                                                                "actors": lambda side, actors: [{**actor, "visible": {"cards": True, "embers": True}} for actor in actors]}})
    expect_row(result, "FI5a/ft", "판정 불가", "토글 증거", name="Lb B측 embers 보임 → 판정 불가(토글 증거)")
    result = judge_case(module, work, "c_cap", {"IN": {"toggle": "C", "backend": "GPU", "grid": {LABEL: 512}, "console": {"TD.FluidFX.MassInput": {"A": -1, "B": 0}},
                                                        "evidence_columns": [SPLAT], "rows": [("gt", 1.0)], "step_count": lambda side, round_number, position: 2 if side == "B" else 1}})
    expect_row(result, "IN/gt", "판정 불가", "토글 증거", name="StepCount 상한 C 장면 B측 2 > 1 → 판정 불가(토글 증거)")
    result = judge_case(module, work, "x_cap", {"CL2": {"toggle": "X", "targets": [], "backend": None, "x_labels": ["TDGen_CloudLayers"], "rows": [("ft", 3.1)],
                                                        "step_count": lambda side, round_number, position: 1 if side == "A" else None}})
    expect_row(result, "CL2/ft", "판정 불가", "토글 증거", name="StepCount 상한 X 장면 A측 1 > 0 → 판정 불가(토글 증거)")
    result = judge_case(module, work, "target_missing", {"LS": {**base, "actors": lambda side, actors: [] if side == "B" else actors}})
    expect_row(result, "LS/ft", "판정 불가", "대상 기록", name="대상 기록 없는 블록 → 판정 불가(대상 기록)")
    asleep = lambda side, actors: [{**actor, "awake": False} for actor in actors] if side == "A" else actors
    result = judge_case(module, work, "awake_record", {"FI5b": {"toggle": "Xn", "targets": ["TDGen_EmberB"], "actors": asleep}})
    expect_row(result, "FI5b/ft", "판정 불가", "토글 증거", name="aw both A측 대상 기록 awake 거짓 → 판정 불가(토글 증거, aw별 awake)")
    clock = lambda side, round_number, position: "discarded_clock" if position == 1 else "ok"
    result = judge_case(module, work, "clock", {"LS": {**base, "block_status": clock}})
    expect_row(result, "LS/ft", "판정 불가", "클럭", name="클럭 폐기 쌍 3/6 > 1/3 → 판정 불가(클럭, PR-7)")
    fire = {"toggle": "L", "targets": ["TDGen_CampfireFire", "TDGen_CampfireSmoke"], "grid": {"TDGen_CampfireFire": 128, "TDGen_CampfireSmoke": 96},
            "parts": {"TDGen_CampfireFire": ["cards"]}, "light": True, "light_labels": ["TDGen_CampfireSmoke"],
            "actors": lambda side, actors: [{**actor, "lights_visible": []} for actor in actors]}
    result = judge_case(module, work, "light_zero", {"FI1": fire})
    expect_row(result, "FI1/ft", "판정 불가", "토글 증거", name="광원 0개(lights_visible []) → 판정 불가(토글 증거)")


def check_console_evidence_cases(module, work):
    mass = {"toggle": "C", "backend": "GPU", "grid": {LABEL: 512}, "console": {"TD.FluidFX.MassInput": {"A": -1, "B": 0}}, "evidence_columns": [SPLAT, DATA_POINTS],
            "rows": [("ft", 1.0), ("gt", 1.0)]}
    result = judge_case(module, work, "c_both_pass", {"IN1": mass})
    expect_row(result, "IN1/gt", "통과", name="C aw both·B측 증거 열 없음 = 0 → 통과(IN1 DataChannelPoints, F1)")
    b_positive = lambda columns, side, round_number, position: columns.update({SPLAT: 2.0}) if side == "B" else None
    result = judge_case(module, work, "c_b_positive", {"IN1": {**mass, "mutate": b_positive}})
    expect_row(result, "IN1/gt", "판정 불가", "토글 증거", name="C B측 증거 >0 → 판정 불가(토글 증거)")
    a_zero = lambda columns, side, round_number, position: columns.update({SPLAT: 0.0}) if side == "A" else None
    result = judge_case(module, work, "c_a_zero", {"IN1": {**mass, "mutate": a_zero}})
    expect_row(result, "IN1/gt", "판정 불가", "토글 증거", name="C A측 증거 p50 0 → 판정 불가(토글 증거)")
    a_missing = lambda columns, side, round_number, position: columns.pop(DATA_POINTS, None)
    result = judge_case(module, work, "c_a_missing", {"IN1": {**mass, "mutate": a_missing}})
    expect_row(result, "IN1/gt", "측정 불가", "열 없음", name="C A측 후보 전부 없음 → 측정 불가(열 없음, E4)")
    def split_wetness(columns, side, round_number, position):
        columns.pop(WETNESS_COLUMNS[0], None)
        if side == "A":
            columns[WETNESS_COLUMNS[0]] = [0.4] * 4 + [0.0] * (FRAMES - 4)
            columns[WETNESS_COLUMNS[1]] = [0.0] * 4 + [0.4] * 4 + [0.0] * (FRAMES - 8)
    wetness = {"toggle": "C", "targets": ["TDGen_CoastStageWaterSea"], "grid": {"TDGen_CoastStageWaterSea": 192}, "water_layer": True,
               "console": {"TD.FluidFX.WaterWetness": {"A": 1, "B": 0}}, "evidence_columns": list(WETNESS_COLUMNS), "rows": [("gt", 0.5)], "mutate": split_wetness}
    result = judge_case(module, work, "c_two_candidates", {"WA3": wetness}, mode="pilot")
    entry = result["scenes"]["WA3"]["evidence"]["evidence_columns"]["WaterWetness"]
    check("C 두 후보(GameThread·AllWorkers) 프레임 합 p50 >0(각각 p50 0)", all(item["p50"] == 0.4 for item in entry["a_p50"]) and not entry["problems"], f"{entry['a_p50'][:2]} {entry['problems']}")
    result = judge_case(module, work, "c_two_candidates_judge", {"WA3": wetness})
    expect_row(result, "WA3/gt", "통과", name="C 두 후보 합(S9) 통과")
    outside = lambda columns, side, round_number, position: columns.update({"TDFluid/RenderThread/MassInputSplatCalls": 1.0}) if side == "A" else None
    run_dir, table_path = build_case(work, "c_outside", {"IN1": {**mass, "mutate": outside}})
    output = work / "out" / "c_outside.json"
    completed = run_cli("judge", run_dir, "--budget-table", table_path, "--out", output)
    saved = json.loads(output.read_text(encoding="utf-8")) if output.is_file() else {"rows": {}}
    row = saved["rows"].get("IN1/gt") or {}
    check("C 목록 밖 같은 끝 이름 → 판정 불가(열 결함: <이름>)·종료 0(E8)", completed.returncode == 0 and row.get("verdict") == "판정 불가"
          and row.get("reason") == "열 결함: TDFluid/RenderThread/MassInputSplatCalls", f"종료 {completed.returncode} {row.get('verdict')}({row.get('reason')})")
    def outside_only(columns, side, round_number, position):
        columns.pop(SPLAT, None)
        if side == "A":
            columns["Other/MassInputSplatCalls"] = 1.0
    result = judge_case(module, work, "c_outside_only", {"IN1": {**mass, "mutate": outside_only}})
    expect_row(result, "IN1/gt", "판정 불가", "열 결함: Other/MassInputSplatCalls", name="C 후보 없음 + 목록 밖 같은 끝 이름 → 열 결함이 열 없음보다 우선(F2)")
    check("C 열 결함 행에 열 없음 사유도 남김", [item["reason"] for item in result["rows"]["IN1/gt"]["reasons"]][:2] == ["열 결함: Other/MassInputSplatCalls", "열 없음"],
          f"{[item['reason'] for item in result['rows']['IN1/gt']['reasons']]}")


def check_actor_cases(module, work):
    result = judge_case(module, work, "x_pass", {"CL1": {"toggle": "X", "targets": [], "backend": None, "x_labels": ["TDGen_CloudLayers"], "rows": [("ft", 2.5)]}})
    expect_row(result, "CL1/ft", "통과", name="X 숨김 수 B = matched·A 0 통과")
    wrong_key = lambda side, steps: [{"action": "actor", "result": {"labels": ["TDGen_CloudLayers"], "matched": 1, "ns_inactive": 1 if side == "B" else 0}}]
    result = judge_case(module, work, "x_key", {"CL1": {"toggle": "X", "targets": [], "backend": None, "x_labels": ["TDGen_CloudLayers"], "rows": [("ft", 2.5)], "side_steps": wrong_key}})
    expect_row(result, "CL1/ft", "판정 불가", "토글 증거", name="X 선언 키 hidden 없음(ns_inactive만) → 판정 불가(토글 증거)")
    zero_match = lambda side, steps: [{"action": "actor", "result": {"labels": ["TDGen_EmberB"], "matched": 0, "ns_inactive": 0}}]
    result = judge_case(module, work, "xn_zero", {"FI5b": {"toggle": "Xn", "targets": ["TDGen_EmberB"], "side_steps": zero_match}})
    expect_row(result, "FI5b/ft", "판정 불가", "토글 증거", name="Xn matched 0 → 판정 불가(토글 증거)")
    vortex = {"toggle": "Ls+X", "targets": ["TDGen_HvolVortexA", "TDGen_HvolVortexB"], "grid": {"TDGen_HvolVortexA": 128, "TDGen_HvolVortexB": 128}, "rows": [("ft", 6.0)]}
    result = judge_case(module, work, "composite_pass", {"HV2": vortex})
    expect_row(result, "HV2/ft", "통과", name="복합 Ls+X ①·③ 통과")
    result = judge_case(module, work, "composite_step", {"HV2": {**vortex, "step_count": lambda side, round_number, position: 2 if side == "A" else 1}})
    expect_row(result, "HV2/ft", "판정 불가", "토글 증거", name="복합 Ls+X B측 StepCount 1 → 판정 불가(토글 증거, ①)")
    def awake_hidden(side, steps):
        return [step if step["action"] != "actor" else {"action": "actor", "result": {**step["result"], "hidden": 2}} for step in steps]
    result = judge_case(module, work, "composite_actor", {"HV2": {**vortex, "side_steps": awake_hidden}})
    expect_row(result, "HV2/ft", "판정 불가", "토글 증거", name="복합 Ls+X A측 숨김 2 → 판정 불가(토글 증거, ③)")


def check_render_thread_cases(module, work):
    result = judge_case(module, work, "rt_wait", {"RT": {"rows": [("rt", 0.1)]}})
    row = expect_row(result, "RT/rt", "통과", name="rt EventWait 제외 RTSUM d 0.05 통과")
    check("rt 구간 0.05(EventWait 포함이면 49)·rt_control 0.1 기록(E9)", row["interval"]["upper"] == 0.05 and row["rt_control"]["median"] == 0.1 and row["d_prime"] is None,
          f"{row['interval']} {row['rt_control']}")
    a_only = lambda columns, side, round_number, position: columns.update({"Exclusive/RenderThread/WaterOnly": 0.03}) if side == "A" else None
    result = judge_case(module, work, "rt_b_missing", {"RT": {"rows": [("rt", 0.1)], "mutate": a_only}})
    row = expect_row(result, "RT/rt", "통과", name="rt B측 없는 열 = 0(쌍 유지)")
    check("rt B측 없는 열 0 → d 0.08", row["interval"]["upper"] == 0.08 and row["pairs"] == 6, f"{row['interval']} 쌍 {row['pairs']}")
    short = lambda side, round_number, position: 1 if side == "B" else 0
    result = judge_case(module, work, "rt_short", {"RT": {"rows": [("rt", 0.1)], "short_columns": short}})
    row = expect_row(result, "RT/rt", "미달", name="rt 짧은 행 칸 = 0(B측 Other 칸 빠짐 → B RTSUM 0.4)")
    check("rt 짧은 칸 0 → d 1.65", row["interval"]["lower"] == 1.65, f"{row['interval']}")
    def no_prefix(columns, side, round_number, position):
        for name in [name for name in columns if name.startswith("Exclusive/RenderThread/")]:
            columns.pop(name)
    result = judge_case(module, work, "rt_no_prefix", {"RT": {"rows": [("rt", 0.1), ("gt", 1.0)], "mutate": no_prefix}})
    expect_row(result, "RT/rt", "판정 불가", "열 결함 의심(PR-4)", name="rt 접두어 열 없음(RenderThreadTime만) → 판정 불가(열 결함 의심, AM6 K5)")
    expect_row(result, "RT/gt", "통과", name="rt 접두어 결함은 rt 행만")
    pilot = judge_case(module, work, "rt_no_prefix_pilot", {"RT": {"rows": [("rt", 0.1)], "mutate": no_prefix}}, mode="pilot")
    defects = pilot["scenes"]["RT"]["header_defects"]
    check("시운전 header_defects에 Exclusive/RenderThread/*(RenderThreadTime 관측)", defects and defects[0]["column"] == "Exclusive/RenderThread/*" and "RenderThreadTime" in defects[0]["observed"], f"{defects[:1]}")
    def frame_alias(columns, side, round_number, position):
        columns["Misc/FrameTime"] = columns.pop("FrameTime")
    result = judge_case(module, work, "core_alias", {"FT": {"rows": [("ft", 1.0), ("gt", 1.0)], "mutate": frame_alias}})
    expect_row(result, "FT/ft", "판정 불가", "열 결함 의심(PR-4)", name="핵심 열 FrameTime 없음·Misc/FrameTime 있음 → 판정 불가(열 결함 의심(PR-4))")
    expect_row(result, "FT/gt", "통과", name="FrameTime 결함은 gt 행에 영향 없음")
    def drop_game(columns, side, round_number, position):
        columns.pop("GameThreadTime")
    result = judge_case(module, work, "core_missing", {"FT": {"rows": [("gt", 1.0)], "mutate": drop_game}})
    expect_row(result, "FT/gt", "측정 불가", "열 없음", name="핵심 열 GameThreadTime 같은 통계도 없음 → 측정 불가(열 없음)")


def check_record_cases(module, work):
    varying = {"model": {"frame": {"A": [10.5] * (FRAMES - 1) + [12.5], "B": 10.0}}, "rows": [("ft", 1.0)]}
    result = judge_case(module, work, "p95", {"FT": varying})
    row = expect_row(result, "FT/ft", "통과", name="p95 기록은 판정에 안 씀")
    check("ft 쌍 p95 차분 기록 1.4(p50 차분 0.5)", row["p95_record"]["median"] == 1.4 and row["interval"]["upper"] == 0.5, f"{row['p95_record']} {row['interval']}")
    result = judge_case(module, work, "judge_keys", {"FT": {"rows": [("ft", 1.0)]}})
    row = row_of(result, "FT/ft")
    keys = ("row", "ledger_row", "scene", "metric", "verdict", "reason", "interval", "d", "d_prime", "sp", "viewport", "rounds", "pairs", "p95_record", "backend", "limit_ms", "rt_control")
    check("judge 행 출력 키(V3 T2)", all(key in row for key in keys) and row["ledger_row"] == "FT/ft" and row["rounds"] == 3 and row["pairs"] == 6, f"빠짐 {[key for key in keys if key not in row]}")


def check_cli_cases(module, work):
    run_dir, table_path = build_case(work, "cli", {"FT": {"rows": [("ft", 1.0), ("rt", 0.1)]}})
    reference = run_cli("judge", run_dir, "--budget-table", table_path, "--water-ref-ms", "0.5")
    check("reference null 표 + --water-ref-ms → 종료 2", reference.returncode == 2, f"종료 {reference.returncode}")
    no_table = run_cli("judge", run_dir)
    check("P 계획 + --budget-table 없음 → 종료 2", no_table.returncode == 2, f"종료 {no_table.returncode} {no_table.stderr[-120:]!r}")
    out_dir = work / "out"
    output = out_dir / "judge.json"
    completed = run_cli("judge", run_dir, "--budget-table", table_path, "--out", output)
    text = output.read_text(encoding="utf-8") if output.is_file() else ""
    saved = json.loads(text) if text else {}
    table_sha = hashlib.sha256(table_path.read_bytes()).hexdigest()
    check("--out 새 파일 종료 0·indent 2·끝 줄바꿈 1·BOM 없음", completed.returncode == 0 and text.endswith("}\n") and not text.endswith("\n\n") and text.startswith("{\n  \"mode\"")
          and not output.read_bytes().startswith(b"\xef\xbb\xbf"), f"종료 {completed.returncode} {completed.stderr[-160:]!r}")
    check("judge.json 표 sha256(C3)·run_token·run_dir", saved.get("budget_table", {}).get("sha256") == table_sha and saved.get("run_token") == RUN_TOKEN and saved.get("run_dir") == run_dir.as_posix(),
          f"{saved.get('budget_table')} {saved.get('run_token')}")
    check("--out 쓴 뒤 임시 파일 없음", not [path.name for path in out_dir.iterdir() if ".tmp-" in path.name], f"{sorted(path.name for path in out_dir.iterdir())}")
    check("CLI --out = 모듈 결과", saved == analyze(module, run_dir, table_path), "같음 비교")
    before = output.read_bytes()
    again = run_cli("judge", run_dir, "--budget-table", table_path, "--out", output)
    check("--out 이미 있음 → 종료 2·파일 그대로", again.returncode == 2 and output.read_bytes() == before, f"종료 {again.returncode} {again.stderr[-120:]!r}")
    pilot_output = out_dir / "pilot1-judge.json"
    pilot = run_cli("pilot", run_dir, "--budget-table", table_path, "--out", pilot_output)
    pilot_saved = json.loads(pilot_output.read_text(encoding="utf-8")) if pilot_output.is_file() else {}
    check("pilot --out 종료 0·장면 regime 강", pilot.returncode == 0 and pilot_saved.get("scenes", {}).get("FT", {}).get("regime", {}).get("strong") is True, f"종료 {pilot.returncode} {pilot.stderr[-120:]!r}")
    bad_table = work / "tables" / "bad.json"
    table = json.loads(table_path.read_text(encoding="utf-8"))
    table["scenes"]["FT"]["toggle"] = "Q"
    write_json(bad_table, table)
    broken = run_cli("judge", run_dir, "--budget-table", bad_table, "--out", out_dir / "bad.json")
    check("P 표 오류(토글 Q) → 종료 2·출력 없음", broken.returncode == 2 and not (out_dir / "bad.json").exists(), f"종료 {broken.returncode} {broken.stderr[-120:]!r}")
    ladder = json.loads(table_path.read_text(encoding="utf-8"))
    ladder["rows"] = []
    ladder_path = work / "tables" / "ladder.json"
    write_json(ladder_path, ladder)
    ladder_run = run_cli("pilot", run_dir, "--budget-table", ladder_path)
    check("사다리 표 rows [] 허용(pilot 종료 0)", ladder_run.returncode == 0, f"종료 {ladder_run.returncode} {ladder_run.stderr[-120:]!r}")


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    work = TMP_ROOT / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    work.mkdir(parents=True, exist_ok=True)
    module = load_judge()
    groups = (check_p_mode, check_pixel_cases, check_regime_cases, check_backend_cases, check_readback_cases, check_step_count_cases, check_console_evidence_cases,
              check_actor_cases, check_render_thread_cases, check_record_cases, check_cli_cases)
    try:
        for group in groups:
            try:
                group(module, work)
            except Exception:
                check(f"{group.__name__} 예외 없이 끝남", False, traceback.format_exc()[-800:])
    finally:
        if not KEEP:
            shutil.rmtree(work, ignore_errors=True)
    passed = sum(RESULTS)
    print(f"fixtures_t2: 통과 {passed}/{len(RESULTS)} (판정기 {JUDGE_PATH})")
    return 0 if RESULTS and passed == len(RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())
