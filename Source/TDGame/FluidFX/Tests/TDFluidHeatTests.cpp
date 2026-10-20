#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidFragmentEmitterActor.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidHeatTests, Log, All);

namespace TDFluidHeatTestPrivate
{
	constexpr float HeatTestStepSeconds = 1.0f / 60.0f;
	constexpr int32 HeatTestResolution = 64;
	constexpr double HeatTestExtentCm = 640.0;
	constexpr float LegacyTemperatureMinKelvin = 1000.0f;
	constexpr float LegacyTemperatureMaxKelvin = 2700.0f;
	constexpr float LegacyFlameErosion = 0.25f;
	constexpr float LegacyTemperatureCurve = 1.4f;
	constexpr float EmissionHeatExponent = 1.5f;
	constexpr float EmissionDensityExponent = 0.5f;
	constexpr float EmissionThreshold = 0.05f;
	constexpr float HotTemperatureThreshold = 0.6f;
	constexpr float SmokeCoverageThreshold = 0.15f;
	constexpr double ExplosionRegionHalfSizeCm = 300.0;
	constexpr int32 RestingFragmentCount = 64;
	constexpr double RestingFragmentRingRadiusCm = 150.0;
	const FVector2D ExplosionBurstPlaneCenter(0.0, -180.0);
	constexpr float StageSmokeBurstRadiusScale = 1.75f;
	constexpr double StageHitPointHeightCm = 15.0;
	constexpr double StageActorHeightCm = 50.0;
	constexpr double StageWindowCenterOffsetRatio = 0.45;
	constexpr double MaxUndergroundHeatRatio = 0.02;
	constexpr float WindowFillEmissionThreshold = 0.1f;
	constexpr double MaxWindowFillWidthRatio = 0.5;
	constexpr double WindowBorderBandRatio = 0.05;
	constexpr double MaxWindowBorderEmission = 0.05;

	struct FTDHeatGoldenExpectation
	{
		ETDFluidPresetKind Kind = ETDFluidPresetKind::Explosion;
		uint32 StateChecksum = 0;
		uint32 AuxChecksum = 0;
	};

	const FTDHeatGoldenExpectation HeatGoldenExpectations[] = {
		{ ETDFluidPresetKind::Explosion, 0x2FE1B4F5u, 0x1A9D5122u },
		{ ETDFluidPresetKind::Torch, 0xF7340506u, 0x90656197u },
	};

