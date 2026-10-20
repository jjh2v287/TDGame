#include "FluidFX/TDFluidGrid2DSolver.h"
#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	constexpr float BrushVelocityResponsePerSec = 12.0f;
	constexpr float MaxCellSpeedCmPerSec = 1200.0f;
	constexpr float BorderVelocityDampingPerSec = 4.0f;
	constexpr float BrushDisplacementRatio = 0.5f;
	constexpr float BrushEraseRatePerSec = 8.0f;
	constexpr float PressureNormalizationCmPerSec = 600.0f;
	constexpr float PressureReferenceCellSizeCm = 50.0f;
	constexpr float OutputSpeedReferenceCmPerSec = 600.0f;
	constexpr double FallbackExtentCm = 4000.0;
	constexpr int32 MinGridResolution = 16;
	constexpr int32 MaxGridResolution = 256;
	constexpr uint32 FlowNoiseSeed = 0x2F6E2B1Du;
	constexpr uint32 AmbientNoiseSeed = 0x6B43A9B5u;
	constexpr float AmbientNoiseScaleMultiplier = 1.7f;
	constexpr double NoiseFrequencyMinimum = 0.5;
	constexpr int32 NoiseRowsPerBatch = 16;
	constexpr int32 NoiseLatticeInlineCount = 1024;

	TAutoConsoleVariable<int32> CVarSolverRowParallel(
		TEXT("TD.FluidFX.SolverRowParallel"),
		1,
		TEXT("Run the heavy FluidFX solver passes as parallel row batches with bit-identical results. 0 = One thread, 1 = Parallel."));

	float ClampFinite(float Value, float MinValue, float MaxValue)
	{
		if (!FMath::IsFinite(Value))
		{
			return MinValue;
		}
		return FMath::Clamp(Value, MinValue, MaxValue);
	}

	double ClampExtentCm(double Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return FallbackExtentCm;
		}
		return FMath::Max(Value, 100.0);
	}

	void SanitizeConfig(FTDFluidSimulationConfig& InOutConfig)
	{
		InOutConfig.GridResolution = FMath::Clamp(InOutConfig.GridResolution, MinGridResolution, MaxGridResolution);
		InOutConfig.SimExtentCm.X = ClampExtentCm(InOutConfig.SimExtentCm.X);
		InOutConfig.SimExtentCm.Y = ClampExtentCm(InOutConfig.SimExtentCm.Y);
		InOutConfig.VelocityDamping = ClampFinite(InOutConfig.VelocityDamping, 0.0f, 10.0f);
		InOutConfig.DensityDissipation = ClampFinite(InOutConfig.DensityDissipation, 0.0f, 10.0f);
		InOutConfig.PressureIterations = FMath::Clamp(InOutConfig.PressureIterations, 1, 32);
		InOutConfig.VorticityStrength = ClampFinite(InOutConfig.VorticityStrength, 0.0f, 20.0f);
		InOutConfig.TerrainSlopeGravity = ClampFinite(InOutConfig.TerrainSlopeGravity, 0.0f, 1000000.0f);
		InOutConfig.AdvectionSpeedScale = ClampFinite(InOutConfig.AdvectionSpeedScale, 0.0f, 10.0f);
		InOutConfig.BorderFadeRatio = ClampFinite(InOutConfig.BorderFadeRatio, 0.0f, 0.49f);
		InOutConfig.MaxPointsPerTick = FMath::Clamp(InOutConfig.MaxPointsPerTick, 1, 512);
		InOutConfig.AmbientDensity = ClampFinite(InOutConfig.AmbientDensity, 0.0f, 1.0f);
		InOutConfig.AmbientRefillPerSec = ClampFinite(InOutConfig.AmbientRefillPerSec, 0.0f, 100.0f);
		InOutConfig.ActorBrushDensityStrength = FMath::IsFinite(InOutConfig.ActorBrushDensityStrength) ? FMath::Clamp(InOutConfig.ActorBrushDensityStrength, -10.0f, 10.0f) : 0.0f;
		InOutConfig.ActorBrushPunctureCmPerSec = FMath::IsFinite(InOutConfig.ActorBrushPunctureCmPerSec) ? FMath::Clamp(InOutConfig.ActorBrushPunctureCmPerSec, -MaxCellSpeedCmPerSec, MaxCellSpeedCmPerSec) : 0.0f;
		InOutConfig.FlowNoiseAccelerationCmPerSec2 = ClampFinite(InOutConfig.FlowNoiseAccelerationCmPerSec2, 0.0f, 10000.0f);
		InOutConfig.FlowNoiseScaleCm = ClampFinite(InOutConfig.FlowNoiseScaleCm, 1.0f, 100000.0f);
		InOutConfig.FlowNoiseSpeed = ClampFinite(InOutConfig.FlowNoiseSpeed, 0.0f, 100.0f);
		InOutConfig.AmbientNoiseAmount = ClampFinite(InOutConfig.AmbientNoiseAmount, 0.0f, 1.0f);
		InOutConfig.BuoyancyCmPerSec2 = ClampFinite(InOutConfig.BuoyancyCmPerSec2, 0.0f, 20000.0f);
		InOutConfig.OutflowBandRatio = ClampFinite(InOutConfig.OutflowBandRatio, 0.0f, 0.45f);
		InOutConfig.OutflowRatePerSec = ClampFinite(InOutConfig.OutflowRatePerSec, 0.0f, 50.0f);
		InOutConfig.WaterDepthReferenceCm = ClampFinite(InOutConfig.WaterDepthReferenceCm, 1.0f, 100000.0f);
		InOutConfig.FlowGuideResponsePerSec = ClampFinite(InOutConfig.FlowGuideResponsePerSec, 0.0f, 100.0f);
		InOutConfig.FlowGuideBankFalloffCm = ClampFinite(InOutConfig.FlowGuideBankFalloffCm, 0.0f, 10000.0f);
		InOutConfig.FlowGuideBankSpeedRatio = ClampFinite(InOutConfig.FlowGuideBankSpeedRatio, 0.0f, 1.0f);
		InOutConfig.RapidsWhitewaterPerSec = ClampFinite(InOutConfig.RapidsWhitewaterPerSec, 0.0f, 1000.0f);
		InOutConfig.WhitewaterFromVorticity = ClampFinite(InOutConfig.WhitewaterFromVorticity, 0.0f, 100.0f);
		InOutConfig.WhitewaterVorticityThresholdPerSec = ClampFinite(InOutConfig.WhitewaterVorticityThresholdPerSec, 0.0f, 1000.0f);
		InOutConfig.RippleWaveSpeedCmPerSec = ClampFinite(InOutConfig.RippleWaveSpeedCmPerSec, 0.0f, 2000.0f);
		InOutConfig.RippleDampingPerSec = ClampFinite(InOutConfig.RippleDampingPerSec, 0.0f, 100.0f);
		InOutConfig.RippleHeightReferenceCm = ClampFinite(InOutConfig.RippleHeightReferenceCm, 0.01f, 1000.0f);
		InOutConfig.RippleFlowAdvection = ClampFinite(InOutConfig.RippleFlowAdvection, 0.0f, 4.0f);
		InOutConfig.ActorBrushRippleImpulseCm = ClampFinite(InOutConfig.ActorBrushRippleImpulseCm, -100.0f, 100.0f);
		InOutConfig.CoastWaveDepthSpacingCm = ClampFinite(InOutConfig.CoastWaveDepthSpacingCm, 1.0f, 100000.0f);
		InOutConfig.CoastWavePeriodSeconds = ClampFinite(InOutConfig.CoastWavePeriodSeconds, 0.1f, 1000.0f);
		InOutConfig.CoastWaveMaxDepthCm = ClampFinite(InOutConfig.CoastWaveMaxDepthCm, 0.0f, 100000.0f);
		InOutConfig.SwashHeightCm = ClampFinite(InOutConfig.SwashHeightCm, 0.0f, 1000.0f);
		InOutConfig.BankWetHeightCm = ClampFinite(InOutConfig.BankWetHeightCm, 0.0f, 1000.0f);
		InOutConfig.WetnessDryingSeconds = ClampFinite(InOutConfig.WetnessDryingSeconds, 0.01f, 10000.0f);
		InOutConfig.WetnessFromWhitewater = ClampFinite(InOutConfig.WetnessFromWhitewater, 0.0f, 1.0f);
		InOutConfig.CpuFallbackResolution = FMath::Clamp(InOutConfig.CpuFallbackResolution, 0, MaxGridResolution);
		InOutConfig.HeatCoolingPerSec = ClampFinite(InOutConfig.HeatCoolingPerSec, 0.0f, 50.0f);
		InOutConfig.HeatBuoyancyCmPerSec2 = ClampFinite(InOutConfig.HeatBuoyancyCmPerSec2, 0.0f, 20000.0f);
		InOutConfig.EdgeBounciness = ClampFinite(InOutConfig.EdgeBounciness, 0.0f, 1.0f);
		InOutConfig.BorderFadeExponent = FMath::IsFinite(InOutConfig.BorderFadeExponent) ? FMath::Clamp(InOutConfig.BorderFadeExponent, 0.25f, 8.0f) : 1.0f;
		InOutConfig.VortexTangentialAccelerationCmPerSec2 = FMath::IsFinite(InOutConfig.VortexTangentialAccelerationCmPerSec2) ? FMath::Clamp(InOutConfig.VortexTangentialAccelerationCmPerSec2, -20000.0f, 20000.0f) : 0.0f;
		InOutConfig.VortexRadialDriftCmPerSec = FMath::IsFinite(InOutConfig.VortexRadialDriftCmPerSec) ? FMath::Clamp(InOutConfig.VortexRadialDriftCmPerSec, -MaxCellSpeedCmPerSec, MaxCellSpeedCmPerSec) : 0.0f;
		InOutConfig.VortexRadiusRatio = ClampFinite(InOutConfig.VortexRadiusRatio, 0.01f, 0.5f);
	}

	bool IsFiniteBrushStamp(const FTDFluidBrushStamp& Stamp)
	{
		return !Stamp.WorldLocation.ContainsNaN()
			&& !Stamp.WorldVelocity.ContainsNaN()
			&& FMath::IsFinite(Stamp.RadiusCm)
			&& FMath::IsFinite(Stamp.DensityStrength)
			&& FMath::IsFinite(Stamp.VelocityStrength)
			&& FMath::IsFinite(Stamp.RadialVelocityCmPerSec)
			&& FMath::IsFinite(Stamp.RippleImpulseCm)
			&& FMath::IsFinite(Stamp.HeatStrength);
	}

	bool IsPositiveZero(double Value)
	{
		return Value == 0.0 && !FMath::IsNegativeOrNegativeZero(Value);
	}

	bool IsPositiveZeroVector(const FVector2D& Value)
	{
		return IsPositiveZero(Value.X) && IsPositiveZero(Value.Y);
	}

	struct FTDFluidBilinearSample
	{
		int32 Index00 = 0;
		int32 Index10 = 0;
		int32 Index01 = 0;
		int32 Index11 = 0;
		float FracX = 0.0f;
		float FracY = 0.0f;
	};

	FTDFluidBilinearSample MakeBilinearSample(float GridX, float GridY, int32 Resolution)
	{
		const float ClampedX = FMath::Clamp(GridX, 0.0f, static_cast<float>(Resolution - 1));
		const float ClampedY = FMath::Clamp(GridY, 0.0f, static_cast<float>(Resolution - 1));

		const int32 X0 = FMath::FloorToInt(ClampedX);
		const int32 Y0 = FMath::FloorToInt(ClampedY);
		const int32 X1 = FMath::Min(X0 + 1, Resolution - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, Resolution - 1);

		FTDFluidBilinearSample Sample;
		Sample.Index00 = Y0 * Resolution + X0;
		Sample.Index10 = Y0 * Resolution + X1;
		Sample.Index01 = Y1 * Resolution + X0;
		Sample.Index11 = Y1 * Resolution + X1;
		Sample.FracX = ClampedX - static_cast<float>(X0);
		Sample.FracY = ClampedY - static_cast<float>(Y0);
		return Sample;
	}

	struct FTDFluidDenormalPreservingScope
	{
		FTDFluidDenormalPreservingScope()
			: CallerControlRegister(VectorGetControlRegister())
		{
			VectorSetControlRegister(CallerControlRegister & ~static_cast<uint32>(VECTOR_DENORMALS_FLUSH_TO_ZERO));
		}

		~FTDFluidDenormalPreservingScope()
		{
			VectorSetControlRegister(CallerControlRegister);
		}

		uint32 CallerControlRegister;
	};

	void ForEachGridRow(const TCHAR* DebugName, int32 RowCount, TFunctionRef<void(int32)> RowBody)
	{
		const EParallelForFlags Flags = CVarSolverRowParallel.GetValueOnAnyThread() != 0 ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread;
		ParallelFor(DebugName, RowCount, NoiseRowsPerBatch, [&RowBody](int32 RowIndex)
		{
			const FTDFluidDenormalPreservingScope DenormalPreservingScope;
			RowBody(RowIndex);
		}, Flags);
	}

	template <typename ElementType>
	void ShiftGridInPlace(ElementType* Values, int32 GridSize, int32 ShiftCellsX, int32 ShiftCellsY)
	{
		const int32 ClampedShiftX = FMath::Clamp(ShiftCellsX, -GridSize, GridSize);
		const int32 ClampedShiftY = FMath::Clamp(ShiftCellsY, -GridSize, GridSize);
		const int32 CopyWidth = GridSize - FMath::Abs(ClampedShiftX);
		const int32 DestinationStartX = FMath::Max(-ClampedShiftX, 0);
		const int32 SourceStartX = FMath::Max(ClampedShiftX, 0);
		const int32 TailStartX = DestinationStartX + CopyWidth;
		const int32 RowStep = ClampedShiftY >= 0 ? 1 : -1;
		const int32 FirstRow = ClampedShiftY >= 0 ? 0 : GridSize - 1;

		for (int32 RowOrder = 0; RowOrder < GridSize; ++RowOrder)
		{
			const int32 NewY = FirstRow + RowOrder * RowStep;
			const int32 OldY = NewY + ClampedShiftY;
			ElementType* NewRow = Values + NewY * GridSize;
			if (CopyWidth <= 0 || OldY < 0 || OldY >= GridSize)
			{
				FMemory::Memzero(NewRow, sizeof(ElementType) * GridSize);
				continue;
			}

			FMemory::Memmove(NewRow + DestinationStartX, Values + OldY * GridSize + SourceStartX, sizeof(ElementType) * CopyWidth);
			FMemory::Memzero(NewRow, sizeof(ElementType) * DestinationStartX);
			FMemory::Memzero(NewRow + TailStartX, sizeof(ElementType) * (GridSize - TailStartX));
		}
	}

	float InterpolateBilinear(const float* Field, const FTDFluidBilinearSample& Sample)
	{
		const float TopLerp = FMath::Lerp(Field[Sample.Index00], Field[Sample.Index10], Sample.FracX);
		const float BottomLerp = FMath::Lerp(Field[Sample.Index01], Field[Sample.Index11], Sample.FracX);
		return FMath::Lerp(TopLerp, BottomLerp, Sample.FracY);
	}

	struct FTDFluidNoiseAxisSample
	{
		int32 LatticeIndex = 0;
		float Fade = 0.0f;
		float FadeSlope = 0.0f;
	};

	struct FTDFluidNoiseLattice
	{
		FTDFluidNoiseAxisSample Columns[MaxGridResolution];
		FTDFluidNoiseAxisSample Rows[MaxGridResolution];
		TArray<float, TInlineAllocator<NoiseLatticeInlineCount>> Values;
		int32 Width = 0;
	};

	uint32 HashLatticePoint(int32 IndexX, int32 IndexY, uint32 Seed)
	{
		uint32 Hash = Seed ^ (static_cast<uint32>(IndexX) * 0x8DA6B343u) ^ (static_cast<uint32>(IndexY) * 0xD8163841u);
		Hash ^= Hash >> 16;
		Hash *= 0x7FEB352Du;
		Hash ^= Hash >> 15;
		Hash *= 0x846CA68Bu;
		Hash ^= Hash >> 16;
		return Hash;
	}

	float EvaluateLatticeValue(int32 IndexX, int32 IndexY, uint32 Seed, double NoiseTime)
	{
		const uint32 Hash = HashLatticePoint(IndexX, IndexY, Seed);
		const double Phase = static_cast<double>(Hash & 0xFFFFu) / 65536.0;
		const double Frequency = NoiseFrequencyMinimum + static_cast<double>(Hash >> 16) / 65536.0;
		const double Cycles = Phase + Frequency * NoiseTime;
		const double Turns = Cycles - FMath::FloorToDouble(Cycles);
		return FMath::Sin(static_cast<float>(UE_DOUBLE_TWO_PI * Turns));
	}

	float EvaluateQuinticFade(float Fraction)
	{
		return Fraction * Fraction * Fraction * (Fraction * (Fraction * 6.0f - 15.0f) + 10.0f);
	}

	float EvaluateQuinticFadeSlope(float Fraction)
	{
		const float Complement = Fraction * (1.0f - Fraction);
		return 30.0f * Complement * Complement;
	}

	int32 BuildNoiseAxis(FTDFluidNoiseAxisSample* OutSamples, int32 Resolution, double MinWorld, double CellSize, double InverseSpacing)
	{
		for (int32 Coordinate = 0; Coordinate < Resolution; ++Coordinate)
		{
			const double LatticePosition = (MinWorld + (static_cast<double>(Coordinate) + 0.5) * CellSize) * InverseSpacing;
			const double LatticeFloor = FMath::FloorToDouble(LatticePosition);
			const float Fraction = static_cast<float>(LatticePosition - LatticeFloor);
			OutSamples[Coordinate].LatticeIndex = static_cast<int32>(LatticeFloor);
			OutSamples[Coordinate].Fade = EvaluateQuinticFade(Fraction);
			OutSamples[Coordinate].FadeSlope = EvaluateQuinticFadeSlope(Fraction);
		}

		const int32 MinIndex = OutSamples[0].LatticeIndex;
		for (int32 Coordinate = 0; Coordinate < Resolution; ++Coordinate)
		{
			OutSamples[Coordinate].LatticeIndex -= MinIndex;
		}
		return MinIndex;
	}

	float ResolveNoiseSpacingCm(const FTDFluidSimulationConfig& Config, int32 Resolution, float ScaleMultiplier)
	{
		const float LargestCellSizeCm = static_cast<float>(FMath::Max(Config.SimExtentCm.X, Config.SimExtentCm.Y)) / static_cast<float>(Resolution);
		return FMath::Max(Config.FlowNoiseScaleCm, LargestCellSizeCm) * ScaleMultiplier;
	}

	void BuildNoiseLattice(FTDFluidNoiseLattice& OutLattice, const FTDFluidSimulationConfig& Config, int32 Resolution, const FVector2D& CenterWorld, float SpacingCm, uint32 Seed, double SimulationTimeSeconds)
	{
		const double InverseSpacing = 1.0 / static_cast<double>(SpacingCm);
		const double NoiseTime = static_cast<double>(Config.FlowNoiseSpeed) * SimulationTimeSeconds;
		const int32 MinIndexX = BuildNoiseAxis(OutLattice.Columns, Resolution, CenterWorld.X - 0.5 * Config.SimExtentCm.X, Config.SimExtentCm.X / Resolution, InverseSpacing);
		const int32 MinIndexY = BuildNoiseAxis(OutLattice.Rows, Resolution, CenterWorld.Y - 0.5 * Config.SimExtentCm.Y, Config.SimExtentCm.Y / Resolution, InverseSpacing);

		const int32 Width = OutLattice.Columns[Resolution - 1].LatticeIndex + 2;
		const int32 Height = OutLattice.Rows[Resolution - 1].LatticeIndex + 2;
		OutLattice.Width = Width;
		OutLattice.Values.SetNumUninitialized(Width * Height);

		float* Values = OutLattice.Values.GetData();
		for (int32 RowIndex = 0; RowIndex < Height; ++RowIndex)
		{
			for (int32 ColumnIndex = 0; ColumnIndex < Width; ++ColumnIndex)
			{
				Values[RowIndex * Width + ColumnIndex] = EvaluateLatticeValue(MinIndexX + ColumnIndex, MinIndexY + RowIndex, Seed, NoiseTime);
			}
		}
	}

	void BuildFlowNoiseLattice(FTDFluidNoiseLattice& OutLattice, const FTDFluidSimulationConfig& Config, int32 Resolution, const FVector2D& CenterWorld, double SimulationTimeSeconds)
	{
		BuildNoiseLattice(OutLattice, Config, Resolution, CenterWorld, ResolveNoiseSpacingCm(Config, Resolution, 1.0f), FlowNoiseSeed, SimulationTimeSeconds);
	}

	void BuildAmbientNoiseLattice(FTDFluidNoiseLattice& OutLattice, const FTDFluidSimulationConfig& Config, int32 Resolution, const FVector2D& CenterWorld, double SimulationTimeSeconds)
	{
		BuildNoiseLattice(OutLattice, Config, Resolution, CenterWorld, ResolveNoiseSpacingCm(Config, Resolution, AmbientNoiseScaleMultiplier), AmbientNoiseSeed, SimulationTimeSeconds);
	}

	void EvaluateFlowNoiseRow(const FTDFluidNoiseLattice& Lattice, int32 Resolution, int32 RowIndex, float Amplitude, float* OutAccelerationX, float* OutAccelerationY)
	{
		const FTDFluidNoiseAxisSample& Row = Lattice.Rows[RowIndex];
		const int32 Width = Lattice.Width;
		const float* RowValues = Lattice.Values.GetData() + Row.LatticeIndex * Width;

		for (int32 ColumnIndex = 0; ColumnIndex < Resolution; ++ColumnIndex)
		{
			const FTDFluidNoiseAxisSample& Column = Lattice.Columns[ColumnIndex];
			const float* Corner = RowValues + Column.LatticeIndex;
			const float Value00 = Corner[0];
			const float Value10 = Corner[1];
			const float Value01 = Corner[Width];
			const float Value11 = Corner[Width + 1];
			const float PotentialSlopeAlongX = Column.FadeSlope * FMath::Lerp(Value10 - Value00, Value11 - Value01, Row.Fade);
			const float PotentialSlopeAlongY = Row.FadeSlope * FMath::Lerp(Value01 - Value00, Value11 - Value10, Column.Fade);
			OutAccelerationX[ColumnIndex] = PotentialSlopeAlongY * Amplitude;
			OutAccelerationY[ColumnIndex] = -PotentialSlopeAlongX * Amplitude;
		}
	}

	void EvaluateNoiseValueRow(const FTDFluidNoiseLattice& Lattice, int32 Resolution, int32 RowIndex, float* OutValues)
	{
		const FTDFluidNoiseAxisSample& Row = Lattice.Rows[RowIndex];
		const int32 Width = Lattice.Width;
		const float* RowValues = Lattice.Values.GetData() + Row.LatticeIndex * Width;

		for (int32 ColumnIndex = 0; ColumnIndex < Resolution; ++ColumnIndex)
		{
			const FTDFluidNoiseAxisSample& Column = Lattice.Columns[ColumnIndex];
			const float* Corner = RowValues + Column.LatticeIndex;
			const float LowerEdge = FMath::Lerp(Corner[0], Corner[1], Column.Fade);
			const float UpperEdge = FMath::Lerp(Corner[Width], Corner[Width + 1], Column.Fade);
			OutValues[ColumnIndex] = FMath::Lerp(LowerEdge, UpperEdge, Row.Fade);
		}
	}
}

