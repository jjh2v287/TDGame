"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17, 저장소 루트에서 실행
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_selftest.py [시험 이름 ...] [--out <json>]   (이름 없으면 전부, k2 = 식 3판 시험만)
출력: 시험별 결과 JSON(기본 Saved/AgentOps/fog3d_ref/selftest.json, 시험별 키로 합쳐 씀) + 표준 출력 요약
상태: 기준 모의 자체 시험, 2026-11-13 X2a 가 selftest_core2.py 에서 올림. 옛 시험은 옛 기본값(A3·S4·식 2판·REF·부풀리기 끔)을 그대로 쓰고 pivot_guard_shaft·path_t32_tie 를 더함
"""
import json
import math
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fog3d_core as c  # noqa: E402

OUT = os.path.join(c.ROOT, "Saved", "AgentOps", "fog3d_ref", "selftest.json")
BOX_SMALL = ((60.0, -75.0, -1.0), (210.0, 75.0, 180.0))
LEGACY_DEFAULTS = dict(advect="A3", solid="S4", diff_formula=2, solver="REF", inflate=False)


def legacy_params(**kw):
    return c.Params(**dict(LEGACY_DEFAULTS, **kw))


def save(key, value, path=OUT):
    data = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    data[key] = value
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=1, ensure_ascii=False)


def field_max(sim):
    return max(float(np.abs(a).max()) for a in (sim.u, sim.v, sim.w, sim.D, sim.pr))


def test_static():
    cases = [
        dict(name="U8_A3_S4_P2_box_stain_follow", layers="U8", advect="A3", solid="S4", solver="P2", follow=True),
        dict(name="S8_A1_S2_P6_box_stain", layers="S8", advect="A1", solid="S2", solver="P6", follow=False),
        dict(name="U8_A2_S3_REF_box_formula1", layers="U8", advect="A2", solid="S3", solver="REF", follow=False, diff_formula=1),
        dict(name="S8_A3_S1_P4_box_stain_follow", layers="S8", advect="A3", solid="S1", solver="P4", follow=True),
    ]
    rows = {}
    for cs in cases:
        params = legacy_params(nx=48, ny=48, layers=cs["layers"], advect=cs["advect"], solid=cs["solid"], solver=cs["solver"],
                          stain_amp=0.3, diff_formula=cs.get("diff_formula", 2))
        sim = c.Sim(params, boxes=[BOX_SMALL])
        moves = 0
        for n in range(600):
            origin = sim.origin
            sim.step(follow=(600.0 * n * params.dt, -300.0 * n * params.dt, 60.0) if cs["follow"] else None)
            moves += origin != sim.origin
        rows[cs["name"]] = dict(max_abs=field_max(sim), window_moves=moves, ok=field_max(sim) == 0.0)
    return rows, all(r["ok"] for r in rows.values())


def swing_inputs(sim_dt, n, height=45.0, speed=800.0, length=100.0, radius=20.0):
    y0 = -75.0 + speed * sim_dt * n
    y1 = y0 + speed * sim_dt
    return c.capsule_spheres((-length / 2, y0, height), (length / 2, y0, height), (-length / 2, y1, height),
                             (length / 2, y1, height), radius, velocity=(0.0, speed, 0.0))


def run_swing(anchor, steps=18, **kw):
    params = legacy_params(nx=64, ny=64, layers="U8", advect="A3", solid="S4", solver="P2", **kw)
    sim = c.Sim(params, anchor=anchor, boxes=[BOX_SMALL])
    for n in range(steps):
        sim.step(swing_inputs(params.dt, n) if n * 800.0 * params.dt < 150.0 else ())
    return sim


def test_determinism():
    h = [run_swing((0, 0)).state_hash(True) for _ in range(2)]
    return dict(hashes=h), h[0] == h[1]


def test_far_origin():
    rows = {}
    for anchor in ((0, 0), (1000000, 1000000), (-1000000, -1000000), (1000000, -1000000)):
        sim = run_swing(anchor)
        rows[str(anchor)] = dict(hash=sim.state_hash(True), origin=sim.origin, maxD=float(np.abs(sim.D).max()))
    hs = {r["hash"] for r in rows.values()}
    return rows, len(hs) == 1


def test_ref_blocked():
    rows = {}
    ok = True
    for layers in ("U8", "S8"):
        params = legacy_params(nx=64, ny=64, layers=layers, advect="A3", solid="S4", solver="REF")
        sim = c.Sim(params, boxes=[BOX_SMALL])
        west_edge = -sim.nx // 2 * params.hx
        inp = [c.still_sphere((west_edge + 4 * params.hx, 0.0, 75.0), 50.0, (-500.0, 0.0, 0.0)),
               c.still_sphere((0.0, 0.0, 75.0), 50.0, (500.0, 0.0, 0.0))]
        Rs = []
        for n in range(3):
            sim.step(inp)
            Rs.append(sim.last["R"])
        r = dict(R=Rs, cycles=sim.last["cycles"], blocked_face_max=sim.blocked_face_max(), seam_face_max=sim.seam_face_max(),
                 wind_max=sim.max_wind())
        r["ok"] = max(Rs) <= 1e-3 and r["blocked_face_max"] == 0.0 and r["seam_face_max"] == 0.0
        ok &= r["ok"]
        rows[layers] = r
    for layers in ("U8", "S8"):
        sim = c.t_test_sim(layers, 128, "T3")
        res = c.t_test_R(sim, c.SOLVER_DEFAULTS["REF"])
        res["ok"] = res["R"] <= 1e-3 and res["blocked"] == 0.0
        ok &= res["ok"]
        rows["T3_128_" + layers] = res
    return rows, ok


def test_window_move():
    params = legacy_params(nx=64, ny=64, layers="S8")
    sim = c.Sim(params, boxes=[BOX_SMALL])
    rng = np.random.default_rng(1)
    for name in ("u", "v", "w", "D", "pr"):
        setattr(sim, name, rng.standard_normal(sim.u.shape).astype(np.float32))
    sim.zero_closed_faces()
    old = {n: getattr(sim, n).copy() for n in ("u", "v", "w", "D", "pr")}
    old_solid = sim.solid.copy()
    sim.set_window((sim.origin[0] + 16, sim.origin[1] - 32))
    band = sim.new_band_mask(16, -32)
    new_band_max = max(float(np.abs(getattr(sim, n)[band]).max()) for n in old)
    keep = ~band
    moved_ok = True
    for n in ("D", "pr"):
        shifted = np.zeros_like(old[n])
        shifted[:-16, 32:] = old[n][16:, :-32]
        cmp = keep[:, :, None] & ~sim.solid & ~np.roll(np.roll(old_solid, -16, 0), 32, 1)
        moved_ok &= bool(np.array_equal(getattr(sim, n)[cmp], shifted[cmp]))
    res = dict(new_band_max=new_band_max, seam_face_max=sim.seam_face_max(), kept_values_shifted=moved_ok, overlap=sim.overlap)
    sim.step()
    res["after_step_new_band_D_max"] = float(np.abs(sim.D[band]).max())
    res["note"] = "새 띠는 들어온 계산의 옮기기 결과를 0 으로 둔다(k1a-core.md 해석 1). 한 번 계산 뒤 값은 되돌림·띠 감쇠만 받은 0 이어야 한다."
    return res, new_band_max == 0.0 and res["seam_face_max"] == 0.0 and moved_ok


def test_nowind():
    params = legacy_params(nx=48, ny=48, layers="U8", mode="nowind")
    sim = c.Sim(params, boxes=[BOX_SMALL])
    sim.D = np.where(sim.fluid, -0.5 * sim.Bc, 0.0).astype(np.float32)
    d0 = sim.D.astype(np.float64).copy()
    inp = [c.still_sphere((-200.0, 0.0, 45.0), 40.0, (0.0, 0.0, 0.0), erase_per_sec=30.0)]
    for _ in range(30):
        sim.step(inp)
    rx, ry, rz = sim.center_rel()
    dist2 = (rx[:, None, None] + 200.0) ** 2 + ry[None, :, None] ** 2 + (rz[None, None, :] - 45.0) ** 2
    far = sim.fluid & (dist2 > 60.0 ** 2)
    expect = d0 * math.exp(-params.diff_relax_per_sec * params.dt) ** 30
    rel = float(np.abs(sim.D[far] - expect[far]).max())
    erased = float(sim.total_density()[:, :, 1].min())
    res = dict(wind_max=sim.max_wind(), far_relax_abs_err=rel, min_total_layer1=erased,
               min_total_far=float(sim.total_density()[far & (rz[None, None, :] < 60.0)].min()))
    return res, res["wind_max"] == 0.0 and rel < 1e-6 and erased < 0.5 * res["min_total_far"]


def run_empty(layers, advect, solid, formula, band_on, steps=30):
    params = legacy_params(nx=48, ny=48, layers=layers, advect=advect, solid=solid, diff_formula=formula, pressure=False,
                      diff_relax_per_sec=0.0, band_on=band_on)
    sim = c.Sim(params)
    sim.empty_all()
    for n in range(steps):
        sim.set_wind((0.0, 0.0, 100.0 * math.sin(2 * math.pi * n * params.dt / 0.25)))
        sim.step()
    tot = sim.total_density()
    inner = tot[8:-8, 8:-8]
    return float(tot.max()), float(inner.max())


def test_empty_vertical():
    rows = {}
    ok = True
    for layers in ("U8", "S8"):
        for advect, solid in (("A3", "S4"), ("A1", "S0")):
            for formula in (2, 1):
                whole, inner = run_empty(layers, advect, solid, formula, False)
                whole_b, inner_b = run_empty(layers, advect, solid, formula, True)
                key = f"{layers}_{advect}_{solid}_f{formula}"
                rows[key] = dict(max_total_band_off=whole, max_total_band_on_inner=inner_b, max_total_band_on_whole=whole_b)
                if formula == 2:
                    ok &= whole <= 1e-6 and inner_b <= 1e-6
    return rows, ok


def test_thin_layer_weight():
    rng = np.random.default_rng(3)
    pts = rng.uniform(-60, 60, (3, 20000))
    a = np.array([0.0, 0.0, 0.0])
    b = np.array([30.0, 10.0, 5.0])
    r = 40.0
    W = c.band_weight(pts[0], pts[1], pts[2], 1e-4, a, b, r)
    seg = b - a
    t = np.clip(((pts.T - a) @ seg) / (seg @ seg), 0, 1)
    d2 = ((pts.T - (a + t[:, None] * seg)) ** 2).sum(1)
    Wp = c.point_weight(d2, r)
    err = float(np.abs(W - Wp).max())
    Wt = c.band_weight(pts[0], pts[1], pts[2], 30.0, a, b, r)
    return dict(max_abs_diff_dz_tiny=err, max_W_dz30=float(Wt.max())), err <= 1e-6


def test_solvers_T1():
    rows = {}
    ok = True
    for layers in ("U8", "S8"):
        sim = c.t_test_sim(layers, 128, "T1")
        for name in ("P1", "P2", "P3", "P4", "P5", "P6", "REF"):
            cfg = c.resolve_solver_cfg(legacy_params(layers=layers, solver=name))
            res = c.t_test_R(sim, cfg)
            res["cfg"] = cfg
            rows[f"{layers}_{name}"] = res
            if name != "REF":
                ok &= res["R"] < 1.0
        if layers == "U8":
            try:
                res = c.t_test_R(sim, c.SOLVER_DEFAULTS["P7"])
                rows["U8_P7"] = res
            except Exception as exc:
                rows["U8_P7"] = dict(error=repr(exc))
    return rows, ok


def test_speed():
    rows = {}
    for nx, ny, layers, solver, advect, solid in ((128, 128, "U8", "P2", "A3", "S4"), (128, 128, "U8", "P2", "A1", "S0"),
                                                  (128, 128, "S8", "P2", "A3", "S4"), (128, 128, "U8", "REF", "A3", "S4"),
                                                  (512, 128, "U8", "P2", "A3", "S4"), (512, 128, "U8", "REF", "A3", "S4"),
                                                  (128, 128, "U32", "P2", "A3", "S4"), (128, 128, "U32", "REF", "A3", "S4")):
        params = legacy_params(nx=nx, ny=ny, layers=layers, solver=solver, advect=advect, solid=solid)
        sim = c.Sim(params, boxes=[c.T3_BOX])
        inp = swing_inputs(params.dt, 0)
        sim.step(inp)
        ts = []
        for n in range(1, 3):
            t0 = time.perf_counter()
            sim.step(swing_inputs(params.dt, n))
            ts.append(time.perf_counter() - t0)
        rows[f"{nx}x{ny}_{layers}_{solver}_{advect}_{solid}"] = dict(sec_per_step=min(ts), R=sim.last["R"], cost=sim.last["cost"],
                                                                     cycles=sim.last["cycles"])
    return rows, rows["128x128_U8_P2_A3_S4"]["sec_per_step"] <= 0.5


# ---------------- K2: 식 3판(밀린 거리)·새 깎기 정의 ----------------

OUT_K2 = OUT
V3_STATE = ("u", "v", "w", "D", "pr", "dpx", "dpy", "dpz", "g")


def v3_max(sim):
    return max(float(np.abs(getattr(sim, n)).max()) for n in V3_STATE)


def test_v3_static():
    cases = [
        dict(name="U8_A3_S4_P2_box_stain_follow", layers="U8", advect="A3", solid="S4", solver="P2", follow=True),
        dict(name="S8_A1_S2_P6_box_stain", layers="S8", advect="A1", solid="S2", solver="P6", follow=False),
        dict(name="U8_A2_S3_REF_box", layers="U8", advect="A2", solid="S3", solver="REF", follow=False),
        dict(name="S8_A3_S1_P4_box_stain_fine_follow", layers="S8", advect="A3", solid="S1", solver="P4", follow=True, fine=0.2),
        dict(name="U8_A3_S0_REF_box_follow", layers="U8", advect="A3", solid="S0", solver="REF", follow=True),
    ]
    rows = {}
    for cs in cases:
        params = legacy_params(nx=48, ny=48, layers=cs["layers"], advect=cs["advect"], solid=cs["solid"], solver=cs["solver"],
                          stain_amp=0.3, diff_formula=3, fine_amp=cs.get("fine", 0.0))
        sim = c.Sim(params, boxes=[BOX_SMALL])
        moves = 0
        for n in range(600):
            origin = sim.origin
            sim.step(follow=(600.0 * n * params.dt, -300.0 * n * params.dt, 60.0) if cs["follow"] else None)
            moves += origin != sim.origin
        rows[cs["name"]] = dict(max_abs=v3_max(sim), window_moves=moves, ok=v3_max(sim) == 0.0)
    return rows, all(r["ok"] for r in rows.values())


def run_swing_v3(anchor, steps=18, erase=30.0, **kw):
    params = legacy_params(nx=64, ny=64, layers="U8", advect="A3", solid="S4", solver="P2", diff_formula=3, **kw)
    sim = c.Sim(params, anchor=anchor, boxes=[BOX_SMALL])
    for n in range(steps):
        ins = swing_inputs(params.dt, n) if n * 800.0 * params.dt < 150.0 else []
        for s in ins:
            s.erase_per_sec = erase
        sim.step(ins)
    return sim


def test_v3_determinism():
    h = [run_swing_v3((0, 0)).state_hash(True) for _ in range(2)]
    return dict(hashes=h), h[0] == h[1]


def test_v3_far_origin():
    rows = {}
    for anchor in ((0, 0), (1000000, 1000000), (-1000000, -1000000), (1000000, -1000000)):
        sim = run_swing_v3(anchor)
        rows[str(anchor)] = dict(hash=sim.state_hash(True), origin=sim.origin, maxD=float(np.abs(sim.D).max()), state=sim.state_max())
    hs = {r["hash"] for r in rows.values()}
    return rows, len(hs) == 1


def relax_off(**kw):
    return dict(diff_relax_per_sec=0.0, disp_relax_h_per_sec=0.0, disp_relax_v_per_sec=0.0, fill_relax_per_sec=0.0, **kw)


def test_v3_empty_vertical():
    rows = {}
    ok = True
    for layers in ("U8", "S8"):
        for advect, solid in (("A3", "S4"), ("A1", "S0")):
            params = legacy_params(nx=48, ny=48, layers=layers, advect=advect, solid=solid, diff_formula=3, pressure=False, band_on=False,
                              **relax_off())
            sim = c.Sim(params)
            sim.empty_all()
            for n in range(30):
                sim.set_wind((0.0, 0.0, 100.0 * math.sin(2 * math.pi * n * params.dt / 0.25)))
                sim.step()
            m = float((sim.total_density() * sim.fluid).max())
            rows[f"{layers}_{advect}_{solid}"] = dict(max_total=m, g_min=float(sim.g.min()), g_max=float(sim.g.max()))
            ok &= m <= 1e-6
    return rows, ok


def test_v3_shake_return():
    """고르게 위아래로 흔든 뒤(2주기, 압력·되돌림·띠 끔) d 가 0 근처로: 바닥·맨 위 층을 뺀 층의 |d| 최대 ≤ 진행 중 최대의 5%(실행 전에 정한 기준)."""
    rows = {}
    ok = True
    for layers in ("U8", "S8"):
        params = legacy_params(nx=48, ny=48, layers=layers, advect="A3", solid="S4", diff_formula=3, pressure=False, band_on=False, **relax_off())
        sim = c.Sim(params)
        peak = 0.0
        for n in range(30):
            sim.set_wind((0.0, 0.0, 100.0 * math.sin(2 * math.pi * n * params.dt / 0.25)))
            sim.step()
            peak = max(peak, float(np.abs(sim.dpz).max()))
        inner = float(np.abs(sim.dpz[:, :, 1:-1]).max())
        whole = float(np.abs(sim.dpz).max())
        rows[layers] = dict(peak_dz=peak, end_inner_dz=inner, end_whole_dz=whole, end_absdiff=float(np.abs(sim.D).max()),
                            end_dz_by_layer=[float(np.abs(sim.dpz[:, :, k]).max()) for k in range(sim.nz)])
        ok &= inner <= 0.05 * peak
    return rows, ok


def test_v3_baseline_independent():
    """평소 곡선(기본 농도·두께·흐림)·얼룩·잔무늬를 바꿔도 d·g·바람이 비트 단위로 같다(상태 해시는 u·v·w·d·g·p)."""
    variants = dict(base=dict(), dense=dict(base_density=0.5, thickness_cm=120.0, top_blur_cm=30.0), stain=dict(stain_amp=0.3),
                    fine=dict(fine_amp=0.2))
    rows = {k: run_swing_v3((0, 0), **kw).state_hash(True) for k, kw in variants.items()}
    return rows, len(set(rows.values())) == 1


def test_v3_nowind():
    params = legacy_params(nx=48, ny=48, layers="U8", mode="nowind", diff_formula=3)
    sim = c.Sim(params, boxes=[BOX_SMALL])
    sim.g = np.where(sim.fluid, -0.5, 0.0).astype(np.float32)
    inp = [c.still_sphere((-200.0, 0.0, 45.0), 40.0, (0.0, 0.0, 0.0), erase_per_sec=30.0)]
    for _ in range(30):
        sim.step(inp)
    rx, ry, rz = sim.center_rel()
    dist2 = (rx[:, None, None] + 200.0) ** 2 + ry[None, :, None] ** 2 + (rz[None, None, :] - 45.0) ** 2
    far = sim.fluid & (dist2 > 60.0 ** 2)
    expect = -0.5 * math.exp(-0.3 * params.dt) ** 30
    err = float(np.abs(sim.g[far] - expect).max())
    res = dict(wind_max=sim.max_wind(), d_max=sim.state_max()["d"], far_g_err=err, g_min=float(sim.g.min()))
    return res, res["wind_max"] == 0.0 and res["d_max"] == 0.0 and err < 1e-6 and res["g_min"] < -0.9


def test_v3_window_move():
    params = legacy_params(nx=64, ny=64, layers="S8", diff_formula=3)
    sim = c.Sim(params, boxes=[BOX_SMALL])
    rng = np.random.default_rng(1)
    for name in ("u", "v", "w", "pr", "dpx", "dpy", "dpz", "g"):
        setattr(sim, name, rng.standard_normal(sim.u.shape).astype(np.float32))
    sim.zero_closed_faces()
    sim._zero_solid_state()
    old = {n: getattr(sim, n).copy() for n in ("dpx", "dpy", "dpz", "g")}
    old_solid = sim.solid.copy()
    sim.set_window((sim.origin[0] + 16, sim.origin[1] - 32))
    band = sim.new_band_mask(16, -32)
    new_band_max = max(float(np.abs(getattr(sim, n)[band]).max()) for n in old)
    keep = ~band
    moved_ok = True
    for n in old:
        shifted = np.zeros_like(old[n])
        shifted[:-16, 32:] = old[n][16:, :-32]
        cmp = keep[:, :, None] & ~sim.solid & ~np.roll(np.roll(old_solid, -16, 0), 32, 1)
        moved_ok &= bool(np.array_equal(getattr(sim, n)[cmp], shifted[cmp]))
    sim.step()
    after = max(float(np.abs(getattr(sim, n)[band]).max()) for n in old)
    res = dict(new_band_max=new_band_max, kept_values_shifted=moved_ok, after_step_new_band_max=after,
               solid_state_max=max(float(np.abs(getattr(sim, n)[sim.solid]).max()) for n in old))
    return res, new_band_max == 0.0 and moved_ok and after == 0.0 and res["solid_state_max"] == 0.0


def test_erase_new_def():
    """덧붙임 1절 깎기: 반지름 20cm·높이 45cm(U8) 에서 W>0 칸이 모두 깎이는지, 멈춘 붓 = dt·W, 느린 붓 → 멈춘 붓에 수렴."""
    params = legacy_params(nx=48, ny=48, layers="U8")
    sim = c.Sim(params)
    X, Y, Z, R, _ = sim.location("c")
    a = sim.to_local((0.0, 0.0, 45.0))
    Wpos = c.band_weight(X, Y, Z, R, a, a, 20.0) > 0
    D_still = c.dwell_amount(X, Y, Z, R, a, a, 20.0, params.dt)
    b_slow = a + np.array([0.004, 0.0, 0.0])
    D_slow = c.dwell_amount(X, Y, Z, R, a, a + np.array([0.02, 0.0, 0.0]), 20.0, params.dt)
    b = a + np.array([800.0 * params.dt, 0.0, 0.0])
    D_move = c.dwell_amount(X, Y, Z, R, a, b, 20.0, params.dt)
    vol = (params.hx * params.hy * sim.dz)[None, None, :]
    target = params.dt * 32.0 * math.pi * 20.0 ** 3 / 105.0
    res = dict(W_positive_cells=int(Wpos.sum()), erased_cells_still=int((D_still > 0).sum()), erased_cells_move=int((D_move > 0).sum()),
               still_equals_dtW=bool(np.array_equal(D_still, params.dt * c.band_weight(X, Y, Z, R, a, a, 20.0))),
               slow_vs_still_rel=float(np.abs(D_slow - D_still).max() / D_still.max()),
               Q_still_over_formula=float((D_still * vol).sum() / target), Q_move_over_formula=float((D_move * vol).sum() / target),
               unused_b=float(b_slow[0]))
    ok = res["erased_cells_still"] == res["W_positive_cells"] and res["still_equals_dtW"] and res["slow_vs_still_rel"] < 1e-2
    return res, ok


def test_v3_speed():
    rows = {}
    for layers, solver in (("U8", "REF"), ("U8", "P2"), ("S8", "REF")):
        for formula in (3, 2):
            params = legacy_params(nx=128, ny=128, layers=layers, solver=solver, advect="A3", solid="S4", diff_formula=formula)
            sim = c.Sim(params, boxes=[c.T3_BOX])
            sim.step(swing_inputs(params.dt, 0))
            ts = []
            for n in range(1, 4):
                ins = swing_inputs(params.dt, n)
                for s in ins:
                    s.erase_per_sec = 30.0
                t0 = time.perf_counter()
                sim.step(ins)
                ts.append(time.perf_counter() - t0)
            rows[f"128x128_{layers}_{solver}_A3_S4_v{formula}"] = dict(sec_per_step=min(ts), R=sim.last["R"])
    return rows, rows["128x128_U8_REF_A3_S4_v3"]["sec_per_step"] <= 0.75


def test_pivot_guard_shaft():
    nx = ny = 32
    mask = np.zeros((nx, ny, 8), bool)
    mask[15:18, 15:18, :] = True
    mask[16, 16, :7] = False
    out = {}
    for guard in (True, False):
        sim = c.Sim(c.Params(nx=nx, ny=ny, layers="U8", pivot_guard=guard), obstacles=[dict(kind="cells", mask=mask)])
        sim.w[16, 16, 2] = 100.0
        with np.errstate(all="ignore"):
            sim._project()
        out["guard" if guard else "no_guard"] = dict(tiny_pivots=int(sim.tiny_pivot_count), finite=bool(np.isfinite(sim.pr).all()),
                                                    shaft_pressure_abs_max=float(np.abs(sim.pr[16, 16]).max()))
    return out, out["guard"]["finite"] and out["guard"]["tiny_pivots"] >= 1


def test_path_t32_tie():
    nx = ny = 64
    mask = np.zeros((nx, ny, 8), bool)
    probes = [(i, j) for i in range(2, 60, 3) for j in range(2, 60, 3)]
    for i, j in probes:
        mask[i + 1, j + 1, :] = True
    out = {}
    for local32 in (True, False):
        params = c.Params(nx=nx, ny=ny, layers="U8", path_t32=local32)
        sim = c.Sim(params, obstacles=[dict(kind="cells", mask=mask)])
        shape = sim.u.shape
        sim._path_clamp((np.full(shape, -700.0), np.full(shape, -700.0), np.zeros(shape)), params.dt)
        end_i, end_j = sim._end_cell[0], sim._end_cell[1]
        x_first = sum(int((end_i[i, j] == i + 1).all() and (end_j[i, j] == j).all()) for i, j in probes)
        y_first = sum(int((end_i[i, j] == i).all() and (end_j[i, j] == j + 1).all()) for i, j in probes)
        out["t32" if local32 else "t64"] = dict(probes=len(probes), x_first=x_first, y_first=y_first)
    return out, out["t32"]["x_first"] == out["t32"]["probes"]


TESTS = dict(static=test_static, determinism=test_determinism, far_origin=test_far_origin, ref_blocked=test_ref_blocked,
             window_move=test_window_move, nowind=test_nowind, empty_vertical=test_empty_vertical,
             thin_layer_weight=test_thin_layer_weight, solvers_T1=test_solvers_T1, speed=test_speed,
             v3_static=test_v3_static, v3_determinism=test_v3_determinism, v3_far_origin=test_v3_far_origin,
             v3_empty_vertical=test_v3_empty_vertical, v3_shake_return=test_v3_shake_return,
             v3_baseline_independent=test_v3_baseline_independent, v3_nowind=test_v3_nowind, v3_window_move=test_v3_window_move,
             erase_new_def=test_erase_new_def, v3_speed=test_v3_speed, pivot_guard_shaft=test_pivot_guard_shaft,
             path_t32_tie=test_path_t32_tie)
K2_TESTS = [k for k in TESTS if k.startswith("v3_") or k == "erase_new_def"]


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--out" in args:
        at = args.index("--out")
        OUT = OUT_K2 = os.path.abspath(args[at + 1])
        del args[at:at + 2]
    names = args or list(TESTS)
    if names == ["k2"]:
        names = K2_TESTS
    t_all = time.perf_counter()
    for name in names:
        t0 = time.perf_counter()
        detail, ok = TESTS[name]()
        dt = time.perf_counter() - t0
        if name in K2_TESTS:
            OUT_SAVE = OUT_K2
        else:
            OUT_SAVE = OUT
        save(name, dict(ok=bool(ok), seconds=round(dt, 1), detail=detail), OUT_SAVE)
        print(f"{name}: {'통과' if ok else '실패'} ({dt:.1f}s)", flush=True)
    print(f"전체 {time.perf_counter() - t_all:.1f}s")
