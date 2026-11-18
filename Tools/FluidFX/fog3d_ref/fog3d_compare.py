"""
실행 환경: Python 3.12 + numpy 2.2 (추가 설치 없음), 저장소 루트에서 실행. fog3d_core.py·fog3d_replay.py 와 같은 폴더
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_compare.py [--gpu Saved/FluidFX/Fog3D/gpu] [--ref Saved/FluidFX/Fog3D/ref] [--numbers <기준 숫자 JSON>] [--out Saved/FluidFX/Fog3D/compare/verdict.json] [--extras] | --make-numbers [--shake Saved/FluidFX/Fog3D/ref_shake] | --self-test
출력: 판정 표(표준 출력)와 판정 JSON. 종료 코드 0 = 모두 통과, 1 = 실패 있음, 2 = 덤프 없음·형식 오류. --make-numbers 는 기준 덤프에서 기준 숫자·민감도·장면별 X 를 뽑아 JSON 으로 씀
상태: 1a 통과 기준 초안 3판의 A3·A5·A8~A13·A15 판정(--extras 는 A4·A7 을 더함). 기준 숫자 JSON 은 prereg-1a.md 로 등록하기 전의 초안 값이다. 2026-11-13 X2b 작성
"""
import argparse
import hashlib
import json
import math
import os
import shutil
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fog3d_core as core  # noqa: E402

ROOT = core.ROOT
F32 = np.float32
DUMP_FIELDS = ("u", "v", "w", "dx", "dy", "dz", "g", "pressure", "divergence", "fluid")
DEFAULT_GPU = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "gpu")
DEFAULT_REF = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref")
DEFAULT_SHAKE = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_shake")
DEFAULT_OUT = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "compare", "verdict.json")
DEFAULT_NUMBERS = os.path.join(ROOT, "Docs", "VolumeFog3D", "measurements", "stage1a-ref", "prereg-1a-numbers.json")
DEFAULT_REPLAYS = os.path.join(HERE, "replays")
BAND_CELLS = 8
X_FLOOR, X_CEILING, X_SCALE = 0.002, 0.03, 3.0
CELL, LAYER = 15.625, 30.0

A3_SCENES = {"k1_downstrike": ("t0p5", False), "k2_swing_low": ("end", False), "k3_box_high": ("end", False), "k4_run": ("end", False),
             "k5_swing_dt30": ("end", False), "k6_swing_256": ("end", False), "k7_thin_swing": ("end", False), "k8_seam_west": ("end", True)}
A15_SCENES = {"k2_swing_low_mode1": "end", "k4_run_mode1": "end"}
A8_FIRST = [f"first_solve_t{i}" for i in range(1, 6)]
A8_MOVING = ("first_solve_moving_256", [f"r{k}" for k in range(51, 61)])
A10_ERASE_CASES = ("ga", "na", "da")
A10_ERASE_DTS = (60, 30, 144)
A12_SCENES = ("k8_seam_west", "seam_corner")
A13_THICK = ["leak_thick_n50", "leak_thick_n300", "leak_thick_n1000", "leak_thick_d45", "leak_thick_top100"]
A13_OTHER = ([f"leak_thin_{k}_dt{d}" for k in ("x3_v1000", "x3_v3000", "a45_normal_v3000", "up_v1000", "corner_v300") for d in (60, 30)]
             + [f"leak_stacked45_l099_up_dt{d}" for d in (60, 30)]
             + [f"leak_room_{m}_dt{d}" for m in ("X8_s075_xp", "sXs_s075_yp") for d in (20, 30)])
A4_SCENES = ("far_origin_p1e6", "far_origin_m1e6", "far_origin_p1e6_16")
A7_SCENES = {"k3_box_high": [f"s{k:02d}" for k in range(1, 10)] + ["end"], "k4_run": [f"s{k:02d}" for k in range(1, 10)] + ["end"]}


class DumpError(Exception):
    pass


# ---------------- 덤프 읽기·쓰기 ----------------

class Dump:
    def __init__(self, meta, fields, extra=None):
        self.meta, self.fields, self.extra = meta, fields, extra or {}
        self.n = int(meta["n"])


def load_ref(folder, scene, dump):
    path = os.path.join(folder, f"{scene}__{dump}.npz")
    if not os.path.isfile(path):
        raise DumpError(f"기준 덤프 없음: {path}")
    with np.load(path) as z:
        meta = json.loads(str(z["meta"]))
        fields = {k: np.array(z[k], F32) for k in meta["fields"]}
        extra = {k: np.array(z[k]) for k in z.files if k not in meta["fields"] and k != "meta"}
    return Dump(meta, fields, extra)


