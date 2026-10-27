#include "TDFluidGpuMassInput.h"

namespace
{
	int32 WrapMassInputIndex(int32 Index, int32 Resolution)
	{
		const int32 Remainder = Index % Resolution;
		return Remainder < 0 ? Remainder + Resolution : Remainder;
	}

	float RoundHalfAwayFromZero(float Value)
	{
		return FMath::Sign(Value) * FMath::FloorToFloat(FMath::Abs(Value) + 0.5f);
	}

	bool TryResolveMassInputIndex(int32 Index, int32 Resolution, bool bWrapsArea, int32& OutIndex)
	{
		if (bWrapsArea)
		{
			OutIndex = WrapMassInputIndex(Index, Resolution);
			return true;
		}
		OutIndex = Index;
		return Index >= 0 && Index < Resolution;
	}

	uint32 FetchMassInputChannel(const TArray<uint32>& Accumulation, int32 Channel, int32 X, int32 Y, int32 InputResolution, bool bWrapsArea)
	{
		int32 ResolvedX = 0;
		int32 ResolvedY = 0;
		if (!TryResolveMassInputIndex(X, InputResolution, bWrapsArea, ResolvedX) || !TryResolveMassInputIndex(Y, InputResolution, bWrapsArea, ResolvedY))
		{
			return 0u;
		}
		const int64 Index = static_cast<int64>(Channel) * InputResolution * InputResolution + static_cast<int64>(ResolvedY) * InputResolution + ResolvedX;
		return Accumulation.IsValidIndex(Index) ? Accumulation[Index] : 0u;
	}

	float BilinearUnsigned(const uint32 Values[4], float FractionX, float FractionY)
	{
		const float Bottom = FMath::Lerp(static_cast<float>(Values[0]), static_cast<float>(Values[1]), FractionX);
		const float Top = FMath::Lerp(static_cast<float>(Values[2]), static_cast<float>(Values[3]), FractionX);
		return FMath::Lerp(Bottom, Top, FractionY);
	}

	float BilinearSigned(const uint32 Values[4], float FractionX, float FractionY)
	{
		const float Bottom = FMath::Lerp(static_cast<float>(static_cast<int32>(Values[0])), static_cast<float>(static_cast<int32>(Values[1])), FractionX);
		const float Top = FMath::Lerp(static_cast<float>(static_cast<int32>(Values[2])), static_cast<float>(static_cast<int32>(Values[3])), FractionX);
		return FMath::Lerp(Bottom, Top, FractionY);
	}
}

bool FTDFluidGpuMassInputRegistry::FindTarget(uint64 ActorKey, FTDFluidGpuMassInputTargetView& OutView) const
{
	const FTDFluidGpuMassInputTargetView* Found = Targets.Find(ActorKey);
	if (!Found)
	{
		return false;
	}
	OutView = *Found;
	return true;
}

void FTDFluidGpuMassInputRegistry::UpdateTarget(uint64 ActorKey, const FTDFluidGpuMassInputTargetView& View)
{
	Targets.Add(ActorKey, View);
}

void FTDFluidGpuMassInputRegistry::RemoveTarget(uint64 ActorKey)
{
	Targets.Remove(ActorKey);
}

void FTDFluidGpuMassInputRegistry::MarkAllTargetsNotReady()
{
	for (TPair<uint64, FTDFluidGpuMassInputTargetView>& Pair : Targets)
	{
		Pair.Value.bIsReady = false;
	}
}

int32 TDFluidGpuMassInput::ResolveDivisor(int32 RequestedDivisor)
{
	if (RequestedDivisor <= 1)
	{
		return 1;
	}
	return RequestedDivisor <= 2 ? 2 : 4;
}

int32 TDFluidGpuMassInput::ComputeInputResolution(int32 ResolutionTier, int32 Divisor)
{
	return FMath::Max(1, ResolutionTier / ResolveDivisor(Divisor));
}

uint32 TDFluidGpuMassInput::QuantizeUnit(float Value)
{
	const float Clamped = FMath::Clamp(Value, 0.0f, 1.0f);
	return static_cast<uint32>(FMath::FloorToFloat(Clamped * TDFluidGpuSolverConstants::MassInputDensityScale + 0.5f));
}

