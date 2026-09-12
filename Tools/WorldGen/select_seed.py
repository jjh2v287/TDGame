"""잿빛 골짜기(Ashen Vale) 후보 시드 생성·검증·추천 (설계서 11장 R-92, 할 일 P3-08의 에디터 밖 버전).

각 시드마다 `generate_ashen_vale.py --seed N --out Saved/WorldGen/Candidates/seed_N` 을 서브프로세스로 실행하고
`validate_world.py` 로 점수를 매긴 뒤 표를 `Saved/WorldGen/candidates.md` (및 candidates.json) 에 쓴다.

실행: python Tools/WorldGen/select_seed.py [--seeds 1-5] [--top 3] [--skip-generate] [--slots 3] [검증 임계값 인자]
  --seeds        "1-5", "1,3,7-9" 형식. 시드당 생성 약 15초(검증 약 3초)이므로 기본은 1-5.
  --skip-generate 이미 있는 후보 폴더를 다시 만들지 않고 검증만 한다.
  --out-root     후보 출력 루트(기본 Saved/WorldGen/Candidates).
정렬: 점수 내림차순, 같은 점수면 실패 항목 수 오름차순, 그다음 시드 오름차순. 상위 --top 개를 추천으로 표시한다.
선택한 시드를 실제로 쓰려면 `generate_ashen_vale.py --seed N` (기본 출력 폴더)을 다시 돌린 뒤 에디터 반영 스크립트를 실행한다.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate_world

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GENERATOR = os.path.join(HERE, "generate_ashen_vale.py")
COUNT_KEYS = ("trees", "bushes", "dread", "bigrocks", "smallrocks", "puddles", "fog", "instances", "actors", "lights", "markers")


def parse_seeds(spec):
    seeds = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            seeds.extend(range(int(a), int(b) + 1))
        else:
            seeds.append(int(part))
    return sorted(set(seeds))


def parse_generator_log(text):
    counts = {}
    for key in COUNT_KEYS:
        m = re.search(rf"\b{key} (\d+)", text)
        if m:
            counts[key] = int(m.group(1))
    m = re.search(r"height range m: (-?[\d.]+) \.\. (-?[\d.]+)", text)
    if m:
        counts["height_min_m"], counts["height_max_m"] = float(m.group(1)), float(m.group(2))
    return counts


def generate(seed, out_dir):
    t0 = time.time()
    proc = subprocess.run([sys.executable, GENERATOR, "--seed", str(seed), "--out", out_dir], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    elapsed = time.time() - t0
    if proc.returncode != 0:
        raise RuntimeError(f"seed {seed} 생성 실패 (코드 {proc.returncode}):\n{proc.stderr[-2000:]}")
    return parse_generator_log(proc.stdout), elapsed


def evaluate(seed, out_dir, opts, skip_generate):
    row = {"seed": seed, "dir": out_dir, "gen_seconds": None, "counts": {}}
    if not skip_generate or not os.path.exists(os.path.join(out_dir, "layout.json")):
        row["counts"], row["gen_seconds"] = generate(seed, out_dir)
        row["gen_seconds"] = round(row["gen_seconds"], 1)
    else:
        log = os.path.join(out_dir, "generator.log")
        if os.path.exists(log):
            row["counts"] = parse_generator_log(open(log, encoding="utf-8").read())
    result = validate_world.run_validation(out_dir, opts)
    validate_world.write_report(result, os.path.join(out_dir, "report.md"))
    with open(os.path.join(out_dir, "validation.json"), "w", encoding="utf-8") as fp:
        json.dump(result, fp, ensure_ascii=False, indent=1, default=float)
    row.update({"score": result["score"], "passed": result["passed"], "failed": result["failed"],
                "check_points": {cid: c["points"] for cid, c in result["checks"].items()}})
    return row


def write_table(rows, top, path):
    ranked = sorted(rows, key=lambda r: (-r["score"], len(r["failed"]), r["seed"]))
    check_ids = [cid for cid, _ in validate_world.CHECKS]
    head = ["순위", "시드", "점수", "통과", "실패 항목"] + [f"{validate_world.NAMES[c]}" for c in check_ids] + ["나무", "덤불", "인스턴스", "액터", "조명", "높이 m", "생성 초"]
    lines = [f"# 후보 시드 표 (시드 {len(rows)}개, 추천 상위 {top}개)", "", "| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    for i, r in enumerate(ranked, 1):
        c = r["counts"]
        hr = f"{c['height_min_m']}..{c['height_max_m']}" if "height_min_m" in c else "-"
        cells = [("**" if i <= top else "") + str(i) + ("**" if i <= top else ""), str(r["seed"]), str(r["score"]), "예" if r["passed"] else "아니오", ", ".join(r["failed"]) or "-"]
        cells += [str(r["check_points"][cid]) for cid in check_ids]
        cells += [str(c.get("trees", "-")), str(c.get("bushes", "-")), str(c.get("instances", "-")), str(c.get("actors", "-")), str(c.get("lights", "-")), hr, str(r["gen_seconds"] if r["gen_seconds"] is not None else "-")]
        lines.append("| " + " | ".join(cells) + " |")
    lines += ["", f"## 추천 상위 {top}", ""]
    for r in ranked[:top]:
        lines.append(f"- 시드 {r['seed']}: 점수 {r['score']}, 실패 {', '.join(r['failed']) or '없음'} — `{r['dir']}`")
    lines += ["", "각 후보 폴더의 `report.md`에 실패 위치(cm)와 원인이 있다. 채택: `python Tools/WorldGen/generate_ashen_vale.py --seed N` 후 에디터 반영."]
    with open(path, "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines) + "\n")
    return ranked, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", default="1-5")
    ap.add_argument("--top", type=int, default=3)
    ap.add_argument("--out-root", dest="out_root", default=os.path.join(ROOT, "Saved", "WorldGen", "Candidates"))
    ap.add_argument("--table", default=os.path.join(ROOT, "Saved", "WorldGen", "candidates.md"))
    ap.add_argument("--skip-generate", dest="skip_generate", action="store_true")
    validate_world.add_threshold_args(ap)
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    seeds = parse_seeds(args.seeds)
    opts = validate_world.opts_from_args(args)
    os.makedirs(args.out_root, exist_ok=True)
    rows = []
    for seed in seeds:
        out_dir = os.path.join(args.out_root, f"seed_{seed}")
        print(f"seed {seed}: 생성{'(건너뜀)' if args.skip_generate else ''}·검증 중 ...", flush=True)
        row = evaluate(seed, out_dir, opts, args.skip_generate)
        rows.append(row)
        print(f"  점수 {row['score']} 실패 {', '.join(row['failed']) or '-'} 나무 {row['counts'].get('trees', '-')} ({row['gen_seconds']} s)", flush=True)
    ranked, lines = write_table(rows, args.top, args.table)
    with open(os.path.splitext(args.table)[0] + ".json", "w", encoding="utf-8") as fp:
        json.dump({"seeds": seeds, "top": [r["seed"] for r in ranked[:args.top]], "rows": ranked}, fp, ensure_ascii=False, indent=1)
    print()
    print("\n".join(lines))
    print("table:", args.table)


if __name__ == "__main__":
    main()
