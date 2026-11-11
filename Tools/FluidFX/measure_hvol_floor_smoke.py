"""실행 환경: 시스템 Python(numpy·Pillow·scipy), 에디터 불필요.
실행 명령: python -B Tools/FluidFX/measure_hvol_floor_smoke.py regions --prereg <prereg.json> --shots <fs 촬영 폴더> --output <json> [--overlay <png>] | judge --prereg <prereg.json> --shots <fs 촬영 폴더> --before-a <폴더> --before-b <폴더> --after <폴더> --output <json> [--overlay <png>] | self-test --prereg <prereg.json>
출력: regions 는 숨김·마스크 장만으로 상자 윗면·둘레 바닥 영역을 만들고, judge 는 기여·잡음·판별 조건·H1~H4·판정을 JSON 으로 쓴다(종료 코드 pass 0, fail 1, invalid 2). 문턱·영역 값은 모두 prereg 에서 읽는다.
상태: hvfs(HVOL 바닥 연기 TerrainOnly) 판정용(2026-11-11), self-test 합성 fixture 로 거짓 양성·음성 반박."""
import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

EXIT_CODES = {"pass": 0, "fail": 1, "invalid": 2}
SQUARE_STRUCTURE = np.ones((3, 3), dtype=bool)
CROSS_STRUCTURE = ndimage.generate_binary_structure(2, 1)
FLOOR_SMOKE_VARIANTS = ("fs_hidden", "fs_hidden_repeat", "fs_mask", "fs_on", "fs_on_repeat", "fs_off")
SELF_TEST_SIZE = (540, 960)
SELF_TEST_IDENTITY_SIZE = (108, 192)
SELF_TEST_BOX_SIDE = 60
SELF_TEST_NOISE_SIGMA = 0.4
SELF_TEST_LAYOUT = {"TDGen_FloorBump0": (400, 250), "TDGen_FloorBump1": (280, 600), "TDGen_FloorBump2": (150, 300)}
SELF_TEST_SWAPPED_LAYOUT = {"TDGen_FloorBump0": (400, 250), "TDGen_FloorBump1": (280, 200), "TDGen_FloorBump2": (150, 600)}
FLOOR_SMOKE_ACTOR = "TDGen_HvolFloorSmoke"


def load_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def find_shot(folder, name):
    pattern = re.compile(rf"^\d+_{re.escape(name)}_\d+ms\.png$")
    matches = sorted(path for path in Path(folder).iterdir() if pattern.match(path.name)) if Path(folder).is_dir() else []
    if len(matches) != 1:
        return None, f"{folder}: {name} 장 {len(matches)}개"
    return matches[0], None


def read_luminance(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64).mean(axis=2)


def normalized_label(value):
    return str(value).split(".")[-1].replace("_", "").upper()


def scaled_pixels(value, scale):
    return max(int(round(value * scale)), 0)


def dilate(mask, pixels):
    if pixels <= 0:
        return mask.copy()
    return ndimage.binary_dilation(mask, structure=SQUARE_STRUCTURE, iterations=pixels)


def erode(mask, pixels):
    if pixels <= 0:
        return mask.copy()
    return ndimage.binary_erosion(mask, structure=SQUARE_STRUCTURE, iterations=pixels)


def floor_smoke_variant_names(prereg):
    return [name for name in FLOOR_SMOKE_VARIANTS if name in prereg["variants"]]


