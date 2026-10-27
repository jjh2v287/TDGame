#include "FluidFX/TDFluidMassInputDataChannelReader.h"

#include "NiagaraDataChannel.h"

int32 FTDFluidMassInputDataChannelReader::ReadPoints(
	const UObject* WorldContextObject,
	const UNiagaraDataChannel* DataChannel,
	const FNiagaraDataChannelSearchParameters& SearchParameters,
	bool bReadPreviousFrame,
	int32 MaxPoints,
	float DefaultRadiusCm,
	TArray<FTDFluidMassInputPoint>& OutPoints)
{
	OutPoints.Reset();
	if (!WorldContextObject || !DataChannel || MaxPoints <= 0)
	{
		return 0;
	}

	if (!BeginRead(WorldContextObject, DataChannel, SearchParameters, bReadPreviousFrame))
	{
		return 0;
	}

	const int32 ReadCount = FMath::Min(Num(), MaxPoints);
	OutPoints.Reserve(ReadCount);
	for (int32 PointIndex = 0; PointIndex < ReadCount; ++PointIndex)
	{
		TNiagaraTypeHelper<FNiagaraPosition>::TLWCType Position = TNiagaraTypeHelper<FNiagaraPosition>::TLWCType::ZeroVector;
		if (!ReadPosition(PointIndex, Position))
		{
			continue;
		}

		TNiagaraTypeHelper<FVector>::TLWCType Velocity = TNiagaraTypeHelper<FVector>::TLWCType::ZeroVector;
		TNiagaraTypeHelper<float>::TLWCType RadiusCm = DefaultRadiusCm;
		TNiagaraTypeHelper<float>::TLWCType DensityStrength = 1.0f;
		TNiagaraTypeHelper<float>::TLWCType HeatStrength = 0.0f;
		ReadVelocity(PointIndex, Velocity);
		ReadRadiusCm(PointIndex, RadiusCm);
		ReadDensityStrength(PointIndex, DensityStrength);
		ReadHeatStrength(PointIndex, HeatStrength);

		FTDFluidMassInputPoint& Point = OutPoints.AddDefaulted_GetRef();
		Point.WorldLocation = FVector(Position);
		Point.WorldVelocity = FVector(Velocity);
		Point.RadiusCm = FMath::IsFinite(static_cast<float>(RadiusCm)) && RadiusCm > 0.0f ? static_cast<float>(RadiusCm) : DefaultRadiusCm;
		Point.DensityStrength = FMath::IsFinite(static_cast<float>(DensityStrength)) ? static_cast<float>(DensityStrength) : 0.0f;
		Point.HeatStrength = FMath::IsFinite(static_cast<float>(HeatStrength)) ? static_cast<float>(HeatStrength) : 0.0f;
	}

	EndRead();
	return OutPoints.Num();
}
