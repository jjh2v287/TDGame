"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_gates_s2a.py·fog3d_terrain.py 와 fog3d_terrain_add1.py 를 import 만 한다
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_gates_s2a_add1.py run --out-dir <폴더> [--only G1,G11,G12,INFO] [--jobs N] [--smoke] [--force] [--candidates R4] / collect --out-dir <폴더>
출력: <폴더>/pieces/<후보>_<판정선>_<장면>.json(+npz), collect 는 <폴더>/results.json·results.md. 끝 코드 0 끝남, 1 실패 조각 있음(같은 명령으로 이어 하기), 2 잠금 지문 다름
상태: 2a 덧붙임 1 판정 실행기(Docs/VolumeFog3D/stage2/prereg-2a-design-add1.md E·F·G절, 본 등록 4절), 2026-11-15 잠금(공식 판정 실행에 쓴 판. 잠근 뒤에는 고치지 않는다)
"""
import os

for _thread_var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_thread_var, "1")

import argparse
import concurrent.futures
import json
import math
import sys
import time
import traceback

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fog3d_gates_s2a as base  # noqa: E402
import fog3d_terrain_add1 as terrain_add1  # noqa: E402
from fog3d_terrain import TERRAINS, TerrainSim  # noqa: E402
from fog3d_terrain_add1 import SLOPE_CURB_TERRAIN, TerrainSimR4  # noqa: E402

ROOT = base.ROOT
ADDENDUM_PATH = os.path.join(ROOT, "Docs", "VolumeFog3D", "stage2", "prereg-2a-design-add1.md")
PREREG_PATH = base.PREREG_PATH
LOCKED_STAGE2A = {"fog3d_terrain.py": "7a375bb4669b1da5", "fog3d_gates_s2a.py": "067957a533391361"}
NEW_CODE = ("fog3d_terrain_add1.py", "fog3d_gates_s2a_add1.py")

R4 = "R4"
R3 = "R3"
REF = base.REFERENCE_CANDIDATE
CANDIDATES_ALLOWED = (R3, R4)
GATES_NEEDING_R4_FLAT = ("G5", "G12")
THRESHOLDS_ADD1 = dict(G11_S_RATIO_MIN=0.5, G11_S_RATIO_MAX=2.0, G11_WIND_FACTOR=1.5, G12_REL_MAX=1e-3)
G11_PATHS = {"hill": ((-50.0, 0.0), (1.0, 0.0)), "up": base.WALK_PATHS["up"], "down": base.WALK_PATHS["down"]}
Y_DOWN_PATH = ((0.0, 300.0), (0.0, -1.0))
SCENE_ADD1 = dict(
    g11_walks=(("HILL", "hill", "S"), ("FOOT30", "up", "crossing_abs_mean"), ("FOOT30", "down", "crossing_abs_mean"),
               ("TOP30", "up", "crossing_abs_mean"), ("TOP30", "down", "crossing_abs_mean")),
    g12_walks=(("S20", "up"), ("S20", "down"), ("S30", "up"), ("S30", "down"), ("S30", "contour"), ("S30Y", "contour"), ("S30Y", "ydown")),
    g12_g6_variants=tuple((t, k, 60) for t in ("S30", "S45", "S60") for k in ("k2", "k3")) + (("S60", "k2", 30), ("S60", "k2", 144)),
    info11_walk_point=(-20.0, 0.0), info12_variant=("ROUGH", "k3", 60), info14_terrain=SLOPE_CURB_TERRAIN, info14_walk_point=(-20.0, 0.0),
)
GATE_KIND = dict(base.GATE_KIND, G11="deciding", G12="required")
ALL_GATES = base.ALL_GATES + ("G11", "G12")


def make_sim(cand, terrain, params, **kw):
    if cand in (R4, REF):
        return TerrainSimR4(params, terrain, **kw)
    return TerrainSim(params, terrain, base.candidate_settings(cand), **kw)


def install():
    terrain_add1.register_terrains()
    base.SCENE["g4_headings"].setdefault(SLOPE_CURB_TERRAIN, base.SCENE["g4_headings"]["CLIFF"])
    base.SCENE["g4_blades"].setdefault(SLOPE_CURB_TERRAIN, base.SCENE["g4_blades"]["CLIFF"])
    base.WALK_PATHS.setdefault("ydown", Y_DOWN_PATH)
    base.make_sim = make_sim


install()


def locked_check():
    check = base.locked_check()
    found = dict(check["found"])
    mismatched = dict(check["mismatched"])
    for name, expected in LOCKED_STAGE2A.items():
        found[name] = base.fingerprint(os.path.join(HERE, name))
        if found[name] != expected:
            mismatched[name] = found[name]
    return dict(ok=not mismatched, mismatched=mismatched, found=found)


def code_fingerprints():
    out = {name: base.fingerprint(os.path.join(HERE, name)) for name in NEW_CODE}
    out["prereg-2a-design-add1.md"] = base.fingerprint(ADDENDUM_PATH)
    out["prereg-2a-design.md"] = base.fingerprint(PREREG_PATH)
    return out


def piece_walk_tracked(spec):
    terrain = TERRAINS[spec["terrain"]]
    sim = base.make_sim(spec["cand"], terrain, base.game_params())
    walker = base.Walker(terrain, spec["start"], spec["heading"], spec["smoke"])
    col0 = base.column(sim)
    run_max = 0.0
    for _ in range(walker.total):
        sim.step(walker.next_inputs())
        wind = float(sim.max_wind())
        run_max = math.inf if math.isnan(wind) else max(run_max, wind)
    delta = base.column(sim) - col0
    out = base.band_stats(sim, delta, walker)
    out.update(steps=walker.total, max_wind=float(sim.max_wind()), run_max_wind=run_max, state_hash=sim.state_hash(), finite=base.finite(sim))
    return out


PIECE_RUNNERS = dict(base.PIECE_RUNNERS, walk_tracked=piece_walk_tracked)
PIECE_COST = dict(base.PIECE_COST, walk_tracked=148)


def flat_candidate(cand):
    return REF if cand == R4 else cand


def g11_spec(cand, terrain, path, smoke):
    start, heading = G11_PATHS[path]
    return base.spec(cand, "G11", f"{terrain}_{path}", "walk_tracked", smoke, terrain=terrain, path=path, start=list(start), heading=list(heading))


def g6_spec(cand, terrain, motion, hz, smoke):
    return base.spec(cand, "G6", f"{terrain}_{motion}_{hz}", "g6", smoke, terrain=terrain, motion=motion, hz=hz)


def gate_specs(cand, gate, smoke):
    if gate == "G11":
        walks = SCENE_ADD1["g11_walks"]
        flat = sorted({p for _, p, _ in walks})
        return [g11_spec(cand, t, p, smoke) for t, p, _ in walks] + [g11_spec(flat_candidate(cand), "F0", p, smoke) for p in flat]
    if gate == "G12":
        walks = SCENE_ADD1["g12_walks"]
        out = [base.walk_spec(cand, t, p, smoke) for t, p in walks] + [base.walk_spec(REF, "F0", p, smoke) for p in sorted({p for _, p in walks})]
        return out + [g6_spec(cand, t, k, hz, smoke) for t, k, hz in SCENE_ADD1["g12_g6_variants"]]
    return base.gate_specs(cand, gate, smoke)


def g4_spec(cand, terrain, walk_point, smoke):
    return base.spec(cand, "G4", f"{terrain}_a_x{walk_point[0]:g}", "g4", smoke, terrain=terrain, side="a", walk_point=list(walk_point))


def info_specs(smoke):
    out = [base.walk_spec(R4, t, p, smoke) for t in ("CURB45", "CURB60") for p in ("up", "down")]
    out += [base.walk_spec(REF, "F0", p, smoke) for p in ("up", "down", "contour")]
    out += [base.walk_spec(R4, t, p, smoke) for t in ("S10", "S45") for p in ("up", "down")]
    out += [base.walk_spec(R4, t, "contour", smoke) for t in ("S10", "S20", "S30", "S45")]
    out += base.gate_specs(R4, "G6", smoke)
    out += gate_specs(R3, "G11", smoke)
    out.append(g4_spec(R4, "CLIFF", SCENE_ADD1["info11_walk_point"], smoke))
    out.append(g6_spec(R4, *SCENE_ADD1["info12_variant"], smoke))
    out += [g4_spec(c, SCENE_ADD1["info14_terrain"], SCENE_ADD1["info14_walk_point"], smoke) for c in (R3, R4)]
    return out


def run_piece(item):
    s, out_dir = item
    path, npz = base.piece_paths(out_dir, s["id"])
    s = dict(s)
    if s["kind"] == "g9" or s.get("save_fields"):
        s["npz"] = npz
    t0 = time.time()
    try:
        values = PIECE_RUNNERS[s["kind"]](s)
        error = None
    except Exception:
        values, error = None, traceback.format_exc()
    res = dict(spec={k: v for k, v in s.items() if k != "npz"}, values=values, error=error, elapsed_sec=round(time.time() - t0, 2),
               code=code_fingerprints(), locked=locked_check()["ok"], model=dict(R4="TerrainSimR4", REF="TerrainSimR4(F0 평지 값)"))
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
    todo.sort(key=lambda s: -PIECE_COST[s["kind"]])
    if not todo:
        return []
    print(f"[run] 조각 {len(todo)}개, 프로세스 {jobs}개", flush=True)
    failures = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        for piece_id, error, elapsed in pool.map(run_piece, [(s, out_dir) for s in todo]):
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
            if cand != R4 and g in GATES_NEEDING_R4_FLAT:
                print(f"[run] {cand} {g} 건너뜀: 평지 값 조각(REF)이 R4 라서 검증용 후보에는 쓰지 않음", flush=True)
                continue
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


def ratio_or_nan(a, b):
    return a / b if b > 0 else float("nan")


def g11_walk(pieces, cand, terrain, path, measure):
    vals = base.need(pieces, [f"{cand}_G11_{terrain}_{path}", f"{flat_candidate(cand)}_G11_F0_{path}"])
    if vals is None:
        return None
    v, f = vals
    lo, hi, factor = THRESHOLDS_ADD1["G11_S_RATIO_MIN"], THRESHOLDS_ADD1["G11_S_RATIO_MAX"], THRESHOLDS_ADD1["G11_WIND_FACTOR"]
    value_ratio = ratio_or_nan(v[measure], f[measure])
    checks = dict(finite=bool(v["finite"]), value_in_band=bool(lo <= value_ratio <= hi), wind_ok=bool(v["run_max_wind"] <= factor * f["run_max_wind"]))
    return dict(measure=measure, finite=bool(v["finite"]), value_over_flat=value_ratio, run_max_wind_over_flat=ratio_or_nan(v["run_max_wind"], f["run_max_wind"]),
                value=v[measure], value_flat=f[measure], run_max_wind=v["run_max_wind"], run_max_wind_flat=f["run_max_wind"], checks=checks,
                passed=bool(all(checks.values())))


def eval_g11(pieces, cand):
    walks = {}
    for terrain, path, measure in SCENE_ADD1["g11_walks"]:
        w = g11_walk(pieces, cand, terrain, path, measure)
        if w is None:
            return None
        walks[f"{terrain}_{path}"] = w
    lo, hi, factor = THRESHOLDS_ADD1["G11_S_RATIO_MIN"], THRESHOLDS_ADD1["G11_S_RATIO_MAX"], THRESHOLDS_ADD1["G11_WIND_FACTOR"]
    value = {k: dict(finite=w["finite"], value_over_flat=w["value_over_flat"], run_max_wind_over_flat=w["run_max_wind_over_flat"]) for k, w in walks.items()}
    return base.gate_result(value, dict(value_over_flat=[lo, hi], run_max_wind_over_flat=factor), bool(all(w["passed"] for w in walks.values())),
                            walks=walks)


def relative_gap(a, b):
    if b is None or a is None or not b > 0:
        return float("inf")
    return abs(a - b) / b


def eval_g12(pieces, cand):
    values = {}
    for t, p in SCENE_ADD1["g12_walks"]:
        vals = base.need(pieces, [f"{cand}_WALK_{t}_{p}", f"{REF}_WALK_F0_{p}"])
        if vals is None:
            return None
        values[f"walk_{t}_{p}"] = relative_gap(vals[0]["S"], vals[1]["S"])
    for t, k, hz in SCENE_ADD1["g12_g6_variants"]:
        v = base.values_of(pieces, f"{cand}_G6_{t}_{k}_{hz}")
        if v is None:
            return None
        values[f"g6_{t}_{k}_{hz}"] = relative_gap(v["max_wind"], (v.get("flat_control") or {}).get("max_wind"))
    lim = THRESHOLDS_ADD1["G12_REL_MAX"]
    return base.gate_result(values, lim, bool(all(x <= lim for x in values.values())), count=len(values))


def evaluate(pieces, cand, out_dir):
    table = {"G1": lambda: base.eval_g1(pieces, cand), "G2": lambda: base.eval_g2(pieces, cand), "G3": lambda: base.eval_g3(pieces, cand),
             "G4": lambda: base.eval_g4(pieces, cand), "G5": lambda: base.eval_g5(pieces, cand), "G6": lambda: base.eval_g6(pieces, cand),
             "G7": lambda: base.eval_g7(pieces, cand), "G8": lambda: base.eval_g8(pieces, cand), "G9": lambda: base.eval_g9(pieces, cand, out_dir),
             "G10": lambda: base.eval_g10(pieces, cand), "G11": lambda: eval_g11(pieces, cand), "G12": lambda: eval_g12(pieces, cand)}
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


def select(status):
    if status["status"] == "all_passed":
        return dict(chosen=R4, reason="R4 가 2A-G1 ~ 2A-G12 를 모두 만족(덧붙임 F절)")
    if status["status"] == "failed":
        names = ", ".join(f"{f['gate']}({f['kind']})" for f in status["failed"])
        return dict(chosen=None, reason=f"떨어진 선이 있어 아무것도 고르지 않음(덧붙임 F절): {names}")
    return dict(chosen=None, reason=f"조각이 빠져 판정할 수 없음: {', '.join(status['missing'])}")


def info_block(pieces):
    info = {"1_curb": {t: base.curb_ratios(pieces, R4, t) for t in ("CURB45", "CURB60")}}
    flat = {p: base.values_of(pieces, f"{REF}_WALK_F0_{p}") for p in ("up", "down", "contour")}
    ratios = {}
    for t in ("S10", "S20", "S30", "S45"):
        for p in ("up", "down", "contour"):
            v = base.values_of(pieces, f"{R4}_WALK_{t}_{p}")
            if v is not None and flat[p] is not None and flat[p]["S"] > 0:
                ratios[f"{t}_{p}"] = dict(S=v["S"], over_flat=v["S"] / flat[p]["S"], signed_sum=v["signed_sum"])
    info["2_slope_walks"] = dict(ratios=ratios, A={t: base.asymmetry(pieces, R4, t) for t in ("S10", "S20", "S30", "S45")})
    world = {}
    for t, k, hz in base.g6_variants() + [SCENE_ADD1["info12_variant"]]:
        v, f = base.values_of(pieces, f"{R4}_G6_{t}_{k}_{hz}"), base.values_of(pieces, f"{REF}_G6_F0_{k}_{hz}")
        if v and f and f["world_wind_max"] > 0:
            world[f"{t}_{k}_{hz}"] = dict(world_wind_max=v["world_wind_max"], over_flat=v["world_wind_max"] / f["world_wind_max"])
    info["7_world_wind"] = world
    info["9_g6_original_limit"] = {f"{t}_{k}_{hz}": (base.values_of(pieces, f"{R4}_G6_{t}_{k}_{hz}") or {}).get("info9_original")
                                   for t, k, hz in base.g6_variants()}
    info["10_R3_G11"] = eval_g11(pieces, R3)
    x11 = SCENE_ADD1["info11_walk_point"][0]
    info["11_cliff_x_minus_20_opposite_max"] = (base.values_of(pieces, f"{R4}_G4_CLIFF_a_x{x11:g}") or {}).get("opposite_max")
    t, k, hz = SCENE_ADD1["info12_variant"]
    rough = base.values_of(pieces, f"{R4}_G6_{t}_{k}_{hz}") or {}
    info["12_rough_k3"] = {key: rough.get(key) for key in ("wind_over_limit", "max_wind", "converted_input_speed", "M", "limit",
                                                          "energy_end_over_peak", "finite", "passed", "info9_original")}
    info["13_cost_add1"] = dict(per_input="틀 기울기 읽기 1번: Sx·Sy 를 이중선형 보간(기둥 네 개, 비율 0~1 로 자름)으로, 입력 하나에 한 번",
                                per_sample="z̃ = z − Scx·(x − mx) − Scy·(y − my): 곱셈 2번·뺄셈 2번(+ 표본 자리와 가운데의 차 2번)",
                                memory="더 드는 메모리 없음(기둥 기울기는 R3 가 이미 가짐)")
    x14 = SCENE_ADD1["info14_walk_point"][0]
    slope_curb = {c: (base.values_of(pieces, f"{c}_G4_{SCENE_ADD1['info14_terrain']}_a_x{x14:g}") or {}).get("opposite_max") for c in (R3, R4)}
    r3, r4 = slope_curb[R3], slope_curb[R4]
    slope_curb["R4_over_R3"] = r4 / r3 if r3 is not None and r4 is not None and r3 > 0 else None
    info["14_slope_curb_opposite_max"] = slope_curb
    return info


def write_markdown(path, res):
    fmt = base.fmt
    gates = res["candidates"][R4]
    status = res["statuses"][R4]
    lines = ["# 2a 덧붙임 1 판정 결과 (후보 R4)", "", f"- 시운전(단계 줄임): {res['smoke']}", f"- 잠긴 파일 지문 일치: {res['locked']['ok']}",
             f"- 코드·등록 지문: {fmt(res['code'])}", f"- 고른 후보: {res['selection']['chosen']} ({res['selection']['reason']})",
             f"- 평지 값(REF 조각)은 R4 를 F0 에서 돌린 값이다(덧붙임 E절)",
             f"- 한 단계 시간(128칸, 걷기 조각 중앙값): {fmt(res['step_seconds_128'])}초", "",
             "| 후보 | 판정선 | 종류 | 값 | 문턱 | 통과 |", "|---|---|---|---|---|---|"]
    for g in ALL_GATES:
        r = gates.get(g)
        if r is None:
            lines.append(f"| R4 | {g} | {GATE_KIND[g]} | 조각 없음 | - | - |")
            continue
        lines.append(f"| R4 | {g} | {r['kind']} | {fmt(r['value'])} | {fmt(r['threshold'])} | {r['passed']} |")
        if g == "G6":
            lines += [base.g6_markdown_row(R4, name, run) for name, run in r["detail"]["runs"].items()]
        if g == "G11":
            lines += [f"| R4 | G11 · {name} | 걷기 | {fmt({k: w[k] for k in ('measure', 'finite', 'value_over_flat', 'run_max_wind_over_flat')})} | "
                      f"0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | {w['passed']} |" for name, w in r["detail"]["walks"].items()]
        if g == "G12":
            lines += [f"| R4 | G12 · {name} | 값 | {fmt(v)} | ≤ {THRESHOLDS_ADD1['G12_REL_MAX']:g} | {v <= THRESHOLDS_ADD1['G12_REL_MAX']} |"
                      for name, v in r["value"].items()]
    failed = ", ".join(f"{f['gate']}({'필수' if f['kind'] == 'required' else '가름'})" for f in status["failed"]) or "없음"
    lines += ["", "| 후보 | 상태 | 떨어진 선(종류) | 빠진 선 |", "|---|---|---|---|",
              f"| R4 | {status['status']} | {failed} | {', '.join(status['missing']) or '없음'} |"]
    lines += ["", "## 정보", ""] + [f"- {k}: {fmt(v)}" for k, v in res["info"].items()]
    if res["missing_or_failed"]:
        lines += ["", "## 오류 조각", ""] + [f"- {x}" for x in res["missing_or_failed"]]
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


def cmd_collect(args):
    pieces = base.load_pieces(args.out_dir)
    gates = evaluate(pieces, R4, args.out_dir)
    status = candidate_status(gates)
    selection = select(status)
    res = dict(prereg="Docs/VolumeFog3D/stage2/prereg-2a-design.md", addendum="Docs/VolumeFog3D/stage2/prereg-2a-design-add1.md",
               smoke=any(p["spec"].get("smoke") for p in pieces.values()), locked=locked_check(), code=code_fingerprints(),
               thresholds=dict(base.THRESHOLDS, **THRESHOLDS_ADD1), scene=base.json_safe(dict(base.SCENE, **SCENE_ADD1)),
               candidates={R4: {g: r for g, r in gates.items() if r is not None}}, statuses={R4: status}, selection=selection,
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
    ap.add_argument("--candidates", default=R4)
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
