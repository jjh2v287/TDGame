#include "Characters/TDCombatCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Backends/MoverStandaloneLiaison.h"
#include "Characters/TDCapsuleModifierComponent.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDKnockbackLayeredMove.h"
#include "CollisionQueryParams.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"
#include "Combat/Feedback/TDDeathLaunchRules.h"
#include "Combat/GAS/TDCombatAttributeSet.h"
#include "Combat/TDCombatComponent.h"
#include "Component/AnimNextComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TDGameplayTags.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "Engine/CollisionProfile.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "Materials/MaterialInterface.h"
#include "Module/UAFWeakSystemReference.h"
#include "MovementMode.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "TimerManager.h"

const FName ATDCombatCharacter::CapsuleComponentName(TEXT("CollisionCylinder"));
const FName ATDCombatCharacter::MeshComponentName(TEXT("CharacterMesh0"));

namespace TDCombatCharacterMovement
{
	constexpr float StandardCapsuleRadius = 34.f;
	constexpr float StandardCapsuleHalfHeight = 88.f;
	constexpr float MinimumOrientationInputSize = 1e-3f;
	constexpr float PresentationOffsetTolerance = 0.01f;
	constexpr float RagdollMaxDepenetrationVelocity = 150.f;
	constexpr float RagdollLinearDamping = 0.6f;
	constexpr float RagdollAngularDamping = 1.f;
	constexpr float RagdollSettleCheckSeconds = 0.1f;
	constexpr float RagdollSettleMinSeconds = 1.f;
	constexpr float RagdollSettleForceSeconds = 2.f;
	constexpr float RagdollFirmSeconds = 2.4f;
	constexpr float RagdollSleepCapSeconds = 3.f;
	constexpr float RagdollSleepForceSeconds = 4.f;
	constexpr float RagdollSettleMeanSpeed = 15.f;
	constexpr float RagdollSleepBodySpeed = 20.f;
	constexpr float RagdollSleepCapBodySpeed = 30.f;
	constexpr float RagdollSettleDamping = 4.f;
	constexpr float RagdollFirmDamping = 12.f;
	constexpr float RagdollPosePushRenderToleranceSeconds = 0.1f;

	const FName& GetRagdollCollisionProfileName()
	{
		static const FName ProfileName(TEXT("Ragdoll"));
		return ProfileName;
	}

	const FName& GetAnimationPrePhysicsEventName()
	{
		static const FName EventName(TEXT("PrePhysics"));
		return EventName;
	}

	bool IsMoverReady(const UMoverComponent* MoverComponent)
	{
		return MoverComponent && MoverComponent->HasBegunPlay();
	}

	bool IsRestorableMovementMode(const FName MovementModeName)
	{
		return !MovementModeName.IsNone() && MovementModeName != UNullMovementMode::NullModeName;
	}

	FVector MakePlanarDirection(const FRotator& Rotation)
	{
		return FRotator(0.f, Rotation.Yaw, 0.f).Vector();
	}

	struct FTDRagdollMotion
	{
		float MeanSpeed = 0.f;
		float MaxBodySpeed = 0.f;
	};

	void SetRagdollDamping(USkeletalMeshComponent& MeshComponent, float LinearDamping, float AngularDamping)
	{
		for (FBodyInstance* Body : MeshComponent.Bodies)
		{
			if (!Body)
			{
				continue;
			}
			Body->LinearDamping = LinearDamping;
			Body->AngularDamping = AngularDamping;
			Body->UpdateDampingProperties();
		}
	}

	void ConfigureRagdollBodies(USkeletalMeshComponent& MeshComponent)
	{
		for (FBodyInstance* Body : MeshComponent.Bodies)
		{
			if (Body)
			{
				Body->SetMaxDepenetrationVelocity(RagdollMaxDepenetrationVelocity);
			}
		}
		SetRagdollDamping(MeshComponent, RagdollLinearDamping, RagdollAngularDamping);
	}

	FTDRagdollMotion MeasureRagdollMotion(const USkeletalMeshComponent& MeshComponent)
	{
		FTDRagdollMotion Motion;
		FVector Momentum = FVector::ZeroVector;
		float TotalMass = 0.f;
		for (const FBodyInstance* Body : MeshComponent.Bodies)
		{
			if (!Body)
			{
				continue;
			}
			const FVector Velocity = Body->GetUnrealWorldVelocity();
			const float Mass = Body->GetBodyMass();
			Momentum += Velocity * Mass;
			TotalMass += Mass;
			Motion.MaxBodySpeed = FMath::Max(Motion.MaxBodySpeed, static_cast<float>(Velocity.Size()));
		}
		Motion.MeanSpeed = TotalMass > 0.f ? static_cast<float>(Momentum.Size()) / TotalMass : 0.f;
		return Motion;
	}

	void GrantStartupAbilitiesAndEffects(UTDCombatComponent& CombatComponent, const TArray<TSubclassOf<UGameplayAbility>>& Abilities, const TArray<TSubclassOf<UGameplayEffect>>& Effects, UObject* SourceObject)
	{
		const int32 AbilityLevel = CombatComponent.GetStats().Level;
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : Abilities)
		{
			if (AbilityClass)
			{
				CombatComponent.GiveAbility(FGameplayAbilitySpec(AbilityClass, AbilityLevel, INDEX_NONE, SourceObject));
			}
		}

		FGameplayEffectContextHandle EffectContext = CombatComponent.MakeEffectContext();
		EffectContext.AddSourceObject(SourceObject);
		for (const TSubclassOf<UGameplayEffect>& EffectClass : Effects)
		{
			if (EffectClass)
			{
				CombatComponent.ApplyGameplayEffectToSelf(EffectClass.GetDefaultObject(), AbilityLevel, EffectContext);
			}
		}
	}
}

ATDCombatCharacter::ATDCombatCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetReplicatingMovement(false);

	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(CapsuleComponentName);
	CapsuleComponent->InitCapsuleSize(TDCombatCharacterMovement::StandardCapsuleRadius, TDCombatCharacterMovement::StandardCapsuleHalfHeight);
	CapsuleComponent->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	CapsuleComponent->CanCharacterStepUpOn = ECB_No;
	CapsuleComponent->SetShouldUpdatePhysicsVolume(true);
	CapsuleComponent->SetCanEverAffectNavigation(false);
	CapsuleComponent->bDynamicObstacle = true;
	RootComponent = CapsuleComponent;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(MeshComponentName);
	Mesh->SetupAttachment(CapsuleComponent);
	Mesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -TDCombatCharacterMovement::StandardCapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
	Mesh->AlwaysLoadOnClient = true;
	Mesh->AlwaysLoadOnServer = true;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	Mesh->bCastDynamicShadow = true;
	Mesh->bAffectDynamicIndirectLighting = true;
	Mesh->PrimaryComponentTick.TickGroup = TG_PrePhysics;
	Mesh->SetCollisionProfileName(TEXT("CharacterMesh"));
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetEnableAnimation(false);

	MoverComponent = CreateDefaultSubobject<UCharacterMoverComponent>(TEXT("MoverComponent"));
	MoverComponent->BackendClass = UMoverStandaloneLiaisonComponent::StaticClass();
	MoverComponent->bAcceptExternalMovement = true;
	MoverComponent->bWarnOnExternalMovement = false;
	MoverComponent->StartingMovementMode = DefaultModeNames::Walking;

	NavMoverComponent = CreateDefaultSubobject<UNavMoverComponent>(TEXT("NavMoverComponent"));
	UAFComponent = CreateDefaultSubobject<UUAFComponent>(TEXT("UAFComponent"));
	CharacterAnimation = CreateDefaultSubobject<UTDCharacterAnimationComponent>(TEXT("CharacterAnimation"));

	CombatAttributes = CreateDefaultSubobject<UTDCombatAttributeSet>(TEXT("CombatAttributes"));
	CombatComponent = CreateDefaultSubobject<UTDCombatComponent>(TEXT("CombatComponent"));
}

