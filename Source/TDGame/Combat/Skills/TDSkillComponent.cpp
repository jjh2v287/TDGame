#include "Combat/Skills/TDSkillComponent.h"

#include "Abilities/GameplayAbility.h"
#include "Combat/Skills/TDCombatStyleDefinition.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Core/TDGameplayTags.h"
#include "Engine/World.h"

UTDSkillComponent::UTDSkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTDSkillComponent::BeginPlay()
{
	Super::BeginPlay();

	UTDCombatComponent* CombatComponent = UTDCombatLibrary::GetCombatComponent(GetOwner());
	if (!CombatComponent)
	{
		return;
	}

	CombatComponent->OnDamaged.AddUObject(this, &ThisClass::HandleDamageReceived);
	CombatComponent->OnDeath.AddUObject(this, &ThisClass::HandleOwnerDeath);
	CombatComponent->RegisterGameplayTagEvent(TDGameplayTags::State_ComboWindow, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::HandleComboWindowChanged);
	CombatComponent->RegisterGameplayTagEvent(TDGameplayTags::State_Attacking, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::HandleAttackStateChanged);
	CombatComponent->RegisterGameplayTagEvent(TDGameplayTags::State_Recovery, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::HandleRecoveryStateChanged);
	CombatComponent->RegisterGameplayTagEvent(TDGameplayTags::State_Skill, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::HandleSkillStateChanged);
	CombatComponent->RegisterGameplayTagEvent(TDGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ThisClass::HandleDeathStateChanged);
}

void UTDSkillComponent::SetCombatTarget(AActor* NewTarget)
{
	CombatTarget = NewTarget;
}

bool UTDSkillComponent::HasActionDefinition(const FGameplayTag ActionTag) const
{
	return FindActionDefinition(ActionTag) != nullptr;
}

FGameplayTag UTDSkillComponent::ResolvePrimaryActionTag(int32& OutComboStep) const
{
	OutComboStep = 1;

	if (const UWorld* World = GetWorld())
	{
		const double CurrentTime = World->GetTimeSeconds();
		const bool bCanContinueCombo =
			bIsComboWindowOpen &&
			CurrentPrimaryComboStep > 0 &&
			CurrentPrimaryComboStep < MaxPrimaryComboCount &&
			LastPrimaryAttackTimestamp >= 0.0 &&
			(CurrentTime - LastPrimaryAttackTimestamp) <= PrimaryComboResetWindow;

		if (bCanContinueCombo)
		{
			OutComboStep = CurrentPrimaryComboStep + 1;
		}
	}

	const FGameplayTag ComboTags[] =
	{
		TDGameplayTags::Action_Attack_Primary_01,
		TDGameplayTags::Action_Attack_Primary_02,
		TDGameplayTags::Action_Attack_Primary_03
	};

	const int32 ComboArrayIndex = FMath::Clamp(OutComboStep - 1, 0, static_cast<int32>(UE_ARRAY_COUNT(ComboTags)) - 1);
	const FGameplayTag ComboActionTag = ComboTags[ComboArrayIndex];
	if (HasActionDefinition(ComboActionTag))
	{
		return ComboActionTag;
	}

	if (OutComboStep <= 1 && HasActionDefinition(TDGameplayTags::Action_Attack_Primary))
	{
		return TDGameplayTags::Action_Attack_Primary;
	}

	return FGameplayTag();
}

