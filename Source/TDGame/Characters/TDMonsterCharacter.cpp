#include "Characters/TDMonsterCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "AIController.h"
#include "AI/CombatToken/TDCombatTokenSubsystem.h"
#include "AI/NPC/TDNPCUpdateSubsystem.h"
#include "AI/NPC/TDSignificanceComponent.h"
#include "Combat/GAS/Abilities/TDCombatActionAbility.h"
#include "Combat/GAS/Abilities/TDReactionAbility.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayAbilitySpec.h"

ATDMonsterCharacter::ATDMonsterCharacter(const FObjectInitializer& ObjectInitializer)
{
	FTDCombatStats MonsterStats;
	MonsterStats.TeamId = 2;
	CombatComponent->SetStats(MonsterStats);
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();

	SkillComponent = CreateDefaultSubobject<UTDSkillComponent>(TEXT("SkillComponent"));
	SignificanceComponent = CreateDefaultSubobject<UTDSignificanceComponent>(TEXT("SignificanceComponent"));
}

void ATDMonsterCharacter::BeginPlay()
{
	Super::BeginPlay();

	GrantDefaultActionAbilities();
	CombatComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);

	if (!bUseNPCUpdateSubsystem)
	{
		return;
	}

	UTDNPCUpdateSubsystem* UpdateSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UTDNPCUpdateSubsystem>() : nullptr;
	if (UpdateSubsystem)
	{
		UpdateSubsystem->Register(this);
	}
}

void ATDMonsterCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromNPCUpdateSubsystem();
	Super::EndPlay(EndPlayReason);
}

FGenericTeamId ATDMonsterCharacter::GetGenericTeamId() const
{
	return FGenericTeamId(static_cast<uint8>(CombatComponent->GetStats().TeamId));
}

void ATDMonsterCharacter::SetManagedByNPCUpdateSubsystem(const bool bIsManaged)
{
	bIsManagedByNPCUpdateSubsystem = bIsManaged;

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->SetComponentTickEnabled(!bIsManaged);
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->SetComponentTickEnabled(!bIsManaged);
	}
}

void ATDMonsterCharacter::ManualUpdateMovement(const float DeltaTime)
{
	if (!bIsManagedByNPCUpdateSubsystem)
	{
		return;
	}

	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (MovementComponent && MovementComponent->IsActive())
	{
		MovementComponent->TickComponent(DeltaTime, LEVELTICK_All, nullptr);
	}
}

void ATDMonsterCharacter::ManualUpdateAnimation(const float DeltaTime)
{
	if (!bIsManagedByNPCUpdateSubsystem)
	{
		return;
	}

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent || !MeshComponent->IsActive())
	{
		return;
	}

	if (ManagedAnimationRenderTolerance <= 0.f || MeshComponent->WasRecentlyRendered(ManagedAnimationRenderTolerance))
	{
		MeshComponent->TickComponent(DeltaTime, LEVELTICK_All, &MeshComponent->PrimaryComponentTick);
	}
}

void ATDMonsterCharacter::SetCurrentTarget(AActor* NewTarget)
{
	CurrentTarget = NewTarget;
}

bool ATDMonsterCharacter::ExecutePrimaryAttack(AActor* TargetActor)
{
	return ExecuteCombatAction(TDGameplayTags::Action_Monster_Attack_Primary, TargetActor);
}

bool ATDMonsterCharacter::ExecuteCombatAction(const FGameplayTag ActionTag, AActor* TargetActor)
{
	SetCurrentTarget(TargetActor);
	return ActivateCombatAbility(ResolveAbilityTag(ActionTag), TargetActor);
}

float ATDMonsterCharacter::GetDesiredAttackRange() const
{
	if (!SkillComponent)
	{
		return 0.f;
	}

	const FGameplayTag PreferredActionTag = SkillComponent->HasActionDefinition(TDGameplayTags::Action_Monster_Attack_Primary)
		? TDGameplayTags::Action_Monster_Attack_Primary
		: TDGameplayTags::Action_Attack_Primary;
	return SkillComponent->GetActionRange(PreferredActionTag);
}

void ATDMonsterCharacter::RegisterCombatTokenAggro()
{
	if (UTDCombatTokenSubsystem* CombatTokenSubsystem = GetCombatTokenSubsystem())
	{
		CombatTokenSubsystem->RegisterAggroMonster(this);
	}
}

