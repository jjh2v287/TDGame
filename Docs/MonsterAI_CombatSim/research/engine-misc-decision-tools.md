# 엔진의 기타 의사결정·데이터 도구: Chooser, SmartObjects, GameplayBehaviors, StateGraph, AIAssistant, MLflow, PlainProps, 비주얼 로거

조사 대상 엔진: Unreal Engine 5.8 (`C:/Program Files/Epic Games/UE_5.8/Engine`). 아래 경로는 모두 이 엔진 루트 기준 상대 경로이며, `파일:줄` 은 헤더 선언 또는 구현 위치다. 프로젝트 파일은 `C:/Project/TDGame` 기준이다. 웹 자료는 사용하지 않았고, 모든 근거는 엔진 소스 코드다. 확인하지 못한 항목은 "미확인" 으로 표기했다.

---

## 결론 요약 (설계 결정에 바로 쓸 수 있는 문장들)

| 번호 | 결론 | 근거 |
|---|---|---|
| R1 | **Chooser 는 애니메이션 전용이 아니라 "컨텍스트 구조체 값 → 행 필터/점수 → 결과(UObject/UClass/출력 컬럼)" 를 고르는 범용 데이터 테이블이며, 공격 선택에 쓸 수 있다.** 결과 타입 열거형에 `ObjectResult / ClassResult / NoPrimaryResult(출력 전용)` 이 있고, 출력 컬럼(`FOutputFloatColumn`, `FOutputStructColumn`)으로 컨텍스트 구조체에 값을 되쓴다. | `Plugins/Chooser/Source/Chooser/Public/IHasContext.h:41-47`, `OutputFloatColumn.h:13-21`, `OutputStructColumn.h:32-38` |
| R2 | **Chooser 평가는 Randomize 컬럼을 제외하면 순수 함수다. Randomize 컬럼은 `FChooserEvaluationContext::RandomStream` 이 설정돼 있으면 그 스트림을, 아니면 전역 `FMath::FRandRange` 를 쓴다.** 결정론이 필요하면 반드시 컨텍스트에 `FRandomStream*` 을 넣어야 한다. | `IObjectChooser.h:124` (`FRandomStream* RandomStream = nullptr;`), `Private/RandomizeColumn.cpp:111,163` |
| R3 | **Chooser 테이블은 `.uasset` 이므로 텍스트 우선 원칙과 어긋난다. C++ 에서 `NewObject<UChooserTable>` 후 `ColumnsStructs` 와 `CookedResults` 를 채우면 코드 전용 사용은 가능하지만, `ResultsStructs` 는 에디터 전용 데이터라 두 빌드에서 채우는 배열이 다르다.** 자체 JSON 테이블 + C++ 점수 함수가 더 단순하다. | `Chooser.h:147-150`(WITH_EDITORONLY_DATA `ResultsStructs`), `Chooser.h:200-205`(`CookedResults`, `ColumnsStructs`), `Private/Chooser.cpp:617,842`(평가 시 `CookedResults` 사용), `Private/Chooser.cpp:302-323`(`PopulateCookedData`) |
| R4 | **SmartObjects 는 정식(Beta/Experimental 플래그 없음) 런타임 플러그인이고, 서브시스템은 클라이언트 넷모드가 아닌 모든 월드에 생성되므로 `UWorld::CreateWorld` 헤드리스 월드에서도 동작한다.** 다만 몬스터 전투 판단 자체에는 필요 없고, "환경 상호작용" 과 "슬롯 점유(claim) 토큰" 용도에만 가치가 있다. | `Plugins/Runtime/SmartObjects/SmartObjects.uplugin:15`, `Source/SmartObjectsModule/Private/SmartObjectSubsystem.cpp:3248-3259` |
| R5 | **GameplayBehaviors 는 베타(`IsBetaVersion: true`)이며 `UGameplayBehavior::Trigger` 가 블루프린트 이벤트(`K2_OnTriggered*`) 디스패치를 전제로 설계됐다.** C++ 서브클래스에서 `Trigger` 를 override 하면 코드 전용 사용은 가능하나, 이 계층은 SmartObject 연계 없이는 이점이 거의 없다. | `Plugins/Experimental/GameplayBehaviors/GameplayBehaviors.uplugin:15`, `Source/GameplayBehaviorsModule/Public/GameplayBehavior.h:60-61,101-116`, `Private/GameplayBehavior.cpp:84-105` |
| R6 | **StateGraph 는 게임 AI 상태 기계가 아니다. 이름과 달리 "의존성 DAG(방향 비순환 그래프) 기반 1회성 태스크 실행기" 이며, 노드는 `NotStarted→Blocked→Started→Completed` 로만 진행하고 전이·루프·재진입이 없다. 타임아웃은 `FDateTime::UtcNow` 벽시계와 `FTSTicker` 를 쓴다.** 관리자 모듈은 클라이언트 접속/서버 등록 흐름용이다. 몬스터 AI 에 쓰면 안 된다. | `Plugins/Experimental/StateGraph/Source/StateGraph/Public/StateGraph.h:4-14,59-73,145,387-397`, `Private/StateGraph.cpp:345-470`, `StateGraphManager/Public/*.h` |
| R7 | **AIAssistant 는 Epic Developer Assistant 웹앱(`https://dev.epicgames.com/community/assistant/embedded`)을 에디터 안 웹브라우저에 내장하는 에디터 전용·실험 플러그인이다.** 슬레이트 위젯 컨텍스트를 JSON 으로 어시스턴트에 보내고, 툴은 `GetProjectContext / GetDockedContext` 두 개뿐이다. "콘솔 명령 실행" 기능은 없다(`AIAssistantConsole` 은 `ai.assistant.uefn` 콘솔 변수 구독기). 우리 생성형 AI 파이프라인에는 못 쓴다. | `Plugins/Experimental/AIAssistant/AIAssistant.uplugin:16,22-24`, `Source/AIAssistant/Private/AIAssistantConfig.cpp:18`, `AIAssistantToolset.h:78-90`, `AIAssistantConsole.cpp:15` |
| R8 | **생성형 AI 파이프라인의 실제 접점은 AIAssistant 가 아니라 `ToolsetRegistry`(+`ModelContextProtocol` 어댑터)다.** `UToolsetDefinition` 서브클래스의 `meta=(AICallable)` 정적 UFUNCTION 이 곧바로 MCP(Model Context Protocol, 모델 컨텍스트 프로토콜) 툴이 된다. 프로젝트 uproject 는 이미 `ModelContextProtocol` 과 `AllToolsets` 를 켜 두었다. | `Plugins/Experimental/ToolsetRegistry/Source/ToolsetRegistry/Public/ToolsetRegistry/ToolsetDefinition.h:12-14,32`, `ToolsetRegistry.h:66,79,86`, `Plugins/Experimental/ModelContextProtocol/Source/ModelContextProtocolEditor/Private/ModelContextProtocolToolsetRegistryAdapter.cpp:21-36`, `C:/Project/TDGame/TDGame.uproject:23-31` |
| R9 | **MLflow 플러그인은 코드 모듈이 없는 "파이썬 패키지 설치 선언(mlflow-skinny 2.20.2)" 이며, Learning Agents 의 PPO/모방학습 트레이너 설정(`bUseMLflow`, `MLflowTrackingUri`)을 통해 LearningCore 파이썬 트레이너가 `mlflow.set_tracking_uri` 로 실험 추적을 한다.** 강화학습을 할 때 켜면 된다. | `Plugins/Experimental/MLflow/MLflow.uplugin:6,17-38`, `Plugins/Experimental/LearningAgents/Source/LearningAgentsTraining/Public/LearningAgentsPPOTrainer.h:251-257`, `Private/LearningAgentsPPOTrainer.cpp:462-463`, `Plugins/Experimental/LearningCore/Content/Python/learning_core/train_common.py:1028-1036,1070` |
| R10 | **PlainProps 는 "새 직렬화 스택 프로토타입"(실험)으로, 공개 API 는 바이너리 저장/로드/델타(`SaveStruct`, `SaveStructDelta`, `LoadStruct`, `FWriter`)뿐이고 JSON/텍스트 문서 형식은 공개 API 에 없다. 엔진 내 소비자 모듈도 없다.** 시뮬레이션 입출력 저장에는 쓰지 않는다. | `Plugins/Experimental/PlainProps/PlainProps.uplugin:4,9`, `Source/Public/PlainPropsSave.h:56-58`, `PlainPropsLoad.h:27-28`, `PlainPropsWrite.h:22-32`, `PlainPropsPrint.h:29-47` |
| R11 | **비주얼 로거는 파일 기록(`.bvlog`, `FPaths::ProjectLogDir()`)과 되읽기(`FVisualLoggerHelpers::Serialize` 의 `Ar.IsLoading()` 분기, 에디터 LogVisualizer 로드)를 모두 지원하고, `FVisualLogDevice` 를 직접 추가해 JSONL 등 자체 싱크로 보낼 수 있다.** 단, 에디터 프로세스(`GIsEditor`)에서는 타임스탬프가 `FApp::GetCurrentTime()` 벽시계라 결정론이 깨지므로 `SetGetTimeStampFunc` 로 시뮬레이션 스텝 시간을 주입해야 한다. 헤드리스 고정 스텝 루프에서는 `FTSTicker` 기반 Tick 이 돌지 않을 수 있으므로 `Flush()` 를 직접 호출한다. | `Source/Runtime/Engine/Classes/VisualLogger/VisualLoggerBinaryFileDevice.h:11`, `Private/VisualLogger/VisualLoggerBinaryFileDevice.cpp:41-75`, `Private/VisualLogger/VisualLoggerTypes.cpp:677`, `Private/VisualLogger/VisualLogger.cpp:343-363,597(헤더),712,882-889`, `Public/VisualLogger/VisualLogger.h:597` |
| R12 | **GameplayDebugger 는 `APlayerController` 와 화면 캔버스를 전제로 한 온스크린 런타임 도구(비-Shipping)이며 파일 기록 API 가 없다.** 헤드리스 시뮬레이션 근거 자료로는 부적합, 플레이 중 확인용으로만 사용. | `Source/Runtime/GameplayDebugger/GameplayDebugger.Build.cs:49-64`, `Public/GameplayDebuggerCategory.h:56,59,68,71` |
| R13 | **UE 5.8 의 `FJsonObjectConverter` 는 `FInstancedStruct` 를 `_structType` 키를 가진 평면 객체로 왕복 직렬화하고, `FInstancedPropertyBag` 도 지원한다. TMap 은 키가 문자열로 변환 가능해야 하며(문자열/이름/열거형/ExportTextItem 있는 구조체), 그렇지 않으면 `"Unparsed Key N"` 으로 손상된다. TSet 은 JSON 배열이다.** | `Source/Runtime/JsonUtilities/Private/JsonObjectConverter.cpp:175-226,265-300,304-310,716-770,910-946` |

