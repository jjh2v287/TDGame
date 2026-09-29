"""Blender 안에서 실행: 작업 씬의 검 공격 Action을 프레임마다 재서 타격 프레임·검 끝 궤적 평면·날 정렬·팔꿈치·손목·몸통 회전·골반·발 미끄러짐·루트 이동·관절 순서를 JSON으로 낸다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_measure.py TD_ACTION="'<Action>'" [TD_CONTACT_FRAME=<프레임>]
출력: Saved/BlenderAnimation/SwordAttack01/measure_<Action>.json (summary + frames), 마지막 줄 summary JSON
상태: 현행 (2026-09-25, Manny·HandGrip_R 소켓 전제: 칼날 = 소켓 -Y, 날 면 법선 = 소켓 Z)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path('C:/Project/TDGame')
WORK = ROOT / 'Saved/BlenderAnimation/SwordAttack01'
scene = bpy.context.scene
armature = bpy.data.objects['root']
if globals().get('TD_ACTION'):
    armature.animation_data.action = bpy.data.actions[TD_ACTION]
action = armature.animation_data.action
fps = scene.render.fps / scene.render.fps_base
first, last = [int(round(value)) for value in action.frame_range]
socket = bpy.data.objects['TD_HandGrip_R']
tip = bpy.data.objects['TD_SwordTip']
SEQUENCE_BONES = ['pelvis', 'spine_03', 'spine_05', 'upperarm_r', 'lowerarm_r', 'hand_r']
BODY_CAPSULES = [('thigh_l', 'calf_l', 8.0), ('calf_l', 'foot_l', 6.0), ('thigh_r', 'calf_r', 8.0), ('calf_r', 'foot_r', 6.0),
                 ('pelvis', 'spine_05', 14.0), ('neck_01', 'head', 11.0), ('upperarm_l', 'lowerarm_l', 5.0), ('lowerarm_l', 'hand_l', 4.5)]


def segment_distance(p1, q1, p2, q2):
    d1, d2, r = q1 - p1, q2 - p2, p1 - p2
    a, e, f = d1.dot(d1), d2.dot(d2), d2.dot(r)
    c, b = d1.dot(r), d1.dot(d2)
    denominator = a * e - b * b
    s = min(max((b * f - c * e) / denominator, 0.0), 1.0) if denominator > 1e-9 else 0.0
    t = (b * s + f) / e if e > 1e-9 else 0.0
    if t < 0.0:
        t, s = 0.0, min(max(-c / a, 0.0), 1.0) if a > 1e-9 else 0.0
    elif t > 1.0:
        t, s = 1.0, min(max((b - c) / a, 0.0), 1.0) if a > 1e-9 else 0.0
    return ((p1 + d1 * s) - (p2 + d2 * t)).length


def character(vector):
    return Vector((-vector.y, -vector.x, vector.z))


def world_head(name):
    return armature.matrix_world @ armature.pose.bones[name].head


def world_rotation(name):
    return (armature.matrix_world @ armature.pose.bones[name].matrix).to_quaternion()


def yaw_degrees(vector):
    return math.degrees(math.atan2(vector.y, vector.x))


def line_yaw(left_bone, right_bone):
    line = character(world_head(left_bone) - world_head(right_bone))
    forward = Vector((line.y, -line.x, 0.0))
    return yaw_degrees(forward)


def angle_between(a, b):
    if a.length < 1e-9 or b.length < 1e-9:
        return 0.0
    return math.degrees(a.angle(b))


def wrist_angles():
    forearm = (world_head('hand_r') - world_head('lowerarm_r')).normalized()
    hand = (world_head('middle_01_r') - world_head('hand_r')).normalized()
    radial = (world_head('index_01_r') - world_head('pinky_01_r')).normalized()
    palm = forearm.cross(radial).normalized()
    radial = palm.cross(forearm).normalized()
    along = hand.dot(forearm)
    flexion = math.degrees(math.atan2(hand.dot(palm), along))
    deviation = math.degrees(math.atan2(hand.dot(radial), along))
    return flexion, deviation


def sample():
    rows = []
    for frame in range(first, last + 1):
        scene.frame_set(frame)
        socket_matrix = socket.matrix_world
        blade = -(socket_matrix.to_quaternion() @ Vector((0, 1, 0)))
        flat_normal = socket_matrix.to_quaternion() @ Vector((0, 0, 1))
        flexion, deviation = wrist_angles()
        blade_start, blade_end = character(socket_matrix.translation) * 100 + character(blade) * 11.0, character(tip.matrix_world.translation) * 100
        clearances = {}
        for start_bone, end_bone, radius in BODY_CAPSULES:
            clearances[start_bone] = segment_distance(blade_start, blade_end, character(world_head(start_bone)) * 100, character(world_head(end_bone)) * 100) - radius
        rows.append({
            'body_clearance': clearances,
            'frame': frame,
            'tip': character(tip.matrix_world.translation) * 100,
            'grip': character(socket_matrix.translation) * 100,
            'blade': character(blade),
            'flat_normal': character(flat_normal),
            'root': character(armature.matrix_world.translation) * 100,
            'pelvis': character(world_head('pelvis')) * 100,
            'hand_l': character(world_head('hand_l')) * 100,
            'shoulder': character(world_head('upperarm_r')) * 100,
            'elbow': character(world_head('lowerarm_r')) * 100,
            'wrist': character(world_head('hand_r')) * 100,
            'ball_l': character(world_head('ball_l')) * 100,
            'ball_r': character(world_head('ball_r')) * 100,
            'foot_l': character(world_head('foot_l')) * 100,
            'foot_r': character(world_head('foot_r')) * 100,
            'hip_yaw': line_yaw('thigh_l', 'thigh_r'),
            'shoulder_yaw': line_yaw('upperarm_l', 'upperarm_r'),
            'wrist_flexion': flexion,
            'wrist_deviation': deviation,
            'rotations': {name: world_rotation(name) for name in SEQUENCE_BONES},
            'all_rotations': [bone.matrix.to_quaternion() for bone in armature.pose.bones],
        })
    return rows


def unwrap(values):
    result = [values[0]]
    for value in values[1:]:
        delta = (value - result[-1] + 180) % 360 - 180
        result.append(result[-1] + delta)
    return result


def plane_fit(points):
    center = sum(points, Vector()) / len(points)
    best = None
    for index in range(-60, 61):
        for tilt_index in range(0, 91):
            azimuth = math.radians(index * 3)
            tilt = math.radians(tilt_index)
            normal = Vector((math.sin(tilt) * math.cos(azimuth), math.sin(tilt) * math.sin(azimuth), math.cos(tilt)))
            residual = math.sqrt(sum(((point - center).dot(normal)) ** 2 for point in points) / len(points))
            if best is None or residual < best[0]:
                best = (residual, normal)
    residual, normal = best
    return residual, 90.0 - math.degrees(math.acos(min(1.0, abs(normal.z))))


def contact_frames(rows, bone):
    grounded = []
    for index, row in enumerate(rows):
        speed = 0.0 if index == 0 else (row[bone] - rows[index - 1][bone]).length
        grounded.append(row[bone].z < 8.0 and speed < 1.5)
    return grounded


def summarize(rows):
    speeds = [0.0] + [(rows[i]['tip'] - rows[i - 1]['tip']).length * fps for i in range(1, len(rows))]
    peak = max(range(len(rows)), key=lambda i: speeds[i])
    fast = [i for i in range(len(rows)) if speeds[i] >= 0.5 * speeds[peak]]
    swing_start = peak
    while swing_start > 0 and speeds[swing_start - 1] >= 0.5 * speeds[peak]:
        swing_start -= 1
    swing_end = peak
    while swing_end < len(rows) - 1 and speeds[swing_end + 1] >= 0.5 * speeds[peak]:
        swing_end += 1
    contact = int(globals().get('TD_CONTACT_FRAME', 0)) - first if globals().get('TD_CONTACT_FRAME') else None
    if contact is None:
        crossings = [i for i in range(max(1, swing_start - 3), min(len(rows), swing_end + 4)) if (rows[i - 1]['tip'] - rows[i - 1]['root']).y > 0 >= (rows[i]['tip'] - rows[i]['root']).y]
        contact = crossings[0] if crossings else peak
    active = list(range(max(0, swing_start - 1), min(len(rows), swing_end + 2)))
    active_tips = [rows[i]['tip'] - rows[i]['root'] for i in active]
    residual, tilt = plane_fit(active_tips)
    edge_angles = []
    for i in active[1:]:
        velocity = rows[i]['tip'] - rows[i - 1]['tip']
        edge_angles.append(angle_between(velocity, rows[i]['flat_normal']))
    rolls = []
    for i in active[1:]:
        rolls.append(angle_between(rows[i]['flat_normal'], rows[active[0]]['flat_normal']))
    contact_row = rows[contact]
    elbow = angle_between(contact_row['shoulder'] - contact_row['elbow'], contact_row['wrist'] - contact_row['elbow'])
    arm_blade = angle_between(contact_row['elbow'] - contact_row['wrist'], contact_row['blade'])
    shoulder_yaw = unwrap([row['shoulder_yaw'] for row in rows])
    hip_yaw = unwrap([row['hip_yaw'] for row in rows])
    tip_azimuth = unwrap([yaw_degrees(row['tip'] - row['root']) for row in rows])
    horizontal = lambda v: Vector((v.x, v.y, 0.0))
    peaks = {}
    window = range(max(1, swing_start - 12), min(len(rows), swing_end + 4))
    for name in SEQUENCE_BONES:
        angular = {i: math.degrees(rows[i]['rotations'][name].rotation_difference(rows[i - 1]['rotations'][name]).angle) * fps for i in window}
        peaks[name] = max(angular, key=angular.get) + first
    max_step_rotation = 0.0
    for i in range(1, len(rows)):
        for current, previous in zip(rows[i]['all_rotations'], rows[i - 1]['all_rotations']):
            angle = math.degrees(current.rotation_difference(previous).angle)
            max_step_rotation = max(max_step_rotation, min(angle, 360.0 - angle))
    slides = {}
    for bone in ('ball_l', 'ball_r'):
        grounded = contact_frames(rows, bone)
        worst = 0.0
        for i in range(1, len(rows)):
            if grounded[i] and grounded[i - 1]:
                worst = max(worst, (horizontal(rows[i][bone]) - horizontal(rows[i - 1][bone])).length)
        slides[bone] = {'max_slide_cm_per_frame_grounded': round(worst, 3), 'grounded_frames': sum(grounded), 'min_z_cm': round(min(row[bone].z for row in rows), 2),
                        'travel_cm': round((horizontal(rows[-1][bone]) - horizontal(rows[0][bone])).length, 1)}
    root_steps = [(horizontal(rows[i]['root']) - horizontal(rows[i - 1]['root'])).length for i in range(1, len(rows))]
    pelvis_root = max((horizontal(row['pelvis']) - horizontal(row['root'])).length for row in rows)
    to_time = lambda index: round(index / fps, 3)
    return {
        'action': action.name, 'fps': fps, 'frames': [first, last], 'duration_s': to_time(last - first),
        'tip_peak_speed_m_s': round(speeds[peak] / 100, 2), 'tip_peak_frame': peak + first,
        'swing_frames_50pct': [swing_start + first, swing_end + first], 'swing_duration_s': to_time(swing_end - swing_start + 1),
        'contact_frame': contact + first, 'contact_time_s': to_time(contact),
        'contact_tip_cm': [round(v, 1) for v in contact_row['tip'] - contact_row['root']],
        'contact_horizontal_radius_cm': round(horizontal(contact_row['tip'] - contact_row['pelvis']).length, 1),
        'active_plane_rms_cm': round(residual, 2), 'active_plane_tilt_deg': round(tilt, 1),
        'active_direction': 'R->L' if (active_tips[-1].y - active_tips[0].y) < 0 else 'L->R',
        'active_drop_cm': round(active_tips[0].z - active_tips[-1].z, 1),
        'edge_alignment_min_deg': round(min(edge_angles), 1), 'edge_alignment_mean_deg': round(sum(edge_angles) / len(edge_angles), 1),
        'blade_roll_active_max_deg': round(max(rolls), 1),
        'elbow_at_contact_deg': round(elbow, 1), 'arm_blade_at_contact_deg': round(arm_blade, 1),
        'shoulder_yaw_range_deg': round(max(shoulder_yaw) - min(shoulder_yaw), 1), 'hip_yaw_range_deg': round(max(hip_yaw) - min(hip_yaw), 1),
        'tip_horizontal_sweep_active_deg': round(abs(tip_azimuth[active[-1]] - tip_azimuth[active[0]]), 1),
        'pelvis_z_range_cm': round(max(r['pelvis'].z for r in rows) - min(r['pelvis'].z for r in rows), 1),
        'pelvis_z_min_frame': min(range(len(rows)), key=lambda i: rows[i]['pelvis'].z) + first,
        'angular_peak_frames': peaks,
        'wrist_flexion_range_deg': [round(min(r['wrist_flexion'] for r in rows), 1), round(max(r['wrist_flexion'] for r in rows), 1)],
        'wrist_deviation_range_deg': [round(min(r['wrist_deviation'] for r in rows), 1), round(max(r['wrist_deviation'] for r in rows), 1)],
        'hand_l_travel_max_cm': round(max((r['hand_l'] - rows[0]['hand_l']).length for r in rows), 1),
        'tip_min_z_cm': round(min(r['tip'].z for r in rows), 1), 'tip_min_z_frame': min(range(len(rows)), key=lambda i: rows[i]['tip'].z) + first,
        'root_travel_cm': [round(v, 1) for v in rows[-1]['root'] - rows[0]['root']],
        'root_speed_start_end_cm_per_frame': [round(root_steps[0], 3), round(root_steps[-1], 3)],
        'pelvis_root_horizontal_max_cm': round(pelvis_root, 1),
        'max_bone_rotation_per_frame_deg': round(max_step_rotation, 1),
        'feet': slides,
        'blade_body_clearance_min_cm': {name: round(min(r['body_clearance'][name] for r in rows), 1) for name in rows[0]['body_clearance']},
        'blade_body_clearance_worst_frame': {name: min(range(len(rows)), key=lambda i: rows[i]['body_clearance'][name]) + first for name in rows[0]['body_clearance']},
    }


rows = sample()
summary = summarize(rows)
serial = [{key: ([round(v, 2) for v in value] if isinstance(value, Vector) else round(value, 2) if isinstance(value, float) else value)
           for key, value in row.items() if key not in ('rotations', 'all_rotations', 'body_clearance')} for row in rows]
WORK.mkdir(parents=True, exist_ok=True)
(WORK / f'measure_{action.name}.json').write_text(json.dumps({'summary': summary, 'frames': serial}, indent=1), encoding='utf-8')
print(json.dumps(summary))
