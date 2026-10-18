#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
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
#include "MonsterAI/TDCombatDirector.h"
#include "MonsterAI/TDCombatTactics.h"
#include "MonsterAI/TDMonsterActionExecutor.h"
#include "MonsterAI/TDMonsterDefinitionLibrary.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MonsterAI/TDMonsterThinkSubsystem.h"
#include "MoverSimulationTypes.h"

namespace
{
	constexpr float TacticsStepSeconds = 1.f / 64.f;
	constexpr int32 TacticsStepsPerSecond = 64;
	constexpr float TacticsPlayerMaxHealth = 100000.f;
	constexpr float TacticsRingInnerRadius = 700.f;
	constexpr float TacticsRingRadiusStep = 100.f;
	constexpr int32 TacticsRingRadiusVariants = 3;
	constexpr uint64 TacticsHashOffsetBasis = 14695981039346656037ull;
	constexpr uint64 TacticsHashPrime = 1099511628211ull;

	constexpr int32 TacticsBudgetMonsterCount = 6;
	constexpr float TacticsBudgetSeconds = 20.f;
	constexpr int32 TacticsBudgetMaxConcurrentCasts = 2;
	constexpr int32 TacticsCrowdMonsterCount = 10;
	constexpr float TacticsSpacingSeconds = 30.f;
	constexpr int32 TacticsMinStartSpacingSteps = 16;
	constexpr float TacticsWaitRingSeconds = 20.f;
	constexpr float TacticsWaitRingWindowBeginSeconds = 8.f;
	constexpr float TacticsMinWaiterMedianDistance = 300.f;
	constexpr float TacticsMaxCastStartDistance = 360.f;
	constexpr float TacticsTurnSeconds = 30.f;
	constexpr int32 TacticsMaxIdleSeconds = 20;
	constexpr float TacticsDeterminismSeconds = 15.f;

	struct FTDTacticsTestWorld
	{
		FTDTacticsTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDTacticsTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDTacticsTestWorld()
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

