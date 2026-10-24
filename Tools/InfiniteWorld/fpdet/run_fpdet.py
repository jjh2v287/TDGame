"""부동소수점 결정론·C++ 생성 속도 실험 구동기: chunkgen_port.cpp 를 MSVC 변형(/fp·/arch, UE 5.8 기본 플래그 조합 포함)으로 빌드하고 덤프·시간을 재서 chunkgen.py(numpy float64) 와 비교한다.
실행 환경: Windows 10 + Python 3.12 + numpy 2.x + Visual Studio 2022 Enterprise(vcvars64.bat), UE 빌드·에디터 불필요
실행: python Tools/InfiniteWorld/fpdet/run_fpdet.py --work <임시 폴더> [--out <결과 폴더>] [--skip-build] [--skip-timing] | --rounds N(빌드된 변형을 교차 순서로 N회 단일 스레드 재측정만)
출력: <out>/build_log.txt, asm_stats.json, compare.json, timing.json, python_timing.json, environment.json, tables.md, (--rounds) timing_rounds.json·timing_rounds.md (기본 out=Saved/AgentOps/20261023/infinite-world/fpdet, 덤프는 --work)
상태: 실험 재현용(2026-10-23 무한 샌드박스 세계 갈래 02)
"""
import argparse
import csv
import hashlib
import io
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[2]
sys.path.insert(0, str(SCRIPT_DIR.parent / "proto"))
import chunkgen as cg  # noqa: E402

WORLD_SEED = 20261023
def find_vcvars():
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    found = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], capture_output=True, text=True)
    return Path(found.stdout.strip()) / "VC/Auxiliary/Build/vcvars64.bat"


VCVARS = find_vcvars()
SOURCE = SCRIPT_DIR / "chunkgen_port.cpp"
UE_COMMON_FLAGS = ["/Ox", "/Ot", "/Oi", "/GF", "/Gw", "/Gy", "/GS-", "/permissive-", "/Zc:inline"]
VARIANTS = {
    "precise_sse2": ["/O2", "/fp:precise"],
    "fast_sse2": ["/O2", "/fp:fast"],
    "strict_sse2": ["/O2", "/fp:strict"],
    "precise_avx2": ["/O2", "/fp:precise", "/arch:AVX2"],
    "fast_avx2": ["/O2", "/fp:fast", "/arch:AVX2"],
    "strict_avx2": ["/O2", "/fp:strict", "/arch:AVX2"],
    "precise_contract_avx2": ["/O2", "/fp:precise", "/fp:contract", "/arch:AVX2"],
    "ue_game_default": UE_COMMON_FLAGS + ["/fp:fast"],
    "ue_editor_default": UE_COMMON_FLAGS + ["/fp:precise"],
    "ue_game_avx": UE_COMMON_FLAGS + ["/fp:fast", "/arch:AVX"],
    "ue_game_avx2": UE_COMMON_FLAGS + ["/fp:fast", "/arch:AVX2"],
}
THREADED_VARIANTS = {"ue_game_default", "ue_editor_default", "strict_sse2", "ue_game_avx2"}
CHUNKS = [(0, 0), (-1, 0), (10000000, -3000000), (1 << 40, 5)]
STEPS = [2, 1]
WIDE_RANGE = range(-4, 4)
ASM_FUNCTION_TOKENS = ("GenerateChunk", "EvaluateFbm", "EvaluateWarpedFbm", "GradientNoise", "EvaluateBaseHeight", "ClassifyClimate", "CornerDot", "Fade")
ASM_PATTERNS = {
    "fma": re.compile(r"\bvfn?m(add|sub)\w*"),
    "div": re.compile(r"\bv?divsd\b|\bv?divpd\b"),
    "mul": re.compile(r"\bv?mulsd\b|\bv?mulpd\b"),
    "rcp_or_rsqrt": re.compile(r"\bv?rcp|\bv?rsqrt"),
    "roundsd": re.compile(r"\bv?roundsd\b"),
}
BUSY_PROCESS_NAMES = {"unrealeditor.exe", "cl.exe", "link.exe", "unrealbuildtool.exe", "shadercompileworker.exe", "unrealeditor-cmd.exe"}


def run_batch(lines, work_dir, name):
    batch_path = work_dir / f"{name}.bat"
    batch_path.write_text("@echo off\r\n" + "\r\n".join(lines) + "\r\n", encoding="ascii")
    completed = subprocess.run(["cmd", "/d", "/c", str(batch_path)], capture_output=True, text=True, errors="replace")
    return completed.returncode, completed.stdout + completed.stderr


