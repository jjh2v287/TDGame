#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "TDFluidPresetTuningTestTypes.generated.h"

USTRUCT()
struct FTDFluidPresetTuningTestFormats
{
	GENERATED_BODY()

	UPROPERTY()
	float Scale = 1.0f;

	UPROPERTY()
	TArray<float> Weights;

	UPROPERTY()
	TArray<FVector2D> Points;

	UPROPERTY()
	FName Label = NAME_None;

	UPROPERTY()
	TSoftObjectPtr<UObject> AssetReference;

	UPROPERTY()
	FSoftObjectPath AssetPath;
};

USTRUCT()
struct FTDFluidPresetTuningTestGlobals
{
	GENERATED_BODY()

	UPROPERTY()
	float GlobalScale = 1.0f;

	UPROPERTY()
	int32 GlobalCount = 2;
};
