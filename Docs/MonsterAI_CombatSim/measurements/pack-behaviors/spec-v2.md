# 무리 행동 3종 명세 v2 — 경보 연쇄(M3-30)·무리 사기와 공포 전염(M3-31, MD-43)·역할 독점(M3-32)

작성: 2026-10-18 claude. v1 → 반박 3관점(A 결정론 치명1·주요6·경미12, B 게임 체감 주요6·경미8, C 연결·시험 치명2·주요8·경미5)을 반영. 반영 표는 9절. 기반: M3-29 통합 트리(14:47, `ETDMonsterCombatRole{Unspecified,Melee,Ranged}`·`bIsBoss`·`ReserveCostQuarters`·`GetHeldCostQuarters`·엄격 우선순위 부여 루프).
근거 문헌: `research/web-topdown-arpg-crowd-combat.md` §3.3~3.7·§4·§6 순위 2~4, `research/web-squad-slots-and-directors.md` A2·A3.

## 0. 공통 규약

- 고정 스텝(1/64초), SimulationId 오름차순(또는 명시 정렬 키 + SimulationId). 전역 난수·`TMap` 순회·월드 타이머 금지. 외부 콜백(피해·사망)이 쌓는 목록은 처리 전에 SimulationId로 정렬한다.
- 커널 순서(전술 활성): Sense → stale 해제 → RebuildNeighborGrid → `UpdateEngagement`(경보 포함) → **`UpdatePackMorale`(신규)** → `UpdateTactics` → ThinkAndAct → 회피. `bIsTacticsActive`가 거짓이면 `UpdatePackMorale` 전체와 경보 신규 경로를 건너뛰고 기존 즉시 1단 경보를 쓴다.
- 상태는 서브시스템 슬롯 배열. 새 병렬 배열 `Morales`는 `RegisterMonster`에서 다른 배열과 함께 늘리고(`AddDefaulted`) 슬롯마다 `FTDPackMorale::MakeFragment(*Definition)`로 새로 만든다. `Deinitialize`는 `Morales`·`PendingAlarms`·`PendingPackDeaths`를 비운다. `ResetTactics`(CVar 전환)는 `PendingAlarms`·`PendingPackDeaths`를 비우고, 각 슬롯 사기 조각을 정의로 다시 만들며(면역·우두머리 보존), 두뇌 `AlarmRole = None`·`AlarmHop = 0`·`AlarmCallStep = 0`.
- 새 규칙 파일(`TDPackAlarm.cpp`·`TDPackMorale.cpp`)은 익명 네임스페이스 없이 클래스 정적 함수만 둔다(L-build-03). 새 시험 파일의 익명 네임스페이스 이름은 `Pack` 접두(`FTDPackTestWorld`·`SpawnPackPlayerPawn`·`ReadPackLineField` 등).
- 새 C++에 주석 금지, 조기 반환, 이름 풀어 쓰기. `TDMonsterThinkSubsystem.cpp`에는 접착 코드만.

## 1. 경보 연쇄(M3-30)

### 1.1 발견자
- 발견자 = 이번 스텝 비교전→교전이 된 몬스터(근접 발견 EngageRadius 1500, 피격 — 귀환 중 피격 재교전 포함, 기존 `PendingAlertSlots` 경로). 처리 순서는 SimulationId.
- **사망 경보(한 방 처치)**: `UnregisterMonster`에서 사망(액터 유효 + `IsActorAlive` 거짓)·전술 활성·이 슬롯이 `PendingAlertSlots`에 있음(이번 프레임 피격으로 교전 직전)·두뇌 대상 유효이면 `PendingDeathAlarms`에 `{Location, Target, TeamId, SimulationId}`를 넣는다. 다음 `UpdateEngagement`에서 이 기록을 발견자처럼 1단 예약의 원점으로 쓴다(호출자 판정 없음 — 죽은 개체는 외치지 않는다).