namespace
{
	constexpr float FluidWaterUnknownTerrainSdfCm = 10000.0f;
	constexpr float FluidWaterNoSurfaceSdfCm = -1.0f;
	constexpr float FluidWaterMinimumTerrainSlope = 0.05f;
	constexpr float FluidWaterSdfLimitCm = 10000.0f;
	constexpr int32 FluidWaterMaxRippleSubsteps = 8;
	constexpr float FluidWaterRippleSubstepSafety = 0.5f;
	constexpr float FluidWaterRippleQuietThreshold = 0.0001f;
	constexpr float FluidWaterRippleOvershootRatio = 0.25f;
	constexpr float FluidWaterTerrainHeightScaleCm = 1000.0f;
	constexpr float FluidWaterTerrainHeightLimit = 8.0f;
	constexpr float FluidWaterSquareRootOfTwo = 1.41421356f;

	float FluidWaterBankFalloffWeight(float DistanceCm, float HalfWidthCm, float FalloffCm)
	{
		if (DistanceCm >= HalfWidthCm)
		{
			return 0.0f;
		}

		const float EffectiveFalloffCm = FMath::Min(FalloffCm, HalfWidthCm);
		if (EffectiveFalloffCm <= 0.0f || DistanceCm <= HalfWidthCm - EffectiveFalloffCm)
		{
			return 1.0f;
		}

		const float Fraction = (DistanceCm - (HalfWidthCm - EffectiveFalloffCm)) / EffectiveFalloffCm;
		return 1.0f - Fraction * Fraction * (3.0f - 2.0f * Fraction);
	}

	void FluidWaterShiftDirtyFlags(TArray<uint8>& Flags, int32 ShiftCells)
	{
		const int32 FlagCount = Flags.Num();
		TArray<uint8> ShiftedFlags;
		ShiftedFlags.Init(1, FlagCount);
		for (int32 FlagIndex = 0; FlagIndex < FlagCount; ++FlagIndex)
		{
			const int32 SourceIndex = FlagIndex + ShiftCells;
			if (SourceIndex >= 0 && SourceIndex < FlagCount)
			{
				ShiftedFlags[FlagIndex] = Flags[SourceIndex];
			}
		}
		Flags = MoveTemp(ShiftedFlags);
	}

	void FluidWaterBuildCubicWeights(float Fraction, float* OutWeights)
	{
		const float Square = Fraction * Fraction;
		const float Cube = Square * Fraction;
		OutWeights[0] = -0.5f * Cube + Square - 0.5f * Fraction;
		OutWeights[1] = 1.5f * Cube - 2.5f * Square + 1.0f;
		OutWeights[2] = -1.5f * Cube + 2.0f * Square + 0.5f * Fraction;
		OutWeights[3] = 0.5f * Cube - 0.5f * Square;
	}
}

void FTDFluidGrid2DSolver::Initialize(const FTDFluidSimulationConfig& InConfig, const FVector2D& InitialCenterWorld)
{
	Config = InConfig;
	SanitizeConfig(Config);

	Resolution = Config.GridResolution;
	CenterWorld = InitialCenterWorld.ContainsNaN() ? FVector2D::ZeroVector : InitialCenterWorld;

	ResetSimulation();
}

void FTDFluidGrid2DSolver::UpdateConfig(const FTDFluidSimulationConfig& InConfig)
{
	const int32 ClampedResolution = FMath::Clamp(InConfig.GridResolution, MinGridResolution, MaxGridResolution);
	if (ClampedResolution != Resolution || !IsInitialized())
	{
		Initialize(InConfig, CenterWorld);
		return;
	}

	const FTDFluidSimulationConfig PreviousConfig = Config;
	Config = InConfig;
	SanitizeConfig(Config);
	SynchronizeWaterBuffersWithConfig(PreviousConfig);
	SynchronizeHeatBuffersWithConfig();
}

void FTDFluidGrid2DSolver::ResetSimulation()
{
	if (Resolution <= 0)
	{
		return;
	}

	++ReinitializationGeneration;
	SimulationTimeSeconds = 0.0;

	const int32 CellCount = Resolution * Resolution;
	PrimaryDensity.Init(0.0f, CellCount);
	SelectiveDensity.Init(0.0f, CellCount);
	SurfaceDisplacement.Init(0.0f, CellCount);
	ScratchDensity.Init(0.0f, CellCount);
	ScratchSelectiveDensity.Init(0.0f, CellCount);
	ScratchSurfaceDisplacement.Init(0.0f, CellCount);
	VelocityX.Init(0.0f, CellCount);
	VelocityY.Init(0.0f, CellCount);
	ScratchVelocityX.Init(0.0f, CellCount);
	ScratchVelocityY.Init(0.0f, CellCount);
	PendingRadialPushX.Init(0.0f, CellCount);
	PendingRadialPushY.Init(0.0f, CellCount);
	bHasPendingRadialPush = false;
	Pressure.Init(0.0f, CellCount);
	PressureSolution.Init(0.0f, CellCount);
	ScratchPressure.Init(0.0f, CellCount);
	Divergence.Init(0.0f, CellCount);
	VorticityCurl.Init(0.0f, CellCount);
	ObstacleSdf.Init(10000.0f, CellCount);
	TerrainElevation.Init(0.0f, CellCount);
	TerrainSlope.Init(FVector2D::ZeroVector, CellCount);
	bIsTerrainSlopeZero = true;
	ResetWaterBuffers();
	Heat.Reset();
	ScratchHeat.Reset();
	SynchronizeHeatBuffersWithConfig();
	RefreshObstacleCellCache();
}

bool FTDFluidGrid2DSolver::ShiftOriginForSlidingWindow(const FVector2D& DesiredCenterWorld)
{
	if (!IsInitialized())
	{
		return false;
	}

	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const int32 ShiftCellsX = FMath::RoundToInt((DesiredCenterWorld.X - CenterWorld.X) / CellSizeX);
	const int32 ShiftCellsY = FMath::RoundToInt((DesiredCenterWorld.Y - CenterWorld.Y) / CellSizeY);

	if (ShiftCellsX == 0 && ShiftCellsY == 0)
	{
		return false;
	}

	CenterWorld.X += static_cast<float>(ShiftCellsX) * CellSizeX;
	CenterWorld.Y += static_cast<float>(ShiftCellsY) * CellSizeY;

	ShiftGridInPlace(PrimaryDensity.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(SelectiveDensity.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(SurfaceDisplacement.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(VelocityX.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(VelocityY.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(Pressure.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(PressureSolution.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(TerrainElevation.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(TerrainSlope.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	if (!Heat.IsEmpty())
	{
		ShiftGridInPlace(Heat.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	}

	if (IsWaterLayerActive())
	{
		ShiftWaterBuffers(ShiftCellsX, ShiftCellsY);
	}

	return true;
}

void FTDFluidGrid2DSolver::SetUniformTerrainSlope(const FVector2D& InSlopeGradient)
{
	if (!IsInitialized())
	{
		return;
	}

	for (FVector2D& SlopeValue : TerrainSlope)
	{
		SlopeValue = InSlopeGradient;
	}
	bIsTerrainSlopeZero = IsPositiveZeroVector(InSlopeGradient);
}

void FTDFluidGrid2DSolver::SetTerrainElevationAtCell(int32 CellX, int32 CellY, float ElevationCm)
{
	if (!IsValidCell(CellX, CellY))
	{
		return;
	}

	const int32 CellIndex = ToCellIndex(CellX, CellY);
	TerrainElevation[CellIndex] = ElevationCm;

	if (!IsWaterLayerActive() || TerrainKnownMask[CellIndex] != 0)
	{
		return;
	}

	TerrainKnownMask[CellIndex] = 1;
	++TerrainKnownCellCount;
}

void FTDFluidGrid2DSolver::ComputeTerrainSlopeFromElevation()
{
	if (!IsInitialized())
	{
		return;
	}

	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);

	bool bAreAllSlopesZero = true;
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		const int32 YMinus = FMath::Max(Y - 1, 0);
		const int32 YPlus = FMath::Min(Y + 1, Resolution - 1);

		for (int32 X = 0; X < Resolution; ++X)
		{
			const int32 XMinus = FMath::Max(X - 1, 0);
			const int32 XPlus = FMath::Min(X + 1, Resolution - 1);

			const float GradX = (TerrainElevation[ToCellIndex(XPlus, Y)] - TerrainElevation[ToCellIndex(XMinus, Y)]) / (static_cast<float>(XPlus - XMinus) * CellSizeX);
			const float GradY = (TerrainElevation[ToCellIndex(X, YPlus)] - TerrainElevation[ToCellIndex(X, YMinus)]) / (static_cast<float>(YPlus - YMinus) * CellSizeY);
			const FVector2D CellSlope(GradX, GradY);
			TerrainSlope[ToCellIndex(X, Y)] = CellSlope;
			bAreAllSlopesZero = bAreAllSlopesZero && IsPositiveZeroVector(CellSlope);
		}
	}
	bIsTerrainSlopeZero = bAreAllSlopesZero;
	bWaterDryDirty = true;
}

void FTDFluidGrid2DSolver::RasterizeSdfObstacles(const TArray<FTDFluidSdfObstacle>& Obstacles)
{
	if (!IsInitialized())
	{
		return;
	}

	const int32 CellCount = Resolution * Resolution;
	ObstacleSdf.Init(10000.0f, CellCount);

	if (Obstacles.IsEmpty())
	{
		if (IsWaterLayerActive())
		{
			CommitWaterObstacleSdf();
			return;
		}

		RefreshObstacleCellCache();
		return;
	}

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const FVector2D CellWorld = CellToWorldCenter(X, Y);
			float MinSignedDistance = 10000.0f;

			for (const FTDFluidSdfObstacle& Obstacle : Obstacles)
			{
				const FVector2D Delta = CellWorld - FVector2D(Obstacle.WorldCenter.X, Obstacle.WorldCenter.Y);
				float SignedDistance = 10000.0f;

				if (Obstacle.bIsBox)
				{
					const FVector2D AbsDelta(FMath::Abs(Delta.X), FMath::Abs(Delta.Y));
					const FVector2D BoxDiff = AbsDelta - Obstacle.BoxHalfExtentsCm;
					const FVector2D Outside(FMath::Max(BoxDiff.X, 0.0f), FMath::Max(BoxDiff.Y, 0.0f));
					const float Inside = FMath::Min(FMath::Max(BoxDiff.X, BoxDiff.Y), 0.0f);
					SignedDistance = Outside.Size() + Inside;
				}
				else
				{
					SignedDistance = Delta.Size() - FMath::Max(Obstacle.RadiusCm, 1.0f);
				}

				MinSignedDistance = FMath::Min(MinSignedDistance, SignedDistance);
			}

			ObstacleSdf[ToCellIndex(X, Y)] = MinSignedDistance;
		}
	}

	if (IsWaterLayerActive())
	{
		CommitWaterObstacleSdf();
		EnforceObstacleBoundaries();
		return;
	}

	RefreshObstacleCellCache();
	EnforceObstacleBoundaries();
}

void FTDFluidGrid2DSolver::ApplyBrushStamps(const TArray<FTDFluidBrushStamp>& Stamps, float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ApplyBrushStamps);
	const FTDFluidDenormalPreservingScope DenormalPreservingScope;

	if (!IsInitialized() || Stamps.IsEmpty() || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	const float VelocityBlend = FMath::Min(1.0f, BrushVelocityResponsePerSec * DeltaTime);
	int32 AppliedStampCount = 0;

	for (const FTDFluidBrushStamp& Stamp : Stamps)
	{
		if (AppliedStampCount >= Config.MaxPointsPerTick)
		{
			break;
		}

		if (!IsFiniteBrushStamp(Stamp))
		{
			continue;
		}

		ApplyBrushStamp(Stamp, DeltaTime, VelocityBlend);
		if (!Heat.IsEmpty())
		{
			ApplyHeatStamp(Stamp, DeltaTime);
		}
		++AppliedStampCount;
	}
}

void FTDFluidGrid2DSolver::ApplyBrushStamp(const FTDFluidBrushStamp& Stamp, float DeltaTime, float VelocityBlend)
{
	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float MinBoundsX = CenterWorld.X - 0.5f * Config.SimExtentCm.X;
	const float MinBoundsY = CenterWorld.Y - 0.5f * Config.SimExtentCm.Y;
	const float MinRadius = FMath::Max(CellSizeX, CellSizeY) * 0.5f;
	const float MaxRadius = FMath::Max(Config.SimExtentCm.X, Config.SimExtentCm.Y);
	const float EffectiveRadius = FMath::Clamp(Stamp.RadiusCm, MinRadius, MaxRadius);
	const FVector2D StampCenter(Stamp.WorldLocation.X, Stamp.WorldLocation.Y);

	const bool bIsOutsideGrid = StampCenter.X + EffectiveRadius < MinBoundsX
		|| StampCenter.X - EffectiveRadius > MinBoundsX + Config.SimExtentCm.X
		|| StampCenter.Y + EffectiveRadius < MinBoundsY
		|| StampCenter.Y - EffectiveRadius > MinBoundsY + Config.SimExtentCm.Y;
	if (bIsOutsideGrid)
	{
		return;
	}

	const bool bErasesDensity = Stamp.DensityStrength < 0.0f;
	const float DensityStrength = FMath::Max(Stamp.DensityStrength, 0.0f);
	const float EraseRatePerSec = FMath::Abs(Stamp.DensityStrength) * BrushEraseRatePerSec;
	const bool bWritesSelectiveDensity = Stamp.Channel == ETDFluidBrushChannel::ChannelG_Blood || Stamp.Channel == ETDFluidBrushChannel::ChannelB_Fire;
	const bool bIsWaterLayerActive = IsWaterLayerActive();
	const bool bWritesRipple = bIsWaterLayerActive && Stamp.RippleImpulseCm != 0.0f;
	const bool bHasRadialVelocity = Stamp.RadialVelocityCmPerSec != 0.0f && !bIsWaterLayerActive;
	const bool bDefersRadialVelocity = bHasRadialVelocity && Config.SimMode != ETDFluidSimMode::SimplePainter;
	const bool bBlendsRadialVelocity = bHasRadialVelocity && !bDefersRadialVelocity;
	const bool bInjectsVelocity = Stamp.VelocityStrength > 0.0f || bBlendsRadialVelocity;
	const FVector2D TargetVelocity = (FVector2D(Stamp.WorldVelocity.X, Stamp.WorldVelocity.Y) * Stamp.VelocityStrength).GetClampedToMaxSize(MaxCellSpeedCmPerSec);

	const int32 MinCellX = FMath::Clamp(FMath::FloorToInt((StampCenter.X - EffectiveRadius - MinBoundsX) / CellSizeX), 0, Resolution - 1);
	const int32 MaxCellX = FMath::Clamp(FMath::CeilToInt((StampCenter.X + EffectiveRadius - MinBoundsX) / CellSizeX), 0, Resolution - 1);
	const int32 MinCellY = FMath::Clamp(FMath::FloorToInt((StampCenter.Y - EffectiveRadius - MinBoundsY) / CellSizeY), 0, Resolution - 1);
	const int32 MaxCellY = FMath::Clamp(FMath::CeilToInt((StampCenter.Y + EffectiveRadius - MinBoundsY) / CellSizeY), 0, Resolution - 1);

	for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
	{
		for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
		{
			if (IsSolidCell(CellX, CellY))
			{
				continue;
			}

			const FVector2D CellCenter = CellToWorldCenter(CellX, CellY);
			const float DistanceCm = FVector2D::Distance(CellCenter, StampCenter);
			if (DistanceCm > EffectiveRadius)
			{
				continue;
			}

			const float NormalizedFalloff = 1.0f - (DistanceCm / EffectiveRadius);
			const float SmoothWeight = NormalizedFalloff * NormalizedFalloff * (3.0f - 2.0f * NormalizedFalloff);
			const int32 CellIdx = ToCellIndex(CellX, CellY);

			if (bWritesRipple)
			{
				RippleHeight[CellIdx] -= Stamp.RippleImpulseCm * SmoothWeight;
				bHasRippleActivity = true;
			}

			if (bErasesDensity)
			{
				const float RetainFactor = FMath::Exp(-EraseRatePerSec * SmoothWeight * DeltaTime);
				PrimaryDensity[CellIdx] *= RetainFactor;
				if (bWritesSelectiveDensity)
				{
					SelectiveDensity[CellIdx] *= RetainFactor;
				}
			}
			else
			{
				const float DensityTarget = FMath::Clamp(DensityStrength * SmoothWeight, 0.0f, 1.0f);
				PrimaryDensity[CellIdx] = FMath::Max(PrimaryDensity[CellIdx], DensityTarget);
				if (!bIsWaterLayerActive)
				{
					SurfaceDisplacement[CellIdx] = FMath::Max(SurfaceDisplacement[CellIdx], DensityTarget * BrushDisplacementRatio);
				}
				if (bWritesSelectiveDensity)
				{
					SelectiveDensity[CellIdx] = FMath::Max(SelectiveDensity[CellIdx], DensityTarget);
				}
			}

			if (bDefersRadialVelocity && DistanceCm > UE_KINDA_SMALL_NUMBER)
			{
				const FVector2D OutwardDirection = (CellCenter - StampCenter) / DistanceCm;
				PendingRadialPushX[CellIdx] += static_cast<float>(OutwardDirection.X) * Stamp.RadialVelocityCmPerSec * SmoothWeight;
				PendingRadialPushY[CellIdx] += static_cast<float>(OutwardDirection.Y) * Stamp.RadialVelocityCmPerSec * SmoothWeight;
				bHasPendingRadialPush = true;
			}

			if (!bInjectsVelocity)
			{
				continue;
			}

			FVector2D CellTargetVelocity = TargetVelocity;
			if (bBlendsRadialVelocity && DistanceCm > UE_KINDA_SMALL_NUMBER)
			{
				const FVector2D OutwardDirection = (CellCenter - StampCenter) / DistanceCm;
				CellTargetVelocity = (TargetVelocity + OutwardDirection * Stamp.RadialVelocityCmPerSec).GetClampedToMaxSize(MaxCellSpeedCmPerSec);
			}

			const float CellBlend = SmoothWeight * VelocityBlend;
			VelocityX[CellIdx] += (CellTargetVelocity.X - VelocityX[CellIdx]) * CellBlend;
			VelocityY[CellIdx] += (CellTargetVelocity.Y - VelocityY[CellIdx]) * CellBlend;
		}
	}
}

void FTDFluidGrid2DSolver::StepSimulation(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_StepSimulation);
	const FTDFluidDenormalPreservingScope DenormalPreservingScope;

	if (!IsInitialized() || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	const float ClampedDeltaTime = FMath::Min(DeltaTime, 0.1f);
	if (Config.SimMode == ETDFluidSimMode::SimplePainter)
	{
		StepSimplePainter(ClampedDeltaTime);
	}
	else if (IsWaterLayerActive())
	{
		StepFluidSimWater(ClampedDeltaTime);
	}
	else
	{
		StepFluidSim(ClampedDeltaTime);
	}

	RefillAmbientDensity(ClampedDeltaTime);
	ClampVelocityMagnitudes();
	SimulationTimeSeconds += ClampedDeltaTime;
}

void FTDFluidGrid2DSolver::ComputeFlowNoiseAccelerationField(TArray<FVector2D>& OutAccelerationCmPerSec2, bool bForceSingleThread) const
{
	if (!IsInitialized())
	{
		OutAccelerationCmPerSec2.Reset();
		return;
	}

	const int32 GridSize = Resolution;
	OutAccelerationCmPerSec2.Init(FVector2D::ZeroVector, GridSize * GridSize);
	if (Config.FlowNoiseAccelerationCmPerSec2 <= 0.0f)
	{
		return;
	}

	FTDFluidNoiseLattice Lattice;
	BuildFlowNoiseLattice(Lattice, Config, GridSize, CenterWorld, SimulationTimeSeconds);

	const float Amplitude = Config.FlowNoiseAccelerationCmPerSec2;
	FVector2D* AccelerationValues = OutAccelerationCmPerSec2.GetData();
	const EParallelForFlags Flags = bForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;
	ParallelFor(TEXT("TDFluidFlowNoiseField"), GridSize, NoiseRowsPerBatch, [&](int32 Y)
	{
		float RowAccelerationX[MaxGridResolution];
		float RowAccelerationY[MaxGridResolution];
		EvaluateFlowNoiseRow(Lattice, GridSize, Y, Amplitude, RowAccelerationX, RowAccelerationY);
		for (int32 X = 0; X < GridSize; ++X)
		{
			AccelerationValues[Y * GridSize + X] = FVector2D(RowAccelerationX[X], RowAccelerationY[X]);
		}
	}, Flags);
}

void FTDFluidGrid2DSolver::RefillAmbientDensity(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_RefillAmbient);

	if (Config.AmbientRefillPerSec <= 0.0f)
	{
		return;
	}

	const float RefillBlend = 1.0f - FMath::Exp(-Config.AmbientRefillPerSec * DeltaTime);
	if (Config.AmbientNoiseAmount > 0.0f)
	{
		RefillAmbientDensityWithNoise(RefillBlend);
		return;
	}

	const float TargetDensity = Config.AmbientDensity;
	const int32 CellCount = Resolution * Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* PrimaryValues = PrimaryDensity.GetData();

	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		if (SolidCells[Idx])
		{
			continue;
		}

		PrimaryValues[Idx] += (TargetDensity - PrimaryValues[Idx]) * RefillBlend;
	}
}

void FTDFluidGrid2DSolver::RefillAmbientDensityWithNoise(float RefillBlend)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_RefillAmbientNoise);

	FTDFluidNoiseLattice Lattice;
	BuildAmbientNoiseLattice(Lattice, Config, Resolution, CenterWorld, SimulationTimeSeconds);

	const int32 GridSize = Resolution;
	const float AmbientDensity = Config.AmbientDensity;
	const float NoiseAmount = Config.AmbientNoiseAmount;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* PrimaryValues = PrimaryDensity.GetData();

	ParallelFor(TEXT("TDFluidAmbientNoise"), GridSize, NoiseRowsPerBatch, [&](int32 Y)
	{
		const FTDFluidDenormalPreservingScope DenormalPreservingScope;
		float RowNoise[MaxGridResolution];
		EvaluateNoiseValueRow(Lattice, GridSize, Y, RowNoise);
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx])
			{
				continue;
			}

			const float TargetDensity = AmbientDensity * (1.0f - NoiseAmount * (0.5f + 0.5f * RowNoise[X]));
			PrimaryValues[Idx] += (TargetDensity - PrimaryValues[Idx]) * RefillBlend;
		}
	});
}

