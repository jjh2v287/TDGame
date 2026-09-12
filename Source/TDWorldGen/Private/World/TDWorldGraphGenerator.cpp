#include "World/TDWorldGeneration.h"

namespace
{
	struct FTDPlacementCandidate
	{
		FVector2D LocationCm = FVector2D::ZeroVector;
		float SlopeDeg = 0.0f;
		bool bIsUnderWater = false;
		bool bIsUsed = false;
	};

	struct FTDPlacementRequest
	{
		FName ArchetypeId;
		ETDPoiKind Kind = ETDPoiKind::Custom;
		int32 Count = 0;
		float ExclusionRadiusCm = 2000.0f;
		float MaxSlopeDeg = 25.0f;
	};

	FGuid MakeStableGuid(int32 Seed, const TCHAR* Category, int32 Index)
	{
		return FTDSeedContext(Seed).DeriveGuid(Category, Index);
	}

	FName MakeUnusedPlacementId(const TCHAR* Prefix, int32 StartIndex, const TFunctionRef<bool(FName)>& IsUsed)
	{
		for (int32 Index = StartIndex; ; ++Index)
		{
			const FName Candidate(*FString::Printf(TEXT("%s_%d"), Prefix, Index));
			if (!IsUsed(Candidate))
			{
				return Candidate;
			}
		}
	}

	bool HasEntranceId(const FTDWorldLayout& Layout, FName DungeonId)
	{
		return Layout.Entrances.ContainsByPredicate([DungeonId](const FTDDungeonEntrancePlacement& Entrance) { return Entrance.DungeonId == DungeonId; });
	}

	bool HasPoiId(const FTDWorldLayout& Layout, FName PoiId)
	{
		return Layout.Pois.ContainsByPredicate([PoiId](const FTDPoiPlacement& Poi) { return Poi.PoiId == PoiId; });
	}

	int32 CountSideEntrances(const FTDWorldLayout& Layout)
	{
		int32 Count = 0;
		for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
		{
			Count += Entrance.bIsMain ? 0 : 1;
		}
		return Count;
	}

	int32 CountPoisOfArchetype(const FTDWorldLayout& Layout, FName ArchetypeId)
	{
		int32 Count = 0;
		for (const FTDPoiPlacement& Poi : Layout.Pois)
		{
			Count += Poi.ArchetypeId == ArchetypeId ? 1 : 0;
		}
		return Count;
	}

	FString PoiKindToString(ETDPoiKind Kind)
	{
		return StaticEnum<ETDPoiKind>()->GetNameStringByValue(static_cast<int64>(Kind));
	}

	void SamplePoissonDisc(const FBox2D& AreaCm, float RadiusCm, int32 AttemptsPerPoint, FRandomStream& Stream, TArray<FTDPlacementCandidate>& OutCandidates)
	{
		const FVector2D AreaSize = AreaCm.GetSize();
		const float CellSize = RadiusCm / UE_SQRT_2;
		const int32 GridWidth = FMath::Max(1, FMath::CeilToInt(AreaSize.X / CellSize));
		const int32 GridHeight = FMath::Max(1, FMath::CeilToInt(AreaSize.Y / CellSize));
		TArray<int32> Grid;
		Grid.Init(INDEX_NONE, GridWidth * GridHeight);
		TArray<FVector2D> Points;
		TArray<int32> Active;

		auto CellOf = [&](const FVector2D& Point, int32& OutX, int32& OutY)
		{
			OutX = FMath::Clamp(FMath::FloorToInt((Point.X - AreaCm.Min.X) / CellSize), 0, GridWidth - 1);
			OutY = FMath::Clamp(FMath::FloorToInt((Point.Y - AreaCm.Min.Y) / CellSize), 0, GridHeight - 1);
		};
		auto IsFarFromOthers = [&](const FVector2D& Point)
		{
			int32 CellX = 0;
			int32 CellY = 0;
			CellOf(Point, CellX, CellY);
			for (int32 Y = FMath::Max(0, CellY - 2); Y <= FMath::Min(GridHeight - 1, CellY + 2); ++Y)
			{
				for (int32 X = FMath::Max(0, CellX - 2); X <= FMath::Min(GridWidth - 1, CellX + 2); ++X)
				{
					const int32 Existing = Grid[Y * GridWidth + X];
					if (Existing != INDEX_NONE && FVector2D::DistSquared(Points[Existing], Point) < RadiusCm * RadiusCm)
					{
						return false;
					}
				}
			}
			return true;
		};
		auto AddPoint = [&](const FVector2D& Point)
		{
			int32 CellX = 0;
			int32 CellY = 0;
			CellOf(Point, CellX, CellY);
			const int32 NewIndex = Points.Add(Point);
			Grid[CellY * GridWidth + CellX] = NewIndex;
			Active.Add(NewIndex);
		};

		AddPoint(FVector2D(Stream.FRandRange(AreaCm.Min.X, AreaCm.Max.X), Stream.FRandRange(AreaCm.Min.Y, AreaCm.Max.Y)));
		while (Active.Num() > 0)
		{
			const int32 ActiveSlot = Stream.RandRange(0, Active.Num() - 1);
			const FVector2D Center = Points[Active[ActiveSlot]];
			bool bFoundCandidate = false;
			for (int32 Attempt = 0; Attempt < AttemptsPerPoint; ++Attempt)
			{
				const float Angle = Stream.FRandRange(0.0f, 2.0f * UE_PI);
				const float Distance = Stream.FRandRange(RadiusCm, 2.0f * RadiusCm);
				const FVector2D Candidate = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
				if (!AreaCm.IsInside(Candidate) || !IsFarFromOthers(Candidate))
				{
					continue;
				}
				AddPoint(Candidate);
				bFoundCandidate = true;
				break;
			}
			if (!bFoundCandidate)
			{
				Active.RemoveAtSwap(ActiveSlot);
			}
		}

		OutCandidates.Reset(Points.Num());
		for (const FVector2D& Point : Points)
		{
			FTDPlacementCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
			Candidate.LocationCm = Point;
		}
	}

