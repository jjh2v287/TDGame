#include "FluidFX/TDFluidVolumeDriverComponent.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFluidRenderProfile.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(TDFluid);

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidVolumeDriver, Log, All);

namespace
{
	constexpr ETDFluidOutputMaterialSlot TDFluidAllOutputSlots[] = {
		ETDFluidOutputMaterialSlot::Primary,
		ETDFluidOutputMaterialSlot::Secondary,
		ETDFluidOutputMaterialSlot::Tertiary
	};

	const TArray<TSoftObjectPtr<UMaterialInterface>>& TDFluidSlotMaterials(const UTDFluidPresetAsset& Preset, ETDFluidOutputMaterialSlot Slot)
	{
		if (Slot == ETDFluidOutputMaterialSlot::Primary)
		{
			return Preset.PrimaryOutputMaterials;
		}
		if (Slot == ETDFluidOutputMaterialSlot::Secondary)
		{
			return Preset.SecondaryOutputMaterials;
		}
		return Preset.TertiaryOutputMaterials;
	}

	TSoftObjectPtr<UMaterialInterface> TDFluidResolvePresetSlotMaterial(const UTDFluidPresetAsset& Preset, ETDFluidOutputMaterialSlot Slot, const FTDFluidOutputTargetSettings& Settings)
	{
		const int32 MaterialIndex = UTDFluidVolumeDriverComponent::ResolveSlotMaterialIndex(Slot, Settings, TDFluidSlotMaterials(Preset, Slot).Num());
		return Preset.ResolveSlotMaterial(Slot, MaterialIndex);
	}

	void TDFluidDestroyOwnedComponent(UActorComponent* Component)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
}

UTDFluidVolumeDriverComponent::UTDFluidVolumeDriverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FVector UTDFluidVolumeDriverComponent::ComputeWorldOffsetCm(const FTDFluidPlaneBasis& Basis, const FVector& OffsetCm)
{
	return Basis.U * OffsetCm.X + Basis.V * OffsetCm.Y + Basis.N * OffsetCm.Z;
}

FTDFluidVolumePlacement UTDFluidVolumeDriverComponent::ComputeHeterogeneousVolumePlacement(const FTDFluidPlaneBasis& Basis, const FVector& PlaneCenterWorld, const FVector2D& ExtentCm, const FTDFluidVolumeDriverSettings& Settings)
{
	const double HeightCm = Settings.HeterogeneousVolumeHeightCm;
	const double SizeMultiplier = Settings.HeterogeneousVolumeSizeMultiplier;
	const int32 ResolutionXY = FMath::Max(Settings.HeterogeneousVolumeResolutionXY, 1);
	const int32 ResolutionZ = FMath::Max(Settings.HeterogeneousVolumeResolutionZ, 1);
	const FVector SizeCm(ExtentCm.X * SizeMultiplier, ExtentCm.Y * SizeMultiplier, HeightCm);
	FVector CenterWorld = PlaneCenterWorld + ComputeWorldOffsetCm(Basis, Settings.HeterogeneousVolumeOffsetCm);
	if (Basis.IsWorldFacing())
	{
		CenterWorld += Basis.N * (HeightCm * 0.5);
	}

	FTDFluidVolumePlacement Placement;
	Placement.Resolution = FIntVector(ResolutionXY, ResolutionXY, ResolutionZ);
	Placement.Transform = FTransform(Basis.GetRotation(), CenterWorld, SizeCm / FVector(Placement.Resolution));
	return Placement;
}

FTransform UTDFluidVolumeDriverComponent::ComputeFogVolumeTransform(const FBoxSphereBounds& MeshBounds, const FTDFluidPlaneBasis& Basis, const FVector& PlaneCenterWorld, const FVector2D& ExtentCm, const FTDFluidVolumeDriverSettings& Settings)
{
	const FVector MeshSize = MeshBounds.BoxExtent * 2.0;
	if (MeshSize.X < UE_KINDA_SMALL_NUMBER || MeshSize.Y < UE_KINDA_SMALL_NUMBER || MeshSize.Z < UE_KINDA_SMALL_NUMBER)
	{
		return FTransform::Identity;
	}

	const double HeightCm = Settings.FogVolumeHeightCm;
	const double SizeMultiplier = Settings.FogVolumeSizeMultiplier;
	const FVector Scale = FVector(ExtentCm.X * SizeMultiplier, ExtentCm.Y * SizeMultiplier, HeightCm) / MeshSize;
	const FQuat Rotation = Basis.GetRotation();
	FVector CenterWorld = PlaneCenterWorld;
	if (Basis.IsWorldFacing())
	{
		CenterWorld += Basis.N * (HeightCm * 0.5);
	}
	return FTransform(Rotation, CenterWorld - Rotation.RotateVector(Scale * MeshBounds.Origin), Scale);
}

