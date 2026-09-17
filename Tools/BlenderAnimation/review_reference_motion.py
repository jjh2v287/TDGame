import json
from pathlib import Path

import addon_utils
import bpy
from mathutils import Vector

root = Path('C:/Project/TDGame')
out = root / 'Saved/BlenderAnimation/Reference/Preview'
out.mkdir(parents=True, exist_ok=True)
print('ADDONS', [module.__name__ for module in addon_utils.modules() if any(term in module.__name__.lower() for term in ['rigify', 'rig_pro', 'animation'])])
summary = []
for index in (1, 2, 3):
    scene = bpy.data.scenes.new(f'TD_Reference_Attack_{index:02d}')
    bpy.context.window.scene = scene
    bpy.ops.import_scene.fbx(filepath=str(root / 'Saved/BlenderAnimation/SKM_Manny_Simple.fbx'), use_anim=False)
    armature = next(obj for obj in scene.objects if obj.type == 'ARMATURE')
    bpy.ops.import_scene.fbx(filepath=str(root / f'Saved/BlenderAnimation/Reference/MM_Attack_{index:02d}.fbx'), use_anim=True)
    source = next(obj for obj in scene.objects if obj.type == 'ARMATURE' and obj != armature)
    world = armature.matrix_world.copy()
    armature.parent = None
    armature.matrix_world = world
    armature.animation_data_create()
    armature.animation_data.action = source.animation_data.action
    armature.animation_data.action_slot = source.animation_data.action_slot
    source.hide_render = True
    source.hide_set(True)
    action = armature.animation_data.action
    last = int(action.frame_range[1])
    scene.render.fps = 30
    scene.frame_start = 1
    scene.frame_end = last
    for obj in scene.objects:
        if obj.type == 'MESH':
            obj.color = (0.52, 0.59, 0.63, 1)
    camera_data = bpy.data.cameras.new(f'TD_Reference_Camera_{index}')
    camera = bpy.data.objects.new(camera_data.name, camera_data)
    scene.collection.objects.link(camera)
    camera.location = (3.5, -5.5, 2.8)
    camera.rotation_euler = (Vector((0, -0.6, 0.9)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera_data.type = 'ORTHO'
    camera_data.ortho_scale = 3.6
    scene.camera = camera
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'OBJECT'
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = 'BOTH'
    scene.display.shading.background_type = 'WORLD'
    scene.world = bpy.data.worlds.new(f'TD_Reference_World_{index}')
    scene.world.color = (0.07, 0.08, 0.10)
    scene.render.resolution_x = 480
    scene.render.resolution_y = 480
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'Standard'
    samples = []
    for frame in [1, 6, 11, 14, 19, 25, last]:
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        samples.append({'frame': frame, 'root': list(armature.location),
                        'hand_r': list(armature.matrix_world @ armature.pose.bones['hand_r'].matrix.translation)})
        scene.render.filepath = str(out / f'attack{index}_frame{frame:03d}.png')
        bpy.ops.render.render(write_still=True)
    summary.append({'reference': index, 'action': action.name, 'range': list(action.frame_range), 'samples': samples})
(out / 'reference-summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary))
