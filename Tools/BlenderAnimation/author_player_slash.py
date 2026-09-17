import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector


def author(request):
    source = Path(request['source_fbx'])
    output = Path(request['output_directory'])
    name = request['name']
    output.mkdir(parents=True, exist_ok=True)
    blend_path = output / (name + '.blend')
    fbx_path = output / (name + '.fbx')
    if blend_path.exists() or fbx_path.exists():
        raise FileExistsError('Choose a new output name; existing animation sources are preserved')
    if not source.is_file():
        raise FileNotFoundError(source)
    if bpy.data.objects.get('root') is not None:
        raise RuntimeError('Open a new Blender document after saving the current one; Manny requires the original root name')
    bpy.context.window.scene = bpy.data.scenes.new('TD_' + name)
    bpy.ops.import_scene.fbx(filepath=str(source), use_anim=False,
                             automatic_bone_orientation=False, ignore_leaf_bones=False)
    armatures = [obj for obj in bpy.context.scene.objects if obj.type == 'ARMATURE']
    if len(armatures) != 1:
        raise ValueError('Expected one exported skeletal mesh armature')
    armature = armatures[0]
    if armature.name != 'root':
        raise ValueError('The imported Manny armature must preserve the root name')
    bones = armature.pose.bones
    required = ['pelvis', 'spine_01', 'spine_03', 'spine_05', 'head']
    required += [part + side for side in ('_r', '_l') for part in
                 ('upperarm', 'lowerarm', 'hand', 'thigh', 'calf', 'foot', 'ball')]
    missing = [bone for bone in required if bone not in bones]
    if missing:
        raise ValueError('This sample requires the inspected Manny profile: ' + ', '.join(missing))
    rest = {bone.name: bone.matrix.copy() for bone in bones}
    right = (rest['upperarm_r'].translation - rest['upperarm_l'].translation)
    right.z = 0
    right.normalize()
    up = Vector((0, 0, 1))
    forward = (rest['ball_r'].translation - rest['foot_r'].translation)
    forward.z = 0
    forward -= right * forward.dot(right)
    forward.normalize()
    if abs(right.dot(forward)) > 1e-4:
        raise ValueError('Character axes could not be resolved')
    fps = 30
    end = 36
    scene = bpy.context.scene
    scene.render.fps = fps
    scene.render.fps_base = 1
    scene.frame_start = 1
    scene.frame_end = end + 1
    scene.unit_settings.system = 'METRIC'
    armature.animation_data_clear()
    armature.animation_data_create()
    action = bpy.data.actions.new(name)
    armature.animation_data.action = action
    for bone in bones:
        bone.rotation_mode = 'QUATERNION'
    feet = {
        'r': rest['foot_r'].translation + right * 4 - forward * 8,
        'l': rest['foot_l'].translation - right * 4 + forward * 10,
    }
    grip_axis = (rest['index_01_r'].translation - rest['pinky_01_r'].translation).normalized()
    finger_axis = (rest['middle_01_r'].translation - rest['hand_r'].translation).normalized()
    finger_axis = (finger_axis - grip_axis * finger_axis.dot(grip_axis)).normalized()
    rest_hand_basis = Matrix((grip_axis, finger_axis, grip_axis.cross(finger_axis))).transposed()
    keys = [
        (0, 28, 28, 121, 18, -5, 0),
        (7, 48, -4, 123, 80, -20, -2),
        (11, 51, 8, 122, 68, -18, -2),
        (14, 26, 48, 121, 25, -2, -3),
        (17, -14, 36, 122, -26, 18, -3),
        (21, -25, 22, 121, -65, 25, -2),
        (25, -24, 19, 121, -70, 22, -1),
        (36, 28, 28, 121, 18, -5, 0),
    ]

    def interpolate(frame):
        for start, stop in zip(keys, keys[1:]):
            if start[0] <= frame <= stop[0]:
                t = (frame - start[0]) / (stop[0] - start[0])
                t = t * t * (3 - 2 * t)
                return [a + (b - a) * t for a, b in zip(start[1:], stop[1:])]
        return list(keys[-1][1:])

    def rotate_world(bone_name, axis, degrees):
        bone = bones[bone_name]
        matrix = bone.matrix.copy()
        rotation = Quaternion(axis, math.radians(degrees)) @ matrix.to_quaternion()
        bone.matrix = Matrix.LocRotScale(matrix.translation, rotation, matrix.to_scale())
        bpy.context.view_layer.update()

    def solve_chain(upper_name, lower_name, end_name, target, pole):
        upper = bones[upper_name]
        lower = bones[lower_name]
        endpoint = bones[end_name]
        shoulder = upper.matrix.translation.copy()
        length_a = (rest[lower_name].translation - rest[upper_name].translation).length
        length_b = (rest[end_name].translation - rest[lower_name].translation).length
        delta = target - shoulder
        distance = min(delta.length, length_a + length_b - 0.05)
        direction = delta.normalized()
        along = (length_a * length_a - length_b * length_b + distance * distance) / (2 * distance)
        height = math.sqrt(max(0, length_a * length_a - along * along))
        bend = pole - shoulder
        bend -= direction * bend.dot(direction)
        bend.normalize()
        elbow = shoulder + direction * along + bend * height
        current = lower.matrix.translation - shoulder
        rotation = current.rotation_difference(elbow - shoulder) @ upper.matrix.to_quaternion()
        upper.matrix = Matrix.LocRotScale(shoulder, rotation, upper.matrix.to_scale())
        bpy.context.view_layer.update()
        elbow = lower.matrix.translation.copy()
        current = endpoint.matrix.translation - elbow
        rotation = current.rotation_difference(target - elbow) @ lower.matrix.to_quaternion()
        lower.matrix = Matrix.LocRotScale(elbow, rotation, lower.matrix.to_scale())
        bpy.context.view_layer.update()

    samples = []
    previous_rotations = {}
    for frame in range(end + 1):
        scene.frame_set(frame + 1)
        for bone in bones:
            bone.matrix_basis = Matrix.Identity(4)
        bpy.context.view_layer.update()
        hand_right, hand_forward, hand_height, sword_angle, torso, crouch = interpolate(frame)
        pelvis = bones['pelvis'].matrix.copy()
        pelvis.translation += up * (-6 + crouch) + forward * (2 + math.sin(math.pi * frame / end))
        bones['pelvis'].matrix = pelvis
        bpy.context.view_layer.update()
        rotate_world('pelvis', up, torso * 0.22)
        for bone_name in ('spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05'):
            rotate_world(bone_name, up, torso * 0.156)
        rotate_world('head', up, -torso * 0.55)
        for side in ('r', 'l'):
            target = feet[side]
            pole = target + forward * 90 + up * 45
            solve_chain('thigh_' + side, 'calf_' + side, 'foot_' + side, target, pole)
            foot = bones['foot_' + side]
            foot.matrix = Matrix.LocRotScale(target, rest['foot_' + side].to_quaternion(), Vector((1, 1, 1)))
            bpy.context.view_layer.update()
        target = right * hand_right + forward * hand_forward + up * hand_height
        solve_chain('upperarm_r', 'lowerarm_r', 'hand_r', target,
                    right * 85 + forward * 6 + up * 100)
        angle = math.radians(sword_angle)
        blade = right * math.sin(angle) + forward * math.cos(angle)
        desired_fingers = -up
        hand_basis = Matrix((blade, desired_fingers, blade.cross(desired_fingers))).transposed()
        hand_rotation = (hand_basis @ rest_hand_basis.transposed()).to_quaternion() @ rest['hand_r'].to_quaternion()
        bones['hand_r'].matrix = Matrix.LocRotScale(bones['hand_r'].matrix.translation, hand_rotation, Vector((1, 1, 1)))
        bpy.context.view_layer.update()
        left_target = -right * 21 + forward * 10 + up * 145
        solve_chain('upperarm_l', 'lowerarm_l', 'hand_l', left_target,
                    -right * 68 + forward * 4 + up * 105)
        for side in ('r', 'l'):
            local_grip = grip_axis if side == 'r' else -grip_axis
            for finger in ('index', 'middle', 'ring', 'pinky'):
                for segment, curl in ((1, 65), (2, 78), (3, 55)):
                    bone_name = f'{finger}_{segment:02d}_{side}'
                    if bone_name in bones:
                        axis = rest[bone_name].to_quaternion().inverted() @ local_grip
                        bones[bone_name].rotation_quaternion = Quaternion(axis, math.radians(curl))
        bpy.context.view_layer.update()
        for follower, leader in [('ik_foot_r', 'foot_r'), ('ik_foot_l', 'foot_l'),
                                 ('ik_hand_gun', 'hand_r'), ('ik_hand_r', 'hand_r'), ('ik_hand_l', 'hand_l')]:
            if follower in bones:
                bones[follower].matrix = bones[leader].matrix.copy()
                bpy.context.view_layer.update()
        for bone in bones:
            rotation = bone.rotation_quaternion.copy()
            if bone.name in previous_rotations:
                rotation.make_compatible(previous_rotations[bone.name])
                bone.rotation_quaternion = rotation
            previous_rotations[bone.name] = rotation
            bone.keyframe_insert(data_path='location', frame=frame + 1)
            bone.keyframe_insert(data_path='rotation_quaternion', frame=frame + 1)
            bone.keyframe_insert(data_path='scale', frame=frame + 1)
        samples.append({'frame': frame, 'hand_r_cm': list(bones['hand_r'].matrix.translation),
                        'foot_r_cm': list(bones['foot_r'].matrix.translation),
                        'foot_l_cm': list(bones['foot_l'].matrix.translation),
                        'target_error_cm': (bones['hand_r'].matrix.translation - target).length,
                        'blade_direction': list(blade)})
    for layer in action.layers:
        for strip in layer.strips:
            for slot in action.slots:
                bag = strip.channelbag(slot)
                if bag:
                    for curve in bag.fcurves:
                        for key in curve.keyframe_points:
                            key.interpolation = 'LINEAR'
    scene.frame_set(1)
    bpy.ops.object.select_all(action='DESELECT')
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.fbx(filepath=str(fbx_path), use_selection=True,
                             object_types={'ARMATURE'}, add_leaf_bones=False,
                             bake_anim=True, bake_anim_use_all_bones=True,
                             bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                             bake_anim_step=1, bake_anim_simplify_factor=0,
                             axis_forward='-Z', axis_up='Y')
    armature['td_source_mesh'] = request['mesh_asset']
    armature['td_animation_intent'] = 'Right-handed horizontal slash, character right to left, in-place'
    report = {'name': name, 'fps': fps, 'duration_seconds': end / fps,
              'blender_frames': [1, end + 1], 'armature_object': armature.name,
              'bone_count_without_armature_root': len(bones),
              'right_axis': list(right), 'forward_axis': list(forward),
              'source_mesh': request['mesh_asset'], 'skeleton': request['skeleton_asset'],
              'fbx': str(fbx_path), 'blend': str(blend_path), 'samples': samples}
    report_path = output / (name + '.json')
    report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path), check_existing=False)
    print(json.dumps({'blend': str(blend_path), 'fbx': str(fbx_path), 'report': str(report_path),
                      'maximum_hand_error_cm': max(sample['target_error_cm'] for sample in samples)}))
    return report
