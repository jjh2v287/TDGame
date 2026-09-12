# 비헤이비어 트리·AIController·EQS·퍼셉션의 틱 모델과 수동 제어 가능성

조사 대상: 언리얼 엔진 5.8 소스 `Engine/Source/Runtime/AIModule` (이하 경로는 특별한 표기가 없으면 이 디렉터리 기준 상대 경로). 보조 근거는 `Engine/Source/Developer/AITestSuite`, `Engine/Source/Runtime/Engine`, `Engine/Source/Runtime/NavigationSystem`, `Engine/Source/Runtime/Navmesh`, `Engine/Source/Editor/UnrealEd`.
조사 방법: 헤더 선언과 .cpp 구현을 grep/sed 로 직접 읽음. 웹 자료는 사용하지 않았음(모든 근거가 엔진 코드).

---

## 결론 요약

설계 결정에 바로 쓸 수 있는 문장들이다. 각 문장 끝에 근거(파일:줄)를 붙였다.

1. **비헤이비어 트리(Behavior Tree, 이하 BT) 컴포넌트는 엔진 틱 밖에서 코드로 한 스텝씩 강제 진행할 수 있다(가능).** `UBehaviorTreeComponent::TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction* ThisTickFunction)` 은 public 이며, `ThisTickFunction == nullptr` 로 호출하면 월드 델타타임 대신 인자로 넘긴 `DeltaTime` 을 쓰도록 명시적으로 설계되어 있다(주석: "manual ticking in unit tests"). 엔진 자체 테스트 스위트가 `PrimaryComponentTick.bCanEverTick = false` 로 등록한 뒤 `TickComponent(DeltaTime, LEVELTICK_All, nullptr)` 를 직접 호출한다. — `Classes/BehaviorTree/BehaviorTreeComponent.h:202`, `Private/BehaviorTree/BehaviorTreeComponent.cpp:1698-1708`, `Developer/AITestSuite/Private/MockAI/MockAI_BT.cpp:26-27`, `Developer/AITestSuite/Private/MockAI/MockAI.cpp:78-94`.

2. **`SetComponentTickInterval` 로 BT 주기를 낮추는 것은 사실상 불가능하다(덮어써짐).** BT 는 매 틱 끝에서 스스로 다음 필요 시각을 계산해 `SetComponentTickIntervalAndCooldown(NextTickDeltaTime)` 을 호출하므로 외부에서 설정한 간격은 다음 틱에 덮어써진다. 주기를 늘리려면 노드의 `bTickIntervals`/`SetNextTickTime` 을 쓰거나(서비스 기본값), 아예 틱 함수를 끄고 수동 틱을 해야 한다. — `Private/BehaviorTree/BehaviorTreeComponent.cpp:1868, 1923-1951 (특히 1947)`.

3. **BT 의 "틱 간격 스케줄링(bTickIntervals)" 은 이미 자체 최적화가 들어 있다.** 활성 보조 노드(서비스·데코레이터)와 태스크가 각각 `NextTickRemainingTime` 을 갖고, 컴포넌트는 그중 최소값으로 다음 틱을 예약하며 필요 없으면 틱을 끈다(`DisableTick = FLT_MAX`). 즉 "매 프레임 트리 전체 평가"가 아니다. — `Private/BehaviorTree/BTAuxiliaryNode.cpp:45-80, 156-170`, `Private/BehaviorTree/BehaviorTreeComponent.cpp:39, 1868, 1931-1936`.

4. **BT 는 에디터 없이 C++ 로 완전히 구성·실행할 수 있다(가능).** `UBehaviorTree::RootNode` 는 런타임 UPROPERTY 이고 에디터 전용 데이터는 `BTGraph`/`LastEditedDocuments` 뿐이다. 런타임 로더 `UBehaviorTreeManager::LoadTree` 는 `Asset.RootNode` 만 보고 템플릿을 복제·초기화한다. 엔진 테스트 헬퍼 `FBTBuilder` 가 `NewObject<UBehaviorTree>()` + `NewObject<UBTComposite_Selector>(&Tree)` + `Children.AddZeroed` 로 트리를 코드로 만든다. — `Classes/BehaviorTree/BehaviorTree.h:19-33`, `Private/BehaviorTree/BehaviorTreeManager.cpp:263-317`, `Developer/AITestSuite/Public/BTBuilder.h:66-115`.

5. **BT 전용 텍스트 직렬화기는 없다(미확인이 아니라 부재 확인).** `Editor/BehaviorTreeEditor` 에 `UExporter` 파생 클래스가 없다. 다만 범용 `UObjectExporterT3D`(SupportedClass = UObject, 확장자 T3D/COPY) 와 `FJsonObjectConverter::UStructToJsonObjectString` 은 UPROPERTY 기반이라 적용 가능하지만, 노드 간 참조(ChildComposite/ChildTask/Decorators)와 서브오브젝트 복원은 직접 처리해야 한다. — `Editor/UnrealEd/Private/EditorExporters.cpp:322-326`, `Runtime/Engine/Classes/Exporters/Exporter.h:231`, `Runtime/JsonUtilities/Public/JsonObjectConverter.h:124,140`.

6. **몬스터 1마리당 BT 비용의 핵심은 UObject 3~4개 + 인스턴스 메모리 블록이다.** `UBehaviorTreeComponent`, `UBlackboardComponent`(ValueMemory), 액터 `AAIController`(+`UPathFollowingComponent`) 가 기본이며, 트리 노드 자체는 월드 단위 템플릿(`UBehaviorTreeManager::LoadedTemplates`)을 공유하고 인스턴스마다 `FBehaviorTreeInstance::InstanceMemory`(uint8 배열, 4바이트 정렬 패킹) 만 갖는다. 예외: 블루프린트 노드(`bCreateNodeInstance = true`)는 컴포넌트마다 `StaticDuplicateObject` 로 UObject 복제본을 만든다 → 대량 몬스터에서는 C++ 노드만 써야 한다. — `Classes/BehaviorTree/BehaviorTreeTypes.h:290-343`, `Private/BehaviorTree/BehaviorTreeComponent.cpp:2743-2769`, `Private/BehaviorTree/BTNode.cpp:90-102`, `Private/BehaviorTree/Tasks/BTTask_BlueprintBase.cpp:24`.

7. **BT 코어는 타이머를 쓰지 않고 랜덤은 두 곳뿐이다.** 블랙보드 옵저버 어보트는 `SetValue → NotifyObservers → ConditionalFlowAbort → RequestExecution → ScheduleExecutionUpdate(다음 틱)` 로 동기 호출 + 다음 틱 처리이며 `FTimerManager` 를 쓰지 않는다. 랜덤은 `UBTService::ScheduleNextTick` 의 `RandomDeviation` 과 `UBTTask_Wait` 의 `RandomDeviation` 이 `FMath::FRandRange`(전역 난수, `UAISystem` 스트림 아님)를 호출하는 두 곳이 전부다. 두 값을 0 으로 두면 BT 자체는 결정론적이다. — `Private/BehaviorTree/BlackboardComponent.cpp:426-470`, `Private/BehaviorTree/BTDecorator.cpp:88-120`, `Private/BehaviorTree/BehaviorTreeComponent.cpp:1453, 1144-1150`, `Private/BehaviorTree/BTService.cpp:107`, `Private/BehaviorTree/Tasks/BTTask_Wait.cpp:19`.

