#include "Characters/TDKnockbackLayeredMove.h"
#include "MoverTypes.h"

namespace TDKnockback
{
	constexpr uint8 KnockbackLayeredMovePriority = 10;

	float ComputeScale(const float ElapsedMs, const float DurationMs)
	{
		if (DurationMs <= 0.f)
		{
			return 0.f;
		}
		const float Remaining = 1.f - FMath::Clamp(ElapsedMs / DurationMs, 0.f, 1.f);
		return Remaining * Remaining;
	}

	float ComputeLinearScale(const float ElapsedMs, const float DurationMs)
	{
		if (DurationMs <= 0.f)
		{
			return 0.f;
		}
		return 1.f - FMath::Clamp(ElapsedMs / DurationMs, 0.f, 1.f);
	}
}

FTDKnockbackLayeredMove::FTDKnockbackLayeredMove()
{
	MixMode = EMoveMixMode::OverrideVelocity;
	Priority = TDKnockback::KnockbackLayeredMovePriority;
	FinishVelocitySettings.FinishVelocityMode = ELayeredMoveFinishVelocityMode::SetVelocity;
	FinishVelocitySettings.SetVelocity = FVector::ZeroVector;
}

bool FTDKnockbackLayeredMove::GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove)
{
	if (!FLayeredMove_LinearVelocity::GenerateMove(StartState, TimeStep, MoverComp, SimBlackboard, OutProposedMove))
	{
		return false;
	}
	const float ElapsedMs = static_cast<float>(TimeStep.BaseSimTimeMs - StartSimTimeMs);
	OutProposedMove.LinearVelocity *= bUsesLinearDecay ? TDKnockback::ComputeLinearScale(ElapsedMs, DurationMs) : TDKnockback::ComputeScale(ElapsedMs, DurationMs);
	return true;
}

FLayeredMoveBase* FTDKnockbackLayeredMove::Clone() const
{
	return new FTDKnockbackLayeredMove(*this);
}

UScriptStruct* FTDKnockbackLayeredMove::GetScriptStruct() const
{
	return FTDKnockbackLayeredMove::StaticStruct();
}

FString FTDKnockbackLayeredMove::ToSimpleString() const
{
	return TEXT("TDKnockback");
}