def build_regions(prereg, hidden, hidden_repeat, mask):
    rules = prereg["regions"]
    height, width = hidden.shape
    scale = height / float(prereg["reference_height_px"])
    area_scale = scale * scale
    border = scaled_pixels(rules["frame_border_px"], scale)
    inside = np.zeros(hidden.shape, dtype=bool)
    inside[border:height - border, border:width - border] = True
    difference = hidden - mask
    box_mask = dilate(np.abs(difference) > rules["box_mask_threshold_255"], scaled_pixels(rules["box_mask_dilate_px"], scale))
    unstable = np.abs(hidden - hidden_repeat) > rules["unstable_pixel_threshold_255"]
    stable_inside = inside & ~unstable
    top_rule = rules["top_threshold"]
    percentile_value = float(np.percentile(difference[inside], top_rule["percentile"]))
    top_threshold = max(top_rule["minimum_255"], top_rule["fraction_of_percentile"] * percentile_value)
    candidates = (difference > top_threshold) & inside
    component_rule = rules["top_component"]
    structure = SQUARE_STRUCTURE if component_rule["connectivity"] == 8 else CROSS_STRUCTURE
    labels, count = ndimage.label(candidates, structure=structure)
    areas = ndimage.sum(candidates, labels, index=np.arange(1, count + 1)) if count else np.array([])
    minimum_area = component_rule["min_area_px"] * area_scale
    eligible = sorted(((float(area), index + 1) for index, area in enumerate(areas) if area >= minimum_area), reverse=True)
    chosen = eligible[:component_rule["take_largest"]]
    report = {"scale": scale, "border_px": border, "difference_percentile_255": percentile_value, "top_threshold_255": top_threshold,
              "candidate_components": int(count), "eligible_components": len(eligible), "unstable_px": int((unstable & inside).sum()),
              "box_mask_px": int(box_mask.sum()), "errors": [], "boxes": {}}
    masks = {"box_mask": box_mask, "unstable": unstable & inside, "boxes": {}}
    boxes = sorted(prereg["stage_boxes"]["boxes"], key=lambda box: box["screen_order_from_bottom"])
    if len(chosen) != len(boxes):
        report["errors"].append(f"윗면 성분 {len(chosen)}개(필요 {len(boxes)})")
        return report, masks
    components = []
    for area, label_index in chosen:
        component = labels == label_index
        rows, columns = np.nonzero(component)
        components.append({"mask": component, "area_px": int(area), "centroid": [float(rows.mean()), float(columns.mean())]})
    components.sort(key=lambda item: item["centroid"][0], reverse=True)
    center_column = (width - 1) / 2.0
    distance_limit = rules["ring"]
    inner = distance_limit["inner_px"] * scale
    outer = distance_limit["outer_px"] * scale
    full_rings = []
    for component in components:
        distance = ndimage.distance_transform_edt(~component["mask"])
        full_rings.append((distance > inner) & (distance <= outer))
    for index, (component, box) in enumerate(zip(components, boxes)):
        is_right = component["centroid"][1] > center_column
        if is_right != (box["screen_side"] == "right"):
            report["errors"].append(f"{box['actor']}: 중심 열 {component['centroid'][1]:.0f} 이 등록 쪽({box['screen_side']})과 다름")
        others = np.zeros(hidden.shape, dtype=bool)
        for other_index, other in enumerate(components):
            if other_index != index:
                others |= other["mask"] | full_rings[other_index]
        near_rows = np.zeros(hidden.shape, dtype=bool)
        near_rows[int(np.ceil(component["centroid"][0])):, :] = True
        top = erode(component["mask"], scaled_pixels(component_rule["erode_px"], scale)) & stable_inside
        ring = full_rings[index] & ~box_mask & ~others & near_rows & stable_inside
        if not top.any():
            report["errors"].append(f"{box['actor']}: 침식 뒤 윗면 0 화소")
        if ring.sum() < distance_limit["min_area_px"] * area_scale:
            report["errors"].append(f"{box['actor']}: 둘레 {int(ring.sum())} 화소 < {distance_limit['min_area_px'] * area_scale:.0f}")
        masks["boxes"][box["actor"]] = {"top": top, "ring": ring}
        report["boxes"][box["actor"]] = {"component_area_px": component["area_px"], "centroid_row_column": [round(value, 1) for value in component["centroid"]],
                                          "top_px": int(top.sum()), "ring_px": int(ring.sum()), "height_cm": box["height_cm"]}
    ring_union = np.zeros(hidden.shape, dtype=bool)
    for box_masks in masks["boxes"].values():
        ring_union |= box_masks["ring"]
    masks["ring_union"] = ring_union
    report["ring_union_px"] = int(ring_union.sum())
    return report, masks


