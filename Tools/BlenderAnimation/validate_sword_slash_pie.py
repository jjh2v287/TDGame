"""시스템 Python으로 실행(언리얼 에디터 열림 필요): PIE를 켜고 editor_pie_capture_sword_slash.py로 검 횡베기 몽타주를 실제 플레이어에 재생해 재생 시간·슬롯 가중치·루트 모션 이동 거리를 기록한 뒤 PIE를 끈다.
실행: python Tools/BlenderAnimation/validate_sword_slash_pie.py
출력: Docs/Validation/BlenderAnimation/sword-slash-pie.json, Docs/Validation/BlenderAnimation/sword-slash-pie.png
상태: 현행 (2026-09-19)
"""
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tools'))
import uemcp  # noqa: E402

REPORT = ROOT / 'Docs/Validation/BlenderAnimation/sword-slash-pie.json'
SCREENSHOT = ROOT / 'Saved/Screenshots/WindowsEditor/sword_slash_contact.png'


def call(session, name, arguments):
    response = uemcp.call_tool(session, 'call_tool', {'toolset_name': 'EditorToolset.EditorAppToolset', 'tool_name': name, 'arguments': arguments})
    if not response or 'error' in response or response.get('result', {}).get('isError'):
        raise RuntimeError(f'{name}: {response}')
    return response['result']


def main():
    if REPORT.exists():
        REPORT.unlink()
    if SCREENSHOT.exists():
        SCREENSHOT.unlink()
    session = uemcp.connect()
    call(session, 'StartPIE', {'options': {'warmupSeconds': 2.0}})
    try:
        time.sleep(1.0)
        completed = subprocess.run([sys.executable, str(ROOT / 'Tools/run_in_editor.py'), str(ROOT / 'Tools/BlenderAnimation/editor_pie_capture_sword_slash.py')],
                                   capture_output=True, text=True, cwd=ROOT)
        if completed.returncode != 0:
            raise RuntimeError(completed.stdout[-1500:] + completed.stderr[-500:])
        for _ in range(30):
            time.sleep(0.5)
            if REPORT.exists():
                break
        time.sleep(1.0)
    finally:
        call(session, 'StopPIE', {})
    if not REPORT.exists():
        raise SystemExit('PIE report was not written')
    report = json.loads(REPORT.read_text(encoding='utf-8'))
    if SCREENSHOT.exists():
        shutil.copy(SCREENSHOT, REPORT.with_suffix('.png'))
        report['screenshot'] = str(REPORT.with_suffix('.png').relative_to(ROOT))
        REPORT.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'samples'}, indent=2))
    if not report.get('passed'):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
