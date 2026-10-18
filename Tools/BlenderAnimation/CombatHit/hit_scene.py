"""Blender MCP 안에서 실행: CombatHit 참고 FBX(MM_HitReact 5종·MM_Idle, Saved/BlenderAnimation/Reference/CombatHit)를 칼·방패 콤보 장면의 Manny 리그에 Action으로 불러온다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/CombatHit/hit_scene.py [TD_이름=<파이썬 리터럴> ...]
출력: Blender 장면(작업 파일 Saved/AgentOps/20261018/combat-full/blender/hit_work.blend)
상태: 현행 (2026-10-18, M3-27)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT = Path('C:/Project/TDGame')
OUT = ROOT / 'Saved/AgentOps/20261018/combat-full/blender'
REF = ROOT / 'Saved/BlenderAnimation/Reference/CombatHit'
SOURCES = ['MM_Idle', 'MM_HitReact_Front_Lgt_01', 'MM_HitReact_Front_Lgt_02', 'MM_HitReact_Front_Hvy_01', 'MM_HitReact_Front_Med_01', 'MM_HitReact_Back_Med_01']
FPS = 60

scene = bpy.context.scene
for obj in list(scene.objects):
    if obj.name.startswith('TD_TipTrail'):
        bpy.data.objects.remove(obj, do_unlink=True)
rig = bpy.data.objects['root']
rig.animation_data.action = bpy.data.actions['TD_SwordCombo01']
scene.frame_set(1)
guard = {b.name: [list(r) for r in b.matrix] for b in rig.pose.bones}
(OUT / 'guard_pose.json').write_text(json.dumps(guard), encoding='utf-8')
rig.location = (0, 0, 0)


def import_fbx(path):
    before = set(scene.objects)
    dummy = bpy.data.objects.new('TD_ContextDummy', None)
    scene.collection.objects.link(dummy)
    bpy.context.view_layer.objects.active = dummy
    bpy.ops.import_scene.fbx(filepath=str(path), use_anim=True)
    bpy.data.objects.remove(dummy, do_unlink=True)
    fps = scene.render.fps / scene.render.fps_base
    scene.render.fps, scene.render.fps_base = FPS, 1.0
    created = [o for o in scene.objects if o not in before]
    arm = next(o for o in created if o.type == 'ARMATURE')
    parent = arm.parent
    world = arm.matrix_world.copy()
    arm.parent = None
    arm.matrix_world = world
    return arm, created, fps, parent


def bake(rig, ref, name, fps):
    act = ref.animation_data.action
    first, last = [float(v) for v in act.frame_range]
    step = fps / FPS
    count = int(round((last - first) / step)) + 1
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    rig.animation_data.action = action
    rest = {b.name: b.matrix_local.copy() for b in rig.data.bones}
    names = [b.name for b in rig.pose.bones if b.name in ref.pose.bones]
    rig_inv = rig.matrix_world.inverted()
    stats = {'root_obj': [], 'pelvis': [], 'foot_l': [], 'foot_r': [], 'ball_l': [], 'ball_r': [], 'hand_l': [], 'hand_r': [], 'head': [], 'spine_05': []}
    for i in range(count):
        f = first + i * step
        scene.frame_set(int(math.floor(f)), subframe=f - math.floor(f))
        targets = {b.name: rig_inv @ ref.matrix_world @ b.matrix for b in ref.pose.bones}
        stats['root_obj'].append([round(v * 100, 3) for v in ref.matrix_world.translation])
        for k in stats:
            if k != 'root_obj':
                stats[k].append([round(v, 3) for v in targets[k].translation])
        for n in names:
            pb = rig.pose.bones[n]
            p = pb.parent
            if p is None:
                basis = rest[n].inverted() @ targets[n]
            else:
                basis = (targets.get(p.name, rest[p.name]) @ rest[p.name].inverted() @ rest[n]).inverted() @ targets[n]
            loc, rot, sc = basis.decompose()
            pb.location, pb.rotation_quaternion, pb.scale = loc, rot, sc
            for path in ('location', 'rotation_quaternion', 'scale'):
                pb.keyframe_insert(path, frame=i + 1, group=n)
    return count, stats, [first, last]


report = {}
for src in SOURCES:
    ref, created, fps, parent = import_fbx(REF / f'{src}.fbx')
    count, stats, rng = bake(rig, ref, f'TD_Src_{src}', fps)
    info = {'fps': fps, 'source_range': rng, 'frames60': count, 'dur_s': round((count - 1) / FPS, 3), 'ref_scale': list(ref.scale), 'parent': parent.name if parent else None, 'missing_bones': [b.name for b in rig.pose.bones if b.name not in ref.pose.bones][:10]}
    def drift(key):
        pts = [Vector(p) for p in stats[key]]
        return round(max((p - pts[0]).length for p in pts), 2)
    info['max_drift_cm'] = {k: drift(k) for k in ('pelvis', 'foot_l', 'foot_r', 'ball_l', 'ball_r', 'hand_l', 'head')}
    info['root_obj_first_last'] = [stats['root_obj'][0], stats['root_obj'][-1]]
    sample = list(range(0, count, max(1, count // 12))) + [count - 1]
    info['pelvis_rel'] = [[i] + [round(a - b, 1) for a, b in zip(stats['pelvis'][i], stats['pelvis'][0])] for i in sample]
    info['head_rel'] = [[i] + [round(a - b, 1) for a, b in zip(stats['head'][i], stats['head'][0])] for i in sample]
    info['foot_l_rel'] = [[i] + [round(a - b, 1) for a, b in zip(stats['ball_l'][i], stats['ball_l'][0])] for i in sample]
    info['foot_r_rel'] = [[i] + [round(a - b, 1) for a, b in zip(stats['ball_r'][i], stats['ball_r'][0])] for i in sample]
    info['first'] = {k: stats[k][0] for k in ('pelvis', 'foot_l', 'foot_r', 'ball_l', 'ball_r', 'hand_l', 'hand_r', 'head')}
    vel = []
    for i in range(1, count):
        vel.append(round((Vector(stats['head'][i]) - Vector(stats['head'][i - 1])).length, 2))
    info['head_speed_cm_f'] = vel
    report[src] = info
    for o in created:
        if o.name in scene.objects:
            bpy.data.objects.remove(o, do_unlink=True)
    if parent is not None and parent.name in scene.objects:
        bpy.data.objects.remove(parent, do_unlink=True)
    act = bpy.data.actions.get(ref.name) if False else None
for a in list(bpy.data.actions):
    if not (a.name.startswith('TD_Src_') or a.name in ('TD_SwordCombo01', 'TD_Ref_Attack_PrimaryA')):
        bpy.data.actions.remove(a)
rig.animation_data.action = bpy.data.actions['TD_Src_MM_Idle']
scene.render.fps, scene.render.fps_base = FPS, 1.0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'hit_work.blend'))
(OUT / 'source-analysis.json').write_text(json.dumps(report, indent=1), encoding='utf-8')
print(json.dumps({k: {kk: v[kk] for kk in ('fps', 'frames60', 'dur_s', 'max_drift_cm', 'root_obj_first_last', 'parent', 'missing_bones')} for k, v in report.items()}))
