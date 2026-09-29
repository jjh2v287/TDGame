"""Blender MCP로 bpy 스크립트 파일을 실행하되 TD_ 네임스페이스 값을 앞에 주입한다(call_tool.py --code 는 인수를 못 넘기므로).
실행: python Tools/BlenderAnimation/blender_run.py <script.py> [TD_NAME=<파이썬 리터럴> ...]  예) TD_ACTION='"TD_Ref_Attack_PrimaryA"' TD_STEP=2
출력: Blender 쪽 print 출력(마지막 2000자)과 종료 코드(0 성공, 1 실패). 전체 응답은 Saved/BlenderAnimation/last-call.json
상태: 현행 (2026-09-25)
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PYTHON = ROOT / 'Tools/BlenderMCP/.venv/Scripts/python.exe'
CALL_TOOL = ROOT / 'Tools/BlenderMCP/call_tool.py'
WORK = ROOT / 'Saved/BlenderAnimation'


def build_code(script, assignments):
    lines = []
    for assignment in assignments:
        name, _, value = assignment.partition('=')
        if not name.startswith('TD_') or not value:
            raise SystemExit(f'Expected TD_NAME=value, got {assignment}')
        lines.append(f'{name} = {value}')
    path = Path(script).resolve()
    lines.append(f"exec(compile(open(r'{path}', encoding='utf-8-sig').read(), r'{path}', 'exec'))")
    return '\n'.join(lines)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    WORK.mkdir(parents=True, exist_ok=True)
    arguments = WORK / 'last-call-arguments.json'
    arguments.write_text(json.dumps({'code': build_code(sys.argv[1], sys.argv[2:])}), encoding='utf-8')
    output = WORK / 'last-call.json'
    completed = subprocess.run([str(PYTHON), str(CALL_TOOL), '--arguments', str(arguments), '--output', str(output)], capture_output=True, text=True, encoding='utf-8', errors='replace')
    text = ''
    if output.exists():
        result = json.loads(output.read_text(encoding='utf-8'))
        text = '\n'.join(item.get('text', '') for item in result.get('content', []))
    print((text or completed.stdout + completed.stderr)[-2000:])
    return completed.returncode


raise SystemExit(main())
