# 엔진 내장 실험 HTNPlanner 플러그인의 실체와 사용 가능성

조사 대상: `C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/AI/HTNPlanner` (아래 모든 경로는 이 폴더 기준 상대 경로. `Engine/...`으로 시작하면 엔진 루트 기준).
조사 방법: 플러그인의 모든 `.h/.cpp/.cs/.uplugin`(총 31개 파일, 생성 파일 제외 소스 약 2,100줄)을 직접 읽었고, 플래너 알고리즘은 파이썬으로 동일 로직을 재현해 결함을 재확인했다. 외부 문서·기억이 아니라 코드가 근거다.

용어 풀이
- HTN(Hierarchical Task Network, 계층적 태스크 네트워크): 복합 태스크를 조건이 맞는 "메서드"로 하위 태스크 열로 분해해 원시 태스크(실행 가능한 행동) 열, 즉 계획을 만드는 방식.
- 월드 스테이트(World State): 플래너가 조건 검사와 효과 적용에 쓰는 정수 배열 형태의 세계 상태 요약.
- 백트래킹(Backtracking): 분해가 막히면 직전 분기점으로 되돌아가 다음 메서드를 시도하는 것.
- UHT(Unreal Header Tool): 리플렉션 코드 생성기. `.generated.h` 파일을 만든다.

---

## 결론 요약

1. **이 플러그인은 "플래너 코어 + 빌더"만 있는 미완성 프로토타입이다. 실행기(Executor)와 두뇌 컴포넌트는 껍데기다.** `UHTNBrainComponent`는 생성자 하나만 있고(`Source/HTNPlanner/Private/AI/HTNBrainComponent.cpp:8-11`), 멤버 `FHTNPlanner Planner`(`Public/AI/HTNBrainComponent.h:21`)를 어디서도 사용하지 않는다. `UBrainComponent`의 `StartLogic/StopLogic/TickComponent` 등 어떤 가상 함수도 재정의하지 않는다(`HTNBrainComponent.h:12-22` 전체). 게임플레이 디버거 카테고리 `CollectData/DrawData`도 빈 함수다(`Private/Debug/GameplayDebuggerCategory_HTN.cpp:16-23`). **따라서 "UHTNBrainComponent의 틱·계획 재수립 조건"은 존재하지 않는다(구현 없음).**

2. **플래너 알고리즘에는 실제 결함이 있다.** 백트래킹 시 `RestorePoints`는 월드 스테이트·계획만 복구하고 `TasksToProcess` 스택은 복구하지 않는다(`Public/HTNPlanner.h:28-31, 57-58, 80`, `Private/HTNPlanner.cpp:75-80`). 그 결과 실패한 메서드의 나머지 형제 태스크가 스택에 남아 잘못된 계획에 섞여 들어간다(파이썬 재현: 기대 `[C]` → 실제 `[C, B]`; 5.2절 참조). 또한 루트에서 만족하는 메서드가 하나도 없으면 "성공 + 빈 계획"을 반환해 실패와 구분되지 않는다(`HTNPlanner.cpp:75-81` else 분기 없음, `:97` `bPlanningSuccessful = (TasksToProcess.Num() == 0)`). 이 결함은 기존 테스트가 단일 태스크 메서드만 쓰기 때문에 잡히지 않는다(`Source/HTNTestSuite/Private/HTNTest.cpp:269-317`).

3. **UE4 시절 코드가 사실상 그대로 남아 있다.** `.uplugin` 설명이 "UE4's AI module"이고 `FileVersion: 1`, `VersionName: 0.01`, `IsBetaVersion: true`, `EnabledByDefault: false`다(`HTNPlanner.uplugin:2,4,6,13,15`). 소스에는 파이썬 프로토타입에서 옮기며 남긴 파이썬 주석(`HTNPlanner.cpp:45, 87-93`), 오타가 있는 주석 처리 코드(`HTNDebug.cpp:69` `FString:Printf`), 빈 테스트 3개(`HTNTest.cpp:467-512`), 미완성 목록 주석(`HTNPlanner.cpp:9-12`, `HTNDomain.cpp:9-12`, `HTNTest.cpp:10-14`)이 남아 있다. 5.8에서도 컴파일은 되지만(`Binaries/Win64/UnrealEditor-HTNPlanner.dll`, `Intermediate/Build/Win64/x64/UnrealGame/{Development,Shipping}/HTNPlanner/HTNPlanner.precompiled` 존재) 기능 추가 흔적은 `IntCastChecked`, `EAllowShrinking::No` 같은 엔진 API 추종 수정뿐이다. 의미 있는 마지막 기능 변경 시점은 **미확인**(설치본에는 git 이력이 없음).

4. **런타임 타깃 빌드는 가능하다.** `HTNPlanner` 모듈은 `Type: Runtime`이고 `TargetAllowList/PlatformAllowList`가 없다(`HTNPlanner.uplugin:19-23`). 의존성은 `Core, CoreUObject, Engine, GameplayTags, GameplayTasks, AIModule`(`Source/HTNPlanner/HTNPlanner.Build.cs:6-13`) + 에디터 빌드에서만 `EditorFramework, UnrealEd`(`:17-21`). `HTNTestSuite`는 `UncookedOnly`(`.uplugin:24-27`)이며 `AITestSuite`에 의존(`HTNTestSuite.Build.cs:16-24`).