		void Tick(const float Duration)
		{
			for (float Remaining = Duration; Remaining > UE_SMALL_NUMBER; Remaining -= TacticsStepSeconds)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, TacticsStepSeconds);
			}
		}

		UWorld* World = nullptr;
	};

	struct FTDTacticsMonsterSample
	{
		bool bIsRegistered = false;
		bool bIsEngaged = false;
		bool bIsCasting = false;
		float TargetDistance = 0.f;
	};

	struct FTDTacticsStepSample
	{
		TArray<FTDTacticsMonsterSample> Monsters;
		uint64 CumulativeLineHash = 0;
	};

	struct FTDTacticsRunResult
	{
		TArray<FTDTacticsStepSample> Steps;
		int32 LineCountMismatchSteps = 0;
		int32 UnmatchedLineCount = 0;
		bool bIsSetupValid = false;
	};

	struct FTDTacticsCastStart
	{
		int32 StepIndex = 0;
		int32 MonsterIndex = 0;
		float TargetDistance = 0.f;
	};

	APawn* SpawnTacticsPlayerPawn(UWorld& World, const FVector& Location)
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
		Stats.BaseMaxHealth = TacticsPlayerMaxHealth;
		Combatant->SetStats(Stats);
		Combatant->RegisterComponent();
		APlayerController* PlayerController = World.SpawnActor<APlayerController>();
		PlayerController->SetActorTickEnabled(false);
		PlayerController->SetPawn(PlayerPawn);
		return PlayerPawn;
	}

	TArray<ATDMonsterCharacter*> SpawnTacticsGoblinRing(FTDTacticsTestWorld& Fixture, const int32 MonsterCount, const FVector& Center)
	{
		TArray<ATDMonsterCharacter*> Monsters;
		for (int32 Index = 0; Index < MonsterCount; ++Index)
		{
			const float AngleRadians = FMath::DegreesToRadians(static_cast<float>(Index) * 360.f / static_cast<float>(MonsterCount));
			const float Radius = TacticsRingInnerRadius + static_cast<float>(Index % TacticsRingRadiusVariants) * TacticsRingRadiusStep;
			const FVector Offset(Radius * FMath::Cos(AngleRadians), Radius * FMath::Sin(AngleRadians), 0.f);
			Monsters.Add(Fixture.SpawnMonster(TEXT("Goblin_Melee"), Center + Offset));
		}
		return Monsters;
	}

	FString ReadTacticsLineField(const FString& Line, const TCHAR* Key)
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

	FString RemoveTacticsActorField(const FString& Line)
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

	uint64 AccumulateTacticsHash(const uint64 Seed, const FString& Text)
	{
		uint64 Hash = Seed;
		for (const TCHAR Character : Text)
		{
			const uint32 Code = static_cast<uint32>(Character);
			for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
			{
				Hash ^= static_cast<uint64>((Code >> (ByteIndex * 8)) & 0xFF);
				Hash *= TacticsHashPrime;
			}
		}
		return Hash;
	}

	FTDTacticsRunResult RunTacticsScenario(const int32 MonsterCount, const float Seconds)
	{
		FTDTacticsRunResult Result;
		FTDTacticsTestWorld Fixture;
		const FVector PlayerLocation = FVector::ZeroVector;
		APawn* PlayerPawn = SpawnTacticsPlayerPawn(*Fixture.World, PlayerLocation);
		const TArray<ATDMonsterCharacter*> Monsters = SpawnTacticsGoblinRing(Fixture, MonsterCount, PlayerLocation);
		UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
		if (!PlayerPawn || !ThinkSubsystem || Monsters.Contains(nullptr) || UGameplayStatics::GetPlayerPawn(Fixture.World, 0) != PlayerPawn)
		{
			return Result;
		}
		Result.bIsSetupValid = true;

		TMap<FString, int32> MonsterIndexByName;
		for (int32 MonsterIndex = 0; MonsterIndex < Monsters.Num(); ++MonsterIndex)
		{
			MonsterIndexByName.Add(Monsters[MonsterIndex]->GetName(), MonsterIndex);
		}

		uint64 RunningHash = TacticsHashOffsetBasis;
		const int32 StepCount = FMath::RoundToInt(Seconds * static_cast<float>(TacticsStepsPerSecond));
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Fixture.Tick(TacticsStepSeconds);
			const TArray<FString> Lines = ThinkSubsystem->GetMonsterDebugLines();
			FTDTacticsStepSample& Sample = Result.Steps.AddDefaulted_GetRef();
			Sample.Monsters.SetNum(MonsterCount);
			if (Lines.Num() != MonsterCount)
			{
				++Result.LineCountMismatchSteps;
			}
			for (const FString& Line : Lines)
			{
				RunningHash = AccumulateTacticsHash(RunningHash, RemoveTacticsActorField(Line) + TEXT("\n"));
				const int32* MonsterIndex = MonsterIndexByName.Find(ReadTacticsLineField(Line, TEXT("actor")));
				if (!MonsterIndex)
				{
					++Result.UnmatchedLineCount;
					continue;
				}
				FTDTacticsMonsterSample& MonsterSample = Sample.Monsters[*MonsterIndex];
				MonsterSample.bIsRegistered = true;
				MonsterSample.bIsEngaged = ReadTacticsLineField(Line, TEXT("engaged")) == TEXT("1");
				MonsterSample.bIsCasting = ReadTacticsLineField(Line, TEXT("state")) == TEXT("Cast");
				MonsterSample.TargetDistance = FVector::Dist2D(Monsters[*MonsterIndex]->GetActorLocation(), PlayerPawn->GetActorLocation());
			}
			Sample.CumulativeLineHash = RunningHash;
		}
		return Result;
	}

	bool CheckTacticsRunSetup(FAutomationTestBase& Test, const FTDTacticsRunResult& Run, const int32 MonsterCount)
	{
		if (!Run.bIsSetupValid)
		{
			Test.AddError(TEXT("scenario setup failed: player pawn, think subsystem or monsters are missing"));
			return false;
		}
		if (Run.LineCountMismatchSteps > 0)
		{
			Test.AddError(FString::Printf(TEXT("debug line count differed from %d monsters on %d steps"), MonsterCount, Run.LineCountMismatchSteps));
			return false;
		}
		if (Run.UnmatchedLineCount > 0)
		{
			Test.AddError(FString::Printf(TEXT("%d debug lines named no spawned monster"), Run.UnmatchedLineCount));
			return false;
		}
		return true;
	}

	TArray<FTDTacticsCastStart> CollectTacticsCastStarts(const FTDTacticsRunResult& Run)
	{
		TArray<FTDTacticsCastStart> Starts;
		for (int32 StepIndex = 0; StepIndex < Run.Steps.Num(); ++StepIndex)
		{
			const FTDTacticsStepSample& Step = Run.Steps[StepIndex];
			for (int32 MonsterIndex = 0; MonsterIndex < Step.Monsters.Num(); ++MonsterIndex)
			{
				if (!Step.Monsters[MonsterIndex].bIsCasting)
				{
					continue;
				}
				const bool bWasCasting = StepIndex > 0 && Run.Steps[StepIndex - 1].Monsters[MonsterIndex].bIsCasting;
				if (bWasCasting)
				{
					continue;
				}
				FTDTacticsCastStart& Start = Starts.AddDefaulted_GetRef();
				Start.StepIndex = StepIndex;
				Start.MonsterIndex = MonsterIndex;
				Start.TargetDistance = Step.Monsters[MonsterIndex].TargetDistance;
			}
		}
		return Starts;
	}

	float ComputeTacticsMedian(TArray<float> Values)
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

	TSharedPtr<const FTDResolvedMonsterDefinition> ResolveTacticsProbeDefinition(const FString& FileName, const FString& Text, TArray<FString>& OutErrors)
	{
		TMap<FString, FString> Texts;
		Texts.Add(FileName, Text);
		TMap<FName, TSharedPtr<const FTDResolvedMonsterDefinition>> Definitions;
		UTDMonsterDefinitionLibrary::ResolveDefinitionTexts(Texts, [](FName) -> UTDDamageDefinition* { return GetMutableDefault<UTDDamageDefinition>(); }, Definitions, OutErrors);
		const TSharedPtr<const FTDResolvedMonsterDefinition>* Definition = Definitions.Find(FName(*FPaths::GetBaseFilename(FileName)));
		return Definition ? *Definition : nullptr;
	}

	int32 FindTacticsProbeAbilityIndex(const FTDResolvedMonsterDefinition& Definition, const FName AbilityName)
	{
		return Definition.Abilities.IndexOfByPredicate([AbilityName](const FTDResolvedAbility& Ability) { return Ability.Name == AbilityName; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTacticsConcurrentAttackersTest, "TDGame.MonsterAI.Tactics.ConcurrentAttackersFollowBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTacticsConcurrentAttackersTest::RunTest(const FString& Parameters)
{
	const FTDTacticsRunResult Run = RunTacticsScenario(TacticsBudgetMonsterCount, TacticsBudgetSeconds);
	if (!CheckTacticsRunSetup(*this, Run, TacticsBudgetMonsterCount))
	{
		return false;
	}
	const int32 CastStartCount = CollectTacticsCastStarts(Run).Num();
	if (CastStartCount == 0)
	{
		AddError(TEXT("no monster started a cast in the whole run, so the budget was never exercised"));
		return false;
	}
	int32 MaxConcurrentCasts = 0;
	int32 MaxConcurrentStep = INDEX_NONE;
	for (int32 StepIndex = 0; StepIndex < Run.Steps.Num(); ++StepIndex)
	{
		int32 ConcurrentCasts = 0;
		for (const FTDTacticsMonsterSample& MonsterSample : Run.Steps[StepIndex].Monsters)
		{
			ConcurrentCasts += MonsterSample.bIsCasting ? 1 : 0;
		}
		if (ConcurrentCasts > MaxConcurrentCasts)
		{
			MaxConcurrentCasts = ConcurrentCasts;
			MaxConcurrentStep = StepIndex;
		}
	}
	AddInfo(FString::Printf(TEXT("monsters %d, steps %d, cast starts %d, max concurrent casts %d (first reached at step %d)"), TacticsBudgetMonsterCount, Run.Steps.Num(), CastStartCount, MaxConcurrentCasts, MaxConcurrentStep));
	if (MaxConcurrentCasts > TacticsBudgetMaxConcurrentCasts)
	{
		AddError(FString::Printf(TEXT("max concurrent casts %d > %d (first reached at step %d)"), MaxConcurrentCasts, TacticsBudgetMaxConcurrentCasts, MaxConcurrentStep));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTacticsAttackStartSpacingTest, "TDGame.MonsterAI.Tactics.AttackStartsAreSpaced", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTacticsAttackStartSpacingTest::RunTest(const FString& Parameters)
{
	const FTDTacticsRunResult Run = RunTacticsScenario(TacticsCrowdMonsterCount, TacticsSpacingSeconds);
	if (!CheckTacticsRunSetup(*this, Run, TacticsCrowdMonsterCount))
	{
		return false;
	}
	const TArray<FTDTacticsCastStart> Starts = CollectTacticsCastStarts(Run);
	if (Starts.Num() < 2)
	{
		AddError(FString::Printf(TEXT("only %d cast starts in %.0f s, so spacing cannot be measured"), Starts.Num(), TacticsSpacingSeconds));
		return false;
	}
	int32 MinGapSteps = MAX_int32;
	int32 MinGapStartIndex = INDEX_NONE;
	int32 SameStepStartPairs = 0;
	for (int32 StartIndex = 1; StartIndex < Starts.Num(); ++StartIndex)
	{
		const int32 GapSteps = Starts[StartIndex].StepIndex - Starts[StartIndex - 1].StepIndex;
		SameStepStartPairs += GapSteps == 0 ? 1 : 0;
		if (GapSteps < MinGapSteps)
		{
			MinGapSteps = GapSteps;
			MinGapStartIndex = StartIndex;
		}
	}
	AddInfo(FString::Printf(TEXT("monsters %d, cast starts %d, min start gap %d steps (at step %d), same-step start pairs %d"), TacticsCrowdMonsterCount, Starts.Num(), MinGapSteps, Starts[MinGapStartIndex].StepIndex, SameStepStartPairs));
	if (MinGapSteps < TacticsMinStartSpacingSteps)
	{
		AddError(FString::Printf(TEXT("min start gap %d steps < %d (at step %d, monsters %d and %d)"), MinGapSteps, TacticsMinStartSpacingSteps, Starts[MinGapStartIndex].StepIndex, Starts[MinGapStartIndex - 1].MonsterIndex, Starts[MinGapStartIndex].MonsterIndex));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTacticsWaitRingTest, "TDGame.MonsterAI.Tactics.TokenlessMonstersHoldTheWaitRing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTacticsWaitRingTest::RunTest(const FString& Parameters)
{
	const FTDTacticsRunResult Run = RunTacticsScenario(TacticsCrowdMonsterCount, TacticsWaitRingSeconds);
	if (!CheckTacticsRunSetup(*this, Run, TacticsCrowdMonsterCount))
	{
		return false;
	}
	const int32 WindowBeginStep = FMath::RoundToInt(TacticsWaitRingWindowBeginSeconds * static_cast<float>(TacticsStepsPerSecond));
	TArray<float> WaiterDistances;
	for (int32 StepIndex = WindowBeginStep; StepIndex < Run.Steps.Num(); ++StepIndex)
	{
		for (const FTDTacticsMonsterSample& MonsterSample : Run.Steps[StepIndex].Monsters)
		{
			if (MonsterSample.bIsEngaged && !MonsterSample.bIsCasting)
			{
				WaiterDistances.Add(MonsterSample.TargetDistance);
			}
		}
	}
	const TArray<FTDTacticsCastStart> Starts = CollectTacticsCastStarts(Run);
	float MaxStartDistance = 0.f;
	for (const FTDTacticsCastStart& Start : Starts)
	{
		MaxStartDistance = FMath::Max(MaxStartDistance, Start.TargetDistance);
	}
	if (WaiterDistances.IsEmpty())
	{
		AddError(TEXT("no engaged non-casting monster samples in the measurement window"));
		return false;
	}
	const float MedianDistance = ComputeTacticsMedian(WaiterDistances);
	AddInfo(FString::Printf(TEXT("waiter samples %d, median target distance %.0f cm, nearest waiter %.0f cm, cast starts %d, farthest cast start %.0f cm"), WaiterDistances.Num(), MedianDistance, FMath::Min(WaiterDistances), Starts.Num(), MaxStartDistance));
	if (MedianDistance < TacticsMinWaiterMedianDistance)
	{
		AddError(FString::Printf(TEXT("median waiter distance %.0f cm < %.0f cm"), MedianDistance, TacticsMinWaiterMedianDistance));
	}
	if (MaxStartDistance >= TacticsMaxCastStartDistance)
	{
		AddError(FString::Printf(TEXT("farthest cast start distance %.0f cm >= %.0f cm"), MaxStartDistance, TacticsMaxCastStartDistance));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTacticsEveryMonsterTurnTest, "TDGame.MonsterAI.Tactics.EveryMonsterGetsATurn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTacticsEveryMonsterTurnTest::RunTest(const FString& Parameters)
{
	const FTDTacticsRunResult Run = RunTacticsScenario(TacticsCrowdMonsterCount, TacticsTurnSeconds);
	if (!CheckTacticsRunSetup(*this, Run, TacticsCrowdMonsterCount))
	{
		return false;
	}
	const TArray<FTDTacticsCastStart> Starts = CollectTacticsCastStarts(Run);
	TArray<int32> StartCounts;
	TArray<int32> IdleAnchorSteps;
	TArray<int32> LongestIdleSteps;
	StartCounts.Init(0, TacticsCrowdMonsterCount);
	LongestIdleSteps.Init(0, TacticsCrowdMonsterCount);
	IdleAnchorSteps.Init(0, TacticsCrowdMonsterCount);
	for (int32 MonsterIndex = 0; MonsterIndex < TacticsCrowdMonsterCount; ++MonsterIndex)
	{
		for (int32 StepIndex = 0; StepIndex < Run.Steps.Num(); ++StepIndex)
		{
			if (Run.Steps[StepIndex].Monsters[MonsterIndex].bIsEngaged)
			{
				IdleAnchorSteps[MonsterIndex] = StepIndex;
				break;
			}
		}
	}
	for (const FTDTacticsCastStart& Start : Starts)
	{
		++StartCounts[Start.MonsterIndex];
		LongestIdleSteps[Start.MonsterIndex] = FMath::Max(LongestIdleSteps[Start.MonsterIndex], Start.StepIndex - IdleAnchorSteps[Start.MonsterIndex]);
		IdleAnchorSteps[Start.MonsterIndex] = Start.StepIndex;
	}
	int32 DistinctCasters = 0;
	int32 WorstMonsterIndex = 0;
	FString StartCountSummary;
	for (int32 MonsterIndex = 0; MonsterIndex < TacticsCrowdMonsterCount; ++MonsterIndex)
	{
		LongestIdleSteps[MonsterIndex] = FMath::Max(LongestIdleSteps[MonsterIndex], Run.Steps.Num() - IdleAnchorSteps[MonsterIndex]);
		DistinctCasters += StartCounts[MonsterIndex] > 0 ? 1 : 0;
		if (LongestIdleSteps[MonsterIndex] > LongestIdleSteps[WorstMonsterIndex])
		{
			WorstMonsterIndex = MonsterIndex;
		}
		StartCountSummary += FString::Printf(TEXT("%s%d"), MonsterIndex > 0 ? TEXT(",") : TEXT(""), StartCounts[MonsterIndex]);
	}
	const int32 MaxIdleSteps = TacticsMaxIdleSeconds * TacticsStepsPerSecond;
	AddInfo(FString::Printf(TEXT("distinct casters %d of %d, starts per monster [%s], longest idle %.1f s (monster %d)"), DistinctCasters, TacticsCrowdMonsterCount, *StartCountSummary, static_cast<float>(LongestIdleSteps[WorstMonsterIndex]) / static_cast<float>(TacticsStepsPerSecond), WorstMonsterIndex));
	if (DistinctCasters < TacticsCrowdMonsterCount)
	{
		AddError(FString::Printf(TEXT("distinct casters %d < %d"), DistinctCasters, TacticsCrowdMonsterCount));
	}
	if (LongestIdleSteps[WorstMonsterIndex] > MaxIdleSteps)
	{
		AddError(FString::Printf(TEXT("monster %d went %.1f s without an attack > %d s"), WorstMonsterIndex, static_cast<float>(LongestIdleSteps[WorstMonsterIndex]) / static_cast<float>(TacticsStepsPerSecond), TacticsMaxIdleSeconds));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTacticsDeterministicTest, "TDGame.MonsterAI.Tactics.Deterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTacticsDeterministicTest::RunTest(const FString& Parameters)
{
	const FTDTacticsRunResult FirstRun = RunTacticsScenario(TacticsCrowdMonsterCount, TacticsDeterminismSeconds);
	const FTDTacticsRunResult SecondRun = RunTacticsScenario(TacticsCrowdMonsterCount, TacticsDeterminismSeconds);
	if (!CheckTacticsRunSetup(*this, FirstRun, TacticsCrowdMonsterCount) || !CheckTacticsRunSetup(*this, SecondRun, TacticsCrowdMonsterCount))
	{
		return false;
	}
	if (FirstRun.Steps.Num() != SecondRun.Steps.Num())
	{
		AddError(FString::Printf(TEXT("run lengths differ: %d vs %d steps"), FirstRun.Steps.Num(), SecondRun.Steps.Num()));
		return false;
	}
	int32 DifferingSteps = 0;
	int32 FirstDifferingStep = INDEX_NONE;
	for (int32 StepIndex = 0; StepIndex < FirstRun.Steps.Num(); ++StepIndex)
	{
		if (FirstRun.Steps[StepIndex].CumulativeLineHash == SecondRun.Steps[StepIndex].CumulativeLineHash)
		{
			continue;
		}
		++DifferingSteps;
		FirstDifferingStep = FirstDifferingStep == INDEX_NONE ? StepIndex : FirstDifferingStep;
	}
	AddInfo(FString::Printf(TEXT("steps %d, final hash %llu vs %llu, differing steps %d"), FirstRun.Steps.Num(), FirstRun.Steps.Last().CumulativeLineHash, SecondRun.Steps.Last().CumulativeLineHash, DifferingSteps));
	if (DifferingSteps > 0)
	{
		AddError(FString::Printf(TEXT("hash column differs on %d of %d steps, first at step %d"), DifferingSteps, FirstRun.Steps.Num(), FirstDifferingStep));
	}
	return true;
}

struct FTDCombatTacticsTestAccess
{
	static bool IsHoldingEngagedAttacker(const UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.IsHoldingEngagedAttacker(SlotIndex);
	}

	static bool IsLegacyHoldingEngagedAttacker(const UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		const FTDMonsterBrainFragment& Brain = Subsystem.Brains[SlotIndex];
		return Brain.IsEngaged() && Brain.Target.IsValid();
	}

	static bool IsAvoidingTacticsTarget(const UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.IsAvoidingTacticsTarget(SlotIndex);
	}

	static TArray<int32> GetOrderedSlots(const UTDMonsterThinkSubsystem& Subsystem)
	{
		return Subsystem.OrderedSlots;
	}

	static ETDTacticsTokenState GetTokenState(const UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.Tactics[SlotIndex].TokenState;
	}

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

	static void SetStepIndex(UTDMonsterThinkSubsystem& Subsystem, const uint64 ScenarioStep)
	{
		Subsystem.StepIndex = ScenarioStep;
	}

	static int32 AddScenarioRecord(UTDMonsterThinkSubsystem& Subsystem, AActor* Target)
	{
		return Subsystem.CreateTacticsRecord(Target);
	}

	static FTDTargetTacticsRecord& GetRecord(UTDMonsterThinkSubsystem& Subsystem, const int32 RecordIndex)
	{
		return Subsystem.TacticsRecords[RecordIndex];
	}

	static int32 AddScenarioSlot(UTDMonsterThinkSubsystem& Subsystem, const TSharedPtr<const FTDResolvedMonsterDefinition>& Definition, const FVector& Location)
	{
		const int32 SlotIndex = Subsystem.Bodies.AddDefaulted();
		Subsystem.Brains.AddDefaulted();
		Subsystem.Intents.AddDefaulted();
		Subsystem.Avoidances.AddDefaulted();
		Subsystem.Tactics.Add(FTDCombatTactics::MakeFragment(*Definition));
		Subsystem.Morales.Add(FTDPackMorale::MakeFragment(*Definition));
		FTDMonsterBodyFragment& Body = Subsystem.Bodies[SlotIndex];
		Body.SimulationId = Subsystem.NextSimulationId++;
		Body.Location = Location;
		FTDMonsterBrainFragment& Brain = Subsystem.Brains[SlotIndex];
		Brain.Definition = Definition;
		FTDMonsterActionExecutor::ResetForDefinition(Brain);
		Brain.Engagement = ETDMonsterEngagement::Engaged;
		Subsystem.OrderedSlots.Add(SlotIndex);
		return SlotIndex;
	}

	static bool AreAbilitiesReady(const UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		for (const int32 Cooldown : Subsystem.Brains[SlotIndex].AbilityCooldownSteps)
		{
			if (Cooldown != 0)
			{
				return false;
			}
		}
		return true;
	}

	static int32 GetWaitSector(const UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.Tactics[SlotIndex].WaitSector;
	}

	static FTDMonsterBrainFragment& GetBrain(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.Brains[SlotIndex];
	}

	static void RevokeInvalidReservation(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		Subsystem.RevokeInvalidReservation(SlotIndex);
	}

	static FTDMonsterTacticsFragment& GetTactics(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.Tactics[SlotIndex];
	}

	static void GrantPoolTokens(UTDMonsterThinkSubsystem& Subsystem, const int32 RecordIndex, const int32 PoolIndex)
	{
		FTDTargetTacticsRecord& Record = Subsystem.TacticsRecords[RecordIndex];
		Subsystem.GrantPoolTokens(Record, PoolIndex, Record.Target->GetActorLocation());
	}

	static void ConfirmAttackStart(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex, const int32 AbilityIndex)
	{
		Subsystem.Brains[SlotIndex].LastCastAbilityIndex = AbilityIndex;
		++Subsystem.Brains[SlotIndex].CastStartCount;
		Subsystem.ConfirmTacticsAttackStart(SlotIndex);
	}

	static void CountEngaged(UTDMonsterThinkSubsystem& Subsystem, const int32 RecordIndex)
	{
		Subsystem.CountTacticsEngaged(Subsystem.TacticsRecords[RecordIndex]);
	}
};

namespace TDCombatTacticsPhaseBTests
{
	constexpr float TacticsTestStepSeconds = 1.f / 64.f;

	struct FTDTacticsPhaseBWorld
	{
		FTDTacticsPhaseBWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDTacticsPhaseBWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDTacticsPhaseBWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		APawn* SpawnPlayer(const FVector& Location)
		{
			APawn* PlayerPawn = World->SpawnActor<APawn>();
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
			Stats.BaseMaxHealth = 100000.f;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			APlayerController* PlayerController = World->SpawnActor<APlayerController>();
			PlayerController->SetActorTickEnabled(false);
			PlayerController->SetPawn(PlayerPawn);
			return PlayerPawn;
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

		TArray<ATDMonsterCharacter*> SpawnMonsterRing(const FName DefinitionId, const int32 Count, const float Radius)
		{
			TArray<ATDMonsterCharacter*> Monsters;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float Angle = 2.f * UE_PI * static_cast<float>(Index) / static_cast<float>(Count);
				Monsters.Add(SpawnMonster(DefinitionId, FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f)));
			}
			return Monsters;
		}

		void TickSteps(const int32 Steps)
		{
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, TacticsTestStepSeconds);
			}
		}

		void TickSeconds(const float Seconds)
		{
			TickSteps(FMath::RoundToInt(Seconds * 64.f));
		}

		UTDMonsterThinkSubsystem* GetThinkSubsystem() const
		{
			return World->GetSubsystem<UTDMonsterThinkSubsystem>();
		}

		UWorld* World = nullptr;
	};

	FString ReadLineField(const FString& Line, const TCHAR* Key)
	{
		const FString Padded = FString(TEXT(" ")) + Line;
		const FString Token = FString(TEXT(" ")) + Key + TEXT("=");
		const int32 TokenStart = Padded.Find(Token, ESearchCase::CaseSensitive);
		if (TokenStart == INDEX_NONE)
		{
			return FString();
		}
		const int32 ValueStart = TokenStart + Token.Len();
		const int32 SpaceIndex = Padded.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
		const int32 ValueEnd = SpaceIndex == INDEX_NONE ? Padded.Len() : SpaceIndex;
		return Padded.Mid(ValueStart, ValueEnd - ValueStart);
	}

	FString FindMonsterLine(const UTDMonsterThinkSubsystem& ThinkSubsystem, const AActor* Monster)
	{
		const FString ActorName = Monster ? Monster->GetName() : FString();
		for (const FString& Line : ThinkSubsystem.GetMonsterDebugLines())
		{
			if (ReadLineField(Line, TEXT("actor")) == ActorName)
			{
				return Line;
			}
		}
		return FString();
	}

	int32 CountCastingMonsters(const UTDMonsterThinkSubsystem& ThinkSubsystem)
	{
		int32 CastingCount = 0;
		for (const FString& Line : ThinkSubsystem.GetMonsterDebugLines())
		{
			CastingCount += ReadLineField(Line, TEXT("state")) == TEXT("Cast") ? 1 : 0;
		}
		return CastingCount;
	}

	void HitMonsterFromPlayer(ATDMonsterCharacter& Monster, APawn& PlayerPawn, const float Damage)
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

	bool TickUntilMonsterLine(FTDTacticsPhaseBWorld& Fixture, const AActor* Monster, const int32 MaxSteps, TFunctionRef<bool(const FString&)> Predicate)
	{
		const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			Fixture.TickSteps(1);
			if (Predicate(FindMonsterLine(*ThinkSubsystem, Monster)))
			{
				return true;
			}
		}
		return false;
	}

	int32 MeasureLongestStartGap(FTDTacticsPhaseBWorld& Fixture, const int32 Steps, TFunctionRef<bool(const FString&)> IsTracked, int32& OutStartCount)
	{
		const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
		TMap<FString, bool> WasCastingByActor;
		int32 LastStartStep = 0;
		int32 LongestGap = 0;
		OutStartCount = 0;
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			Fixture.TickSteps(1);
			for (const FString& Line : ThinkSubsystem->GetMonsterDebugLines())
			{
				if (!IsTracked(Line))
				{
					continue;
				}
				const bool bIsCasting = ReadLineField(Line, TEXT("state")) == TEXT("Cast");
				bool& bWasCasting = WasCastingByActor.FindOrAdd(ReadLineField(Line, TEXT("actor")));
				if (bIsCasting && !bWasCasting)
				{
					LongestGap = FMath::Max(LongestGap, Step - LastStartStep);
					LastStartStep = Step;
					++OutStartCount;
				}
				bWasCasting = bIsCasting;
			}
		}
		return FMath::Max(LongestGap, Steps - LastStartStep);
	}

	bool CheckCrowdCostScene(FAutomationTestBase& Test, const TCHAR* SceneName, const TSharedPtr<const FTDResolvedMonsterDefinition>& Definition, const int32 ExpectedEngagedCostQuarters, const int32 ExpectedReservedCount)
	{
		constexpr uint64 ScenarioStep = 1000;
		constexpr int32 MonsterCount = 10;
		FTDTacticsPhaseBWorld Fixture;
		APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
		UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
		if (!Test.TestTrue(FString::Printf(TEXT("%s: the scene has a player and a think subsystem"), SceneName), PlayerPawn && ThinkSubsystem))
		{
			return false;
		}
		FTDCombatTacticsTestAccess::SetStepIndex(*ThinkSubsystem, ScenarioStep);
		const int32 RecordIndex = FTDCombatTacticsTestAccess::AddScenarioRecord(*ThinkSubsystem, PlayerPawn);
		TArray<int32> MonsterSlots;
		for (int32 MonsterIndex = 0; MonsterIndex < MonsterCount; ++MonsterIndex)
		{
			MonsterSlots.Add(FTDCombatTacticsTestAccess::AddScenarioSlot(*ThinkSubsystem, Definition, FVector(200.f + 30.f * static_cast<float>(MonsterIndex), 0.f, 0.f)));
		}
		FTDTargetTacticsRecord& Record = FTDCombatTacticsTestAccess::GetRecord(*ThinkSubsystem, RecordIndex);
		Record.MemberSlots = MonsterSlots;
		for (const int32 SlotIndex : MonsterSlots)
		{
			FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, SlotIndex).TargetRecordId = Record.RecordId;
		}
		FTDCombatTacticsTestAccess::CountEngaged(*ThinkSubsystem, RecordIndex);
		FTDTacticsPool& MeleePool = Record.Pools[FTDTargetTacticsRecord::MeleePoolIndex];
		Test.TestEqual(FString::Printf(TEXT("%s: every monster counts as engaged"), SceneName), MeleePool.EngagedCount, MonsterCount);
		Test.TestEqual(FString::Printf(TEXT("%s: the engaged cost adds every reserve cost"), SceneName), MeleePool.EngagedCostQuarters, ExpectedEngagedCostQuarters);
		MeleePool.BudgetQuarters = 2 * TDMonsterAI::QuartersPerToken;
		FTDCombatTacticsTestAccess::GrantPoolTokens(*ThinkSubsystem, RecordIndex, FTDTargetTacticsRecord::MeleePoolIndex);
		int32 ReservedCount = 0;
		for (const int32 SlotIndex : MonsterSlots)
		{
			ReservedCount += FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, SlotIndex) == ETDTacticsTokenState::Reserved ? 1 : 0;
		}
		Test.TestEqual(FString::Printf(TEXT("%s: the two-token budget reserves this many monsters"), SceneName), ReservedCount, ExpectedReservedCount);
		Test.TestEqual(FString::Printf(TEXT("%s: the reservations fill the budget exactly"), SceneName), MeleePool.HeldQuarters, MeleePool.BudgetQuarters);
		Test.TestTrue(FString::Printf(TEXT("%s: engaged cost above the budget forms a melee queue"), SceneName), Record.HasMeleeQueue());
		return true;
	}

	bool CheckSettlementScene(FAutomationTestBase& Test, const TCHAR* SceneName, const TSharedPtr<const FTDResolvedMonsterDefinition>& Definition, const FName StartedAbilityName, const ETDTacticsTokenState ExpectedFarthestState, const int32 ExpectedHeldQuarters)
	{
		constexpr uint64 ScenarioStep = 1000;
		constexpr int32 MonsterCount = 3;
		const int32 StartedAbilityIndex = FindTacticsProbeAbilityIndex(*Definition, StartedAbilityName);
		if (!Test.TestTrue(FString::Printf(TEXT("%s: the started ability exists"), SceneName), StartedAbilityIndex != INDEX_NONE))
		{
			return false;
		}
		FTDTacticsPhaseBWorld Fixture;
		APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
		UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
		if (!Test.TestTrue(FString::Printf(TEXT("%s: the scene has a player and a think subsystem"), SceneName), PlayerPawn && ThinkSubsystem))
		{
			return false;
		}
		FTDCombatTacticsTestAccess::SetStepIndex(*ThinkSubsystem, ScenarioStep);
		const int32 RecordIndex = FTDCombatTacticsTestAccess::AddScenarioRecord(*ThinkSubsystem, PlayerPawn);
		TArray<int32> MonsterSlots;
		for (int32 MonsterIndex = 0; MonsterIndex < MonsterCount; ++MonsterIndex)
		{
			MonsterSlots.Add(FTDCombatTacticsTestAccess::AddScenarioSlot(*ThinkSubsystem, Definition, FVector(200.f + 50.f * static_cast<float>(MonsterIndex), 0.f, 0.f)));
		}
		FTDTargetTacticsRecord& Record = FTDCombatTacticsTestAccess::GetRecord(*ThinkSubsystem, RecordIndex);
		Record.MemberSlots = MonsterSlots;
		for (const int32 SlotIndex : MonsterSlots)
		{
			FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, SlotIndex).TargetRecordId = Record.RecordId;
		}
		FTDCombatTacticsTestAccess::CountEngaged(*ThinkSubsystem, RecordIndex);
		FTDTacticsPool& MeleePool = Record.Pools[FTDTargetTacticsRecord::MeleePoolIndex];
		Test.TestEqual(FString::Printf(TEXT("%s: every monster counts as engaged"), SceneName), MeleePool.EngagedCount, MonsterCount);
		MeleePool.BudgetQuarters = 2 * TDMonsterAI::QuartersPerToken;
		FTDCombatTacticsTestAccess::GrantPoolTokens(*ThinkSubsystem, RecordIndex, FTDTargetTacticsRecord::MeleePoolIndex);
		const int32 NearestSlot = MonsterSlots[0];
		const int32 MiddleSlot = MonsterSlots[1];
		const int32 FarthestSlot = MonsterSlots[2];
		Test.TestTrue(FString::Printf(TEXT("%s: the two nearest monsters are reserved first"), SceneName),
			FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, NearestSlot) == ETDTacticsTokenState::Reserved
			&& FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, MiddleSlot) == ETDTacticsTokenState::Reserved);
		Test.TestEqual(FString::Printf(TEXT("%s: the first pass holds two full reservations"), SceneName), MeleePool.HeldQuarters, 2 * FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, NearestSlot).ReserveCostQuarters);
		Test.TestTrue(FString::Printf(TEXT("%s: the farthest monster has no token after the first pass"), SceneName), FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, FarthestSlot) == ETDTacticsTokenState::None);
		Test.TestTrue(FString::Printf(TEXT("%s: the farthest monster waits on deck after the first pass"), SceneName), FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, FarthestSlot).bIsOnDeck);
		FTDCombatTacticsTestAccess::ConfirmAttackStart(*ThinkSubsystem, NearestSlot, StartedAbilityIndex);
		Test.TestTrue(FString::Printf(TEXT("%s: the started attack commits the nearest token"), SceneName), FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, NearestSlot) == ETDTacticsTokenState::Committed);
		FTDCombatTacticsTestAccess::GrantPoolTokens(*ThinkSubsystem, RecordIndex, FTDTargetTacticsRecord::MeleePoolIndex);
		Test.TestEqual(FString::Printf(TEXT("%s: held quarters after the committed cost settles"), SceneName), MeleePool.HeldQuarters, ExpectedHeldQuarters);
		Test.TestEqual(FString::Printf(TEXT("%s: token state of the farthest monster after settling"), SceneName), static_cast<int32>(FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, FarthestSlot)), static_cast<int32>(ExpectedFarthestState));
		return true;
	}

	bool CheckRetaliationCostScene(FAutomationTestBase& Test, const TCHAR* SceneName, const TSharedPtr<const FTDResolvedMonsterDefinition>& HolderDefinition, const TSharedPtr<const FTDResolvedMonsterDefinition>& RetaliatorDefinition, const bool bHasNearHolder, const bool bShouldSteal, const int32 ExpectedHeldQuarters)
	{
		constexpr uint64 ScenarioStep = 1000;
		FTDTacticsPhaseBWorld Fixture;
		APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
		UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
		if (!Test.TestTrue(FString::Printf(TEXT("%s: the scene has a player and a think subsystem"), SceneName), PlayerPawn && ThinkSubsystem))
		{
			return false;
		}
		FTDCombatTacticsTestAccess::SetStepIndex(*ThinkSubsystem, ScenarioStep);
		const int32 RecordIndex = FTDCombatTacticsTestAccess::AddScenarioRecord(*ThinkSubsystem, PlayerPawn);
		FTDTargetTacticsRecord& Record = FTDCombatTacticsTestAccess::GetRecord(*ThinkSubsystem, RecordIndex);
		TArray<FVector> HolderLocations = { FVector(800.f, 0.f, 0.f) };
		if (bHasNearHolder)
		{
			HolderLocations.Add(FVector(-500.f, 0.f, 0.f));
		}
		TArray<int32> HolderSlots;
		for (const FVector& HolderLocation : HolderLocations)
		{
			const int32 HolderSlot = FTDCombatTacticsTestAccess::AddScenarioSlot(*ThinkSubsystem, HolderDefinition, HolderLocation);
			FTDMonsterTacticsFragment& Holder = FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, HolderSlot);
			Holder.TokenState = ETDTacticsTokenState::Reserved;
			Holder.TokenRecordId = Record.RecordId;
			Holder.TargetRecordId = Record.RecordId;
			Holder.GrantStep = ScenarioStep - 40;
			HolderSlots.Add(HolderSlot);
		}
		const int32 RetaliatorSlot = FTDCombatTacticsTestAccess::AddScenarioSlot(*ThinkSubsystem, RetaliatorDefinition, FVector(0.f, 300.f, 0.f));
		FTDMonsterTacticsFragment& Retaliator = FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, RetaliatorSlot);
		Retaliator.TargetRecordId = Record.RecordId;
		Retaliator.LastDamagedStep = ScenarioStep - 10;
		Record.MemberSlots = HolderSlots;
		Record.MemberSlots.Add(RetaliatorSlot);
		FTDTacticsPool& MeleePool = Record.Pools[FTDTargetTacticsRecord::MeleePoolIndex];
		MeleePool.EngagedCount = Record.MemberSlots.Num();
		MeleePool.BudgetQuarters = TDMonsterAI::QuartersPerToken;
		FTDCombatTacticsTestAccess::GrantPoolTokens(*ThinkSubsystem, RecordIndex, FTDTargetTacticsRecord::MeleePoolIndex);
		const bool bDidSteal = FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, RetaliatorSlot) == ETDTacticsTokenState::Reserved
			&& FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, HolderSlots[0]) == ETDTacticsTokenState::None;
		Test.TestTrue(FString::Printf(TEXT("%s: the retaliator takes the far reservation only when its cost fits"), SceneName), bDidSteal == bShouldSteal);
		Test.TestEqual(FString::Printf(TEXT("%s: held quarters after the retaliation pass"), SceneName), MeleePool.HeldQuarters, ExpectedHeldQuarters);
		Test.TestTrue(FString::Printf(TEXT("%s: the retaliation pass stays inside the budget"), SceneName), MeleePool.HeldQuarters <= MeleePool.BudgetQuarters);
		return true;
	}
}

