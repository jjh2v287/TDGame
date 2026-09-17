import json
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

root = Path('C:/Project/TDGame')
scene = bpy.context.scene
armature = bpy.data.objects['root']
scene.frame_set(1)
if bpy.data.objects.get('TD_SwordPreview') is None:
    before = set(scene.objects)
    bpy.ops.import_scene.fbx(filepath=str(root / 'Saved/BlenderAnimation/Reference/SM_Sword.fbx'), use_anim=False)
    imported = [obj for obj in scene.objects if obj not in before]
    sword = next(obj for obj in imported if obj.type == 'MESH')
    sword.data.transform(sword.matrix_world)
    sword.parent = None
    sword.matrix_world = Matrix.Identity(4)
    sword.name = 'TD_SwordPreview'
    coordinates = [vertex.co.copy() for vertex in sword.data.vertices]
    wide = [point for point in coordinates if abs(point.x) > 0.11]
    guard_y = sum(point.y for point in wide) / len(wide)
    grip = Vector((0, guard_y + 0.11, 0.014))
    sword.data.transform(Matrix.Translation(-grip))
    hand = armature.pose.bones['hand_r']
    index = armature.pose.bones['index_01_r']
    pinky = armature.pose.bones['pinky_01_r']
    direction = (armature.matrix_world.to_quaternion() @ (index.matrix.translation - pinky.matrix.translation)).normalized()
    side = (-direction).cross(Vector((0, 0, 1))).normalized()
    normal = side.cross(-direction).normalized()
    rotation = Matrix((side, -direction, normal)).transposed().to_4x4()
    rotation.translation = armature.matrix_world @ ((index.matrix.translation + pinky.matrix.translation) * 0.5)
    sword.parent = armature
    sword.parent_type = 'BONE'
    sword.parent_bone = 'hand_r'
    sword.matrix_world = rotation
    sword.color = (0.75, 0.61, 0.3, 1)
    sword['source_asset'] = '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword'
    print('SWORD', {'guard_y_m': guard_y, 'grip_m': list(grip), 'dimensions': list(sword.dimensions)})
for obj in scene.objects:
    if obj.type == 'MESH' and obj.name != 'TD_SwordPreview':
        obj.color = (0.55, 0.62, 0.68, 1)
if bpy.data.objects.get('TD_ReviewFloor') is None:
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.015))
    floor = bpy.context.object
    floor.name = 'TD_ReviewFloor'
    floor.color = (0.18, 0.2, 0.24, 1)
    for axis in (0, 1):
        for line in range(-5, 6):
            bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, -0.007))
            strip = bpy.context.object
            strip.name = f'TD_Grid_{axis}_{line}'
            strip.location[axis] = line * 0.5
            strip.dimensions = (0.006, 6, 0.002) if axis == 0 else (6, 0.006, 0.002)
            strip.color = (0.3, 0.32, 0.36, 1)
camera_data = bpy.data.cameras.get('TD_ReviewCamera') or bpy.data.cameras.new('TD_ReviewCamera')
camera = bpy.data.objects.get('TD_ReviewCamera')
if camera is None:
    camera = bpy.data.objects.new('TD_ReviewCamera', camera_data)
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
    directory = root / 'Saved/BlenderAnimation/HeavyV04' / label
    directory.mkdir(parents=True, exist_ok=True)
    for frame in range(1, 45):
        scene.frame_set(frame)
        scene.render.filepath = str(directory / f'frame_{frame:03d}.png')
        bpy.ops.render.render(write_still=True)
camera.location = views['front']
camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
scene.frame_set(1)
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(root / 'AnimationSources/Player/AS_TD_Player_Attack01_Heavy_RToL_v04.blend'), check_existing=False)
print(json.dumps({'rendered': 132, 'views': list(views)}))