8. **EQS(Environment Query System, 환경 질의 시스템)는 결정론 위험이 두 겹이다.** (a) `UEnvQueryManager::Tick` 은 `FPlatformTime::Seconds()`(벽시계) 기준 `MaxAllowedTestingTime` 으로 프레임 분할 실행하므로 어느 프레임에 결과가 나오는지가 기기 속도에 따라 달라진다. (b) `RandomBest5Pct/25Pct` 실행 모드는 `UAISystem::GetRandomStream()` 을 쓰며 이 스트림은 `-FixedSeed` 커맨드라인이 없으면 `FDateTime::Now()` 로 시드된다. 해결책: `RunInstantQuery`(동기 완료) + `SingleResult`/`AllMatching` 모드 + `UAISystem::SeedRandomStream(Seed)`. — `Private/EnvironmentQuery/EnvQueryManager.cpp:434-590, 305-335`, `Private/EnvironmentQuery/EnvQueryInstance.cpp:750`, `Private/AISystem.cpp:35-43`, `Classes/AISystem.h:278-279`.

9. **AIPerception(인식) 시스템은 월드 시간 기반이라 고정 스텝에서는 대체로 결정론적이지만, 시각 감각(`UAISense_Sight`)의 시간 분할이 벽시계를 쓴다.** `UAIPerceptionSystem::Tick` 은 `World->GetTimeSeconds()` 와 `TimeUntilNextUpdate` 카운터로 동작하지만, `UAISense_Sight::Update` 는 `FPlatformTime::Seconds() + MaxTimeSlicePerTick(5ms)` 로 처리량을 자르고 비동기 라인트레이스(`AsyncLineTraceByChannel`)도 쓴다. 결정론 시뮬레이션에서는 퍼셉션을 쓰지 않고 자체 거리/시야 판정으로 대체하는 것이 안전하다. — `Private/Perception/AIPerceptionSystem.cpp:160-241`, `Private/Perception/AISense_Sight.cpp:46-48, 136-140, 296, 328-336, 556`.

10. **DetourCrowd(군중 회피) 는 월드 틱 순서 안에서 결정적으로 갱신되며 난수를 쓰지 않는다.** `UCrowdManager::Tick` 은 `UNavigationSystemV1::Tick` 이 호출하고, 그 호출은 `UWorld::Tick` 안에 있다. Detour 군중 코드(`Navmesh/Private/DetourCrowd`)에는 `rand(` 호출이 없고, 벽시계도 쓰지 않는다. 다만 에이전트 순회가 `TMap` 이라 등록 순서에 의존한다(같은 등록 순서면 같은 결과). — `Private/Navigation/CrowdManager.cpp:231-323`, `Runtime/NavigationSystem/Private/NavigationSystem.cpp:1831-1835`, `Runtime/Engine/Private/LevelTick.cpp:1647`.

11. **AIController 는 기본적으로 매 프레임 액터 틱을 돈다(제어 회전 갱신).** `AController` 생성자가 `PrimaryActorTick.bCanEverTick = true`, `AAIController::Tick` 은 `UpdateControlRotation` 만 호출. 수천 마리에서는 이 액터 틱 자체가 비용이므로 AIController 없는 설계(폰이 직접 두뇌 컴포넌트를 소유)도 검토 대상이다. — `Runtime/Engine/Private/Controller.cpp:62`, `Private/AIController.cpp:58-63`.

12. **프로젝트의 테스트 픽스처 `FTDScopedCombatWorld` 는 `World->Tick(LEVELTICK_All, Delta)` 를 쓰므로 EQS·퍼셉션·군중 관리자(모두 월드 틱 내부의 `FTickableGameObject::TickObjects` 또는 `NavigationSystem->Tick`)가 함께 돈다.** BT 컴포넌트도 틱 함수가 켜져 있으면 같이 돌지만, 완전한 수동 제어를 원하면 `bCanEverTick=false` + 직접 `TickComponent` 호출로 바꿔야 한다. — `Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:35-38, 71-77`, `Runtime/Engine/Private/LevelTick.cpp:1647, 1821`.

---

## 상세 조사

### 1) UBehaviorTreeComponent / UBrainComponent / AAIController 의 틱 경로와 수동 제어

#### 1-1. 클래스 계층과 기본 틱 설정

| 사실 | 근거 |
|---|---|
| `UBrainComponent : UActorComponent`, 생성자에서 `PrimaryComponentTick.bCanEverTick = true` | `Classes/BrainComponent.h:117`, `Private/BrainComponent.cpp:191-193` |
| `UBrainComponent::TickComponent` 는 `MessagesToProcess` 큐를 비우는 일만 한다(AI 메시지 관찰자 통지) | `Private/BrainComponent.cpp:277-294` |
| `UBehaviorTreeComponent : UBrainComponent`, 생성자 `bAutoActivate = true; bWantsInitializeComponent = true; bIsRunning=false; bIsPaused=false` | `Classes/BehaviorTree/BehaviorTreeComponent.h:104`, `Private/BehaviorTree/BehaviorTreeComponent.cpp:61-74` |
| `AAIController` 생성자는 `PathFollowingComponent` 를 기본 서브오브젝트로 만들고 `bStartAILogicOnPossess = false` | `Private/AIController.cpp:40-56` |
| `AAIController::Tick(float DeltaTime)` = `Super::Tick` + `UpdateControlRotation(DeltaTime)` | `Private/AIController.cpp:58-63` |
| `AController` 는 `PrimaryActorTick.bCanEverTick = true` | `Runtime/Engine/Private/Controller.cpp:62` |
| `AAIController::RunBehaviorTree(UBehaviorTree*)` 가 필요 시 `NewObject<UBehaviorTreeComponent>(this)` + `RegisterComponent()` + `StartTree(*BTAsset, Looped)` | `Private/AIController.cpp:1001-1043` |

#### 1-2. 틱 등록과 자체 간격 스케줄링

```cpp
// Private/BehaviorTree/BehaviorTreeComponent.cpp:112-119
void UBehaviorTreeComponent::RegisterComponentTickFunctions(bool bRegister)
{ if (bRegister) { ScheduleNextTick(0.0f); } Super::RegisterComponentTickFunctions(bRegister); }

// Private/BehaviorTree/BehaviorTreeComponent.cpp:1923-1951 (발췌)
void UBehaviorTreeComponent::ScheduleNextTick(const float NextNeededDeltaTime)
{
	NextTickDeltaTime = NextNeededDeltaTime;
	if (bRequestedFlowUpdate) { NextTickDeltaTime = 0.0f; }
	if (NextTickDeltaTime == UE::BehaviorTree::DisableTick) { if (IsComponentTickEnabled()) SetComponentTickEnabled(false); }
	else { if (!IsComponentTickEnabled()) SetComponentTickEnabled(true);
	       SetComponentTickIntervalAndCooldown(!bTickedOnce && NextTickDeltaTime < FORCE_TICK_INTERVAL_DT ? FORCE_TICK_INTERVAL_DT : NextTickDeltaTime); }
	LastRequestedDeltaTimeGameTime = MyWorld ? MyWorld->GetTimeSeconds() : 0.;
}
```

