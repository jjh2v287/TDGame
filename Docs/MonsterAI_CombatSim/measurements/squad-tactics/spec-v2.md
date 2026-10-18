# M3-14·M3-28 명세 v2 — 무리 전술층(공격 토큰·포위 자리)과 전투 디렉터

작성: claude 2026-10-18. v1 → v2: 결정론 반박 13건, 게임 품질 반박 21건, 코드·시험 반박 14건, 파이썬 결정론 시뮬 457회(`Tools/scratch/20261018_claude_m314_tactics/`) 반영. 근거 폴더 `Saved/AgentOps/20261018/combat-tactics/`(understand·wf1·wf2).

## 0. 목표·범위

1. 공격 토큰: 대상별 교전 수에 비례한 예산(사용자 예시 10 → 3 유지). 근접·원거리 풀 분리, 보스 차선.
2. 포위 자리: 대상 주위 대기 고리 2겹·안쪽 공격 부채꼴·원거리 부채꼴. 토큰 없는 몬스터는 대기 칸에서 표류·위협 발짝을 한다.
3. 전투 디렉터: 대상별 긴장도와 박자(형성 → 절정 유지 → 감쇠 → 숨). 박자는 예산 배율과 공격 시작 간격(빈도)으로 조절한다. 자비 창·큰 피해 숨 고르기·조우 경계·개시 유예를 둔다.
4. 추가: 공격 시작 간격(가독성), 보복 우선(대기자 착취 방지), 막힌 칸 감지(벽·복도), 시퀀스 첫 시전 실패 시 중단.

범위 밖(후속 제안으로 기록): 종 JSON 키(역할·토큰 비용·보스 표식, 로더는 다른 세션 미커밋), 경보 연쇄 2단, 무리 사기·공포 전염, 역할 독점(돌진자 1명), 구 `AI/CombatToken` 삭제, 정면 유지 옆걸음(Characters 수정 필요), 능력 선딜 기반 타격 간격.

## 1. 파일과 계약

- 새 파일: `Source/TDGame/MonsterAI/TDCombatTactics.h/.cpp`(자료 구조·상수·순수 함수, 대표 타입 `FTDCombatTactics`), `Source/TDGame/MonsterAI/TDCombatDirector.h/.cpp`(대표 타입 `FTDCombatDirector` 정적 함수 + `FTDCombatDirectorState`), `Source/TDGame/MonsterAI/Tests/TDCombatTacticsTests.cpp`.
- 수정 허용: `TDMonsterThinkSubsystem.h/.cpp`, `TDMonsterActionExecutor.h/.cpp`, `TDMonsterBrainSlot.h`, `TDBrainInputs.h`, `TDBrainInputRegistry.cpp`.
- 수정 금지: `TDMonsterDefinitionLibrary.*`, `TDMonsterAttackCatalog.*`, `TDMonsterDefinition.h`, `TDMonsterSpeciesAsset.h`, `Characters/*`, `Combat/*`, `Core/*`, `TDGame.Build.cs`, `Config/*`, 종 JSON, `AI/CombatToken/*`, 기존 시험 파일(`Tests/TDMonsterAITests.cpp`·`Tests/TDCircleAvoidanceTests.cpp`).
- 규칙: 새 코드 주석 0(AGENTS 5절), 새 UFUNCTION은 `GetTacticsDebugLines` 하나, 전역 난수 0, TMap·TSet 순회 0(조회만 허용), 모든 상태 정수 POD(긴장도·체력·각도·거리 비교는 정수), 순회는 OrderedSlots(SimulationId 오름차순)와 대상 기록 생성 순.

## 2. 상수(`FTDCombatTactics` 안 `static constexpr`)