5. **플래너 코어 자체는 결정론적이다.** `FHTNPlanner::GeneratePlan`(`HTNPlanner.cpp:22-104`)에는 난수·타이머·델타타임·프레임 카운터·월드 참조가 전혀 없고, 입력은 `FHTNDomain`과 `FHTNWorldState`(int32 배열)뿐이다. 메서드 선택은 "첫 번째로 조건을 만족하는 메서드"(`HTNDomain.cpp:239-250`)로 비용·확률 개념이 없다. 단, 사용자 정의 조건/효과 함수 등록은 프로세스 전역 `TArray`에 추가만 하는 방식(`HTNDomain.cpp:149-167`)이라 등록 순서·중복 등록이 연산 ID를 바꾸고 스레드 안전하지 않다.

6. **판단: 제품에 이 플러그인을 그대로 쓰는 것은 부적합하다. 참고용 설계 골격(도메인 컴파일 → 연속 메모리 블록, 정수 월드 스테이트, 조건/효과 함수 포인터 테이블)만 차용하고 TDGame 자체 HTN(또는 다른 모델)을 짜는 것이 낫다.** 근거: 실행기 부재(1), 알고리즘 결함(2), 유지보수 정지(3), 원시 태스크 전제조건 미지원·태스크 파라미터 미지원·반복 상한 없음(`HTNPlanner.cpp:9-12` 자인), 태스크당 메서드/태스크/효과/조건 255개·도메인 64KB·월드 스테이트 키 65,535개 하드 한계(`HTNDomain.h:18-24`, `HTNBuilder.cpp:82-85,107,122-123`). 코어 규모가 약 600줄이라 재작성 비용도 작다.

7. **같은 폴더의 다른 AI 플러그인 상태**: AISupport(정식, 기본 활성), MassAI/MassCrowd/MLAdapter(모두 `IsExperimentalVersion: true`), HTNPlanner(`IsBetaVersion: true`, 설명에 [EXPERIMENTAL]). 표는 5절.

---

## 상세 조사

### 1) 공개 API 전체 목록·도메인 구성법·월드 스테이트·플래너 알고리즘·실행 방식

#### 1-1. 타입 정책과 하드 한계 (`Source/HTNPlanner/Public/HTNDomain.h`)

| 항목 | 정의 | 근거 |
|---|---|---|
| 월드 스테이트 키 | `typedef uint16 FWSKey` (최대 65,535, `InvalidWSKey = MAX_uint16`) | `HTNDomain.h:18, 26` |
| 월드 스테이트 값 | `typedef int32 FWSValue`, 기본값 0 | `HTNDomain.h:19, 25` |
| 태스크 ID | `typedef uint16 FTaskID` — **컴파일된 RawData 안의 바이트 오프셋**이 곧 ID. `InvalidTaskID = FTaskID(INDEX_NONE)`(=0xFFFF) | `HTNDomain.h:20, 27`, `HTNBuilder.cpp:80, 104` |
| 행동 ID / 파라미터 | `uint16 FActionID`, `int32 FActionParameter` | `HTNDomain.h:22-23` |
| 연산 ID | `uint8 FWSOperationID` (조건·효과 함수 테이블 인덱스) | `HTNDomain.h:24` |
| 디버그 이름 | `WITH_HTN_DEBUG = !(UE_BUILD_SHIPPING \|\| UE_BUILD_TEST)`; Shipping/Test에서는 `GetTaskName`이 `NAME_None` | `HTNDomain.h:9`, `HTNDomain.cpp:294-302` |

```cpp
// HTNDomain.h:16-31
namespace FHTNPolicy {
    typedef uint16 FWSKey;  typedef int32 FWSValue;  typedef uint16 FTaskID;
    typedef uint16 FMethodID;  typedef uint16 FActionID;  typedef int32 FActionParameter;
    typedef uint8 FWSOperationID;
    const FTaskID InvalidTaskID = FTaskID(INDEX_NONE);   // 0xFFFF
}
```

#### 1-2. 조건·효과 (`HTNDomain.h:33-118`)

- 조건 열거 `EHTNWorldStateCheck { Less, LessOrEqual, Equal, NotEqual, GreaterOrEqual, Greater, IsTrue }` (`HTNDomain.h:34-45`)
- 효과 열거 `EHTNWorldStateOperation { Set, Increase, Decrease }` (`HTNDomain.h:48-55`)
- 공통 템플릿 `THTNWorldStateOperation<TOperationEnum>`: 멤버 `Operation, KeyLeftHand, KeyRightHand, Value`; 우변은 상수(`SetRHSAsValue`, `:95`) 또는 다른 키(`SetRHSAsWSKey`, `:83`); `IsRHSAbsolute()`(`:101`), `IsValid()`(`:103`). `typedef ... FHTNCondition; typedef ... FHTNEffect;`(`:106-107`)
- 확장점(사용자 정의 연산): 함수 포인터 등록

```cpp
// HTNDomain.h:111-117
typedef bool(*FConditionFunctionPtr)(const FHTNPolicy::FWSValue*, const FHTNCondition& Condition);
typedef void(*FOperationFunctionPtr)(FHTNPolicy::FWSValue*, const FHTNEffect& Effect);
FHTNPolicy::FWSOperationID RegisterCustomCheckType(const FConditionFunctionPtr, const FName& DebugName);
FHTNPolicy::FWSOperationID RegisterCustomOperationType(const FOperationFunctionPtr, const FName& DebugName);
```
구현은 전역 `TArray<FOperationFunctionPtr> OpFunctions; TArray<FConditionFunctionPtr> CheckFunctions;`(`HTNDomain.cpp:105-106`)에 정적 초기화 객체 `FWSOperationsSetUp`이 내장 7개 조건·3개 효과를 채우고(`:112-147`), 등록 함수는 `Add`만 한다(`:149-167`). 조건 검사는 `Values[Condition.KeyLeftHand]`처럼 **경계 검사 없이** 배열 인덱싱한다(`HTNDomain.cpp:45-78`, `:189-197`).

