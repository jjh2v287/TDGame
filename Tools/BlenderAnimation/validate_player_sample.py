import json
import math
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'Tools/AnimationAuthoring'))
from smoke_test import TDMcpAnimationClient

client = TDMcpAnimationClient()
asset = '/Game/Characters/Mannequins/Anims/Blender/AS_TD_Player_Attack01_RToL_Blender'
sampled = client.call('.TDBlenderAnimationTools.sample_animation_poses',
                      animation_path=asset,
                      skeletal_mesh_path='/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple',
                      bone_names=['root', 'pelvis', 'hand_r', 'hand_l', 'foot_r', 'foot_l', 'head'],
                      sample_times=[frame / 30 for frame in range(37)])
source = json.loads((root / 'AnimationSources/Player/AS_TD_Player_Attack01_RToL_Blender.json').read_text())
source_errors = []
scale_errors = []
for frame, sample in enumerate(sampled['samples']):
    for bone in ['hand_r', 'foot_r', 'foot_l']:
        location = source['samples'][frame][bone + '_cm']
        expected = [location[0], -location[1], location[2]]
        observed = sample['bones'][bone]['component']['translation']
        source_errors.append(math.dist(expected, observed))
    for bone in sample['bones'].values():
        scale_errors.extend(abs(value - 1) for value in bone['component']['scale'])
rows = sampled['samples']
displacement = {bone: max(math.dist(sample['bones'][bone]['component']['translation'],
                                     rows[0]['bones'][bone]['component']['translation']) for sample in rows)
                for bone in ['root', 'foot_r', 'foot_l']}
report = {
    'animation': asset, 'sample_count': len(rows), 'fps': 30, 'duration_seconds': 1.2,
    'maximum_blender_unreal_position_error_cm': max(source_errors),
    'maximum_component_scale_error': max(scale_errors),
    'maximum_displacement_cm': displacement,
    'right_hand_strike_start_cm': rows[11]['bones']['hand_r']['component']['translation'],
    'right_hand_strike_end_cm': rows[21]['bones']['hand_r']['component']['translation'],
    'start_end_pose_position_error_cm': max(math.dist(rows[0]['bones'][bone]['component']['translation'],
                                                    rows[-1]['bones'][bone]['component']['translation'])
                                           for bone in rows[0]['bones']),
}
report['passed'] = (max(source_errors) < 0.05 and max(scale_errors) < 0.001 and
                    max(displacement.values()) < 0.01 and
                    report['start_end_pose_position_error_cm'] < 0.01 and
                    report['right_hand_strike_start_cm'][0] < -40 and
                    report['right_hand_strike_end_cm'][0] > 20)
directory = root / 'Docs/Validation/BlenderAnimation'
directory.mkdir(parents=True, exist_ok=True)
(directory / 'player-attack01-validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
(root / 'Saved/BlenderAnimation/unreal-samples.json').write_text(json.dumps(sampled, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
if not report['passed']:
    raise SystemExit(1)
