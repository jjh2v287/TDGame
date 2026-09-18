"""에디터 안(PIE 실행 중)에서 run_in_editor.py로 실행: 플레이어 캐릭터에 검 횡베기 몽타주를 재생하고 슬롯 가중치·실제 이동 거리를 틱마다 기록하며 타격 시점 스크린샷을 찍는다.
실행: python Tools/BlenderAnimation/validate_sword_slash_pie.py 가 PIE 시작 뒤 이 파일을 넘긴다(직접 실행 시 PIE가 켜져 있어야 한다).
출력: Docs/Validation/BlenderAnimation/sword-slash-pie.json, Saved/Screenshots/WindowsEditor/sword_slash_contact.png
상태: 현행 (2026-09-19)
"""
import json
import math
import time
from pathlib import Path

import unreal

MONTAGE = '/Game/Characters/Mannequins/Anims/Blender/AM_TD_Player_Attack01_SwordSlash_RToL'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
player = unreal.GameplayStatics.get_player_character(world, 0)
mesh = player.get_component_by_class(unreal.SkeletalMeshComponent)
mesh.set_editor_property('visibility_based_anim_tick_option', unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
instance = mesh.get_anim_instance()
montage = unreal.load_asset(MONTAGE)
arm = player.get_component_by_class(unreal.SpringArmComponent)
arm.set_editor_property('target_arm_length', 320)
arm.set_editor_property('do_collision_test', False)
arm.set_editor_property('use_pawn_control_rotation', False)
arm.set_relative_rotation(unreal.Rotator(pitch=-12, yaw=135, roll=0), False, True)


def coordinates(vector):
    return [vector.x, vector.y, vector.z]


report = {'montage': montage.get_path_name(), 'pawn_class': player.get_class().get_name(),
          'start_position_cm': coordinates(player.get_actor_location()), 'samples': [], 'screenshot_taken': False}
report['play_duration_seconds'] = instance.montage_play(montage, 1.0)
started = time.monotonic()
handle = None


def capture_tick(delta):
    global handle
    elapsed = time.monotonic() - started
    report['samples'].append({'elapsed': round(elapsed, 4),
                              'slot_weight': instance.blueprint_get_slot_montage_local_weight('DefaultSlot'),
                              'actor_position_cm': coordinates(player.get_actor_location())})
    if not report['screenshot_taken'] and elapsed >= 0.5:
        unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, 'sword_slash_contact.png')
        report['screenshot_taken'] = True
    if elapsed < 1.9:
        return
    unreal.unregister_slate_post_tick_callback(handle)
    report['end_position_cm'] = coordinates(player.get_actor_location())
    report['travel_distance_cm'] = math.dist(report['start_position_cm'], report['end_position_cm'])
    report['maximum_slot_weight'] = max(sample['slot_weight'] for sample in report['samples'])
    report['passed'] = 1.2 < report['play_duration_seconds'] < 1.4 and 44 < report['travel_distance_cm'] < 56 and report['maximum_slot_weight'] > 0.99
    path = Path('C:/Project/TDGame/Docs/Validation/BlenderAnimation/sword-slash-pie.json')
    path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.log('TD_SWORD_SLASH_PIE ' + str(report['passed']) + ' travel_cm=' + str(report['travel_distance_cm']))


handle = unreal.register_slate_post_tick_callback(capture_tick)
print('Sword slash montage runtime capture scheduled')
