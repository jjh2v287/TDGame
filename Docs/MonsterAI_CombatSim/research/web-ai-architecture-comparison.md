# 웹: 게임 몬스터 AI 아키텍처 비교(BT/ST/FSM/HTN/GOAP/유틸리티) 산업 사례와 대량 몬스터 ARPG 관점

- 작성일: 2026-09-09
- 조사 방법: WebSearch 로 후보를 찾고, WebFetch 로 본문을 직접 열어 확인. PDF 는 로컬로 저장 후 `pdftotext` 로 본문을 추출해 원문 줄을 인용.
- 용어: BT(Behavior Tree, 비헤이비어 트리), ST(StateTree, 언리얼 스테이트 트리), FSM(Finite State Machine, 유한 상태 기계), HTN(Hierarchical Task Network, 계층적 태스크 네트워크), GOAP(Goal Oriented Action Planning, 목표 지향 행동 계획), IAUS(Infinite Axis Utility System, 무한 축 유틸리티 시스템), DSE(Decision Score Evaluator, 결정 점수 평가기), LOD(Level of Detail, 상세도 단계), ECS(Entity Component System, 엔티티 컴포넌트 시스템), LLM(Large Language Model, 대규모 언어 모델).
- 표기 규칙: 확인한 사실은 URL·연도를 붙였다. 본문을 열지 못한 것은 "미확인"으로 남겼다.

---

## 결론 요약 (설계 결정에 바로 쓸 수 있는 문장들)

### 추천 후보 3개 (탑다운 ARPG · 대량 몬스터 · 코드 정의 · 결정론 조건)

| 순위 | 후보 | 한 줄 근거 |
|---|---|---|
| 1 | **코드 정의 유틸리티 AI(고려사항 테이블 + 응답 곡선) + 얇은 FSM 실행층** | 대량·다품종 캐릭터에서 검증(길드워 2: "수백 종 캐릭터 타입, 각각 수십 개 행동", 2015)되었고, 점수 계산이 순수 함수라 결정론 확보가 쉬우며, 고려사항이 표 형태라 LLM 이 읽고 고치기 가장 쉽다. 디아블로 4 시즌 11 도 "행동 풀에서 선택" 방식으로 전환(2025). |
| 2 | **코드 HTN(총순서 전방 분해, C++ 빌더 도메인)** — 엘리트·보스·무리 조정용 상위 계획층 | 비용 탐색이 없어 GOAP 보다 빠르고(Transformers, 2013), 재계획 조건이 3가지로 명시적(계획 완료/실패, 계획 없음, 센서로 월드 상태 변화)이라 결정론 시뮬레이션에 맞다. 호라이즌·킬존·메트로 어웨이크닝(2024)이 상용 검증. |
| 3 | **언리얼 StateTree(유틸리티 선택기 + 예약 틱 정책)** — 소수 영웅급 액터 한정 또는 대체안 | 5.6 부터 "Scheduled Tick Policy" 로 틱 빈도 제어 가능(2025), 유틸리티 기반 자식 선택과 Consideration 노드를 내장. 그러나 에셋(에디터) 정의가 기본이고, Mass 결합 시 신호 기반 틱이라 헤드리스 결정론 검증 비용이 크다. |

### 설계 결정 문장 (근거 표기)

1. **상용 AI 는 순수 단일 모델을 거의 쓰지 않는다.** 블리자드의 Brian Schwab 은 GDC 2010 에서 "AI programmers rarely use a pure architecture such as a State Machine, Planner, or Behavior Tree in isolation" 이라 했고, 게릴라는 호라이즌 제로 던에 "HTN planning and utility based decision making" 의 조합을 썼다(2017). → TDGame 도 "상위 선택(유틸리티/HTN) + 하위 실행(FSM 시퀀스)" 2층 구조로 간다.
2. **GOAP 의 실제 계획 길이는 1~2 액션, HTN 은 2~5 액션이며, 계획기 예산 대부분이 전투가 아닌 순찰·애니메이션에 쓰였다**(AIIDE 2014, F.E.A.R./킬존 3/트랜스포머 3 로그 분석). 논문은 "반복 액션(길이 1 계획)과 고정 계획에는 계획기 대신 대안을 고려하라" 고 결론. → 잡몹 다수는 계획기가 필요 없다. 유틸리티/FSM 으로 충분.
3. **HTN 은 비용 탐색·정렬이 없어 GOAP 보다 빠르다.** Humphreys(2013): "Because we aren't using a heuristic or cost ... we can skip any kind of sorting. These features allowed the HTN planner in Transformers: Fall of Cybertron to be considerably faster than our GOAP system". → 계획형이 필요하면 GOAP 가 아니라 HTN.
4. **재계획 주기는 명시적으로 잘라야 한다.** 킬존 2 는 개인 5Hz, 분대 2Hz, 진영 리더 6초 주기로 재계획하고, "Continue current plan" 분기로 "twitchy behaviour" 를 막았다(2007 논문, 2009 강연). 드래곤 에이지 인퀴지션의 유틸리티 평가는 "typically repeated on every AI update pass, but can be performed as often as appropriate"(2017). → TDGame 의 사고 주기(think tick)는 AI 모델과 분리된 스케줄러 파라미터로 둔다.
5. **유틸리티 평가 비용은 고려사항 순서(싼 것 먼저, 0 이면 조기 종료)로 제어된다**(Lewis, 2017: "cheaper considerations can be ordered first and early-out to avoid processing more expensive ones"; 레이캐스트는 비싸므로 마지막). → 고려사항 테이블에 "비용 등급" 열을 두고 정렬을 강제한다.
6. **결정론은 AI 모델보다 실행 환경(고정 스텝, 시드 RNG, 순회 순서, 부동소수점)이 좌우한다**(lockstep 사례, 2015). 유틸리티의 "상위 몇 개 중 무작위 선택"(심즈, 2023 해설)이나 StateTree 의 "Random Weighted by Utility" 선택기는 반드시 시드 RNG 를 써야 한다. 다중 스레드 계획기(Crashkonijn GOAP, Unity Job)는 결정론 보장이 문서화되어 있지 않다(미확인).
7. **텍스트/코드 정의가 LLM 친화적이라는 것은 산업이 이미 택한 방향이다.** BehaviorTree.CPP 는 XML DSL 을 런타임에 로드하고(v4.9, 2026), Fluid HTN 은 C# 빌더 코드, GTPyhop 은 Python 함수가 도메인이다(2021). 유니티는 LLM 으로 트리를 생성하는 Muse Behavior 를 2024년에 냈다가 2025-05 에 기능을 껐다(Muse 종료). 연구는 LLM 출력을 제한된 DSL 로 묶는 방향(Real-Time World Crafting, 2025). → TDGame 은 "제한된 C++/데이터 테이블 DSL" 을 정의하고 LLM 이 그것만 만지게 한다.
8. **엔진 StateTree 는 5.6 에서 틱 정책이 바뀌어 기존 트리가 멈추는 사례가 있었다**(에픽 포럼, 2025-06~10). Blueprint 태스크는 C++ 보다 "significantly worse" 성능. → StateTree 를 쓰더라도 C++ 구조체 태스크 + 명시적 틱 정책이 전제.
9. **Mass(ECS) 결합 StateTree 는 신호(signal) 기반으로 틱한다**("Tick on StateTree Tasks are only ran once and with subscribed signals"). 1만 NPC 60fps 사례는 사전 계산 흐름장(flow field)과 LOD 로 달성했고 StateTree 를 언급하지 않는다(2026-03). → 초대량(수천) 단계에서는 AI 모델보다 데이터 배치·LOD 가 성능을 결정한다.
10. **ARPG/로그라이크 계열은 스크립트·데이터 테이블 기반 FSM 이 주류.** 하데스는 `EnemyData` Lua 테이블(`AIEndHealthThreshold`, `DefaultAIData.LeapRetreatAtHealthPercent`)과 `EnemyAI.lua` 로 적 행동을 정의하고, 디아블로 3 는 몬스터를 "행동 범주" 로 분류(2010), 디아블로 4 시즌 11 은 "set behavior pool" 에서 난이도별로 선택(2025). 패스 오브 엑자일의 내부 구현은 미확인.

