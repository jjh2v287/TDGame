#include "FluidFX/TDFluidWaterActor.h"
#include "CollisionQueryParams.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TDGame.h"

namespace
{
	constexpr int32 WaterActorTerrainTracesPerTick = 1024;
	constexpr float WaterActorPresenceCheckIntervalSeconds = 0.25f;
	constexpr double WaterActorGuideRecollectExtentRatio = 0.25;
	constexpr double WaterActorGuideCollectExtentScale = 2.0;
	constexpr float WaterActorMinimumFadeSeconds = 0.01f;
	constexpr double WaterActorRoundObstacleAspectLimit = 1.5;
	constexpr float WaterActorLodSurfaceHeightToleranceCm = 1.0f;

	void CollectWaterActorTaggedActors(const UWorld& World, FName Tag, TArray<AActor*>& OutActors)
	{
		OutActors.Reset();
		if (Tag.IsNone())
		{
			return;
		}

		UGameplayStatics::GetAllActorsWithTag(&World, Tag, OutActors);
	}

	FTDFluidSdfObstacle MakeWaterActorObstacle(const FBox& Bounds)
	{
		const FVector Extent = Bounds.GetExtent();
		const double LongExtentCm = FMath::Max(Extent.X, Extent.Y);
		const double ShortExtentCm = FMath::Max(FMath::Min(Extent.X, Extent.Y), 1.0);

		FTDFluidSdfObstacle Obstacle;
		Obstacle.WorldCenter = Bounds.GetCenter();
		Obstacle.RadiusCm = static_cast<float>(FMath::Max(LongExtentCm, 1.0));
		Obstacle.BoxHalfExtentsCm = FVector2D(Extent.X, Extent.Y);
		Obstacle.bIsBox = LongExtentCm / ShortExtentCm > WaterActorRoundObstacleAspectLimit;
		return Obstacle;
	}
}

ATDFluidWaterActor::ATDFluidWaterActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bShouldFollowPlayerPawn = true;
	bSampleTerrainElevation = true;
	MaxTerrainTracesPerTick = WaterActorTerrainTracesPerTick;
	InternalSurfaceMesh->SetVisibility(false);

	WetDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("WetDecal"));
	WetDecal->SetupAttachment(SceneRoot);
	WetDecal->SetAbsolute(true, true, true);
	WetDecal->SetVisibility(false);

	LodGridComponent = CreateDefaultSubobject<UTDFluidWaterLodGridComponent>(TEXT("LodGrid"));
	LodGridComponent->SetupAttachment(SceneRoot);
}

void ATDFluidWaterActor::BeginPlay()
{
	Super::BeginPlay();

	RequestWetDecalMaterial();
	RequestLodGridPatchMesh();

	UWorld* World = GetWorld();
	if (!World || !LiveComponent)
	{
		return;
	}

	for (TActorIterator<ATDFluidWaterActor> ActorIterator(World); ActorIterator; ++ActorIterator)
	{
		const ATDFluidWaterActor* OtherWaterActor = *ActorIterator;
		if (OtherWaterActor == this || !OtherWaterActor->HasActorBegunPlay() || OtherWaterActor->bIsDormantDuplicate)
		{
			continue;
		}

		UE_LOG(LogTDGame, Warning, TEXT("TDFluidWaterActor %s sleeps because %s already drives water in this world"), *GetName(), *OtherWaterActor->GetName());
		bIsDormantDuplicate = true;
		LiveComponent->SetSimulationAwake(false);
		return;
	}

	RequestDenseWaterCacheLoad();
}

void ATDFluidWaterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideWetDecal();
	if (WetDecalLoadHandle.IsValid())
	{
		WetDecalLoadHandle->CancelHandle();
		WetDecalLoadHandle.Reset();
	}
	if (LodGridMeshLoadHandle.IsValid())
	{
		LodGridMeshLoadHandle->CancelHandle();
		LodGridMeshLoadHandle.Reset();
	}
	if (DenseWaterCacheLoadHandle.IsValid())
	{
		DenseWaterCacheLoadHandle->CancelHandle();
		DenseWaterCacheLoadHandle.Reset();
	}
	bIsWaitingForDenseWaterCache = false;
	DisableLodGrid();

	Super::EndPlay(EndPlayReason);
}

