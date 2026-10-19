#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDGameCharacter.h"
#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"
#include "Combat/TDCombatComponent.h"
#include "Component/AnimNextComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace TDHitFeelReactionTestSupport
{
	constexpr float FrameSeconds = 1.f / 60.f;
	constexpr float FloorHalfThickness = 50.f;
	constexpr float PawnSpawnHeight = 98.f;
	constexpr float SettleSeconds = 0.5f;
	constexpr float ActionLeadSeconds = 0.1f;
	constexpr float HitStopRealSeconds = 0.1f;
	constexpr float RateHitStopClockScale = 0.05f;
	constexpr float ClockScaleTolerance = 0.01f;
	constexpr float RemainingSecondsTolerance = 0.001f;
	constexpr float KnockbackSpeed = 600.f;
	constexpr float KnockbackSeconds = 0.2f;
	constexpr float HeldKnockbackStopSeconds = 0.2f;
	constexpr float HeldKnockbackCheckSeconds = 0.15f;
	constexpr float HeldMovementToleranceCm = 1.f;
	constexpr float ReleasedMovementMinimumCm = 5.f;
	constexpr float LethalDamage = 1000000.f;
	constexpr int32 MaximumVictimStartFrames = 3;
	const TCHAR* const SwordAttackSequencePath = TEXT("/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordAttack01.AS_TD_Player_SwordAttack01");

	void PreloadPlayerAnimations(UClass& PlayerPawnClass)
	{
		const UObject* PlayerPawnDefaults = PlayerPawnClass.GetDefaultObject();
		for (TFieldIterator<FSoftObjectProperty> PropertyIt(&PlayerPawnClass); PropertyIt; ++PropertyIt)
		{
			if (!PropertyIt->PropertyClass->IsChildOf(UAnimationAsset::StaticClass()))
			{
				continue;
			}
			PropertyIt->GetPropertyValue_InContainer(PlayerPawnDefaults).LoadSynchronous();
		}
	}

	UClass* LoadDefaultPlayerPawnClass()
	{
		FString GameModePath;
		GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), GameModePath, GEngineIni);
		const UClass* GameModeClass = LoadClass<AGameModeBase>(nullptr, *GameModePath);
		const AGameModeBase* GameModeDefaults = GameModeClass ? GameModeClass->GetDefaultObject<AGameModeBase>() : nullptr;
		UClass* PlayerPawnClass = GameModeDefaults ? GameModeDefaults->DefaultPawnClass.Get() : nullptr;
		return PlayerPawnClass && PlayerPawnClass->IsChildOf(ATDGameCharacter::StaticClass()) ? PlayerPawnClass : nullptr;
	}

	void IgnoreEngineInjectionTimelineEnsure(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("TimelineTrait.GetState(Context, ChildState)"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("=== Handled ensure: ==="), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Stack:"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("[Callstack]"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Exact, -1, false);
	}

	struct FTDHitFeelTestWorld
	{
		FTDHitFeelTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDHitFeelReactionTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			SpawnFloor();
		}

		~FTDHitFeelTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void SpawnFloor()
		{
			AActor* Floor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
			Floor->AddInstanceComponent(Box);
			Floor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(5000.f, 5000.f, FloorHalfThickness));
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->RegisterComponent();
			Floor->SetActorLocation(FVector(0.f, 0.f, -FloorHalfThickness));
		}

		ATDGameCharacter* SpawnPlayer()
		{
			UClass* PlayerPawnClass = LoadDefaultPlayerPawnClass();
			if (!PlayerPawnClass)
			{
				return nullptr;
			}
			PreloadPlayerAnimations(*PlayerPawnClass);
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, PawnSpawnHeight));
			return World->SpawnActor<ATDGameCharacter>(PlayerPawnClass, SpawnTransform);
		}

		ATDMonsterCharacter* SpawnPlainMonster(const FVector& Location = FVector::ZeroVector)
		{
			const FTransform SpawnTransform(FRotator::ZeroRotator, Location + FVector(0.f, 0.f, PawnSpawnHeight));
			return World->SpawnActor<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
		}

		ATDMonsterCharacter* SpawnMonsterOfDefinition(const FName DefinitionId, const FVector& Location)
		{
			UTDMonsterSpeciesAsset* Species = NewObject<UTDMonsterSpeciesAsset>(World);
			Species->DefinitionId = DefinitionId;
			const FTransform SpawnTransform(FRotator::ZeroRotator, Location + FVector(0.f, 0.f, PawnSpawnHeight));
			ATDMonsterCharacter* Monster = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
			if (!Monster)
			{
				return nullptr;
			}
			Monster->SetSpecies(Species);
			Monster->FinishSpawning(SpawnTransform);
			return Monster;
		}

		void TickFrame()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, FrameSeconds);
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

	float MeasureActionClockStep(FTDHitFeelTestWorld& Fixture, const UTDCharacterAnimationComponent& Animation)
	{
		const float TimeBefore = Animation.GetActionAnimationTime();
		Fixture.TickFrame();
		return Animation.GetActionAnimationTime() - TimeBefore;
	}

	int32 TickUntilVictimHitStopStarts(FTDHitFeelTestWorld& Fixture, const UTDCharacterAnimationComponent& Animation)
	{
		int32 FrameCount = 0;
		while (Animation.IsHitStopPending() && FrameCount <= MaximumVictimStartFrames)
		{
			Fixture.TickFrame();
			++FrameCount;
		}
		return FrameCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDAttackerHitStopSlowsActionClockTest, "TDGame.Combat.HitFeel.Reaction.AttackerHitStopSlowsActionClock", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDAttackerHitStopSlowsActionClockTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	UAnimSequence* Attack = LoadObject<UAnimSequence>(nullptr, SwordAttackSequencePath);
	if (!TestNotNull(TEXT("Sword attack sequence exists"), Attack))
	{
		return false;
	}

	FTDHitFeelTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	UTDCharacterAnimationComponent* Animation = Player->GetCharacterAnimation();
	UUAFComponent* AnimationSystem = Player->GetUAFComponent();
	if (!TestNotNull(TEXT("Player has an animation driver"), Animation) || !TestNotNull(TEXT("Player has a UAF system"), AnimationSystem))
	{
		return false;
	}

	for (const bool bUseRootMotion : { false, true })
	{
		const TCHAR* const Label = bUseRootMotion ? TEXT("root motion action") : TEXT("in-place action");
		if (!TestTrue(FString::Printf(TEXT("%s starts"), Label), Animation->PlayAction(Attack, 1.f, 0.f, 0.f, bUseRootMotion) > 0.f))
		{
			return false;
		}
		Fixture.Tick(ActionLeadSeconds);
		const float NormalStep = MeasureActionClockStep(Fixture, *Animation);
		if (!TestTrue(FString::Printf(TEXT("%s clock advances"), Label), NormalStep > 0.f))
		{
			return false;
		}

		TestTrue(FString::Printf(TEXT("%s accepts the attacker hit stop"), Label), Animation->StartAttackerHitStop(HitStopRealSeconds));
		TestTrue(FString::Printf(TEXT("%s reports the attacker hit stop"), Label), Animation->IsAttackerHitStopActive());
		TestFalse(FString::Printf(TEXT("%s attacker hit stop does not hold the pose"), Label), Animation->IsHitStopped());
		TestEqual(FString::Printf(TEXT("%s attacker hit stop reports its real length"), Label), Animation->GetHitStopRemainingSeconds(), HitStopRealSeconds, RemainingSecondsTolerance);
		const float StoppedStep = MeasureActionClockStep(Fixture, *Animation);
		TestEqual(FString::Printf(TEXT("%s clock runs at 0.05x during the attacker hit stop"), Label), StoppedStep / NormalStep, RateHitStopClockScale, ClockScaleTolerance);
		TestTrue(FString::Printf(TEXT("%s keeps the UAF system running"), Label), AnimationSystem->IsActive());

		Fixture.Tick(HitStopRealSeconds);
		TestFalse(FString::Printf(TEXT("%s attacker hit stop expires"), Label), Animation->IsAttackerHitStopActive());
		const float ResumedStep = MeasureActionClockStep(Fixture, *Animation);
		TestEqual(FString::Printf(TEXT("%s clock restores the original rate"), Label), ResumedStep / NormalStep, 1.f, ClockScaleTolerance);

		Animation->StartAttackerHitStop(HitStopRealSeconds);
		Animation->PlayAction(Attack, 1.f, 0.f, 0.f, bUseRootMotion);
		TestFalse(FString::Printf(TEXT("%s restart clears the attacker hit stop"), Label), Animation->IsAttackerHitStopActive());
		Animation->StopAction();
		TestFalse(FString::Printf(TEXT("%s finish clears the attacker hit stop"), Label), Animation->IsAttackerHitStopActive());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDVictimHitStopWaitsForNextTickTest, "TDGame.Combat.HitFeel.Reaction.VictimHitStopWaitsForNextTick", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDVictimHitStopWaitsForNextTickTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	UAnimSequence* Attack = LoadObject<UAnimSequence>(nullptr, SwordAttackSequencePath);
	if (!TestNotNull(TEXT("Sword attack sequence exists"), Attack))
	{
		return false;
	}

	FTDHitFeelTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	UTDCharacterAnimationComponent* Animation = Player->GetCharacterAnimation();
	UUAFComponent* AnimationSystem = Player->GetUAFComponent();
	if (!TestNotNull(TEXT("Player has an animation driver"), Animation) || !TestNotNull(TEXT("Player has a UAF system"), AnimationSystem))
	{
		return false;
	}

	TestTrue(TEXT("Victim hit stop request is accepted"), Animation->StartVictimHitStop(HitStopRealSeconds));
	TestTrue(TEXT("Victim hit stop waits for the next tick"), Animation->IsHitStopPending());
	TestFalse(TEXT("Victim hit stop does not hold the pose in the request frame"), Animation->IsHitStopped());
	TestTrue(TEXT("UAF system keeps running in the request frame"), AnimationSystem->IsActive());
	TestEqual(TEXT("Pending victim hit stop counts toward the remaining time"), Animation->GetHitStopRemainingSeconds(), HitStopRealSeconds, RemainingSecondsTolerance);

	Fixture.TickFrame();
	TestFalse(TEXT("Victim hit stop leaves the pending state after one tick"), Animation->IsHitStopPending());
	TestTrue(TEXT("Victim hit stop holds the pose after one tick"), Animation->IsHitStopped());
	TestFalse(TEXT("UAF system pauses during the victim hit stop"), AnimationSystem->IsActive());
	TestTrue(TEXT("Victim hit stop reports remaining time"), Animation->GetHitStopRemainingSeconds() > 0.f);

	Fixture.Tick(HitStopRealSeconds + FrameSeconds);
	TestFalse(TEXT("Victim hit stop expires"), Animation->IsHitStopped());
	TestTrue(TEXT("UAF system resumes after the victim hit stop"), AnimationSystem->IsActive());

	if (!TestTrue(TEXT("Reaction action starts"), Animation->PlayReactionAction(Attack) > 0.f))
	{
		return false;
	}
	Animation->StartVictimHitStop(HitStopRealSeconds);
	const int32 FramesUntilStart = TickUntilVictimHitStopStarts(Fixture, *Animation);
	TestTrue(FString::Printf(TEXT("Victim hit stop starts after the reaction injection within %d frames (took %d)"), MaximumVictimStartFrames, FramesUntilStart), FramesUntilStart >= 1 && FramesUntilStart <= MaximumVictimStartFrames);
	TestTrue(TEXT("Victim hit stop holds the reaction pose"), Animation->IsHitStopped());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDVictimHitStopUsesPlayRateDuringRootMotionTest, "TDGame.Combat.HitFeel.Reaction.VictimHitStopUsesPlayRateDuringRootMotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDVictimHitStopUsesPlayRateDuringRootMotionTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	UAnimSequence* Attack = LoadObject<UAnimSequence>(nullptr, SwordAttackSequencePath);
	if (!TestNotNull(TEXT("Sword attack sequence exists"), Attack))
	{
		return false;
	}

	FTDHitFeelTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	UTDCharacterAnimationComponent* Animation = Player->GetCharacterAnimation();
	UUAFComponent* AnimationSystem = Player->GetUAFComponent();
	if (!TestNotNull(TEXT("Player has an animation driver"), Animation) || !TestNotNull(TEXT("Player has a UAF system"), AnimationSystem))
	{
		return false;
	}
	if (!TestTrue(TEXT("Root motion action starts"), Animation->PlayAction(Attack, 1.f, 0.f, 0.f, true) > 0.f))
	{
		return false;
	}
	Fixture.Tick(ActionLeadSeconds);
	TestTrue(TEXT("Action drives root motion"), Animation->IsPlayingRootMotionAction());
	const float NormalStep = MeasureActionClockStep(Fixture, *Animation);
	if (!TestTrue(TEXT("Root motion action clock advances"), NormalStep > 0.f))
	{
		return false;
	}

	TestTrue(TEXT("Victim hit stop request is accepted"), Animation->StartVictimHitStop(HitStopRealSeconds));
	const int32 FramesUntilStart = TickUntilVictimHitStopStarts(Fixture, *Animation);
	TestTrue(FString::Printf(TEXT("Victim hit stop leaves the pending state within %d frames (took %d)"), MaximumVictimStartFrames, FramesUntilStart), FramesUntilStart >= 1 && FramesUntilStart <= MaximumVictimStartFrames);
	TestFalse(TEXT("Root motion victim hit stop does not pause the UAF system"), Animation->IsHitStopped());
	TestTrue(TEXT("UAF system keeps running during the root motion victim hit stop"), AnimationSystem->IsActive());
	TestTrue(TEXT("Root motion victim hit stop is reported as a victim stop"), Animation->IsVictimHitStopActive());
	TestFalse(TEXT("Root motion victim hit stop is not an attacker stop"), Animation->IsAttackerHitStopActive());
	const float StoppedStep = MeasureActionClockStep(Fixture, *Animation);
	TestEqual(TEXT("Root motion victim hit stop runs the clock at 0.05x"), StoppedStep / NormalStep, RateHitStopClockScale, ClockScaleTolerance);

	Fixture.Tick(HitStopRealSeconds);
	TestFalse(TEXT("Root motion victim hit stop expires"), Animation->IsVictimHitStopActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDKnockbackWaitsForVictimHitStopTest, "TDGame.Combat.HitFeel.Reaction.KnockbackWaitsForVictimHitStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDKnockbackWaitsForVictimHitStopTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	FTDHitFeelTestWorld Fixture;
	ATDMonsterCharacter* Monster = Fixture.SpawnPlainMonster();
	if (!TestNotNull(TEXT("Monster pawn spawns"), Monster) || !TestNotNull(TEXT("Monster has an animation driver"), Monster->GetCharacterAnimation()))
	{
		return false;
	}
	UTDCharacterAnimationComponent* Animation = Monster->GetCharacterAnimation();
	Fixture.Tick(SettleSeconds);

	const FVector FreeStart = Monster->GetActorLocation();
	Monster->ApplyKnockback(FVector(KnockbackSpeed, 0.f, 0.f), KnockbackSeconds);
	Fixture.Tick(ActionLeadSeconds);
	const float FreeTravel = static_cast<float>(Monster->GetActorLocation().X - FreeStart.X);
	TestTrue(FString::Printf(TEXT("Knockback without a hit stop applies on the next tick (moved %.2f cm)"), FreeTravel), FreeTravel > ReleasedMovementMinimumCm);
	Fixture.Tick(SettleSeconds);

	const FVector HeldStart = Monster->GetActorLocation();
	TestTrue(TEXT("Victim hit stop request is accepted"), Animation->StartVictimHitStop(HeldKnockbackStopSeconds));
	Monster->ApplyKnockback(FVector(KnockbackSpeed, 0.f, 0.f), KnockbackSeconds);
	Fixture.Tick(HeldKnockbackCheckSeconds);
	const float HeldTravel = static_cast<float>(FMath::Abs(Monster->GetActorLocation().X - HeldStart.X));
	TestTrue(FString::Printf(TEXT("Knockback waits while the victim hit stop runs (moved %.2f cm)"), HeldTravel), HeldTravel < HeldMovementToleranceCm);
	TestTrue(TEXT("Victim hit stop is still running at the check"), Animation->IsVictimHitStopActive());

	Fixture.Tick(HeldKnockbackStopSeconds);
	const float ReleasedTravel = static_cast<float>(Monster->GetActorLocation().X - HeldStart.X);
	TestFalse(TEXT("Victim hit stop has ended"), Animation->IsVictimHitStopActive());
	TestTrue(FString::Printf(TEXT("Knockback applies after the victim hit stop ends (moved %.2f cm)"), ReleasedTravel), ReleasedTravel > ReleasedMovementMinimumCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDStaggerImpulseFollowsComboWeightTest, "TDGame.Combat.HitFeel.Reaction.StaggerImpulseFollowsComboWeight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDStaggerImpulseFollowsComboWeightTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("First combo stage pushes 240 cm/s"), ATDMonsterCharacter::ComputeStaggerPushSpeed(true, false, 0), 240.f);
	TestEqual(TEXT("First combo stage pushes for 0.2 s"), ATDMonsterCharacter::ComputeStaggerPushSeconds(true, false, 0), 0.2f);
	TestEqual(TEXT("Third combo stage pushes 240 cm/s"), ATDMonsterCharacter::ComputeStaggerPushSpeed(true, false, 2), 240.f);
	TestEqual(TEXT("Combo finisher pushes 660 cm/s"), ATDMonsterCharacter::ComputeStaggerPushSpeed(true, true, 3), 660.f);
	TestEqual(TEXT("Combo finisher pushes for 0.25 s"), ATDMonsterCharacter::ComputeStaggerPushSeconds(true, true, 3), 0.25f);
	TestEqual(TEXT("Melee without combo information keeps the 450 cm/s impulse"), ATDMonsterCharacter::ComputeStaggerPushSpeed(true, false, INDEX_NONE), 450.f);
	TestEqual(TEXT("Melee without combo information stays an impulse"), ATDMonsterCharacter::ComputeStaggerPushSeconds(true, false, INDEX_NONE), 0.f);
	TestEqual(TEXT("Non-melee damage keeps the 450 cm/s impulse"), ATDMonsterCharacter::ComputeStaggerPushSpeed(false, false, 0), 450.f);
	TestEqual(TEXT("Non-melee damage ignores the finisher flag"), ATDMonsterCharacter::ComputeStaggerPushSpeed(false, true, 3), 450.f);
	TestEqual(TEXT("Non-melee damage stays an impulse"), ATDMonsterCharacter::ComputeStaggerPushSeconds(false, true, 3), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDStaggerPushTravelsComboDistanceTest, "TDGame.Combat.HitFeel.Reaction.StaggerPushTravelsComboDistance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDStaggerPushTravelsComboDistanceTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	constexpr float MonsterSpacingCm = 400.f;
	constexpr float SourceOffsetCm = 100.f;
	constexpr float PushObservationSeconds = 0.4f;
	constexpr float ComboTravelCm = 25.f;
	constexpr float ComboTravelToleranceCm = 5.f;
	constexpr float FinisherTravelCm = 85.f;
	constexpr float FinisherTravelToleranceCm = 10.f;
	constexpr float ImpulseMatchToleranceCm = 0.5f;
	constexpr float LegacyImpulseSpeed = 450.f;

	FTDHitFeelTestWorld Fixture;
	ATDMonsterCharacter* ComboTarget = Fixture.SpawnPlainMonster(FVector(0.f, 0.f, 0.f));
	ATDMonsterCharacter* FinisherTarget = Fixture.SpawnPlainMonster(FVector(0.f, MonsterSpacingCm, 0.f));
	ATDMonsterCharacter* LegacyTarget = Fixture.SpawnPlainMonster(FVector(0.f, MonsterSpacingCm * 2.f, 0.f));
	ATDMonsterCharacter* ReferenceTarget = Fixture.SpawnPlainMonster(FVector(0.f, MonsterSpacingCm * 3.f, 0.f));
	if (!TestNotNull(TEXT("Combo target spawns"), ComboTarget) || !TestNotNull(TEXT("Finisher target spawns"), FinisherTarget)
		|| !TestNotNull(TEXT("Legacy target spawns"), LegacyTarget) || !TestNotNull(TEXT("Reference target spawns"), ReferenceTarget))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);

	const FVector SourceOffset(SourceOffsetCm, 0.f, 0.f);
	const FVector ComboStart = ComboTarget->GetActorLocation();
	const FVector FinisherStart = FinisherTarget->GetActorLocation();
	const FVector LegacyStart = LegacyTarget->GetActorLocation();
	const FVector ReferenceStart = ReferenceTarget->GetActorLocation();
	ComboTarget->PushBodyAwayFrom(ComboStart - SourceOffset, ATDMonsterCharacter::ComputeStaggerPushSpeed(true, false, 0), ATDMonsterCharacter::ComputeStaggerPushSeconds(true, false, 0));
	FinisherTarget->PushBodyAwayFrom(FinisherStart - SourceOffset, ATDMonsterCharacter::ComputeStaggerPushSpeed(true, true, 3), ATDMonsterCharacter::ComputeStaggerPushSeconds(true, true, 3));
	LegacyTarget->PushBodyAwayFrom(LegacyStart - SourceOffset, ATDMonsterCharacter::ComputeStaggerPushSpeed(false, false, INDEX_NONE), ATDMonsterCharacter::ComputeStaggerPushSeconds(false, false, INDEX_NONE));
	ReferenceTarget->AddImpulseVelocity(FVector(LegacyImpulseSpeed, 0.f, 0.f));
	Fixture.Tick(PushObservationSeconds);

	const float ComboTravel = static_cast<float>(ComboTarget->GetActorLocation().X - ComboStart.X);
	const float FinisherTravel = static_cast<float>(FinisherTarget->GetActorLocation().X - FinisherStart.X);
	const float LegacyTravel = static_cast<float>(LegacyTarget->GetActorLocation().X - LegacyStart.X);
	const float ReferenceTravel = static_cast<float>(ReferenceTarget->GetActorLocation().X - ReferenceStart.X);
	AddInfo(FString::Printf(TEXT("Push travel after %.1f s: combo %.2f cm, finisher %.2f cm, legacy %.2f cm, reference impulse %.2f cm"), PushObservationSeconds, ComboTravel, FinisherTravel, LegacyTravel, ReferenceTravel));
	TestTrue(FString::Printf(TEXT("Combo stage push travels %.0f +/- %.0f cm (got %.2f)"), ComboTravelCm, ComboTravelToleranceCm, ComboTravel), FMath::IsNearlyEqual(ComboTravel, ComboTravelCm, ComboTravelToleranceCm));
	TestTrue(FString::Printf(TEXT("Finisher push travels %.0f +/- %.0f cm (got %.2f)"), FinisherTravelCm, FinisherTravelToleranceCm, FinisherTravel), FMath::IsNearlyEqual(FinisherTravel, FinisherTravelCm, FinisherTravelToleranceCm));
	TestTrue(FString::Printf(TEXT("Push without combo information matches the old 450 cm/s impulse (got %.2f, reference %.2f)"), LegacyTravel, ReferenceTravel), FMath::IsNearlyEqual(LegacyTravel, ReferenceTravel, ImpulseMatchToleranceCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDEliteMonsterFlagTest, "TDGame.Combat.HitFeel.Reaction.EliteMonsterFlag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDEliteMonsterFlagTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	FTDHitFeelTestWorld Fixture;
	const ATDMonsterCharacter* Elite = Fixture.SpawnMonsterOfDefinition(TEXT("BookHead_Brute"), FVector(-400.f, 0.f, 0.f));
	const ATDMonsterCharacter* Grunt = Fixture.SpawnMonsterOfDefinition(TEXT("Goblin_Melee"), FVector(400.f, 0.f, 0.f));
	const ATDMonsterCharacter* Plain = Fixture.SpawnPlainMonster();
	if (!TestNotNull(TEXT("Elite monster spawns"), Elite) || !TestNotNull(TEXT("Grunt monster spawns"), Grunt) || !TestNotNull(TEXT("Plain monster spawns"), Plain))
	{
		return false;
	}
	TestTrue(TEXT("BookHead_Brute is an elite"), Elite->IsEliteMonster());
	TestFalse(TEXT("Goblin_Melee is not an elite"), Grunt->IsEliteMonster());
	TestFalse(TEXT("A monster without a species is not an elite"), Plain->IsEliteMonster());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDKillPoseHoldNeedsFeedbackSubsystemTest, "TDGame.Combat.HitFeel.Reaction.KillPoseHoldNeedsFeedbackSubsystem", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDKillPoseHoldNeedsFeedbackSubsystemTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	FTDHitFeelTestWorld Fixture;
	ATDMonsterCharacter* Monster = Fixture.SpawnMonsterOfDefinition(NAME_None, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Monster pawn spawns"), Monster))
	{
		return false;
	}
	UTDCombatComponent* Combat = Monster->GetCombatComponent();
	UTDCharacterAnimationComponent* Animation = Monster->GetCharacterAnimation();
	if (!TestNotNull(TEXT("Monster has combat"), Combat) || !TestNotNull(TEXT("Monster has an animation driver"), Animation))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);

	const bool bHasFeedbackSubsystem = Fixture.World->GetSubsystem<UTDCombatFeedbackSubsystem>() != nullptr;
	FTDDamageContext Context;
	Context.Delivery = ETDDamageDelivery::Melee;
	Context.Budget = MakeShared<FTDDamageChainBudget>();
	Combat->ReceiveDamage(LethalDamage, ETDDamageElement::Physical, false, Context);
	if (!TestFalse(TEXT("Monster dies"), Combat->IsAlive()))
	{
		return false;
	}
	TestTrue(TEXT("Kill pose hold runs only when the feedback subsystem exists"), Animation->IsHitStopped() == bHasFeedbackSubsystem);
	TestFalse(TEXT("Kill pose hold never leaves a pending victim stop"), Animation->IsHitStopPending());
	return true;
}

namespace TDHitFeelReactionRealTimeTestSupport
{
	constexpr float SlowMotionDilation = 0.2f;
	constexpr float ChangedDilation = 0.5f;
	constexpr float AttackerStopRealSeconds = 0.05f;
	constexpr float VictimStopRealSeconds = 0.067f;
	constexpr float LongStopRealSeconds = 0.1f;
	constexpr int32 MaximumCountedTicks = 120;
	constexpr int32 AllowedFrameError = 1;
	constexpr float SpellRadius = 200.f;
	constexpr float MeleeRange = 100.f;
	constexpr float RangedRange = 600.f;
	constexpr float WindupSeconds = 0.5f;
	constexpr float WindupPassedSeconds = 0.7f;

	int32 CountTicksWhile(TDHitFeelReactionTestSupport::FTDHitFeelTestWorld& Fixture, TFunctionRef<bool()> IsStillActive)
	{
		int32 TickCount = 0;
		while (IsStillActive() && TickCount < MaximumCountedTicks)
		{
			Fixture.TickFrame();
			++TickCount;
		}
		return TickCount;
	}

	bool IsWithinOneFrame(const int32 FrameCount, const float RealSeconds)
	{
		return FMath::Abs(FrameCount - FMath::RoundToInt(RealSeconds / TDHitFeelReactionTestSupport::FrameSeconds)) <= AllowedFrameError;
	}

	UTDDamageDefinition* MakeSpell(const ETDDamageEntityMode Mode, const ETDDamageAreaShape Shape)
	{
		UTDDamageDefinition* Spell = NewObject<UTDDamageDefinition>(GetTransientPackage());
		Spell->Mode = Mode;
		Spell->AreaShape = Shape;
		Spell->Radius = SpellRadius;
		Spell->ActivationDelay = WindupSeconds;
		return Spell;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitStopsEndInRealTimeUnderSlowMotionTest, "TDGame.Combat.HitFeel.Reaction.HitStopsEndInRealTimeUnderSlowMotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitStopsEndInRealTimeUnderSlowMotionTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	using namespace TDHitFeelReactionRealTimeTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	UAnimSequence* Attack = LoadObject<UAnimSequence>(nullptr, SwordAttackSequencePath);
	if (!TestNotNull(TEXT("Sword attack sequence exists"), Attack))
	{
		return false;
	}

	FTDHitFeelTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	AWorldSettings* WorldSettings = Fixture.World->GetWorldSettings();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player) || !TestNotNull(TEXT("World settings exist"), WorldSettings))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	UTDCharacterAnimationComponent* Animation = Player->GetCharacterAnimation();
	if (!TestNotNull(TEXT("Player has an animation driver"), Animation))
	{
		return false;
	}

	WorldSettings->SetTimeDilation(SlowMotionDilation);
	if (!TestTrue(TEXT("Action starts under slow motion"), Animation->PlayAction(Attack) > 0.f))
	{
		return false;
	}
	Fixture.Tick(ActionLeadSeconds);
	TestTrue(TEXT("Attacker hit stop starts under slow motion"), Animation->StartAttackerHitStop(AttackerStopRealSeconds));
	const int32 AttackerFrames = CountTicksWhile(Fixture, [Animation]() { return Animation->IsAttackerHitStopActive(); });
	TestTrue(FString::Printf(TEXT("Attacker hit stop of %.3f s ends in real time under %.1fx (%d frames)"), AttackerStopRealSeconds, SlowMotionDilation, AttackerFrames), IsWithinOneFrame(AttackerFrames, AttackerStopRealSeconds));
	Animation->StopAction();

	TestTrue(TEXT("Victim hit stop request is accepted under slow motion"), Animation->StartVictimHitStop(VictimStopRealSeconds));
	Fixture.TickFrame();
	TestTrue(TEXT("Victim hit stop starts on the next tick under slow motion"), Animation->IsHitStopped());
	const int32 VictimFrames = CountTicksWhile(Fixture, [Animation]() { return Animation->IsHitStopped(); });
	TestTrue(FString::Printf(TEXT("Victim hit stop of %.3f s ends in real time under %.1fx (%d frames)"), VictimStopRealSeconds, SlowMotionDilation, VictimFrames), IsWithinOneFrame(VictimFrames, VictimStopRealSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitStopsKeepRealLengthWhenDilationChangesTest, "TDGame.Combat.HitFeel.Reaction.HitStopsKeepRealLengthWhenDilationChanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitStopsKeepRealLengthWhenDilationChangesTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	using namespace TDHitFeelReactionRealTimeTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	UAnimSequence* Attack = LoadObject<UAnimSequence>(nullptr, SwordAttackSequencePath);
	if (!TestNotNull(TEXT("Sword attack sequence exists"), Attack))
	{
		return false;
	}

	FTDHitFeelTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	AWorldSettings* WorldSettings = Fixture.World->GetWorldSettings();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player) || !TestNotNull(TEXT("World settings exist"), WorldSettings))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);
	UTDCharacterAnimationComponent* Animation = Player->GetCharacterAnimation();
	if (!TestNotNull(TEXT("Player has an animation driver"), Animation))
	{
		return false;
	}

	if (!TestTrue(TEXT("Action starts"), Animation->PlayAction(Attack) > 0.f))
	{
		return false;
	}
	Fixture.Tick(ActionLeadSeconds);
	TestTrue(TEXT("Attacker hit stop starts at full speed"), Animation->StartAttackerHitStop(LongStopRealSeconds));
	Fixture.TickFrame();
	WorldSettings->SetTimeDilation(ChangedDilation);
	const int32 AttackerFrames = 1 + CountTicksWhile(Fixture, [Animation]() { return Animation->IsAttackerHitStopActive(); });
	TestTrue(FString::Printf(TEXT("Attacker hit stop keeps %.3f s of real time when dilation drops to %.1fx (%d frames)"), LongStopRealSeconds, ChangedDilation, AttackerFrames), IsWithinOneFrame(AttackerFrames, LongStopRealSeconds));
	Animation->StopAction();

	WorldSettings->SetTimeDilation(1.f);
	TestTrue(TEXT("Victim hit stop request is accepted"), Animation->StartVictimHitStop(LongStopRealSeconds));
	Fixture.TickFrame();
	TestTrue(TEXT("Victim hit stop starts on the next tick"), Animation->IsHitStopped());
	Fixture.TickFrame();
	WorldSettings->SetTimeDilation(ChangedDilation);
	const int32 VictimFrames = 1 + CountTicksWhile(Fixture, [Animation]() { return Animation->IsHitStopped(); });
	TestTrue(FString::Printf(TEXT("Victim hit stop keeps %.3f s of real time when dilation drops to %.1fx (%d frames)"), LongStopRealSeconds, ChangedDilation, VictimFrames), IsWithinOneFrame(VictimFrames, LongStopRealSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDangerWindupFollowsAbilityRequestTest, "TDGame.Combat.HitFeel.Reaction.DangerWindupFollowsAbilityRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDangerWindupFollowsAbilityRequestTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	using namespace TDHitFeelReactionRealTimeTestSupport;
	FTDHitFeelTestWorld Fixture;
	ATDMonsterCharacter* Monster = Fixture.SpawnPlainMonster();
	if (!TestNotNull(TEXT("Monster pawn spawns"), Monster))
	{
		return false;
	}
	Fixture.Tick(SettleSeconds);

	FTDMonsterAbilityRequest Request;
	Request.Spell = MakeSpell(ETDDamageEntityMode::Area, ETDDamageAreaShape::Circle);
	Request.Range = MeleeRange;
	Request.TargetLocation = Monster->GetActorLocation() + FVector(MeleeRange, 0.f, 0.f);
	Request.WindupSeconds = WindupSeconds;
	Request.bIsDanger = true;
	if (!TestTrue(TEXT("Danger ability starts"), Monster->BeginBodyAbility(Request) > 0.f))
	{
		return false;
	}
	TestTrue(TEXT("Danger ability is in its windup"), Monster->IsInDangerWindup());
	TestTrue(TEXT("Danger ability is a committed attack"), Monster->IsInCommittedAttack());
	Fixture.Tick(WindupPassedSeconds);
	TestFalse(TEXT("Danger windup ends at the impact time"), Monster->IsInDangerWindup());

	Request.bIsDanger = false;
	if (!TestTrue(TEXT("Normal ability starts"), Monster->BeginBodyAbility(Request) > 0.f))
	{
		return false;
	}
	TestFalse(TEXT("Normal ability windup is not a danger windup"), Monster->IsInDangerWindup());
	TestTrue(TEXT("Normal ability windup is a committed attack"), Monster->IsInCommittedAttack());
	Request.bIsDanger = true;
	Fixture.Tick(WindupPassedSeconds);
	Monster->BeginBodyAbility(Request);
	Monster->CancelBodyAbility();
	TestFalse(TEXT("Cancelling the ability clears the danger windup"), Monster->IsInDangerWindup());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMeleeSwingMessageOnlyForMeleeSpellsTest, "TDGame.Combat.HitFeel.Reaction.MeleeSwingMessageOnlyForMeleeSpells", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMeleeSwingMessageOnlyForMeleeSpellsTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionRealTimeTestSupport;
	TestTrue(TEXT("Self-centred circle is a melee swing"), ATDMonsterCharacter::IsMeleeSwingSpell(*MakeSpell(ETDDamageEntityMode::Area, ETDDamageAreaShape::Circle), MeleeRange));
	TestTrue(TEXT("Self-centred cone is a melee swing"), ATDMonsterCharacter::IsMeleeSwingSpell(*MakeSpell(ETDDamageEntityMode::Area, ETDDamageAreaShape::Cone), MeleeRange));
	TestFalse(TEXT("Ranged area is not a melee swing"), ATDMonsterCharacter::IsMeleeSwingSpell(*MakeSpell(ETDDamageEntityMode::Area, ETDDamageAreaShape::Circle), RangedRange));
	TestFalse(TEXT("Line area is not a melee swing"), ATDMonsterCharacter::IsMeleeSwingSpell(*MakeSpell(ETDDamageEntityMode::Area, ETDDamageAreaShape::Line), MeleeRange));
	TestFalse(TEXT("Mine is not a melee swing"), ATDMonsterCharacter::IsMeleeSwingSpell(*MakeSpell(ETDDamageEntityMode::Mine, ETDDamageAreaShape::Circle), MeleeRange));
	TestFalse(TEXT("Shockwave is not a melee swing"), ATDMonsterCharacter::IsMeleeSwingSpell(*MakeSpell(ETDDamageEntityMode::Shockwave, ETDDamageAreaShape::Circle), MeleeRange));
	TestFalse(TEXT("Projectile is not a melee swing"), ATDMonsterCharacter::IsMeleeSwingSpell(*MakeSpell(ETDDamageEntityMode::Projectile, ETDDamageAreaShape::Circle), MeleeRange));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeferredKnockbackClearsTest, "TDGame.Combat.HitFeel.Reaction.DeferredKnockbackClears", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeferredKnockbackClearsTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelReactionTestSupport;
	FTDHitFeelTestWorld Fixture;
	ATDMonsterCharacter* Monster = Fixture.SpawnPlainMonster();
	if (!TestNotNull(TEXT("Monster pawn spawns"), Monster) || !TestNotNull(TEXT("Monster has an animation driver"), Monster->GetCharacterAnimation()))
	{
		return false;
	}
	UTDCharacterAnimationComponent* Animation = Monster->GetCharacterAnimation();
	Fixture.Tick(SettleSeconds);

	const FVector HeldStart = Monster->GetActorLocation();
	TestTrue(TEXT("Victim hit stop request is accepted"), Animation->StartVictimHitStop(HeldKnockbackStopSeconds));
	Monster->ApplyKnockback(FVector(KnockbackSpeed, 0.f, 0.f), KnockbackSeconds);
	Monster->AddImpulseVelocity(FVector(KnockbackSpeed, 0.f, 0.f));
	Fixture.Tick(ActionLeadSeconds);
	Monster->ClearDeferredKnockback();
	Fixture.Tick(HeldKnockbackStopSeconds + SettleSeconds);
	const float ClearedTravel = static_cast<float>(FMath::Abs(Monster->GetActorLocation().X - HeldStart.X));
	TestFalse(TEXT("Victim hit stop has ended"), Animation->IsVictimHitStopActive());
	TestTrue(FString::Printf(TEXT("Cleared knockback and impulse never apply (moved %.2f cm)"), ClearedTravel), ClearedTravel < HeldMovementToleranceCm);
	return true;
}

#endif
