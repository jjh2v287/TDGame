"""Blender MCP 안에서 실행: 하이에나 Idle 0프레임 위에 0.45초 피격 반동(몸통·머리·앞다리·꼬리·귀)을 얹는다(TD_HYENA·TD_FRAMES).
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/CombatHit/hyena_author.py [TD_이름=<파이썬 리터럴> ...]
출력: Action TD_Hyena_Hit
상태: 현행 (2026-10-18, M3-27)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

OUT = Path('C:/Project/TDGame/Saved/AgentOps/20261018/combat-full/blender')
scene = bpy.context.scene
scene.render.fps, scene.render.fps_base = 60, 1.0
arm = bpy.data.objects['RL_BoneRoot']
bones = arm.pose.bones
rest = {b.name: b.matrix_local.copy() for b in arm.data.bones}
parents = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
order = [b.name for b in bones]
children = {n: [] for n in order}
for n in order:
    if parents[n]:
        children[parents[n]].append(n)
src = bpy.data.actions.get('TD_Src_Hyena_Idle') or arm.animation_data.action
src.name = 'TD_Src_Hyena_Idle'
src.use_fake_user = True
LATERAL = Vector((1.0, 0.0, 0.0))
FRAMES = int(globals().get('TD_FRAMES', 28))
P = globals().get('TD_HYENA') or {
    'body_back': 8.0, 'body_down': 3.0, 'body_pitch': 1.5, 'spine1': 1.0, 'spine2': 2.0,
    'neck': 10.0, 'neck1': 3.0, 'head': 10.0, 'tail': [-2.5, -3.5, -3.5, -3.5, -2.5, -2.0], 'ears': -30.0,
}
CURVES = {
    'body': [(0, 0.0), (3, 0.85), (5, 1.0), (9, 0.7), (15, 0.25), (20, 0.02), (23, -0.04), (27, 0.0)],
    'spine': [(0, 0.0), (4, 0.8), (6, 1.0), (10, 0.65), (16, 0.22), (21, 0.0), (24, -0.03), (27, 0.0)],
    'neck': [(0, 0.0), (2, 0.3), (5, 0.85), (7, 1.0), (11, 0.75), (17, 0.3), (22, 0.05), (27, 0.0)],
    'tail': [(0, 0.0), (4, 0.35), (8, 0.85), (11, 1.0), (16, 0.65), (21, 0.25), (25, 0.05), (27, 0.0)],
    'ears': [(0, 0.0), (3, 0.6), (6, 1.0), (12, 0.8), (20, 0.2), (27, 0.0)],
}


def pchip(keys, x):
    xs = [k[0] for k in keys]
    ys = [k[1] for k in keys]
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    n = len(xs)
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    for i in range(1, n - 1):
        if d[i - 1] * d[i] > 0:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    k = max(i for i in range(n - 1) if xs[i] <= x)
    t = (x - xs[k]) / h[k]
    h00, h10, h01, h11 = 2 * t ** 3 - 3 * t ** 2 + 1, t ** 3 - 2 * t ** 2 + t, -2 * t ** 3 + 3 * t ** 2, t ** 3 - t ** 2
    return h00 * ys[k] + h10 * h[k] * m[k] + h01 * ys[k + 1] + h11 * h[k] * m[k + 1]


def sample(frame):
    arm.animation_data.action = src
    scene.frame_set(frame)
    return {b.name: b.matrix.copy() for b in bones}


def fk_from(pose, overrides):
    out = {}
    for n in order:
        if n in overrides:
            out[n] = overrides[n]
            continue
        p = parents[n]
        out[n] = pose[n].copy() if p is None else out[p] @ (pose[p].inverted() @ pose[n])
    return out


def rotate(pose, name, angle_deg, axis=LATERAL, translate=None):
    m = pose[name]
    pivot = m.translation.copy()
    r = Matrix.Translation(pivot) @ Quaternion(axis, math.radians(angle_deg)).to_matrix().to_4x4() @ Matrix.Translation(-pivot)
    if translate is not None:
        r = Matrix.Translation(translate) @ r
    return fk_from(pose, {name: r @ m})


def frame_from(direction, normal):
    x = direction.normalized()
    z = x.cross(normal).normalized()
    y = z.cross(x)
    return Matrix((x, y, z)).transposed()


def align(src_m, src_dir, src_n, head, new_dir, new_n):
    r = frame_from(new_dir, new_n) @ frame_from(src_dir, src_n).transposed()
    out = (r @ src_m.to_3x3()).to_4x4()
    out.translation = head
    return out


def two_bone(root, target, la, lb, pole):
    d = target - root
    dist = min(max(d.length, abs(la - lb) + 0.01), (la + lb) * 0.999)
    axis = d.normalized()
    along = (la * la - lb * lb + dist * dist) / (2 * dist)
    hgt = math.sqrt(max(0.0, la * la - along * along))
    perp = (pole - axis * pole.dot(axis)).normalized()
    return root + axis * along + perp * hgt, root + axis * dist, max(0.0, d.length - (la + lb) * 0.999)


def solve_chain(pose, idle, a, b, end_name, end_target):
    sa, sb, se = idle[a].translation, idle[b].translation, idle[end_name].translation
    la, lb = (sb - sa).length, (se - sb).length
    root = pose[a].translation
    axis = (se - sa).normalized()
    pole = (sb - sa) - axis * (sb - sa).dot(axis)
    mid, end, over = two_bone(root, end_target, la, lb, pole)
    n_old = (sb - sa).cross(se - sb)
    n_new = (mid - root).cross(end - mid)
    return {a: align(idle[a], sb - sa, n_old, root, mid - root, n_new), b: align(idle[b], se - sb, n_old, mid, end - mid, n_new)}, over


idle0 = sample(1)
poses, overs = [], []
for i in range(FRAMES):
    eb, es, en, et, ee = (pchip(CURVES[k], i) for k in ('body', 'spine', 'neck', 'tail', 'ears'))
    pose = {k: v.copy() for k, v in idle0.items()}
    pose = rotate(pose, 'bip01', P['body_pitch'] * eb, translate=Vector((0.0, P['body_back'] * eb, -P['body_down'] * eb)))
    pose = rotate(pose, 'bip01_spine1', P['spine1'] * es)
    pose = rotate(pose, 'bip01_spine2', P['spine2'] * es)
    chest_delta = pose['bip01_spine2'] @ idle0['bip01_spine2'].inverted()
    clav = {s: chest_delta @ idle0[f'bip01_{s}_clavicle'] for s in 'lr'}
    pose = rotate(pose, 'bip01_neck', P['neck'] * en)
    pose = rotate(pose, 'bip01_neck1', P['neck1'] * en)
    pose = rotate(pose, 'bip01_head', P['head'] * en)
    pose = fk_from(pose, {f'bip01_{s}_clavicle': clav[s] for s in 'lr'})
    for k, nm in enumerate(['bone01', 'bone02', 'bone03', 'bone04', 'bone05', 'bone06']):
        pose = rotate(pose, nm, P['tail'][k] * et)
    for nm in ('bip01_ponytail1', 'bip01_ponytail2'):
        pose = rotate(pose, nm, P['ears'] * ee)
    over = {}
    worst = 0.0
    for s in 'lr':
        res, o1 = solve_chain(pose, idle0, f'bip01_{s}_upperarm', f'bip01_{s}_forearm', f'bip01_{s}_hand', idle0[f'bip01_{s}_hand'].translation)
        over.update(res)
        over[f'bip01_{s}_hand'] = idle0[f'bip01_{s}_hand'].copy()
        res2, o2 = solve_chain(pose, idle0, f'bip01_{s}_thigh', f'bip01_{s}_calf', f'bip01_{s}_horselink', idle0[f'bip01_{s}_horselink'].translation)
        over.update(res2)
        over[f'bip01_{s}_horselink'] = idle0[f'bip01_{s}_horselink'].copy()
        worst = max(worst, o1, o2)
    pose = fk_from(pose, over)
    poses.append(pose)
    overs.append(round(worst, 3))

old = bpy.data.actions.get('TD_Hyena_Hit')
if old:
    bpy.data.actions.remove(old)
action = bpy.data.actions.new('TD_Hyena_Hit')
action.use_fake_user = True
arm.animation_data.action = action
prev = {}
for i, pose in enumerate(poses):
    for n in order:
        p = parents[n]
        basis = rest[n].inverted() @ pose[n] if p is None else (pose[p] @ rest[p].inverted() @ rest[n]).inverted() @ pose[n]
        loc, rot, sc = basis.decompose()
        if n in prev and prev[n].dot(rot) < 0:
            rot.negate()
        prev[n] = rot
        pb = bones[n]
        pb.rotation_mode = 'QUATERNION'
        pb.location, pb.rotation_quaternion, pb.scale = loc, rot, sc
        for path in ('location', 'rotation_quaternion', 'scale'):
            pb.keyframe_insert(path, frame=i + 1, group=n)
scene.frame_start, scene.frame_end = 1, FRAMES
probe = {}
for key, nm in (('head', 'bip01_head'), ('neck', 'bip01_neck'), ('tail_tip', 'bone06'), ('ear_l', 'bip01_ponytail1'), ('pelvis', 'bip01_pelvis'), ('l_hand', 'bip01_l_hand'), ('r_foot', 'bip01_r_foot')):
    probe[key] = [[round(v, 1) for v in (poses[i][nm].translation - idle0[nm].translation)] for i in (0, 4, 7, 11, 16, 22, FRAMES - 1)]
tipd = {}
for nm, child in (('bip01_head', 'bone08'), ('bone06', 'bone06_end'), ('bip01_ponytail1', 'bip01_ponytail1_end')):
    if child in poses[0]:
        tipd[nm] = [[round(v, 1) for v in (poses[i][child].translation - idle0[child].translation)] for i in (0, 7, 11, 16, FRAMES - 1)]
res = {'action': action.name, 'frames': FRAMES, 'duration_s': round((FRAMES - 1) / 60.0, 4), 'leg_overextension_cm_max': max(overs), 'disp_cm_xyz_at_f1_5_8_12_17_23_end': probe, 'child_tip_disp': tipd}
(OUT / 'hyena-author-result.json').write_text(json.dumps(res, indent=1), encoding='utf-8')
print(json.dumps(res))