UAbilitySystemComponent* ATDCombatCharacter::GetAbilitySystemComponent() const
{
	return CombatComponent;
}

void ATDCombatCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (!MoverComponent)
	{
		return;
	}
	MoverComponent->SetUpdatedComponent(CapsuleComponent);
	MoverComponent->SetPrimaryVisualComponent(Mesh);
}

void ATDCombatCharacter::BeginPlay()
{
	CombatComponent->AddAttributeSetSubobject(CombatAttributes.Get());
	CombatComponent->InitAbilityActorInfo(this, this);
	Super::BeginPlay();

	if (HasAuthority())
	{
		TDCombatCharacterMovement::GrantStartupAbilitiesAndEffects(*CombatComponent, StartupAbilities, StartupEffects, this);
	}

	ApplyMovementSettings();
	LinkAnimationBeforeMovement();
	MoverComponent->OnMovementModeChanged.AddUniqueDynamic(this, &ThisClass::HandleMovementModeChanged);
	CombatComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeferredMovementOwnerDeath);
}

void ATDCombatCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearPendingDeathRagdoll();
	CancelScheduledRagdollEntry();
	StopDeathLaunchWatch();
	StopRagdollPosePush();
	ClearDeferredKnockback();
	Super::EndPlay(EndPlayReason);
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

FVector ATDCombatCharacter::GetNavAgentLocation() const
{
	const FVector FeetLocation = NavMoverComponent ? NavMoverComponent->GetFeetLocation() : FNavigationSystem::InvalidLocation;
	if (FNavigationSystem::IsValidLocation(FeetLocation))
	{
		return FeetLocation;
	}
	if (!CapsuleComponent)
	{
		return GetActorLocation();
	}
	return CapsuleComponent->GetComponentLocation() - FVector(0.f, 0.f, CapsuleComponent->GetScaledCapsuleHalfHeight());
}

void ATDCombatCharacter::UpdateNavigationRelevance()
{
	if (CapsuleComponent)
	{
		CapsuleComponent->SetCanEverAffectNavigation(bCanAffectNavigationGeneration);
	}
}

bool ATDCombatCharacter::CastDamageSpell(int32 Slot, const FVector& Target)
{
	if (!DamageSpells.IsValidIndex(Slot) || !IsValid(DamageSpells[Slot]))
	{
		return false;
	}
	return CombatComponent->TryCastDamageDefinition(DamageSpells[Slot], Target);
}

bool ATDCombatCharacter::IsMovingOnGround() const
{
	return MoverComponent && MoverComponent->IsOnGround();
}

bool ATDCombatCharacter::IsAirborne() const
{
	return MoverComponent && MoverComponent->IsAirborne();
}

bool ATDCombatCharacter::CanJump() const
{
	if (bIsMovementDisabled || bIsMovementFrozen || !TDCombatCharacterMovement::IsMoverReady(MoverComponent))
	{
		return false;
	}
	return MoverComponent->CanActorJump();
}

bool ATDCombatCharacter::Jump()
{
	if (!CanJump())
	{
		return false;
	}
	return MoverComponent->Jump();
}

void ATDCombatCharacter::SetMaxMoveSpeed(float NewMaxSpeed)
{
	UCommonLegacyMovementSettings* MovementSettings = MoverComponent ? MoverComponent->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>() : nullptr;
	if (!MovementSettings)
	{
		return;
	}
	MovementSettings->MaxSpeed = FMath::Max(NewMaxSpeed, 0.f);
}

float ATDCombatCharacter::GetMaxMoveSpeed() const
{
	const UCommonLegacyMovementSettings* MovementSettings = MoverComponent ? MoverComponent->FindSharedSettings<UCommonLegacyMovementSettings>() : nullptr;
	return MovementSettings ? MovementSettings->MaxSpeed : DefaultMaxMoveSpeed;
}

void ATDCombatCharacter::StopMovementImmediately()
{
	if (!TDCombatCharacterMovement::IsMoverReady(MoverComponent))
	{
		return;
	}
	MoverComponent->QueueInstantMovementEffect(MakeShared<FApplyVelocityEffect>());
}

void ATDCombatCharacter::DisableMovement()
{
	if (bIsMovementDisabled)
	{
		return;
	}
	bIsMovementDisabled = true;
	StopMovementImmediately();
	if (MoverComponent)
	{
		MoverComponent->QueueNextMode(UNullMovementMode::NullModeName);
	}
}

void ATDCombatCharacter::EnableMovement()
{
	if (!bIsMovementDisabled)
	{
		return;
	}
	bIsMovementDisabled = false;
	if (bIsMovementFrozen || !MoverComponent)
	{
		return;
	}
	MoverComponent->QueueNextMode(DefaultModeNames::Falling);
}

void ATDCombatCharacter::AddImpulseVelocity(const FVector& VelocityChange)
{
	if (VelocityChange.IsNearlyZero())
	{
		return;
	}
	DeferredImpulseVelocities.Add(VelocityChange);
	ScheduleDeferredMovement();
}

void ATDCombatCharacter::ScheduleDeferredMovement()
{
	UWorld* World = GetWorld();
	if (!World || World->GetTimerManager().TimerExists(DeferredMovementTimerHandle))
	{
		return;
	}
	DeferredMovementTimerHandle = World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ThisClass::ResolveDeferredMovement));
}

void ATDCombatCharacter::ClearDeferredKnockback()
{
	DeferredImpulseVelocities.Reset();
	DeferredKnockbacks.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredMovementTimerHandle);
	}
	DeferredMovementTimerHandle.Invalidate();
}

void ATDCombatCharacter::HandleDeferredMovementOwnerDeath(const FTDDamageContext& Context)
{
	ClearDeferredKnockback();
}

void ATDCombatCharacter::ResolveDeferredMovement()
{
	DeferredMovementTimerHandle.Invalidate();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FTimerManager& TimerManager = World->GetTimerManager();
	if (CharacterAnimation && CharacterAnimation->IsHitStopPending())
	{
		DeferredMovementTimerHandle = TimerManager.SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ThisClass::ResolveDeferredMovement));
		return;
	}
	const float HoldRealSeconds = CharacterAnimation && CharacterAnimation->IsVictimHitStopActive() ? CharacterAnimation->GetVictimHitStopRemainingSeconds() : 0.f;
	if (HoldRealSeconds > 0.f)
	{
		TimerManager.SetTimer(DeferredMovementTimerHandle, FTimerDelegate::CreateUObject(this, &ThisClass::ResolveDeferredMovement), UTDCharacterAnimationComponent::ConvertRealToWorldSeconds(*World, HoldRealSeconds), false);
		return;
	}
	const TArray<FVector, TInlineAllocator<2>> ImpulseVelocities = MoveTemp(DeferredImpulseVelocities);
	const TArray<FTDDeferredKnockback, TInlineAllocator<2>> Knockbacks = MoveTemp(DeferredKnockbacks);
	DeferredImpulseVelocities.Reset();
	DeferredKnockbacks.Reset();
	for (const FVector& ImpulseVelocity : ImpulseVelocities)
	{
		ApplyImpulseVelocityNow(ImpulseVelocity);
	}
	for (const FTDDeferredKnockback& Knockback : Knockbacks)
	{
		ApplyKnockbackNow(Knockback.PlanarVelocity, Knockback.Seconds, Knockback.bUsesLinearDecay);
	}
}

