import json
import math
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path("C:/Project/TDGame")
BLEND_FILE = ROOT / "AnimationSources/Player/AS_TD_Player_Attack03_MocapSlash.blend"
WORK = ROOT / "Saved/BlenderAnimation/MocapSlash"
OUT_DIR = ROOT / "Docs/Validation/BlenderAnimation"

import bpy
from mathutils import Vector

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
fps = 30

for view, setup in camera_setups.items():
    camera.location = setup["location"]
    camera.rotation_euler = setup["rotation"]
    view_dir = WORK / view
    view_dir.mkdir(parents=True, exist_ok=True)
    for frame in range(num_frames):
        scene.frame_set(frame)
        scene.render.filepath = str(view_dir / f"frame_{frame:03d}.png")
        bpy.ops.render.render(write_still=True)

OUT_DIR.mkdir(parents=True, exist_ok=True)
per_view = {}
for view in ["front", "side", "game"]:
    per_view[view] = [Image.open(p).convert("RGB") for p in sorted((WORK / view).glob("frame_*.png"))]

width, height = per_view["front"][0].size

for view, imgs in per_view.items():
    imgs[0].save(OUT_DIR / f"mocap-slash-{view}.gif", save_all=True, append_images=imgs[1:], duration=int(1000 / fps), loop=0)

combined = []
for idx in range(num_frames):
    sheet = Image.new("RGB", (width * 3, height))
    for col, v in enumerate(["front", "side", "game"]):
        sheet.paste(per_view[v][idx], (col * width, 0))
    ImageDraw.Draw(sheet).text((12, 12), f"f{idx:02d} {idx/fps:.2f}s [Mocap Attack 03]", fill=(255, 255, 255))
    combined.append(sheet.resize((width * 3 // 2, height // 2), Image.LANCZOS))

combined[0].save(OUT_DIR / "mocap-slash-three-views.gif", save_all=True, append_images=combined[1:], duration=int(1000 / fps), loop=0)

key_frames = [0, 5, 10, 15, 20, 25, 30, 35, 40]
columns = 5
rows = (len(key_frames) + columns - 1) // columns
poses = Image.new("RGB", (width * columns, height * 3 * rows))

for slot, frame in enumerate(key_frames):
    col, row = slot % columns, slot // columns
    for line, v in enumerate(["front", "side", "game"]):
        poses.paste(per_view[v][frame], (col * width, (row * 3 + line) * height))
    ImageDraw.Draw(poses).text((col * width + 12, row * 3 * height + 12), f"f{frame:02d} {frame/fps:.2f}s", fill=(255, 255, 0))

poses.thumbnail((width * columns // 2, height * 3 * rows // 2))
poses.save(OUT_DIR / "mocap-slash-poses.png")
print("MOCAP PREVIEW RENDER COMPLETE: mocap-slash-poses.png & mocap-slash-three-views.gif")