bool ATDFluidWaterActor::IsWetDecalVisible() const
{
	return WetDecal && WetDecal->IsVisible() && !WetDecal->bHiddenInGame;
}

UMaterialInstanceDynamic* ATDFluidWaterActor::GetWetDecalMaterialInstance() const
{
	return WetDecalMID;
}

void ATDFluidWaterActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TickWaterLayer(DeltaSeconds);
	UpdateWetDecalPlacement();
	UpdateLodGrid();
}

void ATDFluidWaterActor::ReapplyPresetTuning(FTDFluidPresetReapplyReport& InOutReport)
{
	Super::ReapplyPresetTuning(InOutReport);
	bHasLodGridSettings = false;
	bIsLodSelectionDirty = true;
}

void ATDFluidWaterActor::RequestLodGridPatchMesh()
{
	if (LodGridPatchMesh.IsNull() || LodGridPatchMesh.Get() || LodGridMeshLoadHandle.IsValid())
	{
		return;
	}

	LodGridMeshLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(LodGridPatchMesh.ToSoftObjectPath());
}

bool ATDFluidWaterActor::CanUseLodGrid() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && LodGridComponent && LiveComponent && !bIsDormantDuplicate && !IsPooledInactive()
		&& !LiveComponent->IsDenseWaterActive() && UTDFluidWaterLodGridComponent::ResolveLodGridEnabled(LodGridSettings.bEnableLodGrid);
}

bool ATDFluidWaterActor::PrepareLodGridPatchMesh()
{
	if (LodGridPatchMesh.IsNull())
	{
		if (!bHasWarnedLodGridMesh)
		{
			UE_LOG(LogTDGame, Warning, TEXT("TDFluidWaterLodGrid %s stays off because it has no patch mesh"), *GetName());
			bHasWarnedLodGridMesh = true;
		}
		return false;
	}

	UStaticMesh* PatchMesh = LodGridPatchMesh.Get();
	if (!PatchMesh)
	{
		RequestLodGridPatchMesh();
		return false;
	}

	if (LodGridComponent->GetStaticMesh() == PatchMesh)
	{
		return true;
	}

	const int32 ExpectedVertexCount = (LodGridSettings.PatchQuads + 1) * (LodGridSettings.PatchQuads + 1);
	if (PatchMesh->GetNumVertices(0) != ExpectedVertexCount)
	{
		if (!bHasWarnedLodGridMesh)
		{
			UE_LOG(LogTDGame, Error, TEXT("TDFluidWaterLodGrid %s stays off because %s has %d vertices instead of %d"), *GetName(), *PatchMesh->GetName(), PatchMesh->GetNumVertices(0), ExpectedVertexCount);
			bHasWarnedLodGridMesh = true;
		}
		return false;
	}

	LodGridComponent->SetStaticMesh(PatchMesh);
	return true;
}

bool ATDFluidWaterActor::CollectLodGridCoverage(FBox2D& OutCoverageCm, double& OutSurfaceHeightCm) const
{
	OutCoverageCm = FBox2D(ForceInit);
	if (CachedWaterRegions.IsEmpty())
	{
		return false;
	}

	OutSurfaceHeightCm = CachedWaterRegions[0].SurfaceHeightCm;
	for (const FTDFluidWaterRegion& Region : CachedWaterRegions)
	{
		if (FMath::Abs(Region.SurfaceHeightCm - OutSurfaceHeightCm) > WaterActorLodSurfaceHeightToleranceCm)
		{
			continue;
		}
		OutCoverageCm += FBox2D(Region.MinXY, Region.MaxXY);
	}
	return OutCoverageCm.bIsValid;
}

