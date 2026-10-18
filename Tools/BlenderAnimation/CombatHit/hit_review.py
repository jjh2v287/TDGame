"""Blender MCP 안에서 실행: 피격 Action의 발 미끄러짐·루트 이동·뼈 길이·칼과 몸 간격을 재고 접촉 시트를 렌더한다(TD_JOBS·TD_RENDER).
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/CombatHit/hit_review.py [TD_이름=<파이썬 리터럴> ...]
출력: <이름>-measure.json·<이름>-sheet.png
상태: 현행 (2026-10-18, M3-27)
"""
import json
import math
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector

ROOT = Path('C:/Project/TDGame')
OUT = ROOT / 'Saved/AgentOps/20261018/combat-full/blender'
scene = bpy.context.scene
rig = bpy.data.objects['root']
JOBS = globals().get('TD_JOBS') or [
    ('TD_HitReact_Light', 'AS_TD_Player_HitReact_Light', True),
    ('TD_HitReact_Heavy', 'AS_TD_Player_HitReact_Heavy', True),
    ('TD_Manny_HitReact_Unarmed', 'AS_TD_Manny_HitReact_Unarmed', False),
]
DO_RENDER = globals().get('TD_RENDER', True)
SHEET_FRAMES = globals().get('TD_SHEET_FRAMES', None)
rest = {b.name: b.matrix_local.copy() for b in rig.data.bones}
r_names = [b.name for b in rig.data.bones]
parents = {b.name: (b.parent.name if b.parent else None) for b in rig.data.bones}
CAPSULES = [('pelvis', 'spine_03', 13.0), ('spine_03', 'neck_01', 14.0), ('neck_01', 'head', 6.0), ('head', 'head', 11.0),
            ('thigh_l', 'calf_l', 8.0), ('calf_l', 'foot_l', 5.5), ('thigh_r', 'calf_r', 8.0), ('calf_r', 'foot_r', 5.5),
            ('upperarm_l', 'lowerarm_l', 5.0), ('lowerarm_l', 'hand_l', 4.0), ('upperarm_r', 'lowerarm_r', 5.0), ('lowerarm_r', 'hand_r', 4.0)]


def seg_dist(p, a, b):
    ab = b - a
    t = 0.0 if ab.length < 1e-6 else max(0.0, min(1.0, (p - a).dot(ab) / ab.dot(ab)))
    return (p - (a + ab * t)).length


def heads():
    off = rig.matrix_world.translation * 100.0
    return {b.name: b.head + off for b in rig.pose.bones}, {b.name: b.matrix.copy() for b in rig.pose.bones}


def clearance(points, h, exclude):
    best = (1e9, None)
    for a, b, r in CAPSULES:
        if a in exclude:
            continue
        for p in points:
            d = seg_dist(p, h[a], h[b]) - r
            if d < best[0]:
                best = (d, a)
    return best


def angle_deg(qa, qb):
    d = abs(qa.normalized().dot(qb.normalized()))
    return math.degrees(2 * math.acos(min(1.0, d)))


def idle_reference():
    rig.animation_data.action = bpy.data.actions['TD_Src_MM_Idle']
    scene.frame_set(1)
    return heads()