| 이름 | 값 | 뜻 |
|---|---|---|
| TokenPassPeriodSteps | 8 | 정기 토큰 배정(StepIndex % 8 == 0). 반납이 생긴 스텝에도 추가로 한 번 |
| RingPassPeriodSteps | 32 | 대기·원거리 칸 배정, 디렉터 박자 판정(StepIndex % 32 == 0) |
| MaxTokensPerTarget | 8 | 근접+원거리 합 상한 |
| MaxRangedTokens | 3 | 원거리 풀 상한 |
| TokenCostQuarters | 4 | 1마리 공격 비용(1/4 단위). 종별 비용은 후속 JSON |
| ReservationExpirySteps | 160 | Reserved 뒤 시작 못 하면 회수 |
| RegrantCooldownSteps | 48 | 반납·회수 뒤 후순위 기간 |
| WaitStageCap | 64 | 대기 단계 상한(굶음 방지) |
| MeleeGrantRadius | 900 | 근접 후보 거리 상한 |
| RangedGrantMargin | 100 | 원거리 후보 = 참조 최대 사거리 + 이 값 |
| EngagedCountRadius | 1600 | N 계산 거리 상한 |
| RangedRoleRange | 450 | 참조 능력이 모두 이 이상이면 원거리 |
| BossHealthThreshold | 2000 | 페이즈 0 MaxHealth 이상이면 보스 차선(임시, 후속 JSON) |
| BossSpacingSteps | 24 | 보스↔졸개 시작 간격 |
| RetaliationSteps | 64 | 대상에게 맞은 뒤 보복 우선 기간 |
| WaitRingInnerMin / Max | 320 / 520 | 근접 대기 반경 clamp |
| WaitRingMargin | 200 | 대기 반경 = 참조 근접 최대 사거리 + 이 값 + 몸 반경 |
| WaitRingSpacing | 200 | 둘째 고리 = 첫째 + 이 값 |
| WaitSectorCounts | {12, 16} | 고리별 칸 수 |
| InnerSectorCount | 8 | 안쪽 공격 칸 수 |
| RangedSectorCount | 12 | 원거리 칸 수(15° 엇갈림) |
| SlotKeepErrorDecidegrees | 450 | 이전 칸 유지 허용 각도 오차(0.1° 단위) |
| SlotSearchHalfDecidegrees | 900 | 새 칸 탐색은 현재 방위 ±90° 안 |
| SlotGapCm | 60 | 칸 폭 계산 여유 |
| BlockedProgressSteps / Cm | 64 / 20 | 칸 점에서 60cm 넘게 떨어진 채 64스텝 동안 20cm 미만 접근이면 막힘 |
| BlockedSectorSteps | 512 | 막힌 칸 사용 금지 기간 |
| MovingTargetSpeed | 150 | 대상 속도가 이 이상이면 진행 방향 뒤쪽 칸에 비용 +600(0.1°) |
| SouthPenaltyDecidegrees | 900 | 원거리 칸 중 화면 남쪽(−X, 중심각 cos < −0.5)에 가산 |
| SouthBandRadius | 340 | 남쪽 원거리 칸이면 띠 반경 상한 |
| DriftHalfDecidegrees / PeriodSteps | 60 / 256 | 대기 칸 표류 진폭·주기(SimulationId 위상) |
| FeintDepthCm / Steps / Every | 40 / 32 / 6 | 위협 발짝: (StepIndex/32 + SimulationId) % 6 == 0 인 32스텝 동안 반경 −40 |
| EncounterResetSteps | 128 | N == 0 이 이만큼 이어지면 대상 기록 삭제 |

시작 간격(박자별, 스텝): BuildUp 20, SustainPeak 16, PeakFade 24, Relax 32. 자비 창 안이면 +12.

CVar: `TD.MonsterAI.Tactics`(기본 1, 0 = 기존 '대상당 동시 3' 경로), `TD.MonsterAI.Tactics.Debug`(기본 0, 1 = 고리·칸 그리기). 둘 다 `RunKernelStep` 첫머리에서 멤버로 한 번 스냅샷. 전술 값이 바뀐 스텝에는 전술 슬롯 조각·대상 기록·처치 큐를 모두 초기화한다.

## 3. 역할·N·예산

