#include "Dungeon/TDDungeonGeneration.h"

namespace
{
	struct FTDSelectorGraph
	{
		TMap<FName, TArray<FName>> Neighbors;

		explicit FTDSelectorGraph(const FTDDungeonLayout& Layout)
		{
			for (const FTDPlacedRoom& Room : Layout.Rooms)
			{
				Neighbors.Add(Room.RoomId);
			}
			for (const FTDPlacedDoor& Door : Layout.Doors)
			{
				Neighbors.FindOrAdd(Door.RoomA).Add(Door.RoomB);
				Neighbors.FindOrAdd(Door.RoomB).Add(Door.RoomA);
			}
		}

		int32 DegreeOf(FName RoomId) const
		{
			const TArray<FName>* Found = Neighbors.Find(RoomId);
			return Found ? Found->Num() : 0;
		}

		bool FindShortestPath(FName Start, FName Goal, TArray<FName>& OutPath) const
		{
			TMap<FName, FName> Previous;
			Previous.Add(Start, NAME_None);
			TArray<FName> Frontier = {Start};
			for (int32 Head = 0; Head < Frontier.Num(); ++Head)
			{
				const FName Current = Frontier[Head];
				if (Current == Goal)
				{
					OutPath.Reset();
					for (FName Walk = Current; !Walk.IsNone(); Walk = Previous[Walk])
					{
						OutPath.Insert(Walk, 0);
					}
					return true;
				}
				const TArray<FName>* Adjacent = Neighbors.Find(Current);
				if (!Adjacent)
				{
					continue;
				}
				for (const FName Next : *Adjacent)
				{
					if (Previous.Contains(Next))
					{
						continue;
					}
					Previous.Add(Next, Current);
					Frontier.Add(Next);
				}
			}
			return false;
		}
	};
}

float FTDCandidateSelector::ScoreDungeon(const FTDDungeonLayout& Layout)
{
	if (!Layout.Validation.bPassed)
	{
		return 0.0f;
	}
	const FTDPlacedRoom* Entrance = Layout.FindRoomByRole(ETDRoomRole::Entrance);
	const FTDPlacedRoom* Boss = Layout.FindRoomByRole(ETDRoomRole::Boss);
	if (!Entrance || !Boss)
	{
		return 0.0f;
	}
	const FTDSelectorGraph Graph(Layout);
	int32 RoomCount = 0;
	int32 DeadEndCount = 0;
	int32 BranchCount = 0;
	for (const FTDPlacedRoom& Room : Layout.Rooms)
	{
		if (Room.HasRole(ETDRoomRole::Corridor))
		{
			continue;
		}
		++RoomCount;
		const int32 Degree = Graph.DegreeOf(Room.RoomId);
		const bool bIsEndpoint = Room.HasRole(ETDRoomRole::Entrance) || Room.HasRole(ETDRoomRole::Boss);
		DeadEndCount += (!bIsEndpoint && Degree == 1) ? 1 : 0;
		BranchCount += Degree >= 3 ? 1 : 0;
	}
	if (RoomCount == 0)
	{
		return 0.0f;
	}
	TArray<FName> Path;
	int32 MainPathRooms = 0;
	if (Graph.FindShortestPath(Entrance->RoomId, Boss->RoomId, Path))
	{
		for (const FName RoomId : Path)
		{
			const FTDPlacedRoom* Room = Layout.FindRoom(RoomId);
			MainPathRooms += (Room && !Room->HasRole(ETDRoomRole::Corridor)) ? 1 : 0;
		}
	}
	const float DeadEndRatio = static_cast<float>(DeadEndCount) / RoomCount;
	const float MainPathRatio = static_cast<float>(MainPathRooms) / RoomCount;
	const int32 LoopCount = FMath::Max(0, Layout.Doors.Num() - (Layout.Rooms.Num() - 1));
	float Score = 60.0f;
	Score -= 40.0f * DeadEndRatio;
	Score -= 30.0f * FMath::Abs(MainPathRatio - 0.65f);
	Score += FMath::Min(15.0f, 3.0f * BranchCount);
	Score += FMath::Min(10.0f, 5.0f * LoopCount);
	Score += 15.0f * FMath::Clamp(Layout.Validation.Score / 100.0f, 0.0f, 1.0f);
	return FMath::Clamp(Score, 0.0f, 100.0f);
}

void FTDCandidateSelector::Rank(TArray<FCandidate>& InOutCandidates)
{
	InOutCandidates.StableSort([](const FCandidate& Left, const FCandidate& Right)
	{
		if (Left.bPassed != Right.bPassed)
		{
			return Left.bPassed;
		}
		if (!FMath::IsNearlyEqual(Left.Score, Right.Score))
		{
			return Left.Score > Right.Score;
		}
		return Left.Seed < Right.Seed;
	});
}
