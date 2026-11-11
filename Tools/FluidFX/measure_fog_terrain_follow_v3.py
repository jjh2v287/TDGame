"""실행 환경: 시스템 Python 3(numpy·Pillow), 에디터 불필요(PowerShell 또는 Git Bash). v2 도구 measure_fog_terrain_follow.py 의 영역·평균 함수를 가져다 쓰고 v2 파일은 고치지 않는다.
실행 명령: python -B Tools/FluidFX/measure_fog_terrain_follow_v3.py judge-relief --shots <촬영 폴더> --prereg <prereg-v3.json> --output <판정 JSON> [--overlay <PNG>] | judge-tile --shots <촬영 폴더> --prereg <prereg-v3.json> --output <판정 JSON> [--overlay <PNG>] [--shot-spec <촬영 명세 JSON, 기본 prereg tools.shot_specs 의 tile 명세>] | regions --kind relief|tile --shots <촬영 폴더> --prereg <prereg-v3.json> --overlay <PNG> | --self-test
출력: 판정 JSON{kind, prereg 경로·sha256, shots, regions(능선·지형 열 T·빠진 열 구간·평지 행·돌출물 칸 / IN·OUT 열), guards(지형 열·M4 낮은 칸·감싸기 보임){값, 문턱, passed}, noise_floor, metrics(relief M1~M5 / tile T1~T4){값, 문턱, passed}, records, valid, verdict pass|fail|invalid}과 오버레이 PNG, 종료 0 통과·1 실패·2 판정 불가(장 없음·크기 다름·영역 자기 점검·판별 조건 실패·tile 촬영 명세 자세가 prereg 와 다름). regions 는 숨김 장만으로 영역을 정해 그림으로 남긴다. --self-test 는 합성 장 fixture ①~⑧과 기준 통과 세트를 돌려 모두 기대와 같으면 종료 0.
상태: 미검증(2026-11-11 Q4-1b, prereg-v3 r2 규칙 구현, --self-test 합성 fixture 만, 실제 촬영 전). 문턱·영역 값은 모두 prereg JSON 에서 읽는다(코드 안 문턱 상수 없음). 화소 값은 1080 행 기준이며 장 높이 비례로 바꾼다(v2 와 같음).
"""
import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

from measure_fog_terrain_follow import (
    RegionError,
    absolute_mean,
    column_contributions,
    find_overhang_regions,
    find_shot_files,
    find_side_regions,
    group_index_runs,
    load_frame,
    make_overhang_scene,
    overhang_cap_mask,
    overhang_cell_mask,
    scaled_pixels,
    side_band_mask,
    side_floor_mask,
    signed_mean,
)

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TILE_SHOT_SPEC_NAME = "q4b_fog_tile_relief.json"


def to_json_value(value):
    if isinstance(value, np.bool_):
        return bool(value)
    if isinstance(value, np.integer):
        return int(value)
    if isinstance(value, np.floating):
        return float(value)
    if isinstance(value, np.ndarray):
        return value.tolist()
    raise TypeError(f"JSON 으로 바꿀 수 없음: {type(value)}")


def read_prereg(path):
    raw = Path(path).read_bytes()
    return json.loads(raw.decode("utf-8")), hashlib.sha256(raw).hexdigest()


def frame_scale(shape, prereg):
    return shape[0] / prereg["reference_height_px"]


def relief_region_values(prereg):
    relief = prereg["relief"]
    metrics = relief["metrics"]
    return {
        "reference_height_px": prereg["reference_height_px"],
        "side_regions": relief["side_regions"],
        "overhang_regions": relief["overhang_regions"],
        "metrics": {"M1": metrics["M1"], "M4": metrics["M4"], "cap_record": {"z_cm": metrics["records"]["cap_z_cm"]}},
    }


def tile_region_values(prereg):
    tile = prereg["tile"]
    return {
        "reference_height_px": prereg["reference_height_px"],
        "side_regions": tile["side_regions"],
        "metrics": {"M1": {"band_px": tile["metrics"]["T1"]["band_px"]}},
    }


def with_columns(regions, columns):
    return dict(regions, ridge_columns=columns)


def column_runs(columns):
    return [[int(start), int(end)] for start, end in group_index_runs(np.asarray(columns))]


def find_terrain_columns(hidden_regions, mask_regions, prereg, scale):
    rule = prereg["relief"]["terrain_columns_v3"]
    guards = rule["guards"]
    agreement = scaled_pixels(rule["top_agreement_px"], scale)
    ridge_hidden = hidden_regions["ridge_columns"]
    is_mask_ridge = np.zeros(hidden_regions["top_rows"].size, dtype=bool)
    is_mask_ridge[mask_regions["ridge_columns"]] = True
    top_difference = np.abs(hidden_regions["top_rows"][ridge_hidden] - mask_regions["top_rows"][ridge_hidden])
    terrain = ridge_hidden[is_mask_ridge[ridge_hidden] & (top_difference <= agreement)]
    excluded = np.setdiff1d(ridge_hidden, terrain)
    excluded_fraction = float(excluded.size / ridge_hidden.size) if ridge_hidden.size else 1.0
    minimum = guards["min_terrain_columns"] * scale
    guard = {
        "terrain_column_count": int(terrain.size),
        "hidden_ridge_column_count": int(ridge_hidden.size),
        "excluded_fraction": excluded_fraction,
        "minimum_terrain_columns": minimum,
        "maximum_excluded_fraction": guards["max_excluded_fraction"],
        "top_agreement_px": agreement,
        "passed": bool(terrain.size >= minimum and excluded_fraction <= guards["max_excluded_fraction"]),
    }
    return terrain, excluded, guard


