#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Pawn.h"
#include "MoverSimulationTypes.h"
#include "TDCombatCharacter.generated.h"

class UCapsuleComponent;
class UCharacterMoverComponent;
class UGameplayAbility;
class UGameplayEffect;
class UNavMoverComponent;
class USkeletalMeshComponent;
class UTDCharacterAnimationComponent;
class UTDCombatAttributeSet;
class UTDCombatComponent;
class UTDDamageDefinition;
class UUAFComponent;

UENUM(BlueprintType)
enum class ETDFacingMode : uint8
{
	MovementDirection,
	ControlRotation
};

UCLASS(Abstract)
class TDGAME_API ATDCombatCharacter : public APawn, public IAbilitySystemInterface, public IMoverInputProducerInterface
{
	GENERATED_BODY()

public:
	ATDCombatCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static const FName CapsuleComponentName;
	static const FName MeshComponentName;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void PostInitializeComponents() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual FVector GetNavAgentLocation() const override;
	virtual void UpdateNavigationRelevance() override;

	UTDCombatComponent* GetCombatComponent() const { return CombatComponent.Get(); }
	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent.Get(); }
	USkeletalMeshComponent* GetMesh() const { return Mesh.Get(); }
	UCharacterMoverComponent* GetMoverComponent() const { return MoverComponent.Get(); }
	UNavMoverComponent* GetNavMoverComponent() const { return NavMoverComponent.Get(); }
	UUAFComponent* GetUAFComponent() const { return UAFComponent.Get(); }
	UTDCharacterAnimationComponent* GetCharacterAnimation() const { return CharacterAnimation.Get(); }

	UFUNCTION(BlueprintCallable, Category="Combat")
	bool CastDamageSpell(int32 Slot, const FVector& Target);

	UFUNCTION(BlueprintPure, Category="Movement")
	bool IsMovingOnGround() const;

	UFUNCTION(BlueprintPure, Category="Movement")
	bool IsAirborne() const;

	UFUNCTION(BlueprintPure, Category="Movement")
	bool CanJump() const;

	UFUNCTION(BlueprintCallable, Category="Movement")
	bool Jump();

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetMaxMoveSpeed(float NewMaxSpeed);

	UFUNCTION(BlueprintPure, Category="Movement")
	float GetMaxMoveSpeed() const;

	UFUNCTION(BlueprintCallable, Category="Movement")
	void StopMovementImmediately();

	UFUNCTION(BlueprintCallable, Category="Movement")
	void DisableMovement();

	UFUNCTION(BlueprintCallable, Category="Movement")
	void EnableMovement();

	UFUNCTION(BlueprintPure, Category="Movement")
	bool IsMovementDisabled() const { return bIsMovementDisabled; }

	UFUNCTION(BlueprintCallable, Category="Movement")
	void AddImpulseVelocity(const FVector& VelocityChange);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void FaceRotationImmediately(const FRotator& NewRotation);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void TeleportPawn(const FVector& NewLocation, const FRotator& NewRotation);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetFacingMode(ETDFacingMode NewFacingMode) { FacingMode = NewFacingMode; }

	UFUNCTION(BlueprintPure, Category="Movement")
	ETDFacingMode GetFacingMode() const { return FacingMode; }

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetDesiredFacingDirection(const FVector& Direction);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void ClearDesiredFacingDirection();

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetMovementFrozen(bool bIsFrozen);

protected:
	virtual void BeginPlay() override;
	virtual void ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult) override;

	UFUNCTION()
	virtual void HandleMovementModeChanged(const FName& PreviousMovementModeName, const FName& NewMovementModeName);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UCapsuleComponent> CapsuleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UCharacterMoverComponent> MoverComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UNavMoverComponent> NavMoverComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UUAFComponent> UAFComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTDCharacterAnimationComponent> CharacterAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UTDCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UTDCombatAttributeSet> CombatAttributes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	TArray<TObjectPtr<UTDDamageDefinition>> DamageSpells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> StartupAbilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0", ForceUnits="cm/s"))
	float DefaultMaxMoveSpeed = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0"))
	float MoveAcceleration = 2048.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0"))
	float MoveDeceleration = 2048.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0", ForceUnits="deg/s"))
	float TurningRate = 640.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement")
	ETDFacingMode FacingMode = ETDFacingMode::MovementDirection;

private:
	void ApplyMovementSettings();
	void LinkAnimationBeforeMovement();
	FVector ConsumeMoveIntent();

	FVector DesiredFacingDirection = FVector::ZeroVector;
	FName MovementModeBeforeFreeze = NAME_None;
	bool bIsMovementDisabled = false;
	bool bIsMovementFrozen = false;
};