int32 UTDFluidVolumeDriverComponent::ResolveSlotMaterialIndex(ETDFluidOutputMaterialSlot Slot, const FTDFluidOutputTargetSettings& Settings, int32 MaterialCount)
{
	if (MaterialCount <= 0)
	{
		return 0;
	}
	const int32 RequestedIndex = Slot == ETDFluidOutputMaterialSlot::Primary ? Settings.PrimaryOutputMaterialIndex
		: Slot == ETDFluidOutputMaterialSlot::Secondary ? Settings.SecondaryOutputMaterialIndex
		: Settings.TertiaryOutputMaterialIndex;
	return FMath::Clamp(RequestedIndex, 0, MaterialCount - 1);
}

bool UTDFluidVolumeDriverComponent::ShouldRecollectTargets(double NowSeconds, double LastSeconds, const FTDFluidOutputTargetSettings& Settings, bool bForced)
{
	if (bForced)
	{
		return true;
	}
	if (!Settings.bRecollectTargetsDuringGame)
	{
		return false;
	}
	return NowSeconds - LastSeconds >= Settings.RecollectIntervalSeconds;
}

bool UTDFluidVolumeDriverComponent::ShouldRecaptureHeight(const FVector2D& Center, const FVector2D& LastCenter, float CellCm, int32 Cells, bool bForced)
{
	if (bForced)
	{
		return true;
	}
	const double ThresholdCm = static_cast<double>(FMath::Max(Cells, 1)) * FMath::Max(CellCm, UE_KINDA_SMALL_NUMBER);
	return FVector2D::Distance(Center, LastCenter) >= ThresholdCm;
}

bool UTDFluidVolumeDriverComponent::IsDriverNeeded(const FTDFluidVolumeDriverSettings& Driver, const FTDFluidTerrainHeightSettings& Terrain, const UTDFluidPresetAsset* Preset)
{
	if (Driver.bRenderHeterogeneousVolume || Driver.bRenderFogVolume)
	{
		return true;
	}
	if (Terrain.VolumeHeightSource != ETDFluidVolumeHeightSource::None)
	{
		return true;
	}
	if (!Preset)
	{
		return false;
	}
	return !Preset->PrimaryOutputActorTag.IsNone() || !Preset->SecondaryOutputActorTag.IsNone() || !Preset->TertiaryOutputActorTag.IsNone();
}

bool UTDFluidVolumeDriverComponent::IsVolumeDomainMaterial(const UMaterialInterface* Material)
{
	const UMaterial* BaseMaterial = Material ? Material->GetMaterial() : nullptr;
	return BaseMaterial && BaseMaterial->MaterialDomain == MD_Volume;
}

TArray<ETDFluidOutputMaterialSlot> UTDFluidVolumeDriverComponent::GatherUsedSlots() const
{
	TArray<ETDFluidOutputMaterialSlot> Slots;
	if (DriverSettings.bRenderHeterogeneousVolume)
	{
		Slots.AddUnique(DriverSettings.HeterogeneousVolumeMaterialSlot);
	}
	if (DriverSettings.bRenderFogVolume)
	{
		Slots.AddUnique(DriverSettings.FogVolumeMaterialSlot);
	}
	if (!ConfiguredPreset)
	{
		return Slots;
	}
	for (const ETDFluidOutputMaterialSlot Slot : TDFluidAllOutputSlots)
	{
		if (!ConfiguredPreset->GetSlotActorTag(Slot).IsNone())
		{
			Slots.AddUnique(Slot);
		}
	}
	return Slots;
}

