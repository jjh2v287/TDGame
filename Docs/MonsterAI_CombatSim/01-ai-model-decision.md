[← 인덱스로](../MonsterAI_CombatSim_Plan.md)

# 01. 몬스터 AI 모델 비교와 결정

## 이 문서가 답하는 질문

1. "엔진 내장 AI(비헤이비어 트리·StateTree)는 틱을 수동 제어하기 어렵다"는 우려는 엔진 소스로 볼 때 어디까지 사실인가.
2. "노드 에셋이라 생성형 AI 가 다루기 어렵다"는 우려는 5.8 에서 해소됐는가.
3. 반응형(BT·StateTree·FSM·유틸리티)과 계획형(HTN·GOAP)은 개념적으로 무엇이 다르고, 사용자의 분류에서 무엇을 보정해야 하는가.
4. 후보 8개(BT, StateTree, FSM, 엔진 HTNPlanner, 자체 HTN, GOAP, 유틸리티, 하이브리드) 가운데 TDGame 이 무엇을 왜 택하는가.
5. 배제한 후보를 다시 꺼내는 조건은 무엇인가.

## 결론 요약(결정 문장)

- **주력은 코드 정의 유틸리티 AI + 얇은 실행 FSM 5상태(Idle/Move/Cast/Sequence/Stagger)다**(결정 기록 D1). 행동 × 고려사항 표, 4파라미터 응답 곡선, 비용 등급 순 평가와 0점 조기 종료, 최고점 선택, 동률은 정의 순서, 관성(InertiaSwitchRatio + 최소 유지 스텝)이 규칙의 전부다.
- **보조 표현은 데이터 정의 시퀀스(콤보)와 보스 페이즈 표다**(D2). 종 단위 옵션 "상위 N개 가중 무작위"는 몬스터별 AI 스트림만 소비하고 기본은 최고점이다.
- **자체 C++ HTN(Hierarchical Task Network, 계층적 태스크 네트워크)은 Phase 4 조건부 추가다**(D3). 엔진 HTNPlanner 플러그인은 실행기 부재·백트래킹 결함·방치 상태라 채택하지 않는다.
- **엔진 BT·StateTree·GOAP·Mass 두뇌·Mover·MLAdapter 는 전투 코어에서 배제한다**(D4). StateTree 는 "보스 연출·시각 디버깅용 재검토 선택지"로만 남긴다.
- 사용자의 두 우려 가운데 **"틱 수동 제어 불가"는 절반만 사실이고, "노드 에셋이라 생성형 AI 가 못 다룬다"는 5.8 에서도 큰 틀에서 사실이다**. 정밀하게는 BT 는 엔진 제공 텍스트 경로가 없어 직렬화기·로더를 자작해야 하고(BT 결론 4·5), StateTree 는 패키지 빌드에서 텍스트 → 트리가 불가하다(ST 결론 6). 두 경우 모두 우리 JSON 이 정본이 되므로 엔진 트리는 중간 산출물에 그친다. 배제의 결정적 근거는 틱이 아니라 엔진 제공 텍스트 정의 경로의 부재다.
- 사용자 분류의 보정: **GOAP 는 확률형이 아니라 비용 기반 A\* 탐색 계획형**, **유틸리티는 점수 기반 반응형**, **HTN 은 분해 기반 계획형**이다. 확률은 계획형/반응형과 직교하는 별개 축이다.

---

## 1. 사용자의 두 우려를 엔진 소스로 검증한 결과

### 1.1 우려 1 — "엔진 내장 AI 는 틱을 수동 제어하기 어렵다"

판정: **BT 는 "간격 제어는 불가, 수동 한 스텝 진행은 가능"이고, StateTree 는 "컨텍스트 직접 구동으로 수동 제어 가능"이다.** 우려는 BT 의 간격 제어에 대해서만 정확하다.

| 항목 | 비헤이비어 트리(BT) | StateTree | 근거 |
|---|---|---|---|
| 엔진 틱 밖에서 한 스텝 강제 진행 | 가능. `TickComponent(DeltaTime, LEVELTICK_All, nullptr)` 를 직접 호출하면 인자 DeltaTime 을 쓴다. 엔진 테스트 스위트가 `bCanEverTick=false` 로 두고 이 방식으로 돌린다 | 가능. `FStateTreeExecutionContext` 를 스택에 만들어 `Start()/Tick(DeltaTime)/Stop()` 을 임의 시점·임의 DeltaTime 으로 호출한다. 엔진 테스트가 같은 방식이다 | engine-behaviortree-tick 결론 1, BehaviorTreeComponent.cpp:1698-1708, MockAI_BT.cpp:26-27(`bCanEverTick=false`), MockAI.cpp:78-94(수동 `TickComponent`) / engine-statetree-runtime 결론 1, StateTreeExecutionContext.h:331-336·480-517, StateTreeTest.cpp:97-110 |
| 외부에서 틱 간격 지정 | **불가.** 매 틱 끝에 `ScheduleNextTick` 이 `SetComponentTickIntervalAndCooldown` 을 다시 호출해 외부 값을 덮어쓴다 | 컴포넌트 경로는 5.6 예약 틱(Sleep/EveryFrame/NextFrame/CustomTickRate)을 엔진 틱 매니저에 맡긴다. 컨텍스트 직접 구동이면 호출자가 주기를 정한다 | engine-behaviortree-tick 결론 2, BehaviorTreeComponent.cpp:1868·1923-1951 / engine-statetree-runtime 결론 5, StateTreeExecutionContext.cpp:409-569, StateTreeComponent.cpp:298-346 |
| 태스크 갱신과 전이 판정 분리 | 불가(한 `TickComponent` 안에서 보조 노드 틱 → 실행 요청 처리 → 태스크 틱 순서 고정) | 가능. `TickUpdateTasks(DeltaTime)` 와 `TickTriggerTransitions()` 를 따로 부른다 | engine-behaviortree-tick 상세 1-3, BehaviorTreeComponent.cpp:1698-1921 / engine-statetree-runtime 결론 2, StateTreeExecutionContext.h:517-524 |
| 트리 내부 자체 스케줄링 | 있음. 보조 노드·태스크의 `NextTickRemainingTime` 최소값으로 다음 틱을 예약하고 필요 없으면 틱을 끈다(`DisableTick=FLT_MAX`). "매 프레임 트리 전체 평가"가 아니다 | 있음. `GetNextScheduledTick()` 이 잠자기/주기를 계산한다. 단 스키마가 `IsScheduledTickAllowed()` 를 true 로 돌려줘야 하며 기본 `UStateTreeSchema` 는 false 다 | engine-behaviortree-tick 결론 3, BTAuxiliaryNode.cpp:45-80·156-170 / engine-statetree-runtime 결론 5, StateTreeSchema.h:54-57 |
| 수동 틱 시 남는 엔진 결합 | `AAIController` 액터 틱(`UpdateControlRotation`)이 매 프레임 남는다. 수동 틱 중에도 `ScheduleNextTick` 이 틱 함수 API 를 매 스텝 호출한다(무해하나 오버헤드) | 컨텍스트는 "여러 프레임에 걸쳐 보관하지 말라"는 임시 객체 전제(StateTreeExecutionContext.h:277)라 매 사고마다 스택 생성이 기본 사용법이다. 한 스텝 안의 재사용은 막지 않는다(비용 미측정) | engine-behaviortree-tick 결론 11, AIController.cpp:58-63, Controller.cpp:62 / engine-statetree-runtime 상세 1, StateTreeExecutionContext.h:277-279, 미확인 1 |
| 결정론 난수 | 서비스 `RandomDeviation` 과 `BTTask_Wait` 의 `RandomDeviation` 이 전역 `FMath::FRandRange` 를 쓴다. 두 값을 0 으로 두면 BT 코어는 결정적 | 인스턴스별 `FRandomStream`. `Start(FStartParameters{.RandomSeed})` 로 시드 지정 가능. 컴포넌트 경로는 시드를 넘기지 않아 `FPlatformTime::Cycles()` 시드(비결정) | engine-behaviortree-tick 결론 7, BTService.cpp:107, BTTask_Wait.cpp:19 / engine-statetree-runtime 결론 3, StateTreeExecutionContext.cpp:1513, StateTreeComponent.cpp:194-198 |