	void ShuffleCandidates(TArray<FTDPlacementCandidate>& Candidates, FRandomStream& Stream)
	{
		for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
		{
			const int32 SwapIndex = Stream.RandRange(0, Index);
			Candidates.Swap(Index, SwapIndex);
		}
	}

	void SampleCandidateTerrain(TArray<FTDPlacementCandidate>& Candidates, const FTDTerrainSampler* Terrain)
	{
		if (Terrain == nullptr || !Terrain->IsValid())
		{
			return;
		}
		for (FTDPlacementCandidate& Candidate : Candidates)
		{
			Candidate.SlopeDeg = Terrain->SampleSlope(Candidate.LocationCm);
			Candidate.bIsUnderWater = Terrain->IsUnderWater(Candidate.LocationCm);
		}
	}

	float SampleHeight(const FTDTerrainSampler* Terrain, const FVector2D& LocationCm)
	{
		return Terrain != nullptr && Terrain->IsValid() ? Terrain->SampleHeight(LocationCm) : 0.0f;
	}

	bool IsCandidateClear(const FTDPlacementCandidate& Candidate, const FTDWorldLayout& Layout, float MaxSlopeDeg)
	{
		if (Candidate.bIsUsed || Candidate.bIsUnderWater || Candidate.SlopeDeg > MaxSlopeDeg)
		{
			return false;
		}
		for (const FTDExclusionArea& Exclusion : Layout.Exclusions)
		{
			if (FVector2D::DistSquared(FVector2D(Exclusion.CenterCm), Candidate.LocationCm) < FMath::Square(Exclusion.RadiusCm))
			{
				return false;
			}
		}
		return true;
	}

	int32 PlaceSideEntrances(const UTDRegionDefinition& Region, int32 TargetCount, TArray<FTDPlacementCandidate>& Candidates, const FTDTerrainSampler* Terrain, FRandomStream& Stream, FTDWorldLayout& Layout)
	{
		int32 Placed = 0;
		const float SpacingSquared = FMath::Square(Region.MinEntranceSpacingCm);
		for (FTDPlacementCandidate& Candidate : Candidates)
		{
			if (Placed >= TargetCount)
			{
				break;
			}
			if (!IsCandidateClear(Candidate, Layout, Region.MaxPlacementSlopeDeg))
			{
				continue;
			}
			bool bIsSpaced = true;
			for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
			{
				bIsSpaced &= FVector2D::DistSquared(FVector2D(Entrance.LocationCm), Candidate.LocationCm) >= SpacingSquared;
			}
			for (const FTDWorldAnchor& Anchor : Layout.Anchors)
			{
				bIsSpaced &= Anchor.Kind != ETDWorldAnchorKind::MainDungeon || FVector2D::DistSquared(FVector2D(Anchor.LocationCm), Candidate.LocationCm) >= SpacingSquared;
			}
			if (!bIsSpaced)
			{
				continue;
			}
			Candidate.bIsUsed = true;
			FTDDungeonEntrancePlacement& Entrance = Layout.Entrances.AddDefaulted_GetRef();
			Entrance.DungeonId = MakeUnusedPlacementId(TEXT("Side"), Placed, [&Layout](FName Id) { return HasEntranceId(Layout, Id); });
			Entrance.LocationCm = FVector(Candidate.LocationCm, SampleHeight(Terrain, Candidate.LocationCm));
			Entrance.Yaw = Stream.FRandRange(0.0f, 360.0f);
			Entrance.bIsMain = false;
			Entrance.bLocked = false;
			Entrance.StableId = MakeStableGuid(Layout.Seed, TEXT("SideEntrance"), Placed);
			++Placed;
		}
		return Placed;
	}

