import math
from pathlib import Path
import bpy
from mathutils import Vector

ROOT = Path("C:/Project/TDGame")
BLEND_FILE = ROOT / "AnimationSources/Player/AS_TD_Player_Attack03_MocapSlash.blend"
WORK = ROOT / "Saved/BlenderAnimation/MocapSlash"

bpy.ops.wm.open_mainfile(filepath=str(BLEND_FILE))
scene = bpy.context.scene
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

camera_setups = {
    "front": {"location": Vector((0.0, -3.2, 1.25)), "rotation": Vector((math.radians(82), 0, 0))},
    "side": {"location": Vector((-3.2, -0.25, 1.25)), "rotation": Vector((math.radians(82), 0, math.radians(-90)))},
    "game": {"location": Vector((-2.2, -2.5, 2.6)), "rotation": Vector((math.radians(52), 0, math.radians(-42)))},
}

num_frames = scene.frame_end - scene.frame_start + 1

for view, setup in camera_setups.items():
    camera.location = setup["location"]
    camera.rotation_euler = setup["rotation"]
    view_dir = WORK / view
    view_dir.mkdir(parents=True, exist_ok=True)
    for frame in range(num_frames):
        scene.frame_set(frame)
        scene.render.filepath = str(view_dir / f"frame_{frame:03d}.png")
        bpy.ops.render.render(write_still=True)

print(f"RENDER COMPLETE: {num_frames} frames rendered across 3 views.")
