#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "Math/Float16Color.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidWaterGoldenTests, Log, All);

namespace TDFluidWaterGoldenTestPrivate
{
	constexpr int32 GoldenResolution = 192;
	constexpr double GoldenExtentCm = 3600.0;
	constexpr double GoldenCellSizeCm = GoldenExtentCm / GoldenResolution;
	constexpr int32 GoldenStepCount = 240;
	constexpr int32 FirstWindowShiftStep = 60;
	constexpr int32 SecondWindowShiftStep = 120;
	constexpr int32 GuideResetStep = 180;
	constexpr float GoldenStepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 144.0f, 0.1f };
	constexpr int32 GoldenStepSecondsCycleLength = UE_ARRAY_COUNT(GoldenStepSecondsCycle);
	constexpr float GoldenRiverSurfaceCm = 0.0f;
	constexpr uint32 ExpectedStateChecksum = 0xC6E04174u;
	constexpr uint32 ExpectedWaterChecksum = 0xB8696739u;
	constexpr uint32 ExpectedDensityPressurePixelChecksum = 0xB5302E57u;
	constexpr uint32 ExpectedVelocityPixelChecksum = 0xE6D7C1D8u;
	constexpr uint32 ExpectedWaterSurfacePixelChecksum = 0x3F71B606u;
	const TCHAR* const RowParallelVariableName = TEXT("TD.FluidFX.SolverRowParallel");

	struct FTDWaterGoldenRowParallelScope
	{
		FTDWaterGoldenRowParallelScope()
			: Variable(IConsoleManager::Get().FindConsoleVariable(RowParallelVariableName))
		{
			if (Variable)
			{
				PreviousValue = Variable->GetInt();
			}
		}

		~FTDWaterGoldenRowParallelScope()
		{
			SetValue(PreviousValue);
		}

		FTDWaterGoldenRowParallelScope(const FTDWaterGoldenRowParallelScope&) = delete;
		FTDWaterGoldenRowParallelScope& operator=(const FTDWaterGoldenRowParallelScope&) = delete;

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

	FTDFluidSimulationConfig MakeGoldenRiverConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = GoldenResolution;
		Config.SimExtentCm = FVector2D(GoldenExtentCm, GoldenExtentCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.35f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.3f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.ActorBrushDensityStrength = 0.6f;
		Config.MaxPointsPerTick = 512;
		Config.bEnableWaterLayer = true;
		Config.FlowNoiseAccelerationCmPerSec2 = 150.0f;
		Config.RapidsWhitewaterPerSec = 2.0f;
		Config.WhitewaterFromVorticity = 0.3f;
		Config.WhitewaterVorticityThresholdPerSec = 2.0f;
		Config.RippleWaveSpeedCmPerSec = 120.0f;
		Config.RippleDampingPerSec = 1.2f;
		Config.RippleFlowAdvection = 1.0f;
		Config.BankWetHeightCm = 10.0f;
		Config.ActorBrushRippleImpulseCm = 2.5f;
		return Config;
	}

	float EvaluateBankedTerrainElevationCm(const FVector2D& WorldPosition)
	{
		return static_cast<float>(-150.0 + 0.0006 * WorldPosition.Y * WorldPosition.Y);
	}

	void ApplyBankedTerrain(FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, EvaluateBankedTerrainElevationCm(Solver.CellToWorldCenter(CellX, CellY)));
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
	}

	FTDFluidFlowGuidePoint MakeGoldenGuidePoint(double WorldX, double WorldY, float HalfWidthCm, float SpeedCmPerSec)
	{
		FTDFluidFlowGuidePoint Point;
		Point.WorldLocation = FVector(WorldX, WorldY, GoldenRiverSurfaceCm);
		Point.HalfWidthCm = HalfWidthCm;
		Point.FlowSpeedCmPerSec = SpeedCmPerSec;
		return Point;
	}

	TArray<FTDFluidFlowGuide> MakeCurvedWideningGuides(double OffsetYCm, float SpeedScale)
	{
		FTDFluidFlowGuide Guide;
		Guide.Points.Add(MakeGoldenGuidePoint(-2600.0, -350.0 + OffsetYCm, 380.0f, 230.0f * SpeedScale));
		Guide.Points.Add(MakeGoldenGuidePoint(-1300.0, 150.0 + OffsetYCm, 300.0f, 260.0f * SpeedScale));
		Guide.Points.Add(MakeGoldenGuidePoint(0.0, 250.0 + OffsetYCm, 220.0f, 320.0f * SpeedScale));
		Guide.Points.Add(MakeGoldenGuidePoint(1300.0, -100.0 + OffsetYCm, 340.0f, 250.0f * SpeedScale));
		Guide.Points.Add(MakeGoldenGuidePoint(2600.0, -400.0 + OffsetYCm, 420.0f, 220.0f * SpeedScale));
		TArray<FTDFluidFlowGuide> Guides;
		Guides.Add(Guide);
		return Guides;
	}

	TArray<FTDFluidWaterRegion> MakeGoldenRegions()
	{
		FTDFluidWaterRegion Region;
		Region.MinXY = FVector2D(700.0, -460.0);
		Region.MaxXY = FVector2D(1500.0, 420.0);
		Region.SurfaceHeightCm = 15.0f;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(Region);
		return Regions;
	}

	TArray<FTDFluidSdfObstacle> MakeGoldenObstacles()
	{
		FTDFluidSdfObstacle Pillar;
		Pillar.WorldCenter = FVector(-400.0, 120.0, 0.0);
		Pillar.RadiusCm = 90.0f;
		TArray<FTDFluidSdfObstacle> Obstacles;
		Obstacles.Add(Pillar);
		return Obstacles;
	}

	TArray<FTDFluidBrushStamp> MakeGoldenOrbitStamps(int32 StepIndex)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StampIndex = 0; StampIndex < 3; ++StampIndex)
		{
			const double Angle = StepIndex * 0.07 + StampIndex * (UE_DOUBLE_TWO_PI / 3.0);
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(FMath::Cos(Angle) * 500.0, FMath::Sin(Angle) * 260.0, 0.0);
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

	struct FTDWaterGoldenChecksums
	{
		uint32 StateChecksum = 0;
		uint32 WaterChecksum = 0;
		uint32 DensityPressurePixelChecksum = 0;
		uint32 VelocityPixelChecksum = 0;
		uint32 WaterSurfacePixelChecksum = 0;
	};

	uint32 ComputePixelChecksum(const TArray<FFloat16Color>& Pixels)
	{
		return FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color));
	}

	FTDWaterGoldenChecksums RunEnabledGoldenScenario()
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(MakeGoldenRiverConfig(), FVector2D::ZeroVector);
		ApplyBankedTerrain(Solver);
		Solver.SetFlowGuides(MakeCurvedWideningGuides(0.0, 1.0f));
		Solver.SetWaterRegions(MakeGoldenRegions());
		const TArray<FTDFluidSdfObstacle> Obstacles = MakeGoldenObstacles();
		Solver.RasterizeSdfObstacles(Obstacles);

		for (int32 StepIndex = 0; StepIndex < GoldenStepCount; ++StepIndex)
		{
			if (StepIndex == FirstWindowShiftStep || StepIndex == SecondWindowShiftStep)
			{
				const FVector2D ShiftCells = StepIndex == FirstWindowShiftStep ? FVector2D(3.0, -2.0) : FVector2D(-4.0, 1.0);
				Solver.ShiftOriginForSlidingWindow(Solver.GetCenterWorld() + ShiftCells * GoldenCellSizeCm);
				Solver.RasterizeSdfObstacles(Obstacles);
			}
			if (StepIndex == GuideResetStep)
			{
				Solver.SetFlowGuides(MakeCurvedWideningGuides(120.0, 1.1f));
			}

			const float DeltaTime = GoldenStepSecondsCycle[StepIndex % GoldenStepSecondsCycleLength];
			Solver.ApplyBrushStamps(MakeGoldenOrbitStamps(StepIndex), DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}

		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		TArray<FFloat16Color> WaterSurfacePixels;
		Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);
		Solver.PackWaterSurfacePixels(WaterSurfacePixels, 0.0f);

		FTDWaterGoldenChecksums Checksums;
		Checksums.StateChecksum = Solver.ComputeStateChecksum();
		Checksums.WaterChecksum = Solver.ComputeWaterStateChecksum();
		Checksums.DensityPressurePixelChecksum = ComputePixelChecksum(DensityPressurePixels);
		Checksums.VelocityPixelChecksum = ComputePixelChecksum(VelocityPixels);
		Checksums.WaterSurfacePixelChecksum = ComputePixelChecksum(WaterSurfacePixels);
		return Checksums;
	}

	void ExpectGoldenValue(FAutomationTestBase& Test, const FString& Label, const TCHAR* ValueName, uint32 ActualValue, uint32 ExpectedValue)
	{
		if (ActualValue == ExpectedValue)
		{
			return;
		}

		Test.AddError(FString::Printf(TEXT("%s: H5 golden %s mismatch, expected 0x%08X actual 0x%08X"), *Label, ValueName, ExpectedValue, ActualValue));
	}

	void ExpectGoldenChecksums(FAutomationTestBase& Test, const FString& Label, const FTDWaterGoldenChecksums& Checksums)
	{
		ExpectGoldenValue(Test, Label, TEXT("state checksum"), Checksums.StateChecksum, ExpectedStateChecksum);
		ExpectGoldenValue(Test, Label, TEXT("water checksum"), Checksums.WaterChecksum, ExpectedWaterChecksum);
		ExpectGoldenValue(Test, Label, TEXT("density-pressure pixel CRC32"), Checksums.DensityPressurePixelChecksum, ExpectedDensityPressurePixelChecksum);
		ExpectGoldenValue(Test, Label, TEXT("velocity pixel CRC32"), Checksums.VelocityPixelChecksum, ExpectedVelocityPixelChecksum);
		ExpectGoldenValue(Test, Label, TEXT("water surface pixel CRC32"), Checksums.WaterSurfacePixelChecksum, ExpectedWaterSurfacePixelChecksum);
	}

	FString DescribeGoldenChecksums(const TCHAR* ModeName, const FTDWaterGoldenChecksums& Checksums)
	{
		return FString::Printf(
			TEXT("TDFluidWaterGoldenEnabled: mode=%s state=0x%08X water=0x%08X density=0x%08X velocity=0x%08X surface=0x%08X"),
			ModeName,
			Checksums.StateChecksum,
			Checksums.WaterChecksum,
			Checksums.DensityPressurePixelChecksum,
			Checksums.VelocityPixelChecksum,
			Checksums.WaterSurfacePixelChecksum);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterGoldenEnabledChecksumsTest,
	"TDGame.FluidFX.Water.WaterLayer.EnabledGoldenChecksums",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterGoldenEnabledChecksumsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterGoldenTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFX.Water.WaterLayer.EnabledGoldenChecksums");
	FTDWaterGoldenRowParallelScope RowParallelScope;
	if (!TestNotNull(TEXT("TD.FluidFX.SolverRowParallel console variable exists"), RowParallelScope.Variable))
	{
		return false;
	}

	RowParallelScope.SetValue(0);
	const FTDWaterGoldenChecksums SerialChecksums = RunEnabledGoldenScenario();
	RowParallelScope.SetValue(1);
	const FTDWaterGoldenChecksums ParallelChecksums = RunEnabledGoldenScenario();

	const FString SerialLine = DescribeGoldenChecksums(TEXT("serial"), SerialChecksums);
	const FString ParallelLine = DescribeGoldenChecksums(TEXT("parallel"), ParallelChecksums);
	UE_LOG(LogTDFluidWaterGoldenTests, Display, TEXT("%s"), *SerialLine);
	UE_LOG(LogTDFluidWaterGoldenTests, Display, TEXT("%s"), *ParallelLine);
	AddInfo(SerialLine);
	AddInfo(ParallelLine);

	TestEqual(TEXT("Row parallel state checksum equals the serial run"), static_cast<uint32>(ParallelChecksums.StateChecksum), static_cast<uint32>(SerialChecksums.StateChecksum));
	TestEqual(TEXT("Row parallel water checksum equals the serial run"), static_cast<uint32>(ParallelChecksums.WaterChecksum), static_cast<uint32>(SerialChecksums.WaterChecksum));
	TestEqual(TEXT("Row parallel density-pressure pixels equal the serial run"), static_cast<uint32>(ParallelChecksums.DensityPressurePixelChecksum), static_cast<uint32>(SerialChecksums.DensityPressurePixelChecksum));
	TestEqual(TEXT("Row parallel velocity pixels equal the serial run"), static_cast<uint32>(ParallelChecksums.VelocityPixelChecksum), static_cast<uint32>(SerialChecksums.VelocityPixelChecksum));
	TestEqual(TEXT("Row parallel water surface pixels equal the serial run"), static_cast<uint32>(ParallelChecksums.WaterSurfacePixelChecksum), static_cast<uint32>(SerialChecksums.WaterSurfacePixelChecksum));

	ExpectGoldenChecksums(*this, CaseLabel + TEXT(" serial"), SerialChecksums);
	ExpectGoldenChecksums(*this, CaseLabel + TEXT(" parallel"), ParallelChecksums);
	return !HasAnyErrors();
}

#endif
