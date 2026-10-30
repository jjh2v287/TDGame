#include "FluidFX/TDFluidGrid2DSolver.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "TDFluidGpuDenseWater.h"

CSV_DECLARE_CATEGORY_EXTERN(TDFluid);

namespace
{
	constexpr int32 TDFluidDenseWaterMaxRowCells = 256;

	TAutoConsoleVariable<int32> CVarTDFluidDenseWater(
		TEXT("TD.FluidFX.DenseWater"),
		1,
		TEXT("1 follows the preset dense water switch, 0 keeps the sparse water path. Read when water buffers synchronize."),
		ECVF_Default);

	FTDFluidGpuDenseWaterSettings TDFluidDenseWaterMakeSettings(const FTDFluidSimulationConfig& Config)
	{
		FTDFluidGpuDenseWaterSettings Settings;
		Settings.GravityCmPerSec2 = Config.DenseWaterGravityCmPerSec2;
		Settings.GapFillTendency = Config.DenseWaterGapFillTendency;
		Settings.PeakAvoidance = Config.DenseWaterPeakAvoidance;
		Settings.PeakSoftnessCm = Config.DenseWaterPeakSoftnessCm;
		Settings.SdfRepelCmPerSec2 = Config.DenseWaterSdfRepelCmPerSec2;
		Settings.SdfRepelBandCm = Config.DenseWaterSdfRepelBandCm;
		Settings.LinearDragPerSec = Config.DenseWaterLinearDragPerSec;
		Settings.BedFriction = Config.DenseWaterBedFriction;
		Settings.WetThresholdCm = Config.DenseWaterWetThresholdCm;
		Settings.DryDepthCm = Config.DenseWaterDryDepthCm;
		Settings.FilmDrainCmPerSec = Config.DenseWaterFilmDrainCmPerSec;
		Settings.MaxFlowDepthCm = Config.DenseWaterMaxFlowDepthCm;
		Settings.MaxSpeedCmPerSec = Config.DenseWaterMaxSpeedCmPerSec;
		Settings.CourantNumber = Config.DenseWaterCourantNumber;
		Settings.MaxSubsteps = Config.DenseWaterMaxSubsteps;
		Settings.MomentumAdvection = Config.DenseWaterMomentumAdvection;
		Settings.ReservoirRelaxPerSec = Config.DenseWaterReservoirRelaxPerSec;
		FTDFluidGpuDenseWaterRules::Sanitize(Settings);
		return Settings;
	}

	float TDFluidDenseWaterSampleClamped(const float* Values, int32 GridSize, float GridX, float GridY)
	{
		const float MaxCoordinate = static_cast<float>(GridSize - 1);
		const float ClampedX = FMath::Clamp(GridX, 0.0f, MaxCoordinate);
		const float ClampedY = FMath::Clamp(GridY, 0.0f, MaxCoordinate);
		const int32 X0 = FMath::Min(FMath::FloorToInt32(ClampedX), GridSize - 1);
		const int32 Y0 = FMath::Min(FMath::FloorToInt32(ClampedY), GridSize - 1);
		const int32 X1 = FMath::Min(X0 + 1, GridSize - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, GridSize - 1);
		const float FracX = ClampedX - static_cast<float>(X0);
		const float FracY = ClampedY - static_cast<float>(Y0);
		const float Bottom = FMath::Lerp(Values[Y0 * GridSize + X0], Values[Y0 * GridSize + X1], FracX);
		const float Top = FMath::Lerp(Values[Y1 * GridSize + X0], Values[Y1 * GridSize + X1], FracX);
		return FMath::Lerp(Bottom, Top, FracY);
	}

