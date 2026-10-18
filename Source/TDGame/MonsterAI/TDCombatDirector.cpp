#include "MonsterAI/TDCombatDirector.h"

void FTDCombatDirector::AddIntensity(FTDCombatDirectorState& State, const int32 Amount)
{
	State.Intensity = FMath::Clamp(State.Intensity + Amount, 0, MaxIntensity);
}

void FTDCombatDirector::ApplyHealthDrop(FTDCombatDirectorState& State, const int32 DropPermille, const uint64 StepIndex)
{
	AddIntensity(State, DropPermille * DamageIntensityScale);
	State.LastDamageStep = StepIndex;
	State.WindowDropPermille[1] += DropPermille;
}

void FTDCombatDirector::RunRingPass(FTDCombatDirectorState& State, const uint64 StepIndex, const int32 HealthPermille, uint64& InOutGrantFreezeUntilStep)
{
	if (StepIndex - State.LastDamageStep > static_cast<uint64>(QuietSteps))
	{
		AddIntensity(State, -QuietDecayIntensity);
	}
	if (State.WindowDropPermille[0] + State.WindowDropPermille[1] >= HeavyDamagePermille)
	{
		InOutGrantFreezeUntilStep = FMath::Max(InOutGrantFreezeUntilStep, StepIndex + HeavyDamageFreezeSteps);
	}
	State.WindowDropPermille[0] = State.WindowDropPermille[1];
	State.WindowDropPermille[1] = 0;
	State.Intensity = FMath::Clamp(State.Intensity, 0, MaxIntensity);
	AdvancePhase(State, StepIndex);
	UpdateMercy(State, StepIndex, HealthPermille, InOutGrantFreezeUntilStep);
}

int32 FTDCombatDirector::GetBudgetPercent(const ETDCombatDirectorPhase Phase)
{
	switch (Phase)
	{
	case ETDCombatDirectorPhase::PeakFade:
		return 75;
	case ETDCombatDirectorPhase::Relax:
		return 50;
	default:
		return 100;
	}
}

int32 FTDCombatDirector::GetStartSpacingSteps(const FTDCombatDirectorState& State)
{
	const int32 MercySpacing = State.bIsMercyOpen ? MercySpacingSteps : 0;
	switch (State.Phase)
	{
	case ETDCombatDirectorPhase::SustainPeak:
		return 16 + MercySpacing;
	case ETDCombatDirectorPhase::PeakFade:
		return 24 + MercySpacing;
	case ETDCombatDirectorPhase::Relax:
		return 32 + MercySpacing;
	default:
		return 20 + MercySpacing;
	}
}

const TCHAR* FTDCombatDirector::GetPhaseName(const ETDCombatDirectorPhase Phase)
{
	switch (Phase)
	{
	case ETDCombatDirectorPhase::SustainPeak:
		return TEXT("SustainPeak");
	case ETDCombatDirectorPhase::PeakFade:
		return TEXT("PeakFade");
	case ETDCombatDirectorPhase::Relax:
		return TEXT("Relax");
	default:
		return TEXT("BuildUp");
	}
}

void FTDCombatDirector::AdvancePhase(FTDCombatDirectorState& State, const uint64 StepIndex)
{
	switch (State.Phase)
	{
	case ETDCombatDirectorPhase::BuildUp:
		if (State.Intensity >= PeakIntensity)
		{
			EnterPhase(State, ETDCombatDirectorPhase::SustainPeak, StepIndex);
		}
		return;
	case ETDCombatDirectorPhase::SustainPeak:
		if (StepIndex >= State.PhaseUntilStep)
		{
			EnterPhase(State, ETDCombatDirectorPhase::PeakFade, StepIndex);
		}
		return;
	case ETDCombatDirectorPhase::PeakFade:
		if (State.Intensity <= FadeFloorIntensity || StepIndex >= State.PhaseUntilStep)
		{
			State.Intensity = FMath::Min(State.Intensity, FadeFloorIntensity);
			EnterPhase(State, ETDCombatDirectorPhase::Relax, StepIndex);
		}
		return;
	case ETDCombatDirectorPhase::Relax:
		if (StepIndex >= State.PhaseUntilStep)
		{
			State.Intensity = FMath::Min(State.Intensity, RelaxCeilingIntensity);
			EnterPhase(State, ETDCombatDirectorPhase::BuildUp, StepIndex);
		}
		return;
	}
}

void FTDCombatDirector::EnterPhase(FTDCombatDirectorState& State, const ETDCombatDirectorPhase Phase, const uint64 StepIndex)
{
	State.Phase = Phase;
	State.PhaseUntilStep = Phase == ETDCombatDirectorPhase::BuildUp ? 0 : StepIndex + PhaseSteps;
}

void FTDCombatDirector::UpdateMercy(FTDCombatDirectorState& State, const uint64 StepIndex, const int32 HealthPermille, uint64& InOutGrantFreezeUntilStep)
{
	if (State.bIsMercyOpen && StepIndex >= State.MercyUntilStep)
	{
		State.bIsMercyOpen = false;
	}
	if (HealthPermille < 0)
	{
		return;
	}
	if (HealthPermille > MercyRearmPermille)
	{
		State.bIsMercyArmed = true;
		return;
	}
	if (HealthPermille >= MercyOpenPermille || !State.bIsMercyArmed || StepIndex < State.MercyLockoutUntilStep)
	{
		return;
	}
	State.bIsMercyOpen = true;
	State.MercyUntilStep = StepIndex + MercySteps;
	InOutGrantFreezeUntilStep = FMath::Max(InOutGrantFreezeUntilStep, StepIndex + MercyFreezeSteps);
	State.bIsMercyArmed = false;
	State.MercyLockoutUntilStep = StepIndex + MercyLockoutSteps;
}
