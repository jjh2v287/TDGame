#include "FluidFX/TDFluidLiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "TextureResource.h"

namespace
{
	constexpr double NiagaraCacheRefreshIntervalSeconds = 1.0;

	void EnsureOutputTexture(TObjectPtr<UTexture2D>& InOutTexture, int32 Resolution)
	{
		if (InOutTexture && InOutTexture->GetSizeX() == Resolution && InOutTexture->GetSizeY() == Resolution)
		{
			return;
		}

		UTexture2D* NewTexture = UTexture2D::CreateTransient(Resolution, Resolution, PF_FloatRGBA);
		InOutTexture = NewTexture;
		if (!NewTexture)
		{
			return;
		}

		NewTexture->SRGB = false;
		NewTexture->CompressionSettings = TC_HDR;
		NewTexture->Filter = TF_Bilinear;
		NewTexture->AddressX = TA_Clamp;
		NewTexture->AddressY = TA_Clamp;
		NewTexture->NeverStream = true;
		NewTexture->UpdateResource();
	}
}

UTDFluidLiveComponent::UTDFluidLiveComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTDFluidLiveComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bShouldInitSystemStateFromPreset && ActivePreset)
	{
		InitSystemStateFromPreset(ActivePreset);
		return;
	}

	Solver.Initialize(SimulationConfig, ResolveOwnerCenterWorld());
	ReapplyActiveObstacles();
	RequestAsyncAssetLoads();
}

void UTDFluidLiveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AssetLoadHandle.IsValid())
	{
		AssetLoadHandle->CancelHandle();
		AssetLoadHandle.Reset();
	}

	PendingBrushStamps.Reset();
	ExternalMaterialTargets.Reset();
	DestroyOwnedNiagaraComponent();
	Super::EndPlay(EndPlayReason);
}

void UTDFluidLiveComponent::InitSystemStateFromPreset(UTDFluidPresetAsset* InPreset)
{
	if (!InPreset)
	{
		ReinitializeSolverAtOwnerLocation();
		return;
	}

	ActivePreset = InPreset;
	SimulationConfig = InPreset->ResolveEffectiveConfig();
	OutputMaterial = InPreset->OutputMaterial;
	ParameterCollection = InPreset->ParameterCollection;
	ExternalNiagaraSystem = InPreset->ExternalNiagaraSystem;

	if (!InPreset->ExternalRendererActorTag.IsNone())
	{
		DriveNiagaraComponentInTaggedActors = InPreset->ExternalRendererActorTag;
	}

	ReinitializeSolverAtOwnerLocation();
	RequestAsyncAssetLoads();
	ApplyLoadedOutputsToBoundMesh();
	ApplyLoadedOutputsToVolumeMesh();
}

void UTDFluidLiveComponent::ApplySimulationConfig(const FTDFluidSimulationConfig& InConfig)
{
	SimulationConfig = InConfig;
	if (Solver.IsInitialized())
	{
		Solver.UpdateConfig(SimulationConfig);
	}
	else
	{
		Solver.Initialize(SimulationConfig, ResolveOwnerCenterWorld());
	}

	ReapplyActiveObstacles();
}

void UTDFluidLiveComponent::ResetSimulationState()
{
	PendingBrushStamps.Reset();
	LastProcessedPointCount = 0;
	Solver.ResetSimulation();
	ReapplyActiveObstacles();
}

void UTDFluidLiveComponent::QueueBrushStamp(const FTDFluidBrushStamp& Stamp)
{
	if (!bIsSimulationAwake)
	{
		return;
	}

	PendingBrushStamps.Add(Stamp);
}

void UTDFluidLiveComponent::QueueNiagaraParticleStamps(
	const TArray<FVector>& ParticlePositions,
	const TArray<FVector>& ParticleVelocities,
	float BrushRadiusCm,
	ETDFluidBrushChannel Channel)
{
	if (!bIsSimulationAwake || ParticlePositions.IsEmpty())
	{
		return;
	}

	const int32 Count = ParticlePositions.Num();
	for (int32 Idx = 0; Idx < Count; ++Idx)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = ParticlePositions[Idx];
		Stamp.WorldVelocity = ParticleVelocities.IsValidIndex(Idx) ? ParticleVelocities[Idx] : FVector::ZeroVector;
		Stamp.RadiusCm = BrushRadiusCm;
		Stamp.DensityStrength = 0.85f;
		Stamp.VelocityStrength = 1.0f;
		Stamp.Channel = Channel;
		PendingBrushStamps.Add(Stamp);
	}
}

