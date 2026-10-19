// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/TDCombatCharacter.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "TDGameCharacter.generated.h"

class UAnimSequence;
class UBlendSpace;
class UCameraComponent;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTDCapsuleModifierComponent;
class UTDSkillComponent;
struct FStreamableHandle;
struct FTDDamageContext;
struct FTDDamageResult;

/**
 *  A controllable top-down perspective character
 */
UCLASS(abstract)
class TDGAME_API ATDGameCharacter : public ATDCombatCharacter, public IGenericTeamAgentInterface
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

	virtual void UnPossessed() override;

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

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetPassagePaceScale(float Scale);

	bool ActivateCombatAbility(FGameplayTag AbilityTag, AActor* TargetActor = nullptr);
	FVector ConsumePendingRollDirection();
	float PlayRollAnimation(const FVector& RollDirection);
	void EndRollAnimation();
	bool CanStartJump() const;
	float GetRollStaminaCost() const { return RollStaminaCost; }
	bool IsRollBlockedByCurrentAction() const;
	bool CanRollCancelCurrentAction() const;
	bool IsCancelRollLocked() const;
	bool CanPlayRollAnimation() const;
	void NotifyAttackCancelRollStarted();
	bool TryCancelActionForMovement();
	bool TryStartApproachStep(const AActor& TargetActor);
	float ComputeApproachStepDistance(const AActor& TargetActor) const;

protected:
	virtual void HandleMovementModeChanged(const FName& PreviousMovementModeName, const FName& NewMovementModeName) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTDCapsuleModifierComponent> CapsuleModifierComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTDSkillComponent> SkillComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0"))
	float AttackAcceptanceRadius = 50.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation")
	TSoftObjectPtr<UBlendSpace> LocomotionBlendSpace;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation")
	TSoftObjectPtr<UAnimSequence> JumpStartAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation")
	TSoftObjectPtr<UAnimSequence> FallLoopAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation")
	TSoftObjectPtr<UAnimSequence> LandAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	TSoftObjectPtr<UAnimSequence> RollAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0"))
	float RollStaminaCost = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	bool bGrantDefaultActionAbilities = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> SwordMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> ShieldMeshComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Weapons")
	TSoftObjectPtr<UStaticMesh> SwordMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Weapons")
	FName SwordSocketName = TEXT("HandGrip_R");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Weapons")
	FTransform SwordRelativeTransform;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Weapons")
	TSoftObjectPtr<UStaticMesh> ShieldMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Weapons")
	FName ShieldSocketName = TEXT("hand_l");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Weapons")
	FTransform ShieldRelativeTransform;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LightKnockbackRatio = 0.08f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HeavyKnockbackRatio = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0", ForceUnits="cm/s"))
	float LightKnockbackSpeed = 300.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0", ForceUnits="s"))
	float LightKnockbackSeconds = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0", ForceUnits="cm/s"))
	float HeavyKnockbackSpeed = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Knockback", meta=(ClampMin="0.0", ForceUnits="s"))
	float HeavyKnockbackSeconds = 0.22f;

private:
	void ApplyDefaultCombatStyle();
	void AttachWeaponMeshes();
	void RequestWeaponMeshes();
	void ApplyWeaponMeshes();
	void HandleDamaged(const FTDDamageResult& Result, const FTDDamageContext& Context);
	void GrantDefaultActionAbilities();
	void RequestDefaultDamageSpells();
	void HandleDefaultDamageSpellsLoaded();
	void RequestDamageSpellPresentation();
	void RequestAnimationAssets();
	void GatherAnimationAssetPaths(TArray<FSoftObjectPath>& OutPaths) const;
	void ApplyAnimationSettings();
	void RefreshJumpStateTag(bool bIsInAir) const;
	bool CanStartRoll() const;
	bool CanStartRollIgnoringCurrentAction() const;
	bool TryBufferRoll(const FVector& RollDirection);
	bool IsApproachStepPathClear(const AActor& TargetActor, const FVector& StepOffset) const;
	bool WasDamagedWithin(double Seconds) const;
	void HandleActionAnimationEnded(const UAnimSequence* Animation, bool bWasInterrupted);
	void HandleDeath(const FTDDamageContext& Context);

	FVector PendingRollDirection = FVector::ForwardVector;
	double LastAttackCancelRollStartTimeSeconds = -1.0;
	double LastDamagedTimeSeconds = -1.0;
	bool bHasGrantedDefaultActionAbilities = false;
	TSharedPtr<FStreamableHandle> DefaultDamageSpellsHandle;
	TSharedPtr<FStreamableHandle> DamageSpellPresentationHandle;
	TSharedPtr<FStreamableHandle> AnimationAssetsHandle;
	TSharedPtr<FStreamableHandle> WeaponMeshesHandle;
};
