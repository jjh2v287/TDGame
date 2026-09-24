# File: Tools/BlenderAnimation/td_aaa_animation_engine.py
import math
from typing import Optional
try:
    import bpy
    from mathutils import Matrix, Quaternion, Vector
except ImportError:
    bpy = None
    Matrix, Quaternion, Vector = None, None, None


class TDAAAAnimationEngine:
    @staticmethod
    def calculate_gait_phase(leg_index: int, total_legs: int, cycle_progress: float, duty_factor: float = 0.6) -> tuple[bool, float]:
        if total_legs == 2:
            phase_offset = 0.5 * leg_index
        elif total_legs == 4:
            trot_offsets = [0.0, 0.5, 0.5, 0.0]
            phase_offset = trot_offsets[leg_index % 4]
        elif total_legs == 6:
            tripod_offsets = [0.0, 0.5, 0.0, 0.5, 0.0, 0.5]
            phase_offset = tripod_offsets[leg_index % 6]
        else:
            phase_offset = (leg_index / total_legs) % 1.0

        local_phase = (cycle_progress + phase_offset) % 1.0
        b_is_stance = local_phase < duty_factor

        if b_is_stance:
            sub_progress = local_phase / duty_factor
        else:
            sub_progress = (local_phase - duty_factor) / (1.0 - duty_factor)

        return b_is_stance, sub_progress

    @staticmethod
    def solve_two_bone_ik(root_pos: Vector, target_pos: Vector, pole_pos: Vector, upper_len: float, lower_len: float) -> tuple[Vector, Vector]:
        target_dir = target_pos - root_pos
        dist = target_dir.length
        max_dist = upper_len + lower_len - 0.001
        clamped_dist = max(0.01, min(dist, max_dist))

        cos_upper = (upper_len ** 2 + clamped_dist ** 2 - lower_len ** 2) / (2.0 * upper_len * clamped_dist)
        cos_upper = max(-1.0, min(1.0, cos_upper))
        upper_angle = math.acos(cos_upper)

        plane_normal = target_dir.cross(pole_pos - root_pos)
        if plane_normal.length < 1e-5:
            plane_normal = Vector((0, 0, 1)).cross(target_dir)
            if plane_normal.length < 1e-5:
                plane_normal = Vector((1, 0, 0)).cross(target_dir)
        plane_normal = plane_normal.normalized()

        bend_axis = plane_normal.cross(target_dir).normalized()
        mid_pos = root_pos + (target_dir.normalized() * (upper_len * math.cos(upper_angle))) + (bend_axis * (upper_len * math.sin(upper_angle)))
        final_tip_pos = root_pos + target_dir.normalized() * clamped_dist

        return mid_pos, final_tip_pos

    @staticmethod
    def evaluate_hermite_arc(start_pos: Vector, end_pos: Vector, apex_offset: Vector, progress: float) -> Vector:
        t = max(0.0, min(1.0, progress))
        linear_pos = start_pos.lerp(end_pos, t)
        arc_weight = 4.0 * t * (1.0 - t)
        return linear_pos + (apex_offset * arc_weight)

    @staticmethod
    def apply_kinetic_delay(base_curve: list[float], delay_frames: int) -> list[float]:
        if delay_frames <= 0 or not base_curve:
            return list(base_curve)
        if delay_frames >= len(base_curve):
            return [base_curve[0]] * len(base_curve)
        return [base_curve[0]] * delay_frames + base_curve[:-delay_frames]

    @staticmethod
    def distribute_twist_roll(pose_bone_parent, pose_bone_twist, roll_quaternion: Quaternion, primary_ratio: float = 0.65):
        twist_roll = roll_quaternion.slerp(Quaternion((1.0, 0.0, 0.0, 0.0)), 1.0 - primary_ratio)
        parent_roll = roll_quaternion.slerp(Quaternion((1.0, 0.0, 0.0, 0.0)), primary_ratio)
        pose_bone_twist.rotation_quaternion = twist_roll
        pose_bone_parent.rotation_quaternion = parent_roll

    @staticmethod
    def clamp_zero_drift_foot_contacts(armature_name: str, foot_bone: str, contact_frames: list[int]):
        armature = bpy.data.objects.get(armature_name)
        if not armature or armature.type != 'ARMATURE':
            raise ValueError(f"Armature '{armature_name}' not found.")

        bone = armature.pose.bones.get(foot_bone)
        if not bone:
            raise ValueError(f"Pose bone '{foot_bone}' not found.")

        scene = bpy.context.scene
        if not contact_frames:
            return

        scene.frame_set(contact_frames[0])
        locked_world_matrix = (armature.matrix_world @ bone.matrix).copy()

        for frame in contact_frames:
            scene.frame_set(frame)
            if bone.parent:
                inv_parent = (armature.matrix_world @ bone.parent.matrix).inverted()
                bone.matrix = inv_parent @ locked_world_matrix
            else:
                bone.matrix = armature.matrix_world.inverted() @ locked_world_matrix

            bone.keyframe_insert(data_path="location", frame=frame)
            bone.keyframe_insert(data_path="rotation_quaternion", frame=frame)
