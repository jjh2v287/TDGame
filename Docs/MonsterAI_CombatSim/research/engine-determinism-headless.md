# 언리얼 5.8 결정론적 고정 스텝 헤드리스 시뮬레이션의 조건과 함정

조사 대상: 언리얼 엔진 5.8 엔진 소스(읽기 전용)와 TDGame 프로젝트 소스.
- `R/` = `C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime`
- `X/` = `R/Experimental/Chaos` (Chaos 물리)
- `PL/` = `C:/Program Files/Epic Games/UE_5.8/Engine/Plugins`
- `P/` = `C:/Project/TDGame`

모든 사실은 "파일:줄" 근거를 붙였다. 코드로 확인하지 못한 항목은 "미확인"으로 표기했다.
웹 자료는 1건(마이크로소프트 C 런타임 `rand` 문서)만 사용했고 URL과 연도를 적었다.

---

## 결론 요약

1. **`UWorld::Tick(LEVELTICK_All, Delta)` 를 직접 부르는 고정 스텝 루프는 유효하다. 단, 매 스텝 `GFrameCounter` 를 직접 1 증가시켜야 한다.** 틱 함수 큐잉은 `GFrameCounter` 하위 32비트로 "이번 프레임에 이미 방문했는지" 판정하므로(`R/Engine/Private/TickTaskManager.cpp:621-624`, `:1476-1487`, `:2624-2631`) 카운터가 멈추면 두 번째 스텝부터 어떤 액터·컴포넌트도 틱되지 않는다. 엔진 루프에서는 `FEngineLoop::Tick` 이 프레임당 한 번 올린다(`R/Launch/Private/LaunchEngineLoop.cpp:6131`). 프로젝트 픽스처 `FTDScopedCombatWorld::Tick` 은 이미 `++GFrameCounter` 를 하고 있다(`P/Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:71-80`). `FTimerManager` 는 월드 델타를 누적하는 논리 시계라 배속에 안전하고(`R/Engine/Private/TimerManager.cpp:1160`), `FLatentActionManager` 는 `GFrameCounter` 가 아니라 자체 플래그로 프레임을 구분한다(`R/Engine/Private/LatentActionManager.cpp:92-98`).

2. **엔진 내장 고정 델타타임(`-UseFixedTimeStep`, `-Deterministic`, `-FPS=`)은 "엔진 루프 전체"를 고정 스텝으로 만들고 대기(sleep)를 하지 않으므로 실시간보다 빠르게 돈다.** `UEngine::UpdateTimeAndHandleMaxTickRate` 의 고정 분기는 `FApp::SetDeltaTime(FixedDeltaTime)` 만 하고 `t.MaxFPS` 대기 로직은 실시간 분기에만 있다(`R/Engine/Private/UnrealEngine.cpp:3004-3022` vs `:3026-3075`). 이 기능은 `WITH_FIXED_TIME_STEP_SUPPORT` 빌드 정의(`UEBuildTarget.cs:6447-6454`, `Rules.bWithFixedTimeStepSupport`)에 묶여 있으며 `-BENCHMARK` 와 `-FPS=` 처리는 비(非)Shipping 전용이다(`LaunchEngineLoop.cpp:2450-2462`, `:4726-4733`).

3. **같은 틱 그룹 안의 액터 틱 순서는 기본 설정에서 결정적이다(포인터 주소에 의존하지 않는다).** 레벨별 틱 함수 집합 `TSet<FTickFunction*> AllEnabledTickFunctions` 를 순회해 게임 스레드 태스크를 만들고(`TickTaskManager.cpp:1481-1487`), 그룹 해제 시 고우선순위 배열 → 일반 배열을 인덱스 순서대로 잠금 해제한다(`:1170-1205`). `TSet` 은 기본이 `TSparseSet` 이고(`R/Core/Public/Containers/Set.h:11-32`, `ContainerAllocationPolicies.h:1912`) 순회는 해시가 아니라 희소 배열 인덱스 순서다(`SparseSet.h.inl:1317-1320`, `:1462-1472`). 즉 순서는 "등록·해제 이력"에만 의존한다(빈 슬롯 재사용은 `SparseArray.h:117-136`). 이를 깨는 콘솔 변수는 `tick.AllowConcurrentTickQueue`(기본 0, 설명에 "순서를 바꿀 수 있음" 명시)와 `tick.AllowAsyncComponentTicks`(기본 1, `bRunOnAnyThread` 틱만 워커 스레드로 보냄)이며(`TickTaskManager.cpp:54-88`), 엔진 런타임 컴포넌트 중 `bRunOnAnyThread=true` 를 쓰는 곳은 `TaskSyncManager` 뿐이다(`R/Engine/Private/TaskSyncManager.cpp:1257-1273`). 전용 서버나 `-onethread` 에서는 아예 단일 스레드 모드가 된다(`:583-611`, `R/Core/Private/Misc/App.cpp:290-302`).

4. **난수: 엔진 전역 난수는 두 종류이고 `-FixedSeed` 로 0 시드가 되지만, 워커 스레드·플러그인·프로젝트 코드가 이를 우회한다.** `FMath::Rand/FRand` 는 C 런타임 `rand()/srand()` 이고 `FMath::SRand` 는 전역 정적 LCG(선형 합동 생성기)다(`R/Core/Public/GenericPlatform/GenericPlatformMath.h:603-620`, `GenericPlatformMath.cpp:9-29`). 시드는 엔진 초기화 때 `-FixedSeed`/`-Deterministic`/`-BENCHMARK` 면 0, 아니면 CPU 사이클이다(`LaunchEngineLoop.cpp:2462-2479`). 그러나 StateTree 는 시드를 안 주면 `FPlatformTime::Cycles()` 로 자체 스트림을 만들고(`PL/Runtime/StateTree/.../StateTreeExecutionContext.cpp:1513`), 일부 StateTree 조건·태스크와 BehaviorTree 대기·서비스, GAS 확률 적용은 전역 `FMath::FRand` 를 쓴다(아래 3절 표). TDGame 도 치명타·산탄 위치에 `FMath::FRand` 를 쓴다(`P/Source/TDGame/Combat/TDCombatComponent.cpp:220`, `TDDamageSubsystem.cpp:203-204`). **시뮬레이션 코드는 월드/전투 단위 `FRandomStream` 하나로 통일해야 한다.**

