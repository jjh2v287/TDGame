# 스테이트 트리 런타임 구조, 코드 구동, 유틸리티 선택, Mass 연동, 5.8 신기능

조사 대상: 언리얼 엔진 5.8 엔진 소스 (읽기 전용)
- `E/` = `C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Runtime/StateTree/Source`
- `G/` = `C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule`
- `M/` = `C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/AI/MassAI/Source/MassAIBehavior`
- `S/` = `C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Mass/MassSignals`
- `P/` = `C:/Project/TDGame`

모든 사실은 "파일:줄" 근거를 붙였다. 근거를 찾지 못한 항목은 "미확인"으로 표기했다.

---

## 결론 요약

1. **스테이트 트리는 컴포넌트 없이 순수 C++ 로 구동할 수 있다.** `FStateTreeExecutionContext(TNotNull<UObject*> Owner, TNotNull<const UStateTree*> StateTree, FStateTreeInstanceData& InInstanceData, ...)` 를 스택에 만들고 `Start()` / `Tick(const float DeltaTime)` / `Stop()` 을 아무 시점에 아무 DeltaTime 으로 호출하면 된다. 인스턴스 데이터(`FStateTreeInstanceData`)는 호출자가 소유하며 컨텍스트는 참조만 잡는다. (E/StateTreeModule/Public/StateTreeExecutionContext.h:331-336, 480-517; 엔진 자체 테스트가 정확히 이 방식으로 실행함: E/StateTreeTestSuite/Private/StateTreeTest.cpp:97-110)

2. **틱 분리 호출이 가능하다.** `TickUpdateTasks(DeltaTime)` 와 `TickTriggerTransitions()` 로 "태스크 갱신"과 "전이 판정"을 나눠 호출할 수 있다. (StateTreeExecutionContext.h:517-524, .cpp:1833-1871) 결정론 시뮬레이션에서 고정 스텝으로 반복 호출하기에 적합하다.

3. **난수원은 인스턴스마다 있는 `FRandomStream` 이며 시드를 지정할 수 있다.** `FStateTreeExecutionState::RandomStream` (StateTreeExecutionTypes.h:1207) 을 `Start(FStartParameters{ .RandomSeed = N })` 로 초기화한다. 시드를 주지 않으면 `FPlatformTime::Cycles()` 가 시드가 되어 비결정적이다. (StateTreeExecutionContext.cpp:1513) 랜덤 자식 선택(.cpp:7983), 유틸리티 가중 랜덤(.cpp:8119), 전이 지연 랜덤(StateTreeTypes.h:574-578) 모두 이 스트림만 사용하고 `FMath::FRand` 는 쓰지 않는다. **따라서 시드만 고정하면 스테이트 트리 자체는 결정론적이다.**

4. **상태 선택은 6가지 행동(`EStateTreeStateSelectionBehavior`)** 중 하나: None / TryEnterState / TrySelectChildrenInOrder / TrySelectChildrenAtRandom / TrySelectChildrenWithHighestUtility / TrySelectChildrenAtRandomWeightedByUtility / TryFollowTransitions (StateTreeTypes.h:172-198). "최고 유틸리티"는 난수 없이 최고 점수를 고르고(.cpp:8040-8046), "유틸리티 가중 랜덤"은 점수 비례 룰렛이다(.cpp:8119-8130). 점수는 `FStateTreeConsiderationBase::GetScore` 를 0~1 로 클램프한 값 × 상태 `Weight` 다. (StateTreeConsiderationBase.cpp:14-17, StateTreeState.h:474-476)

5. **"틱 없이 잠들고 이벤트로 깨우는" 기능은 5.6 부터 있고 5.8 에도 있다.** 실행 컨텍스트가 `GetNextScheduledTick()` 으로 Sleep / EveryFrame / NextFrame / CustomTickRate 를 계산하고(.cpp:409-569), `UStateTreeComponent::ScheduleTickFrame` 이 이를 `SetComponentTickEnabled(false)` 또는 `SetComponentTickIntervalAndCooldown(rate)` 로 옮긴다. (G/Private/Components/StateTreeComponent.cpp:298-346) 이벤트·전이 요청·딜리게이트가 오면 `FStateTreeExecutionExtension::ScheduleNextTick` 을 통해 컴포넌트가 다시 깨어난다. (StateTreeExecutionContext.cpp:945-954, StateTreeComponent.cpp:30-36) 다만 스키마가 `IsScheduledTickAllowed()` 를 true 로 돌려줘야 하고 기본 `UStateTreeSchema` 는 false 다. (E/StateTreeModule/Public/StateTreeSchema.h:54-57, G/.../StateTreeComponentSchema.cpp:58-70)

6. **에셋의 런타임 데이터는 에디터 전용 `UStateTreeEditorData` 를 컴파일한 결과이며, 런타임 모듈에는 트리를 조립하는 공개 API 가 없다.** `UStateTree::States/Transitions/Nodes/PropertyBindings` 는 모두 `private` 이고 `FStateTreeCompiler` 가 friend 로 채운다. (E/StateTreeModule/Public/StateTree.h:445-493, 684) 조립 API(`AddSubTree / AddChildState / AddTask<T> / AddTransition / AddEnterCondition<T> / AddConsideration<T>`)와 컴파일러(`FStateTreeCompiler::Compile`)는 **StateTreeEditorModule** 에 있고 이 모듈은 uplugin 에서 `UncookedOnly` 다. (E/StateTreeEditorModule/Public/StateTreeEditorData.h:272-345, StateTreeState.h:282-342, StateTreeCompiler.h:48-67, `StateTree.uplugin` Modules 항목) → **에디터 타깃(자동화 테스트·커맨드렛)에서는 C++ 로 트리를 만들고 컴파일해 바로 실행할 수 있지만, 패키징된 게임에서는 불가능하다.**

