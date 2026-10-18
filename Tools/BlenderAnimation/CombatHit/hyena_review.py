"""Blender MCP 안에서 실행: 하이에나 피격의 네 발 미끄러짐·끝 포즈 일치를 재고 정면·측면 시트를 렌더한다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/CombatHit/hyena_review.py [TD_이름=<파이썬 리터럴> ...]
출력: AS_TD_Hyena_Hit-measure.json·-sheet.png
상태: 현행 (2026-10-18, M3-27)
"""
import json
import math
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector

OUT = Path('C:/Project/TDGame/Saved/AgentOps/20261018/combat-full/blender')
scene = bpy.context.scene
arm = bpy.data.objects['RL_BoneRoot']
rest = {b.name: b.matrix_local.copy() for b in arm.data.bones}
parents = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
kids = {b.name: [c.name for c in b.children] for b in arm.data.bones}
SKIP = ('s_fx', 'Armature', 'bip01_l_clavicle', 'bip01_r_clavicle')


def radius(n):
    if 'ponytail' in n:
        return 3.0
    if n.startswith('bone0') and n != 'bone08':
        return 4.5 if n in ('bone01', 'bone02') else 3.0
    if n == 'bone08':
        return 5.0
    if any(k in n for k in ('spine', 'pelvis')):
        return 16.0
    if 'neck' in n:
        return 10.0
    if 'head' in n:
        return 9.0
    if any(k in n for k in ('thigh', 'upperarm')):
        return 6.0
    return 3.2


def build_proxy():
    for o in [o for o in bpy.data.objects if o.name.startswith('TD_HyenaProxy_')]:
        bpy.data.objects.remove(o, do_unlink=True)
    mat_color = (0.62, 0.55, 0.45, 1)
    for b in arm.data.bones:
        if b.name.startswith(SKIP) or b.name == 'bip01' or not kids[b.name]:
            continue
        child = kids[b.name][0]
        if b.name == 'bip01_pelvis':
            child = 'bip01_spine'
        if b.name == 'bip01_spine2':
            child = 'bip01_neck'
        if b.name == 'bip01_head':
            child = 'bone08'
        if child.endswith('_end') and ('finger' in b.name or 'toe' in b.name):
            continue
        hinv = rest[b.name].inverted()
        a = Vector((0, 0, 0))
        c = hinv @ rest[child].translation
        length = (c - a).length
        if length < 1.0:
            continue
        mesh = bpy.data.meshes.new(f'TD_HyenaProxy_{b.name}')
        import bmesh
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=radius(b.name), radius2=radius(b.name) * 0.8, depth=length)
        bm.to_mesh(mesh)
        bm.free()
        obj = bpy.data.objects.new(mesh.name, mesh)
        scene.collection.objects.link(obj)
        obj.parent = arm
        obj.parent_type = 'BONE'
        obj.parent_bone = b.name
        mid = (a + c) * 0.5
        rot = Vector((0, 0, 1)).rotation_difference((c - a).normalized()).to_matrix().to_4x4()
        obj.matrix_parent_inverse = Matrix.Identity(4)
        obj.matrix_basis = Matrix.Translation((0.0, -b.length, 0.0)) @ Matrix.Translation(mid) @ rot
        obj.color = mat_color
    if not bpy.data.objects.get('TD_ReviewFloor'):
        bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, -0.002))
        fl = bpy.context.active_object
        fl.name = 'TD_ReviewFloor'
        fl.color = (0.25, 0.27, 0.3, 1)


def heads():
    off = arm.matrix_world.translation * 100
    return {b.name: b.head + off for b in arm.pose.bones}, {b.name: b.matrix.copy() for b in arm.pose.bones}


