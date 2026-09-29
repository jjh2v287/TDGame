"""에디터 안에서 실행: Blender 미리보기의 칼 특징점(grip-points.json)과 SM_Sword 메시 정점에서 찾은 같은 특징점을 프레임별 HandGrip_R 소켓 변환에 맞춰(Horn 쿼터니언 정합) 게임에서 쓸 SM_Sword 부착 상대 변환(위치 cm·회전)을 역산하고 프레임 간 일관성과 잔차를 보고한다.
실행: python Tools/run_in_editor.py Tools/BlenderAnimation/editor_solve_sword_attachment.py (PowerShell)
출력: Saved/BlenderAnimation/SwordAttack01/sword-attachment.json, 로그의 [TDTool] attachment 줄
상태: 현행 (2026-09-25, UE5 Manny SKM_Manny_Simple·SM_Sword 전용)
"""
import json
import math
import os

import unreal

SEQUENCE = globals().get('TD_SEQUENCE', '/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordAttack01')
MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
SWORD = '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword'
WORK = os.path.join(unreal.Paths.project_saved_dir(), 'BlenderAnimation', 'SwordAttack01')
NAMES = ['tip', 'pommel', 'guard_left', 'guard_right', 'guard_center']


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def q_inv(q):
    return (-q[0], -q[1], -q[2], q[3])


def q_rot(q, v):
    return q_mul(q_mul(q, (v[0], v[1], v[2], 0.0)), q_inv(q))[:3]


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def quat_of(rotator_or_quat):
    q = rotator_or_quat if isinstance(rotator_or_quat, unreal.Quat) else rotator_or_quat.quaternion()
    return (q.x, q.y, q.z, q.w)


def jacobi_eigen(matrix):
    a = [row[:] for row in matrix]
    size = len(a)
    vectors = [[1.0 if i == j else 0.0 for j in range(size)] for i in range(size)]
    for _ in range(100):
        p, q = max(((i, j) for i in range(size) for j in range(i + 1, size)), key=lambda pair: abs(a[pair[0]][pair[1]]))
        if abs(a[p][q]) < 1e-12:
            break
        theta = 0.5 * math.atan2(2.0 * a[p][q], a[q][q] - a[p][p])
        c, s = math.cos(theta), math.sin(theta)
        for k in range(size):
            akp, akq = a[k][p], a[k][q]
            a[k][p], a[k][q] = c * akp - s * akq, s * akp + c * akq
        for k in range(size):
            apk, aqk = a[p][k], a[q][k]
            a[p][k], a[q][k] = c * apk - s * aqk, s * apk + c * aqk
        for k in range(size):
            vkp, vkq = vectors[k][p], vectors[k][q]
            vectors[k][p], vectors[k][q] = c * vkp - s * vkq, s * vkp + c * vkq
    return [a[i][i] for i in range(size)], vectors


def horn_fit(source_points, target_points):
    count = len(source_points)
    source_center = tuple(sum(p[k] for p in source_points) / count for k in range(3))
    target_center = tuple(sum(p[k] for p in target_points) / count for k in range(3))
    s = [[0.0] * 3 for _ in range(3)]
    for a, b in zip(source_points, target_points):
        a, b = sub(a, source_center), sub(b, target_center)
        for i in range(3):
            for j in range(3):
                s[i][j] += a[i] * b[j]
    (sxx, sxy, sxz), (syx, syy, syz), (szx, szy, szz) = s
    n = [[sxx + syy + szz, syz - szy, szx - sxz, sxy - syx],
         [syz - szy, sxx - syy - szz, sxy + syx, szx + sxz],
         [szx - sxz, sxy + syx, -sxx + syy - szz, syz + szy],
         [sxy - syx, szx + sxz, syz + szy, -sxx - syy + szz]]
    values, vectors = jacobi_eigen(n)
    best = max(range(4), key=lambda k: values[k])
    vector = [vectors[i][best] for i in range(4)]
    rotation = (vector[1], vector[2], vector[3], vector[0])
    translation = sub(target_center, q_rot(rotation, source_center))
    residual = math.sqrt(sum(sum(d * d for d in sub(add(q_rot(rotation, a), translation), b)) for a, b in zip(source_points, target_points)) / count)
    return rotation, translation, residual