void UTDFluidVolumeDriverComponent::ConfigureFromLiveComponent(UTDFluidLiveComponent& LiveComponent)
{
	ConfiguredPreset = LiveComponent.ActivePreset;
	const ETDFluidPresetKind Kind = ConfiguredPreset ? ConfiguredPreset->PresetKind : ETDFluidPresetKind::Custom;
	DriverSettings = UTDFluidPresetAsset::BuildVolumeDriverForPresetKind(Kind);
	OutputTargetSettings = UTDFluidPresetAsset::BuildOutputTargetForPresetKind(Kind);
	TerrainHeightSettings = UTDFluidPresetAsset::BuildTerrainHeightForPresetKind(Kind);
	bShouldForceRecollect = true;
	bShouldForceRecapture = true;
	bHasReleasedResources = false;
	if (!bHasAcquiredRenderProfile)
	{
		FTDFluidRenderProfile::AcquireDriver();
		bHasAcquiredRenderProfile = true;
	}

	if (SlotMaterialLoadHandle.IsValid())
	{
		SlotMaterialLoadHandle->CancelHandle();
		SlotMaterialLoadHandle.Reset();
	}

	TArray<FSoftObjectPath> AssetsToLoad;
	if (ConfiguredPreset)
	{
		for (const ETDFluidOutputMaterialSlot Slot : GatherUsedSlots())
		{
			const TSoftObjectPtr<UMaterialInterface> SlotMaterial = TDFluidResolvePresetSlotMaterial(*ConfiguredPreset, Slot, OutputTargetSettings);
			if (!SlotMaterial.IsNull() && !SlotMaterial.Get())
			{
				AssetsToLoad.AddUnique(SlotMaterial.ToSoftObjectPath());
			}
		}
		const bool bNeedsFogMesh = DriverSettings.bRenderFogVolume && !ConfiguredPreset->VolumeMesh.IsNull() && !ConfiguredPreset->VolumeMesh.Get();
		if (bNeedsFogMesh)
		{
			AssetsToLoad.AddUnique(ConfiguredPreset->VolumeMesh.ToSoftObjectPath());
		}
	}

	if (AssetsToLoad.IsEmpty())
	{
		ApplyLoadedSlotMaterials();
		return;
	}
	SlotMaterialLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &UTDFluidVolumeDriverComponent::HandleSlotMaterialsLoaded));
}

void UTDFluidVolumeDriverComponent::HandleSlotMaterialsLoaded()
{
	SlotMaterialLoadHandle.Reset();
	ApplyLoadedSlotMaterials();
}

UMaterialInstanceDynamic* UTDFluidVolumeDriverComponent::FindSlotMID(ETDFluidOutputMaterialSlot Slot) const
{
	if (Slot == ETDFluidOutputMaterialSlot::Primary)
	{
		return PrimarySlotMID;
	}
	if (Slot == ETDFluidOutputMaterialSlot::Secondary)
	{
		return SecondarySlotMID;
	}
	return TertiarySlotMID;
}

UMaterialInstanceDynamic* UTDFluidVolumeDriverComponent::GetOutputMaterialInstance(ETDFluidOutputMaterialSlot Slot) const
{
	return FindSlotMID(Slot);
}

void UTDFluidVolumeDriverComponent::ApplyLoadedSlotMaterials()
{
	EnsureInternalComponents();
	if (!ConfiguredPreset)
	{
		return;
	}

	const TArray<ETDFluidOutputMaterialSlot> UsedSlots = GatherUsedSlots();
	for (const ETDFluidOutputMaterialSlot Slot : TDFluidAllOutputSlots)
	{
		TObjectPtr<UMaterialInstanceDynamic>& SlotMID = Slot == ETDFluidOutputMaterialSlot::Primary ? PrimarySlotMID
			: Slot == ETDFluidOutputMaterialSlot::Secondary ? SecondarySlotMID
			: TertiarySlotMID;
		if (!UsedSlots.Contains(Slot))
		{
			SlotMID = nullptr;
			continue;
		}
		UMaterialInterface* LoadedMaterial = TDFluidResolvePresetSlotMaterial(*ConfiguredPreset, Slot, OutputTargetSettings).Get();
		if (!LoadedMaterial)
		{
			continue;
		}
		if (!SlotMID || SlotMID->Parent != LoadedMaterial)
		{
			SlotMID = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
			UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*SlotMID);
		}
		ApplySlotMaterialToTargets(Slot);
	}

	UMaterialInstanceDynamic* VolumeMID = FindSlotMID(DriverSettings.HeterogeneousVolumeMaterialSlot);
	const bool bHasVolumeMID = IsVolumeDomainMaterial(VolumeMID);
	if (VolumeMID && !bHasVolumeMID && !bHasWarnedNonVolumeMaterial)
	{
		bHasWarnedNonVolumeMaterial = true;
		UE_LOG(LogTDFluidVolumeDriver, Warning, TEXT("%s: slot material %s is not a Volume domain material; engine volumes stay empty"), *GetPathNameSafe(GetOwner()), *GetPathNameSafe(VolumeMID->Parent));
	}
	if (InternalHeterogeneousVolume)
	{
		InternalHeterogeneousVolume->SetMaterial(0, bHasVolumeMID ? VolumeMID : nullptr);
	}
	if (InternalFogVolume)
	{
		UMaterialInstanceDynamic* FogMID = FindSlotMID(DriverSettings.FogVolumeMaterialSlot);
		InternalFogVolume->SetStaticMesh(ConfiguredPreset->VolumeMesh.Get());
		InternalFogVolume->SetMaterial(0, IsVolumeDomainMaterial(FogMID) ? FogMID : nullptr);
	}
}