void FTDFluidGrid2DSolver::StepSimplePainter(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_StepSimplePainter);

	const float DensityDecay = 1.0f / (1.0f + Config.DensityDissipation * DeltaTime);
	const float VelocityDecay = 1.0f / (1.0f + Config.VelocityDamping * DeltaTime * 10.0f);

	const int32 CellCount = Resolution * Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* PrimaryValues = PrimaryDensity.GetData();
	float* SelectiveValues = SelectiveDensity.GetData();
	float* DisplacementValues = SurfaceDisplacement.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();
	float* PressureValues = Pressure.GetData();
	float* PressureSolutionValues = PressureSolution.GetData();

	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		if (SolidCells[Idx])
		{
			PrimaryValues[Idx] = 0.0f;
			SelectiveValues[Idx] = 0.0f;
			DisplacementValues[Idx] = 0.0f;
			VelX[Idx] = 0.0f;
			VelY[Idx] = 0.0f;
			PressureValues[Idx] = 0.0f;
			PressureSolutionValues[Idx] = 0.0f;
			continue;
		}

		PrimaryValues[Idx] *= DensityDecay;
		SelectiveValues[Idx] *= DensityDecay;
		DisplacementValues[Idx] *= DensityDecay;
		VelX[Idx] *= VelocityDecay;
		VelY[Idx] *= VelocityDecay;
		PressureValues[Idx] *= DensityDecay;
	}
}

void FTDFluidGrid2DSolver::StepFluidSim(float DeltaTime)
{
	ApplyTerrainSlopeAcceleration(DeltaTime);
	ApplyFlowNoiseAcceleration(DeltaTime);
	ApplyBuoyancyAcceleration(DeltaTime);
	ApplyHeatBuoyancyAcceleration(DeltaTime);
	ApplyVortexAcceleration(DeltaTime);
	EnforceObstacleBoundaries();
	ApplyVorticityConfinement(DeltaTime);
	SolvePressureProjection();
	AdvectVelocity(DeltaTime);
	if (Config.bReflectsAtBoundaries)
	{
		EnforceObstacleBoundariesWithReflection();
		ApplyEdgeReflection();
	}
	else
	{
		EnforceObstacleBoundaries();
	}
	SolvePressureProjection();
	EnforceObstacleBoundaries();
	AccumulateVortexRadialDrift();

	const bool bAppliesRadialPush = bHasPendingRadialPush;
	if (bAppliesRadialPush)
	{
		ApplyPendingRadialPush();
		EnforceObstacleBoundaries();
	}

	AdvectDensities(DeltaTime);
	AdvectHeat(DeltaTime);
	ApplyDirectionalOutflow(DeltaTime);
	ApplyDirectionalOutflowToHeat(DeltaTime);

	if (bAppliesRadialPush)
	{
		RestoreVelocityBeforeRadialPush();
	}
}

void FTDFluidGrid2DSolver::ApplyPendingRadialPush()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_RadialPush);

	const int32 CellCount = Resolution * Resolution;
	FMemory::Memcpy(ScratchVelocityX.GetData(), VelocityX.GetData(), sizeof(float) * CellCount);
	FMemory::Memcpy(ScratchVelocityY.GetData(), VelocityY.GetData(), sizeof(float) * CellCount);

	const uint8* SolidCells = SolidCellMask.GetData();
	float* PushX = PendingRadialPushX.GetData();
	float* PushY = PendingRadialPushY.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		if (PushX[Idx] == 0.0f && PushY[Idx] == 0.0f)
		{
			continue;
		}

		if (!SolidCells[Idx])
		{
			const FVector2D PushedVelocity = (FVector2D(VelX[Idx] + PushX[Idx], VelY[Idx] + PushY[Idx])).GetClampedToMaxSize(MaxCellSpeedCmPerSec);
			VelX[Idx] = static_cast<float>(PushedVelocity.X);
			VelY[Idx] = static_cast<float>(PushedVelocity.Y);
		}

		PushX[Idx] = 0.0f;
		PushY[Idx] = 0.0f;
	}

	bHasPendingRadialPush = false;
}

void FTDFluidGrid2DSolver::RestoreVelocityBeforeRadialPush()
{
	const int32 CellCount = Resolution * Resolution;
	FMemory::Memcpy(VelocityX.GetData(), ScratchVelocityX.GetData(), sizeof(float) * CellCount);
	FMemory::Memcpy(VelocityY.GetData(), ScratchVelocityY.GetData(), sizeof(float) * CellCount);
}

void FTDFluidGrid2DSolver::ApplyTerrainSlopeAcceleration(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_TerrainSlope);

	if (Config.TerrainSlopeGravity <= 0.0f || bIsTerrainSlopeZero)
	{
		return;
	}

	const int32 GridSize = Resolution;
	const float SlopeGravity = Config.TerrainSlopeGravity;
	const uint8* SolidCells = SolidCellMask.GetData();
	const FVector2D* SlopeValues = TerrainSlope.GetData();
	const float* PrimaryValues = PrimaryDensity.GetData();
	const float* SelectiveValues = SelectiveDensity.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	ForEachGridRow(TEXT("TDFluidTerrainSlopeRows"), GridSize, [&](int32 Y)
	{
		const int32 RowStart = Y * GridSize;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = RowStart + X;
			if (SolidCells[Idx])
			{
				continue;
			}

			const float ActiveFluidMask = FMath::Clamp(PrimaryValues[Idx] + SelectiveValues[Idx] + 0.1f, 0.0f, 1.0f);
			VelX[Idx] -= SlopeValues[Idx].X * SlopeGravity * ActiveFluidMask * DeltaTime;
			VelY[Idx] -= SlopeValues[Idx].Y * SlopeGravity * ActiveFluidMask * DeltaTime;
		}
	});
}

void FTDFluidGrid2DSolver::ApplyFlowNoiseAcceleration(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_FlowNoise);

	if (Config.FlowNoiseAccelerationCmPerSec2 <= 0.0f)
	{
		return;
	}

	FTDFluidNoiseLattice Lattice;
	BuildFlowNoiseLattice(Lattice, Config, Resolution, CenterWorld, SimulationTimeSeconds);

	const int32 GridSize = Resolution;
	const float Amplitude = Config.FlowNoiseAccelerationCmPerSec2;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	ParallelFor(TEXT("TDFluidFlowNoise"), GridSize, NoiseRowsPerBatch, [&](int32 Y)
	{
		const FTDFluidDenormalPreservingScope DenormalPreservingScope;
		float RowAccelerationX[MaxGridResolution];
		float RowAccelerationY[MaxGridResolution];
		EvaluateFlowNoiseRow(Lattice, GridSize, Y, Amplitude, RowAccelerationX, RowAccelerationY);
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx])
			{
				continue;
			}

			VelX[Idx] += RowAccelerationX[X] * DeltaTime;
			VelY[Idx] += RowAccelerationY[X] * DeltaTime;
		}
	});
}

void FTDFluidGrid2DSolver::SetPlaneUpDirection(const FVector2D& InPlaneUpDirection)
{
	PlaneUpDirection = InPlaneUpDirection.IsNearlyZero() ? FVector2D::ZeroVector : InPlaneUpDirection.GetSafeNormal();
}

void FTDFluidGrid2DSolver::ApplyBuoyancyAcceleration(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_Buoyancy);

	if (Config.BuoyancyCmPerSec2 <= 0.0f || PlaneUpDirection.IsNearlyZero())
	{
		return;
	}

	const int32 CellCount = Resolution * Resolution;
	const float AmbientDensityValue = Config.AmbientDensity;
	const float AccelerationX = static_cast<float>(PlaneUpDirection.X) * Config.BuoyancyCmPerSec2 * DeltaTime;
	const float AccelerationY = static_cast<float>(PlaneUpDirection.Y) * Config.BuoyancyCmPerSec2 * DeltaTime;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* PrimaryValues = PrimaryDensity.GetData();
	const float* SelectiveValues = SelectiveDensity.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		if (SolidCells[Idx])
		{
			continue;
		}

		const float ExcessDensity = FMath::Max(0.0f, FMath::Max(PrimaryValues[Idx], SelectiveValues[Idx]) - AmbientDensityValue);
		VelX[Idx] += AccelerationX * ExcessDensity;
		VelY[Idx] += AccelerationY * ExcessDensity;
	}
}

void FTDFluidGrid2DSolver::ApplyDirectionalOutflow(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_Outflow);

	if (Config.OutflowRatePerSec <= 0.0f || Config.OutflowBandRatio <= 0.0f || PlaneUpDirection.IsNearlyZero())
	{
		return;
	}

	const float BandWidth = 2.0f * Config.OutflowBandRatio;
	const float BandStart = 1.0f - BandWidth;
	const float InverseResolution = 1.0f / static_cast<float>(Resolution);
	const int32 GridSize = Resolution;
	float* PrimaryValues = PrimaryDensity.GetData();
	float* SelectiveValues = SelectiveDensity.GetData();
	float* DisplacementValues = SurfaceDisplacement.GetData();

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		const float OffsetY = (static_cast<float>(Y) + 0.5f) * InverseResolution - 0.5f;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const float OffsetX = (static_cast<float>(X) + 0.5f) * InverseResolution - 0.5f;
			const float BandPosition = (OffsetX * static_cast<float>(PlaneUpDirection.X) + OffsetY * static_cast<float>(PlaneUpDirection.Y)) / 0.5f;
			if (BandPosition <= BandStart)
			{
				continue;
			}

			const float BandWeight = FMath::Clamp((BandPosition - BandStart) / BandWidth, 0.0f, 1.0f);
			const float RetainFactor = FMath::Exp(-Config.OutflowRatePerSec * BandWeight * DeltaTime);
			const int32 Idx = Y * GridSize + X;
			PrimaryValues[Idx] *= RetainFactor;
			SelectiveValues[Idx] *= RetainFactor;
			DisplacementValues[Idx] *= RetainFactor;
		}
	}
}

void FTDFluidGrid2DSolver::EnforceObstacleBoundaries()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ObstacleBoundaries);

	const float CellWorldSize = FMath::Max(Config.SimExtentCm.X, Config.SimExtentCm.Y) / static_cast<float>(Resolution);
	if (ObstacleSdfMinimum >= CellWorldSize * 1.5f)
	{
		return;
	}

	float SpacingX = 1.0f;
	float SpacingY = 1.0f;
	ComputeRelativeCellSpacing(SpacingX, SpacingY);

	const int32 GridSize = Resolution;
	const float* SdfValues = ObstacleSdf.GetData();
	float* PrimaryValues = PrimaryDensity.GetData();
	float* SelectiveValues = SelectiveDensity.GetData();
	float* DisplacementValues = SurfaceDisplacement.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();
	float* PressureValues = Pressure.GetData();
	float* PressureSolutionValues = PressureSolution.GetData();

	ForEachGridRow(TEXT("TDFluidObstacleBoundaryRows"), GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			const float SignedDistance = SdfValues[Idx];

			if (SignedDistance <= 0.0f)
			{
				PrimaryValues[Idx] = 0.0f;
				SelectiveValues[Idx] = 0.0f;
				DisplacementValues[Idx] = 0.0f;
				VelX[Idx] = 0.0f;
				VelY[Idx] = 0.0f;
				PressureValues[Idx] = 0.0f;
				PressureSolutionValues[Idx] = 0.0f;
				continue;
			}

			if (SignedDistance >= CellWorldSize * 1.5f)
			{
				continue;
			}

			const int32 XMinus = FMath::Max(X - 1, 0);
			const int32 XPlus = FMath::Min(X + 1, GridSize - 1);
			const int32 YMinus = FMath::Max(Y - 1, 0);
			const int32 YPlus = FMath::Min(Y + 1, GridSize - 1);

			const float GradientSpanX = static_cast<float>(XPlus - XMinus) * SpacingX;
			const float GradientSpanY = static_cast<float>(YPlus - YMinus) * SpacingY;
			FVector2D Normal(
				(SdfValues[Y * GridSize + XPlus] - SdfValues[Y * GridSize + XMinus]) / GradientSpanX,
				(SdfValues[YPlus * GridSize + X] - SdfValues[YMinus * GridSize + X]) / GradientSpanY);

			if (!Normal.Normalize())
			{
				continue;
			}

			FVector2D CurrentVelocity(VelX[Idx], VelY[Idx]);
			const float NormalDot = FVector2D::DotProduct(CurrentVelocity, Normal);
			if (NormalDot < 0.0f)
			{
				CurrentVelocity -= Normal * NormalDot;
				VelX[Idx] = CurrentVelocity.X;
				VelY[Idx] = CurrentVelocity.Y;
			}
		}
	});
}