UMaterialInstanceDynamic* ATDFluidWaterActor::ResolveLodGridMaterial(const FBox2D& CoverageCm, TArray<AActor*>& OutCoveredSurfaceActors) const
{
	OutCoveredSurfaceActors.Reset();
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<AActor*> SurfaceActors;
	CollectWaterActorTaggedActors(*World, WaterSurfaceActorTag, SurfaceActors);
	UMaterialInstanceDynamic* LodMaterial = nullptr;
	for (AActor* SurfaceActor : SurfaceActors)
	{
		const FBox Bounds = SurfaceActor->GetComponentsBoundingBox(true);
		if (!Bounds.IsValid || !CoverageCm.Intersect(FBox2D(FVector2D(Bounds.Min.X, Bounds.Min.Y), FVector2D(Bounds.Max.X, Bounds.Max.Y))))
		{
			continue;
		}

		OutCoveredSurfaceActors.Add(SurfaceActor);
		if (LodMaterial)
		{
			continue;
		}

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents(SurfaceActor);
		for (const UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			LodMaterial = Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(0));
			if (LodMaterial)
			{
				break;
			}
		}
	}
	return LodMaterial;
}

FVector ATDFluidWaterActor::ResolveLodGridObserverWorld() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		return PlayerController->PlayerCameraManager->GetCameraLocation();
	}

	if (const AActor* FollowedActor = ResolveFollowedActor())
	{
		return FollowedActor->GetActorLocation();
	}

	return GetActorLocation();
}

bool ATDFluidWaterActor::ShouldReselectLodGrid(const FVector& ObserverWorld) const
{
	if (!bIsLodGridActive || bIsLodSelectionDirty || LodAppliedRegistryVersion != AppliedRegistryVersion)
	{
		return true;
	}

	return FVector::DistSquared(ObserverWorld, LastLodObserverWorld) > FMath::Square(static_cast<double>(LodGridSettings.ReselectDistanceCm));
}

void ATDFluidWaterActor::PushLodGridMaterialParameters(UMaterialInstanceDynamic& LodMaterial, const FVector& ObserverWorld, double EffectiveBaseRangeCm) const
{
	LodMaterial.SetVectorParameterValue(TEXT("LodObserverWorld"), FLinearColor(static_cast<float>(ObserverWorld.X), static_cast<float>(ObserverWorld.Y), static_cast<float>(ObserverWorld.Z), 0.0f));
	LodMaterial.SetScalarParameterValue(TEXT("LodBaseRangeCm"), static_cast<float>(EffectiveBaseRangeCm));
	LodMaterial.SetScalarParameterValue(TEXT("LodBaseQuadSizeCm"), LodGridSettings.BaseQuadSizeCm);
	LodMaterial.SetScalarParameterValue(TEXT("LodMorphStartRatio"), LodGridSettings.MorphStartRatio);
	LodMaterial.SetScalarParameterValue(TEXT("LodVertexWaveFilterRatio"), LodGridSettings.VertexWaveFilterRatio);
}

void ATDFluidWaterActor::SetTaggedSurfacesHiddenInGame(const TArray<AActor*>& SurfaceActors, bool bShouldHide)
{
	if (!bShouldHide)
	{
		for (const TWeakObjectPtr<AActor>& HiddenActor : LodHiddenSurfaceActors)
		{
			if (AActor* SurfaceActor = HiddenActor.Get())
			{
				SurfaceActor->SetActorHiddenInGame(false);
			}
		}
		LodHiddenSurfaceActors.Reset();
		return;
	}

	for (AActor* SurfaceActor : SurfaceActors)
	{
		if (SurfaceActor->IsHidden())
		{
			continue;
		}
		SurfaceActor->SetActorHiddenInGame(true);
		LodHiddenSurfaceActors.Add(SurfaceActor);
	}
}

void ATDFluidWaterActor::DisableLodGrid()
{
	SetTaggedSurfacesHiddenInGame(TArray<AActor*>(), false);
	if (LodGridComponent)
	{
		LodGridComponent->ClearPatches();
	}
	bIsLodGridActive = false;
	LodGridStats.PatchCount = 0;
	LodGridStats.VertexCount = 0;
	LodGridStats.MaxLevel = 0;
}