def sword_features():
    description = unreal.load_asset(SWORD).get_static_mesh_description(0)
    points = [description.get_vertex_position(unreal.VertexID(i)).to_tuple() for i in range(description.get_vertex_count())]
    axis = max(range(3), key=lambda k: max(p[k] for p in points) - min(p[k] for p in points))
    low_end = min(points, key=lambda p: p[axis])
    high_end = max(points, key=lambda p: p[axis])
    width_axes = [k for k in range(3) if k != axis]
    width_axis = max(width_axes, key=lambda k: max(p[k] for p in points) - min(p[k] for p in points))
    guard_left = min(points, key=lambda p: p[width_axis])
    guard_right = max(points, key=lambda p: p[width_axis])
    guard_middle = (guard_left[axis] + guard_right[axis]) * 0.5
    tip, pommel = (low_end, high_end) if abs(low_end[axis] - guard_middle) > abs(high_end[axis] - guard_middle) else (high_end, low_end)
    guard_center = tuple((a + b) * 0.5 for a, b in zip(guard_left, guard_right))
    return {'tip': tip, 'pommel': pommel, 'guard_left': guard_left, 'guard_right': guard_right, 'guard_center': guard_center}


def main():
    with open(os.path.join(WORK, 'grip-points.json'), encoding='utf-8') as handle:
        blender = json.load(handle)
    features = sword_features()
    mesh = unreal.load_asset(MESH)
    socket = mesh.find_socket('HandGrip_R')
    socket_rotation = quat_of(socket.relative_rotation)
    socket_location = socket.relative_location.to_tuple()
    sequence = unreal.load_asset(SEQUENCE)
    options = unreal.AnimPoseEvaluationOptions()
    options.optional_skeletal_mesh = mesh
    options.evaluation_type = unreal.AnimDataEvalType.RAW
    options.extract_root_motion = False
    options.incorporate_root_motion_into_pose = True
    relatives = []
    for row in blender['frames']:
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, row['time'], options)
        hand = unreal.AnimPoseExtensions.get_bone_pose(pose, socket.bone_name, unreal.AnimPoseSpaces.WORLD)
        hand_rotation, hand_location = quat_of(hand.rotation), hand.translation.to_tuple()
        world_rotation = q_mul(hand_rotation, socket_rotation)
        world_location = add(q_rot(hand_rotation, socket_location), hand_location)
        best = None
        for mirrored in (False, True):
            names = ['tip', 'pommel', 'guard_right', 'guard_left', 'guard_center'] if mirrored else NAMES
            fit = horn_fit([features[name] for name in names], [tuple(row['points'][name]) for name in NAMES])
            if best is None or fit[2] < best[2]:
                best = fit + (mirrored,)
        sword_rotation, sword_location, residual, mirrored = best
        inverse = q_inv(world_rotation)
        relative_rotation = q_mul(inverse, sword_rotation)
        if relative_rotation[3] < 0:
            relative_rotation = tuple(-v for v in relative_rotation)
        relative_location = q_rot(inverse, sub(sword_location, world_location))
        relatives.append({'frame': row['frame'], 'rotation': relative_rotation, 'location': relative_location, 'fit_residual_cm': residual, 'mirrored_labels': mirrored})
    mean_rotation = [sum(r['rotation'][k] for r in relatives) / len(relatives) for k in range(4)]
    norm = math.sqrt(sum(v * v for v in mean_rotation))
    mean_rotation = tuple(v / norm for v in mean_rotation)
    mean_location = tuple(sum(r['location'][k] for r in relatives) / len(relatives) for k in range(3))
    spread_angle = max(2.0 * math.degrees(math.acos(min(1.0, abs(sum(a * b for a, b in zip(r['rotation'], mean_rotation)))))) for r in relatives)
    spread_location = max(math.sqrt(sum(d * d for d in sub(r['location'], mean_location))) for r in relatives)
    rotator = unreal.Quat(*mean_rotation).rotator()
    report = {
        'socket': str(socket.socket_name), 'socket_bone': str(socket.bone_name),
        'relative_location_cm': [round(v, 3) for v in mean_location],
        'relative_rotation_quat_xyzw': [round(v, 6) for v in mean_rotation],
        'relative_rotation_rotator_deg': {'roll': round(rotator.roll, 3), 'pitch': round(rotator.pitch, 3), 'yaw': round(rotator.yaw, 3)},
        'max_fit_residual_cm': round(max(r['fit_residual_cm'] for r in relatives), 4),
        'frame_spread_angle_deg': round(spread_angle, 4), 'frame_spread_location_cm': round(spread_location, 4),
        'mirrored_labels': sorted({r['mirrored_labels'] for r in relatives}),
        'sword_mesh_features_cm': {name: [round(v, 3) for v in value] for name, value in features.items()},
    }
    report['passed'] = report['max_fit_residual_cm'] < 0.5 and spread_angle < 0.5 and spread_location < 0.5
    with open(os.path.join(WORK, 'sword-attachment.json'), 'w', encoding='utf-8') as handle:
        json.dump(report, handle, indent=1)
    unreal.log('[TDTool] attachment ' + json.dumps(report))


main()