- 역할(등록 때 고정, 슬롯 조각에 저장): 정의의 모든 페이즈 행동(`CastAbility`의 능력, `PlaySequence`가 가리키는 시퀀스의 `CastAbility` 능력)이 실제로 참조하는 능력만 본다. 페이즈 0 MaxHealth ≥ BossHealthThreshold 면 Boss. 아니면 참조 능력 사거리가 모두 ≥ 450 이면 Ranged, 하나라도 < 450 이면 Melee, 참조 능력이 없으면 Melee. 참조 근접 최대 사거리(< 450 중 최대)와 참조 최대 사거리도 저장. 기대: Goblin·Hyena·StoneGolem·BookHead_Brute = Melee, Archer·BookHead_Caster = Ranged, Ogre_Boss = Boss.
- N_melee, N_ranged: 그 대상 기록의 Engaged 슬롯 중 역할별, 대상 거리 ≤ EngagedCountRadius. 보스는 N에서 뺀다.
- 기본 토큰(풀별): N == 0 → 0. 아니면 clamp(max((N + 1) / 3, min(N, 2)), 1, 상한). 근접 상한 8, 원거리 상한 MaxRangedTokens. 근접 N: 1 → 1, 2~7 → 2, 8~10 → 3(사용자 예시 유지), 11~13 → 4, 23 이상 → 8. 합이 8을 넘으면 원거리부터 줄인다.
- 박자 배율(%): BuildUp 100, SustainPeak 100, PeakFade 75, Relax 50. 풀 예산(토큰 수) = max(1, max(기본 − 1, (기본 × 배율 + 50) / 100)) — 반올림, 기본보다 1개 넘게 줄이지 않음. 1/4 단위 예산 = 토큰 수 × 4.
- 감소 지연: 계산값이 현재보다 작으면 정기 토큰 배정 2회 연속일 때만 낮춘다. 증가는 즉시.

## 4. 토큰 수명

슬롯 조각 `FTDMonsterTacticsFragment`(Bodies·Brains와 같은 길이, `Avoidances` 뒤에 선언):
`TokenState{None, Reserved, Committed}`, `TokenRecordId`(uint32, 0 = 없음), `GrantStep`, `RegrantReadyStep`, `LastAttackEndStep`, `LastDamagedStep`, `Role{Melee, Ranged, Boss}`, `MeleeReachCm`, `MaxReachCm`, `WaitRing`, `WaitSector`, `InnerSector`, `RangedSector`(int8, −1 = 없음), `bIsOnDeck`, `SlotProgressAnchorStep`, `SlotProgressBestCm`, `LastCastStartCount`.

`FTDMonsterBrainFragment`에 `uint32 CastStartCount` 추가: `StartCast`가 성공하는 순간 ++(실행기). 확정은 상태 비교가 아니라 이 값의 증가로 감지한다.

