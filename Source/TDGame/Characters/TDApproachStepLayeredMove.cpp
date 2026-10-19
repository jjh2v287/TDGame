#include "Characters/TDApproachStepLayeredMove.h"
#include "MoverTypes.h"

namespace TDApproachStep
{
	float ComputeStepDistance(const float SurfaceDistanceCm, const bool bIsBossTarget)
	{
		const float StepDistance = FMath::Clamp(SurfaceDistanceCm - IdealSurfaceDistanceCm, 0.f, MaxStepCm);
		return bIsBossTarget ? FMath::Min(StepDistance, MaxBossStepCm) : StepDistance;
	}
}

FTDApproachStepLayeredMove::FTDApproachStepLayeredMove()
{
	MixMode = EMoveMixMode::OverrideVelocity;
	Priority = TDApproachStep::LayeredMovePriority;
	FinishVelocitySettings.FinishVelocityMode = ELayeredMoveFinishVelocityMode::SetVelocity;
	FinishVelocitySettings.SetVelocity = FVector::ZeroVector;
}

FLayeredMoveBase* FTDApproachStepLayeredMove::Clone() const
{
	return new FTDApproachStepLayeredMove(*this);
}

UScriptStruct* FTDApproachStepLayeredMove::GetScriptStruct() const
{
	return FTDApproachStepLayeredMove::StaticStruct();
}

FString FTDApproachStepLayeredMove::ToSimpleString() const
{
	return TEXT("TDApproachStep");
}