7. **Mass 연동은 "한 프로세서가 시그널을 받은 엔티티 청크만 순회하며 각 엔티티마다 실행 컨텍스트를 만들어 Tick" 하는 구조다.** `UMassStateTreeProcessor::SignalEntities` 가 청크마다 `ForEachEntityInChunk` 로 `FMassStateTreeExecutionContext` 를 만들고 `Tick(AdjustedDeltaTime)` 을 호출한다. (M/Private/MassStateTreeProcessors.cpp:32-63, 313-407) 신호가 없는 프레임은 아예 실행되지 않는다(S/Private/MassSignalProcessorBase.cpp:55-58). `bProcessEntitiesInParallel`(기본 false) 로 청크 병렬 실행 가능. (M/Public/MassStateTreeProcessors.h:111) Mass 스키마는 `IsScheduledTickAllowed` 를 재정의하지 않고(검색 결과 0건) 지연 전이는 `DelaySignalEntityDeferred` 시그널로 깨운다. (M/Private/MassStateTreeExecutionContext.cpp:185-191)

8. **5.6 → 5.8 변화 요약(코드 근거):** 5.6 = 스케줄 틱/슬립, 딜리게이트, 태스크 완료 상태 재설계(`UE_DEPRECATED(5.6` 67건); 5.7 = 상태 선택 규칙(`EStateTreeStateSelectionRules`, "Previous (UE 5.6) rules" 주석), 선택 결과 재설계(`FSelectStateResult`), 컴파일러 매니저(`UE_DEPRECATED(5.7, "Use the compiler manager.")`) (71건); 5.8 = `FStartParameters`(시작 상태 태그 지정·전역 파라미터·시드), `EComparisonOperator` 통합, `UStateTreeEditingSubsystem::CompileStateTree`, PropertyRef 비동기 접근 정리 (24건). (E/ 전체 grep 집계; StateTreeExecutionContext.h:440-495; StateTreeTypes.h:110-122)

---

## 상세 조사

### 1) 실행 컨텍스트를 코드에서 직접 만들고 임의 시점·임의 DeltaTime 으로 구동

| 사실 | 근거 |
|---|---|
| 생성자: `FStateTreeExecutionContext(TNotNull<UObject*> Owner, TNotNull<const UStateTree*> StateTree, FStateTreeInstanceData& InInstanceData, const FOnCollectStateTreeExternalData& = {}, EStateTreeRecordTransitions = No)` | StateTreeExecutionContext.h:333 |
| 복사 금지, 스택 임시 객체 전제 ("The context is meant to be temporary, you should not store a context across multiple frames") | StateTreeExecutionContext.h:277-279, 341-342 |
| `IsValid()` = `RootStateTree.IsReadyToRun()` 만 검사 (월드·컴포넌트 불필요) | StateTreeExecutionContext.h:89-92; StateTree.cpp:48-60 |
| `Start()`, `Start(FConstStructView InitialGlobalParameters)`, `Start(FStartParameters)` | StateTreeExecutionContext.h:480-495 |
| `Stop(EStateTreeRunStatus CompletionStatus = Stopped)` | :502 |
| `Tick(const float DeltaTime)` / `TickUpdateTasks(DeltaTime)` / `TickTriggerTransitions()` | :509-524 |
| Tick 은 `TickPrelude → TickUpdateTasksInternal → TickTriggerTransitionsInternal → TickPostlude`; DeltaTime 은 0 이상으로 클램프됨 | .cpp:1812-1831, 1884 |
| 재진입 방지: `Exec.CurrentPhase != Unset` 이면 ensure 실패 후 Failed 반환 | .cpp:1480-1484 (Start), 1778-1782 (Tick) |
| Start 는 `InstanceData.Reset()` 후 실행 상태를 새로 만듦(이전 상태 완전 폐기) | .cpp:1488 |
| 인스턴스 데이터 외부 소유: 컨텍스트 멤버는 `FStateTreeInstanceData& InstanceData` 참조 | StateTreeExecutionContext.h:1554 |
| `FStateTreeInstanceData` 는 값 타입(복사·이동·대입·`CopyFrom`·`Serialize` 지원) | StateTreeInstanceData.h:458-504, 596-598 |
| 구조체 멤버로 둘 경우 `AddStructReferencedObjects` 를 직접 호출해야 GC 안전 | StateTreeInstanceData.h:453 |
| 외부 데이터 주입: `SetContextData(Handle, DataView)`, `SetContextDataByName`, `SetCollectExternalDataCallback` | StateTreeExecutionContext.h:643, 340-346 |

엔진 테스트의 실제 사용 예 (E/StateTreeTestSuite/Private/StateTreeTest.cpp:97-110):

```cpp
FStateTreeInstanceData InstanceData;
FTestStateTreeExecutionContext Exec(StateTree, StateTree, InstanceData); // Owner=StateTree 자체
Exec.IsValid();
Status = Exec.Start();
Status = Exec.Tick(0.1f);
Exec.Stop();
```

**이벤트 큐(SendEvent) 동작**