주의할 사실 두 가지. 첫째, BT 의 실행 요청(`RequestExecution`)은 즉시 처리되지 않고 다음 틱으로 미뤄지므로 "블랙보드 값 변경 → 브랜치 전환"에 최소 한 스텝 지연이 있다(engine-behaviortree-tick 상세 1-3, BehaviorTreeComponent.cpp:1453·1144-1150). 고정 스텝에서는 결정적이지만 스텝 크기를 바꾸면 결과가 달라진다. 둘째, 웹 조사는 StateTree 에 대해 "5.6 부터 엔진 틱에 묶인다는 우려가 구조적으로 해소됐다"고 결론지었다(web-ue-5-6-to-5-8-ai-changes 결론 1). 따라서 StateTree 를 배제하는 근거를 틱에 두면 근거가 약하다.

### 1.2 우려 2 — "노드 에셋이라 생성형 AI 가 다루기 어렵다"

판정: **5.8 에서도 큰 틀에서 사실이다.** 정밀하게 나누면 BT 는 "엔진 제공 텍스트 경로가 없다(직렬화기·로더 자작은 가능)"이고, StateTree 는 "패키지 빌드에서 텍스트 → 트리가 불가하다"이다. 어느 쪽이든 "에디터 없이 텍스트로 정의를 만들어 헤드리스로 돌리는 엔진 제공 경로"는 없다.

| 항목 | BT | StateTree | 근거 |
|---|---|---|---|
| C++ 로 트리 조립 | 가능. `UBehaviorTree::RootNode` 가 런타임 UPROPERTY 이고 엔진 테스트 헬퍼 `FBTBuilder` 가 `NewObject` 로 조립한다 | 에디터 타깃에서만 가능. 조립 API(`AddChildState/AddTask<T>/AddTransition/AddConsideration<T>`)와 `FStateTreeCompiler` 가 **StateTreeEditorModule(UncookedOnly)** 에 있다. 런타임 데이터는 `UStateTree` 의 private 멤버이고, 이를 채우는(쓰는) friend 는 `WITH_EDITOR` 안의 `FStateTreeCompiler` 와 `FCompilerManagerImpl` 둘뿐이다(StateTree.h:684-685). 실행 컨텍스트 계열 7개(`FStateTreeInstance`, `FStateTreeExecutionContext` 등)도 friend 이지만 컴파일 데이터를 만드는 쪽은 아니다(StateTree.h:675-681) | engine-behaviortree-tick 결론 4, BTBuilder.h:66-115 / engine-statetree-runtime 결론 6·상세 4, StateTree.h:445-493·675-685, StateTreeEditorData.h:272-345, StateTreeCompiler.h:48-67 |
| 전용 텍스트 직렬화기 | **부재 확인.** `Editor/BehaviorTreeEditor` 에 `UExporter` 파생이 없다. 범용 T3D·`FJsonObjectConverter` 는 노드 간 참조·서브오브젝트 복원을 직접 짜야 한다 | **미발견(미확인).** `DebugInternalLayoutAsString()` 으로 덤프(읽기)만 가능하며 그것도 에디터/디버그 빌드 한정(`WITH_EDITOR` 또는 `WITH_STATETREE_DEBUG` 매크로 안)이라 패키지 빌드에는 없다 | engine-behaviortree-tick 결론 5, EditorExporters.cpp:322-326 / engine-statetree-runtime 상세 4 우회 경로 (다), StateTree.h:376-378 |
| 5.8 공식 MCP(Model Context Protocol, 모델 컨텍스트 프로토콜) 툴셋 | `AIModuleToolset` 의 BT 도구는 `get_*/list_nodes` 뿐. `set_/add_/create_` 문자열 없음 | `StateTreeToolset` 은 설명이 "Toolset for StateTree Inspection", 실험·기본 비활성, 도구 9개 모두 `get_*` | web-llm-authorable-tooling 결론 2, `behavior_tree.py:47~136`, `state_tree.py:12~140`, `StateTreeToolset.uplugin:6,16,19` |
| 에디터 Python 으로 생성 | (조사 범위 밖) | 반쯤만 열림. `SubTrees` 는 `BlueprintReadOnly`, `Tasks/EnterConditions/Considerations` 는 `TArray<FStateTreeEditorNode>`(FInstancedStruct) 이고 `AddTask<T>` 는 템플릿이라 리플렉션 비노출. Python 이 노드를 채울 수 있는지 미확인. Python 은 에디터 전용 | web-llm-authorable-tooling 결론 3, StateTreeState.h:282~342·475~496 |
| 패키지 빌드에서 텍스트 → 트리 | 자체 로더를 짜면 가능(직렬화기 자작) | **불가.** 반드시 에디터/커맨드렛 파이프라인으로 에셋을 만들어 쿠킹해야 한다 | engine-behaviortree-tick 결론 4·5 / engine-statetree-runtime 결론 6 우회 경로 (라) |

두 표를 합치면 결론은 하나다. StateTree 를 텍스트에서 만들려면 "우리 JSON → 커맨드렛 → `UStateTreeEditorData` 조립 → 컴파일 → 에셋 저장" 파이프라인이 필요하다. 이 커맨드렛은 런타임 TDGame 모듈에 두고(D13, `UCommandlet` 은 Engine 모듈, Commandlet.h:39-41) 에디터 타깃(UnrealEditor-Cmd)으로 실행하며, StateTreeEditorModule(UncookedOnly, StateTree.uplugin:34-35) 의존은 Build.cs 의 `Target.bBuildEditor` 조건부로만 추가한다. 별도 에디터 모듈을 만들지 않는다. 그리고 그 파이프라인의 입력이 곧 우리 정의 형식이므로 **StateTree 는 중간 산출물일 뿐 정본이 될 수 없다**. 정본을 우리 JSON 으로 두는 순간 StateTree 가 주는 것은 리와인드 디버거뿐이다(심사 판정 "StateTree vs 자체 유틸리티" 3인 일치).

