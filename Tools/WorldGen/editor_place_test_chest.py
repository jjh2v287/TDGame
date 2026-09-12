"""에디터 안에서 실행: 영속 상태 테스트 상자(ATDPersistentTestChest)를 메인 던전 입구 옆과 마을에 놓고 저장한다(P1-06 검증용).

실행: python Tools/run_in_editor.py Tools/WorldGen/editor_place_test_chest.py
"""
import unreal

EAL = unreal.EditorAssetLibrary
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()
for a in list(eas.get_all_level_actors()):
    if a.get_actor_label().startswith("TDGen_TestChest_"):
        eas.destroy_actor(a)
mesh = EAL.load_asset("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenChest")
placed = []
for name, loc in (("Village", unreal.Vector(-10300.0, 2900.0, 530.0)), ("MainCryptGate", unreal.Vector(29000.0, -26200.0, 2140.0))):
    chest = eas.spawn_actor_from_class(unreal.TDPersistentTestChest, loc, unreal.Rotator(0, 0, 0))
    chest.set_actor_label(f"TDGen_TestChest_{name}")
    chest.set_folder_path("TDGen/Persistence")
    smc = chest.get_component_by_class(unreal.StaticMeshComponent)
    if smc and mesh:
        smc.set_static_mesh(mesh)
    placed.append(name)
print("saved:", unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True), "chests:", placed)