| 사실 | 근거 |
|---|---|
| `FStateTreeMinimalExecutionContext::SendEvent(FGameplayTag Tag, FConstStructView Payload, FName Origin)` — 최소 컨텍스트(Owner, StateTree, InstanceData)만으로 호출 가능 | StateTreeExecutionContext.h:231-232, 255; .cpp:918-940 |
| 큐 상한 `MaxActiveEvents = 64`, 초과 시 오류 로그 후 드롭 | StateTreeEvents.h:181; StateTreeEvents.cpp:45-48 |
| 이벤트는 전이 처리 단계가 끝나면 `ClearEventsForCurrentTransitionProcessingPhase()` 로 일괄 제거 (다음 전이 처리까지 보류 플래그가 있는 것만 남김) | StateTreeExecutionContext.cpp:5773-5777; StateTreeEvents.cpp:69-81 |
| 전이가 이벤트를 소비하면 `ConsumeEvent` 로 즉시 제거(`bConsumeEventOnSelect`) | .cpp:2168, 6124-6126 |
| 이벤트 큐를 여러 인스턴스가 공유 가능: `FStartParameters::SharedEventQueue`, `FStateTreeInstanceData::SetSharedEventQueue` | StateTreeExecutionContext.h:449; StateTreeInstanceData.h:569 |
| SendEvent 는 성공 시 `ScheduleNextTick(ETickReason::Event)` 로 소유자에게 깨우기 요청 | .cpp:936-938 |

즉 이벤트는 "다음 Tick 의 전이 판정 한 번" 동안만 살아 있는 소비형 큐다. 시뮬레이션에서 `SendEvent → Tick` 순서를 지키면 결정론이 유지된다.

### 2) UStateTreeComponent / UStateTreeAIComponent 의 틱

| 사실 | 근거 |
|---|---|
| `UStateTreeComponent : UBrainComponent`, `bStartLogicAutomatically = true` (UPROPERTY EditAnywhere), `SetStartLogicAutomatically(bool)` | G/Public/Components/StateTreeComponent.h:39, 186-187, 113 |
| 생성자에서 `PrimaryComponentTick.bStartWithTickEnabled = false` (시작 전엔 틱 안 함) | StateTreeComponent.cpp:41-48 |
| `BeginPlay` → `StartLogic` → `StartTree` : 매번 스택에 `FStateTreeExecutionContext` 를 만들고 `Start(FStartParameters{ .InitialGlobalParameters, .ExecutionExtension = FStateTreeComponentExecutionExtension })` 호출 후 `ScheduleTickFrame(Context.GetNextScheduledTick())` | .cpp:95-102, 172-212 |
| `TickComponent` : 컨텍스트 생성 → `SetContextRequirements` → `Context.Tick(DeltaTime)` → `ScheduleTickFrame(Context.GetNextScheduledTick())` | .cpp:112-158 |
| `ScheduleTickFrame` : Sleep 이면 `SetComponentTickEnabled(false)`; EveryFrame 이면 `SetComponentTickIntervalAndCooldown(0)`; CustomTickRate 면 `SetComponentTickIntervalAndCooldown(rate)`; NextFrame 이면 `UE_KINDA_SMALL_NUMBER` 간격 | .cpp:298-346 |
| 깨우기 경로: `FStateTreeComponentExecutionExtension::ScheduleNextTick` → `Component->ConditionalEnableTick()` → `ScheduleTickFrame(MakeNextFrame())` | .cpp:30-36, 348-352 |
| 컴포넌트 밖에서 이벤트 전송: `SendStateTreeEvent` 는 `FStateTreeMinimalExecutionContext` 만 만들어 `SendEvent` (틱 중이 아니어도 됨) | .cpp:577-591 |
| 콘솔 변수 `StateTree.Component.ScheduledTickEnabled` (기본 true) — false 면 항상 매 프레임 틱 | .cpp:18-24, 302-310 |
| 콘솔 변수 `StateTree.Component.DefaultScheduledTickAllowed` (기본 true) — 스키마 `ScheduledTickPolicy = Default` 일 때 사용 | G/Private/Components/StateTreeComponentSchema.cpp:24-28, 58-70 |
| 스키마 정책 열거형 `EStateTreeComponentSchemaScheduledTickPolicy { Default, Allowed, Denied }` | StateTreeComponentSchema.h:18-23, 89 |
| `UStateTreeAIComponent` 는 스키마만 `UStateTreeAIComponentSchema` 로 바꿈 (컨텍스트 데이터 = Actor(폰) + AIController) | G/Public/Components/StateTreeAIComponent.h:16-21; StateTreeAIComponentSchema.cpp:16-26 |

**스케줄 틱 결정 로직** (`FStateTreeReadOnlyExecutionContext::GetNextScheduledTick`, StateTreeExecutionContext.cpp:409-569) 우선순위:

1. 트리 미실행 → Sleep (:419-422)
2. 활성 프레임 중 하나라도 스키마가 스케줄 불허 → `EveryFrames(Forced)` (:426-433)
3. 활성 상태의 `bHasCustomTickRate` 중 최소값 수집; 없으면 태스크 `DoesRequestTickTasks(bHasEvents)` / 전이 `ShouldTickTransitions(...)` 로 매 프레임 필요 여부 판단 (:480-504)
4. `AddScheduledTickRequest` 로 태스크가 넣은 요청 병합 (:522-533)
5. 전이 요청 있음 → NextFrame; 이벤트 있음 → NextFrame; 완료된 상태 대기 → NextFrame (:536-552)
6. 지연 전이 남은 시간 → CustomTickRate 후보 (:554-561)
7. 후보 있으면 `MakeCustomTickRate`, 없으면 `MakeSleep` (:566-569)

