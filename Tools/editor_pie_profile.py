"""에디터 안(PIE 실행 중)에서 run_in_editor.py로 실행: 실제 프레임 간격을 틱마다 기록하고 `stat dumpframe`·`ProfileGPU`를 로그에 남긴다. pie_profile.py가 PIE를 켠 뒤 넘긴다.
실행: python Tools/pie_profile.py (직접 실행 시 PIE가 켜져 있어야 한다)
출력: Saved/AgentOps/pie_profile.json, Saved/Logs/TDGame.log의 `TD_PROFILE_CMD` 뒤 LogStats·LogRHI 블록
상태: 현행 (2026-09-19)
"""
import json
import time

import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
state = {'started': time.monotonic(), 'warmup': [], 'steady': [], 'stage': 0, 'handle': None, 'last_real': None}


def run(command):
    unreal.SystemLibrary.execute_console_command(world, command)
    unreal.log('TD_PROFILE_CMD ' + command)


def tick(delta):
    now = time.monotonic() - state['started']
    real = unreal.GameplayStatics.get_real_time_seconds(world)
    if state['last_real'] is not None:
        (state['steady'] if now > 4.0 else state['warmup']).append(round(real - state['last_real'], 4))
    state['last_real'] = real
    if state['stage'] == 0 and now > 6.0:
        run('stat dumpframe -ms=15')
        state['stage'] = 1
    elif state['stage'] == 1 and now > 8.0:
        run('ProfileGPU')
        state['stage'] = 2
    elif state['stage'] == 2 and now > 11.0:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        steady = state['steady'] or [0.0]
        report = {'map': world.get_path_name(), 'warmup_ticks': len(state['warmup']), 'steady_ticks': len(steady),
                  'steady_mean_s': sum(steady) / len(steady), 'steady_max_s': max(steady), 'steady_min_s': min(steady),
                  'steady_fps': (len(steady) / sum(steady)) if sum(steady) > 0 else 0.0}
        with open('C:/Project/TDGame/Saved/AgentOps/pie_profile.json', 'w', encoding='utf-8') as handle:
            handle.write(json.dumps(report, indent=1))
        unreal.log('TD_PROFILE_DONE ' + json.dumps(report))


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('pie profile scheduled')
