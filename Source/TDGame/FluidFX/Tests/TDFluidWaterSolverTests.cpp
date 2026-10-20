#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Math/Float16Color.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "TDGame.h"

namespace TDFluidWaterSolverTestPrivate
{
	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr float SquareCellSizeCm = 18.75f;
	constexpr float FlatBedElevationCm = -100.0f;
	constexpr float RiverSurfaceCm = 0.0f;
	constexpr float GuideSampleSpacingCm = 50.0f;
	constexpr float GoldenStepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 45.0f, 0.15f };
	constexpr int32 GoldenStepSecondsCycleLength = UE_ARRAY_COUNT(GoldenStepSecondsCycle);
	constexpr int32 GoldenStepCount = 120;
	const TCHAR* const RowParallelVariableName = TEXT("TD.FluidFX.SolverRowParallel");

	struct FWaterRowParallelScope
	{
		FWaterRowParallelScope()
			: Variable(IConsoleManager::Get().FindConsoleVariable(RowParallelVariableName))
		{
			if (Variable)
			{
				PreviousValue = Variable->GetInt();
			}
		}

		~FWaterRowParallelScope()
		{
			SetValue(PreviousValue);
		}

		void SetValue(int32 Value)
		{
			if (Variable)
			{
				Variable->Set(Value, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 1;
	};

	void ReportLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDGame, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	FTDFluidSimulationConfig MakeWaterTestConfig(int32 Resolution)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(Resolution * SquareCellSizeCm, Resolution * SquareCellSizeCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.35f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.3f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.ActorBrushDensityStrength = 0.6f;
		Config.MaxPointsPerTick = 512;
		Config.bEnableWaterLayer = true;
		Config.WhitewaterFromVorticity = 0.15f;
		Config.WhitewaterVorticityThresholdPerSec = 2.0f;
		return Config;
	}

	void ApplyTerrainElevation(FTDFluidGrid2DSolver& Solver, TFunctionRef<float(const FVector2D&)> Elevation)
	{
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, Elevation(Solver.CellToWorldCenter(CellX, CellY)));
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
	}

	FTDFluidFlowGuidePoint MakeGuidePoint(double X, double Y, float HalfWidthCm, float SpeedCmPerSec)
	{
		FTDFluidFlowGuidePoint Point;
		Point.WorldLocation = FVector(X, Y, RiverSurfaceCm);
		Point.HalfWidthCm = HalfWidthCm;
		Point.FlowSpeedCmPerSec = SpeedCmPerSec;
		return Point;
	}

	FTDFluidFlowGuide MakeStraightGuide(const FVector2D& Start, const FVector2D& End, float HalfWidthCm, float SpeedCmPerSec)
	{
		FTDFluidFlowGuide Guide;
		const double Length = (End - Start).Size();
		const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(Length / GuideSampleSpacingCm));
		for (int32 PointIndex = 0; PointIndex <= SegmentCount; ++PointIndex)
		{
			const double Alpha = static_cast<double>(PointIndex) / SegmentCount;
			const FVector2D Position = Start + (End - Start) * Alpha;
			Guide.Points.Add(MakeGuidePoint(Position.X, Position.Y, HalfWidthCm, SpeedCmPerSec));
		}
		return Guide;
	}

	FTDFluidFlowGuide MakeSinuousGuide(float HalfWidthCm, float SpeedCmPerSec)
	{
		FTDFluidFlowGuide Guide;
		for (double X = -1400.0; X <= 1400.0; X += 60.0)
		{
			Guide.Points.Add(MakeGuidePoint(X, 150.0 * FMath::Sin(X / 450.0), HalfWidthCm, SpeedCmPerSec));
		}
		return Guide;
	}

	struct FGuideKnot
	{
		float X;
		float HalfWidthCm;
		float SpeedCmPerSec;
	};

	FTDFluidFlowGuide MakeProfiledGuide(const TArray<FGuideKnot>& Knots)
	{
		FTDFluidFlowGuide Guide;
		const float FirstX = Knots[0].X;
		const float LastX = Knots.Last().X;
		for (float X = FirstX; X <= LastX + 0.01f; X += GuideSampleSpacingCm)
		{
			int32 KnotIndex = 0;
			while (KnotIndex + 2 < Knots.Num() && X > Knots[KnotIndex + 1].X)
			{
				++KnotIndex;
			}
			const FGuideKnot& Start = Knots[KnotIndex];
			const FGuideKnot& End = Knots[KnotIndex + 1];
			const float Alpha = FMath::Clamp((X - Start.X) / (End.X - Start.X), 0.0f, 1.0f);
			Guide.Points.Add(MakeGuidePoint(X, 0.0, FMath::Lerp(Start.HalfWidthCm, End.HalfWidthCm, Alpha), FMath::Lerp(Start.SpeedCmPerSec, End.SpeedCmPerSec, Alpha)));
		}
		return Guide;
	}

	void StartRiver(FTDFluidGrid2DSolver& Solver, const FTDFluidSimulationConfig& Config, const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidSdfObstacle>& Obstacles)
	{
		Solver.Initialize(Config, FVector2D::ZeroVector);
		ApplyTerrainElevation(Solver, [](const FVector2D&) { return FlatBedElevationCm; });
		Solver.SetFlowGuides(Guides);
		if (!Obstacles.IsEmpty())
		{
			Solver.RasterizeSdfObstacles(Obstacles);
		}
	}

	void RunSteps(FTDFluidGrid2DSolver& Solver, int32 StepCount)
	{
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			Solver.StepSimulation(StepSeconds);
		}
	}

	struct FSectionFlux
	{
		double NetFluxCm2PerSec = 0.0;
		double ReverseFluxCm2PerSec = 0.0;
		double TargetFluxCm2PerSec = 0.0;
	};

	FSectionFlux MeasureSectionFlux(const FTDFluidGrid2DSolver& Solver, int32 CellX)
	{
		const int32 Resolution = Solver.GetResolution();
		const double CellSizeY = Solver.GetExtentCm().Y / Resolution;
		FSectionFlux Flux;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
			Flux.TargetFluxCm2PerSec += Sample.FlowTargetVelocity.X * Sample.FlowGuideWeight * CellSizeY;
			if (Sample.ObstacleSdf <= 0.0f)
			{
				continue;
			}
			Flux.NetFluxCm2PerSec += Sample.Velocity.X * CellSizeY;
			Flux.ReverseFluxCm2PerSec += FMath::Max(0.0, -Sample.Velocity.X) * CellSizeY;
		}
		return Flux;
	}

	FTDFluidCellSample SampleCellAtWorld(const FTDFluidGrid2DSolver& Solver, const FVector2D& WorldPosition)
	{
		FVector2D GridUV;
		Solver.WorldToGridUV(WorldPosition, GridUV);
		const int32 Resolution = Solver.GetResolution();
		const int32 CellX = FMath::Clamp(FMath::FloorToInt(GridUV.X * Resolution), 0, Resolution - 1);
		const int32 CellY = FMath::Clamp(FMath::FloorToInt(GridUV.Y * Resolution), 0, Resolution - 1);
		return Solver.SampleAtCell(CellX, CellY);
	}

	double MeasureAverageSpeed(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		double SpeedSum = 0.0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				SpeedSum += Solver.SampleAtCell(CellX, CellY).Velocity.Size();
			}
		}
		return SpeedSum / (Resolution * Resolution);
	}

	TArray<FTDFluidSdfObstacle> MakeScenarioObstacles()
	{
		TArray<FTDFluidSdfObstacle> Obstacles;
		FTDFluidSdfObstacle Pillar;
		Pillar.WorldCenter = FVector(200.0, 0.0, 0.0);
		Pillar.RadiusCm = 70.0f;
		Obstacles.Add(Pillar);
		return Obstacles;
	}

	float BankedBedElevationCm(const FVector2D& WorldPosition)
	{
		return static_cast<float>(-150.0 + 0.0006 * WorldPosition.Y * WorldPosition.Y);
	}

	TArray<FTDFluidBrushStamp> MakeWaterScenarioStamps(const FVector2D& OrbitCenter, int32 Step)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StampIndex = 0; StampIndex < 2; ++StampIndex)
		{
			const double Angle = Step * 0.09 + StampIndex * UE_DOUBLE_PI;
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(OrbitCenter.X + FMath::Cos(Angle) * 300.0, OrbitCenter.Y + FMath::Sin(Angle) * 90.0, 0.0);
			Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 400.0;
			Stamp.RadiusCm = 70.0f;
			Stamp.DensityStrength = 0.6f;
			Stamp.VelocityStrength = 1.0f;
			Stamp.RippleImpulseCm = 2.5f;
			Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			Stamps.Add(Stamp);
		}
		return Stamps;
	}

	FTDFluidSimulationConfig MakeScenarioConfig()
	{
		FTDFluidSimulationConfig Config = MakeWaterTestConfig(96);
		Config.RippleWaveSpeedCmPerSec = 120.0f;
		Config.RippleDampingPerSec = 1.2f;
		Config.RippleFlowAdvection = 1.0f;
		Config.RapidsWhitewaterPerSec = 40.0f;
		Config.WhitewaterFromVorticity = 0.3f;
		return Config;
	}

	void StartScenario(FTDFluidGrid2DSolver& Solver, const FTDFluidSimulationConfig& Config)
	{
		Solver.Initialize(Config, FVector2D::ZeroVector);
		ApplyTerrainElevation(Solver, BankedBedElevationCm);
		TArray<FTDFluidFlowGuide> Guides;
		Guides.Add(MakeSinuousGuide(600.0f, 220.0f));
		Solver.SetFlowGuides(Guides);
		FTDFluidWaterRegion Region;
		Region.MinXY = FVector2D(-900.0, 300.0);
		Region.MaxXY = FVector2D(-300.0, 600.0);
		Region.SurfaceHeightCm = RiverSurfaceCm;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(Region);
		Solver.SetWaterRegions(Regions);
		Solver.RasterizeSdfObstacles(MakeScenarioObstacles());
	}

	void AdvanceScenario(FTDFluidGrid2DSolver& Solver, int32 FirstStep, int32 StepCount, bool bShiftsWindow)
	{
		const TArray<FTDFluidSdfObstacle> Obstacles = MakeScenarioObstacles();
		for (int32 Step = FirstStep; Step < FirstStep + StepCount; ++Step)
		{
			const FVector2D DesiredCenter = bShiftsWindow ? FVector2D(Step * 3.9, Step * -1.3) : FVector2D::ZeroVector;
			if (bShiftsWindow && Solver.ShiftOriginForSlidingWindow(DesiredCenter))
			{
				ApplyTerrainElevation(Solver, BankedBedElevationCm);
				Solver.RasterizeSdfObstacles(Obstacles);
			}
			Solver.ApplyBrushStamps(MakeWaterScenarioStamps(Solver.GetCenterWorld(), Step), StepSeconds);
			Solver.StepSimulation(StepSeconds);
		}
	}

	struct FWaterSnapshot
	{
		uint32 StateChecksum = 0;
		uint32 WaterChecksum = 0;
		double MaximumSpeed = 0.0;
		double MaximumRipple = 0.0;
		double MaximumDensity = 0.0;
		int32 SolidCellCount = 0;
		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		TArray<FFloat16Color> WaterSurfacePixels;
	};

	FWaterSnapshot CaptureWaterSnapshot(const FTDFluidGrid2DSolver& Solver)
	{
		FWaterSnapshot Snapshot;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
				Snapshot.MaximumSpeed = FMath::Max(Snapshot.MaximumSpeed, Sample.Velocity.Size());
				Snapshot.MaximumRipple = FMath::Max(Snapshot.MaximumRipple, static_cast<double>(FMath::Abs(Sample.RippleHeightCm)));
				Snapshot.MaximumDensity = FMath::Max(Snapshot.MaximumDensity, static_cast<double>(Sample.PrimaryDensity));
				Snapshot.SolidCellCount += Sample.ObstacleSdf <= 0.0f ? 1 : 0;
			}
		}
		Solver.PackOutputPixels(Snapshot.DensityPressurePixels, Snapshot.VelocityPixels);
		Solver.PackWaterSurfacePixels(Snapshot.WaterSurfacePixels, 0.0f);
		Snapshot.StateChecksum = Solver.ComputeStateChecksum();
		Snapshot.WaterChecksum = Solver.ComputeWaterStateChecksum();
		return Snapshot;
	}

	bool ArePixelArraysBitIdentical(const TArray<FFloat16Color>& Expected, const TArray<FFloat16Color>& Actual)
	{
		return Expected.Num() == Actual.Num() && FMemory::Memcmp(Expected.GetData(), Actual.GetData(), Expected.Num() * sizeof(FFloat16Color)) == 0;
	}

	void ExpectIdenticalSnapshots(FAutomationTestBase& Test, const FString& Label, const FWaterSnapshot& Expected, const FWaterSnapshot& Actual)
	{
		Test.TestEqual(*(Label + TEXT(" state checksum")), Actual.StateChecksum, Expected.StateChecksum);
		Test.TestEqual(*(Label + TEXT(" water checksum")), Actual.WaterChecksum, Expected.WaterChecksum);
		Test.TestTrue(*(Label + TEXT(" density and pressure pixels are bit-identical")), ArePixelArraysBitIdentical(Expected.DensityPressurePixels, Actual.DensityPressurePixels));
		Test.TestTrue(*(Label + TEXT(" velocity pixels are bit-identical")), ArePixelArraysBitIdentical(Expected.VelocityPixels, Actual.VelocityPixels));
		Test.TestTrue(*(Label + TEXT(" water surface pixels are bit-identical")), ArePixelArraysBitIdentical(Expected.WaterSurfacePixels, Actual.WaterSurfacePixels));
	}

	FWaterSnapshot RunWaterScenario(int32 StepCount, bool bShiftsWindow)
	{
		FTDFluidGrid2DSolver Solver;
		StartScenario(Solver, MakeScenarioConfig());
		AdvanceScenario(Solver, 0, StepCount, bShiftsWindow);
		return CaptureWaterSnapshot(Solver);
	}

	TArray<FTDFluidSdfObstacle> MakeGoldenObstacles()
	{
		TArray<FTDFluidSdfObstacle> Obstacles;

		FTDFluidSdfObstacle Pillar;
		Pillar.WorldCenter = FVector(150.0, -100.0, 0.0);
		Pillar.RadiusCm = 90.0f;
		Obstacles.Add(Pillar);

		FTDFluidSdfObstacle Crate;
		Crate.WorldCenter = FVector(-250.0, 120.0, 0.0);
		Crate.BoxHalfExtentsCm = FVector2D(80.0, 50.0);
		Crate.bIsBox = true;
		Obstacles.Add(Crate);

		FTDFluidSdfObstacle EdgeWall;
		EdgeWall.WorldCenter = FVector(470.0, 0.0, 0.0);
		EdgeWall.BoxHalfExtentsCm = FVector2D(60.0, 250.0);
		EdgeWall.bIsBox = true;
		Obstacles.Add(EdgeWall);

		FTDFluidSdfObstacle TopLedge;
		TopLedge.WorldCenter = FVector(0.0, 450.0, 0.0);
		TopLedge.BoxHalfExtentsCm = FVector2D(200.0, 80.0);
		TopLedge.bIsBox = true;
		Obstacles.Add(TopLedge);

		return Obstacles;
	}

	TArray<FTDFluidBrushStamp> MakeGoldenStamps(const FVector2D& OrbitCenter, float OrbitRadiusCm, int32 Step)
	{
		constexpr ETDFluidBrushChannel StampChannels[] = { ETDFluidBrushChannel::ChannelA_Water, ETDFluidBrushChannel::ChannelG_Blood, ETDFluidBrushChannel::ChannelB_Fire };
		constexpr float StampRadiiCm[] = { 60.0f, 90.0f, 120.0f };
		constexpr int32 OrbitingStampCount = UE_ARRAY_COUNT(StampChannels);

		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StampIndex = 0; StampIndex < OrbitingStampCount; ++StampIndex)
		{
			const double Angle = Step * 0.11 + StampIndex * (UE_DOUBLE_TWO_PI / 3.0);
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(OrbitCenter.X + FMath::Cos(Angle) * OrbitRadiusCm, OrbitCenter.Y + FMath::Sin(Angle) * OrbitRadiusCm, 0.0);
			Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 900.0;
			Stamp.RadiusCm = StampRadiiCm[StampIndex];
			Stamp.DensityStrength = 0.6f + 0.2f * StampIndex;
			Stamp.VelocityStrength = 1.0f;
			Stamp.Channel = StampChannels[StampIndex];
			Stamps.Add(Stamp);
		}

		FTDFluidBrushStamp FastStamp;
		FastStamp.WorldLocation = FVector(OrbitCenter.X - OrbitRadiusCm * 0.5, OrbitCenter.Y + OrbitRadiusCm * 0.25, 0.0);
		FastStamp.WorldVelocity = FVector(1500.0, -700.0, 0.0);
		FastStamp.RadiusCm = 70.0f;
		FastStamp.DensityStrength = 1.0f;
		FastStamp.VelocityStrength = 1.5f;
		FastStamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		Stamps.Add(FastStamp);

		return Stamps;
	}

	struct FGoldenChecksums
	{
		uint32 StateChecksum = 0;
		uint32 PixelChecksum = 0;
		bool bHasWaterSurface = false;
		bool bIsTerrainComplete = false;
	};

	FGoldenChecksums RunGoldenScenarioWithWaterFieldsChanged()
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Water);
		Config.GridResolution = 96;
		Config.SimExtentCm = FVector2D(1000.0, 800.0);
		Config.bEnableWaterLayer = false;
		Config.bOpenWaterBorders = false;
		Config.bTreatDryCellsAsSolid = false;
		Config.WaterDepthReferenceCm = 55.0f;
		Config.FlowGuideResponsePerSec = 9.0f;
		Config.RapidsWhitewaterPerSec = 30.0f;
		Config.WhitewaterFromVorticity = 2.0f;
		Config.RippleWaveSpeedCmPerSec = 300.0f;
		Config.RippleFlowAdvection = 2.0f;
		Config.ActorBrushRippleImpulseCm = 4.0f;
		Config.SwashHeightCm = 30.0f;
		Config.BankWetHeightCm = 12.0f;

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		const TArray<FTDFluidSdfObstacle> Obstacles = MakeGoldenObstacles();
		Solver.RasterizeSdfObstacles(Obstacles);
		Solver.SetUniformTerrainSlope(FVector2D(0.12, -0.07));

		const float OrbitRadiusCm = static_cast<float>(FMath::Min(Config.SimExtentCm.X, Config.SimExtentCm.Y) * 0.3);
		for (int32 Step = 0; Step < GoldenStepCount; ++Step)
		{
			const float DeltaTime = GoldenStepSecondsCycle[Step % GoldenStepSecondsCycleLength];
			const FVector2D DesiredCenter(Step * 4.3, Step * -2.7);
			if (Solver.ShiftOriginForSlidingWindow(DesiredCenter))
			{
				Solver.RasterizeSdfObstacles(Obstacles);
			}
			Solver.ApplyBrushStamps(MakeGoldenStamps(DesiredCenter, OrbitRadiusCm, Step), DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}

		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);

		FGoldenChecksums Checksums;
		Checksums.StateChecksum = Solver.ComputeStateChecksum();
		Checksums.PixelChecksum = FCrc::MemCrc32(DensityPressurePixels.GetData(), DensityPressurePixels.Num() * sizeof(FFloat16Color));
		Checksums.PixelChecksum = FCrc::MemCrc32(VelocityPixels.GetData(), VelocityPixels.Num() * sizeof(FFloat16Color), Checksums.PixelChecksum);
		Checksums.bHasWaterSurface = Solver.GetWaterSurfaceHeightAtCell(10, 10) != FTDFluidGrid2DSolver::NoWaterSurfaceHeight;
		Checksums.bIsTerrainComplete = Solver.IsTerrainSamplingCompleteForWater();
		return Checksums;
	}

	struct FRingMeasurement
	{
		double RadiusCells = 0.0;
		double PeakAmplitudeCm = 0.0;
	};

	FRingMeasurement MeasureRingRadius(const FTDFluidGrid2DSolver& Solver, int32 CenterCellX, int32 CenterCellY)
	{
		const int32 Resolution = Solver.GetResolution();
		TArray<double> BinSum;
		TArray<int32> BinSampleCount;
		BinSum.Init(0.0, Resolution);
		BinSampleCount.Init(0, Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const double Distance = FMath::Sqrt(static_cast<double>(FMath::Square(CellX - CenterCellX) + FMath::Square(CellY - CenterCellY)));
				const int32 Bin = FMath::RoundToInt(Distance);
				if (Bin >= Resolution)
				{
					continue;
				}
				BinSum[Bin] += Solver.SampleAtCell(CellX, CellY).RippleHeightCm;
				++BinSampleCount[Bin];
			}
		}

		TArray<double> BinMean;
		BinMean.Init(0.0, Resolution);
		double LargestMagnitude = 0.0;
		for (int32 Bin = 1; Bin < Resolution; ++Bin)
		{
			BinMean[Bin] = BinSampleCount[Bin] > 0 ? BinSum[Bin] / BinSampleCount[Bin] : 0.0;
			LargestMagnitude = FMath::Max(LargestMagnitude, FMath::Abs(BinMean[Bin]));
		}

		int32 OuterBin = INDEX_NONE;
		for (int32 Bin = Resolution - 1; Bin >= 1; --Bin)
		{
			if (FMath::Abs(BinMean[Bin]) >= 0.1 * LargestMagnitude)
			{
				OuterBin = Bin;
				break;
			}
		}

		FRingMeasurement Measurement;
		if (OuterBin == INDEX_NONE || LargestMagnitude <= 0.0)
		{
			return Measurement;
		}

		const bool bLeadingLobeIsPositive = BinMean[OuterBin] > 0.0;
		double WeightSum = 0.0;
		double RadiusSum = 0.0;
		for (int32 Bin = OuterBin; Bin >= 1 && BinMean[Bin] != 0.0 && (BinMean[Bin] > 0.0) == bLeadingLobeIsPositive; --Bin)
		{
			const double Magnitude = FMath::Abs(BinMean[Bin]);
			WeightSum += Magnitude;
			RadiusSum += Magnitude * Bin;
			Measurement.PeakAmplitudeCm = FMath::Max(Measurement.PeakAmplitudeCm, Magnitude);
		}
		Measurement.RadiusCells = WeightSum > 0.0 ? RadiusSum / WeightSum : 0.0;
		return Measurement;
	}

	double MeasureRippleEnergy(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		double Energy = 0.0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Energy += FMath::Square(static_cast<double>(Solver.SampleAtCell(CellX, CellY).RippleHeightCm));
			}
		}
		return Energy;
	}

	struct FStepCostResult
	{
		double AverageMilliseconds = 0.0;
		double MaximumMilliseconds = 0.0;
		double AveragePackMilliseconds = 0.0;
	};

	FStepCostResult MeasureWaterStepCost(int32 Resolution, bool bShiftsWindow)
	{
		constexpr int32 WarmupStepCount = 20;
		constexpr int32 MeasuredStepCount = 120;
		constexpr int32 StepsBetweenShifts = 3;

		FTDFluidSimulationConfig Config = MakeWaterTestConfig(Resolution);
		Config.RippleWaveSpeedCmPerSec = 120.0f;
		Config.RippleDampingPerSec = 1.2f;
		Config.RippleFlowAdvection = 1.0f;
		Config.RapidsWhitewaterPerSec = 40.0f;
		Config.WhitewaterFromVorticity = 0.15f;
		Config.PressureIterations = 12;

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		ApplyTerrainElevation(Solver, BankedBedElevationCm);
		TArray<FTDFluidFlowGuide> Guides;
		Guides.Add(MakeSinuousGuide(600.0f, 220.0f));
		Solver.SetFlowGuides(Guides);
		Solver.RasterizeSdfObstacles(MakeScenarioObstacles());

		FStepCostResult Result;
		double TotalSeconds = 0.0;
		double TotalPackMilliseconds = 0.0;
		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		TArray<FFloat16Color> WaterSurfacePixels;
		for (int32 Step = 0; Step < WarmupStepCount + MeasuredStepCount; ++Step)
		{
			if (bShiftsWindow && Step % StepsBetweenShifts == 0)
			{
				const FVector2D DesiredCenter(Step * 6.0, 0.0);
				if (Solver.ShiftOriginForSlidingWindow(DesiredCenter))
				{
					ApplyTerrainElevation(Solver, BankedBedElevationCm);
					Solver.RasterizeSdfObstacles(MakeScenarioObstacles());
				}
			}
			Solver.ApplyBrushStamps(MakeWaterScenarioStamps(Solver.GetCenterWorld(), Step), StepSeconds);
			const double StartSeconds = FPlatformTime::Seconds();
			Solver.StepSimulation(StepSeconds);
			const double ElapsedMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			const double PackStartSeconds = FPlatformTime::Seconds();
			Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);
			Solver.PackWaterSurfacePixels(WaterSurfacePixels, 0.0f);
			const double PackMilliseconds = (FPlatformTime::Seconds() - PackStartSeconds) * 1000.0;
			if (Step >= WarmupStepCount)
			{
				TotalSeconds += ElapsedMilliseconds;
				TotalPackMilliseconds += PackMilliseconds;
				Result.MaximumMilliseconds = FMath::Max(Result.MaximumMilliseconds, ElapsedMilliseconds);
			}
		}
		Result.AverageMilliseconds = TotalSeconds / MeasuredStepCount;
		Result.AveragePackMilliseconds = TotalPackMilliseconds / MeasuredStepCount;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLayerDisabledKeepsGoldenHashTest,
	"TDGame.FluidFX.Water.WaterLayer.DisabledKeepsGoldenHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLayerDisabledKeepsGoldenHashTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	constexpr uint32 ExpectedStateChecksum = 0x24BE4A74u;
	constexpr uint32 ExpectedPixelChecksum = 0xA9C79CD5u;

	const FGoldenChecksums Checksums = RunGoldenScenarioWithWaterFieldsChanged();
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterGolden: state=0x%08X pixels=0x%08X"), Checksums.StateChecksum, Checksums.PixelChecksum));

	TestEqual(TEXT("Disabled water layer with changed water fields keeps the golden state checksum"), Checksums.StateChecksum, ExpectedStateChecksum);
	TestEqual(TEXT("Disabled water layer with changed water fields keeps the golden pixel checksum"), Checksums.PixelChecksum, ExpectedPixelChecksum);
	TestFalse(TEXT("Disabled water layer reports no water surface"), Checksums.bHasWaterSurface);
	TestFalse(TEXT("Disabled water layer never reports terrain sampling as complete"), Checksums.bIsTerrainComplete);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLayerParallelEqualsSerialTest,
	"TDGame.FluidFX.Water.WaterLayer.ParallelEqualsSerial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLayerParallelEqualsSerialTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FWaterRowParallelScope RowParallelVariable;
	if (!TestNotNull(TEXT("TD.FluidFX.SolverRowParallel console variable exists"), RowParallelVariable.Variable))
	{
		return false;
	}

	for (const bool bShiftsWindow : { false, true })
	{
		const FString Label = bShiftsWindow ? TEXT("sliding window") : TEXT("fixed window");
		RowParallelVariable.SetValue(0);
		const FWaterSnapshot SerialSnapshot = RunWaterScenario(110, bShiftsWindow);
		RowParallelVariable.SetValue(1);
		const FWaterSnapshot ParallelSnapshot = RunWaterScenario(110, bShiftsWindow);
		const FWaterSnapshot RepeatedParallelSnapshot = RunWaterScenario(110, bShiftsWindow);

		ReportLine(*this, FString::Printf(TEXT("TDFluidWaterParallel: %s state=0x%08X water=0x%08X maxSpeed=%.2f maxRipple=%.3f maxDensity=%.3f solid=%d"), *Label, SerialSnapshot.StateChecksum, SerialSnapshot.WaterChecksum, SerialSnapshot.MaximumSpeed, SerialSnapshot.MaximumRipple, SerialSnapshot.MaximumDensity, SerialSnapshot.SolidCellCount));
		TestTrue(*(Label + TEXT(" carries flow")), SerialSnapshot.MaximumSpeed > 10.0);
		TestTrue(*(Label + TEXT(" carries ripples")), SerialSnapshot.MaximumRipple > 0.01);
		TestTrue(*(Label + TEXT(" carries foam density")), SerialSnapshot.MaximumDensity > 0.05);
		TestTrue(*(Label + TEXT(" has solid dry or rock cells")), SerialSnapshot.SolidCellCount > 0);
		ExpectIdenticalSnapshots(*this, Label + TEXT(" parallel equals serial:"), SerialSnapshot, ParallelSnapshot);
		ExpectIdenticalSnapshots(*this, Label + TEXT(" repeated parallel equals serial:"), SerialSnapshot, RepeatedParallelSnapshot);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLayerDeterministicChecksumTest,
	"TDGame.FluidFX.Water.WaterLayer.DeterministicChecksum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLayerDeterministicChecksumTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	const FWaterSnapshot First = RunWaterScenario(80, true);
	const FWaterSnapshot Second = RunWaterScenario(80, true);
	const FWaterSnapshot ShorterRun = RunWaterScenario(79, true);

	TestTrue(TEXT("Scenario carries flow"), First.MaximumSpeed > 10.0);
	ExpectIdenticalSnapshots(*this, TEXT("Repeated run:"), First, Second);
	TestNotEqual(TEXT("One more step changes the water checksum"), ShorterRun.WaterChecksum, First.WaterChecksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLayerShiftKeepsWaterBuffersAlignedTest,
	"TDGame.FluidFX.Water.WaterLayer.ShiftKeepsWaterBuffersAligned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLayerShiftKeepsWaterBuffersAlignedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	struct FShiftCase
	{
		int32 ShiftCellsX;
		int32 ShiftCellsY;
	};
	const FShiftCase ShiftCases[] = { { 3, -2 }, { -5, 4 }, { 0, 6 }, { 200, 0 } };

	FTDFluidGrid2DSolver Solver;
	StartScenario(Solver, MakeScenarioConfig());
	AdvanceScenario(Solver, 0, 40, false);
	const int32 Resolution = Solver.GetResolution();

	for (const FShiftCase& ShiftCase : ShiftCases)
	{
		const FString Label = FString::Printf(TEXT("Shift (%d, %d):"), ShiftCase.ShiftCellsX, ShiftCase.ShiftCellsY);
		TArray<FTDFluidCellSample> Before;
		TArray<float> SurfaceBefore;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Before.Add(Solver.SampleAtCell(CellX, CellY));
				SurfaceBefore.Add(Solver.GetWaterSurfaceHeightAtCell(CellX, CellY));
			}
		}

		const FVector2D DesiredCenter = Solver.GetCenterWorld() + FVector2D(ShiftCase.ShiftCellsX * SquareCellSizeCm, ShiftCase.ShiftCellsY * SquareCellSizeCm);
		TestTrue(*(Label + TEXT(" the window moves")), Solver.ShiftOriginForSlidingWindow(DesiredCenter));

		int32 MismatchCount = 0;
		int32 NewCellErrorCount = 0;
		int32 ComparedRippleCells = 0;
		for (int32 NewY = 0; NewY < Resolution; ++NewY)
		{
			for (int32 NewX = 0; NewX < Resolution; ++NewX)
			{
				const FTDFluidCellSample After = Solver.SampleAtCell(NewX, NewY);
				const float SurfaceAfter = Solver.GetWaterSurfaceHeightAtCell(NewX, NewY);
				const int32 OldX = NewX + ShiftCase.ShiftCellsX;
				const int32 OldY = NewY + ShiftCase.ShiftCellsY;
				if (OldX < 0 || OldX >= Resolution || OldY < 0 || OldY >= Resolution)
				{
					NewCellErrorCount += (After.bIsTerrainKnown || SurfaceAfter != FTDFluidGrid2DSolver::NoWaterSurfaceHeight || After.RippleHeightCm != 0.0f || After.Wetness != 0.0f) ? 1 : 0;
					continue;
				}

				const FTDFluidCellSample& Old = Before[OldY * Resolution + OldX];
				const bool bMatches = After.bIsTerrainKnown == Old.bIsTerrainKnown
					&& After.RippleHeightCm == Old.RippleHeightCm
					&& After.Wetness == Old.Wetness
					&& After.FlowGuideWeight == Old.FlowGuideWeight
					&& After.FlowTargetVelocity == Old.FlowTargetVelocity
					&& After.WaterDepthCm == Old.WaterDepthCm
					&& After.PrimaryDensity == Old.PrimaryDensity
					&& SurfaceAfter == SurfaceBefore[OldY * Resolution + OldX];
				MismatchCount += bMatches ? 0 : 1;
				ComparedRippleCells += Old.RippleHeightCm != 0.0f ? 1 : 0;
			}
		}

		TestEqual(*(Label + TEXT(" cells whose water buffers differ from the copy shift")), MismatchCount, 0);
		TestEqual(*(Label + TEXT(" new cells that are not unknown and empty")), NewCellErrorCount, 0);
		if (FMath::Abs(ShiftCase.ShiftCellsX) < Resolution)
		{
			TestTrue(*(Label + TEXT(" compared cells carry ripples")), ComparedRippleCells > 0);
		}
		TestFalse(*(Label + TEXT(" terrain sampling is incomplete after a shift")), Solver.IsTerrainSamplingCompleteForWater());

		ApplyTerrainElevation(Solver, BankedBedElevationCm);
		TestTrue(*(Label + TEXT(" terrain sampling is complete after every cell is traced")), Solver.IsTerrainSamplingCompleteForWater());
		Solver.RasterizeSdfObstacles(MakeScenarioObstacles());
		AdvanceScenario(Solver, 40, 4, false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLayerFlatSurfaceOverSlopeStaysStillTest,
	"TDGame.FluidFX.Water.WaterLayer.FlatSurfaceOverSlopeStaysStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLayerFlatSurfaceOverSlopeStaysStillTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FTDFluidSimulationConfig Config = MakeWaterTestConfig(96);
	Config.TerrainSlopeGravity = 400.0f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	ApplyTerrainElevation(Solver, [](const FVector2D& WorldPosition) { return static_cast<float>(-250.0 + 0.1 * WorldPosition.X); });

	FTDFluidWaterRegion Region;
	Region.MinXY = FVector2D(-5000.0, -5000.0);
	Region.MaxXY = FVector2D(5000.0, 5000.0);
	Region.SurfaceHeightCm = RiverSurfaceCm;
	TArray<FTDFluidWaterRegion> Regions;
	Regions.Add(Region);
	Solver.SetWaterRegions(Regions);

	RunSteps(Solver, 300);
	const double AverageSpeed = MeasureAverageSpeed(Solver);
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterFlatSurface: average_speed_cm_per_sec=%.5f"), AverageSpeed));

	const FTDFluidCellSample CenterSample = Solver.SampleAtCell(48, 48);
	TestTrue(TEXT("The center cell is water over the sloped bed"), CenterSample.WaterDepthCm > 100.0f && CenterSample.ObstacleSdf > 0.0f);
	TestTrue(TEXT("A flat water surface over a sloped bed stays still (average speed under 1 cm/s)"), AverageSpeed < 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLayerUpdateConfigTogglesBuffersTest,
	"TDGame.FluidFX.Water.WaterLayer.UpdateConfigTogglesBuffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLayerUpdateConfigTogglesBuffersTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FTDFluidSimulationConfig Config = MakeWaterTestConfig(48);
	Config.bEnableWaterLayer = false;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	RunSteps(Solver, 5);
	TestEqual(TEXT("Disabled layer has no surface"), Solver.GetWaterSurfaceHeightAtCell(5, 5), FTDFluidGrid2DSolver::NoWaterSurfaceHeight);

	Config.bEnableWaterLayer = true;
	Solver.UpdateConfig(Config);
	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeStraightGuide(FVector2D(-800.0, 0.0), FVector2D(800.0, 0.0), 200.0f, 150.0f));
	Solver.SetFlowGuides(Guides);
	RunSteps(Solver, 5);
	TestEqual(TEXT("Enabling the layer through UpdateConfig rasterizes the guide surface"), Solver.GetWaterSurfaceHeightAtCell(24, 24), RiverSurfaceCm);

	Config.bEnableWaterLayer = false;
	Solver.UpdateConfig(Config);
	RunSteps(Solver, 5);
	TestEqual(TEXT("Disabling the layer releases the surface"), Solver.GetWaterSurfaceHeightAtCell(24, 24), FTDFluidGrid2DSolver::NoWaterSurfaceHeight);
	TestFalse(TEXT("Disabled layer never reports terrain sampling as complete"), Solver.IsTerrainSamplingCompleteForWater());

	Config.bEnableWaterLayer = true;
	Config.SimMode = ETDFluidSimMode::SimplePainter;
	Solver.UpdateConfig(Config);
	RunSteps(Solver, 5);
	TestEqual(TEXT("The simple painter mode never runs the water layer"), Solver.GetWaterSurfaceHeightAtCell(24, 24), FTDFluidGrid2DSolver::NoWaterSurfaceHeight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLayerStaysFiniteWithIrregularStepsTest,
	"TDGame.FluidFX.Water.WaterLayer.StaysFiniteWithIrregularSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLayerStaysFiniteWithIrregularStepsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	constexpr float StepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 0.1f, 0.15f, 1.0f / 90.0f };
	constexpr int32 StepSecondsCycleLength = UE_ARRAY_COUNT(StepSecondsCycle);
	constexpr float MaximumExpectedSpeedCmPerSec = 1200.5f;

	FTDFluidGrid2DSolver Solver;
	StartScenario(Solver, MakeScenarioConfig());
	const TArray<FTDFluidSdfObstacle> Obstacles = MakeScenarioObstacles();
	for (int32 Step = 0; Step < 200; ++Step)
	{
		const float DeltaTime = StepSecondsCycle[Step % StepSecondsCycleLength];
		const FVector2D DesiredCenter(Step * 9.0, Step * -4.0);
		if (Solver.ShiftOriginForSlidingWindow(DesiredCenter))
		{
			ApplyTerrainElevation(Solver, BankedBedElevationCm);
			Solver.RasterizeSdfObstacles(Obstacles);
		}
		Solver.ApplyBrushStamps(MakeWaterScenarioStamps(Solver.GetCenterWorld(), Step), DeltaTime);
		Solver.StepSimulation(DeltaTime);
	}

	const int32 Resolution = Solver.GetResolution();
	int32 NonFiniteCellCount = 0;
	int32 TooFastCellCount = 0;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
			const bool bIsFinite = FMath::IsFinite(Sample.Velocity.X) && FMath::IsFinite(Sample.Velocity.Y) && FMath::IsFinite(Sample.Pressure)
				&& FMath::IsFinite(Sample.PrimaryDensity) && FMath::IsFinite(Sample.RippleHeightCm) && FMath::IsFinite(Sample.ObstacleSdf);
			NonFiniteCellCount += bIsFinite ? 0 : 1;
			TooFastCellCount += Sample.Velocity.Size() > MaximumExpectedSpeedCmPerSec ? 1 : 0;
		}
	}

	TArray<FFloat16Color> WaterSurfacePixels;
	Solver.PackWaterSurfacePixels(WaterSurfacePixels, 0.0f);
	int32 NonFinitePixelCount = 0;
	for (const FFloat16Color& Pixel : WaterSurfacePixels)
	{
		const bool bIsFinite = FMath::IsFinite(Pixel.R.GetFloat()) && FMath::IsFinite(Pixel.G.GetFloat()) && FMath::IsFinite(Pixel.B.GetFloat()) && FMath::IsFinite(Pixel.A.GetFloat());
		NonFinitePixelCount += bIsFinite ? 0 : 1;
	}

	const FWaterSnapshot Snapshot = CaptureWaterSnapshot(Solver);
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterFinite: maxSpeed=%.2f maxRipple=%.3f maxDensity=%.3f"), Snapshot.MaximumSpeed, Snapshot.MaximumRipple, Snapshot.MaximumDensity));
	TestEqual(TEXT("Cells with non-finite values after irregular steps"), NonFiniteCellCount, 0);
	TestEqual(TEXT("Cells faster than the solver speed limit"), TooFastCellCount, 0);
	TestEqual(TEXT("Water surface pixels with non-finite values"), NonFinitePixelCount, 0);
	TestTrue(TEXT("The scenario still carries flow"), Snapshot.MaximumSpeed > 10.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterFlowGuideRasterizesTangentInsideWidthTest,
	"TDGame.FluidFX.Water.FlowGuide.RasterizesTangentInsideWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterFlowGuideRasterizesTangentInsideWidthTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	constexpr float HalfWidthCm = 300.0f;
	constexpr float SpeedCmPerSec = 200.0f;
	FTDFluidSimulationConfig Config = MakeWaterTestConfig(64);
	Config.FlowGuideBankFalloffCm = 100.0f;

	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		TArray<FTDFluidFlowGuide> Guides;
		Guides.Add(MakeStraightGuide(FVector2D(-900.0, 0.0), FVector2D(900.0, 0.0), HalfWidthCm, SpeedCmPerSec));
		Solver.SetFlowGuides(Guides);
		RunSteps(Solver, 1);

		const FTDFluidCellSample CenterSample = Solver.SampleAtCell(32, 32);
		TestTrue(TEXT("Straight guide center target x is 200 cm/s within 1 percent"), FMath::IsNearlyEqual(CenterSample.FlowTargetVelocity.X, SpeedCmPerSec, SpeedCmPerSec * 0.01));
		TestTrue(TEXT("Straight guide center target y is zero"), FMath::Abs(CenterSample.FlowTargetVelocity.Y) < 0.5);
		TestEqual(TEXT("Straight guide center weight is one"), CenterSample.FlowGuideWeight, 1.0f);
		TestTrue(TEXT("The first step starts the flow from the guide target instead of from rest"), CenterSample.Velocity.X > 0.9 * SpeedCmPerSec);
		TestEqual(TEXT("Guide surface height is written inside the width"), Solver.GetWaterSurfaceHeightAtCell(32, 32), RiverSurfaceCm);

		const FTDFluidCellSample BandSample = Solver.SampleAtCell(32, 32 + 11);
		TestTrue(TEXT("A cell inside the bank falloff has a partial weight"), BandSample.FlowGuideWeight > 0.0f && BandSample.FlowGuideWeight < 1.0f);

		const FTDFluidCellSample OutsideSample = Solver.SampleAtCell(32, 32 + 16);
		TestEqual(TEXT("A cell beyond the half width has weight zero"), OutsideSample.FlowGuideWeight, 0.0f);
		TestTrue(TEXT("A cell beyond the half width has a zero target"), OutsideSample.FlowTargetVelocity.IsNearlyZero());
		TestEqual(TEXT("A cell beyond the half width has no surface"), Solver.GetWaterSurfaceHeightAtCell(32, 32 + 16), FTDFluidGrid2DSolver::NoWaterSurfaceHeight);

		const FTDFluidCellSample UpstreamSample = Solver.SampleAtCell(0, 32);
		TestTrue(TEXT("The window edge column is still inside the guide"), UpstreamSample.FlowGuideWeight > 0.99f);
	}

	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		TArray<FTDFluidFlowGuide> Guides;
		Guides.Add(MakeStraightGuide(FVector2D(-600.0, -600.0), FVector2D(600.0, 600.0), HalfWidthCm, SpeedCmPerSec));
		Solver.SetFlowGuides(Guides);
		RunSteps(Solver, 1);

		const FTDFluidCellSample DiagonalSample = Solver.SampleAtCell(31, 31);
		const double ExpectedComponent = SpeedCmPerSec * FMath::Sqrt(0.5);
		TestTrue(TEXT("Diagonal guide target x follows the tangent"), FMath::IsNearlyEqual(DiagonalSample.FlowTargetVelocity.X, ExpectedComponent, ExpectedComponent * 0.01));
		TestTrue(TEXT("Diagonal guide target y follows the tangent"), FMath::IsNearlyEqual(DiagonalSample.FlowTargetVelocity.Y, ExpectedComponent, ExpectedComponent * 0.01));
	}
	return true;
}