태스크가 스케줄에 참여하는 플래그: `bShouldCallTick`(기본 true), `bShouldCallTickOnlyOnEvents`(false), `bShouldAffectTransitions`(false), `bConsideredForScheduling`(true). (E/StateTreeModule/Public/StateTreeTaskBase.h:24-34, 113-133) 상태 단위 주기: `UStateTreeState::CustomTickRate / bHasCustomTickRate` — "If set all the other states (children or parents) will also tick at that rate. ... smallest custom tick rate wins." (E/StateTreeEditorModule/Public/StateTreeState.h:439-450)

**프레임당 비용 관찰(코드 구조 기준, 수치는 미측정):** 컴포넌트 틱 한 번 = 실행 컨텍스트 스택 생성 + `SetContextRequirements`(컨텍스트 데이터 뷰 배열 세팅) + `CollectActiveExternalData`(.cpp:1763) + 활성 프레임별 전역 태스크/평가기/상태 태스크 Tick + 전이 조건 평가 + 이벤트 정리. 태스크 Tick 이 모두 `bShouldCallTick=false` 이고 전이가 이벤트/딜리게이트 기반이면 트리는 Sleep 으로 가서 컴포넌트 틱 자체가 꺼진다. 즉 "많은 몬스터가 대기 상태" 인 경우 비용은 0 에 가깝고, 전투 중 매 프레임 폴링하는 태스크가 있으면 액터 컴포넌트 틱 비용을 그대로 낸다. 정량 수치는 미확인(프로파일 필요).

### 3) 상태 선택 방식과 난수원

| 사실 | 근거 |
|---|---|
| `EStateTreeStateSelectionBehavior` 7값 (None, TryEnterState, TrySelectChildrenInOrder, TrySelectChildrenAtRandom, TrySelectChildrenWithHighestUtility, TrySelectChildrenAtRandomWeightedByUtility, TryFollowTransitions) | StateTreeTypes.h:172-198 |
| 랜덤 자식: `Exec.RandomStream.RandRange(0, N-1)` 로 뽑고 실패하면 제거 후 재시도 | StateTreeExecutionContext.cpp:7960-7997 (핵심 :7983) |
| 최고 유틸리티: 점수>0 인 자식 중 `Score > HighestScore` 선형 탐색(동점은 앞 순서) — 난수 없음 | .cpp:8040-8058 |
| 가중 랜덤: `RandomScore = Exec.RandomStream.FRand() * TotalScore` 룰렛 | .cpp:8119-8130 |
| 점수 계산: `EvaluateUtilityWithValidation(..., UtilityConsiderationsBegin, UtilityConsiderationsNum, CurrentState.Weight)`; 개별 고려는 `GetNormalizedScore = Clamp(GetScore, 0, 1)` | .cpp:8032(최고 유틸리티), 8107(가중 랜덤); StateTreeConsiderationBase.cpp:14-17 |
| 내장 고려(Consideration): `FStateTreeConstantConsideration`, `FStateTreeFloatInputConsideration`(응답 곡선 `FStateTreeConsiderationResponseCurve`), `FStateTreeEnumInputConsideration` | E/StateTreeModule/Public/Considerations/StateTreeCommonConsiderations.h:25, 45, 88, 175 |
| 난수 스트림 위치: `FStateTreeExecutionState::RandomStream` (UPROPERTY, 인스턴스 데이터 안) | StateTreeExecutionTypes.h:1206-1207 |
| 시드: `Exec.RandomStream.Initialize(Parameters.RandomSeed.IsSet() ? Parameters.RandomSeed.GetValue() : FPlatformTime::Cycles())` | StateTreeExecutionContext.cpp:1513 |
| 지연 전이 랜덤 길이도 같은 스트림: `Transition.Delay.GetRandomDuration(Exec.RandomStream)` | .cpp:6081; StateTreeTypes.h:574-578 |
| `FMath::FRand/Rand` 사용 없음 (실행 컨텍스트·인스턴스 데이터·타입 헤더 grep 결과 0건) | grep 집계 |
| 5.7 추가 선택 규칙 `EStateTreeStateSelectionRules` (CompletedTransitionStatesCreateNewStates 등, 콘솔 변수 `StateTree.SelectState.*` 로 전역 기본값) | StateTreeTypes.h:110-122; StateTreeSchema.cpp:12-20 |

결정론 결론: **컴포넌트 경로(`UStateTreeComponent::StartTree`)는 `RandomSeed` 를 넘기지 않으므로 비결정적**(StateTreeComponent.cpp:194-198). Mass 활성화 프로세서도 `Start()` 무인자 호출(M/Private/MassStateTreeProcessors.cpp:225, MassStateTreeExecutionContext.cpp:128-138). 시뮬레이터에서는 반드시 `Start(FStartParameters{ .RandomSeed = seed })` 를 직접 호출해야 한다.

### 4) 에셋을 코드로 구성할 수 있는가