void UTDFluidVolumeDriverComponent::EnsureInternalComponents()
{
	AActor* Owner = GetOwner();
	if (!Owner || !FApp::CanEverRender())
	{
		return;
	}

	if (!DriverSettings.bRenderHeterogeneousVolume && InternalHeterogeneousVolume)
	{
		TDFluidDestroyOwnedComponent(InternalHeterogeneousVolume);
		InternalHeterogeneousVolume = nullptr;
	}
	if (DriverSettings.bRenderHeterogeneousVolume && !InternalHeterogeneousVolume)
	{
		UHeterogeneousVolumeComponent* Volume = NewObject<UHeterogeneousVolumeComponent>(Owner, NAME_None, RF_Transient);
		Volume->SetMobility(EComponentMobility::Movable);
		Volume->SetUsingAbsoluteLocation(true);
		Volume->SetUsingAbsoluteRotation(true);
		Volume->SetUsingAbsoluteScale(true);
		Volume->bPivotAtCentroid = true;
		Volume->VolumeResolution = FIntVector(DriverSettings.HeterogeneousVolumeResolutionXY, DriverSettings.HeterogeneousVolumeResolutionXY, DriverSettings.HeterogeneousVolumeResolutionZ);
		Volume->SetVisibility(false);
		Volume->SetupAttachment(Owner->GetRootComponent());
		Volume->RegisterComponent();
		InternalHeterogeneousVolume = Volume;
	}

	if (!DriverSettings.bRenderFogVolume && InternalFogVolume)
	{
		TDFluidDestroyOwnedComponent(InternalFogVolume);
		InternalFogVolume = nullptr;
	}
	if (DriverSettings.bRenderFogVolume && !InternalFogVolume)
	{
		UStaticMeshComponent* FogVolume = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		FogVolume->SetMobility(EComponentMobility::Movable);
		FogVolume->SetUsingAbsoluteLocation(true);
		FogVolume->SetUsingAbsoluteRotation(true);
		FogVolume->SetUsingAbsoluteScale(true);
		FogVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FogVolume->SetCastShadow(false);
		FogVolume->SetVisibleInRayTracing(false);
		FogVolume->SetAffectDistanceFieldLighting(false);
		FogVolume->SetVisibility(false);
		FogVolume->SetupAttachment(Owner->GetRootComponent());
		FogVolume->RegisterComponent();
		InternalFogVolume = FogVolume;
	}

	const bool bNeedsCapture = TerrainHeightSettings.VolumeHeightSource == ETDFluidVolumeHeightSource::SceneCapture;
	if (!bNeedsCapture && HeightCapture)
	{
		TDFluidDestroyOwnedComponent(HeightCapture);
		HeightCapture = nullptr;
		HeightCaptureTarget = nullptr;
	}
	if (bNeedsCapture && !HeightCapture)
	{
		const int32 CaptureResolution = FMath::Max(TerrainHeightSettings.SceneCaptureResolution, 1);
		HeightCaptureTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		HeightCaptureTarget->RenderTargetFormat = RTF_R32f;
		HeightCaptureTarget->ClearColor = FLinearColor(1.0e9f, 0.0f, 0.0f, 0.0f);
		HeightCaptureTarget->InitAutoFormat(CaptureResolution, CaptureResolution);
		HeightCaptureTarget->UpdateResourceImmediate(true);

		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Owner, NAME_None, RF_Transient);
		Capture->SetUsingAbsoluteLocation(true);
		Capture->SetUsingAbsoluteRotation(true);
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->TextureTarget = HeightCaptureTarget;
		Capture->SetupAttachment(Owner->GetRootComponent());
		Capture->RegisterComponent();
		HeightCapture = Capture;
		bShouldForceRecapture = true;
	}
}

