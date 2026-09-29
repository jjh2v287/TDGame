"""Blender 안에서 실행: Manny 메시·HandGrip_R 소켓의 SM_Sword를 세우고, 참고 FBX(기본 Greystone PrimaryA·B·C)를 월드 행렬 그대로 Manny 리그 Action으로 베이크해 편집용 작업 파일을 만든다.
실행: Tools/BlenderMCP/.venv/Scripts/python.exe Tools/BlenderMCP/call_tool.py --code Tools/BlenderAnimation/sword_attack01_scene.py
출력: Saved/BlenderAnimation/SwordAttack01/work.blend(Action TD_Ref_<이름>), Saved/BlenderAnimation/SwordAttack01/scene-result.json
상태: 현행 (2026-09-25, UE5 Manny 전용: 본 이름·HandGrip_R 소켓 고정)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path('C:/Project/TDGame')
WORK = ROOT / 'Saved/BlenderAnimation/SwordAttack01'
REFERENCES = globals().get('TD_REFERENCES', ['Attack_PrimaryA', 'Attack_PrimaryB', 'Attack_PrimaryC'])
REFERENCE_DIR = ROOT / 'Saved/BlenderAnimation/Reference/Greystone'
FPS = 60
HAND_GRIP_R_LOCAL_CM = Vector((-7.012133741056203, -2.0487315208256485, 0.0))
HAND_GRIP_R_LOCAL_QUAT = Quaternion((0.7071054093780323, 0.0, 0.0, -0.7071081529861803))
SWORD_GRIP_OFFSET_M = Vector((0.0, 0.21225492159525552 + 0.055, 0.014))
GRIP_MODE = globals().get('TD_GRIP_MODE', 'fist')
FIST_CENTER_CM = {'along_hand': 9.0, 'volar': 2.8}
HANDLE_TILT_TOWARD_FINGERS_DEG = 17.0
WORK.mkdir(parents=True, exist_ok=True)
scene = bpy.context.scene


def set_scene_rate():
    scene.render.fps = FPS
    scene.render.fps_base = 1.0


def clear_scene():
    for obj in list(scene.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for collection in (bpy.data.meshes, bpy.data.armatures, bpy.data.actions, bpy.data.materials, bpy.data.curves, bpy.data.cameras):
        for item in list(collection):
            collection.remove(item)


def import_fbx(path, use_anim):
    before = set(scene.objects)
    bpy.ops.import_scene.fbx(filepath=str(path), use_anim=use_anim)
    source_fps = scene.render.fps / scene.render.fps_base
    set_scene_rate()
    created = [obj for obj in scene.objects if obj not in before]
    for obj in created:
        obj['td_source_fps'] = source_fps
    return created


def detach_from_fbx_root(armature):
    parent = armature.parent
    world = armature.matrix_world.copy()
    armature.parent = None
    armature.matrix_world = world
    if parent is not None and parent.type == 'EMPTY':
        bpy.data.objects.remove(parent, do_unlink=True)


def build_character():
    dummy = bpy.data.objects.new('TD_ContextDummy', None)
    scene.collection.objects.link(dummy)
    bpy.context.view_layer.objects.active = dummy
    created = import_fbx(ROOT / 'Saved/BlenderAnimation/SKM_Manny_Simple.fbx', use_anim=False)
    armature = next(obj for obj in created if obj.type == 'ARMATURE')
    if armature.name != 'root':
        raise ValueError(f'The Manny armature must keep the name root, got {armature.name}')
    detach_from_fbx_root(armature)
    bpy.data.objects.remove(dummy, do_unlink=True)
    body = next(obj for obj in scene.objects if obj.type == 'MESH')
    body.name = 'TD_Body'
    body.color = (0.55, 0.62, 0.68, 1)
    armature.rotation_mode = 'QUATERNION'
    for bone in armature.pose.bones:
        bone.rotation_mode = 'QUATERNION'
    return armature


def fist_grip_local(armature):
    rest = {bone.name: bone.matrix_local for bone in armature.data.bones}
    hand_inverse = rest['hand_r'].inverted()
    local = lambda name: hand_inverse @ rest[name].translation
    wrist = local('hand_r')
    axis = (local('middle_01_r') - wrist).normalized()
    radial = local('index_01_r') - local('pinky_01_r')
    radial = (radial - axis * radial.dot(axis)).normalized()
    volar = radial.cross(axis)
    tilt = math.radians(HANDLE_TILT_TOWARD_FINGERS_DEG)
    blade = (radial * math.cos(tilt) + axis * math.sin(tilt)).normalized()
    edge = (axis - blade * axis.dot(blade)).normalized()
    flat = blade.cross(edge)
    rotation = Matrix((edge, -blade, flat)).transposed()
    center = wrist + axis * FIST_CENTER_CM['along_hand'] + volar * FIST_CENTER_CM['volar']
    return Matrix.Translation(center) @ rotation.to_4x4()


def build_sword(armature):
    bpy.context.view_layer.objects.active = armature
    created = import_fbx(ROOT / 'Saved/BlenderAnimation/Reference/SM_Sword.fbx', use_anim=False)
    sword = next(obj for obj in created if obj.type == 'MESH')
    sword.data.transform(sword.matrix_world)
    sword.parent = None
    sword.matrix_world = Matrix.Identity(4)
    sword.data.transform(Matrix.Translation(-SWORD_GRIP_OFFSET_M))
    sword.name = 'TD_SwordPreview'
    sword.color = (0.85, 0.72, 0.36, 1)
    sword['source_asset'] = '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword'
    for obj in created:
        if obj != sword:
            bpy.data.objects.remove(obj, do_unlink=True)
    socket = bpy.data.objects.new('TD_HandGrip_R', None)
    scene.collection.objects.link(socket)
    socket.parent = armature
    socket.parent_type = 'BONE'
    socket.parent_bone = 'hand_r'
    bone = armature.data.bones['hand_r']
    socket.matrix_parent_inverse = Matrix.Identity(4)
    socket.rotation_mode = 'QUATERNION'
    tail_offset = Matrix.Translation((0.0, -bone.length, 0.0))
    socket_local = Matrix.LocRotScale(HAND_GRIP_R_LOCAL_CM, HAND_GRIP_R_LOCAL_QUAT, Vector((1, 1, 1)))
    if GRIP_MODE == 'fist':
        socket_local = fist_grip_local(armature)
    socket.matrix_basis = tail_offset @ socket_local
    socket['grip_mode'] = GRIP_MODE
    socket.empty_display_type = 'ARROWS'
    socket.empty_display_size = 6
    socket['unreal_socket'] = 'HandGrip_R'
    sword.parent = socket
    sword.matrix_parent_inverse = Matrix.Identity(4)
    sword.matrix_basis = Matrix.Scale(100.0, 4)
    tip = bpy.data.objects.new('TD_SwordTip', None)
    scene.collection.objects.link(tip)
    tip.parent = socket
    tip.location = (0.0, -(0.753124475479126 + SWORD_GRIP_OFFSET_M.y) * 100.0, 0.0)
    tip.empty_display_size = 3
    return sword


def armature_space_targets(reference):
    return {bone.name: reference.matrix_world @ bone.matrix for bone in reference.pose.bones}


def bake_reference(armature, reference, action_name):
    action = bpy.data.actions.new(action_name)
    action.use_fake_user = True
    armature.animation_data_create()
    armature.animation_data.action = action
    reference_action = reference.animation_data.action
    first, last = [int(round(value)) for value in reference_action.frame_range]
    source_step = reference['td_source_fps'] / FPS
    frame_count = int(round((last - first) / source_step)) + 1
    world_inverse = armature.matrix_world.inverted()
    rest = {bone.name: bone.matrix_local.copy() for bone in armature.data.bones}
    names = [bone.name for bone in armature.pose.bones if bone.name in reference.pose.bones]
    for index in range(frame_count):
        source_frame = first + index * source_step
        scene.frame_set(int(source_frame), subframe=source_frame - int(source_frame))
        targets = {name: world_inverse @ matrix for name, matrix in armature_space_targets(reference).items()}
        for name in names:
            pose_bone = armature.pose.bones[name]
            parent = pose_bone.parent
            if parent is None:
                basis = rest[name].inverted() @ targets[name]
            else:
                parent_pose = targets.get(parent.name, rest[parent.name])
                basis = (parent_pose @ rest[parent.name].inverted() @ rest[name]).inverted() @ targets[name]
            location, rotation, scale = basis.decompose()
            pose_bone.location = location
            pose_bone.rotation_quaternion = rotation
            pose_bone.scale = scale
            frame_offset = index + 1
            pose_bone.keyframe_insert('location', frame=frame_offset, group=name)
            pose_bone.keyframe_insert('rotation_quaternion', frame=frame_offset, group=name)
            pose_bone.keyframe_insert('scale', frame=frame_offset, group=name)
    return {'action': action_name, 'frames': frame_count, 'source_fps': reference['td_source_fps'], 'source_range': [first, last], 'bones': len(names)}


def import_reference(name):
    created = import_fbx(REFERENCE_DIR / f'{name}.fbx', use_anim=True)
    reference = next(obj for obj in created if obj.type == 'ARMATURE')
    detach_from_fbx_root(reference)
    return reference, created


def measure_bake_error(armature, reference, source_range):
    first, last = source_range
    source_step = reference['td_source_fps'] / FPS
    worst = 0.0
    for frame in range(first, last + 1, 4):
        scene.frame_set(frame)
        reference_heads = {bone.name: reference.matrix_world @ bone.head for bone in reference.pose.bones}
        scene.frame_set(int(round((frame - first) / source_step)) + 1)
        for bone in armature.pose.bones:
            if bone.name in reference_heads:
                worst = max(worst, ((armature.matrix_world @ bone.head) - reference_heads[bone.name]).length)
    return round(worst * 100.0, 4)


clear_scene()
set_scene_rate()
armature = build_character()
build_sword(armature)
results = []
for reference_name in REFERENCES:
    reference, created = import_reference(reference_name)
    action_name = f'TD_Ref_{reference_name}'
    result = bake_reference(armature, reference, action_name)
    result['max_head_error_cm'] = measure_bake_error(armature, reference, result['source_range'])
    reference_action = reference.animation_data.action
    for obj in created:
        if obj.name in scene.objects:
            bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.actions.remove(reference_action)
    results.append(result)
armature.animation_data.action = bpy.data.actions[f'TD_Ref_{REFERENCES[0]}']
scene.frame_start = 1
scene.frame_end = results[0]['frames']
set_scene_rate()
bpy.ops.wm.save_as_mainfile(filepath=str(WORK / 'work.blend'))
report = {'blend': str(WORK / 'work.blend'), 'armature_scale': list(armature.scale), 'armature_rotation': list(armature.rotation_quaternion), 'references': results}
(WORK / 'scene-result.json').write_text(json.dumps(report, indent=1), encoding='utf-8')
print(json.dumps(report))
