#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidInputBrushRules.h"
#include "Algo/Compare.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFluidVolumeTransmittance.h"
#include "FluidFX/TDFluidWaterWetnessRules.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "TDFluidGpuBatch.h"
#include "TextureResource.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarTDFluidNearRipple(
	TEXT("TD.FluidFX.NearRipple"),
	-1,
	TEXT("Near ripple layer: -1 follows the preset, 0 forces it off, 1 forces it on for world-facing water layers."),
	ECVF_Default);

CSV_DECLARE_CATEGORY_EXTERN(TDFluid);

namespace
{
	constexpr int32 MaximumShadowCapsuleCount = 8;
	constexpr float MaximumCapsuleShadowStrength = 0.99f;

	void EnsureOutputTexture(TObjectPtr<UTexture2D>& InOutTexture, int32 Resolution, TextureAddress Address = TA_Clamp)
	{
		if (InOutTexture && InOutTexture->GetSizeX() == Resolution && InOutTexture->GetSizeY() == Resolution && InOutTexture->AddressX == Address)
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
		NewTexture->AddressX = Address;
		NewTexture->AddressY = Address;
		NewTexture->NeverStream = true;
		NewTexture->UpdateResource();
	}

	constexpr int32 PainterVelocityTargetSize = 4;
	const FLinearColor EmptyDensityPressureColor(0.0f, 0.5f, 0.0f, 0.0f);
	const FLinearColor EmptyVelocityColor(0.5f, 0.5f, 0.0f, 0.0f);
	const FLinearColor EmptyWaterSurfaceColor(0.0f, 0.0f, 0.5f, TDFluidGpuSolverConstants::WaterTerrainHeightLimit);

	void EnsureGpuOutputTarget(TObjectPtr<UTextureRenderTarget2D>& InOutTarget, UObject* TargetOuter, int32 TargetSize, const FLinearColor& EmptyColor, TextureAddress Address = TA_Clamp)
	{
		if (InOutTarget && InOutTarget->SizeX == TargetSize && InOutTarget->SizeY == TargetSize && InOutTarget->AddressX == Address)
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
		NewTarget->AddressX = Address;
		NewTarget->AddressY = Address;
		NewTarget->InitCustomFormat(TargetSize, TargetSize, PF_FloatRGBA, true);
		InOutTarget = NewTarget;
	}

	FTextureRenderTargetResource* ResolveRenderTargetResource(UTextureRenderTarget2D* Target)
	{
		return Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
	}

	FIntPoint LiveComponentMapWaterProbeGridUVToCell(const FVector2D& GridUV, int32 GridResolution)
	{
		const float GridX = GridUV.X * static_cast<float>(GridResolution) - 0.5f;
		const float GridY = GridUV.Y * static_cast<float>(GridResolution) - 0.5f;
		return FIntPoint(
			FMath::Clamp(FMath::RoundToInt(GridX), 0, GridResolution - 1),
			FMath::Clamp(FMath::RoundToInt(GridY), 0, GridResolution - 1));
	}

	bool LiveComponentHasWaterProbeFlag(uint32 Flags, ETDFluidGpuWaterProbeFlags Flag)
	{
		return (Flags & static_cast<uint32>(Flag)) != 0;
	}

	template <typename TStruct>
	void AssignEffectiveStruct(TStruct& Target, const TStruct& Source, FTDFluidPresetReapplyReport* InOutReport)
	{
		if (InOutReport)
		{
			InOutReport->AssignTrackedStruct(Target, Source);
			return;
		}

		Target = Source;
	}

	int32 GetPresetTuningTableGeneration()
	{
		const UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
		return TuningSubsystem ? TuningSubsystem->GetTableGeneration() : INDEX_NONE;
	}

	FTDFluidWaterPointSample MakeOutsideWaterPointSample(const FVector& WorldLocation, int32 FrameAge)
	{
		FTDFluidWaterPointSample PointSample;
		PointSample.WorldLocation = WorldLocation;
		PointSample.FrameAge = FrameAge;
		return PointSample;
	}
}

UTDFluidLiveComponent::UTDFluidLiveComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTDFluidLiveComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshEffectivePresetSettings(nullptr);
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
	FoliageBendLayer.Release();
	NearRippleLayer.Release();
	NearRippleTarget = nullptr;
	ExternalMaterialTargets.Reset();
	TileMapTargets.Reset();
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
	bIsSimulationConfigFromPreset = true;
	RefreshEffectivePresetSettings(nullptr);
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
	bIsSimulationConfigFromPreset = false;
	ApplySimulationConfigAndDetectReset(InConfig);
}

bool UTDFluidLiveComponent::ApplySimulationConfigAndDetectReset(const FTDFluidSimulationConfig& InConfig)
{
	SimulationConfig = InConfig;
	const int32 GenerationBefore = GetSimulationGeneration();
	const FTDFluidGpuBackendDecision BackendDecision = ResolveSolverBackend();
	const bool bKeepsGpuState = bUsesGpuSolver && BackendDecision.bUsesGpu && BackendDecision.Resolution == GpuSolverProxy.GetResolution();
	bool bWasStateReset = false;
	if (bKeepsGpuState)
	{
		GpuSolverProxy.UpdateSettings(SimulationConfig);
		bWasStateReset = GpuSolverProxy.WasLastSettingsUpdateReset();
	}
	else if (bUsesGpuSolver || BackendDecision.bUsesGpu)
	{
		InitializeSimulation(IsSimulationInitialized() ? GetSimulationCenterWorld() : ResolveOwnerCenterWorld());
		bWasStateReset = true;
	}
	else if (Solver.IsInitialized())
	{
		Solver.UpdateConfig(MakeCpuSolverConfig());
	}
	else
	{
		Solver.Initialize(MakeCpuSolverConfig(), ResolveOwnerCenterWorld());
		bWasStateReset = true;
	}

	ReapplyActiveObstacles();
	return bWasStateReset || GetSimulationGeneration() != GenerationBefore;
}

void UTDFluidLiveComponent::ReapplyPresetTuning(FTDFluidPresetReapplyReport& InOutReport)
{
	RefreshEffectivePresetSettings(&InOutReport);
	if (bIsSimulationConfigFromPreset && ActivePreset)
	{
		FTDFluidSimulationConfig PresetConfig = SimulationConfig;
		InOutReport.AssignTrackedStruct(PresetConfig, ActivePreset->ResolveEffectiveConfig());
		if (ApplySimulationConfigAndDetectReset(PresetConfig))
		{
			++InOutReport.StateResetActorCount;
		}
	}
	else
	{
		ReapplyActiveObstacles();
	}

	EnsureMassInputDataChannelLoadRequested();
	ReapplyMaterialLooks(InOutReport);
	ApplyPlaneRenderFlags();
	ApplyParametersToMID(InternalSurfaceMID);
	FitVolumeMeshToSimulation();
	ApplyVolumeParameters();
	ApplyParametersToExternalTargets();
}

void UTDFluidLiveComponent::ReapplyMaterialLooks(FTDFluidPresetReapplyReport& InOutReport)
{
	InOutReport.ApplyLiveMaterialLook(InternalSurfaceMID);
	InOutReport.ApplyLiveMaterialLook(VolumeMID);
	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& ExternalTarget : ExternalMaterialTargets)
	{
		InOutReport.ApplyLiveMaterialLook(ExternalTarget.Get());
	}
}

bool UTDFluidLiveComponent::HasFreshEffectivePresetSettings() const
{
	if (!bHasEffectivePresetSettings || EffectiveSettingsPreset.Get() != ActivePreset)
	{
		return false;
	}

	return !ActivePreset || ActivePreset->PresetKind == EffectiveSettingsPresetKind;
}

void UTDFluidLiveComponent::RefreshEffectivePresetSettings(FTDFluidPresetReapplyReport* InOutReport)
{
	const ETDFluidPresetKind PresetKind = ActivePreset ? ActivePreset->PresetKind : ETDFluidPresetKind::Custom;
	AssignEffectiveStruct(EffectivePlaneSettings, ActivePreset ? ActivePreset->ResolveEffectivePlaneSettings() : FTDFluidPlaneSettings(), InOutReport);
	AssignEffectiveStruct(EffectiveVolumeRender, ActivePreset ? ActivePreset->ResolveEffectiveVolumeRender() : FTDFluidVolumeRenderSettings(), InOutReport);
	AssignEffectiveStruct(InputBrushSettings, UTDFluidPresetAsset::BuildInputBrushForPresetKind(PresetKind), InOutReport);
	AssignEffectiveStruct(RuntimeOpsSettings, UTDFluidPresetAsset::BuildRuntimeOpsForPresetKind(PresetKind), InOutReport);
	AssignEffectiveStruct(SurfaceRenderSettings, UTDFluidPresetAsset::BuildSurfaceRenderForPresetKind(PresetKind), InOutReport);
	AssignEffectiveStruct(MassInputSettings, UTDFluidPresetAsset::BuildMassInputForPresetKind(PresetKind), InOutReport);
	AssignEffectiveStruct(NearRippleSettings, UTDFluidPresetAsset::BuildWaterNearRippleSettingsForPresetKind(PresetKind), InOutReport);
	NearRippleLayer.Configure(NearRippleSettings);
	EffectiveSettingsPreset = ActivePreset;
	EffectiveSettingsPresetKind = PresetKind;
	TableGeneration = GetPresetTuningTableGeneration();
	bHasEffectivePresetSettings = true;
}

