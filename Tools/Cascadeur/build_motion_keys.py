"""시스템 Python에서 실행: 동작 명세(*.motion.json, 캐릭터 좌표 [오른쪽, 위, 정면] cm·도)를 Cascadeur 리그 기준 자세(rest_pose.json) 위에서 FK·2관절 IK로 풀어 리그 점 43개·손가락 박스 회전·루트 위치 키(JSON)로 바꾸고 칼끝·도달 오차·접지 보고를 낸다(라이브러리 겸 명령줄).
실행: python Tools/Cascadeur/build_motion_keys.py describe --rest <rest_pose.json> --weapon Tools/Cascadeur/weapons/SM_Sword.json | solve --rest <rest_pose.json> --weapon <무기.json> --spec <motion.json> --out <keys.json>  (PowerShell)
출력: keys.json(csc_motion.build_animation_scene 입력), 보고 JSON(표준 출력, solve는 keys.json 옆 *.solve.json에도 저장)
상태: 현행
"""
import argparse
import copy
import json
import math
import re
from pathlib import Path

import numpy as np

CHARACTER_TO_CASCADEUR = np.diag([-1.0, 1.0, 1.0])
POINT_NAME = re.compile(r"^(?P<bone>.+?)_(?:Main|Additional|Direction|Self\d+)Point(?:_(?P<side>[lr]))?$")
BOX_NAME = re.compile(r"^(?P<bone>.+?)_Box(?:_(?P<side>[lr]))?$")
SPINE_WEIGHTS = {"spine_01": 0.12, "spine_02": 0.18, "spine_03": 0.25, "spine_04": 0.25, "spine_05": 0.20}
NECK_WEIGHTS = {"neck_01": 0.3, "neck_02": 0.3, "head": 0.4}
FINGERS = ("thumb", "index", "middle", "ring", "pinky")
FIST_CURL_DEGREES = {"thumb": (15.0, 35.0, 40.0), "index": (70.0, 95.0, 60.0), "middle": (78.0, 95.0, 60.0), "ring": (82.0, 95.0, 60.0), "pinky": (86.0, 95.0, 60.0)}
DEFAULT_INTERPOLATION = "clamped_bezier"
HEAD_YAW_LIMIT = 85.0
HEAD_PITCH_LIMIT = 50.0
COMFORTABLE_FOREARM_TWIST_DEGREES = 50.0
FOREARM_TWIST_COST_PER_DEGREE = 1.5
ELBOW_RAISE_FREE_CM = 5.0
ELBOW_RAISE_COST_PER_CM = 1.5
EDGE_DEVIATION_COST_PER_DEGREE = 0.15
POSITION_SEARCH_COARSE_STEP_CM = 8.0
POSITION_SEARCH_FINE_STEP_CM = 4.0
POSITION_SEARCH_COST_PER_CM = 0.6
REACH_ERROR_COST_PER_CM = 10.0
ELBOW_FOLD_COST_PER_DEGREE = 0.8
MINIMUM_COMFORTABLE_ELBOW_INTERIOR_DEGREES = 75.0
PER_KEY_OPTIONS = ("position_search", "edge_tolerance")
SWEEP_AXES = {"sweep_cw": np.array((0.0, -1.0, 0.0)), "sweep_ccw": np.array((0.0, 1.0, 0.0))}
POSE_COMPONENTS = ("root", "pelvis", "spine", "head", "clavicle_l", "clavicle_r", "hand_l", "hand_r", "foot_l", "foot_r", "fingers_l", "fingers_r")


def to_cascadeur(vector):
    return CHARACTER_TO_CASCADEUR @ np.asarray(vector, dtype=np.float64)


def to_character(vector):
    return CHARACTER_TO_CASCADEUR @ np.asarray(vector, dtype=np.float64)


def normalized(vector):
    length = np.linalg.norm(vector)
    if length < 1e-9:
        raise ValueError(f"Zero-length direction: {vector}")
    return vector / length


def quaternion_wxyz_to_matrix(quaternion):
    w, x, y, z = quaternion
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def matrix_to_quaternion_wxyz(matrix):
    trace = np.trace(matrix)
    if trace > 0:
        s = math.sqrt(trace + 1.0) * 2
        quaternion = [0.25 * s, (matrix[2, 1] - matrix[1, 2]) / s, (matrix[0, 2] - matrix[2, 0]) / s, (matrix[1, 0] - matrix[0, 1]) / s]
    elif matrix[0, 0] > matrix[1, 1] and matrix[0, 0] > matrix[2, 2]:
        s = math.sqrt(1.0 + matrix[0, 0] - matrix[1, 1] - matrix[2, 2]) * 2
        quaternion = [(matrix[2, 1] - matrix[1, 2]) / s, 0.25 * s, (matrix[0, 1] + matrix[1, 0]) / s, (matrix[0, 2] + matrix[2, 0]) / s]
    elif matrix[1, 1] > matrix[2, 2]:
        s = math.sqrt(1.0 + matrix[1, 1] - matrix[0, 0] - matrix[2, 2]) * 2
        quaternion = [(matrix[0, 2] - matrix[2, 0]) / s, (matrix[0, 1] + matrix[1, 0]) / s, 0.25 * s, (matrix[1, 2] + matrix[2, 1]) / s]
    else:
        s = math.sqrt(1.0 + matrix[2, 2] - matrix[0, 0] - matrix[1, 1]) * 2
        quaternion = [(matrix[1, 0] - matrix[0, 1]) / s, (matrix[0, 2] + matrix[2, 0]) / s, (matrix[1, 2] + matrix[2, 1]) / s, 0.25 * s]
    quaternion = np.array(quaternion)
    return (quaternion / np.linalg.norm(quaternion)).tolist()


