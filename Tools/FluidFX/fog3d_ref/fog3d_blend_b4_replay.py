"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_blend_patch·fog3d_blend_probe·fog3d_blend_gates·fog3d_replay_s2a·fog3d_compare·fog3d_compare_v2 와 fog3d_* 를 import 만 한다
실행 명령: PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_blend_b4_replay.py frame-check [--jobs N] | build [--jobs N] [--force] [--only 이름,…] | check [--jobs N] [--only 이름,…] | numbers --out <json> | list
출력: 재생 묶음 Tools/FluidFX/fog3d_ref/replays_blend_b4/(재생 json·.ground.bin·manifest.json), 기준 덤프 Saved/FluidFX/Fog3D/ref_blend_b4/(.npz·.column.bin·.frames.bin·manifest.json·shake_diff.json)와 흔든 덤프 ref_blend_b4/shake/, numbers 는 숫자 파일 json
상태: 바람 섞기 새 식(B4) GPU 반영의 기준 덤프 생성 도구(Docs/VolumeFog3D/stage2/impl-blend-contract.md 5절, 등록 prereg-blend-impl.md 4절), 2026-11-17
"""
import os

for _thread_var in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_thread_var, "1")

import argparse
import concurrent.futures
import copy
import json
import math
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import fog3d_blend_patch as blend_patch  # noqa: E402

blend_patch.install_from_environment()

import numpy as np  # noqa: E402

import fog3d_blend_gates as blend_gates  # noqa: E402
import fog3d_blend_probe as probe  # noqa: E402
import fog3d_compare as base  # noqa: E402
import fog3d_compare_v2 as v2  # noqa: E402
import fog3d_core as core  # noqa: E402
import fog3d_replay as rp  # noqa: E402
import fog3d_replay_s2a as rs  # noqa: E402

ROOT = core.ROOT
SCENE = rs.SCENE
BASE_DT = SCENE["dt"]
FORMULA = "B4"
CANDIDATE = "R5"
REPLAYS = os.path.join(HERE, "replays_blend_b4")
REF = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_blend_b4")
S2A_REPLAYS = os.path.join(HERE, "replays_s2a", CANDIDATE)
S2A_REF = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_s2a", CANDIDATE)
FRAME_CHECK_SCENES = ("s2a_cliff_at_edge", "s2a_slope_walk_up")
SHAKE_FOLDER = rs.SHAKE_FOLDER
MAX_JOBS = 8
GROUPS = ("wind", "disp", "fill")
DEFAULT_LIMITS = dict(relative=dict(wind=0.002, disp=0.002, fill=0.002), slope=1.0e-6, frames_position_cm=0.001, frames_slope=1.0e-5)
LIMIT_FLOOR = 0.002
LIMIT_CEILING = 0.03
NUMBERS_FORMAT = "fog3d-prereg-blend-impl-numbers-1"
NUMBERS_RULE = ("X = min(0.03, max(0.002, 3 x sensitivity)); sensitivity = fog3d_compare_v2.group_diffs(B4 reference, shaken B4 reference); "
                "only dumps and groups with X > 0.002 are listed under dumps, all others use default.relative")
TOOL_PATH = os.path.abspath(__file__)

ORIGINAL_CORE_SPHERES = rp.core_spheres


def core_spheres_with_gain(spheres):
    out = ORIGINAL_CORE_SPHERES(spheres)
    for sphere, raw in zip(out, spheres):
        if "g" in raw:
            sphere.wind_gain = float(raw["g"])
    return out


rp.core_spheres = core_spheres_with_gain
rp.make_sim = rs.make_sim_s2a


def gain_label(gain):
    return f"g{int(round(gain * 100)):03d}"


def build_new_specs():
    specs = {}
    for hz, gain in ((60, 1.0), (60, 1.25), (30, 1.0), (30, 1.25), (144, 1.0)):
        specs[f"b4_walk_stand_h{hz}_{gain_label(gain)}"] = dict(kind="walk_stand", hz=hz, gain=gain)
    for kind, label in (("blade", "blade"), ("turn", "turn_stand")):
        for hz, gain in ((60, 1.0), (60, 1.25), (30, 1.0), (30, 1.25)):
            specs[f"b4_{label}_h{hz}_{gain_label(gain)}"] = dict(kind=kind, hz=hz, gain=gain)
    specs["b4_slope_walk_up_h60_g125"] = dict(kind="slope_walk_up", hz=60, gain=1.25)
    return specs


NEW_SPECS = build_new_specs()
S2A_NAMES = list(rs.scenes_for(CANDIDATE))
ALL_NAMES = S2A_NAMES + list(NEW_SPECS)


def sha256_file(path):
    return rs.sha256_file(path)


def step_index(seconds, dt):
    return int(round(seconds / dt))


def named_dumps(pairs, total):
    out = {}
    for index, name in pairs:
        if not 1 <= index <= total:
            raise ValueError(f"덤프 걸음 {index}({name}) 이 1..{total} 밖")
        if index in out:
            raise ValueError(f"덤프 걸음 {index} 가 겹침: {out[index]}, {name}")
        out[index] = name
    return out


def add_steps(replay, steps, dt, dumps):
    for k, spheres in enumerate(steps, start=1):
        replay.step(dt, spheres, dump=dumps.get(k))


def standing_spheres(last_step):
    out = []
    for sphere in last_step:
        held = copy.copy(sphere)
        held.a = held.b
        held.velocity = (0.0, 0.0, 0.0)
        out.append(held)
    return out


def build_flat_replay(name, spec):
    dt = 1.0 / spec["hz"]
    kind = spec["kind"]
    if kind == "walk_stand":
        steps, moving = probe.walk_stand_inputs(SCENE["body_wind"], dt)
        total = len(steps)
        dumps = named_dumps([(moving, "walk_end"), (moving + step_index(0.5, dt), "stand_0p5"), (moving + step_index(1.5, dt), "stand_1p5"), (total, "end")], total)
        info = dict(walk_steps=moving, stop_steps=total - moving, speed_cm_s=SCENE["walk_speed"], strength=SCENE["body_wind"])
    elif kind == "blade":
        steps, moving = probe.blade_inputs(SCENE["blade_wind"], dt)
        total = len(steps)
        dumps = named_dumps([(moving, "swing_end"), (moving + step_index(0.25, dt), "tail_0p25"), (total, "end")], total)
        info = dict(swing_steps=moving, tail_steps=total - moving, speed_cm_s=SCENE["blade_speed"], strength=SCENE["blade_wind"])
    elif kind == "turn":
        steps, moving = blend_gates.turn_steps(SCENE["body_wind"], dt, 1.0)
        leg = moving // 2
        stand = step_index(120 * BASE_DT, dt)
        steps = list(steps) + [standing_spheres(steps[-1]) for _ in range(stand)]
        total = len(steps)
        dumps = named_dumps([(leg + 1, "turn"), (moving, "turn_walk_end"), (moving + step_index(1.0, dt), "stand_1p0"), (total, "end")], total)
        info = dict(leg_steps=leg, stop_steps=stand, speed_cm_s=SCENE["walk_speed"], strength=SCENE["body_wind"])
    else:
        raise ValueError(f"모르는 종류: {kind}")
    replay = rs.new_replay(name)
    add_steps(replay, steps, dt, dumps)
    replay.body["b4_scene"] = dict(kind=kind, steps_per_second=spec["hz"], gain=spec["gain"], dumps={v: k for k, v in dumps.items()}, **info)
    return replay.finish(), "F0"


def build_slope_replay(name, spec):
    replay, terrain_name = rs.scene_walk(name, "S30", (0.0, 0.0), (1.0, 0.0))
    replay["b4_scene"] = dict(kind=spec["kind"], steps_per_second=spec["hz"], gain=spec["gain"], base_scene="s2a_slope_walk_up")
    return replay, terrain_name


def finalize_b4(replay, terrain_name, gain, from_tool):
    replay["source"] = dict(tool="fog3d_blend_b4_replay.py", code=rs.code_fingerprint(rs.R5_CODE), from_tool=from_tool)
    image = rs.attach_ground(replay, terrain_name, CANDIDATE)
    problems = rp.check_replay(replay) + rs.check_ground(replay, image)
    if problems:
        raise ValueError(f"{replay['name']} 형식 문제: {problems}")
    for step in replay["steps"]:
        for sphere in step["spheres"]:
            sphere["g"] = float(gain)
    replay["wind_blend"] = FORMULA
    return image


def scene_for(name, flavor):
    if flavor == "frame":
        replay, image = rs.build_scene(name, CANDIDATE)
        return replay, image, False
    if name in S2A_NAMES:
        replay, image = rs.build_scene(name, CANDIDATE)
        replay["source"] = dict(tool="fog3d_blend_b4_replay.py", code=rs.code_fingerprint(rs.R5_CODE), from_tool="fog3d_replay_s2a.py")
        for step in replay["steps"]:
            for sphere in step["spheres"]:
                sphere["g"] = 1.0
        replay["wind_blend"] = FORMULA
        return replay, image, False
    spec = NEW_SPECS[name]
    if spec["kind"] == "slope_walk_up":
        replay, terrain_name = build_slope_replay(name, spec)
    else:
        replay, terrain_name = build_flat_replay(name, spec)
    image = finalize_b4(replay, terrain_name, spec["gain"], "fog3d_blend_b4_replay.py")
    is_flat_zero = terrain_name == "F0" and bool(np.all(image == 0.0))
    return replay, image, is_flat_zero


def check_b4_replay(replay, image):
    problems = []
    stripped = copy.deepcopy(replay)
    stripped.pop("wind_blend", None)
    stripped.pop("b4_scene", None)
    for step in stripped["steps"]:
        for sphere in step["spheres"]:
            sphere.pop("g", None)
    problems += rp.check_replay(stripped) + rs.check_ground(stripped, image)
    if replay.get("wind_blend") != FORMULA:
        problems.append("wind_blend")
    for index, step in enumerate(replay["steps"]):
        for sphere in step["spheres"]:
            if not isinstance(sphere.get("g"), float) or not math.isfinite(sphere["g"]):
                problems.append(f"steps[{index}] g")
                return problems
    return problems


def scene_cost(name):
    replay, _, _ = scene_for(name, "b4")
    return sum(int(s.get("repeat", 1)) for s in replay["steps"])


def run_job(job):
    kind, name, flavor, ref_dir = job
    started = time.time()
    replay, image, _ = scene_for(name, flavor)
    if flavor == "b4":
        problems = check_b4_replay(replay, image)
        if problems:
            raise ValueError(f"{name}: {problems}")
    out = ref_dir if kind == "ref" else os.path.join(ref_dir, SHAKE_FOLDER)
    result = rs.run_replay(replay, image, out, shake=(kind == "shake"))
    return dict(kind=kind, name=name, steps=result["steps"], hash=result["hash"], files=result["files"], walls_last=result["walls"],
                blend_counts=result["blend_counts"], seconds=round(time.time() - started, 1))


def run_pool(jobs, workers):
    costs = {name: scene_cost(name) for name in {j[1] for j in jobs}}
    jobs = sorted(jobs, key=lambda j: -costs[j[1]])
    infos = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=max(1, min(workers, MAX_JOBS))) as pool:
        for info in pool.map(run_job, jobs):
            print(json.dumps({k: v for k, v in info.items() if k != "files"}, ensure_ascii=False), flush=True)
            infos.append(info)
    return infos


def tree_hashes(folder):
    out = {}
    for base_dir, _, files in os.walk(folder):
        for f in files:
            path = os.path.join(base_dir, f)
            out[os.path.relpath(path, folder).replace("\\", "/")] = sha256_file(path)
    return out


def write_json(path, body):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(body, fh, ensure_ascii=False, indent=1)
        fh.write("\n")


def read_json(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def kept_manifest_entries(replays_dir):
    groups, current = {}, None
    for entry in read_json(os.path.join(replays_dir, "manifest.json"))["files"]:
        if "scene" in entry:
            current = entry["scene"]
            groups[current] = []
        groups[current].append(entry)
    return groups


def parse_only(text):
    names = [n for n in text.split(",") if n]
    unknown = [n for n in names if n not in ALL_NAMES]
    if unknown or not names:
        raise SystemExit(f"--only 에 모르는 장면: {unknown}")
    return [n for n in ALL_NAMES if n in names]


def write_replays(replays_dir, names, order=None):
    os.makedirs(replays_dir, exist_ok=True)
    kept = kept_manifest_entries(replays_dir) if order else {}
    files = []
    twins = {}
    for name in order or names:
        if name not in names:
            files += kept[name]
            continue
        replay, image, is_flat_zero = scene_for(name, "b4")
        problems = check_b4_replay(replay, image)
        if problems:
            raise ValueError(f"{name}: {problems}")
        rs.write_replay(replays_dir, replay, image)
        twins[name] = is_flat_zero
        files.append(dict(name=f"{name}.json", scene=name, ground_off_twin=is_flat_zero))
        files.append(dict(name=replay["ground"]["file"]))
    write_json(os.path.join(replays_dir, "manifest.json"), dict(files=files))
    return twins


def shake_table(ref_dir, names, replays_dir):
    out = {}
    for name in names:
        with open(os.path.join(replays_dir, f"{name}.json"), encoding="utf-8") as fh:
            replay = json.load(fh)
        rows = {}
        for dump in rs.dump_steps(replay):
            ref = base.load_ref(ref_dir, name, dump)
            shaken = base.load_ref(os.path.join(ref_dir, SHAKE_FOLDER), name, dump)
            groups = v2.group_diffs(ref, shaken)
            row = {group: float(groups[group]) for group in GROUPS}
            row["fluid_mismatch"] = v2.fluid_mismatch(ref, shaken)
            ca = rs.load_columns(os.path.join(ref_dir, f"{name}__{dump}.column.bin"), int(ref.meta["n"]))
            cb = rs.load_columns(os.path.join(ref_dir, SHAKE_FOLDER, f"{name}__{dump}.column.bin"), int(ref.meta["n"]))
            row["columns_max"] = float(np.abs(ca.astype(np.float64) - cb).max())
            rows[dump] = row
        out[name] = rows
    return out


def produce(names, jobs, replays_dir, ref_dir, order=None):
    old_manifest = read_json(os.path.join(ref_dir, "manifest.json")) if order else None
    old_table = read_json(os.path.join(ref_dir, "shake_diff.json"))["scenes"] if order else {}
    twins = write_replays(replays_dir, names, order)
    infos = run_pool([(kind, name, "b4", ref_dir) for name in names for kind in ("ref", "shake")], jobs)
    table = shake_table(ref_dir, names, replays_dir)
    if order:
        table = {name: table[name] if name in table else old_table[name] for name in order}
    write_json(os.path.join(ref_dir, "shake_diff.json"), dict(format="fog3d-shake-diff-blend-b4-1", shake_cm=rp.SENSITIVITY_SHAKE_CM, seed=rp.SENSITIVITY_SEED,
                                                              rule="덤프·묶음마다 fog3d_compare_v2.group_diffs(기준, 흔든 기준)", scenes=table))
    scenes = dict(old_manifest["scenes"]) if order else {}
    for info in sorted((i for i in infos if i["kind"] == "ref"), key=lambda i: i["name"]):
        scenes[info["name"]] = dict(steps=info["steps"], state_hash=info["hash"], walls_last=info["walls_last"], blend_counts=info["blend_counts"],
                                    ground_off_twin=twins[info["name"]], files=info["files"])
    scenes = {name: scenes[name] for name in sorted(scenes)}
    hashes = tree_hashes(ref_dir)
    hashes.pop("manifest.json", None)
    rows = [dict(name=k, bytes=os.path.getsize(os.path.join(ref_dir, k)), sha256=hashes[k]) for k in sorted(hashes)]
    listing = "".join(f"{r['name']} {r['bytes']} {r['sha256']}\n" for r in rows)
    write_json(os.path.join(ref_dir, "manifest.json"), dict(format="fog3d-ref-manifest-blend-b4-1", formula=FORMULA, candidate=CANDIDATE, advect="N2", solid="S9",
                                                            code=rs.code_fingerprint(rs.R5_CODE), tool_sha256=sha256_file(TOOL_PATH),
                                                            patch_sha256=sha256_file(os.path.join(HERE, "fog3d_blend_patch.py")), files=rows,
                                                            listing_sha256=rs.sha256_bytes(listing.encode()), scenes=scenes))
    return infos


def refuse_existing(paths):
    present = [p for p in paths if os.path.exists(p) and os.listdir(p)]
    if present:
        print("이미 있음(--force 로만 덮음):", *present, sep="\n  ")
        return True
    return False


def cmd_build_only(args):
    names = parse_only(args.only)
    for name in names:
        for folder in (REF, os.path.join(REF, SHAKE_FOLDER)):
            for f in os.listdir(folder):
                if f.startswith(name + "__"):
                    os.remove(os.path.join(folder, f))
    started = time.time()
    produce(names, args.jobs, REPLAYS, REF, ALL_NAMES)
    print(f"build --only 끝: 장면 {len(names)}개, {round(time.time() - started, 1)}초")
    return 0


def cmd_build(args):
    blend_patch.install(FORMULA)
    if args.only:
        return cmd_build_only(args)
    folders = (REPLAYS, REF)
    if refuse_existing(folders) and not args.force:
        return 1
    for folder in folders:
        shutil.rmtree(folder, ignore_errors=True)
    started = time.time()
    produce(ALL_NAMES, args.jobs, REPLAYS, REF)
    print(f"build 끝: 장면 {len(ALL_NAMES)}개, {round(time.time() - started, 1)}초")
    return 0


def scene_key(key, names):
    return any(key.startswith((f"{n}.", f"{n}__", f"{SHAKE_FOLDER}/{n}__")) for n in names)


def cmd_check(args):
    blend_patch.install(FORMULA)
    names = parse_only(args.only) if args.only else ALL_NAMES
    tmp = tempfile.mkdtemp(prefix="fog3d_b4_check_")
    try:
        t_replays, t_ref = os.path.join(tmp, "replays"), os.path.join(tmp, "ref")
        produce(names, args.jobs, t_replays, t_ref)
        bad = 0
        if args.only:
            stored_table, fresh_table = read_json(os.path.join(REF, "shake_diff.json"))["scenes"], read_json(os.path.join(t_ref, "shake_diff.json"))["scenes"]
            different_rows = [n for n in names if stored_table.get(n) != fresh_table.get(n)]
            for name in different_rows:
                print("다름: shake_diff.json", name)
            bad += len(different_rows)
        for stored, fresh in ((REPLAYS, t_replays), (REF, t_ref)):
            a, b = tree_hashes(stored), tree_hashes(fresh)
            if args.only:
                a, b = ({k: v for k, v in t.items() if scene_key(k, names)} for t in (a, b))
            keys = sorted(set(a) | set(b))
            different = [k for k in keys if a.get(k) != b.get(k)]
            for key in different:
                print("다름:", os.path.join(stored, key))
            print(f"{stored}: 대조 {len(keys)}개, 다른 것 {len(different)}개")
            bad += len(different)
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def cmd_frame_check(args):
    blend_patch.install("B0")
    tmp = tempfile.mkdtemp(prefix="fog3d_b4_frame_")
    try:
        t_replays, t_ref = os.path.join(tmp, "replays"), os.path.join(tmp, "ref")
        os.makedirs(t_replays)
        for name in FRAME_CHECK_SCENES:
            replay, image, _ = scene_for(name, "frame")
            rs.write_replay(t_replays, replay, image)
        run_pool([("ref", name, "frame", t_ref) for name in FRAME_CHECK_SCENES], args.jobs)
        bad, compared = 0, 0
        for name in FRAME_CHECK_SCENES:
            pairs = [(os.path.join(t_replays, f"{name}.json"), os.path.join(S2A_REPLAYS, f"{name}.json")),
                     (os.path.join(t_replays, f"{name}.ground.bin"), os.path.join(S2A_REPLAYS, f"{name}.ground.bin"))]
            stems = sorted(f for f in os.listdir(S2A_REF) if f.startswith(name + "__"))
            if not stems:
                print("기존 기준 덤프 없음:", name)
                bad += 1
            pairs += [(os.path.join(t_ref, f), os.path.join(S2A_REF, f)) for f in stems]
            fresh_files = sorted(f for f in os.listdir(t_ref) if f.startswith(name + "__")) if os.path.isdir(t_ref) else []
            if fresh_files != stems:
                print("파일 목록 다름:", name, set(fresh_files) ^ set(stems))
                bad += 1
            for fresh, stored in pairs:
                compared += 1
                if not os.path.isfile(fresh) or not os.path.isfile(stored) or sha256_file(fresh) != sha256_file(stored):
                    print("다름:", fresh, stored)
                    bad += 1
        print(f"frame-check: 장면 {len(FRAME_CHECK_SCENES)}개, 바이트 대조 {compared}개, 다른 것 {bad}개")
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def limit_for(sensitivity):
    return min(LIMIT_CEILING, max(LIMIT_FLOOR, 3.0 * sensitivity))


def cmd_numbers(args):
    path = os.path.join(REF, "shake_diff.json")
    if not os.path.isfile(path):
        print("shake_diff.json 없음: build 를 먼저")
        return 2
    with open(path, encoding="utf-8") as fh:
        shake = json.load(fh)
    sensitivity, dumps = {}, {}
    group_max = {g: 0.0 for g in GROUPS}
    wide_pairs = {g: 0 for g in GROUPS}
    for scene, rows in sorted(shake["scenes"].items()):
        for dump, row in rows.items():
            key = f"{scene}__{dump}"
            sensitivity[key] = {g: row[g] for g in GROUPS}
            relative = {}
            for g in GROUPS:
                group_max[g] = max(group_max[g], row[g])
                limit = limit_for(row[g])
                if limit > LIMIT_FLOOR:
                    relative[g] = limit
                    wide_pairs[g] += 1
            if relative:
                dumps[key] = dict(relative=relative)
    body = dict(version=2, format=NUMBERS_FORMAT, candidate=CANDIDATE, formula=FORMULA, rule=NUMBERS_RULE, default=DEFAULT_LIMITS, dumps=dumps,
                sensitivity=sensitivity, tool_sha256=sha256_file(TOOL_PATH), source_shake_diff_sha256=sha256_file(path))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    write_json(args.out, body)
    print(json.dumps(dict(out=args.out, sha256=sha256_file(args.out), dumps_total=len(sensitivity), dumps_with_wider_limit=len(dumps), wider_pairs=wide_pairs,
                          sensitivity_max=group_max), ensure_ascii=False))
    return 0


def cmd_list(args):
    for name in ALL_NAMES:
        replay, _, is_flat_zero = scene_for(name, "b4")
        print(json.dumps(dict(name=name, steps=sum(int(s.get("repeat", 1)) for s in replay["steps"]), dumps=rs.dump_steps(replay), ground_off_twin=is_flat_zero),
                         ensure_ascii=False))
    return 0


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("frame-check", "build", "check"):
        p = sub.add_parser(name)
        p.add_argument("--jobs", type=int, default=6)
        if name == "build":
            p.add_argument("--force", action="store_true")
        if name in ("build", "check"):
            p.add_argument("--only", default="")
    p = sub.add_parser("numbers")
    p.add_argument("--out", required=True)
    sub.add_parser("list")
    args = ap.parse_args(argv)
    commands = {"frame-check": cmd_frame_check, "build": cmd_build, "check": cmd_check, "numbers": cmd_numbers, "list": cmd_list}
    return commands[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
