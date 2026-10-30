#include "FluidFX/TDFluidUnderwaterSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

static TAutoConsoleVariable<int32> CVarTDFluidUnderwaterSwitch(
	TEXT("TD.FluidFX.UnderwaterSwitch"),
	-1,
	TEXT("Underwater post-process switch: -1 follows Globals.TDFluidUnderwaterSettings, 0 forces off, 1 forces on."),
	ECVF_Default);

const FName UTDFluidUnderwaterSubsystem::UnderwaterVolumeTag(TEXT("TDFluidUnderwater"));
const FName UTDFluidUnderwaterSubsystem::WaterRegionTag(TEXT("TDFluidWaterRegion"));
const FName UTDFluidUnderwaterSubsystem::SurfaceHeightParameterName(TEXT("UnderwaterSurfaceZCm"));

namespace TDFluidUnderwaterSubsystemPrivate
{
	bool IsUnderwaterRegionContaining(const FTDFluidWaterRegion& Region, const FVector2D& LocationXY)
	{
		return LocationXY.X >= Region.MinXY.X && LocationXY.X <= Region.MaxXY.X
			&& LocationXY.Y >= Region.MinXY.Y && LocationXY.Y <= Region.MaxXY.Y;
	}

	bool IsUnderwaterReadbackCandidate(const ATDFluidWaterActor& WaterActor)
	{
		return !WaterActor.IsPooledInactive() && WaterActor.LiveComponent && WaterActor.LiveComponent->IsSimulationAwake();
	}
}

void UTDFluidUnderwaterSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UTDFluidUnderwaterSubsystem::HandleLevelAddedOrRemoved);
	LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UTDFluidUnderwaterSubsystem::HandleLevelAddedOrRemoved);
	CollectUnderwaterVolumes();
}

void UTDFluidUnderwaterSubsystem::Deinitialize()
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
	LevelAddedHandle.Reset();
	LevelRemovedHandle.Reset();
	RestoreDrivenVolumes();
	Super::Deinitialize();
}

void UTDFluidUnderwaterSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const uint64 StartCycles = FPlatformTime::Cycles64();
	TickUnderwaterSwitch(DeltaTime);
	RecordTickCost(StartCycles);
}

TStatId UTDFluidUnderwaterSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDFluidUnderwaterSubsystem, STATGROUP_Tickables);
}

void UTDFluidUnderwaterSubsystem::NotifyUnderwaterVolumesChanged()
{
	CollectUnderwaterVolumes();
}

void UTDFluidUnderwaterSubsystem::SetCameraLocationOverrideForTests(const FVector& CameraLocation)
{
	CameraLocationOverride = CameraLocation;
	bHasPendingQuery = true;
}

void UTDFluidUnderwaterSubsystem::ClearCameraLocationOverrideForTests()
{
	CameraLocationOverride.Reset();
	bHasPendingQuery = true;
}

void UTDFluidUnderwaterSubsystem::RestoreDrivenVolumesForTests()
{
	RestoreDrivenVolumes();
}

bool UTDFluidUnderwaterSubsystem::ResolveSurfaceHeightAt(TConstArrayView<FTDFluidWaterRegion> Regions, const FVector2D& LocationXY, float& OutSurfaceHeightCm)
{
	using namespace TDFluidUnderwaterSubsystemPrivate;

	bool bHasSurface = false;
	OutSurfaceHeightCm = 0.0f;
	for (const FTDFluidWaterRegion& Region : Regions)
	{
		if (!IsUnderwaterRegionContaining(Region, LocationXY))
		{
			continue;
		}

		OutSurfaceHeightCm = bHasSurface ? FMath::Max(OutSurfaceHeightCm, Region.SurfaceHeightCm) : Region.SurfaceHeightCm;
		bHasSurface = true;
	}
	return bHasSurface;
}

bool UTDFluidUnderwaterSubsystem::ResolveUnderwaterState(bool bWasUnderwater, double CameraHeightCm, bool bHasSurface, float SurfaceHeightCm, float SurfaceMarginCm)
{
	if (!bHasSurface)
	{
		return false;
	}
	if (CameraHeightCm < SurfaceHeightCm - SurfaceMarginCm)
	{
		return true;
	}
	if (CameraHeightCm > SurfaceHeightCm + SurfaceMarginCm)
	{
		return false;
	}
	return bWasUnderwater;
}

