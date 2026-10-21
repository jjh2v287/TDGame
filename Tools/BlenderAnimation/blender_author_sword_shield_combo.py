"""Blender 안에서 실행: 오른손 한손검(SM_Sword)과 왼손 방패(TD_ShieldPreview)를 장착한 플레이어 기본 공격 4단 콤보(1타 우→좌 횡배기, 2타 좌→우 횡배기, 3타 우→좌 대각선 배기, 4타 좌→우 360°+ 전신 회전 강베기)와 연속 재생 Action을 루트 모션으로 제작한다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/blender_author_sword_shield_combo.py [TD_SAVE=True]
출력: Saved/BlenderAnimation/SwordAttack01/work.blend 내 TD_SwordCombo01..04·TD_SwordCombo_All Action, Saved/BlenderAnimation/SwordAttack01/combo-author-result.json
상태: 현행 (2026-10-18, UE5 Manny·해부학적 오른손 검 쥐기·왼손 히터 실드 소켓 전제, 60fps)
"""
import json
import math
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path(__file__).resolve().parents[2] if '__file__' in globals() else Path.cwd()
WORK = ROOT / 'Saved/BlenderAnimation/SwordAttack01'
SAVE = bool(globals().get('TD_SAVE', True))
FORWARD = Vector((0.0, -1.0, 0.0))
RIGHT = Vector((-1.0, 0.0, 0.0))
LEFT = Vector((1.0, 0.0, 0.0))
UP = Vector((0.0, 0.0, 1.0))
LEG_BONES = {side: (f'thigh_{side}', f'calf_{side}', f'foot_{side}', f'ball_{side}') for side in ('l', 'r')}
RIGHT_ARM_CHAIN = ('clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r')
LEFT_ARM_CHAIN = ('clavicle_l', 'upperarm_l', 'lowerarm_l', 'hand_l')
TWIST_BONES_R = ('lowerarm_twist_01_r', 'lowerarm_twist_02_r')
TWIST_BONES_L = ('lowerarm_twist_01_l', 'lowerarm_twist_02_l')
SPINE_BONES = ('spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05')
SPINE_WEIGHTS = (0.15, 0.20, 0.25, 0.20, 0.20)
NECK_BONES = ('neck_01', 'neck_02', 'head')
NECK_WEIGHTS = (0.35, 0.35, 0.30)

scene = bpy.context.scene
scene.render.fps = 60
scene.render.fps_base = 1.0
armature = bpy.data.objects['root']
bones = armature.pose.bones
rest = {bone.name: bone.matrix_local.copy() for bone in armature.data.bones}
parents = {bone.name: (bone.parent.name if bone.parent else None) for bone in armature.data.bones}
order = [bone.name for bone in armature.pose.bones]


def ensure_shield(armature_obj):
    for name in ('TD_ShieldPreview', 'TD_ShieldSocket_L'):
        old = bpy.data.objects.get(name)
        if old is not None:
            bpy.data.objects.remove(old, do_unlink=True)
    hand_inv = rest['hand_l'].inverted()
    loc = lambda name: hand_inv @ rest[name].translation
    wrist = loc('hand_l')
    axis = (loc('middle_01_l') - wrist).normalized()
    radial = loc('index_01_l') - loc('pinky_01_l')
    radial = (radial - axis * radial.dot(axis)).normalized()
    dorsal = radial.cross(axis).normalized()
    shield_normal = (dorsal * math.cos(math.radians(18.0)) + radial * math.sin(math.radians(18.0))).normalized()
    shield_up = (radial - shield_normal * radial.dot(shield_normal)).normalized()
    shield_right = shield_up.cross(shield_normal).normalized()
    rotation = Matrix((shield_right, shield_up, shield_normal)).transposed()
    center = wrist - axis * 5.0 + dorsal * 6.2
    socket_local = Matrix.Translation(center) @ rotation.to_4x4()
    bone = armature_obj.data.bones['hand_l']
    tail_offset = Matrix.Translation((0.0, -bone.length, 0.0))
    socket = bpy.data.objects.new('TD_ShieldSocket_L', None)
    scene.collection.objects.link(socket)
    socket.parent = armature_obj
    socket.parent_type = 'BONE'
    socket.parent_bone = 'hand_l'
    socket.matrix_parent_inverse = Matrix.Identity(4)
    socket.rotation_mode = 'QUATERNION'
    socket.matrix_basis = tail_offset @ socket_local
    socket.empty_display_type = 'ARROWS'
    socket.empty_display_size = 6
    socket['unreal_socket'] = 'Shield_L'
    old_mesh = bpy.data.meshes.get('TD_ShieldPreview')
    if old_mesh is not None:
        bpy.data.meshes.remove(old_mesh)
    mesh = bpy.data.meshes.new('TD_ShieldPreview')
    bm = bmesh.new()
    profile = [
        (-19.0, 25.0), (-9.5, 27.5), (0.0, 28.5), (9.5, 27.5), (19.0, 25.0),
        (21.0, 10.0), (19.5, -4.0), (14.0, -17.0), (7.0, -25.0), (0.0, -30.0),
        (-7.0, -25.0), (-14.0, -17.0), (-19.5, -4.0), (-21.0, 10.0),
    ]
    count = len(profile)
    outer_f, inner_f, center_f, back_v = [], [], [], []
    for x, y in profile:
        cz = 4.0 * (1.0 - (x / 22.0) ** 2)
        outer_f.append(bm.verts.new((x, y, cz + 1.8)))
        inner_f.append(bm.verts.new((x * 0.84, y * 0.84, cz + 1.1)))
        center_f.append(bm.verts.new((x * 0.22, y * 0.22 + 2.0, cz + 3.4)))
        back_v.append(bm.verts.new((x * 0.94, y * 0.94, cz - 1.2)))
    apex = bm.verts.new((0.0, 2.0, 8.8))
    back_center = bm.verts.new((0.0, 0.0, 2.2))
    for i in range(count):
        j = (i + 1) % count
        bm.faces.new([outer_f[i], outer_f[j], inner_f[j], inner_f[i]])
        bm.faces.new([inner_f[i], inner_f[j], center_f[j], center_f[i]])
        bm.faces.new([center_f[i], center_f[j], apex])
        bm.faces.new([outer_f[j], outer_f[i], back_v[i], back_v[j]])
        bm.faces.new([back_v[j], back_v[i], back_center])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    shield = bpy.data.objects.new('TD_ShieldPreview', mesh)
    scene.collection.objects.link(shield)
    shield.parent = socket
    shield.matrix_parent_inverse = Matrix.Identity(4)
    shield.matrix_basis = Matrix.Identity(4)
    shield.color = (0.24, 0.42, 0.68, 1.0)
    return socket, shield, socket_local


shield_socket_obj, shield_preview_obj, SHIELD_SOCKET_LOCAL = ensure_shield(armature)
socket_object = bpy.data.objects['TD_HandGrip_R']
tip_object = bpy.data.objects['TD_SwordTip']
hand_r_length = armature.data.bones['hand_r'].length
SOCKET_LOCAL = Matrix.Translation((0.0, hand_r_length, 0.0)) @ socket_object.matrix_basis
TIP_LOCAL = Vector(tip_object.location)


def pchip(keys, x):
    xs = [float(k[0]) for k in keys]
    ys = [float(k[1]) for k in keys]
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    slopes = [(ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]) for i in range(len(xs) - 1)]
    tangents = [slopes[0]] + [0.0 if slopes[i - 1] * slopes[i] <= 0 else 2.0 / (1.0 / slopes[i - 1] + 1.0 / slopes[i]) for i in range(1, len(slopes))] + [slopes[-1]]
    index = max(i for i in range(len(xs) - 1) if xs[i] <= x)
    h = xs[index + 1] - xs[index]
    t = (x - xs[index]) / h
    h00, h10, h01, h11 = 2 * t ** 3 - 3 * t ** 2 + 1, t ** 3 - 2 * t ** 2 + t, -2 * t ** 3 + 3 * t ** 2, t ** 3 - t ** 2
    return h00 * ys[index] + h10 * h * tangents[index] + h01 * ys[index + 1] + h11 * h * tangents[index + 1]


def smoothstep(edge0, edge1, x):
    if edge1 == edge0:
        return 1.0 if x >= edge1 else 0.0
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def quintic_step(fraction):
    f = max(0.0, min(1.0, fraction))
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0)


def window_bump(window, frame):
    begin, full_begin, full_end, end = window
    return smoothstep(begin, full_begin, frame) * (1.0 - smoothstep(full_end, end, frame))


def gaussian_smooth(values, sigma):
    if sigma <= 0:
        return list(values)
    radius = int(math.ceil(sigma * 3))
    weights = [math.exp(-0.5 * (k / sigma) ** 2) for k in range(-radius, radius + 1)]
    result = []
    for i in range(len(values)):
        total = weight_sum = 0.0
        for k, weight in zip(range(-radius, radius + 1), weights):
            j = min(len(values) - 1, max(0, i + k))
            total += values[j] * weight
            weight_sum += weight
        result.append(total / weight_sum)
    return result


def max_filter(values, radius):
    return [max(values[max(0, i - radius):i + radius + 1]) for i in range(len(values))]


def with_translation(matrix, translation):
    result = matrix.copy()
    result.translation = translation
    return result


def rotate_about(matrix, pivot, rotation):
    return Matrix.Translation(pivot) @ rotation.to_matrix().to_4x4() @ Matrix.Translation(-pivot) @ matrix


def cw_quat(degrees_cw):
    return Quaternion(UP, -math.radians(degrees_cw))