---

## 상세 조사

### 1) 각 모델의 상용 사례와 포스트모템

#### 1-1. 비헤이비어 트리(BT)

| 사실 | 출처 | 연도 |
|---|---|---|
| 헤일로 2: 약 50개 행동의 계층 DAG. 부동소수점 관련도 대신 **우선순위 목록의 이진 결정**. "impulse" 가 트리 다른 곳의 행동을 참조해 우선순위를 동적으로 바꿈. 행동 태그를 비트벡터로 두어 트리의 큰 부분을 잠금/해제. | https://www.gamedeveloper.com/programming/gdc-2005-proceeding-handling-complexity-in-the-i-halo-2-i-ai | 2005 |
| 메모리: 행동별 영구 저장 대신 액터당 작은 풀을 깊이별로 나눠 100 액터 기준 192KB → 약 25KB. 지속 상태(쿨다운, 탐색 실패)는 "props" 로 외부화. `.character` 파일 상속으로 30여 캐릭터 타입의 파라미터 폭주 방지. | 위와 동일 | 2005 |
| BT 설계 함정 3가지: (1) 조직용 클래스 남발, (2) 언어(DSL)를 너무 일찍 구현, (3) 모든 통신을 블랙보드로 강제. 저자는 블랙보드와 트리를 강하게 분리할 것을 권고. | https://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter09_Overcoming_Pitfalls_in_Behavior_Tree_Design.pdf (로컬 추출, 9.3.1~9.3.3 절) | 2017 |
| BT 는 "문제 해결(탐색)" 이 아니라 "지능형 제어" 라 GOAP 보다 계산이 싸고, 상위 태스크가 하위를 선점한다. 무상태(stateless) 제어 버그는 기능 테스트에서 잡히지만 유상태 버그는 QC 이후에 잡힌다는 주장. 100줄 C# 연산자 오버로딩(&&, \|\|) 구현으로 DSL/비주얼 스크립팅을 피함. | https://www.gamedeveloper.com/programming/behavior-trees-and-the-future-of-intelligent-control-2 | 2020-10-12 |

발췌(헤일로 2 의 핵심 아이디어, 원문 요약):
```
behavior tree = prioritized list (binary decisions), not float relevancy
impulse       = free-floating trigger referencing a behavior elsewhere
behavior mask = bitvector of common conditions (vehicle, alertness) gates subtrees
props         = external persistent state (cooldowns, search failures)
```

#### 1-2. 유틸리티 AI

