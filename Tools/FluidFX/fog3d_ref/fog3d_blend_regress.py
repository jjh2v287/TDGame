"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 잠긴 fog3d_gates_v2·fog3d_gates_s2a_add2 를 import 만 하고 fog3d_blend_patch 로 섞기 식을 갈아 끼운다
실행 명령: PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_blend_regress.py stage1 --formula <B0|B1|B3|B4> --out-dir <폴더> [--jobs 6] [--set required|extra|all] [--only v2,t] / stage2a --formula <식> --out-dir <폴더> [--jobs 6] [--pieces id,id] [--inprocess] [--sub stage2a] / compare --out-dir <폴더> [--baseline-dir <B0 로 돌린 폴더>]
출력: <폴더>/stage1/{pieces,walk,logs,blend_calls}/, <폴더>/<sub>/{pieces,blend_calls}/ + results.json·results.md(덧붙임 2 collect)·blend_patch.json, compare 는 <폴더>/compare.json·compare.md
상태: 바람 섞기 식 후보로 예전 판정선(1단계 N2 조각·2a 덧붙임 2 공식 실행)을 다시 돌리고 기록과 견주는 실행기(Docs/VolumeFog3D/stage2/prereg-blend.md 6절), 2026-11-16
"""
import os

for _thread_var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_thread_var, "1")

import argparse
import concurrent.futures
import json
import math
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fog3d_blend_patch as blend_patch  # noqa: E402

blend_patch.install_from_environment()

import fog3d_core as core  # noqa: E402

ROOT = core.ROOT
THIS = os.path.abspath(__file__)
STAGE1_RECORD = os.path.join(ROOT, "Saved", "AgentOps", "20261114", "fog3d", "stage1d")
STAGE2A_RECORD = os.path.join(ROOT, "Saved", "AgentOps", "20261115", "fog3d", "stage2a", "official_add2")
IGNORED_KEYS = {"elapsed_sec", "finished", "code", "code_v2", "script_v2", "seconds"}
WALK_DIRECTIONS = ("px", "mx", "py", "my", "pxpy")
REQUIRED_GATE_PIECES = (("v2", None), ("g12", None), ("t", None), ("v3", None), ("k4hash", None), ("k4hash", "run2"), ("g14", None),
                        ("g15", None), ("leak", None), ("g16", "ga"), ("g16", "gb26"), ("g16", "gb45"), ("g16", "gc"), ("g16", "gd"))
EXTRA_GATE_PIECES = tuple((name, None) for name in ("g2", "g3", "g4", "g5", "g6", "g7", "g8", "g9", "g10", "g11", "g13", "v1"))
EXTRA_STEP_RATE_PIECES = (("g4", 30), ("g4", 144))
D_S1_MAX = 0.03
D_S2_BAND = 0.05
ADD2_ORIGINAL = {}


def stage1_piece(kind, name, which=None, label=None):
    if kind == "walk":
        file = f"walk_{name}_N2.json"
        return dict(id=f"walk_{name}", kind="walk", argv=["walk", name, "--advect", "N2"], subdir="walk", file=file,
                    record=os.path.join(STAGE1_RECORD, "walk", file))
    stem = name if which is None or which == "run2" else f"{name}_{which}"
    file = f"{stem}_N2{'_run2' if which == 'run2' else ''}.json"
    argv = [name] + ([which] if which not in (None, "run2") else []) + ["--advect", "N2", "--budget-sec", "100000"]
    record = os.path.join(STAGE1_RECORD, "pieces", file)
    return dict(id=os.path.splitext(file)[0], kind="gate", argv=argv, subdir="pieces", file=file, record=record if os.path.exists(record) else None)


def stage1_step_rate_piece(name, steps_per_second):
    file = f"{name}_dt{steps_per_second}_N2.json"
    argv = [name, "--advect", "N2", "--budget-sec", "100000", "--dt", str(steps_per_second)]
    return dict(id=os.path.splitext(file)[0], kind="gate", argv=argv, subdir="pieces", file=file, record=None)


def stage1_pieces(which_set):
    required = [stage1_piece("walk", d) for d in WALK_DIRECTIONS] + [stage1_piece("gate", n, w) for n, w in REQUIRED_GATE_PIECES]
    extra = [stage1_piece("gate", n, w) for n, w in EXTRA_GATE_PIECES] + [stage1_step_rate_piece(n, r) for n, r in EXTRA_STEP_RATE_PIECES]
    return {"required": required, "extra": extra, "all": required + extra}[which_set]


def recorded_seconds(piece):
    if not piece["record"]:
        return 600.0
    with open(piece["record"], encoding="utf-8") as fh:
        data = json.load(fh)
    return float(data.get("elapsed_sec") or data.get("seconds") or 600.0)


def run_stage1_subprocess(piece, formula, out_dir):
    out = os.path.join(out_dir, "stage1", piece["subdir"], piece["file"])
    calls = os.path.join(out_dir, "stage1", "blend_calls", piece["id"] + ".json")
    log = os.path.join(out_dir, "stage1", "logs", piece["id"] + ".log")
    for path in (out, calls, log):
        os.makedirs(os.path.dirname(path), exist_ok=True)
    env = dict(os.environ, PYTHONUTF8="1", PYTHONIOENCODING="utf-8")
    cmd = [sys.executable, "-B", THIS, "stage1-piece", "--formula", formula, "--out", out, "--calls", calls, "--"] + piece["argv"] + ["--out", out]
    t0 = time.time()
    with open(log, "w", encoding="utf-8") as fh:
        code = subprocess.run(cmd, cwd=ROOT, env=env, stdout=fh, stderr=subprocess.STDOUT).returncode
    return piece["id"], code, round(time.time() - t0, 1)


def cmd_stage1(args):
    blend_patch.install(args.formula)
    pieces = stage1_pieces(args.set)
    if args.only:
        wanted = set(args.only.split(","))
        pieces = [p for p in pieces if p["id"] in wanted or p["argv"][0] in wanted]
    todo = [p for p in pieces if not os.path.exists(os.path.join(args.out_dir, "stage1", p["subdir"], p["file"]))]
    todo.sort(key=lambda p: -recorded_seconds(p))
    print(f"[stage1] 식 {args.formula}, 조각 {len(todo)}개(전체 {len(pieces)}), 병렬 {args.jobs}", flush=True)
    failures = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for piece_id, code, seconds in pool.map(lambda p: run_stage1_subprocess(p, args.formula, args.out_dir), todo):
            print(f"[stage1] {piece_id} 끝 코드 {code} {seconds}s", flush=True)
            failures += int(code != 0)
    return 1 if failures else 0


def cmd_stage1_piece(args, gate_argv):
    blend_patch.install(args.formula)
    import fog3d_gates_v2 as gates_v2
    blend_patch.reset_calls()
    code = gates_v2.main(gate_argv)
    with open(args.calls, "w", encoding="utf-8") as fh:
        json.dump(dict(piece=os.path.basename(args.out), calls=blend_patch.calls_snapshot(), patch=blend_patch.patch_record(), exit_code=code), fh,
                  ensure_ascii=False, indent=1)
    return code or 0


def run_piece_with_calls(item):
    import fog3d_gates_s2a_add2 as add2
    spec, out_dir = item
    original = ADD2_ORIGINAL.get("run_piece") or add2.run_piece
    blend_patch.reset_calls()
    result = original(item)
    folder = os.path.join(out_dir, "blend_calls")
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, spec["id"] + ".json"), "w", encoding="utf-8") as fh:
        json.dump(dict(piece=spec["id"], calls=blend_patch.calls_snapshot(), patch=blend_patch.patch_record()), fh, ensure_ascii=False, indent=1)
    return result


def wrap_add2(add2):
    ADD2_ORIGINAL.setdefault("run_piece", add2.run_piece)
    add2.run_piece = run_piece_with_calls


def all_add2_specs(add2):
    return add2.requested_specs(argparse.Namespace(only=[], candidates=[add2.R5], smoke=False))


def cmd_stage2a(args):
    blend_patch.install(args.formula)
    import fog3d_gates_s2a_add2 as add2
    out_dir = os.path.abspath(os.path.join(args.out_dir, args.sub))
    os.makedirs(os.path.join(out_dir, "pieces"), exist_ok=True)
    os.environ[blend_patch.CALL_LOG_ENV] = os.path.join(out_dir, "blend_calls.jsonl")
    wrap_add2(add2)
    with open(os.path.join(out_dir, "blend_patch.json"), "w", encoding="utf-8") as fh:
        json.dump(dict(blend_patch.patch_record(), started=time.strftime("%Y-%m-%d %H:%M:%S"), note="덧붙임 2 실행기를 섞기 식을 갈아 끼운 채 그대로 부름"),
                  fh, ensure_ascii=False, indent=1)
    if not args.pieces:
        code = add2.main(["run", "--out-dir", out_dir, "--jobs", str(args.jobs)] + (["--only", args.only] if args.only else []))
        add2.main(["collect", "--out-dir", out_dir])
        return code
    wanted = args.pieces.split(",")
    specs = [s for s in all_add2_specs(add2) if s["id"] in wanted]
    missing = sorted(set(wanted) - {s["id"] for s in specs})
    if missing:
        raise SystemExit(f"모르는 조각: {missing}")
    if args.inprocess:
        failures = [r for r in (run_piece_with_calls((s, out_dir)) for s in specs) if r[1]]
    else:
        failures = add2.execute(specs, out_dir, args.jobs, False)
    print(f"[stage2a] 식 {args.formula}, 조각 {len(specs)}개, 실패 {len(failures)}, 작업 프로세스 {'없음(같은 프로세스)' if args.inprocess else args.jobs}", flush=True)
    return 1 if failures else 0


def same_number(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return a is b
    if isinstance(a, float) and isinstance(b, float) and math.isnan(a) and math.isnan(b):
        return True
    return a == b


def differing_paths(a, b, path="", limit=20):
    out = []
    if isinstance(a, dict) and isinstance(b, dict):
        for key in sorted(set(a) | set(b)):
            if key in IGNORED_KEYS:
                continue
            if key not in a or key not in b:
                out.append(f"{path}/{key}(한쪽만)")
            else:
                out += differing_paths(a[key], b[key], f"{path}/{key}", limit)
            if len(out) >= limit:
                return out[:limit]
        return out
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return [f"{path}(길이 {len(a)}≠{len(b)})"]
        for i, (x, y) in enumerate(zip(a, b)):
            out += differing_paths(x, y, f"{path}[{i}]", limit)
            if len(out) >= limit:
                return out[:limit]
        return out
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return [] if same_number(a, b) else [f"{path}: {a!r} ≠ {b!r}"]
    return [] if a == b else [f"{path}: {a!r} ≠ {b!r}"]


def load_json(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def hashes_in(obj, found=None, path=""):
    found = {} if found is None else found
    if isinstance(obj, dict):
        for key, value in obj.items():
            if key in IGNORED_KEYS:
                continue
            if "hash" in str(key) and isinstance(value, (str, dict)):
                found[f"{path}/{key}"] = value
            else:
                hashes_in(value, found, f"{path}/{key}")
    return found


def compare_piece(new_path, reference_path):
    new = load_json(new_path)
    if reference_path is None or not os.path.exists(reference_path):
        return dict(compared=False, passed=new.get("passed"), error=new.get("error"))
    ref = load_json(reference_path)
    diffs = differing_paths(new, ref)
    new_hash, ref_hash = hashes_in(new), hashes_in(ref)
    return dict(compared=True, equal=not diffs, differing=diffs, hash_equal=new_hash == ref_hash, hashes=len(new_hash), passed=new.get("passed"))


def calls_of(folder, piece_id):
    path = os.path.join(folder, "blend_calls", piece_id + ".json")
    if not os.path.exists(path):
        return None
    data = load_json(path)
    return dict(formula=data["patch"]["formula"], installed=data["patch"]["installed"], pid=data["patch"]["pid"],
                calls={k: v["calls"] for k, v in data["calls"].items()})


def compare_stage1(out_dir, baseline_dir):
    folder = os.path.join(out_dir, "stage1")
    rows = {}
    for piece in stage1_pieces("all"):
        path = os.path.join(folder, piece["subdir"], piece["file"])
        if not os.path.exists(path):
            continue
        reference, source = piece["record"], "기록"
        if reference is None and baseline_dir:
            reference, source = os.path.join(baseline_dir, "stage1", piece["subdir"], piece["file"]), "B0 기준 실행"
        row = compare_piece(path, reference)
        row.update(reference=source if row["compared"] else None, calls=calls_of(folder, piece["id"]))
        rows[piece["id"]] = row
    return rows


def walk_criteria(out_dir):
    folder = os.path.join(out_dir, "stage1", "walk")
    walks = {}
    for d in ("px", "mx", "py", "my"):
        path = os.path.join(folder, f"walk_{d}_N2.json")
        if not os.path.exists(path):
            return None
        walks[d] = load_json(path)
    sizes = [w["total_abs_mean"] for w in walks.values()]
    mean = sum(sizes) / len(sizes)
    biases = {d: w["side_bias"] for d, w in walks.items()}
    spread = max(abs(s - mean) / mean for s in sizes)
    return dict(D_S1=dict(value=biases, threshold=D_S1_MAX, passed=bool(all(b <= D_S1_MAX for b in biases.values()))),
                D_S2=dict(value=dict(sizes=dict(zip(walks, sizes)), max_relative_spread=spread), threshold=D_S2_BAND,
                          passed=bool(spread <= D_S2_BAND and all(b <= D_S1_MAX for b in biases.values()))))


STAGE1_GATES = (
    ("가", "D-G3·D-V3", ("v3_N2",)), ("가", "D-V2", ("v2_N2",)), ("가", "D-G12b", ("g12_N2",)), ("가", "R2-P(a)", ("t_N2",)),
    ("가", "결정론 해시", ("k4hash_N2", "k4hash_N2_run2")), ("가", "D-G4(1/60 판, 조각 안 dt60)", ("g4_N2",)), ("가", "D-G5a·b", ("g5_N2",)),
    ("가", "D-G7", ("g7_N2",)), ("가", "D-G9", ("g9_N2",)), ("가", "D-G10", ("g10_N2",)), ("가", "D-G11", ("g11_N2",)), ("가", "D-G13", ("g13_N2",)),
    ("가", "D-V1", ("v1_N2",)), ("나", "D-G2", ("g2_N2",)), ("나", "D-G6", ("g6_N2",)), ("나", "D-G8", ("g8_N2",)), ("나", "D-G12a·c·d", ("g12_N2",)),
    ("나", "R2-S 누수", ("leak_N2",)), ("나", "D-G14", ("g14_N2",)), ("나", "D-G15", ("g15_N2",)),
    ("나", "D-G16", ("g16_ga_N2", "g16_gb26_N2", "g16_gb45_N2", "g16_gc_N2", "g16_gd_N2")), ("다", "D-G4(1/30·1/144 판)", ("g4_dt30_N2", "g4_dt144_N2")))


def stage1_group_rows(rows, criteria):
    out = []
    for group, gate, ids in STAGE1_GATES:
        present = [rows[i] for i in ids if i in rows]
        if not present:
            out.append(dict(group=group, gate=gate, pieces=list(ids), state="조각 없음"))
            continue
        compared = [r for r in present if r["compared"]]
        out.append(dict(group=group, gate=gate, pieces=list(ids), present=len(present), compared=len(compared),
                        equal=sum(bool(r["equal"]) for r in compared), hash_equal=sum(bool(r["hash_equal"]) for r in compared),
                        reference=sorted({r["reference"] for r in compared}), tool_passed=[r["passed"] for r in present]))
    if criteria:
        for name, crit in criteria.items():
            out.append(dict(group="다", gate=name.replace("_", "-"), pieces=[f"walk_{d}" for d in ("px", "mx", "py", "my")], value=crit["value"],
                            threshold=crit["threshold"], threshold_passed=crit["passed"]))
    return out


def piece_ids(folder):
    path = os.path.join(folder, "pieces")
    return sorted(os.path.splitext(n)[0] for n in os.listdir(path) if n.endswith(".json")) if os.path.isdir(path) else []


def compare_stage2a_folder(folder):
    rows = {}
    for piece_id in piece_ids(folder):
        row = compare_piece(os.path.join(folder, "pieces", piece_id + ".json"), os.path.join(STAGE2A_RECORD, "pieces", piece_id + ".json"))
        row.update(calls=calls_of(folder, piece_id))
        rows[piece_id] = row
    return rows


def stage2a_group(piece_id):
    gate = piece_id.split("_")[1]
    if gate == "G9" or (gate == "G6" and piece_id.endswith("_k1_60")):
        return "가"
    if gate in ("G2", "G3", "G8"):
        return "나"
    return "다·라·정보"


STAGE2A_RESULT_GROUPS = (("다", ("G4", "G5", "G6", "G7", "G10", "G11", "G14")), ("라", ("G1", "G12", "G13")), ("가", ("G9",)), ("나", ("G2", "G3", "G8")))


def stage2a_result_rows(folder):
    new_path, ref_path = os.path.join(folder, "results.json"), os.path.join(STAGE2A_RECORD, "results.json")
    if not os.path.exists(new_path):
        return None
    new, ref = load_json(new_path)["candidates"]["R5"], load_json(ref_path)["candidates"]["R5"]
    out = []
    for group, gates in STAGE2A_RESULT_GROUPS:
        for g in gates:
            n, r = new.get(g) or {}, ref.get(g) or {}
            out.append(dict(group=group, gate=f"2A-{g}", value=n.get("value"), threshold=n.get("threshold"), passed=n.get("passed"),
                            recorded_value=r.get("value"), recorded_passed=r.get("passed"), value_equal_to_record=not differing_paths(n.get("value"), r.get("value"))))
    return out


def cmd_compare(args):
    out_dir = os.path.abspath(args.out_dir)
    stage1_rows = compare_stage1(out_dir, args.baseline_dir) if os.path.isdir(os.path.join(out_dir, "stage1")) else {}
    stage1_groups = stage1_group_rows(stage1_rows, walk_criteria(out_dir)) if stage1_rows else []
    stage2a = {}
    for name in sorted(os.listdir(out_dir)):
        folder = os.path.join(out_dir, name)
        if name.startswith("stage2a") and os.path.isdir(folder):
            rows = compare_stage2a_folder(folder)
            groups = {}
            for piece_id, row in rows.items():
                g = groups.setdefault(stage2a_group(piece_id), dict(pieces=0, equal=0, hash_equal=0))
                g["pieces"] += 1
                g["equal"] += int(bool(row.get("equal")))
                g["hash_equal"] += int(bool(row.get("hash_equal")))
            stage2a[name] = dict(pieces=rows, groups=groups, results=stage2a_result_rows(folder))
    res = dict(out_dir=out_dir, stage1=dict(pieces=stage1_rows, groups=stage1_groups), stage2a=stage2a, tools=blend_patch.tool_fingerprints(),
               rule="(가)·(나)·(라): 기록과 같음(json 에 저장된 자릿수 그대로, 해시 같음). (다): 문턱에 대한 값. 시간·코드 지문 칸은 견주지 않음")
    with open(os.path.join(out_dir, "compare.json"), "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False, indent=1, default=str)
    write_compare_md(os.path.join(out_dir, "compare.md"), res)
    print(json.dumps(dict(stage1_pieces=len(stage1_rows), stage1_equal=sum(bool(r.get("equal")) for r in stage1_rows.values()),
                          stage2a={k: v["groups"] for k, v in stage2a.items()}), ensure_ascii=False))
    return 0


def short(value, width=160):
    text = json.dumps(value, ensure_ascii=False, default=str)
    return text if len(text) <= width else text[:width] + "…"


def write_compare_md(path, res):
    lines = ["# 예전 판정선 다시 돌리기 — 기록과 견줌 (fog3d_blend_regress.py compare)", "", f"- {res['rule']}", f"- 도구 지문: {short(res['tools'], 400)}", "",
             "## 1단계 조각", "", "| 조각 | 견준 것 | 같음 | 해시 같음 | 다른 곳(앞 3) | 도구의 통과 | 갈아 끼운 식·호출 |", "|---|---|---|---|---|---|---|"]
    for piece_id, r in res["stage1"]["pieces"].items():
        lines.append(f"| {piece_id} | {r.get('reference')} | {r.get('equal')} | {r.get('hash_equal')} | {short(r.get('differing', [])[:3], 200)} | "
                     f"{short(r.get('passed'), 80)} | {short(r.get('calls'), 160)} |")
    lines += ["", "## 1단계 판정선 묶음(등록 6절)", "", "| 묶음 | 판정선 | 조각 | 같음/견줌 | 해시 같음 | 문턱 대어 본 값 |", "|---|---|---|---|---|---|"]
    for g in res["stage1"]["groups"]:
        tail = f"{short(g.get('value'))} ≤ {g.get('threshold')} → {g.get('threshold_passed')}" if "threshold" in g else short(g.get("tool_passed"), 100)
        lines.append(f"| {g['group']} | {g['gate']} | {', '.join(g['pieces'])} | {g.get('equal', '-')}/{g.get('compared', '-')} | {g.get('hash_equal', '-')} | {tail} |")
    for name, block in res["stage2a"].items():
        lines += ["", f"## 2a ({name})", "", "| 묶음 | 조각 수 | 기록과 같음 | 해시 같음 |", "|---|---|---|---|"]
        for group, g in sorted(block["groups"].items()):
            lines.append(f"| {group} | {g['pieces']} | {g['equal']} | {g['hash_equal']} |")
        differing = {k: v for k, v in block["pieces"].items() if v.get("compared") and not v.get("equal")}
        if differing:
            lines += ["", "기록과 다른 조각:", ""] + [f"- {k}: {short(v['differing'][:3], 300)}" for k, v in differing.items()]
        lines += ["", "| 조각 | 같음 | 해시 같음 | 갈아 끼운 식·호출 |", "|---|---|---|---|"]
        lines += [f"| {k} | {v.get('equal')} | {v.get('hash_equal')} | {short(v.get('calls'), 160)} |" for k, v in block["pieces"].items()]
        if block["results"]:
            lines += ["", "| 묶음 | 판정선 | 값 | 문턱 | 통과 | 기록 값 | 기록 통과 | 값이 기록과 같음 |", "|---|---|---|---|---|---|---|---|"]
            lines += [f"| {r['group']} | {r['gate']} | {short(r['value'])} | {short(r['threshold'], 80)} | {r['passed']} | {short(r['recorded_value'])} | "
                      f"{r['recorded_passed']} | {r['value_equal_to_record']} |" for r in block["results"]]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "stage1-piece":
        split = argv.index("--")
        ap = argparse.ArgumentParser()
        ap.add_argument("command")
        ap.add_argument("--formula", required=True, choices=blend_patch.FORMULAS)
        ap.add_argument("--out", required=True)
        ap.add_argument("--calls", required=True)
        return cmd_stage1_piece(ap.parse_args(argv[:split]), argv[split + 1:])
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="command", required=True)
    s1 = sub.add_parser("stage1")
    s1.add_argument("--formula", required=True, choices=blend_patch.FORMULAS)
    s1.add_argument("--out-dir", required=True)
    s1.add_argument("--jobs", type=int, default=6)
    s1.add_argument("--set", default="required", choices=("required", "extra", "all"))
    s1.add_argument("--only", default="")
    s2 = sub.add_parser("stage2a")
    s2.add_argument("--formula", required=True, choices=blend_patch.FORMULAS)
    s2.add_argument("--out-dir", required=True)
    s2.add_argument("--jobs", type=int, default=6)
    s2.add_argument("--only", default="")
    s2.add_argument("--pieces", default="")
    s2.add_argument("--inprocess", action="store_true")
    s2.add_argument("--sub", default="stage2a")
    cp = sub.add_parser("compare")
    cp.add_argument("--out-dir", required=True)
    cp.add_argument("--baseline-dir", default="")
    args = ap.parse_args(argv)
    if args.command != "compare":
        args.out_dir = os.path.abspath(args.out_dir)
        os.makedirs(args.out_dir, exist_ok=True)
    commands = dict(stage1=cmd_stage1, stage2a=cmd_stage2a, compare=cmd_compare)
    return commands[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
