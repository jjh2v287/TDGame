#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TDCharacterAnimationComponent.generated.h"

class UAnimSequence;
class UBlendSpace;
class UUAFComponent;

namespace UE::UAF
{
	struct FInjectionRequest;
}

DECLARE_MULTICAST_DELEGATE_TwoParams(FTDOnActionAnimationEnded, const UAnimSequence*, bool);

USTRUCT(BlueprintType)
struct TDGAME_API FTDLocomotionClipSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion")
	TObjectPtr<UAnimSequence> Idle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion")
	TObjectPtr<UAnimSequence> Walk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion")
	TObjectPtr<UAnimSequence> Run;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion", meta=(ClampMin="1.0", ForceUnits="cm/s"))
	float WalkClipSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion", meta=(ClampMin="1.0", ForceUnits="cm/s"))
	float RunClipSpeed = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Locomotion", meta=(ClampMin="0.05"))
	float IdlePlayRate = 1.f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDAirborneClipSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Airborne")
	TObjectPtr<UAnimSequence> JumpStart;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Airborne")
	TObjectPtr<UAnimSequence> FallLoop;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Airborne")
	TObjectPtr<UAnimSequence> Land;
};

UCLASS(ClassGroup=(TD), meta=(BlueprintSpawnableComponent))
class TDGAME_API UTDCharacterAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDCharacterAnimationComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void UseLocomotionBlendSpace(UBlendSpace* BlendSpace);
	void UseLocomotionClips(const FTDLocomotionClipSet& Clips);
	void UseAirborneClips(const FTDAirborneClipSet& Clips);

	float PlayAction(UAnimSequence* Animation, float PlayRate = 1.f, float StartSeconds = 0.f, float EndSeconds = 0.f, bool bUseRootMotion = false);
	void StopAction(float BlendOutSeconds = 0.15f);
	bool IsPlayingAction() const { return ActiveAction.Animation != nullptr; }
	bool IsPlayingAction(const UAnimSequence* Animation) const { return Animation && ActiveAction.Animation == Animation; }
	const UAnimSequence* GetActionAnimation() const { return ActiveAction.Animation; }
	float GetActionAnimationTime() const { return ActiveAction.AnimationTime; }
	float GetPreviousActionAnimationTime() const { return ActiveAction.PreviousAnimationTime; }

	float PlayDeath(UAnimSequence* Animation, float PlayRate = 1.f, float EndSeconds = 0.f);
	bool IsPlayingDeath() const { return bIsPlayingDeath; }

	void SetAnimationFrozen(bool bIsFrozen);
	bool IsAnimationFrozen() const { return bIsAnimationFrozen; }

	FTDOnActionAnimationEnded OnActionAnimationEnded;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class ETDLocomotionPose : uint8
	{
		None,
		Idle,
		Walk,
		Run,
		JumpStart,
		FallLoop,
		Land
	};

	struct FTDActiveAction
	{
		TObjectPtr<UAnimSequence> Animation = nullptr;
		float PlayRate = 1.f;
		float StartSeconds = 0.f;
		float EndSeconds = 0.f;
		float AnimationTime = 0.f;
		float PreviousAnimationTime = 0.f;
		bool bUsesRootMotion = false;
	};

	UUAFComponent* ResolveUAFComponent() const;
	void ApplyBaseAsset();
	void UpdateBlendSpaceInputs() const;
	void UpdateLocomotionClips(float DeltaTime);
	void UpdateAirborneClips();
	void AdvanceAction(float DeltaTime);
	void FinishAction(bool bWasInterrupted, float BlendOutSeconds);
	void InjectLocomotionPose(ETDLocomotionPose Pose, UAnimSequence* Animation, float PlayRate, bool bShouldLoop);
	void ClearLocomotionInjection();
	void SetRootMotionEnabled(bool bIsEnabled);
	void CancelPendingRootMotion();
	float ComputeLocomotionPlayRate(ETDLocomotionPose Pose, float PlanarSpeed) const;
	ETDLocomotionPose ChooseGroundPose(float PlanarSpeed) const;
	FVector GetOwnerVelocity() const;
	bool IsOwnerAirborne() const;

	UPROPERTY(Transient)
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	UPROPERTY(Transient)
	FTDLocomotionClipSet LocomotionClips;

	UPROPERTY(Transient)
	FTDAirborneClipSet AirborneClips;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> DeathAnimation;

	FTDActiveAction ActiveAction;
	TSharedPtr<UE::UAF::FInjectionRequest> ActionRequest;
	TSharedPtr<UE::UAF::FInjectionRequest> LocomotionRequest;
	ETDLocomotionPose CurrentLocomotionPose = ETDLocomotionPose::None;
	float CurrentLocomotionPlayRate = 1.f;
	float LandingSecondsRemaining = 0.f;
	bool bUsesBlendSpace = false;
	bool bHasBaseAsset = false;
	bool bWasAirborne = false;
	bool bIsPlayingDeath = false;
	bool bIsAnimationFrozen = false;
	bool bIsRootMotionEnabled = false;
	bool bHasPendingRootMotionCancel = false;
};