void UTDFluidVolumeDriverComponent::ApplySlotMaterialToTarget(FTDFluidDrivenTarget& Target, UMaterialInstanceDynamic* SlotMID) const
{
	UMeshComponent* Component = Target.Component.Get();
	if (!Component || !SlotMID)
	{
		return;
	}
	if (Component->IsA<UHeterogeneousVolumeComponent>() && !IsVolumeDomainMaterial(SlotMID))
	{
		return;
	}
	const int32 MaterialCount = FMath::Max(Component->GetNumMaterials(), 1);
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		Component->SetMaterial(MaterialIndex, SlotMID);
	}
}

void UTDFluidVolumeDriverComponent::ApplySlotMaterialToTargets(ETDFluidOutputMaterialSlot Slot)
{
	UMaterialInstanceDynamic* SlotMID = FindSlotMID(Slot);
	for (FTDFluidDrivenTarget& Target : DrivenTargets)
	{
		if (Target.Slot == Slot)
		{
			ApplySlotMaterialToTarget(Target, SlotMID);
		}
	}
}

int32 UTDFluidVolumeDriverComponent::AddTaggedComponents(AActor& TaggedActor, FName Tag, ETDFluidOutputMaterialSlot Slot)
{
	TInlineComponentArray<UMeshComponent*> MeshComponents(&TaggedActor);
	const bool bHasTaggedComponent = MeshComponents.ContainsByPredicate([Tag](const UMeshComponent* Component) { return Component->ComponentHasTag(Tag); });
	int32 AddedCount = 0;
	for (UMeshComponent* Component : MeshComponents)
	{
		if (bHasTaggedComponent && !Component->ComponentHasTag(Tag))
		{
			continue;
		}
		const bool bIsHeterogeneousVolume = Component->IsA<UHeterogeneousVolumeComponent>();
		const bool bShouldDrive = bIsHeterogeneousVolume ? OutputTargetSettings.bDriveTaggedHeterogeneousVolumes : OutputTargetSettings.bDriveTaggedMeshes;
		if (!bShouldDrive)
		{
			continue;
		}
		const bool bIsTracked = DrivenTargets.ContainsByPredicate([Component](const FTDFluidDrivenTarget& Target) { return Target.Component.Get() == Component; });
		if (bIsTracked)
		{
			continue;
		}
		FTDFluidDrivenTarget& Target = DrivenTargets.AddDefaulted_GetRef();
		Target.Component = Component;
		Target.Slot = Slot;
		const int32 MaterialCount = FMath::Max(Component->GetNumMaterials(), 1);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			Target.OriginalMaterials.Add(Component->GetMaterial(MaterialIndex));
		}
		ApplySlotMaterialToTarget(Target, FindSlotMID(Slot));
		++AddedCount;
	}
	return AddedCount;
}

int32 UTDFluidVolumeDriverComponent::CollectTaggedTargets(double NowSeconds, bool bForce)
{
	UWorld* World = GetWorld();
	if (!World || !ConfiguredPreset)
	{
		return 0;
	}
	if (!ShouldRecollectTargets(NowSeconds, LastCollectSeconds, OutputTargetSettings, bForce || bShouldForceRecollect))
	{
		return 0;
	}
	LastCollectSeconds = NowSeconds;
	bShouldForceRecollect = false;
	++TargetCollectionCount;
	DrivenTargets.RemoveAll([](const FTDFluidDrivenTarget& Target) { return !Target.Component.IsValid(); });

	int32 AddedCount = 0;
	for (const ETDFluidOutputMaterialSlot Slot : TDFluidAllOutputSlots)
	{
		const FName Tag = ConfiguredPreset->GetSlotActorTag(Slot);
		if (Tag.IsNone())
		{
			continue;
		}
		TArray<AActor*> TaggedActors;
		UGameplayStatics::GetAllActorsWithTag(World, Tag, TaggedActors);
		for (AActor* TaggedActor : TaggedActors)
		{
			if (TaggedActor && TaggedActor != GetOwner())
			{
				AddedCount += AddTaggedComponents(*TaggedActor, Tag, Slot);
			}
		}
	}
	return AddedCount;
}

