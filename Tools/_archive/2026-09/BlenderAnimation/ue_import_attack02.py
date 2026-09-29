# File: Tools/BlenderAnimation/ue_import_attack02.py
import json
import unreal

FOLDER = "/Game/Characters/Mannequins/Anims/Blender"
SKELETON_PATH = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
FBX_FILE = r"C:\Project\TDGame\AnimationSources\Player\AS_TD_Player_Attack02_SwordSlash_LToR.fbx"
ASSET_NAME = "AS_TD_Player_Attack02_SwordSlash_LToR"
MONTAGE_NAME = "AM_TD_Player_Attack02_SwordSlash_LToR"

print(f"Importing {FBX_FILE} -> {FOLDER}/{ASSET_NAME}")

skeleton = unreal.EditorAssetLibrary.load_asset(SKELETON_PATH)
options = unreal.FbxImportUI()
for name, value in {
    "automated_import_should_detect_type": False,
    "mesh_type_to_import": unreal.FBXImportType.FBXIT_ANIMATION,
    "original_import_type": unreal.FBXImportType.FBXIT_ANIMATION,
    "import_as_skeletal": True,
    "import_mesh": False,
    "import_animations": True,
    "import_materials": False,
    "import_textures": False,
    "create_physics_asset": False,
    "skeleton": skeleton,
    "override_animation_name": ASSET_NAME,
}.items():
    options.set_editor_property(name, value)

animation_options = options.get_editor_property("anim_sequence_import_data")
for name, value in {
    "animation_length": unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
    "use_default_sample_rate": False,
    "custom_sample_rate": 30,
    "import_bone_tracks": True,
    "import_custom_attribute": False,
    "add_curve_metadata_to_skeleton": False,
    "preserve_local_transform": True,
    "convert_scene": True,
    "convert_scene_unit": True,
    "force_front_x_axis": False,
    "import_uniform_scale": 1.0,
}.items():
    animation_options.set_editor_property(name, value)

task = unreal.AssetImportTask()
task.filename = FBX_FILE
task.destination_path = FOLDER
task.destination_name = ASSET_NAME
task.replace_existing = True
task.replace_existing_settings = True
task.automated = True
task.save = False
task.factory = unreal.FbxFactory()
task.options = options

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

seq = unreal.EditorAssetLibrary.load_asset(f"{FOLDER}/{ASSET_NAME}")
if not seq:
    raise RuntimeError(f"Failed to load imported sequence {FOLDER}/{ASSET_NAME}")

seq.set_editor_property("enable_root_motion", True)
seq.set_editor_property("root_motion_root_lock", unreal.RootMotionRootLock.REF_POSE)
seq.set_editor_property("force_root_lock", False)
unreal.EditorAssetLibrary.save_loaded_asset(seq, only_if_is_dirty=False)

print(f"SUCCESS: Imported AnimSequence {FOLDER}/{ASSET_NAME} with Root Motion enabled.")

montage_path = f"{FOLDER}/{MONTAGE_NAME}"
montage = unreal.EditorAssetLibrary.load_asset(montage_path)
if not montage:
    factory = unreal.AnimMontageFactory()
    factory.target_skeleton = skeleton
    montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MONTAGE_NAME, FOLDER, unreal.AnimMontage, factory)

if montage:
    unreal.EditorAssetLibrary.save_loaded_asset(montage, only_if_is_dirty=False)
    print(f"SUCCESS: Saved AnimMontage {montage_path}")
