import unreal
import os

anim_path = '/Game/Characters/Mannequins/Anims/Sword/AS_Sword_Slash_01'
out_fbx = r'C:\Project\TDGame\Saved\TempAnim\AS_Sword_Slash_01.fbx'

anim = unreal.load_asset(anim_path)
if not anim:
    raise RuntimeError(f'Failed to load anim: {anim_path}')

task = unreal.AssetExportTask()
task.object = anim
task.filename = out_fbx
task.automated = True
task.prompt = False
task.replace_identical = True

options = unreal.FbxExportOption()
task.options = options

result = unreal.Exporter.run_asset_export_task(task)
print(f'Export anim result: {result}, File exists: {os.path.exists(out_fbx)}')