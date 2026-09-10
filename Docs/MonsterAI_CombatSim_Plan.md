# TDGame 몬스터 AI · 결정론 전투 시뮬레이션 계획 (인덱스)

- 작성일: 2026-09-10. 상태: **문서 완료, 구현 미착수**.
- 이 묶음은 "어떤 AI 모델인가 → 정의 형식과 아키텍처 → 틱과 규모 → 결정론 시뮬레이터 → 머신러닝·생성형 AI → 요구 밖 고려사항 → 로드맵·할 일" 순서로 읽는다. 결정 기록(00)과 문서 7편은 `Docs/MonsterAI_CombatSim/`, 조사 17편은 `Docs/MonsterAI_CombatSim/research/` 에 있다.
- 원칙: 로직은 C++ 전용(AGENTS.md 7절), `TD` 접두어. 정본은 텍스트(JSON)이고 에셋은 파생물이다. UKGame(`Docs/UKGame/`)은 과거 설계 참고용이며 기준이 아니다.
- 결정 기록(D1~D38)의 전문은 [00 결정 기록](MonsterAI_CombatSim/00-decision-record.md)이다(저장소에 반영됨(2026-09-10)). 각 문서의 "결론 요약(결정 문장)" 절과 이 문서 §2 는 결정 번호별 요약이며, 표현이 다르면 00 이 우선한다.
- 병행 작업 [월드·던전·PCG 계획](WorldDungeonPCG_Plan.md)과는 대장이 다르다(§5).

## 1. 이 묶음이 답하는 사용자 질문 6가지

| # | 사용자 질문 | 한 줄 답 | 담당 문서 |
|---|---|---|---|
| 1 | 엔진 내장 AI(비헤이비어 트리·StateTree)는 틱 수동 제어가 어렵고 노드 에셋이라 생성형 AI 가 못 다루는데, 계획형(HTN·GOAP)과 반응형 중 무엇을 쓰나 | 코드 정의 유틸리티 AI + 실행 FSM(Finite State Machine, 유한 상태 기계) 5상태. 배제의 결정적 근거는 틱이 아니라 "엔진 제공 텍스트 정의 경로 부재"다. GOAP 는 확률형이 아니라 비용 탐색 계획형이다 | [01 AI 모델 결정](MonsterAI_CombatSim/01-ai-model-decision.md) |
| 2 | 생성형 AI 가 몬스터를 만들고 검증하기 좋은 정의 형식과 코드 구조는 무엇인가 | 종 하나 = `Content/MonsterAI/Definitions/<Id>.json` 한 파일(평면 표 4개 + 메타). 원시·입력 함수는 C++ 등록표, 검증 3단, 스키마 문서는 코드에서 생성, 종당 컴파일 0회 | [02 아키텍처와 정의 형식](MonsterAI_CombatSim/02-architecture-and-definition-format.md) |
| 3 | 한 화면에 많은 몬스터를 어떤 주기로 어떻게 돌리나 | 서브시스템 SoA(Structure of Arrays, 배열 구조체) 슬롯이 정본, 단일 틱 함수가 4채널 × LOD 4단 주기표로 처리. 규모 단계 A(≤300 액터) → B → C → D 는 실측 KPI(Key Performance Indicator, 핵심 지표)로만 전환 | [03 틱과 규모](MonsterAI_CombatSim/03-tick-and-scale.md) |
| 4 | 장비·물약·버프 × 몬스터 종류·마릿수를 결정론적으로 시뮬레이션하는 밸런스 툴을 어떻게 만드나 | 1/64초 고정 스텝 헤드리스 커맨드렛 + 자동화 테스트가 세션 클래스를 공유. 난수는 시드 파생 스트림만, 순서는 SimulationId, 스텝별 해시 체인 게이트, 시뮬-실기 일치는 A/B/C 3단 | [04 결정론 전투 시뮬레이터](MonsterAI_CombatSim/04-combat-simulator.md) |
| 5 | 머신러닝·생성형 AI 를 어디까지 넣나 | 두뇌는 신경망화하지 않는다. ML 은 플레이어 대리 봇(규칙 → BC(Behavior Cloning, 행동 복제) → PPO 상한)과 JSON `tune` 잎 CMA/PSO 튜닝 두 지점뿐. LLM 루프는 스키마 덤프 → JSON → 검증 → 시드 배치 → 30줄 요약 | [05 머신러닝과 생성형 AI](MonsterAI_CombatSim/05-ml-and-generative-ai.md) |
| 6 | 이것 외에 내가 생각하지 못한 것은 무엇인가 | 콘텐츠 모델(난이도·웨이브·넉백·무리)·텔레메트리·기획자용 시각화·벤치 하네스·세이브 직렬화 다섯 묶음이 비어 있다. 우선순위 15개와 스키마 조각을 제시 | [06 요구사항 밖 고려사항](MonsterAI_CombatSim/06-beyond-the-ask.md) |
| — | 무엇부터 하나 | Phase 0 기반(1주) → 1 두뇌·시뮬 최소판 → 2 밸런스 툴 → 3 게임 몸·규모 → 4 조건부 확장. 할 일 50개, 사용자 결정 MD-01~MD-10 | [07 로드맵과 할 일 대장](MonsterAI_CombatSim/07-roadmap-and-tasks.md) |

## 2. 핵심 결정 12문장(결정 기록 D1~D38 압축)

전문은 [00 결정 기록](MonsterAI_CombatSim/00-decision-record.md)에 있다. 아래 12문장은 그 압축이며, 표현이 다르면 00 이 우선한다.

