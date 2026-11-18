"""실행 환경: 파이썬 3 + numpy + Pillow, 작업 폴더 C:/Project/TDGame (PYTHONUTF8=1 python -B)
실행 명령: python -B Docs/VolumeFog3D/measurements/stage1d/retune3/final_measure.py <실행이름 네 개: 선 A B C D 순서>
출력: 같은 폴더 final_rows.json, final_check_crops.png (마지막 확인 네 방향 걷기 영역 1:1 + 차이 x8)
상태: 1d 마지막 확인(prereg-1d-diag.md 4절 + prereg-1d-gait.md 4절). 영역·식은 fog3d_calib_report, 유효성 점검은 gait_measure.py 와 같은 방식."""
import sys, json, re
from pathlib import Path
import numpy
from PIL import Image, ImageDraw, ImageFont
ROOT = Path("C:/Project/TDGame")
sys.path.insert(0, str(ROOT / "Tools")); sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_report as R
CAL = ROOT / "Saved/FluidFX/fog3d_calib"
X = ROOT / "Saved/AgentOps/20261114/fog3d/stage1d/r"
OUT = Path(__file__).resolve().parent
names = sys.argv[1:5]
SPEC = {"A": ("y", 500.0, 0), "B": ("y", 500.0, 180), "C": ("x", -2000.0, 90), "D": ("x", -2000.0, 270)}
EXPECT = {"FootRadiusCm": 45.0, "HandRadiusCm": 45.0, "PelvisRadiusCm": 60.0, "BodyWindStrength": 3.0, "BladeRadiusCm": 35.0, "BladeWindStrength": 2.5, "bStainNoiseEnabled": False,
          "StainNoiseAmplitude": 0.75, "StainNoiseSpacingCm": 400.0, "DisplacementRefillHorizontalPerSec": 0.05, "DisplacementRefillVerticalPerSec": 0.15}
if len(sys.argv) > 5:
    EXPECT.update(json.loads(sys.argv[5]))
log_text = (ROOT / "Saved/Logs/TDGame.log").read_text(encoding="utf-8", errors="replace")
summaries = [(int(a), int(b)) for a, b in re.findall(r"Fog3D zone summary: submitted=(\d+) rejected=(\d+)", log_text)]
problem_lines = re.findall(r"LogTDFog3D\w*: (?:Warning|Error).*", log_text)
opts = R.parse_arguments(["--runs", "x", "--no-images", "--out-root", str(CAL)])
PAWN_BODY_BOX = (575, 300, 735, 520)
rows = {}
for i, n in enumerate(names):
    key = "ABCD"[i]; axis, target, wy = SPEC[key]; idx = 0 if axis == "x" else 1
    d = CAL / n / "capture"
    report, matched, rec, lumas = R.analyze_run(CAL / n, opts); num = report["numbers"]; sp = R.split_walk_numbers(lumas)
    walk = json.loads(next(d.glob("*event09600ms_walk.json")).read_text(encoding="utf-8"))
    pre = json.loads(next(d.glob("*event07400ms_walk.json")).read_text(encoding="utf-8"))
    state = json.loads((X / f"state_{n}.json").read_text(encoding="utf-8")); end = state.get("pawn_xyz"); start = walk["start"]
    settings = json.loads((CAL / n / "tuning.json").read_text(encoding="utf-8"))["Globals"]["TDFog3DSettings"]
    mism = {k: [settings.get(k), v] for k, v in EXPECT.items() if k not in settings or abs(float(settings[k]) - float(v)) > 1e-9}
    variants = json.loads((CAL / n / "variants.json").read_text(encoding="utf-8")); result = json.loads((d / "result.json").read_text(encoding="utf-8"))
    sub = summaries[i - 4] if len(summaries) >= 4 else None
    line_start = start[idx] - target; line_end = (end[idx] - target) if end else None
    yaw_ok = abs(((state.get("pawn_yaw") or 0) - wy + 180) % 360 - 180) < 1.0
    line_ok = abs(line_start) <= 3.0 and line_end is not None and abs(line_end) <= 3.0
    row = {"name": n, "dir": key, "walk_yaw": wy, "valid_by_tool": bool(report["valid"]), "invalid_reasons": report["invalid_reasons"], "Wk_12.1": num["Wk"]["W12.1"], "Wk_17.6": num["Wk"]["W17.6"],
           "Wk_L": sp["Wk_L"], "Wk_R": sp["Wk_R"], "asymmetry": sp["asymmetry"], "Sk2_2.5": num["Sk2"]["S2.5"], "pawn_start_xy": start, "pawn_end_xy": end[:2] if end else None, "pawn_yaw_end": state.get("pawn_yaw"),
           "line_error_start_cm": line_start, "line_error_end_cm": line_end, "line_within_3cm": line_ok, "yaw_matches_walk_yaw": yaw_ok,
           "prewalk": {"moved_cm": pre["moved_cm"], "elapsed_s": pre["elapsed_game_seconds"], "timed_out": pre["timed_out"]}, "walk_timed_out": walk["timed_out"],
           "tuning_mismatches": mism, "sun_rotation": variants["base"]["props"]["TDGen_Fog3D_Sun"]["root_component"]["relative_rotation"], "key_errors": result.get("key_errors"),
           "tuning_load_error_count": result["base"]["tuning_reload"]["load"]["error_count"], "editor_log_submitted_rejected": sub}
    row["valid"] = bool(report["valid"] and line_ok and yaw_ok and not mism and not walk["timed_out"] and not pre["timed_out"] and not row["key_errors"] and row["tuning_load_error_count"] == 0 and sub is not None and sub[1] == 0 and not problem_lines)
    rows[n] = row