### 1.2 호출자(첫 발견자만 표시)
- 발견자 D(대상 유효)는 다음을 만족하는 활성 몬스터 M이 없을 때만 `Caller`, 있으면 `Silent`: `M.AlarmCallStep ≠ 0`, `Now − M.AlarmCallStep < AlarmCallMemorySteps(192)`, `M.Target == D.Target`, 같은 `Definition->Id`, 같은 팀, 평면 거리(M, D) ≤ `AlarmCallRadiusCm(1800)`. Caller는 `AlarmCallStep = Now`.
- `BeginReturn`은 `AlarmRole = None`, `AlarmHop = 0`, `AlarmCallStep = 0`.

### 1.3 연쇄 예약
- 원점 O(발견자 또는 사망 경보)마다 OrderedSlots 순으로 S: 활성·Home·`ReengageBlockSteps == 0`·같은 팀·평면 거리(S, O) ≤ `AlertRadius(900)`·평면 거리(S.Home, Target) ≤ `LeashRadius(3000)` → `FTDPendingAlarm{Slot, Generation, SimulationId, Target, Due = Now + ComputeHopDelaySteps(FloorToInt32(거리)), Hop = 1}`.
- `ComputeHopDelaySteps(DistanceCm) = 8 + 8 × clamp(DistanceCm, 0, 900) / 900`(정수) → 8~16.
- 중복: 같은 슬롯·같은 세대가 이미 있으면 Due가 이른 쪽, 같으면 Hop이 작은 쪽만 남긴다(`FTDPackAlarm::AddOrKeepEarlier`).
- 처리: `Due ≤ Now` 항목을 (Due, SimulationId) 순으로 꺼내 검증(세대 일치·활성·Home·차단 0·대상 생존·같은 팀·집이 영역 안) 후 `Target = 예약 대상`, `Engage`, `AlarmRole = Follower`, `AlarmHop = Hop`. `Hop < MaxAlarmHops(2)`면 그 몬스터의 현재 위치를 원점으로 Hop+1 예약. 2단에서 멈춘다.
- `UpdateEngagement` 안 순서: ① 기존 상태 루프 ② 발견자·사망 경보 처리(호출자 판정 + 1단 예약) ③ 만기 예약 처리. 새 예약은 Due ≥ Now+8이라 같은 스텝에 처리되지 않는다.
- 정리: `UnregisterMonster`가 그 슬롯 예약을 지운다. 전술 꺼짐이면 기존 즉시 1단 코드 그대로.

### 1.4 표시
- 공개 `ETDMonsterAlarmRole GetMonsterAlarmRole(FTDMonsterSlotHandle) const`(표현층 외침·아이콘 자리). 디버그 줄 끝 ` alarm=<none|caller|silent|hop1|hop2>`. `TD.MonsterAI.Tactics.Debug 1`이면 Caller 머리 위 "!".

## 2. 무리 사기·공포 전염(M3-31) + MD-43

### 2.1 단계
평상 → 공황(후퇴, 경직·빙결 뒤 재개) → 결속(이번 교전 동안 면역, 귀환하면 풀림) → 평상. 판정은 Days Gone 공식 1의 정수 축약.

### 2.2 면역·우두머리
- 면역 = `bIsBoss` 또는 새 키 `"elite": true`(최상위 불리언, 기본 false, `FTDResolvedMonsterDefinition::bIsElite`) 또는 역할 Support(D2 주술사).
- 우두머리 = `bIsBoss || bIsElite`. 지지 계산에서 면역 개체는 2(`ImmuneAllyWeight`), 그 밖 1.
- 데이터(이번 범위, MD-48 proposed): StoneGolem·BookHead_Brute `"elite": true`.

### 2.3 사망 기록
- `UnregisterMonster`에서 사망·전술 활성이면 기존 `PendingKillLocations`(디렉터)와 함께 `PendingPackDeaths`에 `{Location, VictimSimulationId, TeamId(정의 phase-0 stats.Team), bWasLeader}`. bWasLeader는 슬롯을 비우기 전에 읽는다.

