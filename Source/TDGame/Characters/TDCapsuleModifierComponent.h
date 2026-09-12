#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TDCapsuleModifierComponent.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class TDGAME_API UTDCapsuleModifierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDCapsuleModifierComponent();

	UFUNCTION(BlueprintCallable, Category="Capsule")
	void SetJumpModifierEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category="Capsule")
	void SetRollModifierEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category="Capsule")
	void ResetModifiers();

	UFUNCTION(BlueprintPure, Category="Capsule")
	bool IsJumpModifierEnabled() const { return bIsJumpModifierEnabled; }

	UFUNCTION(BlueprintPure, Category="Capsule")
	bool IsRollModifierEnabled() const { return bIsRollModifierEnabled; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capsule", meta=(ClampMin="0.0"))
	float JumpCapsuleRadius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capsule", meta=(ClampMin="0.0"))
	float JumpCapsuleHalfHeight = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capsule", meta=(ClampMin="0.0"))
	float RollCapsuleRadius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Capsule", meta=(ClampMin="0.0"))
	float RollCapsuleHalfHeight = 40.f;

private:
	void CacheDefaultCapsuleSize();
	void RefreshCapsuleSize();
	UCapsuleComponent* GetOwnerCapsule() const;
	USkeletalMeshComponent* GetOwnerMesh() const;
	float ResolveRadius(float RequestedRadius) const;
	float ResolveHalfHeight(float RequestedHalfHeight, float ResolvedRadius) const;
	void MoveOwnerToMaintainCapsuleBase(float HalfHeightAdjustment) const;
	void RefreshMeshOffset(float TargetScaledHalfHeight, bool bMaintainCapsuleBase) const;
	void RestoreOwnerFromBaseMaintainedState() const;

	TWeakObjectPtr<UCapsuleComponent> OwnerCapsule;
	TWeakObjectPtr<USkeletalMeshComponent> OwnerMesh;
	FVector DefaultMeshRelativeLocation = FVector::ZeroVector;
	float DefaultCapsuleRadius = 0.f;
	float DefaultCapsuleHalfHeight = 0.f;
	float DefaultCapsuleScaleZ = 1.f;
	bool bIsJumpModifierEnabled = false;
	bool bIsRollModifierEnabled = false;
	bool bIsMaintainCapsuleBaseApplied = false;
};
