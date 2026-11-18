"""
실행 환경: Python 3.12 + numpy 2.2 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core·fog3d_terrain·fog3d_terrain_add2 를 import 만 하고 섞기 함수 셋을 클래스 속성으로 갈아 끼운다
실행 명령: 직접 판정에 쓰지 않는다(fog3d_blend_gates.py·fog3d_blend_regress.py 가 import). 식 성질 점검만: PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_blend_patch.py check
출력: check 는 표준 출력 json 한 줄
상태: 바람 섞기 식 후보 B0·B1·B3·B4 와 배율 wind_gain 의 갈아 끼우는 틀(Docs/VolumeFog3D/stage2/prereg-blend.md 2절·6절), 2026-11-16
"""
import atexit
import hashlib
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import fog3d_core as core  # noqa: E402
import fog3d_terrain as terrain  # noqa: E402
import fog3d_terrain_add2 as terrain_add2  # noqa: E402
from fog3d_core import F32  # noqa: E402

FORMULA_ENV = "TD_FOG3D_BLEND_FORMULA"
CALL_LOG_ENV = "TD_FOG3D_BLEND_CALL_LOG"
REFERENCE_STEPS_PER_SECOND = 60.0
UNIT_STEP_TOLERANCE = 1e-9
FORMULAS = ("B0", "B1", "B3", "B4")
TOOL_FILES = ("fog3d_blend_patch.py", "fog3d_blend_gates.py", "fog3d_blend_regress.py", "fog3d_blend_probe.py")

ACTIVE = dict(formula=None, default_gain=1.0)
CALLS = {}
ORIGINALS = {}
LOGGED_FIRST = set()


def steps_per_reference(dt):
    return REFERENCE_STEPS_PER_SECOND * dt


def factor_b0(weight_strength, dt):
    return weight_strength


def factor_b1(weight_strength, dt):
    return np.minimum(weight_strength, 1.0)


def factor_b4(weight_strength, dt):
    clipped = np.minimum(weight_strength, 1.0)
    repeat = steps_per_reference(dt)
    if abs(repeat - 1.0) <= UNIT_STEP_TOLERANCE:
        return clipped
    below = clipped < 1.0
    safe = np.where(below, clipped, 0.0)
    return np.where(below, -np.expm1(repeat * np.log1p(-safe)), 1.0) + 0.0


def factor_b3(weight_strength, dt):
    return -np.expm1(-(weight_strength * REFERENCE_STEPS_PER_SECOND * dt)) + 0.0


FACTORS = dict(B0=factor_b0, B1=factor_b1, B3=factor_b3, B4=factor_b4)


def formula_of(sim):
    chosen = sim.__dict__.get("blend_formula") or ACTIVE["formula"]
    if chosen not in FACTORS:
        raise RuntimeError(f"섞기 식이 정해지지 않음: {chosen}")
    return chosen


def gain_of(inp, formula):
    gain = float(getattr(inp, "wind_gain", ACTIVE["default_gain"]))
    if formula == "B0" and gain != 1.0:
        raise ValueError("B0(지금 식)에는 배율이 없다(G = 1)")
    return gain


def count_call(formula, label, blends):
    key = f"{formula}|{label}"
    entry = CALLS.setdefault(key, dict(calls=0, component_blends=0))
    entry["calls"] += 1
    entry["component_blends"] += blends
    if key not in LOGGED_FIRST:
        LOGGED_FIRST.add(key)
        write_call_log(dict(event="first_call", pid=os.getpid(), formula=formula, function=label))


def write_call_log(row):
    path = os.environ.get(CALL_LOG_ENV)
    if not path:
        return
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "a", encoding="utf-8") as fh:
        fh.write(json.dumps(row, ensure_ascii=False) + "\n")


def write_exit_counts():
    if CALLS:
        write_call_log(dict(event="exit", pid=os.getpid(), calls=CALLS))


def calls_snapshot():
    return {k: dict(v) for k, v in CALLS.items()}


def reset_calls():
    CALLS.clear()