- 후보 조건: Engaged, 슬롯의 대상 기록 = 이 기록, 빙결·Stagger 아님, `ActivePrimitive != MoveAway`, 토큰 None, 참조 능력 중 쿨다운 0 이고 사거리 ≥ (현재 거리 − 300)인 것이 하나 이상, 거리 ≤ 근접 MeleeGrantRadius / 원거리 MaxReachCm + RangedGrantMargin.
- 정렬 키: ⓪ 보복(StepIndex − LastDamagedStep ≤ RetaliationSteps, LastDamagedStep != 0) 먼저 ① 쿨다운 끝남(StepIndex ≥ RegrantReadyStep) 먼저 ② 대기 단계 min((StepIndex − LastAttackEndStep) / 32, WaitStageCap) 큰 순 ③ 거리² 정수 cm² 작은 순 ④ SimulationId 작은 순.
- 배정: 풀별로 (예산 − 보유) ≥ 4 인 동안 정렬 순으로 Reserved 부여(TokenRecordId·GrantStep 기록). 예산이 꽉 차면 정렬 1위 후보 1명을 `bIsOnDeck`(풀별 1명).
- 보복 가로채기: 예산이 꽉 찼고 ⓪ 후보가 있으면, 같은 풀의 Reserved 보유자(Committed 제외) 중 대상에서 가장 멀고 그 후보보다 2배 이상 먼 보유자의 토큰을 회수해 넘긴다(회수당한 쪽은 벌점 없음).
- 회수(벌점 있음: LastAttackEndStep = StepIndex, RegrantReadyStep = StepIndex + 48): Reserved 이고 (StepIndex − GrantStep ≥ 160) 또는 후보 거리 상한 × 1.25 초과 또는 MoveAway 시작.
- 회수(벌점 없음): 예산 감소로 초과 → Reserved 중 정렬 최하위부터. 교전 해제·대상 기록 변경·빙결·Stagger·등록 해제 → Reserved는 즉시 None.
- 확정: 사고·행동 루프에서 ThinkAndAct 뒤 `Brain.CastStartCount != Tactics.LastCastStartCount` 이면 동기화. 이때 Reserved면 Committed. 대상 기록의 LastAttackStartStep = StepIndex(시퀀스 두 번째 시전도 갱신, 자기 자신은 Committed라 막히지 않음). 공격 시작마다 디렉터 I += 8. 보스는 TokenState를 쓰지 않고 시작 때 LastAttackStartStep·LastBossStartStep만 갱신.
- 반납: 전술 갱신의 동기화 단계에서 Committed 인데 `State != Cast && SequenceIndex == INDEX_NONE` 이면 반납(LastAttackEndStep = StepIndex, RegrantReadyStep = StepIndex + 48). 같은 풀에 다른 후보가 없으면 None 대신 바로 Reserved(GrantStep = StepIndex). Committed 중 대상이 바뀌어도 원래 기록에 남겨 두고 시전·시퀀스가 끝날 때 원래 기록에서 반납.
- 반납이 생긴 스텝에는 같은 갱신 안에서 토큰 배정을 한 번 더 돈다(OnDeck가 곧바로 Reserved).
- 시작 허가 `HasAttackToken(Slot)`(전술 켬):
  - 보스: StepIndex − LastAttackStartStep ≥ BossSpacingSteps(졸개의 마지막 시작 뒤 24스텝)면 참. 디렉터·자비·유예·예산을 적용하지 않음. 보스 시작은 LastBossStartStep만 갱신하고 LastAttackStartStep은 바꾸지 않는다.
  - Committed: TokenRecordId == 현재 대상의 기록이면 참.
  - Reserved: TokenRecordId 일치 && (StepIndex − LastAttackStartStep ≥ 박자별 시작 간격(+자비 12)) && (StepIndex − LastBossStartStep ≥ BossSpacingSteps) && !(유예 중 && N_melee + N_ranged ≥ 2) && StepIndex ≥ GrantFreezeUntilStep.
  - 그 밖: 거짓.
- 같은 스텝 순서: 사고 루프가 SimulationId 순이고 확정이 루프 안 슬롯별이라, 앞 슬롯의 시작이 뒤 슬롯의 같은 스텝 허가를 막는다.

## 5. 포위 자리

