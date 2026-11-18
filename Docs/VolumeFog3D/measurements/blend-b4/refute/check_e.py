"""BL-6 independent check. Usage: python -B check_e.py --out-dir <regress_b4> [--baseline-dir <regress_b0>] [--json <path>]"""
import argparse
import glob
import json
import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..", ".."))
REC1 = os.path.join(ROOT, "Saved", "AgentOps", "20261114", "fog3d", "stage1d")
REC2A = os.path.join(ROOT, "Saved", "AgentOps", "20261115", "fog3d", "stage2a", "official_add2")
TIME_KEYS = {"elapsed_sec", "finished", "seconds"}
CODE_KEYS = {"code", "code_v2", "script_v2", "locked", "model"}

S1_REQ_GA = ["v2_N2", "g12_N2", "t_N2", "v3_N2", "k4hash_N2", "k4hash_N2_run2"]
S1_REQ_NA = ["g14_N2", "g15_N2", "leak_N2", "g16_ga_N2", "g16_gb26_N2", "g16_gb45_N2", "g16_gc_N2", "g16_gd_N2"]
S1_EXTRA_GA = ["g4_N2", "g5_N2", "g7_N2", "g9_N2", "g10_N2", "g11_N2", "g13_N2", "v1_N2"]
S1_EXTRA_NA = ["g2_N2", "g6_N2", "g8_N2"]
GATE_OF = {"v2_N2": "D-V2", "g12_N2": "D-G12a·b·c·d", "t_N2": "R2-P(a)", "v3_N2": "D-G3·D-V3①②바닥", "k4hash_N2": "결정론", "k4hash_N2_run2": "결정론",
           "g14_N2": "D-G14", "g15_N2": "D-G15", "leak_N2": "누수 R2-S", "g16_ga_N2": "D-G16", "g16_gb26_N2": "D-G16", "g16_gb45_N2": "D-G16",
           "g16_gc_N2": "D-G16", "g16_gd_N2": "D-G16", "g4_N2": "D-G4 1/60", "g5_N2": "D-G5a·b", "g7_N2": "D-G7", "g9_N2": "D-G9", "g10_N2": "D-G10",
           "g11_N2": "D-G11", "g13_N2": "D-G13", "v1_N2": "D-V1", "g2_N2": "D-G2", "g6_N2": "D-G6", "g8_N2": "D-G8"}


def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def diffs(a, b, path="", skip=TIME_KEYS, out=None):
    out = [] if out is None else out
    if len(out) >= 10:
        return out
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b), key=str):
            if k in skip:
                continue
            if k not in a or k not in b:
                out.append(f"{path}/{k}: only one side")
            else:
                diffs(a[k], b[k], f"{path}/{k}", skip, out)
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append(f"{path}: len {len(a)}!={len(b)}")
        else:
            for i, (x, y) in enumerate(zip(a, b)):
                diffs(x, y, f"{path}[{i}]", skip, out)
    elif isinstance(a, float) and isinstance(b, float) and math.isnan(a) and math.isnan(b):
        pass
    elif type(a) is not type(b) and not (isinstance(a, (int, float)) and isinstance(b, (int, float)) and not isinstance(a, bool) and not isinstance(b, bool)):
        out.append(f"{path}: type {type(a).__name__}!={type(b).__name__}")
    elif a != b:
        out.append(f"{path}: {str(a)[:60]} != {str(b)[:60]}")
    return out


def hashes(o, path="", found=None):
    found = {} if found is None else found
    if isinstance(o, dict):
        for k, v in o.items():
            if "hash" in str(k).lower() and not isinstance(v, (dict, list)):
                found[f"{path}/{k}"] = v
            hashes(v, f"{path}/{k}", found)
    elif isinstance(o, list):
        for i, v in enumerate(o):
            hashes(v, f"{path}[{i}]", found)
    return found


def equal_row(new_path, ref_path, label):
    if not os.path.exists(new_path):
        return dict(state="아직 없음")
    if not ref_path or not os.path.exists(ref_path):
        return dict(state="견줄 것 없음", reference=label)
    n, r = load(new_path), load(ref_path)
    d = diffs(n, r)
    code = diffs({k: n.get(k) for k in CODE_KEYS}, {k: r.get(k) for k in CODE_KEYS}, skip=set())
    hn, hr = hashes(n), hashes(r)
    return dict(state="같음" if not d else "다름", reference=label, differing=d[:5], hashes=len(hn), hash_equal=hn == hr, code_fields_differ=code[:3],
                passed=n.get("passed"))


