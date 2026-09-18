"""Blender 안에서 실행: v05 리포트의 그립 프로파일대로 검 미리보기 메시를 오른손 프레임에 붙이고 검토용 정지 이미지를 렌더한다.
실행: Blender MCP execute_blender_code로 본문을 전달(`python Tools/BlenderMCP/call_tool.py --code …`); 먼저 author_sword_grip.py 결과 .json이 있어야 한다.
출력: 검토 렌더 이미지(본문의 scene.render.filepath 경로)와 AnimationSources/Player/…_v05.json 갱신
상태: 현행 (후보 v05 예제)
"""
import json
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

root = Path('C:/Project/TDGame')
scene = bpy.context.scene
armature = bpy.data.objects['root']
report_path = root / 'AnimationSources/Player/AS_TD_Player_Attack01_Heavy_RToL_v05.json'
report = json.loads(report_path.read_text())
profile = report['grip_profile']
scene.frame_set(1)
if bpy.data.objects.get('TD_SwordPreview') is None:
    before = set(scene.objects)
    bpy.ops.import_scene.fbx(filepath=str(root / 'Saved/BlenderAnimation/Reference/SM_Sword.fbx'), use_anim=False)
    bpy.context.view_layer.update()
    sword = next(obj for obj in scene.objects if obj not in before and obj.type == 'MESH')
    sword.data.transform(sword.matrix_world)
    sword.parent = None
    sword.matrix_world = Matrix.Identity(4)
    sword.name = 'TD_SwordPreview'
    wide = [vertex.co for vertex in sword.data.vertices if abs(vertex.co.x) > 0.11]
    guard_y = sum(point.y for point in wide) / len(wide)
    grip = Vector((0, guard_y + 0.11, 0.014))
    sword.data.transform(Matrix.Translation(-grip))
    hand_frame = bpy.data.objects.new('TD_HandFrame_R', None)
    scene.collection.objects.link(hand_frame)
    for kind in ['COPY_LOCATION', 'COPY_ROTATION']:
        constraint = hand_frame.constraints.new(kind)
        constraint.target = armature
        constraint.subtarget = 'hand_r'
        constraint.owner_space = 'WORLD'
        constraint.target_space = 'WORLD'
    socket_frame = bpy.data.objects.new('TD_HandGrip_R', None)
    scene.collection.objects.link(socket_frame)
    socket_frame.parent = hand_frame
    socket_frame.rotation_mode = 'QUATERNION'
    socket_frame.location = Vector(profile['blender_local_cm']['translation']) * 0.01
    socket_frame.rotation_quaternion = Quaternion(profile['blender_local_cm']['quaternion_wxyz'])
    socket_frame.empty_display_type = 'ARROWS'
    socket_frame.empty_display_size = 0.15
    socket_frame['unreal_socket'] = 'HandGrip_R'
    sword.parent = socket_frame
    sword.matrix_basis = Matrix.Identity(4)
    sword.color = (0.76, 0.63, 0.31, 1)
    sword['source_asset'] = '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword'
    report['weapon_attachment'] = {
        'socket': 'HandGrip_R', 'parent_bone': 'hand_r',
        'unreal_weapon_relative_location_cm': [-grip.x*100, grip.y*100, -grip.z*100],
        'unreal_weapon_relative_rotation_degrees': [0, 0, 0],
        'note': 'Mesh pivot compensation only; the skeleton socket is unchanged',
    }
    report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
for obj in scene.objects:
    if obj.type == 'MESH' and obj.name != 'TD_SwordPreview':
        obj.color = (0.56, 0.63, 0.69, 1)
if bpy.data.objects.get('TD_ReviewFloor') is None:
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.015))
    bpy.context.object.name = 'TD_ReviewFloor'
    bpy.context.object.color = (0.18, 0.20, 0.24, 1)
    for axis in (0, 1):
        for line in range(-5, 6):
            bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, -0.007))
            strip = bpy.context.object
            strip.name = f'TD_Grid_{axis}_{line}'
            strip.location[axis] = line * 0.5
            strip.dimensions = (0.006, 6, 0.002) if axis == 0 else (6, 0.006, 0.002)
            strip.color = (0.3, 0.32, 0.36, 1)
camera_data = bpy.data.cameras.new('TD_GripReviewCamera')
camera = bpy.data.objects.new(camera_data.name, camera_data)
scene.collection.objects.link(camera)
scene.camera = camera
camera_data.type = 'ORTHO'
camera_data.ortho_scale = 3.5
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'OBJECT'
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = 'BOTH'
scene.display.shading.background_type = 'WORLD'
scene.world = scene.world or bpy.data.worlds.new('TD_ReviewWorld')
scene.world.color = (0.07, 0.08, 0.10)
scene.render.resolution_x = 640
scene.render.resolution_y = 640
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.view_settings.view_transform = 'Standard'
views = {'front': (1.8, -5.5, 2.3), 'side': (4, -0.75, 1.8), 'game': (3, 2, 5)}
target = Vector((0, -0.75, 0.95))
for label, location in views.items():
    camera.location = location
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    directory = root / 'Saved/BlenderAnimation/GripV05' / label
    directory.mkdir(parents=True, exist_ok=True)
    for frame in range(1, 45):
        scene.frame_set(frame)
        scene.render.filepath = str(directory / f'frame_{frame:03d}.png')
        bpy.ops.render.render(write_still=True)
camera_data.ortho_scale = 0.52
scene.display.shading.show_shadows = False
directory = root / 'Saved/BlenderAnimation/GripV05/closeup'
directory.mkdir(parents=True, exist_ok=True)
for frame in [1, 11, 18, 23, 26, 36, 44]:
    scene.frame_set(frame)
    bpy.context.view_layer.update()
    target = bpy.data.objects['TD_HandGrip_R'].matrix_world.translation
    camera.location = target + Vector((0.55, -0.75, 0.40))
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(directory / f'frame_{frame:03d}.png')
    bpy.ops.render.render(write_still=True)
camera_data.ortho_scale = 3.5
camera.location = views['front']
camera.rotation_euler = (Vector((0, -0.75, 0.95)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
scene.display.shading.show_shadows = True
scene.frame_set(1)
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(root / 'AnimationSources/Player/AS_TD_Player_Attack01_Heavy_RToL_v05.blend'), check_existing=False)
print(json.dumps({'rendered': 139, 'socket': profile['socket'], 'weapon_attachment': report.get('weapon_attachment')}))