- 대상 기록 `FTDTargetTacticsRecord`: `RecordId`(단조 증가 uint32), `Target`(TWeakObjectPtr, 조회는 포인터 비교 선형 탐색), 칸 점유표(고리 2 × 16, 안쪽 8, 원거리 12, 각 SimulationId 0 = 빔), 막힘 만료표(같은 모양, uint64 스텝), 예산·보유, `LastAttackStartStep`(졸개 시작), `LastBossStartStep`, `OpeningGraceUntilStep`, `GrantFreezeUntilStep`, `ZeroEngagedSinceStep`, 디렉터 상태, `PrevHealthCenti`, 대상 이전 위치(속도 계산용, 정수 cm). 배열은 생성 순, 삭제는 `RemoveAt`(순서 유지).
- 각도: 정수 0.1° 단위(`FMath::FloorToInt32(Atan2 도 × 10)`, 음수면 +3600). 칸 중심 방향 단위 벡터는 칸 수별 상수표. 비용 비교는 정수, 동률이면 작은 칸 번호.
- 칸 폭: 호 길이 ArcCm = FloorToInt32(2πR / K), 폭 = (2 × 반경 + SlotGapCm + ArcCm − 1) / ArcCm, 가운데 칸 기준 연속 칸 모두 비어야 함(안쪽·대기·원거리 공통).
- 대기 고리(고리 주기, 근접만, 대기열이 있을 때만 = N_melee × 4 > 근접 예산 1/4 단위): 근접 Engaged 전원(None·Reserved·Committed)을 (거리² 정수, SimulationId) 순으로 처리. ① 이전 칸이 비었고 막히지 않았고 오차 ≤ 45° 면 유지 ② 아니면 현재 방위 ±90° 안 빈 칸 중 비용 최소(비용 = 오차 + 이동 대상 뒤쪽 가산) ③ 첫 고리가 없으면 둘째 고리 ④ 둘 다 없으면 자리 없음. 대기열이 없으면 대기 칸을 모두 해제.
- 안쪽 칸(토큰 배정 직후, 근접 Reserved·Committed): 같은 규칙(유지 45°, ±90°, 폭).
- 원거리 칸(고리 주기, 원거리 Engaged 전원): 같은 규칙 + 남쪽 가산.
- 막힘 감지(매 스텝, 칸이 있는 슬롯): 칸 점과 거리 d > 60 이면 64스텝 창에서 최선 거리가 20cm 넘게 줄지 않았을 때 그 칸을 막힘표에 512스텝 등록하고 다음 고리 주기에 재배정. 대기 칸·원거리 칸에만 적용.
- 해제: 교전 해제·등록 해제·대상 기록 변경 즉시.

## 6. 이동 반영(실행기, 모든 종, JSON 변경 0)

`FTDMonsterStepContext` 추가 필드: `bIsTacticsActive`, `bHoldsAttackToken`, `TacticsSlotKind{None, Wait, Inner, Ranged}`, `TacticsSlotDirection`(FVector2D), `TacticsSlotRadius`, `TacticsSlotHalfAngleDegrees`, `bIsTargetClosingIn`(대상 속도의 이 몬스터 쪽 성분 > 150).

`MakeContext`가 칸 하나만 고른다: 근접 토큰 보유 + 안쪽 칸 → Inner / 토큰 없음 + 대기 칸 → Wait / 원거리 칸 → Ranged / 그 밖 None. 문맥에는 `TacticsSlotDirection`(대상에서 칸 쪽 단위 2D 벡터, Wait는 표류 각 반영)과 `TacticsSlotRadius`(Wait = 고리 반경 − 위협 발짝 깊이(발짝 중일 때), Ranged = 0이면 실행기가 띠 중간 사용, Inner = 0)를 넣고, 점 계산은 실행기가 한다. `TacticsSlotPoint` 필드는 두지 않는다.
- 표류 각 = 삼각파(진폭 6°, 주기 256스텝, 위상 = SimulationId × 37 % 256), 방향 부호 = SimulationId 홀짝.

동작:
- MoveToward + Inner: 대상 거리 ≤ StopAtDistance 면 도착(기존과 같음), 아니면 (대상 + 칸 방향 × StopAtDistance) 점으로 이동.
- MoveToward + Wait: 점 40cm 안이면 도착. 대상 거리 < 반경 − 60 이고 `!bIsTargetClosingIn` 이면 바깥으로(−대상 방향), `bIsTargetClosingIn` 이면 그 자리 대치(도착). 그 밖이면 점으로 이동. 속도 배율 1.0.
- MoveToward + 토큰 없음 + 칸 없음(전술 켬, 대기열 있음): 대상 거리 < 첫 고리 반경 − 60 이면 바깥으로, ≤ 첫 고리 반경 + 200 이면 도착, 아니면 대상 방향.
- MoveToBand + Ranged: 띠 밖이면 기존처럼 거리만 맞춤. 띠 안이고 칸 각도 오차 ≤ 반폭이면 도착. 띠 안이지만 오차가 크면 (대상 + 칸 방향 × 띠 중간 반경, 남쪽 칸이면 min(중간, 340)) 점으로 이동.
- MoveToBand + Wait(근접 종의 선회, 예: 하이에나): 반경은 띠 우선, 각도만 대기 칸(같은 점 이동, 띠 중간 반경).
- 그 밖 조합(토큰 보유 + 안쪽 칸 없음, Ranged + MoveToward, 전술 끔): 기존 동작.
- 양보: `IsHoldingEngagedAttacker` = Engaged && 대상 유효 && !(전술 켬 && 대기 칸 있음 && 토큰 None && State != Cast). 단일 몬스터·대기열 없음이면 기존과 같다.
- 회피 장애물: 대상 제외 조건(TDMonsterThinkSubsystem.cpp 교전 대상 제외 지점)에 "토큰 보유 또는 Cast"를 추가해 대기 칸으로 가는 몬스터는 대상을 돌아간다.
- 시퀀스: `StartSequence`에서 첫 단계가 CastAbility 인데 StartPrimitive 뒤 State != Cast 이면 시퀀스를 중단(SequenceIndex = INDEX_NONE, Idle, bForceThink).

