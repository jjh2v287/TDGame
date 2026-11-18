"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core·fog3d_replay·fog3d_replay_s2a·fog3d_terrain·fog3d_terrain_add2·fog3d_gates_s2a·fog3d_compare 를 import 만 한다
실행 명령: PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_blend_probe.py scenes | probe [--jobs N] [--n 128] | gpu-table [--gpu-dir Saved/FluidFX/Fog3D/gpu_blend]
출력: scenes = Tools/FluidFX/fog3d_ref/replays_blend/(재생 파일·manifest) + Saved/FluidFX/Fog3D/ref_blend/(기준 덤프), probe = Saved/AgentOps/20261116/fog3d/blend/probe.json·probe.md, gpu-table = 같은 폴더 gpu_table.json·gpu_table.md
상태: 3D 안개 바람 섞기 넘침(1단계 흠 ⑤)의 지금 식 특성 측정·GPU 재현용, 2026-11-16, 고친 식 없음
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

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fog3d_core as core  # noqa: E402
import fog3d_replay as rp  # noqa: E402
import fog3d_replay_s2a as rs  # noqa: E402
import fog3d_gates_s2a as gates_s2a  # noqa: E402
import fog3d_terrain_add2 as terrain_add2  # noqa: E402
import fog3d_compare as compare  # noqa: E402
from fog3d_terrain import TERRAINS  # noqa: E402

ROOT = core.ROOT
HX = rp.HX
SCENE = gates_s2a.SCENE
FLAT = TERRAINS["F0"]
REPLAYS = os.path.join(HERE, "replays_blend")
REF = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_blend")
GPU = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "gpu_blend")
OUT = os.path.join(ROOT, "Saved", "AgentOps", "20261116", "fog3d", "blend")
WALK_SECONDS = SCENE["walk_steps"] * SCENE["dt"]
STAND_SECONDS = 200 * SCENE["dt"]
BLADE_PATH = SCENE["g4_blades"]["CLIFF"]
BLADE_TAIL_SECONDS = 1.0
LAYER_CENTERS = 15.0 + 30.0 * np.arange(8)
GPU_SCENE = "blend_walk_stand"
GPU_DUMPS = {67: "w67", 68: "w68", 69: "w69", 70: "w70", 71: "s01", 72: "s02", 73: "s03", 74: "s04",
             267: "s197", 268: "s198", 269: "s199", 270: "s200"}


def walker(strength, dt):
    w = gates_s2a.Walker(FLAT, np.array((-SCENE["walk_back"], 0.0)), (1.0, 0.0), False, strength=strength, dt=dt)
    w.walk_steps = int(round(WALK_SECONDS / dt))
    w.total = w.walk_steps + int(round(STAND_SECONDS / dt))
    return w


def walk_stand_inputs(strength, dt):
    w = walker(strength, dt)
    return [w.next_inputs() for _ in range(w.total)], w.walk_steps


def blade_inputs(strength, dt):
    steps = int(math.ceil(float(np.linalg.norm(np.subtract(BLADE_PATH[1], BLADE_PATH[0]))) / (SCENE["blade_speed"] * dt) - 1e-9))
    out = []
    for k in range(steps):
        out.append([core.SweptSphere(s.a, s.b, s.radius, s.velocity, strength, s.erase_per_sec)
                    for s in gates_s2a.blade_inputs(FLAT, BLADE_PATH[0], BLADE_PATH[1], k, dt)])
    out += [[] for _ in range(int(round(BLADE_TAIL_SECONDS / dt)))]
    return out, steps


def footprint(spheres, origin, n):
    x = (origin[0] + np.arange(n) + 0.5) * HX
    y = (origin[1] + np.arange(n) + 0.5) * HX
    X, Y, Z = x[:, None, None], y[None, :, None], LAYER_CENTERS[None, None, :]
    mask = np.zeros((n, n, 8), bool)
    for s in spheres:
        mask |= np.broadcast_to(core.band_weight(X, Y, Z, 30.0, s.a, s.b, float(s.radius)) > 0, mask.shape)
    return mask


def center_speed(u, v, w):
    def mid(a, axis):
        lower = np.zeros_like(a, np.float64)
        index = [slice(None)] * 3
        index[axis] = slice(1, None)
        source = [slice(None)] * 3
        source[axis] = slice(None, -1)
        lower[tuple(index)] = a[tuple(source)]
        return 0.5 * (a.astype(np.float64) + lower)
    cu, cv, cw = mid(u, 0), mid(v, 1), mid(w, 2)
    return np.sqrt(cu * cu + cv * cv + cw * cw), cu