float UTDFluidUnderwaterSubsystem::StepBlendWeight(float CurrentWeight, bool bIsUnderwater, float DeltaSeconds, float BlendSeconds)
{
	const float TargetWeight = bIsUnderwater ? 1.0f : 0.0f;
	if (BlendSeconds <= 0.0f)
	{
		return TargetWeight;
	}

	const float WeightStep = FMath::Max(DeltaSeconds, 0.0f) / BlendSeconds;
	const float ClampedWeight = FMath::Clamp(CurrentWeight, 0.0f, 1.0f);
	return bIsUnderwater ? FMath::Min(ClampedWeight + WeightStep, 1.0f) : FMath::Max(ClampedWeight - WeightStep, 0.0f);
}

bool UTDFluidUnderwaterSubsystem::ResolveSwitchEnabled(int32 CVarValue, bool bSetting)
{
	if (CVarValue == 0)
	{
		return false;
	}
	if (CVarValue > 0)
	{
		return true;
	}
	return bSetting;
}

bool UTDFluidUnderwaterSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTDFluidUnderwaterSubsystem::HandleLevelAddedOrRemoved(ULevel* ChangedLevel, UWorld* ChangedWorld)
{
	if (!ChangedWorld || ChangedWorld != GetWorld() || ChangedWorld->bIsTearingDown)
	{
		return;
	}

	CollectUnderwaterVolumes();
}

void UTDFluidUnderwaterSubsystem::CollectUnderwaterVolumes()
{
	RestoreDrivenVolumes();
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}

	Settings = FTDFluidUnderwaterSettings();
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFluidUnderwaterSettings::StaticStruct(), &Settings);
	AppliedTableGeneration = ReadTableGeneration();
	AppliedWaterRegistryVersion = ReadWaterRegistryVersion();
	CollectWaterRegions(*World);
	for (TActorIterator<APostProcessVolume> VolumeIt(World); VolumeIt; ++VolumeIt)
	{
		if (VolumeIt->ActorHasTag(UnderwaterVolumeTag))
		{
			DriveTaggedVolume(**VolumeIt);
		}
	}

	Stats.DrivenVolumeCount = DrivenVolumes.Num();
	AppliedSurfaceParameterCm.Reset();
	bHasPendingQuery = true;
	ApplyBlendWeightToVolumes();
}

void UTDFluidUnderwaterSubsystem::CollectWaterRegions(UWorld& World)
{
	using namespace TDFluidUnderwaterSubsystemPrivate;

	WaterRegions.Reset();
	ReadbackWaterActor.Reset();

	TArray<AActor*> RegionActors;
	UGameplayStatics::GetAllActorsWithTag(&World, WaterRegionTag, RegionActors);
	for (const AActor* RegionActor : RegionActors)
	{
		const FBox Bounds = RegionActor->GetComponentsBoundingBox(true);
		if (!Bounds.IsValid)
		{
			continue;
		}

		FTDFluidWaterRegion& Region = WaterRegions.AddDefaulted_GetRef();
		Region.MinXY = FVector2D(Bounds.Min.X, Bounds.Min.Y);
		Region.MaxXY = FVector2D(Bounds.Max.X, Bounds.Max.Y);
		Region.SurfaceHeightCm = static_cast<float>(Bounds.Max.Z);
	}

	for (TActorIterator<ATDFluidWaterActor> WaterIt(&World); WaterIt; ++WaterIt)
	{
		if (IsUnderwaterReadbackCandidate(**WaterIt))
		{
			ReadbackWaterActor = *WaterIt;
			return;
		}
	}
}

