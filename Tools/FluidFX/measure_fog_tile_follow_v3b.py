"""실행 환경: 시스템 Python 3(numpy·Pillow), 에디터 불필요(PowerShell 또는 Git Bash). v2 measure_fog_terrain_follow.py·v3 measure_fog_terrain_follow_v3.py 의 영역·평균 함수를 가져다 쓰고 두 파일은 고치지 않는다.
실행 명령: python -B Tools/FluidFX/measure_fog_tile_follow_v3b.py judge --shots <촬영 폴더> --prereg <prereg-v3b.json> --output <판정 JSON> [--overlay <PNG>] [--shot-spec <촬영 명세, 기본 prereg tile.shot_spec>] | --self-test
출력: 판정 JSON{kind tile-v3b, prereg 경로·sha256, shots(역할별 장 목록), regions(EV100 0 영역 장의 능선·IN·OUT 열·평지 행), guards(saturation·off_equals_canonical·wrap_visible), noise_floor_255, metrics T1~T4{값, 문턱, checks, passed}, records, valid, verdict pass|fail|invalid, verdict_reason}과 오버레이 PNG(영역·켬 기여 x8·끔 기여 x8). 촬영 폴더의 result.json(명세 해시·key_errors·역할별 tuning_reload 오류·사본 경로·reinit·state_reset·대상 액터 다시 읽기 값)이 prereg 와 다르면 판정 불가, 잡음 배수(on_over_noise)만 못 넘은 지표가 있으면 판정 불가. 종료 0 통과·1 실패·2 판정 불가.
상태: 미검증(2026-11-11 Q4-1b tile v3b r2, --self-test 합성 fixture 만, 실제 v3b 촬영 전). 문턱·영역·장 조건·다시 읽기 기대값·잠금 파일은 모두 prereg JSON 에서 읽는다(코드 안 문턱 상수 없음).
"""
import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

from measure_fog_terrain_follow import absolute_mean, column_contributions, load_frame, luminance, scaled_pixels, side_band_mask, side_floor_mask, signed_mean
from measure_fog_terrain_follow_v3 import brighten, find_tile_regions, is_same_pose_value, make_tile_frames, read_prereg, tint, to_json_value, with_columns

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SHOT_NAME_PATTERN = re.compile(r"^\d+_(?P<name>.+)_(?P<milliseconds>\d{5})ms\.png$")


def check_locked_files(prereg):
    problems = []
    for path, expected in prereg.get("locked_files", {}).items():
        file_path = REPOSITORY_ROOT / path
        if not file_path.is_file():
            problems.append(f"잠금 파일 없음: {path}")
            continue
        if hashlib.sha256(file_path.read_bytes()).hexdigest() != expected:
            problems.append(f"잠금 파일 해시 다름: {path}")
    return problems


def check_shot_spec(spec_path, prereg):
    path = Path(spec_path)
    if not path.is_file():
        return [f"촬영 명세 없음: {path.as_posix()}"]
    spec = json.loads(path.read_text(encoding="utf-8"))
    base = spec.get("base", {})
    variants = {variant.get("name"): variant.get("apply", {}) for variant in spec.get("variants", [])}
    tile = prereg["tile"]
    target = tile["target_label"]
    problems = []
    for role, condition in tile["shot_conditions"].items():
        name = tile["variants"][role]
        apply = variants.get(name)
        if apply is None:
            problems.append(f"{name}: 촬영 명세에 변형 없음")
            continue
        pose = tile["pose"][condition["pose"]]
        for key in ("pawn", "arm", "view"):
            actual = apply.get(key, base.get(key))
            if not is_same_pose_value(actual, pose[key]):
                problems.append(f"{name}: {key} {actual} 가 prereg {pose[key]} 와 다름")
        reload = apply.get("tuning_reload", {})
        observed = {
            "ev100": apply.get("exposure_ev100"),
            "exposure_manual": apply.get("exposure_manual"),
            "times_ms": [int(round(float(value) * 1000.0)) for value in apply.get("schedule", {}).get("times", [])],
            "tuning": reload.get("tuning"),
            "reset": reload.get("reset"),
            "hidden": bool(apply.get("hide", {}).get(target, False)),
            "awake": sorted(label for label, is_awake in apply.get("awake", {}).items() if is_awake),
            "read_back": reload.get("read_back"),
        }
        expected = {
            "ev100": condition["ev100"],
            "exposure_manual": tile["exposure"]["manual"],
            "times_ms": condition["times_ms"],
            "tuning": condition["tuning"],
            "reset": tile["reset"],
            "hidden": condition["hidden"],
            "awake": [target],
            "read_back": tile["read_back"]["requests"],
        }
        problems += [f"{name}: {key} {observed[key]} 가 prereg {value} 와 다름" for key, value in expected.items() if observed[key] != value]
    return problems


