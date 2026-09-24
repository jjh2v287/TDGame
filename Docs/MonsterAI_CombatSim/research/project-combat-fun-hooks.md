# 조사 노트(project) — 전투 재미 설계가 기댈 결정·연결점·공백

작성 2026-09-23, 조사자 project(웹 조사 없음). 표기: [검증]=파일·줄 직접 확인, [가정]=추론, [미확인]=확인 못 함. 줄 번호는 작업 트리 기준(사용자 미커밋 변경 포함, 유동).

## 0. 한 줄 결론

관계·전장 상태·조작 동사는 기존 결정(D1~D38)과 충돌하지 않고 **"C++ 등록표에 입력·원시 몇 개 + 데미지 규칙에 액션 1종 + 서브시스템 소유 표 2개(장판·시체)"** 로 들어간다. 없는 것은 강제 이동(밀기·당기기) 구현, 장판·시체의 조회 가능 상태, 피해자 쪽 사망 훅, 여러 슬롯이 묶이는 동사(합체·포식·빙의), 재미 지표, 조우 디렉터다.

## (a) 반드시 지킬 제약

| # | 제약 | 근거 | 새 설계에 주는 의미 |
|---|---|---|---|
| a1 | 두뇌 = 유틸리티 + FSM(유한 상태 기계) 5상태(Idle/Move/Cast/Sequence/Stagger). 비헤이비어 트리·StateTree·GOAP(목표 지향 행동 계획)·Mass 두뇌 배제, 자체 HTN(계층적 태스크 네트워크)은 Phase 4 조건부 | D1·D3·D4, `00-decision-record.md:11-14` | "주변 상태가 기능을 결정"은 역할 계획기가 아니라 고려사항(입력×곡선)으로 표현한다 |
| a2 | 종 하나 = JSON 한 파일, 최상위 키는 닫힌 목록 15개, 미지 키 거부 | D6·D8, `02:151-152` | 관계 키(예 `relations`, `on_death`)는 `schema` 2 로 올리는 스키마 확장이며 검증기·스키마 덤프도 같이 바뀐다(`06:213` 원칙 동일) |
| a3 | 입력 함수는 `FTDBrainInputs` 스냅샷만 읽고 월드·액터 접근 금지. 비용 등급 Cheap<Grid<Trace | D7, `02:277-307,325-330` | 관계 입력(근처 시체 수, 발밑 장판, 약한 아군)은 스냅샷 채우기 단계에서 미리 계산해야 한다 |
| a4 | 상태 정본은 `UTDMonsterThinkSubsystem` SoA(배열 구조체) 슬롯. 단일 틱 순서: 공간 해시→사고→이동→판정→`ExecuteRules` | D14·D15, `02:100-109` | 한 몬스터가 다른 슬롯을 바꾸는 동사(포식·합체)는 사고 중 즉시 쓰지 말고 스텝의 고정 지점에서 SimulationId 순으로 적용해야 한다 |
| a5 | 잡몹 몸은 캡슐 QueryOnly, 물리 상호작용·RVO(상호 속도 장애물 회피)·DetourCrowd 없음 | D16·D17, `00:32-33` | 밀기·당기기·볼링은 물리 임펄스가 아니라 2D 운동학 적분(`FTDKnockback::Integrate`, `06:256-267`)과 공간 해시로 한다 |
| a6 | GAS(게임플레이 어빌리티 시스템) 유지, 피해 공식 하나(`TDDamageFormula::Compute`) | D18, `00:34` | "보호" 같은 관계 버프는 Multiplicative GE(게임플레이 이펙트)나 공식 입력으로. 체력 직접 수정 금지 |
| a7 | 몬스터 공격 판정은 데이터 권위(`UTDDamageDefinition`, `ActivationDelay`=선딜) | D19, `00:38` | 예고(텔레그래프)는 데이터 선딜로 보장된다. 탑다운 가독성의 기반 |
| a8 | 플레이어 근접은 노티파이 스윕 유지 | D20, `00:39` | 플레이어 밀기 동사는 검 적중 규칙(`HitRules`) 경로에 붙이는 것이 자연스럽다 |
| a9 | 화면 밖에서도 이동·판정은 매 스텝 진행 | D21 | 장판 소멸·시체 부패·연쇄 반응도 화면 밖에서 진행된다 |
| a10 | 결정론: 이름 있는 난수 스트림만(Combat/PlayerProxy/Spawn/몬스터별 AI), 순서는 SimulationId, 스텝별 해시 체인 | D29·D30·D31, `00:54-56` | 새 상태(장판·시체·위협·연결)는 해시에 넣고, 디렉터 난수는 Spawn 스트림 재사용 또는 새 이름 있는 스트림(D29 목록 확장 결정 필요) |
| a11 | 단일 스레드, 규모 예산: 300마리 추정 4.0~4.7ms로 목표 4.0ms 경계 | D24·D25, `03:311-313` | 관계 입력은 기존 이웃 8개 목록·격자 조회만 쓰고 추가 트레이스를 만들지 않는다 |
| a12 | ML(머신러닝)은 플레이어 대리 봇·수치 튜닝만, 몬스터 신경망 두뇌 금지 | D36 | "재미" 검증도 페르소나 봇 + 시뮬 지표로 한다 |
| a13 | LLM(대규모 언어 모델) 제작 루프는 종당 컴파일 0회. 새 메커닉당 C++ 한 번 | D37, `02:330`, `05:282-294` | 관계 동사는 작은 닫힌 원시 집합이어야 하고 종은 JSON으로 조합만 한다 |
| a14 | 새 모듈 금지, `MonsterAI/`·`CombatSim/` 폴더만 | D13 | 장판·시체 표도 이 두 폴더 또는 기존 `Combat/` 안 |
| a15 | 로직은 C++ 전용 | AGENTS.md 7절 | 관계 반응 규칙을 블루프린트로 두지 않는다 |
| a16 | 게임=시뮬 비트 동일은 약속 안 함, B단계 이벤트 등가 ±1스텝 | D32 | 넉백 시나리오를 B단계 정합 테스트에 넣는 계획이 이미 있음(`06:267`) |

