#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FluidFX/TDFluidCloudTypes.h"
#include "TDFluidCloudWindCurtainActor.generated.h"

class ATDFluidLiveActor;

struct FTDFluidCloudWindCurtainStamp
{
	FVector2D PlanePointCm = FVector2D::ZeroVector;
	FVector2D PlaneVelocityCmPerSec = FVector2D::ZeroVector;
	float RadiusCm = 0.0f;
	float DensityStrength = 0.0f;
};

UCLASS()
class TDGAME_API ATDFluidCloudWindCurtainActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidCloudWindCurtainActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain")
	FName TargetLiveActorTag = FName(TEXT("TDFluidCloud_StormWall"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain")
	FName EyeSinkTargetLiveActorTag = FName(TEXT("TDFluidCloud_Cyclone"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain")
	bool bIsWindCurtainEnabled = true;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	static void BuildWindCurtainStamps(const FTDFluidCloudSceneSettings& Settings, const FVector2D& SimCenterPlaneCm, const FVector2D& SimExtentCm, double TimeSeconds, float ActorBrushDensityStrength, TArray<FTDFluidCloudWindCurtainStamp>& OutStamps);
	static bool BuildEyeSinkStamp(const FTDFluidCloudSceneSettings& Settings, const FVector2D& SimCenterPlaneCm, const FVector2D& SimExtentCm, FTDFluidCloudWindCurtainStamp& OutStamp);
	static bool ResolveWindCurtainEnabled(int32 CVarValue, bool bActorValue);

private:
	void HandlePresetTuningReloaded(int32 TableGeneration);
	void ReadSceneSettings();
	void QueueWindCurtain(double TimeSeconds);
	void QueueEyeSink();
	ATDFluidLiveActor* ResolveLiveActor(FName LiveActorTag, TWeakObjectPtr<ATDFluidLiveActor>& CachedActor, double& LastSearchSeconds) const;

	FTDFluidCloudSceneSettings SceneSettings;
	TArray<FTDFluidCloudWindCurtainStamp> CurtainStamps;
	TWeakObjectPtr<ATDFluidLiveActor> CachedCurtainLiveActor;
	TWeakObjectPtr<ATDFluidLiveActor> CachedEyeSinkLiveActor;
	FDelegateHandle PresetTuningReloadedHandle;
	double LastCurtainSearchSeconds = -1.0e9;
	double LastEyeSinkSearchSeconds = -1.0e9;
};
