"""무한 세계 청크 생성 프로토타입 실측: 청크 생성 시간, 이음매 비트 일치, 생성 순서 무관(행·무작위·프로세스 분리)과 음성 대조, 바이옴 비율·원점/청크 색인 1e7 미리보기, 지역 격자 구조물 4청크 일치, float32 정밀도 시연·ULP 표, 탑다운 카메라 시야 기준 생성 처리량. 실행 환경: Python 3.12 + numpy 2.x + matplotlib + Pillow, Windows PowerShell, numpy 단일 스레드.
실행: python Tools/InfiniteWorld/proto/run_proto.py [--out Saved/AgentOps/20261023/infinite-world/proto] [--repeats 31] [--quick]
출력: <out>/{results.json, timing.csv, preview_origin_vs_far.png, biome_hist.png, structure_4chunks.png, precision_float32_vs_int.png, walk_jitter.png}, 표준 출력에 요약
상태: 실험 재현용(2026-10-23 무한 샌드박스 세계 가능성 검토 갈래 01)
"""
import argparse
import hashlib
import json
import math
import multiprocessing
import os
import sys
import time

os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import numpy as np

import chunkgen as cg

WORLD_SEED = 20261023
FAR_CHUNK_INDEX = 10 ** 7
REFERENCE_CHUNK_M = 64
CHUNK_CONFIGS = [(32, 1.0), (64, 1.0), (128, 1.0), (64, 0.5)]
SEAM_BASES = [(0, 0), (-1, -1), (FAR_CHUNK_INDEX, FAR_CHUNK_INDEX), (-FAR_CHUNK_INDEX, 12345), (2 ** 40, -(2 ** 40))]
WALK_SPEED_CM_PER_S = 600.0


def far_origin_units(chunk_m=REFERENCE_CHUNK_M):
    return FAR_CHUNK_INDEX * chunk_m * cg.UNITS_PER_M


def time_call(function, repeats, warmup=3):
    for _ in range(warmup):
        function()
    samples = []
    for _ in range(repeats):
        start = time.perf_counter()
        function()
        samples.append((time.perf_counter() - start) * 1000.0)
    samples = np.array(samples)
    return {"median_ms": float(np.median(samples)), "p10_ms": float(np.percentile(samples, 10)),
            "p90_ms": float(np.percentile(samples, 90)), "repeats": repeats}


def measure_timing(repeats):
    rows = []
    for chunk_m, spacing_m in CHUNK_CONFIGS:
        samples_per_side = int(round(chunk_m / spacing_m)) + 1
        for label, chunk_index in (("origin", 0), ("far_1e7", FAR_CHUNK_INDEX)):
            for mode, with_biome, with_structures in (("height", False, False), ("full", True, True)):
                stats = time_call(lambda: cg.generate_chunk(WORLD_SEED, chunk_index, chunk_index, chunk_m, spacing_m,
                                                            with_biome, with_structures), repeats)
                probe = cg.generate_chunk(WORLD_SEED, chunk_index, chunk_index, chunk_m, spacing_m, with_biome, with_structures)
                stats.update({"chunk_m": chunk_m, "spacing_m": spacing_m, "location": label, "mode": mode,
                              "samples": samples_per_side ** 2,
                              "ns_per_sample": stats["median_ms"] * 1e6 / samples_per_side ** 2,
                              "structures_touched": probe.get("structures_touched", 0),
                              "output_bytes": int(sum(np.asarray(v).nbytes for k, v in probe.items() if k != "structures_touched"))})
                rows.append(stats)
                print(f"timing {chunk_m}m@{spacing_m}m {label:8s} {mode:6s} median {stats['median_ms']:.2f} ms "
                      f"p90 {stats['p90_ms']:.2f} ({stats['ns_per_sample']:.0f} ns/sample, structures {stats['structures_touched']})")
    structure_rows = []
    for chunk_m, spacing_m in CHUNK_CONFIGS:
        target = find_chunk_with_structure(chunk_m, spacing_m)
        stats = time_call(lambda: cg.generate_chunk(WORLD_SEED, target[0], target[1], chunk_m, spacing_m), repeats)
        probe = cg.generate_chunk(WORLD_SEED, target[0], target[1], chunk_m, spacing_m)
        stats.update({"chunk_m": chunk_m, "spacing_m": spacing_m, "chunk": list(target),
                      "structures_touched": probe["structures_touched"]})
        structure_rows.append(stats)
        print(f"timing with structure {chunk_m}m@{spacing_m}m chunk {target} median {stats['median_ms']:.2f} ms "
              f"structures {probe['structures_touched']}")
    return rows, structure_rows


def find_chunk_with_structure(chunk_m, spacing_m):
    for region_index in range(64):
        structure = cg.describe_region_structure(WORLD_SEED, region_index, 0)
        if structure is None:
            continue
        chunk_units = int(round(chunk_m * cg.UNITS_PER_M))
        return structure["center"][0] // chunk_units, structure["center"][1] // chunk_units
    raise RuntimeError("no structure found")