### 2.4 `UpdatePackMorale`(매 스텝, 전술 활성)
1. 사망 정렬(VictimSimulationId) 후 사망 d마다 OrderedSlots 순으로 목격자 W(활성·생존·Engaged·같은 팀·비면역·`bHasRallied` 거짓·공황 상태 아님):
   - 평면 거리 ≤ `WitnessRadiusCm(1200)`이면 W의 손실 링(4칸, 최근 사망 스텝)에 Now를 넣는다.
   - d가 우두머리이고 거리 ≤ `LeaderWitnessRadiusCm(1600)`이고 W에서 1600 안에 다른 생존 우두머리가 없으면 `bLeaderShock = true`.
2. 판정이 필요할 때만(이번 스텝 사망이 있었거나 직전 스텝에 공황 후퇴가 실제로 시작됨): 이번 스텝 시작 시점 공황 집합으로 스냅샷 판정. 후보 W(위 목격자 조건 동일)마다
   - `bLeaderShock` → 공황(길이 `LeaderFearMinSteps 128 ~ LeaderFearMaxSteps 192`).
   - 아니면 `Loss = 링에서 Now − 스텝 ≤ LossWindowSteps(80, v2.1: 시뮬 v2에서 192는 꾸준 처치 6/7 무리 공황)인 개수`, `Panicking = 반경 1200 안 공황 아군 수`, `Support = 1 + Σ 반경 1200 안 비공황 아군 가중치`(아군 = 활성·생존·Engaged·같은 팀). `Loss ≥ MinLossesForPanic(2)` 그리고 `Support ≥ 2`(혼자 남은 개체 제외) 그리고 `2 × Loss + Panicking ≥ Support` → 공황(길이 `FearMinSteps 64 ~ FearMaxSteps 128`).
   - 판정된 W 전부에 한꺼번에: 공황 상태 = 대기(`FearStartDeadlineStep = Now + FearPendingSteps(64)`, `FearSteps = ComputeFearSteps(SimulationId, Now, bIsLeaderShock)`), 손실 링 비움, `bLeaderShock = false`, `bHasRallied = true`.
3. 공황 진행(판정 게이트와 무관, 공황 상태 슬롯이 하나라도 있으면): 각 공황 슬롯 W
   - 대기 상태: `FearStartDeadlineStep < Now`면 취소(평상). 두뇌가 `CanThink()`·비빙결·비경직이면 `FearEndStep = Now + FearSteps`, 후퇴 시작, 다음 스텝 판정 켜기(전염).
   - 진행 상태: 두뇌의 `bWasRetreatCornered`가 참이면 공황 끝(막히면 싸움), 플래그 내림. `Now ≥ FearEndStep`이면 공황 끝. 시간 제한 후퇴 중이 아니고(경직·빙결로 끊김) `FearEndStep − Now ≥ FearResumeMinSteps(16)`이고 `CanThink()`·비빙결·비경직이면 남은 스텝으로 후퇴 재개.
   - 후퇴 시작·재개 = `FTDMonsterActionExecutor::BeginRetreat(Brain, Steps, FearSpeedScale(1.2), PanicHoldDistanceCm(700), Context)`.
4. `ComputeFearSteps(SimulationId, StepIndex, bIsLeaderShock)` = 최소 + (SimulationId × 37 + StepIndex) % (최대 − 최소 + 1).
5. `bHasRallied`는 `BeginReturn`·`FinishReturn`에서 거짓(교전당 1회).