def write_overlay(path, hidden, masks):
    base = np.clip(hidden, 0, 255)[..., None].repeat(3, axis=2) * 0.6
    tint = [(masks.get("box_mask"), (255, 60, 60), 0.25), (masks.get("unstable"), (255, 0, 255), 0.6)]
    for box_masks in masks.get("boxes", {}).values():
        tint.append((box_masks["ring"], (60, 120, 255), 0.6))
        tint.append((box_masks["top"], (60, 255, 60), 0.6))
    for region, color, weight in tint:
        if region is None:
            continue
        base[region] = base[region] * (1.0 - weight) + np.array(color, dtype=np.float64) * weight
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.clip(base, 0, 255).astype(np.uint8)).save(path)


def load_floor_smoke_shots(prereg, shots_folder, names):
    errors = []
    shots = {}
    paths = {}
    for name in names:
        path, error = find_shot(shots_folder, name)
        if error:
            errors.append(error)
            continue
        paths[name] = {"path": str(path), "sha256": file_sha256(path)}
        shots[name] = read_luminance(path)
    shapes = {shot.shape for shot in shots.values()}
    if len(shapes) > 1:
        errors.append(f"장 크기가 다름: {sorted(shapes)}")
    return shots, paths, errors


def result_checks(prereg, shots_folder, names):
    result_path = Path(shots_folder) / "result.json"
    if not result_path.is_file():
        return {}, [f"{result_path} 없음"]
    result = load_json(result_path)
    errors = [f"result.json key_errors: {result['key_errors']}"] if result.get("key_errors") else []
    entries = {variant.get("name"): variant for variant in result.get("variants", [])}
    read_back_rule = prereg["capture"]["read_back"]
    field = read_back_rule["path"].split(".")[-1]
    hide_labels = sorted({label for name in names for label in prereg["variants"][name].get("hide", {})})
    observed = {}
    for name in names:
        variant = entries.get(name)
        if variant is None:
            errors.append(f"result.json 에 {name} 없음")
            continue
        apply = variant.get("apply", {})
        reload = apply.get("tuning_reload", {})
        if reload.get("error"):
            errors.append(f"{name}: tuning_reload 오류 {reload['error']}")
        expected = read_back_rule[prereg["variants"][name]["tuning"]]
        values = {actor: entry.get("value", entry.get("error")) for item in reload.get("read_back", []) if item.get("field") == field for actor, entry in item.get("actors", {}).items()}
        observed[name] = {"expected": expected, "values": values, "tuning": reload.get("tuning", "")}
        if FLOOR_SMOKE_ACTOR not in values or any(normalized_label(value) != normalized_label(expected) for value in values.values()):
            errors.append(f"{name}: 다시 읽기 {values} ≠ {expected}")
        hide_outcome = apply.get("hide", {})
        for label in hide_labels:
            wanted = bool(prereg["variants"][name].get("hide", {}).get(label, False))
            if hide_outcome.get(label) is not wanted:
                errors.append(f"{name}: 숨김 {label}={hide_outcome.get(label)} (필요 {wanted})")
    return observed, errors


def region_mean(values, region):
    return float(values[region].mean()) if region.any() else float("nan")


def identity_metric(prereg, before_a, before_b, after):
    rule = prereg["metrics"]["H4"]
    zones = {}
    errors = []
    for zone in rule["zones"]:
        name = f"z{zone}"
        paths = {}
        for role, folder in (("before", before_a), ("before_repeat", before_b), ("after", after)):
            path, error = find_shot(folder, name)
            if error:
                errors.append(error)
            paths[role] = path
        if any(path is None for path in paths.values()):
            continue
        frames = {role: read_luminance(path) for role, path in paths.items()}
        if len({frame.shape for frame in frames.values()}) > 1:
            errors.append(f"{name}: 장 크기가 다름")
            continue
        change = float(np.abs(frames["after"] - frames["before"]).mean())
        noise = float(np.abs(frames["before"] - frames["before_repeat"]).mean())
        limit = rule["noise_factor"] * noise + rule["epsilon_255"]
        zones[name] = {"after_minus_before_255": round(change, 4), "before_noise_255": round(noise, 4), "limit_255": round(limit, 4), "passed": change <= limit,
                       "files": {role: str(path) for role, path in paths.items()}}
    return zones, errors