5. **Chaos 물리 결정론은 `bEnableEnhancedDeterminism` 프로젝트 설정(기본 false) 또는 `p.Chaos.Solver.Deterministic=1` 로 켠다.** 켜면 병렬 충돌 감지 결과를 파티클 ID 기반 키로 정렬하고 아일랜드 관리자의 희소 배열 프리리스트를 정리한다(`X/Private/Chaos/PBDRigidsEvolutionGBF.cpp:1316-1323`, `X/Private/Chaos/PBDCollisionConstraints.cpp:695-711`, `X/Public/Chaos/Collision/CollisionKeys.h:101-115`; 설정 적용은 `R/PhysicsCore/Private/ChaosScene.cpp:90-95`, CVar 는 `X/Private/PBDRigidsSolver.cpp:345-346`, `:3621-3638`). 비동기 물리(`bTickPhysicsAsync`)와 서브스테핑은 기본 꺼져 있고(`R/Engine/Private/PhysicsEngine/PhysicsSettings.cpp:23-27`), 꺼진 상태에서는 게임 틱마다 1스텝을 돌리고 `TG_EndPhysics` 에서 완료를 기다린다(`X/Private/Chaos/Framework/PhysicsSolverBase.cpp:742-753`, `R/Engine/Private/PhysicsEngine/PhysLevel.cpp:239-275`). 반면 **공간 가속 구조(브로드페이즈 트리)의 비동기 재구축 교체 시점은 태스크 완료 여부(`IsComplete()`)로 결정되어 벽시계 타이밍에 의존한다**(`X/Private/Chaos/PBDRigidsEvolution.cpp:802-845`).

6. **스윕(Sweep) 결과는 시간순 정렬되지만 오버랩(Overlap) 결과는 정렬되지 않고 가속 구조 순회 순서 그대로다.** 스윕: `OutHits.Sort(FCompareFHitResultTime())`(`R/Engine/Private/Collision/CollisionConversions.cpp:526-527`, 비교자 `:930-936`, `WorldCollision.cpp:568`). 오버랩: `GeomOverlapMultiImp` → `ConvertOverlapResults` 가 히트 버퍼 순서대로 추가하며 정렬 코드가 없다(`SceneQuery.cpp:1097-1166`, `CollisionConversions.cpp:843-878`). 따라서 **다수 대상 판정은 호출 측에서 안정 정렬(예: 액터 고유 ID·거리)을 해야 결정적이다.** 같은 시간(Time 동일) 스윕 히트 사이의 순서도 `TArray::Sort`(불안정 정렬)라 보장되지 않는다.

7. **내비게이션: 동기 경로 탐색은 결정적이나, 비동기 탐색과 병렬 타일 빌드는 타이밍 의존이 있다.** `FindPathSync` 는 게임 스레드에서 즉시 수행하고(`R/NavigationSystem/Private/NavigationSystem.cpp:1887-1913`), `FindPathAsync` 는 큐에 넣어 다음 틱에 태스크 그래프 워커로 보낸다(`:1968`, `:1821-1829`, `:2018-2026`). 내비메시 타일은 워커 `FAsyncTask` 로 병렬 생성하고 완료 검사 순서(역순 순회 + `IsDone()`)에 따라 `dtNavMesh::addTile` 호출 순서가 달라져 타일 인덱스·솔트가 실행마다 달라질 수 있다(`RecastNavMeshGenerator.cpp:5465`, `:7144`, `:7184-7217`; `R/Navmesh/Private/Detour/DetourNavMesh.cpp:1945-1993`). `MaxSimultaneousTileGenerationJobsCount=1` 과 `EnsureBuildCompletion()` 으로 순차·동기 빌드를 강제할 수 있다(`RecastNavMesh.cpp:549`, `:3619`; `RecastNavMeshGenerator.cpp:5822-5847`; `NavigationSystem.cpp:4492-4500`). DetourCrowd 는 난수를 쓰지 않는다(`R/Navmesh/Private/DetourCrowd/*.cpp` 에 `rand` 호출 없음).

8. **헤드리스 실행 방식 중 "커맨드렛 + 직접 Tick" 이 가장 통제하기 쉽고 빠르다.** 커맨드렛은 `GEngine->Init` 까지만 하고 `Main()` 을 한 번 부른 뒤 종료하며 엔진 루프 `Tick` 을 돌리지 않는다(`LaunchEngineLoop.cpp:4017-4055`, `:4110`, `:4125`). 렌더링은 `FApp::CanEverRender()` 가 커맨드렛·전용 서버·`-nullrhi` 에서 false 라 NullRHI 로 초기화된다(`R/Core/Public/Misc/App.h:405-406`, `R/RHI/Private/DynamicRHI.cpp:303-306`). 자동화 테스트의 레이턴트 커맨드는 엔진 프레임당 1회만 진행되고 대기 명령이 벽시계를 쓰므로 배속 시뮬레이션에 부적합하다(`R/AutomationWorker/Private/AutomationWorkerModule.cpp:66-68`, `LaunchEngineLoop.cpp:6037-6045`, `R/Engine/Private/Tests/AutomationCommon.cpp:797-803`, `:1139-1145`).

9. **`UWorld::CreateWorld(EWorldType::Game)` 기본값은 물리 씬은 만들되 시뮬레이션은 끄고, 내비게이션·AI 시스템은 만들지 않는다.** (`R/Engine/Private/World.cpp:2850`; 빌더 `R/Engine/Classes/Engine/WorldInitializationValues.h:76-79`). 몬스터 AI 시뮬레이션 월드는 `InitializationValues().CreateNavigation(true).CreateAISystem(true).ShouldSimulatePhysics(필요 시 true)` 를 `InIVS` 로 넘겨야 한다. 물리 틱 함수는 `bShouldSimulatePhysics` 가 true 일 때만 등록되며 에디터 빌드에서만 `bEnableTraceCollision` 으로도 등록된다(`PhysLevel.cpp:145-160`).

10. **배속 실행 함정 요약**: (a) 스켈레탈 메시는 `bRecentlyRendered` 가 헤드리스에서 항상 false 이므로 `OnlyTickPoseWhenRendered` 옵션이면 틱이 꺼진다(`R/Engine/Private/Components/SkeletalMeshComponent.cpp:1068-1071`, `SkinnedMeshComponent.cpp:1864`); (b) 전용 서버 넷모드에서는 `bTriggerOnDedicatedServer` 가 꺼진 애님 노티파이가 발화하지 않는다(`R/Engine/Private/Animation/AnimNotifyQueue.cpp:55-63`); (c) 가비지 컬렉션 주기는 `FApp::GetDeltaTime()` 누적(기본 60초)이라 직접 Tick 루프에서는 진행되지 않으므로 수동 `CollectGarbage` 가 필요하다(`UnrealEngine.cpp:2263-2286`, `:1777-1780`); (d) `CharacterMovement` 는 스텝이 0.05초를 넘으면 내부 분할(최대 8회)하므로 델타를 고정하면 결정적이다(`CharacterMovementComponent.cpp:698-700`, `:8162-8184`); (e) 액터 이름은 전역 카운터로 만들어 충돌하지 않지만 실행마다 달라질 수 있으니 이름을 키로 쓰면 안 된다(`R/CoreUObject/Private/UObject/UObjectGlobals.cpp:2621`, `:2703-2715`, `R/Engine/Private/LevelActor.cpp:361-365`).

