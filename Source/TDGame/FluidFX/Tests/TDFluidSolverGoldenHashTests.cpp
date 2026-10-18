#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"

namespace TDFluidGoldenHashPrivate
{
	constexpr int32 GoldenStepCount = 120;
	constexpr int32 ElevationRefreshStepInterval = 40;
	constexpr float StepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 45.0f, 0.15f };
	constexpr int32 StepSecondsCycleLength = UE_ARRAY_COUNT(StepSecondsCycle);

	enum class ETerrainSlopeSource : uint8
	{
		None,
		Uniform,
		Elevation
	};

	struct FTDFluidGoldenScenario
	{
		const TCHAR* Name;
		ETDFluidPresetKind PresetKind;
		int32 Resolution;
		FVector2D ExtentCm;
		bool bHasObstacles;
		bool bSlidesWindow;
		ETerrainSlopeSource SlopeSource;
		bool bDisablesOptionalForces;
		uint32 ExpectedStateChecksum;
		uint32 ExpectedPixelChecksum;
	};

	const FTDFluidGoldenScenario GoldenScenarios[] = {
		{ TEXT("WaterNonSquareObstaclesSlidingUniformSlope"), ETDFluidPresetKind::Water, 96, FVector2D(1000.0, 800.0), true, true, ETerrainSlopeSource::Uniform, false, 0x24BE4A74u, 0xA9C79CD5u },
		{ TEXT("MudSquareElevationSlope"), ETDFluidPresetKind::Mud, 64, FVector2D(900.0, 900.0), false, false, ETerrainSlopeSource::Elevation, false, 0xB89FEB00u, 0x476B365Au },
		{ TEXT("FireNonSquareObstaclesSlidingElevationSlope"), ETDFluidPresetKind::Fire, 128, FVector2D(1000.0, 800.0), true, true, ETerrainSlopeSource::Elevation, false, 0x40FC23E6u, 0x0C5E736Cu },
		{ TEXT("BloodTallObstaclesWithoutOptionalForces"), ETDFluidPresetKind::Blood, 48, FVector2D(600.0, 1000.0), true, false, ETerrainSlopeSource::None, true, 0xFC35DA8Bu, 0x8616E732u },
		{ TEXT("FootprintNonSquareObstaclesSliding"), ETDFluidPresetKind::FootprintPainter, 64, FVector2D(1000.0, 800.0), true, true, ETerrainSlopeSource::None, false, 0xABB40516u, 0x748F15C6u },
	};

	struct FTDFluidScenarioChecksums
	{
		uint32 StateChecksum = 0;
		uint32 PixelChecksum = 0;
		float MaxPrimaryDensity = 0.0f;
		float MaxSpeedCmPerSec = 0.0f;
		int32 SolidCellCount = 0;
	};

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

	float EvaluateGoldenElevationCm(const FVector2D& WorldPos)
	{
		return static_cast<float>(40.0 * FMath::Sin(WorldPos.X * 0.004) + 25.0 * FMath::Cos(WorldPos.Y * 0.006) + 0.03 * WorldPos.X);
	}

	void RefreshGoldenElevation(FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, EvaluateGoldenElevationCm(Solver.CellToWorldCenter(CellX, CellY)));
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
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

	FTDFluidScenarioChecksums RunGoldenScenario(const FTDFluidGoldenScenario& Scenario, float SlopeGravityOverride = -1.0f)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Scenario.PresetKind);
		Config.GridResolution = Scenario.Resolution;
		Config.SimExtentCm = Scenario.ExtentCm;
		if (Scenario.bDisablesOptionalForces)
		{
			Config.BorderFadeRatio = 0.0f;
			Config.VorticityStrength = 0.0f;
			Config.TerrainSlopeGravity = 0.0f;
		}
		if (SlopeGravityOverride >= 0.0f)
		{
			Config.TerrainSlopeGravity = SlopeGravityOverride;
		}

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		const TArray<FTDFluidSdfObstacle> Obstacles = Scenario.bHasObstacles ? MakeGoldenObstacles() : TArray<FTDFluidSdfObstacle>();
		if (Scenario.bHasObstacles)
		{
			Solver.RasterizeSdfObstacles(Obstacles);
		}
		if (Scenario.SlopeSource == ETerrainSlopeSource::Uniform)
		{
			Solver.SetUniformTerrainSlope(FVector2D(0.12, -0.07));
		}

		const float OrbitRadiusCm = static_cast<float>(FMath::Min(Scenario.ExtentCm.X, Scenario.ExtentCm.Y) * 0.3);
		for (int32 Step = 0; Step < GoldenStepCount; ++Step)
		{
			const float DeltaTime = StepSecondsCycle[Step % StepSecondsCycleLength];
			const FVector2D DesiredCenter = Scenario.bSlidesWindow ? FVector2D(Step * 4.3, Step * -2.7) : FVector2D::ZeroVector;

			if (Scenario.bSlidesWindow && Solver.ShiftOriginForSlidingWindow(DesiredCenter) && Scenario.bHasObstacles)
			{
				Solver.RasterizeSdfObstacles(Obstacles);
			}
			if (Scenario.SlopeSource == ETerrainSlopeSource::Elevation && Step % ElevationRefreshStepInterval == 0)
			{
				RefreshGoldenElevation(Solver);
			}

			Solver.ApplyBrushStamps(MakeGoldenStamps(DesiredCenter, OrbitRadiusCm, Step), DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}

		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);

		FTDFluidScenarioChecksums Checksums;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				const FTDFluidCellSample CellSample = Solver.SampleAtCell(CellX, CellY);
				Checksums.MaxPrimaryDensity = FMath::Max(Checksums.MaxPrimaryDensity, CellSample.PrimaryDensity);
				Checksums.MaxSpeedCmPerSec = FMath::Max(Checksums.MaxSpeedCmPerSec, static_cast<float>(CellSample.Velocity.Size()));
				Checksums.SolidCellCount += CellSample.ObstacleSdf <= 0.0f ? 1 : 0;
			}
		}
		Checksums.StateChecksum = Solver.ComputeStateChecksum();
		Checksums.PixelChecksum = FCrc::MemCrc32(DensityPressurePixels.GetData(), DensityPressurePixels.Num() * sizeof(FFloat16Color));
		Checksums.PixelChecksum = FCrc::MemCrc32(VelocityPixels.GetData(), VelocityPixels.Num() * sizeof(FFloat16Color), Checksums.PixelChecksum);
		return Checksums;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverGoldenStateChecksumTest,
	"TDGame.FluidFX.SolverRegression.GoldenStateChecksums",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverGoldenStateChecksumTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGoldenHashPrivate;

	for (const FTDFluidGoldenScenario& Scenario : GoldenScenarios)
	{
		const FTDFluidScenarioChecksums Checksums = RunGoldenScenario(Scenario);
		const FTDFluidScenarioChecksums RepeatedChecksums = RunGoldenScenario(Scenario);

		const FString ChecksumLine = FString::Printf(TEXT("TDFluidGoldenHash: scenario=%s state=0x%08X pixels=0x%08X"), Scenario.Name, Checksums.StateChecksum, Checksums.PixelChecksum);
		AddInfo(ChecksumLine);

		TestTrue(*FString::Printf(TEXT("%s carries density after %d steps"), Scenario.Name, GoldenStepCount), Checksums.MaxPrimaryDensity > 0.05f);
		TestTrue(*FString::Printf(TEXT("%s carries velocity after %d steps"), Scenario.Name, GoldenStepCount), Checksums.MaxSpeedCmPerSec > 1.0f);
		TestEqual(*FString::Printf(TEXT("%s has solid cells exactly when it has obstacles"), Scenario.Name), Checksums.SolidCellCount > 0, Scenario.bHasObstacles);

		TestEqual(*FString::Printf(TEXT("%s repeats the same state checksum"), Scenario.Name), RepeatedChecksums.StateChecksum, Checksums.StateChecksum);
		TestEqual(*FString::Printf(TEXT("%s repeats the same pixel checksum"), Scenario.Name), RepeatedChecksums.PixelChecksum, Checksums.PixelChecksum);
		if (!TestEqual(*FString::Printf(TEXT("%s state checksum matches the golden value"), Scenario.Name), Checksums.StateChecksum, Scenario.ExpectedStateChecksum)
			|| !TestEqual(*FString::Printf(TEXT("%s pixel checksum matches the golden value"), Scenario.Name), Checksums.PixelChecksum, Scenario.ExpectedPixelChecksum))
		{
			AddError(ChecksumLine);
		}
	}

	const FTDFluidGoldenScenario FlatWaterScenario = { TEXT("WaterNonSquareObstaclesSlidingFlat"), ETDFluidPresetKind::Water, 96, FVector2D(1000.0, 800.0), true, true, ETerrainSlopeSource::None, false, 0u, 0u };
	const FTDFluidScenarioChecksums FlatWithSlopeGravity = RunGoldenScenario(FlatWaterScenario);
	const FTDFluidScenarioChecksums FlatWithoutSlopeGravity = RunGoldenScenario(FlatWaterScenario, 0.0f);
	TestEqual(TEXT("Slope gravity on a flat (+0.0) slope field leaves the state bit-identical to no slope gravity"), FlatWithSlopeGravity.StateChecksum, FlatWithoutSlopeGravity.StateChecksum);
	TestEqual(TEXT("Slope gravity on a flat (+0.0) slope field leaves the pixels bit-identical to no slope gravity"), FlatWithSlopeGravity.PixelChecksum, FlatWithoutSlopeGravity.PixelChecksum);

	return true;
}

#endif
