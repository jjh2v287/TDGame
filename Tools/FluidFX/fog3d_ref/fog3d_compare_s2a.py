"""
실행 환경: Python 3.12 + numpy 2.2 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_compare.py·fog3d_compare_v2.py 를 import 만 한다
실행 명령: PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_compare_s2a.py --numbers <숫자 JSON 판 2> [--gpu-dir D] [--ref-dir D] [--scenes a,b] [--report J] [--verbose] | --self-test
출력: 장면·덤프·항목별 판정 표와 정보 표(표준 출력), --report JSON. 종료 코드 0 = 모두 통과, 1 = 실패 있음, 2 = GPU 덤프가 하나도 없음·읽기 오류
상태: 2a 단계 4 GPU 대 기준 덤프 대조(계약 §8, 3차원 장은 1단계 1d 와 같은 묶음별 장 차이). 허용 숫자는 감독이 잠근 파일에서만 읽는다. 2026-11-15
"""
import argparse
import json
import os
import shutil
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fog3d_compare as base  # noqa: E402
import fog3d_compare_v2 as v2  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
FOG = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D")
REF_DIR = os.path.join(FOG, "ref_s2a")
GPU_DIR = os.path.join(FOG, "gpu_s2a")
F32 = np.float32
GROUPS = ("wind", "disp", "fill")
FIELDS = ("u", "v", "w", "dx", "dy", "dz", "g", "pressure", "divergence")
POSITION_COLUMNS = (0, 1, 2, 3, 4, 5, 6, 8, 9, 12, 13)
SLOPE_COLUMNS = (10, 11)
FRAME_WIDTH = 16


def valid_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and value >= 0


def relative_limit(numbers, scene, dump, group):
    entry = numbers.get("dumps", {}).get(f"{scene}__{dump}", {}).get("relative", {})
    if valid_number(entry.get(group)):
        return float(entry[group])
    value = numbers.get("default", {}).get("relative", {}).get(group)
    return float(value) if valid_number(value) else None


def default_limit(numbers, name):
    value = numbers.get("default", {}).get(name)
    return float(value) if valid_number(value) else None


def stem_of(folder, scene, dump):
    return os.path.join(folder, f"{scene}__{dump}")


def read_column(path, n):
    raw = np.fromfile(path, dtype="<f4")
    if raw.size != 4 * n * n:
        raise base.DumpError(f"기둥 장 크기 불일치 {path}: 기대 {4 * n * n}개, 실제 {raw.size}개")
    return raw.reshape(4, n, n)


def read_frames(path):
    raw = np.fromfile(path, dtype="<f4")
    if raw.size % FRAME_WIDTH:
        raise base.DumpError(f"입력 틀 크기 오류 {path}: {raw.size}개는 {FRAME_WIDTH} 의 배수가 아님")
    return raw.reshape(-1, FRAME_WIDTH)