void UTDFluidLiveComponent::SetSdfObstacles(const TArray<FTDFluidSdfObstacle>& InObstacles)
{
	ActiveObstacles = InObstacles;
	bHasPendingObstacleUpdate = true;

	if (Solver.IsInitialized())
	{
		RasterizeActiveObstacles();
		bHasPendingObstacleUpdate = false;
	}
}

void UTDFluidLiveComponent::SetSimulationAwake(bool bShouldWake)
{
	if (bIsSimulationAwake == bShouldWake)
	{
		return;
	}

	bIsSimulationAwake = bShouldWake;
	if (!bIsSimulationAwake)
	{
		PendingBrushStamps.Reset();
	}

	if (!OwnedNiagaraComponent)
	{
		return;
	}

	if (bIsSimulationAwake)
	{
		OwnedNiagaraComponent->Activate(true);
		return;
	}

	OwnedNiagaraComponent->DeactivateImmediate();
}

void UTDFluidLiveComponent::RegisterExternalMaterialTarget(UMaterialInstanceDynamic* TargetMID)
{
	if (!TargetMID)
	{
		return;
	}

	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Existing : ExternalMaterialTargets)
	{
		if (Existing.Get() == TargetMID)
		{
			return;
		}
	}

	ExternalMaterialTargets.Add(TargetMID);
	ApplyParametersToMID(TargetMID);
}

void UTDFluidLiveComponent::BindInternalMeshComponent(UStaticMeshComponent* InMeshComponent)
{
	BoundInternalMeshComponent = InMeshComponent;
	ApplyLoadedOutputsToBoundMesh();
}

void UTDFluidLiveComponent::BindVolumeMeshComponent(UStaticMeshComponent* InMeshComponent)
{
	BoundVolumeMeshComponent = InMeshComponent;
	ApplyLoadedOutputsToVolumeMesh();
}

bool UTDFluidLiveComponent::SampleFluidAtWorldLocation(const FVector& WorldLocation, FTDFluidCellSample& OutSample) const
{
	return Solver.SampleAtWorldPosition(ProjectWorldToSolverPlane(WorldLocation), OutSample);
}

bool UTDFluidLiveComponent::ComputeDensityCentroidWorld(bool bUseSelectiveDensity, FVector& OutCentroidWorld, float& OutDensityAreaCm2) const
{
	FVector2D CentroidPlane;
	if (!ComputeDensityCentroidPlane(bUseSelectiveDensity, CentroidPlane, OutDensityAreaCm2))
	{
		OutCentroidWorld = FVector::ZeroVector;
		return false;
	}

	OutCentroidWorld = UnprojectSolverPlaneToWorld(CentroidPlane);
	return true;
}

bool UTDFluidLiveComponent::SetPlaneBasis(const FTDFluidPlaneBasis& InBasis, double InPlaneDepthCm)
{
	PlaneDepthCm = InPlaneDepthCm;
	if (PlaneBasis.IsNearlyEqual(InBasis))
	{
		return false;
	}

	PlaneBasis = InBasis;
	ApplyPlaneRenderFlags();
	ReinitializeSolverAtOwnerLocation();
	return true;
}

FTDFluidPlaneSettings UTDFluidLiveComponent::GetPlaneSettings() const
{
	return ActivePreset ? ActivePreset->PlaneSettings : FTDFluidPlaneSettings();
}

FVector2D UTDFluidLiveComponent::ProjectWorldToSolverPlane(const FVector& WorldPoint) const
{
	return PlaneBasis.ProjectPoint(WorldPoint);
}

bool UTDFluidLiveComponent::TryProjectWorldPointIntoSlab(const FVector& WorldPoint, FVector2D& OutPlanePoint) const
{
	OutPlanePoint = ProjectWorldToSolverPlane(WorldPoint);

	FVector2D GridUV;
	if (!Solver.WorldToGridUV(OutPlanePoint, GridUV))
	{
		return false;
	}

	if (PlaneBasis.IsWorldFacing())
	{
		return true;
	}

	return FMath::Abs(PlaneBasis.ProjectDepth(WorldPoint) - PlaneDepthCm) <= GetPlaneSettings().InteractionDepthCm;
}

FVector UTDFluidLiveComponent::UnprojectSolverPlaneToWorld(const FVector2D& PlanePoint, double DepthOffsetCm) const
{
	return PlaneBasis.UnprojectPoint(PlanePoint, PlaneDepthCm + DepthOffsetCm);
}

