import json
from datetime import datetime
from pathlib import Path

import bpy

directory = Path('C:/Project/TDGame/Saved/BlenderAnimation')
snapshot = directory / ('BeforeGripRevision_' + datetime.now().strftime('%Y%m%d_%H%M%S') + '.blend')
bpy.ops.wm.save_as_mainfile(filepath=str(snapshot), copy=True, check_existing=True)
print(json.dumps({'preserved_session': str(snapshot), 'original_document': bpy.data.filepath}))
bpy.ops.wm.read_factory_settings(use_empty=True)
