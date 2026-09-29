"""에디터 안에서 실행(PIE 중): 몬스터 think 서브시스템 상태·플레이어 체력을 기록하고, 플레이어를 옮기거나 주문을 시전하는 탐침 함수 모음. pie_check_monsters.py가 단계별로 호출한다.
실행: python Tools/run_in_editor.py -c "exec(open(r'<이 파일>', encoding='utf-8').read()); probe('snapshot', 'tag')"
출력: Saved/MonsterAI/pie_probe.jsonl (한 줄 한 기록), Saved/Screenshots/WindowsEditor/<이름>.png
상태: 현행
"""
import json
import os

import unreal

PROBE_FILE = os.path.join(unreal.SystemLibrary.get_project_saved_directory(), "MonsterAI", "pie_probe.jsonl")


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def _record(entry):
    os.makedirs(os.path.dirname(PROBE_FILE), exist_ok=True)
    with open(PROBE_FILE, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(entry, ensure_ascii=False) + "\n")
    print(json.dumps(entry, ensure_ascii=False)[:1500])


def _player_state(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        return None, {}
    combat = pawn.get_component_by_class(unreal.TDCombatComponent)
    location = pawn.get_actor_location()
    return pawn, {"health": combat.get_current_health() if combat else None, "location": [round(location.x), round(location.y), round(location.z)]}


def _monsters(world):
    think = unreal.TDMonsterThinkSubsystem.get_monster_think_subsystem(world)
    lines = list(think.get_monster_debug_lines()) if think else []
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter)
    dead = 0
    for actor in actors:
        combat = actor.get_component_by_class(unreal.TDCombatComponent)
        if combat and not combat.is_alive():
            dead += 1
    return {"active": think.get_active_monster_count() if think else -1, "actors": len(actors), "dead": dead, "lines": lines}


def probe(action, tag="", x=0.0, y=0.0, slot=0, shot=""):
    world = _world()
    if world is None:
        _record({"tag": tag, "error": "no PIE world"})
        return
    pawn, player = _player_state(world)
    if action == "teleport" and pawn is not None:
        pawn.set_actor_location(unreal.Vector(x, y, player["location"][2]), False, True)
    if action == "cast" and pawn is not None:
        nearest = None
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter):
            combat = actor.get_component_by_class(unreal.TDCombatComponent)
            if not combat or not combat.is_alive():
                continue
            distance = actor.get_distance_to(pawn)
            if nearest is None or distance < nearest[0]:
                nearest = (distance, actor)
        if nearest is not None:
            pawn.cast_damage_spell(slot, nearest[1].get_actor_location())
    if action == "console":
        unreal.SystemLibrary.execute_console_command(world, tag)
    if shot:
        unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, shot)
    pawn, player = _player_state(world)
    _record({"tag": tag, "action": action, "time": round(unreal.GameplayStatics.get_time_seconds(world), 2), "player": player, "monsters": _monsters(world)})
