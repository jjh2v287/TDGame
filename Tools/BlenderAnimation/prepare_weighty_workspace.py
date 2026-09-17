from pathlib import Path

import bpy

path = Path('C:/Project/TDGame/Saved/BlenderAnimation/Reference/ReferenceLibrary.blend')
if bpy.data.filepath:
    current = Path(bpy.data.filepath).resolve()
    if current.parent != Path('C:/Project/TDGame/AnimationSources/Player') or not current.name.startswith('AS_TD_Player_Attack01_Heavy_RToL_') or bpy.data.is_dirty:
        raise RuntimeError('Save the current document before preparing a new candidate')
else:
    bpy.ops.wm.save_as_mainfile(filepath=str(path), check_existing=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
print('Reference review saved; empty authoring document ready')