void ATDCombatCharacter::ApplyImpulseVelocityNow(const FVector& VelocityChange)
{
	if (bIsMovementDisabled || bIsMovementFrozen || !TDCombatCharacterMovement::IsMoverReady(MoverComponent) || VelocityChange.IsNearlyZero())
	{
		return;
	}
	const TSharedPtr<FApplyVelocityEffect> VelocityEffect = MakeShared<FApplyVelocityEffect>();
	VelocityEffect->VelocityToApply = VelocityChange;
	VelocityEffect->bAdditiveVelocity = true;
	if (VelocityChange.Z > 0.f)
	{
		VelocityEffect->ForceMovementMode = DefaultModeNames::Falling;
	}
	MoverComponent->QueueInstantMovementEffect(VelocityEffect);
}

void ATDCombatCharacter::FaceRotationImmediately(const FRotator& NewRotation)
{
	const FRotator UprightRotation(0.f, NewRotation.Yaw, 0.f);
	DesiredFacingDirection = TDCombatCharacterMovement::MakePlanarDirection(UprightRotation);
	SetActorRotation(UprightRotation);
	if (!TDCombatCharacterMovement::IsMoverReady(MoverComponent))
	{
		return;
	}
	const TSharedPtr<FTeleportEffect> RotationEffect = MakeShared<FTeleportEffect>();
	RotationEffect->TargetLocation = GetActorLocation();
	RotationEffect->bUseActorRotation = false;
	RotationEffect->TargetRotation = UprightRotation;
	MoverComponent->QueueInstantMovementEffect(RotationEffect);
}

void ATDCombatCharacter::TeleportPawn(const FVector& NewLocation, const FRotator& NewRotation)
{
	const FRotator UprightRotation(0.f, NewRotation.Yaw, 0.f);
	DesiredFacingDirection = TDCombatCharacterMovement::MakePlanarDirection(UprightRotation);
	if (!TeleportTo(NewLocation, UprightRotation))
	{
		SetActorLocationAndRotation(NewLocation, UprightRotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (!TDCombatCharacterMovement::IsMoverReady(MoverComponent))
	{
		return;
	}
	const TSharedPtr<FTeleportEffect> TeleportEffect = MakeShared<FTeleportEffect>();
	TeleportEffect->TargetLocation = GetActorLocation();
	TeleportEffect->bUseActorRotation = false;
	TeleportEffect->TargetRotation = UprightRotation;
	MoverComponent->QueueInstantMovementEffect(TeleportEffect);
}

void ATDCombatCharacter::SetDesiredFacingDirection(const FVector& Direction)
{
	DesiredFacingDirection = Direction.GetSafeNormal2D();
}

void ATDCombatCharacter::ClearDesiredFacingDirection()
{
	DesiredFacingDirection = FVector::ZeroVector;
}

void ATDCombatCharacter::SetMovementFrozen(bool bIsFrozen)
{
	if (bIsMovementFrozen == bIsFrozen)
	{
		return;
	}
	bIsMovementFrozen = bIsFrozen;
	if (CharacterAnimation)
	{
		CharacterAnimation->SetAnimationFrozen(bIsFrozen);
	}
	if (!MoverComponent)
	{
		return;
	}

	if (bIsFrozen)
	{
		MovementModeBeforeFreeze = MoverComponent->GetMovementModeName();
		StopMovementImmediately();
		MoverComponent->QueueNextMode(UNullMovementMode::NullModeName);
		return;
	}

	const FName MovementModeToRestore = MovementModeBeforeFreeze;
	MovementModeBeforeFreeze = NAME_None;
	if (bIsMovementDisabled)
	{
		return;
	}
	MoverComponent->QueueNextMode(TDCombatCharacterMovement::IsRestorableMovementMode(MovementModeToRestore) ? MovementModeToRestore : DefaultModeNames::Falling);
}

void ATDCombatCharacter::SetHitFlashOverlay(UMaterialInterface* Material)
{
	if (!Mesh || Mesh->GetOverlayMaterial() == Material)
	{
		return;
	}
	Mesh->SetOverlayMaterial(Material);
}

void ATDCombatCharacter::SetPresentationOffset(const FVector& WorldOffset)
{
	if (!MoverComponent || !Mesh || !CapsuleComponent || bIsRagdolling || MoverComponent->GetPrimaryVisualComponent() != Mesh.Get() || IsCapsuleModifierActive())
	{
		LastPresentationOffsetLocal = FVector::ZeroVector;
		return;
	}

	const FVector NewOffsetLocal = CapsuleComponent->GetComponentTransform().InverseTransformVector(WorldOffset);
	if (NewOffsetLocal.Equals(LastPresentationOffsetLocal, TDCombatCharacterMovement::PresentationOffsetTolerance))
	{
		return;
	}
	FTransform VisualTransform = MoverComponent->GetBaseVisualComponentTransform();
	VisualTransform.SetLocation(VisualTransform.GetLocation() - LastPresentationOffsetLocal + NewOffsetLocal);
	MoverComponent->SetBaseVisualComponentTransform(VisualTransform);
	Mesh->SetRelativeTransform(VisualTransform);
	LastPresentationOffsetLocal = NewOffsetLocal;
}

void ATDCombatCharacter::ApplyKnockback(const FVector& PlanarVelocity, float Seconds)
{
	QueueKnockback(PlanarVelocity, Seconds, false);
}

void ATDCombatCharacter::ApplyLinearKnockback(const FVector& PlanarVelocity, float Seconds)
{
	QueueKnockback(PlanarVelocity, Seconds, true);
}

void ATDCombatCharacter::QueueKnockback(const FVector& PlanarVelocity, float Seconds, bool bUsesLinearDecay)
{
	const FVector Velocity(PlanarVelocity.X, PlanarVelocity.Y, 0.f);
	if (Velocity.IsNearlyZero() || Seconds <= 0.f)
	{
		return;
	}
	DeferredKnockbacks.Add(FTDDeferredKnockback{ Velocity, Seconds, bUsesLinearDecay });
	ScheduleDeferredMovement();
}

void ATDCombatCharacter::ApplyKnockbackNow(const FVector& PlanarVelocity, float Seconds, bool bUsesLinearDecay)
{
	const FVector Velocity(PlanarVelocity.X, PlanarVelocity.Y, 0.f);

	if (bIsMovementDisabled || bIsMovementFrozen || bIsRagdolling || !TDCombatCharacterMovement::IsMoverReady(MoverComponent) || Velocity.IsNearlyZero() || Seconds <= 0.f)
	{
		return;
	}
	if (IsKnockbackSuppressed())
	{
		return;
	}
	const TSharedPtr<FTDKnockbackLayeredMove> KnockbackMove = MakeShared<FTDKnockbackLayeredMove>();
	KnockbackMove->Velocity = Velocity;
	KnockbackMove->DurationMs = Seconds * 1000.f;
	KnockbackMove->bUsesLinearDecay = bUsesLinearDecay;
	MoverComponent->QueueLayeredMove(KnockbackMove);
}

bool ATDCombatCharacter::StartRagdoll(const FTDDeathLaunchInput& Input)
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	if (!MeshComponent || !MeshComponent->GetPhysicsAsset())
	{
		CancelScheduledRagdollEntry();
		return false;
	}
	if (bIsRagdolling)
	{
		return true;
	}

	DeathLaunchKillRank = Input.KillRank;
	bHasHorizontalDeathBodyPlan = Input.bHasHorizontalBodyPlan;
	SetPresentationOffset(FVector::ZeroVector);
	bIsRagdolling = true;
	if (CharacterAnimation)
	{
		CharacterAnimation->HoldPoseForDeath();
	}
	if (MoverComponent)
	{
		MoverComponent->SetPrimaryVisualComponent(nullptr);
	}
	MeshComponent->UpdateKinematicBonesToAnim(MeshComponent->GetComponentSpaceTransforms(), ETeleportType::TeleportPhysics, true);
	SetActorEnableCollision(true);
	if (CapsuleComponent)
	{
		CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	MeshComponent->SetCollisionProfileName(TDCombatCharacterMovement::GetRagdollCollisionProfileName());
	MeshComponent->SetComponentSpaceTransformsDoubleBuffering(false);
	MeshComponent->SetAllBodiesSimulatePhysics(true);
	MeshComponent->SetSimulatePhysics(true);
	TDCombatCharacterMovement::ConfigureRagdollBodies(*MeshComponent);
	MeshComponent->WakeAllRigidBodies();
	MeshComponent->bBlendPhysics = true;
	bWasRagdollAwakeAtLastPoseFinalize = true;
	bHasUnpushedRagdollPose = true;
	RagdollPoseFinalizedHandle = MeshComponent->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &ATDCombatCharacter::PushRagdollPoseToRenderer));
	ApplyDeathLaunch(*MeshComponent, Input);
	bHasScheduledRagdollEntry = false;
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	if (UTDCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UTDCombatFeedbackSubsystem>())
	{
		Feedback->NotifyRagdollEntered(*this);
	}
	RagdollStartSeconds = World->GetTimeSeconds();
	bIsRagdollSettling = false;
	bIsRagdollFirm = false;
	World->GetTimerManager().SetTimer(RagdollSettleTimerHandle, this, &ATDCombatCharacter::UpdateRagdollSettle, TDCombatCharacterMovement::RagdollSettleCheckSeconds, true);
	return true;
}

