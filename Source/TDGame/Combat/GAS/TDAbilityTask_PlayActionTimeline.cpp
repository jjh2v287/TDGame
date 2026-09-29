#include "Combat/GAS/TDAbilityTask_PlayActionTimeline.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Characters/TDCapsuleModifierComponent.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDCombatCharacter.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	constexpr bool bSampleWithRootMotionExtracted = true;
	constexpr float InterruptedWindowCloseTime = -1.f;

	enum class ETDWindowTransition : uint8
	{
		None,
		Open,
		Close,
		OpenAndClose
	};

	ETDWindowTransition ResolveWindowTransition(const FTDActionTimeWindow& Window, const bool bIsActive, const float PreviousAnimationTime, const float AnimationTime)
	{
		const bool bShouldBeActive = Window.Contains(AnimationTime);
		if (bIsActive)
		{
			return bShouldBeActive ? ETDWindowTransition::None : ETDWindowTransition::Close;
		}
		if (bShouldBeActive)
		{
			return ETDWindowTransition::Open;
		}

		const bool bWasSkippedWithinStep = Window.IsValid()
			&& PreviousAnimationTime <= Window.StartSeconds
			&& AnimationTime >= Window.EndSeconds;
		return bWasSkippedWithinStep ? ETDWindowTransition::OpenAndClose : ETDWindowTransition::None;
	}

	void ApplyWindowTransition(const ETDWindowTransition Transition, const TFunctionRef<void(bool)> SetWindowActive)
	{
		if (Transition == ETDWindowTransition::Open || Transition == ETDWindowTransition::OpenAndClose)
		{
			SetWindowActive(true);
		}
		if (Transition == ETDWindowTransition::Close || Transition == ETDWindowTransition::OpenAndClose)
		{
			SetWindowActive(false);
		}
	}

	float ResolveActionEndSeconds(const FTDActionAnimation& Action, const UAnimSequence& Animation)
	{
		const float AnimationLength = Animation.GetPlayLength();
		return Action.EndSeconds > Action.StartSeconds ? FMath::Min(Action.EndSeconds, AnimationLength) : AnimationLength;
	}

	USkeletalMeshComponent* FindAvatarMesh(const AActor& Avatar)
	{
		if (const ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(&Avatar))
		{
			return CombatCharacter->GetMesh();
		}
		return Avatar.FindComponentByClass<USkeletalMeshComponent>();
	}

	FTDDamageContext MakeWindowDamageContext(const FTDDamageContext& BaseContext, AActor& Avatar)
	{
		FTDDamageContext WindowContext = BaseContext;
		if (!WindowContext.Caster.IsValid())
		{
			WindowContext.Caster = &Avatar;
		}
		if (const UTDCombatComponent* Combatant = Avatar.FindComponentByClass<UTDCombatComponent>())
		{
			WindowContext.Stats = Combatant->GetStats();
		}
		WindowContext.CastTarget = Avatar.GetActorLocation();
		WindowContext.Direction = Avatar.GetActorForwardVector();
		WindowContext.Budget = MakeShared<FTDDamageChainBudget>();
		return WindowContext;
	}
}

UTDAbilityTask_PlayActionTimeline::UTDAbilityTask_PlayActionTimeline(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickingTask = true;
}

UTDAbilityTask_PlayActionTimeline* UTDAbilityTask_PlayActionTimeline::PlayActionTimeline(UGameplayAbility* OwningAbility, const FTDActionAnimation& InAction, const FTDDamageContext& InDamageContext)
{
	UTDAbilityTask_PlayActionTimeline* Task = NewAbilityTask<UTDAbilityTask_PlayActionTimeline>(OwningAbility);
	Task->Action = InAction;
	Task->DamageContext = InDamageContext;
	return Task;
}

void UTDAbilityTask_PlayActionTimeline::Activate()
{
	Super::Activate();

	UTDCharacterAnimationComponent* CharacterAnimation = ResolveCharacterAnimation();
	LoadedAnimation = Action.Animation.LoadSynchronous();
	if (CharacterAnimation && LoadedAnimation)
	{
		PlaySeconds = CharacterAnimation->PlayAction(LoadedAnimation.Get(), Action.PlayRate, Action.StartSeconds, Action.EndSeconds, Action.bUseRootMotion);
	}

	if (PlaySeconds <= 0.f)
	{
		bHasFinished = true;
		if (ShouldBroadcastAbilityTaskDelegates())
		{
			OnInterrupted.Broadcast();
		}
		EndTask();
		return;
	}

	ActiveSweeps.SetNum(Action.HitWindows.Num());
	ActiveSweepFlags.Init(false, Action.HitWindows.Num());
	ActiveTagWindowFlags.Init(false, Action.TagWindows.Num());
	LastProcessedAnimationTime = CharacterAnimation->GetActionAnimationTime();
	ActionEndedHandle = CharacterAnimation->OnActionAnimationEnded.AddUObject(this, &ThisClass::HandleActionAnimationEnded);
	if (!CharacterAnimation->IsPlayingAction(LoadedAnimation.Get()))
	{
		HandleActionAnimationEnded(LoadedAnimation.Get(), true);
	}
}