bool UTDFluidLiveComponent::ComputeDensityCentroidPlane(bool bUseSelectiveDensity, FVector2D& OutCentroid, float& OutAreaCm2) const
{
	OutCentroid = FVector2D::ZeroVector;
	OutAreaCm2 = 0.0f;
	if (!Solver.IsInitialized())
	{
		return false;
	}

	const int32 Resolution = Solver.GetResolution();
	const FVector2D CellSizeCm = Solver.GetExtentCm() / static_cast<double>(Resolution);
	double DensitySum = 0.0;
	FVector2D WeightedPosition = FVector2D::ZeroVector;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FTDFluidCellSample Cell = Solver.SampleAtCell(CellX, CellY);
			const double Density = bUseSelectiveDensity ? Cell.SelectiveDensity : Cell.PrimaryDensity;
			if (Density <= 0.0)
			{
				continue;
			}

			DensitySum += Density;
			WeightedPosition += Solver.CellToWorldCenter(CellX, CellY) * Density;
		}
	}

	if (DensitySum <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	OutCentroid = WeightedPosition / DensitySum;
	OutAreaCm2 = static_cast<float>(DensitySum * CellSizeCm.X * CellSizeCm.Y);
	return true;
}

void UTDFluidLiveComponent::AdvanceSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift)
{
	if (!PrepareSimulationStep(DeltaTime, DesiredCenterWorld, bAllowSlidingShift))
	{
		return;
	}

	RunPreparedSolverStep(DeltaTime);
	FinishSimulationStep();
}

bool UTDFluidLiveComponent::PrepareSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift)
{
	if (!bIsSimulationAwake || DeltaTime <= 0.0f)
	{
		return false;
	}

	if (!Solver.IsInitialized())
	{
		Solver.Initialize(SimulationConfig, DesiredCenterWorld);
	}

	bool bDidShiftWindow = false;
	if (bAllowSlidingShift)
	{
		bDidShiftWindow = Solver.ShiftOriginForSlidingWindow(DesiredCenterWorld);
	}

	if ((bDidShiftWindow || bHasPendingObstacleUpdate) && !ActiveObstacles.IsEmpty())
	{
		RasterizeActiveObstacles();
		bHasPendingObstacleUpdate = false;
	}

	if (!PlaneBasis.IsWorldFacing())
	{
		Solver.SetUniformTerrainSlope(FVector2D::ZeroVector);
	}

	Solver.SetPlaneUpDirection(PlaneBasis.GetPlaneUpDirection());
	ConvertPendingStampsToSolverPlane();
	PrioritizeAndClampPendingStamps();
	LastProcessedPointCount = PendingBrushStamps.Num();
	return true;
}

void UTDFluidLiveComponent::RunPreparedSolverStep(float DeltaTime)
{
	if (!PendingBrushStamps.IsEmpty())
	{
		Solver.ApplyBrushStamps(PendingBrushStamps, DeltaTime);
		PendingBrushStamps.Reset();
	}

	Solver.StepSimulation(DeltaTime);
	Solver.PackOutputPixels(PackedDensityPressurePixels, PackedVelocityPixels);
}

void UTDFluidLiveComponent::FinishSimulationStep()
{
	if (!Solver.IsInitialized())
	{
		return;
	}

	PushDirectDriveOutputs();
}

void UTDFluidLiveComponent::PublishCurrentOutputs()
{
	if (!Solver.IsInitialized())
	{
		return;
	}

	Solver.PackOutputPixels(PackedDensityPressurePixels, PackedVelocityPixels);
	PushDirectDriveOutputs();
}

void UTDFluidLiveComponent::RequestAsyncAssetLoads()
{
	if (AssetLoadHandle.IsValid())
	{
		AssetLoadHandle->CancelHandle();
		AssetLoadHandle.Reset();
	}

	SyncOwnedNiagaraComponent();

	TArray<FSoftObjectPath> AssetsToLoad;
	if (!OutputMaterial.IsNull())
	{
		AssetsToLoad.Add(OutputMaterial.ToSoftObjectPath());
	}
	if (!ParameterCollection.IsNull())
	{
		AssetsToLoad.Add(ParameterCollection.ToSoftObjectPath());
	}
	if (!ExternalNiagaraSystem.IsNull())
	{
		AssetsToLoad.Add(ExternalNiagaraSystem.ToSoftObjectPath());
	}
	if (ActivePreset && !ActivePreset->SurfaceMesh.IsNull())
	{
		AssetsToLoad.Add(ActivePreset->SurfaceMesh.ToSoftObjectPath());
	}
	if (ActivePreset && !ActivePreset->VolumeMaterial.IsNull())
	{
		AssetsToLoad.Add(ActivePreset->VolumeMaterial.ToSoftObjectPath());
	}
	if (ActivePreset && !ActivePreset->VolumeMesh.IsNull())
	{
		AssetsToLoad.Add(ActivePreset->VolumeMesh.ToSoftObjectPath());
	}

	if (AssetsToLoad.IsEmpty())
	{
		return;
	}

	AssetLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &UTDFluidLiveComponent::HandleAssetsLoaded));
}