| 사실 | 출처 | 연도 |
|---|---|---|
| IAUS: 2012년 고안, ArenaNet 길드워 2: Heart of Thorns 에 배치(2015). "database oriented design tool" 로 완전 데이터 주도, NPC AI 패키지를 "7분" 만에 구성. 개체→분대→군대→무생물 객체까지 확장. | https://www.gameai.com/iaus.php | 2012~2015 |
| GDC 2015 "Building a Better Centaur: AI at Massive Scale": "modular, utility-based AI system and a powerful influence map engine", 수백 종 에이전트 타입 × 각 수십 개 행동, 이전보다 "significantly less processing time". | https://www.gdcvault.com/play/1021848/Building-a-Better-Centaur-AI | 2015 |
| Heart of Thorns 구현 상세: 각 행동은 DSE 에 매핑, think cycle 마다 모든 DSE 를 채점해 최고점 선택. 고려사항 = 입력을 [0,1] 정규화 → 응답 곡선 → 곱셈 결합. 점수가 0 이면 조기 종료하므로 싼 고려사항을 먼저 배치. 모든 타깃에 경로 계산은 "too expensive" 라 생략, 레이캐스트는 마지막에. | https://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter13_Choosing_Effective_Utility-Based_Considerations.pdf (로컬 추출, 13.2/13.4/13.5 절) | 2017 |
| 드래곤 에이지 인퀴지션 BDS(Behavior Decision System): 60개 이상 능력. 각 "behavior snippet" 은 점수 노드가 박힌 **평가 트리**로 점수+타깃을 반환하고, 최고점 스니펫의 **실행 트리**(BT)를 돌린다. 평가 주기는 "typically repeated on every AI update pass, but can be performed as often as appropriate". 디자이너용 점수 범위 규약(행동 부류별 동적 범위)을 명문화. | https://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter31_Behavior_Decision_System_Dragon_Age_Inquisition%E2%80%99s_Utility_Scoring_Architecture.pdf (로컬 추출, 31.3~31.5 절) | 2017 |
| 심즈: 스마트 오브젝트가 효용을 "광고" 하고, 점수 = 광고값 × 현재 동기(motive) 가중. **상위 점수 몇 개 중 무작위 선택**으로 예측 가능성 회피. 배경 심은 단순화된 시뮬레이션. | https://gmtk.substack.com/p/the-genius-ai-behind-the-sims | 2023 |
| 언리얼용 IAUS 오픈소스(Project Borealis): BT 데코레이터/서비스 안에 유틸리티를 넣은 구조, 고려사항은 C++ 클래스(`UIAUSConsideration`, `UIAUSAxisInput_Range`) + 에디터 곡선. "Inertia Weight" 로 행동 교체 최소 간격 제어. | https://github.com/ProjectBorealis/IAUS/wiki | 2024-05 |

발췌(DA:I 평가 루프 의사코드, 원문 Listing 31.1 요약):
```cpp
Optional<SnippetEvaluation> EvaluateSnippets() {
  list<SnippetEvaluation> evaluated;
  for (snippet : registeredSnippets) {
    auto e = snippet.evaluate();        // 평가 트리 → score + target
    if (e.result) evaluated.push(e);
  }
  sortByDescendingScore(evaluated);
  return evaluated.empty() ? none : evaluated.first();
}
```

#### 1-3. HTN

| 사실 | 출처 | 연도 |
|---|---|---|
| 킬존 2 봇: 모든 계획은 단일 복합 태스크 `behave` 에서 시작. 데몬(C++)이 매 업데이트 월드를 HTN 사실(fact)로 번역(예: `(door door_1 open)`), `request_path` 같은 요청 사실은 한 사이클 지연, `(call add ?a ?b)` 콜 텀은 즉시 실행. 원시 태스크 예: `!fire_burst_at_threat`, `!walk_segment`. | https://www.guerrilla-games.com/media/News/Files/VUA07_Verweij_Hierarchically-Layered-MP-Bot_System.pdf (로컬 추출, 3.2.2~3.2.4 절) | 2007 |
| 재계획 주기: "The agents replan at a rate of five times per second ... Squads replan at a rate of two times per second", 진영 리더는 6초마다. 긴 계획은 금방 무효화되므로 짧은 국소 계획. 같은 태스크를 다시 만나면 재계획 대신 태스크를 갱신. | 위와 동일(1585~1598행) | 2007 |
| 파리 GDC 2009 강연 노트: 5Hz 재계획, "Continue current plan" 분기로 twitchy 방지. AI 가 CPU 예산의 1/4(PPU 의 50%)로 30fps 에서 15 봇 + 6 터렛 + 6~8 분대. AI 로직은 격프레임 실행. 계획기는 Erol/Hendler/Nau(AAAI-94) 기반. | https://aarmstrong.org/notes/paris-2009/the-ai-of-killzone-2s-multiplayer-bots | 2009 |
| 호라이즌 제로 던: "combination of HTN planning and utility based decision making", 무리 조정 시스템. | https://www.guerrilla-games.com/read/the-ai-of-horizon-zero-dawn | 2017 |
| 호라이즌 상세: 개체 에이전트와 물리 실체 없는 그룹 에이전트, 공유 블랙보드는 **매 프레임이 아니라 주기적으로** 갱신(하이브 마인드 방지). 28 종 머신. 역할(순찰/도주/전투/수집)이 HTN 목표를 결정. | https://www.gamedeveloper.com/design/behind-the-ai-of-horizon-zero-dawn-part-1- | 2019 |
| Decima HTN(2024 강연): 전제조건에 대해 "backtracking (similar to Prolog)", 그 흐름을 **생성된 C++ 코드**로 실현, HTN 분해를 인게임에서 디버그. | https://www.guerrilla-games.com/read/htn-planning-in-decima | 2024-11-25 |
| 트랜스포머 폴 오브 사이버트론: 총순서 전방 분해 HTN. 재계획 조건 3가지 = "the NPC finishes or fails the current plan, the NPC does not have a plan, or the NPC's world state changes via a sensor". 월드 상태는 enum 인덱스 배열. 휴리스틱/비용이 없어 정렬 생략 → 전작 GOAP 보다 "considerably faster". | https://www.gameaipro.com/GameAIPro/GameAIPro_Chapter12_Exploring_HTN_Planners_through_Example.pdf (로컬 추출, 12.2~12.5 절) | 2013 |
| Maks Maisak HTN 플러그인(언리얼): 노드 그래프 에디터, C++/Blueprint 태스크, 블랙보드를 월드 상태로 사용, 최저 비용 우선순위 큐, "Plan rechecking" 으로 잔여 단계 재검증, EQS 통합. **메트로 어웨이크닝(2024-11 출시) 적 AI 의 근간**. Fab 마켓 판매, 포럼 글 2025-07. | https://github.com/maksmaisak/htn/blob/master/README.md , https://github.com/maksmaisak/htn/blob/master/planning.md , https://maksmaisak.dev/portfolio.html , https://forums.unrealengine.com/t/maks-maisak-hierarchical-task-network-planning-ai/2597962 | 2024~2025 |

