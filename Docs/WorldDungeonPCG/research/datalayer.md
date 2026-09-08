[← 인덱스로](../../WorldDungeonPCG_Plan.md)

# 리서치: 데이터 레이어 런타임 API (UE 5.8 엔진 소스 확인, 2026-09-09)

경로 기준 `Engine/Source/Runtime/Engine`, `Public/…`·`Private/…`로 줄임.

## 1. UDataLayerManager
- 접근: `UDataLayerManager::GetDataLayerManager(const T* InObject)` — `Public/WorldPartition/DataLayer/DataLayerManager.h:52`. 블루프린트는 `UWorldPartitionBlueprintLibrary::GetDataLayerManager(UObject*)` — `Public/WorldPartition/WorldPartitionBlueprintLibrary.h:165`.
- 상태 변경: `bool SetDataLayerInstanceRuntimeState(const UDataLayerInstance*, EDataLayerRuntimeState, bool bInIsRecursive=false)` — `DataLayerManager.h:82`; `bool SetDataLayerRuntimeState(const UDataLayerAsset*, EDataLayerRuntimeState, bool bInIsRecursive=false)` — `:94`. 둘 다 현행(Deprecated 아님). `SetDataLayerRuntimeStateByAsset`이라는 함수는 없다.
- 조회: `GetDataLayerInstanceFromAsset` `:66`, `GetDataLayerInstanceRuntimeState(const UDataLayerInstance*)` `:98`, `GetDataLayerInstanceEffectiveRuntimeState` `:102`, `GetDataLayerInstances()` `:62`, `IsAnyDataLayerInEffectiveRuntimeState / IsAllDataLayerInEffectiveRuntimeState(TArrayView<const FName>, State)` `:131-132`(C++ 전용, 인스턴스 이름).
- 델리게이트: `OnDataLayerInstanceRuntimeStateChanged` — `DataLayerManager.h:106`(BlueprintAssignable).
- 반환 `bool`은 요청 수락 여부. 실패는 Verbose 로그뿐이므로 무시하면 안 된다.

## 2. 상태와 Effective 상태
- `enum class EDataLayerRuntimeState : uint8 { Unloaded, Loaded, Activated }` — `Public/WorldPartition/DataLayer/DataLayerInstance.h:22-33`. Loaded = 로드됐지만 보이지 않음(BeginPlay 안 됨), Activated = 로드되고 보임.
- Effective = 자신의 요청 상태와 모든 조상 런타임 레이어 상태의 최솟값 — `Private/WorldPartition/DataLayer/WorldDataLayers.cpp:386-436`. 부모가 Loaded면 자식은 Activated를 요청해도 Loaded로 클램프.
- 셀이 여러 레이어에 속할 때 `DataLayersLogicOperator`(Or/And, `Public/WorldPartition/WorldPartition.h:89-93`)가 결정. 기본값은 (미확인).
- 판정은 반드시 Effective로 한다.

## 3. 애셋·인스턴스
- 런타임 레이어 조건: `UDataLayerAsset::IsRuntime()` — `Public/…/DataLayerAsset.h:62`(`DataLayerType == Runtime`이고 private 아님). Editor 타입에 상태를 걸면 `NotRuntime` 오류로 무시된다(`WorldDataLayers.cpp:86-93`).
- `InitialRuntimeState` — `DataLayerInstance.h:238`. 게임 시작 시 한 번 적용(`WorldDataLayers.cpp:140-169`). 세이브 복원은 그 뒤에 덮어써야 한다.
- 클라이언트/서버 전용은 `EDataLayerLoadFilter { None, ClientOnly, ServerOnly }` — `DataLayerAsset.h:18-26`.
- External Data Layer(`UExternalDataLayerAsset`)는 셀의 상한(cap)으로 작동한다(`WorldPartitionRuntimeHash.cpp:852-885`).

## 4. 권한
- `AWorldDataLayers::CanChangeDataLayerRuntimeState` — `WorldDataLayers.cpp:84-129`. `GetNetMode()` 기준: 일반 런타임 레이어는 클라이언트에서 거부, 스탠드얼론은 모두 허용.
- 매니저의 Set 함수에 `BlueprintAuthorityOnly`가 없어 클라이언트 BP 호출은 조용히 실패한다. 게임 코드에서 `HasAuthority()` 가드 필요.
- 늦게 접속한 클라이언트는 델리게이트를 못 받는다(`OnRep_*`가 브로드캐스트하지 않음, `WorldDataLayers.cpp:351-363`). 접속 시 Effective 상태를 폴링해 동기화.

## 5. 세이브·로드 (엔진 제공 없음)
- 상태 배열은 모두 `Transient`(`WorldDataLayers.h:288-305`). 세이브 API 없음. `OverwriteDataLayerRuntimeStates`는 5.8에서 Deprecated, 에디터 전용, 매치 시작 전만.
- 권장: 저장 시 `GetDataLayerInstances()` 순회 → `IsRuntime()`인 것만 **요청 상태**(Effective 아님)를 `TMap<TSoftObjectPath, EDataLayerRuntimeState>`로 저장. 복원은 월드 BeginPlay 이후, 부모→자식 순서, `bRecursive=false`로 `SetDataLayerRuntimeState(Asset, …)`. 키는 이름이 아니라 애셋 경로.

## 6. 반영 시점
- 상태 세팅은 즉시(같은 프레임) Effective 갱신·델리게이트. 셀 반영은 다음 스트리밍 업데이트 틱(`UWorld::InternalUpdateStreamingState` → `UWorldPartitionSubsystem::OnUpdateStreamingState`, `WorldPartitionSubsystem.cpp:1168-1173`), 실제 레벨 로드는 비동기. 즉 "세팅 → 액터 존재"는 여러 프레임.
- Activated→Loaded는 레벨을 월드에서 제거(액터 사라짐, 패키지 유지). Unloaded는 액터 파괴 → 런타임 상태 소실. 퀘스트 상태는 항상 로드된 곳(서브시스템·세이브)에 둔다.
- 완료 확인: `UWorldPartitionSubsystem::IsStreamingCompleted(EWorldPartitionRuntimeCellState, const TArray<FWorldPartitionStreamingQuerySource>&, bool bExactState)` — `Public/WorldPartition/WorldPartitionSubsystem.h:91-92`. 쿼리 소스에 `DataLayers`, `bDataLayersOnly`, `bIncludeAnyDataLayer` 필드(`WorldPartitionStreamingSource.h:279-291`).
- 퀘스트 단계 전환(A Unloaded, B Activated)은 비동기가 겹치므로 B를 미리 Loaded로 예열한다.

## 7. UDataLayerSubsystem
- 존재하지만 모든 멤버가 Deprecated(5.3). 새 코드에서 쓰지 않는다. 구 API는 애셋 인자, 신 API는 인스턴스 인자라 이름이 같아도 다르다.

## 이 프로젝트 적용
- `UTDWorldStateSubsystem`이 "상태 태그 → {애셋 경로, 원하는 상태}" 표를 적용하고, 세이브에 요청 상태를 기록·복원한다. 전환 전 Loaded 예열, 전환 후 `IsStreamingCompleted`로 확인.
