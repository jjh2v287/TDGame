"""시스템 Python으로 실행: git log 의 커밋을 영역(유체·안개 / 게임플레이 / 기타)별로 나눠 커밋 수와 바뀐 줄 수 비율을 표로 출력한다. 한 커밋이 여러 영역에 걸치면 줄 수 비율로 나눈다(틀 몰두 방지 주간 집계).
실행: python Tools/week_ratio.py [--since "7 days ago"] [--root <저장소 루트>] [--json] [--include-merges] (PowerShell 또는 Git Bash, 에디터 불필요)
출력: 머지 커밋은 기본 제외(--include-merges 로 포함), 바뀐 줄이 없는 커밋은 기타 1건으로 센다. 머리줄 커밋 수와 표의 커밋 수 합은 같다. 영역별 커밋 수(소수, 줄 수 비율로 배분)·바뀐 줄 수·비율 표, --json 이면 같은 내용의 JSON. 종료 코드 0 / 2 git 실행 실패
상태: 현행
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[1]
FLUID_FOG_PREFIXES = (
    "Source/TDGame/FluidFX/",
    "Source/TDFluidGpu/",
    "Plugins/TDFogScattering/",
    "Docs/FluidFX/",
    "Docs/VolumeFog3D/",
    "Docs/FogScattering/",
    "Tools/FluidFX/",
)
GAMEPLAY_PREFIXES = (
    "Source/TDGame/Foundation/",
    "Source/TDGame/Framework/",
    "Source/TDGame/Characters/",
    "Source/TDGame/Combat/",
    "Source/TDGame/MonsterAI/",
    "Source/TDGame/World/",
    "Source/TDGame/Actors/",
    "Docs/Gameplay",
)
AREA_NAMES = ("유체·안개", "게임플레이", "기타")
RENAME_PATTERN = re.compile(r"\{[^{}]* => ([^{}]*)\}")


def classify_path(path):
    if path.startswith(FLUID_FOG_PREFIXES):
        return AREA_NAMES[0]
    if path.startswith(GAMEPLAY_PREFIXES):
        return AREA_NAMES[1]
    return AREA_NAMES[2]


def normalize_path(raw_path):
    if " => " not in raw_path:
        return raw_path
    if "{" in raw_path:
        return RENAME_PATTERN.sub(r"\1", raw_path).replace("//", "/")
    return raw_path.split(" => ")[-1]


def read_commit_line_counts(root, since, include_merges=False):
    command = ["git", "-C", str(root), "log", f"--since={since}", "--numstat", "--no-renames", "--format=%x01%H"]
    if not include_merges:
        command.insert(4, "--no-merges")
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(result.stderr.strip(), file=sys.stderr)
        return None
    commits = []
    for block in result.stdout.split("\x01"):
        lines = [line for line in block.splitlines() if line.strip()]
        if not lines:
            continue
        counts = dict.fromkeys(AREA_NAMES, 0)
        for line in lines[1:]:
            parts = line.split("\t", 2)
            if len(parts) != 3 or not parts[0].isdigit() or not parts[1].isdigit():
                continue
            counts[classify_path(normalize_path(parts[2]))] += int(parts[0]) + int(parts[1])
        commits.append(counts)
    return commits


def summarize(commits):
    commit_share = dict.fromkeys(AREA_NAMES, 0.0)
    line_total = dict.fromkeys(AREA_NAMES, 0)
    for counts in commits:
        commit_lines = sum(counts.values())
        for area, lines in counts.items():
            line_total[area] += lines
            if commit_lines:
                commit_share[area] += lines / commit_lines
        if not commit_lines:
            commit_share[AREA_NAMES[2]] += 1
    all_commits = sum(commit_share.values())
    all_lines = sum(line_total.values())
    return [
        {
            "area": area,
            "commits": round(commit_share[area], 2),
            "commit_ratio": round(commit_share[area] / all_commits, 3) if all_commits else 0.0,
            "changed_lines": line_total[area],
            "line_ratio": round(line_total[area] / all_lines, 3) if all_lines else 0.0,
        }
        for area in AREA_NAMES
    ]


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="영역별 커밋·줄 수 비율 집계")
    parser.add_argument("--since", default="7 days ago")
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--include-merges", action="store_true")
    arguments = parser.parse_args()
    commits = read_commit_line_counts(arguments.root, arguments.since, arguments.include_merges)
    if commits is None:
        return 2
    rows = summarize(commits)
    if arguments.json:
        print(json.dumps({"since": arguments.since, "commit_count": len(commits), "areas": rows}, ensure_ascii=False, indent=1))
        return 0
    print(f"since={arguments.since} commits={len(commits)}")
    print(f"{'영역':<10}{'커밋 수':>9}{'커밋 비율':>10}{'바뀐 줄':>11}{'줄 비율':>9}")
    for row in rows:
        print(f"{row['area']:<10}{row['commits']:>9.2f}{row['commit_ratio']:>10.1%}{row['changed_lines']:>11}{row['line_ratio']:>9.1%}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
