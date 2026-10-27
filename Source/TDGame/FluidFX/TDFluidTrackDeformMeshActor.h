#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"
#include "FluidFX/TDFluidTypes.h"
#include "GameFramework/Actor.h"
#include "TDFluidTrackDeformMeshActor.generated.h"

class ATDFluidLiveActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class TDGAME_API ATDFluidTrackDeformMeshActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidTrackDeformMeshActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackDeform")
	TObjectPtr<ATDFluidLiveActor> SourcePainterActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackDeform")
	TObjectPtr<AActor> FollowTargetActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackDeform")
	TSoftObjectPtr<UStaticMesh> GridMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackDeform")
	TSoftObjectPtr<UMaterialInterface> DeformMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TrackDeform")
	TObjectPtr<UStaticMeshComponent> DeformMeshComponent;

	UFUNCTION(BlueprintPure, Category = "TrackDeform")
	bool IsDeformMeshShown() const;

	UFUNCTION(BlueprintPure, Category = "TrackDeform")
	FVector2D GetDeformCenterCm() const { return DeformCenterCm; }

	void UpdateDeformMesh();

	static double ComputeGridSpacingCm(double ExtentCm, int32 VertexCount);
	static FVector2D SnapDeformCenterCm(const FVector2D& TargetCm, double SpacingCm);
	static bool ShouldRecenterDeformMesh(const FVector2D& CurrentCenterCm, const FVector2D& SnappedCenterCm, bool bHasCurrentPlacement, double RecenterDistanceCm);
	static bool ResolveDeformMeshEnabled(bool bIsEnabledByPreset, int32 ConsoleOverride);
	static void ApplyShadowPolicy(UPrimitiveComponent& Component, ETDFluidTrackDeformShadowPolicy Policy);

private:
	void RequestDeformAssets();
	void HandleDeformAssetsLoaded();
	AActor* ResolveFollowTarget() const;
	void PlaceDeformMesh(const FVector2D& CenterCm, double ExtentCm, double FloorHeightCm);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DeformMID;

	TSharedPtr<FStreamableHandle> DeformAssetHandle;
	FVector2D DeformCenterCm = FVector2D::ZeroVector;
	int32 GridVertexCount = 0;
	bool bHasPlacement = false;
	bool bAreDeformAssetsLoaded = false;
	bool bHasAppliedShadowPolicy = false;
	ETDFluidTrackDeformShadowPolicy AppliedShadowPolicy = ETDFluidTrackDeformShadowPolicy::NoShadow;
};