void UTDFluidLiveComponent::HandleAssetsLoaded()
{
	SyncOwnedNiagaraComponent();
	ApplyLoadedOutputsToBoundMesh();
	ApplyLoadedOutputsToVolumeMesh();
}

void UTDFluidLiveComponent::SyncOwnedNiagaraComponent()
{
	UNiagaraSystem* DesiredSystem = ExternalNiagaraSystem.Get();
	if (OwnedNiagaraComponent && OwnedNiagaraSystem == DesiredSystem)
	{
		return;
	}

	DestroyOwnedNiagaraComponent();

	AActor* OwnerActor = GetOwner();
	USceneComponent* AttachRoot = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
	if (!DesiredSystem || !AttachRoot || !GetWorld())
	{
		return;
	}

	UNiagaraComponent* NewNiagaraComponent = NewObject<UNiagaraComponent>(OwnerActor, NAME_None, RF_Transient);
	NewNiagaraComponent->SetAsset(DesiredSystem);
	NewNiagaraComponent->SetAutoActivate(false);
	NewNiagaraComponent->SetAutoDestroy(false);
	NewNiagaraComponent->SetupAttachment(AttachRoot);
	NewNiagaraComponent->RegisterComponent();
	OwnedNiagaraComponent = NewNiagaraComponent;
	if (bIsSimulationAwake)
	{
		OwnedNiagaraComponent->Activate(true);
	}
	OwnedNiagaraSystem = DesiredSystem;
	DriveOwnedNiagaraComponent();
}

void UTDFluidLiveComponent::DestroyOwnedNiagaraComponent()
{
	OwnedNiagaraSystem = nullptr;
	if (!OwnedNiagaraComponent)
	{
		return;
	}

	OwnedNiagaraComponent->DestroyComponent();
	OwnedNiagaraComponent = nullptr;
}

void UTDFluidLiveComponent::ApplyLoadedOutputsToBoundMesh()
{
	UStaticMeshComponent* InternalMesh = BoundInternalMeshComponent.Get();
	if (!InternalMesh)
	{
		return;
	}

	if (ActivePreset && !InternalMesh->GetStaticMesh())
	{
		if (UStaticMesh* LoadedSurfaceMesh = ActivePreset->SurfaceMesh.Get())
		{
			InternalMesh->SetStaticMesh(LoadedSurfaceMesh);
		}
	}

	if (ActivePreset)
	{
		InternalMesh->SetTranslucentSortPriority(ActivePreset->TranslucencySortPriority);
	}
	ApplyPlaneRenderFlags();

	UMaterialInterface* LoadedMaterial = OutputMaterial.Get();
	if (!LoadedMaterial)
	{
		return;
	}

	UploadSolverOutputTextures();

	if (!InternalSurfaceMID || InternalSurfaceMID->Parent != LoadedMaterial)
	{
		InternalSurfaceMID = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
	}
	InternalMesh->SetMaterial(0, InternalSurfaceMID);
	ApplyParametersToMID(InternalSurfaceMID);
}

void UTDFluidLiveComponent::ApplyLoadedOutputsToVolumeMesh()
{
	UStaticMeshComponent* VolumeMesh = BoundVolumeMeshComponent.Get();
	if (!VolumeMesh)
	{
		return;
	}

	UMaterialInterface* LoadedMaterial = ActivePreset ? ActivePreset->VolumeMaterial.Get() : nullptr;
	UStaticMesh* LoadedMesh = ActivePreset ? ActivePreset->VolumeMesh.Get() : nullptr;
	if (!LoadedMaterial || !LoadedMesh)
	{
		VolumeMesh->SetVisibility(false);
		VolumeMesh->SetMaterial(0, nullptr);
		VolumeMID = nullptr;
		return;
	}

	if (VolumeMesh->GetStaticMesh() != LoadedMesh)
	{
		VolumeMesh->SetStaticMesh(LoadedMesh);
	}
	VolumeMesh->SetCastShadow(false);
	VolumeMesh->SetVisibleInRayTracing(false);
	VolumeMesh->SetAffectDistanceFieldLighting(false);
	VolumeMesh->SetTranslucentSortPriority(ActivePreset->TranslucencySortPriority);

	if (!VolumeMID || VolumeMID->Parent != LoadedMaterial)
	{
		VolumeMID = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
	}
	VolumeMesh->SetMaterial(0, VolumeMID);

	UploadSolverOutputTextures();
	FitVolumeMeshToSimulation();
	ApplyVolumeParameters();
	VolumeMesh->SetVisibility(Solver.IsInitialized());
}

