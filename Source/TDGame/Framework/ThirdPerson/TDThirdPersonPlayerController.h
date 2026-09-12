#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "TDThirdPersonPlayerController.generated.h"

class AActor;
class ATDGameCharacter;
class UInputAction;
class UInputMappingContext;

UCLASS()
class TDGAME_API ATDThirdPersonPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATDThirdPersonPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void SetupInputComponent() override;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> PrimaryAttackAction;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> RollAction;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> SkillQAction;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> SkillEAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ThirdPerson|Targeting")
	TEnumAsByte<ECollisionChannel> TargetTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ThirdPerson|Targeting", meta=(ClampMin="0.0"))
	float TargetingTraceDistance = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ThirdPerson|Targeting", meta=(ClampMin="0.0"))
	float TargetingSweepRadius = 90.f;

private:
	void ConfigureDefaultInputMapping();
	void AddMoveMappings();
	void AddLookMappings();

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartPrimaryAttack();
	void UseSkillQ();
	void UseSkillE();
	void StartRoll();
	void StartJump();

	ATDGameCharacter* GetPlayerCharacter() const;
	AActor* ResolveHostileTargetFromView(ATDGameCharacter* PlayerCharacter) const;
	FVector GetMovementDirection(const FVector2D& MovementInput) const;
	FVector GetRollDirection(const APawn& ControlledPawn) const;

	FVector2D CachedMovementInput = FVector2D::ZeroVector;
};
