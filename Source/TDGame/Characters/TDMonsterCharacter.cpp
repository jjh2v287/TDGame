#include "Characters/TDMonsterCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "AI/NPC/TDSignificanceComponent.h"
#include "Animation/AnimSequence.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "CollisionQueryParams.h"
#include "Combat/GAS/Abilities/TDCombatActionAbility.h"
#include "Combat/GAS/Abilities/TDReactionAbility.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageEntity.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayAbilitySpec.h"
#include "Misc/PackageName.h"
#include "MonsterAI/TDMonsterDefinitionLibrary.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MoverSimulationTypes.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDMonster, Log, All);

namespace TDMonsterCorpse
{
	constexpr float SinkDepthMargin = 30.f;
	constexpr float SinkEndMarginSeconds = 0.2f;
	constexpr float SinkStepSeconds = 1.f / 30.f;
	constexpr float MinimumSinkSeconds = 0.05f;
	constexpr float RagdollSinkLinearDamping = 6.f;
	constexpr float DeathClipLandVolumeScale = 0.6f;
}

namespace
{
	constexpr float GroundTargetedRangeThreshold = 450.f;
	constexpr float DefaultRecoverySeconds = 0.3f;
	constexpr float MaxAnimationOverrunSeconds = 0.6f;
	constexpr float StaggerImpulse = 450.f;
	constexpr float ComboPushSpeed = 240.f;
	constexpr float ComboPushSeconds = 0.2f;
	constexpr float FinisherPushSpeed = 660.f;
	constexpr float FinisherPushSeconds = 0.25f;
	constexpr float MinimumClipCatchUpScale = 0.5f;
	constexpr float MaximumClipCatchUpScale = 2.f;
	constexpr float ClipCatchUpScaleTolerance = 0.01f;
	constexpr float MinimumClipPlayRate = 0.05f;
	constexpr float WindupSpellMarginSeconds = 0.01f;
	constexpr float FlightHeightGainCm = 120.f;
	constexpr float FlightHoverToleranceCm = 10.f;
	constexpr float FlightLandingToleranceCm = 15.f;
	constexpr float FlightTransitionGraceSeconds = 0.5f;
	constexpr float FlightBoostSpeedScale = 1.3f;
	constexpr float FlightClipMinimumPlayRate = 0.5f;
	constexpr float FlightClipMaximumPlayRate = 2.f;
	constexpr float GroundTraceAboveCm = 300.f;
	constexpr float FlightGroundTraceAboveCm = 50.f;
	constexpr float GroundTraceBelowCm = 1000.f;
	constexpr float SummonGroundClearanceCm = 3.f;

	const FName& GetSummonClipName()
	{
		static const FName ClipName(TEXT("Summon"));
		return ClipName;
	}

	bool HasFlightClips(const UTDMonsterSpeciesAsset& SpeciesAsset)
	{
		return SpeciesAsset.TakeOffClip.IsValidClip()
			|| SpeciesAsset.FlyIdleClip.IsValidClip()
			|| SpeciesAsset.LandClip.IsValidClip()
			|| SpeciesAsset.FlyHitClip.IsValidClip();
	}

	float ComputeFlightClipPlayRate(const FTDMonsterAnimClip& Clip, const float Seconds)
	{
		const UAnimSequence* Sequence = Cast<UAnimSequence>(Clip.Animation);
		if (!Sequence || Seconds <= 0.f)
		{
			return Clip.PlayRate;
		}
		const float EndSeconds = Clip.EndSeconds > Clip.StartSeconds ? FMath::Min(Clip.EndSeconds, Sequence->GetPlayLength()) : Sequence->GetPlayLength();
		const float ClipSeconds = FMath::Max(EndSeconds - Clip.StartSeconds, UE_KINDA_SMALL_NUMBER);
		return FMath::Clamp(ClipSeconds / Seconds, FlightClipMinimumPlayRate, FlightClipMaximumPlayRate);
	}

	UTDDamageDefinition* ResolveCastSpell(const FTDMonsterAbilityRequest& Request)
	{
		UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
		if (!Library || Request.WindupSeconds <= Request.Spell->ActivationDelay + WindupSpellMarginSeconds)
		{
			return Request.Spell;
		}
		UTDDamageDefinition* WindupSpell = Library->FindOrCreateWindupSpell(Request.Spell, Request.WindupSeconds, Request.bIsDanger);
		return WindupSpell ? WindupSpell : Request.Spell;
	}

	void ResolveHitStaggerPush(const FTDDamageContext& Context, float& OutSpeed, float& OutSeconds)
	{
		const AActor* Caster = Context.Caster.Get();
		const UTDSkillComponent* CasterSkills = Caster ? Caster->FindComponentByClass<UTDSkillComponent>() : nullptr;
		const bool bIsMeleeHit = CasterSkills && Context.Delivery == ETDDamageDelivery::Melee;
		const bool bIsComboFinisher = bIsMeleeHit && CasterSkills->IsPrimaryComboFinisherActive();
		const int32 ComboStageIndex = bIsMeleeHit ? CasterSkills->GetActivePrimaryComboStageIndex() : INDEX_NONE;
		OutSpeed = ATDMonsterCharacter::ComputeStaggerPushSpeed(bIsMeleeHit, bIsComboFinisher, ComboStageIndex);
		OutSeconds = ATDMonsterCharacter::ComputeStaggerPushSeconds(bIsMeleeHit, bIsComboFinisher, ComboStageIndex);
	}

	const TCHAR* ChannelEffectPath = TEXT("/Game/MegaMagicVFXBundle/VFX/MagicShieldsVFX/VFX/DefaultVersions/AquaShield/Systems/N_AquaShield.N_AquaShield");
	constexpr float ChannelEffectRadiusPerScale = 50.f;
	constexpr float DashAcceleration = 20000.f;
	constexpr float LongStaggerSeconds = 1.f;

	FTDLocomotionClipSet MakeLocomotionClipSet(const UTDMonsterSpeciesAsset& SpeciesAsset, const FTDMonsterAnimClip& Idle, const FTDMonsterAnimClip& Walk, const FTDMonsterAnimClip& Run)
	{
		FTDLocomotionClipSet Clips;
		Clips.Idle = Cast<UAnimSequence>(Idle.Animation);
		Clips.Walk = Cast<UAnimSequence>(Walk.Animation);
		Clips.Run = Cast<UAnimSequence>(Run.Animation);
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
		Clips.IdlePlayRate = Idle.PlayRate;
		return Clips;
	}

