#pragma once

#include "CoreMinimal.h"
#include "TDFluidOutputQuality.generated.h"

class UMaterialInstanceDynamic;
struct FTDFluidMaterialLookEntry;

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidOutputQualitySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OutputQuality", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float Level0LoopScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OutputQuality", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float Level1LoopScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OutputQuality", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float Level2LoopScale = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OutputQuality", meta = (ClampMin = "1.0", ClampMax = "16.0"))
	float MinimumLoopScalar = 2.0f;
};

class TDGAME_API FTDFluidOutputQuality
{
public:
	static constexpr int32 HighestLevel = 3;

	static TConstArrayView<FName> GetLoopScalarNames();
	static int32 ResolveLevel();
	static float ResolveLoopScale(int32 Level, const FTDFluidOutputQualitySettings& Settings);
	static float ComputeScaledLoopValue(float Base, float Scale, float Minimum);
	static int32 ApplyToMaterial(UMaterialInstanceDynamic& Material, const FTDFluidMaterialLookEntry* Look);
};