1. 주력은 코드 정의 유틸리티 AI(행동 × 고려사항 표, 4파라미터 곡선 7종, 곱 결합, 비용순 조기 종료, 최고점, 동률 정의 순서, 관성) + 실행 FSM 5상태이고, 엔진 BT·StateTree·GOAP·Mass 두뇌·Mover·MLAdapter 는 전투 코어에서 배제한다(D1·D4·D5) — web-ai-architecture-comparison, engine-behaviortree-tick, engine-statetree-runtime.
2. 보조는 데이터 정의 시퀀스(콤보)와 보스 페이즈 표이며, 자체 C++ HTN(Hierarchical Task Network, 계층적 태스크 네트워크)은 3페이즈 보스 안무·무리 역할 요구가 실제로 올 때 Phase 4 에 추가하고 엔진 HTNPlanner 플러그인은 쓰지 않는다(D2·D3) — engine-htnplanner-plugin.
3. 정본은 종당 JSON 한 파일(평면 표 4개 + 메타, 미지 키 거부, `extends` 상속)이고, 행동 원시·입력 함수·FSM 상태는 C++ 정적 등록표에 두며 입력 함수는 스텝 시작 스냅샷 `FTDBrainInputs` 만 읽는다(D6·D7) — web-llm-authorable-tooling.
4. 검증기 3단(스키마·참조 → 정적 규칙 → 5초 헤드리스 생존), 스키마 문서 코드 생성, JSON → C++ 상수표 되돌림 커맨드렛, float 비트를 포함한 정의 해시 dh, 시뮬 세션 중 핫리로드 잠금, 플레이어 대리 페르소나도 같은 JSON 형식이다(D8~D12) — engine-misc-decision-tools(R13 `FJsonObjectConverter`).
5. 새 모듈 없이 `Source/TDGame/MonsterAI/`·`CombatSim/` 두 폴더만 추가하고 커맨드렛도 런타임 모듈에 두며, 몬스터 상태의 정본은 `UTDMonsterThinkSubsystem` 의 SoA 슬롯 배열이고 단일 고우선 `TG_PrePhysics` 틱 함수가 `World->Tick` 당 1회 SimulationId 오름차순으로 "공간 해시 → 사고 → 이동 → 판정 → 피해" 를 처리한다(D13~D15) — engine-determinism-headless, project-current-combat-code.
6. 몸은 `ITDMonsterBody` 3구현(잡몹 `APawn`+`UFloatingPawnMovement`·컨트롤러 없음, 정예 `ATDMonsterCharacter`, 시뮬 `ATDSimCombatant`)으로 격리하고, 근접 탐색은 `THierarchicalHashGrid2D`(셀 250cm) 이며 잡몹은 물리 오버랩·RVO·DetourCrowd 를 쓰지 않고, GAS(Gameplay Ability System, 게임플레이 어빌리티 시스템)는 유지한다(D16~D18) — engine-movement-anim-scale, engine-gas-determinism.
7. 몬스터 공격 판정은 게임·시뮬 모두 데이터가 권위다(Phase 0~2 `UTDDamageDefinition`, Phase 3 `FTDAttackTimetable`, 애님 노티파이는 표현·오라클). 플레이어의 노티파이 스윕 경로는 유지하고, 화면 밖에서도 이동·판정은 멈추지 않는다(D19~D21) — project-current-combat-code, engine-determinism-headless.
8. 사고 주기는 4채널(think/move/judge/present) × LOD 4단 주기표 `uint8 PeriodTable[4][4]` 데이터이고 위상은 `Slot % Period` 로 난수 없이 분산하며, 시뮬 기본은 전원 L0, 규모 단계 A→B→C→D 는 실측 KPI 로만 넘어가고 게임·시뮬 모두 단일 스레드로 시작한다(D22~D25) — web-mass-monster-performance, engine-mass-entity-ai, engine-movement-anim-scale.
9. 시뮬 세션 클래스 `FTDCombatSimSession` 하나를 커맨드렛 `-run=TDCombatSim` 과 자동화 테스트 `TDGame.CombatSim.*` 이 공유하고, 스텝은 1/64초 고정(`++GFrameCounter` → `FApp` 시간 → `World->Tick`)이며 기존 28개 테스트는 0.02 스텝을 유지한 채 Phase 0 에서 실제 통과부터 확인한다(D26~D28) — engine-determinism-headless, web-balance-simulation-tools.
10. 난수는 마스터 시드에서 `HashCombine` 파생한 이름 있는 `FRandomStream` 만 쓰고 전역 `FMath::FRand` 계열은 grep 테스트로 금지하며, 순서는 SimulationId 하나로 통일하고, 스텝별 FNV-1a 64비트 해시 체인을 같은 프로세스 2회 + 다른 프로세스 1회 + 골든으로 게이트한다(D29~D31) — engine-determinism-headless, engine-gas-determinism, engine-behaviortree-tick(EQS·퍼셉션 벽시계).
11. "게임 = 시뮬 비트 동일" 은 약속하지 않고 A 비트 동일 / B 이벤트 등가(±1 스텝) / C 통계 등가 3단으로 정의하며, 입력은 시나리오 JSON, 출력은 결과 CSV + 결정 로그 JSONL(JSON Lines, 한 줄 한 레코드), 병렬은 프로세스 팬아웃이다(D32~D35) — web-balance-simulation-tools, engine-misc-decision-tools(R11 비주얼 로거).
12. ML 은 플레이어 대리 봇 3단(규칙 페르소나 → BC → PPO 상한)과 JSON `tune` 잎 CMA/PSO 두 지점뿐이고 추론·시뮬은 결정적이어야 하며, LLM 제작 루프는 종당 컴파일 0회, Phase 0 은 기존 코드 최소 변경 집합(D38)으로 시작한다(D36~D38) — engine-learning-agents-ml, web-ml-generative-npc, web-ue-5-6-to-5-8-ai-changes.

## 3. 문서 지도

| 순서 | 파일 | 제목 | 내용 | 읽는 시점 |
|---|---|---|---|---|
| 0 | [00-decision-record.md](MonsterAI_CombatSim/00-decision-record.md) | 결정 기록(D1~D38, 구속력) | 심사 합의(골격 A·결정론 C·규모 B·최소 변경 D), D1~D38 전문 11절(AI 모델, 정의 형식, 아키텍처·상태 소유, 공격 판정, 틱·규모, 시뮬레이터, ML·생성형 AI, Phase 0 최소 변경, 로드맵 요약, UKGame 차이, 정오표). 01~07 과 표현이 다르면 이 문서가 우선 | 모든 작업 전 |
| 1 | [01-ai-model-decision.md](MonsterAI_CombatSim/01-ai-model-decision.md) | 몬스터 AI 모델 비교와 결정 | 두 우려의 엔진 소스 검증, 계획형/반응형 보정, 후보 8개 비교표, StateTree 재검토 S1~S7, HTN 도입 H1~H5 | AI 모델 정할 때 |
| 2 | [02-architecture-and-definition-format.md](MonsterAI_CombatSim/02-architecture-and-definition-format.md) | 아키텍처와 몬스터 AI 정의 형식 | 폴더·클래스 표, 데이터 흐름, 게임/시뮬 공유 경계, JSON 스키마 규칙·예시 3종, 등록표, 점수기 코드, 검증·덤프·해시·핫리로드, 페르소나, LLM 절차 | 코드 짤 때 |
| 3 | [03-tick-and-scale.md](MonsterAI_CombatSim/03-tick-and-scale.md) | 틱 제어와 대량 몬스터 규모 전략 | 채널 × LOD 주기표, 위상·누적기·히스테리시스, 스케줄러, 몸 3구현·이동·공간 해시, 단계 A~D KPI, 예산표(추정), Mass 이관 모양, 단일 스레드 | 코드 짤 때 |
| 4 | [04-combat-simulator.md](MonsterAI_CombatSim/04-combat-simulator.md) | 결정론 전투 시뮬레이터(밸런스 툴) | 실행 형태, 고정 스텝 루프, 난수 소유권, 순서 결정성, 물리·내비·애니 배제, 판정 단일 경로, 해시·골든, 일치 3단, 입출력 스키마, 로그·스크립트, 팬아웃 | 시뮬 돌릴 때 |
| 5 | [05-ml-and-generative-ai.md](MonsterAI_CombatSim/05-ml-and-generative-ai.md) | 머신러닝과 생성형 AI 통합 | 두뇌 비신경망화 이유, Learning Agents 5.8 구조, 대리 봇 3단·관측 스키마·추론 결정론, tune 잎 CMA/PSO, LLM 루프, 플러그인·Build.cs | 시뮬 돌릴 때(Phase 4 전 참고) |
| 6 | [06-beyond-the-ask.md](MonsterAI_CombatSim/06-beyond-the-ask.md) | 요구사항 밖의 추가 고려사항 | 우선순위 15개, 비평 누락 M1~M20·B1~B14 반영 상태, 모순 C1~C13 판정, 운영 규칙(골든 소유·문서=코드·저장·텔레메트리·시각화·벤치·업그레이드), 콘텐츠 모델 스키마 | 할 일 고를 때(제안 항목 확인) |
| 7 | [07-roadmap-and-tasks.md](MonsterAI_CombatSim/07-roadmap-and-tasks.md) | 로드맵과 할 일 대장 | Phase 0~4 표, 기존 코드 변경 15건(D38), 할 일 50개(M0~M4), 사용자 결정 MD-01~MD-10, 위험표 12, 에이전트 절차 | 할 일 고를 때 |

## 4. 조사 자료(`MonsterAI_CombatSim/research/`, 16편 + 비평 1편)