## 7. 디렉터(대상 기록마다 `FTDCombatDirectorState`)

- 상태: `Intensity`(int32 0~1000), `Phase`, `PhaseUntilStep`, `LastDamageStep`, `WindowDropPermille[2]`(직전·현재 32스텝 창), `bIsMercyOpen`, `MercyUntilStep`, `MercyLockoutUntilStep`, `bIsMercyArmed`.
- 체력 폴링(매 스텝): Cur = FloorToInt64(GetCurrentHealth × 100), Max = FloorToInt64(GetStats().GetMaxHealth() × 100). 기록 생성 때 Prev = Cur. 매 폴링 Drop = max(0, Prev − Cur), Prev = Cur(회복도 Prev에 반영). DropPermille = Drop × 1000 / Max. I += DropPermille × 2, LastDamageStep = StepIndex(Drop > 0), 현재 창 += DropPermille. 대상 전투 컴포넌트가 없으면 폴링 생략. 폴링은 프레임 양자화되며 결정론은 고정 dt에서만 보장(명세에 기록).
- 처치: UnregisterMonster에서 `Actor.Reset()` 전에 액터 유효 && !IsActorAlive 면 Bodies 위치를 처치 큐(TArray, 넣은 순)에 넣는다. 다음 전술 갱신에서 큐 순서대로 각 대상 기록에 대해 1200cm 안이면 I += 40.
- 공격 시작: 확정 때 I += 8.
- 고리 주기마다: (StepIndex − LastDamageStep > 64) 면 I −= 40. 큰 피해: 창[0] + 창[1] ≥ 200 이면 GrantFreezeUntilStep = StepIndex + 64. 창 밀기(창[0] = 창[1], 창[1] = 0). I clamp 0~1000.
- 박자(고리 주기마다 판정): BuildUp: I ≥ 700 → SustainPeak, 256스텝. SustainPeak: 시간 끝 → PeakFade, 최대 256스텝. PeakFade: I ≤ 250 또는 시간 끝 → Relax, I = min(I, 250), 256스텝. Relax: 시간 끝 → BuildUp, I = min(I, 400).
- 자비: 체력 비율 < 25% 이고 bIsMercyArmed 이고 StepIndex ≥ MercyLockoutUntilStep 이면 창 열기: MercyUntilStep = StepIndex + 256, GrantFreezeUntilStep = max(현재, StepIndex + 32), bIsMercyArmed = false, MercyLockoutUntilStep = StepIndex + 1920. 창 안이면 시작 간격 +12. 체력 비율 > 40% 면 bIsMercyArmed = true. 예산은 줄이지 않는다.
- 개시 유예: 기록 생성 때 OpeningGraceUntilStep = StepIndex + 96(1.5초). 유예 중에는 N ≥ 2 일 때만 시작 금지(배정·접근은 허용).
- 조우 경계: N == 0 이 EncounterResetSteps 이어지면 기록 삭제. 대상이 죽거나 무효면 즉시 삭제.

## 8. 커널 순서

