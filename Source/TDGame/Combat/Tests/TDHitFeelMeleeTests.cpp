#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Characters/TDApproachStepLayeredMove.h"
#include "Characters/TDGameCharacter.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/GAS/Abilities/TDPlayerMovementAbilities.h"
#include "Combat/GAS/TDAbilityTask_PlayActionTimeline.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Combat/Skills/TDCombatStyleDefinition.h"
#include "Combat/Skills/TDPlayerCombatCatalog.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Core/TDGameplayTags.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace TDHitFeelMeleeTestSupport
{
	constexpr float FrameSeconds = 1.f / 60.f;
	constexpr float FloorHalfThickness = 50.f;
	constexpr float PlayerSpawnHeight = 98.f;
	constexpr float WindowTolerance = 0.002f;
	constexpr float TargetCapsuleRadius = 40.f;
	constexpr int32 SwordBranchFrames[] = { 36, 36, 38, 68 };
	constexpr int32 SwordLastFrames[] = { 86, 86, 92, 68 };
	constexpr int32 SwordContactFrames[] = { 20, 23, 21, 35 };
	constexpr int32 FinisherRollCancelFrame = 55;
	constexpr double BranchToleranceSeconds = 0.04;
	constexpr float ApproachSurfaceDistanceCm = 160.f;
	constexpr float ApproachToleranceCm = 10.f;
	constexpr float StillToleranceCm = 15.f;
	constexpr float LedgeHeightCm = 60.f;
	constexpr float NearSurfaceDistanceCm = 40.f;

	enum class ETDApproachScenario : uint8
	{
		OpenGround,
		TargetOnLedge,
		RecentlyHit
	};

	float FrameToSeconds(const int32 Frame)
	{
		return static_cast<float>(Frame - 1) / 60.f;
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

	void PreloadPlayerAnimations(UClass& PlayerPawnClass)
	{
		const UObject* PlayerPawnDefaults = PlayerPawnClass.GetDefaultObject();
		for (TFieldIterator<FSoftObjectProperty> PropertyIt(&PlayerPawnClass); PropertyIt; ++PropertyIt)
		{
			if (PropertyIt->PropertyClass->IsChildOf(UAnimationAsset::StaticClass()))
			{
				PropertyIt->GetPropertyValue_InContainer(PlayerPawnDefaults).LoadSynchronous();
			}
		}
	}

	void IgnoreEngineInjectionTimelineEnsure(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("TimelineTrait.GetState(Context, ChildState)"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("=== Handled ensure: ==="), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Stack:"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("[Callstack]"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Exact, -1, false);
	}

	struct FTDHitFeelMeleeTestWorld
	{
		FTDHitFeelMeleeTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDHitFeelMeleeTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			SpawnBox(FVector(0.f, 0.f, -FloorHalfThickness), FVector(5000.f, 5000.f, FloorHalfThickness));
		}

		~FTDHitFeelMeleeTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void SpawnBox(const FVector& Location, const FVector& Extent)
		{
			AActor* BoxActor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(BoxActor);
			BoxActor->AddInstanceComponent(Box);
			BoxActor->SetRootComponent(Box);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->RegisterComponent();
			BoxActor->SetActorLocation(Location);
		}

		AActor* SpawnCapsuleTarget(const FVector& Location)
		{
			AActor* Target = World->SpawnActor<AActor>();
			UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(Target);
			Target->AddInstanceComponent(Capsule);
			Target->SetRootComponent(Capsule);
			Capsule->InitCapsuleSize(TargetCapsuleRadius, 90.f);
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Capsule->RegisterComponent();
			Target->SetActorLocation(Location);
			return Target;
		}

		ATDGameCharacter* SpawnPlayer()
		{
			UClass* PlayerPawnClass = LoadDefaultPlayerPawnClass();
			if (!PlayerPawnClass)
			{
				return nullptr;
			}
			PreloadPlayerAnimations(*PlayerPawnClass);
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, PlayerSpawnHeight));
			return World->SpawnActor<ATDGameCharacter>(PlayerPawnClass, SpawnTransform);
		}

		UTDSkillComponent* SpawnSkillOwner()
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Actor->AddInstanceComponent(Sphere);
			Actor->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(10.f);
			Sphere->RegisterComponent();
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			Combatant->RegisterComponent();
			UTDSkillComponent* SkillComponent = NewObject<UTDSkillComponent>(Actor);
			Actor->AddInstanceComponent(SkillComponent);
			SkillComponent->RegisterComponent();
			return SkillComponent;
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

	UTDCombatComponent* FindCombatComponent(const UTDSkillComponent* SkillComponent)
	{
		const AActor* Owner = SkillComponent ? SkillComponent->GetOwner() : nullptr;
		return Owner ? Owner->FindComponentByClass<UTDCombatComponent>() : nullptr;
	}

	void TickUntilElapsed(FTDHitFeelMeleeTestWorld& Fixture, const double StartSeconds, const double ElapsedSeconds)
	{
		for (int32 Frame = 0; Frame < 600 && Fixture.World->GetTimeSeconds() - StartSeconds < ElapsedSeconds - 0.001; ++Frame)
		{
			Fixture.Tick(FrameSeconds);
		}
	}

	bool LoadSwordComboClips(const UTDSkillComponent& SkillComponent)
	{
		const FGameplayTag ComboTags[] =
		{
			TDGameplayTags::Action_Attack_Primary_01,
			TDGameplayTags::Action_Attack_Primary_02,
			TDGameplayTags::Action_Attack_Primary_03,
			TDGameplayTags::Action_Attack_Primary_04
		};
		for (const FGameplayTag& ComboTag : ComboTags)
		{
			const FTDCombatActionDefinition* Definition = SkillComponent.FindActionDefinition(ComboTag);
			if (!Definition || !Definition->Action.Animation.LoadSynchronous())
			{
				return false;
			}
		}
		return true;
	}

	bool AddDirectSkillAction(const UTDSkillComponent& SkillComponent, const FGameplayTag ActionTag)
	{
		UTDCombatStyleDefinition* Style = SkillComponent.GetSkillSet();
		const FMapProperty* ActionMapProperty = FindFProperty<FMapProperty>(UTDCombatStyleDefinition::StaticClass(), TEXT("ActionMap"));
		if (!Style || !ActionMapProperty)
		{
			return false;
		}
		TMap<FGameplayTag, FTDCombatActionDefinition>* ActionMap = ActionMapProperty->ContainerPtrToValuePtr<TMap<FGameplayTag, FTDCombatActionDefinition>>(Style);
		FTDCombatActionDefinition Definition;
		Definition.HitExecutionType = ETDCombatHitExecutionType::DirectDamage;
		ActionMap->Add(ActionTag, Definition);
		return true;
	}

	FTDDamageContext MakeDirectDamageContext()
	{
		FTDDamageContext Context;
		Context.Delivery = ETDDamageDelivery::Direct;
		Context.Budget = MakeShared<FTDDamageChainBudget>();
		return Context;
	}

	bool MeasureApproachDisplacement(const ETDApproachScenario Scenario, float& OutDisplacementCm)
	{
		FTDHitFeelMeleeTestWorld Fixture;
		ATDGameCharacter* Player = Fixture.SpawnPlayer();
		UTDSkillComponent* SkillComponent = Player ? Player->GetSkillComponent() : nullptr;
		if (!SkillComponent || !LoadSwordComboClips(*SkillComponent))
		{
			return false;
		}
		Fixture.Tick(0.3f);

		const FVector StartLocation = Player->GetActorLocation();
		const float CenterDistance = ApproachSurfaceDistanceCm + Player->GetCapsuleComponent()->GetScaledCapsuleRadius() + TargetCapsuleRadius;
		FVector TargetLocation = StartLocation + FVector(CenterDistance, 0.f, 0.f);
		if (Scenario == ETDApproachScenario::TargetOnLedge)
		{
			const float LedgeStartX = 60.f;
			const float LedgeEndX = CenterDistance + 100.f;
			Fixture.SpawnBox(FVector(StartLocation.X + (LedgeStartX + LedgeEndX) * 0.5f, StartLocation.Y, LedgeHeightCm * 0.5f), FVector((LedgeEndX - LedgeStartX) * 0.5f, 200.f, LedgeHeightCm * 0.5f));
			TargetLocation.Z += LedgeHeightCm;
		}
		if (Scenario == ETDApproachScenario::RecentlyHit)
		{
			UTDCombatComponent* Combat = Player->GetCombatComponent();
			Combat->ReceiveDamage(Combat->GetMaxHealth() * 0.01f, ETDDamageElement::Physical, false, MakeDirectDamageContext());
		}

		AActor* Target = Fixture.SpawnCapsuleTarget(TargetLocation);
		if (!Player->ExecutePrimaryAttack(Target))
		{
			return false;
		}
		Fixture.Tick(TDApproachStep::DurationSeconds);
		OutDisplacementCm = FVector::Dist2D(Player->GetActorLocation(), StartLocation);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelSwordCatalogTimingsTest, "TDGame.Combat.HitFeel.Melee.SwordCatalogTimings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelSwordCatalogTimingsTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelMeleeTestSupport;
	UTDCombatStyleDefinition* Style = TDPlayerCombatCatalog::CreateSwordShieldStyle(GetTransientPackage());
	if (!TestNotNull(TEXT("The sword and shield style builds"), Style))
	{
		return false;
	}

	const FGameplayTag ComboTags[] =
	{
		TDGameplayTags::Action_Attack_Primary_01,
		TDGameplayTags::Action_Attack_Primary_02,
		TDGameplayTags::Action_Attack_Primary_03,
		TDGameplayTags::Action_Attack_Primary_04
	};
	for (int32 StageIndex = 0; StageIndex < static_cast<int32>(UE_ARRAY_COUNT(ComboTags)); ++StageIndex)
	{
		const FTDCombatActionDefinition* Definition = Style->FindAction(ComboTags[StageIndex]);
		if (!TestNotNull(FString::Printf(TEXT("Combo %d is in the catalog"), StageIndex + 1), Definition))
		{
			continue;
		}
		const FTDActionAnimation& Action = Definition->Action;
		const bool bIsFinisher = StageIndex == 3;
		const float BranchSeconds = FrameToSeconds(SwordBranchFrames[StageIndex]);
		const float ClipSeconds = FrameToSeconds(SwordLastFrames[StageIndex]);
		const FString Stage = FString::Printf(TEXT("Combo %d"), StageIndex + 1);

		TestEqual(Stage + TEXT(" buffers attack input from 0.12 s"), Action.InputBufferWindow.StartSeconds, 0.12f, WindowTolerance);
		TestEqual(Stage + TEXT(" roll cancel window ends with the clip"), Action.RollCancelWindow.EndSeconds, ClipSeconds, WindowTolerance);
		const float ExpectedRollCancelSeconds = bIsFinisher ? FrameToSeconds(FinisherRollCancelFrame) : BranchSeconds;
		TestEqual(Stage + TEXT(" roll cancel window opens at its frame"), Action.RollCancelWindow.StartSeconds, ExpectedRollCancelSeconds, WindowTolerance);
		TestEqual(Stage + TEXT(" hit confirmation lead"), Action.HitConfirmLeadSeconds, bIsFinisher ? 0.f : 0.05f, WindowTolerance);
		if (bIsFinisher)
		{
			TestFalse(Stage + TEXT(" finisher cannot be cancelled into a skill"), Action.SkillCancelWindow.IsValid());
			TestFalse(Stage + TEXT(" finisher cannot be cancelled by movement"), Action.MovementCancelWindow.IsValid());
		}
		else
		{
			TestEqual(Stage + TEXT(" skill cancel opens with the combo window"), Action.SkillCancelWindow.StartSeconds, BranchSeconds, WindowTolerance);
			TestEqual(Stage + TEXT(" movement cancel opens 0.15 s after the branch"), Action.MovementCancelWindow.StartSeconds, BranchSeconds + 0.15f, WindowTolerance);
		}

		if (!TestEqual(Stage + TEXT(" has one sweep"), Action.HitWindows.Num(), 1))
		{
			continue;
		}
		const float HitWindowStartSeconds = Action.HitWindows[0].Window.StartSeconds;
		const float ExpectedSwingSoundSeconds = FMath::Max(FrameToSeconds(SwordContactFrames[StageIndex]) - 0.07f, HitWindowStartSeconds);
		TestEqual(Stage + TEXT(" swing sound plays 0.07 s before contact"), HitWindowStartSeconds + Action.SwingSoundDelaySeconds, ExpectedSwingSoundSeconds, WindowTolerance);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelInputBufferRulesTest, "TDGame.Combat.HitFeel.Melee.InputBufferRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelInputBufferRulesTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelMeleeTestSupport;
	FTDHitFeelMeleeTestWorld Fixture;
	UTDSkillComponent* SkillComponent = Fixture.SpawnSkillOwner();
	UTDCombatComponent* Combat = FindCombatComponent(SkillComponent);
	if (!TestNotNull(TEXT("Skill owner spawns"), SkillComponent) || !TestNotNull(TEXT("Skill owner has combat"), Combat))
	{
		return false;
	}

	Combat->AddLooseGameplayTag(TDGameplayTags::State_Attacking);
	TestFalse(TEXT("Input is not buffered before the buffer window opens"), SkillComponent->BufferCombatInput(TDGameplayTags::Action_Attack_Primary, nullptr));

	SkillComponent->BeginInputBufferWindow();
	TestTrue(TEXT("A skill is buffered"), SkillComponent->BufferCombatInput(TDGameplayTags::Action_Skill_Q, nullptr));
	TestTrue(TEXT("A later attack is buffered"), SkillComponent->BufferCombatInput(TDGameplayTags::Action_Attack_Primary, nullptr));
	TestTrue(TEXT("The last attack input wins"), SkillComponent->GetBufferedActionTag() == TDGameplayTags::Action_Attack_Primary);

	TestTrue(TEXT("A roll replaces a buffered attack"), SkillComponent->BufferCombatInput(TDGameplayTags::Action_Roll, nullptr));
	TestFalse(TEXT("An attack cannot replace a buffered roll"), SkillComponent->BufferCombatInput(TDGameplayTags::Action_Attack_Primary, nullptr));
	TestTrue(TEXT("The roll stays buffered"), SkillComponent->GetBufferedActionTag() == TDGameplayTags::Action_Roll);

	Fixture.Tick(0.1f);
	TestTrue(TEXT("A roll is still buffered after 0.1 s"), SkillComponent->GetBufferedActionTag() == TDGameplayTags::Action_Roll);
	Fixture.Tick(0.15f);
	TestFalse(TEXT("A buffered roll expires after 0.2 s"), SkillComponent->GetBufferedActionTag().IsValid());
	TestTrue(TEXT("An attack is buffered again once the roll expired"), SkillComponent->BufferCombatInput(TDGameplayTags::Action_Attack_Primary, nullptr));

	SkillComponent->EndInputBufferWindow();
	SkillComponent->ClearBufferedInput();
	Combat->RemoveLooseGameplayTag(TDGameplayTags::State_Attacking);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelComboStageQueriesTest, "TDGame.Combat.HitFeel.Melee.ComboStageQueries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelComboStageQueriesTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelMeleeTestSupport;
	FTDHitFeelMeleeTestWorld Fixture;
	UTDSkillComponent* SkillComponent = Fixture.SpawnSkillOwner();
	if (!TestNotNull(TEXT("Skill owner spawns"), SkillComponent))
	{
		return false;
	}
	SkillComponent->SetPrimaryComboSettings(4, 1.6f);

	TestEqual(TEXT("No action reports no combo stage"), SkillComponent->GetActivePrimaryComboStageIndex(), INDEX_NONE);
	SkillComponent->BeginActionExecution(nullptr, TDGameplayTags::Action_Attack_Primary_02, nullptr, ETDCombatHitExecutionType::TimelineSweep);
	TestEqual(TEXT("The second stage reports index 1"), SkillComponent->GetActivePrimaryComboStageIndex(), 1);
	TestFalse(TEXT("The second stage is not the finisher"), SkillComponent->IsPrimaryComboFinisherActive());
	SkillComponent->BeginActionExecution(nullptr, TDGameplayTags::Action_Attack_Primary_04, nullptr, ETDCombatHitExecutionType::TimelineSweep);
	TestEqual(TEXT("The fourth stage reports index 3"), SkillComponent->GetActivePrimaryComboStageIndex(), 3);
	TestTrue(TEXT("The fourth stage is the finisher"), SkillComponent->IsPrimaryComboFinisherActive());
	SkillComponent->BeginActionExecution(nullptr, TDGameplayTags::Action_Skill_Q, nullptr, ETDCombatHitExecutionType::TimelineSweep);
	TestEqual(TEXT("A skill is not a combo stage"), SkillComponent->GetActivePrimaryComboStageIndex(), INDEX_NONE);
	TestFalse(TEXT("A skill is not the finisher"), SkillComponent->IsPrimaryComboFinisherActive());
	SkillComponent->EndActionExecution(nullptr);
	TestEqual(TEXT("Ending the action clears the stage"), SkillComponent->GetActivePrimaryComboStageIndex(), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelHitConfirmWindowTest, "TDGame.Combat.HitFeel.Melee.HitConfirmWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelHitConfirmWindowTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelMeleeTestSupport;
	FTDActionTimeWindow ComboWindow;
	ComboWindow.StartSeconds = 0.583f;
	ComboWindow.EndSeconds = 1.417f;

	const FTDActionTimeWindow Missed = UTDAbilityTask_PlayActionTimeline::ResolveHitConfirmedWindow(ComboWindow, 0.05f, false);
	const FTDActionTimeWindow Confirmed = UTDAbilityTask_PlayActionTimeline::ResolveHitConfirmedWindow(ComboWindow, 0.05f, true);
	const FTDActionTimeWindow Finisher = UTDAbilityTask_PlayActionTimeline::ResolveHitConfirmedWindow(ComboWindow, 0.f, true);
	TestEqual(TEXT("A miss keeps the branch frame"), Missed.StartSeconds, 0.583f, WindowTolerance);
	TestEqual(TEXT("A hit opens the combo window 0.05 s earlier"), Confirmed.StartSeconds, 0.533f, WindowTolerance);
	TestEqual(TEXT("A hit keeps the window end"), Confirmed.EndSeconds, 1.417f, WindowTolerance);
	TestEqual(TEXT("Without a lead a hit changes nothing"), Finisher.StartSeconds, 0.583f, WindowTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelApproachStepTest, "TDGame.Combat.HitFeel.Melee.ApproachStep", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelApproachStepTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelMeleeTestSupport;
	TestEqual(TEXT("Inside the ideal range the step is zero"), TDApproachStep::ComputeStepDistance(60.f, false), 0.f);
	TestEqual(TEXT("A touching target needs no step"), TDApproachStep::ComputeStepDistance(-10.f, false), 0.f);
	TestEqual(TEXT("The step closes the gap to the ideal range"), TDApproachStep::ComputeStepDistance(100.f, false), 40.f);
	TestEqual(TEXT("The step is capped at 110 cm"), TDApproachStep::ComputeStepDistance(500.f, false), 110.f);
	TestEqual(TEXT("Bosses allow at most 40 cm"), TDApproachStep::ComputeStepDistance(500.f, true), 40.f);
	TestEqual(TEXT("Bosses keep shorter steps"), TDApproachStep::ComputeStepDistance(80.f, true), 20.f);

	IgnoreEngineInjectionTimelineEnsure(*this);
	FTDHitFeelMeleeTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	Fixture.Tick(0.3f);

	const float PlayerRadius = Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FVector PlayerLocation = Player->GetActorLocation();
	AActor* FarTarget = Fixture.SpawnCapsuleTarget(PlayerLocation + FVector(300.f, 0.f, 0.f));
	AActor* NearTarget = Fixture.SpawnCapsuleTarget(PlayerLocation + FVector(0.f, 60.f + PlayerRadius + TargetCapsuleRadius, 0.f));
	AActor* HiddenTarget = Fixture.SpawnCapsuleTarget(PlayerLocation + FVector(0.f, -300.f, 0.f));
	Fixture.SpawnBox(PlayerLocation + FVector(0.f, -150.f, 0.f), FVector(100.f, 10.f, 150.f));

	const float ExpectedFarStep = TDApproachStep::ComputeStepDistance(300.f - PlayerRadius - TargetCapsuleRadius, false);
	TestEqual(TEXT("The step uses the surface distance"), Player->ComputeApproachStepDistance(*FarTarget), ExpectedFarStep, 0.5f);
	TestEqual(TEXT("A target inside the ideal range gets no step"), Player->ComputeApproachStepDistance(*NearTarget), 0.f, 0.5f);
	TestEqual(TEXT("A target behind a wall gets no step"), Player->ComputeApproachStepDistance(*HiddenTarget), 0.f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelRollCancelTest, "TDGame.Combat.HitFeel.Melee.RollCancel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelRollCancelTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelMeleeTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	FTDHitFeelMeleeTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	UTDSkillComponent* SkillComponent = Player->GetSkillComponent();
	UTDCombatComponent* Combat = Player->GetCombatComponent();
	const FTDCombatActionDefinition* FirstStage = SkillComponent ? SkillComponent->FindActionDefinition(TDGameplayTags::Action_Attack_Primary_01) : nullptr;
	if (!TestNotNull(TEXT("The first sword stage exists"), FirstStage) || !TestNotNull(TEXT("The first stage clip loads"), FirstStage->Action.Animation.LoadSynchronous()))
	{
		return false;
	}
	Fixture.Tick(0.3f);

	AActor* Target = Fixture.SpawnCapsuleTarget(Player->GetActorLocation() + FVector(600.f, 0.f, 0.f));
	if (!TestTrue(TEXT("The first attack starts"), Player->ExecutePrimaryAttack(Target)))
	{
		return false;
	}
	const double AttackStartSeconds = Fixture.World->GetTimeSeconds();
	TestTrue(TEXT("The attack holds the attacking state"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Attacking));
	TestFalse(TEXT("A roll before 0.12 s is neither started nor buffered"), Player->TryStartRoll(FVector::ForwardVector));

	Fixture.Tick(0.15f);
	TestTrue(TEXT("A roll at 0.15 s is buffered"), Player->TryStartRoll(FVector::ForwardVector));
	TestTrue(TEXT("The buffered input is the roll"), SkillComponent->GetBufferedActionTag() == TDGameplayTags::Action_Roll);
	Fixture.Tick(0.25f);
	TestFalse(TEXT("The early roll expired before the combo window"), SkillComponent->GetBufferedActionTag().IsValid());
	TestFalse(TEXT("The expired roll never started"), Player->IsRollPlaying());

	Fixture.Tick(0.05f);
	TestTrue(TEXT("A roll 0.15 s before the branch is buffered"), Player->TryStartRoll(FVector::ForwardVector));
	bool bHasRollStarted = false;
	for (int32 Frame = 0; Frame < 30 && !bHasRollStarted; ++Frame)
	{
		Fixture.Tick(FrameSeconds);
		bHasRollStarted = Combat->HasMatchingGameplayTag(TDGameplayTags::State_Rolling);
	}
	if (!TestTrue(TEXT("The buffered roll fires in the combo window"), bHasRollStarted))
	{
		return false;
	}
	const double RollStartSeconds = Fixture.World->GetTimeSeconds();
	const double BranchSeconds = FrameToSeconds(SwordBranchFrames[0]);
	TestTrue(FString::Printf(TEXT("The buffered roll starts at the branch, not before (%.3f s)"), RollStartSeconds - AttackStartSeconds), RollStartSeconds - AttackStartSeconds >= BranchSeconds - BranchToleranceSeconds);
	TestFalse(TEXT("The roll cancelled the attack"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Attacking));
	TestFalse(TEXT("The roll is not invulnerable on its first frame"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Invulnerable));
	TickUntilElapsed(Fixture, RollStartSeconds, 0.033);
	TestFalse(TEXT("The roll is not invulnerable at 0.033 s"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Invulnerable));
	TickUntilElapsed(Fixture, RollStartSeconds, 0.05);
	TestTrue(TEXT("The roll is invulnerable at 0.05 s"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Invulnerable));
	TickUntilElapsed(Fixture, RollStartSeconds, 0.33);
	TestTrue(TEXT("The roll is invulnerable at 0.33 s"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Invulnerable));
	TickUntilElapsed(Fixture, RollStartSeconds, 0.35);
	TestFalse(TEXT("The roll is vulnerable again at 0.35 s"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Invulnerable));

	TickUntilElapsed(Fixture, RollStartSeconds, TDPlayerRoll::CancelRollLockoutSeconds - 0.05);
	SkillComponent->BeginActionCancelWindow(ETDActionCancelType::Roll);
	TestFalse(TEXT("A cancel roll stays locked 0.85 s after the last cancel roll started"), Player->CanRollCancelCurrentAction());
	SkillComponent->EndActionCancelWindow(ETDActionCancelType::Roll);
	if (Combat->HasMatchingGameplayTag(TDGameplayTags::State_Rolling))
	{
		AddInfo(TEXT("The roll is still playing at 0.85 s, so the locked roll input check was skipped."));
	}
	else
	{
		Combat->AddLooseGameplayTag(TDGameplayTags::State_Attacking);
		SkillComponent->BeginInputBufferWindow();
		TestFalse(TEXT("A roll during the lock is rejected"), Player->TryStartRoll(FVector::ForwardVector));
		TestFalse(TEXT("A roll during the lock is not buffered"), SkillComponent->GetBufferedActionTag().IsValid());
		SkillComponent->EndInputBufferWindow();
		Combat->RemoveLooseGameplayTag(TDGameplayTags::State_Attacking);
	}
	TickUntilElapsed(Fixture, RollStartSeconds, TDPlayerRoll::CancelRollLockoutSeconds + 0.05);
	SkillComponent->BeginActionCancelWindow(ETDActionCancelType::Roll);
	TestTrue(TEXT("The cancel roll lock ends 0.9 s after the cancel roll started"), Player->CanRollCancelCurrentAction());
	SkillComponent->EndActionCancelWindow(ETDActionCancelType::Roll);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelApproachStepMovesTest, "TDGame.Combat.HitFeel.Melee.ApproachStepMoves", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelApproachStepMovesTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelMeleeTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	const float ExpectedStepCm = TDApproachStep::ComputeStepDistance(ApproachSurfaceDistanceCm, false);
	TestEqual(TEXT("A boss target allows at most 40 cm at the same distance"), TDApproachStep::ComputeStepDistance(ApproachSurfaceDistanceCm, true), TDApproachStep::MaxBossStepCm);

	float OpenGroundCm = 0.f;
	if (TestTrue(TEXT("The open ground attack runs"), MeasureApproachDisplacement(ETDApproachScenario::OpenGround, OpenGroundCm)))
	{
		TestEqual(FString::Printf(TEXT("The approach step moves the player %.1f cm in 0.2 s"), OpenGroundCm), OpenGroundCm, ExpectedStepCm, ApproachToleranceCm);
	}
	float LedgeCm = 0.f;
	if (TestTrue(TEXT("The ledge attack runs"), MeasureApproachDisplacement(ETDApproachScenario::TargetOnLedge, LedgeCm)))
	{
		TestTrue(FString::Printf(TEXT("A target on a 60 cm ledge gets no step (%.1f cm)"), LedgeCm), LedgeCm <= StillToleranceCm);
	}
	float RecentlyHitCm = 0.f;
	if (TestTrue(TEXT("The recently hit attack runs"), MeasureApproachDisplacement(ETDApproachScenario::RecentlyHit, RecentlyHitCm)))
	{
		TestTrue(FString::Printf(TEXT("A player hit within 0.3 s gets no step (%.1f cm)"), RecentlyHitCm), RecentlyHitCm <= StillToleranceCm);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelSkillCancelTest, "TDGame.Combat.HitFeel.Melee.SkillCancel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelSkillCancelTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelMeleeTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	FTDHitFeelMeleeTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	UTDSkillComponent* SkillComponent = Player ? Player->GetSkillComponent() : nullptr;
	if (!TestNotNull(TEXT("Player has a skill component"), SkillComponent) || !TestTrue(TEXT("The sword combo clips load"), LoadSwordComboClips(*SkillComponent))
		|| !TestTrue(TEXT("Skill Q gets a direct action"), AddDirectSkillAction(*SkillComponent, TDGameplayTags::Action_Skill_Q)))
	{
		return false;
	}
	UTDCombatComponent* Combat = Player->GetCombatComponent();
	Fixture.Tick(0.3f);

	const float CenterDistance = NearSurfaceDistanceCm + Player->GetCapsuleComponent()->GetScaledCapsuleRadius() + TargetCapsuleRadius;
	AActor* Target = Fixture.SpawnCapsuleTarget(Player->GetActorLocation() + FVector(CenterDistance, 0.f, 0.f));
	if (!TestTrue(TEXT("The first attack starts"), Player->ExecutePrimaryAttack(Target)))
	{
		return false;
	}
	const double AttackStartSeconds = Fixture.World->GetTimeSeconds();
	Fixture.Tick(0.15f);
	TestTrue(TEXT("Q before the branch is buffered"), Player->ExecuteSkillQ(Target));
	TestFalse(TEXT("Q before the branch does not fire directly"), Combat->HasMatchingGameplayTag(TDGameplayTags::Cooldown_Skill_Q));
	TestTrue(TEXT("The attack keeps running after the early Q"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Attacking));

	bool bHasSkillFired = false;
	for (int32 Frame = 0; Frame < 40 && !bHasSkillFired; ++Frame)
	{
		Fixture.Tick(FrameSeconds);
		bHasSkillFired = Combat->HasMatchingGameplayTag(TDGameplayTags::Cooldown_Skill_Q);
	}
	if (!TestTrue(TEXT("The buffered Q fires in the skill cancel window"), bHasSkillFired))
	{
		return false;
	}
	const double SkillElapsedSeconds = Fixture.World->GetTimeSeconds() - AttackStartSeconds;
	const double EarliestSkillSeconds = FrameToSeconds(SwordBranchFrames[0]) - 0.05 - BranchToleranceSeconds;
	TestTrue(FString::Printf(TEXT("The buffered Q waits for the branch (%.3f s)"), SkillElapsedSeconds), SkillElapsedSeconds >= EarliestSkillSeconds);
	TestFalse(TEXT("The Q cancelled the attack"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Attacking));

	Fixture.Tick(0.8f);
	if (!TestTrue(TEXT("A new first attack starts"), Player->ExecutePrimaryAttack(Target)))
	{
		return false;
	}
	for (int32 StageIndex = 1; StageIndex < 4; ++StageIndex)
	{
		Fixture.Tick(0.2f);
		Player->ExecutePrimaryAttack(Target);
		for (int32 Frame = 0; Frame < 90 && SkillComponent->GetActivePrimaryComboStageIndex() != StageIndex; ++Frame)
		{
			Fixture.Tick(FrameSeconds);
		}
	}
	if (!TestEqual(TEXT("The combo reaches the finisher"), SkillComponent->GetActivePrimaryComboStageIndex(), 3))
	{
		return false;
	}
	const double FinisherStartSeconds = Fixture.World->GetTimeSeconds();
	for (int32 Frame = 0; Frame < 90 && !SkillComponent->IsActionCancelWindowOpen(ETDActionCancelType::Roll); ++Frame)
	{
		Fixture.Tick(FrameSeconds);
	}
	if (!TestTrue(TEXT("The finisher opens its roll window"), SkillComponent->IsActionCancelWindowOpen(ETDActionCancelType::Roll)))
	{
		return false;
	}
	const double RollWindowSeconds = Fixture.World->GetTimeSeconds() - FinisherStartSeconds;
	TestTrue(FString::Printf(TEXT("The finisher roll window opens near frame 55 (%.3f s)"), RollWindowSeconds), RollWindowSeconds >= FrameToSeconds(FinisherRollCancelFrame) - BranchToleranceSeconds);
	TestFalse(TEXT("The finisher has no skill cancel window"), SkillComponent->IsActionCancelWindowOpen(ETDActionCancelType::Skill));
	Player->ExecuteSkillQ(Target);
	TestFalse(TEXT("Q is rejected in the finisher roll window"), Combat->HasMatchingGameplayTag(TDGameplayTags::Cooldown_Skill_Q));
	TestTrue(TEXT("The finisher keeps running after Q"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Attacking));
	TestTrue(TEXT("A roll is allowed in the finisher roll window"), Player->TryStartRoll(FVector::ForwardVector));
	TestTrue(TEXT("The finisher roll plays"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Rolling));
	TestFalse(TEXT("The finisher roll cancelled the attack"), Combat->HasMatchingGameplayTag(TDGameplayTags::State_Attacking));
	return true;
}

#endif
