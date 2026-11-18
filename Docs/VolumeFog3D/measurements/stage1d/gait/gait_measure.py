"""실행 환경: 파이썬 3 + numpy + Pillow, 작업 폴더 C:/Project/TDGame (PYTHONUTF8=1 python -B)
실행 명령: python -B Docs/VolumeFog3D/measurements/stage1d/gait/gait_measure.py
출력: 같은 폴더의 measure.json, gait_crops.png, pose_compare.png, 그리고 Saved/AgentOps/20261114/fog3d/stage1d/gait.json (규칙 1~3 판정 요약)
상태: 1d 같은 출발(prereg-1d-gait.md 2~3절)용. 영역·식은 앞 촬영(seam_measure.py)과 같은 fog3d_calib_report 를 쓴다."""
import sys, json, re
from pathlib import Path
import numpy
from PIL import Image, ImageDraw, ImageFont
ROOT = Path("C:/Project/TDGame")
sys.path.insert(0, str(ROOT / "Tools")); sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_report as R
CAL = ROOT / "Saved/FluidFX/fog3d_calib"
K = ROOT / "Saved/AgentOps/20261114/fog3d/stage1d/k"
OUT = Path(__file__).resolve().parent
GAIT_JSON = ROOT / "Saved/AgentOps/20261114/fog3d/stage1d/gait.json"
SEAM = json.loads((ROOT / "Docs/VolumeFog3D/measurements/stage1d/seam/measure.json").read_text(encoding="utf-8"))
RUN_ORDER = ["K1_B", "K1_A", "K1_C", "K1_D", "K2_A", "K2_B", "K2_D", "K3_520", "K3_690"]
SPEC = {
    "K1_A": ("y", 500.0, 600), "K1_B": ("y", 500.0, 600), "K1_C": ("x", -2000.0, 600), "K1_D": ("x", -2000.0, 600),
    "K2_A": ("y", 500.0, 600), "K2_B": ("y", 500.0, 600), "K2_D": ("x", -2000.0, 600), "K3_520": ("y", 500.0, 520), "K3_690": ("y", 500.0, 690),
}
SAME_VALUES = {"FootRadiusCm": 45.0, "HandRadiusCm": 45.0, "PelvisRadiusCm": 60.0, "BodyWindStrength": 2.0, "BladeRadiusCm": 30.0, "bStainNoiseEnabled": False, "StainNoiseAmplitude": 0.75,
               "StainNoiseSpacingCm": 400.0, "DisplacementRefillHorizontalPerSec": 0.05, "DisplacementRefillVerticalPerSec": 0.15}
PELVIS_VALUES = {**SAME_VALUES, "FootRadiusCm": 16.0, "HandRadiusCm": 16.0, "BladeWindStrength": 0.0, "BladeErosionPerSec": 0.0}
EXPECTED = {n: (PELVIS_VALUES if n.startswith("K2") else SAME_VALUES) for n in SPEC}
WALK_YAW = {"K1_A": 0, "K1_B": 180, "K1_C": 90, "K1_D": 270, "K2_A": 0, "K2_B": 180, "K2_D": 270, "K3_520": 0, "K3_690": 0}
LINE_TOLERANCE_CM = 3.0
PAWN_BOX = (540, 230, 740, 520)
PAWN_BODY_BOX = (575, 300, 735, 520)
POSE_TIMES = ["10.0", "10.4", "10.8"]
OLD = ["P1_px", "P1_nx", "P4_py", "P4_ny"]
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


