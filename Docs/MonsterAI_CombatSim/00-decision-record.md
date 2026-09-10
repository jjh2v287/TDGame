[← 인덱스로](../MonsterAI_CombatSim_Plan.md)

# 00. 결정 기록(Decision Record) — 몬스터 AI · 결정론 전투 시뮬레이터

작성 2026-09-09, 저장소 반영 2026-09-10. 근거: `research/` 조사 16편과 `research/zz-completeness-critique.md`, 관점 4개 설계안과 심사 3인 판정(세션 임시 산출물, 저장소 미포함; 요지는 이 기록에 반영). 이 문서는 01~07 문서 전체에 **구속력**이 있다. 문서 사이에 내용이 어긋나면 이 기록이 우선하고, 이 기록을 바꾸려면 `research/` 의 근거를 먼저 반박한다. 세부가 비어 있는 곳은 각 주제의 담당 문서(모델 01, 아키텍처·정의 형식 02, 틱·규모 03, 시뮬레이터 04, 머신러닝 05, 추가 고려 06, 로드맵 07)가 정본이다.

심사 합의: 골격 = 설계안 A(생성형 AI 제작성 최우선), 결정론 규율 = C(시뮬레이션 우선), 규모 구조 = B(대량 성능 우선), 최소 변경·정의 기반 공격 = D(실용 재사용). 심사 평균 점수 A 7.82 / C 7.69 / B 7.35 / D 6.04(10점 만점).

## 1. 몬스터 AI 모델

- D1. 주력 모델은 **코드 정의 유틸리티 AI**(행동 × 고려사항 표, 4파라미터 응답 곡선(m, k, b, c), 곡선 종류 Linear/Quadratic/Logistic/Logit/Gaussian/Constant/Binary, 점수 결합은 곱, 비용 등급 순 평가와 0점 조기 종료, 최고점 선택, 동률은 정의 순서, 관성 = InertiaSwitchRatio + 최소 유지 시간) + **얇은 실행 FSM(유한 상태 기계) 5상태**(Idle/Move/Cast/Sequence/Stagger). 최소 유지 시간은 JSON 에 초 단위 `min_hold_seconds` 로 적고 로더가 `FMath::RoundToInt(초 × 64)` 로 스텝으로 환산한다. 근거: `research/web-ai-architecture-comparison.md` 추천 1순위.
- D2. 보조 표현은 **데이터 정의 시퀀스(콤보)와 보스 페이즈 표**. 종 단위 옵션으로 "상위 N개 가중 무작위 선택"을 허용하되 그 몬스터의 AI 스트림만 소비하고 기본은 최고점 선택이다.
- D3. **자체 C++ HTN(Hierarchical Task Network, 계층적 태스크 네트워크)** 은 "3페이즈 이상 보스 안무 또는 무리 역할 배정" 요구가 실제 콘텐츠로 들어올 때 Phase 4 에서 추가한다. 형식은 JSON 도메인(composite/primitive/effects/replan_hz/max_iterations, 재귀 종료 정적 검사, 복원점에 태스크 스택 포함, 실패는 실패로 반환). 엔진 HTNPlanner 플러그인은 채택하지 않는다(실행기 부재, 백트래킹 결함, 방치 — `research/engine-htnplanner-plugin.md` 결론 1·2·3·6).
- D4. **엔진 비헤이비어 트리·StateTree·GOAP·Mass 두뇌·Mover·MLAdapter 는 전투 코어에서 배제**한다. StateTree 는 "보스 연출·시각 디버깅용 재검토 선택지"로만 남긴다. 근거 요약: 비헤이비어 트리는 텍스트 직렬화기 부재·서비스 기본 난수·마리당 UObject 3~4개·틱 간격 외부 제어 불가(`engine-behaviortree-tick` 결론 2·5·6·7); StateTree 는 런타임 구동·시드는 가능하나 트리 조립·컴파일이 UncookedOnly 에디터 모듈에만 있고 MCP(Model Context Protocol, 모델 컨텍스트 프로토콜) 툴셋은 검사 전용, 5.6~5.8 폐기 24~71건·예약 틱 회귀(`engine-statetree-runtime` 결론 6·8, `web-llm-authorable-tooling` 결론 2·3, `web-ue-5-6-to-5-8-ai-changes` 결론 10); GOAP 는 실계획 길이 1~2·탐색 비용(`web-ai-architecture-comparison` 결론 2·3); Mass 는 엔티티 압축 벽시계 예산·Epic "결정론 재생 계획 없음"·GAS 미이식·5.8.0 병렬 회귀(`engine-mass-entity-ai` 결론 5, `web-ue-5-6-to-5-8-ai-changes` 결론 2, `web-mass-monster-performance` 결론 4).
- D5. 비교표는 후보 8개(비헤이비어 트리, StateTree, 유한 상태 기계, 엔진 HTNPlanner 플러그인, 자체 HTN, GOAP, 유틸리티, 하이브리드)를 결정론·틱 제어·코드 정의·LLM 제작성·마리당 비용·디버깅·엔진 의존·성숙도 8열로 만들고 각 셀에 조사 파일명과 결론 번호를 단다.

