#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "TDFluidSurfaceOutputActor.generated.h"

class ATDFluidLiveActor;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTDFluidLiveComponent;

UCLASS()
class TDGAME_API ATDFluidSurfaceOutputActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidSurfaceOutputActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceOutput")
	TObjectPtr<ATDFluidLiveActor> SourceActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceOutput")
	TObjectPtr<ATDFluidLiveActor> TileSourceActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceOutput")
	TSoftObjectPtr<UMaterialInterface> DecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceOutput")
	TSoftObjectPtr<UMaterialInterface> MeshMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceOutput")
	TSoftObjectPtr<UStaticMesh> OutputMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceOutput")
	int32 DecalSortOrder = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SurfaceOutput")
	TObjectPtr<UDecalComponent> OutputDecal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SurfaceOutput")
	TObjectPtr<UStaticMeshComponent> OutputMeshComponent;

	UFUNCTION(BlueprintPure, Category = "SurfaceOutput")
	bool IsDecalShown() const;

	UFUNCTION(BlueprintPure, Category = "SurfaceOutput")
	bool IsMeshShown() const;

	int32 GetRegisteredOutputMaterialCount() const { return RegisteredOutputMaterialCount; }

	void BindOutputAssets(UMaterialInterface* LoadedDecalMaterial, UMaterialInterface* LoadedMeshMaterial, UStaticMesh* LoadedMesh);
	void UpdateDecal();

	static void ComputeDecalPlacement(const FVector2D& WindowCenterCm, const FVector2D& WindowExtentCm, double FloorHeightCm, float BoxHeightCm, FVector& OutLocation, FVector& OutDecalSize);
	static bool ResolveSurfaceDecalEnabled(bool bIsEnabledByPreset, int32 ConsoleOverride);

private:
	static UTDFluidLiveComponent* ResolveSourceComponent(const ATDFluidLiveActor* Actor);

	void RequestOutputAssets();
	void HandleOutputAssetsLoaded();
	void BindOutputMesh(UMaterialInterface* LoadedMeshMaterial, UStaticMesh* LoadedMesh);
	UMaterialInstanceDynamic* CreateRegisteredOutputMaterial(UMaterialInterface& LoadedMaterial);
	bool ShouldShowDecal() const;
	void ResolveDecalVerticalRange(const UTDFluidLiveComponent& SourceComponent, double& OutFloorHeightCm, float& OutBoxHeightCm) const;
	void PlaceDecalIfChanged(const FVector& WorldLocation, const FVector& DecalSize);
	void HideDecal();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DecalMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MeshMID;

	TSharedPtr<FStreamableHandle> OutputAssetHandle;
	int32 RegisteredOutputMaterialCount = 0;
};