def normalized_value(value):
    return str(value).split(".")[-1].replace("_", "").upper()


def resolved_tuning_path(tuning):
    return (REPOSITORY_ROOT / tuning).resolve().as_posix() if tuning else ""


def check_read_back(entries, expected, prereg):
    tile = prereg["tile"]
    read_back = tile["read_back"]
    requests = read_back["requests"]
    if not isinstance(entries, list) or len(entries) != len(requests):
        return [f"다시 읽기 {entries} 가 요청 {len(requests)}개와 다름"]
    problems = []
    for request, entry in zip(requests, entries):
        field = request["field"]
        if not isinstance(entry, dict) or (entry.get("struct"), entry.get("field")) != (request["struct"], field):
            problems.append(f"{field}: 다시 읽기 항목 {entry} 가 요청과 다름")
            continue
        read = entry.get("actors", {}).get(tile["target_label"])
        if not isinstance(read, dict):
            problems.append(f"{field}: {tile['target_label']} 다시 읽기 없음")
            continue
        value = read.get("value")
        if "error" in read:
            absent = read_back["absent_owner"]
            if read["error"] != absent["error"] or field not in absent["values"]:
                problems.append(f"{field}: 다시 읽기 오류 {read['error']}")
                continue
            value = absent["values"][field]
        if value is None:
            problems.append(f"{field}: 다시 읽은 값 없음")
        elif normalized_value(value) != normalized_value(expected[field]):
            problems.append(f"{field}: 다시 읽은 값 {value} 가 기대 {expected[field]} 와 다름")
    return problems


def check_reload_outcome(reload_outcome, condition, prereg):
    tile = prereg["tile"]
    if not isinstance(reload_outcome, dict):
        return ["tuning_reload 결과 없음"]
    if "error" in reload_outcome:
        return [f"tuning_reload 오류(이전 표가 남고 재초기화를 건너뛰었을 수 있음): {reload_outcome['error']}"]
    problems = []
    if reload_outcome.get("tuning") != resolved_tuning_path(condition["tuning"]):
        problems.append(f"적재한 사본 {reload_outcome.get('tuning')} 가 prereg {condition['tuning']} 와 다름")
    if reload_outcome.get("reset") != tile["reset"] or "reinit" not in reload_outcome:
        problems.append(f"재초기화 결과 없음(reset {reload_outcome.get('reset')})")
    state_reset = reload_outcome.get("state_reset")
    if not isinstance(state_reset, dict) or state_reset.get("errors"):
        problems.append(f"상태 초기화 결과 없음 또는 오류: {state_reset}")
    expected = tile["read_back"]["expected_by_tuning"][condition["tuning"]]
    return problems + check_read_back(reload_outcome.get("read_back"), expected, prereg)


def check_capture_result(shots_folder, spec_path, prereg):
    tile = prereg["tile"]
    path = Path(shots_folder) / tile["capture_result"]["file"]
    if not path.is_file():
        return [f"촬영 결과 없음: {path.as_posix()}"]
    capture = json.loads(path.read_text(encoding="utf-8"))
    problems = []
    spec_sha = hashlib.sha256(Path(spec_path).read_bytes()).hexdigest() if Path(spec_path).is_file() else None
    if capture.get("variants_sha256") != spec_sha:
        problems.append(f"촬영 결과의 명세 해시 {capture.get('variants_sha256')} 가 촬영 명세 {spec_sha} 와 다름")
    key_errors = capture.get("key_errors")
    if not isinstance(key_errors, list):
        problems.append("촬영 결과에 key_errors 없음")
    else:
        problems += [f"촬영 키 오류: {line}" for line in key_errors]
    entries = {entry.get("name"): entry for entry in capture.get("variants", []) if isinstance(entry, dict)}
    for role, condition in tile["shot_conditions"].items():
        name = tile["variants"][role]
        entry = entries.get(name)
        if entry is None:
            problems.append(f"{name}: 촬영 결과에 변형 없음")
            continue
        reload_outcome = (entry.get("apply") or {}).get("tuning_reload")
        problems += [f"{name}: {line}" for line in check_reload_outcome(reload_outcome, condition, prereg)]
    return problems