def load_gpu(folder, scene, dump):
    stem = os.path.join(folder, f"{scene}__{dump}")
    if not (os.path.isfile(stem + ".json") and os.path.isfile(stem + ".f32")):
        raise DumpError(f"GPU 덤프 없음: {stem}.json / .f32")
    try:
        with open(stem + ".json", encoding="utf-8") as fh:
            meta = json.load(fh)
        if meta.get("format") != "fog3d-dump-1" or not all(k in meta for k in ("scene", "dump", "n", "layers", "window_min", "step_index", "fields")):
            raise DumpError(f"덤프 JSON 형식 오류: {stem}.json")
        if any(name not in DUMP_FIELDS for name in meta["fields"]):
            raise DumpError(f"덤프 필드 이름 오류: {stem}.json")
        count = int(meta["n"]) * int(meta["n"]) * int(meta["layers"])
        raw = np.fromfile(stem + ".f32", dtype="<f4")
    except (OSError, ValueError, TypeError) as err:
        raise DumpError(f"덤프 읽기 실패 {stem}: {err}")
    if raw.size != count * len(meta["fields"]):
        raise DumpError(f"덤프 크기 불일치 {stem}.f32: 기대 {count * len(meta['fields'])}개, 실제 {raw.size}개")
    shape = (int(meta["n"]), int(meta["n"]), int(meta["layers"]))
    fields = {name: raw[i * count:(i + 1) * count].reshape(shape).astype(F32) for i, name in enumerate(meta["fields"])}
    return Dump(meta, fields)


def write_gpu_format(folder, scene, dump, ref):
    os.makedirs(folder, exist_ok=True)
    meta = dict(ref.meta)
    stem = os.path.join(folder, f"{scene}__{dump}")
    with open(stem + ".json", "w", encoding="utf-8") as fh:
        json.dump(meta, fh)
    np.concatenate([ref.fields[name].reshape(-1) for name in meta["fields"]]).astype("<f4").tofile(stem + ".f32")


def load_replay(folder, scene):
    path = os.path.join(folder, f"{scene}.json")
    if not os.path.isfile(path):
        raise DumpError(f"재생 파일 없음: {path}")
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def replay_step_of_dump(replay, dump):
    for step in replay["steps"]:
        if step.get("dump") == dump:
            return step
    raise DumpError(f"재생 파일 {replay['name']} 에 덤프 {dump} 없음")


# ---------------- 측정 ----------------

def fluid_of(d):
    return d.fields["fluid"] > 0.5


def inner_slice(band):
    return (slice(band, -band), slice(band, -band)) if band else (slice(None), slice(None))


def field_norm_diff(parts, fluid, band):
    si, sj = inner_slice(band)
    mask = fluid[si, sj]
    num = den = 0.0
    for ref_arr, test_arr in parts:
        r = np.where(mask, ref_arr[si, sj].astype(np.float64), 0.0)
        t = np.where(mask, test_arr[si, sj].astype(np.float64), 0.0)
        num += float(np.sum((t - r) ** 2))
        den += float(np.sum(r ** 2))
    if den == 0.0:
        return 0.0 if num == 0.0 else float("inf")
    return math.sqrt(num) / math.sqrt(den)


def field_diff(ref, test, band):
    fluid = fluid_of(ref)
    pick = lambda d, names: [d.fields[k] for k in names]  # noqa: E731
    out = {}
    for label, names in (("wind", ("u", "v", "w")), ("disp", ("dx", "dy", "dz")), ("fill", ("g",))):
        out[label] = field_norm_diff(list(zip(pick(ref, names), pick(test, names))), fluid, band)
    return out


def canonical_hash(d):
    solid = ~fluid_of(d)
    m = hashlib.sha256()
    for name in ("u", "v", "w", "dx", "dy", "dz", "g"):
        a = np.where(solid, F32(0), d.fields[name]) + F32(0)
        m.update(np.ascontiguousarray(a, F32).tobytes())
    return m.hexdigest()[:16]


def divergence(d):
    u, v, w = (d.fields[k].astype(np.float64) for k in ("u", "v", "w"))
    um, vm, wm = np.zeros_like(u), np.zeros_like(v), np.zeros_like(w)
    um[1:] = u[:-1]
    vm[:, 1:] = v[:, :-1]
    wm[:, :, 1:] = w[:, :, :-1]
    div = (u - um) / CELL + (v - vm) / CELL + (w - wm) / LAYER
    return np.where(fluid_of(d), div, 0.0)


def rms(v):
    return float(np.sqrt(np.mean(np.square(v, dtype=np.float64)))) if v.size else 0.0


def residual_ratio(d):
    fluid = fluid_of(d)
    before = rms(d.fields["divergence"].astype(np.float64)[fluid])
    after = rms(divergence(d)[fluid])
    return after / before if before > 0 else 0.0


def drawn_difference(d):
    sim = core.Sim(core.Params(nx=d.n, ny=d.n))
    sim.dpx, sim.dpy, sim.dpz, sim.g = (d.fields[k].astype(F32) for k in ("dx", "dy", "dz", "g"))
    sim.fluid = fluid_of(d)
    sim.refresh_diff()
    return sim.D.astype(np.float64)


