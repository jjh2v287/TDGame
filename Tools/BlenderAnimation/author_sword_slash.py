"""Blender 안에서 실행: 플레이어(SKM_Manny_Simple) 한손검 오른손 우→좌 횡베기 기본 공격을 절차적으로 저작한다(발 접지 모델·2본 IK·검 궤적으로 손 방향 결정·루트 모션 전진). 씬의 기존 오브젝트는 모두 제거된다.
실행: Blender MCP execute_blender_code로 본문 전달(`python Tools/BlenderMCP/call_tool.py --code Tools/BlenderAnimation/author_sword_slash.py`). 네임스페이스에 TD_SAVE=True를 주면 .blend/.fbx/.json을 저장하고, 없으면 씬만 만든다.
출력: AnimationSources/Player/AS_TD_Player_Attack01_SwordSlash_RToL.{blend,fbx,json}, Saved/BlenderAnimation/SwordSlash/author-result.json
상태: 현행 (2026-09-18, Manny 전용: 본 이름·치수·HandGrip_R 소켓 고정)
"""
import json
import math
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = Path('C:/Project/TDGame')
NAME = 'AS_TD_Player_Attack01_SwordSlash_RToL'
OUTPUT = ROOT / 'AnimationSources/Player'
WORK = ROOT / 'Saved/BlenderAnimation/SwordSlash'
FPS = 30
FRAME_COUNT = 40
SAVE = bool(globals().get('TD_SAVE', False))
WORK.mkdir(parents=True, exist_ok=True)

HAND_GRIP_R_LOCAL_CM = Vector((-7.012133741056203, -2.0487315208256485, 0.0))
HAND_GRIP_R_LOCAL_QUAT = Quaternion((0.7071054093780323, 0.0, 0.0, -0.7071081529861803))
SWORD_GRIP_OFFSET_M = Vector((0.0, 0.21225492159525552 + 0.11, 0.014))
SWORD_TIP_LOCAL_CM = Vector((0.0, -(0.753124475479126 + SWORD_GRIP_OFFSET_M.y) * 100.0, 0.0))
SWORD_GUARD_LOCAL_CM = Vector((0.0, -0.11 * 100.0, 0.0))


def character(fwd, right, up):
    return Vector((-right, -fwd, up))


def to_character(vector):
    return Vector((-vector.y, -vector.x, vector.z))


def rotation_about(axis, degrees):
    return Quaternion(axis.normalized(), math.radians(degrees))


def yaw_right(degrees):
    return rotation_about(Vector((0, 0, 1)), -degrees)


class Track:
    def __init__(self, keys):
        self.keys = []
        for key in keys:
            frame, value = key[0], key[1]
            tension = key[2] if len(key) > 2 else 1.0
            if isinstance(value, (list, tuple, Vector)):
                value = Vector(value)
            else:
                value = float(value)
            self.keys.append((float(frame), value, tension))
        self.keys.sort(key=lambda item: item[0])

    def _tangent(self, index):
        frame, value, tension = self.keys[index]
        if index == 0 or index == len(self.keys) - 1 or tension == 0:
            return value * 0.0
        before_frame, before_value, _ = self.keys[index - 1]
        after_frame, after_value, _ = self.keys[index + 1]
        if before_value == value or after_value == value:
            return value * 0.0
        return (after_value - before_value) / (after_frame - before_frame) * tension

    def at(self, frame):
        frame = float(frame)
        if frame <= self.keys[0][0]:
            return self.keys[0][1]
        if frame >= self.keys[-1][0]:
            return self.keys[-1][1]
        for index in range(len(self.keys) - 1):
            start, a, _ = self.keys[index]
            end, b, _ = self.keys[index + 1]
            if not start <= frame <= end:
                continue
            span = end - start
            t = (frame - start) / span
            ta = self._tangent(index) * span
            tb = self._tangent(index + 1) * span
            h00 = 2 * t ** 3 - 3 * t ** 2 + 1
            h10 = t ** 3 - 2 * t ** 2 + t
            h01 = -2 * t ** 3 + 3 * t ** 2
            h11 = t ** 3 - t ** 2
            return a * h00 + ta * h10 + b * h01 + tb * h11
        return self.keys[-1][1]