def char_vec(forward_cm, right_cm, up_cm):
    return FORWARD * forward_cm + RIGHT * right_cm + UP * up_cm


def to_char_cm(vec):
    return Vector((-vec.y, -vec.x, vec.z))


def frame_from(direction, normal):
    x = direction.normalized()
    z = x.cross(normal).normalized()
    y = z.cross(x)
    return Matrix((x, y, z)).transposed()


def align_bone(source_matrix, source_direction, source_normal, new_head, new_direction, new_normal):
    rotation = frame_from(new_direction, new_normal) @ frame_from(source_direction, source_normal).transposed()
    result = (rotation @ source_matrix.to_3x3()).to_4x4()
    result.translation = new_head
    return result


def child_offset(parent, child):
    return rest[parent].inverted() @ rest[child].translation


def forward_kinematics(source, overrides):
    result = {}
    for name in order:
        if name in overrides:
            result[name] = overrides[name]
            continue
        parent = parents[name]
        if parent is None:
            result[name] = source[name]
        else:
            result[name] = result[parent] @ (source[parent].inverted() @ source[name])
    return result


def shift_pose(pose, delta):
    shifted = {}
    for name, mat in pose.items():
        m = mat.copy()
        if name != 'root':
            m.translation = mat.translation + delta
        shifted[name] = m
    return shifted


def blend_matrix(matrix_a, matrix_b, weight):
    if weight <= 0.0:
        return matrix_a.copy()
    if weight >= 1.0:
        return matrix_b.copy()
    qa = matrix_a.to_quaternion()
    qb = matrix_b.to_quaternion()
    if qa.dot(qb) < 0.0:
        qb.negate()
    rotation = qa.slerp(qb, weight)
    result = rotation.to_matrix().to_4x4()
    result.translation = matrix_a.translation.lerp(matrix_b.translation, weight)
    return result


def blend_poses_local(pose_a, pose_b, weight, skip_legs=False):
    if weight <= 0.0:
        return {k: v.copy() for k, v in pose_a.items()}
    if weight >= 1.0 and not skip_legs:
        return {k: v.copy() for k, v in pose_b.items()}
    leg_set = {b for side in LEG_BONES.values() for b in side} if skip_legs else set()
    result = {}
    for name in order:
        if name in leg_set:
            result[name] = pose_a[name].copy()
            continue
        parent = parents[name]
        if parent is None or name == 'pelvis':
            result[name] = blend_matrix(pose_a[name], pose_b[name], weight)
            continue
        local_a = pose_a[parent].inverted() @ pose_a[name]
        local_b = pose_b[parent].inverted() @ pose_b[name]
        local_blend = blend_matrix(local_a, local_b, weight)
        result[name] = result[parent] @ local_blend
    return result


def sample_source(action_name, times):
    armature.animation_data.action = bpy.data.actions[action_name]
    armature.location = (0.0, 0.0, 0.0)
    armature.rotation_quaternion = Quaternion()
    frames = []
    for time in times:
        scene.frame_set(int(math.floor(time)), subframe=time - math.floor(time))
        frames.append({bone.name: bone.matrix.copy() for bone in bones})
    return frames


def solve_two_bone(root_pt, target, length_a, length_b, pole):
    delta = target - root_pt
    distance = delta.length
    reach = (length_a + length_b) * 0.998
    overextension = max(0.0, distance - reach)
    distance = min(max(distance, abs(length_a - length_b) + 0.01), reach)
    axis = delta.normalized() if delta.length > 1e-6 else FORWARD
    clamped_target = root_pt + axis * distance
    along = (length_a ** 2 - length_b ** 2 + distance ** 2) / (2.0 * distance)
    height = math.sqrt(max(0.0, length_a ** 2 - along ** 2))
    perp = pole - axis * pole.dot(axis)
    perpendicular = perp.normalized() if perp.length > 1e-6 else UP
    return root_pt + axis * along + perpendicular * height, clamped_target, overextension


HAND_MIDDLE_R = child_offset('hand_r', 'middle_01_r')
HAND_RADIAL_R = child_offset('hand_r', 'index_01_r') - child_offset('hand_r', 'pinky_01_r')
FOREARM_LOCAL_R = child_offset('lowerarm_r', 'hand_r').normalized()
REST_HAND_IN_LOWERARM_R = rest['lowerarm_r'].to_3x3().inverted() @ rest['hand_r'].to_3x3()
FOREARM_IN_HAND_R = (REST_HAND_IN_LOWERARM_R.transposed() @ FOREARM_LOCAL_R).normalized()
RADIAL_IN_HAND_R = (HAND_RADIAL_R - FOREARM_IN_HAND_R * HAND_RADIAL_R.dot(FOREARM_IN_HAND_R)).normalized()

HAND_MIDDLE_L = child_offset('hand_l', 'middle_01_l')
HAND_RADIAL_L = child_offset('hand_l', 'index_01_l') - child_offset('hand_l', 'pinky_01_l')
FOREARM_LOCAL_L = child_offset('lowerarm_l', 'hand_l').normalized()
REST_HAND_IN_LOWERARM_L = rest['lowerarm_l'].to_3x3().inverted() @ rest['hand_l'].to_3x3()
FOREARM_IN_HAND_L = (REST_HAND_IN_LOWERARM_L.transposed() @ FOREARM_LOCAL_L).normalized()
RADIAL_IN_HAND_L = (HAND_RADIAL_L - FOREARM_IN_HAND_L * HAND_RADIAL_L.dot(FOREARM_IN_HAND_L)).normalized()

LENGTHS = {
    'upperarm_r': child_offset('upperarm_r', 'lowerarm_r').length,
    'lowerarm_r': child_offset('lowerarm_r', 'hand_r').length,
    'upperarm_l': child_offset('upperarm_l', 'lowerarm_l').length,
    'lowerarm_l': child_offset('lowerarm_l', 'hand_l').length,
}
for side_key, (thigh_n, calf_n, foot_n, ball_n) in LEG_BONES.items():
    LENGTHS[thigh_n] = child_offset(thigh_n, calf_n).length
    LENGTHS[calf_n] = child_offset(calf_n, foot_n).length


def wrist_angles_r(hand_matrix, forearm):
    rotation = hand_matrix.to_3x3()
    hand_direction = (rotation @ HAND_MIDDLE_R).normalized()
    radial = (rotation @ HAND_RADIAL_R).normalized()
    palm = forearm.cross(radial).normalized()
    radial = palm.cross(forearm).normalized()
    along = hand_direction.dot(forearm)
    return math.degrees(math.atan2(hand_direction.dot(palm), along)), math.degrees(math.atan2(hand_direction.dot(radial), along))


def arm_geometry_side(pose, wrist, side):
    upper_name = f'upperarm_{side}'
    lower_name = f'lowerarm_{side}'
    shoulder = pose[upper_name].translation
    length_a, length_b = LENGTHS[upper_name], LENGTHS[lower_name]
    axis = wrist - shoulder
    distance = max(axis.length, 1e-4)
    reach = (length_a + length_b) * 0.996
    overextension = max(0.0, distance - reach)
    distance = min(max(distance, abs(length_a - length_b) + 1.0), reach)
    axis.normalize()
    reference = UP if abs(axis.dot(UP)) < 0.95 else FORWARD
    u = (reference - axis * reference.dot(axis)).normalized()
    v = axis.cross(u)
    along = (length_a ** 2 - length_b ** 2 + distance ** 2) / (2.0 * distance)
    radius = math.sqrt(max(0.0, length_a ** 2 - along ** 2))
    return {'shoulder': shoulder, 'wrist': shoulder + axis * distance, 'axis': axis, 'center': shoulder + axis * along, 'radius': radius, 'u': u, 'v': v, 'over': overextension}


def perpendicular_direction(geometry, direction):
    axis = geometry['axis']
    result = direction - axis * direction.dot(axis)
    if result.length < 1e-6:
        return geometry['u'].copy()
    return result.normalized()


def arm_bones_side(geometry, source_pose, direction, side):
    upper_name = f'upperarm_{side}'
    lower_name = f'lowerarm_{side}'
    hand_name = f'hand_{side}'
    shoulder, wrist, axis = geometry['shoulder'], geometry['wrist'], geometry['axis']
    elbow = geometry['center'] + direction * geometry['radius']
    source_shoulder = source_pose[upper_name].translation
    source_elbow = source_pose[lower_name].translation
    source_wrist = source_pose[hand_name].translation
    source_axis = (source_wrist - source_shoulder).normalized()
    source_direction = source_elbow - source_shoulder
    source_direction = (source_direction - source_axis * source_direction.dot(source_axis)).normalized()
    source_normal = source_direction.cross(source_axis)
    normal = direction.cross(axis)
    upper = align_bone(source_pose[upper_name], source_elbow - source_shoulder, source_normal, shoulder, elbow - shoulder, normal)
    lower = align_bone(source_pose[lower_name], source_wrist - source_elbow, source_normal, elbow, wrist - elbow, normal)
    return elbow, upper, lower


def forearm_twist_side(lower_matrix, hand_matrix, side):
    rest_rel = REST_HAND_IN_LOWERARM_R if side == 'r' else REST_HAND_IN_LOWERARM_L
    forearm_local = FOREARM_LOCAL_R if side == 'r' else FOREARM_LOCAL_L
    relative = lower_matrix.to_3x3().inverted() @ hand_matrix.to_3x3()
    delta = (relative @ rest_rel.inverted()).to_quaternion()
    projection = Vector((delta.x, delta.y, delta.z)).dot(forearm_local)
    return 2.0 * math.atan2(projection, delta.w)


