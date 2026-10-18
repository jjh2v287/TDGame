#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SphereComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "MonsterAI/TDCombatTactics.h"
#include "MonsterAI/TDMonsterActionExecutor.h"
#include "MonsterAI/TDMonsterDefinitionLibrary.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MonsterAI/TDMonsterThinkSubsystem.h"
#include "MonsterAI/TDPackMorale.h"
#include "MoverSimulationTypes.h"

struct FTDPackBehaviorTestAccess
{
	static int32 FindSlotForActor(const UTDMonsterThinkSubsystem& Subsystem, const AActor* Actor)
	{
		for (const int32 SlotIndex : Subsystem.OrderedSlots)
		{
			if (Subsystem.Bodies[SlotIndex].Actor.Get() == Actor)
			{
				return SlotIndex;
			}
		}
		return INDEX_NONE;
	}

	static void SetTacticsRole(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex, const ETDTacticsRole Role)
	{
		Subsystem.Tactics[SlotIndex].Role = Role;
	}

	static int32 CountTokenHolders(const UTDMonsterThinkSubsystem& Subsystem, const ETDTacticsRole Role)
	{
		int32 HolderCount = 0;
		for (const int32 SlotIndex : Subsystem.OrderedSlots)
		{
			const FTDMonsterTacticsFragment& Fragment = Subsystem.Tactics[SlotIndex];
			HolderCount += Fragment.Role == Role && Fragment.TokenState != ETDTacticsTokenState::None ? 1 : 0;
		}
		return HolderCount;
	}

	static bool IsHoldingToken(const UTDMonsterThinkSubsystem& Subsystem, const AActor* Actor)
	{
		const int32 SlotIndex = FindSlotForActor(Subsystem, Actor);
		return SlotIndex != INDEX_NONE && Subsystem.Tactics[SlotIndex].TokenState != ETDTacticsTokenState::None;
	}
};

namespace
{
	constexpr float PackStepSeconds = 1.f / 64.f;
	constexpr int32 PackStepsPerSecond = 64;
	constexpr float PackPlayerMaxHealth = 100000.f;
	constexpr float PackLethalDamage = 1000000.f;
	constexpr float PackProbeHitDamage = 5.f;
	constexpr float PackSetupHitDamage = 8.f;
	constexpr float PackLowHealthRatio = 0.26f;
	constexpr int32 PackMaxSetupHits = 200;
	constexpr uint64 PackHashOffsetBasis = 14695981039346656037ull;
	constexpr uint64 PackHashPrime = 1099511628211ull;
	constexpr int32 PackMinHopSteps = 13;
	constexpr int32 PackMaxHopSteps = 15;
	constexpr int32 PackCallerMemorySteps = 192;
	constexpr int32 PackMinRetreatSteps = 32;
	constexpr int32 PackLeaderRunMinSteps = 126;
	constexpr int32 PackLeaderRunMaxSteps = 194;
	constexpr float PackLeaderRunMinGainCm = 150.f;
	constexpr int32 PackBurstRunMinSteps = 62;
	constexpr int32 PackBurstRunMaxSteps = 130;
	constexpr float PackPanicMaxTargetDistanceCm = 800.f;
	constexpr int32 PackPanicWindowSteps = 64;
	constexpr int32 PackReengageWindowSteps = 352;
	constexpr float PackReengageDistanceCm = 400.f;
	constexpr float PackChainSeconds = 8.f;
	constexpr float PackTacticsOffChainSeconds = 5.f;
	constexpr float PackExclusiveSeconds = 20.f;
	constexpr float PackBandSettleSeconds = 6.f;
	constexpr float PackBandTotalSeconds = 10.f;
	constexpr float PackBandMedianMinimumCm = 620.f;
	constexpr float PackBandTooCloseCm = 590.f;
	constexpr float PackBandTooCloseMaxFraction = 0.1f;
	constexpr float PackLowHealthSeconds = 30.f;
	constexpr float PackFleeMaxFraction = 0.3f;
	constexpr int32 PackFleeMaxRunSteps = 108;
	constexpr int32 PackLowHealthMinCastStarts = 8;
	constexpr int32 PackRetreatExpectedSteps = 96;
	constexpr int32 PackRetreatSlackSteps = 1;
	constexpr int32 PackCorneredMinSteps = 24;
	constexpr int32 PackCorneredMaxSteps = 26;
	constexpr int32 PackRetreatMaxProbeSteps = 200;
	constexpr float PackRetreatStartDistanceCm = 600.f;
	constexpr int32 PackDeathAlarmMaxSteps = 16;
	constexpr int32 PackDeathAlarmMinSteps = 9;
	constexpr int32 PackBurstGapSteps = 16;
	constexpr int32 PackSteadyLossGapSteps = 96;
	constexpr float PackRallySettleSeconds = 5.f;
	constexpr float PackSecondWaveObserveSeconds = 3.f;
	constexpr int32 PackRetaliationWindowSteps = 64;
	constexpr int32 PackRetaliationSettleSteps = 320;
	constexpr int32 PackRetaliationObserveSteps = 512;
	constexpr float PackRetaliationSearchSeconds = 10.f;
	constexpr float PackMixedMeleeMinFraction = 0.5f;
	constexpr float PackStalledRetreatStepCm = 1.f;

	int32 PackSecondsToSteps(const float Seconds)
	{
		return FMath::RoundToInt(Seconds * static_cast<float>(PackStepsPerSecond));
	}

	FVector MakePackRingLocation(const float Radius, const float AngleDegrees)
	{
		const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
		return FVector(Radius * FMath::Cos(AngleRadians), Radius * FMath::Sin(AngleRadians), 0.f);
	}

	FString ReadPackLineField(const FString& Line, const TCHAR* Key)
	{
		const FString Needle = FString::Printf(TEXT(" %s="), Key);
		const FString Padded = TEXT(" ") + Line;
		const int32 KeyIndex = Padded.Find(Needle, ESearchCase::CaseSensitive);
		if (KeyIndex == INDEX_NONE)
		{
			return FString();
		}
		const int32 ValueStart = KeyIndex + Needle.Len();
		int32 ValueEnd = Padded.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
		if (ValueEnd == INDEX_NONE)
		{
			ValueEnd = Padded.Len();
		}
		return Padded.Mid(ValueStart, ValueEnd - ValueStart);
	}