FTDFluidVolumeRenderSettings UTDFluidLiveComponent::GetVolumeRenderSettings() const
{
	if (!ActivePreset)
	{
		return FTDFluidVolumeRenderSettings();
	}

	if (ActivePreset->UsesOwnPresetCopies())
	{
		return ActivePreset->VolumeRender;
	}

	return HasFreshEffectivePresetSettings() ? EffectiveVolumeRender : ActivePreset->ResolveEffectiveVolumeRender();
}

void UTDFluidLiveComponent::ResetSimulationState()
{
	PendingBrushStamps.Reset();
	FoliageBendLayer.RequestReset();
	LastProcessedPointCount = 0;
	bHasSampledTerrainRange = false;
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
	if (Stamp.RippleImpulseCm != 0.0f && IsNearRippleActive())
	{
		NearRippleLayer.AddMovingSource(FVector2D(Stamp.WorldLocation.X, Stamp.WorldLocation.Y), FVector2D(Stamp.WorldVelocity.X, Stamp.WorldVelocity.Y), Stamp.RadiusCm);
	}
}

void UTDFluidLiveComponent::QueueNearRippleIdleContact(const FVector& WorldLocation)
{
	if (!bIsSimulationAwake || !IsNearRippleActive())
	{
		return;
	}

	NearRippleLayer.AddIdleContact(FVector2D(WorldLocation.X, WorldLocation.Y));
}

void UTDFluidLiveComponent::DiscardNearRippleSources()
{
	NearRippleLayer.ClearPendingSources();
}

bool UTDFluidLiveComponent::IsNearRippleActive() const
{
	if (!FTDFluidNearRippleLayer::ResolveEnabled(CVarTDFluidNearRipple.GetValueOnGameThread(), NearRippleSettings.bEnabled))
	{
		return false;
	}

	return SimulationConfig.bEnableWaterLayer
		&& IsWaterMaterialActive()
		&& PlaneBasis.IsWorldFacing()
		&& FApp::CanEverRender()
		&& GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5;
}

UTexture* UTDFluidLiveComponent::GetNearRippleTexture() const
{
	return NearRippleTarget.Get();
}

void UTDFluidLiveComponent::SubmitNearRippleStep(const FVector2D& CenterWorldXY, float DeltaTime)
{
	if (!IsNearRippleActive())
	{
		NearRippleLayer.ClearPendingSources();
		if (NearRippleTarget)
		{
			NearRippleLayer.Release();
			NearRippleTarget = nullptr;
		}
		return;
	}

	const int32 NearRippleResolution = NearRippleLayer.GetResolution();
	if (!NearRippleTarget || NearRippleTarget->SizeX != NearRippleResolution)
	{
		UTextureRenderTarget2D* NewTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		NewTarget->RenderTargetFormat = RTF_RGBA16f;
		NewTarget->ClearColor = FLinearColor::Transparent;
		NewTarget->bAutoGenerateMips = false;
		NewTarget->bSupportsUAV = true;
		NewTarget->bCanCreateUAV = true;
		NewTarget->Filter = TF_Bilinear;
		NewTarget->AddressX = TA_Wrap;
		NewTarget->AddressY = TA_Wrap;
		NewTarget->InitCustomFormat(NearRippleResolution, NearRippleResolution, PF_FloatRGBA, true);
		NearRippleTarget = NewTarget;
	}

	FTDFluidNearRippleMaskInputs Masks;
	Masks.WaterSurfaceTexture = GetWaterSurfaceOutputTexture();
	Masks.AuxTexture = IsObstacleDistanceAuxActive() ? GetAuxTexture() : nullptr;
	Masks.MainWindowCenterCm = GetSimulationCenterWorld();
	Masks.MainWindowExtentCm = GetSimulationExtentCm();
	Masks.WaterDepthReferenceCm = SimulationConfig.WaterDepthReferenceCm;
	Masks.ObstacleDistanceOutputRangeCm = FMath::Clamp(SimulationConfig.ObstacleDistanceOutputRangeCm, 20.0f, 2000.0f);
	Masks.bHasObstacleDistanceAux = Masks.AuxTexture != nullptr;
	NearRippleLayer.SubmitStep(Masks, CenterWorldXY, DeltaTime, *NearRippleTarget);
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
		Stamp.DensityStrength = InputBrushSettings.ParticleDensityStrength;
		Stamp.VelocityStrength = 1.0f;
		Stamp.Channel = Channel;
		Stamp.Source = ETDFluidBrushSource::PointArray;
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
	if (bIsSimulationAwake)
	{
		FoliageBendLayer.RequestReset();
	}

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
	FVector2D PlanePoint = ProjectWorldToSolverPlane(WorldLocation);
	if (IsAreaWrapped())
	{
		PlanePoint = WrapPlanePointIntoWindow(PlanePoint, ResolveWrapWindowMinimumCm(), GetSimulationExtentCm());
	}

	if (!bUsesGpuSolver)
	{
		return Solver.SampleAtWorldPosition(PlanePoint, OutSample);
	}

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

	bHasPendingObstacleUpdate = bHasPendingObstacleUpdate || HasAnySolverObstacles();
	return true;
}

FTDFluidPlaneSettings UTDFluidLiveComponent::GetPlaneSettings() const
{
	if (!ActivePreset)
	{
		return FTDFluidPlaneSettings();
	}

	if (ActivePreset->UsesOwnPresetCopies())
	{
		return ActivePreset->PlaneSettings;
	}

	return HasFreshEffectivePresetSettings() ? EffectivePlaneSettings : ActivePreset->ResolveEffectivePlaneSettings();
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
	if (!HasFreshEffectivePresetSettings())
	{
		RefreshEffectivePresetSettings(nullptr);
	}

	if (!bIsSimulationAwake || DeltaTime <= 0.0f)
	{
		return false;
	}

	SyncSolverBackend(DesiredCenterWorld);
	RefreshFoliageBendStep(DeltaTime);
	if (bUsesGpuSolver)
	{
		return PrepareGpuSimulationStep(DeltaTime, DesiredCenterWorld, bAllowSlidingShift);
	}

	if (!Solver.IsInitialized())
	{
		Solver.Initialize(MakeCpuSolverConfig(), DesiredCenterWorld);
	}

	const bool bDidShiftWindow = AdvanceCpuSolverWindow(DeltaTime, DesiredCenterWorld, bAllowSlidingShift);

	if ((bDidShiftWindow || bHasPendingObstacleUpdate) && HasAnySolverObstacles())
	{
		RasterizeActiveObstacles();
		bHasPendingObstacleUpdate = false;
	}

	if (!PlaneBasis.IsWorldFacing())
	{
		Solver.SetUniformTerrainSlope(FVector2D::ZeroVector);
	}

	Solver.SetPlaneUpDirection(PlaneBasis.GetPlaneUpDirection());
	ApplyGroundSlabToPendingStamps();
	ApplyInputBrushRulesToPendingStamps();
	ConvertPendingStampsToSolverPlane();
	WrapPendingStampsIntoWindow();
	PrioritizeAndClampPendingStamps();
	LastProcessedPointCount = PendingBrushStamps.Num();
	if (IsAreaWrapped())
	{
		AppendWrappedStampGhosts(PendingBrushStamps, ResolveWrapWindowMinimumCm(), GetSimulationExtentCm());
	}
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
	AdvanceFoliageBendCpu(DeltaTime);
	PackAuxAndTransmittanceOutputs();
}

void UTDFluidLiveComponent::FinishSimulationStep()
{
	if (!IsSimulationInitialized())
	{
		return;
	}

	SubmitFoliageBendGpu();
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

	RequestedMassInputDataChannelPath = MassInputSettings.bReadDataChannel ? MassInputSettings.DataChannel.ToSoftObjectPath() : FSoftObjectPath();
	if (RequestedMassInputDataChannelPath.IsValid())
	{
		AssetsToLoad.Add(RequestedMassInputDataChannelPath);
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

	NiagaraLookGenerationByComponent.Remove(FObjectKey(OwnedNiagaraComponent));
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
		UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*InternalSurfaceMID);
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
		UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*VolumeMID);
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

	const FTDFluidVolumeRenderSettings VolumeRender = GetVolumeRenderSettings();
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

void UTDFluidLiveComponent::SetTerrainCollisionObstacles(const TArray<FTDFluidSdfObstacle>& InObstacles)
{
	if (InObstacles.IsEmpty() && TerrainCollisionObstacles.IsEmpty())
	{
		return;
	}
	TerrainCollisionObstacles = InObstacles;
	bHasPendingObstacleUpdate = true;
	if (!IsSimulationInitialized())
	{
		return;
	}
	RasterizeActiveObstacles();
	bHasPendingObstacleUpdate = false;
}

bool UTDFluidLiveComponent::HasAnySolverObstacles() const
{
	return !ActiveObstacles.IsEmpty() || !TerrainCollisionObstacles.IsEmpty() || !CollectedObstacles.IsEmpty();
}

void UTDFluidLiveComponent::BuildWorldSolverObstacles(TArray<FTDFluidSdfObstacle>& OutObstacles) const
{
	OutObstacles.Reset(ActiveObstacles.Num() + TerrainCollisionObstacles.Num() + CollectedObstacles.Num());
	OutObstacles.Append(ActiveObstacles);
	OutObstacles.Append(TerrainCollisionObstacles);
	OutObstacles.Append(CollectedObstacles);
}