def measure(action_name, armed):
    idle_h, idle_m = idle_reference()
    act = bpy.data.actions[action_name]
    rig.animation_data.action = act
    first, last = [int(round(v)) for v in act.frame_range]
    sword_tip = bpy.data.objects['TD_SwordTip']
    sword_grip = bpy.data.objects['TD_HandGrip_R']
    shield = bpy.data.objects['TD_ShieldPreview']
    rim = [shield.data.vertices[i].co.copy() for i in range(0, len(shield.data.vertices), 3)]
    heel_local = {s: rest[f'foot_{s}'].inverted() @ Vector((rest[f'foot_{s}'].translation.x, rest[f'foot_{s}'].translation.y + 4.0, 0.6)) for s in 'lr'}
    frames = []
    for f in range(first, last + 1):
        scene.frame_set(f)
        h, m = heads()
        rec = {'f': f, 'root': [round(v * 100, 3) for v in rig.matrix_world.translation], 'h': h, 'm': m}
        for s in 'lr':
            rec[f'ball_{s}'] = h[f'ball_{s}']
            rec[f'heel_{s}'] = m[f'foot_{s}'] @ heel_local[s] + rig.matrix_world.translation * 100
        inv = rig.matrix_world.inverted()
        if armed:
            tip = (inv @ sword_tip.matrix_world.translation)
            grip = (inv @ sword_grip.matrix_world.translation)
            blade = [grip.lerp(tip, k / 9.0) for k in range(10)]
            rec['tip_z'] = tip.z
            rec['sword_clear'] = clearance(blade[2:], h, {'lowerarm_r', 'upperarm_r'})
            mw = inv @ shield.matrix_world
            rec['shield_clear'] = clearance([mw @ v for v in rim], h, {'lowerarm_l', 'upperarm_l'})
        frames.append(rec)
    contact = {}
    for key in ('ball_l', 'ball_r', 'heel_l', 'heel_r'):
        slips, in_contact = [], 0
        for a, b in zip(frames, frames[1:]):
            if a[key].z < 2.0 and b[key].z < 2.0:
                in_contact += 1
                d = b[key] - a[key]
                slips.append(math.hypot(d.x, d.y))
        z = [r[key].z for r in frames]
        xy0 = frames[0][key]
        contact[key] = {'contact_frame_pairs': in_contact, 'max_slip_cm_per_frame': round(max(slips) if slips else 0.0, 4), 'total_slip_cm': round(sum(slips), 4),
                        'max_xy_drift_cm': round(max(math.hypot(r[key].x - xy0.x, r[key].y - xy0.y) for r in frames), 3), 'z_range_cm': [round(min(z), 2), round(max(z), 2)]}
    bone_len = 0.0
    scale_dev = 0.0
    for r in frames:
        for n, p in parents.items():
            if p is None or n.startswith('ik_') or n in ('interaction', 'center_of_mass'):
                continue
            want = (rest[n].translation - rest[p].translation).length
            bone_len = max(bone_len, abs((r['h'][n] - r['h'][p]).length - want))
        for b in rig.pose.bones:
            pass
    for f in (first, last):
        scene.frame_set(f)
        for b in rig.pose.bones:
            scale_dev = max(scale_dev, max(abs(v - 1.0) for v in b.matrix.to_scale()))
    skip = {n for n in r_names if n.startswith('ik_') or n in ('interaction', 'center_of_mass')}
    fingers = {n for n in r_names if any(n.startswith(k) for k in ('thumb_', 'index_', 'middle_', 'ring_', 'pinky_'))}
    def pose_diff(r):
        names = [n for n in r['h'] if n not in skip and n not in fingers]
        pos = max((r['h'][n] - idle_h[n]).length for n in names)
        rot = 0.0
        for n in names:
            p = parents[n]
            la = (r['m'][p].inverted() @ r['m'][n]) if p else r['m'][n]
            lb = (idle_m[p].inverted() @ idle_m[n]) if p else idle_m[n]
            rot = max(rot, angle_deg(la.to_quaternion(), lb.to_quaternion()))
        return round(pos, 3), round(rot, 3)
    heel_detail = {s: [[r['f'], round(r[f'heel_{s}'].x - frames[0][f'heel_{s}'].x, 2), round(r[f'heel_{s}'].y - frames[0][f'heel_{s}'].y, 2), round(r[f'heel_{s}'].z, 2)] for r in frames[::3]] for s in 'lr'}
    elbow = {}
    for s in 'lr':
        flex = []
        for r in frames:
            S, E, W = r['h'][f'upperarm_{s}'], r['h'][f'lowerarm_{s}'], r['h'][f'hand_{s}']
            flex.append(math.degrees((E - S).angle(W - E)))
        hand_speed = [angle_deg(a['m'][f'hand_{s}'].to_quaternion(), b['m'][f'hand_{s}'].to_quaternion()) for a, b in zip(frames, frames[1:])]
        wrist_speed = [(b['h'][f'hand_{s}'] - a['h'][f'hand_{s}']).length for a, b in zip(frames, frames[1:])]
        accel = [abs(wrist_speed[i + 1] - wrist_speed[i]) for i in range(len(wrist_speed) - 1)]
        elbow[s] = {'flex_deg_range': [round(min(flex), 1), round(max(flex), 1)], 'max_flex_change_deg_per_frame': round(max(abs(b - a) for a, b in zip(flex, flex[1:])), 2),
                    'max_hand_rot_deg_per_frame': round(max(hand_speed), 2), 'max_wrist_speed_cm_per_frame': round(max(wrist_speed), 2), 'max_wrist_speed_jump_cm_per_frame2': round(max(accel), 2),
                    'wrist_travel_from_start_max_cm': round(max((r['h'][f'hand_{s}'] - frames[0]['h'][f'hand_{s}']).length for r in frames), 1)}
    head_disp = [round((r['h']['head'] - frames[0]['h']['head']).length, 1) for r in frames]
    pelvis_disp = [[round(v, 1) for v in (r['h']['pelvis'] - frames[0]['h']['pelvis'])] for r in frames]
    report = {'action': action_name, 'frames': [first, last], 'fps': scene.render.fps, 'duration_s': round((last - first) / 60.0, 4),
              'root_max_cm': round(max(Vector(r['root']).length for r in frames), 4), 'contacts': contact,
              'max_bone_length_error_cm': round(bone_len, 5), 'max_scale_deviation_start_end': round(scale_dev, 6),
              'start_vs_idle0': dict(zip(('max_bone_pos_cm', 'max_local_rot_deg'), pose_diff(frames[0]))),
              'end_vs_idle0': dict(zip(('max_bone_pos_cm', 'max_local_rot_deg'), pose_diff(frames[-1]))),
              'elbow_wrist': elbow, 'heel_detail_every3': heel_detail, 'start_end_compare_excludes': 'ik_* bones, interaction, center_of_mass, finger bones (grip held constant)', 'head_disp_cm': head_disp, 'pelvis_disp_cm_every4': pelvis_disp[::4],
              'peak_head_frame': first + head_disp.index(max(head_disp))}
    if armed:
        sc = [r['sword_clear'] for r in frames]
        sh = [r['shield_clear'] for r in frames]
        w = min(sc, key=lambda v: v[0])
        ws = min(sh, key=lambda v: v[0])
        report['sword'] = {'min_tip_z_cm': round(min(r['tip_z'] for r in frames), 1), 'min_blade_clearance_cm': round(w[0], 2), 'nearest_capsule': w[1], 'frame': first + sc.index(w)}
        report['shield'] = {'min_rim_clearance_cm': round(ws[0], 2), 'nearest_capsule': ws[1], 'frame': first + sh.index(ws)}
    return report