### 2.5 시간 제한 후퇴(공황·Flee 공통, 시퀀스 밖 전용)
- `BeginRetreat(Brain, Steps, SpeedScale, HoldDistanceCm, Context)`(공개): `InterruptActivity` 뒤 `ActivePrimitive = MoveAway`, `ActiveArgs = {SpeedScale, StopAtDistance = HoldDistanceCm}`, `State = Move`, `StateStepsRemaining = Steps`, `CurrentActionIndex = INDEX_NONE`, `bForceThink = false`, 후퇴 추적 초기화.
- JSON `MoveAway`에 `seconds > 0`이면 시퀀스 밖에서도 시간 제한 후퇴로 시작한다(`StartPrimitive`, 후퇴 추적 초기화 포함). `seconds` 없는 MoveAway(Backpedal)는 지금과 같다. 시퀀스 안 MoveAway는 지금과 같다.
- 시간 제한 후퇴 = `SequenceIndex == INDEX_NONE && State == Move && ActivePrimitive == MoveAway && StateStepsRemaining > 0`. `CanThink()`는 이때 거짓(사고 루프·RefreshTarget·페이즈 진입이 최대 3초 늦는 것은 의도).
- 후퇴 추적(두뇌 조각): `RetreatAnchorDistance`(초기 −1), `RetreatStallSteps`, `bWasRetreatCornered`. 매 스텝 `StepState`(Move, 시퀀스 밖, 시간 제한 후퇴):
  1. 남은 스텝 −1.
  2. 대상 없음 → 끝.
  3. 거리 d(평면). `StopAtDistance > 0 && d ≥ StopAtDistance` → 이동 0·대상 바라봄(움츠림), 정체 계산 안 함.
  4. 아니면 앵커 −1이면 앵커 = d. `d > 앵커 + RetreatProgressCm(30)`이면 앵커 = d·정체 0, 아니면 정체 +1. 정체 ≥ `RetreatCorneredSteps(24)` → `bWasRetreatCornered = true`, 끝.
  5. 남은 스텝 0 → 끝. 끝 = `CompleteActivity`(Idle·강제 사고).
- 공황·Flee는 MoveAway라 기존 규칙대로 Reserved 회수(`RevokeInvalidReservation`)·후보 제외(`IsTokenCandidate`)·Committed는 시전이 끝난 뒤 `CanKeepTacticsToken` 거짓으로 해제된다. 공황 상태 슬롯은 대기·안쪽·원거리 칸 배정 필터에서 제외하고 `ResolveTacticsSlotKind`는 None.
- 경직·빙결은 후퇴를 끊고(기존 InterruptActivity), 공황 남은 시간이 16스텝 이상이면 풀린 뒤 재개된다.

### 2.6 MD-43(Flee 끝) — 권장 (b)+(c) + 체력 곡선, 적용 후 proposed
- `Goblin_Melee.json` Flee: args `"seconds": 1.5`, 행동 `"cooldown": 6.0`, 체력 고려를 `{ "input": "SelfHealthRatio", "curve": "Logistic", "m": -20, "c": 0.3 }`로 교체(약 32% 미만), 고려 추가 `{ "input": "DistanceToTarget", "curve": "Logistic", "m": -12, "c": 0.35, "range": [0, 1200] }`. Archer는 상속.
- `Hyena.json` Flee: `"seconds": 1.5`, `"cooldown": 6.0`, 체력 고려 같은 Logistic, `DistanceToTarget` Logistic m −12, c 0.15, range [0,1500](물기 접근 거리 약 225 안).
- 곡선 실제 값은 시뮬·`TDResponseCurve` 정의로 확인해 표에 남긴다.

