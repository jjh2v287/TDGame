#include "FluidFX/TDFluidLiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidVolumeTransmittance.h"
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
	constexpr int32 MaximumShadowCapsuleCount = 8;
	constexpr float MaximumCapsuleShadowStrength = 0.99f;

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

	constexpr int32 PainterVelocityTargetSize = 4;
	const FLinearColor EmptyDensityPressureColor(0.0f, 0.5f, 0.0f, 0.0f);
	const FLinearColor EmptyVelocityColor(0.5f, 0.5f, 0.0f, 0.0f);

	void EnsureGpuOutputTarget(TObjectPtr<UTextureRenderTarget2D>& InOutTarget, UObject* TargetOuter, int32 TargetSize, const FLinearColor& EmptyColor)
	{
		if (InOutTarget && InOutTarget->SizeX == TargetSize && InOutTarget->SizeY == TargetSize)
		{
			return;
		}

		UTextureRenderTarget2D* NewTarget = NewObject<UTextureRenderTarget2D>(TargetOuter, NAME_None, RF_Transient);
		NewTarget->RenderTargetFormat = RTF_RGBA16f;
		NewTarget->ClearColor = EmptyColor;
		NewTarget->bAutoGenerateMips = false;
		NewTarget->bSupportsUAV = true;
		NewTarget->bCanCreateUAV = true;
		NewTarget->Filter = TF_Bilinear;
		NewTarget->AddressX = TA_Clamp;
		NewTarget->AddressY = TA_Clamp;
		NewTarget->InitCustomFormat(TargetSize, TargetSize, PF_FloatRGBA, true);
		InOutTarget = NewTarget;
	}

	FTextureRenderTargetResource* ResolveRenderTargetResource(UTextureRenderTarget2D* Target)
	{
		return Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
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

	InitializeSimulation(ResolveOwnerCenterWorld());
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
	ReleaseGpuSolverSlot();
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
	const FTDFluidGpuBackendDecision BackendDecision = ResolveSolverBackend();
	const bool bKeepsGpuState = bUsesGpuSolver && BackendDecision.bUsesGpu && BackendDecision.Resolution == GpuSolverProxy.GetResolution();
	if (bKeepsGpuState)
	{
		GpuSolverProxy.UpdateSettings(SimulationConfig);
	}
	else if (bUsesGpuSolver || BackendDecision.bUsesGpu)
	{
		InitializeSimulation(IsSimulationInitialized() ? GetSimulationCenterWorld() : ResolveOwnerCenterWorld());
	}
	else if (Solver.IsInitialized())
	{
		Solver.UpdateConfig(MakeCpuSolverConfig());
	}
	else
	{
		Solver.Initialize(MakeCpuSolverConfig(), ResolveOwnerCenterWorld());
	}

	ReapplyActiveObstacles();
}