## (b) 재사용할 기존 연결점

| 연결점 | 위치 [검증] | 재사용 방식 | 한계 |
|---|---|---|---|
| 데미지 규칙 = 사건 × 행동 | `TDDamageTypes.h:18-39`(사건 Spawn/Hit/Pulse/Trigger/Expire/End/Kill/Activate, 행동 Damage/ApplyStatus/SpawnEntity/ApplyHoming/StopHoming), `:257-269` `FTDDamageRule` | 행동 종류 `Displace`(밀기·당기기·방사) 하나를 추가하면 주문·상태이상·플레이어 검 `HitRules`·몬스터 능력에 동시에 쓰인다 | 강제 이동 행동은 현재 없음 |
| 연쇄 폭주 방지 | `TDDamageTypes.h:183-187`(행동 2048·스폰 128), `TDDamageSubsystem.cpp:39,89`(깊이 ≤32) | 관계 연쇄(폭발→밀림→장판→폭발)를 같은 예산 안에서 돌린다 | 예산 초과 시 조용히 중단 → 로그 필요 |
| 처치 사건 규칙 | `TDDamageSubsystem.cpp:185-190` | "처치 시 장판 생성" 등 공격자 쪽 처치 반응 | 규칙이 **공격자 정의**에 속한다. 피해자 종의 사망 반응 훅은 없음 |
| 대상 정책 | `TDDamageTypes.h:50-56`, `TDDamageSubsystem.cpp:261-273`(TeamId 비교) | `Everyone`=아군 오사(유인해 동족 광역기에 맞히기), `Allies`=아군 버프 | 몬스터 능력에 정책 지정만 하면 새 코드 0 |
| 장판·함정 | `TDDamageExamples.cpp:51-68`(FlameField 반경 180, 4초, 0.5초 펄스; Fireball End→FlameField), `:106-112`(Mine Trigger), `TDMegaMagicDamageExamples.cpp:93-124`(ThunderCage, VenomPool/Bloom) | 전장 상태 1차 재료 | 전투원만 때린다. 장판끼리 반응·AI 조회 불가(아래 c) |
| 누적·빙결·파쇄 | `TDStatusDefinition.h:14-33`, `TDDamageExamples.cpp:70-77`(누적 30 → 빙결 2초 → 만료 시 파쇄 피해), `TDCombatComponentStatus.cpp:79-102` | "상태가 반응을 만든다"의 기존 예 | 상태 규칙 사건은 Spawn/Pulse/Expire/Kill 뿐(`TDStatusDefinition.cpp:32-35`). 상태 간 상호작용 없음 |
| 원소 | `TDDamageTypes.h:9-16`(Physical/Fire/Frost/Arcane), 저항은 방어/마법저항 2분류(`TDCombatComponent.cpp:264`) | 원소 태그로 반응 조건 | 원소별 약점·조합 규칙 없음 |
| 상태 태그 | `TDGameplayTags.h:9-18`(Dead/Frozen/Attacking/Stunned/Invulnerable 등) | 스냅샷 채우기에서 비트로 복사해 입력화 | 입력 함수가 ASC(어빌리티 시스템 컴포넌트) 태그를 직접 못 읽음(a3) |
| 예고 표시 | `TDDamageDefinition.h:33-34`(ActivationDelay), `:96-97`(bShowDuringActivationDelay) | 탑다운 가독성: 관계 반응도 선딜+표시로 예고 | — |
| 입력·원시 등록표 | `02:301-320`, 원시 7+1(`02:154`) | `NearbyCorpseCount`, `StandingInField`, `WeakestAllyHealth`, `TargetHeat` 등 입력과 `Shove`/`Devour`/`Guard` 원시를 한 줄씩 등록 | `FTDNeighborEntry`(`02:290`) 필드가 문서 어디에도 정의되지 않음 |
| 넉백·CC(군중제어) 규약 | `06:256-267` `cc_response`, `FTDKnockback::Integrate`, 07 M3-13 | 플레이어 밀기·몬스터 밀치기·볼링 모두 같은 순수 함수 | Phase 3, 선행 M3-02·M3-03 |
| 무리 전술 | `06:269-281`(groups, attack_tokens, ring, coordinator 32스텝, `HasAttackToken`·`RingSlotFree` 입력) | 위협 부채(Heat)가 토큰 수·배정 우선순위를 조절하는 형태로 얹기 | 07 대장에 항목 없음(06 제안 M3-14) |
| 웨이브·페이즈 사건 | `06:229-254`(spawn_table `when`, `on_enter_events` SetInvulnerable/Spawn/ForceSequence) | 디렉터·사망 반응 JSON의 문체 원형 | 정적 시간표일 뿐 강도 기반 판단 없음 |
| 결정 로그·분석 | `04:515-541` | 관계 사건 줄(`ev:"relation"`)·재미 지표 추가 | 현재 지표는 밸런스 전용(`04:481-494`) |
| 사망 메시지 | `TDGameplayMessages.h:29-41`, `TDMonsterCharacter.cpp:175-196` | 게임 쪽 시체 표현 트리거 | UObject 메시지 버스라 시뮬 경로 밖 |
| 빙결 정지 경로 | `TDCombatComponentStatus.cpp:397-460`(CustomTimeDilation 0, `PauseLogic` 441) | M1-04 `SetFrozen` 병행 호출 → CC 규약 사례 | — |
| 조우 데이터 | `Source/TDWorldGen/Public/Dungeon/TDEncounterDefinitions.h:11-77`(EnemyClass·깊이·Weight·DifficultyCost, 방당 예산 6·최대 4, `FTDSeedContext` 결정론), `World/Generation/TDEncounterSpawner.h:11-50` | 디렉터가 딛을 방 단위 예산·결정론 선택기 | 월드젠 대장 P2-11 소유(todo, 남은 일 있음), 키가 액터 클래스라 D6 JSON id와 불일치 |

