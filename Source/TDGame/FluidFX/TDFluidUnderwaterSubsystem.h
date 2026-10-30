#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterPropTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDFluidUnderwaterSubsystem.generated.h"

class APostProcessVolume;
class ATDFluidWaterActor;
class ULevel;
class UMaterialInstanceDynamic;

USTRUCT()
struct FTDFluidUnderwaterDrivenVolume
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<APostProcessVolume> Volume;

	UPROPERTY(Transient)
	TArray<int32> BlendableIndices;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> OriginalBlendables;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MaterialInstances;

	float OriginalBlendWeight = 1.0f;
	bool bIsUnbound = false;
};

UCLASS()
class TDGAME_API UTDFluidUnderwaterSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static const FName UnderwaterVolumeTag;
	static const FName WaterRegionTag;
	static const FName SurfaceHeightParameterName;
	static constexpr float NoSurfaceHeightCm = 1.0e7f;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !DrivenVolumes.IsEmpty(); }
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void NotifyUnderwaterVolumesChanged();

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidUnderwaterStats GetUnderwaterStats() const { return Stats; }

	void SetCameraLocationOverrideForTests(const FVector& CameraLocation);
	void ClearCameraLocationOverrideForTests();
	void RestoreDrivenVolumesForTests();

	static bool ResolveSurfaceHeightAt(TConstArrayView<FTDFluidWaterRegion> Regions, const FVector2D& LocationXY, float& OutSurfaceHeightCm);
	static bool ResolveUnderwaterState(bool bWasUnderwater, double CameraHeightCm, bool bHasSurface, float SurfaceHeightCm, float SurfaceMarginCm);
	static float StepBlendWeight(float CurrentWeight, bool bIsUnderwater, float DeltaSeconds, float BlendSeconds);
	static bool ResolveSwitchEnabled(int32 CVarValue, bool bSetting);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleLevelAddedOrRemoved(ULevel* ChangedLevel, UWorld* ChangedWorld);
	void CollectUnderwaterVolumes();
	void CollectWaterRegions(UWorld& World);
	void DriveTaggedVolume(APostProcessVolume& Volume);
	void RestoreDrivenVolumes();
	void RecollectIfSourcesChanged();
	void TickUnderwaterSwitch(float DeltaTime);
	void SuspendUnderwaterSwitch();
	void QueryUnderwaterState();
	bool ResolveCameraLocation(FVector& OutCameraLocation) const;
	float SampleReadbackRippleHeight(const FVector& CameraLocation) const;
	void ApplyBlendWeightToVolumes();
	void RecordTickCost(uint64 StartCycles);
	int32 ReadWaterRegistryVersion() const;
	static int32 ReadTableGeneration();

	UPROPERTY(Transient)
	TArray<FTDFluidUnderwaterDrivenVolume> DrivenVolumes;

	TArray<FTDFluidWaterRegion> WaterRegions;
	TWeakObjectPtr<ATDFluidWaterActor> ReadbackWaterActor;
	FTDFluidUnderwaterSettings Settings;
	FTDFluidUnderwaterStats Stats;
	TOptional<FVector> CameraLocationOverride;
	TOptional<float> AppliedSurfaceParameterCm;
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelRemovedHandle;
	float QueryAccumulatedSeconds = 0.0f;
	int32 AppliedWaterRegistryVersion = INDEX_NONE;
	int32 AppliedTableGeneration = INDEX_NONE;
	bool bHasPendingQuery = true;
	bool bIsSwitchSuspended = false;
};