def direction_track(keys):
    return Track([(frame, character(*value).normalized()) for frame, value in keys])


def spherical(center, azimuth_right, elevation, radius):
    azimuth, elevation = math.radians(azimuth_right), math.radians(elevation)
    return center + character(math.cos(elevation) * math.cos(azimuth), math.cos(elevation) * math.sin(azimuth), math.sin(elevation)) * radius


# ---------------------------------------------------------------- choreography (character frame, cm, degrees)
STEP = 50.0
S = STEP
root_fwd = Track([(0, 0), (6, 0), (9, 5), (12, 13), (15, 25), (17, 33), (19, 39), (22, 43), (26, 47), (30, S), (39, S)])

pelvis_pos = Track([
    (0, character(2, 0, 93)), (4, character(0, 3, 92.5)), (8, character(-3, 6, 91)), (11, character(12, 4, 90.5)),
    (13, character(24, 1, 89.5)), (15, character(33, -2, 88)), (17, character(39, -4, 87)), (19, character(42, -5, 87.5)),
    (22, character(44, -4, 89)), (26, character(47, -2, 91)), (31, character(S + 1, 0, 92.5)), (39, character(S + 2, 0, 93))])
pelvis_yaw = Track([(0, 8), (8, 30), (11, 24), (13, 12), (15, -6), (17, -20), (19, -30), (22, -32), (26, -22), (31, -2), (39, 8)])
pelvis_lean = Track([(0, 3), (8, 4), (13, 7), (17, 11), (20, 10), (24, 7), (31, 4), (39, 3)])
pelvis_roll = Track([(0, 0), (8, -4), (12, 2), (17, 4), (22, 2), (31, 0), (39, 0)])
chest_yaw = Track([(0, 15), (5, 35), (8, 52), (11, 50), (13, 40), (15, 18), (17, -8), (19, -36), (21, -50), (24, -50), (28, -30), (32, 2), (39, 15)])
chest_lean = Track([(0, 4), (8, 2), (13, 6), (17, 12), (20, 14), (24, 10), (31, 5), (39, 4)])
chest_side = Track([(0, 0), (8, -6), (15, 0), (19, 6), (24, 4), (31, 0), (39, 0)])
head_yaw = Track([(0, 0), (8, 10), (13, 4), (17, -4), (21, -14), (25, -12), (32, 0), (39, 0)])
head_pitch = Track([(0, 2), (8, 0), (17, 8), (21, 10), (26, 6), (33, 2), (39, 2)])
clavicle_r_protract = Track([(0, 0), (8, -12), (13, -8), (17, 10), (20, 16), (24, 12), (31, 2), (39, 0)])
clavicle_l_protract = Track([(0, 0), (8, 10), (17, -4), (21, -10), (31, 0), (39, 0)])

# right hand around the right shoulder: (azimuth right, elevation, radius); azimuth 0 = forward, 90 = right, 180 = back
hand_r_sph = Track([
    (0, (24, -62, 50)), (4, (75, -34, 44)), (8, (128, -14, 38)), (11, (138, -8, 37)), (13, (110, -2, 42)), (15, (62, -10, 49)),
    (17, (4, -18, 51)), (19, (-44, -21, 50)), (21, (-78, -25, 46)), (24, (-90, -36, 42)), (28, (-52, -50, 44)), (32, (-2, -58, 46)),
    (39, (24, -62, 50))])
blade_dir = direction_track([
    (0, (0.8, -0.15, -0.55)), (4, (0.35, 0.85, 0.15)), (8, (-0.6, 0.7, 0.3)), (11, (-0.85, 0.45, 0.25)),
    (13, (-0.5, 0.85, 0.15)), (15, (0.45, 0.88, 0.05)), (17, (0.9, -0.42, -0.05)), (19, (0.35, -0.93, -0.1)),
    (21, (-0.25, -0.9, -0.3)), (24, (-0.3, -0.7, -0.6)), (28, (0.4, -0.35, -0.75)), (32, (0.7, -0.25, -0.62)), (39, (0.8, -0.15, -0.55))])