void FTDFluidGrid2DSolver::ApplyVorticityConfinement(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_Vorticity);

	if (Config.VorticityStrength <= 0.0f)
	{
		return;
	}

	float SpacingX = 1.0f;
	float SpacingY = 1.0f;
	ComputeRelativeCellSpacing(SpacingX, SpacingY);

	const int32 GridSize = Resolution;
	const float VorticityStrength = Config.VorticityStrength;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();
	float* CurlValues = VorticityCurl.GetData();

	ForEachGridRow(TEXT("TDFluidVorticityCurlRows"), GridSize - 2, [&](int32 RowIndex)
	{
		const int32 RowStart = (RowIndex + 1) * GridSize;
		for (int32 X = 1; X < GridSize - 1; ++X)
		{
			const int32 Idx = RowStart + X;
			const float DvYDx = 0.5f * (VelY[Idx + 1] - VelY[Idx - 1]) / SpacingX;
			const float DvXDy = 0.5f * (VelX[Idx + GridSize] - VelX[Idx - GridSize]) / SpacingY;
			CurlValues[Idx] = DvYDx - DvXDy;
		}
	});

	ForEachGridRow(TEXT("TDFluidVorticityApplyRows"), GridSize - 4, [&](int32 RowIndex)
	{
		const int32 RowStart = (RowIndex + 2) * GridSize;
		for (int32 X = 2; X < GridSize - 2; ++X)
		{
			const int32 Idx = RowStart + X;
			if (SolidCells[Idx])
			{
				continue;
			}

			const float AbsCurlRight = FMath::Abs(CurlValues[Idx + 1]);
			const float AbsCurlLeft = FMath::Abs(CurlValues[Idx - 1]);
			const float AbsCurlUp = FMath::Abs(CurlValues[Idx + GridSize]);
			const float AbsCurlDown = FMath::Abs(CurlValues[Idx - GridSize]);

			FVector2D CurlGrad(0.5f * (AbsCurlRight - AbsCurlLeft) / SpacingX, 0.5f * (AbsCurlUp - AbsCurlDown) / SpacingY);
			if (!CurlGrad.Normalize())
			{
				continue;
			}

			const float Curl = CurlValues[Idx];
			VelX[Idx] += CurlGrad.Y * Curl * VorticityStrength * DeltaTime;
			VelY[Idx] += -CurlGrad.X * Curl * VorticityStrength * DeltaTime;
		}
	});
}

void FTDFluidGrid2DSolver::AdvectVelocity(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_AdvectVelocity);

	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float StepScaleX = (DeltaTime * Config.AdvectionSpeedScale) / CellSizeX;
	const float StepScaleY = (DeltaTime * Config.AdvectionSpeedScale) / CellSizeY;
	const float DampingFactor = 1.0f / (1.0f + Config.VelocityDamping * DeltaTime * 15.0f);

	float BorderFadeByCoordinate[MaxGridResolution];
	float BorderDampingByCoordinate[MaxGridResolution];
	BuildBorderFadeTable(BorderFadeByCoordinate);
	BuildBorderDampingTable(BorderFadeByCoordinate, DeltaTime, BorderDampingByCoordinate);

	const int32 GridSize = Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* VelX = VelocityX.GetData();
	const float* VelY = VelocityY.GetData();
	float* AdvectedVelX = ScratchVelocityX.GetData();
	float* AdvectedVelY = ScratchVelocityY.GetData();

	ForEachGridRow(TEXT("TDFluidAdvectVelocityRows"), GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx])
			{
				AdvectedVelX[Idx] = 0.0f;
				AdvectedVelY[Idx] = 0.0f;
				continue;
			}

			const float BacktraceX = static_cast<float>(X) - VelX[Idx] * StepScaleX;
			const float BacktraceY = static_cast<float>(Y) - VelY[Idx] * StepScaleY;
			const float BorderDamping = BorderFadeByCoordinate[X] <= BorderFadeByCoordinate[Y] ? BorderDampingByCoordinate[X] : BorderDampingByCoordinate[Y];
			const FTDFluidBilinearSample Sample = MakeBilinearSample(BacktraceX, BacktraceY, GridSize);

			AdvectedVelX[Idx] = InterpolateBilinear(VelX, Sample) * DampingFactor * BorderDamping;
			AdvectedVelY[Idx] = InterpolateBilinear(VelY, Sample) * DampingFactor * BorderDamping;
		}
	});

	Swap(VelocityX, ScratchVelocityX);
	Swap(VelocityY, ScratchVelocityY);
}

void FTDFluidGrid2DSolver::SolvePressureProjection()
{
	float SpacingX = 1.0f;
	float SpacingY = 1.0f;
	ComputeRelativeCellSpacing(SpacingX, SpacingY);

	const float InvSpacingXSquared = 1.0f / (SpacingX * SpacingX);
	const float InvSpacingYSquared = 1.0f / (SpacingY * SpacingY);
	const float InvNeighborWeightSum = 1.0f / (2.0f * (InvSpacingXSquared + InvSpacingYSquared));
	const float SmallestCellSizeCm = static_cast<float>(FMath::Min(Config.SimExtentCm.X, Config.SimExtentCm.Y)) / static_cast<float>(Resolution);
	const float PressureDisplayScale = SmallestCellSizeCm / (PressureNormalizationCmPerSec * PressureReferenceCellSizeCm);

	ComputeVelocityDivergence(SpacingX, SpacingY);
	RelaxPressureJacobi(InvSpacingXSquared, InvSpacingYSquared, InvNeighborWeightSum);
	RemoveMeanPressure();
	SubtractPressureGradient(SpacingX, SpacingY, PressureDisplayScale);
}

void FTDFluidGrid2DSolver::ComputeVelocityDivergence(float SpacingX, float SpacingY)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ProjectionDivergence);

	const int32 GridSize = Resolution;
	const float DoubleSpacingX = 2.0f * SpacingX;
	const float DoubleSpacingY = 2.0f * SpacingY;
	const uint8* SolidFreeRows = SolidFreeStencilRows.GetData();
	const float* VelX = VelocityX.GetData();
	const float* VelY = VelocityY.GetData();
	float* DivergenceValues = Divergence.GetData();
	float* PressureSolutionValues = PressureSolution.GetData();

	const auto ComputeCellNearSolid = [&](int32 X, int32 Y)
	{
		const int32 Idx = Y * GridSize + X;
		if (IsSolidCell(X, Y))
		{
			DivergenceValues[Idx] = 0.0f;
			PressureSolutionValues[Idx] = 0.0f;
			return;
		}

		const float VelRight = IsSolidCell(X + 1, Y) ? 0.0f : VelX[Idx + 1];
		const float VelLeft = IsSolidCell(X - 1, Y) ? 0.0f : VelX[Idx - 1];
		const float VelUp = IsSolidCell(X, Y + 1) ? 0.0f : VelY[Idx + GridSize];
		const float VelDown = IsSolidCell(X, Y - 1) ? 0.0f : VelY[Idx - GridSize];
		DivergenceValues[Idx] = (VelRight - VelLeft) / DoubleSpacingX + (VelUp - VelDown) / DoubleSpacingY;
	};

	ForEachGridRow(TEXT("TDFluidDivergenceRows"), GridSize, [&](int32 Y)
	{
		if (!SolidFreeRows[Y])
		{
			for (int32 X = 0; X < GridSize; ++X)
			{
				ComputeCellNearSolid(X, Y);
			}
			return;
		}

		const int32 RowStart = Y * GridSize;
		const float* RowVelX = VelX + RowStart;
		const float* RowVelYUp = VelY + RowStart + GridSize;
		const float* RowVelYDown = VelY + RowStart - GridSize;
		float* RowDivergence = DivergenceValues + RowStart;

		ComputeCellNearSolid(0, Y);
		for (int32 X = 1; X < GridSize - 1; ++X)
		{
			RowDivergence[X] = (RowVelX[X + 1] - RowVelX[X - 1]) / DoubleSpacingX + (RowVelYUp[X] - RowVelYDown[X]) / DoubleSpacingY;
		}
		ComputeCellNearSolid(GridSize - 1, Y);
	});
}

void FTDFluidGrid2DSolver::RelaxPressureJacobi(float InvSpacingXSquared, float InvSpacingYSquared, float InvNeighborWeightSum)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ProjectionJacobi);

	const int32 GridSize = Resolution;
	const uint8* SolidFreeRows = SolidFreeStencilRows.GetData();
	const float* DivergenceValues = Divergence.GetData();

	for (int32 Iter = 0; Iter < Config.PressureIterations; ++Iter)
	{
		const float* SourcePressure = PressureSolution.GetData();
		float* TargetPressure = ScratchPressure.GetData();

		const auto RelaxCellNearSolid = [&](int32 X, int32 Y)
		{
			const int32 Idx = Y * GridSize + X;
			if (IsSolidCell(X, Y))
			{
				TargetPressure[Idx] = 0.0f;
				return;
			}

			const float CenterP = SourcePressure[Idx];
			const float PRight = IsSolidCell(X + 1, Y) ? CenterP : SourcePressure[Idx + 1];
			const float PLeft = IsSolidCell(X - 1, Y) ? CenterP : SourcePressure[Idx - 1];
			const float PUp = IsSolidCell(X, Y + 1) ? CenterP : SourcePressure[Idx + GridSize];
			const float PDown = IsSolidCell(X, Y - 1) ? CenterP : SourcePressure[Idx - GridSize];
			TargetPressure[Idx] = ((PLeft + PRight) * InvSpacingXSquared + (PDown + PUp) * InvSpacingYSquared - DivergenceValues[Idx]) * InvNeighborWeightSum;
		};

		ForEachGridRow(TEXT("TDFluidJacobiRows"), GridSize, [&](int32 Y)
		{
			if (!SolidFreeRows[Y])
			{
				for (int32 X = 0; X < GridSize; ++X)
				{
					RelaxCellNearSolid(X, Y);
				}
				return;
			}

			const int32 RowStart = Y * GridSize;
			const float* RowPressure = SourcePressure + RowStart;
			const float* RowPressureUp = RowPressure + GridSize;
			const float* RowPressureDown = RowPressure - GridSize;
			const float* RowDivergence = DivergenceValues + RowStart;
			float* RowTarget = TargetPressure + RowStart;

			RelaxCellNearSolid(0, Y);
			for (int32 X = 1; X < GridSize - 1; ++X)
			{
				RowTarget[X] = ((RowPressure[X - 1] + RowPressure[X + 1]) * InvSpacingXSquared + (RowPressureDown[X] + RowPressureUp[X]) * InvSpacingYSquared - RowDivergence[X]) * InvNeighborWeightSum;
			}
			RelaxCellNearSolid(GridSize - 1, Y);
		});
		Swap(PressureSolution, ScratchPressure);
	}
}

void FTDFluidGrid2DSolver::RemoveMeanPressure()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ProjectionMeanPressure);

	const int32 CellCount = Resolution * Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* PressureSolutionValues = PressureSolution.GetData();

	double PressureSum = 0.0;
	int32 FluidCellCount = 0;
	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		if (SolidCells[Idx])
		{
			continue;
		}
		PressureSum += PressureSolutionValues[Idx];
		++FluidCellCount;
	}

	if (FluidCellCount <= 0)
	{
		return;
	}

	const float MeanPressure = static_cast<float>(PressureSum / FluidCellCount);
	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		if (SolidCells[Idx])
		{
			continue;
		}
		PressureSolutionValues[Idx] -= MeanPressure;
	}
}

void FTDFluidGrid2DSolver::SubtractPressureGradient(float SpacingX, float SpacingY, float PressureDisplayScale)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ProjectionGradient);

	const int32 GridSize = Resolution;
	const float DoubleSpacingX = 2.0f * SpacingX;
	const float DoubleSpacingY = 2.0f * SpacingY;
	const uint8* SolidFreeRows = SolidFreeStencilRows.GetData();
	const float* PressureSolutionValues = PressureSolution.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();
	float* PressureValues = Pressure.GetData();

	const auto SubtractCellNearSolid = [&](int32 X, int32 Y)
	{
		const int32 Idx = Y * GridSize + X;
		if (IsSolidCell(X, Y))
		{
			VelX[Idx] = 0.0f;
			VelY[Idx] = 0.0f;
			PressureValues[Idx] = 0.0f;
			return;
		}

		const float CenterP = PressureSolutionValues[Idx];
		const float PRight = IsSolidCell(X + 1, Y) ? CenterP : PressureSolutionValues[Idx + 1];
		const float PLeft = IsSolidCell(X - 1, Y) ? CenterP : PressureSolutionValues[Idx - 1];
		const float PUp = IsSolidCell(X, Y + 1) ? CenterP : PressureSolutionValues[Idx + GridSize];
		const float PDown = IsSolidCell(X, Y - 1) ? CenterP : PressureSolutionValues[Idx - GridSize];

		VelX[Idx] -= (PRight - PLeft) / DoubleSpacingX;
		VelY[Idx] -= (PUp - PDown) / DoubleSpacingY;
		PressureValues[Idx] = FMath::Clamp(CenterP * PressureDisplayScale, -1.0f, 1.0f);
	};

	ForEachGridRow(TEXT("TDFluidGradientRows"), GridSize, [&](int32 Y)
	{
		if (!SolidFreeRows[Y])
		{
			for (int32 X = 0; X < GridSize; ++X)
			{
				SubtractCellNearSolid(X, Y);
			}
			return;
		}

		const int32 RowStart = Y * GridSize;
		const float* RowPressure = PressureSolutionValues + RowStart;
		const float* RowPressureUp = RowPressure + GridSize;
		const float* RowPressureDown = RowPressure - GridSize;
		float* RowVelX = VelX + RowStart;
		float* RowVelY = VelY + RowStart;
		float* RowDisplayPressure = PressureValues + RowStart;

		SubtractCellNearSolid(0, Y);
		for (int32 X = 1; X < GridSize - 1; ++X)
		{
			RowVelX[X] -= (RowPressure[X + 1] - RowPressure[X - 1]) / DoubleSpacingX;
			RowVelY[X] -= (RowPressureUp[X] - RowPressureDown[X]) / DoubleSpacingY;
			RowDisplayPressure[X] = FMath::Clamp(RowPressure[X] * PressureDisplayScale, -1.0f, 1.0f);
		}
		SubtractCellNearSolid(GridSize - 1, Y);
	});
}

void FTDFluidGrid2DSolver::AdvectDensities(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_AdvectDensities);

	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float StepScaleX = (DeltaTime * Config.AdvectionSpeedScale) / CellSizeX;
	const float StepScaleY = (DeltaTime * Config.AdvectionSpeedScale) / CellSizeY;
	const float DissipationFactor = 1.0f / (1.0f + Config.DensityDissipation * DeltaTime);

	const int32 GridSize = Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* VelX = VelocityX.GetData();
	const float* VelY = VelocityY.GetData();
	const float* PrimaryValues = PrimaryDensity.GetData();
	const float* SelectiveValues = SelectiveDensity.GetData();
	const float* DisplacementValues = SurfaceDisplacement.GetData();
	float* AdvectedPrimary = ScratchDensity.GetData();
	float* AdvectedSelective = ScratchSelectiveDensity.GetData();
	float* AdvectedDisplacement = ScratchSurfaceDisplacement.GetData();

	ForEachGridRow(TEXT("TDFluidAdvectDensityRows"), GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx])
			{
				AdvectedPrimary[Idx] = 0.0f;
				AdvectedSelective[Idx] = 0.0f;
				AdvectedDisplacement[Idx] = 0.0f;
				continue;
			}

			const float BacktraceX = static_cast<float>(X) - VelX[Idx] * StepScaleX;
			const float BacktraceY = static_cast<float>(Y) - VelY[Idx] * StepScaleY;
			const FTDFluidBilinearSample Sample = MakeBilinearSample(BacktraceX, BacktraceY, GridSize);

			AdvectedPrimary[Idx] = FMath::Clamp(InterpolateBilinear(PrimaryValues, Sample) * DissipationFactor, 0.0f, 1.0f);
			AdvectedSelective[Idx] = FMath::Clamp(InterpolateBilinear(SelectiveValues, Sample) * DissipationFactor, 0.0f, 1.0f);
			AdvectedDisplacement[Idx] = FMath::Clamp(InterpolateBilinear(DisplacementValues, Sample) * DissipationFactor, 0.0f, 1.0f);
		}
	});

	Swap(PrimaryDensity, ScratchDensity);
	Swap(SelectiveDensity, ScratchSelectiveDensity);
	Swap(SurfaceDisplacement, ScratchSurfaceDisplacement);
}

void FTDFluidGrid2DSolver::ClampVelocityMagnitudes()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ClampVelocity);

	const int32 CellCount = Resolution * Resolution;
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		const float SpeedSquared = VelX[Idx] * VelX[Idx] + VelY[Idx] * VelY[Idx];
		if (!FMath::IsFinite(SpeedSquared))
		{
			VelX[Idx] = 0.0f;
			VelY[Idx] = 0.0f;
			continue;
		}

		if (SpeedSquared <= MaxCellSpeedCmPerSec * MaxCellSpeedCmPerSec)
		{
			continue;
		}

		const float ClampScale = MaxCellSpeedCmPerSec / FMath::Sqrt(SpeedSquared);
		VelX[Idx] *= ClampScale;
		VelY[Idx] *= ClampScale;
	}
}

void FTDFluidGrid2DSolver::RefreshObstacleCellCache()
{
	const int32 GridSize = Resolution;
	SolidCellMask.SetNumUninitialized(GridSize * GridSize);
	SolidFreeStencilRows.SetNumUninitialized(GridSize);
	ObstacleSdfMinimum = TNumericLimits<float>::Max();

	bool bRowHasSolidCell[MaxGridResolution];
	const float* SdfValues = ObstacleSdf.GetData();
	uint8* SolidCells = SolidCellMask.GetData();
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		bool bHasSolidCellInRow = false;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			const float SignedDistance = SdfValues[Idx];
			const bool bIsSolid = SignedDistance <= 0.0f;
			SolidCells[Idx] = bIsSolid ? 1 : 0;
			bHasSolidCellInRow = bHasSolidCellInRow || bIsSolid;
			ObstacleSdfMinimum = FMath::IsNaN(SignedDistance) ? -TNumericLimits<float>::Max() : FMath::Min(ObstacleSdfMinimum, SignedDistance);
		}
		bRowHasSolidCell[Y] = bHasSolidCellInRow;
	}

	uint8* SolidFreeRows = SolidFreeStencilRows.GetData();
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		const bool bIsInteriorRow = Y > 0 && Y < GridSize - 1;
		SolidFreeRows[Y] = bIsInteriorRow && !bRowHasSolidCell[Y - 1] && !bRowHasSolidCell[Y] && !bRowHasSolidCell[Y + 1] ? 1 : 0;
	}
}