### 1.3 검증 결과가 결정에 주는 함의

| 우려 | 사실 여부 | 결정에 미친 영향 |
|---|---|---|
| 틱 수동 제어 어려움 | BT 간격 제어: 사실. BT 수동 스텝: 가능. StateTree: 컨텍스트 직접 구동이면 가능 | 배제의 주된 근거가 아니다. 다만 BT 는 `AAIController` 액터 틱과 마리당 UObject 3~4개가 남아 대량에서 불리하다(engine-behaviortree-tick 결론 6·11) |
| 노드 에셋이라 생성형 AI 부적합 | BT: 엔진 제공 텍스트 경로 부재(자작은 가능). StateTree: 패키지 빌드에서 불가(5.8) | **배제의 결정적 근거.** 정본은 텍스트여야 하고 검증·시뮬은 헤드리스여야 한다([02 아키텍처와 정의 형식](02-architecture-and-definition-format.md)) |
| 추가로 드러난 위험 | 두 종류다. (1) C++ API 폐기(`UE_DEPRECATED`) 24~71건/버전(5.6 67, 5.7 71, 5.8 24) → C++ 태스크 유지보수 부담. (2) 5.6 예약 틱 정책 도입 시 트리 정지 회귀, 5.8 링크 에셋 서브트리 전이 실패(UE-384337) → 콘텐츠 회귀 | 엔진 내부 변경이 코드·콘텐츠 양쪽 회귀로 직결되는 구조를 피한다(engine-statetree-runtime 결론 8·상세 6, web-ue-5-6-to-5-8-ai-changes 결론 10) |

---

## 2. 계획형 vs 반응형 개념 정리(사용자 분류 보정)

사용자의 분류("BT/StateTree/FSM 반응형 규칙, HTN 계획형, GOAP 계획형+확률")는 큰 틀은 맞고 세부 셋이 다르다.

| 모델 | 결정 방식 | 계획 지평 | 확률 개입 | 사용자 분류 대비 보정 | 근거 |
|---|---|---|---|---|---|
| FSM(유한 상태 기계) | 상태별 전이 조건 검사 | 없음(현재 상태만) | 조건에 넣을 때만 | 그대로 | web-ai-architecture-comparison 상세 2-1 |
| BT | 우선순위 목록의 이진 결정(헤일로 2), "문제 해결이 아니라 지능형 제어" | 없음(매 틱 루트부터 또는 예약 틱) | 랜덤 셀렉터·Wait 편차에만 | "규칙"보다 "우선순위 제어"가 정확. 반응형은 맞음 | web-ai-architecture-comparison 결론 1-1(2005, 2020) |
| StateTree | 계층 FSM + BT 식 선택기. `None` 을 제외한 선택 행동 6종에 **최고 유틸리티·유틸리티 가중 무작위**가 내장 | 없음 | 가중 무작위 선택기·지연 전이만(인스턴스 스트림) | 반응형이 맞되 "점수 기반 선택"을 내장한다 | engine-statetree-runtime 결론 4, StateTreeTypes.h:172-198 |
| 유틸리티 | 후보마다 고려사항 점수를 곱해 최고점 선택 | 없음(사고마다 재평가) | 기본 없음. "상위 N 중 무작위"(심즈) 는 선택 옵션 | **점수 기반 반응형.** 사용자 분류에 빠져 있던 항목 | web-ai-architecture-comparison 결론 1-2, 결론 6 |
| HTN | 복합 태스크를 조건에 맞는 첫 메서드로 분해해 원시 태스크 열(계획) 생성 | 2~5 액션 | 없음(첫 만족 메서드, 비용·정렬 없음) | **분해 기반 계획형.** 그대로 | web-ai-architecture-comparison 결론 2·3, engine-htnplanner-plugin 결론 5 |
| GOAP | 목표 상태까지 액션 비용 합이 최소인 열을 A\* 로 탐색 | 실측 1~2 액션 | **없음.** 비용은 결정적이고 동률 tie-break 규약만 필요 | **"확률"이 아니라 "비용 기반 탐색"이다.** 확률처럼 보이는 것은 절차적 전제조건·재계획 빈도의 불규칙성 | web-ai-architecture-comparison 결론 1-4(F.E.A.R. 2006, 툼 레이더 2015), 상세 2-1 |

세 가지 보정 요점.

