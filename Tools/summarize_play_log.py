"""시스템 Python으로 실행: 사건 기록기(Saved/Telemetry/*.jsonl)를 읽어 사건 종류별 개수, 플레이어 팀(1)이 준·받은 피해 합, 사망 수, 조우 수·길이, 세션 길이를 표로 요약한다(FD-05).
실행: python Tools/summarize_play_log.py <파일|폴더>... [--json] [--player-team 1] [--self-test] (PowerShell 또는 Git Bash, 에디터 불필요)
출력: 표준 출력에 표(--json 이면 같은 내용의 JSON). 같은 입력은 늘 같은 출력이며 깨진 줄은 세어서 보고하고 건너뛴다. 종료 코드 0 / 2 읽을 파일 없음
상태: 현행
"""
import argparse
import contextlib
import io
import json
import sys
from pathlib import Path

DEFAULT_PLAYER_TEAM = 1
MONSTER_TEAM = 2
UTF8_BOM = b"\xef\xbb\xbf"
SELF_TEST_DIR = Path(__file__).resolve().parent / "fixtures" / "summarize_play_log"
SELF_TEST_EXPECTED = {
    "files": 1,
    "sessions": 1,
    "broken_lines": 2,
    "session_seconds": 2.0,
    "events": {"damage": 2, "session": 1},
    "damage_dealt": 5.0,
    "damage_taken": 3.0,
    "player_deaths": 0,
    "monster_deaths": 0,
    "encounters_started": 0,
    "encounters_finished": 0,
    "encounter_victories": 0,
    "encounter_seconds_mean": 0.0,
}


def collect_files(inputs):
    files = []
    for entry in inputs:
        path = Path(entry)
        if path.is_dir():
            files.extend(sorted(path.glob("*.jsonl")))
        elif path.is_file():
            files.append(path)
    return sorted(set(files), key=lambda item: str(item))


def decode_line(raw_line):
    try:
        return raw_line.decode("utf-8").strip()
    except UnicodeDecodeError:
        return None


def parse_record(text):
    try:
        record = json.loads(text)
    except ValueError:
        return None
    if not isinstance(record, dict):
        return None
    return record


def read_records(path):
    records = []
    broken = 0
    with path.open("rb") as handle:
        for index, raw_line in enumerate(handle):
            if index == 0 and raw_line.startswith(UTF8_BOM):
                raw_line = raw_line[len(UTF8_BOM):]
            text = decode_line(raw_line)
            if text is None:
                broken += 1
                continue
            if not text:
                continue
            record = parse_record(text)
            if record is None:
                broken += 1
                continue
            records.append(record)
    return records, broken


def number(record, key):
    value = record.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return 0.0
    return float(value)


def summarize(files, player_team):
    summary = {
        "files": len(files),
        "sessions": 0,
        "broken_lines": 0,
        "session_seconds": 0.0,
        "events": {},
        "damage_dealt": 0.0,
        "damage_taken": 0.0,
        "player_deaths": 0,
        "monster_deaths": 0,
        "encounters_started": 0,
        "encounters_finished": 0,
        "encounter_victories": 0,
        "encounter_seconds": [],
    }
    for path in files:
        records, broken = read_records(path)
        summary["broken_lines"] += broken
        encounter_starts = {}
        last_time = 0.0
        for record in records:
            kind = record.get("ev")
            if not isinstance(kind, str):
                continue
            time_seconds = number(record, "t")
            last_time = max(last_time, time_seconds)
            summary["events"][kind] = summary["events"].get(kind, 0) + 1
            if kind == "session":
                summary["sessions"] += 1
            elif kind == "damage":
                amount = number(record, "amount")
                if record.get("src_team") == player_team:
                    summary["damage_dealt"] += amount
                if record.get("dst_team") == player_team:
                    summary["damage_taken"] += amount
            elif kind == "death":
                if record.get("dst_team") == player_team:
                    summary["player_deaths"] += 1
                elif record.get("dst_team") == MONSTER_TEAM:
                    summary["monster_deaths"] += 1
            elif kind == "encounter_banner":
                encounter = str(record.get("enc", ""))
                if encounter not in encounter_starts:
                    encounter_starts[encounter] = time_seconds
                    summary["encounters_started"] += 1
            elif kind == "encounter_end":
                summary["encounters_finished"] += 1
                if record.get("victory") is True:
                    summary["encounter_victories"] += 1
                encounter = str(record.get("enc", ""))
                if encounter in encounter_starts:
                    summary["encounter_seconds"].append(time_seconds - encounter_starts.pop(encounter))
        summary["session_seconds"] += last_time
    return summary


