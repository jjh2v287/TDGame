"""Blender 안에서 실행: v05 후보 저작. 빈 씬에 SKM_Manny_Simple과 참고 MM_Attack_01을 불러와 검 그립 프로파일을 적용한 공격 애니메이션을 만들고 .blend/.fbx/.json으로 저장한다.
실행: Blender MCP execute_blender_code로 본문을 전달(`python Tools/BlenderMCP/call_tool.py --code …`, 절차는 Docs/BlenderAnimationWorkflow.md). 같은 이름의 후보가 있으면 중단한다.
출력: AnimationSources/Player/AS_TD_Player_Attack01_Heavy_RToL_v05.{blend,fbx,json}
상태: 현행 (특정 캐릭터·후보 v05 예제, 본 이름·프레임 고정; 다른 요청에 그대로 재사용하지 않는다)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

root = Path('C:/Project/TDGame')
name = 'AS_TD_Player_Attack01_Heavy_RToL_v05'
output = root / 'AnimationSources/Player'
if any((output / (name + extension)).exists() for extension in ['.blend', '.fbx']):
    raise FileExistsError('Choose a new candidate name')
if any(obj.type == 'ARMATURE' for obj in bpy.data.objects):
    raise RuntimeError('Preserve the current document and use an empty authoring scene')
scene = bpy.context.scene
scene.name = 'TD_SwordGrip_v05'
bpy.ops.import_scene.fbx(filepath=str(root / 'Saved/BlenderAnimation/SKM_Manny_Simple.fbx'), use_anim=False)
armature = next(obj for obj in scene.objects if obj.type == 'ARMATURE')
world = armature.matrix_world.copy()
armature.parent = None
armature.matrix_world = world
if armature.name != 'root':
    raise ValueError('The original root name must be preserved')
bpy.ops.import_scene.fbx(filepath=str(root / 'Saved/BlenderAnimation/Reference/MM_Attack_01.fbx'), use_anim=True)
reference = next(obj for obj in scene.objects if obj.type == 'ARMATURE' and obj != armature)
reference.name = 'TD_FootworkReference'
reference.hide_render = True
reference.hide_set(True)
reference.animation_data.action.use_fake_user = True
bones = armature.pose.bones
rest = {bone.name: bone.matrix_local.copy() for bone in armature.data.bones}
upper = {bone.name for bone in armature.data.bones['spine_01'].children_recursive} | {'spine_01'}
time_map = [(0, 0), (10, 7), (14, 10), (19, 13), (25, 18), (35, 25), (40, 30), (43, 30)]
pose_keys = [
    (0,  [-8, 4, 25, 32, -12, -28, 15, -33]),
    (8,  [-23, 3, 46, -5, -5, -30, 7, -30]),
    (14, [-35, 5, 48, 2, -4, -37, -8, -28]),
    (18, [-18, 8, 40, 22, -10, -38, -16, -22]),
    (23, [22, 10, -8, 40, -12, -33, -21, -24]),
    (27, [38, 8, -25, 24, -16, -30, -12, -28]),
    (34, [15, 5, -7, 34, -25, -28, 0, -31]),
    (43, [-8, 4, 25, 32, -12, -28, 15, -33]),
]


def linear(keys, time):
    for (start, a), (end, b) in zip(keys, keys[1:]):
        if start <= time <= end:
            return a + (b - a) * (time - start) / (end - start)
    return keys[-1][1]


def pose_at(frame):
    for index, ((start, first), (end, second)) in enumerate(zip(pose_keys, pose_keys[1:])):
        if not start <= frame <= end:
            continue
        t = (frame - start) / (end - start)
        before = pose_keys[max(0, index - 1)]
        after = pose_keys[min(len(pose_keys) - 1, index + 2)]
        out = []
        for channel, (a, b) in enumerate(zip(first, second)):
            slope_a = 0 if index == 0 else (b - before[1][channel]) / (end - before[0])
            slope_b = 0 if index + 2 == len(pose_keys) else (after[1][channel] - a) / (after[0] - start)
            out.append((2*t**3 - 3*t*t + 1)*a + (t**3 - 2*t*t + t)*(end-start)*slope_a
                       + (-2*t**3 + 3*t*t)*b + (t**3-t*t)*(end-start)*slope_b)
        return out
    return pose_keys[-1][1]


def rotate_world(name, axis, degrees):
    bone = bones[name]
    matrix = bone.matrix.copy()
    bone.matrix = Matrix.LocRotScale(matrix.translation,
                                    Quaternion(axis, math.radians(degrees)) @ matrix.to_quaternion(),
                                    matrix.to_scale())
    bpy.context.view_layer.update()


def chain_basis(direction, normal):
    direction = direction.normalized()
    normal = (normal - direction * normal.dot(direction)).normalized()
    return Matrix((direction, normal.cross(direction), normal)).transposed()


def pose_arm(side, target, pole, level_forearm):
    upper_name, lower_name, hand_name = (part + '_' + side for part in ('upperarm', 'lowerarm', 'hand'))
    shoulder = bones[upper_name].matrix.translation.copy()
    upper_rest = rest[lower_name].translation - rest[upper_name].translation
    lower_rest = rest[hand_name].translation - rest[lower_name].translation
    length_a, length_b = upper_rest.length, lower_rest.length
    delta = target - shoulder
    distance = delta.length
    if distance >= length_a + length_b - 0.1 or distance <= abs(length_a - length_b) + 0.1:
        raise ValueError(f'Unreachable {side} hand target at frame {scene.frame_current}: {distance}')
    direction = delta.normalized()
    along = (length_a**2 - length_b**2 + distance**2) / (2*distance)
    radius = math.sqrt(max(0, length_a**2 - along**2))
    center = shoulder + direction * along
    if level_forearm:
        plane_up = Vector((0, 0, 1)) - direction * direction.z
        plane_up.normalize()
        plane_side = direction.cross(plane_up).normalized()
        cosine = max(-0.97, min(0.97, (target.z - center.z) / (radius * plane_up.z)))
        sine = math.sqrt(1 - cosine*cosine)
        candidates = [center + radius * (plane_up*cosine + plane_side*sine*sign) for sign in (-1, 1)]
        elbow = min(candidates, key=lambda position: (position - pole).length_squared)
    else:
        bend = pole - center
        bend = (bend - direction * bend.dot(direction)).normalized()
        elbow = center + bend * radius
    normal = (elbow - shoulder).cross(target - elbow).normalized()
    rest_normal = upper_rest.cross(lower_rest).normalized()
    for bone_name, reference_direction, posed_direction, location in [
        (upper_name, upper_rest, elbow - shoulder, shoulder),
        (lower_name, lower_rest, target - elbow, elbow),
    ]:
        rotation = (chain_basis(posed_direction, normal) @ chain_basis(reference_direction, rest_normal).transposed()).to_quaternion()
        rotation = rotation @ rest[bone_name].to_quaternion()
        bones[bone_name].matrix = Matrix.LocRotScale(location, rotation, Vector((1, 1, 1)))
        bpy.context.view_layer.update()
    wrist_offset = rest[lower_name].to_quaternion().inverted() @ rest[hand_name].to_quaternion()
    hand_rotation = bones[lower_name].matrix.to_quaternion() @ wrist_offset
    bones[hand_name].matrix = Matrix.LocRotScale(target, hand_rotation, Vector((1, 1, 1)))
    bpy.context.view_layer.update()
    return (bones[hand_name].matrix.translation - target).length


base_samples = []
fist = {}
for frame in range(44):
    source_frame = 1 + linear(time_map, frame)
    scene.frame_set(int(source_frame), subframe=source_frame % 1)
    bpy.context.view_layer.update()
    armature.matrix_world = reference.matrix_world.copy()
    for bone in bones:
        bone.matrix = reference.pose.bones[bone.name].matrix.copy()
        bpy.context.view_layer.update()
    if frame == 0:
        fist = {bone.name: bone.matrix_basis.to_quaternion().copy() for bone in bones
                if any(part in bone.name for part in ['thumb_', 'index_', 'middle_', 'ring_', 'pinky_'])}
    base_samples.append({'world': armature.matrix_world.copy(),
                         'basis': {bone.name: bone.matrix_basis.copy() for bone in bones if bone.name not in upper}})
armature.animation_data_clear()
armature.animation_data_create()
armature.animation_data.action = bpy.data.actions.new(name)
armature.rotation_mode = 'QUATERNION'
for bone in bones:
    bone.rotation_mode = 'QUATERNION'
scene.render.fps = 30
scene.frame_start = 1
scene.frame_end = 44
profile = json.loads((root / 'Saved/BlenderAnimation/grip-profile-v05.json').read_text())
socket = Matrix.LocRotScale(Vector(profile['blender_local_cm']['translation']),
                           Quaternion(profile['blender_local_cm']['quaternion_wxyz']), Vector((1, 1, 1)))
records = []
previous = {}
for frame in range(44):
    scene.frame_set(frame + 1)
    armature.matrix_world = base_samples[frame]['world']
    for bone in bones:
        bone.matrix_basis = base_samples[frame]['basis'].get(bone.name, Matrix.Identity(4))
    bpy.context.view_layer.update()
    values = pose_at(frame)
    yaw, lean = values[:2]
    pelvis_delta = bones['pelvis'].matrix.to_quaternion() @ rest['pelvis'].to_quaternion().inverted()
    pelvis_yaw = math.degrees(pelvis_delta.to_euler('XYZ').z)
    for spine in ('spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05'):
        rotate_world(spine, Vector((0, 0, 1)), (yaw - pelvis_yaw) / 5)
        rotate_world(spine, Vector((1, 0, 0)), lean / 5)
    rotate_world('head', Vector((0, 0, 1)), -yaw * 0.7)
    chest = bones['spine_05'].matrix.translation.copy()
    right_target = chest + Vector((-values[2], -values[3], values[4]))
    left_target = chest + Vector((-values[5], -values[6], values[7]))
    right_error = pose_arm('r', right_target, chest + Vector((-75, -2, -20)), True)
    left_error = pose_arm('l', left_target, chest + Vector((62, 0, -40)), False)
    for bone_name, rotation in fist.items():
        influence = 0.9 if bone_name.endswith('_r') else 0.22
        bones[bone_name].rotation_quaternion = Quaternion().slerp(rotation, influence)
    bpy.context.view_layer.update()
    for follower, leader in [('ik_hand_gun', 'hand_r'), ('ik_hand_r', 'hand_r'), ('ik_hand_l', 'hand_l')]:
        bones[follower].matrix = bones[leader].matrix.copy()
        bpy.context.view_layer.update()
    for path in ['location', 'rotation_quaternion', 'scale']:
        armature.keyframe_insert(data_path=path, frame=frame + 1)
    for bone in bones:
        rotation = bone.rotation_quaternion.copy()
        if bone.name in previous:
            rotation.make_compatible(previous[bone.name])
            bone.rotation_quaternion = rotation
        previous[bone.name] = rotation
        for path in ['location', 'rotation_quaternion', 'scale']:
            bone.keyframe_insert(data_path=path, frame=frame + 1)
    socket_matrix = bones['hand_r'].matrix @ socket
    recorded_bones = ['pelvis', 'lowerarm_r', 'hand_r', 'hand_l', 'ik_hand_gun', 'ik_hand_r', 'ik_hand_l', 'foot_r', 'foot_l', 'ball_r', 'ball_l', 'head']
    records.append({'frame': frame, 'source_footwork_frame': linear(time_map, frame),
                    'right_target_error_cm': right_error, 'left_target_error_cm': left_error,
                    'socket_world_cm': list((armature.matrix_world @ socket_matrix.translation) * 100),
                    'blade_forward': list(socket_matrix.to_quaternion() @ Vector((0, -1, 0))),
                    'bones_world_cm': {bone: list((armature.matrix_world @ bones[bone].matrix.translation) * 100) for bone in recorded_bones}})
for layer in armature.animation_data.action.layers:
    for strip in layer.strips:
        for slot in armature.animation_data.action.slots:
            bag = strip.channelbag(slot)
            if bag:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:
                        key.interpolation = 'LINEAR'
scene.frame_set(1)
armature['td_upper_body'] = 'Authored sword key poses; neutral wrist relative to forearm; relaxed counterbalancing left arm'
armature['td_binding_socket'] = 'HandGrip_R'
armature['td_footwork_source'] = '/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01'
bpy.ops.object.select_all(action='DESELECT')
armature.select_set(True)
bpy.context.view_layer.objects.active = armature
fbx = output / (name + '.fbx')
bpy.ops.export_scene.fbx(filepath=str(fbx), use_selection=True, object_types={'ARMATURE'},
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_bones=True,
                         bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                         bake_anim_step=1, bake_anim_simplify_factor=0, axis_forward='-Z', axis_up='Y')
report = {'name': name, 'fps': 30, 'duration': 43/30, 'grip_profile': profile,
          'upper_body_source': 'authored poses', 'footwork_source': armature['td_footwork_source'],
          'pose_keys': pose_keys, 'records': records}
(output / (name + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(output / (name + '.blend')), check_existing=False)
print(json.dumps({'name': name, 'max_right_error_cm': max(row['right_target_error_cm'] for row in records),
                  'max_left_error_cm': max(row['left_target_error_cm'] for row in records)}))
