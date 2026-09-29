"""에디터 안에서 실행: 가져온 검 공격 AnimSequence를 프레임마다 샘플링해 Blender 측정 JSON(measure_<Action>.json)의 손목·골반·발 위치와 비교하고, 루트 이동·길이·루트 모션 설정을 확인한다.
실행: python Tools/run_in_editor.py Tools/BlenderAnimation/editor_validate_sword_attack01.py (PowerShell; 다른 에셋은 -c "TD_SEQUENCE='/Game/...'; TD_MEASURE='<json>'; exec(open(...).read())")
출력: Saved/BlenderAnimation/SwordAttack01/ue-validation.json, 로그의 [TDTool] validation 줄
상태: 현행 (2026-09-25, UE5 Manny: 캐릭터 전방 = 메시 +Y, 오른쪽 = 메시 -X)
"""
import json
import math
import os

import unreal

SEQUENCE = globals().get('TD_SEQUENCE', '/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordAttack01')
MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
WORK = os.path.join(unreal.Paths.project_saved_dir(), 'BlenderAnimation', 'SwordAttack01')
MEASURE = globals().get('TD_MEASURE', os.path.join(WORK, 'measure_TD_SwordAttack01.json'))
BLADE_REACH_CM = 110.0
COMPARED = {'wrist': 'hand_r', 'pelvis': 'pelvis', 'ball_l': 'ball_l', 'ball_r': 'ball_r', 'foot_l': 'foot_l', 'foot_r': 'foot_r'}


def character(location):
    return (location.y, -location.x, location.z)


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def main():
    sequence = unreal.load_asset(SEQUENCE)
    mesh = unreal.load_asset(MESH)
    with open(os.path.abspath(MEASURE), encoding='utf-8') as handle:
        blender_frames = json.load(handle)['frames']
    options = unreal.AnimPoseEvaluationOptions()
    options.optional_skeletal_mesh = mesh
    options.evaluation_type = unreal.AnimDataEvalType.RAW
    options.should_retarget = False
    options.extract_root_motion = False
    options.incorporate_root_motion_into_pose = True
    compressed = unreal.AnimPoseEvaluationOptions()
    compressed.optional_skeletal_mesh = mesh
    compressed.evaluation_type = unreal.AnimDataEvalType.COMPRESSED
    compressed.should_retarget = False
    compressed.extract_root_motion = False
    compressed.incorporate_root_motion_into_pose = True
    length = unreal.AnimationLibrary.get_sequence_length(sequence)
    compression_hand_angle = 0.0
    compression_hand_position = 0.0
    worst = {key: 0.0 for key in COMPARED}
    worst_frame = {key: 0 for key in COMPARED}
    root_path = []
    for index, row in enumerate(blender_frames):
        time = min(index / 60.0, length)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, time, options)
        root_path.append(character(unreal.AnimPoseExtensions.get_bone_pose(pose, 'root', unreal.AnimPoseSpaces.WORLD).translation))
        compressed_pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, time, compressed)
        raw_hand = unreal.AnimPoseExtensions.get_bone_pose(pose, 'hand_r', unreal.AnimPoseSpaces.WORLD)
        compressed_hand = unreal.AnimPoseExtensions.get_bone_pose(compressed_pose, 'hand_r', unreal.AnimPoseSpaces.WORLD)
        dot = abs(sum(a * b for a, b in zip((raw_hand.rotation.x, raw_hand.rotation.y, raw_hand.rotation.z, raw_hand.rotation.w), (compressed_hand.rotation.x, compressed_hand.rotation.y, compressed_hand.rotation.z, compressed_hand.rotation.w))))
        compression_hand_angle = max(compression_hand_angle, 2.0 * math.degrees(math.acos(min(1.0, dot))))
        compression_hand_position = max(compression_hand_position, distance(character(raw_hand.translation), character(compressed_hand.translation)))
        for key, bone in COMPARED.items():
            location = character(unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation)
            error = distance(location, row[key])
            if error > worst[key]:
                worst[key], worst_frame[key] = error, row['frame']
    report = {
        'sequence': SEQUENCE,
        'length_s': round(length, 4),
        'frames_compared': len(blender_frames),
        'enable_root_motion': sequence.get_editor_property('enable_root_motion'),
        'root_lock': str(sequence.get_editor_property('root_motion_root_lock')),
        'root_travel_cm': [round(v, 2) for v in root_path[-1]],
        'root_max_lateral_cm': round(max(abs(p[1]) for p in root_path), 3),
        'root_max_vertical_cm': round(max(abs(p[2]) for p in root_path), 3),
        'max_position_error_cm': {key: round(value, 4) for key, value in worst.items()},
        'worst_frame': worst_frame,
        'compression_hand_max_angle_deg': round(compression_hand_angle, 4),
        'compression_hand_max_position_cm': round(compression_hand_position, 4),
        'compression_tip_error_estimate_cm': round(compression_hand_position + math.radians(compression_hand_angle) * BLADE_REACH_CM, 3),
    }
    report['passed'] = (report['enable_root_motion'] and max(worst.values()) < 0.5 and abs(report['root_travel_cm'][0] - blender_frames[-1]['root'][0]) < 0.5
                        and report['compression_tip_error_estimate_cm'] <= 1.0)
    with open(os.path.join(WORK, 'ue-validation.json'), 'w', encoding='utf-8') as handle:
        json.dump(report, handle, indent=1)
    unreal.log('[TDTool] validation ' + json.dumps(report))


main()
