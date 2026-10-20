#include "FluidFX/TDFluidWaterActor.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
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
}

void ATDFluidWaterActor::BeginPlay()
{
	Super::BeginPlay();

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
}

void ATDFluidWaterActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

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
	if (!bShouldFollowPlayerPawn)
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

	RefreshWaterRegistryIfChanged();
	LiveComponent->SetTerrainReferenceZCm(static_cast<float>(GetActorLocation().Z));
	RefreshSolverWaterInputs();
}

void ATDFluidWaterActor::FinishActorSpecificOutputs(float DeltaTime)
{
	const int32 SolverGeneration = LiveComponent->GetSolver().GetReinitializationGeneration();
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
	const float SurfaceHeightCm = LiveComponent->GetSolver().GetWaterSurfaceHeightAtCell(CellX, CellY);
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
	TrackedSolverGeneration = LiveComponent->GetSolver().GetReinitializationGeneration();
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
				LiveComponent->RegisterExternalMaterialTarget(SharedMaterial.Get());
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
	FTDFluidGrid2DSolver& Solver = LiveComponent->GetMutableSolver();
	if (!Solver.IsInitialized() || !Solver.GetConfig().bEnableWaterLayer)
	{
		return;
	}

	const FVector AnchorLocation = ResolvePlaneAnchorWorldLocation();
	const FVector2D AnchorXY(AnchorLocation.X, AnchorLocation.Y);
	const double RecollectDistanceCm = Solver.GetExtentCm().GetMax() * WaterActorGuideRecollectExtentRatio;
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

	Solver.SetFlowGuides(Guides);
	Solver.SetWaterRegions(Regions);
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
