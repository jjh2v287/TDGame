# 무리 행동 3종 명세 v1 — 경보 연쇄(M3-30)·무리 사기와 공포 전염(M3-31, MD-43 정리)·역할 독점(M3-32)

작성: 2026-10-18 claude(부모 세션 c7fdf4가 위임). 기반 코드: 커밋 e785257d(M3-14·M3-28) + 통합 예정 M3-29(`Saved/AgentOps/20261018/m329/spec.md`: `ETDMonsterCombatRole{Unspecified,Melee,Ranged}`·`bIsBoss`·`ReserveCostQuarters`·가중 토큰). 이 명세는 M3-29 통합 뒤 트리를 전제로 한다.
근거 문헌: `Docs/MonsterAI_CombatSim/research/web-topdown-arpg-crowd-combat.md` §3.3~3.7·§6 순위 2~4, `web-squad-slots-and-directors.md` A2·A3.

## 0. 공통 규약(위반 시 불합격)

- 모든 판단은 `UTDMonsterThinkSubsystem` 고정 스텝(1/64초) 안. 처리 순서는 SimulationId 오름차순(또는 명시한 정렬 키 + SimulationId 동률 해소). 전역 난수(`FMath::FRand`·`FMath::RandRange` 등)·`TMap` 순회·월드 타이머 금지. 흔들림이 필요하면 SimulationId·StepIndex의 정수 식.
- 상태는 서브시스템 슬롯 배열(D14). 두뇌(유틸리티+FSM, D1)에는 기존처럼 스냅샷 입력·실행기 문맥만 넘긴다. BT·StateTree 금지(D4).
- `TD.MonsterAI.Tactics 0`이면 세 기능 모두 꺼지고 기존 동작(즉시 1단 경보, 사기 없음, 역할 독점 없음)과 같다. 단 MD-43 데이터(Flee `seconds`)는 실행기 규칙이라 CVar와 무관하게 적용된다.
- 새 코드에 주석 금지, 조기 반환, 이름 풀어 쓰기(AGENTS.md 3~5절). `TDMonsterThinkSubsystem.cpp`(약 2,300줄)에는 접착 코드만, 규칙은 새 파일의 정적 순수 함수에 둔다.

## 1. 경보 연쇄(Alert Ripple, M3-30)

### 1.1 현재
`UpdateEngagement`(`TDMonsterThinkSubsystem.cpp:602-704`): 이번 스텝 새로 교전한 몬스터(근접 발견 또는 피격)마다, 900cm 안 Home·재교전 차단 0·집이 대상 영역(3000) 안인 동료를 **같은 스텝에 즉시** 교전시킨다. 경보로 깨어난 몬스터는 다시 전파하지 않는다(1단).

### 1.2 새 규칙
- 발견자(0단): 근접 발견(EngageRadius 1500) 또는 피격으로 Home→Engaged가 된 몬스터. 같은 스텝 발견자는 SimulationId 순으로 처리한다.
- **호출자 표시(첫 발견자만)**: 발견자 D는 다음이 모두 거짓일 때만 `Caller`, 아니면 `Silent`. "활성 몬스터 M이 있어 M.AlarmCallStep ≠ 0, Now − M.AlarmCallStep < `AlarmCallMemorySteps`(192 = 3초), M.Target == D.Target, 평면 거리(M, D) ≤ `AlarmCallRadiusCm`(1800)". Caller는 `AlarmCallStep = Now`. (TLOU 비명, Hades II 밀도 비판: 머리 위 표시는 무리당 하나)
- 1단 예약: 발견자 D마다 OrderedSlots 순으로 Home·`ReengageBlockSteps == 0`·평면 거리(S, D) ≤ `AlertRadius`(900)·평면 거리(S.Home, Target) ≤ `LeashRadius`(3000)인 S를 `Due = Now + ComputeHopDelaySteps(거리)`, Hop 1로 예약한다.
- `ComputeHopDelaySteps(DistanceCm) = AlarmHopBaseDelaySteps(8) + AlarmHopDelaySpanSteps(8) × clamp(DistanceCm, 0, 900) / 900`(정수 나눗셈) → 8~16스텝. 가까운 동료가 먼저 깨어나 파문이 퍼지는 모양.
- 예약 처리: 매 스텝 `Due ≤ Now`인 항목을 (Due, SimulationId) 순으로 꺼내 검증(슬롯 세대 일치·활성·Home·`ReengageBlockSteps == 0`·대상 생존·집이 영역 안) 후 `Target = 예약 대상`, `Engage`, `AlarmRole = Follower`, `AlarmHop = Hop`. Hop < `MaxAlarmHops`(2)면 그 몬스터의 현재 위치 기준으로 다음 단(Hop+1)을 같은 규칙으로 예약한다. 2단에서 멈춘다.
- 중복: 같은 슬롯이 이미 예약돼 있으면 Due가 더 이른 쪽, 같으면 Hop이 작은 쪽을 남긴다. 예약된 몬스터가 스스로 발견하면 처리 시 검증에서 빠진다(이미 Engaged).
- 순서(UpdateEngagement 안): ① 기존 상태 루프(근접 발견 수집) ② 발견자 처리(호출자 판정 + 1단 예약, 발견자는 SimulationId 순 정렬) ③ 만기 예약 처리(새 예약은 Due ≥ Now+8이라 같은 스텝에 처리되지 않음).
- 정리: `UnregisterMonster`·`ResetTactics`(CVar 전환)·`Deinitialize`에서 해당 슬롯 예약 삭제/전체 비움. 귀환(Returning)으로 바뀐 몬스터의 `AlarmRole`은 None으로 되돌린다(`BeginReturn`).
- 표시 진입점: `ETDMonsterAlarmRole GetMonsterAlarmRole(FTDMonsterSlotHandle) const`(공개, 표현층이 머리 위 아이콘·외침을 붙일 자리). 디버그 줄 끝에 ` alarm=<none|caller|silent|hop1|hop2>`, `TD.MonsterAI.Tactics.Debug 1`이면 Caller 머리 위 "!" 문자열.