	int32 PlacePois(const FTDPlacementRequest& Request, TArray<FTDPlacementCandidate>& Candidates, const FTDTerrainSampler* Terrain, FRandomStream& Stream, FTDWorldLayout& Layout)
	{
		int32 Placed = 0;
		for (FTDPlacementCandidate& Candidate : Candidates)
		{
			if (Placed >= Request.Count)
			{
				break;
			}
			if (!IsCandidateClear(Candidate, Layout, Request.MaxSlopeDeg))
			{
				continue;
			}
			Candidate.bIsUsed = true;
			FTDPoiPlacement& Poi = Layout.Pois.AddDefaulted_GetRef();
			Poi.PoiId = MakeUnusedPlacementId(*Request.ArchetypeId.ToString(), Placed, [&Layout](FName Id) { return HasPoiId(Layout, Id); });
			Poi.ArchetypeId = Request.ArchetypeId;
			Poi.Kind = Request.Kind;
			Poi.LocationCm = FVector(Candidate.LocationCm, SampleHeight(Terrain, Candidate.LocationCm));
			Poi.Yaw = Stream.FRandRange(0.0f, 360.0f);
			Poi.ExclusionRadiusCm = Request.ExclusionRadiusCm;
			Poi.bLocked = false;
			Poi.StableId = MakeStableGuid(Layout.Seed, *Request.ArchetypeId.ToString(), Placed);

			FTDExclusionArea& Exclusion = Layout.Exclusions.AddDefaulted_GetRef();
			Exclusion.CenterCm = Poi.LocationCm;
			Exclusion.RadiusCm = Request.ExclusionRadiusCm;
			Exclusion.Reason = TEXT("Poi");
			++Placed;
		}
		return Placed;
	}

	void BuildPlacementRequests(const UTDRegionDefinition& Region, const FTDWorldGraphGenerator::FSettings& Settings, FRandomStream& CountStream, TArray<FTDPlacementRequest>& OutRequests)
	{
		for (const FTDPoiQuota& Quota : Region.PoiQuotas)
		{
			FTDPlacementRequest& Request = OutRequests.AddDefaulted_GetRef();
			Request.Count = CountStream.RandRange(Quota.CountRange.X, FMath::Max(Quota.CountRange.X, Quota.CountRange.Y));
			Request.MaxSlopeDeg = Region.MaxPlacementSlopeDeg;
			const UTDPoiArchetype* Archetype = Quota.Archetype.LoadSynchronous();
			if (Archetype == nullptr)
			{
				Request.ArchetypeId = *Quota.Archetype.GetAssetName();
				Request.Kind = ETDPoiKind::Custom;
				continue;
			}
			Request.ArchetypeId = Archetype->PoiId;
			Request.Kind = Archetype->Kind;
			Request.ExclusionRadiusCm = Archetype->ExclusionRadiusCm;
			Request.MaxSlopeDeg = FMath::Min(Region.MaxPlacementSlopeDeg, Archetype->MaxSlopeDeg);
		}

		FTDPlacementRequest& Arena = OutRequests.AddDefaulted_GetRef();
		Arena.ArchetypeId = TEXT("Arena");
		Arena.Kind = ETDPoiKind::Arena;
		Arena.Count = CountStream.RandRange(Region.ArenaRange.X, FMath::Max(Region.ArenaRange.X, Region.ArenaRange.Y));
		Arena.ExclusionRadiusCm = Settings.DefaultArenaRadiusCm;
		Arena.MaxSlopeDeg = Region.MaxPlacementSlopeDeg;

		FTDPlacementRequest& EventArea = OutRequests.AddDefaulted_GetRef();
		EventArea.ArchetypeId = TEXT("EventArea");
		EventArea.Kind = ETDPoiKind::EventArea;
		EventArea.Count = CountStream.RandRange(Region.EventAreaRange.X, FMath::Max(Region.EventAreaRange.X, Region.EventAreaRange.Y));
		EventArea.MaxSlopeDeg = Region.MaxPlacementSlopeDeg;
	}