void FTDFluidGrid2DSolver::BuildBorderFadeTable(float* OutFadeByCoordinate) const
{
	if (Config.BorderFadeRatio <= 0.0f)
	{
		for (int32 Coordinate = 0; Coordinate < Resolution; ++Coordinate)
		{
			OutFadeByCoordinate[Coordinate] = 1.0f;
		}
		return;
	}

	for (int32 Coordinate = 0; Coordinate < Resolution; ++Coordinate)
	{
		const float NormalizedCoordinate = (static_cast<float>(Coordinate) + 0.5f) / static_cast<float>(Resolution);
		const float EdgeDistance = FMath::Min(NormalizedCoordinate, 1.0f - NormalizedCoordinate);
		OutFadeByCoordinate[Coordinate] = FMath::Clamp(EdgeDistance / Config.BorderFadeRatio, 0.0f, 1.0f);
	}
}

void FTDFluidGrid2DSolver::BuildBorderDampingTable(const float* FadeByCoordinate, float DeltaTime, float* OutDampingByCoordinate) const
{
	for (int32 Coordinate = 0; Coordinate < Resolution; ++Coordinate)
	{
		const float BorderFade = FadeByCoordinate[Coordinate];
		OutDampingByCoordinate[Coordinate] = BorderFade >= 1.0f ? 1.0f : FMath::Exp(-BorderVelocityDampingPerSec * (1.0f - BorderFade) * DeltaTime);
	}
}

void FTDFluidGrid2DSolver::ComputeRelativeCellSpacing(float& OutSpacingX, float& OutSpacingY) const
{
	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float SmallestCellSize = FMath::Min(CellSizeX, CellSizeY);
	OutSpacingX = CellSizeX / SmallestCellSize;
	OutSpacingY = CellSizeY / SmallestCellSize;
}

float FTDFluidGrid2DSolver::SampleBilinear(const TArray<float>& Field, float GridX, float GridY) const
{
	return InterpolateBilinear(Field.GetData(), MakeBilinearSample(GridX, GridY, Resolution));
}

bool FTDFluidGrid2DSolver::WorldToGridUV(const FVector2D& WorldPos, FVector2D& OutUV) const
{
	if (!IsInitialized())
	{
		OutUV = FVector2D::ZeroVector;
		return false;
	}

	const FVector2D MinWorld = CenterWorld - 0.5f * Config.SimExtentCm;
	OutUV.X = (WorldPos.X - MinWorld.X) / Config.SimExtentCm.X;
	OutUV.Y = (WorldPos.Y - MinWorld.Y) / Config.SimExtentCm.Y;

	return OutUV.X >= 0.0f && OutUV.X <= 1.0f && OutUV.Y >= 0.0f && OutUV.Y <= 1.0f;
}

FVector2D FTDFluidGrid2DSolver::CellToWorldCenter(int32 CellX, int32 CellY) const
{
	const FVector2D MinWorld = CenterWorld - 0.5f * Config.SimExtentCm;
	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	return FVector2D(
		MinWorld.X + (static_cast<float>(CellX) + 0.5f) * CellSizeX,
		MinWorld.Y + (static_cast<float>(CellY) + 0.5f) * CellSizeY);
}

bool FTDFluidGrid2DSolver::SampleAtWorldPosition(const FVector2D& WorldPos, FTDFluidCellSample& OutSample) const
{
	FVector2D GridUV;
	if (!WorldToGridUV(WorldPos, GridUV))
	{
		OutSample = FTDFluidCellSample();
		return false;
	}

	const float GridX = GridUV.X * static_cast<float>(Resolution) - 0.5f;
	const float GridY = GridUV.Y * static_cast<float>(Resolution) - 0.5f;
	const int32 NearestX = FMath::Clamp(FMath::RoundToInt(GridX), 0, Resolution - 1);
	const int32 NearestY = FMath::Clamp(FMath::RoundToInt(GridY), 0, Resolution - 1);

	OutSample.PrimaryDensity = SampleBilinear(PrimaryDensity, GridX, GridY);
	OutSample.SelectiveDensity = SampleBilinear(SelectiveDensity, GridX, GridY);
	OutSample.Pressure = SampleBilinear(Pressure, GridX, GridY);
	OutSample.ObstacleSdf = SampleBilinear(ObstacleSdf, GridX, GridY);
	OutSample.Velocity = FVector2D(SampleBilinear(VelocityX, GridX, GridY), SampleBilinear(VelocityY, GridX, GridY));
	OutSample.VelocityCmPerSec = OutSample.Velocity;
	OutSample.TerrainSlope = TerrainSlope[ToCellIndex(NearestX, NearestY)];
	if (!Heat.IsEmpty())
	{
		OutSample.Heat = SampleBilinear(Heat, GridX, GridY);
	}

	if (!IsWaterLayerActive())
	{
		return true;
	}

	const int32 NearestIndex = ToCellIndex(NearestX, NearestY);
	OutSample.FlowGuideWeight = SampleBilinear(FlowGuideWeight, GridX, GridY);
	OutSample.FlowTargetVelocity = FVector2D(SampleBilinear(FlowTargetVelocityX, GridX, GridY), SampleBilinear(FlowTargetVelocityY, GridX, GridY));
	OutSample.Wetness = SampleBilinear(Wetness, GridX, GridY);
	OutSample.RippleHeightCm = SampleBilinear(RippleHeight, GridX, GridY);
	OutSample.bIsTerrainKnown = TerrainKnownMask[NearestIndex] != 0;
	OutSample.WaterDepthCm = ComputeWaterDepthAtIndex(NearestIndex);
	return true;
}

FTDFluidCellSample FTDFluidGrid2DSolver::SampleAtCell(int32 CellX, int32 CellY) const
{
	FTDFluidCellSample Result;
	if (!IsValidCell(CellX, CellY))
	{
		return Result;
	}

	const int32 Idx = ToCellIndex(CellX, CellY);
	Result.PrimaryDensity = PrimaryDensity[Idx];
	Result.SelectiveDensity = SelectiveDensity[Idx];
	Result.Pressure = Pressure[Idx];
	Result.ObstacleSdf = ObstacleSdf[Idx];
	Result.Velocity = FVector2D(VelocityX[Idx], VelocityY[Idx]);
	Result.VelocityCmPerSec = Result.Velocity;
	Result.TerrainSlope = TerrainSlope[Idx];
	if (!Heat.IsEmpty())
	{
		Result.Heat = Heat[Idx];
	}

	if (!IsWaterLayerActive())
	{
		return Result;
	}

	Result.FlowGuideWeight = FlowGuideWeight[Idx];
	Result.FlowTargetVelocity = FVector2D(FlowTargetVelocityX[Idx], FlowTargetVelocityY[Idx]);
	Result.Wetness = Wetness[Idx];
	Result.RippleHeightCm = RippleHeight[Idx];
	Result.bIsTerrainKnown = TerrainKnownMask[Idx] != 0;
	Result.WaterDepthCm = ComputeWaterDepthAtIndex(Idx);
	return Result;
}

void FTDFluidGrid2DSolver::PackOutputPixels(TArray<FFloat16Color>& OutDensityPressurePixels, TArray<FFloat16Color>& OutVelocityPixels) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_PackOutput);

	if (!IsInitialized())
	{
		OutDensityPressurePixels.Reset();
		OutVelocityPixels.Reset();
		return;
	}

	const int32 GridSize = Resolution;
	const int32 CellCount = GridSize * GridSize;
	OutDensityPressurePixels.SetNumUninitialized(CellCount);
	OutVelocityPixels.SetNumUninitialized(CellCount);

	float BorderFadeByCoordinate[MaxGridResolution];
	BuildBorderAlphaTable(BorderFadeByCoordinate);

	const float* SdfValues = ObstacleSdf.GetData();
	const float* PressureValues = Pressure.GetData();
	const float* DisplacementValues = SurfaceDisplacement.GetData();
	const float* PrimaryValues = PrimaryDensity.GetData();
	const float* SelectiveValues = SelectiveDensity.GetData();
	const float* VelX = VelocityX.GetData();
	const float* VelY = VelocityY.GetData();
	const FVector2D* SlopeValues = TerrainSlope.GetData();
	FFloat16Color* DensityPressurePixels = OutDensityPressurePixels.GetData();
	FFloat16Color* VelocityPixels = OutVelocityPixels.GetData();

	ParallelFor(GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			const float BorderAlpha = FMath::Min(BorderFadeByCoordinate[X], BorderFadeByCoordinate[Y]);
			const float ObstacleMask = SdfValues[Idx] > 0.0f ? 1.0f : 0.0f;
			const float SurfaceHeight = FMath::Clamp(PressureValues[Idx] + DisplacementValues[Idx], -1.0f, 1.0f);

			const float Density = FMath::Clamp(PrimaryValues[Idx], 0.0f, 1.0f);
			const float Surface = SurfaceHeight * 0.5f + 0.5f;
			const float Selective = FMath::Clamp(SelectiveValues[Idx], 0.0f, 1.0f);
			const float Alpha = FMath::Clamp(BorderAlpha * ObstacleMask, 0.0f, 1.0f);
			DensityPressurePixels[Idx] = FFloat16Color(FLinearColor(Density, Surface, Selective, Alpha));

			const float NormVelX = FMath::Clamp(VelX[Idx] / OutputSpeedReferenceCmPerSec, -1.0f, 1.0f) * 0.5f + 0.5f;
			const float NormVelY = FMath::Clamp(VelY[Idx] / OutputSpeedReferenceCmPerSec, -1.0f, 1.0f) * 0.5f + 0.5f;
			const float SpeedRatio = FMath::Clamp(FVector2D(VelX[Idx], VelY[Idx]).Size() / OutputSpeedReferenceCmPerSec, 0.0f, 1.0f);
			const float SlopeRatio = FMath::Clamp(SlopeValues[Idx].Size(), 0.0f, 1.0f);

			VelocityPixels[Idx] = FFloat16Color(FLinearColor(NormVelX, NormVelY, SpeedRatio, SlopeRatio));
		}
	});
}

uint32 FTDFluidGrid2DSolver::ComputeStateChecksum() const
{
	const TArray<float>* const StateFields[] = {
		&PrimaryDensity,
		&SelectiveDensity,
		&SurfaceDisplacement,
		&VelocityX,
		&VelocityY,
		&Pressure,
		&PressureSolution,
		&Divergence,
		&VorticityCurl,
		&ObstacleSdf,
		&TerrainElevation,
	};

	uint32 Checksum = FCrc::MemCrc32(&Resolution, sizeof(Resolution));
	Checksum = FCrc::MemCrc32(&CenterWorld, sizeof(CenterWorld), Checksum);
	for (const TArray<float>* Field : StateFields)
	{
		Checksum = FCrc::MemCrc32(Field->GetData(), Field->Num() * sizeof(float), Checksum);
	}
	const uint32 FieldChecksum = FCrc::MemCrc32(TerrainSlope.GetData(), TerrainSlope.Num() * sizeof(FVector2D), Checksum);
	if (Heat.IsEmpty())
	{
		return FieldChecksum;
	}

	return FCrc::MemCrc32(Heat.GetData(), Heat.Num() * sizeof(float), FieldChecksum);
}

float FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec()
{
	return OutputSpeedReferenceCmPerSec;
}

bool FTDFluidGrid2DSolver::ShouldAllocateWaterBuffers() const
{
	return Config.bEnableWaterLayer && Config.SimMode != ETDFluidSimMode::SimplePainter;
}

bool FTDFluidGrid2DSolver::IsWaterLayerActive() const
{
	return ShouldAllocateWaterBuffers() && Resolution > 0 && WaterSurfaceHeight.Num() == Resolution * Resolution;
}

void FTDFluidGrid2DSolver::ReleaseWaterBuffers()
{
	WaterSurfaceHeight.Empty();
	WaterDrySdf.Empty();
	ObstacleSdfRaw.Empty();
	TerrainKnownMask.Empty();
	FlowTargetVelocityX.Empty();
	FlowTargetVelocityY.Empty();
	FlowGuideWeight.Empty();
	FlowGuideDescent.Empty();
	Wetness.Empty();
	RippleHeight.Empty();
	RippleHeightVelocity.Empty();
	ScratchRippleHeight.Empty();
	ScratchRippleHeightVelocity.Empty();
	WaterDirtyColumns.Empty();
	WaterDirtyRows.Empty();
	WaterRowSegmentOffsets.Empty();
	WaterRowSegmentIndices.Empty();
	TerrainKnownCellCount = 0;
	bHasRippleActivity = false;
}

void FTDFluidGrid2DSolver::ResetWaterBuffers()
{
	if (!ShouldAllocateWaterBuffers())
	{
		ReleaseWaterBuffers();
		return;
	}

	const int32 CellCount = Resolution * Resolution;
	WaterSurfaceHeight.Init(NoWaterSurfaceHeight, CellCount);
	WaterDrySdf.Init(FluidWaterUnknownTerrainSdfCm, CellCount);
	ObstacleSdfRaw = ObstacleSdf;
	TerrainKnownMask.Init(0, CellCount);
	FlowTargetVelocityX.Init(0.0f, CellCount);
	FlowTargetVelocityY.Init(0.0f, CellCount);
	FlowGuideWeight.Init(0.0f, CellCount);
	FlowGuideDescent.Init(0.0f, CellCount);
	Wetness.Init(0.0f, CellCount);
	RippleHeight.Init(0.0f, CellCount);
	RippleHeightVelocity.Init(0.0f, CellCount);
	ScratchRippleHeight.Init(0.0f, CellCount);
	ScratchRippleHeightVelocity.Init(0.0f, CellCount);
	WaterDirtyColumns.Init(0, Resolution);
	WaterDirtyRows.Init(0, Resolution);
	TerrainKnownCellCount = 0;
	bWaterRasterFullDirty = true;
	bWaterHasDirtyStrips = false;
	bWaterDryDirty = true;
	bWaterVelocityInitPending = true;
	bHasRippleActivity = false;
}

void FTDFluidGrid2DSolver::SynchronizeWaterBuffersWithConfig(const FTDFluidSimulationConfig& PreviousConfig)
{
	if (!ShouldAllocateWaterBuffers())
	{
		ReleaseWaterBuffers();
		return;
	}

	if (WaterSurfaceHeight.Num() != Resolution * Resolution)
	{
		ResetWaterBuffers();
		return;
	}

	if (PreviousConfig.FlowGuideBankFalloffCm != Config.FlowGuideBankFalloffCm || PreviousConfig.FlowGuideBankSpeedRatio != Config.FlowGuideBankSpeedRatio)
	{
		bWaterRasterFullDirty = true;
	}

	if (PreviousConfig.bTreatDryCellsAsSolid != Config.bTreatDryCellsAsSolid)
	{
		bWaterDryDirty = true;
	}
}

