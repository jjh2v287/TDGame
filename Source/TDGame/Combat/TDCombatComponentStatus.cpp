#include "Combat/TDCombatComponent.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "Combat/GAS/TDCombatGameplayEffects.h"
#include "Combat/GAS/TDGameplayTags.h"
#include "Combat/TDDamageSubsystem.h"
#include "Combat/TDStatusDefinition.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

void UTDCombatComponent::ApplyStatus(UTDStatusDefinition* Definition, const FTDDamageContext& Context, float BuildupAmount)
{
	UWorld* World = GetWorld();
	if (bIsEndingPlay || !World || !IsAlive() || !IsValid(Definition) || Context.Depth < 0 || Context.Depth >= 32)
	{
		return;
	}
	FString ValidationError;
	if (!Definition->ValidateDefinition(ValidationError))
	{
		return;
	}

	const TStrongObjectPtr<UTDStatusDefinition> RetainedDefinition(Definition);
	const double Now = World->GetTimeSeconds();
	auto FindStatusIndex = [this, Definition, Caster = Context.Caster]()
	{
		return Statuses.IndexOfByPredicate([this, Definition, Caster](const FTDStatusRuntime& Status)
		{
			if (Status.Definition != Definition || Status.Context.Caster != Caster)
			{
				return false;
			}
			const FActiveGameplayEffect* Effect = GetActiveGameplayEffect(Status.EffectHandle);
			return !Effect || !Effect->IsPendingRemove;
		});
	};
	int32 StatusIndex = FindStatusIndex();
	if (StatusIndex != INDEX_NONE && Statuses[StatusIndex].bIsApplyingEffect)
	{
		return;
	}
	if (StatusIndex != INDEX_NONE && Statuses[StatusIndex].bIsActive)
	{
		const FActiveGameplayEffectHandle ExistingHandle = Statuses[StatusIndex].EffectHandle;
		const FActiveGameplayEffect* ExistingEffect = GetActiveGameplayEffect(ExistingHandle);
		if (!ExistingEffect)
		{
			Statuses.RemoveAt(StatusIndex);
			StatusIndex = INDEX_NONE;
		}
		else if (ExistingEffect->GetTimeRemaining(static_cast<float>(Now)) <= UE_KINDA_SMALL_NUMBER)
		{
			const uint64 GenerationBeforeExpiry = StatusGeneration;
			CheckDurationExpired(ExistingHandle);
			if (GenerationBeforeExpiry != StatusGeneration || bIsEndingPlay || !IsAlive())
			{
				return;
			}
			StatusIndex = FindStatusIndex();
			if (StatusIndex != INDEX_NONE && Statuses[StatusIndex].EffectHandle == ExistingHandle)
			{
				return;
			}
		}
	}

	const bool bIsRefreshing = StatusIndex != INDEX_NONE && Statuses[StatusIndex].bIsActive;
	if (bIsRefreshing && !Definition->bRefreshDuration)
	{
		return;
	}
	if (!bIsRefreshing && Definition->DamageThreshold > 0.f && (!FMath::IsFinite(BuildupAmount) || BuildupAmount <= 0.f))
	{
		return;
	}
	if (StatusIndex == INDEX_NONE)
	{
		StatusIndex = Statuses.AddDefaulted();
		Statuses[StatusIndex].Id = NextStatusId++;
		Statuses[StatusIndex].Definition = Definition;
	}

	FTDStatusRuntime& Status = Statuses[StatusIndex];
	if (!bIsRefreshing)
	{
		Status.Context = Context;
		++Status.Context.Depth;
		if (Now - Status.LastBuildupTime >= Definition->BuildupResetDelay)
		{
			Status.Buildup = 0.f;
		}
		Status.LastBuildupTime = Now;
		const double AddedBuildup = FMath::IsFinite(BuildupAmount) ? FMath::Max(0.f, BuildupAmount) : 0.f;
		Status.Buildup = static_cast<float>(FMath::Min(static_cast<double>(TNumericLimits<float>::Max()), Status.Buildup + AddedBuildup));
		if (Status.Buildup < Definition->DamageThreshold)
		{
			ScheduleStatusUpdate();
			return;
		}
		Status.bFreezesTarget = Definition->bFreezesTarget;
		Status.NextPulseTime = Now + Definition->PulseInterval;
	}

	FGameplayEffectContextHandle EffectContext = MakeEffectContext();
	EffectContext.AddSourceObject(Definition);
	EffectContext.AddInstigator(Status.Context.Caster.Get(), Status.Context.Caster.Get());
	const TSubclassOf<UGameplayEffect> EffectClass = Status.bFreezesTarget
		? UTDFreezeStatusEffect::StaticClass()
		: UTDDurationStatusEffect::StaticClass();
	FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(EffectClass, FMath::Clamp(Status.Context.Stats.Level, 1, 1000), EffectContext);
	if (!Spec.IsValid())
	{
		ScheduleStatusUpdate();
		return;
	}
	Spec.Data->SetDuration(Definition->Duration, true);

	const uint64 StatusId = Status.Id;
	const uint64 GenerationBeforeApplication = StatusGeneration;
	const FActiveGameplayEffectHandle PreviousHandle = Status.EffectHandle;
	Status.bIsActive = true;
	Status.bIsApplyingEffect = true;
	const FActiveGameplayEffectHandle AppliedHandle = ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	StatusIndex = Statuses.IndexOfByPredicate([StatusId](const FTDStatusRuntime& Candidate) { return Candidate.Id == StatusId; });
	if (GenerationBeforeApplication != StatusGeneration || StatusIndex == INDEX_NONE || bIsEndingPlay || !IsAlive())
	{
		if (AppliedHandle.IsValid())
		{
			RemoveActiveGameplayEffect(AppliedHandle);
		}
		return;
	}

	FTDStatusRuntime& AppliedStatus = Statuses[StatusIndex];
	AppliedStatus.bIsApplyingEffect = false;
	const FActiveGameplayEffect* AppliedEffect = GetActiveGameplayEffect(AppliedHandle);
	if (!AppliedEffect || AppliedEffect->IsPendingRemove)
	{
		if (!GetActiveGameplayEffect(PreviousHandle))
		{
			Statuses.RemoveAt(StatusIndex);
		}
		ScheduleStatusUpdate();
		return;
	}
	AppliedStatus.EffectHandle = AppliedHandle;
	AppliedStatus.Buildup = 0.f;
	AppliedStatus.ExpireTime = AppliedEffect->GetEndTime();
	if (FOnActiveGameplayEffectRemoved_Info* RemovedDelegate = OnGameplayEffectRemoved_InfoDelegate(AppliedHandle))
	{
		RemovedDelegate->AddUObject(this, &UTDCombatComponent::HandleStatusEffectRemoved, StatusId);
	}
	const FTDStatusRuntime EventStatus = AppliedStatus;
	if (PreviousHandle.IsValid() && PreviousHandle != AppliedHandle)
	{
		RemoveActiveGameplayEffect(PreviousHandle);
	}
	if (!bIsRefreshing && GenerationBeforeApplication == StatusGeneration)
	{
		ExecuteStatusEvent(EventStatus, ETDDamageEvent::Spawn);
	}
	ScheduleStatusUpdate();
}

