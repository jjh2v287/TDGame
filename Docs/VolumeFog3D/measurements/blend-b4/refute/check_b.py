import datetime
import glob
import hashlib
import json
import os
import sys

BLEND = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EXPECT = dict(patch="edcb43dc6a8eec97", gates="91ab8df3ceb66893", regress="1e8b80654d1d7767")
START = datetime.datetime(2026, 11, 16, 14, 11, 0).timestamp()
REQUIRED_STAGE1 = ["walk_px", "walk_mx", "walk_py", "walk_my", "walk_pxpy", "v2_N2", "g12_N2", "t_N2", "v3_N2", "k4hash_N2", "k4hash_N2_run2", "g14_N2",
                   "g15_N2", "leak_N2", "g16_ga_N2", "g16_gb26_N2", "g16_gb45_N2", "g16_gc_N2", "g16_gd_N2"]


def sha(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def tools_ok(tools):
    return (tools.get("fog3d_blend_patch.py", "").startswith(EXPECT["patch"]) and tools.get("fog3d_blend_gates.py", "").startswith(EXPECT["gates"])
            and tools.get("fog3d_blend_regress.py", "").startswith(EXPECT["regress"]))


def check_patch(patch, calls, want_formula, mtime):
    problems = []
    if patch.get("formula") != want_formula:
        problems.append(f"patch.formula={patch.get('formula')}")
    if not patch.get("installed"):
        problems.append("not installed")
    if not tools_ok(patch.get("tools", {})):
        problems.append("tool fingerprint")
    bad_keys = [k for k in calls if not k.startswith(want_formula + "|")]
    if bad_keys:
        problems.append(f"call keys {bad_keys}")
    if mtime < START:
        problems.append("older than 14:11")
    total = sum(v["calls"] for v in calls.values())
    return problems, total


def main():
    out = dict(official_b4=[], stage1=[], stage2a=[], reused=[], pids={})
    for path in sorted(glob.glob(os.path.join(BLEND, "official_b4", "pieces", "*"))):
        mtime = os.path.getmtime(path)
        if not path.endswith(".json"):
            if mtime < START:
                out["official_b4"].append(dict(id=os.path.basename(path), problems=["npz older than 14:11"]))
            continue
        p = json.load(open(path, encoding="utf-8"))
        want = p["case"]["formula"]
        problems, total = check_patch(p["patch"], p["calls"], want, mtime)
        if p["formula"] != want:
            problems.append("formula field")
        if p.get("error"):
            problems.append("error")
        v = p["values"] or {}
        if "finite" in v and not v["finite"]:
            problems.append("not finite")
        if p["dt"] is not None and abs(p["dt"] - 1.0 / p["case"]["hz"]) > 0:
            problems.append("dt")
        out["pids"].setdefault("official_b4", set()).add(p["patch"]["pid"])
        out["official_b4"].append(dict(id=os.path.basename(path)[:-5], formula=want, calls=total, pid=p["patch"]["pid"], problems=problems))
    s1 = os.path.join(BLEND, "regress_b4", "stage1")
    for pid_name in REQUIRED_STAGE1:
        piece = os.path.join(s1, "walk", pid_name + "_N2.json") if pid_name.startswith("walk") else os.path.join(s1, "pieces", pid_name + ".json")
        calls_path = os.path.join(s1, "blend_calls", pid_name + ".json")
        row = dict(id=pid_name)
        if not os.path.exists(piece) or not os.path.exists(calls_path):
            row["problems"] = ["missing piece or calls"]
        else:
            c = json.load(open(calls_path, encoding="utf-8"))
            problems, total = check_patch(c["patch"], c["calls"], "B4", min(os.path.getmtime(piece), os.path.getmtime(calls_path)))
            if c.get("exit_code") not in (0, None):
                problems.append(f"exit {c.get('exit_code')}")
            row.update(calls=total, pid=c["patch"]["pid"], problems=problems, piece_mtime=datetime.datetime.fromtimestamp(os.path.getmtime(piece)).isoformat(timespec="seconds"))
            out["pids"].setdefault("stage1", set()).add(c["patch"]["pid"])
        out["stage1"].append(row)
    s2 = os.path.join(BLEND, "regress_b4", "stage2a")
    parent = json.load(open(os.path.join(s2, "blend_patch.json"), encoding="utf-8"))["pid"]
    for path in sorted(glob.glob(os.path.join(s2, "pieces", "*.json"))):
        pid_name = os.path.basename(path)[:-5]
        calls_path = os.path.join(s2, "blend_calls", pid_name + ".json")
        row = dict(id=pid_name)
        if not os.path.exists(calls_path):
            row["problems"] = ["no calls file"]
        else:
            c = json.load(open(calls_path, encoding="utf-8"))
            problems, total = check_patch(c["patch"], c["calls"], "B4", min(os.path.getmtime(path), os.path.getmtime(calls_path)))
            if c["patch"]["pid"] == parent:
                problems.append("ran in parent pid")
            p = json.load(open(path, encoding="utf-8"))
            if p.get("error"):
                problems.append("error")
            row.update(calls=total, pid=c["patch"]["pid"], problems=problems)
            out["pids"].setdefault("stage2a", set()).add(c["patch"]["pid"])
        out["stage2a"].append(row)
    official = {}
    for folder in ("official_b4/pieces", "regress_b4/stage1/pieces", "regress_b4/stage1/walk", "regress_b4/stage2a/pieces"):
        for path in glob.glob(os.path.join(BLEND, folder, "*.json")):
            official[sha(path)] = os.path.relpath(path, BLEND)
    for folder in ("gates_smoke", "gates_check_b0", "gates_pipeline_b0_n32", "gates_pipeline_b0_n48", "gates_pipeline_b0_n64", "frame_b0", "frame_b3", "frame_b4"):
        for path in glob.glob(os.path.join(BLEND, folder, "**", "*.json"), recursive=True):
            h = sha(path)
            if h in official:
                out["reused"].append(dict(old=os.path.relpath(path, BLEND), official=official[h]))
    out["pids"] = {k: sorted(v) for k, v in out["pids"].items()}
    summary = {k: dict(count=len(out[k]), with_problems=[r for r in out[k] if r.get("problems")],
                       zero_calls=[r["id"] for r in out[k] if r.get("calls") == 0]) for k in ("official_b4", "stage1", "stage2a")}
    summary["reused"] = out["reused"]
    summary["distinct_pids"] = {k: len(v) for k, v in out["pids"].items()}
    summary["stage2a_parent_pid"] = parent
    json.dump(out, open(os.path.join(os.path.dirname(__file__), "B_pieces.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(json.dumps(summary, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    sys.exit(main())