	void SeedLayoutWithLockedElements(const UTDRegionDefinition& Region, const FTDLockedLayoutElements& Locked, FTDWorldLayout& Layout)
	{
		for (const FTDDungeonEntrancePlacement& Source : Locked.Entrances)
		{
			if (HasEntranceId(Layout, Source.DungeonId))
			{
				continue;
			}
			FTDDungeonEntrancePlacement& Entrance = Layout.Entrances.Add_GetRef(Source);
			Entrance.bLocked = true;

			FTDExclusionArea& Exclusion = Layout.Exclusions.AddDefaulted_GetRef();
			Exclusion.CenterCm = Entrance.LocationCm;
			Exclusion.RadiusCm = Region.MinPoiSpacingCm;
			Exclusion.Reason = TEXT("LockedEntrance");
		}
		for (const FTDPoiPlacement& Source : Locked.Pois)
		{
			if (HasPoiId(Layout, Source.PoiId))
			{
				continue;
			}
			FTDPoiPlacement& Poi = Layout.Pois.Add_GetRef(Source);
			Poi.bLocked = true;

			FTDExclusionArea& Exclusion = Layout.Exclusions.AddDefaulted_GetRef();
			Exclusion.CenterCm = Poi.LocationCm;
			Exclusion.RadiusCm = FMath::Max(Poi.ExclusionRadiusCm, Region.MinPoiSpacingCm);
			Exclusion.Reason = TEXT("Poi");
		}
	}

	void SubtractLockedCounts(const FTDWorldLayout& Layout, TArray<FTDPlacementRequest>& Requests)
	{
		for (FTDPlacementRequest& Request : Requests)
		{
			Request.Count = FMath::Max(0, Request.Count - CountPoisOfArchetype(Layout, Request.ArchetypeId));
		}
	}

	void SeedLayoutWithAnchors(const UTDRegionDefinition& Region, const TArray<FTDWorldAnchor>& Anchors, const FBox2D& BoundsCm, const FTDSeedContext& Seed, FTDWorldLayout& Layout)
	{
		Layout = FTDWorldLayout();
		Layout.Seed = Seed.MasterSeed;
		Layout.GeneratorVersion = TD_WORLDGEN_VERSION;
		Layout.RegionId = Region.RegionId;
		Layout.BoundsCm = BoundsCm;

		int32 MainIndex = 0;
		for (const FTDWorldAnchor& Source : Anchors)
		{
			FTDWorldAnchor& Anchor = Layout.Anchors.Add_GetRef(Source);
			Anchor.bLocked = true;

			FTDExclusionArea& Exclusion = Layout.Exclusions.AddDefaulted_GetRef();
			Exclusion.CenterCm = Anchor.LocationCm;
			Exclusion.RadiusCm = Anchor.ExclusionRadiusCm;
			Exclusion.Reason = TEXT("Anchor");

			if (Anchor.Kind != ETDWorldAnchorKind::MainDungeon)
			{
				continue;
			}
			FTDDungeonEntrancePlacement& Entrance = Layout.Entrances.AddDefaulted_GetRef();
			Entrance.DungeonId = Anchor.AnchorId.IsNone() ? FName(TEXT("Main")) : Anchor.AnchorId;
			Entrance.LocationCm = Anchor.LocationCm;
			Entrance.Yaw = Anchor.YawDeg;
			Entrance.bIsMain = true;
			Entrance.bLocked = true;
			Entrance.StableId = MakeStableGuid(Layout.Seed, TEXT("MainEntrance"), MainIndex);
			++MainIndex;
		}
	}
}