void FTDFluidGrid2DSolver::ShiftWaterBuffers(int32 ShiftCellsX, int32 ShiftCellsY)
{
	ShiftGridInPlace(WaterSurfaceHeight.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(WaterDrySdf.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(TerrainKnownMask.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(FlowTargetVelocityX.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(FlowTargetVelocityY.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(FlowGuideWeight.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(FlowGuideDescent.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(Wetness.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(RippleHeight.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	ShiftGridInPlace(RippleHeightVelocity.GetData(), Resolution, ShiftCellsX, ShiftCellsY);
	FluidWaterShiftDirtyFlags(WaterDirtyColumns, ShiftCellsX);
	FluidWaterShiftDirtyFlags(WaterDirtyRows, ShiftCellsY);
	bWaterHasDirtyStrips = true;
	bWaterDryDirty = true;

	const int32 GridSize = Resolution;
	int32 KnownCellCount = 0;
	for (int32 CellY = 0; CellY < GridSize; ++CellY)
	{
		const int32 SourceY = CellY + ShiftCellsY;
		const bool bIsNewRow = SourceY < 0 || SourceY >= GridSize;
		for (int32 CellX = 0; CellX < GridSize; ++CellX)
		{
			const int32 SourceX = CellX + ShiftCellsX;
			const int32 CellIndex = CellY * GridSize + CellX;
			if (bIsNewRow || SourceX < 0 || SourceX >= GridSize)
			{
				WaterSurfaceHeight[CellIndex] = NoWaterSurfaceHeight;
				WaterDrySdf[CellIndex] = FluidWaterUnknownTerrainSdfCm;
			}
			KnownCellCount += TerrainKnownMask[CellIndex];
		}
	}
	TerrainKnownCellCount = KnownCellCount;
}

void FTDFluidGrid2DSolver::SetFlowGuides(const TArray<FTDFluidFlowGuide>& Guides)
{
	WaterGuideSegments.Reset();
	for (int32 GuideIndex = 0; GuideIndex < Guides.Num(); ++GuideIndex)
	{
		const TArray<FTDFluidFlowGuidePoint>& Points = Guides[GuideIndex].Points;
		for (int32 PointIndex = 0; PointIndex + 1 < Points.Num(); ++PointIndex)
		{
			const FTDFluidFlowGuidePoint& StartPoint = Points[PointIndex];
			const FTDFluidFlowGuidePoint& EndPoint = Points[PointIndex + 1];
			const bool bIsFinite = !StartPoint.WorldLocation.ContainsNaN() && !EndPoint.WorldLocation.ContainsNaN()
				&& FMath::IsFinite(StartPoint.HalfWidthCm) && FMath::IsFinite(EndPoint.HalfWidthCm)
				&& FMath::IsFinite(StartPoint.FlowSpeedCmPerSec) && FMath::IsFinite(EndPoint.FlowSpeedCmPerSec);
			if (!bIsFinite)
			{
				continue;
			}

			const FVector2D Start(StartPoint.WorldLocation.X, StartPoint.WorldLocation.Y);
			const FVector2D Offset(EndPoint.WorldLocation.X - Start.X, EndPoint.WorldLocation.Y - Start.Y);
			const double Length = Offset.Size();
			if (Length < UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			FWaterGuideSegment Segment;
			Segment.Start = Start;
			Segment.Direction = Offset / Length;
			Segment.LengthCm = Length;
			Segment.StartSurfaceCm = static_cast<float>(StartPoint.WorldLocation.Z);
			Segment.EndSurfaceCm = static_cast<float>(EndPoint.WorldLocation.Z);
			Segment.StartHalfWidthCm = FMath::Max(StartPoint.HalfWidthCm, 0.0f);
			Segment.EndHalfWidthCm = FMath::Max(EndPoint.HalfWidthCm, 0.0f);
			Segment.StartSpeedCmPerSec = StartPoint.FlowSpeedCmPerSec;
			Segment.EndSpeedCmPerSec = EndPoint.FlowSpeedCmPerSec;
			Segment.DescentSlope = FMath::Max(0.0f, static_cast<float>((StartPoint.WorldLocation.Z - EndPoint.WorldLocation.Z) / Length));
			Segment.GuideIndex = GuideIndex;

			const double PaddingCm = FMath::Max(Segment.StartHalfWidthCm, Segment.EndHalfWidthCm);
			const FVector2D End(EndPoint.WorldLocation.X, EndPoint.WorldLocation.Y);
			Segment.MinimumX = FMath::Min(Start.X, End.X) - PaddingCm;
			Segment.MaximumX = FMath::Max(Start.X, End.X) + PaddingCm;
			Segment.MinimumY = FMath::Min(Start.Y, End.Y) - PaddingCm;
			Segment.MaximumY = FMath::Max(Start.Y, End.Y) + PaddingCm;
			WaterGuideSegments.Add(Segment);
		}
	}
	bWaterRasterFullDirty = true;
}

void FTDFluidGrid2DSolver::SetWaterRegions(const TArray<FTDFluidWaterRegion>& Regions)
{
	WaterRegions.Reset();
	for (const FTDFluidWaterRegion& Region : Regions)
	{
		if (Region.MinXY.ContainsNaN() || Region.MaxXY.ContainsNaN() || !FMath::IsFinite(Region.SurfaceHeightCm))
		{
			continue;
		}

		FTDFluidWaterRegion NormalizedRegion;
		NormalizedRegion.MinXY = FVector2D(FMath::Min(Region.MinXY.X, Region.MaxXY.X), FMath::Min(Region.MinXY.Y, Region.MaxXY.Y));
		NormalizedRegion.MaxXY = FVector2D(FMath::Max(Region.MinXY.X, Region.MaxXY.X), FMath::Max(Region.MinXY.Y, Region.MaxXY.Y));
		NormalizedRegion.SurfaceHeightCm = Region.SurfaceHeightCm;
		WaterRegions.Add(NormalizedRegion);
	}
	bWaterRasterFullDirty = true;
}

float FTDFluidGrid2DSolver::GetWaterSurfaceHeightAtCell(int32 CellX, int32 CellY) const
{
	if (!IsWaterLayerActive() || !IsValidCell(CellX, CellY))
	{
		return NoWaterSurfaceHeight;
	}
	return WaterSurfaceHeight[ToCellIndex(CellX, CellY)];
}

bool FTDFluidGrid2DSolver::IsTerrainSamplingCompleteForWater() const
{
	return IsWaterLayerActive() && TerrainKnownCellCount >= Resolution * Resolution;
}

float FTDFluidGrid2DSolver::ComputeWaterDepthAtIndex(int32 CellIndex) const
{
	const float SurfaceHeight = WaterSurfaceHeight[CellIndex];
	if (SurfaceHeight == NoWaterSurfaceHeight || TerrainKnownMask[CellIndex] == 0)
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, SurfaceHeight - TerrainElevation[CellIndex]);
}

void FTDFluidGrid2DSolver::CommitWaterObstacleSdf()
{
	ObstacleSdfRaw = ObstacleSdf;
	ComposeWaterObstacleSdf();
}

void FTDFluidGrid2DSolver::ComposeWaterObstacleSdf()
{
	const int32 CellCount = Resolution * Resolution;
	const float* RawValues = ObstacleSdfRaw.GetData();
	const float* DryValues = WaterDrySdf.GetData();
	float* SdfValues = ObstacleSdf.GetData();

	if (Config.bTreatDryCellsAsSolid)
	{
		for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			SdfValues[CellIndex] = FMath::Min(RawValues[CellIndex], DryValues[CellIndex]);
		}
	}
	else
	{
		FMemory::Memcpy(SdfValues, RawValues, sizeof(float) * CellCount);
	}

	RefreshObstacleCellCache();
}

void FTDFluidGrid2DSolver::RefreshWaterDrySdf()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_WaterDrySdf);

	const int32 GridSize = Resolution;
	const uint8* KnownValues = TerrainKnownMask.GetData();
	const float* SurfaceValues = WaterSurfaceHeight.GetData();
	const float* ElevationValues = TerrainElevation.GetData();
	const FVector2D* SlopeValues = TerrainSlope.GetData();
	float* DryValues = WaterDrySdf.GetData();

	ForEachGridRow(TEXT("TDFluidWaterDrySdfRows"), GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (KnownValues[Idx] == 0)
			{
				DryValues[Idx] = FluidWaterUnknownTerrainSdfCm;
				continue;
			}

			if (SurfaceValues[Idx] == NoWaterSurfaceHeight)
			{
				DryValues[Idx] = FluidWaterNoSurfaceSdfCm;
				continue;
			}

			const float SlopeMagnitude = FMath::Max(static_cast<float>(SlopeValues[Idx].Size()), FluidWaterMinimumTerrainSlope);
			const float SignedDistance = (SurfaceValues[Idx] - ElevationValues[Idx]) / SlopeMagnitude;
			DryValues[Idx] = FMath::IsFinite(SignedDistance) ? FMath::Clamp(SignedDistance, -FluidWaterSdfLimitCm, FluidWaterSdfLimitCm) : FluidWaterUnknownTerrainSdfCm;
		}
	});
}

void FTDFluidGrid2DSolver::BuildWaterRowSegmentLists()
{
	const int32 GridSize = Resolution;
	const double RowSlackCm = 0.5 * Config.SimExtentCm.Y / GridSize + Config.FlowGuideBankFalloffCm;

	double RowWorldY[MaxGridResolution];
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		RowWorldY[Y] = CellToWorldCenter(0, Y).Y;
	}

	WaterRowSegmentOffsets.Init(0, GridSize + 1);
	for (const FWaterGuideSegment& Segment : WaterGuideSegments)
	{
		for (int32 Y = 0; Y < GridSize; ++Y)
		{
			if (RowWorldY[Y] >= Segment.MinimumY - RowSlackCm && RowWorldY[Y] <= Segment.MaximumY + RowSlackCm)
			{
				++WaterRowSegmentOffsets[Y + 1];
			}
		}
	}

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		WaterRowSegmentOffsets[Y + 1] += WaterRowSegmentOffsets[Y];
	}

	WaterRowSegmentIndices.SetNumUninitialized(WaterRowSegmentOffsets[GridSize]);
	int32 RowCursors[MaxGridResolution];
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		RowCursors[Y] = WaterRowSegmentOffsets[Y];
	}

	for (int32 SegmentIndex = 0; SegmentIndex < WaterGuideSegments.Num(); ++SegmentIndex)
	{
		const FWaterGuideSegment& Segment = WaterGuideSegments[SegmentIndex];
		for (int32 Y = 0; Y < GridSize; ++Y)
		{
			if (RowWorldY[Y] >= Segment.MinimumY - RowSlackCm && RowWorldY[Y] <= Segment.MaximumY + RowSlackCm)
			{
				WaterRowSegmentIndices[RowCursors[Y]++] = SegmentIndex;
			}
		}
	}
}

void FTDFluidGrid2DSolver::RasterizeWaterInputs()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_RasterizeWater);

	BuildWaterRowSegmentLists();

	const int32 GridSize = Resolution;
	const bool bRasterizesAllCells = bWaterRasterFullDirty;
	const bool bInitializesAllVelocities = bWaterVelocityInitPending;
	const float BankFalloffCm = Config.FlowGuideBankFalloffCm;
	const float BankSpeedRatio = Config.FlowGuideBankSpeedRatio;
	const int32* RowOffsets = WaterRowSegmentOffsets.GetData();
	const int32* RowSegmentIndices = WaterRowSegmentIndices.GetData();
	const FWaterGuideSegment* Segments = WaterGuideSegments.GetData();
	const FTDFluidWaterRegion* Regions = WaterRegions.GetData();
	const int32 RegionCount = WaterRegions.Num();
	const uint8* DirtyColumns = WaterDirtyColumns.GetData();
	const uint8* DirtyRows = WaterDirtyRows.GetData();
	float* SurfaceValues = WaterSurfaceHeight.GetData();
	float* TargetX = FlowTargetVelocityX.GetData();
	float* TargetY = FlowTargetVelocityY.GetData();
	float* WeightValues = FlowGuideWeight.GetData();
	float* DescentValues = FlowGuideDescent.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	ForEachGridRow(TEXT("TDFluidRasterizeWaterRows"), GridSize, [&](int32 Y)
	{
		const bool bIsDirtyRow = DirtyRows[Y] != 0;
		const int32 SegmentBegin = RowOffsets[Y];
		const int32 SegmentEnd = RowOffsets[Y + 1];

		for (int32 X = 0; X < GridSize; ++X)
		{
			const bool bIsNewCell = bIsDirtyRow || DirtyColumns[X] != 0;
			if (!bRasterizesAllCells && !bIsNewCell)
			{
				continue;
			}

			const FVector2D CellWorld = CellToWorldCenter(X, Y);
			double WeightSum = 0.0;
			double VelocitySumX = 0.0;
			double VelocitySumY = 0.0;
			float StrongestWeight = 0.0f;
			float StrongestSurface = 0.0f;
			float StrongestDescent = 0.0f;

			int32 CurrentGuideIndex = INDEX_NONE;
			const FWaterGuideSegment* NearestSegment = nullptr;
			float NearestDistanceCm = TNumericLimits<float>::Max();
			float NearestFraction = 0.0f;

			const auto FlushGuide = [&]()
			{
				if (NearestSegment == nullptr)
				{
					return;
				}

				const float HalfWidthCm = FMath::Lerp(NearestSegment->StartHalfWidthCm, NearestSegment->EndHalfWidthCm, NearestFraction);
				const float Weight = FluidWaterBankFalloffWeight(NearestDistanceCm, HalfWidthCm, BankFalloffCm);
				if (Weight > 0.0f)
				{
					const float NormalizedDistance = FMath::Clamp(NearestDistanceCm / HalfWidthCm, 0.0f, 1.0f);
					const float ProfileScale = BankSpeedRatio + (1.0f - BankSpeedRatio) * (1.0f - NormalizedDistance * NormalizedDistance);
					const float Speed = FMath::Lerp(NearestSegment->StartSpeedCmPerSec, NearestSegment->EndSpeedCmPerSec, NearestFraction) * ProfileScale;
					WeightSum += Weight;
					VelocitySumX += Weight * NearestSegment->Direction.X * Speed;
					VelocitySumY += Weight * NearestSegment->Direction.Y * Speed;
					if (Weight > StrongestWeight)
					{
						StrongestWeight = Weight;
						StrongestSurface = FMath::Lerp(NearestSegment->StartSurfaceCm, NearestSegment->EndSurfaceCm, NearestFraction);
						StrongestDescent = NearestSegment->DescentSlope;
					}
				}

				NearestSegment = nullptr;
				NearestDistanceCm = TNumericLimits<float>::Max();
			};

			for (int32 ListIndex = SegmentBegin; ListIndex < SegmentEnd; ++ListIndex)
			{
				const FWaterGuideSegment& Segment = Segments[RowSegmentIndices[ListIndex]];
				if (Segment.GuideIndex != CurrentGuideIndex)
				{
					FlushGuide();
					CurrentGuideIndex = Segment.GuideIndex;
				}

				if (CellWorld.X < Segment.MinimumX || CellWorld.X > Segment.MaximumX)
				{
					continue;
				}

				const double AlongCm = FMath::Clamp(FVector2D::DotProduct(CellWorld - Segment.Start, Segment.Direction), 0.0, Segment.LengthCm);
				const FVector2D ClosestPoint = Segment.Start + Segment.Direction * AlongCm;
				const float DistanceCm = static_cast<float>((CellWorld - ClosestPoint).Size());
				if (DistanceCm < NearestDistanceCm)
				{
					NearestSegment = &Segment;
					NearestDistanceCm = DistanceCm;
					NearestFraction = static_cast<float>(AlongCm / Segment.LengthCm);
				}
			}
			FlushGuide();

			const int32 Idx = Y * GridSize + X;
			if (WeightSum > 0.0)
			{
				SurfaceValues[Idx] = StrongestSurface;
				TargetX[Idx] = static_cast<float>(VelocitySumX / WeightSum);
				TargetY[Idx] = static_cast<float>(VelocitySumY / WeightSum);
				WeightValues[Idx] = StrongestWeight;
				DescentValues[Idx] = StrongestDescent;
			}
			else
			{
				float RegionSurface = NoWaterSurfaceHeight;
				for (int32 RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
				{
					const FTDFluidWaterRegion& Region = Regions[RegionIndex];
					const bool bIsInsideRegion = CellWorld.X >= Region.MinXY.X && CellWorld.X <= Region.MaxXY.X
						&& CellWorld.Y >= Region.MinXY.Y && CellWorld.Y <= Region.MaxXY.Y;
					if (bIsInsideRegion)
					{
						RegionSurface = FMath::Max(RegionSurface, Region.SurfaceHeightCm);
					}
				}
				SurfaceValues[Idx] = RegionSurface;
				TargetX[Idx] = 0.0f;
				TargetY[Idx] = 0.0f;
				WeightValues[Idx] = 0.0f;
				DescentValues[Idx] = 0.0f;
			}

			if (bInitializesAllVelocities || bIsNewCell)
			{
				VelX[Idx] = TargetX[Idx] * WeightValues[Idx];
				VelY[Idx] = TargetY[Idx] * WeightValues[Idx];
			}
		}
	});

	WaterDirtyColumns.Init(0, GridSize);
	WaterDirtyRows.Init(0, GridSize);
	bWaterRasterFullDirty = false;
	bWaterHasDirtyStrips = false;
	bWaterVelocityInitPending = false;
	bWaterDryDirty = true;
}

void FTDFluidGrid2DSolver::PrepareWaterInputs()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_PrepareWaterInputs);

	if (bWaterRasterFullDirty || bWaterHasDirtyStrips)
	{
		RasterizeWaterInputs();
	}

	if (!bWaterDryDirty)
	{
		return;
	}

	RefreshWaterDrySdf();
	ComposeWaterObstacleSdf();
	bWaterDryDirty = false;
}

void FTDFluidGrid2DSolver::StepFluidSimWater(float DeltaTime)
{
	PrepareWaterInputs();
	ApplyTerrainSlopeAccelerationDryOnly(DeltaTime);
	ApplyFlowNoiseAcceleration(DeltaTime);
	ApplyFlowGuideAcceleration(DeltaTime);
	EnforceObstacleBoundaries();
	ApplyVorticityConfinement(DeltaTime);
	SolvePressureProjectionWater();
	AdvectVelocityWater(DeltaTime);
	EnforceObstacleBoundaries();
	SolvePressureProjectionWater();
	EnforceObstacleBoundaries();
	AdvectDensities(DeltaTime);
	AddWhitewaterSources(DeltaTime);
	StepRipples(DeltaTime);
}

void FTDFluidGrid2DSolver::ApplyTerrainSlopeAccelerationDryOnly(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_TerrainSlopeDryOnly);

	if (Config.TerrainSlopeGravity <= 0.0f || bIsTerrainSlopeZero)
	{
		return;
	}

	const int32 GridSize = Resolution;
	const float SlopeGravity = Config.TerrainSlopeGravity;
	const uint8* SolidCells = SolidCellMask.GetData();
	const uint8* KnownValues = TerrainKnownMask.GetData();
	const float* SurfaceValues = WaterSurfaceHeight.GetData();
	const float* ElevationValues = TerrainElevation.GetData();
	const FVector2D* SlopeValues = TerrainSlope.GetData();
	const float* PrimaryValues = PrimaryDensity.GetData();
	const float* SelectiveValues = SelectiveDensity.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	ForEachGridRow(TEXT("TDFluidTerrainSlopeDryRows"), GridSize, [&](int32 Y)
	{
		const int32 RowStart = Y * GridSize;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = RowStart + X;
			if (SolidCells[Idx])
			{
				continue;
			}

			const bool bIsSubmerged = SurfaceValues[Idx] != NoWaterSurfaceHeight && (KnownValues[Idx] == 0 || SurfaceValues[Idx] > ElevationValues[Idx]);
			if (bIsSubmerged)
			{
				continue;
			}

			const float ActiveFluidMask = FMath::Clamp(PrimaryValues[Idx] + SelectiveValues[Idx] + 0.1f, 0.0f, 1.0f);
			VelX[Idx] -= SlopeValues[Idx].X * SlopeGravity * ActiveFluidMask * DeltaTime;
			VelY[Idx] -= SlopeValues[Idx].Y * SlopeGravity * ActiveFluidMask * DeltaTime;
		}
	});
}

void FTDFluidGrid2DSolver::ApplyFlowGuideAcceleration(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_FlowGuide);

	if (Config.FlowGuideResponsePerSec <= 0.0f)
	{
		return;
	}

	const int32 GridSize = Resolution;
	const float ResponseBlend = 1.0f - FMath::Exp(-Config.FlowGuideResponsePerSec * DeltaTime);
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* TargetX = FlowTargetVelocityX.GetData();
	const float* TargetY = FlowTargetVelocityY.GetData();
	const float* WeightValues = FlowGuideWeight.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	ForEachGridRow(TEXT("TDFluidFlowGuideRows"), GridSize, [&](int32 Y)
	{
		const int32 RowStart = Y * GridSize;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = RowStart + X;
			if (SolidCells[Idx] || WeightValues[Idx] <= 0.0f)
			{
				continue;
			}

			const float CellBlend = ResponseBlend * WeightValues[Idx];
			VelX[Idx] += (TargetX[Idx] - VelX[Idx]) * CellBlend;
			VelY[Idx] += (TargetY[Idx] - VelY[Idx]) * CellBlend;
		}
	});
}

void FTDFluidGrid2DSolver::AdvectVelocityWater(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_AdvectVelocityWater);

	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float StepScaleX = (DeltaTime * Config.AdvectionSpeedScale) / CellSizeX;
	const float StepScaleY = (DeltaTime * Config.AdvectionSpeedScale) / CellSizeY;
	const float DampingFactor = 1.0f / (1.0f + Config.VelocityDamping * DeltaTime * 15.0f);

	float BorderFadeByCoordinate[MaxGridResolution];
	float BorderDampingByCoordinate[MaxGridResolution];
	BuildBorderFadeTable(BorderFadeByCoordinate);
	BuildBorderDampingTable(BorderFadeByCoordinate, DeltaTime, BorderDampingByCoordinate);

	const int32 GridSize = Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* TargetX = FlowTargetVelocityX.GetData();
	const float* TargetY = FlowTargetVelocityY.GetData();
	const float* VelX = VelocityX.GetData();
	const float* VelY = VelocityY.GetData();
	float* AdvectedVelX = ScratchVelocityX.GetData();
	float* AdvectedVelY = ScratchVelocityY.GetData();

	ForEachGridRow(TEXT("TDFluidAdvectVelocityWaterRows"), GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx])
			{
				AdvectedVelX[Idx] = 0.0f;
				AdvectedVelY[Idx] = 0.0f;
				continue;
			}

			const float BacktraceX = static_cast<float>(X) - VelX[Idx] * StepScaleX;
			const float BacktraceY = static_cast<float>(Y) - VelY[Idx] * StepScaleY;
			const float BorderDamping = BorderFadeByCoordinate[X] <= BorderFadeByCoordinate[Y] ? BorderDampingByCoordinate[X] : BorderDampingByCoordinate[Y];
			const FTDFluidBilinearSample Sample = MakeBilinearSample(BacktraceX, BacktraceY, GridSize);

			const float AdvectedX = InterpolateBilinear(VelX, Sample) * DampingFactor;
			const float AdvectedY = InterpolateBilinear(VelY, Sample) * DampingFactor;
			AdvectedVelX[Idx] = TargetX[Idx] + (AdvectedX - TargetX[Idx]) * BorderDamping;
			AdvectedVelY[Idx] = TargetY[Idx] + (AdvectedY - TargetY[Idx]) * BorderDamping;
		}
	});

	Swap(VelocityX, ScratchVelocityX);
	Swap(VelocityY, ScratchVelocityY);
}

bool FTDFluidGrid2DSolver::HasOpenWaterBorderCell() const
{
	const int32 GridSize = Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* SurfaceValues = WaterSurfaceHeight.GetData();

	const auto IsOpenCell = [&](int32 X, int32 Y)
	{
		const int32 Idx = Y * GridSize + X;
		return SolidCells[Idx] == 0 && SurfaceValues[Idx] != NoWaterSurfaceHeight;
	};

	for (int32 EdgeIndex = 0; EdgeIndex < GridSize; ++EdgeIndex)
	{
		if (IsOpenCell(EdgeIndex, 0) || IsOpenCell(EdgeIndex, GridSize - 1) || IsOpenCell(0, EdgeIndex) || IsOpenCell(GridSize - 1, EdgeIndex))
		{
			return true;
		}
	}
	return false;
}