void ATDFluidWaterActor::UpdateLodGrid()
{
	if (!bHasLodGridSettings)
	{
		const ETDFluidPresetKind PresetKind = LiveComponent && LiveComponent->ActivePreset ? LiveComponent->ActivePreset->PresetKind : ETDFluidPresetKind::Custom;
		LodGridSettings = UTDFluidWaterLodGridComponent::SanitizeSettings(UTDFluidPresetAsset::BuildWaterLodGridSettingsForPresetKind(PresetKind));
		bHasLodGridSettings = true;
	}

	if (!CanUseLodGrid() || !PrepareLodGridPatchMesh())
	{
		DisableLodGrid();
		return;
	}

	const FVector ObserverWorld = ResolveLodGridObserverWorld();
	if (!ShouldReselectLodGrid(ObserverWorld))
	{
		return;
	}

	FBox2D CoverageCm;
	double SurfaceHeightCm = 0.0;
	TArray<AActor*> CoveredSurfaceActors;
	UMaterialInstanceDynamic* LodMaterial = CollectLodGridCoverage(CoverageCm, SurfaceHeightCm) ? ResolveLodGridMaterial(CoverageCm, CoveredSurfaceActors) : nullptr;
	if (!LodMaterial)
	{
		DisableLodGrid();
		return;
	}

	FTDFluidWaterLodSelectionInput SelectionInput;
	SelectionInput.ObserverWorld = ObserverWorld;
	SelectionInput.CoverageCm = CoverageCm;
	SelectionInput.SurfaceHeightCm = SurfaceHeightCm;
	SelectionInput.Settings = LodGridSettings;
	const double SelectionStartSeconds = FPlatformTime::Seconds();
	const FTDFluidWaterLodSelectionResult SelectionResult = UTDFluidWaterLodGridComponent::SelectPatches(SelectionInput);
	const double SelectionSeconds = FPlatformTime::Seconds() - SelectionStartSeconds;
	if (SelectionResult.bReachedBudget && !bIsLodGridActive)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDFluidWaterLodGrid %s exceeds the vertex budget with %d patches"), *GetName(), SelectionResult.Patches.Num());
	}

	LodGridComponent->ApplyPatches(SelectionResult, SurfaceHeightCm, LodGridSettings.PatchQuads);
	if (LodGridComponent->GetMaterial(0) != LodMaterial)
	{
		LodGridComponent->SetMaterial(0, LodMaterial);
	}
	PushLodGridMaterialParameters(*LodMaterial, ObserverWorld, SelectionResult.EffectiveBaseRangeCm);
	SetTaggedSurfacesHiddenInGame(CoveredSurfaceActors, true);

	int32 MaxLevel = 0;
	for (const FTDFluidWaterLodPatch& Patch : SelectionResult.Patches)
	{
		MaxLevel = FMath::Max(MaxLevel, Patch.Level);
	}
	LodGridStats.PatchCount = SelectionResult.Patches.Num();
	LodGridStats.VertexCount = SelectionResult.Patches.Num() * (LodGridSettings.PatchQuads + 1) * (LodGridSettings.PatchQuads + 1);
	LodGridStats.MaxLevel = MaxLevel;
	LodGridStats.EffectiveBaseRangeCm = static_cast<float>(SelectionResult.EffectiveBaseRangeCm);
	LodGridStats.LastSelectionMilliseconds = static_cast<float>(SelectionSeconds * 1000.0);
	++LodGridStats.SelectionCount;
	LastLodObserverWorld = ObserverWorld;
	LodAppliedRegistryVersion = AppliedRegistryVersion;
	bIsLodSelectionDirty = false;
	bIsLodGridActive = true;
}

void ATDFluidWaterActor::TickWaterLayer(float DeltaSeconds)
{
	if (!LiveComponent || IsPooledInactive())
	{
		return;
	}

	if (bIsDormantDuplicate)
	{
		LiveComponent->SetSimulationAwake(false);
		return;
	}

	RefreshWaterRegistryIfChanged();

	if (!bSleepWhenNoWaterInWindow || !LiveComponent->SimulationConfig.bEnableWaterLayer)
	{
		if (bIsSleepingForMissingWater)
		{
			WakeForPresentWater();
		}
		return;
	}

	UpdateWaterPresence(DeltaSeconds);

	if (LiveComponent->IsSimulationAwake())
	{
		if (SecondsWithoutWater >= SleepDelaySeconds)
		{
			SleepForMissingWater();
		}
		return;
	}

	if (bIsSleepingForMissingWater && bHasWaterInWindow)
	{
		WakeForPresentWater();
	}
}

