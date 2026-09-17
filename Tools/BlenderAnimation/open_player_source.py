from pathlib import Path

import bpy

path = Path('C:/Project/TDGame/AnimationSources/Player/AS_TD_Player_Attack01_RToL_Blender.blend')
if bpy.data.is_dirty and bpy.data.filepath:
    raise RuntimeError('Save the current Blender document before opening the sample')
bpy.ops.wm.open_mainfile(filepath=str(path))
print({'opened': str(path), 'frames': [bpy.context.scene.frame_start, bpy.context.scene.frame_end]})