## 2. 정의 형식(정본)

- D6. **몬스터 한 종 = `Content/MonsterAI/Definitions/<Id>.json` 파일 하나가 정본.** 내용은 평면 표 4개(actions, considerations, sequences, phases) + 메타(think_hz, LOD 주기표 오버라이드, extends 상속, tune 잎 표시). 트리 DSL(도메인 특화 언어)이 아니다. 파서는 `FJsonObjectConverter`(USTRUCT ↔ JSON, `FInstancedStruct` 지원 — `engine-misc-decision-tools` R13).
- D7. **행동 원시(action primitive)·입력 함수(input)·FSM 상태는 C++ 정적 등록표**에 둔다. 입력 함수는 스텝 시작 시 채운 `FTDBrainInputs` 스냅샷만 읽고 액터·월드 접근은 등록 시 거부한다. 각 원시에 `bSimulatable` 플래그와 비용 등급을 두며 검증기가 평가 순서를 자동 정렬한다(레이캐스트 계열 마지막).
- D8. **검증기 3단**: (1) 엄격 스키마·미지 키 거부·등록표 참조 무결성·이름 유사도 힌트 → (2) 정적 규칙(도달 불가 행동, 항상 0 인 고려사항, 페이즈 순환, 시퀀스 길이) → (3) 헤드리스 5초 생존 시뮬(첫 공격 < 3초, 행동 교체율 < 5회/초, 대기 점유 < 60%, 2회 실행 해시 일치, "2초 내 사고 0회" 감시). 진입점은 커맨드렛 `-run=TDMonsterAIValidate` 와 자동화 테스트 `TDGame.MonsterAI.Validate`. `-print-resolved` 로 extends 병합 결과를 덤프한다.
- D9. **스키마 문서는 코드에서 생성**한다: `UTDMonsterAISchemaDumpCommandlet` 이 USTRUCT 리플렉션과 등록표에서 `monster-definition.schema.json`, `inputs.md`, `actions.md` 를 재생성한다. 대규모 언어 모델 에이전트는 AGENTS.md 의 절차 8줄 + 이 문서 + 예시 1개(약 3.5~5천 토큰)만 읽는다.
- D10. **되돌림 경로**: Phase 3 안에 "JSON → C++ 상수표 생성 커맨드렛"을 만들어, 검증기가 못 잡는 런타임 오류가 반복되거나 곡선 해석 비용이 300마리 기준 1ms 를 넘으면 형식은 유지한 채 컴파일 산출물로 전환할 수 있게 한다. Phase 1 시작 전 "종 하나 추가 시 증분 빌드 시간"을 실측해 기록한다(60초 미만이면 C++ 빌더 진입점을 병행 유지).
- D11. 부동소수점 문자열 왕복 위험: 로드 시 수치를 float 비트(uint32) 로 덤프해 **정의 해시**에 포함하고, 모든 산출물(로그·결과·골든 해시)에 정의 해시와 빌드 해시를 찍는다. 핫리로드는 게임·에디터에서만 허용하고 시뮬 세션 중에는 잠근다.
- D12. **플레이어 대리 봇(페르소나)도 같은 JSON 형식**(반응 지연, 물약 임계, 회피 확률 등 페르소나 파라미터)으로 작성한다. 두뇌 형식이 하나라 검증기·로그·분석 스크립트를 공유한다.