void UTDFluidUnderwaterSubsystem::DriveTaggedVolume(APostProcessVolume& Volume)
{
	FTDFluidUnderwaterDrivenVolume& Driven = DrivenVolumes.AddDefaulted_GetRef();
	Driven.Volume = &Volume;
	Driven.OriginalBlendWeight = Volume.BlendWeight;
	Driven.bIsUnbound = Volume.bUnbound != 0;

	const float BoxSurfaceHeightCm = Driven.bIsUnbound ? NoSurfaceHeightCm : static_cast<float>(Volume.GetComponentsBoundingBox(true).Max.Z);
	TArray<FWeightedBlendable>& Blendables = Volume.Settings.WeightedBlendables.Array;
	for (int32 BlendableIndex = 0; BlendableIndex < Blendables.Num(); ++BlendableIndex)
	{
		UMaterialInterface* Material = Cast<UMaterialInterface>(Blendables[BlendableIndex].Object);
		if (!Material)
		{
			continue;
		}

		UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(Material, this);
		if (!MaterialInstance)
		{
			continue;
		}

		UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*MaterialInstance);
		MaterialInstance->SetScalarParameterValue(SurfaceHeightParameterName, BoxSurfaceHeightCm);
		Driven.BlendableIndices.Add(BlendableIndex);
		Driven.OriginalBlendables.Add(Material);
		Driven.MaterialInstances.Add(MaterialInstance);
		Blendables[BlendableIndex].Object = MaterialInstance;
	}
}

void UTDFluidUnderwaterSubsystem::RestoreDrivenVolumes()
{
	for (const FTDFluidUnderwaterDrivenVolume& Driven : DrivenVolumes)
	{
		APostProcessVolume* Volume = Driven.Volume.Get();
		if (!Volume)
		{
			continue;
		}

		TArray<FWeightedBlendable>& Blendables = Volume->Settings.WeightedBlendables.Array;
		for (int32 EntryIndex = 0; EntryIndex < Driven.BlendableIndices.Num(); ++EntryIndex)
		{
			const int32 BlendableIndex = Driven.BlendableIndices[EntryIndex];
			if (Blendables.IsValidIndex(BlendableIndex) && ToRawPtr(Blendables[BlendableIndex].Object) == ToRawPtr(Driven.MaterialInstances[EntryIndex]))
			{
				Blendables[BlendableIndex].Object = Driven.OriginalBlendables[EntryIndex];
			}
		}
		if (Driven.bIsUnbound)
		{
			Volume->BlendWeight = Driven.OriginalBlendWeight;
		}
	}

	DrivenVolumes.Reset();
	Stats.DrivenVolumeCount = 0;
}

void UTDFluidUnderwaterSubsystem::RecollectIfSourcesChanged()
{
	if (ReadWaterRegistryVersion() == AppliedWaterRegistryVersion && ReadTableGeneration() == AppliedTableGeneration)
	{
		return;
	}

	CollectUnderwaterVolumes();
}

void UTDFluidUnderwaterSubsystem::TickUnderwaterSwitch(float DeltaTime)
{
	RecollectIfSourcesChanged();
	if (!ResolveSwitchEnabled(CVarTDFluidUnderwaterSwitch.GetValueOnGameThread(), Settings.bEnableUnderwaterSwitch))
	{
		SuspendUnderwaterSwitch();
		return;
	}

	bIsSwitchSuspended = false;
	QueryAccumulatedSeconds += FMath::Max(DeltaTime, 0.0f);
	const float QueryIntervalSeconds = 1.0f / FMath::Max(Settings.QueryRateHz, UE_KINDA_SMALL_NUMBER);
	if (bHasPendingQuery || QueryAccumulatedSeconds >= QueryIntervalSeconds)
	{
		QueryAccumulatedSeconds = 0.0f;
		bHasPendingQuery = false;
		QueryUnderwaterState();
	}

	Stats.CurrentBlendWeight = StepBlendWeight(Stats.CurrentBlendWeight, Stats.bIsCameraUnderwater, DeltaTime, Settings.BlendSeconds);
	ApplyBlendWeightToVolumes();
}

void UTDFluidUnderwaterSubsystem::SuspendUnderwaterSwitch()
{
	if (bIsSwitchSuspended)
	{
		return;
	}

	bIsSwitchSuspended = true;
	bHasPendingQuery = true;
	Stats.bIsCameraUnderwater = false;
	Stats.CurrentBlendWeight = 0.0f;
	ApplyBlendWeightToVolumes();
}