def bit_equal(first, second):
    first = np.ascontiguousarray(first)
    second = np.ascontiguousarray(second)
    if first.dtype == np.float64:
        return bool(np.array_equal(first.view(np.uint64), second.view(np.uint64)))
    return bool(np.array_equal(first, second))


def max_abs_difference(first, second):
    return float(np.max(np.abs(np.asarray(first, dtype=np.float64) - np.asarray(second, dtype=np.float64))))


def compare_edges(chunk_a, chunk_b, chunk_c, chunk_d):
    report = {"edges_bit_equal": True, "max_abs_height_diff": 0.0}
    for key in ("height", "biome", "struct_class", "struct_id"):
        pairs = [(chunk_a[key][:, -1], chunk_b[key][:, 0]), (chunk_a[key][-1, :], chunk_c[key][0, :]),
                 (chunk_b[key][-1, :], chunk_d[key][0, :]), (chunk_c[key][:, -1], chunk_d[key][:, 0])]
        for first, second in pairs:
            report["edges_bit_equal"] &= bit_equal(first, second)
            if key == "height":
                report["max_abs_height_diff"] = max(report["max_abs_height_diff"], max_abs_difference(first, second))
    corners = [chunk_a["height"][-1, -1], chunk_b["height"][-1, 0], chunk_c["height"][0, -1], chunk_d["height"][0, 0]]
    report["corner_bit_equal"] = len({np.float64(value).tobytes() for value in corners}) == 1
    return report


def measure_seams():
    results = []
    for chunk_m, spacing_m in CHUNK_CONFIGS:
        for base_x, base_y in SEAM_BASES:
            chunks = [cg.generate_chunk(WORLD_SEED, base_x + dx, base_y + dy, chunk_m, spacing_m)
                      for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1))]
            report = compare_edges(*chunks)
            report.update({"chunk_m": chunk_m, "spacing_m": spacing_m, "base_chunk": [base_x, base_y],
                           "edge_samples": int(chunks[0]["height"].shape[0])})
            results.append(report)
        for base_x, base_y in ((-1, -1), (FAR_CHUNK_INDEX - 1, FAR_CHUNK_INDEX - 1)):
            samples = int(round(chunk_m / spacing_m))
            step = int(round(spacing_m * cg.UNITS_PER_M))
            xs = base_x * samples * step + np.arange(3 * samples + 1, dtype=np.int64) * step
            ys = base_y * samples * step + np.arange(3 * samples + 1, dtype=np.int64) * step
            whole = cg.generate_area(WORLD_SEED, xs, ys)
            all_equal = True
            worst = 0.0
            for offset_y in range(3):
                for offset_x in range(3):
                    chunk = cg.generate_chunk(WORLD_SEED, base_x + offset_x, base_y + offset_y, chunk_m, spacing_m)
                    rows = slice(offset_y * samples, (offset_y + 1) * samples + 1)
                    columns = slice(offset_x * samples, (offset_x + 1) * samples + 1)
                    for key in ("height", "biome", "struct_class", "struct_id"):
                        all_equal &= bit_equal(whole[key][rows, columns], chunk[key])
                    worst = max(worst, max_abs_difference(whole["height"][rows, columns], chunk["height"]))
            results.append({"chunk_m": chunk_m, "spacing_m": spacing_m, "base_chunk": [base_x, base_y],
                            "test": "one_shot_3x3_vs_individual", "all_bit_equal": all_equal, "max_abs_height_diff": worst})
    for row in results:
        print("seam", row)
    return results


def chunk_digest_worker(task):
    chunk_x, chunk_y, chunk_m, spacing_m = task
    return (chunk_x, chunk_y), cg.chunk_digest(cg.generate_chunk(WORLD_SEED, chunk_x, chunk_y, chunk_m, spacing_m)), os.getpid()


def combine_digests(digest_by_key):
    combined = hashlib.sha256()
    for key in sorted(digest_by_key):
        combined.update(f"{key[0]},{key[1]}:{digest_by_key[key]};".encode())
    return combined.hexdigest()


def upsample_bilinear(grid, samples, cell_samples):
    t = np.arange(samples + 1) / cell_samples
    index = np.minimum(np.floor(t).astype(np.int64), grid.shape[0] - 2)
    frac = t - index
    rows = grid[index, :] * (1.0 - frac)[:, None] + grid[index + 1, :] * frac[:, None]
    return rows[:, index] * (1.0 - frac)[None, :] + rows[:, index + 1] * frac[None, :]


def control_shared_stream(order):
    stream = np.random.default_rng(7)
    chunks = {}
    for key in order:
        chunks[key] = upsample_bilinear(stream.random((5, 5)), 64, 16)
    return chunks


