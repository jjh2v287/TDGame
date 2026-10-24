# 01. 월드 파티션과 런타임 무한 콘텐츠: 엔진 소스 근거 (UE 5.8, 2026-10-23)

경로 약칭: `E/` = `C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/`, `R/` = `.../Engine/Source/Runtime/`, `PCG/` = `.../Engine/Plugins/PCG/Source/PCG/`. 프로젝트 파일은 저장소 상대 경로.
표기: [검증] = 원문을 이번에 직접 읽음, [추정] = 원문 미확인 또는 계산·추론. 앞선 조사 문서(`Docs/WorldDungeonPCG/research/worldpartition-streaming.md`, `level-instance.md`, `actor-id-and-navmesh.md`)의 내용은 재조사하지 않고 "선행 문서"로 인용한다.

## 0. 결론

1. **월드 파티션(World Partition, 이하 WP)은 런타임에 새 콘텐츠를 스스로 만들지 못한다.** 셀 생성(`GenerateStreaming`)은 에디터·쿠킹 전용이고, 액터의 공간 로딩 플래그·런타임 그리드 지정도 에디터 전용 데이터다. 런타임 스폰 액터는 WP 공간 로딩 대상이 아니다. [검증]
2. **예외 하나: "월드 애셋 스트리밍(World Asset Streaming)"** — `UWorldPartition::RegisterWorldAssetStreaming`으로 쿠킹된 템플릿 레벨을 임의 위치에 **새 런타임 셀로 주입**하면, 그 셀은 메인 그리드의 로딩 범위·플레이어 스트리밍 소스에 따라 비동기로 들어오고 나간다. 단 엔진 주석상 **Experimental(실험)**이고, 엔진·플러그인 안에 사용처가 없다. [검증]
3. 그러므로 "무한"의 그릇은 **우리 C++ 청크 관리자**가 맡고, WP는 손으로 만든 지역(현재 메인 월드)을 계속 담는 **혼합 구조**가 가능하다. 권장 청크 그릇: **풀링된 청크 액터(메시 종류별 ISM(Instanced Static Mesh) 컴포넌트) + 풀링된 게임플레이 액터**를 기본으로, 디자이너가 만든 세트피스(POI)만 월드 애셋 스트리밍(실패 시 `LoadLevelInstance`)으로 얹는다. [추정: 설계 판단]
4. 런타임 청크에서는 HLOD(계층형 LOD), WP 미니맵, WP 정적 내비 청크, 데이터 레이어, 엔진 안정 ID가 **모두 작동하지 않거나 의미가 없다**. 월드 경계(`WORLD_MAX`)는 사실상 제약이 아니다(약 4,398만 km). 탑다운 시점은 HLOD·원거리 스트리밍 필요를 크게 줄여 무한 세계에 유리하다. [검증 + 추정]
5. 내비는 현재 Dynamic 설정(D-06) 그대로 쓰되 **내비게이션 인보커(Navigation Invoker) 필수**: Dynamic 생성은 내비 경계 볼륨 안 전체 타일을 만들려 하므로 인보커 없이는 무한 영역이 불가능하다. [검증 + 추정]

---

## 1. 질문 1 — 런타임 셀·런타임 해시

