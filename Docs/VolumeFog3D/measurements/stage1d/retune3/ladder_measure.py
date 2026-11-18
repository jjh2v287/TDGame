"""실행 환경: 파이썬 3 + numpy, 작업 폴더 C:/Project/TDGame (PYTHONUTF8=1 python -B)
실행 명령: python -B Docs/VolumeFog3D/measurements/stage1d/retune3/ladder_measure.py <실행이름...>
출력: 표준 출력에 실행마다 Wk(12.1)·좌우 치우침·Sk2(2.5)·남는 비를 적고 같은 폴더 ladder_rows.json 에 쓴다
상태: 1d 값 맞추기(prereg-1d-diag.md 4절) 사다리 단계용. 식은 fog3d_calib_report 그대로."""
import sys, json
from pathlib import Path
ROOT = Path("C:/Project/TDGame")
sys.path.insert(0, str(ROOT / "Tools")); sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import fog3d_calib_report as R
CAL = ROOT / "Saved/FluidFX/fog3d_calib"
opts = R.parse_arguments(["--runs", "x", "--no-images", "--out-root", str(CAL)])
rows = {}
for n in sys.argv[1:]:
    report, matched, rec, lumas = R.analyze_run(CAL / n, opts)
    num = report["numbers"]; sp = R.split_walk_numbers(lumas); wk = num["Wk"]
    rows[n] = {"valid": bool(report["valid"]), "usable": report.get("usable"), "invalid_reasons": report["invalid_reasons"],
               "log": {k: report["log"].get(k) for k in ("submitted", "rejected", "fog3d_warning_or_error_lines")},
               "Wk_12.1": wk["W12.1"], "Wk_17.6": wk["W17.6"], "residual_rain": wk["W17.6"] / wk["W12.1"] if wk["W12.1"] else None,
               "Sk2_2.5": num["Sk2"]["S2.5"], "Sk_2.5": num["Sk"]["S2.5"], **sp}
    r = rows[n]
    print(f"{n}: valid={r['valid']} Wk12.1={r['Wk_12.1']:.3f} Wk_L={r['Wk_L']:.3f} Wk_R={r['Wk_R']:.3f} asym={r['asymmetry']:+.3f} Wk17.6={r['Wk_17.6']:.3f} rain={r['residual_rain']:.3f} Sk2={r['Sk2_2.5']:.2f} log={r['log']} {r['invalid_reasons']}")
(Path(__file__).resolve().parent / "ladder_rows.json").write_text(json.dumps(rows, ensure_ascii=False, indent=1), encoding="utf-8")