| 사실 | 근거 |
|---|---|
| `DisableTick = FLT_MAX` 상수; 다음 틱이 필요 없으면 틱 함수 자체를 끈다 | `Private/BehaviorTree/BehaviorTreeComponent.cpp:39, 1931-1936` |
| 매 `TickComponent` 끝에서 `ScheduleNextTick(NextNeededDeltaTime)` 호출 → 외부 `SetComponentTickInterval` 값은 다음 틱에 덮어써짐 | `Private/BehaviorTree/BehaviorTreeComponent.cpp:1868, 1947` |
| `SetComponentTickInterval` 은 "다음 틱부터 적용", `SetComponentTickIntervalAndCooldown` 은 "즉시 적용" (엔진 API 주석) | `Runtime/Engine/Classes/Components/ActorComponent.h:1017-1028` |
| `SetComponentTickEnabled(true)` 로 다시 켜면 `bTickedOnce=false` 로 되돌리고 `ScheduleNextTick(0)` | `Private/BehaviorTree/BehaviorTreeComponent.cpp:121-133` |
| 예약 시각보다 일찍 틱되면 `AccumulatedTickDeltaTime` 에 누적하고 즉시 반환(작업 없음) | `Private/BehaviorTree/BehaviorTreeComponent.cpp:1721-1733` |
| 상태 필드: `bTickedOnce`, `NextTickDeltaTime`, `AccumulatedTickDeltaTime`, `CurrentFrameDeltaTime` | `Classes/BehaviorTree/BehaviorTreeComponent.h:555-564` |

#### 1-3. TickComponent 의 처리 순서(한 스텝의 정의)

`Private/BehaviorTree/BehaviorTreeComponent.cpp:1698-1921` 를 순서대로 요약:

1. `ThisTickFunction && World` 이면 `CurrentFrameDeltaTime = World->GetDeltaSeconds()`, 아니면 인자 `DeltaTime` (수동 틱 경로, 1704-1708).
2. `NextTickDeltaTime -= DeltaTime`; 아직 남았으면 누적 후 반환 (1721-1733).
3. `Super::TickComponent` — 큐에 쌓인 AI 메시지 처리 (1748).
4. 모든 인스턴스 스택의 활성 보조 노드(서비스·데코레이터) `WrappedTickNode` (1760-1775).
5. 잠복 어보트 완료 추적 → 필요 시 `StopTree` 또는 `ScheduleExecutionUpdate` (1778-1800).
6. `bRequestedFlowUpdate` 이면 `ProcessExecutionRequest()` — 실제 트리 탐색/노드 전환은 여기서만 일어난다 (1801-1809).
7. `bIsRunning && !bIsPaused` 이면 병렬 태스크, 활성 태스크, 어보트 중인 태스크 `WrappedTickTask` (1814-1851).
8. 활성 보조 노드들의 `GetNextNeededDeltaTime` 최소값으로 `ScheduleNextTick` (1853-1868).

핵심: **탐색 요청(`RequestExecution`)은 즉시 처리되지 않고 다음 틱으로 미뤄진다.** `RequestExecution(...)` 의 마지막 줄이 `ScheduleExecutionUpdate()` 이고, 이 함수는 `ScheduleNextTick(0.0f); bRequestedFlowUpdate = true;` 만 한다(`Private/BehaviorTree/BehaviorTreeComponent.cpp:1453, 1144-1150`). 즉 "블랙보드 값 변경 → 실제 브랜치 전환"은 최소 한 틱의 지연이 있다. 결정론적 시뮬레이션에서는 이 지연이 스텝 단위로 고정되므로 문제가 되지 않지만, 스텝 크기를 바꾸면 결과가 달라진다는 뜻이다.

#### 1-4. PauseLogic / ResumeLogic / StartTree / StopTree

| 사실 | 근거 |
|---|---|
| `PauseLogic`: `bIsPaused = true` + `BlackboardComp->PauseObserverNotifications()` (틱 함수는 끄지 않음) | `Private/BehaviorTree/BehaviorTreeComponent.cpp:173-185` |
| 일시정지 중에도 `TickComponent` 는 호출되며 보조 노드 틱(4단계)은 돈다; 태스크 틱(7단계)과 `ProcessExecutionRequest` 만 막힌다 | `Private/BehaviorTree/BehaviorTreeComponent.cpp:1814, 1962-1966` |
| `ResumeLogic`: `bIsPaused=false; ScheduleNextTick(0)`; `Super` 가 `Continue` 면 큐된 블랙보드 통지를 재전송하고 보류된 실행 요청을 재예약; `RestartedInstead` 면 큐 폐기 | `Private/BehaviorTree/BehaviorTreeComponent.cpp:187-223`, `Private/BrainComponent.cpp:318-331` |
| `IsRunning() = !bIsPaused && TreeHasBeenStarted()` | `Private/BehaviorTree/BehaviorTreeComponent.cpp:230-233` |
| `StartTree` 는 `StopTree(Safe)` 후 `TreeStartInfo` 를 채우고 **즉시** `ProcessPendingInitialize()` → `PushInstance` → 루트에 `RequestExecution` (실제 첫 탐색은 다음 틱) | `Private/BehaviorTree/BehaviorTreeComponent.cpp:240-299`, `2707-2795 (PushInstance)` |
| `StopTree(EBTStopMode::Safe/Forced)`, `RestartTree(EBTRestartMode)` 공개 API | `Classes/BehaviorTree/BehaviorTreeComponent.h:137-146` |

프로젝트는 이미 `PauseLogic(TEXT("Frozen"))` 을 사용한다(`Source/TDGame/Combat/TDCombatComponentStatus.cpp:434-442`). 위 사실에 따라 "빙결" 중에도 서비스 틱은 계속 돈다는 점을 알아 두어야 한다.

#### 1-5. 엔진 틱 밖에서 한 스텝 강제 진행: 가능(엔진 테스트가 그렇게 한다)

```cpp
// Developer/AITestSuite/Private/MockAI/MockAI_BT.cpp:26-27
BTComp->PrimaryComponentTick.bCanEverTick = false;   // 틱 함수로는 절대 틱되지 않게
BTComp->RegisterComponent();
// Developer/AITestSuite/Private/MockAI/MockAI.cpp:78-94
void UMockAI::TickMe(float DeltaTime)
{
	if (BBComp)         { BBComp->TickComponent(DeltaTime, ELevelTick::LEVELTICK_All, nullptr); }
	if (PerceptionComp) { PerceptionComp->TickComponent(DeltaTime, ELevelTick::LEVELTICK_All, nullptr); }
	if (BrainComp)      { BrainComp->TickComponent(DeltaTime, ELevelTick::LEVELTICK_All, nullptr); }
}
```