void UTDFluidLiveComponent::FitVolumeMeshToSimulation()
{
	UStaticMeshComponent* VolumeMesh = BoundVolumeMeshComponent.Get();
	const UStaticMesh* VolumeStaticMesh = VolumeMesh ? VolumeMesh->GetStaticMesh() : nullptr;
	const AActor* OwnerActor = GetOwner();
	if (!VolumeStaticMesh || !ActivePreset || !OwnerActor || !Solver.IsInitialized())
	{
		return;
	}

	const FBoxSphereBounds MeshBounds = VolumeStaticMesh->GetBounds();
	const FVector MeshSize = MeshBounds.BoxExtent * 2.0;
	if (MeshSize.X < UE_KINDA_SMALL_NUMBER || MeshSize.Y < UE_KINDA_SMALL_NUMBER || MeshSize.Z < UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector2D CenterCm = Solver.GetCenterWorld();
	const FVector2D ExtentCm = Solver.GetExtentCm();
	const double HeightCm = ActivePreset->VolumeHeightCm;
	const double FloorHeightCm = OwnerActor->GetActorLocation().Z;

	const FVector DesiredScale = FVector(ExtentCm.X, ExtentCm.Y, HeightCm) / MeshSize;
	const FQuat DesiredRotation = PlaneBasis.GetRotation();
	const FVector DesiredCenter = PlaneBasis.IsWorldFacing()
		? FVector(CenterCm.X, CenterCm.Y, FloorHeightCm + HeightCm * 0.5)
		: UnprojectSolverPlaneToWorld(CenterCm);
	const FVector DesiredLocation = DesiredCenter - DesiredRotation.RotateVector(DesiredScale * MeshBounds.Origin);

	const bool bIsAlreadyFitted = VolumeMesh->GetComponentLocation().Equals(DesiredLocation, 0.01)
		&& VolumeMesh->GetComponentScale().Equals(DesiredScale, 0.0001)
		&& VolumeMesh->GetComponentQuat().Equals(DesiredRotation, 0.0001);
	if (bIsAlreadyFitted)
	{
		return;
	}

	VolumeMesh->SetWorldTransform(FTransform(DesiredRotation, DesiredLocation, DesiredScale));
}

void UTDFluidLiveComponent::ApplyVolumeParameters() const
{
	const AActor* OwnerActor = GetOwner();
	if (!VolumeMID || !ActivePreset || !OwnerActor)
	{
		return;
	}

	ApplyParametersToMID(VolumeMID, OwnerActor->GetActorLocation().Z);
	VolumeMID->SetScalarParameterValue(TEXT("VolumeHeightCm"), ActivePreset->VolumeHeightCm);
}

void UTDFluidLiveComponent::UploadSolverOutputTextures()
{
	if (!Solver.IsInitialized())
	{
		return;
	}

	EnsureOutputTextures();
	Solver.PackOutputPixels(PackedDensityPressurePixels, PackedVelocityPixels);
	UploadPixelsToTexture(DensityPressureTexture, PackedDensityPressurePixels);
	UploadPixelsToTexture(VelocityTexture, PackedVelocityPixels);
}

void UTDFluidLiveComponent::ReapplyActiveObstacles()
{
	if (ActiveObstacles.IsEmpty())
	{
		return;
	}

	bHasPendingObstacleUpdate = true;
	if (!Solver.IsInitialized())
	{
		return;
	}

	RasterizeActiveObstacles();
	bHasPendingObstacleUpdate = false;
}

void UTDFluidLiveComponent::RasterizeActiveObstacles()
{
	if (PlaneBasis.IsWorldFacing())
	{
		Solver.RasterizeSdfObstacles(ActiveObstacles);
		return;
	}

	TArray<FTDFluidSdfObstacle> PlaneObstacles;
	BuildSolverPlaneObstacles(PlaneObstacles);
	Solver.RasterizeSdfObstacles(PlaneObstacles);
}

void UTDFluidLiveComponent::BuildSolverPlaneObstacles(TArray<FTDFluidSdfObstacle>& OutObstacles) const
{
	OutObstacles.Reset(ActiveObstacles.Num());

	const double MaxDepthOffsetCm = GetPlaneSettings().InteractionDepthCm;
	for (const FTDFluidSdfObstacle& Obstacle : ActiveObstacles)
	{
		if (FMath::Abs(PlaneBasis.ProjectDepth(Obstacle.WorldCenter) - PlaneDepthCm) > MaxDepthOffsetCm)
		{
			continue;
		}

		const FVector2D PlaneCenter = PlaneBasis.ProjectPoint(Obstacle.WorldCenter);
		FTDFluidSdfObstacle& PlaneObstacle = OutObstacles.Add_GetRef(Obstacle);
		PlaneObstacle.WorldCenter = FVector(PlaneCenter.X, PlaneCenter.Y, 0.0);
	}
}

void UTDFluidLiveComponent::ApplyPlaneRenderFlags()
{
	UStaticMeshComponent* InternalMesh = BoundInternalMeshComponent.Get();
	if (!InternalMesh || PlaneBasis.IsWorldFacing())
	{
		return;
	}

	InternalMesh->SetCastShadow(false);
	InternalMesh->SetVisibleInRayTracing(false);
	InternalMesh->SetAffectDistanceFieldLighting(false);
}

void UTDFluidLiveComponent::ReinitializeSolverAtOwnerLocation()
{
	Solver.Initialize(SimulationConfig, ResolveOwnerCenterWorld());
	PendingBrushStamps.Reset();
	LastProcessedPointCount = 0;
	ReapplyActiveObstacles();
}

FVector2D UTDFluidLiveComponent::ResolveOwnerCenterWorld() const
{
	if (const AActor* OwnerActor = GetOwner())
	{
		const FVector OwnerLocation = OwnerActor->GetActorLocation();
		if (PlaneBasis.IsWorldFacing())
		{
			return FVector2D(OwnerLocation.X, OwnerLocation.Y);
		}

		const double CenterOffsetAlongUpCm = GetPlaneSettings().CenterOffsetAlongUpRatio * SimulationConfig.SimExtentCm.Y;
		return PlaneBasis.ProjectPoint(OwnerLocation) + FVector2D(0.0, CenterOffsetAlongUpCm);
	}

	return Solver.IsInitialized() ? Solver.GetCenterWorld() : FVector2D::ZeroVector;
}

void UTDFluidLiveComponent::ConvertPendingStampsToSolverPlane()
{
	if (PlaneBasis.IsWorldFacing())
	{
		return;
	}

	const double MaxDepthOffsetCm = GetPlaneSettings().InteractionDepthCm;
	int32 KeptCount = 0;
	for (int32 Index = 0; Index < PendingBrushStamps.Num(); ++Index)
	{
		FTDFluidBrushStamp Stamp = PendingBrushStamps[Index];
		if (FMath::Abs(PlaneBasis.ProjectDepth(Stamp.WorldLocation) - PlaneDepthCm) > MaxDepthOffsetCm)
		{
			continue;
		}

		const FVector2D PlaneLocation = PlaneBasis.ProjectPoint(Stamp.WorldLocation);
		const FVector2D PlaneVelocity = PlaneBasis.ProjectVector(Stamp.WorldVelocity);
		Stamp.WorldLocation = FVector(PlaneLocation.X, PlaneLocation.Y, 0.0);
		Stamp.WorldVelocity = FVector(PlaneVelocity.X, PlaneVelocity.Y, 0.0);
		PendingBrushStamps[KeptCount] = Stamp;
		++KeptCount;
	}

	PendingBrushStamps.SetNum(KeptCount, EAllowShrinking::No);
}


void UTDFluidLiveComponent::PrioritizeAndClampPendingStamps()
{
	const int32 MaxPoints = FMath::Max(SimulationConfig.MaxPointsPerTick, 1);
	if (PendingBrushStamps.Num() <= MaxPoints)
	{
		return;
	}

	const FVector2D SimCenter = Solver.GetCenterWorld();
	PendingBrushStamps.Sort([&SimCenter](const FTDFluidBrushStamp& Left, const FTDFluidBrushStamp& Right)
	{
		const float DistLeftSq = FVector2D::DistSquared(FVector2D(Left.WorldLocation.X, Left.WorldLocation.Y), SimCenter);
		const float DistRightSq = FVector2D::DistSquared(FVector2D(Right.WorldLocation.X, Right.WorldLocation.Y), SimCenter);
		return DistLeftSq < DistRightSq;
	});

	PendingBrushStamps.SetNum(MaxPoints, EAllowShrinking::No);
}

void UTDFluidLiveComponent::EnsureOutputTextures()
{
	if (IsRunningCommandlet() || !FApp::CanEverRender())
	{
		return;
	}

	const int32 Res = Solver.GetResolution();
	if (Res <= 0)
	{
		return;
	}

	EnsureOutputTexture(DensityPressureTexture, Res);
	EnsureOutputTexture(VelocityTexture, Res);
}

void UTDFluidLiveComponent::UploadPixelsToTexture(UTexture2D* TargetTexture, const TArray<FFloat16Color>& SourcePixels)
{
	if (!TargetTexture || TargetTexture->IsStreamable() || !TargetTexture->GetResource())
	{
		return;
	}

	const int32 Res = Solver.GetResolution();
	if (Res <= 0 || SourcePixels.Num() != Res * Res || TargetTexture->GetSizeX() != Res || TargetTexture->GetSizeY() != Res)
	{
		return;
	}

	const SIZE_T ByteCount = static_cast<SIZE_T>(SourcePixels.Num()) * sizeof(FFloat16Color);
	uint8* PixelCopy = static_cast<uint8*>(FMemory::Malloc(ByteCount));
	FMemory::Memcpy(PixelCopy, SourcePixels.GetData(), ByteCount);

	FUpdateTextureRegion2D* FullRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, static_cast<uint32>(Res), static_cast<uint32>(Res));
	TargetTexture->UpdateTextureRegions(
		0,
		1,
		FullRegion,
		static_cast<uint32>(Res) * sizeof(FFloat16Color),
		sizeof(FFloat16Color),
		PixelCopy,
		[](uint8* SourceData, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(SourceData);
			delete Regions;
		});
}