def find_stacks(folder, prereg):
    tile = prereg["tile"]
    names = {}
    for path in sorted(Path(folder).glob("*.png")):
        match = SHOT_NAME_PATTERN.match(path.name)
        if match:
            names.setdefault(match.group("name"), {}).setdefault(int(match.group("milliseconds")), []).append(path)
    stacks, problems = {}, []
    for role, condition in tile["shot_conditions"].items():
        name = tile["variants"][role]
        found = names.get(name, {})
        expected = condition["times_ms"]
        if sorted(found) != sorted(expected) or any(len(paths) != 1 for paths in found.values()):
            problems.append(f"{name}: 장 시각 {sorted(found)} 가 prereg {expected}(시각마다 1장)와 다름")
            continue
        stacks[role] = [found[milliseconds][0] for milliseconds in expected]
    return stacks, problems


def average_stacks(stacks, level_255):
    averages, saturated = {}, {}
    for role, frames in stacks.items():
        averages[role] = np.mean(frames, axis=0)
        saturated[role] = np.any([(frame >= level_255 / 255.0 - 1.0e-9).any(axis=2) for frame in frames], axis=0)
    return averages, saturated


def graze_mask(regions, prereg, shape):
    wrap = prereg["tile"]["guards"]["wrap_visible"]
    scale = shape[0] / prereg["reference_height_px"]
    row_guard = scaled_pixels(wrap["row_guard_px"], scale)
    exclusion = scaled_pixels(wrap["ridge_exclusion_px"], scale)
    ridge = np.asarray(regions["ridge_columns"])
    floor_top = regions["floor_rows"][0]
    mask = np.zeros(shape[:2], dtype=bool)
    for column in range(regions["border_px"], int(wrap["wrap_columns_below_fraction"] * shape[1])):
        if ridge.size and np.abs(ridge - column).min() <= exclusion:
            continue
        top = int(regions["top_rows"][column])
        if top >= 0:
            mask[top + row_guard:floor_top, column] = True
    return mask


def invalid(result, reason):
    return result | {"valid": False, "verdict": "invalid", "verdict_reason": reason}, None


def band_metric(rule, on_value, off_value, noise_value, extra_checks=None):
    checks = {
        "on_over_off": bool(on_value >= rule["on_over_off_factor"] * off_value),
        "on_minimum": bool(on_value >= rule["on_minimum_255"] / 255.0),
        "on_over_noise": bool(on_value >= rule["noise_factor"] * noise_value),
    } | (extra_checks or {})
    return {
        "on_minus_hidden_255": on_value * 255.0,
        "off_minus_hidden_255": off_value * 255.0,
        "band_noise_255": noise_value * 255.0,
        "threshold": {key: value for key, value in rule.items() if key not in ("rule", "band_px")},
        "checks": checks,
        "rule": rule["rule"],
        "passed": all(checks.values()),
    }


