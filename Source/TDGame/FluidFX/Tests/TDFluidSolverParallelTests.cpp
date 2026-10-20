#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Math/Float16Color.h"
#include "Misc/AutomationTest.h"

namespace TDFluidSolverParallelTestPrivate
{
	const TCHAR* const RowParallelVariableName = TEXT("TD.FluidFX.SolverRowParallel");
	constexpr float StepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 45.0f, 0.15f };
	constexpr int32 StepSecondsCycleLength = UE_ARRAY_COUNT(StepSecondsCycle);
	constexpr int32 ElevationRefreshStepInterval = 40;
	constexpr int32 RadialPushStepInterval = 5;
	constexpr int32 SnapshotValuesPerCell = 8;
	constexpr int32 SnapshotPrimaryIndex = 0;
	constexpr int32 SnapshotSelectiveIndex = 1;
	constexpr int32 SnapshotPressureIndex = 2;
	constexpr int32 SnapshotObstacleIndex = 3;
	constexpr int32 SnapshotVelocityXIndex = 4;
	constexpr int32 SnapshotVelocityYIndex = 5;
	constexpr int32 SnapshotSlopeXIndex = 6;
	constexpr int32 SnapshotSlopeYIndex = 7;

	enum class ETerrainSlopeSource : uint8
	{
		None,
		Uniform,
		Elevation
	};

	struct FTDFluidRowScenario
	{
		const TCHAR* Name;
		ETDFluidPresetKind PresetKind;
		int32 Resolution;
		FVector2D ExtentCm;
		bool bHasObstacles;
		bool bSlidesWindow;
		ETerrainSlopeSource SlopeSource;
		bool bDisablesOptionalForces;
		bool bUsesPlaneUpDirection;
		int32 StepCount;
	};

	const FTDFluidRowScenario RowScenarios[] = {
		{ TEXT("WaterOddResolutionObstaclesSlidingUniformSlope"), ETDFluidPresetKind::Water, 100, FVector2D(1000.0, 800.0), true, true, ETerrainSlopeSource::Uniform, false, false, 40 },
		{ TEXT("MudSquareElevationSlope"), ETDFluidPresetKind::Mud, 64, FVector2D(900.0, 900.0), false, false, ETerrainSlopeSource::Elevation, false, false, 48 },
		{ TEXT("FireObstaclesSlidingElevationSlope"), ETDFluidPresetKind::Fire, 128, FVector2D(1000.0, 800.0), true, true, ETerrainSlopeSource::Elevation, false, false, 40 },
		{ TEXT("BloodTallObstaclesWithoutOptionalForces"), ETDFluidPresetKind::Blood, 48, FVector2D(600.0, 1000.0), true, false, ETerrainSlopeSource::None, true, false, 40 },
		{ TEXT("WaterOpenField192"), ETDFluidPresetKind::Water, 192, FVector2D(1200.0, 1200.0), false, false, ETerrainSlopeSource::None, false, false, 24 },
		{ TEXT("SmokePlaneUpObstacles"), ETDFluidPresetKind::Smoke, 128, FVector2D(1000.0, 800.0), true, false, ETerrainSlopeSource::None, false, true, 32 },
	};

	struct FRowParallelVariableScope
	{
		FRowParallelVariableScope()
			: Variable(IConsoleManager::Get().FindConsoleVariable(RowParallelVariableName))
		{
			if (Variable)
			{
				PreviousValue = Variable->GetInt();
			}
		}

		~FRowParallelVariableScope()
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

	struct FTDFluidSolverSnapshot
	{
		uint32 StateChecksum = 0;
		TArray<double> CellValues;
		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
	};

	FTDFluidSolverSnapshot CaptureSnapshot(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		FTDFluidSolverSnapshot Snapshot;
		Snapshot.CellValues.Reserve(Resolution * Resolution * SnapshotValuesPerCell);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample CellSample = Solver.SampleAtCell(CellX, CellY);
				Snapshot.CellValues.Add(CellSample.PrimaryDensity);
				Snapshot.CellValues.Add(CellSample.SelectiveDensity);
				Snapshot.CellValues.Add(CellSample.Pressure);
				Snapshot.CellValues.Add(CellSample.ObstacleSdf);
				Snapshot.CellValues.Add(CellSample.Velocity.X);
				Snapshot.CellValues.Add(CellSample.Velocity.Y);
				Snapshot.CellValues.Add(CellSample.TerrainSlope.X);
				Snapshot.CellValues.Add(CellSample.TerrainSlope.Y);
			}
		}
		Solver.PackOutputPixels(Snapshot.DensityPressurePixels, Snapshot.VelocityPixels);
		Snapshot.StateChecksum = Solver.ComputeStateChecksum();
		return Snapshot;
	}

	int32 FindFirstDifferentValueIndex(const TArray<double>& Expected, const TArray<double>& Actual)
	{
		if (Expected.Num() != Actual.Num())
		{
			return 0;
		}

		for (int32 ValueIndex = 0; ValueIndex < Expected.Num(); ++ValueIndex)
		{
			if (FMemory::Memcmp(&Expected[ValueIndex], &Actual[ValueIndex], sizeof(double)) != 0)
			{
				return ValueIndex;
			}
		}
		return INDEX_NONE;
	}

	bool ArePixelArraysBitIdentical(const TArray<FFloat16Color>& Expected, const TArray<FFloat16Color>& Actual)
	{
		return Expected.Num() == Actual.Num() && FMemory::Memcmp(Expected.GetData(), Actual.GetData(), Expected.Num() * sizeof(FFloat16Color)) == 0;
	}

	void ExpectIdenticalSnapshots(FAutomationTestBase& Test, const FString& Label, const FTDFluidSolverSnapshot& Expected, const FTDFluidSolverSnapshot& Actual)
	{
		Test.TestEqual(*(Label + TEXT(" state checksum")), Actual.StateChecksum, Expected.StateChecksum);
		Test.TestEqual(*(Label + TEXT(" first differing cell value index (-1 = none)")), FindFirstDifferentValueIndex(Expected.CellValues, Actual.CellValues), static_cast<int32>(INDEX_NONE));
		Test.TestTrue(*(Label + TEXT(" density and pressure pixels are bit-identical")), ArePixelArraysBitIdentical(Expected.DensityPressurePixels, Actual.DensityPressurePixels));
		Test.TestTrue(*(Label + TEXT(" velocity pixels are bit-identical")), ArePixelArraysBitIdentical(Expected.VelocityPixels, Actual.VelocityPixels));
	}

	TArray<FTDFluidSdfObstacle> MakeObstacles()
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

	float EvaluateElevationCm(const FVector2D& WorldPos)
	{
		return static_cast<float>(40.0 * FMath::Sin(WorldPos.X * 0.004) + 25.0 * FMath::Cos(WorldPos.Y * 0.006) + 0.03 * WorldPos.X);
	}

	void RefreshElevation(FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, EvaluateElevationCm(Solver.CellToWorldCenter(CellX, CellY)));
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
	}

	TArray<FTDFluidBrushStamp> MakeStamps(const FVector2D& OrbitCenter, float OrbitRadiusCm, int32 Step)
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

		if (Step % RadialPushStepInterval == 0)
		{
			FTDFluidBrushStamp RadialStamp;
			RadialStamp.WorldLocation = FVector(OrbitCenter.X + OrbitRadiusCm * 0.2, OrbitCenter.Y - OrbitRadiusCm * 0.3, 0.0);
			RadialStamp.RadiusCm = 100.0f;
			RadialStamp.DensityStrength = 0.8f;
			RadialStamp.VelocityStrength = 0.0f;
			RadialStamp.RadialVelocityCmPerSec = 500.0f;
			RadialStamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			Stamps.Add(RadialStamp);
		}

		return Stamps;
	}

	FTDFluidSimulationConfig BuildScenarioConfig(const FTDFluidRowScenario& Scenario)
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
		return Config;
	}

	FTDFluidSolverSnapshot RunRowScenario(const FTDFluidRowScenario& Scenario)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(BuildScenarioConfig(Scenario), FVector2D::ZeroVector);
		if (Scenario.bUsesPlaneUpDirection)
		{
			Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));
		}

		const TArray<FTDFluidSdfObstacle> Obstacles = Scenario.bHasObstacles ? MakeObstacles() : TArray<FTDFluidSdfObstacle>();
		if (Scenario.bHasObstacles)
		{
			Solver.RasterizeSdfObstacles(Obstacles);
		}
		if (Scenario.SlopeSource == ETerrainSlopeSource::Uniform)
		{
			Solver.SetUniformTerrainSlope(FVector2D(0.12, -0.07));
		}

		const float OrbitRadiusCm = static_cast<float>(FMath::Min(Scenario.ExtentCm.X, Scenario.ExtentCm.Y) * 0.3);
		for (int32 Step = 0; Step < Scenario.StepCount; ++Step)
		{
			const float DeltaTime = StepSecondsCycle[Step % StepSecondsCycleLength];
			const FVector2D DesiredCenter = Scenario.bSlidesWindow ? FVector2D(Step * 4.3, Step * -2.7) : FVector2D::ZeroVector;

			if (Scenario.bSlidesWindow && Solver.ShiftOriginForSlidingWindow(DesiredCenter) && Scenario.bHasObstacles)
			{
				Solver.RasterizeSdfObstacles(Obstacles);
			}
			if (Scenario.SlopeSource == ETerrainSlopeSource::Elevation && Step % ElevationRefreshStepInterval == 0)
			{
				RefreshElevation(Solver);
			}

			Solver.ApplyBrushStamps(MakeStamps(DesiredCenter, OrbitRadiusCm, Step), DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}

		return CaptureSnapshot(Solver);
	}

	bool IsSnapshotActive(const FTDFluidSolverSnapshot& Snapshot)
	{
		double MaximumDensity = 0.0;
		double MaximumSpeedSquared = 0.0;
		for (int32 ValueIndex = 0; ValueIndex + SnapshotValuesPerCell <= Snapshot.CellValues.Num(); ValueIndex += SnapshotValuesPerCell)
		{
			const double VelocityX = Snapshot.CellValues[ValueIndex + SnapshotVelocityXIndex];
			const double VelocityY = Snapshot.CellValues[ValueIndex + SnapshotVelocityYIndex];
			MaximumDensity = FMath::Max(MaximumDensity, Snapshot.CellValues[ValueIndex + SnapshotPrimaryIndex]);
			MaximumSpeedSquared = FMath::Max(MaximumSpeedSquared, VelocityX * VelocityX + VelocityY * VelocityY);
		}
		return MaximumDensity > 0.05 && MaximumSpeedSquared > 1.0;
	}

	struct FShiftCase
	{
		int32 ShiftCellsX;
		int32 ShiftCellsY;
	};

	constexpr int32 ShiftResolution = 64;
	const FVector2D ShiftExtentCm = FVector2D(1000.0, 800.0);
	constexpr float ShiftCellSizeXCm = 1000.0f / 64.0f;
	constexpr float ShiftCellSizeYCm = 800.0f / 64.0f;

	const FShiftCase ShiftCases[] = {
		{ 3, 0 },
		{ 0, -2 },
		{ -5, 7 },
		{ 1, 1 },
		{ 0, 0 },
		{ -63, 0 },
		{ 70, 0 },
		{ 0, 70 },
		{ -2, -3 },
		{ 63, -63 },
		{ 0, 5 },
		{ -64, 64 },
	};
	constexpr int32 ShiftCaseCount = UE_ARRAY_COUNT(ShiftCases);

	const uint32 ExpectedPostShiftStateChecksums[ShiftCaseCount] = {
		0xD6A8EF03u, 0x295118E8u, 0xBE7F545Fu, 0xE4AFE104u, 0xBFB0CF52u, 0x72A01EF4u,
		0xEC069C33u, 0x6AA27024u, 0x75316D9Fu, 0x6B6564F5u, 0x47357393u, 0x5EC85137u,
	};
	const uint32 ExpectedFinalStateChecksum = 0x15CC10EEu;

	int32 ResolveSourceCellIndex(int32 NewX, int32 NewY, int32 ShiftCellsX, int32 ShiftCellsY)
	{
		const int32 OldX = NewX + ShiftCellsX;
		const int32 OldY = NewY + ShiftCellsY;
		if (OldX < 0 || OldX >= ShiftResolution || OldY < 0 || OldY >= ShiftResolution)
		{
			return INDEX_NONE;
		}
		return OldY * ShiftResolution + OldX;
	}

	FTDFluidSolverSnapshot BuildCopyShiftedSnapshot(const FTDFluidSolverSnapshot& Before, int32 ShiftCellsX, int32 ShiftCellsY)
	{
		constexpr int32 ShiftedValueIndices[] = {
			SnapshotPrimaryIndex, SnapshotSelectiveIndex, SnapshotPressureIndex,
			SnapshotVelocityXIndex, SnapshotVelocityYIndex, SnapshotSlopeXIndex, SnapshotSlopeYIndex,
		};

		FTDFluidSolverSnapshot Expected;
		Expected.CellValues.Init(0.0, Before.CellValues.Num());
		for (int32 NewY = 0; NewY < ShiftResolution; ++NewY)
		{
			for (int32 NewX = 0; NewX < ShiftResolution; ++NewX)
			{
				const int32 NewCellIndex = NewY * ShiftResolution + NewX;
				Expected.CellValues[NewCellIndex * SnapshotValuesPerCell + SnapshotObstacleIndex] = Before.CellValues[NewCellIndex * SnapshotValuesPerCell + SnapshotObstacleIndex];

				const int32 OldCellIndex = ResolveSourceCellIndex(NewX, NewY, ShiftCellsX, ShiftCellsY);
				if (OldCellIndex == INDEX_NONE)
				{
					continue;
				}

				for (const int32 ValueIndex : ShiftedValueIndices)
				{
					Expected.CellValues[NewCellIndex * SnapshotValuesPerCell + ValueIndex] = Before.CellValues[OldCellIndex * SnapshotValuesPerCell + ValueIndex];
				}
			}
		}
		return Expected;
	}

	TArray<float> BuildCopyShiftedElevation(const TArray<float>& Before, int32 ShiftCellsX, int32 ShiftCellsY)
	{
		TArray<float> Shifted;
		Shifted.Init(0.0f, Before.Num());
		for (int32 NewY = 0; NewY < ShiftResolution; ++NewY)
		{
			for (int32 NewX = 0; NewX < ShiftResolution; ++NewX)
			{
				const int32 OldCellIndex = ResolveSourceCellIndex(NewX, NewY, ShiftCellsX, ShiftCellsY);
				if (OldCellIndex != INDEX_NONE)
				{
					Shifted[NewY * ShiftResolution + NewX] = Before[OldCellIndex];
				}
			}
		}
		return Shifted;
	}

	FTDFluidSimulationConfig BuildShiftConfig()
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Water);
		Config.GridResolution = ShiftResolution;
		Config.SimExtentCm = ShiftExtentCm;
		return Config;
	}

	void AdvanceShiftScenario(FTDFluidGrid2DSolver& Solver, int32 StepCount, int32 FirstStep)
	{
		for (int32 Step = FirstStep; Step < FirstStep + StepCount; ++Step)
		{
			const float DeltaTime = StepSecondsCycle[Step % StepSecondsCycleLength];
			Solver.ApplyBrushStamps(MakeStamps(Solver.GetCenterWorld(), 250.0f, Step), DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}
	}

	struct FStepCostResult
	{
		double AverageMilliseconds = 0.0;
	};

	FStepCostResult MeasureSolverStepCost(int32 Resolution)
	{
		constexpr int32 WarmupStepCount = 20;
		constexpr int32 MeasuredStepCount = 100;
		constexpr float StepSeconds = 1.0f / 60.0f;

		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Water);
		Config.GridResolution = Resolution;

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		double TotalSeconds = 0.0;
		for (int32 Step = 0; Step < WarmupStepCount + MeasuredStepCount; ++Step)
		{
			Solver.ApplyBrushStamps(MakeStamps(FVector2D::ZeroVector, 700.0f, Step), StepSeconds);
			const double StartSeconds = FPlatformTime::Seconds();
			Solver.StepSimulation(StepSeconds);
			const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			if (Step >= WarmupStepCount)
			{
				TotalSeconds += ElapsedSeconds;
			}
		}

		FStepCostResult Result;
		Result.AverageMilliseconds = TotalSeconds * 1000.0 / MeasuredStepCount;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverRowParallelEqualsSerialTest,
	"TDGame.FluidFX.Solver.RowParallelEqualsSerial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverRowParallelEqualsSerialTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverParallelTestPrivate;

	FRowParallelVariableScope RowParallelVariable;
	if (!TestNotNull(TEXT("TD.FluidFX.SolverRowParallel console variable exists"), RowParallelVariable.Variable))
	{
		return false;
	}
	TestEqual(TEXT("TD.FluidFX.SolverRowParallel defaults to 1"), RowParallelVariable.PreviousValue, 1);

	for (const FTDFluidRowScenario& Scenario : RowScenarios)
	{
		RowParallelVariable.SetValue(0);
		const FTDFluidSolverSnapshot SerialSnapshot = RunRowScenario(Scenario);
		RowParallelVariable.SetValue(1);
		const FTDFluidSolverSnapshot ParallelSnapshot = RunRowScenario(Scenario);
		const FTDFluidSolverSnapshot RepeatedParallelSnapshot = RunRowScenario(Scenario);

		AddInfo(FString::Printf(TEXT("TDFluidRowParallel: scenario=%s state=0x%08X"), Scenario.Name, SerialSnapshot.StateChecksum));

		TestTrue(*FString::Printf(TEXT("%s carries density and velocity after %d steps"), Scenario.Name, Scenario.StepCount), IsSnapshotActive(SerialSnapshot));
		ExpectIdenticalSnapshots(*this, FString::Printf(TEXT("%s parallel equals serial:"), Scenario.Name), SerialSnapshot, ParallelSnapshot);
		ExpectIdenticalSnapshots(*this, FString::Printf(TEXT("%s repeated parallel equals serial:"), Scenario.Name), SerialSnapshot, RepeatedParallelSnapshot);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverInPlaceShiftEqualsCopyShiftTest,
	"TDGame.FluidFX.Solver.InPlaceShiftEqualsCopyShift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverInPlaceShiftEqualsCopyShiftTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverParallelTestPrivate;

	const FTDFluidSimulationConfig Config = BuildShiftConfig();
	const TArray<FTDFluidSdfObstacle> Obstacles = MakeObstacles();

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	Solver.RasterizeSdfObstacles(Obstacles);

	TArray<float> ReferenceElevation;
	ReferenceElevation.Init(0.0f, ShiftResolution * ShiftResolution);
	for (int32 CellY = 0; CellY < ShiftResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < ShiftResolution; ++CellX)
		{
			const float ElevationCm = EvaluateElevationCm(Solver.CellToWorldCenter(CellX, CellY));
			ReferenceElevation[CellY * ShiftResolution + CellX] = ElevationCm;
			Solver.SetTerrainElevationAtCell(CellX, CellY, ElevationCm);
		}
	}
	Solver.ComputeTerrainSlopeFromElevation();

	int32 NextStep = 0;
	constexpr int32 StepsBetweenShifts = 3;
	AdvanceShiftScenario(Solver, StepsBetweenShifts, NextStep);
	NextStep += StepsBetweenShifts;

	for (int32 CaseIndex = 0; CaseIndex < ShiftCaseCount; ++CaseIndex)
	{
		const FShiftCase& ShiftCase = ShiftCases[CaseIndex];
		const FString CaseLabel = FString::Printf(TEXT("Shift case %d (%d, %d):"), CaseIndex, ShiftCase.ShiftCellsX, ShiftCase.ShiftCellsY);
		const bool bIsNoOpShift = ShiftCase.ShiftCellsX == 0 && ShiftCase.ShiftCellsY == 0;

		const FTDFluidSolverSnapshot Before = CaptureSnapshot(Solver);
		const FVector2D CenterBefore = Solver.GetCenterWorld();
		const FVector2D DesiredCenter = bIsNoOpShift
			? CenterBefore + FVector2D(ShiftCellSizeXCm * 0.2, -ShiftCellSizeYCm * 0.2)
			: CenterBefore + FVector2D(ShiftCase.ShiftCellsX * ShiftCellSizeXCm, ShiftCase.ShiftCellsY * ShiftCellSizeYCm);

		const bool bDidShift = Solver.ShiftOriginForSlidingWindow(DesiredCenter);
		TestEqual(*(CaseLabel + TEXT(" reports whether the window moved")), bDidShift, !bIsNoOpShift);

		const FVector2D ExpectedCenter = CenterBefore + FVector2D(ShiftCase.ShiftCellsX * ShiftCellSizeXCm, ShiftCase.ShiftCellsY * ShiftCellSizeYCm);
		TestTrue(*(CaseLabel + TEXT(" moves the center by whole cells")), Solver.GetCenterWorld().Equals(ExpectedCenter, 0.001));

		const FTDFluidSolverSnapshot After = CaptureSnapshot(Solver);
		const FTDFluidSolverSnapshot ExpectedAfter = BuildCopyShiftedSnapshot(Before, ShiftCase.ShiftCellsX, ShiftCase.ShiftCellsY);
		TestEqual(*(CaseLabel + TEXT(" first cell value that differs from the copy shift (-1 = none)")), FindFirstDifferentValueIndex(ExpectedAfter.CellValues, After.CellValues), static_cast<int32>(INDEX_NONE));

		const uint32 PostShiftChecksum = Solver.ComputeStateChecksum();
		AddInfo(FString::Printf(TEXT("TDFluidShiftGolden: index=%d state=0x%08X"), CaseIndex, PostShiftChecksum));
		if (bIsNoOpShift)
		{
			TestEqual(*(CaseLabel + TEXT(" leaves the state checksum unchanged")), PostShiftChecksum, Before.StateChecksum);
		}
		TestEqual(*(CaseLabel + TEXT(" state checksum matches the copy shift golden value")), PostShiftChecksum, ExpectedPostShiftStateChecksums[CaseIndex]);

		ReferenceElevation = BuildCopyShiftedElevation(ReferenceElevation, ShiftCase.ShiftCellsX, ShiftCase.ShiftCellsY);
		FTDFluidGrid2DSolver ElevationReference;
		ElevationReference.Initialize(Config, FVector2D::ZeroVector);
		for (int32 CellY = 0; CellY < ShiftResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < ShiftResolution; ++CellX)
			{
				ElevationReference.SetTerrainElevationAtCell(CellX, CellY, ReferenceElevation[CellY * ShiftResolution + CellX]);
			}
		}
		ElevationReference.ComputeTerrainSlopeFromElevation();
		Solver.ComputeTerrainSlopeFromElevation();

		const FTDFluidSolverSnapshot SolverSlopes = CaptureSnapshot(Solver);
		const FTDFluidSolverSnapshot ReferenceSlopes = CaptureSnapshot(ElevationReference);
		int32 FirstDifferentSlopeCell = INDEX_NONE;
		for (int32 CellIndex = 0; CellIndex < ShiftResolution * ShiftResolution && FirstDifferentSlopeCell == INDEX_NONE; ++CellIndex)
		{
			const int32 ValueIndex = CellIndex * SnapshotValuesPerCell;
			const bool bSlopeXMatches = FMemory::Memcmp(&SolverSlopes.CellValues[ValueIndex + SnapshotSlopeXIndex], &ReferenceSlopes.CellValues[ValueIndex + SnapshotSlopeXIndex], sizeof(double)) == 0;
			const bool bSlopeYMatches = FMemory::Memcmp(&SolverSlopes.CellValues[ValueIndex + SnapshotSlopeYIndex], &ReferenceSlopes.CellValues[ValueIndex + SnapshotSlopeYIndex], sizeof(double)) == 0;
			if (!bSlopeXMatches || !bSlopeYMatches)
			{
				FirstDifferentSlopeCell = CellIndex;
			}
		}
		TestEqual(*(CaseLabel + TEXT(" shifted terrain elevation gives the copy-shifted slope (first different cell, -1 = none)")), FirstDifferentSlopeCell, static_cast<int32>(INDEX_NONE));

		Solver.RasterizeSdfObstacles(Obstacles);
		AdvanceShiftScenario(Solver, StepsBetweenShifts, NextStep);
		NextStep += StepsBetweenShifts;
	}

	const uint32 FinalChecksum = Solver.ComputeStateChecksum();
	AddInfo(FString::Printf(TEXT("TDFluidShiftGolden: final state=0x%08X"), FinalChecksum));
	TestEqual(TEXT("The state after all shifts and steps matches the copy shift golden value"), FinalChecksum, ExpectedFinalStateChecksum);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverRowParallelStepCostTest,
	"TDGame.FluidFX.Solver.RowParallelStepCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverRowParallelStepCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverParallelTestPrivate;

	FRowParallelVariableScope RowParallelVariable;
	if (!TestNotNull(TEXT("TD.FluidFX.SolverRowParallel console variable exists"), RowParallelVariable.Variable))
	{
		return false;
	}

	constexpr int32 Resolutions[] = { 64, 128, 192, 256 };
	for (const int32 Resolution : Resolutions)
	{
		for (const int32 RowParallelValue : { 0, 1 })
		{
			RowParallelVariable.SetValue(RowParallelValue);
			const FStepCostResult Cost = MeasureSolverStepCost(Resolution);
			AddInfo(FString::Printf(TEXT("TDFluidRowParallelBench: resolution=%d row_parallel=%d avg_step_ms=%.4f"), Resolution, RowParallelValue, Cost.AverageMilliseconds));
		}
	}

	return true;
}

#endif