	FString RemovePackActorField(const FString& Line)
	{
		const int32 KeyIndex = Line.Find(TEXT(" actor="), ESearchCase::CaseSensitive);
		if (KeyIndex == INDEX_NONE)
		{
			return Line;
		}
		int32 ValueEnd = Line.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyIndex + 1);
		if (ValueEnd == INDEX_NONE)
		{
			ValueEnd = Line.Len();
		}
		return Line.Left(KeyIndex) + Line.Mid(ValueEnd);
	}

	uint64 AccumulatePackHash(const uint64 Seed, const FString& Text)
	{
		uint64 Hash = Seed;
		for (const TCHAR Character : Text)
		{
			const uint32 Code = static_cast<uint32>(Character);
			for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
			{
				Hash ^= static_cast<uint64>((Code >> (ByteIndex * 8)) & 0xFF);
				Hash *= PackHashPrime;
			}
		}
		return Hash;
	}

	float ComputePackMedian(TArray<float> Values)
	{
		if (Values.IsEmpty())
		{
			return 0.f;
		}
		Values.Sort();
		const int32 Middle = Values.Num() / 2;
		if (Values.Num() % 2 == 1)
		{
			return Values[Middle];
		}
		return (Values[Middle - 1] + Values[Middle]) * 0.5f;
	}

	struct FTDPackTestWorld
	{
		FTDPackTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDPackTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDPackTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ATDMonsterCharacter* SpawnMonster(const FName DefinitionId, const FVector& Location)
		{
			UTDMonsterSpeciesAsset* Species = NewObject<UTDMonsterSpeciesAsset>(World);
			Species->DefinitionId = DefinitionId;
			const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
			ATDMonsterCharacter* Monster = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
			Monster->SetSpecies(Species);
			Monster->GetMoverComponent()->StartingMovementMode = DefaultModeNames::Flying;
			Monster->FinishSpawning(SpawnTransform);
			return Monster;
		}

		void Tick()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, PackStepSeconds);
		}

		UWorld* World = nullptr;
	};

	APawn* SpawnPackPlayerPawn(UWorld& World, const FVector& Location)
	{
		APawn* PlayerPawn = World.SpawnActor<APawn>();
		USphereComponent* Sphere = NewObject<USphereComponent>(PlayerPawn);
		PlayerPawn->AddInstanceComponent(Sphere);
		PlayerPawn->SetRootComponent(Sphere);
		Sphere->SetSphereRadius(30.f);
		Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Sphere->SetCollisionObjectType(ECC_Pawn);
		Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
		Sphere->RegisterComponent();
		PlayerPawn->SetActorLocation(Location);
		UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(PlayerPawn);
		PlayerPawn->AddInstanceComponent(Combatant);
		FTDCombatStats Stats;
		Stats.TeamId = 1;
		Stats.BaseMaxHealth = PackPlayerMaxHealth;
		Combatant->SetStats(Stats);
		Combatant->RegisterComponent();
		APlayerController* PlayerController = World.SpawnActor<APlayerController>();
		PlayerController->SetActorTickEnabled(false);
		PlayerController->SetPawn(PlayerPawn);
		return PlayerPawn;
	}

	void HitPackMonster(ATDMonsterCharacter& Monster, APawn& PlayerPawn, const float Damage)
	{
		FTDDamageContext HitContext;
		HitContext.Caster = &PlayerPawn;
		const UTDCombatComponent* PlayerCombat = PlayerPawn.FindComponentByClass<UTDCombatComponent>();
		if (PlayerCombat)
		{
			HitContext.Stats = PlayerCombat->GetStats();
		}
		Monster.GetCombatComponent()->ReceiveDamage(Damage, ETDDamageElement::Physical, false, HitContext);
	}

	struct FTDPackSample
	{
		bool bIsPresent = false;
		bool bIsEngaged = false;
		bool bIsReturning = false;
		bool bIsCasting = false;
		bool bIsMoving = false;
		bool bIsFleeing = false;
		FString Alarm;
		FString Morale;
		float TargetDistance = 0.f;
	};

	struct FTDPackScenario
	{
		explicit FTDPackScenario(const FVector& PlayerLocation)
		{
			PlayerPawn = SpawnPackPlayerPawn(*Fixture.World, PlayerLocation);
			Subsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
		}

		int32 AddMonster(const FName DefinitionId, const FVector& Location)
		{
			ATDMonsterCharacter* Monster = Fixture.SpawnMonster(DefinitionId, Location);
			Monsters.Add(Monster);
			MonsterNames.Add(Monster ? Monster->GetName() : FString());
			return Monsters.Num() - 1;
		}

		bool IsReady() const
		{
			if (!PlayerPawn || !Subsystem || UGameplayStatics::GetPlayerPawn(Fixture.World, 0) != PlayerPawn)
			{
				return false;
			}
			for (const TWeakObjectPtr<ATDMonsterCharacter>& Monster : Monsters)
			{
				if (!Monster.IsValid())
				{
					return false;
				}
			}
			return Subsystem->GetMonsterDebugLines().Num() == Monsters.Num();
		}

		int32 GetLineCount() const
		{
			return Subsystem->GetMonsterDebugLines().Num();
		}

		int32 GetStepCount() const
		{
			return Steps.Num();
		}

		FVector GetMonsterLocation(const int32 MonsterIndex) const
		{
			const ATDMonsterCharacter* Monster = Monsters[MonsterIndex].Get();
			return Monster ? Monster->GetActorLocation() : FVector::ZeroVector;
		}

		bool SetRole(const int32 MonsterIndex, const ETDTacticsRole Role)
		{
			const int32 SlotIndex = FTDPackBehaviorTestAccess::FindSlotForActor(*Subsystem, Monsters[MonsterIndex].Get());
			if (SlotIndex == INDEX_NONE)
			{
				return false;
			}
			FTDPackBehaviorTestAccess::SetTacticsRole(*Subsystem, SlotIndex, Role);
			return true;
		}

		void Hit(const int32 MonsterIndex, const float Damage)
		{
			ATDMonsterCharacter* Monster = Monsters[MonsterIndex].Get();
			if (!Monster)
			{
				return;
			}
			HitPackMonster(*Monster, *PlayerPawn, Damage);
		}

		void TickAndRecord(const int32 StepCount)
		{
			for (int32 Step = 0; Step < StepCount; ++Step)
			{
				Fixture.Tick();
				RecordSample();
			}
		}

		void RecordSample()
		{
			TArray<FTDPackSample>& StepSamples = Steps.AddDefaulted_GetRef();
			StepSamples.SetNum(Monsters.Num());
			const TArray<FString> Lines = Subsystem->GetMonsterDebugLines();
			for (const FString& Line : Lines)
			{
				RunningHash = AccumulatePackHash(RunningHash, RemovePackActorField(Line) + TEXT("\n"));
				const int32 MonsterIndex = MonsterNames.IndexOfByKey(ReadPackLineField(Line, TEXT("actor")));
				if (MonsterIndex == INDEX_NONE)
				{
					++UnmatchedLineCount;
					continue;
				}
				const FString State = ReadPackLineField(Line, TEXT("state"));
				FTDPackSample& MonsterSample = StepSamples[MonsterIndex];
				MonsterSample.bIsPresent = true;
				MonsterSample.bIsEngaged = ReadPackLineField(Line, TEXT("engaged")) == TEXT("1");
				MonsterSample.bIsReturning = ReadPackLineField(Line, TEXT("engagement")) == TEXT("Returning");
				MonsterSample.bIsCasting = State == TEXT("Cast");
				MonsterSample.bIsMoving = State == TEXT("Move");
				MonsterSample.bIsFleeing = ReadPackLineField(Line, TEXT("action")) == TEXT("Flee");
				MonsterSample.Alarm = ReadPackLineField(Line, TEXT("alarm"));
				MonsterSample.Morale = ReadPackLineField(Line, TEXT("morale"));
				const ATDMonsterCharacter* Monster = Monsters[MonsterIndex].Get();
				MonsterSample.TargetDistance = Monster ? static_cast<float>(FVector::Dist2D(Monster->GetActorLocation(), PlayerPawn->GetActorLocation())) : 0.f;
			}
			StepHashes.Add(RunningHash);
		}

		FTDPackTestWorld Fixture;
		APawn* PlayerPawn = nullptr;
		UTDMonsterThinkSubsystem* Subsystem = nullptr;
		TArray<TWeakObjectPtr<ATDMonsterCharacter>> Monsters;
		TArray<FString> MonsterNames;
		TArray<TArray<FTDPackSample>> Steps;
		TArray<uint64> StepHashes;
		uint64 RunningHash = PackHashOffsetBasis;
		int32 UnmatchedLineCount = 0;
	};

	bool CheckPackScenarioReady(FAutomationTestBase& Test, const FTDPackScenario& Scenario)
	{
		if (!Scenario.IsReady())
		{
			Test.AddError(FString::Printf(TEXT("scenario setup failed: player %d, subsystem %d, spawned %d monsters"), Scenario.PlayerPawn ? 1 : 0, Scenario.Subsystem ? 1 : 0, Scenario.Monsters.Num()));
			return false;
		}
		return true;
	}

	bool CheckPackTacticsActive(FAutomationTestBase& Test)
	{
		IConsoleVariable* TacticsVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.MonsterAI.Tactics"));
		if (!Test.TestNotNull(TEXT("the tactics console variable exists"), TacticsVariable))
		{
			return false;
		}
		return Test.TestEqual(TEXT("the tactics console variable is 1 so the pack rules run"), TacticsVariable->GetInt(), 1);
	}

	void CheckPackAllLinesMatched(FAutomationTestBase& Test, const FTDPackScenario& Scenario)
	{
		Test.TestEqual(FString::Printf(TEXT("every debug line belongs to a spawned monster (%d unmatched)"), Scenario.UnmatchedLineCount), Scenario.UnmatchedLineCount, 0);
	}

	bool KillPackMonsterChecked(FAutomationTestBase& Test, FTDPackScenario& Scenario, const int32 MonsterIndex)
	{
		const int32 LineCountBefore = Scenario.GetLineCount();
		Scenario.Hit(MonsterIndex, PackLethalDamage);
		const int32 LineCountAfter = Scenario.GetLineCount();
		return Test.TestTrue(FString::Printf(TEXT("a lethal hit on monster %d lowers the debug line count (%d -> %d)"), MonsterIndex, LineCountBefore, LineCountAfter), LineCountAfter == LineCountBefore - 1);
	}

	int32 AddPackGoblinRing(FTDPackScenario& Scenario, const int32 MonsterCount, const float Radius, const float FirstAngleDegrees)
	{
		const int32 FirstIndex = Scenario.Monsters.Num();
		for (int32 RingIndex = 0; RingIndex < MonsterCount; ++RingIndex)
		{
			const float AngleDegrees = FirstAngleDegrees + 360.f * static_cast<float>(RingIndex) / static_cast<float>(MonsterCount);
			Scenario.AddMonster(TEXT("Goblin_Melee"), MakePackRingLocation(Radius, AngleDegrees));
		}
		return FirstIndex;
	}

	int32 FindPackFirstSample(const FTDPackScenario& Scenario, const int32 MonsterIndex, const int32 FromStep, TFunctionRef<bool(const FTDPackSample&)> Predicate)
	{
		for (int32 StepIndex = FMath::Max(0, FromStep); StepIndex < Scenario.Steps.Num(); ++StepIndex)
		{
			if (Predicate(Scenario.Steps[StepIndex][MonsterIndex]))
			{
				return StepIndex;
			}
		}
		return INDEX_NONE;
	}

	TArray<int32> FindPackFirstEngagedSteps(const FTDPackScenario& Scenario)
	{
		TArray<int32> EngagedSteps;
		for (int32 MonsterIndex = 0; MonsterIndex < Scenario.Monsters.Num(); ++MonsterIndex)
		{
			EngagedSteps.Add(FindPackFirstSample(Scenario, MonsterIndex, 0, [](const FTDPackSample& Candidate) { return Candidate.bIsEngaged; }));
		}
		return EngagedSteps;
	}

	int32 CountPackCastStarts(const FTDPackScenario& Scenario, const int32 MonsterIndex, const int32 FromStep, const int32 ToStep)
	{
		int32 StartCount = 0;
		const int32 EndStep = FMath::Min(ToStep, Scenario.Steps.Num());
		for (int32 StepIndex = FMath::Max(0, FromStep); StepIndex < EndStep; ++StepIndex)
		{
			const bool bIsCasting = Scenario.Steps[StepIndex][MonsterIndex].bIsCasting;
			const bool bWasCasting = StepIndex > 0 && Scenario.Steps[StepIndex - 1][MonsterIndex].bIsCasting;
			StartCount += bIsCasting && !bWasCasting ? 1 : 0;
		}
		return StartCount;
	}

	int32 CountPackCastingMonsters(const FTDPackScenario& Scenario, const int32 StepIndex)
	{
		int32 CastingCount = 0;
		for (const FTDPackSample& MonsterSample : Scenario.Steps[StepIndex])
		{
			CastingCount += MonsterSample.bIsCasting ? 1 : 0;
		}
		return CastingCount;
	}

	int32 CountPackEngagedMonsters(const FTDPackScenario& Scenario, const TArray<int32>& MonsterIndices, const int32 StepIndex)
	{
		int32 EngagedCount = 0;
		for (const int32 MonsterIndex : MonsterIndices)
		{
			EngagedCount += Scenario.Steps[StepIndex][MonsterIndex].bIsEngaged ? 1 : 0;
		}
		return EngagedCount;
	}

	enum class ETDPackMoraleKind : uint8
	{
		NonSteady,
		FearOrPanic,
		Rallied
	};

	bool MatchesPackMoraleKind(const FTDPackSample& MonsterSample, const ETDPackMoraleKind Kind)
	{
		if (!MonsterSample.bIsPresent)
		{
			return false;
		}
		if (Kind == ETDPackMoraleKind::NonSteady)
		{
			return !(MonsterSample.Morale == TEXT("steady"));
		}
		if (Kind == ETDPackMoraleKind::Rallied)
		{
			return MonsterSample.Morale == TEXT("rallied");
		}
		return MonsterSample.Morale == TEXT("fear") || MonsterSample.Morale == TEXT("panic");
	}

	int32 CountPackMoraleSamples(const FTDPackScenario& Scenario, const TArray<int32>& MonsterIndices, const int32 FromStep, const int32 ToStep, const ETDPackMoraleKind Kind)
	{
		int32 SampleCount = 0;
		const int32 EndStep = FMath::Min(ToStep, Scenario.Steps.Num());
		for (int32 StepIndex = FMath::Max(0, FromStep); StepIndex < EndStep; ++StepIndex)
		{
			for (const int32 MonsterIndex : MonsterIndices)
			{
				SampleCount += MatchesPackMoraleKind(Scenario.Steps[StepIndex][MonsterIndex], Kind) ? 1 : 0;
			}
		}
		return SampleCount;
	}

	int32 CountPackReturningSamples(const FTDPackScenario& Scenario, const TArray<int32>& MonsterIndices, const int32 FromStep, const int32 ToStep)
	{
		int32 ReturningSampleCount = 0;
		const int32 EndStep = FMath::Min(ToStep, Scenario.Steps.Num());
		for (int32 StepIndex = FMath::Max(0, FromStep); StepIndex < EndStep; ++StepIndex)
		{
			for (const int32 MonsterIndex : MonsterIndices)
			{
				ReturningSampleCount += Scenario.Steps[StepIndex][MonsterIndex].bIsReturning ? 1 : 0;
			}
		}
		return ReturningSampleCount;
	}

	struct FTDPackRetreatRun
	{
		int32 MonsterIndex = INDEX_NONE;
		int32 StartStep = INDEX_NONE;
		int32 LengthSteps = 0;
		float DistanceGain = 0.f;

		bool IsMeasuredRetreat() const
		{
			return LengthSteps >= PackMinRetreatSteps && DistanceGain > 0.f;
		}
	};

	bool IsPackPanicSample(const FTDPackSample& MonsterSample)
	{
		return MonsterSample.bIsPresent && MonsterSample.bIsMoving && MonsterSample.Morale == TEXT("panic");
	}

	TArray<FTDPackRetreatRun> FindPackPanicRuns(const FTDPackScenario& Scenario, const int32 MonsterIndex, const int32 FromStep, const int32 ToStep)
	{
		TArray<FTDPackRetreatRun> Runs;
		const int32 EndStep = FMath::Min(ToStep, Scenario.Steps.Num());
		for (int32 StepIndex = FMath::Max(0, FromStep); StepIndex < EndStep; ++StepIndex)
		{
			const FTDPackSample& MonsterSample = Scenario.Steps[StepIndex][MonsterIndex];
			if (!IsPackPanicSample(MonsterSample))
			{
				continue;
			}
			const bool bContinuesRun = !Runs.IsEmpty() && Runs.Last().StartStep + Runs.Last().LengthSteps == StepIndex;
			if (!bContinuesRun)
			{
				FTDPackRetreatRun& NewRun = Runs.AddDefaulted_GetRef();
				NewRun.MonsterIndex = MonsterIndex;
				NewRun.StartStep = StepIndex;
				NewRun.LengthSteps = 1;
				continue;
			}
			FTDPackRetreatRun& OpenRun = Runs.Last();
			++OpenRun.LengthSteps;
			OpenRun.DistanceGain = MonsterSample.TargetDistance - Scenario.Steps[OpenRun.StartStep][MonsterIndex].TargetDistance;
		}
		return Runs;
	}

	int32 FindPackReengageStep(const FTDPackScenario& Scenario, const int32 MonsterIndex, const int32 EventStep, const int32 EndStep)
	{
		const int32 LimitStep = FMath::Min(EndStep, Scenario.Steps.Num());
		int32 LastPanicStep = EventStep - 1;
		for (int32 StepIndex = EventStep; StepIndex < LimitStep; ++StepIndex)
		{
			LastPanicStep = IsPackPanicSample(Scenario.Steps[StepIndex][MonsterIndex]) ? StepIndex : LastPanicStep;
		}
		for (int32 StepIndex = LastPanicStep + 1; StepIndex < LimitStep; ++StepIndex)
		{
			const FTDPackSample& MonsterSample = Scenario.Steps[StepIndex][MonsterIndex];
			if (MonsterSample.bIsPresent && (MonsterSample.bIsCasting || MonsterSample.TargetDistance <= PackReengageDistanceCm))
			{
				return StepIndex;
			}
		}
		return INDEX_NONE;
	}

	struct FTDPackPanicSummary
	{
		int32 PanickedCount = 0;
		int32 MeasuredRetreatCount = 0;
		int32 LongestRunSteps = 0;
		int32 ShortestPanickedRunSteps = MAX_int32;
		float SmallestPanickedGainCm = TNumericLimits<float>::Max();
		float MaxPanicTargetDistanceCm = 0.f;
		int32 ReturningSampleCount = 0;
		TArray<int32> NotReengagedIndices;
		FString RunDescription;
	};

	FTDPackPanicSummary SummarizePackPanic(const FTDPackScenario& Scenario, const TArray<int32>& MonsterIndices, const int32 EventStep)
	{
		FTDPackPanicSummary Summary;
		const int32 PanicWindowEnd = EventStep + PackPanicWindowSteps;
		const int32 ReengageWindowEnd = EventStep + PackReengageWindowSteps;
		const int32 LimitStep = FMath::Min(ReengageWindowEnd, Scenario.Steps.Num());
		Summary.ReturningSampleCount = CountPackReturningSamples(Scenario, MonsterIndices, EventStep, ReengageWindowEnd);
		for (const int32 MonsterIndex : MonsterIndices)
		{
			bool bPanickedInWindow = false;
			bool bHasMeasuredRetreat = false;
			FTDPackRetreatRun LongestWindowRun;
			for (const FTDPackRetreatRun& Run : FindPackPanicRuns(Scenario, MonsterIndex, EventStep, ReengageWindowEnd))
			{
				Summary.LongestRunSteps = FMath::Max(Summary.LongestRunSteps, Run.LengthSteps);
				Summary.RunDescription += FString::Printf(TEXT(" m%d[+%d len %d gain %.0f]"), MonsterIndex, Run.StartStep - EventStep, Run.LengthSteps, Run.DistanceGain);
				if (Run.StartStep >= PanicWindowEnd)
				{
					continue;
				}
				bPanickedInWindow = true;
				bHasMeasuredRetreat = bHasMeasuredRetreat || Run.IsMeasuredRetreat();
				if (Run.LengthSteps > LongestWindowRun.LengthSteps)
				{
					LongestWindowRun = Run;
				}
			}
			if (bPanickedInWindow)
			{
				Summary.ShortestPanickedRunSteps = FMath::Min(Summary.ShortestPanickedRunSteps, LongestWindowRun.LengthSteps);
				Summary.SmallestPanickedGainCm = FMath::Min(Summary.SmallestPanickedGainCm, LongestWindowRun.DistanceGain);
			}
			Summary.PanickedCount += bPanickedInWindow ? 1 : 0;
			Summary.MeasuredRetreatCount += bHasMeasuredRetreat ? 1 : 0;
			for (int32 StepIndex = FMath::Max(0, EventStep); StepIndex < LimitStep; ++StepIndex)
			{
				const FTDPackSample& MonsterSample = Scenario.Steps[StepIndex][MonsterIndex];
				Summary.MaxPanicTargetDistanceCm = IsPackPanicSample(MonsterSample) ? FMath::Max(Summary.MaxPanicTargetDistanceCm, MonsterSample.TargetDistance) : Summary.MaxPanicTargetDistanceCm;
			}
			if (FindPackReengageStep(Scenario, MonsterIndex, EventStep, ReengageWindowEnd) == INDEX_NONE)
			{
				Summary.NotReengagedIndices.Add(MonsterIndex);
			}
		}
		return Summary;
	}

	TSharedPtr<const FTDResolvedMonsterDefinition> ResolvePackProbeDefinition(const FString& Text, TArray<FString>& OutErrors)
	{
		TMap<FString, FString> Texts;
		Texts.Add(TEXT("PackProbe.json"), Text);
		TMap<FName, TSharedPtr<const FTDResolvedMonsterDefinition>> Definitions;
		UTDMonsterDefinitionLibrary::ResolveDefinitionTexts(Texts, [](FName) -> UTDDamageDefinition* { return GetMutableDefault<UTDDamageDefinition>(); }, Definitions, OutErrors);
		const TSharedPtr<const FTDResolvedMonsterDefinition>* Definition = Definitions.Find(FName(TEXT("PackProbe")));
		return Definition ? *Definition : nullptr;
	}

	FString MakePackRoleProbeText(const FString& TopLevelFields)
	{
		return FString::Printf(TEXT(R"({ "schema": 1, "id": "PackProbe", %s
  "stats": { "max_health": 100, "attack_power": 1, "armor": 0, "move_speed": 100, "team": 2 },
  "abilities": { "Nip": { "spell": "DA_Any", "range": 150, "cooldown": 1 } },
  "actions": [ { "id": "Idle", "do": "Wait", "weight": 1 } ] })"), *TopLevelFields);
	}

	bool CheckPackRoleProbe(FAutomationTestBase& Test, const TCHAR* Label, const FString& TopLevelFields, const int32 ExpectedRoleValue)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = ResolvePackProbeDefinition(MakePackRoleProbeText(TopLevelFields), Errors);
		const FString FirstError = Errors.IsEmpty() ? FString(TEXT("none")) : Errors[0];
		const bool bIsClean = Test.TestTrue(FString::Printf(TEXT("%s resolves without errors (errors %d, first: %s)"), Label, Errors.Num(), *FirstError), Errors.IsEmpty() && Definition.IsValid());
		if (!bIsClean || ExpectedRoleValue < 0)
		{
			return bIsClean;
		}
		const int32 RoleValue = static_cast<int32>(FTDCombatTactics::MakeFragment(*Definition).Role);
		return Test.TestEqual(FString::Printf(TEXT("%s becomes tactics role %d"), Label, ExpectedRoleValue), RoleValue, ExpectedRoleValue);
	}

	void CheckPackMoraleFlags(FAutomationTestBase& Test, const FString& Label, const TSharedPtr<const FTDResolvedMonsterDefinition>& Definition, const bool bExpectedElite, const bool bExpectedLeader, const bool bExpectedImmune)
	{
		if (!Test.TestTrue(FString::Printf(TEXT("%s resolves"), *Label), Definition.IsValid()))
		{
			return;
		}
		const FTDMonsterMoraleFragment Fragment = FTDPackMorale::MakeFragment(*Definition);
		Test.TestTrue(FString::Printf(TEXT("%s: the definition elite flag is %d (got %d)"), *Label, bExpectedElite ? 1 : 0, Definition->bIsElite ? 1 : 0), Definition->bIsElite == bExpectedElite);
		Test.TestTrue(FString::Printf(TEXT("%s: the morale fragment leader flag is %d (got %d)"), *Label, bExpectedLeader ? 1 : 0, Fragment.bIsLeader ? 1 : 0), Fragment.bIsLeader == bExpectedLeader);
		Test.TestTrue(FString::Printf(TEXT("%s: the morale fragment immune flag is %d (got %d)"), *Label, bExpectedImmune ? 1 : 0, Fragment.bIsImmune ? 1 : 0), Fragment.bIsImmune == bExpectedImmune);
	}

	void CheckPackMoraleProbe(FAutomationTestBase& Test, const TCHAR* Label, const FString& TopLevelFields, const bool bExpectedElite, const bool bExpectedLeader, const bool bExpectedImmune)
	{
		TArray<FString> Errors;
		const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = ResolvePackProbeDefinition(MakePackRoleProbeText(TopLevelFields), Errors);
		CheckPackMoraleFlags(Test, FString(Label), Definition, bExpectedElite, bExpectedLeader, bExpectedImmune);
	}

	FString MakePackFleeProbeText()
	{
		return TEXT(R"({ "schema": 1, "id": "PackProbe",
  "stats": { "max_health": 100, "attack_power": 1, "armor": 0, "move_speed": 100, "team": 2 },
  "actions": [
    { "id": "Flee", "do": "MoveAway", "args": { "target": "Player", "seconds": 1.5 }, "weight": 1.0 },
    { "id": "Idle", "do": "Wait", "weight": 0.05 } ] })");
	}

	struct FTDPackRetreatDrive
	{
		bool bDidStartMoving = false;
		int32 IdleStep = INDEX_NONE;
	};

	FTDPackRetreatDrive DrivePackTimedRetreat(const TSharedPtr<const FTDResolvedMonsterDefinition>& Definition, AActor* Target, const float StepCm)
	{
		FTDPackRetreatDrive Drive;
		FTDMonsterBrainFragment Brain;
		Brain.Definition = Definition;
		FTDMonsterActionExecutor::ResetForDefinition(Brain);
		FTDMonsterStepContext Context;
		Context.Target = Target;
		Context.TargetLocation = FVector::ZeroVector;
		Context.SelfLocation = FVector(PackRetreatStartDistanceCm, 0.f, 0.f);
		FTDBrainDecision Decision;
		Decision.ActionIndex = Definition->GetActions(0).IndexOfByPredicate([](const FTDResolvedAction& Candidate) { return Candidate.Id == TEXT("Flee"); });
		Decision.Score = 1.f;
		FTDMonsterActionExecutor::ApplyDecision(Brain, Decision, Context);
		Drive.bDidStartMoving = Brain.State == ETDMonsterFsmState::Move && Brain.ActivePrimitive == ETDMonsterPrimitive::MoveAway;
		if (!Drive.bDidStartMoving)
		{
			return Drive;
		}
		for (int32 StepIndex = 1; StepIndex <= PackRetreatMaxProbeSteps; ++StepIndex)
		{
			Context.SelfLocation.X += StepCm;
			FTDMonsterActionExecutor::StepState(Brain, Context);
			if (Brain.State == ETDMonsterFsmState::Idle)
			{
				Drive.IdleStep = StepIndex;
				return Drive;
			}
		}
		return Drive;
	}

	bool CheckPackExclusiveRole(FAutomationTestBase& Test, const FName DefinitionId, const int32 MonsterCount, const float Radius, const ETDTacticsRole Role, const TCHAR* Label)
	{
		if (!CheckPackTacticsActive(Test))
		{
			return false;
		}
		FTDPackScenario Scenario(FVector::ZeroVector);
		for (int32 MonsterIndex = 0; MonsterIndex < MonsterCount; ++MonsterIndex)
		{
			Scenario.AddMonster(DefinitionId, MakePackRingLocation(Radius, 360.f * static_cast<float>(MonsterIndex) / static_cast<float>(MonsterCount)));
		}
		if (!CheckPackScenarioReady(Test, Scenario))
		{
			return false;
		}
		for (int32 MonsterIndex = 0; MonsterIndex < MonsterCount; ++MonsterIndex)
		{
			if (!Test.TestTrue(FString::Printf(TEXT("%s: monster %d is registered and takes the role"), Label, MonsterIndex), Scenario.SetRole(MonsterIndex, Role)))
			{
				return false;
			}
		}
		int32 MaxHolderCount = 0;
		int32 MaxHolderStep = INDEX_NONE;
		int32 MaxCastCount = 0;
		int32 MaxCastStep = INDEX_NONE;
		const int32 StepCount = PackSecondsToSteps(PackExclusiveSeconds);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Scenario.TickAndRecord(1);
			const int32 HolderCount = FTDPackBehaviorTestAccess::CountTokenHolders(*Scenario.Subsystem, Role);
			const int32 CastCount = CountPackCastingMonsters(Scenario, StepIndex);
			MaxHolderStep = HolderCount > MaxHolderCount ? StepIndex : MaxHolderStep;
			MaxHolderCount = FMath::Max(MaxHolderCount, HolderCount);
			MaxCastStep = CastCount > MaxCastCount ? StepIndex : MaxCastStep;
			MaxCastCount = FMath::Max(MaxCastCount, CastCount);
		}
		FString CastStartText;
		int32 SilentMonsterCount = 0;
		for (int32 MonsterIndex = 0; MonsterIndex < MonsterCount; ++MonsterIndex)
		{
			const int32 CastStartCount = CountPackCastStarts(Scenario, MonsterIndex, 0, StepCount);
			SilentMonsterCount += CastStartCount == 0 ? 1 : 0;
			CastStartText += FString::Printf(TEXT(" %d"), CastStartCount);
		}
		Test.AddInfo(FString::Printf(TEXT("[pack] %s: max token holders %d (step %d), max concurrent casts %d (step %d), cast starts per monster:%s"), Label, MaxHolderCount, MaxHolderStep, MaxCastCount, MaxCastStep, *CastStartText));
		Test.TestTrue(FString::Printf(TEXT("%s: at most one monster of the exclusive role holds a token on every step (max %d, first at step %d)"), Label, MaxHolderCount, MaxHolderStep), MaxHolderCount <= 1);
		Test.TestTrue(FString::Printf(TEXT("%s: at most one monster of the exclusive role casts at a time (max %d, first at step %d)"), Label, MaxCastCount, MaxCastStep), MaxCastCount <= 1);
		Test.TestTrue(FString::Printf(TEXT("%s: every monster casts at least once in %.0f s (%d never cast; starts:%s)"), Label, PackExclusiveSeconds, SilentMonsterCount, *CastStartText), SilentMonsterCount == 0);
		Test.TestEqual(FString::Printf(TEXT("%s: the exclusive role does reach one token holder, so the cap is exercised (max %d)"), Label, MaxHolderCount), MaxHolderCount, 1);
		CheckPackAllLinesMatched(Test, Scenario);
		return true;
	}

	int32 FindPackCastStartMonster(const FTDPackScenario& Scenario)
	{
		const int32 LastStep = Scenario.GetStepCount() - 1;
		if (LastStep < 1)
		{
			return INDEX_NONE;
		}
		for (int32 MonsterIndex = 0; MonsterIndex < Scenario.Monsters.Num(); ++MonsterIndex)
		{
			if (Scenario.Steps[LastStep][MonsterIndex].bIsCasting && !Scenario.Steps[LastStep - 1][MonsterIndex].bIsCasting)
			{
				return MonsterIndex;
			}
		}
		return INDEX_NONE;
	}

	TArray<int32> FindPackTokenlessMonsters(const FTDPackScenario& Scenario)
	{
		TArray<int32> TokenlessIndices;
		for (int32 MonsterIndex = 0; MonsterIndex < Scenario.Monsters.Num(); ++MonsterIndex)
		{
			if (!FTDPackBehaviorTestAccess::IsHoldingToken(*Scenario.Subsystem, Scenario.Monsters[MonsterIndex].Get()))
			{
				TokenlessIndices.Add(MonsterIndex);
			}
		}
		return TokenlessIndices;
	}

	struct FTDPackDeterminismRun
	{
		TArray<uint64> StepHashes;
		int32 FinalLineCount = 0;
		int32 FearSampleCount = 0;
		int32 HopTwoSampleCount = 0;
		int32 MaxChargerHolderCount = 0;
		int32 UnmatchedLineCount = 0;
	};

	void TickPackDeterminismSteps(FTDPackScenario& Scenario, FTDPackDeterminismRun& Run, const int32 StepCount)
	{
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			Scenario.TickAndRecord(1);
			for (const FTDPackSample& MonsterSample : Scenario.Steps.Last())
			{
				Run.FearSampleCount += MatchesPackMoraleKind(MonsterSample, ETDPackMoraleKind::FearOrPanic) ? 1 : 0;
				Run.HopTwoSampleCount += MonsterSample.bIsPresent && MonsterSample.Alarm == TEXT("hop2") ? 1 : 0;
			}
			Run.MaxChargerHolderCount = FMath::Max(Run.MaxChargerHolderCount, FTDPackBehaviorTestAccess::CountTokenHolders(*Scenario.Subsystem, ETDTacticsRole::Charger));
		}
	}

	bool RunPackDeterminismScenario(FTDPackDeterminismRun& OutRun)
	{
		FTDPackScenario Scenario(FVector::ZeroVector);
		Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 0.f, 0.f));
		Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 700.f, 0.f));
		Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 1400.f, 0.f));
		Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 2100.f, 0.f));
		const int32 FirstRingIndex = Scenario.Monsters.Num();
		for (int32 RingIndex = 0; RingIndex < 5; ++RingIndex)
		{
			const FName DefinitionId = RingIndex == 0 ? FName(TEXT("StoneGolem")) : FName(TEXT("Goblin_Melee"));
			Scenario.AddMonster(DefinitionId, MakePackRingLocation(450.f, 72.f * static_cast<float>(RingIndex)));
		}
		if (!Scenario.IsReady())
		{
			return false;
		}
		const bool bAreChargersAssigned = Scenario.SetRole(FirstRingIndex + 2, ETDTacticsRole::Charger) && Scenario.SetRole(FirstRingIndex + 3, ETDTacticsRole::Charger);
		if (!bAreChargersAssigned)
		{
			return false;
		}
		TickPackDeterminismSteps(Scenario, OutRun, PackSecondsToSteps(2.f));
		Scenario.Hit(FirstRingIndex, PackLethalDamage);
		TickPackDeterminismSteps(Scenario, OutRun, PackBurstGapSteps);
		Scenario.Hit(FirstRingIndex + 1, PackLethalDamage);
		TickPackDeterminismSteps(Scenario, OutRun, PackSecondsToSteps(PackChainSeconds));
		OutRun.StepHashes = Scenario.StepHashes;
		OutRun.FinalLineCount = Scenario.GetLineCount();
		OutRun.UnmatchedLineCount = Scenario.UnmatchedLineCount;
		return true;
	}

	struct FTDPackEventOrderRun
	{
		TArray<uint64> StepHashes;
		int32 FinalLineCount = 0;
		float SmallestHitHealthLoss = 0.f;
		int32 UnmatchedLineCount = 0;
	};

	bool RunPackEventOrderScenario(const bool bIsReversed, FTDPackEventOrderRun& OutRun)
	{
		constexpr int32 FirstHitIndex = 1;
		constexpr int32 FirstKillIndex = 2;
		constexpr int32 SecondKillIndex = 3;
		constexpr int32 SecondHitIndex = 4;
		FTDPackScenario Scenario(FVector::ZeroVector);
		for (int32 ColumnIndex = 0; ColumnIndex < 6; ++ColumnIndex)
		{
			Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(2000.f, -1000.f + 400.f * static_cast<float>(ColumnIndex), 0.f));
		}
		if (!Scenario.IsReady())
		{
			return false;
		}
		Scenario.TickAndRecord(8);
		const UTDCombatComponent* FirstHitCombat = Scenario.Monsters[FirstHitIndex]->GetCombatComponent();
		const UTDCombatComponent* SecondHitCombat = Scenario.Monsters[SecondHitIndex]->GetCombatComponent();
		const float FirstHealthBefore = FirstHitCombat->GetCurrentHealth();
		const float SecondHealthBefore = SecondHitCombat->GetCurrentHealth();
		if (!bIsReversed)
		{
			Scenario.Hit(FirstHitIndex, PackProbeHitDamage);
			Scenario.Hit(SecondHitIndex, PackProbeHitDamage);
			Scenario.Hit(FirstKillIndex, PackLethalDamage);
			Scenario.Hit(SecondKillIndex, PackLethalDamage);
		}
		else
		{
			Scenario.Hit(SecondKillIndex, PackLethalDamage);
			Scenario.Hit(FirstKillIndex, PackLethalDamage);
			Scenario.Hit(SecondHitIndex, PackProbeHitDamage);
			Scenario.Hit(FirstHitIndex, PackProbeHitDamage);
		}
		OutRun.SmallestHitHealthLoss = FMath::Min(FirstHealthBefore - FirstHitCombat->GetCurrentHealth(), SecondHealthBefore - SecondHitCombat->GetCurrentHealth());
		Scenario.TickAndRecord(PackSecondsToSteps(6.f));
		OutRun.StepHashes = Scenario.StepHashes;
		OutRun.FinalLineCount = Scenario.GetLineCount();
		OutRun.UnmatchedLineCount = Scenario.UnmatchedLineCount;
		return true;
	}

	int32 CountPackDifferingSteps(const TArray<uint64>& FirstHashes, const TArray<uint64>& SecondHashes, int32& OutFirstDifferingStep)
	{
		int32 DifferingSteps = 0;
		OutFirstDifferingStep = INDEX_NONE;
		const int32 CommonSteps = FMath::Min(FirstHashes.Num(), SecondHashes.Num());
		for (int32 StepIndex = 0; StepIndex < CommonSteps; ++StepIndex)
		{
			if (FirstHashes[StepIndex] == SecondHashes[StepIndex])
			{
				continue;
			}
			++DifferingSteps;
			OutFirstDifferingStep = OutFirstDifferingStep == INDEX_NONE ? StepIndex : OutFirstDifferingStep;
		}
		return DifferingSteps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackAlarmRipplesTwoHopsTest, "TDGame.MonsterAI.Pack.AlarmRipplesTwoHops", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackAlarmRipplesTwoHopsTest::RunTest(const FString& Parameters)
{
	FTDPackScenario Scenario(FVector::ZeroVector);
	Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 0.f, 0.f));
	Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 700.f, 0.f));
	Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 1400.f, 0.f));
	Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 2100.f, 0.f));
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	const float EngageRadius = UTDMonsterThinkSubsystem::EngageRadius;
	const float FarthestDistance = static_cast<float>(FVector::Dist2D(Scenario.GetMonsterLocation(3), FVector::ZeroVector));
	const float SecondDistance = static_cast<float>(FVector::Dist2D(Scenario.GetMonsterLocation(1), FVector::ZeroVector));
	TestTrue(FString::Printf(TEXT("goblins B and D start outside the sight radius (B %.0f, D %.0f, radius %.0f)"), SecondDistance, FarthestDistance, EngageRadius), SecondDistance > EngageRadius && FarthestDistance > EngageRadius);
	Scenario.TickAndRecord(PackSecondsToSteps(PackChainSeconds));
	const TArray<int32> EngagedSteps = FindPackFirstEngagedSteps(Scenario);
	AddInfo(FString::Printf(TEXT("[pack] first engaged steps A %d, B %d, C %d, D %d"), EngagedSteps[0], EngagedSteps[1], EngagedSteps[2], EngagedSteps[3]));
	if (!TestTrue(TEXT("goblin A (1400,0) engages by sight"), EngagedSteps[0] != INDEX_NONE))
	{
		return false;
	}
	const int32 DelayAB = EngagedSteps[1] - EngagedSteps[0];
	TestTrue(FString::Printf(TEXT("B engages %d..%d steps after A (A at %d, B at %d)"), PackMinHopSteps, PackMaxHopSteps, EngagedSteps[0], EngagedSteps[1]), EngagedSteps[1] != INDEX_NONE && DelayAB >= PackMinHopSteps && DelayAB <= PackMaxHopSteps);
	const int32 DelayBC = EngagedSteps[2] - EngagedSteps[1];
	TestTrue(FString::Printf(TEXT("C engages %d..%d steps after B (B at %d, C at %d)"), PackMinHopSteps, PackMaxHopSteps, EngagedSteps[1], EngagedSteps[2]), EngagedSteps[1] != INDEX_NONE && EngagedSteps[2] != INDEX_NONE && DelayBC >= PackMinHopSteps && DelayBC <= PackMaxHopSteps);
	const bool bIsDEngagedEarly = EngagedSteps[3] != INDEX_NONE && (EngagedSteps[2] == INDEX_NONE || EngagedSteps[3] < EngagedSteps[2] + PackCallerMemorySteps);
	TestFalse(FString::Printf(TEXT("D stays calm for 3 s after C engages (C at %d, D at %d)"), EngagedSteps[2], EngagedSteps[3]), bIsDEngagedEarly);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackOnlyFirstDiscovererCallsTest, "TDGame.MonsterAI.Pack.OnlyFirstDiscovererCalls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackOnlyFirstDiscovererCallsTest::RunTest(const FString& Parameters)
{
	FTDPackScenario Scenario(FVector::ZeroVector);
	const int32 FirstDiscoverer = Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1300.f, 100.f, 0.f));
	const int32 SecondDiscoverer = Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1300.f, -100.f, 0.f));
	const int32 Sleeper = Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 700.f, 0.f));
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(4.f));
	const TArray<int32> EngagedSteps = FindPackFirstEngagedSteps(Scenario);
	AddInfo(FString::Printf(TEXT("[pack] first engaged steps %d, %d, sleeper %d"), EngagedSteps[FirstDiscoverer], EngagedSteps[SecondDiscoverer], EngagedSteps[Sleeper]));
	if (!TestTrue(TEXT("both goblins at (1300,+-100) engage by sight on the same step"), EngagedSteps[FirstDiscoverer] != INDEX_NONE && EngagedSteps[FirstDiscoverer] == EngagedSteps[SecondDiscoverer]))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("the goblin at (1400,700) is alerted instead of seeing the player (engaged at step %d)"), EngagedSteps[Sleeper]), EngagedSteps[Sleeper] != INDEX_NONE);
	int32 BadCallerSteps = 0;
	int32 FirstBadCallerStep = INDEX_NONE;
	int32 BadSilentSteps = 0;
	for (int32 StepIndex = EngagedSteps[FirstDiscoverer]; StepIndex < Scenario.GetStepCount(); ++StepIndex)
	{
		int32 CallerCount = 0;
		for (const FTDPackSample& MonsterSample : Scenario.Steps[StepIndex])
		{
			CallerCount += MonsterSample.Alarm == TEXT("caller") ? 1 : 0;
		}
		const bool bIsCallerCorrect = CallerCount == 1 && Scenario.Steps[StepIndex][FirstDiscoverer].Alarm == TEXT("caller");
		BadCallerSteps += bIsCallerCorrect ? 0 : 1;
		FirstBadCallerStep = !bIsCallerCorrect && FirstBadCallerStep == INDEX_NONE ? StepIndex : FirstBadCallerStep;
		BadSilentSteps += Scenario.Steps[StepIndex][SecondDiscoverer].Alarm == TEXT("silent") ? 0 : 1;
	}
	TestTrue(FString::Printf(TEXT("exactly one alarm=caller line, on the lowest SimulationId, on every step after the sighting (%d bad steps, first at step %d)"), BadCallerSteps, FirstBadCallerStep), BadCallerSteps == 0);
	TestTrue(FString::Printf(TEXT("the second discoverer is alarm=silent on every step after the sighting (%d bad steps)"), BadSilentSteps), BadSilentSteps == 0);
	const int32 SleeperStep = EngagedSteps[Sleeper];
	const FString SleeperAlarm = SleeperStep != INDEX_NONE ? Scenario.Steps[SleeperStep][Sleeper].Alarm : FString(TEXT("never engaged"));
	TestEqual(TEXT("the goblin that heard the alarm reports alarm=hop1 when it engages"), SleeperAlarm, FString(TEXT("hop1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackTacticsOffKeepsInstantAlarmTest, "TDGame.MonsterAI.Pack.TacticsOffKeepsInstantAlarm", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackTacticsOffKeepsInstantAlarmTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* TacticsVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.MonsterAI.Tactics"));
	if (!TestNotNull(TEXT("the tactics console variable exists"), TacticsVariable))
	{
		return false;
	}
	const int32 PreviousValue = TacticsVariable->GetInt();
	TacticsVariable->Set(0, ECVF_SetByCode);
	ON_SCOPE_EXIT
	{
		TacticsVariable->Set(PreviousValue, ECVF_SetByCode);
	};
	FTDPackScenario Scenario(FVector::ZeroVector);
	Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 0.f, 0.f));
	Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 700.f, 0.f));
	Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 1400.f, 0.f));
	Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(1400.f, 2100.f, 0.f));
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(PackTacticsOffChainSeconds));
	const TArray<int32> EngagedSteps = FindPackFirstEngagedSteps(Scenario);
	AddInfo(FString::Printf(TEXT("[pack] tactics off: first engaged steps A %d, B %d, C %d, D %d"), EngagedSteps[0], EngagedSteps[1], EngagedSteps[2], EngagedSteps[3]));
	if (!TestTrue(TEXT("goblin A engages by sight with tactics off"), EngagedSteps[0] != INDEX_NONE))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("with tactics off B is alerted on the same step as A (A at %d, B at %d)"), EngagedSteps[0], EngagedSteps[1]), EngagedSteps[1] == EngagedSteps[0]);
	TestTrue(FString::Printf(TEXT("with tactics off the alarm stops after one hop, so C never engages (C at %d)"), EngagedSteps[2]), EngagedSteps[2] == INDEX_NONE);
	TestTrue(FString::Printf(TEXT("with tactics off D never engages (D at %d)"), EngagedSteps[3]), EngagedSteps[3] == INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackOneShotKillAlertsNeighborsTest, "TDGame.MonsterAI.Pack.OneShotKillAlertsNeighbors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackOneShotKillAlertsNeighborsTest::RunTest(const FString& Parameters)
{
	FTDPackScenario Scenario(FVector::ZeroVector);
	const int32 VictimIndex = Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(2000.f, 0.f, 0.f));
	const int32 NeighborIndex = Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(2000.f, 600.f, 0.f));
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(8);
	const int32 HitStep = Scenario.GetStepCount();
	const bool bAreBothCalm = !Scenario.Steps.Last()[VictimIndex].bIsEngaged && !Scenario.Steps.Last()[NeighborIndex].bIsEngaged;
	if (!TestTrue(TEXT("both goblins start calm, outside the sight radius of the player"), bAreBothCalm))
	{
		return false;
	}
	if (!KillPackMonsterChecked(*this, Scenario, VictimIndex))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(2.f));
	const int32 NeighborEngagedStep = FindPackFirstSample(Scenario, NeighborIndex, HitStep, [](const FTDPackSample& Candidate) { return Candidate.bIsEngaged; });
	const int32 TicksAfterKill = NeighborEngagedStep == INDEX_NONE ? INDEX_NONE : NeighborEngagedStep - HitStep + 1;
	AddInfo(FString::Printf(TEXT("[pack] neighbor engaged %d ticks after the one-shot kill"), TicksAfterKill));
	TestTrue(FString::Printf(TEXT("the neighbor engages %d..%d steps after the one-shot kill, after the 600 cm hop delay and not at once (ticks after kill %d)"), PackDeathAlarmMinSteps, PackDeathAlarmMaxSteps, TicksAfterKill), TicksAfterKill != INDEX_NONE && TicksAfterKill >= PackDeathAlarmMinSteps && TicksAfterKill <= PackDeathAlarmMaxSteps);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackLeaderDeathScattersPackTest, "TDGame.MonsterAI.Pack.LeaderDeathScattersPack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackLeaderDeathScattersPackTest::RunTest(const FString& Parameters)
{
	FTDPackScenario Scenario(FVector::ZeroVector);
	const int32 GolemIndex = Scenario.AddMonster(TEXT("StoneGolem"), MakePackRingLocation(450.f, 0.f));
	Scenario.AddMonster(TEXT("Goblin_Melee"), MakePackRingLocation(400.f, 120.f));
	Scenario.AddMonster(TEXT("Goblin_Melee"), MakePackRingLocation(480.f, 200.f));
	Scenario.AddMonster(TEXT("Goblin_Melee"), MakePackRingLocation(430.f, 280.f));
	const TArray<int32> GoblinIndices = { 1, 2, 3 };
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(3.f));
	const int32 EngagedBeforeCount = CountPackEngagedMonsters(Scenario, { 0, 1, 2, 3 }, Scenario.GetStepCount() - 1);
	if (!TestEqual(TEXT("the golem and all three goblins are engaged before the golem dies"), EngagedBeforeCount, 4))
	{
		return false;
	}
	const int32 EventStep = Scenario.GetStepCount();
	if (!KillPackMonsterChecked(*this, Scenario, GolemIndex))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(5.f));
	const FTDPackPanicSummary Summary = SummarizePackPanic(Scenario, GoblinIndices, EventStep);
	AddInfo(FString::Printf(TEXT("[pack] leader death: panicked within 1 s %d, measured retreats %d, returning samples %d, not re-engaged %d, runs:%s"), Summary.PanickedCount, Summary.MeasuredRetreatCount, Summary.ReturningSampleCount, Summary.NotReengagedIndices.Num(), *Summary.RunDescription));
	TestTrue(FString::Printf(TEXT("at least two goblins show morale=panic within 1 s of the leader dying (%d)"), Summary.PanickedCount), Summary.PanickedCount >= 2);
	TestTrue(FString::Printf(TEXT("at least one goblin retreats for %d or more steps while the distance to the player grows (%d measured; runs:%s)"), PackMinRetreatSteps, Summary.MeasuredRetreatCount, *Summary.RunDescription), Summary.MeasuredRetreatCount >= 1);
	TestTrue(FString::Printf(TEXT("every panicked goblin's longest retreat lasts %d..%d steps, the leader fear length (shortest %d, longest %d; runs:%s)"), PackLeaderRunMinSteps, PackLeaderRunMaxSteps, Summary.ShortestPanickedRunSteps, Summary.LongestRunSteps, *Summary.RunDescription), Summary.PanickedCount > 0 && Summary.ShortestPanickedRunSteps >= PackLeaderRunMinSteps && Summary.LongestRunSteps <= PackLeaderRunMaxSteps);
	TestTrue(FString::Printf(TEXT("every panicked goblin ends its longest retreat at least %.0f cm farther from the player than it started (smallest gain %.0f)"), PackLeaderRunMinGainCm, Summary.SmallestPanickedGainCm), Summary.PanickedCount > 0 && Summary.SmallestPanickedGainCm >= PackLeaderRunMinGainCm);
	TestTrue(FString::Printf(TEXT("panicked goblins cower at 700 cm so no panic sample is farther than %.0f cm from the player (max %.0f)"), PackPanicMaxTargetDistanceCm, Summary.MaxPanicTargetDistanceCm), Summary.MaxPanicTargetDistanceCm <= PackPanicMaxTargetDistanceCm);
	TestTrue(FString::Printf(TEXT("all three goblins act against the player again (cast or within %.0f cm) after panicking, inside 4 s (%d did not)"), PackReengageDistanceCm, Summary.NotReengagedIndices.Num()), Summary.NotReengagedIndices.IsEmpty());
	TestEqual(TEXT("no goblin turns to go home during the first 4 s after the leader dies"), Summary.ReturningSampleCount, 0);
	CheckPackAllLinesMatched(*this, Scenario);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackBurstLossesPanicSmallPackTest, "TDGame.MonsterAI.Pack.BurstLossesPanicSmallPack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackBurstLossesPanicSmallPackTest::RunTest(const FString& Parameters)
{
	FTDPackScenario Scenario(FVector::ZeroVector);
	AddPackGoblinRing(Scenario, 5, 470.f, 10.f);
	const TArray<int32> SurvivorIndices = { 2, 3, 4 };
	const TArray<int32> LivingBeforeSecondLossIndices = { 1, 2, 3, 4 };
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(2.f));
	const int32 FirstLossStep = Scenario.GetStepCount();
	if (!KillPackMonsterChecked(*this, Scenario, 0))
	{
		return false;
	}
	Scenario.TickAndRecord(PackBurstGapSteps);
	if (!TestEqual(TEXT("the three future survivors are engaged before the second loss"), CountPackEngagedMonsters(Scenario, SurvivorIndices, Scenario.GetStepCount() - 1), 3))
	{
		return false;
	}
	const int32 EventStep = Scenario.GetStepCount();
	if (!KillPackMonsterChecked(*this, Scenario, 1))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(5.f));
	const FTDPackPanicSummary Summary = SummarizePackPanic(Scenario, SurvivorIndices, EventStep);
	AddInfo(FString::Printf(TEXT("[pack] burst losses: panicked within 1 s %d, measured retreats %d, longest run %d, returning samples %d, not re-engaged %d, runs:%s"), Summary.PanickedCount, Summary.MeasuredRetreatCount, Summary.LongestRunSteps, Summary.ReturningSampleCount, Summary.NotReengagedIndices.Num(), *Summary.RunDescription));
	TestTrue(FString::Printf(TEXT("at least two of the three survivors panic within 1 s of the second loss (%d)"), Summary.PanickedCount), Summary.PanickedCount >= 2);
	TestTrue(FString::Printf(TEXT("at least two survivors retreat %d or more steps while the distance grows (%d measured; runs:%s)"), PackMinRetreatSteps, Summary.MeasuredRetreatCount, *Summary.RunDescription), Summary.MeasuredRetreatCount >= 2);
	TestTrue(FString::Printf(TEXT("every panicked survivor's longest retreat lasts %d..%d steps, the ordinary fear length (shortest %d, longest %d; runs:%s)"), PackBurstRunMinSteps, PackBurstRunMaxSteps, Summary.ShortestPanickedRunSteps, Summary.LongestRunSteps, *Summary.RunDescription), Summary.PanickedCount > 0 && Summary.ShortestPanickedRunSteps >= PackBurstRunMinSteps && Summary.LongestRunSteps <= PackBurstRunMaxSteps);
	TestTrue(FString::Printf(TEXT("panicked survivors cower at 700 cm so no panic sample is farther than %.0f cm from the player (max %.0f)"), PackPanicMaxTargetDistanceCm, Summary.MaxPanicTargetDistanceCm), Summary.MaxPanicTargetDistanceCm <= PackPanicMaxTargetDistanceCm);
	TestTrue(FString::Printf(TEXT("every survivor acts against the player again after panicking, inside 4 s (%d did not)"), Summary.NotReengagedIndices.Num()), Summary.NotReengagedIndices.IsEmpty());
	const int32 EarlyMoraleSampleCount = CountPackMoraleSamples(Scenario, LivingBeforeSecondLossIndices, FirstLossStep, EventStep, ETDPackMoraleKind::NonSteady);
	TestEqual(TEXT("the first loss alone changes no goblin's morale before the second loss lands"), EarlyMoraleSampleCount, 0);
	CheckPackAllLinesMatched(*this, Scenario);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackLargePackHoldsNerveTest, "TDGame.MonsterAI.Pack.LargePackHoldsNerve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackLargePackHoldsNerveTest::RunTest(const FString& Parameters)
{
	if (!CheckPackTacticsActive(*this))
	{
		return false;
	}
	FTDPackScenario Scenario(FVector::ZeroVector);
	AddPackGoblinRing(Scenario, 7, 450.f, 0.f);
	AddPackGoblinRing(Scenario, 5, 650.f, 25.f);
	TArray<int32> SurvivorIndices;
	for (int32 MonsterIndex = 2; MonsterIndex < 12; ++MonsterIndex)
	{
		SurvivorIndices.Add(MonsterIndex);
	}
	TArray<int32> AllIndices = SurvivorIndices;
	AllIndices.Add(0);
	AllIndices.Add(1);
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(2.f));
	const int32 FirstLossStep = Scenario.GetStepCount();
	if (!KillPackMonsterChecked(*this, Scenario, 0))
	{
		return false;
	}
	Scenario.TickAndRecord(PackBurstGapSteps);
	const int32 EventStep = Scenario.GetStepCount();
	if (!KillPackMonsterChecked(*this, Scenario, 1))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(3.f));
	const int32 PanicSampleCount = CountPackMoraleSamples(Scenario, AllIndices, FirstLossStep, Scenario.GetStepCount(), ETDPackMoraleKind::NonSteady);
	const int32 EngagedAtEndCount = CountPackEngagedMonsters(Scenario, SurvivorIndices, Scenario.GetStepCount() - 1);
	const int32 ReturningSampleCount = CountPackReturningSamples(Scenario, SurvivorIndices, EventStep, Scenario.GetStepCount());
	AddInfo(FString::Printf(TEXT("[pack] large pack: non-steady morale samples %d, survivors engaged at end %d of %d, returning samples %d"), PanicSampleCount, EngagedAtEndCount, SurvivorIndices.Num(), ReturningSampleCount));
	TestEqual(TEXT("a twelve-goblin pack that loses two never leaves morale=steady (no fear, panic or rallied sample)"), PanicSampleCount, 0);
	TestEqual(TEXT("all ten survivors are still engaged after 3 s"), EngagedAtEndCount, SurvivorIndices.Num());
	CheckPackAllLinesMatched(*this, Scenario);
	TestEqual(TEXT("no survivor turns to go home"), ReturningSampleCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackSlowLossesDoNotPanicTest, "TDGame.MonsterAI.Pack.SlowLossesDoNotPanic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackSlowLossesDoNotPanicTest::RunTest(const FString& Parameters)
{
	if (!CheckPackTacticsActive(*this))
	{
		return false;
	}
	FTDPackScenario Scenario(FVector::ZeroVector);
	AddPackGoblinRing(Scenario, 5, 470.f, 10.f);
	const TArray<int32> AllIndices = { 0, 1, 2, 3, 4 };
	const TArray<int32> SurvivorIndices = { 2, 3, 4 };
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(2.f));
	const int32 FirstLossStep = Scenario.GetStepCount();
	if (!KillPackMonsterChecked(*this, Scenario, 0))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSteadyLossGapSteps);
	if (!KillPackMonsterChecked(*this, Scenario, 1))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(4.f));
	const int32 ObservedEndStep = Scenario.GetStepCount();
	const int32 PanicSampleCount = CountPackMoraleSamples(Scenario, AllIndices, FirstLossStep, ObservedEndStep, ETDPackMoraleKind::NonSteady);
	const int32 EngagedAtEndCount = CountPackEngagedMonsters(Scenario, SurvivorIndices, Scenario.GetStepCount() - 1);
	const int32 ReturningSampleCount = CountPackReturningSamples(Scenario, SurvivorIndices, FirstLossStep, ObservedEndStep);
	AddInfo(FString::Printf(TEXT("[pack] steady losses: non-steady morale samples %d, survivors engaged at end %d of 3, returning samples %d"), PanicSampleCount, EngagedAtEndCount, ReturningSampleCount));
	TestEqual(TEXT("two losses 96 steps (1.5 s) apart, longer than the 80 step loss window, never leave morale=steady"), PanicSampleCount, 0);
	TestEqual(TEXT("all three survivors are still engaged at the end"), EngagedAtEndCount, 3);
	CheckPackAllLinesMatched(*this, Scenario);
	TestEqual(TEXT("no survivor turns to go home"), ReturningSampleCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackLoneSurvivorHoldsNerveTest, "TDGame.MonsterAI.Pack.LoneSurvivorHoldsNerve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackLoneSurvivorHoldsNerveTest::RunTest(const FString& Parameters)
{
	if (!CheckPackTacticsActive(*this))
	{
		return false;
	}
	FTDPackScenario Scenario(FVector::ZeroVector);
	AddPackGoblinRing(Scenario, 3, 470.f, 10.f);
	const TArray<int32> SurvivorIndices = { 2 };
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(2.f));
	const int32 FirstLossStep = Scenario.GetStepCount();
	if (!KillPackMonsterChecked(*this, Scenario, 0))
	{
		return false;
	}
	Scenario.TickAndRecord(PackBurstGapSteps);
	if (!KillPackMonsterChecked(*this, Scenario, 1))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(4.f));
	const int32 MoraleSampleCount = CountPackMoraleSamples(Scenario, SurvivorIndices, FirstLossStep, Scenario.GetStepCount(), ETDPackMoraleKind::NonSteady);
	const int32 EngagedAtEndCount = CountPackEngagedMonsters(Scenario, SurvivorIndices, Scenario.GetStepCount() - 1);
	AddInfo(FString::Printf(TEXT("[pack] lone survivor: non-steady morale samples %d, engaged at end %d, living goblins %d"), MoraleSampleCount, EngagedAtEndCount, Scenario.GetLineCount()));
	TestEqual(TEXT("the last of three goblins, left alone by two losses 16 steps apart, never leaves morale=steady"), MoraleSampleCount, 0);
	TestEqual(TEXT("the lone survivor is still engaged at the end"), EngagedAtEndCount, 1);
	TestEqual(TEXT("exactly one goblin is left alive"), Scenario.GetLineCount(), 1);
	CheckPackAllLinesMatched(*this, Scenario);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackRalliedPackPanicsOnceTest, "TDGame.MonsterAI.Pack.RalliedPackPanicsOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackRalliedPackPanicsOnceTest::RunTest(const FString& Parameters)
{
	if (!CheckPackTacticsActive(*this))
	{
		return false;
	}
	FTDPackScenario Scenario(FVector::ZeroVector);
	AddPackGoblinRing(Scenario, 6, 470.f, 10.f);
	const TArray<int32> FirstWaveSurvivorIndices = { 2, 3, 4, 5 };
	const TArray<int32> SecondWaveSurvivorIndices = { 4, 5 };
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(2.f));
	if (!KillPackMonsterChecked(*this, Scenario, 0))
	{
		return false;
	}
	Scenario.TickAndRecord(PackBurstGapSteps);
	const int32 FirstEventStep = Scenario.GetStepCount();
	if (!KillPackMonsterChecked(*this, Scenario, 1))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(PackRallySettleSeconds));
	const FTDPackPanicSummary FirstSummary = SummarizePackPanic(Scenario, FirstWaveSurvivorIndices, FirstEventStep);
	AddInfo(FString::Printf(TEXT("[pack] first wave: panicked within 1 s %d, longest run %d, runs:%s"), FirstSummary.PanickedCount, FirstSummary.LongestRunSteps, *FirstSummary.RunDescription));
	if (!TestEqual(TEXT("all four survivors of the first wave panic within 1 s of the second loss"), FirstSummary.PanickedCount, 4))
	{
		return false;
	}
	const int32 LastStep = Scenario.GetStepCount() - 1;
	const int32 RalliedCount = CountPackMoraleSamples(Scenario, FirstWaveSurvivorIndices, LastStep, LastStep + 1, ETDPackMoraleKind::Rallied);
	if (!TestEqual(TEXT("all four survivors show morale=rallied once their panic is over and they are still in the same fight"), RalliedCount, 4))
	{
		return false;
	}
	const int32 SecondWaveStep = Scenario.GetStepCount();
	if (!KillPackMonsterChecked(*this, Scenario, 2))
	{
		return false;
	}
	Scenario.TickAndRecord(PackBurstGapSteps);
	if (!KillPackMonsterChecked(*this, Scenario, 3))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(PackSecondWaveObserveSeconds));
	const int32 SecondWaveMoraleCount = CountPackMoraleSamples(Scenario, SecondWaveSurvivorIndices, SecondWaveStep, Scenario.GetStepCount(), ETDPackMoraleKind::FearOrPanic);
	const int32 EngagedAtEndCount = CountPackEngagedMonsters(Scenario, SecondWaveSurvivorIndices, Scenario.GetStepCount() - 1);
	AddInfo(FString::Printf(TEXT("[pack] second wave: fear or panic samples %d, engaged at end %d, living goblins %d"), SecondWaveMoraleCount, EngagedAtEndCount, Scenario.GetLineCount()));
	TestEqual(TEXT("two rallied survivors that lose two more allies in the same fight show no fear or panic sample"), SecondWaveMoraleCount, 0);
	TestEqual(TEXT("both remaining goblins are still engaged at the end"), EngagedAtEndCount, 2);
	TestEqual(TEXT("exactly two goblins are left alive"), Scenario.GetLineCount(), 2);
	CheckPackAllLinesMatched(*this, Scenario);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackTimedRetreatEndsOnTimeTest, "TDGame.MonsterAI.Pack.TimedRetreatEndsOnTime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackTimedRetreatEndsOnTimeTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = ResolvePackProbeDefinition(MakePackFleeProbeText(), Errors);
	if (!TestTrue(FString::Printf(TEXT("the Flee probe definition resolves (errors %d, first: %s)"), Errors.Num(), Errors.IsEmpty() ? TEXT("none") : *Errors[0]), Definition.IsValid()))
	{
		return false;
	}
	FTDPackScenario Scenario(FVector::ZeroVector);
	if (!TestNotNull(TEXT("the target pawn exists"), Scenario.PlayerPawn))
	{
		return false;
	}
	const TArray<float> StepSizesCm = { 10.f, 5.f };
	for (const float StepCm : StepSizesCm)
	{
		const FTDPackRetreatDrive Drive = DrivePackTimedRetreat(Definition, Scenario.PlayerPawn, StepCm);
		AddInfo(FString::Printf(TEXT("[pack] timed retreat moving %.0f cm per step: idle at step %d"), StepCm, Drive.IdleStep));
		if (!TestTrue(FString::Printf(TEXT("Flee starts a MoveAway move (step %.0f cm)"), StepCm), Drive.bDidStartMoving))
		{
			return false;
		}
		const bool bEndsOnTime = Drive.IdleStep != INDEX_NONE && FMath::Abs(Drive.IdleStep - PackRetreatExpectedSteps) <= PackRetreatSlackSteps;
		TestTrue(FString::Printf(TEXT("a 1.5 s retreat that keeps gaining distance (%.0f cm per step) ends within %d of step %d (idle at step %d)"), StepCm, PackRetreatSlackSteps, PackRetreatExpectedSteps, Drive.IdleStep), bEndsOnTime);
	}
	const FTDPackRetreatDrive StalledDrive = DrivePackTimedRetreat(Definition, Scenario.PlayerPawn, PackStalledRetreatStepCm);
	AddInfo(FString::Printf(TEXT("[pack] timed retreat moving %.0f cm per step: idle at step %d"), PackStalledRetreatStepCm, StalledDrive.IdleStep));
	if (!TestTrue(TEXT("Flee starts a MoveAway move (step 1 cm)"), StalledDrive.bDidStartMoving))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("a retreat that gains only %.0f cm per step never reaches the 30 cm progress mark and ends between steps %d and %d (idle at step %d)"), PackStalledRetreatStepCm, PackCorneredMinSteps, PackCorneredMaxSteps, StalledDrive.IdleStep), StalledDrive.IdleStep >= PackCorneredMinSteps && StalledDrive.IdleStep <= PackCorneredMaxSteps);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackTimedRetreatEndsWhenCorneredTest, "TDGame.MonsterAI.Pack.TimedRetreatEndsWhenCornered", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackTimedRetreatEndsWhenCorneredTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = ResolvePackProbeDefinition(MakePackFleeProbeText(), Errors);
	if (!TestTrue(FString::Printf(TEXT("the Flee probe definition resolves (errors %d, first: %s)"), Errors.Num(), Errors.IsEmpty() ? TEXT("none") : *Errors[0]), Definition.IsValid()))
	{
		return false;
	}
	FTDPackScenario Scenario(FVector::ZeroVector);
	if (!TestNotNull(TEXT("the target pawn exists"), Scenario.PlayerPawn))
	{
		return false;
	}
	const FTDPackRetreatDrive Drive = DrivePackTimedRetreat(Definition, Scenario.PlayerPawn, 0.f);
	AddInfo(FString::Printf(TEXT("[pack] cornered retreat: idle at step %d"), Drive.IdleStep));
	if (!TestTrue(TEXT("Flee starts a MoveAway move"), Drive.bDidStartMoving))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("a retreat that cannot gain distance ends between steps %d and %d (idle at step %d)"), PackCorneredMinSteps, PackCorneredMaxSteps, Drive.IdleStep), Drive.IdleStep >= PackCorneredMinSteps && Drive.IdleStep <= PackCorneredMaxSteps);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackLowHealthFleeDoesNotLoopTest, "TDGame.MonsterAI.Pack.LowHealthFleeDoesNotLoop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackLowHealthFleeDoesNotLoopTest::RunTest(const FString& Parameters)
{
	FTDPackScenario Scenario(FVector::ZeroVector);
	const int32 GoblinIndex = Scenario.AddMonster(TEXT("Goblin_Melee"), FVector(300.f, 0.f, 0.f));
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	const UTDCombatComponent* GoblinCombat = Scenario.Monsters[GoblinIndex]->GetCombatComponent();
	for (int32 HitCount = 0; HitCount < PackMaxSetupHits && GoblinCombat->GetCurrentHealth() > PackLowHealthRatio * GoblinCombat->GetMaxHealth(); ++HitCount)
	{
		Scenario.Hit(GoblinIndex, PackSetupHitDamage);
	}
	const float HealthRatio = GoblinCombat->GetCurrentHealth() / FMath::Max(1.f, GoblinCombat->GetMaxHealth());
	if (!TestTrue(FString::Printf(TEXT("the setup leaves the goblin alive near 25%% health (ratio %.3f)"), HealthRatio), HealthRatio > 0.15f && HealthRatio <= 0.30f))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(PackLowHealthSeconds));
	int32 ReturningSteps = 0;
	int32 FleeSteps = 0;
	int32 LongestFleeRun = 0;
	int32 CurrentFleeRun = 0;
	for (const TArray<FTDPackSample>& StepSamples : Scenario.Steps)
	{
		const FTDPackSample& GoblinSample = StepSamples[GoblinIndex];
		ReturningSteps += GoblinSample.bIsReturning ? 1 : 0;
		FleeSteps += GoblinSample.bIsFleeing ? 1 : 0;
		CurrentFleeRun = GoblinSample.bIsFleeing ? CurrentFleeRun + 1 : 0;
		LongestFleeRun = FMath::Max(LongestFleeRun, CurrentFleeRun);
	}
	const float FleeFraction = static_cast<float>(FleeSteps) / static_cast<float>(FMath::Max(1, Scenario.GetStepCount()));
	const int32 CastStartCount = CountPackCastStarts(Scenario, GoblinIndex, 0, Scenario.GetStepCount());
	AddInfo(FString::Printf(TEXT("[pack] low health flee (health %.2f): returning steps %d, flee fraction %.3f, longest flee run %d, cast starts %d"), HealthRatio, ReturningSteps, FleeFraction, LongestFleeRun, CastStartCount));
	TestEqual(TEXT("the goblin never gives up and turns toward home"), ReturningSteps, 0);
	TestTrue(FString::Printf(TEXT("the goblin spends at most %.0f%% of 30 s in the Flee action (%.3f)"), PackFleeMaxFraction * 100.f, FleeFraction), FleeFraction <= PackFleeMaxFraction);
	TestTrue(FString::Printf(TEXT("no single flee lasts longer than %d steps (longest %d)"), PackFleeMaxRunSteps, LongestFleeRun), LongestFleeRun <= PackFleeMaxRunSteps);
	TestTrue(FString::Printf(TEXT("the goblin still starts at least %d casts in 30 s (%d)"), PackLowHealthMinCastStarts, CastStartCount), CastStartCount >= PackLowHealthMinCastStarts);
	CheckPackAllLinesMatched(*this, Scenario);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackChargersTakeTurnsTest, "TDGame.MonsterAI.Pack.ChargersTakeTurns", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackChargersTakeTurnsTest::RunTest(const FString& Parameters)
{
	return CheckPackExclusiveRole(*this, TEXT("Goblin_Melee"), 4, 600.f, ETDTacticsRole::Charger, TEXT("chargers"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackChargerCapDoesNotBlockMeleeTest, "TDGame.MonsterAI.Pack.ChargerCapDoesNotBlockMelee", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackChargerCapDoesNotBlockMeleeTest::RunTest(const FString& Parameters)
{
	if (!CheckPackTacticsActive(*this))
	{
		return false;
	}
	FTDPackScenario Scenario(FVector::ZeroVector);
	const int32 FirstCharger = Scenario.AddMonster(TEXT("Goblin_Melee"), MakePackRingLocation(550.f, 0.f));
	const int32 SecondCharger = Scenario.AddMonster(TEXT("Goblin_Melee"), MakePackRingLocation(550.f, 180.f));
	const int32 FirstMelee = Scenario.AddMonster(TEXT("Goblin_Melee"), MakePackRingLocation(650.f, 90.f));
	const int32 SecondMelee = Scenario.AddMonster(TEXT("Goblin_Melee"), MakePackRingLocation(650.f, 270.f));
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	if (!TestTrue(TEXT("both goblins on the 550 cm ring are registered and take the charger role"), Scenario.SetRole(FirstCharger, ETDTacticsRole::Charger) && Scenario.SetRole(SecondCharger, ETDTacticsRole::Charger)))
	{
		return false;
	}
	int32 MaxChargerHolderCount = 0;
	int32 ChargerHoldingStepCount = 0;
	int32 SharedHoldingStepCount = 0;
	const int32 StepCount = PackSecondsToSteps(PackExclusiveSeconds);
	for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
	{
		Scenario.TickAndRecord(1);
		const int32 ChargerHolderCount = FTDPackBehaviorTestAccess::CountTokenHolders(*Scenario.Subsystem, ETDTacticsRole::Charger);
		const int32 MeleeHolderCount = FTDPackBehaviorTestAccess::CountTokenHolders(*Scenario.Subsystem, ETDTacticsRole::Melee);
		MaxChargerHolderCount = FMath::Max(MaxChargerHolderCount, ChargerHolderCount);
		ChargerHoldingStepCount += ChargerHolderCount == 1 ? 1 : 0;
		SharedHoldingStepCount += ChargerHolderCount == 1 && MeleeHolderCount >= 1 ? 1 : 0;
	}
	const int32 FirstMeleeCastStarts = CountPackCastStarts(Scenario, FirstMelee, 0, StepCount);
	const int32 SecondMeleeCastStarts = CountPackCastStarts(Scenario, SecondMelee, 0, StepCount);
	const float SharedFraction = static_cast<float>(SharedHoldingStepCount) / static_cast<float>(FMath::Max(1, ChargerHoldingStepCount));
	AddInfo(FString::Printf(TEXT("[pack] mixed roles: max charger holders %d, steps with one charger holder %d, of those with a melee holder %d (%.3f), melee cast starts %d and %d"), MaxChargerHolderCount, ChargerHoldingStepCount, SharedHoldingStepCount, SharedFraction, FirstMeleeCastStarts, SecondMeleeCastStarts));
	TestTrue(FString::Printf(TEXT("at most one of the two chargers holds a token on every step (max %d)"), MaxChargerHolderCount), MaxChargerHolderCount <= 1);
	TestTrue(FString::Printf(TEXT("a charger holds a token on some steps, so the cap is exercised (%d steps)"), ChargerHoldingStepCount), ChargerHoldingStepCount > 0);
	TestTrue(FString::Printf(TEXT("while one charger holds a token, a melee goblin holds one on at least %.0f%% of those steps, so the blocked second charger does not hide the melee candidates (%.3f)"), PackMixedMeleeMinFraction * 100.f, SharedFraction), SharedFraction >= PackMixedMeleeMinFraction);
	TestTrue(FString::Printf(TEXT("both melee goblins cast at least once in %.0f s (%d and %d)"), PackExclusiveSeconds, FirstMeleeCastStarts, SecondMeleeCastStarts), FirstMeleeCastStarts >= 1 && SecondMeleeCastStarts >= 1);
	CheckPackAllLinesMatched(*this, Scenario);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackChargerRetaliationAllowsSecondHolderTest, "TDGame.MonsterAI.Pack.ChargerRetaliationAllowsSecondHolder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackChargerRetaliationAllowsSecondHolderTest::RunTest(const FString& Parameters)
{
	if (!CheckPackTacticsActive(*this))
	{
		return false;
	}
	FTDPackScenario Scenario(FVector::ZeroVector);
	for (int32 ChargerIndex = 0; ChargerIndex < 4; ++ChargerIndex)
	{
		Scenario.AddMonster(TEXT("Goblin_Melee"), MakePackRingLocation(600.f, 90.f * static_cast<float>(ChargerIndex)));
	}
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	for (int32 ChargerIndex = 0; ChargerIndex < 4; ++ChargerIndex)
	{
		if (!TestTrue(FString::Printf(TEXT("goblin %d is registered and takes the charger role"), ChargerIndex), Scenario.SetRole(ChargerIndex, ETDTacticsRole::Charger)))
		{
			return false;
		}
	}
	int32 MaxHolderCountBeforeHit = 0;
	TArray<int32> HitIndices;
	const int32 SearchStepCount = PackSecondsToSteps(PackRetaliationSearchSeconds);
	for (int32 StepIndex = 0; StepIndex < SearchStepCount && HitIndices.IsEmpty(); ++StepIndex)
	{
		Scenario.TickAndRecord(1);
		const int32 HolderCount = FTDPackBehaviorTestAccess::CountTokenHolders(*Scenario.Subsystem, ETDTacticsRole::Charger);
		MaxHolderCountBeforeHit = FMath::Max(MaxHolderCountBeforeHit, HolderCount);
		if (HolderCount != 1 || FindPackCastStartMonster(Scenario) == INDEX_NONE)
		{
			continue;
		}
		HitIndices = FindPackTokenlessMonsters(Scenario);
	}
	if (!TestTrue(FString::Printf(TEXT("within %.0f s one charger starts a cast while other chargers hold no token (%d to hit)"), PackRetaliationSearchSeconds, HitIndices.Num()), !HitIndices.IsEmpty()))
	{
		return false;
	}
	for (const int32 HitIndex : HitIndices)
	{
		Scenario.Hit(HitIndex, PackProbeHitDamage);
	}
	TArray<int32> HolderCountsAfterHit;
	for (int32 StepIndex = 0; StepIndex < PackRetaliationObserveSteps; ++StepIndex)
	{
		Scenario.TickAndRecord(1);
		HolderCountsAfterHit.Add(FTDPackBehaviorTestAccess::CountTokenHolders(*Scenario.Subsystem, ETDTacticsRole::Charger));
	}
	int32 MaxHolderCountInWindow = 0;
	int32 MaxHolderCountAfterSettle = 0;
	for (int32 StepOffset = 0; StepOffset < HolderCountsAfterHit.Num(); ++StepOffset)
	{
		if (StepOffset < PackRetaliationWindowSteps)
		{
			MaxHolderCountInWindow = FMath::Max(MaxHolderCountInWindow, HolderCountsAfterHit[StepOffset]);
		}
		if (StepOffset >= PackRetaliationWindowSteps + PackRetaliationSettleSteps)
		{
			MaxHolderCountAfterSettle = FMath::Max(MaxHolderCountAfterSettle, HolderCountsAfterHit[StepOffset]);
		}
	}
	AddInfo(FString::Printf(TEXT("[pack] retaliation: %d goblins hit, max holders before the hit %d, inside the %d step window %d, after %d more steps %d"), HitIndices.Num(), MaxHolderCountBeforeHit, PackRetaliationWindowSteps, MaxHolderCountInWindow, PackRetaliationSettleSteps, MaxHolderCountAfterSettle));
	TestTrue(FString::Printf(TEXT("at most one charger holds a token before anyone is hit (max %d)"), MaxHolderCountBeforeHit), MaxHolderCountBeforeHit <= 1);
	TestEqual(FString::Printf(TEXT("a charger hit by the player while another holds the token gets a second token inside %d steps (max %d)"), PackRetaliationWindowSteps, MaxHolderCountInWindow), MaxHolderCountInWindow, 2);
	TestTrue(FString::Printf(TEXT("the cap falls back to one holder %d steps after the hit (max %d)"), PackRetaliationWindowSteps + PackRetaliationSettleSteps, MaxHolderCountAfterSettle), MaxHolderCountAfterSettle <= 1);
	CheckPackAllLinesMatched(*this, Scenario);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackSupportCastsAloneTest, "TDGame.MonsterAI.Pack.SupportCastsAlone", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackSupportCastsAloneTest::RunTest(const FString& Parameters)
{
	return CheckPackExclusiveRole(*this, TEXT("Archer"), 3, 700.f, ETDTacticsRole::Support, TEXT("support"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackRangedBandFloorFormulaTest, "TDGame.MonsterAI.Pack.RangedBandFloorFormula", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackRangedBandFloorFormulaTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a 650 cm melee ring floor, 40 cm body and 900 cm reach is clamped to the 700 cm floor cap"), FTDCombatTactics::ComputeRangedBandFloor(650, 40, 900), 700);
	TestEqual(TEXT("a 650 cm melee ring floor, 40 cm body and 750 cm reach is clamped to reach minus 100 cm (650)"), FTDCombatTactics::ComputeRangedBandFloor(650, 40, 750), 650);
	TestEqual(TEXT("a 400 cm melee ring floor, 40 cm body and 900 cm reach adds the 100 cm gap and the body (540)"), FTDCombatTactics::ComputeRangedBandFloor(400, 40, 900), 540);
	TestEqual(TEXT("a 0 cm melee ring floor keeps the band floor at 0"), FTDCombatTactics::ComputeRangedBandFloor(0, 40, 900), 0);
	TestEqual(TEXT("a negative melee ring floor keeps the band floor at 0"), FTDCombatTactics::ComputeRangedBandFloor(-20, 40, 900), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackRangedHoldsBandBehindMeleeRingTest, "TDGame.MonsterAI.Pack.RangedHoldsBandBehindMeleeRing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackRangedHoldsBandBehindMeleeRingTest::RunTest(const FString& Parameters)
{
	const FVector FirstArcherLocation = MakePackRingLocation(540.f, 45.f);
	const FVector SecondArcherLocation = MakePackRingLocation(540.f, 105.f);
	const int32 FirstArcherBearing = FTDCombatTactics::ToDecidegrees(FirstArcherLocation.X, FirstArcherLocation.Y);
	const int32 SecondArcherBearing = FTDCombatTactics::ToDecidegrees(SecondArcherLocation.X, SecondArcherLocation.Y);
	if (!TestTrue(FString::Printf(TEXT("the archer bearings %d and %d decidegrees avoid the south penalty zone"), FirstArcherBearing, SecondArcherBearing), !FTDCombatTactics::IsSouthDecidegrees(FirstArcherBearing) && !FTDCombatTactics::IsSouthDecidegrees(SecondArcherBearing)))
	{
		return false;
	}
	FTDPackScenario Scenario(FVector::ZeroVector);
	Scenario.AddMonster(TEXT("StoneGolem"), MakePackRingLocation(600.f, 200.f));
	Scenario.AddMonster(TEXT("StoneGolem"), MakePackRingLocation(650.f, 240.f));
	Scenario.AddMonster(TEXT("StoneGolem"), MakePackRingLocation(700.f, 280.f));
	Scenario.AddMonster(TEXT("StoneGolem"), MakePackRingLocation(650.f, 320.f));
	const int32 FirstArcher = Scenario.AddMonster(TEXT("Archer"), FirstArcherLocation);
	const int32 SecondArcher = Scenario.AddMonster(TEXT("Archer"), SecondArcherLocation);
	if (!CheckPackScenarioReady(*this, Scenario))
	{
		return false;
	}
	Scenario.TickAndRecord(PackSecondsToSteps(PackBandTotalSeconds));
	TArray<float> Distances;
	int32 TooCloseCount = 0;
	for (int32 StepIndex = PackSecondsToSteps(PackBandSettleSeconds); StepIndex < Scenario.GetStepCount(); ++StepIndex)
	{
		for (const int32 ArcherIndex : { FirstArcher, SecondArcher })
		{
			const FTDPackSample& ArcherSample = Scenario.Steps[StepIndex][ArcherIndex];
			if (!ArcherSample.bIsPresent)
			{
				continue;
			}
			Distances.Add(ArcherSample.TargetDistance);
			TooCloseCount += ArcherSample.TargetDistance < PackBandTooCloseCm ? 1 : 0;
		}
	}
	if (!TestTrue(TEXT("both archers stay registered and measurable"), Distances.Num() == 2 * (Scenario.GetStepCount() - PackSecondsToSteps(PackBandSettleSeconds))))
	{
		return false;
	}
	const float MedianDistance = ComputePackMedian(Distances);
	const float TooCloseFraction = static_cast<float>(TooCloseCount) / static_cast<float>(Distances.Num());
	AddInfo(FString::Printf(TEXT("[pack] archer band: median distance %.0f, too close fraction %.3f, first archer %.0f, second archer %.0f at the end"), MedianDistance, TooCloseFraction, Scenario.Steps.Last()[FirstArcher].TargetDistance, Scenario.Steps.Last()[SecondArcher].TargetDistance));
	TestTrue(FString::Printf(TEXT("the archer median distance after 6 s is at least %.0f cm (%.0f)"), PackBandMedianMinimumCm, MedianDistance), MedianDistance >= PackBandMedianMinimumCm);
	TestTrue(FString::Printf(TEXT("at most %.0f%% of archer samples are closer than %.0f cm (%.3f)"), PackBandTooCloseMaxFraction * 100.f, PackBandTooCloseCm, TooCloseFraction), TooCloseFraction <= PackBandTooCloseMaxFraction);
	CheckPackAllLinesMatched(*this, Scenario);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackDefinitionKeysTest, "TDGame.MonsterAI.Pack.PackDefinitionKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackDefinitionKeysTest::RunTest(const FString& Parameters)
{
	CheckPackRoleProbe(*this, TEXT("role Charger"), TEXT("\"role\": \"Charger\","), 3);
	CheckPackRoleProbe(*this, TEXT("role Support"), TEXT("\"role\": \"Support\","), 4);
	CheckPackRoleProbe(*this, TEXT("elite true"), TEXT("\"role\": \"Melee\", \"elite\": true,"), -1);
	CheckPackMoraleProbe(*this, TEXT("elite true"), TEXT("\"role\": \"Melee\", \"elite\": true,"), true, true, true);
	CheckPackMoraleProbe(*this, TEXT("role Support"), TEXT("\"role\": \"Support\","), false, false, true);
	CheckPackMoraleProbe(*this, TEXT("role Charger"), TEXT("\"role\": \"Charger\","), false, false, false);
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (TestNotNull(TEXT("the definition library exists"), Library))
	{
		for (const FName ShippedEliteId : { FName(TEXT("StoneGolem")), FName(TEXT("BookHead_Brute")) })
		{
			CheckPackMoraleFlags(*this, ShippedEliteId.ToString(), Library->FindDefinition(ShippedEliteId), true, true, true);
		}
	}
	TArray<FString> Errors;
	ResolvePackProbeDefinition(MakePackRoleProbeText(TEXT("\"role\": \"Melee\", \"elite\": \"yes\",")), Errors);
	const bool bHasEliteTypeError = Errors.ContainsByPredicate([](const FString& Candidate) { return Candidate.Contains(TEXT("elite")) && Candidate.Contains(TEXT("expected true or false")); });
	TestTrue(FString::Printf(TEXT("elite set to a string is rejected with 'expected true or false' (errors %d, first: %s)"), Errors.Num(), Errors.IsEmpty() ? TEXT("none") : *Errors[0]), bHasEliteTypeError);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackDeterministicTest, "TDGame.MonsterAI.Pack.Deterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackDeterministicTest::RunTest(const FString& Parameters)
{
	if (!CheckPackTacticsActive(*this))
	{
		return false;
	}
	FTDPackDeterminismRun FirstRun;
	FTDPackDeterminismRun SecondRun;
	if (!TestTrue(TEXT("the first run sets up its alarm chain, ring with a golem, chargers and losses"), RunPackDeterminismScenario(FirstRun)))
	{
		return false;
	}
	if (!TestTrue(TEXT("the second run sets up its alarm chain, ring with a golem, chargers and losses"), RunPackDeterminismScenario(SecondRun)))
	{
		return false;
	}
	int32 FirstDifferingStep = INDEX_NONE;
	const int32 DifferingSteps = CountPackDifferingSteps(FirstRun.StepHashes, SecondRun.StepHashes, FirstDifferingStep);
	AddInfo(FString::Printf(TEXT("[pack] determinism: steps %d vs %d, final lines %d vs %d, differing steps %d, fear or panic samples %d, hop2 samples %d, max charger holders %d"), FirstRun.StepHashes.Num(), SecondRun.StepHashes.Num(), FirstRun.FinalLineCount, SecondRun.FinalLineCount, DifferingSteps, FirstRun.FearSampleCount, FirstRun.HopTwoSampleCount, FirstRun.MaxChargerHolderCount));
	TestEqual(TEXT("both runs record the same number of steps"), FirstRun.StepHashes.Num(), SecondRun.StepHashes.Num());
	TestEqual(TEXT("both runs end with the same number of living monsters"), FirstRun.FinalLineCount, SecondRun.FinalLineCount);
	TestEqual(TEXT("two losses leave seven of nine monsters"), FirstRun.FinalLineCount, 7);
	TestTrue(FString::Printf(TEXT("the scene exercises fear or panic, otherwise the hash proves nothing about morale (%d samples)"), FirstRun.FearSampleCount), FirstRun.FearSampleCount > 0);
	TestTrue(FString::Printf(TEXT("the scene exercises the second alarm hop (%d samples)"), FirstRun.HopTwoSampleCount), FirstRun.HopTwoSampleCount > 0);
	TestTrue(FString::Printf(TEXT("the scene exercises a charger token holder (max %d)"), FirstRun.MaxChargerHolderCount), FirstRun.MaxChargerHolderCount >= 1);
	const bool bAreCountsEqual = FirstRun.FearSampleCount == SecondRun.FearSampleCount && FirstRun.HopTwoSampleCount == SecondRun.HopTwoSampleCount && FirstRun.MaxChargerHolderCount == SecondRun.MaxChargerHolderCount;
	TestTrue(FString::Printf(TEXT("both runs observe the same fear (%d vs %d), hop2 (%d vs %d) and charger holder (%d vs %d) counts"), FirstRun.FearSampleCount, SecondRun.FearSampleCount, FirstRun.HopTwoSampleCount, SecondRun.HopTwoSampleCount, FirstRun.MaxChargerHolderCount, SecondRun.MaxChargerHolderCount), bAreCountsEqual);
	TestEqual(TEXT("every debug line of both runs belongs to a spawned monster"), FirstRun.UnmatchedLineCount + SecondRun.UnmatchedLineCount, 0);
	TestTrue(FString::Printf(TEXT("the cumulative debug line hash matches on every step (%d differ, first at step %d)"), DifferingSteps, FirstDifferingStep), DifferingSteps == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPackEventOrderInvariantTest, "TDGame.MonsterAI.Pack.EventOrderInvariant", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPackEventOrderInvariantTest::RunTest(const FString& Parameters)
{
	if (!CheckPackTacticsActive(*this))
	{
		return false;
	}
	FTDPackEventOrderRun ForwardRun;
	FTDPackEventOrderRun ReversedRun;
	if (!TestTrue(TEXT("the forward-order run (hit, hit, kill, kill) sets up"), RunPackEventOrderScenario(false, ForwardRun)))
	{
		return false;
	}
	if (!TestTrue(TEXT("the reversed-order run (kill, kill, hit, hit) sets up"), RunPackEventOrderScenario(true, ReversedRun)))
	{
		return false;
	}
	int32 FirstDifferingStep = INDEX_NONE;
	const int32 DifferingSteps = CountPackDifferingSteps(ForwardRun.StepHashes, ReversedRun.StepHashes, FirstDifferingStep);
	AddInfo(FString::Printf(TEXT("[pack] event order: steps %d vs %d, final lines %d vs %d, smallest hit loss %.1f vs %.1f, differing steps %d"), ForwardRun.StepHashes.Num(), ReversedRun.StepHashes.Num(), ForwardRun.FinalLineCount, ReversedRun.FinalLineCount, ForwardRun.SmallestHitHealthLoss, ReversedRun.SmallestHitHealthLoss, DifferingSteps));
	TestTrue(FString::Printf(TEXT("both non-lethal hits land in both orders (smallest loss %.1f and %.1f)"), ForwardRun.SmallestHitHealthLoss, ReversedRun.SmallestHitHealthLoss), ForwardRun.SmallestHitHealthLoss > 0.f && ReversedRun.SmallestHitHealthLoss > 0.f);
	TestEqual(TEXT("four goblins survive in the forward order"), ForwardRun.FinalLineCount, 4);
	TestEqual(TEXT("four goblins survive in the reversed order"), ReversedRun.FinalLineCount, 4);
	TestEqual(TEXT("every debug line of both runs belongs to a spawned monster"), ForwardRun.UnmatchedLineCount + ReversedRun.UnmatchedLineCount, 0);
	TestTrue(FString::Printf(TEXT("the cumulative debug line hash matches on every step (%d differ, first at step %d)"), DifferingSteps, FirstDifferingStep), DifferingSteps == 0);
	return true;
}

#endif