def apply_erase(sim, erase, any_erase):
    if any_erase and sim.v3:
        g = sim.g.astype(np.float64)
        hit = erase > 0
        sim.g = np.where(hit, np.maximum(0.0, 1.0 + g) * np.exp(-erase) - 1.0, g).astype(F32)
    elif any_erase:
        bc = sim.Bc.astype(np.float64)
        total = np.maximum(bc + sim.D, 0.0) * np.exp(-erase)
        sim.D = (total - bc).astype(F32)


def blended_core_apply_inputs(self, inputs):
    p = self.p
    formula = formula_of(self)
    factor = FACTORS[formula]
    erase = np.zeros(self.D.shape)
    any_erase = False
    blends = 0
    for inp in inputs:
        a = self.to_local(inp.a)
        b = self.to_local(inp.b)
        r = float(inp.radius)
        si, sj = self._bbox(a, b, r)
        if si.start >= si.stop or sj.start >= sj.stop:
            continue
        gain = gain_of(inp, formula)
        for comp, name in enumerate(("u", "v", "w")):
            X, Y, Z, R, _ = self.location(name)
            W = core.band_weight(X[si], Y[:, sj], Z, R, a, b, r) * float(inp.strength)
            k = factor(W, p.dt)
            target = gain * float(inp.velocity[comp])
            arr = getattr(self, name)
            blk = arr[si, sj].astype(np.float64)
            arr[si, sj] = (blk + (target - blk) * k).astype(F32)
            blends += 1
        if inp.erase_per_sec > 0:
            X, Y, Z, R, _ = self.location("c")
            amount = core.dwell_amount(X[si], Y[:, sj], Z, R, a, b, r, p.dt)
            erase[si, sj] += inp.erase_per_sec * amount
            any_erase = True
    apply_erase(self, erase, any_erase)
    count_call(formula, "core.Sim._apply_inputs", blends)


def blended_terrain_apply_inputs(self, inputs):
    p = self.p
    formula = formula_of(self)
    factor = FACTORS[formula]
    erase = np.zeros(self.D.shape)
    any_erase = False
    blends = 0
    for inp in inputs:
        a, b, slope = self._input_frame(inp)
        r = float(inp.radius)
        si, sj = self._bbox(a, b, r)
        if si.start >= si.stop or sj.start >= sj.stop:
            continue
        gain = gain_of(inp, formula)
        for comp, name in enumerate(("u", "v", "w")):
            X, Y, _, R, _ = self.location(name)
            Z = self._sample_height(name, si, sj)
            W = core.band_weight(X[si], Y[:, sj], Z, R, a, b, r) * float(inp.strength)
            k = factor(W, p.dt)
            target = gain * self._wind_target(comp, inp.velocity, si, sj, slope)
            if comp == 2:
                self._record_top_target(target, k)
            arr = getattr(self, name)
            blk = arr[si, sj].astype(np.float64)
            arr[si, sj] = (blk + (target - blk) * k).astype(F32)
            blends += 1
        if inp.erase_per_sec > 0:
            X, Y, _, R, _ = self.location("c")
            Z = self._sample_height("c", si, sj)
            amount = core.dwell_amount(X[si], Y[:, sj], Z, R, a, b, r, p.dt)
            erase[si, sj] += inp.erase_per_sec * amount
            any_erase = True
    apply_erase(self, erase, any_erase)
    count_call(formula, "TerrainSim._apply_inputs", blends)


def blended_r5_apply_inputs(self, inputs):
    p = self.p
    formula = formula_of(self)
    factor = FACTORS[formula]
    erase = np.zeros(self.D.shape)
    any_erase = False
    blends = 0
    for inp in inputs:
        a, b, slope = self._input_frame(inp)
        r = float(inp.radius)
        si, sj = self._bbox(a, b, r)
        if si.start >= si.stop or sj.start >= sj.stop:
            continue
        self._count_blend(si, sj, a, b, r)
        gain = gain_of(inp, formula)
        for comp, name in enumerate(("u", "v", "w")):
            W = self.blended_weight(name, si, sj, a, b, r) * float(inp.strength)
            k = factor(W, p.dt)
            target = gain * self._wind_target(comp, inp.velocity, si, sj, slope)
            if comp == 2:
                self._record_top_target(target, k)
            arr = getattr(self, name)
            blk = arr[si, sj].astype(np.float64)
            arr[si, sj] = (blk + (target - blk) * k).astype(F32)
            blends += 1
        if inp.erase_per_sec > 0:
            amount = self.blended_dwell(si, sj, a, b, r, p.dt)
            erase[si, sj] += inp.erase_per_sec * amount
            any_erase = True
    apply_erase(self, erase, any_erase)
    count_call(formula, "TerrainSimR5._apply_inputs", blends)


