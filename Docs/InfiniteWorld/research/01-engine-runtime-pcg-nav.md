# 01 엔진 갈래: 런타임 PCG(절차적 콘텐츠 생성)와 내비게이션이 "플레이어를 따라 끝없이 생성되는 세계"를 감당하는가 (UE 5.8 소스 확인, 2026-10-23)

경로 약칭: `<PCG>` = `C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/PCG/Source/PCG`, `<NAV>` = `.../Engine/Source/Runtime/NavigationSystem`, `<ENG>` = `.../Engine/Source/Runtime/Engine`.
표기: [검증] = 원문을 직접 읽음, [추정] = 원문 미확인 또는 계산·추론. 엔진·에디터·빌드는 실행하지 않았다(정적 판독만).

## 0. 결론 요약

1. **런타임 PCG는 "끝없는 세계"를 구조적으로 감당한다. 단, 무한 경계가 아니라 "아주 큰 고정 볼륨 + 플레이어 주변 셀만 생성"으로 감당한다.** 스케줄러는 생성 소스 주변의 격자 셀을 열거한 뒤 원본 컴포넌트 경계와 교차한 셀만 생성한다. 볼륨 밖은 생성되지 않는다 [검증 `<PCG>/Private/RuntimeGen/PCGRuntimeGenScheduler.cpp:742-878`]. 그러나 볼륨 경계는 배우 경계(액터 바운드) 하나이므로 ±1,000km 크기로 둬도 셀 열거 비용은 반경에만 비례한다. 실용적 무한(가장자리까지 걸어서 수십 시간)은 충분하다.
2. **월드 파티션 없이도 동작한다.** 월드 파티션 서브시스템이 없으면 스트리밍 완료 질의를 건너뛸 뿐이다 [검증 `PCGRuntimeGenScheduler.cpp:568-575`]. 단 지속 레벨(Persistent Level)에 `APCGWorldActor`가 저장돼 있어야 스케줄러가 돈다 [검증 `<PCG>/Private/Subsystems/PCGSubsystem.cpp:254-264, 425-428, 432-442`].
3. **탑다운이라 생성 반경 요구가 매우 작다.** 이 프로젝트 카메라로 보이는 지면은 폰 기준 뒤 5.5m에서 앞 15.3m까지, 가장 먼 모서리가 약 25.9m다 [추정, 9절 계산]. 기본 반경(격자 크기 × 2)으로 64m 격자를 쓰면 128m를 생성한다. 1인칭 오픈월드보다 생성량이 한 자릿수 이상 적다.
4. **내비게이션은 "인보커(Navigation Invoker) + 거대 NavMeshBoundsVolume + 고정 타일 풀"로 감당한다.** 경계 볼륨은 여전히 필수다. 전 세계 내비 플래그 `bWholeWorldNavigable`은 엔진이 편집 노출을 막아 두고 "현재 고장"이라고 주석을 달았다 [검증 `<NAV>/Public/NavigationSystem.h:365-371`]. 타일 더럽힘(dirty)은 포함 경계와 활성 타일 집합 밖이면 버려진다 [검증 `<NAV>/Private/NavMesh/RecastNavMeshGenerator.cpp:6790, 6812, 6827`]. 지금 프로젝트 설정 `bFixedTilePoolSize=False`로 경계를 거대하게 잡으면 타일 수가 하드 한계 2^20개로 잘리고 오류가 난다 [검증 `RecastNavMeshGenerator.cpp:5756-5772`, `Config/DefaultEngine.ini:10-12,24`]. 따라서 고정 풀로 바꿔야 한다.
5. **D-47(비동기 로드) 충돌 지점은 두 군데다.** Static Mesh Spawner는 기본적으로 비동기 로드를 한다(`bSynchronousLoad=false`) [검증 `<PCG>/Public/Elements/PCGStaticMeshSpawner.h:116`, `.cpp:579`]. 반면 Spawn Actor 노드는 클래스를 `LoadSynchronous()`로 불러온다 [검증 `<PCG>/Private/Elements/PCGSpawnActor.cpp:122,138`]. 해결책은 생성 전에 `FStreamableManager`로 미리 불러와 핸들을 쥐고 있는 것이다.
6. **샌드박스 관점의 핵심 제약:** PCG 출력은 정리(cleanup) 반경에서 지워지고 다시 들어오면 재생성되는 일시적 결과다(풀링되는 Transient 파티션 액터) [검증 `PCGRuntimeGenScheduler.cpp:2341-2370`]. 플레이어 변경(채집·철거·설치)은 PCG 밖에 셀 단위 차분(delta)으로 저장하고, 재생성 때 제외 입력이나 후처리로 다시 적용해야 한다. 수준 (A)는 엔진 기본기로 가능하다. (B)는 차분 저장소와 내비 동적 장애물로 가능하다. (C)는 PCG·랜드스케이프의 범위 밖이라 별도 복셀 메시 계층이 필요하고, 내비 타일 지오메트리 재빌드 비용이 지배적이다.

