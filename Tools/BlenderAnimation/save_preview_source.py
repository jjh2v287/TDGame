from pathlib import Path

import bpy

path = Path('C:/Project/TDGame/AnimationSources/Player/AS_TD_Player_Attack01_RToL_Blender.blend')
if Path(bpy.data.filepath).resolve() != path.resolve():
    raise ValueError('The active file is not the generated player sample')
bpy.context.preferences.filepaths.save_version = 0
bpy.context.scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(path), check_existing=False)
print({'saved': str(path), 'dirty': bpy.data.is_dirty})