def twist_axis_in(name, side):
    lower_name = f'lowerarm_{side}'
    forearm_local = FOREARM_LOCAL_R if side == 'r' else FOREARM_LOCAL_L
    return (rest[name].to_3x3().inverted() @ rest[lower_name].to_3x3() @ forearm_local).normalized()


def twist_fractions(side):
    lower_name = f'lowerarm_{side}'
    hand_name = f'hand_{side}'
    twist_names = TWIST_BONES_R if side == 'r' else TWIST_BONES_L
    elbow = rest[lower_name].translation
    wrist = rest[hand_name].translation
    span = (wrist - elbow).length
    return {name: max(0.0, min(1.0, (rest[name].translation - elbow).dot((wrist - elbow).normalized()) / span)) for name in twist_names}


def hand_from_parameters_r(lower_matrix, twist_angle, flexion_component, deviation_component):
    neutral = (lower_matrix.to_3x3() @ REST_HAND_IN_LOWERARM_R).to_quaternion()
    twist = Quaternion(FOREARM_IN_HAND_R, twist_angle)
    radial = twist @ RADIAL_IN_HAND_R
    palm = FOREARM_IN_HAND_R.cross(radial)
    swing_vector = radial * flexion_component + palm * deviation_component
    swing = Quaternion(swing_vector.normalized(), swing_vector.length) if swing_vector.length > 1e-9 else Quaternion()
    return neutral @ swing @ twist


def hand_parameters_r(lower_matrix, hand_rotation):
    neutral = (lower_matrix.to_3x3() @ REST_HAND_IN_LOWERARM_R).to_quaternion()
    relative = neutral.inverted() @ hand_rotation
    direction = relative @ FOREARM_IN_HAND_R
    swing = FOREARM_IN_HAND_R.rotation_difference(direction)
    twist = swing.inverted() @ relative
    twist_angle = 2.0 * math.atan2(Vector((twist.x, twist.y, twist.z)).dot(FOREARM_IN_HAND_R), twist.w)
    radial = Quaternion(FOREARM_IN_HAND_R, twist_angle) @ RADIAL_IN_HAND_R
    palm = FOREARM_IN_HAND_R.cross(radial)
    swing_vector = swing.axis * swing.angle if swing.angle > 1e-9 else Vector()
    return twist_angle, swing_vector.dot(radial), swing_vector.dot(palm)


def hand_from_parameters_l(lower_matrix, twist_angle, flexion_component, deviation_component):
    neutral = (lower_matrix.to_3x3() @ REST_HAND_IN_LOWERARM_L).to_quaternion()
    twist = Quaternion(FOREARM_IN_HAND_L, twist_angle)
    radial = twist @ RADIAL_IN_HAND_L
    palm = FOREARM_IN_HAND_L.cross(radial)
    swing_vector = radial * flexion_component + palm * deviation_component
    swing = Quaternion(swing_vector.normalized(), swing_vector.length) if swing_vector.length > 1e-9 else Quaternion()
    return neutral @ swing @ twist


def hand_parameters_l(lower_matrix, hand_rotation):
    neutral = (lower_matrix.to_3x3() @ REST_HAND_IN_LOWERARM_L).to_quaternion()
    relative = neutral.inverted() @ hand_rotation
    direction = relative @ FOREARM_IN_HAND_L
    swing = FOREARM_IN_HAND_L.rotation_difference(direction)
    twist = swing.inverted() @ relative
    twist_angle = 2.0 * math.atan2(Vector((twist.x, twist.y, twist.z)).dot(FOREARM_IN_HAND_L), twist.w)
    radial = Quaternion(FOREARM_IN_HAND_L, twist_angle) @ RADIAL_IN_HAND_L
    palm = FOREARM_IN_HAND_L.cross(radial)
    swing_vector = swing.axis * swing.angle if swing.angle > 1e-9 else Vector()
    return twist_angle, swing_vector.dot(radial), swing_vector.dot(palm)


def soft_clamp(value, low, high, knee):
    if value > high - knee:
        return high - knee * math.exp(-(value - (high - knee)) / knee)
    if value < low + knee:
        return low + knee * math.exp((value - (low + knee)) / knee)
    return value


def unwrap(values):
    result = [values[0]]
    for value in values[1:]:
        result.append(result[-1] + math.remainder(value - result[-1], math.tau))
    return result


def grip_and_tip(lower_matrix, wrist, parameters):
    rotation = hand_from_parameters_r(lower_matrix, *parameters)
    hand = rotation.to_matrix().to_4x4()
    hand.translation = wrist
    grip = hand @ SOCKET_LOCAL
    return rotation, grip, grip @ TIP_LOCAL


def solve_leg(side, pose, source_pose, ankle_target, foot_rotation, ball_world_rotation, custom_pole=None):
    thigh, calf, foot, ball = LEG_BONES[side]
    hip = pose[thigh].translation
    source_hip, source_knee, source_ankle = (source_pose[name].translation for name in (thigh, calf, foot))
    if custom_pole is not None:
        pole = custom_pole
    else:
        leg_axis = (source_ankle - source_hip).normalized() if (source_ankle - source_hip).length > 1e-4 else -UP
        pole = source_knee - (source_hip + leg_axis * (source_knee - source_hip).dot(leg_axis))
    knee, ankle, overextension = solve_two_bone(hip, ankle_target, LENGTHS[thigh], LENGTHS[calf], pole)
    source_normal = (source_knee - source_hip).cross(source_ankle - source_knee)
    new_normal = (knee - hip).cross(ankle - knee)
    if new_normal.length < 1e-6:
        new_normal = source_normal
    result = {
        thigh: align_bone(source_pose[thigh], source_knee - source_hip, source_normal, hip, knee - hip, new_normal),
        calf: align_bone(source_pose[calf], source_ankle - source_knee, source_normal, knee, ankle - knee, new_normal),
    }
    foot_matrix = foot_rotation.to_matrix().to_4x4()
    foot_matrix.translation = ankle
    result[foot] = foot_matrix
    ball_head = foot_matrix @ child_offset(foot, ball)
    ball_matrix = ball_world_rotation.to_matrix().to_4x4()
    ball_matrix.translation = ball_head
    result[ball] = ball_matrix
    return result, overextension


def foot_pivot_pose(foot_matrix, ball_matrix, pivot, angle_degrees):
    toes = ball_matrix.translation - foot_matrix.translation
    toes.z = 0.0
    axis = UP.cross(toes.normalized() if toes.length > 1e-3 else FORWARD)
    rotation = Quaternion(axis.normalized(), math.radians(angle_degrees))
    return rotate_about(foot_matrix, pivot, rotation), rotate_about(ball_matrix, pivot, rotation)


def rear_heel_lift_for_reach(pelvis_pose, side, foot_target, max_heel_deg=40.0, max_pitch_deg=68.0):
    thigh, calf, foot, ball = LEG_BONES[side]
    hip = pelvis_pose[thigh].translation
    reach = (LENGTHS[thigh] + LENGTHS[calf]) * 0.98
    foot_matrix, ball_matrix = foot_target
    if (hip - foot_matrix.translation).length <= reach:
        return 0.0
    ankle_over_ball = foot_matrix.translation - ball_matrix.translation
    current_pitch = math.degrees(math.atan2(ankle_over_ball.z, Vector((ankle_over_ball.x, ankle_over_ball.y, 0.0)).length))
    allowed = min(max_heel_deg, max(0.0, max_pitch_deg - current_pitch))
    for step in range(1, int(allowed) + 1):
        lifted, _ = foot_pivot_pose(foot_matrix, ball_matrix, ball_matrix.translation, float(step))
        if (hip - lifted.translation).length <= reach:
            return float(step)
    return float(int(allowed))


def flatten_toes(foot_matrix, ball_matrix, reference_foot, reference_ball, low=1.5, high=4.0):
    weight = 1.0 - smoothstep(low, high, ball_matrix.translation.z)
    if weight <= 0.0:
        return ball_matrix
    forward_now = foot_matrix.to_3x3() @ (reference_foot.to_3x3().inverted() @ FORWARD)
    yaw = math.atan2(forward_now.x, -forward_now.y) - math.atan2(FORWARD.x, -FORWARD.y)
    flat = Quaternion(UP, yaw) @ reference_ball.to_quaternion()
    qb = ball_matrix.to_quaternion()
    if qb.dot(flat) < 0.0:
        flat.negate()
    rotation = qb.slerp(flat, weight)
    result = rotation.to_matrix().to_4x4()
    result.translation = ball_matrix.translation
    return result


def pelvis_drop_for_reach(pelvis_pose, ankle_targets):
    drop = 0.0
    for side, (thigh, calf, foot, ball) in LEG_BONES.items():
        hip = pelvis_pose[thigh].translation
        reach = (LENGTHS[thigh] + LENGTHS[calf]) * 0.985
        delta = hip - ankle_targets[side]
        horizontal = Vector((delta.x, delta.y, 0.0)).length
        if horizontal >= reach:
            drop = max(drop, delta.z)
            continue
        allowed_height = math.sqrt(reach ** 2 - horizontal ** 2)
        drop = max(drop, delta.z - allowed_height)
    return max(0.0, drop)


