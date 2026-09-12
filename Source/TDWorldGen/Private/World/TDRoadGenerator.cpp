#include "World/TDWorldGeneration.h"

#include "Algo/Reverse.h"

namespace
{
	constexpr float TD_ROAD_POINT_SPACING_CM = 2000.0f;

	struct FTDRoadNode
	{
		FName NodeId;
		FVector LocationCm = FVector::ZeroVector;
	};

	struct FTDRoadGrid
	{
		FVector2D OriginCm = FVector2D::ZeroVector;
		float CellCm = 400.0f;
		int32 Width = 0;
		int32 Height = 0;
		TArray<float> CostMultiplier;

		int32 CellCount() const { return Width * Height; }
		int32 CellIndexOf(const FVector2D& LocationCm) const
		{
			const int32 X = FMath::Clamp(FMath::FloorToInt((LocationCm.X - OriginCm.X) / CellCm), 0, Width - 1);
			const int32 Y = FMath::Clamp(FMath::FloorToInt((LocationCm.Y - OriginCm.Y) / CellCm), 0, Height - 1);
			return Y * Width + X;
		}
		FVector2D CellCenterCm(int32 CellIndex) const
		{
			const int32 X = CellIndex % Width;
			const int32 Y = CellIndex / Width;
			return OriginCm + FVector2D((X + 0.5f) * CellCm, (Y + 0.5f) * CellCm);
		}
	};

	struct FTDOpenEntry
	{
		float FCost = 0.0f;
		int32 CellIndex = INDEX_NONE;
		bool operator<(const FTDOpenEntry& Other) const { return FCost < Other.FCost; }
	};

	struct FTDPairPath
	{
		float Cost = TNumericLimits<float>::Max();
		TArray<FVector> PointsCm;
	};

	void BuildGrid(const FTDWorldLayout& Layout, const FTDTerrainSampler* Terrain, const FTDRoadGenerator::FSettings& Settings, FTDRoadGrid& OutGrid)
	{
		const FVector2D Size = Layout.BoundsCm.GetSize();
		OutGrid.OriginCm = Layout.BoundsCm.Min;
		OutGrid.CellCm = FMath::Max(50.0f, Settings.GridCm);
		OutGrid.Width = FMath::Max(1, FMath::CeilToInt(Size.X / OutGrid.CellCm));
		OutGrid.Height = FMath::Max(1, FMath::CeilToInt(Size.Y / OutGrid.CellCm));
		OutGrid.CostMultiplier.Init(1.0f, OutGrid.CellCount());
		if (Terrain == nullptr || !Terrain->IsValid())
		{
			return;
		}
		for (int32 CellIndex = 0; CellIndex < OutGrid.CellCount(); ++CellIndex)
		{
			const FVector2D Center = OutGrid.CellCenterCm(CellIndex);
			const float SlopeTerm = Settings.SlopeCostWeight * FMath::Max(0.0f, Terrain->SampleSlope(Center)) / 45.0f;
			const float WaterTerm = Terrain->IsUnderWater(Center) ? Settings.WaterCost : 0.0f;
			OutGrid.CostMultiplier[CellIndex] = 1.0f + SlopeTerm + WaterTerm;
		}
	}

	float OctileDistanceCm(const FTDRoadGrid& Grid, int32 FromCell, int32 ToCell)
	{
		const int32 DeltaX = FMath::Abs(FromCell % Grid.Width - ToCell % Grid.Width);
		const int32 DeltaY = FMath::Abs(FromCell / Grid.Width - ToCell / Grid.Width);
		const int32 Diagonal = FMath::Min(DeltaX, DeltaY);
		return (DeltaX + DeltaY - 2 * Diagonal + Diagonal * UE_SQRT_2) * Grid.CellCm;
	}