def build_variants(work_dir, out_dir):
    log_parts = []
    code, banner = run_batch([f'call "{VCVARS}" >nul', "cl 2>&1"], work_dir, "cl_banner")
    log_parts.append("== cl banner ==\n" + banner.strip())
    for name, flags in VARIANTS.items():
        variant_dir = work_dir / name
        variant_dir.mkdir(parents=True, exist_ok=True)
        flag_text = " ".join(flags)
        command = (f'cl /nologo /std:c++17 /EHsc {flag_text} /FAs /Fa"{variant_dir / "port.asm"}" '
                   f'/Fo"{variant_dir / "port.obj"}" /Fe"{variant_dir / "chunkgen_port.exe"}" "{SOURCE}"')
        code, output = run_batch([f'call "{VCVARS}" >nul', command], work_dir, f"build_{name}")
        log_parts.append(f"== {name} (exit {code}) ==\n{command}\n{output.strip()}")
        if code != 0:
            (out_dir / "build_log.txt").write_text("\n\n".join(log_parts), encoding="utf-8")
            raise SystemExit(f"build failed: {name}")
    (out_dir / "build_log.txt").write_text("\n\n".join(log_parts), encoding="utf-8")


def collect_asm_stats(work_dir):
    stats = {}
    for name in VARIANTS:
        text = (work_dir / name / "port.asm").read_text(encoding="latin-1")
        counts = {key: 0 for key in ASM_PATTERNS}
        procedures = []
        inside = False
        for line in text.splitlines():
            procedure_match = re.match(r"^(\S+)\s+PROC", line)
            if procedure_match:
                inside = any(token in procedure_match.group(1) for token in ASM_FUNCTION_TOKENS)
                if inside:
                    procedures.append(procedure_match.group(1))
                continue
            if re.match(r"^\S+\s+ENDP", line):
                inside = False
                continue
            if not inside or line.lstrip().startswith(";"):
                continue
            for key, pattern in ASM_PATTERNS.items():
                counts[key] += len(pattern.findall(line))
        stats[name] = {"counts": counts, "procedures": procedures}
    return stats


def run_dumps(work_dir):
    for name in VARIANTS:
        dump_dir = work_dir / name / "dump"
        dump_dir.mkdir(parents=True, exist_ok=True)
        completed = subprocess.run([str(work_dir / name / "chunkgen_port.exe"), "dump", str(dump_dir)], capture_output=True, text=True)
        if completed.returncode != 0:
            raise SystemExit(f"dump failed: {name}: {completed.stderr}")


def chunk_file_name(chunk_x, chunk_y, step_units):
    return f"c_{chunk_x}_{chunk_y}_64m_{step_units}u.bin"


def axis_sample_count(step_units):
    return 64 * cg.UNITS_PER_M // step_units + 1


def load_chunk(path, step_units):
    count = axis_sample_count(step_units) ** 2
    raw = np.fromfile(path, dtype=np.uint8)
    return {
        "height": raw[0:count * 8].view(np.float64),
        "temperature": raw[count * 8:count * 16].view(np.float64),
        "humidity": raw[count * 16:count * 24].view(np.float64),
        "biome": raw[count * 24:count * 25],
    }


def load_wide(path):
    count = len(WIDE_RANGE) ** 2 * axis_sample_count(1) ** 2
    raw = np.fromfile(path, dtype=np.uint8)
    return {"height": raw[0:count * 8].view(np.float64), "biome": raw[count * 8:count * 9]}


def python_chunk(chunk_x, chunk_y, step_units):
    spacing_m = step_units / cg.UNITS_PER_M
    result = cg.generate_chunk(WORLD_SEED, chunk_x, chunk_y, 64, spacing_m, with_biome=True, with_structures=False)
    position_x, position_y = np.meshgrid(cg.chunk_axis_units(chunk_x, 64, spacing_m), cg.chunk_axis_units(chunk_y, 64, spacing_m))
    temperature = cg.TEMPERATURE_MEAN_C + cg.TEMPERATURE_RANGE_C * cg.fbm(
        cg.derive_seed(WORLD_SEED, cg.TAG_TEMPERATURE), position_x, position_y, cg.CLIMATE_CELL_UNITS, cg.CLIMATE_OCTAVES)
    temperature = temperature - cg.TEMPERATURE_LAPSE_C_PER_M * np.maximum(result["height"], 0.0)
    humidity = np.clip(0.5 + cg.HUMIDITY_RANGE * cg.fbm(
        cg.derive_seed(WORLD_SEED, cg.TAG_HUMIDITY), position_x, position_y, cg.CLIMATE_CELL_UNITS, cg.CLIMATE_OCTAVES), 0.0, 1.0)
    return {"height": result["height"].ravel(), "temperature": temperature.ravel(), "humidity": humidity.ravel(), "biome": result["biome"].ravel()}


