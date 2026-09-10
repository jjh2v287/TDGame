# MassEntity·MassGameplay·MassAI·MassCrowd 로 전투 몬스터를 다루는 구조와 틱 제어

조사 대상 엔진: `C:/Program Files/Epic Games/UE_5.8/Engine` (설치형 빌드, `Engine/Build/InstalledBuild.txt` 존재).
아래 상대 경로는 특별한 표기가 없으면 `Engine/` 기준이다. 약어: ECS(Entity Component System, 엔티티 컴포넌트 시스템), ISM(Instanced Static Mesh, 인스턴스 스태틱 메시), CVar(Console Variable, 콘솔 변수), GAS(Gameplay Ability System), LOD(Level of Detail, 세부 수준), ASC(Ability System Component).

## 결론 요약

설계 결정에 바로 쓸 수 있는 문장들이다. 각 문장 끝에 근거를 붙였다.

1. **Mass 의 틱은 기본적으로 월드 틱 함수 6개(PrePhysics/StartPhysics/DuringPhysics/EndPhysics/PostPhysics/FrameEnd)에 묶여 있지만, `FMassProcessingPhaseManager::TriggerPhase(Phase, DeltaTime, CompletionEvent)` 로 임의 DeltaTime 을 넣어 한 페이즈를 수동 실행할 수 있다.** 엔진 자체 테스트가 바로 이 방식(월드 틱 등록을 건너뛴 파생 매니저 + 페이즈별 수동 트리거)으로 동작한다. — `Source/Runtime/MassEntity/Private/MassProcessingPhaseManager.cpp:378-388`, `Source/Developer/MassEntityTestSuite/Private/MassTestTypes.cpp:200-210`, `Source/Developer/MassEntityTestSuite/Private/MassProcessingPhasesTest.cpp:59-75`.
2. **프로세서 하나 또는 파이프라인을 월드 없이 직접 실행하는 공식 유틸이 있다.** `UE::Mass::Executor::Run(UMassProcessor&, FProcessingContext&)` 와 `FProcessingContext(EntityManager, DeltaSeconds, bFlushCommandBuffer)`. `FMassEntityManager` 는 Owner 가 `nullptr` 이어도 생성·초기화된다. — `Source/Runtime/MassEntity/Public/MassExecutor.h:22-52`, `Public/MassProcessingContext.h:22-25`, `Source/Developer/MassEntityTestSuite/Private/MassTestTypes.cpp:17-35`.
3. **프로세서 실행 순서는 결정적이다(같은 프로세서 집합·같은 아키타입이면 같은 그래프).** 후보 목록은 `GetDerivedClasses` 후 이름순 정렬, 솔버는 `ExecutionPriority` 내림차순 → 대기 노드 수 내림차순으로 정렬한 뒤 자원 접근 충돌 검사로 순서를 확정한다. 랜덤 요소가 없다. — `Source/Runtime/MassEntity/Private/MassEntitySettings.cpp:137,169-171`, `Private/MassProcessorDependencySolver.cpp:829-833,855-857`.
4. **병렬 실행은 기본 켜짐(`MASS_DO_PARALLEL = !UE_SERVER`)이며 CVar 로 끌 수 있다.** `mass.FullyParallel 0`(페이즈 내 프로세서 병렬 분배), `mass.AllowQueryParallelFor 0`(쿼리 ParallelFor), `mass.UseProcessingQueue`, `mass.ForceInlineProcessorExecution`(ReadOnly). 설치형 엔진이므로 `MASS_DO_PARALLEL` 매크로는 바꿀 수 없고 CVar 가 유일한 손잡이다. — `Public/MassProcessingTypes.h:13-15`, `Private/MassProcessingPhaseManager.cpp:30-38`, `Private/MassEntityQuery.cpp:26-32`, `Private/MassProcessingQueueTypes.cpp:26-34`.
5. **결정론의 가장 큰 함정은 "엔티티 압축(compaction)" 이다.** `UMassSimulationSubsystem` 이 PrePhysics 시작마다 `FMassEntityManager::DoEntityCompaction(TimeAllowed)` 를 호출하고, 이 함수는 `FPlatformTime::Seconds()` 벽시계 예산으로 청크 간 엔티티를 옮긴다. 실행 속도에 따라 청크 내 엔티티 순서가 달라지므로 시뮬레이션에서는 반드시 꺼야 한다(`mass.EntityCompaction 0` 또는 `UMassSimulationSettings::bEntityCompactionEnabled=false`). — `Plugins/Runtime/MassGameplay/Source/MassSimulation/Private/MassSimulationSubsystem.cpp:22-30,263-282`, `Source/Runtime/MassEntity/Private/MassEntityManager.cpp:586-612`, `Private/MassArchetypeData.cpp:929-957`.
6. **Mass 가 자체 제공하는 결정론 스위치는 `FApp::bUseFixedSeed`(`-FixedSeed`/`-Deterministic`) 하나뿐이고, 가변 틱·표현 갱신 주기의 무작위 분산만 고정한다.** MassAI 의 조향·시선·경로 태스크는 `FMath::RandRange` 를 직접 호출해 이 스위치를 무시한다. — `Plugins/Runtime/MassGameplay/Source/MassCommon/Private/MassCommonUtils.cpp:9-14,39-56`, `MassLOD/Public/MassLODTickRateController.h:137,143`, `Plugins/AI/MassAI/Source/MassNavigation/Private/MassSteeringProcessors.cpp:272`, `MassAIBehavior/Private/MassLookAtProcessors.cpp:363,370`.
7. **커맨드 버퍼(지연 명령)는 제출 순서가 아니라 연산 종류(Create→Add→ChangeComposition→Set→Remove/Destroy)로 그룹화된 뒤 안정 정렬로 실행된다.** 결정적이지만 "제출 순서대로 실행" 가정은 틀린다. — `Source/Runtime/MassEntity/Private/MassCommandBuffer.cpp:109-118,154-168`.
8. **시뮬레이션 LOD 와 가변 틱은 이미 있고 청크 단위로 동작한다.** `FMassSimulationLODFragment`(거리→LOD), `FMassSimulationVariableTickFragment`(엔티티별 누적 DeltaTime), `FMassSimulationVariableTickChunkFragment`(청크가 이번 프레임 틱하는지). LOD 별 `TickRates[EMassLOD::Max]` 를 설정하면 청크 단위로 틱을 건너뛴다. Viewer 는 플레이어 컨트롤러와 World Partition 스트리밍 소스에서 수집된다. — `Plugins/Runtime/MassGameplay/Source/MassLOD/Public/MassSimulationLOD.h:17-58,83-119,166-187`, `MassLOD/Private/MassLODSubsystem.cpp:130,212-224`.
9. **표현 LOD 는 `EMassRepresentationType`(HighResSpawnedActor/LowResSpawnedActor/SkinnedMeshInstance/StaticMeshInstance/None) 4단계를 LOD 별로 매핑하며, 액터 스폰은 프레임당 시간 예산(기본 1.5ms)으로 시간 분할된다.** 액터 ↔ ISM 전환 프로세서(`UMassVisualizationProcessor`)는 게임 스레드 전용이다. — `MassRepresentation/Public/MassRepresentationTypes.h:36-43`, `MassRepresentation/Public/MassRepresentationFragments.h:161-202`, `MassSimulation/Public/MassSimulationSettings.h:22-27`, `MassRepresentation/Private/MassRepresentationProcessor.cpp:638`.
10. **내비메시 경로 추종은 5.8 에서 동작한다(`MassNavMeshNavigation` 모듈, `NavCorridor` 플러그인 의존).** 그러나 StateTree 태스크 `FMassNavMeshPathFollowTask` 는 `EnterState` 에서 동기 `FindPath` 를 한 번만 하고 `Tick` 은 짧은 경로(short path)만 갱신한다. 움직이는 플레이어를 추적하려면 재경로 로직을 직접 써야 한다. — `Plugins/AI/MassAI/Source/MassAIBehavior/Private/Tasks/MassNavMeshPathFollowTask.cpp:44-75,166-195,195-230`, `MassNavMeshNavigation/MassNavMeshNavigation.Build.cs:13-25`.
11. **ZoneGraph 의존 범위: `MassNavigation`(조향·회피)은 ZoneGraph 비의존, `MassNavMeshNavigation` 도 비의존. 그러나 `MassAIBehavior`(StateTree 프로세서·태스크), `MassZoneGraphNavigation`, `MassCrowd`, 그리고 `MassMovement` 모듈까지 ZoneGraph 를 링크한다.** MassAI 플러그인 자체가 ZoneGraph/SmartObjects/StateTree/NavCorridor 플러그인을 강제 활성화한다. — `Plugins/AI/MassAI/MassAI.uplugin:59-80`, `MassAIBehavior/MassAIBehavior.Build.cs`, `Plugins/Runtime/MassGameplay/Source/MassMovement/MassMovement.Build.cs`, `Plugins/AI/MassCrowd/MassCrowd.uplugin:27-40`.
12. **Mass 엔티티와 액터를 잇는 공식 경로는 `FMassActorFragment` + `UMassActorSubsystem`(액터→핸들 맵) + `UMassAgentComponent`(액터가 자기 엔티티를 만들거나, Mass 가 만든 엔티티의 "puppet" 이 됨)이다.** Mass 소스는 GAS(GameplayAbilities) 를 전혀 참조하지 않으므로 ASC 연동은 전부 프로젝트 몫이다. — `Plugins/Runtime/MassGameplay/Source/MassActors/Public/MassActorSubsystem.h:14-59,148-164`, `MassActors/Public/MassAgentComponent.h:18-30,43-64`, GAS 참조 grep 결과 0건.
13. **Mass StateTree 는 신호(signal) 기반으로만 틱한다.** `UMassStateTreeProcessor` 는 `UMassSignalProcessorBase` 파생이며 `StateTreeActivate`, `NewStateTreeTaskRequired`, `DelayedTransitionWakeup`, `FollowPointPathDone`, `HitReceived` 등 신호를 받은 엔티티만 `Tick` 한다. 활성화 프로세서는 LOD 별 `MaxActivationsPerLOD` 상한을 둔다. 스키마는 `UMassStateTreeSchema` 로, 프로젝트가 쓰는 GameplayStateTree(StateTreeAIComponent) 트리와 호환되지 않는다. — `Plugins/AI/MassAI/Source/MassAIBehavior/Private/MassStateTreeProcessors.cpp:165-167,193-208,245,255-264,279-300,313-357`, `MassAIBehavior/Public/MassBehaviorSettings.h:21-22`, `MassAIBehavior/Public/MassStateTreeSchema.h:15-33`.
14. **5.8 에서도 세 플러그인은 `"IsExperimentalVersion": true` 다.** 다만 코어가 엔진 런타임으로 이동했다: `Engine/Source/Runtime/MassEntity` 외에 `Engine/Source/Runtime/Mass/{MassCore, MassDeveloper, MassEngine, MassSignals}` 가 새로 있고, 5.8 릴리스 노트는 "Mass 대규모 개편, Mass Signals 코어 편입, MassCore 모듈 신설, 프로세서 실행·의존성 해결 전면 개편" 을 명시한다(Production-Ready 문구는 없음). — `Plugins/Runtime/MassGameplay/MassGameplay.uplugin:15-16`, `Plugins/AI/MassAI/MassAI.uplugin:15-16`, `Plugins/AI/MassCrowd/MassCrowd.uplugin:15-16`, https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes (2026).
15. **헤드리스 커맨드렛에서 Mass 를 쓰려면: 서브시스템 생성은 `mass.RuntimeSubsystemsEnabled`(기본 true)만 검사하지만, 페이즈 틱 시작은 게임 월드의 `OnWorldBeginPlay` 에서 `StartSimulation` 이 호출될 때다.** `UMassAgentComponent` 는 커맨드렛에서 등록 자체를 거부한다. 따라서 커맨드렛 헤드리스 시뮬은 (a) 게임 월드를 만들어 BeginPlay 까지 돌리거나, (b) `FMassEntityManager` + 자체 `FMassProcessingPhaseManager` 파생으로 월드 틱 없이 수동 트리거하는 두 갈래다. — `Source/Runtime/MassEntity/Private/MassSubsystemBase.cpp:56-77`, `MassSimulation/Private/MassSimulationSubsystem.cpp:126-136,205-220`, `MassActors/Private/MassAgentComponent.cpp:99-130`.

