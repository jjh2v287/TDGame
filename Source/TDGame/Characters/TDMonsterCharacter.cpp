#include "Characters/TDMonsterCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "AI/CombatToken/TDCombatTokenSubsystem.h"
#include "AI/NPC/TDSignificanceComponent.h"
#include "Animation/AnimSequence.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Combat/GAS/Abilities/TDCombatActionAbility.h"
#include "Combat/GAS/Abilities/TDReactionAbility.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageEntity.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Combat/TDCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayAbilitySpec.h"
#include "MonsterAI/TDMonsterDefinitionLibrary.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "PhysicsEngine/PhysicsAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDMonster, Log, All);

namespace
{
	constexpr float GroundTargetedRangeThreshold = 450.f;
	constexpr float DefaultRecoverySeconds = 0.3f;
	constexpr float MaxAnimationOverrunSeconds = 0.6f;
	constexpr float StaggerImpulse = 450.f;
	constexpr float MinimumClipPlayRate = 0.05f;

	FTDLocomotionClipSet MakeLocomotionClipSet(const UTDMonsterSpeciesAsset& SpeciesAsset)
	{
		FTDLocomotionClipSet Clips;
		Clips.Idle = Cast<UAnimSequence>(SpeciesAsset.Idle.Animation);
		Clips.Walk = Cast<UAnimSequence>(SpeciesAsset.Walk.Animation);
		Clips.Run = Cast<UAnimSequence>(SpeciesAsset.Run.Animation);
		if (!Clips.Walk)
		{
			Clips.Walk = Clips.Idle;
		}
		if (!Clips.Run)
		{
			Clips.Run = Clips.Walk;
		}
		Clips.WalkClipSpeed = SpeciesAsset.WalkClipSpeed * SpeciesAsset.MeshScale;
		Clips.RunClipSpeed = SpeciesAsset.RunClipSpeed * SpeciesAsset.MeshScale;
		Clips.IdlePlayRate = SpeciesAsset.Idle.PlayRate;
		return Clips;
	}
}

ATDMonsterCharacter::ATDMonsterCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FTDCombatStats MonsterStats;
	MonsterStats.TeamId = 2;
	CombatComponent->SetStats(MonsterStats);
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
	bUseControllerRotationYaw = false;
	DefaultMaxMoveSpeed = 400.f;
	TurningRate = 540.f;

	SkillComponent = CreateDefaultSubobject<UTDSkillComponent>(TEXT("SkillComponent"));
	SignificanceComponent = CreateDefaultSubobject<UTDSignificanceComponent>(TEXT("SignificanceComponent"));
}

void ATDMonsterCharacter::BeginPlay()
{
	Super::BeginPlay();

	GrantDefaultActionAbilities();
	CombatComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);
	ApplySpeciesBody();
	ApplySpeciesAnimation();
	StartMonsterBrain();
}

void ATDMonsterCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopMonsterBrain();
	Super::EndPlay(EndPlayReason);
}

FGenericTeamId ATDMonsterCharacter::GetGenericTeamId() const
{
	return FGenericTeamId(static_cast<uint8>(CombatComponent->GetStats().TeamId));
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
	DisableMovement();
	SetActorEnableCollision(false);
	if (DeathLifeSpanSeconds > 0.f)
	{
		SetLifeSpan(DeathLifeSpanSeconds);
	}
	UnregisterCombatTokenAggro();
	StopMonsterBrain();
	PlayDeathPresentation();

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

void ATDMonsterCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplySpeciesBody();
}

void ATDMonsterCharacter::SetSpecies(UTDMonsterSpeciesAsset* NewSpecies)
{
	Species = NewSpecies;
	ApplySpeciesBody();
}

FVector ATDMonsterCharacter::GetBodyLocation() const
{
	return GetActorLocation();
}

float ATDMonsterCharacter::GetBodyYaw() const
{
	return GetActorRotation().Yaw;
}

float ATDMonsterCharacter::GetBodyRadius() const
{
	return GetCapsuleComponent()->GetScaledCapsuleRadius();
}

UTDCombatComponent* ATDMonsterCharacter::GetBodyCombatComponent() const
{
	return CombatComponent;
}