### 2.7 자료
- 새 `MonsterAI/TDPackMorale.h/.cpp`: `enum class ETDPackFearState : uint8 { None, Pending, Retreating };` `struct FTDMonsterMoraleFragment { bool bIsImmune; bool bIsLeader; bool bHasRallied; bool bLeaderShock; bool bIsLeaderFear; ETDPackFearState FearState; uint64 LossSteps[4]; uint64 FearStartDeadlineStep; uint64 FearEndStep; int32 FearSteps; };` `struct FTDPackDeathEvent { FVector Location; uint32 VictimSimulationId; int32 TeamId; bool bWasLeader; };` `class FTDPackMorale`(상수 + 정적 `MakeFragment`, `RecordLoss`, `CountRecentLosses`, `ShouldPanic(int32 Loss, int32 Panicking, int32 Support)`, `ComputeFearSteps`, `GetAllyWeight(bool bIsImmune)`).
- 두뇌 조각: `float RetreatAnchorDistance = -1.f; int32 RetreatStallSteps = 0; bool bWasRetreatCornered = false;` + 경보 필드.
- 서브시스템: `TArray<FTDMonsterMoraleFragment> Morales; TArray<FTDPackDeathEvent> PendingPackDeaths; TArray<FTDPackDeathAlarm> PendingDeathAlarms; bool bShouldEvaluateMorale = false;` 공개 `bool IsMonsterPanicking(FTDMonsterSlotHandle) const`(Retreating일 때 참).
- 디버그 줄 끝 ` morale=<steady|fear|panic|rallied>`(fear = Pending 또는 Retreating인데 후퇴가 끊긴 중, panic = 시간 제한 후퇴 진행 중, rallied = bHasRallied·평상).

## 3. 역할 독점(M3-32)

### 3.1 역할·차선
- `ETDMonsterCombatRole`에 `Charger`, `Support` 덧붙임. 로더 `role` 허용값 4개(오류 문구 "expected Melee, Ranged, Charger or Support", `"role":"Boss"` 문구 유지). `ETDTacticsRole`에 `Charger = 3`, `Support = 4`.
- `MakeFragment`: bIsBoss → Boss / Charger → Charger / Support → Support / 나머지 M3-29 규칙.
- `FTDCombatTactics::IsMeleeLaneRole(Role)`(Melee·Charger), `IsRangedLaneRole(Role)`(Ranged·Support). 교체할 비교 전부: `GetTacticsPoolIndex`·`GetGrantRadiusCm`(익명 ns)·`AssignWaitSectors`·`AssignRangedSectors`·`AssignInnerSectors`(`IsInnerHolder`)·`ResolveTacticsSlotKind`·`IsAvoidingTacticsTarget`·`FillTacticsContext`. 수용: `rg -n "ETDTacticsRole::(Melee|Ranged)" Source/TDGame/MonsterAI --glob "!**/Tests/**"`가 MakeFragment·두 차선 함수 밖 0건.
- 데이터: Hyena는 **Melee 유지**(반박 B-3: 돌진자 지정 시 무리 공격 빈도 절반). 돌진자 지정은 시뮬 S4 결과와 함께 MD-48에서 결정. Support 종 없음.

### 3.2 독점 상한
- `GetExclusiveRoleCap(Role)`: Charger 1, Support 1, 그 밖 `MAX_int32`.
- `GrantPoolTokens`: 초과 회수 루프 뒤에 역할별 보유 수(Reserved·Committed, 이 기록)를 센다. `Pool.RoleBlockedCount = 0`으로 시작. 부여 루프 본문 순서: ① 후보 역할 보유 수 ≥ 상한 + (보복 중이면 1) → `RoleBlockedCount++`, 그 후보를 `RoleBlocked` 목록에 두고 continue ② 예산 부족 → break ③ 부여, HeldQuarters·역할 보유 수 증가.
- `RemainingCandidates`(보복 가로채기·OnDeck 입력)는 break 지점 뒤 후보 중 역할 상한에 걸리지 않은 것만. 가로채기에서 희생자 해제·후보 부여 뒤 역할 보유 수를 갱신하고, 후보가 상한에 걸리면(보복 +1 포함) 건너뛴다.
- `HasOtherTokenCandidate`는 같은 역할 상한에 걸린 후보를 세지 않는다.
- `FTDTacticsPool::RoleBlockedCount`, `HasMeleeQueue()`는 M3-29 식 `|| Pools[Melee].RoleBlockedCount > 0`.

