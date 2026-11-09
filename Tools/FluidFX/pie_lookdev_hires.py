"""실행 환경: 시스템 Python(pie_lookdev_fluid.py 와 같음: 언리얼 에디터가 TDGame 을 열고 MCP 포트 8000·원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). 같은 폴더의 잠금 도구 pie_lookdev_fluid.py 를 importlib 로 불러 모듈 전역 SHOT_SIZE 만 바꾸고 나머지 인수를 그 main() 에 그대로 넘긴다(잠금 도구 파일은 바꾸지 않음).
실행 명령: python -B Tools/FluidFX/pie_lookdev_hires.py [--shot-size 1920x1080] --variants <변형 JSON> [pie_lookdev_fluid.py 의 나머지 인수] (/Game 경로 인수가 있으므로 PowerShell, L-repo-01)
출력: pie_lookdev_fluid.py 와 같다(--output-dir 의 장별 PNG·grid.png·result.json). 촬영은 HighResShot <--shot-size> 로 요청된다.
상태: 실험(roadmap-v2 2.4 T1). 1080p 결과는 눈 판정용이고 지표 띠 판정은 720p 를 쓴다."""
import argparse
import importlib
import re
import sys
from pathlib import Path

LOOKDEV_MODULE_NAME = "pie_lookdev_fluid"
DEFAULT_SHOT_SIZE = "1920x1080"
SHOT_SIZE_PATTERN = re.compile(r"^[1-9][0-9]{2,4}x[1-9][0-9]{2,4}$")


def parse_shot_size(text):
    if not SHOT_SIZE_PATTERN.match(text):
        raise argparse.ArgumentTypeError(f"--shot-size 는 <가로>x<세로> 꼴이어야 한다: {text}")
    return text


def split_arguments(argv):
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--shot-size", type=parse_shot_size, default=DEFAULT_SHOT_SIZE)
    known, remaining = parser.parse_known_args(argv)
    return known.shot_size, remaining


def load_lookdev_module():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    return importlib.import_module(LOOKDEV_MODULE_NAME)


def main():
    shot_size, remaining = split_arguments(sys.argv[1:])
    if "-h" in remaining or "--help" in remaining:
        print(f"pie_lookdev_hires.py 전용 인수: --shot-size <가로>x<세로> (기본 {DEFAULT_SHOT_SIZE}). 나머지는 pie_lookdev_fluid.py 인수다.")
    lookdev = load_lookdev_module()
    lookdev.SHOT_SIZE = shot_size
    print(f"촬영 크기: {lookdev.SHOT_SIZE}", flush=True)
    sys.argv = [str(Path(__file__).resolve())] + remaining
    return lookdev.main()


if __name__ == "__main__":
    sys.exit(main())
