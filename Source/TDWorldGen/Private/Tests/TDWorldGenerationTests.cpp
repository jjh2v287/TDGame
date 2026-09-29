#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "World/TDWorldGeneration.h"
#include "Dungeon/TDDungeonDefinitions.h"

namespace
{
	struct FTDFlatRegionFixture
	{
		UTDRegionDefinition* Region = nullptr;
		TArray<FTDWorldAnchor> Anchors;
		FBox2D BoundsCm = FBox2D(FVector2D(-50000.0, -50000.0), FVector2D(50000.0, 50000.0));
		TArray<UObject*> RootedObjects;

		FTDFlatRegionFixture()
		{
			UTDBiomeDefinition* Biome = CreateRooted<UTDBiomeDefinition>();
			Biome->BiomeId = TEXT("AshenVale");

			Region = CreateRooted<UTDRegionDefinition>();
			Region->RegionId = TEXT("FlatTest");
			Region->Biome = TSoftObjectPtr<UTDBiomeDefinition>(Biome);
			Region->SideDungeonCount = 3;
			AddQuota(TEXT("Camp"), ETDPoiKind::Camp, FIntPoint(2, 3));
			AddQuota(TEXT("Shrine"), ETDPoiKind::Shrine, FIntPoint(1, 2));
			AddQuota(TEXT("Ruin"), ETDPoiKind::Ruin, FIntPoint(2, 3));
			AddQuota(TEXT("Graveyard"), ETDPoiKind::Graveyard, FIntPoint(1, 2));

			AddAnchor(TEXT("Town"), ETDWorldAnchorKind::Town, FVector(-11000.0, 2000.0, 0.0));
			AddAnchor(TEXT("MainDungeon"), ETDWorldAnchorKind::MainDungeon, FVector(29600.0, -26800.0, 0.0));
			AddAnchor(TEXT("PlayerStart"), ETDWorldAnchorKind::PlayerStart, FVector(-9000.0, 4000.0, 0.0));
		}

		~FTDFlatRegionFixture()
		{
			for (UObject* Object : RootedObjects)
			{
				Object->RemoveFromRoot();
			}
		}

		template <typename TObject>
		TObject* CreateRooted()
		{
			TObject* Object = NewObject<TObject>(GetTransientPackage());
			Object->AddToRoot();
			RootedObjects.Add(Object);
			return Object;
		}

		void AddQuota(const TCHAR* PoiId, ETDPoiKind Kind, FIntPoint CountRange)
		{
			UTDPoiArchetype* Archetype = CreateRooted<UTDPoiArchetype>();
			Archetype->PoiId = PoiId;
			Archetype->Kind = Kind;
			FTDPoiQuota& Quota = Region->PoiQuotas.AddDefaulted_GetRef();
			Quota.Archetype = TSoftObjectPtr<UTDPoiArchetype>(Archetype);
			Quota.CountRange = CountRange;
		}

		void AddAnchor(const TCHAR* AnchorId, ETDWorldAnchorKind Kind, const FVector& LocationCm)
		{
			FTDWorldAnchor& Anchor = Anchors.AddDefaulted_GetRef();
			Anchor.AnchorId = AnchorId;
			Anchor.Kind = Kind;
			Anchor.LocationCm = LocationCm;
		}

		bool Generate(int32 Seed, const UTDDungeonAtlasDefinition* Atlas, FTDWorldLayout& OutLayout, FString& OutError) const
		{
			return FTDWorldGenerator::GenerateAndValidate(*Region, Anchors, BoundsCm, Seed, nullptr, Atlas, OutLayout, OutError);
		}

		bool GeneratePlacements(int32 Seed, const FTDTerrainSampler* Terrain, FTDWorldLayout& OutLayout, FString& OutError) const
		{
			return FTDWorldGraphGenerator::Generate(*Region, Anchors, BoundsCm, FTDSeedContext(Seed), Terrain, FTDWorldGraphGenerator::FSettings(), OutLayout, OutError);
		}
	};

	constexpr double TD_TEST_WATER_EDGE_X_CM = -30000.0;
	constexpr double TD_TEST_CLIFF_MIN_X_CM = 5000.0;
	constexpr double TD_TEST_CLIFF_MAX_X_CM = 15000.0;
	constexpr double TD_TEST_HILL_MAX_X_CM = 20000.0;
	constexpr float TD_TEST_CLIFF_SLOPE_DEG = 40.0f;
	constexpr float TD_TEST_HILL_SLOPE_DEG = 22.0f;
	constexpr float TD_TEST_GENTLE_SLOPE_DEG = 3.0f;
	constexpr double TD_TEST_STEEP_BAND_HALF_WIDTH_CM = 3000.0;
	constexpr double TD_TEST_STEEP_BAND_CORE_HALF_WIDTH_CM = 2000.0;
	constexpr double TD_TEST_PASS_MIN_Y_CM = 15200.0;
	constexpr double TD_TEST_PASS_MAX_Y_CM = 19200.0;

	FTDTerrainSampler MakeWaterAndCliffTerrain()
	{
		FTDTerrainSampler Terrain;
		Terrain.HeightCm = [](const FVector2D& LocationCm) { return LocationCm.X < TD_TEST_WATER_EDGE_X_CM ? -1000.0f : 0.0f; };
		Terrain.SlopeDeg = [](const FVector2D& LocationCm)
		{
			if (LocationCm.X >= TD_TEST_CLIFF_MIN_X_CM && LocationCm.X < TD_TEST_CLIFF_MAX_X_CM)
			{
				return TD_TEST_CLIFF_SLOPE_DEG;
			}
			if (LocationCm.X >= TD_TEST_CLIFF_MAX_X_CM && LocationCm.X < TD_TEST_HILL_MAX_X_CM)
			{
				return TD_TEST_HILL_SLOPE_DEG;
			}
			return TD_TEST_GENTLE_SLOPE_DEG;
		};
		return Terrain;
	}