void ATDMonsterCharacter::ApplyBodyStats(const FTDMonsterStatValues& Stats)
{
	FTDCombatStats CombatStats;
	CombatStats.Level = 1;
	CombatStats.TeamId = Stats.Team;
	CombatStats.BaseMaxHealth = Stats.MaxHealth;
	CombatStats.HealthPerLevel = 0.f;
	CombatStats.AttackPower = Stats.AttackPower;
	CombatStats.AttackPowerPerLevel = 0.f;
	CombatStats.SpellPower = Stats.AttackPower;
	CombatStats.SpellPowerPerLevel = 0.f;
	CombatStats.Armor = Stats.Armor;
	CombatComponent->SetStats(CombatStats, !bHasAppliedMonsterStats);
	bHasAppliedMonsterStats = true;
	BaseMoveSpeed = Stats.MoveSpeed;
	SetMaxMoveSpeed(BaseMoveSpeed);
}

void ATDMonsterCharacter::ApplyBodyMoveIntent(const FVector2D& Direction, const float SpeedScale)
{
	if (!CombatComponent->IsAlive() || CombatComponent->IsFrozen())
	{
		return;
	}
	if (Direction.IsNearlyZero() || SpeedScale <= 0.f)
	{
		return;
	}
	SetMaxMoveSpeed(BaseMoveSpeed * SpeedScale);
	ClearDesiredFacingDirection();
	Internal_AddMovementInput(FVector(Direction.X, Direction.Y, 0.f), true);
}

void ATDMonsterCharacter::FaceBodyToward(const FVector& Location)
{
	const FVector Offset = Location - GetActorLocation();
	if (Offset.SizeSquared2D() < 1.f)
	{
		return;
	}
	SetDesiredFacingDirection(Offset.GetSafeNormal2D());
}

float ATDMonsterCharacter::BeginBodyAbility(const FTDMonsterAbilityRequest& Request)
{
	UWorld* World = GetWorld();
	UTDDamageSubsystem* DamageSubsystem = World ? World->GetSubsystem<UTDDamageSubsystem>() : nullptr;
	if (!Request.Spell || !DamageSubsystem || !CombatComponent->IsAlive() || CombatComponent->IsFrozen())
	{
		return 0.f;
	}
	const FVector Origin = ComputeAbilityOrigin(Request);
	const FVector AimOffset = Request.TargetLocation - GetActorLocation();
	if (AimOffset.SizeSquared2D() > 1.f)
	{
		FaceRotationImmediately(FRotator(0.f, AimOffset.Rotation().Yaw, 0.f));
	}
	StopMovementImmediately();
	ATDDamageEntity* Entity = DamageSubsystem->Cast(Request.Spell, this, Origin, Request.TargetLocation);
	if (!Entity)
	{
		return 0.f;
	}
	ActiveAbilityEntity = Entity;
	const float WindupSeconds = Request.Spell->ActivationDelay;
	AbilityImpactTime = World->GetTimeSeconds() + WindupSeconds;
	float RecoverySeconds = DefaultRecoverySeconds;
	const float ClipSeconds = PlayAbilityClip(Request, WindupSeconds, RecoverySeconds);
	const float CommittedSeconds = WindupSeconds + RecoverySeconds;
	return FMath::Clamp(ClipSeconds, CommittedSeconds, CommittedSeconds + MaxAnimationOverrunSeconds);
}

void ATDMonsterCharacter::CancelBodyAbility()
{
	const UWorld* World = GetWorld();
	const bool bIsStillWindingUp = World && World->GetTimeSeconds() < AbilityImpactTime;
	ATDDamageEntity* Entity = ActiveAbilityEntity.Get();
	if (Entity && bIsStillWindingUp)
	{
		Entity->Finish();
	}
	ActiveAbilityEntity.Reset();
	AbilityImpactTime = 0.f;
	CharacterAnimation->StopAction();
}

