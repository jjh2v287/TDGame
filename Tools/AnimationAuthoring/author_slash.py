# -*- coding: utf-8 -*-
"""기존 공격 애니메이션을 바탕에 깔고 오른팔만 검 궤적으로 바꿔 새 AnimSequence 를 만든다.

    python Tools/AnimationAuthoring/dump_animation.py --out base.json /Game/.../MM_Attack_01
    python Tools/AnimationAuthoring/author_slash.py --base base.json --preview
    python Tools/AnimationAuthoring/author_slash.py --base base.json --create

PowerShell 에서 실행할 것. Git Bash 는 `/Game/...` 인수를 윈도 경로로 바꾼다.

왜 이렇게 하나: 손으로 키를 몇 개 찍어 선형 보간하면 등속이라 로봇처럼 보인다. 실제
공격 애니메이션은 감기 8프레임이 1~3 m/s 로 느리다가 타격 4프레임에 19 m/s 까지 치솟고,
그 뒤 몸이 미끄러지며 정착한다. 그 타이밍과 하체·척추·루트 모션을 그대로 물려받고
**오른팔만** 다시 풀면 무게감이 붙는다.

손 목표는 **어깨 기준 상대 위치**다(오른쪽, 앞, 높이). 몸이 크게 이동·회전하므로 절대
좌표로 적으면 팔이 몸을 따라가지 못한다. 키 사이는 Catmull-Rom 곡선으로 잇고, 구간 안의
시간 배분은 **기준 애니메이션의 손 이동량**을 그대로 쓴다. 그래서 이징이 실제 모션이다.
"""
import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anim_report import component_positions, load_skeleton_order
from pose_kinematics import ARM_AXES, Skeleton, euler_from_quaternion, solve_chain
from smoke_test import TDMcpAnimationClient

MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
ARM_BONES = ('clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r')

# 프레임: (라벨, 손 목표(어깨 기준: 오른쪽, 앞, 높이), 팔꿈치 선호 굽힘, 손목 roll)
SLASH_KEYS = {
    0:  ('guard 겨눔',    (22, 20, 10),   40,   0),
    8:  ('windup 감기',   (18, -22, 30),  65, -30),
    13: ('contact 타격',  (-28, 38, -17), -5,  15),
    20: ('follow 관통',   (-26, 28, -19),  5,  25),
    30: ('settle 정착',   (8, 38, -14),   30,   5),
}


def catmull_rom(points, t):
    """키 포인트들을 지나는 곡선. t 는 0..len(points)-1 구간 파라미터."""
    if len(points) < 2:
        return points[0]
    index = max(0, min(len(points) - 2, int(math.floor(t))))
    local = t - index
    p0 = points[max(0, index - 1)]
    p1, p2 = points[index], points[index + 1]
    p3 = points[min(len(points) - 1, index + 2)]
    out = []
    for axis in range(3):
        a, b, c, d = p0[axis], p1[axis], p2[axis], p3[axis]
        out.append(0.5 * ((2 * b) + (-a + c) * local
                          + (2 * a - 5 * b + 4 * c - d) * local * local
                          + (-a + 3 * b - 3 * c + d) * local ** 3))
    return tuple(out)


def base_easing(placed_by_frame, key_frames):
    """구간별로 기준 애니메이션의 손 이동량을 누적해 0..1 진행도를 만든다."""
    progress = {}
    for start, end in zip(key_frames, key_frames[1:]):
        lengths, total = {start: 0.0}, 0.0
        previous = placed_by_frame[start]['hand_r'][0]
        for frame in range(start + 1, end + 1):
            current = placed_by_frame[frame]['hand_r'][0]
            total += math.dist(current, previous)
            lengths[frame] = total
            previous = current
        for frame, value in lengths.items():
            progress[frame] = (start, end, value / total if total > 1e-6 else
                               (frame - start) / max(1, end - start))
    progress[key_frames[-1]] = (key_frames[-2], key_frames[-1], 1.0)
    return progress


