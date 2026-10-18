"""Blender MCP 안에서 실행: 내보낸 FBX를 다시 불러와 원본 Action과 본 위치 오차를 비교한다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/CombatHit/roundtrip.py [TD_이름=<파이썬 리터럴> ...]
출력: 표준 출력 최대 오차(cm)
상태: 현행 (2026-10-18, M3-27)
"""
import json
import math
from pathlib import Path

import bpy

scene = bpy.context.scene
JOBS = TD_JOBS
report = {}
for fbx, action_name, arm_name, frames in JOBS:
    ref = bpy.data.objects[arm_name]
    ref.animation_data.action = bpy.data.actions[action_name]
    dummy = bpy.data.objects.new('TD_ContextDummy', None)
    scene.collection.objects.link(dummy)
    bpy.context.view_layer.objects.active = dummy
    before = set(scene.objects)
    bpy.ops.import_scene.fbx(filepath=fbx, use_anim=True)
    fps = scene.render.fps / scene.render.fps_base
    bpy.data.objects.remove(dummy, do_unlink=True)
    created = [o for o in scene.objects if o not in before]
    imp = next(o for o in created if o.type == 'ARMATURE')
    act = imp.animation_data.action
    rng = [round(v, 2) for v in act.frame_range]
    offset = int(round(act.frame_range[0] - bpy.data.actions[action_name].frame_range[0]))
    imp.animation_data.action = act
    worst = 0.0
    names = [b.name for b in ref.pose.bones if b.name in imp.pose.bones]
    for f in frames:
        scene.frame_set(f)
        a = {n: ref.matrix_world @ ref.pose.bones[n].head for n in names}
        scene.frame_set(f + offset)
        b = {n: imp.matrix_world @ imp.pose.bones[n].head for n in names}
        worst = max(worst, max((a[n] - b[n]).length for n in names) * 100)
    report[Path(fbx).name] = {'imported_root_object': imp.name, 'bones': len(imp.data.bones), 'matched_bones': len(names), 'fps': fps, 'frame_range': rng, 'frame_offset_vs_blender_action': offset, 'max_world_head_error_cm': round(worst, 4)}
    for o in created:
        if o.name in scene.objects:
            bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.actions.remove(act)
    scene.render.fps, scene.render.fps_base = 60, 1.0
print(json.dumps(report))