## (c) 공백과 충돌 위험

### c1. 관계·반응 규칙
- [검증] 여러 슬롯이 묶이는 동사(합체·포식·빙의·운반) 지원이 없다: FSM 5상태는 단일 슬롯, 슬롯 프래그먼트 6종(`03:363-379`)에 연결·태그 필드가 없다. → 연결 프래그먼트 + 예약(자리 토큰처럼 SimulationId 순 청구) + 지연 명령 버퍼 필요. 빙의는 핫리로드 재매핑 규칙(`02:407`, 행동 id 재매핑·없으면 Idle)을 재사용할 수 있다 [가정].
- [검증] 종 간 관계 표가 없다. 정의는 종별 파일 + 단일 `extends`. 관계를 종 id로 직접 참조하면 N² 결합 → 종 태그(예 `family: slime`)로 참조하고 검증기 1단이 태그 존재를 검사하게 한다 [가정].
- [검증] 합체로 생긴 새 개체는 Spawn 스트림·SimulationId 단조 증가 규약(`06:240,254`)을 그대로 따라야 한다.
- [가정] 관계 반응이 판정 단계(5)에서 일어나면 두뇌는 다음 사고 스냅샷에서 본다(1스텝 이상 지연). 문서화 필요.

### c2. 전장 상태(시체·장판)
- [검증] 시체: 죽은 전투원은 대상 제외(`TDDamageSubsystem.cpp:263`), 몬스터 액터는 이동 끔·충돌 끔 후 기본 영구 잔존(`TDMonsterCharacter.h:74-75` `DeathLifeSpanSeconds=0`, `.cpp:177-181`). 게임 규칙상 시체 기록은 없고 시뮬에는 아예 없다. → 서브시스템 소유 시체 표(위치·종·사망 스텝·소비 여부·부패 스텝), 해시 포함. 300마리 기준 영구 잔존 시체의 표현 비용은 [미확인].
- [검증] 피해자 쪽 사망 훅 없음(처치 규칙은 공격자 소유). → 정의 JSON `on_death` 사건 목록(06 `on_enter_events` 문체).
- [검증] 장판: `UTDDamageSubsystem` 은 전투원 집합만 가진다(`TDDamageSubsystem.h:29`). 엔티티 목록·위치 조회 API가 없어 AI가 "발밑 장판"을 못 읽고 장판끼리(불+독, 물+번개) 반응도 없다. → 250cm 공간 해시 셀과 맞춘 장판 등록표 + 입력 함수.
- [검증] 엔티티는 자체 액터 틱(`TDDamageEntity.cpp:13-14`). 03 문서가 서브시스템 틱을 선행 조건으로 거는 규칙을 이미 두었다(`03:193`). 장판 간 반응 순서(엔티티끼리)는 규정이 없다 → 엔티티 SimulationId 순 처리 규칙 추가 필요.
- [검증] 원소 반응 기반이 약하다(원소 4종, 저항 2분류, 상태 간 규칙 없음). 반응표를 크게 만들면 PoE(Path of Exile) 과복잡 교훈과 충돌 → 반응 수를 적게 제한하는 편이 안전 [가정].