bool UTDSkillComponent::CanUseAction(const FGameplayTag ActionTag) const
{
	if (!FindActionDefinition(ActionTag))
	{
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	const UTDCombatComponent* CombatComponent = UTDCombatLibrary::GetCombatComponent(OwnerActor);
	if (CombatComponent && !CombatComponent->IsAlive())
	{
		return false;
	}

	return true;
}

bool UTDSkillComponent::TryPlayReaction(const FGameplayTag ReactionTag)
{
	if (!ReactionTag.IsValid() || !FindReactionDefinition(ReactionTag))
	{
		return false;
	}

	UTDCombatComponent* CombatComponent = UTDCombatLibrary::GetCombatComponent(GetOwner());
	if (!CombatComponent)
	{
		return false;
	}

	FGameplayTagContainer ReactionTags;
	ReactionTags.AddTag(ReactionTag);
	return CombatComponent->TryActivateAbilitiesByTag(ReactionTags, true);
}

float UTDSkillComponent::GetActionRange(const FGameplayTag ActionTag) const
{
	const FTDCombatActionDefinition* ActionDefinition = FindActionDefinition(ActionTag);
	return ActionDefinition ? ActionDefinition->Range : 0.f;
}

bool UTDSkillComponent::ShouldUseTraceForCurrentAction() const
{
	return CurrentActionTag.IsValid() && CurrentHitExecutionType == ETDCombatHitExecutionType::NotifyTrace;
}

bool UTDSkillComponent::BuildCurrentActionDamageSpec(FTDDamageSpec& OutDamageSpec) const
{
	const FTDCombatActionDefinition* ActionDefinition = FindActionDefinition(CurrentActionTag);
	return ActionDefinition && BuildDamageSpecForAction(*ActionDefinition, OutDamageSpec);
}

const FTDCombatActionDefinition* UTDSkillComponent::FindActionDefinition(const FGameplayTag ActionTag) const
{
	return SkillSet ? SkillSet->FindAction(ActionTag) : nullptr;
}

const FTDCombatReactionDefinition* UTDSkillComponent::FindReactionDefinition(const FGameplayTag ReactionTag) const
{
	return SkillSet ? SkillSet->FindReaction(ReactionTag) : nullptr;
}

void UTDSkillComponent::BeginActionExecution(
	UGameplayAbility* OwningAbility,
	const FGameplayTag ActionTag,
	AActor* TargetActor,
	const ETDCombatHitExecutionType HitExecutionType)
{
	if (TargetActor)
	{
		CombatTarget = TargetActor;
	}

	CurrentActionOwningAbility = OwningAbility;
	SetCurrentActionContext(ActionTag, TargetActor, HitExecutionType);
}

void UTDSkillComponent::EndActionExecution(UGameplayAbility* OwningAbility)
{
	if (CurrentActionOwningAbility.Get() == OwningAbility)
	{
		ClearCurrentActionContext();
	}
}

void UTDSkillComponent::NotifyPrimaryActionActivated(const int32 ComboStep, const FGameplayTag ResolvedActionTag)
{
	LastPrimaryAttackTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : LastPrimaryAttackTimestamp;
	CurrentPrimaryComboStep = FMath::Clamp(ComboStep, 1, MaxPrimaryComboCount);

	if (!ResolvedActionTag.IsValid() || ResolvedActionTag == TDGameplayTags::Action_Attack_Primary)
	{
		CurrentPrimaryComboStep = 1;
	}
}

void UTDSkillComponent::ResetPrimaryCombo()
{
	CurrentPrimaryComboStep = 0;
	LastPrimaryAttackTimestamp = -1.0;
	bIsComboWindowOpen = false;
}

bool UTDSkillComponent::BufferCombatInput(const FGameplayTag RequestedActionTag, AActor* TargetActor)
{
	if (!RequestedActionTag.IsValid())
	{
		return false;
	}

	const UTDCombatComponent* CombatComponent = UTDCombatLibrary::GetCombatComponent(GetOwner());
	if (!CombatComponent || !CombatComponent->IsAlive())
	{
		return false;
	}

	if (ActiveInputBufferWindowCount <= 0)
	{
		return false;
	}

	BufferedActionTag = RequestedActionTag;
	BufferedActionTarget = TargetActor;
	TryConsumeBufferedInput();
	return true;
}

void UTDSkillComponent::ClearBufferedInput()
{
	BufferedActionTag = FGameplayTag();
	BufferedActionTarget.Reset();
}

void UTDSkillComponent::BeginInputBufferWindow()
{
	++ActiveInputBufferWindowCount;
	TryConsumeBufferedInput();
}

void UTDSkillComponent::EndInputBufferWindow()
{
	ActiveInputBufferWindowCount = FMath::Max(ActiveInputBufferWindowCount - 1, 0);
}

void UTDSkillComponent::ClearCurrentActionContext()
{
	CurrentActionOwningAbility.Reset();
	CurrentActionTag = FGameplayTag();
	CurrentActionTarget.Reset();
	CurrentHitExecutionType = ETDCombatHitExecutionType::NotifyTrace;
}

bool UTDSkillComponent::BuildDamageSpecForAction(const FTDCombatActionDefinition& ActionDefinition, FTDDamageSpec& OutDamageSpec) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	float BaseDamage = 0.f;
	if (const UTDCombatComponent* CombatComponent = UTDCombatLibrary::GetCombatComponent(OwnerActor))
	{
		BaseDamage = CombatComponent->GetStats().AttackPower;
	}

	OutDamageSpec.Amount = (BaseDamage * ActionDefinition.DamageMultiplier) + ActionDefinition.FlatDamageBonus;
	OutDamageSpec.Element = ActionDefinition.DamageElement;
	OutDamageSpec.Instigator = OwnerActor;
	OutDamageSpec.Causer = OwnerActor;
	return true;
}