### 1.3 자료
- `TDMonsterBrainSlot.h`: `enum class ETDMonsterAlarmRole : uint8 { None, Caller, Silent, Follower };` 두뇌 조각에 `ETDMonsterAlarmRole AlarmRole = None; uint8 AlarmHop = 0; uint64 AlarmCallStep = 0;`
- 새 `MonsterAI/TDPackAlarm.h/.cpp`: `struct FTDPendingAlarm { int32 SlotIndex; uint32 Generation; uint32 SimulationId; TWeakObjectPtr<AActor> Target; uint64 DueStep; uint8 Hop; };` `class FTDPackAlarm`(상수 `MaxAlarmHops=2`·`AlarmHopBaseDelaySteps=8`·`AlarmHopDelaySpanSteps=8`·`AlarmCallMemorySteps=192`·`AlarmCallRadiusCm=1800`, 정적 `ComputeHopDelaySteps(int32)`, `IsAlarmDueBefore(const FTDPendingAlarm&, const FTDPendingAlarm&)`, `AddOrKeepEarlier(TArray<FTDPendingAlarm>&, const FTDPendingAlarm&)`).
- 서브시스템: `TArray<FTDPendingAlarm> PendingAlarms;`

## 2. 무리 사기·공포 전염(Pack Morale, M3-31) + Flee 끝 조건 정리(MD-43)

### 2.1 단계(Days Gone 5단계 단순화)
평상(Steady) → 공황(Panicked: 1~2초 후퇴) → 결속(Rallied: 10초 면역) → 평상. 판정은 Days Gone 공식 1의 정수 축약: "잃은 아군 + 공황 아군" 대 "남은 아군 지지".