	bool FindGridPath(const FTDRoadGrid& Grid, int32 StartCell, int32 GoalCell, TArray<int32>& OutCells, float& OutCost)
	{
		OutCells.Reset();
		OutCost = 0.0f;
		if (StartCell == GoalCell)
		{
			OutCells.Add(StartCell);
			return true;
		}
		TArray<float> GCost;
		GCost.Init(TNumericLimits<float>::Max(), Grid.CellCount());
		TArray<int32> Parent;
		Parent.Init(INDEX_NONE, Grid.CellCount());
		TBitArray<> Closed(false, Grid.CellCount());
		TArray<FTDOpenEntry> Open;
		GCost[StartCell] = 0.0f;
		Open.HeapPush(FTDOpenEntry{ OctileDistanceCm(Grid, StartCell, GoalCell), StartCell });

		const int32 OffsetsX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		const int32 OffsetsY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		while (Open.Num() > 0)
		{
			FTDOpenEntry Current;
			Open.HeapPop(Current, EAllowShrinking::No);
			if (Closed[Current.CellIndex])
			{
				continue;
			}
			Closed[Current.CellIndex] = true;
			if (Current.CellIndex == GoalCell)
			{
				break;
			}
			const int32 CurrentX = Current.CellIndex % Grid.Width;
			const int32 CurrentY = Current.CellIndex / Grid.Width;
			for (int32 Direction = 0; Direction < 8; ++Direction)
			{
				const int32 NextX = CurrentX + OffsetsX[Direction];
				const int32 NextY = CurrentY + OffsetsY[Direction];
				if (NextX < 0 || NextY < 0 || NextX >= Grid.Width || NextY >= Grid.Height)
				{
					continue;
				}
				const int32 NextCell = NextY * Grid.Width + NextX;
				if (Closed[NextCell])
				{
					continue;
				}
				const float StepCm = Direction < 4 ? Grid.CellCm : Grid.CellCm * UE_SQRT_2;
				const float NextG = GCost[Current.CellIndex] + StepCm * Grid.CostMultiplier[NextCell];
				if (NextG >= GCost[NextCell])
				{
					continue;
				}
				GCost[NextCell] = NextG;
				Parent[NextCell] = Current.CellIndex;
				Open.HeapPush(FTDOpenEntry{ NextG + OctileDistanceCm(Grid, NextCell, GoalCell) * 1.001f, NextCell });
			}
		}
		if (!Closed[GoalCell])
		{
			return false;
		}
		for (int32 Cell = GoalCell; Cell != INDEX_NONE; Cell = Parent[Cell])
		{
			OutCells.Add(Cell);
		}
		Algo::Reverse(OutCells);
		OutCost = GCost[GoalCell];
		return true;
	}

