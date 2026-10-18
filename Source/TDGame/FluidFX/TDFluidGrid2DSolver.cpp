#include "FluidFX/TDFluidGrid2DSolver.h"
#include "Async/ParallelFor.h"
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
	}

	bool IsFiniteBrushStamp(const FTDFluidBrushStamp& Stamp)
	{
		return !Stamp.WorldLocation.ContainsNaN()
			&& !Stamp.WorldVelocity.ContainsNaN()
			&& FMath::IsFinite(Stamp.RadiusCm)
			&& FMath::IsFinite(Stamp.DensityStrength)
			&& FMath::IsFinite(Stamp.VelocityStrength)
			&& FMath::IsFinite(Stamp.RadialVelocityCmPerSec);
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

	Config = InConfig;
	SanitizeConfig(Config);
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

	const int32 CellCount = Resolution * Resolution;
	TArray<float> ShiftedPrimary;
	TArray<float> ShiftedSelective;
	TArray<float> ShiftedDisplacement;
	TArray<float> ShiftedVelX;
	TArray<float> ShiftedVelY;
	TArray<float> ShiftedPressure;
	TArray<float> ShiftedPressureSolution;
	TArray<float> ShiftedElevation;
	TArray<FVector2D> ShiftedSlope;

	ShiftedPrimary.Init(0.0f, CellCount);
	ShiftedSelective.Init(0.0f, CellCount);
	ShiftedDisplacement.Init(0.0f, CellCount);
	ShiftedVelX.Init(0.0f, CellCount);
	ShiftedVelY.Init(0.0f, CellCount);
	ShiftedPressure.Init(0.0f, CellCount);
	ShiftedPressureSolution.Init(0.0f, CellCount);
	ShiftedElevation.Init(0.0f, CellCount);
	ShiftedSlope.Init(FVector2D::ZeroVector, CellCount);

	for (int32 NewY = 0; NewY < Resolution; ++NewY)
	{
		const int32 OldY = NewY + ShiftCellsY;
		if (OldY < 0 || OldY >= Resolution)
		{
			continue;
		}

		for (int32 NewX = 0; NewX < Resolution; ++NewX)
		{
			const int32 OldX = NewX + ShiftCellsX;
			if (OldX < 0 || OldX >= Resolution)
			{
				continue;
			}

			const int32 NewIdx = ToCellIndex(NewX, NewY);
			const int32 OldIdx = ToCellIndex(OldX, OldY);
			ShiftedPrimary[NewIdx] = PrimaryDensity[OldIdx];
			ShiftedSelective[NewIdx] = SelectiveDensity[OldIdx];
			ShiftedDisplacement[NewIdx] = SurfaceDisplacement[OldIdx];
			ShiftedVelX[NewIdx] = VelocityX[OldIdx];
			ShiftedVelY[NewIdx] = VelocityY[OldIdx];
			ShiftedPressure[NewIdx] = Pressure[OldIdx];
			ShiftedPressureSolution[NewIdx] = PressureSolution[OldIdx];
			ShiftedElevation[NewIdx] = TerrainElevation[OldIdx];
			ShiftedSlope[NewIdx] = TerrainSlope[OldIdx];
		}
	}

	PrimaryDensity = MoveTemp(ShiftedPrimary);
	SelectiveDensity = MoveTemp(ShiftedSelective);
	SurfaceDisplacement = MoveTemp(ShiftedDisplacement);
	VelocityX = MoveTemp(ShiftedVelX);
	VelocityY = MoveTemp(ShiftedVelY);
	Pressure = MoveTemp(ShiftedPressure);
	PressureSolution = MoveTemp(ShiftedPressureSolution);
	TerrainElevation = MoveTemp(ShiftedElevation);
	TerrainSlope = MoveTemp(ShiftedSlope);

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

	TerrainElevation[ToCellIndex(CellX, CellY)] = ElevationCm;
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
	const bool bHasRadialVelocity = Stamp.RadialVelocityCmPerSec != 0.0f;
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
				SurfaceDisplacement[CellIdx] = FMath::Max(SurfaceDisplacement[CellIdx], DensityTarget * BrushDisplacementRatio);
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
	EnforceObstacleBoundaries();
	ApplyVorticityConfinement(DeltaTime);
	SolvePressureProjection();
	AdvectVelocity(DeltaTime);
	EnforceObstacleBoundaries();
	SolvePressureProjection();
	EnforceObstacleBoundaries();

	const bool bAppliesRadialPush = bHasPendingRadialPush;
	if (bAppliesRadialPush)
	{
		ApplyPendingRadialPush();
		EnforceObstacleBoundaries();
	}

	AdvectDensities(DeltaTime);
	ApplyDirectionalOutflow(DeltaTime);

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

	const int32 CellCount = Resolution * Resolution;
	const float SlopeGravity = Config.TerrainSlopeGravity;
	const uint8* SolidCells = SolidCellMask.GetData();
	const FVector2D* SlopeValues = TerrainSlope.GetData();
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

		const float ActiveFluidMask = FMath::Clamp(PrimaryValues[Idx] + SelectiveValues[Idx] + 0.1f, 0.0f, 1.0f);
		VelX[Idx] -= SlopeValues[Idx].X * SlopeGravity * ActiveFluidMask * DeltaTime;
		VelY[Idx] -= SlopeValues[Idx].Y * SlopeGravity * ActiveFluidMask * DeltaTime;
	}
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

	for (int32 Y = 0; Y < GridSize; ++Y)
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
	}
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

	for (int32 Y = 1; Y < GridSize - 1; ++Y)
	{
		const int32 RowStart = Y * GridSize;
		for (int32 X = 1; X < GridSize - 1; ++X)
		{
			const int32 Idx = RowStart + X;
			const float DvYDx = 0.5f * (VelY[Idx + 1] - VelY[Idx - 1]) / SpacingX;
			const float DvXDy = 0.5f * (VelX[Idx + GridSize] - VelX[Idx - GridSize]) / SpacingY;
			CurlValues[Idx] = DvYDx - DvXDy;
		}
	}

	for (int32 Y = 2; Y < GridSize - 2; ++Y)
	{
		const int32 RowStart = Y * GridSize;
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
	}
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

	for (int32 Y = 0; Y < GridSize; ++Y)
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
	}

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

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		if (!SolidFreeRows[Y])
		{
			for (int32 X = 0; X < GridSize; ++X)
			{
				ComputeCellNearSolid(X, Y);
			}
			continue;
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
	}
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

		for (int32 Y = 0; Y < GridSize; ++Y)
		{
			if (!SolidFreeRows[Y])
			{
				for (int32 X = 0; X < GridSize; ++X)
				{
					RelaxCellNearSolid(X, Y);
				}
				continue;
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
		}
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

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		if (!SolidFreeRows[Y])
		{
			for (int32 X = 0; X < GridSize; ++X)
			{
				SubtractCellNearSolid(X, Y);
			}
			continue;
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
	}
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

	for (int32 Y = 0; Y < GridSize; ++Y)
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
	}

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
	BuildBorderFadeTable(BorderFadeByCoordinate);

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
	return FCrc::MemCrc32(TerrainSlope.GetData(), TerrainSlope.Num() * sizeof(FVector2D), Checksum);
}