#### 1-3. 컴파일된 도메인 자료구조 (`HTNDomain.h:120-228`)

| 구조체 | 필드 | 근거 |
|---|---|---|
| `FHTNExecutableAction` | `FActionID ActionID; FActionParameter Parameter;` | `:120-136` |
| `FHTNPrimitiveTask : FHTNExecutableAction` | `FHTNEffect* Effects; uint8 EffectsCount;` — **전제조건 필드 없음** | `:138-148` |
| `FHTNMethod` | `FTaskID* Tasks; FHTNCondition* Conditions; uint8 TasksCount; uint8 ConditionsCount;` | `:150-163` |
| `FHTNCompositeTask` | `FHTNMethod* Methods; uint8 MethodsCount; int32 FindSatisfiedMethod(const FHTNWorldState&, int32 StartIndex=0) const;` | `:165-176` |
| `FHTNDomain : TSharedFromThis` | `FTaskID FindTaskID(const FName&) const; bool IsPrimitiveTask(FTaskID) const; bool IsCompositeTask(FTaskID) const; const FHTNPrimitiveTask& GetPrimitiveTask(FTaskID) const; const FHTNCompositeTask& GetCompositeTask(FTaskID) const; FName GetTaskName(FTaskID) const; FTaskID GetRootTaskID() const; bool IsEmpty() const;` protected: `uint8* RawData; TMap<FName,FTaskID> TaskNameMap; FTaskID FirstCompositeTaskID; FTaskID RootTaskID;` | `:178-228` |

`GetPrimitiveTask`는 `*((const FHTNPrimitiveTask*)(&RawData[TaskID]))`(`:195-198`) — 원시 태스크는 RawData 앞부분, 복합 태스크는 뒷부분에 놓이고 `IsPrimitiveTask = TaskID < FirstCompositeTaskID`(`:186-189`)로 구분한다.

#### 1-4. 월드 스테이트 (`HTNDomain.h:230-275`, `HTNDomain.cpp:18-35, 199-219`)

```cpp
struct FHTNWorldState {
    FHTNWorldState(const uint32 WorldStateSize = 128);          // :232, 0으로 초기화(cpp:18-24)
    void Reinit(const uint32 NewWorldStateSize = 128);           // :235
    bool CheckCondition(const FHTNCondition&) const;             // :237
    bool CheckConditions(const FHTNCondition*, int32) const;     // :238-248 (AND)
    void ApplyEffect(const FHTNEffect&);  void ApplyEffects(const FHTNEffect*, int32); // :250-257
    bool GetValue(FWSKey, FWSValue&) const;  FWSValue GetValueUnsafe(FWSKey) const;   // :259-263 (Get은 경계검사, Unsafe는 없음)
    bool SetValue(FWSKey, FWSValue);  void SetValueUnsafe(FWSKey, FWSValue);          // :265-269
protected: TArray<FHTNPolicy::FWSValue> Values;                  // :274
};
```
즉 월드 스테이트는 **int32 고정 배열**이다. 액터 참조·벡터·실수는 직접 담을 수 없고, 정수 핸들이나 양자화 값으로 바꿔 넣어야 한다(테스트 스위트도 `EnemyActor`, `MoveDestination`을 정수 키로만 다룬다: `HTNTestSuite/Private/MockHTN.h:8-21`).

#### 1-5. 빌더 — 도메인을 C++ 코드로 구성하는 방법 (`Source/HTNPlanner/Public/HTNBuilder.h`)

| 빌더 | 공개 API | 근거 |
|---|---|---|
| `FHTNBuilder_PrimitiveTask` | `uint32 ActionID; uint32 Parameter; TArray<FHTNEffect> Effects; void SetOperator(uint32 InActionID, uint32 InParameter=0); template<A,B> SetOperator(A,B); void AddEffect(const FHTNEffect&);` | `:8-27` |
| `FHTNBuilder_Method` | `TArray<FHTNCondition> Conditions; TArray<FName> Tasks; FHTNBuilder_Method(); explicit FHTNBuilder_Method(const FHTNCondition&); FHTNBuilder_Method(const TArray<FHTNCondition>&); void AddTask(const FName&);` | `:29-47` |
| `FHTNBuilder_CompositeTask` | `TArray<FHTNBuilder_Method> Methods; FHTNBuilder_Method& AddMethod(); AddMethod(const FHTNCondition&); AddMethod(const TArray<FHTNCondition>&);` | `:49-67` |
| `FHTNBuilder_Domain` | `TSharedPtr<FHTNDomain> DomainInstance; FHTNBuilder_Domain(); FHTNBuilder_Domain(const TSharedPtr<FHTNDomain>&); void SetRootName(FName); FHTNBuilder_CompositeTask& AddCompositeTask(const FName&); FHTNBuilder_PrimitiveTask& AddPrimitiveTask(const FName&); FHTNBuilder_CompositeTask* FindCompositeTask(const FName&); FHTNBuilder_PrimitiveTask* FindPrimitiveTask(const FName&); GetRootAsCompositeTask(); GetRootAsPrimitiveTask(); bool Compile(); void Decompile(); FString GetDebugDescription() const; FName RootTaskName; TMap<FName,FHTNBuilder_PrimitiveTask> PrimitiveTasks; TMap<FName,FHTNBuilder_CompositeTask> CompositeTasks;` | `:69-98` |