def find_relief_regions(frames, prereg):
    values = relief_region_values(prereg)
    side_hidden = find_side_regions(frames["side_hidden"], values)
    side_mask = find_side_regions(frames["side_terrain_mask"], values)
    try:
        overhang = find_overhang_regions(frames["overhang_hidden"], values)
    except RegionError as error:
        overhang = {"problems": [str(error)]}
    problems = [f"옆 단면 숨김: {problem}" for problem in side_hidden["problems"]]
    problems += [f"옆 단면 지형 마스크: {problem}" for problem in side_mask["problems"]]
    problems += [f"돌출물: {problem}" for problem in overhang["problems"]]
    terrain, excluded, terrain_guard = find_terrain_columns(side_hidden, side_mask, prereg, frame_scale(frames["side_hidden"].shape, prereg))
    regions = {
        "side": {
            "band_px": side_hidden["band_px"],
            "hidden_ridge_column_count": int(side_hidden["ridge_columns"].size),
            "mask_ridge_column_count": int(side_mask["ridge_columns"].size),
            "terrain_column_count": int(terrain.size),
            "terrain_column_runs": column_runs(terrain),
            "excluded_column_runs": column_runs(excluded),
            "floor_rows": list(side_hidden["floor_rows"]),
        },
        "overhang": {key: value for key, value in overhang.items() if key != "problems"},
    }
    return {"side_hidden": side_hidden, "terrain": terrain, "excluded": excluded, "overhang": overhang}, regions, problems, terrain_guard


def measure_relief(frames, prereg):
    metrics = prereg["relief"]["metrics"]
    result = {"kind": "relief", "valid": True, "problems": [], "guards": {}, "regions": {}, "metrics": {}, "records": {}}
    shapes = sorted({frame.shape for frame in frames.values()})
    if len(shapes) != 1:
        return result | {"valid": False, "verdict": "invalid", "problems": [f"장 크기 다름 {shapes}"]}
    found, result["regions"], result["problems"], result["guards"]["terrain_columns"] = find_relief_regions(frames, prereg)
    noise = absolute_mean(frames["side_on"], frames["side_on_repeat"])
    result["noise_floor"] = noise
    if result["problems"] or not result["guards"]["terrain_columns"]["passed"]:
        return result | {"valid": False, "verdict": "invalid"}

    shape = frames["side_hidden"].shape
    terrain_regions = with_columns(found["side_hidden"], found["terrain"])
    band_mask = side_band_mask(terrain_regions, shape)
    on_band = signed_mean(frames["side_on"], frames["side_hidden"], band_mask)
    off_band = signed_mean(frames["side_off"], frames["side_hidden"], band_mask)
    m1 = metrics["M1"]
    result["metrics"]["M1"] = {
        "on_minus_hidden_255": on_band * 255.0,
        "off_minus_hidden_255": off_band * 255.0,
        "threshold": {"on_over_off_factor": m1["on_over_off_factor"], "on_minimum_255": m1["on_minimum_255"]},
        "rule": m1["rule"],
        "passed": bool(on_band >= m1["on_over_off_factor"] * off_band and on_band >= m1["on_minimum_255"] / 255.0),
    }
    m2 = metrics["M2"]
    on_ratio = float((column_contributions(frames["side_on"], frames["side_hidden"], terrain_regions) < m2["column_threshold_255"] / 255.0).mean())
    off_ratio = float((column_contributions(frames["side_off"], frames["side_hidden"], terrain_regions) < m2["column_threshold_255"] / 255.0).mean())
    result["metrics"]["M2"] = {
        "on_buried_ratio": on_ratio,
        "off_buried_ratio": off_ratio,
        "threshold": {"column_threshold_255": m2["column_threshold_255"], "on_max_ratio": m2["on_max_ratio"], "off_min_ratio": m2["off_min_ratio"]},
        "rule": m2["rule"],
        "passed": bool(on_ratio <= m2["on_max_ratio"] and off_ratio >= m2["off_min_ratio"]),
    }
    floor_mask = side_floor_mask(found["side_hidden"], shape)
    on_off = absolute_mean(frames["side_on"], frames["side_off"], floor_mask)
    on_repeat = absolute_mean(frames["side_on"], frames["side_on_repeat"], floor_mask)
    m3 = metrics["M3"]
    result["metrics"]["M3"] = {
        "on_off_abs_255": on_off * 255.0,
        "on_repeat_abs_255": on_repeat * 255.0,
        "threshold": {"noise_factor": m3["noise_factor"]},
        "rule": m3["rule"],
        "passed": bool(on_off <= m3["noise_factor"] * on_repeat),
    }
    m4 = metrics["M4"]
    overhang = found["overhang"]
    high_mask = overhang_cell_mask(overhang, shape, overhang["high_rows"])
    low_mask = overhang_cell_mask(overhang, shape, overhang["low_rows"])
    high_on_off = signed_mean(frames["overhang_on"], frames["overhang_off"], high_mask)
    high_limit = max(m4["high_minimum_255"] / 255.0, m4["high_noise_factor"] * noise)
    low_on = signed_mean(frames["overhang_on"], frames["overhang_hidden"], low_mask)
    low_off = signed_mean(frames["overhang_off"], frames["overhang_hidden"], low_mask)
    low_visible_limit = max(m4["low_visible_minimum_255"] / 255.0, m4["low_visible_noise_factor"] * noise)
    low_limit = max(m4["low_ratio"] * abs(low_off), m4["low_noise_factor"] * noise)
    low_visible = abs(low_off) >= low_visible_limit
    result["guards"]["M4_low_visible"] = {
        "abs_off_minus_hidden_255": abs(low_off) * 255.0,
        "limit_255": low_visible_limit * 255.0,
        "passed": bool(low_visible),
    }
    high_passed = high_on_off <= high_limit
    low_passed = abs(low_on - low_off) <= low_limit
    result["metrics"]["M4"] = {
        "high_on_minus_off_255": high_on_off * 255.0,
        "high_limit_255": high_limit * 255.0,
        "high_passed": bool(high_passed),
        "low_on_minus_hidden_255": low_on * 255.0,
        "low_off_minus_hidden_255": low_off * 255.0,
        "low_abs_on_minus_off_255": abs(low_on - low_off) * 255.0,
        "low_limit_255": low_limit * 255.0,
        "low_passed": bool(low_passed),
        "rule": m4["high_rule"] + " ; " + m4["low_rule"],
        "passed": bool(high_passed and low_passed),
    }
    m5 = metrics["M5"]
    high_allgeom = signed_mean(frames["overhang_allgeom"], frames["overhang_hidden"], high_mask)
    result["metrics"]["M5"] = {
        "high_allgeom_minus_hidden_255": high_allgeom * 255.0,
        "threshold": {"high_maximum_255": m5["high_maximum_255"]},
        "rule": m5["rule"],
        "passed": bool(high_allgeom <= m5["high_maximum_255"] / 255.0),
    }
    cap_mask = overhang_cap_mask(overhang, shape)
    result["records"]["cap"] = {
        "allgeom_minus_hidden_255": signed_mean(frames["overhang_allgeom"], frames["overhang_hidden"], cap_mask) * 255.0 if cap_mask.any() else None,
        "on_minus_hidden_255": signed_mean(frames["overhang_on"], frames["overhang_hidden"], cap_mask) * 255.0 if cap_mask.any() else None,
    }
    result["records"]["gamecam_on_off_abs_255"] = absolute_mean(frames["gamecam_on"], frames["gamecam_off"]) * 255.0
    result["records"]["excluded_column_runs"] = result["regions"]["side"]["excluded_column_runs"]
    if not low_visible:
        return result | {"valid": False, "verdict": "invalid"}
    result["verdict"] = "pass" if all(item["passed"] for item in result["metrics"].values()) else "fail"
    return result


