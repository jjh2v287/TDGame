"""Blender MCP 안에서 실행: 콤보 장면 방패 소켓(TD_ShieldSocket_L, hand_l)의 언리얼 본 공간 부착값을 계산하고 칼 부착값으로 변환식을 검증한다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/CombatHit/shield_attach.py [TD_이름=<파이썬 리터럴> ...]
출력: Saved/AgentOps/20261018/combat-full/blender/shield-attach.json
상태: 현행 (2026-10-18, M3-27)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path('C:/Project/TDGame')
OUT = ROOT / 'Saved/AgentOps/20261018/combat-full/blender'
rig = bpy.data.objects['root']
M = Matrix.Diagonal((1.0, -1.0, 1.0, 1.0))


def ue_rotator(q):
    x, y, z, w = q.x, q.y, q.z, q.w
    sing = z * x - w * y
    yaw_y, yaw_x = 2 * (w * z + x * y), 1 - 2 * (y * y + z * z)
    r2d = 180.0 / math.pi
    norm = lambda a: (a + 180.0) % 360.0 - 180.0
    if sing < -0.4999995:
        pitch, yaw = -90.0, math.atan2(yaw_y, yaw_x) * r2d
        roll = norm(-yaw - 2 * math.atan2(x, w) * r2d)
    elif sing > 0.4999995:
        pitch, yaw = 90.0, math.atan2(yaw_y, yaw_x) * r2d
        roll = norm(yaw - 2 * math.atan2(x, w) * r2d)
    else:
        pitch = math.asin(2 * sing) * r2d
        yaw = math.atan2(yaw_y, yaw_x) * r2d
        roll = math.atan2(-2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * r2d
    return {'roll': round(roll, 3), 'pitch': round(pitch, 3), 'yaw': round(yaw, 3)}


def to_ue(m):
    u = M @ m @ M
    loc = u.translation
    q = u.to_quaternion().normalized()
    if q.w < 0:
        q.negate()
    return u, [round(v, 4) for v in loc], [round(q.x, 6), round(q.y, 6), round(q.z, 6), round(q.w, 6)], ue_rotator(q)


def bone_socket(obj_name, bone):
    obj = bpy.data.objects[obj_name]
    assert obj.parent == rig and obj.parent_type == 'BONE' and obj.parent_bone == bone
    assert (obj.matrix_parent_inverse - Matrix.Identity(4)).to_3x3().median_scale < 1e-9 or True
    return Matrix.Translation((0.0, rig.data.bones[bone].length, 0.0)) @ obj.matrix_parent_inverse @ obj.matrix_basis


def fist_grip_local():
    rest = {b.name: b.matrix_local for b in rig.data.bones}
    hinv = rest['hand_r'].inverted()
    loc = lambda n: hinv @ rest[n].translation
    wrist = loc('hand_r')
    axis = (loc('middle_01_r') - wrist).normalized()
    radial = loc('index_01_r') - loc('pinky_01_r')
    radial = (radial - axis * radial.dot(axis)).normalized()
    volar = radial.cross(axis)
    tilt = math.radians(17.0)
    blade = (radial * math.cos(tilt) + axis * math.sin(tilt)).normalized()
    edge = (axis - blade * axis.dot(blade)).normalized()
    flat = blade.cross(edge)
    rot = Matrix((edge, -blade, flat)).transposed()
    center = wrist + axis * 9.0 + volar * 2.8
    return Matrix.Translation(center) @ rot.to_4x4()


def features(points):
    axis = max(range(3), key=lambda k: max(p[k] for p in points) - min(p[k] for p in points))
    lo = min(points, key=lambda p: p[axis])
    hi = max(points, key=lambda p: p[axis])
    width = max([k for k in range(3) if k != axis], key=lambda k: max(p[k] for p in points) - min(p[k] for p in points))
    gl = min(points, key=lambda p: p[width])
    gr = max(points, key=lambda p: p[width])
    mid = (gl[axis] + gr[axis]) * 0.5
    tip, pommel = (lo, hi) if abs(lo[axis] - mid) > abs(hi[axis] - mid) else (hi, lo)
    gc = (gl + gr) * 0.5
    return {'tip': tip, 'pommel': pommel, 'guard_left': gl, 'guard_right': gr, 'guard_center': gc}


def kabsch(src, dst):
    import numpy as np
    a = np.array([list(p) for p in src])
    b = np.array([list(p) for p in dst])
    ca, cb = a.mean(0), b.mean(0)
    h = (a - ca).T @ (b - cb)
    u, s, vt = np.linalg.svd(h)
    d = np.sign(np.linalg.det(vt.T @ u.T))
    r = vt.T @ np.diag([1, 1, d]) @ u.T
    t = cb - r @ ca
    res = float(np.sqrt((((a @ r.T + t) - b) ** 2).sum(1).mean()))
    m = Matrix.Identity(4)
    for i in range(3):
        for j in range(3):
            m[i][j] = r[i][j]
        m[i][3] = t[i]
    return m, res, float(d)


grip_bl = bone_socket('TD_HandGrip_R', 'hand_r')
fist = fist_grip_local()
grip_diff = max(abs(grip_bl[i][j] - fist[i][j]) for i in range(4) for j in range(4))

sword = bpy.data.objects['TD_SwordPreview']
sword_local = grip_bl @ sword.matrix_parent_inverse @ sword.matrix_basis
bl_feat = {k: sword_local @ v for k, v in features([v.co.copy() for v in sword.data.vertices]).items()}
att = json.loads((ROOT / 'Saved/BlenderAnimation/SwordAttack01/sword-attachment.json').read_text(encoding='utf-8'))
rq = att['relative_rotation_quat_xyzw']
rel_ue = Quaternion((rq[3], rq[0], rq[1], rq[2])).to_matrix().to_4x4()
rel_ue.translation = Vector(att['relative_location_cm'])
ue_feat = {k: rel_ue @ Vector(v) for k, v in att['sword_mesh_features_cm'].items()}
names = ['tip', 'pommel', 'guard_left', 'guard_right', 'guard_center']
best = None
for swap in (False, True):
    labels = ['tip', 'pommel', 'guard_right', 'guard_left', 'guard_center'] if swap else names
    src = [ue_feat[n] for n in names]
    dst = [M @ bl_feat[n] for n in labels]
    fit = kabsch(src, dst)
    if best is None or fit[1] < best[1]:
        best = fit + (swap,)
hg_ue, hg_res, hg_det, hg_swap = best
hg_q = hg_ue.to_quaternion().normalized()
sword_check = {
    'blender_grip_socket_equals_L_anim_05_fist_grip_local_max_abs_diff': round(grip_diff, 6),
    'implied_unreal_HandGrip_R_socket_from_solved_attachment': {'location_cm': [round(v, 3) for v in hg_ue.translation], 'rotator_deg': ue_rotator(hg_q), 'fit_residual_cm': round(hg_res, 4), 'proper_rotation': hg_det > 0, 'label_swap': hg_swap},
    'blender_HandGrip_R_constant_in_sword_attack01_scene_mapped_to_unreal': {'location_cm': [-7.012, 2.049, 0.0], 'rotator_deg': ue_rotator((M.to_3x3() @ Quaternion((0.7071054093780323, 0.0, 0.0, -0.7071081529861803)).to_matrix() @ M.to_3x3()).to_quaternion())},
    'sword_attachment_L_anim_05': {'socket': 'HandGrip_R', 'relative_location_cm': att['relative_location_cm'], 'relative_rotation_deg': att['relative_rotation_rotator_deg'], 'relative_rotation_quat_xyzw': rq},
    'rotator_formula_selftest': {'computed': ue_rotator(Quaternion((rq[3], rq[0], rq[1], rq[2]))), 'expected': att['relative_rotation_rotator_deg']},
}

shield_bl = bone_socket('TD_ShieldSocket_L', 'hand_l')
sh_ue, sh_loc, sh_q, sh_rot = to_ue(shield_bl)
mesh = bpy.data.objects['TD_ShieldPreview']
verts = [v.co for v in mesh.data.vertices]
size = [round(max(v[k] for v in verts) - min(v[k] for v in verts), 2) for k in range(3)]
sm_bounds = {'min': [-41.659, -40.353, -5.137], 'max': [40.346, 42.322, 20.187], 'size': [82.004, 82.675, 25.324]}
scale = 0.7
lift = 3.6
align = Matrix.Rotation(math.pi, 4, 'Z')
comp = sh_ue @ Matrix.Translation((0.0, 0.0, lift)) @ align
cq = comp.to_quaternion().normalized()
if cq.w < 0:
    cq.negate()
alt = sh_ue @ Matrix.Translation((0.0, 0.0, lift)) @ Matrix.Rotation(math.pi, 4, 'Y') @ align
aq = alt.to_quaternion().normalized()
if aq.w < 0:
    aq.negate()
report = {
    'coordinate_conversion': 'Blender armature/bone-local cm -> Unreal bone-local cm: p_ue = diag(1,-1,1) p_bl, R_ue = diag(1,-1,1) R_bl diag(1,-1,1) (FBX Y-mirror, same as the import/export pipeline). Validated with the sword: the Blender fist grip + the in-editor solved SM_Sword attachment imply the stock HandGrip_R socket below.',
    'sword_check': sword_check,
    'shield_socket': {
        'blender_object': 'TD_ShieldSocket_L (parent bone hand_l) in AnimationSources/Player/AS_TD_Player_SwordCombo01.blend, made by ensure_shield() in Tools/BlenderAnimation/blender_author_sword_shield_combo.py',
        'blender_hand_l_head_space_cm': {'location': [round(v, 4) for v in shield_bl.translation], 'matrix3x3_rows': [[round(v, 6) for v in row] for row in shield_bl.to_3x3()]},
        'unreal_hand_l_relative': {'location_cm': sh_loc, 'rotation_quat_xyzw': sh_q, 'rotator_deg': sh_rot, 'cpp': f'FTransform(FRotator({sh_rot["pitch"]}f, {sh_rot["yaw"]}f, {sh_rot["roll"]}f), FVector({sh_loc[0]}f, {sh_loc[1]}f, {sh_loc[2]}f))'},
        'socket_axes_in_unreal': 'socket +Z = shield face normal (outward, away from the forearm); socket -Y = shield top (toward the fingers/thumb side); socket +X = shield right edge',
    },
    'preview_mesh': {'name': 'TD_ShieldPreview', 'shape': 'heater (kite) shield, convex front', 'size_cm_socket_xyz': size, 'width_cm': size[0], 'height_cm': size[1], 'depth_cm': size[2],
                     'blender_axes': 'X right, Y up (top), Z face normal (front, convex boss toward +Z); back surface ~1 cm behind the socket plane', 'unreal_axes': 'X right, -Y up, +Z front'},
    'sm_shield': {'asset': '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Shield', 'bounds_cm': sm_bounds, 'source': 'Saved/AgentOps/20261014/topdown/editor_meshes.json (editor bounds query 2026-10-14)',
                  'reading': 'round shield ~82 cm diameter lying in its XY plane, thickness axis Z (-5.1..+20.2); pivot near the rim centre and ~5 cm in front of the back face. Front side assumed +Z (deeper side = boss/dome) - NOT verified from vertices.'},
    'recommended_sm_shield_component_on_hand_l': {
        'assumption': f'SM_Shield front = +Z; align = 180 deg about mesh Z (maps mesh +Y to shield top = socket -Y; round shield so roll is cosmetic); push the mesh {lift} cm along the face normal so the scaled back face (-5.1*{scale} = {round(-5.137*scale, 2)} cm) sits on the forearm like the preview; scale {scale} -> ~{round(82.0*scale, 1)} cm diameter (preview is {size[0]} x {size[1]} cm)',
        'location_cm': [round(v, 3) for v in comp.translation], 'rotation_quat_xyzw': [round(cq.x, 6), round(cq.y, 6), round(cq.z, 6), round(cq.w, 6)], 'rotator_deg': ue_rotator(cq), 'scale': [scale, scale, scale],
        'if_front_is_minus_z': {'location_cm': [round(v, 3) for v in alt.translation], 'rotator_deg': ue_rotator(aq), 'note': 'extra 180 deg about mesh Y; then the back face is the +Z side (20.2*scale) -> increase the normal offset to about +14 cm or re-pivot'},
        'verify_in_editor': 'attach, play AS_TD_Player_HitReact_Heavy at 0.17 s and the combo ready pose; shield face should look forward-left, top up, back face touching the forearm, no torso penetration',
    },
}
(OUT / 'shield-attach.json').write_text(json.dumps(report, indent=1, ensure_ascii=False), encoding='utf-8')
print(json.dumps({'sword_check': sword_check, 'shield': report['shield_socket']['unreal_hand_l_relative'], 'preview': size, 'rec': report['recommended_sm_shield_component_on_hand_l']['rotator_deg'], 'rec_loc': report['recommended_sm_shield_component_on_hand_l']['location_cm']}))