void UTDCombatComponent::UpdateStatuses()
{
	if (bIsUpdatingStatuses)
	{
		return;
	}
	TGuardValue<bool> UpdatingStatusesGuard(bIsUpdatingStatuses, true);
	UWorld* World = GetWorld();
	if (!World || bIsEndingPlay || !IsAlive())
	{
		ClearStatuses();
		return;
	}

	const double Now = World->GetTimeSeconds();
	const uint64 GenerationAtStart = StatusGeneration;
	TArray<uint64> StatusIds;
	StatusIds.Reserve(Statuses.Num());
	for (const FTDStatusRuntime& Status : Statuses)
	{
		StatusIds.Add(Status.Id);
	}
	for (const uint64 StatusId : StatusIds)
	{
		auto FindStatusIndex = [this, StatusId]()
		{
			return Statuses.IndexOfByPredicate([StatusId](const FTDStatusRuntime& Status) { return Status.Id == StatusId; });
		};
		int32 StatusIndex = FindStatusIndex();
		if (StatusIndex == INDEX_NONE || Statuses[StatusIndex].bIsApplyingEffect)
		{
			continue;
		}
		if (!IsValid(Statuses[StatusIndex].Definition))
		{
			const FActiveGameplayEffectHandle Handle = Statuses[StatusIndex].EffectHandle;
			Statuses.RemoveAt(StatusIndex);
			if (Handle.IsValid())
			{
				RemoveActiveGameplayEffect(Handle);
			}
			if (GenerationAtStart != StatusGeneration || bIsEndingPlay || !IsAlive())
			{
				return;
			}
			continue;
		}
		if (!Statuses[StatusIndex].bIsActive)
		{
			if (Now >= Statuses[StatusIndex].LastBuildupTime + Statuses[StatusIndex].Definition->BuildupResetDelay)
			{
				Statuses.RemoveAt(StatusIndex);
			}
			continue;
		}
		const FActiveGameplayEffect* Effect = GetActiveGameplayEffect(Statuses[StatusIndex].EffectHandle);
		if (!Effect)
		{
			Statuses.RemoveAt(StatusIndex);
			continue;
		}
		if (Effect->IsPendingRemove)
		{
			continue;
		}
		Statuses[StatusIndex].ExpireTime = Effect->GetEndTime();

		int32 PulsesExecuted = 0;
		while (StatusIndex != INDEX_NONE && PulsesExecuted < 64)
		{
			FTDStatusRuntime& Status = Statuses[StatusIndex];
			if (Status.bIsApplyingEffect || Status.NextPulseTime > Now || Status.NextPulseTime >= Status.ExpireTime)
			{
				break;
			}
			Status.NextPulseTime += Status.Definition->PulseInterval;
			const FTDStatusRuntime PulsingStatus = Status;
			++PulsesExecuted;
			ExecuteStatusEvent(PulsingStatus, ETDDamageEvent::Pulse);
			if (GenerationAtStart != StatusGeneration || bIsEndingPlay || !IsAlive())
			{
				return;
			}
			StatusIndex = FindStatusIndex();
		}
		if (StatusIndex != INDEX_NONE && PulsesExecuted == 64 && Statuses[StatusIndex].NextPulseTime <= Now)
		{
			Statuses[StatusIndex].NextPulseTime = Now + Statuses[StatusIndex].Definition->PulseInterval;
		}
	}
	ScheduleStatusUpdate();
}

