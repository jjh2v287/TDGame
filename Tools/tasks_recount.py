"""Docs/Tasks/phase-*.md 의 '- 상태:' 줄을 세어 맨 위 상태 표를 다시 쓴다 (결정론적 문서 정리).

실행: python Tools/tasks_recount.py [파일...]  (인자 없으면 phase-*.md 전부)
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORDER = ["todo", "doing", "done", "decision", "blocked"]


def recount(path):
    text = open(path, encoding="utf-8").read()
    counts = {}
    for m in re.finditer(r"^- 상태: *([A-Za-z]+)", text, re.M):
        counts[m.group(1)] = counts.get(m.group(1), 0) + 1
    rows = "\n".join(f"| {k} | {counts[k]} |" for k in ORDER if k in counts)
    new = re.sub(r"\| 상태 \| 개수 \|\n\|---\|---\|\n(?:\|[^\n]*\|\n)+", f"| 상태 | 개수 |\n|---|---|\n{rows}\n", text, count=1)
    if new != text:
        open(path, "w", encoding="utf-8", newline="\n").write(new)
    print(os.path.relpath(path, ROOT), counts)


if __name__ == "__main__":
    files = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, "Docs", "Tasks", "phase-*.md")))
    for f in files:
        recount(f)
