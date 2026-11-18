import sys, json
from pathlib import Path
ROOT = Path("C:/Project/TDGame")
sys.path.insert(0, str(ROOT / "Tools")); sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_report as R
import fog3d_calib_clips as C
CAL = ROOT / "Saved/FluidFX/fog3d_calib"
names = sys.argv[2:]
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
opts = R.parse_arguments(["--runs", "x", "--no-images", "--out-root", str(CAL)])
rows = []
for n in names:
    report, matched, rec, lumas = R.analyze_run(CAL / n, opts)
    num = report["numbers"]; sp = R.split_walk_numbers(lumas)
    wk = num["Wk"]
    rows.append({"name": n, "valid": bool(report["valid"]), "usable": report.get("usable"), "invalid_reasons": report["invalid_reasons"],
        "log": {k: report["log"].get(k) for k in ("submitted", "rejected", "fog3d_warning_or_error_lines")},
        "Wk_12.1": wk["W12.1"], "Wk_17.6": wk["W17.6"], "residual_rain": wk["W17.6"] / wk["W12.1"] if wk["W12.1"] else None,
        "Sk2_2.5": num["Sk2"]["S2.5"], "Sk_2.5": num["Sk"]["S2.5"], "Tx2": num["Tx2"], **sp})
(out / "measure.json").write_text(json.dumps({"rows": rows}, ensure_ascii=False, indent=1), encoding="utf-8")
for r in rows:
    print(f"{r['name']}: valid={r['valid']} usable={r['usable']} Wk12.1={r['Wk_12.1']:.3f} Wk_L={r['Wk_L']:.3f} Wk_R={r['Wk_R']:.3f} asym={r['asymmetry']:+.3f} Wk17.6={r['Wk_17.6']:.3f} rain={r['residual_rain']:.3f} Sk2={r['Sk2_2.5']:.2f} Tx2={r['Tx2']:.3f} log={r['log']} {r['invalid_reasons']}")
w = [r["Wk_12.1"] for r in rows]
if len(rows) == 3:
    m = sum(w) / 3
    print("M", round(m, 3), "dev%", [round((x / m - 1) * 100, 1) for x in w], "asym", [round(r["asymmetry"], 3) for r in rows])
if "--figure" in sys.argv[:2] or True:
    C.ROUND2_ASYMMETRY_ROWS = tuple(names)
    files, missing = C.round2_figure_asymmetry(out, CAL, out)
    print("figure", files, missing)
