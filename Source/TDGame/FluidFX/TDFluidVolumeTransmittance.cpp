#include "FluidFX/TDFluidVolumeTransmittance.h"
#include "Async/ParallelFor.h"

namespace
{
	constexpr double MinimumLightDistanceCm = 1.0e-4;
	constexpr double MinimumDirectionComponent = 1.0e-6;

	struct FTDTransmittanceBox
	{
		FVector Size = FVector::ZeroVector;
		float HalfHeightCm = 0.0f;
		float HeightScaleCm = 0.0f;
		float TopFadeCm = 1.0f;
		float SoftFalloffCm = 1.0f;
	};

	bool HasValidInputs(const FTDFluidTransmittanceInputs& Inputs, int32 PixelCount)
	{
		const int32 Resolution = Inputs.SimResolution;
		return Resolution > 0
			&& PixelCount == Resolution * Resolution
			&& Inputs.ExtentCm.X > 0.0
			&& Inputs.ExtentCm.Y > 0.0
			&& Inputs.VolumeHeightCm > 0.0f
			&& Inputs.LightMode > 0;
	}

	FTDTransmittanceBox MakeBox(const FTDFluidTransmittanceInputs& Inputs)
	{
		FTDTransmittanceBox Box;
		Box.Size = FVector(Inputs.ExtentCm.X, Inputs.ExtentCm.Y, Inputs.VolumeHeightCm);
		Box.HalfHeightCm = 0.5f * Inputs.VolumeHeightCm;
		Box.HeightScaleCm = TDFluidVolumeTransmittance::HeightScaleRatio * Inputs.VolumeHeightCm * (Inputs.bIsSymmetric ? 0.5f : 1.0f);
		Box.TopFadeCm = FMath::Max(TDFluidVolumeTransmittance::TopFadeRatio * Inputs.VolumeHeightCm, UE_KINDA_SMALL_NUMBER);
		Box.SoftFalloffCm = FMath::Max(Inputs.SoftFalloffCm, 1.0f);
		return Box;
	}

	float SampleCoveredDensity(TConstArrayView<FFloat16Color> Pixels, int32 Resolution, float U, float V)
	{
		const float MaxCoordinate = static_cast<float>(Resolution - 1);
		const float GridX = FMath::Clamp(U * static_cast<float>(Resolution) - 0.5f, 0.0f, MaxCoordinate);
		const float GridY = FMath::Clamp(V * static_cast<float>(Resolution) - 0.5f, 0.0f, MaxCoordinate);
		const int32 X0 = FMath::FloorToInt(GridX);
		const int32 Y0 = FMath::FloorToInt(GridY);
		const int32 X1 = FMath::Min(X0 + 1, Resolution - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, Resolution - 1);
		const float FracX = GridX - static_cast<float>(X0);
		const float FracY = GridY - static_cast<float>(Y0);

		const FFloat16Color& Pixel00 = Pixels[Y0 * Resolution + X0];
		const FFloat16Color& Pixel10 = Pixels[Y0 * Resolution + X1];
		const FFloat16Color& Pixel01 = Pixels[Y1 * Resolution + X0];
		const FFloat16Color& Pixel11 = Pixels[Y1 * Resolution + X1];
		const float Density = FMath::Lerp(
			FMath::Lerp(Pixel00.R.GetFloat(), Pixel10.R.GetFloat(), FracX),
			FMath::Lerp(Pixel01.R.GetFloat(), Pixel11.R.GetFloat(), FracX),
			FracY);
		const float Alpha = FMath::Lerp(
			FMath::Lerp(Pixel00.A.GetFloat(), Pixel10.A.GetFloat(), FracX),
			FMath::Lerp(Pixel01.A.GetFloat(), Pixel11.A.GetFloat(), FracX),
			FracY);
		return Density * Alpha;
	}

	float ComputeEdgeFade(float U, float V)
	{
		const float EdgeDistance = FMath::Min(FMath::Min(U, 1.0f - U), FMath::Min(V, 1.0f - V));
		return FMath::SmoothStep(0.0f, TDFluidVolumeTransmittance::EdgeFadeFraction, EdgeDistance);
	}

	double ComputeBoxExitDistance(const FVector& Start, const FVector& Direction, const FVector& BoxSize)
	{
		double ExitDistance = TNumericLimits<double>::Max();
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Direction[Axis] > MinimumDirectionComponent)
			{
				ExitDistance = FMath::Min(ExitDistance, (BoxSize[Axis] - Start[Axis]) / Direction[Axis]);
			}
			else if (Direction[Axis] < -MinimumDirectionComponent)
			{
				ExitDistance = FMath::Min(ExitDistance, -Start[Axis] / Direction[Axis]);
			}
		}
		return ExitDistance;
	}

	float ComputeExtinction(const FTDFluidTransmittanceInputs& Inputs, const FTDTransmittanceBox& Box, TConstArrayView<FFloat16Color> Pixels, const FVector& Point)
	{
		const float U = static_cast<float>(Point.X / Box.Size.X);
		const float V = static_cast<float>(Point.Y / Box.Size.Y);
		if (U < 0.0f || U > 1.0f || V < 0.0f || V > 1.0f)
		{
			return 0.0f;
		}

		const float CoveredDensity = SampleCoveredDensity(Pixels, Inputs.SimResolution, U, V) * ComputeEdgeFade(U, V);
		const float Coverage = FMath::Clamp(CoveredDensity * TDFluidVolumeTransmittance::CoverageScale, 0.0f, 1.0f);
		if (Coverage <= 0.0f)
		{
			return 0.0f;
		}

		const float PointHeightCm = static_cast<float>(Point.Z);
		const float CenterDistanceCm = FMath::Abs(PointHeightCm - Box.HalfHeightCm);
		const float HeightOffsetCm = Inputs.bIsSymmetric ? CenterDistanceCm : PointHeightCm;
		const float TopMarginCm = Inputs.bIsSymmetric ? Box.HalfHeightCm - CenterDistanceCm : Inputs.VolumeHeightCm - PointHeightCm;
		const float SurfaceDepthCm = CoveredDensity * Box.HeightScaleCm - HeightOffsetCm;
		const float TopFade = FMath::Clamp(TopMarginCm / Box.TopFadeCm, 0.0f, 1.0f);
		return Inputs.DensityScale * Coverage * TopFade * FMath::Exp(FMath::Min(SurfaceDepthCm, 0.0f) / Box.SoftFalloffCm);
	}
}

