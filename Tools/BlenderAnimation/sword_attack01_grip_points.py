"""Blender 안에서 실행: 작업 씬의 칼 미리보기에서 칼끝·코등이 중심·코등이 양 끝·폼멜 끝 좌표를 여러 프레임에 대해 언리얼 컴포넌트 공간(cm, x = 캐릭터 왼쪽→오른쪽의 반대, y = 전방)으로 내보낸다. 언리얼 쪽 editor_solve_sword_attachment.py가 이 점들로 HandGrip_R 기준 SM_Sword 부착 상대 변환을 역산한다.
실행: python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_grip_points.py [TD_ACTION="'TD_SwordAttack01'"] [TD_FRAMES=[1,22,40]]
출력: Saved/BlenderAnimation/SwordAttack01/grip-points.json
상태: 현행 (2026-09-25, 언리얼 좌표 = 100 × (Blender x, -Blender y, Blender z))
"""
import json
from pathlib import Path

import bpy
from mathutils import Vector

WORK = Path('C:/Project/TDGame/Saved/BlenderAnimation/SwordAttack01')
scene = bpy.context.scene
armature = bpy.data.objects['root']
armature.animation_data.action = bpy.data.actions[globals().get('TD_ACTION', 'TD_SwordAttack01')]
frames = globals().get('TD_FRAMES', [1, 12, 18, 22, 26, 30, 40, 60, 80, 99])
sword = bpy.data.objects['TD_SwordPreview']
socket = bpy.data.objects['TD_HandGrip_R']
to_socket = socket.matrix_world.inverted() @ sword.matrix_world
points = [to_socket @ vertex.co for vertex in sword.data.vertices]
tip = min(points, key=lambda point: point.y)
pommel = max(points, key=lambda point: point.y)
guard_band = [point for point in points if -20.0 < point.y < 5.0]
guard_left = min(guard_band, key=lambda point: point.x)
guard_right = max(guard_band, key=lambda point: point.x)
features = {'tip': tip, 'pommel': pommel, 'guard_left': guard_left, 'guard_right': guard_right,
            'guard_center': (guard_left + guard_right) * 0.5}


def unreal_cm(world):
    return [round(world.x * 100.0, 4), round(-world.y * 100.0, 4), round(world.z * 100.0, 4)]


rows = []
for frame in frames:
    scene.frame_set(frame)
    socket_world = socket.matrix_world
    rows.append({'frame': frame, 'time': round((frame - 1) / 60.0, 6), 'points': {name: unreal_cm(socket_world @ local) for name, local in features.items()}})
report = {'action': armature.animation_data.action.name, 'socket_local_features_cm': {name: [round(v, 3) for v in local] for name, local in features.items()}, 'frames': rows}
(WORK / 'grip-points.json').write_text(json.dumps(report, indent=1), encoding='utf-8')
print(json.dumps(report['socket_local_features_cm']))