FVector ATDFluidWaterActor::ResolvePlaneAnchorWorldLocation() const
{
	if (!bShouldFollowPlayerPawn || (LiveComponent && LiveComponent->IsDenseWaterActive()))
	{
		return GetActorLocation();
	}

	const AActor* FollowedActor = ResolveFollowedActor();
	if (!FollowedActor)
	{
		return GetActorLocation();
	}

	return FollowedActor->GetActorLocation() + ComputeLookAheadOffset();
}

void ATDFluidWaterActor::PrepareActorSpecificInputs(float DeltaTime)
{
	if (bIsDormantDuplicate)
	{
		LiveComponent->SetSimulationAwake(false);
		return;
	}

	if (bIsWaitingForDenseWaterCache)
	{
		LiveComponent->SetSimulationAwake(false);
		return;
	}

	RefreshWaterRegistryIfChanged();
	LiveComponent->SetTerrainReferenceZCm(static_cast<float>(GetActorLocation().Z));
	RefreshSolverWaterInputs();
}

void ATDFluidWaterActor::FinishActorSpecificOutputs(float DeltaTime)
{
	const int32 SolverGeneration = LiveComponent->GetSimulationGeneration();
	if (SolverGeneration != TrackedSolverGeneration)
	{
		ResetWaterLayerFade();
		TrackedSolverGeneration = SolverGeneration;
	}

	if (bTerrainSamplingFinished)
	{
		FadeElapsedSeconds += DeltaTime;
	}

	const float FadeSeconds = FMath::Max(WaterLayerFadeInSeconds, WaterActorMinimumFadeSeconds);
	WaterLayerDisplayAlpha = bTerrainSamplingFinished ? FMath::Clamp(FadeElapsedSeconds / FadeSeconds, 0.0f, 1.0f) : 0.0f;
	LiveComponent->SetWaterLayerDisplayAlpha(WaterLayerDisplayAlpha);
}

float ATDFluidWaterActor::ResolveTerrainMissElevationCm(int32 CellX, int32 CellY) const
{
	const float SurfaceHeightCm = LiveComponent->GetWaterSurfaceHeightAtTerrainSampleCell(CellX, CellY);
	if (SurfaceHeightCm == FTDFluidGrid2DSolver::NoWaterSurfaceHeight)
	{
		return Super::ResolveTerrainMissElevationCm(CellX, CellY);
	}

	return SurfaceHeightCm - DeepWaterFallbackDepthCm;
}

void ATDFluidWaterActor::AddTerrainTraceIgnoredActors(FCollisionQueryParams& QueryParams) const
{
	for (const TWeakObjectPtr<AActor>& IgnoredActor : TraceIgnoredActors)
	{
		if (const AActor* ResolvedActor = IgnoredActor.Get())
		{
			QueryParams.AddIgnoredActor(ResolvedActor);
		}
	}
}

void ATDFluidWaterActor::OnTerrainSamplingCompleted()
{
	TrackedSolverGeneration = LiveComponent->GetSimulationGeneration();
	bTerrainSamplingFinished = true;
}

const AActor* ATDFluidWaterActor::ResolveFollowedActor() const
{
	if (const AActor* ExplicitTarget = FollowTargetActor.Get())
	{
		return ExplicitTarget;
	}

	const UWorld* World = GetWorld();
	return World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
}

FVector ATDFluidWaterActor::ComputeLookAheadOffset() const
{
	const UWorld* World = GetWorld();
	const UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	const FRotator ViewRotation = FluidSubsystem ? FluidSubsystem->GetPlaneViewRotation() : FTDFluidPlaneBasis::GetDefaultViewRotation();
	return FRotator(0.0, ViewRotation.Yaw, 0.0).Vector() * ViewLookAheadCm;
}

FBox2D ATDFluidWaterActor::ComputeWindowBoundsXY(double HalfExtentScale) const
{
	const FVector AnchorLocation = ResolvePlaneAnchorWorldLocation();
	const FVector2D Center(AnchorLocation.X, AnchorLocation.Y);
	const FVector2D HalfExtentCm = LiveComponent->SimulationConfig.SimExtentCm * (0.5 * HalfExtentScale);
	return FBox2D(Center - HalfExtentCm, Center + HalfExtentCm);
}