PATCH_TARGETS = ((core.Sim, blended_core_apply_inputs), (terrain.TerrainSim, blended_terrain_apply_inputs),
                 (terrain_add2.TerrainSimR5, blended_r5_apply_inputs))


def install(formula, default_gain=1.0):
    if formula not in FACTORS:
        raise ValueError(f"모르는 식: {formula} (가능: {FORMULAS})")
    for cls, replacement in PATCH_TARGETS:
        ORIGINALS.setdefault(cls, cls.__dict__["_apply_inputs"])
        cls._apply_inputs = replacement
    ACTIVE["formula"] = formula
    ACTIVE["default_gain"] = float(default_gain)
    os.environ[FORMULA_ENV] = formula


def install_from_environment():
    formula = os.environ.get(FORMULA_ENV)
    if formula:
        install(formula)
    return formula


def uninstall():
    for cls, original in ORIGINALS.items():
        cls._apply_inputs = original
    ORIGINALS.clear()
    ACTIVE["formula"] = None


def is_installed():
    return all(cls.__dict__.get("_apply_inputs") is replacement for cls, replacement in PATCH_TARGETS)


def sha256_of(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def tool_fingerprints():
    return {name: sha256_of(os.path.join(HERE, name)) for name in TOOL_FILES if os.path.exists(os.path.join(HERE, name))}


def patch_record():
    return dict(formula=ACTIVE["formula"], installed=is_installed(), default_gain=ACTIVE["default_gain"], pid=os.getpid(),
                targets=[f"{cls.__module__}.{cls.__name__}._apply_inputs" for cls, _ in PATCH_TARGETS], tools=tool_fingerprints())


def property_check():
    rng = np.random.default_rng(20261116)
    ws = np.concatenate([rng.uniform(0.0, 2.9, 2000), np.array([0.0, 1e-12, 0.5, 1.0, 1.0 - 1e-15, 2.88])])
    dt60, dt30, dt144 = 1.0 / 60.0, 1.0 / 30.0, 1.0 / 144.0
    k60 = np.minimum(ws, 1.0)
    out = dict(
        b4_60_equals_clip=bool(np.array_equal(factor_b4(ws, dt60), k60)),
        b4_60_equals_b0_when_le1=bool(np.array_equal(factor_b4(ws[ws <= 1.0], dt60), ws[ws <= 1.0])),
        b4_30_vs_two_60_max_abs=float(np.abs((1.0 - factor_b4(ws, dt30)) - (1.0 - k60) ** 2).max()),
        b4_144_in_unit=bool(((factor_b4(ws, dt144) >= 0) & (factor_b4(ws, dt144) <= 1)).all()),
        b4_k1_is_1=bool(factor_b4(np.array([1.0, 2.0]), dt30).tolist() == [1.0, 1.0]),
        b3_60_lt_b0=bool((factor_b3(ws[ws > 0], dt60) < ws[ws > 0]).all()),
        b3_30_vs_two_60_max_abs=float(np.abs((1.0 - factor_b3(ws, dt30)) - (1.0 - factor_b3(ws, dt60)) ** 2).max()),
        b1_equals_clip=bool(np.array_equal(factor_b1(ws, dt30), k60)),
        no_negative_zero=bool(not np.any(np.signbit(np.concatenate([factor_b4(ws, dt30), factor_b3(ws, dt30)])))),
        unit_step_60=steps_per_reference(dt60), unit_step_30=steps_per_reference(dt30))
    return out


atexit.register(write_exit_counts)


if __name__ == "__main__":
    if sys.argv[1:] != ["check"]:
        raise SystemExit("사용: fog3d_blend_patch.py check")
    print(json.dumps(property_check(), ensure_ascii=False))
