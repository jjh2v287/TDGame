"""에디터 안에서 실행(PIE 실행 중): 심리스 이동 왕복을 콘솔 명령으로 시험하고 폰 위치를 출력한다(P1-04/P1-08 수동 검증).

단계 인자는 Saved/WorldGen/pie_travel_step.txt 에 적는다: "to MainCrypt" | "field" | "where".
실행: python Tools/run_in_editor.py Tools/WorldGen/pie_travel_check.py
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
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if step.startswith("to "):
        unreal.SystemLibrary.execute_console_command(world, "TDTravelToDungeon " + step[3:].strip(), pc)
        print("requested travel to", step[3:].strip())
    elif step == "field":
        unreal.SystemLibrary.execute_console_command(world, "TDTravelToField", pc)
        print("requested travel to field")
    sub = world.get_subsystem_by_class(unreal.TDSeamlessTravelSubsystem) if hasattr(world, "get_subsystem_by_class") else None
    lib = getattr(unreal, "SubsystemBlueprintLibrary", None)
    if lib is not None and sub is None:
        try:
            sub = lib.get_world_subsystem(world, unreal.TDSeamlessTravelSubsystem)
        except Exception:
            sub = None
    state = None
    if sub:
        for name in ("get_travel_state", "travel_state", "current_state"):
            try:
                state = getattr(sub, name)() if callable(getattr(sub, name, None)) else sub.get_editor_property(name)
                break
            except Exception:
                continue
    print("pawn:", pawn.get_actor_location() if pawn else None, "state:", state)
