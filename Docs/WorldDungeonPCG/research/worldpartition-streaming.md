[← 인덱스로](../../WorldDungeonPCG_Plan.md)

# 리서치: 월드 파티션 스트리밍 소스와 완료 판정 (UE 5.8 엔진 헤더 확인, 2026-09-09)

경로 기준 `Engine/Source/Runtime/Engine`.

## 1. 스트리밍 소스 컴포넌트
- `UWorldPartitionStreamingSourceComponent : UActorComponent, IWorldPartitionStreamingSourceProvider` — `Classes/Components/WorldPartitionStreamingSourceComponent.h:16`.
- `EnableStreamingSource()/DisableStreamingSource()/IsStreamingSourceEnabled()` `:28-36`(단순 bool 토글), `GetStreamingSource(FWorldPartitionStreamingSource&)` `:39`, **`bool IsStreamingCompleted() const`** `:44`(이 컴포넌트가 요청한 셀이 모두 준비됐는지).
- 프로퍼티: `TargetGrids`(`TArray<FName>`) `:61`, `TargetHLODLayers`, `Shapes`(`TArray<FStreamingSourceShape>`) `:79`, `Priority`(`EStreamingSourcePriority`) `:82`, `TargetState`(`EStreamingSourceTargetState`) `:85`, `bStreamingSourceEnabled` `:90`. 이전 이름 `TargetGrid`(단수)는 Deprecated.

## 2. 소스 제공자 인터페이스와 구조체 (`Public/WorldPartition/WorldPartitionStreamingSource.h`)
- `IWorldPartitionStreamingSourceProvider`: `virtual bool GetStreamingSource(FWorldPartitionStreamingSource&) const` `:534`, `virtual bool GetStreamingSources(TArray<FWorldPartitionStreamingSource>&) const` `:539`(기본은 단수 호출), `GetStreamingSourceOwner()`. 액터 없이 서브시스템이 직접 구현 가능.
- `FWorldPartitionStreamingSource` `:341`: `Name` `:401`, `Location` `:404`, `Rotation` `:407`, `TargetState` `:410`, `bBlockOnSlowLoading` `:413`, `Priority` `:416`, `TargetGrids`, `Shapes` `:434`, `bRemote` `:440`.
- `FStreamingSourceShape` `:95`: `bUseGridLoadingRange` `:111`, `LoadingRangeScale` `:115`, `Radius` `:119`, `bIsSector` `:123`, `SectorAngle`, `Location` `:131`, `Rotation` `:135`.
- `FWorldPartitionStreamingQuerySource` `:240`: `Location` `:268`, `Radius` `:272`, `bUseGridLoadingRange` `:276`, `DataLayers`, `bDataLayersOnly` `:284`, `bSpatialQuery` `:293`, `Rotation` `:296`, `Shapes` `:303`.
- 열거: `EStreamingSourceTargetState { Loaded, Activated }` `:218`, `EStreamingSourcePriority { Highest=0, High=64, Normal=128, Low=192, Lowest=255 }` `:328`, `EWorldPartitionRuntimeCellState { Unloaded, Loaded, Activated }` — `Public/WorldPartition/WorldPartitionRuntimeCell.h:202`.

## 3. 서브시스템 (`Public/WorldPartition/WorldPartitionSubsystem.h`)
- `bool IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState) const` `:92`(BlueprintCallable, 임의 위치·반경 질의).
- `bool IsAllStreamingCompleted()` `:96`.
- `bool IsStreamingCompleted(const IWorldPartitionStreamingSourceProvider* = nullptr) const` `:102`(특정 제공자 기준).
- `RegisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider*)` `:108`, `UnregisterStreamingSourceProvider` `:110`.
- `OnStreamingStateUpdated()` 델리게이트 `:116`.

## 4. 플레이어 컨트롤러 (`Classes/GameFramework/PlayerController.h`)
- `bEnableStreamingSource` `:557`, `bStreamingSourceShouldActivate` `:561`, `bStreamingSourceShouldBlockOnSlowStreaming` `:565`, `StreamingSourcePriority`, `StreamingSourceDebugColor`, `StreamingSourceShapes` `:577`. 가상 함수 `StreamingSourceShouldActivate()` 등은 `:768` 부근에서 오버라이드 가능.

## 5. 월드 파티션 설정 (`Public/WorldPartition/WorldPartition.h`)
- `bEnableStreaming` `:560`(WorldPartitionSetup 카테고리, 셀 크기·로딩 범위 등은 이 값이 켜져야 편집 가능).
- 런타임 해시 기본 클래스: `BaseEngine.ini:3953 RuntimeHashDefaultClass=/Script/Engine.WorldPartitionRuntimeHashSet` → 5.8 새 월드는 **RuntimeHashSet**.

## 6. 로딩 범위 공개 API (리플렉션 불필요)
- `UWorldPartitionRuntimeHashSet::ResolveRuntimePartition(FName GridName, bool bMainPartitionLayer=false) const` → `const URuntimePartition*` — `Public/WorldPartition/RuntimeHashSet/WorldPartitionRuntimeHashSet.h:252`(public).
- `URuntimePartition::LoadingRange`(int32) — `RuntimeHashSet/RuntimePartition.h:104`, `Name` `:89`. `URuntimePartitionLHGrid::GetCellSize()` — `RuntimePartitionLHGrid.h:46`(기본 `CellSize = 25600`).
- `FRuntimePartitionStreamingData::GetLoadingRange()` `:105`는 public이지만 배열 `RuntimeStreamingData` `:266`은 private, 순회 함수 `ForEachStreamingData` `:247`도 private → 그리드 이름으로는 `ResolveRuntimePartition`을 쓴다. UKGame이 리플렉션으로 읽던 것을 이 API로 대체한다.

## 7. 텔레포트 선로딩 절차 (이 프로젝트)
1. `UTDSeamlessTravelSubsystem`이 `IWorldPartitionStreamingSourceProvider`를 구현하고 `RegisterStreamingSourceProvider(this)`.
2. 진입 요청 시 목적지 `FWorldPartitionStreamingSource{ Location=Entry, TargetState=Activated, Priority=Highest, Shapes=[{bUseGridLoadingRange=true}] }`를 제공 목록에 추가.
3. 매 틱 `IsStreamingCompleted(this)`와 내비 `ProjectPointToNavigation` 성공을 확인. 타임아웃(설정, 예 20초)이면 연출을 연장하고 경고 로그.
4. 이동 후 `Settling` 상태에서 플레이어 소스가 셀을 유지하는지 확인(다음 `OnStreamingStateUpdated`) → 목적지 소스 제거 → 필드 셀은 거리로 언로드.
5. 서버 권한: 스트리밍 소스와 데이터 레이어 변경은 서버에서만. 싱글플레이(스탠드얼론)는 모두 허용.