	bool TDFluidDenseWaterDoesSeedMatchWindow(const FTDFluidDenseWaterSeed& Seed, const FVector2D& CenterCm, const FVector2D& ExtentCm)
	{
		const int32 SeedCellCount = Seed.Resolution * Seed.Resolution;
		const bool bHasSeedData = Seed.Resolution > 0
			&& Seed.DepthCm.Num() == SeedCellCount
			&& Seed.FaceVelocityXCmPerSec.Num() == SeedCellCount
			&& Seed.FaceVelocityYCmPerSec.Num() == SeedCellCount;
		if (!bHasSeedData || Seed.ExtentCm.X <= 0.0 || Seed.ExtentCm.Y <= 0.0)
		{
			return false;
		}

		constexpr double ExtentToleranceCm = 1.0;
		const FVector2D HalfSeedCellCm = Seed.ExtentCm / (2.0 * static_cast<double>(Seed.Resolution));
		const FVector2D CenterOffsetCm = (CenterCm - Seed.CenterCm).GetAbs();
		const FVector2D ExtentOffsetCm = (ExtentCm - Seed.ExtentCm).GetAbs();
		return CenterOffsetCm.X <= HalfSeedCellCm.X && CenterOffsetCm.Y <= HalfSeedCellCm.Y
			&& ExtentOffsetCm.X <= ExtentToleranceCm && ExtentOffsetCm.Y <= ExtentToleranceCm;
	}
}

bool FTDFluidGrid2DSolver::IsDenseWaterConsoleEnabled()
{
	return CVarTDFluidDenseWater.GetValueOnAnyThread() != 0;
}

bool FTDFluidGrid2DSolver::IsDenseWaterActive() const
{
	return bIsDenseWaterEnabledAtSync && IsWaterLayerActive() && DenseDepth.Num() == Resolution * Resolution;
}

void FTDFluidGrid2DSolver::SetDenseWaterSeed(const FTDFluidDenseWaterSeed& Seed)
{
	DenseSeed = Seed;
	if (!IsDenseWaterActive())
	{
		return;
	}

	ApplyDenseWaterSeedIfMatching();
}

void FTDFluidGrid2DSolver::ClearDenseWaterSeed()
{
	DenseSeed.Reset();
}

bool FTDFluidGrid2DSolver::ExportDenseWaterState(FTDFluidDenseWaterSeed& OutSeed) const
{
	if (!IsDenseWaterActive())
	{
		return false;
	}

	OutSeed.Resolution = Resolution;
	OutSeed.CenterCm = CenterWorld;
	OutSeed.ExtentCm = Config.SimExtentCm;
	OutSeed.DepthCm = DenseDepth;
	OutSeed.FaceVelocityXCmPerSec = DenseFaceVelocityX;
	OutSeed.FaceVelocityYCmPerSec = DenseFaceVelocityY;
	return true;
}

double FTDFluidGrid2DSolver::ComputeDenseWaterVolumeCm3() const
{
	if (!IsDenseWaterActive())
	{
		return 0.0;
	}

	double DepthSumCm = 0.0;
	for (const float DepthCm : DenseDepth)
	{
		DepthSumCm += DepthCm;
	}

	const double CellAreaCm2 = Config.SimExtentCm.X * Config.SimExtentCm.Y / static_cast<double>(Resolution * Resolution);
	return DepthSumCm * CellAreaCm2;
}

void FTDFluidGrid2DSolver::SynchronizeDenseWaterBuffers()
{
	const bool bWasEnabled = bIsDenseWaterEnabledAtSync;
	bIsDenseWaterEnabledAtSync = Config.bEnableDenseWater && IsDenseWaterConsoleEnabled() && IsWaterLayerActive();
	if (bWasEnabled != bIsDenseWaterEnabledAtSync)
	{
		bWaterRasterFullDirty = true;
		bWaterDryDirty = true;
	}

	if (!bIsDenseWaterEnabledAtSync)
	{
		DenseDepth.Empty();
		ScratchDenseDepth.Empty();
		DenseFaceVelocityX.Empty();
		DenseFaceVelocityY.Empty();
		ScratchDenseFaceVelocityX.Empty();
		ScratchDenseFaceVelocityY.Empty();
		DenseCellVelocityX.Empty();
		DenseCellVelocityY.Empty();
		DenseReservoirSurface.Empty();
		LastDenseSubstepCount = 0;
		bDidDenseReachSubstepCap = false;
		return;
	}

	const int32 CellCount = Resolution * Resolution;
	if (DenseDepth.Num() == CellCount && !bWaterVelocityInitPending)
	{
		return;
	}

	DenseDepth.Init(0.0f, CellCount);
	ScratchDenseDepth.Init(0.0f, CellCount);
	DenseFaceVelocityX.Init(0.0f, CellCount);
	DenseFaceVelocityY.Init(0.0f, CellCount);
	ScratchDenseFaceVelocityX.Init(0.0f, CellCount);
	ScratchDenseFaceVelocityY.Init(0.0f, CellCount);
	DenseCellVelocityX.Init(0.0f, CellCount);
	DenseCellVelocityY.Init(0.0f, CellCount);
	DenseReservoirSurface.Init(NoWaterSurfaceHeight, CellCount);
	bWaterRasterFullDirty = true;
	bDenseSkipsStampDelta = true;
	ApplyDenseWaterSeedIfMatching();
}

