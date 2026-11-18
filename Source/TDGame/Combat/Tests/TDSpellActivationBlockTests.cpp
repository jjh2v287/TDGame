#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimationAsset.h"
#include "Characters/TDGameCharacter.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/TDGameplayTags.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace TDSpellBlockSupport
{
	constexpr float FrameSeconds = 1.f / 60.f;
	constexpr float FloorHalfThickness = 50.f;
	constexpr float PlayerSpawnHeight = 98.f;

	struct FTDSpellBlockWorld
	{
		FTDSpellBlockWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDSpellBlockTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			AActor* FloorActor = World->SpawnActor<AActor>();
			UBoxComponent* Floor = NewObject<UBoxComponent>(FloorActor);
			FloorActor->AddInstanceComponent(Floor);
			FloorActor->SetRootComponent(Floor);
			Floor->SetBoxExtent(FVector(5000.f, 5000.f, FloorHalfThickness));
			Floor->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Floor->RegisterComponent();
			FloorActor->SetActorLocation(FVector(0.f, 0.f, -FloorHalfThickness));
		}

		~FTDSpellBlockWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ATDGameCharacter* SpawnPlayer()
		{
			FString GameModePath;
			GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), GameModePath, GEngineIni);
			const UClass* GameModeClass = LoadClass<AGameModeBase>(nullptr, *GameModePath);
			const AGameModeBase* GameModeDefaults = GameModeClass ? GameModeClass->GetDefaultObject<AGameModeBase>() : nullptr;
			UClass* PlayerPawnClass = GameModeDefaults ? GameModeDefaults->DefaultPawnClass.Get() : nullptr;
			if (!PlayerPawnClass || !PlayerPawnClass->IsChildOf(ATDGameCharacter::StaticClass()))
			{
				return nullptr;
			}
			const UObject* PlayerPawnDefaults = PlayerPawnClass->GetDefaultObject();
			for (TFieldIterator<FSoftObjectProperty> PropertyIt(PlayerPawnClass); PropertyIt; ++PropertyIt)
			{
				if (PropertyIt->PropertyClass->IsChildOf(UAnimationAsset::StaticClass()))
				{
					PropertyIt->GetPropertyValue_InContainer(PlayerPawnDefaults).LoadSynchronous();
				}
			}
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, PlayerSpawnHeight));
			return World->SpawnActor<ATDGameCharacter>(PlayerPawnClass, SpawnTransform);
		}

		AActor* SpawnTarget(const FVector& Location)
		{
			AActor* Target = World->SpawnActor<AActor>();
			UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(Target);
			Target->AddInstanceComponent(Capsule);
			Target->SetRootComponent(Capsule);
			Capsule->InitCapsuleSize(40.f, 90.f);
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Capsule->RegisterComponent();
			Target->SetActorLocation(Location);
			return Target;
		}

		void Tick(const float Duration)
		{
			for (float Remaining = Duration; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, FrameSeconds);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		UWorld* World = nullptr;
	};

	void IgnoreEngineInjectionTimelineEnsure(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("TimelineTrait.GetState(Context, ChildState)"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("=== Handled ensure: ==="), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Stack:"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("[Callstack]"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Exact, -1, false);
	}

	bool TryCastSpell(UTDCombatComponent& Combat, UObject& Outer)
	{
		UTDDamageDefinition* Spell = NewObject<UTDDamageDefinition>(&Outer);
		Spell->Lifetime = 0.05f;
		return Combat.TryCastDamageDefinition(Spell, FVector(200.f, 0.f, 0.f));
	}

	bool BeginPrimaryAttack(FTDSpellBlockWorld& Fixture, ATDGameCharacter& Player)
	{
		const UTDSkillComponent* SkillComponent = Player.GetSkillComponent();
		const FGameplayTag ComboTags[] = { TDGameplayTags::Action_Attack_Primary_01, TDGameplayTags::Action_Attack_Primary_02, TDGameplayTags::Action_Attack_Primary_03, TDGameplayTags::Action_Attack_Primary_04 };
		for (const FGameplayTag& ComboTag : ComboTags)
		{
			const FTDCombatActionDefinition* Definition = SkillComponent ? SkillComponent->FindActionDefinition(ComboTag) : nullptr;
			if (!Definition || !Definition->Action.Animation.LoadSynchronous())
			{
				return false;
			}
		}
		AActor* Target = Fixture.SpawnTarget(Player.GetActorLocation() + FVector(150.f, 0.f, 0.f));
		if (!Player.ExecutePrimaryAttack(Target))
		{
			return false;
		}
		Fixture.Tick(0.15f);
		const UTDCombatComponent* Combat = Player.GetCombatComponent();
		return Combat && Combat->HasMatchingGameplayTag(TDGameplayTags::State_Attacking);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSpellCastIdlePlayerTest, "TDGame.Combat.GAS.SpellCast.IdlePlayerCasts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDSpellCastIdlePlayerTest::RunTest(const FString& Parameters)
{
	using namespace TDSpellBlockSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	FTDSpellBlockWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player spawns"), Player) || !TestNotNull(TEXT("Player has a skill component"), Player->GetSkillComponent()))
	{
		return false;
	}
	Fixture.Tick(0.3f);
	TestTrue(TEXT("Idle player can cast"), TryCastSpell(*Player->GetCombatComponent(), *Fixture.World));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSpellCastRollingPlayerTest, "TDGame.Combat.GAS.SpellCast.RollingPlayerBlocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDSpellCastRollingPlayerTest::RunTest(const FString& Parameters)
{
	using namespace TDSpellBlockSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	FTDSpellBlockWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player spawns"), Player) || !TestNotNull(TEXT("Player has a skill component"), Player->GetSkillComponent()))
	{
		return false;
	}
	Fixture.Tick(0.3f);
	UTDCombatComponent* Combat = Player->GetCombatComponent();
	if (!TestTrue(TEXT("The roll starts"), Player->TryStartRoll(FVector::ForwardVector)) || !TestTrue(TEXT("The player is rolling"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Rolling)))
	{
		return false;
	}
	TestFalse(TEXT("Rolling blocks spell casting"), TryCastSpell(*Combat, *Fixture.World));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSpellCastAttackClosedWindowTest, "TDGame.Combat.GAS.SpellCast.AttackingClosedWindowBlocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDSpellCastAttackClosedWindowTest::RunTest(const FString& Parameters)
{
	using namespace TDSpellBlockSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	FTDSpellBlockWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	UTDSkillComponent* SkillComponent = Player ? Player->GetSkillComponent() : nullptr;
	if (!TestNotNull(TEXT("Player has a skill component"), SkillComponent))
	{
		return false;
	}
	Fixture.Tick(0.3f);
	if (!TestTrue(TEXT("The attack is running"), BeginPrimaryAttack(Fixture, *Player)))
	{
		return false;
	}
	if (!TestFalse(TEXT("The skill cancel window is closed"), SkillComponent->IsActionCancelWindowOpen(ETDActionCancelType::Skill)))
	{
		return false;
	}
	TestFalse(TEXT("Attacking with the skill cancel window closed blocks spell casting"), TryCastSpell(*Player->GetCombatComponent(), *Fixture.World));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSpellCastAttackOpenWindowTest, "TDGame.Combat.GAS.SpellCast.AttackingOpenWindowCasts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDSpellCastAttackOpenWindowTest::RunTest(const FString& Parameters)
{
	using namespace TDSpellBlockSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	FTDSpellBlockWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	UTDSkillComponent* SkillComponent = Player ? Player->GetSkillComponent() : nullptr;
	if (!TestNotNull(TEXT("Player has a skill component"), SkillComponent))
	{
		return false;
	}
	Fixture.Tick(0.3f);
	if (!TestTrue(TEXT("The attack is running"), BeginPrimaryAttack(Fixture, *Player)))
	{
		return false;
	}
	for (int32 Frame = 0; Frame < 90 && !SkillComponent->IsActionCancelWindowOpen(ETDActionCancelType::Skill); ++Frame)
	{
		Fixture.Tick(FrameSeconds);
	}
	if (!TestTrue(TEXT("The skill cancel window opens"), SkillComponent->IsActionCancelWindowOpen(ETDActionCancelType::Skill))
		|| !TestTrue(TEXT("The attack is still running"), Player->GetCombatComponent()->HasMatchingGameplayTag(TDGameplayTags::State_Attacking)))
	{
		return false;
	}
	TestTrue(TEXT("Attacking with the skill cancel window open allows spell casting"), TryCastSpell(*Player->GetCombatComponent(), *Fixture.World));
	return true;
}

#endif