## 1. PCG 런타임 생성 구조

### 1.1 트리거와 구성 요소
- `EPCGComponentGenerationTrigger::GenerateAtRuntime` [검증, 프로젝트 기존 조사 `Docs/WorldDungeonPCG/research/pcg-api-and-nodes.md:7`이 `<PCG>/Public/PCGComponent.h:76-82`를 인용].
- 스케줄러 `FPCGRuntimeGenScheduler`는 `UPCGSubsystem::Tick` 끝에서 `PCGWorldActor`가 있을 때만 호출된다 [검증 `PCGSubsystem.cpp:421-428`]. 스케줄러 Tick 첫 줄은 `check(InPCGWorldActor && GenSourceManager)`이다 [검증 `PCGRuntimeGenScheduler.cpp:332-334`].
- `APCGWorldActor`는 런타임·PIE(에디터 플레이)에서 새로 만들지 않는다. 에디터에서 런타임이 아닐 때만 `CreatePCGWorldActor`를 호출한다 [검증 `PCGSubsystem.cpp:432-442`]. 게임에서는 지속 레벨에서 찾기만 한다 [검증 `PCGSubsystem.cpp:254-264`]. **결론: 무한 월드 맵에는 에디터에서 만든 PCG 월드 액터를 저장해 둬야 한다.** 이 액터의 `PartitionGridSize`는 HiGen(계층 생성)을 끈 그래프의 기본 격자다 [검증 `<PCG>/Private/Helpers/PCGHelpers.cpp:547-564`].
- 생성 소스는 `<PCG>/Public/RuntimeGen/GenSources/`에 Player, Component, EditorCamera, WPStreamingSource 네 가지가 있다 [검증, 파일 목록].
  - `UPCGGenSourcePlayer`는 위치로 `PlayerController->GetPlayerViewPoint`(카메라 위치)를 쓴다 [검증 `<PCG>/Private/RuntimeGen/GenSources/PCGGenSourcePlayer.cpp:39-43`]. 이 프로젝트 카메라는 폰보다 수평 7.07m 뒤, 8.43m 위에 있다 [추정, 스프링암 1100cm·피치 -50°로 계산]. 그래서 생성 원이 화면 중심보다 뒤로 치우친다.
  - `UPCGGenSourceComponent`를 폰에 붙이거나 반경에 여유를 두면 된다. 이 컴포넌트가 소유자 위치를 쓴다는 것은 [추정]이다(`PCGGenSourceComponent.cpp:125`의 본문은 읽지 않음).

### 1.2 셀 열거 방식: 볼륨이 영역을 덮어야 하는가 (핵심)
- 분할(partitioned) 원본마다 다음 순서로 판정한다 [검증 `PCGRuntimeGenScheduler.cpp:742-878`].
  1. 원본 경계 `OriginalExecutionState.GetBounds()`와 생성 소스 사이 거리가 최대 반경을 넘으면 건너뛴다.
  2. 격자 크기마다 생성 소스 셀 좌표 ± `ceil(반경/격자)`의 정육면체를 순회한다. 2D 격자면 Z는 1층이다.
  3. 각 셀 상자를 원본 경계와 `Overlap`한다. 부피가 0이면 버린다.
  4. 교차 상자가 반경 안이고 정책(Policy)의 `ShouldGenerate`를 통과하면 생성 후보에 넣는다.
  - 2D 격자의 셀 Z 반높이는 `HALF_WORLD_MAX1`로 사실상 무한이다 [검증 `:805-809`].