	FTDLocomotionClipSet MakeLocomotionClipSet(const UTDMonsterSpeciesAsset& SpeciesAsset)
	{
		return MakeLocomotionClipSet(SpeciesAsset, SpeciesAsset.Idle, SpeciesAsset.Walk, SpeciesAsset.Run);
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
	ApplySpeciesFollowerMeshes();
	ApplySpeciesAnimation();
	StartMonsterBrain();
}

void ATDMonsterCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelSummonLoads();
	if (ChannelEffectHandle.IsValid())
	{
		ChannelEffectHandle->CancelHandle();
		ChannelEffectHandle.Reset();
	}
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

void ATDMonsterCharacter::HandleDeath(const FTDDamageContext& Context)
{
	RecordKillingBlow(Context);
	DisableMovement();
	SetActorEnableCollision(false);
	if (DeathLifeSpanSeconds > 0.f)
	{
		SetLifeSpan(DeathLifeSpanSeconds);
	}
	StopMonsterBrain();
	PlayDeathPresentation();
	ScheduleCorpseSink();
	BroadcastDeathMessage(Context);
}

void ATDMonsterCharacter::BroadcastDeathMessage(const FTDDamageContext& Context)
{
	UWorld* World = GetWorld();
	if (!World || !UGameplayMessageSubsystem::HasInstance(World))
	{
		return;
	}

	FTDActorDeathMessage DeathMessage;
	DeathMessage.DeadActor = this;
	DeathMessage.Killer = Context.Caster.Get();
	DeathMessage.DeathLocation = GetActorLocation();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(TDGameplayTags::Event_Actor_Death, DeathMessage);
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
	FinishBodyLandingIfArrived();
	if (bIsBodyFlying || bIsBodyLanding)
	{
		ApplyFlightMoveInput(Direction, SpeedScale);
		return;
	}
	if (Direction.IsNearlyZero() || SpeedScale <= 0.f || CharacterAnimation->IsHitStopped())
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
	UTDDamageDefinition* CastSpell = ResolveCastSpell(Request);
	ATDDamageEntity* Entity = DamageSubsystem->Cast(CastSpell, this, Origin, Request.TargetLocation);
	if (!Entity)
	{
		return 0.f;
	}
	ActiveAbilityEntity = Entity;
	const float WindupSeconds = CastSpell->ActivationDelay + (CastSpell != Request.Spell ? Request.Spell->ActivationDelay : 0.f);
	AbilityImpactTime = World->GetTimeSeconds() + WindupSeconds;
	bIsAbilityDanger = Request.bIsDanger;
	ScheduleMeleeSwingMessage(*CastSpell, Request.Range, WindupSeconds);
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
	bIsAbilityDanger = false;
	AbilityClipAnimation.Reset();
	if (World)
	{
		World->GetTimerManager().ClearTimer(MeleeSwingTimerHandle);
		World->GetTimerManager().ClearTimer(AbilityClipRestoreTimerHandle);
	}
	CharacterAnimation->StopAction();
}

void ATDMonsterCharacter::BeginBodyStagger(const FVector& SourceLocation, const float Seconds)
{
	if (!CombatComponent->IsAlive())
	{
		return;
	}
	PushBodyAwayFrom(SourceLocation, DamageStaggerPushSpeed > 0.f ? DamageStaggerPushSpeed : StaggerImpulse, DamageStaggerPushSeconds);
	if (!Species)
	{
		return;
	}
	const bool bIsBodyAirborne = bIsBodyFlying || bIsBodyLanding;
	const bool bIsLongStagger = Seconds >= LongStaggerSeconds && Species->StunClip.IsValidClip();
	const FTDMonsterAnimClip& AirOrGroundHitClip = bIsBodyAirborne && Species->FlyHitClip.IsValidClip() ? Species->FlyHitClip : SelectHitClip();
	const FTDMonsterAnimClip& HitClip = bIsLongStagger ? Species->StunClip : AirOrGroundHitClip;
	if (HitClip.IsValidClip())
	{
		PlaySpeciesReactionClip(HitClip);
	}
}

void ATDMonsterCharacter::PresentBody(const float DeltaSeconds, const ETDMonsterFsmState State)
{
	const bool bIsHitStopHoldingBody = CharacterAnimation->IsHitStopPending() || CharacterAnimation->IsVictimHitStopActive();
	const bool bHasHitStopEnded = bWasHitStopHoldingBody && !bIsHitStopHoldingBody;
	bWasHitStopHoldingBody = bIsHitStopHoldingBody;
	if (bHasHitStopEnded)
	{
		CatchUpAbilityClip();
	}
}

float ATDMonsterCharacter::BeginBodyFlight(const float AltitudeCm, const float Seconds)
{
	UCharacterMoverComponent* Mover = GetMoverComponent();
	const UWorld* World = GetWorld();
	if (!Mover || !World || !CombatComponent->IsAlive() || CombatComponent->IsFrozen())
	{
		return 0.f;
	}
	const FVector Location = GetActorLocation();
	const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	float GroundZ = static_cast<float>(Location.Z) - CapsuleHalfHeight;
	FindGroundHeight(Location - FVector(0.f, 0.f, GroundTraceAboveCm - FlightGroundTraceAboveCm), nullptr, GroundZ);
	BodyFlightTargetZ = GroundZ + CapsuleHalfHeight + AltitudeCm;
	BodyFlightBoostSpeed = Seconds > 0.f ? AltitudeCm / Seconds * FlightBoostSpeedScale : 0.f;
	BodyFlightTransitionEndSeconds = World->GetTimeSeconds() + Seconds + FlightTransitionGraceSeconds;
	bIsBodyFlying = true;
	bIsBodyLanding = false;
	Mover->QueueNextMode(DefaultModeNames::Flying);
	if (Species)
	{
		PlaySpeciesClip(Species->TakeOffClip, ComputeFlightClipPlayRate(Species->TakeOffClip, Seconds));
	}
	return Seconds;
}

float ATDMonsterCharacter::EndBodyFlight(const float Seconds)
{
	const UWorld* World = GetWorld();
	if (!World || !bIsBodyFlying || !CombatComponent->IsAlive() || CombatComponent->IsFrozen())
	{
		return 0.f;
	}
	const FVector Location = GetActorLocation();
	const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	float GroundZ = static_cast<float>(Location.Z) - CapsuleHalfHeight;
	FindGroundHeight(Location - FVector(0.f, 0.f, GroundTraceAboveCm - FlightGroundTraceAboveCm), nullptr, GroundZ);
	BodyFlightTargetZ = GroundZ + CapsuleHalfHeight;
	const float DescentCm = FMath::Max(static_cast<float>(Location.Z) - BodyFlightTargetZ, 0.f);
	BodyFlightBoostSpeed = Seconds > 0.f ? DescentCm / Seconds * FlightBoostSpeedScale : 0.f;
	BodyFlightTransitionEndSeconds = World->GetTimeSeconds() + Seconds + FlightTransitionGraceSeconds;
	bIsBodyLanding = true;
	if (Species)
	{
		PlaySpeciesClip(Species->LandClip, ComputeFlightClipPlayRate(Species->LandClip, Seconds));
	}
	return Seconds;
}

float ATDMonsterCharacter::BeginBodySummon(const FTDMonsterSummonRequest& Request)
{
	UWorld* World = GetWorld();
	if (!World || !CombatComponent->IsAlive() || Request.SpeciesName.IsNone() || Request.Count <= 0)
	{
		return 0.f;
	}
	const FTDMonsterAnimClip* SummonClip = Species ? Species->AbilityClips.Find(GetSummonClipName()) : nullptr;
	if (SummonClip && SummonClip->IsValidClip())
	{
		PlaySpeciesClip(*SummonClip, SummonClip->PlayRate);
	}

	const FSoftObjectPath SpeciesPath = UTDMonsterDefinitionLibrary::MakeSpeciesAssetPath(Request.SpeciesName);
	PendingMinionCount += Request.Count;
	if (SpeciesPath.ResolveObject())
	{
		SummonTimerHandles.Add(World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, SpeciesPath, Request]()
		{
			CompleteSummonLoad(SpeciesPath, Request);
		})));
		return Request.Seconds;
	}

	SummonLoadHandles.RemoveAll([](const TSharedPtr<FStreamableHandle>& Handle)
	{
		return !Handle.IsValid() || Handle->HasLoadCompleted();
	});
	TSharedPtr<FStreamableHandle> SpeciesHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(SpeciesPath, FStreamableDelegate::CreateWeakLambda(this, [this, SpeciesPath, Request]()
	{
		CompleteSummonLoad(SpeciesPath, Request);
	}), FStreamableManager::AsyncLoadHighPriority);
	if (!SpeciesHandle.IsValid())
	{
		PendingMinionCount = FMath::Max(PendingMinionCount - Request.Count, 0);
		UE_LOG(LogTDMonster, Warning, TEXT("%s: could not request species asset '%s' for the summon."), *GetName(), *SpeciesPath.ToString());
		return Request.Seconds;
	}
	SummonLoadHandles.Add(SpeciesHandle);
	return Request.Seconds;
}

