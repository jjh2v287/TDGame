import glob
import json
import math
import os
import re
import sys

import numpy as np

BLEND = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PIECES = os.path.join(BLEND, "official_b4", "pieces")
SPEED = dict(walk_stand=520.0, blade=None, turn=520.0)


def load(name):
    path = os.path.join(PIECES, name + ".json")
    return json.load(open(path, encoding="utf-8")) if os.path.exists(path) else None


def alt(x):
    x = np.asarray(x, float)
    if len(x) < 2 or x.mean() <= 0:
        return 0.0
    return float(np.abs(np.diff(x)).mean() / x.mean())


def metrics(p, blade_speed):
    c, v = p["case"], p["values"]
    dt = 1.0 / c["hz"]
    face = np.asarray(v["series"]["face_max"], float)
    ring = np.asarray(v["series"]["ring_max"], float)
    moving = v["moving"]
    speed = blade_speed if c["scene"] == "blade" else 520.0
    out = dict(nan=bool(np.isnan(face).any() or np.isnan(ring).any()), steps=len(face), moving=moving)
    fm = face[:moving].max()
    out["bl3"] = fm / (c["gain"] * speed)
    warm = 0 if c["scene"] == "blade" else int(round(10 * (1 / 60) / dt))
    out["warm"] = warm
    out["bl2"] = alt(ring[warm:moving])
    if c["scene"] in ("walk_stand",):
        w = int(round(0.5 / dt))
        last, prev = face[-w:].max(), face[-2 * w:-w].max()
        out.update(window=w, bl1_a=last / prev, bl1_b=last / fm, bl1_a_pass=bool(last <= 0.85 * prev), bl1_b_pass=bool(last <= 0.15 * fm),
                   stand_steps=len(face) - moving)
    lim = 0.10 if c["scene"] == "blade" else 0.02
    out["bl2_pass"] = bool(out["bl2"] <= lim)
    out["bl3_pass"] = bool(out["bl3"] <= 1.27)
    rows = v.get("bl4") or []
    if rows:
        out["bl4_f32_min"] = min(r["target_f32"]["min"] for r in rows)
        out["bl4_f32_max"] = max(r["target_f32"]["max"] for r in rows)
        out["bl4_f32_outside"] = sum(r["target_f32"]["outside"] for r in rows)
        out["bl4_64_outside"] = sum(r["outside"] for r in rows)
        out["bl4_64_outside_not_within_spacing"] = sum(r["outside"] - r["outside_within_f32_spacing"] for r in rows)
        out["bl4_f32_pass"] = bool(out["bl4_f32_outside"] == 0 and out["bl4_f32_min"] >= 0 and out["bl4_f32_max"] <= 1)
        out["bl4_steps"] = [r["step"] for r in rows]
        out["bl4_faces"] = sum(r["faces"] for r in rows)
    out["T_end"] = v["T"].get("end", {}).get("full")
    out["T_end_steps"] = v["T"].get("end", {}).get("steps")
    return out


def main():
    import fog3d_gates_s2a as s2a  # only for SCENE constants
    blade_speed = s2a.SCENE["blade_speed"]
    res = dict(blade_speed=blade_speed, grid=[], missing=[], extra_bl_pieces=[])
    for scene in ("walk_stand", "blade"):
        for hz in (60, 30):
            for s in (1.0, 2.0, 2.5, 3.0):
                for g in (1.0, 3.0):
                    name = f"B4_{scene}_s{s:g}_g{g:g}_hz{hz}_n128"
                    p = load(name)
                    if not p:
                        res["missing"].append(name)
                        continue
                    c = p["case"]
                    assert c["formula"] == "B4" and c["strength"] == s and c["gain"] == g and c["hz"] == hz and c["n"] == 128 and not c.get("max_steps")
                    res["grid"].append(dict(id=name, **metrics(p, blade_speed)))
    names = {os.path.basename(x)[:-5] for x in glob.glob(os.path.join(PIECES, "*.json"))}
    res["all_pass"] = {k: all(r.get(k, True) for r in res["grid"]) for k in ("bl1_a_pass", "bl1_b_pass", "bl2_pass", "bl3_pass", "bl4_f32_pass")}
    res["nan_any"] = any(r["nan"] for r in res["grid"])
    res["bl4_64_outside_not_within_spacing_total"] = sum(r.get("bl4_64_outside_not_within_spacing", 0) for r in res["grid"])
    bl5 = {}
    for hz in (60, 30):
        a, b = load(f"B4_shake_s3_hz{hz}"), load(f"B0_shake_s2_hz{hz}")
        ca, cb = a["values"]["max_abs_diff"], b["values"]["max_abs_diff"]
        bl5[hz] = dict(step=(a["values"]["measured_step"], b["values"]["measured_step"]), dt=(a["dt"], b["dt"]), formula=(a["formula"], b["formula"]),
                       strength=(a["strength"], b["strength"]), ratio={k: ca[k] / cb[k] for k in ca}, passed={k: bool(ca[k] <= 3 * cb[k]) for k in ca})
    res["bl5"] = bl5
    ladder = {}
    for scene, s in (("walk_stand", 3.0), ("blade", 2.5)):
        ref = load(f"B0_{scene}_s{s:g}_g1_hz60_n128")
        t_ref = ref["values"]["T"]["end"]["full"]
        ts = {}
        for g in (1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0):
            p = load(f"B4_{scene}_s{s:g}_g{g:g}_hz60_n128")
            ts[g] = p["values"]["T"]["end"]["full"] if p else None
        d = {g: abs(math.log(t / t_ref)) for g, t in ts.items() if t}
        pick = sorted(d, key=lambda g: (d[g], g))[0]
        vals = [t for t in ts.values() if t]
        spread = (max(vals) - min(vals)) / (sum(vals) / len(vals))
        ladder[scene] = dict(T_B0=t_ref, B0_steps=ref["values"]["T"]["end"]["steps"], T=ts, pick=1.0 if spread < 0.05 else pick, spread=spread,
                             second=sorted(d, key=lambda g: (d[g], g))[1], dist=d)
    res["ladder"] = ladder
    info = {}
    for name in ("B4_walk_stand_s3_g1_hz60_n256", "B4_turn_s3_g1_hz60_n128", "B4_walk_stand_s3_g1_hz144_n128"):
        p = load(name)
        info[name] = dict(case=p["case"], dt=p["dt"], n=p["n"], steps=p["values"]["steps"], moving=p["values"]["moving"], metrics=metrics(p, blade_speed))
    res["info"] = info
    print(json.dumps(res, ensure_ascii=False, indent=1, default=float))


if __name__ == "__main__":
    sys.path.insert(0, os.path.join(BLEND, "..", "..", "..", "..", "..", "Tools", "FluidFX", "fog3d_ref"))
    sys.exit(main())