void UTDFluidVolumeDriverComponent::RestoreTargetMaterials()
{
	for (const FTDFluidDrivenTarget& Target : DrivenTargets)
	{
		UMeshComponent* Component = Target.Component.Get();
		if (!Component)
		{
			continue;
		}
		for (int32 MaterialIndex = 0; MaterialIndex < Target.OriginalMaterials.Num(); ++MaterialIndex)
		{
			Component->SetMaterial(MaterialIndex, Target.OriginalMaterials[MaterialIndex].Get());
		}
	}
	DrivenTargets.Reset();
}

void UTDFluidVolumeDriverComponent::ApplyDriverParametersToMID(const UTDFluidLiveComponent& LiveComponent, UMaterialInstanceDynamic& SlotMID) const
{
	const AActor* Owner = GetOwner();
	const double OwnerZ = Owner ? Owner->GetActorLocation().Z : 0.0;
	const FVector WorldOffsetCm = ComputeWorldOffsetCm(LiveComponent.GetPlaneBasis(), DriverSettings.HeterogeneousVolumeOffsetCm);
	LiveComponent.ApplyParametersToMID(&SlotMID, static_cast<float>(OwnerZ + WorldOffsetCm.Z));
	SlotMID.SetScalarParameterValue(TEXT("VolumeHeightCm"), DriverSettings.HeterogeneousVolumeHeightCm);
	SlotMID.SetVectorParameterValue(TEXT("VolumeOffsetCm"), FLinearColor(WorldOffsetCm.X, WorldOffsetCm.Y, WorldOffsetCm.Z, 0.0f));
	if (TerrainHeightSettings.VolumeHeightSource == ETDFluidVolumeHeightSource::None)
	{
		return;
	}
	const bool bHasHeightTexture = TerrainHeightTexture != nullptr;
	if (bHasHeightTexture)
	{
		SlotMID.SetTextureParameterValue(TEXT("TerrainHeightTexture"), TerrainHeightTexture);
	}
	SlotMID.SetVectorParameterValue(TEXT("TerrainHeightWindow"), FLinearColor(TerrainHeightWindow.X, TerrainHeightWindow.Y, TerrainHeightWindow.Z, TerrainHeightWindow.W));
	SlotMID.SetVectorParameterValue(TEXT("TerrainHeightDecode"), FLinearColor(TerrainHeightDecode.X, TerrainHeightDecode.Y, bHasHeightTexture ? TerrainHeightDecode.Z : 0.0f, 0.0f));
}

void UTDFluidVolumeDriverComponent::UpdateInternalHeterogeneousVolume(const UTDFluidLiveComponent& LiveComponent, const FVector& PlaneCenterWorld, bool bIsVisible)
{
	UHeterogeneousVolumeComponent* Volume = InternalHeterogeneousVolume;
	if (!Volume)
	{
		return;
	}
	const FTDFluidVolumePlacement Placement = ComputeHeterogeneousVolumePlacement(LiveComponent.GetPlaneBasis(), PlaneCenterWorld, LiveComponent.GetSimulationExtentCm(), DriverSettings);
	if (Volume->VolumeResolution != Placement.Resolution)
	{
		Volume->SetVolumeResolution(Placement.Resolution);
	}

	bool bIsRenderStateDirty = false;
	const auto AssignFactor = [&bIsRenderStateDirty](float& Field, float Value)
	{
		if (!FMath::IsNearlyEqual(Field, Value, 1.0e-4f))
		{
			Field = Value;
			bIsRenderStateDirty = true;
		}
	};
	AssignFactor(Volume->StepFactor, DriverSettings.HeterogeneousVolumeStepFactor);
	AssignFactor(Volume->ShadowStepFactor, DriverSettings.HeterogeneousVolumeShadowStepFactor);
	AssignFactor(Volume->ShadowBiasFactor, DriverSettings.HeterogeneousVolumeShadowBiasFactor);
	AssignFactor(Volume->LightingDownsampleFactor, DriverSettings.HeterogeneousVolumeLightingDownsampleFactor);

	const FTransform CurrentTransform = Volume->GetComponentTransform();
	if (!CurrentTransform.GetScale3D().Equals(Placement.Transform.GetScale3D(), 1.0e-4))
	{
		bIsRenderStateDirty = true;
	}
	if (!CurrentTransform.Equals(Placement.Transform, 1.0e-4))
	{
		Volume->SetWorldTransform(Placement.Transform);
	}
	if (Volume->CastShadow != DriverSettings.bHeterogeneousVolumeCastShadow)
	{
		Volume->SetCastShadow(DriverSettings.bHeterogeneousVolumeCastShadow);
	}
	if (!FMath::IsNearlyEqual(Volume->LDMaxDrawDistance, DriverSettings.HeterogeneousVolumeMaxDrawDistanceCm))
	{
		Volume->SetCullDistance(DriverSettings.HeterogeneousVolumeMaxDrawDistanceCm);
	}
	if (Volume->IsVisible() != bIsVisible)
	{
		Volume->SetVisibility(bIsVisible);
	}
	if (bIsRenderStateDirty)
	{
		Volume->MarkRenderStateDirty();
	}
}