void UTDFluidUnderwaterSubsystem::QueryUnderwaterState()
{
	FVector CameraLocation;
	if (!ResolveCameraLocation(CameraLocation))
	{
		return;
	}

	float SurfaceHeightCm = 0.0f;
	const bool bHasSurface = ResolveSurfaceHeightAt(WaterRegions, FVector2D(CameraLocation.X, CameraLocation.Y), SurfaceHeightCm);
	if (bHasSurface && Settings.bUseWaterLayerReadback)
	{
		SurfaceHeightCm += SampleReadbackRippleHeight(CameraLocation);
	}

	const bool bWasUnderwater = Stats.bIsCameraUnderwater;
	Stats.bIsCameraUnderwater = ResolveUnderwaterState(bWasUnderwater, CameraLocation.Z, bHasSurface, SurfaceHeightCm, Settings.SurfaceMarginCm);
	Stats.bHasSurface = bHasSurface;
	Stats.LastSurfaceHeightCm = bHasSurface ? SurfaceHeightCm : 0.0f;
	++Stats.QueryCount;
	Stats.SwitchCount += Stats.bIsCameraUnderwater != bWasUnderwater ? 1 : 0;
}

bool UTDFluidUnderwaterSubsystem::ResolveCameraLocation(FVector& OutCameraLocation) const
{
	if (CameraLocationOverride.IsSet())
	{
		OutCameraLocation = CameraLocationOverride.GetValue();
		return true;
	}

	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!CameraManager)
	{
		return false;
	}

	OutCameraLocation = CameraManager->GetCameraLocation();
	return true;
}

float UTDFluidUnderwaterSubsystem::SampleReadbackRippleHeight(const FVector& CameraLocation) const
{
	using namespace TDFluidUnderwaterSubsystemPrivate;

	const ATDFluidWaterActor* WaterActor = ReadbackWaterActor.Get();
	if (!WaterActor || !IsUnderwaterReadbackCandidate(*WaterActor))
	{
		return 0.0f;
	}

	FTDFluidCellSample Sample;
	return WaterActor->LiveComponent->SampleFluidAtWorldLocation(CameraLocation, Sample) ? Sample.RippleHeightCm : 0.0f;
}

void UTDFluidUnderwaterSubsystem::ApplyBlendWeightToVolumes()
{
	const float SurfaceParameterCm = Stats.bHasSurface ? Stats.LastSurfaceHeightCm : NoSurfaceHeightCm;
	const bool bShouldWriteSurface = !AppliedSurfaceParameterCm.IsSet() || AppliedSurfaceParameterCm.GetValue() != SurfaceParameterCm;
	AppliedSurfaceParameterCm = SurfaceParameterCm;
	for (const FTDFluidUnderwaterDrivenVolume& Driven : DrivenVolumes)
	{
		APostProcessVolume* Volume = Driven.Volume.Get();
		if (!Driven.bIsUnbound || !Volume)
		{
			continue;
		}

		Volume->BlendWeight = Stats.CurrentBlendWeight;
		if (!bShouldWriteSurface)
		{
			continue;
		}

		for (UMaterialInstanceDynamic* MaterialInstance : Driven.MaterialInstances)
		{
			if (MaterialInstance)
			{
				MaterialInstance->SetScalarParameterValue(SurfaceHeightParameterName, SurfaceParameterCm);
			}
		}
	}
}

void UTDFluidUnderwaterSubsystem::RecordTickCost(uint64 StartCycles)
{
	const double TickMilliseconds = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles);
	++Stats.TickCount;
	Stats.TotalTickMilliseconds += TickMilliseconds;
	Stats.MaxTickMilliseconds = FMath::Max(Stats.MaxTickMilliseconds, static_cast<float>(TickMilliseconds));
}

int32 UTDFluidUnderwaterSubsystem::ReadWaterRegistryVersion() const
{
	const UWorld* World = GetWorld();
	const UTDFluidLiveSubsystem* LiveSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	return LiveSubsystem ? LiveSubsystem->GetWaterRegistryVersion() : 0;
}

int32 UTDFluidUnderwaterSubsystem::ReadTableGeneration()
{
	const UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	return TuningSubsystem ? TuningSubsystem->GetTableGeneration() : 0;
}
