#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Templates/SharedPointer.h"
#include "TDFluidSimStateCacheAsset.generated.h"

struct FTDFluidSimStateSnapshot;

UENUM()
enum class ETDFluidSimStateCacheBackend : uint8
{
	Cpu,
	Gpu
};

USTRUCT()
struct FTDFluidSimStateCacheChannel
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	UPROPERTY()
	uint8 PixelFormat = 0;

	UPROPERTY()
	TArray<uint8> Bytes;
};

UCLASS(BlueprintType)
class TDGAME_API UTDFluidSimStateCacheAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	int32 LayoutVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	ETDFluidSimStateCacheBackend Backend = ETDFluidSimStateCacheBackend::Cpu;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	int32 Resolution = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D SimExtentCm = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D CenterPlaneCm = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	ETDFluidAreaAddressMode AreaAddressMode = ETDFluidAreaAddressMode::Clamp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	ETDFluidSimMode SimMode = ETDFluidSimMode::FluidSim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	bool bUsesHeatChannel = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	bool bUsesWaterLayer = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	bool bUsesDenseWater = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	double SimulationTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	int32 GpuResolutionTier = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	int32 GpuLayoutFlags = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	ETDFluidPresetKind PresetKind = ETDFluidPresetKind::Custom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FString SourceLevel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FString SourceActorLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	int64 SourceChecksum = 0;

	UPROPERTY()
	TArray<FTDFluidSimStateCacheChannel> Channels;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	bool IsUsable() const;

	TSharedRef<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> MakeSnapshot() const;
	void SetFromSnapshot(const FTDFluidSimStateSnapshot& Snapshot);
	int64 ComputeChecksum() const;
};
