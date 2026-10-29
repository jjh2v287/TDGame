#pragma once

#include "CoreMinimal.h"
#include "TDFluidGpuSolverOptionSettings.h"
#include "TDFluidGpuSolverOptions.h"

struct FTDFluidKernelPressureGrid
{
	int32 Resolution = 0;
	const float* Pressure = nullptr;
	const float* Divergence = nullptr;
	const uint8* SolidMask = nullptr;
	int32 RadiusTexels = 4;
	int32 DivergenceRadiusTexels = 2;
	float Feedback = 0.98f;
	float DivergenceScale = 0.0f;
};

namespace TDFluidKernelPressure
{
	inline constexpr int32 MaximumTapCount = 2 * TDFluidGpuSolverOptionConstants::MaximumKernelRadiusTexels + 1;

	struct FTDFluidKernelPressureWeights
	{
		int32 RadiusTexels = 1;
		float Values[MaximumTapCount] = {};
	};

	inline FTDFluidKernelPressureWeights MakeKernelPressureWeights(int32 RequestedRadiusTexels)
	{
		FTDFluidKernelPressureWeights Weights;
		Weights.RadiusTexels = FMath::Clamp(RequestedRadiusTexels, 1, TDFluidGpuSolverOptionConstants::MaximumKernelRadiusTexels);
		for (int32 Offset = -Weights.RadiusTexels; Offset <= Weights.RadiusTexels; ++Offset)
		{
			Weights.Values[Offset + Weights.RadiusTexels] = ComputeTDFluidKernelWeight(Offset, Weights.RadiusTexels);
		}
		return Weights;
	}

	inline bool CanComputeKernelRow(const FTDFluidKernelPressureGrid& Grid, int32 Row)
	{
		return Grid.Resolution > 0
			&& Grid.Pressure != nullptr
			&& Grid.Divergence != nullptr
			&& Grid.SolidMask != nullptr
			&& Row >= 0
			&& Row < Grid.Resolution;
	}

	inline void ComputeHorizontalRow(const FTDFluidKernelPressureGrid& Grid, int32 Row, FVector4f* OutPartials)
	{
		if (OutPartials == nullptr || !CanComputeKernelRow(Grid, Row))
		{
			return;
		}

		const FTDFluidKernelPressureWeights PressureWeights = MakeKernelPressureWeights(Grid.RadiusTexels);
		const FTDFluidKernelPressureWeights DivergenceWeights = MakeKernelPressureWeights(Grid.DivergenceRadiusTexels);
		const int32 RowStart = Row * Grid.Resolution;
		for (int32 Column = 0; Column < Grid.Resolution; ++Column)
		{
			FVector4f Sums(0.0f, 0.0f, 0.0f, 0.0f);
			for (int32 Offset = -PressureWeights.RadiusTexels; Offset <= PressureWeights.RadiusTexels; ++Offset)
			{
				const int32 TapColumn = Column + Offset;
				if (TapColumn < 0 || TapColumn >= Grid.Resolution || Grid.SolidMask[RowStart + TapColumn] != 0)
				{
					continue;
				}

				const float Weight = PressureWeights.Values[Offset + PressureWeights.RadiusTexels];
				Sums.X += Weight * Grid.Pressure[RowStart + TapColumn];
				Sums.Y += Weight;
			}
			for (int32 Offset = -DivergenceWeights.RadiusTexels; Offset <= DivergenceWeights.RadiusTexels; ++Offset)
			{
				const int32 TapColumn = Column + Offset;
				if (TapColumn < 0 || TapColumn >= Grid.Resolution || Grid.SolidMask[RowStart + TapColumn] != 0)
				{
					continue;
				}

				const float Weight = DivergenceWeights.Values[Offset + DivergenceWeights.RadiusTexels];
				Sums.Z += Weight * Grid.Divergence[RowStart + TapColumn];
				Sums.W += Weight;
			}
			OutPartials[Column] = Sums;
		}
	}

	inline void ComputeVerticalRow(const FTDFluidKernelPressureGrid& Grid, const FVector4f* Partials, int32 Row, float* OutPressure)
	{
		if (OutPressure == nullptr || Partials == nullptr || !CanComputeKernelRow(Grid, Row))
		{
			return;
		}

		const FTDFluidKernelPressureWeights PressureWeights = MakeKernelPressureWeights(Grid.RadiusTexels);
		const FTDFluidKernelPressureWeights DivergenceWeights = MakeKernelPressureWeights(Grid.DivergenceRadiusTexels);
		const int32 RowStart = Row * Grid.Resolution;
		for (int32 Column = 0; Column < Grid.Resolution; ++Column)
		{
			if (Grid.SolidMask[RowStart + Column] != 0)
			{
				OutPressure[Column] = 0.0f;
				continue;
			}

			float PressureNumerator = 0.0f;
			float PressureDenominator = 0.0f;
			float DivergenceNumerator = 0.0f;
			float DivergenceDenominator = 0.0f;
			for (int32 Offset = -PressureWeights.RadiusTexels; Offset <= PressureWeights.RadiusTexels; ++Offset)
			{
				const int32 TapRow = Row + Offset;
				if (TapRow < 0 || TapRow >= Grid.Resolution)
				{
					continue;
				}

				const FVector4f& TapSums = Partials[TapRow * Grid.Resolution + Column];
				const float Weight = PressureWeights.Values[Offset + PressureWeights.RadiusTexels];
				PressureNumerator += Weight * TapSums.X;
				PressureDenominator += Weight * TapSums.Y;
			}
			for (int32 Offset = -DivergenceWeights.RadiusTexels; Offset <= DivergenceWeights.RadiusTexels; ++Offset)
			{
				const int32 TapRow = Row + Offset;
				if (TapRow < 0 || TapRow >= Grid.Resolution)
				{
					continue;
				}

				const FVector4f& TapSums = Partials[TapRow * Grid.Resolution + Column];
				const float Weight = DivergenceWeights.Values[Offset + DivergenceWeights.RadiusTexels];
				DivergenceNumerator += Weight * TapSums.Z;
				DivergenceDenominator += Weight * TapSums.W;
			}

			const float BlurredPressure = PressureDenominator > 0.0f ? PressureNumerator / PressureDenominator : 0.0f;
			const float BlurredDivergence = DivergenceDenominator > 0.0f ? DivergenceNumerator / DivergenceDenominator : 0.0f;
			OutPressure[Column] = Grid.Feedback * BlurredPressure - Grid.DivergenceScale * BlurredDivergence;
		}
	}
}
