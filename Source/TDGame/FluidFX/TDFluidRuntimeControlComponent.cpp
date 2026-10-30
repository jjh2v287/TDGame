#include "FluidFX/TDFluidRuntimeControlComponent.h"

#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"

namespace TDFluidRuntimeControlPrivate
{
	bool AreConfigsIdentical(const FTDFluidSimulationConfig& First, const FTDFluidSimulationConfig& Second)
	{
		return FTDFluidSimulationConfig::StaticStruct()->CompareScriptStruct(&First, &Second, PPF_None);
	}

	FTDFluidSimulationConfig ComposeControlledConfig(
		const FTDFluidSimulationConfig& BaseConfig,
		const UTDFluidRuntimeControlComponent& Control,
		float FadeFraction,
		float FadeDissipationPerSec)
	{
		const float FadeDissipation = FadeDissipationPerSec * FadeFraction;
		const float InputScale = Control.InputStrengthScale * (1.0f - FadeFraction);
		FTDFluidSimulationConfig Composed = BaseConfig;
		Composed.DensityDissipation = BaseConfig.DensityDissipation * Control.DissipationScale + FadeDissipation;
		Composed.HeatCoolingPerSec = BaseConfig.HeatCoolingPerSec + FadeDissipation;
		Composed.ActorBrushDensityStrength = BaseConfig.ActorBrushDensityStrength * InputScale;
		Composed.AmbientRefillPerSec = BaseConfig.AmbientRefillPerSec * InputScale;
		Composed.VorticityStrength = BaseConfig.VorticityStrength * Control.VorticityScale;
		Composed.FlowNoiseAccelerationCmPerSec2 = BaseConfig.FlowNoiseAccelerationCmPerSec2 * Control.FlowNoiseScale;
		return Composed;
	}
}

UTDFluidRuntimeControlComponent::UTDFluidRuntimeControlComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

UTDFluidRuntimeControlComponent* UTDFluidRuntimeControlComponent::FindOrAddRuntimeControl(ATDFluidLiveActor* LiveActor)
{
	if (!LiveActor)
	{
		return nullptr;
	}

	if (UTDFluidRuntimeControlComponent* ExistingControl = LiveActor->FindComponentByClass<UTDFluidRuntimeControlComponent>())
	{
		return ExistingControl;
	}

	UTDFluidRuntimeControlComponent* NewControl = NewObject<UTDFluidRuntimeControlComponent>(LiveActor);
	LiveActor->AddInstanceComponent(NewControl);
	NewControl->RegisterComponent();
	return NewControl;
}

void UTDFluidRuntimeControlComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UTDFluidLiveComponent* LiveComponent = ResolveLiveComponent();
	if (!LiveComponent)
	{
		return;
	}

	RefreshBaseConfig(*LiveComponent);
	if (bIsAwaitingFadeSleep)
	{
		FinishFadeOut(*LiveComponent);
		return;
	}

	const float FadeFraction = AdvanceFadeFraction(DeltaTime);
	ApplyComposedConfig(*LiveComponent, FadeFraction);
	if (!bIsFadingOut || FadeFraction < 1.0f)
	{
		return;
	}

	LiveComponent->ResetSimulationState();
	bIsAwaitingFadeSleep = true;
}

void UTDFluidRuntimeControlComponent::FadeOut(float Seconds)
{
	const UTDFluidLiveComponent* LiveComponent = ResolveLiveComponent();
	if (!LiveComponent)
	{
		return;
	}

	const float RequestedSeconds = Seconds > 0.0f ? Seconds : LiveComponent->GetRuntimeOpsSettings().FadeOutSeconds;
	FadeDurationSeconds = FMath::Max(RequestedSeconds, UE_KINDA_SMALL_NUMBER);
	FadeElapsedSeconds = 0.0f;
	bIsFadingOut = true;
	bIsAwaitingFadeSleep = false;
}