---

## 상세 조사

### 1) 각 플러그인의 정체·상태·런타임/코드 전용 가능 여부

| 플러그인 | 무엇을 하는가 | 상태 표기 (uplugin) | 런타임 모듈 | 코드 전용 사용 |
|---|---|---|---|---|
| Chooser | "Chooser 와 Proxy Table 로 동적 에셋 선택 로직을 만든다" — 컨텍스트 값으로 행을 필터/채점해 결과를 고르는 테이블 | 플래그 없음(정식), `EnabledByDefault: false` — `Plugins/Chooser/Chooser.uplugin:6,13` | `Chooser`, `ProxyTable` 이 Runtime — `Chooser.uplugin:33-35,48-50` | 가능(주의점 R3). 런타임 의존: Core, CoreUObject, Engine, GameplayTags, AnimationCore, AnimGraphRuntime, BlendStack, TraceLog, RewindDebuggerRuntimeInterface — `Source/Chooser/Chooser.Build.cs:19-27` |
| SmartObjects | "게임 월드를 채우는 앰비언트 라이프 지원" — 월드 내 상호작용 지점(슬롯) 등록·검색·점유 | `IsBetaVersion: false`, Experimental 키 없음 — `Plugins/Runtime/SmartObjects/SmartObjects.uplugin:15` | `SmartObjectsModule` Runtime PreDefault — `:20-22` | 가능. 정의는 `USmartObjectDefinition`(DataAsset) 이지만 C++ 로 생성 가능 |
| GameplayBehaviorSmartObjects | "GameplayBehavior 를 기본 런타임 행동으로 쓰는 SmartObject 플러그인" | `IsExperimentalVersion: true` — `Plugins/Runtime/GameplayBehaviorSmartObjects/GameplayBehaviorSmartObjects.uplugin:15` | Runtime — `:19-21` | 가능하나 AIController 전제(AITask) |
| GameplayBehaviors | "AI 에이전트용 캡슐화된 fire-and-forget 행동" | `IsBetaVersion: true` — `Plugins/Experimental/GameplayBehaviors/GameplayBehaviors.uplugin:15` | `GameplayBehaviorsModule` Runtime — `:19-21` | 가능(Trigger override) |
| StateGraph | "범용 상태 기계 관리 클래스" | 폴더는 Experimental, uplugin 에 Experimental/Beta 키 없음(미확인), `EnabledByDefault: false` — `Plugins/Experimental/StateGraph/StateGraph.uplugin:6,10` | `StateGraph`, `StateGraphManager` Runtime PostConfigInit — `:13-20` | 순수 C++(UObject 아님)이지만 용도 불일치(R6) |
| AIAssistant | Epic Developer Assistant 웹앱 내장 | `IsExperimentalVersion: true`, `EnabledByDefault: false` — `Plugins/Experimental/AIAssistant/AIAssistant.uplugin:16,19` | **Editor 전용** — `:22-24` | 불가(에디터 UI) |
| MLflow | "선택적 mlflow 파이썬 패키지와 의존성" | `IsExperimentalVersion: true` — `Plugins/Experimental/MLflow/MLflow.uplugin:15` | 모듈 없음, `PythonRequirements` 만 — `:24-38` | 파이썬 트레이너 측에서만 의미 |
| PlainProps / PlainPropsUObject / PlainPropsEngine | "새 직렬화 스택 프로토타입" (+CoreUObject/Engine 바인딩) | 모두 `IsExperimentalVersion: true` — `Plugins/Experimental/PlainProps/PlainProps.uplugin:4,9`, `PlainPropsUObject.uplugin:4,9`, `PlainPropsEngine.uplugin:4,9` | Runtime — `PlainProps.uplugin:15-16` | 가능하나 공개 API 가 바이너리 전용(R10) |
| VisualLogger | 엔진 내장(플러그인 아님), `Source/Runtime/Engine/Public/VisualLogger/` | `ENABLE_VISUAL_LOG = PLATFORM_DESKTOP && !NO_LOGGING && UE_ENABLE_DEBUG_DRAWING` — `Source/Runtime/Engine/Public/EngineDefines.h:19`; `UE_ENABLE_DEBUG_DRAWING = !(Shipping||Test) || WITH_EDITOR` — `:15` | Engine 모듈 | 가능(매크로 `UE_VLOG*`) |
| GameplayDebugger | 엔진 내장 `Source/Runtime/GameplayDebugger` | `WITH_GAMEPLAY_DEBUGGER` 는 `Target.bUseGameplayDebugger`, Shipping 이면 0 — `GameplayDebugger.Build.cs:49-64` | Runtime(비-Shipping) | 가능하나 화면 전제(R12) |

