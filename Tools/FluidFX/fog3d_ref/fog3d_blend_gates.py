"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. fog3d_blend_patch(섞기 식 갈아 끼우기)·fog3d_blend_probe(재는 양의 정의)와 잠긴 fog3d_* 를 import 만 한다
실행 명령: PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_blend_gates.py run --formula <B4|B3|B1|B0> --out-dir <폴더> [--jobs 6] [--set all|bl|ladder|info|shake|smoke] [--bl-gains 1,3] / collect --out-dir <폴더> [--formula B4]
출력: <폴더>/pieces/<식>_<장면>_….json(+npz, 경우마다 하나, 있으면 건너뛰어 이어 하기), collect 는 <폴더>/results_<식>.json·results_<식>.md(값 표와 등록 문턱을 기계적으로 댄 결과 열)
상태: 바람 섞기 식 후보의 필수 판정선 BL-1~5·정보 1~7·자국 지표 T 사다리 측정기(Docs/VolumeFog3D/stage2/prereg-blend.md 3~5절), 2026-11-16
"""
import os

for _thread_var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_thread_var, "1")

import argparse
import concurrent.futures
import copy
import json
import math
import sys
import time
import traceback

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fog3d_blend_patch as blend_patch  # noqa: E402

blend_patch.install_from_environment()

import fog3d_blend_probe as probe  # noqa: E402
import fog3d_core as core  # noqa: E402
import fog3d_gates_s2a as gates_s2a  # noqa: E402
import fog3d_replay as rp  # noqa: E402
import fog3d_replay_s2a as rs  # noqa: E402
import fog3d_terrain_add1 as terrain_add1  # noqa: E402
import fog3d_terrain_add2 as terrain_add2  # noqa: E402

SCENE = gates_s2a.SCENE
FLAT = probe.FLAT
BASE_DT = SCENE["dt"]
STRENGTHS = (1.0, 2.0, 2.5, 3.0)
HZ_PAIR = (60, 30)
LADDER = (1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0)
CANONICAL = dict(walk_stand=SCENE["body_wind"], blade=SCENE["blade_wind"])
SPEED = dict(walk_stand=SCENE["walk_speed"], blade=SCENE["blade_speed"], turn=SCENE["walk_speed"])
THRESHOLDS = dict(BL1_RATIO_MAX=0.85, BL1_SHARE_MAX=0.15, BL1_WINDOW_SECONDS=0.5, BL2_WALK_MAX=0.02, BL2_BLADE_MAX=0.10, BL3_MAX=1.27,
                  BL4_MIN=0.0, BL4_MAX=1.0, BL4_SKIP_CM_S=1e-3, BL5_FACTOR=3.0, LADDER_FLAT=0.05, T_EXCLUDE_RADIUS_CM=120.0)
SHAKE = dict(amp=1e-4, seed=20261113, walk_steps_60=70, stop_steps_60=78, compare_strength=2.0, compare_formula="B0")
INFO_TIMES = dict(walk_stand=(0.5, 1.5), blade=(0.25, 1.0))
GRID = dict(n=128)
CASE_SECONDS = dict(walk_stand=500, blade=100, turn=150, shake=700, s2a_walk=300, s2a_g4=350)


def gained(spheres, gain):
    for s in spheres:
        s.wind_gain = float(gain)
    return spheres


class BlendGateSim(probe.ProbeSim):
    blend_rows = None

    def _apply_inputs(self, inputs):
        if self.blend_rows is not None and inputs:
            self.blend_rows.append(measure_blend_ratio(self, inputs))
        super()._apply_inputs(inputs)


def make_sim(n, dt, formula):
    r = rp.Replay("blend_gates", n=n)
    r.body["tuning"] = dict(rs.TUNING_S2A)
    p = rp.build_params(r.finish(), dt)
    p.advect = "N2"
    sim = BlendGateSim(p, FLAT, anchor=(0, 0), window_min=rp.default_window((0, 0), n))
    sim.blend_formula = formula
    return sim


def measure_blend_ratio(sim, inputs):
    clone = copy.deepcopy(sim)
    clone.blend_rows = None
    formula = blend_patch.formula_of(clone)
    lo, hi, outside, f32_only, faces = math.inf, -math.inf, 0, 0, 0
    lo32, hi32, outside32 = math.inf, -math.inf, 0
    for inp in inputs:
        a, b, slope = clone._input_frame(inp)
        si, sj = clone._bbox(a, b, float(inp.radius))
        if si.start >= si.stop or sj.start >= sj.stop:
            continue
        gain = blend_patch.gain_of(inp, formula)
        before = {name: getattr(clone, name)[si, sj].astype(np.float64).copy() for name in ("u", "v", "w")}
        targets = {name: np.broadcast_to(gain * np.asarray(clone._wind_target(comp, inp.velocity, si, sj, slope), float), before[name].shape)
                   for comp, name in enumerate(("u", "v", "w"))}
        terrain_add2.TerrainSimR5._apply_inputs(clone, [inp])
        for name in ("u", "v", "w"):
            after = getattr(clone, name)[si, sj].astype(np.float64)
            ratio, keep = ratio_to_target(before[name], after, targets[name])
            if ratio is not None:
                bad = (ratio < THRESHOLDS["BL4_MIN"]) | (ratio > THRESHOLDS["BL4_MAX"])
                spacing = np.spacing(np.abs(targets[name][keep]).astype(np.float32)).astype(np.float64)
                lo, hi = min(lo, float(ratio.min())), max(hi, float(ratio.max()))
                outside += int(bad.sum())
                f32_only += int((bad & (np.abs((after - targets[name])[keep]) <= spacing)).sum())
                faces += int(keep.sum())
            ratio32, _ = ratio_to_target(before[name], after, targets[name].astype(np.float32).astype(np.float64))
            if ratio32 is not None:
                lo32, hi32 = min(lo32, float(ratio32.min())), max(hi32, float(ratio32.max()))
                outside32 += int(((ratio32 < THRESHOLDS["BL4_MIN"]) | (ratio32 > THRESHOLDS["BL4_MAX"])).sum())
    return dict(min=lo, max=hi, outside=outside, outside_within_f32_spacing=f32_only, faces=faces,
                target_f32=dict(min=lo32, max=hi32, outside=outside32))


def ratio_to_target(before, after, target):
    d_before = before - target
    keep = np.abs(d_before) > THRESHOLDS["BL4_SKIP_CM_S"]
    if not keep.any():
        return None, keep
    return (after - target)[keep] / d_before[keep], keep


def walk_stand_steps(strength, dt, gain):
    steps, moving = probe.walk_stand_inputs(strength, dt)
    return [gained(s, gain) for s in steps], moving


def blade_steps(strength, dt, gain):
    steps, moving = probe.blade_inputs(strength, dt)
    return [gained(s, gain) for s in steps], moving


def turn_steps(strength, dt, gain):
    leg = int(round(35 * BASE_DT / dt))
    first = gates_s2a.Walker(FLAT, np.array((-SCENE["walk_back"], 0.0)), (1.0, 0.0), False, strength=strength, dt=dt, stops=False)
    first.walk_steps = first.total = leg
    steps = [first.next_inputs() for _ in range(leg)]
    second = gates_s2a.Walker(FLAT, first.pos, (0.0, 1.0), False, strength=strength, dt=dt, stops=False)
    second.walk_steps = second.total = leg
    steps += [second.next_inputs() for _ in range(leg)]
    return [gained(s, gain) for s in steps], 2 * leg


def t_pairs(case, moving, dt):
    pairs = dict(end=(moving - 1, moving))
    for seconds in INFO_TIMES.get(case["scene"], ()):
        t = moving + int(round(seconds / dt))
        pairs[f"after_{seconds:g}s"] = (t - 1, t)
    return pairs


def exclusion_center(case, moving, dt):
    if case["scene"] == "blade":
        return tuple(float(c) for c in probe.BLADE_PATH[1])
    return (-SCENE["walk_back"] + SCENE["walk_speed"] * dt * moving, 0.0)


def mark_amounts(sim, col0, center):
    delta = np.abs(gates_s2a.column(sim) - col0)
    rx, ry, _ = sim.center_rel()
    far = np.hypot(rx[:, None] - center[0], ry[None, :] - center[1]) > THRESHOLDS["T_EXCLUDE_RADIUS_CM"]
    return dict(full=float(delta.sum()), outside_120cm=float(delta[far].sum()))


def run_scene(case):
    dt = 1.0 / case["hz"]
    builder = dict(walk_stand=walk_stand_steps, blade=blade_steps, turn=turn_steps)[case["scene"]]
    steps, moving = builder(case["strength"], dt, case["gain"])
    if case.get("max_steps"):
        steps = steps[:case["max_steps"]]
        moving = min(moving, len(steps))
    sim = make_sim(case["n"], dt, case["formula"])
    col0 = gates_s2a.column(sim)
    pairs = t_pairs(case, moving, dt)
    wanted = {s for pair in pairs.values() for s in pair if 1 <= s <= len(steps)}
    center = exclusion_center(case, moving, dt)
    standing_step = moving + (len(steps) - moving) * 3 // 4
    bl4_steps = set()
    if case.get("bl4"):
        bl4_steps = set(range(moving)) if case["scene"] == "blade" else {standing_step}
    marks, fields, bl4, records, mask = {}, {}, [], [], None
    for index, spheres in enumerate(steps):
        if spheres:
            mask = probe.footprint(spheres, sim.origin, case["n"])
        sim.blend_rows = [] if index in bl4_steps and index < len(steps) else None
        sim.step(spheres)
        if sim.blend_rows:
            bl4.append(dict(step=index + 1, **sim.blend_rows[0]))
        sim.blend_rows = None
        records.append(probe.field_metrics(probe.state_fields(sim), mask, sim.fluid))
        if index + 1 in wanted:
            marks[index + 1] = mark_amounts(sim, col0, center)
            if case.get("save_fields"):
                for name, attr in (("dx", "dpx"), ("dy", "dpy"), ("dz", "dpz"), ("g", "g")):
                    fields[f"{name}_{index + 1}"] = getattr(sim, attr).copy()
    if fields:
        fields["fluid"] = sim.fluid.copy()
        np.savez_compressed(case["npz"], **fields)
    series = {k: [r[k] for r in records] for k in ("face_max", "ring_max", "ring_along", "ring_mean", "disp_max", "g_min")}
    t_values = {name: dict(steps=list(pair), **{w: float(np.mean([marks[s][w] for s in pair])) for w in ("full", "outside_120cm")})
                for name, pair in pairs.items() if all(s in marks for s in pair)}
    return dict(moving=moving, steps=len(steps), series=series, T=t_values, T_center=center, bl4=bl4, finite=bool(gates_s2a.finite(sim)),
                state_hash=sim.state_hash(), fields_file=os.path.basename(case["npz"]) if fields else None)


def shake_spheres(spheres, rng):
    out = []
    for s in spheres:
        s = dict(s)
        for key in ("a_cm", "b_cm"):
            d = [float(rng.choice([-1.0, 1.0])) * SHAKE["amp"] for _ in s[key]]
            s[key] = [c + e for c, e in zip(s[key], d)]
        out.append(s)
    return out


def shake_replay(strength, dt, n, max_steps=None):
    r = rs.new_replay("blend_shake", tuning=rs.TUNING_S2A)
    r.n = n
    r.body["grid"]["n"] = n
    r.window = rp.default_window((0, 0), n)
    heading = np.array((1.0, 0.0))
    walker = gates_s2a.Walker(FLAT, -heading * SCENE["walk_back"], heading, False, strength=strength, dt=dt)
    walker.walk_steps = int(round(SHAKE["walk_steps_60"] * BASE_DT / dt))
    walker.total = walker.walk_steps + int(round(SHAKE["stop_steps_60"] * BASE_DT / dt))
    total = walker.total if not max_steps else min(walker.total, max_steps)
    for _ in range(total):
        r.step(dt, walker.next_inputs())
    replay = r.finish()
    return replay, rs.attach_ground(replay, "F0", "R4")


def shake_sim(replay, formula):
    first = replay["steps"][0]
    p = rp.build_params(replay, first["dt"])
    p.advect = "N2"
    sim = terrain_add1.TerrainSimR4(p, FLAT, anchor=(0, 0), window_min=tuple(first["prev_window_min"]), obstacles=[])
    sim.blend_formula = formula
    return sim


def advance(sim, replay, step, spheres):
    if tuple(step["window_min"]) != sim.origin:
        sim.set_window(tuple(step["window_min"]))
    rp.apply_knobs(sim, replay, step)
    sim.step(rp.core_spheres(spheres))


def run_shake(case):
    if rp.SENSITIVITY_SEED != SHAKE["seed"]:
        raise RuntimeError("흔들기 씨앗이 등록과 다름")
    dt = 1.0 / case["hz"]
    replay, _ = shake_replay(case["strength"], dt, case["n"], case.get("max_steps"))
    a, b = shake_sim(replay, case["formula"]), shake_sim(replay, case["formula"])
    rng = np.random.default_rng(SHAKE["seed"])
    for step in replay["steps"]:
        advance(a, replay, step, step["spheres"])
        advance(b, replay, step, shake_spheres(step["spheres"], rng))
    fluid = a.fluid & b.fluid
    diff = {}
    for name in ("u", "v", "w"):
        d = np.abs(getattr(a, name).astype(np.float64) - getattr(b, name))
        d[~fluid] = 0
        diff[name] = float(d.max())
    return dict(measured_step=len(replay["steps"]), measured_seconds=len(replay["steps"]) * dt, max_abs_diff=diff, finite=bool(gates_s2a.finite(a) and gates_s2a.finite(b)))


def run_s2a(case):
    import fog3d_gates_s2a_add2 as add2
    if case["kind"] == "s2a_walk":
        spec = gates_s2a.walk_spec("R5", "S30", "up", bool(case.get("smoke")))
    else:
        spec = gates_s2a.spec("R5", "G4", "CLIFF_a", "g4", bool(case.get("smoke")), terrain="CLIFF", side="a")
    values = add2.PIECE_RUNNERS[spec["kind"]](spec)
    return dict(spec=spec, values=json.loads(json.dumps(values, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))))


def run_case(item):
    case, out_dir = item
    path = os.path.join(out_dir, "pieces", case["id"] + ".json")
    case = dict(case, npz=os.path.join(out_dir, "pieces", case["id"] + ".npz"))
    blend_patch.install(case["formula"], default_gain=case.get("default_gain", 1.0))
    blend_patch.reset_calls()
    t0 = time.time()
    try:
        runner = dict(scene=run_scene, shake=run_shake, s2a_walk=run_s2a, s2a_g4=run_s2a)[case["kind"]]
        values, error = runner(case), None
    except Exception:
        values, error = None, traceback.format_exc()
    res = dict(case={k: v for k, v in case.items() if k != "npz"}, formula=case["formula"], gain=case.get("gain", case.get("default_gain")),
               strength=case.get("strength"), dt=1.0 / case["hz"] if "hz" in case else None, n=case.get("n"), values=values, error=error,
               elapsed_sec=round(time.time() - t0, 1), calls=blend_patch.calls_snapshot(), patch=blend_patch.patch_record(),
               prereg="Docs/VolumeFog3D/stage2/prereg-blend.md")
    blend_patch.install(case["formula"], default_gain=1.0)
    if error is None:
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(res, fh, ensure_ascii=False, indent=1)
        os.replace(tmp, path)
    return case["id"], error, res["elapsed_sec"]


def scene_case(formula, scene, strength, gain, hz, n=None, bl4=False, save_fields=False, max_steps=None, purpose=""):
    n = GRID["n"] if n is None else n
    tail = f"_max{max_steps}" if max_steps else ""
    return dict(id=f"{formula}_{scene}_s{strength:g}_g{gain:g}_hz{hz}_n{n}{tail}", kind="scene", formula=formula, scene=scene, strength=float(strength),
                gain=float(gain), hz=hz, n=n, bl4=bl4, save_fields=save_fields, max_steps=max_steps, purpose=purpose)


def shake_case(formula, strength, hz, n=None, max_steps=None):
    n = GRID["n"] if n is None else n
    tail = f"_n{n}_max{max_steps}" if max_steps else (f"_n{n}" if n != 128 else "")
    return dict(id=f"{formula}_shake_s{strength:g}_hz{hz}{tail}", kind="shake", formula=formula, strength=float(strength), gain=1.0, hz=hz, n=n,
                max_steps=max_steps, purpose="BL-5")


def s2a_case(formula, kind, smoke=False):
    return dict(id=f"{formula}_{kind}_g3{'_smoke' if smoke else ''}", kind=kind, formula=formula, default_gain=3.0, smoke=smoke, purpose="정보 6")


def bl_cases(formula, gains):
    out = []
    for scene in ("walk_stand", "blade"):
        for hz in HZ_PAIR:
            for s in STRENGTHS:
                for g in gains:
                    save = hz == 60 and g == 1.0 and s in (CANONICAL[scene], 1.0)
                    out.append(scene_case(formula, scene, s, g, hz, bl4=True, save_fields=save, purpose="BL-1~4"))
    return out


def ladder_cases(formula):
    return [scene_case(formula, scene, CANONICAL[scene], g, 60, purpose="3절 사다리") for scene in ("walk_stand", "blade") for g in LADDER]


def companion_cases(formula):
    out = [scene_case("B0", "walk_stand", CANONICAL["walk_stand"], 1.0, 60, save_fields=True, purpose="T 기준·정보 1·2"),
           scene_case("B0", "walk_stand", CANONICAL["walk_stand"], 1.0, 30, purpose="정보 1"),
           scene_case("B0", "blade", CANONICAL["blade"], 1.0, 60, save_fields=True, purpose="T 기준·정보 2"),
           scene_case("B0", "walk_stand", 1.0, 1.0, 60, save_fields=True, purpose="정보 2"),
           scene_case("B0", "blade", 1.0, 1.0, 60, save_fields=True, purpose="정보 2")]
    if formula != "B0":
        out += [scene_case("B1", "walk_stand", CANONICAL["walk_stand"], 1.0, hz, purpose="정보 1") for hz in HZ_PAIR]
    return out


def info_cases(formula):
    out = [scene_case(formula, "walk_stand", CANONICAL["walk_stand"], 1.0, 60, n=2 * GRID["n"], purpose="정보 4"),
           scene_case(formula, "turn", CANONICAL["walk_stand"], 1.0, 60, purpose="정보 5"),
           scene_case(formula, "walk_stand", CANONICAL["walk_stand"], 1.0, 144, purpose="정보 7")]
    if formula != "B0":
        out += [s2a_case(formula, "s2a_walk"), s2a_case(formula, "s2a_g4")]
    return out


def shake_cases(formula):
    return [shake_case(formula, CANONICAL["walk_stand"], hz) for hz in HZ_PAIR] + [shake_case(SHAKE["compare_formula"], SHAKE["compare_strength"], hz) for hz in HZ_PAIR]


def smoke_cases(formula):
    out = [scene_case(formula, scene, 3.0, g, hz, n=64, bl4=True, max_steps=20, purpose="연기 시험") for scene in ("walk_stand", "blade") for hz in HZ_PAIR for g in (1.0, 3.0)]
    out.append(scene_case(formula, "turn", 3.0, 1.0, 60, n=64, max_steps=20, purpose="연기 시험"))
    out.append(shake_case(formula, 3.0, 60, n=64, max_steps=20))
    out += [s2a_case(formula, "s2a_walk", smoke=True), s2a_case(formula, "s2a_g4", smoke=True)]
    return out


def requested_cases(formula, which, gains):
    table = dict(bl=lambda: bl_cases(formula, gains), ladder=lambda: ladder_cases(formula) + companion_cases(formula),
                 info=lambda: info_cases(formula) + companion_cases(formula), shake=lambda: shake_cases(formula), smoke=lambda: smoke_cases(formula))
    names = ("bl", "ladder", "info", "shake") if which == "all" else (which,)
    unique = {}
    for name in names:
        for case in table[name]():
            unique.setdefault(case["id"], case)
    return list(unique.values())


def case_cost(case):
    base = CASE_SECONDS.get(case.get("scene") or case["kind"], 300) * (case["n"] / 128) ** 2 if case.get("n") else CASE_SECONDS.get(case["kind"], 300)
    return base * (case.get("hz", 60) / 60)


def cmd_run(args):
    blend_patch.install(args.formula)
    os.makedirs(os.path.join(args.out_dir, "pieces"), exist_ok=True)
    gains = tuple(float(g) for g in args.bl_gains.split(","))
    cases = requested_cases(args.formula, args.set, gains)
    todo = [c for c in cases if not os.path.exists(os.path.join(args.out_dir, "pieces", c["id"] + ".json"))]
    todo.sort(key=lambda c: -case_cost(c))
    print(f"[run] 식 {args.formula}, 경우 {len(todo)}개(전체 {len(cases)}), 병렬 {args.jobs}, 어림 합 {round(sum(case_cost(c) for c in todo) / 60)}분", flush=True)
    failures = 0
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_case, (c, args.out_dir)) for c in todo]
        for future in concurrent.futures.as_completed(futures):
            case_id, error, elapsed = future.result()
            print(f"[run] {case_id} {'실패' if error else '끝'} {elapsed}s", flush=True)
            if error:
                failures += 1
                print(error, flush=True)
    return 1 if failures else 0


def load_pieces(out_dir):
    folder = os.path.join(out_dir, "pieces")
    out = {}
    for name in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        if name.endswith(".json"):
            with open(os.path.join(folder, name), encoding="utf-8") as fh:
                out[name[:-5]] = json.load(fh)
    return out


def scene_metrics(piece):
    case, v = piece["case"], piece["values"]
    dt = 1.0 / case["hz"]
    face, ring, along = v["series"]["face_max"], v["series"]["ring_max"], v["series"]["ring_along"]
    moving = v["moving"]
    speed = SPEED[case["scene"]]
    out = dict(face_max_moving=max(face[:moving]), bl3=max(face[:moving]) / (case["gain"] * speed))
    if case["scene"] == "blade":
        out["bl2"] = probe.alternation(ring[:moving])
    else:
        out["bl2"] = probe.alternation(ring[int(round(10 * BASE_DT / dt)):moving])
    index = int(round(0.1 / dt)) - 1
    out["along_at_0p1s"] = along[index] / speed if index < len(along) else None
    if case["scene"] == "walk_stand" and len(face) > moving:
        window = int(round(THRESHOLDS["BL1_WINDOW_SECONDS"] / dt))
        last, prev = max(face[-window:]), max(face[-2 * window:-window])
        out.update(bl1_last=last, bl1_prev=prev, bl1_a=last / prev if prev > 0 else math.inf, bl1_b=last / out["face_max_moving"],
                   bl1_a_pass=bool(last <= THRESHOLDS["BL1_RATIO_MAX"] * prev), bl1_b_pass=bool(last <= THRESHOLDS["BL1_SHARE_MAX"] * out["face_max_moving"]))
        stop_value = face[moving - 1]
        half = next((i - (moving - 1) for i in range(moving, len(face)) if face[i] <= 0.5 * stop_value), None)
        out["half_time_s"] = half * dt if half is not None else None
    bl2_max = THRESHOLDS["BL2_BLADE_MAX"] if case["scene"] == "blade" else THRESHOLDS["BL2_WALK_MAX"]
    out.update(bl2_pass=bool(out["bl2"] <= bl2_max), bl3_pass=bool(out["bl3"] <= THRESHOLDS["BL3_MAX"]))
    if v["bl4"]:
        lo, hi = min(r["min"] for r in v["bl4"]), max(r["max"] for r in v["bl4"])
        out.update(bl4_min=lo, bl4_max=hi, bl4_outside=sum(r["outside"] for r in v["bl4"]),
                   bl4_outside_within_f32_spacing=sum(r["outside_within_f32_spacing"] for r in v["bl4"]), bl4_steps=[r["step"] for r in v["bl4"]],
                   bl4_pass=bool(sum(r["outside"] for r in v["bl4"]) == 0),
                   bl4_target_f32_min=min(r["target_f32"]["min"] for r in v["bl4"]), bl4_target_f32_max=max(r["target_f32"]["max"] for r in v["bl4"]),
                   bl4_target_f32_pass=bool(sum(r["target_f32"]["outside"] for r in v["bl4"]) == 0))
    out["T_end"] = (v["T"].get("end") or {}).get("full")
    return out


def find(pieces, formula, scene, strength, gain, hz, n=None):
    return pieces.get(scene_case(formula, scene, strength, gain, hz, n)["id"])


def bl_table(pieces, formula):
    rows = []
    for key, piece in pieces.items():
        case = piece["case"]
        if case["formula"] != formula or case["kind"] != "scene" or case.get("max_steps") or case["n"] != GRID["n"] or not case.get("bl4"):
            continue
        rows.append(dict(id=key, scene=case["scene"], s=case["strength"], G=case["gain"], hz=case["hz"], **scene_metrics(piece)))
    rows.sort(key=lambda r: (r["scene"], -r["hz"], r["s"], r["G"]))
    return rows


def bl5_table(pieces, formula):
    rows = []
    for hz in HZ_PAIR:
        cand = pieces.get(shake_case(formula, CANONICAL["walk_stand"], hz)["id"])
        ref = pieces.get(shake_case(SHAKE["compare_formula"], SHAKE["compare_strength"], hz)["id"])
        if not cand or not ref:
            rows.append(dict(hz=hz, missing=True))
            continue
        c, r = cand["values"]["max_abs_diff"], ref["values"]["max_abs_diff"]
        ratio = {k: c[k] / r[k] if r[k] > 0 else (0.0 if c[k] == 0 else math.inf) for k in c}
        rows.append(dict(hz=hz, step=cand["values"]["measured_step"], candidate=c, b0_s2=r, ratio=ratio,
                         passed={k: bool(c[k] <= THRESHOLDS["BL5_FACTOR"] * r[k]) for k in c}))
    return rows


def ladder_table(pieces, formula):
    out = {}
    for scene in ("walk_stand", "blade"):
        s = CANONICAL[scene]
        ref = find(pieces, "B0", scene, s, 1.0, 60)
        t_ref = (ref or {}).get("values", {}).get("T", {}).get("end", {}).get("full") if ref else None
        values = {}
        for g in LADDER:
            piece = find(pieces, formula, scene, s, g, 60)
            if piece:
                values[g] = piece["values"]["T"]["end"]["full"]
        if not t_ref or len(values) < len(LADDER):
            out[scene] = dict(T_B0=t_ref, T=values, complete=False)
            continue
        dist = {g: abs(math.log(t / t_ref)) if t > 0 else math.inf for g, t in values.items()}
        best = min(sorted(dist), key=lambda g: (dist[g], g))
        ts = list(values.values())
        spread = (max(ts) - min(ts)) / (sum(ts) / len(ts))
        flat = spread < THRESHOLDS["LADDER_FLAT"]
        out[scene] = dict(T_B0=t_ref, T=values, abs_log_ratio=dist, spread=spread, flat=flat, rule_pick=1.0 if flat else best,
                          all_below_B0=bool(max(ts) < t_ref), complete=True)
    return out


def info_t_table(pieces, formula):
    out = {}
    for scene in ("walk_stand", "blade"):
        for label, f in ((formula, formula), ("B0", "B0")):
            piece = find(pieces, f, scene, CANONICAL[scene], 1.0, 60)
            if piece:
                out[f"{scene}_{label}"] = piece["values"]["T"]
    return out


def group_ratio_file(a_path, b_path, step):
    if not (os.path.exists(a_path) and os.path.exists(b_path)):
        return None
    a, b = np.load(a_path), np.load(b_path)
    if f"g_{step}" not in a or f"g_{step}" not in b:
        return None
    fluid = b["fluid"]
    cand = {k: a[f"{k}_{step}"] for k in ("dx", "dy", "dz", "g")}
    ref = {k: b[f"{k}_{step}"] for k in ("dx", "dy", "dz", "g")}
    return dict(disp=probe.group_ratio(ref, cand, ("dx", "dy", "dz"), fluid), fill=probe.group_ratio(ref, cand, ("g",), fluid))


def info2_table(pieces, out_dir, formula):
    out = {}
    folder = os.path.join(out_dir, "pieces")
    for scene in ("walk_stand", "blade"):
        for s in (CANONICAL[scene], 1.0):
            cand, ref = find(pieces, formula, scene, s, 1.0, 60), find(pieces, "B0", scene, s, 1.0, 60)
            if not cand or not ref or not cand["values"].get("fields_file") or not ref["values"].get("fields_file"):
                continue
            rows = {}
            for name, t in cand["values"]["T"].items():
                rows[name] = {str(step): group_ratio_file(os.path.join(folder, cand["values"]["fields_file"]), os.path.join(folder, ref["values"]["fields_file"]), step)
                              for step in t["steps"]}
            out[f"{scene}_s{s:g}"] = rows
    return out


def info1_table(pieces, formula):
    out = {}
    for f in ("B0", "B1", formula):
        for hz in HZ_PAIR:
            piece = find(pieces, f, "walk_stand", CANONICAL["walk_stand"], 1.0, hz)
            if piece:
                m = scene_metrics(piece)
                out[f"{f}_hz{hz}"] = dict(along_at_0p1s=m["along_at_0p1s"], face_max_over_speed=m["bl3"], half_time_s=m.get("half_time_s"))
    return out


def info3_table(pieces, formula):
    out = {}
    for s in (1.0, 2.0, 3.0):
        for hz in HZ_PAIR:
            piece = find(pieces, formula, "walk_stand", s, 1.0, hz)
            if piece:
                m = scene_metrics(piece)
                out[f"s{s:g}_hz{hz}"] = dict(face_max_moving=m["face_max_moving"], along_at_0p1s=m["along_at_0p1s"], T_end=m["T_end"])
    return out


def metric_subset(piece, keys):
    if not piece:
        return None
    m = scene_metrics(piece)
    return {k: m.get(k) for k in keys}


def info_tables(pieces, out_dir, formula):
    s3 = CANONICAL["walk_stand"]
    bl123 = ("bl1_a", "bl1_a_pass", "bl1_b", "bl1_b_pass", "bl2", "bl2_pass", "bl3", "bl3_pass")
    s2a = {k: pieces.get(s2a_case(formula, k)["id"]) for k in ("s2a_walk", "s2a_g4")}
    six = {}
    for k, p in s2a.items():
        if p:
            v = p["values"]["values"]
            six[k] = dict(finite=v.get("finite"), opposite_max=v.get("opposite_max"), max_wind=v.get("max_wind"), run_max_wind=v.get("run_max_wind"))
    return {"1_time_match": info1_table(pieces, formula), "2_field_diff_vs_B0": info2_table(pieces, out_dir, formula),
            "3_strength": info3_table(pieces, formula), "4_n256": metric_subset(find(pieces, formula, "walk_stand", s3, 1.0, 60, n=2 * GRID["n"]), bl123),
            "5_turn": metric_subset(find(pieces, formula, "turn", s3, 1.0, 60), ("bl2", "bl2_pass", "bl3", "bl3_pass")),
            "6_s2a_gain3": six, "7_dt144_bl1": metric_subset(find(pieces, formula, "walk_stand", s3, 1.0, 144), ("bl1_last", "bl1_prev", "bl1_a", "bl1_a_pass", "bl1_b", "bl1_b_pass")),
            "T_info_times": info_t_table(pieces, formula)}


def all_passed(rows, key):
    flags = [r[key] for r in rows if key in r]
    return bool(flags) and all(flags), len(flags)


def cmd_collect(args):
    out_dir = os.path.abspath(args.out_dir)
    pieces = load_pieces(out_dir)
    formulas = [args.formula] if args.formula else sorted({p["case"]["formula"] for p in pieces.values() if p["case"]["formula"] not in ("B0",)} or {"B0"})
    for formula in formulas:
        rows = bl_table(pieces, formula)
        bl5 = bl5_table(pieces, formula)
        summary = {k: all_passed(rows, k) for k in ("bl1_a_pass", "bl1_b_pass", "bl2_pass", "bl3_pass", "bl4_pass", "bl4_target_f32_pass")}
        summary["bl5_pass"] = (bool(bl5) and all(not r.get("missing") and all(r["passed"].values()) for r in bl5), len(bl5))
        res = dict(formula=formula, prereg="Docs/VolumeFog3D/stage2/prereg-blend.md", thresholds=THRESHOLDS, shake=SHAKE, bl_rows=rows, bl5=bl5,
                   all_rows_pass=summary, ladder=ladder_table(pieces, formula), info=info_tables(pieces, out_dir, formula),
                   pieces=len(pieces), tools=blend_patch.tool_fingerprints(),
                   errors=[k for k, p in pieces.items() if p.get("error")])
        with open(os.path.join(out_dir, f"results_{formula}.json"), "w", encoding="utf-8") as fh:
            json.dump(res, fh, ensure_ascii=False, indent=1, default=str)
        write_markdown(os.path.join(out_dir, f"results_{formula}.md"), res)
        print(json.dumps(dict(formula=formula, bl_rows=len(rows), all_rows_pass=summary), ensure_ascii=False))
    return 0


def fmt(x):
    if isinstance(x, float):
        return f"{x:.4g}"
    if isinstance(x, dict):
        return "{" + ", ".join(f"{k}: {fmt(v)}" for k, v in x.items()) + "}"
    if isinstance(x, (list, tuple)):
        return "[" + ", ".join(fmt(v) for v in x) + "]"
    return str(x)


def write_markdown(path, res):
    t = res["thresholds"]
    lines = [f"# 바람 섞기 식 {res['formula']} — 필수 판정선 값 표 (fog3d_blend_gates.py collect)", "",
             "- 판정 문장은 쓰지 않는다. '통과' 열은 등록 4절 문턱을 기계적으로 댄 결과다.",
             f"- 문턱: BL-1 (가) ≤ {t['BL1_RATIO_MAX']} × 앞 구간, (나) ≤ {t['BL1_SHARE_MAX']} × 걷는 동안 / BL-2 걷기 ≤ {t['BL2_WALK_MAX']}, 칼 ≤ {t['BL2_BLADE_MAX']} / "
             f"BL-3 ≤ {t['BL3_MAX']} / BL-4 비 ∈ [{t['BL4_MIN']}, {t['BL4_MAX']}](직전 |차| ≤ {t['BL4_SKIP_CM_S']} 뺌) / BL-5 ≤ {t['BL5_FACTOR']} × B0 몸 2.0",
             f"- 모든 행 통과(값, 행 수): {fmt(res['all_rows_pass'])}", f"- 오류 조각: {res['errors'] or '없음'}", "",
             "- BL-4 는 두 읽기를 함께 적는다: '통과' = 목표를 64비트 G×목표 그대로 둔 비, '목표 f32' = 목표를 저장 형식(32비트)으로 둔 비. 넘김 글 ② 참고.",
             "| 장면 | s | G | 초당 | BL-1 (가) 비 | 통과 | BL-1 (나) 몫 | 통과 | BL-2 | 통과 | BL-3 | 통과 | BL-4 최소·최대 | 밖 면(그중 f32 간격 안) | 통과 | BL-4 목표 f32 최소·최대 | 통과 | T 끝 |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in res["bl_rows"]:
        lines.append(f"| {r['scene']} | {r['s']:g} | {r['G']:g} | {r['hz']} | {fmt(r.get('bl1_a', '-'))} | {r.get('bl1_a_pass', '-')} | {fmt(r.get('bl1_b', '-'))} | "
                     f"{r.get('bl1_b_pass', '-')} | {fmt(r['bl2'])} | {r['bl2_pass']} | {fmt(r['bl3'])} | {r['bl3_pass']} | {fmt(r.get('bl4_min', '-'))}·{fmt(r.get('bl4_max', '-'))} | "
                     f"{r.get('bl4_outside', '-')}({r.get('bl4_outside_within_f32_spacing', '-')}) | {r.get('bl4_pass', '-')} | "
                     f"{fmt(r.get('bl4_target_f32_min', '-'))}·{fmt(r.get('bl4_target_f32_max', '-'))} | {r.get('bl4_target_f32_pass', '-')} | {fmt(r['T_end'])} |")
    lines += ["", "## BL-5 흔들기", "", "| 초당 | 잰 걸음 | 후보 u·v·w | B0 몸 2.0 u·v·w | 비 | 통과 |", "|---|---|---|---|---|---|"]
    for r in res["bl5"]:
        if r.get("missing"):
            lines.append(f"| {r['hz']} | 조각 없음 | | | | |")
            continue
        lines.append(f"| {r['hz']} | {r['step']} | {fmt(r['candidate'])} | {fmt(r['b0_s2'])} | {fmt(r['ratio'])} | {fmt(r['passed'])} |")
    lines += ["", "## 3절 자국 지표 T 사다리(초당 60걸음, 128칸, 걷기 끝 두 걸음·칼 휘두름 끝 두 걸음 평균)", ""]
    for scene, block in res["ladder"].items():
        lines.append(f"- {scene}: {fmt(block)}")
    lines += ["", "## 정보(5절)", ""] + [f"- {k}: {fmt(v)}" for k, v in res["info"].items()]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run")
    run.add_argument("--formula", required=True, choices=blend_patch.FORMULAS)
    run.add_argument("--out-dir", required=True)
    run.add_argument("--jobs", type=int, default=6)
    run.add_argument("--set", default="all", choices=("all", "bl", "ladder", "info", "shake", "smoke"))
    run.add_argument("--bl-gains", default="1,3")
    run.add_argument("--n", type=int, default=128, help="도구 시험 전용(판정에는 128)")
    col = sub.add_parser("collect")
    col.add_argument("--out-dir", required=True)
    col.add_argument("--formula", default="")
    col.add_argument("--n", type=int, default=128, help="도구 시험 전용(판정에는 128)")
    args = ap.parse_args(argv)
    args.out_dir = os.path.abspath(args.out_dir)
    GRID["n"] = args.n
    return cmd_run(args) if args.command == "run" else cmd_collect(args)


if __name__ == "__main__":
    sys.exit(main())
