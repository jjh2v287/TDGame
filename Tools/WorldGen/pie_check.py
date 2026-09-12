"""에디터 안에서 실행(PIE 실행 중): 플레이어 폰 위치·지면 높이·내비메시 투영 결과를 출력한다.

MCP `EditorToolset.EditorAppToolset.StartPIE` 로 PIE를 켠 뒤 몇 초 후 실행한다.
"""
import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_game_world()
if world is None:
    print("PIE world not running")
else:
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    print("pawn:", pawn.get_class().get_name() if pawn else None)
    if pawn:
        loc = pawn.get_actor_location()
        print("pawn location:", loc)
        landscape = None
        for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeProxy):
            if a.get_class().get_name() == "Landscape":
                landscape = a
        if landscape:
            res = unreal.TDLandscapeEditorLibrary.try_get_landscape_height_at_location(landscape, loc)
            print("landscape height at pawn:", res)
        nav = unreal.NavigationSystemV1.get_navigation_system(world)
        if nav:
            proj = unreal.NavigationSystemV1.project_point_to_navigation(world, loc, None, None, unreal.Vector(300, 300, 500))
            print("nav projection:", proj)
        starts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)
        print("player starts:", [str(s.get_actor_location()) for s in starts])
        hism = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDInstancedMeshActor)
        print("instanced mesh actors loaded:", len(hism))
