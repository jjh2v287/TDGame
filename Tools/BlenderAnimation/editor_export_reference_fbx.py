"""에디터 안에서 실행: 참고 AnimSequence 여러 개를 Blender 편집용 FBX로 내보낸다(에셋은 바꾸지 않음, 같은 이름 파일이 있으면 건너뜀).
실행: python Tools/run_in_editor.py Tools/BlenderAnimation/editor_export_reference_fbx.py (에셋 목록 변경: -c "TD_EXPORT_ASSETS=['/Game/...']; exec(open('Tools/BlenderAnimation/editor_export_reference_fbx.py', encoding='utf-8').read())", PowerShell에서 실행)
출력: Saved/BlenderAnimation/Reference/Greystone/<에셋 이름>.fbx, 로그의 [TDTool] export 줄
상태: 현행 (2026-09-25)
"""
import os

import unreal

DEFAULT_ASSETS = [
    "/Game/Characters/Paragon/Greystone/Attack/Attack_PrimaryA",
    "/Game/Characters/Paragon/Greystone/Attack/Attack_PrimaryB",
    "/Game/Characters/Paragon/Greystone/Attack/Attack_PrimaryC",
]
ASSETS = globals().get("TD_EXPORT_ASSETS", DEFAULT_ASSETS)
OUTPUT_DIR = globals().get("TD_EXPORT_DIR", os.path.join(unreal.Paths.project_saved_dir(), "BlenderAnimation", "Reference", "Greystone"))


def export_animation(asset_path):
    output_path = os.path.abspath(os.path.join(OUTPUT_DIR, asset_path.split("/")[-1] + ".fbx"))
    if os.path.exists(output_path):
        unreal.log(f"[TDTool] export skip (exists) {output_path}")
        return
    animation = unreal.load_asset(asset_path)
    if not isinstance(animation, unreal.AnimSequence):
        raise TypeError(f"Not an AnimSequence: {asset_path}")
    options = unreal.FbxExportOption()
    options.set_editor_property("ascii", False)
    options.set_editor_property("export_morph_targets", False)
    options.set_editor_property("export_preview_mesh", False)
    options.set_editor_property("force_front_x_axis", False)
    options.set_editor_property("map_skeletal_motion_to_root", False)
    task = unreal.AssetExportTask()
    task.object = animation
    task.filename = output_path
    task.automated = True
    task.prompt = False
    task.replace_identical = False
    task.options = options
    task.exporter = unreal.AnimSequenceExporterFBX()
    if not unreal.Exporter.run_asset_export_task(task) or not os.path.isfile(output_path):
        raise RuntimeError(f"FBX export failed for {asset_path}: {list(task.errors)}")
    unreal.log(f"[TDTool] export {asset_path} -> {output_path} ({os.path.getsize(output_path)} bytes)")


os.makedirs(OUTPUT_DIR, exist_ok=True)
for asset in ASSETS:
    export_animation(asset)
