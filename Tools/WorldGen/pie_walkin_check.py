"""에디터 안에서 실행(PIE 실행 중): 입구·관문 액터의 선로딩 구·이동 상자로 폰을 옮겨 걸어 들어가는 왕복을 흉내 내거나 콘솔로 이동을 요청하고 폰 위치·이동 상태를 출력한다(P1-05, P3-13).
실행: python Tools/run_in_editor.py Tools/WorldGen/pie_walkin_check.py (PowerShell, 단계 인자는 Saved/WorldGen/pie_travel_step.txt)
출력: 표준 출력 "pawn_cm: x=.. y=.. z=.. state=.. last=.. realm=.." 한 줄(+ 이동한 단계 줄)
상태: 현행

단계: "approach <Id>" (관문 향(액터 정면) 쪽 선로딩 반경×0.6 지점의 랜드스케이프 지면 위 100cm로, 지면을 못 찾으면 입구 높이) | "enter <Id>" (이동 상자 안으로) | "exit <Id>" (던전·이계 안 출구 상자로) | "to <Id>" · "field" (활성 선로딩을 취소하고 같은 실행에서 콘솔 TDTravelToDungeon·TDTravelToField) | "where"
취소와 콘솔 요청을 한 실행에 묶는 이유: 상태가 Idle이 되면 입구 액터가 다음 틱에 폰이 선로딩 구 안에 있는지 보고 선로딩을 다시 건다.
Id는 아틀라스 던전 ID(MainCrypt 등) 또는 이계 관문 ID(SunkenStarfieldSouthwest 등). 이동 상태는 정적 접근자로 읽는다(L-editor-07), 없으면 None.
"""
import os

import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_game_world()
step_path = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "WorldGen", "pie_travel_step.txt")
step = open(step_path, encoding="utf-8").read().strip() if os.path.exists(step_path) else "where"
GROUND_TRACE_HALF_SPAN_CM = 20000.0
GROUND_TRACE_MAX_ATTEMPTS = 8


def find_entrance(destination_id, want_exit):
    target = None
    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDungeonEntrance):
        if str(a.get_editor_property("dungeon_id")) == destination_id and bool(a.get_editor_property("is_exit")) == want_exit:
            target = a
    return target


def find_landscape_ground_z(x, y, reference_z, ignored_actors):
    ignored = list(ignored_actors)
    start = unreal.Vector(x, y, reference_z + GROUND_TRACE_HALF_SPAN_CM)
    end = unreal.Vector(x, y, reference_z - GROUND_TRACE_HALF_SPAN_CM)
    for _ in range(GROUND_TRACE_MAX_ATTEMPTS):
        hit = unreal.SystemLibrary.line_trace_single(world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, ignored, unreal.DrawDebugTrace.NONE, True)
        if hit is None:
            return None
        hit_values = hit.to_tuple()
        hit_actor = hit_values[9]
        if hit_actor is None or isinstance(hit_actor, unreal.LandscapeProxy):
            return hit_values[4].z
        ignored.append(hit_actor)
    return None


def find_approach_destination(pawn, target, loc):
    dist = float(target.get_editor_property("preload_distance_cm")) * 0.6
    forward = target.get_actor_forward_vector()
    x = loc.x + forward.x * dist
    y = loc.y + forward.y * dist
    ground_z = find_landscape_ground_z(x, y, loc.z, [pawn])
    if ground_z is None:
        print("approach ground not found, using entrance height:", x, y)
        return unreal.Vector(x, y, loc.z + 100.0)
    return unreal.Vector(x, y, ground_z + 100.0)


def move_pawn_to_entrance(pawn, parts):
    target = find_entrance(parts[1], parts[0] == "exit")
    if target is None:
        print("entrance not found:", parts)
        return
    loc = target.get_actor_location()
    dest = unreal.Vector(loc.x, loc.y, loc.z + 100.0)
    if parts[0] == "approach":
        dest = find_approach_destination(pawn, target, loc)
    pawn.set_actor_location(dest, False, True)
    print(parts[0], parts[1], "->", dest)


def find_travel_subsystem():
    travel_class = getattr(unreal, "TDSeamlessTravelSubsystem", None)
    if travel_class is None:
        return None
    return travel_class.get_seamless_travel_subsystem(world)


def cancel_active_preload():
    travel = find_travel_subsystem()
    if travel is None:
        print("travel subsystem unavailable: preload not cancelled")
        return
    active_id = travel.get_active_dungeon_id()
    cancelled = travel.cancel_preload(active_id, True) or travel.cancel_preload(active_id, False)
    print("cancel preload", active_id, "->", cancelled)


def request_console_travel(parts):
    cancel_active_preload()
    command = "TDTravelToField" if parts[0] == "field" else "TDTravelToDungeon " + parts[1]
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    unreal.SystemLibrary.execute_console_command(world, command, controller)
    print("console:", command)


def read_travel_status():
    status = {"state": None, "last": None, "realm": None}
    try:
        travel = find_travel_subsystem()
        state = travel.get_travel_state()
        status["state"] = getattr(state, "name", str(state))
        status["last"] = str(travel.get_last_traveled_dungeon_id())
    except Exception as e:
        print("travel subsystem unavailable:", e)
    realm_class = getattr(unreal, "TDRealmSubsystem", None)
    try:
        status["realm"] = str(realm_class.get_realm_subsystem(world).get_active_realm_id())
    except Exception as e:
        print("realm subsystem unavailable:", e)
    return status


if world is None:
    print("PIE world not running")
else:
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    parts = step.split()
    if pawn and len(parts) > 1 and parts[0] in ("approach", "enter", "exit"):
        move_pawn_to_entrance(pawn, parts)
    if parts[:1] == ["field"] or (len(parts) > 1 and parts[0] == "to"):
        request_console_travel(parts)
    status = read_travel_status()
    if pawn is None:
        print("pawn_cm: none", f"state={status['state']} last={status['last']} realm={status['realm']}")
    else:
        p = pawn.get_actor_location()
        print(f"pawn_cm: x={p.x:.1f} y={p.y:.1f} z={p.z:.1f} state={status['state']} last={status['last']} realm={status['realm']}")