| 사실 | 근거 |
|---|---|
| 런타임 데이터(`Schema, Frames, States, Transitions, Nodes, SharedInstanceData, PropertyBindings` 등)는 모두 `UStateTree` 의 private UPROPERTY. 주석 "Data created during compilation, source data in EditorData." | StateTree.h:445-493 |
| 접근자는 읽기 전용(`GetNodes()`, `GetStates()`, `GetPropertyBindings()`) | StateTree.h:237-290 |
| 쓰기 권한은 friend 만: `FStateTreeCompiler`, `FCompilerManagerImpl`(5.7+) | StateTree.h:684-685 |
| `EditorData` 는 `WITH_EDITORONLY_DATA` 안의 `TObjectPtr<UObject>` | StateTree.h:381-384 |
| `IsReadyToRun()` = `States.Num()>0 && CompileStatus==Executable && PropertyBindings.IsValid()` (에디터에서는 `bCompilationPending` 이면 동기 컴파일) | StateTree.cpp:48-60 |
| 조립 API: `UStateTreeEditorData::AddSubTree / AddEvaluator<T> / AddGlobalTask<T> / AddPropertyBinding(...)`, `UStateTreeState::AddChildState / AddEnterCondition<T> / AddConsideration<T> / AddTask<T> / AddTransition(...)` | StateTreeEditorData.h:272-345; StateTreeState.h:282-342 |
| 상태 속성: `SelectionBehavior`, `TasksCompletion`, `CustomTickRate`, `Weight`, `EnterConditions`, `Tasks`, `Considerations` 모두 UPROPERTY → 파이썬/블루프린트 접근 가능(`UCLASS(BlueprintType)`) | StateTreeState.h:242-243, 420-487; StateTreeEditorData.h:64-65 |
| 컴파일: `FStateTreeCompiler(FStateTreeCompilerLog&)` → `bool Compile(UStateTree&)`; 5.8 권장 경로 `UStateTreeEditingSubsystem::CompileStateTree(TNotNull<UStateTree*>, FStateTreeCompilerLog&)` (static) | StateTreeCompiler.h:48-67; StateTreeEditingSubsystem.h:37; StateTreeDelegates.h:71-74 (5.8 deprecation) |
| 모듈 타입: `StateTreeEditorModule = UncookedOnly`, `StateTreeTestSuite = UncookedOnly`; 편집 모듈은 `UnrealEd` 의존 | StateTree.uplugin Modules 항목; StateTreeEditorModule.Build.cs:25-26 |
| 엔진 테스트의 코드 조립→컴파일→실행 예 | StateTreeTestBase.cpp:14-23 (`NewObject<UStateTree>`, `NewObject<UStateTreeEditorData>(StateTree)`, `StateTree->EditorData = EditorData; EditorData->Schema = ...`); StateTreeTest.cpp:34-70 |
| 커맨드렛 `UStateTreeCompileAllCommandlet::Main` : 에셋 레지스트리에서 모든 UStateTree 를 로드→`CompileStateTree`→저장 | E/StateTreeEditorModule/Private/Commandlets/StateTreeCompileAllCommandlet.cpp:25-80 |
| 텍스트 덤프: `UStateTree::DebugInternalLayoutAsString()` (`WITH_EDITOR || WITH_STATETREE_DEBUG`) | StateTree.h:376-378; StateTree.cpp:1581 |

코드 조립 예(엔진 테스트 그대로, StateTreeTest.cpp:36-69 요약):

```cpp
UStateTreeEditorData& ED = *Cast<UStateTreeEditorData>(StateTree.EditorData);
UStateTreeState& Root = ED.AddSubTree(FName("Root"));
UStateTreeState& A = Root.AddChildState(FName("A"));
auto& Task = A.AddTask<FTestTask_B>();
auto& Cond = A.AddEnterCondition<FStateTreeCompareIntCondition>(EComparisonOperator::Less);
ED.AddPropertyBinding(EvalA, TEXT("IntA"), Cond, TEXT("Left"));
A.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &B);
FStateTreeCompilerLog Log; FStateTreeCompiler Compiler(Log);
bool bOk = Compiler.Compile(StateTree);
```

**우회 경로 정리**
- (가) 에디터 타깃 C++ (자동화 테스트, 커맨드렛, 에디터 유틸리티): 위 API 로 완전 조립·컴파일·실행 가능. TDGame 의 기존 자동화 테스트가 에디터 타깃이므로 그대로 쓸 수 있다.
- (나) 파이썬: `UStateTreeEditorData`/`UStateTreeState` 가 `BlueprintType` UPROPERTY 노출이므로 값 편집은 가능하나, `AddTask<T>` 같은 템플릿 함수는 UFUNCTION 이 아니어서 파이썬에서 직접 호출 불가. 파이썬 전용 헬퍼 존재 여부: 엔진 `PythonScriptPlugin/Content/Python` 에 StateTree 관련 스크립트 0건 → **미확인/없음**.
- (다) 텍스트 형식: 전용 JSON/텍스트 임포트·익스포트 API 는 발견되지 않음(미확인). `UObject` 일반 `ExportText`/T3D 로는 `FStateTreeEditorNode`(TInstancedStruct) 직렬화가 가능할 것으로 보이나 검증하지 않음.
- (라) 런타임 빌드에서 트리를 만들려면 자체 정의 파일 → 에디터 커맨드렛으로 UStateTree 에셋 생성·쿠킹 파이프라인이 필요하다.

### 5) Mass 연동 구조