	FTDFluidSimulationConfig MakeHeatTestConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = HeatTestResolution;
		Config.SimExtentCm = FVector2D(HeatTestExtentCm, HeatTestExtentCm);
		Config.VelocityDamping = 0.0f;
		Config.DensityDissipation = 0.0f;
		Config.PressureIterations = 8;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.BorderFadeRatio = 0.0f;
		Config.MaxPointsPerTick = 512;
		Config.bEnableHeatChannel = true;
		Config.HeatCoolingPerSec = 0.0f;
		Config.HeatBuoyancyCmPerSec2 = 0.0f;
		return Config;
	}

	FTDFluidBrushStamp MakeStamp(const FVector2D& Center, float RadiusCm, float DensityStrength, float HeatStrength, const FVector2D& Velocity = FVector2D::ZeroVector, float VelocityStrength = 0.0f)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(Center.X, Center.Y, 0.0);
		Stamp.WorldVelocity = FVector(Velocity.X, Velocity.Y, 0.0);
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = DensityStrength;
		Stamp.HeatStrength = HeatStrength;
		Stamp.VelocityStrength = VelocityStrength;
		Stamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		return Stamp;
	}

	double SumHeat(const FTDFluidGrid2DSolver& Solver)
	{
		double Total = 0.0;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				Total += Solver.SampleAtCell(CellX, CellY).Heat;
			}
		}
		return Total;
	}

	double SumPrimaryDensity(const FTDFluidGrid2DSolver& Solver)
	{
		double Total = 0.0;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				Total += Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
			}
		}
		return Total;
	}

	FVector2D ComputeHeatCentroidCells(const FTDFluidGrid2DSolver& Solver)
	{
		double Weight = 0.0;
		FVector2D Weighted = FVector2D::ZeroVector;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				const double CellHeat = Solver.SampleAtCell(CellX, CellY).Heat;
				Weight += CellHeat;
				Weighted += FVector2D(CellX, CellY) * CellHeat;
			}
		}
		return Weight > UE_DOUBLE_SMALL_NUMBER ? Weighted / Weight : FVector2D::ZeroVector;
	}

	uint32 ComputePixelChecksum(const FTDFluidGrid2DSolver& Solver)
	{
		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);
		const uint32 DensityChecksum = FCrc::MemCrc32(DensityPressurePixels.GetData(), DensityPressurePixels.Num() * sizeof(FFloat16Color));
		return FCrc::MemCrc32(VelocityPixels.GetData(), VelocityPixels.Num() * sizeof(FFloat16Color), DensityChecksum);
	}

	uint32 ComputeAuxChecksum(const FTDFluidGrid2DSolver& Solver)
	{
		TArray<FFloat16Color> AuxPixels;
		Solver.PackAuxPixels(AuxPixels);
		return FCrc::MemCrc32(AuxPixels.GetData(), AuxPixels.Num() * sizeof(FFloat16Color));
	}

	void AppendOrbitStamps(int32 StepIndex, double ExtentCm, float HeatStrength, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		const double Angle = 0.21 * static_cast<double>(StepIndex);
		const FVector2D Center(FMath::Cos(Angle) * ExtentCm * 0.2, FMath::Sin(Angle) * ExtentCm * 0.2);
		const FVector2D Velocity(-FMath::Sin(Angle) * 400.0, FMath::Cos(Angle) * 400.0);
		OutStamps.Add(MakeStamp(Center, static_cast<float>(ExtentCm * 0.06), 0.9f, HeatStrength, Velocity, 1.0f));
	}

	void RunOrbitScenario(FTDFluidGrid2DSolver& Solver, int32 StepCount, float HeatStrength)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Stamps.Reset();
			AppendOrbitStamps(StepIndex, Solver.GetExtentCm().X, HeatStrength, Stamps);
			Solver.ApplyBrushStamps(Stamps, HeatTestStepSeconds);
			Solver.StepSimulation(HeatTestStepSeconds);
		}
	}

	float ComputeLegacyTemperatureProxy(float Density)
	{
		const float RidgeValue = 0.5f;
		const float ErodedDensity = FMath::Clamp((Density - LegacyFlameErosion * (1.0f - RidgeValue)) / (1.0f - LegacyFlameErosion), 0.0f, 1.0f);
		const float TemperatureBlend = FMath::Clamp(FMath::Pow(ErodedDensity, LegacyTemperatureCurve) * 0.8f + 0.12f * (RidgeValue - 0.5f), 0.0f, 1.0f);
		const float TemperatureKelvin = FMath::Lerp(LegacyTemperatureMinKelvin, LegacyTemperatureMaxKelvin, TemperatureBlend);
		return TemperatureKelvin / LegacyTemperatureMaxKelvin;
	}

	float ComputeEmissionProxy(float Heat, float Density)
	{
		return FMath::Pow(FMath::Clamp(Heat, 0.0f, 1.0f), EmissionHeatExponent) * FMath::Pow(FMath::Max(Density, 1.0e-6f), EmissionDensityExponent);
	}

	struct FTDExplosionRun
	{
		FTDFluidGrid2DSolver FireSolver;
		FTDFluidGrid2DSolver SmokeSolver;
		FTDFluidFragmentTrailSettings Trail;
		bool bUsesHeat = false;
	};

	FTDFluidSimulationConfig MakeCpuFallbackConfig(ETDFluidPresetKind Kind)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		if (Config.CpuFallbackResolution > 0)
		{
			Config.GridResolution = Config.CpuFallbackResolution;
		}
		Config.SolverBackend = ETDFluidSolverBackend::Cpu;
		return Config;
	}

	void InitializeExplosionRun(FTDExplosionRun& Run, bool bUsesFx8bValues)
	{
		Run.bUsesHeat = bUsesFx8bValues;
		FTDFluidSimulationConfig FireConfig = MakeCpuFallbackConfig(ETDFluidPresetKind::Explosion);
		FTDFluidSimulationConfig SmokeConfig = MakeCpuFallbackConfig(ETDFluidPresetKind::ExplosionSmoke);
		Run.Trail = UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion);
		if (!bUsesFx8bValues)
		{
			FireConfig.bEnableHeatChannel = false;
			FireConfig.DensityDissipation = 1.3f;
			FireConfig.OutflowBandRatio = 0.15f;
			FireConfig.OutflowRatePerSec = 6.0f;
			FireConfig.VorticityStrength = 5.0f;
			FireConfig.BuoyancyCmPerSec2 = 400.0f;
			SmokeConfig.bEnableHeatChannel = false;
			SmokeConfig.BuoyancyCmPerSec2 = 120.0f;
			Run.Trail = FTDFluidFragmentTrailSettings();
		}

		Run.FireSolver.Initialize(FireConfig, FVector2D::ZeroVector);
		Run.SmokeSolver.Initialize(SmokeConfig, FVector2D::ZeroVector);
		Run.FireSolver.SetPlaneUpDirection(FVector2D(0.0, 1.0));
		Run.SmokeSolver.SetPlaneUpDirection(FVector2D(0.0, 1.0));
	}

	void AppendBurstStamps(const FTDExplosionRun& Run, float ElapsedSeconds, bool bIsFirstStep, TArray<FTDFluidBrushStamp>& OutFireStamps, TArray<FTDFluidBrushStamp>& OutSmokeStamps)
	{
		OutFireStamps.Reset();
		OutSmokeStamps.Reset();
		const FTDFluidFragmentTrailSettings& Trail = Run.Trail;
		const bool bIsSustaining = Trail.BurstSustainSeconds > UE_SMALL_NUMBER && ElapsedSeconds < Trail.BurstSustainSeconds;
		if (!bIsFirstStep && !bIsSustaining)
		{
			return;
		}

		const float DensityRatio = bIsFirstStep ? 1.0f : 1.0f - ElapsedSeconds / Trail.BurstSustainSeconds;
		const float HeatRatio = bIsFirstStep || Trail.bSustainsBurstHeat ? 1.0f : DensityRatio;
		const FTDFluidPlaneBasis WorldBasis = FTDFluidPlaneBasis::MakeWorldFacing();
		const FVector BurstCenter(ExplosionBurstPlaneCenter.X, ExplosionBurstPlaneCenter.Y, 0.0);
		ATDFluidFragmentEmitterActor::BuildBurstStamps(BurstCenter, WorldBasis, Trail, ETDFluidBrushChannel::ChannelB_Fire, 1.0f, Trail.FireDensityStrength * DensityRatio, Run.bUsesHeat ? Trail.FireHeatStrength * HeatRatio : 0.0f, OutFireStamps);
		ATDFluidFragmentEmitterActor::BuildBurstStamps(BurstCenter, WorldBasis, Trail, ETDFluidBrushChannel::ChannelR_Mud, StageSmokeBurstRadiusScale, Trail.SmokeDensityStrength * DensityRatio, Run.bUsesHeat ? Trail.SmokeHeatStrength * HeatRatio : 0.0f, OutSmokeStamps);
	}

	void AppendRestingStamps(const FTDExplosionRun& Run, float ElapsedSeconds, TArray<FTDFluidBrushStamp>& InOutFireStamps)
	{
		TArray<FVector> RestingPositions;
		for (int32 FragmentIndex = 0; FragmentIndex < RestingFragmentCount; ++FragmentIndex)
		{
			const double Angle = UE_DOUBLE_TWO_PI * static_cast<double>(FragmentIndex) / static_cast<double>(RestingFragmentCount);
			RestingPositions.Add(FVector(ExplosionBurstPlaneCenter.X + FMath::Cos(Angle) * RestingFragmentRingRadiusCm, ExplosionBurstPlaneCenter.Y + FMath::Sin(Angle) * RestingFragmentRingRadiusCm, 0.0));
		}

		TArray<FTDFluidFragmentTarget> LegacyTargets;
		ATDFluidFragmentEmitterActor::BuildLegacyTargets(nullptr, nullptr, LegacyTargets);
		TArray<FTDFluidBrushStamp> RestingStamps;
		ATDFluidFragmentEmitterActor::BuildRestingHeatStamps(LegacyTargets[0], RestingPositions, RestingPositions, HeatTestStepSeconds, ElapsedSeconds, Run.Trail, RestingStamps);
		InOutFireStamps.Append(RestingStamps);
	}

	void StepExplosionRun(FTDExplosionRun& Run, int32 StepIndex)
	{
		const float ElapsedSeconds = static_cast<float>(StepIndex) * HeatTestStepSeconds;
		TArray<FTDFluidBrushStamp> FireStamps;
		TArray<FTDFluidBrushStamp> SmokeStamps;
		AppendBurstStamps(Run, ElapsedSeconds, StepIndex == 0, FireStamps, SmokeStamps);
		AppendRestingStamps(Run, ElapsedSeconds, FireStamps);
		Run.FireSolver.ApplyBrushStamps(FireStamps, HeatTestStepSeconds);
		Run.SmokeSolver.ApplyBrushStamps(SmokeStamps, HeatTestStepSeconds);
		Run.FireSolver.StepSimulation(HeatTestStepSeconds);
		Run.SmokeSolver.StepSimulation(HeatTestStepSeconds);
	}

	bool IsInsideExplosionRegion(const FVector2D& PlanePoint)
	{
		const FVector2D Offset = PlanePoint - ExplosionBurstPlaneCenter;
		return FMath::Abs(Offset.X) <= ExplosionRegionHalfSizeCm && FMath::Abs(Offset.Y) <= ExplosionRegionHalfSizeCm;
	}

	template <typename PredicateType>
	double MeasureRegionFraction(const FTDFluidGrid2DSolver& Solver, PredicateType&& Predicate)
	{
		int32 RegionCellCount = 0;
		int32 MatchingCellCount = 0;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				if (!IsInsideExplosionRegion(Solver.CellToWorldCenter(CellX, CellY)))
				{
					continue;
				}

				++RegionCellCount;
				MatchingCellCount += Predicate(Solver.SampleAtCell(CellX, CellY)) ? 1 : 0;
			}
		}
		return RegionCellCount > 0 ? static_cast<double>(MatchingCellCount) / RegionCellCount : 0.0;
	}

	double MeasureHotFraction(const FTDExplosionRun& Run)
	{
		if (Run.bUsesHeat)
		{
			return MeasureRegionFraction(Run.FireSolver, [](const FTDFluidCellSample& Sample) { return Sample.Heat >= HotTemperatureThreshold; });
		}
		return MeasureRegionFraction(Run.FireSolver, [](const FTDFluidCellSample& Sample) { return ComputeLegacyTemperatureProxy(Sample.SelectiveDensity) >= HotTemperatureThreshold; });
	}

	double MeasureSmokeCoverage(const FTDExplosionRun& Run)
	{
		return MeasureRegionFraction(Run.SmokeSolver, [](const FTDFluidCellSample& Sample) { return Sample.PrimaryDensity >= SmokeCoverageThreshold; });
	}

	double MeasureEmissionFraction(const FTDExplosionRun& Run)
	{
		if (Run.bUsesHeat)
		{
			return MeasureRegionFraction(Run.FireSolver, [](const FTDFluidCellSample& Sample) { return ComputeEmissionProxy(Sample.Heat, Sample.SelectiveDensity) >= EmissionThreshold; });
		}
		return MeasureRegionFraction(Run.FireSolver, [](const FTDFluidCellSample& Sample) { return ComputeEmissionProxy(ComputeLegacyTemperatureProxy(Sample.SelectiveDensity), Sample.SelectiveDensity) >= EmissionThreshold; });
	}

	float ComputeRunEmissionProxy(const FTDExplosionRun& Run, const FTDFluidCellSample& Sample)
	{
		if (Run.bUsesHeat)
		{
			return ComputeEmissionProxy(Sample.Heat, Sample.SelectiveDensity);
		}
		return ComputeEmissionProxy(ComputeLegacyTemperatureProxy(Sample.SelectiveDensity), Sample.SelectiveDensity);
	}

	double MeasureWindowEmissionWidthRatio(const FTDExplosionRun& Run, float EmissionLowerBound)
	{
		const FTDFluidGrid2DSolver& Solver = Run.FireSolver;
		int32 MinEmittingCellX = Solver.GetResolution();
		int32 MaxEmittingCellX = -1;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				if (ComputeRunEmissionProxy(Run, Solver.SampleAtCell(CellX, CellY)) < EmissionLowerBound)
				{
					continue;
				}

				MinEmittingCellX = FMath::Min(MinEmittingCellX, CellX);
				MaxEmittingCellX = FMath::Max(MaxEmittingCellX, CellX);
			}
		}
		if (MaxEmittingCellX < MinEmittingCellX)
		{
			return 0.0;
		}
		return static_cast<double>(MaxEmittingCellX - MinEmittingCellX + 1) / static_cast<double>(Solver.GetResolution());
	}

	bool IsInsideWindowBorderBand(const FTDFluidGrid2DSolver& Solver, const FVector2D& PlanePoint)
	{
		const FVector2D ExtentCm = Solver.GetExtentCm();
		const FVector2D CenterOffset = PlanePoint - Solver.GetCenterWorld();
		const double InnerHalfWidthCm = 0.5 * ExtentCm.X * (1.0 - 2.0 * WindowBorderBandRatio);
		const double InnerBottomCm = -0.5 * ExtentCm.Y * (1.0 - 2.0 * WindowBorderBandRatio);
		return FMath::Abs(CenterOffset.X) > InnerHalfWidthCm || CenterOffset.Y < InnerBottomCm;
	}

	double MeasureWindowBorderEmissionMax(const FTDExplosionRun& Run)
	{
		const FTDFluidGrid2DSolver& Solver = Run.FireSolver;
		double MaxEmission = 0.0;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				if (!IsInsideWindowBorderBand(Solver, Solver.CellToWorldCenter(CellX, CellY)))
				{
					continue;
				}

				MaxEmission = FMath::Max(MaxEmission, static_cast<double>(ComputeRunEmissionProxy(Run, Solver.SampleAtCell(CellX, CellY))));
			}
		}
		return MaxEmission;
	}

	struct FTDUndergroundHeatShares
	{
		double UndergroundRatio = 0.0;
		double WindowLostRatio = 0.0;
	};

	FTDUndergroundHeatShares MeasureUndergroundHeatShares(double PitchDegrees, float BurstCenterHeightOffsetCm)
	{
		const FTDFluidPlaneBasis CameraBasis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(PitchDegrees, 0.0, 0.0), 0.0f);
		const FVector BurstWorld(0.0, 0.0, StageHitPointHeightCm + BurstCenterHeightOffsetCm);
		const double PlaneDepthCm = CameraBasis.ProjectDepth(BurstWorld);
		FTDFluidSimulationConfig Config = MakeCpuFallbackConfig(ETDFluidPresetKind::Explosion);
		const FVector2D StageWindowCenter = CameraBasis.ProjectPoint(FVector(0.0, 0.0, StageActorHeightCm)) + FVector2D(0.0, StageWindowCenterOffsetRatio * Config.SimExtentCm.Y);
		const FVector2D StageWindowHalfExtent = Config.SimExtentCm * 0.5;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, CameraBasis.ProjectPoint(BurstWorld));

		FTDFluidFragmentTrailSettings Trail = UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion);
		TArray<FTDFluidBrushStamp> BurstStamps;
		ATDFluidFragmentEmitterActor::BuildBurstStamps(BurstWorld, CameraBasis, Trail, ETDFluidBrushChannel::ChannelB_Fire, 1.0f, Trail.FireDensityStrength, 1.0f, BurstStamps);
		for (FTDFluidBrushStamp& Stamp : BurstStamps)
		{
			const FVector2D PlanePoint = CameraBasis.ProjectPoint(Stamp.WorldLocation);
			Stamp.WorldLocation = FVector(PlanePoint.X, PlanePoint.Y, 0.0);
			Stamp.VelocityStrength = 0.0f;
		}
		Solver.ApplyBrushStamps(BurstStamps, HeatTestStepSeconds);

		double TotalHeat = 0.0;
		double UndergroundHeat = 0.0;
		double WindowLostHeat = 0.0;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				const double CellHeat = Solver.SampleAtCell(CellX, CellY).Heat;
				TotalHeat += CellHeat;
				const FVector2D PlanePoint = Solver.CellToWorldCenter(CellX, CellY);
				const FVector2D WindowOffset = PlanePoint - StageWindowCenter;
				const bool bIsUnderground = CameraBasis.UnprojectPoint(PlanePoint, PlaneDepthCm).Z < 0.0;
				const bool bIsOutsideStageWindow = FMath::Abs(WindowOffset.X) > StageWindowHalfExtent.X || FMath::Abs(WindowOffset.Y) > StageWindowHalfExtent.Y;
				UndergroundHeat += bIsUnderground ? CellHeat : 0.0;
				WindowLostHeat += (bIsUnderground || bIsOutsideStageWindow) ? CellHeat : 0.0;
			}
		}

		FTDUndergroundHeatShares Shares;
		if (TotalHeat <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Shares;
		}
		Shares.UndergroundRatio = UndergroundHeat / TotalHeat;
		Shares.WindowLostRatio = WindowLostHeat / TotalHeat;
		return Shares;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeatDisabledKeepsChecksumsTest,
	"TDGame.FluidFX.Heat.DisabledKeepsGoldenChecksums",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeatDisabledKeepsChecksumsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	const ETDFluidPresetKind Kinds[] = { ETDFluidPresetKind::Fire, ETDFluidPresetKind::Smoke, ETDFluidPresetKind::Mud, ETDFluidPresetKind::Water };
	for (const ETDFluidPresetKind Kind : Kinds)
	{
		const FTDFluidSimulationConfig BaseConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		FTDFluidSimulationConfig InactiveConfig = BaseConfig;
		InactiveConfig.HeatCoolingPerSec = 7.0f;
		InactiveConfig.HeatBuoyancyCmPerSec2 = 500.0f;
		InactiveConfig.EdgeBounciness = 0.7f;
		InactiveConfig.VortexRadiusRatio = 0.2f;
		InactiveConfig.CpuFallbackResolution = 32;

		FTDFluidGrid2DSolver BaseSolver;
		BaseSolver.Initialize(BaseConfig, FVector2D::ZeroVector);
		RunOrbitScenario(BaseSolver, 24, 0.0f);
		FTDFluidGrid2DSolver InactiveSolver;
		InactiveSolver.Initialize(InactiveConfig, FVector2D::ZeroVector);
		RunOrbitScenario(InactiveSolver, 24, 1.0f);

		const FString Label = UEnum::GetValueAsString(Kind);
		TestEqual(*(Label + TEXT(" state checksum is unchanged by inactive FX-8b fields and stamp heat")), InactiveSolver.ComputeStateChecksum(), BaseSolver.ComputeStateChecksum());
		TestEqual(*(Label + TEXT(" output pixel checksum is unchanged")), ComputePixelChecksum(InactiveSolver), ComputePixelChecksum(BaseSolver));
		TestFalse(*(Label + TEXT(" heat channel stays inactive")), InactiveSolver.IsHeatChannelActive());
		TestEqual(*(Label + TEXT(" aux pixels are not packed when heat is off")), ComputeAuxChecksum(InactiveSolver), FCrc::MemCrc32(nullptr, 0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeatAdvectsAndCoolsTest,
	"TDGame.FluidFX.Heat.AdvectsWithVelocityAndCools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeatAdvectsAndCoolsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	constexpr int32 StepCount = 40;
	constexpr float CoolingPerSec = 2.0f;
	const FVector2D BlobCenter(-150.0, 0.0);

	auto RunBlob = [&BlobCenter](FTDFluidGrid2DSolver& Solver, float HeatCoolingPerSec)
	{
		FTDFluidSimulationConfig Config = MakeHeatTestConfig();
		Config.HeatCoolingPerSec = HeatCoolingPerSec;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Add(MakeStamp(BlobCenter, 60.0f, 0.8f, 0.8f));
		Solver.ApplyBrushStamps(Stamps, HeatTestStepSeconds);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Stamps.Reset();
			Stamps.Add(MakeStamp(BlobCenter + FVector2D(StepIndex * 5.0, 0.0), 120.0f, 0.0f, 0.0f, FVector2D(300.0, 0.0), 1.0f));
			Solver.ApplyBrushStamps(Stamps, HeatTestStepSeconds);
			Solver.StepSimulation(HeatTestStepSeconds);
		}
	};

	FTDFluidGrid2DSolver NeutralSolver;
	RunBlob(NeutralSolver, 0.0f);
	FTDFluidGrid2DSolver CoolingSolver;
	RunBlob(CoolingSolver, CoolingPerSec);

	const FVector2D InitialCentroid((BlobCenter.X + 0.5 * HeatTestExtentCm) / (HeatTestExtentCm / HeatTestResolution) - 0.5, (BlobCenter.Y + 0.5 * HeatTestExtentCm) / (HeatTestExtentCm / HeatTestResolution) - 0.5);
	const FVector2D FinalCentroid = ComputeHeatCentroidCells(NeutralSolver);
	TestTrue(TEXT("Heat moved with the velocity field by at least two cells"), FinalCentroid.X - InitialCentroid.X >= 2.0);

	const double CoolingFactor = FMath::Pow(1.0 / (1.0 + CoolingPerSec * HeatTestStepSeconds), static_cast<double>(StepCount));
	double MaximumNeutralMismatch = 0.0;
	double MaximumCoolingRelativeError = 0.0;
	for (int32 CellY = 0; CellY < HeatTestResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < HeatTestResolution; ++CellX)
		{
			const FTDFluidCellSample Neutral = NeutralSolver.SampleAtCell(CellX, CellY);
			MaximumNeutralMismatch = FMath::Max(MaximumNeutralMismatch, static_cast<double>(FMath::Abs(Neutral.Heat - Neutral.PrimaryDensity)));
			const FTDFluidCellSample Cooling = CoolingSolver.SampleAtCell(CellX, CellY);
			if (Cooling.PrimaryDensity > 0.01f)
			{
				const double Expected = Cooling.PrimaryDensity * CoolingFactor;
				MaximumCoolingRelativeError = FMath::Max(MaximumCoolingRelativeError, FMath::Abs(Cooling.Heat - Expected) / Expected);
			}
		}
	}
	AddInfo(FString::Printf(TEXT("TDFluidHeat: centroid_shift_cells=%.3f neutral_max_mismatch=%.3e cooling_max_relative_error=%.3e"), FinalCentroid.X - InitialCentroid.X, MaximumNeutralMismatch, MaximumCoolingRelativeError));
	TestTrue(TEXT("Heat with zero cooling follows the density advection exactly"), MaximumNeutralMismatch <= 1.0e-6);
	TestTrue(TEXT("Heat cools by the configured implicit factor within 1e-4 relative"), MaximumCoolingRelativeError <= 1.0e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeatBuoyancyTest,
	"TDGame.FluidFX.Heat.BuoyancyFromHeatOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeatBuoyancyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	auto MeasureRise = [this](float HeatBuoyancy)
	{
		FTDFluidSimulationConfig Config = MakeHeatTestConfig();
		Config.HeatBuoyancyCmPerSec2 = HeatBuoyancy;
		Config.HeatCoolingPerSec = 0.5f;
		Config.BorderFadeRatio = 0.1f;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Add(MakeStamp(FVector2D(0.0, -100.0), 50.0f, 0.0f, 1.0f));
		Solver.ApplyBrushStamps(Stamps, HeatTestStepSeconds);
		const FVector2D InitialCentroid = ComputeHeatCentroidCells(Solver);
		for (int32 StepIndex = 0; StepIndex < 30; ++StepIndex)
		{
			Solver.StepSimulation(HeatTestStepSeconds);
		}
		TestFalse(TEXT("A heat-only stamp leaves density empty"), SumPrimaryDensity(Solver) > 1.0e-6);
		return ComputeHeatCentroidCells(Solver).Y - InitialCentroid.Y;
	};

	const double BuoyantRise = MeasureRise(1500.0f);
	const double NeutralRise = MeasureRise(0.0f);
	AddInfo(FString::Printf(TEXT("TDFluidHeat: buoyant_rise_cells=%.3f neutral_rise_cells=%.3f"), BuoyantRise, NeutralRise));
	TestTrue(TEXT("Heat buoyancy lifts a heat-only blob by at least two cells"), BuoyantRise >= 2.0);
	TestTrue(TEXT("Without heat buoyancy the blob stays within half a cell"), FMath::Abs(NeutralRise) < 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeatSeparateCoolingTest,
	"TDGame.FluidFX.Heat.SeparateFromDensityCooling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeatSeparateCoolingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	constexpr int32 StepCount = 60;
	FTDFluidSimulationConfig Config = MakeHeatTestConfig();
	Config.HeatCoolingPerSec = 3.0f;
	Config.DensityDissipation = 0.3f;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	TArray<FTDFluidBrushStamp> Stamps;
	Stamps.Add(MakeStamp(FVector2D::ZeroVector, 80.0f, 0.8f, 0.8f));
	Solver.ApplyBrushStamps(Stamps, HeatTestStepSeconds);
	const double InitialHeat = SumHeat(Solver);
	const double InitialDensity = SumPrimaryDensity(Solver);
	for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
	{
		Solver.StepSimulation(HeatTestStepSeconds);
	}

	const double HeatRatio = SumHeat(Solver) / InitialHeat;
	const double DensityRatio = SumPrimaryDensity(Solver) / InitialDensity;
	const double ExpectedHeatRatio = FMath::Pow(1.0 / (1.0 + 3.0 * HeatTestStepSeconds), static_cast<double>(StepCount));
	const double ExpectedDensityRatio = FMath::Pow(1.0 / (1.0 + 0.3 * HeatTestStepSeconds), static_cast<double>(StepCount));
	AddInfo(FString::Printf(TEXT("TDFluidHeat: heat_ratio=%.5f expected=%.5f density_ratio=%.5f expected=%.5f"), HeatRatio, ExpectedHeatRatio, DensityRatio, ExpectedDensityRatio));
	TestTrue(TEXT("Heat decays at its own cooling rate"), FMath::Abs(HeatRatio - ExpectedHeatRatio) <= 0.02 * ExpectedHeatRatio);
	TestTrue(TEXT("Density decays at its own dissipation rate"), FMath::Abs(DensityRatio - ExpectedDensityRatio) <= 0.02 * ExpectedDensityRatio);

	const FTDFluidSimulationConfig ExplosionConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Explosion);
	TestTrue(TEXT("Explosion kind cools heat separately from density dissipation"), ExplosionConfig.HeatCoolingPerSec != ExplosionConfig.DensityDissipation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeatShiftResetTest,
	"TDGame.FluidFX.Heat.ShiftAndResetCarryHeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeatShiftResetTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	constexpr int32 ShiftX = 5;
	constexpr int32 ShiftY = -3;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeHeatTestConfig(), FVector2D::ZeroVector);
	TArray<FTDFluidBrushStamp> Stamps;
	Stamps.Add(MakeStamp(FVector2D(50.0, 30.0), 70.0f, 0.6f, 0.9f));
	Solver.ApplyBrushStamps(Stamps, HeatTestStepSeconds);

	TArray<FTDFluidCellSample> Before;
	for (int32 CellY = 0; CellY < HeatTestResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < HeatTestResolution; ++CellX)
		{
			Before.Add(Solver.SampleAtCell(CellX, CellY));
		}
	}

	const double CellSizeCm = HeatTestExtentCm / HeatTestResolution;
	TestTrue(TEXT("The window shifted"), Solver.ShiftOriginForSlidingWindow(FVector2D(ShiftX * CellSizeCm, ShiftY * CellSizeCm)));
	int32 MismatchCount = 0;
	for (int32 CellY = 0; CellY < HeatTestResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < HeatTestResolution; ++CellX)
		{
			const int32 SourceX = CellX + ShiftX;
			const int32 SourceY = CellY + ShiftY;
			const bool bHasSource = SourceX >= 0 && SourceX < HeatTestResolution && SourceY >= 0 && SourceY < HeatTestResolution;
			const FTDFluidCellSample After = Solver.SampleAtCell(CellX, CellY);
			const float ExpectedHeat = bHasSource ? Before[SourceY * HeatTestResolution + SourceX].Heat : 0.0f;
			const float ExpectedDensity = bHasSource ? Before[SourceY * HeatTestResolution + SourceX].PrimaryDensity : 0.0f;
			MismatchCount += (After.Heat != ExpectedHeat || After.PrimaryDensity != ExpectedDensity) ? 1 : 0;
		}
	}
	TestEqual(TEXT("Heat and density moved by the same cell offset"), MismatchCount, 0);

	Solver.ResetSimulation();
	TestEqual(TEXT("Reset clears heat"), SumHeat(Solver), 0.0);
	TArray<FFloat16Color> AuxPixels;
	Solver.PackAuxPixels(AuxPixels);
	TestEqual(TEXT("Reset keeps the heat channel allocated"), AuxPixels.Num(), HeatTestResolution * HeatTestResolution);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeatDeterministicReplayTest,
	"TDGame.FluidFX.Heat.DeterministicReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeatDeterministicReplayTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	auto RunReplay = [](bool bEnablesHeat)
	{
		FTDFluidSimulationConfig Config = MakeHeatTestConfig();
		Config.bEnableHeatChannel = bEnablesHeat;
		Config.HeatBuoyancyCmPerSec2 = 800.0f;
		Config.HeatCoolingPerSec = 1.5f;
		Config.bReflectsAtBoundaries = true;
		Config.EdgeBounciness = 0.4f;
		Config.VortexTangentialAccelerationCmPerSec2 = 300.0f;
		Config.VortexRadialDriftCmPerSec = 30.0f;
		Config.BorderFadeRatio = 0.1f;
		Config.BorderFadeExponent = 2.0f;
		Config.VorticityStrength = 2.0f;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));
		RunOrbitScenario(Solver, 60, 0.9f);
		return Solver.ComputeStateChecksum() ^ ComputePixelChecksum(Solver);
	};

	const uint32 FirstRun = RunReplay(true);
	const uint32 SecondRun = RunReplay(true);
	const uint32 HeatOffRun = RunReplay(false);
	TestEqual(TEXT("Two runs with heat, reflection and vortex give the same checksum"), SecondRun, FirstRun);
	TestNotEqual(TEXT("The heat channel changes the simulation state"), HeatOffRun, FirstRun);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeatAuxPixelsTest,
	"TDGame.FluidFX.Heat.AuxPixelsPackHeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeatAuxPixelsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeHeatTestConfig(), FVector2D::ZeroVector);
	TArray<FTDFluidBrushStamp> Stamps;
	Stamps.Add(MakeStamp(FVector2D::ZeroVector, 90.0f, 0.3f, 0.75f));
	Solver.ApplyBrushStamps(Stamps, HeatTestStepSeconds);
	TArray<FFloat16Color> AuxPixels;
	Solver.PackAuxPixels(AuxPixels);
	TestEqual(TEXT("Aux pixels cover the grid"), AuxPixels.Num(), HeatTestResolution * HeatTestResolution);

	int32 MismatchCount = 0;
	for (int32 CellY = 0; CellY < HeatTestResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < HeatTestResolution; ++CellX)
		{
			const FFloat16Color& Pixel = AuxPixels[CellY * HeatTestResolution + CellX];
			const float ExpectedHeat = FFloat16(Solver.SampleAtCell(CellX, CellY).Heat).GetFloat();
			const bool bMatches = Pixel.R.GetFloat() == ExpectedHeat && Pixel.G.GetFloat() == 0.0f && Pixel.B.GetFloat() == 0.0f && Pixel.A.GetFloat() == 0.0f;
			MismatchCount += bMatches ? 0 : 1;
		}
	}
	TestEqual(TEXT("Aux R is the half-float heat and G, B, A are reserved zero"), MismatchCount, 0);

	FTDFluidSimulationConfig HeatOffConfig = MakeHeatTestConfig();
	HeatOffConfig.bEnableHeatChannel = false;
	Solver.UpdateConfig(HeatOffConfig);
	Solver.PackAuxPixels(AuxPixels);
	TestEqual(TEXT("Turning heat off releases the aux output"), AuxPixels.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeatGoldenChecksumsTest,
	"TDGame.FluidFX.Heat.GoldenChecksums",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeatGoldenChecksumsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	for (const FTDHeatGoldenExpectation& Expectation : HeatGoldenExpectations)
	{
		auto RunGolden = [&Expectation](uint32& OutAuxChecksum)
		{
			FTDFluidGrid2DSolver Solver;
			Solver.Initialize(MakeCpuFallbackConfig(Expectation.Kind), FVector2D::ZeroVector);
			Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));
			RunOrbitScenario(Solver, 45, 1.0f);
			OutAuxChecksum = ComputeAuxChecksum(Solver);
			return Solver.ComputeStateChecksum();
		};

		uint32 FirstAux = 0;
		uint32 SecondAux = 0;
		const uint32 FirstState = RunGolden(FirstAux);
		const uint32 SecondState = RunGolden(SecondAux);
		const FString Line = FString::Printf(TEXT("TDFluidHeatGolden: kind=%s state=0x%08X aux=0x%08X"), *UEnum::GetValueAsString(Expectation.Kind), FirstState, FirstAux);
		UE_LOG(LogTDFluidHeatTests, Display, TEXT("%s"), *Line);
		AddInfo(Line);
		TestEqual(TEXT("Heat golden state checksum repeats"), SecondState, FirstState);
		TestEqual(TEXT("Heat golden aux checksum repeats"), SecondAux, FirstAux);
		if (Expectation.StateChecksum != 0u)
		{
			TestEqual(TEXT("Heat golden state checksum matches the recorded value"), FirstState, Expectation.StateChecksum);
			TestEqual(TEXT("Heat golden aux checksum matches the recorded value"), FirstAux, Expectation.AuxChecksum);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidExplosionHotCoreTimelineTest,
	"TDGame.FluidFX.Explosion.HotCoreAreaTimeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidExplosionHotCoreTimelineTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	constexpr int32 HotCoreStep = 12;
	constexpr int32 SmokeCoverageStep = 72;
	constexpr int32 WindowFillStep = 18;
	constexpr int32 WindowBorderLastStep = 150;
	constexpr int32 LingerStep = 300;
	for (const bool bUsesFx8bValues : { false, true })
	{
		FTDExplosionRun Run;
		InitializeExplosionRun(Run, bUsesFx8bValues);
		double HotFraction = 0.0;
		double SmokeCoverage = 0.0;
		double EmissionFraction = 0.0;
		double WindowFillWidthRatio = 0.0;
		double WindowBorderEmissionMax = 0.0;
		for (int32 StepIndex = 0; StepIndex < LingerStep; ++StepIndex)
		{
			StepExplosionRun(Run, StepIndex);
			if (StepIndex + 1 == HotCoreStep)
			{
				HotFraction = MeasureHotFraction(Run);
			}
			if (StepIndex + 1 == SmokeCoverageStep)
			{
				SmokeCoverage = MeasureSmokeCoverage(Run);
			}
			if (StepIndex + 1 == WindowFillStep)
			{
				WindowFillWidthRatio = MeasureWindowEmissionWidthRatio(Run, WindowFillEmissionThreshold);
			}
			if (StepIndex + 1 <= WindowBorderLastStep)
			{
				WindowBorderEmissionMax = FMath::Max(WindowBorderEmissionMax, MeasureWindowBorderEmissionMax(Run));
			}
		}
		EmissionFraction = MeasureEmissionFraction(Run);

		const TCHAR* CaseName = bUsesFx8bValues ? TEXT("FX8b") : TEXT("FX3Legacy");
		const FString Line = FString::Printf(TEXT("TDFluidExplosionTimeline: case=%s hot_fraction_t0_2=%.4f smoke_coverage_t1_2=%.4f emission_fraction_t5=%.4f width_ratio_t0_3=%.4f border_emission_max_t2_5=%.4f"), CaseName, HotFraction, SmokeCoverage, EmissionFraction, WindowFillWidthRatio, WindowBorderEmissionMax);
		UE_LOG(LogTDFluidHeatTests, Display, TEXT("%s"), *Line);
		AddInfo(Line);
		if (!bUsesFx8bValues)
		{
			TestTrue(TEXT("FX-3 legacy hot core area stays below the acceptance threshold"), HotFraction < 0.10);
			continue;
		}

		TestTrue(TEXT("FX-8b hot core area at 0.2 s is at least 5% of the explosion region"), HotFraction >= 0.05);
		TestTrue(TEXT("FX-8b smoke covers at least 30% of the explosion region at 1.2 s"), SmokeCoverage >= 0.30);
		TestTrue(TEXT("FX-8b lingering emission covers at least 1% of the explosion region at 5 s"), EmissionFraction >= 0.01);
		TestTrue(TEXT("FX-8b emission above 0.1 spans at most half of the window width at 0.3 s"), WindowFillWidthRatio <= MaxWindowFillWidthRatio);
		TestTrue(TEXT("FX-8b emission stays below 0.05 in the left, right and bottom 5% window bands through 2.5 s"), WindowBorderEmissionMax < MaxWindowBorderEmission);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidExplosionUndergroundLossTest,
	"TDGame.FluidFX.Explosion.UndergroundLossRatio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidExplosionUndergroundLossTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeatTestPrivate;
	const float Fx8bBurstCenterHeightOffsetCm = UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion).BurstCenterHeightOffsetCm;
	for (const double PitchDegrees : { 0.0, -60.0 })
	{
		const FTDUndergroundHeatShares LegacyShares = MeasureUndergroundHeatShares(PitchDegrees, 0.0f);
		const FTDUndergroundHeatShares Fx8bShares = MeasureUndergroundHeatShares(PitchDegrees, Fx8bBurstCenterHeightOffsetCm);
		const FString Line = FString::Printf(TEXT("TDFluidExplosionUnderground: pitch=%.0f ratio=%.4f legacy_ratio=%.4f window_lost_ratio=%.4f"), PitchDegrees, Fx8bShares.UndergroundRatio, LegacyShares.UndergroundRatio, Fx8bShares.WindowLostRatio);
		UE_LOG(LogTDFluidHeatTests, Display, TEXT("%s"), *Line);
		AddInfo(Line);
		TestTrue(*FString::Printf(TEXT("Burst heat below the ground stays at most 2%% at camera pitch %.0f"), PitchDegrees), Fx8bShares.UndergroundRatio <= MaxUndergroundHeatRatio);
		TestTrue(*FString::Printf(TEXT("Raising the burst center reduces the underground share at camera pitch %.0f"), PitchDegrees), Fx8bShares.UndergroundRatio < LegacyShares.UndergroundRatio);
	}
	return true;
}

#endif
