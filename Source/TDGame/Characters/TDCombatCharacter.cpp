#include "Characters/TDCombatCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Backends/MoverStandaloneLiaison.h"
#include "Characters/TDCapsuleModifierComponent.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDKnockbackLayeredMove.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageTypes.h"
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
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "Materials/MaterialInterface.h"
#include "Module/UAFWeakSystemReference.h"
#include "MovementMode.h"
#include "PhysicsEngine/BodyInstance.h"
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
	constexpr float StatusKillUpSpeedScale = 0.3f;

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

bool ATDCombatCharacter::StartRagdoll(const FVector& LinearVelocity)
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	if (!MeshComponent || !MeshComponent->GetPhysicsAsset())
	{
		return false;
	}
	if (bIsRagdolling)
	{
		return true;
	}

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
	if (!LinearVelocity.IsNearlyZero())
	{
		MeshComponent->SetAllPhysicsLinearVelocity(LinearVelocity, false);
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
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
		TDCombatCharacterMovement::SetRagdollDamping(*MeshComponent, TDCombatCharacterMovement::RagdollSettleDamping, TDCombatCharacterMovement::RagdollSettleDamping);
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
	bIsRagdollSettling = false;
	bIsRagdollFirm = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RagdollSettleTimerHandle);
	}
}

FVector ATDCombatCharacter::ComputeKillVelocity(const FTDDamageResult& Result) const
{
	if (Result.Delivery == ETDDamageDelivery::Status)
	{
		return FVector(0.f, 0.f, RagdollUpSpeed * TDCombatCharacterMovement::StatusKillUpSpeedScale);
	}
	const float MaxHealth = CombatComponent ? CombatComponent->GetMaxHealth() : 0.f;
	const float UnclampedDamageRatio = MaxHealth > 0.f ? Result.UnclampedDamage / MaxHealth : 0.f;
	const float RatioScale = FMath::Clamp(FMath::Max(Result.DamageRatio, UnclampedDamageRatio), 0.f, 1.f);
	const float CriticalScale = Result.bWasCritical ? RagdollCriticalScale : 1.f;
	const float PlanarSpeed = (RagdollBaseSpeed + RagdollRatioSpeed * RatioScale) * CriticalScale;
	return ResolveHitPlanarDirection(Result) * PlanarSpeed + FVector(0.f, 0.f, RagdollUpSpeed);
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