void UTDFluidLiveComponent::ReapplyActiveObstacles()
{
	if (!HasAnySolverObstacles())
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
	if (IsAreaWrapped())
	{
		TArray<FTDFluidSdfObstacle> WrappedObstacles;
		if (PlaneBasis.IsWorldFacing())
		{
			BuildWorldSolverObstacles(WrappedObstacles);
		}
		else
		{
			BuildSolverPlaneObstacles(WrappedObstacles);
		}
		WrapObstaclesIntoWindow(WrappedObstacles);
		if (bUsesGpuSolver)
		{
			GpuSolverProxy.SetObstacles(WrappedObstacles);
			return;
		}

		Solver.RasterizeSdfObstacles(WrappedObstacles);
		return;
	}

	if (PlaneBasis.IsWorldFacing() && (!TerrainCollisionObstacles.IsEmpty() || !CollectedObstacles.IsEmpty()))
	{
		TArray<FTDFluidSdfObstacle> WorldObstacles;
		BuildWorldSolverObstacles(WorldObstacles);
		if (bUsesGpuSolver)
		{
			GpuSolverProxy.SetObstacles(WorldObstacles);
			return;
		}

		Solver.RasterizeSdfObstacles(WorldObstacles);
		return;
	}

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
	OutObstacles.Reset(ActiveObstacles.Num() + CollectedObstacles.Num());

	const FTDFluidPlaneSettings CurrentPlaneSettings = GetPlaneSettings();
	for (const TArray<FTDFluidSdfObstacle>* SourceObstacles : { &ActiveObstacles, &CollectedObstacles })
	{
		for (const FTDFluidSdfObstacle& Obstacle : *SourceObstacles)
		{
			FTDFluidSdfObstacle PlaneObstacle;
			if (ProjectObstacleToPlane(PlaneBasis, PlaneDepthCm, CurrentPlaneSettings, Obstacle, PlaneObstacle))
			{
				OutObstacles.Add(PlaneObstacle);
			}
		}
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
	FoliageBendLayer.RequestReset();
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

void UTDFluidLiveComponent::ApplyInputBrushRulesToPendingStamps()
{
	FTDFluidInputBrushRules::ApplyToStamps(PendingBrushStamps, InputBrushSettings, PlaneBasis, ResolveBrushSurfaceDepthCm());
}

double UTDFluidLiveComponent::ResolveBrushSurfaceDepthCm() const
{
	if (!PlaneBasis.IsWorldFacing())
	{
		return PlaneDepthCm;
	}

	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetActorLocation().Z : 0.0;
}

bool UTDFluidLiveComponent::NeedsStampGroundElevation() const
{
	if (!PlaneBasis.IsWorldFacing())
	{
		return false;
	}
	return InputBrushSettings.GroundSlabThicknessCm > 0.0f || InputBrushSettings.BrushHeightFadeDistanceCm > 0.0f;
}

void UTDFluidLiveComponent::ApplyGroundSlabToPendingStamps()
{
	const double SlabThicknessCm = InputBrushSettings.GroundSlabThicknessCm;
	if (SlabThicknessCm <= 0.0 || !PlaneBasis.IsWorldFacing())
	{
		return;
	}

	const double FallbackGroundCm = ResolveBrushSurfaceDepthCm();
	int32 KeptCount = 0;
	for (int32 Index = 0; Index < PendingBrushStamps.Num(); ++Index)
	{
		const FTDFluidBrushStamp Stamp = PendingBrushStamps[Index];
		const double GroundCm = Stamp.bHasGroundElevation ? Stamp.GroundElevationCm : FallbackGroundCm;
		if (FMath::Abs(PlaneBasis.ProjectDepth(Stamp.WorldLocation) - GroundCm) > SlabThicknessCm)
		{
			continue;
		}
		PendingBrushStamps[KeptCount] = Stamp;
		++KeptCount;
	}

	PendingBrushStamps.SetNum(KeptCount, EAllowShrinking::No);
}

void UTDFluidLiveComponent::SetWetnessSourceComponent(UTDFluidLiveComponent* SourceComponent)
{
	WetnessSourceComponent = SourceComponent;
	if (!SourceComponent && !bHasWetnessSourceBinding)
	{
		return;
	}

	bHasWetnessSourceBinding = true;
	if (InternalSurfaceMID)
	{
		ApplyTrackParametersToMID(*InternalSurfaceMID);
	}
	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& ExternalTarget : ExternalMaterialTargets)
	{
		if (UMaterialInstanceDynamic* TargetMID = ExternalTarget.Get())
		{
			ApplyTrackParametersToMID(*TargetMID);
		}
	}
}

void UTDFluidLiveComponent::ApplyTrackParametersToMID(UMaterialInstanceDynamic& TargetMID) const
{
	if (SimulationConfig.SimMode == ETDFluidSimMode::SimplePainter)
	{
		TargetMID.SetScalarParameterValue(TEXT("TrackBankFromOutput"), SimulationConfig.TrackBankStrength > 0.0f ? 1.0f : 0.0f);
	}
	if (!bHasWetnessSourceBinding)
	{
		return;
	}

	const UTDFluidLiveComponent* SourceComponent = WetnessSourceComponent.Get();
	UTexture* WetSourceTexture = SourceComponent && SourceComponent->IsWaterMaterialActive() ? SourceComponent->GetWaterSurfaceOutputTexture() : nullptr;
	if (!WetSourceTexture)
	{
		TargetMID.SetScalarParameterValue(TEXT("WetSourceActive"), 0.0f);
		return;
	}

	const FVector2D SourceCenter = SourceComponent->GetSimulationCenterWorld();
	const FVector2D SourceExtent = SourceComponent->GetSimulationExtentCm();
	TargetMID.SetTextureParameterValue(TEXT("WetSourceTexture"), WetSourceTexture);
	TargetMID.SetVectorParameterValue(TEXT("WetSourceWindow"), FLinearColor(SourceCenter.X, SourceCenter.Y, SourceExtent.X, SourceExtent.Y));
	TargetMID.SetScalarParameterValue(TEXT("WetSourceDepthReferenceCm"), SourceComponent->SimulationConfig.WaterDepthReferenceCm);
	TargetMID.SetScalarParameterValue(TEXT("WetSourceActive"), 1.0f);
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
		const FVector2D PlaneShapeAxis = PlaneBasis.ProjectVector(Stamp.ShapeMajorAxis);
		Stamp.WorldLocation = FVector(PlaneLocation.X, PlaneLocation.Y, 0.0);
		Stamp.WorldVelocity = FVector(PlaneVelocity.X, PlaneVelocity.Y, 0.0);
		Stamp.ShapeMajorAxis = FVector(PlaneShapeAxis.X, PlaneShapeAxis.Y, 0.0);
		PendingBrushStamps[KeptCount] = Stamp;
		++KeptCount;
	}

	PendingBrushStamps.SetNum(KeptCount, EAllowShrinking::No);
}


FVector2D UTDFluidLiveComponent::ResolveWrapWindowMinimumCm() const
{
	return GetSimulationCenterWorld() - 0.5 * GetSimulationExtentCm();
}

FVector2D UTDFluidLiveComponent::WrapPlanePointIntoWindow(const FVector2D& PlanePoint, const FVector2D& WindowMinimumCm, const FVector2D& WindowExtentCm)
{
	FVector2D WrappedPoint = PlanePoint;
	if (WindowExtentCm.X > 0.0)
	{
		const double OffsetX = PlanePoint.X - WindowMinimumCm.X;
		WrappedPoint.X = WindowMinimumCm.X + (OffsetX - WindowExtentCm.X * FMath::FloorToDouble(OffsetX / WindowExtentCm.X));
	}
	if (WindowExtentCm.Y > 0.0)
	{
		const double OffsetY = PlanePoint.Y - WindowMinimumCm.Y;
		WrappedPoint.Y = WindowMinimumCm.Y + (OffsetY - WindowExtentCm.Y * FMath::FloorToDouble(OffsetY / WindowExtentCm.Y));
	}
	return WrappedPoint;
}

namespace
{
	int32 CollectWrappedGhostOffsets(const FVector2D& CenterCm, double ReachCm, const FVector2D& WindowMinimumCm, const FVector2D& WindowExtentCm, FVector2D OutOffsets[3])
	{
		double OffsetsX[2] = { 0.0, 0.0 };
		double OffsetsY[2] = { 0.0, 0.0 };
		int32 CountX = 1;
		int32 CountY = 1;
		if (CenterCm.X <= WindowMinimumCm.X + ReachCm)
		{
			OffsetsX[CountX++] = WindowExtentCm.X;
		}
		else if (CenterCm.X >= WindowMinimumCm.X + WindowExtentCm.X - ReachCm)
		{
			OffsetsX[CountX++] = -WindowExtentCm.X;
		}
		if (CenterCm.Y <= WindowMinimumCm.Y + ReachCm)
		{
			OffsetsY[CountY++] = WindowExtentCm.Y;
		}
		else if (CenterCm.Y >= WindowMinimumCm.Y + WindowExtentCm.Y - ReachCm)
		{
			OffsetsY[CountY++] = -WindowExtentCm.Y;
		}

		int32 OffsetCount = 0;
		for (int32 IndexY = 0; IndexY < CountY; ++IndexY)
		{
			for (int32 IndexX = 0; IndexX < CountX; ++IndexX)
			{
				if (IndexX == 0 && IndexY == 0)
				{
					continue;
				}
				OutOffsets[OffsetCount++] = FVector2D(OffsetsX[IndexX], OffsetsY[IndexY]);
			}
		}
		return OffsetCount;
	}
}