def finalize(summary):
    lengths = summary["encounter_seconds"]
    result = dict(summary)
    result["events"] = dict(sorted(summary["events"].items()))
    result["session_seconds"] = round(summary["session_seconds"], 3)
    result["damage_dealt"] = round(summary["damage_dealt"], 3)
    result["damage_taken"] = round(summary["damage_taken"], 3)
    result["encounter_seconds"] = [round(value, 3) for value in lengths]
    result["encounter_seconds_mean"] = round(sum(lengths) / len(lengths), 3) if lengths else 0.0
    return result


def render_table(result):
    lines = [
        "항목 | 값",
        "--- | ---",
        f"파일 | {result['files']}",
        f"세션 | {result['sessions']}",
        f"깨진 줄 | {result['broken_lines']}",
        f"세션 길이 합(초) | {result['session_seconds']}",
        f"준 피해 합 | {result['damage_dealt']}",
        f"받은 피해 합 | {result['damage_taken']}",
        f"플레이어 사망 | {result['player_deaths']}",
        f"몬스터 사망 | {result['monster_deaths']}",
        f"조우 시작 | {result['encounters_started']}",
        f"조우 끝 | {result['encounters_finished']}",
        f"조우 승리 | {result['encounter_victories']}",
        f"조우 길이 평균(초) | {result['encounter_seconds_mean']}",
        f"조우 길이 목록(초) | {', '.join(str(value) for value in result['encounter_seconds']) or '-'}",
        "",
        "사건 종류 | 개수",
        "--- | ---",
    ]
    for kind, count in result["events"].items():
        lines.append(f"{kind} | {count}")
    return "\n".join(lines)


def run_cli(argv):
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = main(argv)
    return code, buffer.getvalue()


def self_test():
    failures = []
    files = collect_files([str(SELF_TEST_DIR)])
    if not files:
        print(f"FAIL: 시험 입력이 없다: {SELF_TEST_DIR}", file=sys.stderr)
        return 1

    code_first, json_first = run_cli(["--json", str(SELF_TEST_DIR)])
    code_second, json_second = run_cli(["--json", str(SELF_TEST_DIR)])
    _, table_first = run_cli([str(SELF_TEST_DIR)])
    _, table_second = run_cli([str(SELF_TEST_DIR)])

    if code_first != 0 or code_second != 0:
        failures.append(f"종료 코드가 0이 아니다: {code_first}, {code_second}")
    if json_first != json_second:
        failures.append("두 번 실행한 JSON 출력이 다르다")
    if table_first != table_second:
        failures.append("두 번 실행한 표 출력이 다르다")

    result = json.loads(json_first)
    for key, expected in SELF_TEST_EXPECTED.items():
        if result.get(key) != expected:
            failures.append(f"{key}: 기대 {expected}, 실제 {result.get(key)}")

    for message in failures:
        print(f"FAIL: {message}", file=sys.stderr)
    if failures:
        return 1
    print("PASS: 깨진 UTF-8 줄 1·깨진 JSON 줄 1 을 세고 건너뛰었고, 정상 3줄 집계와 두 번 실행 출력이 같다")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="사건 기록기 JSONL 요약")
    parser.add_argument("inputs", nargs="*", help="JSONL 파일 또는 폴더")
    parser.add_argument("--json", action="store_true", help="JSON 으로 출력")
    parser.add_argument("--player-team", type=int, default=DEFAULT_PLAYER_TEAM, help="플레이어 팀 번호(기본 1)")
    parser.add_argument("--self-test", action="store_true", help="fixtures/summarize_play_log 로 자체 시험")
    args = parser.parse_args(argv)

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if args.self_test:
        return self_test()
    if not args.inputs:
        parser.error("JSONL 파일 또는 폴더를 주거나 --self-test 를 써라")

    files = collect_files(args.inputs)
    if not files:
        print("읽을 JSONL 파일이 없다", file=sys.stderr)
        return 2
    result = finalize(summarize(files, args.player_team))
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        print(render_table(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
