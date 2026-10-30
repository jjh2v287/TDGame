#include "FluidFX/TDFluidWaterActor.h"
#include "CollisionQueryParams.h"
#include "Engine/AssetManager.h"
#include "Engine/HitResult.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidDenseWaterCacheAsset.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "TDGame.h"

namespace
{
	constexpr float TDFluidWaterActorDenseWaterBakeStepSeconds = 1.0f / 60.0f;
	constexpr float TDFluidWaterActorDenseWaterBakeMaximumSeconds = 600.0f;
	constexpr int32 TDFluidWaterActorDenseWaterBakeMaximumResolution = 256;
}

void ATDFluidWaterActor::RequestDenseWaterCacheLoad()
{
	if (DenseWaterCache.IsNull() || !LiveComponent || bIsDormantDuplicate)
	{
		return;
	}

	if (DenseWaterCache.Get())
	{
		ApplyLoadedDenseWaterCache();
		return;
	}

	bIsWaitingForDenseWaterCache = true;
	DenseWaterCacheLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		DenseWaterCache.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &ATDFluidWaterActor::ApplyLoadedDenseWaterCache));
	if (!DenseWaterCacheLoadHandle.IsValid())
	{
		ApplyLoadedDenseWaterCache();
	}
}

void ATDFluidWaterActor::ApplyLoadedDenseWaterCache()
{
	const bool bWasWaiting = bIsWaitingForDenseWaterCache;
	bIsWaitingForDenseWaterCache = false;
	DenseWaterCacheLoadHandle.Reset();
	if (!LiveComponent)
	{
		return;
	}

	const UTDFluidDenseWaterCacheAsset* LoadedCache = DenseWaterCache.Get();
	const bool bHasSeeded = LoadedCache && LiveComponent->SeedDenseWaterState(*LoadedCache);
	if (!bHasSeeded && !bHasWarnedDenseWaterCache)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDFluidWaterActor %s starts dense water empty because cache %s is missing or unusable"), *GetName(), *DenseWaterCache.ToString());
		bHasWarnedDenseWaterCache = true;
	}

	if (bWasWaiting && !bIsDormantDuplicate && !bIsSleepingForMissingWater)
	{
		LiveComponent->SetSimulationAwake(true);
	}
}

bool ATDFluidWaterActor::BakeDenseWaterCache(UTDFluidDenseWaterCacheAsset* TargetCache, float SimulatedSeconds)
{
	UWorld* World = GetWorld();
	if (!TargetCache || !World || !LiveComponent || !FMath::IsFinite(SimulatedSeconds) || SimulatedSeconds <= 0.0f)
	{
		return false;
	}

	FTDFluidSimulationConfig BakeConfig = LiveComponent->ActivePreset ? LiveComponent->ActivePreset->ResolveEffectiveConfig() : LiveComponent->SimulationConfig;
	if (!BakeConfig.bEnableDenseWater || !BakeConfig.bEnableWaterLayer || !FTDFluidGrid2DSolver::IsDenseWaterConsoleEnabled())
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDFluidWaterActor %s cannot bake a dense water cache because dense water is off"), *GetName());
		return false;
	}

	const int32 RequestedResolution = BakeConfig.CpuFallbackResolution > 0 ? BakeConfig.CpuFallbackResolution : BakeConfig.GridResolution;
	BakeConfig.GridResolution = FMath::Clamp(RequestedResolution, 1, TDFluidWaterActorDenseWaterBakeMaximumResolution);
	BakeConfig.SolverBackend = ETDFluidSolverBackend::Cpu;

	const FVector AnchorLocation = ResolvePlaneAnchorWorldLocation();
	FTDFluidGrid2DSolver BakeSolver;
	BakeSolver.Initialize(BakeConfig, FVector2D(AnchorLocation.X, AnchorLocation.Y));
	if (!BakeSolver.IsInitialized())
	{
		return false;
	}

	RebuildWaterRegistryCaches();

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDFluidDenseWaterBake), false, this);
	AddTerrainTraceIgnoredActors(QueryParams);
	const FCollisionObjectQueryParams ObjectQueryParams(ECC_WorldStatic);
	const FTDFluidTerrainHeightSettings& HeightSettings = GetTerrainHeightSettings();
	const float BaseHeightCm = static_cast<float>(GetActorLocation().Z);
	const int32 Resolution = BakeSolver.GetResolution();
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FVector2D CellCenter = BakeSolver.CellToWorldCenter(CellX, CellY);
			const FVector TraceStart(CellCenter.X, CellCenter.Y, BaseHeightCm + HeightSettings.TraceStartHeightCm);
			const FVector TraceEnd(CellCenter.X, CellCenter.Y, BaseHeightCm - HeightSettings.TraceEndDepthCm);
			FHitResult Hit;
			const bool bDidHit = World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjectQueryParams, QueryParams);
			BakeSolver.SetTerrainElevationAtCell(CellX, CellY, bDidHit ? ApplyTerrainHeightOps(Hit.ImpactPoint.Z, HeightSettings) : BaseHeightCm);
		}
	}
	BakeSolver.ComputeTerrainSlopeFromElevation();
	BakeSolver.SetWaterRegions(CachedWaterRegions);
	BakeSolver.RasterizeSdfObstacles(CachedWaterObstacles);

	const float ClampedSeconds = FMath::Min(SimulatedSeconds, TDFluidWaterActorDenseWaterBakeMaximumSeconds);
	const int32 StepCount = FMath::Max(FMath::CeilToInt32(ClampedSeconds / TDFluidWaterActorDenseWaterBakeStepSeconds), 1);
	for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
	{
		BakeSolver.StepSimulation(TDFluidWaterActorDenseWaterBakeStepSeconds);
	}

	FTDFluidDenseWaterSeed Seed;
	if (!BakeSolver.ExportDenseWaterState(Seed))
	{
		return false;
	}

	TargetCache->Modify();
	TargetCache->SetFromSeed(Seed);
	TargetCache->TerrainReferenceZCm = BaseHeightCm;
	TargetCache->SimulatedSeconds = static_cast<float>(StepCount) * TDFluidWaterActorDenseWaterBakeStepSeconds;
	TargetCache->SourceLevel = World->GetOutermost()->GetName();
	TargetCache->MarkPackageDirty();
	return true;
}