| 파일 | 한 줄 요약 | 핵심 결론 번호 |
|---|---|---|
| [engine-behaviortree-tick.md](MonsterAI_CombatSim/research/engine-behaviortree-tick.md) | BT 는 수동 한 스텝 진행은 되지만 간격 외부 제어는 덮어써지고, 텍스트 직렬화기가 없으며 마리당 UObject 3~4개가 남는다. EQS·시각 퍼셉션은 벽시계를 쓴다 | 1·2·4·5·6·7·8·11 |
| [engine-statetree-runtime.md](MonsterAI_CombatSim/research/engine-statetree-runtime.md) | StateTree 는 컨텍스트 직접 구동·시드 지정·분리 틱이 가능하나, 조립·컴파일이 UncookedOnly 에디터 모듈이라 패키지 빌드에서 텍스트 → 트리가 불가하다 | 1·2·3·4·5·6·8 |
| [engine-htnplanner-plugin.md](MonsterAI_CombatSim/research/engine-htnplanner-plugin.md) | 엔진 HTNPlanner 는 실행기 부재·백트래킹 결함·UE4 잔재의 방치 프로토타입이다. 코어는 결정적이라 설계 골격만 차용한다 | 1·2·3·5·6 |
| [engine-determinism-headless.md](MonsterAI_CombatSim/research/engine-determinism-headless.md) | `UWorld::Tick` 직접 호출 고정 스텝은 유효하되 매 스텝 `GFrameCounter` 증가가 필수. 전역 난수는 스레드별, 오버랩 결과는 비정렬, 커맨드렛 + 직접 Tick 이 가장 통제하기 쉽다 | 1·2·3·4·6·8·9·10 |
| [engine-gas-determinism.md](MonsterAI_CombatSim/research/engine-gas-determinism.md) | GE 지속시간은 타이머 구동이고 만료는 "누적 시간이 만료 시각을 엄격히 초과한 첫 스텝". GAS 내부 난수는 한 곳, 다수 ASC 비용은 틱이 아니라 메모리·타이머 개수 | 1·2·3·4·7·9·10 |
| [engine-mass-entity-ai.md](MonsterAI_CombatSim/research/engine-mass-entity-ai.md) | Mass 는 프로세서 순서는 결정적이나 엔티티 압축이 벽시계 예산이고 병렬이 기본이다. 프로세서 직접 실행 유틸과 시뮬 LOD·표현 LOD 가 이미 있다 | 2·3·4·5·6·8·9 |
| [engine-movement-anim-scale.md](MonsterAI_CombatSim/research/engine-movement-anim-scale.md) | CMC 기본값은 매 프레임 스윕·트레이스, RVO 는 O(N²). `APawn`+`UFloatingPawnMovement` 로 바꿔도 길찾기는 유지되고, 대량 객체 정석은 "액터 틱 끄고 한 틱 함수가 배열 순회" | 1·2·3·4·7·9·10·11 |
| [engine-learning-agents-ml.md](MonsterAI_CombatSim/research/engine-learning-agents-ml.md) | Learning Agents 는 게임 안 경험 수집 + 외부 파이썬 학습이고, 관측·행동 스키마는 C++ 로 정의 가능, 추론은 NNERuntimeBasicCpu 로 결정적이며 MLAdapter 는 정체 상태 | 1·2·3·4·5·7·8 |
| [engine-misc-decision-tools.md](MonsterAI_CombatSim/research/engine-misc-decision-tools.md) | Chooser·SmartObjects·StateGraph·AIAssistant·MLflow·PlainProps 는 전투 코어에 부적합. 쓸 것은 `FJsonObjectConverter`(FInstancedStruct 왕복)와 비주얼 로거 2차 송출 | R3·R6·R7·R8·R9·R11·R13 |
| [project-current-combat-code.md](MonsterAI_CombatSim/research/project-current-combat-code.md) | 현재 전투 코드는 고정 스텝이면 대체로 재현되나 전역 난수 2곳·`TSet` 순회 순서·`GetUniqueID` tie-break 가 위험. 근접 판정은 메시·몽타주 의존, 픽스처는 내비·AI 시스템 없음 | 1·3·4·5·6·8·10 |
| [web-ai-architecture-comparison.md](MonsterAI_CombatSim/research/web-ai-architecture-comparison.md) | 상용 AI 는 하이브리드가 표준(선택은 유틸리티/HTN, 실행은 FSM/BT). GOAP 계획 길이 1~2, HTN 2~5, 재계획 주기는 명시적으로 자른다. 결정론은 모델보다 실행 환경이 좌우 | 1·2·3·4·5·6·7·10 |
| [web-balance-simulation-tools.md](MonsterAI_CombatSim/research/web-balance-simulation-tools.md) | 밸런스 툴은 계산기형과 몬테카를로형, 결정론 검증은 틱별 해시 + 리플레이 재실행, 병렬은 프로세스 단위, 반복 수는 승률 표준오차로, 대리 봇은 규칙 → 모방 → 강화 순 | 1·2·4·5·6·8·9·10 |
| [web-llm-authorable-tooling.md](MonsterAI_CombatSim/research/web-llm-authorable-tooling.md) | 5.8 공식 MCP(Model Context Protocol, 모델 컨텍스트 프로토콜) 툴셋은 읽기 전용, AIAssistant 는 만들지 못한다. 유틸리티 표준 형식은 "입력 + 곡선 + 파라미터 4 + 가중치" 이고 제한 JSON DSL 에서 LLM 유효율이 높다 | 1·2·3·5·6·7·8·9 |
| [web-mass-monster-performance.md](MonsterAI_CombatSim/research/web-mass-monster-performance.md) | 풀 액터 몬스터 0.13~0.41ms/마리(타사 실측), 애니메이션이 진짜 병목, Epic 은 BT·GAS 의 Mass 이식 가능성을 낮게 봄. 단계별 선택 기준 ≤300 액터 → Mass | 1·2·3·4·6·8·10 |
| [web-ml-generative-npc.md](MonsterAI_CombatSim/research/web-ml-generative-npc.md) | 업계는 RL 을 적 두뇌보다 대리 봇·QA·밸런스에 먼저 쓴다. 추론 결정론 3조건(시드·노이즈 0·같은 빌드/명령셋), 생성형 AI 에 좋은 형식은 제한 텍스트 DSL, SLM NPC 는 소수 캐릭터용 | 1·3·4·5·6·7·8 |
| [web-ue-5-6-to-5-8-ai-changes.md](MonsterAI_CombatSim/research/web-ue-5-6-to-5-8-ai-changes.md) | StateTree 만 정식이고 Mass 는 런타임 모듈 승격, Learning Agents·Mover·MCP 는 Experimental. 결정론 신기능 없음, 5.8 이 마지막 UE5 메이저(UE6 2027년 말 예고), StateTree 회귀 이력 | 1·2·3·4·5·6·7·10 |
| [zz-completeness-critique.md](MonsterAI_CombatSim/research/zz-completeness-critique.md) | 조사 16편이 놓친 주제 20개(M1~M20), 조사 간 모순 13건(C1~C13) 판정, 요구사항 밖 고려사항 14개(B1~B14). 06 문서의 입력 | M1~M20, C1~C13, B1~B14 |

## 5. 에이전트 작업 절차

상세 규칙은 [07 §6](MonsterAI_CombatSim/07-roadmap-and-tasks.md) 이 정본이다. 요약:

| 단계 | 규칙 |
|---|---|
| 읽기 | `AGENTS.md`(7절·11절), 이 인덱스, 07 대장, 02 §2 클래스 표를 먼저 읽는다. 클래스·파일·명령 이름은 §6 용어집과 02 §2 표기를 그대로 쓴다 |
| 고르기 | 할 일은 07 대장에서만 고른다. 상태 `todo` 이고 선행 항목이 모두 `done` 인 것 중 우선순위(높음 > 중간 > 낮음)가 가장 높은 항목 |
| 상태 갱신 | 고르면 `doing` + 날짜·세션 기록. 완료 조건 전부 만족 시에만 `done` 으로 바꾸고 검증 근거(테스트 로그 경로, 해시 값, ms 수치, 승률 ± 표준오차)를 기록 칸에 남긴다. 로그 파일이 없으면 리뷰에서 `doing` 으로 되돌린다. 결정이 필요하면 `decision` 으로 바꾸고 07 §4 에 질문·선택지·권장안을 적는다 |
| 검증 명령 1 빌드 | `"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" TDGameEditor Win64 Development -Project="C:\Project\TDGame\TDGame.uproject" -WaitMutex -NoHotReload` |
| 검증 명령 2 자동화 테스트 | `UnrealEditor-Cmd.exe TDGame.uproject -ExecCmds="Automation RunTests <필터>" -TestExit="Automation Test Queue Empty" -unattended -NullRHI -NoSplash -NoSound -log=<이름>.log` — 필터는 `TDGame.Combat`, `TDGame.MonsterAI.*`, `TDGame.CombatSim.*` |
| 검증 명령 3 커맨드렛 | 시뮬 `-run=TDCombatSim -Scenario=<json> -SeedStart=N -SeedCount=M -Out=<dir> -nullrhi -unattended -nosplash -nosound -FixedSeed -onethread -abslog=<file>`(골든 생성은 `-WriteGolden`), 정의 검증 `-run=TDMonsterAIValidate [-only=<Id>] [-print-resolved] -nullrhi -unattended`(종료 코드 ≠ 0 이면 실패) |
| 실측 기록 | PIE(Play In Editor, 에디터 내 실행) 캡처·벤치 CSV 는 `Docs/MonsterAI_CombatSim/measurements/` 에 남긴다(MD-05 결정 전까지 `Docs/Validation/` 은 쓰지 않는다) |
| 골든 해시 | 에이전트는 `-WriteGolden` 으로 후보만 생성하고, 기록 칸에 "왜 바뀌었는가"(엔진 버전·dh·코드 커밋)를 먼저 적는다. 승인은 사람(06 §4-1) |
| 범위 | 새 할 일은 해당 Phase 절 끝에 `todo` 로 추가. 월드젠 소유 파일·다른 문서 묶음은 고치지 않고 항목으로만 남긴다. 사용자 결정 없이 에셋 삭제·덮어쓰기·설정 변경 금지 |
| 커밋 | 사용자가 지시할 때만 한다 |

