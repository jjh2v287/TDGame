"""에디터 안에서 실행: 현재 레벨의 내비메시를 월드 파티션 정적 생성으로 설정한다(P1-02).

설정: RecastNavMesh `is_world_partitioned=True`, `runtime_generation=STATIC`, `fixed_tile_pool_size=True`, `tile_pool_size=4096`,
      WorldSettings `navigation_data_chunk_grid_size`(기본 25600×4 = 102400cm → 런타임 셀 크기 배수로 조정), 레벨 저장.
그다음 밖에서 빌드: UnrealEditor-Cmd.exe TDGame.uproject -run=WorldPartitionBuilderCommandlet /Game/Level/LV_DarkFantasy_OpenWorld -Builder=WorldPartitionNavigationDataBuilder -AllowCommandletRendering -unattended
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_setup_navmesh_wp.py
"""
import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()
navs = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh)
if not navs:
    print("RecastNavMesh 없음: NavMeshBoundsVolume이 있으면 내비 시스템이 만든다. 레벨을 다시 열어 보세요.")
for nav in navs:
    for name, value in (("is_world_partitioned", True), ("runtime_generation", unreal.RuntimeGenerationType.STATIC), ("fixed_tile_pool_size", True), ("tile_pool_size", 4096)):
        try:
            nav.set_editor_property(name, value)
        except Exception as e:
            print("skip", name, str(e)[:100])
    print("navmesh:", nav.get_actor_label(), nav.get_editor_property("runtime_generation"), "wp:", nav.get_editor_property("is_world_partitioned"))
ws = world.get_world_settings()
try:
    ws.set_editor_property("navigation_data_chunk_grid_size", 51200)
    print("chunk grid size:", ws.get_editor_property("navigation_data_chunk_grid_size"))
except Exception as e:
    print("chunk grid size skip:", str(e)[:120])
print("saved:", unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
