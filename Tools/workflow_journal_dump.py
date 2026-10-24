"""Workflow 실행 기록(journal.jsonl)의 에이전트 결과를 라벨별 파일로 나눠 쓴다.
실행 환경: 아무 Python 3 (에디터 불필요)
실행 명령: python Tools/workflow_journal_dump.py <journal.jsonl 또는 실행 폴더> <출력 폴더>
출력: <출력 폴더>/<라벨>.md (구조화 결과는 들여쓴 JSON) + 표준 출력에 라벨·상태·글자 수 목록
상태: 2026-10-23 claude, 무한 세계 가능성 검토에서 작성
"""
import json
import os
import re
import sys


def load_entries(journal_path):
    with open(journal_path, encoding="utf-8") as journal_file:
        for line in journal_file:
            line = line.strip()
            if line:
                yield json.loads(line)


def resolve_journal_path(path):
    if os.path.isdir(path):
        return os.path.join(path, "journal.jsonl")
    return path


def to_text(value):
    if isinstance(value, str):
        return value
    return json.dumps(value, ensure_ascii=False, indent=2)


def safe_name(label):
    return re.sub(r"[^0-9A-Za-z가-힣._-]+", "_", label) or "agent"


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    journal_path = resolve_journal_path(sys.argv[1])
    output_dir = sys.argv[2]
    os.makedirs(output_dir, exist_ok=True)

    labels_by_key = {}
    written_names = {}
    for entry in load_entries(journal_path):
        entry_type = entry.get("type")
        key = entry.get("key")
        if entry_type == "started" and key:
            labels_by_key[key] = entry.get("label") or entry.get("agentId") or "agent"
            continue
        if entry_type in ("launched", "started") or not key:
            continue
        label = labels_by_key.get(key, entry.get("label") or key[-12:])
        payload = {name: value for name, value in entry.items() if name not in ("key",)}
        result = payload.get("result", payload)
        name = safe_name(label)
        count = written_names.get(name, 0)
        written_names[name] = count + 1
        if count:
            name = f"{name}-{count + 1}"
        text = to_text(result)
        with open(os.path.join(output_dir, name + ".md"), "w", encoding="utf-8", newline="\n") as output_file:
            output_file.write(text)
        print(f"{name}\t{entry_type}\t{len(text)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
