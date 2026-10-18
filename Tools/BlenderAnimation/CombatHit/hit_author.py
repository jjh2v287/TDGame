"""Blender MCP 안에서 실행: Manny 피격 3종(플레이어 칼·방패 가벼움·무거움, 맨손 몬스터)을 원본 모캡 몸통·다리 변화량 보존 + 팔 교체로 저작한다(TD_SPECS로 대상 선택).
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/CombatHit/hit_author.py [TD_이름=<파이썬 리터럴> ...]
출력: Action TD_Player_HitReact_*·TD_Manny_HitReact_Unarmed
상태: 현행 (2026-10-18, M3-27)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path('C:/Project/TDGame')
OUT = ROOT / 'Saved/AgentOps/20261018/combat-full/blender'
COMBO = ROOT / 'Tools/BlenderAnimation/blender_author_sword_shield_combo.py'
_text = COMBO.read_text(encoding='utf-8')
C = {'__file__': str(COMBO), '__name__': 'td_combo_helpers'}
exec(compile(_text[:_text.index('STAGE_SPECS = [')], str(COMBO), 'exec'), C)

rig = C['armature']
order, parents, rest = C['order'], C['parents'], C['rest']
FORWARD, RIGHT, LEFT, UP = C['FORWARD'], C['RIGHT'], C['LEFT'], C['UP']
FPS = 60.0
DT = 1.0 / FPS
children = {n: [] for n in order}
for n in order:
    if parents[n]:
        children[parents[n]].append(n)


def descendants(name):
    out, stack = [], [name]
    while stack:
        cur = stack.pop()
        out.append(cur)
        stack.extend(children[cur])
    return set(out)


ARM = {s: descendants(f'clavicle_{s}') for s in 'lr'}
FINGERS = {s: descendants(f'hand_{s}') - {f'hand_{s}'} for s in 'lr'}
LEG = {s: descendants(f'thigh_{s}') for s in 'lr'}
BODY = ['pelvis', 'spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05', 'neck_01', 'neck_02', 'head']
HEAD_EXTRA = descendants('head') - {'head'}


def sample(action, frames):
    return C['sample_source'](action, [float(f) for f in frames])


def local_of(pose, n):
    p = parents[n]
    return pose[p].inverted() @ pose[n] if p else pose[n].copy()


def fk(local):
    out = {}
    for n in order:
        p = parents[n]
        out[n] = local[n].copy() if p is None else out[p] @ local[n]
    return out


def qmat(q, t):
    m = q.to_matrix().to_4x4()
    m.translation = t
    return m


def smooth01(x):
    x = max(0.0, min(1.0, x))
    return x * x * x * (x * (x * 6 - 15) + 10)


def fade_out(i, n, length):
    return 1.0 - smooth01((i - (n - 1 - length)) / float(length))


def env(i, keys):
    return C['pchip'](keys, float(i))


def rotvec(q):
    q = q.normalized()
    if q.w < 0:
        q.negate()
    ang = 2.0 * math.atan2(Vector((q.x, q.y, q.z)).length, q.w)
    ax = Vector((q.x, q.y, q.z))
    return ax.normalized() * ang if ax.length > 1e-9 else Vector()


def from_rotvec(v):
    return Quaternion(v.normalized(), v.length) if v.length > 1e-9 else Quaternion()


def rotate_chain(pose, bone, pivot, q):
    over = {bone: Matrix.Translation(pivot) @ q.to_matrix().to_4x4() @ Matrix.Translation(-pivot) @ pose[bone]}
    return C['forward_kinematics'](pose, over)


def accel_series(points):
    n = len(points)
    acc = []
    for i in range(n):
        a, b, c = points[max(0, i - 1)], points[i], points[min(n - 1, i + 1)]
        acc.append((a - 2 * b + c) / (DT * DT) if 0 < i < n - 1 else Vector())
    sm = []
    for i in range(n):
        tot, wsum = Vector(), 0.0
        for k in range(-2, 3):
            j = min(n - 1, max(0, i + k))
            w = math.exp(-0.5 * (k / 1.0) ** 2)
            tot += acc[j] * w
            wsum += w
        sm.append(tot / wsum)
    return sm


def simulate_spring(drive, axes, omega, zeta, sub=8):
    pos, vel, out = Vector(), Vector(), []
    for i, a in enumerate(drive):
        h = DT / sub
        for _ in range(sub):
            acc = -omega * omega * pos - 2 * zeta * omega * vel - a
            vel += acc * h
            pos += vel * h
            ax = axes[i]
            pos -= ax * pos.dot(ax)
            vel -= ax * vel.dot(ax)
        out.append(pos.copy())
    return out


def body_transfer(src, idle0, n_out, fade_len, src_start=0):
    s0 = src[src_start]
    poses = []
    for i in range(n_out):
        s = src[src_start + i]
        f = fade_out(i, n_out, fade_len)
        local = {name: local_of(idle0, name) for name in order}
        for name in BODY:
            ls, l0, li = local_of(s, name), local_of(s0, name), local_of(idle0, name)
            dq = Quaternion().slerp(ls.to_quaternion() @ l0.to_quaternion().inverted(), f)
            t = li.translation + (ls.translation - l0.translation) * f if name == 'pelvis' else li.translation
            local[name] = qmat(dq @ li.to_quaternion(), t)
        for side in 'lr':
            for name in LEG[side]:
                ls, l0, li = local_of(s, name), local_of(s0, name), local_of(idle0, name)
                dq = Quaternion().slerp(ls.to_quaternion() @ l0.to_quaternion().inverted(), f)
                local[name] = qmat(dq @ li.to_quaternion(), li.translation)
        poses.append(fk(local))
    return poses


HEEL_LOCAL = {s: rest[f'foot_{s}'].inverted() @ Vector((rest[f'foot_{s}'].translation.x, rest[f'foot_{s}'].translation.y + 4.0, 0.6)) for s in 'lr'}


def plant_legs(poses, src, idle0, n_out, fade_len, src_start=0):
    s0 = src[src_start]
    targets = {'l': [], 'r': []}
    ball_locals = {'l': [], 'r': []}
    for i in range(n_out):
        s = src[src_start + i]
        f = fade_out(i, n_out, fade_len)
        for side in 'lr':
            foot, ball = f'foot_{side}', f'ball_{side}'
            dq = Quaternion().slerp(s[foot].to_quaternion() @ s0[foot].to_quaternion().inverted(), f)
            dt = (s[foot].translation - s0[foot].translation) * f
            target = qmat(dq @ idle0[foot].to_quaternion(), idle0[foot].translation + dt)
            ball_local = local_of(s, ball)
            ball_world = target @ ball_local
            lift = (s[ball].translation.z - s0[ball].translation.z)
            lock = 1.0 - smooth01((lift - 1.0) / 2.0)
            slide = idle0[ball].translation - ball_world.translation
            slide.z = 0.0
            target.translation += slide * lock
            targets[side].append(target)
            ball_locals[side].append(ball_local)
    heel_lift = {}
    for side in 'lr':
        heel = [t @ HEEL_LOCAL[side] for t in targets[side]]
        speed = []
        for i in range(n_out):
            a, b = heel[max(0, i - 1)], heel[min(n_out - 1, i + 1)]
            d = b - a
            speed.append(math.hypot(d.x, d.y) / max(1, min(n_out - 1, i + 1) - max(0, i - 1)))
        raw = [min(3.0, 1.8 * v) if heel[i].z < 3.0 else 0.0 for i, v in enumerate(speed)]
        sm = []
        for i in range(n_out):
            tot = wsum = 0.0
            for k in range(-3, 4):
                j = min(n_out - 1, max(0, i + k))
                w = math.exp(-0.5 * (k / 1.5) ** 2)
                tot += raw[j] * w
                wsum += w
            sm.append(tot / wsum * fade_out(i, n_out, 3) * (1.0 if i > 0 else 0.0))
        heel_lift[side] = sm
    out = []
    stats = {'l': [], 'r': []}
    for i, pose in enumerate(poses):
        overrides = {}
        for side in 'lr':
            target = targets[side][i]
            ball_local = ball_locals[side][i]
            ball_world = target @ ball_local
            if heel_lift[side][i] > 1e-3:
                pivot = ball_world.translation
                heel = target @ HEEL_LOCAL[side]
                arm_len = max(1.0, math.hypot(heel.x - pivot.x, heel.y - pivot.y))
                angle = math.degrees(math.asin(min(0.5, heel_lift[side][i] / arm_len)))
                target, _ = C['foot_pivot_pose'](target, ball_world, pivot, angle)
            res, over = C['solve_leg'](side, pose, pose, target.translation, target.to_quaternion(), ball_world.to_quaternion())
            overrides.update(res)
            stats[side].append(round(over, 3))
        for ik, tgt in (('ik_foot_l', 'foot_l'), ('ik_foot_r', 'foot_r')):
            overrides[ik] = overrides[tgt].copy()
        out.append(C['forward_kinematics'](pose, overrides))
    return out, {k: max(v) for k, v in stats.items()} | {'heel_lift_max_cm': {k: round(max(v), 2) for k, v in heel_lift.items()}}


def set_arm_locals(poses, idle0, guards, weights, grip):
    out = []
    for i, pose in enumerate(poses):
        local = {name: local_of(pose, name) for name in order}
        for side in 'lr':
            w = weights[side][i]
            for name in ARM[side]:
                li = local_of(idle0, name)
                if guards is None:
                    local[name] = li
                    continue
                if name in FINGERS[side]:
                    local[name] = local_of(grip, name)
                    continue
                lg = local_of(guards[i], name)
                qi, qg = li.to_quaternion(), lg.to_quaternion()
                if qi.dot(qg) < 0:
                    qg.negate()
                local[name] = qmat(qi.slerp(qg, w), li.translation.lerp(lg.translation, w))
        out.append(fk(local))
    return out


def chest_frame_targets(idle0, spec_guard):
    pelvis = idle0['pelvis'].translation
    chest_inv = idle0['spine_05'].inverted()
    rel = lambda v: chest_inv @ (pelvis + C['char_vec'](*v))
    chest_rot_inv = idle0['spine_05'].to_quaternion().inverted()
    def unit(v):
        return chest_rot_inv @ C['char_vec'](*v).normalized()
    return {'wrist_l': rel(spec_guard['wrist_l']), 'elbow_l': rel(spec_guard['elbow_l']), 'shield_n': unit(spec_guard['shield_normal']),
            'wrist_r': rel(spec_guard['wrist_r']), 'elbow_r': rel(spec_guard['elbow_r']), 'blade': unit(spec_guard['blade'])}


def twist_overrides(side, pose, over):
    full = C['forward_kinematics'](pose, over)
    lo, ha = f'lowerarm_{side}', f'hand_{side}'
    twist = math.remainder(C['forearm_twist_side'](full[lo], full[ha], side) - C['forearm_twist_side'](pose[lo], pose[ha], side), math.tau)
    for name, frac in C['twist_fractions'](side).items():
        base = pose[lo].inverted() @ pose[name]
        over[name] = over[lo] @ base @ Quaternion(C['twist_axis_in'](name, side), twist * frac).to_matrix().to_4x4()
    return over


def ik_guard(pose, targets):
    chest = pose['spine_05']
    crot = chest.to_quaternion()
    over = {}
    n = crot @ targets['shield_n']
    horiz = Vector((n.x, n.y, 0.0)).normalized()
    az = math.degrees(math.atan2(horiz.dot(RIGHT), horiz.dot(FORWARD)))
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, n.z))))
    left, _ = C['solve_left_shield_arm_single'](pose, pose, chest @ targets['wrist_l'], chest @ targets['elbow_l'], az, pitch)
    over.update(left)
    over = twist_overrides('l', pose, over)
    geom = C['arm_geometry_side'](pose, chest @ targets['wrist_r'], 'r')
    direction = C['perpendicular_direction'](geom, chest @ targets['elbow_r'] - geom['shoulder'])
    _, upper, lower = C['arm_bones_side'](geom, pose, direction, 'r')
    blade = (crot @ targets['blade']).normalized()
    edge = (-UP - blade * (-UP).dot(blade)).normalized()
    socket_rot = Matrix((edge, -blade, edge.cross(-blade))).transposed()
    hand_target = (socket_rot @ C['SOCKET_LOCAL'].to_3x3().inverted()).to_quaternion()
    tw, fl, dv = C['hand_parameters_r'](lower, hand_target)
    k = math.radians(8.0)
    fl = C['soft_clamp'](fl, -math.radians(60.0), math.radians(60.0), k)
    dv = C['soft_clamp'](dv, -math.radians(30.0), math.radians(25.0), k)
    right = {'upperarm_r': upper, 'lowerarm_r': lower, 'hand_r': qmat(C['hand_from_parameters_r'](lower, tw, fl, dv), geom['wrist'])}
    right = twist_overrides('r', pose, right)
    over.update(right)
    return C['forward_kinematics'](pose, over)


def shield_push(poses, push_keys, push_deg, idle0):
    out = []
    chest_inv = idle0['spine_05'].to_quaternion().inverted()
    up_local, right_local = chest_inv @ UP, chest_inv @ RIGHT
    for i, pose in enumerate(poses):
        amount = env(i, push_keys)
        if amount < 1e-4:
            out.append(pose)
            continue
        crot = pose['spine_05'].to_quaternion()
        up_c, right_c = crot @ up_local, crot @ right_local
        S = pose['upperarm_l'].translation.copy()
        q = Quaternion(up_c, math.radians(push_deg['abduct']) * amount) @ Quaternion(right_c, math.radians(push_deg['raise']) * amount)
        pose = rotate_chain(pose, 'upperarm_l', S, q)
        S, E, W = pose['upperarm_l'].translation, pose['lowerarm_l'].translation.copy(), pose['hand_l'].translation
        hinge = (E - S).cross(W - E).normalized()
        pose = rotate_chain(pose, 'lowerarm_l', E, Quaternion(hinge, math.radians(push_deg['fold']) * amount))
        W = pose['hand_l'].translation.copy()
        pose = rotate_chain(pose, 'hand_l', W, Quaternion(right_c, math.radians(push_deg['tilt']) * amount))
        out.append(pose)
    return out


def arm_dynamics(poses, cfg, n_out, fade_len):
    spine0 = poses[0]['spine_05'].to_quaternion()
    for side in 'lr':
        c = cfg[side]
        up, lo, ha = f'upperarm_{side}', f'lowerarm_{side}', f'hand_{side}'
        shoulders = [p[up].translation.copy() for p in poses]
        arm_axes = [(p[ha].translation - p[up].translation).normalized() for p in poses]
        drive = accel_series(shoulders)
        defl = simulate_spring([a * c['gain'] for a in drive], arm_axes, c['omega'], c['zeta'])
        new = []
        for i, pose in enumerate(poses):
            f = fade_out(i, n_out, fade_len)
            S = pose[up].translation
            v = pose[ha].translation - S
            q_swing = v.rotation_difference(v + defl[i] * f)
            tilt = rotvec(pose['spine_05'].to_quaternion() @ spine0.inverted())
            tilt.z = 0.0
            q_grav = from_rotvec(-tilt * c['gravity'] * f)
            new.append(rotate_chain(pose, up, S, q_swing @ q_grav))
        poses = new
        elbows = [p[lo].translation.copy() for p in poses]
        fore_axes = [(p[ha].translation - p[lo].translation).normalized() for p in poses]
        drive2 = accel_series(elbows)
        defl2 = simulate_spring([a * c['whip_gain'] for a in drive2], fore_axes, c['whip_omega'], c['zeta'])
        new = []
        for i, pose in enumerate(poses):
            f = fade_out(i, n_out, fade_len)
            S, E, W = pose[up].translation, pose[lo].translation, pose[ha].translation
            hinge = (E - S).cross(W - E)
            if hinge.length < 1e-6:
                new.append(pose)
                continue
            hinge.normalize()
            d = defl2[i] * f
            d -= hinge * d.dot(hinge)
            fore = W - E
            target = fore + d
            ang = fore.angle(target) if d.length > 1e-6 else 0.0
            sign = 1.0 if fore.cross(target).dot(hinge) >= 0 else -1.0
            flex = (E - S).angle(W - E)
            new_flex = flex + sign * ang
            new_flex = max(math.radians(6.0), min(math.radians(140.0), new_flex))
            q = Quaternion(hinge, new_flex - flex)
            new.append(rotate_chain(pose, lo, E, q))
        poses = new
    return poses


def finish_ik_bones(poses):
    out = []
    for pose in poses:
        over = {'ik_hand_gun': pose['hand_r'].copy(), 'ik_hand_r': pose['hand_r'].copy(), 'ik_hand_l': pose['hand_l'].copy(), 'ik_foot_l': pose['foot_l'].copy(), 'ik_foot_r': pose['foot_r'].copy()}
        out.append(C['forward_kinematics'](pose, over))
    return out


def build(spec, idle0, guard):
    n = spec['frames']
    src = sample(spec['source'], range(1 + spec['src_start'], 1 + spec['src_start'] + n))
    src_local = {i: s for i, s in enumerate(src)}
    poses = body_transfer(src_local, idle0, n, spec['fade'], 0)
    poses, leg_over = plant_legs(poses, src_local, idle0, n, spec['fade'], 0)
    if guard is not None:
        targets = chest_frame_targets(idle0, spec['guard_pose'])
        guards = [ik_guard(p, targets) for p in poses]
        weights = {s: [env(i, spec['guard'][s]) for i in range(n)] for s in 'lr'}
    else:
        guards, weights = None, {s: [0.0] * n for s in 'lr'}
    poses = set_arm_locals(poses, idle0, guards, weights, guard)
    if spec.get('push'):
        poses = shield_push(poses, spec['push'], spec['push_deg'], idle0)
    poses = arm_dynamics(poses, spec['dynamics'], n, spec['fade'])
    poses = finish_ik_bones(poses)
    C['write_action'](spec['action'], poses, [0.0] * n)
    return {'action': spec['action'], 'frames': n, 'duration_s': round((n - 1) / FPS, 4), 'source': spec['source'], 'source_frames60': [1 + spec['src_start'], spec['src_start'] + n], 'leg_overextension_cm': leg_over}


idle0 = sample('TD_Src_MM_Idle', [1])[0]
guard = sample('TD_SwordCombo01', [1])[0]
PLAYER_DYN = {'l': {'gain': 1.0, 'omega': 11.0, 'zeta': 0.40, 'gravity': 0.12, 'whip_gain': 0.7, 'whip_omega': 14.0},
              'r': {'gain': 1.0, 'omega': 10.0, 'zeta': 0.38, 'gravity': 0.15, 'whip_gain': 0.8, 'whip_omega': 13.0}}
MONSTER_DYN = {s: {'gain': 1.8, 'omega': 6.5, 'zeta': 0.35, 'gravity': 0.6, 'whip_gain': 1.1, 'whip_omega': 9.0} for s in 'lr'}
GUARD_POSE = {'wrist_l': (28.0, -20.0, 28.0), 'elbow_l': (2.0, -40.0, 16.0), 'shield_normal': (0.98, -0.17, 0.14),
              'wrist_r': (22.0, 25.0, 12.0), 'elbow_r': (-6.0, 40.0, 18.0), 'blade': (0.92, -0.12, 0.36)}
SPECS = globals().get('TD_SPECS') or [
    {'action': 'TD_HitReact_Light', 'source': 'TD_Src_MM_HitReact_Front_Lgt_01', 'src_start': 0, 'frames': 39, 'fade': 10, 'guard_pose': GUARD_POSE,
     'guard': {'l': [(0, 0.0), (4, 0.45), (9, 0.8), (20, 0.8), (38, 0.0)], 'r': [(0, 0.0), (5, 0.3), (10, 0.6), (20, 0.6), (38, 0.0)]}, 'dynamics': PLAYER_DYN},
    {'action': 'TD_HitReact_Heavy', 'source': 'TD_Src_MM_HitReact_Front_Hvy_01', 'src_start': 0, 'frames': 47, 'fade': 12, 'guard_pose': GUARD_POSE,
     'guard': {'l': [(0, 0.0), (3, 0.45), (7, 1.0), (30, 1.0), (46, 0.0)], 'r': [(0, 0.0), (5, 0.35), (10, 0.7), (28, 0.7), (46, 0.0)]},
     'push': [(0, 0.0), (3, 0.0), (10, 1.0), (14, 0.85), (24, 0.3), (33, 0.0), (46, 0.0)],
     'push_deg': {'abduct': 28.0, 'raise': 8.0, 'fold': 12.0, 'tilt': 20.0}, 'dynamics': PLAYER_DYN},
    {'action': 'TD_Manny_HitReact_Unarmed', 'source': 'TD_Src_MM_HitReact_Front_Med_01', 'src_start': 0, 'frames': 41, 'fade': 10,
     'guard': None, 'dynamics': MONSTER_DYN},
]
results = []
for spec in SPECS:
    results.append(build(spec, idle0, guard if spec['guard'] else None))
rig.animation_data.action = bpy.data.actions[SPECS[0]['action']]
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'hit_work.blend'))
(OUT / 'hit-author-result.json').write_text(json.dumps(results, indent=1), encoding='utf-8')
print(json.dumps(results))