def stage1(out_dir, base):
    s1 = os.path.join(out_dir, "stage1")
    rows = {}
    for pid in S1_REQ_GA + S1_REQ_NA:
        rows[pid] = dict(group="가" if pid in S1_REQ_GA else "나", gate=GATE_OF[pid],
                         **equal_row(os.path.join(s1, "pieces", pid + ".json"), os.path.join(REC1, "pieces", pid + ".json"), "1d 기록"))
    for pid in S1_EXTRA_GA + S1_EXTRA_NA:
        ref = os.path.join(base, "stage1", "pieces", pid + ".json") if base else None
        rows[pid] = dict(group="가" if pid in S1_EXTRA_GA else "나", gate=GATE_OF[pid], **equal_row(os.path.join(s1, "pieces", pid + ".json"), ref, "B0 기준 실행"))
        if base and os.path.exists(os.path.join(s1, "pieces", pid + ".json")):
            ref_piece = os.path.join(base, "stage1", "pieces", pid + ".json")
            if os.path.exists(ref_piece):
                rows[pid]["b0_passed"] = load(ref_piece).get("passed")
    a, b = os.path.join(s1, "pieces", "k4hash_N2.json"), os.path.join(s1, "pieces", "k4hash_N2_run2.json")
    if os.path.exists(a) and os.path.exists(b):
        rows["k4hash_run1_vs_run2"] = dict(group="가", gate="결정론(두 번)", state="같음" if not diffs(load(a), load(b)) else "다름", differing=diffs(load(a), load(b))[:3])
    walks = {}
    for d in ("px", "mx", "py", "my", "pxpy"):
        p = os.path.join(s1, "walk", f"walk_{d}_N2.json")
        if os.path.exists(p):
            w = load(p)
            rec = load(os.path.join(REC1, "walk", f"walk_{d}_N2.json"))
            walks[d] = dict(side_bias=w["side_bias"], signed=w["signed_bias_left_minus_right"], total=w["total_abs_mean"], rec_side_bias=rec["side_bias"],
                            rec_total=rec["total_abs_mean"], max_wind=w.get("max_wind"), rec_max_wind=rec.get("max_wind"), equal_record=not diffs(w, rec))
    crit = {}
    if all(d in walks for d in ("px", "mx", "py", "my")):
        four = [walks[d] for d in ("px", "mx", "py", "my")]
        mean = sum(x["total"] for x in four) / 4
        crit["D-S1"] = dict(value=max(abs(x["signed"]) for x in four), threshold=0.03, passed=all(abs(x["signed"]) <= 0.03 for x in four))
        spread = max(abs(x["total"] - mean) / mean for x in four)
        crit["D-S2"] = dict(value=spread, threshold=0.05, passed=spread <= 0.05)
    g4 = os.path.join(s1, "pieces", "g4_N2.json")
    rows["D-G4 1/30·1/144"] = dict(group="다", gate="D-G4 1/30·1/144", state="조각 없음: 도구가 g4 를 --dt 60(기본)으로만 돈다",
                                   g4_piece_dt=load(g4)["config"]["dt"] if os.path.exists(g4) else "아직 없음")
    return dict(pieces=rows, walks=walks, criteria=crit)


def s2a_group(pid):
    gate = pid.split("_")[1]
    if gate == "G9" or (gate == "G6" and pid.endswith("_k1_60")):
        return "가"
    if gate in ("G2", "G3", "G8"):
        return "나"
    return "다·라"


def within(v, lo=None, hi=None):
    return (lo is None or v >= lo) and (hi is None or v <= hi)