def split_tile_columns(regions, prereg, width):
    split = prereg["tile"]["split"]
    ridge = regions["ridge_columns"]
    split_column = split["split_fraction"] * width
    return ridge[ridge >= split_column], ridge[ridge < split_column]


def find_tile_regions(frames, prereg):
    shape = frames["side_hidden"].shape
    scale = frame_scale(shape, prereg)
    regions = find_side_regions(frames["side_hidden"], tile_region_values(prereg))
    inside, outside = split_tile_columns(regions, prereg, shape[1])
    minimum = prereg["tile"]["split"]["min_columns_each_side"] * scale
    problems = [f"옆 단면 숨김: {problem}" for problem in regions["problems"]]
    if inside.size < minimum:
        problems.append(f"IN(화면 오른쪽) 능선 열 {inside.size} < {minimum:.0f}")
    if outside.size < minimum:
        problems.append(f"OUT(화면 왼쪽) 능선 열 {outside.size} < {minimum:.0f}")
    summary = {
        "band_px": regions["band_px"],
        "ridge_column_count": int(regions["ridge_columns"].size),
        "in_column_count": int(inside.size),
        "out_column_count": int(outside.size),
        "in_column_runs": column_runs(inside),
        "out_column_runs": column_runs(outside),
        "minimum_columns_each_side": minimum,
        "floor_rows": list(regions["floor_rows"]),
    }
    return {"side_hidden": regions, "inside": inside, "outside": outside}, summary, problems


def wrap_guard_mask(regions, prereg, shape):
    fraction = prereg["tile"]["guards"]["wrap_visible"]["wrap_columns_below_fraction"]
    mask = side_floor_mask(regions, shape)
    mask &= (np.arange(shape[1]) < fraction * shape[1])[None, :]
    return mask


