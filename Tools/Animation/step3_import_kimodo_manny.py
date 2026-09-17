import unreal
import os

fbx_path = r'C:\Project\TDGame\Saved\TempAnim\Kimodo_Slash_Manny.fbx'
dest_path = '/Game/Characters/Mannequins/Anims/Sword'
dest_name = 'AS_Player_Attack_Kimodo'
skeleton_path = '/Game/Characters/Mannequins/Meshes/SK_Mannequin'

skeleton = unreal.load_asset(skeleton_path)

task = unreal.AssetImportTask()
task.filename = fbx_path
task.destination_path = dest_path
task.destination_name = dest_name
task.replace_existing = True
task.automated = True
task.save = True

options = unreal.FbxImportUI()
options.import_mesh = False
options.import_animations = True
options.skeleton = skeleton

task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
print('>>> SUCCESS IMPORTED PATHS:', task.imported_object_paths)