def run_row(n):
    d = CAL / n / "capture"
    report, matched, rec, lumas = R.analyze_run(CAL / n, opts)
    num = report["numbers"]; sp = R.split_walk_numbers(lumas)
    run = json.loads((CAL / n / "run.json").read_text(encoding="utf-8"))
    row = {"name": n, "valid_by_tool": bool(report["valid"]), "invalid_reasons": report["invalid_reasons"], "Wk_12.1": num["Wk"]["W12.1"], "Wk_17.6": num["Wk"]["W17.6"], "Wk_L": sp["Wk_L"], "Wk_R": sp["Wk_R"],
           "asymmetry": sp["asymmetry"], "camera_yaw": run.get("camera_yaw_degrees"), "walk_yaw": run.get("walk_yaw_degrees"), **centroid_numbers(d)}
    axis, target, dist = SPEC[n]
    index = 0 if axis == "x" else 1
    walk = json.loads(next(d.glob("*event09600ms_walk.json")).read_text(encoding="utf-8"))
    state = json.loads((K / f"state_{n}.json").read_text(encoding="utf-8"))
    end = state.get("pawn_xyz"); start = walk["start"]
    row.update({"pawn_start_xy": start, "pawn_end_xy": end[:2] if end else None, "pawn_yaw_end": state.get("pawn_yaw"), "line_axis": axis, "line_target": target,
                "line_error_start_cm": start[index] - target, "line_error_end_cm": (end[index] - target) if end else None, "walk_distance_target_cm": dist, "walk_distance_actual_cm": walk["distance_cm"],
                "walk_moved_cm": walk["moved_cm"], "walk_timed_out": walk["timed_out"], "state_target_xyz": state.get("state_target")})
    line_ok = abs(row["line_error_start_cm"]) <= LINE_TOLERANCE_CM and row["line_error_end_cm"] is not None and abs(row["line_error_end_cm"]) <= LINE_TOLERANCE_CM
    prewalk = next(iter(d.glob("*event07400ms_walk.json")), None)
    pre = json.loads(prewalk.read_text(encoding="utf-8")) if prewalk else None
    row["prewalk"] = None if pre is None else {"yaw": pre["yaw"], "distance_cm": pre["distance_cm"], "moved_cm": pre["moved_cm"], "elapsed_s": pre["elapsed_game_seconds"], "timed_out": pre["timed_out"], "start_xy": pre["start"]}
    row["yaw_matches_walk_yaw"] = abs(((state.get("pawn_yaw") or 0) - WALK_YAW[n] + 180) % 360 - 180) < 1.0
    settings = json.loads((CAL / n / "tuning.json").read_text(encoding="utf-8"))["Globals"]["TDFog3DSettings"]
    mismatches = {k: [settings.get(k), v] for k, v in EXPECTED[n].items() if k not in settings or abs(float(settings[k]) - float(v)) > 1e-9}
    row["tuning_values_match"] = mismatches == {}
    row["tuning_mismatches"] = mismatches
    row["tuning_selected"] = {k: settings.get(k) for k in ("FootRadiusCm", "HandRadiusCm", "BladeWindStrength", "BladeErosionPerSec", "bStainNoiseEnabled", "PelvisRadiusCm", "BodyWindStrength")}
    variants = json.loads((CAL / n / "variants.json").read_text(encoding="utf-8"))
    row["sun_rotation_variants"] = variants["base"]["props"]["TDGen_Fog3D_Sun"]["root_component"]["relative_rotation"]
    result = json.loads((d / "result.json").read_text(encoding="utf-8"))
    row["key_errors"] = result.get("key_errors")
    row["tuning_load_error_count"] = result["base"]["tuning_reload"]["load"]["error_count"]
    row["line_within_3cm"] = line_ok
    row["valid_pre"] = bool(report["valid"]) and line_ok and not walk["timed_out"] and row["yaw_matches_walk_yaw"] and row["tuning_values_match"] and not row["key_errors"] and row["tuning_load_error_count"] == 0
    return row


rows = {}
for i, n in enumerate(RUN_ORDER):
    rows[n] = run_row(n)
    rows[n]["editor_log_submitted_rejected"] = summaries[i - len(RUN_ORDER)] if len(summaries) >= len(RUN_ORDER) else None
    sub = rows[n]["editor_log_submitted_rejected"]
    rows[n]["valid"] = bool(rows[n]["valid_pre"] and sub is not None and sub[1] == 0 and len(problem_lines) == 0)


def spread(vals):
    return max(vals) - min(vals)