---

## 상세 조사

### 1) 고정 델타타임과 직접 Tick 루프

| 항목 | 근거 | 요지 |
|---|---|---|
| `FApp::SetFixedDeltaTime / SetUseFixedTimeStep / GetFixedDeltaTime / UseFixedTimeStep` | `R/Core/Public/Misc/App.h:547-559`, `:662-675` | `FAppTime` 로 위임 |
| `FAppTime::bUseFixedTimeStep`, `FixedDeltaTime` 기본 1/30 | `R/Core/Public/Misc/AppTime.h:66-73`, `:130-137` | `WITH_FIXED_TIME_STEP_SUPPORT` 가 0 이면 `static constexpr bool bUseFixedTimeStep=false` 로 컴파일되어 세터가 무시됨 |
| 빌드 정의 | `Engine/Source/Programs/UnrealBuildTool/Configuration/UEBuildTarget.cs:6447-6454` | `Rules.bWithFixedTimeStepSupport` → `WITH_FIXED_TIME_STEP_SUPPORT=1/0` |
| 명령행 `-BENCHMARK`, `-Deterministic`, `-UseFixedTimeStep`, `-FixedSeed` | `R/Launch/Private/LaunchEngineLoop.cpp:2450-2462` | `-Deterministic` = `-UseFixedTimeStep -FixedSeed`; `-BENCHMARK` 는 `!UE_BUILD_SHIPPING` |
| 명령행 `-FPS=` | `LaunchEngineLoop.cpp:4726-4731` | `FApp::SetFixedDeltaTime(1/FixedFPS)`; 비Shipping 블록 안 |
| `UEngine::UpdateTimeAndHandleMaxTickRate()` | `R/Engine/Private/UnrealEngine.cpp:2980`, `:3004-3022` | 고정 분기: `SetDeltaTime(FixedDeltaTime)`, `SetCurrentTime(Current+Delta)` 만 수행, 대기 없음 |
| `t.MaxFPS` | `UnrealEngine.cpp:12136-12137`, `GetMaxTickRate :12193, :12234-12236` | 실시간 분기(`:3058-3075`)에서만 대기 시간 계산 |
| `GFrameCounter++` | `LaunchEngineLoop.cpp:6131` | 엔진 루프 한 프레임당 1회. `UWorld::Tick` 은 올리지 않음 |
| `UWorld::Tick(ELevelTick, float)` | `R/Engine/Private/LevelTick.cpp:1502`, 시간 갱신 `:1584-1611`, 틱 그룹 `:1740-1786`, 레이턴트 `:1658-1661, :1797`, 타이머 `:1816`, Tickable `:1821`, GC `:1970` | 월드 시간은 인자 `DeltaSeconds` 만으로 진행 |
| 틱 함수 방문 판정이 `GFrameCounter` 에 의존 | `R/Engine/Private/TickTaskManager.cpp:621-624`, `:1476-1487`, `:2624-2631` | 아래 발췌 |
| `FTimerManager::Tick` | `R/Engine/Private/TimerManager.cpp:1107`, `:1160`, `:1374`; `TimerManager.h:466-469` | `InternalTime += DeltaTime`; `LastTickedFrame = GFrameCounter`; `HasBeenTickedThisFrame()` 이 `SetTimer` 의 "이번 프레임/다음 프레임" 분기에 사용(`:759`, CVar `TimerManager.GuaranteeEngineTickDelay :58-61`) |
| `FLatentActionManager` | `R/Engine/Private/LatentActionManager.cpp:92-98`, `:168-257` | `bProcessedThisFrame` 플래그를 `BeginFrame` 에서 리셋; 프레임 카운터 미사용 |
| 프로젝트 픽스처 | `P/Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:30-88` | `CreateWorld(Game)`, `CreateNewWorldContext`, `InitializeActorsForPlay`, `BeginPlay`, 스텝 루프에서 `++GFrameCounter; World->Tick(LEVELTICK_All, Delta)` |

발췌(틱 방문 판정):
```cpp
// R/Engine/Private/TickTaskManager.cpp:621-624
FORCEINLINE static bool HasBeenVisited(const FTickFunction* TickFunction, uint32 CurrentFrameCounter)
{
    return (TickFunction->InternalData->TickVisitedGFrameCounter.load(std::memory_order_relaxed) == CurrentFrameCounter);
}
// :1478-1486  uint32 CurrentFrameCounter = (uint32)GFrameCounter;
//             for (TSet<FTickFunction*>::TIterator It(AllEnabledTickFunctions); It; ++It)
//                 if (!TTS.HasBeenVisited(TickFunction, CurrentFrameCounter)) TickFunction->QueueTickFunction(TTS, Context);
```

해석: 직접 Tick 루프는 `FApp` 시간(`FApp::GetCurrentTime/GetDeltaTime`)을 갱신하지 않는다. `FApp::GetDeltaTime` 을 읽는 코드(GC 주기 `UnrealEngine.cpp:2263`, 코어 티커 `LaunchEngineLoop.cpp:6103`)는 진행하지 않으므로 필요하면 루프에서 `FApp::SetDeltaTime/SetCurrentTime` 도 함께 갱신하는 편이 안전하다.

### 2) 틱 순서 결정론