namespace TDTacticsB = TDCombatTacticsPhaseBTests;

namespace
{
	constexpr float TacticsWallCenterY = 250.f;
	constexpr float TacticsWallHalfLength = 1500.f;
	constexpr float TacticsWallHalfThickness = 30.f;
	constexpr float TacticsWallHalfHeight = 200.f;
	constexpr float TacticsBlockedSpawnY = 520.f;
	constexpr float TacticsOpenSpawnY = -800.f;
	constexpr float TacticsUnreachableWarmupSeconds = 3.f;
	constexpr float TacticsUnreachableMeasureSeconds = 30.f;
	constexpr int32 TacticsMaxBlockedReservationSteps = 128;
	constexpr float TacticsMaxBlockedReservationShare = 0.2f;
	constexpr int32 TacticsMinOpenAttackStarts = 25;
	constexpr float TacticsCirclingSpawnRadius = 450.f;
	constexpr float TacticsCirclingSeconds = 20.f;
	constexpr int32 TacticsMinCirclingBitesPerHyena = 3;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsBudgetFormulaTest, "TDGame.MonsterAI.Tactics.BudgetFormula", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsBudgetFormulaTest::RunTest(const FString& Parameters)
{
	const int32 ExpectedMeleeTokens[31] = { 0, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 };
	for (int32 EngagedCount = 0; EngagedCount <= 30; ++EngagedCount)
	{
		TestEqual(FString::Printf(TEXT("Melee base tokens for N=%d"), EngagedCount), FTDCombatTactics::ComputeBaseTokens(EngagedCount, FTDCombatTactics::MaxTokensPerTarget), ExpectedMeleeTokens[EngagedCount]);
	}
	TestEqual(TEXT("Ranged base tokens stop at the ranged cap"), FTDCombatTactics::ComputeBaseTokens(30, FTDCombatTactics::MaxRangedTokens), 3);
	int32 MeleeTokens = 0;
	int32 RangedTokens = 0;
	FTDCombatTactics::ComputePoolBaseTokens(23, 0, MeleeTokens, RangedTokens);
	TestEqual(TEXT("A melee-only pool keeps all five tokens"), MeleeTokens, 5);
	FTDCombatTactics::ComputePoolBaseTokens(23, 9, MeleeTokens, RangedTokens);
	TestEqual(TEXT("Engaged ranged monsters cap melee at four tokens"), MeleeTokens, 4);
	TestEqual(TEXT("Engaged ranged monsters always keep one token"), RangedTokens, 1);
	FTDCombatTactics::ComputePoolBaseTokens(1, 1, MeleeTokens, RangedTokens);
	TestEqual(TEXT("One melee and one ranged monster each get a token"), MeleeTokens + RangedTokens, 2);
	FTDCombatTactics::ComputePoolBaseTokens(11, 9, MeleeTokens, RangedTokens);
	TestEqual(TEXT("Four melee tokens leave one ranged token"), RangedTokens, 1);
	FTDCombatTactics::ComputePoolBaseTokens(8, 9, MeleeTokens, RangedTokens);
	TestEqual(TEXT("Three melee tokens leave two ranged tokens"), RangedTokens, 2);
	TestEqual(TEXT("The pool sum never passes five"), MeleeTokens + RangedTokens, 5);
	TestEqual(TEXT("BuildUp keeps the full budget"), FTDCombatDirector::GetBudgetPercent(ETDCombatDirectorPhase::BuildUp), 100);
	TestEqual(TEXT("SustainPeak keeps the full budget"), FTDCombatDirector::GetBudgetPercent(ETDCombatDirectorPhase::SustainPeak), 100);
	TestEqual(TEXT("PeakFade uses three quarters"), FTDCombatDirector::GetBudgetPercent(ETDCombatDirectorPhase::PeakFade), 75);
	TestEqual(TEXT("Relax uses half"), FTDCombatDirector::GetBudgetPercent(ETDCombatDirectorPhase::Relax), 50);
	TestEqual(TEXT("No engaged monsters means no tokens"), FTDCombatTactics::ComputePoolTokens(0, 100), 0);
	TestEqual(TEXT("Three tokens at 100 percent"), FTDCombatTactics::ComputePoolTokens(3, 100), 3);
	TestEqual(TEXT("Three tokens at 75 percent round to two"), FTDCombatTactics::ComputePoolTokens(3, 75), 2);
	TestEqual(TEXT("Three tokens at 50 percent lose at most one"), FTDCombatTactics::ComputePoolTokens(3, 50), 2);
	TestEqual(TEXT("Four tokens at 75 percent"), FTDCombatTactics::ComputePoolTokens(4, 75), 3);
	TestEqual(TEXT("Five tokens at 50 percent lose at most one"), FTDCombatTactics::ComputePoolTokens(5, 50), 4);
	TestEqual(TEXT("Two tokens at 50 percent"), FTDCombatTactics::ComputePoolTokens(2, 50), 1);
	TestEqual(TEXT("One token never drops to zero"), FTDCombatTactics::ComputePoolTokens(1, 50), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsRoleClassificationTest, "TDGame.MonsterAI.Tactics.RoleClassification", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsRoleClassificationTest::RunTest(const FString& Parameters)
{
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!TestNotNull(TEXT("Definition library exists"), Library))
	{
		return false;
	}
	const TPair<FName, ETDTacticsRole> ExpectedRoles[] =
	{
		{ FName(TEXT("Goblin_Melee")), ETDTacticsRole::Melee },
		{ FName(TEXT("Hyena")), ETDTacticsRole::Melee },
		{ FName(TEXT("StoneGolem")), ETDTacticsRole::Melee },
		{ FName(TEXT("BookHead_Brute")), ETDTacticsRole::Melee },
		{ FName(TEXT("Archer")), ETDTacticsRole::Ranged },
		{ FName(TEXT("BookHead_Caster")), ETDTacticsRole::Ranged },
		{ FName(TEXT("Ogre_Boss")), ETDTacticsRole::Boss }
	};
	for (const TPair<FName, ETDTacticsRole>& Expected : ExpectedRoles)
	{
		const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = Library->FindDefinition(Expected.Key);
		if (!TestTrue(FString::Printf(TEXT("%s resolves"), *Expected.Key.ToString()), Definition.IsValid()))
		{
			continue;
		}
		const FTDMonsterTacticsFragment Fragment = FTDCombatTactics::MakeFragment(*Definition);
		TestEqual(FString::Printf(TEXT("%s role"), *Expected.Key.ToString()), static_cast<int32>(Fragment.Role), static_cast<int32>(Expected.Value));
	}
	const TSharedPtr<const FTDResolvedMonsterDefinition> Goblin = Library->FindDefinition(TEXT("Goblin_Melee"));
	const TSharedPtr<const FTDResolvedMonsterDefinition> Archer = Library->FindDefinition(TEXT("Archer"));
	if (Goblin.IsValid())
	{
		TestEqual(TEXT("Goblin melee reach comes from Slash"), FTDCombatTactics::MakeFragment(*Goblin).MeleeReachCm, 160);
		const TSharedRef<FTDResolvedMonsterDefinition> LargeHealthVariant = MakeShared<FTDResolvedMonsterDefinition>(*Goblin);
		LargeHealthVariant->Stats.MaxHealth = 5000.f;
		TestEqual(TEXT("A large health pool alone no longer makes a boss"), static_cast<int32>(FTDCombatTactics::MakeFragment(*LargeHealthVariant).Role), static_cast<int32>(ETDTacticsRole::Melee));
		const TSharedRef<FTDResolvedMonsterDefinition> RangedRoleVariant = MakeShared<FTDResolvedMonsterDefinition>(*Goblin);
		RangedRoleVariant->CombatRole = ETDMonsterCombatRole::Ranged;
		TestEqual(TEXT("An explicit Ranged role beats the reach estimate"), static_cast<int32>(FTDCombatTactics::MakeFragment(*RangedRoleVariant).Role), static_cast<int32>(ETDTacticsRole::Ranged));
		const TSharedRef<FTDResolvedMonsterDefinition> BossVariant = MakeShared<FTDResolvedMonsterDefinition>(*Goblin);
		BossVariant->bIsBoss = true;
		TestEqual(TEXT("The boss flag makes a boss"), static_cast<int32>(FTDCombatTactics::MakeFragment(*BossVariant).Role), static_cast<int32>(ETDTacticsRole::Boss));
	}
	if (Archer.IsValid())
	{
		TestEqual(TEXT("Archer ignores the inherited but unused Slash"), FTDCombatTactics::MakeFragment(*Archer).MeleeReachCm, 0);
		TestEqual(TEXT("Archer reach comes from Shoot"), FTDCombatTactics::MakeFragment(*Archer).MaxReachCm, 900);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsDirectorCyclesTest, "TDGame.MonsterAI.Tactics.DirectorCyclesUnderConstantPressure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsDirectorCyclesTest::RunTest(const FString& Parameters)
{
	constexpr uint64 TotalSteps = 60 * 64;
	constexpr uint64 HitPeriodSteps = 48;
	constexpr int32 HitPermille = 40;
	constexpr int32 TwoTokenEngagedCount = 5;
	FTDCombatDirectorState State;
	uint64 GrantFreezeUntilStep = 0;
	TArray<ETDCombatDirectorPhase> PhaseOrder = { State.Phase };
	int32 RelaxEntries = 0;
	uint64 LowBudgetSteps = 0;
	uint64 CalmPhaseSteps = 0;
	uint64 NextAttackStep = 1;
	const int32 BaseTokens = FTDCombatTactics::ComputeBaseTokens(TwoTokenEngagedCount, FTDCombatTactics::MaxTokensPerTarget);
	TestEqual(TEXT("Five engaged monsters give two base tokens"), BaseTokens, 2);
	for (uint64 Step = 1; Step <= TotalSteps; ++Step)
	{
		if (Step % HitPeriodSteps == 0)
		{
			FTDCombatDirector::ApplyHealthDrop(State, HitPermille, Step);
		}
		if (Step >= NextAttackStep)
		{
			FTDCombatDirector::AddIntensity(State, FTDCombatDirector::AttackStartIntensity);
			NextAttackStep = Step + FTDCombatDirector::GetStartSpacingSteps(State);
		}
		if (Step % FTDCombatTactics::RingPassPeriodSteps == 0)
		{
			FTDCombatDirector::RunRingPass(State, Step, 1000, GrantFreezeUntilStep);
		}
		if (State.Phase != PhaseOrder.Last())
		{
			PhaseOrder.Add(State.Phase);
			RelaxEntries += State.Phase == ETDCombatDirectorPhase::Relax ? 1 : 0;
		}
		LowBudgetSteps += FTDCombatTactics::ComputePoolTokens(BaseTokens, FTDCombatDirector::GetBudgetPercent(State.Phase)) <= 1 ? 1 : 0;
		CalmPhaseSteps += State.Phase == ETDCombatDirectorPhase::Relax || State.Phase == ETDCombatDirectorPhase::PeakFade ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Relax and PeakFade hold %llu of %llu steps; one token or less for %llu steps"), CalmPhaseSteps, TotalSteps, LowBudgetSteps));
	TestTrue(TEXT("Constant pressure still reaches Relax"), RelaxEntries >= 1);
	TestTrue(FString::Printf(TEXT("Budget of one token or less stays at or under 35 percent of the time (%llu of %llu steps)"), LowBudgetSteps, TotalSteps), LowBudgetSteps * 100 <= TotalSteps * 35);
	constexpr int32 ExpectedPhaseCount = 5;
	const ETDCombatDirectorPhase ExpectedOrder[ExpectedPhaseCount] = { ETDCombatDirectorPhase::BuildUp, ETDCombatDirectorPhase::SustainPeak, ETDCombatDirectorPhase::PeakFade, ETDCombatDirectorPhase::Relax, ETDCombatDirectorPhase::BuildUp };
	if (!TestTrue(TEXT("At least one full cycle happens in 60 seconds"), PhaseOrder.Num() >= ExpectedPhaseCount))
	{
		return false;
	}
	for (int32 Index = 0; Index < ExpectedPhaseCount; ++Index)
	{
		TestEqual(FString::Printf(TEXT("Phase %d follows the cycle"), Index), static_cast<int32>(PhaseOrder[Index]), static_cast<int32>(ExpectedOrder[Index]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsMercyWindowTest, "TDGame.MonsterAI.Tactics.MercyWindowOpensOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsMercyWindowTest::RunTest(const FString& Parameters)
{
	FTDCombatDirectorState HeldLowState;
	uint64 HeldLowFreeze = 0;
	int32 HeldLowOpenings = 0;
	bool bWasHeldLowOpen = false;
	for (uint64 Step = 1; Step <= 4000; ++Step)
	{
		if (Step % FTDCombatTactics::RingPassPeriodSteps == 0)
		{
			FTDCombatDirector::RunRingPass(HeldLowState, Step, 240, HeldLowFreeze);
		}
		HeldLowOpenings += HeldLowState.bIsMercyOpen && !bWasHeldLowOpen ? 1 : 0;
		bWasHeldLowOpen = HeldLowState.bIsMercyOpen;
	}
	TestEqual(TEXT("Health held at 24 percent opens the mercy window once"), HeldLowOpenings, 1);

	FTDCombatDirectorState State;
	uint64 GrantFreezeUntilStep = 0;
	int32 Openings = 0;
	bool bWasOpen = false;
	auto RunSteps = [&State, &GrantFreezeUntilStep, &Openings, &bWasOpen](const uint64 FirstStep, const uint64 LastStep, const int32 HealthPermille)
	{
		for (uint64 Step = FirstStep; Step <= LastStep; ++Step)
		{
			if (Step % FTDCombatTactics::RingPassPeriodSteps == 0)
			{
				FTDCombatDirector::RunRingPass(State, Step, HealthPermille, GrantFreezeUntilStep);
			}
			Openings += State.bIsMercyOpen && !bWasOpen ? 1 : 0;
			bWasOpen = State.bIsMercyOpen;
		}
	};
	RunSteps(1, 199, 240);
	TestEqual(TEXT("The first low-health ring pass opens the window"), Openings, 1);
	TestEqual(TEXT("Opening the window freezes new starts for 32 steps"), static_cast<int64>(GrantFreezeUntilStep), static_cast<int64>(64));
	TestEqual(TEXT("An open window adds 12 steps of spacing"), FTDCombatDirector::GetStartSpacingSteps(State), 32);
	RunSteps(200, 299, 410);
	TestTrue(TEXT("Recovering above 40 percent re-arms the window"), State.bIsMercyArmed);
	RunSteps(300, 1951, 240);
	TestEqual(TEXT("The lockout blocks a second window for 1920 steps"), Openings, 1);
	RunSteps(1952, 2100, 240);
	TestEqual(TEXT("A re-armed window opens again after the lockout"), Openings, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsTokenReleasedOnInterruptTest, "TDGame.MonsterAI.Tactics.TokenReleasedOnInterrupt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsTokenReleasedOnInterruptTest::RunTest(const FString& Parameters)
{
	TDTacticsB::FTDTacticsPhaseBWorld Fixture;
	APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(150.f, 0.f, 0.f));
	if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestNotNull(TEXT("Monster exists"), Monster) || !TestNotNull(TEXT("Think subsystem exists"), Fixture.GetThinkSubsystem()))
	{
		return false;
	}
	auto IsCastingWithCommittedToken = [](const FString& Line)
	{
		return TDTacticsB::ReadLineField(Line, TEXT("state")) == TEXT("Cast") && TDTacticsB::ReadLineField(Line, TEXT("token")) == TEXT("C");
	};
	auto HasNoToken = [](const FString& Line)
	{
		return TDTacticsB::ReadLineField(Line, TEXT("token")) == TEXT("-");
	};
	if (!TestTrue(TEXT("The goblin starts a slash with a committed token"), TDTacticsB::TickUntilMonsterLine(Fixture, Monster, 256, IsCastingWithCommittedToken)))
	{
		return false;
	}
	UTDStatusDefinition* Freeze = NewObject<UTDStatusDefinition>(Fixture.World);
	Freeze->Duration = 0.6f;
	Freeze->bFreezesTarget = true;
	FTDDamageContext FreezeContext;
	FreezeContext.Caster = PlayerPawn;
	FreezeContext.Stats = PlayerPawn->FindComponentByClass<UTDCombatComponent>()->GetStats();
	Monster->GetCombatComponent()->ApplyStatus(Freeze, FreezeContext);
	TestTrue(TEXT("Freezing mid-cast returns the token within one token pass"), TDTacticsB::TickUntilMonsterLine(Fixture, Monster, FTDCombatTactics::TokenPassPeriodSteps + 1, HasNoToken));
	Fixture.TickSeconds(1.5f);
	if (!TestTrue(TEXT("The goblin slashes again after thawing"), TDTacticsB::TickUntilMonsterLine(Fixture, Monster, 512, IsCastingWithCommittedToken)))
	{
		return false;
	}
	TDTacticsB::HitMonsterFromPlayer(*Monster, *PlayerPawn, 40.f);
	TestTrue(TEXT("A staggering hit mid-cast returns the token within one token pass"), TDTacticsB::TickUntilMonsterLine(Fixture, Monster, FTDCombatTactics::TokenPassPeriodSteps + 1, HasNoToken));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsFleeingMonsterTest, "TDGame.MonsterAI.Tactics.FleeingMonsterDoesNotStallTokens", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsFleeingMonsterTest::RunTest(const FString& Parameters)
{
	constexpr float ChipDamage = 13.f;
	constexpr float FleeHealth = 24.f;
	constexpr int32 MaxChipHits = 12;
	constexpr int32 MaxFleeWaitSteps = 6 * 64;
	TDTacticsB::FTDTacticsPhaseBWorld Fixture;
	APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
	const TArray<ATDMonsterCharacter*> Monsters = Fixture.SpawnMonsterRing(TEXT("Goblin_Melee"), 3, 300.f);
	const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
	if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestEqual(TEXT("Three goblins spawn"), Monsters.Num(), 3) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	Fixture.TickSeconds(2.f);
	ATDMonsterCharacter* Fleeing = Monsters[0];
	UTDCombatComponent* FleeingCombat = Fleeing->GetCombatComponent();
	int32 HitCount = 0;
	int32 FleeStartStep = INDEX_NONE;
	FString FleeStartToken;
	for (int32 Step = 1; Step <= MaxFleeWaitSteps && FleeStartStep == INDEX_NONE; ++Step)
	{
		if (FleeingCombat->GetCurrentHealth() > FleeHealth && HitCount < MaxChipHits)
		{
			TDTacticsB::HitMonsterFromPlayer(*Fleeing, *PlayerPawn, ChipDamage);
			++HitCount;
		}
		Fixture.TickSteps(1);
		const FString Line = TDTacticsB::FindMonsterLine(*ThinkSubsystem, Fleeing);
		if (TDTacticsB::ReadLineField(Line, TEXT("action")) == TEXT("Flee"))
		{
			FleeStartStep = Step;
			FleeStartToken = TDTacticsB::ReadLineField(Line, TEXT("token"));
		}
	}
	if (!TestTrue(TEXT("Chip damage below the stagger threshold leaves the goblin alive"), FleeingCombat->IsAlive()))
	{
		return false;
	}
	if (!TestTrue(TEXT("The wounded goblin chooses Flee"), FleeStartStep != INDEX_NONE))
	{
		AddInfo(FString::Printf(TEXT("No Flee within %d steps (%d chip hits, health %.1f)"), MaxFleeWaitSteps, HitCount, FleeingCombat->GetCurrentHealth()));
		return false;
	}
	int32 TokenClearSteps = 0;
	while (TDTacticsB::ReadLineField(TDTacticsB::FindMonsterLine(*ThinkSubsystem, Fleeing), TEXT("token")) != TEXT("-") && TokenClearSteps < 4 * FTDCombatTactics::TokenPassPeriodSteps)
	{
		Fixture.TickSteps(1);
		++TokenClearSteps;
	}
	AddInfo(FString::Printf(TEXT("Flee started %d steps after the first of %d chip hits with token=%s; token was clear %d steps later"), FleeStartStep, HitCount, *FleeStartToken, TokenClearSteps));
	const FString FleeingName = Fleeing->GetName();
	int32 StartCount = 0;
	const int32 LongestGap = TDTacticsB::MeasureLongestStartGap(Fixture, 8 * 64, [&FleeingName](const FString& Line)
	{
		return TDTacticsB::ReadLineField(Line, TEXT("actor")) != FleeingName;
	}, StartCount);
	AddInfo(FString::Printf(TEXT("Other goblins: %d attack starts, longest gap %d steps"), StartCount, LongestGap));
	TestTrue(FString::Printf(TEXT("The other goblins keep attacking (%d starts)"), StartCount), StartCount >= 2);
	TestTrue(FString::Printf(TEXT("The longest gap between the other goblins' attack starts is at most 128 steps (%d)"), LongestGap), LongestGap <= 128);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsAttackedWaiterTest, "TDGame.MonsterAI.Tactics.AttackedWaiterRetaliates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsAttackedWaiterTest::RunTest(const FString& Parameters)
{
	TDTacticsB::FTDTacticsPhaseBWorld Fixture;
	APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
	const TArray<ATDMonsterCharacter*> Monsters = Fixture.SpawnMonsterRing(TEXT("Goblin_Melee"), 10, 450.f);
	const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
	if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	TMap<FString, int32> LastAttackEndStepByActor;
	TMap<FString, bool> WasCastingByActor;
	for (int32 Step = 1; Step <= 8 * 64; ++Step)
	{
		Fixture.TickSteps(1);
		for (const FString& Line : ThinkSubsystem->GetMonsterDebugLines())
		{
			const FString ActorName = TDTacticsB::ReadLineField(Line, TEXT("actor"));
			const bool bIsCasting = TDTacticsB::ReadLineField(Line, TEXT("state")) == TEXT("Cast");
			bool& bWasCasting = WasCastingByActor.FindOrAdd(ActorName);
			if (bWasCasting && !bIsCasting)
			{
				LastAttackEndStepByActor.Add(ActorName, Step);
			}
			bWasCasting = bIsCasting;
		}
	}
	ATDMonsterCharacter* Waiter = nullptr;
	int32 WaiterAttackEndStep = INDEX_NONE;
	for (ATDMonsterCharacter* Monster : Monsters)
	{
		const FString Line = TDTacticsB::FindMonsterLine(*ThinkSubsystem, Monster);
		const bool bIsWaiting = TDTacticsB::ReadLineField(Line, TEXT("token")) == TEXT("-")
			&& TDTacticsB::ReadLineField(Line, TEXT("slot")).StartsWith(TEXT("W"))
			&& TDTacticsB::ReadLineField(Line, TEXT("state")) != TEXT("Cast");
		const int32* AttackEndStep = LastAttackEndStepByActor.Find(Monster->GetName());
		const int32 EndStep = AttackEndStep ? *AttackEndStep : 0;
		if (bIsWaiting && (!Waiter || EndStep > WaiterAttackEndStep))
		{
			Waiter = Monster;
			WaiterAttackEndStep = EndStep;
		}
	}
	if (!TestNotNull(TEXT("A tokenless goblin holds a wait slot"), Waiter))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Chosen waiter %s last finished an attack at step %d of the warm-up"), *Waiter->GetName(), WaiterAttackEndStep));
	const int32 WaiterSlot = FTDCombatTacticsTestAccess::FindSlotForActor(*ThinkSubsystem, Waiter);
	if (!TestTrue(TEXT("The waiter has a brain slot"), WaiterSlot != INDEX_NONE))
	{
		return false;
	}
	int32 CooldownWaitSteps = 0;
	while (!FTDCombatTacticsTestAccess::AreAbilitiesReady(*ThinkSubsystem, WaiterSlot) && CooldownWaitSteps < 4 * 64)
	{
		Fixture.TickSteps(1);
		++CooldownWaitSteps;
	}
	if (!TestTrue(TEXT("The waiter's abilities come off cooldown before the first hit"), FTDCombatTacticsTestAccess::AreAbilitiesReady(*ThinkSubsystem, WaiterSlot)))
	{
		return false;
	}
	const int32 HitSpacingSteps = FTDCombatTactics::TokenPassPeriodSteps;
	const int32 LastHitStep = 2 * HitSpacingSteps;
	const int32 TokenWindowSteps = FTDCombatTactics::RetaliationSteps + FTDCombatTactics::TokenPassPeriodSteps;
	int32 TokenArrivalStep = INDEX_NONE;
	bool bDidRetaliate = false;
	for (int32 Step = 0; Step <= LastHitStep + TokenWindowSteps && !bDidRetaliate; ++Step)
	{
		if (Step <= LastHitStep && Step % HitSpacingSteps == 0)
		{
			TDTacticsB::HitMonsterFromPlayer(*Waiter, *PlayerPawn, 2.f);
		}
		Fixture.TickSteps(1);
		const FString Line = TDTacticsB::FindMonsterLine(*ThinkSubsystem, Waiter);
		if (TokenArrivalStep == INDEX_NONE && TDTacticsB::ReadLineField(Line, TEXT("token")) != TEXT("-"))
		{
			TokenArrivalStep = Step + 1;
		}
		bDidRetaliate = TDTacticsB::ReadLineField(Line, TEXT("state")) == TEXT("Cast");
	}
	AddInfo(FString::Printf(TEXT("Cooldown wait %d steps; token arrived %d steps after the first hit; retaliated=%d"), CooldownWaitSteps, TokenArrivalStep, bDidRetaliate ? 1 : 0));
	TestTrue(FString::Printf(TEXT("The struck waiter receives a token within %d steps of the first hit (%d)"), TokenWindowSteps, TokenArrivalStep), TokenArrivalStep != INDEX_NONE && TokenArrivalStep <= TokenWindowSteps);
	TestTrue(TEXT("The struck waiter attacks within the retaliation window plus one token pass after the last hit"), bDidRetaliate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsBossLaneTest, "TDGame.MonsterAI.Tactics.BossLaneNotStarved", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsBossLaneTest::RunTest(const FString& Parameters)
{
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!TestNotNull(TEXT("Definition library exists"), Library))
	{
		return false;
	}
	const TSharedPtr<const FTDResolvedMonsterDefinition> Ogre = Library->FindDefinition(TEXT("Ogre_Boss"));
	if (!TestTrue(TEXT("Ogre_Boss resolves"), Ogre.IsValid()))
	{
		return false;
	}
	const FTDResolvedAbility* Smash = Ogre->Abilities.FindByPredicate([](const FTDResolvedAbility& Ability) { return Ability.Name == TEXT("Smash"); });
	if (!TestNotNull(TEXT("Ogre_Boss has Smash"), Smash))
	{
		return false;
	}
	TDTacticsB::FTDTacticsPhaseBWorld Fixture;
	APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
	ATDMonsterCharacter* Boss = Fixture.SpawnMonster(TEXT("Ogre_Boss"), FVector(350.f, 0.f, 0.f));
	Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(0.f, 400.f, 0.f));
	Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(-400.f, 0.f, 0.f));
	Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(0.f, -400.f, 0.f));
	if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestNotNull(TEXT("Boss exists"), Boss) || !TestNotNull(TEXT("Think subsystem exists"), Fixture.GetThinkSubsystem()))
	{
		return false;
	}
	const FString BossName = Boss->GetName();
	int32 StartCount = 0;
	const int32 LongestGap = TDTacticsB::MeasureLongestStartGap(Fixture, 20 * 64, [&BossName](const FString& Line)
	{
		return TDTacticsB::ReadLineField(Line, TEXT("actor")) == BossName;
	}, StartCount);
	const int32 AllowedGap = Smash->CooldownSteps + 64;
	TestTrue(FString::Printf(TEXT("The boss attacks repeatedly (%d starts)"), StartCount), StartCount >= 2);
	TestTrue(FString::Printf(TEXT("The longest gap between boss attack starts is at most cooldown + 64 steps (%d of %d)"), LongestGap, AllowedGap), LongestGap <= AllowedGap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsEncounterResetTest, "TDGame.MonsterAI.Tactics.EncounterResetRestoresGrace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsEncounterResetTest::RunTest(const FString& Parameters)
{
	TDTacticsB::FTDTacticsPhaseBWorld Fixture;
	APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
	Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(600.f, -200.f, 0.f));
	Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(600.f, 0.f, 0.f));
	Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(600.f, 200.f, 0.f));
	const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
	if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	Fixture.TickSeconds(2.f);
	const TArray<FString> FirstRecords = ThinkSubsystem->GetTacticsDebugLines();
	if (!TestEqual(TEXT("The first engagement opens one record"), FirstRecords.Num(), 1))
	{
		return false;
	}
	const FString FirstRecordId = TDTacticsB::ReadLineField(FirstRecords[0], TEXT("record"));
	PlayerPawn->SetActorLocation(FVector(6000.f, 0.f, 0.f));
	Fixture.TickSeconds(10.f);
	TestEqual(TEXT("The record is dropped after everyone goes home"), ThinkSubsystem->GetTacticsDebugLines().Num(), 0);
	PlayerPawn->SetActorLocation(FVector::ZeroVector);
	FString ReopenedRecord;
	for (int32 Step = 0; Step < 4 * 64 && ReopenedRecord.IsEmpty(); ++Step)
	{
		Fixture.TickSteps(1);
		const TArray<FString> Records = ThinkSubsystem->GetTacticsDebugLines();
		ReopenedRecord = Records.IsEmpty() ? FString() : Records[0];
	}
	if (!TestFalse(TEXT("Re-engaging opens a record"), ReopenedRecord.IsEmpty()))
	{
		return false;
	}
	TestNotEqual(TEXT("The reopened record is new"), TDTacticsB::ReadLineField(ReopenedRecord, TEXT("record")), FirstRecordId);
	TestEqual(TEXT("The new encounter starts with the opening grace"), TDTacticsB::ReadLineField(ReopenedRecord, TEXT("grace")), FString(TEXT("1")));
	TestEqual(TEXT("The new encounter starts in BuildUp"), TDTacticsB::ReadLineField(ReopenedRecord, TEXT("phase")), FString(TEXT("BuildUp")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsLegacyCapTest, "TDGame.MonsterAI.Tactics.TacticsOffKeepsLegacyCap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsLegacyCapTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* TacticsVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.MonsterAI.Tactics"));
	if (!TestNotNull(TEXT("Tactics console variable exists"), TacticsVariable))
	{
		return false;
	}
	const int32 PreviousValue = TacticsVariable->GetInt();
	TacticsVariable->Set(0, ECVF_SetByCode);
	ON_SCOPE_EXIT
	{
		TacticsVariable->Set(PreviousValue, ECVF_SetByCode);
	};
	if (!TestEqual(TEXT("The tactics console variable switches off"), TacticsVariable->GetInt(), 0))
	{
		return false;
	}
	TDTacticsB::FTDTacticsPhaseBWorld Fixture;
	APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
	Fixture.SpawnMonsterRing(TEXT("Goblin_Melee"), 6, 300.f);
	const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
	if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	int32 MaxConcurrentCasts = 0;
	bool bHasTacticsToken = false;
	for (int32 Step = 0; Step < 20 * 64; ++Step)
	{
		Fixture.TickSteps(1);
		MaxConcurrentCasts = FMath::Max(MaxConcurrentCasts, TDTacticsB::CountCastingMonsters(*ThinkSubsystem));
		for (const int32 SlotIndex : FTDCombatTacticsTestAccess::GetOrderedSlots(*ThinkSubsystem))
		{
			bHasTacticsToken |= FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, SlotIndex) != ETDTacticsTokenState::None;
		}
	}
	TestTrue(FString::Printf(TEXT("Tactics off allows the legacy three concurrent attackers (%d)"), MaxConcurrentCasts), MaxConcurrentCasts >= 3);
	TestFalse(TEXT("Tactics off hands out no tokens"), bHasTacticsToken);
	TestTrue(TEXT("Tactics off keeps no target records"), ThinkSubsystem->GetTacticsDebugLines().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsHoldingAttackerTest, "TDGame.MonsterAI.Tactics.HoldingAttackerStillHoldsWithoutQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsHoldingAttackerTest::RunTest(const FString& Parameters)
{
	{
		TDTacticsB::FTDTacticsPhaseBWorld Fixture;
		APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
		Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(300.f, 0.f, 0.f));
		const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
		if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
		{
			return false;
		}
		int32 EngagedSteps = 0;
		int32 Mismatches = 0;
		int32 AvoidingSteps = 0;
		for (int32 Step = 0; Step < 5 * 64; ++Step)
		{
			Fixture.TickSteps(1);
			const TArray<int32> Slots = FTDCombatTacticsTestAccess::GetOrderedSlots(*ThinkSubsystem);
			if (!TestEqual(FString::Printf(TEXT("Exactly one brain slot at step %d"), Step), Slots.Num(), 1))
			{
				break;
			}
			if (!TestTrue(FString::Printf(TEXT("A single monster holds no wait slot at step %d"), Step), FTDCombatTacticsTestAccess::GetWaitSector(*ThinkSubsystem, Slots[0]) < 0))
			{
				break;
			}
			const bool bIsLegacyHolding = FTDCombatTacticsTestAccess::IsLegacyHoldingEngagedAttacker(*ThinkSubsystem, Slots[0]);
			EngagedSteps += bIsLegacyHolding ? 1 : 0;
			Mismatches += FTDCombatTacticsTestAccess::IsHoldingEngagedAttacker(*ThinkSubsystem, Slots[0]) != bIsLegacyHolding ? 1 : 0;
			AvoidingSteps += FTDCombatTacticsTestAccess::IsAvoidingTacticsTarget(*ThinkSubsystem, Slots[0]) ? 1 : 0;
		}
		TestTrue(TEXT("The single goblin engages"), EngagedSteps > 0);
		TestEqual(TEXT("A single monster yields exactly as before"), Mismatches, 0);
		TestEqual(TEXT("A single monster never steers around its target"), AvoidingSteps, 0);
	}
	TDTacticsB::FTDTacticsPhaseBWorld Fixture;
	APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
	const TArray<ATDMonsterCharacter*> Monsters = Fixture.SpawnMonsterRing(TEXT("Goblin_Melee"), 10, 450.f);
	const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
	if (!TestNotNull(TEXT("Crowd player exists"), PlayerPawn) || !TestNotNull(TEXT("Crowd think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	Fixture.TickSeconds(8.f);
	int32 WaiterSlot = INDEX_NONE;
	for (ATDMonsterCharacter* Monster : Monsters)
	{
		const FString Line = TDTacticsB::FindMonsterLine(*ThinkSubsystem, Monster);
		const bool bIsWaiting = TDTacticsB::ReadLineField(Line, TEXT("token")) == TEXT("-")
			&& TDTacticsB::ReadLineField(Line, TEXT("slot")).StartsWith(TEXT("W"))
			&& TDTacticsB::ReadLineField(Line, TEXT("state")) != TEXT("Cast");
		if (bIsWaiting)
		{
			WaiterSlot = FTDCombatTacticsTestAccess::FindSlotForActor(*ThinkSubsystem, Monster);
			break;
		}
	}
	if (!TestTrue(TEXT("A tokenless goblin holds a wait slot in the crowd"), WaiterSlot != INDEX_NONE))
	{
		return false;
	}
	TestTrue(TEXT("A wait-slot goblin steers around its target"), FTDCombatTacticsTestAccess::IsAvoidingTacticsTarget(*ThinkSubsystem, WaiterSlot));
	TestFalse(TEXT("A wait-slot goblin yields instead of holding"), FTDCombatTacticsTestAccess::IsHoldingEngagedAttacker(*ThinkSubsystem, WaiterSlot));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsRetaliationStealTest, "TDGame.MonsterAI.Tactics.RetaliationStealsFarReservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsRetaliationStealTest::RunTest(const FString& Parameters)
{
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!TestNotNull(TEXT("Definition library exists"), Library))
	{
		return false;
	}
	const TSharedPtr<const FTDResolvedMonsterDefinition> Goblin = Library->FindDefinition(TEXT("Goblin_Melee"));
	if (!TestTrue(TEXT("Goblin_Melee resolves"), Goblin.IsValid()))
	{
		return false;
	}
	auto RunScenario = [&Goblin](const float HolderDistance, bool& bOutDidSteal, bool& bOutIsRetaliatorOnDeck)
	{
		const uint64 ScenarioStep = 1000;
		const float RetaliatorDistance = 300.f;
		TDTacticsB::FTDTacticsPhaseBWorld Fixture;
		APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
		UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
		if (!PlayerPawn || !ThinkSubsystem)
		{
			return false;
		}
		FTDCombatTacticsTestAccess::SetStepIndex(*ThinkSubsystem, ScenarioStep);
		const int32 RecordIndex = FTDCombatTacticsTestAccess::AddScenarioRecord(*ThinkSubsystem, PlayerPawn);
		const int32 HolderSlot = FTDCombatTacticsTestAccess::AddScenarioSlot(*ThinkSubsystem, Goblin, FVector(HolderDistance, 0.f, 0.f));
		const int32 RetaliatorSlot = FTDCombatTacticsTestAccess::AddScenarioSlot(*ThinkSubsystem, Goblin, FVector(0.f, RetaliatorDistance, 0.f));
		FTDTargetTacticsRecord& Record = FTDCombatTacticsTestAccess::GetRecord(*ThinkSubsystem, RecordIndex);
		Record.MemberSlots = { HolderSlot, RetaliatorSlot };
		Record.Pools[FTDTargetTacticsRecord::MeleePoolIndex].EngagedCount = 2;
		Record.Pools[FTDTargetTacticsRecord::MeleePoolIndex].BudgetQuarters = TDMonsterAI::QuartersPerToken;
		FTDMonsterTacticsFragment& Holder = FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, HolderSlot);
		Holder.TokenState = ETDTacticsTokenState::Reserved;
		Holder.TokenRecordId = Record.RecordId;
		Holder.TargetRecordId = Record.RecordId;
		Holder.GrantStep = ScenarioStep - 40;
		FTDMonsterTacticsFragment& Retaliator = FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, RetaliatorSlot);
		Retaliator.TargetRecordId = Record.RecordId;
		Retaliator.LastDamagedStep = ScenarioStep - 10;
		FTDCombatTacticsTestAccess::GrantPoolTokens(*ThinkSubsystem, RecordIndex, FTDTargetTacticsRecord::MeleePoolIndex);
		bOutDidSteal = FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, RetaliatorSlot) == ETDTacticsTokenState::Reserved
			&& FTDCombatTacticsTestAccess::GetTokenState(*ThinkSubsystem, HolderSlot) == ETDTacticsTokenState::None;
		bOutIsRetaliatorOnDeck = FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, RetaliatorSlot).bIsOnDeck;
		return true;
	};
	bool bDidStealFar = false;
	bool bIsOnDeckFar = false;
	if (!TestTrue(TEXT("The far-holder scenario runs"), RunScenario(800.f, bDidStealFar, bIsOnDeckFar)))
	{
		return false;
	}
	TestTrue(TEXT("A retaliator takes the token from a reserved holder at least twice as far"), bDidStealFar);
	TestFalse(TEXT("The retaliator that took the token is not on deck"), bIsOnDeckFar);
	bool bDidStealNear = false;
	bool bIsOnDeckNear = false;
	if (!TestTrue(TEXT("The near-holder scenario runs"), RunScenario(500.f, bDidStealNear, bIsOnDeckNear)))
	{
		return false;
	}
	TestFalse(TEXT("A holder closer than twice the retaliator's distance keeps its token"), bDidStealNear);
	TestTrue(TEXT("The retaliator waits on deck instead"), bIsOnDeckNear);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsWeightedTokenCostsTest, "TDGame.MonsterAI.Tactics.WeightedTokenCosts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsWeightedTokenCostsTest::RunTest(const FString& Parameters)
{
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!TestNotNull(TEXT("Definition library exists"), Library))
	{
		return false;
	}
	const TSharedPtr<const FTDResolvedMonsterDefinition> Goblin = Library->FindDefinition(TEXT("Goblin_Melee"));
	if (!TestTrue(TEXT("Goblin_Melee resolves"), Goblin.IsValid()))
	{
		return false;
	}
	const FString ContactText = TEXT(R"({ "schema": 1, "id": "CostContact",
  "stats": { "max_health": 100, "attack_power": 1, "armor": 0, "move_speed": 100, "team": 2 },
  "abilities": {
    "Touch": { "spell": "DA_Any", "range": 150, "cooldown": 1, "token_cost": 0.25 } },
  "actions": [
    { "id": "Touch", "do": "CastAbility", "args": { "ability": "Touch" }, "weight": 1 },
    { "id": "Idle", "do": "Wait", "weight": 1 } ] })");
	const FString MixedText = TEXT(R"({ "schema": 1, "id": "CostMixed",
  "stats": { "max_health": 100, "attack_power": 1, "armor": 0, "move_speed": 100, "team": 2 },
  "abilities": {
    "Nip": { "spell": "DA_Any", "range": 150, "cooldown": 1, "token_cost": 0.25 },
    "Maul": { "spell": "DA_Any", "range": 180, "cooldown": 1, "token_cost": 0.75 },
    "Unused": { "spell": "DA_Any", "range": 150, "cooldown": 1, "token_cost": 1 } },
  "actions": [
    { "id": "Nip", "do": "CastAbility", "args": { "ability": "Nip" }, "weight": 1 },
    { "id": "Maul", "do": "CastAbility", "args": { "ability": "Maul" }, "weight": 1 },
    { "id": "Idle", "do": "Wait", "weight": 1 } ] })");
	TArray<FString> Errors;
	const TSharedPtr<const FTDResolvedMonsterDefinition> CostContact = ResolveTacticsProbeDefinition(TEXT("CostContact.json"), ContactText, Errors);
	const TSharedPtr<const FTDResolvedMonsterDefinition> CostMixed = ResolveTacticsProbeDefinition(TEXT("CostMixed.json"), MixedText, Errors);
	if (!TestTrue(TEXT("The cost probe definitions resolve"), CostContact.IsValid() && CostMixed.IsValid()))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	TestEqual(TEXT("The reserve cost follows the dearest referenced ability and ignores the unreferenced one"), FTDCombatTactics::MakeFragment(*CostMixed).ReserveCostQuarters, 3);
	TestEqual(TEXT("A contact-only monster reserves one quarter token"), FTDCombatTactics::MakeFragment(*CostContact).ReserveCostQuarters, 1);
	TestEqual(TEXT("Goblin_Melee keeps the whole-token reserve cost"), FTDCombatTactics::MakeFragment(*Goblin).ReserveCostQuarters, TDMonsterAI::QuartersPerToken);

	FTDMonsterTacticsFragment CheapFragment = FTDCombatTactics::MakeFragment(*CostMixed);
	CheapFragment.TokenState = ETDTacticsTokenState::Reserved;
	TestEqual(TEXT("A reserved fragment holds its reserve cost"), FTDCombatTactics::GetHeldCostQuarters(CheapFragment), 3);
	FTDCombatTactics::CommitToken(CheapFragment, 1);
	TestTrue(TEXT("A cheap first cast commits the reservation"), CheapFragment.TokenState == ETDTacticsTokenState::Committed);
	TestEqual(TEXT("A committed cheap cast holds only its own cost"), FTDCombatTactics::GetHeldCostQuarters(CheapFragment), 1);
	FTDCombatTactics::CommitToken(CheapFragment, 3);
	TestEqual(TEXT("A later dearer cast raises the held cost"), FTDCombatTactics::GetHeldCostQuarters(CheapFragment), 3);
	FTDMonsterTacticsFragment TokenlessFragment = FTDCombatTactics::MakeFragment(*CostMixed);
	FTDCombatTactics::CommitToken(TokenlessFragment, 1);
	TestTrue(TEXT("Committing without a token leaves the state alone"), TokenlessFragment.TokenState == ETDTacticsTokenState::None);
	TestEqual(TEXT("Committing without a token holds nothing"), FTDCombatTactics::GetHeldCostQuarters(TokenlessFragment), 0);
	FTDMonsterTacticsFragment OverpaidFragment = FTDCombatTactics::MakeFragment(*CostMixed);
	OverpaidFragment.TokenState = ETDTacticsTokenState::Reserved;
	FTDCombatTactics::CommitToken(OverpaidFragment, 9);
	TestEqual(TEXT("A cast cost above the reservation is capped at the reserved cost"), FTDCombatTactics::GetHeldCostQuarters(OverpaidFragment), 3);

	FTDTargetTacticsRecord QueueRecord;
	FTDTacticsPool& QueuePool = QueueRecord.Pools[FTDTargetTacticsRecord::MeleePoolIndex];
	QueuePool.EngagedCount = 6;
	QueuePool.EngagedCostQuarters = 6;
	QueuePool.BudgetQuarters = 2 * TDMonsterAI::QuartersPerToken;
	TestFalse(TEXT("Six contact monsters fit a two-token budget without a melee queue"), QueueRecord.HasMeleeQueue());
	QueuePool.EngagedCostQuarters = 9;
	TestTrue(TEXT("An engaged cost above the budget forms a melee queue"), QueueRecord.HasMeleeQueue());

	TDTacticsB::CheckCrowdCostScene(*this, TEXT("Contact crowd"), CostContact, 10, 8);
	TDTacticsB::CheckCrowdCostScene(*this, TEXT("Whole-token crowd"), Goblin, 10 * TDMonsterAI::QuartersPerToken, 2);
	TDTacticsB::CheckSettlementScene(*this, TEXT("Cheap first attack"), CostMixed, TEXT("Nip"), ETDTacticsTokenState::Reserved, 7);
	TDTacticsB::CheckSettlementScene(*this, TEXT("Costly first attack"), CostMixed, TEXT("Maul"), ETDTacticsTokenState::None, 6);
	TDTacticsB::CheckRetaliationCostScene(*this, TEXT("Cheap far holder only"), CostContact, Goblin, false, true, TDMonsterAI::QuartersPerToken);
	TDTacticsB::CheckRetaliationCostScene(*this, TEXT("Cheap far holder beside a near holder"), CostContact, Goblin, true, false, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsLoneMonsterFromFarTest, "TDGame.MonsterAI.Tactics.LoneMonsterEngagedFromFarNeverWaits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsLoneMonsterFromFarTest::RunTest(const FString& Parameters)
{
	constexpr float StartDistance = 2000.f;
	constexpr float CloseDistance = 900.f;
	TDTacticsB::FTDTacticsPhaseBWorld Fixture;
	APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(StartDistance, 0.f, 0.f));
	const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
	if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestNotNull(TEXT("Monster exists"), Monster) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	Fixture.TickSteps(1);
	TDTacticsB::HitMonsterFromPlayer(*Monster, *PlayerPawn, 2.f);
	int32 EngagedSteps = 0;
	int32 WaitSlotSteps = 0;
	int32 AvoidingSteps = 0;
	int32 CloseStep = INDEX_NONE;
	for (int32 Step = 1; Step <= 10 * 64 && CloseStep == INDEX_NONE; ++Step)
	{
		Fixture.TickSteps(1);
		const FString Line = TDTacticsB::FindMonsterLine(*ThinkSubsystem, Monster);
		const int32 SlotIndex = FTDCombatTacticsTestAccess::FindSlotForActor(*ThinkSubsystem, Monster);
		EngagedSteps += TDTacticsB::ReadLineField(Line, TEXT("engaged")) == TEXT("1") ? 1 : 0;
		WaitSlotSteps += TDTacticsB::ReadLineField(Line, TEXT("slot")).StartsWith(TEXT("W")) ? 1 : 0;
		AvoidingSteps += SlotIndex != INDEX_NONE && FTDCombatTacticsTestAccess::IsAvoidingTacticsTarget(*ThinkSubsystem, SlotIndex) ? 1 : 0;
		if (FVector::Dist2D(Monster->GetActorLocation(), PlayerPawn->GetActorLocation()) <= CloseDistance)
		{
			CloseStep = Step;
		}
	}
	AddInfo(FString::Printf(TEXT("Engaged for %d steps; came within %.0f cm at step %d"), EngagedSteps, CloseDistance, CloseStep));
	TestTrue(TEXT("The hit engages the lone goblin from afar"), EngagedSteps > 0);
	TestEqual(TEXT("A lone goblin never takes a wait slot while closing in"), WaitSlotSteps, 0);
	TestEqual(TEXT("A lone goblin never steers around its target while closing in"), AvoidingSteps, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsFleeingRevokeTest, "TDGame.MonsterAI.Tactics.FleeingReservationIsRevoked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsFleeingRevokeTest::RunTest(const FString& Parameters)
{
	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!TestNotNull(TEXT("Definition library exists"), Library))
	{
		return false;
	}
	const TSharedPtr<const FTDResolvedMonsterDefinition> Goblin = Library->FindDefinition(TEXT("Goblin_Melee"));
	if (!TestTrue(TEXT("Goblin_Melee resolves"), Goblin.IsValid()))
	{
		return false;
	}
	const uint64 ScenarioStep = 1000;
	auto RunScenario = [&Goblin, ScenarioStep](const ETDMonsterPrimitive ActivePrimitive, ETDTacticsTokenState& OutTokenState, uint64& OutRegrantReadyStep, uint64& OutLastAttackEndStep)
	{
		TDTacticsB::FTDTacticsPhaseBWorld Fixture;
		APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
		UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
		if (!PlayerPawn || !ThinkSubsystem)
		{
			return false;
		}
		FTDCombatTacticsTestAccess::SetStepIndex(*ThinkSubsystem, ScenarioStep);
		const int32 RecordIndex = FTDCombatTacticsTestAccess::AddScenarioRecord(*ThinkSubsystem, PlayerPawn);
		const int32 SlotIndex = FTDCombatTacticsTestAccess::AddScenarioSlot(*ThinkSubsystem, Goblin, FVector(300.f, 0.f, 0.f));
		FTDTargetTacticsRecord& Record = FTDCombatTacticsTestAccess::GetRecord(*ThinkSubsystem, RecordIndex);
		Record.MemberSlots = { SlotIndex };
		FTDMonsterTacticsFragment& Fragment = FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, SlotIndex);
		Fragment.TokenState = ETDTacticsTokenState::Reserved;
		Fragment.TokenRecordId = Record.RecordId;
		Fragment.TargetRecordId = Record.RecordId;
		Fragment.GrantStep = ScenarioStep - 10;
		Fragment.LastAttackEndStep = ScenarioStep - 100;
		Fragment.RegrantReadyStep = 0;
		FTDCombatTacticsTestAccess::GetBrain(*ThinkSubsystem, SlotIndex).ActivePrimitive = ActivePrimitive;
		FTDCombatTacticsTestAccess::RevokeInvalidReservation(*ThinkSubsystem, SlotIndex);
		const FTDMonsterTacticsFragment& Result = FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, SlotIndex);
		OutTokenState = Result.TokenState;
		OutRegrantReadyStep = Result.RegrantReadyStep;
		OutLastAttackEndStep = Result.LastAttackEndStep;
		return true;
	};
	ETDTacticsTokenState FleeingState = ETDTacticsTokenState::Reserved;
	uint64 FleeingRegrantReadyStep = 0;
	uint64 FleeingLastAttackEndStep = 0;
	if (!TestTrue(TEXT("The fleeing scenario runs"), RunScenario(ETDMonsterPrimitive::MoveAway, FleeingState, FleeingRegrantReadyStep, FleeingLastAttackEndStep)))
	{
		return false;
	}
	TestEqual(TEXT("Starting to flee revokes the reservation"), static_cast<int32>(FleeingState), static_cast<int32>(ETDTacticsTokenState::None));
	TestEqual(TEXT("The revoked monster waits the regrant cooldown"), static_cast<int64>(FleeingRegrantReadyStep), static_cast<int64>(ScenarioStep + FTDCombatTactics::RegrantCooldownSteps));
	TestEqual(TEXT("Revocation keeps the waiting age"), static_cast<int64>(FleeingLastAttackEndStep), static_cast<int64>(ScenarioStep - 100));
	ETDTacticsTokenState ApproachState = ETDTacticsTokenState::None;
	uint64 ApproachRegrantReadyStep = 0;
	uint64 ApproachLastAttackEndStep = 0;
	if (!TestTrue(TEXT("The approaching scenario runs"), RunScenario(ETDMonsterPrimitive::MoveToward, ApproachState, ApproachRegrantReadyStep, ApproachLastAttackEndStep)))
	{
		return false;
	}
	TestEqual(TEXT("An approaching monster keeps its reservation"), static_cast<int32>(ApproachState), static_cast<int32>(ETDTacticsTokenState::Reserved));
	TestEqual(TEXT("An approaching monster gets no regrant cooldown"), static_cast<int64>(ApproachRegrantReadyStep), static_cast<int64>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsUnreachableReservationTest, "TDGame.MonsterAI.Tactics.UnreachableReservationIsReleased", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsUnreachableReservationTest::RunTest(const FString& Parameters)
{
	TDTacticsB::FTDTacticsPhaseBWorld Fixture;
	APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* WallBox = NewObject<UBoxComponent>(Wall);
	Wall->AddInstanceComponent(WallBox);
	Wall->SetRootComponent(WallBox);
	WallBox->SetBoxExtent(FVector(TacticsWallHalfLength, TacticsWallHalfThickness, TacticsWallHalfHeight));
	WallBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WallBox->SetCollisionObjectType(ECC_WorldStatic);
	WallBox->SetCollisionResponseToAllChannels(ECR_Block);
	WallBox->RegisterComponent();
	Wall->SetActorLocation(FVector(0.f, TacticsWallCenterY, 0.f));
	const TArray<ATDMonsterCharacter*> BlockedMonsters = {
		Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(-120.f, TacticsBlockedSpawnY, 0.f)),
		Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(120.f, TacticsBlockedSpawnY, 0.f)) };
	const TArray<ATDMonsterCharacter*> OpenMonsters = {
		Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(-300.f, TacticsOpenSpawnY, 0.f)),
		Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(0.f, TacticsOpenSpawnY, 0.f)),
		Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(300.f, TacticsOpenSpawnY, 0.f)) };
	const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
	if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem) || BlockedMonsters.Contains(nullptr) || OpenMonsters.Contains(nullptr))
	{
		return false;
	}
	Fixture.TickSeconds(TacticsUnreachableWarmupSeconds);
	TArray<int32> ReservationRuns;
	ReservationRuns.SetNumZeroed(BlockedMonsters.Num());
	TArray<bool> WasOpenCasting;
	WasOpenCasting.SetNumZeroed(OpenMonsters.Num());
	TArray<int32> ReservedStepsByBlocked;
	ReservedStepsByBlocked.SetNumZeroed(BlockedMonsters.Num());
	int32 LongestBlockedReservationSteps = 0;
	int32 OpenAttackStarts = 0;
	float ClosestBlockedY = TNumericLimits<float>::Max();
	const int32 MeasureSteps = FMath::RoundToInt(TacticsUnreachableMeasureSeconds * 64.f);
	for (int32 Step = 0; Step < MeasureSteps; ++Step)
	{
		Fixture.TickSteps(1);
		for (int32 Index = 0; Index < BlockedMonsters.Num(); ++Index)
		{
			const bool bIsReserved = TDTacticsB::ReadLineField(TDTacticsB::FindMonsterLine(*ThinkSubsystem, BlockedMonsters[Index]), TEXT("token")) == TEXT("R");
			ReservationRuns[Index] = bIsReserved ? ReservationRuns[Index] + 1 : 0;
			LongestBlockedReservationSteps = FMath::Max(LongestBlockedReservationSteps, ReservationRuns[Index]);
			ReservedStepsByBlocked[Index] += bIsReserved ? 1 : 0;
			ClosestBlockedY = FMath::Min(ClosestBlockedY, static_cast<float>(BlockedMonsters[Index]->GetActorLocation().Y));
		}
		for (int32 Index = 0; Index < OpenMonsters.Num(); ++Index)
		{
			const bool bIsCasting = TDTacticsB::ReadLineField(TDTacticsB::FindMonsterLine(*ThinkSubsystem, OpenMonsters[Index]), TEXT("state")) == TEXT("Cast");
			OpenAttackStarts += bIsCasting && !WasOpenCasting[Index] ? 1 : 0;
			WasOpenCasting[Index] = bIsCasting;
		}
	}
	const float BlockedReservationShare = static_cast<float>(FMath::Max(ReservedStepsByBlocked[0], ReservedStepsByBlocked[1])) / static_cast<float>(MeasureSteps);
	AddInfo(FString::Printf(TEXT("Longest blocked reservation %d steps, largest blocked reservation share %.2f, open attack starts %d, closest blocked y %.0f"), LongestBlockedReservationSteps, BlockedReservationShare, OpenAttackStarts, ClosestBlockedY));
	if (!TestTrue(TEXT("The wall keeps the blocked goblins on its far side"), ClosestBlockedY > TacticsWallCenterY))
	{
		return false;
	}
	TestTrue(TEXT("The open goblins keep attacking past the blocked ones"), OpenAttackStarts >= TacticsMinOpenAttackStarts);
	TestTrue(TEXT("A blocked goblin gives up its reservation within two seconds"), LongestBlockedReservationSteps <= TacticsMaxBlockedReservationSteps);
	TestTrue(TEXT("Each blocked goblin holds a reservation at most a fifth of the time"), BlockedReservationShare <= TacticsMaxBlockedReservationShare);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatTacticsCirclingHolderTest, "TDGame.MonsterAI.Tactics.CirclingHolderIsNotStalled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCombatTacticsCirclingHolderTest::RunTest(const FString& Parameters)
{
	for (const int32 HyenaCount : { 1, 3 })
	{
		TDTacticsB::FTDTacticsPhaseBWorld Fixture;
		APawn* PlayerPawn = Fixture.SpawnPlayer(FVector::ZeroVector);
		const TArray<ATDMonsterCharacter*> Hyenas = Fixture.SpawnMonsterRing(TEXT("Hyena"), HyenaCount, TacticsCirclingSpawnRadius);
		UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.GetThinkSubsystem();
		if (!TestNotNull(TEXT("Player exists"), PlayerPawn) || !TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem) || Hyenas.Contains(nullptr))
		{
			return false;
		}
		TArray<bool> WasCasting;
		WasCasting.SetNumZeroed(Hyenas.Num());
		int32 BiteStarts = 0;
		int32 StalledHyenas = 0;
		const int32 MeasureSteps = FMath::RoundToInt(TacticsCirclingSeconds * 64.f);
		for (int32 Step = 0; Step < MeasureSteps; ++Step)
		{
			Fixture.TickSteps(1);
			for (int32 Index = 0; Index < Hyenas.Num(); ++Index)
			{
				const bool bIsCasting = TDTacticsB::ReadLineField(TDTacticsB::FindMonsterLine(*ThinkSubsystem, Hyenas[Index]), TEXT("state")) == TEXT("Cast");
				BiteStarts += bIsCasting && !WasCasting[Index] ? 1 : 0;
				WasCasting[Index] = bIsCasting;
			}
		}
		for (ATDMonsterCharacter* Hyena : Hyenas)
		{
			const int32 SlotIndex = FTDCombatTacticsTestAccess::FindSlotForActor(*ThinkSubsystem, Hyena);
			StalledHyenas += SlotIndex != INDEX_NONE && FTDCombatTacticsTestAccess::GetTactics(*ThinkSubsystem, SlotIndex).StalledUntilStep != 0 ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("%d hyenas: bite starts %d, stalled hyenas %d"), HyenaCount, BiteStarts, StalledHyenas));
		TestTrue(FString::Printf(TEXT("%d hyenas keep biting"), HyenaCount), BiteStarts >= TacticsMinCirclingBitesPerHyena * HyenaCount);
		TestEqual(FString::Printf(TEXT("No circling or biting hyena is marked stalled (%d hyenas)"), HyenaCount), StalledHyenas, 0);
	}
	return true;
}

#endif
