"""실행 환경: 파이썬 3 + numpy + Pillow, 작업 폴더 C:/Project/TDGame (PYTHONUTF8=1 python -B)
실행 명령: python -B Docs/VolumeFog3D/measurements/stage1d/diag/diag_measure.py
출력: 같은 폴더의 measure.json, diag_crops.png (앞 촬영 E_px·E_nx·E_py 와 이번 G1·G2·U0 의 숫자·그림)
상태: 1d 진단(prereg-1d-diag.md 2~3절)용. 앞 촬영과 같은 스크립트(fog3d_calib_report)·영역·식을 쓴다."""
import sys, json
from pathlib import Path
import numpy
from PIL import Image, ImageDraw
ROOT = Path("C:/Project/TDGame")
sys.path.insert(0, str(ROOT / "Tools")); sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_report as R
CAL = ROOT / "Saved/FluidFX/fog3d_calib"
OUT = Path(__file__).resolve().parent
NAMES = ["E_px", "E_nx", "E_py", "G1_px", "G1_nx", "G1_py", "G2_s11", "G2_s23", "U0", "G1_px_b", "G0_px_b"]
EDITOR_RUN_ORDER = ["G1_px", "G1_nx", "G1_py", "G2_s11", "G2_s23", "U0", "G1_px_b", "G0_px_b"]
import re
log_text = (ROOT / "Saved/Logs/TDGame.log").read_text(encoding="utf-8", errors="replace")
summaries = [(int(a), int(b)) for a, b in re.findall(r"Fog3D zone summary: submitted=(\d+) rejected=(\d+)", log_text)]
fog3d_problem_lines = len(re.findall(r"LogTDFog3D\w*: (?:Warning|Error)", log_text))
opts = R.parse_arguments(["--runs", "x", "--no-images", "--out-root", str(CAL)])
rows = []
for n in NAMES:
    report, matched, rec, lumas = R.analyze_run(CAL / n, opts)
    num = report["numbers"]; sp = R.split_walk_numbers(lumas)
    h, w = lumas["W12.1"].shape
    top, bottom, left, right = R.walk_region(h, w)
    wk = num["Wk"]
    editor_log = None
    if n in EDITOR_RUN_ORDER and len(summaries) == len(EDITOR_RUN_ORDER):
        sub, rej = summaries[EDITOR_RUN_ORDER.index(n)]
        editor_log = {"submitted": sub, "rejected": rej, "fog3d_warning_or_error_lines_whole_editor_log": fog3d_problem_lines}
    rows.append({"name": n, "editor_log_per_run": editor_log, "valid": bool(report["valid"]), "usable": report.get("usable"), "invalid_reasons": report["invalid_reasons"],
        "log": {k: report["log"].get(k) for k in ("submitted", "rejected", "fog3d_warning_or_error_lines")},
        "Wk_12.1": wk["W12.1"], "Wk_17.6": wk["W17.6"], "residual_rain": wk["W17.6"] / wk["W12.1"] if wk["W12.1"] else None,
        "Sk2_2.5": num["Sk2"]["S2.5"], "Sk_2.5": num["Sk"]["S2.5"], "Tx2": num["Tx2"],
        "walk_region_mean_brightness_ref_W19.4": float(lumas["W19.4"][top:bottom, left:right].mean() * 255.0),
        "walk_region_mean_brightness_W12.1": float(lumas["W12.1"][top:bottom, left:right].mean() * 255.0), **sp})
(OUT / "measure.json").write_text(json.dumps({"rows": rows}, ensure_ascii=False, indent=1), encoding="utf-8")
for r in rows:
    print(f"{r['name']}: valid={r['valid']} usable={r['usable']} Wk12.1={r['Wk_12.1']:.3f} Wk_L={r['Wk_L']:.3f} Wk_R={r['Wk_R']:.3f} asym={r['asymmetry']:+.3f} Wk17.6={r['Wk_17.6']:.3f} rain={r['residual_rain']:.3f} Sk2={r['Sk2_2.5']:.2f} refY={r['walk_region_mean_brightness_ref_W19.4']:.1f} log={r['log']} {r['invalid_reasons']}")

def crop_cell(name):
    d = CAL / name / "capture"
    stopped = Image.open(d / "00_c_12100ms.png").convert("RGB")
    ref = Image.open(d / "00_c_19400ms.png").convert("RGB")
    w, h = stopped.size
    top, bottom, left, right = R.walk_region(h, w)
    a = numpy.asarray(stopped.convert("L"), dtype=numpy.float32); b = numpy.asarray(ref.convert("L"), dtype=numpy.float32)
    diff = numpy.clip(numpy.abs(a - b) * 8.0, 0, 255).astype(numpy.uint8)
    diff_img = Image.fromarray(diff).convert("RGB")
    return stopped.crop((left, top, right, bottom)), diff_img.crop((left, top, right, bottom))
by = {r["name"]: r for r in rows}
grid = [("얼룩 켬(앞 촬영)", ["E_px", "E_nx", "E_py"]), ("얼룩 끔(G1)", ["G1_px", "G1_nx", "G1_py"])]
cw, ch = crop_cell("E_px")[0].size
label_h = 40; gap = 6
img = Image.new("RGB", (3 * cw + 4 * gap, 2 * (label_h + 2 * ch + gap) + gap), (24, 24, 24))
draw = ImageDraw.Draw(img)
try:
    from PIL import ImageFont
    font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 15)
except Exception:
    font = None
for ri, (title, names) in enumerate(grid):
    y0 = gap + ri * (label_h + 2 * ch + gap)
    for ci, n in enumerate(names):
        x0 = gap + ci * (cw + gap)
        s, d = crop_cell(n)
        r = by[n]
        draw.text((x0, y0 + 2), f"{title} {n}", fill=(255, 255, 255), font=font)
        draw.text((x0, y0 + 18), f"Wk {r['Wk_12.1']:.2f}  L {r['Wk_L']:.2f} R {r['Wk_R']:.2f}  치우침 {r['asymmetry']:+.3f}", fill=(255, 220, 120), font=font)
        img.paste(s, (x0, y0 + label_h)); img.paste(d, (x0, y0 + label_h + ch))
img.save(OUT / "diag_crops.png")
print("figure", (OUT / "diag_crops.png").as_posix(), img.size)