### c3. 플레이어 조작 동사(밀기·당기기)
- [검증] 구현 전무: `Knockback|Impulse|Launch|Pull|Push` 검색 결과는 사용되지 않는 선언 `FTDCombatReactionDefinition::KnockbackImpulse`/`StaggerDuration`(`TDCombatActionTypes.h:66-70`, 참조 0건)과 파괴물 임펄스(`TDBreakableActor.cpp:143`)뿐. 플레이어 능력은 콤보 1~3·Q·E·구르기·점프(`TDCombatActionAbility.h:78-141`, `TDPlayerMovementAbilities.h`).
- [검증] 피해 > 0 이면 무조건 피격 반응(Stunned)·콤보 리셋(`TDSkillComponent.cpp:334-344`, `TDReactionAbility.cpp:219-221`). 강인도(poise) 개념 없음 → 대량 전투에서 경직 연쇄 위험. `cc_response.stagger_steps` 에 경직 문턱을 넣는 편이 맞다 [가정].
- [검증] 순서 충돌: 게임 몬스터 위치 정본이 SoA가 되는 것은 Phase 3(M3-01)이고 넉백은 M3-13(Phase 3). 조작 동사의 재미를 시뮬에서 재려면 최소판을 Phase 2로 당겨야 한다. 게임에서 그 전에 시험하려면 현행 `ATDMonsterCharacter`(CMC(캐릭터 이동 컴포넌트) 몸)용 임시 경로가 필요하고 이는 두 경로 위험.
- [검증] 벽·장애물 충돌: 시뮬 `arena.obstacles` 는 빈 배열 형식만 있다(`04:424`). 벽에 박기·함정으로 밀기는 2D 장애물 선분 형식 결정이 선행 조건.
- [검증] 페르소나 봇(`kind: player_proxy`)도 같은 원시를 써야 시뮬이 "조작 사용"을 잰다(D12).

### c4. 조우 디렉터
- [검증] 강도 기반 디렉터 없음. 시나리오 spawn_table 은 정적(`06:229-240`), 게임 조우는 월드젠 `FTDEncounterResolver`(가중 룰렛·예산) 소유.
- [검증] 소유권 충돌: `Docs/Tasks/phase-2-dungeon-vertical-slice.md:120-130` P2-11(todo)이 `UTDEncounterSet`·스포너를 가진다. 관계 조합 선택(예 "방패 1 + 궁수 2")을 넣으려면 월드젠과 조정 또는 사용자 결정 필요.
- [가정] 디렉터 판단 주기는 조정자와 같은 32스텝, 입력은 스냅샷(플레이어 체력비·최근 피해·관계 사건 수), 난수는 Spawn 스트림. 판단 로그를 결정 로그와 같은 JSONL로.

