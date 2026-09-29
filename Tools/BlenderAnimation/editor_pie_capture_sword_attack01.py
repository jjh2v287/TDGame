"""에디터 안(PIE 실행 중)에서 run_in_editor.py로 실행: 플레이어 폰에 콘솔 TDPlayMeleeAction으로 검 공격 시퀀스를 UAF 행동으로 재생해(루트모션 → Mover) 틱마다 실제 위치를 기록하고 타격 시점 스크린샷을 찍는다(무기 부착은 게임 캐릭터에 아직 없어 검 없이 찍힌다).
실행: python Tools/BlenderAnimation/validate_sword_attack01_pie.py 가 PIE 시작 뒤 이 파일을 넘긴다(직접 실행 시 PIE가 켜져 있어야 한다).
출력: Docs/Validation/BlenderAnimation/sword-attack01-pie.json, Saved/Screenshots/WindowsEditor/sword_attack01_contact.png
상태: 현행 (2026-09-30 몽타주 → UAF 행동 전환, 고해상도 스크린샷은 한 번 실행에 한 장만 확실히 찍힌다)
"""
import json
import math
import time
from pathlib import Path

import unreal

SEQUENCE = '/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordAttack01'
SHOTS = [(0.35, 'contact')]
SETTLE_SECONDS = 0.3
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
player = unreal.GameplayStatics.get_player_pawn(world, 0)
mesh = player.get_editor_property('mesh')
sequence = unreal.load_asset(SEQUENCE)
arm = player.get_component_by_class(unreal.SpringArmComponent)
arm.set_editor_property('target_arm_length', 420)
arm.set_editor_property('do_collision_test', False)
arm.set_editor_property('use_pawn_control_rotation', False)
arm.set_relative_rotation(unreal.Rotator(pitch=-35, yaw=150, roll=0), False, True)


def coordinates(vector):
    return [vector.x, vector.y, vector.z]


report = {'sequence': sequence.get_path_name(), 'pawn_class': player.get_class().get_name(), 'sword_socket_exists': mesh.does_socket_exist('HandGrip_R'),
          'mesh_animation_enabled': mesh.get_editor_property('enable_animation'),
          'start_position_cm': coordinates(player.get_actor_location()), 'samples': [], 'screenshots': []}
report['play_duration_seconds'] = unreal.AnimationLibrary.get_sequence_length(sequence)
unreal.SystemLibrary.execute_console_command(world, 'TDPlayMeleeAction')
started = time.monotonic()
handle = None


def capture_tick(delta):
    global handle
    elapsed = time.monotonic() - started
    report['samples'].append({'elapsed': round(elapsed, 4), 'actor_position_cm': coordinates(player.get_actor_location())})
    for when, label in SHOTS:
        if label not in report['screenshots'] and elapsed >= when:
            report['screenshots'].append(label)
            unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, f'sword_attack01_{label}.png')
            break
    if elapsed < report['play_duration_seconds'] + SETTLE_SECONDS and elapsed < 8.0:
        return
    unreal.unregister_slate_post_tick_callback(handle)
    report['end_position_cm'] = coordinates(player.get_actor_location())
    report['travel_distance_cm'] = math.dist(report['start_position_cm'], report['end_position_cm'])
    report['passed'] = 1.7 < report['play_duration_seconds'] < 1.95 and 30 < report['travel_distance_cm'] < 40 and not report['mesh_animation_enabled']
    path = Path('C:/Project/TDGame/Docs/Validation/BlenderAnimation/sword-attack01-pie.json')
    path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.log('TD_SWORD_ATTACK01_PIE ' + str(report['passed']) + ' travel_cm=' + str(report['travel_distance_cm']))


handle = unreal.register_slate_post_tick_callback(capture_tick)
print('Sword attack action runtime capture scheduled')
