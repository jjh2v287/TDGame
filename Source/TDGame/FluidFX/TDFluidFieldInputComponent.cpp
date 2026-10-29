#include "FluidFX/TDFluidFieldInputComponent.h"

#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveSubsystem.h"

UTDFluidFieldInputComponent::UTDFluidFieldInputComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTDFluidFieldInputComponent::BeginPlay()
{
	Super::BeginPlay();
	RequestFieldTextureLoad();
	UWorld* World = GetWorld();
	if (UTDFluidLiveSubsystem* Subsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr)
	{
		Subsystem->RegisterFieldInputSource(this);
	}
}

void UTDFluidFieldInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (UTDFluidLiveSubsystem* Subsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr)
	{
		Subsystem->UnregisterFieldInputSource(this);
	}

	if (FieldTextureLoadHandle.IsValid())
	{
		FieldTextureLoadHandle->CancelHandle();
		FieldTextureLoadHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void UTDFluidFieldInputComponent::RequestFieldTextureLoad()
{
	if (FieldTexture.IsNull())
	{
		return;
	}

	if (UTexture* ResidentTexture = FieldTexture.Get())
	{
		LoadedFieldTexture = ResidentTexture;
		return;
	}

	FieldTextureLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		FieldTexture.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &ThisClass::HandleFieldTextureLoaded));
}

void UTDFluidFieldInputComponent::HandleFieldTextureLoaded()
{
	LoadedFieldTexture = FieldTexture.Get();
	FieldTextureLoadHandle.Reset();
}

void UTDFluidFieldInputComponent::UpdateMotion(float DeltaTime)
{
	const FVector WorldLocation = GetComponentLocation();
	WorldVelocity = bHasPreviousWorldLocation && DeltaTime > 0.0f ? (WorldLocation - PreviousWorldLocation) / DeltaTime : FVector::ZeroVector;
	PreviousWorldLocation = WorldLocation;
	bHasPreviousWorldLocation = true;
}

bool UTDFluidFieldInputComponent::BuildPlacement(double WorldTimeSeconds, FTDFluidFieldInputPlacement& OutPlacement) const
{
	if (!LoadedFieldTexture || FieldSizeCm.X <= 0.0 || FieldSizeCm.Y <= 0.0)
	{
		return false;
	}

	OutPlacement.Texture = LoadedFieldTexture;
	OutPlacement.WorldCenter = GetComponentLocation();
	OutPlacement.WorldAxisX = GetForwardVector();
	OutPlacement.SizeCm = FieldSizeCm;
	OutPlacement.WorldVelocity = WorldVelocity;
	OutPlacement.Layout = Layout;
	OutPlacement.bSignedVelocityEncoding = bUsesSignedVelocityEncoding;
	OutPlacement.UVTileCount = FMath::Clamp(UVTileCount, 0.01f, 64.0f);
	OutPlacement.UVOffset = FVector2D(FMath::Frac(UVScrollPerSec.X * WorldTimeSeconds), FMath::Frac(UVScrollPerSec.Y * WorldTimeSeconds));
	OutPlacement.UVRotationRadians = FMath::DegreesToRadians(UVRotationDegrees);
	OutPlacement.bTiled = OutPlacement.UVTileCount != 1.0f || !UVScrollPerSec.IsZero();
	return true;
}