int32 ATDMonsterCharacter::CountBodyMinionsAlive() const
{
	int32 AliveCount = PendingMinionCount;
	for (const TWeakObjectPtr<ATDMonsterCharacter>& Minion : SummonedMinions)
	{
		if (UTDCombatLibrary::IsActorAlive(Minion.Get()))
		{
			++AliveCount;
		}
	}
	return AliveCount;
}

void ATDMonsterCharacter::NotifyBodyPhaseEntered(const int32 PhaseIndex, const FName PhaseId)
{
	ApplyPhasePresentation(PhaseId);
	UWorld* World = GetWorld();
	if (!World || !UGameplayMessageSubsystem::HasInstance(World))
	{
		return;
	}

	FTDMonsterPhaseMessage PhaseMessage;
	PhaseMessage.Monster = this;
	PhaseMessage.PhaseIndex = PhaseIndex;
	PhaseMessage.PhaseId = PhaseId;
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(TDGameplayTags::Event_Monster_PhaseEntered, PhaseMessage);
}

int32 ATDMonsterCharacter::GetMonsterPhaseIndex() const
{
	const UTDMonsterThinkSubsystem* ThinkSubsystem = GetThinkSubsystem();
	if (!ThinkSubsystem || !BrainHandle.IsValid())
	{
		return 0;
	}
	return ThinkSubsystem->GetMonsterPhaseIndex(BrainHandle);
}

TArray<float> ATDMonsterCharacter::GetPhaseHealthMarks() const
{
	const UTDMonsterThinkSubsystem* ThinkSubsystem = GetThinkSubsystem();
	if (!ThinkSubsystem || !BrainHandle.IsValid())
	{
		return TArray<float>();
	}
	return ThinkSubsystem->GetMonsterPhaseHealthMarks(BrainHandle);
}

bool ATDMonsterCharacter::IsMonsterFlying() const
{
	const UTDMonsterThinkSubsystem* ThinkSubsystem = GetThinkSubsystem();
	return ThinkSubsystem && BrainHandle.IsValid() && ThinkSubsystem->IsMonsterFlying(BrainHandle);
}

bool ATDMonsterCharacter::IsBossMonster() const
{
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!Library || !Species || Species->DefinitionId.IsNone())
	{
		return false;
	}
	const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = Library->FindDefinition(Species->DefinitionId);
	return Definition.IsValid() && Definition->bIsBoss;
}

bool ATDMonsterCharacter::IsEliteMonster() const
{
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!Library || !Species || Species->DefinitionId.IsNone())
	{
		return false;
	}
	const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = Library->FindDefinition(Species->DefinitionId);
	return Definition.IsValid() && Definition->bIsElite;
}

bool ATDMonsterCharacter::IsInDangerWindup() const
{
	const UWorld* World = GetWorld();
	return bIsAbilityDanger && World && World->GetTimeSeconds() < AbilityImpactTime;
}

bool ATDMonsterCharacter::IsMeleeSwingSpell(const UTDDamageDefinition& Spell, const float Range)
{
	if (Spell.Mode != ETDDamageEntityMode::Area)
	{
		return false;
	}
	const bool bIsSwingShape = Spell.AreaShape == ETDDamageAreaShape::Cone || Spell.AreaShape == ETDDamageAreaShape::Circle;
	return bIsSwingShape && Range <= Spell.Radius;
}