def python_wide():
    heights, biomes = [], []
    for chunk_y in WIDE_RANGE:
        for chunk_x in WIDE_RANGE:
            result = cg.generate_chunk(WORLD_SEED, chunk_x, chunk_y, 64, 0.5, with_biome=True, with_structures=False)
            heights.append(result["height"].ravel())
            biomes.append(result["biome"].ravel())
    return {"height": np.concatenate(heights), "biome": np.concatenate(biomes)}


def short_digest(array):
    return hashlib.sha256(np.ascontiguousarray(array).tobytes()).hexdigest()[:16]


def compare_float(candidate, reference):
    candidate_bits = candidate.view(np.int64)
    reference_bits = reference.view(np.int64)
    differing = candidate_bits != reference_bits
    absolute = np.abs(candidate - reference)
    report = {
        "bit_identical": bool(not differing.any()),
        "differing_samples": int(differing.sum()),
        "differing_ratio": float(differing.mean()),
        "max_abs_diff": float(absolute.max()),
        "max_ulp": int(np.abs(candidate_bits - reference_bits).max()),
    }
    for label, scale in (("1mm", 1000.0), ("1cm", 100.0)):
        report[f"quantized_{label}_differing"] = int((np.rint(candidate * scale) != np.rint(reference * scale)).sum())
        report[f"expected_flips_{label}"] = float(absolute.sum() * scale)
    return report


def compare_sets(candidate, reference):
    report = {}
    for key in candidate:
        if key == "biome":
            report["biome_differing"] = int((candidate[key] != reference[key]).sum())
            continue
        report[key] = compare_float(candidate[key], reference[key])
    report["height_sha256_16"] = short_digest(candidate["height"])
    return report


def run_comparison(work_dir):
    references = {}
    for chunk_x, chunk_y in CHUNKS:
        for step_units in STEPS:
            references[(chunk_x, chunk_y, step_units)] = python_chunk(chunk_x, chunk_y, step_units)
    wide_reference = python_wide()
    comparison = {"python_reference": {}, "variants": {}}
    for key, data in references.items():
        comparison["python_reference"][f"{key[0]},{key[1]}@{key[2]}u"] = {
            "height_sha256_16": short_digest(data["height"]), "samples": int(data["height"].size),
            "height_min": float(data["height"].min()), "height_max": float(data["height"].max())}
    comparison["python_reference"]["wide"] = {"height_sha256_16": short_digest(wide_reference["height"]), "samples": int(wide_reference["height"].size)}
    loaded = {}
    for name in VARIANTS:
        dump_dir = work_dir / name / "dump"
        loaded[name] = {key: load_chunk(dump_dir / chunk_file_name(*key), key[2]) for key in references}
        loaded[name]["wide"] = load_wide(dump_dir / "wide_8x8_64m_1u.bin")
    for name in VARIANTS:
        variant_report = {"vs_python": {}, "vs_precise_sse2": {}}
        for key in references:
            label = f"{key[0]},{key[1]}@{key[2]}u"
            variant_report["vs_python"][label] = compare_sets(loaded[name][key], references[key])
            variant_report["vs_precise_sse2"][label] = compare_sets(loaded[name][key], loaded["precise_sse2"][key])
        variant_report["vs_python"]["wide"] = compare_sets(loaded[name]["wide"], wide_reference)
        variant_report["vs_precise_sse2"]["wide"] = compare_sets(loaded[name]["wide"], loaded["precise_sse2"]["wide"])
        comparison["variants"][name] = variant_report
    return comparison


def measure_cpu_load():
    completed = subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_Processor).LoadPercentage"],
                               capture_output=True, text=True)
    return completed.stdout.strip()


def list_busy_processes():
    completed = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True, errors="replace")
    counts = {}
    for row in csv.reader(io.StringIO(completed.stdout)):
        if row and row[0].lower() in BUSY_PROCESS_NAMES:
            counts[row[0]] = counts.get(row[0], 0) + 1
    return counts


