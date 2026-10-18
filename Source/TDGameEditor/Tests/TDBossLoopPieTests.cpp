#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDGameCharacter.h"
#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"
#include "Combat/Feedback/TDCombatOverlayWidget.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/TDGameGameMode.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/TDPieWaitUntilCommand.h"
#include "UnrealClient.h"
#include "World/Generation/TDArenaEncounter.h"
#include "World/Generation/TDArenaEncounterDefinition.h"

namespace
{
	const TCHAR* const TDBossLoopPieMapPackagePath = TEXT("/Game/MonsterAI/Maps/LV_TDBossArena");
	const TCHAR* const TDBossLoopPieOutputRootFolder = TEXT("AgentOps");
	const TCHAR* const TDBossLoopPieOutputLeafFolder = TEXT("boss-loop/pie");
	const TCHAR* const TDBossLoopPieDateFormat = TEXT("%Y%m%d");
	const TCHAR* const TDBossLoopPieReportFileName = TEXT("boss-loop-pie.json");
	const TCHAR* const TDBossLoopPieArenaSealedScreenshot = TEXT("01_arena_sealed.png");
	const TCHAR* const TDBossLoopPieWaveOneScreenshot = TEXT("02_wave_one.png");
	const TCHAR* const TDBossLoopPieDefeatScreenshot = TEXT("03_defeat.png");
	const TCHAR* const TDBossLoopPieRespawnScreenshot = TEXT("04_respawn_checkpoint.png");
	const TCHAR* const TDBossLoopPieMidBossBarScreenshot = TEXT("05_midboss_bar.png");
	const TCHAR* const TDBossLoopPieBossBarScreenshot = TEXT("06_boss_bar.png");
	const TCHAR* const TDBossLoopPieBossLowScreenshot = TEXT("07_boss_low.png");
	const TCHAR* const TDBossLoopPieVictoryScreenshot = TEXT("08_victory.png");
	const TCHAR* const TDBossLoopPieTimelineEnsureExpression = TEXT("TimelineTrait.GetState(Context, ChildState)");
	const TCHAR* const TDBossLoopPieEnsureHeadingPattern = TEXT("^LogOutputDevice: === Handled ensure: ===\\s*$");
	const TCHAR* const TDBossLoopPieEnsureBlankLinePattern = TEXT("^LogOutputDevice:\\s*$");
	const TCHAR* const TDBossLoopPieEnsureStackHeadingPattern = TEXT("^LogOutputDevice: Stack:\\s*$");
	const TCHAR* const TDBossLoopPieEnsureCallstackLinePattern = TEXT("^LogOutputDevice: \\[Callstack\\] 0x");
	const FVector TDBossLoopPieNavigationExtent(50.0, 50.0, 250.0);

	constexpr bool bRequireBossPhaseMessages = true;
	constexpr double TDBossLoopPieSetupTimeoutSeconds = 20.0;
	constexpr double TDBossLoopPieNavigationTimeoutSeconds = 180.0;
	constexpr double TDBossLoopPieNavigationCheckIntervalSeconds = 1.0;
	constexpr int32 TDBossLoopPieNavigationRequiredPasses = 2;
	constexpr double TDBossLoopPieSpeciesLoadTimeoutSeconds = 60.0;
	constexpr double TDBossLoopPieSealTimeoutSeconds = 5.0;
	constexpr double TDBossLoopPieFirstWaveTimeoutSeconds = 15.0;
	constexpr double TDBossLoopPieDefeatTimeoutSeconds = 3.0;
	constexpr double TDBossLoopPieArenaResetTimeoutSeconds = 30.0;
	constexpr double TDBossLoopPieRespawnTimeoutSeconds = 60.0;
	constexpr double TDBossLoopPieSecondAttemptTimeoutSeconds = 5.0;
	constexpr double TDBossLoopPieWaveTimeoutSeconds = 60.0;
	constexpr double TDBossLoopPieWaveClearedTimeoutSeconds = 15.0;
	constexpr double TDBossLoopPieBossBarTimeoutSeconds = 5.0;
	constexpr double TDBossLoopPieMarkTimeoutSeconds = 30.0;
	constexpr double TDBossLoopPieBossPhaseTimeoutSeconds = 60.0;
	constexpr double TDBossLoopPieVictoryTimeoutSeconds = 15.0;
	constexpr double TDBossLoopPieVictoryBannerTimeoutSeconds = 3.0;
	constexpr double TDBossLoopPieBossBarHideTimeoutSeconds = 3.0;
	constexpr double TDBossLoopPieScreenshotTimeoutSeconds = 5.0;
	constexpr float TDBossLoopPieSealedScreenshotDelaySeconds = 0.5f;
	constexpr float TDBossLoopPieWaveScreenshotDelaySeconds = 1.f;
	constexpr float TDBossLoopPieDefeatScreenshotDelaySeconds = 0.6f;
	constexpr float TDBossLoopPieMinionKillDelaySeconds = 0.8f;
	constexpr float TDBossLoopPiePhaseSettleSeconds = 1.5f;
	constexpr float TDBossLoopPiePlayerMaxHealth = 1000000.f;
	constexpr float TDBossLoopPieLethalDamage = 1000000000.f;
	constexpr float TDBossLoopPieMarkHitHealthRatio = 0.02f;
	constexpr float TDBossLoopPieMarkMarginRatio = 0.03f;
	constexpr int32 TDBossLoopPieMarkHitLimit = 100;
	constexpr double TDBossLoopPieBossDuelDistanceCm = 600.0;
	constexpr double TDBossLoopPieArenaEdgeMarginCm = 200.0;
	constexpr double TDBossLoopPieRespawnToleranceCm = 300.0;
	constexpr double TDBossLoopPieLeftoverMarginCm = 300.0;

	struct FTDBossLoopPiePhaseRecord
	{
		TWeakObjectPtr<AActor> Monster;
		int32 PhaseIndex = 0;
	};