void UTDFluidRuntimeControlComponent::FadeIn()
{
	RestoreFromFade();
}

void UTDFluidRuntimeControlComponent::CancelFadeOut()
{
	RestoreFromFade();
}

UTDFluidLiveComponent* UTDFluidRuntimeControlComponent::ResolveLiveComponent() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UTDFluidLiveComponent>() : nullptr;
}

void UTDFluidRuntimeControlComponent::RefreshBaseConfig(const UTDFluidLiveComponent& LiveComponent)
{
	if (bHasAppliedConfig && TDFluidRuntimeControlPrivate::AreConfigsIdentical(LiveComponent.SimulationConfig, ReadBackConfig))
	{
		return;
	}

	BaseConfig = LiveComponent.SimulationConfig;
	bHasAppliedConfig = false;
}

float UTDFluidRuntimeControlComponent::AdvanceFadeFraction(float DeltaTime)
{
	if (!bIsFadingOut)
	{
		return 0.0f;
	}

	FadeElapsedSeconds += FMath::Max(DeltaTime, 0.0f);
	return FMath::Clamp(FadeElapsedSeconds / FadeDurationSeconds, 0.0f, 1.0f);
}

void UTDFluidRuntimeControlComponent::ApplyComposedConfig(UTDFluidLiveComponent& LiveComponent, float FadeFraction)
{
	LiveComponent.SetWaterDisplayAlphaScale(WaterDisplayAlpha * (1.0f - FadeFraction));
	const float FadeDissipationPerSec = LiveComponent.GetRuntimeOpsSettings().FadeOutDissipationPerSec;
	const FTDFluidSimulationConfig Composed = TDFluidRuntimeControlPrivate::ComposeControlledConfig(BaseConfig, *this, FadeFraction, FadeDissipationPerSec);
	const FTDFluidSimulationConfig& Reference = bHasAppliedConfig ? LastComposedConfig : LiveComponent.SimulationConfig;
	if (TDFluidRuntimeControlPrivate::AreConfigsIdentical(Composed, Reference))
	{
		return;
	}

	LiveComponent.ApplyRuntimeConfigOverride(Composed);
	LastComposedConfig = Composed;
	ReadBackConfig = LiveComponent.SimulationConfig;
	bHasAppliedConfig = true;
	++AppliedConfigCount;
}

void UTDFluidRuntimeControlComponent::FinishFadeOut(UTDFluidLiveComponent& LiveComponent)
{
	ATDFluidLiveActor* LiveActor = Cast<ATDFluidLiveActor>(GetOwner());
	if (LiveActor && !bHasSavedActivationFlag)
	{
		bSavedShouldUseActivationVolume = LiveActor->bShouldUseActivationVolume;
		bHasSavedActivationFlag = true;
		LiveActor->bShouldUseActivationVolume = false;
	}

	LiveComponent.SetSimulationAwake(false);
	bIsAwaitingFadeSleep = false;
	bIsFadingOut = false;
	FadeElapsedSeconds = 0.0f;
	ApplyComposedConfig(LiveComponent, 0.0f);
}

void UTDFluidRuntimeControlComponent::RestoreFromFade()
{
	bIsFadingOut = false;
	bIsAwaitingFadeSleep = false;
	FadeElapsedSeconds = 0.0f;
	ATDFluidLiveActor* LiveActor = Cast<ATDFluidLiveActor>(GetOwner());
	if (LiveActor && bHasSavedActivationFlag)
	{
		LiveActor->bShouldUseActivationVolume = bSavedShouldUseActivationVolume;
		bHasSavedActivationFlag = false;
	}

	UTDFluidLiveComponent* LiveComponent = ResolveLiveComponent();
	if (!LiveComponent)
	{
		return;
	}

	LiveComponent->SetSimulationAwake(true);
	RefreshBaseConfig(*LiveComponent);
	ApplyComposedConfig(*LiveComponent, 0.0f);
}