| 항목 | 근거 | 요지 |
|---|---|---|
| 콘솔 변수 정의 | `TickTaskManager.cpp:54-88` | `tick.AllowAsyncComponentTicks=1`, `tick.AllowBatchedTicks=0`, `tick.AllowBatchedTicksUnordered=0`, `tick.AllowOptimizedPrerequisites=1`, `tick.AllowConcurrentTickQueue=0`("can change the order of ticking"), `tick.AllowAsyncTickDispatch=0`, `tick.AllowAsyncTickCleanup=0` |
| 레벨별 틱 함수 컨테이너 | `TickTaskManager.cpp:1221-1225`, `:1296`, `:1481` | `TSet<FTickFunction*> AllEnabledTickFunctions / AllDisabledTickFunctions`, 쿨다운은 연결 리스트 `AllCoolingDownTickFunctions`(`:1263-1279`, 정렬 삽입 `:1395-1469`) |
| `TSet` 순회 순서 | `R/Core/Public/Containers/Set.h:11-32`, `ContainerAllocationPolicies.h:1912`(`UE_USE_COMPACT_SET_AS_DEFAULT 0`), `SparseSet.h.inl:1317-1320`, `:1462-1472` | 요소는 `TSparseArray` 에 저장, 반복자는 희소 배열 인덱스 순. 해시는 조회에만 사용 |
| 빈 슬롯 재사용 | `R/Core/Public/Containers/SparseArray.h:117-136`, `SortFreeList :314-322` | 프리리스트는 후입선출; 같은 등록·해제 이력이면 같은 인덱스 |
| 프레임 시작(비병렬 경로) | `TickTaskManager.cpp:2033-2055` | `bConcurrentQueue` 가 false 면 레벨 순서대로 `StartFrame` → `QueueAllTicks` |
| 실행 스레드 결정 | `TickTaskManager.cpp:628-656` | `bRunOnAnyThread && bAllowConcurrentTicks && 원래 그룹` 일 때만 워커, 그 외 `ENamedThreads::GameThread`(고우선순위 여부만 다름) |
| `bAllowConcurrentTicks` | `:1085-1092` | 단일 스레드 모드면 false, 아니면 `tick.AllowAsyncComponentTicks` |
| 단일 스레드 모드 판정 | `:583-611` | 전용 서버(`UE_DEFAULT_MULTITHREAD_SERVER` 기본 0, `R/Core/Private/Misc/App.cpp:17-19, :304-308`), `!FApp::ShouldUseThreadingForPerformance()`, 코어 3개 미만 |
| `-onethread`, `-noperfthreads`, `-norenderthread` | `R/Core/Private/Misc/App.cpp:290-302`, `R/Core/Private/GenericPlatform/GenericPlatformMisc.cpp:1849-1856` | 각각 성능 스레드 비활성, 렌더 스레드 비활성 |
| 태스크 그래프 워커 수 | `GenericPlatformMisc.cpp:1975-1985` | 게임/서버 실행 시 최대 4, 최소 2 (`-corelimit` 류 옵션은 이 함수에 없음, 미확인) |
| 그룹 해제 순서 | `:672-684`(등록), `:1170-1205`(HiPri 배열 → 일반 배열, 인덱스 순 `Unlock`), `:938-960` | 배열 순서 = 큐잉 순서 = `TSet` 순회 순서 |
| 선행 조건 | `:2636-2668`, `FTickFunction::AddPrerequisite :2481` | 선행 틱을 먼저 큐잉하고 그룹을 선행의 최댓값으로 승격 |
| 신규 스폰 액터 | `:2120-2150` | 그룹 종료 후 `QueueNewlySpawned` 로 같은 프레임에 틱(`TSet` 순서) |
| `TG_DuringPhysics` | `LevelTick.cpp:1765` | `bBlockTillComplete=false` 로 실행 |
| `FTickableGameObject::TickObjects` | `R/Engine/Private/Tickable.cpp:183-187` | 등록 배열 순서대로 순회 |
| 엔진 내 `bRunOnAnyThread=true` 사용처 | `R/Engine/Private/TaskSyncManager.cpp:1257-1273`; 플러그인 `PL/Animation/PoseSearch/.../PoseSearchInteractionIsland.cpp`, `PL/Experimental/UAF/.../ModuleTickFunction.h` | 일반 액터·무브먼트·애님 컴포넌트는 게임 스레드 |

발췌(스레드 선택):
```cpp
// R/Engine/Private/TickTaskManager.cpp:640-654
if (TickFunction->bRunOnAnyThread && bAllowConcurrentTicks && bIsOriginalTickGroup)
{   UseContext.Thread = TickFunction->bHighPriority ? CPrio_HiPriAsyncTickTaskPriority.Get() : CPrio_NormalAsyncTickTaskPriority.Get(); }
else
{   UseContext.Thread = ENamedThreads::SetTaskPriority(ENamedThreads::GameThread, TickFunction->bHighPriority ? ENamedThreads::HighTaskPriority : ENamedThreads::NormalTaskPriority); }
```

결론: 같은 틱 그룹 안 순서는 "등록 순서(+고우선순위 플래그, 선행 조건)"로 확정된다. 순서를 명시적으로 고정하려면 (1) 스폰 순서를 고정하고, (2) 필요한 곳에 `AddPrerequisite`, (3) `tick.AllowConcurrentTickQueue=0`(기본) 유지, (4) 헤드리스에서는 `-onethread` 또는 전용 서버 모드로 단일 스레드 강제가 가능하다.

### 3) 난수

| 항목 | 근거 | 요지 |
|---|---|---|
| `FMath::Rand()` = `rand()`, `RandInit` = `srand`, `FRand` = `Rand()&0xffffff / max` | `R/Core/Public/GenericPlatform/GenericPlatformMath.h:603-617` | C 런타임 의존 |
| `SRandInit / SRand / GetRandSeed` | `GenericPlatformMath.h:620-623`, `GenericPlatformMath.cpp:9-29` | 전역 `static int32 GSRandSeed` LCG(`*196314165 + 907633515`) |
| 엔진 시드 초기화 | `LaunchEngineLoop.cpp:2462-2479` | `bUseFixedSeed` = `-Deterministic` ∨ `-BENCHMARK` ∨ `-FixedSeed`; 아니면 `FPlatformTime::Cycles()` 두 번 |
| `FRandomStream` | `R/Core/Public/Math/RandomStream.h:30-33`(기본 시드 0), `:63-66`, `:75-86`(FName 시드, 없으면 Cycles), `:105-108`(`GenerateNewSeed` 는 `FMath::Rand`) | 인스턴스 난수; 시드 재현 가능 |
| Chaos 난수 | `X/Private/Chaos/Island/IslandManager.cpp:156-157`, `:2402-2411`, `:2521-2524` | `p.Chaos.Solver.RandomizeConstraintOrder`(기본 false) 테스트용에서만 `FMath::RandRange`. 그 외 난수 사용 파일은 Field/GeometryCollection 유틸(`FieldSystemNodes.cpp`, `CollectionTransformSelectionFacade.cpp`, `GeometryCollectionClusteringUtility.cpp`)로 강체 솔버 경로가 아님 |
| AI 시스템 스트림 | `R/AIModule/Private/AISystem.cpp:21`, `:36-43` | `-FixedSeed` 면 시드 0 유지, 아니면 현재 시각 |
| BehaviorTree | `R/AIModule/Private/BehaviorTree/Tasks/BTTask_Wait.cpp:19`, `BTService.cpp:107` | 전역 `FMath::FRandRange` |
| StateTree | `PL/Runtime/StateTree/Source/StateTreeModule/Private/StateTreeExecutionContext.cpp:1513`, `:7983`; `Conditions/StateTreeCommonConditions.cpp:438`; `Tasks/StateTreeDelayTask.cpp:23` | 실행 스트림은 시드 지정 가능(미지정 시 Cycles); 랜덤 조건과 지연 태스크는 전역 `FMath::FRandRange` |
| GAS | `PL/Runtime/GameplayAbilities/.../GameplayEffectComponents/ChanceToApplyGameplayEffectComponent.cpp:29`, `GameplayCueNotifyTypes.cpp:165` | 적용 확률·큐 재생 확률에 전역 `FMath::FRand` |
| 내비게이션 랜덤 지점 | `R/NavigationSystem/Private/NavMesh/RecastNavMesh.cpp:1952-1994`, `PImplRecastNavMesh.cpp:1899-2349` | `FMath::FRand` 전달 |
| TDGame | `P/Source/TDGame/Combat/TDCombatComponent.cpp:220`, `TDDamageSubsystem.cpp:203-204`, `Variant_TwinStick/AI/TwinStickNPC.cpp:104`, `TwinStickSpawner.cpp:87` | 치명타·산탄·드롭·스폰 지연이 전역 난수 |
| 고유 이름 카운터 | `R/CoreUObject/Private/UObject/UObjectGlobals.cpp:2247`, `:2621`, `:2703-2715` | 전역 원자 카운터·클래스별 카운터. 난수는 아니지만 실행 간 동일성 없음 |

