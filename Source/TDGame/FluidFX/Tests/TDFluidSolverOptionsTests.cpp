#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidKernelPressure.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPlaneTypes.h"
#include "FluidFX/TDFluidSolverOptionRules.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuSolverOptions.h"

namespace TDFluidSolverOptionsTestPrivate
{
	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr float OffsetTolerance = 1.0e-3f;
	const TCHAR* const RowParallelVariableName = TEXT("TD.FluidFX.SolverRowParallel");
	const TCHAR* const CpuPressureMethodVariableName = TEXT("TD.FluidFX.CpuPressureMethod");

	struct FTDFluidSolverOptionsConsoleOverride
	{
		FTDFluidSolverOptionsConsoleOverride(const TCHAR* VariableName, int32 OverrideValue)
			: Variable(IConsoleManager::Get().FindConsoleVariable(VariableName))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetInt();
			Variable->Set(OverrideValue, ECVF_SetByCode);
			bIsApplied = Variable->GetInt() == OverrideValue;
		}

		~FTDFluidSolverOptionsConsoleOverride()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
		bool bIsApplied = false;
	};

	struct FTDFluidKernelTestField
	{
		int32 Resolution = 0;
		TArray<float> Pressure;
		TArray<float> Divergence;
		TArray<uint8> SolidMask;
	};

	struct FTDFluidSolverRunResult
	{
		TArray<FVector2D> Velocities;
		uint32 Checksum = 0;
	};

	FTDFluidSimulationConfig MakeQuietConfig(int32 Resolution, const FVector2D& ExtentCm)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = ExtentCm;
		Config.DensityDissipation = 0.0f;
		Config.VorticityStrength = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		return Config;
	}

	FTDFluidSimulationConfig MakeKernelConfig(int32 Resolution, ETDFluidPressureMethod PressureMethod)
	{
		FTDFluidSimulationConfig Config = MakeQuietConfig(Resolution, FVector2D(1000.0, 1000.0));
		Config.PressureMethod = PressureMethod;
		return Config;
	}

	FTDFluidSimulationConfig MakeAnchoredConfig(double MotionVelocityScale)
	{
		FTDFluidSimulationConfig Config = MakeQuietConfig(128, FVector2D(1000.0, 800.0));
		Config.bAnchorsDensityToActor = true;
		Config.SimAreaMotionVelocityScale = static_cast<float>(MotionVelocityScale);
		return Config;
	}

	FTDFluidBrushStamp MakeSolverStamp(const FVector2D& LocationCm, const FVector2D& VelocityCmPerSec, float RadiusCm, float DensityStrength)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(LocationCm.X, LocationCm.Y, 0.0);
		Stamp.WorldVelocity = FVector(VelocityCmPerSec.X, VelocityCmPerSec.Y, 0.0);
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = DensityStrength;
		Stamp.VelocityStrength = 1.0f;
		return Stamp;
	}

	TArray<FTDFluidBrushStamp> MakeDivergentStamps()
	{
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Add(MakeSolverStamp(FVector2D(-70.0, 0.0), FVector2D(-300.0, 0.0), 60.0f, 0.3f));
		Stamps.Add(MakeSolverStamp(FVector2D(70.0, 0.0), FVector2D(300.0, 0.0), 60.0f, 0.3f));
		Stamps.Add(MakeSolverStamp(FVector2D(0.0, 120.0), FVector2D(0.0, 250.0), 50.0f, 0.3f));
		return Stamps;
	}

	TArray<FTDFluidBrushStamp> MakeSwirlStamps()
	{
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Add(MakeSolverStamp(FVector2D(-200.0, -200.0), FVector2D(300.0, 0.0), 150.0f, 0.5f));
		Stamps.Add(MakeSolverStamp(FVector2D(200.0, -200.0), FVector2D(0.0, 300.0), 150.0f, 0.5f));
		Stamps.Add(MakeSolverStamp(FVector2D(200.0, 200.0), FVector2D(-300.0, 0.0), 150.0f, 0.5f));
		Stamps.Add(MakeSolverStamp(FVector2D(-200.0, 200.0), FVector2D(0.0, -300.0), 150.0f, 0.5f));
		return Stamps;
	}

	TArray<FTDFluidBrushStamp> MakeStillBlobStamp(const FVector2D& LocationCm, float RadiusCm)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Add(MakeSolverStamp(LocationCm, FVector2D::ZeroVector, RadiusCm, 1.0f));
		return Stamps;
	}

	void StepWithStamps(FTDFluidGrid2DSolver& Solver, const TArray<FTDFluidBrushStamp>& Stamps, int32 StepCount)
	{
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Solver.ApplyBrushStamps(Stamps, StepSeconds);
			Solver.StepSimulation(StepSeconds);
		}
	}

	uint32 RunStampedChecksum(const FTDFluidSimulationConfig& Config, int32 StepCount)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		StepWithStamps(Solver, MakeDivergentStamps(), StepCount);
		return Solver.ComputeStateChecksum();
	}

	TArray<FVector2D> CollectVelocities(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		TArray<FVector2D> Velocities;
		Velocities.Reserve(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Velocities.Add(Solver.SampleAtCell(CellX, CellY).VelocityCmPerSec);
			}
		}
		return Velocities;
	}

	TArray<float> CollectDensities(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		TArray<float> Densities;
		Densities.Reserve(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Densities.Add(Solver.SampleAtCell(CellX, CellY).PrimaryDensity);
			}
		}
		return Densities;
	}

	float MeasureMaxDensity(const FTDFluidGrid2DSolver& Solver)
	{
		float MaxDensity = 0.0f;
		for (const float Density : CollectDensities(Solver))
		{
			MaxDensity = FMath::Max(MaxDensity, Density);
		}
		return MaxDensity;
	}

	FVector2D MeasureDensityCentroid(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		double TotalDensity = 0.0;
		FVector2D WeightedSum = FVector2D::ZeroVector;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const double Density = Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
				TotalDensity += Density;
				WeightedSum += Solver.CellToWorldCenter(CellX, CellY) * Density;
			}
		}
		if (TotalDensity <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FVector2D::ZeroVector;
		}
		return WeightedSum / TotalDensity;
	}

	double MeasureMaxSpeed(const TArray<FVector2D>& Velocities)
	{
		double MaxSpeed = 0.0;
		for (const FVector2D& Velocity : Velocities)
		{
			MaxSpeed = FMath::Max(MaxSpeed, Velocity.Size());
		}
		return MaxSpeed;
	}

	double MeasureMeanAbsoluteDivergence(const FTDFluidGrid2DSolver& Solver)
	{
		const int32 Resolution = Solver.GetResolution();
		const TArray<FVector2D> Velocities = CollectVelocities(Solver);
		double DivergenceSum = 0.0;
		int32 CellCount = 0;
		for (int32 CellY = 2; CellY < Resolution - 2; ++CellY)
		{
			for (int32 CellX = 2; CellX < Resolution - 2; ++CellX)
			{
				const double GradientX = Velocities[CellY * Resolution + CellX + 1].X - Velocities[CellY * Resolution + CellX - 1].X;
				const double GradientY = Velocities[(CellY + 1) * Resolution + CellX].Y - Velocities[(CellY - 1) * Resolution + CellX].Y;
				DivergenceSum += FMath::Abs(0.5 * (GradientX + GradientY));
				++CellCount;
			}
		}
		return CellCount > 0 ? DivergenceSum / CellCount : 0.0;
	}

	double MeasureVelocityCosineSimilarity(const TArray<FVector2D>& Left, const TArray<FVector2D>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return 1.0;
		}

		double Dot = 0.0;
		double LeftSquared = 0.0;
		double RightSquared = 0.0;
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			Dot += FVector2D::DotProduct(Left[Index], Right[Index]);
			LeftSquared += Left[Index].SizeSquared();
			RightSquared += Right[Index].SizeSquared();
		}
		if (LeftSquared <= UE_DOUBLE_SMALL_NUMBER || RightSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return 1.0;
		}
		return Dot / FMath::Sqrt(LeftSquared * RightSquared);
	}

	double ComputeReferenceKernelWeight(int32 Offset, int32 RadiusTexels)
	{
		return FMath::Exp(-2.0 * static_cast<double>(Offset * Offset) / static_cast<double>(RadiusTexels * RadiusTexels));
	}

	double ComputeReferenceDivergenceScale(int32 RadiusTexels, double Scale)
	{
		double WeightSum = 0.0;
		double MomentSum = 0.0;
		for (int32 Offset = -RadiusTexels; Offset <= RadiusTexels; ++Offset)
		{
			const double Weight = ComputeReferenceKernelWeight(Offset, RadiusTexels);
			WeightSum += Weight;
			MomentSum += Weight * static_cast<double>(Offset * Offset);
		}
		return 0.5 * (MomentSum / WeightSum) * Scale;
	}

	bool IsKernelFluidCell(const FTDFluidKernelTestField& Field, int32 CellX, int32 CellY)
	{
		if (CellX < 0 || CellX >= Field.Resolution || CellY < 0 || CellY >= Field.Resolution)
		{
			return false;
		}
		return Field.SolidMask[CellY * Field.Resolution + CellX] == 0;
	}

	double ComputeReferenceBlur(const FTDFluidKernelTestField& Field, const TArray<float>& Values, int32 CellX, int32 CellY, int32 RadiusTexels)
	{
		double Numerator = 0.0;
		double Denominator = 0.0;
		for (int32 OffsetY = -RadiusTexels; OffsetY <= RadiusTexels; ++OffsetY)
		{
			for (int32 OffsetX = -RadiusTexels; OffsetX <= RadiusTexels; ++OffsetX)
			{
				const int32 TapX = CellX + OffsetX;
				const int32 TapY = CellY + OffsetY;
				if (!IsKernelFluidCell(Field, TapX, TapY))
				{
					continue;
				}

				const double Weight = ComputeReferenceKernelWeight(OffsetX, RadiusTexels) * ComputeReferenceKernelWeight(OffsetY, RadiusTexels);
				Numerator += Weight * Values[TapY * Field.Resolution + TapX];
				Denominator += Weight;
			}
		}
		return Denominator > 0.0 ? Numerator / Denominator : 0.0;
	}

	double ComputeReferenceKernelPressure(const FTDFluidKernelTestField& Field, int32 CellX, int32 CellY, int32 RadiusTexels, int32 DivergenceRadiusTexels, double Feedback, double DivergenceScale)
	{
		if (!IsKernelFluidCell(Field, CellX, CellY))
		{
			return 0.0;
		}
		return Feedback * ComputeReferenceBlur(Field, Field.Pressure, CellX, CellY, RadiusTexels)
			- DivergenceScale * ComputeReferenceBlur(Field, Field.Divergence, CellX, CellY, DivergenceRadiusTexels);
	}

	FTDFluidKernelTestField MakeRandomKernelField(int32 Resolution, int32 Seed, float SolidFraction, float DivergenceMinimum)
	{
		FRandomStream Random(Seed);
		FTDFluidKernelTestField Field;
		Field.Resolution = Resolution;
		Field.Pressure.SetNumZeroed(Resolution * Resolution);
		Field.Divergence.SetNumZeroed(Resolution * Resolution);
		Field.SolidMask.SetNumZeroed(Resolution * Resolution);
		for (int32 Index = 0; Index < Resolution * Resolution; ++Index)
		{
			Field.Pressure[Index] = Random.FRandRange(-1.0f, 1.0f);
			Field.Divergence[Index] = Random.FRandRange(DivergenceMinimum, 1.0f);
			Field.SolidMask[Index] = Random.FRand() < SolidFraction ? 1 : 0;
		}
		return Field;
	}

	FTDFluidKernelPressureGrid MakeKernelGrid(const FTDFluidKernelTestField& Field, int32 RadiusTexels, int32 DivergenceRadiusTexels, float Feedback, float DivergenceScale)
	{
		FTDFluidKernelPressureGrid Grid;
		Grid.Resolution = Field.Resolution;
		Grid.Pressure = Field.Pressure.GetData();
		Grid.Divergence = Field.Divergence.GetData();
		Grid.SolidMask = Field.SolidMask.GetData();
		Grid.RadiusTexels = RadiusTexels;
		Grid.DivergenceRadiusTexels = DivergenceRadiusTexels;
		Grid.Feedback = Feedback;
		Grid.DivergenceScale = DivergenceScale;
		return Grid;
	}

	void RunKernelPass(FTDFluidKernelPressureGrid Grid, TArray<float>& Pressure, TArray<float>& ScratchPressure, TArray<FVector4f>& Partials)
	{
		const int32 Resolution = Grid.Resolution;
		Grid.Pressure = Pressure.GetData();
		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			TDFluidKernelPressure::ComputeHorizontalRow(Grid, Row, Partials.GetData() + Row * Resolution);
		}
		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			TDFluidKernelPressure::ComputeVerticalRow(Grid, Partials.GetData(), Row, ScratchPressure.GetData() + Row * Resolution);
		}
		Swap(Pressure, ScratchPressure);
	}

	TArray<float> RunKernelPasses(const FTDFluidKernelTestField& Field, int32 RadiusTexels, int32 DivergenceRadiusTexels, float Feedback, float DivergenceScale, int32 PassCount)
	{
		const int32 CellCount = Field.Resolution * Field.Resolution;
		TArray<float> Pressure;
		TArray<float> ScratchPressure;
		TArray<FVector4f> Partials;
		Pressure.SetNumZeroed(CellCount);
		ScratchPressure.SetNumZeroed(CellCount);
		Partials.SetNumZeroed(CellCount);
		const FTDFluidKernelPressureGrid Grid = MakeKernelGrid(Field, RadiusTexels, DivergenceRadiusTexels, Feedback, DivergenceScale);
		for (int32 PassIndex = 0; PassIndex < PassCount; ++PassIndex)
		{
			RunKernelPass(Grid, Pressure, ScratchPressure, Partials);
		}
		return Pressure;
	}

	double MeasureKernelRowError(const FTDFluidKernelTestField& Field, int32 RadiusTexels, int32 DivergenceRadiusTexels, float Feedback, float DivergenceScale)
	{
		const int32 Resolution = Field.Resolution;
		const FTDFluidKernelPressureGrid Grid = MakeKernelGrid(Field, RadiusTexels, DivergenceRadiusTexels, Feedback, DivergenceScale);
		TArray<FVector4f> Partials;
		Partials.SetNumZeroed(Resolution * Resolution);
		TArray<float> Output;
		Output.Init(1.0e30f, Resolution * Resolution);
		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			TDFluidKernelPressure::ComputeHorizontalRow(Grid, Row, Partials.GetData() + Row * Resolution);
		}
		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			TDFluidKernelPressure::ComputeVerticalRow(Grid, Partials.GetData(), Row, Output.GetData() + Row * Resolution);
		}

		double MaxError = 0.0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const double Expected = ComputeReferenceKernelPressure(Field, CellX, CellY, RadiusTexels, DivergenceRadiusTexels, Feedback, DivergenceScale);
				MaxError = FMath::Max(MaxError, FMath::Abs(static_cast<double>(Output[CellY * Resolution + CellX]) - Expected));
			}
		}
		return MaxError;
	}

	FVector2D ComputeOffset(const FTDFluidSimulationConfig& Config, const FTDFluidPlaneBasis& Basis, const FVector2D& PreviousCenter, const FVector2D& CurrentCenter, bool bHasPreviousCenter, float DeltaTimeSeconds)
	{
		FTDFluidAdvectionOffsetInputs Inputs;
		Inputs.Config = &Config;
		Inputs.PlaneBasis = Basis;
		Inputs.PreviousCenterPlane = PreviousCenter;
		Inputs.CurrentCenterPlane = CurrentCenter;
		Inputs.bHasPreviousCenter = bHasPreviousCenter;
		Inputs.DeltaTimeSeconds = DeltaTimeSeconds;
		return TDFluidSolverOptionRules::ComputeAdvectionOffsetPlane(Inputs);
	}

	void ExpectOffset(FAutomationTestBase& Test, const TCHAR* CaseName, const FVector2D& Actual, const FVector2D& Expected)
	{
		Test.TestTrue(FString::Printf(TEXT("%s: offset %s matches %s"), CaseName, *Actual.ToString(), *Expected.ToString()), Actual.Equals(Expected, OffsetTolerance));
	}

	FVector2D MeasureAdvectionOffsetShift(float StepSecondsValue, int32 StepCount, double& OutMaxSpeed)
	{
		FTDFluidSimulationConfig Config = MakeQuietConfig(128, FVector2D(1200.0, 1200.0));
		Config.DirectVelocityCmPerSec = FVector(60.0, 0.0, 0.0);
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.ApplyBrushStamps(MakeStillBlobStamp(FVector2D(-150.0, 0.0), 60.0f), 0.1f);
		const FVector2D CentroidBefore = MeasureDensityCentroid(Solver);
		Solver.SetAdvectionOffsetPlane(FVector2D(60.0, 0.0));
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Solver.StepSimulation(StepSecondsValue);
		}
		OutMaxSpeed = MeasureMaxSpeed(CollectVelocities(Solver));
		return MeasureDensityCentroid(Solver) - CentroidBefore;
	}

	FTDFluidSolverRunResult RunAmplitudeNoiseStep(float DampingPerSec, float ScaleCm)
	{
		FTDFluidSimulationConfig Config = MakeQuietConfig(64, FVector2D(1000.0, 1000.0));
		Config.PressureIterations = 1;
		Config.FlowAmplitudeNoiseDampingPerSec = DampingPerSec;
		Config.FlowAmplitudeNoiseScaleCm = ScaleCm;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.ApplyBrushStamps(MakeSwirlStamps(), 0.1f);
		Solver.StepSimulation(0.1f);
		FTDFluidSolverRunResult Result;
		Result.Velocities = CollectVelocities(Solver);
		Result.Checksum = Solver.ComputeStateChecksum();
		return Result;
	}

	FTDFluidSimulationConfig MakeFlowNoiseConfig(ETDFluidAreaAddressMode AddressMode)
	{
		FTDFluidSimulationConfig Config = MakeQuietConfig(64, FVector2D(2000.0, 2000.0));
		Config.FlowNoiseAccelerationCmPerSec2 = 400.0f;
		Config.FlowNoiseScaleCm = 100.0f;
		Config.bRandomizeNoiseOffsetPerActor = true;
		Config.AreaAddressMode = AddressMode;
		return Config;
	}

	FTDFluidSolverRunResult RunFlowNoiseSteps(const FIntPoint& LatticeOffsetCells, int32 StepCount)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(MakeFlowNoiseConfig(ETDFluidAreaAddressMode::Clamp), FVector2D::ZeroVector);
		Solver.SetNoiseLatticeOffsetCells(LatticeOffsetCells);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Solver.StepSimulation(StepSeconds);
		}
		FTDFluidSolverRunResult Result;
		Result.Velocities = CollectVelocities(Solver);
		Result.Checksum = Solver.ComputeStateChecksum();
		return Result;
	}

	TArray<FVector2D> ComputeWrappedFlowNoiseField(const FIntPoint& LatticeOffsetCells)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(MakeFlowNoiseConfig(ETDFluidAreaAddressMode::Wrap), FVector2D::ZeroVector);
		Solver.SetNoiseLatticeOffsetCells(LatticeOffsetCells);
		TArray<FVector2D> AccelerationField;
		Solver.ComputeFlowNoiseAccelerationField(AccelerationField, true);
		return AccelerationField;
	}

	FIntPoint ComputeSpecLatticeOffset(const FVector& LocationCm, ETDFluidPresetKind Kind, const FString& OwnerName)
	{
		const int32 HashInputs[4] = {
			static_cast<int32>(FMath::RoundToInt(LocationCm.X)),
			static_cast<int32>(FMath::RoundToInt(LocationCm.Y)),
			static_cast<int32>(FMath::RoundToInt(LocationCm.Z)),
			static_cast<int32>(Kind)
		};
		const uint32 Hash = FCrc::MemCrc32(HashInputs, static_cast<int32>(sizeof(HashInputs))) ^ FCrc::StrCrc32(*OwnerName);
		return FIntPoint(static_cast<int32>(Hash & 1023u), static_cast<int32>((Hash >> 10) & 1023u));
	}

	bool IsLatticeOffsetInRange(const FIntPoint& Offset)
	{
		return Offset.X >= 0 && Offset.X <= 1023 && Offset.Y >= 0 && Offset.Y <= 1023;
	}

	struct FTDFluidUnsupportedWindowCase
	{
		const TCHAR* Name;
		FTDFluidSimulationConfig Config;
	};

	struct FTDFluidSanitizeCase
	{
		const TCHAR* Name = TEXT("");
		FTDFluidSimulationConfig Config;
		bool bExpectsKernel = false;
		bool bExpectsForceOptions = false;
		bool bExpectsAnchoring = false;
	};

	FTDFluidSimulationConfig MakeAllOptionsConfig(bool bAnchorsDensity)
	{
		FTDFluidSimulationConfig Config = MakeQuietConfig(256, FVector2D(4000.0, 4000.0));
		Config.PressureMethod = ETDFluidPressureMethod::KernelFeedback;
		Config.FlowAmplitudeNoiseDampingPerSec = 2.0f;
		Config.DirectVelocityCmPerSec = FVector(60.0, 0.0, 0.0);
		Config.bAnchorsDensityToActor = bAnchorsDensity;
		Config.SimAreaMotionVelocityScale = bAnchorsDensity ? 1.0f : 0.0f;
		Config.bRandomizeNoiseOffsetPerActor = true;
		return Config;
	}

	TArray<FTDFluidSanitizeCase> MakeSanitizeCases()
	{
		TArray<FTDFluidSanitizeCase> Cases;
		for (const bool bAnchorsDensity : { false, true })
		{
			FTDFluidSanitizeCase Plain;
			Plain.Name = bAnchorsDensity ? TEXT("PlainAnchored") : TEXT("Plain");
			Plain.Config = MakeAllOptionsConfig(bAnchorsDensity);
			Plain.bExpectsKernel = true;
			Plain.bExpectsForceOptions = true;
			Plain.bExpectsAnchoring = bAnchorsDensity;
			Cases.Add(Plain);

			FTDFluidSanitizeCase Painter;
			Painter.Name = bAnchorsDensity ? TEXT("PainterAnchored") : TEXT("Painter");
			Painter.Config = MakeAllOptionsConfig(bAnchorsDensity);
			Painter.Config.SimMode = ETDFluidSimMode::SimplePainter;
			Cases.Add(Painter);

			FTDFluidSanitizeCase Water;
			Water.Name = bAnchorsDensity ? TEXT("WaterAnchored") : TEXT("Water");
			Water.Config = MakeAllOptionsConfig(bAnchorsDensity);
			Water.Config.bEnableWaterLayer = true;
			Cases.Add(Water);

			FTDFluidSanitizeCase Wrapped;
			Wrapped.Name = bAnchorsDensity ? TEXT("WrappedAnchored") : TEXT("Wrapped");
			Wrapped.Config = MakeAllOptionsConfig(bAnchorsDensity);
			Wrapped.Config.AreaAddressMode = ETDFluidAreaAddressMode::Wrap;
			Wrapped.bExpectsForceOptions = true;
			Cases.Add(Wrapped);

			FTDFluidSanitizeCase NonSquare;
			NonSquare.Name = bAnchorsDensity ? TEXT("NonSquareAnchored") : TEXT("NonSquare");
			NonSquare.Config = MakeAllOptionsConfig(bAnchorsDensity);
			NonSquare.Config.SimExtentCm = FVector2D(4000.0, 3000.0);
			NonSquare.bExpectsForceOptions = true;
			NonSquare.bExpectsAnchoring = bAnchorsDensity;
			Cases.Add(NonSquare);
		}
		return Cases;
	}

	FTDFluidGpuSimulationSettings MakeGpuSettingsForConfig(const FTDFluidSimulationConfig& RawConfig)
	{
		FTDFluidGpuSimulationSettings Settings;
		Settings.SimMode = RawConfig.SimMode == ETDFluidSimMode::SimplePainter ? ETDFluidGpuSimMode::SimplePainter : ETDFluidGpuSimMode::FluidSim;
		Settings.Resolution = RawConfig.GridResolution;
		Settings.ExtentCm = RawConfig.SimExtentCm;
		Settings.AreaAddressModeRequest = RawConfig.AreaAddressMode == ETDFluidAreaAddressMode::Wrap ? TDFluidGpuSolverConstants::WrapAreaAddressModeRequest : static_cast<uint8>(0);
		Settings.bUsesWaterLayer = RawConfig.bEnableWaterLayer;
		Settings.PressureSolverModeRequest = RawConfig.PressureMethod == ETDFluidPressureMethod::KernelFeedback ? TDFluidGpuSolverOptionConstants::KernelFeedbackPressureSolverModeRequest : static_cast<uint8>(0);
		Settings.SolverOptions = TDFluidSolverOptionRules::MakeGpuSolverOptionSettings(RawConfig);
		Settings.SolverOptions.AdvectionOffsetPlaneCmPerSec = FVector2D(60.0, 0.0);
		return Settings;
	}

	bool HasGpuOptionFlag(const FTDFluidGpuSolverOptionParameters& Parameters, ETDFluidGpuSolverOptionFlags Flag)
	{
		return EnumHasAnyFlags(static_cast<ETDFluidGpuSolverOptionFlags>(Parameters.OptionFlags), Flag);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsKernelRowsMatchDirectConvolutionTest,
	"TDGame.FluidFX.SolverOptions.KernelRowsMatchDirectConvolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsKernelRowsMatchDirectConvolutionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	const FTDFluidKernelTestField Field = MakeRandomKernelField(32, 1511, 0.1f, -1.0f);
	const float WideScale = static_cast<float>(ComputeReferenceDivergenceScale(4, 1.0));
	const double WideError = MeasureKernelRowError(Field, 4, 2, 0.98f, WideScale);
	const double NarrowError = MeasureKernelRowError(Field, 7, 1, 0.9f, 0.75f);
	TestTrue(FString::Printf(TEXT("R4 Rd2 rows match the direct normalized product convolution (max error %.3g)"), WideError), WideError <= 1.0e-5);
	TestTrue(FString::Printf(TEXT("R7 Rd1 rows match the direct normalized product convolution (max error %.3g)"), NarrowError), NarrowError <= 1.0e-5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsKernelSteadyStateSolvesPoissonTest,
	"TDGame.FluidFX.SolverOptions.KernelSteadyStateSolvesPoisson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsKernelSteadyStateSolvesPoissonTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	constexpr int32 Resolution = 64;
	constexpr int32 RadiusTexels = 4;
	constexpr int32 DivergenceRadiusTexels = 2;
	FTDFluidKernelTestField Field;
	Field.Resolution = Resolution;
	Field.Pressure.SetNumZeroed(Resolution * Resolution);
	Field.Divergence.SetNumZeroed(Resolution * Resolution);
	Field.SolidMask.SetNumZeroed(Resolution * Resolution);
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			Field.Divergence[CellY * Resolution + CellX] = static_cast<float>(FMath::Cos(UE_DOUBLE_TWO_PI * CellX / Resolution) * FMath::Cos(UE_DOUBLE_TWO_PI * CellY / Resolution));
		}
	}

	const float DivergenceScale = static_cast<float>(ComputeReferenceDivergenceScale(RadiusTexels, 1.0));
	const TArray<float> Pressure = RunKernelPasses(Field, RadiusTexels, DivergenceRadiusTexels, 1.0f, DivergenceScale, 400);

	constexpr int32 Margin = 2 * RadiusTexels;
	double ResidualSquared = 0.0;
	double TargetSquared = 0.0;
	bool bAllFinite = true;
	for (int32 CellY = Margin; CellY < Resolution - Margin; ++CellY)
	{
		for (int32 CellX = Margin; CellX < Resolution - Margin; ++CellX)
		{
			const int32 Index = CellY * Resolution + CellX;
			const double Laplacian = static_cast<double>(Pressure[Index + 1]) + Pressure[Index - 1] + Pressure[Index + Resolution] + Pressure[Index - Resolution] - 4.0 * Pressure[Index];
			const double Target = ComputeReferenceBlur(Field, Field.Divergence, CellX, CellY, DivergenceRadiusTexels);
			bAllFinite &= FMath::IsFinite(Laplacian);
			ResidualSquared += FMath::Square(Laplacian - Target);
			TargetSquared += FMath::Square(Target);
		}
	}

	const double RelativeResidual = TargetSquared > 0.0 ? FMath::Sqrt(ResidualSquared / TargetSquared) : 1.0;
	TestTrue(TEXT("Steady state pressure stays finite"), bAllFinite);
	TestTrue(FString::Printf(TEXT("F=1 steady state solves the Poisson equation inside the 2R border (relative residual %.4f)"), RelativeResidual), RelativeResidual <= 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsKernelFeedbackBoundsPressureTest,
	"TDGame.FluidFX.SolverOptions.KernelFeedbackBoundsPressure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsKernelFeedbackBoundsPressureTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	constexpr float Feedback = 0.98f;
	const FTDFluidKernelTestField Field = MakeRandomKernelField(48, 2203, 0.08f, 0.0f);
	const float DivergenceScale = static_cast<float>(ComputeReferenceDivergenceScale(4, 1.0));
	const TArray<float> Pressure = RunKernelPasses(Field, 4, 2, Feedback, DivergenceScale, 1000);

	double MaxDivergence = 0.0;
	for (const float Divergence : Field.Divergence)
	{
		MaxDivergence = FMath::Max(MaxDivergence, static_cast<double>(FMath::Abs(Divergence)));
	}

	bool bAllFinite = true;
	bool bSolidCellsAreZero = true;
	double MaxPressure = 0.0;
	for (int32 Index = 0; Index < Pressure.Num(); ++Index)
	{
		bAllFinite &= FMath::IsFinite(Pressure[Index]);
		MaxPressure = FMath::Max(MaxPressure, static_cast<double>(FMath::Abs(Pressure[Index])));
		bSolidCellsAreZero &= Field.SolidMask[Index] == 0 || Pressure[Index] == 0.0f;
	}

	const double Bound = DivergenceScale * MaxDivergence / (1.0 - Feedback);
	TestTrue(TEXT("Every pressure value stays finite after 1000 passes"), bAllFinite);
	TestTrue(TEXT("Solid cells keep zero pressure"), bSolidCellsAreZero);
	TestTrue(FString::Printf(TEXT("Max |p| %.4f stays within 1.01 s max|d| / (1 - F) = %.4f"), MaxPressure, 1.01 * Bound), MaxPressure <= 1.01 * Bound);
	TestTrue(FString::Printf(TEXT("Feedback pressure builds up (max |p| %.4f, bound %.4f)"), MaxPressure, Bound), MaxPressure >= 0.1 * Bound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsKernelModeReducesDivergenceTest,
	"TDGame.FluidFX.SolverOptions.KernelModeReducesDivergence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsKernelModeReducesDivergenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	double MeanDivergenceByScale[2] = { 0.0, 0.0 };
	const float KernelScales[2] = { 1.0f, 0.0f };
	for (int32 ScaleIndex = 0; ScaleIndex < 2; ++ScaleIndex)
	{
		FTDFluidSimulationConfig Config = MakeKernelConfig(128, ETDFluidPressureMethod::KernelFeedback);
		Config.PressureKernelScale = KernelScales[ScaleIndex];
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		StepWithStamps(Solver, MakeDivergentStamps(), 60);
		MeanDivergenceByScale[ScaleIndex] = MeasureMeanAbsoluteDivergence(Solver);
	}

	TestTrue(TEXT("The control run keeps measurable divergence"), MeanDivergenceByScale[1] > 1.0e-3);
	TestTrue(FString::Printf(TEXT("Kernel projection mean |div| %.4f is at least 30%% below the scale 0 control %.4f"), MeanDivergenceByScale[0], MeanDivergenceByScale[1]), MeanDivergenceByScale[0] <= 0.7 * MeanDivergenceByScale[1]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsKernelModeParallelEqualsSerialTest,
	"TDGame.FluidFX.SolverOptions.KernelModeParallelEqualsSerial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsKernelModeParallelEqualsSerialTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	const FTDFluidSimulationConfig KernelConfig = MakeKernelConfig(128, ETDFluidPressureMethod::KernelFeedback);
	const FTDFluidSimulationConfig PoissonConfig = MakeKernelConfig(128, ETDFluidPressureMethod::PoissonSolve);
	uint32 SerialChecksum = 0;
	uint32 ParallelChecksum = 0;
	{
		FTDFluidSolverOptionsConsoleOverride RowParallel(RowParallelVariableName, 0);
		TestTrue(TEXT("Row parallel console variable switches to serial"), RowParallel.bIsApplied);
		SerialChecksum = RunStampedChecksum(KernelConfig, 120);
	}
	{
		FTDFluidSolverOptionsConsoleOverride RowParallel(RowParallelVariableName, 1);
		TestTrue(TEXT("Row parallel console variable switches to parallel"), RowParallel.bIsApplied);
		ParallelChecksum = RunStampedChecksum(KernelConfig, 120);
	}

	TestEqual(TEXT("Kernel mode checksum is identical for serial and row parallel runs"), ParallelChecksum, SerialChecksum);
	TestTrue(TEXT("Kernel mode takes a different path than the Poisson solve"), SerialChecksum != RunStampedChecksum(PoissonConfig, 120));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsKernelModeFallsBackOnUnsupportedWindowsTest,
	"TDGame.FluidFX.SolverOptions.KernelModeFallsBackOnUnsupportedWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsKernelModeFallsBackOnUnsupportedWindowsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	constexpr int32 StepCount = 30;
	FTDFluidSimulationConfig NonSquare = MakeQuietConfig(64, FVector2D(1000.0, 800.0));
	FTDFluidSimulationConfig Wrapped = MakeQuietConfig(64, FVector2D(1000.0, 1000.0));
	Wrapped.AreaAddressMode = ETDFluidAreaAddressMode::Wrap;
	FTDFluidSimulationConfig Water = MakeQuietConfig(64, FVector2D(1000.0, 1000.0));
	Water.bEnableWaterLayer = true;

	const FTDFluidUnsupportedWindowCase UnsupportedCases[] = {
		{ TEXT("NonSquare"), NonSquare },
		{ TEXT("Wrapped"), Wrapped },
		{ TEXT("Water"), Water }
	};
	for (const FTDFluidUnsupportedWindowCase& UnsupportedCase : UnsupportedCases)
	{
		FTDFluidSimulationConfig KernelConfig = UnsupportedCase.Config;
		KernelConfig.PressureMethod = ETDFluidPressureMethod::KernelFeedback;
		FTDFluidSimulationConfig PoissonConfig = UnsupportedCase.Config;
		PoissonConfig.PressureMethod = ETDFluidPressureMethod::PoissonSolve;
		TestFalse(FString::Printf(TEXT("%s window cannot use kernel pressure"), UnsupportedCase.Name), TDFluidSolverOptionRules::CanUseKernelPressure(KernelConfig));
		TestEqual(*FString::Printf(TEXT("%s window falls back to the Poisson solve bit for bit"), UnsupportedCase.Name), RunStampedChecksum(KernelConfig, StepCount), RunStampedChecksum(PoissonConfig, StepCount));
	}

	const FTDFluidSimulationConfig SupportedKernel = MakeKernelConfig(64, ETDFluidPressureMethod::KernelFeedback);
	const FTDFluidSimulationConfig SupportedPoisson = MakeKernelConfig(64, ETDFluidPressureMethod::PoissonSolve);
	TestTrue(TEXT("Square clamped fluid window can use kernel pressure"), TDFluidSolverOptionRules::CanUseKernelPressure(SupportedKernel));
	TestTrue(TEXT("Square clamped fluid window takes the kernel path"), RunStampedChecksum(SupportedKernel, StepCount) != RunStampedChecksum(SupportedPoisson, StepCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsCpuPressureMethodConsoleOverrideTest,
	"TDGame.FluidFX.SolverOptions.CpuPressureMethodConsoleOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsCpuPressureMethodConsoleOverrideTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	constexpr int32 StepCount = 40;
	const FTDFluidSimulationConfig PoissonConfig = MakeKernelConfig(64, ETDFluidPressureMethod::PoissonSolve);
	const FTDFluidSimulationConfig KernelConfig = MakeKernelConfig(64, ETDFluidPressureMethod::KernelFeedback);
	uint32 KernelChecksum = 0;
	uint32 PoissonChecksum = 0;
	{
		FTDFluidSolverOptionsConsoleOverride PressureMethod(CpuPressureMethodVariableName, -1);
		TestTrue(TEXT("CPU pressure method console variable exists and accepts -1"), PressureMethod.bIsApplied);
		TestTrue(TEXT("-1 keeps the Poisson preset"), TDFluidSolverOptionRules::ResolveCpuPressureMethod(PoissonConfig) == ETDFluidPressureMethod::PoissonSolve);
		TestTrue(TEXT("-1 keeps the kernel preset"), TDFluidSolverOptionRules::ResolveCpuPressureMethod(KernelConfig) == ETDFluidPressureMethod::KernelFeedback);
		KernelChecksum = RunStampedChecksum(KernelConfig, StepCount);
		PoissonChecksum = RunStampedChecksum(PoissonConfig, StepCount);
	}
	{
		FTDFluidSolverOptionsConsoleOverride PressureMethod(CpuPressureMethodVariableName, 1);
		TestTrue(TEXT("CPU pressure method console variable accepts 1"), PressureMethod.bIsApplied);
		TestTrue(TEXT("1 turns the Poisson preset into kernel feedback"), TDFluidSolverOptionRules::ResolveCpuPressureMethod(PoissonConfig) == ETDFluidPressureMethod::KernelFeedback);
		TestEqual(TEXT("1 runs the Poisson preset through the kernel path"), RunStampedChecksum(PoissonConfig, StepCount), KernelChecksum);
	}
	{
		FTDFluidSolverOptionsConsoleOverride PressureMethod(CpuPressureMethodVariableName, 0);
		TestTrue(TEXT("CPU pressure method console variable accepts 0"), PressureMethod.bIsApplied);
		TestTrue(TEXT("0 turns the kernel preset into the Poisson solve"), TDFluidSolverOptionRules::ResolveCpuPressureMethod(KernelConfig) == ETDFluidPressureMethod::PoissonSolve);
		TestEqual(TEXT("0 runs the kernel preset through the Poisson path"), RunStampedChecksum(KernelConfig, StepCount), PoissonChecksum);
	}

	TestTrue(TEXT("Kernel and Poisson presets differ without an override"), KernelChecksum != PoissonChecksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsAdvectionOffsetMovesCentroidTest,
	"TDGame.FluidFX.SolverOptions.AdvectionOffsetMovesCentroid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsAdvectionOffsetMovesCentroidTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	double MaxSpeed = 0.0;
	const FVector2D Shift = MeasureAdvectionOffsetShift(StepSeconds, 120, MaxSpeed);
	TestTrue(FString::Printf(TEXT("Offset U=(60,0) for 2 s moves the centroid 120 cm +-10%% along X (moved %.2f)"), Shift.X), FMath::Abs(Shift.X - 120.0) <= 12.0);
	TestTrue(FString::Printf(TEXT("The centroid stays on its row (moved %.3f along Y)"), Shift.Y), FMath::Abs(Shift.Y) <= 2.0);
	TestTrue(FString::Printf(TEXT("The offset is not stored in the velocity field (max speed %.4f)"), MaxSpeed), MaxSpeed <= 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsAdvectionOffsetFrameRateInvariantTest,
	"TDGame.FluidFX.SolverOptions.AdvectionOffsetFrameRateInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsAdvectionOffsetFrameRateInvariantTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	double SlowMaxSpeed = 0.0;
	double FastMaxSpeed = 0.0;
	const FVector2D SlowShift = MeasureAdvectionOffsetShift(1.0f / 30.0f, 60, SlowMaxSpeed);
	const FVector2D FastShift = MeasureAdvectionOffsetShift(1.0f / 120.0f, 240, FastMaxSpeed);
	TestTrue(FString::Printf(TEXT("120 Hz run moves the centroid (moved %.2f)"), FastShift.X), FastShift.X >= 60.0 && FastShift.X <= 180.0);
	TestTrue(FString::Printf(TEXT("30 Hz and 120 Hz shifts differ by at most 3%% (%.2f vs %.2f)"), SlowShift.X, FastShift.X), FMath::Abs(SlowShift.X - FastShift.X) <= 0.03 * FMath::Abs(FastShift.X));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsAdvectionOffsetRuleProjectsWorldWindTest,
	"TDGame.FluidFX.SolverOptions.AdvectionOffsetRuleProjectsWorldWind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsAdvectionOffsetRuleProjectsWorldWindTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	const FTDFluidPlaneBasis WorldBasis = FTDFluidPlaneBasis::MakeWorldFacing();
	const FTDFluidPlaneBasis CameraBasis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(-60.0, 20.0, 0.0), 0.0f);
	const FVector Wind(60.0, 30.0, 10.0);
	const FVector2D Origin = FVector2D::ZeroVector;
	const FVector2D SmallMove(12.0, 0.0);

	FTDFluidSimulationConfig WorldWind = MakeQuietConfig(128, FVector2D(1000.0, 800.0));
	WorldWind.DirectVelocityCmPerSec = Wind;
	WorldWind.bDirectVelocityInWorldSpace = true;
	ExpectOffset(*this, TEXT("World wind on a world facing plane"), ComputeOffset(WorldWind, WorldBasis, Origin, Origin, false, StepSeconds), WorldBasis.ProjectVector(Wind));
	ExpectOffset(*this, TEXT("World wind on a world facing plane keeps X and Y"), ComputeOffset(WorldWind, WorldBasis, Origin, Origin, false, StepSeconds), FVector2D(60.0, 30.0));
	ExpectOffset(*this, TEXT("World wind on a camera facing plane"), ComputeOffset(WorldWind, CameraBasis, Origin, Origin, false, StepSeconds), CameraBasis.ProjectVector(Wind));

	FTDFluidSimulationConfig LocalWind = WorldWind;
	LocalWind.bDirectVelocityInWorldSpace = false;
	ExpectOffset(*this, TEXT("Local wind on a camera facing plane"), ComputeOffset(LocalWind, CameraBasis, Origin, Origin, false, StepSeconds), FVector2D(60.0, 30.0));

	FTDFluidSimulationConfig HalfAnchored = MakeAnchoredConfig(0.5);
	HalfAnchored.DirectVelocityCmPerSec = FVector(20.0, 0.0, 0.0);
	ExpectOffset(*this, TEXT("Anchored k=0.5 subtracts half the window velocity"), ComputeOffset(HalfAnchored, WorldBasis, Origin, FVector2D(12.0, 6.0), true, 0.1f), FVector2D(-40.0, -30.0));

	const FTDFluidSimulationConfig FullAnchored = MakeAnchoredConfig(1.0);
	ExpectOffset(*this, TEXT("Anchored k=1 cancels the window velocity"), ComputeOffset(FullAnchored, WorldBasis, Origin, SmallMove, true, 0.1f), FVector2D(-120.0, 0.0));

	FTDFluidSimulationConfig AnchoredWind = MakeAnchoredConfig(1.0);
	AnchoredWind.DirectVelocityCmPerSec = FVector(20.0, 0.0, 0.0);
	const FVector2D WindOnly(20.0, 0.0);
	ExpectOffset(*this, TEXT("A jump beyond half the window is a teleport"), ComputeOffset(AnchoredWind, WorldBasis, Origin, FVector2D(450.0, 0.0), true, 1.0f), WindOnly);
	ExpectOffset(*this, TEXT("A window speed above MaxCellSpeed is a teleport"), ComputeOffset(AnchoredWind, WorldBasis, Origin, FVector2D(30.0, 0.0), true, 0.01f), WindOnly);
	ExpectOffset(*this, TEXT("Without a previous center there is no window velocity"), ComputeOffset(AnchoredWind, WorldBasis, Origin, SmallMove, false, 0.1f), WindOnly);
	ExpectOffset(*this, TEXT("A zero time step has no window velocity"), ComputeOffset(AnchoredWind, WorldBasis, Origin, SmallMove, true, 0.0f), WindOnly);

	FTDFluidSimulationConfig NotAnchored = AnchoredWind;
	NotAnchored.bAnchorsDensityToActor = false;
	ExpectOffset(*this, TEXT("Without anchoring the motion scale has no effect"), ComputeOffset(NotAnchored, WorldBasis, Origin, SmallMove, true, 0.1f), WindOnly);

	FTDFluidSimulationConfig StrongWind = MakeQuietConfig(128, FVector2D(1000.0, 800.0));
	StrongWind.DirectVelocityCmPerSec = FVector(1000.0, 1000.0, 0.0);
	ExpectOffset(*this, TEXT("The offset is clamped to MaxCellSpeed"), ComputeOffset(StrongWind, WorldBasis, Origin, Origin, false, StepSeconds), FVector2D(1.0, 1.0).GetSafeNormal() * StrongWind.MaxCellSpeedCmPerSec);

	FTDFluidSimulationConfig ScaledWind = MakeQuietConfig(128, FVector2D(1000.0, 800.0));
	ScaledWind.DirectVelocityCmPerSec = FVector(60.0, 0.0, 0.0);
	ScaledWind.AdvectionSpeedScale = 2.0f;
	ExpectOffset(*this, TEXT("AdvectionSpeedScale 2 halves the offset"), ComputeOffset(ScaledWind, WorldBasis, Origin, Origin, false, StepSeconds), FVector2D(30.0, 0.0));
	FTDFluidSimulationConfig ScaledAnchored = MakeAnchoredConfig(1.0);
	ScaledAnchored.AdvectionSpeedScale = 2.0f;
	ExpectOffset(*this, TEXT("AdvectionSpeedScale 2 halves the anchored correction"), ComputeOffset(ScaledAnchored, WorldBasis, Origin, SmallMove, true, 0.1f), FVector2D(-60.0, 0.0));

	FTDFluidSimulationConfig PainterAnchored = MakeAnchoredConfig(1.0);
	PainterAnchored.SimMode = ETDFluidSimMode::SimplePainter;
	ExpectOffset(*this, TEXT("Painter windows ignore anchoring"), ComputeOffset(PainterAnchored, WorldBasis, Origin, SmallMove, true, 0.1f), FVector2D::ZeroVector);
	FTDFluidSimulationConfig WaterAnchored = MakeAnchoredConfig(1.0);
	WaterAnchored.bEnableWaterLayer = true;
	ExpectOffset(*this, TEXT("Water windows ignore anchoring"), ComputeOffset(WaterAnchored, WorldBasis, Origin, SmallMove, true, 0.1f), FVector2D::ZeroVector);
	FTDFluidSimulationConfig WrappedAnchored = AnchoredWind;
	WrappedAnchored.AreaAddressMode = ETDFluidAreaAddressMode::Wrap;
	ExpectOffset(*this, TEXT("Wrapped windows ignore anchoring but keep the wind"), ComputeOffset(WrappedAnchored, WorldBasis, Origin, SmallMove, true, 0.1f), WindOnly);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsFlowAmplitudeNoiseOnlyRemovesEnergyTest,
	"TDGame.FluidFX.SolverOptions.FlowAmplitudeNoiseOnlyRemovesEnergy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsFlowAmplitudeNoiseOnlyRemovesEnergyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	const FTDFluidSolverRunResult Baseline = RunAmplitudeNoiseStep(0.0f, 100.0f);
	const FTDFluidSolverRunResult Damped = RunAmplitudeNoiseStep(2.0f, 100.0f);
	const FTDFluidSolverRunResult Disabled = RunAmplitudeNoiseStep(0.0f, 37.0f);
	TestEqual(TEXT("D=0 keeps the baseline checksum whatever the noise size"), Disabled.Checksum, Baseline.Checksum);

	const double MaxSpeed = MeasureMaxSpeed(Baseline.Velocities);
	if (!TestTrue(TEXT("Swirl stamps create velocity"), MaxSpeed > 1.0) || !TestEqual(TEXT("Both runs have the same cell count"), Damped.Velocities.Num(), Baseline.Velocities.Num()))
	{
		return false;
	}

	int32 IncreasedCellCount = 0;
	int32 MovingCellCount = 0;
	int32 ReducedCellCount = 0;
	double BaselineEnergy = 0.0;
	double DampedEnergy = 0.0;
	for (int32 Index = 0; Index < Baseline.Velocities.Num(); ++Index)
	{
		const double BaselineSpeed = Baseline.Velocities[Index].Size();
		const double DampedSpeed = Damped.Velocities[Index].Size();
		BaselineEnergy += FMath::Square(BaselineSpeed);
		DampedEnergy += FMath::Square(DampedSpeed);
		IncreasedCellCount += DampedSpeed > BaselineSpeed + 0.02 * MaxSpeed ? 1 : 0;
		if (BaselineSpeed <= 0.05 * MaxSpeed)
		{
			continue;
		}
		++MovingCellCount;
		ReducedCellCount += DampedSpeed < 0.99 * BaselineSpeed ? 1 : 0;
	}

	TestEqual(TEXT("No cell gains speed beyond the projection tolerance of 2% of the max speed"), IncreasedCellCount, 0);
	TestTrue(FString::Printf(TEXT("Damping removes kinetic energy (%.1f vs %.1f)"), DampedEnergy, BaselineEnergy), DampedEnergy < BaselineEnergy);
	TestTrue(FString::Printf(TEXT("At least 10%% of moving cells lose more than 1%% speed (%d of %d)"), ReducedCellCount, MovingCellCount), ReducedCellCount * 10 >= MovingCellCount && ReducedCellCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsNoiseLatticeOffsetDecorrelatesStackedActorsTest,
	"TDGame.FluidFX.SolverOptions.NoiseLatticeOffsetDecorrelatesStackedActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsNoiseLatticeOffsetDecorrelatesStackedActorsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	const FIntPoint ShiftedOffset(37, 911);
	const FTDFluidSolverRunResult Unshifted = RunFlowNoiseSteps(FIntPoint::ZeroValue, 60);
	const FTDFluidSolverRunResult Shifted = RunFlowNoiseSteps(ShiftedOffset, 60);
	const FTDFluidSolverRunResult Repeated = RunFlowNoiseSteps(ShiftedOffset, 60);
	const double Similarity = MeasureVelocityCosineSimilarity(Unshifted.Velocities, Shifted.Velocities);
	TestTrue(TEXT("Flow noise moves the stacked actors"), MeasureMaxSpeed(Unshifted.Velocities) > 1.0);
	TestTrue(FString::Printf(TEXT("Offset lattices decorrelate the velocity fields (similarity %.3f)"), Similarity), Similarity < 0.3);
	TestEqual(TEXT("The same offset reproduces the same bits"), Repeated.Checksum, Shifted.Checksum);

	const FTDFluidSimulationConfig WrappedConfig = MakeFlowNoiseConfig(ETDFluidAreaAddressMode::Wrap);
	const float SpacingCm = FMath::Max(WrappedConfig.FlowNoiseScaleCm, static_cast<float>(WrappedConfig.SimExtentCm.X) / WrappedConfig.GridResolution);
	const int32 PeriodCells = FMath::Max(1, FMath::RoundToInt(static_cast<float>(WrappedConfig.SimExtentCm.X) / SpacingCm));
	const TArray<FVector2D> WrappedBase = ComputeWrappedFlowNoiseField(FIntPoint::ZeroValue);
	const TArray<FVector2D> WrappedWholePeriod = ComputeWrappedFlowNoiseField(FIntPoint(PeriodCells * 2, PeriodCells * 45));
	const TArray<FVector2D> WrappedShifted = ComputeWrappedFlowNoiseField(ShiftedOffset);
	TestTrue(TEXT("Wrapped flow noise exists"), MeasureMaxSpeed(WrappedBase) > 0.0);
	TestTrue(TEXT("A whole period offset keeps the wrapped field bit for bit"), WrappedWholePeriod == WrappedBase);
	TestTrue(TEXT("A non period offset changes the wrapped field"), WrappedShifted != WrappedBase);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsNoiseLatticeOffsetRuleIsDeterministicTest,
	"TDGame.FluidFX.SolverOptions.NoiseLatticeOffsetRuleIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsNoiseLatticeOffsetRuleIsDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;
	using TDFluidSolverOptionRules::ComputeNoiseLatticeOffsetCells;

	const FVector Location(1234.4, -567.6, 89.5);
	const FString OwnerName = TEXT("BP_TDFluidCauldron_2");
	const FIntPoint Base = ComputeNoiseLatticeOffsetCells(Location, ETDFluidPresetKind::Smoke, OwnerName);
	const FIntPoint Repeated = ComputeNoiseLatticeOffsetCells(Location, ETDFluidPresetKind::Smoke, OwnerName);
	const FIntPoint OtherKind = ComputeNoiseLatticeOffsetCells(Location, ETDFluidPresetKind::SmokeWindDrift, OwnerName);
	const FIntPoint OtherOwner = ComputeNoiseLatticeOffsetCells(Location, ETDFluidPresetKind::Smoke, TEXT("BP_TDFluidCauldron_3"));
	const FIntPoint OtherLocation = ComputeNoiseLatticeOffsetCells(Location + FVector(100.0, 0.0, 0.0), ETDFluidPresetKind::Smoke, OwnerName);

	TestTrue(TEXT("Same inputs give the same offset"), Base == Repeated);
	TestTrue(TEXT("Another preset kind gives another offset"), OtherKind != Base);
	TestTrue(TEXT("Another owner name gives another offset"), OtherOwner != Base);
	TestTrue(TEXT("Another location gives another offset"), OtherLocation != Base);
	TestTrue(TEXT("Offsets stay in 0..1023"), IsLatticeOffsetInRange(Base) && IsLatticeOffsetInRange(OtherKind) && IsLatticeOffsetInRange(OtherOwner) && IsLatticeOffsetInRange(OtherLocation));
	TestTrue(TEXT("Offset follows the CRC rule of the spec"), Base == ComputeSpecLatticeOffset(Location, ETDFluidPresetKind::Smoke, OwnerName));
	TestTrue(TEXT("Owner variant follows the CRC rule of the spec"), OtherOwner == ComputeSpecLatticeOffset(Location, ETDFluidPresetKind::Smoke, TEXT("BP_TDFluidCauldron_3")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsAnchoredWindowCarriesDensityWithoutResampleTest,
	"TDGame.FluidFX.SolverOptions.AnchoredWindowCarriesDensityWithoutResample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsAnchoredWindowCarriesDensityWithoutResampleTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	FTDFluidSimulationConfig Config = MakeQuietConfig(128, FVector2D(1200.0, 1200.0));
	Config.bAnchorsDensityToActor = true;
	Config.SimAreaMotionVelocityScale = 1.0f;
	const FVector2D InitialCenter(300.0, -200.0);
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, InitialCenter);
	TArray<FTDFluidBrushStamp> Stamps;
	Stamps.Add(MakeSolverStamp(InitialCenter, FVector2D(40.0, 0.0), 80.0f, 1.0f));
	Solver.ApplyBrushStamps(Stamps, 0.1f);
	Solver.StepSimulation(StepSeconds);

	const TArray<float> DensitiesBefore = CollectDensities(Solver);
	const TArray<FVector2D> VelocitiesBefore = CollectVelocities(Solver);
	const double CellSizeCm = Solver.GetExtentCm().X / Solver.GetResolution();
	const FVector2D NewCenter = Solver.GetCenterWorld() + FVector2D(3.4 * CellSizeCm, 0.0);
	TestTrue(TEXT("The stamp wrote density"), MeasureMaxDensity(Solver) > 0.1f);
	TestTrue(TEXT("An anchored clamped window moves without a shift"), Solver.MoveWindowWithoutShift(NewCenter));
	TestTrue(TEXT("The window center is exactly the requested center"), Solver.GetCenterWorld() == NewCenter);
	TestTrue(TEXT("Density bits are untouched"), CollectDensities(Solver) == DensitiesBefore);
	TestTrue(TEXT("Velocity bits are untouched"), CollectVelocities(Solver) == VelocitiesBefore);

	FTDFluidGrid2DSolver Uninitialized;
	TestFalse(TEXT("An uninitialized solver refuses to move"), Uninitialized.MoveWindowWithoutShift(NewCenter));

	FTDFluidSimulationConfig WrappedConfig = Config;
	WrappedConfig.AreaAddressMode = ETDFluidAreaAddressMode::Wrap;
	FTDFluidGrid2DSolver Wrapped;
	Wrapped.Initialize(WrappedConfig, InitialCenter);
	TestFalse(TEXT("A wrapped solver refuses to move"), Wrapped.MoveWindowWithoutShift(NewCenter));
	TestTrue(TEXT("A wrapped solver keeps its center"), Wrapped.GetCenterWorld() == InitialCenter);

	FTDFluidSimulationConfig WaterConfig = Config;
	WaterConfig.bEnableWaterLayer = true;
	FTDFluidGrid2DSolver Water;
	Water.Initialize(WaterConfig, InitialCenter);
	TestFalse(TEXT("A water solver refuses to move"), Water.MoveWindowWithoutShift(NewCenter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsAnchoredWindowWithFullAreaMotionKeepsWorldCentroidTest,
	"TDGame.FluidFX.SolverOptions.AnchoredWindowWithFullAreaMotionKeepsWorldCentroid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsAnchoredWindowWithFullAreaMotionKeepsWorldCentroidTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	FTDFluidSimulationConfig Config = MakeQuietConfig(128, FVector2D(1200.0, 1200.0));
	Config.bAnchorsDensityToActor = true;
	Config.SimAreaMotionVelocityScale = 1.0f;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	Solver.ApplyBrushStamps(MakeStillBlobStamp(FVector2D::ZeroVector, 80.0f), 0.1f);

	const double CellSizeCm = Solver.GetExtentCm().X / Solver.GetResolution();
	const FVector2D StartCenter = Solver.GetCenterWorld();
	const FVector2D CentroidBefore = MeasureDensityCentroid(Solver);
	const float MaxDensityBefore = MeasureMaxDensity(Solver);
	const FVector2D StepMotion(120.0 * StepSeconds, 0.0);
	const FTDFluidPlaneBasis WorldBasis = FTDFluidPlaneBasis::MakeWorldFacing();
	FVector2D PreviousCenter = StartCenter;
	for (int32 StepIndex = 0; StepIndex < 120; ++StepIndex)
	{
		const FVector2D CurrentCenter = PreviousCenter + StepMotion;
		const FVector2D AdvectionOffset = ComputeOffset(Solver.GetConfig(), WorldBasis, PreviousCenter, CurrentCenter, true, StepSeconds);
		Solver.MoveWindowWithoutShift(CurrentCenter);
		Solver.SetAdvectionOffsetPlane(AdvectionOffset);
		Solver.StepSimulation(StepSeconds);
		PreviousCenter = CurrentCenter;
	}

	const FVector2D CenterMotion = Solver.GetCenterWorld() - StartCenter;
	const double CentroidDrift = FVector2D::Distance(MeasureDensityCentroid(Solver), CentroidBefore);
	const float MaxDensityAfter = MeasureMaxDensity(Solver);
	TestTrue(FString::Printf(TEXT("The anchored window travelled 240 cm (travelled %s)"), *CenterMotion.ToString()), CenterMotion.Equals(FVector2D(240.0, 0.0), 1.0e-3));
	TestTrue(FString::Printf(TEXT("The density world centroid drifts at most one cell (%.3f cm, cell %.3f cm)"), CentroidDrift, CellSizeCm), CentroidDrift <= CellSizeCm);
	TestTrue(FString::Printf(TEXT("Resampling blurs the peak (%.5f -> %.5f)"), MaxDensityBefore, MaxDensityAfter), MaxDensityAfter < MaxDensityBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsSanitizeRulesMatchBetweenBackendsTest,
	"TDGame.FluidFX.SolverOptions.SanitizeRulesMatchBetweenBackends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsSanitizeRulesMatchBetweenBackendsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsTestPrivate;

	const FTDFluidPlaneBasis WorldBasis = FTDFluidPlaneBasis::MakeWorldFacing();
	for (const FTDFluidSanitizeCase& SanitizeCase : MakeSanitizeCases())
	{
		FTDFluidSimulationConfig Sanitized = SanitizeCase.Config;
		TDFluidSolverOptionRules::SanitizeSolverOptions(Sanitized);
		const bool bCpuKernel = Sanitized.PressureMethod == ETDFluidPressureMethod::KernelFeedback && TDFluidSolverOptionRules::CanUseKernelPressure(Sanitized);
		const bool bCpuAmplitude = Sanitized.FlowAmplitudeNoiseDampingPerSec > 0.0f;
		const bool bCpuOffset = !ComputeOffset(Sanitized, WorldBasis, FVector2D::ZeroVector, FVector2D::ZeroVector, false, StepSeconds).IsNearlyZero(OffsetTolerance);
		const bool bCpuAnchoring = Sanitized.bAnchorsDensityToActor;

		const FTDFluidGpuSimulationSettings GpuSettings = MakeGpuSettingsForConfig(SanitizeCase.Config);
		const FTDFluidGpuSolverOptionParameters GpuParameters = MakeTDFluidGpuSolverOptionParameters(GpuSettings, FVector2D::ZeroVector, StepSeconds, GpuSettings.Resolution);
		const bool bGpuKernel = FTDFluidGpuBatch::MakeKey(GpuSettings).PressureSolver == ETDFluidGpuPressureSolver::KernelFeedback;
		const bool bGpuAmplitude = HasGpuOptionFlag(GpuParameters, ETDFluidGpuSolverOptionFlags::UsesFlowAmplitudeNoise);
		const bool bGpuOffset = HasGpuOptionFlag(GpuParameters, ETDFluidGpuSolverOptionFlags::UsesAdvectionOffset);

		TestEqual(*FString::Printf(TEXT("%s: CPU kernel rule"), SanitizeCase.Name), bCpuKernel, SanitizeCase.bExpectsKernel);
		TestEqual(*FString::Printf(TEXT("%s: GPU kernel batch key"), SanitizeCase.Name), bGpuKernel, SanitizeCase.bExpectsKernel);
		TestEqual(*FString::Printf(TEXT("%s: CPU amplitude noise rule"), SanitizeCase.Name), bCpuAmplitude, SanitizeCase.bExpectsForceOptions);
		TestEqual(*FString::Printf(TEXT("%s: GPU amplitude noise flag"), SanitizeCase.Name), bGpuAmplitude, SanitizeCase.bExpectsForceOptions);
		TestEqual(*FString::Printf(TEXT("%s: CPU advection offset rule"), SanitizeCase.Name), bCpuOffset, SanitizeCase.bExpectsForceOptions);
		TestEqual(*FString::Printf(TEXT("%s: GPU advection offset flag"), SanitizeCase.Name), bGpuOffset, SanitizeCase.bExpectsForceOptions);
		TestEqual(*FString::Printf(TEXT("%s: CPU anchoring rule"), SanitizeCase.Name), bCpuAnchoring, SanitizeCase.bExpectsAnchoring);
		TestFalse(FString::Printf(TEXT("%s: GPU entry never sets the kernel bit"), SanitizeCase.Name), HasGpuOptionFlag(GpuParameters, ETDFluidGpuSolverOptionFlags::UsesKernelPressure));
	}
	return true;
}

#endif