void ATDCombatCharacter::UpdateRagdollSettle()
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	const UWorld* World = GetWorld();
	if (!bIsRagdolling || !MeshComponent || !World)
	{
		StopRagdollSettle();
		return;
	}
	const double ElapsedSeconds = World->GetTimeSeconds() - RagdollStartSeconds;
	if (!MeshComponent->IsAnyRigidBodyAwake() || ElapsedSeconds < TDCombatCharacterMovement::RagdollSettleMinSeconds)
	{
		return;
	}
	const TDCombatCharacterMovement::FTDRagdollMotion Motion = TDCombatCharacterMovement::MeasureRagdollMotion(*MeshComponent);
	if (!bIsRagdollSettling)
	{
		if (Motion.MeanSpeed > TDCombatCharacterMovement::RagdollSettleMeanSpeed && ElapsedSeconds < TDCombatCharacterMovement::RagdollSettleForceSeconds)
		{
			return;
		}
		bIsRagdollSettling = true;
		DisableDeathLaunchCcd();
		TDCombatCharacterMovement::SetRagdollDamping(*MeshComponent, TDCombatCharacterMovement::RagdollSettleDamping, TDCombatCharacterMovement::RagdollSettleDamping);
		ApplyRagdollDamping(*MeshComponent);
		return;
	}
	if (!bIsRagdollFirm && ElapsedSeconds >= TDCombatCharacterMovement::RagdollFirmSeconds)
	{
		bIsRagdollFirm = true;
		TDCombatCharacterMovement::SetRagdollDamping(*MeshComponent, TDCombatCharacterMovement::RagdollFirmDamping, TDCombatCharacterMovement::RagdollFirmDamping);
	}
	const bool bIsStill = Motion.MaxBodySpeed <= TDCombatCharacterMovement::RagdollSleepBodySpeed;
	const bool bIsStillPastSleepCap = ElapsedSeconds >= TDCombatCharacterMovement::RagdollSleepCapSeconds && Motion.MaxBodySpeed <= TDCombatCharacterMovement::RagdollSleepCapBodySpeed;
	const bool bIsJitteringInPlace = ElapsedSeconds >= TDCombatCharacterMovement::RagdollSleepForceSeconds && Motion.MeanSpeed <= TDCombatCharacterMovement::RagdollSettleMeanSpeed;
	if (bIsStill || bIsStillPastSleepCap || bIsJitteringInPlace)
	{
		MeshComponent->PutAllRigidBodiesToSleep();
	}
}

void ATDCombatCharacter::StopRagdollSettle()
{
	StopDeathLaunchWatch();
	bIsRagdollSettling = false;
	bIsRagdollFirm = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RagdollSettleTimerHandle);
	}
}

void ATDCombatCharacter::PushRagdollPoseToRenderer()
{
	if (!bIsRagdolling || !Mesh)
	{
		return;
	}
	const bool bIsRagdollAwake = Mesh->IsAnyRigidBodyAwake();
	bHasUnpushedRagdollPose |= bIsRagdollAwake || bWasRagdollAwakeAtLastPoseFinalize;
	bWasRagdollAwakeAtLastPoseFinalize = bIsRagdollAwake;
	if (!bHasUnpushedRagdollPose || !Mesh->WasRecentlyRendered(TDCombatCharacterMovement::RagdollPosePushRenderToleranceSeconds))
	{
		return;
	}
	bHasUnpushedRagdollPose = false;
	Mesh->ForceMotionVector();
}

void ATDCombatCharacter::StopRagdollPosePush()
{
	if (!RagdollPoseFinalizedHandle.IsValid())
	{
		return;
	}
	if (Mesh)
	{
		Mesh->UnregisterOnBoneTransformsFinalizedDelegate(RagdollPoseFinalizedHandle);
	}
	RagdollPoseFinalizedHandle.Reset();
}

FVector ATDCombatCharacter::ResolveHitPlanarDirection(const FTDDamageResult& Result) const
{
	const FVector HitDirection = Result.HitDirection.GetSafeNormal2D();
	if (!HitDirection.IsNearlyZero())
	{
		return HitDirection;
	}
	return (-GetActorForwardVector()).GetSafeNormal2D();
}

