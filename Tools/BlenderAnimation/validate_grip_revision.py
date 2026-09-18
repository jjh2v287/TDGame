"""에디터 밖에서 실행: 가져온 v05 AnimSequence를 Unreal MCP(InspectAnimation·sample_animation_poses)로 조회해 원본 리포트와 손·발·머리 본 포즈를 비교한다.
실행: python Tools/BlenderAnimation/validate_grip_revision.py (PowerShell; 에디터 실행 중, /Game/ 경로 인수는 Git Bash 금지 L-repo-01)
출력: Saved/BlenderAnimation/GripV05/grip-v05-validation.json, unreal-samples.json
상태: 현행 (후보 v05 예제; MCP 클라이언트는 Tools/AnimationAuthoring/smoke_test.py에서 import, D-22)
"""
import json
import math
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'Tools/AnimationAuthoring'))
from smoke_test import TDMcpAnimationClient
from pose_kinematics import quaternion_from_euler, quaternion_multiply, quaternion_rotate

name = 'AS_TD_Player_Attack01_Heavy_RToL_v05'
asset = '/Game/Characters/Mannequins/Anims/Blender/' + name
mesh = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
client = TDMcpAnimationClient()
info = client.call('.TDAnimationAuthoringTools.InspectAnimation', AssetPath=asset)
if not info.get('success'):
    raise RuntimeError(info.get('error', 'Animation inspection failed'))
source = json.loads((root / 'AnimationSources/Player' / (name + '.json')).read_text())
bone_names = ['pelvis', 'lowerarm_r', 'hand_r', 'hand_l', 'ik_hand_gun', 'ik_hand_r', 'ik_hand_l', 'foot_r', 'foot_l', 'ball_r', 'ball_l', 'head']
sampled = client.call('.TDBlenderAnimationTools.sample_animation_poses', animation_path=asset,
                      skeletal_mesh_path=mesh, bone_names=bone_names,
                      sample_times=[min(frame / 30, info['duration']) for frame in range(44)])
rows = sampled['samples']


def angle_between(a, b):
    dot = abs(sum(x*y for x, y in zip(a, b)))
    length = math.sqrt(sum(x*x for x in a) * sum(x*x for x in b))
    return math.degrees(2 * math.acos(max(-1, min(1, dot / length))))


def pair_errors(first, second):
    position = max(math.dist(row['bones'][first]['component']['translation'], row['bones'][second]['component']['translation']) for row in rows)
    angle = max(angle_between(row['bones'][first]['component']['quaternion'], row['bones'][second]['component']['quaternion']) for row in rows)
    return {'position_cm': position, 'angle_degrees': angle}


steps = {}
for bone in ['upperarm_r', 'lowerarm_r', 'hand_r', 'hand_l', 'ik_hand_gun', 'ik_hand_r', 'ik_hand_l']:
    if bone not in bone_names:
        continue
    steps[bone] = {space: max(angle_between(a['bones'][bone][space]['quaternion'], b['bones'][bone][space]['quaternion'])
                             for a, b in zip(rows, rows[1:])) for space in ['local', 'component']}
conversion_errors = [math.dist([point[0], -point[1], point[2]], rows[index]['bones'][bone]['component']['translation'])
                     for index, record in enumerate(source['records']) for bone, point in record['bones_world_cm'].items()]
socket = client.call('.SkeletalMeshTools.get_socket_transform', mesh={'refPath': mesh + '.SKM_Manny_Simple'}, socket_name='HandGrip_R')
socket_rotation = socket['rotation']
socket_quaternion = quaternion_from_euler(socket_rotation['pitch'], socket_rotation['yaw'], socket_rotation['roll'])
socket_location = [socket['location'][axis] for axis in ['x', 'y', 'z']]
socket_errors = []
blade_errors = []
for index, row in enumerate(rows):
    hand = row['bones']['hand_r']['component']
    offset = quaternion_rotate(hand['quaternion'], socket_location)
    socket_position = [a+b for a, b in zip(hand['translation'], offset)]
    expected = source['records'][index]['socket_world_cm']
    socket_errors.append(math.dist(socket_position, [expected[0], -expected[1], expected[2]]))
    rotation = quaternion_multiply(hand['quaternion'], socket_quaternion)
    blade = quaternion_rotate(rotation, (0, 1, 0))
    reference_blade = source['records'][index]['blade_forward']
    blade_errors.append(math.dist(blade, [reference_blade[0], -reference_blade[1], reference_blade[2]]))
toe = [row['bones']['ball_r']['component']['translation'] for row in rows]
left_hand_head_distances = [math.dist(row['bones']['hand_l']['component']['translation'], row['bones']['head']['component']['translation']) for row in rows]
report = {
    'animation': asset, 'duration_seconds': info['duration'], 'sample_count': len(rows),
    'maximum_frame_rotation_step_degrees': steps,
    'right_hand_to_ik_hand': pair_errors('hand_r', 'ik_hand_r'),
    'right_hand_to_ik_hand_gun': pair_errors('hand_r', 'ik_hand_gun'),
    'left_hand_to_ik_hand': pair_errors('hand_l', 'ik_hand_l'),
    'maximum_blender_unreal_position_error_cm': max(conversion_errors),
    'maximum_socket_position_error_cm': max(socket_errors),
    'maximum_blade_unit_direction_error': max(blade_errors),
    'minimum_left_hand_to_head_distance_cm': min(left_hand_head_distances),
    'right_toe_plant_xy_displacement_cm': max(math.dist(position[:2], toe[23][:2]) for position in toe[23:]),
    'visual_approval_is_separate': True,
}
report['technical_checks_passed'] = (
    max(conversion_errors) < 0.05 and max(socket_errors) < 0.05 and max(blade_errors) < 0.001
    and all(report[key]['position_cm'] < 0.05 and report[key]['angle_degrees'] < 0.05
            for key in ['right_hand_to_ik_hand', 'right_hand_to_ik_hand_gun', 'left_hand_to_ik_hand'])
    and steps['hand_r']['local'] < 0.1 and steps['hand_r']['component'] < 45
    and steps['hand_l']['local'] < 0.1 and steps['hand_l']['component'] < 45
    and min(left_hand_head_distances) > 40
)
directory = root / 'Docs/Validation/BlenderAnimation'
(directory / 'grip-v05-validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
(root / 'Saved/BlenderAnimation/GripV05/unreal-samples.json').write_text(json.dumps(sampled, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
if not report['technical_checks_passed']:
    raise SystemExit(1)
