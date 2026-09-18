import unreal

fbx_path = r'C:\Project\TDGame\Saved\TempAnim\Kimodo_Slash.fbx'
dest_path = '/Game/Characters/Mannequins/Anims/Sword'
dest_name = 'AS_Player_Attack_Kimodo'

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

task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
print('>>> IMPORTED KIMODO ANIM PATHS:', task.imported_object_paths)