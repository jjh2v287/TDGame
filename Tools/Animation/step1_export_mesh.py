import unreal
import os

mesh_path = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
out_fbx = r'C:\Project\TDGame\Saved\TempAnim\SKM_Manny.fbx'

mesh = unreal.load_asset(mesh_path)
if not mesh:
    raise RuntimeError(f'Failed to load mesh: {mesh_path}')

task = unreal.AssetExportTask()
task.object = mesh
task.filename = out_fbx
task.automated = True
task.prompt = False
task.replace_identical = True

options = unreal.FbxExportOption()
options.collision = False
options.export_morph_targets = False
options.export_preview_mesh = False
task.options = options

result = unreal.Exporter.run_asset_export_task(task)
print(f'Export result: {result}, File exists: {os.path.exists(out_fbx)}')