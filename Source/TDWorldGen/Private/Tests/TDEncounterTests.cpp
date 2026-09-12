#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Dungeon/TDEncounterDefinitions.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

namespace
{
	struct FTDEncounterFixture
	{
		UTDEncounterSet* EncounterSet = nullptr;

		FTDEncounterFixture()
		{
			EncounterSet = NewObject<UTDEncounterSet>(GetTransientPackage());
			EncounterSet->AddToRoot();
			EncounterSet->DifficultyBudgetPerRoom = 6.0f;
			EncounterSet->MaxPerRoom = 4;
			AddEntry(AActor::StaticClass(), 0, 2, 3.0f, 1.0f);
			AddEntry(ATargetPoint::StaticClass(), 1, 5, 1.0f, 2.5f);
			AddEntry(AActor::StaticClass(), 4, 9, 2.0f, 4.0f);
		}

		~FTDEncounterFixture()
		{
			EncounterSet->RemoveFromRoot();
		}

		void AddEntry(UClass* EnemyClass, int32 MinDepth, int32 MaxDepth, float Weight, float DifficultyCost)
		{
			FTDEncounterEntry& Entry = EncounterSet->Entries.AddDefaulted_GetRef();
			Entry.EnemyClass = TSoftClassPtr<AActor>(EnemyClass);
			Entry.MinDepth = MinDepth;
			Entry.MaxDepth = MaxDepth;
			Entry.Weight = Weight;
			Entry.DifficultyCost = DifficultyCost;
		}
	};

	FString PicksToString(const TArray<FTDEncounterPick>& Picks)
	{
		FString Out;
		for (const FTDEncounterPick& Pick : Picks)
		{
			Out += FString::Printf(TEXT("%d:%d;"), Pick.MarkerIndex, Pick.EntryIndex);
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDEncounterDeterminismTest, "TDGame.WorldGen.EncounterResolveIsDeterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDEncounterDeterminismTest::RunTest(const FString& Parameters)
{
	FTDEncounterFixture Fixture;
	TSet<FString> DistinctResults;
	for (int32 Seed = 1; Seed <= 50; ++Seed)
	{
		const FTDSeedContext Context(Seed);
		const TArray<FTDEncounterPick> First = FTDEncounterResolver::Resolve(Fixture.EncounterSet, 2, 4, Context);
		const TArray<FTDEncounterPick> Second = FTDEncounterResolver::Resolve(Fixture.EncounterSet, 2, 4, Context);
		TestEqual(FString::Printf(TEXT("시드 %d 같은 입력은 같은 선택"), Seed), PicksToString(First), PicksToString(Second));
		TestTrue(FString::Printf(TEXT("시드 %d 최소 1개 선택"), Seed), First.Num() > 0);
		DistinctResults.Add(PicksToString(First));
	}
	TestTrue(TEXT("시드에 따라 선택이 달라짐"), DistinctResults.Num() > 1);

	const TArray<FTDEncounterPick> DepthTwo = FTDEncounterResolver::Resolve(Fixture.EncounterSet, 2, 4, FTDSeedContext(7));
	const TArray<FTDEncounterPick> DepthTwoAgain = FTDEncounterResolver::Resolve(Fixture.EncounterSet, 2, 4, FTDSeedContext(7));
	TestEqual(TEXT("같은 깊이·시드는 재현"), PicksToString(DepthTwo), PicksToString(DepthTwoAgain));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDEncounterDepthBudgetTest, "TDGame.WorldGen.EncounterRespectsDepthAndBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDEncounterDepthBudgetTest::RunTest(const FString& Parameters)
{
	FTDEncounterFixture Fixture;
	for (int32 Seed = 1; Seed <= 30; ++Seed)
	{
		for (int32 Depth = 0; Depth <= 9; ++Depth)
		{
			const TArray<FTDEncounterPick> Picks = FTDEncounterResolver::Resolve(Fixture.EncounterSet, Depth, 6, FTDSeedContext(Seed));
			TestTrue(FString::Printf(TEXT("시드 %d 깊이 %d 방당 최대 수"), Seed, Depth), Picks.Num() <= Fixture.EncounterSet->MaxPerRoom);
			TestTrue(FString::Printf(TEXT("시드 %d 깊이 %d 난이도 예산"), Seed, Depth), FTDEncounterResolver::SumDifficulty(Picks) <= Fixture.EncounterSet->DifficultyBudgetPerRoom + KINDA_SMALL_NUMBER);
			for (const FTDEncounterPick& Pick : Picks)
			{
				const FTDEncounterEntry& Entry = Fixture.EncounterSet->Entries[Pick.EntryIndex];
				TestTrue(FString::Printf(TEXT("시드 %d 깊이 %d 항목 %d 깊이 범위"), Seed, Depth, Pick.EntryIndex), Entry.MatchesDepth(Depth));
				TestEqual(TEXT("선택 클래스는 항목 클래스"), Pick.EnemyClass.ToString(), Entry.EnemyClass.ToString());
			}
			for (int32 Index = 1; Index < Picks.Num(); ++Index)
			{
				TestEqual(TEXT("마커 인덱스는 순서대로"), Picks[Index].MarkerIndex, Picks[Index - 1].MarkerIndex + 1);
			}
		}
	}

	const TArray<FTDEncounterPick> DepthZero = FTDEncounterResolver::Resolve(Fixture.EncounterSet, 0, 4, FTDSeedContext(3));
	TestEqual(TEXT("깊이 0은 항목 0만 가능하므로 예산 6/1 → 최대 4개"), DepthZero.Num(), 4);
	const TArray<FTDEncounterPick> DepthSix = FTDEncounterResolver::Resolve(Fixture.EncounterSet, 6, 4, FTDSeedContext(3));
	TestEqual(TEXT("깊이 6은 항목 2(비용 4)만 가능하므로 1개 후 예산 초과로 중단"), DepthSix.Num(), 1);
	const TArray<FTDEncounterPick> NoMarkers = FTDEncounterResolver::Resolve(Fixture.EncounterSet, 2, 0, FTDSeedContext(3));
	TestEqual(TEXT("마커 0개면 선택 없음"), NoMarkers.Num(), 0);
	const TArray<FTDEncounterPick> NullSet = FTDEncounterResolver::Resolve(nullptr, 2, 4, FTDSeedContext(3));
	TestEqual(TEXT("세트 null이면 선택 없음"), NullSet.Num(), 0);
	const TArray<FTDEncounterPick> TooDeep = FTDEncounterResolver::Resolve(Fixture.EncounterSet, 20, 4, FTDSeedContext(3));
	TestEqual(TEXT("깊이 범위 밖이면 선택 없음"), TooDeep.Num(), 0);
	return true;
}

#endif