### 3.3 원거리 띠(근접 전열 뒤)
- 고리 패스(`AssignWaitSectors` 끝): `Record.RangedBandFloorCm = 대기 칸을 받은 근접 차선 몬스터의 max(ComputeWaitRingRadius + 몸 반경)`, 대기자 없으면 0.
- 원거리 칸 문맥: `Context.TacticsBandFloor = clamp(RangedBandFloorCm + RangedBandGapCm(100) + 자기 몸 반경, 0, min(MaxReachCm − RangedReachMarginCm(100), RangedBandFloorCapCm(700)))`, 대기자 없으면 0.
- 실행기 `MoveToBand`(원거리 칸 문맥일 때): `BandMin = max(Args.BandMin, TacticsBandFloor)`, `BandMax = max(Args.BandMax, BandMin + RangedBandMinWidthCm(100))`. 안·밖 판정·칸 점 중간 반경(`ComputeTacticsSlotPoint`)에 같은 값.
- 예(시험 픽스처 몸 반경 40 가정): 골렘 사거리 300 → 고리 490 → 바닥 490+40+100+40 = 670 → 궁수 띠 [670, 800]. 고블린 → 고리 350 → 530 → 궁수 [530, 800].

## 4. 시험(빨강 먼저)

### 4.1 빨강 규칙
- M3-29 통합 트리에서 **컴파일되고 실패**. 새 심볼 금지. 허용: 디버그 줄 문자열, 역할 리터럴 `static_cast<ETDTacticsRole>(3|4)`, 공개 API(`GetMonsterDebugLines`·`IsMonsterEngaged`·`FTDMonsterActionExecutor::ApplyDecision`/`StepState`·`UTDMonsterDefinitionLibrary::ResolveDefinitionTexts`), 접근자.
- 빨강 단계에 헤더 한 줄 `friend struct FTDPackBehaviorTestAccess;`(`TDMonsterThinkSubsystem.h`, 동작 무관)를 허용한다. 접근자는 새 시험 파일에 정의하고 기존 private 멤버만 쓴다(Tactics[].Role 쓰기, OrderedSlots·Bodies 읽기, FindSlotForActor 같은 것).
- 사망은 치명 `ReceiveDamage`(가드 확인됨: `BroadcastDeathMessage`의 HasInstance, StartRagdoll은 PhysicsAsset 없으면 반환). 각 사기 시험 첫 단언 "치명 피해 뒤 디버그 줄 수 감소".

### 4.2 표(파일 `MonsterAI/Tests/TDPackBehaviorTests.cpp`, 접두 `TDGame.MonsterAI.Pack.`)