bool ATDMonsterCharacter::IsInCommittedAttack() const
{
	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() < AbilityImpactTime)
	{
		return true;
	}
	const UTDMonsterThinkSubsystem* ThinkSubsystem = GetThinkSubsystem();
	if (!ThinkSubsystem || !BrainHandle.IsValid())
	{
		return false;
	}
	const ETDMonsterFsmState State = ThinkSubsystem->GetMonsterState(BrainHandle);
	return State == ETDMonsterFsmState::Cast || State == ETDMonsterFsmState::Sequence;
}

float ATDMonsterCharacter::ComputeStaggerPushSpeed(const bool bIsMeleeHit, const bool bIsComboFinisher, const int32 ComboStageIndex)
{
	if (!bIsMeleeHit)
	{
		return StaggerImpulse;
	}
	if (bIsComboFinisher)
	{
		return FinisherPushSpeed;
	}
	return ComboStageIndex >= 0 ? ComboPushSpeed : StaggerImpulse;
}

float ATDMonsterCharacter::ComputeStaggerPushSeconds(const bool bIsMeleeHit, const bool bIsComboFinisher, const int32 ComboStageIndex)
{
	if (!bIsMeleeHit)
	{
		return 0.f;
	}
	if (bIsComboFinisher)
	{
		return FinisherPushSeconds;
	}
	return ComboStageIndex >= 0 ? ComboPushSeconds : 0.f;
}

void ATDMonsterCharacter::PushBodyAwayFrom(const FVector& SourceLocation, const float Speed, const float Seconds)
{
	const FVector Away = (GetActorLocation() - SourceLocation).GetSafeNormal2D();
	if (Away.IsNearlyZero() || Speed <= 0.f || CombatComponent->IsFrozen())
	{
		return;
	}
	if (Seconds > 0.f)
	{
		ApplyLinearKnockback(Away * Speed, Seconds);
		return;
	}
	AddImpulseVelocity(Away * Speed);
}

void ATDMonsterCharacter::EngageMonsterBrain(AActor* Target)
{
	UTDMonsterThinkSubsystem* ThinkSubsystem = GetThinkSubsystem();
	if (!ThinkSubsystem || !BrainHandle.IsValid())
	{
		return;
	}
	ThinkSubsystem->EngageMonster(BrainHandle, Target);
}

void ATDMonsterCharacter::ApplyFlightMoveInput(const FVector2D& Direction, const float SpeedScale)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UCharacterMoverComponent* Mover = GetMoverComponent();
	if (Mover && bIsBodyFlying && !bIsBodyLanding && Mover->GetMovementModeName() != DefaultModeNames::Flying)
	{
		Mover->QueueNextMode(DefaultModeNames::Flying);
	}
	const bool bHasPlanarIntent = !Direction.IsNearlyZero() && SpeedScale > 0.f;
	const float HeightError = BodyFlightTargetZ - static_cast<float>(GetActorLocation().Z);
	if (!bHasPlanarIntent && FMath::Abs(HeightError) < FlightHoverToleranceCm)
	{
		return;
	}

	float MoveSpeed = bHasPlanarIntent ? BaseMoveSpeed * SpeedScale : 0.f;
	if (World->GetTimeSeconds() < BodyFlightTransitionEndSeconds)
	{
		MoveSpeed = FMath::Max(MoveSpeed, BodyFlightBoostSpeed);
	}
	if (MoveSpeed > 0.f)
	{
		SetMaxMoveSpeed(MoveSpeed);
	}

	const FVector2D PlanarDirection = bHasPlanarIntent ? Direction : FVector2D::ZeroVector;
	if (bHasPlanarIntent)
	{
		ClearDesiredFacingDirection();
	}
	const float VerticalInput = FMath::Clamp(HeightError / FlightHeightGainCm, -1.f, 1.f);
	Internal_AddMovementInput(FVector(PlanarDirection.X, PlanarDirection.Y, VerticalInput).GetClampedToMaxSize(1.f), true);
}

void ATDMonsterCharacter::FinishBodyLandingIfArrived()
{
	const UWorld* World = GetWorld();
	UCharacterMoverComponent* Mover = GetMoverComponent();
	if (!bIsBodyLanding || !World || !Mover)
	{
		return;
	}
	const bool bHasArrived = FMath::Abs(BodyFlightTargetZ - static_cast<float>(GetActorLocation().Z)) <= FlightLandingToleranceCm;
	const bool bHasTimedOut = World->GetTimeSeconds() >= BodyFlightTransitionEndSeconds;
	if (!bHasArrived && !bHasTimedOut)
	{
		return;
	}
	bIsBodyLanding = false;
	bIsBodyFlying = false;
	Mover->QueueNextMode(DefaultModeNames::Falling);
}

bool ATDMonsterCharacter::FindGroundHeight(const FVector& Location, const AActor* SpawnedActor, float& OutGroundZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	GroundObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDMonsterFindGround), false, this);
	if (SpawnedActor)
	{
		QueryParams.AddIgnoredActor(SpawnedActor);
	}
	for (const TWeakObjectPtr<ATDMonsterCharacter>& Minion : SummonedMinions)
	{
		if (const ATDMonsterCharacter* MinionActor = Minion.Get())
		{
			QueryParams.AddIgnoredActor(MinionActor);
		}
	}
	FHitResult GroundHit;
	if (!World->LineTraceSingleByObjectType(GroundHit, Location + FVector(0.f, 0.f, GroundTraceAboveCm), Location - FVector(0.f, 0.f, GroundTraceBelowCm), GroundObjects, QueryParams))
	{
		return false;
	}
	OutGroundZ = static_cast<float>(GroundHit.ImpactPoint.Z);
	return true;
}

void ATDMonsterCharacter::CompleteSummonLoad(const FSoftObjectPath& SpeciesPath, const FTDMonsterSummonRequest& Request)
{
	PendingMinionCount = FMath::Max(PendingMinionCount - Request.Count, 0);
	if (!CombatComponent->IsAlive())
	{
		return;
	}
	UTDMonsterSpeciesAsset* MinionSpecies = Cast<UTDMonsterSpeciesAsset>(SpeciesPath.ResolveObject());
	if (!MinionSpecies)
	{
		UE_LOG(LogTDMonster, Warning, TEXT("%s: species asset '%s' did not load for the summon."), *GetName(), *SpeciesPath.ToString());
		return;
	}
	SpawnSummonedMinions(MinionSpecies, Request);
}

