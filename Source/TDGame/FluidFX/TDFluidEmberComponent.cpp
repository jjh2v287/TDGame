#include "FluidFX/TDFluidEmberComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "FluidFX/TDFluidBandLimitedNoise.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"

namespace
{
	constexpr int32 MaxEmberPoolSize = 512;
	constexpr int32 MaxRejectionTrialsPerEmber = 8;
	constexpr int32 EmberSortPriority = 2;
	constexpr float FadeInverseSeconds = 4.0f;
	constexpr float BurstMinSpeedRatio = 0.5f;
	constexpr float MinStretchSpeedCmPerSec = 1.0f;
}

UTDFluidEmberComponent::UTDFluidEmberComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	NumCustomDataFloats = 2;
	SetMobility(EComponentMobility::Movable);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	SetVisibleInRayTracing(false);
	SetAffectDistanceFieldLighting(false);
	SetCanEverAffectNavigation(false);
	SetTranslucentSortPriority(EmberSortPriority);
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
}

void UTDFluidEmberComponent::BeginPlay()
{
	Super::BeginPlay();
	RequestAsyncAssetLoads();
}

void UTDFluidEmberComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AssetLoadHandle.IsValid())
	{
		AssetLoadHandle->CancelHandle();
		AssetLoadHandle.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void UTDFluidEmberComponent::RequestAsyncAssetLoads()
{
	TArray<FSoftObjectPath> AssetsToLoad;
	if (!EmberMeshAsset.IsNull())
	{
		AssetsToLoad.Add(EmberMeshAsset.ToSoftObjectPath());
	}
	if (!EmberMaterialAsset.IsNull())
	{
		AssetsToLoad.Add(EmberMaterialAsset.ToSoftObjectPath());
	}

	if (AssetsToLoad.IsEmpty())
	{
		return;
	}

	AssetLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &UTDFluidEmberComponent::HandleAssetsLoaded));
}

void UTDFluidEmberComponent::HandleAssetsLoaded()
{
	if (UStaticMesh* LoadedMesh = EmberMeshAsset.Get())
	{
		SetStaticMesh(LoadedMesh);
	}

	if (UMaterialInterface* LoadedMaterial = EmberMaterialAsset.Get())
	{
		SetMaterial(0, LoadedMaterial);
	}
}

void UTDFluidEmberComponent::EnsureEmberPool()
{
	const int32 PoolSize = FMath::Clamp(MaxEmbers, 1, MaxEmberPoolSize);
	if (!bIsRandomStateReady)
	{
		RandomState = TDFluidDeterministic::MakeNonZeroState(RandomSeed);
		bIsRandomStateReady = true;
	}

	if (Positions.Num() == PoolSize && GetInstanceCount() == PoolSize)
	{
		return;
	}

	ClearInstances();
	Positions.Init(FVector::ZeroVector, PoolSize);
	Velocities.Init(FVector::ZeroVector, PoolSize);
	Heats.Init(0.0f, PoolSize);
	Ages.Init(0.0f, PoolSize);
	Lifetimes.Init(0.0f, PoolSize);
	SlowSeconds.Init(0.0f, PoolSize);
	InstanceTransforms.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), PoolSize);
	AliveEmberCount = 0;
	for (int32 SlotIndex = 0; SlotIndex < PoolSize; ++SlotIndex)
	{
		AddInstance(InstanceTransforms[SlotIndex], true);
	}
}