def solve_left_shield_arm_single(pose, source_pose, wrist_world, elbow_hint_world, shield_azimuth_cw_deg, shield_pitch_deg=6.0):
    shoulder_l = pose['upperarm_l'].translation
    max_shield_reach = (LENGTHS['upperarm_l'] + LENGTHS['lowerarm_l']) * 0.93
    delta_l = wrist_world - shoulder_l
    if delta_l.length > max_shield_reach and delta_l.length > 1e-5:
        wrist_world = shoulder_l + delta_l.normalized() * max_shield_reach
    geometry = arm_geometry_side(pose, wrist_world, 'l')
    direction = perpendicular_direction(geometry, elbow_hint_world - geometry['shoulder'])
    elbow, upper, lower = arm_bones_side(geometry, source_pose, direction, 'l')
    az_rad = math.radians(shield_azimuth_cw_deg)
    pitch_rad = math.radians(shield_pitch_deg)
    horiz_dir = (FORWARD * math.cos(az_rad) + RIGHT * math.sin(az_rad)).normalized()
    shield_normal = (horiz_dir * math.cos(pitch_rad) + UP * math.sin(pitch_rad)).normalized()
    shield_up = (UP - shield_normal * UP.dot(shield_normal)).normalized()
    shield_right = shield_up.cross(shield_normal).normalized()
    socket_rot = Matrix((shield_right, shield_up, shield_normal)).transposed()
    hand_rot_target = (socket_rot @ SHIELD_SOCKET_LOCAL.to_3x3().inverted()).to_quaternion()
    twist_a, flex_a, dev_a = hand_parameters_l(lower, hand_rot_target)
    knee = math.radians(8.0)
    flex_clamped = soft_clamp(flex_a, -math.radians(52.0), math.radians(52.0), knee)
    dev_clamped = soft_clamp(dev_a, -math.radians(32.0), math.radians(28.0), knee)
    hand_rot = hand_from_parameters_l(lower, twist_a, flex_clamped, dev_clamped)
    hand = hand_rot.to_matrix().to_4x4()
    hand.translation = geometry['wrist']
    return {'upperarm_l': upper, 'lowerarm_l': lower, 'hand_l': hand}, geometry['over']


def make_ready_pose():
    raw = sample_source('TD_Ref_Attack_PrimaryA', [102.0])[0]
    pelvis_pos = raw['pelvis'].translation
    wrist_l = pelvis_pos + char_vec(26.0, -34.0, 16.5)
    elbow_l = pelvis_pos + char_vec(2.0, -39.0, 16.0)
    left_arm, _ = solve_left_shield_arm_single(raw, raw, wrist_l, elbow_l, shield_azimuth_cw_deg=-32.0, shield_pitch_deg=6.0)
    overrides = dict(left_arm)
    full = forward_kinematics(raw, overrides)
    fractions_l = twist_fractions('l')
    twist_l = math.remainder(forearm_twist_side(full['lowerarm_l'], full['hand_l'], 'l') - forearm_twist_side(raw['lowerarm_l'], raw['hand_l'], 'l'), math.tau)
    for name, frac in fractions_l.items():
        overrides[name] = full[name] @ Quaternion(twist_axis_in(name, 'l'), twist_l * frac).to_matrix().to_4x4()
    for ik_name, target in (('ik_hand_gun', 'hand_r'), ('ik_hand_r', 'hand_r'), ('ik_hand_l', 'hand_l'), ('ik_foot_l', 'foot_l'), ('ik_foot_r', 'foot_r')):
        overrides[ik_name] = full[target].copy()
    return forward_kinematics(raw, overrides)


READY_POSE = make_ready_pose()
FLOOR_Z = {name: READY_POSE[name].translation.z for side in LEG_BONES for name in LEG_BONES[side][2:]}


def blend_arm_chain(pose, reference, weight, space, local_bones):
    overrides = {}
    for name in RIGHT_ARM_CHAIN:
        parent = parents[name]
        own_local = pose[parent].inverted() @ pose[name]
        parent_matrix = overrides.get(parent, pose[parent])
        if name in local_bones:
            reference_local = reference[parent].inverted() @ reference[name]
            rotation = own_local.to_quaternion().slerp(reference_local.to_quaternion(), weight * local_bones[name])
            local = rotation.to_matrix().to_4x4()
            local.translation = own_local.translation
            overrides[name] = parent_matrix @ local
            continue
        if space == 'armature':
            rotation = pose[name].to_quaternion().slerp(reference[name].to_quaternion(), weight)
            blended = rotation.to_matrix().to_4x4()
            blended.translation = parent_matrix @ own_local.translation
            overrides[name] = blended
            continue
        reference_local = reference[parent].inverted() @ reference[name]
        rotation = own_local.to_quaternion().slerp(reference_local.to_quaternion(), weight)
        local = rotation.to_matrix().to_4x4()
        local.translation = own_local.translation.lerp(reference_local.translation, weight)
        overrides[name] = parent_matrix @ local
    return forward_kinematics(pose, overrides)


def apply_arm_blends(source, source_action_name, blends):
    if not blends:
        return source
    active = [[index for index in range(len(source)) if pchip(blend['weight_keys'], index + 1) > 1e-4] for blend in blends]
    references = [dict(zip(frames, sample_source(source_action_name, [pchip(blend['reference_source_frames'], index + 1) for index in frames])))
                  for blend, frames in zip(blends, active)]
    result = []
    for index, pose in enumerate(source):
        for blend, reference in zip(blends, references):
            if index in reference:
                pose = blend_arm_chain(pose, reference[index], pchip(blend['weight_keys'], index + 1), blend['space'], blend['local_bones'])
        result.append(pose)
    return result


def level_right_arm_plane_in_pose(pose, target_wrist_z, target_tip_z, weight):
    if weight <= 1e-5:
        return pose
    sh = pose['upperarm_r'].translation
    upper_m = pose['upperarm_r']
    lower_m = pose['lowerarm_r']
    hand_m = pose['hand_r']
    w_pos = hand_m.translation
    rel_w = w_pos - sh
    horiz_w = max(math.hypot(rel_w.x, rel_w.y), 15.0)
    cur_elev_w = math.atan2(rel_w.z, horiz_w)
    tgt_elev_w = math.atan2(target_wrist_z - sh.z, horiz_w)
    d_elev_w = (tgt_elev_w - cur_elev_w) * weight
    rad_w = Vector((rel_w.x, rel_w.y, 0.0))
    rad_w = rad_w.normalized() if rad_w.length > 1e-4 else FORWARD
    axis_w = UP.cross(rad_w).normalized()
    q_w = Quaternion(axis_w, -d_elev_w)
    rot_sh = Matrix.Translation(sh) @ q_w.to_matrix().to_4x4() @ Matrix.Translation(-sh)
    upper2 = rot_sh @ upper_m
    lower2 = rot_sh @ lower_m
    hand2 = rot_sh @ hand_m

    w2 = hand2.translation
    tip2 = (hand2 @ SOCKET_LOCAL) @ TIP_LOCAL
    rel_t = tip2 - w2
    horiz_t = max(math.hypot(rel_t.x, rel_t.y), 20.0)
    cur_elev_t = math.atan2(rel_t.z, horiz_t)
    tgt_elev_t = math.atan2(target_tip_z - w2.z, horiz_t)
    d_elev_t = max(min((tgt_elev_t - cur_elev_t) * weight, math.radians(42.0)), -math.radians(42.0))
    rad_t = Vector((rel_t.x, rel_t.y, 0.0))
    rad_t = rad_t.normalized() if rad_t.length > 1e-4 else FORWARD
    axis_t = UP.cross(rad_t).normalized()
    q_t = Quaternion(axis_t, -d_elev_t)
    hand3 = Matrix.Translation(w2) @ q_t.to_matrix().to_4x4() @ Matrix.Translation(-w2) @ hand2
    return forward_kinematics(pose, {'upperarm_r': upper2, 'lowerarm_r': lower2, 'hand_r': hand3})


def clearance_offsets(lowers, wrists, curves, minimum_heights, maximum_heights, limits):
    count = len(lowers)
    flexion_range = (-math.radians(limits['flexion'][1]), -math.radians(limits['flexion'][0]))
    deviation_range = (math.radians(limits['deviation'][0]), math.radians(limits['deviation'][1]))
    flexion_offsets, deviation_offsets = [], []
    for index in range(count):
        parameters = (curves[0][index], curves[1][index], curves[2][index])
        clearance = minimum_heights[index]
        ceiling = maximum_heights[index]
        if clearance <= grip_and_tip(lowers[index], wrists[index], parameters)[2].z <= ceiling:
            flexion_offsets.append(0.0)
            deviation_offsets.append(0.0)
            continue
        best = None
        for flexion_step in range(-24, 25):
            for deviation_step in range(-18, 19):
                flexion = min(max(parameters[1] + math.radians(flexion_step * 2), flexion_range[0]), flexion_range[1])
                deviation = min(max(parameters[2] + math.radians(deviation_step * 2), deviation_range[0]), deviation_range[1])
                tip = grip_and_tip(lowers[index], wrists[index], (parameters[0], flexion, deviation))[2]
                shortfall = max(0.0, clearance - tip.z, tip.z - ceiling)
                cost = (flexion - parameters[1]) ** 2 + (deviation - parameters[2]) ** 2 + (0.0 if shortfall <= 0.0 else 1e3 + shortfall * 10.0)
                if best is None or cost < best[0]:
                    best = (cost, flexion - parameters[1], deviation - parameters[2])
        flexion_offsets.append(best[1])
        deviation_offsets.append(best[2])

    def spread(values):
        magnitude = max_filter([abs(v) for v in values], 4)
        signs = gaussian_smooth(values, 3.0)
        return gaussian_smooth([math.copysign(m, s) if abs(s) > 1e-9 else 0.0 for m, s in zip(magnitude, signs)], 2.5)

    return spread(flexion_offsets), spread(deviation_offsets)