### 1-1. 셀은 에디터·쿠킹 시점에만 만들어진다
- `UWorldPartitionRuntimeHash::GenerateStreaming`, `SetupHLODActors`, `StoreStreamingContentToExternalStreamingObject`, `GetExternalStreamingObjectClass`는 모두 `#if WITH_EDITOR`(에디터 빌드 전용) 블록 안 — `E/Public/WorldPartition/WorldPartitionRuntimeHash.h:217-239, 243-252`. [검증]
- RuntimeHashSet의 기본 파티션인 LH 그리드(Loose Hierarchical Grid, 느슨한 계층 격자)의 `URuntimePartitionLHGrid::GenerateStreaming`도 `#if WITH_EDITOR` — `E/Private/WorldPartition/RuntimeHashSet/RuntimePartitionLHGrid.cpp:188-243`. 셀은 "에디터 시점에 액터가 있던 곳"에만 희소하게 생긴다. [검증]
- 런타임에 쓰이는 공개 질의 함수는 `ForEachStreamingCells/Query/Sources`와 `InjectExternalStreamingObject/RemoveExternalStreamingObject`뿐 — `WorldPartitionRuntimeHash.h:268-280`. [검증]
- `CreateRuntimeCell`·`CreateExternalStreamingObject`는 `WITH_EDITOR` 밖의 public 함수다 — `WorldPartitionRuntimeHash.h:290-291`, 구현 `E/Private/WorldPartition/WorldPartitionRuntimeHash.cpp:221, 291`. 런타임 호출처는 아래 1-2의 월드 애셋 스트리밍뿐이다(`WorldPartitionRuntimeHashSet.cpp:872`, `WorldPartitionRuntimeSpatialHash.cpp:2225`). [검증]

### 1-2. 런타임 셀 주입: 월드 애셋 스트리밍 (실험)
- 선언: `UWorldPartition::FRegisterWorldAssetStreamingParams`와 `RegisterWorldAssetStreaming / UnregisterWorldAssetStreaming / GetWorldAssetStreamingCells / SupportsWorldAssetStreaming` — `E/Public/WorldPartition/WorldPartition.h:765-809`. 주석 원문: "Experimental: World Asset Streaming can be used to inject streaming levels into the runtime grids dynamically, with one level of HLODs support"(`:766-767`). `WITH_EDITOR` 가드 없음 → 쿠킹 게임에서 호출 가능. [검증]
- 매개변수: `WorldAsset`(소프트 참조 템플릿 레벨) + `TargetGrid`(이름), `Guid`(필수, 유효해야 함), `Transform`, `Bounds`(필수), `Priority`, `CellInstanceSuffix`, 선택 HLOD 월드 애셋 — `WorldPartition.h:769-803`, 유효성 `IsValid()` `:790-793`. [검증]
- 구현(`E/Private/WorldPartition/RuntimeHashSet/WorldPartitionRuntimeHashSet.cpp:791-930`) [검증]:
  - `TargetGrid`를 `ResolveRuntimePartition`으로 찾는다 → **메인 월드에 이미 있는 그리드 이름**이어야 하고 로딩 범위는 그 그리드의 `LoadingRange`를 그대로 쓴다(`:806-811, 848-849`).
  - 등록 1회마다 외부 스트리밍 객체 1개 + `UWorldPartitionRuntimeLevelStreamingCell` 1개 생성(`:831, 871-872`). 셀 GUID = MD5(그리드 이름, 등록 Guid)(`:855-862`) → 등록 Guid를 청크 좌표에서 결정론적으로 만들면 셀 ID도 결정론적.
  - 셀 경계 = 넘겨준 `Bounds`, `bIs2D=false`, 계층 레벨 `MAX_int32`(`:885-892`) → 3D 경계로 스트리밍 소스 구와 교차 판정. 탑다운이라도 Z 범위를 넉넉히 줘야 한다[추정: 결과 해석].
  - 셀 데이터에 데이터 레이어를 설정하지 않는다(위 줄들 어디에도 없음) → 주입 셀은 공간 거리만으로 로드된다.
  - 끝에서 `InjectExternalStreamingObject` 호출(`:926`). 해제 시 이름을 폐기해 같은 타일을 GC 전에 재등록해도 충돌하지 않게 처리(`:934-958`) → **슬라이딩 창식 등록·해제를 전제로 만든 코드**다.