void UTDFluidLiveComponent::AppendWrappedStampGhosts(TArray<FTDFluidBrushStamp>& InOutStamps, const FVector2D& WindowMinimumCm, const FVector2D& WindowExtentCm)
{
	if (WindowExtentCm.X <= 0.0 || WindowExtentCm.Y <= 0.0)
	{
		return;
	}

	const int32 RealStampCount = InOutStamps.Num();
	for (int32 StampIndex = 0; StampIndex < RealStampCount; ++StampIndex)
	{
		const FTDFluidBrushStamp RealStamp = InOutStamps[StampIndex];
		FVector2D GhostOffsets[3];
		const int32 GhostCount = CollectWrappedGhostOffsets(FVector2D(RealStamp.WorldLocation.X, RealStamp.WorldLocation.Y), RealStamp.RadiusCm, WindowMinimumCm, WindowExtentCm, GhostOffsets);
		for (int32 GhostIndex = 0; GhostIndex < GhostCount; ++GhostIndex)
		{
			FTDFluidBrushStamp& GhostStamp = InOutStamps.Add_GetRef(RealStamp);
			GhostStamp.WorldLocation.X += GhostOffsets[GhostIndex].X;
			GhostStamp.WorldLocation.Y += GhostOffsets[GhostIndex].Y;
		}
	}
}

void UTDFluidLiveComponent::AppendWrappedObstacleGhosts(TArray<FTDFluidSdfObstacle>& InOutObstacles, const FVector2D& WindowMinimumCm, const FVector2D& WindowExtentCm)
{
	if (WindowExtentCm.X <= 0.0 || WindowExtentCm.Y <= 0.0)
	{
		return;
	}

	const int32 RealObstacleCount = InOutObstacles.Num();
	for (int32 ObstacleIndex = 0; ObstacleIndex < RealObstacleCount; ++ObstacleIndex)
	{
		const FTDFluidSdfObstacle RealObstacle = InOutObstacles[ObstacleIndex];
		const double ReachCm = RealObstacle.bIsBox ? RealObstacle.BoxHalfExtentsCm.Size() : RealObstacle.RadiusCm;
		FVector2D GhostOffsets[3];
		const int32 GhostCount = CollectWrappedGhostOffsets(FVector2D(RealObstacle.WorldCenter.X, RealObstacle.WorldCenter.Y), ReachCm, WindowMinimumCm, WindowExtentCm, GhostOffsets);
		for (int32 GhostIndex = 0; GhostIndex < GhostCount; ++GhostIndex)
		{
			FTDFluidSdfObstacle& GhostObstacle = InOutObstacles.Add_GetRef(RealObstacle);
			GhostObstacle.WorldCenter.X += GhostOffsets[GhostIndex].X;
			GhostObstacle.WorldCenter.Y += GhostOffsets[GhostIndex].Y;
		}
	}
}

void UTDFluidLiveComponent::WrapPendingStampsIntoWindow()
{
	if (!IsAreaWrapped())
	{
		return;
	}

	const FVector2D WindowMinimumCm = ResolveWrapWindowMinimumCm();
	const FVector2D WindowExtentCm = GetSimulationExtentCm();
	for (FTDFluidBrushStamp& Stamp : PendingBrushStamps)
	{
		const FVector2D WrappedPoint = WrapPlanePointIntoWindow(FVector2D(Stamp.WorldLocation.X, Stamp.WorldLocation.Y), WindowMinimumCm, WindowExtentCm);
		Stamp.WorldLocation.X = WrappedPoint.X;
		Stamp.WorldLocation.Y = WrappedPoint.Y;
	}
}

void UTDFluidLiveComponent::WrapObstaclesIntoWindow(TArray<FTDFluidSdfObstacle>& InOutObstacles) const
{
	const FVector2D WindowMinimumCm = ResolveWrapWindowMinimumCm();
	const FVector2D WindowExtentCm = GetSimulationExtentCm();
	for (FTDFluidSdfObstacle& Obstacle : InOutObstacles)
	{
		const FVector2D WrappedPoint = WrapPlanePointIntoWindow(FVector2D(Obstacle.WorldCenter.X, Obstacle.WorldCenter.Y), WindowMinimumCm, WindowExtentCm);
		Obstacle.WorldCenter.X = WrappedPoint.X;
		Obstacle.WorldCenter.Y = WrappedPoint.Y;
	}
	AppendWrappedObstacleGhosts(InOutObstacles, WindowMinimumCm, WindowExtentCm);
}

bool UTDFluidLiveComponent::IsAreaWrapped() const
{
	return SimulationConfig.AreaAddressMode == ETDFluidAreaAddressMode::Wrap;
}

TextureAddress UTDFluidLiveComponent::ResolveOutputTextureAddress() const
{
	return IsAreaWrapped() ? TA_Wrap : TA_Clamp;
}

FVector2D UTDFluidLiveComponent::ComputeTileMapOriginCm(const FVector2D& AnchorCm, const FVector2D& PeriodCm, const FVector2D& ObserverCm)
{
	FVector2D OriginCm = AnchorCm;
	if (PeriodCm.X > 0.0)
	{
		OriginCm.X = AnchorCm.X + PeriodCm.X * FMath::FloorToDouble((ObserverCm.X - AnchorCm.X) / PeriodCm.X);
	}
	if (PeriodCm.Y > 0.0)
	{
		OriginCm.Y = AnchorCm.Y + PeriodCm.Y * FMath::FloorToDouble((ObserverCm.Y - AnchorCm.Y) / PeriodCm.Y);
	}
	return OriginCm;
}

void UTDFluidLiveComponent::RegisterTileMapTarget(UMaterialInstanceDynamic* TargetMID)
{
	if (!TargetMID)
	{
		return;
	}

	TileMapTargets.RemoveAll([](const FTDFluidTileMapTarget& Target) { return !Target.TargetMID.IsValid(); });
	if (TileMapTargets.ContainsByPredicate([TargetMID](const FTDFluidTileMapTarget& Target) { return Target.TargetMID.Get() == TargetMID; }))
	{
		return;
	}

	FTDFluidTileMapTarget& NewTarget = TileMapTargets.AddDefaulted_GetRef();
	NewTarget.TargetMID = TargetMID;
}

FVector2D UTDFluidLiveComponent::ResolveTileMapObserverWorld(const FVector2D& FallbackWorldCm) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return FallbackWorldCm;
	}

	if (PlayerController->PlayerCameraManager)
	{
		const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
		return FVector2D(CameraLocation.X, CameraLocation.Y);
	}

	if (const APawn* Pawn = PlayerController->GetPawn())
	{
		const FVector PawnLocation = Pawn->GetActorLocation();
		return FVector2D(PawnLocation.X, PawnLocation.Y);
	}

	return FallbackWorldCm;
}

