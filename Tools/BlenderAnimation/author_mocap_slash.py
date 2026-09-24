import json
import math
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BLEND_SOURCE = ROOT / "AnimationSources/Player/AS_TD_Player_Attack02_SwordSlash_LToR.blend"
BVH_SOURCE = ROOT / "AnimationSources/Mocap/02_07.bvh"
OUTPUT_BLEND = ROOT / "AnimationSources/Player/AS_TD_Player_Attack03_MocapSlash.blend"
OUTPUT_FBX = ROOT / "AnimationSources/Player/AS_TD_Player_Attack03_MocapSlash.fbx"
OUTPUT_JSON = ROOT / "AnimationSources/Player/AS_TD_Player_Attack03_MocapSlash.json"

import bpy
from mathutils import Matrix, Quaternion, Vector

MAPPING = {
    "pelvis": "Hips",
    "spine_01": "Spine",
    "spine_02": "Spine1",
    "neck_01": "Neck",
    "head": "Head",
    "clavicle_l": "LeftShoulder",
    "upperarm_l": "LeftArm",
    "lowerarm_l": "LeftForeArm",
    "hand_l": "LeftHand",
    "clavicle_r": "RightShoulder",
    "upperarm_r": "RightArm",
    "lowerarm_r": "RightForeArm",
    "hand_r": "RightHand",
    "thigh_l": "LeftUpLeg",
    "calf_l": "LeftLeg",
    "foot_l": "LeftFoot",
    "ball_l": "LeftToeBase",
    "thigh_r": "RightUpLeg",
    "calf_r": "RightLeg",
    "foot_r": "RightFoot",
    "ball_r": "RightToeBase",
}


def build_mocap_slash():
    bpy.ops.wm.open_mainfile(filepath=str(BLEND_SOURCE))
    tgt = bpy.data.objects.get("root")
    if not tgt:
        raise RuntimeError("Target armature 'root' not found.")

    bpy.ops.import_anim.bvh(filepath=str(BVH_SOURCE))
    bvh = bpy.data.objects.get("02_07")
    if not bvh:
        raise RuntimeError("Imported BVH '02_07' not found.")

    bpy.context.view_layer.objects.active = tgt
    bpy.ops.object.mode_set(mode="POSE")

    for tgt_bone, src_bone in MAPPING.items():
        if tgt_bone in tgt.pose.bones and src_bone in bvh.pose.bones:
            pb = tgt.pose.bones[tgt_bone]
            con = pb.constraints.new("COPY_ROTATION")
            con.target = bvh
            con.subtarget = src_bone

    start_mocap_frame = 1400
    end_mocap_frame = 1520
    step = 3

    bpy.ops.nla.bake(
        frame_start=start_mocap_frame,
        frame_end=end_mocap_frame,
        step=step,
        visual_keying=True,
        clear_constraints=True,
        bake_types={"POSE"},
    )
    bpy.data.objects.remove(bvh, do_unlink=True)

    act = tgt.animation_data.action
    act.name = "AS_TD_Player_Attack03_MocapSlash"
    
    for layer in act.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for fc in bag.fcurves:
                    for kp in fc.keyframe_points:
                        kp.co[0] = (kp.co[0] - start_mocap_frame) / step

    num_frames = int((end_mocap_frame - start_mocap_frame) / step) + 1
    bpy.context.scene.frame_start = 0
    bpy.context.scene.frame_end = num_frames - 1

    for idx in range(num_frames):
        bpy.context.scene.frame_set(idx)
        forward_dist = (idx / (num_frames - 1)) * 45.0
        tgt.location = Vector((0.0, -forward_dist, 0.0))
        tgt.keyframe_insert(data_path="location", frame=idx)
        for follower, leader in [("ik_hand_r", "hand_r"), ("ik_hand_l", "hand_l"), ("ik_foot_r", "foot_r"), ("ik_foot_l", "foot_l")]:
            if follower in tgt.pose.bones and leader in tgt.pose.bones:
                tgt.pose.bones[follower].matrix = tgt.pose.bones[leader].matrix.copy()
                tgt.pose.bones[follower].keyframe_insert(data_path="rotation_quaternion", frame=idx)
                tgt.pose.bones[follower].keyframe_insert(data_path="location", frame=idx)

    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT_BLEND))
    
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.ops.object.select_all(action="DESELECT")
    tgt.select_set(True)
    bpy.context.view_layer.objects.active = tgt
    bpy.ops.export_scene.fbx(
        filepath=str(OUTPUT_FBX),
        use_selection=True,
        object_types={"ARMATURE"},
        add_leaf_bones=False,
        bake_anim=True,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False,
        bake_anim_step=1,
        bake_anim_simplify_factor=0,
        axis_forward="-Z",
        axis_up="Y",
    )
    print(f"Mocap Retargeted & Saved: {OUTPUT_BLEND} and {OUTPUT_FBX} ({num_frames} frames)")


if __name__ == "__main__":
    build_mocap_slash()

