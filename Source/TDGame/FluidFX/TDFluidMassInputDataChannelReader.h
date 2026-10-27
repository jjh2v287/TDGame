#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "NiagaraDataChannelAccessor.h"

class UNiagaraDataChannel;

struct FTDFluidMassInputDataChannelReader : public FNDCReaderBase
{
	NDCVarReader(FNiagaraPosition, Position);
	NDCVarReaderOptional(FVector, Velocity);
	NDCVarReaderOptional(float, RadiusCm);
	NDCVarReaderOptional(float, DensityStrength);
	NDCVarReaderOptional(float, HeatStrength);

	int32 ReadPoints(
		const UObject* WorldContextObject,
		const UNiagaraDataChannel* DataChannel,
		const FNiagaraDataChannelSearchParameters& SearchParameters,
		bool bReadPreviousFrame,
		int32 MaxPoints,
		float DefaultRadiusCm,
		TArray<FTDFluidMassInputPoint>& OutPoints);
};