def measure_tile(frames, prereg):
    tile = prereg["tile"]
    metrics = tile["metrics"]
    result = {"kind": "tile", "valid": True, "problems": [], "guards": {}, "regions": {}, "metrics": {}, "records": {}}
    shapes = sorted({frame.shape for frame in frames.values()})
    if len(shapes) != 1:
        return result | {"valid": False, "verdict": "invalid", "problems": [f"장 크기 다름 {shapes}"]}
    found, result["regions"], result["problems"] = find_tile_regions(frames, prereg)
    noise = absolute_mean(frames["side_on"], frames["side_on_repeat"])
    result["noise_floor"] = noise
    if result["problems"]:
        return result | {"valid": False, "verdict": "invalid"}

    shape = frames["side_hidden"].shape
    regions = found["side_hidden"]
    guard_mask = wrap_guard_mask(regions, prereg, shape)
    wrap = tile["guards"]["wrap_visible"]
    wrap_value = signed_mean(frames["side_off"], frames["side_hidden"], guard_mask) if guard_mask.any() else 0.0
    wrap_passed = abs(wrap_value) >= wrap["minimum_255"] / 255.0
    result["guards"]["wrap_visible"] = {
        "abs_off_minus_hidden_255": abs(wrap_value) * 255.0,
        "minimum_255": wrap["minimum_255"],
        "columns_below_fraction": wrap["wrap_columns_below_fraction"],
        "passed": bool(wrap_passed),
    }
    if not wrap_passed:
        return result | {"valid": False, "verdict": "invalid"}

    band_inside = side_band_mask(with_columns(regions, found["inside"]), shape)
    band_outside = side_band_mask(with_columns(regions, found["outside"]), shape)
    on_inside = signed_mean(frames["side_on"], frames["side_hidden"], band_inside)
    off_inside = signed_mean(frames["side_off"], frames["side_hidden"], band_inside)
    on_outside = signed_mean(frames["side_on"], frames["side_hidden"], band_outside)
    off_outside = signed_mean(frames["side_off"], frames["side_hidden"], band_outside)
    t1 = metrics["T1"]
    result["metrics"]["T1"] = {
        "on_minus_hidden_255": on_inside * 255.0,
        "off_minus_hidden_255": off_inside * 255.0,
        "threshold": {"on_over_off_factor": t1["on_over_off_factor"], "on_minimum_255": t1["on_minimum_255"]},
        "rule": t1["rule"],
        "passed": bool(on_inside >= t1["on_over_off_factor"] * off_inside and on_inside >= t1["on_minimum_255"] / 255.0),
    }
    t2 = metrics["T2"]
    result["metrics"]["T2"] = {
        "on_minus_hidden_255": on_outside * 255.0,
        "off_minus_hidden_255": off_outside * 255.0,
        "in_on_minus_hidden_255": on_inside * 255.0,
        "threshold": {"on_over_off_factor": t2["on_over_off_factor"], "on_minimum_255": t2["on_minimum_255"], "in_ratio": t2["in_ratio"]},
        "rule": t2["rule"],
        "passed": bool(on_outside >= t2["on_over_off_factor"] * off_outside and on_outside >= t2["on_minimum_255"] / 255.0 and on_outside >= t2["in_ratio"] * on_inside),
    }
    t3 = metrics["T3"]
    on_ratio = float((column_contributions(frames["side_on"], frames["side_hidden"], regions) < t3["column_threshold_255"] / 255.0).mean())
    off_ratio = float((column_contributions(frames["side_off"], frames["side_hidden"], regions) < t3["column_threshold_255"] / 255.0).mean())
    result["metrics"]["T3"] = {
        "on_buried_ratio": on_ratio,
        "off_buried_ratio": off_ratio,
        "threshold": {"column_threshold_255": t3["column_threshold_255"], "on_max_ratio": t3["on_max_ratio"], "off_min_ratio": t3["off_min_ratio"]},
        "rule": t3["rule"],
        "passed": bool(on_ratio <= t3["on_max_ratio"] and off_ratio >= t3["off_min_ratio"]),
    }
    floor_mask = side_floor_mask(regions, shape)
    on_off = absolute_mean(frames["side_on"], frames["side_off"], floor_mask)
    on_repeat = absolute_mean(frames["side_on"], frames["side_on_repeat"], floor_mask)
    t4 = metrics["T4"]
    result["metrics"]["T4"] = {
        "on_off_abs_255": on_off * 255.0,
        "on_repeat_abs_255": on_repeat * 255.0,
        "threshold": {"noise_factor": t4["noise_factor"]},
        "rule": t4["rule"],
        "passed": bool(on_off <= t4["noise_factor"] * on_repeat),
    }
    result["records"]["gamecam_on_off_abs_255"] = absolute_mean(frames["gamecam_on"], frames["gamecam_off"]) * 255.0
    result["records"]["in_out_column_counts"] = [int(found["inside"].size), int(found["outside"].size)]
    result["verdict"] = "pass" if all(item["passed"] for item in result["metrics"].values()) else "fail"
    return result


def default_tile_shot_spec(prereg):
    for entry in prereg["tools"]["shot_specs"]:
        if entry.endswith(TILE_SHOT_SPEC_NAME):
            return REPOSITORY_ROOT / entry
    return REPOSITORY_ROOT / "Tools" / "FluidFX" / "quality" / TILE_SHOT_SPEC_NAME


def is_same_pose_value(actual, expected):
    if actual is None:
        return False
    actual_values = np.atleast_1d(np.asarray(actual, dtype=np.float64))
    expected_values = np.atleast_1d(np.asarray(expected, dtype=np.float64))
    return actual_values.shape == expected_values.shape and bool(np.allclose(actual_values, expected_values, rtol=0.0, atol=1.0e-6))


def check_tile_pose(shot_spec_path, prereg):
    path = Path(shot_spec_path)
    if not path.is_file():
        return [f"tile 촬영 명세 없음: {path.as_posix()}"]
    spec = json.loads(path.read_text(encoding="utf-8"))
    base = spec.get("base", {})
    variants = {variant.get("name"): variant for variant in spec.get("variants", [])}
    tile = prereg["tile"]
    problems = []
    for role, name in tile["variants"].items():
        variant = variants.get(name)
        if variant is None:
            problems.append(f"{name}: 촬영 명세에 변형 없음")
            continue
        expected = tile["pose"]["gamecam" if role.startswith("gamecam") else "side"]
        applied = variant.get("apply", {})
        for key in ("pawn", "arm", "view"):
            actual = applied.get(key, base.get(key))
            if not is_same_pose_value(actual, expected[key]):
                problems.append(f"{name}: {key} {actual} 가 prereg {expected[key]} 와 다름")
    return problems