def solve_right_arm_stage(rigid_poses, source, spec):
    count = len(source)
    wrists = [rigid['hand_r'].translation.copy() for rigid in rigid_poses]
    lowers, elbows, geometries, directions = [], [], [], []
    for index in range(count):
        geometry = arm_geometry_side(rigid_poses[index], wrists[index], 'r')
        elbow_vec = source[index]['lowerarm_r'].translation - source[index]['upperarm_r'].translation
        direction = perpendicular_direction(geometry, elbow_vec)
        elbow, _, lower = arm_bones_side(geometry, source[index], direction, 'r')
        geometries.append(geometry)
        directions.append(direction)
        elbows.append(elbow)
        lowers.append(lower)

    raw = [hand_parameters_r(lowers[i], source[i]['hand_r'].to_quaternion()) for i in range(count)]
    limits = spec['wrist_hard_limits']
    knee = math.radians(spec.get('wrist_knee_deg', 10.0))
    flexion_low, flexion_high = -math.radians(limits['flexion'][1]), -math.radians(limits['flexion'][0])
    deviation_low, deviation_high = math.radians(limits['deviation'][0]), math.radians(limits['deviation'][1])
    twist = unwrap([p[0] for p in raw])
    sigma = spec.get('wrist_curve_sigma', 1.5)
    protect = spec.get('wrist_protect_window', [20, 23, 35, 39])
    strength = [1.0 - window_bump(protect, index + 1) for index in range(count)]
    flexion_correction = gaussian_smooth([(soft_clamp(p[1], flexion_low, flexion_high, knee) - p[1]) * strength[i] for i, p in enumerate(raw)], sigma)
    deviation_correction = gaussian_smooth([(soft_clamp(p[2], deviation_low, deviation_high, knee) - p[2]) * strength[i] for i, p in enumerate(raw)], sigma)
    flexion = [p[1] + c for p, c in zip(raw, flexion_correction)]
    deviation = [p[2] + c for p, c in zip(raw, deviation_correction)]
    curves = [twist, flexion, deviation]

    base_floor = spec.get('tip_clearance_cm', 22.0)
    minimum_heights = [base_floor] * count
    maximum_heights = [1e6] * count
    for _ in range(spec.get('iterations', 2)):
        flexion_offsets, deviation_offsets = clearance_offsets(lowers, wrists, curves, minimum_heights, maximum_heights, limits)
        curves[1] = [value if abs(offset) < 1e-9 else min(max(value + offset, flexion_low), flexion_high) for value, offset in zip(curves[1], flexion_offsets)]
        curves[2] = [value if abs(offset) < 1e-9 else min(max(value + offset, deviation_low), deviation_high) for value, offset in zip(curves[2], deviation_offsets)]

    arms = []
    for index in range(count):
        geometry = geometries[index]
        elbow, upper, lower = arm_bones_side(geometry, source[index], directions[index], 'r')
        rotation = hand_from_parameters_r(lower, curves[0][index], curves[1][index], curves[2][index])
        hand = rotation.to_matrix().to_4x4()
        hand.translation = geometry['wrist']
        flexion_value, deviation_value = wrist_angles_r(hand, (geometry['wrist'] - elbow).normalized())
        arms.append(({'upperarm_r': upper, 'lowerarm_r': lower, 'hand_r': hand}, flexion_value, deviation_value, geometry['over'], elbow))
    return arms


def build_anchored_foot_targets(frame_count, start_pose_rel, end_pose_world, foot_plans, lead_side, lead_pitch_keys):
    targets_per_frame = [{} for _ in range(frame_count)]
    for side, (thigh, calf, foot, ball) in LEG_BONES.items():
        start_foot = start_pose_rel[foot].copy()
        start_ball = start_pose_rel[ball].copy()
        end_foot = end_pose_world[foot].copy()
        end_ball = end_pose_world[ball].copy()
        total_delta_ball = end_ball.translation - start_ball.translation
        total_delta_ball.z = 0.0
        plans = foot_plans[side]
        ball_local_in_foot = child_offset(foot, ball)
        for index in range(frame_count):
            frame = index + 1
            progress = 0.0
            lift = 0.0
            for begin, end, share, height in plans:
                if frame <= begin:
                    continue
                if frame >= end:
                    progress += share
                    continue
                fraction = (frame - begin) / float(end - begin)
                progress += share * smoothstep(0.0, 1.0, fraction)
                lift += height * (math.sin(math.pi * fraction) ** 0.58)
            progress = max(0.0, min(1.0, progress))
            foot_mat = blend_matrix(start_foot, end_foot, progress)
            ball_mat = blend_matrix(start_ball, end_ball, progress)
            ball_xy = start_ball.translation + total_delta_ball * progress
            ball_pos = Vector((ball_xy.x, ball_xy.y, FLOOR_Z[ball]))
            foot_rot_3 = foot_mat.to_3x3()
            ankle_from_ball = ball_pos - (foot_rot_3 @ ball_local_in_foot)
            foot_mat.translation = Vector((ankle_from_ball.x, ankle_from_ball.y, FLOOR_Z[foot]))
            ball_mat.translation = ball_pos
            if side == lead_side and lead_pitch_keys:
                pitch = pchip(lead_pitch_keys, frame)
                if abs(pitch) > 1e-4:
                    heel = foot_mat.translation - FORWARD * 5.0
                    heel.z = 0.0
                    pivot = ball_mat.translation if pitch > 0 else heel
                    foot_mat, ball_mat = foot_pivot_pose(foot_mat, ball_mat, pivot, pitch)
            raise_by = max(0.0, FLOOR_Z[foot] - foot_mat.translation.z, FLOOR_Z[ball] - ball_mat.translation.z)
            foot_mat.translation = foot_mat.translation + UP * (raise_by + lift)
            ball_mat.translation = ball_mat.translation + UP * (raise_by + lift)
            targets_per_frame[index][side] = (foot_mat, ball_mat)
    return targets_per_frame