- 원본 경계는 소유 액터의 바운드다. `PCGHelpers::GetGridBounds` → `GetActorBounds`가 계산한다 [검증 `PCGHelpers.cpp:120-153`]. **따라서 볼륨이 모든 영역을 덮어야 한다. 덮지 않은 곳은 생성되지 않는다.**
- **권장: 거대 고정 볼륨.** 볼륨을 ±1e8cm(±1,000km)로 스케일한다. 셀 순회는 생성 소스 주변 `(2·ceil(R/G)+1)^2`개뿐이라 볼륨 크기와 무관하다 [검증 같은 루프]. 600cm/s 보행 속도라면 가장자리까지 약 46시간 걸린다 [추정, 계산].
- **함정 1, Unbounded 층:** 그래프에 Unbounded 격자 노드(분할 없이 원본 전체에서 한 번 실행)가 있으면 그 노드가 거대 볼륨 전체를 대상으로 돈다. 원본 키 `(Unbounded, 0, 원본)` 하나로 생성된다 [검증 `PCGRuntimeGenScheduler.cpp:764-786`, `PCGSubsystem.cpp:756-768`]. 거대 볼륨에서는 Unbounded 노드를 두지 않는다. HiGen 기본 격자도 유한 격자로 둔다(`UPCGGraph::HiGenGridSize`). 표면 샘플러를 Unbounded로 쓰면 월드 데이터 쪽에서 "경계 없이 생성 불가" 오류가 난다 [검증 `<PCG>/Private/Data/PCGWorldData.cpp:314,504`].
- **볼륨을 플레이어와 함께 옮기는 안은 비권장이다.** 지역 소스 경계가 원본 경계와 겹치지 않으면 정리 대상이 된다 [검증 `PCGRuntimeGenScheduler.cpp:2032-2035`]. 원본 변환이 바뀌면 변경 추적이 새로고침(refresh)을 부를 가능성이 크다 [추정, 미확인]. 거대 고정 볼륨이 더 단순하고 비용도 0이다.
- **함정 2, 입력 데이터:** `Get Landscape Data` 계열은 `ALandscape`가 있어야 한다. 랜드스케이프는 크기가 유한한 액터이므로 끝없는 지형은 다른 갈래의 런타임 지형 메시가 필요하다. PCG는 그 메시나 콜리전을 `World Raycast`·`Get Primitive Data`로 샘플링해야 한다 [추정].

### 1.3 격자 크기와 반경
- HiGen 격자는 400cm~204,800cm 10단계가 노출돼 있고, 숨은 단계는 4,194,304cm까지다. `Unbounded = 1u<<31`이다 [검증 `<PCG>/Public/PCGCommon.h:519-553`].
- 기본 생성 반경은 격자 × 2.0이고, 정리 반경은 생성 반경 × 1.1이다. 값은 품질 수준별(`FPerQualityLevelFloat`)로 둔다 [검증 `PCGCommon.h:620-660`]. 컴포넌트에서 `bOverrideGenerationRadii`로 덮어쓴다(기존 조사 `pcg-api-and-nodes.md:7`).
- 전역 배수는 `pcg.RuntimeGeneration.GlobalRadiusMultiplier`(1.0)이다 [검증 `PCGRuntimeGenScheduler.cpp:72-75`]. 그래프 쪽 반경 상한은 `UnboundedGridSize × 2` 이내로 잘린다 [검증 `<PCG>/Private/PCGGraph.cpp:681`].
- 탑다운 적용 예 [추정]:
  - 지형 장식은 Grid64(64m), 반경 128m. 600cm/s면 셀 하나를 10.7초에 지난다.
  - 작은 소품은 Grid16(16m), 반경 32m. 가시 거리 26m보다 약간 크다.
  - 큰 구조물·조우 앵커는 Grid256(256m), 반경 512m.

### 1.4 콘솔 변수 (`pcg.RuntimeGeneration.*` 등, 기본값)

| 변수 | 기본값 | 의미 | 근거 [검증] |
|---|---|---|---|
| `pcg.RuntimeGeneration.Enable` | true | 시스템 켜기 | `PCGRuntimeGenScheduler.cpp:57-60` |
| `.EnableChangeDetection` | true | 소스·상태가 안 바뀌면 스캔 생략 | `:62-65` |
| `.NumGeneratingComponents` | 16 | 동시에 생성 중인 런타임 컴포넌트 상한 | `:67-70`, 적용 `:1208-1225` |
| `.GlobalRadiusMultiplier` | 1.0 | 반경 배수 | `:72-75` |
| `.EnablePooling` / `.BasePoolSize` | true / 100 | 파티션 액터 풀 | `:82-90` |
| `.FlushActorPool` (명령) | — | 풀 비우고 재생성 | `:92-101` |
| `.EnableWorldStreamingQueries` | true | 월드 파티션 셀 활성화 뒤 생성 | `:115-118` |
| `.FramesBeforeFirstGenerate` | 0 | 첫 생성 지연 틱 | `:120-123` |
| `.TimeBetweenRuntimeGenSchedulerTicks` | 0.0 | 스케줄러 틱 간격 | `:136-139` |
| `.ChangeDetection.MaxPositionDeltaGridProportion` | 0.1 | 최소 격자의 10% 이동마다 재스캔 | `PCGRuntimeGenChangeDetection.cpp:9-12` |
| `.ChangeDetection.MaxPositionDeltaUnbounded` | 100cm | Unbounded 층의 재스캔 이동량 | `:14-17` |
| `.Server.Mode` | 2 | 서버에서 모든 소스 | `PCGGenSourceManager.cpp:36-39` |
| `.EnableActorComponentlessGeneration` | false | 파티션 액터·지역 컴포넌트 없이 실행 | `<PCG>/Private/PCGGraph.cpp:71-74` |
| `.DisableDynamicTracking` | — | 동적 추적 끄기 | `<PCG>/Private/ChangeTracking/PCGChangeTracker.cpp:15` |
| `pcg.FrameTime` | 5.0ms | 프레임당 PCG 실행 예산 | `<PCG>/Private/Graph/PCGGraphExecutor.cpp:82-85` |
| `pcg.Runtime.MaxExecutingThreads` | 8 | 실행 스레드 하드 상한 | `:66-69` |
| `pcg.Runtime.MaxPercentageOfThreadsToUse` / `MaxPercentageOfExecutingThreads` | 0.9 / 0.3 | 스레드 비율 | `:56-64` |
| `pcg.GraphMultithreading` | true | 그래프 병렬 디스패치 | `:87-90` |