| 사실 | 근거 |
|---|---|
| 테스트 루프는 `FTimerManager::Tick(1/30)` 을 먼저 돌리고(일부 노드가 타이머 의존) `TickMe(1/30)` 호출 | `Developer/AITestSuite/Private/Tests/BTTest.cpp:60-73`, `Developer/AITestSuite/Public/AITestsCommon.h:40` |
| 트리 시작은 `RegisterComponentWithWorld(World)` 후 `StartTree(BTAsset, RunType)` | `Developer/AITestSuite/Private/MockAI/MockAI_BT.cpp:55-58` |
| `TickComponent` 첫 줄의 `ensureMsgf(bIsRunning || NextTickDeltaTime != DisableTick)` — 트리가 멈춘 뒤 수동 틱하면 ensure 가 뜬다 | `Private/BehaviorTree/BehaviorTreeComponent.cpp:1700` |
| 수동 틱 시 `ThisTickFunction == nullptr` 이면 "예약보다 일찍 틱됨" 경고(VLOG Error)는 건너뛴다(`ThisTickFunction != nullptr` 조건) | `Private/BehaviorTree/BehaviorTreeComponent.cpp:1710, 1725` |

주의할 점: 수동 틱을 하더라도 `ScheduleNextTick` 은 여전히 `SetComponentTickEnabled/SetComponentTickIntervalAndCooldown` 을 호출한다(1931-1947). `bCanEverTick=false` 상태에서는 `SetComponentTickEnabled(true)` 가 실제 등록을 하지 않으므로 해가 없지만, 이 호출은 매 스텝 발생한다(경미한 오버헤드).

### 2) 에디터 없이 C++ 로 BT 구성하기, 텍스트 직렬화

#### 2-1. 에셋 구조

```cpp
// Classes/BehaviorTree/BehaviorTree.h:19-53 (발췌)
UPROPERTY(BlueprintReadOnly) TObjectPtr<UBTCompositeNode> RootNode;          // :21
#if WITH_EDITORONLY_DATA
	UPROPERTY() TObjectPtr<class UEdGraph> BTGraph;                          // :27  (에디터 전용)
	UPROPERTY() TArray<FEditedDocumentInfo> LastEditedDocuments;             // :31  (에디터 전용)
#endif
UPROPERTY(BlueprintReadOnly) TObjectPtr<UBlackboardData> BlackboardAsset;   // :42
UPROPERTY(BlueprintReadOnly) TArray<TObjectPtr<UBTDecorator>> RootDecorators; // :46
UPROPERTY() TArray<FBTDecoratorLogic> RootDecoratorOps;                      // :50
uint16 InstanceMemorySize;                                                    // :53
```

| 사실 | 근거 |
|---|---|
| 컴포지트 자식은 `FBTCompositeChild { ChildComposite, ChildTask, Decorators, DecoratorOps }` 배열 `Children` 과 `Services` 배열 — 전부 런타임 UPROPERTY | `Classes/BehaviorTree/BTCompositeNode.h:66-97` |
| `UBTNode` 의 `WITH_EDITOR` 블록은 아이콘/검증/PreSave 등 에디터 편의 함수뿐, 실행에 필요한 데이터 아님 | `Classes/BehaviorTree/BTNode.h:165-185` |
| `UBTCompositeNode` 의 `WITH_EDITOR` 는 `CanAbortLowerPriority/CanAbortSelf` 만 | `Classes/BehaviorTree/BTCompositeNode.h:171-175` |
| 런타임 로더는 `Asset.RootNode` 를 `StaticDuplicateObject` 로 복제 → `InitializeNodeHelper` 가 재귀로 `InitializeFromAsset` 호출 및 실행 인덱스 부여 → 메모리 크기순 정렬 후 `InitializeNode(Parent, ExecIdx, MemoryOffset, Depth)` | `Private/BehaviorTree/BehaviorTreeManager.cpp:263-317 (282, 287, 302-308)`, `117-130` |
| 템플릿은 월드 매니저 `LoadedTemplates` 에 에셋당 1개 캐시 | `Classes/BehaviorTree/BehaviorTreeManager.h:73`, `Private/BehaviorTree/BehaviorTreeManager.cpp:267-276` |

#### 2-2. 코드로 만든 트리가 실행 가능하다는 직접 증거

```cpp
// Developer/AITestSuite/Public/BTBuilder.h:80-97 (발췌)
static UBTComposite_Selector& AddSelector(UBehaviorTree& TreeOb)
{ UBTComposite_Selector* NodeOb = NewObject<UBTComposite_Selector>(&TreeOb);
  NodeOb->InitializeFromAsset(TreeOb); TreeOb.RootNode = NodeOb; return *NodeOb; }
static UBTComposite_Selector& AddSelector(UBTCompositeNode& ParentNode)
{ ... const int32 ChildIdx = ParentNode.Children.AddZeroed(1);
  ParentNode.Children[ChildIdx].ChildComposite = NodeOb; ... }
```

블랙보드도 코드로 만든다: `NewObject<UBlackboardData>()` 에 `FBlackboardEntry{EntryName, KeyType=NewObject<UBlackboardKeyType_Int>()}` 를 `Keys.Add` 후 `UpdateParentKeys()` (`Developer/AITestSuite/Public/BTBuilder.h:60-64`). 이 트리를 `UMockAI_BT::RunBT` 가 `StartTree` 로 실행한다(`MockAI_BT.cpp:45-59`). 따라서 "에디터 전용 데이터 없이 실행 가능" 은 엔진 자체 테스트로 입증된다.

#### 2-3. 텍스트 직렬화(T3D / JSON)

| 사실 | 근거 |
|---|---|
| `Editor/BehaviorTreeEditor` 에 `UExporter` 파생 클래스 없음(grep 결과 0건) | `Engine/Source/Editor/BehaviorTreeEditor` 전체 grep "UExporter" |
| 범용 `UObjectExporterT3D`: `SupportedClass = UObject::StaticClass(); bText = true; FormatExtension T3D, COPY` | `Editor/UnrealEd/Private/EditorExporters.cpp:322-326` |
| `UExporter::ExportToOutputDevice(Context, Object, Exporter, Out, FileType, Indent, PortFlags, ...)` 로 UPROPERTY 를 텍스트로 내보낼 수 있음(에디터 모듈, 런타임 아님) | `Runtime/Engine/Classes/Exporters/Exporter.h:231` |
| `FJsonObjectConverter::UStructToJsonObject(const UStruct*, const void*, ...)` / `UStructToJsonObjectString` — 런타임 모듈(JsonUtilities), UClass 도 UStruct 이므로 노드 속성 덤프 가능 | `Runtime/JsonUtilities/Public/JsonObjectConverter.h:124, 140` |

