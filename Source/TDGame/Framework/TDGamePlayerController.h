// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
//#include "Templates/SubclassOf.h"
#include "GameFramework/PlayerController.h"
#include "TDGamePlayerController.generated.h"

class UNiagaraSystem;
class UInputMappingContext;
class UInputAction;
class UEnhancedInputComponent;
class UPathFollowingComponent;
class ATDGameCharacter;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  Player controller for a top-down perspective game.
 *  Implements point and click based controls
 */
UCLASS(abstract)
class ATDGamePlayerController : public APlayerController
{
	GENERATED_BODY()

protected:

	/** Component used for moving along a NavMesh path. */
	UPROPERTY(VisibleDefaultsOnly, Category = AI)
	TObjectPtr<UPathFollowingComponent> PathFollowingComponent;

	/** Time Threshold to know if it was a short press */
	UPROPERTY(EditAnywhere, Category="Input")
	float ShortPressThreshold;

	/** FX Class that we will spawn when clicking */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UNiagaraSystem> FXCursor;

	/** MappingContext */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;
	
	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SetDestinationClickAction;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> SetDestinationTouchAction;

	UPROPERTY(EditAnywhere, Category="Input|Combat")
	TObjectPtr<UInputAction> PrimaryAttackAction;

	UPROPERTY(EditAnywhere, Category="Input|Combat")
	TObjectPtr<UInputAction> RollAction;

	UPROPERTY(EditAnywhere, Category="Input|Combat")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, Category="Input|Combat")
	TObjectPtr<UInputAction> SkillQAction;

	UPROPERTY(EditAnywhere, Category="Input|Combat")
	TObjectPtr<UInputAction> SkillEAction;

	UPROPERTY(EditAnywhere, Category="Input|Combat")
	bool bAttackHostileOnClick = false;

	/** True if the controlled character should navigate to the mouse cursor. */
	uint32 bMoveToMouseCursor : 1;

	/** Set to true if we're using touch input */
	uint32 bIsTouch : 1;

	/** Saved location of the character movement destination */
	FVector CachedDestination;

	/** Time that the click input has been pressed */
	float FollowTime = 0.0f;

public:

	/** Constructor */
	ATDGamePlayerController();
	virtual void FlushPressedKeys() override;

	UFUNCTION(Exec)
	void TDSpawnDamageTargets();

	UFUNCTION(Exec)
	void TDSetCasterLevel(int32 Level);

	/** Plays the given montage asset on the controlled character. Console: TDPlayMeleeMontage /Game/Combat/Animations/AM_TDMeleeAttack_Test */
	UFUNCTION(Exec)
	void TDPlayMeleeMontage(const FString& MontagePath);

	UFUNCTION(Exec)
	void TDTravelToDungeon(FName DungeonId);

	UFUNCTION(Exec)
	void TDTravelToField();

	UFUNCTION(Exec)
	void TDSaveWorldState(const FString& SlotName);

	UFUNCTION(Exec)
	void TDLoadWorldState(const FString& SlotName);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnUnPossess() override;
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;

	/** Initialize input bindings */
	virtual void SetupInputComponent() override;
	
	/** Input handlers */
	void OnInputStarted();
	void OnSetDestinationTriggered();
	void OnSetDestinationReleased();
	void OnPointerMovementCanceled();
	void OnTouchStarted();
	void OnTouchTriggered();
	void OnTouchReleased();

	/** Helper function to get the move destination */
	void UpdateCachedDestination();
	void CastDamageSpell(int32 Slot);
	void CastFirstDamageSpell();
	void CastSecondDamageSpell();
	void CastThirdDamageSpell();
	void CastFourthDamageSpell();
	void CastFifthDamageSpell();
	void CastSixthDamageSpell();

private:
	void SetupKeyboardMovement();
	void OnKeyboardMovementTriggered(const FInputActionValue& Value);
	void OnKeyboardMovementStopped();
	void StartPointerMovement(bool bTouchInput);
	void ResetMovementInput();
	void SetKeyboardMovementMode(bool bKeyboardMode);
	bool GetCursorHit(FHitResult& Hit) const;
	void FaceMouseCursor(ATDGameCharacter* ControlledCharacter) const;
	void SetupCombatInputBindings(UEnhancedInputComponent* EnhancedInputComponent);
	void OnPrimaryAttackStarted();
	void OnSkillQStarted();
	void OnSkillEStarted();
	void OnRollStarted();
	void OnJumpStarted();
	bool TryIssueAttackUnderCursor();
	void SuppressPointerMovementUntilRelease();
	AActor* ResolveHostileTargetUnderCursor(const ATDGameCharacter* PlayerCharacter) const;
	void IssueAttackCommand(AActor* TargetActor);
	void UpdateQueuedAttack();
	void ClearQueuedAttack();
	FVector GetRollDirection(const ATDGameCharacter& ControlledCharacter) const;
	static FVector2D SnapMovementInputToEightDirections(const FVector2D& MovementInput);

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> KeyboardMoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> KeyboardMappingContext;

	FVector2D KeyboardMoveInput = FVector2D::ZeroVector;
	bool bIsUsingKeyboardMovement = false;
	bool bIsPointerInputActive = false;
	bool bHasPointerStarted = false;
	bool bHasPendingPointerRelease = false;
	bool bShouldIgnorePointerUntilRelease = false;
	bool bHasValidPointerDestination = false;
	TWeakObjectPtr<AActor> QueuedAttackTarget;
	bool bIsAttackMoveQueued = false;
	uint64 LastAttackCommandFrame = MAX_uint64;
};