def brighten(frame, factor):
    return (np.clip(frame * factor, 0.0, 1.0) * 255.0).astype(np.uint8)


def tint(image, mask, color, weight=0.5):
    image[mask] = (image[mask] * (1.0 - weight) + np.array(color) * weight).astype(np.uint8)


def draw_overlay(kind, frames, prereg, overlay_path):
    if kind == "relief":
        found, _, _, _ = find_relief_regions(frames, prereg)
        side = found["side_hidden"]
        panel = brighten(frames["side_hidden"], 4.0)
        panel[side_band_mask(with_columns(side, found["excluded"]), panel.shape)] = (255, 220, 0)
        panel[side_band_mask(with_columns(side, found["terrain"]), panel.shape)] = (255, 60, 60)
        tint(panel, side_floor_mask(side, panel.shape), (0, 200, 0))
        panels = [panel, brighten(frames["side_terrain_mask"], 4.0)]
        overhang_panel = brighten(frames["overhang_hidden"], 2.0)
        overhang = found["overhang"]
        if "high_rows" in overhang:
            overhang_panel[overhang_cell_mask(overhang, overhang_panel.shape, overhang["high_rows"])] = (255, 60, 60)
            overhang_panel[overhang_cell_mask(overhang, overhang_panel.shape, overhang["low_rows"])] = (60, 120, 255)
            overhang_panel[overhang_cap_mask(overhang, overhang_panel.shape)] = (255, 0, 255)
        panels.append(overhang_panel)
    else:
        found, _, _ = find_tile_regions(frames, prereg)
        side = found["side_hidden"]
        panel = brighten(frames["side_hidden"], 4.0)
        panel[side_band_mask(with_columns(side, found["inside"]), panel.shape)] = (255, 60, 60)
        panel[side_band_mask(with_columns(side, found["outside"]), panel.shape)] = (60, 120, 255)
        tint(panel, side_floor_mask(side, panel.shape), (0, 200, 0))
        tint(panel, wrap_guard_mask(side, prereg, panel.shape), (255, 0, 255), 0.3)
        panels = [panel]
    Path(overlay_path).parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.concatenate(panels, axis=1)).save(overlay_path)


def write_result(result, output_path):
    if not output_path:
        return
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(result, ensure_ascii=False, indent=1, default=to_json_value) + "\n"
    Path(output_path).write_text(text, encoding="utf-8", newline="\n")


def judge(kind, shots_folder, prereg_path, output_path, overlay_path, shot_spec_path=None):
    prereg, prereg_sha = read_prereg(prereg_path)
    problems = []
    if kind == "tile":
        problems += check_tile_pose(shot_spec_path or default_tile_shot_spec(prereg), prereg)
    files, file_problems = find_shot_files(shots_folder, prereg[kind]["variants"])
    problems += file_problems
    if problems:
        result = {"kind": kind, "valid": False, "verdict": "invalid", "problems": problems}
    else:
        frames = {role: load_frame(path) for role, path in files.items()}
        result = measure_relief(frames, prereg) if kind == "relief" else measure_tile(frames, prereg)
        if overlay_path and len({frame.shape for frame in frames.values()}) == 1:
            draw_overlay(kind, frames, prereg, overlay_path)
            result["overlay"] = Path(overlay_path).as_posix()
    result["prereg"] = Path(prereg_path).as_posix()
    result["prereg_sha256"] = prereg_sha
    result["shots"] = {role: path.as_posix() for role, path in files.items()}
    write_result(result, output_path)
    summary = {"kind": kind, "verdict": result["verdict"], "problems": result["problems"]}
    summary |= {name: item["passed"] for name, item in result.get("guards", {}).items()}
    summary |= {name: item["passed"] for name, item in result.get("metrics", {}).items()}
    print(json.dumps(summary, ensure_ascii=False, default=to_json_value))
    return {"pass": 0, "fail": 1}.get(result["verdict"], 2)


def regions_only(kind, shots_folder, prereg_path, overlay_path):
    prereg, prereg_sha = read_prereg(prereg_path)
    roles = ("side_hidden", "side_terrain_mask", "overhang_hidden") if kind == "relief" else ("side_hidden",)
    files, problems = find_shot_files(shots_folder, {role: prereg[kind]["variants"][role] for role in roles})
    result = {"kind": kind, "prereg": Path(prereg_path).as_posix(), "prereg_sha256": prereg_sha, "problems": problems}
    if not problems:
        frames = {role: load_frame(path) for role, path in files.items()}
        if kind == "relief":
            _, result["regions"], region_problems, terrain_guard = find_relief_regions(frames, prereg)
            result["guards"] = {"terrain_columns": terrain_guard}
            if not terrain_guard["passed"]:
                region_problems.append("지형 열 판별 조건 실패")
        else:
            _, result["regions"], region_problems = find_tile_regions(frames, prereg)
        result["problems"] += region_problems
        draw_overlay(kind, frames, prereg, overlay_path)
        result["overlay"] = Path(overlay_path).as_posix()
    print(json.dumps(result, ensure_ascii=False, default=to_json_value))
    return 0 if not result["problems"] else 2


