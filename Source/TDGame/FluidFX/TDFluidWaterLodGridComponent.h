#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidWaterLodGridComponent.generated.h"

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidWaterLodGridStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "WaterLodGrid")
	int32 PatchCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "WaterLodGrid")
	int32 VertexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "WaterLodGrid")
	int32 MaxLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "WaterLodGrid")
	float EffectiveBaseRangeCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "WaterLodGrid")
	float LastSelectionMilliseconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "WaterLodGrid")
	int32 SelectionCount = 0;
};

struct FTDFluidWaterLodPatch
{
	FVector2D MinCornerCm = FVector2D::ZeroVector;
	double SizeCm = 0.0;
	int32 Level = 0;

	bool operator==(const FTDFluidWaterLodPatch& Other) const
	{
		return MinCornerCm == Other.MinCornerCm && SizeCm == Other.SizeCm && Level == Other.Level;
	}
};

struct FTDFluidWaterLodSelectionInput
{
	FVector ObserverWorld = FVector::ZeroVector;
	FBox2D CoverageCm = FBox2D(ForceInit);
	double SurfaceHeightCm = 0.0;
	FTDFluidWaterLodGridSettings Settings;
	bool bSkipsCrackFreeRange = false;
};

struct FTDFluidWaterLodSelectionResult
{
	TArray<FTDFluidWaterLodPatch> Patches;
	double EffectiveBaseRangeCm = 0.0;
	bool bReachedBudget = false;
};

UCLASS(ClassGroup = (Custom))
class TDGAME_API UTDFluidWaterLodGridComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UTDFluidWaterLodGridComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static constexpr double PatchMeshSizeCm = 100.0;

	static FTDFluidWaterLodGridSettings SanitizeSettings(const FTDFluidWaterLodGridSettings& InSettings);

	static double ComputeCrackFreeMinimumRangeCm(const FTDFluidWaterLodGridSettings& Settings);

	static FTDFluidWaterLodSelectionResult SelectPatches(const FTDFluidWaterLodSelectionInput& Input);

	static FVector2D ComputeMorphedVertexXY(const FVector2D& VertexWorldXY, double SpacingCm, const FVector& ObserverWorld, double SurfaceHeightCm, double BaseRangeCm, const FTDFluidWaterLodGridSettings& Settings);

	static bool ResolveLodGridEnabled(bool bIsPresetEnabled);

	void ApplyPatches(const FTDFluidWaterLodSelectionResult& Result, double SurfaceHeightCm, int32 PatchQuads);

	void ClearPatches();
};