def measure_tile_v3b(stacks, prereg):
    tile = prereg["tile"]
    guards = tile["guards"]
    metrics = tile["metrics"]
    result = {"kind": "tile-v3b", "valid": True, "problems": [], "guards": {}, "regions": {}, "metrics": {}, "records": {}}
    averages, saturated = average_stacks(stacks, guards["saturation"]["level_255"])
    shapes = sorted({frame.shape for frame in averages.values()})
    if len(shapes) != 1:
        return invalid(result, f"장 크기 다름 {shapes}")
    shape = shapes[0]
    found, result["regions"], problems = find_tile_regions({"side_hidden": averages["regions_hidden"]}, prereg)
    if problems:
        result["problems"] = problems
        return invalid(result, "영역 찾기 실패")
    regions = found["side_hidden"]
    raw_masks = {
        "band_in": side_band_mask(with_columns(regions, found["inside"]), shape),
        "band_out": side_band_mask(with_columns(regions, found["outside"]), shape),
        "floor": side_floor_mask(regions, shape),
        "graze": graze_mask(regions, prereg, shape),
    }
    usable = ~np.any([saturated[role] for role in guards["saturation"]["roles"]], axis=0)
    lost = {name: float(1.0 - (mask & usable).sum() / mask.sum()) for name, mask in raw_masks.items() if mask.any()}
    saturation_passed = all(value <= guards["saturation"]["max_fraction"] for value in lost.values())
    result["guards"]["saturation"] = {"lost_fraction": lost, "level_255": guards["saturation"]["level_255"], "max_fraction": guards["saturation"]["max_fraction"], "passed": saturation_passed}
    masks = {name: mask & usable for name, mask in raw_masks.items()}
    result["regions"]["mask_pixels"] = {name: int(mask.sum()) for name, mask in masks.items()}

    def noise(mask):
        return absolute_mean(averages["side_on"], averages["side_on_repeat"], mask)

    def contribution(role, mask):
        return signed_mean(averages[role], averages["side_hidden"], mask)

    union = masks["band_in"] | masks["band_out"] | masks["floor"] | masks["graze"]
    canonical = guards["off_equals_canonical"]
    canonical_gap = absolute_mean(averages["side_off"], averages["side_canonical"], union)
    canonical_noise = noise(union)
    canonical_passed = bool(canonical_gap <= canonical["noise_factor"] * canonical_noise)
    result["guards"]["off_equals_canonical"] = {"off_canonical_abs_255": canonical_gap * 255.0, "on_repeat_abs_255": canonical_noise * 255.0, "noise_factor": canonical["noise_factor"], "rule": canonical["rule"], "passed": canonical_passed}
    result["noise_floor_255"] = noise(masks["floor"]) * 255.0
    if not saturation_passed:
        return invalid(result, "포화 화소가 측정 영역을 너무 많이 지움")
    if not canonical_passed:
        return invalid(result, "끔 사본이 정본과 다르게 보임")

    wrap = guards["wrap_visible"]
    wrap_value = contribution("side_off", masks["graze"]) if masks["graze"].any() else 0.0
    wrap_passed = bool(abs(wrap_value) >= wrap["minimum_255"] / 255.0)
    result["guards"]["wrap_visible"] = {"abs_off_minus_hidden_255": abs(wrap_value) * 255.0, "minimum_255": wrap["minimum_255"], "rule": wrap["rule"], "gates": wrap["gates"], "passed": wrap_passed}

    on_in, off_in = contribution("side_on", masks["band_in"]), contribution("side_off", masks["band_in"])
    on_out, off_out = contribution("side_on", masks["band_out"]), contribution("side_off", masks["band_out"])
    result["metrics"]["T1"] = band_metric(metrics["T1"], on_in, off_in, noise(masks["band_in"]))
    result["metrics"]["T2"] = band_metric(metrics["T2"], on_out, off_out, noise(masks["band_out"]), {"in_ratio": bool(on_out >= metrics["T2"]["in_ratio"] * on_in)})
    result["metrics"]["T2"]["in_on_minus_hidden_255"] = on_in * 255.0
    t3 = metrics["T3"]
    on_ratio = float((column_contributions(averages["side_on"], averages["side_hidden"], regions) < t3["column_threshold_255"] / 255.0).mean())
    off_ratio = float((column_contributions(averages["side_off"], averages["side_hidden"], regions) < t3["column_threshold_255"] / 255.0).mean())
    result["metrics"]["T3"] = {
        "on_buried_ratio": on_ratio,
        "off_buried_ratio": off_ratio,
        "threshold": {key: value for key, value in t3.items() if key != "rule"},
        "rule": t3["rule"],
        "passed": bool(on_ratio <= t3["on_max_ratio"] and off_ratio >= t3["off_min_ratio"]),
    }
    t4 = metrics["T4"]
    on_off = absolute_mean(averages["side_on"], averages["side_off"], masks["floor"])
    on_repeat = noise(masks["floor"])
    result["metrics"]["T4"] = {"on_off_abs_255": on_off * 255.0, "on_repeat_abs_255": on_repeat * 255.0, "threshold": {"noise_factor": t4["noise_factor"]}, "rule": t4["rule"], "passed": bool(on_off <= t4["noise_factor"] * on_repeat)}

    result["records"] = {
        "gamecam_on_off_abs_255": absolute_mean(averages["gamecam_on"], averages["gamecam_off"]) * 255.0,
        "frame_counts": {role: len(frames) for role, frames in stacks.items()},
        "in_out_column_counts": [int(found["inside"].size), int(found["outside"].size)],
    }
    failed = [name for name, item in result["metrics"].items() if not item["passed"]]
    noise_only = [name for name in failed if {check for check, ok in result["metrics"][name].get("checks", {}).items() if not ok} == {"on_over_noise"}]
    if not failed:
        result |= {"verdict": "pass", "verdict_reason": "T1~T4 모두 통과"}
    elif noise_only:
        result |= {"valid": False, "verdict": "invalid", "verdict_reason": f"{failed} 실패 중 {noise_only} 는 잡음 배수만 못 넘음, 신호가 반복 잡음과 구분되지 않아 판정 불가"}
    elif not wrap_passed:
        result |= {"valid": False, "verdict": "invalid", "verdict_reason": f"{failed} 실패, 감싸기 안개가 비스듬한 평지 행에서 보이지 않아 따라가기 결함과 구분 불가"}
    else:
        result |= {"verdict": "fail", "verdict_reason": f"{failed} 실패, 감싸기 안개는 보임"}
    return result, {"averages": averages, "found": found, "masks": masks}


