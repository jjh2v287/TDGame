"""에디터 안에서 실행(PIE 실행 중): 영속 상자 상태가 스트리밍·세이브를 견디는지 확인한다(P1-06/07).

단계 인자 Saved/WorldGen/pie_travel_step.txt: "chest open" | "chest status" | "chest save <slot>" | "chest load <slot>"
레벨에 ATDPersistentTestChest(라벨 TDGen_TestChest_*)가 있어야 한다(없으면 에디터에서 Tools/WorldGen/editor_place_test_chest.py).
실행: python Tools/run_in_editor.py Tools/WorldGen/pie_chest_check.py
"""
import os

import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_game_world()
step_path = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "WorldGen", "pie_travel_step.txt")
step = open(step_path, encoding="utf-8").read().strip() if os.path.exists(step_path) else "chest status"
if world is None:
    print("PIE world not running")
else:
    parts = step.split()
    chests = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDPersistentTestChest)
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    if len(parts) >= 2 and parts[1] == "open":
        for c in chests:
            c.open()
        print("opened", len(chests))
    elif len(parts) >= 3 and parts[1] == "save":
        unreal.SystemLibrary.execute_console_command(world, "TDSaveWorldState " + parts[2], pc)
        print("save requested", parts[2])
    elif len(parts) >= 3 and parts[1] == "load":
        unreal.SystemLibrary.execute_console_command(world, "TDLoadWorldState " + parts[2], pc)
        print("load requested", parts[2])
    for c in chests:
        comp = c.get_component_by_class(unreal.TDPersistentStateComponent)
        sid = comp.get_editor_property("stable_id") if comp else None
        print("chest:", c.get_actor_label(), "opened:", c.get_editor_property("is_opened"), "stable_id:", sid, "loc:", c.get_actor_location())
    print("chests loaded:", len(chests))