void UTDFluidLiveComponent::ApplyTileMapTargets()
{
	TileMapTargets.RemoveAll([](const FTDFluidTileMapTarget& Target) { return !Target.TargetMID.IsValid(); });
	if (TileMapTargets.IsEmpty())
	{
		return;
	}

	UTexture* SourceTexture = IsAreaWrapped() && PlaneBasis.IsWorldFacing() && IsSimulationInitialized() ? GetDensityPressureTexture() : nullptr;
	if (!SourceTexture)
	{
		for (FTDFluidTileMapTarget& Target : TileMapTargets)
		{
			if (!Target.bWasActive)
			{
				continue;
			}
			Target.TargetMID->SetVectorParameterValue(TEXT("TileMap1PeriodCm"), FLinearColor::Transparent);
			Target.bWasActive = false;
		}
		return;
	}

	const FVector2D PeriodCm = GetSimulationExtentCm();
	const FVector2D AnchorCm = ResolveWrapWindowMinimumCm();
	const FVector2D OriginCm = ComputeTileMapOriginCm(AnchorCm, PeriodCm, ResolveTileMapObserverWorld(AnchorCm));
	const FLinearColor OriginValue(static_cast<float>(OriginCm.X), static_cast<float>(OriginCm.Y), 0.0f, 0.0f);
	const FLinearColor PeriodValue(static_cast<float>(PeriodCm.X), static_cast<float>(PeriodCm.Y), static_cast<float>(GetSimulationResolution()), 0.0f);
	for (FTDFluidTileMapTarget& Target : TileMapTargets)
	{
		UMaterialInstanceDynamic* TargetMID = Target.TargetMID.Get();
		if (Target.LastTexture.Get() != SourceTexture)
		{
			TargetMID->SetTextureParameterValue(TEXT("TileMap1Texture"), SourceTexture);
			Target.LastTexture = SourceTexture;
		}
		TargetMID->SetVectorParameterValue(TEXT("TileMap1OriginCm"), OriginValue);
		TargetMID->SetVectorParameterValue(TEXT("TileMap1PeriodCm"), PeriodValue);
		Target.bWasActive = true;
	}
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

	const TextureAddress OutputAddress = ResolveOutputTextureAddress();
	EnsureOutputTexture(DensityPressureTexture, Res, OutputAddress);
	EnsureOutputTexture(VelocityTexture, Res, OutputAddress);
	if (IsAuxOutputActive())
	{
		const UTexture2D* PreviousAuxTexture = AuxTexture.Get();
		EnsureOutputTexture(AuxTexture, Res, OutputAddress);
		if (AuxTexture.Get() != PreviousAuxTexture)
		{
			PackedAuxObstacleGeneration = INDEX_NONE;
		}
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
	if (!IsWaterMaterialActive())
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
	ApplyTileMapTargets();

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

	ApplyNiagaraLookIfStale(*OwnedNiagaraComponent);
	DriveNiagaraComponent(*OwnedNiagaraComponent, *OwnerActor);
}

void UTDFluidLiveComponent::ApplyNiagaraLookIfStale(UNiagaraComponent& NiagaraComp)
{
	const UFXSystemAsset* NiagaraSystem = NiagaraComp.GetFXSystemAsset();
	const int32 CurrentTableGeneration = GetPresetTuningTableGeneration();
	if (!NiagaraSystem || CurrentTableGeneration == INDEX_NONE)
	{
		return;
	}

	int32& AppliedTableGeneration = NiagaraLookGenerationByComponent.FindOrAdd(FObjectKey(&NiagaraComp), INDEX_NONE);
	if (AppliedTableGeneration == CurrentTableGeneration)
	{
		return;
	}

	AppliedTableGeneration = CurrentTableGeneration;
	const FTDFluidNiagaraLookEntry* Look = UTDFluidPresetTuningSubsystem::FindNiagaraLook(NiagaraSystem->GetPackage()->GetFName());
	if (!Look)
	{
		return;
	}

	for (const TPair<FName, float>& Variable : Look->Floats)
	{
		NiagaraComp.SetVariableFloat(Variable.Key, Variable.Value);
	}
	for (const TPair<FName, int32>& Variable : Look->Ints)
	{
		NiagaraComp.SetVariableInt(Variable.Key, Variable.Value);
	}
	for (const TPair<FName, bool>& Variable : Look->Bools)
	{
		NiagaraComp.SetVariableBool(Variable.Key, Variable.Value);
	}
	for (const TPair<FName, FVector2D>& Variable : Look->Vectors2)
	{
		NiagaraComp.SetVariableVec2(Variable.Key, Variable.Value);
	}
	for (const TPair<FName, FVector>& Variable : Look->Vectors3)
	{
		NiagaraComp.SetVariableVec3(Variable.Key, Variable.Value);
	}
	for (const TPair<FName, FLinearColor>& Variable : Look->Colors)
	{
		NiagaraComp.SetVariableLinearColor(Variable.Key, Variable.Value);
	}
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
	if (UTexture* WaterSurfaceOutput = GetWaterSurfaceOutputTexture())
	{
		NiagaraComp.SetVariableTexture(FName(TEXT("User.WaterSurfaceTexture")), WaterSurfaceOutput);
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

	const bool bIsCacheStale = FMath::Abs(World->GetTimeSeconds() - LastNiagaraCacheRefreshSeconds) >= RuntimeOpsSettings.NiagaraCacheRefreshIntervalSeconds;
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

		ApplyNiagaraLookIfStale(*NiagaraComp);
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
	TargetMID->SetScalarParameterValue(TEXT("SimAreaWrapped"), IsAreaWrapped() && bIsWorldFacing ? 1.0f : 0.0f);

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
	TargetMID->SetScalarParameterValue(TEXT("AuxHeatActive"), AuxOutput && IsHeatAuxOutputActive() ? 1.0f : 0.0f);
	TargetMID->SetScalarParameterValue(TEXT("AuxFoliageBendActive"), AuxOutput && bIsFoliageBendStepEnabled && FoliageBendLayer.HasOutput() ? 1.0f : 0.0f);
	ApplyTrackParametersToMID(*TargetMID);

	if (!IsWaterMaterialActive())
	{
		return;
	}

	const FTDFluidSimulationConfig& SolverConfig = bUsesGpuSolver ? SimulationConfig : Solver.GetConfig();

	if (UTexture* WaterSurfaceOutput = GetWaterSurfaceOutputTexture())
	{
		TargetMID->SetTextureParameterValue(TEXT("WaterSurfaceTexture"), WaterSurfaceOutput);
	}
	const double WaterTimeSeconds = bUsesGpuSolver ? GpuSolverProxy.GetSimulationTimeSeconds() : Solver.GetSimulationTimeSeconds();
	const float CoastWavePhase = FTDFluidWaterWetnessRules::ComputeCoastWavePhase(WaterTimeSeconds, SolverConfig.CoastWavePeriodSeconds);
	TargetMID->SetScalarParameterValue(TEXT("WaterDepthReferenceCm"), SolverConfig.WaterDepthReferenceCm);
	TargetMID->SetScalarParameterValue(TEXT("RippleHeightReferenceCm"), SolverConfig.RippleHeightReferenceCm);
	TargetMID->SetScalarParameterValue(TEXT("VelocitySpeedReferenceCmPerSec"), FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec());
	TargetMID->SetScalarParameterValue(TEXT("FluidWaterLayerActive"), bIsSimulationAwake ? WaterLayerDisplayAlpha : 0.0f);
	TargetMID->SetScalarParameterValue(TEXT("CoastWavePhase"), CoastWavePhase);
	TargetMID->SetScalarParameterValue(TEXT("CoastWaveDepthSpacingCm"), SolverConfig.CoastWaveDepthSpacingCm);
	TargetMID->SetScalarParameterValue(TEXT("CoastWaveMaxDepthCm"), SolverConfig.CoastWaveMaxDepthCm);
	TargetMID->SetScalarParameterValue(TEXT("SwashHeightCm"), SolverConfig.SwashHeightCm);
	TargetMID->SetScalarParameterValue(TEXT("TerrainReferenceZCm"), TerrainReferenceZCm);
	TargetMID->SetScalarParameterValue(TEXT("AuxObstacleDistanceActive"), AuxOutput && IsObstacleDistanceAuxActive() ? 1.0f : 0.0f);
	TargetMID->SetScalarParameterValue(TEXT("ObstacleDistanceRangeCm"), FMath::Clamp(SolverConfig.ObstacleDistanceOutputRangeCm, 20.0f, 2000.0f));

	UTexture* NearRippleOutput = IsNearRippleActive() ? GetNearRippleTexture() : nullptr;
	if (NearRippleOutput)
	{
		TargetMID->SetTextureParameterValue(TEXT("NearRippleTexture"), NearRippleOutput);
	}
	const FVector2D NearRippleCenter = NearRippleLayer.GetSnappedCenterWorld();
	TargetMID->SetVectorParameterValue(TEXT("NearRippleWindow"), FLinearColor(NearRippleCenter.X, NearRippleCenter.Y, NearRippleLayer.GetExtentCm(), FMath::Clamp(NearRippleSettings.EdgeFadeRatio, 0.02f, 0.4f)));
	TargetMID->SetScalarParameterValue(TEXT("NearRippleActive"), NearRippleOutput ? 1.0f : 0.0f);
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

bool UTDFluidLiveComponent::TryGetSampledTerrainRangeCm(float& OutMinCm, float& OutMaxCm) const
{
	if (!bHasSampledTerrainRange)
	{
		return false;
	}

	OutMinCm = SampledTerrainMinCm;
	OutMaxCm = SampledTerrainMaxCm;
	return true;
}

void UTDFluidLiveComponent::SetTerrainElevationAtCell(int32 CellX, int32 CellY, float ElevationCm)
{
	if (FMath::IsFinite(ElevationCm))
	{
		SampledTerrainMinCm = bHasSampledTerrainRange ? FMath::Min(SampledTerrainMinCm, ElevationCm) : ElevationCm;
		SampledTerrainMaxCm = bHasSampledTerrainRange ? FMath::Max(SampledTerrainMaxCm, ElevationCm) : ElevationCm;
		bHasSampledTerrainRange = true;
	}

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
	FTextureRenderTargetResource* WaterSurfaceOutput = nullptr;
	int32 WaterProbeFirstSlot = 0;
	if (GpuSolverProxy.UsesWaterLayer())
	{
		WaterSurfaceOutput = ResolveRenderTargetResource(GpuWaterSurfaceTarget.Get());
		GpuSolverProxy.SetWaterTerrainReferenceZCm(TerrainReferenceZCm);
		{
			CSV_SCOPED_TIMING_STAT(TDFluid, WaterProbeMapping);
			SubmitGpuWaterProbePlanePoints();
		}
		for (const FTDFluidGpuActorInput& PreviousActorInput : InOutFrameInputs.Actors)
		{
			WaterProbeFirstSlot += PreviousActorInput.WaterProbeCells.Num();
		}
	}

	FTDFluidGpuActorInput ActorInput;
	if (!GpuSolverProxy.GatherStep(InOutFrameInputs.FrameNumber, InOutFrameInputs.Actors.Num(), DensityPressureOutput, VelocityOutput, ActorInput, AuxOutput, TransmittanceOutput, WaterSurfaceOutput, WaterProbeFirstSlot))
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
	BackendQuery.bIsSimplePainter = SimulationConfig.SimMode == ETDFluidSimMode::SimplePainter;
	return FTDFluidGpuBackendSelection::Decide(BackendQuery, FTDFluidGpuBackendSelection::CaptureEnvironment());
}

void UTDFluidLiveComponent::InitializeSimulation(const FVector2D& CenterWorld)
{
	bHasSampledTerrainRange = false;
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
			GpuWaterSurfaceTarget = nullptr;
			bUsesGpuSolver = false;
		}

		Solver.Initialize(MakeCpuSolverConfig(), CenterWorld);
		if (bHasWaterInputsMissingOnCpu)
		{
			Solver.SetFlowGuides(LastWaterGuides);
			Solver.SetWaterRegions(LastWaterRegions);
			bHasWaterInputsMissingOnCpu = false;
		}
		return;
	}

	bUsesGpuSolver = true;
	DensityPressureTexture = nullptr;
	VelocityTexture = nullptr;
	WaterSurfaceTexture = nullptr;
	AuxTexture = nullptr;
	TransmittanceTexture = nullptr;
	GpuSolverProxy.Initialize(SimulationConfig, BackendDecision.Resolution, CenterWorld);
	if (bHasLastWaterInputs && GpuSolverProxy.UsesWaterLayer())
	{
		GpuSolverProxy.SetWaterInputs(LastWaterGuides, LastWaterRegions);
	}
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

namespace TDFluidLiveComponentMassInput
{
	bool DoesPresetWriteSelectiveDensity(const UTDFluidPresetAsset* Preset)
	{
		return Preset && (Preset->DefaultChannel == ETDFluidBrushChannel::ChannelG_Blood || Preset->DefaultChannel == ETDFluidBrushChannel::ChannelB_Fire);
	}
}

bool UTDFluidLiveComponent::PrepareGpuSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift)
{
	const bool bDidShiftWindow = AdvanceGpuSolverWindow(DeltaTime, DesiredCenterWorld, bAllowSlidingShift);
	if ((bDidShiftWindow || bHasPendingObstacleUpdate) && HasAnySolverObstacles())
	{
		RasterizeActiveObstacles();
		bHasPendingObstacleUpdate = false;
	}

	ApplyGroundSlabToPendingStamps();
	ApplyInputBrushRulesToPendingStamps();
	ConvertPendingStampsToSolverPlane();
	WrapPendingStampsIntoWindow();
	LastProcessedPointCount = FMath::Min(PendingBrushStamps.Num(), FMath::Clamp(SimulationConfig.GpuMaxStampsPerTick, 1, TDFluidGpuSolverConstants::MaximumStampsPerTick));
	if (IsAreaWrapped())
	{
		AppendWrappedStampGhosts(PendingBrushStamps, ResolveWrapWindowMinimumCm(), GetSimulationExtentCm());
	}
	GpuSolverProxy.PrepareStep(DeltaTime, PendingBrushStamps, PlaneBasis.GetPlaneUpDirection(), PlaneBasis.IsWorldFacing());
	PendingBrushStamps.Reset();
	GpuSolverProxy.SetMassInputAcceptance(AcceptsMassInput(), TDFluidLiveComponentMassInput::DoesPresetWriteSelectiveDensity(ActivePreset));
	LastUploadedMassPointCount = PendingMassPoints.Num();
	GpuSolverProxy.SetPreparedMassPoints(MoveTemp(PendingMassPoints));
	PendingMassPoints.Reset();
	return true;
}

bool UTDFluidLiveComponent::AcceptsMassInput() const
{
	return MassInputSettings.bAcceptMassInput && TDFluidMassInput::IsAllowedByConsole();
}

FVector UTDFluidLiveComponent::GetSimulationWindowCenterWorld() const
{
	const FVector2D CenterPlane = bUsesGpuSolver ? GpuSolverProxy.GetCenterPlaneCm() : FVector2D(Solver.GetCenterWorld());
	return UnprojectSolverPlaneToWorld(CenterPlane, 0.0);
}

FTDFluidGpuMassInputCounters UTDFluidLiveComponent::GetMassInputCounters() const
{
	return bUsesGpuSolver ? GpuSolverProxy.GetLatestMassInputCounters() : FTDFluidGpuMassInputCounters();
}

void UTDFluidLiveComponent::EnsureMassInputDataChannelLoadRequested()
{
	const FSoftObjectPath DesiredPath = MassInputSettings.bReadDataChannel ? MassInputSettings.DataChannel.ToSoftObjectPath() : FSoftObjectPath();
	if (DesiredPath == RequestedMassInputDataChannelPath)
	{
		return;
	}

	RequestAsyncAssetLoads();
}

void UTDFluidLiveComponent::SortAndClampMassPoints(TArray<FTDFluidMassInputPoint>& InOutPoints, const FVector& CenterWorld, int32 MaxPoints)
{
	if (MaxPoints <= 0)
	{
		InOutPoints.Reset();
		return;
	}

	constexpr int32 SortKeyCount = 9;
	struct FTDFluidMassPointSortEntry
	{
		int64 Keys[SortKeyCount];
		int32 PointIndex;
	};

	auto ToMillimeters = [](double ValueCm)
	{
		return FMath::IsFinite(ValueCm) ? FMath::RoundToInt64(FMath::Clamp(ValueCm * 10.0, -9.0e15, 9.0e15)) : 0;
	};

	TArray<FTDFluidMassPointSortEntry> SortEntries;
	SortEntries.Reserve(InOutPoints.Num());
	for (int32 PointIndex = 0; PointIndex < InOutPoints.Num(); ++PointIndex)
	{
		const FTDFluidMassInputPoint& Point = InOutPoints[PointIndex];
		FTDFluidMassPointSortEntry& Entry = SortEntries.AddDefaulted_GetRef();
		const double DistanceSquared = FVector::DistSquared(Point.WorldLocation, CenterWorld);
		Entry.Keys[0] = FMath::IsFinite(DistanceSquared) ? FMath::RoundToInt64(FMath::Min(DistanceSquared, 9.0e15)) : MAX_int64;
		Entry.Keys[1] = ToMillimeters(Point.WorldLocation.X);
		Entry.Keys[2] = ToMillimeters(Point.WorldLocation.Y);
		Entry.Keys[3] = ToMillimeters(Point.WorldLocation.Z);
		Entry.Keys[4] = ToMillimeters(Point.WorldVelocity.X);
		Entry.Keys[5] = ToMillimeters(Point.WorldVelocity.Y);
		Entry.Keys[6] = ToMillimeters(Point.RadiusCm);
		Entry.Keys[7] = ToMillimeters(Point.DensityStrength * 1000.0);
		Entry.Keys[8] = ToMillimeters(Point.HeatStrength * 1000.0);
		Entry.PointIndex = PointIndex;
	}

	SortEntries.StableSort([](const FTDFluidMassPointSortEntry& Left, const FTDFluidMassPointSortEntry& Right)
	{
		for (int32 KeyIndex = 0; KeyIndex < SortKeyCount; ++KeyIndex)
		{
			if (Left.Keys[KeyIndex] != Right.Keys[KeyIndex])
			{
				return Left.Keys[KeyIndex] < Right.Keys[KeyIndex];
			}
		}
		return false;
	});

	const int32 KeptCount = FMath::Min(MaxPoints, SortEntries.Num());
	TArray<FTDFluidMassInputPoint> SortedPoints;
	SortedPoints.Reserve(KeptCount);
	for (int32 SortedIndex = 0; SortedIndex < KeptCount; ++SortedIndex)
	{
		SortedPoints.Add(InOutPoints[SortEntries[SortedIndex].PointIndex]);
	}
	InOutPoints = MoveTemp(SortedPoints);
}

void UTDFluidLiveComponent::QueueMassInputPoints(TConstArrayView<FTDFluidMassInputPoint> Points, ETDFluidBrushSource Source)
{
	if (Points.IsEmpty() || (Source != ETDFluidBrushSource::Rain && !AcceptsMassInput()) || !IsSimulationInitialized())
	{
		return;
	}

	TArray<FTDFluidMassInputPoint> AcceptedPoints;
	AcceptedPoints.Reserve(Points.Num());
	for (const FTDFluidMassInputPoint& Point : Points)
	{
		FVector2D PlanePoint;
		if (TryProjectWorldPointIntoSlab(Point.WorldLocation, PlanePoint))
		{
			AcceptedPoints.Add(Point);
		}
	}
	if (AcceptedPoints.IsEmpty())
	{
		return;
	}

	const FVector CenterWorld = GetSimulationWindowCenterWorld();
	if (!bUsesGpuSolver || IsUsingGpuWaterLayer() || !AcceptsMassInput())
	{
		const int32 RemainingStampSlots = FMath::Max(SimulationConfig.MaxPointsPerTick, 1) - PendingBrushStamps.Num();
		SortAndClampMassPoints(AcceptedPoints, CenterWorld, RemainingStampSlots);
		for (const FTDFluidMassInputPoint& Point : AcceptedPoints)
		{
			FTDFluidBrushStamp& Stamp = PendingBrushStamps.AddDefaulted_GetRef();
			Stamp.WorldLocation = Point.WorldLocation;
			Stamp.WorldVelocity = Point.WorldVelocity;
			Stamp.RadiusCm = FMath::Max(Point.RadiusCm, 0.0f) * InputBrushSettings.MassPointRadiusScale;
			Stamp.DensityStrength = Point.DensityStrength * InputBrushSettings.MassPointDensityStrength;
			Stamp.VelocityStrength = InputBrushSettings.MassPointVelocityStrength * Point.VelocityStrengthScale;
			Stamp.HeatStrength = Point.HeatStrength * InputBrushSettings.MassPointHeatStrength;
			Stamp.RippleImpulseCm = Point.RippleImpulseCm + InputBrushSettings.MassPointRippleImpulseCm;
			Stamp.Channel = ActivePreset ? ActivePreset->DefaultChannel : ETDFluidBrushChannel::ChannelR_Mud;
			Stamp.Source = Source;
		}
		return;
	}

	const int32 RemainingUploadSlots = TDFluidGpuSolverConstants::MaximumMassPointsPerUpload - PendingMassPoints.Num();
	if (AcceptedPoints.Num() > RemainingUploadSlots)
	{
		SortAndClampMassPoints(AcceptedPoints, CenterWorld, RemainingUploadSlots);
	}

	const float VelocityStrength = InputBrushSettings.MassPointVelocityStrength;
	const float MaxCellSpeed = SimulationConfig.MaxCellSpeedCmPerSec;
	for (const FTDFluidMassInputPoint& Point : AcceptedPoints)
	{
		const float PointVelocityStrength = VelocityStrength * Point.VelocityStrengthScale;
		FVector2D PlaneVelocity = PlaneBasis.ProjectVector(Point.WorldVelocity) * PointVelocityStrength;
		if (MaxCellSpeed > 0.0f)
		{
			PlaneVelocity = PlaneVelocity.GetClampedToMaxSize(MaxCellSpeed);
		}

		FTDFluidGpuMassPointInput& MassPoint = PendingMassPoints.AddDefaulted_GetRef();
		MassPoint.CenterPlaneCm = ProjectWorldToSolverPlane(Point.WorldLocation);
		MassPoint.VelocityPlaneCmPerSec = PlaneVelocity;
		MassPoint.RadiusCm = FMath::Max(Point.RadiusCm, 0.0f) * InputBrushSettings.MassPointRadiusScale;
		MassPoint.DensityStrength = Point.DensityStrength * InputBrushSettings.MassPointDensityStrength;
		MassPoint.HeatStrength = Point.HeatStrength * InputBrushSettings.MassPointHeatStrength;
		MassPoint.bWritesVelocity = PointVelocityStrength > 0.0f;
	}
}

bool UTDFluidLiveComponent::BuildMassInputTarget(FTDFluidMassInputTarget& OutTarget) const
{
	if (!bUsesGpuSolver || !AcceptsMassInput() || !GpuSolverProxy.HasActorKey())
	{
		return false;
	}

	OutTarget.ActorKey = GpuSolverProxy.GetActorKey();
	OutTarget.ChannelName = MassInputSettings.MassInputChannelName;
	OutTarget.WindowCenterWorld = GetSimulationWindowCenterWorld();
	OutTarget.AxisU = PlaneBasis.U;
	OutTarget.AxisV = PlaneBasis.V;
	OutTarget.AxisN = PlaneBasis.N;
	OutTarget.PlaneCoordinateOffsetCm = PlaneBasis.PlaneCoordinateOffsetCm;
	OutTarget.PlaneDepthCm = PlaneDepthCm;
	OutTarget.SlabHalfDepthCm = GetPlaneSettings().InteractionDepthCm;
	OutTarget.bIsWorldFacing = PlaneBasis.IsWorldFacing();
	OutTarget.DensityStrength = InputBrushSettings.MassPointDensityStrength;
	OutTarget.VelocityStrength = InputBrushSettings.MassPointVelocityStrength;
	OutTarget.HeatStrength = InputBrushSettings.MassPointHeatStrength;
	OutTarget.RadiusScale = InputBrushSettings.MassPointRadiusScale;
	OutTarget.MaxCellSpeedCmPerSec = SimulationConfig.MaxCellSpeedCmPerSec;
	OutTarget.RadiusCapCells = SimulationConfig.MassSplatRadiusCapCells;
	OutTarget.bWritesSelectiveDensity = TDFluidLiveComponentMassInput::DoesPresetWriteSelectiveDensity(ActivePreset);
	OutTarget.bUsesHeat = GpuSolverProxy.GetSettings().bUsesHeatChannel;
	return true;
}

void UTDFluidLiveComponent::SetCollectedObstacles(const TArray<FTDFluidSdfObstacle>& InObstacles)
{
	const bool bIsSameList = InObstacles.Num() == CollectedObstacles.Num() && Algo::Compare(InObstacles, CollectedObstacles, [](const FTDFluidSdfObstacle& Left, const FTDFluidSdfObstacle& Right)
	{
		return Left.WorldCenter == Right.WorldCenter
			&& Left.RadiusCm == Right.RadiusCm
			&& Left.BoxHalfExtentsCm == Right.BoxHalfExtentsCm
			&& Left.bIsBox == Right.bIsBox
			&& Left.BoxHalfExtents3DCm == Right.BoxHalfExtents3DCm
			&& Left.WorldRotation == Right.WorldRotation;
	});
	if (bIsSameList)
	{
		return;
	}

	CollectedObstacles = InObstacles;
	bHasPendingObstacleUpdate = true;
	if (!IsSimulationInitialized())
	{
		return;
	}

	RasterizeActiveObstacles();
	bHasPendingObstacleUpdate = false;
}

bool UTDFluidLiveComponent::ProjectObstacleToPlane(const FTDFluidPlaneBasis& Basis, double InPlaneDepthCm, const FTDFluidPlaneSettings& InPlaneSettings, const FTDFluidSdfObstacle& InObstacle, FTDFluidSdfObstacle& OutObstacle)
{
	const double MaxDepthOffsetCm = InPlaneSettings.InteractionDepthCm;
	const bool bProjectsOrientedBox = InPlaneSettings.bProjectOrientedObstacles && InObstacle.bIsBox && !InObstacle.BoxHalfExtents3DCm.IsNearlyZero() && !Basis.IsWorldFacing();
	if (!bProjectsOrientedBox)
	{
		if (FMath::Abs(Basis.ProjectDepth(InObstacle.WorldCenter) - InPlaneDepthCm) > MaxDepthOffsetCm)
		{
			return false;
		}

		const FVector2D PlaneCenter = Basis.ProjectPoint(InObstacle.WorldCenter);
		OutObstacle = InObstacle;
		OutObstacle.WorldCenter = FVector(PlaneCenter.X, PlaneCenter.Y, 0.0);
		return true;
	}

	const FRotationMatrix RotationMatrix(InObstacle.WorldRotation);
	const FVector& HalfExtents = InObstacle.BoxHalfExtents3DCm;
	double MinimumDepth = TNumericLimits<double>::Max();
	double MaximumDepth = TNumericLimits<double>::Lowest();
	FBox2D PlaneBox(ForceInit);
	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		const FVector LocalCorner(
			(CornerIndex & 1) ? HalfExtents.X : -HalfExtents.X,
			(CornerIndex & 2) ? HalfExtents.Y : -HalfExtents.Y,
			(CornerIndex & 4) ? HalfExtents.Z : -HalfExtents.Z);
		const FVector WorldCorner = InObstacle.WorldCenter + RotationMatrix.TransformVector(LocalCorner);
		const double CornerDepth = Basis.ProjectDepth(WorldCorner);
		MinimumDepth = FMath::Min(MinimumDepth, CornerDepth);
		MaximumDepth = FMath::Max(MaximumDepth, CornerDepth);
		PlaneBox += Basis.ProjectPoint(WorldCorner);
	}

	const bool bMissesSlab = MaximumDepth < InPlaneDepthCm - MaxDepthOffsetCm || MinimumDepth > InPlaneDepthCm + MaxDepthOffsetCm;
	if (bMissesSlab)
	{
		return false;
	}

	const double PaddingCm = FMath::Max(0.0, static_cast<double>(InPlaneSettings.ObstacleContourPaddingCm));
	const FVector2D PlaneCenter = PlaneBox.GetCenter();
	OutObstacle = InObstacle;
	OutObstacle.WorldCenter = FVector(PlaneCenter.X, PlaneCenter.Y, 0.0);
	OutObstacle.BoxHalfExtentsCm = PlaneBox.GetExtent() + FVector2D(PaddingCm, PaddingCm);
	return true;
}

