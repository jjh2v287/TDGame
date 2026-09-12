#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Performance/BudgetTick/TDBudgetTickable.h"
#include "TDBudgetTickTestActor.generated.h"

class UBillboardComponent;
class UTDBudgetTickParticipantComponent;
class USceneComponent;
class UTextRenderComponent;

UCLASS()
class TDGAME_API ATDBudgetTickTestActor : public AActor, public ITDBudgetTickable
{
	GENERATED_BODY()

public:
	ATDBudgetTickTestActor();

	virtual void BeginPlay() override;

	virtual void OnBudgetTickManagedStateChanged(bool bIsManaged) override;
	virtual void BudgetTick(float DeltaTimeSinceLastUpdate) override;
	virtual bool CanBudgetTick() const override;
	virtual float GetBudgetImportanceBias() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UBillboardComponent> MarkerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTextRenderComponent> DebugTextComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTDBudgetTickParticipantComponent> BudgetTickParticipant;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Budget Tick Test")
	bool bAllowBudgetTick = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Budget Tick Test")
	bool bSimulateWork = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Budget Tick Test", meta=(ClampMin="0"))
	int32 SimulatedWorkIterations = 20000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Budget Tick Test")
	float RotationRateDegrees = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Budget Tick Test")
	float OrbitRadius = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Budget Tick Test")
	float OrbitRateDegrees = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Budget Tick Test")
	float RuntimeImportanceBias = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Budget Tick Test")
	bool bIsManagedByScheduler = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Budget Tick Test")
	int32 BudgetTickCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Budget Tick Test")
	float LastBudgetDeltaTime = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Budget Tick Test")
	float LastGapBetweenUpdates = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Budget Tick Test")
	float LastSimulatedCostMs = 0.f;

private:
	void RefreshDebugText();

	FVector InitialLocation = FVector::ZeroVector;
	float OrbitAngleDegrees = 0.f;
	double LastBudgetTickTimeSeconds = 0.0;
};