### 2) Chooser: 애니메이션 외 의사결정(공격 선택) 활용 가능성과 결정론

**평가 진입점과 컨텍스트**

```cpp
// Plugins/Chooser/Source/Chooser/Public/Chooser.h:214-216
static CHOOSER_API FObjectChooserBase::EIteratorStatus EvaluateChooser(FChooserEvaluationContext& Context, const UChooserTable* Chooser, FObjectChooserBase::FObjectChooserIteratorCallback Callback);
static CHOOSER_API FObjectChooserBase::EIteratorStatus IterateChooser(FChooserEvaluationContext& Context, const UChooserTable* Chooser, FObjectChooserBase::FObjectChooserIteratorCallback Callback);
// Plugins/Chooser/Source/Chooser/Public/IObjectChooser.h:68-124
struct FChooserEvaluationContext {
    void AddObjectParam(UObject* Param);                 // :77
    template <class T> void AddStructParam(T& Param);   // :104-108 (참조로 보관, 수명 주의 :102-103)
    TArray<FStructView, TInlineAllocator<4>> Params;    // :114
    FRandomStream* RandomStream = nullptr;              // :124
};
```

- 컨텍스트는 UObject 또는 **임의의 USTRUCT** 를 넣을 수 있다(`AddStructParam`). 애니메이션 관련 타입은 `ChooserTypes.h` 의 `FChooserPlayerSettings`(`:57`) 등 별개 구조체일 뿐, 평가기는 컨텍스트 타입을 가리지 않는다.
- 컬럼은 `FChooserColumnBase`(`IChooserColumn.h:34`) 의 `Filter(...)`(`:42,45`) 로 행 인덱스 집합을 좁히고, `HasCosts()`(`:50`) 가 참인 컬럼은 점수(비용)를 더한다. 종류: `FEnumColumn`(`EnumColumn.h:97-117`), `FGameplayTagColumn`(`GameplayTagColumn.h:54,90`), `FBoolColumn`, `FFloatRangeColumn`, `FFloatDistanceColumn`(입력 float 와의 차이를 비용으로 — `FloatDistanceColumn.h:40-41,56,90`), `FObjectClassColumn`, `FRandomizeColumn`, 출력 컬럼(`OutputFloat/Enum/Bool/Name/GameplayTag/Struct/Object`).
- 결과 종류: `EObjectChooserResultType { ObjectResult, ClassResult, NoPrimaryResult }` — `IHasContext.h:41-47`. 즉 "공격 정의 DataAsset(UObject)", "어빌리티 클래스(UClass)", "출력 컬럼으로 공격 ID 열거형만 되쓰기" 세 방식 모두 가능.
- 평가 루프(`Private/Chooser.cpp:584-820`): 컬럼 순서대로 `Filter`(`:694`) → 선택 행에 대해 모든 컬럼 `SetOutputs`(`:720-737`) → 결과 `ChooseMulti`(`:743`) → 행이 없으면 `FallbackResult`(`:766`, 선언 `Chooser.h:149-151`).

**결정론**

- 필터/비용 컬럼은 입력 값만 본다. 행 순서는 `FChooserIndexArray` 인덱스 순서로 안정적이다.
- 유일한 난수원은 `FRandomizeColumn::Filter` — `Private/RandomizeColumn.cpp:111,163`:
  `const float RandomNumber = Context.RandomStream ? Context.RandomStream->RandRange(0.f, TotalWeight) : FMath::FRandRange(0.0f, TotalWeight);`
  → 시뮬레이션에서는 반드시 `Context.RandomStream = &SimRandomStream;` 을 넣는다. "최근 선택 반복 억제" 상태는 `FChooserRandomizationContext`(`RandomizeColumn.h:38-41`) 로 외부 구조체에 저장되므로, 이것도 시뮬레이션 상태의 일부로 리셋/저장해야 한다.
- 비용 동률 판정 `EqualCostThreshold`(`RandomizeColumn.cpp:80,118`) 는 float 비교이므로 플랫폼 간 부동소수점 차이는 남는다(같은 빌드·같은 머신에서는 동일).

**속성 바인딩(리플렉션) 비용**

- 컬럼 입력은 `FChooserPropertyBinding { TArray<FName> PropertyBindingChain; int ContextIndex; }`(`ChooserPropertyAccess.h:82-90`) 로 컨텍스트 구조체 멤버를 이름 경로로 가리킨다. `Compile()`(`Private/ChooserPropertyAccess.cpp:76`) 이 오프셋 체인 `FCompiledBinding` 을 만들며 에디터 전용이 아니다(`:161-165`). 단 에디터에서는 콘솔 변수 `Choosers.UseCompiledPropertyChainsInEditor`(기본 false, `:26-30`) 때문에 `FindFProperty` 이름 검색 경로(`:585-593`)를 탄다 → **에디터 자동화 테스트에서 성능 측정 시 이 변수를 1 로 켜야 패키지 빌드와 같은 경로**가 된다.

**디버그·추적**

- `TRACE_CHOOSER_EVALUATION`(`ChooserTrace.h:36-44`, `CHOOSER_TRACE_ENABLED` 조건) 이 Unreal Insights/Rewind Debugger 로 평가 기록을 보낸다. 에디터 전용 `bEnableDebugTesting`(`Chooser.h:104`).

**코드 전용 생성의 함정(R3)**

- 에디터 빌드에서 행 목록은 `ResultsStructs`(`Chooser.h:147-150`, `WITH_EDITORONLY_DATA`), 평가는 항상 `CookedResults`(`Private/Chooser.cpp:617`) 를 읽는다. `CookedResults` 는 `Serialize` 저장 시 `PopulateCookedData`(`:212-216, 302-323`) 로 채워진다. 따라서 저장하지 않는 트랜지언트 테이블을 C++ 로 만들면 **직접 `CookedResults` 와 `ColumnsStructs` 를 채우고 `Compile()`(`:173`) 을 호출**해야 한다. 이 경로는 엔진 자체 테스트가 없어(`Plugins/Chooser` 에 Tests 폴더 없음) 스파이크 검증이 필요하다(미해결 Q1).