사용 예(테스트 스위트의 실제 코드, `HTNTest.cpp:33-48, 96-100`을 압축):
```cpp
DomainBuilder.SetRootName(TEXT("Root"));
FHTNBuilder_CompositeTask& Root = DomainBuilder.AddCompositeTask(TEXT("Root"));
FHTNBuilder_Method& M0 = Root.AddMethod({ FHTNCondition(EMockHTNWorldState::EnemyHealth, EHTNWorldStateCheck::Greater).SetRHSAsValue(0),
                                          FHTNCondition(EMockHTNWorldState::EnemyActor,  EHTNWorldStateCheck::IsTrue) });
M0.AddTask(TEXT("AttackEnemy"));
FHTNBuilder_PrimitiveTask& Use = DomainBuilder.AddPrimitiveTask(TEXT("UseWeapon"));
Use.SetOperator(EMockHTNTaskOperator::UseWeapon, EMockHTNWorldState::EnemyActor);
Use.AddEffect(FHTNEffect(EMockHTNWorldState::EnemyHealth, EHTNWorldStateOperation::Decrease).SetRHSAsValue(1));
DomainBuilder.Compile();   // HTNBuilder.cpp:42-223
```
`Compile()` 동작(`HTNBuilder.cpp:42-223`): 필요한 바이트를 합산(`:48-67`) → `FMemory::Malloc` 한 덩어리(`:69-72`) → 원시 태스크·효과 배치(`:75-95`) → `FirstCompositeTaskID` 기록(`:97`) → 복합 태스크·메서드·조건·태스크 ID 자리 배치(`:99-141`) → 이름→ID 패치, 없는 태스크면 경고 후 실패(`:154-187`) → 루트 이름을 못 찾으면 첫 복합 태스크(없으면 ID 0)로 **조용히 대체**(`:192-210`). 수량은 `IntCastChecked<uint8>`이라 **태스크당 메서드/하위태스크/조건/효과 255개 초과 시 체크 실패(크래시)**(`:85, 107, 122-123`). `AddCompositeTask/AddPrimitiveTask`는 `FindOrAdd`라 같은 이름 재호출은 기존 항목을 반환한다(`:22-30`). `Decompile()`(`:225-288`)은 컴파일된 도메인에서 빌더를 복원한다. `GetDebugDescription()`은 효과 목록을 인덱스 1부터 출력해 **첫 효과가 항상 누락**되는 결함이 있다(`:327`).

#### 1-6. 플래너 (`Source/HTNPlanner/Public/HTNPlanner.h`, `Private/HTNPlanner.cpp`)

```cpp
struct FHTNResult { TArray<FTaskID> TaskIDs; TArray<FHTNExecutableAction> ActionsSequence; void Reset(); void Set(const FHTNDomain&, const TArray<FTaskID>&); }; // HTNPlanner.h:8-19
struct FHTNRestorePoint { FHTNWorldState WorldState; TArray<FTaskID> Plan; int32 NextMethod; FTaskID ActiveTask; }; // :21-42
struct FHTNPlanner {
    bool GeneratePlan(const FHTNDomain& Domain, const FHTNWorldState& InitialWorldState, FHTNResult& Result, const FName& StartTaskName = NAME_None); // :52
    const FHTNWorldState& GetWorldState() const;   // :53 (계획 끝 시점의 시뮬레이션 월드 스테이트)
protected: FHTNRestorePoint CurrentState; TArray<FHTNRestorePoint> RestorePoints; int32 RestorePointsStored; TArray<FTaskID> TasksToProcess; // :72-80
};
```
알고리즘(`HTNPlanner.cpp:22-104`) — 반복형(비재귀) 깊이 우선 분해, 순서 백트래킹:
1. 시작 태스크 = 루트 또는 이름 지정(`:26`). 없으면 경고 후 `false`(`:32-37`).
2. `TasksToProcess` 스택에서 태스크를 꺼내(`:47`) 복합이면 `FindSatisfiedMethod(WorldState, NextMethod)`로 **첫 만족 메서드**를 고르고(`:58`, `HTNDomain.cpp:239-250`) 복원점을 쌓은 뒤(`:63`) 하위 태스크를 역순으로 push(`:70-73`).
3. 만족 메서드가 없고 복원점이 있으면 pop 해서 `NextMethod = 이전 메서드 + 1`, 해당 복합 태스크를 다시 push(`:75-80`). **복원점이 없으면 아무 것도 하지 않고 넘어간다**(else 분기 없음).
4. 원시 태스크면 효과를 시뮬레이션 월드 스테이트에 적용하고 계획에 추가(`:84-86`). 원시 태스크 조건 검사는 파이썬 주석으로만 남아 있고 구현되지 않았다(`:87-93`).
5. 스택이 비면 성공(`:97`) → `Result.Set`으로 TaskID 열과 `ActionsSequence`(ActionID+Parameter 쌍)를 만든다(`:100`, `:109-120`).

특징 정리: 깊이 제한 없음, 반복 횟수 상한 없음(자인: `:12`), 비용/휴리스틱 없음, 메서드 순서 = 우선순위, 복합 태스크 재귀 허용(테스트 도메인 `AttackEnemy`가 `Root`를 다시 포함: `HTNTest.cpp:56`) — 효과가 조건을 언젠가 거짓으로 만들지 않으면 무한 루프. 계획 실행·부분 계획·재계획·모니터링은 없다. 프로파일 훅은 `SCOPE_CYCLE_COUNTER(STAT_HTN_Planning)` 하나(`:6, :24`).