	bool IsInsideGentlePass(const FVector& LocationCm)
	{
		return LocationCm.Y >= TD_TEST_PASS_MIN_Y_CM && LocationCm.Y < TD_TEST_PASS_MAX_Y_CM;
	}

	FTDTerrainSampler MakeSteepBandWithPassTerrain()
	{
		FTDTerrainSampler Terrain;
		Terrain.HeightCm = [](const FVector2D&) { return 0.0f; };
		Terrain.SlopeDeg = [](const FVector2D& LocationCm)
		{
			const bool bIsInsideBand = FMath::Abs(LocationCm.X) < TD_TEST_STEEP_BAND_HALF_WIDTH_CM;
			return bIsInsideBand && !IsInsideGentlePass(FVector(LocationCm, 0.0)) ? 45.0f : 0.0f;
		};
		return Terrain;
	}

	FTDWorldLayout MakeTownToEntranceRoadLayout()
	{
		FTDWorldLayout Layout;
		Layout.Seed = 1;
		Layout.BoundsCm = FBox2D(FVector2D(-30000.0, -30000.0), FVector2D(30000.0, 30000.0));
		FTDWorldAnchor& Town = Layout.Anchors.AddDefaulted_GetRef();
		Town.AnchorId = TEXT("Town");
		Town.Kind = ETDWorldAnchorKind::Town;
		Town.LocationCm = FVector(-20000.0, 0.0, 0.0);
		FTDDungeonEntrancePlacement& Entrance = Layout.Entrances.AddDefaulted_GetRef();
		Entrance.DungeonId = TEXT("Side_0");
		Entrance.LocationCm = FVector(20000.0, 0.0, 0.0);
		return Layout;
	}

	bool IsCountWithinRange(int32 Count, const FIntPoint& Range)
	{
		return Count >= Range.X && Count <= FMath::Max(Range.X, Range.Y);
	}

	int32 CountPlacedPoisOfArchetype(const FTDWorldLayout& Layout, FName ArchetypeId)
	{
		int32 Count = 0;
		for (const FTDPoiPlacement& Poi : Layout.Pois)
		{
			Count += Poi.ArchetypeId == ArchetypeId ? 1 : 0;
		}
		return Count;
	}