def draw_overlay(context, overlay_path):
    averages, found, masks = context["averages"], context["found"], context["masks"]
    side = found["side_hidden"]
    shape = averages["regions_hidden"].shape
    panel = brighten(averages["regions_hidden"], 4.0)
    panel[side_band_mask(with_columns(side, found["inside"]), shape)] = (255, 60, 60)
    panel[side_band_mask(with_columns(side, found["outside"]), shape)] = (60, 120, 255)
    tint(panel, side_floor_mask(side, shape), (0, 200, 0))
    tint(panel, masks["graze"], (255, 0, 255), 0.4)
    panels = [panel]
    for role in ("side_on", "side_off"):
        contribution = np.clip((luminance(averages[role]) - luminance(averages["side_hidden"])) * 8.0, 0.0, 1.0)
        panels.append((np.repeat(contribution[:, :, None], 3, axis=2) * 255.0).astype(np.uint8))
    Path(overlay_path).parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.concatenate(panels, axis=1)).save(overlay_path)


def write_result(result, output_path):
    if not output_path:
        return
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    Path(output_path).write_text(json.dumps(result, ensure_ascii=False, indent=1, default=to_json_value) + "\n", encoding="utf-8", newline="\n")


def judge(shots_folder, prereg_path, output_path, overlay_path=None, shot_spec_path=None):
    prereg, prereg_sha = read_prereg(prereg_path)
    problems = check_locked_files(prereg)
    spec_path = shot_spec_path or REPOSITORY_ROOT / prereg["tile"]["shot_spec"]
    problems += check_shot_spec(spec_path, prereg)
    problems += check_capture_result(shots_folder, spec_path, prereg)
    files, file_problems = find_stacks(shots_folder, prereg)
    problems += file_problems
    context = None
    if problems:
        result = {"kind": "tile-v3b", "valid": False, "verdict": "invalid", "verdict_reason": "장·명세·잠금·촬영 결과 확인 실패", "problems": problems}
    else:
        result, context = measure_tile_v3b({role: [load_frame(path) for path in paths] for role, paths in files.items()}, prereg)
    if overlay_path and context is not None:
        draw_overlay(context, overlay_path)
        result["overlay"] = Path(overlay_path).as_posix()
    result["prereg"] = Path(prereg_path).as_posix()
    result["prereg_sha256"] = prereg_sha
    result["shots"] = {role: [path.as_posix() for path in paths] for role, paths in files.items()}
    write_result(result, output_path)
    summary = {"verdict": result["verdict"], "reason": result.get("verdict_reason"), "problems": result.get("problems", [])}
    summary |= {name: item["passed"] for name, item in result.get("guards", {}).items()}
    summary |= {name: item["passed"] for name, item in result.get("metrics", {}).items()}
    print(json.dumps(summary, ensure_ascii=False, default=to_json_value))
    return {"pass": 0, "fail": 1}.get(result["verdict"], 2)


def default_prereg_path():
    return REPOSITORY_ROOT / "Saved" / "AgentOps" / "20261110" / "fog-terrain" / "tile-v3b" / "prereg-v3b.json"