#### 1-7. 실행 방식 — `UHTNBrainComponent` (`Public/AI/HTNBrainComponent.h`, `Private/AI/HTNBrainComponent.cpp`)

```cpp
// HTNBrainComponent.h:12-22 (전체)
UCLASS() class HTNPLANNER_API UHTNBrainComponent : public UBrainComponent {
    GENERATED_BODY()
public:  UHTNBrainComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
protected: FHTNPlanner Planner;
};
// HTNBrainComponent.cpp:8-11: 생성자 본문 비어 있음
```
`UBrainComponent`가 제공하는 `StartLogic/RestartLogic/StopLogic/PauseLogic/ResumeLogic/TickComponent/HandleMessage`(`Engine/Source/Runtime/AIModule/Classes/BrainComponent.h:143-187`) 중 재정의된 것이 없다. 부모의 `TickComponent`는 메시지 큐 처리만 한다(`Engine/Source/Runtime/AIModule/Private/BrainComponent.cpp:277-294`). **즉 이 컴포넌트를 붙여도 계획을 세우지도, 실행하지도 않는다.** 틱 주기, 재계획 조건, 월드 스테이트 수집 등 실행 계층 전부를 사용자가 새로 써야 한다.

부가 결함: `IHTNPlannerModule::Get()/IsAvailable()`는 모듈 이름 `"HTNPlannerModule"`을 찾지만(`Public/HTNPlannerModule.h:24, 34`) 실제 모듈 이름은 `HTNPlanner`(`Private/HTNPlannerModule.cpp:23` `IMPLEMENT_MODULE(FHTNPlannerModule, HTNPlanner)`)라 `Get()`은 체크 실패, `IsAvailable()`은 항상 false다.

### 2) 테스트 스위트, 수정 시점, 모듈 의존성, 런타임 빌드 가능성

#### 2-1. 테스트 스위트가 검증하는 것 (`Source/HTNTestSuite/Private/HTNTest.cpp`)

| 테스트 이름 (Automation 경로) | 검증 내용 | 근거 |
|---|---|---|
| System.AI.HTN.DomainBuilderBasics | 빌더에 복합/원시 태스크·메서드·조건 수가 그대로 저장되는지 | `:111-204` |
| System.AI.HTN.BuildDomain | 예제 도메인(원시 7, 복합 2) 구조 확인 | `:206-230` |
| System.AI.HTN.Planning | 빈 도메인 → 빈 계획; 기본 월드 스테이트 → 순찰 계획(루트 마지막 메서드); 적 존재 시 계획 길이 > 0 | `:232-267` |
| System.AI.HTN.PlanningRollback | 첫 메서드의 하위 복합이 실패하면 둘째 메서드로 롤백 (각 메서드가 태스크 1개뿐) | `:269-317` |
| System.AI.HTN.DomainDecompilation | Compile→Decompile 결과 디버그 문자열 동일 | `:319-336` |
| System.AI.HTN.DomainDecompilationIssues | 없는 태스크 참조 시 컴파일 실패·도메인 비어 있음 | `:338-359` |
| System.AI.HTN.WorldRepresentation / Condition | 7개 내장 조건이 상수·키 우변 모두에서 맞는지 전수 검사 | `:361-465` |
| System.AI.HTN.MethodSelection / TrivialPlanning / ExtendingDomain | **본문 없음(항상 true)**, "// INJECTION" 자리표시자 | `:467-512` |
| System.AI.HTN.CustomWSCheck / CustomWSOperation | 사용자 정의 조건·효과 함수 등록 후 호출됨 | `:514-583` |
| System.AI.HTN.OperatorsOfGeneratedPlan | 계획 순서와 ActionID/Parameter 대응 | `:586-621` |
| (컴포넌트 테스트) | `typedef FAITest_SimpleComponentBasedTest<UMockHTNComponent>`만 있고 **등록 매크로 없음** → 실행되지 않음 | `:626` |

검증하지 않는 것: 다중 태스크 메서드의 롤백(2절 결함이 숨겨진 이유), 재귀 도메인 종료, 원시 태스크 조건, 계획 실행, 성능·메모리(자인: `:10-14`). `CustomWSOperation`은 정적 카운터 `StaticValue == 2`를 기대하므로 같은 프로세스에서 두 번 실행하면 실패한다(`:548-583`).

#### 2-2. 마지막 의미 있는 수정 시점의 흔적

| 흔적 | 근거 |
|---|---|
| `.uplugin` `FileVersion: 1`(현행 플러그인은 3), `VersionName "0.01"`, 설명 "…to the UE4's AI module" | `HTNPlanner.uplugin:2, 4, 6`; 비교: `../AISupport/AISupport.uplugin:2` `FileVersion 3` |
| 파이썬 원형 주석 잔존 | `HTNPlanner.cpp:45` `//self.print_progress(...)`, `:87-93` `/*if current_task.check_condition(working_ws): ...*/` |
| 컴파일되지 않는 주석 코드(오타) | `HTNDebug.cpp:69` `FString:Printf` |
| 주석 처리된 폐기 코드 | `HTNDomain.cpp:224-237`(`FHTNTaskWrapper`), `HTNDomain.cpp:80-88`(조건 함수 배열) |
| 미완성 목록 | `HTNPlanner.cpp:9-12`, `HTNDomain.cpp:9-12`, `HTNBuilder.cpp:135, 283`, `HTNTest.cpp:10-14` |
| `Deprecated`/`UE_DEPRECATED` 매크로 | 없음(grep 결과 0건) — 폐기 예고조차 없이 방치 상태 |
| 5.x API 추종만 반영 | `IntCastChecked`(`HTNBuilder.cpp:82`), `EAllowShrinking::No`(`HTNPlanner.cpp:47`, `HTNPlanner.h:58`), `UE_CLOGF`(`HTNPlanner.cpp:35`) |
| 공개 문서 | UE 4.27 API 문서와 5.7 API 문서에 같은 클래스 목록이 있음 — 최소 4.27 시점부터 현재까지 존재. https://docs.unrealengine.com/4.27/en-US/API/Plugins/HTNPlanner/index.html , https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/HTNPlanner (검색일 2026-09-09; 최초 도입 버전은 **미확인**) |

