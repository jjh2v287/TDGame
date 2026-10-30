#include "FluidFX/TDFluidRainActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidRainImpacts.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

namespace TDFluidRainActorPrivate
{
	const FName RainIntensityVariableName(TEXT("User.TDRainIntensity"));

	bool TryGetFirstLocalCameraLocation(const UWorld& World, FVector& OutCameraLocation)
	{
		const APlayerController* PlayerController = GEngine ? GEngine->GetFirstLocalPlayerController(&World) : nullptr;
		const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
		if (!CameraManager)
		{
			return false;
		}

		OutCameraLocation = CameraManager->GetCameraLocation();
		return true;
	}
}

ATDFluidRainActor::ATDFluidRainActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RainVisual = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RainVisual"));
	RainVisual->SetupAttachment(RootComponent);
	RainVisual->SetAutoActivate(false);
}

void ATDFluidRainActor::BeginPlay()
{
	Super::BeginPlay();

	RequestRainSystem();
	PushRainIntensity();
}

void ATDFluidRainActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RainSystemLoadHandle.IsValid())
	{
		RainSystemLoadHandle->CancelHandle();
		RainSystemLoadHandle.Reset();
	}
	if (RainVisual)
	{
		RainVisual->Deactivate();
	}

	UWorld* World = GetWorld();
	UTDFluidLiveSubsystem* Subsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	if (Subsystem)
	{
		Subsystem->SetRainIntensity(0.0f);
	}

	Super::EndPlay(EndPlayReason);
}

void ATDFluidRainActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FollowViewTarget();
	PushRainIntensity();
}

void ATDFluidRainActor::SetRainIntensity(float InRainIntensity)
{
	RainIntensity = FTDFluidRainImpacts::SanitizeIntensity(InRainIntensity);
}

void ATDFluidRainActor::RequestRainSystem()
{
	if (RainSystem.IsNull() || !RainVisual)
	{
		return;
	}

	if (RainSystem.Get())
	{
		ApplyLoadedRainSystem();
		return;
	}

	RainSystemLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RainSystem.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &ATDFluidRainActor::ApplyLoadedRainSystem));
}

void ATDFluidRainActor::ApplyLoadedRainSystem()
{
	UNiagaraSystem* LoadedSystem = RainSystem.Get();
	if (!LoadedSystem || !RainVisual)
	{
		return;
	}

	RainVisual->SetAsset(LoadedSystem);
	RainVisual->SetVariableFloat(TDFluidRainActorPrivate::RainIntensityVariableName, RainIntensity);
	RainVisual->Activate(true);
}

void ATDFluidRainActor::FollowViewTarget()
{
	const UWorld* World = GetWorld();
	FVector CameraLocation = FVector::ZeroVector;
	if (!bShouldFollowViewTarget || !World || !TDFluidRainActorPrivate::TryGetFirstLocalCameraLocation(*World, CameraLocation))
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	const FVector FollowLocation(CameraLocation.X, CameraLocation.Y, CurrentLocation.Z);
	if (CurrentLocation.Equals(FollowLocation, 0.01))
	{
		return;
	}
	SetActorLocation(FollowLocation);
}

void ATDFluidRainActor::PushRainIntensity()
{
	UWorld* World = GetWorld();
	UTDFluidLiveSubsystem* Subsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	if (Subsystem)
	{
		Subsystem->SetRainIntensity(RainIntensity);
	}

	if (!RainVisual || !RainVisual->GetFXSystemAsset())
	{
		return;
	}
	RainVisual->SetVariableFloat(TDFluidRainActorPrivate::RainIntensityVariableName, RainIntensity);
}