void ATDMonsterCharacter::SpawnSummonedMinions(UTDMonsterSpeciesAsset* MinionSpecies, const FTDMonsterSummonRequest& Request)
{
	SummonedMinions.RemoveAll([](const TWeakObjectPtr<ATDMonsterCharacter>& Minion)
	{
		return !UTDCombatLibrary::IsActorAlive(Minion.Get());
	});
	const FVector Center = GetActorLocation();
	const float BaseYaw = static_cast<float>(GetActorRotation().Yaw);
	AActor* Target = Request.Target.Get();
	for (int32 MinionIndex = 0; MinionIndex < Request.Count; ++MinionIndex)
	{
		const float RingYaw = BaseYaw + 360.f * MinionIndex / Request.Count + 180.f / Request.Count;
		const FVector RingDirection = FRotator(0.f, RingYaw, 0.f).Vector();
		const FVector Location = Center + RingDirection * Request.RadiusCm;
		const FVector FacingDirection = Target ? (Target->GetActorLocation() - Location).GetSafeNormal2D() : RingDirection;
		ATDMonsterCharacter* Minion = SpawnSummonedMinion(MinionSpecies, Location, FRotator(0.f, FacingDirection.Rotation().Yaw, 0.f));
		if (!Minion)
		{
			continue;
		}
		SummonedMinions.Add(Minion);
		Minion->EngageMonsterBrain(Target);
	}
}

ATDMonsterCharacter* ATDMonsterCharacter::SpawnSummonedMinion(UTDMonsterSpeciesAsset* MinionSpecies, const FVector& Location, const FRotator& Rotation)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	ATDMonsterCharacter* Minion = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), FTransform(Rotation, Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Minion)
	{
		return nullptr;
	}
	Minion->SetSpecies(MinionSpecies);
	FVector SpawnLocation = Location;
	float GroundZ = 0.f;
	if (FindGroundHeight(Location, Minion, GroundZ))
	{
		SpawnLocation.Z = GroundZ + Minion->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + SummonGroundClearanceCm;
	}
	Minion->FinishSpawning(FTransform(Rotation, SpawnLocation));
	return Minion;
}

void ATDMonsterCharacter::CancelSummonLoads()
{
	for (const TSharedPtr<FStreamableHandle>& Handle : SummonLoadHandles)
	{
		if (Handle.IsValid() && !Handle->HasLoadCompleted())
		{
			Handle->CancelHandle();
		}
	}
	SummonLoadHandles.Reset();
	if (UWorld* World = GetWorld())
	{
		for (FTimerHandle& TimerHandle : SummonTimerHandles)
		{
			World->GetTimerManager().ClearTimer(TimerHandle);
		}
	}
	SummonTimerHandles.Reset();
	PendingMinionCount = 0;
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
	if (MeshComponent->PhysicsAssetOverride != Species->RagdollPhysicsAsset)
	{
		MeshComponent->SetPhysicsAsset(Species->RagdollPhysicsAsset);
	}
	MeshComponent->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -Species->CapsuleHalfHeight + Species->MeshHeightOffset), FRotator(0.f, Species->MeshYaw, 0.f));
	MeshComponent->SetRelativeScale3D(FVector(Species->MeshScale));
	UCharacterMoverComponent* Mover = GetMoverComponent();
	if (Mover && Mover->GetPrimaryVisualComponent() == MeshComponent)
	{
		Mover->SetBaseVisualComponentTransform(MeshComponent->GetRelativeTransform());
	}
}

void ATDMonsterCharacter::ApplySpeciesFollowerMeshes()
{
	ApplyFollowerMeshes(Species ? Species->FollowerMeshes : TArray<TObjectPtr<USkeletalMesh>>());
}

void ATDMonsterCharacter::ApplyFollowerMeshes(const TArray<TObjectPtr<USkeletalMesh>>& Meshes)
{
	const int32 FollowerCount = Meshes.Num();
	while (FollowerMeshComponents.Num() > FollowerCount)
	{
		if (USkeletalMeshComponent* RemovedFollower = FollowerMeshComponents.Pop())
		{
			RemovedFollower->DestroyComponent();
		}
	}
	for (int32 FollowerIndex = 0; FollowerIndex < FollowerCount; ++FollowerIndex)
	{
		if (!FollowerMeshComponents.IsValidIndex(FollowerIndex))
		{
			FollowerMeshComponents.Add(CreateFollowerMeshComponent());
		}
		USkeletalMeshComponent* FollowerMesh = FollowerMeshComponents[FollowerIndex];
		if (!FollowerMesh)
		{
			continue;
		}
		FollowerMesh->SetSkeletalMeshAsset(Meshes[FollowerIndex]);
		FollowerMesh->SetLeaderPoseComponent(GetMesh());
	}
}

