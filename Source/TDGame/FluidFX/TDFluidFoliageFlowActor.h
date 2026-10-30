#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TDFluidFoliageFlowActor.generated.h"

class ATDFluidLiveActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UTDFluidLiveComponent;
class UTextureRenderTarget2D;
struct FStreamableHandle;

struct FTDFluidFoliageWindowVectors
{
	FLinearColor Center = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	FLinearColor Extents = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	FLinearColor State = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
};

struct FTDFluidFoliageSourceCandidate
{
	FName Name;
	bool bIsAwake = false;
	bool bIsInitialized = false;
};

UCLASS()
class TDGAME_API ATDFluidFoliageFlowActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidFoliageFlowActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FoliageFlow")
	FName SourceLiveActorTag = TEXT("TDFluidFoliageSource");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FoliageFlow")
	TSoftObjectPtr<UTextureRenderTarget2D> FlowRenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FoliageFlow")
	TSoftObjectPtr<UMaterialInterface> FlowCopyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FoliageFlow")
	TSoftObjectPtr<UMaterialParameterCollection> FoliageWindowCollection;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FoliageFlow")
	bool bWritesFlow = true;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	static FTDFluidFoliageWindowVectors MakeWindowVectors(const FVector2D& Center, const FVector2D& Extent, float BorderFadeRatio, bool bIsActive, float SpeedReferenceCmPerSec);
	static int32 SelectSourceIndex(const TArray<FTDFluidFoliageSourceCandidate>& Candidates);
	static bool ResolveFlowEnabled(int32 ConsoleValue, bool bActorWritesFlow);

	bool AreFlowAssetsReady() const { return bAreAssetsReady; }
	bool IsWritingFlow() const { return bIsWritingFlow; }
	UMaterialInstanceDynamic* GetFlowCopyMaterialInstance() const { return FlowCopyMaterialInstance; }
	ATDFluidLiveActor* GetSourceLiveActor() const { return SourceLiveActor.Get(); }

private:
	void HandleAssetsLoaded();
	void HandlePresetTuningReloaded(int32 TableGeneration);
	UMaterialInstanceDynamic* CreateFlowCopyMaterialInstance();
	void RefreshSourceLiveActor();
	void BindSourceLiveActor(ATDFluidLiveActor* NewSource);
	UTDFluidLiveComponent* FindReadySourceComponent() const;
	void WriteWindowVectors(const FTDFluidFoliageWindowVectors& Vectors) const;
	void WriteInactiveWindow() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlowCopyMaterialInstance;

	TWeakObjectPtr<ATDFluidLiveActor> SourceLiveActor;
	TSharedPtr<FStreamableHandle> AssetLoadHandle;
	FDelegateHandle PresetTuningReloadedHandle;
	bool bAreAssetsReady = false;
	bool bIsWritingFlow = false;
};
