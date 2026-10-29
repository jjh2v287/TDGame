#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace TDFluidViscousSolverTestPrivate
{
	constexpr int32 ViscousGridResolution = 64;
	constexpr int32 ViscousMetricsResolution = 256;
	constexpr float ViscousSeedRadiusCm = 40.0f;
	constexpr int32 ViscousLeftSeedCellX = 20;
	constexpr int32 ViscousRightSeedCellX = 44;
	constexpr int32 ViscousSeedCellY = 32;

	FTDFluidSimulationConfig MakeViscousQuietConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = ViscousGridResolution;
		Config.DensityDissipation = 0.0f;
		Config.VelocityDamping = 0.0f;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.BuoyancyCmPerSec2 = 0.0f;
		return Config;
	}

	FTDFluidSimulationConfig MakeViscousDenseConfig(float GrowthPerSec, float Ceiling, float Threshold)
	{
		FTDFluidSimulationConfig Config = MakeViscousQuietConfig();
		Config.DensityAccumulationPerSec = GrowthPerSec;
		Config.DenseDensityCeiling = Ceiling;
		Config.DenseGrowthThreshold = Threshold;
		return Config;
	}

	FTDFluidBrushStamp MakeViscousSeedStamp(const FTDFluidGrid2DSolver& Solver, int32 CellX, int32 CellY, float Strength, ETDFluidBrushChannel Channel)
	{
		const FVector2D CellCenter = Solver.CellToWorldCenter(CellX, CellY);
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(CellCenter.X, CellCenter.Y, 0.0);
		Stamp.RadiusCm = ViscousSeedRadiusCm;
		Stamp.DensityStrength = Strength;
		Stamp.VelocityStrength = 0.0f;
		Stamp.Channel = Channel;
		return Stamp;
	}

	float RunViscousSeed(const FTDFluidSimulationConfig& Config, float SeedStrength, float StepSeconds, int32 StepCount)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.ApplyBrushStamps({ MakeViscousSeedStamp(Solver, ViscousLeftSeedCellX, ViscousSeedCellY, SeedStrength, ETDFluidBrushChannel::ChannelR_Mud) }, StepSeconds);
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			Solver.StepSimulation(StepSeconds);
		}
		return Solver.SampleAtCell(ViscousLeftSeedCellX, ViscousSeedCellY).PrimaryDensity;
	}

	FTDFluidBrushStamp MakeViscousOrbitingStamp(int32 Step)
	{
		const double Angle = Step * 0.11;
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(FMath::Cos(Angle) * 600.0, FMath::Sin(Angle) * 600.0, 0.0);
		Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 500.0;
		Stamp.RadiusCm = 250.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelG_Blood;
		return Stamp;
	}

	uint32 RunViscousOrbitChecksum(const FTDFluidSimulationConfig& Config)
	{
		constexpr float StepSeconds = 1.0f / 60.0f;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		for (int32 Step = 0; Step < 90; ++Step)
		{
			Solver.ApplyBrushStamps({ MakeViscousOrbitingStamp(Step) }, StepSeconds);
			Solver.StepSimulation(StepSeconds);
		}
		return Solver.ComputeStateChecksum();
	}

	float MeasureViscousMaxSpeed(const FTDFluidGrid2DSolver& Solver)
	{
		float MaxSpeed = 0.0f;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				MaxSpeed = FMath::Max(MaxSpeed, static_cast<float>(Solver.SampleAtCell(CellX, CellY).Velocity.Size()));
			}
		}
		return MaxSpeed;
	}

	int32 CountViscousCellsAtLeast(const FTDFluidGrid2DSolver& Solver, float Threshold)
	{
		int32 CellCount = 0;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				CellCount += Solver.SampleAtCell(CellX, CellY).PrimaryDensity >= Threshold ? 1 : 0;
			}
		}
		return CellCount;
	}

	struct FTDFluidViscousStreakShape
	{
		int32 LengthCells = 0;
		int32 MaxWidthCells = 0;
		int32 TipWidthCells = 0;
	};

	FTDFluidViscousStreakShape MeasureViscousLargestStreak(const FTDFluidGrid2DSolver& Solver, float Threshold)
	{
		const int32 Resolution = Solver.GetResolution();
		TArray<int32> ComponentByCell;
		ComponentByCell.Init(INDEX_NONE, Resolution * Resolution);
		TArray<int32> LargestCells;
		TArray<int32> CurrentCells;
		TArray<int32> Frontier;
		for (int32 SeedIndex = 0; SeedIndex < Resolution * Resolution; ++SeedIndex)
		{
			if (ComponentByCell[SeedIndex] != INDEX_NONE || Solver.SampleAtCell(SeedIndex % Resolution, SeedIndex / Resolution).PrimaryDensity < Threshold)
			{
				continue;
			}

			CurrentCells.Reset();
			Frontier.Reset();
			Frontier.Add(SeedIndex);
			ComponentByCell[SeedIndex] = SeedIndex;
			while (!Frontier.IsEmpty())
			{
				const int32 CellIndex = Frontier.Pop();
				CurrentCells.Add(CellIndex);
				const int32 CellX = CellIndex % Resolution;
				const int32 CellY = CellIndex / Resolution;
				const FIntPoint Neighbors[] = { { CellX + 1, CellY }, { CellX - 1, CellY }, { CellX, CellY + 1 }, { CellX, CellY - 1 } };
				for (const FIntPoint& Neighbor : Neighbors)
				{
					if (Neighbor.X < 0 || Neighbor.Y < 0 || Neighbor.X >= Resolution || Neighbor.Y >= Resolution)
					{
						continue;
					}
					const int32 NeighborIndex = Neighbor.Y * Resolution + Neighbor.X;
					if (ComponentByCell[NeighborIndex] != INDEX_NONE || Solver.SampleAtCell(Neighbor.X, Neighbor.Y).PrimaryDensity < Threshold)
					{
						continue;
					}
					ComponentByCell[NeighborIndex] = SeedIndex;
					Frontier.Add(NeighborIndex);
				}
			}
			if (CurrentCells.Num() > LargestCells.Num())
			{
				LargestCells = CurrentCells;
			}
		}

		FTDFluidViscousStreakShape Shape;
		if (LargestCells.IsEmpty())
		{
			return Shape;
		}

		TArray<int32> WidthByColumn;
		WidthByColumn.Init(0, Resolution);
		int32 MinColumn = Resolution;
		int32 MaxColumn = -1;
		for (const int32 CellIndex : LargestCells)
		{
			const int32 Column = CellIndex % Resolution;
			++WidthByColumn[Column];
			MinColumn = FMath::Min(MinColumn, Column);
			MaxColumn = FMath::Max(MaxColumn, Column);
		}
		Shape.LengthCells = MaxColumn - MinColumn + 1;
		const int32 TipColumnCount = FMath::Max(1, FMath::CeilToInt(0.1f * Shape.LengthCells));
		for (int32 Column = MinColumn; Column <= MaxColumn; ++Column)
		{
			Shape.MaxWidthCells = FMath::Max(Shape.MaxWidthCells, WidthByColumn[Column]);
			if (Column > MaxColumn - TipColumnCount)
			{
				Shape.TipWidthCells = FMath::Max(Shape.TipWidthCells, WidthByColumn[Column]);
			}
		}
		return Shape;
	}

	FTDFluidSimulationConfig MakeViscousMetricsConfig(ETDFluidPresetKind Kind)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		Config.GridResolution = ViscousMetricsResolution;
		Config.SolverBackend = ETDFluidSolverBackend::Cpu;
		return Config;
	}

	void WriteViscousConfigFields(FJsonObject& Report, const FTDFluidSimulationConfig& Config)
	{
		Report.SetNumberField(TEXT("grid_resolution"), Config.GridResolution);
		Report.SetNumberField(TEXT("extent_cm"), Config.SimExtentCm.X);
		Report.SetNumberField(TEXT("density_accumulation_per_sec"), Config.DensityAccumulationPerSec);
		Report.SetNumberField(TEXT("dense_density_ceiling"), Config.DenseDensityCeiling);
		Report.SetNumberField(TEXT("dense_growth_threshold"), Config.DenseGrowthThreshold);
		Report.SetNumberField(TEXT("flow_noise_speed_mask_reference_cm_per_sec"), Config.FlowNoiseSpeedMaskReferenceCmPerSec);
		Report.SetNumberField(TEXT("advection_speed_scale"), Config.AdvectionSpeedScale);
		Report.SetNumberField(TEXT("density_dissipation"), Config.DensityDissipation);
		Report.SetNumberField(TEXT("velocity_damping"), Config.VelocityDamping);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousDenseGrowthIsBistableTest,
	"TDGame.FluidFX.Viscous.Dense.GrowthIsBistable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousDenseGrowthIsBistableTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidViscousSolverTestPrivate;

	constexpr float StepSeconds = 1.0f / 60.0f;
	const FTDFluidSimulationConfig DenseConfig = MakeViscousDenseConfig(2.0f, 3.0f, 0.15f);
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(DenseConfig, FVector2D::ZeroVector);
	TestTrue(TEXT("dense density is active"), Solver.UsesDenseDensity());
	Solver.ApplyBrushStamps({
		MakeViscousSeedStamp(Solver, ViscousLeftSeedCellX, ViscousSeedCellY, 0.5f, ETDFluidBrushChannel::ChannelR_Mud),
		MakeViscousSeedStamp(Solver, ViscousRightSeedCellX, ViscousSeedCellY, 0.05f, ETDFluidBrushChannel::ChannelR_Mud) }, StepSeconds);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Solver.StepSimulation(StepSeconds);
	}

	const float GrownDensity = Solver.SampleAtCell(ViscousLeftSeedCellX, ViscousSeedCellY).PrimaryDensity;
	const float QuietDensity = Solver.SampleAtCell(ViscousRightSeedCellX, ViscousSeedCellY).PrimaryDensity;
	TestTrue(*FString::Printf(TEXT("seed above threshold grows to ceiling (%.7f)"), GrownDensity), FMath::Abs(GrownDensity - 3.0f) <= 1.0e-5f);
	TestTrue(*FString::Printf(TEXT("seed below threshold stays (%.7f)"), QuietDensity), FMath::Abs(QuietDensity - 0.05f) <= 1.0e-6f);

	const float LegacyDensity = RunViscousSeed(MakeViscousQuietConfig(), 0.5f, StepSeconds, 60);
	TestTrue(*FString::Printf(TEXT("legacy config does not grow (%.7f)"), LegacyDensity), LegacyDensity <= 1.0f && GrownDensity > LegacyDensity + 1.0f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousDenseFrameRateIndependentTest,
	"TDGame.FluidFX.Viscous.Dense.FrameRateIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousDenseFrameRateIndependentTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidViscousSolverTestPrivate;

	FTDFluidSimulationConfig Config = MakeViscousDenseConfig(0.5f, 8.0f, 0.1f);
	Config.DensityDissipation = 0.0f;
	const float ThirtyHzDensity = RunViscousSeed(Config, 0.5f, 1.0f / 30.0f, 30);
	const float HundredTwentyHzDensity = RunViscousSeed(Config, 0.5f, 1.0f / 120.0f, 120);
	TestTrue(*FString::Printf(TEXT("30 Hz %.7f vs 120 Hz %.7f"), ThirtyHzDensity, HundredTwentyHzDensity), FMath::Abs(ThirtyHzDensity - HundredTwentyHzDensity) <= 1.0e-4f);
	TestTrue(*FString::Printf(TEXT("density grew above seed (%.7f)"), ThirtyHzDensity), ThirtyHzDensity > 0.75f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousDenseStampCeilingAndEncodingTest,
	"TDGame.FluidFX.Viscous.Dense.StampCeilingAndEncoding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousDenseStampCeilingAndEncodingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidViscousSolverTestPrivate;

	constexpr float StampStrength = 2.5f;
	constexpr float StepSeconds = 1.0f / 60.0f;
	const FTDFluidSimulationConfig DenseConfig = MakeViscousDenseConfig(0.0f, 3.0f, 0.1f);
	const FTDFluidSimulationConfig LegacyConfig = MakeViscousQuietConfig();
	const int32 CellIndex = ViscousSeedCellY * ViscousGridResolution + ViscousLeftSeedCellX;

	FTDFluidGrid2DSolver DenseSolver;
	DenseSolver.Initialize(DenseConfig, FVector2D::ZeroVector);
	TestTrue(TEXT("ceiling alone activates dense density"), DenseSolver.UsesDenseDensity());
	TestEqual(TEXT("dense stamp ceiling"), DenseSolver.GetStampDensityCeiling(), 3.0f);
	DenseSolver.ApplyBrushStamps({ MakeViscousSeedStamp(DenseSolver, ViscousLeftSeedCellX, ViscousSeedCellY, StampStrength, ETDFluidBrushChannel::ChannelG_Blood) }, StepSeconds);

	FTDFluidGrid2DSolver LegacySolver;
	LegacySolver.Initialize(LegacyConfig, FVector2D::ZeroVector);
	TestFalse(TEXT("legacy config is not dense"), LegacySolver.UsesDenseDensity());
	TestEqual(TEXT("legacy stamp ceiling"), LegacySolver.GetStampDensityCeiling(), 1.0f);
	LegacySolver.ApplyBrushStamps({ MakeViscousSeedStamp(LegacySolver, ViscousLeftSeedCellX, ViscousSeedCellY, StampStrength, ETDFluidBrushChannel::ChannelG_Blood) }, StepSeconds);

	const FTDFluidCellSample DenseSample = DenseSolver.SampleAtCell(ViscousLeftSeedCellX, ViscousSeedCellY);
	const FTDFluidCellSample LegacySample = LegacySolver.SampleAtCell(ViscousLeftSeedCellX, ViscousSeedCellY);
	TestTrue(*FString::Printf(TEXT("dense primary keeps stamp strength (%.6f)"), DenseSample.PrimaryDensity), FMath::Abs(DenseSample.PrimaryDensity - StampStrength) <= 1.0e-5f);
	TestTrue(*FString::Printf(TEXT("dense selective keeps stamp strength (%.6f)"), DenseSample.SelectiveDensity), FMath::Abs(DenseSample.SelectiveDensity - StampStrength) <= 1.0e-5f);
	TestEqual(TEXT("legacy primary clamps to one"), LegacySample.PrimaryDensity, 1.0f);

	TArray<FFloat16Color> DensePixels;
	TArray<FFloat16Color> DenseVelocityPixels;
	DenseSolver.PackOutputPixels(DensePixels, DenseVelocityPixels);
	TArray<FFloat16Color> LegacyPixels;
	TArray<FFloat16Color> LegacyVelocityPixels;
	LegacySolver.PackOutputPixels(LegacyPixels, LegacyVelocityPixels);
	const float ExpectedEncoded = StampStrength / 3.0f;
	TestTrue(*FString::Printf(TEXT("dense R encodes D/C (%.5f)"), DensePixels[CellIndex].R.GetFloat()), FMath::Abs(DensePixels[CellIndex].R.GetFloat() - ExpectedEncoded) <= 1.0e-3f);
	TestTrue(*FString::Printf(TEXT("dense B encodes D/C (%.5f)"), DensePixels[CellIndex].B.GetFloat()), FMath::Abs(DensePixels[CellIndex].B.GetFloat() - ExpectedEncoded) <= 1.0e-3f);
	TestEqual(TEXT("legacy R saturates"), LegacyPixels[CellIndex].R.GetFloat(), 1.0f);
	TestEqual(TEXT("displacement encoding matches legacy"), DensePixels[CellIndex].G.GetFloat(), LegacyPixels[CellIndex].G.GetFloat());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousDenseOffMatchesLegacyTest,
	"TDGame.FluidFX.Viscous.Dense.OffMatchesLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousDenseOffMatchesLegacyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidViscousSolverTestPrivate;

	FTDFluidSimulationConfig LegacyConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
	LegacyConfig.GridResolution = ViscousGridResolution;
	LegacyConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
	LegacyConfig.FlowNoiseAccelerationCmPerSec2 = 300.0f;

	FTDFluidSimulationConfig NeutralConfig = LegacyConfig;
	NeutralConfig.DenseDensityCeiling = 1.0f;
	NeutralConfig.DensityAccumulationPerSec = 0.0f;
	NeutralConfig.DenseGrowthThreshold = 0.7f;
	NeutralConfig.FlowNoiseSpeedMaskReferenceCmPerSec = 0.0f;

	FTDFluidSimulationConfig DenseConfig = LegacyConfig;
	DenseConfig.DenseDensityCeiling = 2.0f;
	DenseConfig.DensityAccumulationPerSec = 1.0f;

	FTDFluidSimulationConfig MaskedConfig = LegacyConfig;
	MaskedConfig.FlowNoiseSpeedMaskReferenceCmPerSec = 100.0f;

	const uint32 LegacyChecksum = RunViscousOrbitChecksum(LegacyConfig);
	TestEqual(TEXT("neutral dense and mask fields keep the legacy checksum"), RunViscousOrbitChecksum(NeutralConfig), LegacyChecksum);
	TestNotEqual(TEXT("dense growth changes the checksum"), RunViscousOrbitChecksum(DenseConfig), LegacyChecksum);
	TestNotEqual(TEXT("speed mask changes the checksum"), RunViscousOrbitChecksum(MaskedConfig), LegacyChecksum);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousFlowNoiseSpeedMaskTest,
	"TDGame.FluidFX.Viscous.FlowNoise.SpeedMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousFlowNoiseSpeedMaskTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidViscousSolverTestPrivate;

	constexpr float InverseReference = 1.0f / 100.0f;
	TestEqual(TEXT("still cell is masked"), FTDFluidGrid2DSolver::ComputeFlowNoiseSpeedMask(0.0f, InverseReference), 0.0f);
	TestTrue(TEXT("half reference speed is half mask"), FMath::Abs(FTDFluidGrid2DSolver::ComputeFlowNoiseSpeedMask(50.0f, InverseReference) - 0.5f) <= 1.0e-6f);
	TestEqual(TEXT("fast cell is unmasked"), FTDFluidGrid2DSolver::ComputeFlowNoiseSpeedMask(200.0f, InverseReference), 1.0f);
	TestEqual(TEXT("zero reference disables the mask"), FTDFluidGrid2DSolver::ComputeFlowNoiseSpeedMask(50.0f, 0.0f), 1.0f);

	constexpr float StepSeconds = 1.0f / 60.0f;
	FTDFluidSimulationConfig MaskedConfig = MakeViscousQuietConfig();
	MaskedConfig.FlowNoiseAccelerationCmPerSec2 = 400.0f;
	MaskedConfig.FlowNoiseSpeedMaskReferenceCmPerSec = 100.0f;
	FTDFluidSimulationConfig UnmaskedConfig = MaskedConfig;
	UnmaskedConfig.FlowNoiseSpeedMaskReferenceCmPerSec = 0.0f;

	FTDFluidGrid2DSolver MaskedSolver;
	MaskedSolver.Initialize(MaskedConfig, FVector2D::ZeroVector);
	TestTrue(TEXT("speed mask is active"), MaskedSolver.UsesFlowNoiseSpeedMask());
	MaskedSolver.StepSimulation(StepSeconds);
	TestEqual(TEXT("masked noise leaves a still grid still"), MeasureViscousMaxSpeed(MaskedSolver), 0.0f);

	FTDFluidGrid2DSolver UnmaskedSolver;
	UnmaskedSolver.Initialize(UnmaskedConfig, FVector2D::ZeroVector);
	TestFalse(TEXT("speed mask is inactive without reference"), UnmaskedSolver.UsesFlowNoiseSpeedMask());
	UnmaskedSolver.StepSimulation(StepSeconds);
	TestTrue(TEXT("unmasked noise moves a still grid"), MeasureViscousMaxSpeed(UnmaskedSolver) > 0.0f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousHeadlessMetricsReportTest,
	"TDGame.FluidFX.Viscous.HeadlessMetricsReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousHeadlessMetricsReportTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidViscousSolverTestPrivate;

	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr float SpreadDensityThreshold = 0.5f;
	constexpr float StreakDensityThreshold = 0.3f;

	const FTDFluidSimulationConfig MudConfig = MakeViscousMetricsConfig(ETDFluidPresetKind::ViscousMud);
	FTDFluidGrid2DSolver MudSolver;
	MudSolver.Initialize(MudConfig, FVector2D::ZeroVector);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(-150.0 + 300.0 * Step * StepSeconds, 0.0, 0.0);
		Stamp.WorldVelocity = FVector(300.0, 0.0, 0.0);
		Stamp.RadiusCm = 60.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		MudSolver.ApplyBrushStamps({ Stamp }, StepSeconds);
		MudSolver.StepSimulation(StepSeconds);
	}
	for (int32 Step = 0; Step < 60; ++Step)
	{
		MudSolver.StepSimulation(StepSeconds);
	}
	const int32 SpreadAreaAtTwoSeconds = CountViscousCellsAtLeast(MudSolver, SpreadDensityThreshold);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		MudSolver.StepSimulation(StepSeconds);
	}
	const int32 SpreadAreaAtThreeSeconds = CountViscousCellsAtLeast(MudSolver, SpreadDensityThreshold);
	const double SpreadGrowthPerSec = static_cast<double>(SpreadAreaAtThreeSeconds - SpreadAreaAtTwoSeconds) / FMath::Max(SpreadAreaAtTwoSeconds, 1);

	const FTDFluidSimulationConfig BloodConfig = MakeViscousMetricsConfig(ETDFluidPresetKind::ViscousBlood);
	FTDFluidGrid2DSolver BloodSolver;
	BloodSolver.Initialize(BloodConfig, FVector2D::ZeroVector);
	for (int32 Step = 0; Step < 15; ++Step)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(-50.0 + 400.0 * Step * StepSeconds, 0.0, 0.0);
		Stamp.WorldVelocity = FVector(400.0, 0.0, 0.0);
		Stamp.RadiusCm = 30.0f;
		Stamp.DensityStrength = 2.0f;
		Stamp.VelocityStrength = 1.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelG_Blood;
		BloodSolver.ApplyBrushStamps({ Stamp }, StepSeconds);
		BloodSolver.StepSimulation(StepSeconds);
	}
	for (int32 Step = 0; Step < 30; ++Step)
	{
		BloodSolver.StepSimulation(StepSeconds);
	}
	const FTDFluidViscousStreakShape Streak = MeasureViscousLargestStreak(BloodSolver, StreakDensityThreshold);
	const double StreakAspect = Streak.MaxWidthCells > 0 ? static_cast<double>(Streak.LengthCells) / Streak.MaxWidthCells : 0.0;
	const double StreakTipWidthRatio = Streak.MaxWidthCells > 0 ? static_cast<double>(Streak.TipWidthCells) / Streak.MaxWidthCells : 0.0;

	TSharedRef<FJsonObject> MudReport = MakeShared<FJsonObject>();
	WriteViscousConfigFields(*MudReport, MudConfig);
	MudReport->SetBoolField(TEXT("uses_dense_density"), MudSolver.UsesDenseDensity());
	MudReport->SetNumberField(TEXT("area_cells_at_2s"), SpreadAreaAtTwoSeconds);
	MudReport->SetNumberField(TEXT("area_cells_at_3s"), SpreadAreaAtThreeSeconds);

	TSharedRef<FJsonObject> BloodReport = MakeShared<FJsonObject>();
	WriteViscousConfigFields(*BloodReport, BloodConfig);
	BloodReport->SetBoolField(TEXT("uses_dense_density"), BloodSolver.UsesDenseDensity());
	BloodReport->SetNumberField(TEXT("streak_length_cells"), Streak.LengthCells);
	BloodReport->SetNumberField(TEXT("streak_max_width_cells"), Streak.MaxWidthCells);
	BloodReport->SetNumberField(TEXT("streak_tip_width_cells"), Streak.TipWidthCells);

	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("note"), TEXT("FX-14 CPU headless viscous metrics, record only"));
	Report->SetNumberField(TEXT("spread_growth_per_sec"), SpreadGrowthPerSec);
	Report->SetNumberField(TEXT("streak_aspect"), StreakAspect);
	Report->SetNumberField(TEXT("streak_tip_width_ratio"), StreakTipWidthRatio);
	Report->SetObjectField(TEXT("viscous_mud"), MudReport);
	Report->SetObjectField(TEXT("viscous_blood"), BloodReport);

	FString ReportText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportText);
	FJsonSerializer::Serialize(Report, Writer);
	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("calib"), TEXT("viscous_headless.json"));
	const bool bWroteReport = FFileHelper::SaveStringToFile(ReportText, *ReportPath);
	AddInfo(FString::Printf(TEXT("spread %.4f/s, streak aspect %.3f, tip ratio %.3f, written %d -> %s"), SpreadGrowthPerSec, StreakAspect, StreakTipWidthRatio, bWroteReport ? 1 : 0, *ReportPath));
	return !HasAnyErrors();
}

#endif