void ATDFluidWaterActor::RefreshWaterRegistryIfChanged()
{
	const UWorld* World = GetWorld();
	const UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	if (!FluidSubsystem || FluidSubsystem->GetWaterRegistryVersion() == AppliedRegistryVersion)
	{
		return;
	}

	AppliedRegistryVersion = FluidSubsystem->GetWaterRegistryVersion();
	RebuildWaterRegistryCaches();
}

void ATDFluidWaterActor::RebuildWaterRegistryCaches()
{
	const UWorld* World = GetWorld();
	if (!World || !LiveComponent)
	{
		return;
	}

	TArray<AActor*> TaggedActors;

	CachedWaterRegions.Reset();
	CollectWaterActorTaggedActors(*World, WaterRegionActorTag, TaggedActors);
	for (const AActor* RegionActor : TaggedActors)
	{
		const FBox Bounds = RegionActor->GetComponentsBoundingBox(true);
		if (!Bounds.IsValid)
		{
			continue;
		}

		FTDFluidWaterRegion& Region = CachedWaterRegions.AddDefaulted_GetRef();
		Region.MinXY = FVector2D(Bounds.Min.X, Bounds.Min.Y);
		Region.MaxXY = FVector2D(Bounds.Max.X, Bounds.Max.Y);
		Region.SurfaceHeightCm = static_cast<float>(Bounds.Max.Z);
	}

	TArray<FTDFluidSdfObstacle> Obstacles;
	CollectWaterActorTaggedActors(*World, WaterObstacleActorTag, TaggedActors);
	for (const AActor* ObstacleActor : TaggedActors)
	{
		const FBox Bounds = ObstacleActor->GetComponentsBoundingBox(true);
		if (Bounds.IsValid)
		{
			Obstacles.Add(MakeWaterActorObstacle(Bounds));
		}
	}
	if (!Obstacles.IsEmpty() || AppliedObstacleCount > 0)
	{
		LiveComponent->SetSdfObstacles(Obstacles);
	}
	AppliedObstacleCount = Obstacles.Num();
	CachedWaterObstacles = Obstacles;

	TraceIgnoredActors.Reset();
	CollectWaterActorTaggedActors(*World, TerrainTraceIgnoreTag, TaggedActors);
	for (AActor* IgnoredActor : TaggedActors)
	{
		TraceIgnoredActors.Add(IgnoredActor);
	}

	CollectWaterActorTaggedActors(*World, WaterSurfaceActorTag, TaggedActors);
	for (AActor* SurfaceActor : TaggedActors)
	{
		TraceIgnoredActors.Add(SurfaceActor);
	}
	RebuildSharedSurfaceMaterials(TaggedActors);

	bAreWaterInputsDirty = true;
	bIsPresenceCheckPending = true;
}

void ATDFluidWaterActor::RebuildSharedSurfaceMaterials(const TArray<AActor*>& SurfaceActors)
{
	DrivenSurfaceCount = 0;
	TSet<UMaterialInterface*> UsedParentMaterials;

	for (const AActor* SurfaceActor : SurfaceActors)
	{
		TInlineComponentArray<UStaticMeshComponent*> MeshComponents(SurfaceActor);
		for (UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			UMaterialInterface* CurrentMaterial = MeshComponent->GetMaterial(0);
			if (!CurrentMaterial)
			{
				continue;
			}

			UMaterialInterface* ParentMaterial = ResolveSharedMaterialParent(CurrentMaterial);
			TObjectPtr<UMaterialInstanceDynamic>& SharedMaterial = SharedSurfaceMaterials.FindOrAdd(ParentMaterial);
			if (!SharedMaterial)
			{
				SharedMaterial = UMaterialInstanceDynamic::Create(ParentMaterial, this);
				UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*SharedMaterial);
				LiveComponent->RegisterExternalMaterialTarget(SharedMaterial.Get());
				if (TileMapSourceActor && TileMapSourceActor->LiveComponent)
				{
					TileMapSourceActor->LiveComponent->RegisterTileMapTarget(SharedMaterial.Get());
				}
			}
			UMaterialInstanceDynamic* SharedMaterialInstance = SharedMaterial.Get();
			if (!SharedMaterialInstance)
			{
				continue;
			}
			UsedParentMaterials.Add(ParentMaterial);

			if (MeshComponent->GetMaterial(0) != SharedMaterialInstance)
			{
				MeshComponent->SetMaterial(0, SharedMaterialInstance);
			}
			if (MeshComponent->bReceivesDecals)
			{
				MeshComponent->SetReceivesDecals(false);
			}
			++DrivenSurfaceCount;
		}
	}

	for (auto MaterialIterator = SharedSurfaceMaterials.CreateIterator(); MaterialIterator; ++MaterialIterator)
	{
		if (!UsedParentMaterials.Contains(MaterialIterator.Key().Get()))
		{
			MaterialIterator.RemoveCurrent();
		}
	}
}