void UTDFluidLiveComponent::PushDirectDriveOutputs()
{
	EnsureOutputTextures();
	UploadPixelsToTexture(DensityPressureTexture, PackedDensityPressurePixels);
	UploadPixelsToTexture(VelocityTexture, PackedVelocityPixels);

	if (InternalSurfaceMID)
	{
		ApplyParametersToMID(InternalSurfaceMID);
	}

	FitVolumeMeshToSimulation();
	ApplyVolumeParameters();

	for (int32 Idx = ExternalMaterialTargets.Num() - 1; Idx >= 0; --Idx)
	{
		UMaterialInstanceDynamic* TargetMID = ExternalMaterialTargets[Idx].Get();
		if (!TargetMID)
		{
			ExternalMaterialTargets.RemoveAtSwap(Idx);
			continue;
		}
		ApplyParametersToMID(TargetMID);
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (UMaterialParameterCollection* LoadedMPC = PlaneBasis.IsWorldFacing() ? ParameterCollection.Get() : nullptr)
	{
		if (UMaterialParameterCollectionInstance* MPCInstance = World->GetParameterCollectionInstance(LoadedMPC))
		{
			const FVector2D Center = Solver.GetCenterWorld();
			const FVector2D Extent = Solver.GetExtentCm();
			MPCInstance->SetVectorParameterValue(TEXT("SimCenterWorld"), FLinearColor(Center.X, Center.Y, 0.0f, 1.0f));
			MPCInstance->SetVectorParameterValue(TEXT("SimExtentsCm"), FLinearColor(Extent.X, Extent.Y, SimulationConfig.BorderFadeRatio, 0.0f));
		}
	}

	DriveTaggedNiagaraComponents(World);
	DriveOwnedNiagaraComponent();
}

void UTDFluidLiveComponent::DriveOwnedNiagaraComponent()
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnedNiagaraComponent || !OwnerActor || !Solver.IsInitialized())
	{
		return;
	}

	DriveNiagaraComponent(*OwnedNiagaraComponent, *OwnerActor);
}