void FTDFluidGrid2DSolver::ApplyDenseWaterSeedIfMatching()
{
	if (!DenseSeed.IsSet())
	{
		return;
	}

	const FTDFluidDenseWaterSeed& Seed = DenseSeed.GetValue();
	if (!TDFluidDenseWaterDoesSeedMatchWindow(Seed, CenterWorld, Config.SimExtentCm))
	{
		return;
	}

	const int32 GridSize = Resolution;
	const int32 SeedGridSize = Seed.Resolution;
	const float SeedCellsPerCell = static_cast<float>(SeedGridSize) / static_cast<float>(GridSize);
	const float* SeedDepth = Seed.DepthCm.GetData();
	const float* SeedFaceX = Seed.FaceVelocityXCmPerSec.GetData();
	const float* SeedFaceY = Seed.FaceVelocityYCmPerSec.GetData();
	float* DepthValues = DenseDepth.GetData();
	float* FaceXValues = DenseFaceVelocityX.GetData();
	float* FaceYValues = DenseFaceVelocityY.GetData();
	float* CellXValues = DenseCellVelocityX.GetData();
	float* CellYValues = DenseCellVelocityY.GetData();

	RunRowsInParallel(TEXT("TDFluidDenseSeedRows"), GridSize, [&](int32 Y)
	{
		const float SeedCenterY = (static_cast<float>(Y) + 0.5f) * SeedCellsPerCell - 0.5f;
		const float SeedEdgeY = (static_cast<float>(Y) + 1.0f) * SeedCellsPerCell - 1.0f;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			const float SeedCenterX = (static_cast<float>(X) + 0.5f) * SeedCellsPerCell - 0.5f;
			const float SeedEdgeX = (static_cast<float>(X) + 1.0f) * SeedCellsPerCell - 1.0f;
			DepthValues[Idx] = FMath::Max(0.0f, TDFluidDenseWaterSampleClamped(SeedDepth, SeedGridSize, SeedCenterX, SeedCenterY));
			FaceXValues[Idx] = TDFluidDenseWaterSampleClamped(SeedFaceX, SeedGridSize, SeedEdgeX, SeedCenterY);
			FaceYValues[Idx] = TDFluidDenseWaterSampleClamped(SeedFaceY, SeedGridSize, SeedCenterX, SeedEdgeY);
			CellXValues[Idx] = 0.0f;
			CellYValues[Idx] = 0.0f;
		}
	});

	bDenseSkipsStampDelta = true;
}

void FTDFluidGrid2DSolver::StepDenseWater(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_DenseWater);

	const FTDFluidGpuDenseWaterSettings Settings = TDFluidDenseWaterMakeSettings(Config);
	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(Resolution);
	const FTDFluidGpuDenseWaterStepPlan Plan = FTDFluidGpuDenseWaterRules::PlanStep(Settings, DeltaTime, FMath::Min(CellSizeX, CellSizeY));
	LastDenseSubstepCount = Plan.SubstepCount;
	bDidDenseReachSubstepCap = Plan.bReachedCap;
	CSV_CUSTOM_STAT(TDFluid, WaterDenseSubsteps, Plan.SubstepCount, ECsvCustomStatOp::Max);
	CSV_CUSTOM_STAT(TDFluid, WaterDenseSubstepCapHits, Plan.bReachedCap ? 1 : 0, ECsvCustomStatOp::Accumulate);

	const bool bAppliesStampDelta = !bDenseSkipsStampDelta;
	for (int32 SubstepIndex = 0; SubstepIndex < Plan.SubstepCount; ++SubstepIndex)
	{
		UpdateDenseFaceVelocities(Plan.SubstepSeconds, Plan.GravityScale, bAppliesStampDelta && SubstepIndex == 0);
		UpdateDenseDepth(Plan.SubstepSeconds, SubstepIndex == Plan.SubstepCount - 1);
	}

	bDenseSkipsStampDelta = false;
	PublishDenseSurfaceAndVelocity();
}