def stage2a(out_dir):
    folder = os.path.join(out_dir, "stage2a")
    rows, counts = {}, {}
    for path in sorted(glob.glob(os.path.join(folder, "pieces", "*.json"))):
        pid = os.path.basename(path)[:-5]
        g = s2a_group(pid)
        row = equal_row(path, os.path.join(REC2A, "pieces", pid + ".json"), "2a 기록")
        row["group"] = g
        rows[pid] = row
        c = counts.setdefault(g, dict(pieces=0, equal=0, hash_equal=0))
        c["pieces"] += 1
        c["equal"] += row["state"] == "같음"
        c["hash_equal"] += bool(row.get("hash_equal"))
    rec_ids = {os.path.basename(p)[:-5] for p in glob.glob(os.path.join(REC2A, "pieces", "*.json"))}
    new_ids = set(rows)
    res = load(os.path.join(folder, "results.json"))["candidates"]["R5"] if os.path.exists(os.path.join(folder, "results.json")) else {}
    v = {k: (res.get(k) or {}).get("value") for k in res}
    th = {}
    if res:
        th["G4"] = within(v["G4"], hi=1e-6)
        th["G5"] = all(within(x, lo=0.5) for x in v["G5"].values())
        g6 = res["G6"]
        th["G6_wind"] = all(within(x, hi=1.0) for x in v["G6"].values()) if isinstance(v["G6"], dict) else None
        th["G6_detail"] = {k: g6[k] for k in g6 if k not in ("value",)}
        th["G7"] = all(within(x, hi=0.15) for x in v["G7"].values())
        th["G10"] = within(v["G10"], hi=0.5)
        th["G11"] = all(x["finite"] and within(x["value_over_flat"], 0.5, 2.0) and within(x["run_max_wind_over_flat"], hi=1.5) for x in v["G11"].values())
        th["G14"] = all(x == "pass" for x in v["G14"].values())
        th["G1"] = within(v["G1"]["F0"], hi=1e-5) and within(v["G1"]["F1"], hi=1e-4)
        th["G12"] = all(within(x, hi=1e-3) for x in v["G12"].values())
        th["G13"] = all(within(x, 0.8, 1.25) for x in v["G13"].values())
        th["passed_flags"] = {k: (res.get(k) or {}).get("passed") for k in res}
    r5r4 = {}
    for path in sorted(glob.glob(os.path.join(folder, "pieces", "R5_G1[34]_*.json"))):
        pid = os.path.basename(path)[:-5]
        other = os.path.join(folder, "pieces", "R4" + pid[2:] + ".json")
        if os.path.exists(other):
            r5r4[pid] = not diffs(load(path)["values"], load(other)["values"])
    return dict(counts=counts, record_minus_new=sorted(rec_ids - new_ids), new_minus_record=sorted(new_ids - rec_ids), thresholds=th, values=v,
                r5_equals_r4_values=r5r4, not_equal_in_ga_na=[k for k, r in rows.items() if r["group"] in ("가", "나") and r["state"] != "같음"],
                code_fields_differ=[k for k, r in rows.items() if r.get("code_fields_differ")], pieces=rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--baseline-dir", default="")
    ap.add_argument("--json", default="")
    a = ap.parse_args()
    out = dict(stage1=stage1(a.out_dir, a.baseline_dir), stage2a=stage2a(a.out_dir))
    js = a.json or os.path.join(os.path.dirname(os.path.abspath(__file__)), "E_check.json")
    with open(js, "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False, indent=1, default=str)
    s1 = out["stage1"]
    print("[1단계 조각]")
    for k, r in s1["pieces"].items():
        print(f"  {r['group']} {k} {r.get('gate')} : {r['state']} 해시같음={r.get('hash_equal')} 통과={r.get('passed')} B0통과={r.get('b0_passed', '-')} 다른곳={r.get('differing', [])[:2]} 코드칸차={r.get('code_fields_differ', [])[:1]}")
    print("[D-S1·D-S2]", json.dumps(s1["criteria"], ensure_ascii=False))
    s2 = out["stage2a"]
    print("[2a 묶음]", json.dumps(s2["counts"], ensure_ascii=False), "기록에만:", s2["record_minus_new"], "새것에만:", s2["new_minus_record"])
    print("[2a (가)(나) 기록과 다름]", s2["not_equal_in_ga_na"], " 코드칸 다름:", s2["code_fields_differ"][:5])
    print("[2a 문턱]", json.dumps({k: v for k, v in s2["thresholds"].items() if k != "G6_detail"}, ensure_ascii=False))
    print("[2a R5=R4 값]", json.dumps(s2["r5_equals_r4_values"], ensure_ascii=False))


if __name__ == "__main__":
    sys.exit(main())
