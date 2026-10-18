"""Blender MCP 안에서 실행: 하이에나 피격을 FBX·blend·json으로 내보낸다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/CombatHit/hyena_export.py [TD_이름=<파이썬 리터럴> ...]
출력: AnimationSources/Monster/AS_TD_Hyena_Hit.(fbx,blend,json)
상태: 현행 (2026-10-18, M3-27)
"""
import json
from pathlib import Path

import bpy

ROOT = Path('C:/Project/TDGame')
OUT = ROOT / 'Saved/AgentOps/20261018/combat-full/blender'
FOLDER = ROOT / 'AnimationSources/Monster'
NAME = 'AS_TD_Hyena_Hit'
scene = bpy.context.scene
arm = bpy.data.objects['RL_BoneRoot']
action = bpy.data.actions['TD_Hyena_Hit']
arm.animation_data.action = action
first, last = [int(round(v)) for v in action.frame_range]
scene.frame_start, scene.frame_end = first, last
scene.render.fps, scene.render.fps_base = 60, 1.0
scene.frame_set(first)
FOLDER.mkdir(parents=True, exist_ok=True)
for obj in bpy.context.view_layer.objects:
    obj.select_set(False)
arm.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.export_scene.fbx(filepath=str(FOLDER / f'{NAME}.fbx'), use_selection=True, object_types={'ARMATURE'},
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_bones=True,
                         bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                         bake_anim_step=1, bake_anim_simplify_factor=0, axis_forward='-Z', axis_up='Y')
keep = {action, bpy.data.actions['TD_Src_Hyena_Idle']}
bpy.data.libraries.write(str(FOLDER / f'{NAME}.blend'), {scene} | keep, path_remap='RELATIVE_ALL', fake_user=True, compress=True)
author = json.loads((OUT / 'hyena-author-result.json').read_text(encoding='utf-8'))
measure = json.loads((OUT / f'{NAME}-measure.json').read_text(encoding='utf-8'))
record = {
    'name': NAME, 'fps': 60, 'frames': [first, last], 'frame_count': last - first + 1, 'duration_s': round((last - first) / 60.0, 4),
    'skeleton': '/Game/Fab/Hyena_A1/Hyenas_A1_Skeleton (assumed from the pack; confirm it is the skeleton of /Game/MonsterAI/Animations/Hyena/AS_TD_Hyena_Idle)',
    'root_motion': False, 'in_place': True,
    'source': 'AS_TD_Hyena_Idle frame 0 (Saved/BlenderAnimation/Reference/CombatHit/AS_TD_Hyena_Idle.fbx, 60fps) as the base and end pose; no hyena hit clip exists in the Fab pack, so the reaction is layered on that pose',
    'edit': 'world-axis (lateral = +X, verified: forward -Y, left +X, up +Z from bone positions) rotations about bone heads with phase-lagged pchip curves: bip01 back 8 cm / down 3 cm / front-down 1.5 deg (peak f6), spine1 +1 / spine2 +2 deg, neck 10 / neck1 3 / head 10 deg down (peak f8), tail bone01-06 tuck down -17.5 deg total (peak f12), ears (ponytail1/2) back -30 deg; clavicles follow the chest; front paws (hand/finger0) and rear horselink/foot/toe0 held at idle world transforms with two-bone IK (upperarm-forearm, thigh-calf); ends exactly on frame 0',
    'parameters': globals().get('TD_HYENA'),
    'author_script': 'Tools/BlenderAnimation/CombatHit/hyena_author.py',
    'export': 'Blender FBX: armature RL_BoneRoot only, add_leaf_bones False, bake every frame, simplify 0, axis_forward -Z, axis_up Y (same as sword_attack01_export.py)',
    'unreal_import': {'sample_rate': 60, 'import_uniform_scale': 1.0, 'preserve_local_transform': True, 'enable_root_motion': False, 'force_root_lock': True},
    'author': author,
    'measure': {k: measure[k] for k in ('root_object_max_cm', 'root_bone_Armature_max_cm', 'contacts', 'max_bone_length_error_cm', 'max_bone_length_error_bone', 'start_vs_idle0', 'end_vs_idle0', 'tail_min_z_cm', 'joint_speed')},
    'note_bone_length': 'only bip01 (translating body root under Armature) changes its offset (0.98 cm max = the 8 cm push); every other parent-child distance equals idle frame 0',
}
(FOLDER / f'{NAME}.json').write_text(json.dumps(record, indent=1, ensure_ascii=False), encoding='utf-8')
print(json.dumps({'fbx_kb': round((FOLDER / f'{NAME}.fbx').stat().st_size / 1024), 'blend_kb': round((FOLDER / f'{NAME}.blend').stat().st_size / 1024), 'frames': [first, last]}))