설치본 파일 날짜(2026-08-19/09-04)는 설치·로컬 빌드 시각이므로 수정 이력 근거가 아니다.

#### 2-3. 모듈 의존성과 런타임 빌드 가능성

| 모듈 | Type / LoadingPhase | 의존성 | 근거 |
|---|---|---|---|
| HTNPlanner | Runtime / PreDefault | Public: Core, CoreUObject, Engine, GameplayTags, GameplayTasks, AIModule; 에디터 빌드 시 Private: EditorFramework, UnrealEd; `SetupGameplayDebuggerSupport` | `HTNPlanner.uplugin:19-23`, `HTNPlanner.Build.cs:6-23` |
| HTNTestSuite | UncookedOnly / PreDefault | Core, CoreUObject, Engine, AIModule, HTNPlanner, AITestSuite(+에디터 시 UnrealEd) | `.uplugin:24-27`, `HTNTestSuite.Build.cs:16-24, 32-36` |

`TargetAllowList`, `PlatformAllowList`, `SupportedTargetPlatforms` 키가 없어 모든 타깃/플랫폼 허용. 실제로 설치본에 `Intermediate/Build/Win64/x64/UnrealGame/Development/HTNPlanner/HTNPlanner.precompiled`와 `.../Shipping/HTNPlanner/HTNPlanner.precompiled`가 있어 **UnrealGame Development·Shipping 타깃용 프리컴파일이 존재**한다. 단, `WITH_HTN_DEBUG`가 Shipping/Test에서 꺼져 태스크 이름 조회가 불가능해진다(`HTNDomain.h:9`, `HTNDomain.cpp:294-302`). 헤드리스 시뮬레이션 프로그램 타깃(Program)에서의 빌드는 **미확인**(AIModule 의존이므로 Engine 전체가 따라온다).

프로젝트 현황: `TDGame.uproject`에 HTNPlanner는 없고(활성 플러그인: GameplayAbilities, ModelContextProtocol, StateTree, GameplayStateTree 등, `C:/Project/TDGame/TDGame.uproject:19-48`), 프로젝트 소스에 `HTN` 참조 0건(grep).

### 3) 제품 사용 vs 자체 HTN 판단 근거

| 관점 | 관찰 | 근거 |
|---|---|---|
| 코드 규모 | 런타임 모듈 소스 약 1,100줄(헤더 561 + cpp 930, 주석·공백 포함), 그중 알고리즘 핵심(`HTNPlanner.cpp` + `HTNDomain.cpp` 조건/효과)은 약 400줄 | `wc -l` 결과 |
| 실행 계층 | 없음(1-7절) | `HTNBrainComponent.cpp:8-11` |
| 결함 | 롤백 시 스택 미복구(아래 5.2 재현), 실패=빈 계획 성공 처리, 디버그 출력 첫 효과 누락, 모듈 이름 불일치, 경계 검사 없는 조건 평가 | `HTNPlanner.cpp:75-97`, `HTNBuilder.cpp:327`, `HTNPlannerModule.h:24,34`, `HTNDomain.cpp:45-78` |
| 확장점 | (a) 사용자 정의 조건/효과 함수 등록(`HTNDomain.h:115-117`), (b) `ActionID/Parameter`의 해석은 전적으로 사용자 몫(`HTNDomain.h:120-136`), (c) `GeneratePlan`의 `StartTaskName`으로 부분 도메인 계획(`HTNPlanner.h:52`). 그 외 상속 가능한 가상 함수 없음(모든 구조체 비가상) |
| 표현력 한계 | 원시 태스크 전제조건 없음, 태스크 파라미터(변수 바인딩) 없음, 비용/유틸리티 없음, 반복 상한 없음, 정수 월드 스테이트 128개 기본 | `HTNPlanner.cpp:9-12`, `HTNDomain.h:138-148, 232` |
| 데이터 주도 | 텍스트/에셋 로더 없음. 도메인은 C++ 빌더 호출로만 구성 | `HTNBuilder.h` 전체 |
| 디버깅 | 게임플레이 디버거 카테고리 "HTN" 등록만 하고 그리지 않음 | `HTNPlannerModule.cpp:25-32`, `GameplayDebuggerCategory_HTN.cpp:16-23` |

판단: 이 코드를 "그대로 사용"하면 얻는 것은 약 400줄의 결함 있는 플래너뿐이고, 잃는 것은 엔진 플러그인 의존(엔진 업그레이드 시 방치 코드 컴파일 위험)과 결함 수정 불가(엔진 소스 수정은 프로젝트 방침상 읽기 전용)다. 자체 구현 시 차용할 만한 설계는 (1) 이름 기반 빌더 → 연속 메모리 블록 컴파일, (2) 정수 배열 월드 스테이트 + 함수 포인터 테이블, (3) 복원점 스택 기반 반복형 분해 — 이 세 가지이며, 복원점에 `TasksToProcess`를 포함시키면 결함 2가 바로 해결된다.