void ATDMonsterCharacter::BeginBodyStagger(const FVector& SourceLocation, const float Seconds)
{
	const FVector Away = (GetActorLocation() - SourceLocation).GetSafeNormal2D();
	if (!Away.IsNearlyZero() && !CombatComponent->IsFrozen())
	{
		AddImpulseVelocity(Away * StaggerImpulse);
	}
	if (Species && Species->Hit.IsValidClip())
	{
		PlaySpeciesClip(Species->Hit, Species->Hit.PlayRate);
	}
}

void ATDMonsterCharacter::PresentBody(const float DeltaSeconds, const ETDMonsterFsmState State)
{
}

UTDMonsterThinkSubsystem* ATDMonsterCharacter::GetThinkSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UTDMonsterThinkSubsystem>() : nullptr;
}

void ATDMonsterCharacter::ApplySpeciesBody()
{
	if (!Species)
	{
		return;
	}
	GetCapsuleComponent()->SetCapsuleSize(Species->CapsuleRadius, Species->CapsuleHalfHeight);
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (Species->Mesh)
	{
		MeshComponent->SetSkeletalMeshAsset(Species->Mesh);
	}
	MeshComponent->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -Species->CapsuleHalfHeight + Species->MeshHeightOffset), FRotator(0.f, Species->MeshYaw, 0.f));
	MeshComponent->SetRelativeScale3D(FVector(Species->MeshScale));
	UCharacterMoverComponent* Mover = GetMoverComponent();
	if (Mover && Mover->GetPrimaryVisualComponent() == MeshComponent)
	{
		Mover->SetBaseVisualComponentTransform(MeshComponent->GetRelativeTransform());
	}
}

void ATDMonsterCharacter::ApplySpeciesAnimation()
{
	if (!Species)
	{
		return;
	}
	const FTDLocomotionClipSet Clips = MakeLocomotionClipSet(*Species);
	if (!Clips.Idle && !Clips.Walk && !Clips.Run)
	{
		return;
	}
	CharacterAnimation->UseLocomotionClips(Clips);
}

void ATDMonsterCharacter::StartMonsterBrain()
{
	if (!Species || Species->DefinitionId.IsNone() || BrainHandle.IsValid())
	{
		return;
	}
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	UTDMonsterThinkSubsystem* ThinkSubsystem = GetThinkSubsystem();
	if (!Library || !ThinkSubsystem)
	{
		return;
	}
	const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = Library->FindDefinition(Species->DefinitionId);
	if (!Definition.IsValid())
	{
		UE_LOG(LogTDMonster, Error, TEXT("%s: monster definition %s is missing or invalid; see LogTDMonsterDefinition errors."), *GetName(), *Species->DefinitionId.ToString());
		return;
	}
	BrainHandle = ThinkSubsystem->RegisterMonster(this, Definition);
	if (!BrainHandle.IsValid())
	{
		return;
	}
	CombatComponent->OnDamaged.AddUObject(this, &ThisClass::HandleMonsterDamaged);
	CombatComponent->OnFreezeChanged.AddUObject(this, &ThisClass::HandleMonsterFrozen);
}

void ATDMonsterCharacter::StopMonsterBrain()
{
	if (!BrainHandle.IsValid())
	{
		return;
	}
	CombatComponent->OnDamaged.RemoveAll(this);
	CombatComponent->OnFreezeChanged.RemoveAll(this);
	if (UTDMonsterThinkSubsystem* ThinkSubsystem = GetThinkSubsystem())
	{
		ThinkSubsystem->UnregisterMonster(BrainHandle);
	}
	BrainHandle = FTDMonsterSlotHandle();
	CancelBodyAbility();
}

void ATDMonsterCharacter::HandleMonsterDamaged(const FTDDamageResult& Result, const FTDDamageContext& Context)
{
	UTDMonsterThinkSubsystem* ThinkSubsystem = GetThinkSubsystem();
	if (!ThinkSubsystem || Result.AppliedDamage <= 0.f)
	{
		return;
	}
	const AActor* Caster = Context.Caster.Get();
	const FVector SourceLocation = Caster ? Caster->GetActorLocation() : GetActorLocation() - GetActorForwardVector() * 100.f;
	ThinkSubsystem->NotifyMonsterDamaged(BrainHandle, Result.AppliedDamage, SourceLocation);
}