def control_chunk_seeded(order):
    chunks = {}
    for key in order:
        local = np.random.default_rng(cg.derive_seed(7, key[0], key[1]))
        chunks[key] = upsample_bilinear(local.random((5, 5)), 64, 16)
    return chunks


def control_report(build, row_order, shuffled_order):
    row_chunks = build(row_order)
    shuffled_chunks = build(shuffled_order)
    digest = lambda chunks: combine_digests({k: hashlib.sha256(v.tobytes()).hexdigest() for k, v in chunks.items()})
    seam = max(max_abs_difference(row_chunks[(x, y)][:, -1], row_chunks[(x + 1, y)][:, 0])
               for (x, y) in row_order if (x + 1, y) in row_chunks)
    return {"row_digest": digest(row_chunks), "shuffled_digest": digest(shuffled_chunks),
            "order_independent": digest(row_chunks) == digest(shuffled_chunks), "max_seam_abs_diff": seam}


def measure_order_independence():
    chunk_m, spacing_m = 64, 1.0
    results = {}
    shuffle_generator = np.random.default_rng(20261023)
    for label, center in (("origin", (0, 0)), ("far_1e7", (FAR_CHUNK_INDEX, FAR_CHUNK_INDEX))):
        row_order = [(center[0] + x, center[1] + y) for y in range(-4, 5) for x in range(-4, 5)]
        shuffled_order = [row_order[i] for i in shuffle_generator.permutation(len(row_order))]
        row_digests = {key: cg.chunk_digest(cg.generate_chunk(WORLD_SEED, key[0], key[1], chunk_m, spacing_m)) for key in row_order}
        shuffled_digests = {key: cg.chunk_digest(cg.generate_chunk(WORLD_SEED, key[0], key[1], chunk_m, spacing_m)) for key in shuffled_order}
        start = time.perf_counter()
        context = multiprocessing.get_context("spawn")
        with context.Pool(processes=6, maxtasksperchild=1) as pool:
            outputs = pool.map(chunk_digest_worker, [(key[0], key[1], chunk_m, spacing_m) for key in shuffled_order], chunksize=1)
        process_digests = {key: digest for key, digest, _ in outputs}
        process_ids = {pid for _, _, pid in outputs}
        results[label] = {
            "chunks": len(row_order), "chunk_m": chunk_m, "spacing_m": spacing_m,
            "row_order_digest": combine_digests(row_digests),
            "shuffled_order_digest": combine_digests(shuffled_digests),
            "separate_process_digest": combine_digests(process_digests),
            "separate_process_count": len(process_ids),
            "separate_process_seconds": time.perf_counter() - start,
        }
        results[label]["all_equal"] = len({results[label]["row_order_digest"], results[label]["shuffled_order_digest"],
                                           results[label]["separate_process_digest"]}) == 1
        print("order", label, results[label])
    row_order = [(x, y) for y in range(-4, 5) for x in range(-4, 5)]
    shuffled_order = [row_order[i] for i in np.random.default_rng(99).permutation(len(row_order))]
    results["control_shared_sequential_stream"] = control_report(control_shared_stream, row_order, shuffled_order)
    results["control_chunk_seeded_local_lattice"] = control_report(control_chunk_seeded, row_order, shuffled_order)
    print("order controls", results["control_shared_sequential_stream"], results["control_chunk_seeded_local_lattice"])
    return results


def hillshade(height, spacing_m):
    gradient_y, gradient_x = np.gradient(height, spacing_m)
    normal = np.stack([-gradient_x, -gradient_y, np.ones_like(height)])
    normal /= np.linalg.norm(normal, axis=0)
    light = np.array([-1.0, -1.0, 1.4])
    light /= np.linalg.norm(light)
    return 0.35 + 0.65 * np.clip(np.tensordot(light, normal, axes=1), 0.0, 1.0)


def render_biome_image(area, spacing_m):
    color = cg.BIOME_COLORS[area["biome"]] * hillshade(area["height"], spacing_m)[..., None]
    structure_colors = {cg.STRUCTURE_CLASS_PLAZA: (200, 195, 185), cg.STRUCTURE_CLASS_WALL: (60, 50, 45),
                        cg.STRUCTURE_CLASS_BUILDING: (170, 70, 50)}
    for structure_class, rgb in structure_colors.items():
        color[area["struct_class"] == structure_class] = rgb
    return np.clip(color, 0, 255).astype(np.uint8)


def biome_fractions(biome):
    counts = np.bincount(biome.ravel(), minlength=len(cg.BIOME_NAMES))
    return {name: float(count / biome.size) for name, count in zip(cg.BIOME_NAMES, counts)}


