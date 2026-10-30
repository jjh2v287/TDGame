#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidRuntimeControlComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidRuntimeControlTestPrivate
{
	constexpr float RuntimeControlStepSeconds = 1.0f / 60.0f;
	constexpr float RuntimeControlFadeSeconds = 0.2f;
	constexpr int32 RuntimeControlStampFrames = 12;
	constexpr double RuntimeControlRelativeTolerance = 1.0e-3;

	bool AreConfigsIdentical(const FTDFluidSimulationConfig& First, const FTDFluidSimulationConfig& Second)
	{
		return FTDFluidSimulationConfig::StaticStruct()->CompareScriptStruct(&First, &Second, PPF_None);
	}

	double SumDensity(const UTDFluidLiveComponent& Component)
	{
		const int32 Resolution = Component.GetSimulationResolution();
		double DensitySum = 0.0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = Component.SampleSimulationCell(CellX, CellY);
				DensitySum += FMath::Abs(Sample.PrimaryDensity) + FMath::Abs(Sample.SelectiveDensity);
			}
		}
		return DensitySum;
	}

	struct FTDFluidRuntimeControlTestWorld
	{
		FTDFluidRuntimeControlTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidRuntimeControlTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidRuntimeControlTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, RuntimeControlStepSeconds);
		}

		ATDFluidLiveActor* SpawnMudActor()
		{
			UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
			Preset = UTDFluidPresetAsset::CreateTransientPreset(World, ETDFluidPresetKind::Mud);
			ATDFluidLiveActor* LiveActor = FluidSubsystem ? FluidSubsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 0.0f) : nullptr;
			if (!LiveActor || !LiveActor->LiveComponent)
			{
				return nullptr;
			}

			LiveActor->LiveComponent->SetSimulationAwake(true);
			StepWorld();
			StepWorld();
			return LiveActor;
		}

		void QueueMudStamp(UTDFluidLiveComponent& Component) const
		{
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector::ZeroVector;
			Stamp.RadiusCm = 150.0f;
			Stamp.DensityStrength = 1.0f;
			Stamp.VelocityStrength = 0.0f;
			Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
			Component.QueueBrushStamp(Stamp);
		}

		UWorld* World = nullptr;
		UTDFluidPresetAsset* Preset = nullptr;
	};

	void EnsureNonZeroDissipation(UTDFluidLiveComponent& Component)
	{
		FTDFluidSimulationConfig Config = Component.SimulationConfig;
		Config.DensityDissipation = FMath::Max(Config.DensityDissipation, 0.25f);
		Component.ApplyRuntimeConfigOverride(Config);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRuntimeControlFadeOutMonotonicToZeroTest,
	"TDGame.FluidFX.RuntimeControl.FadeOutMonotonicToZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRuntimeControlFadeOutMonotonicToZeroTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRuntimeControlTestPrivate;

	FTDFluidRuntimeControlTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = TestWorld.SpawnMudActor();
	if (!TestNotNull(TEXT("Mud actor spawns"), LiveActor))
	{
		return false;
	}

	UTDFluidLiveComponent* Component = LiveActor->LiveComponent;
	if (!TestFalse(TEXT("Mud runs on the CPU solver"), Component->IsUsingGpuSolver()))
	{
		return false;
	}

	LiveActor->bShouldUseActivationVolume = true;
	UTDFluidRuntimeControlComponent* Control = UTDFluidRuntimeControlComponent::FindOrAddRuntimeControl(LiveActor);
	if (!TestNotNull(TEXT("Runtime control is added"), Control))
	{
		return false;
	}

	TestTrue(TEXT("FindOrAdd returns the existing control"), UTDFluidRuntimeControlComponent::FindOrAddRuntimeControl(LiveActor) == Control);
	for (int32 FrameIndex = 0; FrameIndex < RuntimeControlStampFrames; ++FrameIndex)
	{
		TestWorld.QueueMudStamp(*Component);
		TestWorld.StepWorld();
	}

	const FTDFluidSimulationConfig BaseConfig = Component->SimulationConfig;
	double PreviousSum = SumDensity(*Component);
	if (!TestTrue(TEXT("Stamped mud has density before the fade"), PreviousSum > 0.0))
	{
		return false;
	}

	Control->FadeOut(RuntimeControlFadeSeconds);
	TestTrue(TEXT("Fade out starts"), Control->IsFadingOut());
	const int32 FadeFrames = FMath::CeilToInt(RuntimeControlFadeSeconds / RuntimeControlStepSeconds);
	bool bIsMonotonic = true;
	bool bHasCheckedMidFadeInputs = false;
	int32 ZeroDensityFrame = INDEX_NONE;
	for (int32 FrameIndex = 0; FrameIndex < FadeFrames + 3; ++FrameIndex)
	{
		TestWorld.StepWorld();
		const double DensitySum = SumDensity(*Component);
		bIsMonotonic &= DensitySum <= PreviousSum * (1.0 + RuntimeControlRelativeTolerance) + RuntimeControlRelativeTolerance;
		PreviousSum = DensitySum;
		const float FadeDissipation = Component->SimulationConfig.HeatCoolingPerSec - BaseConfig.HeatCoolingPerSec;
		const float FadeDissipationPerSec = Component->GetRuntimeOpsSettings().FadeOutDissipationPerSec;
		if (!bHasCheckedMidFadeInputs && FadeDissipation > 0.0f && FadeDissipation < FadeDissipationPerSec && FadeDissipationPerSec > 0.0f)
		{
			const float FadeFraction = FadeDissipation / FadeDissipationPerSec;
			TestEqual(TEXT("Mid fade dissipation adds the fade term"), Component->SimulationConfig.DensityDissipation, BaseConfig.DensityDissipation + FadeDissipation, 1.0e-4f);
			TestEqual(TEXT("Mid fade actor brush strength follows one minus the fade"), Component->SimulationConfig.ActorBrushDensityStrength, BaseConfig.ActorBrushDensityStrength * (1.0f - FadeFraction), 1.0e-4f);
			bHasCheckedMidFadeInputs = true;
		}

		if (DensitySum == 0.0)
		{
			ZeroDensityFrame = FrameIndex + 1;
			break;
		}
	}

	TestTrue(TEXT("Density sum does not increase during the fade"), bIsMonotonic);
	TestTrue(TEXT("A mid fade frame was observed"), bHasCheckedMidFadeInputs);
	TestTrue(*FString::Printf(TEXT("Density reaches zero by one frame after the fade (frame %d of %d)"), ZeroDensityFrame, FadeFrames), ZeroDensityFrame != INDEX_NONE && ZeroDensityFrame <= FadeFrames + 1);
	TestWorld.StepWorld();
	TestEqual(TEXT("Density stays zero while going to sleep"), SumDensity(*Component), 0.0);
	TestFalse(TEXT("Fade out has finished"), Control->IsFadingOut());
	TestFalse(TEXT("Faded actor sleeps"), Component->IsSimulationAwake());
	TestFalse(TEXT("Activation volume flag is cleared to block proximity wake"), LiveActor->bShouldUseActivationVolume);
	TestTrue(TEXT("Base simulation config is restored"), AreConfigsIdentical(Component->SimulationConfig, BaseConfig));
	TestEqual(TEXT("Water display scale is restored"), Component->GetWaterDisplayAlphaScale(), 1.0f);

	Control->FadeIn();
	TestTrue(TEXT("Fade in restores the activation volume flag"), LiveActor->bShouldUseActivationVolume);
	TestTrue(TEXT("Fade in wakes the simulation"), Component->IsSimulationAwake());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRuntimeControlInterpScaleWithinOneTickTest,
	"TDGame.FluidFX.RuntimeControl.InterpScaleWithinOneTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRuntimeControlInterpScaleWithinOneTickTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRuntimeControlTestPrivate;

	FTDFluidRuntimeControlTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = TestWorld.SpawnMudActor();
	if (!TestNotNull(TEXT("Mud actor spawns"), LiveActor))
	{
		return false;
	}

	UTDFluidLiveComponent* Component = LiveActor->LiveComponent;
	EnsureNonZeroDissipation(*Component);
	const FTDFluidSimulationConfig ConfigBeforeControl = Component->SimulationConfig;
	UTDFluidRuntimeControlComponent* Control = UTDFluidRuntimeControlComponent::FindOrAddRuntimeControl(LiveActor);
	if (!TestNotNull(TEXT("Runtime control is added"), Control))
	{
		return false;
	}

	TestWorld.StepWorld();
	TestWorld.StepWorld();
	TestEqual(TEXT("Unit scales apply no config"), Control->GetAppliedConfigCount(), 0);
	TestTrue(TEXT("Unit scales leave the config untouched"), AreConfigsIdentical(Component->SimulationConfig, ConfigBeforeControl));

	const int32 GenerationBefore = Component->GetSimulationGeneration();
	Control->DissipationScale = 2.0f;
	TestWorld.StepWorld();
	TestEqual(TEXT("Dissipation doubles within one tick"), Component->SimulationConfig.DensityDissipation, ConfigBeforeControl.DensityDissipation * 2.0f, 1.0e-6f);
	TestEqual(TEXT("Scaled config is applied once"), Control->GetAppliedConfigCount(), 1);
	TestEqual(TEXT("Scaling keeps the simulation generation"), Component->GetSimulationGeneration(), GenerationBefore);

	TestWorld.StepWorld();
	TestEqual(TEXT("Unchanged scales apply nothing again"), Control->GetAppliedConfigCount(), 1);
	TestTrue(TEXT("Base stays the adopted config"), AreConfigsIdentical(Control->GetBaseConfig(), ConfigBeforeControl));

	Control->DissipationScale = 1.0f;
	TestWorld.StepWorld();
	TestTrue(TEXT("Unit scale restores the base config"), AreConfigsIdentical(Component->SimulationConfig, ConfigBeforeControl));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRuntimeControlReapplyAdoptsNewBaseTest,
	"TDGame.FluidFX.RuntimeControl.ReapplyAdoptsNewBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRuntimeControlReapplyAdoptsNewBaseTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRuntimeControlTestPrivate;

	FTDFluidRuntimeControlTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = TestWorld.SpawnMudActor();
	if (!TestNotNull(TEXT("Mud actor spawns"), LiveActor) || !TestNotNull(TEXT("Preset exists"), TestWorld.Preset))
	{
		return false;
	}

	UTDFluidLiveComponent* Component = LiveActor->LiveComponent;
	UTDFluidRuntimeControlComponent* Control = UTDFluidRuntimeControlComponent::FindOrAddRuntimeControl(LiveActor);
	if (!TestNotNull(TEXT("Runtime control is added"), Control))
	{
		return false;
	}

	Control->DissipationScale = 2.0f;
	TestWorld.StepWorld();
	Control->FadeOut(10.0f);
	TestWorld.StepWorld();
	TestWorld.StepWorld();
	TestTrue(TEXT("Fade is in progress"), Control->IsFadingOut());

	FTDFluidSimulationConfig NewBase = Control->GetBaseConfig();
	NewBase.DensityDissipation += 0.375f;
	NewBase.HeatCoolingPerSec += 0.5f;
	NewBase.ActorBrushDensityStrength += 0.25f;
	TestWorld.Preset->bOverridePresetDefaults = true;
	TestWorld.Preset->SimulationConfig = NewBase;
	FTDFluidPresetReapplyReport Report;
	Component->ReapplyPresetTuning(Report);
	TestEqual(TEXT("Reapply still owns the overridden config"), Component->SimulationConfig.DensityDissipation, NewBase.DensityDissipation);

	TestWorld.StepWorld();
	TestTrue(TEXT("Control adopts the reapplied config as its base"), AreConfigsIdentical(Control->GetBaseConfig(), Component->SimulationConfig) == false && AreConfigsIdentical(Control->GetBaseConfig(), TestWorld.Preset->ResolveEffectiveConfig()));
	const float FadeDissipation = Component->SimulationConfig.HeatCoolingPerSec - NewBase.HeatCoolingPerSec;
	const float FadeDissipationPerSec = Component->GetRuntimeOpsSettings().FadeOutDissipationPerSec;
	TestTrue(TEXT("Fade term stays between zero and its maximum"), FadeDissipation >= 0.0f && FadeDissipation < FadeDissipationPerSec);
	TestEqual(TEXT("Dissipation is the new base times the scale plus the fade term"), Component->SimulationConfig.DensityDissipation, NewBase.DensityDissipation * 2.0f + FadeDissipation, 1.0e-4f);
	const float FadeFraction = FadeDissipationPerSec > 0.0f ? FadeDissipation / FadeDissipationPerSec : 0.0f;
	TestEqual(TEXT("Brush strength follows the new base without accumulation"), Component->SimulationConfig.ActorBrushDensityStrength, NewBase.ActorBrushDensityStrength * (1.0f - FadeFraction), 1.0e-4f);
	return true;
}

#endif
