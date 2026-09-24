import math
from pathlib import Path
import bpy
from mathutils import Vector

ROOT = Path("C:/Project/TDGame")
BLEND_FILE = ROOT / "AnimationSources/Player/AS_TD_Player_Attack01_SwordSlash_RToL.blend"
BVH_FILE = ROOT / "AnimationSources/Mocap/02_07.bvh"
WORK = ROOT / "Saved/BlenderAnimation/MocapSlash"

bpy.ops.wm.open_mainfile(filepath=str(BLEND_FILE))
bpy.ops.import_anim.bvh(filepath=str(BVH_FILE))
bvh = bpy.data.objects["02_07"]
tgt = bpy.data.objects["root"]

scene = bpy.context.scene
scene.rsl_retargeting_armature_source = bvh
scene.rsl_retargeting_armature_target = tgt
bpy.ops.rsl.build_bone_list()
bpy.ops.rsl.retarget_animation()

start_f = 1430
end_f = 1490
step = 2
frames = list(range(start_f, end_f + 1, step))

scene.display.shading.type = "SOLID"
scene.display.shading.color_type = "OBJECT"
scene.render.engine = "BLENDER_WORKBENCH"
scene.render.resolution_x = 960
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"

if bpy.data.objects.get("TD_ReviewFloor") is None:
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.015))
    floor = bpy.context.object
    floor.name = "TD_ReviewFloor"
    floor.color = (0.18, 0.2, 0.24, 1)

camera_data = bpy.data.cameras.get("TD_ReviewCamera") or bpy.data.cameras.new("TD_ReviewCamera")
camera = bpy.data.objects.get("TD_ReviewCamera")
if camera is None:
    camera = bpy.data.objects.new("TD_ReviewCamera", camera_data)
    scene.collection.objects.link(camera)
scene.camera = camera
camera_data.lens = 50.0

scene.frame_set(1460)
center = tgt.pose.bones["pelvis"].matrix.to_translation()

camera_setups = {
    "front": {"location": center + Vector((0.0, -3.5, 0.3)), "rotation": Vector((math.radians(85), 0, 0))},
    "side": {"location": center + Vector((-3.5, 0.0, 0.3)), "rotation": Vector((math.radians(85), 0, math.radians(-90)))},
    "game": {"location": center + Vector((-2.5, -2.5, 1.8)), "rotation": Vector((math.radians(55), 0, math.radians(-45)))},
}

for view, setup in camera_setups.items():
    camera.location = setup["location"]
    camera.rotation_euler = setup["rotation"]
    view_dir = WORK / view
    view_dir.mkdir(parents=True, exist_ok=True)
    for idx, f in enumerate(frames):
        scene.frame_set(f)
        scene.render.filepath = str(view_dir / f"frame_{idx:03d}.png")
        bpy.ops.render.render(write_still=True)

print(f"MOCAP WORKBENCH RENDER FINISHED: {len(frames)} frames across 3 views.")