판단: T3D 는 에디터 전용이고 서브오브젝트 그래프(각 노드가 트리 에셋의 Outer 아래 UObject) 를 복원하려면 `ImportObjectProperties` 계열이 필요하다. JSON 은 런타임에서 가능하지만 `TObjectPtr` 참조를 이름/인덱스로 바꾸는 사용자 정의 변환(`CustomExportCallback`) 이 필요하다. **결론적으로 "BT 를 텍스트로 정의" 하려면 엔진 직렬화기가 아니라 자체 DSL(도메인 특화 언어) → `FBTBuilder` 식 코드 생성 경로가 현실적이다.**

### 3) 인스턴스 메모리 구조와 몬스터 1마리당 비용 구성

#### 3-1. 자료 구조

```cpp
// Classes/BehaviorTree/BehaviorTreeTypes.h:290-302
struct FBehaviorTreeInstanceId { TObjectPtr<UBehaviorTree> TreeAsset; TObjectPtr<UBTCompositeNode> RootNode;
                                 TArray<uint16> Path; TArray<uint8> InstanceMemory; /* 영속 메모리 */ };
// Classes/BehaviorTree/BehaviorTreeTypes.h:319-343
struct FBehaviorTreeInstance { TObjectPtr<UBTCompositeNode> RootNode; TObjectPtr<UBTNode> ActiveNode;
  TArray<TObjectPtr<UBTAuxiliaryNode>> ActiveAuxNodes; TArray<FBehaviorTreeParallelTask> ParallelTasks;
  TArray<uint8> InstanceMemory; uint8 InstanceIdIndex; TEnumAsByte<EBTActiveNode::Type> ActiveNodeType;
  FBTInstanceDeactivation DeactivationNotify; };
```

| 구성 요소(1마리당) | 내용 | 근거 |
|---|---|---|
| `UBehaviorTreeComponent` UObject | `InstanceStack`(서브트리 깊이만큼), `KnownInstances`, `NodeInstances`, `SearchData`, `ExecutionRequest`, `PendingExecution`, 틱 함수 등록 | `Classes/BehaviorTree/BehaviorTreeComponent.h:309-324` |
| 인스턴스 메모리 블록 | `PushInstance` 가 `LoadTree` 로 `InstanceMemorySize` 를 얻고 `KnownInstances[..].InstanceMemory.AddZeroed(InstanceMemorySize)` (최초 1회), 스택 항목은 이를 참조 | `Private/BehaviorTree/BehaviorTreeComponent.cpp:2743-2764` |
| 노드별 메모리 | 각 노드 `GetInstanceMemorySize()` + `GetSpecialMemorySize()`; 4바이트 정렬(`GetAlignedDataSize`), 크기순 정렬 패킹 | `Classes/BehaviorTree/BTNode.h:69, 84`, `Private/BehaviorTree/BehaviorTreeManager.cpp:58-62, 302-308` |
| 특수 메모리 | `bTickIntervals` 노드는 `FBTAuxiliaryMemory{NextTickRemainingTime, AccumulatedDeltaTime}` / 태스크는 `FBTTaskMemory{NextTickRemainingTime}`; 인스턴스화 노드는 `FBTInstancedNodeMemory{NodeIdx}` | `Classes/BehaviorTree/BTAuxiliaryNode.h:9-12`, `Classes/BehaviorTree/BTTaskNode.h:11-13`, `Private/BehaviorTree/BTAuxiliaryNode.cpp:143`, `Private/BehaviorTree/BTNode.cpp:158` |
| 노드 UObject 복제본 | `bCreateNodeInstance` 노드만 컴포넌트당 `StaticDuplicateObject(this, &OwnerComp)` → `NodeInstances` 에 보관. 블루프린트 태스크/데코레이터/서비스는 기본 true, C++ 노드는 기본 false | `Private/BehaviorTree/BTNode.cpp:27, 90-102`, `Tasks/BTTask_BlueprintBase.cpp:24`, `Decorators/BTDecorator_BlueprintBase.cpp:34`, `Services/BTService_BlueprintBase.cpp:26` |
| `UBlackboardComponent` UObject | `ValueMemory`(uint8), `ValueOffsets`(uint16), `KeyInstances`(인스턴스화 키 타입 UObject) + 옵저버 멀티맵 | `Classes/BehaviorTree/BlackboardComponent.h:245-252, 282-288` |
| `AAIController` 액터 | 액터 틱(매 프레임) + `UPathFollowingComponent`(기본 클래스는 이동 중일 때만 틱) | `Private/AIController.cpp:44`, `Private/Navigation/PathFollowingComponent.cpp:129-134` |
| 메시지 큐 | `TArray<FAIMessage> MessagesToProcess` | `Classes/BrainComponent.h:131` |
| 월드 공유 | 트리 템플릿(노드 UObject 트리)은 `UBehaviorTreeManager::LoadedTemplates` 에 에셋당 1개, `ActiveComponents` 배열에 컴포넌트 등록 | `Classes/BehaviorTree/BehaviorTreeManager.h:73-76`, `Private/BehaviorTree/BehaviorTreeComponent.cpp:289-293` |

#### 3-2. 틱 비용의 구성(프레임당)

- 활성 보조 노드 수 × `WrappedTickNode`(간격 미도달이면 카운트다운만) — `BTAuxiliaryNode.cpp:45-80`.
- 활성 태스크 1개(+병렬 태스크) `WrappedTickTask` — `BehaviorTreeComponent.cpp:1818-1851`.
- 실행 요청이 있을 때만 `ProcessExecutionRequest`(트리 탐색; 비용은 트리 크기·데코레이터 수에 비례) — `BehaviorTreeComponent.cpp:1953-2230`.
- 틱이 필요 없으면 컴포넌트 틱 자체가 꺼지므로 "대기 중" 몬스터의 BT 비용은 0 에 가깝다(단 AIController 액터 틱은 남는다).

### 4) 결정론 위험 목록

#### 4-1. 블랙보드/데코레이터 옵저버 어보트

```cpp
// Private/BehaviorTree/Decorators/BTDecorator_BlackboardBase.cpp:40
BlackboardComp->RegisterObserver(KeyID, this, FOnBlackboardChangeNotification::CreateUObject(this, &UBTDecorator_BlackboardBase::OnBlackboardKeyValueChange));
// Private/BehaviorTree/BTDecorator.cpp:88-120  ConditionalFlowAbort → RequestBranchDeactivation / RequestBranchActivation
// Private/BehaviorTree/BehaviorTreeComponent.cpp:1453  RequestExecution 끝: ScheduleExecutionUpdate();
```

