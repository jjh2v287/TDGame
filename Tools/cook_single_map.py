"""시스템 Python으로 실행(에디터가 꺼져 있어야 함): RunUAT BuildCookRun으로 Win64 Development 게임을 빌드하고 맵 하나만 쿠킹·스테이징·pak 한 뒤, 결과 요약을 남긴다. 쿠킹된 실행·프레임 확인은 이어서 measure_game_frames.py 로 한다.
실행: python Tools/cook_single_map.py --label baseline [--map /Game/Level/LV-Cambat]
출력: Saved/AgentOps/cook_<label>.json (종료 코드, 소요 초, 경고·오류 줄 상위 40개) + Saved/Logs/cook_<label>.log(전체 UAT 출력), 스테이징 Saved/StagedBuilds/Windows
상태: 현행 (2026-09-29)
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools'))
import ue_editor  # noqa: E402

RUN_UAT = Path(ue_editor.ENGINE) / 'Engine/Build/BatchFiles/RunUAT.bat'
PROJECT = ROOT / 'TDGame.uproject'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--label', required=True)
    parser.add_argument('--map', default='/Game/Level/LV-Cambat')
    parser.add_argument('--timeout', type=int, default=7200)
    args = parser.parse_args()

    log_path = ROOT / f'Saved/Logs/cook_{args.label}.log'
    log_path.parent.mkdir(parents=True, exist_ok=True)
    command = [str(RUN_UAT), 'BuildCookRun', f'-project={PROJECT}', '-noP4', '-platform=Win64',
               '-clientconfig=Development', '-build', '-cook', f'-map={args.map}', '-stage', '-pak',
               '-unattended', '-utf8output', '-nocompileeditor']
    started = time.time()
    with log_path.open('w', encoding='utf-8', errors='replace') as log:
        try:
            completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            exit_code = completed.returncode
        except subprocess.TimeoutExpired:
            exit_code = 'timeout'
    lines = log_path.read_text(encoding='utf-8', errors='replace').splitlines()
    problems = [line.strip()[:300] for line in lines if ('Error:' in line or 'error ' in line.lower() or 'Warning:' in line)]
    report = {'label': args.label, 'map': args.map, 'exit_code': exit_code,
              'seconds': round(time.time() - started), 'log': str(log_path),
              'error_count': sum(1 for line in problems if 'rror' in line),
              'warning_count': sum(1 for line in problems if 'Warning:' in line),
              'first_problems': problems[:40],
              'success_line': next((line for line in reversed(lines) if 'BUILD SUCCESSFUL' in line or 'AutomationTool exiting with ExitCode' in line), None)}
    output = ROOT / f'Saved/AgentOps/cook_{args.label}.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('label', 'exit_code', 'seconds', 'error_count', 'warning_count', 'success_line')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