def cell_volume():
    return CELL * CELL * LAYER


def sphere_of(replay, step_index=0):
    return replay["steps"][step_index]["spheres"][0]


def measure_failed_finite(d):
    return all(bool(np.isfinite(a).all()) for a in d.fields.values())


def m_a5(src):
    d = src("k1_downstrike", "t10p5")
    wind = max(float(np.abs(d.fields[k]).max()) for k in ("u", "v", "w"))
    disp = max(float(np.abs(d.fields[k]).max()) for k in ("dx", "dy", "dz"))
    return dict(finite=measure_failed_finite(d), wind_max=wind, disp_max=disp)


def m_a8_first(src, scene):
    return dict(R=residual_ratio(src(scene, "end")))


def m_a8_moving(src):
    rs = [residual_ratio(src(A8_MOVING[0], name)) for name in A8_MOVING[1]]
    return dict(R_last10=float(np.mean(rs)), R_each=rs)


def m_a9(src, replays):
    sphere = sphere_of(load_replay(replays, "brush_height"))
    formula = sphere["v_cm_s"][0] * 32.0 * math.pi * sphere["r_cm"] ** 3 / 105.0
    vol = cell_volume()
    p = np.array([float(src("brush_height", f"h{k:02d}").fields["u"].astype(np.float64).sum()) * vol for k in range(35)])
    mean = float(p.mean())
    return dict(mean_over_formula=mean / formula, max_rel_dev=float(np.abs(p / mean - 1).max()))


def dwell_total(d, rate):
    g = d.fields["g"].astype(np.float64)
    return float(np.sum(-np.log1p(g) / rate)) * cell_volume()


def m_a10_erase(src):
    out = {}
    for case in A10_ERASE_CASES:
        w = {dt: dwell_total(src(f"erase_{case}_dt{dt}", "end"), 30.0) for dt in A10_ERASE_DTS}
        rel = {str(dt): w[dt] / w[60] - 1.0 for dt in A10_ERASE_DTS if dt != 60}
        out[case] = dict(total_dt60=w[60], rel_vs_dt60=rel, max_abs_rel=max(abs(v) for v in rel.values()))
    return out


def m_a10_dwell(src, scene, replays):
    replay = load_replay(replays, scene)
    sphere = replay["steps"][0]["spheres"][0]
    dt = replay["steps"][0]["dt"]
    target = dt * 32.0 * math.pi * sphere["r_cm"] ** 3 / 105.0
    q = np.array([dwell_total(src(scene, f"h{k:02d}"), sphere["e_per_sec"]) for k in range(35)])
    mean = float(q.mean())
    return dict(mean_over_formula=mean / target, max_rel_dev=float(np.abs(q / mean - 1).max()))


def m_a11a(src):
    d = src("empty_hold", "end")
    return dict(max_one_plus_g=float(np.max(1.0 + d.fields["g"].astype(np.float64)[fluid_of(d)])))


def m_a11d(src):
    return dict(mid=float(np.abs(drawn_difference(src("vortex_return", "mid"))).max()), end=float(np.abs(drawn_difference(src("vortex_return", "end"))).max()))


def faces_max(d, mask_xy):
    m = mask_xy[:, :, None]
    return max(float(np.abs(np.where(m, d.fields[k], 0)).max()) for k in ("u", "v", "w"))


def m_a12(src, scene, replays):
    replay = load_replay(replays, scene)
    n = replay["grid"]["n"]
    first = replay["steps"][0]
    sphere = first["spheres"][0]
    ox = (replay["anchor_cell"][0] - first["window_min"][0]) * CELL
    oy = (replay["anchor_cell"][1] - first["window_min"][1]) * CELL
    cx, cy = sphere["a_cm"][0] + ox, sphere["a_cm"][1] + oy
    centers = (np.arange(n) + 0.5) * CELL
    near = (np.abs(centers - cx) <= 100.0)[:, None] & (np.abs(centers - cy) <= 100.0)[None, :]
    corner = abs(sphere["a_cm"][1] - 0.0) > 1.0
    bands = {}
    if corner:
        for name, (si, sj) in dict(se=(slice(n - 8, n), slice(0, 8)), nw=(slice(0, 8), slice(n - 8, n)), ne=(slice(n - 8, n), slice(n - 8, n))).items():
            m = np.zeros((n, n), bool)
            m[si, sj] = True
            bands[name] = m
    else:
        m = np.zeros((n, n), bool)
        m[n - 8:, :] = True
        bands["east"] = m
    d1, d3, de = src(scene, "s1"), src(scene, "s3"), src(scene, "end")
    near_first, near_third = faces_max(d1, near), faces_max(d3, near)
    wind3 = max(faces_max(d3, m) for m in bands.values())
    wind_end = max(faces_max(de, m) for m in bands.values())
    diff = np.abs(drawn_difference(de))
    absdiff = max(float(np.where(m[:, :, None], diff, 0).max()) for m in bands.values())
    return dict(ratio_after_inputs=wind3 / max(near_third, 1e-300), ratio_after_one_second=wind_end / max(near_first, 1e-300), band_absdiff_end=absdiff)


