import json
import math
import time
from pathlib import Path

import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
player = unreal.GameplayStatics.get_player_character(world, 0)
mesh = player.get_component_by_class(unreal.SkeletalMeshComponent)
mesh.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
instance = mesh.get_anim_instance()
montage = unreal.load_asset('/Game/Characters/Mannequins/Anims/Blender/AM_TD_Player_Attack01_Heavy_RToL_v04')
arm = player.get_component_by_class(unreal.SpringArmComponent)
arm.set_editor_property('target_arm_length', 350)
arm.set_editor_property('do_collision_test', False)
arm.set_editor_property('use_pawn_control_rotation', False)
arm.set_relative_rotation(unreal.Rotator(pitch=-15, yaw=150, roll=0), False, True)


def coordinates(vector):
    return [vector.x, vector.y, vector.z]


report = {'montage': montage.get_path_name(), 'pawn_class': player.get_class().get_name(),
          'start_position_cm': coordinates(player.get_actor_location()), 'samples': []}
report['play_duration_seconds'] = instance.montage_play(montage, 1.0)
started = time.monotonic()
handle = None


def capture_tick(delta):
    elapsed = time.monotonic() - started
    report['samples'].append({'elapsed': elapsed,
                              'slot_weight': instance.blueprint_get_slot_montage_local_weight('DefaultSlot'),
                              'actor_position_cm': coordinates(player.get_actor_location())})
    if elapsed < 1.8:
        return
    unreal.unregister_slate_post_tick_callback(handle)
    report['end_position_cm'] = coordinates(player.get_actor_location())
    report['travel_distance_cm'] = math.dist(report['start_position_cm'], report['end_position_cm'])
    report['maximum_slot_weight'] = max(sample['slot_weight'] for sample in report['samples'])
    report['passed'] = report['play_duration_seconds'] > 1 and report['travel_distance_cm'] > 100 and report['maximum_slot_weight'] > 0.99
    path = Path('C:/Project/TDGame/Docs/Validation/BlenderAnimation/weighty-v04-pie.json')
    path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.log('TD_WEIGHTY_PIE_VALIDATION ' + str(report['passed']) + ' travel_cm=' + str(report['travel_distance_cm']))


handle = unreal.register_slate_post_tick_callback(capture_tick)
print('Weighty montage runtime capture scheduled')