	void ResamplePolyline(const TArray<FVector>& Source, float SpacingCm, TArray<FVector>& OutPoints)
	{
		OutPoints.Reset();
		if (Source.Num() < 2)
		{
			OutPoints = Source;
			return;
		}
		float TotalLength = 0.0f;
		for (int32 Index = 1; Index < Source.Num(); ++Index)
		{
			TotalLength += FVector::Dist2D(Source[Index - 1], Source[Index]);
		}
		const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(TotalLength / SpacingCm));
		const float Step = TotalLength / SegmentCount;
		OutPoints.Add(Source[0]);
		int32 SourceIndex = 1;
		float SegmentStart = 0.0f;
		for (int32 Sample = 1; Sample < SegmentCount; ++Sample)
		{
			const float Target = Sample * Step;
			while (SourceIndex < Source.Num() - 1 && SegmentStart + FVector::Dist2D(Source[SourceIndex - 1], Source[SourceIndex]) < Target)
			{
				SegmentStart += FVector::Dist2D(Source[SourceIndex - 1], Source[SourceIndex]);
				++SourceIndex;
			}
			const float SegmentLength = FVector::Dist2D(Source[SourceIndex - 1], Source[SourceIndex]);
			const float Alpha = SegmentLength > UE_KINDA_SMALL_NUMBER ? FMath::Clamp((Target - SegmentStart) / SegmentLength, 0.0f, 1.0f) : 0.0f;
			OutPoints.Add(FMath::Lerp(Source[SourceIndex - 1], Source[SourceIndex], Alpha));
		}
		OutPoints.Add(Source.Last());
	}

	void CollectNodes(const FTDWorldLayout& Layout, TArray<FTDRoadNode>& OutNodes)
	{
		for (const FTDWorldAnchor& Anchor : Layout.Anchors)
		{
			if (Anchor.Kind != ETDWorldAnchorKind::Town)
			{
				continue;
			}
			OutNodes.Add({ Anchor.AnchorId.IsNone() ? FName(TEXT("Town")) : Anchor.AnchorId, Anchor.LocationCm });
		}
		for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
		{
			OutNodes.Add({ Entrance.DungeonId, Entrance.LocationCm });
		}
		for (const FTDPoiPlacement& Poi : Layout.Pois)
		{
			OutNodes.Add({ Poi.PoiId, Poi.LocationCm });
		}
	}

	void BuildPairPath(const FTDRoadGrid& Grid, const FTDTerrainSampler* Terrain, const FTDRoadNode& From, const FTDRoadNode& To, FTDPairPath& OutPath)
	{
		TArray<int32> Cells;
		float Cost = 0.0f;
		if (!FindGridPath(Grid, Grid.CellIndexOf(FVector2D(From.LocationCm)), Grid.CellIndexOf(FVector2D(To.LocationCm)), Cells, Cost))
		{
			return;
		}
		TArray<FVector> RawPoints;
		RawPoints.Add(From.LocationCm);
		for (int32 Index = 1; Index < Cells.Num() - 1; ++Index)
		{
			const FVector2D Center = Grid.CellCenterCm(Cells[Index]);
			const float Height = Terrain != nullptr && Terrain->IsValid() ? Terrain->SampleHeight(Center) : 0.0f;
			RawPoints.Add(FVector(Center, Height));
		}
		RawPoints.Add(To.LocationCm);
		OutPath.Cost = Cost;
		ResamplePolyline(RawPoints, TD_ROAD_POINT_SPACING_CM, OutPath.PointsCm);
	}

	void AddRoad(const FTDRoadNode& From, const FTDRoadNode& To, const FTDPairPath& Path, bool bIsPrimary, const FTDRoadGenerator::FSettings& Settings, FTDWorldLayout& Layout)
	{
		FTDRoadPolyline& Road = Layout.Roads.AddDefaulted_GetRef();
		Road.RoadId = *FString::Printf(TEXT("Road_%s_%s"), *From.NodeId.ToString(), *To.NodeId.ToString());
		Road.PointsCm = Path.PointsCm;
		Road.WidthCm = Settings.RoadWidthCm;
		Road.ClearanceCm = Settings.RoadClearanceCm;
		Road.bIsPrimary = bIsPrimary;
		Road.StableId = FTDSeedContext(Layout.Seed).DeriveGuid(TEXT("Road"), Layout.Roads.Num() - 1);
	}
}