void UTDCombatComponent::ScheduleStatusUpdate()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(StatusTimer);
	if (bIsEndingPlay || !IsAlive())
	{
		return;
	}

	double NextUpdateTime = TNumericLimits<double>::Max();
	for (const FTDStatusRuntime& Status : Statuses)
	{
		if (!IsValid(Status.Definition) || Status.bIsApplyingEffect)
		{
			continue;
		}
		if (!Status.bIsActive)
		{
			NextUpdateTime = FMath::Min(NextUpdateTime, Status.LastBuildupTime + Status.Definition->BuildupResetDelay);
			continue;
		}
		if (Status.NextPulseTime < Status.ExpireTime)
		{
			NextUpdateTime = FMath::Min(NextUpdateTime, Status.NextPulseTime);
		}
	}
	if (NextUpdateTime == TNumericLimits<double>::Max())
	{
		return;
	}
	const double Delay = FMath::Clamp(NextUpdateTime - World->GetTimeSeconds(), 0.001, static_cast<double>(TNumericLimits<float>::Max()));
	World->GetTimerManager().SetTimer(StatusTimer, this, &UTDCombatComponent::UpdateStatuses, static_cast<float>(Delay), false);
}

void UTDCombatComponent::HandleStatusEffectRemoved(const FGameplayEffectRemovalInfo& Info, uint64 StatusId)
{
	if (!Info.ActiveEffect)
	{
		return;
	}
	const int32 StatusIndex = Statuses.IndexOfByPredicate([StatusId, &Info](const FTDStatusRuntime& Status)
	{
		return Status.Id == StatusId && Status.EffectHandle == Info.ActiveEffect->Handle;
	});
	if (StatusIndex == INDEX_NONE)
	{
		return;
	}
	FTDStatusRuntime RemovedStatus = Statuses[StatusIndex];
	const TStrongObjectPtr<UTDStatusDefinition> RetainedDefinition(RemovedStatus.Definition);
	Statuses.RemoveAt(StatusIndex);
	const uint64 GenerationBeforeRemoval = StatusGeneration;
	UpdateFreezeState();
	if (Info.bPrematureRemoval || Info.bPredictionRejected || bIsEndingPlay || !IsAlive()
		|| !IsValid(RemovedStatus.Definition) || GenerationBeforeRemoval != StatusGeneration)
	{
		ScheduleStatusUpdate();
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	int32 PulsesExecuted = 0;
	while (PulsesExecuted < 64 && RemovedStatus.NextPulseTime <= Now && RemovedStatus.NextPulseTime < RemovedStatus.ExpireTime)
	{
		RemovedStatus.NextPulseTime += RemovedStatus.Definition->PulseInterval;
		++PulsesExecuted;
		ExecuteStatusEvent(RemovedStatus, ETDDamageEvent::Pulse);
		if (GenerationBeforeRemoval != StatusGeneration || bIsEndingPlay || !IsAlive())
		{
			return;
		}
	}
	ExecuteStatusEvent(RemovedStatus, ETDDamageEvent::Expire);
	ScheduleStatusUpdate();
}

void UTDCombatComponent::ClearStatuses()
{
	++StatusGeneration;
	TArray<FActiveGameplayEffectHandle> EffectHandles;
	for (const FTDStatusRuntime& Status : Statuses)
	{
		if (Status.EffectHandle.IsValid())
		{
			EffectHandles.Add(Status.EffectHandle);
		}
	}
	Statuses.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StatusTimer);
	}
	for (FActiveGameplayEffectHandle Handle : EffectHandles)
	{
		const FActiveGameplayEffect* Effect = GetActiveGameplayEffect(Handle);
		if (Effect && !Effect->IsPendingRemove)
		{
			RemoveActiveGameplayEffect(Handle);
		}
	}
	UpdateFreezeState();
}