class Judge:
    def __init__(self, numbers):
        self.numbers = numbers
        self.rows = []
        self.infos = []
        self.frames_skipped = 0

    def add(self, scene, dump, item, value, limit, passed, info=""):
        ratio = None
        if value is not None and limit is not None:
            ratio = (0.0 if value == 0 else float("inf")) if limit == 0 else value / limit
        self.rows.append(dict(scene=scene, dump=dump, item=item, value=value, limit=limit, ratio=ratio, passed=bool(passed), info=info))

    def limit_row(self, scene, dump, item, name, value):
        limit = default_limit(self.numbers, name)
        if limit is None:
            self.add(scene, dump, item, value, None, False, f"숫자 없음: {name}")
            return
        self.add(scene, dump, item, value, limit, value <= limit)

    def dump(self, scene, dump, ref_dir, gpu_dir):
        ref = base.load_ref(ref_dir, scene, dump)
        try:
            gpu = base.load_gpu(gpu_dir, scene, dump)
        except base.DumpError as err:
            self.add(scene, dump, "덤프 없음", None, None, False, str(err))
            return
        self.fields(scene, dump, ref, gpu)
        self.columns(scene, dump, ref, gpu, ref_dir, gpu_dir)
        self.frames(scene, dump, ref, ref_dir, gpu_dir)

    def fields(self, scene, dump, ref, gpu):
        if "fluid" not in gpu.fields:
            self.add(scene, dump, "유체 표시", None, 0, False, "GPU 덤프에 fluid 필드 없음")
            return
        bad = v2.fluid_mismatch(ref, gpu)
        self.add(scene, dump, "유체 표시", bad, 0, bad == 0, "다른 칸 수")
        finite = all(np.isfinite(a).all() for a in gpu.fields.values())
        self.add(scene, dump, "유한값(GPU 3차원 장)", None, None, finite, "" if finite else "NaN 또는 무한 있음")
        try:
            diffs = v2.group_diffs(ref, gpu)
        except base.DumpError as err:
            self.add(scene, dump, "장 차이", None, None, False, str(err))
            diffs = {}
        for group in GROUPS:
            if group not in diffs:
                continue
            value = float(diffs[group])
            limit = relative_limit(self.numbers, scene, dump, group)
            if limit is None:
                self.add(scene, dump, f"장 차이 {group}", value, None, False, f"숫자 없음: relative.{group}")
                continue
            self.add(scene, dump, f"장 차이 {group}", value, limit, value <= limit)
        both = (ref.fields["fluid"] > 0.5) & (gpu.fields["fluid"] > 0.5)
        info = {}
        for name in FIELDS:
            if name in ref.fields and name in gpu.fields:
                diff = np.abs(gpu.fields[name].astype(np.float64) - ref.fields[name].astype(np.float64))[both]
                info[name] = float("nan") if np.isnan(diff).any() else (float(diff.max()) if diff.size else 0.0)
        self.infos.append(dict(scene=scene, dump=dump, max_abs_diff=info))

    def columns(self, scene, dump, ref, gpu, ref_dir, gpu_dir):
        ref_path = stem_of(ref_dir, scene, dump) + ".column.bin"
        if not os.path.isfile(ref_path):
            return
        gpu_path = stem_of(gpu_dir, scene, dump) + ".column.bin"
        if not os.path.isfile(gpu_path):
            self.add(scene, dump, "기둥 장 없음", None, None, False, gpu_path)
            return
        try:
            a, b = read_column(ref_path, ref.n), read_column(gpu_path, ref.n)
        except base.DumpError as err:
            self.add(scene, dump, "기둥 장 읽기", None, None, False, str(err))
            return
        finite = bool(np.isfinite(b).all())
        self.add(scene, dump, "유한값(GPU 기둥 장)", None, None, finite, "" if finite else "NaN 또는 무한 있음")
        same = int(np.count_nonzero(a[0].view(np.uint32) != b[0].view(np.uint32)))
        self.add(scene, dump, "ground_h 비트 같음", same, 0, same == 0, "다른 기둥 수")
        walls = int(np.count_nonzero(a[3] != b[3]))
        self.add(scene, dump, "wall 같음", walls, 0, walls == 0, "다른 기둥 수")
        for index, label in ((1, "slope_x"), (2, "slope_y")):
            diff = np.abs(b[index].astype(np.float64) - a[index].astype(np.float64))
            worst = float("inf") if np.isnan(diff).any() else float(diff.max())
            self.limit_row(scene, dump, label, "slope", worst)

    def frames(self, scene, dump, ref, ref_dir, gpu_dir):
        ref_path = stem_of(ref_dir, scene, dump) + ".frames.bin"
        if not os.path.isfile(ref_path):
            return
        a = read_frames(ref_path)
        if a.shape[0] == 0:
            return
        gpu_path = stem_of(gpu_dir, scene, dump) + ".frames.bin"
        if not os.path.isfile(gpu_path):
            self.frames_skipped += 1
            self.add(scene, dump, "입력 틀", None, None, True, "틀 없음(건너뜀)")
            return
        try:
            b = read_frames(gpu_path)
        except base.DumpError as err:
            self.add(scene, dump, "입력 틀 읽기", None, None, False, str(err))
            return
        if b.shape != a.shape:
            self.add(scene, dump, "입력 틀 개수", b.shape[0], a.shape[0], False, "구 수 불일치")
            return
        finite = bool(np.isfinite(b).all())
        self.add(scene, dump, "유한값(GPU 입력 틀)", None, None, finite, "" if finite else "NaN 또는 무한 있음")
        for label, columns, name in (("틀 자리(cm)", POSITION_COLUMNS, "frames_position_cm"), ("틀 기울기", SLOPE_COLUMNS, "frames_slope")):
            diff = np.abs(b[:, columns].astype(np.float64) - a[:, columns].astype(np.float64))
            worst = float("inf") if np.isnan(diff).any() else float(diff.max())
            self.limit_row(scene, dump, label, name, worst)


def scenes_of(ref_dir):
    found = {}
    for f in sorted(os.listdir(ref_dir)):
        if f.endswith(".npz") and "__" in f:
            scene, dump = f[:-4].split("__", 1)
            found.setdefault(scene, []).append(dump)
    return found