UMaterialInterface* ATDFluidWaterActor::ResolveSharedMaterialParent(UMaterialInterface* Material) const
{
	for (const TPair<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>>& SharedEntry : SharedSurfaceMaterials)
	{
		if (SharedEntry.Value.Get() == Material)
		{
			return SharedEntry.Key.Get();
		}
	}

	return Material;
}

void ATDFluidWaterActor::RefreshSolverWaterInputs()
{
	if (!LiveComponent->CanReceiveWaterInputs())
	{
		return;
	}

	const FVector AnchorLocation = ResolvePlaneAnchorWorldLocation();
	const FVector2D AnchorXY(AnchorLocation.X, AnchorLocation.Y);
	const double RecollectDistanceCm = LiveComponent->GetSimulationExtentCm().GetMax() * WaterActorGuideRecollectExtentRatio;
	const bool bHasMovedFar = FVector2D::DistSquared(AnchorXY, GuideCollectionCenter) > FMath::Square(RecollectDistanceCm);
	if (bHasCollectedGuides && !bAreWaterInputsDirty && !bHasMovedFar)
	{
		return;
	}

	const FBox2D CollectBounds = ComputeWindowBoundsXY(WaterActorGuideCollectExtentScale);
	const UWorld* World = GetWorld();
	const UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;

	TArray<FTDFluidFlowGuide> Guides;
	if (FluidSubsystem)
	{
		FluidSubsystem->CollectFlowGuidesInBounds(CollectBounds, Guides);
	}

	TArray<FTDFluidWaterRegion> Regions;
	for (const FTDFluidWaterRegion& Region : CachedWaterRegions)
	{
		if (CollectBounds.Intersect(FBox2D(Region.MinXY, Region.MaxXY)))
		{
			Regions.Add(Region);
		}
	}

	LiveComponent->SetWaterGuidesAndRegions(Guides, Regions);
	GuideCollectionCenter = AnchorXY;
	bHasCollectedGuides = true;
	bAreWaterInputsDirty = false;
}

bool ATDFluidWaterActor::DoesWindowContainWater() const
{
	const FBox2D WindowBounds = ComputeWindowBoundsXY(1.0);
	for (const FTDFluidWaterRegion& Region : CachedWaterRegions)
	{
		if (WindowBounds.Intersect(FBox2D(Region.MinXY, Region.MaxXY)))
		{
			return true;
		}
	}

	const UWorld* World = GetWorld();
	const UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	if (!FluidSubsystem)
	{
		return false;
	}

	TArray<FTDFluidFlowGuide> GuidesInWindow;
	FluidSubsystem->CollectFlowGuidesInBounds(WindowBounds, GuidesInWindow);
	return !GuidesInWindow.IsEmpty();
}

void ATDFluidWaterActor::UpdateWaterPresence(float DeltaSeconds)
{
	PresenceCheckAccumulatorSeconds += DeltaSeconds;
	if (bIsPresenceCheckPending || PresenceCheckAccumulatorSeconds >= WaterActorPresenceCheckIntervalSeconds)
	{
		PresenceCheckAccumulatorSeconds = 0.0f;
		bIsPresenceCheckPending = false;
		bHasWaterInWindow = DoesWindowContainWater();
	}

	SecondsWithoutWater = bHasWaterInWindow ? 0.0f : SecondsWithoutWater + DeltaSeconds;
}

void ATDFluidWaterActor::SleepForMissingWater()
{
	bIsSleepingForMissingWater = true;
	ResetWaterLayerFade();
	LiveComponent->SetSimulationAwake(false);
}

