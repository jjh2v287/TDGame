# 계약 v2 — 빌더 3명(T 시험·I1 규칙·I2 커널)이 쓰는 이름·시그니처(명세 spec-v2.md의 7절)

사본 루트: `C:\Project\TDGame\Saved\AgentOps\20261018\pack-behaviors\staging\`(저장소와 같은 상대 경로). 기준 체크섬 `base.md5`(M3-29 통합 트리 14:57). 공용 트리는 T의 빨강 실행 절차 외에는 고치지 않는다. 각 파일의 줄 끝(CRLF/LF)을 그대로 유지한다. 새 C++에 주석 금지.

## 7.1 `MonsterAI/TDMonsterBrainSlot.h` (I1)
```cpp
enum class ETDMonsterAlarmRole : uint8 { None, Caller, Silent, Follower };   // ETDMonsterEngagement 아래
// FTDMonsterBrainFragment, ClosestReturnDistance 아래:
ETDMonsterAlarmRole AlarmRole = ETDMonsterAlarmRole::None;
uint8 AlarmHop = 0;
uint64 AlarmCallStep = 0;
float RetreatAnchorDistance = -1.f;
int32 RetreatStallSteps = 0;
bool bWasRetreatCornered = false;
bool IsInTimedRetreat() const;   // 본문: SequenceIndex == INDEX_NONE && State == Move && ActivePrimitive == MoveAway && StateStepsRemaining > 0
// CanThink() 본문 끝에 && !IsInTimedRetreat()
```

## 7.2 `MonsterAI/TDMonsterActionExecutor.h/.cpp` (I1)
- `FTDMonsterStepContext`에 `float TacticsBandFloor = 0.f;`(bIsFeinting 아래).
- public: `static constexpr int32 RetreatCorneredSteps = 24; static constexpr float RetreatProgressCm = 30.f;` `static void BeginRetreat(FTDMonsterBrainFragment& Brain, int32 Steps, float SpeedScale, float HoldDistanceCm, const FTDMonsterStepContext& Context);`
- private: `static void ResetRetreatTracking(FTDMonsterBrainFragment& Brain);` `static FTDMonsterMoveIntent StepTimedRetreat(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);`
- 규칙은 spec-v2 2.5 그대로: `StartPrimitive`의 MoveAway가 시퀀스 밖이고 `Args.Seconds > 0`이면 `StateStepsRemaining = max(1, SecondsToSteps(Seconds))` + `ResetRetreatTracking`. `StepState` Move 분기 맨 앞에서 `Brain.IsInTimedRetreat()`이면 `return StepTimedRetreat(Brain, Context);`. `StepTimedRetreat` 순서: 남은 스텝 −1(0 하한) → 대상 없음이면 CompleteActivity·빈 의도 → 평면 거리 d → `bIsHolding = StopAtDistance > 0 && d ≥ StopAtDistance` → 홀드 아니면 앵커(−1이면 d) 비교: d > 앵커 + 30이면 앵커 = d·정체 0, 아니면 정체 +1, 정체 ≥ 24면 `bWasRetreatCornered = true`·CompleteActivity·빈 의도 → 남은 스텝 0이면 CompleteActivity·빈 의도 → 홀드면 방향 0·`bShouldFaceTarget = true` → 아니면 방향 −ToTarget·SpeedScale = Args.SpeedScale·`bShouldFaceTarget = false`.
- `BeginRetreat`: `InterruptActivity` → `ActiveArgs = FTDActionArgs()`, `ActiveArgs.SpeedScale = SpeedScale`, `ActiveArgs.StopAtDistance = HoldDistanceCm`, `ActivePrimitive = MoveAway`, `State = Move`, `StateStepsRemaining = max(1, Steps)`, `CurrentActionIndex = INDEX_NONE`, `CurrentScore = 0`, `HoldSteps = 0`, `bForceThink = false`, `bWasRetreatCornered = false`, `ResetRetreatTracking`.
- `MoveToBand`와 `ComputeTacticsSlotPoint`: `Context.bIsTacticsActive && Context.TacticsSlotKind == ETDTacticsSlotKind::Ranged && Context.TacticsBandFloor > 0`일 때 `BandMin = max(Args.BandMin, TacticsBandFloor)`, `BandMax = max(Args.BandMax, BandMin + FTDCombatTactics::RangedBandMinWidthCm)`, 그 밖은 Args 그대로. 익명 ns 도우미 이름은 `ResolveTacticsBandMin`·`ResolveTacticsBandMax`(통합 빌드 충돌 방지).

## 7.3 새 `MonsterAI/TDPackAlarm.h/.cpp` (I1, 익명 ns 없음)
```cpp
#pragma once
#include "CoreMinimal.h"
struct FTDPendingAlarm { int32 SlotIndex = INDEX_NONE; uint32 Generation = 0; uint32 SimulationId = 0; TWeakObjectPtr<AActor> Target; uint64 DueStep = 0; uint8 Hop = 0; };
struct FTDPackDeathAlarm { FVector Location = FVector::ZeroVector; TWeakObjectPtr<AActor> Target; int32 TeamId = 0; uint32 SimulationId = 0; };
class TDGAME_API FTDPackAlarm
{
public:
	static constexpr int32 MaxAlarmHops = 2;
	static constexpr int32 AlarmHopBaseDelaySteps = 8;
	static constexpr int32 AlarmHopDelaySpanSteps = 8;
	static constexpr int32 AlarmHopRangeCm = 900;
	static constexpr int32 AlarmCallMemorySteps = 192;
	static constexpr int32 AlarmCallRadiusCm = 1800;
	static int32 ComputeHopDelaySteps(int32 DistanceCm);                       // 8 + 8 * clamp(d,0,900) / 900
	static bool IsAlarmDueBefore(const FTDPendingAlarm& Left, const FTDPendingAlarm& Right);   // (DueStep, SimulationId)
	static void AddOrKeepEarlier(TArray<FTDPendingAlarm>& Alarms, const FTDPendingAlarm& Alarm); // 같은 SlotIndex·Generation: Due 이른 쪽, 같으면 Hop 작은 쪽
	static void RemoveSlot(TArray<FTDPendingAlarm>& Alarms, int32 SlotIndex);
};
```

## 7.4 새 `MonsterAI/TDPackMorale.h/.cpp` (I1, 익명 ns 없음)
```cpp
#pragma once
#include "CoreMinimal.h"
struct FTDResolvedMonsterDefinition;
enum class ETDPackFearState : uint8 { None, Pending, Retreating };
struct FTDMonsterMoraleFragment
{
	static constexpr int32 LossSlotCount = 4;
	bool bIsImmune = false;
	bool bIsLeader = false;
	bool bHasRallied = false;
	bool bLeaderShock = false;
	bool bIsLeaderFear = false;
	ETDPackFearState FearState = ETDPackFearState::None;
	uint64 LossSteps[LossSlotCount] = {};
	uint64 FearStartDeadlineStep = 0;
	uint64 FearEndStep = 0;
	int32 FearSteps = 0;
};
struct FTDPackDeathEvent { FVector Location = FVector::ZeroVector; uint32 VictimSimulationId = 0; int32 TeamId = 0; bool bWasLeader = false; };
class TDGAME_API FTDPackMorale
{
public:
	static constexpr int32 WitnessRadiusCm = 1200;
	static constexpr int32 LeaderWitnessRadiusCm = 1600;
	static constexpr int32 LossWindowSteps = 192;
	static constexpr int32 MinLossesForPanic = 2;
	static constexpr int32 MinSupportForPanic = 2;
	static constexpr int32 ImmuneAllyWeight = 2;
	static constexpr int32 FearMinSteps = 64;
	static constexpr int32 FearMaxSteps = 128;
	static constexpr int32 LeaderFearMinSteps = 128;
	static constexpr int32 LeaderFearMaxSteps = 192;
	static constexpr int32 FearPendingSteps = 64;
	static constexpr int32 FearResumeMinSteps = 16;
	static constexpr float FearSpeedScale = 1.2f;
	static constexpr float PanicHoldDistanceCm = 700.f;
	static FTDMonsterMoraleFragment MakeFragment(const FTDResolvedMonsterDefinition& Definition);   // 면역 = bIsBoss || bIsElite || CombatRole == Support, 우두머리 = bIsBoss || bIsElite
	static void RecordLoss(FTDMonsterMoraleFragment& Fragment, uint64 StepIndex);                  // 가장 작은 칸에 덮어씀
	static int32 CountRecentLosses(const FTDMonsterMoraleFragment& Fragment, uint64 StepIndex);    // 값≠0 && StepIndex−값 ≤ LossWindowSteps
	static void ClearLosses(FTDMonsterMoraleFragment& Fragment);
	static bool ShouldPanic(int32 RecentLossCount, int32 PanickingAllyCount, int32 AllySupport);  // Loss≥2 && Support≥2 && 2*Loss+Panicking ≥ Support
	static int32 ComputeFearSteps(uint32 SimulationId, uint64 StepIndex, bool bIsLeaderShock);
	static int32 GetAllyWeight(bool bIsImmune);
	static bool IsInFear(const FTDMonsterMoraleFragment& Fragment);                                 // FearState != None
};
```

## 7.5 `MonsterAI/TDMonsterDefinition.h`·`TDMonsterDefinitionLibrary.cpp` (I1)
- `enum class ETDMonsterCombatRole : uint8 { Unspecified, Melee, Ranged, Charger, Support };`
- `FTDResolvedMonsterDefinition`: `bool bIsElite = false;`(bIsBoss 아래).
- 로더: `TopLevelKeys`에 `TEXT("elite")`. `CombatRoleNames`에 Charger·Support. role 문자열 매핑 4개. 문구 "expected Melee, Ranged, Charger or Support", 미지 값 문구도 4개로("a boss sets \"boss\": true" 부분 유지). `elite`는 `boss`와 같은 방식(불리언 아니면 `Reader.Error(TEXT("elite"), TEXT("expected true or false"))`).

## 7.6 `MonsterAI/TDCombatTactics.h/.cpp` (I1)
- `enum class ETDTacticsRole : uint8 { Melee, Ranged, Boss, Charger, Support };`
- `FTDTacticsPool`: `int32 RoleBlockedCount = 0;` / `FTDTargetTacticsRecord`: `int32 RangedBandFloorCm = 0;`, `HasMeleeQueue()`에 `|| Pools[MeleePoolIndex].RoleBlockedCount > 0`.
- 상수: `MaxChargerHolders = 1`, `MaxSupportHolders = 1`, `RangedBandGapCm = 100`, `RangedReachMarginCm = 100`, `RangedBandFloorCapCm = 700`, `RangedBandMinWidthCm = 100`.
- 정적: `static bool IsMeleeLaneRole(ETDTacticsRole Role);`(Melee·Charger) `static bool IsRangedLaneRole(ETDTacticsRole Role);`(Ranged·Support) `static int32 GetExclusiveRoleCap(ETDTacticsRole Role);`(Charger 1·Support 1·그 밖 MAX_int32) `static int32 ComputeRangedBandFloor(int32 RecordFloorCm, int32 BodyRadiusCm, int32 MaxReachCm);`(RecordFloor ≤ 0이면 0, 아니면 clamp(RecordFloor + Gap + Body, 0, min(MaxReach − Margin, Cap)))
- `MakeFragment` 역할: bIsBoss → Boss / CombatRole Charger → Charger / Support → Support / Melee → Melee / Ranged → Ranged / 그 밖 기존 추정.

## 7.7 JSON (I1)
- Goblin_Melee·Hyena Flee: spec-v2 2.6 그대로(SelfHealthRatio 고려를 `{ "input": "SelfHealthRatio", "curve": "Logistic", "m": -20, "c": 0.3 }`로 교체 — `range` 키가 없으면 기본 0~1인지 로더로 확인, 필요하면 `"range": [0, 1]`).
- StoneGolem·BookHead_Brute: `"role": "Melee",` 다음 줄(또는 같은 줄 뒤)에 `"elite": true,`.

## 7.8 `MonsterAI/TDMonsterThinkSubsystem.h/.cpp` (I2 전용)
- include `MonsterAI/TDPackAlarm.h`·`MonsterAI/TDPackMorale.h`. `friend struct FTDPackBehaviorTestAccess;`(기존 friend 아래).
- public: `ETDMonsterAlarmRole GetMonsterAlarmRole(FTDMonsterSlotHandle Handle) const;` `bool IsMonsterPanicking(FTDMonsterSlotHandle Handle) const;`(FearState == Retreating && 두뇌 IsInTimedRetreat)
- private 멤버: `TArray<FTDMonsterMoraleFragment> Morales; TArray<FTDPendingAlarm> PendingAlarms; TArray<FTDPackDeathEvent> PendingPackDeaths; TArray<FTDPackDeathAlarm> PendingDeathAlarms; bool bShouldEvaluateMorale = false;` private 함수 이름은 I2 재량(동사형, 아래 규칙 이름과 겹치지 않게).
- 커널: spec-v2 0절 순서, 1절 경보, 2절 사기, 3절 역할·띠, 2.7 디버그. 디버그 줄은 기존 `MakeTacticsDebugSuffix` 뒤에 ` alarm=…` ` morale=…`를 덧붙인다(공백 구분, 전술 꺼짐이면 alarm=none morale=steady).

## 7.9 시험 `MonsterAI/Tests/TDPackBehaviorTests.cpp` (T 전용)
- 접근자 `struct FTDPackBehaviorTestAccess`는 이 파일에 정의(기존 private 멤버만 사용: Tactics·Bodies·Brains·OrderedSlots·StepIndex). 익명 ns 이름은 Pack 접두.
- 빨강 실행 때만 공용 트리 `TDMonsterThinkSubsystem.h`에 friend 한 줄을 넣었다가 되돌린다(T 절차 참고).