| 사실 | 근거 |
|---|---|
| 프래그먼트: `FMassStateTreeInstanceFragment { InstanceHandle, LastUpdateTimeInSeconds }`, 공유 프래그먼트 `FMassStateTreeSharedFragment { UStateTree* StateTree }` (청크 내 모든 엔티티가 같은 트리) | M/Public/MassStateTreeFragments.h:11-33 |
| 인스턴스 데이터는 엔티티가 아니라 `UMassStateTreeSubsystem::InstanceDataArray` 에 보관, 핸들로 참조(freelist 재사용) | M/Public/MassStateTreeSubsystem.h:58-91; .cpp:54-61 |
| 트레이트 `UMassStateTreeTrait::BuildTemplate` 이 `StateTree->IsReadyToRun()` 검증 후 공유 프래그먼트 추가 | M/Private/MassStateTreeTrait.cpp:39-49 |
| `UMassStateTreeProcessor : UMassSignalProcessorBase`, `Behavior` 그룹, `SyncWorldToMass` 뒤·`Tasks` 앞 실행; `ai.mass.DynamicSTProcessorsEnabled`(기본 true) 이면 트리별 동적 프로세서 인스턴스 | MassStateTreeProcessors.cpp:252-270; MassStateTreeSubsystem.cpp:20-21 |
| 구독 시그널: StateTreeActivate, NewStateTreeTaskRequired, DelayedTransitionWakeup, SmartObject*, FollowPointPath*, HitReceived 등 | MassStateTreeProcessors.cpp:273-300 |
| `UMassSignalProcessorBase::Execute` 는 받은 시그널이 없으면 즉시 return; 있으면 시그널 받은 엔티티 집합만 `SetEntityCollection` 후 `SignalEntities` 호출 | S/Private/MassSignalProcessorBase.cpp:55-58, 132-133 |
| 배치 실행: `ForEachEntityInChunk` 가 청크의 인스턴스 프래그먼트 뷰를 얻고 엔티티마다 `FMassStateTreeExecutionContext StateTreeContext(Subsystem, *StateTree, *InstanceData, Context); SetEntity(Entity);` 생성 → 콜백 | MassStateTreeProcessors.cpp:32-63 |
| Tick: `AdjustedDeltaTime = TimeInSeconds - LastUpdateTimeInSeconds` → `Tick(AdjustedDeltaTime)`; `LastTickStatus != Running` 이면 즉시 `Tick(0.0f)` 재시도, 그래도 아니면 다음 프레임 `NewStateTreeTaskRequired` 시그널 | .cpp:334-368, 403-406 |
| 병렬: `bProcessEntitiesInParallel`(config, 기본 false) → `ParallelForEachEntityChunk` + MPMC 큐 | .cpp:375-391; MassStateTreeProcessors.h:110-111 |
| 활성화 프로세서: `FMassStateTreeActivatedTag` 없는 엔티티에 `Start()` | .cpp:181-230 |
| 지연 전이 깨우기: `FMassStateTreeExecutionContext::BeginDelayedTransition` → `SignalSubsystem->DelaySignalEntityDeferred(..., DelayedTransitionWakeup, Entity, TimeLeft)` | M/Private/MassStateTreeExecutionContext.cpp:185-191 |
| Mass 스키마 `UMassStateTreeSchema` 는 `IsScheduledTickAllowed` 재정의 없음(0건) → 스케줄 틱 대신 시그널이 실질 스케줄러 | grep 집계; M/Public/MassStateTreeSchema.h:15-40 |
| `FMassStateTreeExecutionContext::Start(const FInstancedPropertyBag*, int32 RandomSeed)` 오버로드로 시드 지정 가능 | MassStateTreeExecutionContext.h:53-54; .cpp:140-152 |

핵심: Mass 는 "매 프레임 전 엔티티 Tick" 이 아니라 **"시그널 받은 엔티티만, 마지막 갱신 이후 경과 시간으로 Tick"** 이다. 즉 폴링 태스크가 없는 트리는 시그널 사이에 CPU 를 쓰지 않는다. 대신 Mass 스키마 전용 태스크(`FMassStateTreeTaskBase` 파생)만 허용된다(MassStateTreeSchema.cpp:20-26).

### 6) 5.6 / 5.7 / 5.8 추가 기능 (코드 흔적)

| 버전 | 근거(코드) | 내용 |
|---|---|---|
| 5.6 | `UE_DEPRECATED(5.6` 67건; StateTreeExecutionContext.h:227-232, 1266-1288 | `TNotNull` 생성자, `FStateTreeWeakTaskRef` 폐기, `FStateTreeTasksCompletionStatus` 로 태스크 완료 재설계, `BindDelegate/UnbindDelegate`(딜리게이트 도입, StateTreeDelegate.h:10-21), 스케줄 틱(`FStateTreeScheduledTick`, `ETickReason`, StateTreeExecutionTypes.h:785-861), 최소 컨텍스트 `SendEvent`(StateTreeExecutionContext.h:255) |
| 5.7 | `UE_DEPRECATED(5.7` 71건; StateTreeTypes.h:110-122 ("Previous (UE 5.6) rules"); StateTree.h:360, 386-390 ("Use the compiler manager."); StateTreeExecutionContext.h:884, 960-977 | 상태 선택 규칙 `EStateTreeStateSelectionRules`, 선택 결과 `FSelectStateResult`(상태 ID·프레임 ID 포함), 컴파일러 매니저(`UE::StateTree::Compiler::FCompilerManager`, StateTreeCompilerManager.h:15-35: 공개/내부 2단계 컴파일과 큐잉), `ForceTransition`(:822), `FStateTreeExecutionExtension::ScheduleNextTick(FContextParameters, FNextTickArguments)`(StateTreeExecutionExtension.h:62-68) |
| 5.8 | `UE_DEPRECATED(5.8` 24건; StateTreeExecutionContext.h:440-495 | `FStartParameters { InitialGlobalParameters, ExecutionExtension, SharedEventQueue, RandomSeed, SelectStateOverrideArgs{StateTag, TagQueryMethod} }` — **시작 상태를 게임플레이 태그로 지정**; `EComparisonOperator` 통합(StateTreeCommonConditions.h:47-231); `UStateTreeEditingSubsystem::CompileStateTree` 로 컴파일 진입점 통일(StateTreeDelegates.h:71-74); `PropertyRefExternalHandle` 폐기 → 약/강 실행 컨텍스트 비동기 패턴(StateTreePropertyRef.h:295, StateTreeAsyncExecutionContext.h:46-58); `ECompileStatus{Public, Internal, Link, Executable}` 와 `EDirtyStatus` 로 재컴파일 필요 추적(StateTree.h:571-600) |

