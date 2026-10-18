"""Blender MCP 안에서 실행: 저작한 Manny 피격 Action을 언리얼 재가져오기 규약 FBX·blend·json으로 내보낸다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/CombatHit/hit_export.py [TD_이름=<파이썬 리터럴> ...]
출력: AnimationSources/Player/AS_TD_Player_HitReact_*.(fbx,blend,json), AnimationSources/Monster/AS_TD_Manny_HitReact_Unarmed.*
상태: 현행 (2026-10-18, M3-27)
"""
import json
from pathlib import Path

import bpy

ROOT = Path('C:/Project/TDGame')
OUT = ROOT / 'Saved/AgentOps/20261018/combat-full/blender'
scene = bpy.context.scene
rig = bpy.data.objects['root']
author = {r['action']: r for r in json.loads((OUT / 'hit-author-result.json').read_text(encoding='utf-8'))}
SKELETON = '/Game/Characters/Mannequins/Meshes/SK_Mannequin'
JOBS = [
    ('TD_HitReact_Light', 'AS_TD_Player_HitReact_Light', ROOT / 'AnimationSources/Player', True, 'MM_HitReact_Front_Lgt_01'),
    ('TD_HitReact_Heavy', 'AS_TD_Player_HitReact_Heavy', ROOT / 'AnimationSources/Player', True, 'MM_HitReact_Front_Hvy_01'),
    ('TD_Manny_HitReact_Unarmed', 'AS_TD_Manny_HitReact_Unarmed', ROOT / 'AnimationSources/Monster', False, 'MM_HitReact_Front_Med_01'),
]
WEAPON_OBJECTS = ['TD_SwordPreview', 'TD_SwordTip', 'TD_HandGrip_R', 'TD_ShieldPreview', 'TD_ShieldSocket_L']
HOW = {
    True: 'body (pelvis/spine/neck/head/legs) = MM_Idle frame0 + source local deltas (parent-space), feet = idle stance + source foot deltas with ball XY locked and heel lift while swivelling, arms = MM_Idle arms blended (local slerp) into a chest-frame IK sword-and-shield guard (L-anim-05 fist grip, shield socket of AS_TD_Player_SwordCombo01) plus shoulder-acceleration spring lag and elbow whip; fingers held in the combo grip pose; tail faded to MM_Idle frame0',
    False: 'body (pelvis/spine/neck/head/legs) = MM_Idle frame0 + source local deltas (parent-space), feet = idle stance + source foot deltas with ball XY locked, arms = MM_Idle hanging arms driven by a shoulder-acceleration pendulum (lags, then swings back away from the impact) plus elbow whip and partial gravity hold; fingers = MM_Idle; tail faded to MM_Idle frame0',
}
results = []
for action_name, name, folder, armed, source in JOBS:
    folder.mkdir(parents=True, exist_ok=True)
    action = bpy.data.actions[action_name]
    rig.animation_data.action = action
    first, last = [int(round(v)) for v in action.frame_range]
    scene.frame_start, scene.frame_end = first, last
    scene.render.fps, scene.render.fps_base = 60, 1.0
    scene.frame_set(first)
    for obj in bpy.context.view_layer.objects:
        obj.select_set(False)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.fbx(filepath=str(folder / f'{name}.fbx'), use_selection=True, object_types={'ARMATURE'},
                             add_leaf_bones=False, bake_anim=True, bake_anim_use_all_bones=True,
                             bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                             bake_anim_step=1, bake_anim_simplify_factor=0, axis_forward='-Z', axis_up='Y')
    unlinked = []
    if not armed:
        for nm in WEAPON_OBJECTS:
            obj = bpy.data.objects.get(nm)
            if obj is None:
                continue
            for col in list(obj.users_collection):
                col.objects.unlink(obj)
                unlinked.append((col, obj))
    keep = {action, bpy.data.actions[f'TD_Src_{source}']}
    for a in keep:
        a.use_fake_user = True
    bpy.data.libraries.write(str(folder / f'{name}.blend'), {scene} | keep, path_remap='RELATIVE_ALL', fake_user=True, compress=True)
    for col, obj in unlinked:
        col.objects.link(obj)
    measure = json.loads((OUT / f'{name}-measure.json').read_text(encoding='utf-8'))
    record = {
        'name': name, 'fps': 60, 'frames': [first, last], 'frame_count': last - first + 1, 'duration_s': round((last - first) / 60.0, 4),
        'skeleton': SKELETON, 'root_motion': False, 'in_place': True,
        'source': f'/Game/Characters/Mannequins/Anims (Lyra/Manny) {source} (30fps, exported to Saved/BlenderAnimation/Reference/CombatHit/{source}.fbx), resampled to 60fps; used source 60fps frames {author[action_name]["source_frames60"]} (= {round((author[action_name]["source_frames60"][0]-1)/60.0,3)}..{round((author[action_name]["source_frames60"][1]-1)/60.0,3)} s), remaining settle tail dropped',
        'start_end_pose': 'MM_Idle frame 0 (/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle); runtime blend in 0.1 s / out 0.15 s',
        'edit': HOW[armed],
        'author_script': 'Tools/BlenderAnimation/CombatHit/hit_author.py (helpers exec-loaded from Tools/BlenderAnimation/blender_author_sword_shield_combo.py)',
        'export': 'Blender FBX: armature root only, add_leaf_bones False, bake every frame, simplify 0, axis_forward -Z, axis_up Y (same as sword_attack01_export.py)',
        'unreal_import': {'skeleton_path': SKELETON, 'sample_rate': 60, 'import_uniform_scale': 1.0, 'preserve_local_transform': True, 'enable_root_motion': False, 'force_root_lock': True},
        'measure': {k: measure[k] for k in ('root_max_cm', 'max_bone_length_error_cm', 'start_vs_idle0', 'end_vs_idle0', 'contacts', 'elbow_wrist', 'peak_head_frame') if k in measure},
    }
    if armed:
        record['weapons'] = {'sword': {'socket': 'HandGrip_R', 'static_mesh': '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword', 'relative_location_cm': [-5.052, 9.119, 25.857], 'relative_rotation_deg': {'roll': -83.734, 'pitch': -15.888, 'yaw': 94.203}, 'note': 'L-anim-05'},
                             'shield': {'bone': 'hand_l', 'static_mesh': '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Shield', 'attach': 'Saved/AgentOps/20261018/combat-full/blender/shield-attach.json'},
                             'measure_sword': measure.get('sword'), 'measure_shield_preview': measure.get('shield')}
    (folder / f'{name}.json').write_text(json.dumps(record, indent=1, ensure_ascii=False), encoding='utf-8')
    results.append({'name': name, 'fbx_kb': round((folder / f'{name}.fbx').stat().st_size / 1024), 'blend_kb': round((folder / f'{name}.blend').stat().st_size / 1024), 'frames': [first, last]})
print(json.dumps(results))
