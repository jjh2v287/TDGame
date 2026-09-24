# File: Tools/BlenderAnimation/test_render_attack02.py
import math
from pathlib import Path
import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path('C:/Project/TDGame')
OUT = ROOT / 'Saved/BlenderAnimation/Attack02_Test'
OUT.mkdir(parents=True, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = 30
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.type = 'SOLID'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'OBJECT'
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.render.resolution_x = 960
scene.render.resolution_y = 720
scene.render.image_settings.file_format = 'PNG'

bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Saved/BlenderAnimation/SKM_Manny_Simple.fbx'), use_anim=False)
armature = next(o for o in scene.objects if o.type == 'ARMATURE')
body_mesh = next(o for o in scene.objects if o.type == 'MESH')
body_mesh.name = 'TD_Body'
body_mesh.color = (0.55, 0.62, 0.68, 1)

world = armature.matrix_world.copy()
armature.parent = None
armature.matrix_world = world

bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Saved/BlenderAnimation/Reference/MM_Attack_02.fbx'), use_anim=True)
source = next(o for o in scene.objects if o.type == 'ARMATURE' and o != armature)
source.hide_render = True
source.hide_set(True)

armature.animation_data_create()
armature.animation_data.action = source.animation_data.action
armature.animation_data.action_slot = source.animation_data.action_slot

SWORD_GRIP_OFFSET_M = Vector((0.0, 0.21225492159525552 + 0.11, 0.014))
HAND_GRIP_R_LOCAL_CM = Vector((-7.012133741056203, -2.0487315208256485, 0.0))
HAND_GRIP_R_LOCAL_QUAT = Quaternion((0.7071054093780323, 0.0, 0.0, -0.7071081529861803))

bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Saved/BlenderAnimation/Reference/SM_Sword.fbx'), use_anim=False)
sword = next(o for o in scene.objects if o.name.startswith('SM_Sword') or (o.type == 'MESH' and o != body_mesh))
sword.data.transform(sword.matrix_world)
sword.parent = None
sword.matrix_world = Matrix.Identity(4)
sword.data.transform(Matrix.Translation(-SWORD_GRIP_OFFSET_M))
sword.name = 'TD_SwordPreview'
sword.color = (0.78, 0.66, 0.34, 1)

hand_frame = bpy.data.objects.new('TD_HandFrame_R', None)
scene.collection.objects.link(hand_frame)
for kind in ['COPY_LOCATION', 'COPY_ROTATION']:
    con = hand_frame.constraints.new(kind)
    con.target = armature
    con.subtarget = 'hand_r'
socket_frame = bpy.data.objects.new('TD_HandGrip_R', None)
scene.collection.objects.link(socket_frame)
socket_frame.parent = hand_frame
socket_frame.rotation_mode = 'QUATERNION'
socket_frame.location = HAND_GRIP_R_LOCAL_CM * 0.01
socket_frame.rotation_quaternion = HAND_GRIP_R_LOCAL_QUAT
sword.parent = socket_frame
sword.matrix_basis = Matrix.Identity(4)

bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, -0.015))
floor = bpy.context.object
floor.name = 'TD_ReviewFloor'
floor.color = (0.18, 0.2, 0.24, 1)

camera_data = bpy.data.cameras.new('TD_ReviewCamera')
camera = bpy.data.objects.new('TD_ReviewCamera', camera_data)
scene.collection.objects.link(camera)
scene.camera = camera
camera_data.lens = 50.0

cam_setups = {
    'front': {'loc': Vector((0.0, -3.2, 1.25)), 'rot': Vector((math.radians(82), 0, 0))},
    'side': {'loc': Vector((-3.2, -0.25, 1.25)), 'rot': Vector((math.radians(82), 0, math.radians(-90)))},
    'game': {'loc': Vector((-2.2, -2.5, 2.6)), 'rot': Vector((math.radians(52), 0, math.radians(-42)))},
}

action = armature.animation_data.action
f_start = int(action.frame_range[0])
f_end = int(action.frame_range[1])
scene.frame_start = f_start
scene.frame_end = f_end

for f in range(f_start, f_end + 1, 4):
    scene.frame_set(f)
    bpy.context.view_layer.update()
    for view, setup in cam_setups.items():
        camera.location = setup['loc']
        camera.rotation_euler = setup['rot']
        vdir = OUT / view
        vdir.mkdir(parents=True, exist_ok=True)
        scene.render.filepath = str(vdir / f'frame_{f:03d}.png')
        bpy.ops.render.render(write_still=True)

print(f'TEST RENDER COMPLETE: frames {f_start} to {f_end}')