```
RunKernelStep:
  ++StepIndex; CVar 스냅샷(바뀌면 전술 초기화)
  감지 → 무효 정리 → 이웃 격자 → UpdateEngagement
  전술 켬: UpdateTactics
    1) 처치 큐 소비  2) 대상 기록 연결: 슬롯의 대상 → 기록(없으면 생성), 기록별 명단(OrderedSlots 순)
    3) 동기화: 반납·회수(벌점 규칙), 막힘 감지
    4) 디렉터: 체력 폴링, [고리 주기] 감쇠·큰 피해·박자·자비
    5) [고리 주기] 대기 고리·원거리 칸 배정
    6) [토큰 주기 또는 반납 발생] 예산·감소 지연·회수·배정·OnDeck·안쪽 칸
    7) 기록 삭제(조우 경계·대상 무효)
  전술 끔: CountAttackersByTarget(기존)
  사고·행동 루프: (SlotIndex, Generation) 사본, 세대 다르면 건너뜀. ThinkAndAct 뒤 CastStartCount 증가면 확정. 전술 끔이면 기존 Cast 계수.
  국소 회피
```
v1과 달리 08 확장 순서(조정자를 사고 뒤)가 아니라 사고 앞에 둔다: 같은 스텝의 사고가 새 배정·허가를 바로 읽게 하기 위해서다(08 문서에 정정 제안으로 남김).

## 9. 입력 등록표(D7)

`FTDBrainInputs`에 `bHasAttackToken`(시작 허가, 기본 true)·`bHasRingSlot`(TacticsSlotKind != None) 추가, `BuildInputs`가 문맥에서 복사. 등록표에 `HasAttackToken`(Cheap, 0~1)·`RingSlotFree`(Cheap, 0~1) 두 줄. 읽기 함수는 스냅샷만 읽는다. JSON은 이번에 쓰지 않는다(디자이너용 진입점). 기존 `ReadyAbilityMask` 토큰 게이트는 유지.

## 10. 관찰

- `GetMonsterDebugLines` 각 줄 끝: ` token=-|R|C|B slot=-|W<고리>:<칸>|I<칸>|R<칸> ondeck=0|1`(B = 보스 차선).
- 새 UFUNCTION `TArray<FString> GetTacticsDebugLines() const`: 기록마다 `record=<Id> target=<이름> melee=<N_melee> ranged=<N_ranged> base_tokens=<근접>/<원거리> budget=<근접 q>/<원거리 q> held=<근접 q>/<원거리 q> phase=<BuildUp|SustainPeak|PeakFade|Relax> intensity=<I> spacing=<스텝> mercy=<0|1> freeze=<0|1> grace=<0|1>`.
- `TD.MonsterAI.Tactics.Debug 1`: PresentFrame 안 DrawDebugState 옆에서 고리·칸·토큰 표시(표현 전용).
- `TRACE_CPUPROFILER_EVENT_SCOPE(TDMonsterAI_Tactics)`.

## 11. 해시 대상(D31 예약, 이번에는 구현하지 않고 명세에만)

슬롯: TokenState, TokenRecordId, GrantStep, RegrantReadyStep, LastAttackEndStep, LastDamagedStep, Role, 칸 4종, bIsOnDeck, LastCastStartCount. 기록: RecordId, 예산·감소 지연 카운터, LastAttackStartStep, LastBossStartStep, 유예·동결·0명 시작 스텝, PrevHealthCenti, 디렉터 상태 전부, 점유·막힘표. 처치 큐. 전술 모드 값.

## 12. 시험(`TDGame.MonsterAI.Tactics.*`, `Tests/TDCombatTacticsTests.cpp`)

도우미는 파일 고유 이름(`FTDTacticsTestWorld`, `SpawnTacticsPlayerPawn` 등, L-build-03)으로 TDMonsterAITests.cpp 픽스처를 복제한다. `FTDMonsterThinkSubsystemTestAccess`를 다시 정의하지 않는다(ODR). 내부 접근이 필요하면 새 친구 구조체 `FTDCombatTacticsTestAccess`를 선언한다. CVar를 바꾸는 시험은 `ON_SCOPE_EXIT`로 복원한다.