def build_standard_stage(spec, start_pose_rel):
    time_keys = spec['time_keys']
    frame_count = int(time_keys[-1][0])
    times = [pchip(time_keys, frame) for frame in range(1, frame_count + 1)]
    raw_source = sample_source(spec['source_action'], times)
    source = apply_arm_blends(raw_source, spec['source_action'], spec.get('arm_blends', []))
    travel = spec['travel_cm']
    root_travel = [travel * pchip(spec['root_travel_keys'], frame) for frame in range(1, frame_count + 1)]
    end_pose_world = shift_pose(READY_POSE, FORWARD * travel)

    plane_level_cfg = spec.get('plane_level')
    leveled_source = []
    for index, pose in enumerate(source):
        frame = index + 1
        p_world = shift_pose(pose, FORWARD * root_travel[index])
        if plane_level_cfg:
            wt = window_bump(plane_level_cfg['window'], frame) * plane_level_cfg.get('strength', 0.9)
            p_world = level_right_arm_plane_in_pose(p_world, plane_level_cfg['wrist_z_cm'], plane_level_cfg['tip_z_cm'], wt)
        leveled_source.append(p_world)

    start_blend_end = spec.get('start_blend_frames', 14)
    end_blend_start = frame_count - spec.get('end_blend_frames', 18)
    blended_source = []
    for index, pose_world in enumerate(leveled_source):
        frame = index + 1
        w_start = 1.0 - quintic_step((frame - 1.0) / float(max(1, start_blend_end - 1))) if frame <= start_blend_end else 0.0
        w_end = quintic_step((frame - end_blend_start) / float(max(1, frame_count - end_blend_start))) if frame >= end_blend_start else 0.0
        if w_start > 1e-5:
            pose_world = blend_poses_local(pose_world, start_pose_rel, w_start, skip_legs=False)
        if w_end > 1e-5:
            pose_world = blend_poses_local(pose_world, end_pose_world, w_end, skip_legs=False)
        blended_source.append(pose_world)

    lead = spec['lead_foot']
    rear = 'r' if lead == 'l' else 'l'
    foot_targets = build_anchored_foot_targets(
        frame_count, start_pose_rel, end_pose_world,
        spec['foot_plans'], lead, spec.get('lead_pitch_keys', [])
    )

    idle_pelvis_height = READY_POSE['pelvis'].translation.z
    crouch = spec.get('crouch_scale', 0.75)
    pelvis_locations = []
    for index, pose in enumerate(blended_source):
        loc = pose['pelvis'].translation.copy()
        if loc.z < idle_pelvis_height:
            loc.z = idle_pelvis_height - (idle_pelvis_height - loc.z) * crouch
        pelvis_locations.append(loc)

    heel_lifts = []
    for index, pose in enumerate(blended_source):
        trial = forward_kinematics(pose, {'pelvis': with_translation(pose['pelvis'], pelvis_locations[index])})
        if any(begin <= index + 1 <= end for begin, end, _, _ in spec['foot_plans'][rear]):
            heel_lifts.append(0.0)
        else:
            heel_lifts.append(rear_heel_lift_for_reach(trial, rear, foot_targets[index][rear]))
    heel_lifts = gaussian_smooth(max_filter(heel_lifts, 2), 1.5)
    for index in range(frame_count):
        w_start = 1.0 - smoothstep(1.0, 6.0, float(index + 1))
        w_end = smoothstep(float(frame_count - 8), float(frame_count), float(index + 1))
        lift_deg = heel_lifts[index] * (1.0 - max(w_start, w_end))
        if lift_deg > 1e-3:
            foot_matrix, ball_matrix = foot_targets[index][rear]
            foot_targets[index][rear] = foot_pivot_pose(foot_matrix, ball_matrix, ball_matrix.translation, lift_deg)

    reference_feet = {side: (READY_POSE[LEG_BONES[side][2]], READY_POSE[LEG_BONES[side][3]]) for side in LEG_BONES}
    for index in range(frame_count):
        if any(begin <= index + 1 <= end for begin, end, _, _ in spec['foot_plans'][rear]):
            continue
        foot_matrix, ball_matrix = foot_targets[index][rear]
        foot_targets[index][rear] = (foot_matrix, flatten_toes(foot_matrix, ball_matrix, *reference_feet[rear]))

    drops = []
    for index, pose in enumerate(blended_source):
        trial = forward_kinematics(pose, {'pelvis': with_translation(pose['pelvis'], pelvis_locations[index])})
        drops.append(pelvis_drop_for_reach(trial, {side: foot_targets[index][side][0].translation for side in LEG_BONES}))
    drops = gaussian_smooth(max_filter(drops, 2), 1.5)

    rigid_poses = []
    for index, pose in enumerate(blended_source):
        location = pelvis_locations[index] - UP * drops[index]
        rigid_poses.append(forward_kinematics(pose, {'pelvis': with_translation(pose['pelvis'], location)}))

    arms_r = solve_right_arm_stage(rigid_poses, blended_source, spec)
    fractions_r = twist_fractions('r')
    fractions_l = twist_fractions('l')
    shield_keys = spec['shield_keys']

    results = []
    diagnostics = []
    previous_twist_r = 0.0
    previous_twist_l = 0.0
    for index, pose in enumerate(blended_source):
        frame = index + 1
        arm_r, flexion_r, deviation_r, arm_over_r, elbow_r = arms_r[index]
        overrides = {'pelvis': rigid_poses[index]['pelvis']}
        overrides.update(arm_r)

        partial_for_shield = forward_kinematics(pose, overrides)
        pelvis_pos = partial_for_shield['pelvis'].translation
        fwd_l = pchip([(k[0], k[1]) for k in shield_keys], frame)
        rgt_l = pchip([(k[0], k[2]) for k in shield_keys], frame)
        up_l = pchip([(k[0], k[3]) for k in shield_keys], frame)
        az_l = pchip([(k[0], k[4]) for k in shield_keys], frame)
        wrist_l = pelvis_pos + char_vec(fwd_l, rgt_l, up_l)
        elbow_l_hint = pelvis_pos + char_vec(fwd_l - 22.0, rgt_l - 8.0, up_l - 2.0)
        arm_l, arm_over_l = solve_left_shield_arm_single(partial_for_shield, READY_POSE, wrist_l, elbow_l_hint, shield_azimuth_cw_deg=az_l, shield_pitch_deg=6.0)
        overrides.update(arm_l)

        full_upper = forward_kinematics(pose, overrides)
        leg_over = {}
        leg_overrides = {}
        for side in LEG_BONES:
            foot_matrix, ball_matrix = foot_targets[index][side]
            leg, leg_over[side] = solve_leg(side, full_upper, pose, foot_matrix.translation, foot_matrix.to_quaternion(), ball_matrix.to_quaternion())
            leg_overrides.update(leg)
        full = forward_kinematics(full_upper, leg_overrides)

        twist_r = math.remainder(forearm_twist_side(full['lowerarm_r'], full['hand_r'], 'r') - forearm_twist_side(pose['lowerarm_r'], pose['hand_r'], 'r'), math.tau)
        if index > 0:
            twist_r = previous_twist_r + math.remainder(twist_r - previous_twist_r, math.tau)
        previous_twist_r = twist_r
        twist_l = math.remainder(forearm_twist_side(full['lowerarm_l'], full['hand_l'], 'l') - forearm_twist_side(READY_POSE['lowerarm_l'], READY_POSE['hand_l'], 'l'), math.tau)
        if index > 0:
            twist_l = previous_twist_l + math.remainder(twist_l - previous_twist_l, math.tau)
        previous_twist_l = twist_l

        post_overrides = {}
        for name, frac in fractions_r.items():
            post_overrides[name] = full[name] @ Quaternion(twist_axis_in(name, 'r'), twist_r * frac).to_matrix().to_4x4()
        for name, frac in fractions_l.items():
            post_overrides[name] = full[name] @ Quaternion(twist_axis_in(name, 'l'), twist_l * frac).to_matrix().to_4x4()
        for ik_name, target in (('ik_hand_gun', 'hand_r'), ('ik_hand_r', 'hand_r'), ('ik_hand_l', 'hand_l'), ('ik_foot_l', 'foot_l'), ('ik_foot_r', 'foot_r')):
            post_overrides[ik_name] = full[target].copy()
        full = forward_kinematics(full, post_overrides)
        results.append(full)
        diagnostics.append({
            'frame': frame,
            'arm_over_cm': round(max(arm_over_r, arm_over_l), 2),
            'leg_over_cm': {k: round(v, 2) for k, v in leg_over.items()},
            'root_travel_cm': round(root_travel[index], 2),
        })
    return results, root_travel, diagnostics