- 템플릿 반복 배치 가능: 셀마다 패키지 이름을 `<템플릿>_LevelInstance_<셀이름>`으로 유일화하고 `SetLevelTransform(InInstanceTransform * WP 인스턴스 변환)`으로 배치 — `E/Private/WorldPartition/WorldPartitionRuntimeLevelStreamingCell.cpp:124-160`. [검증]
- **비용 한계**: 등록된 월드 애셋마다 `FRuntimePartitionStreamingData`가 따로 생기고 공간 인덱스도 따로다(`WorldPartitionRuntimeHashSet.cpp:518`). 스트리밍 소스 질의는 소스 × 그리드 × **등록 타일 수**만큼 반복(`:719-724`), 주입·제거마다 그리드 맵 전체 재구성(`UpdateRuntimeDataGridMap` `:1439-1460`, 호출 `:519, 533`). → 비용이 등록 타일 수에 선형. 무한 세계 전체를 등록하면 안 되고 **플레이어 주변 고리 안만 등록**해야 한다. [검증: 코드 구조 / 추정: 실제 비용 수치는 미측정]

### 1-3. 런타임 스폰 액터는 공간 로딩 대상이 아니다
- `AActor::bIsSpatiallyLoaded`(`E/Classes/GameFramework/Actor.h:1330`)는 `#if WITH_EDITORONLY_DATA`(`:1262-1339`) 안, `RuntimeGrid`(`:818`)도 `WITH_EDITORONLY_DATA`(`:806-819`) 안, `Get/SetIsSpatiallyLoaded`(`:2647-2660`)는 `#if WITH_EDITOR`(`:2583~`) 안. 쿠킹 게임에는 이 개념 자체가 없다. [검증]
- 따라서 런타임 스폰 액터는 퍼시스턴트 레벨(또는 `FActorSpawnParameters::OverrideLevel` — `E/Classes/Engine/World.h:437`로 지정한 레벨)에 들어가 **파괴될 때까지 남는다**. 스트림 아웃은 우리 코드가 해야 한다. `OverrideLevel`로 주입 셀의 레벨에 넣으면 그 레벨이 내려갈 때 함께 사라질 것이나 저장되지는 않는다. [검증: 필드 존재 / 추정: 언로드 동작은 이번에 원문 미확인]

### 1-4. 격자 좌표·범위 한계
- LH 그리드 셀 좌표는 `int64 X,Y,Z`(`RuntimePartitionLHGrid.cpp:12, 93-95`), 셀 크기 계산도 `int64`(`:64, 76`). 고정된 월드 범위가 없고 희소하다. 2D 그리드의 셀 Z 범위는 `±HALF_WORLD_MAX`(`:233-234`). [검증]
- 좌표 변환에 `FMath::FloorToInt`(`:66-68`)를 써서 실효 범위는 ±2^31 셀일 수 있으나, 기본 셀 25,600 cm(`E/Public/WorldPartition/RuntimeHashSet/RuntimePartitionLHGrid.h:57`) 기준 약 5.5×10^13 cm로 `HALF_WORLD_MAX`(4.4×10^12 cm)보다 커서 실질 제약이 아니다. [추정: FloorToInt 반환형 미확인]
- 주입 셀은 경계를 직접 받으므로 메인 월드의 에디터 시점 액터 경계와 무관한 위치에 둘 수 있다(1-2). 구식 RuntimeSpatialHash는 생성 시점 `WorldBounds`(`E/Public/WorldPartition/WorldPartitionRuntimeSpatialHash.h:168`)로 격자를 잡는다. [검증: 필드 / 추정: 격자 범위 고정 여부]
- 프로젝트 메인 월드는 RuntimeHashSet + LH 그리드다: `Content/Level/LV_DarkFantasy_OpenWorld.umap` 이진 문자열에 `WorldPartitionRuntimeHashSet`, `RuntimePartitionLHGrid` 존재(이번 grep). [검증] → 월드 애셋 스트리밍 경로(`WorldPartitionRuntimeHashSet.cpp`)가 그대로 적용된다.

---