def build(skeleton, base, keys, elbow_scale=1.0):
    """프레임별 최종 로컬 트랜스폼을 만든다. {프레임: {본: (translation, euler)}}"""
    parents, order = skeleton.parent, skeleton.order
    raw_bones = [b for b in order if b in base['frames']['0']]
    num_frames = base['num_frames']

    placed = {f: component_positions(base['frames'][str(f)], parents, order) for f in range(num_frames + 1)}
    key_frames = sorted(keys)
    progress = base_easing(placed, key_frames)
    targets = [keys[f][1] for f in key_frames]
    elbows = [keys[f][2] for f in key_frames]
    wrists = [keys[f][3] for f in key_frames]

    out, report = {}, []
    for frame in range(num_frames + 1):
        start, end, ratio = progress.get(frame, (key_frames[-2], key_frames[-1], 1.0))
        segment = key_frames.index(start)
        hand_local = catmull_rom(targets, segment + ratio)
        elbow = (elbows[segment] + (elbows[segment + 1] - elbows[segment]) * ratio) * elbow_scale
        wrist = wrists[segment] + (wrists[segment + 1] - wrists[segment]) * ratio

        # 팔 4본은 기준 애니메이션 포즈를 빼고 레퍼런스에서 다시 푼다. 그래야 관절
        # 가동 범위(레퍼런스 기준으로 실측한 값)가 그대로 맞는다. 어깨 위치와 방향은
        # 부모인 척추가 기준 애니메이션에서 오므로 몸을 그대로 따라간다.
        base_locals = {b: (tuple(base['frames'][str(frame)][b][0:3]),
                           tuple(base['frames'][str(frame)][b][3:7]))
                       for b in raw_bones if b not in ARM_BONES}
        shoulder = placed[frame]['upperarm_r'][0]
        target = (shoulder[0] - hand_local[0], shoulder[1] + hand_local[1], shoulder[2] + hand_local[2])
        offsets, error = solve_chain(
            skeleton, 'hand_r', target, ARM_AXES, base_locals=base_locals,
            preferred={'lowerarm_r': (None, elbow, None), 'clavicle_r': (0, 0, None)}, stiffness=0.05)
        offsets, error = solve_chain(
            skeleton, 'hand_r', target, ARM_AXES, base_offsets=offsets, base_locals=base_locals, stiffness=0.0)
        offsets['hand_r'] = (0, 0, wrist)

        pose = {}
        for bone in raw_bones:
            translation = tuple(base['frames'][str(frame)][bone][0:3])
            rotation = skeleton.local_rotation(bone, offsets.get(bone), base_locals)
            pose[bone] = (translation, euler_from_quaternion(rotation))
        out[frame] = pose
        reached = skeleton.component_transforms(offsets, None, base_locals)['hand_r'][0]
        report.append({'frame': frame, 'error': error, 'world': reached,
                       'hand': (-(reached[0] - shoulder[0]), reached[1] - shoulder[1], reached[2] - shoulder[2]),
                       'root': placed[frame]['root'][0]})
    return out, report


def to_tracks(poses):
    frames = sorted(poses)
    bones = sorted(poses[frames[0]])
    tracks = []
    for bone in bones:
        keys = []
        for frame in frames:
            translation, euler = poses[frame][bone]
            keys.append({'frame': frame, 'translation': [round(v, 4) for v in translation],
                         'rotation': [round(v, 4) for v in euler]})
        tracks.append({'bone': bone, 'keys': keys})
    return tracks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--base', required=True, help='dump_animation.py 가 만든 기준 애니메이션 JSON')
    parser.add_argument('--base-name', help='JSON 안의 애니메이션 이름 (기본: 첫 항목)')
    parser.add_argument('--asset', default='/Game/Characters/Mannequins/Anims/Sword/AS_Sword_Slash_01')
    parser.add_argument('--create', action='store_true')
    parser.add_argument('--no-save', action='store_true')
    args = parser.parse_args()

    client = TDMcpAnimationClient()
    skeleton = Skeleton.from_mcp(client, MESH)
    dump = json.loads(Path(args.base).read_text(encoding='utf-8'))
    name = args.base_name or sorted(dump)[0]
    base = dump[name]
    print('기준: %s  %d프레임  %.2f초  루트모션 %s' % (name, base['num_frames'], base['length'], base['root_motion']))

    poses, report = build(skeleton, base, SLASH_KEYS)
    fps = base['fps']
    print('프레임별 손 위치(어깨 기준)와 속도:')
    previous = None
    peak, worst = 0.0, 0.0
    for row in report:
        speed = 0.0
        if previous is not None:
            speed = math.dist(row['world'], previous) * fps / 100.0  # 몸 이동까지 포함한 실제 속도
        peak = max(peak, speed)
        worst = max(worst, row['error'])
        if row['frame'] % 2 == 0 or speed > 12:
            bar = '#' * int(round(min(speed, 22.0) / 22.0 * 30))
            print('  f%-3d 오른쪽%+6.1f 앞%+6.1f 높이%+6.1f  %5.1f m/s |%-30s| 오차%4.1f' % (
                row['frame'], row['hand'][0], row['hand'][1], row['hand'][2], speed, bar, row['error']))
        previous = row['world']
    print('손 최고속도 %.1f m/s (기준 MM_Attack_01 은 19.4 m/s) | 최대 도달 오차 %.1f cm' % (peak, worst))

    if args.create:
        result = client.request('.TDAnimationAuthoringTools.CreateBoneAnimation', {
            'asset_path': args.asset, 'skeletal_mesh': MESH, 'fps': fps,
            'num_frames': base['num_frames'], 'mode': 'local_absolute', 'save': not args.no_save,
            'root_motion': {'enable': True, 'root_lock': 'RefPose', 'force_root_lock': False},
            'tracks': to_tracks(poses),
        })
        print('생성: %s  루트모션=%s %s' % (result.get('success'), result.get('root_motion_enabled'),
                                       result.get('error', '')))
        if result.get('success'):
            print('  ', result['asset_path'], '%.3f초' % result.get('duration', 0))
        return 0 if result.get('success') else 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