### 1.5 프레임 예산과 비용
- 실행 예산은 `pcg.FrameTime` 5ms다 [검증]. 정리(cleanup)도 같은 종료 시각을 넘으면 중단하고 다음 프레임에 이어서 한다 [검증 `PCGRuntimeGenScheduler.cpp:1159-1179`].
- 프로젝트 조사 기록에 "런타임 HiGen + GPU 스폰도 PS5에서 `PrepareForExecute`·CRC(순환 중복 검사) 비용으로 예산 초과" 사례와 `pcg.cache.runtime.enabled 0` 권고가 있다 [추정, 커뮤니티 출처, `Docs/WorldDungeonPCG/research/pcg-bake-data-community.md:40`]. 무한 세계에서는 매 셀이 새 입력이므로 캐시 적중이 거의 없다. 캐시를 끄는 쪽이 유리할 가능성이 크다 [추정].
- 탑다운 생성량 추정 [추정]: Grid64·반경 128m면 활성 셀은 약 π·2²·(셀 64m) ≈ 13~25개다. 걷는 동안 새로 드는 셀은 10.7초에 1열(약 5개)이므로 초당 0.5셀 수준이다. 16개 동시 생성 상한과 5ms 예산 안에 넉넉히 든다.

### 1.6 생성 결과의 정리와 풀링
- 파티션 액터 풀: 처음에 `BasePoolSize`(100)개를 만들고, 비면 현재 크기만큼 늘린다(두 배) [검증 `PCGRuntimeGenScheduler.cpp:2302-2315, 2341-2370`]. 풀 액터는 `RF_Transient`이고 지속 레벨에 스폰된다 [검증 `:2353-2357`]. 꺼낸 액터는 셀 중심으로 `Teleport`한다 [검증 `:2323-2327`]. 풀 축소 경로는 변수 변경 시 `ResetPartitionActorPoolToSize`뿐이다 [검증 `:1540-1566, 2373-2382`]. 정점(peak) 뒤 자동 축소가 없다는 것은 [추정]이다.
- 셀 정리는 "모든 생성 소스가 정리에 동의할 때"만 일어난다 [검증 `:1101-1106, 1136-1141`]. 정리 반경은 생성 반경 × 1.1이므로 경계에서 생성·정리를 반복하는 진동이 억제된다.
- 액터·컴포넌트 없는(actor-componentless) 생성은 기본 꺼짐이다. 켜진 컴포넌트에 남은 생성 결과는 정리된다 [검증 `:1093-1098`, `PCGGraph.cpp:71-74`]. 5.8의 컴포넌트 없는 GPU 산포는 콜리전과 내비 영향이 없다는 커뮤니티 정리가 있다 [추정, `pcg-bake-data-community.md:22`]. 시각 전용 장식에만 쓴다.

### 1.7 결정론
- 최종 시드 = 컴포넌트 `Seed` × 노드 `Seed` × 포인트 위치 해시다(기존 조사 `pcg-api-and-nodes.md:39-42`, `<PCG>/Public/Helpers/PCGHelpers.h:40-45`).
- `ComputeSeedFromPosition`은 `static_cast<int>(X/Y/Z)`를 쓴다 [검증 `PCGHelpers.cpp:50-53`]. **좌표 절댓값이 2^31cm ≈ 21,474km를 넘으면 정수 변환이 넘쳐 시드가 깨진다.** 이것이 PCG 결정론의 실질 수치 한계이고, ±1,000km 볼륨은 그 안에 든다.
- 셀은 고정 세계 격자에 정렬된다. 셀 중심 = `(Coord + 0.5) × GridSize` [검증 `PCGRuntimeGenScheduler.cpp:826-827, 2323`]. 그래서 같은 셀을 언제 다시 생성해도 같은 입력·같은 시드가 된다. 다만 입력 지오메트리(지형·플레이어 건물)가 바뀌면 결과도 바뀐다. 샌드박스 변경이 PCG 입력 쿼리(레이캐스트 등)에 잡히는 순서와 시점도 결정론에 영향을 준다 [추정].
- GPU 실행은 노드 단위 `bExecuteOnGPU`(기본 false)로 고른다 [검증 `<PCG>/Public/PCGSettings.h:648-685`]. GPU 커널의 원자 연산 순서 탓에 출력 순서가 프레임마다 다를 수 있다 [추정, 소스에서 결정론 보장 문구를 찾지 못함]. 게임플레이에 영향을 주는 배치(충돌·조우 앵커)는 CPU 경로에 둔다. 전투 시뮬레이터 규칙(전역 난수 금지)과 맞추려면 조우 판정은 PCG 밖 C++에서 `FRandomStream(hash(월드시드, 셀좌표))`로 만든다.