void ATDCombatCharacter::ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult)
{
	FVector NavMoveIntent = FVector::ZeroVector;
	FVector NavMoveVelocity = FVector::ZeroVector;
	if (NavMoverComponent)
	{
		NavMoverComponent->ConsumeNavMovementData(NavMoveIntent, NavMoveVelocity);
	}
	const FVector PawnMoveIntent = ConsumeMoveIntent();

	FCharacterDefaultInputs& CharacterInputs = InputCmdResult.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
	const AController* OwningController = GetController();
	if (OwningController)
	{
		CharacterInputs.ControlRotation = OwningController->GetControlRotation();
	}

	if (bIsMovementDisabled || bIsMovementFrozen || IsMoveInputBlockedByStun())
	{
		CharacterInputs.SetMoveInput(EMoveInputType::DirectionalIntent, FVector::ZeroVector);
		CharacterInputs.OrientationIntent = FVector::ZeroVector;
		return;
	}

	if (!NavMoveVelocity.IsZero())
	{
		CharacterInputs.SetMoveInput(EMoveInputType::Velocity, NavMoveVelocity);
	}
	else
	{
		CharacterInputs.SetMoveInput(EMoveInputType::DirectionalIntent, (PawnMoveIntent + NavMoveIntent).GetClampedToMaxSize(1.f));
	}

	if (FacingMode == ETDFacingMode::ControlRotation && OwningController)
	{
		CharacterInputs.OrientationIntent = TDCombatCharacterMovement::MakePlanarDirection(CharacterInputs.ControlRotation);
		return;
	}

	const FVector MoveInput = CharacterInputs.GetMoveInput();
	const bool bHasMoveInput = MoveInput.Size2D() >= TDCombatCharacterMovement::MinimumOrientationInputSize;
	if (FacingMode == ETDFacingMode::MovementDirection && bHasMoveInput)
	{
		DesiredFacingDirection = FVector::ZeroVector;
		CharacterInputs.OrientationIntent = MoveInput.GetSafeNormal2D();
		return;
	}
	CharacterInputs.OrientationIntent = DesiredFacingDirection;
}

void ATDCombatCharacter::HandleMovementModeChanged(const FName& PreviousMovementModeName, const FName& NewMovementModeName)
{
}

void ATDCombatCharacter::ApplyMovementSettings()
{
	UCommonLegacyMovementSettings* MovementSettings = MoverComponent ? MoverComponent->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>() : nullptr;
	if (!MovementSettings)
	{
		return;
	}
	MovementSettings->MaxSpeed = DefaultMaxMoveSpeed;
	MovementSettings->Acceleration = MoveAcceleration;
	MovementSettings->Deceleration = MoveDeceleration;
	MovementSettings->TurningRate = TurningRate;
}

void ATDCombatCharacter::LinkAnimationBeforeMovement()
{
	UMoverStandaloneLiaisonComponent* MovementBackend = FindComponentByClass<UMoverStandaloneLiaisonComponent>();
	FTickFunction* SimulateMovementTick = MovementBackend ? MovementBackend->FindTickFunction(EMoverTickPhase::SimulateMovement) : nullptr;
	if (!SimulateMovementTick || !UAFComponent || !UAFComponent->GetSystemReference().IsValid())
	{
		return;
	}
	UAFComponent->AddSubsequent(MovementBackend, *SimulateMovementTick, TDCombatCharacterMovement::GetAnimationPrePhysicsEventName());
	if (CharacterAnimation)
	{
		UAFComponent->AddComponentPrerequisite(CharacterAnimation, TDCombatCharacterMovement::GetAnimationPrePhysicsEventName());
	}
}

FVector ATDCombatCharacter::ConsumeMoveIntent()
{
	return Internal_ConsumeMovementInputVector().GetClampedToMaxSize(1.f);
}

bool ATDCombatCharacter::IsMoveInputBlockedByStun() const
{
	return bBlockMoveInputWhileStunned && CombatComponent && CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Stunned);
}

bool ATDCombatCharacter::IsCapsuleModifierActive() const
{
	const UTDCapsuleModifierComponent* CapsuleModifier = FindComponentByClass<UTDCapsuleModifierComponent>();
	return CapsuleModifier && (CapsuleModifier->IsJumpModifierEnabled() || CapsuleModifier->IsRollModifierEnabled());
}

bool ATDCombatCharacter::IsKnockbackSuppressed() const
{
	if (IsAirborne())
	{
		return true;
	}
	return CombatComponent && CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Rolling);
}

namespace TDDeathLaunchApplication
{
	static_assert(TDCombatCharacterMovement::RagdollLinearDamping == TDDeathLaunchRules::LaunchLinearDamping);
	static_assert(TDCombatCharacterMovement::RagdollAngularDamping == TDDeathLaunchRules::LaunchAngularDamping);

	constexpr float CcdMinPlanarSpeed = 400.f;
	constexpr int32 CcdBodyCount = 3;
	constexpr float GroundTraceDistance = 1000.f;

	FTimerManagerTimerParameters MakeEveryFrameTimerParameters()
	{
		FTimerManagerTimerParameters Parameters;
		Parameters.bLoop = true;
		Parameters.bMaxOncePerFrame = true;
		Parameters.FirstDelay = 0.f;
		return Parameters;
	}

	FBodyInstance* FindValidBody(const USkeletalMeshComponent& MeshComponent, const int32 BodyIndex)
	{
		FBodyInstance* Body = MeshComponent.Bodies.IsValidIndex(BodyIndex) ? MeshComponent.Bodies[BodyIndex] : nullptr;
		return Body && Body->IsValidBodyInstance() ? Body : nullptr;
	}
}

FTDDeathLaunchInput ATDCombatCharacter::MakeDeathLaunchInput(const FTDDamageResult& Result) const
{
	FTDDeathLaunchInput Input;
	Input.Delivery = Result.Delivery;
	Input.HitDirection = Result.HitDirection;
	Input.SwingDirection = Result.SwingDirection;
	Input.ActorForward = GetActorForwardVector();
	Input.DamageRatio = Result.DamageRatio;
	Input.UnclampedDamage = Result.UnclampedDamage;
	Input.MaxHealth = CombatComponent ? CombatComponent->GetMaxHealth() : 0.f;
	Input.CapsuleHalfHeight = CapsuleComponent ? CapsuleComponent->GetScaledCapsuleHalfHeight() : Input.CapsuleHalfHeight;
	Input.VariationSeed = GetUniqueID();
	Input.bWasCritical = Result.bWasCritical;
	return Input;
}

FTDDeathLaunch ATDCombatCharacter::MakeDeathLaunchFromMassCenter(const FTDDeathLaunchInput& Input, const FVector& MassCenter) const
{
	FTDDeathLaunchInput MeasuredInput = Input;
	const FVector ActorLocation = GetActorLocation();
	float GroundHeight = 0.f;
	MeasuredInput.bHasGroundHeight = TraceDeathGroundHeight(MassCenter, GroundHeight);
	MeasuredInput.GroundHeight = GroundHeight;
	MeasuredInput.ActorHeight = static_cast<float>(ActorLocation.Z);
	MeasuredInput.CenterOfMassHeight = static_cast<float>(MassCenter.Z);
	return TDDeathLaunchRules::ComputeDeathLaunch(MeasuredInput);
}