namespace TDFluidWaterSolverTestPrivate
{
	FVector2D ComputePrimaryDensityCentroid(const FTDFluidGrid2DSolver& Solver, double& OutMass)
	{
		const int32 Resolution = Solver.GetResolution();
		FVector2D WeightedSum = FVector2D::ZeroVector;
		OutMass = 0.0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const double Density = Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
				WeightedSum += Solver.CellToWorldCenter(CellX, CellY) * Density;
				OutMass += Density;
			}
		}
		return OutMass > UE_SMALL_NUMBER ? WeightedSum / OutMass : FVector2D::ZeroVector;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterFlowGuideFoamBlobDriftsDownstreamTest,
	"TDGame.FluidFX.Water.FlowGuide.FoamBlobDriftsDownstream",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterFlowGuideFoamBlobDriftsDownstreamTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	constexpr float SpeedCmPerSec = 200.0f;
	constexpr int32 DriftStepCount = 60;
	FTDFluidSimulationConfig Config = MakeWaterTestConfig(64);
	Config.WhitewaterFromVorticity = 0.0f;
	Config.RapidsWhitewaterPerSec = 0.0f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeStraightGuide(FVector2D(-2000.0, 0.0), FVector2D(2000.0, 0.0), 900.0f, SpeedCmPerSec));
	Solver.SetFlowGuides(Guides);
	RunSteps(Solver, 1);

	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = FVector(-300.0, 0.0, 0.0);
	Stamp.RadiusCm = 90.0f;
	Stamp.DensityStrength = 1.0f;
	Stamp.VelocityStrength = 0.0f;
	Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
	Solver.ApplyBrushStamps({ Stamp }, StepSeconds);

	double StartMass = 0.0;
	const FVector2D StartCentroid = ComputePrimaryDensityCentroid(Solver, StartMass);
	RunSteps(Solver, DriftStepCount);
	double EndMass = 0.0;
	const FVector2D EndCentroid = ComputePrimaryDensityCentroid(Solver, EndMass);

	const double ElapsedSeconds = DriftStepCount * StepSeconds;
	const double ExpectedTravelCm = SpeedCmPerSec * ElapsedSeconds;
	const FVector2D Drift = EndCentroid - StartCentroid;
	AddInfo(FString::Printf(TEXT("FoamBlobDrift x %.1f y %.1f cm over %.2f s, mass %.2f -> %.2f"), Drift.X, Drift.Y, ElapsedSeconds, StartMass, EndMass));
	TestTrue(TEXT("The stamped foam blob has mass"), StartMass > 1.0);
	TestTrue(TEXT("Foam drifts downstream at least half the guide travel"), Drift.X >= 0.5 * ExpectedTravelCm);
	TestTrue(TEXT("Foam does not outrun the guide flow"), Drift.X <= 1.5 * ExpectedTravelCm);
	TestTrue(TEXT("Foam stays on the guide line"), FMath::Abs(Drift.Y) < 40.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterFlowGuideParabolicProfileSlowerAtBankTest,
	"TDGame.FluidFX.Water.FlowGuide.ParabolicProfileSlowerAtBank",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterFlowGuideParabolicProfileSlowerAtBankTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	constexpr float HalfWidthCm = 400.0f;
	constexpr float SpeedCmPerSec = 200.0f;
	FTDFluidSimulationConfig Config = MakeWaterTestConfig(64);
	Config.FlowGuideBankFalloffCm = 150.0f;
	Config.FlowGuideBankSpeedRatio = 0.35f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeStraightGuide(FVector2D(-900.0, 0.0), FVector2D(900.0, 0.0), HalfWidthCm, SpeedCmPerSec));
	Solver.SetFlowGuides(Guides);
	RunSteps(Solver, 1);

	int32 CheckedCellCount = 0;
	int32 WrongProfileCount = 0;
	double PreviousSpeed = TNumericLimits<double>::Max();
	for (int32 CellY = 32; CellY < 64; ++CellY)
	{
		const FTDFluidCellSample Sample = Solver.SampleAtCell(32, CellY);
		const double Distance = FMath::Abs(Solver.CellToWorldCenter(32, CellY).Y);
		if (Distance >= HalfWidthCm)
		{
			TestEqual(TEXT("No target beyond the half width"), Sample.FlowGuideWeight, 0.0f);
			continue;
		}

		const double Normalized = Distance / HalfWidthCm;
		const double ExpectedSpeed = SpeedCmPerSec * (0.35 + 0.65 * (1.0 - Normalized * Normalized));
		const double ActualSpeed = Sample.FlowTargetVelocity.Size();
		++CheckedCellCount;
		WrongProfileCount += FMath::IsNearlyEqual(ActualSpeed, ExpectedSpeed, ExpectedSpeed * 0.005) ? 0 : 1;
		TestTrue(TEXT("Target speed never increases toward the bank"), ActualSpeed <= PreviousSpeed + 0.001);
		PreviousSpeed = ActualSpeed;
	}

	const FTDFluidCellSample CenterSample = Solver.SampleAtCell(32, 32);
	const FTDFluidCellSample BankSample = Solver.SampleAtCell(32, 32 + 20);
	TestTrue(TEXT("Cells were checked"), CheckedCellCount > 15);
	TestEqual(TEXT("Cells whose target speed differs from the parabolic profile by more than 0.5 percent"), WrongProfileCount, 0);
	TestTrue(TEXT("Bank speed is under 45 percent of the center speed"), BankSample.FlowTargetVelocity.Size() < 0.45 * CenterSample.FlowTargetVelocity.Size());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterFlowGuideShiftEqualsFullRasterizeTest,
	"TDGame.FluidFX.Water.FlowGuide.ShiftEqualsFullRasterize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterFlowGuideShiftEqualsFullRasterizeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	const FTDFluidSimulationConfig Config = MakeWaterTestConfig(64);
	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeSinuousGuide(350.0f, 200.0f));
	Guides.Add(MakeStraightGuide(FVector2D(-900.0, 250.0), FVector2D(900.0, -300.0), 250.0f, 120.0f));
	FTDFluidWaterRegion Region;
	Region.MinXY = FVector2D(200.0, 300.0);
	Region.MaxXY = FVector2D(700.0, 560.0);
	Region.SurfaceHeightCm = 25.0f;
	TArray<FTDFluidWaterRegion> Regions;
	Regions.Add(Region);

	FTDFluidGrid2DSolver Shifted;
	Shifted.Initialize(Config, FVector2D::ZeroVector);
	Shifted.SetFlowGuides(Guides);
	Shifted.SetWaterRegions(Regions);
	RunSteps(Shifted, 1);
	TestTrue(TEXT("First shift"), Shifted.ShiftOriginForSlidingWindow(Shifted.GetCenterWorld() + FVector2D(5 * SquareCellSizeCm, 0.0)));
	RunSteps(Shifted, 1);
	TestTrue(TEXT("Second shift"), Shifted.ShiftOriginForSlidingWindow(Shifted.GetCenterWorld() + FVector2D(0.0, -7 * SquareCellSizeCm)));
	TestTrue(TEXT("Third shift without a step in between"), Shifted.ShiftOriginForSlidingWindow(Shifted.GetCenterWorld() + FVector2D(-3 * SquareCellSizeCm, 4 * SquareCellSizeCm)));
	RunSteps(Shifted, 1);

	FTDFluidGrid2DSolver Reference;
	Reference.Initialize(Config, Shifted.GetCenterWorld());
	Reference.SetFlowGuides(Guides);
	Reference.SetWaterRegions(Regions);
	RunSteps(Reference, 1);

	const int32 Resolution = Shifted.GetResolution();
	int32 DifferentCellCount = 0;
	int32 CellsWithGuide = 0;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FTDFluidCellSample A = Shifted.SampleAtCell(CellX, CellY);
			const FTDFluidCellSample B = Reference.SampleAtCell(CellX, CellY);
			const float SurfaceA = Shifted.GetWaterSurfaceHeightAtCell(CellX, CellY);
			const float SurfaceB = Reference.GetWaterSurfaceHeightAtCell(CellX, CellY);
			const bool bIdentical = FMemory::Memcmp(&SurfaceA, &SurfaceB, sizeof(float)) == 0
				&& FMemory::Memcmp(&A.FlowGuideWeight, &B.FlowGuideWeight, sizeof(float)) == 0
				&& FMemory::Memcmp(&A.FlowTargetVelocity, &B.FlowTargetVelocity, sizeof(FVector2D)) == 0;
			DifferentCellCount += bIdentical ? 0 : 1;
			CellsWithGuide += B.FlowGuideWeight > 0.0f ? 1 : 0;
		}
	}

	TestTrue(TEXT("The comparison covers guide cells"), CellsWithGuide > 200);
	TestEqual(TEXT("Cells whose raster differs between strip raster and full raster"), DifferentCellCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRiverOpenBordersCarryDischargeTest,
	"TDGame.FluidFX.Water.River.OpenBordersCarryDischarge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRiverOpenBordersCarryDischargeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FTDFluidSimulationConfig Config = MakeWaterTestConfig(128);
	Config.bOpenWaterBorders = true;
	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeStraightGuide(FVector2D(-1500.0, 0.0), FVector2D(1500.0, 0.0), 400.0f, 200.0f));

	FTDFluidGrid2DSolver Solver;
	StartRiver(Solver, Config, Guides, TArray<FTDFluidSdfObstacle>());
	RunSteps(Solver, 3600);

	const int32 SectionCells[] = { 32, 64, 96 };
	double NetFluxes[3] = {};
	for (int32 SectionIndex = 0; SectionIndex < 3; ++SectionIndex)
	{
		const FSectionFlux Flux = MeasureSectionFlux(Solver, SectionCells[SectionIndex]);
		NetFluxes[SectionIndex] = Flux.NetFluxCm2PerSec;
		ReportLine(*this, FString::Printf(TEXT("TDFluidWaterRiver: open section=%d net=%.1f target=%.1f reverse=%.2f ratio=%.4f"), SectionCells[SectionIndex], Flux.NetFluxCm2PerSec, Flux.TargetFluxCm2PerSec, Flux.ReverseFluxCm2PerSec, Flux.NetFluxCm2PerSec / Flux.TargetFluxCm2PerSec));
		TestTrue(*FString::Printf(TEXT("Section %d target flux is positive"), SectionCells[SectionIndex]), Flux.TargetFluxCm2PerSec > 1000.0);
		TestTrue(*FString::Printf(TEXT("Section %d net flux is within 15 percent of the target flux"), SectionCells[SectionIndex]), FMath::Abs(Flux.NetFluxCm2PerSec - Flux.TargetFluxCm2PerSec) <= 0.15 * Flux.TargetFluxCm2PerSec);
		TestTrue(*FString::Printf(TEXT("Section %d reverse flux is under 5 percent of the target flux"), SectionCells[SectionIndex]), Flux.ReverseFluxCm2PerSec < 0.05 * Flux.TargetFluxCm2PerSec);
	}

	const double MeanFlux = (NetFluxes[0] + NetFluxes[1] + NetFluxes[2]) / 3.0;
	for (int32 SectionIndex = 0; SectionIndex < 3; ++SectionIndex)
	{
		TestTrue(*FString::Printf(TEXT("Section %d net flux is within 10 percent of the section mean"), SectionCells[SectionIndex]), FMath::Abs(NetFluxes[SectionIndex] - MeanFlux) <= 0.10 * FMath::Abs(MeanFlux));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRiverClosedBordersControlStallsTest,
	"TDGame.FluidFX.Water.River.ClosedBordersControlStalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRiverClosedBordersControlStallsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FTDFluidSimulationConfig Config = MakeWaterTestConfig(128);
	Config.bOpenWaterBorders = false;
	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeStraightGuide(FVector2D(-1500.0, 0.0), FVector2D(1500.0, 0.0), 400.0f, 200.0f));

	FTDFluidGrid2DSolver Solver;
	StartRiver(Solver, Config, Guides, TArray<FTDFluidSdfObstacle>());
	RunSteps(Solver, 3600);

	for (const int32 SectionCell : { 32, 64, 96 })
	{
		const FSectionFlux Flux = MeasureSectionFlux(Solver, SectionCell);
		ReportLine(*this, FString::Printf(TEXT("TDFluidWaterRiver: closed section=%d net=%.1f target=%.1f reverse=%.1f ratio=%.4f"), SectionCell, Flux.NetFluxCm2PerSec, Flux.TargetFluxCm2PerSec, Flux.ReverseFluxCm2PerSec, Flux.NetFluxCm2PerSec / Flux.TargetFluxCm2PerSec));
		TestTrue(*FString::Printf(TEXT("Closed borders stall section %d below 30 percent of the target flux"), SectionCell), Flux.NetFluxCm2PerSec < 0.30 * Flux.TargetFluxCm2PerSec);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRiverNarrowingAcceleratesFlowTest,
	"TDGame.FluidFX.Water.River.NarrowingAcceleratesFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRiverNarrowingAcceleratesFlowTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FTDFluidSimulationConfig Config = MakeWaterTestConfig(128);
	TArray<FGuideKnot> Knots;
	Knots.Add({ -1500.0f, 400.0f, 200.0f });
	Knots.Add({ -700.0f, 400.0f, 200.0f });
	Knots.Add({ -400.0f, 200.0f, 400.0f });
	Knots.Add({ 500.0f, 200.0f, 400.0f });
	Knots.Add({ 800.0f, 400.0f, 200.0f });
	Knots.Add({ 1500.0f, 400.0f, 200.0f });
	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeProfiledGuide(Knots));

	FTDFluidGrid2DSolver Solver;
	StartRiver(Solver, Config, Guides, TArray<FTDFluidSdfObstacle>());
	RunSteps(Solver, 3600);

	const auto MeasureCenterSpeed = [&Solver](float WorldX)
	{
		FVector2D GridUV;
		Solver.WorldToGridUV(FVector2D(WorldX, 0.0), GridUV);
		const int32 CellX = FMath::FloorToInt(GridUV.X * Solver.GetResolution());
		const int32 CenterRow = Solver.GetResolution() / 2;
		return 0.5 * (Solver.SampleAtCell(CellX, CenterRow - 1).Velocity.X + Solver.SampleAtCell(CellX, CenterRow).Velocity.X);
	};

	const double WideSpeed = MeasureCenterSpeed(-900.0f);
	const double NarrowSpeed = MeasureCenterSpeed(50.0f);
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterRiver: wide_center_speed=%.2f narrow_center_speed=%.2f ratio=%.3f"), WideSpeed, NarrowSpeed, NarrowSpeed / WideSpeed));
	TestTrue(TEXT("The wide section flows downstream"), WideSpeed > 150.0);
	TestTrue(TEXT("The half width section center is at least 1.6 times faster"), NarrowSpeed >= 1.6 * WideSpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRiverRockSplitsFlowAndLeavesWhitewaterTest,
	"TDGame.FluidFX.Water.River.RockSplitsFlowAndLeavesWhitewater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRiverRockSplitsFlowAndLeavesWhitewaterTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	constexpr double RockCenterX = -200.0;
	constexpr double RockRadiusCm = 70.0;
	FTDFluidSimulationConfig Config = MakeWaterTestConfig(128);
	Config.WhitewaterFromVorticity = 0.3f;
	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeStraightGuide(FVector2D(-1500.0, 0.0), FVector2D(1500.0, 0.0), 400.0f, 200.0f));
	TArray<FTDFluidSdfObstacle> Obstacles;
	FTDFluidSdfObstacle Rock;
	Rock.WorldCenter = FVector(RockCenterX, 0.0, 0.0);
	Rock.RadiusCm = static_cast<float>(RockRadiusCm);
	Obstacles.Add(Rock);

	FTDFluidGrid2DSolver Solver;
	StartRiver(Solver, Config, Guides, Obstacles);
	RunSteps(Solver, 1800);

	const FTDFluidCellSample RockSample = SampleCellAtWorld(Solver, FVector2D(RockCenterX, 0.0));
	TestTrue(TEXT("The rock cell is solid"), RockSample.ObstacleSdf <= 0.0f);
	TestTrue(TEXT("Velocity inside the rock is zero"), RockSample.Velocity.IsNearlyZero(0.001));

	const double FlankDistance = RockRadiusCm + 30.0;
	const double FlankOffset = FlankDistance * FMath::Sqrt(0.5);
	const FTDFluidCellSample UpperFlank = SampleCellAtWorld(Solver, FVector2D(RockCenterX - FlankOffset, FlankOffset));
	const FTDFluidCellSample LowerFlank = SampleCellAtWorld(Solver, FVector2D(RockCenterX - FlankOffset, -FlankOffset));
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterRock: upper_flank_vy=%.2f lower_flank_vy=%.2f upper_vx=%.2f"), UpperFlank.Velocity.Y, LowerFlank.Velocity.Y, UpperFlank.Velocity.X));
	TestTrue(TEXT("Upstream upper flank flows away from the rock centerline"), UpperFlank.Velocity.Y > 1.0);
	TestTrue(TEXT("Upstream lower flank flows away from the rock centerline"), LowerFlank.Velocity.Y < -1.0);

	double WakeWhitewater = 0.0;
	double OpenWaterWhitewater = 0.0;
	const int32 Resolution = Solver.GetResolution();
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FVector2D CellWorld = Solver.CellToWorldCenter(CellX, CellY);
			const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
			if (Sample.ObstacleSdf <= 0.0f)
			{
				continue;
			}
			const bool bInWake = CellWorld.X > RockCenterX + RockRadiusCm && CellWorld.X <= RockCenterX + RockRadiusCm + 300.0 && FMath::Abs(CellWorld.Y) <= 2.0 * RockRadiusCm;
			if (bInWake)
			{
				WakeWhitewater = FMath::Max(WakeWhitewater, static_cast<double>(Sample.PrimaryDensity));
			}
			else if (CellWorld.X < RockCenterX - 3.0 * RockRadiusCm)
			{
				OpenWaterWhitewater = FMath::Max(OpenWaterWhitewater, static_cast<double>(Sample.PrimaryDensity));
			}
		}
	}
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterRock: wake_whitewater_max=%.3f upstream_whitewater_max=%.3f"), WakeWhitewater, OpenWaterWhitewater));
	TestTrue(TEXT("Whitewater within 3 m downstream of the rock reaches 0.2"), WakeWhitewater >= 0.2);
	TestTrue(TEXT("Whitewater upstream of the rock stays lower than in the wake"), OpenWaterWhitewater < WakeWhitewater);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterDepthDryCellsAreSolidTest,
	"TDGame.FluidFX.Water.WaterDepth.DryCellsAreSolidInSdfAndMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterDepthDryCellsAreSolidTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FTDFluidSimulationConfig Config = MakeWaterTestConfig(64);
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	const int32 Resolution = Solver.GetResolution();
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FVector2D CellWorld = Solver.CellToWorldCenter(CellX, CellY);
			if (CellWorld.X > 500.0)
			{
				continue;
			}
			Solver.SetTerrainElevationAtCell(CellX, CellY, static_cast<float>(-100.0 + 0.5 * FMath::Abs(CellWorld.Y)));
		}
	}
	Solver.ComputeTerrainSlopeFromElevation();

	FTDFluidWaterRegion Region;
	Region.MinXY = FVector2D(-5000.0, -5000.0);
	Region.MaxXY = FVector2D(5000.0, 5000.0);
	Region.SurfaceHeightCm = RiverSurfaceCm;
	TArray<FTDFluidWaterRegion> Regions;
	Regions.Add(Region);
	Solver.SetWaterRegions(Regions);

	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = FVector::ZeroVector;
	Stamp.RadiusCm = 900.0f;
	Stamp.DensityStrength = 1.0f;
	Stamp.VelocityStrength = 0.0f;
	Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
	Solver.ApplyBrushStamps({ Stamp }, StepSeconds);
	RunSteps(Solver, 1);

	const FTDFluidCellSample DrySample = Solver.SampleAtCell(10, 53);
	const FTDFluidCellSample WaterSample = Solver.SampleAtCell(10, 32);
	const FTDFluidCellSample UnknownSample = Solver.SampleAtCell(60, 53);

	TestTrue(TEXT("Dry cell has a non-positive obstacle SDF"), DrySample.ObstacleSdf <= 0.0f);
	TestEqual(TEXT("Dry cell holds no foam density"), DrySample.PrimaryDensity, 0.0f);
	TestTrue(TEXT("Dry cell holds no velocity"), DrySample.Velocity.IsNearlyZero(0.0001));
	TestTrue(TEXT("Dry cell has no positive water depth"), DrySample.WaterDepthCm <= 0.0f);
	TestTrue(TEXT("Water cell has a positive obstacle SDF"), WaterSample.ObstacleSdf > 0.0f);
	TestTrue(TEXT("Water cell has positive depth"), WaterSample.WaterDepthCm > 50.0f);
	TestTrue(TEXT("Water cell kept its foam density"), WaterSample.PrimaryDensity > 0.0f);
	TestFalse(TEXT("Cell with unknown terrain reports unknown"), UnknownSample.bIsTerrainKnown);
	TestTrue(TEXT("Cell with unknown terrain is not solid"), UnknownSample.ObstacleSdf > 0.0f);
	TestTrue(TEXT("Cell with unknown terrain kept its foam density"), UnknownSample.PrimaryDensity > 0.0f);

	TArray<FFloat16Color> DensityPressurePixels;
	TArray<FFloat16Color> VelocityPixels;
	Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);
	TestEqual(TEXT("Dry cell output alpha is zero"), DensityPressurePixels[53 * Resolution + 10].A.GetFloat(), 0.0f);
	TestTrue(TEXT("Water cell output alpha is positive"), DensityPressurePixels[32 * Resolution + 32].A.GetFloat() > 0.0f);
	TestTrue(TEXT("Unknown terrain cell output alpha is positive"), DensityPressurePixels[53 * Resolution + 60].A.GetFloat() > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRippleRingRadiusMatchesWaveSpeedTest,
	"TDGame.FluidFX.Water.Ripple.RingRadiusMatchesWaveSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRippleRingRadiusMatchesWaveSpeedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	constexpr float WaveSpeedCmPerSec = 120.0f;
	constexpr int32 StepCount = 180;
	FTDFluidSimulationConfig Config = MakeWaterTestConfig(128);
	Config.RippleWaveSpeedCmPerSec = WaveSpeedCmPerSec;
	Config.RippleDampingPerSec = 0.0f;
	Config.RippleFlowAdvection = 1.0f;
	Config.VorticityStrength = 0.0f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	const FVector2D Origin = Solver.CellToWorldCenter(64, 64);

	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = FVector(Origin.X, Origin.Y, 0.0);
	Stamp.RadiusCm = 40.0f;
	Stamp.DensityStrength = 0.0f;
	Stamp.VelocityStrength = 0.0f;
	Stamp.RippleImpulseCm = 6.0f;
	Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
	Solver.ApplyBrushStamps({ Stamp }, StepSeconds);
	TestTrue(TEXT("The impulse lowers the stamp center"), Solver.SampleAtCell(64, 64).RippleHeightCm < -5.0f);

	RunSteps(Solver, StepCount);
	const FRingMeasurement Ring = MeasureRingRadius(Solver, 64, 64);
	const double ExpectedRadiusCells = WaveSpeedCmPerSec * StepCount * StepSeconds / SquareCellSizeCm;
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterRipple: ring_radius_cells=%.3f expected=%.3f peak_cm=%.4f"), Ring.RadiusCells, ExpectedRadiusCells, Ring.PeakAmplitudeCm));
	TestTrue(TEXT("The leading lobe of the ring sits at wave speed times time within 1.5 cells"), FMath::Abs(Ring.RadiusCells - ExpectedRadiusCells) <= 1.5);
	TestTrue(TEXT("The ring still carries amplitude"), Ring.PeakAmplitudeCm > 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRippleBorderAbsorbsTest,
	"TDGame.FluidFX.Water.Ripple.BorderAbsorbs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRippleBorderAbsorbsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FTDFluidSimulationConfig Config = MakeWaterTestConfig(96);
	Config.RippleWaveSpeedCmPerSec = 200.0f;
	Config.RippleDampingPerSec = 0.0f;
	Config.RippleFlowAdvection = 1.0f;
	Config.VorticityStrength = 0.0f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	const FVector2D Origin = Solver.CellToWorldCenter(48, 48);

	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = FVector(Origin.X, Origin.Y, 0.0);
	Stamp.RadiusCm = 40.0f;
	Stamp.DensityStrength = 0.0f;
	Stamp.VelocityStrength = 0.0f;
	Stamp.RippleImpulseCm = -6.0f;
	Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
	Solver.ApplyBrushStamps({ Stamp }, StepSeconds);

	RunSteps(Solver, 90);
	const double ReferenceEnergy = MeasureRippleEnergy(Solver);
	RunSteps(Solver, 840);
	const double FinalEnergy = MeasureRippleEnergy(Solver);
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterRipple: reference_energy=%.4f final_energy=%.6f ratio=%.5f"), ReferenceEnergy, FinalEnergy, FinalEnergy / ReferenceEnergy));
	TestTrue(TEXT("The expanding ring carries energy before it reaches the border"), ReferenceEnergy > 0.01);
	TestTrue(TEXT("Interior energy after the wave crossed the border is under 10 percent"), FinalEnergy < 0.10 * ReferenceEnergy);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRippleAdvectionKeepsAmplitudeTest,
	"TDGame.FluidFX.Water.Ripple.AdvectionKeepsAmplitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRippleAdvectionKeepsAmplitudeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	constexpr float AmplitudeCm = 4.0f;
	constexpr double WavelengthCells = 8.0;
	constexpr int32 Resolution = 64;
	FTDFluidSimulationConfig Config = MakeWaterTestConfig(Resolution);
	Config.BorderFadeRatio = 0.0f;
	Config.VorticityStrength = 0.0f;
	Config.RippleWaveSpeedCmPerSec = 0.5f;
	Config.RippleDampingPerSec = 0.0f;
	Config.RippleFlowAdvection = 1.0f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeStraightGuide(FVector2D(-1500.0, 0.0), FVector2D(1500.0, 0.0), 5000.0f, 250.0f));
	Solver.SetFlowGuides(Guides);

	TArray<FTDFluidBrushStamp> Stamps;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FVector2D CellWorld = Solver.CellToWorldCenter(CellX, CellY);
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(CellWorld.X, CellWorld.Y, 0.0);
			Stamp.RadiusCm = 5.0f;
			Stamp.DensityStrength = 0.0f;
			Stamp.VelocityStrength = 0.0f;
			Stamp.RippleImpulseCm = static_cast<float>(-AmplitudeCm * FMath::Sin(UE_DOUBLE_TWO_PI * CellX / WavelengthCells));
			Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			Stamps.Add(Stamp);
			if (Stamps.Num() == 512)
			{
				Solver.ApplyBrushStamps(Stamps, StepSeconds);
				Stamps.Reset();
			}
		}
	}
	Solver.ApplyBrushStamps(Stamps, StepSeconds);

	const auto MeasureAmplitude = [&Solver]()
	{
		double SquareSum = 0.0;
		int32 CellCount = 0;
		for (int32 CellY = 24; CellY < 40; ++CellY)
		{
			for (int32 CellX = 24; CellX < 48; ++CellX)
			{
				SquareSum += FMath::Square(static_cast<double>(Solver.SampleAtCell(CellX, CellY).RippleHeightCm));
				++CellCount;
			}
		}
		return FMath::Sqrt(2.0 * SquareSum / CellCount);
	};

	const double InitialAmplitude = MeasureAmplitude();
	RunSteps(Solver, 60);
	const double FinalAmplitude = MeasureAmplitude();
	const FTDFluidCellSample FlowSample = Solver.SampleAtCell(32, 32);
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterRipple: advection initial=%.4f final=%.4f ratio=%.4f flow_vx=%.2f"), InitialAmplitude, FinalAmplitude, FinalAmplitude / InitialAmplitude, FlowSample.Velocity.X));
	TestTrue(TEXT("The sinusoid was written"), InitialAmplitude > 0.9 * AmplitudeCm);
	TestTrue(TEXT("The flow advects at about 0.2 cells per step"), FlowSample.Velocity.X > 200.0f && FlowSample.Velocity.X < 270.0f);
	TestTrue(TEXT("An 8 cell wave keeps at least 70 percent amplitude after 60 advection steps"), FinalAmplitude >= 0.7 * InitialAmplitude);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterRippleBrushDoesNotWriteDisplacementTest,
	"TDGame.FluidFX.Water.Ripple.BrushDoesNotWriteDisplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterRippleBrushDoesNotWriteDisplacementTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FTDFluidSimulationConfig WaterConfig = MakeWaterTestConfig(48);
	FTDFluidSimulationConfig PlainConfig = WaterConfig;
	PlainConfig.bEnableWaterLayer = false;

	FTDFluidGrid2DSolver WaterSolver;
	WaterSolver.Initialize(WaterConfig, FVector2D::ZeroVector);
	FTDFluidGrid2DSolver PlainSolver;
	PlainSolver.Initialize(PlainConfig, FVector2D::ZeroVector);

	const FVector2D Origin = WaterSolver.CellToWorldCenter(24, 24);
	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = FVector(Origin.X, Origin.Y, 0.0);
	Stamp.RadiusCm = 80.0f;
	Stamp.DensityStrength = 1.0f;
	Stamp.VelocityStrength = 0.0f;
	Stamp.RippleImpulseCm = 3.0f;
	Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
	WaterSolver.ApplyBrushStamps({ Stamp }, StepSeconds);
	PlainSolver.ApplyBrushStamps({ Stamp }, StepSeconds);

	TArray<FFloat16Color> WaterDensityPixels;
	TArray<FFloat16Color> WaterVelocityPixels;
	TArray<FFloat16Color> PlainDensityPixels;
	TArray<FFloat16Color> PlainVelocityPixels;
	WaterSolver.PackOutputPixels(WaterDensityPixels, WaterVelocityPixels);
	PlainSolver.PackOutputPixels(PlainDensityPixels, PlainVelocityPixels);

	const int32 CenterIndex = 24 * 48 + 24;
	TestEqual(TEXT("Water layer brush keeps the pressure channel at 0.5"), WaterDensityPixels[CenterIndex].G.GetFloat(), 0.5f);
	TestTrue(TEXT("Plain solver brush raises the pressure channel with displacement"), PlainDensityPixels[CenterIndex].G.GetFloat() > 0.6f);
	TestTrue(TEXT("Water layer brush still writes foam density"), WaterDensityPixels[CenterIndex].R.GetFloat() > 0.9f);
	TestTrue(TEXT("Water layer brush lowers the ripple height by the impulse"), FMath::IsNearlyEqual(WaterSolver.SampleAtCell(24, 24).RippleHeightCm, -3.0f, 0.001f));
	TestEqual(TEXT("Plain solver has no ripple buffer"), PlainSolver.SampleAtCell(24, 24).RippleHeightCm, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterOutputWaterSurfacePackingTest,
	"TDGame.FluidFX.Water.Output.WaterSurfacePacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterOutputWaterSurfacePackingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	constexpr int32 Resolution = 32;
	constexpr float TerrainReferenceZCm = 20.0f;
	FTDFluidSimulationConfig Config = MakeWaterTestConfig(Resolution);
	Config.WaterDepthReferenceCm = 200.0f;
	Config.RippleHeightReferenceCm = 8.0f;
	Config.RippleWaveSpeedCmPerSec = 0.0f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	FTDFluidWaterRegion Region;
	Region.MinXY = FVector2D(-5000.0, -5000.0);
	Region.MaxXY = FVector2D(5000.0, 5000.0);
	Region.SurfaceHeightCm = 100.0f;
	TArray<FTDFluidWaterRegion> Regions;
	Regions.Add(Region);
	Solver.SetWaterRegions(Regions);

	Solver.SetTerrainElevationAtCell(10, 10, 40.0f);
	Solver.SetTerrainElevationAtCell(11, 10, 100.0f);
	Solver.SetTerrainElevationAtCell(12, 10, 160.0f);
	Solver.SetTerrainElevationAtCell(13, 10, 20000.0f);
	Solver.SetTerrainElevationAtCell(14, 10, -20000.0f);
	Solver.ComputeTerrainSlopeFromElevation();

	const FVector2D RippleOrigin = Solver.CellToWorldCenter(10, 10);
	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = FVector(RippleOrigin.X, RippleOrigin.Y, 0.0);
	Stamp.RadiusCm = 5.0f;
	Stamp.DensityStrength = 0.0f;
	Stamp.VelocityStrength = 0.0f;
	Stamp.RippleImpulseCm = 4.0f;
	Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
	Solver.ApplyBrushStamps({ Stamp }, StepSeconds);
	RunSteps(Solver, 1);

	TArray<FFloat16Color> Pixels;
	Solver.PackWaterSurfacePixels(Pixels, TerrainReferenceZCm);
	TestEqual(TEXT("One pixel per cell"), Pixels.Num(), Resolution * Resolution);
	if (Pixels.Num() != Resolution * Resolution)
	{
		return false;
	}

	const auto ExpectPixel = [this, &Pixels](const TCHAR* Label, int32 CellX, int32 CellY, float R, float G, float B, float A)
	{
		const FFloat16Color& Pixel = Pixels[CellY * Resolution + CellX];
		TestTrue(*FString::Printf(TEXT("%s depth channel (got %.4f, want %.4f)"), Label, Pixel.R.GetFloat(), R), FMath::IsNearlyEqual(Pixel.R.GetFloat(), R, 0.002f));
		TestTrue(*FString::Printf(TEXT("%s wetness channel (got %.4f, want %.4f)"), Label, Pixel.G.GetFloat(), G), FMath::IsNearlyEqual(Pixel.G.GetFloat(), G, 0.002f));
		TestTrue(*FString::Printf(TEXT("%s ripple channel (got %.4f, want %.4f)"), Label, Pixel.B.GetFloat(), B), FMath::IsNearlyEqual(Pixel.B.GetFloat(), B, 0.002f));
		TestTrue(*FString::Printf(TEXT("%s terrain height channel (got %.4f, want %.4f)"), Label, Pixel.A.GetFloat(), A), FMath::IsNearlyEqual(Pixel.A.GetFloat(), A, 0.002f));
	};

	ExpectPixel(TEXT("Rippled water cell"), 10, 10, 60.0f / 200.0f, 0.0f, 0.25f, (40.0f - TerrainReferenceZCm) / 1000.0f);
	ExpectPixel(TEXT("Shoreline cell"), 11, 10, 0.0f, 0.0f, 0.5f, (100.0f - TerrainReferenceZCm) / 1000.0f);
	ExpectPixel(TEXT("Dry cell"), 12, 10, 0.0f, 0.0f, 0.5f, (160.0f - TerrainReferenceZCm) / 1000.0f);
	ExpectPixel(TEXT("Very high terrain cell"), 13, 10, 0.0f, 0.0f, 0.5f, 8.0f);
	ExpectPixel(TEXT("Very deep cell"), 14, 10, 1.0f, 0.0f, 0.5f, -8.0f);
	ExpectPixel(TEXT("Unknown terrain cell"), 20, 20, 0.0f, 0.0f, 0.5f, 8.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterStepCostTest,
	"TDGame.FluidFX.Water.Perf.StepCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterStepCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterSolverTestPrivate;

	FWaterRowParallelScope RowParallelVariable;
	if (!TestNotNull(TEXT("TD.FluidFX.SolverRowParallel console variable exists"), RowParallelVariable.Variable))
	{
		return false;
	}

	for (const int32 Resolution : { 128, 192 })
	{
		for (const bool bShiftsWindow : { false, true })
		{
			for (const int32 RowParallelValue : { 0, 1 })
			{
				RowParallelVariable.SetValue(RowParallelValue);
				const FStepCostResult Cost = MeasureWaterStepCost(Resolution, bShiftsWindow);
				ReportLine(*this, FString::Printf(TEXT("TDFluidWaterBench: resolution=%d shifts=%d row_parallel=%d avg_step_ms=%.4f max_step_ms=%.4f avg_pack_ms=%.4f"), Resolution, bShiftsWindow ? 1 : 0, RowParallelValue, Cost.AverageMilliseconds, Cost.MaximumMilliseconds, Cost.AveragePackMilliseconds));
				TestTrue(TEXT("Water layer step cost is finite"), Cost.AverageMilliseconds > 0.0 && Cost.AverageMilliseconds < 1000.0);
			}
		}
	}
	return true;
}

#endif