### 1.8 출력 형태와 동기 로드(D-47)
- **ISM(인스턴스드 스태틱 메시) 출력:** Static Mesh Spawner는 입력을 모두 처리한 뒤 메시·머티리얼 경로를 모아 비동기 로드를 요청한다(`!Settings->bSynchronousLoad`일 때) [검증 `PCGStaticMeshSpawner.cpp:579-600`, 기본값 `PCGStaticMeshSpawner.h:116`]. 이후 `LoadSynchronous()`는 "이미 비동기로 불러왔으므로 공짜"라는 주석이 붙은 보조 경로다 [검증 `:749-750`]. D-47과 충돌하지 않는다. 단 `bSynchronousLoad=true`로 바꾸지 않는다.
- **액터 출력:** Spawn Actor는 클래스를 `TSoftClassPtr::LoadSynchronous()`로 즉시 불러오고, 실패하면 `TryLoad`로 재시도한다 [검증 `PCGSpawnActor.cpp:122-140`]. 비동기 로드 컨텍스트는 쓰지 않는다 [검증, 같은 파일에 `IPCGAsyncLoadingContext`·`RequestAsyncLoad` 없음]. **D-47 충돌.** 해결책: 월드 시작 전에 C++가 스폰 후보 클래스를 `FStreamableManager::RequestAsyncLoad`로 불러와 핸들을 유지한다. 이미 로드된 클래스면 `LoadSynchronous`가 디스크를 건드리지 않는다 [추정, 일반 동작]. 또는 액터 스폰을 PCG 밖 C++ 스포너로 옮기고 PCG는 점(포인트)만 출력한다.
- `Tools/check_sync_loads.py`는 프로젝트 코드만 검사하므로 엔진 노드 내부의 동기 로드는 잡지 못한다 [추정, 도구 범위 미확인].

## 2. 끝없는 세계의 내비게이션

### 2.1 NavMeshBoundsVolume은 여전히 필요하다
- `bWholeWorldNavigable`은 UPROPERTY 지정이 주석 처리돼 있다. 엔진 주석은 "현재 고장, 일반 경우에 효율적으로 만들 수 있을지 모름"이다 [검증 `<NAV>/Public/NavigationSystem.h:365-371`]. 켜도 `GetWorldBounds`가 모든 액터를 순회한다("super slow" 주석) [검증 `<NAV>/Private/NavigationSystem.cpp:2643-2660`].
- 타일 더럽힘 단계에서 포함 경계(InclusionBounds)와 겹치지 않는 영역·타일, 활성 집합 밖 타일은 버려진다 [검증 `RecastNavMeshGenerator.cpp:6790, 6812, 6827`]. **인보커만 있고 경계 볼륨이 없으면 내비가 생기지 않는다.**
- **거대 경계는 가능하다.** 타일 좌표는 int32 범위만 맞으면 된다 [검증 `RecastNavMeshGenerator.cpp:5115-5131`, `RecastNavMesh.cpp:4680-4688`]. 타일 1000cm면 2.1e12cm까지 된다. Detour 좌표형 `dtReal`은 double이다(대규모 월드 좌표 켜짐) [검증 `.../Runtime/Navmesh/Public/Detour/DetourLargeWorldCoordinates.h:7-15`].
- 내비 옥트리는 경계 중심과 반지름으로 만든다 [검증 `NavigationSystem.cpp:1197-1211`]. 최대 깊이는 12다 [검증 `<NAV>/Public/NavigationOctree.h:116`]. ±1,000km 경계면 잎 노드가 약 488m가 되어 쓸 만하다 [추정, 계산 2e8/4096]. 인보커 모드에서는 지오메트리를 지연 내보내기(lazy export)한다 [검증 `NavigationSystem.h:1175`].