def run(numbers, ref_dir, gpu_dir, scenes=None):
    judge = Judge(numbers)
    available = scenes_of(ref_dir)
    chosen = sorted(available) if scenes is None else scenes
    for scene in chosen:
        if scene not in available:
            judge.add(scene, "-", "기준 덤프 없음", None, None, False, ref_dir)
            continue
        for dump in available[scene]:
            judge.dump(scene, dump, ref_dir, gpu_dir)
    return judge


def exit_code(judge):
    gpu_found = any(r["item"] not in ("덤프 없음", "기준 덤프 없음") for r in judge.rows)
    if not gpu_found:
        return 2
    return 1 if any(not r["passed"] for r in judge.rows) else 0


def fmt(v):
    if v is None:
        return "-"
    return f"{v:.6g}" if isinstance(v, float) else str(v)


def closest(judge):
    ranked = [r for r in judge.rows if r["item"].startswith("장 차이") and r["ratio"] is not None]
    return max(ranked, key=lambda r: r["ratio"], default=None)


def print_table(judge, verbose):
    print(f"{'장면':<26}{'덤프':<8}{'항목':<22}{'값':<14}{'허용':<14}판정")
    for r in judge.rows:
        if r["passed"] and not verbose:
            continue
        print(f"{r['scene']:<26}{r['dump']:<8}{r['item']:<22}{fmt(r['value']):<14}{fmt(r['limit']):<14}{'통과' if r['passed'] else '실패'} {r['info']}".rstrip())
    print("정보(판정 안 함): 장별 최대 |차|, 두 쪽 모두 유체인 칸")
    for i in judge.infos:
        print(f"{i['scene']:<26}{i['dump']:<8}" + " ".join(f"{k}={v:.3g}" for k, v in i["max_abs_diff"].items()))
    failed = [r for r in judge.rows if not r["passed"]]
    near = closest(judge)
    tail = f", 가장 한계에 가까운 것 {near['scene']}__{near['dump']} {near['item']} 비 {fmt(near['ratio'])}" if near else ""
    print(f"검사 {len(judge.rows)}개, 실패 {len(failed)}개, 틀 없음(건너뜀) {judge.frames_skipped}개{tail}")


def write_report(judge, path, code):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    failed = [r for r in judge.rows if not r["passed"]]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(dict(format="fog3d-verdict-s2a-2", exit_code=code, checks=len(judge.rows), failed=len(failed), frames_skipped=judge.frames_skipped,
                       closest=closest(judge), info_max_abs_diff=judge.infos, rows=judge.rows), fh, ensure_ascii=False, indent=1, default=float)
        fh.write("\n")


def load_numbers(path):
    with open(path, encoding="utf-8") as fh:
        numbers = json.load(fh)
    if numbers.get("version") != 2:
        raise base.DumpError(f"숫자 파일 version 이 2 가 아님(판 1 꼴은 받지 않는다): {path}")
    return numbers


def write_gpu_copy(gpu_dir, scene, dump, ref_dir, mutate=None):
    ref = base.load_ref(ref_dir, scene, dump)
    column = read_column(stem_of(ref_dir, scene, dump) + ".column.bin", ref.n).copy()
    frames = read_frames(stem_of(ref_dir, scene, dump) + ".frames.bin").copy()
    if mutate:
        mutate(ref, column, frames)
    base.write_gpu_format(gpu_dir, scene, dump, ref)
    column.astype("<f4").tofile(stem_of(gpu_dir, scene, dump) + ".column.bin")
    frames.astype("<f4").tofile(stem_of(gpu_dir, scene, dump) + ".frames.bin")


def first_cell(ref, fluid):
    return tuple(int(x[0]) for x in np.nonzero((ref.fields["fluid"] > 0.5) == fluid))