## 2. 질문 2 — 청크 그릇 후보 비교

| 그릇 | 비동기 적재 | WP 스트리밍 소스 연동 | 런타임 내용 가변성 | 주요 비용 | 결정적 한계 |
|---|---|---|---|---|---|
| ① 월드 애셋 스트리밍(주입 셀) | 예(레벨 스트리밍) | **예(자동 로드/언로드)** | 템플릿 고정, 안쪽 액터가 위치·시드로 변형 | 타일당 레벨 패키지 적재 + AddToWorld, 등록 수 선형 질의 | Experimental, 데이터 레이어·런타임 HLOD 없음, 반복 타일 ID 충돌 |
| ② `ULevelStreamingDynamic::LoadLevelInstance` | 예 | 아니오(코드가 직접 로드/언로드) | ①과 같음 | ①과 같음(질의 비용 없음) | WP 완료 질의에 안 잡힘, 반복 타일 ID 충돌 |
| ③ 액터 직접 스폰 + 풀링 | 애셋만 비동기(D-47), 스폰은 게임 스레드 | 아니오 | **완전 가변(생성기 결과 그대로)** | 스폰·컴포넌트 등록, 풀로 GC 부담 회피 | 공간 로딩 없음 → 자체 관리 |
| ④ 청크별 ISM/HISM 컴포넌트 | 애셋 비동기 + 인스턴스 일괄 추가 | 아니오 | 완전 가변, 인스턴스 추가·제거 | 인스턴스별 물리 바디(충돌 시) | 로직 없는 정적 물체만 |
| ⑤ Packed Level Actor(BP) 스폰 | 클래스·메시 비동기 후 스폰 | 아니오 | 프리팹 고정 | ISM 묶음이라 저렴 | 비 ISM 컴포넌트는 패킹 시 버려짐 |
| ⑥ `ALevelInstance` 런타임 스폰 | 예 | 아니오(자체 레벨 스트리밍) | 월드 애셋을 런타임에 못 바꿈 | ②와 같음 | 클래스마다 애셋 고정, 런타임 ID가 실행마다 다름 |
| (참고) PCG 런타임 생성 | 예(스케줄러) | 자체 생성 소스 | 그래프 결과 | 파티션 액터 풀 | 별도 갈래 조사 대상 |

### ① 월드 애셋 스트리밍
- 1-2 참조. 셀이 런타임 해시의 스트리밍 데이터 목록에 들어가므로 `UWorldPartitionSubsystem::IsStreamingCompleted`(선행 문서 3절) 질의에도 잡힐 것이다. [추정: 질의 경로가 같은 그리드 맵을 쓰는 것까지만 확인]
- 템플릿 레벨은 쿠킹되는 일반 월드 애셋이어야 한다(소프트 참조 → 쿠킹 보장은 `Tools/check_cook_coverage.py` 대상). [추정]
- HLOD는 "미리 만든 HLOD 월드 애셋" 한 단계만(`WorldPartition.h:781, 796`). [검증]