### 2.2 면역·우두머리
- 면역 = `bIsBoss`(M3-29 키) 또는 새 최상위 JSON 키 `"elite": true`(불리언, 기본 false, `FTDResolvedMonsterDefinition::bIsElite`). 면역 개체는 공황에 빠지지 않고, 지지 계산에서 2배(`ImmuneAllyWeight=2`)로 센다(D3 감독관 함성 상쇄).
- 우두머리 = 면역 개체. 우두머리 사망은 반경 `LeaderWitnessRadiusCm`(1600) 안 같은 팀 교전 중 비면역 몬스터를 비율 판정 없이 공황시킨다(Halo 엘리트 사망).
- 데이터 권장(07#MD-48 제안): StoneGolem·BookHead_Brute `"elite": true`, Ogre_Boss는 이미 `boss`.

### 2.3 사망 수집
- `UnregisterMonster`에서 사망(액터 유효 + `IsActorAlive` 거짓)이고 전술 활성일 때 기존 `PendingKillLocations`(디렉터용)와 함께 `PendingPackDeaths`에 `{Location, VictimSimulationId, TeamId(정의 phase-0 stats.team), bWasLeader}`를 넣는다. 처리 전에 VictimSimulationId 순으로 정렬.

### 2.4 판정(스텝마다, 사망이 있었거나 직전 스텝에 공황이 시작됐을 때만 실행)
1. 사망 기록: 사망 d마다 OrderedSlots 순으로 목격자 W(활성·생존·Engaged·같은 팀·비면역·공황/결속 아님·평면 거리 ≤ `WitnessRadiusCm`(1200)): `RecentLossCount = (Now − LastLossStep ≤ LossMemorySteps(512)) ? +1 : 1`, `LastLossStep = Now`. d가 우두머리이고 거리 ≤ 1600이면 W에 `bLeaderShock = true`.
2. 스냅샷 판정(이번 스텝 시작 시점의 공황 집합 기준, 순서 무관): 후보 W(활성·생존·Engaged·비면역·공황/결속 아님)마다
   - `bLeaderShock`이면 공황.
   - 아니면 `RecentLossCount ≥ MinLossesForPanic(2)`이고 Now − LastLossStep ≤ 512일 때 `Pressure = 2 × (RecentLossCount + 공황 중 아군 수(반경 1200))`, `Support = 1 + Σ 비공황 아군(반경 1200, 면역 2·그 밖 1)`; `Pressure ≥ Support`면 공황.
3. 판정된 W를 모두 표시한 뒤(스냅샷) 한꺼번에 적용: `FearPendingUntilStep = Now + FearPendingSteps(64)`, `RecentLossCount = 0`, `bLeaderShock = false`.
4. 공황 시작: 대기 중인 W가 `Brain.CanThink()`(Idle/Move, 시퀀스 아님)·비빙결·비경직이면 `FTDMonsterActionExecutor::BeginRetreat(Brain, FearSteps, FearSpeedScale(1.0), Context)`. `FearSteps = FearMinSteps(64) + (SimulationId × 37 + Now) % (FearMaxSteps(128) − 64 + 1)`. 시전·시퀀스 중이면 끝날 때까지 대기, `FearPendingUntilStep` 지나면 취소. 시작 시 `RallyImmuneUntilStep = Now + FearMaxSteps + RallyImmuneSteps(640)`, `FearStartStep = Now`, 다음 스텝 판정을 켠다(전염).
- 예: 고블린 5마리 중 2마리가 8초 안에 죽으면 남은 3마리 각자 Pressure 4 ≥ Support 3 → 공황. 10마리 중 2마리 → Support 8 → 없음, 4마리 → 8 ≥ 6 → 공황. 골렘(정예) 곁 고블린 5 중 2 사망 → Support 5 > 4 → 버팀.

### 2.5 후퇴 에피소드(공황·Flee 공통 실행기 규칙)
- `FTDMonsterActionExecutor::BeginRetreat(Brain, Steps, SpeedScale, Context)`(공개): `InterruptActivity` 뒤 `ActivePrimitive = MoveAway`, `ActiveArgs.SpeedScale = SpeedScale`, `State = Move`, `StateStepsRemaining = Steps`, `CurrentActionIndex = INDEX_NONE`, `bForceThink = false`, 후퇴 추적 초기화.
- 시간 제한 후퇴 = 시퀀스 밖 `State == Move && ActivePrimitive == MoveAway && StateStepsRemaining > 0`. JSON `MoveAway`에 `seconds` > 0이면 시퀀스 밖에서도 이 모드로 시작한다(`StartPrimitive`). `seconds` 없는 MoveAway(Archer·Caster Backpedal)는 지금과 같다.
- `CanThink()`는 시간 제한 후퇴 중 거짓(사고 루프가 덮어쓰지 않음).
- 매 스텝(`StepState` Move): 남은 스텝 감소, 대상까지 평면 거리가 지금까지 최대보다 `RetreatProgressCm`(30) 넘게 늘면 정체 0, 아니면 정체 +1. 정체 ≥ `RetreatCorneredSteps`(24) 또는 남은 스텝 0 또는 대상 없음 → `CompleteActivity`(Idle·강제 사고). **막히면 계속 싸움**.
- 공황은 MoveAway라 기존 규칙대로 예약 토큰 회수·토큰 후보 제외(`RevokeInvalidReservation`·`IsTokenCandidate`). 공황 중 슬롯은 대기·안쪽·원거리 칸 배정에서 빠진다.

### 2.6 MD-43(Flee 끝 조건) — 권장 (b)+(c) 적용, proposed
- `Goblin_Melee.json` Flee: args `"seconds": 1.5` 추가, 행동 `"cooldown": 6.0`, 고려 `{ "input": "DistanceToTarget", "curve": "Logistic", "m": -12, "c": 0.35, "range": [0, 1200] }` 추가(대상 ≤ 약 400cm일 때만). Archer는 상속.
- `Hyena.json` Flee: `"seconds": 1.5`, `"cooldown": 6.0`, `DistanceToTarget` Logistic m −12, c 0.3, range [0,1500].
- 효과 목표: 저체력 고블린이 '도망 → 포기 → 귀가 → 재교전' 20초 주기 대신 1.5초 물러섰다 4.5초 이상 다시 싸운다. 막히면 즉시 싸운다.

### 2.7 자료
- 새 `MonsterAI/TDPackMorale.h/.cpp`: `struct FTDMonsterMoraleFragment { bool bIsImmune; bool bIsLeader; uint8 RecentLossCount; bool bLeaderShock; uint64 LastLossStep; uint64 FearPendingUntilStep; uint64 FearStartStep; uint64 RallyImmuneUntilStep; };` `struct FTDPackDeathEvent { FVector Location; uint32 VictimSimulationId; int32 TeamId; bool bWasLeader; };` `class FTDPackMorale`(상수, 정적 `MakeFragment(const FTDResolvedMonsterDefinition&)`, `ShouldPanic(int32 RecentLossCount, int32 PanickingAllyCount, int32 AllySupport)`, `ComputeFearSteps(uint32 SimulationId, uint64 StepIndex)`, `ComputeAllySupportWeight(bool bIsImmune)`).
- 두뇌 조각: `int32 RetreatStallSteps = 0; float RetreatBestDistance = -1.f;`
- 서브시스템: `TArray<FTDMonsterMoraleFragment> Morales;`(슬롯 병렬), `TArray<FTDPackDeathEvent> PendingPackDeaths;`, `bool bShouldEvaluateMorale`.
- 표시: 디버그 줄 ` morale=<steady|panic|rally>`(공황 = 시작 후 시간 제한 후퇴 진행 중 또는 대기), 공개 `bool IsMonsterPanicking(FTDMonsterSlotHandle) const`.

## 3. 역할 독점(Role Policy, M3-32)

### 3.1 역할
- `ETDMonsterCombatRole`(M3-29)에 `Charger`, `Support` 추가(뒤에 덧붙임). 로더 `role` 허용값 `Melee|Ranged|Charger|Support`(오류 문구·제안 목록 갱신, `"role":"Boss"` 문구는 유지).
- `ETDTacticsRole`에 `Charger = 3`, `Support = 4` 덧붙임(기존 0~2 값 유지). `MakeFragment`: bIsBoss → Boss / CombatRole Charger → Charger / Support → Support / 나머지는 M3-29 규칙.
- 차선: 근접 차선 = Melee·Charger(근접 풀·대기 고리·안쪽 칸), 원거리 차선 = Ranged·Support(원거리 풀·원거리 칸). `FTDCombatTactics::IsMeleeLaneRole(Role)`·`IsRangedLaneRole(Role)`로 기존 `Role == Melee`/`Role == Ranged` 비교(서브시스템 `GetTacticsPoolIndex`·`AssignWaitSectors`·`AssignRangedSectors`·`ResolveTacticsSlotKind`·`IsAvoidingTacticsTarget`·`FillTacticsContext`)를 바꾼다.
- 데이터 권장(07#MD-48): Hyena `"role": "Charger"`(무리 중 하나만 덤벼들고 나머지는 선회). Support 종은 아직 없음(규칙만, 시험은 역할 덮어쓰기).

### 3.2 독점 상한(TLOU 역할 토큰)
- `GetExclusiveRoleCap(Role)`: Charger 1, Support 1, 그 밖 무제한. 대상 기록마다 그 역할의 보유자(Reserved·Committed, TokenRecordId == 이 기록) 수가 상한이면 그 역할 후보는 이번 패스에서 **건너뛴다**(예산 부족 `break`와 다름: 낮은 순위 다른 역할 후보는 계속 받는다). 부여 루프 안에서 부여할 때마다 보유 수를 갱신한다.
- 보복 가로채기(`InterceptForRetaliation`): 상한에 걸린 후보는 제외. 대기 1순위(OnDeck)도 상한에 걸리지 않은 첫 후보.
- 상한으로 건너뛴 후보 수를 `FTDTacticsPool::RoleBlockedCount`에 둔다. `HasMeleeQueue()`는 M3-29 식 `|| Pools[Melee].RoleBlockedCount > 0`(막힌 돌진자도 대기 고리에서 선회).

### 3.3 원거리 띠 유지(근접 전열 뒤)
- 고리 패스(`AssignWaitSectors`) 끝에 `Record.RangedBandFloorCm = max(대기 칸을 받은 근접 차선 몬스터의 ComputeWaitRingRadius) + RangedBandGapCm(150)`, 대기자 없으면 0.
- 원거리 칸 문맥: `Context.TacticsBandFloor = min(Record.RangedBandFloorCm, Fragment.MaxReachCm − RangedReachMarginCm(100))`(음수면 0).
- 실행기 `MoveToBand`(원거리 칸일 때): `BandMin = max(Args.BandMin, TacticsBandFloor)`, `BandMax = max(Args.BandMax, BandMin + RangedBandMinWidthCm(100))`. 띠 안·밖 판정과 칸 점(`ComputeTacticsSlotPoint` 중간 반경)에 같은 값을 쓴다.
- 예: 골렘(근접 사거리 300) 대기 고리 490cm → 바닥 640 → 궁수 띠 [640, 800]. 고블린만이면 350 → 500 → 궁수 그대로.

## 4. 시험(빨강 먼저, 새 파일 `MonsterAI/Tests/TDPackBehaviorTests.cpp`, 접두사 `TDGame.MonsterAI.Pack.`)

빨강 규칙: M3-29 통합 트리(구현 전)에서 **컴파일되고 실패**해야 한다. 새 심볼 금지 — 디버그 줄 문자열(`alarm=caller`, `morale=panic`), 역할 리터럴 `static_cast<ETDTacticsRole>(3|4)`, 공개 API(`GetMonsterDebugLines`, `ApplyDecision`/`StepState` + 인라인 JSON), 기존 테스트 접근자 패턴만 쓴다. 사망은 치명 `UTDCombatComponent::ReceiveDamage`(L-combat-02: `BroadcastDeathMessage`가 이제 `HasInstance`로 가드됨 — 빌더가 먼저 확인).

| ID | 이름 | 장면 | 단언(초록 기준) | 구현 전 기대 |
|---|---|---|---|---|
| A1 | AlarmRipplesTwoHops | 플레이어 (0,0) 정지, 고블린 A(1400,0)·B(1400,700)·C(1400,1400)·D(1400,2100) | A 발견 tA, 8 ≤ tB−tA ≤ 16, 8 ≤ tC−tB ≤ 16, D는 tC 뒤 3초간 미교전(D–플레이어 > 1500 확인) | tB = tA, C 미교전 → 실패 |
| A2 | OnlyFirstDiscovererCalls | 고블린 둘 (1300,±100) 동시 발견 + 1단 하나 | 교전 뒤 줄 중 `alarm=caller` 정확히 1개, 낮은 SimulationId | 0개 → 실패 |
| A3 | TacticsOffKeepsInstantAlarm | A1 배치, CVar 0 | tB = tA, C 미교전(기존 동작) | 통과(보호 시험) |
| B1 | LeaderDeathScattersPack | StoneGolem 1 + 고블린 3 교전(플레이어 500 안팎), 3초 뒤 골렘 치명 피해 | 1초 안에 고블린 ≥ 2가 `morale=panic`, 4초 안에 셋 모두 다시 교전 행동(시전 또는 대상 거리 ≤ 400), Returning 0 | panic 없음 → 실패 |
| B2 | LossesPanicSmallPack | 고블린 4, 1초 안에 2마리 치명 | 남은 둘 1초 안 panic, 각 후퇴 ≤ 2.2초, 4초 안 재교전 행동 | 실패 |
| B3 | LargePackHoldsNerve | 고블린 12, 2마리 치명 | 3초간 panic 0 | 통과(보호) |
| B4 | TimedRetreatEndsOnTime | 인라인 JSON 정의 `Flee`(MoveAway, seconds 1.5) + `ApplyDecision` + 고정 문맥 `StepState`(자기 위치를 매 스텝 대상 반대쪽으로 10cm씩 옮김) | 96±1스텝에 Idle | 계속 Move → 실패 |
| B5 | TimedRetreatEndsWhenCornered | B4 정의, 자기 위치 고정 | 24~26스텝에 Idle | 실패 |
| B6 | LowHealthFleeDoesNotLoop | 고블린 1(체력 25%로 비치명 피해), 플레이어 400cm 정지, 30초 | Returning 전환 0, 연속 MoveAway ≤ 1.7초, 시전 ≥ 1 | 포기·귀환 반복 → 실패 |
| C1 | ChargersTakeTurns | 고블린 4 등록 후 역할 리터럴 3, 플레이어 정지 20초 | 매 스텝 토큰 보유 돌진자 ≤ 1, 동시 시전 ≤ 1, 넷 모두 시전 ≥ 1 | 보유 2 → 실패 |
| C2 | SupportCastsAlone | 궁수 3 역할 리터럴 4, 20초 | 보유 지원 ≤ 1, 동시 시전 ≤ 1, 셋 모두 시전 ≥ 1 | 보유 2 → 실패 |
| C3 | RangedHoldsBandBehindMeleeRing | StoneGolem 4(600~700)·궁수 2(540), 플레이어 정지 | 6초 뒤 궁수 거리 중앙값 ≥ 620, 590 미만 표본 ≤ 10% | 540 머묾 → 실패 |
| C4 | PackDefinitionKeys | 인라인 JSON `"role":"Charger"`·`"role":"Support"`·`"elite":true` 해석 + 출하 Hyena `MakeFragment().Role == 리터럴 3` | 오류 0, 역할 일치 | 미지 값·키 오류 → 실패 |
| G1 | Deterministic | A1 배치 + 고블린 4 소규모 사망·공황 + 돌진자 2, 2회 실행 | 스텝별 디버그 줄 해시 동일 | 통과(보호) |

기존 `TDGame.MonsterAI.*`(M3-29 뒤 기준) 통과 수 유지, 기준선 `CircleAvoidance.Scene.CrossingStreams` 1건 제외. `RoleClassification`의 Hyena 단언은 Charger로 갱신(명세 근거). `NoGlobalRandom`이 새 파일을 자동으로 훑는다.

## 5. 파이썬 결정론 시뮬(검증 질문)
`Docs/MonsterAI_CombatSim/measurements/squad-tactics/sim_pack.py`(sim_tactics.py의 SimulationV2를 가져와 확장, 프로젝트 소스 불변):
- S1 경보: 무리 배치(일렬 4·덩어리 12·두 덩어리 6+6, 간격 500~800) × {구 즉시 1단, 신 2단 지연} → 깨어나는 순서·시각, 단수, 무리당 호출자 수, 3단 이상 0.
- S2 사기: {고블린 3·5·8·12·20, 고블린 5+골렘, 하이에나 6} × 플레이어 처치 모형 {범위 처치(1초에 3), 꾸준(1.5초당 1), 느림(4초당 1)} × 벽 {없음, 반경 1200 원형 벽} → 공황 발생 수·전염 연쇄 길이·후퇴 시간 비율·공격 시작 수 변화·막힘 종료 비율·공황 반복(결속 뒤 재공황) 수.
- S3 MD-43: 저체력 고블린 1·3 × 플레이어 {정지, 추격, 카이팅} → 포기·귀환 반복 수, 도망 시간 비율, 공격 시작 수(구 무제한 Flee 대 신 1.5초·6초·근접 한정).
- S4 역할: 하이에나 4·6(돌진자 대 근접) → 동시 보유·초당 공격 시작·최장 무공격; 지원 3 → 동시 시전; 골렘 4 + 궁수 2 → 궁수 거리 분포 대 바닥.
- 상수 민감도: 홉 지연 {4~8, 8~16, 16~32}, MinLosses {2,3}, Fear {32~64, 64~128, 128~192}, Rally {320, 640, 1280}, Cornered {16, 24, 48}.

## 6. 남기는 결정(proposed)
- 07#MD-48: 무리 행동 기본값(경보 2단·8~16스텝·호출 3초/1800cm, 사기 공식·면역 2배·공황 1~2초·결속 10초, 돌진자 1·지원 1·띠 간격 150), 데이터(Hyena Charger, StoneGolem·BookHead_Brute elite).
- MD-43: 권장 (b)+(c) 적용으로 상태 갱신(proposed, 데이터만 되돌리면 원복).