### 3) SmartObjects + GameplayBehaviors: 환경 상호작용 기반 몬스터 행동의 가치

**핵심 API**

```cpp
// Plugins/Runtime/SmartObjects/Source/SmartObjectsModule/Public/SmartObjectSubsystem.h
class USmartObjectSubsystem : public UWorldSubsystem                                   // :295
FSmartObjectRequestResult FindSmartObject(const FSmartObjectRequest&, const FConstStructView UserData) const; // :464
bool FindSmartObjects(const FSmartObjectRequest&, TArray<FSmartObjectRequestResult>&, const FConstStructView UserData) const; // :473
bool CanBeClaimed(const FSmartObjectSlotHandle&, ESmartObjectClaimPriority = Normal) const; // :622
FSmartObjectClaimHandle MarkSlotAsClaimed(const FSmartObjectSlotHandle&, ESmartObjectClaimPriority, const FConstStructView UserData = {}); // :631
const USmartObjectBehaviorDefinition* MarkSlotAsOccupied(const FSmartObjectClaimHandle&, TSubclassOf<USmartObjectBehaviorDefinition>); // :665
bool MarkSlotAsFree(const FSmartObjectClaimHandle&);                                    // :684
bool UpdateSmartObjectTransform(const FSmartObjectHandle, const FTransform&);           // :429
```

- 요청 필터 `FSmartObjectRequestFilter`(`SmartObjectRequestTypes.h:86-119`): `UserTags`, `ActivityRequirements`(FGameplayTagQuery), `BehaviorDefinitionClasses`, `bShouldIncludeClaimedSlots`, `Predicate`(TFunction). 요청 `FSmartObjectRequest { FBox QueryBox; Filter; }`(`:131-148`).
- 정의 `USmartObjectDefinition : UDataAsset`(`SmartObjectDefinition.h:257-258`), 슬롯 `FSmartObjectSlotDefinition`(`:102-103`) 의 `ActivityTags`(`:199`)·`SelectionPreconditions`(WorldCondition, `:207`), 객체 전체 `Preconditions`(`:277`). 행동 정의 기반 클래스 `USmartObjectBehaviorDefinition` 은 빈 추상 마커다(`:65-69`) — 즉 "슬롯을 잡으면 무엇을 할지" 는 전적으로 파생 클래스와 사용자 코드 몫.
- 헤드리스 가능성: `ShouldCreateSubsystem` 은 `OuterWorld->IsNetMode(NM_Client) == false` 면 생성(`Private/SmartObjectSubsystem.cpp:3248-3259`). `UWorld::CreateWorld` 로 만든 게임 월드에서도 생성된다. 런타임 인스턴스 초기화는 `bRuntimeInitialized || GetWorldRef().IsGameWorld()` 조건(`:3507`).

**GameplayBehaviors 계층**

- `UGameplayBehavior : UObject, IGameplayTaskOwnerInterface`(`GameplayBehavior.h:35-36`), `virtual bool Trigger(AActor& Avatar, const UGameplayBehaviorConfig*, AActor* SmartObjectOwner)`(`:60`), `EndBehavior`(`:61`), `InstantiationPolicy`(`:157`), `NeedsInstance`(`:133`). 구현(`Private/GameplayBehavior.cpp:84-105`)은 클래스에 `K2_OnTriggered/Pawn/Character` 가 있는지 보고(`:31-40`) 블루프린트 이벤트를 호출한다.
- 정적 진입 `UGameplayBehaviorSubsystem::TriggerBehavior(const UGameplayBehaviorConfig&, AActor&, AActor*)`(`GameplayBehaviorSubsystem.h:33-34`), `StopBehavior`(`:35`). 내장 행동: `UGameplayBehavior_AnimationBased`(몽타주 재생, `GameplayBehavior_AnimationBased.h:57`), `GameplayBehavior_BehaviorTree`(`Public/AI/`).
- SmartObject 연결: `UGameplayBehaviorSmartObjectBehaviorDefinition { TObjectPtr<UGameplayBehaviorConfig> GameplayBehaviorConfig; }`(`GameplayBehaviorSmartObjectBehaviorDefinition.h:16,22`), 사용 태스크 `UAITask_UseGameplayBehaviorSmartObject::UseSmartObjectWithGameplayBehavior(AAIController*, FSmartObjectClaimHandle, ...)`(`AI/AITask_UseGameplayBehaviorSmartObject.h:18,34,44`) — **AAIController 필수**.
- StateTree 연동은 (a) Mass 스키마 전용 태스크(`Plugins/AI/MassAI/Source/MassAIBehavior/Public/Tasks/MassClaimSmartObjectTask.h`, `MassFindSmartObjectTargetTask.h`), (b) 실험 플러그인 GameplayInteractions(`Plugins/Runtime/GameplayInteractions/GameplayInteractions.uplugin:16`) 의 `StateTreeTask_FindSlotEntranceLocation.h`, `GameplayInteractionSmartObjectBehaviorDefinition.h` 뿐이다. 일반 `GameplayStateTree` AI 컴포넌트용 SmartObject 태스크는 5.8 엔진 소스에서 찾지 못했다(미확인 — 프로젝트가 직접 작성해야 한다고 가정).

**가치 판단**

- 가치 있음: (1) 함정·포탑·토템·제단 같은 **환경 오브젝트 사용**, (2) **슬롯 점유(claim)** 를 "플레이어 주변 근접 공격 자리 N개" 토큰으로 쓰기(`UpdateSmartObjectTransform` 으로 이동 객체 추적 가능, `:429`). 점유 우선순위 `ESmartObjectClaimPriority` 로 엘리트 몬스터 우선권 표현 가능.
- 가치 없음/비용: 전투 판단(어떤 공격을 언제) 자체는 SmartObject 와 무관하다. 검색은 공간 해시/옥트리(`SmartObjectHashGrid.h`, `SmartObjectOctree.h`)를 유지해야 하며, 결과 정렬의 결정론은 미확인(Q2). 수백 마리가 매 틱 `FindSmartObjects` 를 부르면 비용이 크므로 **판단 주기(예: 0.2~0.5초)** 에만 질의해야 한다.
- 결론: 1차 설계에서는 제외하고, 근접 슬롯 토큰은 C++ 자체 구현(배열 + 각도 슬롯)이 더 싸고 결정론적이다. 환경 상호작용이 실제 기획에 들어올 때 SmartObjects 만 도입(GameplayBehaviors 계층은 건너뛰고 `USmartObjectBehaviorDefinition` 파생 + StateTree 태스크 직접 작성).

### 4) StateGraph: 실제 API 와 게임 AI 상태 기계 적합성