### 2.2 인보커
- `bGenerateNavigationOnlyAroundNavigationInvokers`(Config, 프로젝트 설정)는 엔진 주석상 "의식적으로 결정할 것, 옥트리 동작이 바뀐다"고 한다 [검증 `NavigationSystem.h:383-391`]. 관련 설정은 `ActiveTilesUpdateInterval`(최소 0.1초)과 `InvokersMaximumDistanceFromSeed`(-1이면 끔)다 [검증 `:393-400`].
- `UNavigationInvokerComponent` 기본값은 생성 3000cm, 제거 5000cm다 [검증 `<NAV>/Private/NavigationInvokerComponent.cpp:11-12`]. 우선순위는 실험 기능이다 [검증 `<NAV>/Public/NavigationInvokerComponent.h:21-36`].
- 활성 타일 갱신은 인보커마다 `RadiusMax` 사각을 순회해 타일 중심 거리로 판정한다. 결과를 추가(최소 반경) 집합과 유지(최대 반경) 집합으로 나눈다 [검증 `<NAV>/Private/NavMesh/RecastNavMesh.cpp:4603-4720`]. 비용은 인보커 수 × (반경/타일)²에 비례한다.
- 재추가 타일은 `bRebuildGeometry=true`로 지오메트리부터 다시 모은다 [검증 `RecastNavMeshGenerator.cpp:6106-6145`]. 인보커에 가까운 타일은 우선순위가 올라간다(`InvokerTilePriorityBumpDistanceThresholdInTileUnits`=1) [검증 `RecastNavMesh.h:899-905`].
- 탑다운 적용 [추정]: 플레이어 인보커 하나를 생성 3000cm·제거 5000cm로 둔다. 가시 거리 26m, 조우 스폰 고리 30~45m, 추격 여유가 들어간다. 몬스터를 인보커로 만들면 타일 수가 겹쳐 늘어나므로, 플레이어에서 멀리 떨어지는 몬스터만 작은 반경 인보커를 붙인다.

### 2.3 타일 풀 한계
- `bFixedTilePoolSize`의 엔진 주석은 "스트리밍 지원에 켜야 함"이다 [검증 `RecastNavMesh.h:680-687`]. 크기 조정형(resizable)이면 최대 타일 수 = `CalculateMaxTilesCount(InclusionBounds, TileSize, AverageLayersPerTile)`이다. 고정형이면 `TilePoolSize`다 [검증 `RecastNavMeshGenerator.cpp:5756-5766`]. 마스크 상한 `1 << log2(TileNumberHardLimit)`을 넘으면 "Navmesh bounds are too large!" 오류를 내고 잘린다 [검증 `:5768-5773`].
- `TileNumberHardLimit` 기본값은 2^20이다. 엔진 주석에 따르면 빈 타일 하나가 176바이트이고 미리 할당된다 [검증 `RecastNavMesh.h:767-773`, `RecastNavMesh.cpp:551`]. 거대 경계 + 크기 조정형이면 2^20 × 176B ≈ 185MB를 미리 잡고 오류도 낸다 [추정, 계산].
- **프로젝트 현재값:** `bFixedTilePoolSize=False`, `TilePoolSize=1024`, `TileSizeUU=1000`, `TileNumberHardLimit=1048576`, `RuntimeGeneration=Dynamic` [검증 `Config/DefaultEngine.ini:10-12, 24, 43-46`]. 무한 세계로 가려면 `bFixedTilePoolSize=True`로 바꿔야 한다.
- 필요 풀 크기 [추정]: 인보커 하나의 제거 반경 5000cm, 타일 1000cm면 약 π·(5.0+0.71)² ≈ 102칸이다. 층(layer) 수를 곱한다. 동굴·다리가 있으면 2~3배다. 플레이어 하나 + 보조 인보커 몇 개면 `TilePoolSize` 1024~2048이 출발점이다. 풀이 차면 "Failed to add tile … tile limit reached" 로그가 난다 [검증 `<NAV>/Private/NavMesh/PImplRecastNavMesh.cpp:741-744`, `RecastNavMeshGenerator.cpp:6366-6369`].
- `SupportsStreaming() = RuntimeGeneration != Dynamic || bIsWorldPartitioned`이다(기존 조사 `actor-id-and-navmesh.md`, `RecastNavMesh.cpp:4385-4390`). 무한 세계는 미리 구운 청크가 없으므로 Dynamic + 인보커 단독 구성이 맞다 [추정].