int32 TDFluidVolumeTransmittance::ComputeOutputResolution(int32 SimResolution)
{
	return FMath::Max(MinimumResolution, FMath::DivideAndRoundUp(FMath::Max(SimResolution, 1), ResolutionDivisor));
}

float TDFluidVolumeTransmittance::ComputeLevelHeightCm(float VolumeHeightCm, int32 LevelIndex)
{
	return (static_cast<float>(LevelIndex) + 0.5f) / static_cast<float>(LevelCount) * VolumeHeightCm;
}

float TDFluidVolumeTransmittance::ComputeOpticalDepth(const FTDFluidTransmittanceInputs& Inputs, TConstArrayView<FFloat16Color> DensityPressurePixels, const FVector& StartLocalCm)
{
	if (!HasValidInputs(Inputs, DensityPressurePixels.Num()))
	{
		return 0.0f;
	}

	const bool bIsPointLight = Inputs.LightMode == PointLightMode;
	const FVector ToLight = bIsPointLight ? Inputs.LightPositionLocalCm - StartLocalCm : -Inputs.LightDirectionLocal;
	const double ToLightLength = ToLight.Size();
	if (!(ToLightLength > MinimumLightDistanceCm))
	{
		return 0.0f;
	}

	const FTDTransmittanceBox Box = MakeBox(Inputs);
	const FVector Direction = ToLight / ToLightLength;
	const double ExitDistance = ComputeBoxExitDistance(StartLocalCm, Direction, Box.Size);
	const double PathLength = FMath::Max(0.0, bIsPointLight ? FMath::Min(ToLightLength, ExitDistance) : ExitDistance);
	if (!(PathLength > 0.0) || !FMath::IsFinite(PathLength))
	{
		return 0.0f;
	}

	const int32 StepCount = FMath::Clamp(Inputs.MarchStepCount, MinimumMarchStepCount, MaximumMarchStepCount);
	const double StepLength = PathLength / static_cast<double>(StepCount);
	float OpticalDepth = 0.0f;
	for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
	{
		const FVector Point = StartLocalCm + Direction * ((static_cast<double>(StepIndex) + 0.5) * StepLength);
		OpticalDepth += ComputeExtinction(Inputs, Box, DensityPressurePixels, Point) * static_cast<float>(StepLength);
	}
	return OpticalDepth;
}

void TDFluidVolumeTransmittance::ComputePixels(const FTDFluidTransmittanceInputs& Inputs, TConstArrayView<FFloat16Color> DensityPressurePixels, TArray<FFloat16Color>& OutPixels)
{
	const int32 OutputResolution = ComputeOutputResolution(Inputs.SimResolution);
	OutPixels.SetNumZeroed(OutputResolution * OutputResolution);
	if (!HasValidInputs(Inputs, DensityPressurePixels.Num()))
	{
		return;
	}

	float LevelHeightsCm[LevelCount];
	for (int32 LevelIndex = 0; LevelIndex < LevelCount; ++LevelIndex)
	{
		LevelHeightsCm[LevelIndex] = ComputeLevelHeightCm(Inputs.VolumeHeightCm, LevelIndex);
	}

	FFloat16Color* Pixels = OutPixels.GetData();
	ParallelFor(TEXT("TDFluidVolumeTransmittanceRows"), OutputResolution, 4, [&](int32 Row)
	{
		const double LocalY = (static_cast<double>(Row) + 0.5) / static_cast<double>(OutputResolution) * Inputs.ExtentCm.Y;
		for (int32 Column = 0; Column < OutputResolution; ++Column)
		{
			const double LocalX = (static_cast<double>(Column) + 0.5) / static_cast<double>(OutputResolution) * Inputs.ExtentCm.X;
			float OpticalDepths[LevelCount];
			for (int32 LevelIndex = 0; LevelIndex < LevelCount; ++LevelIndex)
			{
				OpticalDepths[LevelIndex] = ComputeOpticalDepth(Inputs, DensityPressurePixels, FVector(LocalX, LocalY, LevelHeightsCm[LevelIndex]));
			}
			Pixels[Row * OutputResolution + Column] = FFloat16Color(FLinearColor(OpticalDepths[0], OpticalDepths[1], OpticalDepths[2], OpticalDepths[3]));
		}
	});
}