발췌(Humphreys 총순서 전방 분해 핵심, 원문 의사코드 요약):
```
push root compound task; workingWS = copy(worldState)
while tasks: t = pop
  if compound: find first method whose conditions hold in workingWS
               push its subtasks; else rollback to last decomposition
  else primitive: if preconditions(workingWS) then plan += t; apply effects
                  else rollback
return plan (list of primitive tasks)   // no cost, no sorting
```

#### 1-4. GOAP

| 사실 | 출처 | 연도 |
|---|---|---|
| F.E.A.R.: FSM 은 Goto/Animate/UseSmartObject 3 상태(실질 2 상태). A* 로 액션 순서를 계획. STRIPS 와 4가지 차이: 액션별 비용, Add/Delete 리스트 제거(고정 크기 4바이트 값 배열의 월드 상태), 절차적 전제조건(`CheckProceduralPreconditions()`), 절차적 효과. 액션은 C++ 클래스, Goal Set 은 GDBEdit(게임 DB 편집기)에서 만들어 WorldEdit 에서 할당. 전역 조정자가 근접도로 분대를 주기적 재클러스터. | https://pages.cs.wisc.edu/~dyer/cs540/handouts/gdc2006_orkin_jeff_fear.pdf (로컬 추출) | 2006 |
| 해설(2020): F.E.A.R. 는 약 120개 액션, 재계획으로 동적 문제 해결. 단점: 에이전트끼리 서로를 몰라 협동은 우연적; 2014 연구에서 쥐(rat)의 잦은 재계획이 성능 오버헤드. 이후 GOAP 채택작: Condemned, S.T.A.L.K.E.R., Just Cause 2, Deus Ex HR, 툼 레이더(2013), 섀도 오브 모르도르/워 등. 산업은 킬존 2 등에서 HTN 으로 이동. | https://www.gamedeveloper.com/design/building-the-ai-of-f-e-a-r-with-goal-oriented-action-planning | 2020-05 |
| 크리스탈 다이내믹스(툼 레이더) GDC 2015: "First shipped title using it was Tomb Raider 2013", "Most AI development time is spent on code support for Goals and Actions, not the GOAP library code." 상황 비용(거리 20m 이동 = 20, 무기별 비용), 동기(motive) 로 목표 가용성 제어, 자식 액션 모니터링으로 조기 종료. | https://media.gdcvault.com/gdc2015/presentations/Conway_Chris_Goal-Oriented_Action_Planning.pdf (로컬 추출) | 2015 |
| 섀도 오브 모르도르: GOAP 사용은 2차 자료(위 2020 기사)에만 근거. 모노리스 포스트모템은 네메시스 시스템 중심이며 AI 아키텍처 세부는 없음. | https://www.gamedeveloper.com/audio/postmortem-monolith-productions-i-middle-earth-shadow-of-mordor-i- (검색 결과만, 본문 미열람) | 2014 |

#### 1-5. 계획기 실측 분석(GOAP vs HTN 공통)

| 사실 | 출처 | 연도 |
|---|---|---|
| F.E.A.R.(GOAP), 킬존 3(HTN), 트랜스포머 3(HTN) 세션 로그 분석. 로그된 액션 수 55/44/137. NPC 당 최대 계획 속도 8.5 / 3.4 / 59.9 plans/s. GOAP 계획은 1~2 액션, HTN 은 2~5 액션. "most of the AIP time and memory budget ... is not spent into fighting actions but into actions whose purpose is patrolling and animation". 결론: 반복 액션(길이 1)과 고정 계획에는 계획기 대안을 고려하라. | https://cdn.aaai.org/ojs/12728/12728-52-16245-1-2-20201228.pdf (AIIDE 2014, 로컬 추출) | 2014 |

#### 1-6. 스테이트 트리(언리얼 5.x)

| 사실 | 출처 | 연도 |
|---|---|---|
| 선택 동작 6종: Try Enter / Children in Order / Children at Random / **Highest Utility** / **Random Weighted by Utility** / Follow Transitions. Consideration 노드(Float 응답 곡선, Enum 룩업, 상수)와 Weight 로 점수. AND 는 낮은 점수, OR 는 높은 점수로 결합. | https://dev.epicgames.com/documentation/unreal-engine/state-tree-selectors-overview | 5.8 문서(2026) |
| 개요: "hierarchical state machine that combines the Selectors from behavior trees with States and Transitions". 상태 선택은 "on-demand, based on Transitions". Tick 표시된 조건은 매 프레임 평가. Blueprint 기반 클래스(`UStateTreeTaskBlueprintBase` 등) 중심 설명. | https://dev.epicgames.com/documentation/unreal-engine/overview-of-state-tree-in-unreal-engine | 5.8 문서(2026) |
| 5.6 변경: "Scheduled Tick Policy" 도입 후 기존 트리의 틱/전이가 멈추는 사례 보고(2025-06-07 최초, 07-02 원인 확인). Custom Tick Rate 미설정 시 틱 태스크가 자동 비활성. "I need to write structs for c++ state tree tasks not classes"(2025-10-04). | https://forums.unrealengine.com/t/statetree-changes-at-5-6-version/2545493 | 2025 |
| 에픽 튜토리얼 "Tickless StateTree Changes": 예약 틱 정책으로 틱 수 제한, 틱하지 않는 비동기 태스크 사용. 포럼 게시 2025-10-07. 본문은 제목만 수신되어 세부 옵션은 미확인. | https://forums.unrealengine.com/t/tutorial-tickless-statetree-changes/2663124 , https://dev.epicgames.com/community/learning/tutorials/z3km/unreal-engine-tickless-statetree-changes | 2025 |
| 커뮤니티 노트: "Blueprint based tasks seem to perform significantly worse than C++ based tasks", C++ 태스크는 `Link()` + `TStateTreeExternalDataHandle<T>` + `EnterState()`; 5.3 기준 컴포넌트로 실행 시 런타임 트리 교체 불가. | https://zomgmoz.tv/unreal/State-Tree/StateTree | 연도 미표기(5.3 언급) |
| Mass 결합: "Mass StateTree ... updating each entity's StateTree based on signals sent from other Mass systems", LOD 는 High/Medium/Low/Off, 시뮬레이션 LOD 는 부하 분산용. | https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-gameplay-in-unreal-engine | 5.8 문서(2026) |
| "Tick on StateTree Tasks are only ran once and with subscribed signals (see UMassStateTreeProcessor)"; 신호 구독 방법을 찾지 못해 하드코딩 신호 재사용. | https://github.com/Ji-Rath/MassAIExample/blob/main/README.md | 2022~2023 추정 |
| MassSample: MassLOD 는 "ticking at different rates based on fragment settings", 문서는 "still work in progress", 5.7 로 갱신. | https://github.com/Megafunk/MassSample | 2025~2026 |
| 1만 NPC 60fps 사례(RTX 4080, 군중 시뮬 4.6ms): 연속 배열 데이터, **사전 계산 흐름 경로(개별 BT 평가 제거)**, 4단 LOD, GPU 애니 인스턴싱(에이전트당 0.05~0.15ms → 0.001~0.005ms). 콘솔은 5,000 이상에서 고전. StateTree 언급 없음. | https://www.strayspark.studio/blog/crowd-traffic-simulation-ue5-mass-ai | 2026-03-25 |
| 언리얼 StateTree 실무 고통점 글(jeanpaulsoftware, 2024-08-13) — 서버 연결 거부로 **미확인**. | https://jeanpaulsoftware.com/2024/08/13/state-tree-hell/ | 2024 |

