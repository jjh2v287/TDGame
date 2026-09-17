import json
import math
import re
from pathlib import Path

import unreal
import toolset_registry
from toolset_registry.registration import Registration


def _project_fbx(file_path: str) -> Path:
    project = Path(unreal.Paths.project_dir()).resolve()
    path = Path(file_path).resolve()
    if not path.is_relative_to(project) or path.suffix.lower() != ".fbx":
        raise ValueError("Use an absolute .fbx path inside the current project.")
    return path


def _load_asset(asset_path: str, asset_type):
    if not asset_path.startswith("/Game/"):
        raise ValueError("Use an existing /Game asset path.")
    asset = unreal.load_asset(asset_path)
    if not isinstance(asset, asset_type):
        types = asset_type if isinstance(asset_type, tuple) else (asset_type,)
        expected = ", ".join(item.__name__ for item in types)
        raise ValueError(f"Expected {expected}: {asset_path}")
    return asset


def _transform_values(transform):
    translation = transform.translation
    rotation = transform.rotation
    scale = transform.scale3d
    return {
        "translation": [translation.x, translation.y, translation.z],
        "quaternion": [rotation.x, rotation.y, rotation.z, rotation.w],
        "scale": [scale.x, scale.y, scale.z],
    }


@unreal.uclass()
class TDBlenderAnimationTools(unreal.ToolsetDefinition):
    """Project-local FBX exchange between Unreal skeletal animation and Blender."""

    @toolset_registry.tool_call
    @staticmethod
    def export_fbx(asset_path: str, output_file: str) -> str:
        """Export an existing SkeletalMesh or AnimSequence to a new project-local FBX file.

        The asset and skeleton are never changed. Exports LOD0 without materials baking,
        morph targets or animation preview mesh. An existing output file is refused.
        """
        path = _project_fbx(output_file)
        if path.exists():
            raise FileExistsError(str(path))
        asset = _load_asset(asset_path, (unreal.SkeletalMesh, unreal.AnimSequence))
        options = unreal.FbxExportOption()
        for name, value in {
            "ascii": False,
            "level_of_detail": False,
            "collision": False,
            "export_morph_targets": False,
            "export_preview_mesh": False,
            "force_front_x_axis": False,
            "map_skeletal_motion_to_root": False,
            "bake_material_inputs": unreal.FbxMaterialBakeMode.DISABLED,
        }.items():
            options.set_editor_property(name, value)
        task = unreal.AssetExportTask()
        task.object = asset
        task.filename = str(path)
        task.automated = True
        task.prompt = False
        task.replace_identical = False
        task.options = options
        task.exporter = (
            unreal.SkeletalMeshExporterFBX()
            if isinstance(asset, unreal.SkeletalMesh)
            else unreal.AnimSequenceExporterFBX()
        )
        path.parent.mkdir(parents=True, exist_ok=True)
        if not unreal.Exporter.run_asset_export_task(task) or not path.is_file():
            raise RuntimeError(f"FBX export failed: {list(task.errors)}")
        return json.dumps({
            "asset": asset.get_path_name(),
            "skeleton": asset.get_editor_property("skeleton").get_path_name(),
            "file": str(path),
            "bytes": path.stat().st_size,
        })

    @toolset_registry.tool_call
    @staticmethod
    def import_animation_fbx(
        source_file: str,
        destination_folder: str,
        asset_name: str,
        skeleton_path: str,
        sample_rate: int = 30,
        import_uniform_scale: float = 1.0,
        preserve_local_transform: bool = True,
    ) -> str:
        """Import one baked FBX take as a new AnimSequence on an existing exact skeleton.

        Uses legacy FbxFactory with animation-only import. Imports no mesh, material,
        texture or physics asset and refuses an existing destination. Saves only the
        newly imported animation. Supply a project-local FBX with one animation take.
        """
        path = _project_fbx(source_file)
        if not path.is_file():
            raise FileNotFoundError(str(path))
        folder = destination_folder.rstrip("/")
        if not re.fullmatch(r"/Game(?:/[A-Za-z0-9_]+)+", folder):
            raise ValueError("Use a /Game content folder with alphanumeric/underscore names.")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", asset_name):
            raise ValueError("Invalid animation asset name.")
        if not 1 <= sample_rate <= 120:
            raise ValueError("sample_rate must be between 1 and 120.")
        if not math.isfinite(import_uniform_scale) or not 0 < import_uniform_scale <= 100:
            raise ValueError("import_uniform_scale must be finite, positive and at most 100.")
        target_path = f"{folder}/{asset_name}"
        if unreal.EditorAssetLibrary.does_asset_exist(target_path):
            raise FileExistsError(target_path)
        skeleton = _load_asset(skeleton_path, unreal.Skeleton)
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
            "override_animation_name": asset_name,
        }.items():
            options.set_editor_property(name, value)
        animation_options = options.get_editor_property("anim_sequence_import_data")
        for name, value in {
            "animation_length": unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
            "use_default_sample_rate": False,
            "custom_sample_rate": sample_rate,
            "import_bone_tracks": True,
            "import_custom_attribute": False,
            "add_curve_metadata_to_skeleton": False,
            "preserve_local_transform": preserve_local_transform,
            "convert_scene": True,
            "convert_scene_unit": True,
            "force_front_x_axis": False,
            "import_uniform_scale": import_uniform_scale,
        }.items():
            animation_options.set_editor_property(name, value)
        task = unreal.AssetImportTask()
        task.filename = str(path)
        task.destination_path = folder
        task.destination_name = asset_name
        task.replace_existing = False
        task.replace_existing_settings = False
        task.automated = True
        task.save = False
        task.factory = unreal.FbxFactory()
        task.options = options
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        imported_paths = list(task.get_editor_property("imported_object_paths"))
        if len(imported_paths) != 1:
            raise RuntimeError(f"Expected one animation; generated unsaved assets: {imported_paths}")
        animation = _load_asset(imported_paths[0], unreal.AnimSequence)
        if animation.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("Imported animation uses an unexpected skeleton; asset not saved.")
        if animation.get_path_name().split(".")[0] != target_path:
            raise RuntimeError(f"Unexpected generated name; asset not saved: {animation.get_path_name()}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(animation, only_if_is_dirty=False):
            raise RuntimeError("Could not save the imported animation.")
        return json.dumps({
            "asset": animation.get_path_name(),
            "skeleton": skeleton.get_path_name(),
            "duration_seconds": unreal.AnimationLibrary.get_sequence_length(animation),
            "num_frames": unreal.AnimationLibrary.get_num_frames(animation),
            "source_file": str(path),
            "sample_rate": sample_rate,
        })

    @toolset_registry.tool_call
    @staticmethod
    def sample_animation_poses(
        animation_path: str,
        skeletal_mesh_path: str,
        bone_names: list[str],
        sample_times: list[float],
    ) -> str:
        """Read bone local and component transforms at explicit times in seconds.

        Requires the animation and mesh to share the exact skeleton. Uses raw data,
        no retargeting, and retains root motion in the pose. Translation is centimeters;
        quaternion order is x,y,z,w. Up to 256 bones and 121 times per request.
        """
        animation = _load_asset(animation_path, unreal.AnimSequence)
        mesh = _load_asset(skeletal_mesh_path, unreal.SkeletalMesh)
        if animation.get_editor_property("skeleton") != mesh.get_editor_property("skeleton"):
            raise ValueError("Animation and mesh must share the exact skeleton.")
        if not 1 <= len(bone_names) <= 256 or not 1 <= len(sample_times) <= 121:
            raise ValueError("Supply 1..256 bones and 1..121 sample times.")
        duration = unreal.AnimationLibrary.get_sequence_length(animation)
        if any(not math.isfinite(time) or time < 0 or time > duration for time in sample_times):
            raise ValueError(f"Sample times must be within 0..{duration} seconds.")
        options = unreal.AnimPoseEvaluationOptions()
        options.optional_skeletal_mesh = mesh
        options.evaluation_type = unreal.AnimDataEvalType.RAW
        options.should_retarget = False
        options.extract_root_motion = False
        options.incorporate_root_motion_into_pose = True
        rows = []
        for time in sample_times:
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation, time, options)
            available_bones = set(map(str, unreal.AnimPoseExtensions.get_bone_names(pose)))
            missing = set(bone_names) - available_bones
            if missing:
                raise ValueError(f"Missing bones: {sorted(missing)}")
            rows.append({
                "time": time,
                "bones": {
                    bone: {
                        "local": _transform_values(unreal.AnimPoseExtensions.get_bone_pose(
                            pose, bone, unreal.AnimPoseSpaces.LOCAL)),
                        "component": _transform_values(unreal.AnimPoseExtensions.get_bone_pose(
                            pose, bone, unreal.AnimPoseSpaces.WORLD)),
                    }
                    for bone in bone_names
                },
            })
        return json.dumps({"animation": animation.get_path_name(), "samples": rows})


_registration = Registration([TDBlenderAnimationTools])


def register():
    return _registration.register()