## 3. 아키텍처·상태 소유

- D13. **새 모듈을 만들지 않는다.** `Source/TDGame/MonsterAI/` 와 `Source/TDGame/CombatSim/` 두 폴더만 추가한다. 커맨드렛도 런타임 TDGame 모듈에 둔다(기존 `UTDDamageExamplesCommandlet` 선례, `UCommandlet` 은 Engine 모듈). `TDGameEditor` 는 병행 월드젠 작업 소유이므로 생성하지 않으며, 파일 감시·MCP 툴셋 같은 에디터 전용 편의는 그 모듈이 생긴 뒤 옮긴다. `TDGame.Build.cs` 에 `Json`, `JsonUtilities` 의존을 추가한다.
- D14. **몬스터 상태의 정본은 월드 서브시스템 `UTDMonsterThinkSubsystem` 이 소유한 슬롯 배열(SoA(Structure of Arrays, 배열 구조체), Mass 프래그먼트 모양)**. 슬롯 = SimulationId(스폰 순번, 시뮬에서는 단조 증가·재사용 금지) + 세대 번호. 액터는 표현·충돌 껍데기다.
- D15. **단일 고우선 틱 함수(TG_PrePhysics) 하나가 SimulationId 오름차순으로** 공간 해시 갱신 → 사고(LOD 별 주기) → 2D 이동 적분 → 공격 시간표 진행·형상 판정 → `UTDDamageSubsystem::ExecuteRules` 를 처리한다. **게임과 시뮬 모두 이 틱 함수가 `World->Tick` 안에서 정확히 1회 돈다. 시뮬 러너가 Step 을 직접 호출하지 않는다**(이중 스텝 금지, 결정론 게이트가 "스텝당 사고 횟수"로 검출).
- D16. **몸(Body)은 `ITDMonsterBody` 인터페이스로 격리**: 게임 잡몹 = `APawn` + `UFloatingPawnMovement`(캡슐 QueryOnly, AIController 없음, 액터 틱 없음), 게임 정예/보스 = `ATDMonsterCharacter`(CMC(Character Movement Component) NavWalking, bAlwaysCheckFloor=false, bEnablePhysicsInteraction=false, RVO 끔), 시뮬 = 수학 이동 `ATDSimCombatant`(픽스처 SpawnCombatant 방식, 컨트롤러 없음). 컨트롤러 없는 이동 컴포넌트가 실제로 움직이려면 03 문서의 조건(`UFloatingPawnMovement` 컨트롤러 검사, CMC `bRunPhysicsWithNoController`)을 지킨다. 몬스터 어빌리티 NetExecutionPolicy 는 ServerOnly 옵션.
- D17. **근접 탐색은 `THierarchicalHashGrid2D`** (셀 250cm, 이웃 ≤ 8) 로 하고 잡몹의 물리 오버랩 이벤트·RVO·DetourCrowd 는 쓰지 않는다. 이동은 직선 접근 + 분리 조향 + 근접 자리 토큰(RingSlot)으로 시작하고, 방 단위 플로우 필드(목표당 1장, 2Hz, 커널 스텝 4회 결정적 분할)는 Phase 3 에서 `MoveToward` 원시의 구현체로 붙인다.
- D18. **GAS(Gameplay Ability System) 는 유지**한다. 몬스터도 ASC(`UTDCombatComponent`)를 갖되 `bSuppressGameplayCues=true`, 몽타주·틱 태스크 미사용(ASC 틱 비용 0 근사 — `engine-gas-determinism` 결론 4·10). 무액터 L3 강등을 도입할 때만 SoA 체력 미러를 두며 "승격 시 ASC 값이 정본" 규칙과 강등 금지 규칙(활성 GE·손상 체력·보스/엘리트)을 함께 넣는다. `TDDamageFormula::Compute` 정적 순수 함수를 분리해 GAS 경로와 미래 경량 커널이 같은 공식을 쓴다.