edge_dir = direction_track([
    (0, (0.55, -0.1, 0.8)), (4, (0.8, -0.3, 0.3)), (8, (0.6, 0.8, 0.0)), (13, (0.95, 0.2, 0.0)), (15, (0.7, -0.75, 0.0)), (17, (-0.15, -0.99, 0.0)),
    (19, (-0.95, -0.3, 0.0)), (21, (-0.9, 0.4, -0.1)), (24, (-0.6, 0.5, -0.3)), (28, (0.3, 0.2, 0.6)), (32, (0.55, -0.1, 0.8)), (39, (0.55, -0.1, 0.8))])
elbow_r_dir = direction_track([
    (0, (0.1, 0.6, -0.8)), (8, (-0.5, 0.5, -0.7)), (13, (-0.3, 0.6, -0.75)), (15, (0.0, 0.5, -0.85)), (17, (0.1, 0.35, -0.93)),
    (21, (0.3, -0.2, -0.93)), (24, (0.3, -0.3, -0.9)), (30, (0.1, 0.4, -0.9)), (39, (0.1, 0.6, -0.8))])
hand_l_sph = Track([
    (0, (-28, -44, 44)), (8, (8, -20, 40)), (13, (-4, -24, 40)), (17, (-48, -30, 40)), (21, (-74, -30, 40)), (24, (-72, -34, 40)),
    (30, (-44, -40, 42)), (39, (-28, -44, 44))])
elbow_l_dir = direction_track([
    (0, (-0.1, -0.6, -0.8)), (8, (0.2, -0.7, -0.7)), (17, (-0.3, -0.6, -0.75)), (21, (-0.4, -0.4, -0.8)), (30, (-0.1, -0.6, -0.8)),
    (39, (-0.1, -0.6, -0.8))])

# feet: anchor = ball-of-foot ground contact (fwd, right); lift = ankle lift above flat contact; pitch>0 toes up, pitch<0 heel up; yaw right positive
foot_l_anchor = Track([(0, (20, -16)), (5, (20, -16)), (12, (S + 20, -19)), (39, (S + 20, -19))])
foot_l_lift = Track([(0, 0), (5, 0), (7, 6), (9, 7), (11, 2.5), (12, 0), (39, 0)])
foot_l_pitch = Track([(0, 0), (5, 0), (6, -25), (8, -8), (10, 8), (12, 14), (14, 0), (39, 0)])
foot_l_yaw = Track([(0, -4), (5, -4), (12, -14), (39, -14)])
foot_r_anchor = Track([(0, (4, 16)), (21, (4, 16)), (27, (S + 4, 15)), (39, (S + 4, 15))])
foot_r_lift = Track([(0, 0), (21, 0), (23, 6), (25, 5), (27, 0), (39, 0)])
foot_r_pitch = Track([(0, 0), (12, 0), (15, -16), (19, -46), (21, -56), (24, -24), (26, -6), (28, 0), (39, 0)])
foot_r_yaw = Track([(0, 8), (12, 8), (16, 0), (19, -16), (21, -22), (27, 6), (39, 8)])

FIST_R = 0.88
FIST_L = 0.32
SPINE_WEIGHTS = {'spine_01': 0.12, 'spine_02': 0.16, 'spine_03': 0.2, 'spine_04': 0.24, 'spine_05': 0.28}
NECK_WEIGHTS = {'neck_01': 0.3, 'neck_02': 0.3, 'head': 0.4}