C 런타임 `rand()` 의 스레드별 상태 여부: 마이크로소프트 문서(https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/rand, 갱신 2022-12)는 "global state is scoped to the application" 만 적고 스레드별 시드 여부를 명시하지 않는다 → **미확인**. 어느 쪽이든 워커 스레드(내비 비동기 탐색, 물리 스레드)에서 전역 난수를 호출하면 순서가 비결정적이므로 시뮬레이션 경로에서는 전역 난수 호출 자체를 없애야 한다.

### 4) 물리(Chaos)

| 항목 | 근거 | 요지 |
|---|---|---|
| `p.Chaos.Solver.Deterministic` | `X/Private/PBDRigidsSolver.cpp:345-346`, `IsDetemerministic :3621-3624`, `UpdateIsDeterministic :3635-3638`, `:3796-3799` | `-1` 설정값 사용, `1` 강제. 리와인드 데이터나 네트워크 예측이 켜져도 true |
| 프로젝트 설정 `bEnableEnhancedDeterminism` | `R/PhysicsCore/Public/PhysicsSettingsCore.h:47-49`, 기본 false `PhysicsSettingsCore.cpp:35`, 적용 `R/PhysicsCore/Private/ChaosScene.cpp:90-95` | `SceneSolver->SetIsDeterministic(Settings->bEnableEnhancedDeterminism)` |
| 결정론 모드가 하는 일 | `X/Private/Chaos/PBDRigidsEvolutionGBF.cpp:1316-1323` | 충돌 제약 정렬 + 아일랜드 희소 배열 프리리스트 유지 |
| 충돌 정렬 키 | `X/Private/Chaos/PBDCollisionConstraints.cpp:695-711`, `X/Public/Chaos/Collision/CollisionKeys.h:101-115` | `ParticleID().LocalID/GlobalID` 기반 → 생성 순서에만 의존(포인터 아님). 주석: "두 집합을 다른 순서로 만들면 다르게 동작" |
| `FChaosSolverConfiguration` | `X/Public/ChaosSolverConfiguration.h:51-112` | 반복 횟수 등; 결정론 플래그는 이 구조체가 아니라 `PhysicsSettingsCore` 에 있음 |
| 비동기 물리·서브스텝 기본값 | `R/Engine/Private/PhysicsEngine/PhysicsSettings.cpp:23-27` | `bSubstepping=false, bTickPhysicsAsync=false, AsyncFixedTimeStepSize=1/30, MaxSubstepDeltaTime=1/60, MaxSubsteps=6` |
| 비동기 dt 전달 | `R/Engine/Private/PhysicsEngine/Experimental/PhysScene_Chaos.cpp:548-549` | `bTickPhysicsAsync ? AsyncFixedTimeStepSize : -1` |
| 스텝 계산 | `X/Private/Chaos/Framework/PhysicsSolverBase.cpp:590-618` | 고정 dt 모드: 누적 시간을 `AsyncDt` 로 나눠 정수 스텝; 서브스텝 모드: `Ceil(Dt/MaxDeltaTime)` 를 `MaxSubSteps` 로 제한 |
| 동기 모드 블로킹 | `PhysicsSolverBase.cpp:742-753` | `IsUsingAsyncResults()==false` 면 즉시 블로킹 태스크로 등록 |
| 관련 CVar | `PhysicsSolverBase.cpp:108-109`(`p.PhysicsRunsOnGT`), `:263`(`p.UseAsyncInterpolation`), `:266`(`p.ForceDisableAsyncPhysics`), `:292`(`p.AsyncPhysicsBlockMode`), `:579`(`p.MaxPhysicsStepsPerGameTick`) | 질문의 `p.Chaos.Solver.AsyncDt`, `p.Chaos.Solver.UseAsyncResults` 라는 이름의 CVar 는 5.8 소스에서 찾지 못함(미확인/부재) |
| 물리 틱 함수 등록 | `R/Engine/Private/PhysicsEngine/PhysLevel.cpp:137-160`, `:215-234`, `:239-275` | `bShouldSimulatePhysics`(에디터 빌드는 `bEnableTraceCollision` 도) 일 때만 `TG_StartPhysics/TG_EndPhysics` 등록; End 에서 완료 이벤트 대기 |
| 가속 구조 비동기 재구축 | `X/Private/Chaos/PBDRigidsEvolution.cpp:802-845`, CVar `:43-57` | `AccelerationStructureTaskComplete->IsComplete()` 로 교체 여부 결정 → 타이밍 의존. `p.Chaos.AccelerationStructureTimeSlicingMaxQueueSizeBeforeForce`(1000), `...MaxBytesCopy`(100000), `p.Chaos.AccelerationStructureUseDynamicTree`(1) |
| 스윕 정렬 | `R/Engine/Private/Collision/CollisionConversions.cpp:526-527`, 비교자 `:930-936`, `SceneQuery.cpp:683-686`, `WorldCollision.cpp:568` | 시간 오름차순, 동시간이면 비차단 → 차단 순. 동시간·동종은 `return true` 라 순서 비보장(불안정 정렬) |
| 오버랩 순서 | `SceneQuery.cpp:1097-1166`, `CollisionConversions.cpp:843-878` | 버퍼 순서 그대로, 컴포넌트 단위 중복 제거만 수행 |
| `UCharacterMovementComponent` | `R/Engine/Private/Components/CharacterMovementComponent.cpp:698-700`(`MaxSimulationTimeStep=0.05`, `MaxSimulationIterations=8`), `:8162-8184`, `:97`(`MIN_TICK_TIME=1e-6`), 바닥·서기 판정 `:3311-3455`(`OverlapBlockingTestByChannel`, `SweepSingleByChannel`) | 델타 의존 분할; 바닥 판정은 단일 스윕·차단 테스트라 오버랩 순서 문제 없음 |