void FTDFluidGrid2DSolver::UpdateDenseFaceVelocities(float SubstepSeconds, float GravityScale, bool bAppliesStampDelta)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_DenseMomentum);

	const FTDFluidGpuDenseWaterSettings Settings = TDFluidDenseWaterMakeSettings(Config);
	const int32 GridSize = Resolution;
	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(GridSize);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(GridSize);
	const float FaceSpeedCap = FMath::Min(Settings.MaxSpeedCmPerSec, Settings.CourantNumber * FMath::Min(CellSizeX, CellSizeY) / SubstepSeconds);
	const float Gravity = Settings.GravityCmPerSec2 * GravityScale;
	const float MomentumAdvection = Settings.MomentumAdvection;
	const uint8* KnownValues = TerrainKnownMask.GetData();
	const float* SdfValues = ObstacleSdf.GetData();
	const float* ElevationValues = TerrainElevation.GetData();
	const float* DepthValues = DenseDepth.GetData();
	const float* FaceXValues = DenseFaceVelocityX.GetData();
	const float* FaceYValues = DenseFaceVelocityY.GetData();
	const float* StampedX = VelocityX.GetData();
	const float* StampedY = VelocityY.GetData();
	const float* CellX = DenseCellVelocityX.GetData();
	const float* CellY = DenseCellVelocityY.GetData();
	float* NextFaceX = ScratchDenseFaceVelocityX.GetData();
	float* NextFaceY = ScratchDenseFaceVelocityY.GetData();

	const auto IsDenseSolid = [KnownValues, SdfValues](int32 Idx)
	{
		return KnownValues[Idx] == 0 || SdfValues[Idx] <= 0.0f;
	};

	const auto SolveFace = [&](int32 LowIdx, int32 HighIdx, float AdvectedVelocity, float StampDelta, float CellSizeAlong) -> float
	{
		if (IsDenseSolid(LowIdx) || IsDenseSolid(HighIdx))
		{
			return 0.0f;
		}

		const float LowDepth = DepthValues[LowIdx];
		const float HighDepth = DepthValues[HighIdx];
		if (LowDepth < Settings.DryDepthCm && HighDepth < Settings.DryDepthCm)
		{
			return 0.0f;
		}

		float Velocity = AdvectedVelocity + StampDelta;
		const float BedRise = ElevationValues[HighIdx] - ElevationValues[LowIdx];
		Velocity -= SubstepSeconds * Gravity * (BedRise + Settings.GapFillTendency * (HighDepth - LowDepth)) / CellSizeAlong;

		const bool bFlowsUp = Velocity > 0.0f;
		const int32 UpwindIdx = bFlowsUp ? LowIdx : HighIdx;
		const int32 DownwindIdx = bFlowsUp ? HighIdx : LowIdx;
		if (DepthValues[UpwindIdx] < Settings.DryDepthCm)
		{
			return 0.0f;
		}

		Velocity *= FTDFluidGpuDenseWaterRules::ComputePeakAvoidanceFactor(ElevationValues[UpwindIdx] + DepthValues[UpwindIdx], ElevationValues[DownwindIdx], Settings.PeakAvoidance, Settings.PeakSoftnessCm);

		const float FaceSdf = 0.5f * (SdfValues[LowIdx] + SdfValues[HighIdx]);
		if (FaceSdf > 0.0f && FaceSdf < Settings.SdfRepelBandCm)
		{
			const float NormalAlong = FMath::Clamp((SdfValues[HighIdx] - SdfValues[LowIdx]) / CellSizeAlong, -1.0f, 1.0f);
			Velocity += SubstepSeconds * Settings.SdfRepelCmPerSec2 * (1.0f - FaceSdf / Settings.SdfRepelBandCm) * NormalAlong;
		}

		const float FaceDepth = FMath::Max(0.5f * (LowDepth + HighDepth), 1.0f);
		Velocity /= 1.0f + SubstepSeconds * (Settings.LinearDragPerSec + Settings.BedFriction * FMath::Abs(Velocity) / FaceDepth);
		return FMath::Clamp(Velocity, -FaceSpeedCap, FaceSpeedCap);
	};

	RunRowsInParallel(TEXT("TDFluidDenseMomentumRows"), GridSize, [&](int32 Y)
	{
		const int32 RowStart = Y * GridSize;
		const int32 RowBelow = FMath::Max(Y - 1, 0) * GridSize;
		const int32 RowAbove = FMath::Min(Y + 1, GridSize - 1) * GridSize;
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = RowStart + X;
			const int32 RightX = FMath::Min(X + 1, GridSize - 1);
			const int32 LeftX = FMath::Max(X - 1, 0);

			float NextU = 0.0f;
			if (X < GridSize - 1)
			{
				const float FaceU = FaceXValues[Idx];
				float AdvectedU = FaceU;
				if (MomentumAdvection > 0.0f)
				{
					const float MeanV = 0.25f * (FaceYValues[Idx] + FaceYValues[RowStart + RightX] + FaceYValues[RowBelow + X] + FaceYValues[RowBelow + RightX]);
					const float SampleX = static_cast<float>(X) - SubstepSeconds * FaceU / CellSizeX;
					const float SampleY = static_cast<float>(Y) - SubstepSeconds * MeanV / CellSizeY;
					AdvectedU = FMath::Lerp(FaceU, TDFluidDenseWaterSampleClamped(FaceXValues, GridSize, SampleX, SampleY), MomentumAdvection);
				}

				const float StampDeltaU = bAppliesStampDelta ? 0.5f * ((StampedX[Idx] - CellX[Idx]) + (StampedX[Idx + 1] - CellX[Idx + 1])) : 0.0f;
				NextU = SolveFace(Idx, Idx + 1, AdvectedU, StampDeltaU, CellSizeX);
			}

			float NextV = 0.0f;
			if (Y < GridSize - 1)
			{
				const int32 AboveIdx = RowAbove + X;
				const float FaceV = FaceYValues[Idx];
				float AdvectedV = FaceV;
				if (MomentumAdvection > 0.0f)
				{
					const float MeanU = 0.25f * (FaceXValues[Idx] + FaceXValues[AboveIdx] + FaceXValues[RowStart + LeftX] + FaceXValues[RowAbove + LeftX]);
					const float SampleX = static_cast<float>(X) - SubstepSeconds * MeanU / CellSizeX;
					const float SampleY = static_cast<float>(Y) - SubstepSeconds * FaceV / CellSizeY;
					AdvectedV = FMath::Lerp(FaceV, TDFluidDenseWaterSampleClamped(FaceYValues, GridSize, SampleX, SampleY), MomentumAdvection);
				}

				const float StampDeltaV = bAppliesStampDelta ? 0.5f * ((StampedY[Idx] - CellY[Idx]) + (StampedY[AboveIdx] - CellY[AboveIdx])) : 0.0f;
				NextV = SolveFace(Idx, AboveIdx, AdvectedV, StampDeltaV, CellSizeY);
			}

			NextFaceX[Idx] = NextU;
			NextFaceY[Idx] = NextV;
		}
	});

	Swap(DenseFaceVelocityX, ScratchDenseFaceVelocityX);
	Swap(DenseFaceVelocityY, ScratchDenseFaceVelocityY);
}