## 상세 조사

### 1) 틱 제어: 페이즈, 실행 순서, 병렬, 수동 스텝, 커맨드렛

#### 1-1. 페이즈와 월드 틱 그룹 매핑

| 사실 | 근거 |
|---|---|
| 페이즈 열거형 `EMassProcessingPhase { PrePhysics, StartPhysics, DuringPhysics, EndPhysics, PostPhysics, FrameEnd, MAX }` | `Source/Runtime/MassEntity/Public/MassProcessingTypes.h:173-182` |
| 페이즈 → 틱 그룹: PrePhysics→TG_PrePhysics … FrameEnd→TG_LastDemotable | `Private/MassProcessingPhaseManager.cpp:44-51` |
| `FMassProcessingPhase : public FTickFunction`, `bStartWithTickEnabled=false`, 지원 틱 타입 `LEVELTICK_All | LEVELTICK_TimeOnly` | `Public/MassProcessingPhaseManager.h:54-67`, `Private/MassProcessingPhaseManager.cpp:58-63` |
| `EnableTickFunctions(const UWorld&)` 가 각 페이즈를 `RegisterTickFunction(World.PersistentLevel)` 후 `SetTickFunctionEnable(true)`; `mass.MakePrePhysicsTickFunctionHighPriority` 로 PrePhysics 우선순위 상승. `virtual` 이라 파생에서 무시 가능 | `Private/MassProcessingPhaseManager.cpp:471-500`, `Public/MassProcessingPhaseManager.h:290` |
| `Start(UWorld&)`/`Start(TSharedRef<FMassEntityManager>)` 가 EntityManager 저장 → 월드가 있으면 `EnableTickFunctions` → `bIsAllowedToTick=true` | `Private/MassProcessingPhaseManager.cpp:391-441` |
| `Pause()`/`Resume()` 은 각각 FrameEnd 끝/PrePhysics 시작에서 반영(프로세서만 안 돌고 페이즈는 계속 전이) | `Public/MassProcessingPhaseManager.h:219-240`, `Private:558-575,644-660` |

핵심 시그니처:

```cpp
// Source/Runtime/MassEntity/Public/MassProcessingPhaseManager.h:200-213
MASSENTITY_API const FGraphEventRef& TriggerPhase(const EMassProcessingPhase Phase, const float DeltaTime,
    const FGraphEventRef& MyCompletionGraphEvent, ENamedThreads::Type CurrentThread = ENamedThreads::GameThread);
MASSENTITY_API void Start(UWorld& World);
MASSENTITY_API void Start(const TSharedRef<FMassEntityManager>& InEntityManager);
MASSENTITY_API void Stop();
// Private/MassProcessingPhaseManager.cpp:378-388 — bIsAllowedToTick 이면 ProcessingPhases[Phase].ExecuteTick(DeltaTime, LEVELTICK_All, ...)
```

#### 1-2. 페이즈 한 번의 실행 흐름 (`FMassProcessingPhase::ExecuteTick`)

`Private/MassProcessingPhaseManager.cpp:65-174`:
1. `PhaseManager->OnPhaseStart(*this)` — 여기서 `mass.FullyParallel` 값에 따라 페이즈를 병렬/단일 모드로 재구성하고(`:576-586`), 동적 프로세서 추가/제거를 처리하고(`:588-591`), 새 아키타입이 생겼거나 프로세서가 바뀌었으면 의존성 그래프를 다시 푼다(`:600-641`).
2. `OnPhaseStart.Broadcast(DeltaTime)` — `UMassSimulationSubsystem::GetOnProcessingPhaseStarted(Phase)` 가 이 델리게이트를 그대로 노출한다(`MassSimulation/Private/MassSimulationSubsystem.cpp:56-59`). LOD 서브시스템의 뷰어 동기화, 에이전트 초기화, 엔티티 압축이 모두 PrePhysics 의 이 델리게이트에 붙는다.
3. 세 갈래: (a) 병렬 + ProcessingQueue(`mass.UseProcessingQueue`, 기본 true): `ProcessingQueue.Execute_GetEventRef` (`:91-119`), (b) 병렬 + 구형 TaskGraph: `UE::Mass::Executor::TriggerParallelTasks` (`:120-151`), (c) 단일 스레드: `UE::Mass::Executor::Run(*PhaseProcessor, Context)` (`:152-174`).
4. `OnPhaseEnd` 에서 지연 커맨드 `FlushCommands()` (`:644-665`).