USkeletalMeshComponent* ATDMonsterCharacter::CreateFollowerMeshComponent()
{
	USkeletalMeshComponent* LeaderMesh = GetMesh();
	if (!LeaderMesh)
	{
		return nullptr;
	}
	USkeletalMeshComponent* FollowerMesh = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
	FollowerMesh->SetupAttachment(LeaderMesh);
	FollowerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FollowerMesh->SetCanEverAffectNavigation(false);
	FollowerMesh->bUseBoundsFromLeaderPoseComponent = true;
	FollowerMesh->RegisterComponent();
	return FollowerMesh;
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
	if (!HasFlightClips(*Species))
	{
		return;
	}
	FTDAirborneClipSet AirborneClips;
	AirborneClips.FallLoop = Cast<UAnimSequence>(Species->FlyIdleClip.Animation);
	CharacterAnimation->UseAirborneClips(AirborneClips);
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
	if (Definition->bIsBoss)
	{
		RequestChannelEffect();
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
	ResolveHitStaggerPush(Context, DamageStaggerPushSpeed, DamageStaggerPushSeconds);
	ThinkSubsystem->NotifyMonsterDamaged(BrainHandle, Result.AppliedDamage, SourceLocation);
	DamageStaggerPushSpeed = 0.f;
	DamageStaggerPushSeconds = 0.f;
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
	const float HoldSeconds = ComputeKillPoseHoldSeconds();
	const bool bIsHoldingKillPose = HoldSeconds > 0.f && CharacterAnimation->StartKillPoseHold(HoldSeconds);
	if (ResolveMonsterDeathPresentation() == ETDDeathPresentation::Ragdoll)
	{
		ScheduleRagdollEntry(bIsHoldingKillPose ? HoldSeconds : 0.f);
	}
	if (!bIsHoldingKillPose)
	{
		PlayDeathClipOrRagdoll();
		return;
	}
	ReleaseKillPoseWhenHoldEnds();
}

void ATDMonsterCharacter::ReleaseKillPoseWhenHoldEnds()
{
	UWorld* World = GetWorld();
	const float RemainingRealSeconds = CharacterAnimation->GetVictimHitStopRemainingSeconds();
	if (!World || RemainingRealSeconds <= 0.f)
	{
		PlayDeathClipOrRagdoll();
		return;
	}
	World->GetTimerManager().SetTimer(KillPoseHoldTimerHandle, FTimerDelegate::CreateUObject(this, &ThisClass::ReleaseKillPoseWhenHoldEnds), UTDCharacterAnimationComponent::ConvertRealToWorldSeconds(*World, RemainingRealSeconds), false);
}

float ATDMonsterCharacter::ComputeKillPoseHoldSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->GetSubsystem<UTDCombatFeedbackSubsystem>())
	{
		return 0.f;
	}
	FTDKillPoseHoldInput HoldInput;
	HoldInput.bWasCritical = CombatComponent->GetLastDamageResult().bWasCritical;
	HoldInput.Tier = bWasKilledByComboFinisher || HoldInput.bWasCritical ? ETDHitFeelTier::Heavy : ETDHitFeelTier::Light;
	HoldInput.KillRank = ResolveDeathKillRank();
	HoldInput.GroundAreaDistance = GroundAreaKillDistance;
	HoldInput.MultiKillIndex = MultiKillIndex;
	HoldInput.VariationSeed = GetUniqueID();
	HoldInput.bIsGroundAreaKill = bWasKilledByGroundArea;
	return TDDeathLaunchRules::ComputeKillPoseHoldRealSeconds(HoldInput);
}

void ATDMonsterCharacter::PlayDeathClipOrRagdoll()
{
	if (!Species || IsRagdollPendingOrActive())
	{
		return;
	}
	const ETDDeathPresentation Presentation = ResolveMonsterDeathPresentation();
	if (Presentation == ETDDeathPresentation::Ragdoll && StartDeathRagdoll(Presentation))
	{
		return;
	}
	CancelScheduledRagdollEntry();
	ScheduleDeathClipEnd(PlayDeathClip(), Presentation == ETDDeathPresentation::ClipThenRagdoll);
}

void ATDMonsterCharacter::ScheduleDeathClipEnd(const float DeathClipSeconds, const bool bShouldRagdollAfterClip)
{
	UWorld* World = GetWorld();
	if (DeathClipSeconds <= 0.f || !World)
	{
		return;
	}
	World->GetTimerManager().SetTimer(DeathRagdollTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this, bShouldRagdollAfterClip]()
	{
		PlayDeathClipCorpseLanding();
		if (bShouldRagdollAfterClip)
		{
			StartDeathRagdoll(ETDDeathPresentation::ClipThenRagdoll);
		}
	}), DeathClipSeconds, false);
}

void ATDMonsterCharacter::PlayDeathClipCorpseLanding()
{
	UWorld* World = GetWorld();
	UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
	if (!Feedback)
	{
		return;
	}
	const float HalfHeight = CapsuleComponent ? CapsuleComponent->GetScaledCapsuleHalfHeight() : 0.f;
	FTDDeathLaunch ClipFall;
	ClipFall.DropHeight = FMath::Max(HalfHeight, 1.f);
	FTDDeathLandingSample FullStrengthImpact;
	FullStrengthImpact.MinVerticalSpeed = -TNumericLimits<float>::Max();
	const FTDCorpseLandSound MassSound = TDDeathLaunchRules::ComputeCorpseLandSound(ClipFall, FullStrengthImpact, HalfHeight);
	Feedback->PlayCorpseLanding(GetActorLocation(), TDMonsterCorpse::DeathClipLandVolumeScale * MassSound.VolumeScale, MassSound.PitchScale, ResolveDeathKillRank(), HalfHeight);
}

bool ATDMonsterCharacter::StartDeathRagdoll(const ETDDeathPresentation Presentation)
{
	if (IsRagdollPendingOrActive())
	{
		return true;
	}
	FTDDeathLaunchInput LaunchInput = MakeDeathLaunchInput(CombatComponent->GetLastDamageResult());
	LaunchInput.Presentation = Presentation;
	LaunchInput.KillRank = ResolveDeathKillRank();
	LaunchInput.bIsComboFinisher = bWasKilledByComboFinisher;
	LaunchInput.bHasHorizontalBodyPlan = Species && Species->bHasHorizontalBodyPlan;
	LaunchInput.RestHeightRatio = Species ? Species->CorpseRestHeightRatio : 0.f;
	return RequestDeathRagdoll(LaunchInput);
}

void ATDMonsterCharacter::ScheduleCorpseSink()
{
	UWorld* World = GetWorld();
	const float SinkSeconds = Species ? Species->CorpseSinkSeconds : 0.f;
	const float LifeSeconds = GetLifeSpan();
	if (!World || SinkSeconds <= 0.f || LifeSeconds <= 0.f)
	{
		return;
	}
	const float SinkDelaySeconds = LifeSeconds - SinkSeconds - TDMonsterCorpse::SinkEndMarginSeconds;
	if (SinkDelaySeconds <= 0.f)
	{
		BeginCorpseSink();
		return;
	}
	World->GetTimerManager().SetTimer(CorpseSinkTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		BeginCorpseSink();
	}), SinkDelaySeconds, false);
}

void ATDMonsterCharacter::BeginCorpseSink()
{
	UWorld* World = GetWorld();
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!World || !MeshComponent)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(DeathRagdollTimerHandle);
	StartPendingDeathRagdollNow(true);
	CancelScheduledRagdollEntry();
	StopDeathLaunchWatch();

	if (IsRagdolling())
	{
		StopRagdollSettle();
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		for (FBodyInstance* Body : MeshComponent->Bodies)
		{
			if (!Body)
			{
				continue;
			}
			Body->LinearDamping = TDMonsterCorpse::RagdollSinkLinearDamping;
			Body->UpdateDampingProperties();
		}
		MeshComponent->WakeAllRigidBodies();
		return;
	}

	if (UCharacterMoverComponent* Mover = GetMoverComponent())
	{
		Mover->SetPrimaryVisualComponent(nullptr);
	}
	CorpseSinkStartLocation = MeshComponent->GetRelativeLocation();
	CorpseSinkStartSeconds = World->GetTimeSeconds();
	CorpseSinkDepth = static_cast<float>(MeshComponent->Bounds.BoxExtent.Z) * 2.f + TDMonsterCorpse::SinkDepthMargin;
	CorpseSinkDurationSeconds = FMath::Max(FMath::Min(Species ? Species->CorpseSinkSeconds : 0.f, GetLifeSpan() - TDMonsterCorpse::SinkEndMarginSeconds), TDMonsterCorpse::MinimumSinkSeconds);
	World->GetTimerManager().SetTimer(CorpseSinkTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		AdvanceCorpseSink();
	}), TDMonsterCorpse::SinkStepSeconds, true);
}

