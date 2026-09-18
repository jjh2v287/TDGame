"""시스템 Python으로 실행(언리얼 에디터 열림 필요): PIE를 켜고 editor_pie_profile.py로 약 11초간 프레임 간격을 재고 `stat dumpframe`·`ProfileGPU` 결과를 로그에서 추려 보여준 뒤 PIE를 끈다. "PIE가 느리다"를 캡·스로틀·게임 스레드·GPU 중 어디인지 한 번에 가른다.
실행: python Tools/pie_profile.py
출력: Saved/AgentOps/pie_profile.json + 표준 출력 요약(정상 구간 평균 프레임, 게임 스레드 상위 항목, GPU 프레임 시간)
상태: 현행 (2026-09-19)
"""
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools'))
import uemcp  # noqa: E402

REPORT = ROOT / 'Saved/AgentOps/pie_profile.json'
LOG = ROOT / 'Saved/Logs/TDGame.log'


def call(session, name, arguments):
    response = uemcp.call_tool(session, 'call_tool', {'toolset_name': 'EditorToolset.EditorAppToolset', 'tool_name': name, 'arguments': arguments})
    if not response or 'error' in response or response.get('result', {}).get('isError'):
        raise RuntimeError(f'{name}: {response}')
    return response['result']


def log_excerpt():
    lines = LOG.read_text(encoding='utf-8', errors='replace').splitlines()
    starts = [index for index, line in enumerate(lines) if 'TD_PROFILE_CMD stat dumpframe' in line]
    if not starts:
        return [], None
    block = lines[starts[-1]:starts[-1] + 400]
    strip = lambda line: re.sub(r'^\[[^\]]*\]\[ *\d+\]', '', line)
    game_thread = []
    capture = False
    for line in block:
        text = strip(line)
        if 'GameThread - STATGROUP_Threads' in text:
            capture = True
            continue
        if capture and ('STATGROUP_Threads' in text or 'LogStats' not in text):
            break
        if capture and 'OtherChildren' not in text and 'CPU Stall' not in text:
            game_thread.append(text.strip()[:140])
    gpu = next((strip(line).strip() for line in block if 'Frame Time' in line and 'LogRHI' in line), None)
    return game_thread[:12], gpu


def main():
    if REPORT.exists():
        REPORT.unlink()
    session = uemcp.connect()
    call(session, 'StartPIE', {'options': {'warmupSeconds': 2.0}})
    try:
        time.sleep(1.0)
        completed = subprocess.run([sys.executable, str(ROOT / 'Tools/run_in_editor.py'), str(ROOT / 'Tools/editor_pie_profile.py')], capture_output=True, text=True, cwd=ROOT)
        if completed.returncode != 0:
            raise RuntimeError(completed.stdout[-1200:] + completed.stderr[-400:])
        for _ in range(90):
            time.sleep(1.0)
            if REPORT.exists():
                break
    finally:
        call(session, 'StopPIE', {})
    if not REPORT.exists():
        raise SystemExit('profile report was not written')
    report = json.loads(REPORT.read_text(encoding='utf-8'))
    game_thread, gpu = log_excerpt()
    report['game_thread_top'] = game_thread
    report['gpu_frame_time'] = gpu
    REPORT.write_text(json.dumps(report, indent=1), encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'game_thread_top'}, indent=1))
    print('\n'.join(game_thread))


if __name__ == '__main__':
    main()