void UTDSkillComponent::SetCurrentActionContext(
	const FGameplayTag ActionTag,
	AActor* TargetActor,
	const ETDCombatHitExecutionType HitExecutionType)
{
	CurrentActionTag = ActionTag;
	CurrentActionTarget = TargetActor;
	CurrentHitExecutionType = HitExecutionType;
}

void UTDSkillComponent::HandleComboWindowChanged(const FGameplayTag Tag, const int32 NewCount)
{
	bIsComboWindowOpen = NewCount > 0;

	if (bIsComboWindowOpen)
	{
		TryConsumeBufferedInput();
		return;
	}

	const UTDCombatComponent* CombatComponent = UTDCombatLibrary::GetCombatComponent(GetOwner());
	if (CombatComponent && !CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Attacking))
	{
		ResetPrimaryCombo();
	}
}

void UTDSkillComponent::HandleAttackStateChanged(const FGameplayTag Tag, const int32 NewCount)
{
	if (NewCount > 0)
	{
		return;
	}

	const bool bConsumedBufferedInput = TryConsumeBufferedInput();
	if (!bConsumedBufferedInput && !bIsComboWindowOpen)
	{
		ResetPrimaryCombo();
	}
}

void UTDSkillComponent::HandleRecoveryStateChanged(const FGameplayTag Tag, const int32 NewCount)
{
	if (NewCount <= 0)
	{
		TryConsumeBufferedInput();
	}
}

void UTDSkillComponent::HandleSkillStateChanged(const FGameplayTag Tag, const int32 NewCount)
{
	if (NewCount <= 0)
	{
		TryConsumeBufferedInput();
	}
}

void UTDSkillComponent::HandleDeathStateChanged(const FGameplayTag Tag, const int32 NewCount)
{
	if (NewCount > 0)
	{
		ClearBufferedInput();
		ResetPrimaryCombo();
	}
}

void UTDSkillComponent::HandleDamageReceived(const FTDDamageResult& Result, const FTDDamageContext& Context)
{
	if (Result.AppliedDamage <= 0.f)
	{
		return;
	}

	ClearBufferedInput();
	ResetPrimaryCombo();
	TryPlayReaction(TDGameplayTags::Action_Reaction_Hit);
}

void UTDSkillComponent::HandleOwnerDeath(const FTDDamageContext& Context)
{
	ClearBufferedInput();
	ResetPrimaryCombo();
	ClearCurrentActionContext();
	TryPlayReaction(TDGameplayTags::Action_Reaction_Death);
}

bool UTDSkillComponent::TryConsumeBufferedInput()
{
	if (!HasBufferedInput())
	{
		ClearBufferedInput();
		return false;
	}

	UTDCombatComponent* CombatComponent = UTDCombatLibrary::GetCombatComponent(GetOwner());
	if (!CombatComponent || !CombatComponent->IsAlive())
	{
		ClearBufferedInput();
		return false;
	}

	FGameplayTag ResolvedActionTag = BufferedActionTag;
	if (BufferedActionTag == TDGameplayTags::Action_Attack_Primary)
	{
		int32 ComboStep = 0;
		ResolvedActionTag = ResolvePrimaryActionTag(ComboStep);
	}

	if (!ResolvedActionTag.IsValid() || !CanUseAction(ResolvedActionTag))
	{
		const bool bHasTransientCombatState =
			CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Attacking) ||
			CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Skill) ||
			CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Recovery) ||
			bIsComboWindowOpen;
		if (!bHasTransientCombatState)
		{
			ClearBufferedInput();
		}
		return false;
	}

	SetCombatTarget(BufferedActionTarget.Get());

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(ResolvedActionTag);
	if (CombatComponent->TryActivateAbilitiesByTag(AbilityTags, true))
	{
		ClearBufferedInput();
		return true;
	}

	return false;
}

bool UTDSkillComponent::HasBufferedInput() const
{
	return BufferedActionTag.IsValid();
}