void UTDFluidLiveComponent::DriveNiagaraComponent(UNiagaraComponent& NiagaraComp, const AActor& HeightSourceActor) const
{
	const FVector2D Center = Solver.GetCenterWorld();
	const FVector2D Extent = Solver.GetExtentCm();

	const FVector SimCenterWorld = PlaneBasis.IsWorldFacing()
		? FVector(Center.X, Center.Y, HeightSourceActor.GetActorLocation().Z)
		: UnprojectSolverPlaneToWorld(Center);
	NiagaraComp.SetVariableVec3(FName(TEXT("User.SimCenterWorld")), SimCenterWorld);
	NiagaraComp.SetVariableVec2(FName(TEXT("User.SimExtentsCm")), Extent);
	if (DensityPressureTexture)
	{
		NiagaraComp.SetVariableTexture(FName(TEXT("User.DensityPressureTexture")), DensityPressureTexture);
	}
	if (VelocityTexture)
	{
		NiagaraComp.SetVariableTexture(FName(TEXT("User.VelocityTexture")), VelocityTexture);
	}
}

void UTDFluidLiveComponent::DriveTaggedNiagaraComponents(UWorld* World)
{
	if (DriveNiagaraComponentInTaggedActors.IsNone())
	{
		CachedTaggedNiagaraComponents.Reset();
		CachedNiagaraActorTag = NAME_None;
		return;
	}

	const bool bIsCacheStale = FMath::Abs(World->GetTimeSeconds() - LastNiagaraCacheRefreshSeconds) >= NiagaraCacheRefreshIntervalSeconds;
	if (CachedNiagaraActorTag != DriveNiagaraComponentInTaggedActors || bIsCacheStale)
	{
		RefreshTaggedNiagaraComponentCache(World);
	}

	for (const TWeakObjectPtr<UNiagaraComponent>& CachedComponent : CachedTaggedNiagaraComponents)
	{
		UNiagaraComponent* NiagaraComp = CachedComponent.Get();
		const AActor* TaggedActor = NiagaraComp ? NiagaraComp->GetOwner() : nullptr;
		if (!TaggedActor)
		{
			continue;
		}

		DriveNiagaraComponent(*NiagaraComp, *TaggedActor);
	}
}

