#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Combat/TDMeleeSweep.h"
#include "TDAbilityTask_PlayActionTimeline.generated.h"

class UAnimSequence;
class UNiagaraComponent;
class UTDCharacterAnimationComponent;
struct FStreamableHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTDActionTimelineDelegate);

UCLASS()
class TDGAME_API UTDAbilityTask_PlayActionTimeline : public UAbilityTask
{
	GENERATED_BODY()

public:
	UTDAbilityTask_PlayActionTimeline(const FObjectInitializer& ObjectInitializer);

	static UTDAbilityTask_PlayActionTimeline* PlayActionTimeline(UGameplayAbility* OwningAbility, const FTDActionAnimation& Action, const FTDDamageContext& DamageContext);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

	float GetPlaySeconds() const { return PlaySeconds; }
	static FTDActionTimeWindow ResolveHitConfirmedWindow(const FTDActionTimeWindow& Window, float LeadSeconds, bool bHasConfirmedHit);

	UPROPERTY(BlueprintAssignable)
	FTDActionTimelineDelegate OnCompleted;

	UPROPERTY(BlueprintAssignable)
	FTDActionTimelineDelegate OnInterrupted;

private:
	void StartTimeline();
	void HandleAnimationLoaded();
	void HandleActionAnimationEnded(const UAnimSequence* Animation, bool bWasInterrupted);
	void AdvanceWindows(float PreviousAnimationTime, float AnimationTime);
	void CloseOpenWindows(float AnimationTime);
	void SetTagWindowActive(int32 WindowIndex, bool bIsActive);
	void SetInputBufferActive(bool bIsActive);
	void SetJumpCapsuleActive(bool bIsActive);
	void SetCancelWindowActive(ETDActionCancelType CancelType, bool bIsActive);
	void AdvanceCancelWindow(ETDActionCancelType CancelType, const FTDActionTimeWindow& Window, float PreviousAnimationTime, float AnimationTime);
	FTDActionTimeWindow ResolveActionWindow(const FTDActionTimeWindow& Window) const;
	void RefreshHitConfirmation(const FTDMeleeSweep& Sweep);
	void BroadcastPendingSwingSound(int32 WindowIndex, float AnimationTime);
	void StartSlashTrail(int32 WindowIndex, const FTDMeleeSweepSettings& SweepSettings);
	void StopSlashTrail(int32 WindowIndex);
	UTDCharacterAnimationComponent* ResolveCharacterAnimation() const;

	FTDActionAnimation Action;
	FTDDamageContext DamageContext;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LoadedAnimation;

	TSharedPtr<FStreamableHandle> AnimationLoadHandle;
	TArray<FTDMeleeSweep> ActiveSweeps;
	TArray<bool> ActiveSweepFlags;
	TArray<bool> ActiveTagWindowFlags;
	TArray<bool> PendingSwingSoundFlags;
	TArray<TWeakObjectPtr<UNiagaraComponent>> SlashTrails;
	bool ActiveCancelWindowFlags[TDActionCancelTypeCount] = {};
	FDelegateHandle ActionEndedHandle;
	float PlaySeconds = 0.f;
	float LastProcessedAnimationTime = 0.f;
	bool bIsInputBufferActive = false;
	bool bIsJumpCapsuleActive = false;
	bool bHasConfirmedHit = false;
	bool bHasFinished = false;
};
