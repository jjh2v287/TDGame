#pragma once

#include "CoreMinimal.h"
#include "DefaultMovementSet/LayeredMoves/BasicLayeredMoves.h"
#include "TDApproachStepLayeredMove.generated.h"

namespace TDApproachStep
{
	constexpr float DurationSeconds = 0.2f;
	constexpr float IdealSurfaceDistanceCm = 60.f;
	constexpr float MaxStepCm = 110.f;
	constexpr float MaxBossStepCm = 40.f;
	constexpr float MaxHeightDifferenceCm = 40.f;
	constexpr double RecentHitSeconds = 0.3;
	constexpr uint8 LayeredMovePriority = 5;

	TDGAME_API float ComputeStepDistance(float SurfaceDistanceCm, bool bIsBossTarget);
}

USTRUCT()
struct TDGAME_API FTDApproachStepLayeredMove : public FLayeredMove_LinearVelocity
{
	GENERATED_BODY()

	FTDApproachStepLayeredMove();

	virtual bool SupportsAsync() const override { return false; }
	virtual FLayeredMoveBase* Clone() const override;
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FString ToSimpleString() const override;
};

template<>
struct TStructOpsTypeTraits<FTDApproachStepLayeredMove> : public TStructOpsTypeTraitsBase2<FTDApproachStepLayeredMove>
{
	enum
	{
		WithCopy = true
	};
};