void UTDFluidLiveComponent::ResetSimulationState()
{
	PendingBrushStamps.Reset();
	LastProcessedPointCount = 0;
	if (bUsesGpuSolver)
	{
		ReleaseGpuSolverSlot();
		GpuSolverProxy.ResetState();
	}
	else
	{
		Solver.ResetSimulation();
	}
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

	if (IsSimulationInitialized())
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
		RefreshMaterialTargets();
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

void UTDFluidLiveComponent::SetWaterLayerDisplayAlpha(float Alpha)
{
	WaterLayerDisplayAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
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
	if (!bUsesGpuSolver)
	{
		return Solver.SampleAtWorldPosition(ProjectWorldToSolverPlane(WorldLocation), OutSample);
	}

	const FVector2D PlanePoint = ProjectWorldToSolverPlane(WorldLocation);
	FVector2D GridUV;
	if (!GpuSolverProxy.PlanePointToGridUV(PlanePoint, GridUV))
	{
		OutSample = FTDFluidCellSample();
		return false;
	}

	GpuSolverProxy.GetReadbackField().SampleBilinear(PlanePoint, OutSample);
	return true;
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

bool UTDFluidLiveComponent::SetPlaneBasis(const FTDFluidPlaneBasis& InBasis, const FVector& PivotWorld, double DepthBiasCm)
{
	if (InBasis.U.ContainsNaN() || InBasis.V.ContainsNaN() || PlaneBasis.IsNearlyEqual(InBasis))
	{
		PlaneDepthCm = PlaneBasis.ProjectDepth(PivotWorld) - DepthBiasCm;
		return false;
	}

	const bool bDidChangeFacingKind = PlaneBasis.IsWorldFacing() || InBasis.IsWorldFacing();
	FTDFluidPlaneBasis ReorientedBasis = InBasis;
	ReorientedBasis.PreservePivotPlanePoint(PlaneBasis, PivotWorld);
	PlaneBasis = ReorientedBasis;
	PlaneDepthCm = PlaneBasis.ProjectDepth(PivotWorld) - DepthBiasCm;
	if (bDidChangeFacingKind)
	{
		ApplyPlaneRenderFlags();
		ReinitializeSolverAtOwnerLocation();
		return true;
	}

	bHasPendingObstacleUpdate = bHasPendingObstacleUpdate || !ActiveObstacles.IsEmpty();
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
	const bool bIsInsideSimulation = bUsesGpuSolver ? GpuSolverProxy.PlanePointToGridUV(OutPlanePoint, GridUV) : Solver.WorldToGridUV(OutPlanePoint, GridUV);
	if (!bIsInsideSimulation)
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
	if (bUsesGpuSolver)
	{
		return GpuSolverProxy.GetReadbackField().ComputeDensityCentroid(bUseSelectiveDensity, OutCentroid, OutAreaCm2);
	}

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

	SyncSolverBackend(DesiredCenterWorld);
	if (bUsesGpuSolver)
	{
		return PrepareGpuSimulationStep(DeltaTime, DesiredCenterWorld, bAllowSlidingShift);
	}

	if (!Solver.IsInitialized())
	{
		Solver.Initialize(MakeCpuSolverConfig(), DesiredCenterWorld);
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
	if (bUsesGpuSolver)
	{
		return;
	}

	if (!PendingBrushStamps.IsEmpty())
	{
		Solver.ApplyBrushStamps(PendingBrushStamps, DeltaTime);
		PendingBrushStamps.Reset();
	}

	Solver.StepSimulation(DeltaTime);
	Solver.PackOutputPixels(PackedDensityPressurePixels, PackedVelocityPixels);
	PackWaterSurfaceOutputs();
	PackAuxAndTransmittanceOutputs();
}

void UTDFluidLiveComponent::FinishSimulationStep()
{
	if (!IsSimulationInitialized())
	{
		return;
	}

	PushDirectDriveOutputs();
}

void UTDFluidLiveComponent::PublishCurrentOutputs()
{
	if (!IsSimulationInitialized())
	{
		return;
	}

	if (bUsesGpuSolver)
	{
		ClearGpuOutputTargets();
		PushDirectDriveOutputs();
		return;
	}

	Solver.PackOutputPixels(PackedDensityPressurePixels, PackedVelocityPixels);
	PackWaterSurfaceOutputs();
	PackAuxAndTransmittanceOutputs();
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
	VolumeMesh->SetVisibility(IsSimulationInitialized());
}

void UTDFluidLiveComponent::FitVolumeMeshToSimulation()
{
	UStaticMeshComponent* VolumeMesh = BoundVolumeMeshComponent.Get();
	const UStaticMesh* VolumeStaticMesh = VolumeMesh ? VolumeMesh->GetStaticMesh() : nullptr;
	const AActor* OwnerActor = GetOwner();
	if (!VolumeStaticMesh || !ActivePreset || !OwnerActor || !IsSimulationInitialized())
	{
		return;
	}

	const FBoxSphereBounds MeshBounds = VolumeStaticMesh->GetBounds();
	const FVector MeshSize = MeshBounds.BoxExtent * 2.0;
	if (MeshSize.X < UE_KINDA_SMALL_NUMBER || MeshSize.Y < UE_KINDA_SMALL_NUMBER || MeshSize.Z < UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector2D CenterCm = GetSimulationCenterWorld();
	const FVector2D ExtentCm = GetSimulationExtentCm();
	const double HeightCm = ActivePreset->VolumeHeightCm;
	const double FloorHeightCm = OwnerActor->GetActorLocation().Z;

	const FTDFluidVolumeRenderSettings& VolumeRender = ActivePreset->VolumeRender;
	const double SizeMultiplier = VolumeRender.VolumeSizeMultiplier;
	const FVector DesiredScale = SizeMultiplier == 1.0
		? FVector(ExtentCm.X, ExtentCm.Y, HeightCm) / MeshSize
		: FVector(ExtentCm.X * SizeMultiplier, ExtentCm.Y * SizeMultiplier, HeightCm) / MeshSize;
	const FQuat DesiredRotation = PlaneBasis.GetRotation();
	FVector DesiredCenter = PlaneBasis.IsWorldFacing()
		? FVector(CenterCm.X, CenterCm.Y, FloorHeightCm + HeightCm * 0.5)
		: UnprojectSolverPlaneToWorld(CenterCm);
	if (!VolumeRender.VolumePositionOffsetCm.IsZero())
	{
		const FVector& Offset = VolumeRender.VolumePositionOffsetCm;
		DesiredCenter += PlaneBasis.U * Offset.X + PlaneBasis.V * Offset.Y + PlaneBasis.N * Offset.Z;
	}
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
	ApplyVolumeRenderParameters();
}

void UTDFluidLiveComponent::UploadSolverOutputTextures()
{
	if (!IsSimulationInitialized())
	{
		return;
	}

	EnsureOutputTextures();
	if (bUsesGpuSolver)
	{
		return;
	}

	Solver.PackOutputPixels(PackedDensityPressurePixels, PackedVelocityPixels);
	PackWaterSurfaceOutputs();
	PackAuxAndTransmittanceOutputs();
	UploadPixelsToTexture(DensityPressureTexture, PackedDensityPressurePixels);
	UploadPixelsToTexture(VelocityTexture, PackedVelocityPixels);
	UploadPixelsToTexture(WaterSurfaceTexture, PackedWaterSurfacePixels);
	UploadPixelsToTexture(AuxTexture, PackedAuxPixels);
	UploadPixelsToSizedTexture(TransmittanceTexture, PackedTransmittancePixels, TDFluidVolumeTransmittance::ComputeOutputResolution(Solver.GetResolution()));
}

void UTDFluidLiveComponent::ReapplyActiveObstacles()
{
	if (ActiveObstacles.IsEmpty())
	{
		return;
	}

	bHasPendingObstacleUpdate = true;
	if (!IsSimulationInitialized())
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
		if (bUsesGpuSolver)
		{
			GpuSolverProxy.SetObstacles(ActiveObstacles);
			return;
		}

		Solver.RasterizeSdfObstacles(ActiveObstacles);
		return;
	}

	TArray<FTDFluidSdfObstacle> PlaneObstacles;
	BuildSolverPlaneObstacles(PlaneObstacles);
	if (bUsesGpuSolver)
	{
		GpuSolverProxy.SetObstacles(PlaneObstacles);
		return;
	}

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
	PlaneBasis.PlaneCoordinateOffsetCm = FVector2D::ZeroVector;
	InitializeSimulation(ResolveOwnerCenterWorld());
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

	return IsSimulationInitialized() ? GetSimulationCenterWorld() : FVector2D::ZeroVector;
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

	if (bUsesGpuSolver)
	{
		EnsureGpuOutputTargets();
		return;
	}

	const int32 Res = Solver.GetResolution();
	if (Res <= 0)
	{
		return;
	}

	EnsureOutputTexture(DensityPressureTexture, Res);
	EnsureOutputTexture(VelocityTexture, Res);
	if (IsAuxOutputActive())
	{
		EnsureOutputTexture(AuxTexture, Res);
	}
	else
	{
		AuxTexture = nullptr;
	}
	if (IsTransmittanceActive())
	{
		EnsureOutputTexture(TransmittanceTexture, TDFluidVolumeTransmittance::ComputeOutputResolution(Res));
	}
	else
	{
		TransmittanceTexture = nullptr;
	}

	if (!Solver.GetConfig().bEnableWaterLayer)
	{
		WaterSurfaceTexture = nullptr;
		return;
	}

	EnsureOutputTexture(WaterSurfaceTexture, Res);
}

void UTDFluidLiveComponent::PackWaterSurfaceOutputs()
{
	if (!IsWaterLayerEnabled())
	{
		PackedWaterSurfacePixels.Reset();
		return;
	}

	Solver.PackWaterSurfacePixels(PackedWaterSurfacePixels, TerrainReferenceZCm);
}

void UTDFluidLiveComponent::RefreshMaterialTargets()
{
	if (!IsWaterLayerEnabled())
	{
		return;
	}

	if (InternalSurfaceMID)
	{
		ApplyParametersToMID(InternalSurfaceMID);
	}
	ApplyParametersToExternalTargets();
}

void UTDFluidLiveComponent::ApplyParametersToExternalTargets()
{
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
	if (!bUsesGpuSolver)
	{
		UploadPixelsToTexture(DensityPressureTexture, PackedDensityPressurePixels);
		UploadPixelsToTexture(VelocityTexture, PackedVelocityPixels);
		UploadPixelsToTexture(WaterSurfaceTexture, PackedWaterSurfacePixels);
		UploadPixelsToTexture(AuxTexture, PackedAuxPixels);
		UploadPixelsToSizedTexture(TransmittanceTexture, PackedTransmittancePixels, TDFluidVolumeTransmittance::ComputeOutputResolution(Solver.GetResolution()));
	}

	if (InternalSurfaceMID)
	{
		ApplyParametersToMID(InternalSurfaceMID);
	}

	FitVolumeMeshToSimulation();
	ApplyVolumeParameters();
	ApplyParametersToExternalTargets();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (UMaterialParameterCollection* LoadedMPC = PlaneBasis.IsWorldFacing() ? ParameterCollection.Get() : nullptr)
	{
		if (UMaterialParameterCollectionInstance* MPCInstance = World->GetParameterCollectionInstance(LoadedMPC))
		{
			const FVector2D Center = GetSimulationCenterWorld();
			const FVector2D Extent = GetSimulationExtentCm();
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
	if (!OwnedNiagaraComponent || !OwnerActor || !IsSimulationInitialized())
	{
		return;
	}

	DriveNiagaraComponent(*OwnedNiagaraComponent, *OwnerActor);
}

void UTDFluidLiveComponent::DriveNiagaraComponent(UNiagaraComponent& NiagaraComp, const AActor& HeightSourceActor) const
{
	const FVector2D Center = GetSimulationCenterWorld();
	const FVector2D Extent = GetSimulationExtentCm();

	const FVector SimCenterWorld = PlaneBasis.IsWorldFacing()
		? FVector(Center.X, Center.Y, HeightSourceActor.GetActorLocation().Z)
		: UnprojectSolverPlaneToWorld(Center);
	NiagaraComp.SetVariableVec3(FName(TEXT("User.SimCenterWorld")), SimCenterWorld);
	NiagaraComp.SetVariableVec2(FName(TEXT("User.SimExtentsCm")), Extent);
	NiagaraComp.SetVariableVec2(FName(TEXT("User.SimCenterPlane")), Center);
	NiagaraComp.SetVariableVec3(FName(TEXT("User.PlaneAxisU")), PlaneBasis.U);
	NiagaraComp.SetVariableVec3(FName(TEXT("User.PlaneAxisV")), PlaneBasis.V);
	NiagaraComp.SetVariableVec3(FName(TEXT("User.PlaneAxisN")), PlaneBasis.N);
	NiagaraComp.SetVariableVec2(FName(TEXT("User.PlaneCoordinateOffsetCm")), PlaneBasis.PlaneCoordinateOffsetCm);
	NiagaraComp.SetVariableFloat(FName(TEXT("User.VelocityReferenceCmPerSec")), FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec());
	if (UTexture* DensityPressureOutput = GetDensityPressureTexture())
	{
		NiagaraComp.SetVariableTexture(FName(TEXT("User.DensityPressureTexture")), DensityPressureOutput);
	}
	if (UTexture* VelocityOutput = GetVelocityTexture())
	{
		NiagaraComp.SetVariableTexture(FName(TEXT("User.VelocityTexture")), VelocityOutput);
	}
	if (WaterSurfaceTexture)
	{
		NiagaraComp.SetVariableTexture(FName(TEXT("User.WaterSurfaceTexture")), WaterSurfaceTexture);
	}
	if (UTexture* AuxOutput = GetAuxTexture())
	{
		NiagaraComp.SetVariableTexture(FName(TEXT("User.AuxTexture")), AuxOutput);
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

	const FVector2D Center = GetSimulationCenterWorld();
	const FVector2D Extent = GetSimulationExtentCm();
	const bool bIsWorldFacing = PlaneBasis.IsWorldFacing();
	const float CenterDepthCm = bIsWorldFacing ? FloorHeightCm : static_cast<float>(PlaneDepthCm);
	TargetMID->SetVectorParameterValue(TEXT("SimCenterWorld"), FLinearColor(Center.X, Center.Y, CenterDepthCm, 1.0f));
	TargetMID->SetVectorParameterValue(TEXT("SimExtentsCm"), FLinearColor(Extent.X, Extent.Y, SimulationConfig.BorderFadeRatio, 0.0f));
	TargetMID->SetVectorParameterValue(TEXT("PlaneAxisU"), FLinearColor(PlaneBasis.U.X, PlaneBasis.U.Y, PlaneBasis.U.Z, 0.0f));
	TargetMID->SetVectorParameterValue(TEXT("PlaneAxisV"), FLinearColor(PlaneBasis.V.X, PlaneBasis.V.Y, PlaneBasis.V.Z, 0.0f));
	TargetMID->SetVectorParameterValue(TEXT("PlaneAxisN"), FLinearColor(PlaneBasis.N.X, PlaneBasis.N.Y, PlaneBasis.N.Z, 0.0f));
	TargetMID->SetVectorParameterValue(TEXT("PlaneCoordinateOffsetCm"), FLinearColor(PlaneBasis.PlaneCoordinateOffsetCm.X, PlaneBasis.PlaneCoordinateOffsetCm.Y, 0.0f, 0.0f));
	TargetMID->SetScalarParameterValue(TEXT("ExtrudeSymmetric"), bIsWorldFacing ? 0.0f : 1.0f);
	TargetMID->SetScalarParameterValue(TEXT("SimGridResolution"), static_cast<float>(GetSimulationResolution()));

	if (UTexture* DensityPressureOutput = GetDensityPressureTexture())
	{
		TargetMID->SetTextureParameterValue(TEXT("DensityPressureTexture"), DensityPressureOutput);
	}
	if (UTexture* VelocityOutput = GetVelocityTexture())
	{
		TargetMID->SetTextureParameterValue(TEXT("VelocityTexture"), VelocityOutput);
	}

	UTexture* AuxOutput = IsAuxOutputActive() ? GetAuxTexture() : nullptr;
	if (AuxOutput)
	{
		TargetMID->SetTextureParameterValue(TEXT("AuxTexture"), AuxOutput);
	}
	TargetMID->SetScalarParameterValue(TEXT("AuxHeatActive"), AuxOutput ? 1.0f : 0.0f);

	if (!IsWaterLayerEnabled())
	{
		return;
	}

	const FTDFluidSimulationConfig& SolverConfig = Solver.GetConfig();

	if (WaterSurfaceTexture)
	{
		TargetMID->SetTextureParameterValue(TEXT("WaterSurfaceTexture"), WaterSurfaceTexture);
	}
	const double CoastWavePeriodSeconds = FMath::Max(static_cast<double>(SolverConfig.CoastWavePeriodSeconds), 0.1);
	const float CoastWavePhase = static_cast<float>(FMath::Frac(Solver.GetSimulationTimeSeconds() / CoastWavePeriodSeconds));
	TargetMID->SetScalarParameterValue(TEXT("WaterDepthReferenceCm"), SolverConfig.WaterDepthReferenceCm);
	TargetMID->SetScalarParameterValue(TEXT("RippleHeightReferenceCm"), SolverConfig.RippleHeightReferenceCm);
	TargetMID->SetScalarParameterValue(TEXT("VelocitySpeedReferenceCmPerSec"), FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec());
	TargetMID->SetScalarParameterValue(TEXT("FluidWaterLayerActive"), bIsSimulationAwake ? WaterLayerDisplayAlpha : 0.0f);
	TargetMID->SetScalarParameterValue(TEXT("CoastWavePhase"), CoastWavePhase);
	TargetMID->SetScalarParameterValue(TEXT("CoastWaveDepthSpacingCm"), SolverConfig.CoastWaveDepthSpacingCm);
	TargetMID->SetScalarParameterValue(TEXT("CoastWaveMaxDepthCm"), SolverConfig.CoastWaveMaxDepthCm);
	TargetMID->SetScalarParameterValue(TEXT("SwashHeightCm"), SolverConfig.SwashHeightCm);
	TargetMID->SetScalarParameterValue(TEXT("TerrainReferenceZCm"), TerrainReferenceZCm);
}

UTexture* UTDFluidLiveComponent::GetDensityPressureTexture() const
{
	if (bUsesGpuSolver)
	{
		return GpuDensityPressureTarget.Get();
	}
	return DensityPressureTexture.Get();
}

UTexture* UTDFluidLiveComponent::GetVelocityTexture() const
{
	if (bUsesGpuSolver)
	{
		return GpuVelocityTarget.Get();
	}
	return VelocityTexture.Get();
}

bool UTDFluidLiveComponent::IsSimulationInitialized() const
{
	return bUsesGpuSolver ? GpuSolverProxy.IsInitialized() : Solver.IsInitialized();
}

int32 UTDFluidLiveComponent::GetSimulationResolution() const
{
	return bUsesGpuSolver ? GpuSolverProxy.GetResolution() : Solver.GetResolution();
}

FVector2D UTDFluidLiveComponent::GetSimulationExtentCm() const
{
	return bUsesGpuSolver ? GpuSolverProxy.GetExtentCm() : Solver.GetExtentCm();
}

FVector2D UTDFluidLiveComponent::GetSimulationCenterWorld() const
{
	return bUsesGpuSolver ? GpuSolverProxy.GetCenterPlaneCm() : Solver.GetCenterWorld();
}

int32 UTDFluidLiveComponent::GetSimulationGeneration() const
{
	return Solver.GetReinitializationGeneration() + GpuSolverProxy.GetGeneration();
}

FVector2D UTDFluidLiveComponent::CellToPlaneCenter(int32 CellX, int32 CellY) const
{
	return bUsesGpuSolver ? GpuSolverProxy.CellToPlaneCenter(CellX, CellY) : Solver.CellToWorldCenter(CellX, CellY);
}

FTDFluidCellSample UTDFluidLiveComponent::SampleSimulationCell(int32 CellX, int32 CellY) const
{
	return bUsesGpuSolver ? GpuSolverProxy.SampleSimulationCell(CellX, CellY) : Solver.SampleAtCell(CellX, CellY);
}

void UTDFluidLiveComponent::SetTerrainElevationAtCell(int32 CellX, int32 CellY, float ElevationCm)
{
	if (bUsesGpuSolver)
	{
		GpuSolverProxy.SetTerrainElevationAtCell(CellX, CellY, ElevationCm);
		return;
	}

	Solver.SetTerrainElevationAtCell(CellX, CellY, ElevationCm);
}

void UTDFluidLiveComponent::CompleteTerrainElevationSampling()
{
	if (bUsesGpuSolver)
	{
		GpuSolverProxy.MarkTerrainSamplingComplete();
		return;
	}

	Solver.ComputeTerrainSlopeFromElevation();
}

bool UTDFluidLiveComponent::AppendPreparedGpuStep(UTDFluidLiveSubsystem& FluidSubsystem, FTDFluidGpuFrameInputs& InOutFrameInputs)
{
	if (!bUsesGpuSolver || !GpuSolverProxy.HasPreparedStep())
	{
		return false;
	}

	if (!GpuSolverProxy.HasActorKey())
	{
		GpuSolverProxy.AssignActorKey(FluidSubsystem.AllocateGpuSolverActorKey());
	}

	EnsureOutputTextures();
	FTextureRenderTargetResource* DensityPressureOutput = ResolveRenderTargetResource(GpuDensityPressureTarget.Get());
	FTextureRenderTargetResource* VelocityOutput = GpuSolverProxy.UsesVelocityOutput() ? ResolveRenderTargetResource(GpuVelocityTarget.Get()) : nullptr;
	FTextureRenderTargetResource* AuxOutput = IsAuxOutputActive() ? ResolveRenderTargetResource(GpuAuxTarget.Get()) : nullptr;
	FTextureRenderTargetResource* TransmittanceOutput = IsTransmittanceActive() ? ResolveRenderTargetResource(GpuTransmittanceTarget.Get()) : nullptr;
	GpuSolverProxy.SetTransmittanceInput(BuildGpuTransmittanceInput());
	FTDFluidGpuActorInput ActorInput;
	if (!GpuSolverProxy.GatherStep(InOutFrameInputs.FrameNumber, InOutFrameInputs.Actors.Num(), DensityPressureOutput, VelocityOutput, ActorInput, AuxOutput, TransmittanceOutput))
	{
		return false;
	}

	InOutFrameInputs.Actors.Add(MoveTemp(ActorInput));
	return true;
}

void UTDFluidLiveComponent::ConsumeGpuReadback(const FTDFluidGpuReadbackFrame& ReadbackFrame)
{
	if (!bUsesGpuSolver)
	{
		return;
	}

	GpuSolverProxy.ConsumeReadbackFrame(ReadbackFrame);
}

FTDFluidGpuBackendDecision UTDFluidLiveComponent::ResolveSolverBackend() const
{
	FTDFluidGpuBackendQuery BackendQuery;
	BackendQuery.Preference = FTDFluidGpuSolverProxy::ToSolverPreference(SimulationConfig.SolverBackend);
	BackendQuery.bIsWaterLayerActive = SimulationConfig.bEnableWaterLayer;
	BackendQuery.RequestedResolution = SimulationConfig.GridResolution;
	BackendQuery.CpuFallbackResolution = SimulationConfig.CpuFallbackResolution;
	return FTDFluidGpuBackendSelection::Decide(BackendQuery, FTDFluidGpuBackendSelection::CaptureEnvironment());
}

void UTDFluidLiveComponent::InitializeSimulation(const FVector2D& CenterWorld)
{
	const FTDFluidGpuBackendDecision BackendDecision = ResolveSolverBackend();
	if (!BackendDecision.bUsesGpu)
	{
		if (bUsesGpuSolver)
		{
			ReleaseGpuSolverSlot();
			GpuDensityPressureTarget = nullptr;
			GpuVelocityTarget = nullptr;
			GpuAuxTarget = nullptr;
			GpuTransmittanceTarget = nullptr;
			bUsesGpuSolver = false;
		}

		Solver.Initialize(MakeCpuSolverConfig(), CenterWorld);
		return;
	}

	bUsesGpuSolver = true;
	DensityPressureTexture = nullptr;
	VelocityTexture = nullptr;
	WaterSurfaceTexture = nullptr;
	AuxTexture = nullptr;
	TransmittanceTexture = nullptr;
	GpuSolverProxy.Initialize(SimulationConfig, BackendDecision.Resolution, CenterWorld);
}

void UTDFluidLiveComponent::SyncSolverBackend(const FVector2D& FallbackCenterWorld)
{
	const FTDFluidGpuBackendDecision BackendDecision = ResolveSolverBackend();
	const bool bHasSameBackend = BackendDecision.bUsesGpu == bUsesGpuSolver;
	const bool bHasSameGpuResolution = !BackendDecision.bUsesGpu || BackendDecision.Resolution == GpuSolverProxy.GetResolution();
	if (bHasSameBackend && bHasSameGpuResolution)
	{
		return;
	}

	InitializeSimulation(IsSimulationInitialized() ? GetSimulationCenterWorld() : FallbackCenterWorld);
	ReapplyActiveObstacles();
}

bool UTDFluidLiveComponent::PrepareGpuSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift)
{
	const bool bDidShiftWindow = bAllowSlidingShift && GpuSolverProxy.ShiftWindowTowards(DesiredCenterWorld) != FIntPoint::ZeroValue;
	if ((bDidShiftWindow || bHasPendingObstacleUpdate) && !ActiveObstacles.IsEmpty())
	{
		RasterizeActiveObstacles();
		bHasPendingObstacleUpdate = false;
	}

	ConvertPendingStampsToSolverPlane();
	LastProcessedPointCount = FMath::Min(PendingBrushStamps.Num(), TDFluidGpuSolverConstants::MaximumStampsPerTick);
	GpuSolverProxy.PrepareStep(DeltaTime, PendingBrushStamps, PlaneBasis.GetPlaneUpDirection(), PlaneBasis.IsWorldFacing());
	PendingBrushStamps.Reset();
	return true;
}

void UTDFluidLiveComponent::EnsureGpuOutputTargets()
{
	const int32 GpuResolution = GpuSolverProxy.GetResolution();
	if (GpuResolution <= 0)
	{
		return;
	}

	EnsureGpuOutputTarget(GpuDensityPressureTarget, this, GpuResolution, EmptyDensityPressureColor);
	if (GpuSolverProxy.UsesVelocityOutput())
	{
		EnsureGpuOutputTarget(GpuVelocityTarget, this, GpuResolution, EmptyVelocityColor);
	}
	else
	{
		EnsureGpuOutputTarget(GpuVelocityTarget, this, PainterVelocityTargetSize, FLinearColor::Black);
	}
	if (IsAuxOutputActive())
	{
		EnsureGpuOutputTarget(GpuAuxTarget, this, GpuResolution, FLinearColor::Transparent);
	}
	else
	{
		GpuAuxTarget = nullptr;
	}
	if (IsTransmittanceActive())
	{
		EnsureGpuOutputTarget(GpuTransmittanceTarget, this, TDFluidVolumeTransmittance::ComputeOutputResolution(GpuResolution), FLinearColor::Transparent);
	}
	else
	{
		GpuTransmittanceTarget = nullptr;
	}
	WaterSurfaceTexture = nullptr;
}

void UTDFluidLiveComponent::ClearGpuOutputTargets()
{
	if (GpuDensityPressureTarget)
	{
		GpuDensityPressureTarget->UpdateResourceImmediate(true);
	}
	if (GpuVelocityTarget)
	{
		GpuVelocityTarget->UpdateResourceImmediate(true);
	}
	if (GpuAuxTarget)
	{
		GpuAuxTarget->UpdateResourceImmediate(true);
	}
	if (GpuTransmittanceTarget)
	{
		GpuTransmittanceTarget->UpdateResourceImmediate(true);
	}
}

void UTDFluidLiveComponent::ReleaseGpuSolverSlot()
{
	const uint64 ReleasedActorKey = GpuSolverProxy.ReleaseActorKey();
	if (ReleasedActorKey == 0)
	{
		return;
	}

	UTDFluidLiveSubsystem* FluidSubsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(this);
	if (!FluidSubsystem)
	{
		return;
	}

	FluidSubsystem->ReleaseGpuSolverActorKey(ReleasedActorKey);
}

bool UTDFluidLiveComponent::IsWaterLayerEnabled() const
{
	return !bUsesGpuSolver && Solver.GetConfig().bEnableWaterLayer;
}

UTexture* UTDFluidLiveComponent::GetAuxTexture() const
{
	if (bUsesGpuSolver)
	{
		return GpuAuxTarget.Get();
	}
	return AuxTexture.Get();
}

UTexture* UTDFluidLiveComponent::GetTransmittanceTexture() const
{
	if (bUsesGpuSolver)
	{
		return GpuTransmittanceTarget.Get();
	}
	return TransmittanceTexture.Get();
}

bool UTDFluidLiveComponent::IsAuxOutputActive() const
{
	return SimulationConfig.bEnableHeatChannel && SimulationConfig.SimMode == ETDFluidSimMode::FluidSim && !SimulationConfig.bEnableWaterLayer;
}

bool UTDFluidLiveComponent::IsTransmittanceActive() const
{
	return ActivePreset
		&& ActivePreset->VolumeRender.bEnableTransmittance
		&& CachedVolumeLightState.Mode > 0
		&& SimulationConfig.SimMode == ETDFluidSimMode::FluidSim
		&& IsSimulationInitialized();
}

void UTDFluidLiveComponent::SetVolumeTransmittanceSources(const FTDFluidVolumeLightState& LightState, TConstArrayView<FTDFluidShadowCapsule> Capsules)
{
	CachedVolumeLightState = LightState;
	CachedShadowCapsules.Reset();
	CachedShadowCapsules.Append(Capsules.GetData(), FMath::Min(Capsules.Num(), MaximumShadowCapsuleCount));
}

FTDFluidSimulationConfig UTDFluidLiveComponent::MakeCpuSolverConfig() const
{
	FTDFluidSimulationConfig CpuConfig = SimulationConfig;
	const FTDFluidGpuBackendDecision BackendDecision = ResolveSolverBackend();
	if (!BackendDecision.bUsesGpu)
	{
		CpuConfig.GridResolution = BackendDecision.Resolution;
	}
	return CpuConfig;
}

void UTDFluidLiveComponent::PackAuxAndTransmittanceOutputs()
{
	if (bUsesGpuSolver)
	{
		return;
	}

	Solver.PackAuxPixels(PackedAuxPixels);
	FTDFluidTransmittanceInputs TransmittanceInputs;
	if (!BuildTransmittanceInputs(TransmittanceInputs))
	{
		PackedTransmittancePixels.Reset();
		return;
	}

	TDFluidVolumeTransmittance::ComputePixels(TransmittanceInputs, PackedDensityPressurePixels, PackedTransmittancePixels);
}

FVector UTDFluidLiveComponent::ToPlaneCoordinates(const FVector& WorldVector) const
{
	return FVector(FVector::DotProduct(WorldVector, PlaneBasis.U), FVector::DotProduct(WorldVector, PlaneBasis.V), FVector::DotProduct(WorldVector, PlaneBasis.N));
}

FVector UTDFluidLiveComponent::ToSolverPlanePoint(const FVector& WorldPoint) const
{
	const FVector2D PlanePoint = PlaneBasis.ProjectPoint(WorldPoint);
	return FVector(PlanePoint.X, PlanePoint.Y, PlaneBasis.ProjectDepth(WorldPoint));
}

FVector UTDFluidLiveComponent::ComputeVolumeBoxMinimumPlane() const
{
	const FVector2D Center = GetSimulationCenterWorld();
	const FVector2D Extent = GetSimulationExtentCm();
	const double HeightCm = ActivePreset ? ActivePreset->VolumeHeightCm : 0.0;
	const AActor* OwnerActor = GetOwner();
	const bool bIsWorldFacing = PlaneBasis.IsWorldFacing();
	const double CenterDepthCm = bIsWorldFacing ? (OwnerActor ? OwnerActor->GetActorLocation().Z : 0.0) : PlaneDepthCm;
	const double FloorCm = bIsWorldFacing ? CenterDepthCm : CenterDepthCm - 0.5 * HeightCm;
	const double OffsetZ = ActivePreset ? ActivePreset->VolumeRender.VolumePositionOffsetCm.Z : 0.0;
	return FVector(Center.X - 0.5 * Extent.X, Center.Y - 0.5 * Extent.Y, FloorCm + OffsetZ);
}

bool UTDFluidLiveComponent::BuildTransmittanceInputs(FTDFluidTransmittanceInputs& OutInputs) const
{
	if (!IsTransmittanceActive())
	{
		return false;
	}

	const FTDFluidVolumeRenderSettings& VolumeRender = ActivePreset->VolumeRender;
	const FVector BoxMinimumPlane = ComputeVolumeBoxMinimumPlane();
	OutInputs.SimResolution = GetSimulationResolution();
	OutInputs.ExtentCm = GetSimulationExtentCm();
	OutInputs.VolumeHeightCm = ActivePreset->VolumeHeightCm;
	OutInputs.bIsSymmetric = !PlaneBasis.IsWorldFacing();
	OutInputs.DensityScale = VolumeRender.DensityScale;
	OutInputs.SoftFalloffCm = VolumeRender.SoftFalloffCm;
	OutInputs.MarchStepCount = VolumeRender.MarchStepCount;
	OutInputs.LightMode = CachedVolumeLightState.Mode;
	OutInputs.LightPositionLocalCm = ToSolverPlanePoint(CachedVolumeLightState.PositionWorld) - BoxMinimumPlane;
	OutInputs.LightDirectionLocal = ToPlaneCoordinates(CachedVolumeLightState.DirectionWorld);
	return true;
}

FTDFluidGpuTransmittanceInput UTDFluidLiveComponent::BuildGpuTransmittanceInput() const
{
	FTDFluidGpuTransmittanceInput GpuInput;
	FTDFluidTransmittanceInputs Inputs;
	if (!BuildTransmittanceInputs(Inputs))
	{
		return GpuInput;
	}

	GpuInput.bIsEnabled = true;
	GpuInput.LightMode = Inputs.LightMode;
	GpuInput.LightPositionLocalCm = FVector3f(Inputs.LightPositionLocalCm);
	GpuInput.LightDirectionLocal = FVector3f(Inputs.LightDirectionLocal);
	GpuInput.VolumeHeightCm = Inputs.VolumeHeightCm;
	GpuInput.bIsSymmetric = Inputs.bIsSymmetric;
	GpuInput.DensityScale = Inputs.DensityScale;
	GpuInput.SoftFalloffCm = Inputs.SoftFalloffCm;
	GpuInput.MarchStepCount = Inputs.MarchStepCount;
	return GpuInput;
}

void UTDFluidLiveComponent::ApplyVolumeRenderParameters() const
{
	if (!VolumeMID || !ActivePreset)
	{
		return;
	}

	const FTDFluidVolumeRenderSettings& VolumeRender = ActivePreset->VolumeRender;
	VolumeMID->SetScalarParameterValue(TEXT("VolumeFadeEndCm"), VolumeRender.VolumeFadeEndCm);
	VolumeMID->SetScalarParameterValue(TEXT("VolumeFadeLengthCm"), VolumeRender.VolumeFadeLengthCm);
	VolumeMID->SetScalarParameterValue(TEXT("VolumeBoxScale"), VolumeRender.VolumeSizeMultiplier);
	VolumeMID->SetVectorParameterValue(TEXT("VolumeOffsetCm"), FLinearColor(VolumeRender.VolumePositionOffsetCm.X, VolumeRender.VolumePositionOffsetCm.Y, VolumeRender.VolumePositionOffsetCm.Z, 0.0f));
	VolumeMID->SetVectorParameterValue(TEXT("HeatEmissionColor"), VolumeRender.HeatEmissionColor);
	VolumeMID->SetScalarParameterValue(TEXT("HeatEmissionIntensity"), VolumeRender.HeatEmissionIntensity);
	VolumeMID->SetScalarParameterValue(TEXT("HeatEmissionExponent"), VolumeRender.HeatEmissionExponent);
	if (VolumeRender.bEnableTransmittance)
	{
		VolumeMID->SetScalarParameterValue(TEXT("DensityScale"), VolumeRender.DensityScale);
		VolumeMID->SetScalarParameterValue(TEXT("SoftFalloffCm"), VolumeRender.SoftFalloffCm);
	}

	UTexture* TransmittanceOutput = IsTransmittanceActive() ? GetTransmittanceTexture() : nullptr;
	if (TransmittanceOutput)
	{
		VolumeMID->SetTextureParameterValue(TEXT("TransmittanceTexture"), TransmittanceOutput);
	}
	VolumeMID->SetScalarParameterValue(TEXT("TransmittanceActive"), TransmittanceOutput ? 1.0f : 0.0f);

	const int32 CapsuleCount = FMath::Min(CachedShadowCapsules.Num(), MaximumShadowCapsuleCount);
	float CapsuleRadii[MaximumShadowCapsuleCount] = {};
	for (int32 CapsuleIndex = 0; CapsuleIndex < CapsuleCount; ++CapsuleIndex)
	{
		const FTDFluidShadowCapsule& Capsule = CachedShadowCapsules[CapsuleIndex];
		const FVector StartPlane = ToSolverPlanePoint(Capsule.StartWorld);
		const FVector EndPlane = ToSolverPlanePoint(Capsule.EndWorld);
		VolumeMID->SetVectorParameterValue(*FString::Printf(TEXT("ShadowCapsuleStart%d"), CapsuleIndex), FLinearColor(StartPlane.X, StartPlane.Y, StartPlane.Z, 0.0f));
		VolumeMID->SetVectorParameterValue(*FString::Printf(TEXT("ShadowCapsuleEnd%d"), CapsuleIndex), FLinearColor(EndPlane.X, EndPlane.Y, EndPlane.Z, 0.0f));
		CapsuleRadii[CapsuleIndex] = Capsule.RadiusCm;
	}
	VolumeMID->SetVectorParameterValue(TEXT("ShadowCapsuleRadiiA"), FLinearColor(CapsuleRadii[0], CapsuleRadii[1], CapsuleRadii[2], 0.0f));
	VolumeMID->SetVectorParameterValue(TEXT("ShadowCapsuleRadiiB"), FLinearColor(CapsuleRadii[3], CapsuleRadii[4], CapsuleRadii[5], 0.0f));
	VolumeMID->SetVectorParameterValue(TEXT("ShadowCapsuleRadiiC"), FLinearColor(CapsuleRadii[6], CapsuleRadii[7], 0.0f, 0.0f));
	VolumeMID->SetScalarParameterValue(TEXT("ShadowCapsuleCount"), static_cast<float>(CapsuleCount));
	const float CapsuleStrength = FMath::Clamp(VolumeRender.CapsuleShadowStrength, 0.0f, MaximumCapsuleShadowStrength);
	VolumeMID->SetScalarParameterValue(TEXT("ShadowCapsuleOpticalDepth"), -FMath::Loge(1.0f - CapsuleStrength));
}

void UTDFluidLiveComponent::UploadPixelsToSizedTexture(UTexture2D* TargetTexture, const TArray<FFloat16Color>& SourcePixels, int32 TextureSize)
{
	if (!TargetTexture || TargetTexture->IsStreamable() || !TargetTexture->GetResource())
	{
		return;
	}

	if (TextureSize <= 0 || SourcePixels.Num() != TextureSize * TextureSize || TargetTexture->GetSizeX() != TextureSize || TargetTexture->GetSizeY() != TextureSize)
	{
		return;
	}

	const SIZE_T ByteCount = static_cast<SIZE_T>(SourcePixels.Num()) * sizeof(FFloat16Color);
	uint8* PixelCopy = static_cast<uint8*>(FMemory::Malloc(ByteCount));
	FMemory::Memcpy(PixelCopy, SourcePixels.GetData(), ByteCount);

	FUpdateTextureRegion2D* FullRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, static_cast<uint32>(TextureSize), static_cast<uint32>(TextureSize));
	TargetTexture->UpdateTextureRegions(
		0,
		1,
		FullRegion,
		static_cast<uint32>(TextureSize) * sizeof(FFloat16Color),
		sizeof(FFloat16Color),
		PixelCopy,
		[](uint8* SourceData, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(SourceData);
			delete Regions;
		});
}
