"""Blender 안에서 실행: 작업 씬의 지정 Action을 바닥 격자·검 끝 궤적과 함께 정면·오른쪽·게임 시점(뒤쪽 55° 부감)·위·오른손 근접 시점으로 Workbench 렌더한다.
실행: call_tool.py --code 로 본문 전달. 네임스페이스 TD_ACTION(기본 현재 Action), TD_VIEWS, TD_STEP(프레임 간격), TD_OUT(출력 하위 폴더), TD_SIZE(픽셀)로 조절
출력: Saved/BlenderAnimation/SwordAttack01/<TD_OUT>/<view>/f_NNN.png, 같은 폴더 render-result.json(프레임별 검 끝·손 좌표 cm, 캐릭터 기준 전방·오른쪽·위)
상태: 현행 (2026-09-25, 캐릭터 전방 = 월드 -Y, 오른쪽 = 월드 -X 전제)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path('C:/Project/TDGame')
scene = bpy.context.scene
armature = bpy.data.objects['root']
if globals().get('TD_ACTION'):
    armature.animation_data.action = bpy.data.actions[TD_ACTION]
action = armature.animation_data.action
VIEWS = globals().get('TD_VIEWS', ['front', 'right', 'game', 'top', 'hand'])
STEP = int(globals().get('TD_STEP', 1))
SIZE = int(globals().get('TD_SIZE', 440))
OUT = ROOT / 'Saved/BlenderAnimation/SwordAttack01' / globals().get('TD_OUT', action.name)
first, last = [int(round(value)) for value in action.frame_range]
scene.frame_start, scene.frame_end = first, last
tip = bpy.data.objects['TD_SwordTip']
socket = bpy.data.objects['TD_HandGrip_R']


def character_cm(world_point):
    return [round(-world_point.y * 100, 2), round(-world_point.x * 100, 2), round(world_point.z * 100, 2)]


def remove_object(name):
    obj = bpy.data.objects.get(name)
    if obj is not None:
        bpy.data.objects.remove(obj, do_unlink=True)


def build_floor():
    if bpy.data.objects.get('TD_ReviewFloor') is not None:
        return
    bpy.ops.mesh.primitive_plane_add(size=8, location=(0, 0, -0.004))
    floor = bpy.context.object
    floor.name = 'TD_ReviewFloor'
    floor.color = (0.2, 0.22, 0.26, 1)
    for axis in (0, 1):
        for line in range(-8, 9):
            bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, -0.001))
            strip = bpy.context.object
            strip.name = f'TD_Grid_{axis}_{line}'
            strip.location[axis] = line * 0.25
            strip.dimensions = (0.004, 4, 0.001) if axis == 0 else (4, 0.004, 0.001)
            strip.color = (0.34, 0.36, 0.4, 1) if line % 4 else (0.5, 0.52, 0.56, 1)


def sample_records():
    records = []
    for frame in range(first, last + 1):
        scene.frame_set(frame)
        bones = armature.pose.bones
        world = armature.matrix_world
        records.append({
            'frame': frame,
            'tip': character_cm(tip.matrix_world.translation),
            'grip': character_cm(socket.matrix_world.translation),
            'hand_r': character_cm(world @ bones['hand_r'].head),
            'hand_l': character_cm(world @ bones['hand_l'].head),
            'pelvis': character_cm(world @ bones['pelvis'].head),
            'root': character_cm(world.translation),
            'ball_l': character_cm(world @ bones['ball_l'].head),
            'ball_r': character_cm(world @ bones['ball_r'].head),
            'head': character_cm(world @ bones['head'].head),
        })
    return records


def build_trail(records):
    remove_object('TD_TipTrail')
    curve = bpy.data.curves.new('TD_TipTrail', 'CURVE')
    curve.dimensions = '3D'
    curve.bevel_depth = 0.005
    spline = curve.splines.new('POLY')
    spline.points.add(len(records) - 1)
    for index, row in enumerate(records):
        forward, right, up = row['tip']
        spline.points[index].co = (-right * 0.01, -forward * 0.01, up * 0.01, 1)
    trail = bpy.data.objects.new('TD_TipTrail', curve)
    scene.collection.objects.link(trail)
    trail.color = (0.95, 0.3, 0.2, 1)


def setup_render():
    scene.render.engine = 'BLENDER_WORKBENCH'
    shading = scene.display.shading
    shading.light = 'STUDIO'
    shading.color_type = 'OBJECT'
    shading.show_shadows = True
    shading.show_cavity = True
    shading.background_type = 'WORLD'
    scene.world = scene.world or bpy.data.worlds.new('TD_ReviewWorld')
    scene.world.color = (0.07, 0.08, 0.1)
    scene.render.resolution_x = SIZE
    scene.render.resolution_y = SIZE
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'Standard'
    scene.render.film_transparent = False


def camera_object():
    data = bpy.data.cameras.get('TD_ReviewCamera') or bpy.data.cameras.new('TD_ReviewCamera')
    camera = bpy.data.objects.get('TD_ReviewCamera')
    if camera is None:
        camera = bpy.data.objects.new('TD_ReviewCamera', data)
        scene.collection.objects.link(camera)
    data.type = 'ORTHO'
    scene.camera = camera
    return camera


def aim(camera, position, target, ortho_scale):
    camera.location = position
    camera.rotation_euler = (target - position).to_track_quat('-Z', 'Y').to_euler()
    camera.data.ortho_scale = ortho_scale


def view_pose(view, center, records_by_frame, frame):
    game_direction = Vector((-math.sin(math.radians(35)), math.cos(math.radians(35)), 0)) * math.cos(math.radians(55)) + Vector((0, 0, math.sin(math.radians(55))))
    if view == 'front':
        return center + Vector((0.0, -6.0, 0.25)), center, 2.7
    if view == 'right':
        return center + Vector((-6.0, 0.0, 0.25)), center, 2.7
    if view == 'game':
        return center + game_direction * 8.0, center, 3.0
    if view == 'top':
        return center + Vector((0.0, 0.0, 8.0)), center + Vector((0.0, 0.0, 0.0)), 3.2
    hand = records_by_frame[frame]['hand_r']
    hand_world = Vector((-hand[1] * 0.01, -hand[0] * 0.01, hand[2] * 0.01))
    return hand_world + Vector((-2.0, -3.0, 1.0)).normalized() * 3.0, hand_world, 0.75


setup_render()
build_floor()
records = sample_records()
build_trail(records)
records_by_frame = {row['frame']: row for row in records}
root_start = Vector(records[0]['root'])
root_end = Vector(records[-1]['root'])
middle = (root_start + root_end) * 0.5
center = Vector((-middle[1] * 0.01, -middle[0] * 0.01, 0.95))
camera = camera_object()
rendered = 0
for view in VIEWS:
    (OUT / view).mkdir(parents=True, exist_ok=True)
    for frame in range(first, last + 1, STEP):
        scene.frame_set(frame)
        position, target, scale = view_pose(view, center, records_by_frame, frame)
        aim(camera, position, target, scale)
        scene.render.filepath = str(OUT / view / f'f_{frame:03d}.png')
        bpy.ops.render.render(write_still=True)
        rendered += 1
report = {'action': action.name, 'fps': scene.render.fps, 'frames': [first, last], 'views': VIEWS, 'step': STEP, 'rendered': rendered, 'records': records}
(OUT / 'render-result.json').write_text(json.dumps(report), encoding='utf-8')
print(json.dumps({'out': str(OUT), 'rendered': rendered, 'frames': [first, last]}))