def field_metrics(fields, mask, fluid):
    speed, cu = center_speed(fields["u"], fields["v"], fields["w"])
    mask = mask & fluid
    disp = np.sqrt(fields["dx"].astype(np.float64) ** 2 + fields["dy"].astype(np.float64) ** 2 + fields["dz"].astype(np.float64) ** 2)
    face = max(float(np.abs(fields[k][fluid]).max()) for k in ("u", "v", "w"))
    return dict(ring_max=float(speed[mask].max()), ring_mean=float(speed[mask].mean()), ring_along=float(cu[mask].mean()), ring_along_max=float(cu[mask].max()), face_max=face,
                disp_max=float(disp[mask].max()), g_min=float(fields["g"][mask].min()), g_mean=float(fields["g"][mask].mean()))


class ProbeSim(terrain_add2.TerrainSimR5):
    watch = None
    watch_record = None

    def _apply_inputs(self, inputs):
        if self.watch is None:
            super()._apply_inputs(inputs)
            return
        before = {name: getattr(self, name).astype(np.float64).copy() for name in ("u", "v", "w")}
        factor = {name: np.ones(getattr(self, name).shape) for name in ("u", "v", "w")}
        for inp in inputs:
            a, b, _ = self._input_frame(inp)
            for name in ("u", "v", "w"):
                k = np.broadcast_to(self.blended_weight(name, slice(None), slice(None), a, b, float(inp.radius)) * float(inp.strength), factor[name].shape)
                factor[name] = factor[name] * (1.0 - k)
        super()._apply_inputs(inputs)
        self.watch_record = dict(before=before, after={name: getattr(self, name).astype(np.float64).copy() for name in ("u", "v", "w")}, factor=factor)


def make_sim(n, dt):
    r = rp.Replay("probe", n=n)
    r.body["tuning"] = dict(rs.TUNING_S2A)
    p = rp.build_params(r.finish(), dt)
    p.advect = "N2"
    return ProbeSim(p, FLAT, anchor=(0, 0), window_min=rp.default_window((0, 0), n))


def state_fields(sim):
    return dict(u=sim.u, v=sim.v, w=sim.w, dx=sim.dpx, dy=sim.dpy, dz=sim.dpz, g=sim.g)


def run_case(case):
    scene, strength, dt, n = case["scene"], case["strength"], case["dt"], case["n"]
    started = time.time()
    if scene == "walk_stand":
        steps, moving = walk_stand_inputs(strength, dt)
    elif scene == "blade":
        steps, moving = blade_inputs(strength, dt)
    else:
        steps, moving = walk_stand_inputs(strength, dt)
        steps = [[core.SweptSphere(s.b, s.b, s.radius, (0.0, 0.0, 0.0), s.strength, s.erase_per_sec) for s in steps[moving]] for _ in range(case.get("count", 20))]
        moving = 0
    sim = make_sim(n, dt)
    records, mask, watch = [], None, []
    watch_faces = None
    hand = None
    for index, spheres in enumerate(steps):
        if spheres:
            mask = footprint(spheres, sim.origin, n)
        standing = scene == "walk_stand" and index >= moving
        sim.watch = True if standing else None
        prev = {name: getattr(sim, name).astype(np.float64).copy() for name in ("u", "v", "w")} if standing else None
        sim.step(spheres)
        records.append(field_metrics(state_fields(sim), mask, sim.fluid))
        if not standing:
            continue
        rec = sim.watch_record
        if watch_faces is None:
            watch_faces = {}
            for name in ("u", "v", "w"):
                f = rec["factor"][name]
                flat = np.argsort(np.abs(f).ravel())[::-1][:3]
                watch_faces[name] = [tuple(int(c) for c in np.unravel_index(i, f.shape)) for i in flat]
        row = {}
        for name, faces in watch_faces.items():
            for face in faces:
                key = f"{name}{face}"
                row[key] = dict(prev=prev[name][face], before=rec["before"][name][face], after=rec["after"][name][face],
                                end=float(getattr(sim, name)[face]), factor=float(rec["factor"][name][face]))
        watch.append(row)
        if index == moving + (len(steps) - moving) * 3 // 4:
            err, scale = 0.0, 0.0
            for name in ("u", "v", "w"):
                predicted = rec["before"][name] * rec["factor"][name]
                err = max(err, float(np.abs(predicted - rec["after"][name]).max()))
                scale = max(scale, float(np.abs(rec["after"][name]).max()))
            kmax = {name: float((1.0 - rec["factor"][name]).max()) for name in ("u", "v", "w")}
            hand = dict(step=index + 1, max_abs_error=err, max_abs_after=scale, max_one_minus_factor=kmax,
                        faces_growing=int(sum((np.abs(rec["factor"][name]) > 1.0).sum() for name in ("u", "v", "w"))),
                        largest_abs_factor=float(max(np.abs(rec["factor"][name]).max() for name in ("u", "v", "w"))))
    return dict(case=case, moving=moving, records=records, watch=watch, hand=hand, seconds=round(time.time() - started, 1))