k1 = [rows[n] for n in ("K1_A", "K1_B", "K1_C", "K1_D")]
wk = [r["Wk_12.1"] for r in k1]; mean_wk = sum(wk) / 4
asym = [r["asymmetry"] for r in k1]
rule1 = {"Wk": {r["name"]: r["Wk_12.1"] for r in k1}, "Wk_mean": mean_wk, "Wk_relative_deviation": {r["name"]: r["Wk_12.1"] / mean_wk - 1 for r in k1}, "Wk_within_15pct": all(abs(r["Wk_12.1"] / mean_wk - 1) <= 0.15 for r in k1),
         "asymmetry": {r["name"]: r["asymmetry"] for r in k1}, "asymmetry_spread": spread(asym), "asymmetry_spread_within_0.10": spread(asym) <= 0.10,
         "asymmetry_magnitude_le_0.15": {r["name"]: abs(r["asymmetry"]) <= 0.15 for r in k1}, "all_valid": all(r["valid"] for r in k1)}
rule1["met"] = bool(rule1["Wk_within_15pct"] and rule1["asymmetry_spread_within_0.10"] and rule1["all_valid"])
k2 = [rows[n] for n in ("K2_A", "K2_B", "K2_D")]
rule2 = {"asymmetry": {r["name"]: r["asymmetry"] for r in k2}, "asymmetry_spread": spread([r["asymmetry"] for r in k2]), "Wk": {r["name"]: r["Wk_12.1"] for r in k2}, "all_valid": all(r["valid"] for r in k2)}
rule2["met"] = bool(rule2["asymmetry_spread"] <= 0.10 and rule2["all_valid"])
k3 = [rows[n] for n in ("K3_520", "K3_690")]
rule3 = {"asymmetry": {r["name"]: r["asymmetry"] for r in k3}, "asymmetry_difference_abs": abs(k3[0]["asymmetry"] - k3[1]["asymmetry"]), "Wk": {r["name"]: r["Wk_12.1"] for r in k3}, "all_valid": all(r["valid"] for r in k3),
         "K1_A_600cm_asymmetry": rows["K1_A"]["asymmetry"]}
rule3["met"] = bool(rule3["asymmetry_difference_abs"] >= 0.10 and rule3["all_valid"])


def pawn_crop(name, time, box=None):
    p = CAL / name / "capture" / f"00_c_{int(round(float(time) * 1000)):05d}ms.png"
    return Image.open(p).convert("L").crop(box or PAWN_BOX)


def mean_abs(a, b):
    return float(numpy.abs(numpy.asarray(a, dtype=numpy.float64) - numpy.asarray(b, dtype=numpy.float64)).mean())


pose = {}
names = ["K1_A", "K1_B", "K1_C", "K1_D"]
for t in POSE_TIMES:
    pairs = {}
    for i in range(4):
        for j in range(i + 1, 4):
            pairs[f"{names[i]}-{names[j]}"] = mean_abs(pawn_crop(names[i], t), pawn_crop(names[j], t))
    old = {"P1_px-P1_nx": mean_abs(pawn_crop("P1_px", t), pawn_crop("P1_nx", t)), "P4_py-P4_ny": mean_abs(pawn_crop("P4_py", t), pawn_crop("P4_ny", t)),
           "P1_px-P4_py": mean_abs(pawn_crop("P1_px", t), pawn_crop("P4_py", t))}
    body = {f"{names[i]}-{names[j]}": mean_abs(pawn_crop(names[i], t, PAWN_BODY_BOX), pawn_crop(names[j], t, PAWN_BODY_BOX)) for i in range(4) for j in range(i + 1, 4)}
    body_old = {"P1_px-P1_nx": mean_abs(pawn_crop("P1_px", t, PAWN_BODY_BOX), pawn_crop("P1_nx", t, PAWN_BODY_BOX)), "P4_py-P4_ny": mean_abs(pawn_crop("P4_py", t, PAWN_BODY_BOX), pawn_crop("P4_ny", t, PAWN_BODY_BOX)),
                "P1_px-P4_py": mean_abs(pawn_crop("P1_px", t, PAWN_BODY_BOX), pawn_crop("P4_py", t, PAWN_BODY_BOX))}
    pose[t] = {"K1_pairs_mean_abs_luma_0_255": pairs, "K1_mean": sum(pairs.values()) / len(pairs), "K1_max": max(pairs.values()), "old_P_pairs": old,
               "body_box": list(PAWN_BODY_BOX), "K1_body_pairs": body, "K1_body_mean": sum(body.values()) / len(body), "K1_body_max": max(body.values()), "old_P_body_pairs": body_old}