| 사실 | 근거 |
|---|---|
| `NotifyObservers` 는 `SetValue` 호출 스택 안에서 동기 실행; 일시정지 중이면 `QueuedUpdates.AddUnique(KeyID)` 로 큐잉 | `Private/BehaviorTree/BlackboardComponent.cpp:426-470, 405-424` |
| BT 코어(`Private/BehaviorTree/*`)에서 `FTimerManager` 사용은 `BTTask_PlayAnimation.cpp:63`(애니메이션 종료 지연)과 `BlueprintNodeHelpers.cpp:304`(정리) 뿐 | grep "SetTimer\|GetTimerManager" 결과 |
| `EBTFlowAbortMode { None, LowerPriority, Self, Both }` — 시간 요소 없음 | `Classes/BehaviorTree/BehaviorTreeTypes.h:142-152` |
| `UBTDecorator_Cooldown` 은 `OwnerComp.GetWorld()->GetTimeSeconds()` 기준(월드 시간; 고정 스텝이면 결정적) | `Private/BehaviorTree/Decorators/BTDecorator_Cooldown.cpp:27, 34, 43, 64` |
| `UBTDecorator_TimeLimit` 은 `bTickIntervals + SetNextTickTime(TimeLimit)` — 누적 델타 기반, 결정적 | `Private/BehaviorTree/Decorators/BTDecorator_TimeLimit.cpp:19, 46` |

→ 어보트 경로 자체는 결정적이다. 단 "변경 → 반영"이 다음 틱이므로 스텝 크기가 결과에 포함된다.

#### 4-2. BT 내부 난수(전체 2곳)

```cpp
// Private/BehaviorTree/BTService.cpp:105-109
void UBTService::ScheduleNextTick(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{ const float NextTickTime = FMath::FRandRange(FMath::Max(0.0f, Interval - RandomDeviation), (Interval + RandomDeviation));
  SetNextTickTime(NodeMemory, NextTickTime); }
// Private/BehaviorTree/Tasks/BTTask_Wait.cpp:19
const float RemainingWaitTime = FMath::FRandRange(FMath::Max(0.0f, WaitSecond - DeviationSecond), (WaitSecond + DeviationSecond));
```

| 사실 | 근거 |
|---|---|
| `UBTService` 기본값 `Interval = 0.5f, RandomDeviation = 0.1f, bTickIntervals = true` — **기본값이 비결정적** | `Private/BehaviorTree/BTService.cpp:9-21` |
| `FMath::FRandRange` 는 전역 난수(`UAISystem::RandomStream` 아님) | `Runtime/Core/Public/Math/UnrealMathUtility.h:313` |
| `Private/BehaviorTree` + `Classes/BehaviorTree` 전체에서 `FRand/Rand/RandRange/FRandomStream` 검색 결과는 위 2건뿐; 셀렉터/시퀀스/심플패럴렐에 난수 없음 | grep 결과 |

→ 서비스·Wait 의 `RandomDeviation = 0` 을 강제(또는 자체 서브클래스에서 `ScheduleNextTick` 을 시드 스트림으로 재정의)하면 BT 자체는 결정적이다.

#### 4-3. EQS 실행 스케줄과 난수

```cpp
// Private/EnvironmentQuery/EnvQueryManager.cpp:434-482 (발췌)
void UEnvQueryManager::Tick(float DeltaTime)
{	double TimeLeft = MaxAllowedTestingTime;                               // :446
	while ((TimeLeft > 0.0) && (Index < NumRunningQueries) && ...)          // :456
	{	const double StepStartTime = FPlatformTime::Seconds();              // :462  벽시계
		QueryInstancePtr->ExecuteOneStep(TimeLeft);                         // :482
		if (bAllowEQSTimeSlicing) { TimeLeft -= (FPlatformTime::Seconds() - StepStartTime) - ResultHandlingDuration; } // :573-577
```

| 사실 | 근거 |
|---|---|
| `UEnvQueryManager : UAISubsystem : FTickableGameObject` — `UWorld::Tick` 의 `FTickableGameObject::TickObjects` 에서 틱 | `Classes/EnvironmentQuery/EnvQueryManager.h:207-219`, `Classes/AISubsystem.h:15`, `Runtime/Engine/Private/LevelTick.cpp:1821` |
| 생성자 기본 `MaxAllowedTestingTime = 0.01f, bTestQueriesUsingBreadth = true`; 설정 구조체 `FEnvQueryManagerConfig` 기본 `0.003f / false`(UPROPERTY(config)) | `Private/EnvironmentQuery/EnvQueryManager.cpp:182-183`, `Classes/EnvironmentQuery/EnvQueryManager.h:36, 42` |
| `bAllowEQSTimeSlicing` 정적 플래그(기본 true) | `Private/EnvironmentQuery/EnvQueryManager.cpp:57, 1127` |
| 아이템 반복자도 `FPlatformTime::Seconds() < Deadline` 으로 중단 | `Classes/EnvironmentQuery/EnvQueryTypes.h:1221` |
| `RunInstantQuery(Request, RunMode)` / `RunInstantQuery(QueryInstance)` 는 `while (!IsFinished()) ExecuteOneStep(...)` 로 같은 호출 안에서 완료 → 프레임 분할 없음 | `Private/EnvironmentQuery/EnvQueryManager.cpp:305-335` |
| 실행 모드 `SingleResult, RandomBest5Pct, RandomBest25Pct, AllMatching`; 랜덤 모드는 `UAISystem::GetRandomStream().RandHelper(NumBestItems)` | `Classes/EnvironmentQuery/EnvQueryTypes.h:187-190`, `Private/EnvironmentQuery/EnvQueryInstance.cpp:735-750` |
| `UAISystem::RandomStream` 은 정적; `SeedRandomStream(int32)` 로 시드 가능; CDO 생성 시 `-FixedSeed` 가 없으면 `FDateTime::Now().GetTicks()` 로 시드 | `Classes/AISystem.h:137, 278-279`, `Private/AISystem.cpp:35-43` |
| EQS 소스 전체에서 난수 호출은 위 1곳뿐(제너레이터/테스트에 난수 없음) | `Private/EnvironmentQuery`, `Classes/EnvironmentQuery` grep |

→ 시간 분할은 "언제 끝나는가"만 바꾸고 "무엇을 고르는가"는 바꾸지 않지만, 결과가 도착하는 프레임이 달라지면 이후 행동이 달라지므로 결정론이 깨진다. 결정론 시뮬레이션에서는 `RunInstantQuery` + 비랜덤 모드 + 고정 시드가 필요하다.

#### 4-4. AIPerception 의 갱신 주기와 무작위성

```cpp
// Private/Perception/AIPerceptionSystem.cpp:160-241 (발췌)
CurrentTime = World->GetTimeSeconds();                                          // :176 월드 시간
if (NextStimuliAgingTick <= CurrentTime) { AgeStimuli(PerceptionAgingRate + AgingDt); NextStimuliAgingTick = CurrentTime + PerceptionAgingRate; } // :184-190
for (UAISense* SenseInstance : Senses) bNeedsUpdate |= SenseInstance->ProgressTime(DeltaSeconds); // :193-196
```

