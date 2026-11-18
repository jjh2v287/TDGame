"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_gates_s2a.py·fog3d_gates_s2a_add1.py·fog3d_terrain.py·fog3d_terrain_add1.py 와 fog3d_terrain_add2.py 를 import 만 한다
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_gates_s2a_add2.py run --out-dir <폴더> [--only G1,G13,G14,INFO] [--jobs N] [--smoke] [--force] [--candidates R5] / collect --out-dir <폴더>
출력: <폴더>/pieces/<후보>_<판정선>_<장면>.json(+npz), collect 는 <폴더>/results.json·results.md. 끝 코드 0 끝남, 1 실패 조각 있음(같은 명령으로 이어 하기), 2 잠금 지문 다름
상태: 2a 덧붙임 2 판정 실행기(Docs/VolumeFog3D/stage2/prereg-2a-design-add2.md E·F·G절, 덧붙임 1 E절, 본 등록 4절), 2026-11-15 잠금(공식 판정 실행에 쓴 판. 잠근 뒤에는 고치지 않는다)
"""
import os

for _thread_var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_thread_var, "1")

import argparse
import concurrent.futures
import hashlib
import json
import math
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fog3d_gates_s2a as base
import fog3d_gates_s2a_add1 as add1
import fog3d_terrain_add2 as terrain_add2
from fog3d_terrain import TERRAINS, TerrainSim
from fog3d_terrain_add1 import SLOPE_CURB_TERRAIN, TerrainSimR4
from fog3d_terrain_add2 import BLEND_KINDS, TerrainSimR5

ROOT = base.ROOT
ADDENDUM2_PATH = os.path.join(ROOT, "Docs", "VolumeFog3D", "stage2", "prereg-2a-design-add2.md")
ADDENDUM1_PATH = add1.ADDENDUM_PATH
PREREG_PATH = base.PREREG_PATH
LOCKED_ADD1_SHA256 = {"fog3d_terrain_add1.py": "51726afd38cb3776cb898e5dab49b9fb6400f005f1f3cdf9c72ac92e31d949d8",
                      "fog3d_gates_s2a_add1.py": "38792f921b914a061d117f6195beab3f5582b11acd41262c90942b54f5713cf2"}
NEW_CODE = ("fog3d_terrain_add2.py", "fog3d_gates_s2a_add2.py")

R5 = "R5"
R4 = add1.R4
R3 = add1.R3
REF = base.REFERENCE_CANDIDATE
CANDIDATES_ALLOWED = (R5,)
COMPARISON_CANDIDATES = (R3, R4)
THRESHOLDS_ADD2 = dict(G13_RATIO_MIN=0.8, G13_RATIO_MAX=1.25, G13_ZERO=1e-6, G14_RATIO_MIN=0.5, G14_RATIO_MAX=2.0, G14_WIND_FACTOR=1.5,
                       G14_SHARED_WIND_FACTOR=1.05, G14_SHARED_LOG_MARGIN=0.05, INFO16_OVER_R3=2.0)
CLIFF_WALK_POINT = (-20.0, 0.0)
G13_TERRAINS = (SLOPE_CURB_TERRAIN, "S30DROP100")
INFO16_TERRAINS = ("S30CURB45", "S30CURB60", "S30CURB80", "S30CURB130", "S30DROP60")
G14_PATHS = {"along_cliff": ((-20.0, -300.0), (0.0, 1.0)), "cross_x": ((-300.0, 0.0), (1.0, 0.0)),
             "diagonal": ((-300.0 / math.sqrt(2.0), -300.0 / math.sqrt(2.0)), (1.0 / math.sqrt(2.0), 1.0 / math.sqrt(2.0))), "along_fold": ((0.0, -300.0), (0.0, 1.0))}
G14_WALKS = (
    dict(label="가", key="a", terrain=SLOPE_CURB_TERRAIN, path="along_cliff", measure="S", value_band=False, wind_with_r3=True),
    dict(label="나", key="b", terrain="S30DROP100", path="along_cliff", measure="S", value_band=False, wind_with_r3=True),
    dict(label="다", key="c", terrain="FOOT45", path="cross_x", measure="crossing_abs_mean", value_band=True, wind_with_r3=False),
    dict(label="라", key="d", terrain="TOP45", path="cross_x", measure="crossing_abs_mean", value_band=True, wind_with_r3=False),
    dict(label="마", key="e", terrain="FOOT30", path="diagonal", measure="crossing_abs_mean", value_band=True, wind_with_r3=False),
    dict(label="바", key="f", terrain="FOOT45", path="along_fold", measure="S", value_band=True, wind_with_r3=False),
)
SCENE_ADD2 = dict(g13_terrains=G13_TERRAINS, g13_walk_point=CLIFF_WALK_POINT, info16_terrains=INFO16_TERRAINS, g14_paths=G14_PATHS,
                  g14_walks=G14_WALKS, info17_g5=("CURB30_up", "CURB30_down"), info17_g6="ROUGH_k2_60", info17_g9="HILL_follow")
GATE_KIND = dict(add1.GATE_KIND, G4="required", G13="deciding", G14="deciding")
ALL_GATES = add1.ALL_GATES + ("G13", "G14")
PIECE_SIMS = []


def make_sim(cand, terrain, params, **kw):
    if cand in (R5, REF):
        sim = TerrainSimR5(params, terrain, **kw)
        PIECE_SIMS.append(sim)
        return sim
    if cand == R4:
        return TerrainSimR4(params, terrain, **kw)
    return TerrainSim(params, terrain, base.candidate_settings(cand), **kw)


def install():
    terrain_add2.register_terrains()
    for name in G13_TERRAINS + INFO16_TERRAINS:
        base.SCENE["g4_headings"].setdefault(name, base.SCENE["g4_headings"]["CLIFF"])
        base.SCENE["g4_blades"].setdefault(name, base.SCENE["g4_blades"]["CLIFF"])
    base.make_sim = make_sim


install()


def sha256_of(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def locked_check():
    check = add1.locked_check()
    found = dict(check["found"])
    mismatched = dict(check["mismatched"])
    for name, expected in LOCKED_ADD1_SHA256.items():
        found[name] = sha256_of(os.path.join(HERE, name))
        if found[name] != expected:
            mismatched[name] = found[name]
    return dict(ok=not mismatched, mismatched=mismatched, found=found)


def code_fingerprints():
    out = {name: sha256_of(os.path.join(HERE, name)) for name in NEW_CODE}
    out["prereg-2a-design-add2.md"] = sha256_of(ADDENDUM2_PATH)
    out["prereg-2a-design-add1.md"] = sha256_of(ADDENDUM1_PATH)
    out["prereg-2a-design.md"] = sha256_of(PREREG_PATH)
    return out


def running_max_keeping_nan(previous, value):
    if math.isnan(previous) or math.isnan(value):
        return float("nan")
    return max(previous, value)


def piece_g14_walk(spec):
    terrain = TERRAINS[spec["terrain"]]
    sim = base.make_sim(spec["cand"], terrain, base.game_params())
    walker = base.Walker(terrain, spec["start"], spec["heading"], spec["smoke"])
    col0 = base.column(sim)
    run_max = 0.0
    for _ in range(walker.total):
        sim.step(walker.next_inputs())
        run_max = running_max_keeping_nan(run_max, float(sim.max_wind()))
    delta = base.column(sim) - col0
    out = base.band_stats(sim, delta, walker)
    out.update(steps=walker.total, max_wind=float(sim.max_wind()), run_max_wind=run_max, state_hash=sim.state_hash(), finite=base.finite(sim))
    return out


PIECE_RUNNERS = dict(add1.PIECE_RUNNERS, g14_walk=piece_g14_walk)
ESTIMATED_SECONDS = dict(walk=290, walk_tracked=290, g14_walk=290, g1=560, g2=570, g3=1, g4=330, g8=6, g10=92)


def estimated_seconds(s):
    if s["kind"] == "g9":
        return 2700 if s["mode"] == "fixed" else 740
    if s["kind"] == "g6":
        return 1100 * s["hz"] / 60
    return ESTIMATED_SECONDS[s["kind"]]


def g13_spec(cand, terrain, smoke):
    x = CLIFF_WALK_POINT[0]
    return base.spec(cand, "G13", f"{terrain}_a_x{x:g}", "g4", smoke, terrain=terrain, side="a", walk_point=list(CLIFF_WALK_POINT))


def g14_spec(cand, terrain, path, smoke):
    start, heading = G14_PATHS[path]
    return base.spec(cand, "G14", f"{terrain}_{path}", "g14_walk", smoke, terrain=terrain, path=path, start=list(start), heading=list(heading))


def gate_specs(cand, gate, smoke):
    if gate == "G13":
        return [g13_spec(c, t, smoke) for t in G13_TERRAINS for c in (cand,) + COMPARISON_CANDIDATES]
    if gate == "G14":
        out = [g14_spec(c, w["terrain"], w["path"], smoke) for w in G14_WALKS for c in (cand,) + COMPARISON_CANDIDATES]
        return out + [g14_spec(REF, "F0", p, smoke) for p in sorted({w["path"] for w in G14_WALKS})]
    if gate in ("G11", "G12"):
        return add1.gate_specs(cand, gate, smoke)
    return base.gate_specs(cand, gate, smoke)


def info_specs(smoke):
    out = [base.walk_spec(R5, t, p, smoke) for t in ("CURB45", "CURB60") for p in ("up", "down")]
    out += [base.walk_spec(REF, "F0", p, smoke) for p in ("up", "down", "contour")]
    out += [base.walk_spec(R5, t, p, smoke) for t in ("S10", "S45") for p in ("up", "down")]
    out += [base.walk_spec(R5, t, "contour", smoke) for t in ("S10", "S20", "S30", "S45")]
    out += base.gate_specs(R5, "G6", smoke)
    out.append(add1.g4_spec(R5, "CLIFF", add1.SCENE_ADD1["info11_walk_point"], smoke))
    out.append(add1.g6_spec(R5, *add1.SCENE_ADD1["info12_variant"], smoke))
    out += [g13_spec(c, t, smoke) for t in INFO16_TERRAINS for c in (R3, R4, R5)]
    return out


def collected_blend_counts():
    sims = [s for s in PIECE_SIMS if isinstance(s, TerrainSimR5)]
    if not sims:
        return None
    return {k: int(sum(s.blend_counts[k] for s in sims)) for k in BLEND_KINDS}


def run_piece(item):
    s, out_dir = item
    path, npz = base.piece_paths(out_dir, s["id"])
    s = dict(s)
    if s["kind"] == "g9" or s.get("save_fields"):
        s["npz"] = npz
    PIECE_SIMS.clear()
    t0 = time.time()
    try:
        values = PIECE_RUNNERS[s["kind"]](s)
        error = None
        counts = collected_blend_counts()
        if counts is not None:
            values["blend_counts"] = counts
    except Exception:
        values, error = None, traceback.format_exc()
    PIECE_SIMS.clear()
    res = dict(spec={k: v for k, v in s.items() if k != "npz"}, values=values, error=error, elapsed_sec=round(time.time() - t0, 2),
               code=code_fingerprints(), locked=locked_check()["ok"],
               model=dict(R5="TerrainSimR5", REF="TerrainSimR5(F0 평지 값)", R4="TerrainSimR4", R3="TerrainSim(R3)"))
    if error is None:
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(res, fh, ensure_ascii=False, indent=1)
        os.replace(tmp, path)
    return s["id"], error, res["elapsed_sec"]


def execute(specs, out_dir, jobs, force):
    os.makedirs(os.path.join(out_dir, "pieces"), exist_ok=True)
    unique = {}
    for s in specs:
        unique.setdefault(s["id"], s)
    todo = [s for s in unique.values() if force or not os.path.exists(base.piece_paths(out_dir, s["id"])[0])]
    todo.sort(key=lambda s: -estimated_seconds(s))
    if not todo:
        return []
    total = sum(estimated_seconds(s) for s in todo)
    print(f"[run] 조각 {len(todo)}개, 프로세스 {jobs}개, 어림(줄이지 않은 길이) 합 {round(total / 60)}분", flush=True)
    failures = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run_piece, (s, out_dir)) for s in todo]
        for future in concurrent.futures.as_completed(futures):
            piece_id, error, elapsed = future.result()
            print(f"[run] {piece_id} {'실패' if error else '끝'} {elapsed}s", flush=True)
            if error:
                failures.append((piece_id, error))
                print(error, flush=True)
    return failures


def requested_specs(args):
    gates = [g for g in (args.only or ALL_GATES) if g != "INFO"]
    specs = []
    for cand in args.candidates:
        for g in gates:
            specs += gate_specs(cand, g, args.smoke)
    if not args.only or "INFO" in args.only:
        specs += info_specs(args.smoke)
    return specs


def cmd_run(args):
    check = locked_check()
    if not check["ok"]:
        print(f"[run] 잠긴 파일 지문 불일치: {check['mismatched']}", flush=True)
        return 2
    t0 = time.time()
    failures = execute(requested_specs(args), args.out_dir, args.jobs, args.force)
    print(f"[run] 끝 {round(time.time() - t0, 1)}s, 실패 조각 {len(failures)}", flush=True)
    return 1 if failures else 0


def g13_id(cand, terrain):
    return f"{cand}_G13_{terrain}_a_x{CLIFF_WALK_POINT[0]:g}"


def opposite_max(pieces, cand, terrain):
    return (base.values_of(pieces, g13_id(cand, terrain)) or {}).get("opposite_max")


def finite_number(x):
    return isinstance(x, (int, float)) and math.isfinite(x)


def eval_g13(pieces, cand):
    lo, hi, zero = THRESHOLDS_ADD2["G13_RATIO_MIN"], THRESHOLDS_ADD2["G13_RATIO_MAX"], THRESHOLDS_ADD2["G13_ZERO"]
    runs = {}
    for t in G13_TERRAINS:
        vals = base.need(pieces, [g13_id(cand, t), g13_id(R3, t)])
        if vals is None:
            return None
        v, r3 = vals[0]["opposite_max"], vals[1]["opposite_max"]
        if not (finite_number(v) and finite_number(r3)):
            runs[t] = dict(value=v, r3=r3, ratio=None, rule="값이 NaN·무한이면 실패", passed=False)
        elif r3 <= zero:
            runs[t] = dict(value=v, r3=r3, ratio=None, rule="R3 ≤ 1e-6 → R5 ≤ 1e-6", passed=bool(v <= zero))
        else:
            ratio = v / r3
            runs[t] = dict(value=v, r3=r3, ratio=ratio, rule="0.8 ≤ R5÷R3 ≤ 1.25", passed=bool(lo <= ratio <= hi))
    return base.gate_result({t: r["ratio"] for t, r in runs.items()}, dict(ratio=[lo, hi], zero=zero), bool(all(r["passed"] for r in runs.values())),
                            runs=runs)


def g14_id(cand, terrain, path):
    return f"{cand}_G14_{terrain}_{path}"


def band_is_empty(values, measure):
    cells = values.get("crossing_cells") if measure == "crossing_abs_mean" else values.get("band_cells")
    return not cells


def absolute_conditions(v, f, big_m, walk):
    lo, hi, factor = THRESHOLDS_ADD2["G14_RATIO_MIN"], THRESHOLDS_ADD2["G14_RATIO_MAX"], THRESHOLDS_ADD2["G14_WIND_FACTOR"]
    measure = walk["measure"]
    value_ratio = float("nan")
    if not band_is_empty(v, measure) and not band_is_empty(f, measure) and finite_number(f[measure]) and f[measure] > 0:
        value_ratio = v[measure] / f[measure]
    checks = dict(finite=bool(v["finite"]), wind_ok=bool(v["run_max_wind"] <= factor * big_m))
    if walk["value_band"]:
        checks["value_in_band"] = bool(finite_number(value_ratio) and lo <= value_ratio <= hi)
    return dict(value=v[measure], value_over_flat=value_ratio, run_max_wind=v["run_max_wind"], run_max_wind_over_M=add1.ratio_or_nan(v["run_max_wind"], big_m),
                finite=bool(v["finite"]), checks=checks, holds=bool(all(checks.values())))


def log_distance(ratio):
    if not finite_number(ratio) or ratio <= 0:
        return float("nan")
    return abs(math.log(ratio))


def worst_if_unusable(value, usable):
    return value if usable and finite_number(value) else math.inf


def walk_outcome(r5, r4, walk):
    if r5["holds"]:
        return "pass", "R5 가 절대 조건을 지킴"
    if r4["holds"]:
        return "r5_failure", "R4 는 지키는데 R5 가 못 지킴"
    if not r5["finite"]:
        return "r5_failure", "함께 못 지키는데 R5 의 장이 유한하지 않음"
    r4_wind = worst_if_unusable(r4["run_max_wind"], r4["finite"])
    if not r5["run_max_wind"] <= THRESHOLDS_ADD2["G14_SHARED_WIND_FACTOR"] * r4_wind:
        return "r5_failure", "함께 못 지키는데 R5 의 최대 바람 > 1.05 × R4(또는 NaN)"
    if walk["value_band"]:
        d5 = log_distance(r5["value_over_flat"])
        d4 = worst_if_unusable(log_distance(r4["value_over_flat"]), r4["finite"])
        if not finite_number(d5):
            return "r5_failure", "함께 못 지키는데 R5 의 ② 값이 0 이거나 구할 수 없음"
        if not d5 <= d4 + THRESHOLDS_ADD2["G14_SHARED_LOG_MARGIN"]:
            return "r5_failure", "함께 못 지키는데 R5 의 |ln(값÷평지)| > R4 + 0.05"
    return "shared_limit", "R5·R4 함께 가진 한계(R5 가 R4 보다 나쁘지 않음)"


def g14_walk(pieces, cand, walk):
    ids = [g14_id(cand, walk["terrain"], walk["path"]), g14_id(R4, walk["terrain"], walk["path"]), g14_id(REF, "F0", walk["path"])]
    if walk["wind_with_r3"]:
        ids.append(g14_id(R3, walk["terrain"], walk["path"]))
    vals = base.need(pieces, ids)
    if vals is None:
        return None
    v5, v4, f = vals[0], vals[1], vals[2]
    big_m = max(f["run_max_wind"], vals[3]["run_max_wind"]) if walk["wind_with_r3"] else f["run_max_wind"]
    r5, r4 = absolute_conditions(v5, f, big_m, walk), absolute_conditions(v4, f, big_m, walk)
    outcome, why = walk_outcome(r5, r4, walk)
    return dict(label=walk["label"], measure=walk["measure"], value_band_checked=walk["value_band"], value_flat=f[walk["measure"]],
                run_max_wind_flat=f["run_max_wind"], run_max_wind_r3=vals[3]["run_max_wind"] if walk["wind_with_r3"] else None, M=big_m,
                R5=r5, R4=r4, outcome=outcome, outcome_reason=why, passed=outcome != "r5_failure")


def eval_g14(pieces, cand):
    walks = {}
    for walk in G14_WALKS:
        w = g14_walk(pieces, cand, walk)
        if w is None:
            return None
        walks[f"{walk['label']}_{walk['terrain']}_{walk['path']}"] = w
    lo, hi, factor = THRESHOLDS_ADD2["G14_RATIO_MIN"], THRESHOLDS_ADD2["G14_RATIO_MAX"], THRESHOLDS_ADD2["G14_WIND_FACTOR"]
    value = {k: w["outcome"] for k, w in walks.items()}
    return base.gate_result(value, dict(value_over_flat=[lo, hi], run_max_wind_over_M=factor, rule="R5 의 실패가 없으면 통과(덧붙임 2 F절 2)"),
                            bool(all(w["passed"] for w in walks.values())), walks=walks,
                            r4_limit_walks=[k for k, w in walks.items() if not w["R4"]["holds"]],
                            shared_limit_walks=[k for k, w in walks.items() if w["outcome"] == "shared_limit"])


def evaluate(pieces, cand, out_dir):
    table = {"G1": lambda: base.eval_g1(pieces, cand), "G2": lambda: base.eval_g2(pieces, cand), "G3": lambda: base.eval_g3(pieces, cand),
             "G4": lambda: base.eval_g4(pieces, cand), "G5": lambda: base.eval_g5(pieces, cand), "G6": lambda: base.eval_g6(pieces, cand),
             "G7": lambda: base.eval_g7(pieces, cand), "G8": lambda: base.eval_g8(pieces, cand), "G9": lambda: base.eval_g9(pieces, cand, out_dir),
             "G10": lambda: base.eval_g10(pieces, cand), "G11": lambda: add1.eval_g11(pieces, cand), "G12": lambda: add1.eval_g12(pieces, cand),
             "G13": lambda: eval_g13(pieces, cand), "G14": lambda: eval_g14(pieces, cand)}
    out = {}
    for g in ALL_GATES:
        r = table[g]()
        if r is not None:
            r["kind"] = GATE_KIND[g]
            r["applies"] = True
        out[g] = r
    return out


def candidate_status(gates):
    failed = [dict(gate=g, kind=GATE_KIND[g]) for g in ALL_GATES if gates.get(g) and not gates[g]["passed"]]
    missing = [g for g in ALL_GATES if not gates.get(g)]
    if failed:
        return dict(status="failed", failed=failed, missing=missing)
    if missing:
        return dict(status="incomplete", failed=[], missing=missing)
    return dict(status="all_passed", failed=[], missing=[])


def known_limit_walks(gates, chosen):
    g14 = gates.get("G14")
    if g14 is None or chosen not in (R4, R5):
        return None
    return g14["detail"]["r4_limit_walks"] if chosen == R4 else g14["detail"]["shared_limit_walks"]


def select(status, gates):
    if status["status"] == "all_passed":
        chosen, reason, extra = R5, "R5 가 2A-G1 ~ 2A-G13 을 모두 만족하고 2A-G14 에 R5 의 실패가 없음(덧붙임 2 F절 1)", {}
    elif status["status"] == "failed":
        names = ", ".join(f"{f['gate']}({'필수' if f['kind'] == 'required' else '가름'})" for f in status["failed"])
        required = [f["gate"] for f in status["failed"] if f["kind"] == "required"]
        note = f" 필수 선 실패 {', '.join(required)}: F절 5(구현 실수인지 줄 번호로 확인) 대상." if required else ""
        missing = f" 빠진 선: {', '.join(status['missing'])}." if status["missing"] else ""
        chosen = R4
        reason = f"R5 를 고르지 않음, 덧붙임 1 의 R4 를 그대로 구현(정보 ⑭는 알려진 한계, 덧붙임 2 F절 3). 떨어진 선: {names}.{note}{missing}"
        extra = dict(failed=status["failed"])
    else:
        chosen, reason, extra = "incomplete", f"조각이 빠져 판정할 수 없음: {', '.join(status['missing'])}", {}
    meaning = {R4: "2A-G14 에서 R4 가 절대 조건을 못 지킨 걷기 전부", R5: "2A-G14 에서 '함께 가진 한계'인 걷기"}.get(chosen, "고르지 못해 정하지 않음")
    return dict(chosen=chosen, reason=reason, known_limit_walks=known_limit_walks(gates, chosen),
                known_limit_walks_meaning=f"고른 설계의 알려진 한계(F절 4): {meaning}", **extra)


def ratio_or_none(a, b):
    return a / b if a is not None and b is not None and b > 0 else None


def info_block(pieces):
    info = {"1_curb": {t: base.curb_ratios(pieces, R5, t) for t in ("CURB45", "CURB60")}}
    flat = {p: base.values_of(pieces, f"{REF}_WALK_F0_{p}") for p in ("up", "down", "contour")}
    ratios = {}
    for t in ("S10", "S20", "S30", "S45"):
        for p in ("up", "down", "contour"):
            v = base.values_of(pieces, f"{R5}_WALK_{t}_{p}")
            if v is not None and flat[p] is not None and flat[p]["S"] > 0:
                ratios[f"{t}_{p}"] = dict(S=v["S"], over_flat=v["S"] / flat[p]["S"], signed_sum=v["signed_sum"])
    info["2_slope_walks"] = dict(ratios=ratios, A={t: base.asymmetry(pieces, R5, t) for t in ("S10", "S20", "S30", "S45")})
    world = {}
    for t, k, hz in base.g6_variants() + [add1.SCENE_ADD1["info12_variant"]]:
        v, f = base.values_of(pieces, f"{R5}_G6_{t}_{k}_{hz}"), base.values_of(pieces, f"{REF}_G6_F0_{k}_{hz}")
        if v and f and f["world_wind_max"] > 0:
            world[f"{t}_{k}_{hz}"] = dict(world_wind_max=v["world_wind_max"], over_flat=v["world_wind_max"] / f["world_wind_max"])
    info["7_world_wind"] = world
    info["9_g6_original_limit"] = {f"{t}_{k}_{hz}": (base.values_of(pieces, f"{R5}_G6_{t}_{k}_{hz}") or {}).get("info9_original")
                                   for t, k, hz in base.g6_variants()}
    x11 = add1.SCENE_ADD1["info11_walk_point"][0]
    info["11_cliff_x_minus_20_opposite_max"] = (base.values_of(pieces, f"{R5}_G4_CLIFF_a_x{x11:g}") or {}).get("opposite_max")
    t, k, hz = add1.SCENE_ADD1["info12_variant"]
    rough = base.values_of(pieces, f"{R5}_G6_{t}_{k}_{hz}") or {}
    info["12_rough_k3"] = {key: rough.get(key) for key in ("wind_over_limit", "max_wind", "converted_input_speed", "M", "limit",
                                                          "energy_end_over_peak", "finite", "passed", "info9_original")}
    g13_r4 = {}
    for terrain in G13_TERRAINS:
        r3, r4 = opposite_max(pieces, R3, terrain), opposite_max(pieces, R4, terrain)
        g13_r4[terrain] = dict(R4=r4, R3=r3, R4_over_R3=ratio_or_none(r4, r3))
    g14_r3 = {}
    for walk in G14_WALKS:
        if walk["wind_with_r3"]:
            continue
        v = base.values_of(pieces, g14_id(R3, walk["terrain"], walk["path"])) or {}
        g14_r3[f"{walk['label']}_{walk['terrain']}_{walk['path']}"] = dict(measure=walk["measure"], value=v.get(walk["measure"]),
                                                                         run_max_wind=v.get("run_max_wind"), finite=v.get("finite"))
    info["15_comparison"] = dict(G13_R4=g13_r4, G14_R3_c_to_f=g14_r3)
    sweep = {}
    for terrain in INFO16_TERRAINS:
        r3, r4, r5 = (opposite_max(pieces, c, terrain) for c in (R3, R4, R5))
        limit = None
        if r3 is not None and r4 is not None and r5 is not None:
            limit = bool(r5 > r4 or r5 > THRESHOLDS_ADD2["INFO16_OVER_R3"] * r3)
        sweep[terrain] = dict(R3=r3, R4=r4, R5=r5, R4_over_R3=ratio_or_none(r4, r3), R5_over_R3=ratio_or_none(r5, r3), r5_known_limit=limit)
    info["16_curb_height_sweep_opposite_max"] = sweep
    blend_ids = [f"{R5}_WALK_{s}" for s in SCENE_ADD2["info17_g5"]] + [f"{R5}_G6_{SCENE_ADD2['info17_g6']}", f"{R5}_G9_{SCENE_ADD2['info17_g9']}"]
    blend_ids += [f"{R5}_G11_{t}_{p}" for t, p, _ in add1.SCENE_ADD1["g11_walks"]]
    blend_ids += [g13_id(R5, t) for t in G13_TERRAINS + INFO16_TERRAINS] + [g14_id(R5, w["terrain"], w["path"]) for w in G14_WALKS]
    info["17_blend_counts_c_samples"] = {i.split("_", 1)[1]: (base.values_of(pieces, i) or {}).get("blend_counts") for i in blend_ids}
    info["18_cost_add2"] = dict(per_input="기준 기둥 높이 읽기 1번(m 을 품은 기둥 번호, 창 밖이면 가장자리로 자름)",
                                per_sample="평면 편차 e = Hs − Hr − Scx·(x − rx) − Scy·(y − ry)(곱셈 2번·뺄셈 4번), 몫 g 자르기, "
                                           "0 < g < 1 또는 g = 0 인 표본이 있으면 세계 틀의 band_weight·dwell_amount 한 번 더와 섞기(곱셈 2번)",
                                memory="더 드는 메모리 없음(기둥 높이·면 높이는 R3 가 이미 가짐)")
    return info


def g13_markdown_rows(r):
    return [f"| R5 | G13 · {t} | 장면 | {base.fmt(run)} | {run['rule']} | {run['passed']} |" for t, run in r["detail"]["runs"].items()]


def absolute_summary(r):
    return dict(holds=r["holds"], finite=r["finite"], value_over_flat=r["value_over_flat"], run_max_wind_over_M=r["run_max_wind_over_M"])


def g14_markdown_rows(r):
    rows = []
    for name, w in r["detail"]["walks"].items():
        rule = "유한, 최대 바람 ≤ 1.5 × M" + (", 0.5 ≤ 값÷평지 ≤ 2.0" if w["value_band_checked"] else "")
        rows.append(f"| R5 | G14 · {name} | 걷기(M={base.fmt(w['M'])}) | R5: {base.fmt(absolute_summary(w['R5']))} / R4: {base.fmt(absolute_summary(w['R4']))} "
                    f"→ {w['outcome']}({w['outcome_reason']}) | {rule} | {w['passed']} |")
    return rows


def write_markdown(path, res):
    fmt = base.fmt
    gates = res["candidates"][R5]
    status = res["statuses"][R5]
    lines = ["# 2a 덧붙임 2 판정 결과 (후보 R5)", "", f"- 시운전(단계 줄임): {res['smoke']}", f"- 잠긴 파일 지문 일치: {res['locked']['ok']}",
             f"- 코드·등록 지문: {fmt(res['code'])}", f"- 고른 것: {res['selection']['chosen']} ({res['selection']['reason']})",
             "- 평지 값(REF 조각)은 R5 를 F0 에서 돌린 값이다(덧붙임 2 E절). 2A-G4 는 R5 에서 필수 선이다",
             f"- 한 단계 시간(128칸, 걷기 조각 중앙값): {fmt(res['step_seconds_128'])}초", "",
             "| 후보 | 판정선 | 종류 | 값 | 문턱 | 통과 |", "|---|---|---|---|---|---|"]
    for g in ALL_GATES:
        r = gates.get(g)
        if r is None:
            lines.append(f"| R5 | {g} | {GATE_KIND[g]} | 조각 없음 | - | - |")
            continue
        lines.append(f"| R5 | {g} | {r['kind']} | {fmt(r['value'])} | {fmt(r['threshold'])} | {r['passed']} |")
        if g == "G6":
            lines += [base.g6_markdown_row(R5, name, run) for name, run in r["detail"]["runs"].items()]
        if g == "G11":
            lines += [f"| R5 | G11 · {name} | 걷기 | {fmt({k: w[k] for k in ('measure', 'finite', 'value_over_flat', 'run_max_wind_over_flat')})} | "
                      f"0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | {w['passed']} |" for name, w in r["detail"]["walks"].items()]
        if g == "G12":
            lines += [f"| R5 | G12 · {name} | 값 | {fmt(v)} | ≤ {add1.THRESHOLDS_ADD1['G12_REL_MAX']:g} | {v <= add1.THRESHOLDS_ADD1['G12_REL_MAX']} |"
                      for name, v in r["value"].items()]
        if g == "G13":
            lines += g13_markdown_rows(r)
        if g == "G14":
            lines += g14_markdown_rows(r)
    failed = ", ".join(f"{f['gate']}({'필수' if f['kind'] == 'required' else '가름'})" for f in status["failed"]) or "없음"
    lines += ["", "| 후보 | 상태 | 떨어진 선(종류) | 빠진 선 |", "|---|---|---|---|",
              f"| R5 | {status['status']} | {failed} | {', '.join(status['missing']) or '없음'} |",
              "", f"- 알려진 한계 걷기({res['selection']['known_limit_walks_meaning']}): {fmt(res['selection'].get('known_limit_walks'))}",
              f"- 정보 ⑯ R5 의 알려진 한계 표시(F절 6): {fmt({t: v.get('r5_known_limit') for t, v in res['info']['16_curb_height_sweep_opposite_max'].items()})}"]
    lines += ["", "## 정보", ""] + [f"- {k}: {fmt(v)}" for k, v in res["info"].items()]
    if res["missing_or_failed"]:
        lines += ["", "## 오류 조각", ""] + [f"- {x}" for x in res["missing_or_failed"]]
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


def cmd_collect(args):
    pieces = base.load_pieces(args.out_dir)
    gates = evaluate(pieces, R5, args.out_dir)
    status = candidate_status(gates)
    selection = select(status, gates)
    res = dict(prereg="Docs/VolumeFog3D/stage2/prereg-2a-design.md", addendum1="Docs/VolumeFog3D/stage2/prereg-2a-design-add1.md",
               addendum2="Docs/VolumeFog3D/stage2/prereg-2a-design-add2.md",
               smoke=any(p["spec"].get("smoke") for p in pieces.values()), locked=locked_check(), code=code_fingerprints(),
               thresholds=dict(base.THRESHOLDS, **add1.THRESHOLDS_ADD1, **THRESHOLDS_ADD2),
               scene=base.json_safe(dict(base.SCENE, **add1.SCENE_ADD1, **SCENE_ADD2)),
               candidates={R5: {g: r for g, r in gates.items() if r is not None}}, statuses={R5: status}, selection=selection,
               info=info_block(pieces), step_seconds_128=base.step_time(pieces), pieces=len(pieces),
               missing_or_failed=[k for k, p in pieces.items() if p.get("error")])
    with open(os.path.join(args.out_dir, "results.json"), "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False, indent=1, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))
    write_markdown(os.path.join(args.out_dir, "results.md"), res)
    print(json.dumps(dict(selection=selection, status=status, locked=res["locked"]["ok"]), ensure_ascii=False))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=("run", "collect"))
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--candidates", default=R5)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--smoke", action="store_true")
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args(argv)
    args.only = [g.strip().upper() for g in args.only.split(",") if g.strip()]
    args.candidates = [c.strip().upper() for c in args.candidates.split(",") if c.strip()]
    unknown = [c for c in args.candidates if c not in CANDIDATES_ALLOWED] + [g for g in args.only if g not in ALL_GATES + ("INFO",)]
    if unknown:
        ap.error(f"모르는 후보·판정선: {unknown}")
    args.out_dir = os.path.abspath(args.out_dir)
    return cmd_run(args) if args.command == "run" else cmd_collect(args)


if __name__ == "__main__":
    sys.exit(main())
