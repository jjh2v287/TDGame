"""실행 환경: 시스템 Python. 에디터 로그(Saved/Logs/TDGame.log)에서 탐침이 남긴 GF07_CONSOLE_BEGIN/END 사이의 `ListTextures streaming -csv` 표를 읽어 스냅샷끼리 텍스처 현재 크기 변화를 비교한다.
실행 명령: python -B Tools/Gameplay/floor_popping_texture_diff.py --run <run 이름> [--log Saved/Logs/TDGame.log] [--top 25]
출력: Saved/Gameplay/FloorPopping/<run>/texture_snapshots.json(스냅샷별 텍스처 표)·texture_diff.md(스냅샷 사이 커진 텍스처 상위) + 표준 출력 요약
상태: GF-07 바닥 팝핑 원인 진단(2026-11-20). 스냅샷 지점은 경로 burst_probe 의 console 구간.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUTPUT_BASE = os.path.join(ROOT, "Saved", "Gameplay", "FloorPopping")
DEFAULT_LOG = os.path.join(ROOT, "Saved", "Logs", "TDGame.log")
BEGIN_PATTERN = re.compile(r"GF07_CONSOLE_BEGIN (\S+) (.*)$")
END_PATTERN = re.compile(r"GF07_CONSOLE_END (\S+)")
LOG_PREFIX_PATTERN = re.compile(r"^\[[^\]]*\]\[[^\]]*\](?:[A-Za-z0-9_]+: )?")
COLUMN_COUNT = 16


def strip_log_prefix(line):
    return LOG_PREFIX_PATTERN.sub("", line.rstrip("\r\n"))


def parse_texture_row(text):
    fields = [field.strip() for field in text.split(",")]
    if len(fields) < COLUMN_COUNT or not fields[0].isdigit() or not fields[4].isdigit():
        return None
    return {
        "max_size": [int(fields[0]), int(fields[1])], "max_kb": int(fields[2]), "current_size": [int(fields[4]), int(fields[5])],
        "current_kb": int(fields[6]), "format": fields[7], "lod_group": fields[8], "name": fields[9], "streaming": fields[10],
        "num_mips": int(fields[14]) if fields[14].isdigit() else None,
    }


def read_snapshots(log_path, markers):
    wanted = set(markers)
    snapshots = {}
    current = None
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            text = strip_log_prefix(line)
            begin = BEGIN_PATTERN.search(text)
            if begin and begin.group(1) in wanted:
                current = begin.group(1)
                snapshots[current] = {}
                continue
            end = END_PATTERN.search(text)
            if end and end.group(1) == current:
                current = None
                continue
            if current is None:
                continue
            row = parse_texture_row(text)
            if row is not None:
                snapshots[current][row["name"]] = row
    return snapshots


def diff_snapshots(before, after, top):
    grown = []
    for name, row in after.items():
        previous = before.get(name)
        previous_kb = previous["current_kb"] if previous else 0
        delta = row["current_kb"] - previous_kb
        if delta > 0:
            grown.append((delta, name, previous["current_size"] if previous else [0, 0], row["current_size"], row["max_size"], row["lod_group"]))
    grown.sort(reverse=True)
    return grown[:top], sum(item[0] for item in grown)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", required=True)
    parser.add_argument("--log", default=DEFAULT_LOG)
    parser.add_argument("--top", type=int, default=25)
    args = parser.parse_args()
    run_dir = os.path.join(OUTPUT_BASE, args.run)
    with open(os.path.join(run_dir, "result.json"), encoding="utf-8") as handle:
        result = json.load(handle)
    entries = result.get("console_log", [])
    if not entries:
        print("console_log 없음")
        return 1
    snapshots = read_snapshots(args.log, [entry["marker"] for entry in entries])
    with open(os.path.join(run_dir, "texture_snapshots.json"), "w", encoding="utf-8") as handle:
        json.dump(snapshots, handle, ensure_ascii=False)
    lines = [f"# {args.run} 텍스처 스냅샷 비교", ""]
    for entry in entries:
        rows = snapshots.get(entry["marker"], {})
        lines.append(f"- {entry['marker']}: 텍스처 {len(rows)}개, 현재 합 {sum(row['current_kb'] for row in rows.values()) / 1024:.1f}MB")
    for first, second in zip(entries, entries[1:]):
        before, after = snapshots.get(first["marker"], {}), snapshots.get(second["marker"], {})
        grown, total = diff_snapshots(before, after, args.top)
        lines += ["", f"## {first['marker']} → {second['marker']}: 커진 합 {total / 1024:.1f}MB", "", "| 늘어난 KB | 텍스처 | 전 | 후 | 최대 | 그룹 |", "|---|---|---|---|---|---|"]
        lines += [f"| {delta} | {name} | {a[0]}x{a[1]} | {b[0]}x{b[1]} | {m[0]}x{m[1]} | {group} |" for delta, name, a, b, m, group in grown]
    text = "\n".join(lines) + "\n"
    with open(os.path.join(run_dir, "texture_diff.md"), "w", encoding="utf-8") as handle:
        handle.write(text)
    print(text[:4000])
    return 0


if __name__ == "__main__":
    sys.exit(main())
