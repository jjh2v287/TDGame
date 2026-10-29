#include "FluidFX/TDFluidSurfaceOutputActor.h"

#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

static TAutoConsoleVariable<int32> CVarTDFluidSurfaceDecal(
	TEXT("TD.FluidFX.SurfaceDecal"),
	-1,
	TEXT("Surface output decal: -1 follows the preset, 0 hides it, 1 forces it on."),
	ECVF_Default);

ATDFluidSurfaceOutputActor::ATDFluidSurfaceOutputActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	OutputDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("OutputDecal"));
	OutputDecal->SetupAttachment(RootComponent);
	OutputDecal->SetUsingAbsoluteLocation(true);
	OutputDecal->SetUsingAbsoluteRotation(true);
	OutputDecal->SetUsingAbsoluteScale(true);
	OutputDecal->SetVisibility(false);

	OutputMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OutputMeshComponent"));
	OutputMeshComponent->SetupAttachment(RootComponent);
	OutputMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OutputMeshComponent->SetGenerateOverlapEvents(false);
	OutputMeshComponent->SetCastShadow(false);
	OutputMeshComponent->SetVisibility(false);
}

void ATDFluidSurfaceOutputActor::BeginPlay()
{
	Super::BeginPlay();
	RequestOutputAssets();
}

void ATDFluidSurfaceOutputActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (OutputAssetHandle.IsValid())
	{
		OutputAssetHandle->CancelHandle();
		OutputAssetHandle.Reset();
	}
	HideDecal();
	if (OutputMeshComponent)
	{
		OutputMeshComponent->SetVisibility(false);
	}
	Super::EndPlay(EndPlayReason);
}

void ATDFluidSurfaceOutputActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateDecal();
}

bool ATDFluidSurfaceOutputActor::IsDecalShown() const
{
	return OutputDecal && OutputDecal->IsVisible();
}

bool ATDFluidSurfaceOutputActor::IsMeshShown() const
{
	return OutputMeshComponent && OutputMeshComponent->IsVisible();
}

void ATDFluidSurfaceOutputActor::ComputeDecalPlacement(const FVector2D& WindowCenterCm, const FVector2D& WindowExtentCm, double FloorHeightCm, float BoxHeightCm, FVector& OutLocation, FVector& OutDecalSize)
{
	OutLocation = FVector(WindowCenterCm.X, WindowCenterCm.Y, FloorHeightCm);
	OutDecalSize = FVector(0.5 * BoxHeightCm, 0.5 * WindowExtentCm.Y, 0.5 * WindowExtentCm.X);
}

bool ATDFluidSurfaceOutputActor::ResolveSurfaceDecalEnabled(bool bIsEnabledByPreset, int32 ConsoleOverride)
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

UTDFluidLiveComponent* ATDFluidSurfaceOutputActor::ResolveSourceComponent(const ATDFluidLiveActor* Actor)
{
	return Actor ? Actor->LiveComponent.Get() : nullptr;
}

void ATDFluidSurfaceOutputActor::RequestOutputAssets()
{
	TArray<FSoftObjectPath> AssetPaths;
	if (!DecalMaterial.IsNull())
	{
		AssetPaths.Add(DecalMaterial.ToSoftObjectPath());
	}
	if (!MeshMaterial.IsNull())
	{
		AssetPaths.Add(MeshMaterial.ToSoftObjectPath());
	}
	if (!OutputMesh.IsNull())
	{
		AssetPaths.Add(OutputMesh.ToSoftObjectPath());
	}
	if (AssetPaths.IsEmpty())
	{
		return;
	}

	OutputAssetHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(AssetPaths, FStreamableDelegate::CreateUObject(this, &ATDFluidSurfaceOutputActor::HandleOutputAssetsLoaded));
}

void ATDFluidSurfaceOutputActor::HandleOutputAssetsLoaded()
{
	BindOutputAssets(DecalMaterial.Get(), MeshMaterial.Get(), OutputMesh.Get());
}

void ATDFluidSurfaceOutputActor::BindOutputAssets(UMaterialInterface* LoadedDecalMaterial, UMaterialInterface* LoadedMeshMaterial, UStaticMesh* LoadedMesh)
{
	if (LoadedDecalMaterial && OutputDecal && !DecalMID)
	{
		DecalMID = CreateRegisteredOutputMaterial(*LoadedDecalMaterial);
		OutputDecal->SetDecalMaterial(DecalMID);
		OutputDecal->SetSortOrder(DecalSortOrder);
	}

	BindOutputMesh(LoadedMeshMaterial, LoadedMesh);
	UpdateDecal();
}

