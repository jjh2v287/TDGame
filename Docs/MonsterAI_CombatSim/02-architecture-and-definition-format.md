[← 인덱스로](../MonsterAI_CombatSim_Plan.md)

# 02. 아키텍처와 몬스터 AI 정의 형식

작성 2026-09-09. 구속 기준: 결정 기록 D1~D38. 관련 문서: [01 AI 모델 결정](01-ai-model-decision.md), [03 틱·규모](03-tick-and-scale.md), [04 시뮬레이터](04-combat-simulator.md), [05 머신러닝·생성형 AI](05-ml-and-generative-ai.md), [07 로드맵](07-roadmap-and-tasks.md).

## 이 문서가 답하는 질문

1. 새 코드는 어느 폴더·모듈에 들어가고, 클래스는 무엇이며, 그중 게임과 시뮬레이터가 함께 쓰는 것은 무엇인가?
2. 한 스텝 안에서 데이터는 어떤 순서로 흐르고, 게임 런타임과 헤드리스 시뮬레이터는 어디까지 같은 코드인가?
3. 몬스터 한 종을 정의하는 JSON 은 정확히 어떤 규칙을 따르며, LLM(Large Language Model, 대규모 언어 모델)은 무엇을 외워야 하는가?
4. 정의 파일은 어떻게 검증·해시·핫리로드·되돌림되는가?
5. 생성형 AI 에이전트가 종 하나를 추가할 때 어떤 절차를 밟고 토큰을 얼마나 쓰는가?

## 결론 요약(결정 문장)

- 새 모듈 없이 `Source/TDGame/MonsterAI/` 와 `Source/TDGame/CombatSim/` 두 폴더만 추가하고, 커맨드렛도 런타임 `TDGame` 모듈에 둔다(D13).
- 몬스터 상태의 정본은 `UTDMonsterThinkSubsystem` 이 소유한 SoA(Structure of Arrays, 배열 구조체) 슬롯 배열 `FTDMonsterBrainSlot` 이고, 액터는 표현·충돌 껍데기다(D14).
- 고우선 틱 함수 하나가 `World->Tick` 안에서 스텝당 정확히 1회, SimulationId 오름차순으로 "공간 해시 → 사고 → 이동 → 판정 → 피해" 를 처리한다. 시뮬 러너는 Step 을 직접 부르지 않는다(D15).
- 몬스터 한 종 = `Content/MonsterAI/Definitions/<Id>.json` 파일 하나가 정본이다. 평면 표 4개(actions, considerations, sequences, phases)와 메타이며 트리 DSL 이 아니다(D6).
- 행동 원시·입력 함수·FSM 상태는 C++ 정적 등록표에 둔다. 입력 함수는 스텝 시작 스냅샷 `FTDBrainInputs` 만 읽고, 원시마다 `bSimulatable` 과 비용 등급을 갖는다(D7).
- 검증기는 3단(스키마·참조 → 정적 규칙 → 5초 생존 시뮬)이고 스키마 문서는 커맨드렛이 코드에서 생성한다(D8·D9).
- 정의 해시는 수치를 float 비트로 포함하며, 핫리로드는 게임·에디터에서만 허용하고 시뮬 세션 중에는 잠근다(D11).
- 플레이어 대리 페르소나도 같은 JSON 형식이다(D12). Phase 3 안에 "JSON → C++ 상수표" 되돌림 커맨드렛을 만든다(D10).

---

## 1. 모듈·폴더(D13)

```
Source/TDGame/MonsterAI/                 런타임. 정의·등록표·점수기·FSM·서브시스템·슬롯·로그
Source/TDGame/MonsterAI/Tests/           자동화 테스트 TDGame.MonsterAI.* (검증·결정론·NoGlobalRandom)
Source/TDGame/CombatSim/                 런타임. 세션·시나리오·해시·플레이어 대리·커맨드렛
Source/TDGame/CombatSim/Tests/           자동화 테스트 TDGame.CombatSim.* (해시 게이트·회귀)
Content/MonsterAI/Definitions/<Id>.json  몬스터 정의 정본. 페르소나도 같은 폴더(kind 로 구분)
Content/MonsterAI/Timetables/<Id>.json   공격 시간표(Phase 3 커맨드렛 산출물, 커밋). <Id> 는 시간표 id(예 Goblin_Slash, 04 §6.2)
Docs/MonsterAI_CombatSim/schema/         커맨드렛이 덤프한 스키마·입력·행동 목록(LLM 이 읽는 참고서)
Tools/CombatSim/*.py                     배치 런처 1개(run_batch.py, D35) + 분석 스크립트 4개(D34)(신규 폴더)
```

| 항목 | 결정 | 근거 |
|---|---|---|
| 모듈 | 새 모듈 없음. `TDGameEditor` 는 병행 월드젠 작업 소유라 만들지 않는다 | D13 |
| 커맨드렛 위치 | 런타임 `TDGame` 모듈. `UCommandlet` 은 Engine 모듈 클래스이므로 에디터 모듈이 필요 없다 | 기존 선례 `Source/TDGame/Combat/Damage/TDDamageExamplesCommandlet.h:8`; `Engine/Source/Runtime/Engine/Classes/Commandlets/Commandlet.h:40` |
| 빌드 의존 | `TDGame.Build.cs` 에 `Json`, `JsonUtilities` 추가 | D13 |
| 에디터 전용 편의 | 정의 파일 감시(`IDirectoryWatcher`)·MCP(Model Context Protocol) 툴셋은 `TDGameEditor` 가 생긴 뒤 옮긴다. 그 전까지 콘솔 명령·커맨드렛으로 완결 | D13, D37 |
| 병행 작업 접점 | `Docs/WorldDungeonPCG_Plan.md`·`Docs/Tasks/` 파일은 건드리지 않는다 | 작업 규칙 |

## 2. 클래스 목록

공용 = 게임 런타임과 헤드리스 시뮬레이터가 같은 코드를 실행. 이름 규칙: 결정 기록에 있는 이름은 그대로 쓰고, 나머지는 본 문서 제안이다. '출처' 열의 `결정` 은 결정 기록에 식별자가 그대로 있는 것, `결정(진입점)` 은 결정 기록이 `-run=` 이름만 정한 것, `제안` 은 본 문서가 지은 이름이다. [07 로드맵](07-roadmap-and-tasks.md) 의 산출물 파일 이름(예 `TDMonsterDefinitionLoader`, `TDBrainRegistry`, `TDMonsterAIValidator`)은 이 표의 이름과 일치시켜야 한다(07 문서 갱신 요청, 미결 4).