### 4) 결정론 관점

| 점검 항목 | 결과 | 근거 |
|---|---|---|
| 난수 | 없음(`FMath::Rand`, `FRandomStream` 0건) | grep `rand` 결과: 없음 |
| 타이머/시간/프레임 | 없음(`FTimerManager`, `DeltaTime`, `GFrameCounter`, `FPlatformTime` 0건). `SCOPE_CYCLE_COUNTER`는 통계 수집만 | `HTNPlanner.cpp:24` |
| 월드/액터 참조 | 없음. 입력은 `FHTNDomain`(불변)과 `FHTNWorldState` 복사본 | `HTNPlanner.cpp:22, 30` |
| 메서드 선택 | 정의 순서대로 첫 만족 메서드(결정적) | `HTNDomain.cpp:239-250` |
| 컴파일 순서 | `TMap` 삽입 순서 순회 → 같은 빌더 호출 순서면 같은 TaskID | `HTNBuilder.cpp:49, 75, 99` |
| 전역 상태 | 사용자 정의 연산 테이블이 프로세스 전역이며 등록 순서가 ID를 결정, 중복 등록 시 ID 증가, 락 없음 | `HTNDomain.cpp:105-106, 149-167` |
| 부동소수점 | 없음(모두 int32) | `HTNDomain.h:19` |
| 스레드 | `FHTNPlanner` 인스턴스별 상태만 사용하므로 인스턴스를 스레드마다 두면 병렬 가능(전역 테이블은 읽기 전용 전제) | `HTNPlanner.h:72-80` |

결론: **플래너 코어는 순수 함수에 가깝고 결정론적**이다. 결정론을 깨는 요소는 플러그인 밖(월드 스테이트를 채우는 사용자 코드, 실행 타이밍)에만 있다.

### 5) 같은 폴더 AI 플러그인의 .uplugin 상태

| 플러그인 | FileVersion / VersionName | EnabledByDefault | IsBetaVersion | IsExperimentalVersion | 모듈(Type) | 의존 플러그인 | 근거 |
|---|---|---|---|---|---|---|---|
| AISupport | 3 / 1.0 | true | false | (키 없음) | AISupportModule(Runtime, PostConfigInit) | 없음 | `../AISupport/AISupport.uplugin:2-24` |
| EnvironmentQueryEditor | 3 / 1.0 | true | (키 없음) | (키 없음) | EnvironmentQueryEditor(UncookedOnly) | AISupport | `../EnvironmentQueryEditor/EnvironmentQueryEditor.uplugin:2-29` |
| HTNPlanner | 1 / 0.01 | false | **true** | (키 없음, 설명에 [EXPERIMENTAL]) | HTNPlanner(Runtime), HTNTestSuite(UncookedOnly) | 없음 | `HTNPlanner.uplugin:2-29` |
| MLAdapter | 3 / 0.0.1 | false | false | **true** | MLAdapter(Runtime), MLAdapterTestSuite(DeveloperTool) | GameplayAbilities, EnhancedInput | `../MLAdapter/MLAdapter.uplugin:2-43` |
| MassAI | 3 / 0.4 | false | false | **true** | MassNavigation, MassNavMeshNavigation, MassZoneGraphNavigation, MassAIBehavior, MassAIReplication, MassAIDebug(Runtime), MassNavigationEditor·MassAIBehaviorEditor(Editor), MassAITestSuite(UncookedOnly) | MassGameplay, ZoneGraph, ZoneGraphAnnotations, SmartObjects, StateTree, NavCorridor | `../MassAI/MassAI.uplugin:2-77` |
| MassCrowd | 1 / 0.4 | false | false | **true** | MassCrowd(Runtime, Default) | ZoneGraph, MassGameplay, MassAI, StateTree | `../MassCrowd/MassCrowd.uplugin:2-45` |

주: HTNPlanner만 `IsBetaVersion`(구식 키)을 쓰고, 나머지 실험 플러그인은 `IsExperimentalVersion`(현행 키)을 쓴다. 이것도 HTNPlanner 메타데이터가 갱신되지 않았다는 방증이다. MLAdapter는 이미 GameplayAbilities 의존을 선언해 GAS 기반 프로젝트와 결합이 자연스럽다(`MLAdapter.uplugin:36-39`).

#### 5.2 (보조) 롤백 결함 재현

`FHTNPlanner::GeneratePlan`(`HTNPlanner.cpp:22-104`)의 제어 흐름을 파이썬으로 1:1 옮겨 실행했다(스크립트는 세션 스크래치 폴더, 프로젝트에 남기지 않음).

- 도메인: `Root` 메서드0 = `[A, B]`, 메서드1 = `[C]`; `A`는 조건 `x > 0`인 메서드 하나; `x = 0`.
- 올바른 HTN 결과: `A` 실패 → 메서드0 폐기 → 메서드1 → 계획 `[C]`.
- 플러그인 로직 결과: `(성공, [C, B])`. `A` 실패 시 복원점이 월드 스테이트·계획만 되돌리고(`HTNPlanner.h:28-31`, `:57-58`) 스택에 남은 `B`를 지우지 않아(`HTNPlanner.cpp:75-80`) 폐기된 메서드의 형제 `B`가 계획 끝에 붙는다.
- 두 번째 도메인: `Root` 메서드 하나, 조건 `x > 0`, `x = 0` → 결과 `(성공, [])`. 만족 메서드가 없고 복원점도 없으면 무시하고 루프가 끝나 성공으로 보고된다(`:75-81` else 없음, `:97`).

---

## 프로젝트 적용 시사점

