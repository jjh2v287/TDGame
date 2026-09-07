// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDGameCharacter.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDDamageDefinition.h"
#include "Combat/TDDamageExamples.h"
#include "UObject/ConstructorHelpers.h"
#include "Camera/CameraComponent.h"
#include "Components/DecalComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"

ATDGameCharacter::ATDGameCharacter()
{
	FTDCombatStats PlayerStats;
	PlayerStats.TeamId = 1;
	CombatComponent->SetStats(PlayerStats);

	// Set size for player capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate character to camera direction
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;

	// Create the camera boom component
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));

	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 800.f;
	CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
	CameraBoom->bDoCollisionTest = false;

	// Create the camera component
	TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));

	TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCameraComponent->bUsePawnControlRotation = false;

	// Activate ticking in order to update the cursor every frame.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ATDGameCharacter::BeginPlay()
{
	if (DamageSpells.IsEmpty())
	{
		const TCHAR* SpellNames[] = { TEXT("Fireball"), TEXT("Blizzard"), TEXT("Mine"), TEXT("Shockwave"), TEXT("Meteor"), TEXT("DelayedHoming") };
		for (const TCHAR* SpellName : SpellNames)
		{
			const FString AssetPath = FString::Printf(TEXT("/Game/Combat/Examples/DA_TD%s.DA_TD%s"), SpellName, SpellName);
			UTDDamageDefinition* Definition = LoadObject<UTDDamageDefinition>(nullptr, *AssetPath, nullptr, LOAD_NoWarn);
			if (!Definition)
			{
				DamageSpells.Reset();
				break;
			}
			DamageSpells.Add(Definition);
		}

		if (DamageSpells.IsEmpty())
		{
			TArray<UTDDamageDefinition*> Examples;
			TDDamageExamples::CreateExamples(this, Examples);
			for (UTDDamageDefinition* Definition : Examples)
			{
				DamageSpells.Add(Definition);
			}
		}
	}
	Super::BeginPlay();

	// stub
}

void ATDGameCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

	// stub
}