#### 1-7. FSM + 스크립트/데이터(탑다운 ARPG·로그라이크·대량 몬스터)

| 사실 | 출처 | 연도 |
|---|---|---|
| 하데스: `EnemyData[unitName]` Lua 테이블에 `ProjectileBlockPresentationFunctionName`, `AIEndHealthThreshold`, `DefaultAIData.LeapRetreatAtHealthPercent`, `RetreatLeapWhenHitChance` 등 행동 파라미터. `RoomManager.lua` 가 `Import "EnemyAI.lua"`. 인카운터는 `MinWaves/MaxWaves/MinEliteTypes` 같은 테이블 값으로 생성. | https://github.com/zhang-wen-guang/Hades/blob/main/Combat.lua , https://github.com/GarageOfRick/hades_mod/blob/master/RoomManager.lua , https://github.com/cgullsHadesMods/hades-EncounterBalancer/blob/main/EncounterBalancer.lua (커뮤니티 미러/모드) | 2020년대 |
| 디아블로 3: 모든 몬스터를 "행동 패턴" 으로 분류(Big Hit, Frenzied 등), 인카운터는 역할이 다른 몬스터 조합으로 설계. AI 기법은 언급 없음. | https://blizzplanet.substack.com/p/blizzard-previews-diablo-iii-monster-encounters-and-behavior | 2010-12-23 |
| 블리자드 Brian Schwab GDC 2010 "AI Architecture Mashups": 순수 아키텍처는 거의 쓰지 않고 여러 아키텍처를 "mashed together". | https://blizzplanet.substack.com/p/gdc_2010_blizzard_speakers_announced | 2010 |
| 디아블로 4 시즌 11: 쿨다운 패턴 대신 "pick actions from a set behavior pool, that will adjust itself based on the difficulty level", 공격 속도 기반·무작위 선택, 엘리트/챔피언은 더 복잡한 행동. | https://www.icy-veins.com/d4/news/why-diablo-4-is-redesigning-monsters-and-defenses-in-season-11/ | 날짜 미표기(시즌 11, 2025년 말 추정) |
| 뱀파이어 서바이버즈: Phaser → Unity 전환(2023-08-17) 이유는 "big performance improvements" 등. 적 AI 구조·ECS 사용 여부는 개발자 답변 없음(itch 댓글에 질문만 존재). | https://www.gamingonlinux.com/2023/07/vampire-survivors-switching-to-new-game-engine-on-august-17/ , https://zedtix.itch.io/vampire-survivors/comments | 2023 |
| 패스 오브 엑자일 몬스터 AI 내부 구현: 공식 자료를 찾지 못함. **미확인**. (POE2 에서 몬스터가 투사체를 피한다는 인터뷰 요약만 검색 결과에 존재, 본문 미열람) | https://www.mmorpg.com/interviews/interview-path-of-exile-2s-game-director-jonathan-rogers-chats-gameplay-intentions-inclusions-and-improvements-2000130901 | 2024-03 |

### 2) 결정론성 · 프레임당 비용 · 재계획 빈도 · 디버깅 · 정의 용이성 비교(2020~2026 자료 우선)

#### 2-1. 비교표(조사 근거 종합)