void UTDAbilityTask_PlayActionTimeline::TickTask(const float DeltaTime)
{
	Super::TickTask(DeltaTime);
	if (bHasFinished)
	{
		return;
	}

	const UTDCharacterAnimationComponent* CharacterAnimation = ResolveCharacterAnimation();
	if (!CharacterAnimation || !CharacterAnimation->IsPlayingAction(LoadedAnimation.Get()))
	{
		HandleActionAnimationEnded(LoadedAnimation.Get(), true);
		return;
	}

	AdvanceWindows(LastProcessedAnimationTime, CharacterAnimation->GetActionAnimationTime());
	LastProcessedAnimationTime = CharacterAnimation->GetActionAnimationTime();
}

void UTDAbilityTask_PlayActionTimeline::OnDestroy(const bool bInOwnerFinished)
{
	UTDCharacterAnimationComponent* CharacterAnimation = ResolveCharacterAnimation();
	if (CharacterAnimation && ActionEndedHandle.IsValid())
	{
		CharacterAnimation->OnActionAnimationEnded.Remove(ActionEndedHandle);
	}
	ActionEndedHandle.Reset();

	if (!bHasFinished)
	{
		bHasFinished = true;
		const bool bIsStillPlaying = CharacterAnimation && CharacterAnimation->IsPlayingAction(LoadedAnimation.Get());
		CloseOpenWindows(InterruptedWindowCloseTime);
		if (bIsStillPlaying && CharacterAnimation->IsPlayingAction(LoadedAnimation.Get()))
		{
			CharacterAnimation->StopAction();
		}
	}

	Super::OnDestroy(bInOwnerFinished);
}

void UTDAbilityTask_PlayActionTimeline::HandleActionAnimationEnded(const UAnimSequence* Animation, const bool bWasInterrupted)
{
	if (bHasFinished || !LoadedAnimation || Animation != LoadedAnimation.Get())
	{
		return;
	}

	const float ActionEndSeconds = ResolveActionEndSeconds(Action, *LoadedAnimation);
	if (!bWasInterrupted)
	{
		AdvanceWindows(FMath::Min(LastProcessedAnimationTime, ActionEndSeconds), ActionEndSeconds);
		if (bHasFinished)
		{
			return;
		}
	}

	bHasFinished = true;
	if (UTDCharacterAnimationComponent* CharacterAnimation = ResolveCharacterAnimation())
	{
		CharacterAnimation->OnActionAnimationEnded.Remove(ActionEndedHandle);
	}
	ActionEndedHandle.Reset();
	CloseOpenWindows(bWasInterrupted ? InterruptedWindowCloseTime : ActionEndSeconds);

	if (ShouldBroadcastAbilityTaskDelegates())
	{
		if (bWasInterrupted)
		{
			OnInterrupted.Broadcast();
		}
		else
		{
			OnCompleted.Broadcast();
		}
	}
	EndTask();
}

