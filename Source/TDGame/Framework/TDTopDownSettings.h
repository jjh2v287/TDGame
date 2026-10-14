#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "TDTopDownSettings.generated.h"

class UMaterialParameterCollection;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "TD Top Down"))
class TDGAME_API UTDTopDownSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTDTopDownSettings();

	UPROPERTY(Config, EditAnywhere, Category = "Occlusion")
	TSoftObjectPtr<UMaterialParameterCollection> TopDownParameterCollection;

	UPROPERTY(Config, EditAnywhere, Category = "Occlusion")
	bool bEnableOcclusionCutout = true;

	UPROPERTY(Config, EditAnywhere, Category = "Occlusion", meta = (ClampMin = "0.0"))
	float OcclusionRadiusCm = 260.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Occlusion", meta = (ClampMin = "0.0"))
	float OcclusionSoftnessCm = 120.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Occlusion", meta = (ClampMin = "0.0"))
	float OcclusionKeepBaseCm = 40.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Occlusion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OcclusionMaxCut = 0.92f;

	UPROPERTY(Config, EditAnywhere, Category = "Occlusion", meta = (ClampMin = "0.0"))
	float OcclusionFocusHeightCm = 90.0f;
};