## 4. 공격 판정(단일 경로)

- D19. **몬스터 공격 판정은 게임·시뮬 모두 애니메이션이 아니라 데이터가 권위**다. Phase 0~2: 기존 `UTDDamageDefinition`(Area/Shockwave/Projectile + ActivationDelay=선딜, Cooldown)으로 판정(스켈레탈 메시 없이 성립). Phase 3 에서 몽타주 주도 근접 몬스터가 생기면 `FTDAttackTimetable`(선딜·히트 창·형상 파라미터·소켓 궤적 바운딩, 몽타주 source_hash 포함)을 커맨드렛으로 추출해 권위 판정으로 쓰고, 애님 노티파이는 표현·오라클 테스트 전용(`bAuthoritativeHitJudgment` 게이트)으로 강등한다.
- D20. **플레이어의 근접 노티파이 스윕 경로(TDMeleeAttackNotifyTests 로 검증됨)는 게임에서 그대로 유지**한다. 시뮬의 플레이어 대리는 플레이어 몽타주에서 추출한 시간표로 판정하며, 게임-시뮬 등가는 B단계(이벤트 ±1 스텝) 정합 테스트로 보증한다. Phase 3 이후 플레이어도 시간표 권위로 전환할지는 결정 항목으로 남긴다.
- D21. 화면 밖에서도 판정이 멈추지 않는다: 이동·판정 스텝은 LOD(Level of Detail, 세부 수준) 와 무관하게 매 스텝 진행되고, LOD 는 사고 주기와 표현만 바꾼다.

## 5. 틱·규모

- D22. **네 채널(think/move/judge/present) × LOD 4단(L0 교전 / L1 화면 안 / L2 화면 밖 근처 / L3 원거리·휴면) 주기표 `uint8 PeriodTable[4][4]`** 를 데이터로 두고, 위상은 `Phase[Slot] = Slot % Period`(프레임 카운터 버킷) 로 결정적으로 분산한다. 기본값(03 문서 정본): think 6/13/32/64 스텝(≈10.7/4.9/2/1Hz), move·judge 는 전 LOD 매 스텝, present 1/1/6/0 프레임. 프레임당 스텝 상한 4 의 누적기. 히스테리시스 10%, 승격 시 첫 스텝 강제 사고.
- D23. 시뮬레이터 기본은 **전원 L0 사고**. 성능 검증 시나리오만 `virtual_camera`·`actor_pool` 입력을 명시해 LOD 영향을 재현 가능하게 한다.
- D24. 규모별 단계(실측 KPI 로만 전환): 단계 A ≤ 300 전원 액터(몬스터 몫 합 ≤ 4.0ms) → 단계 B 300~1,000 무액터 L3·액터 풀 400(≤ 6.0ms) → 단계 C ISM/VAT(AnimToTexture) 후열 → 단계 D Mass 는 프래그먼트 1:1 이관 선택지로만(5.8.1 이상, mass.EntityCompaction 0, mass.FullyParallel 0). 애니메이션은 Animation Budget Allocator(a.Budget.BudgetMs 1.5ms) + VisibilityBasedAnimTickOption. 프레임 예산 표는 "추정 → Phase 1 실측"으로 갱신하는 살아 있는 표이며 `stat TDMonsterAI` 카테고리를 만든다.
- D25. **게임·시뮬 모두 단일 스레드**로 시작. 게임 내 think 병렬화는 프로파일에서 병목으로 실측되고 "병렬/직렬 해시 동일" 테스트가 통과한 뒤 게임에서만 옵트인. 시뮬레이터는 항상 단일 스레드(`-onethread`, `tick.AllowAsyncComponentTicks 0`).