int32 TDFluidGpuMassInput::QuantizeVelocity(float Value)
{
	const float Scaled = FMath::Clamp(Value * TDFluidGpuSolverConstants::MassInputVelocityScale, -TDFluidGpuSolverConstants::MassInputQuantizedLimit, TDFluidGpuSolverConstants::MassInputQuantizedLimit);
	return static_cast<int32>(RoundHalfAwayFromZero(Scaled));
}

uint32 TDFluidGpuMassInput::QuantizeWeight(float Weight)
{
	const float Clamped = FMath::Clamp(Weight, 0.0f, 1.0f);
	return static_cast<uint32>(FMath::FloorToFloat(Clamped * TDFluidGpuSolverConstants::MassInputWeightScale + 0.5f));
}

uint32 TDFluidGpuMassInput::ComputeSafeCount(float MaxCellSpeedCmPerSec)
{
	const double Speed = FMath::Max(static_cast<double>(MaxCellSpeedCmPerSec), 1.0);
	const double VelocityLimited = FMath::FloorToDouble(2147483647.0 / (TDFluidGpuSolverConstants::MassInputVelocityScale * Speed));
	const double WeightLimited = FMath::FloorToDouble(4294967295.0 / TDFluidGpuSolverConstants::MassInputWeightScale);
	return static_cast<uint32>(FMath::Max(1.0, FMath::Min(VelocityLimited, WeightLimited)));
}

float TDFluidGpuMassInput::ResolveSplatRadius(float RadiusCm, const FVector2D& InputCellCm, int32 RadiusCapCells)
{
	const float MinimumRadius = 0.5f * static_cast<float>(FMath::Max(InputCellCm.X, InputCellCm.Y));
	const float CapCells = static_cast<float>(FMath::Clamp(RadiusCapCells, 1, TDFluidGpuSolverConstants::MaximumMassSplatRadiusCapCells));
	const float MaximumRadius = FMath::Max(MinimumRadius, CapCells * static_cast<float>(FMath::Min(InputCellCm.X, InputCellCm.Y)));
	if (!FMath::IsFinite(RadiusCm))
	{
		return MinimumRadius;
	}
	return FMath::Clamp(RadiusCm, MinimumRadius, MaximumRadius);
}

bool TDFluidGpuMassInput::SplatPointReference(TArray<uint32>& InOutAccumulation, int32 InputResolution, const FVector2D& InputCellCm, bool bWrapsArea, const FVector2D& LocalCm, const FVector2D& VelocityPlaneCmPerSec, float RadiusCm, float DensityTarget, float HeatTarget, bool bWritesVelocity)
{
	if (InputResolution <= 0 || RadiusCm <= 0.0f || InputCellCm.X <= 0.0 || InputCellCm.Y <= 0.0)
	{
		return false;
	}
	const int64 ChannelStride = static_cast<int64>(InputResolution) * InputResolution;
	if (InOutAccumulation.Num() < ChannelStride * TDFluidGpuSolverConstants::MassInputChannelCount)
	{
		return false;
	}

	const FVector2f Local(LocalCm);
	const FVector2f Cell(InputCellCm);
	const FVector2f Velocity(VelocityPlaneCmPerSec);
	const int32 MinX = FMath::FloorToInt32((Local.X - RadiusCm) / Cell.X);
	const int32 MinY = FMath::FloorToInt32((Local.Y - RadiusCm) / Cell.Y);
	const int32 MaxX = bWrapsArea ? FMath::Min(FMath::FloorToInt32((Local.X + RadiusCm) / Cell.X), MinX + InputResolution - 1) : FMath::FloorToInt32((Local.X + RadiusCm) / Cell.X);
	const int32 MaxY = bWrapsArea ? FMath::Min(FMath::FloorToInt32((Local.Y + RadiusCm) / Cell.Y), MinY + InputResolution - 1) : FMath::FloorToInt32((Local.Y + RadiusCm) / Cell.Y);
	bool bTouched = false;
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		int32 ResolvedY = 0;
		if (!TryResolveMassInputIndex(Y, InputResolution, bWrapsArea, ResolvedY))
		{
			continue;
		}
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			int32 ResolvedX = 0;
			if (!TryResolveMassInputIndex(X, InputResolution, bWrapsArea, ResolvedX))
			{
				continue;
			}
			const FVector2f Center((static_cast<float>(X) + 0.5f) * Cell.X, (static_cast<float>(Y) + 0.5f) * Cell.Y);
			const float Distance = (Center - Local).Length();
			if (Distance >= RadiusCm)
			{
				continue;
			}
			const float Falloff = 1.0f - Distance / RadiusCm;
			const float Weight = Falloff * Falloff * (3.0f - 2.0f * Falloff);
			const int64 Index = static_cast<int64>(ResolvedY) * InputResolution + ResolvedX;
			bTouched = true;
			InOutAccumulation[Index] = FMath::Max(InOutAccumulation[Index], QuantizeUnit(DensityTarget * Weight));
			if (HeatTarget > 0.0f)
			{
				InOutAccumulation[ChannelStride + Index] = FMath::Max(InOutAccumulation[ChannelStride + Index], QuantizeUnit(HeatTarget * Weight));
			}
			if (bWritesVelocity)
			{
				InOutAccumulation[ChannelStride * 2 + Index] += static_cast<uint32>(QuantizeVelocity(Weight * Velocity.X));
				InOutAccumulation[ChannelStride * 3 + Index] += static_cast<uint32>(QuantizeVelocity(Weight * Velocity.Y));
				InOutAccumulation[ChannelStride * 4 + Index] += QuantizeWeight(Weight);
			}
		}
	}
	return bTouched;
}

