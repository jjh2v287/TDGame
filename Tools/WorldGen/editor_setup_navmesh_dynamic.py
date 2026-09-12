"""에디터 안에서 실행: 현재 레벨의 내비메시를 런타임 동적 생성(Dynamic)으로 되돌린다(정적 월드 파티션 내비메시의 반대 설정).

설정: RecastNavMesh `runtime_generation=DYNAMIC`, `is_world_partitioned=False`, `fixed_tile_pool_size=False`,
      정적 빌드가 남긴 NavigationDataChunkActor 전부 삭제, 레벨 저장.
심리스 이동 준비 판정(스트리밍 완료 + 내비 투영)은 이 설정에서 검증됨(TDTravelToDungeon 0.48s / TDTravelToField 10.5s).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_setup_navmesh_dynamic.py
"""
import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()
navs = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh)
if not navs:
    print("RecastNavMesh 없음: NavMeshBoundsVolume이 있으면 내비 시스템이 만든다. 레벨을 다시 열어 보세요.")
for nav in navs:
    for name, value in (("is_world_partitioned", False), ("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC), ("fixed_tile_pool_size", False)):
        try:
            nav.set_editor_property(name, value)
        except Exception as e:
            print("skip", name, str(e)[:100])
    print("navmesh:", nav.get_actor_label(), nav.get_editor_property("runtime_generation"), "wp:", nav.get_editor_property("is_world_partitioned"))
chunks = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == "NavigationDataChunkActor"]
for a in chunks:
    eas.destroy_actor(a)
print("nav chunk actors removed:", len(chunks))
print("saved:", unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