## 6. 결정론 전투 시뮬레이터

- D26. **실행 형태**: 세션 클래스 `FTDCombatSimSession` 하나를 (a) 커맨드렛 `-run=TDCombatSim`(배치, `-nullrhi -unattended -FixedSeed -onethread`, 프로세스 팬아웃) 과 (b) 자동화 테스트(`TDGame.CombatSim.*`, CI 게이트) 가 공유. 레이턴트 커맨드 방식 금지(`engine-determinism-headless` 결론 8).
- D27. **고정 스텝 루프**: 기존 `FTDScopedCombatWorld` 를 공용 헤더로 승격. 스텝 = 1/64초(이진 소수, GE 타이머 누적 오차 없음). 매 스텝 `++GFrameCounter` → `FApp::SetDeltaTime/SetCurrentTime` 갱신 → `World->Tick(LEVELTICK_All, Step)` → 상태 해시. 월드 초기화값 `CreateNavigation(false)`·`CreateAISystem(false)`·`ShouldSimulatePhysics(false)` 기본, 필요 시나리오만 켠다. `MaxUndilatedFrameTime`(기본 0.4초)·`MinUndilatedFrameTime` 을 스텝값으로 고정. 시나리오마다 새 월드 + `CollectGarbage`. 종료 조건은 초가 아니라 MaxSteps. 타이머 만료는 "엄격 초과" 조건이라 1초 지속 효과는 65번째 스텝에 만료된다(재현성은 유지).
- D28. **기존 28개 자동화 테스트는 0.02 스텝 기본값을 유지**하고 Phase 0 에서 실제 통과를 먼저 확인한다(GAS 전환 후 미실행 상태). 1/64 로의 이행은 Phase 2 의 별도 작업(기대값을 스텝 수로 재정의)으로 둔다.
- D29. **난수**: 시나리오 마스터 시드에서 `HashCombine` 으로 파생한 이름 있는 `FRandomStream` 만 사용 — Combat(치명타·산포), PlayerProxy, Spawn, 몬스터별 AI 스트림 `Hash(Master, 1000 + SimulationId)`. 결정 로그에 사고당 소비 횟수(draws) 기록. `FMath::FRand/FRandRange/Rand/Rand32/RandRange/SRand` 사용을 `Source/TDGame/{Combat,MonsterAI,CombatSim}` 에서 grep 하는 자동화 테스트 `TDGame.MonsterAI.NoGlobalRandom`. 기존 두 곳(`TDCombatComponent.cpp:220` 치명타, `TDDamageSubsystem.cpp:203-204` 산포)은 `FTDDamageContext` 의 스트림 포인터로 교체.
- D30. **순서 결정성은 SimulationId 하나로 통일**: `GatherTargets` 결과 정렬, `TDDamageEntity.cpp:305-307·497-498` 의 `GetUniqueID` tie-break 교체, 스윕 다중 히트 (Time, SimulationId) 안정 정렬, 오버랩 결과 정렬, 타이머 삽입 순서, 스폰 순서. 액터 이름을 키로 쓰지 않는다.
- D31. **상태 해시**: 스텝별 FNV-1a 계층 해시 체인(전투원 속성·위치·FSM 상태·활성 GE 수·엔티티·난수 스트림 현재 시드 포함). 게이트 테스트: 같은 프로세스 2회 + 다른 프로세스 1회 + 골든 해시. `diff_runs.py` 가 최초 이탈 스텝을 찾는다. 골든 해시 갱신 절차와 /fp:fast·FMA·CPU 명령셋 주의를 문서화한다.
- D32. **시뮬-실기 일치 3단 정의**: A 비트 동일(시뮬 vs 시뮬, 프로세스 간) / B 이벤트 등가(같은 헤드리스 월드에 실제 메시+몽타주 재생 vs 시간표 판정, 적중 시각 ±1 스텝·총 피해 동일) / C 통계 등가(플레이테스트 승률 y=x 산포). "게임 = 시뮬 비트 동일"은 약속하지 않는다.
- D33. **입력 스키마(JSON)**: 플레이어 조건(레벨, 장비 → GE 목록, 물약 정책, 버프 GE, 페르소나 JSON), 몬스터 조합(종 × 마릿수 × 스폰 배치 규칙, phase_override), 시나리오(맵 반경, MaxSteps, 시드 배열, virtual_camera 선택). 사전 필터로 기대 DPS/EHP(유효 피격 허용량) 비율 계산기. **출력**: CSV/JSONL — 승패, 생존 스텝, 총 피해·받은 피해, 물약 소비, 리썰 위험(최저 체력 비율), 행동 점유율, 교체율, 분위수(p10/p50/p90), 승률 ± 표준오차(n=400 → ±2.5%p, 1,000 → ±1.6%p, 10,000 → ±0.5%p). 결과 옆에 정의 스냅샷(JSON + 해석된 등록표) + 빌드 해시 저장.
- D34. **결정 로그**: JSONL 한 사고 한 줄(스텝, SimulationId, 후보별 총점과 고려사항별 점수, 선택, 이유, draws, 상태 해시) + 이벤트 줄. 분석 스크립트(`analyze_decisions.py` 행동 점유·0 원인, `diff_runs.py`, `summarize_batch.py` 30줄 요약, `propose_tweaks.py` 규칙 힌트). 비주얼 로거(.bvlog)는 에디터 보기용 이중 송출 선택지(`SetGetTimeStampFunc` 로 스텝 시간 주입).
- D35. **병렬은 프로세스 팬아웃**(시드 = 기본 시드 + 시나리오 인덱스). 파이썬 런처 `Tools/CombatSim/run_batch.py` 가 프로세스 N개를 띄우고 결과를 병합한다.