bool ATDCombatCharacter::TraceDeathGroundHeight(const FVector& From, float& OutGroundHeight) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDDeathLaunchGround), false, this);
	FCollisionResponseParams ResponseParams;
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	ResponseParams.CollisionResponse.SetResponse(ECC_PhysicsBody, ECR_Ignore);
	FHitResult GroundHit;
	const FVector TraceEnd = From - FVector(0.f, 0.f, TDDeathLaunchApplication::GroundTraceDistance);
	if (!World->LineTraceSingleByChannel(GroundHit, From, TraceEnd, ECC_WorldStatic, QueryParams, ResponseParams))
	{
		return false;
	}
	OutGroundHeight = static_cast<float>(GroundHit.ImpactPoint.Z);
	return true;
}

void ATDCombatCharacter::ScheduleRagdollEntry(const float HoldRealSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	ScheduledRagdollEntryRealSeconds = World->GetRealTimeSeconds() + FMath::Max(HoldRealSeconds, 0.f);
	bHasScheduledRagdollEntry = true;
	if (UTDCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UTDCombatFeedbackSubsystem>())
	{
		Feedback->NotifyRagdollEntryScheduled(*this);
	}
}

void ATDCombatCharacter::CancelScheduledRagdollEntry()
{
	bHasScheduledRagdollEntry = false;
	UWorld* World = GetWorld();
	UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
	if (!Feedback)
	{
		return;
	}
	Feedback->CancelRagdollEntryWait(*this);
}

bool ATDCombatCharacter::RequestDeathRagdoll(const FTDDeathLaunchInput& Input)
{
	if (IsRagdollPendingOrActive())
	{
		return true;
	}
	const USkeletalMeshComponent* MeshComponent = Mesh.Get();
	if (!MeshComponent || !MeshComponent->GetPhysicsAsset())
	{
		CancelScheduledRagdollEntry();
		return false;
	}
	UWorld* World = GetWorld();
	PendingDeathLaunchInput = Input;
	PendingDeathRagdollStartRealSeconds = World ? World->GetRealTimeSeconds() : 0.0;
	if (!World || !ShouldDeferDeathRagdoll())
	{
		return StartRagdoll(Input);
	}
	bIsRagdollPending = true;
	if (CharacterAnimation)
	{
		CharacterAnimation->HoldPoseForDeath();
	}
	World->GetTimerManager().SetTimer(PendingDeathRagdollTimerHandle, this, &ATDCombatCharacter::RetryPendingDeathRagdoll, UE_SMALL_NUMBER, TDDeathLaunchApplication::MakeEveryFrameTimerParameters());
	return true;
}

bool ATDCombatCharacter::ShouldDeferDeathRagdoll() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FTDRagdollDeferInput DeferInput;
	DeferInput.NowRealSeconds = World->GetRealTimeSeconds();
	DeferInput.ScheduledEntryRealSeconds = bHasScheduledRagdollEntry ? ScheduledRagdollEntryRealSeconds : DeferInput.NowRealSeconds;
	DeferInput.DeferredRealSeconds = static_cast<float>(DeferInput.NowRealSeconds - PendingDeathRagdollStartRealSeconds);
	const AWorldSettings* WorldSettings = World->GetWorldSettings(false, false);
	DeferInput.GlobalTimeDilation = WorldSettings ? WorldSettings->GetEffectiveTimeDilation() : 1.f;
	if (const UTDCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UTDCombatFeedbackSubsystem>())
	{
		DeferInput.bIsSlowMotionActive = Feedback->IsSlowMotionActive();
		const float SecondsUntilSlowMotion = Feedback->GetSecondsUntilPendingSlowMotion();
		if (SecondsUntilSlowMotion < TNumericLimits<float>::Max())
		{
			DeferInput.NextSlowMotionStartRealSeconds = DeferInput.NowRealSeconds + SecondsUntilSlowMotion;
		}
	}
	return TDDeathLaunchRules::ShouldDeferRagdollStart(DeferInput);
}

void ATDCombatCharacter::RetryPendingDeathRagdoll()
{
	if (!bIsRagdollPending)
	{
		ClearPendingDeathRagdoll();
		return;
	}
	if (ShouldDeferDeathRagdoll())
	{
		return;
	}
	StartPendingDeathRagdollNow(false);
}

void ATDCombatCharacter::StartPendingDeathRagdollNow(const bool bStartsAtRest)
{
	if (!bIsRagdollPending)
	{
		return;
	}
	ClearPendingDeathRagdoll();
	PendingDeathLaunchInput.bStartsAtRest = PendingDeathLaunchInput.bStartsAtRest || bStartsAtRest;
	if (StartRagdoll(PendingDeathLaunchInput))
	{
		return;
	}
	HandleDeathRagdollFailed();
}

void ATDCombatCharacter::ClearPendingDeathRagdoll()
{
	bIsRagdollPending = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingDeathRagdollTimerHandle);
	}
}

void ATDCombatCharacter::HandleDeathRagdollFailed()
{
}

void ATDCombatCharacter::ApplyDeathLaunch(USkeletalMeshComponent& MeshComponent, const FTDDeathLaunchInput& Input)
{
	StopDeathLaunchWatch();
	CacheDeathLaunchBodies(MeshComponent);
	CacheDeathRollBodies(MeshComponent, Input);
	FVector MassCenter = FVector::ZeroVector;
	FVector MassVelocity = FVector::ZeroVector;
	if (!MeasureDeathLaunchMassCenter(MeshComponent, MassCenter, MassVelocity))
	{
		return;
	}
	const FTDDeathLaunch Launch = MakeDeathLaunchFromMassCenter(Input, MassCenter);
	for (const FTDDeathLaunchBody& LaunchBody : DeathLaunchBodies)
	{
		FBodyInstance* Body = TDDeathLaunchApplication::FindValidBody(MeshComponent, LaunchBody.BodyIndex);
		if (!Body)
		{
			continue;
		}
		Body->SetLinearVelocity(Launch.LinearVelocity + (Launch.AngularVelocityRadians ^ (Body->GetCOMPosition() - MassCenter)), false);
		Body->SetAngularVelocityInRadians(Launch.AngularVelocityRadians, false);
	}
	ApplyCorpsePhysicalMaterial(MeshComponent);
	UWorld* World = GetWorld();
	if (!Launch.bWatchesLanding || !World)
	{
		return;
	}
	if (Launch.LinearVelocity.Size2D() >= TDDeathLaunchApplication::CcdMinPlanarSpeed)
	{
		EnableDeathLaunchCcd(MeshComponent);
	}
	ActiveDeathLaunch = Launch;
	DeathLaunchStartSeconds = World->GetTimeSeconds();
	DeathLaunchMinVerticalSpeed = TNumericLimits<float>::Max();
	AddDeathLaunchGravity(MeshComponent);
	World->GetTimerManager().SetTimer(DeathLaunchWatchTimerHandle, this, &ATDCombatCharacter::UpdateDeathLaunchWatch, UE_SMALL_NUMBER, TDDeathLaunchApplication::MakeEveryFrameTimerParameters());
}

void ATDCombatCharacter::CacheDeathLaunchBodies(const USkeletalMeshComponent& MeshComponent)
{
	DeathLaunchBodies.Reset();
	for (int32 BodyIndex = 0; BodyIndex < MeshComponent.Bodies.Num(); ++BodyIndex)
	{
		const FBodyInstance* Body = TDDeathLaunchApplication::FindValidBody(MeshComponent, BodyIndex);
		const float Mass = Body ? Body->GetBodyMass() : 0.f;
		if (Mass > 0.f)
		{
			DeathLaunchBodies.Add(FTDDeathLaunchBody{ BodyIndex, Mass });
		}
	}
}

