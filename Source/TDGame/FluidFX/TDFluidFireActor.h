#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "TDFluidFireActor.generated.h"

class FTDFluidGrid2DSolver;
class UStaticMesh;
class UStaticMeshComponent;
class UTDFluidEmberComponent;
class UTDFluidFireLightComponent;
struct FStreamableHandle;

UCLASS()
class TDGAME_API ATDFluidFireActor : public ATDFluidLiveActor
{
	GENERATED_BODY()

public:
	ATDFluidFireActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireSource")
	FTDFluidSourceEmitterSettings SourceEmitter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireSource")
	FName FollowSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireSource")
	bool bShouldHarvestInteractions = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireDownstream")
	TObjectPtr<ATDFluidLiveActor> DownstreamSmokeActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireDownstream")
	FTDFluidDownstreamFeedSettings DownstreamFeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireSource")
	TSoftObjectPtr<UStaticMesh> SourcePropMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireSource")
	FTransform SourcePropRelativeTransform;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FireParts")
	TObjectPtr<UTDFluidFireLightComponent> FireLightComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FireParts")
	TObjectPtr<UTDFluidEmberComponent> EmberComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FireParts")
	TObjectPtr<UStaticMeshComponent> SourcePropMeshComponent;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	static void SelectDownstreamFeedCells(const FTDFluidGrid2DSolver& Solver, const FTDFluidDownstreamFeedSettings& Settings, TArray<FIntPoint>& OutCells);

	FVector2D GetEmitterPlaneVelocity() const;

protected:
	virtual FVector ResolvePlaneAnchorWorldLocation() const override;
	virtual bool ShouldHarvestInteractions() const override { return bShouldHarvestInteractions; }
	virtual FVector2D GetDynamicCenterOffsetPlane() const override;
	virtual void PrepareActorSpecificInputs(float DeltaTime) override;
	virtual void FinishActorSpecificOutputs(float DeltaTime) override;

private:
	const AActor* ResolveFollowTarget() const;
	bool ResolveSourceSocketTransform(FTransform& OutSocketTransform) const;
	void QueueSourceEmitterStamps(const FVector& AnchorWorld);
	void UpdateSourcePropTransform();
	void HandleSourcePropLoaded();
	void FeedDownstreamSmoke();

	TSharedPtr<FStreamableHandle> SourcePropLoadHandle;
	FVector PreviousAnchorWorld = FVector::ZeroVector;
	FVector EmitterWorldVelocity = FVector::ZeroVector;
	double FireSimulationTimeSeconds = 0.0;
	bool bHasPreviousAnchor = false;
};