## 7. 머신러닝·생성형 AI

- D36. ML 적용은 두 지점: (1) **플레이어 대리 봇** 규칙 페르소나 → Learning Agents 행동 복제(BC, `ULearningAgentsRecorder`→`ULearningAgentsImitationTrainer`) → PPO 는 상한 탐색용. 추론 결정론: `MakePolicy(Seed)`, `RunInference(ActionNoiseScale=0)`, MemoryStateSize 0, 에이전트 ID 유지(Add/Remove 반복 버그 회피), CPU 고정. (2) **JSON `tune: true` 잎 벡터의 CMA/PSO 블랙박스 튜닝**(LearningCore `LearningCMAOptimizer`/`LearningPSOOptimizer`) — 목적 함수는 시뮬 승률 밴드. 몬스터 두뇌 신경망화·MLAdapter 는 채택하지 않는다. 학습은 외부 파이썬 프로세스(에디터 1회 실행으로 PipInstall 선행), MLflow 플러그인은 실험 추적용 옵션.
- D37. **LLM 제작 루프**: 스키마 덤프 읽기 → JSON 작성 → 검증 3단 → 100~200 시드 배치 → 30줄 요약 → 수정. 종당 컴파일 0회. MCP 툴셋은 CLI 와 같은 세션 코드를 호출하는 보조 채널(에디터 필요)로만 둔다. Epic AIAssistant 플러그인은 사용하지 않는다(에디터 내장 웹앱).

## 8. 기존 코드 변경 최소 집합(Phase 0)