void FTDFluidGrid2DSolver::UpdateDenseDepth(float SubstepSeconds, bool bIsLastSubstep)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_DenseDepth);

	const FTDFluidGpuDenseWaterSettings Settings = TDFluidDenseWaterMakeSettings(Config);
	const int32 GridSize = Resolution;
	const float CellSizeX = Config.SimExtentCm.X / static_cast<float>(GridSize);
	const float CellSizeY = Config.SimExtentCm.Y / static_cast<float>(GridSize);
	const bool bHasOpenBorders = Config.bOpenWaterBorders;
	const float ReservoirBlend = FTDFluidGpuDenseWaterRules::ComputeReservoirBlend(Settings.ReservoirRelaxPerSec, SubstepSeconds);
	const float FilmDrainCm = Settings.FilmDrainCmPerSec * SubstepSeconds;
	const uint8* KnownValues = TerrainKnownMask.GetData();
	const float* SdfValues = ObstacleSdf.GetData();
	const float* ElevationValues = TerrainElevation.GetData();
	const float* ReservoirValues = DenseReservoirSurface.GetData();
	const float* DepthValues = DenseDepth.GetData();
	const float* FaceXValues = DenseFaceVelocityX.GetData();
	const float* FaceYValues = DenseFaceVelocityY.GetData();
	float* NextDepth = ScratchDenseDepth.GetData();
	float* CellX = DenseCellVelocityX.GetData();
	float* CellY = DenseCellVelocityY.GetData();

	const auto IsOpenBorderCell = [&](int32 Idx)
	{
		return bHasOpenBorders && KnownValues[Idx] != 0 && SdfValues[Idx] > 0.0f;
	};

	const auto FaceU = [&](int32 X, int32 Y) -> float
	{
		if (X < 0)
		{
			return IsOpenBorderCell(Y * GridSize) ? FMath::Min(FaceXValues[Y * GridSize], 0.0f) : 0.0f;
		}
		if (X >= GridSize - 1)
		{
			const int32 EdgeIdx = Y * GridSize + GridSize - 1;
			return IsOpenBorderCell(EdgeIdx) && GridSize > 1 ? FMath::Max(FaceXValues[EdgeIdx - 1], 0.0f) : 0.0f;
		}
		return FaceXValues[Y * GridSize + X];
	};

	const auto FaceV = [&](int32 X, int32 Y) -> float
	{
		if (Y < 0)
		{
			return IsOpenBorderCell(X) ? FMath::Min(FaceYValues[X], 0.0f) : 0.0f;
		}
		if (Y >= GridSize - 1)
		{
			const int32 EdgeIdx = (GridSize - 1) * GridSize + X;
			return IsOpenBorderCell(EdgeIdx) && GridSize > 1 ? FMath::Max(FaceYValues[EdgeIdx - GridSize], 0.0f) : 0.0f;
		}
		return FaceYValues[Y * GridSize + X];
	};

	const auto ComputeOutflowScaleAt = [&](int32 X, int32 Y) -> float
	{
		const float OutgoingX = FMath::Max(FaceU(X, Y), 0.0f) + FMath::Max(-FaceU(X - 1, Y), 0.0f);
		const float OutgoingY = FMath::Max(FaceV(X, Y), 0.0f) + FMath::Max(-FaceV(X, Y - 1), 0.0f);
		return FTDFluidGpuDenseWaterRules::ComputeOutflowScale(OutgoingX / CellSizeX + OutgoingY / CellSizeY, SubstepSeconds);
	};

	RunRowsInParallel(TEXT("TDFluidDenseDepthRows"), GridSize, [&](int32 Y)
	{
		float OutflowScaleRows[3][TDFluidDenseWaterMaxRowCells];
		const int32 RowCount = FMath::Min(GridSize, TDFluidDenseWaterMaxRowCells);
		for (int32 RowOffset = -1; RowOffset <= 1; ++RowOffset)
		{
			const int32 SourceY = Y + RowOffset;
			for (int32 X = 0; X < RowCount; ++X)
			{
				OutflowScaleRows[RowOffset + 1][X] = SourceY >= 0 && SourceY < GridSize ? ComputeOutflowScaleAt(X, SourceY) : 1.0f;
			}
		}

		const auto FluxU = [&](int32 X) -> float
		{
			const float Velocity = FaceU(X, Y);
			if (Velocity == 0.0f)
			{
				return 0.0f;
			}
			const int32 UpwindX = Velocity > 0.0f ? FMath::Max(X, 0) : FMath::Min(X + 1, GridSize - 1);
			const float UpwindDepth = FMath::Min(DepthValues[Y * GridSize + UpwindX], Settings.MaxFlowDepthCm);
			return Velocity * UpwindDepth * OutflowScaleRows[1][UpwindX];
		};

		const auto FluxV = [&](int32 X, int32 FaceY) -> float
		{
			const float Velocity = FaceV(X, FaceY);
			if (Velocity == 0.0f)
			{
				return 0.0f;
			}
			const int32 UpwindY = Velocity > 0.0f ? FMath::Max(FaceY, 0) : FMath::Min(FaceY + 1, GridSize - 1);
			const float UpwindDepth = FMath::Min(DepthValues[UpwindY * GridSize + X], Settings.MaxFlowDepthCm);
			return Velocity * UpwindDepth * OutflowScaleRows[UpwindY - Y + 1][X];
		};

		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			const float NetFlux = (FluxU(X) - FluxU(X - 1)) / CellSizeX + (FluxV(X, Y) - FluxV(X, Y - 1)) / CellSizeY;
			float Depth = FMath::Max(0.0f, DepthValues[Idx] - SubstepSeconds * NetFlux);

			const float ReservoirSurface = ReservoirValues[Idx];
			if (ReservoirSurface != NoWaterSurfaceHeight && KnownValues[Idx] != 0)
			{
				const float ReservoirDepth = FMath::Max(0.0f, ReservoirSurface - ElevationValues[Idx]);
				Depth += (ReservoirDepth - Depth) * ReservoirBlend;
			}

			if (Depth < Settings.WetThresholdCm)
			{
				Depth = FMath::Max(0.0f, Depth - FilmDrainCm);
			}

			NextDepth[Idx] = Depth;
			if (!bIsLastSubstep)
			{
				continue;
			}

			const bool bIsWet = Depth > Settings.WetThresholdCm;
			CellX[Idx] = bIsWet ? 0.5f * (FaceU(X - 1, Y) + FaceU(X, Y)) : 0.0f;
			CellY[Idx] = bIsWet ? 0.5f * (FaceV(X, Y - 1) + FaceV(X, Y)) : 0.0f;
		}
	});

	Swap(DenseDepth, ScratchDenseDepth);
}