```cpp
// Plugins/Experimental/StateGraph/Source/StateGraph/Public/StateGraph.h
// :4-14  "generic state machine framework ... graph nodes with dependencies ... async tasks ... Max execution depth of one node at a time ... Timeout ... ini"
class FStateGraphNode : public TSharedFromThis<FStateGraphNode>, public FNoncopyable  // :41
    enum class EStatus : uint8 { NotStarted, Blocked, Started, Completed, TimedOut };  // :59-73
    virtual void Start() = 0;              // :145
    UE_API virtual void Complete();        // :114
    TSet<FName> Dependencies;              // :140
class FStateGraph : public TSharedFromThis<FStateGraph>, public FNoncopyable          // :203
    UE_API bool AddNode(const FStateGraphNodeRef& Node);   // :293
    UE_API bool AddDependencies(FName NodeName, const TArrayView<const FName> Dependencies); // :378
    UE_API void Run(); UE_API void Reset(); UE_API void Pause();  // :387,392,397
```

- 실행 의미(`Private/StateGraph.cpp:345-470`): `Run()` 은 모든 노드를 순회하며 `NotStarted/Blocked` 노드의 `CheckDependencies()`(의존 노드가 전부 `Completed` 인지, `:67-79`) 가 참이면 `Start()` 를 호출한다. 노드는 나중에 `Complete()` 를 부르고, 그 안에서 다시 `Run()` 을 호출해 후속 노드를 깨운다(`:113-121`). 함수 래퍼 노드 `FStateGraphNodeFunction::Start` 는 델리게이트에 완료 콜백을 넘긴다(`:176-196`).
- 시간: `FDateTime::UtcNow()` 벽시계(`:347,365`)로 시작/지속 시간을 재고, 타임아웃은 `FTSTicker`(`:372`) 로 예약한다. 월드 시간·고정 스텝과 무관하다.
- 상태 개념: 노드는 "한 번 완료되는 태스크" 이고 `Completed → 다른 상태` 전이는 `Reset()` 전체 리셋뿐이다. 순환 전이·재진입·계층 상태·틱 갱신이 없다. 즉 이것은 **로그인/접속 절차 같은 1회성 비동기 파이프라인용 DAG 실행기** 다. 관리자 모듈의 구체 클래스도 `ClientJoinManager`, `PreLoginAsyncManager`, `RegisterServerManager`, `RestartServerManager`(`StateGraphManager/Public/`) 이며, 엔진 내 다른 사용처는 없다(grep 결과 AnimGraph 의 `AnimationStateGraph` 는 이름만 유사한 무관 클래스).
- 결론: 몬스터 AI 상태 기계로 부적합. 굳이 쓸 곳을 찾자면 "시뮬레이션 배치 잡 파이프라인(월드 생성 → 스폰 → N회 실행 → 결과 저장)" 정도인데, 벽시계 타임아웃 때문에 그것도 결정론 도구로는 부적절하다.

### 5) AIAssistant: 정체와 생성형 AI 파이프라인 활용 가능성

- 에디터 전용 모듈(`AIAssistant.uplugin:22-24`), 의존 플러그인 `PythonScriptPlugin`, `EditorScriptingUtilities`, `ToolsetRegistry`(`:30-38`). Build 의존에 `UnrealEd`, `HTTP`, `Kismet`, `BlueprintGraph`, `LevelEditor`, `PythonScriptPlugin`, `Slate`(`Source/AIAssistant/AIAssistant.Build.cs:34-73`).
- 기본 URL `https://dev.epicgames.com/community/assistant/embedded`(`Private/AIAssistantConfig.cpp:18`) 를 내장 브라우저(`SAIAssistantWebBrowser`)에 띄우고, `FWebApi` 는 "Epic Developer Assistant API 의 부분집합"(`AIAssistantWebApi.h:21-24`) 을 JavaScript 객체 `window.eda`(`AIAssistantWebApi.cpp:12-13`) 로 호출한다(`createConversation`, `addMessageToConversation`, `registerOnConversationUpdate`, `updatePendingFileList` 등 `:101-172`). 즉 **LLM 은 Epic 클라우드 쪽에 있고 엔진은 UI 셸**이다.
- 슬레이트 조회: `FSlateQueryStructuredContext`(`AIAssistantSlateQuerier.h:31-68`) 가 창 이름·에디터 모드·툴팁·커서 아래 텍스트·위젯 경로를 JSON 으로 만들어 어시스턴트에 질문한다(`QueryAIAssistantAboutSlateWidget`, `:26`). 화면 요소 "설명 요청" 용이지 자동 조작용이 아니다.
- 툴: `UAIAssistantToolset : UToolsetDefinition` 의 `AICallable` 정적 함수 `GetProjectContext()`, `GetDockedContext()` 두 개(`AIAssistantToolset.h:78-90`).
- 콘솔: `AIAssistantConsole.*` 는 콘솔 변수 `ai.assistant.uefn` 구독기(`AIAssistantConsole.cpp:15-63`) 다. 콘솔 명령 실행 툴은 없다.
- 파이썬: 주석에 `window.ue.aiassistantsubsystem.executepythonscriptviajavascript(code)` 가 언급되나(`AIAssistantWebBrowser.h:23-28`) `UAIAssistantSubsystem` 헤더에 보이는 UFUNCTION 은 `ShowContextMenuViaJavaScript` 뿐(`AIAssistantSubsystem.h:37-38`) — 파이썬 실행 함수 존재는 미확인.
- 부수 장치: 트랜잭션 버퍼 관리자(`AIAssistantTransactionBufferManager.h:13-64`, 어시스턴트 편집의 Undo 스택 분리)와 파일 락 관리자(`AIAssistantFileLockManager.h:12-28`, 읽기 전용 잠금).
- **대안이자 실제 해법: ToolsetRegistry + ModelContextProtocol.** `UToolsetDefinition` 의 정적 `meta=(AICallable)` UFUNCTION 이 툴이 되고(`ToolsetDefinition.h:12-14`, 검증 `IsFunctionAICallable` `:32`), `FToolsetRegistry::RegisterToolset/ExecuteTool`(`ToolsetRegistry.h:66,79,86`) 을 MCP 에디터 모듈이 `FToolsetRegistryToolAdapter` 로 감싼다(`ModelContextProtocolToolsetRegistryAdapter.cpp:21-36`). 엔진에는 이미 `AutomationTestToolset`(`DiscoverTests/ListTests/RunTests/RunTestsByFilter/GetTestResults/GetTestStatus/StopTests` — `Plugins/Experimental/Toolsets/AutomationTestToolset/Source/AutomationTestToolset/Public/AutomationTestToolset.h:40-92`), `GASToolsets`(AttributeSet/GameplayCue/AbilitySystemInspector), `GameplayTagsToolset`, `LogsToolset`, `StateTreeToolset` 등이 있고 `AllToolsets` 집합 플러그인(`Plugins/Experimental/Toolsets/AllToolsets/AllToolsets.uplugin:6`) 이 프로젝트에 켜져 있다(`TDGame.uproject:30-31`). ToolsetRegistry 모듈 자체는 Editor 타입(`ToolsetRegistry.uplugin:22-23`)이므로 MCP 툴은 에디터 프로세스에서만 노출된다.

### 6) MLflow: 실험 추적 용도인가, Learning Agents 와 연동되는가

