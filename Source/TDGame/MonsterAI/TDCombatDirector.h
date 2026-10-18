#pragma once

#include "CoreMinimal.h"

enum class ETDCombatDirectorPhase : uint8
{
	BuildUp,
	SustainPeak,
	PeakFade,
	Relax
};

struct FTDCombatDirectorState
{
	int32 Intensity = 0;
	ETDCombatDirectorPhase Phase = ETDCombatDirectorPhase::BuildUp;
	uint64 PhaseUntilStep = 0;
	uint64 LastDamageStep = 0;
	int32 WindowDropPermille[2] = { 0, 0 };
	bool bIsMercyOpen = false;
	uint64 MercyUntilStep = 0;
	uint64 MercyLockoutUntilStep = 0;
	bool bIsMercyArmed = true;
};

class TDGAME_API FTDCombatDirector
{
public:
	static constexpr int32 MaxIntensity = 1000;
	static constexpr int32 PeakIntensity = 700;
	static constexpr int32 FadeFloorIntensity = 250;
	static constexpr int32 RelaxCeilingIntensity = 400;
	static constexpr int32 DamageIntensityScale = 2;
	static constexpr int32 KillIntensity = 40;
	static constexpr int32 KillRadiusCm = 1200;
	static constexpr int32 AttackStartIntensity = 8;
	static constexpr int32 QuietDecayIntensity = 40;
	static constexpr int32 QuietSteps = 64;
	static constexpr int32 HeavyDamagePermille = 160;
	static constexpr int32 HeavyDamageFreezeSteps = 64;
	static constexpr int32 PhaseSteps = 256;
	static constexpr int32 MercyOpenPermille = 250;
	static constexpr int32 MercyRearmPermille = 400;
	static constexpr int32 MercySteps = 256;
	static constexpr int32 MercyFreezeSteps = 32;
	static constexpr int32 MercyLockoutSteps = 1920;
	static constexpr int32 MercySpacingSteps = 12;
	static constexpr int32 OpeningGraceSteps = 96;

	static void AddIntensity(FTDCombatDirectorState& State, int32 Amount);
	static void ApplyHealthDrop(FTDCombatDirectorState& State, int32 DropPermille, uint64 StepIndex);
	static void RunRingPass(FTDCombatDirectorState& State, uint64 StepIndex, int32 HealthPermille, uint64& InOutGrantFreezeUntilStep);
	static int32 GetBudgetPercent(ETDCombatDirectorPhase Phase);
	static int32 GetStartSpacingSteps(const FTDCombatDirectorState& State);
	static const TCHAR* GetPhaseName(ETDCombatDirectorPhase Phase);

private:
	static void AdvancePhase(FTDCombatDirectorState& State, uint64 StepIndex);
	static void UpdateMercy(FTDCombatDirectorState& State, uint64 StepIndex, int32 HealthPermille, uint64& InOutGrantFreezeUntilStep);
	static void EnterPhase(FTDCombatDirectorState& State, ETDCombatDirectorPhase Phase, uint64 StepIndex);
};