def quaternion_xyzw_rotate(quaternion, vector):
    x, y, z, w = quaternion
    return quaternion_wxyz_to_matrix((w, x, y, z)) @ np.asarray(vector, dtype=np.float64)


def unreal_rotator_to_quaternion_xyzw(roll, pitch, yaw):
    sp, cp = math.sin(math.radians(pitch) / 2), math.cos(math.radians(pitch) / 2)
    sy, cy = math.sin(math.radians(yaw) / 2), math.cos(math.radians(yaw) / 2)
    sr, cr = math.sin(math.radians(roll) / 2), math.cos(math.radians(roll) / 2)
    return (cr * sp * sy - sr * cp * cy, -cr * sp * cy - sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy)


def axis_angle_matrix(axis, degrees):
    axis = normalized(np.asarray(axis, dtype=np.float64))
    angle = math.radians(degrees)
    x, y, z = axis
    c, s, t = math.cos(angle), math.sin(angle), 1 - math.cos(angle)
    return np.array([[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
                     [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
                     [t * x * z - s * y, t * y * z + s * x, t * z * z + c]])


def character_rotation(yaw=0.0, pitch=0.0, roll=0.0):
    return axis_angle_matrix((0, 1, 0), yaw) @ axis_angle_matrix((1, 0, 0), pitch) @ axis_angle_matrix((0, 0, 1), roll)


def frame_matrix(primary, secondary):
    first = normalized(primary)
    second = normalized(secondary - (secondary @ first) * first)
    return np.column_stack([first, second, np.cross(first, second)])


def mapping_rotation(rest_primary, rest_secondary, primary, secondary):
    return frame_matrix(primary, secondary) @ frame_matrix(rest_primary, rest_secondary).T


def angle_between_degrees(first, second):
    return math.degrees(math.acos(float(np.clip(normalized(first) @ normalized(second), -1.0, 1.0))))


def owner_bone(name, pattern):
    match = pattern.match(name)
    if not match:
        return None
    return match.group("bone") + (f"_{match.group('side')}" if match.group("side") else "")


class TDRestRig:
    def __init__(self, rest):
        objects = rest["objects"]
        self.joint_position = {}
        self.joint_rotation = {}
        self.joint_parent = {}
        self.point_position = {}
        self.point_owner = {}
        self.box_rotation = {}
        self.box_owner = {}
        for name, item in objects.items():
            transform = item["frames"]["0"]
            if item["type"] == "Joint":
                self.joint_position[name] = np.array(transform["position"], dtype=np.float64)
                self.joint_rotation[name] = quaternion_wxyz_to_matrix(transform["quaternion_wxyz"])
                self.joint_parent[name] = item["parent"]
            elif item["type"] == "Point":
                self.point_position[name] = np.array(transform["position"], dtype=np.float64)
        for name, item in objects.items():
            if item["type"] == "Point":
                self.point_owner[name] = owner_bone(name, POINT_NAME)
            elif item["type"] == "Box" and "quaternion_wxyz" in item["frames"]["0"]:
                bone = owner_bone(name, BOX_NAME)
                if bone and bone.split("_")[0] in FINGERS:
                    self.box_owner[name] = bone
                    self.box_rotation[name] = quaternion_wxyz_to_matrix(item["frames"]["0"]["quaternion_wxyz"])
        missing = sorted(name for name, bone in list(self.point_owner.items()) + list(self.box_owner.items()) if bone not in self.joint_position)
        if missing:
            raise ValueError(f"Rig controllers without a matching joint: {missing}")
        for name, parent in self.joint_parent.items():
            if parent not in self.joint_position:
                self.joint_parent[name] = None
        self.joint_order = self.topological_order()
        self.finger_axes = {side: {finger: self.finger_flexion_axis(finger, side) for finger in FINGERS} for side in "lr"}
        self.hand_axis_rest = {side: normalized(self.point_position[f"hand_DirectionPoint_{side}"] - self.point_position[f"hand_MainPoint_{side}"]) for side in "lr"}

    def topological_order(self):
        order, placed = [], set()
        while len(order) < len(self.joint_position):
            progressed = False
            for name, parent in self.joint_parent.items():
                if name not in placed and (parent is None or parent in placed):
                    order.append(name)
                    placed.add(name)
                    progressed = True
            if not progressed:
                raise ValueError("Joint hierarchy has a cycle")
        return order

    def finger_flexion_axis(self, finger, side):
        first, second, third = (self.joint_position[f"{finger}_0{index}_{side}"] for index in (1, 2, 3))
        return normalized(np.cross(normalized(second - first), normalized(third - second)))

    def length(self, first, second):
        return float(np.linalg.norm(self.joint_position[second] - self.joint_position[first]))


class TDWeaponGrip:
    def __init__(self, rig, weapon):
        socket = weapon["socket"]
        socket_rotation = unreal_rotator_to_quaternion_xyzw(**socket["relative_rotation_deg"])
        attachment_rotation = weapon["attachment"]["relative_rotation_quat_xyzw"]
        attachment_location = np.array(weapon["attachment"]["relative_location_cm"])
        socket_location = np.array(socket["relative_location_cm"])
        mirror = np.diag([1.0, -1.0, 1.0])
        self.hand_bone = weapon["hand_bone"]
        self.local = {}
        for name, point in weapon["mesh_features_cm"].items():
            in_socket = quaternion_xyzw_rotate(attachment_rotation, point) + attachment_location
            in_hand_unreal = quaternion_xyzw_rotate(socket_rotation, in_socket) + socket_location
            self.local[name] = mirror @ in_hand_unreal
        rest_rotation = rig.joint_rotation[self.hand_bone]
        blade_local = normalized(self.local["tip"] - self.local["guard_center"])
        guard_local = normalized(self.local["guard_right"] - self.local["guard_left"])
        side = self.hand_bone.rsplit("_", 1)[-1]
        finger_direction = normalized(rig.point_position[f"hand_DirectionPoint_{side}"] - rig.point_position[f"hand_MainPoint_{side}"])
        finger_local = rest_rotation.T @ finger_direction
        knuckle_local = finger_local - (finger_local @ blade_local) * blade_local
        guard_perpendicular = guard_local - (guard_local @ blade_local) * blade_local
        self.true_edge_local = normalized(guard_perpendicular if guard_perpendicular @ knuckle_local >= 0 else -guard_perpendicular)
        self.blade_local = blade_local
        self.blade_rest = rest_rotation @ blade_local
        self.edge_rest = rest_rotation @ self.true_edge_local
        self.blade_length = float(np.linalg.norm(self.local["tip"] - self.local["guard_center"]))
        self.blade_angle_to_hand_axis = angle_between_degrees(blade_local, finger_local)

    def hand_delta(self, blade_direction, edge_direction):
        return mapping_rotation(self.blade_rest, self.edge_rest, blade_direction, edge_direction)

    def world_points(self, hand_position, hand_delta, rig):
        rotation = hand_delta @ rig.joint_rotation[self.hand_bone]
        return {name: hand_position + rotation @ local for name, local in self.local.items()}


def solve_two_bone(start, target, upper_length, lower_length, pole):
    offset = target - start
    distance = float(np.linalg.norm(offset))
    direction = normalized(offset)
    reach = min(max(distance, abs(upper_length - lower_length) + 1e-3), upper_length + lower_length - 1e-3)
    cosine = (upper_length ** 2 + reach ** 2 - lower_length ** 2) / (2 * upper_length * reach)
    angle = math.acos(float(np.clip(cosine, -1.0, 1.0)))
    bend = pole - (pole @ direction) * direction
    if np.linalg.norm(bend) < 1e-6:
        bend = np.cross(direction, (0.0, 1.0, 0.0)) if abs(direction[1]) < 0.9 else np.cross(direction, (1.0, 0.0, 0.0))
    bend = normalized(bend)
    middle = start + upper_length * (math.cos(angle) * direction + math.sin(angle) * bend)
    return middle, bend, distance - reach


class TDPoseSolver:
    def __init__(self, rig, grip):
        self.rig = rig
        self.grip = grip

    def chain_rest(self, first, second, third):
        position = self.rig.joint_position
        upper = normalized(position[second] - position[first])
        lower = normalized(position[third] - position[second])
        return upper, lower, normalized(np.cross(upper, lower))

    def limb_solution(self, start, first, second, third, target, pole):
        rig = self.rig
        middle, bend, reach_error = solve_two_bone(start, target, rig.length(first, second), rig.length(second, third), pole)
        upper_rest, lower_rest, normal_rest = self.chain_rest(first, second, third)
        end = middle + rig.length(second, third) * normalized(target - middle) if np.linalg.norm(target - middle) > 1e-6 else target
        upper = normalized(middle - start)
        lower = normalized(end - middle)
        normal = np.cross(upper, lower)
        normal = normalized(normal) if np.linalg.norm(normal) > 1e-4 else normalized(np.cross(upper, bend))
        return {"middle": middle, "reach_error": reach_error, "bend": 180.0 - angle_between_degrees(upper, lower),
                "upper_delta": mapping_rotation(upper_rest, normal_rest, upper, normal), "lower_delta": mapping_rotation(lower_rest, normal_rest, lower, normal)}

    def apply_limb(self, positions, deltas, first, second, solution):
        deltas[first] = solution["upper_delta"]
        deltas[second] = solution["lower_delta"]
        positions[second] = solution["middle"]

    def wrist_angles(self, side, lower_delta, hand_delta):
        relative = lower_delta.T @ hand_delta
        axis = self.rig.hand_axis_rest[side]
        moved = relative @ axis
        swing = angle_between_degrees(axis, moved)
        swing_axis = np.cross(axis, moved)
        swing_rotation = axis_angle_matrix(swing_axis, swing) if np.linalg.norm(swing_axis) > 1e-8 else np.eye(3)
        twist_rotation = swing_rotation.T @ relative
        reference = normalized(np.cross(axis, (0.0, 1.0, 0.0)) if abs(axis[1]) < 0.9 else np.cross(axis, (1.0, 0.0, 0.0)))
        turned = twist_rotation @ reference
        twist = math.degrees(math.atan2(float(np.cross(reference, turned) @ axis), float(reference @ turned)))
        return swing, twist

    def wrist_cost(self, side, start, target, pole, hand_delta):
        solution = self.limb_solution(start, f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}", target, pole)
        swing, twist = self.wrist_angles(side, solution["lower_delta"], hand_delta)
        elbow_raise = solution["middle"][1] - start[1] - ELBOW_RAISE_FREE_CM
        return (swing + FOREARM_TWIST_COST_PER_DEGREE * max(0.0, abs(twist) - COMFORTABLE_FOREARM_TWIST_DEGREES)
                + ELBOW_FOLD_COST_PER_DEGREE * max(0.0, MINIMUM_COMFORTABLE_ELBOW_INTERIOR_DEGREES - solution["bend"])
                + ELBOW_RAISE_COST_PER_CM * max(0.0, elbow_raise)
                + REACH_ERROR_COST_PER_CM * abs(solution["reach_error"]))

    def automatic_elbow(self, side, start, target, hand_delta, coarse_step=10, fine_step=1):
        axis = normalized(target - start)
        base = normalized(np.cross(axis, (0.0, 1.0, 0.0)) if abs(axis[1]) < 0.95 else np.cross(axis, (1.0, 0.0, 0.0)))

        def candidate(degrees):
            pole = axis_angle_matrix(axis, degrees) @ base
            return self.wrist_cost(side, start, target, pole, hand_delta), degrees, pole

        coarse = min((candidate(degrees) for degrees in range(0, 360, coarse_step)), key=lambda item: item[0])
        return min((candidate(coarse[1] + offset) for offset in range(-coarse_step, coarse_step + 1, fine_step)), key=lambda item: item[0])

    def choose_grip_hand(self, side, start, target, hand):
        blade = normalized(to_cascadeur(hand["blade"]))
        edge = hand.get("edge", "auto")
        if isinstance(edge, str) and edge in SWEEP_AXES:
            edge = to_character(np.cross(SWEEP_AXES[edge], blade)).tolist()
        if edge == "auto":
            reference = np.cross(blade, (0.0, 1.0, 0.0)) if abs(blade[1]) < 0.95 else np.cross(blade, (1.0, 0.0, 0.0))
            rolls = range(0, 360, 15)
        else:
            reference = to_cascadeur(edge)
            tolerance = int(hand.get("edge_tolerance", 0))
            rolls = range(-tolerance, tolerance + 1, 5) if tolerance else (0,)
        reference = normalized(reference - (reference @ blade) * blade)
        elbow = hand.get("elbow", "auto")
        radius = float(hand.get("position_search", 0.0))
        hand_deltas = [(roll, self.grip.hand_delta(blade, axis_angle_matrix(blade, roll) @ reference)) for roll in rolls]

        def evaluate(offset, roll, hand_delta, fine):
            candidate_target = target + offset
            if elbow == "auto":
                cost, _, pole = self.automatic_elbow(side, start, candidate_target, hand_delta, *((10, 1) if fine else (30, 10)))
            else:
                pole = to_cascadeur(elbow)
                cost = self.wrist_cost(side, start, candidate_target, pole, hand_delta)
            preference = 0.0 if edge == "auto" else EDGE_DEVIATION_COST_PER_DEGREE * abs(roll)
            return cost + preference + POSITION_SEARCH_COST_PER_CM * float(np.linalg.norm(offset)), hand_delta, pole, roll, candidate_target

        coarse_offsets = sphere_offsets(radius, POSITION_SEARCH_COARSE_STEP_CM)
        coarse = min((evaluate(offset, roll, hand_delta, False) for offset in coarse_offsets for roll, hand_delta in hand_deltas), key=lambda item: item[0])
        best_offset = coarse[4] - target
        fine_offsets = [best_offset + offset for offset in sphere_offsets(POSITION_SEARCH_COARSE_STEP_CM if radius > 0 else 0.0, POSITION_SEARCH_FINE_STEP_CM)]
        near_rolls = [(roll, hand_delta) for roll, hand_delta in hand_deltas if abs(roll - coarse[3]) <= 10] if edge != "auto" else [(coarse[3], coarse[1])]
        best = min((evaluate(offset, roll, hand_delta, True) for offset in fine_offsets for roll, hand_delta in near_rolls), key=lambda item: item[0])
        return best[1], best[2], best[3], best[4]

    def knee_pole(self, foot_spec, foot_delta):
        if foot_spec.get("knee", "auto") != "auto":
            return to_cascadeur(foot_spec["knee"])
        return foot_delta @ np.array((0.0, 0.0, 1.0))

    def ankle_target(self, side, foot_spec, foot_delta, previous_balls):
        rig = self.rig
        ball_offset = foot_delta @ (rig.joint_position[f"ball_{side}"] - rig.joint_position[f"foot_{side}"])
        if foot_spec.get("pivot") == "ball":
            if side not in previous_balls:
                raise ValueError(f"foot_{side} pivot=ball needs an earlier key")
            return previous_balls[side] - ball_offset
        if "ball" in foot_spec:
            return to_cascadeur(foot_spec["ball"]) - ball_offset
        return to_cascadeur(foot_spec["position"])

    def look_angles(self, head_spec, chest_delta, neck_position):
        if "look_at" not in head_spec:
            return head_spec.get("yaw", 0.0), head_spec.get("pitch", 0.0), head_spec.get("roll", 0.0)
        local = chest_delta.T @ normalized(to_cascadeur(head_spec["look_at"]) - neck_position)
        yaw = math.degrees(math.atan2(local[0], local[2]))
        pitch = -math.degrees(math.atan2(local[1], math.hypot(local[0], local[2])))
        return float(np.clip(yaw, -HEAD_YAW_LIMIT, HEAD_YAW_LIMIT)), float(np.clip(pitch, -HEAD_PITCH_LIMIT, HEAD_PITCH_LIMIT)), head_spec.get("roll", 0.0)

    def clavicle_delta(self, side, spec):
        raise_degrees = spec.get("raise", 0.0)
        forward_degrees = spec.get("forward", 0.0)
        sign = -1.0 if side == "r" else 1.0
        return axis_angle_matrix((0, 1, 0), -sign * forward_degrees) @ axis_angle_matrix((0, 0, 1), sign * raise_degrees)

    def solve(self, pose, previous_balls=None):
        previous_balls = previous_balls or {}
        rig = self.rig
        positions, deltas, hand_deltas = {}, {}, {}
        report = {"reach_error_cm": {}, "joint_interior_degrees": {}, "elbow_pole_character": {}, "wrist_degrees": {}}
        identity = np.eye(3)
        for joint in rig.joint_order:
            parent = rig.joint_parent[joint]
            parent_delta = deltas.get(parent, identity)
            if parent is not None and joint not in positions:
                positions[joint] = positions[parent] + parent_delta @ (rig.joint_position[joint] - rig.joint_position[parent])
            if joint == "root":
                positions[joint] = to_cascadeur(pose["root"]["position"])
                deltas[joint] = identity
            elif joint == "pelvis":
                pelvis = pose["pelvis"]
                positions[joint] = to_cascadeur(pelvis["position"])
                deltas[joint] = character_rotation(pelvis.get("yaw", 0.0), pelvis.get("pitch", 0.0), pelvis.get("roll", 0.0))
            elif joint in SPINE_WEIGHTS:
                weight = SPINE_WEIGHTS[joint]
                spine = pose["spine"]
                deltas[joint] = parent_delta @ character_rotation(weight * spine.get("yaw", 0.0), weight * spine.get("pitch", 0.0), weight * spine.get("roll", 0.0))
            elif joint in NECK_WEIGHTS:
                if joint == "neck_01":
                    report["head_angles"] = self.look_angles(pose["head"], parent_delta, positions[joint])
                yaw, pitch, roll = report["head_angles"]
                weight = NECK_WEIGHTS[joint]
                deltas[joint] = parent_delta @ character_rotation(weight * yaw, weight * pitch, weight * roll)
            elif joint in ("clavicle_l", "clavicle_r"):
                deltas[joint] = parent_delta @ self.clavicle_delta(joint[-1], pose[joint])
            elif joint in ("upperarm_l", "upperarm_r"):
                side = joint[-1]
                hand = pose[f"hand_{side}"]
                target = to_cascadeur(hand["position"])
                elbow = hand.get("elbow", "auto")
                if f"hand_{side}" == self.grip.hand_bone:
                    hand_deltas[side], pole, edge_roll, target = self.choose_grip_hand(side, positions[joint], target, hand)
                    report["edge_roll_degrees"] = edge_roll
                elif elbow != "auto":
                    pole = to_cascadeur(elbow)
                else:
                    pole = deltas["spine_05"] @ normalized(np.array((0.5 if side == "l" else -0.5, -1.0, -0.6)))
                solution = self.limb_solution(positions[joint], joint, f"lowerarm_{side}", f"hand_{side}", target, pole)
                self.apply_limb(positions, deltas, joint, f"lowerarm_{side}", solution)
                report["reach_error_cm"][f"arm_{side}"] = round(solution["reach_error"], 2)
                report["joint_interior_degrees"][f"elbow_{side}"] = round(solution["bend"], 1)
                report["elbow_pole_character"][side] = [round(value, 2) for value in to_character(pole)]
            elif joint in ("lowerarm_l", "lowerarm_r"):
                pass
            elif joint in ("hand_l", "hand_r"):
                side = joint[-1]
                if side not in hand_deltas:
                    wrist = pose[joint].get("wrist", {})
                    hand_deltas[side] = parent_delta @ character_rotation(wrist.get("yaw", 0.0), wrist.get("pitch", 0.0), wrist.get("roll", 0.0))
                deltas[joint] = hand_deltas[side]
                swing, twist = self.wrist_angles(side, deltas[f"lowerarm_{side}"], deltas[joint])
                report["wrist_degrees"][side] = {"swing": round(swing, 1), "twist": round(twist, 1)}
            elif joint in ("thigh_l", "thigh_r"):
                side = joint[-1]
                foot = pose[f"foot_{side}"]
                foot_delta = character_rotation(foot.get("yaw", 0.0), foot.get("pitch", 0.0), foot.get("roll", 0.0))
                solution = self.limb_solution(positions[joint], joint, f"calf_{side}", f"foot_{side}", self.ankle_target(side, foot, foot_delta, previous_balls), self.knee_pole(foot, foot_delta))
                self.apply_limb(positions, deltas, joint, f"calf_{side}", solution)
                report["reach_error_cm"][f"leg_{side}"] = round(solution["reach_error"], 2)
                report["joint_interior_degrees"][f"knee_{side}"] = round(solution["bend"], 1)
            elif joint in ("calf_l", "calf_r"):
                pass
            elif joint in ("foot_l", "foot_r"):
                foot = pose[joint]
                deltas[joint] = character_rotation(foot.get("yaw", 0.0), foot.get("pitch", 0.0), foot.get("roll", 0.0))
            elif joint in ("ball_l", "ball_r"):
                deltas[joint] = parent_delta @ axis_angle_matrix((1, 0, 0), -pose[f"foot_{joint[-1]}"].get("toe", 0.0))
            elif self.finger_parts(joint):
                finger, index, side = self.finger_parts(joint)
                curl = pose[f"fingers_{side}"].get("curl", 0.0) * FIST_CURL_DEGREES[finger][index - 1]
                deltas[joint] = parent_delta @ axis_angle_matrix(rig.finger_axes[side][finger], curl)
            else:
                deltas[joint] = parent_delta
        return positions, deltas, report

    @staticmethod
    def finger_parts(joint):
        match = re.match(r"^(thumb|index|middle|ring|pinky)_0([123])_([lr])$", joint)
        return (match.group(1), int(match.group(2)), match.group(3)) if match else None

    def controller_values(self, positions, deltas):
        rig = self.rig
        points = {}
        for name, bone in rig.point_owner.items():
            points[name] = (positions[bone] + deltas[bone] @ (rig.point_position[name] - rig.joint_position[bone])).tolist()
        boxes = {name: {"position": positions[bone].tolist(), "quaternion_wxyz": matrix_to_quaternion_wxyz(deltas[bone] @ rig.box_rotation[name])}
                 for name, bone in rig.box_owner.items()}
        return points, boxes


EXCLUSIVE_FIELDS = (("position", "position_body", "ball", "ball_body", "pivot"), ("blade", "blade_body"), ("edge", "edge_body"),
                    ("elbow", "elbow_body"), ("knee", "knee_body"), ("look_at", "look_at_body", "yaw", "pitch"))
BODY_POINTS = {"position_body": "position", "ball_body": "ball", "look_at_body": "look_at"}
BODY_DIRECTIONS = {"blade_body": "blade", "edge_body": "edge", "elbow_body": "elbow", "knee_body": "knee"}


def sphere_offsets(radius, step):
    if radius <= 0:
        return [np.zeros(3)]
    span = np.arange(-radius, radius + 1e-6, step)
    return [np.array((x, y, z)) for x in span for y in span for z in span if x * x + y * y + z * z <= radius * radius + 1e-6]


def merge_component(current, update):
    merged = {field: value for field, value in current.items() if field not in PER_KEY_OPTIONS}
    for group in EXCLUSIVE_FIELDS:
        if any(field in update for field in group):
            for field in group:
                merged.pop(field, None)
    merged.update(copy.deepcopy(update))
    return merged


def facing_rotation(degrees):
    return CHARACTER_TO_CASCADEUR @ axis_angle_matrix((0, 1, 0), degrees) @ CHARACTER_TO_CASCADEUR


def resolve_body_fields(pose, facing_degrees):
    rotation = facing_rotation(facing_degrees)
    root = np.array(pose["root"]["position"], dtype=np.float64)
    resolved = {}
    for name, component in pose.items():
        component = dict(component)
        for body_field, world_field in BODY_POINTS.items():
            if body_field in component:
                component[world_field] = (root + rotation @ np.array(component.pop(body_field), dtype=np.float64)).tolist()
        for body_field, world_field in BODY_DIRECTIONS.items():
            if body_field in component and component[body_field] != "auto":
                component[world_field] = (rotation @ np.array(component.pop(body_field), dtype=np.float64)).tolist()
        resolved[name] = component
    return resolved


def merged_poses(spec):
    poses, current, facing = [], None, 0.0
    for key in sorted(spec["keys"], key=lambda key: key["frame"]):
        facing = key.get("facing", facing)
        if current is None:
            missing = [name for name in POSE_COMPONENTS if name not in key]
            if missing:
                raise ValueError(f"The first key (frame {key['frame']}) must define every component; missing {missing}")
            current = {name: copy.deepcopy(key[name]) for name in POSE_COMPONENTS}
        else:
            for name in POSE_COMPONENTS:
                if name in key:
                    current[name] = merge_component(current[name], key[name])
        poses.append((dict(key, facing=facing), resolve_body_fields(current, facing)))
    return poses


def ground_report(points, minimum_height_cm=-0.5):
    foot_points = {name: value[1] for name, value in points.items() if name.startswith(("foot_", "ball_"))}
    lowest = min(foot_points, key=foot_points.get)
    return {"lowest_foot_point": lowest, "lowest_height_cm": round(foot_points[lowest], 2), "below_ground": foot_points[lowest] < minimum_height_cm}


EASINGS = {"linear": lambda t: t, "in": lambda t: t * t, "out": lambda t: 1 - (1 - t) ** 2, "inout": lambda t: t * t * (3 - 2 * t)}
LEG_IK_JOINTS = ("thigh_l", "thigh_r", "calf_l", "calf_r", "foot_l", "foot_r")
PLANTED_BALL_TOLERANCE_CM = 1.5
STEP_LIFT_CM = 4.0


def quaternion_slerp(first, second, t):
    first, second = np.array(first), np.array(second)
    dot = float(first @ second)
    if dot < 0:
        second, dot = -second, -dot
    if dot > 0.9995:
        blended = first + t * (second - first)
        return blended / np.linalg.norm(blended)
    theta = math.acos(dot)
    return (math.sin((1 - t) * theta) * first + math.sin(t * theta) * second) / math.sin(theta)


def rotation_slerp(first, second, t):
    return quaternion_wxyz_to_matrix(quaternion_slerp(matrix_to_quaternion_wxyz(first), matrix_to_quaternion_wxyz(second), t))


def fk_inbetween(solver, start, end, t):
    rig = solver.rig
    start_positions, start_deltas = start
    end_positions, end_deltas = end
    positions, deltas = {}, {}
    for joint in rig.joint_order:
        parent = rig.joint_parent[joint]
        if joint in ("root", "pelvis"):
            positions[joint] = (1 - t) * start_positions[joint] + t * end_positions[joint]
            deltas[joint] = rotation_slerp(start_deltas[joint], end_deltas[joint], t)
            continue
        if joint not in positions:
            positions[joint] = positions[parent] + deltas[parent] @ (rig.joint_position[joint] - rig.joint_position[parent])
        if joint in ("thigh_l", "thigh_r"):
            side = joint[-1]
            foot_delta = rotation_slerp(start_deltas[f"foot_{side}"], end_deltas[f"foot_{side}"], t)
            ball_offset = foot_delta @ (rig.joint_position[f"ball_{side}"] - rig.joint_position[f"foot_{side}"])
            start_ball, end_ball = start_positions[f"ball_{side}"], end_positions[f"ball_{side}"]
            if np.linalg.norm(end_ball - start_ball) < PLANTED_BALL_TOLERANCE_CM:
                ankle = (1 - t) * start_ball + t * end_ball - ball_offset
            else:
                travel = float(np.linalg.norm(end_positions[f"foot_{side}"] - start_positions[f"foot_{side}"]))
                ankle = (1 - t) * start_positions[f"foot_{side}"] + t * end_positions[f"foot_{side}"]
                ankle = ankle + np.array((0.0, STEP_LIFT_CM * math.sin(math.pi * t) * min(1.0, travel / 20.0), 0.0))
            solution = solver.limb_solution(positions[joint], joint, f"calf_{side}", f"foot_{side}", ankle, foot_delta @ np.array((0.0, 0.0, 1.0)))
            solver.apply_limb(positions, deltas, joint, f"calf_{side}", solution)
            deltas[f"foot_{side}"] = foot_delta
            continue
        if joint in LEG_IK_JOINTS:
            continue
        start_local = start_deltas[parent].T @ start_deltas[joint]
        end_local = end_deltas[parent].T @ end_deltas[joint]
        deltas[joint] = deltas[parent] @ rotation_slerp(start_local, end_local, t)
    return positions, deltas


def frame_report(solver, frame, name, positions, deltas, points, extra=None):
    rig, grip = solver.rig, solver.grip
    weapon_points = grip.world_points(positions[grip.hand_bone], deltas[grip.hand_bone], rig)
    report = {
        "frame": frame, "name": name,
        "wrist_degrees": {side: dict(zip(("swing", "twist"), (round(value, 1) for value in solver.wrist_angles(side, deltas[f"lowerarm_{side}"], deltas[f"hand_{side}"])))) for side in "lr"},
        "joint_interior_degrees": {f"elbow_{side}": round(180.0 - angle_between_degrees(positions[f"lowerarm_{side}"] - positions[f"upperarm_{side}"], positions[f"hand_{side}"] - positions[f"lowerarm_{side}"]), 1) for side in "lr"},
        "feet_character_cm": {side: {"ankle": [round(value, 1) for value in to_character(positions[f"foot_{side}"])],
                                     "ball": [round(value, 1) for value in to_character(positions[f"ball_{side}"])]} for side in "lr"},
        "pelvis_to_ankle_cm": {side: round(float(np.linalg.norm(positions[f"foot_{side}"] - positions[f"thigh_{side}"])), 1) for side in "lr"},
        "blade_tip_character_cm": [round(value, 1) for value in to_character(weapon_points["tip"])],
        "hand_r_character_cm": [round(value, 1) for value in to_character(positions["hand_r"])],
        "ground": ground_report(points),
    }
    report.update(extra or {})
    return report


def solve_spec(rest, weapon, spec):
    rig = TDRestRig(rest)
    grip = TDWeaponGrip(rig, weapon)
    solver = TDPoseSolver(rig, grip)
    solved, previous_balls = [], {}
    for key, pose in merged_poses(spec):
        positions, deltas, pose_report = solver.solve(pose, previous_balls)
        previous_balls = {side: positions[f"ball_{side}"] for side in "lr"}
        solved.append((key, positions, deltas, pose_report))
    frames = []
    for index, (key, positions, deltas, pose_report) in enumerate(solved):
        extra = {"facing": key["facing"], "authored": True, "reach_error_cm": pose_report["reach_error_cm"], "edge_roll_degrees": pose_report.get("edge_roll_degrees"),
                 "head_angles_degrees": [round(value, 1) for value in pose_report["head_angles"]], "knee_interior_degrees": {k: v for k, v in pose_report["joint_interior_degrees"].items() if k.startswith("knee")}}
        frames.append((int(key["frame"]), key, positions, deltas, extra))
        if key.get("inbetween") != "fk" or index + 1 >= len(solved):
            continue
        next_key, next_positions, next_deltas, _ = solved[index + 1]
        ease = EASINGS[key.get("inbetween_ease", "linear")]
        span = int(next_key["frame"]) - int(key["frame"])
        for step in range(1, span):
            positions_between, deltas_between = fk_inbetween(solver, (positions, deltas), (next_positions, next_deltas), ease(step / span))
            frames.append((int(key["frame"]) + step, {"interpolation": key.get("interpolation", DEFAULT_INTERPOLATION), "ai_description": key.get("ai_description", ""), "name": f"{key.get('name', '')}+{step}"},
                           positions_between, deltas_between, {"authored": False}))
    keys, report = [], []
    for frame, key, positions, deltas, extra in sorted(frames, key=lambda item: item[0]):
        points, boxes = solver.controller_values(positions, deltas)
        keys.append({"frame": frame, "points": points, "boxes": boxes, "root_position": positions["root"].tolist(),
                     "interpolation": key.get("interpolation", DEFAULT_INTERPOLATION), "ai_description": key.get("ai_description", "")})
        report.append(frame_report(solver, frame, key.get("name", ""), positions, deltas, points, extra))
    return {"frame_count": int(spec["frame_count"]), "fps": spec.get("fps", 30), "root_name": "root", "point_names": sorted(rig.point_owner),
            "box_names": sorted(rig.box_owner), "keys": keys}, report


def describe(rest, weapon):
    rig = TDRestRig(rest)
    grip = TDWeaponGrip(rig, weapon)
    names = ["root", "pelvis", "spine_05", "head", "upperarm_r", "lowerarm_r", "hand_r", "upperarm_l", "lowerarm_l", "hand_l", "thigh_r", "calf_r", "foot_r", "ball_r", "thigh_l", "calf_l", "foot_l", "ball_l"]
    return {
        "frame": "character [right, up, forward] cm; root at origin facing forward",
        "rest_joints_character_cm": {name: [round(value, 1) for value in to_character(rig.joint_position[name])] for name in names},
        "lengths_cm": {"upper_arm": round(rig.length("upperarm_r", "lowerarm_r"), 1), "forearm": round(rig.length("lowerarm_r", "hand_r"), 1),
                       "thigh": round(rig.length("thigh_r", "calf_r"), 1), "shin": round(rig.length("calf_r", "foot_r"), 1)},
        "weapon": {"blade_length_cm": round(grip.blade_length, 1), "blade_angle_to_hand_axis_degrees": round(grip.blade_angle_to_hand_axis, 1),
                   "rest_blade_direction_character": [round(value, 3) for value in to_character(grip.blade_rest)],
                   "rest_true_edge_direction_character": [round(value, 3) for value in to_character(grip.edge_rest)],
                   "grip_center_from_wrist_cm": [round(value, 1) for value in to_character(rig.joint_rotation[grip.hand_bone] @ grip.local["guard_center"])]},
        "controllers": {"points": len(rig.point_owner), "finger_boxes": len(rig.box_owner)},
    }


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    describe_parser = commands.add_parser("describe")
    solve_parser = commands.add_parser("solve")
    for command_parser in (describe_parser, solve_parser):
        command_parser.add_argument("--rest", required=True)
        command_parser.add_argument("--weapon", required=True)
    solve_parser.add_argument("--spec", required=True)
    solve_parser.add_argument("--out", required=True)
    arguments = parser.parse_args()
    rest = json.loads(Path(arguments.rest).read_text(encoding="utf-8-sig"))
    rest = rest.get("result", rest)
    weapon = json.loads(Path(arguments.weapon).read_text(encoding="utf-8"))
    if arguments.command == "describe":
        print(json.dumps(describe(rest, weapon), indent=1, ensure_ascii=False))
        return
    keys, report = solve_spec(rest, weapon, json.loads(Path(arguments.spec).read_text(encoding="utf-8")))
    output = Path(arguments.out)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(keys), encoding="utf-8")
    output.with_suffix(".solve.json").write_text(json.dumps(report, indent=1, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(report, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()