- 플러그인에는 C++/파이썬 모듈이 없고 `PythonRequirements` 로 `mlflow-skinny==2.20.2` 와 의존 패키지(pydantic, opentelemetry, databricks-sdk 등)만 선언한다(`MLflow.uplugin:24-38`); `PythonMLPackages` 플러그인(PyTorch 자동 설치, `PythonMLPackages.uplugin:5-6`) 에 의존한다(`:17-21`). `Content/Python/Lib` 에는 라이선스 `.tps` 파일 하나뿐이다.
- Learning Agents 연동: `FLearningAgentsPPOTrainerSettings::bUseMLflow`, `MLflowTrackingUri`(`LearningAgentsPPOTrainer.h:251-257`), 모방학습 `LearningAgentsImitationTrainer.h:135-141`, Flow Matching 트레이너에도 동일 키. C++ 는 이를 트레이너 설정 JSON 에 `UseMLflow`, `MLflowTrackingUri` 로 넣고(`Private/LearningAgentsPPOTrainer.cpp:462-463`), LearningCore 파이썬 `train_common.py` 가 `import mlflow; mlflow.set_tracking_uri(...)`(`:1028-1036`) 후 `if config['UseMLflow']:`(`:1070`) 에서 지표를 기록한다. TensorBoard 도 `:993` 에서 지원.
- 결론: 실험 추적 전용이며 Learning Agents 학습 시 켜기만 하면 된다. 우리 헤드리스 밸런스 시뮬레이션 결과(승률·시간·피해)를 MLflow 에 넣는 것은 별도 파이썬 스크립트로 가능하나 엔진 기능은 아니다.

### 7) PlainProps: 구조체 직렬화 용도와 시뮬레이션 입출력 적합성

- 공개 API: `SaveStruct(const void*, FBindId, const FSaveContext&)`, `SaveStructDelta(...)`, `SaveStructDeltaIfDiff(...)`(`PlainPropsSave.h:56-58`), `LoadStruct(void* Dst, FByteReader Src, FStructSchemaId, const FLoadBatch&)`(`PlainPropsLoad.h:27-28`), `FWriter::WriteSchemas/WriteMembers(TArray64<uint8>&, ...)`(`PlainPropsWrite.h:22-32`), 리플렉션 매크로 `PP_REFLECT_*`(`PlainPropsCtti.h:42-74`). 즉 **스키마 + 바이너리 배치** 형식이며 델타(기준값 대비 차이) 저장이 특징.
- 텍스트: `PlainPropsPrint.h`/`PlainPropsParse.h` 는 기본 타입·열거형 문자열화만 공개(`Print.h:29-47`, `Parse.h:11-27`). 텍스트 왕복은 테스트 옵션 `ERoundtrip::TextMemory/TextStable`(`PlainPropsUObject/Source/Public/PlainPropsRoundtripTest.h:27-28`) 로만 보이고 구현은 Private(`PlainPropsInternalText.h`). JSON 은 디버그 문자열화용 `JsonObjectGraph` 의존뿐(`PlainPropsUObject.Build.cs:19`).
- 소비자: 엔진 `Source/Runtime` 및 다른 플러그인의 Build.cs 에 `PlainProps` 의존이 없다(grep 결과 없음). 엔진 바인딩은 테스트 커맨드릿 `UTestPlainPropsCommandlet`(`PlainPropsEngine/Source/Public/PlainPropsCommandlets.h:10-11`) 용.
- 결론: 시뮬레이션 입력(장비/물약/몬스터 구성)과 결과는 사람과 LLM 이 읽는 텍스트여야 하므로 JSON(`FJsonObjectConverter`) 이 맞고, 대용량 리플레이만 바이너리(`FMemoryWriter` + UStruct `SerializeTaggedProperties` 또는 자체 POD 배열) 로 쓴다. PlainProps 는 API 안정성이 없어 채택하지 않는다.

### 8) VisualLogger / GameplayDebugger: 판단 로그 기록·재생과 헤드리스 가능성

**기록 매크로와 스위치**

- `UE_VLOG(LogOwner, Category, Verbosity, Fmt, ...)` 등(`VisualLogger.h:43-118`), 이벤트 `EventLog(...)`(`:636-642`), `IsRecording()`(`:575`), `Redirect(From, To)`(`:587`, 자식 컴포넌트 로그를 액터로 합치기).
- 전역 상태: `SetIsRecording`(`Private/VisualLogger/VisualLogger.cpp:916`), `SetIsRecordingToFile`(`:1289-1340`), 생성자에서 `FVisualLoggerBinaryFileDevice` 자동 등록(`:872`), 커맨드라인 `-EnableAILogging` 이면 기록+파일 저장 동시 시작(`:876-880`), `GEngine->bEnableVisualLogRecordingOnStart`(`Source/Runtime/Engine/Classes/Engine/Engine.h:1916`). 콘솔 `VISLOG record|stop|disableallbut`(`:1374-1407`): 에디터에서는 메모리 기록만, 비에디터에서는 파일 기록.
- 엔트리 구조 `FVisualLogEntry { double TimeStamp; FVector Location; TArray<FVisualLogEvent> Events; TArray<FVisualLogLine> LogLines; TArray<FVisualLogStatusCategory> Status; TArray<FVisualLogShapeElement> ElementsToDraw; TArray<FVisualLogHistogramSample> HistogramSamples; TArray<FVisualLogDataBlock> DataBlocks; }`(`VisualLoggerTypes.h:216-235`) — 텍스트·상태표·도형·히스토그램·바이너리 블록을 한 프레임 단위로 묶는다.

**파일 기록과 되읽기**

- 바이너리 장치는 `FPaths::ProjectLogDir()` 아래 임시 파일에 쓰다가 종료 시 `"<세션ID>_<이름>_<시간>.bvlog"` 로 옮긴다(`VisualLoggerBinaryFileDevice.cpp:41-75`; 확장자 정의 `VisualLoggerBinaryFileDevice.h:11`). `[VisualLogger] FrameCacheLenght` ini 로 플러시 주기 설정(`:15-17`).
- 되읽기: `FVisualLoggerHelpers::Serialize(FArchive&, TArray<FVisualLogEntryItem>&)`(`VisualLoggerTypes.h:344`, 구현 `VisualLoggerTypes.cpp:677`, 로드 분기 `:400,470,627`) 가 커스텀 버전(`GVisualLoggerVersion`, `VisualLogger.cpp:1364`) 을 포함해 로딩을 지원하고, 에디터 LogVisualizer 가 `.bvlog` 를 연다(`Source/Developer/LogVisualizer/Private/SVisualLogger.cpp:60-61,659`). 즉 **헤드리스에서 기록 → 에디터에서 사람이 스크럽 재생** 흐름이 성립한다.
- 자체 싱크: `FVisualLogDevice` 인터페이스(`VisualLoggerTypes.h:307-334`, `Serialize(...)` 순수가상 `:327`) 구현 후 `AddDevice` 로 등록하면 모든 `FlushEntry`(`VisualLogger.cpp:729-737`) 가 장치로 전달된다 → JSONL(줄 단위 JSON) 로 LLM 분석용 로그를 병행 생성할 수 있다.

**헤드리스·결정론 주의점**