void ATDMonsterCharacter::UnregisterCombatTokenAggro()
{
	if (UTDCombatTokenSubsystem* CombatTokenSubsystem = GetCombatTokenSubsystem())
	{
		CombatTokenSubsystem->UnregisterAggroMonster(this);
	}
}

bool ATDMonsterCharacter::TryAcquireCombatToken()
{
	UTDCombatTokenSubsystem* CombatTokenSubsystem = GetCombatTokenSubsystem();
	return CombatTokenSubsystem && CombatTokenSubsystem->RequestToken(this) != INDEX_NONE;
}

void ATDMonsterCharacter::ReleaseCombatToken()
{
	if (UTDCombatTokenSubsystem* CombatTokenSubsystem = GetCombatTokenSubsystem())
	{
		CombatTokenSubsystem->ReleaseTokenByUser(this);
	}
}

bool ATDMonsterCharacter::HasAvailableCombatToken() const
{
	const UTDCombatTokenSubsystem* CombatTokenSubsystem = GetCombatTokenSubsystem();
	return CombatTokenSubsystem && CombatTokenSubsystem->IsTokenAvailable();
}

void ATDMonsterCharacter::HandleDeath(const FTDDamageContext& Context)
{
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
	if (DeathLifeSpanSeconds > 0.f)
	{
		SetLifeSpan(DeathLifeSpanSeconds);
	}
	UnregisterCombatTokenAggro();
	UnregisterFromNPCUpdateSubsystem();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTDActorDeathMessage DeathMessage;
	DeathMessage.DeadActor = this;
	DeathMessage.Killer = Context.Caster.Get();
	DeathMessage.DeathLocation = GetActorLocation();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(TDGameplayTags::Event_Actor_Death, DeathMessage);
}

void ATDMonsterCharacter::UnregisterFromNPCUpdateSubsystem()
{
	if (!bUseNPCUpdateSubsystem)
	{
		return;
	}

	UTDNPCUpdateSubsystem* UpdateSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UTDNPCUpdateSubsystem>() : nullptr;
	if (UpdateSubsystem)
	{
		UpdateSubsystem->Unregister(this);
	}
}

UTDCombatTokenSubsystem* ATDMonsterCharacter::GetCombatTokenSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UTDCombatTokenSubsystem>() : nullptr;
}

bool ATDMonsterCharacter::ActivateCombatAbility(const FGameplayTag AbilityTag, AActor* TargetActor)
{
	if (!AbilityTag.IsValid() || !CombatComponent->IsAlive())
	{
		return false;
	}

	SetCurrentTarget(TargetActor);
	if (SkillComponent)
	{
		SkillComponent->SetCombatTarget(TargetActor);
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(AbilityTag);
	return CombatComponent->TryActivateAbilitiesByTag(AbilityTags, true);
}

FGameplayTag ATDMonsterCharacter::ResolveAbilityTag(const FGameplayTag RequestedActionTag) const
{
	if (!RequestedActionTag.IsValid())
	{
		return FGameplayTag();
	}

	if (RequestedActionTag == TDGameplayTags::Action_Attack_Primary ||
		RequestedActionTag == TDGameplayTags::Action_Monster_Attack_Primary)
	{
		return TDGameplayTags::Action_Monster_Attack_Primary;
	}

	if (RequestedActionTag == TDGameplayTags::Action_Monster_Skill_01)
	{
		return TDGameplayTags::Action_Monster_Skill_01;
	}

	return RequestedActionTag;
}

void ATDMonsterCharacter::GrantDefaultActionAbilities()
{
	if (!bGrantDefaultActionAbilities || !HasAuthority() || bHasGrantedDefaultActionAbilities)
	{
		return;
	}

	TArray<TSubclassOf<UGameplayAbility>> AbilityClasses;
	if (StartupAbilities.IsEmpty())
	{
		AbilityClasses =
		{
			UTDMonsterPrimaryAttackAbility::StaticClass(),
			UTDMonsterSkill01Ability::StaticClass()
		};
	}

	AbilityClasses.AddUnique(UTDReactionHitAbility::StaticClass());
	AbilityClasses.AddUnique(UTDReactionDeathAbility::StaticClass());

	const int32 AbilityLevel = CombatComponent->GetStats().Level;
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilityClasses)
	{
		if (!AbilityClass || StartupAbilities.Contains(AbilityClass))
		{
			continue;
		}
		CombatComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, AbilityLevel, INDEX_NONE, this));
	}

	bHasGrantedDefaultActionAbilities = true;
}