def run_timing(work_dir):
    timing = {}
    for name in VARIANTS:
        output_path = work_dir / name / "timing.json"
        arguments = [str(work_dir / name / "chunkgen_port.exe"), "time", str(output_path)]
        if name in THREADED_VARIANTS:
            arguments.append("threads")
        load_before = measure_cpu_load()
        busy_before = list_busy_processes()
        started = time.perf_counter()
        completed = subprocess.run(arguments, capture_output=True, text=True)
        elapsed = time.perf_counter() - started
        if completed.returncode != 0:
            raise SystemExit(f"timing failed: {name}: {completed.stderr}")
        result = json.loads(output_path.read_text(encoding="utf-8"))
        result["cpu_load_before_percent"] = load_before
        result["cpu_load_after_percent"] = measure_cpu_load()
        result["busy_processes_before"] = busy_before
        result["run_seconds"] = round(elapsed, 2)
        timing[name] = result
    return timing


def run_interleaved_timing(work_dir, rounds, out_dir):
    names = ["precise_sse2", "fast_sse2", "strict_sse2", "ue_game_default", "ue_editor_default", "ue_game_avx2", "precise_avx2"]
    medians = {name: {} for name in names}
    loads = []
    for round_index in range(rounds):
        loads.append({"round": round_index, "cpu_load_percent": measure_cpu_load(), "busy": list_busy_processes()})
        order = names if round_index % 2 == 0 else list(reversed(names))
        for name in order:
            output_path = work_dir / name / f"timing_round{round_index}.json"
            completed = subprocess.run([str(work_dir / name / "chunkgen_port.exe"), "time", str(output_path)], capture_output=True, text=True)
            if completed.returncode != 0:
                raise SystemExit(f"timing failed: {name}")
            for row in json.loads(output_path.read_text(encoding="utf-8"))["single"]:
                medians[name].setdefault(f"{row['config']}|{row['mode']}", []).append(row["median_ms"])
    summary = {name: {key: {"min_of_medians_ms": min(values), "median_of_medians_ms": statistics.median(values), "rounds": values}
                      for key, values in entries.items()} for name, entries in medians.items()}
    (out_dir / "timing_rounds.json").write_text(json.dumps({"loads": loads, "summary": summary}, indent=1), encoding="utf-8")
    lines = ["| 변형 | " + " | ".join(next(iter(medians.values())).keys()) + " |", "|---" * (1 + len(next(iter(medians.values())))) + "|"]
    for name, entries in summary.items():
        lines.append(f"| {name} | " + " | ".join(f"{entry['min_of_medians_ms']:.3f} / {entry['median_of_medians_ms']:.3f}" for entry in entries.values()) + " |")
    (out_dir / "timing_rounds.md").write_text("최소 중앙값 / 중앙값의 중앙값 (ms, 교차 순서 반복)\n\n" + "\n".join(lines) + "\n", encoding="utf-8")


def run_python_timing():
    configurations = [("32m@1m", 32, 1.0), ("64m@1m", 64, 1.0), ("64m@0.5m", 64, 0.5), ("128m@1m", 128, 1.0)]
    rows = []
    for with_biome, with_structures, mode in ((False, False, "height"), (True, False, "height+climate"), (True, True, "height+climate+structures")):
        for label, chunk_m, spacing_m in configurations:
            cg.generate_chunk(WORLD_SEED, -50, 9, chunk_m, spacing_m, with_biome, with_structures)
            samples = []
            for index in range(15):
                started = time.perf_counter()
                cg.generate_chunk(WORLD_SEED, index - 7, 3, chunk_m, spacing_m, with_biome, with_structures)
                samples.append((time.perf_counter() - started) * 1000.0)
            rows.append({"config": label, "mode": mode, "median_ms": round(statistics.median(samples), 3)})
    return rows