void UTDFluidVolumeDriverComponent::UpdateInternalFogVolume(const UTDFluidLiveComponent& LiveComponent, const FVector& PlaneCenterWorld, bool bIsVisible)
{
	UStaticMeshComponent* FogVolume = InternalFogVolume;
	const UStaticMesh* FogMesh = FogVolume ? FogVolume->GetStaticMesh() : nullptr;
	if (!FogMesh)
	{
		return;
	}
	const FTransform DesiredTransform = ComputeFogVolumeTransform(FogMesh->GetBounds(), LiveComponent.GetPlaneBasis(), PlaneCenterWorld, LiveComponent.GetSimulationExtentCm(), DriverSettings);
	if (!FogVolume->GetComponentTransform().Equals(DesiredTransform, 1.0e-4))
	{
		FogVolume->SetWorldTransform(DesiredTransform);
	}
	const bool bHasMaterial = FogVolume->GetMaterial(0) != nullptr;
	const bool bShouldShow = bIsVisible && bHasMaterial;
	if (FogVolume->IsVisible() != bShouldShow)
	{
		FogVolume->SetVisibility(bShouldShow);
	}
}

void UTDFluidVolumeDriverComponent::UpdateSceneCapture(const UTDFluidLiveComponent& LiveComponent)
{
	if (!HeightCapture || !HeightCaptureTarget || !LiveComponent.IsSimulationInitialized())
	{
		return;
	}
	const FVector2D CenterCm = LiveComponent.GetSimulationCenterWorld();
	const FVector2D ExtentCm = LiveComponent.GetSimulationExtentCm();
	const float CellCm = static_cast<float>(ExtentCm.X / FMath::Max(LiveComponent.GetSimulationResolution(), 1));
	const int32 CaptureResolution = FMath::Max(TerrainHeightSettings.SceneCaptureResolution, 1);
	const bool bHasResolutionChanged = HeightCaptureTarget->SizeX != CaptureResolution || HeightCaptureTarget->SizeY != CaptureResolution;
	const bool bIsForced = bShouldForceRecapture || bHasResolutionChanged;
	if (!ShouldRecaptureHeight(CenterCm, LastCaptureCenter, CellCm, TerrainHeightSettings.SceneCaptureRecaptureCells, bIsForced))
	{
		return;
	}
	if (bHasResolutionChanged)
	{
		HeightCaptureTarget->ResizeTarget(CaptureResolution, CaptureResolution);
	}

	const AActor* Owner = GetOwner();
	const double OwnerZ = Owner ? Owner->GetActorLocation().Z : 0.0;
	const double CaptureZ = OwnerZ + TerrainHeightSettings.SceneCaptureHeightAboveCm;
	const double WidthCm = FMath::Max(ExtentCm.X, ExtentCm.Y) * (1.0 + 2.0 * TerrainHeightSettings.SceneCaptureMarginRatio);
	HeightCapture->OrthoWidth = static_cast<float>(WidthCm);
	HeightCapture->SetWorldLocationAndRotation(FVector(CenterCm.X, CenterCm.Y, CaptureZ), FRotator(-90.0, -90.0, 0.0));
	HeightCapture->HiddenActors.Reset();
	if (Owner)
	{
		HeightCapture->HiddenActors.Add(const_cast<AActor*>(Owner));
	}
	if (TerrainHeightSettings.bSceneCaptureIgnoresPawns)
	{
		for (TActorIterator<APawn> PawnIt(GetWorld()); PawnIt; ++PawnIt)
		{
			HeightCapture->HiddenActors.Add(*PawnIt);
		}
	}
	HeightCapture->CaptureScene();

	TerrainHeightTexture = HeightCaptureTarget;
	TerrainHeightWindow = FVector4f(static_cast<float>(CenterCm.X), static_cast<float>(CenterCm.Y), static_cast<float>(WidthCm), static_cast<float>(WidthCm));
	TerrainHeightDecode = FVector4f(-1.0f, static_cast<float>(CaptureZ), 1.0f, 0.0f);
	LastCaptureCenter = CenterCm;
	bShouldForceRecapture = false;
	++HeightCaptureCount;
}