void UTDCombatComponent::UpdateFreezeState()
{
	SetFrozen(!bIsEndingPlay && IsAlive() && HasMatchingGameplayTag(TDGameplayTags::State_Frozen));
}

void UTDCombatComponent::HandleFrozenTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	UpdateFreezeState();
}

void UTDCombatComponent::ExecuteStatusEvent(const FTDStatusRuntime& Status, ETDDamageEvent Event)
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (bIsEndingPlay || !IsAlive() || !World || !IsValid(Owner) || !IsValid(Status.Definition))
	{
		return;
	}
	const TStrongObjectPtr<UTDStatusDefinition> RetainedDefinition(Status.Definition);
	if (UTDDamageSubsystem* Subsystem = World->GetSubsystem<UTDDamageSubsystem>())
	{
		Subsystem->ExecuteRules(Status.Definition->Rules, Event, Status.Context, Owner, Owner->GetActorLocation());
	}
}

void UTDCombatComponent::SetFrozen(bool bShouldFreeze)
{
	if (bIsChangingFreeze || bIsFrozen == bShouldFreeze)
	{
		return;
	}
	{
		TGuardValue<bool> ChangingFreezeGuard(bIsChangingFreeze, true);
		AActor* Owner = GetOwner();
		bIsFrozen = bShouldFreeze;
		if (bShouldFreeze && IsValid(Owner))
		{
			SavedCustomTimeDilation = Owner->CustomTimeDilation;
			bWasActorTickEnabled = Owner->IsActorTickEnabled();
			Owner->CustomTimeDilation = 0.f;
			Owner->SetActorTickEnabled(false);
			TInlineComponentArray<UMovementComponent*> Movements(Owner);
			for (UMovementComponent* Movement : Movements)
			{
				FTDFrozenMovement& FrozenMovement = FrozenMovements.AddDefaulted_GetRef();
				FrozenMovement.Component = Movement;
				FrozenMovement.bWasTickEnabled = Movement->IsComponentTickEnabled();
				Movement->StopMovementImmediately();
				if (UCharacterMovementComponent* CharacterMovement = Cast<UCharacterMovementComponent>(Movement))
				{
					FrozenMovement.MovementMode = CharacterMovement->MovementMode;
					FrozenMovement.CustomMovementMode = CharacterMovement->CustomMovementMode;
					CharacterMovement->DisableMovement();
				}
				Movement->SetComponentTickEnabled(false);
			}
			if (APawn* Pawn = Cast<APawn>(Owner))
			{
				if (AController* Controller = Pawn->GetController())
				{
					FrozenController = Controller;
					Controller->SetIgnoreMoveInput(true);
					if (AAIController* AIController = Cast<AAIController>(Controller))
					{
						AIController->StopMovement();
						UBrainComponent* Brain = AIController->GetBrainComponent();
						if (Brain && Brain->IsRunning() && !Brain->IsPaused())
						{
							PausedBrain = Brain;
							Brain->PauseLogic(TEXT("Frozen"));
						}
					}
				}
			}
			OnFreezeChanged.Broadcast(true);
		}
		else
		{
			TArray<FTDFrozenMovement> MovementsToRestore = MoveTemp(FrozenMovements);
			const TWeakObjectPtr<UBrainComponent> BrainToResume = PausedBrain;
			const TWeakObjectPtr<AController> ControllerToRestore = FrozenController;
			PausedBrain.Reset();
			FrozenController.Reset();
			if (IsValid(Owner))
			{
				if (Owner->CustomTimeDilation == 0.f)
				{
					Owner->CustomTimeDilation = SavedCustomTimeDilation;
				}
				if (!Owner->IsActorTickEnabled())
				{
					Owner->SetActorTickEnabled(bWasActorTickEnabled);
				}
			}
			for (const FTDFrozenMovement& FrozenMovement : MovementsToRestore)
			{
				if (UMovementComponent* Movement = FrozenMovement.Component.Get())
				{
					if (UCharacterMovementComponent* CharacterMovement = Cast<UCharacterMovementComponent>(Movement))
					{
						if (CharacterMovement->MovementMode == MOVE_None && CharacterMovement->CustomMovementMode == 0)
						{
							CharacterMovement->SetMovementMode(static_cast<EMovementMode>(FrozenMovement.MovementMode), FrozenMovement.CustomMovementMode);
						}
					}
					if (!Movement->IsComponentTickEnabled())
					{
						Movement->SetComponentTickEnabled(FrozenMovement.bWasTickEnabled);
					}
				}
			}
			if (AController* Controller = ControllerToRestore.Get(); Controller && Controller->IsMoveInputIgnored())
			{
				Controller->SetIgnoreMoveInput(false);
			}
			if (UBrainComponent* Brain = BrainToResume.Get(); Brain && Brain->IsPaused())
			{
				Brain->ResumeLogic(TEXT("Thawed"));
			}
			OnFreezeChanged.Broadcast(false);
		}
	}
	UpdateFreezeState();
}