FTDFluidGpuMassInputSample TDFluidGpuMassInput::SampleReference(const TArray<uint32>& Accumulation, int32 InputResolution, const FVector2D& InputCellCm, bool bWrapsArea, const FVector2D& SamplePositionCm)
{
	FTDFluidGpuMassInputSample Sample;
	if (InputResolution <= 0 || InputCellCm.X <= 0.0 || InputCellCm.Y <= 0.0)
	{
		return Sample;
	}
	const float SampleX = static_cast<float>(SamplePositionCm.X / InputCellCm.X) - 0.5f;
	const float SampleY = static_cast<float>(SamplePositionCm.Y / InputCellCm.Y) - 0.5f;
	const int32 BaseX = FMath::FloorToInt32(SampleX);
	const int32 BaseY = FMath::FloorToInt32(SampleY);
	const float FractionX = SampleX - static_cast<float>(BaseX);
	const float FractionY = SampleY - static_cast<float>(BaseY);
	uint32 Values[TDFluidGpuSolverConstants::MassInputChannelCount][4];
	for (int32 Channel = 0; Channel < TDFluidGpuSolverConstants::MassInputChannelCount; ++Channel)
	{
		Values[Channel][0] = FetchMassInputChannel(Accumulation, Channel, BaseX, BaseY, InputResolution, bWrapsArea);
		Values[Channel][1] = FetchMassInputChannel(Accumulation, Channel, BaseX + 1, BaseY, InputResolution, bWrapsArea);
		Values[Channel][2] = FetchMassInputChannel(Accumulation, Channel, BaseX, BaseY + 1, InputResolution, bWrapsArea);
		Values[Channel][3] = FetchMassInputChannel(Accumulation, Channel, BaseX + 1, BaseY + 1, InputResolution, bWrapsArea);
	}
	Sample.Density = BilinearUnsigned(Values[0], FractionX, FractionY) / TDFluidGpuSolverConstants::MassInputDensityScale;
	Sample.Heat = BilinearUnsigned(Values[1], FractionX, FractionY) / TDFluidGpuSolverConstants::MassInputDensityScale;
	Sample.WeightedVelocity = FVector2D(BilinearSigned(Values[2], FractionX, FractionY), BilinearSigned(Values[3], FractionX, FractionY)) / TDFluidGpuSolverConstants::MassInputVelocityScale;
	Sample.Weight = BilinearUnsigned(Values[4], FractionX, FractionY) / TDFluidGpuSolverConstants::MassInputWeightScale;
	return Sample;
}

int64 TDFluidGpuMassInput::ComputeSliceBytes(int32 ResolutionTier, int32 Divisor)
{
	const int64 InputResolution = ComputeInputResolution(ResolutionTier, Divisor);
	return TDFluidGpuSolverConstants::MassInputChannelCount * InputResolution * InputResolution * static_cast<int64>(sizeof(uint32))
		+ TDFluidGpuSolverConstants::MassInputCounterCount * static_cast<int64>(sizeof(uint32));
}