def make_stacks(prereg, in_fog=0.05, out_fog=0.05, floor_fog_from_fraction=0.0, repeat_sigma=0.002, canonical_offset=0.0, saturated_bottom_fraction=0.0):
    frames = make_tile_frames(in_fog=in_fog, out_fog=out_fog, floor_fog_from_fraction=floor_fog_from_fraction)
    sources = {
        "regions_hidden": frames["side_hidden"], "side_hidden": frames["side_hidden"], "side_off": frames["side_off"], "side_on": frames["side_on"],
        "side_on_repeat": frames["side_on"], "side_canonical": frames["side_off"] + canonical_offset, "gamecam_off": frames["gamecam_off"], "gamecam_on": frames["gamecam_on"],
    }
    tile = prereg["tile"]
    generator = np.random.default_rng(20261111)
    stacks = {}
    for role, condition in tile["shot_conditions"].items():
        sigma = repeat_sigma if role == "side_on_repeat" else (0.002 if len(condition["times_ms"]) > 1 else 0.0)
        series = []
        for _ in condition["times_ms"]:
            frame = np.clip(sources[role] + generator.normal(0.0, sigma, size=sources[role].shape[:2])[:, :, None], 0.0, 1.0)
            if saturated_bottom_fraction > 0.0 and role in tile["guards"]["saturation"]["roles"]:
                frame[int((1.0 - saturated_bottom_fraction) * frame.shape[0]):] = 1.0
            series.append(frame)
        stacks[role] = series
    return stacks


def save_stacks(folder, stacks, prereg):
    tile = prereg["tile"]
    for index, (role, frames) in enumerate(stacks.items()):
        for frame, milliseconds in zip(frames, tile["shot_conditions"][role]["times_ms"]):
            Image.fromarray((np.clip(frame, 0.0, 1.0) * 255.0).round().astype(np.uint8)).save(Path(folder) / f"{index:02d}_{tile['variants'][role]}_{milliseconds:05d}ms.png")


def write_shot_spec(path, prereg, overrides=None):
    tile = prereg["tile"]
    variants = []
    for role, condition in tile["shot_conditions"].items():
        pose = tile["pose"][condition["pose"]]
        apply = {
            "pawn": list(pose["pawn"]), "arm": pose["arm"], "view": list(pose["view"]),
            "exposure_ev100": condition["ev100"], "exposure_manual": tile["exposure"]["manual"],
            "schedule": {"times": [value / 1000.0 for value in condition["times_ms"]]},
            "tuning_reload": {"tuning": condition["tuning"], "reset": tile["reset"], "read_back": tile["read_back"]["requests"]},
            "hide": {tile["target_label"]: condition["hidden"]}, "awake": {tile["target_label"]: True, "TDGen_FogRoom": False},
        }
        apply.update((overrides or {}).get(role, {}))
        variants.append({"name": tile["variants"][role], "apply": apply})
    Path(path).write_text(json.dumps({"base": {}, "variants": variants}), encoding="utf-8")


def engine_style_value(value):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", value).upper() if isinstance(value, str) else value


def make_capture_result(prereg, spec_path, reads=None, reload_errors=None, key_errors=None):
    tile = prereg["tile"]
    requests = tile["read_back"]["requests"]
    variants = []
    for role, condition in tile["shot_conditions"].items():
        expected = tile["read_back"]["expected_by_tuning"][condition["tuning"]]
        overrides = (reads or {}).get(role, {})
        entries = [{"struct": request["struct"], "field": request["field"], "actors": {tile["target_label"]: overrides.get(request["field"], {"value": engine_style_value(expected[request["field"]]), "kind": "FOGTILESIM"})}} for request in requests]
        reload_outcome = {"reset": tile["reset"], "tuning": resolved_tuning_path(condition["tuning"]), "look": ""}
        if role in (reload_errors or {}):
            reload_outcome["error"] = reload_errors[role]
        else:
            reload_outcome |= {"reinit": {}, "read_back": entries, "state_reset": {"actors": [tile["target_label"]], "errors": []}}
        variants.append({"name": tile["variants"][role], "apply": {"tuning_reload": reload_outcome}})
    return {"variants_sha256": hashlib.sha256(Path(spec_path).read_bytes()).hexdigest(), "variants": variants, "key_errors": list(key_errors or [])}


