"""Blender 안에서 실행: 참고 Action(기본 TD_Ref_Attack_PrimaryA) 위에 시간 재매핑, 팔 사슬 블렌드(예비 코킹·복귀), 루트 모션 전진·앞발 스텝·뒷발 끌기와 복귀 스텝(다리 2본 IK, 뒤꿈치 들림·발가락 평탄), 손목 한계 초과분 제거(휩 보호 구간), 칼끝 지면 간격을 얹어 새 Action을 굽는다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_author.py [TD_CONFIG="{'travel_cm': 40}"] [TD_SAVE=True]
출력: Action TD_SwordAttack01(work.blend 저장은 TD_SAVE), Saved/BlenderAnimation/SwordAttack01/author-result.json(설정·프레임별 손목·골반 낮춤·IK 과신전)
상태: 현행 (2026-09-25, UE5 Manny·해부학적 칼 쥐기(sword_attack01_scene.py) 전용, 캐릭터 전방 = 아마추어 -Y, 60fps)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path('C:/Project/TDGame')
WORK = ROOT / 'Saved/BlenderAnimation/SwordAttack01'
CONFIG = {
    'source_action': 'TD_Ref_Attack_PrimaryA',
    'output_action': 'TD_SwordAttack01',
    'time_keys': [(1, 1), (7, 7), (10, 9), (18, 19), (22, 23), (25, 25.2), (34, 33), (52, 51), (96, 79), (112, 102)],
    'travel_cm': 36.0,
    'root_travel_keys': [(1, 0.0), (7, 0.0), (12, 0.12), (16, 0.36), (20, 0.6), (24, 0.8), (28, 0.93), (32, 1.0), (36, 1.0)],
    'lead_foot': 'l',
    'foot_plans': {'l': [(7, 20, 1.0, 6.0)], 'r': [(26, 36, 0.5, 2.5), (67, 81, 0.5, 4.0)]},
    'lift_peak_fraction': 0.42,
    'lead_pitch_keys': [(3, 0.0), (7, 14.0), (10, 6.0), (13, 0.0), (17, -9.0), (20, -12.0), (23, 0.0)],
    'lead_flat_window': [1, 7, 21, 25],
    'rear_heel_max_deg': 40.0,
    'rear_foot_max_pitch_deg': 68.0,
    'toe_flat_heights_cm': [1.5, 4.0],
    'tip_height_bands': [],
    'arm_blends': [{'reference_source_frames': [(12, 22.7), (22, 23.0), (25, 25.2)], 'space': 'armature', 'local_bones': {'clavicle_r': 0.5},
                    'weight_keys': [(4, 0.0), (5, 0.0), (13, 1.0), (21, 1.0), (24, 0.0), (25, 0.0)]},
                   {'reference_source_frames': [(1, 102.0)], 'space': 'local', 'local_bones': {},
                    'weight_keys': [(45, 0.0), (51, 0.0), (63, 0.4), (93, 0.4), (105, 0.0), (106, 0.0)]}],
    'rear_ball_pin_release': [26, 33],
    'tip_clearance_cm': 22.0,
    'wrist_hard_limits': {'flexion': [-78.0, 60.0], 'deviation': [-40.0, 28.0]},
    'wrist_protect_window': [20, 23, 35, 39],
    'wrist_knee_deg': 10.0,
    'wrist_curve_sigma': 1.5,
    'iterations': 2,
    'crouch_scale': 0.75,
}
CONFIG.update(globals().get('TD_CONFIG', {}))
SAVE = bool(globals().get('TD_SAVE', False))
FORWARD = Vector((0.0, -1.0, 0.0))
UP = Vector((0.0, 0.0, 1.0))
LEG_BONES = {side: (f'thigh_{side}', f'calf_{side}', f'foot_{side}', f'ball_{side}') for side in ('l', 'r')}
ARM_CHAIN = ('clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r')
TWIST_BONES = ('lowerarm_twist_01_r', 'lowerarm_twist_02_r')

scene = bpy.context.scene
armature = bpy.data.objects['root']
bones = armature.pose.bones
rest = {bone.name: bone.matrix_local.copy() for bone in armature.data.bones}
parents = {bone.name: (bone.parent.name if bone.parent else None) for bone in armature.data.bones}
order = [bone.name for bone in armature.pose.bones]
socket_object = bpy.data.objects['TD_HandGrip_R']
tip_object = bpy.data.objects['TD_SwordTip']
hand_length = armature.data.bones['hand_r'].length
SOCKET_LOCAL = Matrix.Translation((0.0, hand_length, 0.0)) @ socket_object.matrix_basis
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
    return t * t * (3 - 2 * t)


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


def sample_source(action_name, times):
    armature.animation_data.action = bpy.data.actions[action_name]
    frames = []
    for time in times:
        scene.frame_set(int(math.floor(time)), subframe=time - math.floor(time))
        frames.append({bone.name: bone.matrix.copy() for bone in bones})
    return frames


def solve_two_bone(root, target, length_a, length_b, pole):
    delta = target - root
    distance = delta.length
    reach = (length_a + length_b) * 0.998
    overextension = max(0.0, distance - reach)
    distance = min(max(distance, abs(length_a - length_b) + 0.01), reach)
    axis = delta.normalized()
    target = root + axis * distance
    along = (length_a ** 2 - length_b ** 2 + distance ** 2) / (2 * distance)
    height = math.sqrt(max(0.0, length_a ** 2 - along ** 2))
    perpendicular = (pole - axis * pole.dot(axis)).normalized()
    return root + axis * along + perpendicular * height, target, overextension


def wrist_angles(hand_matrix, forearm):
    rotation = hand_matrix.to_3x3()
    hand_direction = (rotation @ HAND_MIDDLE).normalized()
    radial = (rotation @ HAND_RADIAL).normalized()
    palm = forearm.cross(radial).normalized()
    radial = palm.cross(forearm).normalized()
    along = hand_direction.dot(forearm)
    return math.degrees(math.atan2(hand_direction.dot(palm), along)), math.degrees(math.atan2(hand_direction.dot(radial), along))


HAND_MIDDLE = child_offset('hand_r', 'middle_01_r')
HAND_RADIAL = child_offset('hand_r', 'index_01_r') - child_offset('hand_r', 'pinky_01_r')
FOREARM_LOCAL = child_offset('lowerarm_r', 'hand_r').normalized()
REST_HAND_IN_LOWERARM = rest['lowerarm_r'].to_3x3().inverted() @ rest['hand_r'].to_3x3()
FOREARM_IN_HAND = (REST_HAND_IN_LOWERARM.transposed() @ FOREARM_LOCAL).normalized()
RADIAL_IN_HAND = (HAND_RADIAL - FOREARM_IN_HAND * HAND_RADIAL.dot(FOREARM_IN_HAND)).normalized()
LENGTHS = {
    'upperarm_r': child_offset('upperarm_r', 'lowerarm_r').length,
    'lowerarm_r': child_offset('lowerarm_r', 'hand_r').length,
}
for side, (thigh, calf, foot, ball) in LEG_BONES.items():
    LENGTHS[thigh] = child_offset(thigh, calf).length
    LENGTHS[calf] = child_offset(calf, foot).length


def arm_geometry(pose, wrist):
    shoulder = pose['upperarm_r'].translation
    length_a, length_b = LENGTHS['upperarm_r'], LENGTHS['lowerarm_r']
    axis = wrist - shoulder
    distance = axis.length
    reach = (length_a + length_b) * 0.998
    overextension = max(0.0, distance - reach)
    distance = min(distance, reach)
    axis.normalize()
    reference = UP if abs(axis.dot(UP)) < 0.95 else FORWARD
    u = (reference - axis * reference.dot(axis)).normalized()
    v = axis.cross(u)
    along = (length_a ** 2 - length_b ** 2 + distance ** 2) / (2 * distance)
    radius = math.sqrt(max(0.0, length_a ** 2 - along ** 2))
    return {'shoulder': shoulder, 'wrist': shoulder + axis * distance, 'axis': axis, 'center': shoulder + axis * along, 'radius': radius, 'u': u, 'v': v, 'over': overextension}


def perpendicular_direction(geometry, direction):
    axis = geometry['axis']
    result = direction - axis * direction.dot(axis)
    if result.length < 1e-6:
        return geometry['u'].copy()
    return result.normalized()


def arm_bones(geometry, source_pose, direction):
    shoulder, wrist, axis = geometry['shoulder'], geometry['wrist'], geometry['axis']
    elbow = geometry['center'] + direction * geometry['radius']
    source_shoulder = source_pose['upperarm_r'].translation
    source_elbow = source_pose['lowerarm_r'].translation
    source_wrist = source_pose['hand_r'].translation
    source_axis = (source_wrist - source_shoulder).normalized()
    source_direction = source_elbow - source_shoulder
    source_direction = (source_direction - source_axis * source_direction.dot(source_axis)).normalized()
    source_normal = source_direction.cross(source_axis)
    normal = direction.cross(axis)
    upper = align_bone(source_pose['upperarm_r'], source_elbow - source_shoulder, source_normal, shoulder, elbow - shoulder, normal)
    lower = align_bone(source_pose['lowerarm_r'], source_wrist - source_elbow, source_normal, elbow, wrist - elbow, normal)
    return elbow, upper, lower


def forearm_twist(lower_matrix, hand_matrix):
    relative = lower_matrix.to_3x3().inverted() @ hand_matrix.to_3x3()
    delta = (relative @ REST_HAND_IN_LOWERARM.inverted()).to_quaternion()
    projection = Vector((delta.x, delta.y, delta.z)).dot(FOREARM_LOCAL)
    return 2.0 * math.atan2(projection, delta.w)


def twist_axis_in(name):
    return (rest[name].to_3x3().inverted() @ rest['lowerarm_r'].to_3x3() @ FOREARM_LOCAL).normalized()


def twist_fractions():
    elbow = rest['lowerarm_r'].translation
    wrist = rest['hand_r'].translation
    span = (wrist - elbow).length
    return {name: max(0.0, min(1.0, (rest[name].translation - elbow).dot((wrist - elbow).normalized()) / span)) for name in TWIST_BONES}


def blend_matrix(matrix_a, matrix_b, weight):
    if weight <= 0.0:
        return matrix_a
    rotation = matrix_a.to_quaternion().slerp(matrix_b.to_quaternion(), weight)
    result = rotation.to_matrix().to_4x4()
    result.translation = matrix_a.translation.lerp(matrix_b.translation, weight)
    return result


def solve_leg(side, pose, source_pose, ankle_target, foot_rotation, ball_world_rotation):
    thigh, calf, foot, ball = LEG_BONES[side]
    hip = pose[thigh].translation
    source_hip, source_knee, source_ankle = (source_pose[name].translation for name in (thigh, calf, foot))
    source_pole = source_knee - (source_hip + (source_ankle - source_hip).normalized() * (source_knee - source_hip).dot((source_ankle - source_hip).normalized()))
    knee, ankle, overextension = solve_two_bone(hip, ankle_target, LENGTHS[thigh], LENGTHS[calf], source_pole)
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


def hand_from_parameters(lower_matrix, twist_angle, flexion_component, deviation_component):
    neutral = (lower_matrix.to_3x3() @ REST_HAND_IN_LOWERARM).to_quaternion()
    twist = Quaternion(FOREARM_IN_HAND, twist_angle)
    radial = twist @ RADIAL_IN_HAND
    palm = FOREARM_IN_HAND.cross(radial)
    swing_vector = radial * flexion_component + palm * deviation_component
    swing = Quaternion(swing_vector.normalized(), swing_vector.length) if swing_vector.length > 1e-9 else Quaternion()
    return neutral @ swing @ twist


def hand_parameters(lower_matrix, hand_rotation):
    neutral = (lower_matrix.to_3x3() @ REST_HAND_IN_LOWERARM).to_quaternion()
    relative = neutral.inverted() @ hand_rotation
    direction = relative @ FOREARM_IN_HAND
    swing = FOREARM_IN_HAND.rotation_difference(direction)
    twist = swing.inverted() @ relative
    twist_angle = 2.0 * math.atan2(Vector((twist.x, twist.y, twist.z)).dot(FOREARM_IN_HAND), twist.w)
    radial = Quaternion(FOREARM_IN_HAND, twist_angle) @ RADIAL_IN_HAND
    palm = FOREARM_IN_HAND.cross(radial)
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
    rotation = hand_from_parameters(lower_matrix, *parameters)
    hand = rotation.to_matrix().to_4x4()
    hand.translation = wrist
    grip = hand @ SOCKET_LOCAL
    return rotation, grip, grip @ TIP_LOCAL


def window_bump(window, frame):
    begin, full_begin, full_end, end = window
    return smoothstep(begin, full_begin, frame) * (1.0 - smoothstep(full_end, end, frame))


def clearance_offsets(lowers, wrists, curves, minimum_heights, maximum_heights):
    count = len(lowers)
    limits = CONFIG['wrist_hard_limits']
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
                cost = (flexion - parameters[1]) ** 2 + (deviation - parameters[2]) ** 2 + (0.0 if shortfall <= 0.0 else 1e3 + shortfall * 10)
                if best is None or cost < best[0]:
                    best = (cost, flexion - parameters[1], deviation - parameters[2])
        flexion_offsets.append(best[1])
        deviation_offsets.append(best[2])

    def spread(values):
        magnitude = max_filter([abs(v) for v in values], 4)
        signs = gaussian_smooth(values, 3.0)
        return gaussian_smooth([math.copysign(m, s) if abs(s) > 1e-9 else 0.0 for m, s in zip(magnitude, signs)], 2.5)

    return spread(flexion_offsets), spread(deviation_offsets)


def solve_right_arm(rigid_poses, source, root_travel):
    count = len(source)
    wrists = [rigid['hand_r'].translation.copy() for rigid in rigid_poses]
    lowers, elbows, geometries, directions = [], [], [], []
    for index in range(count):
        geometry = arm_geometry(rigid_poses[index], wrists[index])
        direction = perpendicular_direction(geometry, source[index]['lowerarm_r'].translation - source[index]['upperarm_r'].translation)
        elbow, _, lower = arm_bones(geometry, source[index], direction)
        geometries.append(geometry)
        directions.append(direction)
        elbows.append(elbow)
        lowers.append(lower)
    raw = [hand_parameters(source[i]['lowerarm_r'], source[i]['hand_r'].to_quaternion()) for i in range(count)]
    limits = CONFIG['wrist_hard_limits']
    knee = math.radians(CONFIG['wrist_knee_deg'])
    flexion_low, flexion_high = -math.radians(limits['flexion'][1]), -math.radians(limits['flexion'][0])
    deviation_low, deviation_high = math.radians(limits['deviation'][0]), math.radians(limits['deviation'][1])
    twist = unwrap([p[0] for p in raw])
    sigma = CONFIG['wrist_curve_sigma']
    strength = [1.0 - window_bump(CONFIG['wrist_protect_window'], index + 1) for index in range(count)]
    flexion_correction = gaussian_smooth([(soft_clamp(p[1], flexion_low, flexion_high, knee) - p[1]) * strength[i] for i, p in enumerate(raw)], sigma)
    deviation_correction = gaussian_smooth([(soft_clamp(p[2], deviation_low, deviation_high, knee) - p[2]) * strength[i] for i, p in enumerate(raw)], sigma)
    flexion = [p[1] + c for p, c in zip(raw, flexion_correction)]
    deviation = [p[2] + c for p, c in zip(raw, deviation_correction)]
    curves = [twist, flexion, deviation]
    history = []
    minimum_heights = [CONFIG['tip_clearance_cm']] * count
    maximum_heights = []
    for index in range(count):
        ceiling = 1e6
        for band in CONFIG['tip_height_bands']:
            weight = window_bump(band['window'], index + 1)
            if weight > 0.0:
                current = grip_and_tip(lowers[index], wrists[index], (curves[0][index], curves[1][index], curves[2][index]))[2].z
                ceiling = min(ceiling, current + (band['max_cm'] - current) * weight if current > band['max_cm'] else current)
        maximum_heights.append(max(ceiling, CONFIG['tip_clearance_cm'] + 5.0))
    for iteration in range(CONFIG['iterations']):
        flexion_offsets, deviation_offsets = clearance_offsets(lowers, wrists, curves, minimum_heights, maximum_heights)
        curves[1] = [value if abs(offset) < 1e-9 else min(max(value + offset, flexion_low), flexion_high) for value, offset in zip(curves[1], flexion_offsets)]
        curves[2] = [value if abs(offset) < 1e-9 else min(max(value + offset, deviation_low), deviation_high) for value, offset in zip(curves[2], deviation_offsets)]
        history.append({'iteration': iteration,
                        'max_clearance_flexion_deg': round(max(abs(math.degrees(o)) for o in flexion_offsets), 1),
                        'max_clearance_deviation_deg': round(max(abs(math.degrees(o)) for o in deviation_offsets), 1)})
    arms = []
    for index in range(count):
        geometry = geometries[index]
        elbow, upper, lower = arm_bones(geometry, source[index], directions[index])
        rotation = hand_from_parameters(lower, curves[0][index], curves[1][index], curves[2][index])
        hand = rotation.to_matrix().to_4x4()
        hand.translation = geometry['wrist']
        flexion_value, deviation_value = wrist_angles(hand, (geometry['wrist'] - elbow).normalized())
        arms.append(({'upperarm_r': upper, 'lowerarm_r': lower, 'hand_r': hand}, flexion_value, deviation_value, geometry['over'], elbow))
    return arms, history


def blend_arm_chain(pose, reference, weight, space, local_bones):
    overrides = {}
    for name in ARM_CHAIN:
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


def apply_arm_blends(source):
    blends = CONFIG['arm_blends']
    if not blends:
        return source
    active = [[index for index in range(len(source)) if pchip(blend['weight_keys'], index + 1) > 1e-4] for blend in blends]
    references = [dict(zip(frames, sample_source(CONFIG['source_action'], [pchip(blend['reference_source_frames'], index + 1) for index in frames])))
                  for blend, frames in zip(blends, active)]
    result = []
    for index, pose in enumerate(source):
        for blend, reference in zip(blends, references):
            if index in reference:
                pose = blend_arm_chain(pose, reference[index], pchip(blend['weight_keys'], index + 1), blend['space'], blend['local_bones'])
        result.append(pose)
    return result


def rear_heel_lift_for_reach(pelvis_pose, side, foot_target):
    thigh, calf, foot, ball = LEG_BONES[side]
    hip = pelvis_pose[thigh].translation
    reach = (LENGTHS[thigh] + LENGTHS[calf]) * 0.985
    foot_matrix, ball_matrix = foot_target
    if (hip - foot_matrix.translation).length <= reach:
        return 0.0
    ankle_over_ball = foot_matrix.translation - ball_matrix.translation
    current_pitch = math.degrees(math.atan2(ankle_over_ball.z, Vector((ankle_over_ball.x, ankle_over_ball.y, 0.0)).length))
    allowed = min(CONFIG['rear_heel_max_deg'], max(0.0, CONFIG['rear_foot_max_pitch_deg'] - current_pitch))
    for step in range(1, int(allowed) + 1):
        lifted, _ = foot_pivot_pose(foot_matrix, ball_matrix, ball_matrix.translation, float(step))
        if (hip - lifted.translation).length <= reach:
            return float(step)
    return float(int(allowed))


def flatten_toes(foot_matrix, ball_matrix, reference_foot, reference_ball):
    low, high = CONFIG['toe_flat_heights_cm']
    weight = 1.0 - smoothstep(low, high, ball_matrix.translation.z)
    if weight <= 0.0:
        return ball_matrix
    forward_now = foot_matrix.to_3x3() @ (reference_foot.to_3x3().inverted() @ FORWARD)
    yaw = math.atan2(forward_now.x, -forward_now.y) - math.atan2(FORWARD.x, -FORWARD.y)
    flat = Quaternion(UP, yaw) @ reference_ball.to_quaternion()
    rotation = ball_matrix.to_quaternion().slerp(flat, weight)
    result = rotation.to_matrix().to_4x4()
    result.translation = ball_matrix.translation
    return result


def pelvis_drop_for_reach(pelvis_pose, source_pose, ankle_targets):
    drop = 0.0
    for side, (thigh, calf, foot, ball) in LEG_BONES.items():
        hip = pelvis_pose[thigh].translation
        reach = (LENGTHS[thigh] + LENGTHS[calf]) * 0.99
        delta = hip - ankle_targets[side]
        horizontal = Vector((delta.x, delta.y, 0.0)).length
        if horizontal >= reach:
            drop = max(drop, delta.z)
            continue
        allowed_height = math.sqrt(reach ** 2 - horizontal ** 2)
        drop = max(drop, delta.z - allowed_height)
    return max(0.0, drop)


def build():
    source_action = bpy.data.actions[CONFIG['source_action']]
    first, last = [int(round(value)) for value in source_action.frame_range]
    time_keys = CONFIG['time_keys']
    frame_count = (last - first + 1) if not time_keys else int(time_keys[-1][0])
    times = [pchip(time_keys, frame) if time_keys else float(first + frame - 1) for frame in range(1, frame_count + 1)]
    source = apply_arm_blends(sample_source(CONFIG['source_action'], times))
    travel = CONFIG['travel_cm']
    root_travel = [travel * pchip(CONFIG['root_travel_keys'], frame) for frame in range(1, frame_count + 1)]
    pelvis_travel = list(root_travel)
    fractions = twist_fractions()
    lead = CONFIG['lead_foot']
    rear = 'r' if lead == 'l' else 'l'
    peak_power = math.log(0.5) / math.log(CONFIG['lift_peak_fraction'])

    def foot_world_offset(side, frame):
        progress = 0.0
        lift = 0.0
        for begin, end, share, height in CONFIG['foot_plans'][side]:
            fraction = min(max((frame - begin) / (end - begin), 0.0), 1.0)
            progress += share * fraction ** 3 * (fraction * (fraction * 6 - 15) + 10)
            if 0.0 < fraction < 1.0:
                lift += height * math.sin(math.pi * fraction ** peak_power)
        return FORWARD * (travel * progress) + UP * lift

    def lead_pitch(frame):
        return pchip(CONFIG['lead_pitch_keys'], frame)

    flat_in_start, flat_in_end, flat_out_start, flat_out_end = CONFIG['lead_flat_window']

    lead_bones = LEG_BONES[lead]
    lead_base = (source[0][lead_bones[2]], source[0][lead_bones[3]])
    floor = {name: rest[name].translation.z - (0.4 if name.startswith('ball') else 0.6) for side in LEG_BONES for name in LEG_BONES[side][2:]}
    foot_targets = []
    for index, pose in enumerate(source):
        frame = index + 1
        targets = {}
        for side, (thigh, calf, foot, ball) in LEG_BONES.items():
            offset = foot_world_offset(side, frame) - FORWARD * root_travel[index]
            foot_source, ball_source = pose[foot], pose[ball]
            pin_start, pin_end = CONFIG['rear_ball_pin_release']
            if side != lead and frame < pin_end:
                drift = (source[0][ball].translation - ball_source.translation) * (1.0 - smoothstep(pin_start, pin_end, frame))
                drift.z = 0.0
                foot_source = with_translation(foot_source, foot_source.translation + drift)
                ball_source = with_translation(ball_source, ball_source.translation + drift)
            if side == lead:
                weight = smoothstep(flat_in_start, flat_in_end, frame) * (1.0 - smoothstep(flat_out_start, flat_out_end, frame))
                foot_source = blend_matrix(foot_source, lead_base[0], weight)
                ball_source = blend_matrix(ball_source, lead_base[1], weight)
            lift = UP * offset.z
            offset = offset - lift
            foot_matrix = with_translation(foot_source, foot_source.translation + offset)
            ball_matrix = with_translation(ball_source, ball_source.translation + offset)
            if side == lead:
                pitch = lead_pitch(frame)
                if abs(pitch) > 1e-4:
                    heel = foot_matrix.translation - FORWARD * 5.0
                    heel.z = 0.0
                    pivot = ball_matrix.translation if pitch > 0 else heel
                    foot_matrix, ball_matrix = foot_pivot_pose(foot_matrix, ball_matrix, pivot, pitch)
            raise_by = max(0.0, floor[foot] - foot_matrix.translation.z, floor[ball] - ball_matrix.translation.z)
            foot_matrix = with_translation(foot_matrix, foot_matrix.translation + UP * raise_by + lift)
            ball_matrix = with_translation(ball_matrix, ball_matrix.translation + UP * raise_by + lift)
            targets[side] = (foot_matrix, ball_matrix)
        foot_targets.append(targets)

    idle_pelvis_height = source[0]['pelvis'].translation.z
    crouch = CONFIG['crouch_scale']
    pelvis_locations = []
    for index, pose in enumerate(source):
        location = pose['pelvis'].translation + FORWARD * (pelvis_travel[index] - root_travel[index])
        if location.z < idle_pelvis_height:
            location.z = idle_pelvis_height - (idle_pelvis_height - location.z) * crouch
        pelvis_locations.append(location)
    heel_lifts = []
    for index, pose in enumerate(source):
        trial = forward_kinematics(pose, {'pelvis': with_translation(pose['pelvis'], pelvis_locations[index])})
        heel_lifts.append(rear_heel_lift_for_reach(trial, rear, foot_targets[index][rear]))
    heel_lifts = gaussian_smooth(max_filter(heel_lifts, 3), 2.0)
    for index in range(frame_count):
        if heel_lifts[index] > 1e-3:
            foot_matrix, ball_matrix = foot_targets[index][rear]
            foot_targets[index][rear] = foot_pivot_pose(foot_matrix, ball_matrix, ball_matrix.translation, heel_lifts[index])
    reference_feet = {side: (source[0][LEG_BONES[side][2]], source[0][LEG_BONES[side][3]]) for side in LEG_BONES}
    for index in range(frame_count):
        if any(begin <= index + 1 <= end for begin, end, _, _ in CONFIG['foot_plans'][rear]):
            continue
        foot_matrix, ball_matrix = foot_targets[index][rear]
        foot_targets[index][rear] = (foot_matrix, flatten_toes(foot_matrix, ball_matrix, *reference_feet[rear]))
    drops = []
    for index, pose in enumerate(source):
        trial = forward_kinematics(pose, {'pelvis': with_translation(pose['pelvis'], pelvis_locations[index])})
        drops.append(pelvis_drop_for_reach(trial, pose, {side: foot_targets[index][side][0].translation for side in LEG_BONES}))
    drops = gaussian_smooth(max_filter(drops, 3), 2.0)
    rigid_poses = []
    for index, pose in enumerate(source):
        location = pelvis_locations[index] - UP * drops[index]
        rigid_poses.append(forward_kinematics(pose, {'pelvis': with_translation(pose['pelvis'], location)}))

    arms, history = solve_right_arm(rigid_poses, source, root_travel)
    results = []
    diagnostics = []
    previous_elbow = None
    previous_twist = 0.0
    for index, pose in enumerate(source):
        frame = index + 1
        arm, flexion, deviation, arm_over, elbow = arms[index]
        overrides = {'pelvis': rigid_poses[index]['pelvis']}
        overrides.update(arm)
        leg_over = {}
        for side in LEG_BONES:
            foot_matrix, ball_matrix = foot_targets[index][side]
            partial = forward_kinematics(pose, overrides)
            leg, leg_over[side] = solve_leg(side, partial, pose, foot_matrix.translation, foot_matrix.to_quaternion(), ball_matrix.to_quaternion())
            overrides.update(leg)
        full = forward_kinematics(pose, overrides)
        twist = math.remainder(forearm_twist(full['lowerarm_r'], full['hand_r']) - forearm_twist(pose['lowerarm_r'], pose['hand_r']), math.tau)
        if index > 0:
            twist = previous_twist + math.remainder(twist - previous_twist, math.tau)
        previous_twist = twist
        for name, fraction in fractions.items():
            overrides[name] = full[name] @ Quaternion(twist_axis_in(name), twist * fraction).to_matrix().to_4x4()
        for ik_name, target in (('ik_hand_gun', 'hand_r'), ('ik_hand_r', 'hand_r'), ('ik_hand_l', 'hand_l'), ('ik_foot_l', 'foot_l'), ('ik_foot_r', 'foot_r')):
            overrides[ik_name] = full[target].copy()
        full = forward_kinematics(pose, overrides)
        results.append(full)
        elbow_step = 0.0 if previous_elbow is None else (elbow - previous_elbow).length
        previous_elbow = elbow
        diagnostics.append({'frame': frame, 'wrist_flexion': round(flexion, 1), 'wrist_deviation': round(deviation, 1), 'forearm_twist_deg': round(math.degrees(twist), 1),
                            'elbow_step_cm': round(elbow_step, 2), 'arm_over_cm': round(arm_over, 2), 'leg_over_cm': {k: round(v, 2) for k, v in leg_over.items()},
                            'pelvis_drop_cm': round(drops[index], 2), 'pelvis_travel_cm': round(pelvis_travel[index], 2), 'root_travel_cm': round(root_travel[index], 2)})
    return results, root_travel, diagnostics, history


def write_action(results, root_travel):
    name = CONFIG['output_action']
    old = bpy.data.actions.get(name)
    if old is not None:
        bpy.data.actions.remove(old)
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    armature.animation_data.action = action
    armature.rotation_mode = 'QUATERNION'
    previous_rotation = {}
    for index, pose in enumerate(results):
        frame = index + 1
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


results, root_travel, diagnostics, history = build()
action = write_action(results, root_travel)
summary = {
    'action': action.name, 'frames': len(results), 'config': CONFIG,
    'max_arm_over_cm': max(d['arm_over_cm'] for d in diagnostics),
    'max_leg_over_cm': max(max(d['leg_over_cm'].values()) for d in diagnostics),
    'wrist_flexion_range': [min(d['wrist_flexion'] for d in diagnostics), max(d['wrist_flexion'] for d in diagnostics)],
    'wrist_deviation_range': [min(d['wrist_deviation'] for d in diagnostics), max(d['wrist_deviation'] for d in diagnostics)],
    'iterations': history, 'max_elbow_step_cm': max(d['elbow_step_cm'] for d in diagnostics),
    'forearm_twist_range': [min(d['forearm_twist_deg'] for d in diagnostics), max(d['forearm_twist_deg'] for d in diagnostics)],
}
WORK.mkdir(parents=True, exist_ok=True)
(WORK / 'author-result.json').write_text(json.dumps({'summary': summary, 'frames': diagnostics}, indent=1), encoding='utf-8')
if SAVE:
    bpy.ops.wm.save_as_mainfile(filepath=str(WORK / 'work.blend'))
print(json.dumps(summary))