| ID | 이름 | 장면 | 단언(초록) | 구현 전 |
|---|---|---|---|---|
| A1 | AlarmRipplesTwoHops | 플레이어 (0,0) 정지, 고블린 A(1400,0)·B(1400,700)·C(1400,1400)·D(1400,2100) | tB−tA ∈ [8,16], tC−tB ∈ [8,16], D는 tC 뒤 3초 미교전(D–플레이어 > 1500 확인) | tB = tA, C 미교전 |
| A2 | OnlyFirstDiscovererCalls | 고블린 둘 (1300,±100) 동시 발견 + (1400,700) 하나(플레이어 1565, 스스로 발견 못 함) | 교전 뒤 `alarm=caller` 정확히 1줄, 낮은 SimulationId | 0줄 |
| A3 | TacticsOffKeepsInstantAlarm | A1 배치, CVar 0 | tB = tA, C 미교전 | 통과(보호) |
| A4 | OneShotKillAlertsNeighbors | 고블린 X(2000,0)·Y(2000,600) Home, 플레이어 (0,0)(둘 다 1500 밖), X에 치명 피해 | Y가 16스텝 안 교전 | Y Home 유지 |
| B1 | LeaderDeathScattersPack | StoneGolem 1 + 고블린 3, 집 근처에서 교전(플레이어 400~500), 3초 뒤 골렘 치명 | 1초 안 고블린 ≥ 2 `morale=panic`, 그중 ≥1이 MoveAway 후퇴 ≥ 32스텝, 4초 안 셋 모두 재교전 행동(시전 또는 대상 거리 ≤ 400), Returning 0 | panic 없음 |
| B2 | BurstLossesPanicSmallPack | 고블린 5, 0.5초 안 2마리 치명 | 남은 셋 중 ≥ 2 1초 안 panic(후퇴 ≥ 32스텝 실측), 각 후퇴 ≤ 2.3초(공황 128 + 여유), 4초 안 재교전 행동 | 실패 |
| B3 | LargePackHoldsNerve | 고블린 12, 2마리 치명 | 3초간 panic 0 | 통과(보호) |
| B3b | SlowLossesDoNotPanic | 고블린 5, 4초 간격 2마리 치명 | 6초간 panic 0 | 통과(보호) |
| B4 | TimedRetreatEndsOnTime | 인라인 JSON `Flee`(MoveAway, seconds 1.5) + `ApplyDecision` + `StepState`, 자기 위치를 스텝마다 대상 반대쪽으로 10cm(또 다른 표본 5cm) | 96±1스텝에 Idle | 계속 Move |
| B5 | TimedRetreatEndsWhenCornered | B4 정의, 자기 위치 고정 | 24~26스텝에 Idle | 계속 Move |
| B6 | LowHealthFleeDoesNotLoop | 고블린 1(체력 25%, 비치명 피해), 플레이어 300cm 정지, 30초 | Returning 전환 0, MoveAway 시간 ≤ 30%, 연속 MoveAway ≤ 1.7초, 시전 ≥ 8 | 포기·귀환 |
| C1 | ChargersTakeTurns | 고블린 4 등록 후 역할 리터럴 3, 플레이어 정지 20초 | 매 스텝 토큰 보유 돌진자 ≤ 1, 동시 시전 ≤ 1, 넷 모두 시전 ≥ 1 | 보유 2 |
| C2 | SupportCastsAlone | 궁수 3 역할 리터럴 4, 20초 | 보유 지원 ≤ 1, 동시 시전 ≤ 1, 셋 모두 시전 ≥ 1 | 보유 2 |
| C3 | RangedHoldsBandBehindMeleeRing | StoneGolem 4(600~700), 궁수 2를 방위 45°·105°(남쪽 1200~2400 데시도 피함, 원거리 칸 중심 15+30k°)·540cm에, 플레이어 정지 | 6초 뒤 궁수 거리 중앙값 ≥ 620, 590 미만 표본 ≤ 10%(v2.1: 궁수 KeepDistance 점수 곡선이 602~694cm에서 Idle보다 낮아 641에서 멈춤 — 시뮬·엔진 일치) | 540 부근 머묾(구현 전 실행 로그로 확인) |
| C4 | PackDefinitionKeys | 인라인 JSON `"role":"Charger"`·`"role":"Support"`·`"elite":true`(+ `"elite":"yes"` 오류) | 오류 0(정상 셋), 오류 문구(잘못된 것), `MakeFragment().Role`이 리터럴 3·4 | 미지 값·키 오류 |
| G1 | Deterministic | A1 + 고블린 5 소규모 사망·공황 + 리터럴 돌진자 2, 2회 | 스텝별 디버그 줄 해시 동일 | 통과(보호) |
| G2 | EventOrderInvariant | 같은 프레임 고블린 둘 피격·치명을 1→2, 2→1 순서로 | 두 실행 스텝별 해시 동일 | 통과 가능(보호) |

- 기존 `TDGame.MonsterAI.*`(M3-29 뒤) 통과 수 유지, 기준선 CrossingStreams. `FleeingMonsterDoesNotStallTokens`·`HitKitingIsCappedByMaxChase`·`KitingDoesNotYoYo`는 MD-43 데이터로 흔들릴 수 있다 → 데이터 변경 전후로 돌려 비교하고, 깨지면 전제(체력·거리)를 근거와 함께 조정.