1. **HTNPlanner 플러그인을 uproject에 켜지 않는다.** 실행기가 없어 켜도 몬스터가 움직이지 않고, 결함 수정을 위해 엔진 소스를 고칠 수 없다. 켜면 얻는 것은 `FHTNPlanner::GeneratePlan` 하나뿐이다.

2. **HTN을 채택한다면 TDGame 안에 자체 모듈(예: `Source/TDGame/AI/HTN/`)로 짠다.** 차용할 설계 3가지: (a) `FHTNBuilder_Domain`처럼 이름 기반으로 조립 후 `Compile()`로 연속 메모리 블록화 → 캐시 친화·복사 저렴(`HTNBuilder.cpp:42-223`), (b) `FHTNWorldState`처럼 정수 배열 월드 스테이트와 열거형 키(`HTNDomain.h:230-275`) → 결정론·직렬화·해시 검증에 유리, (c) 복원점 스택 기반 반복형 분해(`HTNPlanner.h:21-42`) — 단 복원점에 `TasksToProcess` 크기(또는 스택 자체)를 포함시켜 5.2 결함을 피하고, 반복 상한과 "실패" 반환값을 추가한다.

3. **데이터 정의는 텍스트로.** 플러그인은 C++ 빌더 호출만 지원하지만(`HTNBuilder.h`), 같은 빌더 API 위에 JSON/CSV 로더를 한 번 짜면 생성형 AI가 에디터 없이 도메인을 읽고 쓸 수 있다. 빌더의 이름 기반 참조(`FHTNBuilder_Method::Tasks`가 `TArray<FName>`, `HTNBuilder.h:32`)는 그대로 텍스트 매핑이 된다.

4. **결정론 시뮬레이션과의 결합.** 플래너 코어가 순수 함수라는 점(4절)은 TDGame의 `FTDScopedCombatWorld` 고정 스텝 시뮬레이션과 잘 맞는다. 자체 HTN에서도 (a) 월드 스테이트를 GAS 속성값의 정수 양자화로 채우고, (b) 계획 수립을 고정 스텝 경계에서만 호출하며, (c) 사용자 정의 연산 테이블은 모듈 초기화 시 한 번만 고정 순서로 등록하면 동일 입력=동일 결과가 유지된다.

5. **대량 몬스터 최적화.** 플러그인의 계획 1회 비용은 메서드 조건 검사(정수 비교)와 복원점 복사(월드 스테이트 128×int32 = 512바이트 복사)가 지배한다(`HTNPlanner.h:39-41` 복원점 복사 생성자, `HTNDomain.h:232`). 자체 구현 시 월드 스테이트 크기를 몬스터 종류별 최소 키 수로 줄이고, 복원점은 변경 로그(undo log) 방식으로 바꾸면 수백 마리 규모에서도 계획 비용을 낮게 유지할 수 있다. 계획 주기는 컴포넌트 틱이 아니라 자체 스케줄러(N프레임 라운드로빈)로 돌린다 — 플러그인이 아무 실행기도 강제하지 않으므로 이 자유는 그대로 있다.

6. **머신러닝 결합.** HTN 메서드 선택이 "첫 만족 메서드" 고정(`HTNDomain.cpp:239-250`)이므로 학습 정책이 개입할 자리는 자체 구현에서 (a) 메서드 순서를 정책 출력으로 재정렬, (b) 월드 스테이트 키 일부를 정책 출력으로 채우기, 두 지점이다. MLAdapter 플러그인(`../MLAdapter/MLAdapter.uplugin`, 실험, GAS 의존)이 외부 프로세스 학습 인터페이스 후보이며 별도 조사 영역이다.

7. **피할 것**: `GetValueUnsafe/SetValueUnsafe`류 경계 검사 없는 접근을 데이터 주도 도메인에 쓰는 것(`HTNDomain.h:260-269`), 전역 등록 테이블에 런타임 중 등록하는 것(`HTNDomain.cpp:149-167`), uint8 수량 한계를 넘는 대형 도메인(`HTNBuilder.cpp:85,107,122-123`), 재귀 도메인에서 종료 조건 없는 효과 설계(`HTNTest.cpp:56` 패턴).

---

## 미확인·미해결 질문

1. HTNPlanner 플러그인이 처음 도입된 엔진 버전과 마지막 기능 커밋 시점 — 설치본에 git 이력이 없어 **미확인**. 공개 API 문서로 4.27~5.7 존재만 확인.
2. Program 타깃(에디터·게임이 아닌 독립 실행 프로그램)에서 HTNPlanner 모듈이 링크되는지 — AIModule 의존 때문에 사실상 Engine 전체가 필요하며 실제 빌드는 **미검증**.
3. 5.2절의 롤백 결함은 파이썬 재현으로 확인했고 C++ 자동화 테스트로는 **미검증**(엔진 플러그인을 켜지 않는 방침이므로 프로젝트 내 재현 테스트를 만들지 않았다). 필요하면 프로젝트 테스트 모듈에서 `HTNPlanner` 모듈만 의존해 `System.AI.HTN.*` 스타일로 재현 가능하다.
4. `FHTNWorldStateOperations`의 전역 테이블이 Live Coding/핫 리로드 시 중복 등록되는지 — **미확인**(정적 초기화 객체 `Setup`은 모듈 로드마다 한 번 실행되므로 재로드 시 내장 항목이 중복될 가능성이 있음, `HTNDomain.cpp:147`).
5. Epic이 이 플러그인을 향후 제거·대체(예: StateTree 기반 플래너)할 계획이 있는지 — 공개 로드맵 **미확인**.