### ② `LoadLevelInstance`
- 선언: `FLoadLevelInstanceParams`(변환, 이름 덮어쓰기, 스트리밍 클래스, `bLoadAsTempPackage`, `bInitiallyVisible`) — `E/Classes/Engine/LevelStreamingDynamic.h:32-65, 89-96`. 네트워크에서는 서버·클라이언트가 같은 이름을 쓰도록 `OptionalLevelNameOverride`가 필요(`:83`). [검증]
- 구현 `E/Private/LevelStreaming.cpp:2698-2796`: 매번 새 `ULevelStreamingDynamic` 생성, `bShouldBlockOnLoad=false`(`:2783`), `World->AddStreamingLevel`(`:2794`). 실제 읽기는 `LoadPackageAsync`(`:1811`), 월드 추가도 비차단(`:1094`). → **완전 비동기**. [검증]
- WP 월드 안에서도 동작(레벨 인스턴스 Standalone 모드가 이 경로를 씀, 선행 문서 4절). 로드·언로드 시점은 우리 코드 책임이다. [검증: 선행 문서]
- 템플릿이 WP 레벨이면 하위 WP로 초기화되고(`LevelStreaming.cpp:928`), 각 WP의 스트리밍 정책은 서브시스템의 같은 스트리밍 소스를 자기 로컬 공간으로 변환해 쓴다(`E/Private/WorldPartition/WorldPartitionStreamingPolicy.cpp:118-128`, `:652-653`). 즉 하위 WP 셀은 플레이어 소스로 자동 스트리밍되지만 청크마다 WP 하나는 무겁다. [검증: 소스 공유 / 추정: 비용]
- 시간 분할 CVar(콘솔 변수): `s.AsyncLoadingTimeLimit`, `s.LevelStreamingActorsUpdateTimeLimit`, `s.LevelStreamingComponentsRegistrationGranularity` — `E/Private/CoreSettings.cpp:52, 73, 94`. ①②의 프레임 끊김 조절 수단. [검증]

### ③ 액터 직접 스폰 + 풀링
- 패키지 입출력이 없고 생성기 결과를 그대로 반영. 공간 로딩 개념이 없으므로(1-3) 청크 관리자가 반납·재사용한다. 애셋은 시작 전에 `FStreamableManager`로 비동기 요청(AGENTS.md 7절, D-47). [추정: 설계]
- 게임플레이 액터(자원 노드, 상자, 몬스터, 설치물)와 **샌드박스 변경 대상(B 수준)** 에 적합.

### ④ 청크별 ISM/HISM
- `AddInstances(TArray<FTransform>, bShouldReturnIndices, bWorldSpace, bUpdateNavigation)` — `E/Classes/Components/InstancedStaticMeshComponent.h:275`, 일괄 갱신 `BatchUpdateInstancesTransforms` `:388-397`. [검증]
- 인스턴스마다 `FBodyInstance`(`InstanceBodies` `:525`) → 충돌이 필요 없는 장식은 `bDisableCollision`(`:229`)로 끈다. [검증]
- HISM은 트리 재구성을 비동기로 할 수 있다(`BuildTreeAsync` — `E/Classes/Components/HierarchicalInstancedStaticMeshComponent.h:358`, `bAutoRebuildTreeOnInstanceChanges` `:198`). [검증]
- 나무·바위·잔해 같은 정적 장식, 그리고 로직 없는 설치 블록(B 수준, 인덱스 대응표 필요)에 적합.

### ⑤ Packed Level Actor
- 쿠킹 런타임에서 `APackedLevelActor::IsLoadingEnabled()`는 무조건 `false` — `E/Private/PackedLevelActor/PackedLevelActor.cpp:42-48`. 즉 런타임에는 레벨을 읽지 않는 "ISM 묶음 BP 액터"일 뿐이라 프리팹 장식 도장(stamp)으로 스폰해 쓸 수 있다. 월드 애셋도 쿠킹하지 않는다(`E/Public/PackedLevelActor/PackedLevelActor.h:105`). [검증]
- 패킹 시 비 ISM 컴포넌트 폐기(선행 문서 5절) → 로직 액터 불가.

### ⑥ 레벨 인스턴스 런타임 스폰
- 스폰은 되지만 런타임에 월드 애셋을 바꿀 공개 API가 없고, 런타임 `FLevelInstanceID`는 실행마다 다르다(선행 문서 4절). 템플릿마다 BP 클래스가 필요하고 세이브 키로 못 쓴다 → 무한 청크 그릇으로 부적합. [검증: 선행 문서]