- D38. `FTDDamageContext` 에 `FRandomStream*`(또는 스트림 핸들) 추가; `TDCombatComponent.cpp:220`, `TDDamageSubsystem.cpp:203-204` 교체; `GatherTargets` 정렬; `TDDamageEntity.cpp:305-307·497-498` tie-break 교체; `TDDamageEntity.cpp:17-24` 프리젠테이션 컴포넌트 생성 게이트(헤드리스에서 Niagara/메시 생성 안 함), `bDrawDebug` 기본 false; 픽스처 공용 헤더 추출; `TDDamageFormula::Compute` 분리; `ATDMonsterCharacter` 의 AutoPossessAI/AIControllerClass 제거(정예 경로는 컨트롤러 없는 이동 요청). 28개 테스트 실제 통과가 Phase 0 완료 조건.

## 9. 로드맵(요약, 상세는 07 문서)

- Phase 0 (기반, 1주 추정): 위 최소 변경, 테스트 통과, 시드 스트림, SimulationId, 픽스처 승격, NoGlobalRandom 테스트, 증분 빌드 시간 실측.
- Phase 1 (두뇌·시뮬 최소판, 2~3주 추정): 등록표·JSON 로더·검증기 1~2단·유틸리티 점수기·FSM·서브시스템 틱·시뮬 세션·커맨드렛·해시 게이트·"고블린 10마리 vs 플레이어 규칙 봇 승률" 산출, 300마리 PIE 실측.
- Phase 2 (밸런스 툴 완성, 2~3주 추정): 입력/출력 스키마, 분석 스크립트, 프로세스 팬아웃, 지표·신뢰구간, 검증기 3단, 스키마 덤프, 페르소나 봇, 1/64 이행.
- Phase 3 (게임 몸·규모·시간표, 3~4주 추정): APawn 잡몹 몸, 공간 해시, LOD 주기표, 플로우 필드, 애니 예산, 시간표 추출·B단계 정합 테스트, JSON→상수표 되돌림 커맨드렛, 무액터 L3(실측 시).
- Phase 4 (확장, 조건부): 자체 HTN(보스·무리), BC 플레이어 봇, CMA 튜닝, ISM/VAT, Mass 스파이크(선택).

## 10. UKGame 과의 차이(요약)

모델을 넷(BT+ST+HTN+FSM)에서 하나(유틸리티+FSM)로; 매니저가 액터 틱을 "나눠 부르는" 대신 상태 정본을 서브시스템 배열로 옮기고 게임과 시뮬레이터가 같은 Step 을 SimulationId·시드·해시 명세 아래 돌림; 액터 이름 문자열 대신 SimulationId; 리플렉션으로 엔진 비공개 필드 접근 금지; 에셋 대신 텍스트 정본.

## 11. 정오표(2026-09-10 저장소 반영 시 수정)

| 항목 | 이전 | 수정 | 이유 |
|---|---|---|---|
| D5 후보 수 | "후보 7개" | 후보 8개 | 나열이 8개였다(교차 검토 지적). |
| D1 관성 단위 | "최소 유지 스텝" | 최소 유지 시간(`min_hold_seconds`, 로더가 스텝 환산) | 02 문서 정의 형식과 단위 통일. |
| D22 주기 기본값 | "L0 think 10Hz, L1 5Hz, L2 2Hz, L3 1Hz, present 1/2/6/0" | think 6/13/32/64 스텝(≈10.7/4.9/2/1Hz), present 1/1/6/0 프레임 | 03 문서가 스텝 단위 표를 정본으로 확정. |
| D27 타이머 만료 | (없음) | 1초 지속 효과는 65번째 스텝 만료 명시 | 비평 C2 판정 반영. |
| D29 금지 정규식 | FRand/Rand/RandRange/SRand | FRandRange·Rand32 추가 | 04 검토가 정규식 구멍을 지적. |
| D35 런처 이름 | (없음) | `Tools/CombatSim/run_batch.py` | 04 문서 정본으로 통일. |
