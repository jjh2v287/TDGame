#include "Characters/TDCombatCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "Combat/GAS/TDCombatAttributeSet.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"

ATDCombatCharacter::ATDCombatCharacter()
{
	CombatAttributes = CreateDefaultSubobject<UTDCombatAttributeSet>(TEXT("CombatAttributes"));
	CombatComponent = CreateDefaultSubobject<UTDCombatComponent>(TEXT("CombatComponent"));
}

UAbilitySystemComponent* ATDCombatCharacter::GetAbilitySystemComponent() const
{
	return CombatComponent;
}

void ATDCombatCharacter::BeginPlay()
{
	CombatComponent->AddAttributeSetSubobject(CombatAttributes.Get());
	CombatComponent->InitAbilityActorInfo(this, this);
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}

	const int32 AbilityLevel = CombatComponent->GetStats().Level;
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : StartupAbilities)
	{
		if (AbilityClass)
		{
			CombatComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, AbilityLevel, INDEX_NONE, this));
		}
	}

	FGameplayEffectContextHandle EffectContext = CombatComponent->MakeEffectContext();
	EffectContext.AddSourceObject(this);
	for (const TSubclassOf<UGameplayEffect>& EffectClass : StartupEffects)
	{
		if (EffectClass)
		{
			CombatComponent->ApplyGameplayEffectToSelf(EffectClass.GetDefaultObject(), AbilityLevel, EffectContext);
		}
	}
}

void ATDCombatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	CombatComponent->InitAbilityActorInfo(this, this);
}

void ATDCombatCharacter::UnPossessed()
{
	Super::UnPossessed();
	CombatComponent->RefreshAbilityActorInfo();
}

bool ATDCombatCharacter::CastDamageSpell(int32 Slot, const FVector& Target)
{
	if (!DamageSpells.IsValidIndex(Slot) || !IsValid(DamageSpells[Slot]))
	{
		return false;
	}
	return CombatComponent->TryCastDamageDefinition(DamageSpells[Slot], Target);
}