def m_a13(src, ref_src, scene, replays, kind):
    replay = load_replay(replays, scene)
    step = replay_step_of_dump(replay, "end")
    design = 1.0 - math.exp(-replay["tuning"]["fill_relax_per_sec"] * step["dt"])
    mask = ref_src(scene, "end").extra["measure_mask"]
    fill = 1.0 + src(scene, "end").fields["g"].astype(np.float64)
    picked = fill[mask]
    top = float(picked.mean()) if kind == "mean_fill_ratio" else float(picked.max())
    return dict(value=top / design, cells=int(mask.sum()))


def sphere_distance(points, a, b):
    a, b = np.asarray(a, float), np.asarray(b, float)
    seg = b - a
    l2 = float(seg @ seg)
    rel = points - a
    t = np.clip((rel @ seg) / l2, 0.0, 1.0) if l2 > 0 else np.zeros(len(points))
    return np.linalg.norm(rel - t[:, None] * seg[None, :], axis=1)


def new_band_untouched_cells(replay, dump):
    step = replay_step_of_dump(replay, dump)
    n = replay["grid"]["n"]
    wm, prev = step["window_min"], step["prev_window_min"]
    dx, dy = wm[0] - prev[0], wm[1] - prev[1]
    band = np.zeros((n, n), bool)
    if dx > 0:
        band[n - dx:, :] = True
    elif dx < 0:
        band[:-dx, :] = True
    if dy > 0:
        band[:, n - dy:] = True
    elif dy < 0:
        band[:, :-dy] = True
    rx = (wm[0] - replay["anchor_cell"][0] + np.arange(n) + 0.5) * CELL
    ry = (wm[1] - replay["anchor_cell"][1] + np.arange(n) + 0.5) * CELL
    zc = (np.arange(8) + 0.5) * LAYER
    pts = np.stack(np.meshgrid(rx, ry, zc, indexing="ij"), axis=-1).reshape(-1, 3)
    far = np.ones(len(pts), bool)
    for sp in step["spheres"]:
        far &= sphere_distance(pts, sp["a_cm"], sp["b_cm"]) > sp["r_cm"] + 2.0 * CELL
    return band[:, :, None] & far.reshape(n, n, 8)


# ---------------- 기준 숫자 만들기 ----------------

def code_fingerprint():
    out = {}
    for name in ("fog3d_core.py", "fog3d_replay.py", "fog3d_compare.py", "tuned_solvers.json"):
        with open(os.path.join(HERE, name), "rb") as fh:
            out[name] = hashlib.sha256(fh.read()).hexdigest()[:16]
    return out


