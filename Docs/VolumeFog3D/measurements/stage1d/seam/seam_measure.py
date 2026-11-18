"""실행 환경: 파이썬 3 + numpy + Pillow, 작업 폴더 C:/Project/TDGame (PYTHONUTF8=1 python -B)
실행 명령: python -B Docs/VolumeFog3D/measurements/stage1d/seam/seam_measure.py
출력: 같은 폴더의 measure.json, seam_crops.png, 그리고 Saved/AgentOps/20261114/fog3d/stage1d/seam.json (쌍마다 w·p·유효, 판정)
상태: 1d 이음매 가르기(prereg-1d-seam.md 2~3절)용. 영역·식은 앞 촬영(diag_measure.py)과 같은 fog3d_calib_report 를 쓴다."""
import sys, json, re, importlib.util
from pathlib import Path
from unittest import mock
import numpy
from PIL import Image, ImageDraw, ImageFont
ROOT = Path("C:/Project/TDGame")
sys.path.insert(0, str(ROOT / "Tools")); sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_report as R
CAL = ROOT / "Saved/FluidFX/fog3d_calib"
X = ROOT / "Saved/AgentOps/20261114/fog3d/stage1d/x"
OUT = Path(__file__).resolve().parent
SEAM_JSON = ROOT / "Saved/AgentOps/20261114/fog3d/stage1d/seam.json"
RUN_ORDER = ["P1_px", "P1_nx", "P2_px", "P2_nx", "P4_py", "P4_ny", "P3_py", "P3_ny"]
REFERENCE = ["G1_px", "G1_nx", "G1_py"]
TARGET_LINE = {"P1": ("y", 500.0), "P2": ("y", 507.8125), "P3": ("x", -2000.0), "P4": ("x", -2000.0)}
PAIR_CELLS = {"P1": 256, "P2": 256, "P3": 128, "P4": 256}
LINE_TOLERANCE_CM = 3.0
CLEARANCE_CM = 300.0
FADE_CM = 500.0
sys.modules.setdefault("unreal", mock.MagicMock())
spec = importlib.util.spec_from_file_location("stage_script", ROOT / "Tools/FluidFX/editor_make_fog3d_stage.py")
stage = importlib.util.module_from_spec(spec); spec.loader.exec_module(stage)


def stage_clearance(path_start, path_end):
    layout = stage.build_default_layout()
    rect = (min(path_start[0], path_end[0]), min(path_start[1], path_end[1]), max(path_start[0], path_end[0]), max(path_start[1], path_end[1]))
    gaps = {item["label"]: stage.rectangle_gap(stage.box_xy_bounds(item["center"], item["size"]), rect) for item in layout["obstacles"]}
    half = layout["zone_half_extent"]; inner = half[0] - FADE_CM, half[1] - FADE_CM
    inside_fade = all(abs(c[0]) <= inner[0] and abs(c[1]) <= inner[1] for c in (path_start, path_end))
    return gaps, inside_fade


log_text = (ROOT / "Saved/Logs/TDGame.log").read_text(encoding="utf-8", errors="replace")
summaries = [(int(a), int(b)) for a, b in re.findall(r"Fog3D zone summary: submitted=(\d+) rejected=(\d+)", log_text)]
problem_lines = re.findall(r"LogTDFog3D\w*: (?:Warning|Error).*", log_text)
opts = R.parse_arguments(["--runs", "x", "--no-images", "--out-root", str(CAL)])


def centroid_numbers(d):
    a = numpy.asarray(Image.open(d / "00_c_12100ms.png").convert("L"), dtype=numpy.float64) / 255.0
    b = numpy.asarray(Image.open(d / "00_c_19400ms.png").convert("L"), dtype=numpy.float64) / 255.0
    h, w = a.shape; top, bottom, left, right = R.walk_region(h, w)
    reg = numpy.abs(a - b)[top:bottom, left:right] * 255.0
    xs = numpy.arange(left, right)
    k = int(0.4 * reg.shape[0]); low = reg[k:]
    return {"centroid_offset_px": float((reg.sum(0) * xs).sum() / reg.sum() - w / 2), "centroid_offset_lower60_px": float((low.sum(0) * xs).sum() / low.sum() - w / 2)}


