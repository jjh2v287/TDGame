#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuStampBins.h"

namespace TDFluidGpuWaterModuleTestPrivate
{
	const TCHAR* const WaterVelocityFloat32VariableName = TEXT("TD.FluidFX.WaterGpuVelocityFloat32");
	const TCHAR* const WaterRippleFloat32VariableName = TEXT("TD.FluidFX.WaterGpuRippleFloat32");
	const TCHAR* const WaterPressureModeVariableName = TEXT("TD.FluidFX.WaterGpuPressureMode");
	const TCHAR* const WaterGpuPressureSolverVariableName = TEXT("TD.FluidFX.GpuPressureSolver");
	const TCHAR* const WaterGpuPressureCyclesVariableName = TEXT("TD.FluidFX.GpuPressureCycles");
	constexpr int32 WaterAutomaticFormatRequest = -1;
	constexpr int32 WaterFloat16FormatRequest = 0;
	constexpr int32 WaterFloat32FormatRequest = 1;
	constexpr int32 WaterForcedJacobiPressureSolverMode = 1;
	constexpr int32 WaterForcedMultigridPressureSolverMode = 2;
	constexpr int32 WaterTestPressureIterations = 12;
	constexpr double WaterTestExtentCm = 3600.0;
	constexpr float WaterTestRippleWaveSpeedCmPerSec = 120.0f;
	constexpr float WaterTestSixtyHertzSeconds = 1.0f / 60.0f;
	constexpr int64 WaterTestExpectedReadbackBytes = 98304 + 12288;

	class FTDWaterGpuConsoleVariableScope
	{
	public:
		FTDWaterGpuConsoleVariableScope(const TCHAR* Name, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetInt();
			Variable->Set(Value, ECVF_SetByCode);
			bIsApplied = Variable->GetInt() == Value;
		}

		~FTDWaterGpuConsoleVariableScope()
		{
			if (!Variable)
			{
				return;
			}

			Variable->Set(PreviousValue, ECVF_SetByCode);
		}

		FTDWaterGpuConsoleVariableScope(const FTDWaterGpuConsoleVariableScope&) = delete;
		FTDWaterGpuConsoleVariableScope& operator=(const FTDWaterGpuConsoleVariableScope&) = delete;

		bool IsValid() const
		{
			return Variable != nullptr && bIsApplied;
		}

	private:
		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
		bool bIsApplied = false;
	};

	FTDFluidGpuSimulationSettings MakeWaterSettings(int32 Resolution)
	{
		FTDFluidGpuSimulationSettings Settings;
		Settings.Resolution = Resolution;
		Settings.ExtentCm = FVector2D(WaterTestExtentCm, WaterTestExtentCm);
		Settings.PressureIterations = WaterTestPressureIterations;
		Settings.bUsesWaterLayer = true;
		return Settings;
	}