def judge(prereg_path, shots_folder, before_a, before_b, after, overlay_path=None):
    prereg = load_json(prereg_path)
    names = floor_smoke_variant_names(prereg)
    report = {"prereg": str(prereg_path), "prereg_sha256": file_sha256(prereg_path), "shots_folder": str(shots_folder), "errors": []}
    shots, paths, shot_errors = load_floor_smoke_shots(prereg, shots_folder, names)
    report["shots"] = paths
    report["errors"] += shot_errors
    read_back, result_errors = result_checks(prereg, shots_folder, names)
    report["read_back"] = read_back
    report["errors"] += result_errors
    zones, identity_errors = identity_metric(prereg, before_a, before_b, after)
    report["errors"] += identity_errors
    if shot_errors:
        return finish(report, valid=False, metrics_passed=False)
    region_report, masks = build_regions(prereg, shots["fs_hidden"], shots["fs_hidden_repeat"], shots["fs_mask"])
    report["regions"] = region_report
    report["errors"] += region_report["errors"]
    if overlay_path:
        write_overlay(overlay_path, shots["fs_hidden"], masks)
    if region_report["errors"]:
        return finish(report, valid=False, metrics_passed=False)
    hidden = shots["fs_hidden"]
    noise_map = np.maximum(np.abs(shots["fs_on"] - shots["fs_on_repeat"]), np.abs(hidden - shots["fs_hidden_repeat"]))
    regions = {f"top:{actor}": box_masks["top"] for actor, box_masks in masks["boxes"].items()}
    regions["ring_union"] = masks["ring_union"]
    values = {}
    for region_name, region in regions.items():
        values[region_name] = {"C_off_255": region_mean(np.abs(shots["fs_off"] - hidden), region), "C_on_255": region_mean(np.abs(shots["fs_on"] - hidden), region),
                               "C_on_repeat_255": region_mean(np.abs(shots["fs_on_repeat"] - hidden), region), "N_255": region_mean(noise_map, region), "pixels": int(region.sum())}
        values[region_name] = {key: (round(value, 4) if isinstance(value, float) else value) for key, value in values[region_name].items()}
    report["values_255"] = values
    guards = {}
    g1 = prereg["guards"]["G1"]
    for actor in masks["boxes"]:
        entry = values[f"top:{actor}"]
        limit = max(g1["noise_factor"] * entry["N_255"], g1["minimum_255"])
        guards[f"G1:{actor}"] = {"C_off_255": entry["C_off_255"], "limit_255": round(limit, 4), "passed": entry["C_off_255"] >= limit}
    g2 = prereg["guards"]["G2"]
    ring_entry = values["ring_union"]
    ring_limit = max(g2["noise_factor"] * ring_entry["N_255"], g2["minimum_255"])
    guards["G2:ring_union"] = {"C_off_255": ring_entry["C_off_255"], "limit_255": round(ring_limit, 4), "passed": ring_entry["C_off_255"] >= ring_limit}
    report["guards"] = guards
    metrics = {}
    h1 = prereg["metrics"]["H1"]
    for actor in h1["boxes"]:
        entry = values[f"top:{actor}"]
        limit = h1["noise_factor"] * entry["N_255"] + h1["epsilon_255"]
        metrics[f"H1:{actor}"] = {"C_on_255": entry["C_on_255"], "C_off_255": entry["C_off_255"], "limit_255": round(limit, 4), "passed": entry["C_on_255"] <= limit}
    h2 = prereg["metrics"]["H2"]
    for actor in h2["boxes"]:
        entry = values[f"top:{actor}"]
        limit = h2["max_ratio"] * entry["C_off_255"]
        metrics[f"H2:{actor}"] = {"C_on_255": entry["C_on_255"], "C_off_255": entry["C_off_255"], "limit_255": round(limit, 4), "passed": entry["C_on_255"] <= limit}
    h3 = prereg["metrics"]["H3"]
    h3_limit = h3["min_ratio"] * ring_entry["C_off_255"]
    metrics["H3:ring_union"] = {"C_on_255": ring_entry["C_on_255"], "C_off_255": ring_entry["C_off_255"], "limit_255": round(h3_limit, 4), "passed": ring_entry["C_on_255"] >= h3_limit}
    metrics["H4"] = {"zones": zones, "passed": len(zones) == len(prereg["metrics"]["H4"]["zones"]) and all(zone["passed"] for zone in zones.values())}
    report["metrics"] = metrics
    valid = not report["errors"] and all(guard["passed"] for guard in guards.values())
    return finish(report, valid=valid, metrics_passed=all(metric["passed"] for metric in metrics.values()))