int32 UTDFluidEmberComponent::FindFreeEmberSlot() const
{
	for (int32 SlotIndex = 0; SlotIndex < Lifetimes.Num(); ++SlotIndex)
	{
		if (Lifetimes[SlotIndex] <= 0.0f)
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

void UTDFluidEmberComponent::ActivateEmber(int32 SlotIndex, const FVector& Position, const FVector& Velocity)
{
	const float LifetimeBlend = TDFluidDeterministic::NextUnitFloat(RandomState);
	Positions[SlotIndex] = Position;
	Velocities[SlotIndex] = Velocity;
	Heats[SlotIndex] = 1.0f;
	Ages[SlotIndex] = 0.0f;
	Lifetimes[SlotIndex] = FMath::Max(FMath::Lerp(static_cast<float>(LifetimeRangeSeconds.X), static_cast<float>(LifetimeRangeSeconds.Y), LifetimeBlend), UE_KINDA_SMALL_NUMBER);
	SlowSeconds[SlotIndex] = 0.0f;
	++AliveEmberCount;
}

void UTDFluidEmberComponent::StepEmbers(const UTDFluidLiveComponent& LiveComponent, float DeltaTime)
{
	if (DeltaTime <= 0.0f || !GetStaticMesh() || !LiveComponent.GetSolver().IsInitialized())
	{
		return;
	}

	EnsureEmberPool();
	SpawnEmbersFromDensity(LiveComponent, DeltaTime);
	AdvanceEmbers(LiveComponent, DeltaTime);
	WriteInstances();
}

void UTDFluidEmberComponent::SpawnEmbersFromDensity(const UTDFluidLiveComponent& LiveComponent, float DeltaTime)
{
	float SpawnRatePerSec = SpawnPerSecond;
	if (SpawnPerDensityArea > 0.0f)
	{
		FVector2D DensityCentroid;
		float DensityAreaCm2 = 0.0f;
		LiveComponent.ComputeDensityCentroidPlane(true, DensityCentroid, DensityAreaCm2);
		SpawnRatePerSec += SpawnPerDensityArea * DensityAreaCm2;
	}

	SpawnCarry += SpawnRatePerSec * DeltaTime;
	const int32 SpawnCount = FMath::FloorToInt(SpawnCarry);
	SpawnCarry -= static_cast<float>(SpawnCount);

	const FTDFluidGrid2DSolver& Solver = LiveComponent.GetSolver();
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent.GetPlaneBasis();
	const int32 Resolution = Solver.GetResolution();
	const FVector2D CellSizeCm = Solver.GetExtentCm() / static_cast<double>(Resolution);

	for (int32 SpawnIndex = 0; SpawnIndex < SpawnCount; ++SpawnIndex)
	{
		const int32 SlotIndex = FindFreeEmberSlot();
		if (SlotIndex == INDEX_NONE)
		{
			return;
		}

		for (int32 TrialIndex = 0; TrialIndex < MaxRejectionTrialsPerEmber; ++TrialIndex)
		{
			const int32 CellX = FMath::Min(FMath::FloorToInt(TDFluidDeterministic::NextUnitFloat(RandomState) * Resolution), Resolution - 1);
			const int32 CellY = FMath::Min(FMath::FloorToInt(TDFluidDeterministic::NextUnitFloat(RandomState) * Resolution), Resolution - 1);
			const FTDFluidCellSample Cell = Solver.SampleAtCell(CellX, CellY);
			const float AcceptanceDraw = TDFluidDeterministic::NextUnitFloat(RandomState);
			if (AcceptanceDraw >= Cell.SelectiveDensity)
			{
				continue;
			}

			const double JitterX = (TDFluidDeterministic::NextUnitFloat(RandomState) - 0.5) * CellSizeCm.X;
			const double JitterY = (TDFluidDeterministic::NextUnitFloat(RandomState) - 0.5) * CellSizeCm.Y;
			const double DepthJitter = (TDFluidDeterministic::NextUnitFloat(RandomState) * 2.0 - 1.0) * DepthJitterCm;
			const FVector2D PlanePoint = Solver.CellToWorldCenter(CellX, CellY) + FVector2D(JitterX, JitterY);
			ActivateEmber(SlotIndex, LiveComponent.UnprojectSolverPlaneToWorld(PlanePoint, DepthJitter), PlaneBasis.UnprojectVector(Cell.VelocityCmPerSec));
			break;
		}
	}
}

void UTDFluidEmberComponent::AdvanceEmbers(const UTDFluidLiveComponent& LiveComponent, float DeltaTime)
{
	const FTDFluidGrid2DSolver& Solver = LiveComponent.GetSolver();
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent.GetPlaneBasis();
	const float DragBlend = 1.0f - FMath::Exp(-FluidDragPerSec * DeltaTime);

	for (int32 SlotIndex = 0; SlotIndex < Lifetimes.Num(); ++SlotIndex)
	{
		if (Lifetimes[SlotIndex] <= 0.0f)
		{
			continue;
		}

		FVector FluidVelocity = FVector::ZeroVector;
		FTDFluidCellSample Cell;
		if (Solver.SampleAtWorldPosition(LiveComponent.ProjectWorldToSolverPlane(Positions[SlotIndex]), Cell))
		{
			FluidVelocity = PlaneBasis.UnprojectVector(Cell.VelocityCmPerSec);
		}

		const float Heat = Heats[SlotIndex];
		FVector Velocity = Velocities[SlotIndex];
		Velocity += (FluidVelocity - Velocity) * DragBlend;
		Velocity.Z += (BuoyancyCmPerSec2 * Heat - GravityCmPerSec2 * (1.0f - Heat)) * DeltaTime;
		Velocities[SlotIndex] = Velocity;
		Positions[SlotIndex] += Velocity * DeltaTime;
		Heats[SlotIndex] = Heat - CoolingPerSec * DeltaTime;
		Ages[SlotIndex] += DeltaTime;
		SlowSeconds[SlotIndex] = Velocity.Size() < MinSpeedKillCmPerSec ? SlowSeconds[SlotIndex] + DeltaTime : 0.0f;

		const bool bShouldDie = Ages[SlotIndex] >= Lifetimes[SlotIndex] || Heats[SlotIndex] <= 0.0f || SlowSeconds[SlotIndex] >= SlowKillSeconds;
		if (!bShouldDie)
		{
			continue;
		}

		Lifetimes[SlotIndex] = 0.0f;
		--AliveEmberCount;
	}
}

void UTDFluidEmberComponent::WriteInstances()
{
	const int32 PoolSize = Lifetimes.Num();
	for (int32 SlotIndex = 0; SlotIndex < PoolSize; ++SlotIndex)
	{
		const bool bIsAlive = Lifetimes[SlotIndex] > 0.0f;
		float CustomData[2] = { 0.0f, 0.0f };
		if (!bIsAlive)
		{
			InstanceTransforms[SlotIndex] = FTransform(FQuat::Identity, Positions[SlotIndex], FVector::ZeroVector);
			SetCustomData(SlotIndex, MakeArrayView(CustomData, 2), false);
			continue;
		}

		const FVector& Velocity = Velocities[SlotIndex];
		const float Speed = static_cast<float>(Velocity.Size());
		const FQuat Rotation = Speed > MinStretchSpeedCmPerSec ? FRotationMatrix::MakeFromX(Velocity).ToQuat() : FQuat::Identity;
		const FVector Scale(BaseScale * (1.0f + StretchPerCmPerSec * Speed), BaseScale, BaseScale);
		InstanceTransforms[SlotIndex] = FTransform(Rotation, Positions[SlotIndex], Scale);
		CustomData[0] = FMath::Clamp(Heats[SlotIndex], 0.0f, 1.0f);
		CustomData[1] = FMath::Clamp(FMath::Min(Heats[SlotIndex], Lifetimes[SlotIndex] - Ages[SlotIndex]) * FadeInverseSeconds, 0.0f, 1.0f);
		SetCustomData(SlotIndex, MakeArrayView(CustomData, 2), false);
	}

	BatchUpdateInstancesTransforms(0, InstanceTransforms, true, false, false);
	MarkRenderStateDirty();
}

void UTDFluidEmberComponent::SpawnBurst(FVector Center, int32 Count, float SpeedCmPerSec)
{
	EnsureEmberPool();
	for (int32 BurstIndex = 0; BurstIndex < Count; ++BurstIndex)
	{
		const int32 SlotIndex = FindFreeEmberSlot();
		if (SlotIndex == INDEX_NONE)
		{
			return;
		}

		const float DirectionZ = TDFluidDeterministic::NextUnitFloat(RandomState) * 2.0f - 1.0f;
		const float DirectionAngle = TDFluidDeterministic::NextUnitFloat(RandomState) * UE_TWO_PI;
		const float RingRadius = FMath::Sqrt(FMath::Max(0.0f, 1.0f - DirectionZ * DirectionZ));
		const FVector Direction(RingRadius * FMath::Cos(DirectionAngle), RingRadius * FMath::Sin(DirectionAngle), DirectionZ);
		const float SpeedRatio = BurstMinSpeedRatio + (1.0f - BurstMinSpeedRatio) * TDFluidDeterministic::NextUnitFloat(RandomState);
		ActivateEmber(SlotIndex, Center, Direction * SpeedCmPerSec * SpeedRatio);
	}
}

float UTDFluidEmberComponent::GetMeanEmberHeat() const
{
	if (AliveEmberCount <= 0)
	{
		return 0.0f;
	}

	double HeatSum = 0.0;
	for (int32 SlotIndex = 0; SlotIndex < Lifetimes.Num(); ++SlotIndex)
	{
		if (Lifetimes[SlotIndex] > 0.0f)
		{
			HeatSum += Heats[SlotIndex];
		}
	}

	return static_cast<float>(HeatSum / AliveEmberCount);
}

int32 UTDFluidEmberComponent::ComputeEmberStateHash() const
{
	uint32 Hash = 0;
	for (int32 SlotIndex = 0; SlotIndex < Lifetimes.Num(); ++SlotIndex)
	{
		if (Lifetimes[SlotIndex] <= 0.0f)
		{
			continue;
		}

		Hash = FCrc::MemCrc32(&Positions[SlotIndex], sizeof(FVector), Hash);
		Hash = FCrc::MemCrc32(&Velocities[SlotIndex], sizeof(FVector), Hash);
		Hash = FCrc::MemCrc32(&Heats[SlotIndex], sizeof(float), Hash);
	}

	return static_cast<int32>(TDFluidDeterministic::CombineHash(Hash, static_cast<uint32>(AliveEmberCount)));
}
