#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Core/TDItemTypes.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "TDCaravanActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UTDCombatAttributeSet;
class UTDCombatComponent;

UCLASS()
class TDGAME_API ATDCaravanActor : public AActor, public IAbilitySystemInterface, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ATDCaravanActor();

	virtual void Tick(float DeltaSeconds) override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual FGenericTeamId GetGenericTeamId() const override;

	UTDCombatComponent* GetCombatComponent() const { return CombatComponent.Get(); }

	UFUNCTION(BlueprintPure, Category="Caravan")
	float GetCurrentHealth() const;

	UFUNCTION(BlueprintPure, Category="Caravan")
	bool IsAlive() const;

	UFUNCTION(BlueprintCallable, Category="Caravan")
	void SetFollowTarget(AActor* NewFollowTarget);

	UFUNCTION(BlueprintCallable, Category="Caravan")
	AActor* GetFollowTarget() const;

	UFUNCTION(BlueprintCallable, Category="Caravan")
	void AddStoredItem(const FTDItemStack& ItemStack);

	UFUNCTION(BlueprintCallable, Category="Caravan")
	TArray<FTDItemStack> ExtractStoredItems();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> CaravanMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UTDCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UTDCombatAttributeSet> CombatAttributes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	FTDCombatStats CombatStats;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Follow")
	float FollowDistance = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Follow")
	float StopDistance = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Follow")
	float CatchUpDistance = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Follow")
	float FollowMoveSpeed = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Follow", meta=(ClampMin="0.0"))
	float FollowAcceleration = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Follow", meta=(ClampMin="0.0"))
	float FollowDeceleration = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Follow", meta=(ClampMin="0.0"))
	float FollowSlowdownDistance = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Follow", meta=(ClampMin="0.0"))
	float FollowRotationInterpSpeed = 6.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Follow")
	TObjectPtr<AActor> FollowTarget;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Follow")
	bool bIsFollowingTarget = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Follow")
	FVector CurrentFollowVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Inventory")
	TArray<FTDItemStack> StoredItems;

private:
	void UpdateFollow(float DeltaSeconds);
	void HandleDeath(const FTDDamageContext& Context);
};
