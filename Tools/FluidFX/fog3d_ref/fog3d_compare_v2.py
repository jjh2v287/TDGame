"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_compare.py·fog3d_compare_add1.py 와 fog3d_replay_v2.py 를 import 만 한다
실행 명령: python -B Tools/FluidFX/fog3d_ref/fog3d_compare_v2.py e1 [--gpu G --gpu-add1 G1] | e2 [--gpu G2] | e3 [--gpu G2] | make-numbers | mutants --mut-root <폴더> | fake-gpu --out <폴더>
출력: 판정 표(표준 출력)와 --out JSON. 종료 코드 0 = 모두 통과, 1 = 실패, 2 = 덤프 없음·형식 오류. make-numbers 는 Docs/VolumeFog3D/measurements/stage1d-ref/prereg-1d-numbers.json
상태: 1d 통과 기준(prereg-1d.md) E1 장 차이·E2 새 장면·E3 대칭·E4 판별력 표와 기준 숫자, 2026-11-14 K8. 숫자형 검사(A5·A8~A13·A15·덧붙임)는 잠긴 대조 도구에 --ref·--numbers 로 이 숫자 파일을 준다
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fog3d_compare as base  # noqa: E402
import fog3d_compare_add1 as cadd  # noqa: E402
import fog3d_replay_v2 as rv  # noqa: E402

core = rv.core
ROOT = core.ROOT
F32 = np.float32
FOG = rv.FOG
NUMBERS = os.path.join(ROOT, "Docs", "VolumeFog3D", "measurements", "stage1d-ref", "prereg-1d-numbers.json")
OPS = os.path.join(ROOT, "Saved", "AgentOps", "20261114", "fog3d", "stage1d", "k8")
FIELD_LIMIT = 0.002
GROUPS = (("wind", ("u", "v", "w")), ("disp", ("dx", "dy", "dz")), ("fill", ("g",)))
HEIGHTS = np.arange(1.5, 240.0, 3.0)
DZ_COL = 3.0
AXIS_WALKS = ("walk_px", "walk_py", "walk_mx", "walk_my")
OBLIQUE_WALKS = ("walk_a11", "walk_a22")
E3_BIAS_LIMIT = 0.03
E3_STRENGTH_BAND = 0.05
E3_OBLIQUE_TOL = 0.01
RULE_LOCAL_TOL = 1.0e-3


def load_any(folder, scene, dump):
    if os.path.isfile(os.path.join(folder, f"{scene}__{dump}.npz")):
        return base.load_ref(folder, scene, dump)
    return base.load_gpu(folder, scene, dump)


def dumps_of(replays, scene):
    return [s["dump"] for s in base.load_replay(replays, scene)["steps"] if s.get("dump")]


def scenes_in(folder):
    return sorted(f[:-5] for f in os.listdir(folder) if f.endswith(".json"))


def fluid_mask(d):
    if "fluid" in d.fields:
        return d.fields["fluid"] > 0.5
    shape = next(iter(d.fields.values())).shape
    return np.ones(shape, bool)


def group_diffs(ref, test):
    fluid = fluid_mask(ref)
    out = {}
    for label, names in GROUPS:
        present = [k for k in names if k in ref.fields]
        if not present:
            continue
        missing = [k for k in present if k not in test.fields]
        if missing:
            raise base.DumpError(f"덤프에 필드 없음 {missing}")
        out[label] = base.field_norm_diff([(ref.fields[k], test.fields[k]) for k in present], fluid, 0)
    return out


def fluid_mismatch(ref, test):
    if "fluid" not in ref.fields:
        return 0
    if "fluid" not in test.fields:
        return -1
    return int(np.count_nonzero((ref.fields["fluid"] > 0.5) != (test.fields["fluid"] > 0.5)))


def field_rows(crit, ref_dir, test_dir, replays, scenes):
    t = base.Table()
    for scene in scenes:
        for dump in dumps_of(replays, scene):
            ref, test = base.load_ref(ref_dir, scene, dump), load_any(test_dir, scene, dump)
            bad = fluid_mismatch(ref, test)
            t.add(crit, scene, f"유체 표시 = 기준 ({dump})", 0, bad, "= 0 칸", bad == 0)
            for label, value in group_diffs(ref, test).items():
                t.add(crit, scene, f"{label} 장 차이 ({dump})", "", value, f"≤ {FIELD_LIMIT}", value <= FIELD_LIMIT)
    return t