def autocorr_period(x):
    x = np.asarray(x, float) - np.mean(x)
    if not np.any(x):
        return None
    best, lag_best = -2.0, None
    for lag in range(1, min(40, len(x) // 2)):
        c = float(np.dot(x[:-lag], x[lag:]) / np.dot(x, x))
        if c > best:
            best, lag_best = c, lag
    return dict(lag=lag_best, corr=round(best, 3))


def alternation(x):
    x = np.asarray(x, float)
    return float(np.mean(np.abs(np.diff(x))) / np.mean(x)) if len(x) > 1 and np.mean(x) > 0 else 0.0


def osc_summary(x):
    x = np.asarray(x, float)
    lo, hi, mean = float(x.min()), float(x.max()), float(x.mean())
    return dict(mean=mean, lo=lo, hi=hi, amp=(hi - lo) / 2.0, rel_amp=(hi - lo) / 2.0 / mean if mean > 0 else 0.0, alt=alternation(x), period=autocorr_period(x))


def first_time(values, threshold, dt):
    for i, v in enumerate(values):
        if v >= threshold:
            return round((i + 1) * dt, 4)
    return None


def summarize(result):
    case, rec, moving = result["case"], result["records"], result["moving"]
    dt = case["dt"]
    key = lambda name: [r[name] for r in rec]
    out = dict(scene=case["scene"], strength=case["strength"], hz=round(1.0 / dt), n=case["n"], seconds=result["seconds"])
    if case["scene"] == "walk_stand":
        speed = SCENE["walk_speed"]
        warm = int(round(10 * SCENE["dt"] / dt))
        walk = rec[:moving]
        out["walk"] = dict(ring_max_over_speed=max(r["ring_max"] for r in walk) / speed, face_max_over_speed=max(r["face_max"] for r in walk) / speed,
                           alt_ring_max=alternation(key("ring_max")[warm:moving]), alt_face_max=alternation(key("face_max")[warm:moving]),
                           follow_63pct_s=first_time(key("ring_along")[:moving], 0.63 * speed, dt),
                           follow_63pct_max_cell_s=first_time(key("ring_along_max")[:moving], 0.63 * speed, dt),
                           along_at_0p1s=rec[int(round(0.1 / dt)) - 1]["ring_along"] / speed)
        stand = rec[moving:]
        third = len(stand) // 4
        out["stand"] = dict(ring_max=osc_summary(key("ring_max")[-third:]), ring_mean=osc_summary(key("ring_mean")[-third:]),
                            face_max=osc_summary(key("face_max")[-third:]), ring_max_q3=osc_summary(key("ring_max")[moving + 2 * third:moving + 3 * third]),
                            disp_max_last=rec[-1]["disp_max"], g_min_last=rec[-1]["g_min"],
                            disp_max_range=[min(key("disp_max")[-third:]), max(key("disp_max")[-third:])],
                            g_min_range=[min(key("g_min")[-third:]), max(key("g_min")[-third:])])
        out["series_ring_max"] = [round(v, 3) for v in key("ring_max")]
        out["series_ring_mean"] = [round(v, 3) for v in key("ring_mean")]
        out["series_face_max"] = [round(v, 3) for v in key("face_max")]
        out["series_disp_max"] = [round(v, 4) for v in key("disp_max")]
        out["series_g_min"] = [round(v, 5) for v in key("g_min")]
        out["loop"] = loop_factors(result["watch"], dt)
        out["hand"] = result["hand"]
    elif case["scene"] == "blade":
        speed = SCENE["blade_speed"]
        swing = rec[:moving]
        out["blade"] = dict(ring_max_over_speed=max(r["ring_max"] for r in swing) / speed, face_max_over_speed=max(r["face_max"] for r in swing) / speed,
                            alt_ring_max=alternation(key("ring_max")[:moving]), steps=moving,
                            tail_ring_max=[round(rec[moving + i]["ring_max"], 2) for i in (0, int(round(0.25 / dt)) - 1, len(rec) - moving - 1)],
                            g_min_end=rec[-1]["g_min"], disp_max_end=rec[-1]["disp_max"])
        out["series_ring_max"] = [round(v, 3) for v in key("ring_max")]
        out["series_face_max"] = [round(v, 3) for v in key("face_max")]
    else:
        out["rest"] = dict(face_max_all=max(key("face_max")), disp_max_all=max(key("disp_max")), g_min_all=min(key("g_min")))
    return out


def loop_factors(watch, dt):
    if not watch:
        return None
    last = watch[-50:]
    out = {}
    for key in last[0]:
        rows = [r[key] for r in last]
        def ratio(num, den):
            vals = [r[num] / r[den] for r in rows if abs(r[den]) > 1e-3]
            return [round(float(np.median(vals)), 4), round(float(np.min(vals)), 4), round(float(np.max(vals)), 4)] if vals else None
        out[key] = dict(factor=round(rows[-1]["factor"], 4), scalar_model=round(rows[-1]["factor"] * math.exp(-0.75 * dt), 4),
                        advect=ratio("before", "prev"), blend=ratio("after", "before"), pressure=ratio("end", "after"), whole_step=ratio("end", "prev"),
                        end_abs=[round(min(abs(r["end"]) for r in rows), 2), round(max(abs(r["end"]) for r in rows), 2)])
    return out


def cases(n):
    out = []
    for hz in (60, 30):
        dt = 1.0 / hz
        for s in (1.0, 2.0, 2.5, 3.0):
            out.append(dict(scene="walk_stand", strength=s, dt=dt, n=n))
            out.append(dict(scene="blade", strength=s, dt=dt, n=n))
    out.append(dict(scene="rest", strength=3.0, dt=1.0 / 60.0, n=64, count=20))
    return out


def cmd_probe(args):
    os.makedirs(OUT, exist_ok=True)
    todo = cases(args.n)
    todo.sort(key=lambda c: -(c["n"] ** 2) * (300 if c["scene"] == "walk_stand" else 80) * c["dt"] ** -1)
    summaries = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for result in pool.map(run_case, todo):
            s = summarize(result)
            print(json.dumps({k: v for k, v in s.items() if not k.startswith("series") and k != "loop"}, ensure_ascii=False))
            sys.stdout.flush()
            summaries.append(s)
    with open(os.path.join(OUT, "probe.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(summaries, fh, ensure_ascii=False, indent=1)
    write_probe_md(summaries)


def write_probe_md(summaries):
    lines = ["# 지금 식 특성 표 (fog3d_blend_probe.py probe)", "",
             "| 장면 | 세기 | 초당 | 걷기 최대(링)/속도 | 걷기 최대(면)/속도 | 번갈아(링) | 63% 시간 s | 서기 링최대 평균 | 진폭 | 상대 | 주기 | 서기 면최대 범위 | 서기 링평균 |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for s in summaries:
        if s["scene"] != "walk_stand":
            continue
        w, st = s["walk"], s["stand"]
        lines.append(f"| 걷다 멈춤 | {s['strength']} | {s['hz']} | {w['ring_max_over_speed']:.3f} | {w['face_max_over_speed']:.3f} | {w['alt_ring_max']:.3f} | {w['follow_63pct_s']} | "
                     f"{st['ring_max']['mean']:.1f} | {st['ring_max']['amp']:.2f} | {st['ring_max']['rel_amp']:.4f} | {st['ring_max']['period']} | "
                     f"{st['face_max']['lo']:.1f}~{st['face_max']['hi']:.1f} | {st['ring_mean']['mean']:.2f} |")
    lines += ["", "| 칼 | 세기 | 초당 | 최대(링)/속도 | 최대(면)/속도 | 번갈아 | 꼬리 링최대(첫·0.25s·끝) |", "|---|---|---|---|---|---|---|"]
    for s in summaries:
        if s["scene"] == "blade":
            b = s["blade"]
            lines.append(f"| 칼 | {s['strength']} | {s['hz']} | {b['ring_max_over_speed']:.3f} | {b['face_max_over_speed']:.3f} | {b['alt_ring_max']:.3f} | {b['tail_ring_max']} |")
    with open(os.path.join(OUT, "probe.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")


def build_gpu_scene():
    r = rs.new_replay(GPU_SCENE)
    steps, moving = walk_stand_inputs(SCENE["body_wind"], SCENE["dt"])
    for k, spheres in enumerate(steps, start=1):
        r.step(SCENE["dt"], spheres, dump=GPU_DUMPS.get(k))
    r.body["walk"] = dict(start_cm=[-SCENE["walk_back"], 0.0], heading=[1.0, 0.0], walk_steps=moving, stop_steps=len(steps) - moving,
                          speed_cm_s=SCENE["walk_speed"], strength=SCENE["body_wind"], note="2a 걷기 장면과 같은 걷기, 멈춘 뒤 200걸음 서 있기, 평지 F0")
    replay = r.finish()
    replay["source"] = dict(tool="fog3d_blend_probe.py", code=rs.code_fingerprint(rs.R5_CODE))
    image = rs.attach_ground(replay, "F0", "R5")
    problems = rp.check_replay(replay) + rs.check_ground(replay, image)
    if problems:
        raise ValueError(problems)
    return replay, image


def cmd_scenes(args):
    replay, image = build_gpu_scene()
    rs.write_replay(REPLAYS, replay, image)
    with open(os.path.join(REPLAYS, "manifest.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(dict(files=[dict(name=f"{GPU_SCENE}.json"), dict(name=replay["ground"]["file"])]), fh, ensure_ascii=False, indent=1)
    started = time.time()
    result = rs.run_replay(replay, image, REF)
    print(json.dumps(dict(steps=result["steps"], files=len(result["files"]), seconds=round(time.time() - started, 1)), ensure_ascii=False))


def ring_from_dump(fields, step_spheres, origin, n):
    spheres = rp.core_spheres(step_spheres)
    mask = footprint(spheres, origin, n)
    fluid = fields["fluid"] > 0.5 if "fluid" in fields else np.ones(mask.shape, bool)
    return field_metrics(fields, mask, fluid)


def group_ratio(ref, gpu, names, fluid):
    num = sum(float(np.sum((gpu[k][fluid].astype(np.float64) - ref[k][fluid].astype(np.float64)) ** 2)) for k in names)
    den = sum(float(np.sum(ref[k][fluid].astype(np.float64) ** 2)) for k in names)
    return math.sqrt(num) / math.sqrt(den) if den > 0 else (0.0 if num == 0 else float("inf"))


def cmd_gpu_table(args):
    with open(os.path.join(REPLAYS, f"{GPU_SCENE}.json"), encoding="utf-8") as fh:
        replay = json.load(fh)
    rows = []
    for step, dump in sorted(GPU_DUMPS.items()):
        ref = compare.load_ref(REF, GPU_SCENE, dump)
        gpu = compare.load_gpu(args.gpu_dir, GPU_SCENE, dump)
        origin = tuple(ref.meta["window_min"])
        n = int(ref.meta["n"])
        spheres = replay["steps"][step - 1]["spheres"]
        rf = dict(ref.fields)
        gf = dict(gpu.fields)
        fluid = rf["fluid"] > 0.5
        row = dict(dump=dump, step=step, ref=ring_from_dump(rf, spheres, origin, n), gpu=ring_from_dump(gf, spheres, origin, n),
                   wind=group_ratio(rf, gf, ("u", "v", "w"), fluid), disp=group_ratio(rf, gf, ("dx", "dy", "dz"), fluid), fill=group_ratio(rf, gf, ("g",), fluid),
                   fluid_mismatch=int(np.sum((rf["fluid"] > 0.5) != (gf["fluid"] > 0.5))))
        rows.append(row)
        print(json.dumps(dict(dump=dump, ref=round(row["ref"]["ring_max"], 3), gpu=round(row["gpu"]["ring_max"], 3), wind=row["wind"], disp=row["disp"], fill=row["fill"])))
    with open(os.path.join(OUT, "gpu_table.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(rows, fh, ensure_ascii=False, indent=1)
    lines = ["| 덤프 | 걸음 | 링최대 기준 | 링최대 GPU | 링평균 기준 | 링평균 GPU | 면최대 기준 | 면최대 GPU | 바람 장차 | 밀림 장차 | 채움 장차 | 유체 불일치 |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['dump']} | {r['step']} | {r['ref']['ring_max']:.3f} | {r['gpu']['ring_max']:.3f} | {r['ref']['ring_mean']:.3f} | {r['gpu']['ring_mean']:.3f} | "
                     f"{r['ref']['face_max']:.3f} | {r['gpu']['face_max']:.3f} | {r['wind']:.2e} | {r['disp']:.2e} | {r['fill']:.2e} | {r['fluid_mismatch']} |")
    with open(os.path.join(OUT, "gpu_table.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("scenes")
    probe = sub.add_parser("probe")
    probe.add_argument("--jobs", type=int, default=6)
    probe.add_argument("--n", type=int, default=128)
    table = sub.add_parser("gpu-table")
    table.add_argument("--gpu-dir", default=GPU)
    args = parser.parse_args()
    commands = {"scenes": cmd_scenes, "probe": cmd_probe, "gpu-table": cmd_gpu_table}
    commands[args.cmd](args)


if __name__ == "__main__":
    main()