### (참고) PCG 런타임 생성
- 파티션 액터 풀(`PCG/Public/RuntimeGen/PCGRuntimeGenScheduler.h:212-221, 284-288`), 기본 파티션 격자 25,600 cm(`PCG/Public/PCGWorldActor.h:59`), 격자 좌표 `FIntVector`(`PCGRuntimeGenScheduler.h:215`). 생성 범위가 컴포넌트 경계(볼륨)에 묶이는지는 이번에 확인하지 않았다. [검증: 필드 / 추정: 무한 적용성] — PCG 갈래에서 판정.

### 2-8. 손으로 만든 WP 지역 + 바깥 런타임 세계 혼합
- 가능하다. ①은 메인 WP의 기존 그리드 이름에 셀을 주입하는 방식이라 메인 월드와 같은 런타임 해시 안에서 공존한다(`WorldPartitionRuntimeHashSet.cpp:806-811`). ②③④⑤는 WP와 무관한 레벨·액터라 충돌 요소가 없다. [검증: ① / 추정: 나머지]
- 조건: 생성기가 손으로 만든 지역의 발자국(footprint) 마스크를 알고 그 안의 청크를 건너뛴다. 경계의 지형 높이 이음매는 지형 갈래 과제(랜드스케이프는 런타임 생성 불가 → 바깥 지형은 메시 기반 별도 그릇). [추정]
- 이음매 통로 결정(D-42, 연속 보행)과 충돌 없음: 생성 세계도 걸어서 이어지는 연속 공간이다. [추정]

### 2-9. 스트리밍 소스가 런타임 적재 레벨에 작동하는가
- ① 주입 셀: 예(셀 자체가 런타임 해시 소속). [검증]
- ② 일반(비 WP) 템플릿: 아니오 — 레벨 스트리밍 객체는 `SetShouldBeLoaded` 등으로 직접 제어. WP 템플릿: 하위 WP가 같은 소스를 공유(2절 ②). [검증]
- ③④⑤: 해당 없음(레벨이 아님). 청크 관리자가 플레이어(또는 기존 `IWorldPartitionStreamingSourceProvider` 구현 서브시스템, 선행 문서 7절)의 위치를 직접 읽는다. [추정]

---

## 3. 질문 3 — 런타임 무한 콘텐츠에서 깨지거나 없어지는 것