void ATDMonsterCharacter::HandleMonsterFrozen(const bool bIsFrozen)
{
	if (UTDMonsterThinkSubsystem* ThinkSubsystem = GetThinkSubsystem())
	{
		ThinkSubsystem->NotifyMonsterFrozen(BrainHandle, bIsFrozen);
	}
}

void ATDMonsterCharacter::PlayDeathPresentation()
{
	if (!Species)
	{
		return;
	}
	if (Species->CorpseLifeSeconds > 0.f)
	{
		SetLifeSpan(Species->CorpseLifeSeconds);
	}
	const FTDMonsterAnimClip& DeathClip = Species->Death;
	UAnimSequence* DeathSequence = Cast<UAnimSequence>(DeathClip.Animation);
	if (DeathSequence && CharacterAnimation->PlayDeath(DeathSequence, FMath::Max(DeathClip.PlayRate, MinimumClipPlayRate), DeathClip.EndSeconds) > 0.f)
	{
		return;
	}
	if (!Species->bUseRagdollWithoutDeathClip || !GetMesh()->GetPhysicsAsset())
	{
		return;
	}
	StartRagdoll();
}

void ATDMonsterCharacter::StartRagdoll()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	CharacterAnimation->SetAnimationFrozen(true);
	GetMoverComponent()->SetPrimaryVisualComponent(nullptr);
	MeshComponent->UpdateKinematicBonesToAnim(MeshComponent->GetComponentSpaceTransforms(), ETeleportType::TeleportPhysics, true);
	SetActorEnableCollision(true);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComponent->SetAllBodiesSimulatePhysics(true);
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->WakeAllRigidBodies();
	MeshComponent->bBlendPhysics = true;
}

FVector ATDMonsterCharacter::ComputeAbilityOrigin(const FTDMonsterAbilityRequest& Request) const
{
	const FVector SelfLocation = GetActorLocation();
	FVector Direction = (Request.TargetLocation - SelfLocation).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector().GetSafeNormal2D();
	}
	const UTDDamageDefinition* Spell = Request.Spell;
	if (Spell->Mode == ETDDamageEntityMode::Projectile)
	{
		return SelfLocation + Direction * (GetBodyRadius() + 30.f) + FVector(0.f, 0.f, 20.f);
	}
	if (Spell->Mode == ETDDamageEntityMode::Shockwave || Request.Range <= Spell->Radius)
	{
		return SelfLocation;
	}
	if (Request.Range > GroundTargetedRangeThreshold)
	{
		return Request.TargetLocation;
	}
	const float TargetDistance = FVector::Dist2D(SelfLocation, Request.TargetLocation);
	const float NearestCenter = Spell->Radius * 0.4f;
	const float FarthestCenter = FMath::Max(NearestCenter, Request.Range - Spell->Radius * 0.5f);
	return SelfLocation + Direction * FMath::Clamp(TargetDistance, NearestCenter, FarthestCenter);
}

float ATDMonsterCharacter::PlayAbilityClip(const FTDMonsterAbilityRequest& Request, const float WindupSeconds, float& OutRecoverySeconds)
{
	const FTDMonsterAnimClip* Clip = Species ? Species->AbilityClips.Find(Request.AbilityName) : nullptr;
	if (!Clip || !Clip->IsValidClip())
	{
		return 0.f;
	}
	OutRecoverySeconds = Clip->RecoverySeconds;
	const bool bCanAlignImpact = Clip->ImpactSeconds > 0.f && WindupSeconds > 0.f;
	const float PlayRate = bCanAlignImpact ? FMath::Clamp(Clip->ImpactSeconds / WindupSeconds, 0.5f, 2.f) : Clip->PlayRate;
	return PlaySpeciesClip(*Clip, PlayRate);
}

float ATDMonsterCharacter::PlaySpeciesClip(const FTDMonsterAnimClip& Clip, const float PlayRate)
{
	UAnimSequence* Sequence = Cast<UAnimSequence>(Clip.Animation);
	if (!Sequence)
	{
		return 0.f;
	}
	return CharacterAnimation->PlayAction(Sequence, FMath::Max(PlayRate, MinimumClipPlayRate), Clip.StartSeconds, Clip.EndSeconds, false);
}
