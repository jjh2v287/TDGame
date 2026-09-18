"""Blender 안에서 실행: author_sword_slash.py가 만든 씬에 바닥 격자·검 끝 궤적 선·검토 카메라를 놓고 정면·측면·게임 시점(3/4 부감)으로 전 프레임을 Workbench 렌더한다.
실행: Blender MCP execute_blender_code로 본문 전달(`python Tools/BlenderMCP/call_tool.py --code Tools/BlenderAnimation/render_sword_slash_preview.py`). 네임스페이스 TD_VIEWS로 시점 목록을 줄일 수 있다.
출력: Saved/BlenderAnimation/SwordSlash/<view>/frame_NNN.png, Saved/BlenderAnimation/SwordSlash/render-result.json
상태: 현행 (2026-09-18)
"""
import json
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT = Path('C:/Project/TDGame')
WORK = ROOT / 'Saved/BlenderAnimation/SwordSlash'
VIEWS = globals().get('TD_VIEWS', ['front', 'side', 'game'])
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
    trail.color = (0.95, 0.35, 0.2, 1)
    for index in (12, 15, 17, 19, 21):
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.02, location=blender_m(records[index]['sword_tip']))
        marker = bpy.context.object
        marker.name = f'TD_TipMark_{index}'
        marker.color = (1.0, 0.9, 0.2, 1)
camera_data = bpy.data.cameras.get('TD_ReviewCamera') or bpy.data.cameras.new('TD_ReviewCamera')
camera = bpy.data.objects.get('TD_ReviewCamera')
if camera is None:
    camera = bpy.data.objects.new('TD_ReviewCamera', camera_data)
    scene.collection.objects.link(camera)
scene.camera = camera
camera_data.type = 'ORTHO'
camera_data.ortho_scale = 3.2
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'OBJECT'
scene.display.shading.show_shadows = True
scene.display.shading.show_cavity = True
scene.display.shading.background_type = 'WORLD'
scene.world = scene.world or bpy.data.worlds.new('TD_ReviewWorld')
scene.world.color = (0.07, 0.08, 0.10)
scene.render.resolution_x = 560
scene.render.resolution_y = 560
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.view_settings.view_transform = 'Standard'
step = report['summary']['step_cm'] * 0.01
target = Vector((0, -step * 0.5, 0.95))
views = {'front': Vector((0.4, -6.0, 1.4)), 'side': Vector((6.0, -step * 0.5 - 0.1, 1.2)), 'game': Vector((3.4, 2.4, 4.4)), 'top': Vector((0.0, -step * 0.5, 7.0))}
rendered = 0
for label in VIEWS:
    camera.location = views[label]
    aim = target - camera.location
    camera.rotation_euler = aim.to_track_quat('-Z', 'Y').to_euler()
    if label == 'top':
        camera.rotation_euler = (0, 0, 0)
    directory = WORK / label
    directory.mkdir(parents=True, exist_ok=True)
    for frame in range(scene.frame_start, scene.frame_end + 1):
        scene.frame_set(frame)
        scene.render.filepath = str(directory / f'frame_{frame:03d}.png')
        bpy.ops.render.render(write_still=True)
        rendered += 1
camera.location = views['game']
camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
scene.frame_set(1)
(WORK / 'render-result.json').write_text(json.dumps({'rendered': rendered, 'views': list(VIEWS)}), encoding='utf-8')
print(json.dumps({'rendered': rendered, 'views': list(VIEWS)}))