발췌(오버랩 변환, 정렬 없음):
```cpp
// R/Engine/Private/Collision/CollisionConversions.cpp:843-867
for (int32 PResultIndex = 0; PResultIndex < NumOverlaps; ++PResultIndex)
{   FOverlapResult NewOverlap; ...
    int32& DestinationIndex = OverlapMap.FindOrAdd(FOverlapKey(NewOverlap));
    if (DestinationIndex == 0) { DestinationIndex = OutOverlaps.Add(NewOverlap) + 1; }
```

해석: 물리를 결정적으로 쓰려면 (1) `bEnableEnhancedDeterminism=true`, (2) 비동기 물리 끔(기본) 또는 켜더라도 고정 dt, (3) 바디 생성 순서 고정, (4) 가속 구조 비동기 재구축의 타이밍 의존은 남는다. 시뮬레이션이 "쿼리 전용(QueryOnly)" 콜리전만 쓴다면 강체 솔버 결정론은 필요 없고, 쿼리 결과 순서만 호출 측에서 정렬하면 된다.

### 5) 내비게이션

| 항목 | 근거 | 요지 |
|---|---|---|
| 동기 탐색 | `R/NavigationSystem/Private/NavigationSystem.cpp:1887`, `:1913` | `FPathFindingResult FindPathSync(const FNavAgentProperties&, FPathFindingQuery, EPathFindingMode::Type)` 게임 스레드 즉시 |
| 비동기 탐색 | `:1968`(큐 추가), `:1971`(`uint32 FindPathAsync(...)`), `Tick :1633`, `:1821-1829`, `:2018-2026`(`FSimpleDelegateGraphTask` 워커), `:2053-2075` | 결과는 다음 틱에 배치 처리·디스패치 |
| 스레드별 Detour 쿼리 객체 | `RecastNavMesh.cpp:51-57` | 게임 스레드는 공유 `SharedNavQuery`, 워커는 지역 객체 |
| 비동기 타일 빌드 | `RecastNavMesh.h:31`(`RECAST_ASYNC_REBUILDING 1`), `RecastNavMeshGenerator.cpp:5465`(워커×2), `:7144`(`FAsyncTask<FRecastTileGeneratorWrapper>`), `:7184-7217`(완료 검사 역순 순회) | 완료 순서에 따라 `addTile` 순서가 달라짐 |
| 타일 인덱스·솔트 | `R/Navmesh/Private/Detour/DetourNavMesh.cpp:1945-1993`, `:2703` | 프리리스트 `m_nextFree` 에서 할당, 제거 시 솔트 증가 → 폴리 참조값이 빌드 순서에 의존 |
| 순차·동기 강제 | `RecastNavMesh.cpp:549`(`MaxSimultaneousTileGenerationJobsCount=1024`), `:3619`(세터), `:550`(`bDoFullyAsyncNavDataGathering=false`), `RecastNavMeshGenerator.cpp:5822-5847`(`EnsureBuildCompletion`), `NavigationSystem.cpp:4492-4500`(`Build()` 후 블로킹) | 잡 수 1 + 완료 대기로 순서 고정 가능 |
| 대기 타일 정렬 | `RecastNavMeshGenerator.cpp:6988-7031` | 시드 위치·우선순위로 정렬 |
| Detour 회피(DetourCrowd) | `R/Navmesh/Private/DetourCrowd/*.cpp` 에 `rand` 호출 없음; `R/AIModule/Classes/Navigation/CrowdManager.h:171-173`, `CrowdManager.cpp:231-268` | `FTickableGameObject` 로 `UWorld::Tick` 안에서 델타 기반 갱신 → 순수 결정적 |
| 랜덤 지점 API | 3절 표 참조 | 전역 `FMath::FRand` |

해석: 시뮬레이션은 `FindPathSync` 만 쓰고, 내비메시는 실행 전 1회 동기 빌드(잡 수 1, `EnsureBuildCompletion`)하거나 미리 구운 에셋을 로드한다. 미리 구운 내비메시는 실행 간 동일하다.

### 6) 헤드리스 실행 방식 비교

| 방식 | 초기화·사용 가능성 | 속도·병렬성 | 근거 |
|---|---|---|---|
| (a) 커맨드렛 `UCommandlet::Main` + `UWorld::CreateWorld` + 직접 Tick | `GEngine` 생성·`Init` 후 `Main()` 1회 호출, 엔진 루프 Tick 없음. `IsEditor` 플래그면 `UEditorEngine`, 아니면 `GameEngine` 클래스. 렌더링은 `CanEverRender()=false` → NullRHI. 월드 서브시스템·GAS 전역(`IGameplayAbilitiesModule::GetAbilitySystemGlobals` 첫 호출 시 지연 생성)·내비·AI 는 `InitializationValues` 로 켜면 사용 가능 | 스텝당 비용 = `UWorld::Tick` 순수 비용. 실시간 대비 상한 없음. 프로세스별 독립 실행 가능(공유 파일 잠금은 미확인) | `LaunchEngineLoop.cpp:3960-3985`, `:4017-4055`, `:4110`, `:4125`; `App.h:405-406`; `DynamicRHI.cpp:303-306`; `Commandlet.h:70-92`; `PL/Runtime/GameplayAbilities/.../GameplayAbilitiesModule.cpp:28-41`; `World.cpp:2850` |
| (b) `UnrealEditor-Cmd -game -nullrhi -unattended -ExecCmds` | `-game`: `GIsClient=1, GIsEditor=0`; `-unattended` 는 `FApp::IsUnattended`; `-ExecCmds` 는 엔진 초기화 후 지연 큐. 완전한 게임 모드(GameMode/GameState) | 엔진 루프 그대로 → `-UseFixedTimeStep -FPS=` 로 대기 없이 고정 스텝. 렌더·오디오·슬레이트·자동화 워커 등 프레임 오버헤드 포함 | `LaunchEngineLoop.cpp:2269-2272`; `App.cpp:274-280`; `UnrealEngine.cpp:2552` |
| (c) 서버 타깃 빌드 | `IsRunningDedicatedServer()` 참(서버 전용 빌드 또는 에디터 `-server`); 틱 시퀀서 단일 스레드; 애님 노티파이는 `bTriggerOnDedicatedServer` 필요; `CanEverRender=false` | 스레딩 비활성으로 결정론에 유리하나 서버 넷모드가 GAS 예측·리플리케이션 경로를 탄다 | `R/Core/Public/Misc/CoreMisc.h:152-168`; `TickTaskManager.cpp:601-605`; `AnimNotifyQueue.cpp:55-63`; `LaunchEngineLoop.cpp:2283-2286` |
| (d) 자동화 테스트(`FAutomationTestBase` + 레이턴트 커맨드) + Gauntlet | 에디터 프로세스 안에서 실행; 레이턴트 커맨드는 `FAutomationWorkerModule::Tick` → `ExecuteLatentCommands` 로 엔진 프레임당 1개씩 갱신; `FWaitLatentCommand/FEngineWaitLatentCommand` 는 `FPlatformTime::Seconds()` 벽시계 | 프로젝트 픽스처처럼 단일 `RunTest` 안에서 직접 Tick 루프를 돌리면 (a) 와 같은 속도. Gauntlet 은 C# 오케스트레이션(`Engine/Source/Programs/AutomationTool/Gauntlet/`)으로 다중 프로세스 실행·결과 수집 담당 | `R/Core/Public/Misc/AutomationTest.h:525-534`, `:4087`; `AutomationTest.cpp:679-690`; `AutomationWorkerModule.cpp:66-68`; `LaunchEngineLoop.cpp:6037-6045`; `AutomationCommon.cpp:797-803`, `:1139-1145` |

