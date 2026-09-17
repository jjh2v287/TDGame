# -*- coding: utf-8 -*-
"""검 휘두르기 애니메이션을 손·발 목표 위치로 만든다 (PowerShell 에서 실행할 것).

Git Bash 는 `/Game/...` 인수를 윈도 경로로 바꾸므로 반드시 PowerShell 에서 실행한다.

    python Tools/AnimationAuthoring/author_swing.py --preview
    python Tools/AnimationAuthoring/author_swing.py --create
    python Tools/AnimationAuthoring/author_swing.py --create --pose-assets

관절 각도를 직접 적지 않고 **손과 발이 가야 할 위치**를 적는다. 위치는 캐릭터 기준
(오른쪽, 앞, 높이) 센티미터이고, 관절 각도는 `pose_kinematics.solve_chain` 이
관절 가동 범위 안에서 역으로 푼다. 척추 비틀기와 손목 각도만 직접 지정한다.

**손 목표는 루트 기준 상대 위치**다(루트가 전진하면 손도 같이 간다).
**발 목표는 절대 위치**다(루트가 전진해도 디딘 발은 바닥에 남는다). 이것이 루트 모션의
핵심이라 발이 미끄러지지 않는다.

실측한 축 의미:
    상완 upperarm_r  pitch + 오른쪽 위로   yaw + 앞으로
    팔꿈치 lowerarm_r yaw  + 굽힘         roll 비틀기(손 위치 불변)
    척추 spine_*     roll  - 오른쪽으로 감기  roll + 왼쪽으로 풀기
    고관절 thigh_r    yaw  - 앞으로         pitch 벌림
    무릎 calf_r       yaw  + 굽힘 (오른쪽), calf_l 은 부호 반대
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pose_kinematics import ARM_AXES, LEG_L_AXES, LEG_R_AXES, Skeleton, solve_chain
from smoke_test import TDMcpAnimationClient

MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
FOLDER = '/Game/Characters/Mannequins/Anims/Sword'
FPS = 30
GROUND = 8.2  # 레퍼런스 포즈의 발 높이

SLASH_01 = {
    'name': 'AS_Sword_Slash_01',
    'num_frames': 24,
    'root_motion': {'enable': True, 'root_lock': 'RefPose', 'force_root_lock': False},
    # 프레임: 라벨, 척추(spine_03, spine_05) 비틀기, 루트 이동(앞, 높이),
    #         손 목표(오른쪽, 앞, 높이) - 루트 기준, 팔꿈치 선호 굽힘, 손목 roll,
    #         왼발 목표(오른쪽, 앞, 높이) - 절대, 오른발 목표 - 절대
    'keys': {
        0:  ('ready 준비',   -5,  -8, (0, -1),    (30, 33, 112),  45,   0, (-16, 12, GROUND), (16, -10, GROUND)),
        6:  ('windup 감기', -15, -25, (-3, -2),   (44, -22, 158), 70, -25, (-16, 6, GROUND + 4), (17, -14, GROUND)),
        11: ('contact 타격', 12,  25, (14, -6),   (2, 48, 112),  -10,  10, (-17, 32, GROUND), (16, -6, GROUND)),
        15: ('follow 관통',  18,  32, (20, -4),   (-18, 34, 96),  10,  25, (-17, 32, GROUND), (16, 4, GROUND + 2)),
        24: ('recover 복귀', -5,  -8, (24, -1),   (30, 33, 112),  45,   0, (-16, 36, GROUND), (16, 14, GROUND)),
    },
}


def solve_design(skeleton, design):
    """손·발 목표를 본 오프셋과 루트 이동으로 바꾼다."""
    poses, roots, report = {}, {}, []
    for frame in sorted(design['keys']):
        (label, spine_03, spine_05, (root_forward, root_up),
         (right, forward, height), elbow, wrist, left_foot, right_foot) = design['keys'][frame]

        # 루트 이동: 이 스켈레톤은 +Y 가 앞, +Z 가 위다.
        root_shift = (0.0, root_forward, root_up)
        translations = {'root': root_shift}
        offsets = {'spine_03': (0, 0, spine_03), 'spine_05': (0, 0, spine_05)}

        # 손은 루트를 따라간다
        hand_target = (-right, forward + root_forward, height + root_up)
        offsets, _ = solve_chain(
            skeleton, 'hand_r', hand_target, ARM_AXES, base_offsets=offsets, translations=translations,
            preferred={'lowerarm_r': (None, elbow, None), 'clavicle_r': (0, 0, None)})
        offsets['hand_r'] = (0, 0, wrist)

        # 발은 절대 위치라 루트가 전진해도 바닥에 남는다
        errors = {}
        for side, axes, foot, bone in (('L', LEG_L_AXES, left_foot, 'foot_l'), ('R', LEG_R_AXES, right_foot, 'foot_r')):
            target = (-foot[0], foot[1], foot[2])
            # 1차: 무릎 선호 굽힘으로 자세를 잡고, 2차: 벌점 없이 위치만 다듬는다.
            offsets, _ = solve_chain(
                skeleton, bone, target, axes, base_offsets=offsets, translations=translations,
                preferred={'calf_l': (None, -12, None), 'calf_r': (None, 12, None)}, stiffness=0.03)
            offsets, errors[side] = solve_chain(
                skeleton, bone, target, axes, base_offsets=offsets, translations=translations, stiffness=0.0)

        poses[frame] = offsets
        roots[frame] = root_shift
        placed = skeleton.component_transforms(offsets, translations)
        report.append({
            'frame': frame, 'label': label, 'root': root_shift,
            'hand': placed['hand_r'][0], 'foot_l': placed['foot_l'][0], 'foot_r': placed['foot_r'][0],
            'foot_error': errors,
        })
    return poses, roots, report


def build_tracks(poses, roots):
    frames = sorted(poses)
    bones = sorted({bone for pose in poses.values() for bone in pose})
    tracks = [{'bone': 'root', 'keys': [{'frame': f, 'translation': list(roots[f])} for f in frames]}]
    for bone in bones:
        tracks.append({'bone': bone,
                       'keys': [{'frame': f, 'rotation': list(poses[f].get(bone, (0, 0, 0)))} for f in frames]})
    return tracks


def preview(design, report):
    print('%s  %d frames @ %d fps = %.3f s  (루트 모션 %s)' % (
        design['name'], design['num_frames'], FPS, design['num_frames'] / FPS,
        '켬' if design['root_motion']['enable'] else '끔'))
    previous = None
    for row in report:
        moved = ''
        if previous is not None:
            travel = sum((row['hand'][i] - previous[i]) ** 2 for i in range(3)) ** 0.5
            moved = ' 손이동%5.1f' % travel
        print('  f%-3d %-13s 루트앞%+6.1f 높이%+5.1f | 손 오른쪽%+6.1f 앞%+6.1f 높이%6.1f%s'
              ' | 왼발 앞%+6.1f 높이%5.1f(오차%4.1f) 오른발 앞%+6.1f 높이%5.1f(오차%4.1f)' % (
                  row['frame'], row['label'], row['root'][1], row['root'][2],
                  -row['hand'][0], row['hand'][1], row['hand'][2], moved,
                  row['foot_l'][1], row['foot_l'][2], row['foot_error']['L'],
                  row['foot_r'][1], row['foot_r'][2], row['foot_error']['R']))
        previous = row['hand']


def create(client, asset_path, num_frames, poses, roots, root_motion, save=True):
    request = {
        'asset_path': asset_path, 'skeletal_mesh': MESH, 'fps': FPS,
        'num_frames': num_frames, 'mode': 'reference_offset', 'save': save,
        'tracks': build_tracks(poses, roots),
    }
    if root_motion:
        request['root_motion'] = root_motion
    result = client.request('.TDAnimationAuthoringTools.CreateBoneAnimation', request)
    print('  %s -> %s  루트모션=%s %s' % (asset_path, result.get('success'),
                                        result.get('root_motion_enabled'), result.get('error', '')))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preview', action='store_true')
    parser.add_argument('--create', action='store_true')
    parser.add_argument('--pose-assets', action='store_true', help='키 포즈마다 정지 에셋을 메모리에만 만든다')
    parser.add_argument('--no-save', action='store_true')
    args = parser.parse_args()

    client = TDMcpAnimationClient()
    skeleton = Skeleton.from_mcp(client, MESH)
    design = SLASH_01
    poses, roots, report = solve_design(skeleton, design)
    preview(design, report)

    if args.create:
        create(client, '%s/%s' % (FOLDER, design['name']), design['num_frames'],
               poses, roots, design['root_motion'], not args.no_save)
    if args.pose_assets:
        print('키 포즈 정지 에셋 (저장하지 않음):')
        for frame in sorted(poses):
            if frame == max(poses):
                continue
            create(client, '%s/Preview/AS_Pose_f%02d' % (FOLDER, frame), 1,
                   {0: poses[frame], 1: poses[frame]}, {0: roots[frame], 1: roots[frame]}, None, save=False)
    return 0


if __name__ == '__main__':
    sys.exit(main())