bool FTDWorldGraphGenerator::Generate(const UTDRegionDefinition& Region, const TArray<FTDWorldAnchor>& Anchors, const FBox2D& BoundsCm, const FTDSeedContext& Seed, const FTDTerrainSampler* Terrain, const FSettings& Settings, FTDWorldLayout& OutLayout, FString& OutError)
{
	return Generate(Region, Anchors, FTDLockedLayoutElements(), BoundsCm, Seed, Terrain, Settings, OutLayout, OutError);
}

bool FTDWorldGraphGenerator::Generate(const UTDRegionDefinition& Region, const TArray<FTDWorldAnchor>& Anchors, const FTDLockedLayoutElements& Locked, const FBox2D& BoundsCm, const FTDSeedContext& Seed, const FTDTerrainSampler* Terrain, const FSettings& Settings, FTDWorldLayout& OutLayout, FString& OutError)
{
	OutError.Reset();
	const FBox2D UsableArea(BoundsCm.Min + FVector2D(Region.BorderMarginCm), BoundsCm.Max - FVector2D(Region.BorderMarginCm));
	if (!BoundsCm.bIsValid || UsableArea.Min.X >= UsableArea.Max.X || UsableArea.Min.Y >= UsableArea.Max.Y)
	{
		OutError = TEXT("BoundsCm에서 BorderMarginCm를 뺀 배치 영역이 비어 있습니다");
		return false;
	}
	if (Region.MinPoiSpacingCm <= 0.0f)
	{
		OutError = TEXT("MinPoiSpacingCm는 0보다 커야 합니다");
		return false;
	}

	FTDWorldLayout BaseLayout;
	SeedLayoutWithAnchors(Region, Anchors, BoundsCm, Seed, BaseLayout);
	SeedLayoutWithLockedElements(Region, Locked, BaseLayout);
	const int32 SideEntranceTarget = FMath::Max(0, Region.SideDungeonCount - CountSideEntrances(BaseLayout));

	TArray<FTDPlacementRequest> Requests;
	FRandomStream CountStream = Seed.Derive(TEXT("Counts"));
	BuildPlacementRequests(Region, Settings, CountStream, Requests);
	SubtractLockedCounts(BaseLayout, Requests);

	FTDWorldLayout BestLayout;
	int32 BestShortage = TNumericLimits<int32>::Max();
	FString BestShortageMessage;
	const int32 RetryCount = FMath::Max(0, Settings.MaxPlacementRetries);
	for (int32 Retry = 0; Retry <= RetryCount; ++Retry)
	{
		TArray<FTDPlacementCandidate> Candidates;
		FRandomStream PoissonStream = Seed.Derive(TEXT("Poisson"), Retry);
		SamplePoissonDisc(UsableArea, Region.MinPoiSpacingCm, FMath::Max(1, Settings.PoissonAttemptsPerPoint), PoissonStream, Candidates);
		FRandomStream PlacementStream = Seed.Derive(TEXT("Placement"), Retry);
		ShuffleCandidates(Candidates, PlacementStream);
		SampleCandidateTerrain(Candidates, Terrain);

		FTDWorldLayout Layout = BaseLayout;
		int32 Shortage = 0;
		FString ShortageMessage;
		const int32 PlacedEntrances = PlaceSideEntrances(Region, SideEntranceTarget, Candidates, Terrain, PlacementStream, Layout);
		if (PlacedEntrances < SideEntranceTarget)
		{
			Shortage += SideEntranceTarget - PlacedEntrances;
			ShortageMessage += FString::Printf(TEXT("사이드 던전 입구 %d/%d; "), PlacedEntrances, SideEntranceTarget);
		}
		for (const FTDPlacementRequest& Request : Requests)
		{
			const int32 Placed = PlacePois(Request, Candidates, Terrain, PlacementStream, Layout);
			if (Placed < Request.Count)
			{
				Shortage += Request.Count - Placed;
				ShortageMessage += FString::Printf(TEXT("%s(%s) %d/%d; "), *Request.ArchetypeId.ToString(), *PoiKindToString(Request.Kind), Placed, Request.Count);
			}
		}
		if (Shortage < BestShortage)
		{
			BestShortage = Shortage;
			BestLayout = MoveTemp(Layout);
			BestShortageMessage = ShortageMessage;
		}
		if (BestShortage == 0)
		{
			break;
		}
	}

	OutLayout = MoveTemp(BestLayout);
	if (BestShortage > 0)
	{
		OutError = FString::Printf(TEXT("배치 수량 부족(재시도 %d회): %s"), RetryCount, *BestShortageMessage);
	}
	return true;
}