def finish_table(t, out):
    failed = [r for r in t.rows if not r["passed"]]
    for r in t.rows:
        if not r["passed"]:
            print("실패", r)
    print(f"검사 {len(t.rows)}개, 실패 {len(failed)}개")
    if out:
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        with open(out, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(dict(format="fog3d-verdict-1d", passed=not failed, checks=len(t.rows), failed=len(failed), rows=t.rows), fh, ensure_ascii=False, indent=1,
                      default=float)
    return 1 if failed else 0


# ---------------- E3 대칭 ----------------

def column_delta(d):
    n = int(d.meta["n"])
    sim = core.Sim(core.Params(nx=n, ny=n), window_min=tuple(d.meta["window_min"]))
    col0 = sim.drawn_density_v3(HEIGHTS).sum(axis=2) * DZ_COL
    sim.dpx, sim.dpy, sim.dpz, sim.g = (d.fields[k].astype(F32) for k in ("dx", "dy", "dz", "g"))
    return sim, sim.drawn_density_v3(HEIGHTS).sum(axis=2) * DZ_COL - col0


def walk_measure(d, replay):
    walk = replay["walk"]
    c, s = walk["heading"]
    start = walk["start_cm"]
    sim, delta = column_delta(d)
    rx, ry, _ = sim.center_rel()
    X, Y = np.meshgrid(rx, ry, indexing="ij")
    relx, rely = X - start[0], Y - start[1]
    along = relx * c + rely * s
    lateral = -relx * s + rely * c
    span = walk["walk_steps"] * walk["speed_cm_s"] * rv.WALK_DT
    on = (along > 50.0) & (along < span - 50.0)
    left = on & (lateral < 0) & (lateral > -100.0)
    right = on & (lateral > 0) & (lateral < 100.0)
    a_left, a_right = float(np.abs(delta[left]).mean()), float(np.abs(delta[right]).mean())
    return dict(abs_left=a_left, abs_right=a_right, side_bias=abs(a_left - a_right) / (a_left + a_right),
                signed_bias=(a_left - a_right) / (a_left + a_right), strength=float(np.abs(delta[left | right]).mean()))


def symmetry(folder, replays):
    return {scene: walk_measure(load_any(folder, scene, "end"), base.load_replay(replays, scene)) for scene in AXIS_WALKS + OBLIQUE_WALKS}


def e3_rows(sym, numbers):
    t = base.Table()
    mean = sum(sym[s]["strength"] for s in AXIS_WALKS) / len(AXIS_WALKS)
    for scene in AXIS_WALKS:
        m = sym[scene]
        t.add("E3", scene, "좌우 치우침", "", m["side_bias"], f"≤ {E3_BIAS_LIMIT}", m["side_bias"] <= E3_BIAS_LIMIT)
        rel = m["strength"] / mean - 1.0
        t.add("E3", scene, "세기 ÷ 네 방향 평균 − 1", "", rel, f"± {E3_STRENGTH_BAND}", abs(rel) <= E3_STRENGTH_BAND)
    for scene in OBLIQUE_WALKS:
        ref = numbers["stage1d"]["e3_reference"][scene]["side_bias"]
        got = sym[scene]["side_bias"]
        t.add("E3", scene, "비스듬한 방향 치우침", ref, got, f"± {E3_OBLIQUE_TOL}", abs(got - ref) <= E3_OBLIQUE_TOL)
    return t


# ---------------- 면 이어짐 장면 ----------------

def wind_at_dumps(replay, mutant):
    rv.ACTIVE["mutant"] = mutant
    sim = rv.make_sim_v2(replay)
    out = {}
    for step in replay["steps"]:
        rv.rp.apply_knobs(sim, replay, step)
        rv.rp.apply_wind(sim, step["wind_override"])
        sim.step(())
        if step.get("dump"):
            out[step["dump"]] = {c: getattr(sim, c).copy() for c in ("u", "v", "w")}
    rv.ACTIVE["mutant"] = None
    return out


def rule_faces():
    replay = base.load_replay(rv.REPLAYS_NEW, "face_rule")
    on_all, off_all = wind_at_dumps(replay, None), wind_at_dumps(replay, "rule_off")
    dumps = {}
    for dump, on in on_all.items():
        faces = []
        for comp in ("u", "v", "w"):
            off = off_all[dump][comp]
            for i, j, k in zip(*np.nonzero(on[comp] != off)):
                faces.append(dict(comp=comp, i=int(i), j=int(j), k=int(k), ref=float(on[comp][i, j, k]), rule_off=float(off[i, j, k])))
        scale = max(float(np.abs(on[c]).max()) for c in ("u", "v", "w"))
        dumps[dump] = dict(faces=faces, wind_scale=scale, tolerance=RULE_LOCAL_TOL * scale)
    return dict(dumps=dumps, rule="덤프마다 면 목록의 각 면에서 |GPU − 기준| ≤ 허용(= 1e-3 × 그 기준 덤프 바람 절댓값 최댓값). 목록 = 기준 모의에서 면 이어짐 규칙을 끄면 값이 바뀌는 면. 바람을 계산마다 덮어쓰므로 덤프끼리 섞이지 않는다")


def rule_rows(folder, numbers):
    spec = numbers["stage1d"]["face_rule"]
    t = base.Table()
    ratios = []
    for dump, entry in spec["dumps"].items():
        d = load_any(folder, "face_rule", dump)
        worst = 0.0
        for f in entry["faces"]:
            worst = max(worst, abs(float(d.fields[f["comp"]][f["i"], f["j"], f["k"]]) - f["ref"]))
        t.add("E2-rule", "face_rule", f"규칙이 바꾸는 면 {len(entry['faces'])}개의 최대 |차| ({dump})", 0, worst, f"≤ {entry['tolerance']:.4g}",
              worst <= entry["tolerance"])
        ratios.append((worst / entry["tolerance"], dump, worst))
    return t, max(ratios)


# ---------------- 기준 숫자 ----------------

def folder_listing_sha(folder):
    with open(os.path.join(folder, "manifest.json"), encoding="utf-8") as fh:
        return json.load(fh)["listing_sha256"]


def make_numbers(a):
    tmp = tempfile.mkdtemp()
    try:
        b = base.make_numbers(a.ref, a.shake, rv.REPLAYS_OLD, os.path.join(tmp, "a.json"))
        c = cadd.make_numbers(a.ref_add1, a.shake_add1, rv.REPLAYS_ADD1, os.path.join(tmp, "b.json"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    numbers = dict(b)
    numbers["format"] = "fog3d-prereg-1d-numbers-1"
    numbers["status"] = "1d 기준 숫자(새 방식 N2 기준 모의). 잠긴 fog3d_compare.py·fog3d_compare_add1.py 에 --numbers 로 준다. code 키는 잠긴 도구가 적는 옛 지문이며 새 방식의 지문은 stage1d.code 다"
    numbers["add1_meta"] = {k: c[k] for k in ("format", "status", "code", "code_short", "replays")}
    for k, v in c.items():
        if k not in numbers["add1_meta"]:
            numbers[k] = v
    sym = symmetry(a.ref_new, rv.REPLAYS_NEW)
    numbers["stage1d"] = dict(
        code={name: rv.sha256_file(os.path.join(HERE, name)) for name in ("fog3d_core_v2.py", "fog3d_replay_v2.py", "fog3d_compare_v2.py")},
        ref_listing_sha256={os.path.basename(f): folder_listing_sha(f) for f in (a.ref, a.shake, a.ref_add1, a.shake_add1, a.ref_new)},
        replays_v2={s: rv.sha256_file(os.path.join(rv.REPLAYS_NEW, f"{s}.json")) for s in scenes_in(rv.REPLAYS_NEW)},
        field_rule=f"E1·E2: 모든 덤프에서 바람·밀린 거리·채움 장 차이 각각 ≤ {FIELD_LIMIT}(가장자리 띠 포함, 기준 덤프의 유체 칸, 덤프에 있는 필드만), 유체 표시 같음",
        e3_reference=sym,
        e3_rule=f"네 방향 각각 좌우 치우침 ≤ {E3_BIAS_LIMIT}, 세기(띠 절댓값 평균)가 네 방향 평균의 ±{E3_STRENGTH_BAND:.0%} 안, 비스듬한 방향 치우침은 e3_reference ± {E3_OBLIQUE_TOL}",
        face_rule=rule_faces())
    os.makedirs(os.path.dirname(a.numbers), exist_ok=True)
    with open(a.numbers, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(numbers, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    print(json.dumps(dict(e3=sym, face_rule_faces={k: len(v["faces"]) for k, v in numbers["stage1d"]["face_rule"]["dumps"].items()}), ensure_ascii=False, indent=1))
    return 0


# ---------------- 판별력(E4) ----------------

def cmd_mutants(a):
    with open(a.numbers, encoding="utf-8") as fh:
        numbers = json.load(fh)
    table = {}
    for mutant in sorted(os.listdir(a.mut_root)):
        folder = os.path.join(a.mut_root, mutant)
        if not os.path.isdir(folder):
            continue
        rows = {}
        for scene in sorted({f.split("__")[0] for f in os.listdir(folder) if f.endswith(".npz")}):
            replays = rv.scene_folder(scene)
            ref_dir = {rv.REPLAYS_OLD: a.ref, rv.REPLAYS_ADD1: a.ref_add1, rv.REPLAYS_NEW: a.ref_new}[replays]
            worst = 0.0
            where = None
            for dump in dumps_of(replays, scene):
                diffs = group_diffs(base.load_ref(ref_dir, scene, dump), base.load_ref(folder, scene, dump))
                for label, value in diffs.items():
                    if value > worst:
                        worst, where = value, f"{dump}/{label}"
            row = dict(worst_field_diff=worst, over_limit=worst / FIELD_LIMIT, at=where)
            if scene == "face_rule":
                _, (ratio, dump, local) = rule_rows(folder, numbers)
                row["rule_local_max_abs"] = local
                row["rule_local_over_tol"] = ratio
                row["rule_local_at"] = dump
            rows[scene] = row
        table[mutant] = rows
        best = max(rows.items(), key=lambda kv: max(kv[1]["over_limit"], kv[1].get("rule_local_over_tol", 0.0)), default=(None, {}))
        print(mutant, "가장 잘 잡는 장면", best[0], json.dumps(best[1], ensure_ascii=False))
    with open(a.out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(table, fh, ensure_ascii=False, indent=1)
    return 0


def cmd_fake_gpu(a):
    count = 0
    for ref_dir, replays, sub in ((a.ref, rv.REPLAYS_OLD, "gpu"), (a.ref_add1, rv.REPLAYS_ADD1, "gpu_add1"), (a.ref_new, rv.REPLAYS_NEW, "gpu_new")):
        for scene in scenes_in(replays):
            for dump in dumps_of(replays, scene):
                base.write_gpu_format(os.path.join(a.out, sub), scene, dump, base.load_ref(ref_dir, scene, dump))
                count += 1
    print(f"GPU 형식으로 {count}개 덤프를 썼다: {a.out}")
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["e1", "e2", "e3", "make-numbers", "mutants", "fake-gpu"])
    ap.add_argument("--gpu", default=None)
    ap.add_argument("--gpu-add1", default=os.path.join(FOG, "gpu_add1"))
    ap.add_argument("--ref", default=rv.REF)
    ap.add_argument("--shake", default=rv.REF_SHAKE)
    ap.add_argument("--ref-add1", default=rv.REF_ADD1)
    ap.add_argument("--shake-add1", default=rv.REF_ADD1_SHAKE)
    ap.add_argument("--ref-new", default=rv.REF_NEW)
    ap.add_argument("--numbers", default=NUMBERS)
    ap.add_argument("--mut-root", default=os.path.join(OPS, "mut"))
    ap.add_argument("--out", default=None)
    a = ap.parse_args(argv)
    try:
        if a.cmd == "make-numbers":
            return make_numbers(a)
        if a.cmd == "fake-gpu":
            return cmd_fake_gpu(a)
        if a.cmd == "mutants":
            a.out = a.out or os.path.join(OPS, "e4-mutants.json")
            return cmd_mutants(a)
        with open(a.numbers, encoding="utf-8") as fh:
            numbers = json.load(fh)
        if a.cmd == "e1":
            t = field_rows("E1", a.ref, a.gpu or os.path.join(FOG, "gpu"), rv.REPLAYS_OLD, scenes_in(rv.REPLAYS_OLD))
            t.rows += field_rows("E1", a.ref_add1, a.gpu_add1, rv.REPLAYS_ADD1, scenes_in(rv.REPLAYS_ADD1)).rows
            return finish_table(t, a.out or os.path.join(FOG, "compare", "verdict_1d_e1.json"))
        gpu = a.gpu or os.path.join(FOG, "gpu_1d")
        if a.cmd == "e2":
            t = field_rows("E2", a.ref_new, gpu, rv.REPLAYS_NEW, scenes_in(rv.REPLAYS_NEW))
            t.rows += rule_rows(gpu, numbers)[0].rows
            return finish_table(t, a.out or os.path.join(FOG, "compare", "verdict_1d_e2.json"))
        sym = symmetry(gpu, rv.REPLAYS_NEW)
        print(json.dumps(sym, ensure_ascii=False, indent=1))
        return finish_table(e3_rows(sym, numbers), a.out or os.path.join(FOG, "compare", "verdict_1d_e3.json"))
    except base.DumpError as err:
        print(f"오류(종료 코드 2): {err}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