def finish(report, valid, metrics_passed):
    report["valid"] = valid
    report["verdict"] = "invalid" if not valid else ("pass" if metrics_passed else "fail")
    report["exit_code"] = EXIT_CODES[report["verdict"]]
    return report


def regions_only(prereg_path, shots_folder, overlay_path=None):
    prereg = load_json(prereg_path)
    names = ["fs_hidden", "fs_hidden_repeat", "fs_mask"]
    shots, paths, errors = load_floor_smoke_shots(prereg, shots_folder, names)
    report = {"prereg_sha256": file_sha256(prereg_path), "shots": paths, "errors": list(errors)}
    if errors:
        report["exit_code"] = EXIT_CODES["invalid"]
        return report
    region_report, masks = build_regions(prereg, shots["fs_hidden"], shots["fs_hidden_repeat"], shots["fs_mask"])
    report["regions"] = region_report
    report["errors"] += region_report["errors"]
    if overlay_path:
        write_overlay(overlay_path, shots["fs_hidden"], masks)
    report["exit_code"] = EXIT_CODES["invalid"] if report["errors"] else EXIT_CODES["pass"]
    return report


def synthetic_layers(layout):
    height, width = SELF_TEST_SIZE
    rows, columns = np.mgrid[0:height, 0:width]
    floor = 90.0 + 20.0 * columns / width
    tops = {}
    shadows = np.zeros(SELF_TEST_SIZE, dtype=bool)
    for actor, (row, column) in layout.items():
        in_rows = (rows >= row) & (rows < row + SELF_TEST_BOX_SIDE)
        tops[actor] = in_rows & (columns >= column) & (columns < column + SELF_TEST_BOX_SIDE)
        shadows |= in_rows & (columns >= column + SELF_TEST_BOX_SIDE) & (columns < column + SELF_TEST_BOX_SIDE + 25)
    return floor, tops, shadows


def synthetic_frame(layers, floor_smoke, top_smoke, is_mask=False):
    floor, tops, shadows = layers
    frame = floor.copy()
    if is_mask:
        return frame
    frame[shadows] = 55.0
    frame = frame + floor_smoke
    for actor, top in tops.items():
        frame[top] = 170.0 + top_smoke.get(actor, 0.0)
    return frame


def save_noisy(path, frame, generator):
    noisy = np.clip(np.round(frame + generator.normal(0.0, SELF_TEST_NOISE_SIGMA, frame.shape)), 0, 255).astype(np.uint8)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.stack([noisy] * 3, axis=2)).save(path)


def write_synthetic_case(folder, prereg, case, generator):
    layers = synthetic_layers(SELF_TEST_SWAPPED_LAYOUT if case.get("swapped") else SELF_TEST_LAYOUT)
    defect = {actor: 20.0 for actor in SELF_TEST_LAYOUT}
    fixed = {"TDGen_FloorBump0": 6.0, "TDGen_FloorBump1": 0.0, "TDGen_FloorBump2": 0.0}
    frames = {"fs_hidden": synthetic_frame(layers, 0.0, {}), "fs_hidden_repeat": synthetic_frame(layers, 0.0, {}), "fs_mask": synthetic_frame(layers, 0.0, {}, is_mask=True)}
    on_tops = defect if case.get("defect_kept") else fixed
    on_floor = 0.0 if case.get("floor_smoke_lost") else 20.0
    if case.get("floor_smoke_lost"):
        on_tops = {}
    frames["fs_on"] = synthetic_frame(layers, on_floor, on_tops)
    frames["fs_on_repeat"] = synthetic_frame(layers, on_floor, on_tops)
    frames["fs_off"] = synthetic_frame(layers, 20.0, fixed if case.get("no_defect_in_off") else defect)
    shots = Path(folder) / "shots"
    variants = []
    for index, name in enumerate(FLOOR_SMOKE_VARIANTS):
        save_noisy(shots / f"{index:02d}_{name}_08000ms.png", frames[name], generator)
        side = prereg["variants"][name]["tuning"]
        value = "TERRAIN_ONLY" if side == "on" else "ALL_GEOMETRY"
        if case.get("read_back_wrong") and name == "fs_off":
            value = "TERRAIN_ONLY"
        hide = {label: bool(prereg["variants"][name].get("hide", {}).get(label, False)) for label in ("TDGen_HvolFloorSmoke", "TDGen_FloorBump0", "TDGen_FloorBump1", "TDGen_FloorBump2")}
        variants.append({"name": name, "apply": {"hide": hide, "tuning_reload": {"reset": "reinit", "read_back": [{"field": "SceneCaptureContent", "actors": {FLOOR_SMOKE_ACTOR: {"value": value}}}]}}})
    (shots / "result.json").write_text(json.dumps({"variants": variants, "key_errors": []}), encoding="utf-8")
    identity_base = 80.0 + 40.0 * np.linspace(0.0, 1.0, SELF_TEST_IDENTITY_SIZE[1])[None, :].repeat(SELF_TEST_IDENTITY_SIZE[0], axis=0)
    for zone in prereg["metrics"]["H4"]["zones"]:
        for role in ("before_a", "before_b", "after"):
            offset = 2.0 if case.get("identity_changed") and role == "after" else 0.0
            save_noisy(Path(folder) / role / f"{zone:02d}_z{zone}_08000ms.png", identity_base + offset, generator)
    return shots, Path(folder) / "before_a", Path(folder) / "before_b", Path(folder) / "after"