| 사실 | 근거 |
|---|---|
| 퍼셉션 시스템은 `UAISubsystem`(FTickableGameObject), `PerceptionAgingRate = 0.3f` 기본 | `Classes/Perception/AIPerceptionSystem.h:30, 39`, `Private/Perception/AIPerceptionSystem.cpp:44` |
| 감각 갱신은 `TimeUntilNextUpdate -= DeltaSeconds` 카운터(타이머 매니저 아님) | `Classes/Perception/AISense.h:48, 84-94, 128-134` |
| `UAIPerceptionComponent` 는 자체 `TickComponent` 가 없고(시스템이 밀어줌) 타이머도 안 씀; 시스템의 타이머는 폰 등록용 `SetTimerForNextTick` 1곳 | `Classes/Perception/AIPerceptionComponent.h` grep, `Private/Perception/AIPerceptionSystem.cpp:89` |
| `UAISense_Sight` 예산: `MaxTracesPerTick = 6`, `MaxAsyncTracesPerTick = 10`, `MaxTimeSlicePerTick = 0.005`(5ms), `MinQueriesPerTimeSliceCheck = 40` | `Private/Perception/AISense_Sight.cpp:46-48, 136-140` |
| 시각 갱신 루프가 `FPlatformTime::Seconds() > TimeSliceEnd` 로 중단(벽시계) → 기기 성능에 따라 처리 개수 달라짐 | `Private/Perception/AISense_Sight.cpp:296, 328-336` |
| 시각은 `AsyncLineTraceByChannel`(비동기, 결과 다음 프레임) 과 동기 `LineTraceSingleByChannel` 두 경로 | `Private/Perception/AISense_Sight.cpp:556, 571` |
| 구형 `UPawnSensingComponent` 는 타이머 + `FMath::SRand/FRand` 사용(비결정) — 사용 금지 대상 | `Private/Perception/PawnSensingComponent.cpp:72, 89, 341` |

→ AIPerception 은 월드 시간 기반이라 대체로 결정적이나, 시각 감각의 벽시계 분할과 비동기 트레이스 때문에 "정확히 같은 프레임에 같은 자극" 을 보장하지 못한다.

### 5) DetourCrowd 의 틱과 결정론

```cpp
// Private/Navigation/CrowdManager.cpp:231-323 (발췌)
void UCrowdManager::Tick(float DeltaTime)
{ int32 NumActive = DetourCrowd->cacheActiveAgents();                           // :239
  for (auto It = ActiveAgents.CreateIterator(); It; ++It) PrepareAgentStep(...); // :243-251 (TMap 순회)
  DetourCrowd->updateStepCorridor / updateStepPaths / updateStepProximityData / updateStepNextMovePoint
             / updateStepSteering / updateStepAvoidance / updateStepMove / updateStepOffMeshVelocity  // :257-295
  for (...) if (CrowdComponent->IsCrowdSimulationEnabled()) ApplyVelocity(CrowdComponent, AgentIndex); // :300-313
```

| 사실 | 근거 |
|---|---|
| 호출 경로: `UWorld::Tick → NavigationSystem->Tick(DeltaSeconds) → CrowdManager->Tick(DeltaSeconds)` (틱 그룹 앞, 고정 순서) | `Runtime/Engine/Private/LevelTick.cpp:1643-1648`, `Runtime/NavigationSystem/Private/NavigationSystem.cpp:1633, 1831-1835` |
| `FCrowdTickHelper`(FTickableGameObject) 는 에디터 디버그 그리기 전용 | `Classes/Navigation/CrowdManager.h:166-176`, `Private/Navigation/CrowdManager.cpp:103-110` |
| `ADetourCrowdAIController` 는 `PathFollowingComponent` 를 `UCrowdFollowingComponent` 로 교체하는 것뿐 | `Private/DetourCrowdAIController.cpp:9` |
| `UCrowdFollowingComponent::SetCrowdSimulationState(ECrowdSimulationState)` 로 에이전트별 시뮬레이션 켜고 끔 | `Classes/Navigation/CrowdFollowingComponent.h:84, 107` |
| 설정: `MaxAgents, MaxAgentRadius, MaxAvoidedAgents, MaxAvoidedWalls, NavmeshCheckInterval, PathOptimizationInterval, bResolveCollisions` (config=Engine) | `Classes/Navigation/CrowdManager.h:179, 294-331` |
| `Navmesh/Private/DetourCrowd`, `Navmesh/Public/DetourCrowd` 에 `rand(` 호출 없음; Detour 전체에서 난수는 `DetourNavMeshQuery.cpp`(findRandomPoint 계열) 뿐 | grep 결과, `Runtime/Navmesh/Private/Detour/DetourNavMeshQuery.cpp:337, 377, 396-397` |
| `CrowdManager.cpp` 에 `FPlatformTime` 사용 없음 | grep 결과 |
| 경로 탐색은 `AAIController::FindPathForMoveRequest → NavSys->FindPathSync` (동기) | `Private/AIController.cpp:906` |

→ 군중 회피는 고정 스텝·같은 에이전트 등록 순서·같은 내비메시면 결정적이다. 위험은 `TMap ActiveAgents` 순회 순서(등록 순서·해시에 의존)와, 폰 이동을 실제로 적용하는 `UCharacterMovementComponent` 쪽(별도 조사 영역).

---

## 프로젝트 적용 시사점