#### 1-3. 프로세서 목록 수집과 실행 순서 결정

| 사실 | 근거 |
|---|---|
| 프로세서 후보: `GetDerivedClasses(UMassProcessor::StaticClass())` 를 역순 순회, 추상·`UMassCompositeProcessor` 제외, `ShouldAutoAddToGlobalList()`(=`bAutoRegisterWithProcessingPhases`) 인 것만 페이즈 config 에 등록. 마지막에 이름순 정렬 | `Private/MassEntitySettings.cpp:130-171` |
| `UMassProcessor` 의 순서 관련 필드: `FMassProcessorExecutionOrder ExecutionOrder{ExecuteInGroup, ExecuteBefore, ExecuteAfter}`, `ProcessingPhase`(기본 PrePhysics), `ExecutionFlags`(Standalone/Server/Client/Editor/EditorWorld 비트), `bAutoRegisterWithProcessingPhases=true`, `bRequiresGameThreadExecution=false`, `int16 ExecutionPriority=0` | `Public/MassProcessor.h:42-55,238-259,315` |
| 파이프라인 정렬은 `ExecutionPriority` 내림차순 | `Private/MassProcessingTypes.cpp:273-276` |
| 의존성 솔버: 그룹 의존을 개별 프로세서 의존으로 풀고, `MaxExecutionPriority` 내림차순 → `TotalWaitingNodes` 내림차순으로 `IndicesRemaining.Sort` 후 `PerformSolverStep` 에서 자원 접근 충돌(읽기/쓰기 요구사항) 없는 노드를 순서대로 뽑는다. 단일 스레드 타깃(`bSingleThreadTarget = !MASS_DO_PARALLEL`)이면 충돌 검사를 건너뛴다 | `Private/MassProcessorDependencySolver.cpp:809-870`, `Public/MassProcessorDependencySolver.h:245-251` |
| 관련 CVar: `mass.dependencies.ProcessorExecutionPriorityEnabled`, `mass.dependencies.PickHigherPriorityNodesRegardlessOfRequirements` | `Private/MassProcessorDependencySolver.cpp:112-127` |
| 그래프 재계산 시점: 새 아키타입 생성(`OnNewArchetype`) 또는 동적 프로세서 변경 시 해당 페이즈 시작에서 | `Private/MassProcessingPhaseManager.cpp:600-641` |

정렬의 결정성 판단: 입력 배열(이름순으로 정렬된 CDO 목록)과 비교 키(우선순위, 대기 노드 수)가 같으면 `TArray::Sort`(내부 IntroSort, 무작위 요소 없음)의 출력도 같다. 따라서 **같은 프로세서 집합 + 같은 아키타입 집합 + 같은 CVar → 같은 실행 순서** 다. 실행 순서가 바뀌는 조건은 아키타입이 새로 생기는 프레임뿐이며, 이것도 입력이 같으면 같은 프레임에 같은 방식으로 일어난다.

표준 그룹 이름(프로세서가 `ExecuteInGroup`/`ExecuteAfter` 에 쓰는 문자열): `UpdateWorldFromMass, SyncWorldToMass, Behavior, Tasks, Avoidance, ApplyForces, Movement` (`Plugins/Runtime/MassGameplay/Source/MassCommon/Public/MassCommonTypes.h:18-25`), `LODCollector, LOD` (`MassLOD/Public/MassLODTypes.h:31-32`), `Representation, Representation.VisualizationProcessing` (`MassRepresentation/Public/MassRepresentationTypes.h:27-30`). 엔진 기본 체인은 대략 `SyncWorldToMass → LODCollector → LOD → Behavior(StateTree) → Tasks(경로 추종) → Steering → Avoidance → ApplyForces → Movement → Representation` 이다(각 프로세서 생성자: `MassSteeringProcessors.cpp:62-63`, `MassAvoidanceProcessors.cpp:287-288`, `MassMovementProcessors.cpp:25-26,76-77`, `MassRepresentationProcessor.cpp:56-57`, `MassStateTreeProcessors.cpp:258-264`).

#### 1-4. 병렬 실행과 끄는 CVar (mass.* 전체 목록, TAutoConsoleVariable/FAutoConsoleVariableRef 정의 위치)

| CVar | 기본값 | 의미 | 정의 위치 |
|---|---|---|---|
| `mass.FullyParallel` | `MASS_DO_PARALLEL`(=`!UE_SERVER`) | 페이즈 내 프로세서를 태스크 그래프에 분배 | `MassEntity/Private/MassProcessingPhaseManager.cpp:30-36` |
| `mass.MakePrePhysicsTickFunctionHighPriority` | true | PrePhysics 틱 함수 고우선 | `:37` |
| `mass.UseProcessingQueue` | true | 신형 ProcessingQueue 사용 | `:38` |
| `mass.ForceInlineProcessorExecution` | true (ECVF_ReadOnly) | 청크를 프로세서 스레드에서 인라인 처리 | `Private/MassProcessingQueueTypes.cpp:26-34` |
| `mass.ProcessingQueue.FullLogging` / `.LockLogging` | false | 큐 로깅 | `:29-30` |
| `mass.AllowQueryParallelFor` | true (ECVF_Cheat) | `ParallelForEachEntityChunk` 를 실제 ParallelFor 로 | `Private/MassEntityQuery.cpp:26-32`, 적용 `:533-535` |
| `mass.RuntimeSubsystemsEnabled` | true | 게임 월드 Mass 서브시스템 자동 생성 허용(월드 로드 전 설정) | `Private/MassSubsystemBase.cpp:56-62` |
| `mass.commands.LockObserversDuringFlushing` | – | 커맨드 플러시 중 옵저버 락 | `Private/MassCommandBuffer.cpp:24-25` |
| `mass.observers.CoalesceBufferedNotifications` | – | 옵저버 알림 병합 | `Private/MassObserverManager.cpp:31-35` |
| `Mass.ConcurrentReserve.Enable` / `.MaxEntityCount` / `.EntitiesPerPage` | – | 동시 예약 저장소(5.8 에서 단일 스레드 저장소 제거 예고, Enable 은 DEPRECATED) | `Private/MassEntitySubsystem.cpp:15-35` |
| `mass.EntityBuilder.ForceDeferredCommit`, `mass.debug.ValidateEntityBuilderMakeInput` | – | 빌더 | `Private/MassEntityBuilder.cpp:18-35` |
| `mass.LogProcessingGraph` | false | 매 프레임 그래프 로그 | `Private/MassProcessor.cpp:27-30` |
| `mass.debug.*` (`DebugEntity`, `SetDebugEntityRange`, `SetFragmentBreakpoint`, `TrackRequirementsAccess` …), `mass.PrintEntityFragments`, `mass.LogArchetypes`, `mass.LogFragmentSizes`, `mass.LogMemoryUsage`, `mass.LogKnownFragments`, `mass.RecacheQueries` | – | 디버그 명령 | `Private/MassDebugger.cpp:43-46,136,165,285,311,352,365,380,394,423,459,509,551`, `Private/MassRequirementAccessDetector.cpp:13-14` |
| `mass.SimulationTickingEnabled` | true | 게임 월드 Mass 틱 허용, 바꾸면 즉시 Start/StopSimulation | `MassGameplay/Source/MassSimulation/Private/MassSimulationSubsystem.cpp:23-31,149-186` |
| `mass.EntityCompaction` | true (ECVF_Cheat) | 청크 압축 | `:22-28` |
| `mass.RandomSeedOverride` | 7 | `FApp::bUseFixedSeed` 일 때 Mass 전체 랜덤 시드 | `MassCommon/Private/MassCommonUtils.cpp:9-14` |
| `mass.lod.pause`, `mass.debug.SimulationLOD`, `mass.LODSubsystem.IncludeAllPlayerControllers` | – | LOD | `MassLOD/Private/MassLODUtils.cpp:13`, `MassSimulationLOD.cpp:65`, `MassLODSubsystem.cpp:47` |
| `mass.spawning.actorpooling`(구 `ai.mass.actorpooling`), `mass.spawning.UseGUIDToNameActors` | true / – | 액터 풀링 | `MassActors/Private/MassActorSpawnerSubsystem.cpp:24-32` |
| `mass.debug.BreakOnFailedAgentCheck` | true | 에이전트 컴포넌트 검사 | `MassActors/Private/MassAgentComponent.cpp:28` |
| `mass.debug.FreezeMovement` | – | 이동 정지 | `MassMovement/Private/MassMovementTypes.cpp:16` |
| `mass.debug.Representation*`, `Mass.CallUpdateInstances`, `ai.massrepresentation.AllowKeepActorExtraFrame` 등 | – | 표현 | `MassRepresentation/Private/MassRepresentationDebug.cpp:25-34`, `MassVisualizationComponent.cpp:33`, `MassRepresentationProcessor.cpp:27-42` |
| `ai.mass.scalability.SpawnDensityMultiplier` | – | 스폰 밀도 | `MassSpawner/Private/MassSpawner.cpp:34` |