단계 A(빨강 증거, 기존 공개 API만: `GetMonsterDebugLines`의 state=Cast·engaged, 액터 위치. 새 심볼 금지):
1. `ConcurrentAttackersFollowBudget`: 정지 대역 플레이어(체력 100000) 둘레 고블린 N=6, 20초. 최대 동시 Cast ≤ 2. 기존: 3 → 빨강.
2. `AttackStartsAreSpaced`: N=10, 30초. 같은 대상 시전 시작 사이 최소 간격 ≥ 16스텝. 기존: 같은 스텝 동시 시작 → 빨강.
3. `TokenlessMonstersHoldTheWaitRing`: N=10, 8~20초 구간. Cast 아닌 몬스터의 대상 거리 중앙값 ≥ 300, 시전 시작 순간 거리 < 360. 기존: 중앙값 ~150~225 → 빨강.
4. `EveryMonsterGetsATurn`: N=10, 30초. 서로 다른 시전자 10, 몬스터별 최장 무공격 간격 ≤ 20초.
5. `Deterministic`: N=10 15초를 두 번. 스텝별 디버그 줄(actor 이름 제외) FNV-1a 해시 열 동일.

단계 B(구현 뒤, 새 심볼 허용):
6. `BudgetFormula`(순수): 근접 N 0~30 기대표(위 3절), 원거리 상한 3, 합 상한 8, 박자 배율·반올림·−1 하한.
7. `RoleClassification`: 7종 정의 로드 → 기대 역할(3절).
8. `DirectorCyclesUnderConstantPressure`(순수): 10마리 상시 근접·0.75초마다 4% 피해 대본 60초 → Relax 진입 ≥ 1, 예산 ≤ 1토큰 시간 비율 ≤ 35%, 단계 순서 BuildUp→SustainPeak→PeakFade→Relax→BuildUp.
9. `MercyWindowOpensOnce`(순수): 체력 24% 유지 → 창 1회, 1920스텝 재발동 없음, 40% 회복 뒤 재무장.
10. `TokenReleasedOnInterrupt`: 시전 중 빙결·경직 → 다음 토큰 주기 안 token=-.
11. `FleeingMonsterDoesNotStallTokens`: N=3 중 1마리 체력을 깎아 도주 → 나머지 시전 간격 ≤ 128스텝.
12. `AttackedWaiterRetaliates`: N=10, 대기자 1마리를 대역 플레이어 대상으로 3회 피격(ReceiveDamage) → 64스텝(+토큰 주기) 안 그 몬스터 Cast.
13. `BossLaneNotStarved`: Ogre_Boss + 고블린 3, 보스 시전 간격 ≤ 쿨다운 + 128스텝.
14. `EncounterResetRestoresGrace`: 교전 → 전원 귀환 128스텝 → 재교전 시 grace=1, phase=BuildUp.
15. `TacticsOffKeepsLegacyCap`: CVar 0 → N=6에서 최대 동시 Cast ≥ 3 가능(기존 상한), 복원 보장.
16. `HoldingAttackerStillHoldsWithoutQueue`: 단일 몬스터에서 양보 규칙이 기존과 같음.

기존 시험 기대 영향: `HoldingAttackerDoesNotYield`(대기열 없음 → 영향 없어야 함), `MonsterTelegraphsThenHits`·`HeavyHitInterruptsTelegraph`·`FreezeHaltsThenResumes`(단일 몬스터, 유예는 N ≥ 2 에만 → 영향 없어야 함). 기존 시험이 깨지면 고치지 말고 이름·단언·수치를 보고한다.

전체 회귀: `python Tools/build_and_test.py --filter TDGame.MonsterAI --filter TDGame.Movement --label m314-<단계>`. 기준선: `CircleAvoidance.Scene.CrossingStreams` 1건 실패. NoGlobalRandom 통과.

## 13. 수용 기준

- 단계 A 시험이 기존 코드에서 빨강(로그 보존), 구현 뒤 전부 초록. 단계 B 초록.
- 기존 통과 수 유지(기준선 1건 제외), NoGlobalRandom 통과.
- `python Tools/check_sync_loads.py`·`python Tools/check_cook_coverage.py` 종료 0.
- CVar 0으로 기존 동작 복귀.