| 클래스 | 종류 | 역할 한 줄 | 공용 여부 | 출처 |
|---|---|---|---|---|
| `UTDMonsterThinkSubsystem` | UWorldSubsystem | 슬롯 배열 소유, 고우선 틱 함수 1개, 스텝 순서 고정, 로그·해시 호출 | 공용 | 결정 D14 |
| `FTDMonsterBrainSlot` | SoA 배열 묶음 | SimulationId·세대·정의 인덱스·위치·속도·체력 미러(L3 도입 시)·FSM 상태·현재 행동·관성 잔여·쿨다운 스텝·시퀀스 커서·페이즈·LOD. 내부 배치는 [03 §8.2](03-tick-and-scale.md) 의 프래그먼트 6종(03 의 `FTDMonsterSlots` 는 이 이름으로 통일). 몬스터별 난수 스트림은 슬롯이 아니라 [04 §3.1](04-combat-simulator.md) 의 `FTDCombatRandomStreams::BrainStreams` 에만 둔다 | 공용 | 제안(D14 "슬롯 배열") |
| `FTDBrainInputs` | POD 스냅샷 | 사고 1회 입력(거리·마주봄·체력비·이웃 목록 ≤ 8·능력 준비·대상 공격 중·자리 토큰) | 공용 | 결정 D7 |
| `FTDUtilityScorer` | 정적 함수 | 정의 + 스냅샷 + 슬롯 상태 → 후보 점수·선택(비용순 조기 종료·관성·정의 순서 동률) | 공용 | 제안(D1) |
| `FTDResponseCurve` | USTRUCT + 정적 함수 | 곡선 7종 × (m, k, b, c) 평가, 단위 정사각형, 0~1 클램프 | 공용 | 제안(D1) |
| `FTDMonsterDefinition` | USTRUCT | 한 종의 전체 정의. JSON 과 의미상 1:1(키 이름·잎 형식은 §5.1 의 변환표를 거친다. `FTDUtilityAction`, `FTDUtilityConsideration`, `FTDActionSequence`, `FTDMonsterPhase` 포함) | 공용 | 제안(D6) |
| `UTDMonsterDefinitionLibrary` | UEngineSubsystem | JSON 로드·extends 병합·등록표 해석·정의 해시·핫리로드·잠금 | 공용 | 제안(D6·D11; 07 M1-01 의 `TDMonsterDefinitionLoader` 와 통일 필요) |
| `FTDMonsterActionRegistry` | 정적 표 | 원시 이름 → FSM 진입 상태·인자 스키마·`bSimulatable`·실행 함수 | 공용 | 제안(D7; 07 M1-02 의 `TDBrainRegistry` 와 통일 필요) |
| `FTDBrainInputRegistry` | 정적 표 | 입력 이름 → 함수·비용 등급·기본 정규화 범위 | 공용 | 제안(D7; 07 M1-02 의 `TDBrainRegistry` 와 통일 필요) |
| `FTDMonsterActionExecutor` | POD FSM | 5상태(Idle/Move/Cast/Sequence/Stagger) 진행, 몸에 명령 | 공용 | 제안(D1) |
| `ITDMonsterBody` | UInterface | 위치 읽기·이동 요청·시전·페이싱·경직 | 공용(구현 3종) | 결정 D16 |
| `ATDMonsterPawn` | APawn | 게임 잡몹 몸. `UFloatingPawnMovement`, 캡슐 QueryOnly, AIController·액터 틱 없음 | 게임 | 제안(D16 "APawn") |
| `ATDMonsterCharacter` | 기존 ACharacter | 게임 정예/보스 몸. CMC NavWalking, AutoPossessAI 제거(D38) | 게임 | 기존 코드(D16) |
| `ATDSimCombatant` | AActor | 시뮬 몸. 루트 셰이프 + `UTDCombatComponent`, 수학 이동, 컨트롤러 없음 | 시뮬 | 결정 D16 |
| `FTDNeighborGrid` | 클래스 | `THierarchicalHashGrid2D<2,4,uint32>` 래퍼, 셀 250cm | 공용 | 제안(D17 래퍼) |
| `FTDAttackTimetable` | USTRUCT | 선딜·히트 창·형상·소켓 궤적 바운딩·몽타주 source_hash(Phase 3) | 공용 | 결정 D19 |
| `FTDCombatRandomStreams` | 클래스 | 마스터 시드 → 이름 있는 스트림(Combat/PlayerProxy/Spawn/몬스터별 AI) | 공용 | 제안(D29) |
| `FTDDecisionLogWriter` | 클래스 | JSONL 결정 로그(형식은 [04](04-combat-simulator.md)) | 공용(게임은 옵션) | 제안(D34) |
| `FTDCombatStateHasher` | 정적 함수 | 스텝별 FNV-1a 계층 해시 | 시뮬(게임 디버그 옵션) | 제안(D31) |
| `FTDScopedCombatWorld` | 승격 공용 헤더 | 고정 스텝 월드 픽스처(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:30` 에서 이동) | 시뮬·테스트 | 기존 코드(D27) |
| `FTDCombatSimSession` | 클래스 | 시나리오 1개 × 시드 1개 실행, 커맨드렛·자동화 테스트 공유 | 시뮬 | 결정 D26 |
| `UTDCombatSimCommandlet` | UCommandlet | `-run=TDCombatSim` 배치 실행 | 시뮬 | 결정(진입점, D26) |
| `UTDMonsterAIValidateCommandlet` | UCommandlet | `-run=TDMonsterAIValidate` 3단 검증, `-print-resolved` | 도구 | 결정(진입점, D8; 07 은 검증 본체 `TDMonsterAIValidator` 를 분리) |
| `UTDMonsterAISchemaDumpCommandlet` | UCommandlet | 리플렉션·등록표 → 스키마·입력·행동 문서 | 도구 | 결정 D9 |
| `UTDAttackTimetableExtractCommandlet` | UCommandlet | 몽타주 → 시간표 JSON(Phase 3) | 도구 | 제안(D19; 04 §6.2 와 같은 이름) |
| `UTDMonsterAIBakeConstantsCommandlet` | UCommandlet | JSON → C++ 상수표 생성(D10 되돌림, Phase 3) | 도구 | 제안(D10, 미결 4) |
| `FTDMonsterDefinitionBuilder` | 클래스 | C++ 빌더 진입점. 증분 빌드 실측이 60초 미만일 때만 병행 유지(D10) | 공용(조건부) | 제안(D10, 미결 4) |
| `FTDMonsterTickScheduler` | 구조체(상태 없는 함수 묶음) | 주기표 `PeriodTable[4][4]`(`Content/MonsterAI/PeriodTable.json` 에서 로드) 보관·`GetPeriod`·채널별 due 슬롯 수집. LOD·위상·`NextThinkStep` 의 정본은 슬롯 배열 | 공용 | 제안(D22·D23; [03 §2](03-tick-and-scale.md), 07 M3-03 `MonsterAI/TDMonsterTickScheduler.h/.cpp`) |
| `TDFlowField`(07 표기, 파일 이름) | 클래스, `MonsterAI/TDFlowField.h/.cpp` | 방 단위 플로우 필드(목표당 1장, 2Hz, 4프레임 결정적 분할 재계산). `MoveToward` 원시의 게임 구현체 | 게임(시뮬은 시나리오 옵션) | 제안(D17; [03 §4.3](03-tick-and-scale.md), 07 M3-04) |
| `TDMonsterActorPool`(07 표기, 파일 이름) | 클래스, `MonsterAI/TDMonsterActorPool.h/.cpp` | 단계 B 무액터 L3 강등·승격용 잡몹 액터 풀(400). 실측 KPI 초과 시에만 도입 | 게임 | 제안(D24; [03 §5](03-tick-and-scale.md) 단계 B, 07 M3-08) |
| `TDMonsterCrowdControl`(07 표기, 파일 이름) | 순수 함수 묶음, `MonsterAI/TDMonsterCrowdControl.h/.cpp` | CC(Crowd Control, 군중제어)·넉백 궤적 순수 함수(시작·속도·감쇠·충돌 반경). 게임·시뮬이 같은 코드 | 공용 | 제안(D15·D17; [06 §5-4](06-beyond-the-ask.md), 07 M3-13) |
| `TDMonsterAIStats` | 통계 그룹 헤더 `MonsterAI/TDMonsterAIStats.h` | `STATGROUP_TDMonsterAI`(`stat TDMonsterAI`), 채널별 사이클 스탯·슬롯 수·LOD 별 인원 | 게임(시뮬은 옵션) | 제안(D24; [03 §6.3](03-tick-and-scale.md), 07 M1-13) |

## 3. 데이터 흐름(D15)

```
[LLM/사람] --쓴다--> Content/MonsterAI/Definitions/<Id>.json
                          |  UTDMonsterDefinitionLibrary: 파싱 → extends 병합 → 등록표 해석(이름→인덱스)
                          |  → 비용순 정렬 → 정의 해시(dh) → 불변 FTDMonsterDefinition
                          v
   +------------- FTDMonsterDefinition (불변, 게임·시뮬 공유) -------------+
   |                                                                       |
[게임 월드]                                                    [시뮬 월드: FTDCombatSimSession]
ATDMonsterPawn / ATDMonsterCharacter (ITDMonsterBody)          ATDSimCombatant (ITDMonsterBody)
   |                                                                       |
   +---------> UTDMonsterThinkSubsystem 고우선 틱 함수(TG_PrePhysics) <-----+
               World->Tick 안에서 스텝당 정확히 1회. 러너는 Step 을 직접 호출하지 않는다.
               SimulationId 오름차순:
                 1) FTDNeighborGrid 갱신
                 2) 사고: LOD 별 주기(Phase[Slot] = Slot % Period)에 도달한 슬롯만
                    FTDBrainInputs 채움 → FTDUtilityScorer → FTDMonsterActionExecutor
                 3) 2D 이동 적분 → ITDMonsterBody 에 위치 적용
                 4) 공격 시간표 진행·형상 판정(Phase 0~2 는 UTDDamageDefinition 경로)
                 5) UTDDamageSubsystem::ExecuteRules (기존 GAS 경로, D18)
                 6) FTDDecisionLogWriter (사고당 1줄) → FTDCombatStateHasher (시뮬)
                          |
        [산출물] JSONL 로그 · CSV 결과 · 해시 체인 → Tools/CombatSim/*.py → 30줄 요약 → [LLM]
```

플레이어 대리 봇은 같은 두뇌 형식이므로 시뮬에서는 슬롯 하나(팀 1)로 등록되어 2) 에서 함께 평가된다. 게임에는 그 슬롯이 없다. 스텝 길이·틱 그룹·LOD 주기표는 [03 틱·규모](03-tick-and-scale.md), 세션 루프와 해시는 [04 시뮬레이터](04-combat-simulator.md) 가 다룬다.

## 4. 게임/시뮬 코드 공유 경계

| 계층 | 같은 것 | 다른 것 |
|---|---|---|
| 두뇌 | 정의·등록표·점수기·FSM·슬롯·스케줄·로그 형식 | 없음 |
| 스텝 진입 | `UTDMonsterThinkSubsystem` 틱 함수가 `World->Tick` 안에서 1회 | 게임은 실제 델타를 누적기로 스텝화(프레임당 상한 4, D22), 시뮬은 매 스텝 1/64초 고정(D27) |
| 몸 | `ITDMonsterBody` 인터페이스, 2D 이동 적분 결과 | 게임 `ATDMonsterPawn`/`ATDMonsterCharacter`(메시·애니 예산), 시뮬 `ATDSimCombatant`(메시 없음) |
| 공격 판정 | 데이터 권위(Phase 0~2 `UTDDamageDefinition`, Phase 3 `FTDAttackTimetable`) (D19) | 게임의 애님 노티파이는 표현·오라클 테스트 전용. 플레이어 사람 조작은 노티파이 스윕 유지(D20) |
| 피해·상태이상 | `UTDDamageSubsystem`·GAS(Gameplay Ability System) 경로, `TDDamageFormula::Compute`. 몬스터 ASC(Ability System Component) 는 게임·시뮬 모두 `bSuppressGameplayCues=true`, 몽타주·틱 태스크 미사용(D18) | 시뮬은 프리젠테이션 컴포넌트 생성 게이트로 Niagara·메시를 만들지 않는다(D38) |
| 난수 | `FTDCombatRandomStreams` 클래스 | 게임 시드는 세이브·시간, 시뮬은 시나리오 시드(D29) |
| 근접 탐색 | `FTDNeighborGrid` | 없음(잡몹 물리 오버랩·RVO 미사용, D17) |
| LOD | 주기표 코드 | 시뮬 기본 전원 L0, 성능 시나리오만 `virtual_camera`(D23) |
| 해시·정의 잠금 | 해시 함수 코드 | 시뮬만 매 스텝 해시, 시뮬 세션 중 정의 리로드 잠금(D11) |

이 경계에서 "게임 = 시뮬 비트 동일" 은 약속하지 않는다. 일치 정의 3단(A 비트 동일 / B 이벤트 등가 ±1스텝 / C 통계 등가)은 [04](04-combat-simulator.md) 를 따른다(D32).

---

## 5. 정의 형식(D6~D12)

### 5.1 스키마 규칙(LLM 이 외울 것 전부)

로더는 `FJsonObject` 를 직접 순회하는 자체 로더(미지 키 검사와 같은 패스)이고, 잎·부분 구조체 변환에만 `FJsonObjectConverter`(USTRUCT ↔ JSON, `FInstancedStruct` 왕복 — engine-misc-decision-tools R13, `JsonObjectConverter.cpp:265-280,909-946`) 를 쓴다. `FJsonObjectConverter` 는 키를 프로퍼티 이름(authored name) 그대로 대조하므로(`Class.cpp:2558-2562` `GetAuthoredNameForField` = `GetName`, `JsonObjectConverter.cpp:1338-1339` `JsonAttributes.Find(PropertyName)`) 프로퍼티를 JSON 키와 같은 snake_case 로 선언한다(규칙 18, [04 §9.1](04-combat-simulator.md)). 자체 로더가 필요한 이유는 미지 키 거부와 아래 잎 특례다. 규칙·특례는 아래 4개뿐이다.

| 특례 | JSON 쪽 | C++ 쪽 | 처리 |
|---|---|---|---|
| snake_case 키 | `think_hz`, `min_hold_seconds` | `think_hz`, `min_hold_seconds`(프로퍼티도 snake_case 로 선언, 규칙 18) | 변환 없음 — `FJsonObjectConverter` 가 authored name 으로 그대로 찾는다. 스키마 덤프도 프로퍼티 이름을 키로 그대로 쓴다 |
| `tune` 잎 | 숫자 또는 `{ "value", "tune" }` 객체(규칙 14) | `FTDTunableFloat` | `CustomImportCallback`(`JsonObjectConverter.h:87,239`) 으로 두 형태 모두 받는다 |
| `range` | `[최소, 최대]` 배열 | `FFloatRange` | 원소 2개 배열만 허용 |
| `enter_when.op` | `"<="`, `">="` 같은 문자열 | 열거형 `ETDCompareOp` | 문자열 → 열거형 표(UENUM 이름으로는 `<=` 를 쓸 수 없다) |

원시별 가변 `args` 는 등록표의 `FTDActionArgSchema`·입력 인자 스키마가 검사한다(§5.5).

| # | 규칙 |
|---|---|
| 1 | 파일 하나 = 종 하나. `id` 는 파일 이름과 같다. `schema` 는 정수(현재 1). `kind` 는 `monster`(기본) 또는 `player_proxy`. |
| 2 | 최상위 키는 `schema, id, kind, extends, stats, think_hz, lod_periods, inertia, select, top_n, abilities, sequences, actions, phases, persona` 뿐이다. 그 외 키는 검증 실패(미지 키 거부). |
| 3 | `stats`: `max_health, attack_power, armor, move_speed, team`(`kind: monster` 는 5개 모두 필수). `kind: player_proxy` 는 `stats` 를 생략하고 체력·공격력·팀을 시나리오 `player` 블록([04 §9.1](04-combat-simulator.md), D33)에서 받는다. `abilities.<이름>`: `spell`(`UTDDamageDefinition` 이름, 예 `DA_TDFireball`), `range`, `cooldown`, 선택 `timetable`(Phase 3). |
| 4 | `actions` 는 배열이고 원소는 `id, do, args, weight, cooldown(선택), considerations, remove(선택)`. `do` 는 등록표 원시 이름 중 하나다. Phase 1 기본 등록은 시뮬 가능 원시 7개(`MoveToward, MoveAway, MoveToBand, FaceTarget, Wait, CastAbility, PlaySequence`) + 표현 전용 원시 1개(`PlayEmote`, `bSimulatable=false`). 표현 전용 원시는 검증기 1단이 정보로 남기고 시뮬에서는 같은 길이의 `Wait` 로 치환한다. `remove: true` 인 항목은 `id` 외의 키를 가지면 1단 오류이고 필수 키 검사를 면제한다. |
| 5 | 행동 점수 = `weight × Π(고려사항 점수)`. 고려사항 하나라도 0 이면 그 행동은 0 이고 나머지 고려사항은 평가하지 않는다(싼 것부터). 비용 등급은 파일에 없고 등록표가 갖는다. 로더가 평가 순서를 자동 정렬한다(레이캐스트 계열 마지막). |
| 6 | 고려사항: `input`(등록표 이름), `args`(입력별), `curve`, `m, k, b, c`(기본 1, 1, 0, 0), `range`([최소, 최대], 생략 시 등록표 기본), `invert`(기본 false). 입력은 `x = clamp((raw − 최소) / (최대 − 최소), 0, 1)` 로 정규화되고 곡선 결과 `y` 는 0~1 로 클램프한다. `invert` 는 **곡선 출력** 반전 `y' = 1 − y` 다(입력 반전이 아니다. `Gaussian` + `invert` = "밴드 밖에서 높음", `Binary` + `invert` = "`x ≤ c` 이면 1"). |
| 7 | 곡선 7종의 식은 §5.1.1 표. 정의되지 않은 파라미터(예 `Binary` 의 `k`)는 0 이 아니면 경고. |
| 8 | 선택: 후보 중 최고점. 동률은 `actions` 배열 순서(앞이 이김). `select` 는 `max`(기본) 또는 `weighted_random`(`top_n` 필요, AI 스트림만 소비, D2). |
| 9 | 관성 `inertia`: `{ "switch_ratio": 1.15, "min_hold_seconds": 0.4 }`. 현재 행동이 `min_hold_seconds` 를 채우기 전에는 유지하고, 그 뒤에는 도전자 점수가 `현재 점수 × switch_ratio` 를 넘어야 교체한다(D1). "현재 점수" 는 이번 사고에서 다시 계산한 현재 행동의 점수(`CurrentNow`)이며 이전 사고의 값이 아니다. `CurrentNow` 가 0 이면(쿨다운 진입·페이즈에서 제거·고려사항 0) 최소 유지를 포함한 관성을 무시하고 새로 고른다. 페이즈 `on_enter` 와 경직은 관성을 무시한다. |
| 10 | `cooldown`(행동 단위, 초)은 선택 직후 시작하고 남아 있는 동안 그 행동 점수는 0 이다. 능력 쿨다운은 `abilities.<이름>.cooldown` 이고 `AbilityReady` 입력으로 읽는다. |
| 11 | `sequences.<이름>` 은 단계 배열이고 단계는 `{ "do", "args" }` 다. 단계에서 `PlaySequence` 는 금지(중첩 없음), 길이 ≤ 8. 시퀀스 중에는 사고하지 않고 단계 완료·실패·경직으로만 빠져나온다. |
| 12 | `phases` 는 배열이고 배열 순서대로 한 방향으로만 진입한다. 원소는 `id, enter_when {input, op, value}, on_enter(시퀀스 이름), stats(덮어쓰기), actions(병합)`. `enter_when` 은 사고 시점에 평가한다. |
| 13 | `extends` 는 부모 `id` 하나. 병합 규칙은 §5.11. 행동 하나를 바꾸려면 그 행동을 통째로 다시 쓴다(고려사항 단위 병합 없음). |
| 14 | 수치 잎은 숫자 또는 `{ "value": 0.9, "tune": true }` 객체다. 객체 형태는 `tune` 잎으로 등록되어 [05](05-ml-and-generative-ai.md) 의 블랙박스 튜닝 벡터가 된다(D36). 탐색 범위(min/max)는 파일에 적지 않고 스키마 덤프의 잎별 min/max 를 쓴다(05 §4). 로더는 `value` 만 쓴다. |
| 15 | `think_hz` 가 L0 사고 주기를 정한다(`Period = round(64 / think_hz)`, 하한 2 — [03](03-tick-and-scale.md)). `lod_periods` 는 `{ "think": [13, 32, 64] }` 처럼 **L1~L3 세 값만** 받아 03 의 비율 감쇠(×1/2, ×1/5, ×1/10) 대신 쓴다(D22). L0 값을 넣으면(원소 4개) 1단 오류다. `move`·`judge` 채널은 종별로 덮어쓸 수 없다(03: LOD 불변). 생략 시 03 의 비율 감쇠. 키 이름은 03 문서의 "JSON `lod` 절" 을 `lod_periods` 로 통일한다(미결 6). |
| 16 | 원시 이름·인자 스키마는 `schema/actions.md`, 입력은 `schema/inputs.md`, 전체 구조는 `schema/monster-definition.schema.json` 에 커맨드렛이 덤프한다. LLM 은 이 셋 + AGENTS.md 8줄 + 예시 1개만 읽는다(D9). |
| 17 | 시간 단위: 정의 JSON 의 시간 잎(`cooldown`, `min_hold_seconds`, `reaction_delay_seconds`, `Wait.args.seconds`)은 **초** 이고, 로더가 `Steps = FMath::RoundToInt(초 × 64)`(0 이면 0) 로 스텝 정수로 바꾼 뒤 모든 비교는 스텝 정수로 한다(D27). 정의 해시 `dh` 에는 변환 전 float 비트를 넣는다(§5.10). 시나리오 JSON([04](04-combat-simulator.md))은 처음부터 스텝 정수다. 1/64 에서의 "N+1 스텝 만료" 보정 여부는 06 문서 미결 4 를 따른다. |
| 18 | JSON 키는 snake_case 이며, `FJsonObjectConverter` 는 USTRUCT 프로퍼티의 선언 이름(authored name)과 키를 그대로 대조하므로(`JsonObjectConverter.cpp:1338-1339`) 로더 구조체(`FTDMonsterDefinition` 과 하위 구조체, 페르소나 포함)의 프로퍼티를 JSON 키와 같은 snake_case 로 선언한다(예 `float think_hz`). 엔진 명명 규칙 예외는 이 데이터 구조체에만 허용하고 커스텀 이름 변환기는 만들지 않는다([04 §9.1](04-combat-simulator.md) 과 동일 규칙). |

#### 5.1.1 곡선 표(단위 정사각형, x·y ∈ [0, 1])

| curve | 식 | 기본값(1, 1, 0, 0) 의미 | 주로 쓰는 곳 |
|---|---|---|---|
| `Constant` | `y = b` | 0 | 상시 양수 대기 행동은 `weight` 로 표현하고 고려사항을 비운다 |
| `Binary` | `y = (x > c) ? 1 : 0` | x > 0 이면 1 | `AbilityReady`, `LineOfSightToTarget` 같은 0/1 입력 |
| `Linear` | `y = m·(x − c) + b` | 항등 | 아군 수, 체력비 |
| `Quadratic` | `y = m·(x − c)^k + b` | 항등 | 낮은 체력에서 급증(`m −1, k 2, b 1`) |
| `Logistic` | `y = k / (1 + e^(−m·(x − c))) + b` | 거의 평평(경고) | 거리 문턱. `m` 은 10~20, `c` 는 문턱 위치 |
| `Logit` | `y = k·(0.5 + ln(x' / (1 − x')) / m) + b`, `x' = clamp(x, 0.001, 0.999)` | 양 끝 급변 | 극단값 강조 |
| `Gaussian` | `y = k·e^(−(x − c)² / (2·m²)) + b` | c 근처 종 모양, m = 표준편차 | 거리 밴드 유지 |

IAUS(Infinite Axis Utility System, 무한 축 유틸리티 시스템) 의 "입력 1개 + 곡선 1종 + 파라미터 4개" 형식을 그대로 따른다(web-llm-authorable-tooling 결론 7, tonogameconsultants 2025; Curvature 위키). 정확한 식은 `FTDResponseCurve::Evaluate` 가 정본이고 스키마 덤프가 이 표를 재생성한다.

### 5.2 예시 1 — 근접 잡몹 `Goblin_Melee.json`

```json
{ "schema": 1, "id": "Goblin_Melee",
  "stats": { "max_health": 120, "attack_power": 12, "armor": 5, "move_speed": 420, "team": 2 },
  "think_hz": 10, "inertia": { "switch_ratio": 1.15, "min_hold_seconds": 0.4 },
  "abilities": { "Slash": { "spell": "DA_TDGoblinSlash", "range": 160, "cooldown": 1.2 } },
  "actions": [
    { "id": "Approach", "do": "MoveToward", "args": { "target": "Player", "stop_at": 140 }, "weight": 1.0,
      "considerations": [
        { "input": "DistanceToTarget", "curve": "Logistic", "m": 12, "c": 0.15, "range": [0, 1200] } ] },
    { "id": "Slash", "do": "CastAbility", "args": { "ability": "Slash" }, "weight": 2.0,
      "considerations": [
        { "input": "AbilityReady", "args": { "ability": "Slash" }, "curve": "Binary" },
        { "input": "DistanceToTarget", "curve": "Logistic", "m": -12, "c": 0.14, "range": [0, 1200] },
        { "input": "FacingTarget", "curve": "Linear" },
        { "input": "LineOfSightToTarget", "curve": "Binary" } ] },
    { "id": "Flee", "do": "MoveAway", "args": { "target": "Player", "speed_scale": 1.1 }, "weight": 3.0,
      "considerations": [
        { "input": "SelfHealthRatio", "curve": "Quadratic", "m": -1, "k": 2, "b": 1 },
        { "input": "AllyCountNearby", "args": { "radius": 600 }, "curve": "Linear", "m": -1, "b": 1, "range": [0, 4] } ] },
    { "id": "Idle", "do": "Wait", "weight": 0.05, "considerations": [] }
  ] }
```

읽는 법: 180cm 보다 멀면 접근, 사거리 안이고 준비됐고 마주 보면 베기(가중치 2 로 접근보다 우선), 체력이 낮을수록·아군이 적을수록 도주 점수가 오른다(가중치 3. 아군 4명이면 `AllyCountNearby` 항이 0 이라 도주하지 않고, 체력 50%·아군 2명이면 `3 × 0.75 × 0.5 ≈ 1.13` 으로 베기 2.0 보다 낮다), 아무것도 안 되면 대기(항상 0.05 → "행동 없음" 상태가 없다). `DA_TDGoblinSlash` 는 Phase 1 에서 `TDDamageExamples::CreateExamples` 에 추가하는 Area 정의이며 `ActivationDelay` 가 선딜이다(D19). `LineOfSightToTarget` 은 Phase 1 스텁(항상 1) 이고 Phase 3 에서 2D 장애물 선분 검사로 바뀐다([03](03-tick-and-scale.md)).

### 5.3 예시 2 — 원거리 `Archer.json`(extends)

```json
{ "schema": 1, "id": "Archer", "extends": "Goblin_Melee",
  "stats": { "max_health": 80, "attack_power": 16, "move_speed": 380 },
  "think_hz": 8, "inertia": { "switch_ratio": 1.2, "min_hold_seconds": 0.6 },
  "abilities": { "Shoot": { "spell": "DA_TDIceShard", "range": 900, "cooldown": 1.6 } },
  "actions": [
    { "id": "Approach", "remove": true },
    { "id": "Slash", "remove": true },
    { "id": "KeepDistance", "do": "MoveToBand", "args": { "target": "Player", "min": 500, "max": 800 }, "weight": 1.0,
      "considerations": [
        { "input": "DistanceToTarget", "curve": "Gaussian", "m": 0.12, "c": 0.54, "range": [0, 1200], "invert": true } ] },
    { "id": "Shoot", "do": "CastAbility", "args": { "ability": "Shoot", "lead_target": true }, "weight": 2.0,
      "considerations": [
        { "input": "AbilityReady", "args": { "ability": "Shoot" }, "curve": "Binary" },
        { "input": "DistanceToTarget", "curve": "Gaussian", "m": 0.2, "c": 0.54, "range": [0, 1200] },
        { "input": "LineOfSightToTarget", "curve": "Binary" } ] },
    { "id": "Backpedal", "do": "MoveAway", "args": { "target": "Player" }, "weight": 2.5,
      "considerations": [
        { "input": "DistanceToTarget", "curve": "Logistic", "m": -14, "c": 0.3, "range": [0, 1200] },
        { "input": "TargetIsAttacking", "curve": "Binary" } ] }
  ] }
```

`KeepDistance` 의 `Gaussian` + `invert` 는 규칙 6 의 출력 반전이라 밴드 중심(648cm)에서 0, 1200cm 에서 1, 300cm 에서 약 0.95 다(1200cm 에서 `Shoot` 은 `2 × 0.07 ≈ 0.14` 라 `KeepDistance` 가 이긴다). `Flee`·`Idle` 은 상속으로 남고 `abilities.Slash` 도 남는다(참조하는 행동이 없으므로 검증기 2단이 "미사용 능력" 경고). `DA_TDIceShard` 는 기존 투사체 정의다(`Source/TDGame/Combat/Damage/TDDamageExamples.cpp:79`). 시뮬레이터는 `.uasset` 을 열지 않고 C++ 원본을 메모리 생성한다(project-current-combat-code 결론 10).

### 5.4 예시 3 — 보스 페이즈 `Ogre_Boss.json`

```json
{ "schema": 1, "id": "Ogre_Boss",
  "stats": { "max_health": 4000, "attack_power": 40, "armor": 30, "move_speed": 340, "team": 2 },
  "think_hz": 12, "inertia": { "switch_ratio": 1.1, "min_hold_seconds": 0.8 },
  "abilities": {
    "Smash": { "spell": "DA_TDOgreSmash", "range": 220, "cooldown": 2.5 },
    "Shockwave": { "spell": "DA_TDShockwave", "range": 600, "cooldown": 6.0 },
    "Meteor": { "spell": "DA_TDMeteor", "range": 1400, "cooldown": 9.0 } },
  "sequences": {
    "Roar_Enrage": [ { "do": "Wait", "args": { "seconds": 0.6 } } ],
    "Smash_Combo": [ { "do": "CastAbility", "args": { "ability": "Smash" } }, { "do": "Wait", "args": { "seconds": 0.25 } },
                     { "do": "FaceTarget" }, { "do": "CastAbility", "args": { "ability": "Shockwave" } } ] },
  "actions": [
    { "id": "Approach", "do": "MoveToward", "args": { "target": "Player", "stop_at": 200 }, "weight": 1.0,
      "considerations": [ { "input": "DistanceToTarget", "curve": "Logistic", "m": 12, "c": 0.14, "range": [0, 1600] } ] },
    { "id": "Smash", "do": "CastAbility", "args": { "ability": "Smash" }, "weight": 2.0,
      "considerations": [ { "input": "AbilityReady", "args": { "ability": "Smash" }, "curve": "Binary" },
                          { "input": "DistanceToTarget", "curve": "Logistic", "m": -14, "c": 0.15, "range": [0, 1600] } ] },
    { "id": "Meteor", "do": "CastAbility", "args": { "ability": "Meteor" }, "weight": { "value": 1.5, "tune": true },
      "considerations": [ { "input": "AbilityReady", "args": { "ability": "Meteor" }, "curve": "Binary" },
                          { "input": "DistanceToTarget", "curve": "Linear", "range": [200, 1400] } ] },
    { "id": "Idle", "do": "Wait", "weight": 0.05, "considerations": [] } ],
  "phases": [
    { "id": "Enraged", "enter_when": { "input": "SelfHealthRatio", "op": "<=", "value": 0.5 },
      "on_enter": "Roar_Enrage", "stats": { "attack_power": 56, "move_speed": 400 },
      "actions": [
        { "id": "Smash", "remove": true },
        { "id": "SmashCombo", "do": "PlaySequence", "args": { "sequence": "Smash_Combo" }, "weight": 3.0,
          "considerations": [ { "input": "AbilityReady", "args": { "ability": "Smash" }, "curve": "Binary" },
                              { "input": "DistanceToTarget", "curve": "Logistic", "m": -14, "c": 0.15, "range": [0, 1600] } ] } ] } ] }
```

페이즈 진입 시 `on_enter` 시퀀스가 관성을 무시하고 강제 실행되고 `stats` 는 즉시 반영된다. "2페이즈 보스 + 콤보" 가 계획기 없이 표현된다. 3페이즈 이상·무리 역할 배정이 실제 콘텐츠로 들어오면 자체 HTN(Hierarchical Task Network, 계층적 태스크 네트워크)을 Phase 4 에 추가한다(D3, [01](01-ai-model-decision.md)). `Meteor.weight` 는 `tune` 잎 예시다(규칙 14).

### 5.5 C++ 등록표 예시(LLM 이 드물게 만지는 층, D7)

```cpp
struct FTDBrainInputs
{
	int32 Step = 0;
	int32 SimulationId = -1;
	float DistanceToTarget = 0.f;
	float FacingDot = 0.f;
	float SelfHealthRatio = 1.f;
	float TargetHealthRatio = 1.f;
	bool bIsTargetAttacking = false;
	bool bHasRingSlot = false;
	bool bHasLineOfSight = true;
	uint8 ReadyAbilityMask = 0;
	uint8 NeighborCount = 0;
	FTDNeighborEntry Neighbors[8];
	// 대리 봇(kind: player_proxy) 슬롯 전용. 몬스터 슬롯은 기본값 유지.
	bool bDodgeRoll = false;        // 스냅샷 채우기 단계에서 PlayerProxy 스트림 1회 소비 결과(§6)
	bool bIncomingTelegraph = false; // 대상의 공격 선딜 진행 중
	uint8 PotionCount = 0;
};
static_assert(std::is_trivially_copyable_v<FTDBrainInputs>, "FTDBrainInputs must stay a plain snapshot");
// 주의: 위 단언은 원시 포인터 멤버(AActor*, UWorld*)를 막지 못한다(포인터도 trivially copyable). §5.5 표 '입력 함수 형' 참고.

using FTDBrainInputFunction = float (*)(const FTDBrainInputs&, const FTDInputArgs&);

void FTDBrainInputRegistry::RegisterBuiltins()
{
	Add(TEXT("DistanceToTarget"), [](const FTDBrainInputs& In, const FTDInputArgs&) { return In.DistanceToTarget; }, ETDInputCost::Cheap, FFloatRange(0.f, 1200.f));
	Add(TEXT("SelfHealthRatio"), [](const FTDBrainInputs& In, const FTDInputArgs&) { return In.SelfHealthRatio; }, ETDInputCost::Cheap, FFloatRange(0.f, 1.f));
	Add(TEXT("AbilityReady"), [](const FTDBrainInputs& In, const FTDInputArgs& Args) { return In.IsAbilityReady(Args.AbilityIndex) ? 1.f : 0.f; }, ETDInputCost::Cheap, FFloatRange(0.f, 1.f));
	Add(TEXT("AllyCountNearby"), [](const FTDBrainInputs& In, const FTDInputArgs& Args) { return In.CountAlliesWithin(Args.Radius); }, ETDInputCost::Grid, FFloatRange(0.f, 8.f));
	Add(TEXT("LineOfSightToTarget"), [](const FTDBrainInputs& In, const FTDInputArgs&) { return In.bHasLineOfSight ? 1.f : 0.f; }, ETDInputCost::Trace, FFloatRange(0.f, 1.f));
}

void FTDMonsterActionRegistry::RegisterBuiltins()
{
	Add(TEXT("MoveToward"), ETDMonsterFsmState::Move, FTDActionArgSchema::MoveToward(), true);
	Add(TEXT("MoveAway"), ETDMonsterFsmState::Move, FTDActionArgSchema::MoveAway(), true);
	Add(TEXT("MoveToBand"), ETDMonsterFsmState::Move, FTDActionArgSchema::MoveToBand(), true);
	Add(TEXT("FaceTarget"), ETDMonsterFsmState::Idle, FTDActionArgSchema::None(), true);
	Add(TEXT("Wait"), ETDMonsterFsmState::Idle, FTDActionArgSchema::Wait(), true);
	Add(TEXT("CastAbility"), ETDMonsterFsmState::Cast, FTDActionArgSchema::CastAbility(), true);
	Add(TEXT("PlaySequence"), ETDMonsterFsmState::Sequence, FTDActionArgSchema::PlaySequence(), true);
	Add(TEXT("PlayEmote"), ETDMonsterFsmState::Idle, FTDActionArgSchema::Wait(), false);
}
```

| 요소 | 규칙 |
|---|---|
| 입력 함수 형 | 캡처 없는 함수 포인터 `FTDBrainInputFunction = float(*)(const FTDBrainInputs&, const FTDInputArgs&)` 만 등록된다. 정적으로 막히는 것은 이 시그니처뿐이다: 인자에 `UWorld`·`AActor` 가 없고, 캡처가 있는 람다는 함수 포인터로 변환되지 않아 컴파일이 실패한다. 이것이 D7 "액터·월드 접근을 등록 시 정적 단언으로 거부" 의 실제 범위다(B §3.5 접목). `FTDBrainInputs` 에 UObject·월드 포인터 멤버를 두지 않는 것은 헤더 규칙(`TDBrainInputs.h` 는 UObject 헤더를 포함하지 않는다 + 코드 리뷰)이며 컴파일러가 보장하지 않는다. `is_trivially_copyable` 단언은 원시 포인터 멤버를 막지 못한다(C++ 표준: 포인터는 trivially copyable). 07 문서 M1-02 완료 조건("액터/월드 포인터 등록은 컴파일 실패")은 이 범위로 읽는다. |
| 비용 등급 | `Cheap < Grid < Trace`. 로더가 고려사항을 이 순서로 안정 정렬하고, `Trace` 는 항상 마지막이다(web-ai-architecture-comparison 결론 5). |
| Trace 입력의 실제 비용 | 스냅샷 채우기 단계에서 "그 종의 정의가 Trace 입력을 쓸 때만" 계산한다(정의별 요구 마스크). Phase 1 은 스텁. |
| `bSimulatable` | `false` 인 원시(예 `PlayEmote`)는 시뮬에서 같은 길이의 `Wait` 로 치환되고 검증기 1단이 "표현 전용 원시 사용" 을 정보로 남긴다. |
| 인자 스키마 | `FTDActionArgSchema` 가 필수·선택 인자와 형을 갖고 검증기 1단과 `actions.md` 덤프가 같은 표를 쓴다. |
| 새 입력·원시 추가 | C++ 한 줄 등록 + 컴파일 1회. 종당이 아니라 메커닉당 한 번이다. 추가 후 `TDMonsterAISchemaDump` 를 다시 돌린다. |

### 5.6 점수기 결정성 규칙 코드(D1)

```cpp
FTDBrainDecision FTDUtilityScorer::Score(const FTDMonsterDefinition& Def, const FTDMonsterBrainSlotView& Slot, const FTDBrainInputs& In, FRandomStream& AiStream, FTDDecisionTrace* Trace)
{
	TArray<FTDScoredAction, TInlineAllocator<16>> Scored;
	for (int32 Index = 0; Index < Def.ResolvedActions.Num(); ++Index)
	{
		const FTDResolvedAction& Action = Def.ResolvedActions[Index];
		if (!Def.IsActionInPhase(Index, Slot.PhaseIndex) || Slot.IsActionOnCooldown(Index)) { continue; }
		float Score = Action.Weight;
		for (const FTDResolvedConsideration& Consideration : Action.ConsiderationsByCost)
		{
			const float Raw = Consideration.Function(In, Consideration.Args);
			const float Normalized = Consideration.Range.Normalize(Raw);            // 규칙 6: 입력 정규화 0~1
			float Curve = FTDResponseCurve::Evaluate(Consideration.Curve, Normalized); // 0~1 클램프 포함
			if (Consideration.bInvert) { Curve = 1.f - Curve; }                        // 규칙 6: invert 는 출력 반전
			Score *= Curve;
			if (Trace) { Trace->Record(Index, Consideration, Raw, Score); }
			if (Score <= 0.f) { break; }
		}
		if (Score <= 0.f) { continue; }
		Scored.Add({ Index, Score });
	}
	if (Scored.IsEmpty()) { return FTDBrainDecision::MakeIdle(); }
	// 규칙 9: 현재 행동의 "이번 사고" 점수. 후보에 없으면(쿨다운·페이즈 제거·고려사항 0) 0 → 관성 무시
	float CurrentNow = 0.f;
	for (const FTDScoredAction& S : Scored) { if (S.Index == Slot.CurrentActionIndex) { CurrentNow = S.Score; break; } }
	Scored.StableSort([](const FTDScoredAction& A, const FTDScoredAction& B) { return A.Score > B.Score; });
	const bool bIsHoldingCurrent = Slot.HoldRemainingSteps > 0;
	const bool bShouldKeepByRatio = Scored[0].Index != Slot.CurrentActionIndex && Scored[0].Score < CurrentNow * Def.InertiaSwitchRatio;
	if (Slot.CurrentActionIndex >= 0 && CurrentNow > 0.f && (bIsHoldingCurrent || bShouldKeepByRatio)) { return FTDBrainDecision::MakeKeep(CurrentNow); }
	if (Def.SelectMode == ETDSelectMode::Max) { return FTDBrainDecision::MakePick(Scored[0].Index, Scored[0].Score); }
	return PickWeightedTopN(Scored, Def.TopN, AiStream);
}
```

| 규칙 | 코드 위치 | 근거 |
|---|---|---|
| 정의 순서 동률 | `StableSort` + 배열 순서 | D1 |
| 비용순 조기 종료 | `ConsiderationsByCost` 는 로드 시 정렬, `Score <= 0` 에서 `break` | D1, D7 |
| 관성 | `HoldRemainingSteps`(최소 유지) 와 `InertiaSwitchRatio`. 비교 대상은 이번 사고의 `CurrentNow` 이고 0 이면 관성 무시(규칙 9). `MakeKeep(CurrentNow)` 가 슬롯의 현재 점수를 갱신한다 | D1 |
| 난수 | `weighted_random` 일 때만 `AiStream` 소비. 스트림은 `Hash(Master, 1000 + SimulationId)`, 사고당 소비 횟수(draws)를 로그(저장소는 [04 §3.1](04-combat-simulator.md) `FTDCombatRandomStreams::BrainStreams`, [03 §8.2](03-tick-and-scale.md) 단일 저장 규칙 — `AiStream` 인자는 그 배열의 `SimulationId` 원소다) | D2, D29 |
| 부동소수 | 연산 순서 고정, 같은 빌드에서 비트 동일. /fp:fast·FMA 주의는 [04](04-combat-simulator.md) | web-balance-simulation-tools 결론 2 |
| 월드 비접근 | 인자에 `UWorld`·`AActor` 없음 | D7 |

### 5.7 검증기 3단(D8)

진입점: `UnrealEditor-Cmd.exe TDGame.uproject -run=TDMonsterAIValidate [-only=<Id>] [-print-resolved] -nullrhi -unattended` 와 자동화 테스트 `TDGame.MonsterAI.Validate`. 커맨드렛 종료 코드 ≠ 0 이 실패다.

| 단 | 검사 | 실패 메시지 예 |
|---|---|---|
| 1 스키마·참조 | 최상위·행동·고려사항의 미지 키 거부, 필수 키 존재, 형 일치, `extends` 순환·깊이 ≤ 3, 행동 id 중복, `input`·`do`·`ability`·`sequence`·`spell`·`timetable` 이 등록표·파일에 존재, 인자 스키마 일치, 곡선 이름·파라미터 범위, 이름 유사도 힌트(편집 거리 ≤ 2) | `Archer.json actions[3].considerations[1].input "DistToTarget": 등록되지 않음. 후보: DistanceToTarget` |
| 2 정적 규칙 | 도달 불가 행동(모든 페이즈에서 제거되거나 고려사항이 항상 0), 항상 0 인 고려사항(`Binary` 에 `c ≥ 1`, `range` 폭 0), 페이즈 진입 조건 단조(체력비 내림차순, 순환 없음), 시퀀스 길이 ≤ 8·중첩 금지, 시퀀스가 참조하는 능력이 그 페이즈에 존재, 상시 양수 행동 존재, `Logistic m < 4` 경고, `Quadratic` 은 `k` 가 정수가 아니면 `c ≤ 0` 강제(아니면 `x < c` 에서 NaN — 오류), `Logit m < 2` 경고(기본 m=1 은 양 끝이 항상 클램프), `Trace` 입력이 첫 순서면 자동 정렬 후 정보, `min_hold_seconds < 1/think_hz` 경고, `range > CastRange` 경고, 미사용 능력 경고 | `Ogre_Boss.json phases[1]: 진입 조건 0.5 가 이전 페이즈 0.3 보다 큼(한 방향 규칙 위반)` |
| 3 생존 시뮬 | 헤드리스 5초(320 스텝) × 시드 3개, 플레이어 규칙 봇 상대: 첫 공격 < 3초, 행동 교체율 < 5회/초, 대기 점유 < 60%, 같은 시드 2회 실행 해시 일치, "2초 내 사고 0회" 감시(스텝당 사고 횟수로 이중 스텝·미스케줄 검출). `kind: player_proxy` 는 상대를 `Goblin_Melee` 3마리로 바꿔 같은 지표를 잰다 | `Goblin_Melee: 5초 동안 공격 0회 (Slash 최고점 0.00 — AbilityReady 항상 0: DA_TDGoblinSlash 없음)` |

`FJsonObjectConverter` 의 `bStrictMode` 는 "누락 필드" 만 오류로 만들고 미지 키는 무시하므로(`JsonObjectConverter.cpp:1341-1354`, 구조체 프로퍼티를 기준으로 순회) 1단의 미지 키 거부는 §5.1 의 자체 로더 패스가 JSON 객체 키를 키 변환표(snake_case → 프로퍼티 이름)와 직접 대조해 구현한다. 선택 필드에 기본값이 있어야 하므로 `bStrictMode` 는 켜지 않는다.

### 5.8 스키마 덤프(D9)

`-run=TDMonsterAISchemaDump -out=Docs/MonsterAI_CombatSim/schema/` 가 세 파일을 재생성한다.

| 파일 | 원천 | 내용 | 토큰 추정 |
|---|---|---|---|
| `monster-definition.schema.json` | `FTDMonsterDefinition` USTRUCT 리플렉션 | JSON Schema draft 2020-12 스타일: 키·형·기본값·enum(곡선·원시·op) | 약 1,500 |
| `inputs.md` | `FTDBrainInputRegistry` | 이름 · 인자 · 비용 등급 · 기본 range · 한 줄 의미 | 약 600 |
| `actions.md` | `FTDMonsterActionRegistry` | 이름 · FSM 상태 · 인자 표 · `bSimulatable` | 약 500 |

LLM 이 읽는 총량은 AGENTS.md 8줄(약 200) + 세 파일(약 2,600) + 예시 1개(약 700) ≈ 3,500 토큰(추정)이다. 등록표가 바뀌면 덤프가 바뀌므로 "문서가 코드보다 늦는" 일이 없다. 자동화 테스트 `TDGame.MonsterAI.SchemaUpToDate` 가 덤프 결과와 커밋된 파일의 차이를 검사한다.

### 5.9 핫리로드 범위와 시뮬 중 잠금(D11)

| 상황 | 동작 |
|---|---|
| 게임·에디터 콘솔 `TD.MonsterAI.Reload` | `UTDMonsterDefinitionLibrary::ReloadAll()` 이 새 정의 집합을 만들어 1·2단 검증을 통과하면 포인터를 교체한다. 실패하면 이전 정의를 유지하고 오류를 로그한다. |
| 살아 있는 슬롯 | 다음 사고에서 새 정의를 잡는다. 현재 행동은 id 기준으로 재매핑하고 없으면 `Idle`. 진행 중 시퀀스는 끝까지 간다. 페이즈는 재평가한다. |
| 시뮬 세션 중 | `FTDCombatSimSession` 이 시작 시 `LockDefinitions()` 를 걸고 끝날 때 푼다. 잠금 중 리로드 요청은 거부되고 로그된다. 결과 파일에는 세션 시작 시점의 정의 스냅샷(JSON + 해석된 등록표)이 함께 저장된다(D33). |
| 에디터 파일 감시 | `TDGameEditor` 가 생긴 뒤 `IDirectoryWatcher` 로 저장 즉시 리로드. 그 전에는 콘솔 명령만(D13). |
| 패키지 빌드 | JSON 을 비에셋 디렉터리로 스테이징한다(추정: 프로젝트 설정 'Additional Non-Asset Directories to Package' 에 `MonsterAI` 추가. 미확인 — Phase 3 패키지 빌드에서 확인. 로더·커맨드렛·시뮬은 `FPaths::ProjectContentDir()` 기준 상대 경로를 읽는 것으로 통일한다). `UTDMonsterDefinitionAsset`(DataAsset) 굽기는 Phase 3 선택지이며 JSON 이 계속 정본이다. |

### 5.10 정의 해시(D11)

| 항목 | 규칙 |
|---|---|
| 대상 | extends 병합·등록표 해석이 끝난 `FTDMonsterDefinition`(파일 바이트가 아님). 공백·키 순서 변화는 해시를 바꾸지 않고, 수치·이름·순서 변화는 바꾼다. |
| 수치 | `float` 는 `FMath::AsUInt(Value)` 의 32비트 비트 패턴으로, 정수는 그대로, 문자열은 UTF-8 바이트로 FNV-1a 64비트에 넣는다. JSON 문자열 → double → float 변환이 비트 단위로 보존되는지는 미확인이므로(engine-misc-decision-tools Q4; `FJsonValueNumber` 는 `double` 저장, `JsonValue.h:212`) "같은 파서·같은 빌드에서 같은 float" 만 전제하고 결과 비교는 해시로 한다. |
| 순회 순서 | 메타 → stats → abilities(이름 정렬) → actions(배열 순서) → 고려사항(정렬 후 순서) → sequences(이름 정렬) → phases(배열 순서). 등록표 인덱스가 아니라 이름을 넣어 등록표 재배치에 흔들리지 않게 한다. |
| 표기 | `dh=<16 hex>`. 결정 로그·결과 CSV·골든 해시·`-print-resolved` 출력에 빌드 해시와 함께 찍는다([04](04-combat-simulator.md)). |
| 게이트 | 골든 해시 파일의 `dh` 가 현재 정의와 다르면 결정론 게이트는 "정의 변경" 으로 분류해 실패 원인을 분리한다. |

### 5.11 extends 병합과 `-print-resolved`

| 키 | 병합 규칙 |
|---|---|
| 스칼라(`think_hz`, `inertia`, `select`, `top_n`, `lod_periods`) | 자식이 있으면 덮어쓰기 |
| `stats`, `abilities`, `sequences` | 키 단위 덮어쓰기(부모 키는 유지) |
| `actions` | id 기준. 자식에 같은 id 가 있으면 통째로 교체, `remove: true` 면 삭제, 새 id 는 부모 순서 뒤에 자식 순서로 추가. 결과 순서가 동률 규칙이다. |
| `phases` | 자식이 `phases` 를 쓰면 그 배열이 전체를 대체, 아니면 부모 것 유지 |
| `persona` | 키 단위 덮어쓰기 |
| 깊이·순환 | 부모 하나, 깊이 ≤ 3, 순환은 1단 실패 |

`-print-resolved` 는 병합 결과를 JSON 으로 stdout 에 쓰고, 각 고려사항에 `"_cost": "Trace"`, `"_input_index": 12` 같은 밑줄 키로 해석 결과를 덧붙이며 끝에 `dh` 를 찍는다. LLM 이 "실제로 적용되는 표" 를 머리로 합치지 않아도 되게 하는 장치다(A §11.5 의 약점 보완).

### 5.12 되돌림 커맨드렛(D10)

| 항목 | 내용 |
|---|---|
| 이름·시점 | `UTDMonsterAIBakeConstantsCommandlet`, `-run=TDMonsterAIBakeConstants`. Phase 3 안에 만든다. |
| 산출물 | `Source/TDGame/MonsterAI/Generated/TDMonsterDefinitions.gen.cpp` — 병합·해석이 끝난 정의를 정적 초기화 코드(`FTDMonsterDefinitionBuilder` 호출 열)로 내보낸다. `constexpr` 표는 쓰지 않는다(빌더 호출은 상수식이 아니다). 로더 경로와 같은 `FTDMonsterDefinition` 구조체를 만든다. |
| 전환 조건 | (a) 검증기가 못 잡는 런타임 오류가 반복되거나 (b) 곡선 해석 비용이 300마리 기준 프레임당 1ms(실측) 를 넘을 때. 전환 뒤에도 JSON 이 정본이고 파일 형식은 바뀌지 않는다. |
| 사전 실측 | Phase 1 시작 전 "종 하나 추가 시 증분 빌드 시간" 을 재서 07 문서에 기록한다. 60초 미만이면 `FTDMonsterDefinitionBuilder` 를 병행 진입점으로 유지한다. |
| 검증 | 구운 상수표의 `dh` 가 JSON 로더의 `dh` 와 같아야 한다(자동화 테스트 `TDGame.MonsterAI.BakeMatchesJson`). |

---

## 6. 플레이어 대리 페르소나 JSON(D12)

같은 스키마에 `kind: "player_proxy"` 와 `persona` 블록이 추가된다. 검증기·로그·분석 스크립트를 몬스터와 공유한다.

```json
{ "schema": 1, "id": "Persona_Careful", "kind": "player_proxy",
  "think_hz": 16, "inertia": { "switch_ratio": 1.1, "min_hold_seconds": 0.2 },
  "persona": { "reaction_delay_seconds": 0.25, "potion_health_threshold": 0.35, "dodge_probability": 0.6, "preferred_range": 700 },
  "abilities": {
    "Primary": { "spell": "DA_TDFireball", "range": 1200, "cooldown": 0.8 },
    "Potion": { "spell": "DA_TDHealthPotion", "range": 0, "cooldown": 8.0 } },
  "actions": [
    { "id": "Attack", "do": "CastAbility", "args": { "ability": "Primary" }, "weight": 2.0,
      "considerations": [
        { "input": "AbilityReady", "args": { "ability": "Primary" }, "curve": "Binary" },
        { "input": "DistanceToTarget", "curve": "Logistic", "m": -12, "c": 0.8, "range": [0, 1400] } ] },
    { "id": "Kite", "do": "MoveToBand", "args": { "target": "NearestEnemy", "min": 500, "max": 900 }, "weight": 1.5,
      "considerations": [ { "input": "DistanceToTarget", "curve": "Gaussian", "m": 0.15, "c": 0.5, "range": [0, 1400], "invert": true } ] },
    { "id": "Dodge", "do": "MoveAway", "args": { "target": "NearestEnemy", "speed_scale": 1.5 }, "weight": 4.0, "cooldown": 1.0,
      "considerations": [ { "input": "IncomingAttackTelegraph", "curve": "Binary" }, { "input": "PersonaDodgeRoll", "curve": "Binary" } ] },
    { "id": "DrinkPotion", "do": "CastAbility", "args": { "ability": "Potion" }, "weight": 5.0,
      "considerations": [
        { "input": "AbilityReady", "args": { "ability": "Potion" }, "curve": "Binary" },
        { "input": "SelfHealthRatio", "curve": "Binary", "invert": true, "c": 0.35 },
        { "input": "PotionCount", "curve": "Binary" } ] },
    { "id": "Idle", "do": "Wait", "weight": 0.05, "considerations": [] }
  ] }
```

| 페르소나 파라미터 | 소비 위치 |
|---|---|
| `reaction_delay_seconds` | 스냅샷을 `N = FMath::RoundToInt(초 × 64)` 스텝(규칙 17) 지연된 것으로 채운다(대리 봇 슬롯만). 시나리오 `player.persona.override` 는 스텝 정수 `reaction_delay_steps` 로 덮어쓴다([04 §9.1](04-combat-simulator.md)) |
| `potion_health_threshold` | `SelfHealthRatio` 고려사항(`Binary` + `invert`, 규칙 6 출력 반전이라 `x ≤ c` 에서 1)의 `c` 를 `threshold` 로 치환하는 편의값. 예시는 명시값 0.35 를 그대로 둔 형태 |
| `dodge_probability` | 스냅샷 채우기 단계에서 대리 봇 슬롯만 PlayerProxy 스트림을 **정확히 1회** 소비해 `FTDBrainInputs::bDodgeRoll` 을 채운다(고려사항 평가 여부·조기 종료와 무관하게 사고당 1 draw → 결정 로그 `draws` 가 일정). `PersonaDodgeRoll` 입력 함수는 이 값을 읽기만 한다(D7 순수 입력 함수) |
| `preferred_range` | `Kite` 의 밴드 기본값 |
| 물약·장비 → GE 목록, 시나리오 배치 | [04 입력 스키마](04-combat-simulator.md)(D33) |

`DA_TDHealthPotion` 은 Phase 2 에서 즉시 Health 효과 정의로 추가한다(project-current-combat-code 결론 9: 물약은 Instant/Periodic Health 효과로 표현). 대리 봇의 행동 복제 학습판은 [05](05-ml-and-generative-ai.md)(D36) 가 다루며 규칙 페르소나가 그 시연 데이터 원천이다.

---

## 7. 생성형 AI 작업 절차(D37)

### 7.1 AGENTS.md 에 추가할 8줄 초안

```
## 13. 몬스터 AI 정의와 전투 시뮬레이터
- 몬스터 한 종 = Content/MonsterAI/Definitions/<Id>.json 하나. 형식은 Docs/MonsterAI_CombatSim/schema/ 의 세 파일(스키마·inputs·actions)과 Goblin_Melee.json 예시만 읽고 작성한다. 스키마 문서는 손으로 고치지 않는다(커맨드렛 산출물).
- 새 입력 함수·행동 원시가 필요할 때만 C++(Source/TDGame/MonsterAI/ 등록표)을 고치고 컴파일한 뒤 -run=TDMonsterAISchemaDump 를 다시 돌린다.
- 검증: UnrealEditor-Cmd.exe TDGame.uproject -run=TDMonsterAIValidate -only=<Id> -nullrhi -unattended. 종료 코드 0 이 아니면 커밋하지 않는다.
- 평가: python Tools/CombatSim/run_batch.py Content/CombatSim/Scenarios/Duel_<Id>.json --seed-base 0 --count 200 --procs 4 → summarize_batch.py 의 30줄 요약을 읽고 수치를 고친다. 수정 뒤 검증부터 다시 한다.
- 결정론 게이트 TDGame.CombatSim.* 와 TDGame.MonsterAI.NoGlobalRandom 이 통과해야 한다. FMath::FRand/Rand/SRand 를 Combat·MonsterAI·CombatSim 폴더에서 쓰지 않는다.
- 결과 파일과 로그의 dh(정의 해시)·빌드 해시를 보고에 적는다. 시뮬 세션 중에는 정의를 리로드하지 않는다.
- 완료 조건: 검증 통과 + 승률 목표 밴드 안 + 게이트 통과 + JSON 과 결과 요약 md 커밋.
```

### 7.2 종 하나 추가 10분 루프의 단계별 토큰 추정

시간은 추정이며 Phase 1 완료 후 실측으로 갱신한다.

| 단계 | 명령·행동 | 입력 토큰(추정) | 출력 토큰(추정) | 시간(추정) |
|---|---|---|---|---|
| 1 읽기 | AGENTS.md 8줄 + schema 세 파일 + 예시 1개 | 3,500 | 0 | 10초 |
| 2 작성 | `<Id>.json` 한 파일 | 0 | 700 | 30초 |
| 3 검증 | `-run=TDMonsterAIValidate -only=<Id>`(3단 포함, 컴파일 없음) | 300(출력 표) | 50 | 20~40초(커맨드렛 기동 포함) |
| 4 평가 | `run_batch.py Content/CombatSim/Scenarios/Duel_<Id>.json --seed-base 0 --count 200 --procs 4`([04 §12](04-combat-simulator.md), 프로세스 팬아웃 4개) | 100 | 50 | 60~120초 |
| 5 요약 | `summarize_batch.py` 30줄 + `analyze_decisions.py` 행동 점유·0 원인 | 1,300 | 0 | 5초 |
| 6 수정 | 수치 편집 → 3 으로 | 0 | 400 | 30초 |
| 합계(1회) | | 약 5,200 | 약 1,200 | 약 3~4분 |
| 합계(2회 반복) | | 약 7,000 | 약 1,700 | 약 6~8분 |

컴파일은 0회다. 새 원시가 필요한 경우만 C++ 컴파일이 붙고 그때는 10분 예산 밖으로 분류한다(D37). 배치 규모 100~200 시드는 승률 표준오차 ±3.5~5%p 수준의 개발 중 스모크이며, 확정은 [04](04-combat-simulator.md) 의 n=1,000 이상 절차를 따른다(web-balance-simulation-tools 표: n=400 ±2.5%p, 1,000 ±1.6%p).

MCP 툴셋(`UToolsetDefinition`, `meta=(AICallable)`, engine-misc-decision-tools R8)은 위 CLI 와 같은 세션 코드를 부르는 보조 채널이며 에디터가 떠 있어야 한다. 필수 경로가 아니다(D37).

---

## 8. UKGame 과의 차이

`Docs/UKGame/03-ai-npc.md` 의 절 이름을 그대로 인용한다. UKGame 은 과거 설계이며 복제 대상이 아니다.

| UKGame 절(03-ai-npc.md) | UKGame 방식 | TDGame | 왜 다른가 |
|---|---|---|---|
| AI 캐릭터 기반 클래스 | BT·StateTree·HTN·스테이트 머신 컴포넌트를 한 컨트롤러(`UKAIController`)에 통합 | 모델 하나(유틸리티 + 5상태 FSM), 컨트롤러 없음, 정의는 JSON 1파일 | 모델이 넷이면 스키마도 넷이고 "어느 층이 결정했나" 를 로그로 설명하기 어렵다(D1·D4). BT 는 텍스트 직렬화기가 없고 서비스가 기본 난수를 쓴다(engine-behaviortree-tick 결론 5·7) |
| NPC 업데이트 부하 분산(LOD 틱) | `UKNPCUpdateManager` 가 액터 틱을 모아 나눠 부른다 | 상태 정본을 서브시스템 SoA 배열로 옮기고 틱 함수 하나가 순회. 위상은 `Slot % Period` | 액터가 정본이면 순서·난수·해시를 명세할 수 없다(D14·D15·D22) |
| 몬스터 전투 행동(비헤이비어 트리 코어)·전투 판단(데코레이터/서비스) | BT 태스크·데코레이터·서비스 클래스 다수 | 시뮬 가능 행동 원시 7개 + 표현 전용 원시 1개 + 입력 함수 등록표 + JSON 고려사항 표 | 새 몬스터마다 클래스가 아니라 JSON 한 파일. 종당 컴파일 0회(D6·D7) |
| 전투 토큰(동시 공격 인원 제한) | BT 태스크가 토큰 요청·반납 | 공간 해시 기반 `ring_slot` 자리 토큰을 스텝 안에서 SimulationId 순으로 배정, `RingSlotFree` 입력 | 토큰 배정 순서가 결정적이어야 시뮬에서 재현된다(D17·D30) |
| 보스 페이즈 진행 | Logic Driver 스테이트 머신 클래스 + `BossPhaseManager` | JSON `phases` 배열(한 방향 진입, `on_enter` 시퀀스, stats 덮어쓰기) | 2페이즈 + 콤보는 표로 충분하고, 3페이즈 이상은 자체 HTN 을 조건부 추가(D2·D3) |
| NPC 행동 계획(HTN) | 엔진 HTN 플러그인 기반 NPC 생활 행동 | 전투 코어에는 계획기 없음. 필요 시 자체 C++ HTN, 엔진 HTNPlanner 미채택 | 실행기 부재·백트래킹 결함·방치(engine-htnplanner-plugin 결론 1·2·3·6) |
| NPC 최상위 의사결정(스테이트 트리) | `UKAIStateTreeComponent` + 에셋 트리 | StateTree 배제, 보스 연출·시각 디버깅 재검토 선택지로만 | 트리 조립·컴파일이 에디터 모듈에만 있고 MCP 툴셋은 검사 전용(engine-statetree-runtime 결론 6, web-llm-authorable-tooling 결론 2·3) |
| AI 이동(경로/스플라인/단순 이동) | `UKAIMovementComponent`(CMC 파생) + `UKSimpleMovementComponent` | 잡몹 `APawn` + `UFloatingPawnMovement`, 정예 CMC NavWalking, 시뮬 수학 이동. 2D 적분은 공용 | 잡몹 수백 마리에서 CMC 바닥 검사·물리 상호작용 비용을 없앤다(engine-movement-anim-scale 결론 1·4, D16) |
| 스쿼드 진형·AI 그룹 | `SquadSystem`(레이어·섹터 배정), `AIGroupSystem` 틱 | Phase 1~3 은 분리 조향 + 자리 토큰. 무리 역할 배정은 HTN 조건부 | 요구가 콘텐츠로 확정될 때 추가(D3) |
| 어그로 관리·AI 시야/감지 | `UKAggroComponent`, 커스텀 시각 센스 컴포넌트 | 입력 함수(`DistanceToTarget`, `LineOfSightToTarget`, 대상 선택은 스냅샷 규칙) | 컴포넌트·델리게이트 대신 스냅샷 값. 순수 함수라 시뮬에서 같은 코드(D7) |
| 공통 | 액터 이름 문자열·에셋 정본·리플렉션으로 엔진 비공개 필드 접근 | SimulationId 정수 핸들, 텍스트 정본, 공개 API 만 | 결정 기록 §10 |

---

## 미결 사항(사용자 결정 필요)

1. 페르소나 파일 위치·이름·단위: 본 문서는 `Content/MonsterAI/Definitions/` 에 `kind: "player_proxy"` 로 두어 로더·검증기를 하나로 유지한다. 페르소나 id 는 07 문서 M2-08 의 `Persona_{Careful,Default,Aggressive}` 로 통일하고, 시나리오의 `player.persona.definition` 은 `Definitions/` 기준 id 문자열(예 `"Persona_Default"`, 04 §9.1 의 `"Personas/Normal"` 은 갱신 필요), 반응 지연은 정의에서 `reaction_delay_seconds`(초), 시나리오 override 에서 `reaction_delay_steps`(스텝) 로 받는다 — 04·07 문서 갱신 필요. 별도 폴더(`Content/CombatSim/Personas/`)를 원하면 04 문서와 함께 바꾼다.
2. `CastAbility` 하나로 시전 원시를 통합했다(`spell` 필수, `timetable` 은 Phase 3 선택). 시간표 전용 원시를 따로 두는 것이 낫다고 판단하면 등록표에 `CastTimetable` 을 추가한다. 07 문서 M1-02('원시 6개')·M1-07(원시명 `Cast`, 경로 `Content/MonsterAI/Attacks/`)·M3-06(`Attacks/<종>_timetable.json`) 은 규칙 4 의 7+1 목록·`CastAbility`·`Content/MonsterAI/Timetables/<Id>.json` 으로 맞춘다.
3. `Logit`·`Gaussian` 의 정확한 식(§5.1.1)은 본 문서 제안이다. 다른 정의(예 Curvature 원식)를 원하면 `FTDResponseCurve` 와 덤프를 함께 바꾼다. `Quadratic` 의 비정수 `k` 에서 `x < c` 가 NaN 이 되는 문제는 §5.7 2단 규칙(`c ≤ 0` 강제)으로 막았으나, 식을 `m·|x − c|^k·sign(x − c) + b` 로 바꾸는 선택지도 있다.
4. 클래스 이름 중 §2 표의 '제안' 항목(`UTDMonsterDefinitionLibrary`, `FTDMonsterActionRegistry`/`FTDBrainInputRegistry`, `UTDMonsterAIBakeConstantsCommandlet`(`-run=TDMonsterAIBakeConstants`, 산출물 `TDMonsterDefinitions.gen.cpp`, 테스트 `TDGame.MonsterAI.BakeMatchesJson`), `FTDMonsterDefinitionBuilder` 등)은 본 문서 제안이다. 사용자 확정 전까지 02 표기가 기준이며 07 문서 M1-01(`TDMonsterDefinitionLoader`)·M1-02(`TDBrainRegistry`)·M1-05(`TDMonsterSlots.h` → `TDMonsterBrainSlot.h`)·M2-11·M3-07(`TDMonsterAIBakeCommandlet`, `Generated/*.inl`) 의 파일·클래스 이름을 02 로 맞춘다.
5. `tune` 잎을 값 객체(`{"value","tune": true}`) 로 표시하는 방식 대신 최상위 `tune` 경로 목록으로 두는 방식도 가능하다. LLM 가독성 우선으로 값 객체를 택했다. 탐색 범위는 D36·05 문서대로 스키마 덤프의 잎별 min/max 를 쓰며, 잎마다 범위를 적는 `"tune": [min, max]` 형태를 허용하려면 05 문서·D36 을 함께 바꾸는 결정이 필요하다.
6. `lod_periods` 를 종별로 덮어쓰게 허용할지(본 문서: 허용, `think` 채널 L1~L3 세 값). 전역 표·비율 감쇠만 쓰기로 하면 규칙 15 를 삭제한다. 허용하면 03 문서 §1.3 의 "JSON `lod` 절" 표기를 `lod_periods` 로 갱신해야 한다.

## 근거 색인(인용한 조사 파일 목록)

| 파일 | 인용한 결론 |
|---|---|
| `research/engine-misc-decision-tools.md` | R8(ToolsetRegistry·MCP), R13(`FJsonObjectConverter` 지원 범위), Q4(JSON 부동소수 왕복 미확인) |
| `research/web-llm-authorable-tooling.md` | 결론 2·3(StateTree 툴셋 검사 전용, 조립은 에디터), 결론 6(JSON DSL 유효율), 결론 7(IAUS 4파라미터 곡선), 결론 9(작은 고신호 파일) |
| `research/web-ai-architecture-comparison.md` | 결론 5(비용순 조기 종료), 결론 6(시드 난수) |
| `research/web-balance-simulation-tools.md` | 결론 2(부동소수·빌드), 승률 표준오차 표 |
| `research/project-current-combat-code.md` | 결론 5(픽스처 월드), 결론 9(물약 표현), 결론 10(C++ 원본 정의 메모리 생성) |
| `research/engine-determinism-headless.md` | 결론 1(`GFrameCounter`), 결론 8(커맨드렛 + 직접 Tick) |
| `research/engine-behaviortree-tick.md` | 결론 5·7(직렬화기 부재, 서비스·Wait 난수) |
| `research/engine-statetree-runtime.md` | 결론 6(런타임 조립 API 없음) |
| `research/engine-htnplanner-plugin.md` | 결론 1·2·3·6(엔진 HTN 배제) |
| `research/engine-movement-anim-scale.md` | 결론 1·4(CMC 비용, APawn 대체), 결론 11(`THierarchicalHashGrid2D`) |
| `research/engine-gas-determinism.md` | 결론 4·10(ASC 틱 비용, 헤드리스 ASC 초기화) |
| 엔진 소스 직접 확인 | `Engine/Source/Runtime/JsonUtilities/Public/JsonObjectConverter.h:87,239`(`CustomImportCallback`, `bStrictMode`·`ImportCb` 인자), `Private/JsonObjectConverter.cpp:265-280,909-946`(`FInstancedStruct` 내보내기·들여오기), `:1338-1339`(프로퍼티 이름으로 키 검색), `:1341-1354`(누락 필드만 검사), `Engine/Source/Runtime/CoreUObject/Private/UObject/Class.cpp:2558-2562`(`GetAuthoredNameForField` = `GetName`), `Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:212`(`double` 저장), `Engine/Source/Runtime/Engine/Classes/Commandlets/Commandlet.h:40`(`UCommandlet` 은 Engine 모듈) |
| 프로젝트 소스 | `Source/TDGame/Combat/Damage/TDDamageExamplesCommandlet.h:8`, `TDDamageExamples.cpp:59,79,115,137`, `Combat/Tests/TDDamageSystemTests.cpp:30`, `Combat/Characters/TDMonsterCharacter.cpp:10-11` |
| 같은 묶음 문서 | [03 §1.3·§8.2·§9](03-tick-and-scale.md)(think_hz·lod 절, `FTDMonsterSlots`, CVar), [04 §6.2·§9.1·§11.2·§12](04-combat-simulator.md)(시간표 파일명, 시나리오 `player` 블록, 분석 스크립트 4개, `run_batch.py`), [05 §4](05-ml-and-generative-ai.md)(`tune:true` 잎 벡터), [06 미결 4](06-beyond-the-ask.md)(N+1 스텝 만료), [07 M1-01·M1-02·M1-05·M1-07·M2-08·M3-06·M3-07](07-roadmap-and-tasks.md)(산출물 이름) |
| 설계안·심사 | A §2·§3·§6.2·§10·§11, B §2.3·§3.5, C §2.4·§3.5, D §3.4, 심사 2·3 "정의 형식" 판정, 심사 2 "이중 스텝" 지적 |
