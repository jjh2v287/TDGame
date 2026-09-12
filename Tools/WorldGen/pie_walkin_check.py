"""에디터 안에서 실행(PIE 실행 중): 입구 액터의 선로딩 구·이동 상자로 폰을 옮겨 걸어 들어가는 왕복을 흉내 낸다(P1-05).

단계 인자 Saved/WorldGen/pie_travel_step.txt: "approach <DungeonId>" (선로딩 구 안으로) | "enter <DungeonId>" (이동 상자 안으로) | "exit <DungeonId>" (던전 안 출구 상자로) | "where"
실행: python Tools/run_in_editor.py Tools/WorldGen/pie_walkin_check.py
"""
import os

import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_game_world()
step_path = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "WorldGen", "pie_travel_step.txt")
step = open(step_path, encoding="utf-8").read().strip() if os.path.exists(step_path) else "where"
if world is None:
    print("PIE world not running")
else:
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    parts = step.split()
    if parts and parts[0] in ("approach", "enter", "exit") and len(parts) > 1:
        want_exit = parts[0] == "exit"
        target = None
        for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDungeonEntrance):
            if str(a.get_editor_property("dungeon_id")) == parts[1] and bool(a.get_editor_property("is_exit")) == want_exit:
                target = a
        if target is None:
            print("entrance not found:", parts)
        else:
            loc = target.get_actor_location()
            if parts[0] == "approach":
                dist = float(target.get_editor_property("preload_distance_cm")) * 0.6
                dest = unreal.Vector(loc.x - dist, loc.y, loc.z + 100.0)
            else:
                dest = unreal.Vector(loc.x, loc.y, loc.z + 100.0)
            pawn.set_actor_location(dest, False, True)
            print(parts[0], parts[1], "->", dest)
    print("pawn:", pawn.get_actor_location() if pawn else None)