웹 근거(2024~2026):
- 5.6 스케줄 틱: Tom Looman, "Unreal Engine 5.6 Performance Highlights" (2025-08-30) https://tomlooman.com/unreal-engine-5-6-performance-highlights/ — "State Trees now support scheduled ticking..."
- 5.6 실사용 이슈(Scheduled Tick Policy + Custom Tick Rate 미설정 시 틱 태스크 정지): 포럼 "StateTree Changes at 5.6 version" (2025-06-07) https://forums.unrealengine.com/t/statetree-changes-at-5-6-version/2545493
- 5.8: 공식 릴리스 노트 https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes?lang=en-US (2026, 페이지 게시일 미표시) — "trees can now define their starting state", "new Compiler Manager", "property binding ... reversed"
- 5.7 공식 릴리스 노트 StateTree 절: 페이지가 스크립트 렌더링이라 본문 추출 실패 → **미확인**(코드 흔적으로 대체)
- 로드맵 항목 "StateTree Scheduled Ticks and Performance" https://portal.productboard.com/epicgames/1-unreal-engine-public-roadmap/c/2029-statetree-scheduled-ticks-and-performance (게시 연도 미확인)

### 7) 콘솔 변수 목록 (정의 위치)

| 변수 | 정의 |
|---|---|
| `StateTree.Component.ScheduledTickEnabled` (true) | G/Private/Components/StateTreeComponent.cpp:18-24 |
| `StateTree.Component.DefaultScheduledTickAllowed` (true) | G/Private/Components/StateTreeComponentSchema.cpp:24-28 |
| `StateTree.CopyBoundPropertiesOnNonTickedTask`, `StateTree.TickGlobalNodesFollowingTreeHierarchy`, `StateTree.GlobalTasksCompleteOwningFrame`, `StateTree.SetDeprecatedTransitionResultProperties`, `StateTree.TargetStateRequiresTheSameEventForStateSelectionAsTheRequestedTransition`, `StateTree.CaptureStateEventPayloadForSustainedState` | E/StateTreeModule/Private/StateTreeExecutionContext.cpp:39-82 |
| `StateTree.SelectState.CompletedTransitionStatesCreateNewStates`, `StateTree.SelectState.CompletedStateBeforeTransitionSourceFailsTransition` | E/StateTreeModule/Private/StateTreeSchema.cpp:12-20 |
| `StateTree.RuntimeValidation.Context / DoesNewerVersionExists / EnterExitState / InstanceData / InstanceDataGC` | StateTreeRuntimeValidation.cpp:18-41; StateTreeModule.cpp:46-47 |
| `StateTree.CrashHandlerEnabled` | StateTreeCrashReporterHandler.cpp:18-19 |
| `ai.mass.DynamicSTProcessorsEnabled` (true) | M/Private/MassStateTreeSubsystem.cpp:20-21 |

---

## 프로젝트 적용 시사점

현재 상태(P/): `TDGame.uproject:44-49` 에 StateTree·GameplayStateTree 활성, `Source/TDGame/TDGame.Build.cs:19-20` 에 `StateTreeModule`, `GameplayStateTreeModule` 의존, `Variant_TwinStick/AI/TwinStickAIController.cpp:10` 이 `UStateTreeAIComponent` 사용, `TwinStickStateTreeUtility.cpp:12` 에 C++ 태스크(`FStateTreeGetPlayerTask::Tick`) 예가 있다. 전투 테스트 픽스처는 `UWorld::CreateWorld` + `World->Tick(LEVELTICK_All, Delta)` 고정 스텝이다(`Source/TDGame/Combat/Tests/TDDamageHomingTests.cpp:15-83`).