	int32 CountPlacedPoisOfKind(const FTDWorldLayout& Layout, ETDPoiKind Kind)
	{
		int32 Count = 0;
		for (const FTDPoiPlacement& Poi : Layout.Pois)
		{
			Count += Poi.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	int32 CountPlacedSideEntrances(const FTDWorldLayout& Layout)
	{
		int32 Count = 0;
		for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
		{
			Count += Entrance.bIsMain ? 0 : 1;
		}
		return Count;
	}

	float FindPlacementSlopeLimitDeg(const UTDRegionDefinition& Region, FName ArchetypeId)
	{
		for (const FTDPoiQuota& Quota : Region.PoiQuotas)
		{
			const UTDPoiArchetype* Archetype = Quota.Archetype.LoadSynchronous();
			if (Archetype != nullptr && Archetype->PoiId == ArchetypeId)
			{
				return FMath::Min(Region.MaxPlacementSlopeDeg, Archetype->MaxSlopeDeg);
			}
		}
		return Region.MaxPlacementSlopeDeg;
	}

	bool IsTerrainAllowed(const FTDTerrainSampler& Terrain, const FVector& LocationCm, float MaxSlopeDeg)
	{
		const FVector2D Location(LocationCm);
		return !Terrain.IsUnderWater(Location) && Terrain.SampleSlope(Location) <= MaxSlopeDeg;
	}

	int32 CountTerrainLimitViolations(const FTDWorldLayout& Layout, const UTDRegionDefinition& Region, const FTDTerrainSampler& Terrain)
	{
		int32 Count = 0;
		for (const FTDPoiPlacement& Poi : Layout.Pois)
		{
			Count += IsTerrainAllowed(Terrain, Poi.LocationCm, FindPlacementSlopeLimitDeg(Region, Poi.ArchetypeId)) ? 0 : 1;
		}
		for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
		{
			Count += Entrance.bIsMain || IsTerrainAllowed(Terrain, Entrance.LocationCm, Region.MaxPlacementSlopeDeg) ? 0 : 1;
		}
		return Count;
	}

	int32 CountAnchorExclusionViolations(const FTDWorldLayout& Layout)
	{
		int32 Count = 0;
		for (const FTDWorldAnchor& Anchor : Layout.Anchors)
		{
			const float MinDistanceCm = Anchor.ExclusionRadiusCm - 1.0f;
			for (const FTDPoiPlacement& Poi : Layout.Pois)
			{
				Count += FVector::Dist2D(Poi.LocationCm, Anchor.LocationCm) < MinDistanceCm ? 1 : 0;
			}
			for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
			{
				Count += !Entrance.bIsMain && FVector::Dist2D(Entrance.LocationCm, Anchor.LocationCm) < MinDistanceCm ? 1 : 0;
			}
		}
		return Count;
	}

	float FindMinPairDistanceCm(const TArray<FVector>& LocationsCm)
	{
		float MinDistanceCm = TNumericLimits<float>::Max();
		for (int32 A = 0; A < LocationsCm.Num(); ++A)
		{
			for (int32 B = A + 1; B < LocationsCm.Num(); ++B)
			{
				MinDistanceCm = FMath::Min(MinDistanceCm, static_cast<float>(FVector::Dist2D(LocationsCm[A], LocationsCm[B])));
			}
		}
		return MinDistanceCm;
	}

	void TestPlacementSpacing(FAutomationTestBase& Test, int32 Seed, const FTDWorldLayout& Layout, const UTDRegionDefinition& Region)
	{
		TArray<FVector> PlacementLocations;
		TArray<FVector> EntranceLocations;
		for (const FTDPoiPlacement& Poi : Layout.Pois)
		{
			PlacementLocations.Add(Poi.LocationCm);
		}
		for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
		{
			PlacementLocations.Add(Entrance.LocationCm);
			EntranceLocations.Add(Entrance.LocationCm);
		}
		const float MinPlacementSpacingCm = FindMinPairDistanceCm(PlacementLocations);
		const float MinEntranceSpacingCm = FindMinPairDistanceCm(EntranceLocations);
		Test.TestEqual(FString::Printf(TEXT("시드 %d 앵커 배제 반경 안 배치 수"), Seed), CountAnchorExclusionViolations(Layout), 0);
		Test.TestTrue(FString::Printf(TEXT("시드 %d Arena·EventArea 포함 배치 최소 간격 %.0f cm ≥ %.0f cm"), Seed, MinPlacementSpacingCm, Region.MinPoiSpacingCm), MinPlacementSpacingCm >= Region.MinPoiSpacingCm - 1.0f);
		Test.TestTrue(FString::Printf(TEXT("시드 %d 입구 최소 간격 %.0f cm ≥ %.0f cm"), Seed, MinEntranceSpacingCm, Region.MinEntranceSpacingCm), MinEntranceSpacingCm >= Region.MinEntranceSpacingCm - 1.0f);
	}

	float FindMaxAbsRoadY(const FTDRoadPolyline& Road)
	{
		float MaxAbsY = 0.0f;
		for (const FVector& Point : Road.PointsCm)
		{
			MaxAbsY = FMath::Max(MaxAbsY, static_cast<float>(FMath::Abs(Point.Y)));
		}
		return MaxAbsY;
	}

	int32 CountErrorsWithCode(const FTDValidationReport& Report, FName Code)
	{
		int32 Count = 0;
		for (const FTDValidationItem& Item : Report.Items)
		{
			Count += Item.Severity == ETDValidationSeverity::Error && Item.Code == Code ? 1 : 0;
		}
		return Count;
	}

	bool IsLocationOnRoadNetworkFromTown(const FTDWorldLayout& Layout, const FVector& LocationCm)
	{
		const FTDWorldAnchor* Town = Layout.FindAnchor(ETDWorldAnchorKind::Town);
		if (Town == nullptr)
		{
			return false;
		}
		TArray<bool> Visited;
		Visited.Init(false, Layout.Roads.Num());
		TArray<FVector> Frontier;
		Frontier.Add(Town->LocationCm);
		while (Frontier.Num() > 0)
		{
			const FVector Current = Frontier.Pop(EAllowShrinking::No);
			if (FVector::Dist2D(Current, LocationCm) < 1.0f)
			{
				return true;
			}
			for (int32 RoadIndex = 0; RoadIndex < Layout.Roads.Num(); ++RoadIndex)
			{
				const FTDRoadPolyline& Road = Layout.Roads[RoadIndex];
				if (Visited[RoadIndex] || Road.PointsCm.Num() == 0)
				{
					continue;
				}
				const bool bTouchesStart = FVector::Dist2D(Road.PointsCm[0], Current) < 1.0f;
				const bool bTouchesEnd = FVector::Dist2D(Road.PointsCm.Last(), Current) < 1.0f;
				if (!bTouchesStart && !bTouchesEnd)
				{
					continue;
				}
				Visited[RoadIndex] = true;
				Frontier.Add(bTouchesStart ? Road.PointsCm.Last() : Road.PointsCm[0]);
			}
		}
		return false;
	}

	UTDDungeonAtlasDefinition* CreateAtlas(float SlotPitchCm, int32 SlotCount)
	{
		UTDDungeonAtlasDefinition* Atlas = NewObject<UTDDungeonAtlasDefinition>(GetTransientPackage());
		Atlas->SlotPitchCm = SlotPitchCm;
		Atlas->LoadingRangeCm = 12800.0f;
		Atlas->Columns = 4;
		for (int32 Index = 0; Index < SlotCount; ++Index)
		{
			FTDDungeonSlot& Slot = Atlas->Slots.AddDefaulted_GetRef();
			Slot.DungeonId = *FString::Printf(TEXT("Slot_%d"), Index);
			Slot.SlotIndex = Index;
		}
		return Atlas;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenFlatRegionPassRateTest, "TDGame.WorldGen.World.FlatRegionPassRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenFlatRegionPassRateTest::RunTest(const FString& Parameters)
{
	FTDFlatRegionFixture Fixture;
	constexpr int32 SeedCount = 50;
	int32 PassedCount = 0;
	TMap<FName, int32> FailureCodes;
	for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		FTDWorldLayout Layout;
		FString Error;
		if (!Fixture.Generate(Seed, nullptr, Layout, Error))
		{
			AddError(FString::Printf(TEXT("시드 %d 생성 실패: %s"), Seed, *Error));
			continue;
		}
		if (Layout.Validation.bPassed)
		{
			++PassedCount;
			continue;
		}
		for (const FTDValidationItem& Item : Layout.Validation.Items)
		{
			if (Item.Severity == ETDValidationSeverity::Error)
			{
				FailureCodes.FindOrAdd(Item.Code) += 1;
				AddInfo(FString::Printf(TEXT("시드 %d 실패 [%s] %s"), Seed, *Item.Code.ToString(), *Item.Message));
			}
		}
	}
	const float PassRate = static_cast<float>(PassedCount) / SeedCount;
	AddInfo(FString::Printf(TEXT("평지 1km² 시드 1~%d 검증 통과율 %.1f%% (%d/%d)"), SeedCount, PassRate * 100.0f, PassedCount, SeedCount));
	for (const TPair<FName, int32>& Failure : FailureCodes)
	{
		AddInfo(FString::Printf(TEXT("실패 코드 %s: %d건"), *Failure.Key.ToString(), Failure.Value));
	}
	TestTrue(TEXT("통과율 90% 이상"), PassRate >= 0.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenDeterminismTest, "TDGame.WorldGen.World.SameSeedProducesSameHash", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenDeterminismTest::RunTest(const FString& Parameters)
{
	FTDFlatRegionFixture Fixture;
	for (int32 Seed = 1; Seed <= 5; ++Seed)
	{
		FTDWorldLayout First;
		FTDWorldLayout Second;
		FString Error;
		TestTrue(TEXT("첫 번째 생성"), Fixture.Generate(Seed, nullptr, First, Error));
		TestTrue(TEXT("두 번째 생성"), Fixture.Generate(Seed, nullptr, Second, Error));
		TestEqual(FString::Printf(TEXT("시드 %d 해시 동일"), Seed), First.ComputeHash(), Second.ComputeHash());
		TestEqual(FString::Printf(TEXT("시드 %d JSON 동일"), Seed), FTDWorldGenerator::ToJson(First), FTDWorldGenerator::ToJson(Second));
	}
	FTDWorldLayout SeedOne;
	FTDWorldLayout SeedTwo;
	FString Error;
	Fixture.Generate(1, nullptr, SeedOne, Error);
	Fixture.Generate(2, nullptr, SeedTwo, Error);
	TestNotEqual(TEXT("다른 시드는 다른 해시"), SeedOne.ComputeHash(), SeedTwo.ComputeHash());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenRoadReachesEntrancesTest, "TDGame.WorldGen.World.RoadsReachAllEntrancesFromTown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenRoadReachesEntrancesTest::RunTest(const FString& Parameters)
{
	FTDFlatRegionFixture Fixture;
	for (int32 Seed = 1; Seed <= 10; ++Seed)
	{
		FTDWorldLayout Layout;
		FString Error;
		TestTrue(FString::Printf(TEXT("시드 %d 생성"), Seed), Fixture.Generate(Seed, nullptr, Layout, Error));
		TestEqual(FString::Printf(TEXT("시드 %d 입구 수"), Seed), Layout.Entrances.Num(), 4);
		TestTrue(FString::Printf(TEXT("시드 %d 도로 존재"), Seed), Layout.Roads.Num() > 0);
		TestEqual(FString::Printf(TEXT("시드 %d road_reach 오류 없음"), Seed), CountErrorsWithCode(Layout.Validation, TEXT("road_reach")), 0);
		for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
		{
			TestTrue(FString::Printf(TEXT("시드 %d 입구 %s 마을 도로망 연결"), Seed, *Entrance.DungeonId.ToString()), IsLocationOnRoadNetworkFromTown(Layout, Entrance.LocationCm));
		}
		for (const FTDRoadPolyline& Road : Layout.Roads)
		{
			for (int32 Index = 1; Index < Road.PointsCm.Num(); ++Index)
			{
				TestTrue(FString::Printf(TEXT("시드 %d 도로 %s 점 간격 ≤ 20m"), Seed, *Road.RoadId.ToString()), FVector::Dist2D(Road.PointsCm[Index - 1], Road.PointsCm[Index]) <= 2001.0f);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenStreamingSafetyTest, "TDGame.WorldGen.World.StreamingSafetyDetectsTightAtlasSlots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenStreamingSafetyTest::RunTest(const FString& Parameters)
{
	FTDFlatRegionFixture Fixture;
	UTDDungeonAtlasDefinition* TightAtlas = CreateAtlas(20000.0f, 3);
	UTDDungeonAtlasDefinition* SafeAtlas = CreateAtlas(30000.0f, 3);
	TightAtlas->AddToRoot();
	SafeAtlas->AddToRoot();

	FTDWorldLayout TightLayout;
	FString Error;
	TestTrue(TEXT("좁은 아틀라스 생성"), Fixture.Generate(7, TightAtlas, TightLayout, Error));
	TestTrue(TEXT("좁은 슬롯 간격은 streaming_safety 오류"), CountErrorsWithCode(TightLayout.Validation, TEXT("streaming_safety")) > 0);
	TestFalse(TEXT("좁은 슬롯 간격은 검증 실패"), TightLayout.Validation.bPassed);

	FTDWorldLayout SafeLayout;
	TestTrue(TEXT("안전한 아틀라스 생성"), Fixture.Generate(7, SafeAtlas, SafeLayout, Error));
	TestEqual(TEXT("안전한 슬롯 간격은 streaming_safety 오류 없음"), CountErrorsWithCode(SafeLayout.Validation, TEXT("streaming_safety")), 0);

	TightAtlas->RemoveFromRoot();
	SafeAtlas->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenLockedAnchorsSurviveTest, "TDGame.WorldGen.LockedAnchorsSurviveRegenerate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenLockedAnchorsSurviveTest::RunTest(const FString& Parameters)
{
	FTDFlatRegionFixture Fixture;
	FTDWorldLayout FirstLayout;
	FString Error;
	TestTrue(TEXT("첫 생성"), Fixture.Generate(3, nullptr, FirstLayout, Error));
	if (FirstLayout.Pois.Num() < 2 || FirstLayout.Roads.Num() == 0)
	{
		AddError(TEXT("첫 생성에 POI 2개와 도로가 필요합니다"));
		return false;
	}

	FTDLockedLayoutElements Locked;
	Locked.Pois.Add(FirstLayout.Pois[0]);
	Locked.Pois.Add(FirstLayout.Pois[1]);
	for (const FTDDungeonEntrancePlacement& Entrance : FirstLayout.Entrances)
	{
		if (!Entrance.bIsMain)
		{
			Locked.Entrances.Add(Entrance);
			break;
		}
	}
	Locked.Roads.Add(FirstLayout.Roads[0]);
	TestEqual(TEXT("잠근 사이드 입구 1개"), Locked.Entrances.Num(), 1);

	FTDWorldLayout Regenerated;
	TestTrue(TEXT("잠금 포함 재생성"), FTDWorldGenerator::GenerateAndValidate(*Fixture.Region, Fixture.Anchors, Locked, Fixture.BoundsCm, 11, nullptr, nullptr, Regenerated, Error));

	for (const FTDPoiPlacement& LockedPoi : Locked.Pois)
	{
		const FTDPoiPlacement* Found = Regenerated.Pois.FindByPredicate([&LockedPoi](const FTDPoiPlacement& Poi) { return Poi.PoiId == LockedPoi.PoiId; });
		if (!TestNotNull(FString::Printf(TEXT("잠긴 POI %s 유지"), *LockedPoi.PoiId.ToString()), Found))
		{
			continue;
		}
		TestTrue(TEXT("잠긴 POI 위치 동일"), Found->LocationCm.Equals(LockedPoi.LocationCm, 1.0f));
		TestEqual(TEXT("잠긴 POI 종류 동일"), Found->Kind, LockedPoi.Kind);
		TestEqual(TEXT("잠긴 POI 원형 동일"), Found->ArchetypeId, LockedPoi.ArchetypeId);
		TestEqual(TEXT("잠긴 POI StableId 동일"), Found->StableId, LockedPoi.StableId);
		TestTrue(TEXT("잠긴 POI bLocked"), Found->bLocked);
		for (const FTDPoiPlacement& Other : Regenerated.Pois)
		{
			if (Other.PoiId == LockedPoi.PoiId)
			{
				continue;
			}
			TestTrue(FString::Printf(TEXT("새 POI %s는 잠긴 POI 간격 밖"), *Other.PoiId.ToString()), FVector::Dist2D(Other.LocationCm, LockedPoi.LocationCm) >= Fixture.Region->MinPoiSpacingCm - 1.0f);
		}
	}

	const FTDDungeonEntrancePlacement& LockedEntrance = Locked.Entrances[0];
	const FTDDungeonEntrancePlacement* FoundEntrance = Regenerated.Entrances.FindByPredicate([&LockedEntrance](const FTDDungeonEntrancePlacement& Entrance) { return Entrance.DungeonId == LockedEntrance.DungeonId; });
	if (TestNotNull(TEXT("잠긴 입구 유지"), FoundEntrance))
	{
		TestTrue(TEXT("잠긴 입구 위치 동일"), FoundEntrance->LocationCm.Equals(LockedEntrance.LocationCm, 1.0f));
		TestTrue(TEXT("잠긴 입구 bLocked"), FoundEntrance->bLocked);
	}
	int32 SideCount = 0;
	TSet<FName> EntranceIds;
	for (const FTDDungeonEntrancePlacement& Entrance : Regenerated.Entrances)
	{
		SideCount += Entrance.bIsMain ? 0 : 1;
		TestFalse(FString::Printf(TEXT("입구 ID %s 중복 없음"), *Entrance.DungeonId.ToString()), EntranceIds.Contains(Entrance.DungeonId));
		EntranceIds.Add(Entrance.DungeonId);
	}
	TestEqual(TEXT("사이드 입구 수는 잠금 포함 지역 정의와 동일"), SideCount, Fixture.Region->SideDungeonCount);

	TSet<FName> PoiIds;
	for (const FTDPoiPlacement& Poi : Regenerated.Pois)
	{
		TestFalse(FString::Printf(TEXT("POI ID %s 중복 없음"), *Poi.PoiId.ToString()), PoiIds.Contains(Poi.PoiId));
		PoiIds.Add(Poi.PoiId);
	}
	for (const FTDPoiQuota& Quota : Fixture.Region->PoiQuotas)
	{
		const UTDPoiArchetype* Archetype = Quota.Archetype.LoadSynchronous();
		int32 Count = 0;
		for (const FTDPoiPlacement& Poi : Regenerated.Pois)
		{
			Count += Archetype && Poi.ArchetypeId == Archetype->PoiId ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("%s 수량 %d는 범위 [%d,%d] 안"), Archetype ? *Archetype->PoiId.ToString() : TEXT("?"), Count, Quota.CountRange.X, Quota.CountRange.Y), Count >= Quota.CountRange.X && Count <= Quota.CountRange.Y);
	}

	const FTDRoadPolyline* FoundRoad = Regenerated.Roads.FindByPredicate([&Locked](const FTDRoadPolyline& Road) { return Road.RoadId == Locked.Roads[0].RoadId; });
	if (TestNotNull(TEXT("잠긴 도로 유지"), FoundRoad))
	{
		TestTrue(TEXT("잠긴 도로 bLocked"), FoundRoad->bLocked);
		TestEqual(TEXT("잠긴 도로 점 수 동일"), FoundRoad->PointsCm.Num(), Locked.Roads[0].PointsCm.Num());
	}

	FTDWorldLayout RegeneratedAgain;
	TestTrue(TEXT("잠금 포함 재생성 2회"), FTDWorldGenerator::GenerateAndValidate(*Fixture.Region, Fixture.Anchors, Locked, Fixture.BoundsCm, 11, nullptr, nullptr, RegeneratedAgain, Error));
	TestEqual(TEXT("잠금 포함 생성도 결정론"), Regenerated.ComputeHash(), RegeneratedAgain.ComputeHash());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenRegionFilterTest, "TDGame.WorldGen.RegionFilterLeavesOtherRegionsUntouched", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenRegionFilterTest::RunTest(const FString& Parameters)
{
	FTDFlatRegionFixture Fixture;
	FTDWorldLayout Layout;
	FString Error;
	TestTrue(TEXT("생성"), Fixture.Generate(5, nullptr, Layout, Error));

	const FBox2D RegionCm(FVector2D(-50000.0, -50000.0), FVector2D(0.0, 50000.0));
	FTDWorldLayout Inside;
	FTDWorldLayout Outside;
	FTDWorldLayoutRegionFilter::Split(Layout, RegionCm, Inside, Outside);

	TestEqual(TEXT("POI 합계 보존"), Inside.Pois.Num() + Outside.Pois.Num(), Layout.Pois.Num());
	TestEqual(TEXT("입구 합계 보존"), Inside.Entrances.Num() + Outside.Entrances.Num(), Layout.Entrances.Num());
	TestEqual(TEXT("도로 합계 보존"), Inside.Roads.Num() + Outside.Roads.Num(), Layout.Roads.Num());
	TestEqual(TEXT("배제 합계 보존"), Inside.Exclusions.Num() + Outside.Exclusions.Num(), Layout.Exclusions.Num());
	TestTrue(TEXT("지역 안 POI 존재"), Inside.Pois.Num() > 0);
	TestTrue(TEXT("지역 밖 POI 존재"), Outside.Pois.Num() > 0);
	for (const FTDPoiPlacement& Poi : Inside.Pois)
	{
		TestTrue(FString::Printf(TEXT("%s는 지역 안"), *Poi.PoiId.ToString()), FTDWorldLayoutRegionFilter::ContainsLocation(RegionCm, Poi.LocationCm));
	}
	for (const FTDPoiPlacement& Poi : Outside.Pois)
	{
		TestFalse(FString::Printf(TEXT("%s는 지역 밖"), *Poi.PoiId.ToString()), FTDWorldLayoutRegionFilter::ContainsLocation(RegionCm, Poi.LocationCm));
	}
	for (const FTDRoadPolyline& Road : Outside.Roads)
	{
		TestFalse(FString::Printf(TEXT("도로 %s는 지역에 닿지 않음"), *Road.RoadId.ToString()), FTDWorldLayoutRegionFilter::TouchesRoad(RegionCm, Road));
	}

	const uint32 OutsideHashBefore = Outside.ComputeHash();
	FTDWorldLayout Regenerated;
	TestTrue(TEXT("다른 시드로 재생성"), Fixture.Generate(6, nullptr, Regenerated, Error));
	FTDWorldLayout RegeneratedInside;
	FTDWorldLayout RegeneratedOutside;
	FTDWorldLayoutRegionFilter::Split(Regenerated, RegionCm, RegeneratedInside, RegeneratedOutside);
	FTDWorldLayout Merged = Outside;
	Merged.Pois.Append(RegeneratedInside.Pois);
	Merged.Entrances.Append(RegeneratedInside.Entrances);
	Merged.Roads.Append(RegeneratedInside.Roads);
	Merged.Exclusions.Append(RegeneratedInside.Exclusions);
	FTDWorldLayout MergedInside;
	FTDWorldLayout MergedOutside;
	FTDWorldLayoutRegionFilter::Split(Merged, RegionCm, MergedInside, MergedOutside);
	MergedInside.Seed = RegeneratedInside.Seed;
	TestEqual(TEXT("부분 재생성 후 지역 밖 요소는 그대로"), MergedOutside.ComputeHash(), OutsideHashBefore);
	TestEqual(TEXT("부분 재생성 후 지역 안 요소는 새 레이아웃"), MergedInside.ComputeHash(), RegeneratedInside.ComputeHash());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenPlacementTerrainTest, "TDGame.WorldGen.World.PlacementAvoidsWaterSteepSlopeAndAnchorExclusion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenPlacementTerrainTest::RunTest(const FString& Parameters)
{
	FTDFlatRegionFixture Fixture;
	Fixture.Anchors[0].ExclusionRadiusCm = 9000.0f;
	Fixture.AddAnchor(TEXT("Landmark_Tower"), ETDWorldAnchorKind::Landmark, FVector(-20000.0, -25000.0, 0.0));
	Fixture.Anchors.Last().ExclusionRadiusCm = 7000.0f;
	const FTDTerrainSampler Terrain = MakeWaterAndCliffTerrain();
	constexpr int32 SeedCount = 50;
	int32 PlacedCount = 0;
	int32 ShortageSeedCount = 0;
	int32 FlatForbiddenCount = 0;
	for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		FTDWorldLayout Layout;
		FString Error;
		if (!Fixture.GeneratePlacements(Seed, &Terrain, Layout, Error))
		{
			AddError(FString::Printf(TEXT("시드 %d 배치 실패: %s"), Seed, *Error));
			continue;
		}
		ShortageSeedCount += Error.IsEmpty() ? 0 : 1;
		PlacedCount += Layout.Pois.Num() + CountPlacedSideEntrances(Layout);
		TestEqual(FString::Printf(TEXT("시드 %d 수면·경사 한도 초과 위치 배치 수"), Seed), CountTerrainLimitViolations(Layout, *Fixture.Region, Terrain), 0);
		TestPlacementSpacing(*this, Seed, Layout, *Fixture.Region);

		FTDWorldLayout FlatLayout;
		if (Fixture.GeneratePlacements(Seed, nullptr, FlatLayout, Error))
		{
			FlatForbiddenCount += CountTerrainLimitViolations(FlatLayout, *Fixture.Region, Terrain);
		}
	}
	AddInfo(FString::Printf(TEXT("합성 지형 시드 1~%d: 배치 %d개, 수량 부족 시드 %d개, 지형 없는 대조군의 금지 위치 배치 %d개"), SeedCount, PlacedCount, ShortageSeedCount, FlatForbiddenCount));
	TestTrue(TEXT("합성 지형에서도 배치가 생성됨"), PlacedCount > 0);
	TestTrue(TEXT("지형 없는 대조군은 같은 수면·급경사 위치에 배치함(지형 조건이 실제로 걸러냄)"), FlatForbiddenCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenPlacementCountsTest, "TDGame.WorldGen.World.PlacementCountsWithinRegionRanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenPlacementCountsTest::RunTest(const FString& Parameters)
{
	FTDFlatRegionFixture Fixture;
	const UTDRegionDefinition& Region = *Fixture.Region;
	constexpr int32 SeedCount = 50;
	int32 ShortageSeedCount = 0;
	for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		FTDWorldLayout Layout;
		FString Error;
		if (!Fixture.GeneratePlacements(Seed, nullptr, Layout, Error))
		{
			AddError(FString::Printf(TEXT("시드 %d 배치 실패: %s"), Seed, *Error));
			continue;
		}
		ShortageSeedCount += Error.IsEmpty() ? 0 : 1;
		for (const FTDPoiQuota& Quota : Region.PoiQuotas)
		{
			const UTDPoiArchetype* Archetype = Quota.Archetype.LoadSynchronous();
			if (!TestNotNull(TEXT("할당량 원형 로드"), Archetype))
			{
				continue;
			}
			const int32 Count = CountPlacedPoisOfArchetype(Layout, Archetype->PoiId);
			TestTrue(FString::Printf(TEXT("시드 %d %s 수량 %d는 [%d,%d] 안"), Seed, *Archetype->PoiId.ToString(), Count, Quota.CountRange.X, Quota.CountRange.Y), IsCountWithinRange(Count, Quota.CountRange));
		}
		const int32 ArenaCount = CountPlacedPoisOfKind(Layout, ETDPoiKind::Arena);
		const int32 EventAreaCount = CountPlacedPoisOfKind(Layout, ETDPoiKind::EventArea);
		TestTrue(FString::Printf(TEXT("시드 %d Arena 수량 %d는 [%d,%d] 안"), Seed, ArenaCount, Region.ArenaRange.X, Region.ArenaRange.Y), IsCountWithinRange(ArenaCount, Region.ArenaRange));
		TestTrue(FString::Printf(TEXT("시드 %d EventArea 수량 %d는 [%d,%d] 안"), Seed, EventAreaCount, Region.EventAreaRange.X, Region.EventAreaRange.Y), IsCountWithinRange(EventAreaCount, Region.EventAreaRange));
		TestEqual(FString::Printf(TEXT("시드 %d 사이드 던전 입구 수"), Seed), CountPlacedSideEntrances(Layout), Region.SideDungeonCount);
		TestPlacementSpacing(*this, Seed, Layout, Region);
	}
	AddInfo(FString::Printf(TEXT("평지 시드 1~%d: 수량 부족 경고 시드 %d개"), SeedCount, ShortageSeedCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenRoadsReachLandmarkTest, "TDGame.WorldGen.World.RoadsReachLandmarkAnchors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenRoadsReachLandmarkTest::RunTest(const FString& Parameters)
{
	FTDFlatRegionFixture Fixture;
	Fixture.AddAnchor(TEXT("Landmark_Tower"), ETDWorldAnchorKind::Landmark, FVector(20000.0, 30000.0, 0.0));
	const FTDWorldAnchor LandmarkAnchor = Fixture.Anchors.Last();
	FTDWorldLayout LastLayout;
	for (int32 Seed = 1; Seed <= 5; ++Seed)
	{
		FTDWorldLayout Layout;
		FString Error;
		if (!Fixture.Generate(Seed, nullptr, Layout, Error))
		{
			AddError(FString::Printf(TEXT("시드 %d 생성 실패: %s"), Seed, *Error));
			continue;
		}
		TestEqual(FString::Printf(TEXT("시드 %d road_reach 오류 없음"), Seed), CountErrorsWithCode(Layout.Validation, TEXT("road_reach")), 0);
		TestTrue(FString::Printf(TEXT("시드 %d Landmark 마을 도로망 연결"), Seed), IsLocationOnRoadNetworkFromTown(Layout, LandmarkAnchor.LocationCm));
		int32 PrimaryCount = 0;
		int32 SecondaryCount = 0;
		for (const FTDRoadPolyline& Road : Layout.Roads)
		{
			PrimaryCount += Road.bIsPrimary ? 1 : 0;
			SecondaryCount += Road.bIsPrimary ? 0 : 1;
		}
		const int32 NodeCount = 2 + Layout.Entrances.Num() + Layout.Pois.Num();
		TestEqual(FString::Printf(TEXT("시드 %d 주 도로 수 = 노드(마을·Landmark·입구·POI) 수 - 1"), Seed), PrimaryCount, NodeCount - 1);
		TestEqual(FString::Printf(TEXT("시드 %d 보조 도로 수 = round(비율 × 주 도로 수)"), Seed), SecondaryCount, FMath::RoundToInt(Fixture.Region->SecondaryRoadRatio * PrimaryCount));
		LastLayout = MoveTemp(Layout);
	}

	FTDWorldAnchor& UnlinkedLandmark = LastLayout.Anchors.AddDefaulted_GetRef();
	UnlinkedLandmark.AnchorId = TEXT("Landmark_Unlinked");
	UnlinkedLandmark.Kind = ETDWorldAnchorKind::Landmark;
	UnlinkedLandmark.LocationCm = FVector(49000.0, 49000.0, 0.0);
	const FTDValidationReport Report = FTDWorldValidator::Validate(LastLayout, nullptr, nullptr, FTDWorldValidator::FSettings());
	const bool bFlagsUnlinked = Report.Items.ContainsByPredicate([](const FTDValidationItem& Item) { return Item.Severity == ETDValidationSeverity::Error && Item.Code == FName(TEXT("road_reach")) && Item.RelatedId == FName(TEXT("Landmark_Unlinked")); });
	const bool bFlagsLinked = Report.Items.ContainsByPredicate([&LandmarkAnchor](const FTDValidationItem& Item) { return Item.Severity == ETDValidationSeverity::Error && Item.Code == FName(TEXT("road_reach")) && Item.RelatedId == LandmarkAnchor.AnchorId; });
	TestTrue(TEXT("도로망에서 먼 Landmark는 road_reach 오류"), bFlagsUnlinked);
	TestFalse(TEXT("도로로 연결된 Landmark는 road_reach 오류 아님"), bFlagsLinked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDWorldGenRoadsFollowGentlePassTest, "TDGame.WorldGen.World.RoadsFollowGentlePassThroughSteepBand", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDWorldGenRoadsFollowGentlePassTest::RunTest(const FString& Parameters)
{
	const FTDTerrainSampler Terrain = MakeSteepBandWithPassTerrain();
	const FTDRoadGenerator::FSettings Settings;
	FTDWorldLayout FlatLayout = MakeTownToEntranceRoadLayout();
	FTDWorldLayout TerrainLayout = MakeTownToEntranceRoadLayout();
	FString Error;
	TestTrue(TEXT("평지 도로 생성"), FTDRoadGenerator::Generate(FlatLayout, nullptr, Settings, FTDSeedContext(1), Error));
	TestTrue(TEXT("급경사 띠 지형 도로 생성"), FTDRoadGenerator::Generate(TerrainLayout, &Terrain, Settings, FTDSeedContext(1), Error));
	if (FlatLayout.Roads.Num() != 1 || TerrainLayout.Roads.Num() != 1)
	{
		AddError(FString::Printf(TEXT("노드 2개에는 도로 1개가 필요합니다 (평지 %d, 지형 %d)"), FlatLayout.Roads.Num(), TerrainLayout.Roads.Num()));
		return false;
	}

	int32 BandCorePointCount = 0;
	int32 BandCorePointOutsidePassCount = 0;
	for (const FVector& Point : TerrainLayout.Roads[0].PointsCm)
	{
		if (FMath::Abs(Point.X) > TD_TEST_STEEP_BAND_CORE_HALF_WIDTH_CM)
		{
			continue;
		}
		++BandCorePointCount;
		BandCorePointOutsidePassCount += IsInsideGentlePass(Point) ? 0 : 1;
	}
	const float FlatMaxAbsY = FindMaxAbsRoadY(FlatLayout.Roads[0]);
	const float TerrainMaxAbsY = FindMaxAbsRoadY(TerrainLayout.Roads[0]);
	AddInfo(FString::Printf(TEXT("평지 도로 최대 |Y| %.0f cm, 지형 도로 최대 |Y| %.0f cm, 띠 안쪽 점 %d개 중 통로 밖 %d개"), FlatMaxAbsY, TerrainMaxAbsY, BandCorePointCount, BandCorePointOutsidePassCount));
	TestTrue(TEXT("지형 도로가 급경사 띠를 건넘"), BandCorePointCount > 0);
	TestEqual(TEXT("급경사 띠 안쪽 도로 점은 모두 완만한 통로 안"), BandCorePointOutsidePassCount, 0);
	TestTrue(TEXT("평지 도로는 통로로 우회하지 않음"), FlatMaxAbsY < TD_TEST_PASS_MIN_Y_CM);
	TestTrue(TEXT("지형 도로는 평지 도로와 달리 통로까지 우회"), TerrainMaxAbsY >= TD_TEST_PASS_MIN_Y_CM);
	return true;
}

#endif