병렬의 실제 형태: (1) 페이즈 단위 — 프로세서들이 의존성 그래프에 따라 태스크로 분배됨(`mass.FullyParallel`), `bRequiresGameThreadExecution=true` 프로세서는 게임 스레드 태스크로 분리(`Private/MassProcessingQueue.cpp:171-181,226`). (2) 쿼리 단위 — `FMassEntityQuery::ParallelForEachEntityChunk(Context, Func, Flags)` 가 아키타입/엔티티 범위마다 `FChunkJob` 을 만들어 `ParallelFor` 로 돌림(`Private/MassEntityQuery.cpp:530-600`). 엔진 프로세서 대부분은 단일 `ForEachEntityChunk` 를 쓰며(회피·조향·LOD·표현 전부), `UMassStateTreeProcessor` 만 `bProcessEntitiesInParallel`(기본 false) 옵션이 있다(`MassStateTreeProcessors.cpp:255,367`).

#### 1-5. 임의 DeltaTime 으로 한 스텝 돌리기 (코드 경로)

세 가지 수준이 있다.

(A) 프로세서/파이프라인 직접 실행 — 월드·페이즈 매니저 불필요:
```cpp
// Source/Runtime/MassEntity/Public/MassExecutor.h:19-52 (namespace UE::Mass::Executor)
MASSENTITY_API void Run(FMassRuntimePipeline& RuntimePipeline, FProcessingContext& ProcessingContext);
MASSENTITY_API void Run(UMassProcessor& Processor, FProcessingContext& ProcessingContext);
MASSENTITY_API void RunProcessorsView(TArrayView<UMassProcessor* const> Processors, FProcessingContext&, TConstArrayView<FMassArchetypeEntityCollection> = {});
// Public/MassProcessingContext.h:22
explicit FProcessingContext(FMassEntityManager& InEntityManager, const float InDeltaSeconds = 0.f, const bool bInFlushCommandBuffer = true);
```
`Run` 은 `DeltaSeconds >= 0` 을 ensure 하고 `NewProcessingScope()` 안에서 순차 실행한다(`Private/MassExecutor.cpp:66-108`). 컨텍스트 소멸 시 커맨드가 플러시된다(`bInFlushCommandBuffer`).

(B) 페이즈 매니저 수동 트리거 — 의존성 정렬·옵저버 락·커맨드 플러시까지 엔진과 동일하게 재현:
`FMassProcessingPhaseManager` 를 파생해 `Start` 에서 `EnableTickFunctions` 를 호출하지 않고 `bIsAllowedToTick=true` 만 세팅(엔진 테스트 `FMassTestProcessingPhaseManager::Start`, `Source/Developer/MassEntityTestSuite/Private/MassTestTypes.cpp:200-210`; 헤더 `Public/MassEntityTestTypes.h:648-653` 의 주석 "disable world-based ticking, even if a world is available"). 이후 매 스텝 `for Phase in 0..MAX: TriggerPhase(Phase, DeltaTime, PrevEvent)` (`MassProcessingPhasesTest.cpp:59-75`). `Initialize(*Owner, PhasesConfig)` 에는 `FMassProcessingPhaseConfig[MAX]` 를 넘기며 `UMassEntitySettings::GetProcessingPhasesConfig()` 를 그대로 써도 된다(`Public/MassProcessingPhaseManager.h:29-50`, `Public/MassEntitySettings.h:36-40`).

(C) 게임 월드 + `UMassSimulationSubsystem` — 월드 틱에 종속. 헤드리스 테스트에서 `UWorld::CreateWorld` 후 `World->Tick(LEVELTICK_All, dt)` 로 돌리면 Mass 페이즈 틱 함수가 함께 돈다(틱 타입 `LEVELTICK_All`/`TimeOnly` 만 지원, `MassEntityUtils.cpp:60-67`). 시작 조건은 `OnWorldBeginPlay` (`MassSimulationSubsystem.cpp:126-136`).

(A)/(B) 의 전제: `FMassEntityManager` 생성·초기화(`Public/MassEntityManager.h:95,152-155`; 5.8 은 `Initialize(FMassEntityManagerStorageInitParams)` + `FMassEntityManager_InitParams_Concurrent`, 단일 스레드 저장소는 폐기 예정, `WITH_MASS_CONCURRENT_RESERVE = (REQUESTED_MASS_CONCURRENT_RESERVE || WITH_EDITOR)` `Public/MassEntityManager.h:72`).

#### 1-6. 헤드리스 커맨드렛 조건

| 사실 | 근거 |
|---|---|
| 모든 Mass 서브시스템(`UMassSubsystemBase`, `UMassTickableSubsystemBase`)의 `ShouldCreateSubsystem` 은 `mass.RuntimeSubsystemsEnabled && Super::ShouldCreateSubsystem` 만 본다 — 커맨드렛 여부는 검사하지 않음 | `Private/MassSubsystemBase.cpp:69-77,131-134` |
| `UMassSimulationSubsystem::Initialize` 에서 `PhaseManager->Initialize` 는 `RebuildTickPipeline` 로, 실제 틱 시작 `StartSimulation` 은 `OnWorldBeginPlay` 에서(에디터 비게임 월드는 `PostInitialize` 에서) | `MassSimulationSubsystem.cpp:76-135,188-220` |
| 실행 플래그: `GetProcessorExecutionFlagsForWorld` 는 NetMode 로 Standalone/Server/Client, 에디터 비게임 월드는 EditorWorld. 월드가 없으면 `Editor` 또는 `All` | `Private/MassEntityUtils.cpp:16-57` |
| `UMassAgentComponent::OnRegister` 는 `IsRunningCommandlet() || IsRunningCookCommandlet() || GIsCookerLoadingPackage` 면 즉시 return, 미리보기/비게임 월드도 제외 | `MassActors/Private/MassAgentComponent.cpp:99-130` |
| `MASS_DO_PARALLEL` 은 `!UE_SERVER`; 커맨드렛은 에디터 타깃이므로 병렬 켜짐 → CVar 로 꺼야 함 | `Public/MassProcessingTypes.h:13-15` |

### 2) 시뮬레이션 LOD 와 표현 LOD

#### 2-1. 시뮬레이션 LOD / 가변 틱