월드 생성 기본값 발췌:
```cpp
// R/Engine/Private/World.cpp:2850
NewWorld->InitializeNewWorld(InIVS ? *InIVS : UWorld::InitializationValues()
    .CreatePhysicsScene(InWorldType != EWorldType::Inactive).ShouldSimulatePhysics(false)
    .EnableTraceCollision(true).CreateNavigation(InWorldType == EWorldType::Editor)
    .CreateAISystem(InWorldType == EWorldType::Editor), bInSkipInitWorld);
```
AI 시스템은 `InitWorld` 에서 `IVS.bCreateAISystem && WorldSettings->IsAISystemEnabled()` 일 때 생성되고(`World.cpp:2485-2493`, `:2066-2087`), 넷모드가 클라이언트가 아니면 인스턴스화된다(`R/Engine/Private/AI/AISystemBase.cpp:46-50`). GAS 의 서버 월드 시간은 `GameState` 가 없으면 `World->GetTimeSeconds()` 로 대체된다(`PL/Runtime/GameplayAbilities/.../GameplayEffect.cpp:5354-5366`). 지속 효과는 `FTimerManager` 타이머를 쓴다(`GameplayEffect.cpp:4485-4490`).

### 7) 실시간보다 빠르게 돌릴 때의 함정

| 함정 | 근거 | 대응 |
|---|---|---|
| 스켈레탈 메시 `bRecentlyRendered` 가 헤드리스에서 항상 false | `SkinnedMeshComponent.cpp:1864`(`LastRenderTime > TimeSeconds-1`), `ShouldTickPose :1795-1802`, `SkeletalMeshComponent.cpp:1068-1071`(`OnlyTickPoseWhenRendered && !CanEverRender` → 틱 비활성) | 기본값 `AlwaysTickPoseAndRefreshBones`(`SkeletalMeshComponent.cpp:446`) 유지 |
| 업데이트 레이트 최적화(URO) 가 비렌더 상태에서 갱신 빈도를 낮춤 | `SkinnedMeshComponent.cpp:293-300`, 활성 조건 `:1812`(`bEnableUpdateRateOptimizations && a.URO.Enable`) | 시뮬레이션 메시는 URO 끄기 |
| 몽타주는 서브스텝으로 진행하며 노티파이는 델타 구간 스캔 | `R/Engine/Private/Animation/AnimMontage.cpp:1703`, `:2147-2200`(`FMontageSubStepper::Advance`), `AnimInstance.cpp:1820`(`TriggerAnimNotifies`) | 스텝을 노티파이 간격보다 작게 고정. 기억 파일 `tdgame-anim-notify-facts.md` 의 시간 좌표 제약 병행 |
| 전용 서버 넷모드 노티파이 필터 | `AnimNotifyQueue.cpp:55-63` | 서버 빌드 시 `bTriggerOnDedicatedServer` 설정 필요 |
| 타이머 정밀도 | `TimerManager.cpp:1160`(`InternalTime += DeltaTime`), `:759`, `TimerManager.h:466-469` | 스텝 크기가 곧 정밀도; `SetTimerForNextTick` 은 `GFrameCounter` 판정에 의존 → 카운터 증가 필수 |
| GC 시점 | `UnrealEngine.cpp:2185-2190`(`GFrameCounter != LastGCFrame`), `:2263-2286`(`FApp::GetDeltaTime` 누적, `gc.TimeBetweenPurgingPendingKillObjects=60` `:1777-1780`), 호출 `LevelTick.cpp:1970` | 직접 루프에선 사실상 GC 안 됨 → 시나리오 종료마다 `CollectGarbage` 또는 `FApp::SetDeltaTime` 갱신 |
| 액터·오브젝트 이름 | `LevelActor.cpp:355-365`, `UObjectGlobals.cpp:2247`, `:2703-2715` | 충돌은 없으나 실행 간 값이 다름 → 로그·리플레이 키로 사용 금지 |
| 벽시계 의존 코드 | `AutomationCommon.cpp:797-803`; `StateTreeExecutionContext.cpp:1513`; `AISystem.cpp:36-43`; `RandomStream.h:83` | 시드·대기를 명시 값으로 |
| `FApp` 시간 미갱신 | `LaunchEngineLoop.cpp:6103`(코어 티커), `UnrealEngine.cpp:2263` | 코어 티커(`FTSTicker`) 기반 기능은 직접 루프에서 진행 안 됨 |
| 캐릭터 무브먼트 분할 | `CharacterMovementComponent.cpp:8162-8184` | 스텝 ≤ 0.05 초면 분할 없음 |
| 물리 씬 미틱 | `PhysLevel.cpp:145-160` | 비에디터 빌드에서 `ShouldSimulatePhysics(false)` 면 물리 틱 함수 미등록; 쿼리 전용 콜리전이 갱신되는지는 미확인(아래) |

---

## 프로젝트 적용 시사점

1. **실행 형태**: 밸런스 시뮬레이터는 **커맨드렛(`UCommandlet`)** 으로 만들고 안에서 `FTDScopedCombatWorld` 와 같은 방식(`CreateWorld(Game)` + `CreateNewWorldContext` + `InitializeActorsForPlay` + `BeginPlay` + `++GFrameCounter; World->Tick(LEVELTICK_All, 고정 스텝)`)을 쓴다. 에디터 자동화 테스트 픽스처는 같은 루프를 이미 검증했으므로 코드 공유가 가능하다. 실행 명령은 `UnrealEditor-Cmd.exe TDGame.uproject -run=<커맨드렛> -nullrhi -unattended -FixedSeed -onethread` 형태가 된다(`-FixedSeed` 는 전역 난수 시드 0 고정, `-onethread` 는 단일 스레드 틱). 현재 기억 파일 `tdgame-build-and-test-workflow.md` 의 빌드·테스트 명령과 결합한다.

