"""Blender 안에서 실행: 작업 씬의 TD_SwordAttack01 Action을 Manny 아마추어(root)만 골라 60fps FBX로 굽고, 원본 Action과 결과 Action만 남긴 편집 원본 .blend와 제작 기록 JSON을 저장한다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_export.py [TD_NAME="'AS_TD_Player_SwordAttack01'"]
출력: AnimationSources/Player/<이름>.fbx·.blend·.json (같은 이름이 있으면 덮어씀 — 이 도구가 만든 파일만 대상)
상태: 현행 (2026-09-25, FBX Simplify 0·리프 뼈 끔·축 -Z/Y는 이전 검증 파이프라인과 같음)
"""
import json
from pathlib import Path

import bpy

ROOT = Path('C:/Project/TDGame')
WORK = ROOT / 'Saved/BlenderAnimation/SwordAttack01'
OUTPUT = ROOT / 'AnimationSources/Player'
NAME = globals().get('TD_NAME', 'AS_TD_Player_SwordAttack01')
ACTION = globals().get('TD_ACTION', 'TD_SwordAttack01')
KEEP_ACTIONS = {ACTION, 'TD_Ref_Attack_PrimaryA'}

scene = bpy.context.scene
armature = bpy.data.objects['root']
action = bpy.data.actions[ACTION]
armature.animation_data.action = action
first, last = [int(round(value)) for value in action.frame_range]
scene.frame_start, scene.frame_end = first, last
scene.render.fps, scene.render.fps_base = 60, 1.0
OUTPUT.mkdir(parents=True, exist_ok=True)

bpy.ops.wm.save_as_mainfile(filepath=str(WORK / 'work.blend'))
bpy.ops.object.select_all(action='DESELECT')
armature.select_set(True)
bpy.context.view_layer.objects.active = armature
bpy.ops.export_scene.fbx(filepath=str(OUTPUT / f'{NAME}.fbx'), use_selection=True, object_types={'ARMATURE'},
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_bones=True,
                         bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                         bake_anim_step=1, bake_anim_simplify_factor=0, axis_forward='-Z', axis_up='Y')

for item in list(bpy.data.actions):
    if item.name not in KEEP_ACTIONS:
        bpy.data.actions.remove(item)
armature.animation_data.action = action
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT / f'{NAME}.blend'), copy=True)
bpy.ops.wm.open_mainfile(filepath=str(WORK / 'work.blend'))

record = {
    'name': NAME, 'fps': 60, 'frames': [first, last], 'duration_s': round((last - first) / 60.0, 4),
    'source': 'Paragon Greystone Attack_PrimaryA (Epic, retargeted to UE5 Manny in C:/Project/PJGame), edited by Tools/BlenderAnimation/sword_attack01_author.py',
    'author': json.loads((WORK / 'author-result.json').read_text(encoding='utf-8'))['summary'],
    'measure': json.loads((WORK / f'measure_{ACTION}.json').read_text(encoding='utf-8'))['summary'],
    'weapon': {'socket': 'HandGrip_R', 'static_mesh': '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword', 'relative_location_cm': [0.0, 32.2, -1.4]},
}
(OUTPUT / f'{NAME}.json').write_text(json.dumps(record, indent=1, ensure_ascii=False), encoding='utf-8')
print(json.dumps({'fbx': str(OUTPUT / f'{NAME}.fbx'), 'blend': str(OUTPUT / f'{NAME}.blend'), 'frames': [first, last]}))
