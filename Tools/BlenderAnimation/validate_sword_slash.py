"""시스템 Python으로 실행(언리얼 에디터 열림 필요): 가져온 검 횡베기 AnimSequence의 본 위치를 Blender 저작 기록(JSON)과 프레임별로 비교하고 루트 전진·발 접지·검 끝 높이를 검사한다.
실행: python Tools/BlenderAnimation/validate_sword_slash.py [--name AS_TD_Player_Attack01_SwordSlash_RToL]
출력: Docs/Validation/BlenderAnimation/sword-slash-validation.json (기술 검증만, 시각 승인과 별개)
상태: 현행 (2026-09-19)
"""
import argparse
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tools/AnimationAuthoring'))
from smoke_test import TDMcpAnimationClient  # noqa: E402

MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
FOLDER = '/Game/Characters/Mannequins/Anims/Blender'


def unreal_from_character(point):
    return [point[0], point[1], point[2]]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name', default='AS_TD_Player_Attack01_SwordSlash_RToL')
    args = parser.parse_args()
    source = json.loads((ROOT / 'AnimationSources/Player' / (args.name + '.json')).read_text(encoding='utf-8'))
    records = source['records']
    fps = source['summary']['fps']
    client = TDMcpAnimationClient()
    bones = ['root', 'pelvis', 'hand_r', 'hand_l', 'foot_r', 'foot_l', 'ball_r', 'ball_l']
    sampled = client.call('.TDBlenderAnimationTools.sample_animation_poses', animation_path=f'{FOLDER}/{args.name}',
                          skeletal_mesh_path=MESH, bone_names=bones, sample_times=[row['frame'] / fps for row in records])
    errors = {}
    for row, sample in zip(records, sampled['samples']):
        root_fwd = row['root_fwd_cm']
        expected = {'pelvis': row['pelvis'], 'hand_r': row['hand_r'], 'hand_l': row['hand_l'],
                    'foot_r': row['feet']['r']['ankle'], 'foot_l': row['feet']['l']['ankle'],
                    'ball_r': row['feet']['r']['ball'], 'ball_l': row['feet']['l']['ball']}
        root = sample['bones']['root']['component']['translation']
        errors.setdefault('root_fwd', []).append(abs(root[1] - root_fwd))
        for bone, point in expected.items():
            observed = sample['bones'][bone]['component']['translation']
            unreal_point = [-point[1], point[0], point[2]]
            errors.setdefault(bone, []).append(math.dist(unreal_point, observed))
    positions = lambda bone: [sample['bones'][bone]['component']['translation'] for sample in sampled['samples']]
    ball_l = positions('ball_l')
    ball_r = positions('ball_r')
    plant_l = max(math.dist(point[:2], ball_l[14][:2]) for point in ball_l[14:])
    plant_r_early = max(math.dist(point[:2], ball_r[0][:2]) for point in ball_r[:22])
    plant_r_late = max(math.dist(point[:2], ball_r[27][:2]) for point in ball_r[27:])
    roots = positions('root')
    report = {
        'animation': f'{FOLDER}/{args.name}', 'fps': fps, 'sample_count': len(records), 'duration_seconds': (len(records) - 1) / fps,
        'max_blender_unreal_bone_position_error_cm': {bone: round(max(values), 4) for bone, values in errors.items()},
        'root_travel_cm': round(math.dist(roots[0], roots[-1]), 3), 'expected_step_cm': source['summary']['step_cm'],
        'left_ball_plant_xy_drift_cm': {'frames_14_39': round(plant_l, 4)}, 'right_ball_plant_xy_drift_cm': {'frames_0_21': round(plant_r_early, 4), 'frames_27_39': round(plant_r_late, 4)},
        'sword_tip_min_height_cm': round(min(row['sword_tip'][2] for row in records), 2),
        'max_wrist_twist_deg': source['summary']['max_wrist_twist_deg'], 'max_ik_error_cm': max(source['summary']['max_right_arm_error_cm'], source['summary']['max_left_arm_error_cm'], source['summary']['max_leg_error_cm']),
        'visual_review_is_separate': True, 'user_visual_approval': False,
    }
    worst = max(max(values) for values in errors.values())
    report['technical_checks_passed'] = worst < 0.1 and plant_l < 0.5 and plant_r_early < 0.5 and plant_r_late < 0.5 and abs(report['root_travel_cm'] - source['summary']['step_cm']) < 1.0 and report['sword_tip_min_height_cm'] > 3
    out = ROOT / 'Docs/Validation/BlenderAnimation/sword-slash-validation.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    if not report['technical_checks_passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