void FTDFluidGrid2DSolver::SolvePressureProjectionWater()
{
	if (!Config.bOpenWaterBorders || !HasOpenWaterBorderCell())
	{
		SolvePressureProjection();
		return;
	}

	float SpacingX = 1.0f;
	float SpacingY = 1.0f;
	ComputeRelativeCellSpacing(SpacingX, SpacingY);

	const float InvSpacingXSquared = 1.0f / (SpacingX * SpacingX);
	const float InvSpacingYSquared = 1.0f / (SpacingY * SpacingY);
	const float InvNeighborWeightSum = 1.0f / (2.0f * (InvSpacingXSquared + InvSpacingYSquared));
	const float SmallestCellSizeCm = static_cast<float>(FMath::Min(Config.SimExtentCm.X, Config.SimExtentCm.Y)) / static_cast<float>(Resolution);
	const float PressureDisplayScale = SmallestCellSizeCm / (PressureNormalizationCmPerSec * PressureReferenceCellSizeCm);

	ComputeVelocityDivergenceOpenBorders(SpacingX, SpacingY);
	RelaxPressureJacobiOpenBorders(InvSpacingXSquared, InvSpacingYSquared, InvNeighborWeightSum);
	SubtractPressureGradientOpenBorders(SpacingX, SpacingY, PressureDisplayScale);
}

void FTDFluidGrid2DSolver::ComputeVelocityDivergenceOpenBorders(float SpacingX, float SpacingY)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ProjectionDivergenceOpen);

	const int32 GridSize = Resolution;
	const float DoubleSpacingX = 2.0f * SpacingX;
	const float DoubleSpacingY = 2.0f * SpacingY;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* SurfaceValues = WaterSurfaceHeight.GetData();
	const float* VelX = VelocityX.GetData();
	const float* VelY = VelocityY.GetData();
	float* DivergenceValues = Divergence.GetData();
	float* PressureSolutionValues = PressureSolution.GetData();

	ForEachGridRow(TEXT("TDFluidDivergenceOpenRows"), GridSize, [&](int32 Y)
	{
		const int32 RowStart = Y * GridSize;
		const bool bIsEdgeRow = Y == 0 || Y == GridSize - 1;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = RowStart + X;
			if (SolidCells[Idx])
			{
				DivergenceValues[Idx] = 0.0f;
				PressureSolutionValues[Idx] = 0.0f;
				continue;
			}

			const bool bIsEdgeCell = bIsEdgeRow || X == 0 || X == GridSize - 1;
			const bool bIsOpenCell = bIsEdgeCell && SurfaceValues[Idx] != NoWaterSurfaceHeight;
			const float GhostVelocityX = bIsOpenCell ? VelX[Idx] : 0.0f;
			const float GhostVelocityY = bIsOpenCell ? VelY[Idx] : 0.0f;
			const float VelRight = X + 1 < GridSize ? (SolidCells[Idx + 1] ? 0.0f : VelX[Idx + 1]) : GhostVelocityX;
			const float VelLeft = X > 0 ? (SolidCells[Idx - 1] ? 0.0f : VelX[Idx - 1]) : GhostVelocityX;
			const float VelUp = Y + 1 < GridSize ? (SolidCells[Idx + GridSize] ? 0.0f : VelY[Idx + GridSize]) : GhostVelocityY;
			const float VelDown = Y > 0 ? (SolidCells[Idx - GridSize] ? 0.0f : VelY[Idx - GridSize]) : GhostVelocityY;
			DivergenceValues[Idx] = (VelRight - VelLeft) / DoubleSpacingX + (VelUp - VelDown) / DoubleSpacingY;
		}
	});
}

void FTDFluidGrid2DSolver::RelaxPressureJacobiOpenBorders(float InvSpacingXSquared, float InvSpacingYSquared, float InvNeighborWeightSum)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ProjectionJacobiOpen);

	const int32 GridSize = Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* SurfaceValues = WaterSurfaceHeight.GetData();
	const float* DivergenceValues = Divergence.GetData();

	for (int32 Iter = 0; Iter < Config.PressureIterations; ++Iter)
	{
		const float* SourcePressure = PressureSolution.GetData();
		float* TargetPressure = ScratchPressure.GetData();

		ForEachGridRow(TEXT("TDFluidJacobiOpenRows"), GridSize, [&](int32 Y)
		{
			const int32 RowStart = Y * GridSize;
			const bool bIsEdgeRow = Y == 0 || Y == GridSize - 1;
			for (int32 X = 0; X < GridSize; ++X)
			{
				const int32 Idx = RowStart + X;
				if (SolidCells[Idx])
				{
					TargetPressure[Idx] = 0.0f;
					continue;
				}

				const float CenterP = SourcePressure[Idx];
				const bool bIsEdgeCell = bIsEdgeRow || X == 0 || X == GridSize - 1;
				const bool bIsOpenCell = bIsEdgeCell && SurfaceValues[Idx] != NoWaterSurfaceHeight;
				const float GhostP = bIsOpenCell ? 0.0f : CenterP;
				const float PRight = X + 1 < GridSize ? (SolidCells[Idx + 1] ? CenterP : SourcePressure[Idx + 1]) : GhostP;
				const float PLeft = X > 0 ? (SolidCells[Idx - 1] ? CenterP : SourcePressure[Idx - 1]) : GhostP;
				const float PUp = Y + 1 < GridSize ? (SolidCells[Idx + GridSize] ? CenterP : SourcePressure[Idx + GridSize]) : GhostP;
				const float PDown = Y > 0 ? (SolidCells[Idx - GridSize] ? CenterP : SourcePressure[Idx - GridSize]) : GhostP;
				TargetPressure[Idx] = ((PLeft + PRight) * InvSpacingXSquared + (PDown + PUp) * InvSpacingYSquared - DivergenceValues[Idx]) * InvNeighborWeightSum;
			}
		});
		Swap(PressureSolution, ScratchPressure);
	}
}

void FTDFluidGrid2DSolver::SubtractPressureGradientOpenBorders(float SpacingX, float SpacingY, float PressureDisplayScale)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ProjectionGradientOpen);

	const int32 GridSize = Resolution;
	const float DoubleSpacingX = 2.0f * SpacingX;
	const float DoubleSpacingY = 2.0f * SpacingY;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* SurfaceValues = WaterSurfaceHeight.GetData();
	const float* PressureSolutionValues = PressureSolution.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();
	float* PressureValues = Pressure.GetData();

	ForEachGridRow(TEXT("TDFluidGradientOpenRows"), GridSize, [&](int32 Y)
	{
		const int32 RowStart = Y * GridSize;
		const bool bIsEdgeRow = Y == 0 || Y == GridSize - 1;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = RowStart + X;
			if (SolidCells[Idx])
			{
				VelX[Idx] = 0.0f;
				VelY[Idx] = 0.0f;
				PressureValues[Idx] = 0.0f;
				continue;
			}

			const float CenterP = PressureSolutionValues[Idx];
			const bool bIsEdgeCell = bIsEdgeRow || X == 0 || X == GridSize - 1;
			const bool bIsOpenCell = bIsEdgeCell && SurfaceValues[Idx] != NoWaterSurfaceHeight;
			const float GhostP = bIsOpenCell ? 0.0f : CenterP;
			const float PRight = X + 1 < GridSize ? (SolidCells[Idx + 1] ? CenterP : PressureSolutionValues[Idx + 1]) : GhostP;
			const float PLeft = X > 0 ? (SolidCells[Idx - 1] ? CenterP : PressureSolutionValues[Idx - 1]) : GhostP;
			const float PUp = Y + 1 < GridSize ? (SolidCells[Idx + GridSize] ? CenterP : PressureSolutionValues[Idx + GridSize]) : GhostP;
			const float PDown = Y > 0 ? (SolidCells[Idx - GridSize] ? CenterP : PressureSolutionValues[Idx - GridSize]) : GhostP;

			VelX[Idx] -= (PRight - PLeft) / DoubleSpacingX;
			VelY[Idx] -= (PUp - PDown) / DoubleSpacingY;
			PressureValues[Idx] = FMath::Clamp(CenterP * PressureDisplayScale, -1.0f, 1.0f);
		}
	});
}

void FTDFluidGrid2DSolver::AddWhitewaterSources(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_Whitewater);

	const float VorticityCoefficient = Config.WhitewaterFromVorticity;
	const float RapidsCoefficient = Config.RapidsWhitewaterPerSec;
	if (VorticityCoefficient <= 0.0f && RapidsCoefficient <= 0.0f)
	{
		return;
	}

	const int32 GridSize = Resolution;
	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float VorticityThreshold = Config.WhitewaterVorticityThresholdPerSec;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* SurfaceValues = WaterSurfaceHeight.GetData();
	const float* WeightValues = FlowGuideWeight.GetData();
	const float* DescentValues = FlowGuideDescent.GetData();
	const float* VelX = VelocityX.GetData();
	const float* VelY = VelocityY.GetData();
	float* PrimaryValues = PrimaryDensity.GetData();

	ForEachGridRow(TEXT("TDFluidWhitewaterRows"), GridSize, [&](int32 Y)
	{
		const int32 YMinus = FMath::Max(Y - 1, 0);
		const int32 YPlus = FMath::Min(Y + 1, GridSize - 1);
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx] || SurfaceValues[Idx] == NoWaterSurfaceHeight)
			{
				continue;
			}

			float Source = 0.0f;
			if (VorticityCoefficient > 0.0f)
			{
				const int32 XMinus = FMath::Max(X - 1, 0);
				const int32 XPlus = FMath::Min(X + 1, GridSize - 1);
				const int32 RightIdx = Y * GridSize + XPlus;
				const int32 LeftIdx = Y * GridSize + XMinus;
				const int32 UpIdx = YPlus * GridSize + X;
				const int32 DownIdx = YMinus * GridSize + X;
				const float RightVelY = SolidCells[RightIdx] ? VelY[Idx] : VelY[RightIdx];
				const float LeftVelY = SolidCells[LeftIdx] ? VelY[Idx] : VelY[LeftIdx];
				const float UpVelX = SolidCells[UpIdx] ? VelX[Idx] : VelX[UpIdx];
				const float DownVelX = SolidCells[DownIdx] ? VelX[Idx] : VelX[DownIdx];
				const float DvYDx = (RightVelY - LeftVelY) / (static_cast<float>(XPlus - XMinus) * CellSizeX);
				const float DvXDy = (UpVelX - DownVelX) / (static_cast<float>(YPlus - YMinus) * CellSizeY);
				Source += VorticityCoefficient * FMath::Max(0.0f, FMath::Abs(DvYDx - DvXDy) - VorticityThreshold) * DeltaTime;
			}

			if (RapidsCoefficient > 0.0f)
			{
				Source += RapidsCoefficient * DescentValues[Idx] * WeightValues[Idx] * DeltaTime;
			}

			if (Source > 0.0f)
			{
				PrimaryValues[Idx] = FMath::Min(1.0f, PrimaryValues[Idx] + Source);
			}
		}
	});
}

void FTDFluidGrid2DSolver::StepRipples(float DeltaTime)
{
	if (Config.RippleWaveSpeedCmPerSec <= 0.0f || !bHasRippleActivity)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_Ripples);

	const int32 GridSize = Resolution;
	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float WaveSpeed = Config.RippleWaveSpeedCmPerSec;
	const int32 SubstepCount = FMath::Clamp(FMath::CeilToInt(WaveSpeed * DeltaTime * FluidWaterSquareRootOfTwo / (FluidWaterRippleSubstepSafety * FMath::Min(CellSizeX, CellSizeY))), 1, FluidWaterMaxRippleSubsteps);
	const float SubstepSeconds = DeltaTime / static_cast<float>(SubstepCount);
	const float WaveSpeedSquared = WaveSpeed * WaveSpeed;
	const float InvCellSizeXSquared = 1.0f / (CellSizeX * CellSizeX);
	const float InvCellSizeYSquared = 1.0f / (CellSizeY * CellSizeY);
	const float DampingPerSec = Config.RippleDampingPerSec;

	float BorderFadeByCoordinate[MaxGridResolution];
	float BorderDampingByCoordinate[MaxGridResolution];
	BuildBorderFadeTable(BorderFadeByCoordinate);
	BuildBorderDampingTable(BorderFadeByCoordinate, SubstepSeconds, BorderDampingByCoordinate);

	const uint8* SolidCells = SolidCellMask.GetData();
	float* Heights = RippleHeight.GetData();
	float* Speeds = RippleHeightVelocity.GetData();

	for (int32 Substep = 0; Substep < SubstepCount; ++Substep)
	{
		ForEachGridRow(TEXT("TDFluidRippleAccelerateRows"), GridSize, [&](int32 Y)
		{
			for (int32 X = 0; X < GridSize; ++X)
			{
				const int32 Idx = Y * GridSize + X;
				if (SolidCells[Idx])
				{
					Speeds[Idx] = 0.0f;
					continue;
				}

				const float Center = Heights[Idx];
				const float Left = X > 0 ? Heights[Idx - 1] : Center;
				const float Right = X + 1 < GridSize ? Heights[Idx + 1] : Center;
				const float Down = Y > 0 ? Heights[Idx - GridSize] : Center;
				const float Up = Y + 1 < GridSize ? Heights[Idx + GridSize] : Center;
				const float Laplacian = (Left + Right - 2.0f * Center) * InvCellSizeXSquared + (Down + Up - 2.0f * Center) * InvCellSizeYSquared;
				Speeds[Idx] += (WaveSpeedSquared * Laplacian - DampingPerSec * Speeds[Idx]) * SubstepSeconds;
			}
		});

		ForEachGridRow(TEXT("TDFluidRippleIntegrateRows"), GridSize, [&](int32 Y)
		{
			for (int32 X = 0; X < GridSize; ++X)
			{
				const int32 Idx = Y * GridSize + X;
				if (SolidCells[Idx])
				{
					Heights[Idx] = 0.0f;
					Speeds[Idx] = 0.0f;
					continue;
				}

				const float BorderDamping = BorderFadeByCoordinate[X] <= BorderFadeByCoordinate[Y] ? BorderDampingByCoordinate[X] : BorderDampingByCoordinate[Y];
				Heights[Idx] = (Heights[Idx] + Speeds[Idx] * SubstepSeconds) * BorderDamping;
				Speeds[Idx] *= BorderDamping;
			}
		});
	}

	if (Config.RippleFlowAdvection > 0.0f)
	{
		AdvectRipples(DeltaTime);
		Heights = RippleHeight.GetData();
		Speeds = RippleHeightVelocity.GetData();
	}

	const int32 CellCount = GridSize * GridSize;
	float PeakMagnitude = 0.0f;
	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		PeakMagnitude = FMath::Max(PeakMagnitude, FMath::Max(FMath::Abs(Heights[Idx]), FMath::Abs(Speeds[Idx])));
	}

	if (PeakMagnitude < FluidWaterRippleQuietThreshold)
	{
		FMemory::Memzero(Heights, sizeof(float) * CellCount);
		FMemory::Memzero(Speeds, sizeof(float) * CellCount);
		bHasRippleActivity = false;
	}
}

void FTDFluidGrid2DSolver::AdvectRipples(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_RippleAdvect);

	const int32 GridSize = Resolution;
	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float StepScaleX = (DeltaTime * Config.RippleFlowAdvection) / CellSizeX;
	const float StepScaleY = (DeltaTime * Config.RippleFlowAdvection) / CellSizeY;
	const float MaximumCoordinate = static_cast<float>(GridSize - 1);

	const uint8* SolidCells = SolidCellMask.GetData();
	const float* VelX = VelocityX.GetData();
	const float* VelY = VelocityY.GetData();
	const float* Heights = RippleHeight.GetData();
	const float* Speeds = RippleHeightVelocity.GetData();
	float* AdvectedHeights = ScratchRippleHeight.GetData();
	float* AdvectedSpeeds = ScratchRippleHeightVelocity.GetData();

	ForEachGridRow(TEXT("TDFluidRippleAdvectRows"), GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx])
			{
				AdvectedHeights[Idx] = 0.0f;
				AdvectedSpeeds[Idx] = 0.0f;
				continue;
			}

			const float BacktraceX = FMath::Clamp(static_cast<float>(X) - VelX[Idx] * StepScaleX, 0.0f, MaximumCoordinate);
			const float BacktraceY = FMath::Clamp(static_cast<float>(Y) - VelY[Idx] * StepScaleY, 0.0f, MaximumCoordinate);
			const int32 BaseX = FMath::FloorToInt(BacktraceX);
			const int32 BaseY = FMath::FloorToInt(BacktraceY);

			float WeightsX[4];
			float WeightsY[4];
			FluidWaterBuildCubicWeights(BacktraceX - static_cast<float>(BaseX), WeightsX);
			FluidWaterBuildCubicWeights(BacktraceY - static_cast<float>(BaseY), WeightsY);

			int32 IndicesX[4];
			int32 IndicesY[4];
			for (int32 TapIndex = 0; TapIndex < 4; ++TapIndex)
			{
				IndicesX[TapIndex] = FMath::Clamp(BaseX - 1 + TapIndex, 0, GridSize - 1);
				IndicesY[TapIndex] = FMath::Clamp(BaseY - 1 + TapIndex, 0, GridSize - 1);
			}

			float HeightSum = 0.0f;
			float SpeedSum = 0.0f;
			float HeightMinimum = TNumericLimits<float>::Max();
			float HeightMaximum = -TNumericLimits<float>::Max();
			float SpeedMinimum = TNumericLimits<float>::Max();
			float SpeedMaximum = -TNumericLimits<float>::Max();
			for (int32 TapY = 0; TapY < 4; ++TapY)
			{
				const float* HeightRow = Heights + IndicesY[TapY] * GridSize;
				const float* SpeedRow = Speeds + IndicesY[TapY] * GridSize;
				float RowHeight = 0.0f;
				float RowSpeed = 0.0f;
				for (int32 TapX = 0; TapX < 4; ++TapX)
				{
					const float TapHeight = HeightRow[IndicesX[TapX]];
					const float TapSpeed = SpeedRow[IndicesX[TapX]];
					RowHeight += WeightsX[TapX] * TapHeight;
					RowSpeed += WeightsX[TapX] * TapSpeed;
					HeightMinimum = FMath::Min(HeightMinimum, TapHeight);
					HeightMaximum = FMath::Max(HeightMaximum, TapHeight);
					SpeedMinimum = FMath::Min(SpeedMinimum, TapSpeed);
					SpeedMaximum = FMath::Max(SpeedMaximum, TapSpeed);
				}
				HeightSum += WeightsY[TapY] * RowHeight;
				SpeedSum += WeightsY[TapY] * RowSpeed;
			}

			const float HeightMargin = FluidWaterRippleOvershootRatio * (HeightMaximum - HeightMinimum);
			const float SpeedMargin = FluidWaterRippleOvershootRatio * (SpeedMaximum - SpeedMinimum);
			AdvectedHeights[Idx] = FMath::Clamp(HeightSum, HeightMinimum - HeightMargin, HeightMaximum + HeightMargin);
			AdvectedSpeeds[Idx] = FMath::Clamp(SpeedSum, SpeedMinimum - SpeedMargin, SpeedMaximum + SpeedMargin);
		}
	});

	Swap(RippleHeight, ScratchRippleHeight);
	Swap(RippleHeightVelocity, ScratchRippleHeightVelocity);
}

