#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SignificanceManager.h"
#include "TDSignificanceComponent.generated.h"

USTRUCT(BlueprintType)
struct FTDSignificanceThreshold
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="Significance")
	float Significance = 0.f;

	UPROPERTY(EditDefaultsOnly, Category="Significance", meta=(ClampMin="0.0"))
	float MaxDistance = 3000.f;
};

UCLASS(Blueprintable, ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class TDGAME_API UTDSignificanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDSignificanceComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	float GetCurrentSignificance() const { return CurrentSignificance; }
	float GetCurrentDistance() const { return CurrentDistance; }

private:
	float CalculateSignificance(USignificanceManager::FManagedObjectInfo* ObjectInfo, const FTransform& ViewPoint);
	void OnPostSignificance(USignificanceManager::FManagedObjectInfo* ObjectInfo, float OldSignificance, float Significance, bool bIsFinal);
	float ResolveSignificanceByDistance(float Distance) const;

	UPROPERTY(EditDefaultsOnly, Category="Significance")
	bool bIsActivated = true;

	UPROPERTY(EditDefaultsOnly, Category="Significance")
	bool bUseViewDirectionPenalty = true;

	UPROPERTY(EditDefaultsOnly, Category="Significance")
	TArray<FTDSignificanceThreshold> Thresholds
	{
		{0.f, 2500.f},
		{1.f, 5000.f},
		{2.f, 8000.f},
		{3.f, 12000.f}
	};

	UPROPERTY(Transient)
	float CurrentSignificance = 0.f;

	UPROPERTY(Transient)
	float CurrentDistance = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<AActor> OwnerActor = nullptr;
};