병행 월드젠 대장(`Docs/Tasks/`)과의 관계: **별도 대장**이다. 할 일 ID 접두사는 이 묶음이 `M`(`M<phase>-<번호>`), 월드젠이 `P` 로 구분한다. 통합 시점은 사용자 결정(MD-08, M3-10)이며 그 전까지 `Docs/Tasks/` 파일을 수정하지 않는다.

## 6. 정본 용어집

이름이 문서마다 달랐던 항목은 여기 표기로 통일한다(괄호 안이 통일 전 표기). 축약어: SoA = Structure of Arrays(배열 구조체), POD = Plain Old Data(단순 자료형), GE = Gameplay Effect(게임플레이 효과), ASC = Ability System Component(어빌리티 시스템 컴포넌트), CMC = Character Movement Component(캐릭터 이동 컴포넌트), LOD = Level of Detail(세부 수준), FSM = 유한 상태 기계, HTN = 계층적 태스크 네트워크, BC = 행동 복제, PPO = Proximal Policy Optimization(근접 정책 최적화), CMA/PSO = 공분산 행렬 적응 / 입자 군집 최적화, JSONL = JSON Lines.

### 6.1 아키텍처·클래스

| 용어 | 뜻 | 정본 |
|---|---|---|
| `UTDMonsterThinkSubsystem` | UWorldSubsystem. SoA 슬롯 배열 소유. `TG_PrePhysics` 고우선 틱 함수 1개가 `World->Tick` 당 정확히 1회 SimulationId 오름차순으로 공간 해시 → 사고 → 이동 → 판정 → `ExecuteRules` → 로그·해시 처리 | 02 §2·§3 (D14·D15) |
| `FTDMonsterBrainSlot` | 서브시스템 소유 SoA 슬롯 컨테이너(SimulationId·세대·정의 인덱스·위치·속도·FSM·행동·관성·쿨다운·시퀀스·페이즈·LOD). 내부는 프래그먼트 배열 6종. (03 `FTDMonsterSlots`, 07 `TDMonsterSlots.h` 는 이 이름으로) | 02 §2, 03 §8.2 |
| `FTDBrainInputs` | 스텝 시작 시 채우는 is_trivially_copyable POD 스냅샷. 입력 함수의 유일한 인자, UWorld·AActor 접근 불가 | 02 §5.5 (D7) |
| `FTDUtilityScorer` | 정적 순수 함수 점수기. weight × Π고려사항, 비용순 조기 종료, 최고점, 동률은 actions 배열 순서, 관성, `weighted_random` 만 AI 스트림 소비 | 02 §5.6 (D1·D2) |
| `FTDResponseCurve` | 곡선 7종(Constant/Binary/Linear/Quadratic/Logistic/Logit/Gaussian) × 파라미터(m,k,b,c) 기본(1,1,0,0), 단위 정사각형 0~1 클램프. 정확한 식은 `FTDResponseCurve::Evaluate` 가 정본 | 02 §5.1.1 |
| `FTDMonsterDefinition` | 한 종의 불변 정의 USTRUCT(`FTDUtilityAction`·`FTDUtilityConsideration`·`FTDActionSequence`·`FTDMonsterPhase` 포함). JSON 과 1:1, 파서는 `FJsonObjectConverter` | 02 §2·§5.1 |
| `UTDMonsterDefinitionLibrary` | UEngineSubsystem 로더: 파싱 → `extends` 병합(깊이 ≤ 3) → 등록표 해석 → 비용순 정렬 → 정의 해시 dh → 핫리로드·`LockDefinitions`. (07 `TDMonsterDefinitionLoader` 는 이 이름으로) | 02 §2·§5.9 |
| `FTDMonsterActionRegistry` | 행동 원시 정적 등록표: 이름 → FSM 진입 상태·인자 스키마(`FTDActionArgSchema`)·`bSimulatable`·실행 함수 | 02 §2·§5.5 |
| `FTDBrainInputRegistry` | 입력 함수 정적 등록표: 이름 → 캡처 없는 함수 포인터 `FTDBrainInputFunction`·비용 등급(Cheap < Grid < Trace)·기본 range | 02 §2·§5.5 |
| 행동 원시 7개 | MoveToward, MoveAway, MoveToBand, FaceTarget, Wait, CastAbility, PlaySequence(시뮬 가능). 표현 전용 PlayEmote 는 `bSimulatable=false`. (07 '6개'·'Cast' 는 이에 맞춤) | 02 규칙 4·§5.5 |
| 등록 입력 함수 | DistanceToTarget, SelfHealthRatio, TargetHealthRatio, FacingTarget, AbilityReady, AllyCountNearby, LineOfSightToTarget, TargetIsAttacking, RingSlotFree, IncomingAttackTelegraph, PersonaDodgeRoll, PotionCount(PascalCase). 06 제안 HasAttackToken | 02 §5.2~§6·§8 |
| `FTDMonsterActionExecutor` | 실행 FSM 5상태 Idle/Move/Cast/Sequence/Stagger, 몸에 명령. 07 M1-04 파일 `TDMonsterExecutionState.h` 가 담는다 | 02 §2 (D1) |
| `ITDMonsterBody` | 몸 UInterface(위치 읽기·이동 요청·시전·페이싱·경직), 구현 3종 | 02 §2, 03 §4.1 (D16) |
| `ATDMonsterPawn` | 게임 잡몹 몸: `APawn` + `UFloatingPawnMovement`, 캡슐 QueryOnly, 오버랩 이벤트 끔, AIController 없음, 액터 틱 없음 | 02 §2, 03 §4.1 |
| `ATDMonsterCharacter` | 기존 `ACharacter`, 게임 정예/보스 몸: CMC `MOVE_NavWalking`, `bAlwaysCheckFloor=false`, `bEnablePhysicsInteraction=false`, RVO 끔, `bRunPhysicsWithNoController=true`, AutoPossessAI/AIControllerClass 제거 | 03 §4.1, 07 변경 #12 (D16·D38) |
| `ATDSimCombatant` | 시뮬 몸: `AActor` + `UTDCombatComponent`(ASC, `bSuppressGameplayCues=true`), 수학 이동, 컨트롤러 없음, 픽스처 `SpawnCombatant` 방식 | 02 §2, 04 §5, 07 M1-06 |
| `FTDNeighborGrid` | `THierarchicalHashGrid2D<2,4,uint32>` 래퍼, 셀 250cm(레벨1 1,000cm), 이웃 ≤ 8, 갱신은 스텝 끝·질의는 스텝 시작 스냅샷. (07 `TDMonsterSpatialGrid` 파일이 담는다) | 02 §2, 03 §4.4 (D17) |
| `FTDMonsterTickScheduler` | `uint8 PeriodTable[4][4]`, Lod/Phase 배열, `CollectDue(Channel, StepIndex)` | 03 §2 |
| `FTDDecisionLogWriter` · `FTDMonsterDefinitionBuilder` | 전자는 JSONL 결정 로그 기록기(게임은 옵션, `.bvlog` 는 이중 송출 선택지). 후자는 C++ 빌더 진입점으로 증분 빌드 < 60초 실측 시에만 병행(MD-03) | 02 §2·§5.12, 04 §11 (D10·D34) |

### 6.2 정의 JSON·검증·커맨드렛