wk = [rows[n]["Wk_12.1"] for n in names]; mean = sum(wk) / 4; asym = [rows[n]["asymmetry"] for n in names]
cond = {"Wk": dict(zip(names, wk)), "Wk_mean": mean, "Wk_relative_deviation": {n: rows[n]["Wk_12.1"] / mean - 1 for n in names}, "Wk_within_15pct": all(abs(w / mean - 1) <= 0.15 for w in wk),
        "asymmetry": dict(zip(names, asym)), "asymmetry_spread": max(asym) - min(asym), "asymmetry_spread_within_0.10": max(asym) - min(asym) <= 0.10,
        "asymmetry_magnitude_le_0.15": {n: abs(rows[n]["asymmetry"]) <= 0.15 for n in names}, "all_magnitude_le_0.15": all(abs(a) <= 0.15 for a in asym), "all_valid": all(rows[n]["valid"] for n in names)}
cond["met_first_two"] = bool(cond["Wk_within_15pct"] and cond["asymmetry_spread_within_0.10"] and cond["all_valid"])
cond["met"] = bool(cond["met_first_two"] and cond["all_magnitude_le_0.15"])


def pawn_crop(name, time, box):
    return Image.open(CAL / name / "capture" / f"00_c_{int(round(float(time) * 1000)):05d}ms.png").convert("L").crop(box)


def mad(a, b):
    return float(numpy.abs(numpy.asarray(a, dtype=numpy.float64) - numpy.asarray(b, dtype=numpy.float64)).mean())


pose = {}
for t in ("10.0", "10.4", "10.8"):
    pr = [mad(pawn_crop(names[i], t, PAWN_BODY_BOX), pawn_crop(names[j], t, PAWN_BODY_BOX)) for i in range(4) for j in range(i + 1, 4)]
    pose[t] = {"body_pairs_mean": sum(pr) / len(pr), "body_pairs_max": max(pr)}
(OUT / "final_rows.json").write_text(json.dumps({"rows": rows, "conditions": cond, "pose_body_box_mean_abs_luma": pose, "fog3d_problem_lines_in_editor_log": len(problem_lines)}, ensure_ascii=False, indent=1), encoding="utf-8")
for n in names:
    r = rows[n]
    print(f"{n}: valid={r['valid']} tool={r['valid_by_tool']} Wk={r['Wk_12.1']:.3f} L={r['Wk_L']:.3f} R={r['Wk_R']:.3f} asym={r['asymmetry']:+.3f} Sk2={r['Sk2_2.5']:.2f} lineErr={r['line_error_start_cm']:+.2f}/{r['line_error_end_cm']:+.2f} yaw_ok={r['yaw_matches_walk_yaw']} mism={r['tuning_mismatches']} sun={r['sun_rotation']} prewalk={r['prewalk']} log={r['editor_log_submitted_rejected']} {r['invalid_reasons']}")
print(json.dumps(cond, ensure_ascii=False)); print("pose", pose)
try:
    font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 15)
except Exception:
    font = None
def crop_cell(name):
    d = CAL / name / "capture"
    s = Image.open(d / "00_c_12100ms.png").convert("RGB"); ref = Image.open(d / "00_c_19400ms.png").convert("RGB")
    w, h = s.size; top, bottom, left, right = R.walk_region(h, w)
    a = numpy.asarray(s.convert("L"), dtype=numpy.float32); b = numpy.asarray(ref.convert("L"), dtype=numpy.float32)
    diff = Image.fromarray(numpy.clip(numpy.abs(a - b) * 8.0, 0, 255).astype(numpy.uint8)).convert("RGB")
    return s.crop((left, top, right, bottom)), diff.crop((left, top, right, bottom))
cw, ch = crop_cell(names[0])[0].size; gap = 6; lh = 40
img = Image.new("RGB", (4 * cw + 5 * gap, lh + 2 * ch + 2 * gap), (24, 24, 24)); dr = ImageDraw.Draw(img)
for ci, n in enumerate(names):
    x0 = gap + ci * (cw + gap); r = rows[n]; s, df = crop_cell(n)
    dr.text((x0, gap + 2), f"{n} 선 {SPEC['ABCD'[ci]][0]}={SPEC['ABCD'[ci]][1]:.0f} (같은 출발)", fill=(255, 255, 255), font=font)
    dr.text((x0, gap + 20), f"Wk {r['Wk_12.1']:.2f} L {r['Wk_L']:.2f} R {r['Wk_R']:.2f} 치우침 {r['asymmetry']:+.3f}", fill=(255, 220, 120), font=font)
    img.paste(s, (x0, gap + lh)); img.paste(df, (x0, gap + lh + ch))
img.save(OUT / "final_check_crops.png"); print("figure", img.size)
