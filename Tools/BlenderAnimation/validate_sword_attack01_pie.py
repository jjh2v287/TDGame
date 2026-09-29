"""시스템 Python으로 실행(언리얼 에디터 열림 필요): PIE를 켜고 editor_pie_capture_sword_attack01.py로 검 공격 몽타주를 실제 플레이어에 재생해 재생 시간·슬롯 가중치·루트 모션 이동 거리·타격 시점 스크린샷을 기록한 뒤 PIE를 끈다.
실행: python Tools/BlenderAnimation/validate_sword_attack01_pie.py
출력: Docs/Validation/BlenderAnimation/sword-attack01-pie.json, Docs/Validation/BlenderAnimation/sword-attack01-pie-contact.png
상태: 현행 (2026-09-25)
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

REPORT = ROOT / 'Docs/Validation/BlenderAnimation/sword-attack01-pie.json'
SCREENSHOTS = ROOT / 'Saved/Screenshots/WindowsEditor'
LABELS = ['contact']


def call(session, name, arguments):
    response = uemcp.call_tool(session, 'call_tool', {'toolset_name': 'EditorToolset.EditorAppToolset', 'tool_name': name, 'arguments': arguments})
    if not response or 'error' in response or response.get('result', {}).get('isError'):
        raise RuntimeError(f'{name}: {response}')
    return response['result']


def main():
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    if REPORT.exists():
        REPORT.unlink()
    for label in LABELS:
        (SCREENSHOTS / f'sword_attack01_{label}.png').unlink(missing_ok=True)
    session = uemcp.connect()
    call(session, 'StartPIE', {'options': {'warmupSeconds': 2.0}})
    try:
        time.sleep(1.0)
        completed = subprocess.run([sys.executable, str(ROOT / 'Tools/run_in_editor.py'), str(ROOT / 'Tools/BlenderAnimation/editor_pie_capture_sword_attack01.py')],
                                   capture_output=True, text=True, cwd=ROOT)
        if completed.returncode != 0:
            raise RuntimeError(completed.stdout[-1500:] + completed.stderr[-500:])
        for _ in range(40):
            time.sleep(0.5)
            if REPORT.exists():
                break
        time.sleep(1.5)
    finally:
        call(session, 'StopPIE', {})
    if not REPORT.exists():
        raise SystemExit('PIE report was not written')
    report = json.loads(REPORT.read_text(encoding='utf-8'))
    report['screenshot_files'] = []
    for label in LABELS:
        source = SCREENSHOTS / f'sword_attack01_{label}.png'
        if source.exists():
            target = REPORT.with_name(f'sword-attack01-pie-{label}.png')
            shutil.copy(source, target)
            report['screenshot_files'].append(str(target.relative_to(ROOT)))
    REPORT.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'samples'}, indent=2))
    if not report.get('passed'):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