# ---------------------------------------------------------------- scene
scene = bpy.context.scene
for obj in list(scene.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
for block in (bpy.data.meshes, bpy.data.armatures, bpy.data.actions, bpy.data.materials):
    for item in list(block):
        if item.users == 0 or block is bpy.data.actions:
            block.remove(item)
dummy = bpy.data.objects.new('TD_ContextDummy', None)
scene.collection.objects.link(dummy)
bpy.context.view_layer.objects.active = dummy
scene.name = 'TD_SwordSlash'
scene.render.fps = FPS
scene.frame_start = 1
scene.frame_end = FRAME_COUNT

bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Saved/BlenderAnimation/SKM_Manny_Simple.fbx'), use_anim=False)
armature = next(obj for obj in scene.objects if obj.type == 'ARMATURE')
if armature.name != 'root':
    raise ValueError('The original root name must be preserved')
fbx_root = armature.parent
world = armature.matrix_world.copy()
armature.parent = None
armature.matrix_world = world
if fbx_root:
    bpy.data.objects.remove(fbx_root, do_unlink=True)
bpy.data.objects.remove(dummy, do_unlink=True)
body_mesh = next(obj for obj in scene.objects if obj.type == 'MESH')
body_mesh.name = 'TD_Body'
body_mesh.color = (0.55, 0.62, 0.68, 1)

before = set(scene.objects)
bpy.context.view_layer.objects.active = armature
bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Saved/BlenderAnimation/Reference/MM_Attack_01.fbx'), use_anim=True)
reference = next(obj for obj in scene.objects if obj.type == 'ARMATURE' and obj not in before)
scene.frame_set(1)
bpy.context.view_layer.update()
fist = {bone.name: bone.matrix_basis.to_quaternion().copy() for bone in reference.pose.bones
        if any(part in bone.name for part in ['thumb_', 'index_', 'middle_', 'ring_', 'pinky_'])}
reference_action = reference.animation_data.action if reference.animation_data else None
for obj in [obj for obj in scene.objects if obj not in before]:
    bpy.data.objects.remove(obj, do_unlink=True)
if reference_action:
    bpy.data.actions.remove(reference_action)

before = set(scene.objects)
bpy.context.view_layer.objects.active = armature
bpy.ops.import_scene.fbx(filepath=str(ROOT / 'Saved/BlenderAnimation/Reference/SM_Sword.fbx'), use_anim=False)
sword = next(obj for obj in scene.objects if obj not in before and obj.type == 'MESH')
sword.data.transform(sword.matrix_world)
sword.parent = None
sword.matrix_world = Matrix.Identity(4)
sword.data.transform(Matrix.Translation(-SWORD_GRIP_OFFSET_M))
sword.name = 'TD_SwordPreview'
sword.color = (0.78, 0.66, 0.34, 1)
sword['source_asset'] = '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword'
for obj in [obj for obj in scene.objects if obj not in before and obj != sword]:
    bpy.data.objects.remove(obj, do_unlink=True)
hand_frame = bpy.data.objects.new('TD_HandFrame_R', None)
scene.collection.objects.link(hand_frame)
for kind in ['COPY_LOCATION', 'COPY_ROTATION']:
    constraint = hand_frame.constraints.new(kind)
    constraint.target = armature
    constraint.subtarget = 'hand_r'
    constraint.owner_space = 'WORLD'
    constraint.target_space = 'WORLD'
socket_frame = bpy.data.objects.new('TD_HandGrip_R', None)
scene.collection.objects.link(socket_frame)
socket_frame.parent = hand_frame
socket_frame.rotation_mode = 'QUATERNION'
socket_frame.location = HAND_GRIP_R_LOCAL_CM * 0.01
socket_frame.rotation_quaternion = HAND_GRIP_R_LOCAL_QUAT
socket_frame.empty_display_type = 'ARROWS'
socket_frame.empty_display_size = 0.12
socket_frame['unreal_socket'] = 'HandGrip_R'
sword.parent = socket_frame
sword.matrix_basis = Matrix.Identity(4)

scene.render.fps = FPS
scene.render.fps_base = 1.0
scene.frame_start = 1
scene.frame_end = FRAME_COUNT
bones = armature.pose.bones
rest = {bone.name: bone.matrix_local.copy() for bone in armature.data.bones}
rest_head = {name: matrix.translation.copy() for name, matrix in rest.items()}
socket_local = Matrix.LocRotScale(HAND_GRIP_R_LOCAL_CM, HAND_GRIP_R_LOCAL_QUAT, Vector((1, 1, 1)))
armature.rotation_mode = 'QUATERNION'
for bone in bones:
    bone.rotation_mode = 'QUATERNION'


# ---------------------------------------------------------------- posing helpers (armature space, cm)
def update():
    bpy.context.view_layer.update()


def set_bone(name, location, rotation):
    bones[name].matrix = Matrix.LocRotScale(location, rotation, Vector((1, 1, 1)))
    update()


def rotate_world(name, axis, degrees):
    if abs(degrees) < 1e-6:
        return
    bone = bones[name]
    matrix = bone.matrix.copy()
    set_bone(name, matrix.translation, rotation_about(axis, degrees) @ matrix.to_quaternion())


def chain_basis(direction, normal):
    direction = direction.normalized()
    normal = (normal - direction * normal.dot(direction)).normalized()
    return Matrix((direction, normal.cross(direction), normal)).transposed()


def follow_rotation(child, parent):
    return bones[parent].matrix.to_quaternion() @ rest[parent].to_quaternion().inverted() @ rest[child].to_quaternion()


def twist_degrees(delta, axis):
    axis = axis.normalized()
    projection = Vector((delta.x, delta.y, delta.z)).dot(axis)
    angle = math.degrees(2 * math.atan2(projection, delta.w))
    while angle > 180:
        angle -= 360
    while angle < -180:
        angle += 360
    return angle


def solve_two_bone(upper, lower, end, target, elbow_point):
    shoulder = bones[upper].matrix.translation.copy()
    upper_rest = rest_head[lower] - rest_head[upper]
    lower_rest = rest_head[end] - rest_head[lower]
    length_a, length_b = upper_rest.length, lower_rest.length
    delta = target - shoulder
    distance = delta.length
    max_reach = length_a + length_b - 0.5
    if distance > max_reach:
        target = shoulder + delta.normalized() * max_reach
        delta = target - shoulder
        distance = max_reach
    if distance < abs(length_a - length_b) + 0.5:
        raise ValueError(f'{end} target too close at frame {scene.frame_current}')
    direction = delta.normalized()
    along = (length_a ** 2 - length_b ** 2 + distance ** 2) / (2 * distance)
    radius = math.sqrt(max(0.0, length_a ** 2 - along ** 2))
    center = shoulder + direction * along
    bend = elbow_point - center
    bend = (bend - direction * bend.dot(direction))
    if bend.length < 1e-4:
        bend = Vector((0, 0, -1)) - direction * direction.z
    elbow = center + bend.normalized() * radius
    normal = (elbow - shoulder).cross(target - elbow).normalized()
    rest_normal = upper_rest.cross(lower_rest).normalized()
    for name, reference_direction, posed_direction, location in [
            (upper, upper_rest, elbow - shoulder, shoulder), (lower, lower_rest, target - elbow, elbow)]:
        rotation = (chain_basis(posed_direction, normal) @ chain_basis(reference_direction, rest_normal).transposed()).to_quaternion()
        set_bone(name, location, rotation @ rest[name].to_quaternion())
    return target, elbow


def elbow_candidates(upper, lower, end, target, hint, steps=24):
    shoulder = bones[upper].matrix.translation.copy()
    direction = (target - shoulder).normalized()
    reference = hint - shoulder
    reference = (reference - direction * reference.dot(direction)).normalized()
    side = direction.cross(reference).normalized()
    for index in range(steps):
        angle = math.radians(-100 + 200 * index / (steps - 1))
        yield shoulder + (reference * math.cos(angle) + side * math.sin(angle)) * 30.0, math.degrees(angle)


ELBOW_MEMORY = {}


def pose_right_arm(target, hand_rotation, hint):
    best = None
    previous_angle = ELBOW_MEMORY.get('r')
    for candidate, angle in elbow_candidates('upperarm_r', 'lowerarm_r', 'hand_r', target, hint):
        reached, elbow = solve_two_bone('upperarm_r', 'lowerarm_r', 'hand_r', target, candidate)
        follow = follow_rotation('hand_r', 'lowerarm_r')
        delta = hand_rotation @ follow.inverted()
        axis = (reached - elbow).normalized()
        twist = twist_degrees(delta, axis)
        swing = math.degrees(2 * math.acos(min(1.0, abs((delta @ rotation_about(axis, -twist)).w))))
        cost = (twist / 45.0) ** 2 + (angle / 30.0) ** 2 + max(0.0, swing - 55.0) ** 2 / 400.0
        if previous_angle is not None:
            cost += ((angle - previous_angle) / 12.0) ** 2
        if best is None or cost < best[0]:
            best = (cost, candidate, twist, swing, angle)
    _, candidate, twist, swing, angle = best
    ELBOW_MEMORY['r'] = angle
    reached, elbow = solve_two_bone('upperarm_r', 'lowerarm_r', 'hand_r', target, candidate)
    set_bone('hand_r', reached, hand_rotation)
    axis = (reached - elbow).normalized()
    for twist_bone, share in [('lowerarm_twist_01_r', 0.62), ('lowerarm_twist_02_r', 0.3)]:
        follow = follow_rotation(twist_bone, 'lowerarm_r')
        head = bones['lowerarm_r'].matrix @ (rest['lowerarm_r'].inverted() @ rest_head[twist_bone])
        set_bone(twist_bone, head, rotation_about(axis, twist * share) @ follow)
    return {'error_cm': (reached - target).length, 'wrist_twist_deg': twist, 'wrist_swing_deg': swing, 'elbow': elbow}


def pose_left_arm(target, hint):
    reached, elbow = solve_two_bone('upperarm_l', 'lowerarm_l', 'hand_l', target, hint)
    set_bone('hand_l', reached, follow_rotation('hand_l', 'lowerarm_l'))
    return {'error_cm': (reached - target).length, 'elbow': elbow}


def sword_rotation(blade, edge):
    blade = blade.normalized()
    edge = (edge - blade * edge.dot(blade)).normalized()
    flat = blade.cross(edge)
    socket_world = Matrix((edge, -blade, flat)).transposed().to_quaternion()
    return socket_world @ HAND_GRIP_R_LOCAL_QUAT.inverted()


FOOT = {}
for side in ('l', 'r'):
    ankle = rest_head['foot_' + side]
    ball = rest_head['ball_' + side]
    heel = ankle + Vector((0, 5.0, -ankle.z + ball.z))
    FOOT[side] = {'ankle': ankle, 'ball': ball, 'heel': heel, 'toe_cm': 16.0}


def pose_leg(side, anchor, lift, pitch, yaw):
    geometry = FOOT[side]
    ankle_rest, ball_rest, heel_rest = geometry['ankle'], geometry['ball'], geometry['heel']
    yaw_rotation = yaw_right(yaw)
    lateral = yaw_rotation @ Vector((1, 0, 0))
    pitch_rotation = rotation_about(lateral, -pitch)
    pivot_rest = heel_rest if pitch > 0 else ball_rest
    ground = character(anchor.x, anchor.y, ball_rest.z)
    ankle = ground + yaw_rotation @ (pivot_rest - ball_rest) + pitch_rotation @ (yaw_rotation @ (ankle_rest - pivot_rest)) + Vector((0, 0, lift))
    foot_rotation = pitch_rotation @ yaw_rotation @ rest['foot_' + side].to_quaternion()
    hip = bones['thigh_' + side].matrix.translation.copy()
    forward = yaw_rotation @ Vector((0, -1, 0))
    knee_hint = (hip + ankle) * 0.5 + forward * 45.0 + Vector((-1 if side == 'r' else 1, 0, 0)) * 6.0
    reached, knee = solve_two_bone('thigh_' + side, 'calf_' + side, 'foot_' + side, ankle, knee_hint)
    set_bone('foot_' + side, reached, foot_rotation)
    toes_on_ground = pitch < 0 and lift <= 0.01
    ball_rotation = yaw_rotation @ rest['ball_' + side].to_quaternion() if toes_on_ground else \
        rotation_about(lateral, 6.0 if lift > 0.01 else 0.0) @ foot_rotation @ rest['foot_' + side].to_quaternion().inverted() @ rest['ball_' + side].to_quaternion()
    ball_head = bones['foot_' + side].matrix @ (rest['foot_' + side].inverted() @ ball_rest)
    set_bone('ball_' + side, ball_head, ball_rotation)
    toe = bones['ball_' + side].matrix @ Vector((0, geometry['toe_cm'], 0))
    return {'error_cm': (reached - ankle).length, 'ankle': reached, 'ball': ball_head, 'toe': toe, 'knee': knee}


def apply_fingers():
    for name, rotation in fist.items():
        influence = FIST_R if name.endswith('_r') else FIST_L
        bones[name].rotation_quaternion = Quaternion().slerp(rotation, influence)
    update()


# ---------------------------------------------------------------- bake
armature.animation_data_clear()
armature.animation_data_create()
armature.animation_data.action = bpy.data.actions.new(NAME)
records = []
previous = {}
for frame in range(FRAME_COUNT):
    scene.frame_set(frame + 1)
    root_offset = character(root_fwd.at(frame), 0, 0)
    armature.location = root_offset * 0.01
    armature.rotation_quaternion = Quaternion()
    armature.scale = Vector((0.01, 0.01, 0.01))
    for bone in bones:
        bone.matrix_basis = Matrix.Identity(4)
    update()

    yaw_p, lean_p, roll_p = pelvis_yaw.at(frame), pelvis_lean.at(frame), pelvis_roll.at(frame)
    pelvis_rotation = yaw_right(yaw_p) @ rotation_about(Vector((1, 0, 0)), lean_p) @ rotation_about(Vector((0, -1, 0)), roll_p)
    set_bone('pelvis', pelvis_pos.at(frame) - root_offset, pelvis_rotation @ rest['pelvis'].to_quaternion())

    yaw_c, lean_c, side_c = chest_yaw.at(frame), chest_lean.at(frame), chest_side.at(frame)
    accumulated = yaw_p
    for name, weight in SPINE_WEIGHTS.items():
        accumulated += (yaw_c - yaw_p) * weight
        lateral = yaw_right(accumulated) @ Vector((1, 0, 0))
        forward = yaw_right(accumulated) @ Vector((0, -1, 0))
        rotate_world(name, Vector((0, 0, 1)), -(yaw_c - yaw_p) * weight)
        rotate_world(name, lateral, lean_c * weight)
        rotate_world(name, forward, side_c * weight)
    yaw_h, pitch_h = head_yaw.at(frame), head_pitch.at(frame)
    accumulated = yaw_c
    for name, weight in NECK_WEIGHTS.items():
        accumulated += (yaw_h - yaw_c) * weight
        lateral = yaw_right(accumulated) @ Vector((1, 0, 0))
        rotate_world(name, Vector((0, 0, 1)), -(yaw_h - yaw_c) * weight)
        rotate_world(name, lateral, (pitch_h - lean_c - lean_p) * weight)
    rotate_world('clavicle_r', Vector((0, 0, 1)), clavicle_r_protract.at(frame))
    rotate_world('clavicle_l', Vector((0, 0, 1)), -clavicle_l_protract.at(frame))

    hand_rotation = sword_rotation(blade_dir.at(frame), edge_dir.at(frame))
    shoulder_r = bones['upperarm_r'].matrix.translation.copy()
    target_r = spherical(shoulder_r, *hand_r_sph.at(frame))
    right = pose_right_arm(target_r, hand_rotation, (shoulder_r + target_r) * 0.5 + elbow_r_dir.at(frame).normalized() * 30.0)
    shoulder_l = bones['upperarm_l'].matrix.translation.copy()
    target_l = spherical(shoulder_l, *hand_l_sph.at(frame))
    left = pose_left_arm(target_l, (shoulder_l + target_l) * 0.5 + elbow_l_dir.at(frame).normalized() * 30.0)
    legs = {}
    for side, anchor, lift, pitch, yaw in [
            ('l', foot_l_anchor, foot_l_lift, foot_l_pitch, foot_l_yaw), ('r', foot_r_anchor, foot_r_lift, foot_r_pitch, foot_r_yaw)]:
        anchor_value = anchor.at(frame) - Vector((root_fwd.at(frame), 0))
        legs[side] = pose_leg(side, anchor_value, lift.at(frame), pitch.at(frame), yaw.at(frame))
    apply_fingers()
    for follower, leader in [('ik_hand_gun', 'hand_r'), ('ik_hand_r', 'hand_r'), ('ik_hand_l', 'hand_l'), ('ik_foot_r', 'foot_r'), ('ik_foot_l', 'foot_l')]:
        bones[follower].matrix = bones[leader].matrix.copy()
        update()

    for path in ['location', 'rotation_quaternion', 'scale']:
        armature.keyframe_insert(data_path=path, frame=frame + 1)
    for bone in bones:
        rotation = bone.rotation_quaternion.copy()
        if bone.name in previous:
            rotation.make_compatible(previous[bone.name])
            bone.rotation_quaternion = rotation
        previous[bone.name] = rotation
        for path in ['location', 'rotation_quaternion', 'scale']:
            bone.keyframe_insert(data_path=path, frame=frame + 1)

    socket_matrix = bones['hand_r'].matrix @ socket_local
    to_world = lambda vector: [round(v, 3) for v in to_character(vector + root_offset)]
    records.append({
        'frame': frame, 'time': frame / FPS, 'root_fwd_cm': round(root_fwd.at(frame), 3),
        'pelvis': to_world(bones['pelvis'].matrix.translation), 'hand_r': to_world(bones['hand_r'].matrix.translation),
        'hand_l': to_world(bones['hand_l'].matrix.translation), 'elbow_r': to_world(right['elbow']),
        'sword_tip': to_world(socket_matrix @ SWORD_TIP_LOCAL_CM), 'sword_guard': to_world(socket_matrix @ SWORD_GUARD_LOCAL_CM),
        'blade_dir': [round(v, 4) for v in to_character(socket_matrix.to_quaternion() @ Vector((0, -1, 0)))],
        'right_arm_error_cm': round(right['error_cm'], 3), 'left_arm_error_cm': round(left['error_cm'], 3),
        'wrist_twist_deg': round(right['wrist_twist_deg'], 2), 'wrist_swing_deg': round(right['wrist_swing_deg'], 2),
        'feet': {side: {'ankle': to_world(legs[side]['ankle']), 'ball': to_world(legs[side]['ball']), 'toe': to_world(legs[side]['toe']),
                        'knee': to_world(legs[side]['knee']), 'leg_error_cm': round(legs[side]['error_cm'], 3),
                        'lift': round(lift_track.at(frame), 3), 'pitch': round(pitch_track.at(frame), 2)}
                 for side, lift_track, pitch_track in [('l', foot_l_lift, foot_l_pitch), ('r', foot_r_lift, foot_r_pitch)]},
    })

for layer in armature.animation_data.action.layers:
    for strip in layer.strips:
        for slot in armature.animation_data.action.slots:
            bag = strip.channelbag(slot)
            if bag:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:
                        key.interpolation = 'LINEAR'
scene.frame_set(1)
armature['td_authoring'] = 'procedural key poses; foot contact model; sword-driven wrist; root motion forward step'
armature['td_binding_socket'] = 'HandGrip_R'
weapon_relative_location_cm = [-SWORD_GRIP_OFFSET_M.x * 100, SWORD_GRIP_OFFSET_M.y * 100, -SWORD_GRIP_OFFSET_M.z * 100]
armature['td_weapon_relative_location_cm'] = weapon_relative_location_cm

summary = {
    'name': NAME, 'fps': FPS, 'frames': FRAME_COUNT, 'duration': (FRAME_COUNT - 1) / FPS, 'step_cm': STEP,
    'max_right_arm_error_cm': max(row['right_arm_error_cm'] for row in records),
    'max_left_arm_error_cm': max(row['left_arm_error_cm'] for row in records),
    'max_leg_error_cm': max(max(row['feet'][side]['leg_error_cm'] for side in ('l', 'r')) for row in records),
    'max_wrist_twist_deg': max(abs(row['wrist_twist_deg']) for row in records),
    'max_wrist_swing_deg': max(row['wrist_swing_deg'] for row in records),
}
report = {'summary': summary, 'weapon_attachment': {
    'socket': 'HandGrip_R', 'parent_bone': 'hand_r', 'static_mesh': '/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword',
    'unreal_weapon_relative_location_cm': weapon_relative_location_cm, 'unreal_weapon_relative_rotation_degrees': [0, 0, 0]},
    'records': records}
(WORK / 'author-result.json').write_text(json.dumps(report, indent=1), encoding='utf-8')
if SAVE:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT')
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.fbx(filepath=str(OUTPUT / (NAME + '.fbx')), use_selection=True, object_types={'ARMATURE'},
                             add_leaf_bones=False, bake_anim=True, bake_anim_use_all_bones=True,
                             bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
                             bake_anim_step=1, bake_anim_simplify_factor=0, axis_forward='-Z', axis_up='Y')
    (OUTPUT / (NAME + '.json')).write_text(json.dumps(report, indent=1), encoding='utf-8')
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT / (NAME + '.blend')), check_existing=False)
print(json.dumps(summary))