| 용어 | 뜻 | 정본 |
|---|---|---|
| 정의 JSON 경로·규칙 | `Content/MonsterAI/Definitions/<Id>.json`, id = 파일명, `schema=1`, `kind=monster\|player_proxy`, 최상위 키 15개(schema, id, kind, extends, stats, think_hz, lod_periods, inertia, select, top_n, abilities, sequences, actions, phases, persona) 외 미지 키 거부 | 02 §5.1 규칙 1·2 (D6) |
| 시간 단위 | 정의 파일은 초(`inertia.min_hold_seconds`, `cooldown`, `persona.reaction_delay_seconds`), 로더가 round(초 × 64) 스텝으로 변환. 시나리오 JSON 은 스텝 정수 | 02 규칙 9·10·§6, 04 §9.1 |
| `inertia` | `{switch_ratio(예 1.15), min_hold_seconds(예 0.4)}`: 최소 유지 후 도전자 점수 > 현재 × switch_ratio 일 때만 교체. `on_enter` 시퀀스·경직은 무시 | 02 규칙 9 (D1) |
| `select` / `top_n` | `max`(기본) 또는 `weighted_random`(`top_n` 필요, Brain 스트림만 소비, draws 로그) | 02 규칙 8 (D2) |
| `lod_periods` | 정의 JSON 의 채널별 4값 주기 오버라이드 키(예 `{"think":[6,13,32,64]}`). (03 '`lod` 절' 은 이 키) | 02 규칙 15, 03 §1.3 |
| `tune` 잎 | 수치 잎을 `{"value":x,"tune":[min,max]}` 로 표시하면 CMA/PSO 튜닝 벡터(정의 순서)에 등록 | 02 규칙 14, 05 §4.1 (D36) |
| 정의 해시 dh | 병합·해석 후 `FTDMonsterDefinition` 을 FNV-1a 64로. float 는 AsUInt 비트, 문자열 UTF-8, 순회 메타 → stats → abilities(이름순) → actions → 고려사항 → sequences → phases. 표기 `dh=<16 hex>`, 모든 산출물에 빌드 해시와 함께 | 02 §5.10 (D11) |
| 핫리로드 | 콘솔 `TD.MonsterAI.Reload`(ReloadAll, 1·2단 통과 시 교체), 시뮬 세션 중 잠금, 파일 감시는 `TDGameEditor` 생성 후 | 02 §5.9, 07 M2-11 (D11·D13) |
| 페르소나 정의 | `Content/MonsterAI/Definitions/Persona_{Careful,Default,Aggressive}.json`, `kind=player_proxy`, `persona{reaction_delay_seconds, potion_health_threshold, dodge_probability, preferred_range}`. PlayerProxy 스트림은 PersonaDodgeRoll 만 소비. (04 'Personas/Normal' 은 'Persona_Default') | 02 §6, 07 M1-12·M2-08 (D12) |
| Phase 0~2 몬스터 공격 | `UTDDamageDefinition`(Area/Shockwave/Projectile, ActivationDelay = 선딜, Cooldown). 예 `DA_TDGoblinSlash` 는 `TDDamageExamples::CreateExamples` 에 C++ 로 추가. JSON 시간표 없음 | 02 §5.2, 04 §6.1 (D19) |
| `UTDMonsterAIValidateCommandlet` | `-run=TDMonsterAIValidate [-only=<Id>] [-print-resolved] -nullrhi -unattended`. 종료 코드 ≠ 0 실패. 자동화 테스트 `TDGame.MonsterAI.Validate`. (07 `-definition=` 은 `-only=`) | 02 §5.7 (D8) |
| 검증 3단 임계 | 1단 스키마·미지 키 거부·참조·유사도(편집 거리 ≤ 2) / 2단 정적 규칙 / 3단 헤드리스 5초(320스텝) × 시드 3: 첫 공격 < 3초(192스텝), 교체율 < 5회/초, 대기 점유 < 60%, 2회 해시 일치, 2초 내 사고 0회 감시 | 02 §5.7, 07 M1-08·M2-06 (D8) |
| `UTDMonsterAISchemaDumpCommandlet` | `-run=TDMonsterAISchemaDump -out=Docs/MonsterAI_CombatSim/schema/` → `monster-definition.schema.json`, `inputs.md`, `actions.md`. 테스트 `TDGame.MonsterAI.SchemaUpToDate` | 02 §5.8, 07 M2-07 (D9) |
| `UTDMonsterAIBakeConstantsCommandlet` | `-run=TDMonsterAIBakeConstants` → `Source/TDGame/MonsterAI/Generated/TDMonsterDefinitions.gen.cpp`. 테스트 `TDGame.MonsterAI.BakeMatchesJson`. 전환 조건: 런타임 오류 반복 또는 곡선 해석 > 1ms@300. (07 `TDMonsterAIBake` 는 이 이름으로) | 02 §5.12 (D10) |
| `UTDAttackTimetableExtractCommandlet` | `-run=TDAttackTimetableExtract -Montage=<path>\|-All` → `Content/MonsterAI/Timetables/<Id>.json`. (07 `Attacks/` 경로는 이것으로) | 04 §6.2, 02 §1 |

### 6.3 틱·규모

| 용어 | 뜻 | 정본 |
|---|---|---|
| 스텝 | 1/64초 고정(이진 소수). 시뮬은 `World->Tick` 1회 = 스텝 1회, 게임은 누적기로 프레임당 최대 4스텝, 잔여는 PresentAlpha 보간 | 03 §1.5, 04 §2 (D27) |
| 채널 4개 · PeriodTable 기본값(스텝) | think / move / judge / present(`ETDTickChannel`). 기본 주기 think 6/13/32/64, move 1/1/1/1, judge 1/1/1/1, present 1/1/6/0 프레임. 종별 L0 think 는 `Period=round(64/think_hz)` 하한 2 | 03 §1.1·§1.3·§2 (D22) |
| LOD 4단 · 히스테리시스 | L0 교전(화면 안 ∧ 거리 ≤ 1,200cm ∨ 최근 192스텝 교전) / L1 화면 안 / L2 화면 밖 거리 ≤ 4,000cm / L3 원거리·휴면. `ETDMonsterLod{Engaged,OnScreen,NearOffScreen,Far}`. 승격 임계 R, 강등 R × 1.10, 교전 플래그 192스텝, LOD 평가 32스텝(2Hz), 승격 시 첫 스텝 강제 사고 | 03 §1.2·§1.6·§2 |
| 위상 분산 | `Phase[Slot]=Slot%Period`, `bIsDue=((StepIndex+Phase)%Period)==0`, 난수 없음. present 만 `GFrameCounter` 기준 | 03 §1.4 (D22) |
| 규모 KPI | 단계 A ≤ 300 전원 액터 몬스터 몫 ≤ 4.0ms → B 300~1,000 무액터 L3 + 액터 풀 400 ≤ 6.0ms → C ISM/VAT → D Mass(5.8.1+, `mass.EntityCompaction 0`, `mass.FullyParallel 0`). 실측 KPI 로만 전환 | 03 §5 (D24) |
| `stat TDMonsterAI` | `STATGROUP_TDMonsterAI`, 사이클 스탯 Think/Move/Separation/HashGrid/FlowField/Judge/Present | 03 §6.3 (D24) |
| 분리 조향 | 반경 200cm 이웃 최대 8, (거리², SimulationId) 오름차순, 역제곱 반발 | 03 §4.2, 07 M3-02 |
| `ring_slot` | 근접 자리 토큰: 타깃당 반경 R(예 150cm) K개(예 8), SimulationId 순 청구, 입력 RingSlotFree | 03 §4.2, 02 §8 (D17, 03 미결 2) |
| 플로우 필드 | Phase 3, 방 단위 셀 100cm 64 × 64, 목표당 1장, 32스텝 재계산을 4스텝 결정적 분할, MoveToward 구현체 교체 | 03 §4.3, 07 M3-04 |
| 단일 스레드 원칙 | 게임·시뮬 단일 스레드 시작. 병렬은 think 만, 게임에서만, `TDGame.MonsterAI.ParallelHashEquivalence` 통과 후 CVar `TD.MonsterAI.ParallelThink`(기본 0) | 03 §9 (D25) |

### 6.4 결정론 시뮬레이터

