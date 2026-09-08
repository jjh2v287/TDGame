[← 인덱스로](../../WorldDungeonPCG_Plan.md)

# 리서치: 액터 안정 식별자와 월드 파티션 내비메시 (UE 5.8 엔진 소스 확인, 2026-09-09)

경로 기준 `Engine/Source/Runtime`.

## A. 액터 안정 식별자

### A-1 `AActor::GetActorGuid()`는 에디터 전용 (검증 완료)
- 멤버 `ActorGuid`/`ActorInstanceGuid`는 `#if WITH_EDITORONLY_DATA`(`Engine/Classes/GameFramework/Actor.h:1088`), 접근자 `GetActorGuid()`/`GetActorInstanceGuid()`는 `#if WITH_EDITOR` 블록(`:1147` 시작, `:1180/1183`). 2026-09-09 헤더 직접 확인. **런타임 게임 코드에서 호출하면 컴파일되지 않는다.**
- 쿠킹 빌드에서는 `AActor::Serialize`가 GUID 쌍을 별도 직렬화하고(`Actor.cpp:1047-1050`) 로드 시 전역 어노테이션에 넣는다(`ActorInstanceGuids.cpp:192-206`). 런타임 조회 API는 A-2다.

### A-2 `FActorInstanceGuid` (레벨 인스턴스 대응, 수명 함정)
- `FActorInstanceGuid::GetActorInstanceGuid(const AActor&)` — `Engine/Public/WorldPartition/ActorInstanceGuids.h:26`. 같은 레벨 인스턴스 에셋을 여러 번 배치해도 `FGuid::Combine(LevelInstanceGuid, ActorGuid)`로 인스턴스별 다른 GUID(`ActorInstanceGuids.cpp:146-158`).
- **함정**: `AActor::PostRegisterAllComponents()` 마지막 줄에서 어노테이션을 해제한다(`Engine/Private/Actor.cpp:4140`). 쿠킹 빌드에서 `BeginPlay`에 읽으면 무효 GUID다. `PostRegisterAllComponents()` 오버라이드에서 `Super` 호출 **전에** 읽어 캐시해야 한다. PIE에서는 항상 유효해서 이 버그를 놓치기 쉽다.

### A-3 액터 이름
- 메인 컨테이너 배치 액터의 `_UAID_` 이름은 쿠킹까지 유지되지만(`LevelActor.cpp:349`, `WorldPartitionLevelHelper.cpp:270`), 레벨 인스턴스 내부 액터는 컨테이너 ID 해시가 삽입돼 이름이 바뀌고(`:1032`), 런타임 스폰 액터는 세션마다 다르다. 세이브 키로 쓰지 않는다.

### A-4 에디터 전용
- `FWorldPartitionActorDesc::GetGuid()`, `FStreamingGenerationActorDescView`, `FWorldPartitionRuntimeCellObjectMapping::ActorInstanceGuid`는 모두 에디터 전용. 런타임 세이브 로직의 근거로 쓸 수 없다.

### A-5 권장
- 저장 대상 액터에 비-Transient `UPROPERTY(SaveGame) FGuid`를 두고, `PostRegisterAllComponents` 전반부(`Super` 호출 전)에서 `FActorInstanceGuid::GetActorInstanceGuid`로 한 번 채워 자체 소유한다. 에디터에서는 `#if WITH_EDITOR` 아래 `GetActorGuid()`로 미리 채워 저장해도 된다. 생성기가 배치한 액터는 생성 경로에서 결정론적으로 파생한 GUID를 베이크 시 기록한다.

## B. 월드 파티션 내비메시

### B-1 청크 액터 스트리밍
- `ANavigationDataChunkActor : APartitionActor` — `Engine/Public/WorldPartition/NavigationData/NavigationDataChunkActor.h:12`. `BeginPlay`에서 `AddNavigationDataChunkToWorld()`(`.cpp:102-124`), `EndPlay`에서 제거. 항상 공간 로딩(`CanChangeIsSpatiallyLoadedFlag()==false`).
- 격자: `AWorldSettings::NavigationDataChunkGridSize`(기본 102400 = 1024m) — `Engine/Classes/GameFramework/WorldSettings.h:567`, `NavigationDataBuilderLoadingCellSize` `:575`. 런타임 셀 크기와 어긋나면 청크 로드 타이밍이 어긋나므로 정렬한다.
- 빌더: `UWorldPartitionNavigationDataBuilder` — `Editor/UnrealEd/Public/WorldPartition/WorldPartitionNavigationDataBuilder.h:11`.

### B-2 런타임 생성 옵션
- `ERuntimeGenerationType { Static, DynamicModifiersOnly, Dynamic, LegacyGeneration }` — `NavigationSystem/Public/NavigationData.h:523-533`, 프로퍼티 `RuntimeGeneration` `:583`.
- `ARecastNavMesh::SupportsStreaming()` = `RuntimeGeneration != Dynamic || bIsWorldPartitioned` — `RecastNavMesh.cpp:4385-4390`. 즉 월드 파티션에서는 "베이크 청크 스트리밍 + Dynamic 갱신" 혼합이 가능하다. `bIsWorldPartitioned`(`RecastNavMesh.h:835`)가 켜지면 액티브 타일 방식이 강제된다.
- Static + WorldPartitioned: 청크만 붙고 재생성 없음(가장 빠르고 예측 가능, 런타임 지오메트리 변경 미반영). 이 프로젝트의 베이크 중심 설계에 맞는 기본값.

### B-3 텔레포트 직후 준비 확인
- `UNavigationSystemV1::IsNavigationBuilt()`는 Static 모드 게임 월드에서 항상 true(`NavigationSystem.cpp:2568-2572`). `IsNavigationBeingBuilt()`도 청크 attach는 빌드 작업이 아니라 false. **청크가 붙었는지 알리는 델리게이트는 없다.**
- 실질 수단: 목표 지점에 `ProjectPointToNavigation(Point, OutLocation, Extent, NavData, Filter)` — `NavigationSystem.h:718`을 성공할 때까지 프레임 폴링(타임아웃 포함). 도달성까지 보려면 `GetRandomReachablePointInRadius` `:691`. Dynamic 모드면 `OnNavigationGenerationFinishedDelegate` `:443`도 병행.
- 청크 attach 체인: 셀 로드 → `ANavigationDataChunkActor::BeginPlay` → `UNavigationSystemV1::AddNavigationDataChunk`(`NavigationSystem.cpp:2475-2485`) → `ARecastNavMesh::OnStreamingNavDataAdded` → `AttachNavMeshDataChunk`(`RecastNavMesh.cpp:3684-3726`).

## 이 프로젝트 적용
- `UTDSeamlessTravelSubsystem`의 완료 조건 = 셀 스트리밍 완료 질의 + 목적지 `ProjectPointToNavigation` 성공(둘 다 타임아웃). 내비는 Static + WorldPartitioned로 베이크, 청크 그리드를 런타임 셀 크기에 맞춘다.
- 안정 ID는 `UTDPersistentStateComponent`가 `PostRegisterAllComponents` 전반부에 채운다.