| 구성 요소 | 내용 | 근거 |
|---|---|---|
| `FMassSimulationLODFragment` | `ClosestViewerDistanceSq`, `LOD`, `PrevLOD` | `MassLOD/Public/MassSimulationLOD.h:17-29` |
| `FMassSimulationVariableTickFragment` | `float DeltaTime`(마지막 실제 틱 이후 누적), `LastTickedTime` | `:32-39`, 갱신 `MassLODTickRateController.h:158-165` |
| `FMassSimulationVariableTickChunkFragment` | 청크 단위 `IsChunkHandledThisFrame(Context)`, `ShouldTickChunkThisFrame(Context)`, `GetChunkLOD(Context)` | `:42-80` |
| `FMassSimulationLODParameters`(ConstShared) | `LODDistance[Max]`, `BufferHysteresisOnDistancePercentage=10`, `LODMaxCount[Max]`, `bSetLODTags` | `:83-104` |
| `FMassSimulationVariableTickParameters` | `TickRates[Max]`, `bSpreadFirstSimulationUpdate` | `:107-120` |
| `UMassSimulationLODProcessor` | 그룹 `LOD`, `LODCollector` 이후. Viewer 정보 준비 → 거리 기반 LOD 계산 → 개수 상한 보정 → 가변 틱 갱신(`World->GetTimeSeconds()` 사용) → 태그 스왑 | `MassSimulationLOD.cpp:68-77,110-185` |
| `UMassSimulationLODTrait` | 위 파라미터를 엔티티 템플릿에 넣는 트레잇 | `MassLOD/Public/MassLODTrait.h:43-55` |
| 가변 틱 결정 로직 | 청크 LOD 가 처음 정해질 때 `TimeUntilNextTick = 결정론 ? TickRate*0.5 : Rand(0,TickRate)`, 틱한 다음엔 `TickRate` 또는 `TickRate*(1±0.1 랜덤)`; 청크 구성 변경(SerialModificationNumber)이 있으면 강제 틱 | `MassLODTickRateController.h:101-156` |
| Viewer 수집 | 플레이어 컨트롤러 + World Partition 스트리밍 소스, PrePhysics 시작 델리게이트에서 동기화 | `MassLOD/Private/MassLODSubsystem.cpp:130,212-224` |
| 헤드리스에서 Viewer 가 없으면 | `ClosestViewerDistanceSq` 가 `FLT_MAX` 로 남아 모든 엔티티가 `EMassLOD::Off` 버킷에 들어간다(Off 버킷 크기 `FLT_MAX`) | `MassLODCalculator.h:327-331,392-398` |

가변 틱을 실제로 존중하는 엔진 프로세서: `MassLODCollectorProcessor`, `MassLODDistanceCollectorProcessor`, `MassSimpleMovementTrait`, `MassNavigationProcessors`, `MassSmoothOrientationProcessors`, `MassZoneGraphNavigationProcessors`, `MassCrowdNavigationProcessor` 가 `ShouldTickChunkThisFrame/IsChunkHandledThisFrame` 를 참조한다(grep 결과). 회피·조향·경로 추종 프로세서는 "@todo: validate LOD and variable ticking" 주석 상태다(`MassNavMeshNavigationProcessors.cpp:38,94`).

#### 2-2. 표현 LOD (액터 ↔ ISM)

| 사실 | 근거 |
|---|---|
| `EMassRepresentationType { HighResSpawnedActor, LowResSpawnedActor, SkinnedMeshInstance, StaticMeshInstance, None }` | `MassRepresentation/Public/MassRepresentationTypes.h:36-43` |
| `FMassRepresentationParameters::LODRepresentation[EMassLOD::Max]` 기본 `{HighResActor, LowResActor, StaticMeshInstance, None}`, `NotVisibleUpdateRate=0.5`, `bKeepLowResActors`, `bKeepActorExtraFrame`, `bForceActorRepresentationForExternalActors` | `MassRepresentationFragments.h:161-202` |
| `FMassVisualizationLODParameters`: `BaseLODDistance{0,1000,2500,10000}`, `VisibleLODDistance{0,2000,4000,15000}`, `LODMaxCount{50,100,500,MAX}`, `DistanceToFrustum` | `:299-325` |
| `UMassRepresentationProcessor::UpdateRepresentation(Context, Params)` — 엔티티별 원하는 표현 결정 → 액터 필요하면 `GetOrSpawnActor`(스폰 요청 큐), ISM 이면 액터 비활성/해제 요청 | `MassRepresentationProcessor.cpp:72-200` |
| `UMassVisualizationProcessor` 는 `bRequiresGameThreadExecution=true`; 비가시 엔티티 갱신 주기는 `NotVisibleUpdateRate` 로 분산(결정론 모드면 고정) | `:583-590,638` |
| `UMassRepresentationSubsystem`: ISM 설명자 등록 `FindOrAddStaticMeshDesc`, 템플릿 액터 `FindOrAddTemplateActor`, `GetOrRequestSpawnActorFromTemplate`, `ReleaseTemplateActor` | `MassRepresentationSubsystem.h:42-153` |
| 액터 스폰 예산 `DesiredActorSpawningTimeSlicePerTick=0.0015s`, 파괴 `0.0005s`; 스폰 실패 재시도 5s/500cm | `MassSimulation/Public/MassSimulationSettings.h:22-48`, `MassActorSpawnerSubsystem.cpp:357-363` |
| 액터 풀링 기본 켜짐, `IMassActorPoolableInterface` 구현 액터만 | `MassActorSpawnerSubsystem.cpp:24-32,119-142` |
| ISM 인스턴스 갱신은 `AddInstancesById/RemoveInstancesById` + `MarkRenderStateDirty` | `MassVisualizationComponent.cpp:387-396,481,525` |
| 5.8 신규: 엔진 런타임 `MassEngine` 모듈에 `Mesh/MassEngineRenderISMProcessors.h`, `MassEngineRenderStaticMeshProcessors.h`, `Physics/MassEnginePhysicsFragments.h`, `AI/MassEngineNavigationProcessors.h`(Mass 엔티티를 내비게이션 시스템의 NavigationElement 로 등록) | `Source/Runtime/Mass/MassEngine/Public/*` |

### 3) 내비게이션·회피

| 사실 | 근거 |
|---|---|
| `MassNavigation` 모듈 의존: AIModule, MassCore, MassEntity, MassCommon, MassLOD, MassSignals, MassSimulation, MassSpawner, MassMovement — **ZoneGraph 없음** | `Plugins/AI/MassAI/Source/MassNavigation/MassNavigation.Build.cs:13-25` |
| 이동 목표 `FMassMoveTargetFragment { Center, Forward, DistanceToGoal, SlackRadius, DesiredSpeed(FMassInt16Real), IntentAtGoal, CreateNewAction(Action, World) }` — 액션 시작 시각을 월드 시간으로 기록 | `MassNavigation/Public/MassNavigationFragments.h:18-75` |
| 조향 `UMassSteeringProcessor`: Tasks 이후, Avoidance 이전; `TargetSelectionCooldown` 에 `FMath::RandRange` 사용 | `MassSteeringProcessors.cpp:62-63,272` |
| 회피 `UMassMovingAvoidanceProcessor`(그룹 Avoidance, LOD 이후) / `UMassStandingAvoidanceProcessor`(Moving 이후). 입력: Force, DesiredMovement, NavigationEdges, MoveTarget, Transform, Velocity, AgentRadius. 장애물은 `UMassNavigationSubsystem` 의 `THierarchicalHashGrid2D<2,4>` 격자에서 `FindCloseObstacles` 로 조회. 단일 `ForEachEntityChunk`(게임 스레드 불필요) | `MassAvoidanceProcessors.cpp:53-101,283-331,776-779,1109-1149`, `MassNavigationSubsystem.h:33-51` |
| 장애물 격자 갱신 `UMassNavigationObstacleGridProcessor` 는 "ParallelFor 불가(Move 가 스레드 안전하지 않음)" | `MassNavigationProcessors.cpp:161-195` |
| `MassNavMeshNavigation` 모듈: `UMassNavMeshPathFollowProcessor`(그룹 Tasks, Avoidance 이전), `FMassNavMeshShortPathFragment`, `FMassNavMeshCachedPathFragment{NavPath, TSharedPtr<FNavCorridor> Corridor}`; 의존 `NavCorridor` 플러그인 | `MassNavMeshNavigationProcessors.h:14-34`, `MassNavMeshNavigationFragments.h:40-114`, `MassNavMeshNavigation.Build.cs:13-25` |
| 경로 추종 태스크: `EnterState` → `RequestPath` 가 `UNavigationSystemV1` + `FPathFindingQuery` 로 **동기 `FindPath`** 후 `FNavCorridor::BuildFromPath`, `RequestShortPath`, `MoveTarget.DesiredSpeed` 설정. `Tick` 은 short path 가 끝났고 partial 이면 `UpdateShortPath` 만 호출; 목표가 움직여도 재탐색 없음. 종료 시 `FollowPointPathDone` 신호 | `Tasks/MassNavMeshPathFollowTask.cpp:29-130,150-230`, `MassNavMeshNavigationProcessors.cpp:325-327` |
| 5.8 의 내비메시 StateTree 태스크 목록: `MassNavMeshPathfollowTask`, `MassNavMeshFindReachablePointTask`, `MassNavMeshStandTask`, `MassNavMeshAnimateTask` | `MassAIBehavior/Public/Tasks/` |
| ZoneGraph 필수: `MassAIBehavior`(ZoneGraph, ZoneGraphAnnotations, SmartObjectsModule, StateTreeModule, NavCorridor …), `MassZoneGraphNavigation`, `MassCrowd`(ZoneGraph, ZoneGraphDebug, MassReplication …), 그리고 `MassMovement`(ZoneGraph, ZoneGraphAnnotations, NavigationSystem) | 각 `*.Build.cs`; `MassAI.uplugin:59-80`(플러그인 의존 MassGameplay, ZoneGraph, ZoneGraphAnnotations, SmartObjects, StateTree, NavCorridor 전부 Enabled) |
| `MassCrowd` 의 실체: ZoneGraph 레인 기반 군중(`MassCrowdNavigationProcessor`, `ZoneGraphCrowdLaneAnnotations`, 레인 폐쇄 테스트, 서버 표현·복제) | `Plugins/AI/MassCrowd/Source/MassCrowd/Public/` 파일 목록 |

