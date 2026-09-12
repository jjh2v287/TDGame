"""에디터 안에서 실행: 월드 생성기 액터 1개와 던전 슬롯 앵커 3개(설계서 12장 툴 버튼)를 레벨에 놓고 버튼 함수를 한 번씩 호출해 동작을 확인한다.

실행: python Tools/run_in_editor.py Tools/WorldGen/editor_place_generator_actors.py
"""
import unreal

EAL = unreal.EditorAssetLibrary
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()
atlas = EAL.load_asset("/Game/World/Definitions/DA_TDDungeonAtlas_Main")
world_def = EAL.load_asset("/Game/World/Definitions/DA_TDWorld_Main")
theme = EAL.load_asset("/Game/Dungeon/Themes/DA_TDTheme_Crypt")
for a in list(eas.get_all_level_actors()):
    if a.get_actor_label().startswith("TDGen_Tool_"):
        eas.destroy_actor(a)

gen = eas.spawn_actor_from_class(unreal.TDWorldGeneratorActor, unreal.Vector(-12000.0, 3500.0, 700.0), unreal.Rotator(0, 0, 0))
gen.set_editor_property("world_definition", world_def)
gen.set_editor_property("seed", 7)
gen.set_editor_property("bake_after_generate", False)
gen.set_actor_label("TDGen_Tool_WorldGenerator")
gen.set_folder_path("TDGen/Tools")
gen.call_method("ValidateOutdoor")
print("WorldGenerator ValidateOutdoor report head:", str(gen.get_editor_property("last_report"))[:300].replace("\n", " | "))
gen.call_method("GenerateOutdoor")
print("WorldGenerator GenerateOutdoor report head:", str(gen.get_editor_property("last_report"))[:300].replace("\n", " | "))

for slot in atlas.get_editor_property("slots"):
    idx = slot.get_editor_property("slot_index")
    origin = atlas.get_slot_origin_cm(idx)
    anchor = eas.spawn_actor_from_class(unreal.TDDungeonSlotAnchor, unreal.Vector(origin.x - 800.0, origin.y - 800.0, 300.0), unreal.Rotator(0, 0, 0))
    anchor.set_editor_property("atlas", atlas)
    anchor.set_editor_property("slot_index", idx)
    anchor.set_actor_label(f"TDGen_Tool_SlotAnchor_{idx}")
    anchor.set_folder_path("TDGen/Tools")
    anchor.call_method("Validate")
    print(f"SlotAnchor {idx} Validate:", str(anchor.get_editor_property("last_report"))[:240].replace("\n", " | "))

rep = unreal.TDWorldGenEditorLibrary.validate_room_module_levels(theme)
print("room module spec check: score", rep.get_editor_property("score"), "passed", rep.get_editor_property("passed"), "items", len(rep.get_editor_property("items")))
for i in rep.get_editor_property("items"):
    if not str(i.get_editor_property("severity")).endswith("INFO"):
        print("  ", i.get_editor_property("code"), i.get_editor_property("message"))
nav = unreal.TDWorldGenEditorLibrary.validate_dungeon_navigation(world, atlas, 0)
print("dungeon nav check slot0: passed", nav.get_editor_property("passed"), [str(i.get_editor_property("message"))[:100] for i in nav.get_editor_property("items")][:3])
print("saved:", unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