def side_background(height, width, horizon):
    rows = np.arange(height, dtype=np.float64)[:, None]
    columns = np.arange(width, dtype=np.float64)[None, :]
    sky = 0.004 + 0.01 * rows / horizon
    ground = (0.05 + 0.03 * (rows - horizon) / (height - horizon)) * (1.0 - 0.25 * ((columns - 0.5 * width) / (0.5 * width)) ** 2)
    return np.where(rows < horizon, sky, ground)


def draw_cones(values, cones, hill_base):
    height, width = values.shape
    rows = np.arange(height, dtype=np.float64)[:, None]
    columns = np.arange(width, dtype=np.float64)
    tops = np.full(width, -1.0)
    for apex_column, apex_row, half_width, value in cones:
        outline = apex_row + (hill_base - apex_row) / half_width * np.abs(columns - apex_column)
        inside = np.abs(columns - apex_column) <= half_width
        mask = (rows >= outline[None, :]) & (rows <= hill_base) & inside[None, :]
        values[mask] = value
        tops = np.where(inside & ((tops < 0) | (outline < tops)), outline, tops)
    return tops


def add_ridge_band(values, tops, columns_mask, thickness, amount):
    fogged = values.copy()
    for column in np.flatnonzero(columns_mask & (tops > 0)):
        top = int(np.ceil(tops[column]))
        fogged[max(top - thickness, 0):top, column] += amount
    return fogged


def make_relief_side_scene(height=540, width=960):
    horizon = int(0.42 * height)
    hill_base = horizon + int(0.12 * height)
    hidden = side_background(height, width, horizon)
    tops = draw_cones(hidden, ((0.3 * width, 0.18 * height, 0.15 * width, 0.20), (0.62 * width, 0.08 * height, 0.11 * width, 0.24)), hill_base)
    mask = hidden.copy()
    first, last = int(0.78 * width), int(0.85 * width)
    for column in range(first, last):
        top = int(round(0.24 * height + (column - first) * 0.08 * height / (last - first)))
        hidden[top:top + 6, column] = 0.22
    return hidden, mask, tops, horizon, hill_base


def make_relief_side_frames(height=540, width=960, mask_erase_fraction=0.0, prereg=None):
    hidden, mask, tops, horizon, hill_base = make_relief_side_scene(height, width)
    if mask_erase_fraction > 0.0:
        ridge = find_side_regions(np.repeat(hidden[:, :, None], 3, axis=2), relief_region_values(prereg))["ridge_columns"]
        erased = ridge[:int(np.ceil(mask_erase_fraction * ridge.size))]
        mask[:hill_base + 1, erased] = side_background(height, width, horizon)[:hill_base + 1, erased]
    floor_fog = np.zeros_like(hidden)
    floor_fog[horizon:] = 0.06
    thickness = max(1, int(round(8 * height / 1080)))
    terrain_columns = tops >= 0
    on = add_ridge_band(hidden + floor_fog, tops, terrain_columns, thickness, 0.05)
    off = hidden + floor_fog
    noise = np.random.default_rng(20261111).normal(0.0, 0.002, size=hidden.shape)
    return {"side_hidden": hidden, "side_terrain_mask": mask, "side_on": on, "side_off": off, "side_on_repeat": on + noise}


def make_relief_overhang_frames(low_on=0.06, low_off=0.06):
    hidden, geometry = make_overhang_scene()
    pixels_per_cm = (geometry["pillar_rows"][1] - geometry["pillar_rows"][0] + 1) / 380.0
    bottom = geometry["pillar_rows"][1] + 1
    slits = ((geometry["plate_columns"][0], geometry["pillar_columns"][0] - 1), (geometry["pillar_columns"][1] + 1, geometry["plate_columns"][1]))

    def with_low_cells(amount):
        fogged = hidden.copy()
        for left, right in slits:
            fogged[int(bottom - 60 * pixels_per_cm):bottom + 3, left:right + 1] += amount
        return fogged

    return {"overhang_hidden": hidden, "overhang_off": with_low_cells(low_off), "overhang_on": with_low_cells(low_on), "overhang_allgeom": hidden.copy()}


def make_gamecam_frames(height, width):
    base = np.full((height, width), 0.1)
    return {"gamecam_off": base, "gamecam_on": base + 0.01}


def as_rgb(frames):
    return {role: np.repeat(values[:, :, None], 3, axis=2) for role, values in frames.items()}


def make_relief_frames(prereg, mask_erase_fraction=0.0, low_on=0.06, low_off=0.06):
    side = make_relief_side_frames(mask_erase_fraction=mask_erase_fraction, prereg=prereg)
    return as_rgb(side | make_relief_overhang_frames(low_on, low_off) | make_gamecam_frames(540, 960))