rows = {}
for n in REFERENCE + RUN_ORDER:
    d = CAL / n / "capture"
    report, matched, rec, lumas = R.analyze_run(CAL / n, opts)
    num = report["numbers"]; sp = R.split_walk_numbers(lumas)
    run = json.loads((CAL / n / "run.json").read_text(encoding="utf-8"))
    row = {"name": n, "valid_by_tool": bool(report["valid"]), "invalid_reasons": report["invalid_reasons"], "Wk_12.1": num["Wk"]["W12.1"], "Wk_L": sp["Wk_L"], "Wk_R": sp["Wk_R"], "asymmetry": sp["asymmetry"],
           "camera_yaw": run.get("camera_yaw_degrees"), "walk_yaw": run.get("walk_yaw_degrees"), **centroid_numbers(d)}
    if n in RUN_ORDER:
        walk = json.loads(next(d.glob("*event09600ms_walk.json")).read_text(encoding="utf-8"))
        state = json.loads((X / f"state_{n}.json").read_text(encoding="utf-8"))
        axis, target = TARGET_LINE[n[:2]]
        index = 0 if axis == "x" else 1
        start = walk["start"]; end = state.get("pawn_xyz")
        row.update({"pawn_start_xy": start, "pawn_end_xy": end[:2] if end else None, "line_axis": axis, "line_target": target,
                    "line_error_start_cm": start[index] - target, "line_error_end_cm": (end[index] - target) if end else None, "walk_moved_cm": walk["moved_cm"], "walk_timed_out": walk["timed_out"],
                    "cells_expected": PAIR_CELLS[n[:2]], "state_target_xyz": state.get("state_target"), "quality_cvar_after_run": state.get("TD.Fog3D.Quality")})
        line_ok = abs(row["line_error_start_cm"]) <= LINE_TOLERANCE_CM and row["line_error_end_cm"] is not None and abs(row["line_error_end_cm"]) <= LINE_TOLERANCE_CM
        gaps, inside = stage_clearance(start, end[:2] if end else start)
        row.update({"line_within_3cm": line_ok, "min_gap_to_stage_objects_cm": min(gaps.values()), "gap_by_object_cm": gaps, "path_inside_fade_band_inner": inside})
        sub_rej = summaries[RUN_ORDER.index(n)] if len(summaries) >= len(RUN_ORDER) else None
        row["editor_log_submitted_rejected"] = sub_rej
        row["valid"] = bool(report["valid"]) and line_ok and not walk["timed_out"] and (sub_rej is not None and sub_rej[1] == 0) and row["min_gap_to_stage_objects_cm"] >= CLEARANCE_CM and inside
    rows[n] = row

pairs = {}
for key in ("P1", "P2", "P3", "P4"):
    axis_letter = "x" if key in ("P1", "P2") else "y"
    fwd, rev = rows[f"{key}_p{axis_letter}"], rows[f"{key}_n{axis_letter}"]
    w = (fwd["asymmetry"] - rev["asymmetry"]) / 2.0; p = (fwd["asymmetry"] + rev["asymmetry"]) / 2.0
    side = R.world_side_axis(fwd["camera_yaw"], left=w > 0) if abs(w) > 1e-9 else None
    pairs[key] = {"w": w, "p": p, "valid": bool(fwd["valid"] and rev["valid"]), "fwd": fwd["name"], "rev": rev["name"], "fwd_asymmetry": fwd["asymmetry"], "rev_asymmetry": rev["asymmetry"],
                  "biased_world_side_of_w": side, "cells": PAIR_CELLS[key], "line": f"{TARGET_LINE[key][0]} = {TARGET_LINE[key][1]}"}


def verdict_of(p):
    invalid = [k for k in p if not p[k]["valid"]]
    if invalid:
        return "가르지 못함", f"무효 쌍 {invalid}"
    w = {k: abs(p[k]["w"]) for k in p}
    if w["P1"] <= 0.07 and w["P2"] <= 0.07 and w["P3"] >= 0.15 and w["P4"] <= 0.07:
        return "이음매", "|w1|≤0.07, |w2|≤0.07, |w3|≥0.15, |w4|≤0.07"
    if w["P1"] >= 0.15 and w["P2"] <= 0.07:
        return "칸 경계", "|w1|≥0.15 이고 |w2|≤0.07"
    if w["P1"] >= 0.15 and w["P2"] >= 0.15:
        return "축", "|w1|≥0.15 이고 |w2|≥0.15"
    if w["P1"] <= 0.07 and w["P2"] <= 0.07 and w["P3"] <= 0.07:
        return "y = 0 선", "|w1|,|w2|,|w3| 모두 ≤0.07"
    return "가르지 못함", "어느 규칙에도 해당하지 않음"


verdict, why = verdict_of(pairs)
info = {}
for n in ("I0_px", "I0_nx"):
    if (CAL / n / "capture" / "result.json").exists():
        report, matched, rec, lumas = R.analyze_run(CAL / n, opts)
        sp = R.split_walk_numbers(lumas)
        walk = json.loads(next((CAL / n / "capture").glob("*event09600ms_walk.json")).read_text(encoding="utf-8"))
        state_path = X / f"state_{n}.json"
        info[n] = {"Wk_12.1": report["numbers"]["Wk"]["W12.1"], "Wk_L": sp["Wk_L"], "Wk_R": sp["Wk_R"], "asymmetry": sp["asymmetry"], "valid_by_tool": bool(report["valid"]), "pawn_start_xy": walk["start"],
                   "pawn_end_xy": json.loads(state_path.read_text(encoding="utf-8")).get("pawn_xyz") if state_path.exists() else None, **centroid_numbers(CAL / n / "capture")}