### c5. 공통
- [검증] 재미 지표 없음(`04:481-494` 는 승률·피해·교체율). 필요 예: 관계 사건/분, 조작으로 끝난 처치 비율, 스킬 반복 편중(같은 광역기 비중), 결정 밀도.
- [검증] 문서 내부 불일치: 06 `cc_response.stagger_steps`·`decay_per_step`, `on_enter_events.steps`(`06:249,260-261`)는 스텝 단위인데 02 규칙 17은 정의 JSON 시간을 **초**로 정했다(`02:167`). `lod_periods` 는 02 규칙 15가 3값, 03 표가 4값 예시(`02:165` vs `03:65`).
- [검증] Phase 0 미착수 흔적: `FTDDamageContext` 에 스트림 없음(`TDDamageTypes.h:189-210`), 전역 난수 `TDCombatComponent.cpp:262`, `TDDamageSubsystem.cpp:231-232`, `GetUniqueID` 동률 `TDDamageEntity.cpp:317-322,509`, `AutoPossessAI` 잔존 `TDMonsterCharacter.cpp:24-25`. D29·D38·07 §2 의 줄 번호(220, 203-204, 305-307, 497-498)는 어긋났다.
- [검증] 범위 밖 전역 난수 `TDNPCUpdateSubsystem.cpp:89`(`FRandRange`, NoGlobalRandom 검사 폴더 밖). 기존 `UTDCombatTokenSubsystem`(`TDCombatTokenSubsystem.h:53-116`)은 GameInstance 소유·타이머·요청 순서 선착순이라 결정론 시뮬에 못 쓴다 → Heat는 06 조정자 위에.
- [검증] 작업 충돌: git 작업 트리에 데미지 파일 미커밋 변경(Subsystem +36, Entity +49, Definition.h +18줄 등). `Displace` 추가는 같은 파일을 건드리므로 착수 전 사용자 확인 필요(AGENTS 9절).

## (d) 02 문서 JSON 문체 요약 [검증 `02:149-270`, `04:418-455`, `06:217-278`]

- 키 snake_case, 프로퍼티도 같은 이름으로 선언(규칙 18). 식별자 값은 PascalCase(`"DistanceToTarget"`, `"MoveToward"`, 행동 id `"Approach"`, 곡선 `"Logistic"`). 능력 주문은 `DA_TD...` 이름.
- 최상위: `schema, id, kind, extends, stats, think_hz, lod_periods, inertia, select, top_n, abilities, sequences, actions, phases, persona`.
- `stats {max_health, attack_power, armor, move_speed, team}` / `abilities.<이름> {spell, range, cooldown, timetable?}`.
- `actions[] {id, do, args, weight, cooldown?, considerations[], remove?}`; 점수 = weight × Π(고려사항), 동률은 배열 순, 상시 대기 행동은 `Wait` + weight 0.05.
- `considerations[] {input, args?, curve, m, k, b, c, range[최소,최대], invert}`; 곡선 7종 Constant/Binary/Linear/Quadratic/Logistic/Logit/Gaussian.
- `sequences.<이름>: [{do, args}]`(≤8, 중첩 금지) / `phases[] {id, enter_when{input, op, value}, on_enter, stats, actions}`(한 방향).
- `inertia {switch_ratio, min_hold_seconds}`, 조정 가능 잎 `{ "value": 1.5, "tune": true }`, 정의 시간은 초, 시나리오 시간은 스텝 정수.
- 06 확장 문체: `cc_response {stagger_steps, knockback{mass_scale, decay_per_step, max_distance}, immune[], ai_during_cc}`, `role {default, when_token_denied}`, `actions[].pools`, `phases[].on_enter_events[{do, ...}]`, 시나리오 `groups[{id, members, coordinator_hz, attack_tokens, ring{radius, slots}}]`, `spawn_table[{at_step, definition, count, spawn, wave, when}]`, `difficulty{...}`.
- 결정 로그 키: `t, sim, def, dh, ph, lod, cand, pick, why, draws, h`, 사건 줄 `ev, src, dst, amount, crit, ability`.
- 새 조각이 따를 형태 예(제안, 가정):
```json
"family": "slime",
"on_death": [ { "do": "LeaveCorpse", "decay_seconds": 8.0 }, { "do": "SpawnField", "field": "SlimePuddle" } ],
"actions": [ { "id": "Devour", "do": "Devour", "args": { "family": "any" }, "weight": 2.0,
  "considerations": [ { "input": "NearbyCorpseCount", "args": { "radius": 300 }, "curve": "Binary" } ] } ]
```

