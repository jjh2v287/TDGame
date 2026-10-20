#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "GameFramework/Actor.h"
#include "TDFluidRiverActor.generated.h"

class UMaterialInterface;
class USplineComponent;
class USplineMeshComponent;
class UStaticMesh;
struct FStreamableHandle;

UCLASS()
class TDGAME_API ATDFluidRiverActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidRiverActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "River")
	TObjectPtr<USplineComponent> RiverSpline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River", meta = (ClampMin = "10.0"))
	float RiverWidthCm = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River", meta = (ClampMin = "0.0"))
	float FlowSpeedCmPerSec = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River")
	bool bConserveDischargeAcrossWidth = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River", meta = (ClampMin = "10.0"))
	float GuideSampleSpacingCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River")
	bool bBuildRibbonSurface = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River")
	TSoftObjectPtr<UStaticMesh> RibbonMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River")
	TSoftObjectPtr<UMaterialInterface> RibbonMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River", meta = (ClampMin = "50.0"))
	float RibbonSegmentLengthCm = 400.0f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintCallable, Category = "River")
	void BuildFlowGuide(FTDFluidFlowGuide& OutGuide) const;

	UFUNCTION(BlueprintPure, Category = "River")
	int32 GetRibbonSegmentCount() const { return RibbonSegments.Num(); }

private:
	float ResolveFlowSpeedForWidthScale(float WidthScale) const;
	void HandleRibbonAssetsLoaded();
	void RebuildRibbonSegments(UStaticMesh& Mesh, UMaterialInterface* Material);
	void ClearRibbonSegments();

	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> RibbonSegments;

	TSharedPtr<FStreamableHandle> RibbonLoadHandle;
};