| 용어 | 뜻 | 정본 |
|---|---|---|
| SimulationId | 스폰 순번 정수 핸들. 시뮬은 단조 증가·재사용 금지, 게임은 프리리스트 + 세대 번호. 플레이어 대리 = 0. 정렬·동률·해시·로그의 유일한 키 | 04 §4, 03 §2 (D14·D30) |
| `FTDCombatRandomStreams` | 마스터 시드에서 HashCombine 파생: Combat=Hash(M,1), PlayerProxy=Hash(M,2), Spawn=Hash(M,3), Brain[SimulationId]=Hash(M,1000+SimulationId). `UTDDamageSubsystem` 소유, `GetBrainStream(SimulationId)` | 04 §3.1 (D29) |
| `TDGame.MonsterAI.NoGlobalRandom` | `Source/TDGame/{Combat,MonsterAI,CombatSim}` 의 `FMath::(FRand\|Rand\|RandRange\|SRand\|RandInit\|VRand\|RandHelper)` grep 테스트. 예외 표식 `TD_ALLOW_GLOBAL_RANDOM` | 04 §3.3, 07 M0-07 (D29) |
| `FTDCombatStateHasher` | 스텝별 FNV-1a 64비트 계층 해시(전투원 속성 9개 비트·위치·활성 GE 수, 슬롯 FSM·행동·시간표 스텝·속도, 엔티티, 스트림 `GetCurrentSeed`). `Chain[s]=Mix(Chain[s-1],HashStep(s))` | 04 §7.1 (D31) |
| 결정론 게이트 4종 | `TDGame.CombatSim.Determinism.SameProcess` / `.CrossProcess` / `.Golden` / `.OneStepPerFrame`(사고 스텝 수 == `GFrameCounter` 증가량) | 04 §7.2, 07 M1-11 |
| 골든 해시 | `Content/CombatSim/Golden/<scenario>.hash`: 체인 최종값·64스텝 체크포인트·dh·빌드 해시·엔진 버전·CPU 명령셋. `-WriteGolden` 으로 생성, 승인은 사람 | 04 §7.3, 06 §4-1 |
| 시뮬-실기 일치 3단 | A 비트 동일(시뮬 vs 시뮬) / B 이벤트 등가(적중 ±1 스텝·총 피해 동일, `TDGame.CombatSim.Parity.<Id>`) / C 통계 등가(승률 y=x) | 04 §8 (D32) |
| `FTDScopedCombatWorld` | `Source/TDGame/CombatSim/TDScopedCombatWorld.h` 공용 픽스처. `Params{StepSeconds=1/64, bIsFixedStep, bCreateNavigation=false, bCreateAISystem=false, bSimulatePhysics=false}`, `StepOnce()` = `++GFrameCounter` → `FApp` 시간 → `World->Tick(LEVELTICK_All)`. 기존 `Tick(Duration,0.02f)` 유지 | 04 §2.1 (D27·D28) |
| `FTDCombatSimSession` | 시나리오 1 × 시드 1 Run → `FTDCombatSimResult`. 커맨드렛과 자동화 테스트 공유, Step 직접 호출 금지, 시작 시 `LockDefinitions` | 04 §2.3 (D26) |
| `UTDCombatSimCommandlet` | 런타임 TDGame 모듈. `-run=TDCombatSim -Scenario=<json> -SeedStart=N -SeedCount=M -Out=<dir> -nullrhi -unattended -nosplash -nosound -FixedSeed -onethread -abslog=<file>`. 옵션 `-PrefilterOnly`, `-WriteGolden`, `-mode=train_bc\|train_ppo`(05). 시작 시 강제 CVar `tick.AllowAsyncComponentTicks 0`, `tick.AllowConcurrentTickQueue 0`, `AbilitySystem.DisableGameplayCues 1` | 04 §1 (D25·D26) |
| `FTDCombatSimScenario` | 시나리오 JSON USTRUCT: schema, id, max_steps, seeds{base,count}, arena{radius,obstacles}, player{level,equipment_effects,potions,buffs,spells,persona{definition,override}}, monsters[{definition,count,spawn,phase_override}], virtual_camera(null = 전원 L0), record{events,decisions,hash_every_steps}. 시간은 모두 스텝 정수 | 04 §9.1 (D33) |
| 시나리오 파일 | `Content/CombatSim/Scenarios/{goblin10_vs_rulebot.json, ogre_phase2.json, perf_300_camera.json}`. 게이트 시나리오도 같은 폴더(04 `Gate/`·07 §3.1 `Gate/` 는 이것으로) | 07 M1-10·M2-09·M3-03 |
| `FTDCombatSimResult` / CSV 열 | scenario_id, seed, build_hash, dh, result, end_step, dmg_dealt, dmg_taken, max_single_hit_taken, min_health_ratio, potions_used, casts_by_ability, action_share, switch_rate_per_sec, first_attack_step, monsters_killed, rng_draws, hash_final. 결과 디렉터리 `Saved/CombatSim/<run>/{results.csv, events.jsonl, decisions.jsonl, hash_chain.txt, snapshot/}`(MD-05 결정 전 임시). 승률 표준오차(p=0.5) n=100 ±5.0%p, 400 ±2.5%p, 1,000 ±1.6%p, 10,000 ±0.5%p, 밴드 판정 최소 1,000 | 04 §10.1·§10.3·§10.4, 07 MD-05 (D33) |
| 결정 로그 JSONL | 사고 1줄 `{t,sim,def,dh,ph,lod,cand[[행동,총점,[고려사항 점수]]],pick,why(max\|inertia\|sequence\|phase),hold,draws,h}` + 이벤트 줄 `{t,ev,src,dst,amount,crit,ability,h}` | 04 §11.1 (D34) |
| `Tools/CombatSim/` 스크립트 | 팬아웃 런처 `run_batch.py`(인자 scenario --seed-base --count --procs --out, --gate) + 분석 4개 `analyze_decisions.py`, `diff_runs.py`, `summarize_batch.py`(30줄 요약), `propose_tweaks.py` | 04 §11.2·§12, 07 M2-04·M2-05 (D34·D35) |
| `FTDCombatPrefilter::Estimate` | 기대 DPS/EHP 비율 사전 필터, `TDDamageFormula::Compute` 사용, 제외 구간 [0.2, 5.0](04 미결 6) | 04 §9.3 |
| `FTDAttackTimetable` | Phase 3 권위 판정: play_length_steps, windup_steps, hit_window_steps, recovery_end_steps, shape{Arc radius half_angle_deg height}, max_hits_per_target, source_hash, rules_ref | 04 §6.2 (D19) |
| `bAuthoritativeHitJudgment` | `UTDAnimNotifyState_MeleeAttack` 프로퍼티. 기본 true(플레이어 유지), Phase 3 몬스터만 false(표현·오라클) | 04 §6.2, 07 변경 #14 (D19·D20) |
| GE 만료 스텝 · 스텝 내 엔진 순서 | TimerManager `InternalTime > ExpireTime` 엄격 초과: 1초 효과는 1/64 에서 65번째, 0.02f 에서 51번째 스텝 만료. D27 유지, 기대값·cooldown_steps 해석은 N+1. 엔진 순서는 `TG_PrePhysics`(우리 틱 함수, LevelTick.cpp:1750) 뒤 `GetTimerManager().Tick`(:1816) 이라 같은 스텝 GE 만료는 다음 스텝 사고가 본다 | 06 §2 M19·§3 C2, engine-gas-determinism 결론 3 |

### 6.5 머신러닝·생성형 AI