void ATDMonsterCharacter::AdvanceCorpseSink()
{
	UWorld* World = GetWorld();
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!World || !MeshComponent)
	{
		return;
	}
	const float SinkAlpha = static_cast<float>(FMath::Clamp((World->GetTimeSeconds() - CorpseSinkStartSeconds) / CorpseSinkDurationSeconds, 0.0, 1.0));
	MeshComponent->SetRelativeLocation(CorpseSinkStartLocation - FVector(0.f, 0.f, CorpseSinkDepth * SinkAlpha));
	if (SinkAlpha >= 1.f)
	{
		World->GetTimerManager().ClearTimer(CorpseSinkTimerHandle);
	}
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
	const float ClipSeconds = PlaySpeciesClip(*Clip, PlayRate);
	const UWorld* World = GetWorld();
	AbilityClipAnimation = ClipSeconds > 0.f ? Cast<UAnimSequence>(Clip->Animation) : nullptr;
	AbilityClipPlayRate = FMath::Max(PlayRate, MinimumClipPlayRate);
	AbilityClipImpactAnimationSeconds = Clip->ImpactSeconds > 0.f ? Clip->StartSeconds + Clip->ImpactSeconds : 0.f;
	AbilityClipEndTime = World ? World->GetTimeSeconds() + ClipSeconds : 0.f;
	return ClipSeconds;
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

float ATDMonsterCharacter::PlaySpeciesReactionClip(const FTDMonsterAnimClip& Clip)
{
	UAnimSequence* Sequence = Cast<UAnimSequence>(Clip.Animation);
	if (!Sequence)
	{
		return 0.f;
	}
	return CharacterAnimation->PlayReactionAction(Sequence, FMath::Max(Clip.PlayRate, MinimumClipPlayRate), Clip.StartSeconds, Clip.EndSeconds);
}

void ATDMonsterCharacter::ScheduleMeleeSwingMessage(const UTDDamageDefinition& Spell, const float Range, const float WindupSeconds)

{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(MeleeSwingTimerHandle);
	if (!IsMeleeSwingSpell(Spell, Range))
	{
		return;
	}
	if (WindupSeconds <= 0.f)
	{
		BroadcastMeleeSwingMessage();
		return;
	}
	World->GetTimerManager().SetTimer(MeleeSwingTimerHandle, FTimerDelegate::CreateUObject(this, &ThisClass::BroadcastMeleeSwingMessage), WindupSeconds, false);
}

void ATDMonsterCharacter::BroadcastMeleeSwingMessage()
{
	UWorld* World = GetWorld();
	if (!World || !CombatComponent->IsAlive() || !UGameplayMessageSubsystem::HasInstance(World))
	{
		return;
	}
	FTDMeleeSwingMessage SwingMessage;
	SwingMessage.Attacker = this;
	SwingMessage.Location = GetActorLocation();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(TDGameplayTags::Event_Combat_MeleeSwing, SwingMessage);
}

void ATDMonsterCharacter::CatchUpAbilityClip()
{
	UWorld* World = GetWorld();
	const UAnimSequence* ClipAnimation = AbilityClipAnimation.Get();
	if (!World || !ClipAnimation || !CharacterAnimation->IsPlayingAction(ClipAnimation) || !IsInCommittedAttack())
	{
		return;
	}
	const float NowSeconds = World->GetTimeSeconds();
	const bool bIsBeforeImpact = NowSeconds < AbilityImpactTime && AbilityClipImpactAnimationSeconds > CharacterAnimation->GetActionAnimationTime();
	const float TargetAnimationSeconds = bIsBeforeImpact ? AbilityClipImpactAnimationSeconds : TNumericLimits<float>::Max();
	const float RemainingClipSeconds = CharacterAnimation->GetActionPlaybackSecondsUntil(TargetAnimationSeconds);
	const float RemainingRealSeconds = (bIsBeforeImpact ? AbilityImpactTime : AbilityClipEndTime) - NowSeconds;
	if (RemainingClipSeconds <= 0.f)
	{
		return;
	}
	const float CatchUpScale = RemainingRealSeconds > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(RemainingClipSeconds / RemainingRealSeconds, MinimumClipCatchUpScale, MaximumClipCatchUpScale) : MaximumClipCatchUpScale;
	if (FMath::IsNearlyEqual(CatchUpScale, 1.f, ClipCatchUpScaleTolerance))
	{
		return;
	}
	CharacterAnimation->SetActionPlayRate(AbilityClipPlayRate * CatchUpScale);
	World->GetTimerManager().SetTimer(AbilityClipRestoreTimerHandle, FTimerDelegate::CreateUObject(this, &ThisClass::RestoreAbilityClipPlayRate), RemainingClipSeconds / CatchUpScale, false);
}

void ATDMonsterCharacter::RestoreAbilityClipPlayRate()
{
	const UAnimSequence* ClipAnimation = AbilityClipAnimation.Get();
	if (!ClipAnimation || !CharacterAnimation->IsPlayingAction(ClipAnimation))
	{
		return;
	}
	CharacterAnimation->SetActionPlayRate(AbilityClipPlayRate);
}