전투 몬스터 적합성 판단: 조향+회피+ApplyForce+ApplyMovement 체인은 목표점(`FMassMoveTargetFragment.Center`)만 주면 ZoneGraph 없이 돌아가므로 "플레이어 위치를 매 틱 Center 로 갱신" 하는 자체 프로세서를 쓰면 추적·거리 유지가 가능하다. 내비메시 우회가 필요하면 `MassNavMeshNavigation` 의 corridor 경로를 쓰되 재경로(replan) 주기는 직접 관리해야 한다. 회피는 반경 기반 힘 모델이라 좁은 통로에서 엉킴이 있을 수 있고, 회피 프로세서가 LOD/가변 틱을 아직 존중하지 않는다(위 `@todo`).

### 4) Mass 엔티티와 액터(GAS ASC 보유 캐릭터) 병행

| 패턴 | 내용 | 근거 |
|---|---|---|
| `FMassActorFragment : FObjectWrapperFragment` | `SetAndUpdateHandleMap(MassAgent, Actor, bIsOwnedByMass)`(핸들 맵 갱신 포함), `SetNoHandleMapUpdate(...)`, `GetMutable()`, `IsOwnedByMass()`. `bIsOwnedByMass` 는 "Mass 표현이 스폰한 액터" vs "외부(복제 등)가 만든 액터" 구분 | `MassActors/Public/MassActorSubsystem.h:14-59` |
| `UMassActorSubsystem` | `TMap<TObjectKey<const AActor>, FMassEntityHandle> ActorHandleMap`; `GetEntityHandleFromActor`, `SetHandleForActor`, `RemoveHandleForActor`, `GetActorFromHandle`, `DisconnectActor` | `:148-190` |
| `UMassAgentComponent` (액터 소유 엔티티) | 상태: `EntityPendingCreation → EntityCreated`(액터가 자기 엔티티를 만듦) / `PuppetPendingInitialization → PuppetInitialized/PuppetPaused`(Mass 가 만든 엔티티의 꼭두각시 액터). `EntityConfig(FMassEntityConfig)` 로 추가 프래그먼트 지정, `GetEntityHandle()`, `KillEntity(bDestroyActor)` | `MassActors/Public/MassAgentComponent.h:18-30,43-121` |
| `UMassAgentSubsystem` | `RegisterAgentComponent` → 템플릿별 `PendingAgentEntities` 큐 → PrePhysics 시작 델리게이트에서 일괄 엔티티 생성 + `FObjectFragmentInitializerFunction`(ActorToMass 방향) 실행; puppet 은 MassToActor 방향 | `MassActors/Public/MassAgentSubsystem.h:42-141`, `Private/MassAgentSubsystem.cpp:59,298-400` |
| 번역기(Translators) | `MassCapsuleComponentTranslators`, `MassCharacterMovementTranslators`, `MassSceneComponentLocationTranslator`, `MassSceneComponentVelocityTranslator`, `MassTranslators_BehaviorTree` — 그룹 `SyncWorldToMass`/`UpdateWorldFromMass` 로 액터 컴포넌트 ↔ 프래그먼트 복사 | `MassActors/Public/Translators/` |
| 엔티티 → 액터 스폰/해제 | `UMassRepresentationProcessor::UpdateRepresentation` 이 LOD 에 따라 `RepresentationActorManagement->GetOrSpawnActor(...)`/`ReleaseActorOrCancelSpawning` 을 호출; 실제 SpawnActor 는 `UMassActorSpawnerSubsystem::ProcessPendingSpawningRequest(MaxTimeSlicePerTick)` 에서 시간 예산 안에서 우선순위 순으로 | `MassRepresentationProcessor.cpp:72-200`, `MassActorSpawnerSubsystem.cpp:357-400` |
| Mass 에서 액터 컴포넌트 호출 비용 | 프래그먼트에서 `AActor*` 를 꺼내 UObject 메서드를 부르는 순간 그 프로세서는 게임 스레드 전용(`bRequiresGameThreadExecution=true`)이어야 하고(표현 프로세서가 그 예), 캐시 지역성이 깨진다. 액터 → 엔티티 접근은 `FMassEntityView(EntityManager, Handle).GetFragmentData<T>()` 로 O(1) 무작위 접근 | `MassRepresentationProcessor.cpp:638`, `Source/Runtime/MassEntity/Public/MassEntityView.h:23-76` |
| GAS 결합 | Mass 플러그인·MassEntity 소스에 `AbilitySystem`/`GameplayAbilities` 참조 0건 | grep 결과 |

### 5) 결정론

