import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

root = Path('C:/Project/TDGame')
name = 'AS_TD_Player_Attack01_Heavy_RToL_v04'
output = root / 'AnimationSources/Player'
if any((output / (name + extension)).exists() for extension in ['.blend', '.fbx']):
    raise FileExistsError('Use a new version name; existing results are preserved')
if any(obj.type == 'ARMATURE' for obj in bpy.data.objects):
    raise RuntimeError('Use an empty Blender document to preserve the original root name')
scene = bpy.context.scene
scene.name = 'TD_HeavySlash_v04'
bpy.ops.import_scene.fbx(filepath=str(root / 'Saved/BlenderAnimation/SKM_Manny_Simple.fbx'), use_anim=False)
armature = next(obj for obj in scene.objects if obj.type == 'ARMATURE')
world = armature.matrix_world.copy()
armature.parent = None
armature.matrix_world = world
if armature.name != 'root':
    raise ValueError('Original skeleton root name was not preserved')
bpy.ops.import_scene.fbx(filepath=str(root / 'Saved/BlenderAnimation/Reference/MM_Attack_01.fbx'), use_anim=True)
reference = next(obj for obj in scene.objects if obj.type == 'ARMATURE' and obj != armature)
reference.name = 'TD_Reference_Attack01'
reference.hide_render = True
reference.hide_set(True)
source_action = reference.animation_data.action
source_action.name = 'TD_Reference_MM_Attack_01'
source_action.use_fake_user = True
scene.render.fps = 30
scene.frame_start = 1
scene.frame_end = 44
rest = {bone.name: bone.matrix_local.copy() for bone in armature.data.bones}
time_map = [(0, 0), (10, 7), (14, 10), (19, 13), (25, 18), (35, 25), (40, 30)]
angle_keys = [(0, 20), (7, 120), (10, 100), (13, -45), (18, -85), (25, -30), (30, 20)]


def sample_linear(keys, time):
    for (start, a), (end, b) in zip(keys, keys[1:]):
        if start <= time <= end:
            return a + (b - a) * (time - start) / (end - start)
    return keys[-1][1]


base_samples = []
for frame in range(41):
    source_frame = 1 + sample_linear(time_map, frame)
    scene.frame_set(int(source_frame), subframe=source_frame % 1)
    bpy.context.view_layer.update()
    armature.matrix_world = reference.matrix_world.copy()
    for bone in armature.pose.bones:
        bone.matrix = reference.pose.bones[bone.name].matrix.copy()
        bpy.context.view_layer.update()
    base_samples.append({
        'source_frame': source_frame,
        'world': armature.matrix_world.copy(),
        'basis': {bone.name: bone.matrix_basis.copy() for bone in armature.pose.bones},
        'pose': {bone.name: bone.matrix.copy() for bone in armature.pose.bones},
    })
armature.animation_data_clear()
armature.animation_data_create()
action = bpy.data.actions.new(name)
armature.animation_data.action = action
armature.rotation_mode = 'QUATERNION'
for bone in armature.pose.bones:
    bone.rotation_mode = 'QUATERNION'