void UTDAbilityTask_PlayActionTimeline::AdvanceWindows(const float PreviousAnimationTime, const float AnimationTime)
{
	AActor* Avatar = GetAvatarActor();
	const float StepStartTime = FMath::Max(PreviousAnimationTime, Action.StartSeconds);
	for (int32 WindowIndex = 0; WindowIndex < Action.HitWindows.Num() && !bHasFinished; ++WindowIndex)
	{
		const FTDMeleeHitWindow& HitWindow = Action.HitWindows[WindowIndex];
		if (!HitWindow.Window.IsValid() || HitWindow.Window.EndSeconds <= Action.StartSeconds)
		{
			continue;
		}

		FTDMeleeSweep& Sweep = ActiveSweeps[WindowIndex];
		if (!ActiveSweepFlags[WindowIndex])
		{
			if (!Avatar || AnimationTime < HitWindow.Window.StartSeconds)
			{
				continue;
			}
			ActiveSweepFlags[WindowIndex] = true;
			const float SweepStartTime = FMath::Max(HitWindow.Window.StartSeconds, Action.StartSeconds);
			Sweep.Begin(FindAvatarMesh(*Avatar), LoadedAnimation.Get(), HitWindow.Sweep, MakeWindowDamageContext(DamageContext, *Avatar), SweepStartTime, bSampleWithRootMotionExtracted);
		}

		if (!Sweep.IsActive())
		{
			continue;
		}
		if (AnimationTime >= HitWindow.Window.EndSeconds)
		{
			Sweep.End(HitWindow.Window.EndSeconds);
			continue;
		}
		Sweep.Advance(AnimationTime);
	}

	for (int32 WindowIndex = 0; WindowIndex < Action.TagWindows.Num() && !bHasFinished; ++WindowIndex)
	{
		const ETDWindowTransition Transition = ResolveWindowTransition(Action.TagWindows[WindowIndex].Window, ActiveTagWindowFlags[WindowIndex], StepStartTime, AnimationTime);
		ApplyWindowTransition(Transition, [this, WindowIndex](const bool bIsActive) { SetTagWindowActive(WindowIndex, bIsActive); });
	}

	if (bHasFinished)
	{
		return;
	}
	const ETDWindowTransition InputBufferTransition = ResolveWindowTransition(Action.InputBufferWindow, bIsInputBufferActive, StepStartTime, AnimationTime);
	ApplyWindowTransition(InputBufferTransition, [this](const bool bIsActive) { SetInputBufferActive(bIsActive); });

	if (bHasFinished)
	{
		return;
	}
	const ETDWindowTransition JumpCapsuleTransition = ResolveWindowTransition(Action.JumpCapsuleWindow, bIsJumpCapsuleActive, StepStartTime, AnimationTime);
	ApplyWindowTransition(JumpCapsuleTransition, [this](const bool bIsActive) { SetJumpCapsuleActive(bIsActive); });
}

void UTDAbilityTask_PlayActionTimeline::CloseOpenWindows(const float AnimationTime)
{
	for (int32 WindowIndex = 0; WindowIndex < ActiveSweeps.Num(); ++WindowIndex)
	{
		FTDMeleeSweep& Sweep = ActiveSweeps[WindowIndex];
		if (Sweep.IsActive())
		{
			Sweep.End(FMath::Min(AnimationTime, Action.HitWindows[WindowIndex].Window.EndSeconds));
		}
	}

	for (int32 WindowIndex = 0; WindowIndex < ActiveTagWindowFlags.Num(); ++WindowIndex)
	{
		SetTagWindowActive(WindowIndex, false);
	}
	SetInputBufferActive(false);
	SetJumpCapsuleActive(false);
}

void UTDAbilityTask_PlayActionTimeline::SetTagWindowActive(const int32 WindowIndex, const bool bIsActive)
{
	if (!ActiveTagWindowFlags.IsValidIndex(WindowIndex) || ActiveTagWindowFlags[WindowIndex] == bIsActive)
	{
		return;
	}

	ActiveTagWindowFlags[WindowIndex] = bIsActive;
	UAbilitySystemComponent* OwnerAbilitySystem = AbilitySystemComponent.Get();
	const FGameplayTagContainer& WindowTags = Action.TagWindows[WindowIndex].Tags;
	if (!OwnerAbilitySystem || WindowTags.IsEmpty())
	{
		return;
	}

	if (bIsActive)
	{
		OwnerAbilitySystem->AddLooseGameplayTags(WindowTags);
		return;
	}
	OwnerAbilitySystem->RemoveLooseGameplayTags(WindowTags);
}

void UTDAbilityTask_PlayActionTimeline::SetInputBufferActive(const bool bIsActive)
{
	if (bIsInputBufferActive == bIsActive)
	{
		return;
	}

	bIsInputBufferActive = bIsActive;
	const AActor* Avatar = GetAvatarActor();
	UTDSkillComponent* SkillComponent = Avatar ? Avatar->FindComponentByClass<UTDSkillComponent>() : nullptr;
	if (!SkillComponent)
	{
		return;
	}

	if (bIsActive)
	{
		SkillComponent->BeginInputBufferWindow();
		return;
	}
	SkillComponent->EndInputBufferWindow();
}

void UTDAbilityTask_PlayActionTimeline::SetJumpCapsuleActive(const bool bIsActive)
{
	if (bIsJumpCapsuleActive == bIsActive)
	{
		return;
	}

	bIsJumpCapsuleActive = bIsActive;
	const AActor* Avatar = GetAvatarActor();
	if (UTDCapsuleModifierComponent* CapsuleModifier = Avatar ? Avatar->FindComponentByClass<UTDCapsuleModifierComponent>() : nullptr)
	{
		CapsuleModifier->SetJumpModifierEnabled(bIsActive);
	}
}

UTDCharacterAnimationComponent* UTDAbilityTask_PlayActionTimeline::ResolveCharacterAnimation() const
{
	const AActor* Avatar = GetAvatarActor();
	return Avatar ? Avatar->FindComponentByClass<UTDCharacterAnimationComponent>() : nullptr;
}