	FTDFluidGpuActorInput MakeWaterActor(uint64 ActorKey, int32 Resolution)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ActorKey;
		Actor.Settings = MakeWaterSettings(Resolution);
		Actor.DeltaTimeSeconds = WaterTestSixtyHertzSeconds;
		Actor.WindowCenterPlaneCm = FVector2D(100.0 * static_cast<double>(ActorKey), 0.0);
		return Actor;
	}

	FTDFluidGpuActorInput MakeNonWaterActor(uint64 ActorKey, int32 Resolution)
	{
		FTDFluidGpuActorInput Actor = MakeWaterActor(ActorKey, Resolution);
		Actor.Settings.bUsesWaterLayer = false;
		return Actor;
	}

	FTDFluidGpuBatchKey MakeWaterKey(int32 ResolutionTier, ETDFluidGpuWaterPressureMode PressureMode, bool bUsesFloat32WaterVelocity, bool bUsesFloat32Ripple)
	{
		FTDFluidGpuBatchKey Key;
		Key.ResolutionTier = ResolutionTier;
		Key.bUsesWaterLayer = true;
		Key.WaterPressureMode = PressureMode;
		Key.bUsesFloat32WaterVelocity = bUsesFloat32WaterVelocity;
		Key.bUsesFloat32Ripple = bUsesFloat32Ripple;
		return Key;
	}

	const FTDFluidGpuBatch* FindBatchByWaterLayer(const TArray<FTDFluidGpuBatch>& Batches, bool bUsesWaterLayer, int32 ResolutionTier)
	{
		return Batches.FindByPredicate([bUsesWaterLayer, ResolutionTier](const FTDFluidGpuBatch& Batch)
		{
			return Batch.GetKey().bUsesWaterLayer == bUsesWaterLayer && Batch.GetKey().ResolutionTier == ResolutionTier;
		});
	}

	bool HasWaterFlag(const FTDFluidGpuWaterParameters& WaterParameters, ETDFluidGpuWaterFlags Flag)
	{
		return EnumHasAnyFlags(static_cast<ETDFluidGpuWaterFlags>(WaterParameters.WaterFlags), Flag);
	}

	struct FTDWaterMemoryTableRow
	{
		const TCHAR* Label = TEXT("");
		int32 ResolutionTier = 0;
		ETDFluidGpuWaterPressureMode PressureMode = ETDFluidGpuWaterPressureMode::JacobiOpenBorders;
		bool bUsesFloat32WaterVelocity = false;
		bool bUsesFloat32Ripple = false;
		int64 StateBytes = 0;
		int64 OutputBytes = 0;
		int64 TransientBytes = 0;
		int64 TotalBytesWithoutReadback = 0;
	};

	void VerifyMemoryTableRow(FAutomationTestBase& Test, const FTDWaterMemoryTableRow& Row)
	{
		const FTDFluidGpuBatchKey Key = MakeWaterKey(Row.ResolutionTier, Row.PressureMode, Row.bUsesFloat32WaterVelocity, Row.bUsesFloat32Ripple);
		const FTDFluidGpuMemoryEstimate Estimate = FTDFluidGpuBatch::EstimateWaterActorMemory(Key);
		Test.TestEqual(*FString::Printf(TEXT("%s state bytes"), Row.Label), Estimate.StateBytes, Row.StateBytes);
		Test.TestEqual(*FString::Printf(TEXT("%s output bytes"), Row.Label), Estimate.OutputBytes, Row.OutputBytes);
		Test.TestEqual(*FString::Printf(TEXT("%s transient bytes"), Row.Label), Estimate.TransientBytes, Row.TransientBytes);
		Test.TestEqual(*FString::Printf(TEXT("%s total bytes without readback"), Row.Label), Estimate.GetTotalBytes() - Estimate.ReadbackBytes, Row.TotalBytesWithoutReadback);
		Test.TestEqual(*FString::Printf(TEXT("%s readback bytes"), Row.Label), Estimate.ReadbackBytes, WaterTestExpectedReadbackBytes);
	}

	void VerifyResolutionTiers(FAutomationTestBase& Test)
	{
		const int32 RequestedResolutions[] = { 1, 16, 40, 64, 65, 100, 129, 192, 200, 256, 257, 300, 1000, 4096 };
		const int32 ExpectedTiers[] = { 16, 16, 64, 64, 128, 128, 192, 192, 256, 256, 512, 512, 1024, 2048 };
		for (int32 CaseIndex = 0; CaseIndex < static_cast<int32>(UE_ARRAY_COUNT(RequestedResolutions)); ++CaseIndex)
		{
			Test.TestEqual(*FString::Printf(TEXT("Water tier for %d"), RequestedResolutions[CaseIndex]), FTDFluidGpuBatch::ResolveWaterResolutionTier(RequestedResolutions[CaseIndex]), ExpectedTiers[CaseIndex]);
		}

		const int32 Tiers[] = { 16, 64, 192, 256, 512, 1024, 2048 };
		const int32 ExpectedDivisors[] = { 1, 1, 1, 1, 2, 4, 4 };
		for (int32 CaseIndex = 0; CaseIndex < static_cast<int32>(UE_ARRAY_COUNT(Tiers)); ++CaseIndex)
		{
			Test.TestEqual(*FString::Printf(TEXT("Field divisor for tier %d"), Tiers[CaseIndex]), FTDFluidGpuBatch::ComputeWaterFieldDivisor(Tiers[CaseIndex]), ExpectedDivisors[CaseIndex]);
			Test.TestEqual(*FString::Printf(TEXT("Shift quantum for tier %d"), Tiers[CaseIndex]), FTDFluidGpuBatch::ComputeWaterShiftQuantumCells(Tiers[CaseIndex]), ExpectedDivisors[CaseIndex]);
		}
	}

	void VerifyPressureModeKeys(FAutomationTestBase& Test)
	{
		{
			FTDWaterGpuConsoleVariableScope SolverScope(WaterGpuPressureSolverVariableName, WaterForcedMultigridPressureSolverMode);
			Test.TestTrue(TEXT("Forced multigrid solver variable is settable"), SolverScope.IsValid());
			const FTDFluidGpuBatchKey Water192Key = FTDFluidGpuBatch::MakeKey(MakeWaterSettings(192));
			Test.TestTrue(TEXT("192 water key uses the water layer"), Water192Key.bUsesWaterLayer);
			Test.TestEqual(TEXT("192 water key keeps the exact tier"), Water192Key.ResolutionTier, 192);
			Test.TestTrue(TEXT("192 water key uses open border Jacobi even with forced multigrid"), Water192Key.WaterPressureMode == ETDFluidGpuWaterPressureMode::JacobiOpenBorders && Water192Key.PressureSolver == ETDFluidGpuPressureSolver::Jacobi);
			Test.TestEqual(TEXT("192 water key carries the iterations"), Water192Key.PressureIterations, WaterTestPressureIterations);
			Test.TestEqual(TEXT("192 water key carries no multigrid cycles"), Water192Key.MultigridCycles, 0);
		}

		{
			FTDWaterGpuConsoleVariableScope SolverScope(WaterGpuPressureSolverVariableName, WaterForcedJacobiPressureSolverMode);
			Test.TestTrue(TEXT("Forced Jacobi solver variable is settable"), SolverScope.IsValid());
			const FTDFluidGpuBatchKey Water512Key = FTDFluidGpuBatch::MakeKey(MakeWaterSettings(512));
			Test.TestTrue(TEXT("512 water key uses half resolution multigrid even with forced Jacobi"), Water512Key.WaterPressureMode == ETDFluidGpuWaterPressureMode::HalfResolutionMultigrid && Water512Key.PressureSolver == ETDFluidGpuPressureSolver::Multigrid);
			Test.TestEqual(TEXT("512 water key carries the settings cycles"), Water512Key.MultigridCycles, 1);
			Test.TestEqual(TEXT("512 water key carries no iterations"), Water512Key.PressureIterations, 0);
		}

		FTDFluidGpuSimulationSettings HeatSettings = MakeWaterSettings(192);
		HeatSettings.bUsesHeatChannel = true;
		Test.TestFalse(TEXT("Water keys drop the heat channel"), FTDFluidGpuBatch::MakeKey(HeatSettings).bUsesHeatChannel);

		FTDFluidGpuSimulationSettings NonWaterSettings = MakeWaterSettings(192);
		NonWaterSettings.bUsesWaterLayer = false;
		const FTDFluidGpuBatchKey NonWaterKey = FTDFluidGpuBatch::MakeKey(NonWaterSettings);
		Test.TestFalse(TEXT("Non water keys stay off the water layer"), NonWaterKey.bUsesWaterLayer);
		Test.TestEqual(TEXT("Non water 192 keeps the power of two tier"), NonWaterKey.ResolutionTier, 256);

		FTDFluidGpuSimulationSettings PainterSettings = MakeWaterSettings(192);
		PainterSettings.SimMode = ETDFluidGpuSimMode::SimplePainter;
		const FTDFluidGpuBatchKey PainterKey = FTDFluidGpuBatch::MakeKey(PainterSettings);
		Test.TestFalse(TEXT("Painter keys ignore the water flag"), PainterKey.bUsesWaterLayer);
		Test.TestEqual(TEXT("Painter keys keep the power of two tier"), PainterKey.ResolutionTier, 256);
		Test.TestEqual(TEXT("Water 512 multigrid open boundary bytes"), FTDFluidGpuBatch::ComputeWaterMultigridOpenBoundaryBytes(512), static_cast<int64>(348160));
		Test.TestEqual(TEXT("Water 1024 multigrid open boundary bytes"), FTDFluidGpuBatch::ComputeWaterMultigridOpenBoundaryBytes(1024), static_cast<int64>(1396736));
	}

	void VerifyKeyPurity(FAutomationTestBase& Test)
	{
		FTDFluidGpuSimulationSettings Settings192 = MakeWaterSettings(192);
		{
			FTDWaterGpuConsoleVariableScope VelocityScope(WaterVelocityFloat32VariableName, WaterAutomaticFormatRequest);
			FTDWaterGpuConsoleVariableScope RippleScope(WaterRippleFloat32VariableName, WaterAutomaticFormatRequest);
			Test.TestTrue(TEXT("Format variables are settable"), VelocityScope.IsValid() && RippleScope.IsValid());
			FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Settings192);
		}

		const FTDFluidGpuBatchKey Key192 = FTDFluidGpuBatch::MakeKey(Settings192);
		Test.TestTrue(TEXT("Same settings give the same key"), FTDFluidGpuBatch::MakeKey(Settings192) == Key192);
		{
			FTDWaterGpuConsoleVariableScope VelocityScope(WaterVelocityFloat32VariableName, WaterFloat16FormatRequest);
			Test.TestTrue(TEXT("Velocity format variable is settable"), VelocityScope.IsValid());
			Test.TestTrue(TEXT("Changing the variable alone keeps the key"), FTDFluidGpuBatch::MakeKey(Settings192) == Key192);

			FTDFluidGpuSimulationSettings ReappliedSettings = Settings192;
			FTDFluidGpuBatch::ApplyWaterConsoleOverrides(ReappliedSettings);
			Test.TestFalse(TEXT("Reapplied settings take the 16 bit velocity"), ReappliedSettings.bUsesFloat32WaterVelocity);
			Test.TestFalse(TEXT("Reapplied settings change the key"), FTDFluidGpuBatch::MakeKey(ReappliedSettings) == Key192);
		}

		FTDFluidGpuSimulationSettings Settings512 = MakeWaterSettings(512);
		{
			FTDWaterGpuConsoleVariableScope CycleScope(WaterGpuPressureCyclesVariableName, 1);
			Test.TestTrue(TEXT("Cycle variable is settable"), CycleScope.IsValid());
			FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Settings512);
		}

		const FTDFluidGpuBatchKey Key512 = FTDFluidGpuBatch::MakeKey(Settings512);
		{
			FTDWaterGpuConsoleVariableScope CycleScope(WaterGpuPressureCyclesVariableName, 3);
			Test.TestTrue(TEXT("Cycle variable accepts 3"), CycleScope.IsValid());
			Test.TestTrue(TEXT("Changing cycles alone keeps the water key"), FTDFluidGpuBatch::MakeKey(Settings512) == Key512);

			FTDFluidGpuSimulationSettings ReappliedSettings = Settings512;
			FTDFluidGpuBatch::ApplyWaterConsoleOverrides(ReappliedSettings);
			Test.TestEqual(TEXT("Reapplied settings carry three cycles"), ReappliedSettings.WaterMultigridCycles, 3);
			Test.TestEqual(TEXT("Reapplied key carries three cycles"), FTDFluidGpuBatch::MakeKey(ReappliedSettings).MultigridCycles, 3);
		}

		{
			FTDWaterGpuConsoleVariableScope CycleScope(WaterGpuPressureCyclesVariableName, 9);
			FTDFluidGpuSimulationSettings ReappliedSettings = Settings512;
			FTDFluidGpuBatch::ApplyWaterConsoleOverrides(ReappliedSettings);
			Test.TestEqual(TEXT("Cycle override 9 is clamped to 4"), ReappliedSettings.WaterMultigridCycles, TDFluidGpuSolverConstants::MaximumMultigridCycles);
		}
	}

	void VerifyFormatOverrides(FAutomationTestBase& Test)
	{
		struct FTDWaterFormatCase
		{
			int32 ConsoleValue = WaterAutomaticFormatRequest;
			int32 Resolution = 192;
			bool bExpectedFloat32Velocity = true;
			bool bExpectedFloat32Ripple = true;
		};

		const FTDWaterFormatCase Cases[] = {
			{ WaterAutomaticFormatRequest, 192, true, true },
			{ WaterAutomaticFormatRequest, 256, true, true },
			{ WaterAutomaticFormatRequest, 512, true, false },
			{ WaterAutomaticFormatRequest, 1024, true, false },
			{ WaterFloat16FormatRequest, 192, false, false },
			{ WaterFloat16FormatRequest, 1024, false, false },
			{ WaterFloat32FormatRequest, 512, true, true }
		};

		for (const FTDWaterFormatCase& FormatCase : Cases)
		{
			FTDWaterGpuConsoleVariableScope VelocityScope(WaterVelocityFloat32VariableName, FormatCase.ConsoleValue);
			FTDWaterGpuConsoleVariableScope RippleScope(WaterRippleFloat32VariableName, FormatCase.ConsoleValue);
			Test.TestTrue(TEXT("Format variables are settable"), VelocityScope.IsValid() && RippleScope.IsValid());

			FTDFluidGpuSimulationSettings Settings = MakeWaterSettings(FormatCase.Resolution);
			FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Settings);
			Test.TestTrue(*FString::Printf(TEXT("Velocity format for value %d at %d"), FormatCase.ConsoleValue, FormatCase.Resolution), Settings.bUsesFloat32WaterVelocity == FormatCase.bExpectedFloat32Velocity);
			Test.TestTrue(*FString::Printf(TEXT("Ripple format for value %d at %d"), FormatCase.ConsoleValue, FormatCase.Resolution), Settings.bUsesFloat32Ripple == FormatCase.bExpectedFloat32Ripple);
			const FTDFluidGpuBatchKey Key = FTDFluidGpuBatch::MakeKey(Settings);
			Test.TestTrue(*FString::Printf(TEXT("Key velocity format for value %d at %d"), FormatCase.ConsoleValue, FormatCase.Resolution), Key.bUsesFloat32WaterVelocity == FormatCase.bExpectedFloat32Velocity);
			Test.TestTrue(*FString::Printf(TEXT("Key ripple format for value %d at %d"), FormatCase.ConsoleValue, FormatCase.Resolution), Key.bUsesFloat32Ripple == FormatCase.bExpectedFloat32Ripple);
		}

		{
			FTDWaterGpuConsoleVariableScope VelocityScope(WaterVelocityFloat32VariableName, WaterFloat32FormatRequest);
			FTDFluidGpuSimulationSettings NonWaterSettings = MakeWaterSettings(512);
			NonWaterSettings.bUsesWaterLayer = false;
			FTDFluidGpuBatch::ApplyWaterConsoleOverrides(NonWaterSettings);
			Test.TestFalse(TEXT("Non water settings ignore the water format variables"), NonWaterSettings.bUsesFloat32WaterVelocity);
		}

		const int32 PressureModeValues[] = { 2, 7, -3 };
		const int32 ExpectedPressureModeRequests[] = { 2, 2, 0 };
		for (int32 CaseIndex = 0; CaseIndex < static_cast<int32>(UE_ARRAY_COUNT(PressureModeValues)); ++CaseIndex)
		{
			FTDWaterGpuConsoleVariableScope ModeScope(WaterPressureModeVariableName, PressureModeValues[CaseIndex]);
			Test.TestTrue(TEXT("Pressure mode variable is settable"), ModeScope.IsValid());
			FTDFluidGpuSimulationSettings Settings = MakeWaterSettings(1024);
			FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Settings);
			Test.TestEqual(*FString::Printf(TEXT("Pressure mode request for value %d"), PressureModeValues[CaseIndex]), static_cast<int32>(Settings.WaterPressureModeRequest), ExpectedPressureModeRequests[CaseIndex]);
		}
	}

	void VerifyExactSliceCapacity(FAutomationTestBase& Test)
	{
		FTDFluidGpuFrameInputs FrameInputs;
		FrameInputs.Actors.Add(MakeWaterActor(41, 192));
		FrameInputs.Actors.Add(MakeNonWaterActor(42, 256));

		TArray<FTDFluidGpuBatch> Batches;
		FTDFluidGpuPreparedFrame PreparedFrame;
		FTDFluidGpuBatch::PrepareFrame(FrameInputs, Batches, PreparedFrame);

		const FTDFluidGpuBatch* WaterBatch = FindBatchByWaterLayer(Batches, true, 192);
		const FTDFluidGpuBatch* NonWaterBatch = FindBatchByWaterLayer(Batches, false, 256);
		if (!WaterBatch || !NonWaterBatch)
		{
			Test.AddError(TEXT("Water or non water batch is missing"));
			return;
		}

		Test.TestEqual(TEXT("Water batch slice capacity is exact"), WaterBatch->GetRequiredSliceCapacity(), 1);
		Test.TestEqual(TEXT("Non water 256 batch slice capacity still rounds up"), NonWaterBatch->GetRequiredSliceCapacity(), 4);
	}

	void VerifyRippleSubstepCounts(FAutomationTestBase& Test)
	{
		struct FTDWaterRippleSubstepCase
		{
			const TCHAR* Label = TEXT("");
			int32 ResolutionTier = 0;
			float SmallestCellSizeCm = 0.0f;
			float StepDeltaTimeSeconds = 0.0f;
			int32 ExpectedSubstepCount = 0;
			bool bExpectedReachedCap = false;
			float ExpectedWaveSpeedScale = 1.0f;
		};

		const FTDWaterRippleSubstepCase Cases[] = {
			{ TEXT("192 at 60 Hz"), 192, 18.75f, 1.0f / 60.0f, 1, false, 1.0f },
			{ TEXT("192 at 10 Hz"), 192, 18.75f, 0.1f, 2, false, 1.0f },
			{ TEXT("192 at 144 Hz"), 192, 18.75f, 1.0f / 144.0f, 1, false, 1.0f },
			{ TEXT("256 fine cells at 10 Hz"), 256, 3.515625f, 0.1f, 8, true, 1.0f },
			{ TEXT("1024 at 60 Hz"), 1024, 3.515625f, 1.0f / 60.0f, 2, false, 1.0f },
			{ TEXT("1024 at 40 Hz"), 1024, 3.515625f, 1.0f / 40.0f, 3, false, 1.0f },
			{ TEXT("1024 at 10 Hz"), 1024, 3.515625f, 0.1f, 10, false, 1.0f },
			{ TEXT("1024 1800 cm window at 10 Hz"), 1024, 1.7578125f, 0.1f, 16, true, 1.0f },
			{ TEXT("1024 1000 cm window at 10 Hz"), 1024, 0.9765625f, 0.1f, 16, true, 0.828641f },
			{ TEXT("1024 1100 cm window at 10 Hz"), 1024, 1.07421875f, 0.1f, 16, true, 0.911505f }
		};

		for (const FTDWaterRippleSubstepCase& RippleCase : Cases)
		{
			bool bReachedCap = false;
			float WaveSpeedScale = 0.0f;
			const int32 SubstepCount = FTDFluidGpuBatch::ComputeWaterRippleSubstepCount(WaterTestRippleWaveSpeedCmPerSec, RippleCase.StepDeltaTimeSeconds, RippleCase.SmallestCellSizeCm, RippleCase.ResolutionTier, bReachedCap, WaveSpeedScale);
			Test.TestEqual(*FString::Printf(TEXT("%s substeps"), RippleCase.Label), SubstepCount, RippleCase.ExpectedSubstepCount);
			Test.TestTrue(*FString::Printf(TEXT("%s cap"), RippleCase.Label), bReachedCap == RippleCase.bExpectedReachedCap);
			Test.TestEqual(*FString::Printf(TEXT("%s wave speed scale"), RippleCase.Label), static_cast<float>(WaveSpeedScale), static_cast<float>(RippleCase.ExpectedWaveSpeedScale), 1.0e-5f);
		}

		bool bStillWaterReachedCap = true;
		float StillWaterWaveSpeedScale = 0.0f;
		const int32 StillWaterSubstepCount = FTDFluidGpuBatch::ComputeWaterRippleSubstepCount(0.0f, 0.1f, 18.75f, 192, bStillWaterReachedCap, StillWaterWaveSpeedScale);
		Test.TestEqual(TEXT("No wave speed gives no substeps"), StillWaterSubstepCount, 0);
		Test.TestFalse(TEXT("No wave speed never reaches the cap"), bStillWaterReachedCap);
		Test.TestEqual(TEXT("No wave speed keeps the unit scale"), static_cast<float>(StillWaterWaveSpeedScale), 1.0f, 1.0e-6f);
	}

	void VerifyWaterParameterValues(FAutomationTestBase& Test)
	{
		FTDFluidGpuFrameInputs FrameInputs;
		FrameInputs.Actors.Add(MakeWaterActor(51, 192));
		FrameInputs.Actors.Add(MakeWaterActor(52, 1024));

		TArray<FTDFluidGpuBatch> Batches;
		FTDFluidGpuPreparedFrame PreparedFrame;
		FTDFluidGpuBatch::PrepareFrame(FrameInputs, Batches, PreparedFrame);
		const FTDFluidGpuBatch* Water192Batch = FindBatchByWaterLayer(Batches, true, 192);
		const FTDFluidGpuBatch* Water1024Batch = FindBatchByWaterLayer(Batches, true, 1024);
		if (!Water192Batch || !Water1024Batch || Water192Batch->GetFrame().WaterParameters.Num() != 1 || Water1024Batch->GetFrame().WaterParameters.Num() != 1)
		{
			Test.AddError(TEXT("Water batches do not carry one water parameter element per entry"));
			return;
		}

		const FTDFluidGpuWaterParameters& First192 = Water192Batch->GetFrame().WaterParameters[0];
		Test.TestTrue(TEXT("Prepared frame reports water entries"), PreparedFrame.bHasWaterEntries);
		Test.TestEqual(TEXT("Flow guide response blend is 1 - exp(-3/60)"), static_cast<float>(First192.FlowGuideResponseBlend), 0.0487706f, 1.0e-6f);
		Test.TestTrue(TEXT("New slice initializes all velocities"), HasWaterFlag(First192, ETDFluidGpuWaterFlags::InitializesAllVelocities));
		Test.TestTrue(TEXT("Default settings use open borders"), HasWaterFlag(First192, ETDFluidGpuWaterFlags::UsesOpenBorders));
		Test.TestTrue(TEXT("Default settings treat dry cells as solid"), HasWaterFlag(First192, ETDFluidGpuWaterFlags::TreatsDryCellsAsSolid));
		Test.TestFalse(TEXT("Default settings have no ripples"), HasWaterFlag(First192, ETDFluidGpuWaterFlags::UsesRipples));
		Test.TestEqual(TEXT("192 field resolution"), First192.FieldResolution, 192);
		Test.TestEqual(TEXT("192 field divisor"), First192.FieldDivisor, 1);
		Test.TestEqual(TEXT("192 pressure resolution"), First192.PressureResolution, 192);
		Test.TestEqual(TEXT("192 pressure divisor"), First192.PressureDivisor, 1);

		const FTDFluidGpuWaterParameters& First1024 = Water1024Batch->GetFrame().WaterParameters[0];
		Test.TestEqual(TEXT("1024 field resolution"), First1024.FieldResolution, 256);
		Test.TestEqual(TEXT("1024 field divisor"), First1024.FieldDivisor, 4);
		Test.TestEqual(TEXT("1024 pressure resolution"), First1024.PressureResolution, 512);
		Test.TestEqual(TEXT("1024 pressure divisor"), First1024.PressureDivisor, 2);

		FTDFluidGpuBatch::PrepareFrame(FrameInputs, Batches, PreparedFrame);
		Water192Batch = FindBatchByWaterLayer(Batches, true, 192);
		if (!Water192Batch || Water192Batch->GetFrame().WaterParameters.Num() != 1)
		{
			Test.AddError(TEXT("Water batch is missing on the second frame"));
			return;
		}

		Test.TestFalse(TEXT("Stable slice does not initialize velocities"), HasWaterFlag(Water192Batch->GetFrame().WaterParameters[0], ETDFluidGpuWaterFlags::InitializesAllVelocities));

		FTDFluidGpuFrameInputs ResetFrameInputs = FrameInputs;
		ResetFrameInputs.Actors[0].bShouldResetState = true;
		FTDFluidGpuBatch::PrepareFrame(ResetFrameInputs, Batches, PreparedFrame);
		Water192Batch = FindBatchByWaterLayer(Batches, true, 192);
		if (!Water192Batch || Water192Batch->GetFrame().WaterParameters.Num() != 1)
		{
			Test.AddError(TEXT("Water batch is missing on the reset frame"));
			return;
		}

		Test.TestTrue(TEXT("Forced reset initializes velocities"), HasWaterFlag(Water192Batch->GetFrame().WaterParameters[0], ETDFluidGpuWaterFlags::InitializesAllVelocities));

		FTDFluidGpuFrameInputs PausedFrameInputs = FrameInputs;
		PausedFrameInputs.Actors[0].DeltaTimeSeconds = 0.0f;
		PausedFrameInputs.Actors[1].DeltaTimeSeconds = 0.0f;
		FTDFluidGpuBatch::PrepareFrame(PausedFrameInputs, Batches, PreparedFrame);
		Test.TestFalse(TEXT("Paused water actors give no water entries"), PreparedFrame.bHasWaterEntries);

		FTDFluidGpuFrameInputs NonWaterFrameInputs;
		NonWaterFrameInputs.Actors.Add(MakeNonWaterActor(53, 256));
		TArray<FTDFluidGpuBatch> NonWaterBatches;
		FTDFluidGpuBatch::PrepareFrame(NonWaterFrameInputs, NonWaterBatches, PreparedFrame);
		Test.TestFalse(TEXT("Non water frame has no water entries"), PreparedFrame.bHasWaterEntries);
		Test.TestEqual(TEXT("Non water frame has no probe slots"), PreparedFrame.WaterProbeSlotCount, 0);
		const FTDFluidGpuBatch* NonWaterBatch = FindBatchByWaterLayer(NonWaterBatches, false, 256);
		Test.TestTrue(TEXT("Non water batch carries no water parameters"), NonWaterBatch && NonWaterBatch->GetFrame().WaterParameters.IsEmpty());
	}

	void VerifyWaterUpdatePacking(FAutomationTestBase& Test)
	{
		FTDFluidGpuActorInput FlowActor = MakeWaterActor(61, 192);
		FlowActor.Settings.RippleWaveSpeedCmPerSec = WaterTestRippleWaveSpeedCmPerSec;
		FlowActor.WaterGuideUpdates.Add({ FIntPoint(3, 4), 100.0f, FVector2f(10.0f, 20.0f), 0.5f, 0.1f });
		FlowActor.WaterGuideUpdates.Add({ FIntPoint(3, 4), 120.0f, FVector2f(30.0f, 40.0f), 0.75f, 0.2f });
		FlowActor.WaterGuideUpdates.Add({ FIntPoint(192, 0), 100.0f, FVector2f(1.0f, 1.0f), 1.0f, 0.0f });
		FlowActor.WaterGuideUpdates.Add({ FIntPoint(-1, 2), 100.0f, FVector2f(1.0f, 1.0f), 1.0f, 0.0f });
		FlowActor.WaterGuideUpdates.Add({ FIntPoint(5, 5), 100.0f, FVector2f(1.0f, 1.0f), std::numeric_limits<float>::quiet_NaN(), 0.0f });
		FlowActor.WaterTerrainUpdates.Add({ FIntPoint(7, 8), 100.0f });
		FlowActor.WaterTerrainUpdates.Add({ FIntPoint(7, 8), 200.0f });
		FlowActor.WaterTerrainUpdates.Add({ FIntPoint(9, 9), std::numeric_limits<float>::infinity() });
		FlowActor.WaterTerrainUpdates.Add({ FIntPoint(0, 0), TDFluidGpuSolverConstants::TerrainUnknownElevationCm });
		FlowActor.WaterProbeFirstSlot = 62;
		FlowActor.WaterProbeCells.Add(FIntPoint(10, 10));
		FlowActor.WaterProbeCells.Add(FIntPoint(300, 5));
		FlowActor.WaterProbeCells.Add(FIntPoint(1, 1));

		FTDFluidGpuActorInput FineActor = MakeWaterActor(62, 192);
		FineActor.Settings.ExtentCm = FVector2D(675.0, 675.0);
		FineActor.Settings.RippleWaveSpeedCmPerSec = WaterTestRippleWaveSpeedCmPerSec;
		FineActor.DeltaTimeSeconds = 0.1f;

		FTDFluidGpuFrameInputs FrameInputs;
		FrameInputs.Actors.Add(FlowActor);
		FrameInputs.Actors.Add(FineActor);

		TArray<FTDFluidGpuBatch> Batches;
		FTDFluidGpuPreparedFrame PreparedFrame;
		FTDFluidGpuBatch::PrepareFrame(FrameInputs, Batches, PreparedFrame);
		const FTDFluidGpuBatch* WaterBatch = FindBatchByWaterLayer(Batches, true, 192);
		if (!WaterBatch || WaterBatch->GetFrame().Entries.Num() != 2)
		{
			Test.AddError(TEXT("Both 192 water actors should share one batch"));
			return;
		}

		const FTDFluidGpuBatchFrame& Frame = WaterBatch->GetFrame();
		Test.TestEqual(TEXT("Duplicate and invalid guide updates collapse to one"), Frame.WaterGuideUpdates.Num(), 1);
		if (Frame.WaterGuideUpdates.Num() == 1)
		{
			const FTDFluidGpuPackedWaterGuideUpdate& Guide = Frame.WaterGuideUpdates[0];
			Test.TestEqual(TEXT("Guide update keeps the entry index"), static_cast<int32>(Guide.EntryIndex), 0);
			Test.TestEqual(TEXT("Guide update keeps the field cell X"), static_cast<int32>(Guide.FieldCellX), 3);
			Test.TestEqual(TEXT("Guide update keeps the field cell Y"), static_cast<int32>(Guide.FieldCellY), 4);
			Test.TestEqual(TEXT("Last guide surface wins"), static_cast<float>(Guide.SurfaceHeightCm), 120.0f);
			Test.TestEqual(TEXT("Last guide target X wins"), static_cast<float>(Guide.GuideValues.X), 30.0f);
			Test.TestEqual(TEXT("Last guide target Y wins"), static_cast<float>(Guide.GuideValues.Y), 40.0f);
			Test.TestEqual(TEXT("Last guide weight wins"), static_cast<float>(Guide.GuideValues.Z), 0.75f);
			Test.TestEqual(TEXT("Last guide descent wins"), static_cast<float>(Guide.GuideValues.W), 0.2f);
		}

		Test.TestEqual(TEXT("Duplicate and non finite terrain updates collapse to two"), Frame.WaterTerrainUpdates.Num(), 2);
		if (Frame.WaterTerrainUpdates.Num() == 2)
		{
			Test.TestEqual(TEXT("Last terrain elevation wins"), static_cast<float>(Frame.WaterTerrainUpdates[0].ElevationCm), 200.0f);
			Test.TestTrue(TEXT("Unknown terrain reset stays"), Frame.WaterTerrainUpdates[1].ElevationCm == TDFluidGpuSolverConstants::TerrainUnknownElevationCm);
		}

		Test.TestEqual(TEXT("Probe slots past 63 are dropped"), Frame.WaterProbeRequests.Num(), 2);
		if (Frame.WaterProbeRequests.Num() == 2)
		{
			Test.TestEqual(TEXT("First probe slot"), static_cast<int32>(Frame.WaterProbeRequests[0].ProbeSlot), 62);
			Test.TestEqual(TEXT("First probe cell X"), Frame.WaterProbeRequests[0].CellX, 10);
			Test.TestEqual(TEXT("Second probe slot"), static_cast<int32>(Frame.WaterProbeRequests[1].ProbeSlot), 63);
			Test.TestEqual(TEXT("Probe outside the grid becomes the outside marker X"), Frame.WaterProbeRequests[1].CellX, -1);
			Test.TestEqual(TEXT("Probe outside the grid becomes the outside marker Y"), Frame.WaterProbeRequests[1].CellY, -1);
		}

		Test.TestEqual(TEXT("Probe slot count is clamped to 64"), PreparedFrame.WaterProbeSlotCount, TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame);
		Test.TestEqual(TEXT("Batch ripple substep maximum takes the capped fine actor"), Frame.WaterRippleSubstepMaximum, TDFluidGpuSolverConstants::MaximumWaterRippleSubsteps);
		Test.TestEqual(TEXT("One entry reached the ripple substep cap"), Frame.WaterRippleSubstepCapHitCount, 1);
		Test.TestEqual(TEXT("Flow actor uses one ripple substep"), static_cast<int32>(Frame.WaterParameters[0].RippleSubstepCount), 1);
		Test.TestEqual(TEXT("Flow actor keeps the CPU wave speed squared"), static_cast<float>(Frame.WaterParameters[0].RippleWaveSpeedSquared), 14400.0f);
		Test.TestTrue(TEXT("Fine actor marks the cap"), HasWaterFlag(Frame.WaterParameters[1], ETDFluidGpuWaterFlags::ReachedRippleSubstepCap));
		Test.TestTrue(TEXT("Rippling actor advects ripples"), HasWaterFlag(Frame.WaterParameters[0], ETDFluidGpuWaterFlags::UsesRippleAdvection));
	}

	FTDFluidGpuStampInput MakeRippleStamp(int32 SourceIndex, double CenterXCm)
	{
		FTDFluidGpuStampInput Stamp;
		Stamp.CenterPlaneCm = FVector2D(CenterXCm, 0.0);
		Stamp.RadiusCm = 50.0f;
		Stamp.RippleImpulseCm = -0.25f * static_cast<float>(SourceIndex + 1);
		return Stamp;
	}

	FTDFluidGpuStampBinWindow MakeRippleWindow()
	{
		FTDFluidGpuStampBinWindow Window;
		Window.CenterPlaneCm = FVector2D::ZeroVector;
		Window.ExtentCm = FVector2D(WaterTestExtentCm, WaterTestExtentCm);
		Window.Resolution = 192;
		return Window;
	}

	void VerifyImpulsesFollowStamps(FAutomationTestBase& Test, const TCHAR* Label, const FTDFluidGpuStampBinSet& BinSet, TConstArrayView<FTDFluidGpuStampInput> Stamps)
	{
		Test.TestEqual(*FString::Printf(TEXT("%s impulse count matches stamps"), Label), BinSet.RippleImpulsesCm.Num(), BinSet.Stamps.Num());
		if (BinSet.RippleImpulsesCm.Num() != BinSet.Stamps.Num())
		{
			return;
		}

		for (int32 StampIndex = 0; StampIndex < BinSet.Stamps.Num(); ++StampIndex)
		{
			const int32 SourceIndex = static_cast<int32>(BinSet.Stamps[StampIndex].SourceIndex);
			if (!Stamps.IsValidIndex(SourceIndex))
			{
				Test.AddError(FString::Printf(TEXT("%s stamp %d has an invalid source index"), Label, StampIndex));
				continue;
			}

			Test.TestEqual(*FString::Printf(TEXT("%s impulse %d follows its stamp"), Label, StampIndex), static_cast<float>(BinSet.RippleImpulsesCm[StampIndex]), static_cast<float>(Stamps[SourceIndex].RippleImpulseCm));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterModuleMemoryEstimateMatchesTableTest,
	"TDGame.FluidFX.Gpu.Water.MemoryEstimateMatchesTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterModuleMemoryEstimateMatchesTableTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterModuleTestPrivate;

	const FTDWaterMemoryTableRow Rows[] = {
		{ TEXT("192 Jacobi 32 bit"), 192, ETDFluidGpuWaterPressureMode::JacobiOpenBorders, true, true, 3170304, 884736, 221184, 4276224 },
		{ TEXT("192 Jacobi 16 bit"), 192, ETDFluidGpuWaterPressureMode::JacobiOpenBorders, false, false, 2580480, 884736, 221184, 3686400 },
		{ TEXT("256 Jacobi 32 bit"), 256, ETDFluidGpuWaterPressureMode::JacobiOpenBorders, true, true, 5636096, 1572864, 393216, 7602176 },
		{ TEXT("512 multigrid 16 bit"), 512, ETDFluidGpuWaterPressureMode::HalfResolutionMultigrid, false, false, 12058624, 6291456, 5019652, 23369732 },
		{ TEXT("1024 multigrid 16 bit"), 1024, ETDFluidGpuWaterPressureMode::HalfResolutionMultigrid, false, false, 43515904, 25165824, 19705860, 88387588 },
		{ TEXT("1024 kernel 16 bit"), 1024, ETDFluidGpuWaterPressureMode::HalfResolutionKernelFeedback, false, false, 43515904, 25165824, 21110788, 89792516 },
		{ TEXT("1024 multigrid 32 bit velocity"), 1024, ETDFluidGpuWaterPressureMode::HalfResolutionMultigrid, true, false, 51904512, 25165824, 19705860, 96776196 },
		{ TEXT("1024 multigrid 32 bit velocity and ripple"), 1024, ETDFluidGpuWaterPressureMode::HalfResolutionMultigrid, true, true, 60293120, 25165824, 19705860, 105164804 }
	};

	FTDWaterGpuConsoleVariableScope PostSmoothScope(TEXT("TD.FluidFX.WaterGpuHalfPostSmoothIterations"), 4);
	TestTrue(TEXT("Post smooth variable is settable"), PostSmoothScope.IsValid());
	for (const FTDWaterMemoryTableRow& Row : Rows)
	{
		VerifyMemoryTableRow(*this, Row);
	}

	FTDFluidGpuSimulationSettings DefaultSettings = MakeWaterSettings(192);
	{
		FTDWaterGpuConsoleVariableScope VelocityScope(WaterVelocityFloat32VariableName, WaterAutomaticFormatRequest);
		FTDWaterGpuConsoleVariableScope RippleScope(WaterRippleFloat32VariableName, WaterAutomaticFormatRequest);
		TestTrue(TEXT("Format variables are settable"), VelocityScope.IsValid() && RippleScope.IsValid());
		FTDFluidGpuBatch::ApplyWaterConsoleOverrides(DefaultSettings);
	}

	const FTDFluidGpuMemoryEstimate DefaultEstimate = FTDFluidGpuBatch::EstimateWaterActorMemory(FTDFluidGpuBatch::MakeKey(DefaultSettings));
	TestEqual(TEXT("Default 192 water actor matches the 32 bit row"), DefaultEstimate.GetTotalBytes() - DefaultEstimate.ReadbackBytes, static_cast<int64>(4276224));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterModuleKeyAndTierRulesTest,
	"TDGame.FluidFX.Gpu.Water.KeyAndTierRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterModuleKeyAndTierRulesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterModuleTestPrivate;

	VerifyResolutionTiers(*this);
	VerifyPressureModeKeys(*this);
	VerifyKeyPurity(*this);
	VerifyFormatOverrides(*this);
	VerifyExactSliceCapacity(*this);
	VerifyRippleSubstepCounts(*this);
	VerifyWaterParameterValues(*this);
	VerifyWaterUpdatePacking(*this);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterModuleStampRippleImpulsesFollowStampsTest,
	"TDGame.FluidFX.Gpu.Water.StampRippleImpulsesFollowStamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterModuleStampRippleImpulsesFollowStampsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterModuleTestPrivate;

	constexpr int32 FiniteStampCount = 12;
	constexpr int32 TickLimit = 8;
	const FTDFluidGpuStampBinWindow Window = MakeRippleWindow();

	TArray<FTDFluidGpuStampInput> FiniteStamps;
	for (int32 SourceIndex = 0; SourceIndex < FiniteStampCount; ++SourceIndex)
	{
		FiniteStamps.Add(MakeRippleStamp(SourceIndex, 100.0 * static_cast<double>(FiniteStampCount - 1 - SourceIndex)));
	}

	FTDFluidGpuStampBinLimits Limits;
	Limits.MaxStampsPerTick = TickLimit;
	const FTDFluidGpuStampBinSet FiniteBins = FTDFluidGpuStampBins::Build(Window, FiniteStamps, Limits);
	TestEqual(TEXT("Tick limit keeps eight stamps"), FiniteBins.Stamps.Num(), TickLimit);
	TestEqual(TEXT("Tick limit drops four stamps"), FiniteBins.OverTickLimitStampCount, FiniteStampCount - TickLimit);
	TestEqual(TEXT("Finite stamps are not rejected"), FiniteBins.NonFiniteStampCount, 0);
	VerifyImpulsesFollowStamps(*this, TEXT("Limited and sorted"), FiniteBins, FiniteStamps);
	for (int32 StampIndex = 0; StampIndex < FiniteBins.Stamps.Num(); ++StampIndex)
	{
		TestTrue(TEXT("Tick limit keeps the stamps nearest the window center"), FiniteBins.Stamps[StampIndex].SourceIndex >= static_cast<uint32>(FiniteStampCount - TickLimit));
	}

	TArray<FTDFluidGpuStampInput> MixedStamps = FiniteStamps;
	FTDFluidGpuStampInput NonFiniteStamp = MakeRippleStamp(FiniteStampCount, 0.0);
	NonFiniteStamp.RippleImpulseCm = std::numeric_limits<float>::quiet_NaN();
	MixedStamps.Add(NonFiniteStamp);
	const FTDFluidGpuStampBinSet MixedBins = FTDFluidGpuStampBins::Build(Window, MixedStamps, Limits);
	TestEqual(TEXT("Non finite ripple impulse raises the rejected count"), MixedBins.NonFiniteStampCount, FiniteBins.NonFiniteStampCount + 1);
	TestEqual(TEXT("Non finite ripple impulse does not change the tick drop count"), MixedBins.OverTickLimitStampCount, FiniteBins.OverTickLimitStampCount);
	VerifyImpulsesFollowStamps(*this, TEXT("With a rejected stamp"), MixedBins, MixedStamps);

	TArray<FTDFluidGpuStampInput> SecondStamps;
	SecondStamps.Add(MakeRippleStamp(20, 300.0));
	SecondStamps.Add(MakeRippleStamp(21, 200.0));
	SecondStamps.Add(MakeRippleStamp(22, 100.0));
	const FTDFluidGpuStampBinSet SecondBins = FTDFluidGpuStampBins::Build(Window, SecondStamps, FTDFluidGpuStampBinLimits());

	FTDFluidGpuStampBinSet FrameBins;
	FTDFluidGpuStampBins::AppendToFrame(FiniteBins, FrameBins);
	FTDFluidGpuStampBins::AppendToFrame(SecondBins, FrameBins);
	TestEqual(TEXT("Frame impulse count matches frame stamps"), FrameBins.RippleImpulsesCm.Num(), FrameBins.Stamps.Num());
	TArray<float> ExpectedFrameImpulses = FiniteBins.RippleImpulsesCm;
	ExpectedFrameImpulses.Append(SecondBins.RippleImpulsesCm);
	TestTrue(TEXT("Frame impulses append in stamp order"), FrameBins.RippleImpulsesCm == ExpectedFrameImpulses);

	FTDFluidGpuActorInput WaterActor = MakeWaterActor(71, 192);
	WaterActor.WindowCenterPlaneCm = FVector2D::ZeroVector;
	WaterActor.Stamps = SecondStamps;
	FTDFluidGpuActorInput NonWaterActor = MakeNonWaterActor(72, 256);
	NonWaterActor.WindowCenterPlaneCm = FVector2D::ZeroVector;
	NonWaterActor.Stamps.Add(FTDFluidGpuStampInput());

	FTDFluidGpuFrameInputs FrameInputs;
	FrameInputs.Actors.Add(WaterActor);
	FrameInputs.Actors.Add(NonWaterActor);
	TArray<FTDFluidGpuBatch> Batches;
	FTDFluidGpuPreparedFrame PreparedFrame;
	FTDFluidGpuBatch::PrepareFrame(FrameInputs, Batches, PreparedFrame);
	TestEqual(TEXT("Prepared frame impulse count matches prepared stamps"), PreparedFrame.StampBins.RippleImpulsesCm.Num(), PreparedFrame.StampBins.Stamps.Num());
	TestEqual(TEXT("Prepared frame keeps every stamp"), PreparedFrame.StampBins.Stamps.Num(), SecondStamps.Num() + 1);
	float ImpulseSum = 0.0f;
	for (const float RippleImpulseCm : PreparedFrame.StampBins.RippleImpulsesCm)
	{
		ImpulseSum += RippleImpulseCm;
	}
	TestEqual(TEXT("Prepared frame carries the water impulses and a zero non water impulse"), static_cast<float>(ImpulseSum), -0.25f * static_cast<float>(21 + 22 + 23), 1.0e-5f);
	return !HasAnyErrors();
}

#endif