2. **월드 옵션**: `UWorld::InitializationValues().CreatePhysicsScene(true).ShouldSimulatePhysics(false).EnableTraceCollision(true).CreateNavigation(true).CreateAISystem(true)` 를 `CreateWorld` 의 `InIVS` 로 넘긴다. 몬스터 이동을 `CharacterMovement` 로 하면 강체 시뮬레이션 없이도 스윕·오버랩만으로 동작하므로 Chaos 결정론 옵션은 선택 사항이다. 물리 시뮬레이션이 꼭 필요하면 `bEnableEnhancedDeterminism=true` 와 바디 생성 순서 고정을 추가한다.

3. **틱 순서 규약**: 스폰 순서 = 틱 순서다. 시나리오 로더가 플레이어 → 몬스터(정렬된 ID 순) 순으로 스폰하고, 데미지 처리처럼 순서에 민감한 컴포넌트에는 `AddPrerequisite` 로 의존을 명시한다. `tick.AllowConcurrentTickQueue` 는 절대 켜지 않는다.

4. **난수 규약**: `FMath::FRand` 호출을 전투·AI 경로에서 제거하고, `UTDDamageSubsystem`(월드 서브시스템)에 `FRandomStream` 하나를 두어 시나리오 시드로 초기화한다. `TDCombatComponent.cpp:220` 치명타, `TDDamageSubsystem.cpp:203-204` 산탄 위치가 우선 교체 대상이다. StateTree 를 쓰면 `Start(FStartParameters{ .RandomSeed })` 로 시드를 주고, 랜덤 조건·지연 태스크(전역 난수 사용)는 자체 구현으로 대체한다. GAS `ChanceToApply` 컴포넌트는 쓰지 않는다.

5. **쿼리 결과 정렬**: 범위 공격·타깃 선정에서 `OverlapMulti*` 결과를 쓸 때는 항상 `(거리, 액터 고유 번호)` 등 결정적 키로 정렬한 뒤 처리한다. 스윕 다중 히트도 같은 시간 값에서 순서가 보장되지 않으므로 같은 규칙을 적용한다.

6. **내비게이션**: 시뮬레이션 맵의 내비메시는 미리 구워 로드하거나, 시작 시 `SetMaxSimultaneousTileGenerationJobsCount(1)` → `Build()` → `EnsureBuildCompletion()` 으로 동기 빌드한다. AI 이동은 `FindPathSync` 만 사용한다. DetourCrowd 는 그대로 써도 된다.

7. **애니메이션 의존 로직**: 근접 공격 노티파이가 전투 판정에 관여하므로 시뮬레이션 몬스터의 스켈레탈 메시는 `AlwaysTickPoseAndRefreshBones`(기본) 유지, URO 비활성, 스텝은 노티파이 최소 간격보다 작게(예: 1/60 초) 고정한다. 장기적으로는 "공격 타이밍 테이블"을 데이터로 추출해 애니메이션 없이도 판정할 수 있게 하면 스텝을 키워 더 빠르게 돌릴 수 있다.

8. **정리·재현성**: 시나리오 하나가 끝날 때마다 `World->DestroyWorld` 와 `CollectGarbage(RF_NoFlags)` 를 명시 호출한다. 로그·결과 파일에는 액터 이름 대신 시나리오가 부여한 정수 ID 를 쓴다. 병렬화는 프로세스 단위(커맨드렛 N개, 시나리오 범위 분할)로 하고 각 프로세스는 `-FixedSeed` 와 시나리오 시드를 명령행으로 받는다.

9. **결정론 검증 테스트**: 같은 시드·같은 시나리오를 두 번 돌려 프레임별 체력 합계 해시가 같은지 확인하는 자동화 테스트를 추가한다(픽스처 재사용). 이 테스트가 깨지면 위 규약 위반을 찾는 기준이 된다.

10. **피할 것**: 엔진 루프(`-game`)에 얹는 방식은 렌더·오디오·자동화 워커·코어 티커 비용이 붙고 GameMode 의존이 생기므로 대량 반복에는 피한다. 레이턴트 커맨드로 시간을 흘리는 자동화 테스트 구조도 피한다(벽시계 대기). 전용 서버 타깃은 넷모드 부작용(노티파이 필터, GAS 예측 경로)이 있어 시뮬레이터 용도로는 권하지 않는다.

---

## 미확인·미해결 질문

1. **비에디터(패키지·서버) 빌드에서 `ShouldSimulatePhysics(false)` 인 월드의 쿼리 전용 콜리전이 갱신되는가.** `PhysLevel.cpp:147-149` 는 비에디터에서 `bEnablePhysics = bShouldSimulatePhysics` 만 보므로 물리 틱 함수가 등록되지 않는다. 외부 가속 구조 갱신(`UpdateExternalAccelerationStructure_External`, `PBDRigidsSolver.cpp:3617-3620`)이 솔버 틱 없이도 돌아가는지 코드로 확정하지 못했다. 커맨드렛은 에디터 빌드에서 실행되므로 당장은 문제가 없지만, 패키지 빌드 시뮬레이터로 옮길 때 재확인해야 한다.
2. **C 런타임 `rand()` 의 스레드별 상태.** 마이크로소프트 문서(2022)는 명시하지 않는다. 결론에는 영향이 없다(전역 난수 자체를 배제).
3. **`p.Chaos.Solver.AsyncDt`, `p.Chaos.Solver.UseAsyncResults` 콘솔 변수는 5.8 소스에서 찾지 못했다.** 비동기 물리는 `UPhysicsSettings::bTickPhysicsAsync/AsyncFixedTimeStepSize` 와 `p.ForceDisableAsyncPhysics`, `p.AsyncPhysicsBlockMode` 로 제어한다.
4. **가속 구조 비동기 재구축이 오버랩 결과 순서를 실제로 바꾸는지**는 코드 구조상 가능성만 확인했고 실험으로 확정하지 않았다. 호출 측 정렬로 회피한다.
5. **`FTaskSyncManager`(`TaskSyncManager.cpp:1257-1273`)가 어떤 조건에서 활성화되어 워커 스레드 틱을 만드는지**는 이번 조사 범위 밖이다. 프로젝트가 이 기능을 명시적으로 쓰지 않으면 영향 없다.
6. **다중 커맨드렛 프로세스가 같은 프로젝트 디렉터리를 공유할 때의 파일 잠금(Saved/Logs, DDC)** 은 코드로 확인하지 않았다. `-abslog=`, 별도 `-Saved` 경로 지정으로 회피 가능성이 있으나 미확인.
7. **`-onethread` 가 커맨드렛에서 태스크 그래프 워커 수를 실제로 줄이는지**는 `FApp::ShouldUseThreadingForPerformance` 경로(`App.cpp:290-302`)까지만 확인했고 태스크 그래프 생성 인자까지는 추적하지 않았다.