def build_spin_stage(spec, start_pose_rel):
    time_keys = spec['time_keys']
    frame_count = int(time_keys[-1][0])
    times = [pchip(time_keys, frame) for frame in range(1, frame_count + 1)]
    raw_source = sample_source(spec['source_action'], times)
    travel = spec['travel_cm']
    root_travel = [travel * pchip(spec['root_travel_keys'], frame) for frame in range(1, frame_count + 1)]
    end_pose_world = shift_pose(READY_POSE, FORWARD * travel)

    plane_level_cfg = spec['plane_level']
    extra_spin_keys = spec['extra_spin_yaw_keys']
    extra_chest_keys = spec['extra_chest_yaw_keys']

    spun_source = []
    spin_yaws = []
    for index, pose in enumerate(raw_source):
        frame = index + 1
        p_world = shift_pose(pose, FORWARD * root_travel[index])
        wt = window_bump(plane_level_cfg['window'], frame) * plane_level_cfg.get('strength', 0.95)
        p_leveled = level_right_arm_plane_in_pose(p_world, plane_level_cfg['wrist_z_cm'], plane_level_cfg['tip_z_cm'], wt)

        extra_yaw = pchip(extra_spin_keys, frame)
        extra_chest = pchip(extra_chest_keys, frame)
        spin_yaws.append(extra_yaw)
        q_spin = cw_quat(extra_yaw)
        pelvis_m = p_leveled['pelvis'].copy()
        pelvis_rot = q_spin @ pelvis_m.to_quaternion()
        new_pelvis = pelvis_rot.to_matrix().to_4x4()
        new_pelvis.translation = pelvis_m.translation
        p_spun = forward_kinematics(p_leveled, {'pelvis': new_pelvis})

        if abs(extra_chest) > 1e-4:
            sp_overrides = {}
            for sp_name, sp_w in zip(SPINE_BONES, SPINE_WEIGHTS):
                q_sp = cw_quat(extra_chest * sp_w)
                sp_m = p_spun[sp_name]
                new_sp = (q_sp @ sp_m.to_quaternion()).to_matrix().to_4x4()
                new_sp.translation = sp_m.translation
                sp_overrides[sp_name] = new_sp
                p_spun = forward_kinematics(p_spun, sp_overrides)
        spun_source.append(p_spun)

    start_blend_end = spec.get('start_blend_frames', 14)
    end_blend_start = frame_count - spec.get('end_blend_frames', 18)
    end_blend_end = frame_count - spec.get('end_blend_hold_frames', 6)
    blended_source = []
    for index, pose_world in enumerate(spun_source):
        frame = index + 1
        w_start = 1.0 - quintic_step((frame - 1.0) / float(max(1, start_blend_end - 1))) if frame <= start_blend_end else 0.0
        w_end = smoothstep(float(end_blend_start), float(end_blend_end), float(frame)) if frame >= end_blend_start else 0.0
        if w_start > 1e-5:
            pose_world = blend_poses_local(pose_world, start_pose_rel, w_start, skip_legs=False)
        if w_end > 1e-5:
            pose_world = blend_poses_local(pose_world, end_pose_world, w_end, skip_legs=False)
        blended_source.append(pose_world)

    mid_l_ball = char_vec(38.0, 15.0, FLOOR_Z['ball_l'])
    mid_r_ball = char_vec(18.0, -14.0, FLOOR_Z['ball_r'])
    spin_foot_anchors = {
        'l': [
            (1, 14, start_pose_rel['ball_l'].translation.copy(), 0.0, 0.0),
            (14, 28, mid_l_ball.copy(), 5.5, 180.0),
            (28, 36, mid_l_ball.copy(), 0.0, 180.0),
            (36, 52, end_pose_world['ball_l'].translation.copy(), 5.5, 360.0),
            (52, frame_count, end_pose_world['ball_l'].translation.copy(), 0.0, 360.0),
        ],
        'r': [
            (1, 10, start_pose_rel['ball_r'].translation.copy(), 0.0, 0.0),
            (10, 23, mid_r_ball.copy(), 4.5, 175.0),
            (23, 27, mid_r_ball.copy(), 0.0, 175.0),
            (27, 40, end_pose_world['ball_r'].translation.copy(), 6.0, 360.0),
            (40, frame_count, end_pose_world['ball_r'].translation.copy(), 0.0, 360.0),
        ],
    }

    def sample_spin_foot(side, frame):
        thigh, calf, foot, ball = LEG_BONES[side]
        segments = spin_foot_anchors[side]
        ball_local_in_foot = child_offset(foot, ball)
        prev_ball = start_pose_rel[ball].translation.copy()
        prev_yaw = 0.0
        for seg_idx, (f0, f1, target_ball, lift_cm, end_yaw_cw) in enumerate(segments):
            if seg_idx > 0:
                prev_ball = segments[seg_idx - 1][2].copy()
                prev_yaw = segments[seg_idx - 1][4]
            if frame <= f1 or seg_idx == len(segments) - 1:
                t = 0.0 if f1 == f0 else max(0.0, min(1.0, (frame - f0) / float(f1 - f0)))
                s = smoothstep(0.0, 1.0, t)
                yaw_cw = prev_yaw + (end_yaw_cw - prev_yaw) * s
                q_yaw = cw_quat(yaw_cw)
                foot_rot = q_yaw @ READY_POSE[foot].to_quaternion()
                ball_rot = q_yaw @ READY_POSE[ball].to_quaternion()
                if lift_cm > 0.0:
                    b_pos = prev_ball.lerp(target_ball, s)
                    lift_val = lift_cm * (math.sin(math.pi * t) ** 0.58)
                    b_pos.z = FLOOR_Z[ball] + lift_val
                else:
                    b_pos = Vector((target_ball.x, target_ball.y, FLOOR_Z[ball]))
                    lift_val = 0.0
                foot_mat = foot_rot.to_matrix().to_4x4()
                ankle_pos = b_pos - (foot_mat.to_3x3() @ ball_local_in_foot)
                foot_mat.translation = Vector((ankle_pos.x, ankle_pos.y, FLOOR_Z[foot] + lift_val))
                ball_mat = ball_rot.to_matrix().to_4x4()
                ball_mat.translation = b_pos
                return foot_mat, ball_mat, yaw_cw
        return READY_POSE[foot].copy(), READY_POSE[ball].copy(), 360.0

    idle_pelvis_height = READY_POSE['pelvis'].translation.z
    crouch = spec.get('crouch_scale', 0.65)
    pelvis_locations = []
    for index, pose in enumerate(blended_source):
        loc = pose['pelvis'].translation.copy()
        if loc.z < idle_pelvis_height:
            loc.z = idle_pelvis_height - (idle_pelvis_height - loc.z) * crouch
        pelvis_locations.append(loc)

    foot_samples = [(sample_spin_foot('l', i + 1), sample_spin_foot('r', i + 1)) for i in range(frame_count)]
    drops = []
    for index, pose in enumerate(blended_source):
        trial = forward_kinematics(pose, {'pelvis': with_translation(pose['pelvis'], pelvis_locations[index])})
        f_l, f_r = foot_samples[index][0][0], foot_samples[index][1][0]
        drops.append(pelvis_drop_for_reach(trial, {'l': f_l.translation, 'r': f_r.translation}))
    drops = gaussian_smooth(max_filter(drops, 2), 1.5)

    rigid_poses = []
    for index, pose in enumerate(blended_source):
        location = pelvis_locations[index] - UP * drops[index]
        rigid_poses.append(forward_kinematics(pose, {'pelvis': with_translation(pose['pelvis'], location)}))

    arms_r = solve_right_arm_stage(rigid_poses, blended_source, spec)
    fractions_r = twist_fractions('r')
    fractions_l = twist_fractions('l')

    results = []
    diagnostics = []
    previous_twist_r = 0.0
    previous_twist_l = 0.0
    for index, pose in enumerate(blended_source):
        frame = index + 1
        arm_r, flexion_r, deviation_r, arm_over_r, elbow_r = arms_r[index]
        overrides = {'pelvis': rigid_poses[index]['pelvis']}
        overrides.update(arm_r)

        partial_for_shield = forward_kinematics(pose, overrides)
        pelvis_pos = partial_for_shield['pelvis'].translation
        q_spin = cw_quat(spin_yaws[index])
        sh_open = window_bump([12, 20, 36, 58], frame)
        sh_local = char_vec(26.0 - 8.0 * sh_open, -34.0 - 6.0 * sh_open, 16.5 + 3.5 * sh_open)
        wrist_l = pelvis_pos + (q_spin @ sh_local)
        elbow_l_hint = pelvis_pos + (q_spin @ char_vec(2.0 - 8.0 * sh_open, -39.0 - 4.0 * sh_open, 16.0))
        arm_l, arm_over_l = solve_left_shield_arm_single(partial_for_shield, READY_POSE, wrist_l, elbow_l_hint, shield_azimuth_cw_deg=spin_yaws[index] - 32.0 - 14.0 * sh_open, shield_pitch_deg=6.0)
        overrides.update(arm_l)

        full_upper = forward_kinematics(pose, overrides)
        (foot_l_mat, ball_l_mat, yaw_l), (foot_r_mat, ball_r_mat, yaw_r) = foot_samples[index]
        leg_over = {}
        leg_overrides = {}
        for side, (f_mat, b_mat, f_yaw) in (('l', (foot_l_mat, ball_l_mat, yaw_l)), ('r', (foot_r_mat, ball_r_mat, yaw_r))):
            mid_yaw = spin_yaws[index] * 0.45 + f_yaw * 0.55
            q_pole = cw_quat(mid_yaw)
            out_side = LEFT * 10.0 if side == 'l' else RIGHT * 10.0
            pole_vec = (q_pole @ (FORWARD * 28.0 + out_side)).normalized()
            leg, leg_over[side] = solve_leg(side, full_upper, READY_POSE, f_mat.translation, f_mat.to_quaternion(), b_mat.to_quaternion(), custom_pole=pole_vec)
            leg_overrides.update(leg)
        full = forward_kinematics(full_upper, leg_overrides)

        twist_r = math.remainder(forearm_twist_side(full['lowerarm_r'], full['hand_r'], 'r') - forearm_twist_side(pose['lowerarm_r'], pose['hand_r'], 'r'), math.tau)
        if index > 0:
            twist_r = previous_twist_r + math.remainder(twist_r - previous_twist_r, math.tau)
        previous_twist_r = twist_r
        twist_l = math.remainder(forearm_twist_side(full['lowerarm_l'], full['hand_l'], 'l') - forearm_twist_side(READY_POSE['lowerarm_l'], READY_POSE['hand_l'], 'l'), math.tau)
        if index > 0:
            twist_l = previous_twist_l + math.remainder(twist_l - previous_twist_l, math.tau)
        previous_twist_l = twist_l

        post_overrides = {}
        for name, frac in fractions_r.items():
            post_overrides[name] = full[name] @ Quaternion(twist_axis_in(name, 'r'), twist_r * frac).to_matrix().to_4x4()
        for name, frac in fractions_l.items():
            post_overrides[name] = full[name] @ Quaternion(twist_axis_in(name, 'l'), twist_l * frac).to_matrix().to_4x4()
        for ik_name, target in (('ik_hand_gun', 'hand_r'), ('ik_hand_r', 'hand_r'), ('ik_hand_l', 'hand_l'), ('ik_foot_l', 'foot_l'), ('ik_foot_r', 'foot_r')):
            post_overrides[ik_name] = full[target].copy()
        full = forward_kinematics(full, post_overrides)
        results.append(full)
        diagnostics.append({
            'frame': frame,
            'arm_over_cm': round(max(arm_over_r, arm_over_l), 2),
            'leg_over_cm': {k: round(v, 2) for k, v in leg_over.items()},
            'root_travel_cm': round(root_travel[index], 2),
        })
    return results, root_travel, diagnostics


def write_action(action_name, results, root_travel):
    old = bpy.data.actions.get(action_name)
    if old is not None:
        bpy.data.actions.remove(old)
    action = bpy.data.actions.new(action_name)
    action.use_fake_user = True
    armature.animation_data.action = action
    armature.rotation_mode = 'QUATERNION'
    previous_rotation = {}
    for index, world_pose in enumerate(results):
        frame = index + 1
        pose = shift_pose(world_pose, -FORWARD * root_travel[index])
        armature.location = (0.0, -root_travel[index] * 0.01, 0.0)
        armature.rotation_quaternion = Quaternion()
        armature.keyframe_insert('location', frame=frame, group='root')
        armature.keyframe_insert('rotation_quaternion', frame=frame, group='root')
        for bone_name in order:
            parent = parents[bone_name]
            if parent is None:
                basis = rest[bone_name].inverted() @ pose[bone_name]
            else:
                basis = (pose[parent] @ rest[parent].inverted() @ rest[bone_name]).inverted() @ pose[bone_name]
            location, rotation, scale = basis.decompose()
            if bone_name in previous_rotation and previous_rotation[bone_name].dot(rotation) < 0:
                rotation.negate()
            previous_rotation[bone_name] = rotation
            bone = bones[bone_name]
            bone.location, bone.rotation_quaternion, bone.scale = location, rotation, scale
            bone.keyframe_insert('location', frame=frame, group=bone_name)
            bone.keyframe_insert('rotation_quaternion', frame=frame, group=bone_name)
            bone.keyframe_insert('scale', frame=frame, group=bone_name)
    scene.frame_start, scene.frame_end = 1, len(results)
    return action