def make_numbers(ref_dir, shake_dir, replays, out_path):
    src = lambda scene, dump: load_ref(ref_dir, scene, dump)  # noqa: E731
    fp = code_fingerprint()
    fp_short = hashlib.sha256(json.dumps(fp, sort_keys=True).encode()).hexdigest()[:16]

    def entry(value, method, scene, **extra):
        return dict(value=value, method=method, scene=scene, code=fp_short, **extra)

    numbers = dict(format="fog3d-prereg-1a-numbers-1", status="초안 값: prereg-1a.md 로 등록하기 전", code=fp)
    field_method = "장 차이 = (두 장의 차)의 제곱합 제곱근 ÷ (기준 장)의 제곱합 제곱근, 유체 칸만, 바람 u·v·w / 밀린 거리 dx·dy·dz / 채움 g 를 따로"
    a3 = {}
    for scene, (dump, with_band) in A3_SCENES.items():
        ref = src(scene, dump)
        shaken = load_ref(shake_dir, scene, dump)
        sens = field_diff(ref, shaken, 0 if with_band else BAND_CELLS)
        worst = max(sens.values())
        a3[scene] = dict(dump=dump, include_band=with_band, sensitivity=sens, x=min(X_CEILING, max(X_FLOOR, X_SCALE * worst)),
                         x_by_field={k: min(X_CEILING, max(X_FLOOR, X_SCALE * v)) for k, v in sens.items()},
                         method=field_method + (", 가장자리 띠 포함" if with_band else f", 가장자리 띠 {BAND_CELLS}칸 뺌") + f". 민감도 = 모든 입력 위치를 {1e-4}cm 흔든 실행과의 장 차이",
                         scene=scene, code=fp_short)
    numbers["a3"] = a3
    a5 = m_a5(src)
    numbers["a5"] = dict(wind_max=entry(a5["wind_max"], "10.5초 시점 u·v·w 절댓값 최댓값(상한 = 값 × 1.2 + 1e-4 cm/s)", "k1_downstrike", dump="t10p5"),
                         disp_max=entry(a5["disp_max"], "10.5초 시점 dx·dy·dz 절댓값 최댓값(상한 = 값 × 1.2 + 1e-4 cm)", "k1_downstrike", dump="t10p5"),
                         finite=a5["finite"], factor=1.2, wind_floor=1e-4, disp_floor=1e-4)
    a8 = {}
    for scene in A8_FIRST:
        r = m_a8_first(src, scene)["R"]
        a8[scene] = entry(r, "남은 몰림 비 R = rms(압력 뒤 몰림) ÷ rms(압력 앞 몰림), 유체 칸, 몰림 = (u[i]−u[i−1])/h + (v[j]−v[j−1])/h + (w[k]−w[k−1])/30", scene, dump="end",
                          tolerance=max(0.02 * r, 0.002), tolerance_rule="± max(기준값의 2%, 0.002)")
    mv = m_a8_moving(src)
    a8["first_solve_moving_256"] = entry(mv["R_last10"], "256×256, 61개 계산 가운데 51~60번째 계산의 R 평균(상한 = 값 × 1.2 + 0.005)", "first_solve_moving_256", dump="r51..r60",
                                         upper=mv["R_last10"] * 1.2 + 0.005, R_each=mv["R_each"])
    numbers["a8"] = a8
    a9 = m_a9(src, replays)
    numbers["a9"] = dict(max_rel_dev=entry(a9["max_rel_dev"], "높이 35개(30~200cm, 5cm 간격) 각각 P = Σ(u × 그 자리 부피), 출렁임 = max|P/평균 − 1|(상한 = max(값 × 3, 1e-5))", "brush_height"),
                         mean_over_formula=entry(a9["mean_over_formula"], "평균 P ÷ (V × 32π r³ / 105), V = 300 cm/s, r = 20 cm(허용 ± 0.01)", "brush_height"))
    a10 = m_a10_erase(src)
    numbers["a10"] = dict(erase_dt_difference={c: entry(a10[c]["max_abs_rel"], "머문 양 = −ln(1+g) ÷ 30, 전체 합 × 칸 부피. dt 1/30·1/144 의 합이 dt 1/60 의 합과 다른 비율의 최댓값(상한 0.0037)", f"erase_{c}_dt60/30/144",
                                                  rel_vs_dt60=a10[c]["rel_vs_dt60"], total_dt60=a10[c]["total_dt60"]) for c in A10_ERASE_CASES},
                          dwell={}, dev_floor=1e-5, dev_floor_note="출렁임 상한 = max(값 × 3, 1e-5). 기준 출렁임이 32비트 저장 반올림 수준이라 바닥값을 둔다(감독 결정, 등록 전)")
    for scene in ("dwell_still", "dwell_moving"):
        m = m_a10_dwell(src, scene, replays)
        numbers["a10"]["dwell"][scene] = dict(
            mean_over_formula=entry(m["mean_over_formula"], "높이 35개 각각 Q = Σ(−ln(1+g)÷30 × 칸 부피), 평균 Q ÷ (dt × 32π r³ / 105)(허용 ± 0.002)", scene),
            max_rel_dev=entry(m["max_rel_dev"], "출렁임 = max|Q/평균 − 1|(상한 = max(값 × 3, 1e-5))", scene))
    ma = m_a11a(src)
    md = m_a11d(src)
    numbers["a11"] = dict(empty_hold_limit=1e-6,
                          empty_hold_max_one_plus_g=entry(ma["max_one_plus_g"], "0.5초 뒤 유체 칸 1+g 의 최댓값(판정 ≤ 1e-6, 설계 판정선과 같은 기준)", "empty_hold", dump="end"),
                          vortex_end=entry(md["end"], "끝(0.5초) 차이값 절댓값 최댓값(판정 = 값 ± 0.002 이고 ≤ 0.04)", "vortex_return", dump="end"),
                          vortex_mid=entry(md["mid"], "가운데(0.25초) 차이값 절댓값 최댓값(정보. 0.05 보다 작으면 판정 불가)", "vortex_return", dump="mid"))
    a12 = {}
    for scene in A12_SCENES:
        m = m_a12(src, scene, replays)
        a12[scene] = entry(m, "ratio_* = 반대편 띠 면 바람 최댓값 ÷ 입력 근처 면 바람 최댓값(입력 직후는 3번째 계산 뒤, 1초 뒤는 첫 계산 뒤 값으로 나눔), band_absdiff_end = 끝 시점 띠의 차이값 절댓값 최댓값",
                           scene, dump="s1,s3,end")
    numbers["a12"] = a12
    a13 = {}
    for scene in A13_THICK + A13_OTHER:
        kind = load_replay(replays, scene)["measure"]["kind"]
        m = m_a13(src, src, scene, replays, kind)
        a13[scene] = entry(m["value"], ("재는 칸 1+g 의 평균" if kind == "mean_fill_ratio" else "재는 칸 1+g 의 최댓값") + " ÷ (1 − exp(−0.3 dt))", scene, dump="end", cells=m["cells"], kind=kind,
                           rule=("기준값 ± 0.02" if scene in A13_THICK else "≤ 1.15"))
    numbers["a13"] = a13
    a15 = {}
    for scene, dump in A15_SCENES.items():
        ref = src(scene, dump)
        shaken = load_ref(shake_dir, scene, dump)
        sens = field_diff(ref, shaken, BAND_CELLS)
        worst = max(sens.values())
        a15[scene] = dict(dump=dump, sensitivity=sens, x=min(X_CEILING, max(X_FLOOR, X_SCALE * worst)), method=field_method + f", 가장자리 띠 {BAND_CELLS}칸 뺌", scene=scene, code=fp_short)
    numbers["a15"] = a15
    hashes = {}
    for scene in ("far_origin_0",) + A4_SCENES:
        hashes[scene] = canonical_hash(src(scene, "end"))
    numbers["a4"] = dict(reference_hashes=hashes, all_equal=len(set(hashes.values())) == 1, method="정규 상태 해시(u v w dx dy dz g, 막힌 칸 0, −0 → +0, sha256 앞 16자)")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(numbers, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    return numbers


# ---------------- 판정 ----------------

class Table:
    def __init__(self):
        self.rows = []

    def add(self, crit, scene, what, ref, got, limit, passed, info=""):
        self.rows.append(dict(criterion=crit, scene=scene, what=what, ref=ref, gpu=got, limit=limit, passed=bool(passed), info=info))

    def print(self):
        def fmt(v):
            if isinstance(v, float):
                return f"{v:.6g}"
            return str(v)
        print(f"{'기준':<5}{'장면':<40}{'항목':<26}{'기준값':<14}{'GPU':<14}{'한계':<22}판정")
        for r in self.rows:
            print(f"{r['criterion']:<5}{r['scene']:<40}{r['what']:<26}{fmt(r['ref']):<14}{fmt(r['gpu']):<14}{fmt(r['limit']):<22}{'통과' if r['passed'] else '실패'}{(' ' + r['info']) if r['info'] else ''}")


def judge(gpu_dir, ref_dir, numbers, replays, extras):
    gsrc = lambda scene, dump: load_gpu(gpu_dir, scene, dump)  # noqa: E731
    rsrc = lambda scene, dump: load_ref(ref_dir, scene, dump)  # noqa: E731
    t = Table()
    for scene, spec in numbers["a3"].items():
        got = field_diff(rsrc(scene, spec["dump"]), gsrc(scene, spec["dump"]), 0 if spec["include_band"] else BAND_CELLS)
        for label, value in got.items():
            t.add("A3", scene, f"{label} 장 차이", "", value, f"≤ X {spec['x']:.4g}", value <= spec["x"])
    a5 = m_a5(gsrc)
    n5 = numbers["a5"]
    t.add("A5", "k1_downstrike", "무한대·숫자 아님 없음", "", a5["finite"], "참", a5["finite"])
    limit = n5["wind_max"]["value"] * n5["factor"] + n5["wind_floor"]
    t.add("A5", "k1_downstrike", "10.5초 바람 최댓값", n5["wind_max"]["value"], a5["wind_max"], f"≤ {limit:.6g}", a5["wind_max"] <= limit)
    limit = n5["disp_max"]["value"] * n5["factor"] + n5["disp_floor"]
    t.add("A5", "k1_downstrike", "10.5초 밀린 거리 최댓값", n5["disp_max"]["value"], a5["disp_max"], f"≤ {limit:.6g}", a5["disp_max"] <= limit)
    for scene in A8_FIRST:
        spec = numbers["a8"][scene]
        r = m_a8_first(gsrc, scene)["R"]
        t.add("A8", scene, "남은 몰림 비 R", spec["value"], r, f"± {spec['tolerance']:.4g}", abs(r - spec["value"]) <= spec["tolerance"])
    spec = numbers["a8"]["first_solve_moving_256"]
    r = m_a8_moving(gsrc)["R_last10"]
    t.add("A8", "first_solve_moving_256", "마지막 10번 R 평균", spec["value"], r, f"≤ {spec['upper']:.4g}", r <= spec["upper"])
    a9 = m_a9(gsrc, replays)
    n9 = numbers["a9"]
    limit = max(n9["max_rel_dev"]["value"] * 3.0, 1e-5)
    t.add("A9", "brush_height", "높이 출렁임", n9["max_rel_dev"]["value"], a9["max_rel_dev"], f"≤ {limit:.4g}", a9["max_rel_dev"] <= limit)
    t.add("A9", "brush_height", "평균 ÷ 식 값", n9["mean_over_formula"]["value"], a9["mean_over_formula"], "± 0.01", abs(a9["mean_over_formula"] - n9["mean_over_formula"]["value"]) <= 0.01)
    a10 = m_a10_erase(gsrc)
    for case in A10_ERASE_CASES:
        t.add("A10", f"erase_{case}", "시간 간격 사이 차", numbers["a10"]["erase_dt_difference"][case]["value"], a10[case]["max_abs_rel"], "≤ 0.0037", a10[case]["max_abs_rel"] <= 0.0037)
    for scene, spec in numbers["a10"]["dwell"].items():
        m = m_a10_dwell(gsrc, scene, replays)
        t.add("A10", scene, "평균 ÷ 식 값", spec["mean_over_formula"]["value"], m["mean_over_formula"], "± 0.002", abs(m["mean_over_formula"] - spec["mean_over_formula"]["value"]) <= 0.002)
        limit = max(spec["max_rel_dev"]["value"] * 3.0, numbers["a10"].get("dev_floor", 0.0))
        t.add("A10", scene, "높이 출렁임", spec["max_rel_dev"]["value"], m["max_rel_dev"], f"≤ {limit:.4g}", m["max_rel_dev"] <= limit)
    n11 = numbers["a11"]
    ma = m_a11a(gsrc)
    empty_hold_limit = n11.get("empty_hold_limit", 0.0)
    t.add("A11", "empty_hold", "1+g 최댓값", n11["empty_hold_max_one_plus_g"]["value"], ma["max_one_plus_g"], f"≤ {empty_hold_limit:.4g}", ma["max_one_plus_g"] <= empty_hold_limit)
    md = m_a11d(gsrc)
    end_ref = n11["vortex_end"]["value"]
    t.add("A11", "vortex_return", "끝 차이값 최댓값", end_ref, md["end"], f"{end_ref:.4g} ± 0.002 이고 ≤ 0.04", abs(md["end"] - end_ref) <= 0.002 and md["end"] <= 0.04,
          "" if n11["vortex_mid"]["value"] >= 0.05 else "가운데 시점 < 0.05: 판정 불가 표시 대상")
    for scene in A12_SCENES:
        m = m_a12(gsrc, scene, replays)
        t.add("A12", scene, "입력 직후 띠 바람 비", numbers["a12"][scene]["value"]["ratio_after_inputs"], m["ratio_after_inputs"], "≤ 1e-4", m["ratio_after_inputs"] <= 1e-4)
        t.add("A12", scene, "1초 뒤 띠 바람 비", numbers["a12"][scene]["value"]["ratio_after_one_second"], m["ratio_after_one_second"], "≤ 1e-4", m["ratio_after_one_second"] <= 1e-4)
        t.add("A12", scene, "1초 뒤 띠 차이값", numbers["a12"][scene]["value"]["band_absdiff_end"], m["band_absdiff_end"], "≤ 1e-6", m["band_absdiff_end"] <= 1e-6)
    for scene, spec in numbers["a13"].items():
        m = m_a13(gsrc, rsrc, scene, replays, spec["kind"])
        if spec["rule"].startswith("기준값"):
            t.add("A13", scene, "채움 비", spec["value"], m["value"], "± 0.02", abs(m["value"] - spec["value"]) <= 0.02)
        else:
            t.add("A13", scene, "채움 비", spec["value"], m["value"], "≤ 1.15", m["value"] <= 1.15)
    for scene, spec in numbers["a15"].items():
        d, ref = gsrc(scene, spec["dump"]), rsrc(scene, spec["dump"])
        wind = max(float(np.abs(d.fields[k]).max()) for k in ("u", "v", "w"))
        disp = max(float(np.abs(d.fields[k]).max()) for k in ("dx", "dy", "dz"))
        t.add("A15", scene, "바람 최댓값", 0.0, wind, "= 0", wind == 0.0)
        t.add("A15", scene, "밀린 거리 최댓값", 0.0, disp, "= 0", disp == 0.0)
        fill = field_diff(ref, d, BAND_CELLS)["fill"]
        t.add("A15", scene, "채움 장 차이", "", fill, f"≤ X {spec['x']:.4g}", fill <= spec["x"])
    replay = load_replay(replays, "k4_run_mode1")
    for step in replay["steps"]:
        name = step.get("dump") or ""
        if not name.startswith("mv"):
            continue
        d = gsrc("k4_run_mode1", name)
        cells = new_band_untouched_cells(replay, name)
        worst = float(np.abs(d.fields["g"][cells & fluid_of(d)]).max(initial=0.0))
        t.add("A15", "k4_run_mode1", f"새 띠 채움 {name}", 0.0, worst, "= 0", worst == 0.0, f"칸 {int((cells & fluid_of(d)).sum())}개")
    if extras:
        base = canonical_hash(gsrc("far_origin_0", "end"))
        for scene in A4_SCENES:
            h = canonical_hash(gsrc(scene, "end"))
            t.add("A4+", scene, "상태 해시 = 원점 0", base, h, "같음", h == base)
        for scene, dumps in A7_SCENES.items():
            worst = 0.0
            for name in dumps:
                d = gsrc(scene, name)
                f = fluid_of(d)
                closed_u, closed_v, closed_w = ~f, ~f, ~f
                closed_u[:-1] |= ~f[1:]
                closed_u[-1] = True
                closed_v[:, :-1] |= ~f[:, 1:]
                closed_v[:, -1] = True
                closed_w[:, :, :-1] |= ~f[:, :, 1:]
                for arr, closed in ((d.fields["u"], closed_u), (d.fields["v"], closed_v), (d.fields["w"], closed_w)):
                    worst = max(worst, float(np.abs(arr[closed]).max(initial=0.0)))
            t.add("A7+", scene, "닫힌 면 바람 최댓값", 0.0, worst, "= 0", worst == 0.0, f"표본 {len(dumps)}개")
    return t


def run_compare(a):
    with open(a.numbers, encoding="utf-8") as fh:
        numbers = json.load(fh)
    try:
        table = judge(a.gpu, a.ref, numbers, a.replays, a.extras)
    except DumpError as err:
        print(f"오류(종료 코드 2): {err}")
        return 2
    except (KeyError, ValueError) as err:
        print(f"오류(종료 코드 2): 형식 오류 {type(err).__name__}: {err}")
        return 2
    table.print()
    failed = [r for r in table.rows if not r["passed"]]
    verdict = dict(format="fog3d-verdict-1", passed=not failed, checks=len(table.rows), failed=len(failed), numbers_code=numbers.get("code"), rows=table.rows)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(verdict, fh, ensure_ascii=False, indent=1, default=lambda o: bool(o) if isinstance(o, np.bool_) else float(o))
        fh.write("\n")
    print(f"판정: {'모두 통과' if not failed else f'실패 {len(failed)}개'} (검사 {len(table.rows)}개) -> {a.out}")
    return 1 if failed else 0


def self_test(a):
    names = set()
    with open(a.numbers, encoding="utf-8") as fh:
        numbers = json.load(fh)
    results = []
    with tempfile.TemporaryDirectory() as tmp:
        gpu = os.path.join(tmp, "gpu")
        for fname in sorted(os.listdir(a.ref)):
            if not fname.endswith(".npz"):
                continue
            scene, dump = fname[:-4].split("__")
            write_gpu_format(gpu, scene, dump, load_ref(a.ref, scene, dump))
            names.add(scene)
        base = argparse.Namespace(gpu=gpu, ref=a.ref, numbers=a.numbers, replays=a.replays, out=os.path.join(tmp, "v.json"), extras=True)
        code = run_compare(base)
        results.append(("기준 덤프를 GPU 형식으로 바꿔 넣으면 통과(종료 코드 0)", code == 0, code))
        scene, dump = "k2_swing_low", numbers["a3"]["k2_swing_low"]["dump"]
        shaken = load_gpu(gpu, scene, dump)
        shaken.fields["u"] = (shaken.fields["u"] * F32(1.01)).astype(F32)
        write_gpu_format(gpu, scene, dump, shaken)
        base.out = os.path.join(tmp, "v2.json")
        code = run_compare(base)
        results.append(("한 배열(바람 u)을 1% 흔들면 실패(종료 코드 1)", code == 1, code))
        os.remove(os.path.join(gpu, f"{scene}__{dump}.f32"))
        base.out = os.path.join(tmp, "v3.json")
        code = run_compare(base)
        results.append(("파일이 없으면 종료 코드 2", code == 2, code))
        with open(os.path.join(gpu, f"{scene}__{dump}.f32"), "wb") as fh:
            fh.write(b"\0\0\0\0")
        code = run_compare(base)
        results.append(("크기가 틀린 파일도 종료 코드 2", code == 2, code))
    print("--- 자체 확인 ---")
    for text, ok, code in results:
        print(f"{'통과' if ok else '실패'}: {text} (종료 코드 {code})")
    return 0 if all(ok for _, ok, _ in results) else 1


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--gpu", default=DEFAULT_GPU)
    ap.add_argument("--ref", default=DEFAULT_REF)
    ap.add_argument("--shake", default=DEFAULT_SHAKE)
    ap.add_argument("--numbers", default=DEFAULT_NUMBERS)
    ap.add_argument("--replays", default=DEFAULT_REPLAYS)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--extras", action="store_true")
    ap.add_argument("--make-numbers", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.make_numbers:
        try:
            make_numbers(a.ref, a.shake, a.replays, a.numbers)
        except DumpError as err:
            print(f"오류(종료 코드 2): {err}")
            return 2
        print(f"기준 숫자 JSON 을 썼다: {a.numbers}")
        return 0
    if a.self_test:
        return self_test(a)
    return run_compare(a)


if __name__ == "__main__":
    sys.exit(main())
