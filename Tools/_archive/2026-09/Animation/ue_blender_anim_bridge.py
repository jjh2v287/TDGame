# File: Tools/Animation/ue_blender_anim_bridge.py
import os
import sys
import subprocess
import tempfile
from pathlib import Path

try:
    import unreal
except ImportError:
    unreal = None

BLENDER_EXE = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"


def export_skeletal_mesh_to_fbx(mesh_asset_path: str, output_fbx: str) -> bool:
    if not unreal:
        print("[Bridge] Unreal API not detected. Must run within Unreal Engine environment.")
        return False

    mesh_asset = unreal.load_asset(mesh_asset_path)
    if not mesh_asset:
        print(f"[Bridge] Failed to load mesh asset: {mesh_asset_path}")
        return False

    export_task = unreal.AssetExportTask()
    export_task.object = mesh_asset
    export_task.filename = str(Path(output_fbx).resolve())
    export_task.automated = True
    export_task.prompt = False
    export_task.replace_identical = True

    export_options = unreal.FbxExportOption()
    export_options.collision = False
    export_options.export_morph_targets = False
    export_options.export_preview_mesh = False
    export_task.options = export_options

    result = unreal.Exporter.run_asset_export_task(export_task)
    print(f"[Bridge] Export mesh result: {result} -> {output_fbx}")
    return result


def create_blender_animation_script(input_fbx: str, output_fbx: str, anim_type: str = "sword_slash") -> str:
    script_content = f'''import bpy
import math

bpy.ops.wm.read_factory_settings(use_empty=True)

# 1. Import FBX
bpy.ops.import_scene.fbx(filepath=r"{input_fbx}", use_anim=False)

# 2. Find Armature
armature = None
for obj in bpy.context.scene.objects:
    if obj.type == 'ARMATURE':
        armature = obj
        break

if not armature:
    raise RuntimeError("No armature object found in imported FBX")

bpy.context.view_layer.objects.active = armature
bpy.ops.object.mode_set(mode='POSE')

# 3. Create Action
action = bpy.data.actions.new(name="{anim_type}")
if not armature.animation_data:
    armature.animation_data_create()
armature.animation_data.action = action

# 4. Inject Slash Keyframes (Right to Left Slash)
pose_bones = armature.pose.bones

spine_candidates = ["spine_01", "spine", "Spine", "Spine1"]
arm_candidates = ["upperarm_r", "UpperArm_R", "arm_r", "RightArm"]
forearm_candidates = ["lowerarm_r", "LowerArm_R", "forearm_r", "RightForeArm"]

def find_bone(candidates):
    for name in candidates:
        if name in pose_bones:
            return pose_bones[name]
    return None

spine = find_bone(spine_candidates)
upperarm = find_bone(arm_candidates)
forearm = find_bone(forearm_candidates)

total_frames = 30
bpy.context.scene.frame_start = 0
bpy.context.scene.frame_end = total_frames

keyframes = [
    # (frame, spine_yaw, arm_pitch, arm_yaw)
    (0,   0.0,   0.0,   0.0),
    (8,   25.0, -20.0, -40.0),  # Wind-up
    (14, -35.0,  30.0,  50.0),  # Slash strike
    (20, -40.0,  35.0,  55.0),  # Overshoot
    (30,   0.0,   0.0,   0.0)   # Recovery
]

for frame, s_yaw, a_pitch, a_yaw in keyframes:
    bpy.context.scene.frame_set(frame)
    if spine:
        spine.rotation_mode = 'XYZ'
        spine.rotation_euler = (0, 0, math.radians(s_yaw))
        spine.keyframe_insert(data_path="rotation_euler", frame=frame)

    if upperarm:
        upperarm.rotation_mode = 'XYZ'
        upperarm.rotation_euler = (math.radians(a_pitch), 0, math.radians(a_yaw))
        upperarm.keyframe_insert(data_path="rotation_euler", frame=frame)

# 5. Export FBX with Animation
bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.export_scene.fbx(
    filepath=r"{output_fbx}",
    use_selection=False,
    bake_anim=True,
    bake_anim_use_all_bones=True,
    bake_anim_use_nla_strips=False,
    bake_anim_use_all_actions=False,
    bake_anim_step=1.0,
    add_leaf_bones=False
)
print("Blender animation generation completed successfully.")
'''
    return script_content


def run_blender_headless_animation(input_fbx: str, output_fbx: str, anim_type: str = "sword_slash") -> bool:
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False, encoding="utf-8") as temp_file:
        script_path = temp_file.name
        temp_file.write(create_blender_animation_script(input_fbx, output_fbx, anim_type))

    cmd = [
        BLENDER_EXE,
        "-b",
        "--python", script_path
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
        print(f"[Blender Output]\n{proc.stdout}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"[Blender Error]\n{e.stderr}")
        return False
    finally:
        if os.path.exists(script_path):
            os.remove(script_path)


def import_fbx_as_anim_sequence(fbx_path: str, destination_path: str, destination_name: str, skeleton_path: str) -> bool:
    if not unreal:
        print("[Bridge] Unreal API not detected. Must run within Unreal Engine environment.")
        return False

    task = unreal.AssetImportTask()
    task.filename = str(Path(fbx_path).resolve())
    task.destination_path = destination_path
    task.destination_name = destination_name
    task.replace_existing = True
    task.automated = True
    task.save = True

    options = unreal.FbxImportUI()
    options.import_animations = True
    options.mesh_type_to_import = unreal.FBXMeshType.FBXMT_ANIMATION
    options.skeleton = unreal.load_asset(skeleton_path)

    anim_data = unreal.FbxAnimSequenceImportData()
    anim_data.import_custom_range = False
    anim_data.animation_length = unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME
    options.anim_sequence_import_data = anim_data

    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    success = len(task.imported_object_paths) > 0
    print(f"[Bridge] Imported AnimSequence: {task.imported_object_paths}")
    return success