def self_test(prereg_path):
    prereg = load_json(prereg_path)
    cases = [("pass", {}, "pass"), ("defect_kept", {"defect_kept": True}, "fail"), ("floor_smoke_lost", {"floor_smoke_lost": True}, "fail"),
             ("no_defect_in_off", {"no_defect_in_off": True}, "invalid"), ("swapped_labels", {"swapped": True}, "invalid"),
             ("identity_changed", {"identity_changed": True}, "fail"), ("read_back_wrong", {"read_back_wrong": True}, "invalid")]
    outcomes = []
    with tempfile.TemporaryDirectory() as root:
        for name, case, expected in cases:
            generator = np.random.default_rng(len(outcomes) + 11)
            shots, before_a, before_b, after = write_synthetic_case(Path(root) / name, prereg, case, generator)
            report = judge(prereg_path, shots, before_a, before_b, after)
            failed = sorted(key for key, metric in report.get("metrics", {}).items() if not metric["passed"])
            outcomes.append({"case": name, "expected": expected, "verdict": report["verdict"], "matched": report["verdict"] == expected,
                             "failed_metrics": failed, "errors": report["errors"][:2]})
    for outcome in outcomes:
        print(json.dumps(outcome, ensure_ascii=False))
    all_matched = all(outcome["matched"] for outcome in outcomes)
    print("SELF_TEST " + ("PASS" if all_matched else "FAIL"))
    return 0 if all_matched else 1


def write_report(path, report):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="HVOL 바닥 연기 TerrainOnly 판정")
    commands = parser.add_subparsers(dest="command", required=True)
    regions_parser = commands.add_parser("regions")
    judge_parser = commands.add_parser("judge")
    self_test_parser = commands.add_parser("self-test")
    for command_parser in (regions_parser, judge_parser, self_test_parser):
        command_parser.add_argument("--prereg", required=True)
    for command_parser in (regions_parser, judge_parser):
        command_parser.add_argument("--shots", required=True)
        command_parser.add_argument("--output", required=True)
        command_parser.add_argument("--overlay")
    for name in ("--before-a", "--before-b", "--after"):
        judge_parser.add_argument(name, required=True)
    arguments = parser.parse_args()
    if arguments.command == "self-test":
        return self_test(arguments.prereg)
    if arguments.command == "regions":
        report = regions_only(arguments.prereg, arguments.shots, arguments.overlay)
    else:
        report = judge(arguments.prereg, arguments.shots, arguments.before_a, arguments.before_b, arguments.after, arguments.overlay)
    write_report(arguments.output, report)
    print(json.dumps({"verdict": report.get("verdict"), "exit_code": report["exit_code"], "errors": report["errors"][:5]}, ensure_ascii=False))
    return report["exit_code"]


if __name__ == "__main__":
    sys.exit(main())