현재 상태(근거): 몬스터/동료 캐릭터는 `AutoPossessAI = PlacedInWorldOrSpawned`, `AIControllerClass = AAIController::StaticClass()` 로 두뇌 없는 기본 컨트롤러만 붙어 있고(`Source/TDGame/Characters/TDMonsterCharacter.cpp:10-11`, `TDCompanionCharacter.cpp:10-11`), 상태이상 빙결이 `Brain->PauseLogic` 을 호출한다(`Source/TDGame/Combat/TDCombatComponentStatus.cpp:434-442`). 템플릿 `ATwinStickAIController` 는 `UStateTreeAIComponent` 를 쓴다(`Source/TDGame/Variant_TwinStick/AI/TwinStickAIController.cpp:10`). 모듈 의존성에 `AIModule, NavigationSystem, StateTreeModule, GameplayStateTreeModule` 이 이미 있다(`Source/TDGame/TDGame.Build.cs:17-21`). 테스트 픽스처는 `UWorld::CreateWorld` + `World->Tick(LEVELTICK_All, 0.02f)` 고정 스텝이다(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:35-38, 71-77`).

### 쓸 수 있는 것

1. **BT 를 쓰기로 한다면 "수동 틱 모드"로 쓴다.** `PrimaryComponentTick.bCanEverTick = false` 로 두고 자체 `UTDMonsterBrainScheduler`(가칭) 가 LOD(세부 수준) 버킷별로 `TickComponent(FixedDt × N, LEVELTICK_All, nullptr)` 를 호출한다. 이렇게 하면 (a) 엔진 틱 그룹과 무관하게 몇 마리를 어떤 주기로 돌릴지 코드로 결정할 수 있고, (b) 헤드리스 시뮬레이션에서 같은 호출 순서 → 같은 결과가 보장되며, (c) 테스트 픽스처와 동일한 방식이다(엔진 테스트 스위트가 검증된 선례). 주의: `FTimerManager` 를 쓰는 노드(`BTTask_PlayAnimation`)는 쓰지 말거나 픽스처처럼 `TimerManager.Tick` 을 함께 돌린다.
2. **BT 를 텍스트로 정의하려면 자체 DSL(도메인 특화 언어) → `FBTBuilder` 식 C++ 조립 경로가 유일하게 현실적이다.** 엔진에 BT 전용 텍스트 직렬화기는 없고 범용 T3D 는 에디터 전용이다. JSON/YAML 로 "노드 클래스 이름 + 속성 + 자식" 을 적고 런타임에 `NewObject<UBTNode>(Tree, Class)` + `Children.AddZeroed` 로 조립하면 생성형 AI 가 에디터 없이 트리를 읽고 고칠 수 있다. 블랙보드도 `UBlackboardData::Keys` 로 코드 생성 가능.
3. **서비스·Wait 의 `RandomDeviation` 은 0 으로 강제**하고, 난수가 필요한 노드는 시뮬레이션 시드에서 파생한 `FRandomStream` 을 쓰는 자체 서브클래스로 만든다. `UAISystem::SeedRandomStream(Seed)` 도 시뮬레이션 시작 시 호출한다.
4. **EQS 는 `RunInstantQuery` + `SingleResult/AllMatching` 만 쓴다.** 대량 몬스터에서는 EQS 자체보다 자체 공간 질의(격자/그리드 기반 근접 검색)가 싸고 결정적이다.
5. **DetourCrowd 는 결정론 측면에서 쓸 만하다.** 등록 순서를 몬스터 스폰 순서로 고정하고, `MaxAgents` 를 한 화면 최대치에 맞춘다.

### 피할 것

1. `SetComponentTickInterval` 로 BT 주기를 조절하려는 시도(덮어써짐).
2. 블루프린트 BT 노드(컴포넌트마다 UObject 복제, GC 부담).
3. `UAISense_Sight`/`UPawnSensingComponent` 를 결정론 시뮬레이션에 포함하는 것(벽시계·비동기 트레이스·난수). 시뮬레이션용 "인식" 은 거리·각도·팀 기반 순수 함수로 대체한다.
4. 몬스터마다 `AAIController` 액터를 두는 것(매 프레임 액터 틱 + `UpdateControlRotation`). 수백~수천 마리면 두뇌 컴포넌트를 폰에 직접 붙이거나, 컨트롤러 액터 틱을 끄는 방안을 검토한다. 단 `PauseLogic` 등 기존 코드가 `AAIController::GetBrainComponent()` 에 의존하므로 인터페이스를 맞춰야 한다.
5. "빙결 = PauseLogic" 만으로 AI 가 완전히 멈춘다고 가정하는 것(서비스 틱은 계속 돈다, `BehaviorTreeComponent.cpp:1760-1775` 는 `bIsPaused` 를 검사하지 않음).

### 대안 비교를 위한 판단 재료

- BT 의 "노드 기반이라 생성형 AI 가 다루기 어렵다"는 우려는 **데이터 형식의 문제이지 실행 모델의 문제가 아니다.** 실행 모델(수동 틱·인스턴스 메모리·결정론)은 위와 같이 통제 가능하다. 반면 텍스트 직렬화기가 없으므로 DSL 을 직접 만들어야 하는 비용이 붙는다.
- 자체 C++ 유한 상태 기계/유틸리티 AI 는 이 DSL 비용 없이 처음부터 텍스트(데이터 테이블/JSON) 로 정의할 수 있고, 인스턴스 메모리도 POD(Plain Old Data, 단순 자료) 구조체로 두어 UObject 를 전혀 만들지 않을 수 있다. BT 를 택할 실질적 이득은 "엔진 디버거(비주얼 로거·BT 디버거)와 기존 노드 라이브러리(MoveTo 등)" 뿐이다.
- StateTree 는 별도 조사 영역이지만, 본 조사에서 확인된 BT 의 제약(간격 덮어쓰기, BP 노드 복제, 텍스트 직렬화 부재)이 StateTree 에도 있는지 같은 기준으로 비교해야 한다.

---

## 미확인·미해결 질문

1. **`UStateTreeAIComponent`/`UStateTreeComponent` 의 수동 틱 가능성과 직렬화 형태** — 본 조사 범위 밖(다른 조사 영역). BT 와 같은 기준(틱 함수 우회, 인스턴스 데이터 구조, 난수·타이머 사용)으로 비교 필요.
2. **`UCharacterMovementComponent` 가 군중 속도를 적용할 때의 결정론** — `UCrowdFollowingComponent::ApplyCrowdAgentVelocity` 이후 실제 이동은 이동 컴포넌트 틱(물리 서브스텝, 바닥 탐색 트레이스) 에서 일어난다. 고정 스텝 물리(`Chaos` 고정 틱, `bTickPhysicsAsync` 등) 조건은 미확인.
3. **`ExecuteOneStep(double TimeLimit)` 내부에서 시간 초과 시 "부분 처리된 아이템 집합"이 다음 프레임 재개 때 동일한 순서로 이어지는지** — `EnvQueryTypes.h:1221` 의 반복자 마감 로직만 확인했고 재개 지점 저장 방식(`CurrentTestStartingItem`)의 전체 흐름은 읽지 않았다. `RunInstantQuery` 를 쓰면 무관.
4. **`FMath::FRandRange` 의 전역 시드 고정(`FMath::RandInit`)이 다른 엔진 시스템(파티클·애니메이션) 과 공유되는 정도** — 결정론 시뮬레이션에서 전역 시드 고정이 충분한지, 아니면 BT 서비스 서브클래스로 스트림을 분리해야 하는지는 실행 실험이 필요.
5. **`UBehaviorTreeComponent::TickComponent` 를 수동 호출할 때 `bWantsInitializeComponent` 초기화(`InitializeComponent`) 를 월드 등록 없이 안전하게 마칠 수 있는지** — 엔진 테스트는 `RegisterComponentWithWorld(World)` 를 호출하므로(`MockAI_BT.cpp:55-56`) "월드 없는 순수 헤드리스" 는 미확인. 현재 픽스처가 월드를 만들므로 실무상 문제는 아님.
6. **`ActiveAgents`(TMap) 순회 순서가 동일 등록 순서에서 프로세스 간에도 동일한지** — `TMap` 은 `FSetElementId` 순서(삽입 순서, 삭제 후 재사용 슬롯) 를 따르므로 스폰/사망 순서가 같으면 같다고 판단되나 코드로 검증하지는 않았다.
7. **T3D 로 내보낸 BT 를 `ImportObjectProperties` 로 재구성해 실행까지 되는지** — 에디터 전용 경로라 우선순위가 낮아 실험하지 않았다.