### 2.4 동시 작업 수와 재생성 비용
- `MaxSimultaneousTileGenerationJobsCount` 기본값은 1024(프로젝트도 1024)다 [검증 `RecastNavMesh.cpp:549`, `DefaultEngine.ini:23`]. 런타임에 `UNavigationSystemV1::SetMaxSimultaneousTileGenerationJobsCount`로 조절한다 [검증 `NavigationSystem.h:560-564`].
- `bDoFullyAsyncNavDataGathering`(기본 false): true면 지오메트리 수집이 게임 스레드 밖으로 간다 [검증 `RecastNavMesh.h:864-866, 1499`]. 프로젝트 교훈 L-editor-09는 이 맵에서 true일 때 대기열이 60초 넘게 비지 않았고, false로 7초 안에 해결됐다고 기록한다 [검증 `Docs/Lessons/editor.md:75-81`]. 무한 세계에서도 false로 시작해 측정한다.
- 시간 분할 설정 `TimeSliceFilterLedgeSpansMaxYProcess`(13)와 `TimeSliceLongDurationDebug`(2ms)가 있다 [검증 `RecastNavMesh.h:891-897`]. 더러운 영역 처리 빈도는 `DirtyAreasUpdateFreq`=60Hz다 [검증 `<NAV>/Public/NavigationDirtyAreasController.h:25`].
- 정상 상태 부하 [추정]: 600cm/s로 타일 1000cm 한 칸을 1.67초에 지난다. 생성 반경 3000cm면 전진 방향으로 약 7타일 1열이 새로 든다. 초당 약 4타일이다. 셀 크기 19cm면 타일당 약 53×53 복셀 열이다(1000/19). 작업자 스레드에서 수 ms 수준이고 실측이 필요하다. 프로젝트 교훈 L-editor-06의 편집기 Navigation 표시 플래그(P키)가 켜져 있으면 동적 타일 갱신마다 디버그 지오메트리를 게임 스레드에서 다시 만든다(프레임당 약 400ms) [검증 `Docs/Lessons/editor.md:47-50`]. 끝없는 생성에서는 이 비용이 상시 발생하므로 측정 때 반드시 끈다.

### 2.5 샌드박스 변경과 더러운 영역
- 더러움 플래그는 `Geometry`(1<<0)와 `DynamicModifier`(1<<1)로 나뉜다 [검증 `<ENG>/Classes/AI/Navigation/NavigationDirtyArea.h:15-16`]. 런타임 모드는 `Static / DynamicModifiersOnly / Dynamic`이다 [검증 `<NAV>/Public/NavigationData.h:523-533`].
- 충돌 메시의 `bIsDynamicObstacle`은 "위에 내비메시를 만들지 않고, 추가·제거가 훨씬 빠르다" [검증 `<ENG>/Classes/AI/Navigation/NavCollisionBase.h:33-35`]. 수정자(modifier) 경로라 지오메트리 재수집을 피한다.
- 수준별 판정 [추정]:
  - (A) 지형 변경 없음: Dynamic + 인보커. 재생성은 새로 든 타일과 PCG 출력 등록으로 생긴 더러움뿐이다.
  - (B) 설치·철거: 벽·울타리·상자처럼 위를 걸을 필요가 없는 것은 `bIsDynamicObstacle`로 둔다(수정자 갱신만). 바닥·계단처럼 위를 걷는 건물은 Geometry 더럽힘으로 해당 타일만 재빌드한다. 격자 배치로 건물 크기를 타일 정렬하면 더럽힘이 1~4타일로 제한된다.
  - (C) 지형 변형: 파낼 때마다 Geometry 더럽힘이 생긴다. 연속 채굴은 같은 타일을 반복 재빌드한다. 해결책은 디바운스(일정 시간 묶기)와 타일 크기 축소다. 단 타일을 줄이면 풀 수가 늘어난다.
- **PCG와 내비의 순서 문제** [추정]: PCG가 콜리전이 있는 ISM을 셀에 등록하면 그 영역이 Geometry로 더러워진다. 내비 타일이 PCG보다 먼저 빌드되면 두 번 빌드된다. 대책: ① PCG 생성 반경(128m)을 인보커 반경(30~50m)보다 크게 해서 PCG가 먼저 끝나게 한다. ② 순수 시각 장식은 `bCanEverAffectNavigation=false`로 내비에서 뺀다. ③ 게임플레이 장애물은 PCG 대신 C++ 결정론 생성기가 배치하고, 내비 더럽힘을 한 번만 일으킨다.

