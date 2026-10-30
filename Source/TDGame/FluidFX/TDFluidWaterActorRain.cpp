#include "FluidFX/TDFluidWaterActor.h"

#include "Components/DecalComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace TDFluidWaterActorRainPrivate
{
	void PlaceRainPuddleDecalIfChanged(UDecalComponent& Decal, const FVector& WorldLocation, const FVector& DecalSize)
	{
		const FQuat DownwardRotation = FRotator(-90.0, 0.0, 0.0).Quaternion();
		if (!Decal.GetComponentLocation().Equals(WorldLocation, 0.01) || !Decal.GetComponentQuat().Equals(DownwardRotation, 0.0001))
		{
			Decal.SetWorldLocationAndRotation(WorldLocation, DownwardRotation);
		}

		if (Decal.DecalSize.Equals(DecalSize, 0.01))
		{
			return;
		}

		Decal.DecalSize = DecalSize;
		Decal.UpdateBounds();
		Decal.MarkRenderStateDirty();
	}
}

bool ATDFluidWaterActor::IsRainPuddleDecalVisible() const
{
	return RainPuddleDecal && RainPuddleDecal->IsVisible() && !RainPuddleDecal->bHiddenInGame;
}

UMaterialInstanceDynamic* ATDFluidWaterActor::GetRainPuddleDecalMaterialInstance() const
{
	return RainPuddleDecalMID;
}

void ATDFluidWaterActor::ComputeWindowDecalPlacement(float TerrainMinimumCm, float TerrainMaximumCm, FVector& OutDecalLocation, FVector& OutDecalSize) const
{
	const FVector2D WindowCenter = LiveComponent->GetSimulationCenterWorld();
	const FVector2D WindowExtent = LiveComponent->GetSimulationExtentCm();
	OutDecalLocation = FVector(WindowCenter.X, WindowCenter.Y, 0.5 * (TerrainMinimumCm + TerrainMaximumCm));
	OutDecalSize = FVector(0.5 * (TerrainMaximumCm - TerrainMinimumCm) + WetDecalVerticalMarginCm, 0.5 * WindowExtent.Y, 0.5 * WindowExtent.X);
}

void ATDFluidWaterActor::RequestRainPuddleDecalMaterial()
{
	if (!bEnableRainPuddleDecal || RainPuddleDecalMaterial.IsNull() || RainPuddleDecalMID)
	{
		return;
	}

	CreateRainPuddleDecal();

	if (RainPuddleDecalLoadHandle.IsValid())
	{
		RainPuddleDecalLoadHandle->CancelHandle();
		RainPuddleDecalLoadHandle.Reset();
	}

	if (RainPuddleDecalMaterial.Get())
	{
		ApplyLoadedRainPuddleDecalMaterial();
		return;
	}

	RainPuddleDecalLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		RainPuddleDecalMaterial.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &ATDFluidWaterActor::ApplyLoadedRainPuddleDecalMaterial));
}

void ATDFluidWaterActor::CreateRainPuddleDecal()
{
	if (RainPuddleDecal)
	{
		return;
	}

	RainPuddleDecal = NewObject<UDecalComponent>(this, TEXT("RainPuddleDecal"));
	RainPuddleDecal->SetupAttachment(SceneRoot);
	RainPuddleDecal->SetAbsolute(true, true, true);
	RainPuddleDecal->SetVisibility(false);
	RainPuddleDecal->RegisterComponent();
}

void ATDFluidWaterActor::ApplyLoadedRainPuddleDecalMaterial()
{
	UMaterialInterface* LoadedMaterial = RainPuddleDecalMaterial.Get();
	if (!LoadedMaterial || !RainPuddleDecal || !LiveComponent || RainPuddleDecalMID)
	{
		return;
	}

	RainPuddleDecalMID = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
	UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*RainPuddleDecalMID);
	RainPuddleDecal->SetDecalMaterial(RainPuddleDecalMID);
	LiveComponent->RegisterExternalMaterialTarget(RainPuddleDecalMID);
}

void ATDFluidWaterActor::UpdateRainPuddleDecalPlacement()
{
	if (!RainPuddleDecal)
	{
		return;
	}

	float TerrainMinimumCm = 0.0f;
	float TerrainMaximumCm = 0.0f;
	const bool bCanShowDecal = bEnableRainPuddleDecal
		&& RainPuddleDecalMID
		&& LiveComponent
		&& LiveComponent->GetRainPuddleLevelCm() > 0.0f
		&& !IsPooledInactive()
		&& LiveComponent->IsSimulationAwake()
		&& LiveComponent->SimulationConfig.bEnableWaterLayer
		&& !bIsDormantDuplicate
		&& !bIsSleepingForMissingWater
		&& LiveComponent->TryGetSampledTerrainRangeCm(TerrainMinimumCm, TerrainMaximumCm);
	if (!bCanShowDecal)
	{
		HideRainPuddleDecal();
		return;
	}

	FVector DecalLocation = FVector::ZeroVector;
	FVector DecalSize = FVector::ZeroVector;
	ComputeWindowDecalPlacement(TerrainMinimumCm, TerrainMaximumCm, DecalLocation, DecalSize);
	TDFluidWaterActorRainPrivate::PlaceRainPuddleDecalIfChanged(*RainPuddleDecal, DecalLocation, DecalSize);
	if (!RainPuddleDecal->IsVisible())
	{
		RainPuddleDecal->SetVisibility(true);
	}
}

void ATDFluidWaterActor::ReleaseRainPuddleDecal()
{
	HideRainPuddleDecal();
	if (!RainPuddleDecalLoadHandle.IsValid())
	{
		return;
	}

	RainPuddleDecalLoadHandle->CancelHandle();
	RainPuddleDecalLoadHandle.Reset();
}

void ATDFluidWaterActor::HideRainPuddleDecal()
{
	if (RainPuddleDecal && RainPuddleDecal->IsVisible())
	{
		RainPuddleDecal->SetVisibility(false);
	}
}