| 축 | BT | StateTree(UE) | FSM+데이터 | HTN | GOAP | 유틸리티 |
|---|---|---|---|---|---|---|
| 결정론 | 트리 순회는 결정적. 랜덤 셀렉터·병렬 노드가 있으면 시드 필요(BehaviorTree.CPP 는 비동기 액션이 1급 시민 → 스레드 결정론은 사용자 책임) | 선택은 결정적이나 "Random Weighted by Utility" 는 RNG. Mass 결합 시 신호 순서 의존 | 가장 단순. 전이 조건이 순수 함수면 결정적 | 결정적(탐색에 비용/정렬 없음, 첫 성공 메서드 채택). 재계획 트리거 3종 명시 | A* 로 결정적이나 동률 비용 tie-break 규칙 필요; 다중 스레드 계획기(Crashkonijn)는 보장 문서 없음(미확인) | 점수 = 순수 함수. "상위 N 중 무작위"(심즈) 쓰면 시드 필요 |
| 프레임당 비용 | 루트부터 재평가 시 O(노드). 헤일로 2 는 비트마스크로 서브트리 차단 | 전이는 on-demand, 조건 Tick 은 매 프레임. 5.6 예약 틱 정책으로 제한 가능 | 최소 | 킬존 2: 15 봇 + 6 터렛 + 분대가 CPU 예산 1/4, 격프레임 실행(2009). 계획 길이 2~5 | 계획 길이 1~2, 최대 8.5 plans/s/NPC(F.E.A.R.). 절차적 전제조건(경로 탐색)이 비쌈 | GW2: 수백 종·수십 행동을 조기 종료 순서로 처리(2015/2017). 비용은 고려사항 수 × 후보 수 |
| 재계획/재평가 빈도 | 매 틱 또는 이벤트 | 전이 트리거 기준 | 전이 조건 검사 시 | 킬존 2 개인 5Hz, 분대 2Hz, 진영 6s(2007). Fluid HTN: "Replan only when plans complete/fail or when world state change" | 계획 무효화·목표 변경 시. 쥐의 잦은 재계획이 오버헤드(2020 해설) | DA:I: 매 AI 업데이트 패스, 필요시 조절(2017). IAUS: Inertia Weight 로 최소 간격(2024) |
| 디버깅 | Groot2 시각화·로그·리플레이(BehaviorTree.CPP) | Rewind Debugger/에디터 의존 | 로그로 충분 | Decima: 인게임 분해 시각화(2024). Maisak: Visual Logger 통합 | 크리스탈 다이내믹스가 디버깅/리포팅 도구에 시간 투자(2015) | DA:I: 스니펫별 점수 표를 디버그 뷰로 노출(2017) |
| 데이터/코드 정의 | XML(BehaviorTree.CPP), Python 코드(py_trees), 에셋(Unity Behavior) | 에셋(에디터) 기본. C++ 태스크 가능 | Lua 테이블(하데스) | C# 빌더(Fluid HTN), Python 함수(GTPyhop), 생성 C++(Decima), 그래프 에셋(Maisak) | C++ 클래스 + DB 편집기(F.E.A.R.), C# 클래스(ReGoap), ScriptableObject/코드(Crashkonijn) | DB 도구(IAUS), 평가 트리 에셋(DA:I), C++ 클래스+곡선(Borealis) |

#### 2-2. 결정론 환경 요건(모델과 무관한 공통 조건)

| 사실 | 출처 | 연도 |
|---|---|---|
| 락스텝 사례: 고정 10Hz 시뮬 + 보간, 공유 시드 RNG, 시뮬 코드에서 부동소수점 제거, `MyFunction(rng.GetRand(), rng.GetRand())` 인자 평가 순서 버그, 정렬은 고유 ID 로 tie-break, 시뮬 코드를 `Sim` 디렉터리로 분리해 리뷰 대상 명시. | https://www.gamedeveloper.com/programming/minimizing-the-pain-of-lockstep-multiplayer | 2015-11-24 |
| 밸런싱 시뮬레이션 연구: 게임에 확률 요소가 있으면 "the simulation must be run multiple times to approximate the actual balance", 균형 지표로 Statistical Parity(승률 0.5 = 균형) 사용, 시뮬레이션은 계산 집약적. | Rupp, "Game Balancing via Procedural Content Generation and Simulations", AIIDE 2025 Doctoral Consortium, 21(1) 442–445. https://ojs.aaai.org/index.php/AIIDE/article/view/36856 (PDF 본문을 로컬 추출해 확인) | 2025 |

발췌(락스텝 글이 지적한 순서 버그 형태):
```cpp
// 인자 평가 순서가 컴파일러마다 달라 RNG 소비 순서가 갈린다
MyFunction(myRNG.GetRand(), myRNG.GetRand());
// 안전: 명시적 순차 호출
const int a = myRNG.GetRand(); const int b = myRNG.GetRand(); MyFunction(a, b);
```

### 3) "LLM 이 작성·수정하기 쉬운 형식" 관점의 형식 비교