## 3. 스트리밍 청크에서 조우·몬스터 스폰 (요약)
- **준비 조건:** 셀 PCG 생성 완료(컴포넌트 `OnPCGGraphGeneratedDelegate`, 기존 조사 `pcg-api-and-nodes.md:16`)와 내비 준비를 모두 확인한다. 내비 준비는 목적지 `ProjectPointToNavigation`이 성공할 때까지 프레임 폴링한다. 청크 부착 델리게이트가 없다는 점은 기존 조사 `actor-id-and-navmesh.md` B-3과 같다. 생성기에 영역 단위 `HasDirtyTiles(const FBox&)`가 있다 [검증 `RecastNavMeshGenerator.cpp:6644`]. 다만 공개 생성기 인터페이스에는 `IsBuildInProgressCheckDirty`·`GetNumRemaningBuildTasks`만 보인다 [검증 `<ENG>/Public/AI/NavDataGenerator.h:37-43`]. 영역 질의 접근 경로는 [추정]이다. 전체 완료 이벤트는 `OnNavigationGenerationFinishedDelegate`다 [검증 `NavigationSystem.h:444`].
- **스폰 고리:** 화면 밖이면서 인보커 생성 반경 안이어야 한다. 가시 최대 26m [추정]이므로 30~45m 고리가 맞다. 카메라 요(yaw) 45° 고정이고 화면이 전방으로 15m 치우치므로, 진행 방향 앞쪽 고리는 더 멀리(40m+) 둔다.
- **결정론:** 조우 표는 `hash(월드시드, 셀좌표, 조우슬롯)` 시드의 `FRandomStream`으로 C++에서 만든다(전역 난수 금지 규칙). "처치·소진" 상태는 셀 키로 저장해 재진입 때 재생성을 막는다. 몬스터 정본 `Content/MonsterAI/Definitions/<Id>.json`과 클래스는 지역 진입 전에 비동기 선로드한다(D-47).
- **해제:** PCG 정리 반경(×1.1)과 별도로 몬스터 해제 거리를 정하되, 내비 제거 반경(5000cm)보다 작게 한다. 내비가 사라진 타일 위에서 이동 실패가 나지 않게 하기 위해서다 [추정].
- **틱 예산:** 생성이 끝없이 이어져도 동시 활성 몬스터 수는 반경으로 묶여 상수다. 기존 예산 틱 시스템(`Performance/BudgetTick/`)과 충돌하지 않는다 [추정].

## 4. 수치 한계 정리
- 엔진 월드 상한은 `UE_LARGE_WORLD_MAX` = 8.796e12cm(약 8,796만 km 폭)다 [검증 `<ENG>/Public/EngineDefines.h:41-56`]. 액터 바운드가 `HALF_WORLD_MAX`를 넘으면 `OutsideWorldBounds`로 충돌·물리를 끈다(`bEnableWorldBoundsChecks`) [검증 `<ENG>/Private/Actor.cpp:2331-2345`, `WorldSettings.h:406`]. 이 상한은 실질 제약이 아니다.
- 실질 제약 순서 [추정 종합]:
  1. PCG 위치 시드 int 변환은 ±21,474km다 [검증 근거 1.7].
  2. 사용자가 정한 PCG 볼륨·내비 경계(권장 ±1,000km)다.
  3. 렌더링·물리 정밀도는 대규모 월드 좌표(LWC)로 대부분 해소되지만 이 갈래에서 검증하지 않았다.
- PCG 셀 좌표 `FIntVector`(int32)와 내비 타일 좌표 int32는 위 범위에서 넉넉하다 [검증 `PCGRuntimeGenScheduler.cpp:792-818`, `RecastNavMesh.cpp:4680-4688`].

## 5. 이 갈래의 판정 (A/B/C)
- **(A) 무한 생성, 지형 변경 없음:** 엔진 기능으로 가능하다. 단 지형 자체(높이장)는 ALandscape로 무한이 안 되므로 런타임 지형 메시 계층이 별도로 필요하다(다른 갈래). PCG는 그 위 장식·배치, 내비는 인보커가 맡는다.
- **(B) 설치·철거·건물:** 가능하다. PCG 출력은 일시적이므로 셀 단위 차분 저장 + 재생성 시 제외·추가 적용이 필수다(PCG 밖 C++). 내비는 장애물을 `bIsDynamicObstacle`로, 걷는 건물은 타일 정렬 Geometry 재빌드로 처리한다.
- **(C) 지형 변형(복셀):** PCG·내비 측면에서는 "입력 지오메트리가 자주 바뀐다"는 문제로 환원된다. 내비 Geometry 재빌드 빈도가 병목이고, 채굴 디바운스와 타일 크기 조정이 핵심 조절 변수다. 복셀 메싱 자체는 엔진 기본 기능이 아니다(다른 갈래).

## 6. 열린 질문·검증 필요
1. 거대 볼륨(±1e8cm) + 분할 컴포넌트 + 2D 격자에서 런타임 생성이 실제로 셀 단위로만 도는지 PIE로 확인한다(`pcg.RuntimeGeneration.EnableDebugOverlay`, 생성 셀 디버그 그리기).
2. 원본 PCG 볼륨을 런타임에 옮길 때 전체 새로고침이 일어나는지 확인한다(변경 추적 경로 미판독).
3. `bFixedTilePoolSize=True` + 거대 NavMeshBoundsVolume + 인보커 구성에서 타일 빌드 시간과 메모리를 실측한다(프로젝트 셀 19cm, 타일 1000cm 기준).
4. Spawn Actor 노드 대신 C++ 스포너로 바꿀지, 선로드 핸들 유지로 갈지 결정한다(D-47).
5. GPU 실행 노드의 출력 순서 결정론을 확인한다(같은 셀 재생성 두 번 비교).
6. `UPCGGenSourceComponent::GetPosition`이 소유자 위치를 쓰는지 본문을 확인한다(탑다운에서 카메라 대신 폰 기준으로 생성하기 위해).
