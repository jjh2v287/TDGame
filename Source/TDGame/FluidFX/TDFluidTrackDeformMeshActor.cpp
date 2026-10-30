#include "FluidFX/TDFluidTrackDeformMeshActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

static TAutoConsoleVariable<int32> CVarTDFluidTrackDeformMesh(
	TEXT("TD.FluidFX.TrackDeformMesh"),
	-1,
	TEXT("Track deform mesh: -1 follows the preset, 0 hides it, 1 forces it on."),
	ECVF_Default);

ATDFluidTrackDeformMeshActor::ATDFluidTrackDeformMeshActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	DeformMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeformMesh"));
	DeformMeshComponent->SetupAttachment(RootComponent);
	DeformMeshComponent->SetMobility(EComponentMobility::Movable);
	DeformMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DeformMeshComponent->SetGenerateOverlapEvents(false);
	DeformMeshComponent->SetUsingAbsoluteLocation(true);
	DeformMeshComponent->SetUsingAbsoluteRotation(true);
	DeformMeshComponent->SetUsingAbsoluteScale(true);
	DeformMeshComponent->SetVisibility(false);
}

void ATDFluidTrackDeformMeshActor::BeginPlay()
{
	Super::BeginPlay();
	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr)
	{
		PresetTuningReloadedHandle = TuningSubsystem->OnPresetTuningReloaded.AddUObject(this, &ATDFluidTrackDeformMeshActor::HandlePresetTuningReloaded);
	}
	RequestDeformAssets();
}

void ATDFluidTrackDeformMeshActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr)
	{
		TuningSubsystem->OnPresetTuningReloaded.Remove(PresetTuningReloadedHandle);
	}
	PresetTuningReloadedHandle.Reset();
	if (DeformAssetHandle.IsValid())
	{
		DeformAssetHandle->CancelHandle();
		DeformAssetHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void ATDFluidTrackDeformMeshActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateDeformMesh();
}

bool ATDFluidTrackDeformMeshActor::IsDeformMeshShown() const
{
	return DeformMeshComponent && DeformMeshComponent->IsVisible();
}

void ATDFluidTrackDeformMeshActor::RequestDeformAssets()
{
	if (GridMesh.IsNull() || DeformMaterial.IsNull())
	{
		return;
	}

	TArray<FSoftObjectPath> AssetPaths;
	AssetPaths.Add(GridMesh.ToSoftObjectPath());
	AssetPaths.Add(DeformMaterial.ToSoftObjectPath());
	DeformAssetHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(AssetPaths, FStreamableDelegate::CreateUObject(this, &ATDFluidTrackDeformMeshActor::HandleDeformAssetsLoaded));
}

void ATDFluidTrackDeformMeshActor::HandleDeformAssetsLoaded()
{
	UStaticMesh* LoadedMesh = GridMesh.Get();
	UMaterialInterface* LoadedMaterial = DeformMaterial.Get();
	if (!LoadedMesh || !LoadedMaterial || !DeformMeshComponent)
	{
		return;
	}

	DeformMeshComponent->SetStaticMesh(LoadedMesh);
	GridVertexCount = LoadedMesh->GetNumVertices(0);
	DeformMID = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
	UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*DeformMID);
	DeformMeshComponent->SetMaterial(0, DeformMID);
	if (SourcePainterActor && SourcePainterActor->LiveComponent)
	{
		SourcePainterActor->LiveComponent->RegisterExternalMaterialTarget(DeformMID);
	}
	bAreDeformAssetsLoaded = true;
	UpdateDeformMesh();
}

AActor* ATDFluidTrackDeformMeshActor::ResolveFollowTarget() const
{
	if (FollowTargetActor)
	{
		return FollowTargetActor;
	}
	return UGameplayStatics::GetPlayerPawn(this, 0);
}

void ATDFluidTrackDeformMeshActor::HandlePresetTuningReloaded(int32)
{
	if (DeformMID)
	{
		UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*DeformMID);
	}
}