def ensure_camera(name, location, rotation, scale):
    cam = bpy.data.objects.get(name)
    if cam is None:
        cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
        scene.collection.objects.link(cam)
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = scale
    cam.location = location
    cam.rotation_euler = rotation
    return cam


def render_sheet(action_name, out_name, armed, frames):
    rig.animation_data.action = bpy.data.actions[action_name]
    for nm in ('TD_SwordPreview', 'TD_ShieldPreview'):
        o = bpy.data.objects.get(nm)
        if o:
            o.hide_render = not armed
    try:
        scene.render.engine = 'BLENDER_WORKBENCH'
    except TypeError:
        pass
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'OBJECT'
    scene.display.shading.show_shadows = False
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 300, 420, 100
    scene.render.image_settings.file_format = 'PNG'
    views = [('front', ensure_camera('TD_HitCamFront', (0.0, -4.0, 1.0), (math.radians(90), 0, 0), 2.3)),
             ('side', ensure_camera('TD_HitCamSide', (-4.0, -0.05, 1.0), (math.radians(90), 0, math.radians(-90)), 2.3))]
    tiles = []
    tmp = OUT / 'render_tmp'
    tmp.mkdir(exist_ok=True)
    for vname, cam in views:
        row = []
        scene.camera = cam
        for f in frames:
            scene.frame_set(f)
            path = tmp / f'{out_name}_{vname}_{f:03d}.png'
            scene.render.filepath = str(path)
            bpy.ops.render.render(write_still=True)
            img = bpy.data.images.load(str(path), check_existing=False)
            arr = np.array(img.pixels[:], dtype=np.float32).reshape(img.size[1], img.size[0], 4)
            bpy.data.images.remove(img)
            row.append(arr)
        tiles.append(row)
    h, w = tiles[0][0].shape[:2]
    sheet = np.ones((h * len(tiles), w * len(frames), 4), dtype=np.float32)
    for r, row in enumerate(tiles):
        for c, arr in enumerate(row):
            y0 = (len(tiles) - 1 - r) * h
            sheet[y0:y0 + h, c * w:(c + 1) * w] = arr
            sheet[y0:y0 + h, c * w:c * w + 2] = (1, 1, 1, 1)
    img = bpy.data.images.new(f'{out_name}_sheet', sheet.shape[1], sheet.shape[0], alpha=True)
    img.pixels = sheet.ravel()
    img.filepath_raw = str(OUT / f'{out_name}-sheet.png')
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)
    return str(OUT / f'{out_name}-sheet.png')


summary = {}
for action_name, out_name, armed in JOBS:
    rep = measure(action_name, armed)
    first, last = rep['frames']
    frames = SHEET_FRAMES or sorted({first, min(last, first + 4), min(last, rep['peak_head_frame']), min(last, first + int((last - first) * 0.55)), min(last, first + int((last - first) * 0.8)), last})
    rep['sheet_frames'] = frames
    if DO_RENDER:
        rep['sheet'] = render_sheet(action_name, out_name, armed, frames)
    (OUT / f'{out_name}-measure.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
    summary[out_name] = {k: rep[k] for k in ('duration_s', 'root_max_cm', 'max_bone_length_error_cm', 'start_vs_idle0', 'end_vs_idle0', 'peak_head_frame', 'sheet_frames') if k in rep}
    summary[out_name]['slip'] = {k: (v['max_slip_cm_per_frame'], v['contact_frame_pairs'], v['max_xy_drift_cm']) for k, v in rep['contacts'].items()}
    summary[out_name]['elbow'] = {k: (v['max_flex_change_deg_per_frame'], v['max_hand_rot_deg_per_frame'], v['max_wrist_speed_jump_cm_per_frame2'], v['wrist_travel_from_start_max_cm']) for k, v in rep['elbow_wrist'].items()}
    if armed:
        summary[out_name]['sword'] = rep['sword']
        summary[out_name]['shield'] = rep['shield']
print(json.dumps(summary))
