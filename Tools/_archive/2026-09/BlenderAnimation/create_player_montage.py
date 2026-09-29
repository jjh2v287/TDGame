import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'Tools/AnimationAuthoring'))
from smoke_test import TDMcpAnimationClient

client = TDMcpAnimationClient()
result = client.request('.TDAnimationAuthoringTools.CreateMontage', {
    'asset_path': '/Game/Characters/Mannequins/Anims/Blender/AM_TD_Player_Attack01_RToL_Blender',
    'slot': 'DefaultSlot',
    'save': True,
    'blend_in': 0.12,
    'blend_out': 0.18,
    'segments': [{'sequence': '/Game/Characters/Mannequins/Anims/Blender/AS_TD_Player_Attack01_RToL_Blender'}],
    'sections': [{'name': 'Attack01', 'time': 0}],
})
print(json.dumps(result))
if not result.get('success'):
    raise SystemExit(1)