## 5. 시뮬 재검증(sim_pack.py v2 규칙)
- S2 합격선: 꾸준 처치(1.5초당 1)에서 공황 발생 무리 비율 ≤ 1/3, 느림(4초당 1) 0, 범위(1초 3) ≥ 2/3. 공황 가시 시간(후퇴 중이며 대상 거리 ≤ 800) 평균 ≥ 0.8초.
- S3 합격선: 저체력 고블린 포기·귀환 0, MoveAway 시간 ≤ 30%, 공격 시작 ≥ 구 대비.
- S4: 리터럴 돌진자 하이에나 4·6의 초당 공격 시작 비(구 대비) — MD-48 근거. 골렘 + 궁수 거리 분포.
- S1: 화면 안 첫 도착 간격, 이웃 무리 기상 비율.

## 6. 남기는 결정(proposed)
- 07#MD-48: 무리 행동 기본값(경보 2단·8~16·호출 3초/1800·같은 종, 사기 창 3초·MinLoss 2·Support ≥ 2·면역 2배·전염 가중 1·공황 1~2초/우두머리 2~3초·속도 1.2·움츠림 700·결속 교전당 1회, 돌진자 1·지원 1·보복 +1·띠 간격 100+몸·바닥 상한 700), 데이터(StoneGolem·BookHead_Brute elite), Hyena 돌진자 지정 여부(권장 보류), 연출 연결 전 확인 사항(외침·움츠림 표현 없음).
- MD-43: (b)+(c)+체력 곡선 적용, proposed.

## 9. 반박 반영 표(요지)
A-K1 공황 시작 분리(2.4-3) · A-J1 위치 고정(0절) · A-J2 반경 분리 · A-J3/C-M1 비교 2곳 추가 · A-J4 루프 순서·남은 후보 · A-J5 G2 · A-J6/C-M2 초기화 · A-L1 시퀀스 밖 · A-L2/B-6 경직 뒤 재개 · A-L5 지지 대상 · A-L6 대상 유효 · A-L7 BeginReturn 정리 · A-L8 같은 팀 · A-L9 ResetTactics · A-L11 HasOtherTokenCandidate · B-1 고정 창·혼자 제외·교전당 1회 · B-2 Flee 체력 곡선·B6 강화 · B-3 Hyena Melee 유지·보복 +1 · B-4 속도 1.2·움츠림 700 · B-5 사망 경보 A4 · m-1 전염 가중 1 · m-2 다른 우두머리 생존 시 충격 없음 · m-3 우두머리 공포 128~192 · m-5 같은 종 호출 · m-6 간격 몸 반경·상한 700 · m-7 하이에나 c 0.15 · m-8 Support 면역 · C-X1 friend 한 줄 · C-X2 앵커 · C-M3 C3 방위 · C-M4 데이터 필수(elite) · C-M5 이름 접두 · C-M6 칸 제외 · C-M7 추적 초기화 · C-M8 기존 Flee 시험 비교.
보류: A-L3(공황 뒤 Flee 연쇄 — 체력 32% 미만에서만, 문서화), A-L4(전염은 목격자 재평가 — 설계 의도), A-L10(띠 바닥 > 700은 상한으로 차단), B-4 연출(후속), m-4(S1에서 측정).

## 10. v2.1 변경(시뮬 v2 재검증 뒤)
- `LossWindowSteps` 192 → 80. 시뮬 v2: 창 80(64~95 구간)에서 꾸준 처치 공황 무리 0/7, 범위 처치 6/7, 느림 0(우두머리 충격 제외), 공황 가시 1.34초. 창 192·512는 꾸준 6/7.
- C3 합격선 640/610 → 620/590(궁수 점수 곡선 정지 구간, 알려진 한계: 띠 바닥이 이동 띠만 옮기고 점수기 거리 곡선은 JSON 띠 중심 648 기준).
- 우두머리 공포 128~192 유지(B1형 복귀 최대 3.42초), B1 재교전 창 256 → 352스텝.
- 하이에나 Flee(c 0.15)는 무리 안에서 거의 발동하지 않음(단독 저체력·근접에서만) — 의도대로 둠.