def run_self_test():
    prereg, _ = read_prereg(default_prereg_path())
    unlocked = dict(prereg, locked_files={})
    outcomes = []

    def record(label, passed, detail):
        outcomes.append({"case": label, "passed": bool(passed), "detail": detail})

    def measured(**options):
        result, _ = measure_tile_v3b(make_stacks(unlocked, **options), unlocked)
        return result

    def states(result):
        return {"verdict": result["verdict"]} | {name: item["passed"] for name, item in result.get("metrics", {}).items()} | {f"guard_{name}": item["passed"] for name, item in result.get("guards", {}).items()}

    result = measured()
    record("① 두 언덕 안개·평지 안개 → 통과", result["verdict"] == "pass", states(result))
    result = measured(out_fog=0.0)
    record("② OUT 묻힘(평지 안개 있음) → 실패, T2 실패·T1 통과", result["verdict"] == "fail" and not result["metrics"]["T2"]["passed"] and result["metrics"]["T1"]["passed"], states(result))
    result = measured(in_fog=0.0)
    record("③ IN 묻힘 → 실패, T1 실패", result["verdict"] == "fail" and not result["metrics"]["T1"]["passed"], states(result))
    result = measured(out_fog=0.0, floor_fog_from_fraction=0.75)
    record("④ OUT 묻힘 + 왼쪽 1/4 평지 안개 없음 → 판정 불가", result["verdict"] == "invalid" and not result["guards"]["wrap_visible"]["passed"], states(result))
    result = measured(floor_fog_from_fraction=0.75)
    record("⑤ 두 언덕 통과 + 왼쪽 1/4 평지 안개 없음 → 통과(판별 조건은 실패만 가름)", result["verdict"] == "pass" and not result["guards"]["wrap_visible"]["passed"], states(result))
    result = measured(saturated_bottom_fraction=0.1)
    record("⑥ 아래 10% 포화 → 판정 불가", result["verdict"] == "invalid" and not result["guards"]["saturation"]["passed"], states(result))
    result = measured(canonical_offset=0.02)
    record("⑦ 끔 사본 ≠ 정본 → 판정 불가", result["verdict"] == "invalid" and not result["guards"]["off_equals_canonical"]["passed"], states(result))
    result = measured(repeat_sigma=0.08)
    record("⑧ 반복 잡음 큼 → 판정 불가(T1 잡음 배수만 실패)", result["verdict"] == "invalid" and not result["metrics"]["T1"]["checks"]["on_over_noise"] and result["metrics"]["T1"]["checks"]["on_minimum"], states(result))
    result = measured(out_fog=0.0, repeat_sigma=0.08)
    record("⑧b OUT 묻힘 + 반복 잡음 큼 → 판정 불가(잡음 배수만 실패한 T1 이 있으면 실패로 내지 않음)", result["verdict"] == "invalid" and not result["metrics"]["T2"]["checks"]["on_minimum"], states(result))

    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary)
        shots = folder / "shots"
        shots.mkdir()
        save_stacks(shots, make_stacks(unlocked), unlocked)
        prereg_path = folder / "prereg.json"
        prereg_path.write_text(json.dumps(unlocked), encoding="utf-8")
        spec_path = folder / "spec.json"
        write_shot_spec(spec_path, unlocked)
        capture_path = shots / unlocked["tile"]["capture_result"]["file"]
        capture_path.write_text(json.dumps(make_capture_result(unlocked, spec_path)), encoding="utf-8")

        def judged_with(label, capture):
            if capture is None:
                capture_path.unlink()
            else:
                capture_path.write_text(json.dumps(capture), encoding="utf-8")
            exit_code = judge(shots, prereg_path, folder / f"{label}.json", None, spec_path)
            return exit_code, json.loads((folder / f"{label}.json").read_text(encoding="utf-8")).get("problems", [])

        first, second = folder / "a.json", folder / "b.json"
        code = judge(shots, prereg_path, first, folder / "overlay.png", spec_path)
        judge(shots, prereg_path, second, None, spec_path)
        digests = [hashlib.sha256(path.read_bytes()).hexdigest() for path in (first, second)]
        record("⑨ PNG 왕복 통과 + 두 번 실행 판정 JSON 같음(오버레이 제외)", code == 0 and json.loads(first.read_text(encoding="utf-8"))["verdict"] == "pass", {"exit": code})
        first_result = json.loads(first.read_text(encoding="utf-8"))
        first_result.pop("overlay", None)
        record("⑨b 판정 결정론", json.dumps(first_result, sort_keys=True) == json.dumps(json.loads(second.read_text(encoding="utf-8")), sort_keys=True), digests)
        mismatched = folder / "spec-mismatch.json"
        write_shot_spec(mismatched, unlocked, {"side_on": {"exposure_ev100": -4.0}})
        code = judge(shots, prereg_path, folder / "c.json", None, mismatched)
        record("⑩ 촬영 명세 노출이 prereg 와 다름 → 판정 불가", code == 2, {"exit": code})
        locked_path = folder / "prereg-locked.json"
        locked_path.write_text(json.dumps(dict(unlocked, locked_files={str(spec_path): "0" * 64})), encoding="utf-8")
        code = judge(shots, locked_path, folder / "d.json", None, spec_path)
        record("⑪ 잠금 파일 해시 다름 → 판정 불가", code == 2, {"exit": code})
        valid_capture = make_capture_result(unlocked, spec_path)
        absent_driver = {"error": unlocked["tile"]["read_back"]["absent_owner"]["error"], "kind": "FOGTILESIM"}
        cases = [
            ("15. 촬영 결과 result.json 없음 → 판정 불가", None, 2),
            ("16. 촬영 키 오류 있음 → 판정 불가", dict(valid_capture, key_errors=["03: tuning_reload: 실효 값 다시 읽기: 시간 초과"]), 2),
            ("17. 켬 장 다시 읽기 따라가기 False → 판정 불가", make_capture_result(unlocked, spec_path, reads={"side_on": {"bFogFloorFollowsTerrain": {"value": False}}}), 2),
            ("18. 켬 장 tuning_reload 오류(다시 읽기·재초기화 없음, key_errors 비어도) → 판정 불가", make_capture_result(unlocked, spec_path, reload_errors={"side_on": "실효 값 다시 읽기: 해석 실패"}), 2),
            ("19. 정본 장 드라이버 없음(높이 원천 None) → 통과", make_capture_result(unlocked, spec_path, reads={"side_canonical": {"bFogFloorFollowsTerrain": absent_driver}}), 0),
            ("19b. 켬 장 드라이버 없음 → 판정 불가", make_capture_result(unlocked, spec_path, reads={"side_on": {"bFogFloorFollowsTerrain": absent_driver}}), 2),
            ("20. 촬영 결과 명세 해시 다름 → 판정 불가", dict(valid_capture, variants_sha256="0" * 64), 2),
        ]
        for index, (label, capture, expected_code) in enumerate(cases):
            code, problems = judged_with(f"capture{index}", capture)
            record(label, code == expected_code, {"exit": code, "problems": problems[:3]})
        capture_path.write_text(json.dumps(valid_capture), encoding="utf-8")
        sorted(shots.glob("*_08000ms.png"))[1].unlink()
        code = judge(shots, prereg_path, folder / "e.json", None, spec_path)
        record("⑫ 장 하나 빠짐 → 판정 불가", code == 2, {"exit": code})

    real_spec = REPOSITORY_ROOT / prereg["tile"]["shot_spec"]
    problems = check_shot_spec(real_spec, prereg)
    record("⑬ 실제 촬영 명세 = prereg 장 조건", not problems, problems)
    problems = check_locked_files(prereg)
    record("⑭ 실제 잠금 파일 해시 = prereg", not problems, problems)
    print(json.dumps(outcomes, ensure_ascii=False, indent=1, default=to_json_value))
    passed = sum(outcome["passed"] for outcome in outcomes)
    print(f"self-test {passed}/{len(outcomes)}")
    return 0 if passed == len(outcomes) else 1


def main():
    if "--self-test" in sys.argv[1:]:
        return run_self_test()
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    judge_parser = commands.add_parser("judge")
    judge_parser.add_argument("--shots", required=True)
    judge_parser.add_argument("--prereg", required=True)
    judge_parser.add_argument("--output", required=True)
    judge_parser.add_argument("--overlay")
    judge_parser.add_argument("--shot-spec")
    arguments = parser.parse_args()
    return judge(arguments.shots, arguments.prereg, arguments.output, arguments.overlay, arguments.shot_spec)


if __name__ == "__main__":
    sys.exit(main())