void ATDFluidSurfaceOutputActor::BindOutputMesh(UMaterialInterface* LoadedMeshMaterial, UStaticMesh* LoadedMesh)
{
	if (!LoadedMeshMaterial || !LoadedMesh || !OutputMeshComponent || MeshMID)
	{
		return;
	}

	MeshMID = CreateRegisteredOutputMaterial(*LoadedMeshMaterial);
	OutputMeshComponent->SetStaticMesh(LoadedMesh);
	OutputMeshComponent->SetMaterial(0, MeshMID);
	OutputMeshComponent->SetVisibility(ResolveSourceComponent(SourceActor) || ResolveSourceComponent(TileSourceActor));
}

UMaterialInstanceDynamic* ATDFluidSurfaceOutputActor::CreateRegisteredOutputMaterial(UMaterialInterface& LoadedMaterial)
{
	UMaterialInstanceDynamic* OutputMID = UMaterialInstanceDynamic::Create(&LoadedMaterial, this);
	UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*OutputMID);

	bool bIsRegistered = false;
	if (UTDFluidLiveComponent* SourceComponent = ResolveSourceComponent(SourceActor))
	{
		SourceComponent->RegisterExternalMaterialTarget(OutputMID);
		bIsRegistered = true;
	}
	if (UTDFluidLiveComponent* TileComponent = ResolveSourceComponent(TileSourceActor))
	{
		TileComponent->RegisterTileMapTarget(OutputMID);
		bIsRegistered = true;
	}
	if (bIsRegistered)
	{
		++RegisteredOutputMaterialCount;
	}
	return OutputMID;
}

bool ATDFluidSurfaceOutputActor::ShouldShowDecal() const
{
	const UTDFluidLiveComponent* SourceComponent = ResolveSourceComponent(SourceActor);
	if (!OutputDecal || !DecalMID || !SourceComponent)
	{
		return false;
	}

	return SourceComponent->IsSimulationAwake()
		&& SourceComponent->GetPlaneBasis().IsWorldFacing()
		&& ResolveSurfaceDecalEnabled(SourceComponent->GetSurfaceRenderSettings().bUsesDecalOutput, CVarTDFluidSurfaceDecal.GetValueOnGameThread());
}

void ATDFluidSurfaceOutputActor::UpdateDecal()
{
	if (!ShouldShowDecal())
	{
		HideDecal();
		return;
	}

	const UTDFluidLiveComponent& SourceComponent = *ResolveSourceComponent(SourceActor);
	double FloorHeightCm = 0.0;
	float BoxHeightCm = 0.0f;
	ResolveDecalVerticalRange(SourceComponent, FloorHeightCm, BoxHeightCm);

	FVector DecalLocation;
	FVector DecalSize;
	ComputeDecalPlacement(SourceComponent.GetSimulationCenterWorld(), SourceComponent.GetSimulationExtentCm(), FloorHeightCm, BoxHeightCm, DecalLocation, DecalSize);
	PlaceDecalIfChanged(DecalLocation, DecalSize);

	if (OutputDecal->SortOrder != DecalSortOrder)
	{
		OutputDecal->SetSortOrder(DecalSortOrder);
	}
	if (!OutputDecal->IsVisible())
	{
		OutputDecal->SetVisibility(true);
	}
}

void ATDFluidSurfaceOutputActor::ResolveDecalVerticalRange(const UTDFluidLiveComponent& SourceComponent, double& OutFloorHeightCm, float& OutBoxHeightCm) const
{
	const float MarginCm = SourceComponent.GetSurfaceRenderSettings().DecalBoxHeightCm;
	float TerrainMinimumCm = 0.0f;
	float TerrainMaximumCm = 0.0f;
	if (SourceComponent.TryGetSampledTerrainRangeCm(TerrainMinimumCm, TerrainMaximumCm))
	{
		OutFloorHeightCm = 0.5 * (static_cast<double>(TerrainMinimumCm) + static_cast<double>(TerrainMaximumCm));
		OutBoxHeightCm = (TerrainMaximumCm - TerrainMinimumCm) + MarginCm;
		return;
	}

	OutFloorHeightCm = SourceActor->GetActorLocation().Z;
	OutBoxHeightCm = MarginCm;
}

void ATDFluidSurfaceOutputActor::PlaceDecalIfChanged(const FVector& WorldLocation, const FVector& DecalSize)
{
	const FQuat DownwardRotation = FRotator(-90.0, 0.0, 0.0).Quaternion();
	if (!OutputDecal->GetComponentLocation().Equals(WorldLocation, 0.01) || !OutputDecal->GetComponentQuat().Equals(DownwardRotation, 0.0001))
	{
		OutputDecal->SetWorldLocationAndRotation(WorldLocation, DownwardRotation);
	}

	if (OutputDecal->DecalSize.Equals(DecalSize, 0.01))
	{
		return;
	}

	OutputDecal->DecalSize = DecalSize;
	OutputDecal->UpdateBounds();
	OutputDecal->MarkRenderStateDirty();
}

void ATDFluidSurfaceOutputActor::HideDecal()
{
	if (OutputDecal && OutputDecal->IsVisible())
	{
		OutputDecal->SetVisibility(false);
	}
}