void UTDFluidLiveComponent::EnsureGpuOutputTargets()
{
	const int32 GpuResolution = GpuSolverProxy.GetResolution();
	if (GpuResolution <= 0)
	{
		return;
	}

	const TextureAddress OutputAddress = ResolveOutputTextureAddress();
	EnsureGpuOutputTarget(GpuDensityPressureTarget, this, GpuResolution, EmptyDensityPressureColor, OutputAddress);
	if (GpuSolverProxy.UsesVelocityOutput())
	{
		EnsureGpuOutputTarget(GpuVelocityTarget, this, GpuResolution, EmptyVelocityColor, OutputAddress);
	}
	else
	{
		EnsureGpuOutputTarget(GpuVelocityTarget, this, PainterVelocityTargetSize, FLinearColor::Black);
	}
	if (IsAuxOutputActive())
	{
		const UTextureRenderTarget2D* PreviousAuxTarget = GpuAuxTarget.Get();
		EnsureGpuOutputTarget(GpuAuxTarget, this, GpuResolution, FLinearColor::Transparent, OutputAddress);
		if (GpuAuxTarget.Get() != PreviousAuxTarget)
		{
			GpuSolverProxy.MarkObstacleDistanceAuxStale();
		}
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
	if (GpuSolverProxy.UsesWaterLayer())
	{
		EnsureGpuOutputTarget(GpuWaterSurfaceTarget, this, GpuResolution, EmptyWaterSurfaceColor);
	}
	else
	{
		GpuWaterSurfaceTarget = nullptr;
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
	if (GpuWaterSurfaceTarget)
	{
		GpuWaterSurfaceTarget->UpdateResourceImmediate(true);
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

bool UTDFluidLiveComponent::IsWaterMaterialActive() const
{
	return IsWaterLayerEnabled() || IsUsingGpuWaterLayer();
}

bool UTDFluidLiveComponent::IsUsingGpuWaterLayer() const
{
	return bUsesGpuSolver && GpuSolverProxy.UsesWaterLayer();
}

UTexture* UTDFluidLiveComponent::GetWaterSurfaceOutputTexture() const
{
	if (bUsesGpuSolver)
	{
		return GpuWaterSurfaceTarget.Get();
	}
	return WaterSurfaceTexture.Get();
}

bool UTDFluidLiveComponent::CanReceiveWaterInputs() const
{
	return bUsesGpuSolver ? (GpuSolverProxy.IsInitialized() && GpuSolverProxy.UsesWaterLayer()) : (Solver.IsInitialized() && Solver.GetConfig().bEnableWaterLayer);
}

void UTDFluidLiveComponent::SetWaterGuidesAndRegions(const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidWaterRegion>& Regions)
{
	LastWaterGuides = Guides;
	LastWaterRegions = Regions;
	bHasLastWaterInputs = true;
	if (IsUsingGpuWaterLayer())
	{
		GpuSolverProxy.SetWaterInputs(Guides, Regions);
		bHasWaterInputsMissingOnCpu = true;
		return;
	}

	Solver.SetFlowGuides(Guides);
	Solver.SetWaterRegions(Regions);
	bHasWaterInputsMissingOnCpu = false;
}

int32 UTDFluidLiveComponent::GetTerrainSampleResolution() const
{
	return IsUsingGpuWaterLayer() ? GpuSolverProxy.GetWaterFieldResolution() : GetSimulationResolution();
}

FVector2D UTDFluidLiveComponent::TerrainSampleCellToPlaneCenter(int32 CellX, int32 CellY) const
{
	return IsUsingGpuWaterLayer() ? GpuSolverProxy.WaterFieldCellToPlaneCenter(CellX, CellY) : CellToPlaneCenter(CellX, CellY);
}

float UTDFluidLiveComponent::GetWaterSurfaceHeightAtTerrainSampleCell(int32 CellX, int32 CellY) const
{
	return IsUsingGpuWaterLayer() ? GpuSolverProxy.GetWaterSurfaceHeightAtFieldCell(CellX, CellY) : Solver.GetWaterSurfaceHeightAtCell(CellX, CellY);
}

void UTDFluidLiveComponent::SetWaterProbeLocations(const TArray<FVector>& WorldLocations)
{
	const int32 KeptProbeCount = FMath::Min(WorldLocations.Num(), TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame);
	WaterProbeWorldLocations.Reset(KeptProbeCount);
	WaterProbeWorldLocations.Append(WorldLocations.GetData(), KeptProbeCount);
	++WaterProbeGeneration;
	if (IsUsingGpuWaterLayer())
	{
		SubmitGpuWaterProbePlanePoints();
	}
}

bool UTDFluidLiveComponent::GetWaterProbeSamples(TArray<FTDFluidWaterPointSample>& OutSamples) const
{
	OutSamples.Reset();
	if (bUsesGpuSolver)
	{
		return GetGpuWaterProbeSamples(OutSamples);
	}
	return GetCpuWaterProbeSamples(OutSamples);
}

void UTDFluidLiveComponent::SubmitGpuWaterProbePlanePoints()
{
	TArray<FVector2D, TInlineAllocator<TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame>> PlanePoints;
	for (const FVector& WorldLocation : WaterProbeWorldLocations)
	{
		PlanePoints.Add(ProjectWorldToSolverPlane(WorldLocation));
	}
	GpuSolverProxy.SetWaterProbePlanePoints(PlanePoints, WaterProbeGeneration);
}

bool UTDFluidLiveComponent::GetCpuWaterProbeSamples(TArray<FTDFluidWaterPointSample>& OutSamples) const
{
	if (!Solver.IsInitialized() || !Solver.GetConfig().bEnableWaterLayer)
	{
		return false;
	}

	const int32 SolverResolution = Solver.GetResolution();
	OutSamples.Reserve(WaterProbeWorldLocations.Num());
	for (const FVector& WorldLocation : WaterProbeWorldLocations)
	{
		FTDFluidWaterPointSample& PointSample = OutSamples.Add_GetRef(MakeOutsideWaterPointSample(WorldLocation, 0));
		FVector2D GridUV;
		if (!Solver.WorldToGridUV(ProjectWorldToSolverPlane(WorldLocation), GridUV))
		{
			continue;
		}

		const FIntPoint Cell = LiveComponentMapWaterProbeGridUVToCell(GridUV, SolverResolution);
		const FTDFluidCellSample CellSample = Solver.SampleAtCell(Cell.X, Cell.Y);
		const float SurfaceHeightCm = Solver.GetWaterSurfaceHeightAtCell(Cell.X, Cell.Y);
		PointSample.bIsInsideWindow = true;
		PointSample.bHasWaterSurface = SurfaceHeightCm != FTDFluidGrid2DSolver::NoWaterSurfaceHeight;
		PointSample.bIsTerrainKnown = CellSample.bIsTerrainKnown;
		PointSample.WaterSurfaceHeightCm = PointSample.bHasWaterSurface ? SurfaceHeightCm : 0.0f;
		PointSample.WaterDepthCm = CellSample.WaterDepthCm;
		PointSample.VelocityCmPerSec = CellSample.VelocityCmPerSec;
		PointSample.FlowTargetVelocityCmPerSec = CellSample.FlowTargetVelocity;
		PointSample.FlowGuideWeight = CellSample.FlowGuideWeight;
		PointSample.Whitewater = CellSample.PrimaryDensity;
		PointSample.RippleHeightCm = CellSample.RippleHeightCm;
		PointSample.ObstacleSdfCm = CellSample.ObstacleSdf;
		PointSample.Wetness = CellSample.Wetness;
	}
	return true;
}

bool UTDFluidLiveComponent::GetGpuWaterProbeSamples(TArray<FTDFluidWaterPointSample>& OutSamples) const
{
	if (!IsUsingGpuWaterLayer())
	{
		return false;
	}

	if (WaterProbeWorldLocations.IsEmpty())
	{
		return true;
	}

	TArray<FTDFluidGpuWaterProbeSample> ProbeSamples;
	uint32 SampleFrameNumber = 0;
	if (!GpuSolverProxy.TryGetWaterProbeSamples(ProbeSamples, SampleFrameNumber))
	{
		return false;
	}

	const UTDFluidLiveSubsystem* FluidSubsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(this);
	const int32 FrameAge = FluidSubsystem ? static_cast<int32>(FluidSubsystem->GetLastGpuSolverFrameNumber() - SampleFrameNumber) : 0;
	OutSamples.Reserve(WaterProbeWorldLocations.Num());
	for (int32 ProbeIndex = 0; ProbeIndex < WaterProbeWorldLocations.Num(); ++ProbeIndex)
	{
		FTDFluidWaterPointSample& PointSample = OutSamples.Add_GetRef(MakeOutsideWaterPointSample(WaterProbeWorldLocations[ProbeIndex], FrameAge));
		if (!ProbeSamples.IsValidIndex(ProbeIndex) || !LiveComponentHasWaterProbeFlag(ProbeSamples[ProbeIndex].Flags, ETDFluidGpuWaterProbeFlags::IsInsideWindow))
		{
			continue;
		}

		const FTDFluidGpuWaterProbeSample& ProbeSample = ProbeSamples[ProbeIndex];
		PointSample.bIsInsideWindow = true;
		PointSample.bHasWaterSurface = LiveComponentHasWaterProbeFlag(ProbeSample.Flags, ETDFluidGpuWaterProbeFlags::HasWaterSurface);
		PointSample.bIsTerrainKnown = LiveComponentHasWaterProbeFlag(ProbeSample.Flags, ETDFluidGpuWaterProbeFlags::IsTerrainKnown);
		PointSample.WaterSurfaceHeightCm = PointSample.bHasWaterSurface ? ProbeSample.WaterSurfaceHeightCm : 0.0f;
		PointSample.WaterDepthCm = ProbeSample.WaterDepthCm;
		PointSample.VelocityCmPerSec = FVector2D(ProbeSample.VelocityCmPerSec);
		PointSample.FlowTargetVelocityCmPerSec = FVector2D(ProbeSample.FlowTargetVelocityCmPerSec);
		PointSample.FlowGuideWeight = ProbeSample.FlowGuideWeight;
		PointSample.Whitewater = ProbeSample.Whitewater;
		PointSample.RippleHeightCm = ProbeSample.RippleHeightCm;
		PointSample.ObstacleSdfCm = ProbeSample.ObstacleSdfCm;
		PointSample.Wetness = ProbeSample.Wetness;
	}
	return true;
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
	return IsHeatAuxOutputActive() || IsObstacleDistanceAuxActive() || IsFoliageBendAuxActive();
}

bool UTDFluidLiveComponent::IsHeatAuxOutputActive() const
{
	return SimulationConfig.bEnableHeatChannel && SimulationConfig.SimMode == ETDFluidSimMode::FluidSim && !SimulationConfig.bEnableWaterLayer;
}

bool UTDFluidLiveComponent::IsObstacleDistanceAuxActive() const
{
	return SimulationConfig.bEnableWaterLayer && SimulationConfig.bOutputObstacleDistance;
}

bool UTDFluidLiveComponent::IsTransmittanceActive() const
{
	return ActivePreset
		&& GetVolumeRenderSettings().bEnableTransmittance
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

	if (IsObstacleDistanceAuxActive())
	{
		const int32 ObstacleGeneration = Solver.GetObstacleSdfRawGeneration();
		if (ObstacleGeneration == PackedAuxObstacleGeneration && !bIsFoliageBendStepEnabled)
		{
			PackedAuxPixels.Reset();
		}
		else
		{
			Solver.PackAuxPixels(PackedAuxPixels);
			PackedAuxObstacleGeneration = ObstacleGeneration;
		}
	}
	else
	{
		Solver.PackAuxPixels(PackedAuxPixels);
	}
	if (bIsFoliageBendStepEnabled)
	{
		FoliageBendLayer.WriteCpuBendToAux(PackedAuxPixels, Solver.GetResolution());
	}
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
	const double OffsetZ = ActivePreset ? GetVolumeRenderSettings().VolumePositionOffsetCm.Z : 0.0;
	return FVector(Center.X - 0.5 * Extent.X, Center.Y - 0.5 * Extent.Y, FloorCm + OffsetZ);
}

bool UTDFluidLiveComponent::BuildTransmittanceInputs(FTDFluidTransmittanceInputs& OutInputs) const
{
	if (!IsTransmittanceActive())
	{
		return false;
	}

	const FTDFluidVolumeRenderSettings VolumeRender = GetVolumeRenderSettings();
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

	const FTDFluidVolumeRenderSettings VolumeRender = GetVolumeRenderSettings();
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