void FTDFluidGrid2DSolver::PublishDenseSurfaceAndVelocity()
{
	const int32 GridSize = Resolution;
	const float WetThresholdCm = TDFluidDenseWaterMakeSettings(Config).WetThresholdCm;
	const uint8* KnownValues = TerrainKnownMask.GetData();
	const float* ElevationValues = TerrainElevation.GetData();
	const float* DepthValues = DenseDepth.GetData();
	const float* CellX = DenseCellVelocityX.GetData();
	const float* CellY = DenseCellVelocityY.GetData();
	float* SurfaceValues = WaterSurfaceHeight.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();

	RunRowsInParallel(TEXT("TDFluidDensePublishRows"), GridSize, [&](int32 Y)
	{
		for (int32 X = 0; X < GridSize; ++X)
		{
			const int32 Idx = Y * GridSize + X;
			const bool bIsWet = KnownValues[Idx] != 0 && DepthValues[Idx] > WetThresholdCm;
			SurfaceValues[Idx] = bIsWet ? ElevationValues[Idx] + DepthValues[Idx] : NoWaterSurfaceHeight;
			VelX[Idx] = CellX[Idx];
			VelY[Idx] = CellY[Idx];
		}
	});
}

void FTDFluidGrid2DSolver::ZeroDenseDryRipples()
{
	const int32 CellCount = Resolution * Resolution;
	if (RippleHeight.Num() != CellCount || RippleHeightVelocity.Num() != CellCount)
	{
		return;
	}

	const float* SurfaceValues = WaterSurfaceHeight.GetData();
	float* Heights = RippleHeight.GetData();
	float* Speeds = RippleHeightVelocity.GetData();
	for (int32 Idx = 0; Idx < CellCount; ++Idx)
	{
		if (SurfaceValues[Idx] != NoWaterSurfaceHeight)
		{
			continue;
		}

		Heights[Idx] = 0.0f;
		Speeds[Idx] = 0.0f;
	}
}
