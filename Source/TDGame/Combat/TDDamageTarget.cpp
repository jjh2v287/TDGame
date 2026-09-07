#include "Combat/TDDamageTarget.h"
#include "Combat/TDCombatComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

ATDDamageTarget::ATDDamageTarget()
{
	PrimaryActorTick.bCanEverTick = true;
	Collision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Collision"));
	Collision->InitCapsuleSize(45.f, 65.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_Pawn);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	SetRootComponent(Collision);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Collision);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.3f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Body->SetStaticMesh(Cube.Object);
	}

	HealthLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HealthLabel"));
	HealthLabel->SetupAttachment(Collision);
	HealthLabel->SetRelativeLocation(FVector(0.f, 0.f, 105.f));
	HealthLabel->SetHorizontalAlignment(EHTA_Center);
	HealthLabel->SetWorldSize(24.f);
	HealthLabel->SetTextRenderColor(FColor::Green);

	CombatComponent = CreateDefaultSubobject<UTDCombatComponent>(TEXT("CombatComponent"));
	FTDCombatStats TargetStats;
	TargetStats.TeamId = 2;
	TargetStats.BaseMaxHealth = 300.f;
	CombatComponent->SetStats(TargetStats);
}

UAbilitySystemComponent* ATDDamageTarget::GetAbilitySystemComponent() const
{
	return CombatComponent;
}

void ATDDamageTarget::BeginPlay()
{
	Super::BeginPlay();
	CombatComponent->OnDamaged.AddUObject(this, &ATDDamageTarget::HandleDamage);
	CombatComponent->OnDeath.AddUObject(this, &ATDDamageTarget::HandleDeath);
	CombatComponent->OnFreezeChanged.AddUObject(this, &ATDDamageTarget::HandleFreezeChanged);
	UpdateHealthLabel();
}

void ATDDamageTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		if (Controller->PlayerCameraManager)
		{
			HealthLabel->SetWorldRotation((Controller->PlayerCameraManager->GetCameraLocation() - HealthLabel->GetComponentLocation()).Rotation());
		}
	}
}

void ATDDamageTarget::UpdateHealthLabel()
{
	const TCHAR* State = CombatComponent->IsAlive() ? (CombatComponent->IsFrozen() ? TEXT(" FROZEN") : TEXT("")) : TEXT(" DEFEATED");
	HealthLabel->SetText(FText::FromString(FString::Printf(TEXT("Lv.%d  %.0f / %.0f%s"), CombatComponent->GetStats().Level, CombatComponent->GetCurrentHealth(), CombatComponent->GetStats().GetMaxHealth(), State)));
	HealthLabel->SetTextRenderColor(!CombatComponent->IsAlive() ? FColor::Red : (CombatComponent->IsFrozen() ? FColor::Cyan : FColor::Green));
}

void ATDDamageTarget::HandleDamage(const FTDDamageResult& Result, const FTDDamageContext& Context)
{
	UpdateHealthLabel();
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 160.f), FString::Printf(TEXT("-%.1f%s"), Result.AppliedDamage, Result.bWasCritical ? TEXT(" CRIT") : TEXT("")), nullptr, FColor::Orange, 0.5f, true);
}

void ATDDamageTarget::HandleDeath(const FTDDamageContext& Context)
{
	UpdateHealthLabel();
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetVisibility(false);
	SetLifeSpan(2.f);
}

void ATDDamageTarget::HandleFreezeChanged(bool bIsFrozen)
{
	UpdateHealthLabel();
}