def height_statistics(height, spacing_m=None):
    stats = {"mean_m": float(height.mean()), "std_m": float(height.std()), "p5_m": float(np.percentile(height, 5)),
             "p95_m": float(np.percentile(height, 95)), "water_fraction": float((height < 0).mean())}
    if spacing_m is not None:
        gradient_y, gradient_x = np.gradient(height, spacing_m)
        slope = np.degrees(np.arctan(np.hypot(gradient_x, gradient_y)))
        stats.update({"slope_p50_deg": float(np.percentile(slope, 50)), "slope_p95_deg": float(np.percentile(slope, 95))})
    return stats


def measure_biomes(out_dir, quick):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    preview_spacing_m = 8.0 if quick else 4.0
    step = int(preview_spacing_m * cg.UNITS_PER_M)
    half_units = 2000 * cg.UNITS_PER_M
    samples = int(4000 / preview_spacing_m) + 1
    regions = {"origin": (0, 0), "far_1e7": (far_origin_units(), far_origin_units())}
    previews = {}
    report = {"preview_spacing_m": preview_spacing_m, "preview_size_m": 4000, "far_origin_units": far_origin_units(),
              "far_origin_cm": far_origin_units() * 50, "far_chunk_index": FAR_CHUNK_INDEX, "reference_chunk_m": REFERENCE_CHUNK_M}
    for label, (center_x, center_y) in regions.items():
        xs = center_x - half_units + np.arange(samples, dtype=np.int64) * step
        ys = center_y - half_units + np.arange(samples, dtype=np.int64) * step
        start = time.perf_counter()
        area = cg.generate_area(WORLD_SEED, xs, ys)
        elapsed = time.perf_counter() - start
        previews[label] = area
        broad_xs = center_x + (np.arange(256, dtype=np.int64) - 128) * 800
        broad_ys = center_y + (np.arange(256, dtype=np.int64) - 128) * 800
        broad = cg.generate_area(WORLD_SEED, broad_xs, broad_ys, with_structures=False)
        report[label] = {"preview_seconds": elapsed, "preview_biome_fraction": biome_fractions(area["biome"]),
                         "preview_height": height_statistics(area["height"], preview_spacing_m),
                         "preview_structure_sample_fraction": float((area["struct_class"] > 0).mean()),
                         "preview_structures_touched": area["structures_touched"],
                         "broad_102km_biome_fraction": biome_fractions(broad["biome"]),
                         "broad_102km_height": height_statistics(broad["height"])}
        print("biome", label, f"{elapsed:.1f}s", report[label]["preview_height"])
    figure, axes = plt.subplots(1, 2, figsize=(16, 8.4))
    for axis, label in zip(axes, previews):
        axis.imshow(render_biome_image(previews[label], preview_spacing_m), origin="lower",
                    extent=[-2000, 2000, -2000, 2000], interpolation="nearest")
        title = "origin (0,0)" if label == "origin" else f"chunk index 1e7 (64 m chunks) = {far_origin_units() * 50:.3e} cm"
        axis.set_title(f"{title}\n4 km x 4 km, {preview_spacing_m:g} m sampling")
        axis.set_xlabel("local x (m)")
        axis.set_ylabel("local y (m)")
    handles = [plt.Rectangle((0, 0), 1, 1, color=cg.BIOME_COLORS[i] / 255) for i in range(len(cg.BIOME_NAMES))]
    figure.legend(handles, cg.BIOME_NAMES, loc="lower center", ncol=6, fontsize=9)
    figure.tight_layout(rect=(0, 0.06, 1, 1))
    figure.savefig(os.path.join(out_dir, "preview_origin_vs_far.png"), dpi=110)
    plt.close(figure)
    figure, axis = plt.subplots(figsize=(13, 5))
    width = 0.2
    positions = np.arange(len(cg.BIOME_NAMES))
    series = [("origin preview 4 km", report["origin"]["preview_biome_fraction"]),
              ("far 1e7 preview 4 km", report["far_1e7"]["preview_biome_fraction"]),
              ("origin broad 102 km", report["origin"]["broad_102km_biome_fraction"]),
              ("far 1e7 broad 102 km", report["far_1e7"]["broad_102km_biome_fraction"])]
    for index, (name, fractions) in enumerate(series):
        axis.bar(positions + (index - 1.5) * width, [fractions[n] * 100 for n in cg.BIOME_NAMES], width, label=name)
    axis.set_xticks(positions)
    axis.set_xticklabels(cg.BIOME_NAMES, rotation=30, ha="right", fontsize=8)
    axis.set_ylabel("share of samples (%)")
    axis.legend()
    figure.tight_layout()
    figure.savefig(os.path.join(out_dir, "biome_hist.png"), dpi=110)
    plt.close(figure)
    report["lattice_frac_bit_identical_origin_vs_far"] = check_lattice_frac_identity()
    return report


