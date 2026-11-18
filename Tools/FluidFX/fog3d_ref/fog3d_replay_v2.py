"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core.py·fog3d_replay.py·fog3d_replay_add1.py 와 fog3d_core_v2.py 를 import 만 한다
실행 명령: python -B Tools/FluidFX/fog3d_ref/fog3d_replay_v2.py existing [--jobs 14] | new [--jobs 8] | mutant --mutant <이름> --scene <a,b> --out <폴더> | manifest <폴더> | list
출력: existing = 1a 59장면·1b 덧붙임 3장면을 새 방식(N2)으로 다시 돈 기준 덤프 Saved/FluidFX/Fog3D/ref_v2·ref_v2_shake·ref_add1_v2(+proof.json)·ref_add1_v2_shake, new = 1d 새 장면 재생 파일 replays_v2/*.json 과 기준 덤프 ref_v2_new, mutant = 변이 덤프, 폴더마다 manifest.json
상태: 1d 바람 옮기기 교체(prereg-1d.md) 기준 자료 도구, 2026-11-14 K8. 재생 파일(입력)은 방식과 무관하므로 기존 장면은 잠긴 replays/·replays_add1/ 의 JSON 을 그대로 읽는다
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fog3d_core as core  # noqa: E402
import fog3d_core_v2 as v2  # noqa: E402
import fog3d_replay as rp  # noqa: E402
import fog3d_replay_add1 as ra  # noqa: E402

F32 = np.float32
HX = rp.HX
FOG = os.path.join(core.ROOT, "Saved", "FluidFX", "Fog3D")
REPLAYS_OLD = os.path.join(HERE, "replays")
REPLAYS_ADD1 = os.path.join(HERE, "replays_add1")
REPLAYS_NEW = os.path.join(HERE, "replays_v2")
REF = os.path.join(FOG, "ref_v2")
REF_SHAKE = os.path.join(FOG, "ref_v2_shake")
REF_ADD1 = os.path.join(FOG, "ref_add1_v2")
REF_ADD1_SHAKE = os.path.join(FOG, "ref_add1_v2_shake")
REF_NEW = os.path.join(FOG, "ref_v2_new")
LOCKED = ("fog3d_core.py", "fog3d_replay.py", "fog3d_replay_add1.py", "fog3d_compare.py", "fog3d_compare_add1.py", "tuned_solvers.json")
V2_FILES = ("fog3d_core_v2.py", "fog3d_replay_v2.py")

WALK_DT = 1.0 / 60.0
WALK_SPEED = 520.0
WALK_STEPS = 70
WALK_STOP = 78
WALK_RIG = (((0.0, -12.0, 30.0), 45.0), ((0.0, 12.0, 30.0), 45.0), ((0.0, 0.0, 91.0), 60.0))
WALK_STRENGTH = 2.0
WALK_HEADINGS = {"walk_px": (1.0, 0.0), "walk_py": (0.0, 1.0), "walk_mx": (-1.0, 0.0), "walk_my": (0.0, -1.0),
                 "walk_a11": (math.cos(math.radians(11.25)), math.sin(math.radians(11.25))),
                 "walk_a22": (math.cos(math.radians(22.5)), math.sin(math.radians(22.5)))}
RULE_ORIGIN = -64
RULE_DT = 1.0 / 20.0
RULE_PATTERNS = (
    dict(dump="s1", move_cm=(5.4 * HX, 0.5 * HX / 0.53, 15.0 / 0.7), solid_offsets=((4, 0, 0), (5, 1, 0), (5, 0, 1)),
         bases=tuple((i, 40, 2) for i in (20, 40, 60, 80, 100))),
    dict(dump="s2", move_cm=(5.6 * HX, 0.5 * HX / 0.52, 15.0 / 0.65), solid_offsets=((5, 1, 0), (5, 0, 1), (6, 1, 0), (6, 0, 1)),
         bases=tuple((i, 75, 3) for i in (20, 45, 70, 95))))
TOP_VORTEX = dict(kind="vortex", u_cm_s=1500.0, half_period_cm=160.0, sign=1.0)
TOP_IDLE = 6


def sha256_file(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def code_fingerprint():
    return {name: sha256_file(os.path.join(HERE, name))[:16] for name in LOCKED + V2_FILES}


# ---------------- 새 방식 모의와 변이 ----------------

class MutantSim(v2.Sim):
    mutant = None

    def center_velocity(self):
        if self.mutant != "old_dg":
            return super().center_velocity()
        return (self.u.astype(np.float64), self.v.astype(np.float64), self.w.astype(np.float64))

    def _face_velocity(self, kind, pos, up, vp, wp):
        vel = super()._face_velocity(kind, pos, up, vp, wp)
        if self.mutant != "top_w_draft" or kind != "w":
            return vel
        vel[0][:, :, -1] *= 0.5
        vel[1][:, :, -1] *= 0.5
        vel[2][:, :, -1] = self.w[:, :, -1].astype(np.float64)
        return vel

    def _face_advect(self, dt):
        self._center_end = getattr(self, "_end_cell", None)
        out = super()._face_advect(dt)
        if self.mutant not in ("old_u", "old_v", "old_w"):
            return out
        axis = ("old_u", "old_v", "old_w").index(self.mutant)
        saved = self._end_cell
        vel = (self.u.astype(np.float64), self.v.astype(np.float64), self.w.astype(np.float64))
        dep = self._path_clamp(vel, dt)
        st = self._setup("c", *self._clamp(*dep))
        zero = self._disconnected_corner(st)
        out[axis] = self._gather(self.corners8((self.u, self.v, self.w)[axis]), st, zero)
        self._end_cell = saved
        return out

    def _face_disconnected(self, axis, kind, sample, sf):
        if self.mutant == "rule_off":
            return None
        if self.mutant == "rule_center_end":
            saved = self._end_cell
            self._end_cell = self._center_end
            zero = super()._face_disconnected(axis, kind, sample, sf)
            self._end_cell = saved
            return zero
        if self.mutant == "rule_half_shift":
            half = (self.p.hx / 2.0, self.p.hy / 2.0, 15.0)[axis]
            shifted = list(sample)
            shifted[axis] = sample[axis] - half
            return super()._face_disconnected(axis, kind, tuple(shifted), sf)
        return super()._face_disconnected(axis, kind, sample, sf)


MUTANTS = ("old_u", "old_v", "old_w", "old_dg", "top_w_draft", "rule_off", "rule_center_end", "rule_half_shift")
ACTIVE = {"mutant": None}


def make_sim_v2(replay):
    first = replay["steps"][0]
    p = rp.build_params(replay, first["dt"])
    p.advect = "N2"
    obstacles = [dict(kind=o["kind"], center=o["center_cm"], half=o["half_cm"], yaw_deg=o["yaw_deg"]) for o in replay["obstacles"]]
    cls = v2.Sim
    if ACTIVE["mutant"]:
        cls = type("Mutant_" + ACTIVE["mutant"], (MutantSim,), dict(mutant=ACTIVE["mutant"]))
    return cls(p, anchor=tuple(replay["anchor_cell"]), window_min=tuple(first["prev_window_min"]), obstacles=obstacles)


rp.make_sim = make_sim_v2


# ---------------- 새 장면 ----------------

def walk_scene(name):
    c, s = WALK_HEADINGS[name]
    r = rp.Replay(name, n=128)
    pos = np.array((-300.0 * c, -300.0 * s))
    for k in range(WALK_STEPS + WALK_STOP):
        prev = pos.copy()
        if k < WALK_STEPS:
            pos = pos + np.array((c, s)) * WALK_SPEED * WALK_DT
            vel = (WALK_SPEED * c, WALK_SPEED * s, 0.0)
        else:
            vel = (0.0, 0.0, 0.0)
        spheres = []
        for (f, lat, z), radius in WALK_RIG:
            dx, dy = f * c - lat * s, f * s + lat * c
            spheres.append(core.SweptSphere((float(prev[0] + dx), float(prev[1] + dy), z), (float(pos[0] + dx), float(pos[1] + dy), z),
                                            radius, vel, WALK_STRENGTH, 0.0))
        dump = "w70" if k == WALK_STEPS - 1 else ("end" if k == WALK_STEPS + WALK_STOP - 1 else None)
        r.step(WALK_DT, spheres=spheres, dump=dump)
    r.body["walk"] = dict(heading=[c, s], start_cm=[-300.0 * c, -300.0 * s], walk_steps=WALK_STEPS, stop_steps=WALK_STOP, speed_cm_s=WALK_SPEED)
    return r


def wall_self_scene():
    obstacles = [rp.box_dict((40.0, 0.0, 120.0), (rp.THIN_HALF, 400.0, 120.0)), rp.box_dict((-420.0, -60.0, 120.0), (rp.THIN_HALF, 200.0, 120.0), 45.0)]
    r = rp.Replay("wall_self", n=128, obstacles=obstacles)
    dt = 1.0 / 60.0
    for k in range(12):
        y0, y1 = -240.0 + 40.0 * k, -240.0 + 40.0 * (k + 1)
        d0, d1 = 40.0 * k, 40.0 * (k + 1)
        diag = math.sqrt(0.5)
        a2 = (-420.0 - 300.0 * diag + d0 * diag, -60.0 + 300.0 * diag - d0 * diag, 100.0)
        b2 = (-420.0 - 300.0 * diag + d1 * diag, -60.0 + 300.0 * diag - d1 * diag, 100.0)
        spheres = [core.SweptSphere((-12.0, y0, 90.0), (-12.0, y1, 90.0), 30.0, (0.0, 2400.0, 0.0), 2.0, 0.0),
                   core.SweptSphere(a2, b2, 30.0, (2400.0 * diag, -2400.0 * diag, 0.0), 2.0, 0.0)]
        r.step(dt, spheres=spheres, dump="s12" if k == 11 else None)
    r.idle(dt, 18, dump="s30")
    r.idle(dt, 30, dump="end")
    return r


def top_wface_scene():
    r = rp.Replay("top_wface", n=128)
    dt = 1.0 / 60.0
    r.step(dt, pressure=False, relax=False, wind=dict(TOP_VORTEX), dump="s1")
    r.idle(dt, TOP_IDLE, dump="end", pressure=False, relax=False)
    return r


def rule_cells():
    cells = []
    for pattern in RULE_PATTERNS:
        for i0, j0, k0 in pattern["bases"]:
            cells += [(i0 + a, j0 + b, k0 + c) for a, b, c in pattern["solid_offsets"]]
    return cells


def face_rule_scene():
    obstacles = [rp.box_dict(((RULE_ORIGIN + i + 0.5) * HX, (RULE_ORIGIN + j + 0.5) * HX, 30.0 * k + 15.0), (0.1, 0.1, 0.1)) for i, j, k in rule_cells()]
    r = rp.Replay("face_rule", n=128, obstacles=obstacles)
    for pattern in RULE_PATTERNS:
        vel = [float(-m / RULE_DT) for m in pattern["move_cm"]]
        r.step(RULE_DT, pressure=False, relax=False, band=False, wind=dict(kind="uniform", v_cm_s=vel), dump=pattern["dump"])
    r.body["face_rule"] = dict(patterns=[dict(dump=p["dump"], move_cm=list(p["move_cm"]), solid_offsets=[list(o) for o in p["solid_offsets"]],
                                              bases=[list(b) for b in p["bases"]]) for p in RULE_PATTERNS],
                               note="면 이어짐 규칙이 결과를 바꾸는 꼴(impl-1d-contract 3절의 반례). s1 = 표본이 끝 칸의 지나온 쪽 반, s2 = 앞쪽 반(묶음 원점 반 칸 실수를 가름)")
    return r


NEW_SCENES = dict({name: (lambda name=name: walk_scene(name)) for name in WALK_HEADINGS},
                  wall_self=wall_self_scene, top_wface=top_wface_scene, face_rule=face_rule_scene)


# ---------------- 실행 ----------------

def load_json(folder, name):
    with open(os.path.join(folder, f"{name}.json"), encoding="utf-8") as fh:
        return json.load(fh)


def scene_folder(name):
    for folder in (REPLAYS_OLD, REPLAYS_ADD1, REPLAYS_NEW):
        if os.path.isfile(os.path.join(folder, f"{name}.json")):
            return folder
    raise SystemExit(f"재생 파일 없음: {name}")


def run_job(job):
    kind, name, out_dir, mutant = job
    ACTIVE["mutant"] = mutant
    started = time.time()
    folder = scene_folder(name)
    replay = load_json(folder, name)
    if kind == "add1" or (kind == "mutant" and folder == REPLAYS_ADD1):
        res = ra.execute_add1(replay, out_dir)
        res.pop("arrays", None)
    elif kind == "add1_shake":
        res = ra.execute_add1(replay, out_dir, shake=True)
        res.pop("arrays", None)
    elif kind == "shake":
        res = rp.execute(replay, out_dir, shake=True, stop_after_dump=rp.SENSITIVITY_SCENES[name])
    else:
        res = rp.execute(replay, out_dir)
    return dict(kind=kind, name=name, mutant=mutant, steps=res["steps"], hash=res["hash"], dumps=[os.path.basename(p) for p in res["dumps"]],
                seconds=round(time.time() - started, 1))


def scene_cost(name):
    r = load_json(scene_folder(name), name)
    return sum(int(s.get("repeat", 1)) for s in r["steps"]) * (r["grid"]["n"] / 128.0) ** 2


def run_pool(jobs, workers):
    jobs = sorted(jobs, key=lambda j: -scene_cost(j[1]) * (0.1 if j[0] == "shake" else 1.0))
    infos = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=max(1, workers)) as pool:
        for info in pool.map(run_job, jobs):
            print(json.dumps(info, ensure_ascii=False))
            sys.stdout.flush()
            infos.append(info)
    return infos


def names_in(folder):
    return sorted(f[:-5] for f in os.listdir(folder) if f.endswith(".json"))


def write_manifest(folder, extra=None):
    old_path = os.path.join(folder, "manifest.json")
    if extra is None and os.path.isfile(old_path):
        with open(old_path, encoding="utf-8") as fh:
            extra = {k: v for k, v in json.load(fh).items() if k == "runs"}
    files =sorted(f for f in os.listdir(folder) if f != "manifest.json" and os.path.isfile(os.path.join(folder, f)))
    rows = [dict(name=f, bytes=os.path.getsize(os.path.join(folder, f)), sha256=sha256_file(os.path.join(folder, f))) for f in files]
    listing = "\n".join(f"{r['name']} {r['bytes']} {r['sha256']}" for r in rows) + "\n"
    body = dict(format="fog3d-ref-manifest-1", folder=os.path.relpath(folder, core.ROOT).replace("\\", "/"), advect="N2", solid="S9",
                code=code_fingerprint(), files=rows, listing_sha256=hashlib.sha256(listing.encode()).hexdigest(),
                listing_rule="각 줄 '이름 바이트수 sha256' 을 이름순으로 \\n 으로 잇고 끝에 \\n, 그 sha256", **(extra or {}))
    with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(body, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    return body["listing_sha256"]


def cmd_existing(a):
    jobs = [("ref", n, a.out_ref, None) for n in names_in(REPLAYS_OLD)]
    jobs += [("shake", n, a.out_shake, None) for n in names_in(REPLAYS_OLD) if n in rp.SENSITIVITY_SCENES]
    jobs += [("add1", n, a.out_ref_add1, None) for n in names_in(REPLAYS_ADD1)]
    jobs += [("add1_shake", n, a.out_shake_add1, None) for n in names_in(REPLAYS_ADD1)]
    infos = run_pool(jobs, a.jobs)
    if not a.no_proof:
        write_proof(a.out_ref_add1)
    write_existing_manifests(a, infos)
    return 0


def write_proof(out_ref_add1):
    replays = {n: load_json(REPLAYS_ADD1, n) for n in names_in(REPLAYS_ADD1)}
    refs = {n: ra.execute_add1(r) for n, r in replays.items()}
    shakes = {n: ra.execute_add1(r, shake=True) for n, r in replays.items()}
    mask = refs[ra.SCENE_CONNECT]["arrays"]["end"]["measure_mask"]
    connect_note = None
    if not np.any(mask):
        connect_note = "새 방식에서는 이 장면의 마지막 계산에 이어짐 넷째 단계가 무게 > 0 으로 쓰인 칸이 0개다(measure_mask 빔). 덧붙임 판정 ADD-a 는 값이 정의되지 않는다"
        replays = {n: r for n, r in replays.items() if n != ra.SCENE_CONNECT}
        refs[ra.SCENE_CONNECT]["arrays"]["end"]["measure_mask"] = np.ones_like(mask)
        replays[ra.SCENE_CONNECT] = load_json(REPLAYS_ADD1, ra.SCENE_CONNECT)
    proof = ra.prove(replays, refs, shakes)
    if connect_note:
        proof[ra.SCENE_CONNECT] = dict(step4_cells=0, note=connect_note, step4=refs[ra.SCENE_CONNECT].get("step4"))
    proof["code"] = code_fingerprint()
    with open(os.path.join(out_ref_add1, "proof.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(proof, fh, ensure_ascii=False, indent=1, default=float)
        fh.write("\n")


def write_existing_manifests(a, infos):
    for folder in (a.out_ref, a.out_shake, a.out_ref_add1, a.out_shake_add1):
        mine = [i for i in infos if os.path.abspath(dict(ref=a.out_ref, shake=a.out_shake, add1=a.out_ref_add1, add1_shake=a.out_shake_add1)[i["kind"]]) == os.path.abspath(folder)]
        print(folder, write_manifest(folder, dict(runs=mine)))
    return 0


def cmd_new(a):
    os.makedirs(a.out_replays, exist_ok=True)
    for name, build in NEW_SCENES.items():
        replay = build().finish()
        replay["source"] = dict(tool="fog3d_replay_v2.py", code={k: v for k, v in code_fingerprint().items() if k != "fog3d_replay_v2.py"},
                                note="입력 생성 도구 자체의 지문은 Saved/AgentOps/20261114/fog3d/stage1d/ref-v2-lock.txt")
        problems = rp.check_replay(replay)
        if problems:
            print(name, "형식 문제:", problems)
            return 1
        with open(os.path.join(a.out_replays, f"{name}.json"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(rp.dumps_text(replay))
    if a.no_ref:
        return 0
    names = list(NEW_SCENES) if a.scene == "all" else a.scene.split(",")
    infos = run_pool([("ref", n, a.out_ref, None) for n in names], a.jobs)
    print(json.dumps(infos, ensure_ascii=False))
    print(a.out_ref, write_manifest(a.out_ref))
    return 0


def cmd_mutant(a):
    if a.mutant not in MUTANTS:
        raise SystemExit(f"변이는 {MUTANTS}")
    os.makedirs(a.out, exist_ok=True)
    infos = run_pool([("mutant", n, a.out, a.mutant) for n in a.scene.split(",")], a.jobs)
    with open(os.path.join(a.out, "runs.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(infos, fh, ensure_ascii=False, indent=1)
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("existing")
    e.add_argument("--out-ref", default=REF)
    e.add_argument("--out-shake", default=REF_SHAKE)
    e.add_argument("--out-ref-add1", default=REF_ADD1)
    e.add_argument("--out-shake-add1", default=REF_ADD1_SHAKE)
    e.add_argument("--jobs", type=int, default=14)
    e.add_argument("--no-proof", action="store_true")
    n = sub.add_parser("new")
    n.add_argument("--out-replays", default=REPLAYS_NEW)
    n.add_argument("--out-ref", default=REF_NEW)
    n.add_argument("--jobs", type=int, default=9)
    n.add_argument("--no-ref", action="store_true")
    n.add_argument("--scene", default="all")
    m = sub.add_parser("mutant")
    m.add_argument("--mutant", required=True)
    m.add_argument("--scene", required=True)
    m.add_argument("--out", required=True)
    m.add_argument("--jobs", type=int, default=10)
    fe = sub.add_parser("finish-existing")
    fe.add_argument("--log", required=True)
    fe.add_argument("--out-ref", default=REF)
    fe.add_argument("--out-shake", default=REF_SHAKE)
    fe.add_argument("--out-ref-add1", default=REF_ADD1)
    fe.add_argument("--out-shake-add1", default=REF_ADD1_SHAKE)
    f = sub.add_parser("manifest")
    f.add_argument("folder")
    sub.add_parser("list")
    a = ap.parse_args(argv)
    if a.cmd == "existing":
        return cmd_existing(a)
    if a.cmd == "new":
        return cmd_new(a)
    if a.cmd == "mutant":
        return cmd_mutant(a)
    if a.cmd == "finish-existing":
        with open(a.log, encoding="utf-8") as fh:
            infos = [json.loads(line) for line in fh if line.startswith('{"kind"')]
        write_proof(a.out_ref_add1)
        write_existing_manifests(a, infos)
        return 0
    if a.cmd == "manifest":
        print(write_manifest(a.folder))
        return 0
    print("\n".join(NEW_SCENES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