| 항목 | 런타임 청크에서 | 근거 | 대응 |
|---|---|---|---|
| HLOD | 없음. 생성(`SetupHLODActors`)은 에디터 전용, 주입 셀은 미리 만든 HLOD 월드 애셋 1단계만 | `WorldPartitionRuntimeHash.h:222` [검증], `WorldPartition.h:766-767, 781` [검증] | 탑다운 시야(5절, 반경 약 26 m)에서는 원거리 표현이 거의 불필요. 필요하면 청크 단위 저해상도 ISM 대리 메시 [추정] |
| WP 미니맵 | 고정 경계의 구운 텍스처 | `AWorldPartitionMiniMap::MiniMapWorldBounds`·`MiniMapTexture` — `E/Public/WorldPartition/WorldPartitionMiniMap.h:42, 50`, 빌더 `Editor/UnrealEd/Public/WorldPartition/WorldPartitionMiniMapBuilder.h` [검증] | 생성기 데이터(바이옴·지형)로 청크별 미니맵 타일을 런타임에 그림 [추정] |
| WP 정적 내비 청크 | 에디터 빌더 산출물이라 없음 | 선행 문서 B-1 [검증] | 프로젝트는 이미 Dynamic(`Config/DefaultEngine.ini:43, 46` [검증]). 아래 내비 항목 |
| 내비 생성 범위 | 타일은 포함 경계(내비 경계 볼륨)와 겹칠 때만 생성 | `R/NavigationSystem/Private/NavMesh/RecastNavMeshGenerator.cpp:1821-1832` [검증] | 인보커 전용 생성 `bGenerateNavigationOnlyAroundNavigationInvokers`(`R/NavigationSystem/Public/NavigationSystem.h:391`) + 매우 큰 경계 볼륨(또는 플레이어를 따라 이동) [검증: 옵션 / 추정: 이동 볼륨] |
| 내비 타일 상한 | 프로젝트 `TileNumberHardLimit=1048576`, `TileSizeUU=1000` → 동시 약 104.9 km²(10.24 km 사방) | `Config/DefaultEngine.ini:12, 24` [검증], 속성 `R/NavigationSystem/Public/NavMesh/RecastNavMesh.h:773, 691` [검증] | 인보커가 바깥 타일을 제거하므로 무한 이동에 충분 [추정] |
| 내비 좌표 정밀도 | Detour 실수형이 double | `R/Navmesh/Public/Detour/DetourLargeWorldCoordinates.h:7-15` [검증] | 문제 없음 |
| `WORLD_MAX` | LWC(Large World Coordinates, 대형 월드 좌표) 기본: `WORLD_MAX = 8,796,093,022,208 cm`(원점에서 약 4,398만 km) | `E/Public/EngineDefines.h:41, 45-54` [검증] | 실질 제약 아님 |
| `bEnableWorldBoundsChecks` | 액터 경계가 `±HALF_WORLD_MAX`를 넘을 때만 `OutsideWorldBounds()` | `E/Private/Actor.cpp:2336-2341`, 설정 `E/Classes/GameFramework/WorldSettings.h:406` [검증] | 실질 무관 |
| KillZ | Z만 검사 | `Actor.cpp:2326`, `WorldSettings.h:599` [검증] | 생성 지형 높이를 KillZ 위로 유지 |
| 원점 이동(rebasing) | World Composition 전용 옵션 → WP에서는 못 씀 | `WorldSettings.h:440-442`(EditCondition=`bEnableWorldComposition`) [검증] | 대신 실용 상한 설정(5절) |
| 단정밀도(float) 하위 시스템 | 2^21 cm(약 21 km)에서 float 간격 0.25 cm, 2^24 cm(약 168 km)에서 2 cm | 계산 [추정] | 렌더는 LWC 타일 기준(`EngineDefines.h:48` 주석), 물리·오디오·자체 코드의 float 사용은 미확인 [추정] |
| 데이터 레이어 | 쿠킹 게임에서 액터의 데이터 레이어는 **소속 런타임 셀에서만** 나온다. 셀 없는 스폰 액터는 빈 목록, 주입 셀은 데이터 레이어 미설정 | `Actor.cpp:7168-7197`, `WorldPartitionRuntimeHashSet.cpp:871-893` [검증] | 생성 세계의 켜고 끄기는 청크 관리자 상태로 처리 |
| 액터 안정 ID(`FActorInstanceGuid`) | 런타임 스폰 액터: 없음(선행 문서 A-3). 반복 템플릿 타일(①②): 레벨 인스턴스 GUID를 등록하는 곳이 `LevelInstanceActorImpl.cpp:93` 하나뿐이라, 대체 규칙 `ActorInstanceGuid = ActorGuid`로 떨어져 **같은 템플릿을 깐 모든 타일에서 ID가 겹친다** | `E/Private/WorldPartition/ActorInstanceGuids.cpp:132-160`, `E/Private/LevelInstance/LevelInstanceActorImpl.cpp:93` [검증] | 세이브 키 = (청크 좌표 int64 쌍, 청크 안 배치 번호 또는 템플릿 ActorGuid)를 생성기가 결정론적으로 부여. 공개 `FActorInstanceGuid::SetLevelInstanceGuid`(`E/Public/WorldPartition/ActorInstanceGuids.h:22`)로 직접 등록하는 길은 호출 시점 문제가 있어 미검증 [추정] |
| 세이브 | 엔진은 런타임 생성물의 지속성을 제공하지 않음 | 위 항목들 [검증] | 청크별 델타(철거·설치·변형 목록) 파일. 미방문 청크는 시드로 재생성, 방문 청크는 시드 + 델타 [추정] |