void UTDFluidLiveComponent::RefreshTaggedNiagaraComponentCache(UWorld* World)
{
	CachedNiagaraActorTag = DriveNiagaraComponentInTaggedActors;
	LastNiagaraCacheRefreshSeconds = World->GetTimeSeconds();
	CachedTaggedNiagaraComponents.Reset();

	TArray<AActor*> TaggedActors;
	UGameplayStatics::GetAllActorsWithTag(World, DriveNiagaraComponentInTaggedActors, TaggedActors);
	for (AActor* TaggedActor : TaggedActors)
	{
		if (!TaggedActor)
		{
			continue;
		}

		TInlineComponentArray<UNiagaraComponent*> NiagaraComponents(TaggedActor);
		for (UNiagaraComponent* NiagaraComp : NiagaraComponents)
		{
			if (NiagaraComp)
			{
				CachedTaggedNiagaraComponents.Add(NiagaraComp);
			}
		}
	}
}

void UTDFluidLiveComponent::ApplyParametersToMID(UMaterialInstanceDynamic* TargetMID, float FloorHeightCm) const
{
	if (!TargetMID)
	{
		return;
	}

	const FVector2D Center = Solver.GetCenterWorld();
	const FVector2D Extent = Solver.GetExtentCm();
	const bool bIsWorldFacing = PlaneBasis.IsWorldFacing();
	const float CenterDepthCm = bIsWorldFacing ? FloorHeightCm : static_cast<float>(PlaneDepthCm);
	TargetMID->SetVectorParameterValue(TEXT("SimCenterWorld"), FLinearColor(Center.X, Center.Y, CenterDepthCm, 1.0f));
	TargetMID->SetVectorParameterValue(TEXT("SimExtentsCm"), FLinearColor(Extent.X, Extent.Y, SimulationConfig.BorderFadeRatio, 0.0f));
	TargetMID->SetVectorParameterValue(TEXT("PlaneAxisU"), FLinearColor(PlaneBasis.U.X, PlaneBasis.U.Y, PlaneBasis.U.Z, 0.0f));
	TargetMID->SetVectorParameterValue(TEXT("PlaneAxisV"), FLinearColor(PlaneBasis.V.X, PlaneBasis.V.Y, PlaneBasis.V.Z, 0.0f));
	TargetMID->SetVectorParameterValue(TEXT("PlaneAxisN"), FLinearColor(PlaneBasis.N.X, PlaneBasis.N.Y, PlaneBasis.N.Z, 0.0f));
	TargetMID->SetScalarParameterValue(TEXT("ExtrudeSymmetric"), bIsWorldFacing ? 0.0f : 1.0f);
	TargetMID->SetScalarParameterValue(TEXT("SimGridResolution"), static_cast<float>(Solver.GetResolution()));

	if (DensityPressureTexture)
	{
		TargetMID->SetTextureParameterValue(TEXT("DensityPressureTexture"), DensityPressureTexture);
	}
	if (VelocityTexture)
	{
		TargetMID->SetTextureParameterValue(TEXT("VelocityTexture"), VelocityTexture);
	}
}
