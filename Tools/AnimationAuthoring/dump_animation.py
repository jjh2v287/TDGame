# -*- coding: utf-8 -*-
"""AnimSequence 의 본 포즈를 JSON 으로 덤프한다 (PowerShell 에서 실행).

    python Tools/AnimationAuthoring/dump_animation.py --out dump.json ^
        /Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01 ^
        /Game/Characters/Mannequins/Anims/Sword/AS_Sword_Slash_01

에디터 안에서 돌릴 코드를 여기서 만들어 `Tools/run_in_editor.py` 로 보낸다. 환경 변수는
에디터 프로세스로 전달되지 않고, run_in_editor 는 긴 파일을 경로로 오인하므로 짧은
코드를 생성해 넘기는 방식이 안전하다.

덤프한 JSON 은 `anim_report.py` 가 읽어 타이밍 프로파일과 스틱 피겨를 만든다. 기존
애니메이션을 참고 자료로 읽을 때도 같은 경로를 쓴다.
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

EDITOR_TEMPLATE = '''import unreal, json
paths = {paths!r}
out = {out!r}
data = {{}}
for path in paths:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    frames_count = unreal.AnimationLibrary.get_num_frames(asset)
    bones = [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(asset)]
    frames = {{}}
    for frame in range(frames_count + 1):
        entry = {{}}
        for bone in bones:
            t = unreal.AnimationLibrary.get_bone_pose_for_frame(asset, bone, frame, False)
            l, q = t.translation, t.rotation
            entry[bone] = [l.x, l.y, l.z, q.x, q.y, q.z, q.w]
        frames[str(frame)] = entry
    length = asset.get_play_length()
    data[path.rsplit('/', 1)[-1]] = {{
        'path': path, 'num_frames': frames_count, 'length': length,
        'fps': int(round(frames_count / length)) if length else 30,
        'root_motion': bool(asset.get_editor_property('enable_root_motion')),
        'bones': bones, 'frames': frames,
    }}
    print('dumped %s: %d frames, %.2f s, root motion %s' % (path, frames_count, length, data[path.rsplit('/', 1)[-1]]['root_motion']))
open(out, 'w', encoding='utf-8').write(json.dumps(data))
print('wrote', out)
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('animations', nargs='+', help='/Game 애니메이션 경로')
    parser.add_argument('--out', required=True, help='쓸 JSON 경로')
    args = parser.parse_args()

    out = str(Path(args.out).resolve())
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    code = EDITOR_TEMPLATE.format(paths=list(args.animations), out=out)
    with tempfile.NamedTemporaryFile('w', suffix='.py', delete=False, encoding='utf-8') as handle:
        handle.write(code)
        script = handle.name
    try:
        result = subprocess.run([sys.executable, str(ROOT / 'Tools' / 'run_in_editor.py'), script],
                                cwd=str(ROOT), capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        return result.returncode
    finally:
        Path(script).unlink(missing_ok=True)


if __name__ == '__main__':
    sys.exit(main())