---

## 4. 변형 세 수준별 그릇 판정 [추정: 1~3절 근거를 종합한 설계 판단]

| 수준 | 판정 | 그릇 |
|---|---|---|
| (A) 무한 생성 + 변경 없음 | 가능 | 결정론 생성기(`FRandomStream`(시드 난수) ← 청크 좌표 해시, 전역 난수 금지 규약과 일치) → ④ 장식 ISM + ③ 풀링 액터. 세트피스는 ①(실험 기능이라 기능 플래그, 대체 경로 ②) |
| (B) 설치·철거·건물 | 가능 | A + 청크별 델타 저장. 로직 있는 설치물은 ③, 단순 블록·벽은 ④(인스턴스 번호 대응표). 안정 키는 3절 방식. 내비는 Dynamic이라 설치물 반영 가능 |
| (C) 지형 변형(복셀 등) | 엔진 WP·랜드스케이프로는 불가, 별도 그릇 필요 | 메시 기반 지형 청크(지형 갈래에서 판정). WP는 관여하지 않음 |

권장 골격:
1. `UTDChunkStreamingSubsystem`(가칭, C++): 플레이어 위치 → 청크 좌표 고리 계산 → 상태 기계(요청 → 애셋 비동기 적재 → 시간 분할 구성 → 활성 → 반납). 등록 고리 반경 > 시야·시뮬 반경 + 이동 여유.
2. 청크 크기는 내비 타일(1,000 cm)의 정수배로 정렬(예 8,000 cm) — 선행 문서의 "청크 그리드를 셀 크기에 맞춘다" 원칙과 같은 이유.
3. 준비 판정 = 청크 상태 활성 + 목적지 `ProjectPointToNavigation` 성공(선행 문서 B-3과 같은 방식).
4. 손으로 만든 메인 WP 지역은 그대로 두고, 생성기는 그 발자국 바깥만 채운다.

## 5. 탑다운 시점이 주는 여유 [추정: 과제가 준 카메라 값으로 계산]

- 카메라 높이 = 1100 × sin50° ≈ 843 cm, 뒤쪽 거리 ≈ 707 cm. 수평 시야각 90°, 16:9 기준 수직 시야각 ≈ 58.7°.
- 보이는 지면: 캐릭터 기준 뒤 약 5.5 m ~ 앞 약 15.3 m, 폭은 가까운 쪽 약 15 m · 먼 쪽 약 42 m. 캐릭터에서 가장 먼 보이는 점 ≈ 26 m.
- 600 cm/s로 80 m 청크를 건너는 데 약 13 초 → 고리 한 칸 앞서 요청하면 적재 예산이 넉넉하다.
- 원점에서 21 km(float 간격 0.25 cm)까지 직진 약 58 분, 168 km(2 cm)까지 약 7.8 시간. 원점 이동을 못 쓰므로, 필요하면 "설계상 무한 + 실용 상한(예 반경 수백 km)"으로 정한다.

## 6. 미검증·위험

- 월드 애셋 스트리밍은 **Experimental**이고 엔진 내 사용 예가 없다 → 엔진 판올림 때 깨질 수 있다. 실제 프레임 비용·등록 수 한계는 측정 전.
- `IsStreamingCompleted`가 주입 셀을 포함하는지 질의 경로 끝까지는 미확인.
- `OverrideLevel` 스폰 액터의 언로드 동작, PCG 런타임 생성의 무한 적용성, 물리 등 float 하위 시스템 정밀도는 원문 미확인.
- `SetLevelInstanceGuid`를 게임 코드에서 호출해 반복 타일 ID를 고유화하는 경로는 호출 시점(어노테이션이 `PostRegisterAllComponents`에서 해제됨, 선행 문서 A-2)과 맞물려 미검증.
- 지형(랜드스케이프 대체)과 이음매 높이 일치는 이 갈래 범위 밖.
