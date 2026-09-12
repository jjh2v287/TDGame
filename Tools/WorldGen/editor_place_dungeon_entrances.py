"""에디터 안에서 실행: 아틀라스 정의의 슬롯마다 필드 입구(ATDDungeonEntrance)와 던전 안 출구를 배치한다(P1-05 검증용).

필드 입구 위치 = 슬롯 FieldReturnTransform 근처(레벨의 TDDungeonEntrance_<id> 마커가 있으면 그 위치), 출구 = 슬롯 ExitTransform(월드).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_place_dungeon_entrances.py
"""
import unreal

EAL = unreal.EditorAssetLibrary
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()
atlas = EAL.load_asset("/Game/World/Definitions/DA_TDDungeonAtlas_Main")
if atlas is None:
    raise RuntimeError("DA_TDDungeonAtlas_Main 없음")
by_label = {a.get_actor_label(): a for a in eas.get_all_level_actors()}
for a in list(by_label.values()):
    if a.get_actor_label().startswith("TDGen_Entrance_") or a.get_actor_label().startswith("TDGen_Exit_"):
        eas.destroy_actor(a)
placed = []
for slot in atlas.get_editor_property("slots"):
    did = str(slot.get_editor_property("dungeon_id"))
    ret = slot.get_editor_property("field_return_transform")
    marker = by_label.get(f"TDDungeonEntrance_{did}")
    loc = marker.get_actor_location() if marker else ret.translation
    entrance = eas.spawn_actor_from_class(unreal.TDDungeonEntrance, unreal.Vector(loc.x, loc.y, loc.z + 20.0), unreal.Rotator(0, 0, 0))
    entrance.set_editor_property("dungeon_id", did)
    entrance.set_editor_property("is_exit", False)
    entrance.set_actor_label(f"TDGen_Entrance_{did}")
    entrance.set_folder_path("TDGen/Entrances")
    exit_tf = slot.get_editor_property("exit_transform")
    ex = exit_tf.translation
    if ex.x == 0 and ex.y == 0:
        ex = slot.get_editor_property("world_transform").translation + unreal.Vector(600.0, 400.0, 0.0)
    exit_actor = eas.spawn_actor_from_class(unreal.TDDungeonEntrance, unreal.Vector(ex.x, ex.y, ex.z + 20.0), unreal.Rotator(0, 0, 0))
    exit_actor.set_editor_property("dungeon_id", did)
    exit_actor.set_editor_property("is_exit", True)
    exit_actor.set_actor_label(f"TDGen_Exit_{did}")
    exit_actor.set_folder_path("TDGen/Entrances")
    placed.append((did, [round(loc.x), round(loc.y)], [round(ex.x), round(ex.y)]))
ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
print("entrances placed:", placed, "saved:", ok)
