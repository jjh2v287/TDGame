#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDCombatCharacter.h"
#include "Characters/TDGameCharacter.h"
#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"
#include "Combat/Feedback/TDDeathLaunchRules.h"
#include "Combat/GAS/TDCombatAttributeSet.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Combat/Skills/TDCombatStyleDefinition.h"
#include "Combat/Skills/TDPlayerCombatCatalog.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TDGameplayTags.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/TDGameGameMode.h"
#include "Framework/TDGamePlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MonsterAI/TDMonsterThinkSubsystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/TDPieWaitUntilCommand.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* const TDCombatPieMapPackagePath = TEXT("/Game/Combat/Maps/LV_TDMegaMagicArena");
	const TCHAR* const TDCombatPieGoblinSpeciesPath = TEXT("/Game/MonsterAI/Species/DA_TDMonster_Goblin.DA_TDMonster_Goblin");
	const TCHAR* const TDCombatPieOutputFolder = TEXT("AgentOps/20261018/combat-full/pie");
	const TCHAR* const TDCombatPieReportFileName = TEXT("combat-pie.json");
	const TCHAR* const TDCombatPiePlayerHitsScreenshot = TEXT("01_player_hits.png");
	const TCHAR* const TDCombatPieMonsterRagdollScreenshot = TEXT("02_monster_ragdoll.png");
	const TCHAR* const TDCombatPiePlayerDamagedScreenshot = TEXT("03_player_damaged.png");
	const TCHAR* const TDCombatPiePlayerDeadScreenshot = TEXT("04_player_dead.png");
	const TCHAR* const TDCombatPieAttackOnClickPropertyName = TEXT("bAttackHostileOnClick");
	const TCHAR* const TDCombatPieTimelineEnsureExpression = TEXT("TimelineTrait.GetState(Context, ChildState)");
	const TCHAR* const TDCombatPieEnsureHeadingPattern = TEXT("^LogOutputDevice: === Handled ensure: ===\\s*$");
	const TCHAR* const TDCombatPieEnsureBlankLinePattern = TEXT("^LogOutputDevice:\\s*$");
	const TCHAR* const TDCombatPieEnsureStackHeadingPattern = TEXT("^LogOutputDevice: Stack:\\s*$");
	const TCHAR* const TDCombatPieEnsureCallstackLinePattern = TEXT("^LogOutputDevice: \\[Callstack\\] 0x");
	const FName TDCombatPiePelvisBoneName(TEXT("pelvis"));
	const FName TDCombatPiePoseBoneNames[] = { FName(TEXT("hand_l")), FName(TEXT("hand_r")), FName(TEXT("head")) };
	const FVector TDCombatPieNavigationExtent(50.0, 50.0, 250.0);

	constexpr double TDCombatPieSetupTimeoutSeconds = 20.0;
	constexpr double TDCombatPieNavigationTimeoutSeconds = 180.0;
	constexpr double TDCombatPieNavigationCheckIntervalSeconds = 1.0;
	constexpr int32 TDCombatPieNavigationRequiredPasses = 2;
	constexpr double TDCombatPieAssetLoadTimeoutSeconds = 20.0;
	constexpr int32 TDCombatPiePlayerCombatLevel = 10;
	constexpr float TDCombatPieMonsterSpawnDistanceCm = 220.f;
	constexpr float TDCombatPieMonsterSettleSeconds = 1.f;
	constexpr double TDCombatPieCursorTraceTimeoutSeconds = 3.0;
	constexpr double TDCombatPieAttackIntervalSeconds = 0.35;
	constexpr double TDCombatPieKillTimeoutSeconds = 40.0;
	constexpr int32 TDCombatPieMinimumComboStepReached = 2;
	constexpr double TDCombatPieRagdollStartSlackSeconds = 0.1;
	constexpr float TDCombatPieKillHoldFallbackWaveDistanceCm = 400.f;
	constexpr double TDCombatPieRagdollStartTimeoutSeconds = 1.5;
	constexpr double TDCombatPieRagdollObserveSeconds = 2.0;
	constexpr double TDCombatPieRagdollObserveTimeoutSeconds = 15.0;
	constexpr double TDCombatPieRagdollScreenshotDelaySeconds = 0.5;
	constexpr double TDCombatPieRagdollMinimumPoseChangeCm = 3.0;
	constexpr int32 TDCombatPieRagdollFallbackPoseBoneLimit = 3;
	constexpr double TDCombatPieArenaMaxHorizontalDriftCm = 600.0;
	constexpr double TDCombatPieArenaFloorToleranceCm = 50.0;
	constexpr double TDCombatPieMonsterAttackTimeoutSeconds = 25.0;
	constexpr float TDCombatPieFeedbackSettleSeconds = 0.3f;
	constexpr float TDCombatPieReactionDamageRatio = 0.2f;
	constexpr float TDCombatPieReactionMinimumDamageRatio = 0.18f;
	constexpr float TDCombatPieReactionHealthMargin = 1.5f;
	constexpr double TDCombatPieReactionStartLimitSeconds = 0.1;
	constexpr double TDCombatPieReactionObserveSeconds = 0.4;
	constexpr double TDCombatPieReactionObserveTimeoutSeconds = 5.0;
	constexpr double TDCombatPieReactionMinimumDisplacementCm = 20.0;
	constexpr double TDCombatPiePlayerRagdollTimeoutSeconds = 2.0;
	constexpr float TDCombatPieDeathScreenFadeSeconds = 0.6f;
	constexpr double TDCombatPieRespawnExtraSeconds = 3.0;
	constexpr double TDCombatPieRespawnTimeoutSeconds = 60.0;
	constexpr float TDCombatPieFallbackRespawnDelaySeconds = 4.f;
	constexpr double TDCombatPieScreenshotTimeoutSeconds = 5.0;
	constexpr float TDCombatPieLethalDamage = 1000000.f;
	constexpr float TDCombatPieProbeDamage = 50.f;
	constexpr float TDCombatPieHealthTolerance = 0.01f;
	constexpr float TDCombatPieWindowToleranceSeconds = 1.f / 60.f;
	constexpr int32 TDCombatPieHitRowLimit = 40;
	constexpr int32 TDCombatPieActionSequenceLimit = 40;
	constexpr int32 TDCombatPieMonsterDebugLineLimit = 8;

	struct FTDCombatPieTestState
	{
		bool bIsAborted = false;
		TWeakObjectPtr<ATDGamePlayerController> Controller;
		TWeakObjectPtr<ATDGameCharacter> Player;
		TWeakObjectPtr<UTDCombatFeedbackSubsystem> Feedback;
		TWeakObjectPtr<ATDMonsterCharacter> Monster;
		TStrongObjectPtr<UTDCombatStyleDefinition> CatalogStyle;
		TSharedPtr<FStreamableHandle> AssetHandle;
		TSet<FString> RequestedScreenshots;
		FTDCombatFeedbackStats StatsBefore;

		int32 NavigationPassCount = 0;
		double LastNavigationCheckSeconds = -1.0;

		bool bIsAttackHostileOnClick = false;
		bool bDidCursorTraceHitMonster = false;
		int32 CursorTraceAttempts = 0;
		FVector2D CursorScreenPosition = FVector2D::ZeroVector;
		FString CursorTraceHitActorName;
		FString CursorTraceHitComponentName;

		TWeakObjectPtr<UTDCombatComponent> ObservedCombat;
		FDelegateHandle DamageObserverHandle;

		FVector FirstMonsterSpawnLocation = FVector::ZeroVector;
		double FirstMonsterFloorZ = 0.0;
		float MonsterStartHealth = 0.f;
		float MonsterLowestHealth = 0.f;
		double LastAttackSeconds = -1.0;
		int32 AttackCommandCount = 0;
		int32 AttackedCommandCount = 0;
		int32 ApproachingCommandCount = 0;
		int32 RejectedCommandCount = 0;
		int32 SampledHighestComboStep = 0;
		FGameplayTag LastSampledActionTag;
		TArray<TSharedPtr<FJsonValue>> ActionTagSequence;
		int32 MonsterHitCount = 0;
		int32 MeleeHitsFromPlayer = 0;
		int32 MeleeHitsFromPlayerWithLocation = 0;
		TArray<TSharedPtr<FJsonValue>> MonsterHitRows;
		double PhaseStartSeconds = 0.0;
		double MonsterDeathSeconds = -1.0;
		double MonsterKillHoldSeconds = -1.0;
		FVector MonsterDeathProbeLocation = FVector::ZeroVector;
		bool bHasMonsterDeathProbeLocation = false;
		bool bWasMonsterKilledByMelee = false;
		bool bWasMonsterKillForced = false;

		bool bHasRagdollStarted = false;
		bool bWasRagdollingAtStart = false;
		double RagdollStartSeconds = -1.0;
		FName RagdollBoneName;
		FVector RagdollStartLocation = FVector::ZeroVector;
		FVector RagdollLastProbeLocation = FVector::ZeroVector;
		TArray<FName> RagdollPoseBoneNames;
		TArray<FVector> RagdollPoseStartOffsets;
		double RagdollMaxPoseChangeCm = 0.0;
		double RagdollMaxHorizontalTravelCm = 0.0;
		double RagdollMaxVerticalTravelCm = 0.0;
		bool bWasRagdollObservationCut = false;

		float PlayerStartHealth = 0.f;
		float PlayerDamagedHealth = 0.f;
		double PlayerDamageSeconds = -1.0;
		bool bWasPlayerDamagedByMonster = false;
		int32 PlayerHitCount = 0;
		int32 PlayerHitsFromMonster = 0;
		FString FirstPlayerHitCasterName;
		FString FirstPlayerHitDelivery;
		TArray<TSharedPtr<FJsonValue>> PlayerHitRows;

		bool bWasReactionProbeRun = false;
		bool bWasReactionHealthReset = false;
		bool bWasPlayingActionBeforeReaction = false;
		TArray<TWeakObjectPtr<const UAnimSequence>> ReactionClips;
		FVector ReactionStartLocation = FVector::ZeroVector;
		double ReactionStartSeconds = -1.0;
		double ReactionFirstPlayingSeconds = -1.0;
		double ReactionMaxPlanarDisplacementCm = 0.0;
		float ReactionRawDamage = 0.f;
		float ReactionAppliedDamage = 0.f;
		float ReactionDamageRatio = 0.f;
		FString ReactionActionTag;

		float InvulnerableHealthBefore = 0.f;
		float InvulnerableHealthAfter = 0.f;
		float InvulnerableAppliedDamage = 0.f;
		bool bWasInvulnerableProbeRun = false;

		TWeakObjectPtr<ATDGameCharacter> DeadPlayer;
		bool bWasPlayerKilled = false;
		double PlayerDeathSeconds = -1.0;
		bool bHasPlayerRagdollStarted = false;
		bool bWasPlayerRagdollingAtStart = false;
		double PlayerRagdollSeconds = -1.0;
		float RespawnDelaySeconds = TDCombatPieFallbackRespawnDelaySeconds;
		bool bHasRespawned = false;
		double RespawnSeconds = -1.0;

		TArray<TSharedPtr<FJsonValue>> TimedOutSteps;
		TSharedRef<FJsonObject> TimeoutSnapshots = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> ScreenshotReport = MakeShared<FJsonObject>();
	};

	FString MakeCombatPieOutputPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TDCombatPieOutputFolder / FileName);
	}

	void WriteCombatPieReport(FTDCombatPieTestState& State)
	{
		State.Report->SetBoolField(TEXT("aborted"), State.bIsAborted);
		State.Report->SetObjectField(TEXT("screenshots"), State.ScreenshotReport);
		FString ReportText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportText);
		if (!FJsonSerializer::Serialize(State.Report, Writer))
		{
			return;
		}
		FFileHelper::SaveStringToFile(ReportText, *MakeCombatPieOutputPath(TDCombatPieReportFileName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void RequestCombatPieScreenshot(FTDCombatPieTestState& State, const FString& FileName)
	{
		if (State.RequestedScreenshots.Contains(FileName))
		{
			return;
		}
		const FString ScreenshotPath = MakeCombatPieOutputPath(FileName);
		IFileManager::Get().Delete(*ScreenshotPath, false, true, true);
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false, false, FIntRect(), true);
		State.RequestedScreenshots.Add(FileName);
	}

	TSharedRef<FJsonObject> MakeFeedbackStatsDeltaJson(const FTDCombatFeedbackStats& Before, const FTDCombatFeedbackStats& After)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("numbers_shown"), After.NumbersShown - Before.NumbersShown);
		Json->SetNumberField(TEXT("player_damage_numbers_shown"), After.PlayerDamageNumbersShown - Before.PlayerDamageNumbersShown);
		Json->SetNumberField(TEXT("effects_requested"), After.EffectsRequested - Before.EffectsRequested);
		Json->SetNumberField(TEXT("effects_spawned"), After.EffectsSpawned - Before.EffectsSpawned);
		Json->SetNumberField(TEXT("effects_skipped_not_loaded"), After.EffectsSkippedNotLoaded - Before.EffectsSkippedNotLoaded);
		Json->SetNumberField(TEXT("effects_skipped_frame_cap"), After.EffectsSkippedFrameCap - Before.EffectsSkippedFrameCap);
		Json->SetNumberField(TEXT("effects_spawn_failed"), After.EffectsSpawnFailed - Before.EffectsSpawnFailed);
		Json->SetNumberField(TEXT("monster_hits_seen"), After.MonsterHitsSeen - Before.MonsterHitsSeen);
		Json->SetNumberField(TEXT("monster_hit_effects_spawned"), After.MonsterHitEffectsSpawned - Before.MonsterHitEffectsSpawned);
		Json->SetNumberField(TEXT("sounds_played"), After.SoundsPlayed - Before.SoundsPlayed);
		Json->SetNumberField(TEXT("flashes_started"), After.FlashesStarted - Before.FlashesStarted);
		Json->SetNumberField(TEXT("hit_stops_started"), After.HitStopsStarted - Before.HitStopsStarted);
		Json->SetNumberField(TEXT("wobbles_started"), After.WobblesStarted - Before.WobblesStarted);
		Json->SetNumberField(TEXT("shakes_started"), After.ShakesStarted - Before.ShakesStarted);
		Json->SetNumberField(TEXT("avoided_shown"), After.AvoidedShown - Before.AvoidedShown);
		Json->SetNumberField(TEXT("death_screens_shown"), After.DeathScreensShown - Before.DeathScreensShown);
		Json->SetNumberField(TEXT("swings_played"), After.SwingsPlayed - Before.SwingsPlayed);
		return Json;
	}

	double GetPieWorldSeconds()
	{
		const UWorld* World = TDPieTestUtils::GetPieWorld();
		return World ? World->GetTimeSeconds() : 0.0;
	}

	TArray<TSharedPtr<FJsonValue>> MakeMonsterDebugJson(const AActor* Monster)
	{
		TArray<TSharedPtr<FJsonValue>> Lines;
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const UTDMonsterThinkSubsystem* ThinkSubsystem = World ? UTDMonsterThinkSubsystem::GetMonsterThinkSubsystem(World) : nullptr;
		if (!ThinkSubsystem)
		{
			return Lines;
		}

		const FString ActorToken = Monster ? FString::Printf(TEXT("actor=%s "), *Monster->GetName()) : FString();
		for (const FString& Line : ThinkSubsystem->GetMonsterDebugLines())
		{
			if (!ActorToken.IsEmpty() && !Line.Contains(ActorToken))
			{
				continue;
			}
			Lines.Add(MakeShared<FJsonValueString>(Line));
			if (Lines.Num() >= TDCombatPieMonsterDebugLineLimit)
			{
				break;
			}
		}
		return Lines;
	}

	void RecordCombatPieTimeout(FTDCombatPieTestState& State, const FString& StepName)
	{
		State.TimedOutSteps.Add(MakeShared<FJsonValueString>(StepName));
		const TSharedRef<FJsonObject> Snapshot = MakeShared<FJsonObject>();
		Snapshot->SetNumberField(TEXT("world_seconds"), GetPieWorldSeconds());
		Snapshot->SetStringField(TEXT("monster"), GetNameSafe(State.Monster.Get()));
		Snapshot->SetArrayField(TEXT("monster_debug"), MakeMonsterDebugJson(nullptr));
		State.TimeoutSnapshots->SetObjectField(StepName, Snapshot);
		State.Report->SetStringField(TEXT("timed_out_step"), StepName);
		State.Report->SetArrayField(TEXT("timed_out_steps"), State.TimedOutSteps);
		State.Report->SetObjectField(TEXT("timeout_snapshots"), State.TimeoutSnapshots);
		WriteCombatPieReport(State);
	}

	void UnbindDamageObserver(FTDCombatPieTestState& State)
	{
		if (UTDCombatComponent* ObservedCombat = State.ObservedCombat.Get())
		{
			ObservedCombat->OnDamaged.Remove(State.DamageObserverHandle);
		}
		State.ObservedCombat.Reset();
		State.DamageObserverHandle.Reset();
	}

	bool HasRecastNavMesh(const UWorld* World)
	{
		const TActorIterator<ARecastNavMesh> NavMeshIt(World);
		return static_cast<bool>(NavMeshIt);
	}

	FVector GetMonsterSpawnLocation(const ATDGameCharacter& Player)
	{
		FVector Forward = Player.GetActorForwardVector().GetSafeNormal2D();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::ForwardVector;
		}
		return Player.GetActorLocation() + Forward * TDCombatPieMonsterSpawnDistanceCm;
	}

	FString DescribeCombatPieReadiness()
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const APlayerController* FirstController = World ? World->GetFirstPlayerController() : nullptr;
		const UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
		return FString::Printf(TEXT("PIE world %s, controller %s, pawn %s, feedback subsystem %s (listening %s)."),
			World ? TEXT("yes") : TEXT("no"),
			*GetNameSafe(FirstController),
			FirstController ? *GetNameSafe(FirstController->GetPawn()) : TEXT("None"),
			Feedback ? TEXT("yes") : TEXT("no"),
			Feedback && Feedback->IsListening() ? TEXT("yes") : TEXT("no"));
	}

	bool TryBindCombatPieActors(FTDCombatPieTestState& State)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		ATDGamePlayerController* Controller = World ? Cast<ATDGamePlayerController>(World->GetFirstPlayerController()) : nullptr;
		ATDGameCharacter* Player = Controller ? Cast<ATDGameCharacter>(Controller->GetPawn()) : nullptr;
		const UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
		if (!PlayerCombat || !PlayerCombat->IsAlive() || !Feedback || !Feedback->IsListening() || !Feedback->AreFeedbackAssetsLoaded())
		{
			return false;
		}

		State.Controller = Controller;
		State.Player = Player;
		State.Feedback = Feedback;
		return true;
	}

	bool IsNavigationReadyAroundPlayer(FTDCombatPieTestState& State)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const ATDGameCharacter* Player = State.Player.Get();
		if (!World || !Player)
		{
			return false;
		}

		const double NowSeconds = FPlatformTime::Seconds();
		if (State.LastNavigationCheckSeconds >= 0.0 && NowSeconds - State.LastNavigationCheckSeconds < TDCombatPieNavigationCheckIntervalSeconds)
		{
			return false;
		}
		State.LastNavigationCheckSeconds = NowSeconds;

		UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const bool bIsBuildIdle = NavigationSystem && !NavigationSystem->IsNavigationBuildInProgress() && !NavigationSystem->HasDirtyAreasQueued();
		FNavLocation ProjectedLocation;
		const bool bProjectsPlayer = NavigationSystem && NavigationSystem->ProjectPointToNavigation(Player->GetActorLocation(), ProjectedLocation, TDCombatPieNavigationExtent);
		const bool bProjectsSpawn = NavigationSystem && NavigationSystem->ProjectPointToNavigation(GetMonsterSpawnLocation(*Player), ProjectedLocation, TDCombatPieNavigationExtent);
		const bool bIsReady = HasRecastNavMesh(World) && bIsBuildIdle && bProjectsPlayer && bProjectsSpawn;
		State.NavigationPassCount = bIsReady ? State.NavigationPassCount + 1 : 0;
		return State.NavigationPassCount >= TDCombatPieNavigationRequiredPasses;
	}

	TArray<FGameplayTag> GetSwordComboTags()
	{
		return {
			TDGameplayTags::Action_Attack_Primary_01,
			TDGameplayTags::Action_Attack_Primary_02,
			TDGameplayTags::Action_Attack_Primary_03,
			TDGameplayTags::Action_Attack_Primary_04
		};
	}

	bool ReadAttackHostileOnClick(FAutomationTestBase& Test, const ATDGamePlayerController* Controller)
	{
		const FBoolProperty* AttackOnClickProperty = FindFProperty<FBoolProperty>(ATDGamePlayerController::StaticClass(), TDCombatPieAttackOnClickPropertyName);
		if (!Test.TestNotNull(TEXT("Player controller exposes bAttackHostileOnClick to reflection"), AttackOnClickProperty) || !Controller)
		{
			return false;
		}
		return AttackOnClickProperty->GetPropertyValue_InContainer(Controller);
	}

	void VerifyPlayerSkillSetCombo(FAutomationTestBase& Test, const UTDCombatStyleDefinition* PlayerStyle, const TSharedRef<FJsonObject>& Setup)
	{
		if (!Test.TestNotNull(TEXT("Player skill component has a skill set"), PlayerStyle))
		{
			return;
		}

		const TArray<FGameplayTag> ComboTags = GetSwordComboTags();
		int32 DefinedStepCount = 0;
		for (int32 StepIndex = 0; StepIndex < ComboTags.Num(); ++StepIndex)
		{
			const bool bIsDefined = PlayerStyle->FindAction(ComboTags[StepIndex]) != nullptr;
			Test.TestTrue(*FString::Printf(TEXT("Player skill set defines sword combo step %d (%s)"), StepIndex + 1, *ComboTags[StepIndex].ToString()), bIsDefined);
			DefinedStepCount += bIsDefined ? 1 : 0;
		}
		Setup->SetNumberField(TEXT("player_skill_set_combo_steps"), DefinedStepCount);
	}

	void PrepareCombatLoop(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		ATDGameCharacter* Player = State.Player.Get();
		UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		if (!Test.TestNotNull(TEXT("Player combat component before the combat loop"), PlayerCombat))
		{
			State.bIsAborted = true;
			return;
		}

		PlayerCombat->SetCombatLevel(TDCombatPiePlayerCombatLevel);
		State.CatalogStyle.Reset(TDPlayerCombatCatalog::CreateSwordShieldStyle(GetTransientPackage()));
		const UTDCombatStyleDefinition* PlayerStyle = Player->GetSkillComponent() ? Player->GetSkillComponent()->GetSkillSet() : nullptr;

		TArray<FSoftObjectPath> AssetPaths;
		AssetPaths.Add(FSoftObjectPath(TDCombatPieGoblinSpeciesPath));
		const UTDCombatStyleDefinition* CatalogStyle = State.CatalogStyle.Get();
		if (Test.TestNotNull(TEXT("Sword and shield catalog style is created"), CatalogStyle))
		{
			for (const FGameplayTag& ComboTag : GetSwordComboTags())
			{
				const FTDCombatActionDefinition* Action = CatalogStyle->FindAction(ComboTag);
				const FSoftObjectPath ClipPath = Action ? Action->Action.Animation.ToSoftObjectPath() : FSoftObjectPath();
				if (!ClipPath.IsNull() && FPackageName::DoesPackageExist(ClipPath.GetLongPackageName()))
				{
					AssetPaths.Add(ClipPath);
				}
			}
		}

		State.bIsAttackHostileOnClick = ReadAttackHostileOnClick(Test, State.Controller.Get());
		Test.TestTrue(TEXT("Player controller attacks hostiles on click"), State.bIsAttackHostileOnClick);

		const TSharedRef<FJsonObject> Setup = MakeShared<FJsonObject>();
		Setup->SetStringField(TEXT("map"), TDCombatPieMapPackagePath);
		Setup->SetStringField(TEXT("controller_class"), GetNameSafe(State.Controller.IsValid() ? State.Controller->GetClass() : nullptr));
		Setup->SetStringField(TEXT("player_class"), GetNameSafe(Player->GetClass()));
		Setup->SetStringField(TEXT("player_skill_set"), GetNameSafe(PlayerStyle));
		Setup->SetBoolField(TEXT("attack_hostile_on_click"), State.bIsAttackHostileOnClick);
		Setup->SetNumberField(TEXT("player_combat_level"), PlayerCombat->GetStats().Level);
		Setup->SetNumberField(TEXT("player_max_health"), PlayerCombat->GetMaxHealth());
		Setup->SetNumberField(TEXT("navigation_passes"), State.NavigationPassCount);
		Setup->SetNumberField(TEXT("assets_requested"), AssetPaths.Num());
		VerifyPlayerSkillSetCombo(Test, PlayerStyle, Setup);
		State.Report->SetObjectField(TEXT("setup"), Setup);

		State.AssetHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(AssetPaths, FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority);
		WriteCombatPieReport(State);
	}

	bool IsCombatPieAssetLoadFinished(const FTDCombatPieTestState& State)
	{
		return State.bIsAborted || !State.AssetHandle.IsValid() || State.AssetHandle->HasLoadCompleted() || State.AssetHandle->WasCanceled();
	}

	bool IsWindowInsideClip(const FTDActionTimeWindow& Window, float ClipSeconds)
	{
		return Window.StartSeconds >= 0.f && Window.StartSeconds <= Window.EndSeconds && Window.EndSeconds <= ClipSeconds + TDCombatPieWindowToleranceSeconds;
	}

	void VerifyComboWindowsFitClips(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		const UTDCombatStyleDefinition* CatalogStyle = State.CatalogStyle.Get();
		if (!CatalogStyle)
		{
			return;
		}

		TArray<TSharedPtr<FJsonValue>> StepReports;
		const TArray<FGameplayTag> ComboTags = GetSwordComboTags();
		for (int32 StepIndex = 0; StepIndex < ComboTags.Num(); ++StepIndex)
		{
			const int32 StepNumber = StepIndex + 1;
			const FTDCombatActionDefinition* Action = CatalogStyle->FindAction(ComboTags[StepIndex]);
			if (!Test.TestNotNull(*FString::Printf(TEXT("Catalog defines sword combo step %d"), StepNumber), Action))
			{
				continue;
			}

			const UAnimSequence* Clip = Action->Action.Animation.Get();
			if (!Test.TestNotNull(*FString::Printf(TEXT("Sword combo step %d clip '%s' is loaded"), StepNumber, *Action->Action.Animation.ToString()), Clip))
			{
				continue;
			}

			const float ClipSeconds = Clip->GetPlayLength();
			const TSharedRef<FJsonObject> StepReport = MakeShared<FJsonObject>();
			StepReport->SetNumberField(TEXT("step"), StepNumber);
			StepReport->SetStringField(TEXT("clip"), Clip->GetPathName());
			StepReport->SetNumberField(TEXT("clip_seconds"), ClipSeconds);
			StepReport->SetNumberField(TEXT("hit_windows"), Action->Action.HitWindows.Num());
			StepReport->SetNumberField(TEXT("tag_windows"), Action->Action.TagWindows.Num());
			Test.TestTrue(*FString::Printf(TEXT("Sword combo step %d has a melee hit window"), StepNumber), Action->Action.HitWindows.Num() > 0);
			for (const FTDMeleeHitWindow& HitWindow : Action->Action.HitWindows)
			{
				Test.TestTrue(*FString::Printf(TEXT("Step %d hit window [%.3f, %.3f] fits the %.3f s clip"), StepNumber, HitWindow.Window.StartSeconds, HitWindow.Window.EndSeconds, ClipSeconds), IsWindowInsideClip(HitWindow.Window, ClipSeconds));
			}
			for (const FTDTagWindow& TagWindow : Action->Action.TagWindows)
			{
				Test.TestTrue(*FString::Printf(TEXT("Step %d tag window %s [%.3f, %.3f] fits the %.3f s clip"), StepNumber, *TagWindow.Tags.ToStringSimple(), TagWindow.Window.StartSeconds, TagWindow.Window.EndSeconds, ClipSeconds), IsWindowInsideClip(TagWindow.Window, ClipSeconds));
			}
			const FTDActionTimeWindow& InputBufferWindow = Action->Action.InputBufferWindow;
			if (InputBufferWindow.EndSeconds > 0.f)
			{
				StepReport->SetNumberField(TEXT("input_buffer_start"), InputBufferWindow.StartSeconds);
				StepReport->SetNumberField(TEXT("input_buffer_end"), InputBufferWindow.EndSeconds);
				Test.TestTrue(*FString::Printf(TEXT("Step %d input buffer [%.3f, %.3f] fits the %.3f s clip"), StepNumber, InputBufferWindow.StartSeconds, InputBufferWindow.EndSeconds, ClipSeconds), IsWindowInsideClip(InputBufferWindow, ClipSeconds));
			}
			StepReports.Add(MakeShared<FJsonValueObject>(StepReport));
		}
		State.Report->SetArrayField(TEXT("combo_windows"), StepReports);
		WriteCombatPieReport(State);
	}

	ATDMonsterCharacter* SpawnGoblinInFront(FTDCombatPieTestState& State)
	{
		ATDGamePlayerController* Controller = State.Controller.Get();
		const ATDGameCharacter* Player = State.Player.Get();
		UTDMonsterSpeciesAsset* Species = Cast<UTDMonsterSpeciesAsset>(FSoftObjectPath(TDCombatPieGoblinSpeciesPath).ResolveObject());
		if (!Controller || !Player || !Species)
		{
			return nullptr;
		}

		const FVector SpawnLocation = GetMonsterSpawnLocation(*Player);
		const FRotator FacingPlayer(0.f, (Player->GetActorLocation() - SpawnLocation).GetSafeNormal2D().Rotation().Yaw, 0.f);
		return Controller->SpawnMonsterForTest(Species, SpawnLocation, FacingPlayer);
	}

	void KillActorForTest(AActor* Victim, AActor* Instigator)
	{
		UTDCombatComponent* VictimCombat = UTDCombatLibrary::GetCombatComponent(Victim);
		if (!VictimCombat || !VictimCombat->IsAlive())
		{
			return;
		}
		VictimCombat->ReceiveDamage(TDCombatPieLethalDamage, ETDDamageElement::Physical, false, UTDCombatLibrary::MakeDamageContext(Instigator, Victim->GetActorLocation()));
	}

	void SpawnFirstGoblin(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		ATDMonsterCharacter* Monster = SpawnGoblinInFront(State);
		if (!Test.TestNotNull(TEXT("Goblin spawns 220 cm in front of the player"), Monster))
		{
			State.bIsAborted = true;
			return;
		}
		State.Monster = Monster;
		State.FirstMonsterSpawnLocation = Monster->GetActorLocation();
		const UCapsuleComponent* Capsule = Monster->GetCapsuleComponent();
		State.FirstMonsterFloorZ = State.FirstMonsterSpawnLocation.Z - (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f);
	}

	bool TryTraceMonsterAtScreenPosition(FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return true;
		}

		const ATDGamePlayerController* Controller = State.Controller.Get();
		const ATDMonsterCharacter* Monster = State.Monster.Get();
		const USkeletalMeshComponent* Mesh = Monster ? Monster->GetMesh() : nullptr;
		if (!Controller || !Mesh)
		{
			return false;
		}

		++State.CursorTraceAttempts;
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		Controller->GetViewportSize(ViewportWidth, ViewportHeight);
		FVector2D ScreenPosition = FVector2D::ZeroVector;
		const bool bIsProjected = Controller->ProjectWorldLocationToScreen(Mesh->Bounds.Origin, ScreenPosition);
		State.CursorScreenPosition = ScreenPosition;
		if (!bIsProjected || ScreenPosition.X < 0.0 || ScreenPosition.Y < 0.0 || ScreenPosition.X >= ViewportWidth || ScreenPosition.Y >= ViewportHeight)
		{
			State.CursorTraceHitActorName = TEXT("off-screen");
			State.CursorTraceHitComponentName.Reset();
			return false;
		}

		const AActor* ResolvedTarget = Controller->ResolveHostileTargetAtScreenPosition(ScreenPosition);
		State.CursorTraceHitActorName = ResolvedTarget ? GetNameSafe(ResolvedTarget) : TEXT("None");
		State.CursorTraceHitComponentName = TEXT("ResolveHostileTargetAtScreenPosition");
		State.bDidCursorTraceHitMonster = ResolvedTarget == Monster;
		return State.bDidCursorTraceHitMonster;
	}

	ETDKillRank ResolveCombatPieKillRank(const ATDMonsterCharacter& Monster)
	{
		if (Monster.IsBossMonster())
		{
			return ETDKillRank::Boss;
		}
		return Monster.IsEliteMonster() ? ETDKillRank::Elite : ETDKillRank::Normal;
	}

	double ComputeCombatPieKillHoldSeconds(const ATDGameCharacter* Player, const ATDMonsterCharacter* Monster, const bool bIsPlayerMelee, const bool bWasCritical)
	{
		const UTDSkillComponent* PlayerSkill = Player ? Player->GetSkillComponent() : nullptr;
		const bool bIsComboFinisher = bIsPlayerMelee && PlayerSkill && PlayerSkill->IsPrimaryComboFinisherActive();
		FTDKillPoseHoldInput HoldInput;
		HoldInput.Tier = bIsComboFinisher || bWasCritical ? ETDHitFeelTier::Heavy : ETDHitFeelTier::Light;
		HoldInput.KillRank = Monster ? ResolveCombatPieKillRank(*Monster) : ETDKillRank::Normal;
		HoldInput.bWasCritical = bWasCritical;
		return TDDeathLaunchRules::ComputeKillPoseHoldRealSeconds(HoldInput);
	}

	double ComputeCombatPieRagdollStartLimitSeconds(const FTDCombatPieTestState& State)
	{
		if (State.MonsterKillHoldSeconds >= 0.0)
		{
			return State.MonsterKillHoldSeconds + TDCombatPieRagdollStartSlackSeconds;
		}

		FTDKillPoseHoldInput LongestHoldInput;
		LongestHoldInput.Tier = ETDHitFeelTier::Heavy;
		LongestHoldInput.KillRank = ETDKillRank::Normal;
		LongestHoldInput.GroundAreaDistance = TDCombatPieKillHoldFallbackWaveDistanceCm;
		LongestHoldInput.bWasCritical = true;
		LongestHoldInput.bIsGroundAreaKill = true;
		return TDDeathLaunchRules::ComputeKillPoseHoldRealSeconds(LongestHoldInput) + TDCombatPieRagdollStartSlackSeconds;
	}

	FName FindRagdollProbeBone(const USkeletalMeshComponent& Mesh);

	void RecordMonsterDeathPoint(FTDCombatPieTestState& State)
	{
		State.MonsterDeathSeconds = GetPieWorldSeconds();
		const ATDMonsterCharacter* Monster = State.Monster.Get();
		const USkeletalMeshComponent* Mesh = Monster ? Monster->GetMesh() : nullptr;
		if (!Mesh)
		{
			return;
		}
		State.MonsterDeathProbeLocation = Mesh->GetBoneLocation(FindRagdollProbeBone(*Mesh));
		State.bHasMonsterDeathProbeLocation = true;
	}

	void RecordPlayerHitOnMonster(FTDCombatPieTestState& State, const FTDDamageResult& Result, const FTDDamageContext& Context)
	{
		const ATDGameCharacter* Player = State.Player.Get();
		const ATDMonsterCharacter* Monster = State.Monster.Get();
		const AActor* Caster = Context.Caster.Get();
		const bool bIsFromPlayer = Player && Caster == Player;
		const bool bIsPlayerMelee = bIsFromPlayer && Result.Delivery == ETDDamageDelivery::Melee;
		++State.MonsterHitCount;
		State.MeleeHitsFromPlayer += bIsPlayerMelee ? 1 : 0;
		State.MeleeHitsFromPlayerWithLocation += bIsPlayerMelee && Result.bHasHitLocation ? 1 : 0;
		if (Result.bWasKilled)
		{
			State.bWasMonsterKilledByMelee = bIsPlayerMelee;
			State.MonsterKillHoldSeconds = ComputeCombatPieKillHoldSeconds(Player, Monster, bIsPlayerMelee, Result.bWasCritical);
		}
		if (State.MonsterHitRows.Num() >= TDCombatPieHitRowLimit)
		{
			return;
		}

		const UTDSkillComponent* PlayerSkill = Player ? Player->GetSkillComponent() : nullptr;
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("seconds"), GetPieWorldSeconds() - State.PhaseStartSeconds);
		Row->SetStringField(TEXT("caster"), GetNameSafe(Caster));
		Row->SetBoolField(TEXT("from_player"), bIsFromPlayer);
		Row->SetStringField(TEXT("delivery"), UEnum::GetValueAsString(Result.Delivery));
		Row->SetNumberField(TEXT("applied"), Result.AppliedDamage);
		Row->SetNumberField(TEXT("unclamped"), Result.UnclampedDamage);
		Row->SetNumberField(TEXT("ratio"), Result.DamageRatio);
		Row->SetBoolField(TEXT("critical"), Result.bWasCritical);
		Row->SetBoolField(TEXT("killed"), Result.bWasKilled);
		Row->SetBoolField(TEXT("has_hit_location"), Result.bHasHitLocation);
		Row->SetNumberField(TEXT("hit_location_to_monster_cm"), Monster && Result.bHasHitLocation ? FVector::Dist(Result.HitLocation, Monster->GetActorLocation()) : -1.0);
		Row->SetNumberField(TEXT("combo_step"), PlayerSkill ? PlayerSkill->GetPrimaryComboStep() : -1);
		Row->SetStringField(TEXT("action"), PlayerSkill ? PlayerSkill->GetCurrentActionTag().ToString() : FString());
		State.MonsterHitRows.Add(MakeShared<FJsonValueObject>(Row));
	}

	void RecordMonsterHitOnPlayer(FTDCombatPieTestState& State, const FTDDamageResult& Result, const FTDDamageContext& Context)
	{
		const AActor* Caster = Context.Caster.Get();
		const ATDMonsterCharacter* Monster = State.Monster.Get();
		const bool bIsFromMonster = Monster && Caster == Monster;
		++State.PlayerHitCount;
		State.PlayerHitsFromMonster += bIsFromMonster && Result.Delivery != ETDDamageDelivery::Status ? 1 : 0;
		if (State.FirstPlayerHitCasterName.IsEmpty())
		{
			State.FirstPlayerHitCasterName = GetNameSafe(Caster);
			State.FirstPlayerHitDelivery = UEnum::GetValueAsString(Result.Delivery);
		}
		if (State.PlayerHitRows.Num() >= TDCombatPieHitRowLimit)
		{
			return;
		}

		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("seconds"), GetPieWorldSeconds() - State.PhaseStartSeconds);
		Row->SetStringField(TEXT("caster"), GetNameSafe(Caster));
		Row->SetBoolField(TEXT("from_second_goblin"), bIsFromMonster);
		Row->SetStringField(TEXT("delivery"), UEnum::GetValueAsString(Result.Delivery));
		Row->SetNumberField(TEXT("applied"), Result.AppliedDamage);
		Row->SetNumberField(TEXT("ratio"), Result.DamageRatio);
		Row->SetBoolField(TEXT("has_hit_location"), Result.bHasHitLocation);
		State.PlayerHitRows.Add(MakeShared<FJsonValueObject>(Row));
	}

	void BeginPlayerAttackPhase(FAutomationTestBase& Test, const TSharedRef<FTDCombatPieTestState>& StateRef)
	{
		FTDCombatPieTestState& State = *StateRef;
		if (State.bIsAborted)
		{
			return;
		}

		ATDMonsterCharacter* Monster = State.Monster.Get();
		UTDCombatComponent* MonsterCombat = Monster ? Monster->GetCombatComponent() : nullptr;
		const ATDGameCharacter* Player = State.Player.Get();
		UTDSkillComponent* PlayerSkill = Player ? Player->GetSkillComponent() : nullptr;
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!Test.TestNotNull(TEXT("First goblin is alive before the player attacks"), MonsterCombat)
			|| !Test.TestNotNull(TEXT("Player skill component before the player attacks"), PlayerSkill)
			|| !Test.TestNotNull(TEXT("Combat feedback subsystem before the player attacks"), Feedback))
		{
			State.bIsAborted = true;
			return;
		}

		Test.TestTrue(*FString::Printf(TEXT("Cursor trace at the goblin's screen position (%.0f, %.0f) hits the goblin (hit %s / %s after %d tries)"),
			State.CursorScreenPosition.X, State.CursorScreenPosition.Y, *State.CursorTraceHitActorName, *State.CursorTraceHitComponentName, State.CursorTraceAttempts),
			State.bDidCursorTraceHitMonster);

		State.MonsterStartHealth = MonsterCombat->GetCurrentHealth();
		State.MonsterLowestHealth = State.MonsterStartHealth;
		State.StatsBefore = Feedback->GetStats();
		State.PhaseStartSeconds = GetPieWorldSeconds();
		State.LastAttackSeconds = -1.0;
		State.AttackCommandCount = 0;
		State.AttackedCommandCount = 0;
		State.ApproachingCommandCount = 0;
		State.RejectedCommandCount = 0;
		State.SampledHighestComboStep = 0;
		State.LastSampledActionTag = FGameplayTag();
		State.ActionTagSequence.Reset();
		State.MonsterHitCount = 0;
		State.MeleeHitsFromPlayer = 0;
		State.MeleeHitsFromPlayerWithLocation = 0;
		State.MonsterHitRows.Reset();
		State.MonsterDeathSeconds = -1.0;
		State.MonsterKillHoldSeconds = -1.0;
		State.bHasMonsterDeathProbeLocation = false;
		State.bWasMonsterKilledByMelee = false;
		State.bWasMonsterKillForced = false;
		PlayerSkill->ResetHighestPrimaryComboStepReached();

		UnbindDamageObserver(State);
		const TWeakPtr<FTDCombatPieTestState> WeakState = StateRef;
		State.DamageObserverHandle = MonsterCombat->OnDamaged.AddLambda([WeakState](const FTDDamageResult& Result, const FTDDamageContext& Context)
		{
			if (const TSharedPtr<FTDCombatPieTestState> PinnedState = WeakState.Pin())
			{
				RecordPlayerHitOnMonster(*PinnedState, Result, Context);
			}
		});
		State.ObservedCombat = MonsterCombat;
	}

	void SamplePlayerComboProgress(FTDCombatPieTestState& State)
	{
		const ATDGameCharacter* Player = State.Player.Get();
		const UTDSkillComponent* PlayerSkill = Player ? Player->GetSkillComponent() : nullptr;
		if (!PlayerSkill)
		{
			return;
		}

		State.SampledHighestComboStep = FMath::Max(State.SampledHighestComboStep, PlayerSkill->GetPrimaryComboStep());
		const FGameplayTag ActionTag = PlayerSkill->GetCurrentActionTag();
		if (ActionTag.IsValid() && ActionTag != State.LastSampledActionTag && State.ActionTagSequence.Num() < TDCombatPieActionSequenceLimit)
		{
			State.ActionTagSequence.Add(MakeShared<FJsonValueString>(ActionTag.ToString()));
		}
		State.LastSampledActionTag = ActionTag;
	}

	bool TickPlayerAttackPhase(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return true;
		}

		ATDGamePlayerController* Controller = State.Controller.Get();
		ATDMonsterCharacter* Monster = State.Monster.Get();
		const UTDCombatComponent* MonsterCombat = Monster ? Monster->GetCombatComponent() : nullptr;
		if (!Controller || !MonsterCombat)
		{
			Test.AddError(TEXT("Lost the player controller or the goblin during the melee phase."));
			State.bIsAborted = true;
			return true;
		}

		SamplePlayerComboProgress(State);
		State.MonsterLowestHealth = FMath::Min(State.MonsterLowestHealth, MonsterCombat->GetCurrentHealth());
		if (State.MonsterLowestHealth < State.MonsterStartHealth)
		{
			RequestCombatPieScreenshot(State, TDCombatPiePlayerHitsScreenshot);
		}

		const double NowSeconds = GetPieWorldSeconds();
		if (!MonsterCombat->IsAlive())
		{
			RecordMonsterDeathPoint(State);
			return true;
		}

		if (State.LastAttackSeconds < 0.0 || NowSeconds - State.LastAttackSeconds >= TDCombatPieAttackIntervalSeconds)
		{
			State.LastAttackSeconds = NowSeconds;
			++State.AttackCommandCount;
			const ETDAttackCommandResult CommandResult = Controller->CommandAttackTarget(Monster);
			State.AttackedCommandCount += CommandResult == ETDAttackCommandResult::Attacked ? 1 : 0;
			State.ApproachingCommandCount += CommandResult == ETDAttackCommandResult::Approaching ? 1 : 0;
			State.RejectedCommandCount += CommandResult == ETDAttackCommandResult::Rejected ? 1 : 0;
		}
		return false;
	}

	void FinishPlayerAttackPhase(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		UnbindDamageObserver(State);
		if (State.bIsAborted)
		{
			return;
		}

		const ATDMonsterCharacter* Monster = State.Monster.Get();
		const UTDCombatComponent* MonsterCombat = Monster ? Monster->GetCombatComponent() : nullptr;
		const ATDGameCharacter* Player = State.Player.Get();
		const UTDSkillComponent* PlayerSkill = Player ? Player->GetSkillComponent() : nullptr;
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!Feedback)
		{
			Test.AddError(TEXT("Combat feedback subsystem disappeared during the melee phase."));
			State.bIsAborted = true;
			return;
		}

		const FTDCombatFeedbackStats& StatsAfter = Feedback->GetStats();
		const FTDCombatFeedbackStats& StatsBefore = State.StatsBefore;
		const int32 HighestComboStep = PlayerSkill ? static_cast<int32>(PlayerSkill->GetHighestPrimaryComboStepReached()) : 0;
		const int32 MonsterHitsSeen = StatsAfter.MonsterHitsSeen - StatsBefore.MonsterHitsSeen;
		const int32 MonsterHitEffectsSpawned = StatsAfter.MonsterHitEffectsSpawned - StatsBefore.MonsterHitEffectsSpawned;
		const int32 RequiredMonsterHitEffects = FMath::Max(1, FMath::CeilToInt(static_cast<float>(MonsterHitsSeen) * 0.9f));

		Test.TestTrue(*FString::Printf(TEXT("Player attack commands actually execute attacks (%d attacked, %d approaching, %d rejected of %d)"), State.AttackedCommandCount, State.ApproachingCommandCount, State.RejectedCommandCount, State.AttackCommandCount), State.AttackedCommandCount > 0);
		Test.TestTrue(TEXT("Player melee lowers the goblin's health"), State.MonsterLowestHealth < State.MonsterStartHealth);
		Test.TestTrue(*FString::Printf(TEXT("Goblin takes Melee damage cast by the player (%d of %d hits)"), State.MeleeHitsFromPlayer, State.MonsterHitCount), State.MeleeHitsFromPlayer > 0);
		Test.TestTrue(*FString::Printf(TEXT("Every player melee hit carries a hit location (%d of %d)"), State.MeleeHitsFromPlayerWithLocation, State.MeleeHitsFromPlayer), State.MeleeHitsFromPlayer > 0 && State.MeleeHitsFromPlayerWithLocation == State.MeleeHitsFromPlayer);
		Test.TestTrue(*FString::Printf(TEXT("Player combo reaches step %d or later (highest %d, sampled %d)"), TDCombatPieMinimumComboStepReached, HighestComboStep, State.SampledHighestComboStep), HighestComboStep >= TDCombatPieMinimumComboStepReached);
		Test.TestTrue(TEXT("Goblin dies from the player's melee"), State.bWasMonsterKilledByMelee);
		Test.TestTrue(TEXT("Player melee shows damage numbers"), StatsAfter.NumbersShown > StatsBefore.NumbersShown);
		Test.TestTrue(*FString::Printf(TEXT("Player melee spawns impact effects on the goblin (%d effects for %d hits, need %d)"), MonsterHitEffectsSpawned, MonsterHitsSeen, RequiredMonsterHitEffects), MonsterHitEffectsSpawned >= RequiredMonsterHitEffects);
		Test.TestTrue(TEXT("Player melee flashes the goblin"), StatsAfter.FlashesStarted > StatsBefore.FlashesStarted);
		Test.TestTrue(TEXT("Player melee starts hit stops"), StatsAfter.HitStopsStarted > StatsBefore.HitStopsStarted);
		Test.TestTrue(TEXT("Player melee wobbles the goblin"), StatsAfter.WobblesStarted > StatsBefore.WobblesStarted);
		Test.TestTrue(TEXT("Player melee plays swing sounds"), StatsAfter.SwingsPlayed > StatsBefore.SwingsPlayed);

		const TSharedRef<FJsonObject> CursorTrace = MakeShared<FJsonObject>();
		CursorTrace->SetBoolField(TEXT("hit_goblin"), State.bDidCursorTraceHitMonster);
		CursorTrace->SetNumberField(TEXT("attempts"), State.CursorTraceAttempts);
		CursorTrace->SetNumberField(TEXT("screen_x"), State.CursorScreenPosition.X);
		CursorTrace->SetNumberField(TEXT("screen_y"), State.CursorScreenPosition.Y);
		CursorTrace->SetStringField(TEXT("hit_actor"), State.CursorTraceHitActorName);
		CursorTrace->SetStringField(TEXT("hit_component"), State.CursorTraceHitComponentName);

		const TSharedRef<FJsonObject> Phase = MakeShared<FJsonObject>();
		Phase->SetObjectField(TEXT("cursor_trace"), CursorTrace);
		Phase->SetNumberField(TEXT("attack_commands"), State.AttackCommandCount);
		Phase->SetNumberField(TEXT("attacked_commands"), State.AttackedCommandCount);
		Phase->SetNumberField(TEXT("approaching_commands"), State.ApproachingCommandCount);
		Phase->SetNumberField(TEXT("rejected_commands"), State.RejectedCommandCount);
		Phase->SetNumberField(TEXT("highest_combo_step"), HighestComboStep);
		Phase->SetNumberField(TEXT("sampled_highest_combo_step"), State.SampledHighestComboStep);
		Phase->SetArrayField(TEXT("action_sequence"), State.ActionTagSequence);
		Phase->SetNumberField(TEXT("monster_hits"), State.MonsterHitCount);
		Phase->SetNumberField(TEXT("melee_hits_from_player"), State.MeleeHitsFromPlayer);
		Phase->SetNumberField(TEXT("melee_hits_with_location"), State.MeleeHitsFromPlayerWithLocation);
		Phase->SetArrayField(TEXT("hits"), State.MonsterHitRows);
		Phase->SetNumberField(TEXT("monster_start_health"), State.MonsterStartHealth);
		Phase->SetNumberField(TEXT("monster_lowest_health"), State.MonsterLowestHealth);
		Phase->SetNumberField(TEXT("monster_max_health"), MonsterCombat ? MonsterCombat->GetMaxHealth() : 0.f);
		Phase->SetBoolField(TEXT("killed_by_melee"), State.bWasMonsterKilledByMelee);
		Phase->SetBoolField(TEXT("kill_forced_by_test"), State.bWasMonsterKillForced);
		Phase->SetNumberField(TEXT("seconds_to_kill"), State.MonsterDeathSeconds >= 0.0 ? State.MonsterDeathSeconds - State.PhaseStartSeconds : -1.0);
		Phase->SetNumberField(TEXT("monster_hit_effects_required"), RequiredMonsterHitEffects);
		Phase->SetObjectField(TEXT("feedback_delta"), MakeFeedbackStatsDeltaJson(StatsBefore, StatsAfter));
		Phase->SetArrayField(TEXT("monster_debug"), MakeMonsterDebugJson(Monster));
		State.Report->SetObjectField(TEXT("player_hits"), Phase);
		WriteCombatPieReport(State);
	}

	FName FindRagdollProbeBone(const USkeletalMeshComponent& Mesh)
	{
		if (Mesh.GetBoneIndex(TDCombatPiePelvisBoneName) != INDEX_NONE)
		{
			return TDCombatPiePelvisBoneName;
		}
		for (int32 BoneIndex = 1; BoneIndex < Mesh.GetNumBones(); ++BoneIndex)
		{
			const FName BoneName = Mesh.GetBoneName(BoneIndex);
			if (Mesh.GetBodyInstance(BoneName))
			{
				return BoneName;
			}
		}
		return Mesh.GetBoneName(0);
	}

	TArray<FName> CollectRagdollPoseBones(const USkeletalMeshComponent& Mesh, const FName ProbeBoneName)
	{
		TArray<FName> BoneNames;
		for (const FName& Candidate : TDCombatPiePoseBoneNames)
		{
			if (Candidate != ProbeBoneName && Mesh.GetBoneIndex(Candidate) != INDEX_NONE)
			{
				BoneNames.Add(Candidate);
			}
		}
		if (!BoneNames.IsEmpty())
		{
			return BoneNames;
		}

		for (int32 BoneIndex = 0; BoneIndex < Mesh.GetNumBones() && BoneNames.Num() < TDCombatPieRagdollFallbackPoseBoneLimit; ++BoneIndex)
		{
			const FName BoneName = Mesh.GetBoneName(BoneIndex);
			const FString BoneText = BoneName.ToString();
			if (BoneName != ProbeBoneName && (BoneText.Contains(TEXT("hand")) || BoneText.Contains(TEXT("head"))))
			{
				BoneNames.Add(BoneName);
			}
		}
		return BoneNames;
	}

	FVector GetBoneOffsetFromProbe(const USkeletalMeshComponent& Mesh, const FName ProbeBoneName, const FName BoneName)
	{
		const FTransform ProbeTransform = Mesh.GetSocketTransform(ProbeBoneName, RTS_World);
		return ProbeTransform.InverseTransformPositionNoScale(Mesh.GetBoneLocation(BoneName));
	}

	bool IsMonsterRagdollStarted(FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return true;
		}

		const ATDMonsterCharacter* Monster = State.Monster.Get();
		const USkeletalMeshComponent* Mesh = Monster ? Monster->GetMesh() : nullptr;
		if (!Mesh || !Mesh->IsSimulatingPhysics())
		{
			return false;
		}

		State.bHasRagdollStarted = true;
		State.bWasRagdollingAtStart = Monster->IsRagdolling();
		State.RagdollStartSeconds = GetPieWorldSeconds();
		State.RagdollBoneName = FindRagdollProbeBone(*Mesh);
		State.RagdollStartLocation = Mesh->GetBoneLocation(State.RagdollBoneName);
		State.RagdollLastProbeLocation = State.RagdollStartLocation;
		State.RagdollPoseBoneNames = CollectRagdollPoseBones(*Mesh, State.RagdollBoneName);
		State.RagdollPoseStartOffsets.Reset();
		for (const FName& PoseBoneName : State.RagdollPoseBoneNames)
		{
			State.RagdollPoseStartOffsets.Add(GetBoneOffsetFromProbe(*Mesh, State.RagdollBoneName, PoseBoneName));
		}
		State.RagdollMaxPoseChangeCm = 0.0;
		State.RagdollMaxHorizontalTravelCm = 0.0;
		State.RagdollMaxVerticalTravelCm = 0.0;
		return true;
	}

	bool TickMonsterRagdollObservation(FTDCombatPieTestState& State)
	{
		if (State.bIsAborted || !State.bHasRagdollStarted)
		{
			return true;
		}

		const ATDMonsterCharacter* Monster = State.Monster.Get();
		const USkeletalMeshComponent* Mesh = Monster ? Monster->GetMesh() : nullptr;
		if (!Mesh)
		{
			State.bWasRagdollObservationCut = true;
			return true;
		}

		const double ElapsedSeconds = GetPieWorldSeconds() - State.RagdollStartSeconds;
		const FVector ProbeLocation = Mesh->GetBoneLocation(State.RagdollBoneName);
		State.RagdollLastProbeLocation = ProbeLocation;
		State.RagdollMaxHorizontalTravelCm = FMath::Max(State.RagdollMaxHorizontalTravelCm, FVector::Dist2D(ProbeLocation, State.RagdollStartLocation));
		State.RagdollMaxVerticalTravelCm = FMath::Max(State.RagdollMaxVerticalTravelCm, FMath::Abs(ProbeLocation.Z - State.RagdollStartLocation.Z));
		for (int32 PoseIndex = 0; PoseIndex < State.RagdollPoseBoneNames.Num() && PoseIndex < State.RagdollPoseStartOffsets.Num(); ++PoseIndex)
		{
			const FVector PoseOffset = GetBoneOffsetFromProbe(*Mesh, State.RagdollBoneName, State.RagdollPoseBoneNames[PoseIndex]);
			State.RagdollMaxPoseChangeCm = FMath::Max(State.RagdollMaxPoseChangeCm, FVector::Dist(PoseOffset, State.RagdollPoseStartOffsets[PoseIndex]));
		}
		if (ElapsedSeconds >= TDCombatPieRagdollScreenshotDelaySeconds)
		{
			RequestCombatPieScreenshot(State, TDCombatPieMonsterRagdollScreenshot);
		}
		return ElapsedSeconds >= TDCombatPieRagdollObserveSeconds;
	}

	TArray<TSharedPtr<FJsonValue>> MakeNameArrayJson(const TArray<FName>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FName& Name : Names)
		{
			Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
		}
		return Values;
	}

	void FinishMonsterRagdollPhase(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		const ATDMonsterCharacter* Monster = State.Monster.Get();
		const double SecondsToRagdoll = State.bHasRagdollStarted && State.MonsterDeathSeconds >= 0.0 ? State.RagdollStartSeconds - State.MonsterDeathSeconds : -1.0;
		const double RagdollStartLimitSeconds = ComputeCombatPieRagdollStartLimitSeconds(State);
		const FVector DeathPointLocation = State.bHasMonsterDeathProbeLocation ? State.MonsterDeathProbeLocation : State.RagdollStartLocation;
		const double ArenaDriftCm = FVector::Dist2D(State.RagdollLastProbeLocation, DeathPointLocation);
		const double FloorClearanceCm = State.RagdollLastProbeLocation.Z - State.FirstMonsterFloorZ;
		const bool bStaysInArena = State.bHasRagdollStarted && ArenaDriftCm <= TDCombatPieArenaMaxHorizontalDriftCm && FloorClearanceCm >= -TDCombatPieArenaFloorToleranceCm;
		Test.TestTrue(*FString::Printf(TEXT("Killed goblin simulates physics within the kill pose hold + 0.1 s (%.3f s, limit %.3f s)"), SecondsToRagdoll, RagdollStartLimitSeconds), State.bHasRagdollStarted && SecondsToRagdoll >= 0.0 && SecondsToRagdoll <= RagdollStartLimitSeconds);
		Test.TestTrue(TEXT("Killed goblin reports ragdolling when its physics starts"), State.bWasRagdollingAtStart);
		Test.TestFalse(TEXT("Goblin corpse stays for the 2 s ragdoll observation"), State.bWasRagdollObservationCut);
		Test.TestTrue(*FString::Printf(TEXT("Goblin ragdoll pose bones are found relative to '%s'"), *State.RagdollBoneName.ToString()), State.RagdollPoseBoneNames.Num() > 0);
		Test.TestTrue(*FString::Printf(TEXT("Goblin ragdoll pose changes more than 3 cm relative to '%s' (%.1f cm)"), *State.RagdollBoneName.ToString(), State.RagdollMaxPoseChangeCm), State.RagdollMaxPoseChangeCm > TDCombatPieRagdollMinimumPoseChangeCm);
		Test.TestTrue(*FString::Printf(TEXT("Goblin corpse stays near its death point 2 s after death (%.0f cm from the death point, %.0f cm above the floor)"), ArenaDriftCm, FloorClearanceCm), bStaysInArena);

		const TSharedRef<FJsonObject> Phase = MakeShared<FJsonObject>();
		Phase->SetBoolField(TEXT("ragdoll_started"), State.bHasRagdollStarted);
		Phase->SetNumberField(TEXT("seconds_to_ragdoll"), SecondsToRagdoll);
		Phase->SetNumberField(TEXT("kill_hold_seconds"), State.MonsterKillHoldSeconds);
		Phase->SetNumberField(TEXT("ragdoll_start_limit_seconds"), RagdollStartLimitSeconds);
		Phase->SetBoolField(TEXT("has_death_point"), State.bHasMonsterDeathProbeLocation);
		Phase->SetNumberField(TEXT("death_point_drift_cm"), ArenaDriftCm);
		Phase->SetBoolField(TEXT("is_ragdolling_at_start"), State.bWasRagdollingAtStart);
		Phase->SetBoolField(TEXT("is_ragdolling_after_observation"), Monster && Monster->IsRagdolling());
		Phase->SetStringField(TEXT("probe_bone"), State.RagdollBoneName.ToString());
		Phase->SetArrayField(TEXT("pose_bones"), MakeNameArrayJson(State.RagdollPoseBoneNames));
		Phase->SetNumberField(TEXT("pose_max_change_cm"), State.RagdollMaxPoseChangeCm);
		Phase->SetNumberField(TEXT("probe_max_horizontal_travel_cm"), State.RagdollMaxHorizontalTravelCm);
		Phase->SetNumberField(TEXT("probe_max_vertical_travel_cm"), State.RagdollMaxVerticalTravelCm);
		Phase->SetNumberField(TEXT("arena_drift_cm"), ArenaDriftCm);
		Phase->SetNumberField(TEXT("floor_clearance_cm"), FloorClearanceCm);
		Phase->SetNumberField(TEXT("corpse_life_span_seconds"), Monster ? Monster->GetLifeSpan() : -1.0);
		Phase->SetBoolField(TEXT("observation_cut"), State.bWasRagdollObservationCut);
		State.Report->SetObjectField(TEXT("monster_ragdoll"), Phase);
		WriteCombatPieReport(State);
	}

	void BeginMonsterAttackPhase(FAutomationTestBase& Test, const TSharedRef<FTDCombatPieTestState>& StateRef)
	{
		FTDCombatPieTestState& State = *StateRef;
		if (State.bIsAborted)
		{
			return;
		}

		ATDGamePlayerController* Controller = State.Controller.Get();
		ATDGameCharacter* Player = State.Player.Get();
		UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!Controller || !PlayerCombat || !Feedback)
		{
			Test.AddError(TEXT("Lost the player or the feedback subsystem before the goblin attack phase."));
			State.bIsAborted = true;
			return;
		}

		Controller->StopMovement();
		ATDMonsterCharacter* Monster = SpawnGoblinInFront(State);
		if (!Test.TestNotNull(TEXT("Second goblin spawns 220 cm in front of the player"), Monster))
		{
			State.bIsAborted = true;
			return;
		}

		State.Monster = Monster;
		State.PlayerStartHealth = PlayerCombat->GetCurrentHealth();
		State.PlayerDamagedHealth = State.PlayerStartHealth;
		State.StatsBefore = Feedback->GetStats();
		State.PhaseStartSeconds = GetPieWorldSeconds();
		State.PlayerDamageSeconds = -1.0;
		State.bWasPlayerDamagedByMonster = false;
		State.PlayerHitCount = 0;
		State.PlayerHitsFromMonster = 0;
		State.FirstPlayerHitCasterName.Reset();
		State.FirstPlayerHitDelivery.Reset();
		State.PlayerHitRows.Reset();

		UnbindDamageObserver(State);
		const TWeakPtr<FTDCombatPieTestState> WeakState = StateRef;
		State.DamageObserverHandle = PlayerCombat->OnDamaged.AddLambda([WeakState](const FTDDamageResult& Result, const FTDDamageContext& Context)
		{
			if (const TSharedPtr<FTDCombatPieTestState> PinnedState = WeakState.Pin())
			{
				RecordMonsterHitOnPlayer(*PinnedState, Result, Context);
			}
		});
		State.ObservedCombat = PlayerCombat;
	}

	bool TickMonsterAttackPhase(FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return true;
		}

		const ATDGameCharacter* Player = State.Player.Get();
		const UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		if (!PlayerCombat || (PlayerCombat->GetCurrentHealth() >= State.PlayerStartHealth && State.PlayerHitCount == 0))
		{
			return false;
		}

		State.PlayerDamagedHealth = PlayerCombat->GetCurrentHealth();
		State.PlayerDamageSeconds = GetPieWorldSeconds();
		State.bWasPlayerDamagedByMonster = State.PlayerDamagedHealth < State.PlayerStartHealth;
		RequestCombatPieScreenshot(State, TDCombatPiePlayerDamagedScreenshot);
		return true;
	}

	void FinishMonsterAttackPhase(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		UnbindDamageObserver(State);
		if (State.bIsAborted)
		{
			return;
		}

		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!Feedback)
		{
			Test.AddError(TEXT("Combat feedback subsystem disappeared during the goblin attack phase."));
			State.bIsAborted = true;
			return;
		}

		const FTDCombatFeedbackStats& StatsAfter = Feedback->GetStats();
		Test.TestTrue(TEXT("Goblin attacks lower the player's health"), State.bWasPlayerDamagedByMonster);
		Test.TestTrue(*FString::Printf(TEXT("Player damage is cast by the second goblin with a non-status delivery (%d of %d hits, first by %s as %s)"), State.PlayerHitsFromMonster, State.PlayerHitCount, *State.FirstPlayerHitCasterName, *State.FirstPlayerHitDelivery), State.PlayerHitsFromMonster > 0);
		Test.TestTrue(TEXT("Goblin hits show damage numbers on the player"), StatsAfter.PlayerDamageNumbersShown > State.StatsBefore.PlayerDamageNumbersShown);

		const TSharedRef<FJsonObject> Phase = MakeShared<FJsonObject>();
		Phase->SetBoolField(TEXT("player_damaged"), State.bWasPlayerDamagedByMonster);
		Phase->SetNumberField(TEXT("player_start_health"), State.PlayerStartHealth);
		Phase->SetNumberField(TEXT("player_damaged_health"), State.PlayerDamagedHealth);
		Phase->SetNumberField(TEXT("seconds_to_first_damage"), State.PlayerDamageSeconds >= 0.0 ? State.PlayerDamageSeconds - State.PhaseStartSeconds : -1.0);
		Phase->SetNumberField(TEXT("player_hits"), State.PlayerHitCount);
		Phase->SetNumberField(TEXT("player_hits_from_second_goblin"), State.PlayerHitsFromMonster);
		Phase->SetStringField(TEXT("first_hit_caster"), State.FirstPlayerHitCasterName);
		Phase->SetStringField(TEXT("first_hit_delivery"), State.FirstPlayerHitDelivery);
		Phase->SetArrayField(TEXT("hits"), State.PlayerHitRows);
		Phase->SetObjectField(TEXT("feedback_delta"), MakeFeedbackStatsDeltaJson(State.StatsBefore, StatsAfter));
		Phase->SetArrayField(TEXT("monster_debug"), MakeMonsterDebugJson(State.Monster.Get()));
		State.Report->SetObjectField(TEXT("monster_hits_player"), Phase);

		KillActorForTest(State.Monster.Get(), State.Player.Get());
		WriteCombatPieReport(State);
	}

	void CollectPlayerReactionClips(FTDCombatPieTestState& State, const ATDGameCharacter& Player)
	{
		State.ReactionClips.Reset();
		const UTDSkillComponent* PlayerSkill = Player.GetSkillComponent();
		const UTDCombatStyleDefinition* PlayerStyle = PlayerSkill ? PlayerSkill->GetSkillSet() : nullptr;
		if (!PlayerStyle)
		{
			return;
		}

		const FGameplayTag ReactionTags[] = { TDGameplayTags::Action_Reaction_HeavyHit, TDGameplayTags::Action_Reaction_Hit };
		for (const FGameplayTag& ReactionTag : ReactionTags)
		{
			const FTDCombatReactionDefinition* Reaction = PlayerStyle->FindReaction(ReactionTag);
			if (const UAnimSequence* ReactionClip = Reaction ? Reaction->Action.Animation.Get() : nullptr)
			{
				State.ReactionClips.Add(ReactionClip);
			}
		}
	}

	bool IsPlayingReactionClip(const FTDCombatPieTestState& State, const UTDCharacterAnimationComponent& Animation)
	{
		bool bHasKnownClip = false;
		for (const TWeakObjectPtr<const UAnimSequence>& ReactionClip : State.ReactionClips)
		{
			const UAnimSequence* ReactionSequence = ReactionClip.Get();
			if (!ReactionSequence)
			{
				continue;
			}
			bHasKnownClip = true;
			if (Animation.IsPlayingAction(ReactionSequence))
			{
				return true;
			}
		}
		return !bHasKnownClip && !State.bWasPlayingActionBeforeReaction && Animation.IsPlayingAction();
	}

	bool TickReactionObservation(FTDCombatPieTestState& State)
	{
		if (State.bIsAborted || !State.bWasReactionProbeRun)
		{
			return true;
		}

		const ATDGameCharacter* Player = State.Player.Get();
		const UTDCharacterAnimationComponent* PlayerAnimation = Player ? Player->GetCharacterAnimation() : nullptr;
		if (!PlayerAnimation)
		{
			return true;
		}

		const double ElapsedSeconds = GetPieWorldSeconds() - State.ReactionStartSeconds;
		if (State.ReactionFirstPlayingSeconds < 0.0 && IsPlayingReactionClip(State, *PlayerAnimation))
		{
			State.ReactionFirstPlayingSeconds = ElapsedSeconds;
			const UTDSkillComponent* PlayerSkill = Player->GetSkillComponent();
			State.ReactionActionTag = PlayerSkill ? PlayerSkill->GetCurrentActionTag().ToString() : FString();
		}
		State.ReactionMaxPlanarDisplacementCm = FMath::Max(State.ReactionMaxPlanarDisplacementCm, FVector::Dist2D(Player->GetActorLocation(), State.ReactionStartLocation));
		return ElapsedSeconds >= TDCombatPieReactionObserveSeconds;
	}

	void RunReactionProbe(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		ATDGameCharacter* Player = State.Player.Get();
		UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		const UTDCharacterAnimationComponent* PlayerAnimation = Player ? Player->GetCharacterAnimation() : nullptr;
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!PlayerCombat || !PlayerAnimation || !Feedback)
		{
			Test.AddError(TEXT("Lost the player, its animation or the feedback subsystem before the reaction probe."));
			State.bIsAborted = true;
			return;
		}

		const float TargetDamage = PlayerCombat->GetMaxHealth() * TDCombatPieReactionDamageRatio;
		if (PlayerCombat->GetCurrentHealth() <= TargetDamage * TDCombatPieReactionHealthMargin)
		{
			PlayerCombat->SetStats(PlayerCombat->GetStats(), true);
			State.bWasReactionHealthReset = true;
		}

		CollectPlayerReactionClips(State, *Player);
		FTDDamageContext Context = UTDCombatLibrary::MakeDamageContext(State.Monster.Get(), Player->GetActorLocation());
		Context.Delivery = ETDDamageDelivery::Direct;
		State.StatsBefore = Feedback->GetStats();
		State.ReactionRawDamage = TargetDamage * (100.f + FMath::Max(0.f, static_cast<float>(PlayerCombat->GetNumericAttribute(UTDCombatAttributeSet::GetArmorAttribute())))) / 100.f;
		State.ReactionStartLocation = Player->GetActorLocation();
		State.bWasPlayingActionBeforeReaction = PlayerAnimation->IsPlayingAction();
		State.ReactionStartSeconds = GetPieWorldSeconds();
		State.ReactionFirstPlayingSeconds = -1.0;
		State.ReactionMaxPlanarDisplacementCm = 0.0;
		State.ReactionActionTag.Reset();
		const FTDDamageResult Result = PlayerCombat->ReceiveDamage(State.ReactionRawDamage, ETDDamageElement::Physical, false, Context);
		State.ReactionAppliedDamage = Result.AppliedDamage;
		State.ReactionDamageRatio = Result.DamageRatio;
		State.bWasReactionProbeRun = true;
		TickReactionObservation(State);
	}

	void FinishReactionProbe(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted || !State.bWasReactionProbeRun)
		{
			return;
		}

		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		const bool bHasReactionStartedInTime = State.ReactionFirstPlayingSeconds >= 0.0 && State.ReactionFirstPlayingSeconds <= TDCombatPieReactionStartLimitSeconds;
		Test.TestTrue(*FString::Printf(TEXT("Reaction probe deals at least 18%% of the player's max health (%.3f)"), State.ReactionDamageRatio), State.ReactionDamageRatio >= TDCombatPieReactionMinimumDamageRatio);
		Test.TestTrue(*FString::Printf(TEXT("Player plays a hit reaction within 0.1 s of a 20%% hit (first seen at %.3f s, %d known clips)"), State.ReactionFirstPlayingSeconds, State.ReactionClips.Num()), bHasReactionStartedInTime);
		Test.TestTrue(*FString::Printf(TEXT("Player is knocked back more than 20 cm in 0.4 s (%.1f cm)"), State.ReactionMaxPlanarDisplacementCm), State.ReactionMaxPlanarDisplacementCm > TDCombatPieReactionMinimumDisplacementCm);

		TArray<TSharedPtr<FJsonValue>> ClipNames;
		for (const TWeakObjectPtr<const UAnimSequence>& ReactionClip : State.ReactionClips)
		{
			ClipNames.Add(MakeShared<FJsonValueString>(GetNameSafe(ReactionClip.Get())));
		}

		const TSharedRef<FJsonObject> Phase = MakeShared<FJsonObject>();
		Phase->SetBoolField(TEXT("health_reset"), State.bWasReactionHealthReset);
		Phase->SetNumberField(TEXT("raw_damage"), State.ReactionRawDamage);
		Phase->SetNumberField(TEXT("applied_damage"), State.ReactionAppliedDamage);
		Phase->SetNumberField(TEXT("damage_ratio"), State.ReactionDamageRatio);
		Phase->SetBoolField(TEXT("was_playing_action_before"), State.bWasPlayingActionBeforeReaction);
		Phase->SetArrayField(TEXT("reaction_clips"), ClipNames);
		Phase->SetNumberField(TEXT("reaction_first_seen_seconds"), State.ReactionFirstPlayingSeconds);
		Phase->SetStringField(TEXT("reaction_action"), State.ReactionActionTag);
		Phase->SetNumberField(TEXT("max_planar_displacement_cm"), State.ReactionMaxPlanarDisplacementCm);
		if (Feedback)
		{
			Phase->SetObjectField(TEXT("feedback_delta"), MakeFeedbackStatsDeltaJson(State.StatsBefore, Feedback->GetStats()));
		}
		State.Report->SetObjectField(TEXT("player_reaction"), Phase);
		WriteCombatPieReport(State);
	}

	void RunInvulnerableProbe(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		ATDGameCharacter* Player = State.Player.Get();
		UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!PlayerCombat || !Feedback)
		{
			Test.AddError(TEXT("Lost the player or the feedback subsystem before the invulnerability probe."));
			State.bIsAborted = true;
			return;
		}

		State.StatsBefore = Feedback->GetStats();
		State.InvulnerableHealthBefore = PlayerCombat->GetCurrentHealth();
		PlayerCombat->AddLooseGameplayTag(TDGameplayTags::State_Invulnerable);
		const FTDDamageResult Result = PlayerCombat->ReceiveDamage(TDCombatPieProbeDamage, ETDDamageElement::Physical, false, UTDCombatLibrary::MakeDamageContext(State.Monster.Get(), Player->GetActorLocation()));
		State.InvulnerableHealthAfter = PlayerCombat->GetCurrentHealth();
		State.InvulnerableAppliedDamage = Result.AppliedDamage;
		PlayerCombat->RemoveLooseGameplayTag(TDGameplayTags::State_Invulnerable);
		State.bWasInvulnerableProbeRun = true;
	}

	void FinishInvulnerableProbe(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted || !State.bWasInvulnerableProbeRun)
		{
			return;
		}

		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!Feedback)
		{
			Test.AddError(TEXT("Combat feedback subsystem disappeared during the invulnerability probe."));
			State.bIsAborted = true;
			return;
		}

		const FTDCombatFeedbackStats& StatsAfter = Feedback->GetStats();
		Test.TestTrue(TEXT("Invulnerable player takes no damage"), FMath::IsNearlyEqual(State.InvulnerableHealthBefore, State.InvulnerableHealthAfter, TDCombatPieHealthTolerance) && State.InvulnerableAppliedDamage <= 0.f);
		Test.TestTrue(TEXT("Avoided hit shows the avoid feedback"), StatsAfter.AvoidedShown > State.StatsBefore.AvoidedShown);

		const TSharedRef<FJsonObject> Phase = MakeShared<FJsonObject>();
		Phase->SetNumberField(TEXT("health_before"), State.InvulnerableHealthBefore);
		Phase->SetNumberField(TEXT("health_after"), State.InvulnerableHealthAfter);
		Phase->SetNumberField(TEXT("applied_damage"), State.InvulnerableAppliedDamage);
		Phase->SetObjectField(TEXT("feedback_delta"), MakeFeedbackStatsDeltaJson(State.StatsBefore, StatsAfter));
		State.Report->SetObjectField(TEXT("invulnerable"), Phase);
		WriteCombatPieReport(State);
	}

	void KillPlayerForTest(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		ATDGameCharacter* Player = State.Player.Get();
		UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		UWorld* World = TDPieTestUtils::GetPieWorld();
		if (!PlayerCombat || !Feedback || !World)
		{
			Test.AddError(TEXT("Lost the player or the feedback subsystem before the player death phase."));
			State.bIsAborted = true;
			return;
		}

		const ATDGameGameMode* GameMode = Cast<ATDGameGameMode>(World->GetAuthGameMode());
		State.RespawnDelaySeconds = GameMode ? GameMode->GetPlayerRespawnDelaySeconds() : TDCombatPieFallbackRespawnDelaySeconds;
		State.DeadPlayer = Player;
		State.StatsBefore = Feedback->GetStats();
		const FTDDamageResult Result = PlayerCombat->ReceiveDamage(TDCombatPieLethalDamage, ETDDamageElement::Physical, false, UTDCombatLibrary::MakeDamageContext(State.Monster.Get(), Player->GetActorLocation()));
		State.bWasPlayerKilled = Result.bWasKilled || !PlayerCombat->IsAlive();
		State.PlayerDeathSeconds = GetPieWorldSeconds();
	}

	bool IsPlayerRagdollStarted(FTDCombatPieTestState& State)
	{
		if (State.bIsAborted || !State.bWasPlayerKilled)
		{
			return true;
		}

		const ATDGameCharacter* DeadPlayer = State.DeadPlayer.Get();
		const USkeletalMeshComponent* Mesh = DeadPlayer ? DeadPlayer->GetMesh() : nullptr;
		if (!Mesh || !Mesh->IsSimulatingPhysics())
		{
			return false;
		}

		State.bHasPlayerRagdollStarted = true;
		State.bWasPlayerRagdollingAtStart = DeadPlayer->IsRagdolling();
		State.PlayerRagdollSeconds = GetPieWorldSeconds();
		return true;
	}

	void FinishPlayerDeathPhase(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		const ATDGameCharacter* DeadPlayer = State.DeadPlayer.Get();
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!Feedback)
		{
			Test.AddError(TEXT("Combat feedback subsystem disappeared during the player death phase."));
			State.bIsAborted = true;
			return;
		}

		const FTDCombatFeedbackStats& StatsAfter = Feedback->GetStats();
		Test.TestTrue(TEXT("Lethal damage kills the player"), State.bWasPlayerKilled);
		Test.TestTrue(TEXT("Dead player simulates a ragdoll"), State.bHasPlayerRagdollStarted);
		Test.TestTrue(TEXT("Dead player reports ragdolling when its physics starts"), State.bWasPlayerRagdollingAtStart);
		Test.TestTrue(TEXT("Player death shows the death screen"), StatsAfter.DeathScreensShown > State.StatsBefore.DeathScreensShown);

		const TSharedRef<FJsonObject> Phase = MakeShared<FJsonObject>();
		Phase->SetBoolField(TEXT("player_killed"), State.bWasPlayerKilled);
		Phase->SetBoolField(TEXT("ragdoll_started"), State.bHasPlayerRagdollStarted);
		Phase->SetNumberField(TEXT("seconds_to_ragdoll"), State.bHasPlayerRagdollStarted ? State.PlayerRagdollSeconds - State.PlayerDeathSeconds : -1.0);
		Phase->SetBoolField(TEXT("is_ragdolling_at_start"), State.bWasPlayerRagdollingAtStart);
		Phase->SetBoolField(TEXT("is_ragdolling_after_screenshot"), DeadPlayer && DeadPlayer->IsRagdolling());
		Phase->SetNumberField(TEXT("respawn_delay_seconds"), State.RespawnDelaySeconds);
		Phase->SetObjectField(TEXT("feedback_delta"), MakeFeedbackStatsDeltaJson(State.StatsBefore, StatsAfter));
		State.Report->SetObjectField(TEXT("player_death"), Phase);
		WriteCombatPieReport(State);
	}

	bool TickPlayerRespawn(FTDCombatPieTestState& State)
	{
		if (State.bIsAborted || !State.bWasPlayerKilled)
		{
			return true;
		}

		const ATDGamePlayerController* Controller = State.Controller.Get();
		ATDGameCharacter* CurrentPlayer = Controller ? Cast<ATDGameCharacter>(Controller->GetPawn()) : nullptr;
		const UTDCombatComponent* CurrentCombat = CurrentPlayer ? CurrentPlayer->GetCombatComponent() : nullptr;
		const double NowSeconds = GetPieWorldSeconds();
		if (CurrentCombat && CurrentPlayer != State.DeadPlayer.Get() && CurrentCombat->IsAlive())
		{
			State.bHasRespawned = true;
			State.RespawnSeconds = NowSeconds;
			State.Player = CurrentPlayer;
			return true;
		}
		return NowSeconds - State.PlayerDeathSeconds >= State.RespawnDelaySeconds + TDCombatPieRespawnExtraSeconds;
	}

	void FinishRespawnPhase(FAutomationTestBase& Test, FTDCombatPieTestState& State)
	{
		if (State.bIsAborted || !State.bWasPlayerKilled)
		{
			return;
		}

		Test.TestTrue(TEXT("A new living player pawn exists within the respawn delay plus 3 s"), State.bHasRespawned);
		const ATDGameCharacter* Player = State.bHasRespawned ? State.Player.Get() : nullptr;
		const UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;

		const TSharedRef<FJsonObject> Phase = MakeShared<FJsonObject>();
		Phase->SetBoolField(TEXT("respawned"), State.bHasRespawned);
		Phase->SetNumberField(TEXT("seconds_after_death"), State.bHasRespawned ? State.RespawnSeconds - State.PlayerDeathSeconds : -1.0);
		Phase->SetNumberField(TEXT("new_player_health"), PlayerCombat ? PlayerCombat->GetCurrentHealth() : 0.f);
		State.Report->SetObjectField(TEXT("respawn"), Phase);
		WriteCombatPieReport(State);
	}

	class FTDCombatPieScreenshotCommand : public IAutomationLatentCommand
	{
	public:
		FTDCombatPieScreenshotCommand(FAutomationTestBase& InTest, const TSharedRef<FTDCombatPieTestState>& InState, const FString& InFileName)
			: Test(InTest)
			, State(InState)
			, FileName(InFileName)
		{
		}

		virtual bool Update() override
		{
			if (State->bIsAborted)
			{
				return true;
			}

			RequestCombatPieScreenshot(*State, FileName);
			const bool bIsSaved = IFileManager::Get().FileExists(*MakeCombatPieOutputPath(FileName));
			if (!bIsSaved && GetCurrentRunTime() < TDCombatPieScreenshotTimeoutSeconds)
			{
				return false;
			}

			State->ScreenshotReport->SetBoolField(FileName, bIsSaved);
			if (bIsSaved)
			{
				Test.AddInfo(FString::Printf(TEXT("Screenshot saved: %s"), *MakeCombatPieOutputPath(FileName)));
			}
			else
			{
				Test.AddWarning(FString::Printf(TEXT("Screenshot '%s' was not written within %.0f s; keep the editor in the foreground."), *FileName, TDCombatPieScreenshotTimeoutSeconds));
			}
			WriteCombatPieReport(*State);
			return true;
		}

	private:
		FAutomationTestBase& Test;
		TSharedRef<FTDCombatPieTestState> State;
		FString FileName;
	};

	void EnqueueCombatPieScreenshot(FAutomationTestBase& Test, const TSharedRef<FTDCombatPieTestState>& State, const FString& FileName)
	{
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FTDCombatPieScreenshotCommand(Test, State, FileName)));
	}

	void AllowFirstInjectionTimelineEnsure(FAutomationTestBase& Test)
	{
		Test.AddExpectedErrorPlain(TDCombatPieTimelineEnsureExpression, EAutomationExpectedErrorFlags::Contains, -1);
		Test.AddExpectedError(TDCombatPieEnsureHeadingPattern, EAutomationExpectedErrorFlags::Contains, -1, true);
		Test.AddExpectedError(TDCombatPieEnsureBlankLinePattern, EAutomationExpectedErrorFlags::Contains, -1, true);
		Test.AddExpectedError(TDCombatPieEnsureStackHeadingPattern, EAutomationExpectedErrorFlags::Contains, -1, true);
		Test.AddExpectedError(TDCombatPieEnsureCallstackLinePattern, EAutomationExpectedErrorFlags::Contains, -1, true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatFullLoopPieTest, "TDGame.PIE.Combat.FullLoop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDCombatFullLoopPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("TDGame.PIE.Combat.FullLoop needs a rendering editor and was skipped; run it in the GUI editor with check_automation_tests.py --filter TDGame.PIE.Combat."));
		return true;
	}

	AllowFirstInjectionTimelineEnsure(*this);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(MakeCombatPieOutputPath(TDCombatPieReportFileName)), true);
	TSharedRef<FTDCombatPieTestState> State = MakeShared<FTDCombatPieTestState>();
	if (!OpenMapAndStartPie(*this, TDCombatPieMapPackagePath))
	{
		return false;
	}

	EnqueueWaitUntil(*this, TEXT("PIE player, controller and listening combat feedback subsystem are ready"), [State]()
	{
		return TryBindCombatPieActors(*State);
	}, TDCombatPieSetupTimeoutSeconds, [this, State]()
	{
		AddError(DescribeCombatPieReadiness());
		State->bIsAborted = true;
		RecordCombatPieTimeout(*State, TEXT("bind_actors"));
	});

	EnqueueWaitUntil(*this, TEXT("Navigation mesh is built, idle and covers the player and the spawn point twice in a row"), [State]()
	{
		return State->bIsAborted || IsNavigationReadyAroundPlayer(*State);
	}, TDCombatPieNavigationTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("navigation"));
	});

	EnqueueStep([this, State]()
	{
		PrepareCombatLoop(*this, *State);
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Goblin species and sword combo clips finish async loading"), [State]()
	{
		return IsCombatPieAssetLoadFinished(*State);
	}, TDCombatPieAssetLoadTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("asset_load"));
	});

	EnqueueStep([this, State]()
	{
		VerifyComboWindowsFitClips(*this, *State);
		SpawnFirstGoblin(*this, *State);
		return true;
	});

	EnqueueWaitSeconds(TDCombatPieMonsterSettleSeconds);

	EnqueueWaitUntil(*this, TEXT("Cursor trace at the goblin's screen position hits the goblin"), [State]()
	{
		return TryTraceMonsterAtScreenPosition(*State);
	}, TDCombatPieCursorTraceTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("cursor_trace"));
	});

	EnqueueStep([this, State]()
	{
		BeginPlayerAttackPhase(*this, State);
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Player kills the goblin with melee attacks every 0.35 s"), [this, State]()
	{
		return TickPlayerAttackPhase(*this, *State);
	}, TDCombatPieKillTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("player_kills_goblin"));
		RecordMonsterDeathPoint(*State);
		State->bWasMonsterKillForced = true;
		KillActorForTest(State->Monster.Get(), State->Player.Get());
	});

	EnqueueStep([this, State]()
	{
		FinishPlayerAttackPhase(*this, *State);
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Killed goblin starts its ragdoll"), [State]()
	{
		return IsMonsterRagdollStarted(*State);
	}, TDCombatPieRagdollStartTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("monster_ragdoll_start"));
	});

	EnqueueWaitUntil(*this, TEXT("Goblin ragdoll is observed for 2 s"), [State]()
	{
		return TickMonsterRagdollObservation(*State);
	}, TDCombatPieRagdollObserveTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("monster_ragdoll_observation"));
	});

	EnqueueStep([this, State]()
	{
		FinishMonsterRagdollPhase(*this, *State);
		return true;
	});

	EnqueueCombatPieScreenshot(*this, State, TDCombatPiePlayerHitsScreenshot);
	EnqueueCombatPieScreenshot(*this, State, TDCombatPieMonsterRagdollScreenshot);

	EnqueueStep([this, State]()
	{
		BeginMonsterAttackPhase(*this, State);
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Goblin damages the idle player"), [State]()
	{
		return TickMonsterAttackPhase(*State);
	}, TDCombatPieMonsterAttackTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("goblin_damages_player"));
	});

	EnqueueWaitSeconds(TDCombatPieFeedbackSettleSeconds);

	EnqueueStep([this, State]()
	{
		FinishMonsterAttackPhase(*this, *State);
		return true;
	});

	EnqueueCombatPieScreenshot(*this, State, TDCombatPiePlayerDamagedScreenshot);
	EnqueueWaitSeconds(TDCombatPieFeedbackSettleSeconds);

	EnqueueStep([this, State]()
	{
		RunReactionProbe(*this, *State);
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Player hit reaction and knockback are observed for 0.4 s"), [State]()
	{
		return TickReactionObservation(*State);
	}, TDCombatPieReactionObserveTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("player_reaction"));
	});

	EnqueueStep([this, State]()
	{
		FinishReactionProbe(*this, *State);
		RunInvulnerableProbe(*this, *State);
		return true;
	});

	EnqueueWaitSeconds(TDCombatPieFeedbackSettleSeconds);

	EnqueueStep([this, State]()
	{
		FinishInvulnerableProbe(*this, *State);
		KillPlayerForTest(*this, *State);
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Dead player starts its ragdoll"), [State]()
	{
		return IsPlayerRagdollStarted(*State);
	}, TDCombatPiePlayerRagdollTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("player_ragdoll_start"));
	});

	EnqueueWaitSeconds(TDCombatPieDeathScreenFadeSeconds);
	EnqueueCombatPieScreenshot(*this, State, TDCombatPiePlayerDeadScreenshot);

	EnqueueStep([this, State]()
	{
		FinishPlayerDeathPhase(*this, *State);
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Player respawns as a new living pawn"), [State]()
	{
		return TickPlayerRespawn(*State);
	}, TDCombatPieRespawnTimeoutSeconds, [State]()
	{
		RecordCombatPieTimeout(*State, TEXT("player_respawn"));
	});

	EnqueueStep([this, State]()
	{
		FinishRespawnPhase(*this, *State);
		UnbindDamageObserver(*State);
		State->Report->SetBoolField(TEXT("completed"), true);
		WriteCombatPieReport(*State);
		return true;
	});

	EnqueueEndPie();
	return true;
}

#endif