def format_tables(asm_stats, comparison, timing, python_timing):
    lines = ["# fpdet 원자료 표 (run_fpdet.py 자동 생성)", "", "## 어셈블리 명령 수(생성 함수 범위)", "",
             "| 변형 | FMA | 나눗셈 | 곱셈 | 역수·역제곱근 근사 | roundsd |", "|---|---|---|---|---|---|"]
    for name, entry in asm_stats.items():
        counts = entry["counts"]
        lines.append(f"| {name} | {counts['fma']} | {counts['div']} | {counts['mul']} | {counts['rcp_or_rsqrt']} | {counts['roundsd']} |")
    lines += ["", "## Python(float64) 대비 높이 비교", "",
              "| 변형 | 표본 묶음 | 비트 동일 | 다른 표본 | 비율 | 최대 절대 차이(m) | 최대 ULP | 1mm 양자화 후 다름 | 1cm 후 다름 | 바이옴 다름 | 높이 해시 |",
              "|---|---|---|---|---|---|---|---|---|---|---|"]
    for name, entry in comparison["variants"].items():
        for label, report in entry["vs_python"].items():
            height = report["height"]
            lines.append(f"| {name} | {label} | {'예' if height['bit_identical'] else '아니오'} | {height['differing_samples']} | "
                         f"{height['differing_ratio']:.4f} | {height['max_abs_diff']:.3e} | {height['max_ulp']} | "
                         f"{height['quantized_1mm_differing']} | {height['quantized_1cm_differing']} | {report['biome_differing']} | {report['height_sha256_16']} |")
    lines += ["", "## C++ 단일 스레드 청크당 시간(중앙값 ms)", "", "| 변형 | 모드 | 32m@1m | 64m@1m | 64m@0.5m | 128m@1m | 64m@1m ns/표본 | 부하 전/후 % |",
              "|---|---|---|---|---|---|---|---|"]
    for name, result in timing.items():
        for mode in ("height", "height+climate"):
            entries = {row["config"]: row for row in result["single"] if row["mode"] == mode}
            lines.append(f"| {name} | {mode} | {entries['32m@1m']['median_ms']:.3f} | {entries['64m@1m']['median_ms']:.3f} | "
                         f"{entries['64m@0.5m']['median_ms']:.3f} | {entries['128m@1m']['median_ms']:.3f} | {entries['64m@1m']['ns_per_sample']} | "
                         f"{result['cpu_load_before_percent']}/{result['cpu_load_after_percent']} |")
    lines += ["", "## 다중 스레드 처리량(청크/초, height+climate)", "", "| 변형 | 설정 | 1 | 2 | 4 | 8 | 16 |", "|---|---|---|---|---|---|---|"]
    for name, result in timing.items():
        if not result["threads"]:
            continue
        for config in ("64m@1m", "64m@0.5m"):
            rates = {row["threads"]: row["chunks_per_s"] for row in result["threads"] if row["config"] == config}
            lines.append(f"| {name} | {config} | " + " | ".join(f"{rates[count]:.0f}" for count in (1, 2, 4, 8, 16)) + " |")
    lines += ["", "## Python numpy 청크당 시간(중앙값 ms, 15회)", "", "| 모드 | 설정 | 중앙값 |", "|---|---|---|"]
    for row in python_timing:
        lines.append(f"| {row['mode']} | {row['config']} | {row['median_ms']} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--work", required=True)
    parser.add_argument("--out", default=str(PROJECT_ROOT / "Saved/AgentOps/20261023/infinite-world/fpdet"))
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-timing", action="store_true")
    parser.add_argument("--rounds", type=int, default=0)
    arguments = parser.parse_args()
    work_dir = Path(arguments.work)
    out_dir = Path(arguments.out)
    work_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    if arguments.rounds > 0:
        run_interleaved_timing(work_dir, arguments.rounds, out_dir)
        print("rounds done", out_dir)
        return
    if not arguments.skip_build:
        build_variants(work_dir, out_dir)
    asm_stats = collect_asm_stats(work_dir)
    (out_dir / "asm_stats.json").write_text(json.dumps(asm_stats, indent=1), encoding="utf-8")
    run_dumps(work_dir)
    comparison = run_comparison(work_dir)
    (out_dir / "compare.json").write_text(json.dumps(comparison, indent=1), encoding="utf-8")
    timing = {} if arguments.skip_timing else run_timing(work_dir)
    if timing:
        (out_dir / "timing.json").write_text(json.dumps(timing, indent=1), encoding="utf-8")
    python_timing = run_python_timing()
    (out_dir / "python_timing.json").write_text(json.dumps(python_timing, indent=1), encoding="utf-8")
    environment = {"numpy": np.__version__, "python": sys.version, "variants": VARIANTS, "world_seed": WORLD_SEED,
                   "chunks": [list(map(int, chunk)) for chunk in CHUNKS], "wide": "8x8 chunks (-4..3) at 64m@0.5m"}
    (out_dir / "environment.json").write_text(json.dumps(environment, indent=1), encoding="utf-8")
    if timing:
        (out_dir / "tables.md").write_text(format_tables(asm_stats, comparison, timing, python_timing), encoding="utf-8")
    print("done", out_dir)


if __name__ == "__main__":
    main()