| 항목 | 판정 | 근거 |
|---|---|---|
| 아키타입 순회 순서 | `AllArchetypes` 배열(생성 순) 인덱스 순; 쿼리의 `ValidArchetypes` 는 새 아키타입을 뒤에 `Append` 하므로 생성 순 유지 | `MassEntityManager.cpp:2431-2435`, `MassEntityQuery.cpp:176-200` |
| 청크 순회 순서 | `Chunks[ChunkIndex]` 오름차순; 청크 내 엔티티는 배열 순 | `MassArchetypeData.cpp:841-867` |
| 엔티티 인덱스 배정 | 단일 스레드 저장소: `EntityFreeIndexList.Pop()` 아니면 `Entities.Add()`; 동시 저장소: `FreeListMutex` 잠금 후 `Pop`. 게임 스레드에서 같은 순서로 생성/파괴하면 같은 인덱스 | `MassEntityManagerStorage.cpp:120-154,449-500` |
| 엔티티 압축(compaction) | **비결정적**: `FPlatformTime::Seconds()` 예산으로 `SortedChunksBySharedValues` 를 정렬해 엔티티를 옮김. PrePhysics 시작마다 실행. 끄는 법: `mass.EntityCompaction 0` / `bEntityCompactionEnabled=false` / `DesiredEntityCompactionTimeSlicePerTick=0` | `MassEntityManager.cpp:586-612`, `MassArchetypeData.cpp:929-962`, `MassSimulationSubsystem.cpp:263-282`, `MassSimulationSettings.h:35-40` |
| 커맨드 버퍼 플러시 순서 | 연산 타입 그룹(Create0 → Add2 → ChangeComposition3 → Set4 → Remove/Destroy6) 으로 `StableSort` → 결정적이나 제출 순서와 다름 | `MassCommandBuffer.cpp:109-118,154-168` |
| 병렬 실행 | 태스크 스케줄링 시각은 비결정적이지만 솔버가 읽기/쓰기 충돌 노드를 동시에 돌리지 않으므로 데이터 결과는 순차 실행과 같아야 한다(엔진 설계 의도). 부동소수 누적 순서가 바뀌는 병렬 축약(reduction)은 엔진 프로세서에 없음(모두 엔티티 단위 독립 계산). 확실히 하려면 `mass.FullyParallel 0`, `mass.AllowQueryParallelFor 0` | `MassProcessorDependencySolver.cpp:471-500`, `MassProcessingPhaseManager.cpp:36`, `MassEntityQuery.cpp:31` |
| Mass 자체 랜덤 | `UE::Mass::Utils::IsDeterministic() == FApp::bUseFixedSeed`(비Shipping), `GenerateRandomSeed()` 는 결정론 모드면 `mass.RandomSeedOverride`(7). 가변 틱·표현 갱신 분산은 이 스위치를 존중 | `MassCommonUtils.h:25-52`, `MassCommonUtils.cpp:9-56`, `MassLODTickRateController.h:137,143`, `MassRepresentationProcessor.cpp:583-590` |
| 시드 기반 결정적 랜덤 유틸 | `UE::RandomSequence::RandRange(SeqIndex, Min, Max)` — 엔티티 인덱스를 시퀀스 키로 쓰는 무상태 랜덤(속도 분산 `FMassMovementParameters::GenerateDesiredSpeed` 가 사용) | `MassCommon/Public/RandomSequence.h:11-83`, `MassMovementFragments.h:121` |
| 스위치를 무시하는 랜덤 | `MassSteeringProcessors.cpp:272`(FMath::RandRange), `MassLookAtProcessors.cpp:363,370`, `MassZoneGraphAnnotationFragments.cpp:35,46`, `Tasks/MassFindSmartObjectTask.cpp:194`, `MassZoneGraphFindEscapeTarget.cpp:88`, `MassZoneGraphPathFollowTask.cpp:92`; 스포너는 `FRandomStream`(시드 지정 가능) `MassSpawnLocationProcessor.h:24` | 해당 줄 |
| 시간원 | 가변 틱·StateTree delta·MoveTarget 액션 시각은 `World->GetTimeSeconds()` 의존 → 고정 DeltaTime 으로 월드를 틱하면 결정적, 월드 없는 (A)/(B) 경로에서는 이 프로세서들이 `check(World)` 로 실패 | `MassSimulationLOD.cpp:163-166`, `MassStateTreeProcessors.cpp:320`, `MassNavigationFragments.h:25` |
| 액터 스폰 시간 분할 | `FPlatformTime` 예산 → 액터 등장 프레임 비결정. 시뮬 결과에 영향 없게 하려면 표현 트레잇 자체를 제외 | `MassActorSpawnerSubsystem.cpp:357-363` |
| 신호(signal) 처리 | `SignalEntities` 는 즉시 델리게이트 브로드캐스트 → 각 `UMassSignalProcessorBase` 가 프레임 버퍼에 쌓고 다음 `Execute` 에서 처리; 지연 신호는 `UMassSignalSubsystem::Tick` 에서 누적 시간으로 발화 | `Source/Runtime/Mass/MassSignals/Private/MassSignalSubsystem.cpp:31-82,105-110`, `MassSignalProcessorBase.cpp:39-60,145-155` |

### 6) 프로덕션 승격 여부 (5.7/5.8)

| 사실 | 근거 |
|---|---|
| `MassGameplay.uplugin`, `MassAI.uplugin`, `MassCrowd.uplugin` 모두 `"VersionName": "0.4"`, `"IsBetaVersion": false`, `"IsExperimentalVersion": true` | 각 uplugin `:3-4,15-16` |
| 엔진 런타임으로 이동한 모듈: `Source/Runtime/MassEntity`, `Source/Runtime/Mass/MassCore`(EntityHandle/Fragments/ArchetypeGroup 등 코어 타입), `Mass/MassSignals`(구 MassGameplay 플러그인 모듈), `Mass/MassEngine`(AI 내비 요소·ISM 렌더·물리 프래그먼트), `Mass/MassDeveloper`(Trace) | 디렉터리 목록 |
| `MassEntityTestSuite` 는 `Source/Developer` 에 위치(엔진 자체 단위 테스트 36개 파일, `MassMultiThreadTest`, `MassParallelQueryTest`, `MassDependencyTest` 포함) | `Source/Developer/MassEntityTestSuite/Private/` |
| 5.8 릴리스 노트 Framework 절: "Mass, our data-oriented entity system, gets a major overhaul", "Mass Signals are now part of the core engine, and entities can be created off the game thread thanks to archetype-based, lock-free scheduling", "A new sparse/virtual fragment system…", "We completely overhauled Mass processor execution and dependency resolution…", "A new MassCore module…". Experimental/Beta/Production-Ready 라벨 없음. MetaHuman Crowd 플러그인은 Experimental | https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes (2026) |
| 공식 문서의 MassAI 플러그인 페이지는 Experimental 로 표기(검색 결과 요약; 페이지 본문 직접 확인은 미실시) | https://dev.epicgames.com/documentation/en-us/unreal-engine/API/PluginIndex/MassAI |
| 5.7 릴리스 노트에서 Mass 승격 문구는 검색으로 찾지 못함(PCG·Substrate 만 Production-Ready 언급) | https://www.unrealengine.com/news/unreal-engine-5-7-is-now-available (2025) — 미확인 |
| 5.8 코드의 폐기 예고: 단일 스레드 엔티티 저장소 제거 예정(`UE_DEPRECATED(5.8, ...)`), `Mass.ConcurrentReserve.Enable` DEPRECATED | `MassEntityManager.h:152`, `MassEntitySubsystem.cpp:19-24` |

종합: 플러그인 메타데이터는 여전히 Experimental 이지만, 코어(MassEntity/MassCore/MassSignals/MassEngine)가 엔진 런타임에 편입되고 5.8 에서 실행기·의존성 해결이 재작성된 상태다. "코어는 안정화 중, 게임플레이 스택(MassAI/MassCrowd)은 실험적" 으로 읽는 것이 코드와 부합한다. API 변동은 크다(5.6 에서 `ForEachEntityChunk` 시그니처 변경, 5.8 에서 저장소 초기화 변경 등 `UE_DEPRECATED(5.6/5.7/5.8)` 다수).

## 프로젝트 적용 시사점 (TDGame)

현재 상태: `TDGame.uproject` 는 GameplayAbilities, StateTree, GameplayStateTree, ModelContextProtocol 만 켜져 있고 `Source/` 에 Mass 참조가 없다(`TDGame.Build.cs:11-26`, grep 0건). Mass 를 쓰려면 `MassGameplay` 플러그인 활성화 + `MassEntity`, `MassCommon`, `MassSimulation`, `MassLOD`, `MassMovement`, `MassActors`, `MassRepresentation`, `MassSpawner`, `MassSignals` 모듈 의존 추가가 필요하고, `MassGameplay` 는 ZoneGraph·PoseSearch·SmartObjects·StateTree·DataValidation 플러그인을 자동 활성화한다(`MassGameplay.uplugin:88-109`).

### 쓸 것