if len(info) == 2:
    info["pair"] = {"w": (info["I0_px"]["asymmetry"] - info["I0_nx"]["asymmetry"]) / 2.0, "p": (info["I0_px"]["asymmetry"] + info["I0_nx"]["asymmetry"]) / 2.0, "note": "정보용: P0(y=0, 순간이동 없음) 와 같은 선에 같은 순간이동 이벤트를 넣은 통제. 판정에는 안 씀"}
    print("info I0 (정보용)", {k: (round(v, 3) if isinstance(v, float) else v) for k, v in info["pair"].items() if k != "note"}, {n: round(info[n]["asymmetry"], 3) for n in ("I0_px", "I0_nx")})
log_info = {"summaries_in_order": summaries, "run_order_for_summaries": RUN_ORDER, "fog3d_warning_or_error_lines_whole_editor_log": len(problem_lines), "sample": problem_lines[:3]}
p0 = {"w_from_G1": (rows["G1_px"]["asymmetry"] - rows["G1_nx"]["asymmetry"]) / 2.0, "p_from_G1": (rows["G1_px"]["asymmetry"] + rows["G1_nx"]["asymmetry"]) / 2.0}
(OUT / "measure.json").write_text(json.dumps({"rows": rows, "pairs": pairs, "verdict": verdict, "verdict_rule": why, "editor_log": log_info, "P0_reference": p0, "info_teleport_control_I0": info}, ensure_ascii=False, indent=1), encoding="utf-8")
SEAM_JSON.write_text(json.dumps({"pairs": {k: {"w": v["w"], "p": v["p"], "valid": v["valid"]} for k, v in pairs.items()}, "verdict": verdict}, ensure_ascii=False, indent=1), encoding="utf-8")
for n in RUN_ORDER:
    r = rows[n]
    print(f"{n}: valid={r['valid']} tool={r['valid_by_tool']} Wk={r['Wk_12.1']:.3f} L={r['Wk_L']:.3f} R={r['Wk_R']:.3f} asym={r['asymmetry']:+.3f} cen={r['centroid_offset_px']:+.1f}/{r['centroid_offset_lower60_px']:+.1f}px "
          f"start={[round(v, 2) for v in r['pawn_start_xy']]} end={[round(v, 2) for v in (r['pawn_end_xy'] or [])]} lineErr={r['line_error_start_cm']:+.2f}/{r['line_error_end_cm']:+.2f} gap={r['min_gap_to_stage_objects_cm']:.0f} fade_ok={r['path_inside_fade_band_inner']} "
          f"cells={r['state_target_xyz']} log={r['editor_log_submitted_rejected']} {r['invalid_reasons']}")
for k, v in pairs.items():
    print(k, f"w={v['w']:+.3f} p={v['p']:+.3f} valid={v['valid']} side={v['biased_world_side_of_w']}")
print("P0 reference", p0)
print("verdict:", verdict, "|", why)


def crop_cell(name):
    d = CAL / name / "capture"
    stopped = Image.open(d / "00_c_12100ms.png").convert("RGB"); ref = Image.open(d / "00_c_19400ms.png").convert("RGB")
    w, h = stopped.size; top, bottom, left, right = R.walk_region(h, w)
    a = numpy.asarray(stopped.convert("L"), dtype=numpy.float32); b = numpy.asarray(ref.convert("L"), dtype=numpy.float32)
    diff = Image.fromarray(numpy.clip(numpy.abs(a - b) * 8.0, 0, 255).astype(numpy.uint8)).convert("RGB")
    return stopped.crop((left, top, right, bottom)), diff.crop((left, top, right, bottom))


layout = [["P1_px", "P1_nx", "P2_px", "P2_nx"], ["P3_py", "P3_ny", "P4_py", "P4_ny"]]
cw, ch = crop_cell("P1_px")[0].size
gap = 6; label_h = 40
img = Image.new("RGB", (4 * cw + 5 * gap, 2 * (label_h + 2 * ch + gap) + gap), (24, 24, 24))
draw = ImageDraw.Draw(img)
try:
    font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 15)
except Exception:
    font = None
for ri, names in enumerate(layout):
    y0 = gap + ri * (label_h + 2 * ch + gap)
    for ci, n in enumerate(names):
        x0 = gap + ci * (cw + gap); s, dff = crop_cell(n); r = rows[n]
        draw.text((x0, y0 + 2), f"{n} ({PAIR_CELLS[n[:2]]}칸) 선 {r['line_axis']}={r['line_target']}", fill=(255, 255, 255), font=font)
        draw.text((x0, y0 + 20), f"Wk {r['Wk_12.1']:.2f} L {r['Wk_L']:.2f} R {r['Wk_R']:.2f} 치우침 {r['asymmetry']:+.3f}", fill=(255, 220, 120), font=font)
        img.paste(s, (x0, y0 + label_h)); img.paste(dff, (x0, y0 + label_h + ch))
img.save(OUT / "seam_crops.png")
print("figure", img.size)
