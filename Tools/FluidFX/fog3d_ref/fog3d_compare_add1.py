"""
실행 환경: Python 3.12 + numpy 2.2 (추가 설치 없음), 저장소 루트에서 실행. fog3d_compare.py·fog3d_replay_add1.py 와 같은 폴더(fog3d_compare.py 는 고치지 않고 import 해서 씀)
실행 명령: python Tools/FluidFX/fog3d_ref/fog3d_compare_add1.py [--gpu Saved/FluidFX/Fog3D/gpu_add1] [--ref Saved/FluidFX/Fog3D/ref_add1] [--numbers Docs/VolumeFog3D/measurements/stage1b-ref/prereg-1b-numbers.json] [--out Saved/FluidFX/Fog3D/compare/verdict_add1.json] | --make-numbers [--shake Saved/FluidFX/Fog3D/ref_add1_shake] | --self-test
출력: 덧붙임 장면 세 개의 판정 표(표준 출력)와 판정 JSON. 종료 코드 0 = 모두 통과, 1 = 실패 있음, 2 = 덤프 없음·형식 오류. --make-numbers 는 기준 덤프·흔든 덤프·proof.json 에서 기준 숫자 JSON 을 씀
상태: 1a 반박이 넘긴 보강 장면(이어짐 넷째 단계, 대칭 깬 첫 풀이, 물체 목록 변경)의 덧붙임 판정. 기준 숫자는 등록 전 초안 값. 2026-11-13 Y2 작성
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

ROOT = base.ROOT
F32 = np.float32
DEFAULT_GPU = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "gpu_add1")
DEFAULT_REF = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_add1")
DEFAULT_SHAKE = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "ref_add1_shake")
DEFAULT_OUT = os.path.join(ROOT, "Saved", "FluidFX", "Fog3D", "compare", "verdict_add1.json")
DEFAULT_NUMBERS = os.path.join(ROOT, "Docs", "VolumeFog3D", "measurements", "stage1b-ref", "prereg-1b-numbers.json")
DEFAULT_REPLAYS = os.path.join(HERE, "replays_add1")
BAND_CELLS = base.BAND_CELLS
X_FLOOR, X_SCALE = 0.002, 3.0

SCENE_CONNECT = "add1_connect4_wall45"
SCENE_SKEW = "add1_first_solve_skew"
SCENE_SWAP = "add1_obstacle_swap"
SWAP_DUMPS = ("c00", "c18")
ALL_DUMPS = ((SCENE_CONNECT, "end"), (SCENE_SKEW, "end")) + tuple((SCENE_SWAP, d) for d in SWAP_DUMPS)


def sha16(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()[:16]


def code_fingerprint():
    return {name: sha16(os.path.join(HERE, name)) for name in
            ("fog3d_core.py", "fog3d_replay.py", "fog3d_compare.py", "fog3d_replay_add1.py", "fog3d_compare_add1.py", "tuned_solvers.json")}


def mean_fill(d, mask):
    return float((1.0 + d.fields["g"].astype(np.float64))[mask].mean())


def fluid_mismatch(ref, test):
    return int(np.count_nonzero(base.fluid_of(ref) != base.fluid_of(test)))


# ---------------- 기준 숫자 만들기 ----------------

def make_numbers(ref_dir, shake_dir, replays, out_path):
    src = lambda scene, dump: base.load_ref(ref_dir, scene, dump)  # noqa: E731
    shk = lambda scene, dump: base.load_ref(shake_dir, scene, dump)  # noqa: E731
    fp = code_fingerprint()
    fp_short = hashlib.sha256(json.dumps(fp, sort_keys=True).encode()).hexdigest()[:16]
    numbers = dict(format="fog3d-prereg-1b-add1-numbers-1", status="초안 값: 1b 등록본으로 옮기기 전", code=fp, code_short=fp_short,
                   replays={name: sha16(os.path.join(replays, f"{name}.json")) for name in (SCENE_CONNECT, SCENE_SKEW, SCENE_SWAP)})
    ref = src(SCENE_CONNECT, "end")
    mask = ref.extra["measure_mask"].astype(bool)
    value = mean_fill(ref, mask)
    sens = abs(mean_fill(shk(SCENE_CONNECT, "end"), mask) - value)
    numbers[SCENE_CONNECT] = dict(dump="end", value=value, sensitivity=sens, tolerance=max(X_FLOOR, X_SCALE * sens), cells=int(mask.sum()),
                                  rule="|GPU − 기준값| ≤ 허용, 그리고 재는 칸이 GPU 덤프에서도 모두 유체",
                                  method="재는 칸 = 기준 덤프 measure_mask(기준 모의의 마지막 계산에서 이어짐 넷째 단계가 무게 > 0 인 모서리를 이어 준 유체 칸). 값 = 그 칸들의 1+g 평균. 허용 = max(0.002, 민감도 × 3), 민감도 = 덮어쓴 바람을 계산당 1e-4cm 흔든 실행과의 차")
    r = base.residual_ratio(src(SCENE_SKEW, "end"))
    sens = abs(base.residual_ratio(shk(SCENE_SKEW, "end")) - r)
    numbers[SCENE_SKEW] = dict(dump="end", value=r, sensitivity=sens, tolerance=max(0.02 * r, X_FLOOR), tolerance_rule="± max(기준값의 2%, 0.002)",
                               method="남은 몰림 비 R = rms(압력 뒤 몰림) ÷ rms(압력 앞 몰림), 유체 칸(A8 과 같은 식). 민감도 = 입력 위치 1e-4cm 흔든 실행과의 차(정보)")
    swap = dict(include_band=False, dumps={}, method=base_field_method())
    for dump in SWAP_DUMPS:
        sens = base.field_diff(src(SCENE_SWAP, dump), shk(SCENE_SWAP, dump), BAND_CELLS)
        swap["dumps"][dump] = dict(sensitivity=sens, x=max(X_FLOOR, X_SCALE * max(sens.values())),
                                   x_by_field={k: max(X_FLOOR, X_SCALE * v) for k, v in sens.items()})
    numbers[SCENE_SWAP] = swap
    proof_path = os.path.join(ref_dir, "proof.json")
    if os.path.isfile(proof_path):
        with open(proof_path, encoding="utf-8") as fh:
            numbers["proof"] = json.load(fh)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(numbers, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    return numbers


def base_field_method():
    return ("장 차이 = (두 장의 차)의 제곱합 제곱근 ÷ (기준 장)의 제곱합 제곱근, 기준 덤프의 유체 칸만, 가장자리 띠 8칸 뺌, 바람 u·v·w / 밀린 거리 dx·dy·dz / 채움 g 를 따로."
            " X = max(0.002, 민감도 × 3), 민감도 = 모든 입력 위치를 1e-4cm 흔든 실행과의 장 차이 가운데 큰 것")


# ---------------- 판정 ----------------

def judge(gpu_dir, ref_dir, numbers):
    gsrc = lambda scene, dump: base.load_gpu(gpu_dir, scene, dump)  # noqa: E731
    rsrc = lambda scene, dump: base.load_ref(ref_dir, scene, dump)  # noqa: E731
    t = base.Table()
    for scene, dump in ALL_DUMPS:
        bad = fluid_mismatch(rsrc(scene, dump), gsrc(scene, dump))
        t.add("ADD", scene, f"유체 표시 = 기준 ({dump})", 0, bad, "= 0 칸", bad == 0)
    spec = numbers[SCENE_CONNECT]
    ref, gpu = rsrc(SCENE_CONNECT, "end"), gsrc(SCENE_CONNECT, "end")
    mask = ref.extra["measure_mask"].astype(bool)
    value = mean_fill(gpu, mask)
    t.add("ADD-a", SCENE_CONNECT, "넷째 단계 칸 1+g 평균", spec["value"], value, f"± {spec['tolerance']:.4g}", abs(value - spec["value"]) <= spec["tolerance"],
          f"칸 {int(mask.sum())}개")
    spec = numbers[SCENE_SKEW]
    r = base.residual_ratio(gsrc(SCENE_SKEW, "end"))
    t.add("ADD-b", SCENE_SKEW, "남은 몰림 비 R", spec["value"], r, f"± {spec['tolerance']:.4g}", abs(r - spec["value"]) <= spec["tolerance"])
    for dump, spec in numbers[SCENE_SWAP]["dumps"].items():
        got = base.field_diff(rsrc(SCENE_SWAP, dump), gsrc(SCENE_SWAP, dump), BAND_CELLS)
        for label, value in got.items():
            t.add("ADD-c", SCENE_SWAP, f"{label} 장 차이 ({dump})", "", value, f"≤ X {spec['x']:.4g}", value <= spec["x"])
    return t


def run_compare(a):
    with open(a.numbers, encoding="utf-8") as fh:
        numbers = json.load(fh)
    try:
        table = judge(a.gpu, a.ref, numbers)
    except base.DumpError as err:
        print(f"오류(종료 코드 2): {err}")
        return 2
    except (KeyError, ValueError) as err:
        print(f"오류(종료 코드 2): 형식 오류 {type(err).__name__}: {err}")
        return 2
    table.print()
    failed = [r for r in table.rows if not r["passed"]]
    verdict = dict(format="fog3d-verdict-add1-1", passed=not failed, checks=len(table.rows), failed=len(failed), numbers_code=numbers.get("code_short"), rows=table.rows)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(verdict, fh, ensure_ascii=False, indent=1, default=lambda o: bool(o) if isinstance(o, np.bool_) else float(o))
        fh.write("\n")
    print(f"판정: {'모두 통과' if not failed else f'실패 {len(failed)}개'} (검사 {len(table.rows)}개) -> {a.out}")
    return 1 if failed else 0


# ---------------- 자체 확인 ----------------

def self_test(a):
    results = []
    with open(a.numbers, encoding="utf-8") as fh:
        numbers = json.load(fh)
    with tempfile.TemporaryDirectory() as tmp:
        gpu = os.path.join(tmp, "gpu")

        def fresh():
            shutil.rmtree(gpu, ignore_errors=True)
            for scene, dump in ALL_DUMPS:
                base.write_gpu_format(gpu, scene, dump, base.load_ref(a.ref, scene, dump))

        def check(text, expect, name):
            args = argparse.Namespace(gpu=gpu, ref=a.ref, numbers=a.numbers, out=os.path.join(tmp, f"{name}.json"))
            code = run_compare(args)
            results.append((text, code == expect, code))

        fresh()
        check("기준 덤프를 GPU 형식으로 바꿔 넣으면 통과(종료 코드 0)", 0, "v0")
        d = base.load_gpu(gpu, SCENE_CONNECT, "end")
        mask = base.load_ref(a.ref, SCENE_CONNECT, "end").extra["measure_mask"].astype(bool)
        d.fields["g"] = np.where(mask, F32(0), d.fields["g"]).astype(F32)
        base.write_gpu_format(gpu, SCENE_CONNECT, "end", d)
        check("가: 넷째 단계 칸을 넷째 단계 없는 값(g = 0)으로 바꾸면 실패(1)", 1, "v1")
        fresh()
        d = base.load_gpu(gpu, SCENE_SKEW, "end")
        d.fields["u"] = (d.fields["u"] * F32(1.1)).astype(F32)
        base.write_gpu_format(gpu, SCENE_SKEW, "end", d)
        check("나: 바람 u 를 10% 흔들면 실패(1)", 1, "v2")
        fresh()
        d = base.load_gpu(gpu, SCENE_SWAP, "c00")
        d.fields["u"] = (d.fields["u"] * F32(1.01)).astype(F32)
        base.write_gpu_format(gpu, SCENE_SWAP, "c00", d)
        check("다: 변경 직후 바람 u 를 1% 흔들면 실패(1)", 1, "v3")
        fresh()
        d = base.load_gpu(gpu, SCENE_SWAP, "c18")
        f = d.fields["fluid"].copy()
        f[60:70, 60:70, 0] = 1.0
        d.fields["fluid"] = f
        base.write_gpu_format(gpu, SCENE_SWAP, "c18", d)
        check("다: 0.3초 뒤 덤프의 유체 표시를 기준과 다르게(새 벽 자리 바닥 층을 유체로) 하면 실패(1)", 1, "v4")
        fresh()
        os.remove(os.path.join(gpu, f"{SCENE_SWAP}__c18.f32"))
        check("파일이 없으면 종료 코드 2", 2, "v5")
        with open(os.path.join(gpu, f"{SCENE_SWAP}__c18.f32"), "wb") as fh:
            fh.write(b"\0\0\0\0")
        check("크기가 틀린 파일도 종료 코드 2", 2, "v6")
    print("--- 자체 확인 ---")
    for text, ok, code in results:
        print(f"{'통과' if ok else '실패'}: {text} (종료 코드 {code})")
    return 0 if all(ok for _, ok, _ in results) else 1


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--gpu", default=DEFAULT_GPU)
    ap.add_argument("--ref", default=DEFAULT_REF)
    ap.add_argument("--shake", default=DEFAULT_SHAKE)
    ap.add_argument("--numbers", default=DEFAULT_NUMBERS)
    ap.add_argument("--replays", default=DEFAULT_REPLAYS)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--make-numbers", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.make_numbers:
        try:
            make_numbers(a.ref, a.shake, a.replays, a.numbers)
        except base.DumpError as err:
            print(f"오류(종료 코드 2): {err}")
            return 2
        print(f"기준 숫자 JSON 을 썼다: {a.numbers}")
        return 0
    if a.self_test:
        return self_test(a)
    return run_compare(a)


if __name__ == "__main__":
    sys.exit(main())