	struct FTDBossLoopPieState
	{
		bool bIsAborted = false;
		FString OutputDirectory;
		TWeakObjectPtr<ATDArenaEncounter> Encounter;
		TWeakObjectPtr<ATDGameCharacter> Player;
		TWeakObjectPtr<ATDGameCharacter> DefeatedPlayer;
		TWeakObjectPtr<ATDMonsterCharacter> Boss;
		FGameplayMessageListenerHandle PhaseListener;
		TArray<FTDBossLoopPiePhaseRecord> PhaseRecords;
		TSet<FString> RequestedScreenshots;
		TArray<TSharedPtr<FJsonValue>> ScreenshotRows;
		TArray<TSharedPtr<FJsonValue>> WaveTimings;
		TArray<TSharedPtr<FJsonValue>> BossRows;
		TArray<TSharedPtr<FJsonValue>> ErrorRows;
		int32 NavigationPassCount = 0;
		double LastNavigationCheckSeconds = -1.0;
		double WaveStartSeconds = 0.0;
		int32 WavesCleared = 0;
		double RespawnDistanceCm = -1.0;
	};

	void EnqueueBossLoopPieWave(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State, int32 WaveIndex);

	FString MakeBossLoopPieOutputDirectory()
	{
		const FString DateStamp = FDateTime::Now().ToString(TDBossLoopPieDateFormat);
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TDBossLoopPieOutputRootFolder / DateStamp / TDBossLoopPieOutputLeafFolder);
	}

	FString MakeBossLoopPieOutputPath(const FTDBossLoopPieState& State, const FString& FileName)
	{
		return State.OutputDirectory / FileName;
	}

	void RequestBossLoopPieScreenshot(FTDBossLoopPieState& State, const FString& FileName)
	{
		if (State.RequestedScreenshots.Contains(FileName))
		{
			return;
		}
		const FString ScreenshotPath = MakeBossLoopPieOutputPath(State, FileName);
		IFileManager::Get().Delete(*ScreenshotPath, false, true, true);
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false, false, FIntRect(), true);
		State.RequestedScreenshots.Add(FileName);
		const UWorld* World = TDPieTestUtils::GetPieWorld();
		UGameViewportClient* GameViewport = World ? World->GetGameViewport() : nullptr;
		if (GameViewport && GameViewport->Viewport)
		{
			GameViewport->ProcessScreenShots(GameViewport->Viewport);
		}
	}

	void AllowBossLoopPieFirstInjectionEnsure(FAutomationTestBase& Test)
	{
		Test.AddExpectedErrorPlain(TDBossLoopPieTimelineEnsureExpression, EAutomationExpectedErrorFlags::Contains, -1);
		Test.AddExpectedError(TDBossLoopPieEnsureHeadingPattern, EAutomationExpectedErrorFlags::Contains, -1, true);
		Test.AddExpectedError(TDBossLoopPieEnsureBlankLinePattern, EAutomationExpectedErrorFlags::Contains, -1, true);
		Test.AddExpectedError(TDBossLoopPieEnsureStackHeadingPattern, EAutomationExpectedErrorFlags::Contains, -1, true);
		Test.AddExpectedError(TDBossLoopPieEnsureCallstackLinePattern, EAutomationExpectedErrorFlags::Contains, -1, true);
	}

	UTDCombatOverlayWidget* FindBossLoopPieOverlay()
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
		return Feedback ? Feedback->GetOverlayWidget() : nullptr;
	}

	const TCHAR* GetBossLoopPieYesNo(const bool bValue)
	{
		return bValue ? TEXT("yes") : TEXT("no");
	}

	FString DescribeBossLoopPieStatus(const FTDBossLoopPieState& State)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		if (!Encounter)
		{
			return TEXT("The arena encounter is not available.");
		}

		const UTDCombatOverlayWidget* Overlay = FindBossLoopPieOverlay();
		return FString::Printf(TEXT("Encounter %s, wave %d, attempt %d, alive %d, sealed %s, exit open %s; overlay %s, banner shown %s, death screen %s, boss bar %s."),
			*UEnum::GetValueAsString(Encounter->GetEncounterState()),
			Encounter->GetCurrentWaveIndex(),
			Encounter->GetAttemptCount(),
			Encounter->GetAliveMonsterCount(),
			GetBossLoopPieYesNo(Encounter->IsSealed()),
			GetBossLoopPieYesNo(Encounter->IsExitOpen()),
			GetBossLoopPieYesNo(Overlay != nullptr),
			GetBossLoopPieYesNo(Overlay && Overlay->IsEncounterBannerShown()),
			GetBossLoopPieYesNo(Overlay && Overlay->IsDeathScreenShown()),
			GetBossLoopPieYesNo(Overlay && Overlay->IsBossHealthBarShown()));
	}

	void RecordBossLoopPieError(FAutomationTestBase& Test, FTDBossLoopPieState& State, const FString& Message)
	{
		Test.AddError(Message);
		State.ErrorRows.Add(MakeShared<FJsonValueString>(Message));
	}

	void AbortBossLoopPie(FAutomationTestBase& Test, FTDBossLoopPieState& State, const FString& Message)
	{
		RecordBossLoopPieError(Test, State, Message);
		State.bIsAborted = true;
	}

	void CheckBossLoopPie(FAutomationTestBase& Test, FTDBossLoopPieState& State, const FString& Expectation, const bool bIsMet)
	{
		if (bIsMet)
		{
			return;
		}
		RecordBossLoopPieError(Test, State, FString::Printf(TEXT("Expectation failed: %s. %s"), *Expectation, *DescribeBossLoopPieStatus(State)));
	}

	void EnqueueBossLoopPieWait(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State, const FString& Description, TFunction<bool()> Condition, const double TimeoutSeconds)
	{
		TDPieTestUtils::EnqueueWaitUntil(Test, Description, [State, ConditionFunction = MoveTemp(Condition)]()
		{
			return State->bIsAborted || ConditionFunction();
		}, TimeoutSeconds, [TestPointer = &Test, State, Description, TimeoutSeconds]()
		{
			const FString Status = DescribeBossLoopPieStatus(*State);
			TestPointer->AddInfo(Status);
			State->ErrorRows.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: timed out after %.0f s. %s"), *Description, TimeoutSeconds, *Status)));
			State->bIsAborted = true;
		});
	}

	class FTDBossLoopPieScreenshotCommand : public IAutomationLatentCommand
	{
	public:
		FTDBossLoopPieScreenshotCommand(FAutomationTestBase& InTest, const TSharedRef<FTDBossLoopPieState>& InState, const FString& InFileName)
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

			RequestBossLoopPieScreenshot(*State, FileName);
			const FString ScreenshotPath = MakeBossLoopPieOutputPath(*State, FileName);
			const bool bIsSaved = IFileManager::Get().FileExists(*ScreenshotPath);
			if (!bIsSaved && GetCurrentRunTime() < TDBossLoopPieScreenshotTimeoutSeconds)
			{
				return false;
			}

			const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("file"), FileName);
			Row->SetBoolField(TEXT("saved"), bIsSaved);
			State->ScreenshotRows.Add(MakeShared<FJsonValueObject>(Row));
			if (bIsSaved)
			{
				Test.AddInfo(FString::Printf(TEXT("Screenshot saved: %s"), *ScreenshotPath));
				return true;
			}
			Test.AddWarning(FString::Printf(TEXT("Screenshot '%s' was not written within %.0f s; keep the editor in the foreground."), *FileName, TDBossLoopPieScreenshotTimeoutSeconds));
			return true;
		}

	private:
		FAutomationTestBase& Test;
		TSharedRef<FTDBossLoopPieState> State;
		FString FileName;
	};

	void EnqueueBossLoopPieScreenshot(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State, const FString& FileName)
	{
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FTDBossLoopPieScreenshotCommand(Test, State, FileName)));
	}

	bool HasBossLoopPieNavMesh(const UWorld* World)
	{
		const TActorIterator<ARecastNavMesh> NavMeshIt(World);
		return static_cast<bool>(NavMeshIt);
	}

	bool IsBossLoopPieNavigationReady(FTDBossLoopPieState& State)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const ATDGameCharacter* Player = State.Player.Get();
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		if (!World || !Player || !Encounter)
		{
			return false;
		}

		const double NowSeconds = FPlatformTime::Seconds();
		if (State.LastNavigationCheckSeconds >= 0.0 && NowSeconds - State.LastNavigationCheckSeconds < TDBossLoopPieNavigationCheckIntervalSeconds)
		{
			return false;
		}
		State.LastNavigationCheckSeconds = NowSeconds;

		UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const bool bIsBuildIdle = NavigationSystem && !NavigationSystem->IsNavigationBuildInProgress() && !NavigationSystem->HasDirtyAreasQueued();
		FNavLocation ProjectedLocation;
		const bool bProjectsPlayer = NavigationSystem && NavigationSystem->ProjectPointToNavigation(Player->GetActorLocation(), ProjectedLocation, TDBossLoopPieNavigationExtent);
		const bool bProjectsArenaCenter = NavigationSystem && NavigationSystem->ProjectPointToNavigation(Encounter->GetActorLocation(), ProjectedLocation, TDBossLoopPieNavigationExtent);
		const bool bIsReady = HasBossLoopPieNavMesh(World) && bIsBuildIdle && bProjectsPlayer && bProjectsArenaCenter;
		State.NavigationPassCount = bIsReady ? State.NavigationPassCount + 1 : 0;
		return State.NavigationPassCount >= TDBossLoopPieNavigationRequiredPasses;
	}

	void RegisterBossLoopPiePhaseListener(const TSharedRef<FTDBossLoopPieState>& State, UWorld& World)
	{
		if (State->PhaseListener.IsValid())
		{
			return;
		}

		TFunction<void(FGameplayTag, const FTDMonsterPhaseMessage&)> OnPhaseEntered = [State](FGameplayTag, const FTDMonsterPhaseMessage& Message)
		{
			FTDBossLoopPiePhaseRecord Record;
			Record.Monster = Message.Monster.Get();
			Record.PhaseIndex = Message.PhaseIndex;
			State->PhaseRecords.Add(Record);
		};
		State->PhaseListener = UGameplayMessageSubsystem::Get(&World).RegisterListener<FTDMonsterPhaseMessage>(TDGameplayTags::Event_Monster_PhaseEntered, MoveTemp(OnPhaseEntered));
	}

	bool TryBindBossLoopPieActors(const TSharedRef<FTDBossLoopPieState>& State)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ATDGameCharacter* Player = Controller ? Cast<ATDGameCharacter>(Controller->GetPawn()) : nullptr;
		const UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
		const bool bIsFeedbackReady = Feedback && Feedback->IsListening() && Feedback->AreFeedbackAssetsLoaded() && Feedback->GetOverlayWidget();
		if (!Player || !UTDCombatLibrary::IsActorAlive(Player) || !bIsFeedbackReady || !UGameplayMessageSubsystem::HasInstance(World))
		{
			return false;
		}

		ATDArenaEncounter* Encounter = nullptr;
		int32 EncounterCount = 0;
		for (TActorIterator<ATDArenaEncounter> EncounterIt(World); EncounterIt; ++EncounterIt)
		{
			Encounter = *EncounterIt;
			++EncounterCount;
		}
		if (EncounterCount != 1)
		{
			return false;
		}

		State->Player = Player;
		State->Encounter = Encounter;
		RegisterBossLoopPiePhaseListener(State, *World);
		return true;
	}

	bool IsBossLoopPieSpeciesLoaded(const FTDBossLoopPieState& State)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		return Encounter && Encounter->AreSpeciesLoaded();
	}

	bool BoostBossLoopPiePlayer(ATDGameCharacter* Player)
	{
		UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		if (!PlayerCombat)
		{
			return false;
		}

		FTDCombatStats BoostedStats = PlayerCombat->GetStats();
		BoostedStats.BaseMaxHealth = TDBossLoopPiePlayerMaxHealth;
		BoostedStats.HealthPerLevel = 0.f;
		PlayerCombat->SetStats(BoostedStats, true);
		return PlayerCombat->IsAlive() && FMath::IsNearlyEqual(PlayerCombat->GetMaxHealth(), TDBossLoopPiePlayerMaxHealth, 1.f);
	}

	void BeginBossLoopPieAttempt(FAutomationTestBase& Test, FTDBossLoopPieState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		ATDGameCharacter* Player = State.Player.Get();
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		if (!Player || !Encounter)
		{
			AbortBossLoopPie(Test, State, TEXT("The player or the arena encounter is gone before the attempt starts."));
			return;
		}
		if (!BoostBossLoopPiePlayer(Player))
		{
			AbortBossLoopPie(Test, State, TEXT("The player could not be raised to 1,000,000 maximum health."));
			return;
		}
		Player->TeleportPawn(Encounter->GetEntryZoneCenter(), Player->GetActorRotation());
	}

	bool IsBossLoopPieAttemptSealed(const FTDBossLoopPieState& State)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		const UTDCombatOverlayWidget* Overlay = FindBossLoopPieOverlay();
		return Encounter && Overlay
			&& Encounter->GetEncounterState() == ETDArenaEncounterState::WaveDelay
			&& Encounter->IsSealed()
			&& Overlay->IsEncounterBannerShown()
			&& !Overlay->GetEncounterBannerTitle().IsEmpty();
	}

	bool IsBossLoopPieWaveInState(const FTDBossLoopPieState& State, const int32 WaveIndex, const ETDArenaEncounterState ExpectedState)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		return Encounter && Encounter->GetCurrentWaveIndex() == WaveIndex && Encounter->GetEncounterState() == ExpectedState;
	}

	bool IsBossLoopPieFirstWaveFought(const FTDBossLoopPieState& State)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		return IsBossLoopPieWaveInState(State, 0, ETDArenaEncounterState::WaveActive) && Encounter->GetAliveMonsterCount() >= 1;
	}

	void ApplyBossLoopPieDamage(AActor* Target, AActor* Instigator, const float Amount)
	{
		FTDDamageSpec DamageSpec;
		DamageSpec.Amount = Amount;
		DamageSpec.Instigator = Instigator;
		UTDCombatLibrary::TryApplyDamage(Target, DamageSpec);
	}

	void DefeatBossLoopPiePlayer(FAutomationTestBase& Test, FTDBossLoopPieState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		ATDGameCharacter* Player = State.Player.Get();
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		TArray<ATDMonsterCharacter*> Monsters;
		if (Encounter)
		{
			Encounter->GetAliveMonsters(Monsters);
		}
		if (!Player || Monsters.IsEmpty())
		{
			AbortBossLoopPie(Test, State, TEXT("No living player or monster is available for the lethal hit of attempt 1."));
			return;
		}

		State.DefeatedPlayer = Player;
		ApplyBossLoopPieDamage(Player, Monsters[0], TDBossLoopPieLethalDamage);
		CheckBossLoopPie(Test, State, TEXT("the lethal monster hit kills the player"), !UTDCombatLibrary::IsActorAlive(Player));
	}

	bool HasBossLoopPieDefeatPresentation(const FTDBossLoopPieState& State)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		const UTDCombatOverlayWidget* Overlay = FindBossLoopPieOverlay();
		return Encounter && Overlay
			&& Encounter->GetEncounterState() == ETDArenaEncounterState::Failed
			&& Overlay->IsEncounterBannerShown()
			&& Overlay->GetEncounterBannerStyle() == ETDEncounterBannerStyle::Defeat
			&& Overlay->IsDeathScreenShown();
	}

	int32 CountBossLoopPieMonstersNearArena(const UWorld& World, const ATDArenaEncounter& Encounter)
	{
		const FVector ArenaCenter = Encounter.GetActorLocation();
		const double RadiusSquaredCm = FMath::Square(static_cast<double>(Encounter.GetArenaRadiusCm()) + TDBossLoopPieLeftoverMarginCm);
		int32 MonsterCount = 0;
		for (TActorIterator<ATDMonsterCharacter> MonsterIt(&World); MonsterIt; ++MonsterIt)
		{
			const ATDMonsterCharacter* Monster = *MonsterIt;
			if (IsValid(Monster) && FVector::DistSquared2D(Monster->GetActorLocation(), ArenaCenter) <= RadiusSquaredCm)
			{
				++MonsterCount;
			}
		}
		return MonsterCount;
	}

	bool IsBossLoopPieArenaReset(const FTDBossLoopPieState& State)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		const UWorld* World = TDPieTestUtils::GetPieWorld();
		return Encounter && World
			&& Encounter->GetEncounterState() == ETDArenaEncounterState::Idle
			&& Encounter->GetAliveMonsterCount() == 0
			&& CountBossLoopPieMonstersNearArena(*World, *Encounter) == 0
			&& !Encounter->IsSealed();
	}

	bool TryFindBossLoopPieRespawnedPlayer(FTDBossLoopPieState& State)
	{
		const UWorld* World = TDPieTestUtils::GetPieWorld();
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		ATDGameCharacter* RespawnedPlayer = Controller ? Cast<ATDGameCharacter>(Controller->GetPawn()) : nullptr;
		if (!RespawnedPlayer || RespawnedPlayer == State.DefeatedPlayer.Get() || !UTDCombatLibrary::IsActorAlive(RespawnedPlayer))
		{
			return false;
		}
		State.Player = RespawnedPlayer;
		return true;
	}

	void VerifyBossLoopPieRespawnLocation(FAutomationTestBase& Test, FTDBossLoopPieState& State)
	{
		const ATDGameCharacter* Player = State.Player.Get();
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		if (State.bIsAborted || !Player || !Encounter)
		{
			return;
		}

		State.RespawnDistanceCm = FVector::Dist2D(Player->GetActorLocation(), Encounter->GetCheckpointTransform().GetLocation());
		CheckBossLoopPie(Test, State,
			FString::Printf(TEXT("the respawned pawn stands within %.0f cm of the checkpoint (measured %.1f cm)"), TDBossLoopPieRespawnToleranceCm, State.RespawnDistanceCm),
			State.RespawnDistanceCm <= TDBossLoopPieRespawnToleranceCm);
	}

	bool IsBossLoopPieSecondAttemptStarted(const FTDBossLoopPieState& State)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		return Encounter && Encounter->GetEncounterState() == ETDArenaEncounterState::WaveDelay;
	}

	void VerifyBossLoopPieSecondAttempt(FAutomationTestBase& Test, FTDBossLoopPieState& State)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		if (State.bIsAborted || !Encounter)
		{
			return;
		}
		CheckBossLoopPie(Test, State, TEXT("the encounter counts the second attempt"), Encounter->GetAttemptCount() == 2);
	}

	bool TryBeginBossLoopPieWave(FTDBossLoopPieState& State, const int32 WaveIndex, const ETDArenaEncounterState ExpectedState)
	{
		if (!IsBossLoopPieWaveInState(State, WaveIndex, ExpectedState))
		{
			return false;
		}
		State.WaveStartSeconds = FPlatformTime::Seconds();
		return true;
	}

	bool TryFinishBossLoopPieWave(FTDBossLoopPieState& State, const int32 WaveIndex)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		if (!Encounter)
		{
			return false;
		}

		const bool bIsCleared = Encounter->GetEncounterState() == ETDArenaEncounterState::Victory || Encounter->GetCurrentWaveIndex() > WaveIndex;
		if (!bIsCleared)
		{
			return false;
		}
		State.WaveTimings.Add(MakeShared<FJsonValueNumber>(FPlatformTime::Seconds() - State.WaveStartSeconds));
		++State.WavesCleared;
		return true;
	}

	void KillBossLoopPieAliveMonsters(const FTDBossLoopPieState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		if (!Encounter)
		{
			return;
		}

		TArray<ATDMonsterCharacter*> Monsters;
		Encounter->GetAliveMonsters(Monsters);
		for (ATDMonsterCharacter* Monster : Monsters)
		{
			ApplyBossLoopPieDamage(Monster, State.Player.Get(), TDBossLoopPieLethalDamage);
		}
	}

	float GetBossLoopPieHealthRatio(const AActor* Actor)
	{
		const UTDCombatComponent* Combat = UTDCombatLibrary::GetCombatComponent(Actor);
		if (!Combat || Combat->GetMaxHealth() <= 0.f)
		{
			return 0.f;
		}
		return Combat->GetCurrentHealth() / Combat->GetMaxHealth();
	}

	FVector ComputeBossLoopPieDuelLocation(const ATDArenaEncounter& Encounter, const ATDMonsterCharacter& Boss)
	{
		const FVector BossLocation = Boss.GetActorLocation();
		FVector TowardEntry = (Encounter.GetEntryZoneCenter() - BossLocation).GetSafeNormal2D();
		if (TowardEntry.IsNearlyZero())
		{
			TowardEntry = -FVector::ForwardVector;
		}

		FVector DuelLocation = BossLocation + TowardEntry * TDBossLoopPieBossDuelDistanceCm;
		const FVector ArenaCenter = Encounter.GetActorLocation();
		const double MaximumRadiusCm = static_cast<double>(Encounter.GetArenaRadiusCm()) - TDBossLoopPieArenaEdgeMarginCm;
		const FVector2D PlanarOffset(DuelLocation.X - ArenaCenter.X, DuelLocation.Y - ArenaCenter.Y);
		if (PlanarOffset.Size() > MaximumRadiusCm)
		{
			const FVector2D ClampedOffset = PlanarOffset.GetSafeNormal() * MaximumRadiusCm;
			DuelLocation.X = ArenaCenter.X + ClampedOffset.X;
			DuelLocation.Y = ArenaCenter.Y + ClampedOffset.Y;
		}
		return DuelLocation;
	}

	void BeginBossLoopPieBossFight(FAutomationTestBase& Test, FTDBossLoopPieState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		ATDMonsterCharacter* Boss = Encounter ? Encounter->GetBossHealthBarTarget() : nullptr;
		ATDGameCharacter* Player = State.Player.Get();
		if (!Boss || !Player)
		{
			AbortBossLoopPie(Test, State, TEXT("A boss wave is active but the encounter reports no boss health bar target."));
			return;
		}

		State.Boss = Boss;
		Player->TeleportPawn(ComputeBossLoopPieDuelLocation(*Encounter, *Boss), Player->GetActorRotation());
	}

	bool IsBossLoopPieBossBarShownFor(const AActor* Boss)
	{
		const UTDCombatOverlayWidget* Overlay = FindBossLoopPieOverlay();
		return Overlay && Boss && Overlay->IsBossHealthBarShown() && Overlay->GetBossHealthBarTarget() == Boss;
	}

	bool IsBossLoopPieBannerShown(const ETDEncounterBannerStyle Style)
	{
		const UTDCombatOverlayWidget* Overlay = FindBossLoopPieOverlay();
		return Overlay && Overlay->IsEncounterBannerShown() && Overlay->GetEncounterBannerStyle() == Style;
	}

	bool IsBossLoopPieBossBarHidden()
	{
		const UTDCombatOverlayWidget* Overlay = FindBossLoopPieOverlay();
		return Overlay && !Overlay->IsBossHealthBarShown();
	}

	bool ApplyBossLoopPieMarkHit(FAutomationTestBase& Test, FTDBossLoopPieState& State, const float TargetRatio, int32& HitCount)
	{
		ATDMonsterCharacter* Boss = State.Boss.Get();
		if (!Boss || !UTDCombatLibrary::IsActorAlive(Boss) || GetBossLoopPieHealthRatio(Boss) <= TargetRatio)
		{
			return true;
		}
		if (HitCount >= TDBossLoopPieMarkHitLimit)
		{
			RecordBossLoopPieError(Test, State, FString::Printf(TEXT("Boss health stayed at %.3f after %d hits aimed at %.3f."), GetBossLoopPieHealthRatio(Boss), HitCount, TargetRatio));
			return true;
		}

		const UTDCombatComponent* BossCombat = UTDCombatLibrary::GetCombatComponent(Boss);
		ApplyBossLoopPieDamage(Boss, State.Player.Get(), BossCombat->GetMaxHealth() * TDBossLoopPieMarkHitHealthRatio);
		++HitCount;
		return false;
	}

	void EnqueueBossLoopPieMarkHits(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State, const float Mark)
	{
		const TSharedRef<int32> HitCount = MakeShared<int32>(0);
		const float TargetRatio = Mark - TDBossLoopPieMarkMarginRatio;
		EnqueueBossLoopPieWait(Test, State, FString::Printf(TEXT("Boss health falls to the %.2f mark"), Mark), [TestPointer = &Test, State, HitCount, TargetRatio]()
		{
			return ApplyBossLoopPieMarkHit(*TestPointer, *State, TargetRatio, *HitCount);
		}, TDBossLoopPieMarkTimeoutSeconds);
	}

	void RecordBossLoopPieBoss(FAutomationTestBase& Test, FTDBossLoopPieState& State, const int32 WaveIndex, const FString& BossName, const TArray<float>& Marks)
	{
		if (State.bIsAborted)
		{
			return;
		}

		const ATDMonsterCharacter* Boss = State.Boss.Get();
		int32 PhaseMessageCount = 0;
		for (const FTDBossLoopPiePhaseRecord& Record : State.PhaseRecords)
		{
			PhaseMessageCount += (Boss && Record.Monster.Get() == Boss) ? 1 : 0;
		}

		TArray<TSharedPtr<FJsonValue>> MarkValues;
		for (const float Mark : Marks)
		{
			MarkValues.Add(MakeShared<FJsonValueNumber>(Mark));
		}

		const UTDCombatOverlayWidget* Overlay = FindBossLoopPieOverlay();
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("wave"), WaveIndex);
		Row->SetStringField(TEXT("name"), BossName.IsEmpty() ? GetNameSafe(Boss) : BossName);
		Row->SetArrayField(TEXT("marks"), MarkValues);
		Row->SetNumberField(TEXT("phase_messages"), PhaseMessageCount);
		Row->SetNumberField(TEXT("health_ratio_end"), GetBossLoopPieHealthRatio(Boss));
		Row->SetNumberField(TEXT("overlay_mark_count"), Overlay ? Overlay->GetBossPhaseMarkCount() : -1);
		Row->SetNumberField(TEXT("boss_phase_index"), Boss ? Boss->GetMonsterPhaseIndex() : -1);
		Row->SetBoolField(TEXT("boss_has_brain"), Boss && Boss->HasMonsterBrain());
		const UWorld* PieWorld = TDPieTestUtils::GetPieWorld();
		const UTDCombatFeedbackSubsystem* Feedback = PieWorld ? PieWorld->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
		Row->SetNumberField(TEXT("overlay_phase_pulses"), Feedback ? Feedback->GetStats().BossPhasePulses : -1);
		State.BossRows.Add(MakeShared<FJsonValueObject>(Row));

		const int32 DrawableMarkCount = FMath::Min(Marks.FilterByPredicate([](const float Mark) { return Mark > 0.f && Mark < 1.f; }).Num(), 4);
		CheckBossLoopPie(Test, State, FString::Printf(TEXT("the boss bar of wave %d draws %d phase notches"), WaveIndex, DrawableMarkCount), Overlay && Overlay->GetBossPhaseMarkCount() == DrawableMarkCount);
		if (PhaseMessageCount >= Marks.Num())
		{
			return;
		}
		const FString Message = FString::Printf(TEXT("Boss of wave %d sent %d phase messages for %d health marks; the boss species does not announce its phases yet."), WaveIndex, PhaseMessageCount, Marks.Num());
		if constexpr (bRequireBossPhaseMessages)
		{
			RecordBossLoopPieError(Test, State, Message);
		}
		else
		{
			Test.AddWarning(Message);
		}
	}

	bool IsFirstBossLoopPieBossWave(const UTDArenaEncounterDefinition& Definition, const int32 WaveIndex)
	{
		for (int32 EarlierWaveIndex = 0; EarlierWaveIndex < WaveIndex; ++EarlierWaveIndex)
		{
			if (Definition.IsBossWave(EarlierWaveIndex))
			{
				return false;
			}
		}
		return true;
	}

	void EnqueueBossLoopPieWaveCleared(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State, const int32 WaveIndex)
	{
		EnqueueBossLoopPieWait(Test, State, FString::Printf(TEXT("Wave %d is cleared"), WaveIndex), [State, WaveIndex]()
		{
			return TryFinishBossLoopPieWave(*State, WaveIndex);
		}, TDBossLoopPieWaveClearedTimeoutSeconds);
	}

	void EnqueueBossLoopPieNextWave(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State, const int32 WaveIndex)
	{
		TDPieTestUtils::EnqueueStep([TestPointer = &Test, State, WaveIndex]()
		{
			EnqueueBossLoopPieWave(*TestPointer, State, WaveIndex + 1);
			return true;
		});
	}

	void EnqueueBossLoopPieMinionWave(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State, const int32 WaveIndex)
	{
		EnqueueBossLoopPieWait(Test, State, FString::Printf(TEXT("Wave %d becomes active"), WaveIndex), [State, WaveIndex]()
		{
			return TryBeginBossLoopPieWave(*State, WaveIndex, ETDArenaEncounterState::WaveActive);
		}, TDBossLoopPieWaveTimeoutSeconds);

		TDPieTestUtils::EnqueueWaitSeconds(TDBossLoopPieMinionKillDelaySeconds);

		TDPieTestUtils::EnqueueStep([State]()
		{
			KillBossLoopPieAliveMonsters(*State);
			return true;
		});

		EnqueueBossLoopPieWaveCleared(Test, State, WaveIndex);
		EnqueueBossLoopPieNextWave(Test, State, WaveIndex);
	}

	void EnqueueBossLoopPieBossWave(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State, const UTDArenaEncounterDefinition& Definition, const int32 WaveIndex)
	{
		const FTDArenaSpawnEntry* BossEntry = Definition.Waves[WaveIndex].Entries.FindByPredicate([](const FTDArenaSpawnEntry& Entry)
		{
			return Entry.bShowsBossHealthBar;
		});
		TArray<float> Marks;
		FString BossName;
		if (BossEntry)
		{
			Marks = BossEntry->BossPhaseHealthMarks;
			BossName = BossEntry->BossDisplayName.ToString();
		}
		Marks.Sort([](const float Left, const float Right)
		{
			return Left > Right;
		});
		const bool bIsFirstBossWave = IsFirstBossLoopPieBossWave(Definition, WaveIndex);
		const bool bIsLastWave = WaveIndex == Definition.Waves.Num() - 1;

		EnqueueBossLoopPieWait(Test, State, FString::Printf(TEXT("Wave %d becomes active as a boss wave"), WaveIndex), [State, WaveIndex]()
		{
			return TryBeginBossLoopPieWave(*State, WaveIndex, ETDArenaEncounterState::BossActive);
		}, TDBossLoopPieWaveTimeoutSeconds);

		TDPieTestUtils::EnqueueStep([TestPointer = &Test, State]()
		{
			BeginBossLoopPieBossFight(*TestPointer, *State);
			return true;
		});

		EnqueueBossLoopPieWait(Test, State, FString::Printf(TEXT("Wave %d shows the boss health bar for its boss"), WaveIndex), [State]()
		{
			return IsBossLoopPieBossBarShownFor(State->Boss.Get());
		}, TDBossLoopPieBossBarTimeoutSeconds);

		if (bIsFirstBossWave)
		{
			EnqueueBossLoopPieScreenshot(Test, State, TDBossLoopPieMidBossBarScreenshot);
		}
		if (bIsLastWave)
		{
			EnqueueBossLoopPieScreenshot(Test, State, TDBossLoopPieBossBarScreenshot);
		}

		for (int32 MarkIndex = 0; MarkIndex < Marks.Num(); ++MarkIndex)
		{
			EnqueueBossLoopPieMarkHits(Test, State, Marks[MarkIndex]);
			if (!bRequireBossPhaseMessages)
			{
				continue;
			}
			const int32 ExpectedPhaseIndex = MarkIndex + 1;
			EnqueueBossLoopPieWait(Test, State, FString::Printf(TEXT("Wave %d boss enters phase %d after the %.2f mark"), WaveIndex, ExpectedPhaseIndex, Marks[MarkIndex]), [State, ExpectedPhaseIndex]()
			{
				const ATDMonsterCharacter* Boss = State->Boss.Get();
				return !Boss || !UTDCombatLibrary::IsActorAlive(Boss) || Boss->GetMonsterPhaseIndex() >= ExpectedPhaseIndex;
			}, TDBossLoopPieBossPhaseTimeoutSeconds);
		}

		TDPieTestUtils::EnqueueWaitSeconds(TDBossLoopPiePhaseSettleSeconds);

		TDPieTestUtils::EnqueueStep([TestPointer = &Test, State, WaveIndex, BossName, Marks]()
		{
			RecordBossLoopPieBoss(*TestPointer, *State, WaveIndex, BossName, Marks);
			return true;
		});

		if (bIsLastWave)
		{
			EnqueueBossLoopPieScreenshot(Test, State, TDBossLoopPieBossLowScreenshot);
		}

		TDPieTestUtils::EnqueueStep([State]()
		{
			KillBossLoopPieAliveMonsters(*State);
			return true;
		});

		EnqueueBossLoopPieWaveCleared(Test, State, WaveIndex);
		EnqueueBossLoopPieNextWave(Test, State, WaveIndex);
	}

	void VerifyBossLoopPieVictoryState(FAutomationTestBase& Test, FTDBossLoopPieState& State)
	{
		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		if (State.bIsAborted || !Encounter)
		{
			return;
		}
		CheckBossLoopPie(Test, State, TEXT("the exit opens on victory"), Encounter->IsExitOpen());
		CheckBossLoopPie(Test, State, TEXT("the seal lifts on victory"), !Encounter->IsSealed());
		const UTDArenaEncounterDefinition* Definition = Encounter->GetEncounterDefinition();
		int32 ExpectedKillCount = 0;
		for (int32 WaveIndex = 0; Definition && WaveIndex < Definition->Waves.Num(); ++WaveIndex)
		{
			ExpectedKillCount += Definition->CountMonstersInWave(WaveIndex);
		}
		CheckBossLoopPie(Test, State, FString::Printf(TEXT("the winning attempt counts %d kills"), ExpectedKillCount), Encounter->GetKilledMonsterCount() == ExpectedKillCount);
		const UWorld* World = TDPieTestUtils::GetPieWorld();
		const ATDGameGameMode* GameMode = World ? World->GetAuthGameMode<ATDGameGameMode>() : nullptr;
		CheckBossLoopPie(Test, State, TEXT("victory releases the arena respawn checkpoint"), GameMode && !GameMode->HasRespawnCheckpoint());
	}

	void FinishBossLoopPie(FAutomationTestBase& Test, FTDBossLoopPieState& State)
	{
		State.PhaseListener.Unregister();

		const ATDArenaEncounter* Encounter = State.Encounter.Get();
		const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetBoolField(TEXT("passed"), !State.bIsAborted && !Test.HasAnyErrors());
		Report->SetNumberField(TEXT("attempts"), Encounter ? Encounter->GetAttemptCount() : 0);
		Report->SetNumberField(TEXT("waves_cleared"), State.WavesCleared);
		Report->SetNumberField(TEXT("kills"), Encounter ? Encounter->GetKilledMonsterCount() : 0);
		Report->SetArrayField(TEXT("wave_timings_seconds"), State.WaveTimings);
		Report->SetArrayField(TEXT("bosses"), State.BossRows);
		Report->SetNumberField(TEXT("respawn_distance_cm"), State.RespawnDistanceCm);
		Report->SetArrayField(TEXT("screenshots"), State.ScreenshotRows);
		Report->SetArrayField(TEXT("errors"), State.ErrorRows);

		FString ReportText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportText);
		if (!FJsonSerializer::Serialize(Report, Writer))
		{
			Test.AddWarning(TEXT("The boss loop report could not be serialized."));
			return;
		}

		const FString ReportPath = MakeBossLoopPieOutputPath(State, TDBossLoopPieReportFileName);
		if (!FFileHelper::SaveStringToFile(ReportText, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Test.AddWarning(FString::Printf(TEXT("The boss loop report could not be written to %s."), *ReportPath));
			return;
		}
		Test.AddInfo(FString::Printf(TEXT("Boss loop report written: %s"), *ReportPath));
	}

	void EnqueueBossLoopPieFinish(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State)
	{
		EnqueueBossLoopPieWait(Test, State, TEXT("The encounter reaches victory"), [State]()
		{
			const ATDArenaEncounter* Encounter = State->Encounter.Get();
			return Encounter && Encounter->GetEncounterState() == ETDArenaEncounterState::Victory;
		}, TDBossLoopPieVictoryTimeoutSeconds);

		EnqueueBossLoopPieWait(Test, State, TEXT("The victory banner is shown"), []()
		{
			return IsBossLoopPieBannerShown(ETDEncounterBannerStyle::Victory);
		}, TDBossLoopPieVictoryBannerTimeoutSeconds);

		TDPieTestUtils::EnqueueStep([TestPointer = &Test, State]()
		{
			VerifyBossLoopPieVictoryState(*TestPointer, *State);
			return true;
		});

		EnqueueBossLoopPieWait(Test, State, TEXT("The boss health bar hides after the victory"), []()
		{
			return IsBossLoopPieBossBarHidden();
		}, TDBossLoopPieBossBarHideTimeoutSeconds);

		EnqueueBossLoopPieScreenshot(Test, State, TDBossLoopPieVictoryScreenshot);

		TDPieTestUtils::EnqueueStep([TestPointer = &Test, State]()
		{
			FinishBossLoopPie(*TestPointer, *State);
			return true;
		});

		TDPieTestUtils::EnqueueEndPie();
	}

	void EnqueueBossLoopPieWave(FAutomationTestBase& Test, const TSharedRef<FTDBossLoopPieState>& State, const int32 WaveIndex)
	{
		const ATDArenaEncounter* Encounter = State->Encounter.Get();
		const UTDArenaEncounterDefinition* Definition = Encounter ? Encounter->GetEncounterDefinition() : nullptr;
		if (State->bIsAborted || !Definition || !Definition->Waves.IsValidIndex(WaveIndex))
		{
			EnqueueBossLoopPieFinish(Test, State);
			return;
		}

		if (Definition->IsBossWave(WaveIndex))
		{
			EnqueueBossLoopPieBossWave(Test, State, *Definition, WaveIndex);
			return;
		}
		EnqueueBossLoopPieMinionWave(Test, State, WaveIndex);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDBossLoopPieTest, "TDGame.PIE.Combat.BossLoop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDBossLoopPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("TDGame.PIE.Combat.BossLoop needs a rendering editor and was skipped; run it in the GUI editor with check_automation_tests.py --filter TDGame.PIE.Combat."));
		return true;
	}

	AllowBossLoopPieFirstInjectionEnsure(*this);
	const TSharedRef<FTDBossLoopPieState> State = MakeShared<FTDBossLoopPieState>();
	State->OutputDirectory = MakeBossLoopPieOutputDirectory();
	IFileManager::Get().MakeDirectory(*State->OutputDirectory, true);
	if (!OpenMapAndStartPie(*this, TDBossLoopPieMapPackagePath))
	{
		return false;
	}

	EnqueueBossLoopPieWait(*this, State, TEXT("PIE player, feedback overlay and exactly one arena encounter are ready"), [State]()
	{
		return TryBindBossLoopPieActors(State);
	}, TDBossLoopPieSetupTimeoutSeconds);

	EnqueueBossLoopPieWait(*this, State, TEXT("Navigation mesh is built, idle and covers the player and the arena centre twice in a row"), [State]()
	{
		return IsBossLoopPieNavigationReady(*State);
	}, TDBossLoopPieNavigationTimeoutSeconds);

	EnqueueBossLoopPieWait(*this, State, TEXT("Arena monster species finish loading"), [State]()
	{
		return IsBossLoopPieSpeciesLoaded(*State);
	}, TDBossLoopPieSpeciesLoadTimeoutSeconds);

	EnqueueStep([this, State]()
	{
		BeginBossLoopPieAttempt(*this, *State);
		return true;
	});

	EnqueueBossLoopPieWait(*this, State, TEXT("Attempt 1 seals the arena, starts the first wave delay and shows the wave banner"), [State]()
	{
		return IsBossLoopPieAttemptSealed(*State);
	}, TDBossLoopPieSealTimeoutSeconds);

	EnqueueWaitSeconds(TDBossLoopPieSealedScreenshotDelaySeconds);
	EnqueueBossLoopPieScreenshot(*this, State, TDBossLoopPieArenaSealedScreenshot);

	EnqueueBossLoopPieWait(*this, State, TEXT("The first wave is active with living monsters"), [State]()
	{
		return IsBossLoopPieFirstWaveFought(*State);
	}, TDBossLoopPieFirstWaveTimeoutSeconds);

	EnqueueWaitSeconds(TDBossLoopPieWaveScreenshotDelaySeconds);
	EnqueueBossLoopPieScreenshot(*this, State, TDBossLoopPieWaveOneScreenshot);

	EnqueueStep([this, State]()
	{
		DefeatBossLoopPiePlayer(*this, *State);
		return true;
	});

	EnqueueBossLoopPieWait(*this, State, TEXT("The player's death fails the encounter, shows the defeat banner and the death screen"), [State]()
	{
		return HasBossLoopPieDefeatPresentation(*State);
	}, TDBossLoopPieDefeatTimeoutSeconds);

	EnqueueWaitSeconds(TDBossLoopPieDefeatScreenshotDelaySeconds);
	EnqueueBossLoopPieScreenshot(*this, State, TDBossLoopPieDefeatScreenshot);

	EnqueueBossLoopPieWait(*this, State, TEXT("The encounter resets to idle with the arena unsealed and no hostile left near it"), [State]()
	{
		return IsBossLoopPieArenaReset(*State);
	}, TDBossLoopPieArenaResetTimeoutSeconds);

	EnqueueBossLoopPieWait(*this, State, TEXT("The player respawns as a new living pawn"), [State]()
	{
		return TryFindBossLoopPieRespawnedPlayer(*State);
	}, TDBossLoopPieRespawnTimeoutSeconds);

	EnqueueStep([this, State]()
	{
		VerifyBossLoopPieRespawnLocation(*this, *State);
		return true;
	});

	EnqueueBossLoopPieScreenshot(*this, State, TDBossLoopPieRespawnScreenshot);

	EnqueueStep([this, State]()
	{
		BeginBossLoopPieAttempt(*this, *State);
		return true;
	});

	EnqueueBossLoopPieWait(*this, State, TEXT("Attempt 2 starts the first wave delay"), [State]()
	{
		return IsBossLoopPieSecondAttemptStarted(*State);
	}, TDBossLoopPieSecondAttemptTimeoutSeconds);

	EnqueueStep([this, State]()
	{
		VerifyBossLoopPieSecondAttempt(*this, *State);
		EnqueueBossLoopPieWave(*this, State, 0);
		return true;
	});

	return true;
}

#endif
