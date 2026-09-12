#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"
#include "TDDungeonEntrance.generated.h"

class APlayerController;
class UBoxComponent;
class USphereComponent;

UCLASS()
class ATDDungeonEntrance : public AActor
{
	GENERATED_BODY()

public:
	ATDDungeonEntrance();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Entrance")
	FName DungeonId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Entrance", meta = (ClampMin = "0"))
	float PreloadDistanceCm = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Entrance")
	bool bIsExit = false;

	UFUNCTION(BlueprintImplementableEvent, Category = "TD|Entrance")
	void OnTransitionBegin();

	UFUNCTION(BlueprintImplementableEvent, Category = "TD|Entrance")
	void OnTransitionEnd();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandlePreloadTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleTravelTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleTravelStateChanged(ETDSeamlessTravelState OldState, ETDSeamlessTravelState NewState);

	static APlayerController* FindPlayerControllerOfPawn(const AActor* Actor);
	UTDSeamlessTravelSubsystem* FindTravelSubsystem() const;
	void BeginTransition(APlayerController* PlayerController);
	void EndTransition();

	UPROPERTY(VisibleAnywhere, Category = "TD|Entrance")
	TObjectPtr<USphereComponent> PreloadTrigger;

	UPROPERTY(VisibleAnywhere, Category = "TD|Entrance")
	TObjectPtr<UBoxComponent> TravelTrigger;

	TWeakObjectPtr<APlayerController> RestrictedController;
	bool bIsTransitionActive = false;
};