def check_lattice_frac_identity():
    origin_axis = cg.chunk_axis_units(0, 64, 1.0)
    far_axis = cg.chunk_axis_units(FAR_CHUNK_INDEX, 64, 1.0)
    seed = cg.derive_seed(WORLD_SEED, cg.TAG_HEIGHT)
    identical = []
    cell_units = cg.HEIGHT_CELL_UNITS
    for octave in range(cg.HEIGHT_OCTAVES):
        offset_x, _ = cg.octave_offsets(cg.derive_seed(seed, octave))
        _, origin_frac = cg.lattice_coordinate(origin_axis + offset_x, None, cell_units)
        _, far_frac = cg.lattice_coordinate(far_axis + offset_x, None, cell_units)
        identical.append(bit_equal(origin_frac, far_frac))
        cell_units //= 2
    return identical


def chunk_range(center, reach, chunk_units):
    return (center - reach) // chunk_units, (center + reach) // chunk_units


def measure_structures(out_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    chunk_m, spacing_m = 128, 1.0
    chunk_units = chunk_m * cg.UNITS_PER_M
    samples = chunk_m
    results = {"chunk_m": chunk_m, "spacing_m": spacing_m, "cases": []}
    far_region = far_origin_units() // cg.REGION_UNITS
    spans = {32: [], 64: [], 128: []}
    for label, base_region in (("origin", 0), ("far_1e7", far_region)):
        selected = None
        for region_y in range(base_region - 6, base_region + 7):
            for region_x in range(base_region - 6, base_region + 7):
                structure = cg.describe_region_structure(WORLD_SEED, region_x, region_y)
                if structure is None:
                    continue
                reach = cg.structure_reach_units(structure)
                for size in spans:
                    x0, x1 = chunk_range(structure["center"][0], reach, size * cg.UNITS_PER_M)
                    y0, y1 = chunk_range(structure["center"][1], reach, size * cg.UNITS_PER_M)
                    spans[size].append((x1 - x0 + 1) * (y1 - y0 + 1))
                x0, x1 = chunk_range(structure["center"][0], reach, chunk_units)
                y0, y1 = chunk_range(structure["center"][1], reach, chunk_units)
                footprint_x = chunk_range(structure["center"][0], structure["radius"] - 1, chunk_units)
                footprint_y = chunk_range(structure["center"][1], structure["radius"] - 1, chunk_units)
                if selected is None and x1 - x0 == 1 and y1 - y0 == 1 and footprint_x == (x0, x1) and footprint_y == (y0, y1):
                    selected = (structure, x0, y0)
        structure, chunk_x0, chunk_y0 = selected
        individual = {}
        for offset_y in range(2):
            for offset_x in range(2):
                individual[(offset_x, offset_y)] = cg.generate_chunk(WORLD_SEED, chunk_x0 + offset_x, chunk_y0 + offset_y, chunk_m, spacing_m)
        xs = chunk_x0 * chunk_units + np.arange(2 * samples + 1, dtype=np.int64) * 2
        ys = chunk_y0 * chunk_units + np.arange(2 * samples + 1, dtype=np.int64) * 2
        whole = cg.generate_area(WORLD_SEED, xs, ys)
        all_equal = True
        samples_per_chunk = {}
        for (offset_x, offset_y), chunk in individual.items():
            rows = slice(offset_y * samples, (offset_y + 1) * samples + 1)
            columns = slice(offset_x * samples, (offset_x + 1) * samples + 1)
            for key in ("height", "biome", "struct_class", "struct_id"):
                all_equal &= bit_equal(whole[key][rows, columns], chunk[key])
            samples_per_chunk[f"{offset_x},{offset_y}"] = int((chunk["struct_id"] == structure["id"]).sum())
        stitched = np.zeros((2 * samples + 1, 2 * samples + 1, 3), dtype=np.uint8)
        stitched_area = {key: np.zeros_like(whole[key]) for key in ("height", "biome", "struct_class")}
        for (offset_x, offset_y), chunk in individual.items():
            rows = slice(offset_y * samples, (offset_y + 1) * samples + 1)
            columns = slice(offset_x * samples, (offset_x + 1) * samples + 1)
            for key in stitched_area:
                stitched_area[key][rows, columns] = chunk[key]
        stitched = render_biome_image(stitched_area, spacing_m)
        case = {"location": label, "region": list(structure["region"]),
                "center_units": list(structure["center"]), "center_m": [structure["center"][0] / 2, structure["center"][1] / 2],
                "radius_m": structure["radius"] / 2, "influence_radius_m": cg.structure_reach_units(structure) / 2,
                "chunks": [[chunk_x0 + dx, chunk_y0 + dy] for dy in range(2) for dx in range(2)],
                "structure_samples_per_chunk": samples_per_chunk, "individual_vs_one_shot_bit_equal": all_equal}
        case["spans_all_four_chunks"] = all(value > 0 for value in samples_per_chunk.values())
        case["image"] = stitched
        results["cases"].append(case)
        print("structure", {k: v for k, v in case.items() if k != "image"})
    figure, axes = plt.subplots(1, 2, figsize=(14, 7.4))
    for axis, case in zip(axes, results["cases"]):
        axis.imshow(case["image"], origin="lower", extent=[0, 2 * chunk_m, 0, 2 * chunk_m], interpolation="nearest")
        axis.axvline(chunk_m, color="yellow", linewidth=1.5)
        axis.axhline(chunk_m, color="yellow", linewidth=1.5)
        axis.set_title(f"{case['location']}: 4 chunks generated separately, stitched\nradius {case['radius_m']:.0f} m, "
                       f"bit-equal to one-shot: {case['individual_vs_one_shot_bit_equal']}")
        axis.set_xlabel("m (yellow = chunk borders)")
    figure.tight_layout()
    figure.savefig(os.path.join(out_dir, "structure_4chunks.png"), dpi=110)
    plt.close(figure)
    for case in results["cases"]:
        del case["image"]
    results["chunks_per_structure_by_influence"] = {
        str(size): {"min": int(min(values)), "median": float(np.median(values)), "max": int(max(values)), "count": len(values)}
        for size, values in spans.items()}
    return results


def fade_float32(t):
    return t * t * t * (t * (t * np.float32(6.0) - np.float32(15.0)) + np.float32(10.0))


def gradient_noise_float32_world(seed, x_cm, y_cm, cell_m):
    scaled_x = (x_cm * np.float32(0.01)) / np.float32(cell_m)
    scaled_y = (y_cm * np.float32(0.01)) / np.float32(cell_m)
    floor_x = np.floor(scaled_x)
    floor_y = np.floor(scaled_y)
    frac_x = (scaled_x - floor_x).astype(np.float32)
    frac_y = (scaled_y - floor_y).astype(np.float32)
    cell_x = floor_x.astype(np.int64)
    cell_y = floor_y.astype(np.int64)
    gradient_x = cg.GRADIENT_X.astype(np.float32)
    gradient_y = cg.GRADIENT_Y.astype(np.float32)

    def corner(dx, dy, offset_x, offset_y):
        index = (cg.hash_lattice(seed, cell_x + dx, cell_y + dy) >> np.uint64(61)).astype(np.intp)
        return gradient_x[index] * offset_x + gradient_y[index] * offset_y

    one = np.float32(1.0)
    n00 = corner(0, 0, frac_x, frac_y)
    n10 = corner(1, 0, frac_x - one, frac_y)
    n01 = corner(0, 1, frac_x, frac_y - one)
    n11 = corner(1, 1, frac_x - one, frac_y - one)
    blend_x = fade_float32(frac_x)
    blend_y = fade_float32(frac_y)
    bottom = n00 + blend_x * (n10 - n00)
    top = n01 + blend_x * (n11 - n01)
    return (bottom + blend_y * (top - bottom)) * np.float32(cg.NOISE_SCALE)


def gradient_noise_integer_cm(seed, x_cm, y_cm, cell_m):
    cell_cm = int(round(cell_m * 100))
    cell_x = np.floor_divide(x_cm, cell_cm)
    cell_y = np.floor_divide(y_cm, cell_cm)
    frac_x = (x_cm - cell_x * cell_cm).astype(np.float64) / cell_cm
    frac_y = (y_cm - cell_y * cell_cm).astype(np.float64) / cell_cm
    return cg.gradient_noise(seed, cell_x, cell_y, frac_x, frac_y)


def human_duration(seconds):
    if seconds < 120:
        return f"{seconds:.1f} s"
    if seconds < 7200:
        return f"{seconds / 60:.1f} min"
    if seconds < 172800:
        return f"{seconds / 3600:.1f} h"
    if seconds < 3.15e7 * 2:
        return f"{seconds / 86400:.1f} days"
    return f"{seconds / 3.156e7:.1f} years"


def measure_precision(out_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    seed = cg.derive_seed(WORLD_SEED, 99)
    ulp_table = []
    for exponent in range(4, 11):
        distance_cm = 10.0 ** exponent
        walk_seconds = distance_cm / WALK_SPEED_CM_PER_S
        ulp_table.append({"distance_cm": distance_cm, "distance_km": distance_cm / 1e5,
                          "float32_ulp_cm": float(np.spacing(np.float32(distance_cm))),
                          "float64_ulp_cm": float(np.spacing(np.float64(distance_cm))),
                          "walk_seconds_at_600cm_s": walk_seconds, "walk_time": human_duration(walk_seconds)})
    landmarks = {"UE_FLOAT_HUGE_DISTANCE": 1048576.0, "UE_LARGE_HALF_WORLD_MAX": 8796093022208.0 * 0.5,
                 "far_chunk_1e7_x_64m": far_origin_units() * 50.0}
    landmark_rows = [{"name": name, "distance_cm": value, "float32_ulp_cm": float(np.spacing(np.float32(value))),
                      "float64_ulp_cm": float(np.spacing(np.float64(value))),
                      "walk_time": human_duration(value / WALK_SPEED_CM_PER_S)} for name, value in landmarks.items()]
    transects = []
    figure = plt.figure(figsize=(14, 13))
    grid = figure.add_gridspec(4, 3, height_ratios=[1, 1, 1, 1.6])
    offsets = np.arange(0, 401, dtype=np.int64)
    for row, base_cm in enumerate((10 ** 6, 10 ** 8, 10 ** 9)):
        x_int = base_cm + offsets
        y_int = np.full_like(x_int, base_cm + 37)
        exact = gradient_noise_integer_cm(seed, x_int, y_int, 2.0)
        x_f32 = x_int.astype(np.float32)
        y_f32 = y_int.astype(np.float32)
        naive = gradient_noise_float32_world(seed, x_f32, y_f32, 2.0).astype(np.float64)
        steps = np.abs(np.diff(naive))
        transects.append({"base_cm": base_cm, "samples": int(offsets.size), "distinct_float32_positions": int(np.unique(x_f32).size),
                          "distinct_float32_values": int(np.unique(naive).size), "distinct_integer_values": int(np.unique(exact).size),
                          "max_abs_diff_vs_integer": float(np.max(np.abs(naive - exact))),
                          "integer_max_step": float(np.max(np.abs(np.diff(exact)))), "float32_max_step": float(steps.max())})
        axis = figure.add_subplot(grid[row, :])
        axis.plot(offsets, exact, label="int64 cell index + local fraction (float64)", linewidth=1.6)
        axis.step(offsets, naive, where="post", label="float32 absolute world cm", linewidth=1.0)
        axis.set_title(f"1D transect at x = {base_cm:.0e} cm (2 m noise cell, 1 cm steps) - float32 ULP here = "
                       f"{float(np.spacing(np.float32(base_cm))):g} cm")
        axis.set_xlabel("offset (cm)")
        axis.legend(loc="upper right", fontsize=8)
    patch_offsets = np.arange(0, 401, 2, dtype=np.int64)
    base_cm = 10 ** 8
    patch_x, patch_y = np.meshgrid(base_cm + patch_offsets, base_cm + patch_offsets)
    exact_patch = gradient_noise_integer_cm(seed, patch_x, patch_y, 1.0)
    naive_patch = gradient_noise_float32_world(seed, patch_x.astype(np.float32), patch_y.astype(np.float32), 1.0).astype(np.float64)
    for column, (image, title) in enumerate(((exact_patch, "int64 index (exact)"), (naive_patch, "float32 world cm"),
                                             (naive_patch - exact_patch, "difference"))):
        axis = figure.add_subplot(grid[3, column])
        axis.imshow(image, origin="lower", extent=[0, 400, 0, 400], cmap="gray" if column < 2 else "coolwarm", interpolation="nearest")
        axis.set_title(f"{title}\n4 m x 4 m patch at (1e8, 1e8) cm, 1 m cell, 2 cm px")
    figure.tight_layout()
    figure.savefig(os.path.join(out_dir, "precision_float32_vs_int.png"), dpi=100)
    plt.close(figure)
    patch_stats = {"max_abs_diff": float(np.max(np.abs(naive_patch - exact_patch))),
                   "distinct_values_float32": int(np.unique(naive_patch).size), "distinct_values_integer": int(np.unique(exact_patch).size)}
    walk_rows = []
    figure, axis = plt.subplots(figsize=(12, 5))
    for base_cm in (1e5, 1e6, 1e7, 1e8, 1e9):
        for fps in (30, 60, 120):
            step = np.float32(WALK_SPEED_CM_PER_S / fps)
            position = np.float32(base_cm)
            displacements = []
            for _ in range(fps * 2):
                next_position = np.float32(position + step)
                displacements.append(float(next_position) - float(position))
                position = next_position
            displacements = np.array(displacements)
            walk_rows.append({"base_cm": base_cm, "fps": fps, "intended_step_cm": float(step),
                              "effective_speed_cm_s": float(displacements.mean() * fps),
                              "min_step_cm": float(displacements.min()), "max_step_cm": float(displacements.max())})
            if fps == 60:
                axis.plot(displacements, marker=".", label=f"start {base_cm:.0e} cm")
    axis.axhline(WALK_SPEED_CM_PER_S / 60, color="black", linestyle="--", linewidth=0.8, label="intended 10 cm/frame")
    axis.set_title("float32 position accumulation, 600 cm/s at 60 fps: per-frame displacement")
    axis.set_xlabel("frame")
    axis.set_ylabel("cm moved this frame")
    axis.legend(fontsize=8)
    figure.tight_layout()
    figure.savefig(os.path.join(out_dir, "walk_jitter.png"), dpi=110)
    plt.close(figure)
    for row in transects + walk_rows:
        print("precision", row)
    return {"ulp_table": ulp_table, "landmarks": landmark_rows, "transects": transects,
            "patch_1e8": patch_stats, "walk_float32": walk_rows}


def camera_footprint(arm_cm, pitch_deg, yaw_deg, horizontal_fov_deg, aspect):
    pitch = math.radians(pitch_deg)
    forward = np.array([math.cos(pitch), 0.0, math.sin(pitch)])
    right = np.array([0.0, 1.0, 0.0])
    up = np.array([-math.sin(pitch), 0.0, math.cos(pitch)])
    camera = -forward * arm_cm
    half_width = math.tan(math.radians(horizontal_fov_deg) / 2)
    half_height = half_width / aspect
    corners = []
    for sign_y in (-1, 1):
        for sign_x in (-1, 1):
            direction = forward + right * sign_x * half_width + up * sign_y * half_height
            if direction[2] >= 0:
                raise ValueError("view ray does not hit the ground")
            t = -camera[2] / direction[2]
            corners.append((camera + t * direction)[:2])
    corners = np.array(corners)
    yaw = math.radians(yaw_deg)
    rotation = np.array([[math.cos(yaw), -math.sin(yaw)], [math.sin(yaw), math.cos(yaw)]])
    world = corners @ rotation.T
    return {"arm_cm": arm_cm, "pitch_deg": pitch_deg, "yaw_deg": yaw_deg, "fov_deg": horizontal_fov_deg, "aspect": aspect,
            "camera_height_cm": float(camera[2]), "corners_view_frame_m": (corners / 100).round(2).tolist(),
            "view_frame_bbox_m": ((corners.max(0) - corners.min(0)) / 100).round(2).tolist(),
            "world_axis_bbox_m": ((world.max(0) - world.min(0)) / 100).round(2).tolist()}


def measure_streaming_budget(timing_rows):
    speed_m_s = WALK_SPEED_CM_PER_S / 100
    footprints = [camera_footprint(1100, -50, 45, 90, 16 / 9), camera_footprint(800, -60, 45, 90, 16 / 9)]
    budget = []
    full_median = {(row["chunk_m"], row["spacing_m"]): row["median_ms"] for row in timing_rows
                   if row["mode"] == "full" and row["location"] == "far_1e7"}
    width_m, depth_m = footprints[0]["world_axis_bbox_m"]
    for (chunk_m, spacing_m), median_ms in full_median.items():
        window_x = math.ceil(width_m / chunk_m) + 1 + 2
        window_y = math.ceil(depth_m / chunk_m) + 1 + 2
        axis_rate = max(window_x, window_y) * speed_m_s / chunk_m
        diagonal_rate = (window_x + window_y) * (speed_m_s / math.sqrt(2)) / chunk_m
        rate = max(axis_rate, diagonal_rate)
        budget.append({"chunk_m": chunk_m, "spacing_m": spacing_m, "window_chunks": [window_x, window_y],
                       "new_chunks_per_second": rate, "full_generation_ms": median_ms,
                       "cpu_share_one_core": rate * median_ms / 1000.0,
                       "seconds_to_cross_one_chunk": chunk_m / speed_m_s})
    print("camera", footprints)
    print("budget", budget)
    return {"footprints": footprints, "budget": budget}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="Saved/AgentOps/20261023/infinite-world/proto")
    parser.add_argument("--repeats", type=int, default=31)
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    repeats = 5 if args.quick else args.repeats
    results = {"world_seed": WORLD_SEED, "numpy": np.__version__, "python": sys.version.split()[0]}
    started = time.perf_counter()
    timing_rows, structure_timing = measure_timing(repeats)
    results["timing"] = timing_rows
    results["timing_chunk_with_structure"] = structure_timing
    results["seams"] = measure_seams()
    results["order"] = measure_order_independence()
    results["biomes"] = measure_biomes(args.out, args.quick)
    results["structures"] = measure_structures(args.out)
    results["precision"] = measure_precision(args.out)
    results["streaming"] = measure_streaming_budget(timing_rows)
    results["total_seconds"] = time.perf_counter() - started
    with open(os.path.join(args.out, "results.json"), "w", encoding="utf-8") as handle:
        json.dump(results, handle, indent=1, ensure_ascii=False)
    with open(os.path.join(args.out, "timing.csv"), "w", encoding="utf-8") as handle:
        handle.write("chunk_m,spacing_m,location,mode,samples,median_ms,p10_ms,p90_ms,ns_per_sample,structures_touched,output_bytes\n")
        for row in timing_rows:
            handle.write(f"{row['chunk_m']},{row['spacing_m']},{row['location']},{row['mode']},{row['samples']},"
                         f"{row['median_ms']:.3f},{row['p10_ms']:.3f},{row['p90_ms']:.3f},{row['ns_per_sample']:.1f},"
                         f"{row['structures_touched']},{row['output_bytes']}\n")
    print(f"done in {results['total_seconds']:.1f}s")


if __name__ == "__main__":
    main()