void FTDFluidGrid2DSolver::PackWaterSurfacePixels(TArray<FFloat16Color>& OutPixels, float TerrainReferenceZCm) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_PackWaterSurface);

	if (!IsWaterLayerActive())
	{
		OutPixels.Reset();
		return;
	}

	const int32 GridSize = Resolution;
	OutPixels.SetNumUninitialized(GridSize * GridSize);

	const float DepthReferenceCm = Config.WaterDepthReferenceCm;
	const float RippleReferenceCm = Config.RippleHeightReferenceCm;
	const uint8* KnownValues = TerrainKnownMask.GetData();
	const float* ElevationValues = TerrainElevation.GetData();
	const float* WetnessValues = Wetness.GetData();
	const float* RippleValues = RippleHeight.GetData();
	FFloat16Color* Pixels = OutPixels.GetData();

	ParallelFor(GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			const float NormalizedDepth = FMath::Clamp(ComputeWaterDepthAtIndex(Idx) / DepthReferenceCm, 0.0f, 1.0f);
			const float NormalizedWetness = FMath::Clamp(WetnessValues[Idx], 0.0f, 1.0f);
			const float NormalizedRipple = 0.5f + 0.5f * FMath::Clamp(RippleValues[Idx] / RippleReferenceCm, -1.0f, 1.0f);
			const float NormalizedTerrain = KnownValues[Idx] != 0
				? FMath::Clamp((ElevationValues[Idx] - TerrainReferenceZCm) / FluidWaterTerrainHeightScaleCm, -FluidWaterTerrainHeightLimit, FluidWaterTerrainHeightLimit)
				: FluidWaterTerrainHeightLimit;
			Pixels[Idx] = FFloat16Color(FLinearColor(NormalizedDepth, NormalizedWetness, NormalizedRipple, NormalizedTerrain));
		}
	});
}

uint32 FTDFluidGrid2DSolver::ComputeWaterStateChecksum() const
{
	const TArray<float>* const WaterFields[] = {
		&WaterSurfaceHeight,
		&WaterDrySdf,
		&ObstacleSdfRaw,
		&FlowTargetVelocityX,
		&FlowTargetVelocityY,
		&FlowGuideWeight,
		&FlowGuideDescent,
		&Wetness,
		&RippleHeight,
		&RippleHeightVelocity,
	};

	uint32 Checksum = FCrc::MemCrc32(&TerrainKnownCellCount, sizeof(TerrainKnownCellCount));
	for (const TArray<float>* Field : WaterFields)
	{
		Checksum = FCrc::MemCrc32(Field->GetData(), Field->Num() * sizeof(float), Checksum);
	}
	return FCrc::MemCrc32(TerrainKnownMask.GetData(), TerrainKnownMask.Num(), Checksum);
}

namespace
{
	bool TryComputeVortexWeight(float OffsetX, float OffsetY, float VortexRadiusCm, float& OutWeight, float& OutDistanceCm)
	{
		OutDistanceCm = FMath::Sqrt(OffsetX * OffsetX + OffsetY * OffsetY);
		OutWeight = 0.0f;
		if (!(OutDistanceCm > UE_KINDA_SMALL_NUMBER) || OutDistanceCm >= VortexRadiusCm)
		{
			return false;
		}

		const float RadiusRatio = OutDistanceCm / VortexRadiusCm;
		const float Falloff = 1.0f - RadiusRatio * RadiusRatio;
		OutWeight = Falloff * Falloff;
		return true;
	}

	void ReflectOutwardVelocity(float& InOutVelocity, float OutwardSign, float ReflectionScale)
	{
		const float OutwardSpeed = InOutVelocity * OutwardSign;
		if (OutwardSpeed <= 0.0f)
		{
			return;
		}

		InOutVelocity -= ReflectionScale * OutwardSpeed * OutwardSign;
	}
}

bool FTDFluidGrid2DSolver::IsHeatChannelActive() const
{
	return Config.bEnableHeatChannel && Config.SimMode == ETDFluidSimMode::FluidSim && !Config.bEnableWaterLayer;
}

void FTDFluidGrid2DSolver::SynchronizeHeatBuffersWithConfig()
{
	const int32 CellCount = Resolution * Resolution;
	if (CellCount <= 0 || !IsHeatChannelActive())
	{
		Heat.Empty();
		ScratchHeat.Empty();
		return;
	}

	if (Heat.Num() != CellCount)
	{
		Heat.Init(0.0f, CellCount);
	}
	if (ScratchHeat.Num() != CellCount)
	{
		ScratchHeat.Init(0.0f, CellCount);
	}
}

void FTDFluidGrid2DSolver::ApplyHeatStamp(const FTDFluidBrushStamp& Stamp, float DeltaTime)
{
	const bool bErasesHeat = Stamp.DensityStrength < 0.0f;
	if (!bErasesHeat && Stamp.HeatStrength <= 0.0f)
	{
		return;
	}

	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float MinBoundsX = CenterWorld.X - 0.5f * Config.SimExtentCm.X;
	const float MinBoundsY = CenterWorld.Y - 0.5f * Config.SimExtentCm.Y;
	const float MinRadius = FMath::Max(CellSizeX, CellSizeY) * 0.5f;
	const float MaxRadius = FMath::Max(Config.SimExtentCm.X, Config.SimExtentCm.Y);
	const float EffectiveRadius = FMath::Clamp(Stamp.RadiusCm, MinRadius, MaxRadius);
	const FVector2D StampCenter(Stamp.WorldLocation.X, Stamp.WorldLocation.Y);

	const bool bIsOutsideGrid = StampCenter.X + EffectiveRadius < MinBoundsX
		|| StampCenter.X - EffectiveRadius > MinBoundsX + Config.SimExtentCm.X
		|| StampCenter.Y + EffectiveRadius < MinBoundsY
		|| StampCenter.Y - EffectiveRadius > MinBoundsY + Config.SimExtentCm.Y;
	if (bIsOutsideGrid)
	{
		return;
	}

	const float HeatStrength = FMath::Clamp(Stamp.HeatStrength, 0.0f, 1.0f);
	const float EraseRatePerSec = FMath::Abs(Stamp.DensityStrength) * BrushEraseRatePerSec;
	const int32 MinCellX = FMath::Clamp(FMath::FloorToInt((StampCenter.X - EffectiveRadius - MinBoundsX) / CellSizeX), 0, Resolution - 1);
	const int32 MaxCellX = FMath::Clamp(FMath::CeilToInt((StampCenter.X + EffectiveRadius - MinBoundsX) / CellSizeX), 0, Resolution - 1);
	const int32 MinCellY = FMath::Clamp(FMath::FloorToInt((StampCenter.Y - EffectiveRadius - MinBoundsY) / CellSizeY), 0, Resolution - 1);
	const int32 MaxCellY = FMath::Clamp(FMath::CeilToInt((StampCenter.Y + EffectiveRadius - MinBoundsY) / CellSizeY), 0, Resolution - 1);
	float* HeatValues = Heat.GetData();

	for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
	{
		for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
		{
			if (IsSolidCell(CellX, CellY))
			{
				continue;
			}

			const float DistanceCm = FVector2D::Distance(CellToWorldCenter(CellX, CellY), StampCenter);
			if (DistanceCm > EffectiveRadius)
			{
				continue;
			}

			const float NormalizedFalloff = 1.0f - (DistanceCm / EffectiveRadius);
			const float SmoothWeight = NormalizedFalloff * NormalizedFalloff * (3.0f - 2.0f * NormalizedFalloff);
			const int32 CellIdx = ToCellIndex(CellX, CellY);
			if (bErasesHeat)
			{
				HeatValues[CellIdx] *= FMath::Exp(-EraseRatePerSec * SmoothWeight * DeltaTime);
				continue;
			}

			HeatValues[CellIdx] = FMath::Max(HeatValues[CellIdx], FMath::Clamp(HeatStrength * SmoothWeight, 0.0f, 1.0f));
		}
	}
}

void FTDFluidGrid2DSolver::ApplyHeatBuoyancyAcceleration(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_HeatBuoyancy);

	if (Heat.IsEmpty() || Config.HeatBuoyancyCmPerSec2 <= 0.0f || PlaneUpDirection.IsNearlyZero())
	{
		return;
	}

	const int32 CellCount = Resolution * Resolution;
	const float VelocityStepX = static_cast<float>(PlaneUpDirection.X) * Config.HeatBuoyancyCmPerSec2 * DeltaTime;
	const float VelocityStepY = static_cast<float>(PlaneUpDirection.Y) * Config.HeatBuoyancyCmPerSec2 * DeltaTime;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* HeatValues = Heat.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		if (SolidCells[Idx])
		{
			continue;
		}

		VelX[Idx] += VelocityStepX * HeatValues[Idx];
		VelY[Idx] += VelocityStepY * HeatValues[Idx];
	}
}

void FTDFluidGrid2DSolver::ApplyVortexAcceleration(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_VortexForce);

	const float ExtentX = static_cast<float>(Config.SimExtentCm.X);
	const float ExtentY = static_cast<float>(Config.SimExtentCm.Y);
	const float VortexRadiusCm = Config.VortexRadiusRatio * FMath::Min(ExtentX, ExtentY);
	if (Config.VortexTangentialAccelerationCmPerSec2 == 0.0f || VortexRadiusCm <= 0.0f)
	{
		return;
	}

	const int32 GridSize = Resolution;
	const float CellSizeX = ExtentX / static_cast<float>(GridSize);
	const float CellSizeY = ExtentY / static_cast<float>(GridSize);
	const float TangentialStep = Config.VortexTangentialAccelerationCmPerSec2 * DeltaTime;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	ForEachGridRow(TEXT("TDFluidVortexRows"), GridSize, [&](int32 Y)
	{
		const float OffsetY = (static_cast<float>(Y) + 0.5f) * CellSizeY - 0.5f * ExtentY;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx])
			{
				continue;
			}

			const float OffsetX = (static_cast<float>(X) + 0.5f) * CellSizeX - 0.5f * ExtentX;
			float Weight = 0.0f;
			float DistanceCm = 0.0f;
			if (!TryComputeVortexWeight(OffsetX, OffsetY, VortexRadiusCm, Weight, DistanceCm))
			{
				continue;
			}

			const float Scale = Weight * TangentialStep / DistanceCm;
			VelX[Idx] += -OffsetY * Scale;
			VelY[Idx] += OffsetX * Scale;
		}
	});
}

void FTDFluidGrid2DSolver::AccumulateVortexRadialDrift()
{
	const float ExtentX = static_cast<float>(Config.SimExtentCm.X);
	const float ExtentY = static_cast<float>(Config.SimExtentCm.Y);
	const float VortexRadiusCm = Config.VortexRadiusRatio * FMath::Min(ExtentX, ExtentY);
	if (Config.VortexRadialDriftCmPerSec == 0.0f || VortexRadiusCm <= 0.0f)
	{
		return;
	}

	const int32 GridSize = Resolution;
	const float CellSizeX = ExtentX / static_cast<float>(GridSize);
	const float CellSizeY = ExtentY / static_cast<float>(GridSize);
	const float InwardDrift = Config.VortexRadialDriftCmPerSec;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* PushX = PendingRadialPushX.GetData();
	float* PushY = PendingRadialPushY.GetData();

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		const float OffsetY = (static_cast<float>(Y) + 0.5f) * CellSizeY - 0.5f * ExtentY;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx])
			{
				continue;
			}

			const float OffsetX = (static_cast<float>(X) + 0.5f) * CellSizeX - 0.5f * ExtentX;
			float Weight = 0.0f;
			float DistanceCm = 0.0f;
			if (!TryComputeVortexWeight(OffsetX, OffsetY, VortexRadiusCm, Weight, DistanceCm))
			{
				continue;
			}

			const float Scale = InwardDrift * Weight / DistanceCm;
			PushX[Idx] += -OffsetX * Scale;
			PushY[Idx] += -OffsetY * Scale;
			bHasPendingRadialPush = true;
		}
	}
}

void FTDFluidGrid2DSolver::EnforceObstacleBoundariesWithReflection()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ObstacleReflection);

	const float CellWorldSize = FMath::Max(Config.SimExtentCm.X, Config.SimExtentCm.Y) / static_cast<float>(Resolution);
	if (ObstacleSdfMinimum >= CellWorldSize * 1.5f)
	{
		return;
	}

	float SpacingX = 1.0f;
	float SpacingY = 1.0f;
	ComputeRelativeCellSpacing(SpacingX, SpacingY);

	const int32 GridSize = Resolution;
	const float ReflectionScale = 1.0f + Config.EdgeBounciness;
	const float* SdfValues = ObstacleSdf.GetData();
	float* PrimaryValues = PrimaryDensity.GetData();
	float* SelectiveValues = SelectiveDensity.GetData();
	float* DisplacementValues = SurfaceDisplacement.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();
	float* PressureValues = Pressure.GetData();
	float* PressureSolutionValues = PressureSolution.GetData();

	ForEachGridRow(TEXT("TDFluidObstacleReflectionRows"), GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			const float SignedDistance = SdfValues[Idx];

			if (SignedDistance <= 0.0f)
			{
				PrimaryValues[Idx] = 0.0f;
				SelectiveValues[Idx] = 0.0f;
				DisplacementValues[Idx] = 0.0f;
				VelX[Idx] = 0.0f;
				VelY[Idx] = 0.0f;
				PressureValues[Idx] = 0.0f;
				PressureSolutionValues[Idx] = 0.0f;
				continue;
			}

			if (SignedDistance >= CellWorldSize * 1.5f)
			{
				continue;
			}

			const int32 XMinus = FMath::Max(X - 1, 0);
			const int32 XPlus = FMath::Min(X + 1, GridSize - 1);
			const int32 YMinus = FMath::Max(Y - 1, 0);
			const int32 YPlus = FMath::Min(Y + 1, GridSize - 1);

			const float GradientSpanX = static_cast<float>(XPlus - XMinus) * SpacingX;
			const float GradientSpanY = static_cast<float>(YPlus - YMinus) * SpacingY;
			FVector2D Normal(
				(SdfValues[Y * GridSize + XPlus] - SdfValues[Y * GridSize + XMinus]) / GradientSpanX,
				(SdfValues[YPlus * GridSize + X] - SdfValues[YMinus * GridSize + X]) / GradientSpanY);

			if (!Normal.Normalize())
			{
				continue;
			}

			FVector2D CurrentVelocity(VelX[Idx], VelY[Idx]);
			const float NormalDot = FVector2D::DotProduct(CurrentVelocity, Normal);
			if (NormalDot < 0.0f)
			{
				CurrentVelocity -= Normal * (ReflectionScale * NormalDot);
				VelX[Idx] = CurrentVelocity.X;
				VelY[Idx] = CurrentVelocity.Y;
			}
		}
	});
}

void FTDFluidGrid2DSolver::ApplyEdgeReflection()
{
	const int32 GridSize = Resolution;
	const int32 LastCell = GridSize - 1;
	const float ReflectionScale = 1.0f + Config.EdgeBounciness;
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		ReflectOutwardVelocity(VelX[Y * GridSize], -1.0f, ReflectionScale);
		ReflectOutwardVelocity(VelX[Y * GridSize + LastCell], 1.0f, ReflectionScale);
	}

	for (int32 X = 0; X < GridSize; ++X)
	{
		ReflectOutwardVelocity(VelY[X], -1.0f, ReflectionScale);
		ReflectOutwardVelocity(VelY[LastCell * GridSize + X], 1.0f, ReflectionScale);
	}
}

void FTDFluidGrid2DSolver::AdvectHeat(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_AdvectHeat);

	if (Heat.IsEmpty())
	{
		return;
	}

	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const float StepScaleX = (DeltaTime * Config.AdvectionSpeedScale) / CellSizeX;
	const float StepScaleY = (DeltaTime * Config.AdvectionSpeedScale) / CellSizeY;
	const float HeatDissipationFactor = 1.0f / (1.0f + Config.HeatCoolingPerSec * DeltaTime);

	const int32 GridSize = Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	const float* VelX = VelocityX.GetData();
	const float* VelY = VelocityY.GetData();
	const float* HeatValues = Heat.GetData();
	float* AdvectedHeat = ScratchHeat.GetData();

	ForEachGridRow(TEXT("TDFluidAdvectHeatRows"), GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			if (SolidCells[Idx])
			{
				AdvectedHeat[Idx] = 0.0f;
				continue;
			}

			const float BacktraceX = static_cast<float>(X) - VelX[Idx] * StepScaleX;
			const float BacktraceY = static_cast<float>(Y) - VelY[Idx] * StepScaleY;
			const FTDFluidBilinearSample Sample = MakeBilinearSample(BacktraceX, BacktraceY, GridSize);
			AdvectedHeat[Idx] = FMath::Clamp(InterpolateBilinear(HeatValues, Sample) * HeatDissipationFactor, 0.0f, 1.0f);
		}
	});

	Swap(Heat, ScratchHeat);
}

void FTDFluidGrid2DSolver::ApplyDirectionalOutflowToHeat(float DeltaTime)
{
	if (Heat.IsEmpty() || Config.OutflowRatePerSec <= 0.0f || Config.OutflowBandRatio <= 0.0f || PlaneUpDirection.IsNearlyZero())
	{
		return;
	}

	const float BandWidth = 2.0f * Config.OutflowBandRatio;
	const float BandStart = 1.0f - BandWidth;
	const float InverseResolution = 1.0f / static_cast<float>(Resolution);
	const int32 GridSize = Resolution;
	float* HeatValues = Heat.GetData();

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		const float OffsetY = (static_cast<float>(Y) + 0.5f) * InverseResolution - 0.5f;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const float OffsetX = (static_cast<float>(X) + 0.5f) * InverseResolution - 0.5f;
			const float BandPosition = (OffsetX * static_cast<float>(PlaneUpDirection.X) + OffsetY * static_cast<float>(PlaneUpDirection.Y)) / 0.5f;
			if (BandPosition <= BandStart)
			{
				continue;
			}

			const float BandWeight = FMath::Clamp((BandPosition - BandStart) / BandWidth, 0.0f, 1.0f);
			HeatValues[Y * GridSize + X] *= FMath::Exp(-Config.OutflowRatePerSec * BandWeight * DeltaTime);
		}
	}
}

void FTDFluidGrid2DSolver::BuildBorderAlphaTable(float* OutAlphaByCoordinate) const
{
	BuildBorderFadeTable(OutAlphaByCoordinate);
	if (Config.BorderFadeExponent == 1.0f)
	{
		return;
	}

	for (int32 Coordinate = 0; Coordinate < Resolution; ++Coordinate)
	{
		OutAlphaByCoordinate[Coordinate] = FMath::Pow(OutAlphaByCoordinate[Coordinate], Config.BorderFadeExponent);
	}
}

void FTDFluidGrid2DSolver::PackAuxPixels(TArray<FFloat16Color>& OutAuxPixels) const
{
	if (!IsInitialized() || Heat.IsEmpty())
	{
		OutAuxPixels.Reset();
		return;
	}

	const int32 CellCount = Resolution * Resolution;
	OutAuxPixels.SetNumUninitialized(CellCount);
	const float* HeatValues = Heat.GetData();
	FFloat16Color* AuxPixels = OutAuxPixels.GetData();
	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		AuxPixels[Idx] = FFloat16Color(FLinearColor(FMath::Clamp(HeatValues[Idx], 0.0f, 1.0f), 0.0f, 0.0f, 0.0f));
	}
}
