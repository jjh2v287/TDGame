#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Float16Color.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Tickable.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidWaterWetnessTests, Log, All);

namespace TDFluidWaterWetnessTestsPrivate
{
	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr int32 CoastResolution = 128;
	constexpr float CoastCellSizeCm = 18.75f;
	constexpr double CoastExtentCm = CoastResolution * CoastCellSizeCm;
	constexpr double CoastSlopeDegrees = 5.0;
	constexpr float CoastSeaLevelCm = 0.0f;
	constexpr float BandEdgeToleranceCm = 0.01f;
	constexpr float WhitewaterStepsPerFiveSeconds = 300.0f;
	constexpr int32 FiveSecondStepCount = 300;
	constexpr int32 RetentionResolution = 64;
	constexpr float ActorStepSeconds = 0.05f;
	constexpr int32 ActorGridResolution = 32;
	constexpr float ActorExtentCm = 600.0f;
	constexpr float ActorFloorTopCm = -100.0f;
	constexpr float ActorHighBlockTopCm = 60.0f;
	constexpr float ActorHighBlockCenterXCm = 150.0f;
	constexpr float ActorRegionHalfSizeCm = 250.0f;
	constexpr int32 MaximumWaitTicks = 900;
	constexpr int32 MaximumLoadTicks = 300;
	constexpr float DecalVerticalMarginCm = 30.0f;
	constexpr float DecalPlacementToleranceCm = 1.0f;
	const TCHAR* const WetnessConsoleVariableName = TEXT("TD.FluidFX.WaterWetness");
	const TCHAR* const RowParallelVariableName = TEXT("TD.FluidFX.SolverRowParallel");
	const TCHAR* const EnableWetnessFieldName = TEXT("bEnableWaterWetness");
	const TCHAR* const WhitewaterOverrideFieldName = TEXT("bUseWhitewaterDissipation");
	const TCHAR* const WhitewaterRateFieldName = TEXT("WhitewaterDissipation");
	const TCHAR* const EnableDecalFieldName = TEXT("bEnableWetDecal");
	const TCHAR* const DecalMaterialFieldName = TEXT("WetDecalMaterial");
	const TCHAR* const WetDecalMaterialPath = TEXT("/Game/FluidFX/Materials/M_TD_FluidWetnessDecal.M_TD_FluidWetnessDecal");
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	FString MakeMissingConfigFieldMessage(const FString& Label, const TCHAR* FieldName)
	{
		return FString::Printf(TEXT("%s: FTDFluidSimulationConfig::%s is missing (W-2s-a not implemented)"), *Label, FieldName);
	}

	FString MakeMissingConsoleVariableMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: console variable TD.FluidFX.WaterWetness is not registered (W-2s-a not implemented)"), *Label);
	}

	FString MakeMissingWetDecalMessage(const FString& Label, const TCHAR* MissingName)
	{
		return FString::Printf(TEXT("%s: ATDFluidWaterActor has no wet decal (%s missing, W-2s-a not implemented)"), *Label, MissingName);
	}

	FString MakeZeroWetnessMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: wetness stayed zero where a source exists (UpdateWetness not running)"), *Label);
	}

	bool TrySetConfigBool(FTDFluidSimulationConfig& Config, const TCHAR* FieldName, bool bValue)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(FTDFluidSimulationConfig::StaticStruct(), FieldName);
		if (!Property)
		{
			return false;
		}

		Property->SetPropertyValue_InContainer(&Config, bValue);
		return true;
	}

	bool TrySetConfigFloat(FTDFluidSimulationConfig& Config, const TCHAR* FieldName, float Value)
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(FTDFluidSimulationConfig::StaticStruct(), FieldName);
		if (!Property)
		{
			return false;
		}

		Property->SetPropertyValue_InContainer(&Config, Value);
		return true;
	}

	bool TryGetConfigBool(const FTDFluidSimulationConfig& Config, const TCHAR* FieldName, bool& OutValue)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(FTDFluidSimulationConfig::StaticStruct(), FieldName);
		if (!Property)
		{
			return false;
		}

		OutValue = Property->GetPropertyValue_InContainer(&Config);
		return true;
	}

	bool RequireConfigBool(FAutomationTestBase& Test, const FString& Label, FTDFluidSimulationConfig& Config, const TCHAR* FieldName, bool bValue)
	{
		if (TrySetConfigBool(Config, FieldName, bValue))
		{
			return true;
		}

		Test.AddError(MakeMissingConfigFieldMessage(Label, FieldName));
		return false;
	}

	bool RequireConfigFloat(FAutomationTestBase& Test, const FString& Label, FTDFluidSimulationConfig& Config, const TCHAR* FieldName, float Value)
	{
		if (TrySetConfigFloat(Config, FieldName, Value))
		{
			return true;
		}

		Test.AddError(MakeMissingConfigFieldMessage(Label, FieldName));
		return false;
	}

	struct FTDWetnessConsoleVariableScope
	{
		FTDWetnessConsoleVariableScope(const TCHAR* Name, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetString();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDWetnessConsoleVariableScope()
		{
			if (Variable)
			{
				Variable->Set(*PreviousValue, ECVF_SetByCode);
			}
		}

		FTDWetnessConsoleVariableScope(const FTDWetnessConsoleVariableScope&) = delete;
		FTDWetnessConsoleVariableScope& operator=(const FTDWetnessConsoleVariableScope&) = delete;

		bool IsValid() const
		{
			return Variable != nullptr;
		}

		IConsoleVariable* Variable = nullptr;
		FString PreviousValue;
	};

	struct FTDCellCheckTally
	{
		int32 CheckedCount = 0;
		int32 MismatchCount = 0;
		FString FirstMismatch;

		void Record(bool bMatches, int32 CellX, int32 CellY, float Actual, float Expected)
		{
			++CheckedCount;
			if (bMatches)
			{
				return;
			}

			if (MismatchCount == 0)
			{
				FirstMismatch = FString::Printf(TEXT("cell (%d,%d) actual %.7f expected %.7f"), CellX, CellY, Actual, Expected);
			}
			++MismatchCount;
		}

		bool Finish(FAutomationTestBase& Test, const FString& Label, const FString& Description) const
		{
			if (CheckedCount == 0)
			{
				Test.AddError(FString::Printf(TEXT("%s: %s: no cells were checked"), *Label, *Description));
				return false;
			}

			if (MismatchCount == 0)
			{
				return true;
			}

			Test.AddError(FString::Printf(TEXT("%s: %s: %d of %d cells differ, first %s"), *Label, *Description, MismatchCount, CheckedCount, *FirstMismatch));
			return false;
		}
	};

	float EvaluateCoastTerrainElevationCm(const FVector2D& WorldPosition)
	{
		return static_cast<float>(WorldPosition.X * FMath::Tan(FMath::DegreesToRadians(CoastSlopeDegrees)));
	}

	FTDFluidSimulationConfig MakeCoastConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = CoastResolution;
		Config.SimExtentCm = FVector2D(CoastExtentCm, CoastExtentCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.35f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.ActorBrushDensityStrength = 0.6f;
		Config.MaxPointsPerTick = 512;
		Config.bEnableWaterLayer = true;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.RippleWaveSpeedCmPerSec = 0.0f;
		Config.SwashHeightCm = 0.0f;
		Config.BankWetHeightCm = 0.0f;
		Config.WetnessDryingSeconds = 8.0f;
		Config.WetnessFromWhitewater = 0.5f;
		Config.CoastWavePeriodSeconds = 6.0f;
		return Config;
	}

	void ApplyCoastTerrain(FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, EvaluateCoastTerrainElevationCm(Solver.CellToWorldCenter(CellX, CellY)));
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
	}

	void SetCoastSurface(FTDFluidGrid2DSolver& Solver, float SurfaceHeightCm)
	{
		const double HalfExtentCm = Solver.GetExtentCm().X * 0.5 + 2.0 * CoastCellSizeCm;
		FTDFluidWaterRegion Region;
		Region.MinXY = Solver.GetCenterWorld() - FVector2D(HalfExtentCm, HalfExtentCm);
		Region.MaxXY = Solver.GetCenterWorld() + FVector2D(HalfExtentCm, HalfExtentCm);
		Region.SurfaceHeightCm = SurfaceHeightCm;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(Region);
		Solver.SetWaterRegions(Regions);
	}

	void StartCoast(FTDFluidGrid2DSolver& Solver, const FTDFluidSimulationConfig& Config, float SurfaceHeightCm = CoastSeaLevelCm)
	{
		Solver.Initialize(Config, FVector2D::ZeroVector);
		ApplyCoastTerrain(Solver);
		SetCoastSurface(Solver, SurfaceHeightCm);
	}

	void RunSteps(FTDFluidGrid2DSolver& Solver, int32 StepCount)
	{
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Solver.StepSimulation(StepSeconds);
		}
	}

	struct FCoastCell
	{
		bool bHasSurface = false;
		float DepthCm = 0.0f;
		float HeightAboveSurfaceCm = 0.0f;
	};

	FCoastCell DescribeCoastCell(const FTDFluidGrid2DSolver& Solver, int32 CellX, int32 CellY)
	{
		FCoastCell Cell;
		const float SurfaceHeight = Solver.GetWaterSurfaceHeightAtCell(CellX, CellY);
		Cell.bHasSurface = SurfaceHeight != FTDFluidGrid2DSolver::NoWaterSurfaceHeight;
		if (!Cell.bHasSurface)
		{
			return Cell;
		}

		const float TerrainElevation = EvaluateCoastTerrainElevationCm(Solver.CellToWorldCenter(CellX, CellY));
		Cell.DepthCm = FMath::Max(0.0f, SurfaceHeight - TerrainElevation);
		Cell.HeightAboveSurfaceCm = TerrainElevation - SurfaceHeight;
		return Cell;
	}

	bool IsDryCoastCell(const FCoastCell& Cell)
	{
		return Cell.bHasSurface && Cell.DepthCm <= 0.0f;
	}

	bool IsWetCoastCell(const FCoastCell& Cell)
	{
		return Cell.bHasSurface && Cell.DepthCm > 0.0f;
	}

	float ReadWetness(const FTDFluidGrid2DSolver& Solver, int32 CellX, int32 CellY)
	{
		return Solver.SampleAtCell(CellX, CellY).Wetness;
	}

	float ComputeSwashReachCm(const FTDFluidGrid2DSolver& Solver, float SwashHeightCm, float PeriodSeconds)
	{
		const float Phase = static_cast<float>(FMath::Frac(Solver.GetSimulationTimeSeconds() / static_cast<double>(PeriodSeconds)));
		return SwashHeightCm * FMath::Max(0.0f, FMath::Sin(UE_TWO_PI * Phase));
	}

	bool VerifyCoastHarness(FAutomationTestBase& Test, const FString& Label)
	{
		FTDFluidGrid2DSolver Solver;
		StartCoast(Solver, MakeCoastConfig());
		Solver.StepSimulation(StepSeconds);
		int32 WetCellCount = 0;
		int32 DryCellCount = 0;
		for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
			{
				const FCoastCell Cell = DescribeCoastCell(Solver, CellX, CellY);
				WetCellCount += IsWetCoastCell(Cell) ? 1 : 0;
				DryCellCount += IsDryCoastCell(Cell) ? 1 : 0;
			}
		}

		if (Solver.IsTerrainSamplingCompleteForWater() && WetCellCount > 0 && DryCellCount > 0)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s: coast harness is invalid (terrain complete %d, water cells %d, dry cells %d)"), *Label, Solver.IsTerrainSamplingCompleteForWater() ? 1 : 0, WetCellCount, DryCellCount));
		return false;
	}

	bool HasAnyWetnessAtSources(const FTDFluidGrid2DSolver& Solver)
	{
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				if (IsWetCoastCell(DescribeCoastCell(Solver, CellX, CellY)) && ReadWetness(Solver, CellX, CellY) > 0.0f)
				{
					return true;
				}
			}
		}
		return false;
	}

	int32 FindShoreRowCell(const FTDFluidGrid2DSolver& Solver, int32 RowY)
	{
		for (int32 CellX = 0; CellX + 1 < Solver.GetResolution(); ++CellX)
		{
			if (IsWetCoastCell(DescribeCoastCell(Solver, CellX, RowY)) && IsDryCoastCell(DescribeCoastCell(Solver, CellX + 1, RowY)))
			{
				return CellX;
			}
		}
		return INDEX_NONE;
	}

	struct FWetnessRowParallelScope
	{
		FWetnessRowParallelScope()
			: Variable(IConsoleManager::Get().FindConsoleVariable(RowParallelVariableName))
		{
			if (Variable)
			{
				PreviousValue = Variable->GetInt();
			}
		}

		~FWetnessRowParallelScope()
		{
			SetValue(PreviousValue);
		}

		FWetnessRowParallelScope(const FWetnessRowParallelScope&) = delete;
		FWetnessRowParallelScope& operator=(const FWetnessRowParallelScope&) = delete;

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

	TArray<FTDFluidBrushStamp> MakeShoreOrbitStamps(int32 StepIndex)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StampIndex = 0; StampIndex < 2; ++StampIndex)
		{
			const double Angle = StepIndex * 0.09 + StampIndex * UE_DOUBLE_PI;
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(-150.0 + FMath::Cos(Angle) * 120.0, FMath::Sin(Angle) * 200.0, 0.0);
			Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 300.0;
			Stamp.RadiusCm = 70.0f;
			Stamp.DensityStrength = 0.6f;
			Stamp.VelocityStrength = 1.0f;
			Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			Stamps.Add(Stamp);
		}
		return Stamps;
	}

	struct FWetnessScenarioRun
	{
		uint32 StateChecksum = 0;
		uint32 WaterChecksum = 0;
		TArray<float> Wetness;
	};

	FWetnessScenarioRun RunShoreScenario(const FTDFluidSimulationConfig& Config, int32 RowParallelValue, int32 StepCount)
	{
		FWetnessRowParallelScope RowParallelScope;
		RowParallelScope.SetValue(RowParallelValue);
		FTDFluidGrid2DSolver Solver;
		StartCoast(Solver, Config);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Solver.ApplyBrushStamps(MakeShoreOrbitStamps(StepIndex), StepSeconds);
			Solver.StepSimulation(StepSeconds);
		}

		FWetnessScenarioRun Run;
		Run.StateChecksum = Solver.ComputeStateChecksum();
		Run.WaterChecksum = Solver.ComputeWaterStateChecksum();
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				Run.Wetness.Add(ReadWetness(Solver, CellX, CellY));
			}
		}
		return Run;
	}

	struct FDryingStepPlan
	{
		const TCHAR* Name;
		TArray<float> StepCycle;
		int32 StepCount;
	};

	TArray<FDryingStepPlan> MakeDryingStepPlans()
	{
		TArray<FDryingStepPlan> Plans;
		Plans.Add({ TEXT("dt 1/30"), { 1.0f / 30.0f }, 120 });
		Plans.Add({ TEXT("dt 1/60"), { 1.0f / 60.0f }, 240 });
		Plans.Add({ TEXT("dt 1/144"), { 1.0f / 144.0f }, 576 });
		Plans.Add({ TEXT("dt 1/240"), { 1.0f / 240.0f }, 960 });
		Plans.Add({ TEXT("dt 0.25 clamped"), { 0.25f }, 40 });
		Plans.Add({ TEXT("dt irregular"), { 1.0f / 60.0f, 1.0f / 45.0f, 1.0f / 90.0f, 1.0f / 30.0f, 1.0f / 120.0f }, 220 });
		return Plans;
	}

	bool RunDryingPlan(FAutomationTestBase& Test, const FString& Label, const FDryingStepPlan& Plan, float DryingSeconds)
	{
		FTDFluidSimulationConfig Config = MakeCoastConfig();
		TrySetConfigBool(Config, EnableWetnessFieldName, true);
		Config.WetnessDryingSeconds = DryingSeconds;
		FTDFluidGrid2DSolver Solver;
		StartCoast(Solver, Config);
		Solver.StepSimulation(StepSeconds);
		TArray<uint8> WasWet;
		WasWet.SetNumZeroed(CoastResolution * CoastResolution);
		for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
			{
				WasWet[CellY * CoastResolution + CellX] = IsWetCoastCell(DescribeCoastCell(Solver, CellX, CellY)) ? 1 : 0;
			}
		}

		SetCoastSurface(Solver, CoastSeaLevelCm - 50.0f);
		double ElapsedSeconds = 0.0;
		for (int32 StepIndex = 0; StepIndex < Plan.StepCount; ++StepIndex)
		{
			const float DeltaSeconds = Plan.StepCycle[StepIndex % Plan.StepCycle.Num()];
			ElapsedSeconds += static_cast<double>(FMath::Min(DeltaSeconds, 0.1f));
			Solver.StepSimulation(DeltaSeconds);
		}

		const double ExpectedRetention = FMath::Exp(-ElapsedSeconds / static_cast<double>(DryingSeconds));
		const double Tolerance = 2.0e-7 * Plan.StepCount + 1.0e-6;
		FTDCellCheckTally Tally;
		for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
			{
				if (!WasWet[CellY * CoastResolution + CellX] || !IsDryCoastCell(DescribeCoastCell(Solver, CellX, CellY)))
				{
					continue;
				}

				const float Wetness = ReadWetness(Solver, CellX, CellY);
				Tally.Record(FMath::Abs(Wetness / ExpectedRetention - 1.0) <= Tolerance, CellX, CellY, Wetness, static_cast<float>(ExpectedRetention));
			}
		}
		return Tally.Finish(Test, Label, FString::Printf(TEXT("%s tau %.0f s drying ratio"), Plan.Name, DryingSeconds));
	}

	int32 ComputeExpectedLastBandCellX(const FTDFluidGrid2DSolver& Solver, int32 RowY, float SwashReachCm)
	{
		int32 LastCellX = INDEX_NONE;
		for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
		{
			const FCoastCell Cell = DescribeCoastCell(Solver, CellX, RowY);
			if (IsDryCoastCell(Cell) && Cell.HeightAboveSurfaceCm < SwashReachCm)
			{
				LastCellX = CellX;
			}
		}
		return LastCellX;
	}

	struct FRetentionRatios
	{
		double Primary = 0.0;
		double Selective = 0.0;
		bool bIsValid = false;
	};

	FRetentionRatios MeasureFiveSecondRetention(FTDFluidSimulationConfig Config)
	{
		Config.GridResolution = RetentionResolution;
		Config.SimExtentCm = FVector2D(RetentionResolution * CoastCellSizeCm, RetentionResolution * CoastCellSizeCm);
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.RippleWaveSpeedCmPerSec = 0.0f;
		Config.ActorBrushRippleImpulseCm = 0.0f;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		for (int32 CellY = 0; CellY < RetentionResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < RetentionResolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, -100.0f);
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
		SetCoastSurface(Solver, 0.0f);
		Solver.StepSimulation(StepSeconds);

		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector::ZeroVector;
		Stamp.RadiusCm = 70.0f;
		Stamp.DensityStrength = 0.8f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelG_Blood;
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Add(Stamp);
		Solver.ApplyBrushStamps(Stamps, StepSeconds);

		auto SumDensities = [&Solver](double& OutPrimary, double& OutSelective)
		{
			OutPrimary = 0.0;
			OutSelective = 0.0;
			for (int32 CellY = 0; CellY < RetentionResolution; ++CellY)
			{
				for (int32 CellX = 0; CellX < RetentionResolution; ++CellX)
				{
					const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
					OutPrimary += Sample.PrimaryDensity;
					OutSelective += Sample.SelectiveDensity;
				}
			}
		};

		double PrimaryBefore = 0.0;
		double SelectiveBefore = 0.0;
		SumDensities(PrimaryBefore, SelectiveBefore);
		RunSteps(Solver, FiveSecondStepCount);
		double PrimaryAfter = 0.0;
		double SelectiveAfter = 0.0;
		SumDensities(PrimaryAfter, SelectiveAfter);

		FRetentionRatios Ratios;
		if (PrimaryBefore <= 0.0 || SelectiveBefore <= 0.0)
		{
			return Ratios;
		}

		Ratios.Primary = PrimaryAfter / PrimaryBefore;
		Ratios.Selective = SelectiveAfter / SelectiveBefore;
		Ratios.bIsValid = true;
		return Ratios;
	}

	double ComputeExpectedRetention(double DissipationPerSec)
	{
		return FMath::Pow(1.0 / (1.0 + DissipationPerSec * static_cast<double>(StepSeconds)), static_cast<double>(WhitewaterStepsPerFiveSeconds));
	}

	FTDFluidSimulationConfig MakePresetRetentionConfig(ETDFluidPresetKind Kind)
	{
		return UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
	}

	constexpr int32 GoldenResolution = 192;
	constexpr double GoldenExtentCm = 3600.0;
	constexpr double GoldenCellSizeCm = GoldenExtentCm / GoldenResolution;
	constexpr int32 GoldenStepCount = 240;
	constexpr int32 GoldenFirstShiftStep = 60;
	constexpr int32 GoldenSecondShiftStep = 120;
	constexpr int32 GoldenGuideResetStep = 180;
	constexpr float GoldenStepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 144.0f, 0.1f };
	constexpr int32 GoldenStepSecondsCycleLength = UE_ARRAY_COUNT(GoldenStepSecondsCycle);

	FTDFluidSimulationConfig MakeGoldenSceneConfig()
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

	float EvaluateGoldenTerrainElevationCm(const FVector2D& WorldPosition)
	{
		return static_cast<float>(-150.0 + 0.0006 * WorldPosition.Y * WorldPosition.Y);
	}

	FTDFluidFlowGuidePoint MakeGoldenGuidePoint(double WorldX, double WorldY, float HalfWidthCm, float SpeedCmPerSec)
	{
		FTDFluidFlowGuidePoint Point;
		Point.WorldLocation = FVector(WorldX, WorldY, 0.0);
		Point.HalfWidthCm = HalfWidthCm;
		Point.FlowSpeedCmPerSec = SpeedCmPerSec;
		return Point;
	}

	TArray<FTDFluidFlowGuide> MakeGoldenGuides(double OffsetYCm, float SpeedScale)
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

	struct FGoldenChecksums
	{
		uint32 StateChecksum = 0;
		uint32 WaterChecksum = 0;
		uint32 DensityPressureChecksum = 0;
		uint32 VelocityChecksum = 0;
		uint32 WaterSurfaceChecksum = 0;
		int32 NonZeroWetnessPixelCount = 0;
		int32 WaterSurfacePixelCount = 0;
	};

	uint32 ComputePixelChecksum(const TArray<FFloat16Color>& Pixels)
	{
		return FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color));
	}

	FGoldenChecksums RunGoldenScene(const FTDFluidSimulationConfig& Config)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		for (int32 CellY = 0; CellY < GoldenResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < GoldenResolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, EvaluateGoldenTerrainElevationCm(Solver.CellToWorldCenter(CellX, CellY)));
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
		Solver.SetFlowGuides(MakeGoldenGuides(0.0, 1.0f));
		FTDFluidWaterRegion Region;
		Region.MinXY = FVector2D(700.0, -460.0);
		Region.MaxXY = FVector2D(1500.0, 420.0);
		Region.SurfaceHeightCm = 15.0f;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(Region);
		Solver.SetWaterRegions(Regions);
		FTDFluidSdfObstacle Pillar;
		Pillar.WorldCenter = FVector(-400.0, 120.0, 0.0);
		Pillar.RadiusCm = 90.0f;
		TArray<FTDFluidSdfObstacle> Obstacles;
		Obstacles.Add(Pillar);
		Solver.RasterizeSdfObstacles(Obstacles);

		for (int32 StepIndex = 0; StepIndex < GoldenStepCount; ++StepIndex)
		{
			if (StepIndex == GoldenFirstShiftStep || StepIndex == GoldenSecondShiftStep)
			{
				const FVector2D ShiftCells = StepIndex == GoldenFirstShiftStep ? FVector2D(3.0, -2.0) : FVector2D(-4.0, 1.0);
				Solver.ShiftOriginForSlidingWindow(Solver.GetCenterWorld() + ShiftCells * GoldenCellSizeCm);
				Solver.RasterizeSdfObstacles(Obstacles);
			}
			if (StepIndex == GoldenGuideResetStep)
			{
				Solver.SetFlowGuides(MakeGoldenGuides(120.0, 1.1f));
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

		FGoldenChecksums Checksums;
		Checksums.StateChecksum = Solver.ComputeStateChecksum();
		Checksums.WaterChecksum = Solver.ComputeWaterStateChecksum();
		Checksums.DensityPressureChecksum = ComputePixelChecksum(DensityPressurePixels);
		Checksums.VelocityChecksum = ComputePixelChecksum(VelocityPixels);
		Checksums.WaterSurfaceChecksum = ComputePixelChecksum(WaterSurfacePixels);
		Checksums.WaterSurfacePixelCount = WaterSurfacePixels.Num();
		for (const FFloat16Color& Pixel : WaterSurfacePixels)
		{
			Checksums.NonZeroWetnessPixelCount += Pixel.G.GetFloat() != 0.0f ? 1 : 0;
		}
		return Checksums;
	}

	void ExpectChecksum(FAutomationTestBase& Test, const FString& Label, const TCHAR* ValueName, uint32 ActualValue, uint32 ExpectedValue)
	{
		if (ActualValue == ExpectedValue)
		{
			return;
		}

		Test.AddError(FString::Printf(TEXT("%s: %s differs, expected 0x%08X actual 0x%08X"), *Label, ValueName, ExpectedValue, ActualValue));
	}

	struct FWetnessActorTestWorld
	{
		FWetnessActorTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidWaterWetnessTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FWetnessActorTestWorld()
		{
			if (!World)
			{
				return;
			}

			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		FWetnessActorTestWorld(const FWetnessActorTestWorld&) = delete;
		FWetnessActorTestWorld& operator=(const FWetnessActorTestWorld&) = delete;

		void TickOnce()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, ActorStepSeconds);
			FlushAsyncLoading();
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, ActorStepSeconds);
		}

		void TickSeconds(float TotalSeconds)
		{
			for (float Remaining = TotalSeconds; Remaining > UE_SMALL_NUMBER; Remaining -= ActorStepSeconds)
			{
				TickOnce();
			}
		}

		bool TickUntil(int32 MaximumTicks, TFunctionRef<bool()> Condition)
		{
			for (int32 TickIndex = 0; TickIndex < MaximumTicks; ++TickIndex)
			{
				if (Condition())
				{
					return true;
				}
				TickOnce();
			}
			return Condition();
		}

		UWorld* World = nullptr;
	};

	AActor* SpawnFollowAnchor(UWorld* World, const FVector& Location)
	{
		AActor* Anchor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Anchor)
		{
			return nullptr;
		}

		USceneComponent* AnchorRoot = NewObject<USceneComponent>(Anchor);
		Anchor->AddInstanceComponent(AnchorRoot);
		Anchor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();
		Anchor->SetActorLocation(Location);
		return Anchor;
	}

	AActor* SpawnWaterRegionBox(UWorld* World, float HalfSizeCm, float TopZ)
	{
		AActor* RegionActor = World->SpawnActor<AActor>();
		if (!RegionActor)
		{
			return nullptr;
		}

		UBoxComponent* RegionBox = NewObject<UBoxComponent>(RegionActor);
		RegionActor->AddInstanceComponent(RegionBox);
		RegionActor->SetRootComponent(RegionBox);
		RegionBox->SetBoxExtent(FVector(HalfSizeCm, HalfSizeCm, 50.0f));
		RegionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RegionBox->RegisterComponent();
		RegionActor->SetActorLocation(FVector(0.0, 0.0, TopZ - 50.0f));
		RegionActor->Tags.Add(FName(TEXT("TDFluidWaterRegion")));
		return RegionActor;
	}

	AActor* SpawnCollidingBlock(UWorld* World, UStaticMesh* Mesh, const FVector& Center, const FVector& Scale)
	{
		AActor* BlockActor = World->SpawnActor<AActor>();
		if (!BlockActor)
		{
			return nullptr;
		}

		UStaticMeshComponent* BlockMesh = NewObject<UStaticMeshComponent>(BlockActor);
		BlockActor->AddInstanceComponent(BlockMesh);
		BlockActor->SetRootComponent(BlockMesh);
		BlockMesh->SetMobility(EComponentMobility::Movable);
		BlockMesh->SetStaticMesh(Mesh);
		BlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BlockMesh->SetCollisionObjectType(ECC_WorldStatic);
		BlockMesh->SetCollisionResponseToAllChannels(ECR_Block);
		BlockMesh->SetWorldTransform(FTransform(FRotator::ZeroRotator, Center, Scale));
		BlockMesh->RegisterComponent();
		return BlockActor;
	}

	UTDFluidPresetAsset* MakeActorTestPreset(UObject* Outer)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, ETDFluidPresetKind::River);
		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig.GridResolution = ActorGridResolution;
		Preset->SimulationConfig.SimExtentCm = FVector2D(ActorExtentCm, ActorExtentCm);
		Preset->SimulationConfig.PressureIterations = 8;
		return Preset;
	}

	struct FWetDecalLookups
	{
		const FBoolProperty* EnableProperty = nullptr;
		const FSoftObjectProperty* MaterialProperty = nullptr;

		bool IsComplete() const
		{
			return EnableProperty && MaterialProperty;
		}

		const TCHAR* DescribeMissing() const
		{
			return EnableProperty ? DecalMaterialFieldName : EnableDecalFieldName;
		}
	};

	FWetDecalLookups FindWetDecalLookups()
	{
		FWetDecalLookups Lookups;
		Lookups.EnableProperty = FindFProperty<FBoolProperty>(ATDFluidWaterActor::StaticClass(), EnableDecalFieldName);
		Lookups.MaterialProperty = FindFProperty<FSoftObjectProperty>(ATDFluidWaterActor::StaticClass(), DecalMaterialFieldName);
		return Lookups;
	}

	bool ConfigureWetDecal(const FWetDecalLookups& Lookups, ATDFluidWaterActor* WaterActor)
	{
		if (!Lookups.IsComplete() || !WaterActor)
		{
			return false;
		}

		Lookups.EnableProperty->SetPropertyValue_InContainer(WaterActor, true);
		const TCHAR* ImportEnd = Lookups.MaterialProperty->ImportText_Direct(WetDecalMaterialPath, Lookups.MaterialProperty->ContainerPtrToValuePtr<void>(WaterActor), WaterActor, PPF_None);
		return ImportEnd != nullptr;
	}

	ATDFluidWaterActor* SpawnWetDecalWaterActor(UWorld* World, AActor* Anchor, UTDFluidPresetAsset* Preset, const FWetDecalLookups& Lookups)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
		ATDFluidWaterActor* WaterActor = World->SpawnActorDeferred<ATDFluidWaterActor>(ATDFluidWaterActor::StaticClass(), SpawnTransform);
		if (!WaterActor)
		{
			return nullptr;
		}

		WaterActor->FollowTargetActor = Anchor;
		WaterActor->ViewLookAheadCm = 0.0f;
		ConfigureWetDecal(Lookups, WaterActor);
		WaterActor->FinishSpawning(SpawnTransform);
		WaterActor->InitializeFromPreset(Preset, 0.0f);
		return WaterActor;
	}

	struct FWetDecalScene
	{
		AActor* Anchor = nullptr;
		ATDFluidWaterActor* WaterActor = nullptr;
		UTDFluidPresetAsset* Preset = nullptr;
		FWetDecalLookups Lookups;
	};

	bool BuildWetDecalScene(FAutomationTestBase& Test, const FString& Label, FWetnessActorTestWorld& TestWorld, FWetDecalScene& OutScene)
	{
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		if (!CubeMesh)
		{
			Test.AddError(FString::Printf(TEXT("%s: wet decal harness is invalid (cube mesh missing)"), *Label));
			return false;
		}

		OutScene.Lookups = FindWetDecalLookups();
		OutScene.Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
		SpawnWaterRegionBox(TestWorld.World, ActorRegionHalfSizeCm, 0.0f);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(0.0, 0.0, ActorFloorTopCm - 50.0f), FVector(20.0, 20.0, 1.0));
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(ActorHighBlockCenterXCm, 0.0, ActorHighBlockTopCm - 50.0f), FVector(1.0, 1.0, 1.0));
		OutScene.Preset = MakeActorTestPreset(TestWorld.World);
		OutScene.WaterActor = SpawnWetDecalWaterActor(TestWorld.World, OutScene.Anchor, OutScene.Preset, OutScene.Lookups);
		if (!OutScene.Anchor || !OutScene.WaterActor)
		{
			Test.AddError(FString::Printf(TEXT("%s: wet decal harness is invalid (actors were not spawned)"), *Label));
			return false;
		}

		const bool bIsVisible = TestWorld.TickUntil(MaximumWaitTicks, [&OutScene]() { return OutScene.WaterActor->GetWaterLayerDisplayAlpha() > 0.0f; });
		if (!bIsVisible)
		{
			Test.AddError(FString::Printf(TEXT("%s: wet decal harness is invalid (water layer never became visible)"), *Label));
			return false;
		}

		FTDFluidCellSample CenterSample;
		OutScene.WaterActor->LiveComponent->GetSolver().SampleAtWorldPosition(FVector2D::ZeroVector, CenterSample);
		if (CenterSample.WaterDepthCm <= 0.0f)
		{
			Test.AddError(FString::Printf(TEXT("%s: wet decal harness is invalid (no water at the window center)"), *Label));
			return false;
		}
		return true;
	}

	bool RequireWetDecalProperties(FAutomationTestBase& Test, const FString& Label, const FWetDecalScene& Scene)
	{
		if (Scene.Lookups.IsComplete())
		{
			return true;
		}

		Test.AddError(MakeMissingWetDecalMessage(Label, Scene.Lookups.DescribeMissing()));
		return false;
	}

	UDecalComponent* FindWetDecal(const ATDFluidWaterActor* WaterActor)
	{
		return WaterActor ? WaterActor->FindComponentByClass<UDecalComponent>() : nullptr;
	}

	bool IsDecalShown(const UDecalComponent* Decal)
	{
		return Decal && Decal->IsVisible() && !Decal->bHiddenInGame;
	}

	UMaterialInstanceDynamic* FindDecalInstance(const UDecalComponent* Decal)
	{
		return Decal ? Cast<UMaterialInstanceDynamic>(Decal->GetDecalMaterial()) : nullptr;
	}

	bool WaitForDecalInstance(FAutomationTestBase& Test, const FString& Label, FWetnessActorTestWorld& TestWorld, const FWetDecalScene& Scene)
	{
		const UDecalComponent* Decal = FindWetDecal(Scene.WaterActor);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s: wet decal component"), *Label), Decal))
		{
			return false;
		}

		const bool bIsLoaded = TestWorld.TickUntil(MaximumLoadTicks, [Decal]() { return FindDecalInstance(Decal) != nullptr; });
		if (!bIsLoaded)
		{
			Test.AddError(FString::Printf(TEXT("%s: wet decal material instance was not created within %d ticks"), *Label, MaximumLoadTicks));
		}
		return bIsLoaded;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetnessWaterCellsAreWetTest,
	"TDGame.FluidFX.Water.Wetness.WaterCellsAreWet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetnessWaterCellsAreWetTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Wetness.WaterCellsAreWet");
	if (!VerifyCoastHarness(*this, Label))
	{
		return false;
	}

	FTDFluidSimulationConfig Config = MakeCoastConfig();
	if (!RequireConfigBool(*this, Label, Config, EnableWetnessFieldName, true))
	{
		return false;
	}

	FTDFluidGrid2DSolver Solver;
	StartCoast(Solver, Config);
	Solver.StepSimulation(StepSeconds);
	if (!HasAnyWetnessAtSources(Solver))
	{
		AddError(MakeZeroWetnessMessage(Label));
		return false;
	}

	FTDCellCheckTally Tally;
	for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
		{
			const FCoastCell Cell = DescribeCoastCell(Solver, CellX, CellY);
			const float Wetness = ReadWetness(Solver, CellX, CellY);
			const float Expected = IsWetCoastCell(Cell) ? 1.0f : 0.0f;
			Tally.Record(Wetness == Expected, CellX, CellY, Wetness, Expected);
		}
	}
	return Tally.Finish(*this, Label, TEXT("water cells wet and dry cells dry after one step"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetnessWhitewaterWetsDryNeighborsTest,
	"TDGame.FluidFX.Water.Wetness.WhitewaterWetsDryNeighbors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetnessWhitewaterWetsDryNeighborsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Wetness.WhitewaterWetsDryNeighbors");
	if (!VerifyCoastHarness(*this, Label))
	{
		return false;
	}

	FTDFluidSimulationConfig Config = MakeCoastConfig();
	if (!RequireConfigBool(*this, Label, Config, EnableWetnessFieldName, true))
	{
		return false;
	}

	Config.WetnessFromWhitewater = 0.6f;
	FTDFluidGrid2DSolver Solver;
	StartCoast(Solver, Config);
	Solver.StepSimulation(StepSeconds);
	const int32 RowY = CoastResolution / 2;
	const int32 StampCellX = FindShoreRowCell(Solver, RowY);
	if (StampCellX == INDEX_NONE || StampCellX + 2 >= CoastResolution)
	{
		AddError(FString::Printf(TEXT("%s: coast harness is invalid (no shoreline cell in row %d)"), *Label, RowY));
		return false;
	}

	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = FVector(Solver.CellToWorldCenter(StampCellX, RowY).X, Solver.CellToWorldCenter(StampCellX, RowY).Y, 0.0);
	Stamp.RadiusCm = CoastCellSizeCm * 0.5f;
	Stamp.DensityStrength = 0.8f;
	Stamp.VelocityStrength = 0.0f;
	Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
	TArray<FTDFluidBrushStamp> Stamps;
	Stamps.Add(Stamp);
	Solver.ApplyBrushStamps(Stamps, StepSeconds);

	TArray<float> WhitewaterBeforeStep;
	WhitewaterBeforeStep.SetNumZeroed(CoastResolution * CoastResolution);
	for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
		{
			WhitewaterBeforeStep[CellY * CoastResolution + CellX] = Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
		}
	}

	if (WhitewaterBeforeStep[RowY * CoastResolution + StampCellX] <= 0.0f)
	{
		AddError(FString::Printf(TEXT("%s: coast harness is invalid (the stamp left no whitewater in cell (%d,%d))"), *Label, StampCellX, RowY));
		return false;
	}

	Solver.StepSimulation(StepSeconds);
	FTDCellCheckTally Tally;
	for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
		{
			if (!IsDryCoastCell(DescribeCoastCell(Solver, CellX, CellY)))
			{
				continue;
			}

			float NeighborMaximum = 0.0f;
			const int32 NeighborX[4] = { CellX - 1, CellX + 1, CellX, CellX };
			const int32 NeighborY[4] = { CellY, CellY, CellY - 1, CellY + 1 };
			for (int32 NeighborIndex = 0; NeighborIndex < 4; ++NeighborIndex)
			{
				if (NeighborX[NeighborIndex] < 0 || NeighborX[NeighborIndex] >= CoastResolution || NeighborY[NeighborIndex] < 0 || NeighborY[NeighborIndex] >= CoastResolution)
				{
					continue;
				}
				NeighborMaximum = FMath::Max(NeighborMaximum, WhitewaterBeforeStep[NeighborY[NeighborIndex] * CoastResolution + NeighborX[NeighborIndex]]);
			}

			const float Expected = Config.WetnessFromWhitewater * NeighborMaximum;
			const float Wetness = ReadWetness(Solver, CellX, CellY);
			Tally.Record(FMath::Abs(Wetness - Expected) <= 1.0e-6f, CellX, CellY, Wetness, Expected);
		}
	}
	if (!Tally.Finish(*this, Label, TEXT("dry cell wetness equals WetnessFromWhitewater times the maximum four-neighbor whitewater")))
	{
		return false;
	}

	TestTrue(*FString::Printf(TEXT("%s: the dry cell beside the stamp is wet"), *Label), ReadWetness(Solver, StampCellX + 1, RowY) > 0.0f);
	TestTrue(*FString::Printf(TEXT("%s: the cell touching only diagonally stays dry"), *Label), ReadWetness(Solver, StampCellX + 1, RowY + 1) == 0.0f);
	TestTrue(*FString::Printf(TEXT("%s: the cell two cells from the water stays dry"), *Label), ReadWetness(Solver, StampCellX + 2, RowY) == 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetnessDriesWithTimeConstantTest,
	"TDGame.FluidFX.Water.Wetness.DriesWithTimeConstant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetnessDriesWithTimeConstantTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Wetness.DriesWithTimeConstant");
	if (!VerifyCoastHarness(*this, Label))
	{
		return false;
	}

	FTDFluidSimulationConfig ProbeConfig = MakeCoastConfig();
	if (!RequireConfigBool(*this, Label, ProbeConfig, EnableWetnessFieldName, true))
	{
		return false;
	}

	bool bAllCasesMatch = true;
	for (const FDryingStepPlan& Plan : MakeDryingStepPlans())
	{
		for (const float DryingSeconds : { 8.0f, 30.0f })
		{
			bAllCasesMatch &= RunDryingPlan(*this, Label, Plan, DryingSeconds);
		}
	}
	return bAllCasesMatch;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetnessSwashWetsBandAboveWaterlineTest,
	"TDGame.FluidFX.Water.Wetness.SwashWetsBandAboveWaterline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetnessSwashWetsBandAboveWaterlineTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Wetness.SwashWetsBandAboveWaterline");
	if (!VerifyCoastHarness(*this, Label))
	{
		return false;
	}

	FTDFluidSimulationConfig Config = MakeCoastConfig();
	Config.SwashHeightCm = 25.0f;
	Config.CoastWavePeriodSeconds = 6.0f;
	FTDFluidSimulationConfig EnabledConfig = Config;
	if (!RequireConfigBool(*this, Label, EnabledConfig, EnableWetnessFieldName, true))
	{
		return false;
	}

	FTDFluidGrid2DSolver Solver;
	StartCoast(Solver, Config);
	RunSteps(Solver, 89);
	Solver.UpdateConfig(EnabledConfig);
	Solver.StepSimulation(StepSeconds);

	const float SwashReachCm = ComputeSwashReachCm(Solver, Config.SwashHeightCm, Config.CoastWavePeriodSeconds);
	if (SwashReachCm < 0.99f * Config.SwashHeightCm)
	{
		AddError(FString::Printf(TEXT("%s: coast harness is invalid (swash reach %.4f cm at step 90, expected the 25 cm crest)"), *Label, SwashReachCm));
		return false;
	}

	FTDCellCheckTally Tally;
	for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
		{
			const FCoastCell Cell = DescribeCoastCell(Solver, CellX, CellY);
			if (!IsDryCoastCell(Cell) || FMath::Abs(Cell.HeightAboveSurfaceCm - SwashReachCm) < BandEdgeToleranceCm)
			{
				continue;
			}

			const float Expected = Cell.HeightAboveSurfaceCm < SwashReachCm ? 1.0f : 0.0f;
			const float Wetness = ReadWetness(Solver, CellX, CellY);
			Tally.Record(Wetness == Expected, CellX, CellY, Wetness, Expected);
		}
	}
	if (!Tally.Finish(*this, Label, TEXT("band below the swash reach is wet and the rest is dry at the crest step")))
	{
		return false;
	}

	const int32 RowY = CoastResolution / 2;
	const int32 ExpectedLastCellX = ComputeExpectedLastBandCellX(Solver, RowY, SwashReachCm);
	int32 MeasuredLastCellX = INDEX_NONE;
	for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
	{
		if (IsDryCoastCell(DescribeCoastCell(Solver, CellX, RowY)) && ReadWetness(Solver, CellX, RowY) > 0.5f)
		{
			MeasuredLastCellX = CellX;
		}
	}
	TestEqual(*FString::Printf(TEXT("%s: last wet cell of the band"), *Label), MeasuredLastCellX, ExpectedLastCellX);

	RunSteps(Solver, 179);
	TArray<float> WetnessBeforeStep;
	WetnessBeforeStep.SetNumZeroed(CoastResolution * CoastResolution);
	for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
		{
			WetnessBeforeStep[CellY * CoastResolution + CellX] = ReadWetness(Solver, CellX, CellY);
		}
	}

	Solver.StepSimulation(StepSeconds);
	const float RetainFactor = FMath::Exp(-StepSeconds / Config.WetnessDryingSeconds);
	FTDCellCheckTally DryingTally;
	for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
		{
			if (!IsDryCoastCell(DescribeCoastCell(Solver, CellX, CellY)))
			{
				continue;
			}

			const float Expected = WetnessBeforeStep[CellY * CoastResolution + CellX] * RetainFactor;
			const float Wetness = ReadWetness(Solver, CellX, CellY);
			DryingTally.Record(FMath::Abs(Wetness - Expected) <= 1.0e-7f, CellX, CellY, Wetness, Expected);
		}
	}
	return DryingTally.Finish(*this, Label, TEXT("dry cells only decay at the trough step"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetnessBankBandAlongRiverTest,
	"TDGame.FluidFX.Water.Wetness.BankBandAlongRiver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetnessBankBandAlongRiverTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Wetness.BankBandAlongRiver");
	if (!VerifyCoastHarness(*this, Label))
	{
		return false;
	}

	constexpr float BankWetHeightCm = 10.0f;
	FTDFluidSimulationConfig Config = MakeCoastConfig();
	Config.BankWetHeightCm = BankWetHeightCm;
	Config.SwashHeightCm = 0.0f;
	if (!RequireConfigBool(*this, Label, Config, EnableWetnessFieldName, true))
	{
		return false;
	}

	FTDFluidGrid2DSolver Solver;
	StartCoast(Solver, Config);
	Solver.StepSimulation(StepSeconds);
	if (!HasAnyWetnessAtSources(Solver))
	{
		AddError(MakeZeroWetnessMessage(Label));
		return false;
	}

	FTDCellCheckTally Tally;
	for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
		{
			const FCoastCell Cell = DescribeCoastCell(Solver, CellX, CellY);
			if (!IsDryCoastCell(Cell) || FMath::Abs(Cell.HeightAboveSurfaceCm - BankWetHeightCm) < BandEdgeToleranceCm)
			{
				continue;
			}

			const float Expected = Cell.HeightAboveSurfaceCm < BankWetHeightCm ? 1.0f - Cell.HeightAboveSurfaceCm / BankWetHeightCm : 0.0f;
			const float Wetness = ReadWetness(Solver, CellX, CellY);
			Tally.Record(FMath::Abs(Wetness - Expected) <= 1.0e-6f, CellX, CellY, Wetness, Expected);
		}
	}
	return Tally.Finish(*this, Label, TEXT("bank band falls off linearly with height above the water"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetnessShiftCarriesWetnessWithWorldTest,
	"TDGame.FluidFX.Water.Wetness.ShiftCarriesWetnessWithWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetnessShiftCarriesWetnessWithWorldTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Wetness.ShiftCarriesWetnessWithWorld");
	if (!VerifyCoastHarness(*this, Label))
	{
		return false;
	}

	FTDFluidSimulationConfig Config = MakeCoastConfig();
	Config.SwashHeightCm = 25.0f;
	Config.BankWetHeightCm = 10.0f;
	if (!RequireConfigBool(*this, Label, Config, EnableWetnessFieldName, true))
	{
		return false;
	}

	FTDFluidGrid2DSolver Solver;
	StartCoast(Solver, Config);
	RunSteps(Solver, 150);
	TArray<float> WetnessBeforeShift;
	TArray<FVector2D> CentersBeforeShift;
	int32 PartialCellCount = 0;
	for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
		{
			const float Wetness = ReadWetness(Solver, CellX, CellY);
			WetnessBeforeShift.Add(Wetness);
			CentersBeforeShift.Add(Solver.CellToWorldCenter(CellX, CellY));
			PartialCellCount += (Wetness > 0.0f && Wetness < 1.0f) ? 1 : 0;
		}
	}
	if (PartialCellCount == 0)
	{
		AddError(MakeZeroWetnessMessage(Label));
		return false;
	}

	constexpr int32 ShiftCellsX = 5;
	constexpr int32 ShiftCellsY = -3;
	const FVector2D DesiredCenter = Solver.GetCenterWorld() + FVector2D(ShiftCellsX * CoastCellSizeCm, ShiftCellsY * CoastCellSizeCm);
	if (!TestTrue(*FString::Printf(TEXT("%s: the window shifted"), *Label), Solver.ShiftOriginForSlidingWindow(DesiredCenter)))
	{
		return false;
	}

	const FVector2D ProbeCenterAfterShift = Solver.CellToWorldCenter(10, 20);
	const FVector2D ProbeCenterBeforeShift = CentersBeforeShift[(20 + ShiftCellsY) * CoastResolution + (10 + ShiftCellsX)];
	if (!TestTrue(*FString::Printf(TEXT("%s: shifted cell (10,20) keeps the world position of old cell (%d,%d)"), *Label, 10 + ShiftCellsX, 20 + ShiftCellsY), ProbeCenterAfterShift.Equals(ProbeCenterBeforeShift, 0.01)))
	{
		return false;
	}

	FTDCellCheckTally Tally;
	for (int32 CellY = 0; CellY < CoastResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < CoastResolution; ++CellX)
		{
			const int32 OldX = CellX + ShiftCellsX;
			const int32 OldY = CellY + ShiftCellsY;
			const bool bWasInsideWindow = OldX >= 0 && OldX < CoastResolution && OldY >= 0 && OldY < CoastResolution;
			const float Expected = bWasInsideWindow ? WetnessBeforeShift[OldY * CoastResolution + OldX] : 0.0f;
			const float Wetness = ReadWetness(Solver, CellX, CellY);
			Tally.Record(FMemory::Memcmp(&Wetness, &Expected, sizeof(float)) == 0, CellX, CellY, Wetness, Expected);
		}
	}
	return Tally.Finish(*this, Label, TEXT("wetness moves with the world and new cells are dry"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetnessRowParallelEqualsSerialTest,
	"TDGame.FluidFX.Water.Wetness.RowParallelEqualsSerial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetnessRowParallelEqualsSerialTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Wetness.RowParallelEqualsSerial");
	if (!VerifyCoastHarness(*this, Label))
	{
		return false;
	}

	FTDFluidSimulationConfig Config = MakeCoastConfig();
	Config.VorticityStrength = 0.3f;
	Config.WhitewaterFromVorticity = 0.3f;
	Config.WhitewaterVorticityThresholdPerSec = 2.0f;
	Config.SwashHeightCm = 25.0f;
	Config.BankWetHeightCm = 10.0f;
	if (!RequireConfigBool(*this, Label, Config, EnableWetnessFieldName, true))
	{
		return false;
	}

	if (!TestNotNull(*FString::Printf(TEXT("%s: row parallel console variable"), *Label), IConsoleManager::Get().FindConsoleVariable(RowParallelVariableName)))
	{
		return false;
	}

	constexpr int32 StepCount = 120;
	const FWetnessScenarioRun SerialRun = RunShoreScenario(Config, 0, StepCount);
	const FWetnessScenarioRun ParallelRun = RunShoreScenario(Config, 1, StepCount);
	ExpectChecksum(*this, Label, TEXT("water state checksum"), ParallelRun.WaterChecksum, SerialRun.WaterChecksum);
	ExpectChecksum(*this, Label, TEXT("state checksum"), ParallelRun.StateChecksum, SerialRun.StateChecksum);
	TestTrue(*FString::Printf(TEXT("%s: wetness arrays are bit identical"), *Label), SerialRun.Wetness.Num() == ParallelRun.Wetness.Num() && FMemory::Memcmp(SerialRun.Wetness.GetData(), ParallelRun.Wetness.GetData(), SerialRun.Wetness.Num() * sizeof(float)) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetnessConsoleOffKeepsEnabledGoldenTest,
	"TDGame.FluidFX.Water.Wetness.ConsoleOffKeepsEnabledGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetnessConsoleOffKeepsEnabledGoldenTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Wetness.ConsoleOffKeepsEnabledGolden");
	const FGoldenChecksums Reference = RunGoldenScene(MakeGoldenSceneConfig());
	if (Reference.WaterSurfacePixelCount == 0 || Reference.StateChecksum == 0)
	{
		AddError(FString::Printf(TEXT("%s: golden scene harness is invalid (empty reference run)"), *Label));
		return false;
	}

	FTDFluidSimulationConfig WetnessConfig = MakeGoldenSceneConfig();
	if (!RequireConfigBool(*this, Label, WetnessConfig, EnableWetnessFieldName, true))
	{
		return false;
	}

	FTDWetnessConsoleVariableScope ConsoleScope(WetnessConsoleVariableName, 0);
	if (!ConsoleScope.IsValid())
	{
		AddError(MakeMissingConsoleVariableMessage(Label));
		return false;
	}

	const FGoldenChecksums ConsoleOffRun = RunGoldenScene(WetnessConfig);
	ExpectChecksum(*this, Label, TEXT("state checksum"), ConsoleOffRun.StateChecksum, Reference.StateChecksum);
	ExpectChecksum(*this, Label, TEXT("water checksum"), ConsoleOffRun.WaterChecksum, Reference.WaterChecksum);
	ExpectChecksum(*this, Label, TEXT("density-pressure pixel CRC32"), ConsoleOffRun.DensityPressureChecksum, Reference.DensityPressureChecksum);
	ExpectChecksum(*this, Label, TEXT("velocity pixel CRC32"), ConsoleOffRun.VelocityChecksum, Reference.VelocityChecksum);
	ExpectChecksum(*this, Label, TEXT("water surface pixel CRC32"), ConsoleOffRun.WaterSurfaceChecksum, Reference.WaterSurfaceChecksum);
	TestEqual(*FString::Printf(TEXT("%s: pixels with non-zero wetness"), *Label), ConsoleOffRun.NonZeroWetnessPixelCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetnessPresetsEnableWetnessForRiverAndSeaTest,
	"TDGame.FluidFX.Water.Wetness.PresetsEnableWetnessForRiverAndSea",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetnessPresetsEnableWetnessForRiverAndSeaTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Wetness.PresetsEnableWetnessForRiverAndSea");
	const FTDFluidSimulationConfig HeaderDefaults = FTDFluidSimulationConfig();
	bool bHeaderEnablesWetness = true;
	bool bHeaderUsesWhitewaterOverride = true;
	if (!TryGetConfigBool(HeaderDefaults, EnableWetnessFieldName, bHeaderEnablesWetness))
	{
		AddError(MakeMissingConfigFieldMessage(Label, EnableWetnessFieldName));
		return false;
	}
	if (!TryGetConfigBool(HeaderDefaults, WhitewaterOverrideFieldName, bHeaderUsesWhitewaterOverride))
	{
		AddError(MakeMissingConfigFieldMessage(Label, WhitewaterOverrideFieldName));
		return false;
	}

	TestFalse(*FString::Printf(TEXT("%s: header default of %s"), *Label, EnableWetnessFieldName), bHeaderEnablesWetness);
	TestFalse(*FString::Printf(TEXT("%s: header default of %s"), *Label, WhitewaterOverrideFieldName), bHeaderUsesWhitewaterOverride);

	const FTDFluidSimulationConfig RiverConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::River);
	const FTDFluidSimulationConfig SeaConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Sea);
	bool bRiverEnablesWetness = false;
	bool bSeaEnablesWetness = false;
	bool bRiverUsesWhitewaterOverride = true;
	bool bSeaUsesWhitewaterOverride = false;
	TryGetConfigBool(RiverConfig, EnableWetnessFieldName, bRiverEnablesWetness);
	TryGetConfigBool(SeaConfig, EnableWetnessFieldName, bSeaEnablesWetness);
	TryGetConfigBool(RiverConfig, WhitewaterOverrideFieldName, bRiverUsesWhitewaterOverride);
	TryGetConfigBool(SeaConfig, WhitewaterOverrideFieldName, bSeaUsesWhitewaterOverride);
	TestTrue(*FString::Printf(TEXT("%s: River enables wetness"), *Label), bRiverEnablesWetness);
	TestTrue(*FString::Printf(TEXT("%s: Sea enables wetness"), *Label), bSeaEnablesWetness);
	TestFalse(*FString::Printf(TEXT("%s: River keeps the shared whitewater dissipation"), *Label), bRiverUsesWhitewaterOverride);
	TestTrue(*FString::Printf(TEXT("%s: Sea overrides the whitewater dissipation"), *Label), bSeaUsesWhitewaterOverride);

	const ETDFluidPresetKind NonWaterKinds[] = { ETDFluidPresetKind::Water, ETDFluidPresetKind::Mud, ETDFluidPresetKind::Blood, ETDFluidPresetKind::Fire, ETDFluidPresetKind::Smoke };
	for (const ETDFluidPresetKind Kind : NonWaterKinds)
	{
		bool bEnablesWetness = true;
		TryGetConfigBool(UTDFluidPresetAsset::BuildConfigForPresetKind(Kind), EnableWetnessFieldName, bEnablesWetness);
		TestFalse(*FString::Printf(TEXT("%s: non-water preset kind %d keeps wetness off"), *Label, static_cast<int32>(Kind)), bEnablesWetness);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWhitewaterLaceRetainsHalfAfterFiveSecondsTest,
	"TDGame.FluidFX.Water.Whitewater.LaceRetainsHalfAfterFiveSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWhitewaterLaceRetainsHalfAfterFiveSecondsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Whitewater.LaceRetainsHalfAfterFiveSeconds");
	const FRetentionRatios RiverRatios = MeasureFiveSecondRetention(MakePresetRetentionConfig(ETDFluidPresetKind::River));
	if (!RiverRatios.bIsValid)
	{
		AddError(FString::Printf(TEXT("%s: whitewater harness is invalid (the stamp left no density)"), *Label));
		return false;
	}

	FTDFluidSimulationConfig SeaConfig = MakePresetRetentionConfig(ETDFluidPresetKind::Sea);
	bool bSeaUsesWhitewaterOverride = false;
	if (!TryGetConfigBool(SeaConfig, WhitewaterOverrideFieldName, bSeaUsesWhitewaterOverride))
	{
		AddError(MakeMissingConfigFieldMessage(Label, WhitewaterOverrideFieldName));
		return false;
	}

	const FRetentionRatios SeaRatios = MeasureFiveSecondRetention(SeaConfig);
	UE_LOG(LogTDFluidWaterWetnessTests, Display, TEXT("%s: five second whitewater retention sea %.4f river %.4f"), *Label, SeaRatios.Primary, RiverRatios.Primary);
	TestTrue(*FString::Printf(TEXT("%s: Sea retains between half and 70 percent (%.4f)"), *Label, SeaRatios.Primary), SeaRatios.bIsValid && SeaRatios.Primary >= 0.50 && SeaRatios.Primary <= 0.70);
	TestNearlyEqual(*FString::Printf(TEXT("%s: River keeps the shared dissipation"), *Label), static_cast<float>(RiverRatios.Primary), 0.174f, 0.005f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWhitewaterDyeKeepsDensityDissipationTest,
	"TDGame.FluidFX.Water.Whitewater.DyeKeepsDensityDissipation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWhitewaterDyeKeepsDensityDissipationTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.Whitewater.DyeKeepsDensityDissipation");
	const FTDFluidSimulationConfig BaseConfig = MakePresetRetentionConfig(ETDFluidPresetKind::River);
	const double ExpectedDyeRetention = ComputeExpectedRetention(BaseConfig.DensityDissipation);
	const FRetentionRatios SharedRatios = MeasureFiveSecondRetention(BaseConfig);
	if (!SharedRatios.bIsValid || FMath::Abs(SharedRatios.Selective / ExpectedDyeRetention - 1.0) > 0.005)
	{
		AddError(FString::Printf(TEXT("%s: whitewater harness is invalid (shared dissipation retention %.5f, expected %.5f)"), *Label, SharedRatios.Selective, ExpectedDyeRetention));
		return false;
	}

	constexpr float WhitewaterRatePerSec = 0.12f;
	FTDFluidSimulationConfig OverrideConfig = BaseConfig;
	if (!RequireConfigBool(*this, Label, OverrideConfig, WhitewaterOverrideFieldName, true))
	{
		return false;
	}
	if (!RequireConfigFloat(*this, Label, OverrideConfig, WhitewaterRateFieldName, WhitewaterRatePerSec))
	{
		return false;
	}

	const FRetentionRatios OverrideRatios = MeasureFiveSecondRetention(OverrideConfig);
	const double ExpectedWhitewaterRetention = ComputeExpectedRetention(WhitewaterRatePerSec);
	UE_LOG(LogTDFluidWaterWetnessTests, Display, TEXT("%s: dye %.5f whitewater %.5f expected %.5f %.5f"), *Label, OverrideRatios.Selective, OverrideRatios.Primary, ExpectedDyeRetention, ExpectedWhitewaterRetention);
	TestTrue(*FString::Printf(TEXT("%s: dye keeps the density dissipation (%.5f vs %.5f)"), *Label, OverrideRatios.Selective, ExpectedDyeRetention), OverrideRatios.bIsValid && FMath::Abs(OverrideRatios.Selective / ExpectedDyeRetention - 1.0) <= 0.005);
	TestTrue(*FString::Printf(TEXT("%s: whitewater follows the override rate (%.5f vs %.5f)"), *Label, OverrideRatios.Primary, ExpectedWhitewaterRetention), OverrideRatios.bIsValid && FMath::Abs(OverrideRatios.Primary / ExpectedWhitewaterRetention - 1.0) <= 0.005);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetDecalFollowsWindowTest,
	"TDGame.FluidFX.Water.WetDecal.FollowsWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetDecalFollowsWindowTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.WetDecal.FollowsWindow");
	FWetnessActorTestWorld TestWorld;
	FWetDecalScene Scene;
	if (!BuildWetDecalScene(*this, Label, TestWorld, Scene) || !RequireWetDecalProperties(*this, Label, Scene) || !WaitForDecalInstance(*this, Label, TestWorld, Scene))
	{
		return false;
	}

	const UDecalComponent* Decal = FindWetDecal(Scene.WaterActor);
	Scene.Anchor->SetActorLocation(FVector(240.0, -130.0, 0.0));
	TestWorld.TickSeconds(1.0f);
	const FVector2D WindowCenter = Scene.WaterActor->LiveComponent->GetSimulationCenterWorld();
	const FVector DecalLocation = Decal->GetComponentLocation();
	TestNearlyEqual(*FString::Printf(TEXT("%s: decal X follows the window center"), *Label), static_cast<float>(DecalLocation.X), static_cast<float>(WindowCenter.X), DecalPlacementToleranceCm);
	TestNearlyEqual(*FString::Printf(TEXT("%s: decal Y follows the window center"), *Label), static_cast<float>(DecalLocation.Y), static_cast<float>(WindowCenter.Y), DecalPlacementToleranceCm);
	TestTrue(*FString::Printf(TEXT("%s: the decal is shown"), *Label), IsDecalShown(Decal));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetDecalUsesSingleMIDTest,
	"TDGame.FluidFX.Water.WetDecal.UsesSingleMID",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetDecalUsesSingleMIDTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.WetDecal.UsesSingleMID");
	FWetnessActorTestWorld TestWorld;
	FWetDecalScene Scene;
	if (!BuildWetDecalScene(*this, Label, TestWorld, Scene) || !RequireWetDecalProperties(*this, Label, Scene) || !WaitForDecalInstance(*this, Label, TestWorld, Scene))
	{
		return false;
	}

	TArray<UDecalComponent*> DecalComponents;
	Scene.WaterActor->GetComponents<UDecalComponent>(DecalComponents);
	TestEqual(*FString::Printf(TEXT("%s: decal component count"), *Label), DecalComponents.Num(), 1);
	UMaterialInstanceDynamic* FirstInstance = FindDecalInstance(FindWetDecal(Scene.WaterActor));
	if (!TestNotNull(*FString::Printf(TEXT("%s: decal material instance"), *Label), FirstInstance))
	{
		return false;
	}

	TestEqual(*FString::Printf(TEXT("%s: parent material"), *Label), FirstInstance->Parent ? FirstInstance->Parent->GetPathName() : FString(), FString(WetDecalMaterialPath));
	const UTDFluidLiveComponent* LiveComponent = Scene.WaterActor->LiveComponent;
	TestEqual(*FString::Printf(TEXT("%s: SimGridResolution is driven by the live component"), *Label), FirstInstance->K2_GetScalarParameterValue(TEXT("SimGridResolution")), static_cast<float>(LiveComponent->GetSimulationResolution()));
	if (UTexture* WaterSurfaceOutput = LiveComponent->GetWaterSurfaceOutputTexture())
	{
		TestTrue(*FString::Printf(TEXT("%s: WaterSurfaceTexture is the live component output"), *Label), FirstInstance->K2_GetTextureParameterValue(TEXT("WaterSurfaceTexture")) == WaterSurfaceOutput);
	}
	TestEqual(*FString::Printf(TEXT("%s: the shared surface material table is untouched"), *Label), Scene.WaterActor->GetSharedSurfaceMaterialCount(), 0);

	TestWorld.TickSeconds(2.0f);
	TestTrue(*FString::Printf(TEXT("%s: the same instance is kept"), *Label), FindDecalInstance(FindWetDecal(Scene.WaterActor)) == FirstInstance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetDecalHiddenWhenAsleepTest,
	"TDGame.FluidFX.Water.WetDecal.HiddenWhenAsleep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetDecalHiddenWhenAsleepTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.WetDecal.HiddenWhenAsleep");
	FWetnessActorTestWorld TestWorld;
	FWetDecalScene Scene;
	if (!BuildWetDecalScene(*this, Label, TestWorld, Scene) || !RequireWetDecalProperties(*this, Label, Scene) || !WaitForDecalInstance(*this, Label, TestWorld, Scene))
	{
		return false;
	}

	const UDecalComponent* Decal = FindWetDecal(Scene.WaterActor);
	TestWorld.TickSeconds(0.5f);
	TestTrue(*FString::Printf(TEXT("%s: the decal is shown while the water actor is awake"), *Label), IsDecalShown(Decal));

	Scene.Anchor->SetActorLocation(FVector(6000.0, 0.0, 0.0));
	const bool bFellAsleep = TestWorld.TickUntil(MaximumWaitTicks, [&Scene]() { return !Scene.WaterActor->LiveComponent->IsSimulationAwake(); });
	if (!TestTrue(*FString::Printf(TEXT("%s: the water actor falls asleep without water"), *Label), bFellAsleep))
	{
		return false;
	}

	TestWorld.TickOnce();
	TestFalse(*FString::Printf(TEXT("%s: the decal is hidden once the water actor sleeps"), *Label), IsDecalShown(Decal));

	Scene.Anchor->SetActorLocation(FVector::ZeroVector);
	const bool bWokeUp = TestWorld.TickUntil(MaximumWaitTicks, [&Scene]() { return Scene.WaterActor->LiveComponent->IsSimulationAwake(); });
	if (!TestTrue(*FString::Printf(TEXT("%s: the water actor wakes when water returns"), *Label), bWokeUp))
	{
		return false;
	}

	const bool bShownAgain = TestWorld.TickUntil(MaximumLoadTicks, [Decal]() { return IsDecalShown(Decal); });
	TestTrue(*FString::Printf(TEXT("%s: the decal is shown again after waking"), *Label), bShownAgain);

	ATDFluidWaterActor* DuplicateActor = SpawnWetDecalWaterActor(TestWorld.World, Scene.Anchor, Scene.Preset, Scene.Lookups);
	if (!TestNotNull(*FString::Printf(TEXT("%s: duplicate water actor"), *Label), DuplicateActor))
	{
		return false;
	}

	TestWorld.TickSeconds(1.0f);
	TestFalse(*FString::Printf(TEXT("%s: the duplicate water actor sleeps"), *Label), DuplicateActor->LiveComponent->IsSimulationAwake());
	TestFalse(*FString::Printf(TEXT("%s: the duplicate water actor hides its decal"), *Label), IsDecalShown(FindWetDecal(DuplicateActor)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterWetDecalBoxCoversTerrainRangeTest,
	"TDGame.FluidFX.Water.WetDecal.BoxCoversTerrainRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterWetDecalBoxCoversTerrainRangeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterWetnessTestsPrivate;

	const FString Label = TEXT("TDGame.FluidFX.Water.WetDecal.BoxCoversTerrainRange");
	FWetnessActorTestWorld TestWorld;
	FWetDecalScene Scene;
	if (!BuildWetDecalScene(*this, Label, TestWorld, Scene) || !RequireWetDecalProperties(*this, Label, Scene) || !WaitForDecalInstance(*this, Label, TestWorld, Scene))
	{
		return false;
	}

	TestWorld.TickSeconds(0.5f);
	const UDecalComponent* Decal = FindWetDecal(Scene.WaterActor);
	const FBoxSphereBounds DecalBounds = Decal->CalcBounds(Decal->GetComponentTransform());
	const FVector2D WindowCenter = Scene.WaterActor->LiveComponent->GetSimulationCenterWorld();
	const FVector2D WindowExtent = Scene.WaterActor->LiveComponent->GetSimulationExtentCm();
	TestNearlyEqual(*FString::Printf(TEXT("%s: box minimum Z"), *Label), static_cast<float>(DecalBounds.Origin.Z - DecalBounds.BoxExtent.Z), ActorFloorTopCm - DecalVerticalMarginCm, DecalPlacementToleranceCm);
	TestNearlyEqual(*FString::Printf(TEXT("%s: box maximum Z"), *Label), static_cast<float>(DecalBounds.Origin.Z + DecalBounds.BoxExtent.Z), ActorHighBlockTopCm + DecalVerticalMarginCm, DecalPlacementToleranceCm);
	TestNearlyEqual(*FString::Printf(TEXT("%s: box center X"), *Label), static_cast<float>(DecalBounds.Origin.X), static_cast<float>(WindowCenter.X), DecalPlacementToleranceCm);
	TestNearlyEqual(*FString::Printf(TEXT("%s: box center Y"), *Label), static_cast<float>(DecalBounds.Origin.Y), static_cast<float>(WindowCenter.Y), DecalPlacementToleranceCm);
	TestNearlyEqual(*FString::Printf(TEXT("%s: box half extent X"), *Label), static_cast<float>(DecalBounds.BoxExtent.X), static_cast<float>(WindowExtent.X * 0.5), DecalPlacementToleranceCm);
	TestNearlyEqual(*FString::Printf(TEXT("%s: box half extent Y"), *Label), static_cast<float>(DecalBounds.BoxExtent.Y), static_cast<float>(WindowExtent.Y * 0.5), DecalPlacementToleranceCm);
	return true;
}

#endif