bool ATDCombatCharacter::MeasureDeathLaunchMassCenter(const USkeletalMeshComponent& MeshComponent, FVector& OutLocation, FVector& OutVelocity) const
{
	FVector WeightedLocation = FVector::ZeroVector;
	FVector Momentum = FVector::ZeroVector;
	float TotalMass = 0.f;
	for (const FTDDeathLaunchBody& LaunchBody : DeathLaunchBodies)
	{
		const FBodyInstance* Body = TDDeathLaunchApplication::FindValidBody(MeshComponent, LaunchBody.BodyIndex);
		if (!Body)
		{
			continue;
		}
		WeightedLocation += Body->GetCOMPosition() * LaunchBody.Mass;
		Momentum += Body->GetUnrealWorldVelocity() * LaunchBody.Mass;
		TotalMass += LaunchBody.Mass;
	}
	if (TotalMass <= 0.f)
	{
		return false;
	}
	OutLocation = WeightedLocation / TotalMass;
	OutVelocity = Momentum / TotalMass;
	return true;
}

void ATDCombatCharacter::ApplyCorpsePhysicalMaterial(USkeletalMeshComponent& MeshComponent)
{
	UWorld* World = GetWorld();
	UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
	UPhysicalMaterial* CorpseMaterial = Feedback ? Feedback->GetCorpsePhysicalMaterial() : nullptr;
	if (!CorpseMaterial)
	{
		return;
	}
	MeshComponent.SetPhysMaterialOverride(CorpseMaterial);
}

void ATDCombatCharacter::EnableDeathLaunchCcd(USkeletalMeshComponent& MeshComponent)
{
	TArray<FTDDeathLaunchBody> HeaviestBodies = DeathLaunchBodies;
	HeaviestBodies.Sort([](const FTDDeathLaunchBody& Left, const FTDDeathLaunchBody& Right)
	{
		return Left.Mass > Right.Mass;
	});
	const int32 BodyCount = FMath::Min(HeaviestBodies.Num(), TDDeathLaunchApplication::CcdBodyCount);
	for (int32 RankIndex = 0; RankIndex < BodyCount; ++RankIndex)
	{
		FBodyInstance* Body = TDDeathLaunchApplication::FindValidBody(MeshComponent, HeaviestBodies[RankIndex].BodyIndex);
		if (!Body || Body->bUseCCD)
		{
			continue;
		}
		Body->SetUseCCD(true);
		DeathLaunchCcdBodyIndices.Add(HeaviestBodies[RankIndex].BodyIndex);
	}
}

void ATDCombatCharacter::DisableDeathLaunchCcd()
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	for (const int32 BodyIndex : DeathLaunchCcdBodyIndices)
	{
		FBodyInstance* Body = MeshComponent ? TDDeathLaunchApplication::FindValidBody(*MeshComponent, BodyIndex) : nullptr;
		if (Body)
		{
			Body->SetUseCCD(false);
		}
	}
	DeathLaunchCcdBodyIndices.Reset();
}

void ATDCombatCharacter::AddDeathLaunchGravity(USkeletalMeshComponent& MeshComponent)
{
	if (bIsRagdollSettling || DeathLaunchGravityFrame == GFrameCounter || !MeshComponent.IsAnyRigidBodyAwake())
	{
		return;
	}
	DeathLaunchGravityFrame = GFrameCounter;
	MeshComponent.AddForceToAllBodiesBelow(ActiveDeathLaunch.ExtraGravity, NAME_None, true, true);
}

void ATDCombatCharacter::UpdateDeathLaunchWatch()
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	const UWorld* World = GetWorld();
	FVector MassCenter = FVector::ZeroVector;
	FVector MassVelocity = FVector::ZeroVector;
	if (!bIsRagdolling || !MeshComponent || !World || !MeasureDeathLaunchMassCenter(*MeshComponent, MassCenter, MassVelocity))
	{
		StopDeathLaunchWatch();
		return;
	}
	DeathLaunchMinVerticalSpeed = FMath::Min(DeathLaunchMinVerticalSpeed, static_cast<float>(MassVelocity.Z));
	float TracedGroundHeight = 0.f;
	ActiveDeathLaunch.GroundHeight = TraceDeathGroundHeight(MassCenter, TracedGroundHeight) ? TracedGroundHeight : static_cast<float>(MassCenter.Z) - TDDeathLaunchApplication::GroundTraceDistance;
	FTDDeathLandingSample Sample;
	Sample.VerticalSpeed = static_cast<float>(MassVelocity.Z);
	Sample.MinVerticalSpeed = DeathLaunchMinVerticalSpeed;
	Sample.CenterOfMassHeight = static_cast<float>(MassCenter.Z);
	Sample.ElapsedSeconds = static_cast<float>(World->GetTimeSeconds() - DeathLaunchStartSeconds);
	switch (TDDeathLaunchRules::ResolveDeathLandingState(ActiveDeathLaunch, Sample))
	{
	case ETDDeathLandingState::Landed:
		LandDeathLaunch(*MeshComponent, MassCenter, Sample);
		return;
	case ETDDeathLandingState::Abandoned:
		StopDeathLaunchWatch();
		return;
	case ETDDeathLandingState::Overdue:
		DisableDeathLaunchCcd();
		return;
	default:
		AddDeathLaunchGravity(*MeshComponent);
	}
}

void ATDCombatCharacter::LandDeathLaunch(USkeletalMeshComponent& MeshComponent, const FVector& MassCenter, const FTDDeathLandingSample& Sample)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeathLaunchWatchTimerHandle);
	}
	DisableDeathLaunchCcd();
	PlayCorpseLanding(MassCenter, Sample);
	StartDeathLaunchBite(MeshComponent);
	StartLandingRollLimit();
}

void ATDCombatCharacter::PlayCorpseLanding(const FVector& MassCenter, const FTDDeathLandingSample& Sample)
{
	UWorld* World = GetWorld();
	UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
	if (!Feedback)
	{
		return;
	}
	const float HalfHeight = CapsuleComponent ? CapsuleComponent->GetScaledCapsuleHalfHeight() : 0.f;
	const FTDCorpseLandSound Sound = TDDeathLaunchRules::ComputeCorpseLandSound(ActiveDeathLaunch, Sample, HalfHeight);
	Feedback->PlayCorpseLanding(MassCenter, Sound.VolumeScale, Sound.PitchScale, DeathLaunchKillRank, HalfHeight);
}

void ATDCombatCharacter::StartDeathLaunchBite(USkeletalMeshComponent& MeshComponent)
{
	UWorld* World = GetWorld();
	if (bIsRagdollSettling || !World)
	{
		return;
	}
	World->GetTimerManager().SetTimer(DeathLaunchBiteTimerHandle, this, &ATDCombatCharacter::EndDeathLaunchBite, TDDeathLaunchRules::BiteSeconds, false);
	ApplyRagdollDamping(MeshComponent);
}

void ATDCombatCharacter::EndDeathLaunchBite()
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	UWorld* World = GetWorld();
	if (!bIsRagdolling || !MeshComponent || !World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(DeathLaunchBiteTimerHandle);
	ApplyRagdollDamping(*MeshComponent);
}