void ATDFluidWaterActor::WakeForPresentWater()
{
	bIsSleepingForMissingWater = false;
	SecondsWithoutWater = 0.0f;
	bAreWaterInputsDirty = true;
	ResetWaterLayerFade();
	LiveComponent->ResetSimulationState();
	LiveComponent->SetSimulationAwake(true);
}

void ATDFluidWaterActor::ResetWaterLayerFade()
{
	bTerrainSamplingFinished = false;
	FadeElapsedSeconds = 0.0f;
	WaterLayerDisplayAlpha = 0.0f;
	LiveComponent->SetWaterLayerDisplayAlpha(0.0f);
}

void ATDFluidWaterActor::RequestWetDecalMaterial()
{
	if (!bEnableWetDecal || WetDecalMaterial.IsNull() || !WetDecal || WetDecalMID)
	{
		return;
	}

	if (WetDecalLoadHandle.IsValid())
	{
		WetDecalLoadHandle->CancelHandle();
		WetDecalLoadHandle.Reset();
	}

	WetDecalLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		WetDecalMaterial.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &ATDFluidWaterActor::ApplyLoadedWetDecalMaterial));
}

void ATDFluidWaterActor::ApplyLoadedWetDecalMaterial()
{
	UMaterialInterface* LoadedMaterial = WetDecalMaterial.Get();
	if (!LoadedMaterial || !WetDecal || !LiveComponent || WetDecalMID)
	{
		return;
	}

	WetDecalMID = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
	UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*WetDecalMID);
	WetDecal->SetDecalMaterial(WetDecalMID);
	LiveComponent->RegisterExternalMaterialTarget(WetDecalMID);
}

void ATDFluidWaterActor::UpdateWetDecalPlacement()
{
	if (!WetDecal)
	{
		return;
	}

	float TerrainMinimumCm = 0.0f;
	float TerrainMaximumCm = 0.0f;
	const bool bCanShowDecal = bEnableWetDecal
		&& WetDecalMID
		&& LiveComponent
		&& !IsPooledInactive()
		&& LiveComponent->IsSimulationAwake()
		&& LiveComponent->SimulationConfig.bEnableWaterLayer
		&& !bIsDormantDuplicate
		&& !bIsSleepingForMissingWater
		&& LiveComponent->TryGetSampledTerrainRangeCm(TerrainMinimumCm, TerrainMaximumCm);
	if (!bCanShowDecal)
	{
		HideWetDecal();
		return;
	}

	const FVector2D WindowCenter = LiveComponent->GetSimulationCenterWorld();
	const FVector2D WindowExtent = LiveComponent->GetSimulationExtentCm();
	const FVector DecalLocation(WindowCenter.X, WindowCenter.Y, 0.5 * (TerrainMinimumCm + TerrainMaximumCm));
	const FVector DecalSize(0.5 * (TerrainMaximumCm - TerrainMinimumCm) + WetDecalVerticalMarginCm, 0.5 * WindowExtent.Y, 0.5 * WindowExtent.X);
	PlaceWetDecalIfChanged(DecalLocation, DecalSize);

	if (!WetDecal->IsVisible())
	{
		WetDecal->SetVisibility(true);
	}
}

void ATDFluidWaterActor::HideWetDecal()
{
	if (WetDecal && WetDecal->IsVisible())
	{
		WetDecal->SetVisibility(false);
	}
}

void ATDFluidWaterActor::PlaceWetDecalIfChanged(const FVector& WorldLocation, const FVector& DecalSize)
{
	const FQuat DownwardRotation = FRotator(-90.0, 0.0, 0.0).Quaternion();
	if (!WetDecal->GetComponentLocation().Equals(WorldLocation, 0.01) || !WetDecal->GetComponentQuat().Equals(DownwardRotation, 0.0001))
	{
		WetDecal->SetWorldLocationAndRotation(WorldLocation, DownwardRotation);
	}

	if (WetDecal->DecalSize.Equals(DecalSize, 0.01))
	{
		return;
	}

	WetDecal->DecalSize = DecalSize;
	WetDecal->UpdateBounds();
	WetDecal->MarkRenderStateDirty();
}