STAGE_SPECS = [
    {
        'output_action': 'TD_SwordCombo01',
        'source_action': 'TD_Ref_Attack_C_Med',
        'branch_frame': 36,
        'time_keys': [(1, 14.5), (15, 14.8), (20, 19.0), (26, 23.0), (36, 25.0), (58, 58.0), (76, 84.0), (86, 101.0)],
        'travel_cm': 34.0,
        'root_travel_keys': [(1, 0.0), (6, 0.0), (12, 0.16), (17, 0.50), (22, 0.84), (27, 0.96), (32, 1.0), (86, 1.0)],
        'lead_foot': 'l',
        'foot_plans': {
            'l': [(7, 19, 1.0, 8.5)],
            'r': [(24, 34, 0.55, 6.5), (54, 66, 0.45, 6.5)],
        },
        'lead_pitch_keys': [(5, 0.0), (8, 12.0), (12, 4.0), (15, 0.0), (18, -9.0), (20, 0.0)],
        'start_blend_frames': 15,
        'end_blend_frames': 16,
        'crouch_scale': 0.72,
        'plane_level': {'window': [1, 12, 34, 46], 'wrist_z_cm': 104.0, 'tip_z_cm': 106.0, 'strength': 0.92},
        'tip_clearance_cm': 24.0,
        'wrist_hard_limits': {'flexion': [-78.0, 62.0], 'deviation': [-38.0, 28.0]},
        'wrist_protect_window': [15, 17, 24, 27],
        'shield_keys': [
            (1, 26.0, -34.0, 16.5, -32.0),
            (13, 32.0, -24.0, 18.0, -18.0),
            (23, -12.0, -48.0, 22.0, -72.0),
            (36, -10.0, -46.0, 21.0, -68.0),
            (66, 22.0, -35.0, 17.0, -36.0),
            (86, 26.0, -34.0, 16.5, -32.0),
        ],
    },
    {
        'output_action': 'TD_SwordCombo02',
        'source_action': 'TD_Ref_Attack_PrimaryB',
        'branch_frame': 36,
        'time_keys': [(1, 21.2), (14, 21.5), (20, 26.5), (26, 29.8), (36, 31.2), (60, 32.2), (86, 32.8)],
        'travel_cm': 36.0,
        'root_travel_keys': [(1, 0.0), (6, 0.0), (12, 0.16), (17, 0.50), (22, 0.84), (27, 0.96), (32, 1.0), (86, 1.0)],
        'lead_foot': 'r',
        'foot_plans': {
            'r': [(7, 20, 1.0, 9.0)],
            'l': [(24, 34, 0.55, 6.5), (54, 66, 0.45, 6.5)],
        },
        'lead_pitch_keys': [(5, 0.0), (8, 12.0), (12, 4.0), (16, 0.0), (19, -9.0), (21, 0.0)],
        'start_blend_frames': 14,
        'end_blend_frames': 48,
        'crouch_scale': 0.72,
        'plane_level': {'window': [1, 10, 34, 52], 'wrist_z_cm': 104.0, 'tip_z_cm': 106.0, 'strength': 0.95},
        'tip_clearance_cm': 24.0,
        'wrist_hard_limits': {'flexion': [-78.0, 65.0], 'deviation': [-38.0, 30.0]},
        'wrist_protect_window': [15, 17, 25, 28],
        'shield_keys': [
            (1, -10.0, -46.0, 21.0, -68.0),
            (13, -14.0, -48.0, 25.0, -76.0),
            (24, 28.0, -32.0, 18.0, -28.0),
            (36, 30.0, -30.0, 18.0, -25.0),
            (66, 26.0, -34.0, 16.5, -32.0),
            (86, 26.0, -34.0, 16.5, -32.0),
        ],
    },
    {
        'output_action': 'TD_SwordCombo03',
        'source_action': 'TD_Ref_Attack_PrimaryA',
        'branch_frame': 38,
        'time_keys': [(1, 18.0), (15, 21.8), (21, 25.5), (27, 27.5), (38, 28.6), (60, 62.0), (78, 86.0), (92, 102.0)],
        'travel_cm': 38.0,
        'root_travel_keys': [(1, 0.0), (7, 0.0), (12, 0.14), (17, 0.44), (22, 0.78), (27, 0.95), (33, 1.0), (92, 1.0)],
        'lead_foot': 'l',
        'foot_plans': {
            'l': [(8, 21, 1.0, 9.0)],
            'r': [(25, 35, 0.55, 6.5), (56, 68, 0.45, 6.5)],
        },
        'lead_pitch_keys': [(6, 0.0), (9, 13.0), (13, 5.0), (17, 0.0), (20, -10.0), (22, 0.0)],
        'start_blend_frames': 15,
        'end_blend_frames': 16,
        'crouch_scale': 0.75,
        'arm_blends': [
            {
                'reference_source_frames': [(10, 22.7), (18, 23.0), (21, 25.2)],
                'space': 'armature',
                'local_bones': {'clavicle_r': 0.5},
                'weight_keys': [(4, 0.0), (6, 0.0), (13, 1.0), (17, 1.0), (20, 0.0), (21, 0.0)],
            },
        ],
        'tip_clearance_cm': 22.0,
        'wrist_hard_limits': {'flexion': [-78.0, 60.0], 'deviation': [-40.0, 28.0]},
        'wrist_protect_window': [17, 19, 29, 33],
        'shield_keys': [
            (1, 30.0, -30.0, 18.0, -25.0),
            (14, 36.0, -24.0, 22.0, -18.0),
            (25, -14.0, -46.0, 28.0, -74.0),
            (38, -14.0, -46.0, 26.0, -72.0),
            (68, 24.0, -34.0, 17.0, -32.0),
            (92, 26.0, -34.0, 16.5, -32.0),
        ],
    },
]

SPIN_SPEC = {
    'output_action': 'TD_SwordCombo04',
    'source_action': 'TD_Ref_Attack_PrimaryB',
    'time_keys': [
        (1, 21.5), (14, 22.0), (24, 24.8), (35, 27.2), (45, 28.0),
        (56, 28.2), (68, 28.2),
    ],
    'travel_cm': 54.0,
    'root_travel_keys': [(1, 0.0), (11, 0.0), (18, 0.14), (27, 0.48), (37, 0.82), (45, 0.96), (52, 1.0), (68, 1.0)],
    'extra_spin_yaw_keys': [
        (1, 0.0), (8, -12.0), (14, -18.0),
        (21, 55.0), (29, 175.0), (37, 290.0), (45, 338.0),
        (54, 355.0), (63, 360.0), (68, 360.0),
    ],
    'extra_chest_yaw_keys': [
        (1, 0.0), (14, -14.0), (23, 10.0), (35, 16.0), (46, 8.0), (58, 1.5), (65, 0.0), (68, 0.0),
    ],
    'start_blend_frames': 14,
    'end_blend_frames': 30,
    'end_blend_hold_frames': 3,
    'crouch_scale': 0.65,
    'plane_level': {'window': [1, 10, 36, 52], 'wrist_z_cm': 104.0, 'tip_z_cm': 106.0, 'strength': 0.96},
    'tip_clearance_cm': 24.0,
    'wrist_hard_limits': {'flexion': [-78.0, 65.0], 'deviation': [-38.0, 30.0]},
    'wrist_protect_window': [15, 17, 36, 44],
}


def build_all():
    stage_outputs = []
    current_start_pose = {k: v.copy() for k, v in READY_POSE.items()}

    for spec in STAGE_SPECS:
        results, root_travel, diagnostics = build_standard_stage(spec, current_start_pose)
        action = write_action(spec['output_action'], results, root_travel)
        branch_idx = spec['branch_frame'] - 1
        branch_travel = root_travel[branch_idx]
        current_start_pose = shift_pose(results[branch_idx], -FORWARD * branch_travel)
        stage_outputs.append({
            'name': action.name,
            'frames': len(results),
            'branch_frame': spec['branch_frame'],
            'travel_cm': spec['travel_cm'],
            'branch_travel_cm': round(branch_travel, 2),
            'max_arm_over_cm': max(d['arm_over_cm'] for d in diagnostics),
            'max_leg_over_cm': max(max(d['leg_over_cm'].values()) for d in diagnostics),
            'results': results,
            'root_travel': root_travel,
        })

    spin_results, spin_travel, spin_diag = build_spin_stage(SPIN_SPEC, current_start_pose)
    spin_action = write_action(SPIN_SPEC['output_action'], spin_results, spin_travel)
    stage_outputs.append({
        'name': spin_action.name,
        'frames': len(spin_results),
        'branch_frame': len(spin_results),
        'travel_cm': SPIN_SPEC['travel_cm'],
        'branch_travel_cm': SPIN_SPEC['travel_cm'],
        'max_arm_over_cm': max(d['arm_over_cm'] for d in spin_diag),
        'max_leg_over_cm': max(max(d['leg_over_cm'].values()) for d in spin_diag),
        'results': spin_results,
        'root_travel': spin_travel,
    })

    chained_results = []
    chained_travel = []
    accumulated_travel = 0.0
    for idx, st in enumerate(stage_outputs):
        end_f = st['branch_frame'] if idx < len(stage_outputs) - 1 else st['frames']
        start_f = 0 if idx == 0 else 1
        for f_i in range(start_f, end_f):
            chained_results.append(shift_pose(st['results'][f_i], FORWARD * accumulated_travel))
            chained_travel.append(accumulated_travel + st['root_travel'][f_i])
        accumulated_travel += st['root_travel'][end_f - 1]

    all_action = write_action('TD_SwordCombo_All', chained_results, chained_travel)
    summary = {
        'stages': [{k: v for k, v in st.items() if k not in ('results', 'root_travel')} for st in stage_outputs],
        'chained_action': all_action.name,
        'chained_frames': len(chained_results),
        'chained_total_travel_cm': round(accumulated_travel, 2),
    }
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'combo-author-result.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    (WORK / 'author-result.json').write_text(json.dumps({'summary': summary}, indent=2), encoding='utf-8')
    if SAVE:
        bpy.ops.wm.save_as_mainfile(filepath=str(WORK / 'work.blend'))
    print(json.dumps(summary))


build_all()