1. **대량 몬스터의 "경량 상태" 저장소로서 MassEntity(아키타입 SoA 저장 + 쿼리 + 커맨드 버퍼)**. 몬스터 수백 마리의 위치·속도·체력·타깃·쿨다운을 프래그먼트로 두고 C++ 프로세서로 처리하면 액터 틱 오버헤드가 없다. 프로세서는 순수 C++ 클래스이고 순서·그룹이 코드에 문자열로 적혀 있어 생성형 AI 가 읽고 고치기 쉽다(요구 1·4 충족).
2. **고정 스텝 시뮬레이터는 (B) 경로 — `FMassProcessingPhaseManager` 파생 + `TriggerPhase` 수동 호출.** 엔진 테스트가 같은 방식을 쓰므로 5.8 API 와 어긋나지 않는다. `FTDScopedCombatWorld` 가 이미 `UWorld::CreateWorld` + 고정 `World->Tick` 을 쓰므로, (C) 경로(게임 월드 + `UMassSimulationSubsystem`)도 가능하지만 `OnWorldBeginPlay` 를 호출해 `StartSimulation` 이 돌게 해야 한다(`MassSimulationSubsystem.cpp:126-136`).
3. **결정론 체크리스트(시뮬 시작 시 강제)**: `mass.EntityCompaction 0`, `mass.FullyParallel 0`, `mass.AllowQueryParallelFor 0`, `FApp::bUseFixedSeed=true`(또는 `-FixedSeed`), 프로젝트 프로세서의 모든 랜덤은 `UE::RandomSequence::*` 또는 시드 지정 `FRandomStream` 만 사용, 표현·액터 스폰 트레잇은 시뮬 템플릿에서 제외. 이 다섯 가지를 지키면 코드에서 확인한 비결정 원인은 모두 제거된다.
4. **청크 단위 가변 틱 패턴(`FMassSimulationVariableTickChunkFragment` + LOD 태그 스왑)** 을 "전투 활성/휴면 몬스터" 구분에 재사용. 플레이어 거리 기준 `LODDistance` 와 `TickRates` 만 데이터로 두면 "몇 마리를 어떤 주기로" 가 설정값이 된다(요구 3). Viewer 는 `UMassLODSubsystem::RegisterActorViewer` 로 플레이어 폰을 직접 등록할 수 있다(`MassLODSubsystem.h:112-113`).
5. **StateTree 를 Mass 에서 쓰려면 `UMassStateTreeSchema` 트리를 따로 만든다.** 신호 기반 틱은 "이벤트가 없으면 안 돈다" 는 점에서 대량 몬스터에 유리하지만, 전투처럼 매 틱 조건을 보는 트리는 `NewStateTreeTaskRequired`/`DelayedTransitionWakeup` 을 주기적으로 쏘는 프로젝트 프로세서가 필요하다. 프로젝트의 GameplayStateTree 에셋은 재사용할 수 없다.

### 피할 것

1. **Mass 이동 스택(조향·회피·내비메시 corridor)을 그대로 밸런스 시뮬레이터에 넣는 것.** 랜덤 직접 호출, 월드 시간 의존, 내비메시 필요, 재경로 미지원 때문에 결정론과 헤드리스 요구를 동시에 만족시키기 어렵다. 시뮬레이터용 이동은 "직선 접근 + 반경 정지" 정도의 자체 프로세서로 두고, 실제 게임에서만 Mass 조향·회피를 얹는 이중 구성이 안전하다.
2. **몬스터 한 마리마다 `UMassAgentComponent` 를 달아 액터 주도로 엔티티를 만드는 구성.** 커맨드렛에서 등록이 거부되고(`MassAgentComponent.cpp:103`), 액터 수만큼 오브젝트 비용이 그대로 남는다. 대신 Mass 가 엔티티를 만들고 근접·전투 중인 소수만 `UMassRepresentationProcessor` 패턴으로 `ATDMonsterCharacter` 를 스폰(puppet)하도록 뒤집는 편이 맞다.
3. **GAS(ASC) 를 Mass 프래그먼트와 매 틱 동기화하는 것.** ASC 는 UObject·태그·어트리뷰트 이벤트 중심이라 게임 스레드 전용이 되고 Mass 의 장점(캐시·병렬)이 사라진다. 원거리·휴면 몬스터는 ASC 없이 프래그먼트 수치(체력·상태 비트)만 유지하고, 액터로 승격될 때 `UTDCombatAttributeSet` 에 값을 옮기는 "승격/강등 변환기" 를 두는 것이 Mass 의 puppet 초기화 함수(`FObjectFragmentInitializerFunction`, MassToActor/ActorToMass 방향) 와 같은 모양이다.
4. **MassCrowd·MassZoneGraphNavigation·MassAIBehavior 의존.** 셋 다 ZoneGraph 를 끌어오고 군중(레인) 전제라 탑다운 전투 몬스터에 맞지 않는다. 필요한 것은 `MassEntity` + `MassCommon/LOD/Simulation/Movement/Navigation(MassAI 의 조향·회피 모듈만)` 이다. 단, `MassMovement` 도 ZoneGraph 를 링크하므로 플러그인 활성화는 피할 수 없다(코드 의존은 없음).
5. **커맨드 버퍼 제출 순서에 기대는 로직**(예: "먼저 Set 하고 나중에 Destroy" 를 같은 프레임에 제출). 타입별 그룹 실행이라 의도와 다르게 돈다.

### 대안 비교 메모

- Mass 를 안 쓰고 자체 SoA 배열 + 고정 스텝 루프를 만들면 결정론·헤드리스·토큰(코드 크기) 면에서 가장 단순하다. Mass 가 주는 것은 아키타입 관리, 옵저버, 병렬 실행기, LOD/표현 파이프라인이다. 마릿수가 수백 이하이고 표현 전환(ISM)이 필요 없다면 자체 구현이 더 작다. 수천 마리 + ISM 전환이 목표면 Mass 의 표현·LOD 파이프라인을 재구현하는 비용이 더 크다.
- 두 경우 모두 "시뮬 코어는 월드 시간·랜덤·액터에 의존하지 않는다" 는 규칙을 세우면 밸런스 툴과 실제 게임이 같은 코드를 쓸 수 있다. Mass 를 쓸 때는 (B) 경로가 그 규칙을 지키는 실행 방식이다.

## 미확인·미해결 질문

1. **병렬 모드에서 실제로 동일 입력→동일 결과가 나오는지 실측하지 않았다.** 솔버의 자원 충돌 회피 설계상 그래야 하지만, 관찰자(observer)·커맨드 병합(`mass.observers.CoalesceBufferedNotifications`)이 병렬 경로에서 순서를 바꾸는지는 코드로 완전히 추적하지 못했다. 시뮬레이터는 우선 단일 스레드 CVar 로 고정하고, 병렬은 나중에 A/B 검증한다.
2. **커맨드렛에서 `UMassSimulationSubsystem` 이 실제로 생성되고 `OnWorldBeginPlay` 가 호출되는지** 는 코드 조건만 확인했고 실행해 보지 않았다. `FTDScopedCombatWorld` 가 `BeginPlay` 를 돌리는지 확인이 필요하다.
3. **`MassEngine` 모듈(5.8 신규, `Source/Runtime/Mass/MassEngine`)의 ISM 렌더 프로세서·물리 프래그먼트가 기존 `MassRepresentation` 을 대체하는 흐름인지** 헤더 목록만 봤다. 표현 파이프라인을 채택하기 전에 어느 쪽이 유지될지 확인해야 한다.
4. **ISM 인스턴스 갱신 비용(마릿수별 ms)** 은 실측이 없다. `Mass.CallUpdateInstances` 토글이 있는 것으로 보아 두 경로의 성능 차가 있다.
5. **5.7 릴리스 노트의 Mass 관련 문구**는 찾지 못했다(미확인). 5.8 노트에도 Production-Ready 라벨은 없다.
6. **`FMassNavMeshPathFollowTask` 로 움직이는 목표를 추적할 때의 재경로 전략**(재-EnterState 주기, corridor 재구축 비용)은 코드에 없어 프로젝트에서 설계해야 한다.
7. **`MASS_DO_PARALLEL` 를 프로젝트 타깃에서 바꾸는 방법**은 설치형 엔진에서는 없다(엔진 모듈이 미리 컴파일됨). 소스 빌드 엔진으로 옮길 계획이 아니라면 CVar 제어만 전제한다.
8. **StateTree 신호 기반 틱과 전투 반응성의 궁합**(피격 즉시 반응이 다음 프레임 `Execute` 까지 한 프레임 지연되는지)은 `FrameReceivedSignals` 이중 버퍼 코드상 "같은 페이즈 내 이후 프로세서 또는 다음 프레임" 으로 보이나 정확한 지연 프레임 수는 실측이 필요하다.