**쓸 것**
1. **몬스터 AI 실행은 `UStateTreeComponent` 를 쓰지 않고 자체 "AI 드라이버"(월드 서브시스템 또는 경량 컴포넌트) 가 `FStateTreeInstanceData` 배열을 소유하고 직접 `FStateTreeExecutionContext` 를 만들어 Tick 한다.** 이유: (가) 시드 지정 `Start(FStartParameters{.RandomSeed})` 가능 → 결정론, (나) 틱 주기·LOD(거리 등급) 를 프로젝트 코드가 결정, (다) 헤드리스 시뮬레이터와 게임이 같은 코드 경로. Mass 프로세서가 하는 방식(엔티티별 컨텍스트 생성, `LastUpdateTime` 차이를 DeltaTime 으로) 을 그대로 따라 하면 된다. 주의: `FStateTreeInstanceData` 를 구조체 배열에 두면 `AddStructReferencedObjects` 로 GC 참조 보고 필요(StateTreeInstanceData.h:453).
2. **잠들기/깨우기는 `GetNextScheduledTick()` 결과를 드라이버가 해석**한다. 컴포넌트 구현(StateTreeComponent.cpp:298-346)과 동일하게 Sleep 이면 목록에서 제외, CustomTickRate 면 다음 갱신 시각을 기록. 깨우기는 `FStateTreeExecutionExtension::ScheduleNextTick` 파생 구조체를 `FStartParameters::ExecutionExtension` 으로 넣어 받는다(StateTreeComponent.cpp:30-36 참고). 스키마는 `IsScheduledTickAllowed()` 를 true 로 재정의한 프로젝트 스키마여야 한다(StateTreeSchema.h:54-57).
3. **결정론 시뮬레이터**: 고정 스텝 루프에서 `SendEvent` → `Tick(Step)` 순서 고정, 시드는 몬스터 인덱스 기반, `DeltaTime` 을 실시간이 아닌 스텝 값으로 전달. `TickUpdateTasks` / `TickTriggerTransitions` 분리 호출로 "모든 몬스터 태스크 갱신 → 모든 전이 판정" 순서를 강제할 수도 있다(StateTreeExecutionContext.h:517-524). 이벤트 큐 상한 64 를 넘지 않도록 프레임당 이벤트 수를 제한한다(StateTreeEvents.cpp:45-48).
4. **트리 정의는 에디터 타깃 C++ 로 조립**한다: 텍스트(JSON 등) 몬스터 AI 정의 → 커맨드렛/에디터 유틸리티가 `UStateTreeEditorData` API 로 조립 → `UStateTreeEditingSubsystem::CompileStateTree` → 에셋 저장. 생성형 AI 는 텍스트 정의와 C++ 태스크/조건/고려 구조체만 다루면 되고 에디터를 열 필요가 없다. 자동화 테스트(에디터 타깃) 안에서는 컴파일 후 즉시 실행까지 가능하므로 시뮬레이터 테스트도 같은 방식으로 트리를 만든다.
5. **유틸리티 AI 는 스테이트 트리 안에서 `TrySelectChildrenWithHighestUtility` + 프로젝트 `FStateTreeConsiderationBase` 파생 구조체**로 구현한다. 확률 파라미터가 필요하면 `TrySelectChildrenAtRandomWeightedByUtility` 로 같은 시드 스트림을 쓴다. GOAP/HTN 같은 계획형은 스테이트 트리 밖(별도 C++ 플래너)에서 "목표 상태 태그" 를 정하고 `Start(FStartParameters{.SelectStateOverrideArgs})`(5.8) 또는 `RequestTransition(FStateTreeStateHandle)` 로 진입시키는 혼합이 가능하다.
6. **대량 몬스터**: 화면 밖/원거리 몬스터는 상태에 `CustomTickRate` 를 주거나 드라이버가 갱신 주기를 늘린다. Mass 로 옮기려면 태스크를 `FMassStateTreeTaskBase` 파생으로 다시 써야 하므로(MassStateTreeSchema.cpp:20-26) 액터 기반 GAS 전투와의 결합을 고려해 초기에는 자체 드라이버가 낫다. Mass 는 수백~수천 단위의 비전투 군중에 검토.

**피할 것**
- `UStateTreeComponent`/`UStateTreeAIComponent` 의 기본 시작 경로: 시드 미지정(StateTreeComponent.cpp:194-198) → 재현 불가.
- 에디터 없이(런타임 빌드) 트리 조립: 불가능. 반드시 에디터/커맨드렛 파이프라인.
- 매 프레임 폴링하는 태스크(`bShouldCallTick=true` 기본) 남발: 트리가 절대 Sleep 하지 못한다. 전투 판정은 이벤트(`SendEvent`)·딜리게이트로 밀어 넣는 설계.
- 실행 컨텍스트를 멤버로 보관: 명시적으로 금지된 사용법(StateTreeExecutionContext.h:277-279).
- 여러 스레드에서 같은 인스턴스 데이터 동시 Tick: `AcquireWriteAccess` 검증이 있고(StateTreeInstanceData.h:363-372) Mass 도 청크 단위 병렬만 한다. 병렬화는 "몬스터 집합을 스레드별로 나눠 각자 자기 인스턴스만" 원칙.

---

## 미확인·미해결 질문

1. **프레임당 비용 수치**: 컴포넌트 틱/실행 컨텍스트 생성 비용을 실측하지 않았다. `stat StateTree` / CSV 카테고리(`CSV_SCOPED_TIMING_STAT(StateTree, Tick)`, StateTreeExecutionContext.cpp:1814) 로 측정 필요.
2. **텍스트 임포트/익스포트**: StateTree 전용 JSON·텍스트 직렬화 API 는 발견하지 못했다. 일반 `ExportText`/T3D 가 `FStateTreeEditorNode`(TInstancedStruct) 를 왕복할 수 있는지 미검증.
3. **파이썬 조립 가능성**: `UStateTreeEditorData`/`UStateTreeState` 가 `BlueprintType` 이지만 `AddTask<T>` 류는 UFUNCTION 이 아니다. 파이썬에서 `FStateTreeEditorNode` 배열에 직접 원소를 넣어 컴파일이 통과하는지 미검증.
4. **5.7 공식 릴리스 노트 StateTree 절 본문**: 페이지 렌더링 문제로 확인 실패. 코드 흔적(선택 규칙, 컴파일러 매니저, FSelectStateResult) 으로만 판단.
5. **결정론의 남은 변수**: 트리 자체는 시드로 결정되지만, 태스크가 참조하는 외부(내비게이션, 물리, GAS 타이머, `FPlatformTime`) 는 별도 통제 필요. 특히 `UStateTreeComponent` 의 `SetComponentTickIntervalAndCooldown` 경로는 엔진 틱 매니저에 의존하므로 시뮬레이터에서는 사용하지 않는 것이 맞다.
6. **Mass 병렬 실행 시 GAS 액터 접근 안전성**: `bProcessEntitiesInParallel` 사용 시 태스크가 게임 스레드 전용 UObject 를 만지면 안전하지 않다(MassStateTreeProcessors.cpp:255 `bRequiresGameThreadExecution = bProcessEntitiesInParallel` 로 게임 스레드 강제). 몬스터 AI 를 Mass 로 옮길 때의 경계 설계는 미결.
7. **`UStateTree::PerThreadSharedInstanceData`**(StateTree.h:485-486) 가 시뮬레이터 다중 스레드 실행에 어떤 제약을 주는지(공유 인스턴스 데이터가 스레드별 사본으로 만들어지는 조건) 미확인.