extra_pose = {"K2_A-K2_B_10.4": mean_abs(pawn_crop("K2_A", "10.4"), pawn_crop("K2_B", "10.4")), "K1_A-K2_A_10.4": mean_abs(pawn_crop("K1_A", "10.4"), pawn_crop("K2_A", "10.4")),
              "K1_B-K2_B_10.4": mean_abs(pawn_crop("K1_B", "10.4"), pawn_crop("K2_B", "10.4")), "K3_520-K1_A_10.4": mean_abs(pawn_crop("K3_520", "10.4"), pawn_crop("K1_A", "10.4"))}
old_rows = {n: {"asymmetry": SEAM["rows"][n]["asymmetry"], "Wk_12.1": SEAM["rows"][n]["Wk_12.1"], "Wk_L": SEAM["rows"][n]["Wk_L"], "Wk_R": SEAM["rows"][n]["Wk_R"]} for n in OLD}
compare_old = {"K1_A_vs_P1_px": [rows["K1_A"]["asymmetry"], old_rows["P1_px"]["asymmetry"]], "K1_B_vs_P1_nx": [rows["K1_B"]["asymmetry"], old_rows["P1_nx"]["asymmetry"]],
               "K1_C_vs_P4_py": [rows["K1_C"]["asymmetry"], old_rows["P4_py"]["asymmetry"]], "K1_D_vs_P4_ny": [rows["K1_D"]["asymmetry"], old_rows["P4_ny"]["asymmetry"]]}
(OUT / "measure.json").write_text(json.dumps({"rows": rows, "rule1": rule1, "rule2": rule2, "rule3": rule3, "pose_compare_mean_abs_luma": pose, "pose_extra": extra_pose, "old_P_rows": old_rows, "compare_old": compare_old,
                                              "editor_log": {"summaries_in_order": summaries, "run_order": RUN_ORDER, "fog3d_warning_or_error_lines_whole_editor_log": len(problem_lines), "sample": problem_lines[:3]}},
                                             ensure_ascii=False, indent=1), encoding="utf-8")
GAIT_JSON.write_text(json.dumps({"rule1": rule1, "rule2": rule2, "rule3": rule3}, ensure_ascii=False, indent=1), encoding="utf-8")
for n in RUN_ORDER:
    r = rows[n]
    print(f"{n}: valid={r['valid']} tool={r['valid_by_tool']} Wk12.1={r['Wk_12.1']:.3f} Wk17.6={r['Wk_17.6']:.3f} L={r['Wk_L']:.3f} R={r['Wk_R']:.3f} asym={r['asymmetry']:+.3f} cen={r['centroid_offset_px']:+.1f}/{r['centroid_offset_lower60_px']:+.1f}px "
          f"start={[round(v, 2) for v in r['pawn_start_xy']]} end={[round(v, 2) for v in (r['pawn_end_xy'] or [])]} lineErr={r['line_error_start_cm']:+.2f}/{r['line_error_end_cm']:+.2f} yaw_ok={r['yaw_matches_walk_yaw']} tuning_ok={r['tuning_values_match']} log={r['editor_log_submitted_rejected']} {r['invalid_reasons']}")