def make_tile_frames(height=540, width=960, in_fog=0.05, out_fog=0.05, floor_fog_from_fraction=0.0, out_half_width_fraction=0.1):
    horizon = int(0.45 * height)
    hill_base = horizon + int(0.10 * height)
    hidden = side_background(height, width, horizon)
    cones = ((0.25 * width, 0.30 * height, out_half_width_fraction * width, 0.22), (0.75 * width, 0.30 * height, 0.1 * width, 0.22))
    tops = draw_cones(hidden, cones, hill_base)
    columns = np.arange(width)
    floor_fog = np.zeros_like(hidden)
    floor_fog[horizon:, columns >= floor_fog_from_fraction * width] = 0.06
    thickness = max(1, int(round(8 * height / 1080)))
    is_inside = columns >= 0.5 * width
    on = add_ridge_band(hidden + floor_fog, tops, is_inside, thickness, in_fog)
    on = add_ridge_band(on, tops, ~is_inside, thickness, out_fog)
    off = hidden + floor_fog
    noise = np.random.default_rng(20261112).normal(0.0, 0.002, size=hidden.shape)
    frames = {"side_hidden": hidden, "side_on": on, "side_off": off, "side_on_repeat": on + noise}
    return as_rgb(frames | make_gamecam_frames(height, width))


def v2_buried_ratio_over_hidden_ridge(frames, prereg):
    regions = find_side_regions(frames["side_hidden"], relief_region_values(prereg))
    threshold = prereg["relief"]["metrics"]["M2"]["column_threshold_255"] / 255.0
    return float((column_contributions(frames["side_on"], frames["side_hidden"], regions) < threshold).mean())


def save_frames(folder, frames, variants):
    for index, (role, frame) in enumerate(frames.items()):
        Image.fromarray((np.clip(frame, 0.0, 1.0) * 255.0).round().astype(np.uint8)).save(folder / f"{index:02d}_{variants[role]}_08000ms.png")


def write_tile_shot_spec(path, prereg, pawn_shift=0.0):
    tile = prereg["tile"]
    variants = []
    for role, name in tile["variants"].items():
        pose = tile["pose"]["gamecam" if role.startswith("gamecam") else "side"]
        variants.append({"name": name, "apply": {"pawn": [pose["pawn"][0] + pawn_shift, pose["pawn"][1]], "arm": pose["arm"], "view": list(pose["view"])}})
    path.write_text(json.dumps({"base": {}, "variants": variants}), encoding="utf-8")


def default_prereg_path():
    return REPOSITORY_ROOT / "Saved" / "AgentOps" / "20261110" / "fog-terrain" / "q41b" / "prereg-v3.json"


