#include "Actors/TDCaravanActor.h"
#include "Combat/GAS/TDCombatAttributeSet.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

ATDCaravanActor::ATDCaravanActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	CaravanMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CaravanMesh"));
	CaravanMesh->SetupAttachment(Root);
	CaravanMesh->SetCollisionProfileName(TEXT("Pawn"));

	CombatAttributes = CreateDefaultSubobject<UTDCombatAttributeSet>(TEXT("CombatAttributes"));
	CombatComponent = CreateDefaultSubobject<UTDCombatComponent>(TEXT("CombatComponent"));

	CombatStats.TeamId = 1;
}

void ATDCaravanActor::BeginPlay()
{
	CombatComponent->AddAttributeSetSubobject(CombatAttributes.Get());
	CombatComponent->InitAbilityActorInfo(this, this);
	Super::BeginPlay();

	CombatComponent->SetStats(CombatStats);
	CombatComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);

	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (PlayerPawn)
	{
		SetFollowTarget(PlayerPawn);
	}
}

void ATDCaravanActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateFollow(DeltaSeconds);
}

UAbilitySystemComponent* ATDCaravanActor::GetAbilitySystemComponent() const
{
	return CombatComponent;
}

FGenericTeamId ATDCaravanActor::GetGenericTeamId() const
{
	const int32 TeamId = CombatComponent ? CombatComponent->GetStats().TeamId : INDEX_NONE;
	return TeamId < 0 ? FGenericTeamId::NoTeam : FGenericTeamId(static_cast<uint8>(TeamId));
}

float ATDCaravanActor::GetCurrentHealth() const
{
	return CombatComponent ? CombatComponent->GetCurrentHealth() : 0.f;
}

bool ATDCaravanActor::IsAlive() const
{
	return CombatComponent && CombatComponent->IsAlive();
}

void ATDCaravanActor::SetFollowTarget(AActor* NewFollowTarget)
{
	FollowTarget = NewFollowTarget;
	bIsFollowingTarget = false;
	CurrentFollowVelocity = FVector::ZeroVector;
}

AActor* ATDCaravanActor::GetFollowTarget() const
{
	return FollowTarget;
}

void ATDCaravanActor::AddStoredItem(const FTDItemStack& ItemStack)
{
	if (!ItemStack.ItemTag.IsValid() || ItemStack.Quantity <= 0)
	{
		return;
	}

	StoredItems.Add(ItemStack);
}

TArray<FTDItemStack> ATDCaravanActor::ExtractStoredItems()
{
	TArray<FTDItemStack> DroppedItems = StoredItems;
	StoredItems.Reset();
	return DroppedItems;
}

void ATDCaravanActor::UpdateFollow(const float DeltaSeconds)
{
	if (!FollowTarget || !IsAlive())
	{
		bIsFollowingTarget = false;
		CurrentFollowVelocity = FVector::ZeroVector;
		return;
	}

	FVector ToTarget = FollowTarget->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;

	const float StartFollowDistance = FMath::Max(FollowDistance, StopDistance);
	const float DistanceToTargetSquared = ToTarget.SizeSquared();

	if (!bIsFollowingTarget)
	{
		if (DistanceToTargetSquared <= FMath::Square(StartFollowDistance))
		{
			return;
		}

		bIsFollowingTarget = true;
	}
	else if (DistanceToTargetSquared <= FMath::Square(StopDistance))
	{
		bIsFollowingTarget = false;
		return;
	}

	const FVector DesiredLocation = FollowTarget->GetActorLocation() - FollowTarget->GetActorForwardVector() * StopDistance;
	FVector OffsetToDesiredLocation = DesiredLocation - GetActorLocation();
	OffsetToDesiredLocation.Z = 0.f;

	const float DistanceToDesiredLocation = OffsetToDesiredLocation.Size();
	const FVector DesiredMoveDirection = DistanceToDesiredLocation > KINDA_SMALL_NUMBER
		? OffsetToDesiredLocation / DistanceToDesiredLocation
		: FVector::ZeroVector;

	const float MaxMoveSpeed = DistanceToTargetSquared > FMath::Square(CatchUpDistance) ? FollowMoveSpeed * 1.5f : FollowMoveSpeed;
	float DesiredMoveSpeed = 0.f;
	if (bIsFollowingTarget && !DesiredMoveDirection.IsNearlyZero())
	{
		const float SlowdownDistance = FMath::Max(FollowSlowdownDistance, 1.f);
		const float SlowdownAlpha = FMath::Clamp(DistanceToDesiredLocation / SlowdownDistance, 0.f, 1.f);
		DesiredMoveSpeed = MaxMoveSpeed * SlowdownAlpha;
	}

	const FVector DesiredVelocity = DesiredMoveDirection * DesiredMoveSpeed;
	const float CurrentSpeed = CurrentFollowVelocity.Size();
	const float VelocityChangeRate = DesiredMoveSpeed > CurrentSpeed ? FollowAcceleration : FollowDeceleration;
	CurrentFollowVelocity = FMath::VInterpConstantTo(CurrentFollowVelocity, DesiredVelocity, DeltaSeconds, VelocityChangeRate);
	CurrentFollowVelocity.Z = 0.f;

	FVector MoveDelta = CurrentFollowVelocity * DeltaSeconds;
	if (bIsFollowingTarget && !DesiredMoveDirection.IsNearlyZero() && MoveDelta.SizeSquared() > FMath::Square(DistanceToDesiredLocation))
	{
		MoveDelta = OffsetToDesiredLocation;
		CurrentFollowVelocity = FVector::ZeroVector;
	}

	if (MoveDelta.IsNearlyZero())
	{
		if (!bIsFollowingTarget)
		{
			CurrentFollowVelocity = FVector::ZeroVector;
		}
		return;
	}

	const FVector NewLocation = GetActorLocation() + MoveDelta;
	SetActorLocation(FVector(NewLocation.X, NewLocation.Y, GetActorLocation().Z));

	const FRotator TargetRotation = MoveDelta.Rotation();
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), TargetRotation, DeltaSeconds, FollowRotationInterpSpeed));
}

void ATDCaravanActor::HandleDeath(const FTDDamageContext& Context)
{
	SetActorEnableCollision(false);

	AActor* KillerActor = Context.Caster.Get();
	const TArray<FTDItemStack> DroppedItems = ExtractStoredItems();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(World);

	FTDActorDeathMessage DeathMessage;
	DeathMessage.DeadActor = this;
	DeathMessage.Killer = KillerActor;
	DeathMessage.DeathLocation = GetActorLocation();
	MessageSubsystem.BroadcastMessage(TDGameplayTags::Event_Actor_Death, DeathMessage);

	FTDCaravanDestroyedMessage CaravanDestroyedMessage;
	CaravanDestroyedMessage.CaravanActor = this;
	CaravanDestroyedMessage.Killer = KillerActor;
	CaravanDestroyedMessage.DestroyedLocation = GetActorLocation();
	CaravanDestroyedMessage.DroppedItems = DroppedItems;
	MessageSubsystem.BroadcastMessage(TDGameplayTags::Event_Caravan_Destroyed, CaravanDestroyedMessage);
}