void ATDCombatCharacter::CacheDeathRollBodies(const USkeletalMeshComponent& MeshComponent, const FTDDeathLaunchInput& Input)
{
	DeathRollTorsoBodies.Reset();
	DeathRollAxisBodyIndex = INDEX_NONE;
	float TotalBodyMass = 0.f;
	float TorsoMass = 0.f;
	float AxisBodyMass = 0.f;
	for (const FTDDeathLaunchBody& LaunchBody : DeathLaunchBodies)
	{
		TotalBodyMass += LaunchBody.Mass;
		const FBodyInstance* Body = TDDeathLaunchApplication::FindValidBody(MeshComponent, LaunchBody.BodyIndex);
		const UBodySetup* BodySetup = Body ? Body->GetBodySetup() : nullptr;
		if (!BodySetup || !TDDeathLaunchRules::IsLandingRollTorsoBone(BodySetup->BoneName))
		{
			continue;
		}
		DeathRollTorsoBodies.Add(LaunchBody);
		TorsoMass += LaunchBody.Mass;
		if (LaunchBody.Mass > AxisBodyMass)
		{
			AxisBodyMass = LaunchBody.Mass;
			DeathRollAxisBodyIndex = LaunchBody.BodyIndex;
		}
	}
	const float TorsoMassRatio = TotalBodyMass > 0.f ? TorsoMass / TotalBodyMass : 0.f;
	const FBodyInstance* AxisBody = TDDeathLaunchApplication::FindValidBody(MeshComponent, DeathRollAxisBodyIndex);
	if (!AxisBody || !TDDeathLaunchRules::ShouldLimitLandingRoll(Input.bHasHorizontalBodyPlan, Input.CapsuleHalfHeight, TorsoMassRatio))
	{
		DeathRollTorsoBodies.Reset();
		DeathRollAxisBodyIndex = INDEX_NONE;
		return;
	}
	const FVector Forward = Input.ActorForward.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
	DeathRollAxisInAxisBody = AxisBody->GetUnrealWorldTransform().InverseTransformVectorNoScale(Forward);
}

void ATDCombatCharacter::StartLandingRollLimit()
{
	UWorld* World = GetWorld();
	if (!World || DeathRollTorsoBodies.IsEmpty() || !World->GetTimerManager().IsTimerActive(DeathLaunchBiteTimerHandle))
	{
		return;
	}
	LimitLandingRoll();
	World->GetTimerManager().SetTimer(DeathLaunchWatchTimerHandle, this, &ATDCombatCharacter::LimitLandingRoll, UE_SMALL_NUMBER, TDDeathLaunchApplication::MakeEveryFrameTimerParameters());
}

void ATDCombatCharacter::LimitLandingRoll()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	const FBodyInstance* AxisBody = MeshComponent ? TDDeathLaunchApplication::FindValidBody(*MeshComponent, DeathRollAxisBodyIndex) : nullptr;
	if (!bIsRagdolling || !AxisBody || !World->GetTimerManager().IsTimerActive(DeathLaunchBiteTimerHandle))
	{
		World->GetTimerManager().ClearTimer(DeathLaunchWatchTimerHandle);
		return;
	}
	const FVector RollAxis = AxisBody->GetUnrealWorldTransform().TransformVectorNoScale(DeathRollAxisInAxisBody).GetSafeNormal();
	const float ExcessRollSpeed = TDDeathLaunchRules::ComputeLandingRollSpeedExcess(MeasureTorsoRollRadiansPerSecond(*MeshComponent, RollAxis));
	FVector MassCenter = FVector::ZeroVector;
	FVector MassVelocity = FVector::ZeroVector;
	if (FMath::IsNearlyZero(ExcessRollSpeed) || !MeasureDeathLaunchMassCenter(*MeshComponent, MassCenter, MassVelocity))
	{
		return;
	}
	const FVector ExcessSpin = RollAxis * ExcessRollSpeed;
	for (const FTDDeathLaunchBody& LaunchBody : DeathLaunchBodies)
	{
		FBodyInstance* Body = TDDeathLaunchApplication::FindValidBody(*MeshComponent, LaunchBody.BodyIndex);
		if (!Body)
		{
			continue;
		}
		Body->SetAngularVelocityInRadians(-ExcessSpin, true);
		Body->SetLinearVelocity(-(ExcessSpin ^ (Body->GetCOMPosition() - MassCenter)), true);
	}
}

float ATDCombatCharacter::MeasureTorsoRollRadiansPerSecond(const USkeletalMeshComponent& MeshComponent, const FVector& RollAxis) const
{
	float WeightedRoll = 0.f;
	float TorsoMass = 0.f;
	for (const FTDDeathLaunchBody& TorsoBody : DeathRollTorsoBodies)
	{
		const FBodyInstance* Body = TDDeathLaunchApplication::FindValidBody(MeshComponent, TorsoBody.BodyIndex);
		if (!Body)
		{
			continue;
		}
		WeightedRoll += TorsoBody.Mass * static_cast<float>(Body->GetUnrealWorldAngularVelocityInRadians() | RollAxis);
		TorsoMass += TorsoBody.Mass;
	}
	return TorsoMass > 0.f ? WeightedRoll / TorsoMass : 0.f;
}

void ATDCombatCharacter::ApplyRagdollDamping(USkeletalMeshComponent& MeshComponent) const
{
	float StageDamping = bIsRagdollSettling ? TDCombatCharacterMovement::RagdollSettleDamping : 0.f;
	if (bIsRagdollFirm)
	{
		StageDamping = FMath::Max(StageDamping, TDCombatCharacterMovement::RagdollFirmDamping);
	}
	float LinearDamping = FMath::Max(TDCombatCharacterMovement::RagdollLinearDamping, StageDamping);
	float AngularDamping = FMath::Max(TDCombatCharacterMovement::RagdollAngularDamping, StageDamping);
	const UWorld* World = GetWorld();
	if (World && World->GetTimerManager().IsTimerActive(DeathLaunchBiteTimerHandle))
	{
		LinearDamping = FMath::Max(LinearDamping, TDDeathLaunchRules::BiteLinearDamping);
		AngularDamping = FMath::Max(AngularDamping, TDDeathLaunchRules::ComputeBiteAngularDamping(bHasHorizontalDeathBodyPlan));
	}
	TDCombatCharacterMovement::SetRagdollDamping(MeshComponent, LinearDamping, AngularDamping);
}

bool ATDCombatCharacter::HasActiveDeathLaunchTimersForTest() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FTimerManager& TimerManager = World->GetTimerManager();
	return TimerManager.IsTimerActive(PendingDeathRagdollTimerHandle) || TimerManager.IsTimerActive(DeathLaunchWatchTimerHandle) || TimerManager.IsTimerActive(DeathLaunchBiteTimerHandle);
}

int64 ATDCombatCharacter::GetMeshBoneRevisionForTest() const
{
	return Mesh ? static_cast<int64>(Mesh->GetBoneTransformRevisionNumber()) : INDEX_NONE;
}

void ATDCombatCharacter::StopDeathLaunchWatch()
{
	DisableDeathLaunchCcd();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(DeathLaunchWatchTimerHandle);
	World->GetTimerManager().ClearTimer(DeathLaunchBiteTimerHandle);
}
