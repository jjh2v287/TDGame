# File: Tools/BlenderAnimation/render_sword_slash_ltor_preview.py
"""Blender 안에서 실행: author_sword_slash_ltor.py가 만든 씬에 바닥 격자·검 끝 궤적 선·검토 카메라를 놓고 정면·측면·게임 시점으로 Workbench 렌더한다.
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT = Path('C:/Project/TDGame')
WORK = ROOT / 'Saved/BlenderAnimation/SwordSlashLToR'
VIEWS = globals().get('TD_VIEWS', ['front', 'side', 'game'])
if 'root' not in bpy.data.objects:
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'AnimationSources/Player/AS_TD_Player_Attack02_SwordSlash_LToR.blend'))
scene = bpy.context.scene
armature = bpy.data.objects['root']
report = json.loads((WORK / 'author-result.json').read_text(encoding='utf-8'))
records = report['records']


def blender_m(point):
    return Vector((-point[1], -point[0], point[2])) * 0.01


if bpy.data.objects.get('TD_ReviewFloor') is None:
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.015))
    floor = bpy.context.object
    floor.name = 'TD_ReviewFloor'
    floor.color = (0.18, 0.2, 0.24, 1)
    for axis in (0, 1):
        for line in range(-6, 7):
            bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, -0.007))
            strip = bpy.context.object
            strip.name = f'TD_Grid_{axis}_{line}'
            strip.location[axis] = line * 0.5
            strip.dimensions = (0.006, 7, 0.002) if axis == 0 else (7, 0.006, 0.002)
            strip.color = (0.3, 0.32, 0.36, 1)

if bpy.data.objects.get('TD_TipTrail') is None:
    curve = bpy.data.curves.new('TD_TipTrail', 'CURVE')
    curve.dimensions = '3D'
    curve.bevel_depth = 0.006
    spline = curve.splines.new('POLY')
    spline.points.add(len(records) - 1)
    for index, row in enumerate(records):
        point = blender_m(row['sword_tip'])
        spline.points[index].co = (point.x, point.y, point.z, 1)
    trail = bpy.data.objects.new('TD_TipTrail', curve)
    scene.collection.objects.link(trail)
    trail.color = (0.25, 0.65, 0.95, 1)

camera_data = bpy.data.cameras.get('TD_ReviewCamera') or bpy.data.cameras.new('TD_ReviewCamera')
camera = bpy.data.objects.get('TD_ReviewCamera')
if camera is None:
    camera = bpy.data.objects.new('TD_ReviewCamera', camera_data)
    scene.collection.objects.link(camera)
scene.camera = camera
camera_data.lens = 50.0
camera_data.clip_start = 0.1
camera_data.clip_end = 100.0

scene.display.shading.type = 'SOLID'
scene.display.shading.color_type = 'OBJECT'
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x = 960
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'

camera_setups = {
    'front': {'location': Vector((0.0, -3.2, 1.25)), 'rotation': Vector((math.radians(82), 0, 0))},
    'side': {'location': Vector((-3.2, -0.25, 1.25)), 'rotation': Vector((math.radians(82), 0, math.radians(-90)))},
    'game': {'location': Vector((-2.2, -2.5, 2.6)), 'rotation': Vector((math.radians(52), 0, math.radians(-42)))},
}

rendered_files = {}
for view in VIEWS:
    setup = camera_setups[view]
    camera.location = setup['location']
    camera.rotation_euler = setup['rotation']
    out_dir = WORK / view
    out_dir.mkdir(parents=True, exist_ok=True)
    rendered_files[view] = []
    for frame in range(1, len(records) + 1):
        scene.frame_set(frame)
        filepath = str(out_dir / f'frame_{frame - 1:03d}.png')
        scene.render.filepath = filepath
        bpy.ops.render.render(write_still=True)
        rendered_files[view].append(filepath)

(WORK / 'render-result.json').write_text(json.dumps({'views': VIEWS, 'count': len(records), 'files': rendered_files}, indent=1), encoding='utf-8')
print("RENDER_SUCCESS:", json.dumps({'views': VIEWS, 'frames_per_view': len(records)}))
