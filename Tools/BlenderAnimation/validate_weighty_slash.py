import json
import math
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'Tools/AnimationAuthoring'))
from smoke_test import TDMcpAnimationClient

client = TDMcpAnimationClient()
name = 'AS_TD_Player_Attack01_Heavy_RToL_v04'
source = json.loads((root / 'AnimationSources/Player' / (name + '.json')).read_text())
mesh = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
asset = '/Game/Characters/Mannequins/Anims/Blender/' + name
bones = ['root', 'pelvis', 'hand_r', 'hand_l', 'foot_r', 'foot_l', 'ball_r', 'ball_l']
candidate = client.call('.TDBlenderAnimationTools.sample_animation_poses',
                        animation_path=asset, skeletal_mesh_path=mesh, bone_names=bones,
                        sample_times=[frame / 30 for frame in range(44)])
reference = client.call('.TDBlenderAnimationTools.sample_animation_poses',
                        animation_path=source['source_animation'], skeletal_mesh_path=mesh,
                        bone_names=['root', 'pelvis', 'foot_r', 'foot_l', 'ball_r', 'ball_l'],
                        sample_times=[row['source_frame'] / 30 for row in source['records']])
conversion_errors = []
for frame, row in enumerate(source['records']):
    for bone, location in row['bones_world_cm'].items():
        expected = [location[0], -location[1], location[2]]
        observed = candidate['samples'][frame]['bones'][bone]['component']['translation']
        conversion_errors.append(math.dist(expected, observed))
reference_errors = [
    math.dist(values['component']['translation'], candidate['samples'][frame]['bones'][bone]['component']['translation'])
    for frame, row in enumerate(reference['samples']) for bone, values in row['bones'].items()
]
positions = lambda bone: [row['bones'][bone]['component']['translation'] for row in candidate['samples']]
toe = positions('ball_r')
plant_span = max(math.dist(position[:2], toe[23][:2]) for position in toe[23:])
roots = positions('root')
report = {
    'animation': asset, 'source_animation': source['source_animation'],
    'duration_seconds': 43 / 30, 'fps': 30, 'sample_count': 44,
    'maximum_blender_unreal_bone_position_error_cm': max(conversion_errors),
    'maximum_reference_lower_body_error_cm': max(reference_errors),
    'root_travel_cm': math.dist(roots[0], roots[-1]),
    'right_toe_plant_phase': {'frames_zero_based': [23, 43], 'max_xy_displacement_cm': plant_span},
    'right_hand_sweep_x_cm': [positions('hand_r')[17][0], positions('hand_r')[22][0]],
    'visual_review_is_separate': True,
    'user_visual_approval': False,
}
report['technical_checks_passed'] = max(conversion_errors) < 0.05 and max(reference_errors) < 0.05 and plant_span < 0.5 and 140 < report['root_travel_cm'] < 160
path = root / 'Docs/Validation/BlenderAnimation/weighty-v04-validation.json'
path.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
if not report['technical_checks_passed']:
    raise SystemExit(1)