| 라이브러리/형식 | 정의 방식 | 실행 모델 | LLM 친화도 판단 | 출처 | 연도 |
|---|---|---|---|---|---|
| BehaviorTree.CPP v4.9 | XML DSL, 런타임 로드, 노드는 C++ 플러그인. "asynchronous Actions ... a first-class citizen", 포트로 타입 안전 데이터플로, Groot2 에디터, 로그/리플레이 내장. MIT. | 수동 tick(사용자 루프) | 높음: 텍스트 XML 이라 diff·생성 쉬움. 단 비동기 액션은 결정론 주의 | https://github.com/BehaviorTree/BehaviorTree.CPP | 2026(README) |
| py_trees | Python 코드로 트리 구성 | tick 기반, "do not need to be real time reactive", 50~200ms 지연 허용, 수백 행동 규모 | 높음(코드) 이나 게임 런타임 부적합. 참고 모델 | https://py-trees.readthedocs.io/en/devel/introduction.html | 연도 미표기 |
| Unity Behavior 1.0 | 그래프 에셋(에디터). 1.0.0 2024-09-17. LLM 생성(Muse) 기능은 1.0.10(2025-05-21)에서 "As part of the Muse product sunset, the generative AI features have been disabled". 1.0.16 2026-05-26. | 컴포넌트 틱 | 낮음(에셋). LLM 생성 기능은 상용에서 철회됨 | https://docs.unity3d.com/Packages/com.unity.behavior@1.0/changelog/CHANGELOG.html , https://unity.com/blog/engine-platform/unity-muse-ai-capabilities-in-editor-plus-new-updates | 2024~2026 |
| Fluid HTN(C#) | 플루언트 빌더 코드(`DomainBuilder<Ctx>().Select().Condition().Action().Do().Effect().End()`), Humphreys 총순서 전방 분해 기반, 부분 계획(PausePlan), MTR(Method Traversal Record)로 재계획 우선순위, 도메인 슬롯. MIT. | 사용자가 Tick 호출 | 매우 높음: 코드가 곧 도메인, 구조가 선형 | https://github.com/ptrefall/fluid-hierarchical-task-network | 2020년대(활동 중) |
| GTPyhop(Python) | Python 함수가 메서드/액션. SHOP2·Pyhop 계열 총순서 GTN. BSD-3. | 오프라인 계획 | 높음(코드). 텍스트 도메인 예시로 유용 | https://github.com/dananau/GTPyhop | 2021-07-22 |
| Maks Maisak HTN(UE) | 그래프 에셋 + C++/BP 노드 | AIController 내 계획·실행, 최저 비용 우선순위 큐 | 중간: 에셋이 기본이라 LLM 은 노드 코드만 다룸 | https://github.com/maksmaisak/htn | 2024~2025 |
| ReGoap(C#) | 액션/목표 = C# 클래스(전제조건/효과/우선순위), A* 역방향 | Unity/Godot 어댑터 | 높음(코드) | https://github.com/luxkun/ReGoap | 연도 미표기 |
| Crashkonijn GOAP v3.1.2 | ScriptableObject 또는 코드 설정, Unity Job 다중 스레드 계획, 2,000+ 에이전트 데모 | 잡 시스템 | 중간(코드 가능). 결정론 보장 문서 없음(미확인) | https://github.com/crashkonijn/GOAP | 2020년대(활동 중) |
| Unity AI Planner | 0.2.4-preview.3(2020-11-12) 이후 갱신 없음, preview 상태 | — | 낮음(사실상 정지) | https://docs.unity3d.com/Packages/com.unity.ai.planner@0.2/changelog/CHANGELOG.html | 2020 |
| IAUS 계열(Curvature, Rescue, Borealis) | 고려사항 = 입력·응답 곡선·가중치 표. Borealis 는 C++ 클래스 + 에디터 곡선 | think cycle | 매우 높음: 표 한 줄 = 고려사항 하나 | https://github.com/ProjectBorealis/IAUS/wiki , https://github.com/apoch/curvature (검색 결과만) | 2024 |

발췌(Fluid HTN 도메인 정의 형태, README 요약):
```csharp
var domain = new DomainBuilder<MyContext>("MyDomain")
  .Select("C")
    .Condition("Has A and B", ctx => ctx.HasState(A) && ctx.HasState(B))
    .Action("Get C")
      .Do(ctx => { /* ... */ return TaskStatus.Success; })
      .Effect("Has C", EffectType.PlanAndExecute, (ctx, t) => ctx.SetState(C, true, t))
    .End()
  .End()
  .Build();
```

LLM 생성 연구(형식 선택의 근거):

| 사실 | 출처 | 연도 |
|---|---|---|
| Dendron: BT 를 LLM 에이전트의 구조화 프레임워크로 사용. LLM 은 "brittle in unexpected ways" 라 BT 스캐폴딩이 필요. | https://arxiv.org/abs/2404.07439 | 2024-04-11 |
| BTGenBot: 7B 이하 소형 모델을 600여 BT 데이터셋으로 미세조정해 BT 생성. 정적 구문 검사·검증기·시뮬레이터·실기 검증 파이프라인. | https://arxiv.org/abs/2403.12761 | 2024-03(개정 2025-01) |
| Real-Time World Crafting: 자연어 → **제한된 DSL** → 런타임 ECS 구성. 임의 코드 실행 위험을 DSL 로 차단, LLM 판정기로 평가, 복잡 DSL 에는 few-shot 필수. | https://arxiv.org/abs/2510.16952 | 2025-10 |
| GOBT: BT 안에 "planner node" 로 GOAP+유틸리티를 결합. 정량 성능 수치는 없음(예시 시나리오·로그 비교). | https://www.jmis.org/archive/view_article_pubreader?pid=jmis-10-4-321 | 2023-12-31 |

### 4) 최근(2024~2026) 하이브리드 추세

| 사실 | 출처 | 연도 |
|---|---|---|
| Iron Reclamation 개발 일지: 진영 단위 = 유틸리티 AI(확장/방어/자원), 유닛 단위 = BT(타깃/이동/전투). "no single model would handle the entire spectrum of decision-making cleanly". | https://lghts.itch.io/iron-reclamation/devlog/1081913/engineering-hybrid-intelligence-lessons-from-building-iron-reclamations-ai | 2025-10-10 |
| 언리얼 StateTree: 상태 선택기 안에 유틸리티(Highest Utility / Random Weighted) + Consideration 노드 → "ST 안의 유틸리티". | https://dev.epicgames.com/documentation/unreal-engine/state-tree-selectors-overview | 5.8 |
| Decima: HTN 상위 계획 + 유틸리티 결정(2017) + 생성 C++ 백트래킹(2024). | 위 1-3 절 | 2017/2024 |
| 드래곤 에이지 인퀴지션: 유틸리티 평가 트리(선택) + BT 실행 트리(실행). | 위 1-2 절 | 2017 |
| Project Borealis: BT 데코레이터/서비스 안에 IAUS 점수. | 위 3 절 | 2024 |
| 디아블로 4: 쿨다운 FSM → 난이도별 행동 풀 선택(가중 무작위 성격). | 위 1-7 절 | 2025 |
| GOBT(학술): BT + GOAP 계획 노드 + 유틸리티 선택. | 위 3 절 | 2023 |

추세 요약: **"상위 선택은 점수(유틸리티) 또는 분해(HTN), 하위 실행은 시퀀스(BT/FSM)"** 가 2015~2025 상용·연구 공통 구조다. GOAP 는 2013~2017 이후 새 채택 사례가 줄고, HTN 은 데시마·메트로 어웨이크닝(2024)으로 이어졌다.

---

## 프로젝트 적용 시사점 (TDGame 에서 무엇을 어떻게 쓰고 무엇을 피할지)

### 쓸 것

1. **2층 구조 고정**: `선택층(Brain)` 과 `실행층(Body)` 을 분리한다. 실행층은 F.E.A.R. 의 "Goto / Animate(+UseSmartObject)" 처럼 극소수 상태(이동, 능력 시전, 대기, 경직)만 가진 FSM 으로 두고, GAS(Gameplay Ability System) 능력 활성화가 곧 "Animate" 에 해당하게 한다. 근거: F.E.A.R.(2006) 3상태 FSM, DA:I 평가/실행 분리(2017).
2. **잡몹(다수)은 코드 정의 유틸리티**: 몬스터 종류별 `행동 후보 × 고려사항` 표를 C++ 상수 테이블(또는 텍스트 DataTable) 로 정의. 고려사항은 `[입력 정규화 → 응답 곡선 → 가중]` 곱셈, 0 이면 조기 종료, **비용 등급으로 정렬 강제**(Lewis 2017). 점수 동률은 고정 순서로 tie-break. 행동 교체 최소 간격(Inertia, 2024)과 "현재 계획 계속" 히스테리시스(킬존 2009)로 떨림 방지.
3. **엘리트·보스·무리 리더는 코드 HTN**: Fluid HTN 방식(총순서 전방 분해, 비용 없음, MTR 로 재계획 우선순위)을 C++ 빌더로 이식. 재계획 트리거를 Humphreys 의 3가지로 한정하고, 킬존식으로 개체·무리 주기를 다르게 둔다(예: 개체 5Hz, 무리 2Hz). 무리 블랙보드는 매 프레임이 아니라 주기 갱신(호라이즌 2019).
4. **사고 주기를 AI 모델 밖의 스케줄러 파라미터로**: 유틸리티 재평가·HTN 재계획 주기를 몬스터 등급·거리 LOD 별로 정한다. 근거: DA:I "as often as appropriate", Mass LOD "ticking at different rates".
5. **결정론 체크리스트**: 고정 스텝(이미 `FTDScopedCombatWorld` 가 고정 스텝 Tick), 시드 RNG 를 AI 컨텍스트에 주입, 후보 순회 순서를 몬스터 ID 로 고정, 정렬 tie-break, 부동소수점 누적을 쓰는 점수는 동일 연산 순서 보장, 다중 스레드 계획 금지(또는 결과를 결정적으로 병합). 근거: 락스텝(2015).
6. **LLM 용 제한 DSL**: 유틸리티 표와 HTN 빌더 호출만 LLM 이 만지는 표면으로 정의하고, 정적 검증기(테이블 스키마 검사, 도메인 빌드 성공, 헤드리스 시뮬 통과)를 붙인다. 근거: Real-Time World Crafting(2025), BTGenBot 검증 파이프라인(2024).
7. **밸런스 시뮬**: 확률 요소가 있는 한 같은 조건을 여러 시드로 반복해 분포를 얻는다(AIIDE 2025). 다만 "같은 시드 = 같은 결과" 를 자동화 테스트로 고정한다.

### 피할 것

1. **GOAP 신규 도입**: 계획 길이 1~2 액션에 비용 탐색·절차적 전제조건(경로 탐색) 비용이 붙고(2014 분석, 2006 원문), 개발 시간 대부분이 라이브러리가 아닌 Goal/Action 코드 지원에 든다(2015). HTN 이 같은 효과를 더 싸게 준다(2013).
2. **에디터 에셋 기반 BT/StateTree 를 잡몹 기본으로 삼기**: LLM 접근성이 낮고, 5.6 틱 정책 변경 같은 엔진 업데이트 리스크(2025), Blueprint 태스크 성능 저하(커뮤니티 노트). StateTree 는 영웅급 소수 액터의 "연출형" 상태 흐름에만 조건부 사용.
3. **모든 통신을 블랙보드로 강제**: Francis(2017)의 함정 #3. 유틸리티 고려사항은 게임 상태 구조체를 직접 읽게 하고, 블랙보드는 무리 공유 정보에만 쓴다.
4. **DSL 을 너무 일찍 만들기**: Francis(2017) 함정 #2. 1차는 C++ 상수 테이블·빌더 함수로 시작하고, 텍스트 DSL 은 LLM 워크플로가 실제로 병목이 된 뒤 도입.
5. **다중 스레드 계획기의 결정론을 가정**: Crashkonijn 등은 보장 문서가 없다. 헤드리스 시뮬은 단일 스레드 결정 경로로 고정.
6. **초대량(수천) 단계에서 AI 모델로 성능을 풀려는 시도**: 1만 NPC 사례(2026)는 흐름장·LOD·데이터 배치로 해결했고 개별 트리 평가를 제거했다. TDGame 에서도 원거리·화면 밖 몬스터는 "흐름장 추종 + 유틸리티 재평가 정지" 로 강등.

---

## 미확인·미해결 질문

1. **jeanpaulsoftware "State Tree Hell"(2024-08-13)**: 서버 연결 거부로 본문 미확인. StateTree 실무 디버깅·바인딩 문제의 1차 증언으로 재시도 가치 있음.
2. **에픽 "Tickless StateTree Changes" 튜토리얼 본문**: 제목만 수신. Scheduled Tick Policy 의 정확한 옵션(Default/Allowed/Custom Tick Rate 이외)과 도입 버전(5.6 추정) 은 엔진 소스 확인이 필요.
3. **패스 오브 엑자일 몬스터 AI 구현**: 공식 자료 없음. 커뮤니티 데이터마이닝(poedb) 에 "AI behavior" 항목이 있다는 검색 요약만 존재.
4. **뱀파이어 서바이버즈 적 AI 구조/ECS 사용 여부**: 개발자 답변 없음.
5. **킬존 2 "초당 500 계획"** 수치: 검색 요약에만 등장, 열람한 1차 자료(2007 논문, 2009 노트)에는 없음. 인용 금지.
6. **섀도 오브 모르도르 GOAP 사용**: 2차 기사(2020)만 근거. 모노리스 1차 자료 미확인.
7. **다중 스레드 GOAP(Crashkonijn)의 결정론 보장**: 문서 없음.
8. **Guerrilla Decima HTN 의 도메인 저작 형식**(텍스트/에디터/생성 C++ 의 입력): 2024 강연 페이지에 없음. 슬라이드 PDF 확인 필요.
9. **디아블로 4 시즌 11 기사 날짜**: 본문에 날짜 미표기. 시즌 11 시점(2025년 말)으로 추정.
10. **호라이즌의 유틸리티 결정이 HTN 의 어느 층(메서드 선택 vs 목표 선택)에 결합되는지**: 2017 초록에 없음.