void UTDFluidVolumeDriverComponent::DriveTargets(const UTDFluidLiveComponent& LiveComponent, float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT(TDFluid, VolumeDriver);
	FTDFluidRenderProfile::ApplyIfTableChanged();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	CollectTaggedTargets(World->GetTimeSeconds(), false);
	UpdateSceneCapture(LiveComponent);

	for (const ETDFluidOutputMaterialSlot Slot : TDFluidAllOutputSlots)
	{
		if (UMaterialInstanceDynamic* SlotMID = FindSlotMID(Slot))
		{
			ApplyDriverParametersToMID(LiveComponent, *SlotMID);
		}
	}

	const AActor* Owner = GetOwner();
	const double OwnerZ = Owner ? Owner->GetActorLocation().Z : 0.0;
	const FVector2D CenterCm = LiveComponent.GetSimulationCenterWorld();
	const FVector PlaneCenterWorld = LiveComponent.GetPlaneBasis().IsWorldFacing()
		? FVector(CenterCm.X, CenterCm.Y, OwnerZ)
		: LiveComponent.UnprojectSolverPlaneToWorld(CenterCm);
	const bool bIsVisible = !bIsDriverHidden && LiveComponent.IsSimulationInitialized();
	UpdateInternalHeterogeneousVolume(LiveComponent, PlaneCenterWorld, bIsVisible);
	UpdateInternalFogVolume(LiveComponent, PlaneCenterWorld, bIsVisible);
}

void UTDFluidVolumeDriverComponent::ReapplyMaterialLooks(FTDFluidPresetReapplyReport& InOutReport)
{
	for (const ETDFluidOutputMaterialSlot Slot : TDFluidAllOutputSlots)
	{
		if (UMaterialInstanceDynamic* SlotMID = FindSlotMID(Slot))
		{
			InOutReport.ApplyLiveMaterialLook(SlotMID);
		}
	}
}

void UTDFluidVolumeDriverComponent::SetDriverHidden(bool bInIsHidden)
{
	bIsDriverHidden = bInIsHidden;
	if (!bIsDriverHidden)
	{
		return;
	}
	if (InternalHeterogeneousVolume)
	{
		InternalHeterogeneousVolume->SetVisibility(false);
	}
	if (InternalFogVolume)
	{
		InternalFogVolume->SetVisibility(false);
	}
}

void UTDFluidVolumeDriverComponent::SetTerrainHeightTexture(UTexture* InTexture, const FVector4f& InWindow, const FVector4f& InDecode)
{
	TerrainHeightTexture = InTexture;
	TerrainHeightWindow = InWindow;
	TerrainHeightDecode = InDecode;
}

void UTDFluidVolumeDriverComponent::RequestHeightRecapture()
{
	bShouldForceRecapture = true;
}

void UTDFluidVolumeDriverComponent::ReleaseDriverResources()
{
	if (bHasReleasedResources)
	{
		return;
	}
	bHasReleasedResources = true;
	if (SlotMaterialLoadHandle.IsValid())
	{
		SlotMaterialLoadHandle->CancelHandle();
		SlotMaterialLoadHandle.Reset();
	}
	if (OutputTargetSettings.bRestoreTargetMaterialsOnEnd)
	{
		RestoreTargetMaterials();
	}
	DrivenTargets.Reset();
	TDFluidDestroyOwnedComponent(InternalHeterogeneousVolume);
	TDFluidDestroyOwnedComponent(InternalFogVolume);
	TDFluidDestroyOwnedComponent(HeightCapture);
	InternalHeterogeneousVolume = nullptr;
	InternalFogVolume = nullptr;
	HeightCapture = nullptr;
	HeightCaptureTarget = nullptr;
	if (bHasAcquiredRenderProfile)
	{
		bHasAcquiredRenderProfile = false;
		FTDFluidRenderProfile::ReleaseDriver();
	}
}

void UTDFluidVolumeDriverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseDriverResources();
	Super::EndPlay(EndPlayReason);
}

void UTDFluidVolumeDriverComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ReleaseDriverResources();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