def run_self_test():
    prereg_path = default_prereg_path()
    prereg, _ = read_prereg(prereg_path)
    outcomes = []

    def record(label, passed, detail):
        outcomes.append(bool(passed))
        print(("PASS " if passed else "FAIL ") + label + ": " + json.dumps(detail, ensure_ascii=False, default=to_json_value))

    def metric_states(outcome):
        return {name: item["passed"] for name, item in outcome.get("metrics", {}).items()} | {name: item["passed"] for name, item in outcome.get("guards", {}).items()}

    base_relief = measure_relief(make_relief_frames(prereg), prereg)
    record("relief 기준 세트 M1~M5 통과", base_relief["verdict"] == "pass", {"states": metric_states(base_relief), "problems": base_relief["problems"]})

    v2_ratio = v2_buried_ratio_over_hidden_ridge(make_relief_frames(prereg), prereg)
    m2 = base_relief.get("metrics", {}).get("M2", {})
    excluded = base_relief["guards"]["terrain_columns"]["excluded_fraction"]
    record("① 숨김 장에만 있는 평평하지 않은 돌출 능선(안개 0): v3 M2 통과, v2 방식(숨김 능선 열 전체)은 실패", m2.get("passed") is True and v2_ratio > prereg["relief"]["metrics"]["M2"]["on_max_ratio"] and 0.0 < excluded <= 0.25, {"v3_on_buried_ratio": m2.get("on_buried_ratio"), "v2_on_buried_ratio": v2_ratio, "excluded_fraction": excluded})

    erased = measure_relief(make_relief_frames(prereg, mask_erase_fraction=0.30), prereg)
    record("② 마스크가 능선 열 30% 를 지움 → 판정 불가", erased["verdict"] == "invalid" and not erased["guards"]["terrain_columns"]["passed"], erased["guards"]["terrain_columns"])

    both_negative = measure_relief(make_relief_frames(prereg, low_on=-2.0 / 255.0, low_off=-2.0 / 255.0), prereg)
    record("③a M4 낮은 칸 켬·끔 둘 다 −2/255 → 통과", both_negative["verdict"] == "pass" and both_negative["metrics"]["M4"]["passed"], both_negative["metrics"].get("M4"))
    erased_flow = measure_relief(make_relief_frames(prereg, low_on=0.0, low_off=-2.0 / 255.0), prereg)
    record("③b M4 낮은 칸 켬 0(판 밑 흐름 지움) → 실패", erased_flow["verdict"] == "fail" and not erased_flow["metrics"]["M4"]["passed"], erased_flow["metrics"].get("M4"))
    faint = measure_relief(make_relief_frames(prereg, low_on=-0.3 / 255.0, low_off=-0.3 / 255.0), prereg)
    record("③c M4 낮은 칸 |끔 − 숨김| 0.3/255 → 판정 불가", faint["verdict"] == "invalid" and not faint["guards"]["M4_low_visible"]["passed"], faint["guards"].get("M4_low_visible"))

    base_tile = measure_tile(make_tile_frames(), prereg)
    record("④a tile 두 언덕 안개(IN 오른쪽 반·OUT 왼쪽 반) → 통과", base_tile["verdict"] == "pass", {"states": metric_states(base_tile), "regions": {key: base_tile["regions"].get(key) for key in ("in_column_count", "out_column_count")}, "problems": base_tile["problems"]})
    out_buried = measure_tile(make_tile_frames(out_fog=0.0), prereg)
    record("④b 왼쪽(OUT) 언덕 묻힘 → T2 실패", out_buried["verdict"] == "fail" and not out_buried["metrics"]["T2"]["passed"] and out_buried["metrics"]["T1"]["passed"], metric_states(out_buried))
    in_buried = measure_tile(make_tile_frames(in_fog=0.0), prereg)
    record("④c 오른쪽(IN) 언덕 묻힘 → T1 실패", in_buried["verdict"] == "fail" and not in_buried["metrics"]["T1"]["passed"], metric_states(in_buried))

    no_wrap = measure_tile(make_tile_frames(floor_fog_from_fraction=0.75), prereg)
    record("⑤ 왼쪽 1/4 평지에 안개 없음(오른쪽 1/4 에만) → 판정 불가", no_wrap["verdict"] == "invalid" and not no_wrap["guards"]["wrap_visible"]["passed"], no_wrap["guards"].get("wrap_visible"))

    thin_side = measure_tile(make_tile_frames(out_half_width_fraction=0.015), prereg)
    record("⑥ 한쪽(OUT) 능선 열 < 60 → 판정 불가", thin_side["verdict"] == "invalid" and any("OUT" in problem for problem in thin_side["problems"]), {"problems": thin_side["problems"], "out": thin_side["regions"].get("out_column_count")})

    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        tile_shots = work / "tile"
        tile_shots.mkdir()
        save_frames(tile_shots, make_tile_frames(), prereg["tile"]["variants"])
        spec_path = work / "tile_spec.json"
        write_tile_shot_spec(spec_path, prereg)
        first_code = judge("tile", tile_shots, prereg_path, work / "first.json", work / "overlay.png", spec_path)
        second_code = judge("tile", tile_shots, prereg_path, work / "second.json", work / "overlay.png", spec_path)
        first_sha = hashlib.sha256((work / "first.json").read_bytes()).hexdigest()
        second_sha = hashlib.sha256((work / "second.json").read_bytes()).hexdigest()
        recorded_sha = json.loads((work / "first.json").read_text(encoding="utf-8"))["prereg_sha256"]
        record("⑦ tile 파일 판정 두 번 실행 → 판정 JSON sha256 동일, 종료 0, prereg sha256 기록", first_code == 0 and second_code == 0 and first_sha == second_sha and recorded_sha == hashlib.sha256(prereg_path.read_bytes()).hexdigest(), {"exit": [first_code, second_code], "sha256": [first_sha[:12], second_sha[:12]]})

        moved_spec = work / "tile_spec_moved.json"
        write_tile_shot_spec(moved_spec, prereg, pawn_shift=-500.0)
        moved_code = judge("tile", tile_shots, prereg_path, work / "moved.json", None, moved_spec)
        moved = json.loads((work / "moved.json").read_text(encoding="utf-8"))
        record("⑧ 촬영 명세 자세가 prereg 와 다름(폰 X −500) → 판정 불가 종료 2", moved_code == 2 and moved["verdict"] == "invalid", {"exit": moved_code, "problems": moved["problems"][:2]})

        relief_shots = work / "relief"
        relief_shots.mkdir()
        save_frames(relief_shots, make_relief_frames(prereg), prereg["relief"]["variants"])
        relief_code = judge("relief", relief_shots, prereg_path, work / "relief.json", work / "relief.png")
        record("relief 파일 판정 경로(이름 규칙·PNG 양자화) → 종료 0", relief_code == 0, {"exit": relief_code})
        next(relief_shots.glob("*_relief_side_terrain_mask_08000ms.png")).unlink()
        record("relief 장 하나 없음 → 종료 2", judge("relief", relief_shots, prereg_path, None, None) == 2, {})
    print(json.dumps({"passed": sum(outcomes), "failed": len(outcomes) - sum(outcomes)}, ensure_ascii=False))
    return 0 if all(outcomes) else 1


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if "--self-test" in sys.argv[1:]:
        return run_self_test()
    parser = argparse.ArgumentParser(description="Q4-1b 지형 따라가기 안개 화면 판정 v3(prereg-v3: relief M1~M5, tile T1~T4)")
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("judge-relief", "judge-tile"):
        command = commands.add_parser(name)
        command.add_argument("--shots", required=True)
        command.add_argument("--prereg", required=True)
        command.add_argument("--output", required=True)
        command.add_argument("--overlay")
        if name == "judge-tile":
            command.add_argument("--shot-spec")
    regions_parser = commands.add_parser("regions")
    regions_parser.add_argument("--kind", choices=("relief", "tile"), required=True)
    regions_parser.add_argument("--shots", required=True)
    regions_parser.add_argument("--prereg", required=True)
    regions_parser.add_argument("--overlay", required=True)
    arguments = parser.parse_args()
    if arguments.command == "regions":
        return regions_only(arguments.kind, arguments.shots, arguments.prereg, arguments.overlay)
    if arguments.command == "judge-tile":
        return judge("tile", arguments.shots, arguments.prereg, arguments.output, arguments.overlay, arguments.shot_spec)
    return judge("relief", arguments.shots, arguments.prereg, arguments.output, arguments.overlay)


if __name__ == "__main__":
    sys.exit(main())
