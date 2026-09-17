import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'Tools/AnimationAuthoring'))
from smoke_test import TDMcpAnimationClient

client = TDMcpAnimationClient()
name = 'AS_TD_Player_Attack01_RToL_Blender'
result = client.call(
    '.TDBlenderAnimationTools.import_animation_fbx',
    source_file=str(root / 'AnimationSources/Player' / (name + '.fbx')),
    destination_folder='/Game/Characters/Mannequins/Anims/Blender',
    asset_name=name,
    skeleton_path='/Game/Characters/Mannequins/Meshes/SK_Mannequin',
    sample_rate=30,
    import_uniform_scale=1.0,
    preserve_local_transform=True,
)
(root / 'Saved/BlenderAnimation/import-result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result))