| 용어 | 뜻 | 정본 |
|---|---|---|
| 플레이어 대리 봇 3단 | 1단 규칙 페르소나(최소판 Phase 1 M1-12, 3종 Phase 2 M2-08) → 2단 BC(`ULearningAgentsRecorder` → `ULearningAgentsImitationTrainer`, Phase 4) → 3단 PPO(상한, Phase 4 조건부) | 05 §3.1, 07 (D36) |
| `ITDPlayerProxyBrain::Think` | 대리 봇 두뇌 인터페이스. 구현 `FTDRulePersonaBrain` / `FTDPolicyBrain` / `FTDTrainerBrain`, 틱 함수의 대리 봇 단계에서 호출 | 05 §3.1·§3.4 |
| `UTDPlayerProxyInteractor` | `ULearningAgentsInteractor` C++ 구현. MaxObservedMonsters 16, MaxAbilitySlots 6, Slot 이산 8칸 + MoveDir | 05 §3.2 |
| 추론 결정론 체크리스트 | `MakePolicy(Seed=HashCombine(Master,PlayerProxy))`, `RunInference(0.0f)`, MemoryStateSize 0, `bUseParallelEvaluation false`, 에이전트 ID 유지, NNERuntimeBasicCpu 고정, 정책 해시 ph 기록, 게이트 `TDGame.CombatSim.PolicyDeterminism` | 05 §3.5 (D36) |
| 정책 파일 | `Content/CombatSim/Policies/<name>.ubnne` 스냅샷 3개 + `<name>.policy.json`(ph = SHA-256, 스키마 호환 해시, MLflow run id). 바이너리는 LFS. (07 M4-02 `*.uasset` 은 이것으로) | 05 §3.3 |
| `UTDMonsterAITuneCommandlet` | `-run=TDMonsterAITune -def=<Id> -targets=<file> -generations=30 -workers=8`. 장수 프로세스 + run_batch 팬아웃, 산출 `Saved/CombatSim/Tune/<Id>/gen<N>/`, `history.jsonl`, `<Id>.tuned.json`. 파일 `CombatSim/ML/TDMonsterAITuneCommandlet`. (07 `TDTuneCommandlet`·`tune_cma.py` 는 이것으로) | 05 §4.3·§9.2 |
| 튜닝 목적 함수 | Σw(WinRate − Target)² + 밴드 이탈 벌점 + ±30% 변경폭 벌점 + 교체율 > 5/s 벌점. Targets 는 `Content/CombatSim/Targets/*.json` | 05 §4.2 |
| `TD_WITH_LEARNING_AGENTS` | `TDGame.Target.cs` ProjectDefinitions 로 켜는 매크로. `CombatSim/ML/` 파일은 `#if` 로 격리, Build.cs 조건부 링크 | 05 §9.2 |

### 6.6 로드맵·대장

| 용어 | 뜻 | 정본 |
|---|---|---|
| Phase 0 기반(1주) | D38 최소 변경 15건, 28개 기존 테스트 실제 통과, NoGlobalRandom, 증분 빌드 실측. M0-01~M0-08 | 07 §1·§2·§3.2 |
| Phase 1 두뇌·시뮬 최소판(2~3주) | 등록표·로더·점수기·FSM·서브시스템·세션·커맨드렛·해시 게이트·고블린 10 vs 규칙 봇·PIE 300 실측. M1-01~M1-13 | 07 §3.3 |
| Phase 2 밸런스 툴 완성(2~3주) | 입력/출력 스키마·결정 로그·스크립트 4개·팬아웃·검증 3단·스키마 덤프·페르소나 3종·Archer/Ogre_Boss·1/64 이행(decision)·핫리로드·난이도 축. M2-01~M2-11, M2-15 | 07 §3.4 |
| Phase 3 게임 몸·규모·시간표(3~4주) | 몸 2종·공간 해시·주기표·플로우 필드·애니 예산·시간표 추출 + B단계·되돌림 커맨드렛·무액터 L3(blocked)·C단계 리포트·Docs/Tasks 통합 정리·CC 넉백·세이브 직렬화. M3-01~M3-10, M3-13, M3-15 | 07 §3.5 |
| Phase 4 확장(조건부) | 자체 HTN(M4-01)·BC 봇(M4-02)·CMA/PSO(M4-03)·ISM/VAT(M4-04)·Mass 스파이크(M4-05) | 07 §3.6 (D3·D36·D24) |
| 할 일 ID | `M<phase>-<번호>`. 07 에 50개(M0 8·M1 13·M2 12·M3 12·M4 5). 06 제안 중 M2-15·M3-13·M3-15 는 이관 완료, 나머지(M0-09, M1-14~16, M2-12~14·16~21, M3-11·12·14, M4-06)는 07 이관 전 무효 | 07 §3.1, 06 머리말 |
| 사용자 결정 MD-01~MD-10 | 플레이어 판정 권위 / 1/64 이행 시점(확정) / JSON vs C++ 빌더(확정) / 5.8.1 고정·골든 재생성 / 저장 위치 / StateTree 보스 / think 병렬(확정) / Docs/Tasks 통합 / CI 명령셋 고정 / DetourCrowd 허용 | 07 §4 |
| 재검토 조건 H1~H5 / S1~S7 | 자체 HTN 도입 H1~H5: 3페이즈 이상 보스 안무 또는 무리 역할 배정, 동시 ≤ 10, JSON 도메인(composite/primitive/effects/replan_hz/max_iterations), 복원점에 태스크 스택, 실패는 실패로. StateTree 보스 재검토 S1~S7: 보스 1~2종·JSON 정본·컨텍스트 직접 구동 + RandomSeed·`IsScheduledTickAllowed`·게이트 통과·디버거 사례·5.8.1+ | 01 §5.2·§5.3, 07 MD-06 (D3·D4) |
| 측정 기록 경로 | `Docs/MonsterAI_CombatSim/measurements/{incremental-build.md, pie-300.md, parity-c-<날짜>.md, mass-spike.md}`. perf CSV 도 `measurements/` 로 통일(03 §6.3 `perf/` 는 이것으로, 06 §4-2) | 07 M0-08·M1-13·M3-09·M4-05 |

## 7. 미결 사항 총괄(결정 필요 순)

각 문서의 미결을 모으고 중복을 제거했다. "시점" 은 답이 필요한 마지막 시각이다. MD-02·MD-03·MD-07 은 결정 기록이 확정(D28·D10·D25)했으므로 재확인만 남아 표에서 뺐다.