void ATDMonsterCharacter::SetBodyChanneling(const bool bIsChanneling)
{
	if (!bIsChanneling)
	{
		if (ChannelEffectComponent)
		{
			ChannelEffectComponent->DestroyComponent();
			ChannelEffectComponent = nullptr;
		}
		return;
	}
	if (!CombatComponent->IsAlive())
	{
		return;
	}
	if (Species && Species->ChannelClip.IsValidClip())
	{
		PlaySpeciesClip(Species->ChannelClip, Species->ChannelClip.PlayRate);
	}
	UNiagaraSystem* Effect = Cast<UNiagaraSystem>(FSoftObjectPath(ChannelEffectPath).ResolveObject());
	if (!Effect || ChannelEffectComponent)
	{
		return;
	}
	const FVector EffectScale(GetBodyRadius() / ChannelEffectRadiusPerScale);
	ChannelEffectComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(Effect, GetRootComponent(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, EffectScale, EAttachLocation::KeepRelativeOffset, false, ENCPoolMethod::None);
}

void ATDMonsterCharacter::SetBodyDashing(const bool bIsDashing)
{
	UCommonLegacyMovementSettings* MovementSettings = MoverComponent ? MoverComponent->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>() : nullptr;
	if (!MovementSettings)
	{
		return;
	}
	MovementSettings->Acceleration = bIsDashing ? DashAcceleration : MoveAcceleration;
}

void ATDMonsterCharacter::RequestChannelEffect()
{
	const FSoftObjectPath EffectPath(ChannelEffectPath);
	if (ChannelEffectHandle.IsValid() || EffectPath.ResolveObject() || !FPackageName::DoesPackageExist(EffectPath.GetLongPackageName()))
	{
		return;
	}
	ChannelEffectHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(EffectPath, FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority);
}

void ATDMonsterCharacter::ApplyPhasePresentation(const FName PhaseId)
{
	const FTDMonsterPhasePresentation* Presentation = Species ? Species->PhasePresentations.Find(PhaseId) : nullptr;
	if (!Presentation)
	{
		return;
	}
	ActivePresentationPhaseId = PhaseId;
	const FTDMonsterAnimClip& WalkClip = Presentation->Walk.IsValidClip() ? Presentation->Walk : Species->Walk;
	const FTDMonsterAnimClip& RunClip = Presentation->Run.IsValidClip() ? Presentation->Run : Species->Run;
	CharacterAnimation->ReplaceWalkAndRunClips(MakeLocomotionClipSet(*Species, Species->Idle, WalkClip, RunClip));
	if (!Presentation->FollowerMeshes.IsEmpty())
	{
		ApplyFollowerMeshes(Presentation->FollowerMeshes);
	}
}

const FTDMonsterPhasePresentation* ATDMonsterCharacter::FindActivePhasePresentation() const
{
	return Species && !ActivePresentationPhaseId.IsNone() ? Species->PhasePresentations.Find(ActivePresentationPhaseId) : nullptr;
}

const FTDMonsterAnimClip& ATDMonsterCharacter::SelectHitClip() const
{
	const FTDMonsterPhasePresentation* Presentation = FindActivePhasePresentation();
	return Presentation && Presentation->Hit.IsValidClip() ? Presentation->Hit : Species->Hit;
}

const FTDMonsterAnimClip& ATDMonsterCharacter::SelectDeathClip() const
{
	const FTDMonsterPhasePresentation* Presentation = FindActivePhasePresentation();
	return Presentation && Presentation->Death.IsValidClip() ? Presentation->Death : Species->Death;
}

void ATDMonsterCharacter::HandleDeathRagdollFailed()
{
	if (!Species || !CharacterAnimation || CharacterAnimation->IsPlayingDeath())
	{
		return;
	}
	ScheduleDeathClipEnd(PlayDeathClip(), false);
}

float ATDMonsterCharacter::PlayDeathClip()
{
	if (!Species || !CharacterAnimation)
	{
		return 0.f;
	}
	const FTDMonsterAnimClip& DeathClip = SelectDeathClip();
	UAnimSequence* DeathSequence = Cast<UAnimSequence>(DeathClip.Animation);
	if (!DeathSequence)
	{
		return 0.f;
	}
	return CharacterAnimation->PlayDeath(DeathSequence, FMath::Max(DeathClip.PlayRate, MinimumClipPlayRate), DeathClip.EndSeconds);
}

ETDDeathPresentation ATDMonsterCharacter::ResolveMonsterDeathPresentation() const
{
	if (!Species)
	{
		return ETDDeathPresentation::ClipOnly;
	}
	const USkeletalMeshComponent* MeshComponent = GetMesh();
	FTDDeathPresentationInput PresentationInput;
	PresentationInput.bIsBodyAirborne = bIsBodyFlying || bIsBodyLanding;
	PresentationInput.bHasRagdollPhysicsAsset = MeshComponent && MeshComponent->GetPhysicsAsset();
	PresentationInput.bShouldPlayDeathClipBeforeRagdoll = Species->bPlayDeathClipBeforeRagdoll;
	PresentationInput.bHasDeathClip = Cast<UAnimSequence>(SelectDeathClip().Animation) != nullptr;
	PresentationInput.bShouldRagdollAfterDeathClip = Species->bRagdollAfterDeathClip;
	PresentationInput.bShouldUseRagdollWithoutDeathClip = Species->bUseRagdollWithoutDeathClip;
	return TDDeathLaunchRules::ResolveDeathPresentation(PresentationInput);
}

ETDKillRank ATDMonsterCharacter::ResolveDeathKillRank() const
{
	if (IsBossMonster())
	{
		return ETDKillRank::Boss;
	}
	if (IsEliteMonster())
	{
		return ETDKillRank::Elite;
	}
	return ETDKillRank::Normal;
}

void ATDMonsterCharacter::RecordKillingBlow(const FTDDamageContext& Context)
{
	const AActor* Caster = Context.Caster.Get();
	const UTDSkillComponent* CasterSkills = Caster ? Caster->FindComponentByClass<UTDSkillComponent>() : nullptr;
	bWasKilledByComboFinisher = Context.Delivery == ETDDamageDelivery::Melee && CasterSkills && CasterSkills->IsPrimaryComboFinisherActive();
	const FTDDamageResult& KillingResult = CombatComponent->GetLastDamageResult();
	bWasKilledByGroundArea = Context.Delivery == ETDDamageDelivery::Spell && KillingResult.SwingDirection.IsNearlyZero() && !Context.CastTarget.IsNearlyZero();
	UWorld* World = GetWorld();
	UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
	MultiKillIndex = Feedback && Caster ? Feedback->RecordAttackerKill(*Caster) : 0;
	GroundAreaKillDistance = bWasKilledByGroundArea ? static_cast<float>(FVector::Dist(GetActorLocation(), Context.CastTarget)) : 0.f;
}