def measure(action_name):
    arm.animation_data.action = bpy.data.actions['TD_Src_Hyena_Idle']
    scene.frame_set(1)
    idle_h, idle_m = heads()
    act = bpy.data.actions[action_name]
    arm.animation_data.action = act
    first, last = [int(round(v)) for v in act.frame_range]
    frames = []
    for f in range(first, last + 1):
        scene.frame_set(f)
        h, m = heads()
        frames.append({'f': f, 'h': h, 'm': m, 'root_obj': arm.matrix_world.translation * 100})
    contacts = {}
    for key in ('bip01_l_finger0', 'bip01_r_finger0', 'bip01_l_hand', 'bip01_r_hand', 'bip01_l_toe0', 'bip01_r_toe0', 'bip01_l_foot', 'bip01_r_foot'):
        slips = [math.hypot(*(b['h'][key] - a['h'][key]).xy) for a, b in zip(frames, frames[1:])]
        dz = [abs(r['h'][key].z - frames[0]['h'][key].z) for r in frames]
        contacts[key] = {'z_cm': round(frames[0]['h'][key].z, 2), 'max_slip_cm_per_frame': round(max(slips), 4), 'max_xy_drift_cm': round(max(math.hypot(*(r['h'][key] - frames[0]['h'][key]).xy) for r in frames), 4), 'max_z_change_cm': round(max(dz), 4)}
    blen, blen_bone = 0.0, None
    for r in frames:
        for n, p in parents.items():
            if p is None:
                continue
            want = (idle_h[n] - idle_h[p]).length
            err = abs((r['h'][n] - r['h'][p]).length - want)
            if err > blen:
                blen, blen_bone = err, n
    def diff(r):
        pos = max((r['h'][n] - idle_h[n]).length for n in r['h'])
        rot = 0.0
        for n in r['m']:
            p = parents[n]
            la = (r['m'][p].inverted() @ r['m'][n]) if p else r['m'][n]
            lb = (idle_m[p].inverted() @ idle_m[n]) if p else idle_m[n]
            d = abs(la.to_quaternion().normalized().dot(lb.to_quaternion().normalized()))
            rot = max(rot, math.degrees(2 * math.acos(min(1.0, d))))
        return {'max_bone_pos_cm': round(pos, 4), 'max_local_rot_deg': round(rot, 4)}
    rel = lambda r, n: [round(v, 1) for v in (r['h'][n] - frames[0]['h'][n])]
    speeds = {}
    for n in ('bip01_head', 'bone08', 'bone06_end', 'bip01_l_forearm', 'bip01_l_calf'):
        sp = [(b['h'][n] - a['h'][n]).length for a, b in zip(frames, frames[1:])]
        speeds[n] = {'max_cm_per_frame': round(max(sp), 2), 'max_jump_cm_per_frame2': round(max(abs(sp[i + 1] - sp[i]) for i in range(len(sp) - 1)), 2)}
    tail_min = min(min(r['h'][n].z for n in ('bone03', 'bone04', 'bone05', 'bone06', 'bone06_end')) for r in frames)
    return {'action': action_name, 'frames': [first, last], 'fps': scene.render.fps, 'duration_s': round((last - first) / 60.0, 4),
            'root_object_max_cm': round(max(r['root_obj'].length for r in frames), 4), 'root_bone_Armature_max_cm': round(max((r['h']['Armature'] - frames[0]['h']['Armature']).length for r in frames), 4),
            'contacts': contacts, 'max_bone_length_error_cm': round(blen, 5), 'max_bone_length_error_bone': blen_bone, 'bone_length_reference': 'parent-child head distance of AS_TD_Hyena_Idle frame 0 (the source animates some translations, e.g. bip01 under Armature)', 'start_vs_idle0': diff(frames[0]), 'end_vs_idle0': diff(frames[-1]),
            'tail_min_z_cm': round(tail_min, 1), 'joint_speed': speeds,
            'disp_every3': {n: [[r['f']] + rel(r, n) for r in frames[::3]] for n in ('bip01_pelvis', 'bip01_head', 'bone08', 'bone06_end', 'bip01_ponytail1_end')}}


def render(action_name, out_name, frames):
    build_proxy()
    arm.animation_data.action = bpy.data.actions[action_name]
    try:
        scene.render.engine = 'BLENDER_WORKBENCH'
    except TypeError:
        pass
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'OBJECT'
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 340, 260, 100
    if scene.world is None:
        scene.world = bpy.data.worlds.new('TD_ReviewWorld')
    scene.world.color = (0.28, 0.3, 0.34)
    scene.render.image_settings.file_format = 'PNG'
    cams = []
    for name, loc, rot in (('TD_HyCamFront', (0.0, -5.0, 0.62), (math.radians(90), 0, 0)), ('TD_HyCamSide', (-5.0, globals().get('TD_SIDE_Y', 0.42), 0.62), (math.radians(90), 0, math.radians(-90)))):
        cam = bpy.data.objects.get(name)
        if cam is None:
            cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
            scene.collection.objects.link(cam)
        cam.data.type = 'ORTHO'
        cam.data.ortho_scale = globals().get('TD_SIDE_SCALE', 3.5) if 'Side' in name else 1.9
        cam.location, cam.rotation_euler = loc, rot
        cams.append(cam)
    tmp = OUT / 'render_tmp'
    tmp.mkdir(exist_ok=True)
    rows = []
    for cam in cams:
        scene.camera = cam
        row = []
        for f in frames:
            scene.frame_set(f)
            path = tmp / f'{out_name}_{cam.name}_{f:03d}.png'
            scene.render.filepath = str(path)
            bpy.ops.render.render(write_still=True)
            img = bpy.data.images.load(str(path), check_existing=False)
            row.append(np.array(img.pixels[:], dtype=np.float32).reshape(img.size[1], img.size[0], 4))
            bpy.data.images.remove(img)
        rows.append(row)
    h, w = rows[0][0].shape[:2]
    sheet = np.ones((h * len(rows), w * len(frames), 4), dtype=np.float32)
    for r, row in enumerate(rows):
        for c, arr in enumerate(row):
            y0 = (len(rows) - 1 - r) * h
            sheet[y0:y0 + h, c * w:(c + 1) * w] = arr
            sheet[y0:y0 + h, c * w:c * w + 2] = 1.0
    img = bpy.data.images.new(out_name + '_sheet', sheet.shape[1], sheet.shape[0], alpha=True)
    img.pixels = sheet.ravel()
    img.filepath_raw = str(OUT / f'{out_name}-sheet.png')
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


rep = measure('TD_Hyena_Hit')
frames = globals().get('TD_SHEET_FRAMES') or [1, 5, 8, 12, 18, 28]
rep['sheet_frames'] = frames
if globals().get('TD_RENDER', True):
    render('TD_Hyena_Hit', globals().get('TD_SHEET_NAME', 'AS_TD_Hyena_Hit'), frames)
    rep['sheet'] = str(OUT / f"{globals().get('TD_SHEET_NAME', 'AS_TD_Hyena_Hit')}-sheet.png")
(OUT / 'AS_TD_Hyena_Hit-measure.json').write_text(json.dumps(rep, indent=1), encoding='utf-8')
print(json.dumps({k: rep[k] for k in ('root_object_max_cm', 'root_bone_Armature_max_cm', 'max_bone_length_error_cm', 'start_vs_idle0', 'end_vs_idle0', 'tail_min_z_cm', 'joint_speed')}), json.dumps({k: (v['max_slip_cm_per_frame'], v['z_cm']) for k, v in rep['contacts'].items()}))