- 월드 탐색 `GetWorldForVisualLogger`(`VisualLogger.cpp:227-252`): `GEngine->GetWorldFromContextObject` → 에디터면 PlayWorld/에디터 월드 → 그래도 없으면 `GWorld`. `UWorld::CreateWorld` 월드의 액터를 LogOwner 로 넘기면 첫 경로에서 찾는다.
- 타임스탬프 `GetTimeStampForObject`(`:335-363`): (1) `GetTimeStampFunc` 가 있으면 그것, (2) **`GIsEditor` 면 `FApp::GetCurrentTime()` 벽시계 차이**(`:343-352`), (3) 아니면 `World->TimeSeconds`(`:362-363`). 자동화 테스트는 에디터 프로세스에서 돌므로 (2) 가 적용돼 같은 입력에도 로그 시각이 달라진다 → `FVisualLogger::Get().SetGetTimeStampFunc([](const UObject*){ return SimStep * FixedDt; })`(`VisualLogger.h:597`) 로 고정.
- 플러시: 프레임 단위 플러시는 `FTSTicker` 코어 티커의 `Tick`(`:882-889`, `:1103`) 에서 일어난다. 테스트 픽스처처럼 `World->Tick` 만 직접 호출하는 루프에서는 코어 티커가 돌지 않을 수 있으므로 스텝 끝마다 `FVisualLogger::Get().Flush()`(`:712`) 를 호출한다(엔진 Ticker 동작 여부는 픽스처에서 확인 필요, Q3).
- 빌드 제한: Shipping/Test 에서는 `UE_DEBUG_RECORDING_USING_VLOG = 0`(`VisualLoggerDefines.h:18`) 이라 기록 코드가 제거된다. 시뮬레이션 툴은 Development 빌드로 돌린다.
- 이름 충돌: `bForceUniqueLogNames = true`(`:856`) 가 기본이라 오너 이름에 `[UniqueID]` 가 붙는다(`:1098`). 여러 실행을 비교하려면 `SetUseUniqueNames(false)`(`:959-961`) 또는 안정적인 액터 이름 부여가 필요하다.

**GameplayDebugger**

- 런타임 모듈이나 `WITH_GAMEPLAY_DEBUGGER` 는 `Target.bUseGameplayDebugger` 에 따르고 Shipping 은 0(`GameplayDebugger.Build.cs:49-64`; UBT `ModuleRules.cs:1666-1698`).
- 카테고리 API: `virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor)`(`GameplayDebuggerCategory.h:56`), `DrawData(APlayerController*, FGameplayDebuggerCanvasContext&)`(`:59`), `AddTextLine`(`:68`), `AddShape`(`:71`), 복제 데이터팩 `SetDataPackReplication`(`:139`). 플레이어 컨트롤러·캔버스 전제이며 파일 출력 API 없음. SmartObjects 도 자체 카테고리를 제공(`GameplayDebuggerCategory_SmartObject.h`).
- 결론: 플레이 중 몬스터 판단 상태 확인용으로 커스텀 카테고리를 하나 두는 것은 좋지만, 시뮬레이션 근거 자료는 VisualLogger + 자체 JSONL 로 만든다.

### 9) JSON 유틸: `FJsonObjectConverter` 의 UStruct ↔ JSON 지원 범위와 한계

**진입점**(`Source/Runtime/JsonUtilities/Public/JsonObjectConverter.h`)

```cpp
static bool UStructToJsonObjectString(const UStruct*, const void*, FString& Out, int64 CheckFlags=0, int64 SkipFlags=0, int32 Indent=0, const CustomExportCallback* =nullptr, bool bPrettyPrint=true); // :140
template<typename T> static bool JsonObjectStringToUStruct(const FString&, T* Out, int64 CheckFlags=0, int64 SkipFlags=0, bool bStrictMode=false, FText* OutFailReason=nullptr, const CustomImportCallback* =nullptr); // :313
using CustomExportCallback = TDelegate<TSharedPtr<FJsonValue>(FProperty*, const void*)>; // :80
enum class EJsonObjectConversionFlags { None, SkipStandardizeCase, WriteTextAsComplexString, SuppressClassNameForPersistentObject }; // :30-49
```

**지원 타입과 동작**(`Private/JsonObjectConverter.cpp`)

| 타입 | 내보내기 | 들여오기 | 비고 |
|---|---|---|---|
| 열거형(`FEnumProperty`/바이트 열거형) | 이름 문자열 `:109` | 문자열/숫자 `:593` | |
| `FText` | 문자열(또는 복합 형식) `:148` | `:819-856` | |
| `TArray` | 배열 `:160` | `:677` | |
| `TSet` | **JSON 배열** `:175-189` | 배열 `:775-810` | |
| `TMap` | JSON 객체. 키는 `TryGetString` 성공 시 그 문자열, 아니면 `KeyProp->ExportTextItem_Direct`; 빈 문자열이면 `"Unparsed Key N"` 오류 `:190-226` | 객체의 각 키 문자열을 `JsonValueToFProperty` 로 키 속성에 들여옴 `:716-770` | 열거형/FName 키는 기본 camelCase 변환(`:213-219`, `SkipStandardizeCase` 로 끔). **구조체 키는 ExportTextItem/ImportTextItem 이 있어야 왕복**(예: `FGameplayTag` 가능, 일반 USTRUCT 키 불가) |
| `FInstancedStruct` | 내부 필드를 평면 객체로 + `"_structType": <UScriptStruct 경로>` `:265-280`; 비어 있으면 `{}` | 인스턴스가 비어 있으면 `_structType` 을 `FindObject<UScriptStruct>` 로 해석해 `InitializeAs` 후 필드 적용 `:910-946`; 미해석 시 실패 `:919-931` | 타입이 **이미 로드**돼 있어야 함(`FindObject`, 로드 아님). 배열 안 요소도 같은 경로 |
| `FInstancedPropertyBag` | 값만 평면 객체 `:283-300` | **백이 사전 초기화(스키마 보유)** 돼 있어야 함 `:948-` | |
| ExportTextItem 있는 구조체(FGameplayTag, FSoftObjectPath 등) | 문자열 `:304-310` | `ImportTextItem` → 실패 시 `ImportText_Direct` `:1054-1065` | |
| `FLinearColor`/`FColor`/`FDateTime`/`FGuid` | | 문자열 특례 `:977-1045` | `FDateTime` 은 ISO8601 `ExportCallback_WriteISO8601Dates`(`.h:89`) |
| `UObject*` | 인스턴스드면 값 복사 + `_ClassName`, 아니면 경로 문자열 `:320-352` | | 시뮬레이션 데이터에는 UObject 참조 대신 `FSoftObjectPath`/식별자 사용 권장 |
| `TOptional` | `:354` | `:441` | |
| 그 외(FVector 등 ExportTextItem 없는 구조체) | 중첩 객체(필드별) | 필드별 | |

- `FInstancedStruct` 자체도 `ExportTextItem/ImportTextItem`(`Source/Runtime/CoreUObject/Public/StructUtils/InstancedStruct.h:159-160`, 구현 `Private/StructUtils/InstancedStruct.cpp:294-356`, ImportText 는 `LoadObject` 로 타입 로드 `:349`) 을 가지므로 T3D/텍스트 속성 문자열 형태로도 저장된다. JSON 변환기는 이보다 앞서 `_structType` 특례를 적용한다(`:266` 분기가 `:304` 보다 먼저).
- 결정론 관련: 부동소수점 문자열화·재파싱의 비트 동일성은 미확인(Q4). 시드는 `int64`/`uint32` 정수로, 검증용 해시는 결과 구조체를 바이너리 직렬화한 값으로 계산하는 편이 안전하다.

