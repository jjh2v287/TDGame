import json
from pathlib import Path

import bpy
from mathutils import Vector


def render_preview(output_directory, frames=(1, 8, 12, 15, 18, 22, 26, 37)):
    output = Path(output_directory)
    output.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    armature = next(obj for obj in scene.objects if obj.type == 'ARMATURE')
    for obj in list(scene.objects):
        if obj.name.startswith('TD_Preview_'):
            bpy.data.objects.remove(obj, do_unlink=True)
    for obj in scene.objects:
        if obj.type == 'MESH':
            obj.color = (0.44, 0.53, 0.59, 1)
    bpy.ops.mesh.primitive_cube_add(size=1)
    blade = bpy.context.object
    blade.name = 'TD_Preview_Blade'
    blade.color = (0.86, 0.48, 0.12, 1)
    blade.dimensions = (0.032, 0.85, 0.008)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    camera_data = bpy.data.cameras.new('TD_Preview_Camera')
    camera = bpy.data.objects.new('TD_Preview_Camera', camera_data)
    scene.collection.objects.link(camera)
    camera.location = (2.7, -4.3, 2.4)
    camera.rotation_euler = (Vector((0, 0, 0.95)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera_data.type = 'ORTHO'
    camera_data.ortho_scale = 2.65
    scene.camera = camera
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.studiolight_rotate_z = 0.5
    scene.display.shading.color_type = 'OBJECT'
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = 'BOTH'
    scene.display.shading.background_type = 'WORLD'
    if scene.world is None:
        scene.world = bpy.data.worlds.new('TD_Preview_World')
    scene.world.color = (0.07, 0.08, 0.10)
    scene.render.resolution_x = 600
    scene.render.resolution_y = 600
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'Standard'
    paths = []
    for frame in frames:
        scene.frame_set(frame)
        hand = armature.pose.bones['hand_r']
        index = armature.pose.bones['index_01_r']
        pinky = armature.pose.bones['pinky_01_r']
        direction = (armature.matrix_world.to_quaternion() @
                     (index.matrix.translation - pinky.matrix.translation)).normalized()
        hand_position = armature.matrix_world @ hand.matrix.translation
        blade.location = hand_position + direction * 0.44
        blade.rotation_euler = direction.to_track_quat('Y', 'Z').to_euler()
        blade.keyframe_insert(data_path='location', frame=frame)
        blade.keyframe_insert(data_path='rotation_euler', frame=frame)
        scene.render.filepath = str(output / f'frame_{frame:03d}.png')
        bpy.ops.render.render(write_still=True)
        paths.append(scene.render.filepath)
    scene.frame_set(1)
    print(json.dumps({'preview_frames': paths}))


render_preview('C:/Project/TDGame/Saved/BlenderAnimation/Preview', frames=range(1, 38))
