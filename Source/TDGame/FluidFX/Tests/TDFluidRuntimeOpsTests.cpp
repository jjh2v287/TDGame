#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidRuntimeOps.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidRuntimeOpsTestPrivate
{
	const FLinearColor OpsTestClearColor(0.0f, 0.5f, 0.0f, 0.0f);
	constexpr int32 OpsTestTargetResolution = 64;

	struct FTDOpsConsoleRestore
	{
		explicit FTDOpsConsoleRestore(const TCHAR* InName)
			: Variable(IConsoleManager::Get().FindConsoleVariable(InName))
		{
			if (Variable)
			{
				OriginalValue = Variable->GetString();
			}
		}

		~FTDOpsConsoleRestore()
		{
			if (Variable)
			{
				Variable->Set(*OriginalValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		FString OriginalValue;
	};

	bool HasSameFloatBits(float Left, float Right)
	{
		return FMemory::Memcmp(&Left, &Right, sizeof(float)) == 0;
	}

	bool IsSameSimulationConfig(const FTDFluidSimulationConfig& Left, const FTDFluidSimulationConfig& Right)
	{
		return FTDFluidSimulationConfig::StaticStruct()->CompareScriptStruct(&Left, &Right, PPF_None);
	}

	FTDFluidRuntimeOpsSettings MakeLodSettings()
	{
		FTDFluidRuntimeOpsSettings Settings;
		Settings.LodMinStepRateHz = 30.0f;
		Settings.LodNearDistanceCm = 2500.0f;
		Settings.LodFarDistanceCm = 5000.0f;
		Settings.LodStepCount = 5;
		return Settings;
	}

	FTDFluidSimulationConfig MakeNormalizationConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(4000.0, 4000.0);
		Config.VorticityStrength = 2.0f;
		Config.PressureIterations = 8;
		Config.bEnableWaterLayer = false;
		return Config;
	}

	UTextureRenderTarget2D* MakeUsableTarget()
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->SizeX = OpsTestTargetResolution;
		Target->SizeY = OpsTestTargetResolution;
		Target->bSupportsUAV = true;
		Target->bCanCreateUAV = true;
		Target->ClearColor = OpsTestClearColor;
		Target->AddressX = TA_Wrap;
		Target->AddressY = TA_Wrap;
		Target->bAutoGenerateMips = false;
		return Target;
	}

	bool IsUsableOpsTestTarget(const UTextureRenderTarget2D* Target)
	{
		return TDFluidRuntimeOps::IsUsableExternalOutputTarget(Target, OpsTestTargetResolution, OpsTestClearColor, TA_Wrap);
	}

	struct FTDOpsCadenceResult
	{
		int32 StepCount = 0;
		double StepDeltaSumSeconds = 0.0;
		double FrameDeltaSumSeconds = 0.0;
		bool bEveryStepMatchesExpectedBits = true;
		FTDFluidStepClock Clock;
	};

	FTDOpsCadenceResult RunCadence(float FrameDeltaSeconds, float StepRateHz, int32 TickCount, float ExpectedStepDeltaSeconds)
	{
		FTDOpsCadenceResult Result;
		for (int32 TickIndex = 0; TickIndex < TickCount; ++TickIndex)
		{
			Result.FrameDeltaSumSeconds += FrameDeltaSeconds;
			const float CadenceStepDeltaSeconds = TDFluidRuntimeOps::AdvanceStepClock(Result.Clock, FrameDeltaSeconds, StepRateHz);
			if (CadenceStepDeltaSeconds <= 0.0f)
			{
				continue;
			}

			++Result.StepCount;
			Result.StepDeltaSumSeconds += CadenceStepDeltaSeconds;
			Result.bEveryStepMatchesExpectedBits &= HasSameFloatBits(CadenceStepDeltaSeconds, ExpectedStepDeltaSeconds);
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsStepClockCadenceTest,
	"TDGame.FluidFX.Ops.StepClockCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsStepClockCadenceTest::RunTest(const FString& Parameters)
{
	using TDFluidRuntimeOpsTestPrivate::FTDOpsCadenceResult;
	using TDFluidRuntimeOpsTestPrivate::HasSameFloatBits;
	using TDFluidRuntimeOpsTestPrivate::RunCadence;

	const float UncappedFrameDeltas[] = { 1.0f / 60.0f, 1.0f / 144.0f, 0.0333f, 0.1f };
	for (const float UncappedFrameDeltaSeconds : UncappedFrameDeltas)
	{
		FTDFluidStepClock UncappedClock;
		const float UncappedStepDeltaSeconds = TDFluidRuntimeOps::AdvanceStepClock(UncappedClock, UncappedFrameDeltaSeconds, 0.0f);
		TestTrue(FString::Printf(TEXT("Rate 0 returns the frame delta bit for bit (%.6f)"), UncappedFrameDeltaSeconds), HasSameFloatBits(UncappedStepDeltaSeconds, UncappedFrameDeltaSeconds));
		TestTrue(TEXT("Rate 0 leaves an empty clock"), UncappedClock.ElapsedSinceStepSeconds == 0.0f && UncappedClock.RateClockSeconds == 0.0f);
	}

	const FTDOpsCadenceResult HalfRateResult = RunCadence(1.0f / 120.0f, 60.0f, 120, 1.0f / 60.0f);
	TestEqual(TEXT("120 fps with a 60 Hz cap steps 60 times in 120 ticks"), HalfRateResult.StepCount, 60);
	TestTrue(TEXT("120 fps with a 60 Hz cap steps with exactly 1/60 s"), HalfRateResult.bEveryStepMatchesExpectedBits);

	const FTDOpsCadenceResult UnevenRateResult = RunCadence(1.0f / 144.0f, 60.0f, 144, 0.0f);
	TestTrue(FString::Printf(TEXT("144 fps with a 60 Hz cap averages 60 steps (got %d)"), UnevenRateResult.StepCount), UnevenRateResult.StepCount >= 59 && UnevenRateResult.StepCount <= 61);
	const double UnevenIntegratedSeconds = UnevenRateResult.StepDeltaSumSeconds + UnevenRateResult.Clock.ElapsedSinceStepSeconds;
	TestTrue(TEXT("144 fps with a 60 Hz cap keeps the integrated time"), FMath::IsNearlyEqual(UnevenIntegratedSeconds, UnevenRateResult.FrameDeltaSumSeconds, 1.0e-5));

	const FTDOpsCadenceResult SlowGameResult = RunCadence(1.0f / 50.0f, 60.0f, 50, 1.0f / 50.0f);
	TestEqual(TEXT("50 fps under a 60 Hz cap steps every tick"), SlowGameResult.StepCount, 50);
	TestTrue(TEXT("50 fps under a 60 Hz cap steps with the frame delta bit for bit"), SlowGameResult.bEveryStepMatchesExpectedBits);

	FTDFluidStepClock SkippedClock;
	const float SkippedStepDeltaSeconds = TDFluidRuntimeOps::AdvanceStepClock(SkippedClock, 1.0f / 120.0f, 60.0f);
	TestEqual(TEXT("The first 120 fps tick under a 60 Hz cap is skipped"), SkippedStepDeltaSeconds, 0.0f);
	const float ReleasedStepDeltaSeconds = TDFluidRuntimeOps::AdvanceStepClock(SkippedClock, 1.0f / 120.0f, 0.0f);
	TestTrue(TEXT("Turning the cap off returns the accumulated delta"), HasSameFloatBits(ReleasedStepDeltaSeconds, 1.0f / 60.0f));
	TestTrue(TEXT("Turning the cap off empties the clock"), SkippedClock.ElapsedSinceStepSeconds == 0.0f && SkippedClock.RateClockSeconds == 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsStepRateLodTest,
	"TDGame.FluidFX.Ops.StepRateLod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsStepRateLodTest::RunTest(const FString& Parameters)
{
	using TDFluidRuntimeOpsTestPrivate::FTDOpsConsoleRestore;
	using TDFluidRuntimeOpsTestPrivate::MakeLodSettings;

	const FTDFluidRuntimeOpsSettings LodSettings = MakeLodSettings();
	constexpr float LodTolerance = 1.0e-4f;
	TestTrue(TEXT("Observer at 0 cm keeps the cap"), FMath::IsNearlyEqual(TDFluidRuntimeOps::ComputeLodStepRateHz(LodSettings, 60.0f, 0.0), 60.0f, LodTolerance));
	TestTrue(TEXT("Observer at the near distance keeps the cap"), FMath::IsNearlyEqual(TDFluidRuntimeOps::ComputeLodStepRateHz(LodSettings, 60.0f, 2500.0), 60.0f, LodTolerance));
	TestTrue(TEXT("First band past the near distance drops one step"), FMath::IsNearlyEqual(TDFluidRuntimeOps::ComputeLodStepRateHz(LodSettings, 60.0f, 2600.0), 54.0f, LodTolerance));
	TestTrue(TEXT("Second band drops two steps"), FMath::IsNearlyEqual(TDFluidRuntimeOps::ComputeLodStepRateHz(LodSettings, 60.0f, 3100.0), 48.0f, LodTolerance));
	TestTrue(TEXT("Observer at the far distance reaches the minimum"), FMath::IsNearlyEqual(TDFluidRuntimeOps::ComputeLodStepRateHz(LodSettings, 60.0f, 5000.0), 30.0f, LodTolerance));
	TestTrue(TEXT("Observer past the far distance stays at the minimum"), FMath::IsNearlyEqual(TDFluidRuntimeOps::ComputeLodStepRateHz(LodSettings, 60.0f, 90000.0), 30.0f, LodTolerance));

	FTDFluidRuntimeOpsSettings NoStepSettings = LodSettings;
	NoStepSettings.LodStepCount = 0;
	TestEqual(TEXT("Zero LOD steps return the cap"), TDFluidRuntimeOps::ComputeLodStepRateHz(NoStepSettings, 60.0f, 5000.0), 60.0f);

	FTDFluidRuntimeOpsSettings InvertedSettings = LodSettings;
	InvertedSettings.LodFarDistanceCm = InvertedSettings.LodNearDistanceCm;
	TestEqual(TEXT("Far distance not beyond near returns the cap"), TDFluidRuntimeOps::ComputeLodStepRateHz(InvertedSettings, 60.0f, 5000.0), 60.0f);
	TestEqual(TEXT("No cap stays uncapped"), TDFluidRuntimeOps::ComputeLodStepRateHz(LodSettings, 0.0f, 5000.0), 0.0f);

	FTDFluidRuntimeOpsSettings LowFloorSettings = LodSettings;
	LowFloorSettings.LodMinStepRateHz = 5.0f;
	TestTrue(TEXT("Minimum below the floor is raised to 12 Hz"), FMath::IsNearlyEqual(TDFluidRuntimeOps::ComputeLodStepRateHz(LowFloorSettings, 60.0f, 5000.0), 12.0f, LodTolerance));

	FTDFluidRuntimeOpsSettings HighFloorSettings = LodSettings;
	HighFloorSettings.LodMinStepRateHz = 100.0f;
	TestTrue(TEXT("Minimum above the cap never raises the rate"), FMath::IsNearlyEqual(TDFluidRuntimeOps::ComputeLodStepRateHz(HighFloorSettings, 60.0f, 5000.0), 60.0f, LodTolerance));
	TestTrue(TEXT("Cap below the floor is never exceeded"), TDFluidRuntimeOps::ComputeLodStepRateHz(LodSettings, 10.0f, 5000.0) <= 10.0f);

	FTDOpsConsoleRestore MaxStepRateRestore(TEXT("TD.FluidFX.MaxStepRateHz"));
	if (!TestNotNull(TEXT("TD.FluidFX.MaxStepRateHz exists"), MaxStepRateRestore.Variable))
	{
		return false;
	}

	FTDFluidRuntimeOpsSettings CapSettings;
	MaxStepRateRestore.Variable->Set(-1.0f, ECVF_SetByCode);
	TestEqual(TEXT("Default tuning keeps steps uncapped"), TDFluidRuntimeOps::ResolveMaxStepRateHz(CapSettings), 0.0f);
	CapSettings.MaxStepRateHz = 300.0f;
	TestEqual(TEXT("Tuning cap is clamped to 240 Hz"), TDFluidRuntimeOps::ResolveMaxStepRateHz(CapSettings), 240.0f);
	CapSettings.MaxStepRateHz = 90.0f;
	MaxStepRateRestore.Variable->Set(0.0f, ECVF_SetByCode);
	TestEqual(TEXT("Console 0 forces no cap"), TDFluidRuntimeOps::ResolveMaxStepRateHz(CapSettings), 0.0f);
	MaxStepRateRestore.Variable->Set(60.0f, ECVF_SetByCode);
	TestEqual(TEXT("Console value above 0 forces that cap"), TDFluidRuntimeOps::ResolveMaxStepRateHz(CapSettings), 60.0f);
	MaxStepRateRestore.Variable->Set(500.0f, ECVF_SetByCode);
	TestEqual(TEXT("Console cap is clamped to 240 Hz"), TDFluidRuntimeOps::ResolveMaxStepRateHz(CapSettings), 240.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsCellSizeNormalizationTest,
	"TDGame.FluidFX.Ops.CellSizeNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsCellSizeNormalizationTest::RunTest(const FString& Parameters)
{
	using TDFluidRuntimeOpsTestPrivate::IsSameSimulationConfig;
	using TDFluidRuntimeOpsTestPrivate::MakeNormalizationConfig;

	const FTDFluidSimulationConfig BaseConfig = MakeNormalizationConfig();
	FTDFluidRuntimeOpsSettings DisabledSettings;
	TestTrue(TEXT("Disabled normalization returns the input"), IsSameSimulationConfig(TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(BaseConfig, 32, DisabledSettings, true), BaseConfig));

	FTDFluidRuntimeOpsSettings EnabledSettings;
	EnabledSettings.bNormalizeDynamicsByCellSize = true;

	FTDFluidSimulationConfig WaterConfig = BaseConfig;
	WaterConfig.bEnableWaterLayer = true;
	TestTrue(TEXT("Water layer config is left alone"), IsSameSimulationConfig(TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(WaterConfig, 32, EnabledSettings, true), WaterConfig));

	FTDFluidSimulationConfig PainterConfig = BaseConfig;
	PainterConfig.SimMode = ETDFluidSimMode::SimplePainter;
	TestTrue(TEXT("Simple painter config is left alone"), IsSameSimulationConfig(TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(PainterConfig, 32, EnabledSettings, true), PainterConfig));
	TestTrue(TEXT("Zero resolution is left alone"), IsSameSimulationConfig(TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(BaseConfig, 0, EnabledSettings, true), BaseConfig));
	TestTrue(TEXT("Authored resolution keeps the config"), IsSameSimulationConfig(TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(BaseConfig, BaseConfig.GridResolution, EnabledSettings, true), BaseConfig));

	const FTDFluidSimulationConfig HalfScaleConfig = TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(BaseConfig, 32, EnabledSettings, true);
	TestTrue(TEXT("Half cell scale halves vorticity"), FMath::IsNearlyEqual(HalfScaleConfig.VorticityStrength, BaseConfig.VorticityStrength * 0.5f, 1.0e-6f));
	TestEqual(TEXT("Half cell scale quarters pressure iterations"), HalfScaleConfig.PressureIterations, FMath::RoundToInt32(BaseConfig.PressureIterations * 0.25f));
	FTDFluidSimulationConfig HalfScaleExpected = BaseConfig;
	HalfScaleExpected.VorticityStrength = HalfScaleConfig.VorticityStrength;
	HalfScaleExpected.PressureIterations = HalfScaleConfig.PressureIterations;
	TestTrue(TEXT("Normalization changes only vorticity and pressure iterations"), IsSameSimulationConfig(HalfScaleConfig, HalfScaleExpected));

	FTDFluidSimulationConfig SingleIterationConfig = BaseConfig;
	SingleIterationConfig.PressureIterations = 1;
	TestEqual(TEXT("Pressure iterations never drop below 1"), TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(SingleIterationConfig, 32, EnabledSettings, true).PressureIterations, 1);

	const FTDFluidSimulationConfig DoubleScaleConfig = TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(BaseConfig, 128, EnabledSettings, true);
	TestTrue(TEXT("Double cell scale doubles vorticity"), FMath::IsNearlyEqual(DoubleScaleConfig.VorticityStrength, BaseConfig.VorticityStrength * 2.0f, 1.0e-6f));
	TestEqual(TEXT("Double cell scale quadruples pressure iterations"), DoubleScaleConfig.PressureIterations, FMath::Min(BaseConfig.PressureIterations * 4, 32));

	FTDFluidSimulationConfig ManyIterationConfig = BaseConfig;
	ManyIterationConfig.PressureIterations = 12;
	ManyIterationConfig.VorticityStrength = 15.0f;
	const FTDFluidSimulationConfig ClampedConfig = TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(ManyIterationConfig, 128, EnabledSettings, true);
	TestEqual(TEXT("Pressure iterations clamp at 32"), ClampedConfig.PressureIterations, 32);
	TestEqual(TEXT("Vorticity clamps at 20"), ClampedConfig.VorticityStrength, 20.0f);

	const FTDFluidSimulationConfig GpuScaleConfig = TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(BaseConfig, 128, EnabledSettings, false);
	TestEqual(TEXT("GPU normalization keeps pressure iterations"), GpuScaleConfig.PressureIterations, BaseConfig.PressureIterations);
	TestTrue(TEXT("GPU normalization still scales vorticity"), FMath::IsNearlyEqual(GpuScaleConfig.VorticityStrength, BaseConfig.VorticityStrength * 2.0f, 1.0e-6f));

	FTDFluidRuntimeOpsSettings ReferenceSettings = EnabledSettings;
	ReferenceSettings.ReferenceCellSizeCm = static_cast<float>(BaseConfig.SimExtentCm.X / BaseConfig.GridResolution) * 2.0f;
	const FTDFluidSimulationConfig ReferenceScaleConfig = TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(BaseConfig, BaseConfig.GridResolution, ReferenceSettings, true);
	TestTrue(TEXT("Reference cell size overrides the authored cell size"), FMath::IsNearlyEqual(ReferenceScaleConfig.VorticityStrength, BaseConfig.VorticityStrength * 2.0f, 1.0e-6f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsVisibilityPauseDecisionTest,
	"TDGame.FluidFX.Ops.VisibilityPauseDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsVisibilityPauseDecisionTest::RunTest(const FString& Parameters)
{
	using TDFluidRuntimeOpsTestPrivate::FTDOpsConsoleRestore;

	struct FTDOpsPauseCase
	{
		int32 PauseMode;
		bool bIsEligible;
		bool bCanJudgeVisibility;
		bool bWasRecentlyRendered;
		bool bExpectsPause;
	};

	const FTDOpsPauseCase PauseCases[] = {
		{ 0, true, true, false, false },
		{ 0, true, false, false, false },
		{ 0, false, true, false, false },
		{ 1, false, true, false, false },
		{ 1, true, false, false, false },
		{ 1, true, false, true, false },
		{ 1, true, true, false, true },
		{ 1, true, true, true, false },
		{ 2, false, false, false, false },
		{ 2, true, false, false, true },
		{ 2, true, false, true, false },
		{ 2, true, true, false, true },
		{ 2, true, true, true, false },
	};

	for (const FTDOpsPauseCase& PauseCase : PauseCases)
	{
		const bool bPauses = TDFluidRuntimeOps::ShouldPauseForVisibility(PauseCase.PauseMode, PauseCase.bIsEligible, PauseCase.bCanJudgeVisibility, PauseCase.bWasRecentlyRendered);
		TestTrue(FString::Printf(TEXT("Mode %d eligible %d gate %d rendered %d"), PauseCase.PauseMode, PauseCase.bIsEligible ? 1 : 0, PauseCase.bCanJudgeVisibility ? 1 : 0, PauseCase.bWasRecentlyRendered ? 1 : 0), bPauses == PauseCase.bExpectsPause);
	}

	FTDOpsConsoleRestore PauseModeRestore(TEXT("TD.FluidFX.PauseWhenNotVisible"));
	if (!TestNotNull(TEXT("TD.FluidFX.PauseWhenNotVisible exists"), PauseModeRestore.Variable))
	{
		return false;
	}

	FTDFluidRuntimeOpsSettings PauseSettings;
	PauseModeRestore.Variable->Set(-1, ECVF_SetByCode);
	TestEqual(TEXT("Default tuning keeps visibility pause off"), TDFluidRuntimeOps::ResolvePauseWhenNotVisibleMode(PauseSettings), 0);
	PauseSettings.bPauseWhenNotVisible = true;
	TestEqual(TEXT("Tuning flag selects the gated mode"), TDFluidRuntimeOps::ResolvePauseWhenNotVisibleMode(PauseSettings), 1);
	PauseModeRestore.Variable->Set(0, ECVF_SetByCode);
	TestEqual(TEXT("Console 0 forces visibility pause off"), TDFluidRuntimeOps::ResolvePauseWhenNotVisibleMode(PauseSettings), 0);
	PauseModeRestore.Variable->Set(2, ECVF_SetByCode);
	TestEqual(TEXT("Console 2 forces the ungated mode"), TDFluidRuntimeOps::ResolvePauseWhenNotVisibleMode(PauseSettings), 2);
	PauseModeRestore.Variable->Set(7, ECVF_SetByCode);
	TestEqual(TEXT("Console modes above 2 clamp to 2"), TDFluidRuntimeOps::ResolvePauseWhenNotVisibleMode(PauseSettings), 2);

	TestFalse(TEXT("Shared MPC arbitration is off by default"), TDFluidRuntimeOps::IsSharedMpcArbitrationEnabled());

	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidOpsVisibilityWorld"));
	UWorld* ViewportlessWorld = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	if (!TestNotNull(TEXT("Viewportless world is created"), ViewportlessWorld))
	{
		return false;
	}

	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(ViewportlessWorld);
	TestFalse(TEXT("A world without a game viewport cannot judge visibility"), TDFluidRuntimeOps::CanJudgeVisibility(*ViewportlessWorld));
	GEngine->DestroyWorldContext(ViewportlessWorld);
	ViewportlessWorld->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsExternalTargetValidationTest,
	"TDGame.FluidFX.Ops.ExternalTargetValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsExternalTargetValidationTest::RunTest(const FString& Parameters)
{
	using TDFluidRuntimeOpsTestPrivate::IsUsableOpsTestTarget;
	using TDFluidRuntimeOpsTestPrivate::MakeUsableTarget;
	using TDFluidRuntimeOpsTestPrivate::OpsTestClearColor;
	using TDFluidRuntimeOpsTestPrivate::OpsTestTargetResolution;

	UTextureRenderTarget2D* MatchingTarget = MakeUsableTarget();
	TestTrue(TEXT("Matching target is usable"), IsUsableOpsTestTarget(MatchingTarget));
	TestFalse(TEXT("Null target is rejected"), IsUsableOpsTestTarget(nullptr));
	TestFalse(TEXT("Zero resolution is rejected"), TDFluidRuntimeOps::IsUsableExternalOutputTarget(MatchingTarget, 0, OpsTestClearColor, TA_Wrap));
	TestFalse(TEXT("Expected clamp address rejects a wrapped target"), TDFluidRuntimeOps::IsUsableExternalOutputTarget(MatchingTarget, OpsTestTargetResolution, OpsTestClearColor, TA_Clamp));

	UTextureRenderTarget2D* FormatTarget = MakeUsableTarget();
	FormatTarget->RenderTargetFormat = RTF_RGBA8;
	TestFalse(TEXT("Non half float format is rejected"), IsUsableOpsTestTarget(FormatTarget));

	UTextureRenderTarget2D* SmallTarget = MakeUsableTarget();
	SmallTarget->SizeX = OpsTestTargetResolution / 2;
	SmallTarget->SizeY = OpsTestTargetResolution / 2;
	TestFalse(TEXT("Different resolution is rejected"), IsUsableOpsTestTarget(SmallTarget));

	UTextureRenderTarget2D* NonSquareTarget = MakeUsableTarget();
	NonSquareTarget->SizeY = OpsTestTargetResolution / 2;
	TestFalse(TEXT("Non square target is rejected"), IsUsableOpsTestTarget(NonSquareTarget));

	UTextureRenderTarget2D* NoUavTarget = MakeUsableTarget();
	NoUavTarget->bSupportsUAV = false;
	NoUavTarget->bCanCreateUAV = false;
	TestFalse(TEXT("Target without unordered access is rejected"), IsUsableOpsTestTarget(NoUavTarget));

	UTextureRenderTarget2D* CreatableUavTarget = MakeUsableTarget();
	CreatableUavTarget->bSupportsUAV = false;
	TestTrue(TEXT("Target that can create unordered access is usable"), IsUsableOpsTestTarget(CreatableUavTarget));

	UTextureRenderTarget2D* ClearColorTarget = MakeUsableTarget();
	ClearColorTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	TestFalse(TEXT("Different clear color is rejected"), IsUsableOpsTestTarget(ClearColorTarget));

	UTextureRenderTarget2D* AddressXTarget = MakeUsableTarget();
	AddressXTarget->AddressX = TA_Clamp;
	TestFalse(TEXT("Different horizontal address is rejected"), IsUsableOpsTestTarget(AddressXTarget));

	UTextureRenderTarget2D* AddressYTarget = MakeUsableTarget();
	AddressYTarget->AddressY = TA_Clamp;
	TestFalse(TEXT("Different vertical address is rejected"), IsUsableOpsTestTarget(AddressYTarget));

	UTextureRenderTarget2D* MipTarget = MakeUsableTarget();
	MipTarget->bAutoGenerateMips = true;
	TestFalse(TEXT("Target with automatic mips is rejected"), IsUsableOpsTestTarget(MipTarget));

	FTDFluidExternalOutputTargets ExternalTargets;
	TestFalse(TEXT("Empty external targets report no target"), ExternalTargets.HasAnyTarget());
	ExternalTargets.WaterSurfaceTarget = TSoftObjectPtr<UTextureRenderTarget2D>(MatchingTarget);
	TestTrue(TEXT("Any assigned external target is reported"), ExternalTargets.HasAnyTarget());
	return true;
}

#endif