| 순위 | 항목 | 선택지·권장 | 시점 | 출처(중복 병합) |
|---|---|---|---|---|
| 1 | **MD-05 저장 위치와 run 폴더 이름·결과 보존** — 시뮬 결과·스크립트·페르소나 폴더, run 이름 규약(04 §10.4 / 07 M2-02 / 06 §4-3 셋이 다름), 30일 보존·`keep` 표식, ML 바이너리 LFS | 06 §4-3 안(텍스트 Git 추적, `Saved/` 비추적, 이름 `<scenario>-<dh8>-<빌드8>-<시드base>`) 권장 | 착수 전(M2-02 부터 영향) | 07 MD-05, 04 미결 5, 06 미결 8, 02 미결 1, 05 미결 5 |
| 2 | **MD-09 CI 골든 기준 CPU 명령셋 고정** | CI 서버 명령셋 클래스를 골든 헤더와 같게 고정 | M1-11 골든 최초 생성 전 | 07 MD-09, 04 미결 2, 05 미결 6, 06 §4-1 |
| 3 | **KPI 문서 소유자와 초기 밴드** — `kpi.md` 를 기획이 쓸지 에이전트 초안 후 승인할지, 초기 밴드(예 잡몹 1:10 승률 60~75%, 보스 45~55%) | 에이전트 초안 → 기획 승인 권장 | Phase 2 진입 전(M2-02 요약 기준) | 06 미결 2 |
| 4 | **C2 GE 만료 스텝 반영 방식** — 04 §2.2 에 기준(적용 스텝 k, 두뇌 관측 k+N·64+1) 병기만 할지 로더가 +1 보정할지 | 병기만(로더 보정은 적용 시점 의존이라 비권장) | M1-11·M2-10 전 | 06 미결 4, 06 §3 C2 |
| 5 | **텔레메트리 수집 범위** — 플레이어 입력 8스텝 다운샘플까지 기록할지 이벤트만인지, 저장 위치·보존 기간 | 06 §4-4 규격 | Phase 3 전(BC 봇·C단계 선행) | 06 미결 3 |
| 6 | **L3 move 주기 완화 여부** — 1,000마리 추정 8.5~10.3ms > 6.0ms. (a) 유지 (b) 8스텝 완화 + 공격 불가 (c) 휴면 | 실측 전 (a) 기본 | 단계 B 진입 전 | 03 미결 1 |
| 7 | **MD-01 Phase 3 이후 플레이어 근접 판정 권위** — 노티파이 스윕 유지 vs 시간표 | 유지(D20) | Phase 3 진입 | 07 MD-01, 04 미결 1 |
| 8 | **MD-10 정예·보스(≤ 10) DetourCrowd 허용** | 불허 시 시뮬 몸과 이동 동일. 허용 시 B단계 대상 확대 | Phase 3(M3-01) | 07 MD-10, 03 미결 3 |
| 9 | **MD-04 엔진 버전 고정과 업그레이드 시 골든 재생성** — 5.8.1 고정, 06 §4-7 체크리스트 채택 여부 | 06 §4-7 편입 권장 | 첫 핫픽스 적용 전 | 07 MD-04, 06 미결 항목 15 |
| 10 | **무리 전술 층 위치** — 조정자·공격 토큰을 Phase 3(M3-14 제안)에 넣을지 HTN 과 함께 Phase 4 로 미룰지 | 미루면 대량 시나리오 승률이 "동시 공격" 기준 | Phase 3 계획 시 | 06 미결 6 |
| 11 | **벤치 CI 임계** — M1-15(제안) +20%/+50% 를 Phase 1 실측 후 확정할지 경고 전용으로 둘지 | 경고 전용 시작 | M1-13 | 06 미결 7 |
| 12 | **B1 순수 커널 vs 커널 계층 규율** — M1-14(제안) 규율만 둘지 Phase 4 에 UObject 비의존 커널 추출을 열지 | 규율(D13~D18 유지) | Phase 1 | 06 미결 1 |
| 13 | **MD-06 StateTree 보스 재검토와 S6 판정 주체** — 조건 S1~S7 전부 충족 시에만 | 판정 주체·기준 미정 | 조건부 | 07 MD-06, 01 미결 3·5 |
| 14 | **weighted_random 허용 범위** — 잡몹 전체 vs 보스·엘리트만 | 넓을수록 시드 수 요구 증가 | Phase 2 | 01 미결 2 |
| 15 | **HTN 콘텐츠 시점(H1·H2)** — 3페이즈 보스·무리 역할이 어느 마일스톤에 오는가 | 콘텐츠 계획 의존 | Phase 4 | 01 미결 4 |
| 16 | **Phase 1 실측 후 조정할 수치** — 관성 기본값 1.15/0.4초, ring_slot 8자리·150cm, 거리 임계 1,200/4,000cm·액터 풀 400(여유 20% 여부), present L1 1프레임·L2 6프레임, think 병렬 임계 30%·1.0ms, 히치 시 월드 델타 클램프 | 전부 추정값, 실측 후 확정 | Phase 1·3 실측 후 | 01 미결 1, 03 미결 2·4·5·6·7 |
| 17 | **정의 형식 세부** — `CastTimetable` 원시 분리 여부, Logit·Gaussian 정확한 식, `tune` 표기(값 객체 vs 경로 목록), 클래스 이름 '제안' 항목 확정 | 02 표기가 기준 | Phase 1(M1-01~03) | 02 미결 2·3·4·5 |
| 18 | **시뮬 세부** — `FTDDamageRule` 확장으로 장비 규칙 수용 여부, 결정 로그 `sampled` 표본 규칙, 사전 필터 구간 [0.2, 5.0], `FTDScopedMeleeWorld` 공용 승격 여부 | 04 제안값 | Phase 2 | 04 미결 3·4·6·8 |
| 19 | **ML 세부** — BC 녹화 주체·분량, PPO 하드웨어(GPU 1대 vs CPU), 튜닝 되쓰기(사람 diff 검토 권장), MCP 툴셋 시점, 보스 신경망 재검토 시점, ML 소스 편입 방식 | 05 권장안 | Phase 4 진입 | 05 미결 1·2·3·4·7·8 |
| 20 | **MD-08 이 대장의 Docs/Tasks 통합** | M3-10 에서 정리 | Phase 3 말 | 07 MD-08 |

이미 닫힌 항목: 난이도 축 도입 시점(06 미결 5, 04 미결 7)은 07 이 M2-15 로 Phase 2 에 편입해 확정. `lod_periods` 종별 허용(02 미결 6)은 §6.2 표기로 확정.

## 8. 이번 조사·설계 방법 기록과 재사용 절차

| 단계 | 내용 | 산출물 |
|---|---|---|
| 1 조사 | 에이전트 16개가 엔진 소스(읽기 전용)·프로젝트 소스·웹을 주제별로 조사. 각 결론에 파일:줄 또는 URL·연도 근거 | `research/*.md` 16편 |
| 2 주장 검증 | 약 80개 에이전트가 결론 문장 단위로 엔진 소스·출처를 재확인해 "본문 vs 정정" 을 기록 | 각 조사 파일의 정정 절 |
| 3 완전성 비평 | 에이전트 1개가 누락 주제 20·모순 13·요구 밖 고려사항 14 를 도출 | `research/zz-completeness-critique.md` |
| 4 설계안 | 관점별 에이전트 4개(A 생성형 AI 제작성, B 규모·성능, C 시뮬 우선, D 실용 재사용)가 독립 설계 | 설계안 A~D(세션 임시 산출물, 저장소 미포함; 요지는 00 결정 기록에 반영) |
| 5 심사 | 에이전트 3개가 6축(결정론·LLM 제작성·규모·ML·위험·적합) 채점과 접목(GRAFT)·최종 형태 제안. 평균 A 7.82 / C 7.69 / B 7.35 / D 6.04 | 심사 판정(세션 임시 산출물, 저장소 미포함; 요지는 00 결정 기록에 반영) |
| 6 결정 기록 | 심사 합의를 D1~D38 로 확정(골격 A, 결정론 C, 규모 B, 최소 변경 D). 문서 작성자 전원에게 구속력 | [00 결정 기록](MonsterAI_CombatSim/00-decision-record.md)(저장소에 반영됨(2026-09-10)) |
| 7 문서 작성 | 에이전트 7개가 01~07 을 병렬 작성(06 은 비평 대응) | `Docs/MonsterAI_CombatSim/01~07` |
| 8 검토·수정 | 문서마다 검토 에이전트가 교차 모순·이름 불일치를 찾아 적용/보류 기록(적용 23~37건, 보류 2~6건/문서), 이어 이 인덱스 작성 | 각 문서의 '수정 적용 결과', 이 파일 |

규모: 에이전트 약 120개(16 + 약 80 + 1 + 4 + 3 + 1 + 7 + 약 8), 토큰은 약 수천만(세션 집계 없음, 추정). 총 문서 분량 01~07 약 3,480줄, 조사 약 4,490줄.

재사용 절차(다음 주제에 같은 방법을 쓸 때):

1. 사용자 질문을 5~6개로 분해하고 질문마다 "엔진 소스 조사 / 프로젝트 코드 조사 / 웹 사례 조사" 를 각각 1개 에이전트에 맡긴다. 결론 문장마다 근거(파일:줄, URL·연도)를 붙이게 한다.
2. 결론 문장 단위로 검증 에이전트를 붙여 "사실 / 정정 / 미확인" 을 표기시킨다. 미확인은 문서 끝까지 미확인으로 남긴다.
3. 조사 전체를 읽는 비평 에이전트 1개가 누락·모순·요구 밖 고려사항을 낸다.
4. 서로 다른 관점의 설계안 3~4개를 독립 작성시키고, 심사 에이전트 3개가 같은 채점표로 순위와 접목안을 낸다.
5. 결정 기록을 한 파일로 확정해 구속력을 부여한 뒤 최종 문서를 병렬 작성한다. 결정 기록에 없는 것은 문서 미결 절로 보낸다.
6. 문서마다 검토 에이전트가 교차 불일치를 찾아 적용/보류를 기록하고, 인덱스가 용어집으로 이름을 통일한다.
7. 결정론적으로 반복되는 작업(스키마 덤프, 결과 요약, 이름 검사)은 스크립트·커맨드렛으로 만들어 에이전트 토큰을 쓰지 않는다.

## 유지 방법

- 조사를 추가하면 `research/` 에 파일을 만들고 §4 표에 한 줄 추가한다.
- 할 일은 07 대장 규칙대로 상태를 갱신한다. 아키텍처를 바꾸는 결정은 결정 기록(D 번호)에 한 줄 추가하고 §2 를 갱신한다.
- 이름을 새로 정하거나 바꾸면 §6 용어집이 정본이다. 02 §2 클래스 표와 함께 고친다.
- 미결이 닫히면 §7 표의 행을 지우고 07 §4 또는 결정 기록으로 옮긴다.
- 06 제안 항목을 07 로 이관하면 06 해당 행에 "→ M?-?? 로 이관" 을 적고 §6.6 '할 일 ID' 행을 갱신한다.