def self_test(ref_dir):
    scene, dump = "s2a_cliff_at_edge", "w70"
    zero_scene, zero_dump = "s2a_slope_walk_up", "w35"
    for sc, du in ((scene, dump), (zero_scene, zero_dump)):
        if not os.path.isfile(stem_of(ref_dir, sc, du) + ".npz"):
            print(f"기준 덤프 없음: {stem_of(ref_dir, sc, du)}.npz")
            return 2
    zero_ref = base.load_ref(ref_dir, zero_scene, zero_dump)
    if float(np.abs(zero_ref.fields["g"][zero_ref.fields["fluid"] > 0.5]).max()) != 0.0:
        print(f"{zero_scene}__{zero_dump} 의 fill 이 모두 0 이 아님: 자체 점검 전제 깨짐")
        return 1
    numbers = dict(version=2, default=dict(relative=dict(wind=1.0e-3, disp=1.0e-3, fill=1.0e-3), slope=1.0e-6, frames_position_cm=1.0e-3, frames_slope=1.0e-6), dumps={})
    no_fill = dict(numbers, default=dict(numbers["default"], relative=dict(wind=1.0e-3, disp=1.0e-3)))

    def big_wind(ref, column, frames):
        fluid = ref.fields["fluid"] > 0.5
        norm = float(np.sqrt(sum(float(np.sum(ref.fields[k][fluid].astype(np.float64) ** 2)) for k in ("u", "v", "w"))))
        ref.fields["u"][first_cell(ref, True)] += F32(0.1 * norm)

    def tiny_on_zero_fill(ref, column, frames):
        ref.fields["g"][first_cell(ref, True)] += F32(1.0e-9)

    def ground_ulp(ref, column, frames):
        column[0, 5, 5] = np.nextafter(column[0, 5, 5], F32(np.inf))

    def wall_flip(ref, column, frames):
        column[3, 7, 7] = 1.0 - column[3, 7, 7]

    def fluid_flip(ref, column, frames):
        ref.fields["fluid"][first_cell(ref, True)] = 0.0

    def nan_in(ref, column, frames):
        ref.fields["pressure"][first_cell(ref, False)] = np.nan

    def frame_move(ref, column, frames):
        frames[0, 0] += 1.0

    cases = (
        ("그대로", scene, dump, None, numbers, None),
        ("한 묶음에 한계를 넘는 차", scene, dump, big_wind, numbers, "장 차이 wind"),
        ("기준이 모두 0 인 묶음에 아주 작은 값", zero_scene, zero_dump, tiny_on_zero_fill, numbers, "장 차이 fill"),
        ("ground_h 1ulp", scene, dump, ground_ulp, numbers, "ground_h 비트 같음"),
        ("wall 뒤집기", scene, dump, wall_flip, numbers, "wall 같음"),
        ("유체 표시 바꾸기", scene, dump, fluid_flip, numbers, "유체 표시"),
        ("NaN 넣기", scene, dump, nan_in, numbers, "유한값(GPU 3차원 장)"),
        ("숫자 파일에 묶음 하나 빠짐", scene, dump, None, no_fill, "장 차이 fill"),
        ("틀 자리 이동", scene, dump, frame_move, numbers, "틀 자리(cm)"),
    )
    ok = True
    for label, sc, du, mutate, nums, expect in cases:
        tmp = tempfile.mkdtemp()
        try:
            write_gpu_copy(tmp, sc, du, ref_dir, mutate)
            judge = run(nums, ref_dir, tmp, [sc])
            judge.rows = [r for r in judge.rows if r["dump"] == du]
            failed = sorted({r["item"] for r in judge.rows if not r["passed"]})
            has_frames = any(r["item"].startswith("틀") for r in judge.rows)
            good = (not failed and has_frames) if expect is None else (expect in failed)
            print(f"{'통과' if good else '실패'}: {label} -> 실패 항목 {failed}")
            ok = ok and good
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    tmp = tempfile.mkdtemp()
    try:
        write_gpu_copy(tmp, scene, dump, ref_dir)
        os.remove(stem_of(tmp, scene, dump) + ".frames.bin")
        judge = run(numbers, ref_dir, tmp, [scene])
        judge.rows = [r for r in judge.rows if r["dump"] == dump]
        good = judge.frames_skipped == 1 and all(r["passed"] for r in judge.rows)
        print(f"{'통과' if good else '실패'}: 틀 파일 없음 -> 건너뜀 {judge.frames_skipped}")
        ok = ok and good
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("자체 점검", "통과" if ok else "실패")
    return 0 if ok else 1


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--numbers")
    ap.add_argument("--gpu-dir", default=GPU_DIR)
    ap.add_argument("--ref-dir", default=REF_DIR)
    ap.add_argument("--scenes")
    ap.add_argument("--report")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    try:
        if a.self_test:
            return self_test(a.ref_dir)
        if not a.numbers:
            ap.error("--numbers 필요")
        judge = run(load_numbers(a.numbers), a.ref_dir, a.gpu_dir, a.scenes.split(",") if a.scenes else None)
    except (base.DumpError, OSError, ValueError) as err:
        print(f"오류(종료 코드 2): {err}")
        return 2
    code = exit_code(judge)
    print_table(judge, a.verbose)
    if a.report:
        write_report(judge, a.report, code)
    return code


if __name__ == "__main__":
    sys.exit(main())
