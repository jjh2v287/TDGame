import json
from pathlib import Path

import bpy

armature = bpy.data.objects['root']
names = ['lowerarm_r', 'hand_r', 'index_01_r', 'middle_01_r', 'pinky_01_r', 'thumb_01_r', 'thumb_02_r', 'thumb_03_r', 'ik_hand_gun', 'ik_hand_r', 'hand_l', 'ik_hand_l']
rest = {name: armature.data.bones[name].matrix_local.copy() for name in names}
forward = (rest['middle_01_r'].translation - rest['hand_r'].translation).normalized()
grip = (rest['index_01_r'].translation - rest['pinky_01_r'].translation).normalized()
forearm = (rest['hand_r'].translation - rest['lowerarm_r'].translation).normalized()
samples = []
for frame in [1, 11, 18, 23, 26, 36, 44]:
    bpy.context.scene.frame_set(frame)
    bpy.context.view_layer.update()
    samples.append({'frame': frame, 'bones': {name: {'position': list(armature.pose.bones[name].matrix.translation), 'rotation_wxyz': list(armature.pose.bones[name].matrix.to_quaternion())} for name in names}})
report = {'file': bpy.data.filepath, 'dirty': bpy.data.is_dirty,
          'rest': {name: {'position': list(matrix.translation), 'rotation_wxyz': list(matrix.to_quaternion())} for name, matrix in rest.items()},
          'hand_forward': list(forward), 'knuckle_line': list(grip), 'forearm_direction': list(forearm),
          'knuckle_forearm_dot': grip.dot(forearm), 'hand_forearm_dot': forward.dot(forearm),
          'samples': samples}
path = Path('C:/Project/TDGame/Saved/BlenderAnimation/grip-inspection.json')
path.write_text(json.dumps(report, indent=2), encoding='utf-8')
bpy.context.scene.frame_set(1)
print(json.dumps({key: value for key, value in report.items() if key not in ['samples', 'rest']}))
print(json.dumps(report['rest']))