1. 확률은 계획형/반응형과 **직교하는 축**이다. 유틸리티(심즈 상위 N 무작위)·StateTree(가중 무작위)·BT(랜덤 셀렉터)에도 있고 GOAP·HTN 원형에는 없다. TDGame 은 확률을 "종 단위 옵션 + 몬스터별 AI 스트림"으로 격리한다(D2, D29).
2. 계획형이 이기는 조건은 "행동 열의 순서가 상황에 따라 달라져야 하고 그 길이가 2 이상"일 때뿐이다. AIIDE 2014 로그 분석은 GOAP 계획 길이 1~2, HTN 2~5 였고 "길이 1 계획과 고정 계획에는 계획기 대안을 고려하라"고 결론지었다(web-ai-architecture-comparison 결론 2, https://cdn.aaai.org/ojs/12728/12728-52-16245-1-2-20201228.pdf 2014).
3. 상용 AI 는 순수 단일 모델을 거의 쓰지 않는다. "상위 선택은 점수(유틸리티) 또는 분해(HTN), 하위 실행은 시퀀스(BT/FSM)"가 2015~2025 공통 구조다(web-ai-architecture-comparison 결론 1, 상세 4). 따라서 질문은 "무엇 하나를 쓰나"가 아니라 "선택층과 실행층에 각각 무엇을 두나"다.

---

## 3. 후보 8개 비교표

기준 8열은 결정 기록 D5 를 따른다. D5 원문은 "후보 7개"라 적고 8개를 나열하는데, 나열 8개가 맞고 "7개"는 오기다(결정 기록 담당에게 수정 요청). ◎ 매우 좋음 / ○ 좋음 / △ 조건부 / × 부적합. 각 셀의 괄호가 근거다.

| 후보 | 결정론 | 틱 제어 | 코드/텍스트 정의 | LLM 제작성 | 마리당 비용 | 디버깅 | 엔진 의존 | 성숙도 | 판정 |
|---|---|---|---|---|---|---|---|---|---|
| BT(엔진) | △ 서비스·Wait 편차 전역 난수, 0 으로 강제 필요; 실행 요청 1틱 지연(BT 결론 7, 상세 1-3) | △ 수동 스텝 가능, 간격 외부 제어 불가, 컨트롤러 액터 틱 잔존(BT 결론 1·2·11) | × 조립은 가능, 텍스트 직렬화기 부재(BT 결론 4·5) | × DSL 자작 필요, 공식 MCP 는 읽기 전용(web-llm 결론 2) | △ UObject 3~4개 + 인스턴스 메모리; 블루프린트 노드는 컴포넌트마다 복제(BT 결론 6) | ○ 엔진 디버거·비주얼 로거 | 높음(AIModule) | 정식 | 미채택 |
| StateTree(엔진) | ○ 컨텍스트 직접 구동 + `RandomSeed` 지정 시 결정적; 컴포넌트 경로는 비결정(ST 결론 3) | ○ 컨텍스트 `Tick`·분리 틱 가능; 예약 틱은 스키마 재정의 필요(ST 결론 1·2·5) | × 조립·컴파일이 UncookedOnly 에디터 모듈, 패키지 빌드 불가(ST 결론 6) | × 툴셋 검사 전용, Python 노드 채우기 미확인(web-llm 결론 2·3) | △ 사고마다 컨텍스트 스택 생성 + 인스턴스 데이터 GC 보고; 수치 미측정(ST 상세 2, 미확인 1) | ◎ 디버거 + 5.7 리와인드 디버거(web-ue 상세 1-2) | 높음. 버전당 폐기 24~71건, 5.6 틱 정책 회귀(ST 결론 8, web-ue 결론 10) | 정식(5.1~, 5.8 표식 없음)(web-ue 결론 1) | 미채택. 보스 연출용 재검토 선택지(§5.2) |
| FSM(자체) | ◎ 전이 조건이 순수 함수면 결정적(web-ai 상세 2-1) | ◎ 완전 자유(자체 코드, web-ai 상세 2-1) | ◎ 상태·전이가 C++ 등록표(D7, web-ai 상세 1-4 F.E.A.R. 3상태) | ○ 전이 조건이 코드에 흩어져 표보다 읽기 어려움(web-ai 상세 2-1) | ◎ POD 상태 하나(web-ai 상세 1-4) | ○ 상태 전이 로그(web-ai 상세 1-4) | 없음(자체 코드) | 자체. F.E.A.R. 2006 이후 실행층 표준(web-ai 상세 1-4) | **실행층으로 채택**(D1) |
| 엔진 HTNPlanner 플러그인 | ○ 코어는 순수 함수(HTN 결론 5) | – 실행기 자체가 없음(HTN 결론 1) | × C++ 빌더만, 로더 없음(HTN 상세 3) | △ | ○ 계획 1회 = 정수 비교 + 복원점 512바이트 복사(HTN 시사점 5) | × 디버거 카테고리가 빈 함수(HTN 결론 1) | 중(플러그인, 베타 0.01)(HTN 결론 3) | 방치·결함(HTN 결론 2·3·6) | 미채택 |
| 자체 HTN(C++) | ◎ 첫 만족 메서드, 비용·정렬 없음, 계획 수립을 스텝 경계에서만(HTN 시사점 4) | ◎ 재계획 주기 자유(킬존 2 개인 5Hz·분대 2Hz, web-ai 결론 4) | ◎ 빌더의 이름 참조가 곧 텍스트 매핑(HTN 시사점 3) | ○ 전제조건·효과 이중 서술이 표보다 김 | ○ 계획 길이 2~5, 월드 스테이트 최소 키로 축소 가능(HTN 시사점 5) | ○ 분해 로그(데시마 2024, web-ai 상세 1-3) | 없음 | 자체. 엔진 플러그인 핵심은 약 400줄(HTN 상세 3), 자체 구현 규모는 미측정(추정) | **Phase 4 조건부 채택**(D3) |
| GOAP(자체) | △ A\* 동률 tie-break 규약 필요; 다중 스레드 계획기 결정론 미문서(web-ai 상세 2-1) | ○ | ○ 액션 클래스 + 목표 집합 | △ 전제조건·효과·비용 삼중 서술 | △ 계획 길이 1~2 에 비용 탐색·절차적 전제조건(경로) 비용(web-ai 결론 2) | ○ 도구 투자 필요(툼 레이더 2015) | 없음 | 자체. 2013~2017 이후 신규 채택 감소(web-ai 상세 4) | 미채택 |
| 유틸리티(자체, 코드 정의) | ◎ 점수 = 순수 함수, 동률 정의 순서; "상위 N 무작위"만 시드 필요(web-ai 결론 6) | ◎ 사고 주기를 스케줄러 파라미터로(web-ai 결론 4) | ◎ 표 한 줄 = 고려사항 하나, 5열 표준(web-llm 결론 7) | ◎ JSON DSL 유효율 98~100%(web-llm 결론 6) | ◎ 후보 × 고려사항 곱, 비용순 조기 종료(web-ai 결론 5) | ◎ 후보별 점수표 = 로그 한 줄(드래곤 에이지 인퀴지션, web-ai 상세 1-2) | 없음 | 산업 검증(길드워 2 수백 종, 2015)(web-ai 상세 1-2) | **선택층으로 채택**(D1) |
| 하이브리드(유틸리티 + FSM, 시퀀스·페이즈 표, 조건부 HTN) | ◎ 유틸리티·FSM 행의 근거 합(§4.1·§4.2) | ◎ 유틸리티·FSM 행의 근거 합(§4.1·§4.2) | ◎ 유틸리티·FSM 행의 근거 합(§4.1·§4.2) | ◎ 평면 표 4개 + 메타(D6, web-llm 결론 6·7) | ◎ 유틸리티·FSM 행의 최소 + 시퀀스·페이즈 표 조회(§4.1·§4.2, D2) | ◎ 점수표 로그 + 분해 로그(web-ai 상세 1-2·1-3) | 없음(자체 코드) | 상용 공통 구조(호라이즌·DA:I·F.E.A.R., web-ai 결론 1) | **최종 형태**(D1~D3) |

약어: BT = engine-behaviortree-tick, ST = engine-statetree-runtime, HTN = engine-htnplanner-plugin, web-ai = web-ai-architecture-comparison, web-llm = web-llm-authorable-tooling, web-ue = web-ue-5-6-to-5-8-ai-changes.

마리당 비용 열의 수치 주의. "풀 액터 캐릭터(스켈레탈 메시 + 애님 블루프린트 + CMC(Character Movement Component, 캐릭터 이동 컴포넌트) + BT + 캡슐) 0.13~0.41ms/마리"는 타사 실측(StraySpark 2026, web-mass-monster-performance 결론 1)이고 BT 몫만 분리한 수치는 없다. "BT 0.042ms vs StateTree 0.011ms" 류 수치는 공식 출처가 없어 인용하지 않는다(web-ue-5-6-to-5-8-ai-changes 미확인 10). 유틸리티·StateTree 의 마리당 비용은 Phase 1 실측 항목이다([03 틱과 규모](03-tick-and-scale.md)).

---

## 4. 후보별 "왜 아닌가 / 왜 맞는가"

### 4.1 유틸리티 AI — 왜 맞는가

- **규모와 종 수에서 검증됐다.** 길드워 2 Heart of Thorns 는 IAUS(Infinite Axis Utility System, 무한 축 유틸리티 시스템)로 "수백 종 캐릭터 타입 × 각 수십 개 행동"을 데이터 주도로 운영했고 NPC 패키지를 7분에 조립했다(web-ai-architecture-comparison 상세 1-2, https://www.gameai.com/iaus.php 2012~2015, GDC 2015 "Building a Better Centaur"). "10분 안에 한 종"이라는 이 프로젝트의 제작 목표와 같은 규모다.
- **점수가 순수 함수다.** 고려사항 = 입력 정규화 → 응답 곡선 → 곱셈 결합, 0 이면 조기 종료. 월드·액터·타이머·벽시계에 손대지 않고 스냅샷(`FTDBrainInputs`)만 읽는 함수 형태라 결정론 검증 면적이 가장 좁다(web-ai-architecture-comparison 결론 5·6, D7).
- **정의 형식이 표다.** 업계 표준이 "입력 1 + 곡선 종류 + 파라미터 4(m, k, b, c) + 가중치" 다섯 열이고 스프레드시트로 프로토타입한다(web-llm-authorable-tooling 결론 7, https://tonogameconsultants.com/infinite-axis-utility-systems/ 2025). LLM 은 범용 코드보다 제한 JSON DSL 에서 문법 유효율 98~100% 다(web-llm-authorable-tooling 결론 6, arXiv 2510.16952 2025).
- **로그가 곧 설명이다.** 드래곤 에이지 인퀴지션은 스니펫별 점수표를 디버그 뷰로 노출했다(web-ai-architecture-comparison 상세 1-2, GameAIPro3 Chapter 31, 2017). TDGame 의 결정 로그 한 줄이 후보별 총점·고려사항별 점수·선택 이유다(D34, [04 시뮬레이터](04-combat-simulator.md)).
- **ARPG 계열 추세와 맞는다.** 디아블로 4 시즌 11 은 쿨다운 패턴 대신 "행동 풀에서 난이도별 선택"으로 전환했고(web-ai-architecture-comparison 결론 10, 2025 추정), 하데스는 Lua 테이블 파라미터로 적 행동을 정의한다(같은 결론, 2020년대).

약점과 완화: 장기 계획·무리 조정 표현력은 △ 다. 시퀀스(콤보)·페이즈 표(D2)로 먼저 보완하고, 그래도 부족한 콘텐츠가 실제로 들어오면 §5.3 조건으로 자체 HTN 을 붙인다.

### 4.2 FSM — 왜 실행층인가

- F.E.A.R. 의 FSM 은 Goto/Animate/UseSmartObject 3상태(실질 2상태)였고 "무엇을 할지"는 위층이 정했다(web-ai-architecture-comparison 상세 1-4, https://pages.cs.wisc.edu/~dyer/cs540/handouts/gdc2006_orkin_jeff_fear.pdf 2006). TDGame 도 실행층은 5상태(Idle/Move/Cast/Sequence/Stagger)만 갖는다. Cast 상태는 GAS(Gameplay Ability System, 게임플레이 어빌리티 시스템) 어빌리티 활성화 또는 `UTDDamageDefinition` 실행(D19)에 대응하며, 상세는 02 문서 소유다.
- FSM 만으로 선택까지 맡기면 전이 조건이 코드에 흩어져 표보다 읽기 어렵다. 그래서 선택은 유틸리티, 실행만 FSM 이다.

### 4.3 비헤이비어 트리 — 왜 아닌가

- 텍스트 직렬화기가 없어 정본을 텍스트로 둘 수 없고, 직접 짜면 노드 간 참조·서브오브젝트 복원까지 우리 몫이다(engine-behaviortree-tick 결론 5).
- 서비스·Wait 기본값이 전역 난수라 결정론이 "기본값을 0 으로 강제하는 규율"에 의존한다(결론 7).
- 마리당 UObject 3~4개(`UBehaviorTreeComponent`, `UBlackboardComponent`, `AAIController`, `UPathFollowingComponent`) + 컨트롤러 액터 틱이 남고, 블루프린트 노드는 컴포넌트마다 복제된다(결론 6·11). 수천 마리 규모(1,000~3,000 폰) 포럼 사례에서 BT 를 자체 블루프린트 로직으로 바꾸자 22~24 → 26~28fps 가 됐다(web-mass-monster-performance 상세 2-3·W4, 타사 실측·C++ 규칙 기반 수치는 미확인).
- 틱 간격 외부 제어가 불가하다(결론 2). 수동 스텝은 가능하지만(결론 1) 얻는 것은 엔진 디버거뿐이라고 조사 자체가 결론지었다(engine-behaviortree-tick "대안 비교를 위한 판단 재료").
- 헤일로 2(2005)의 BT 성공 사례는 "우선순위 목록 + props 외부 상태 + `.character` 파일 상속"이 핵심이었고, 이 셋은 유틸리티 표 + `extends` 상속(D6)으로 그대로 흡수된다(web-ai-architecture-comparison 상세 1-1).

### 4.4 StateTree — 왜 아닌가, 무엇이 아깝나

- 런타임은 좋다. 컨텍스트 직접 구동·시드 지정·분리 틱·예약 틱이 모두 있다(engine-statetree-runtime 결론 1·2·3·5). 유틸리티 선택기와 응답 곡선 고려사항도 내장한다(결론 4, StateTreeCommonConsiderations.h:45~108).
- 그러나 트리 정의가 `.uasset` 이고 조립·컴파일이 UncookedOnly 에디터 모듈에만 있다(결론 6). "에디터 없이 텍스트로 만든다"가 5.8 에 없다(web-llm-authorable-tooling 결론 2·3). 텍스트 → StateTree 생성기를 만들면 그 입력이 곧 우리 JSON 이므로 StateTree 는 중간 산출물이 되고 정본이 아니다.
- 사고마다 실행 컨텍스트를 스택에 만드는 것이 기본 사용법이고(여러 프레임에 걸친 보관 금지, StateTreeExecutionContext.h:277-279) `FStateTreeInstanceData` 를 배열에 두면 GC 참조 보고를 직접 해야 한다(StateTreeInstanceData.h:453). 비용 수치는 미측정이다(미확인 1).
- 엔진 의존 위험이 구체적이다. 5.6 예약 틱 정책 도입으로 기존 트리의 틱·전이가 멈춘 사례(https://forums.unrealengine.com/t/statetree-changes-at-5-6-version/2545493 2025), 버전당 폐기 24~71건(결론 8), 5.8 링크 에셋 서브트리 완료 전이 실패(UE-384337, web-ue-5-6-to-5-8-ai-changes 결론 10).
- 아까운 것은 리와인드 디버거 하나다. 이는 JSONL 결정 로그 + 비주얼 로거(.bvlog) 이중 송출로 대체한다(D34, engine-misc-decision-tools R11). 설계안 D 가 "컴포넌트·컨트롤러 없음"이라 적으면서 `AIControllerClass` 를 유지한 모순은 최종 설계에서 컨트롤러 자체를 없애는 것으로 정리했다(D16, D38).

### 4.5 엔진 HTNPlanner 플러그인 — 결함 3개

| 결함 | 내용 | 근거 |
|---|---|---|
| 1. 실행기 부재 | `UHTNBrainComponent` 는 생성자만 있고 멤버 `FHTNPlanner Planner` 를 어디서도 쓰지 않는다. `StartLogic/StopLogic/TickComponent` 재정의 없음. 디버거 카테고리도 빈 함수 | engine-htnplanner-plugin 결론 1, HTNBrainComponent.cpp:8-11, HTNBrainComponent.h:12-22, GameplayDebuggerCategory_HTN.cpp:16-23 |
| 2. 백트래킹 결함 | 복원점이 월드 스테이트·계획만 복구하고 `TasksToProcess` 스택은 복구하지 않아 실패한 메서드의 형제 태스크가 계획에 섞인다(파이썬 재현: 기대 `[C]` → 실제 `[C, B]`). 루트에서 만족 메서드가 없으면 "성공 + 빈 계획"을 반환해 실패와 구분 불가 | 결론 2, HTNPlanner.h:28-31·57-58·80, HTNPlanner.cpp:75-97 |
| 3. 방치 | `.uplugin` 설명이 "UE4's AI module", `VersionName 0.01`, `IsBetaVersion true`, 파이썬 프로토타입 주석 잔존, 본문 없는 테스트 3개. 기능 변경 흔적은 엔진 API 추종 수정뿐 | 결론 3, HTNPlanner.uplugin:2,4,6,13,15, HTNTest.cpp:467-512 |

추가 한계: 원시 태스크 전제조건·태스크 파라미터·반복 상한 없음, 태스크당 255개·도메인 64KB 하드 한계(결론 6, HTNPlanner.cpp:9-12, HTNBuilder.cpp:85,107,122-123). 엔진 소스는 읽기 전용이라 결함을 고칠 수 없다. 엔진 플러그인 핵심은 약 400줄(런타임 모듈 전체 약 1,100줄, 상세 3)이라 재작성이 싸다. 자체 구현 규모는 미측정(추정)이다. 차용할 설계 셋만 가져온다: 이름 기반 빌더 → 연속 메모리, 정수 배열 월드 스테이트, 복원점 스택(복원점에 태스크 스택 포함)(시사점 2).

### 4.6 자체 HTN — 왜 보조이고 왜 지금이 아닌가

- 상용 검증은 충분하다. 킬존 2(2007, 개인 5Hz·분대 2Hz), 호라이즌 제로 던(2017, "HTN planning and utility based decision making"), 트랜스포머 폴 오브 사이버트론(2013, 총순서 전방 분해가 GOAP 보다 "considerably faster"), 데시마 HTN(2024, 생성 C++ 백트래킹), 메트로 어웨이크닝(2024, Maks Maisak 플러그인)(web-ai-architecture-comparison 상세 1-3).
- 그러나 잡몹은 계획이 필요 없다(결론 2). HTN 이 값어치를 내는 곳은 "2~5 길이 계획이 상황에 따라 달라지는" 보스 안무와 무리 역할 배정뿐이고, 그 콘텐츠는 아직 없다.
- 따라서 D3: 요구가 실제 콘텐츠로 들어올 때 Phase 4 에서 추가한다. 형식은 JSON 도메인(`composite/primitive/effects/replan_hz/max_iterations`), 재귀 종료 정적 검사, 복원점에 태스크 스택 포함, 실패는 실패로 반환. 도입 조건은 §5.3.

### 4.7 GOAP — 왜 아닌가

- 실측 계획 길이 1~2 액션(F.E.A.R. 최대 8.5 plans/s/NPC)에 A\* 비용 탐색·절차적 전제조건(경로 탐색) 비용이 붙는다(web-ai-architecture-comparison 결론 2, 상세 1-5, AIIDE 2014).
- "개발 시간 대부분이 라이브러리가 아니라 Goal/Action 코드 지원에 든다"(크리스탈 다이내믹스 GDC 2015, https://media.gdcvault.com/gdc2015/presentations/Conway_Chris_Goal-Oriented_Action_Planning.pdf). 정의 표면이 표가 아니라 클래스다.
- 계획형이 필요하면 HTN 이 같은 효과를 비용·정렬 없이 더 싸게 준다(결론 3). 산업도 킬존 2 이후 HTN 으로 이동했고 2013~2017 이후 GOAP 신규 채택이 줄었다(상세 1-4, 상세 4).
- 결정론 면에서는 동률 비용 tie-break 규약과 다중 스레드 계획기 보장 부재(미확인)가 검증 면적을 넓힌다(상세 2-1).

### 4.8 하이브리드(유틸리티 + FSM + 시퀀스·페이즈 표 + 조건부 HTN) — 최종 형태

- 블리자드 Brian Schwab(GDC 2010): "순수 아키텍처는 거의 쓰지 않는다". 호라이즌은 HTN + 유틸리티, 드래곤 에이지 인퀴지션은 유틸리티 평가 트리 + BT 실행 트리, F.E.A.R. 는 GOAP + 3상태 FSM(web-ai-architecture-comparison 결론 1, 상세 4).
- TDGame 의 2층: 선택층 = 유틸리티(사고 주기), 실행층 = FSM 5상태(이동·판정 주기). 보조 = 시퀀스·페이즈 표. 확장 = 자체 HTN(보스·무리). 한 종의 정의는 평면 표 4개(actions, considerations, sequences, phases) + 메타 한 파일이다(D6, [02 아키텍처와 정의 형식](02-architecture-and-definition-format.md)).

선택층의 규칙은 아래 순서로 한 사고 안에서 끝난다. 점수기 규칙 코드는 [02 §5.6](02-architecture-and-definition-format.md#56-점수기-결정성-규칙-코드d1) 이 정본이며 이 문서는 코드를 싣지 않는다.

| 순서 | 규칙 | 근거 |
|---|---|---|
| 1 | 현재 페이즈에 없는 행동(D2 페이즈 표)과 쿨다운 중인 행동은 후보에서 뺀다 | 02 §5.6, D2 |
| 2 | 점수 = 가중치 × Π(고려사항 점수). 고려사항은 비용 등급 순으로 평가하고 0 이 나오면 즉시 중단한다 | D1, D7 |
| 3 | 최고점 선택, 동률은 정의 순서(작은 인덱스)가 이긴다(안정 정렬) | D1 |
| 4 | 관성: 최소 유지 스텝(`HoldRemainingSteps > 0`) 동안은 무조건 현재 행동을 유지한다. 유지가 끝난 뒤에는 도전자 점수가 `현재 점수 × InertiaSwitchRatio` 를 넘어야 교체한다. 페이즈 `on_enter` 와 경직은 관성을 무시한다 | D1, 02 §5.1 규칙 9 |
| 5 | 종 옵션 `weighted_random` 일 때만 상위 N 가중 무작위(몬스터별 AI 스트림 소비) | D2, D29 |

곡선 종류는 Linear/Quadratic/Logistic/Logit/Gaussian/Constant/Binary 의 4파라미터 수식이며 `FRuntimeFloatCurve` 를 쓰지 않는다(수식이 단순해 부동소수 결정론에 유리, web-llm-authorable-tooling 상세 3). 부동소수는 "같은 빌드·같은 CPU 명령셋" 조건에서 비트 동일을 목표로 하며, 초월함수(Logistic/Logit/Gaussian 의 exp·log)·FMA·`/fp:fast` 주의와 골든 해시 절차는 [04 결정론 전투 시뮬레이터](04-combat-simulator.md) 의 결정론 게이트를 따른다(D31).

UKGame(과거 설계)은 BT·StateTree·HTN·FSM 네 모델을 함께 썼다. TDGame 이 선택층 하나(유틸리티) + 실행층 하나(FSM)로 줄인 이유는 셋이다(결정 기록 §10). (1) 정본이 에셋이 아니라 텍스트여야 생성형 AI 제작 루프(D37)가 성립한다. (2) 모델이 하나면 결정론 검증 면적이 "입력 스냅샷 + 시드 스트림 + 순회 순서"로 줄어든다(§6). (3) 마리당 비용이 POD 슬롯 하나로 고정되어 컨트롤러·컴포넌트·컨텍스트 생성이 없다(§4.3·§4.4). UKGame 방식을 복제하지 않는 것은 결정 사항이지 검토 누락이 아니다.

---

## 5. 최종 결정과 재검토 조건

### 5.1 결정 D1~D4(결정 기록 원문 요약)

| 번호 | 결정 | 이 문서의 근거 절 |
|---|---|---|
| D1 | 주력 = 코드 정의 유틸리티(행동 × 고려사항 표, 4파라미터 곡선 7종, 곱 결합, 비용 등급 순 평가·0점 조기 종료, 최고점, 동률 정의 순서, 관성 InertiaSwitchRatio + 최소 유지 스텝) + 실행 FSM 5상태. 관성의 JSON 키는 종 단위 `inertia.switch_ratio` / `min_hold_seconds`(초) 이고 로더가 `think_hz` 로 스텝 수로 환산한다(02 §5.1 규칙 9). D1 원문의 "스텝" 과 02 의 "초" 단위 불일치는 결정 기록 담당에게 보고 | §4.1, §4.2, §4.8 |
| D2 | 보조 = 데이터 정의 시퀀스(콤보) + 보스 페이즈 표. 종 단위 "상위 N개 가중 무작위"는 AI 스트림만 소비, 기본 최고점 | §2, §4.1 |
| D3 | 자체 C++ HTN 은 "3페이즈 이상 보스 안무 또는 무리 역할 배정"이 실제 콘텐츠로 들어올 때 Phase 4 추가. 엔진 HTNPlanner 미채택 | §4.5, §4.6, §5.3 |
| D4 | BT·StateTree·GOAP·Mass 두뇌·Mover·MLAdapter 전투 코어 배제. StateTree 는 보스 연출·시각 디버깅 재검토 선택지로만 | §1, §4.3, §4.4, §4.7, §5.2 |

Mass·Mover·MLAdapter 의 배제 근거는 이 문서 범위 밖이다. 요약만 적는다: Mass 는 엔티티 압축이 벽시계 예산이고(engine-mass-entity-ai 결론 5), Epic 은 Mass 의 결정론에 대해 "결정론적인 것은 프로세서 실행 순서뿐"이라 답했으며(web-ue-5-6-to-5-8-ai-changes 결론 2), Epic 직원(James Keeling, 2025)은 "BehaviorTree 나 GAS 같은 것들은 Mass 로 이식되지 않을 가능성이 높다"고 밝혔고(web-mass-monster-performance 결론 4·상세 5) 5.8.0 병렬 회귀가 있었다. 상세는 [03 틱과 규모](03-tick-and-scale.md), ML 쪽은 [05 머신러닝과 생성형 AI](05-ml-and-generative-ai.md).

### 5.2 StateTree 를 보스 연출용으로 재검토할 조건

아래 조건이 **모두** 충족될 때만 재검토한다. 하나라도 빠지면 시퀀스·페이즈 표(D2) 또는 자체 HTN(D3)으로 간다.

| 번호 | 조건 | 이유 |
|---|---|---|
| S1 | 대상이 보스·영웅급 1~2종이고 잡몹 경로와 코드가 섞이지 않는다 | 마리당 컨텍스트 생성 비용 미측정(engine-statetree-runtime 미확인 1). 소수에만 허용 |
| S2 | 정본은 여전히 `Content/MonsterAI/Definitions/<Id>.json` 이고, StateTree 에셋은 런타임 TDGame 모듈에 둔 커맨드렛(D13)을 에디터 타깃(UnrealEditor-Cmd)으로 실행해 그 JSON 에서 생성하는 파생 산출물이다. StateTreeEditorModule 의존은 Build.cs 의 `Target.bBuildEditor` 조건부로만 추가하고 별도 에디터 모듈은 만들지 않는다 | 텍스트 정본 원칙(D6). 조립·컴파일은 UncookedOnly 모듈(ST 결론 6, StateTree.uplugin:34-35). `UCommandlet` 은 Engine 모듈(Commandlet.h:39-41) |
| S3 | 실행은 `UStateTreeComponent` 가 아니라 `UTDMonsterThinkSubsystem` 의 슬롯이 소유한 `FStateTreeInstanceData` 를 `FStateTreeExecutionContext` 로 직접 구동하고, `Start(FStartParameters{.RandomSeed})` 에 몬스터별 AI 스트림 시드를 넣는다 | 컴포넌트 경로는 비결정(ST 결론 3, StateTreeComponent.cpp:194-198). 단일 틱 함수 원칙(D15) |
| S4 | 프로젝트 스키마가 `IsScheduledTickAllowed()` 를 재정의하고 전역 난수·벽시계를 쓰는 태스크·조건을 `IsStructAllowed` 로 차단한다 | 예약 틱 전제(ST 결론 5). 결정론 |
| S5 | 시뮬 게이트(같은 프로세스 2회 + 다른 프로세스 1회 + 골든 해시, D31)를 StateTree 보스 시나리오가 통과한다 | 결정론을 약속이 아니라 테스트로 보증 |
| S6 | 리와인드 디버거가 "JSONL 결정 로그 + .bvlog 이중 송출"로 풀 수 없는 문제를 실제로 해결한다는 사례가 하나 이상 기록된다 | StateTree 가 주는 유일한 순이익이 디버거다(§4.4) |
| S7 | UE-384337(링크 에셋 서브트리 전이 실패)은 5.8.2 에도 잔존 보고이고 이슈 트래커에 수정 목표 버전이 없다. 수정이 확인된 엔진 버전 이상으로 고정했을 때만 S7 을 충족한 것으로 본다 | web-ue-5-6-to-5-8-ai-changes 결론 10, 상세 4(94·183·224행) |

### 5.3 자체 HTN 을 도입할 조건

| 번호 | 조건 | 검출 방법 |
|---|---|---|
| H1 | 3페이즈 이상 보스에서 "행동 열의 순서가 상황에 따라 달라지고 길이가 2 이상"인 안무가 콘텐츠 요구로 확정된다 | 페이즈 표 + 시퀀스로 작성 시도 → 검증기 2단(시퀀스 길이·페이즈 순환) 또는 분기 표현 불가로 실패 |
| H2 | 무리 역할 배정(포위·교대·엄호 등)이 요구되고 근접 자리 토큰(ring_slot, D17)만으로 표현되지 않는다 | 시뮬 결과의 행동 점유율·교체율(D33)이 목표 밴드를 벗어남 |
| H3 | 대상이 동시 ≤ 10 마리(설계안 B 제안값, 결정 기록 D3 에는 마릿수 없음, 실측 후 조정)인 보스·엘리트·무리 리더로 한정되고 잡몹은 계속 유틸리티다 | 설계 검토. 잡몹 계획 길이 1~2 는 계획기 불필요(web-ai 결론 2) |
| H4 | 형식은 JSON 도메인(`composite/primitive/effects/replan_hz/max_iterations`), 재귀 종료 정적 검사, 복원점에 태스크 스택 포함, 실패는 실패로 반환, 계획 수립은 스텝 경계에서만 | 엔진 플러그인 결함 2 를 설계로 배제(§4.5). 검증기 1·2단 확장 |
| H5 | HTN 도메인의 원시 태스크는 유틸리티 행동 원시(C++ 등록표, D7)와 같은 것을 가리키고 FSM 5상태로 내려간다 | 실행층 이원화 금지 |

---

## 6. 이 결정이 각 축에 주는 이점

| 축 | 이점 | 상세 문서 |
|---|---|---|
| 결정론 | 점수기·전이·계획 수립이 모두 스냅샷만 읽는 순수 함수라 결정론 검증 면적이 "입력 스냅샷 + 시드 스트림 + 순회 순서"로 줄어든다. 엔진 AI 의 전역 난수·컴포넌트 시드·1틱 지연 같은 변수가 처음부터 없다 | [04 결정론 전투 시뮬레이터](04-combat-simulator.md) |
| 틱·규모 | 사고 주기가 AI 모델 밖의 스케줄러 파라미터(네 채널 × LOD 4단 주기표, D22)다. BT 의 간격 덮어쓰기·컨트롤러 액터 틱·마리당 UObject 3~4개가 없고 상태 정본이 SoA(Structure of Arrays, 배열 구조체) 슬롯이라 단일 틱 함수가 SimulationId 순으로 배치 평가한다 | [03 틱 제어와 대량 몬스터 규모 전략](03-tick-and-scale.md) |
| LLM 제작성 | 정의가 평면 표 4개 + 메타 한 파일이고 스키마 문서는 코드에서 생성된다(D9). 종당 컴파일 0회, 에디터 0회, 검증 3단 + 시드 배치 + 30줄 요약이 제작 루프다(D37). 에셋·MCP·Python 어느 것도 경로에 없다 | [02 아키텍처와 몬스터 AI 정의 형식](02-architecture-and-definition-format.md) |
| 머신러닝 | 두뇌를 신경망화하지 않으므로 결정론이 유지된다. ML 은 (1) 플레이어 대리 봇 행동 복제, (2) JSON `tune:true` 잎 벡터의 CMA/PSO 블랙박스 튜닝 두 지점에만 개입한다(D36). 유틸리티 표의 수치가 곧 튜닝 벡터라 별도 파라미터화가 필요 없다 | [05 머신러닝과 생성형 AI 통합](05-ml-and-generative-ai.md) |
| 로드맵 | Phase 1 에서 등록표·로더·점수기·FSM·서브시스템 틱만으로 "고블린 10마리 vs 규칙 봇 승률"에 도달한다. StateTree·HTN 은 조건부 항목이라 초기 일정에 없다 | [07 로드맵과 할 일 대장](07-roadmap-and-tasks.md) |

---

## 미결 사항(사용자 결정 필요)

1. **관성 기본값 수치.** 형식과 위치는 [02 §5.1 규칙 9](02-architecture-and-definition-format.md) 의 종 단위 `inertia: { switch_ratio, min_hold_seconds }` 로 확정됐고(로더가 `think_hz` 로 스텝 수 환산), 초기 기본값은 02 예시의 1.15 / 0.4초다. 수치는 Phase 1 실측 후 조정한다.
2. **"상위 N개 가중 무작위" 옵션의 허용 범위.** 종 단위 옵션으로 두기로 했으나(D2) 잡몹 전체에 허용할지 보스·엘리트에만 허용할지는 열려 있다. 허용 범위가 넓을수록 시뮬 시드 수 요구가 커진다.
3. **StateTree 재검토 조건 S6 의 판정 주체.** "디버거 없이는 풀 수 없는 문제"를 누가 어떤 기준으로 기록할지 정하지 않았다.
4. **자체 HTN 도입 조건 H1·H2 의 콘텐츠 시점.** 3페이즈 보스·무리 역할 배정이 어느 마일스톤에 들어오는지는 콘텐츠 계획에 달려 있다.
5. **마리당 비용 실측 항목.** 유틸리티 점수기 300마리 1ms 미만(D10)은 [07 M3-07](07-roadmap-and-tasks.md)(할 일)과 [02 §5.12](02-architecture-and-definition-format.md)(전환 조건)가 정본이라 여기서 되풀이하지 않는다. 남은 결정은 StateTree 컨텍스트 생성 비용(미측정)을 S1~S7 진입 시에만 측정할지 여부다.

## 근거 색인(인용한 조사 파일 목록)

| 조사 파일 | 인용한 결론·절 |
|---|---|
| research/engine-behaviortree-tick.md | 결론 1·2·3·4·5·6·7·11, 상세 1-3·1-4·1-5, "대안 비교를 위한 판단 재료" |
| research/engine-statetree-runtime.md | 결론 1·2·3·4·5·6·8, 상세 1·2·4(우회 경로)·6(폐기 건수), 시사점, 미확인 1 |
| research/engine-htnplanner-plugin.md | 결론 1·2·3·5·6, 상세 3(코드 규모), 시사점 2·3·4·5 |
| research/web-ai-architecture-comparison.md | 결론 1·2·3·4·5·6·10, 상세 1-1·1-2·1-3·1-4·1-5·1-7·2-1·4, 시사점 |
| research/web-llm-authorable-tooling.md | 결론 2·3·6·7, 상세 1-1·1-2·3 |
| research/web-ue-5-6-to-5-8-ai-changes.md | 결론 1·2·10, 상세 4(UE-384337 잔존), 미확인 10 |
| research/web-mass-monster-performance.md | 결론 1·4, 상세 2-3·5(Epic 발언 원문) |
| research/engine-mass-entity-ai.md | 결론 5 |
| research/engine-misc-decision-tools.md | R11(비주얼 로거 이중 송출) |
| 설계안·심사(세션 임시 산출물, 저장소 미포함; 요지는 00 결정 기록에 반영) | A §1, B §1·§3.3, C §1, D §1; 심사 판정 "StateTree vs 자체 유틸리티" 3인 항목, C안 §2.1 커맨드렛 모듈 반박(요지는 D1~D5·D13) |
| [00 결정 기록](00-decision-record.md) | D1~D7, D9, D10, D13, D15~D19, D22, D29, D31, D33, D34, D36~D38, §10(UKGame 과의 차이) |
| 엔진 소스(읽기 전용) | StateTree.h:376-378·675-685, StateTreeExecutionContext.h:277, StateTreeTypes.h:172-198, StateTree.uplugin:34-35, Commandlet.h:39-41, MockAI_BT.cpp:26-27, MockAI.cpp:78-94 |
| 이 묶음의 다른 문서 | [02 §5.1 규칙 9·§5.6·§5.12](02-architecture-and-definition-format.md), [04 결정론 게이트](04-combat-simulator.md), [07 M3-07](07-roadmap-and-tasks.md) |
