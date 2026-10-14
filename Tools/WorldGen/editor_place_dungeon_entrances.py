"""에디터 안에서 실행: 아틀라스 정의의 슬롯마다 던전 안 출구(ATDDungeonEntrance, is_exit)를 다시 배치한다. 필드 쪽 입구는 D-41에 따라 만들지도 지우지도 않는다(이계 관문 베이크 editor_build_realm.py가 대체).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_place_dungeon_entrances.py (PowerShell)
출력: 레벨 액터 TDGen_Exit_<DungeonId>(폴더 TDGen/Entrances) + 더티 패키지 저장
상태: 현행

출구 위치 = 슬롯 ExitTransform(월드), 비어 있으면 WorldTransform + (600, 400). 던전은 콘솔 TDTravelToDungeon <DungeonId>로 들어간다.
"""
import unreal

EAL = unreal.EditorAssetLibrary
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()
atlas = EAL.load_asset("/Game/World/Definitions/DA_TDDungeonAtlas_Main")
if atlas is None:
    raise RuntimeError("DA_TDDungeonAtlas_Main 없음")
for a in eas.get_all_level_actors():
    if a.get_actor_label().startswith("TDGen_Exit_"):
        eas.destroy_actor(a)
placed = []
for slot in atlas.get_editor_property("slots"):
    did = str(slot.get_editor_property("dungeon_id"))
    exit_tf = slot.get_editor_property("exit_transform")
    ex = exit_tf.translation
    if ex.x == 0 and ex.y == 0:
        ex = slot.get_editor_property("world_transform").translation + unreal.Vector(600.0, 400.0, 0.0)
    exit_actor = eas.spawn_actor_from_class(unreal.TDDungeonEntrance, unreal.Vector(ex.x, ex.y, ex.z + 20.0), unreal.Rotator(0, 0, 0))
    exit_actor.set_editor_property("dungeon_id", did)
    exit_actor.set_editor_property("is_exit", True)
    exit_actor.set_actor_label(f"TDGen_Exit_{did}")
    exit_actor.set_folder_path("TDGen/Entrances")
    placed.append((did, [round(ex.x), round(ex.y)]))
field_entrances = [a.get_actor_label() for a in eas.get_all_level_actors() if a.get_actor_label().startswith("TDGen_Entrance_")]
ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
print("exits placed:", placed, "field entrances left untouched (D-41):", field_entrances, "saved:", ok)
