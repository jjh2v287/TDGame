// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/TDCombatCharacter.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "TDGameCharacter.generated.h"

class UAnimMontage;
class UCameraComponent;
class USpringArmComponent;
class UTDCapsuleModifierComponent;
class UTDSkillComponent;
struct FTDDamageContext;

/**
 *  A controllable top-down perspective character
 */
UCLASS(abstract)
class ATDGameCharacter : public ATDCombatCharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

private:

	/** Top down camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> TopDownCameraComponent;

	/** Camera boom positioning the camera above the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

public:

	/** Constructor */
	ATDGameCharacter();

	/** Initialization */
	virtual void BeginPlay() override;

	/** Update */
	virtual void Tick(float DeltaSeconds) override;

	virtual FGenericTeamId GetGenericTeamId() const override;

	/** Returns the camera component **/
	UCameraComponent* GetTopDownCameraComponent() const { return TopDownCameraComponent.Get(); }

	/** Returns the Camera Boom component **/
	USpringArmComponent* GetCameraBoom() const { return CameraBoom.Get(); }

	UFUNCTION(BlueprintPure, Category="Combat")
	float GetDesiredAttackRange() const;

	UFUNCTION(BlueprintPure, Category="Combat")
	float GetAttackAcceptanceRadius() const { return AttackAcceptanceRadius; }

	UFUNCTION(BlueprintPure, Category="Combat")
	UTDSkillComponent* GetSkillComponent() const { return SkillComponent; }

	UFUNCTION(BlueprintPure, Category="Combat")
	UTDCapsuleModifierComponent* GetCapsuleModifierComponent() const { return CapsuleModifierComponent; }

	UFUNCTION(BlueprintPure, Category="Combat")
	bool IsAttackReady() const;

	UFUNCTION(BlueprintCallable, Category="Combat")
	bool ExecutePrimaryAttack(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="Combat")
	bool ExecuteSkillQ(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="Combat")
	bool ExecuteSkillE(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="Movement")
	bool TryStartRoll(const FVector& RollDirection);

	UFUNCTION(BlueprintCallable, Category="Movement")
	bool TryStartJumpAbility();

	UFUNCTION(BlueprintPure, Category="Movement")
	bool IsRollPlaying() const;

	bool ActivateCombatAbility(FGameplayTag AbilityTag, AActor* TargetActor = nullptr);
	FVector ConsumePendingRollDirection();
	float PlayRollMontageAbility(const FVector& RollDirection);
	void EndRollMontageAbility();
	float GetRollStaminaCost() const { return RollStaminaCost; }

protected:
	virtual bool CanJumpInternal_Implementation() const override;
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode = 0) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTDCapsuleModifierComponent> CapsuleModifierComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTDSkillComponent> SkillComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0"))
	float AttackAcceptanceRadius = 50.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	TSoftObjectPtr<UAnimMontage> RollMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0"))
	float RollStaminaCost = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	bool bGrantDefaultActionAbilities = false;

private:
	void GrantDefaultActionAbilities();
	void RefreshJumpStateTag() const;
	UAnimMontage* ResolveMontage(const TSoftObjectPtr<UAnimMontage>& MontageReference) const;
	bool CanStartRoll() const;
	void HandleRollMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void HandleDeath(const FTDDamageContext& Context);

	FVector PendingRollDirection = FVector::ForwardVector;
	bool bHasGrantedDefaultActionAbilities = false;
};