---

## 프로젝트 적용 시사점 (TDGame 에서 무엇을 어떻게 쓰고 무엇을 피할지)

**쓴다**

1. **JSON 입출력 규약(즉시)**: 시뮬레이션 입력(장비·물약·버프 × 몬스터 종류·마릿수)과 결과 구조체는 `USTRUCT` 로 정의하고 `FJsonObjectConverter` 로 왕복한다. 규칙: (a) `TMap` 키는 `FName`/열거형/`FGameplayTag` 만, 그 외는 `TArray<FPair구조체>`; (b) 다형 항목(버프 조건, 몬스터 AI 파라미터)은 `FInstancedStruct` + `_structType` 을 쓰되 해당 구조체 타입이 모듈 로드로 이미 존재하는지 확인; (c) UObject 참조 대신 `FSoftObjectPath` 또는 문자열 ID; (d) 실행 시 `SkipStandardizeCase` 플래그로 원래 대소문자 유지(LLM 이 C++ 필드명과 1:1 대조하기 쉬움). 근거 R13.
2. **VisualLogger 를 시뮬레이션 리플레이 근거로(1차)**: `FTDScopedCombatWorld` 고정 스텝 루프에서 `SetGetTimeStampFunc` 로 스텝 시간을 주입, 스텝마다 `Flush()`, 자체 `FVisualLogDevice` 로 JSONL 을 쓰고 동시에 `.bvlog` 도 남겨 에디터에서 사람이 재생. 몬스터 판단은 `EventLog` + `Status` 카테고리(현재 상태·선택 공격·점수)로 기록. 근거 R11.
3. **ToolsetRegistry + MCP 로 생성형 AI 작업 파이프라인(2차)**: TDGame 에디터 모듈에 `UToolsetDefinition` 서브클래스를 두고 `AICallable` 정적 함수로 "헤드리스 시뮬레이션 실행(JSON in/out)", "몬스터 AI 정의 파일 검증", "리플레이 JSONL 요약" 을 노출. 엔진 `AutomationTestToolset` 의 `RunTestsByFilter/GetTestResults` 로 자동화 테스트도 MCP 에서 직접 돌린다. AIAssistant 플러그인은 켜지 않는다. 근거 R7, R8.
4. **Chooser(선택)**: 기획자가 에디터에서 공격 선택 규칙을 만지길 원하는 시점에만 도입. 도입 시 `Context.RandomStream` 주입, `FChooserRandomizationContext` 를 시뮬레이션 상태에 포함, 에디터 테스트에서 `Choosers.UseCompiledPropertyChainsInEditor 1`. 컨텍스트 구조체(`FTDAttackSelectContext { 거리, 체력비, 쿨다운 태그, 최근 공격 }`)를 하나 정의하면 C++ 점수표와 Chooser 를 같은 입력으로 교차 검증할 수 있다. 근거 R1~R3.
5. **MLflow**: Learning Agents 강화학습을 시작할 때 `bUseMLflow=true`, 로컬 `mlflow server` URI 설정. 근거 R9.

**피한다**

- **StateGraph**: 상태 기계가 아니고 벽시계 기반(R6).
- **AIAssistant**: 에디터 웹 셸, 프로그래밍 가능한 접점 없음(R7).
- **PlainProps**: 실험 바이너리 프로토타입, 소비자 없음(R10).
- **GameplayDebugger 를 시뮬레이션 기록에 쓰기**: 화면 전용(R12). 플레이 중 확인용 카테고리는 나중에 추가.
- **GameplayBehaviors 계층**: 블루프린트 이벤트 디스패치 전제의 베타 계층(R5). 환경 상호작용이 필요해지면 SmartObjects 만 쓰고 행동은 StateTree 태스크/C++ 로 직접 구현.

**사용자가 놓쳤을 수 있는 고려사항(이 조사에서 드러난 것)**

- 에디터 프로세스에서 도는 자동화 테스트는 VisualLogger 시각이 벽시계이므로 "같은 입력 → 같은 로그" 를 만들려면 타임스탬프 훅이 필수(R11).
- Chooser 는 기본 난수원이 전역 `FMath` 라서 시드 제어 없이 쓰면 밸런스 툴의 재현성이 깨진다(R2).
- MCP 툴은 ToolsetRegistry 가 Editor 모듈이라 에디터 프로세스에서만 노출된다. 헤드리스 커맨드릿에서 LLM 이 직접 호출하려면 별도 CLI(커맨드릿 + JSON 파일) 경로가 필요하다(R8).
- SmartObject 슬롯 점유는 "근접 몬스터 N마리만 플레이어를 둘러싸게" 하는 토큰 시스템으로 재해석할 수 있으나, 결정론과 비용 면에서 C++ 자체 토큰이 낫다(R4).

---

## 미확인·미해결 질문

- **Q1 (Chooser 코드 전용 생성)**: 저장 없이 `NewObject<UChooserTable>` 로 만든 테이블에 `CookedResults`/`ColumnsStructs` 를 채우고 `Compile()` 만 호출하면 에디터 빌드에서 `EvaluateChooser` 가 정상 동작하는지 — 엔진 테스트가 없어 스파이크 테스트 필요. (`Private/Chooser.cpp:173-208, 617`)
- **Q2 (SmartObjects 결과 순서)**: `FindSmartObjects` 의 결과 배열 순서가 공간 해시 순회 순서에 의존해 실행 간 달라질 수 있는지 미확인. 결정론이 필요하면 호출 측에서 핸들/거리로 정렬.
- **Q3 (VisualLogger 플러시)**: `FTDScopedCombatWorld` 처럼 `World->Tick` 만 부르는 루프에서 `FTSTicker::GetCoreTicker()` 가 돌지 않아 `FVisualLogger::Tick` 이 실행되지 않는지 — 픽스처에서 확인 후 명시적 `Flush()` 채택 여부 결정.
- **Q4 (JSON 부동소수점 왕복)**: `FJsonWriter` 의 double 문자열화가 float/double 값을 비트 단위로 보존하는지 미확인. 시드·검증 해시는 정수로 다룬다.
- **Q5 (AIAssistant 파이썬 실행)**: 주석에 있는 `executepythonscriptviajavascript` 가 실제 UFUNCTION 으로 존재하는지 헤더에서 확인하지 못함(어차피 채택하지 않음).
- **Q6 (StateGraph 상태 표기)**: uplugin 에 Experimental/Beta 키가 없어 공식 상태는 미확인(폴더는 Experimental).
- **Q7 (StateTreeToolset/AIModuleToolset 툴 목록)**: 헤더 grep 으로 `UToolsetDefinition` 파생 클래스를 찾지 못해 노출 툴 목록은 미확인. MCP 툴 목록 조회(`tools/list`)로 확인 권장.
- **Q8 (일반 StateTree 용 SmartObject 태스크)**: Mass 스키마와 GameplayInteractions 외에 `GameplayStateTree` AI 컴포넌트에서 쓰는 SmartObject 태스크가 5.8 에 있는지 못 찾음 — 필요 시 직접 작성 전제.
