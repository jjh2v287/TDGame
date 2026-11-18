import sys, json, glob
from pathlib import Path
import numpy
sys.path.insert(0, "C:/Project/TDGame/Tools/FluidFX")
import fog3d_visible_check as v

def load_run(run_dir):
    run = Path(run_dir)
    sched = json.load(open(run / "01_r_schedule.json", encoding="utf-8"))
    shots = sched["shots"]
    names = ["mode2_a", "mode2_b", "mode0_a", "mode0_b"]
    lumas = {n: v.load_luma(s["path"]) for n, s in zip(names, shots)}
    events = {round(e["time"], 1): e["game_time"] for e in sched["events_done"]}
    swing = events[0.0]; mode0 = events[3.3]
    cap = {n: s["captured_game_time"] for n, s in zip(names, shots)}
    gaps = {
        "swing_to_mode2_a": cap["mode2_a"] - swing,
        "mode2_a_to_mode2_b": cap["mode2_b"] - cap["mode2_a"],
        "mode2_b_before_mode0_cmd": mode0 - cap["mode2_b"],
        "mode0_cmd_to_mode0_a": cap["mode0_a"] - mode0,
        "mode0_a_to_mode0_b": cap["mode0_b"] - cap["mode0_a"],
    }
    valid = (2.15 <= gaps["swing_to_mode2_a"] <= 3.0 and 0.3 <= gaps["mode2_a_to_mode2_b"] <= 1.0
             and gaps["mode2_b_before_mode0_cmd"] > 0 and gaps["mode0_cmd_to_mode0_a"] >= 0.8
             and 0.3 <= gaps["mode0_a_to_mode0_b"] <= 1.0)
    return lumas, gaps, bool(valid), cap

def ground_region(h, w):
    r0 = v.fraction_to_index(0.60, h); r1 = v.fraction_to_index(0.95, h)
    c0 = v.fraction_to_index(0.25, w); c1 = v.fraction_to_index(0.75, w)
    m0 = v.fraction_to_index(0.42, w); m1 = v.fraction_to_index(0.58, w)
    mask = numpy.zeros((h, w), bool); mask[r0:r1, c0:c1] = True; mask[r0:r1, m0:m1] = False
    return mask

def stats(lumas, mask=None, region=None):
    def d(a, b):
        x = numpy.abs(lumas[a] - lumas[b])
        if mask is not None: return float(x[mask].mean())
        return float(v.crop(x, region).mean())
    diff = d("mode2_a", "mode0_a")
    n2 = d("mode2_a", "mode2_b"); n0 = d("mode0_a", "mode0_b")
    noise = max(n2, n0)
    j = v.judge_difference(diff, noise)
    j.update(noise_mode2_pair=n2, noise_mode0_pair=n0, diff_cross_b=d("mode2_b", "mode0_a"))
    return j

def compute(run_dir):
    lumas, gaps, valid, cap = load_run(run_dir)
    h, w = lumas["mode2_a"].shape
    pawn = stats(lumas, region=v.pawn_region(h, w))
    ground = stats(lumas, mask=ground_region(h, w))
    return {"run": str(run_dir), "valid": valid, "gaps": gaps, "captured_game_time": cap, "pawn_region": pawn, "ground_region_G": ground}

if __name__ == "__main__":
    out = {}
    for rd in sys.argv[1:]:
        r = compute(rd); out[Path(rd).name] = r
        p = r["pawn_region"]; g = r["ground_region_G"]
        print(Path(rd).name, "valid", r["valid"], {k: round(x, 3) for k, x in r["gaps"].items()})
        print("  pawn diff %.5f noise %.5f ratio %s pass %s | n2 %.5f n0 %.5f cross %.5f" % (p["difference"], p["noise"], p["ratio"] and round(p["ratio"], 3), p["passed"], p["noise_mode2_pair"], p["noise_mode0_pair"], p["diff_cross_b"]))
        print("  ground diff %.5f noise %.5f ratio %s pass %s | n2 %.5f n0 %.5f" % (g["difference"], g["noise"], g["ratio"] and round(g["ratio"], 3), g["passed"], g["noise_mode2_pair"], g["noise_mode0_pair"]))
    print(json.dumps(out)[:0])
