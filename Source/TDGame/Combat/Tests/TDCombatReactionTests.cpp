#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDGameCharacter.h"
#include "Characters/TDKnockbackLayeredMove.h"
#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Combat/Skills/TDCombatStyleDefinition.h"
#include "Combat/Skills/TDPlayerCombatCatalog.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Component/AnimNextComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Core/TDGameplayTags.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace TDCombatReactionTestSupport
{
	constexpr float FrameSeconds = 1.f / 60.f;
	constexpr float FloorHalfThickness = 50.f;
	constexpr float PlayerSpawnHeight = 98.f;
	constexpr float CombatantMaxHealth = 1000.f;
	constexpr float WindowTolerance = 0.002f;
	constexpr float VelocityTolerance = 0.01f;
	constexpr float LargeHitHealthFraction = 0.35f;
	constexpr float SourceContactFramesPerSecond = 60.f;
	constexpr int32 SwordComboContactFrames[] = { 20, 23, 21, 35 };
	const TCHAR* const SwordAttackSequencePath = TEXT("/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordAttack01.AS_TD_Player_SwordAttack01");

	struct FTDSourceComboStage
	{
		double FramesPerSecond = 60.0;
		double LastFrame = 1.0;
		double SwingStartFrame = 1.0;
		double SwingEndFrame = 1.0;
		double BranchFrame = 1.0;
	};

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

	struct FTDReactionTestWorld
	{
		FTDReactionTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDCombatReactionTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			SpawnFloor();
		}

		~FTDReactionTestWorld()
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
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, PlayerSpawnHeight));
			return World->SpawnActor<ATDGameCharacter>(PlayerPawnClass, SpawnTransform);
		}

		UTDSkillComponent* SpawnSkillOwner(const bool bUsesHitReactionThresholds)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Actor->AddInstanceComponent(Sphere);
			Actor->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(10.f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->RegisterComponent();
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = 1;
			Stats.BaseMaxHealth = CombatantMaxHealth;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			UTDSkillComponent* SkillComponent = NewObject<UTDSkillComponent>(Actor);
			Actor->AddInstanceComponent(SkillComponent);
			SkillComponent->SetUsesHitReactionThresholds(bUsesHitReactionThresholds);
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

	void IgnoreEngineInjectionTimelineEnsure(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("TimelineTrait.GetState(Context, ChildState)"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("=== Handled ensure: ==="), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Stack:"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("[Callstack]"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Exact, -1, false);
	}

	FTDDamageResult MakeDamageResult(const float DamageRatio, const bool bWasCritical = false, const ETDDamageDelivery Delivery = ETDDamageDelivery::Direct, const bool bWasKilled = false)
	{
		FTDDamageResult Result;
		Result.AppliedDamage = DamageRatio * CombatantMaxHealth;
		Result.DamageRatio = DamageRatio;
		Result.bWasCritical = bWasCritical;
		Result.Delivery = Delivery;
		Result.bWasKilled = bWasKilled;
		return Result;
	}

	FTDDamageContext MakeDamageContext(const ETDDamageDelivery Delivery)
	{
		FTDDamageContext Context;
		Context.Delivery = Delivery;
		Context.Budget = MakeShared<FTDDamageChainBudget>();
		return Context;
	}

	UTDCombatStyleDefinition* MakeComboStyle(UObject& Outer, const int32 StepCount)
	{
		UTDCombatStyleDefinition* Style = NewObject<UTDCombatStyleDefinition>(&Outer);
		const FMapProperty* ActionMapProperty = FindFProperty<FMapProperty>(UTDCombatStyleDefinition::StaticClass(), TEXT("ActionMap"));
		if (!ActionMapProperty)
		{
			return nullptr;
		}
		TMap<FGameplayTag, FTDCombatActionDefinition>* ActionMap = ActionMapProperty->ContainerPtrToValuePtr<TMap<FGameplayTag, FTDCombatActionDefinition>>(Style);
		const FGameplayTag ComboTags[] =
		{
			TDGameplayTags::Action_Attack_Primary_01,
			TDGameplayTags::Action_Attack_Primary_02,
			TDGameplayTags::Action_Attack_Primary_03,
			TDGameplayTags::Action_Attack_Primary_04
		};
		const int32 ClampedStepCount = FMath::Clamp(StepCount, 0, static_cast<int32>(UE_ARRAY_COUNT(ComboTags)));
		for (int32 StepIndex = 0; StepIndex < ClampedStepCount; ++StepIndex)
		{
			ActionMap->Add(ComboTags[StepIndex], FTDCombatActionDefinition());
		}
		return Style;
	}

	bool ReadSourceComboStage(const int32 Step, FTDSourceComboStage& OutStage)
	{
		const FString SourcePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("AnimationSources/Player"), FString::Printf(TEXT("AS_TD_Player_SwordCombo%02d.json"), Step));
		FString SourceText;
		if (!FFileHelper::LoadFileToString(SourceText, *SourcePath))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceText), Root) || !Root.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
		const TSharedPtr<FJsonObject>* Measure = nullptr;
		const TSharedPtr<FJsonObject>* Author = nullptr;
		if (!Root->TryGetNumberField(TEXT("fps"), OutStage.FramesPerSecond) || OutStage.FramesPerSecond <= 0.0
			|| !Root->TryGetArrayField(TEXT("frames"), Frames) || Frames->Num() < 2
			|| !Root->TryGetObjectField(TEXT("measure"), Measure) || !Root->TryGetObjectField(TEXT("author"), Author))
		{
			return false;
		}
		OutStage.LastFrame = (*Frames)[1]->AsNumber();

		const TArray<TSharedPtr<FJsonValue>>* SwingFrames = nullptr;
		if (!(*Measure)->TryGetArrayField(TEXT("swing_frames_50pct"), SwingFrames) || SwingFrames->Num() < 2)
		{
			return false;
		}
		OutStage.SwingStartFrame = (*SwingFrames)[0]->AsNumber();
		OutStage.SwingEndFrame = (*SwingFrames)[1]->AsNumber();

		const TArray<TSharedPtr<FJsonValue>>* Stages = nullptr;
		if (!(*Author)->TryGetArrayField(TEXT("stages"), Stages) || !Stages->IsValidIndex(Step - 1))
		{
			return false;
		}
		const TSharedPtr<FJsonObject> StageObject = (*Stages)[Step - 1]->AsObject();
		return StageObject.IsValid() && StageObject->TryGetNumberField(TEXT("branch_frame"), OutStage.BranchFrame);
	}

	float SourceFrameToSeconds(const FTDSourceComboStage& Stage, const double Frame)
	{
		return static_cast<float>((Frame - 1.0) / Stage.FramesPerSecond);
	}

	float ContactFrameToSeconds(const int32 Frame)
	{
		return static_cast<float>(Frame - 1) / SourceContactFramesPerSecond;
	}

	bool IsWindowInsideClip(const FTDActionTimeWindow& Window, const float ClipSeconds)
	{
		return Window.StartSeconds >= -WindowTolerance && Window.StartSeconds <= Window.EndSeconds && Window.EndSeconds <= ClipSeconds + WindowTolerance;
	}

	const UAnimSequence* LoadReactionClip(const UTDSkillComponent& SkillComponent, const FGameplayTag ReactionTag)
	{
		const FTDCombatReactionDefinition* Reaction = SkillComponent.FindReactionDefinition(ReactionTag);
		return Reaction ? Reaction->Action.Animation.LoadSynchronous() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDKnockbackScaleDecaysTest, "TDGame.Combat.Reaction.KnockbackScaleDecays", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDKnockbackScaleDecaysTest::RunTest(const FString& Parameters)
{
	constexpr float DurationMs = 220.f;
	TestEqual(TEXT("Knockback starts at full speed"), TDKnockback::ComputeScale(0.f, DurationMs), 1.f, 1e-4f);
	TestEqual(TEXT("Knockback halfway keeps a quarter of the speed"), TDKnockback::ComputeScale(DurationMs * 0.5f, DurationMs), 0.25f, 1e-4f);
	TestEqual(TEXT("Knockback ends at zero speed"), TDKnockback::ComputeScale(DurationMs, DurationMs), 0.f, 1e-4f);
	TestEqual(TEXT("Knockback stays at zero after the duration"), TDKnockback::ComputeScale(DurationMs * 2.f, DurationMs), 0.f, 1e-4f);
	TestEqual(TEXT("Knockback without duration has no speed"), TDKnockback::ComputeScale(10.f, 0.f), 0.f, 1e-4f);

	float PreviousScale = TDKnockback::ComputeScale(0.f, DurationMs);
	bool bIsMonotonic = true;
	for (float ElapsedMs = 10.f; ElapsedMs <= DurationMs; ElapsedMs += 10.f)
	{
		const float Scale = TDKnockback::ComputeScale(ElapsedMs, DurationMs);
		bIsMonotonic &= Scale <= PreviousScale;
		PreviousScale = Scale;
	}
	TestTrue(TEXT("Knockback speed decreases monotonically"), bIsMonotonic);

	const FTDKnockbackLayeredMove KnockbackMove;
	TestTrue(TEXT("Knockback overrides velocity"), KnockbackMove.MixMode == EMoveMixMode::OverrideVelocity);
	TestTrue(TEXT("Knockback outranks root motion layers"), KnockbackMove.Priority > 0);
	TestFalse(TEXT("Knockback runs on the game thread only"), KnockbackMove.SupportsAsync());
	TestTrue(TEXT("Knockback stops the pawn when it ends"), KnockbackMove.FinishVelocitySettings.FinishVelocityMode == ELayeredMoveFinishVelocityMode::SetVelocity);
	const TUniquePtr<FLayeredMoveBase> ClonedMove(KnockbackMove.Clone());
	TestTrue(TEXT("Knockback clones keep the knockback type"), ClonedMove.IsValid() && ClonedMove->GetScriptStruct() == FTDKnockbackLayeredMove::StaticStruct());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDKillVelocityFromHitDirectionTest, "TDGame.Combat.Reaction.KillVelocityFromHitDirection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDKillVelocityFromHitDirectionTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatReactionTestSupport;
	const ATDMonsterCharacter* Monster = GetDefault<ATDMonsterCharacter>();

	FTDDamageResult Result;
	Result.HitDirection = FVector(0.f, 2.f, 0.f);
	Result.DamageRatio = 0.5f;
	FVector KillVelocity = Monster->ComputeKillVelocity(Result);
	TestEqual(TEXT("Kill velocity has no sideways part"), static_cast<float>(KillVelocity.X), 0.f, VelocityTolerance);
	TestEqual(TEXT("Kill velocity grows with the damage ratio"), static_cast<float>(KillVelocity.Y), 410.f, VelocityTolerance);
	TestEqual(TEXT("Kill velocity lifts the body"), static_cast<float>(KillVelocity.Z), 160.f, VelocityTolerance);

	Result.bWasCritical = true;
	KillVelocity = Monster->ComputeKillVelocity(Result);
	TestEqual(TEXT("Critical kills launch faster"), static_cast<float>(KillVelocity.Y), 492.f, VelocityTolerance);

	Result.bWasCritical = false;
	Result.DamageRatio = 3.f;
	KillVelocity = Monster->ComputeKillVelocity(Result);
	TestEqual(TEXT("Damage ratio is clamped to one"), static_cast<float>(KillVelocity.Y), 600.f, VelocityTolerance);

	const float MonsterMaxHealth = Monster->GetCombatComponent() ? Monster->GetCombatComponent()->GetMaxHealth() : 0.f;
	if (TestTrue(TEXT("Monster defaults report a max health"), MonsterMaxHealth > 0.f))
	{
		Result.DamageRatio = 0.05f;
		Result.UnclampedDamage = MonsterMaxHealth * 0.5f;
		KillVelocity = Monster->ComputeKillVelocity(Result);
		TestEqual(TEXT("Overkill damage beyond the remaining health drives the launch speed"), static_cast<float>(KillVelocity.Y), 410.f, VelocityTolerance);
		Result.UnclampedDamage = 0.f;
	}

	Result.DamageRatio = 0.f;
	Result.HitDirection = FVector(3.f, 0.f, 5.f);
	KillVelocity = Monster->ComputeKillVelocity(Result);
	TestEqual(TEXT("Kill direction is planar"), static_cast<float>(KillVelocity.X), 220.f, VelocityTolerance);
	TestEqual(TEXT("Vertical hit direction does not change the lift"), static_cast<float>(KillVelocity.Z), 160.f, VelocityTolerance);

	Result.HitDirection = FVector::ZeroVector;
	KillVelocity = Monster->ComputeKillVelocity(Result);
	const FVector ExpectedPlanarVelocity = (-Monster->GetActorForwardVector()).GetSafeNormal2D() * 220.f;
	TestTrue(TEXT("Missing hit direction pushes the body backward"), FVector(KillVelocity.X, KillVelocity.Y, 0.f).Equals(ExpectedPlanarVelocity, VelocityTolerance));

	Result.HitDirection = FVector(1.f, 0.f, 0.f);
	Result.DamageRatio = 0.5f;
	Result.bWasCritical = true;
	Result.Delivery = ETDDamageDelivery::Status;
	KillVelocity = Monster->ComputeKillVelocity(Result);
	TestTrue(TEXT("Status kills do not push the body sideways"), FVector(KillVelocity.X, KillVelocity.Y, 0.f).IsNearlyZero(VelocityTolerance));
	TestEqual(TEXT("Status kills only lift the body a little"), static_cast<float>(KillVelocity.Z), 48.f, VelocityTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitStopFreezesActionClockTest, "TDGame.Combat.Reaction.HitStopFreezesActionClock", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitStopFreezesActionClockTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatReactionTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	UAnimSequence* Attack = LoadObject<UAnimSequence>(nullptr, SwordAttackSequencePath);
	if (!TestNotNull(TEXT("Sword attack sequence exists"), Attack))
	{
		return false;
	}

	FTDReactionTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	Fixture.Tick(0.5f);

	UTDCharacterAnimationComponent* Animation = Player->GetCharacterAnimation();
	UUAFComponent* AnimationSystem = Player->GetUAFComponent();
	if (!TestNotNull(TEXT("Player has an animation driver"), Animation) || !TestNotNull(TEXT("Player has a UAF system"), AnimationSystem))
	{
		return false;
	}
	if (!TestTrue(TEXT("Action starts"), Animation->PlayAction(Attack, 1.f, 0.f, 0.f, false) > 0.f))
	{
		return false;
	}
	Fixture.Tick(0.1f);
	const float TimeBeforeHitStop = Animation->GetActionAnimationTime();
	TestTrue(TEXT("Action clock advances before the hit stop"), TimeBeforeHitStop > 0.f);

	Animation->StartHitStop(0.2f);
	TestTrue(TEXT("Hit stop starts"), Animation->IsHitStopped());
	Fixture.Tick(0.1f);
	TestEqual(TEXT("Action clock holds during the hit stop"), Animation->GetActionAnimationTime(), TimeBeforeHitStop, 1e-5f);
	TestTrue(TEXT("Action keeps playing during the hit stop"), Animation->IsPlayingAction(Attack));
	TestFalse(TEXT("UAF system pauses during the hit stop"), AnimationSystem->IsActive());
	TestFalse(TEXT("Hit stop does not count as a status freeze"), Animation->IsAnimationFrozen());

	Fixture.Tick(0.15f);
	TestFalse(TEXT("Hit stop expires"), Animation->IsHitStopped());
	TestTrue(TEXT("UAF system resumes after the hit stop"), AnimationSystem->IsActive());
	const float TimeAfterHitStop = Animation->GetActionAnimationTime();
	Fixture.Tick(0.05f);
	TestTrue(TEXT("Action clock resumes after the hit stop"), Animation->GetActionAnimationTime() > TimeAfterHitStop);

	Animation->SetAnimationFrozen(true);
	Animation->StartHitStop(0.2f);
	TestFalse(TEXT("Status freeze ignores new hit stops"), Animation->IsHitStopped());
	Animation->SetAnimationFrozen(false);
	TestTrue(TEXT("UAF system resumes after the status freeze"), AnimationSystem->IsActive());

	Animation->StartHitStop(0.5f);
	Animation->SetAnimationFrozen(true);
	TestFalse(TEXT("Status freeze ends a running hit stop"), Animation->IsHitStopped());
	TestTrue(TEXT("Status freeze holds the pose"), Animation->IsAnimationFrozen());
	Animation->SetAnimationFrozen(false);

	Animation->StartHitStop(0.5f);
	TestTrue(TEXT("Hit stop restarts"), Animation->IsHitStopped());
	TestTrue(TEXT("Actions may start during a hit stop"), Animation->PlayAction(Attack, 1.f, 0.f, 0.f, false) > 0.f);
	TestFalse(TEXT("Starting an action ends the hit stop"), Animation->IsHitStopped());
	Fixture.Tick(0.1f);
	TestTrue(TEXT("UAF system runs the new action"), AnimationSystem->IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDComboWrapsToFirstStepTest, "TDGame.Combat.Reaction.ComboWrapsToFirstStep", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDComboWrapsToFirstStepTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatReactionTestSupport;
	FTDReactionTestWorld Fixture;
	UTDSkillComponent* SkillComponent = Fixture.SpawnSkillOwner(true);
	UTDCombatComponent* Combat = FindCombatComponent(SkillComponent);
	if (!TestNotNull(TEXT("Skill owner spawns"), SkillComponent) || !TestNotNull(TEXT("Skill owner has combat"), Combat))
	{
		return false;
	}
	UTDCombatStyleDefinition* ThreeStepStyle = MakeComboStyle(*SkillComponent->GetOwner(), 3);
	UTDCombatStyleDefinition* FourStepStyle = MakeComboStyle(*SkillComponent->GetOwner(), 4);
	if (!TestNotNull(TEXT("Three step style builds"), ThreeStepStyle) || !TestNotNull(TEXT("Four step style builds"), FourStepStyle))
	{
		return false;
	}

	SkillComponent->SetSkillSet(ThreeStepStyle);
	SkillComponent->SetPrimaryComboSettings(4, 1.6f);
	Combat->AddLooseGameplayTag(TDGameplayTags::State_ComboWindow);
	SkillComponent->NotifyPrimaryActionActivated(3, TDGameplayTags::Action_Attack_Primary_03);
	int32 ComboStep = 0;
	FGameplayTag ActionTag = SkillComponent->ResolvePrimaryActionTag(ComboStep);
	TestTrue(TEXT("A missing next step wraps to the first attack"), ActionTag == TDGameplayTags::Action_Attack_Primary_01);
	TestEqual(TEXT("The wrapped combo restarts at step one"), ComboStep, 1);

	SkillComponent->SetSkillSet(FourStepStyle);
	ActionTag = SkillComponent->ResolvePrimaryActionTag(ComboStep);
	TestTrue(TEXT("The fourth step follows the third"), ActionTag == TDGameplayTags::Action_Attack_Primary_04);
	TestEqual(TEXT("The fourth step reports step four"), ComboStep, 4);

	SkillComponent->NotifyPrimaryActionActivated(4, TDGameplayTags::Action_Attack_Primary_04);
	ActionTag = SkillComponent->ResolvePrimaryActionTag(ComboStep);
	TestTrue(TEXT("The last step wraps to the first attack"), ActionTag == TDGameplayTags::Action_Attack_Primary_01);
	TestEqual(TEXT("The combo restarts at step one after the last step"), ComboStep, 1);

	Combat->RemoveLooseGameplayTag(TDGameplayTags::State_ComboWindow);
	TestEqual(TEXT("Closing the combo window outside an attack resets the combo"), SkillComponent->GetPrimaryComboStep(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitReactionThresholdsOnlyWhenEnabledTest, "TDGame.Combat.Reaction.HitReactionThresholdsOnlyWhenEnabled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitReactionThresholdsOnlyWhenEnabledTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatReactionTestSupport;
	UTDSkillComponent* LegacySkill = NewObject<UTDSkillComponent>(GetTransientPackage());
	UTDSkillComponent* ThresholdSkill = NewObject<UTDSkillComponent>(GetTransientPackage());
	ThresholdSkill->SetUsesHitReactionThresholds(true);

	TestFalse(TEXT("Thresholds are off by default"), LegacySkill->UsesHitReactionThresholds());
	TestTrue(TEXT("Without thresholds a tiny hit still reacts"), LegacySkill->ResolveHitReactionTag(MakeDamageResult(0.01f)) == TDGameplayTags::Action_Reaction_Hit);
	TestTrue(TEXT("Without thresholds status damage still reacts"), LegacySkill->ResolveHitReactionTag(MakeDamageResult(0.5f, false, ETDDamageDelivery::Status)) == TDGameplayTags::Action_Reaction_Hit);
	TestFalse(TEXT("No damage never reacts"), LegacySkill->ResolveHitReactionTag(MakeDamageResult(0.f)).IsValid());

	TestFalse(TEXT("Small hits do not react"), ThresholdSkill->ResolveHitReactionTag(MakeDamageResult(0.05f)).IsValid());
	TestFalse(TEXT("Small critical hits do not react"), ThresholdSkill->ResolveHitReactionTag(MakeDamageResult(0.05f, true)).IsValid());
	TestFalse(TEXT("Status damage does not react"), ThresholdSkill->ResolveHitReactionTag(MakeDamageResult(0.5f, false, ETDDamageDelivery::Status)).IsValid());
	TestFalse(TEXT("Killing blows leave the reaction to death handling"), ThresholdSkill->ResolveHitReactionTag(MakeDamageResult(0.5f, false, ETDDamageDelivery::Direct, true)).IsValid());
	TestTrue(TEXT("Medium hits play the light reaction"), ThresholdSkill->ResolveHitReactionTag(MakeDamageResult(0.1f)) == TDGameplayTags::Action_Reaction_Hit);
	TestTrue(TEXT("Large hits play the heavy reaction"), ThresholdSkill->ResolveHitReactionTag(MakeDamageResult(0.2f)) == TDGameplayTags::Action_Reaction_HeavyHit);
	TestTrue(TEXT("Critical medium hits play the heavy reaction"), ThresholdSkill->ResolveHitReactionTag(MakeDamageResult(0.09f, true)) == TDGameplayTags::Action_Reaction_HeavyHit);
	TestTrue(TEXT("Melee delivery uses the same thresholds"), ThresholdSkill->ResolveHitReactionTag(MakeDamageResult(0.1f, false, ETDDamageDelivery::Melee)) == TDGameplayTags::Action_Reaction_Hit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDStatusDamageKeepsComboTest, "TDGame.Combat.Reaction.StatusDamageKeepsCombo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDStatusDamageKeepsComboTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatReactionTestSupport;
	FTDReactionTestWorld Fixture;
	UTDSkillComponent* SkillComponent = Fixture.SpawnSkillOwner(true);
	UTDCombatComponent* Combat = FindCombatComponent(SkillComponent);
	if (!TestNotNull(TEXT("Skill owner spawns"), SkillComponent) || !TestNotNull(TEXT("Skill owner has combat"), Combat))
	{
		return false;
	}

	SkillComponent->NotifyPrimaryActionActivated(2, TDGameplayTags::Action_Attack_Primary_02);
	const float HealthBefore = Combat->GetCurrentHealth();
	const FTDDamageResult Result = Combat->ReceiveDamage(300.f, ETDDamageElement::Physical, false, MakeDamageContext(ETDDamageDelivery::Status));
	TestTrue(TEXT("Status damage is applied"), Combat->GetCurrentHealth() < HealthBefore);
	TestTrue(TEXT("Status damage keeps its delivery"), Result.Delivery == ETDDamageDelivery::Status);
	TestEqual(TEXT("Status damage keeps the combo"), SkillComponent->GetPrimaryComboStep(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSmallHitKeepsComboTest, "TDGame.Combat.Reaction.SmallHitKeepsCombo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDSmallHitKeepsComboTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatReactionTestSupport;
	FTDReactionTestWorld Fixture;
	UTDSkillComponent* SkillComponent = Fixture.SpawnSkillOwner(true);
	UTDCombatComponent* Combat = FindCombatComponent(SkillComponent);
	if (!TestNotNull(TEXT("Skill owner spawns"), SkillComponent) || !TestNotNull(TEXT("Skill owner has combat"), Combat))
	{
		return false;
	}

	SkillComponent->NotifyPrimaryActionActivated(2, TDGameplayTags::Action_Attack_Primary_02);
	const FTDDamageResult Result = Combat->ReceiveDamage(50.f, ETDDamageElement::Physical, false, MakeDamageContext(ETDDamageDelivery::Direct));
	TestTrue(TEXT("Small hit is applied"), Result.AppliedDamage > 0.f);
	TestEqual(TEXT("Small hit reports its damage ratio"), Result.DamageRatio, 0.05f, 0.001f);
	TestEqual(TEXT("Small hit keeps the combo"), SkillComponent->GetPrimaryComboStep(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDLargeHitPlaysReactionTest, "TDGame.Combat.Reaction.LargeHitPlaysReaction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDLargeHitPlaysReactionTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatReactionTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	FTDReactionTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	UTDSkillComponent* PlayerSkill = Player->GetSkillComponent();
	UTDCombatComponent* PlayerCombat = Player->GetCombatComponent();
	UTDCharacterAnimationComponent* PlayerAnimation = Player->GetCharacterAnimation();
	if (!TestNotNull(TEXT("Player has a skill component"), PlayerSkill) || !TestNotNull(TEXT("Player has combat"), PlayerCombat) || !TestNotNull(TEXT("Player has an animation driver"), PlayerAnimation))
	{
		return false;
	}
	const UAnimSequence* LightReactionClip = LoadReactionClip(*PlayerSkill, TDGameplayTags::Action_Reaction_Hit);
	const UAnimSequence* HeavyReactionClip = LoadReactionClip(*PlayerSkill, TDGameplayTags::Action_Reaction_HeavyHit);
	if (!TestNotNull(TEXT("The light hit reaction clip loads"), LightReactionClip) || !TestNotNull(TEXT("The heavy hit reaction clip loads"), HeavyReactionClip))
	{
		return false;
	}
	Fixture.Tick(0.5f);

	PlayerSkill->NotifyPrimaryActionActivated(2, TDGameplayTags::Action_Attack_Primary_02);
	const FTDDamageResult LargeResult = PlayerCombat->ReceiveDamage(PlayerCombat->GetMaxHealth() * LargeHitHealthFraction, ETDDamageElement::Physical, false, MakeDamageContext(ETDDamageDelivery::Melee));
	const FGameplayTag ReactionTag = PlayerSkill->ResolveHitReactionTag(LargeResult);
	if (!TestTrue(TEXT("Large hit chooses a hit reaction"), ReactionTag.IsValid()))
	{
		return false;
	}
	const UAnimSequence* ExpectedReactionClip = ReactionTag == TDGameplayTags::Action_Reaction_HeavyHit ? HeavyReactionClip : LightReactionClip;
	TestTrue(TEXT("Large hit plays the chosen reaction clip"), PlayerAnimation->IsPlayingAction(ExpectedReactionClip));
	TestTrue(TEXT("The reaction ability holds the stunned state"), PlayerCombat->HasMatchingGameplayTag(TDGameplayTags::State_Stunned));
	TestEqual(TEXT("The started reaction interrupts the combo"), PlayerSkill->GetPrimaryComboStep(), 0);

	UTDSkillComponent* ThresholdSkill = Fixture.SpawnSkillOwner(true);
	UTDSkillComponent* LegacySkill = Fixture.SpawnSkillOwner(false);
	UTDCombatComponent* ThresholdCombat = FindCombatComponent(ThresholdSkill);
	UTDCombatComponent* LegacyCombat = FindCombatComponent(LegacySkill);
	if (!TestNotNull(TEXT("Threshold owner has combat"), ThresholdCombat) || !TestNotNull(TEXT("Legacy owner has combat"), LegacyCombat))
	{
		return false;
	}

	ThresholdSkill->NotifyPrimaryActionActivated(2, TDGameplayTags::Action_Attack_Primary_02);
	const FTDDamageResult UnplayableResult = ThresholdCombat->ReceiveDamage(200.f, ETDDamageElement::Physical, false, MakeDamageContext(ETDDamageDelivery::Melee));
	TestTrue(TEXT("Large hit chooses the heavy reaction"), ThresholdSkill->ResolveHitReactionTag(UnplayableResult) == TDGameplayTags::Action_Reaction_HeavyHit);
	TestEqual(TEXT("A large hit without a reaction to play keeps the combo"), ThresholdSkill->GetPrimaryComboStep(), 2);

	LegacySkill->NotifyPrimaryActionActivated(2, TDGameplayTags::Action_Attack_Primary_02);
	LegacyCombat->ReceiveDamage(10.f, ETDDamageElement::Physical, false, MakeDamageContext(ETDDamageDelivery::Direct));
	TestEqual(TEXT("Without thresholds a hit without a reaction to play keeps the combo"), LegacySkill->GetPrimaryComboStep(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPlayerDeathWithoutGameInstanceIsSafeTest, "TDGame.Combat.Reaction.PlayerDeathWithoutGameInstanceIsSafe", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPlayerDeathWithoutGameInstanceIsSafeTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatReactionTestSupport;
	IgnoreEngineInjectionTimelineEnsure(*this);
	int32 DeathCount = 0;
	FTDReactionTestWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	Fixture.Tick(0.3f);

	UTDCombatComponent* Combat = Player->GetCombatComponent();
	const FDelegateHandle DeathHandle = Combat->OnDeath.AddLambda([&DeathCount](const FTDDamageContext&)
	{
		++DeathCount;
	});
	const FTDDamageResult Result = Combat->ReceiveDamage(100000.f, ETDDamageElement::Physical, false, MakeDamageContext(ETDDamageDelivery::Direct));
	TestTrue(TEXT("The blow kills the player"), Result.bWasKilled);
	TestFalse(TEXT("The player is dead"), Combat->IsAlive());
	TestEqual(TEXT("Death is reported once"), DeathCount, 1);
	TestTrue(TEXT("Dead players stop moving"), Player->IsMovementDisabled());

	if (Player->GetMesh()->GetPhysicsAsset())
	{
		TestTrue(TEXT("The player falls as a ragdoll"), Player->IsRagdolling());
		TestTrue(TEXT("The ragdoll simulates physics"), Player->GetMesh()->IsSimulatingPhysics());
		const TArray<FBodyInstance*>& RagdollBodies = Player->GetMesh()->Bodies;
		TestTrue(TEXT("Every ragdoll body damps its spin so the corpse can settle"), RagdollBodies.Num() > 0 && !RagdollBodies.ContainsByPredicate([](const FBodyInstance* Body)
		{
			return !Body || Body->AngularDamping < 0.5f;
		}));
		UTDCharacterAnimationComponent* Animation = Player->GetCharacterAnimation();
		if (TestNotNull(TEXT("The dead player has an animation driver"), Animation))
		{
			TestTrue(TEXT("The ragdoll holds the pose for death"), Animation->IsPoseHeldByDeath());
			Animation->SetAnimationFrozen(true);
			Animation->SetAnimationFrozen(false);
			TestFalse(TEXT("Thawing a ragdoll keeps its animation system paused"), Player->GetUAFComponent()->IsActive());
		}
	}
	else
	{
		AddWarning(TEXT("The player mesh has no physics asset; the ragdoll checks were skipped."));
	}

	Fixture.Tick(0.3f);
	TestTrue(TEXT("The dead player stays valid without a game instance"), IsValid(Player));
	Combat->OnDeath.Remove(DeathHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSwordStyleCatalogIsConsistentTest, "TDGame.Combat.Reaction.SwordStyleCatalogIsConsistent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDSwordStyleCatalogIsConsistentTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatReactionTestSupport;
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
	const FName SwordGripSocketName(TEXT("HandGrip_R"));
	for (int32 StepIndex = 0; StepIndex < static_cast<int32>(UE_ARRAY_COUNT(ComboTags)); ++StepIndex)
	{
		const int32 Step = StepIndex + 1;
		const FTDCombatActionDefinition* Definition = Style->FindAction(ComboTags[StepIndex]);
		if (!Definition)
		{
			AddError(FString::Printf(TEXT("Sword combo %d clip is not imported, so the catalog has no entry for it."), Step));
			continue;
		}

		const FTDActionAnimation& Action = Definition->Action;
		const UAnimSequence* Clip = Action.Animation.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("Combo %d clip loads"), Step), Clip))
		{
			continue;
		}
		const float ClipLengthSeconds = Clip->GetPlayLength();
		TestTrue(FString::Printf(TEXT("Combo %d uses root motion"), Step), Action.bUseRootMotion);
		if (!TestEqual(FString::Printf(TEXT("Combo %d has one sword sweep"), Step), Action.HitWindows.Num(), 1))
		{
			continue;
		}
		const FTDActionTimeWindow& HitWindow = Action.HitWindows[0].Window;
		const FTDMeleeSweepSettings& Sweep = Action.HitWindows[0].Sweep;
		TestTrue(FString::Printf(TEXT("Combo %d hit window is valid"), Step), HitWindow.IsValid());
		TestTrue(FString::Printf(TEXT("Combo %d input buffer is valid"), Step), Action.InputBufferWindow.IsValid());
		TestTrue(FString::Printf(TEXT("Combo %d hit window fits inside the clip"), Step), IsWindowInsideClip(HitWindow, ClipLengthSeconds));
		TestTrue(FString::Printf(TEXT("Combo %d input buffer fits inside the clip"), Step), IsWindowInsideClip(Action.InputBufferWindow, ClipLengthSeconds));
		const float ContactSeconds = ContactFrameToSeconds(SwordComboContactFrames[StepIndex]);
		TestTrue(FString::Printf(TEXT("Combo %d hit window covers the blade contact frame"), Step), HitWindow.StartSeconds <= ContactSeconds && ContactSeconds <= HitWindow.EndSeconds);
		TestTrue(FString::Printf(TEXT("Combo %d sweeps from the sword grip"), Step), Sweep.WeaponBaseSocketName == SwordGripSocketName && Sweep.WeaponTipSocketName == SwordGripSocketName);
		TestFalse(FString::Printf(TEXT("Combo %d sweep reaches past the grip"), Step), Sweep.TipLocalOffset.IsNearlyZero());
		TestEqual(FString::Printf(TEXT("Combo %d has one hit rule"), Step), Sweep.HitRules.Num(), 1);

		FTDSourceComboStage Source;
		if (!ReadSourceComboStage(Step, Source))
		{
			AddError(FString::Printf(TEXT("AnimationSources JSON for sword combo %d is missing or unreadable."), Step));
			continue;
		}
		const float ClipSeconds = SourceFrameToSeconds(Source, Source.LastFrame);
		const float SwingStartSeconds = SourceFrameToSeconds(Source, Source.SwingStartFrame);
		const float SwingEndSeconds = SourceFrameToSeconds(Source, Source.SwingEndFrame);
		const float BranchSeconds = SourceFrameToSeconds(Source, Source.BranchFrame);
		TestEqual(FString::Printf(TEXT("Combo %d hit window opens just before the swing"), Step), HitWindow.StartSeconds, FMath::Max(SwingStartSeconds - 0.03f, 0.f), WindowTolerance);
		TestEqual(FString::Printf(TEXT("Combo %d hit window closes just after the swing"), Step), HitWindow.EndSeconds, FMath::Min(SwingEndSeconds + 0.05f, ClipSeconds), WindowTolerance);
		TestEqual(FString::Printf(TEXT("Combo %d input buffer opens at the swing end"), Step), Action.InputBufferWindow.StartSeconds, SwingEndSeconds, WindowTolerance);
		TestEqual(FString::Printf(TEXT("Combo %d input buffer lasts to the clip end"), Step), Action.InputBufferWindow.EndSeconds, ClipSeconds, WindowTolerance);

		const FTDTagWindow* ComboWindow = Action.TagWindows.FindByPredicate([](const FTDTagWindow& TagWindow)
		{
			return TagWindow.Tags.HasTagExact(TDGameplayTags::State_ComboWindow);
		});
		if (BranchSeconds >= ClipSeconds - WindowTolerance)
		{
			TestNull(FString::Printf(TEXT("Combo %d ends the combo"), Step), ComboWindow);
			continue;
		}
		if (!TestNotNull(FString::Printf(TEXT("Combo %d opens a combo window"), Step), ComboWindow))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("Combo %d combo window fits inside the clip"), Step), IsWindowInsideClip(ComboWindow->Window, ClipLengthSeconds));
		TestEqual(FString::Printf(TEXT("Combo %d combo window opens at the branch frame"), Step), ComboWindow->Window.StartSeconds, BranchSeconds, WindowTolerance);
		TestEqual(FString::Printf(TEXT("Combo %d combo window lasts to the clip end"), Step), ComboWindow->Window.EndSeconds, ClipSeconds, WindowTolerance);
	}
	return true;
}

#endif