void ATDFluidTrackDeformMeshActor::UpdateDeformMesh()
{
	const UTDFluidLiveComponent* SourceComponent = SourcePainterActor ? SourcePainterActor->LiveComponent.Get() : nullptr;
	const FTDFluidSurfaceRenderSettings Settings = SourceComponent ? SourceComponent->GetSurfaceRenderSettings() : FTDFluidSurfaceRenderSettings();
	const bool bShouldShow = SourceComponent
		&& bAreDeformAssetsLoaded
		&& SourceComponent->IsSimulationAwake()
		&& ResolveDeformMeshEnabled(Settings.bUsesTrackDeformMesh, CVarTDFluidTrackDeformMesh.GetValueOnGameThread());
	DeformMeshComponent->SetVisibility(bShouldShow);
	if (!bShouldShow)
	{
		return;
	}

	if (!bHasAppliedShadowPolicy || AppliedShadowPolicy != Settings.TrackDeformMeshShadowPolicy)
	{
		ApplyShadowPolicy(*DeformMeshComponent, Settings.TrackDeformMeshShadowPolicy);
		AppliedShadowPolicy = Settings.TrackDeformMeshShadowPolicy;
		bHasAppliedShadowPolicy = true;
	}

	const AActor* FollowTarget = ResolveFollowTarget();
	if (!FollowTarget)
	{
		return;
	}

	const double ExtentCm = Settings.TrackDeformMeshExtentCm;
	const FVector2D SnappedCenterCm = SnapDeformCenterCm(FVector2D(FollowTarget->GetActorLocation()), ComputeGridSpacingCm(ExtentCm, GridVertexCount));
	if (!ShouldRecenterDeformMesh(DeformCenterCm, SnappedCenterCm, bHasPlacement, Settings.TrackDeformMeshRecenterDistanceCm))
	{
		return;
	}

	PlaceDeformMesh(SnappedCenterCm, ExtentCm, SourcePainterActor->GetActorLocation().Z);
}

void ATDFluidTrackDeformMeshActor::PlaceDeformMesh(const FVector2D& CenterCm, double ExtentCm, double FloorHeightCm)
{
	DeformCenterCm = CenterCm;
	bHasPlacement = true;
	DeformMeshComponent->SetWorldLocationAndRotation(FVector(CenterCm.X, CenterCm.Y, FloorHeightCm), FRotator::ZeroRotator);
	DeformMeshComponent->SetWorldScale3D(FVector(ExtentCm / 100.0, ExtentCm / 100.0, 1.0));
	if (DeformMID)
	{
		DeformMID->SetVectorParameterValue(TEXT("TrackDeformWindow"), FLinearColor(CenterCm.X, CenterCm.Y, ExtentCm * 0.5, 1.0f));
	}
}

double ATDFluidTrackDeformMeshActor::ComputeGridSpacingCm(double ExtentCm, int32 VertexCount)
{
	const int32 CellsPerSide = FMath::RoundToInt(FMath::Sqrt(static_cast<double>(FMath::Max(VertexCount, 0)))) - 1;
	if (CellsPerSide < 1)
	{
		return ExtentCm;
	}
	return ExtentCm / static_cast<double>(CellsPerSide);
}

FVector2D ATDFluidTrackDeformMeshActor::SnapDeformCenterCm(const FVector2D& TargetCm, double SpacingCm)
{
	if (!(SpacingCm > 0.0))
	{
		return TargetCm;
	}
	return FVector2D(FMath::RoundToDouble(TargetCm.X / SpacingCm) * SpacingCm, FMath::RoundToDouble(TargetCm.Y / SpacingCm) * SpacingCm);
}

bool ATDFluidTrackDeformMeshActor::ShouldRecenterDeformMesh(const FVector2D& CurrentCenterCm, const FVector2D& SnappedCenterCm, bool bHasCurrentPlacement, double RecenterDistanceCm)
{
	if (!bHasCurrentPlacement)
	{
		return true;
	}
	return FVector2D::Distance(CurrentCenterCm, SnappedCenterCm) >= RecenterDistanceCm;
}

bool ATDFluidTrackDeformMeshActor::ResolveDeformMeshEnabled(bool bIsEnabledByPreset, int32 ConsoleOverride)
{
	if (ConsoleOverride == 0)
	{
		return false;
	}
	if (ConsoleOverride > 0)
	{
		return true;
	}
	return bIsEnabledByPreset;
}

void ATDFluidTrackDeformMeshActor::ApplyShadowPolicy(UPrimitiveComponent& Component, ETDFluidTrackDeformShadowPolicy Policy)
{
	const bool bCastsDynamicShadow = Policy != ETDFluidTrackDeformShadowPolicy::NoShadow;
	Component.SetCastShadow(true);
	Component.bCastDynamicShadow = bCastsDynamicShadow;
	Component.bCastStaticShadow = false;
	Component.SetCastContactShadow(!bCastsDynamicShadow);
	Component.ShadowCacheInvalidationBehavior = Policy == ETDFluidTrackDeformShadowPolicy::Rigid ? EShadowCacheInvalidationBehavior::Rigid : EShadowCacheInvalidationBehavior::Auto;
	Component.MarkRenderStateDirty();
}