bool FTDRoadGenerator::Generate(FTDWorldLayout& InOutLayout, const FTDTerrainSampler* Terrain, const FSettings& Settings, const FTDSeedContext& Seed, FString& OutError)
{
	OutError.Reset();
	InOutLayout.Roads.Reset();
	if (!InOutLayout.BoundsCm.bIsValid)
	{
		OutError = TEXT("BoundsCm가 유효하지 않습니다");
		return false;
	}
	TArray<FTDRoadNode> Nodes;
	CollectNodes(InOutLayout, Nodes);
	if (Nodes.Num() == 0 || InOutLayout.FindAnchor(ETDWorldAnchorKind::Town) == nullptr)
	{
		OutError = TEXT("도로 노드가 없거나 마을(Town) 앵커가 없습니다");
		return false;
	}
	if (Nodes.Num() == 1)
	{
		return true;
	}

	FTDRoadGrid Grid;
	BuildGrid(InOutLayout, Terrain, Settings, Grid);

	const int32 NodeCount = Nodes.Num();
	TArray<FTDPairPath> Paths;
	Paths.SetNum(NodeCount * NodeCount);
	for (int32 From = 0; From < NodeCount; ++From)
	{
		for (int32 To = From + 1; To < NodeCount; ++To)
		{
			BuildPairPath(Grid, Terrain, Nodes[From], Nodes[To], Paths[From * NodeCount + To]);
		}
	}
	auto PathOf = [&](int32 A, int32 B) -> const FTDPairPath&
	{
		return A < B ? Paths[A * NodeCount + B] : Paths[B * NodeCount + A];
	};

	TArray<bool> InTree;
	InTree.Init(false, NodeCount);
	TArray<float> BestCost;
	BestCost.Init(TNumericLimits<float>::Max(), NodeCount);
	TArray<int32> BestParent;
	BestParent.Init(INDEX_NONE, NodeCount);
	TArray<TPair<int32, int32>> TreeEdges;
	BestCost[0] = 0.0f;
	for (int32 Step = 0; Step < NodeCount; ++Step)
	{
		int32 Next = INDEX_NONE;
		for (int32 Node = 0; Node < NodeCount; ++Node)
		{
			if (!InTree[Node] && BestCost[Node] < TNumericLimits<float>::Max() && (Next == INDEX_NONE || BestCost[Node] < BestCost[Next]))
			{
				Next = Node;
			}
		}
		if (Next == INDEX_NONE)
		{
			break;
		}
		InTree[Next] = true;
		if (BestParent[Next] != INDEX_NONE)
		{
			TreeEdges.Emplace(BestParent[Next], Next);
		}
		for (int32 Node = 0; Node < NodeCount; ++Node)
		{
			if (InTree[Node] || Node == Next)
			{
				continue;
			}
			const float Cost = PathOf(Next, Node).Cost;
			if (Cost < BestCost[Node])
			{
				BestCost[Node] = Cost;
				BestParent[Node] = Next;
			}
		}
	}

	TSet<uint64> TreeEdgeKeys;
	for (const TPair<int32, int32>& Edge : TreeEdges)
	{
		const int32 Low = FMath::Min(Edge.Key, Edge.Value);
		const int32 High = FMath::Max(Edge.Key, Edge.Value);
		TreeEdgeKeys.Add((static_cast<uint64>(Low) << 32) | static_cast<uint64>(High));
		AddRoad(Nodes[Low], Nodes[High], PathOf(Low, High), true, Settings, InOutLayout);
	}

	TArray<TPair<int32, int32>> SecondaryCandidates;
	for (int32 From = 0; From < NodeCount; ++From)
	{
		for (int32 To = From + 1; To < NodeCount; ++To)
		{
			const bool bIsTreeEdge = TreeEdgeKeys.Contains((static_cast<uint64>(From) << 32) | static_cast<uint64>(To));
			if (!bIsTreeEdge && InTree[From] && InTree[To] && PathOf(From, To).Cost < TNumericLimits<float>::Max())
			{
				SecondaryCandidates.Emplace(From, To);
			}
		}
	}
	SecondaryCandidates.StableSort([&](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return PathOf(A.Key, A.Value).Cost < PathOf(B.Key, B.Value).Cost;
	});
	const int32 SecondaryCount = FMath::Min(SecondaryCandidates.Num(), FMath::RoundToInt(FMath::Clamp(Settings.SecondaryRoadRatio, 0.0f, 1.0f) * TreeEdges.Num()));
	for (int32 Index = 0; Index < SecondaryCount; ++Index)
	{
		const TPair<int32, int32>& Edge = SecondaryCandidates[Index];
		AddRoad(Nodes[Edge.Key], Nodes[Edge.Value], PathOf(Edge.Key, Edge.Value), false, Settings, InOutLayout);
	}

	for (int32 Node = 0; Node < NodeCount; ++Node)
	{
		if (!InTree[Node])
		{
			OutError += FString::Printf(TEXT("%s 도로 경로 없음; "), *Nodes[Node].NodeId.ToString());
		}
	}
	return true;
}