## (e) 07 로드맵 구조와 끼울 자리 [검증 `07:28-34`, ID 목록]

| Phase | 항목(전부 todo, 예외 표기) | 완료 판정 핵심 |
|---|---|---|
| 0 기반 | M0-01~08 | 기존 테스트 28개 통과, NoGlobalRandom, 증분 빌드 실측 |
| 1 두뇌·시뮬 최소판 | M1-01~13 | 고블린 10 vs 규칙 봇 승률, 해시 게이트, PIE 300마리 실측 |
| 2 밸런스 툴 | M2-01~11, M2-15(M2-10 blocked) | 1,000시드 팬아웃, 30줄 요약, LLM 컴파일 0회 시연 |
| 3 몸·규모·시간표 | M3-01~10, M3-13, M3-15(M3-08 blocked, M3-10 decision) | 300마리 ≤4.0ms 실측, B단계 정합, 넉백·세이브 왕복 |
| 4 확장(조건부) | M4-01~05(전부 blocked) | 항목별 |

빈 번호: M1-14 이후, M2-12·M2-13(06이 M2-13 후보로 언급), M2-14(06 제안: 시각화), M3-11(06 제안: 정합 허용 오차), M3-12(06 제안: 텔레메트리), M3-14(06 제안: 무리 조정자). 사용자 결정 목록은 MD-01~MD-10.

끼워 넣을 자리(제안, 가정):
1. **M1-02 범위 보강**: `FTDNeighborEntry` 필드(SimulationId, 종 태그 비트, 팀, 체력비, FSM 상태, 관계 플래그)를 등록표 작성 시 확정. 나중에 바꾸면 스냅샷·해시·로그가 연쇄 변경.
2. **M1-14(신규) 관계 입력 최소판**: 이웃 기반 입력 3~4개 + 결정 로그 사건 줄. 선행 M1-02·M1-05.
3. **M2-16(신규) 강제 이동 최소판(시뮬)**: `FTDKnockback::Integrate` + 데미지 행동 `Displace` + `cc_response` 파싱. M3-13은 게임 몸 통합으로 축소. (M3-13 선행 변경 = 사용자 결정)
4. **M2-17(신규) 전장 상태 표**: 장판 등록표(격자) + 시체 표 + `on_death`, 해시 포함.
5. **M2-18(신규) 관계 동사 수직 슬라이스**: 예시 3종(슬라임·도끼병·궁수)으로 포식·보호·밀치기 등 5개 상호작용을 JSON만으로 작성, 컴파일은 원시 등록 1회.
6. **M2-02/M2-04 보강**: 재미 지표 열·분석 추가.
7. **M3-14(06 제안 채택) 조정자 + Heat**, 그 뒤 **조우 디렉터**는 월드젠 P2-11과 경계 결정 후(MD-11 후보).
8. 플레이어 밀기·당기기 스킬은 몬스터 AI 대장 밖(전투 기반, 대장 없음, AGENTS 15절 표) → 추적 위치 결정 필요.

## 출처(프로젝트 내부만)
문서: `Docs/MonsterAI_CombatSim/` 00(11-81), 02(15-130, 136-445, 488-552), 03(28-70, 193, 236-313, 358-392), 04(240-246, 410-541, 641-650), 05(280-322), 06(168-186, 211-294), 07(12-100, 222-243, 455-735); `Docs/Tasks/phase-2-dungeon-vertical-slice.md` 120-130.
코드: `Source/TDGame/Combat/{Damage,Skills,GAS/Abilities,AnimNotify}/`, `Combat/TDCombatComponent*`, `Core/TDGameplayTags.h`, `Core/TDGameplayMessages.h`, `Characters/TDCombatCharacter.h`, `Characters/TDMonsterCharacter.*`, `AI/CombatToken/`, `AI/NPC/TDNPCUpdateSubsystem.cpp`, `Source/TDWorldGen/Public/Dungeon/TDEncounterDefinitions.h`, `World/Generation/TDEncounterSpawner.h`.