weapon_control = bpy.data.objects.new('TD_WeaponAim', None)
scene.collection.objects.link(weapon_control)
weapon_control.empty_display_type = 'ARROWS'
weapon_control.empty_display_size = 0.18
weapon_control.rotation_mode = 'QUATERNION'
weapon_control['purpose'] = 'Editable sword-direction control; right wrist rotation follows this object'
constraint = armature.pose.bones['hand_r'].constraints.new('COPY_ROTATION')
constraint.name = 'TD_WeaponAim_WorldRotation'
constraint.target = weapon_control
constraint.owner_space = 'WORLD'
constraint.target_space = 'WORLD'
constraint.mix_mode = 'REPLACE'
records = []
previous = {}
upper_bones = {bone.name for bone in armature.data.bones['spine_01'].children_recursive} | {'spine_01'}
for frame in range(44):
    base = base_samples[min(frame, 40)]
    upper_frame = max(0, min(frame - 3, 40))
    upper = base_samples[upper_frame]
    scene.frame_set(frame + 1)
    armature.matrix_world = base['world']
    constraint.mute = True
    for bone in armature.pose.bones:
        sample = upper if bone.name in upper_bones else base
        bone.matrix_basis = sample['basis'][bone.name]
    bpy.context.view_layer.update()
    pose = {bone.name: bone.matrix.copy() for bone in armature.pose.bones}
    grip = (pose['index_01_r'].translation - pose['pinky_01_r'].translation).normalized()
    angle = math.radians(sample_linear(angle_keys, upper['source_frame'] - 1))
    blade = Vector((-math.sin(angle), -math.cos(angle), 0.12))
    if frame < 7:
        blade.z += 0.5 * (1 - frame / 7)
    blade.normalize()
    hand_rotation = grip.rotation_difference(blade) @ pose['hand_r'].to_quaternion()
    weapon_control.location = base['world'] @ pose['hand_r'].translation
    weapon_control.rotation_quaternion = base['world'].to_quaternion() @ hand_rotation
    if 'weapon' in previous:
        weapon_control.rotation_quaternion.make_compatible(previous['weapon'])
    previous['weapon'] = weapon_control.rotation_quaternion.copy()
    constraint.mute = False
    weapon_control.keyframe_insert(data_path='location', frame=frame + 1)
    weapon_control.keyframe_insert(data_path='rotation_quaternion', frame=frame + 1)
    for path in ['location', 'rotation_quaternion', 'scale']:
        armature.keyframe_insert(data_path=path, frame=frame + 1)
    for bone in armature.pose.bones:
        q = bone.rotation_quaternion.copy()
        if bone.name in previous:
            q.make_compatible(previous[bone.name])
            bone.rotation_quaternion = q
        previous[bone.name] = q
        for path in ['location', 'rotation_quaternion', 'scale']:
            bone.keyframe_insert(data_path=path, frame=frame + 1)
    bpy.context.view_layer.update()
    records.append({'frame': frame, 'source_frame': base['source_frame'] - 1,
                    'upper_source_frame': upper['source_frame'] - 1,
                    'root_cm': list(armature.matrix_world.translation * 100),
                    'blade_direction': list(blade),
                    'bones_world_cm': {bone: list((armature.matrix_world @ armature.pose.bones[bone].matrix.translation) * 100)
                                       for bone in ['pelvis', 'hand_r', 'hand_l', 'foot_r', 'foot_l', 'ball_r', 'ball_l']}})
for animated in [armature, weapon_control]:
    for layer in animated.animation_data.action.layers:
        for strip in layer.strips:
            for slot in animated.animation_data.action.slots:
                bag = strip.channelbag(slot)
                if bag:
                    for curve in bag.fcurves:
                        for key in curve.keyframe_points:
                            key.interpolation = 'LINEAR'
scene.frame_set(1)
bpy.ops.object.select_all(action='DESELECT')
armature.select_set(True)
bpy.context.view_layer.objects.active = armature
fbx = output / (name + '.fbx')
bpy.ops.export_scene.fbx(filepath=str(fbx), use_selection=True, object_types={'ARMATURE'},
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_bones=True,
                         bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                         bake_anim_step=1, bake_anim_simplify_factor=0, axis_forward='-Z', axis_up='Y')
armature['td_source_animation'] = '/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01'
armature['td_authoring_note'] = 'Reference footwork retained; upper body follows pelvis by 3 frames, right wrist controlled separately'
report = {'name': name, 'fps': 30, 'duration': 43 / 30, 'upper_body_delay_frames': 3,
          'root_motion': True, 'time_map': time_map, 'records': records,
          'source_animation': armature['td_source_animation'], 'fbx': str(fbx),
          'bone_count': len(armature.pose.bones) + 1}
(output / (name + '.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(output / (name + '.blend')), check_existing=False)
print(json.dumps({'fbx': str(fbx), 'duration': report['duration'], 'root_motion_cm': records[-1]['root_cm']}))
