"""여러 시드로 던전을 생성·검증·채점해 후보 표를 만든다 (언리얼 모듈 사용 안 함).

사용:
  python Tools/DungeonGen/batch_dungeons.py --seeds 1-20 --flow Branch --size Medium [--theme Crypt] [--flow all]

각 시드는 Saved/DungeonGen/batch_<flow>_<size>/seed_<N>/ 에 layout.json, preview.png, report.md 를 쓰고,
표와 상위 3개 추천은 Saved/DungeonGen/candidates_<flow>_<size>.md 에 쓴다. --flow all 은 흐름 5종을 차례로 돌린다.
점수: 검증 실패 0점. 통과 시 100 - 막다른 길 비율×40 + 분기 수(최대 3)×5 + 루프 수(최대 2)×5 - |주경로 비율-0.65|×60.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import generate_dungeon  # noqa: E402

ROOT = generate_dungeon.ROOT


def parse_seeds(text):
    seeds = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            low, high = part.split("-", 1)
            seeds.extend(range(int(low), int(high) + 1))
        else:
            seeds.append(int(part))
    return seeds


def score_candidate(output):
    validation = output["validation"]
    if not validation["passed"]:
        return 0.0
    metrics = validation["metrics"]
    score = 100.0
    score -= metrics.get("deadend_ratio", 0.0) * 40.0
    score += min(metrics.get("branch_count", 0), 3) * 5.0
    score += min(max(metrics.get("loop_count", 0), 0), 2) * 5.0
    score -= abs(metrics.get("main_path_ratio", 0.0) - 0.65) * 60.0
    return round(score, 1)


def run_batch(theme, flow, size, seeds):
    batch_dir = os.path.join(ROOT, "Saved", "DungeonGen", "batch_%s_%s" % (flow, size))
    rows = []
    for seed in seeds:
        output = generate_dungeon.generate(theme, flow, size, seed)
        if output is None:
            rows.append({"seed": seed, "passed": False, "failed": "layout_unsolved", "score": 0.0})
            continue
        generate_dungeon.write_outputs(output, os.path.join(batch_dir, "seed_%d" % seed))
        metrics = output["validation"]["metrics"]
        failed = [c["name"] for c in output["validation"]["checks"] if not c["passed"]]
        rows.append({
            "seed": seed,
            "passed": output["validation"]["passed"],
            "failed": ",".join(failed) or "-",
            "rooms": metrics["room_count"],
            "modules": metrics["module_count"],
            "branches": metrics["branch_count"],
            "loops": metrics["loop_count"],
            "deadend_ratio": metrics.get("deadend_ratio", 0.0),
            "path_rooms": metrics.get("main_path_rooms", 0),
            "path_modules": metrics.get("main_path_modules", 0),
            "restarts": output["layout_restarts"],
            "score": score_candidate(output),
        })
    return batch_dir, rows


def write_candidates(theme, flow, size, rows, path):
    passed = [r for r in rows if r["passed"]]
    lines = []
    lines.append("# 던전 후보: %s %s %s (시드 %d개, 통과 %d개 = %.0f%%)" % (
        theme, flow, size, len(rows), len(passed), 100.0 * len(passed) / max(1, len(rows))))
    lines.append("")
    lines.append("| seed | 통과 | 실패 항목 | 룸 | 모듈 | 분기 | 루프 | 막다른길 비율 | 주경로(룸/모듈) | 재시도 | 점수 |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        lines.append("| %d | %s | %s | %s | %s | %s | %s | %s | %s/%s | %s | %.1f |" % (
            r["seed"], "PASS" if r["passed"] else "FAIL", r["failed"], r.get("rooms", "-"), r.get("modules", "-"),
            r.get("branches", "-"), r.get("loops", "-"), r.get("deadend_ratio", "-"), r.get("path_rooms", "-"),
            r.get("path_modules", "-"), r.get("restarts", "-"), r["score"]))
    lines.append("")
    lines.append("## 추천 상위 3")
    top = sorted(passed, key=lambda r: (-r["score"], r["seed"]))[:3]
    if not top:
        lines.append("- 통과한 후보가 없다.")
    for r in top:
        lines.append("- seed %d: 점수 %.1f, 룸 %d, 분기 %d, 루프 %d, 막다른길 %.2f, 주경로 %d룸 -> Saved/DungeonGen/batch_%s_%s/seed_%d/" % (
            r["seed"], r["score"], r["rooms"], r["branches"], r["loops"], r["deadend_ratio"], r["path_rooms"], flow, size, r["seed"]))
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return len(passed)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seeds", default="1-10")
    parser.add_argument("--flow", default="Linear", choices=generate_dungeon.FLOWS + ["all"])
    parser.add_argument("--size", default="Medium", choices=sorted(generate_dungeon.SIZE_NODE_RANGE))
    parser.add_argument("--theme", default="Crypt", choices=sorted(generate_dungeon.THEMES))
    args = parser.parse_args(argv)
    seeds = parse_seeds(args.seeds)
    flows = generate_dungeon.FLOWS if args.flow == "all" else [args.flow]
    for flow in flows:
        _, rows = run_batch(args.theme, flow, args.size, seeds)
        path = os.path.join(ROOT, "Saved", "DungeonGen", "candidates_%s_%s.md" % (flow, args.size))
        passed = write_candidates(args.theme, flow, args.size, rows, path)
        print("%-8s %-6s pass %2d/%2d (%.0f%%) -> %s" % (flow, args.size, passed, len(rows), 100.0 * passed / max(1, len(rows)), path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
