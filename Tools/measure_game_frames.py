"""시스템 Python으로 실행: cook_single_map.py가 만든 쿠킹 스테이징 실행 파일(Saved/StagedBuilds/Windows/TDGame.exe)로 맵을 띄워 CSV 프로파일러로 프레임을 잡고, 워밍업 뒤 구간의 프레임·게임 스레드·렌더 스레드·GPU 시간과 크래시 줄을 요약한다(에디터 바이너리 -game은 이 환경에서 로그 없이 즉시 종료돼 쓰지 않는다).
실행: python Tools/measure_game_frames.py --label baseline [--map /Game/Level/LV-Cambat] [--frames 1500] [--warmup 450]
출력: Saved/AgentOps/frames_<label>.json (평균·중앙값·95분위 ms, 종료 코드, 로그의 Fatal/Crash 줄) + 표준 출력 요약
상태: 현행 (2026-09-29)
"""
import argparse
import csv
import json
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'TDGame.uproject'
STAGED_ROOT = ROOT / 'Saved/StagedBuilds/Windows'
COLUMNS = ['FrameTime', 'GameThreadTime', 'RenderThreadTime', 'GPUTime']


def summarize(values):
    ordered = sorted(values)
    return {
        'mean': round(statistics.fmean(ordered), 3),
        'p50': round(ordered[len(ordered) // 2], 3),
        'p95': round(ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))], 3),
    }


def is_number(text):
    try:
        float(text)
        return True
    except (TypeError, ValueError):
        return False


def read_csv_rows(csv_path):
    csv.field_size_limit(2**31 - 1)
    with csv_path.open(encoding='utf-8', errors='replace', newline='') as handle:
        rows = list(csv.DictReader(handle))
    return [row for row in rows if is_number(row.get('FrameTime'))]


def newest_file(folder, pattern, since):
    candidates = [path for path in folder.glob(pattern) if path.stat().st_mtime >= since]
    return max(candidates, key=lambda path: path.stat().st_mtime) if candidates else None


def crash_lines(log_path):
    if not log_path or not log_path.exists():
        return ['로그 없음']
    markers = ('Fatal error', '=== Critical error', 'Unhandled Exception', 'Assertion failed')
    lines = log_path.read_text(encoding='utf-8', errors='replace').splitlines()
    return [line.strip()[:300] for line in lines if any(marker in line for marker in markers)][:20]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--label', required=True)
    parser.add_argument('--map', default='/Game/Level/LV-Cambat')
    parser.add_argument('--frames', type=int, default=1500)
    parser.add_argument('--warmup', type=int, default=450)
    parser.add_argument('--timeout', type=int, default=900)
    args = parser.parse_args()

    started = time.time()
    log_name = f'frames_{args.label}.log'
    common = [args.map, '-windowed', '-ResX=1280', '-ResY=720', '-NoSound', '-unattended',
              f'-csvCaptureFrames={args.frames}', '-ExitAfterCsvProfiling', f'-log={log_name}']
    exe = STAGED_ROOT / 'TDGame.exe'
    command = [str(exe)] + common
    saved = STAGED_ROOT / 'TDGame/Saved'
    if not exe.exists():
        raise SystemExit(f'실행 파일 없음: {exe}')

    try:
        completed = subprocess.run(command, timeout=args.timeout)
        exit_code = completed.returncode
    except subprocess.TimeoutExpired:
        exit_code = 'timeout'

    csv_path = newest_file(saved / 'Profiling/CSV', '*.csv', started)
    log_path = newest_file(saved / 'Logs', '*.log', started)
    report = {'label': args.label, 'map': args.map, 'exit_code': exit_code,
              'csv': str(csv_path) if csv_path else None, 'log': str(log_path) if log_path else None,
              'crash_lines': crash_lines(log_path)}
    if csv_path:
        rows = read_csv_rows(csv_path)[args.warmup:]
        report['frames_measured'] = len(rows)
        for column in COLUMNS:
            values = [float(row[column]) for row in rows if row.get(column) not in (None, '')]
            if values:
                report[column] = summarize(values)

    output = ROOT / f'Saved/AgentOps/frames_{args.label}.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