print("rule1", json.dumps(rule1, ensure_ascii=False))
print("rule2", json.dumps(rule2, ensure_ascii=False))
print("rule3", json.dumps(rule3, ensure_ascii=False))
for t in POSE_TIMES:
    print("pose_body", t, {k: round(v, 2) for k, v in pose[t]["K1_body_pairs"].items()}, "mean", round(pose[t]["K1_body_mean"], 2), "old", {k: round(v, 2) for k, v in pose[t]["old_P_body_pairs"].items()})
    print("pose", t, {k: round(v, 2) for k, v in pose[t]["K1_pairs_mean_abs_luma_0_255"].items()}, "mean", round(pose[t]["K1_mean"], 2), "old", {k: round(v, 2) for k, v in pose[t]["old_P_pairs"].items()})
print("extra", {k: round(v, 2) for k, v in extra_pose.items()})
print("compare_old", compare_old)


def crop_cell(name):
    d = CAL / name / "capture"
    stopped = Image.open(d / "00_c_12100ms.png").convert("RGB"); ref = Image.open(d / "00_c_19400ms.png").convert("RGB")
    w, h = stopped.size; top, bottom, left, right = R.walk_region(h, w)
    a = numpy.asarray(stopped.convert("L"), dtype=numpy.float32); b = numpy.asarray(ref.convert("L"), dtype=numpy.float32)
    diff = Image.fromarray(numpy.clip(numpy.abs(a - b) * 8.0, 0, 255).astype(numpy.uint8)).convert("RGB")
    return stopped.crop((left, top, right, bottom)), diff.crop((left, top, right, bottom))


try:
    font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 15)
except Exception:
    font = None
layout = [["P1_px", "P1_nx", "P4_py", "P4_ny"], ["K1_A", "K1_B", "K1_C", "K1_D"]]
cw, ch = crop_cell("P1_px")[0].size
gap = 6; label_h = 40
img = Image.new("RGB", (4 * cw + 5 * gap, 2 * (label_h + 2 * ch + gap) + gap), (24, 24, 24))
draw = ImageDraw.Draw(img)
for ri, nm in enumerate(layout):
    y0 = gap + ri * (label_h + 2 * ch + gap)
    for ci, n in enumerate(nm):
        x0 = gap + ci * (cw + gap); s, dff = crop_cell(n)
        if n.startswith("P"):
            o = old_rows[n]; t1 = f"{n} (앞 촬영, 돌며 출발)"
        else:
            o = rows[n]; t1 = f"{n} (같은 출발) 선 {rows[n]['line_axis']}={rows[n]['line_target']:.0f}"
        t2 = f"Wk {o['Wk_12.1']:.2f} L {o['Wk_L']:.2f} R {o['Wk_R']:.2f} 치우침 {o['asymmetry']:+.3f}"
        draw.text((x0, y0 + 2), t1, fill=(255, 255, 255), font=font); draw.text((x0, y0 + 20), t2, fill=(255, 220, 120), font=font)
        img.paste(s, (x0, y0 + label_h)); img.paste(dff, (x0, y0 + label_h + ch))
img.save(OUT / "gait_crops.png")
print("figure", img.size)

order = ["P1_px", "P1_nx", "P4_py", "P4_ny", "K1_A", "K1_B", "K1_C", "K1_D", "K2_A", "K2_B", "K2_D", "K3_520", "K3_690"]
cols = 4
cell_w, cell_h = PAWN_BOX[2] - PAWN_BOX[0], PAWN_BOX[3] - PAWN_BOX[1]
rowsn = (len(order) + cols - 1) // cols
pic = Image.new("RGB", (cols * (cell_w + gap) + gap, rowsn * (cell_h + 22 + gap) + gap), (24, 24, 24))
pd = ImageDraw.Draw(pic)
for idx, n in enumerate(order):
    x0 = gap + (idx % cols) * (cell_w + gap); y0 = gap + (idx // cols) * (cell_h + 22 + gap)
    pd.text((x0, y0), f"{n} 10.4초", fill=(255, 255, 255), font=font)
    pic.paste(pawn_crop(n, "10.4").convert("RGB"), (x0, y0 + 22))
pic.save(OUT / "pose_compare.png")
print("pose figure", pic.size)
