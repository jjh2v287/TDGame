#pragma once

#include "CoreMinimal.h"
#include "DefaultMovementSet/LayeredMoves/BasicLayeredMoves.h"
#include "TDKnockbackLayeredMove.generated.h"

namespace TDKnockback
{
	TDGAME_API float ComputeScale(float ElapsedMs, float DurationMs);
}

USTRUCT()
struct TDGAME_API FTDKnockbackLayeredMove : public FLayeredMove_LinearVelocity
{
	GENERATED_BODY()

	FTDKnockbackLayeredMove();

	virtual bool SupportsAsync() const override { return false; }
	virtual bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove) override;
	virtual FLayeredMoveBase* Clone() const override;
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FString ToSimpleString() const override;
};

template<>
struct TStructOpsTypeTraits<FTDKnockbackLayeredMove> : public TStructOpsTypeTraitsBase2<FTDKnockbackLayeredMove>
{
	enum
	{
		WithCopy = true
	};
};
