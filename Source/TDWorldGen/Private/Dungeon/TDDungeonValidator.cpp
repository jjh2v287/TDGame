#include "Dungeon/TDDungeonGeneration.h"

namespace
{
	const FName CodeRequiredRooms(TEXT("required_rooms"));
	const FName CodeOverlap(TEXT("overlap"));
	const FName CodeDoorIntegrity(TEXT("door_integrity"));
	const FName CodeConnectivity(TEXT("connectivity"));
	const FName CodeRoomCount(TEXT("room_count"));
	const FName CodeDeadEndRatio(TEXT("deadend_ratio"));
	const FName CodeMainPathRatio(TEXT("main_path_ratio"));
	const FName CodeProgression(TEXT("progression_key_lock"));
	constexpr int32 CheckCount = 8;
	constexpr float WarningCheckPenalty = 0.5f;

	struct FTDRoomAdjacency
	{
		TMap<FName, TArray<const FTDPlacedDoor*>> DoorsByRoom;

		explicit FTDRoomAdjacency(const FTDDungeonLayout& Layout)
		{
			for (const FTDPlacedRoom& Room : Layout.Rooms)
			{
				DoorsByRoom.Add(Room.RoomId);
			}
			for (const FTDPlacedDoor& Door : Layout.Doors)
			{
				DoorsByRoom.FindOrAdd(Door.RoomA).Add(&Door);
				DoorsByRoom.FindOrAdd(Door.RoomB).Add(&Door);
			}
		}

		int32 DegreeOf(FName RoomId) const
		{
			const TArray<const FTDPlacedDoor*>* Doors = DoorsByRoom.Find(RoomId);
			return Doors ? Doors->Num() : 0;
		}

		static FName OtherSide(const FTDPlacedDoor& Door, FName RoomId)
		{
			return Door.RoomA == RoomId ? Door.RoomB : Door.RoomA;
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
				const TArray<const FTDPlacedDoor*>* Doors = DoorsByRoom.Find(Current);
				if (!Doors)
				{
					continue;
				}
				for (const FTDPlacedDoor* Door : *Doors)
				{
					const FName Next = OtherSide(*Door, Current);
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

	void ReachWithKeys(const FTDDungeonLayout& Layout, const FTDRoomAdjacency& Adjacency, FName StartRoom, bool bRespectLocks, TSet<FName>& OutVisited, TSet<FName>& OutHeldKeys)
	{
		OutVisited.Reset();
		OutHeldKeys.Reset();
		TArray<FName> Order;
		Order.Add(StartRoom);
		OutVisited.Add(StartRoom);
		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;
			for (int32 Head = 0; Head < Order.Num(); ++Head)
			{
				const FName Current = Order[Head];
				if (const FTDPlacedRoom* Room = Layout.FindRoom(Current))
				{
					if (!Room->HeldKeyId.IsNone() && !OutHeldKeys.Contains(Room->HeldKeyId))
					{
						OutHeldKeys.Add(Room->HeldKeyId);
						bChanged = true;
					}
				}
				const TArray<const FTDPlacedDoor*>* Doors = Adjacency.DoorsByRoom.Find(Current);
				if (!Doors)
				{
					continue;
				}
				for (const FTDPlacedDoor* Door : *Doors)
				{
					const FName Next = FTDRoomAdjacency::OtherSide(*Door, Current);
					if (OutVisited.Contains(Next))
					{
						continue;
					}
					if (bRespectLocks && Door->IsLocked() && !OutHeldKeys.Contains(Door->LockId))
					{
						continue;
					}
					OutVisited.Add(Next);
					Order.Add(Next);
					bChanged = true;
				}
			}
		}
	}

	FVector RoomLocation(const FTDDungeonLayout& Layout, const FTDPlacedRoom& Room)
	{
		return Room.Cells.Num() > 0 ? Layout.CellCenterLocal(Room.Cells[0]) : FVector::ZeroVector;
	}

	void AddSummary(FTDValidationReport& Report, FName Code, ETDValidationSeverity FailedSeverity, bool bPassed, const FString& Message, const FTDDungeonLayout& Layout, const FTDPlacedRoom* AnchorRoom)
	{
		const ETDValidationSeverity Severity = bPassed ? ETDValidationSeverity::Info : FailedSeverity;
		const FVector Location = AnchorRoom ? RoomLocation(Layout, *AnchorRoom) : FVector::ZeroVector;
		Report.Add(Severity, Code, Message, Location, AnchorRoom ? AnchorRoom->RoomId : NAME_None);
	}

	void CheckRequiredRooms(const FTDDungeonLayout& Layout, const FTDPlacedRoom* Anchor, FTDValidationReport& Report)
	{
		int32 Entrances = 0;
		int32 Bosses = 0;
		for (const FTDPlacedRoom& Room : Layout.Rooms)
		{
			Entrances += Room.HasRole(ETDRoomRole::Entrance) ? 1 : 0;
			Bosses += Room.HasRole(ETDRoomRole::Boss) ? 1 : 0;
		}
		AddSummary(Report, CodeRequiredRooms, ETDValidationSeverity::Error, Entrances == 1 && Bosses >= 1, FString::Printf(TEXT("start=%d boss=%d"), Entrances, Bosses), Layout, Anchor);
	}

	void CheckOverlap(const FTDDungeonLayout& Layout, const FTDPlacedRoom* Anchor, FTDValidationReport& Report)
	{
		TMap<FIntPoint, FName> Owner;
		int32 Problems = 0;
		for (const FTDPlacedRoom& Room : Layout.Rooms)
		{
			for (const FIntPoint& Cell : Room.Cells)
			{
				if (const FName* Existing = Owner.Find(Cell))
				{
					++Problems;
					Report.Add(ETDValidationSeverity::Error, CodeOverlap, FString::Printf(TEXT("cell (%d,%d): %s and %s"), Cell.X, Cell.Y, *Existing->ToString(), *Room.RoomId.ToString()), Layout.CellCenterLocal(Cell), Room.RoomId);
				}
				Owner.Add(Cell, Room.RoomId);
			}
		}
		AddSummary(Report, CodeOverlap, ETDValidationSeverity::Error, Problems == 0, FString::Printf(TEXT("%d overlapping cells"), Problems), Layout, Anchor);
	}

	void CheckDoorIntegrity(const FTDDungeonLayout& Layout, const FTDPlacedRoom* Anchor, FTDValidationReport& Report)
	{
		TMap<FIntPoint, FName> Owner;
		for (const FTDPlacedRoom& Room : Layout.Rooms)
		{
			for (const FIntPoint& Cell : Room.Cells)
			{
				Owner.Add(Cell, Room.RoomId);
			}
		}
		int32 Problems = 0;
		for (const FTDPlacedDoor& Door : Layout.Doors)
		{
			const FIntPoint ExpectedB = Door.CellA + TDDungeon::DirectionOffset(Door.DirectionFromA);
			const FString DoorName = FString::Printf(TEXT("door %s->%s"), *Door.RoomA.ToString(), *Door.RoomB.ToString());
			if (ExpectedB != Door.CellB)
			{
				++Problems;
				Report.Add(ETDValidationSeverity::Error, CodeDoorIntegrity, FString::Printf(TEXT("%s cells (%d,%d)/(%d,%d) not adjacent-facing"), *DoorName, Door.CellA.X, Door.CellA.Y, Door.CellB.X, Door.CellB.Y), Layout.CellCenterLocal(Door.CellA), Door.RoomA);
				continue;
			}
			const FName* OwnerA = Owner.Find(Door.CellA);
			const FName* OwnerB = Owner.Find(Door.CellB);
			if (!OwnerA || !OwnerB || *OwnerA != Door.RoomA || *OwnerB != Door.RoomB)
			{
				++Problems;
				Report.Add(ETDValidationSeverity::Error, CodeDoorIntegrity, FString::Printf(TEXT("%s cell ownership mismatch at (%d,%d)/(%d,%d)"), *DoorName, Door.CellA.X, Door.CellA.Y, Door.CellB.X, Door.CellB.Y), Layout.CellCenterLocal(Door.CellA), Door.RoomA);
			}
		}
		AddSummary(Report, CodeDoorIntegrity, ETDValidationSeverity::Error, Problems == 0, FString::Printf(TEXT("%d door connections checked, %d problems"), Layout.Doors.Num(), Problems), Layout, Anchor);
	}

	void CheckConnectivity(const FTDDungeonLayout& Layout, const FTDRoomAdjacency& Adjacency, const FTDPlacedRoom* Entrance, const FTDPlacedRoom* Boss, const FTDPlacedRoom* Anchor, FTDValidationReport& Report)
	{
		if (!Entrance || !Boss)
		{
			AddSummary(Report, CodeConnectivity, ETDValidationSeverity::Error, false, TEXT("entrance or boss room missing"), Layout, Anchor);
			return;
		}
		TSet<FName> Visited;
		TSet<FName> HeldKeys;
		ReachWithKeys(Layout, Adjacency, Entrance->RoomId, false, Visited, HeldKeys);
		int32 Unreachable = 0;
		for (const FTDPlacedRoom& Room : Layout.Rooms)
		{
			if (Visited.Contains(Room.RoomId))
			{
				continue;
			}
			++Unreachable;
			Report.Add(ETDValidationSeverity::Error, CodeConnectivity, FString::Printf(TEXT("unreachable room %s"), *Room.RoomId.ToString()), RoomLocation(Layout, Room), Room.RoomId);
		}
		const bool bBossReachable = Visited.Contains(Boss->RoomId);
		AddSummary(Report, CodeConnectivity, ETDValidationSeverity::Error, bBossReachable && Unreachable == 0, FString::Printf(TEXT("boss reachable=%s, unreachable rooms=%d"), bBossReachable ? TEXT("true") : TEXT("false"), Unreachable), Layout, Boss);
	}

	int32 CountNonCorridorRooms(const FTDDungeonLayout& Layout)
	{
		int32 Count = 0;
		for (const FTDPlacedRoom& Room : Layout.Rooms)
		{
			Count += Room.HasRole(ETDRoomRole::Corridor) ? 0 : 1;
		}
		return Count;
	}

	void CheckRoomCount(const FTDDungeonLayout& Layout, const FIntPoint& Range, const FTDPlacedRoom* Anchor, FTDValidationReport& Report)
	{
		const int32 Count = CountNonCorridorRooms(Layout);
		const bool bPassed = Count >= Range.X && Count <= Range.Y;
		AddSummary(Report, CodeRoomCount, ETDValidationSeverity::Error, bPassed, FString::Printf(TEXT("%d rooms (allowed %d..%d for %s)"), Count, Range.X, Range.Y, TDDungeon::SizeName(Layout.Size)), Layout, Anchor);
	}

	void CheckDeadEndRatio(const FTDDungeonLayout& Layout, const FTDRoomAdjacency& Adjacency, float MaxRatio, const FTDPlacedRoom* Anchor, FTDValidationReport& Report)
	{
		const int32 RoomCount = CountNonCorridorRooms(Layout);
		TArray<const FTDPlacedRoom*> DeadEnds;
		for (const FTDPlacedRoom& Room : Layout.Rooms)
		{
			if (Room.HasRole(ETDRoomRole::Corridor) || Room.HasRole(ETDRoomRole::Entrance) || Room.HasRole(ETDRoomRole::Boss))
			{
				continue;
			}
			if (Adjacency.DegreeOf(Room.RoomId) == 1)
			{
				DeadEnds.Add(&Room);
			}
		}
		const float Ratio = RoomCount > 0 ? static_cast<float>(DeadEnds.Num()) / RoomCount : 0.0f;
		const bool bPassed = Ratio <= MaxRatio;
		if (!bPassed)
		{
			for (const FTDPlacedRoom* Room : DeadEnds)
			{
				Report.Add(ETDValidationSeverity::Warning, CodeDeadEndRatio, FString::Printf(TEXT("dead-end room %s"), *Room->RoomId.ToString()), RoomLocation(Layout, *Room), Room->RoomId);
			}
		}
		AddSummary(Report, CodeDeadEndRatio, ETDValidationSeverity::Warning, bPassed, FString::Printf(TEXT("%d/%d = %.2f (max %.2f)"), DeadEnds.Num(), RoomCount, Ratio, MaxRatio), Layout, Anchor);
	}

	void CheckMainPathRatio(const FTDDungeonLayout& Layout, const FTDRoomAdjacency& Adjacency, const FTDPlacedRoom* Entrance, const FTDPlacedRoom* Boss, float MinRatio, const FTDPlacedRoom* Anchor, FTDValidationReport& Report)
	{
		const int32 RoomCount = CountNonCorridorRooms(Layout);
		TArray<FName> Path;
		if (!Entrance || !Boss || RoomCount == 0 || !Adjacency.FindShortestPath(Entrance->RoomId, Boss->RoomId, Path))
		{
			AddSummary(Report, CodeMainPathRatio, ETDValidationSeverity::Warning, false, TEXT("no entrance-to-boss path"), Layout, Boss ? Boss : Anchor);
			return;
		}
		TArray<const FTDPlacedRoom*> PathRooms;
		TArray<FString> PathRoomIds;
		for (const FName RoomId : Path)
		{
			const FTDPlacedRoom* Room = Layout.FindRoom(RoomId);
			if (!Room || Room->HasRole(ETDRoomRole::Corridor))
			{
				continue;
			}
			PathRooms.Add(Room);
			PathRoomIds.Add(RoomId.ToString());
		}
		const float Ratio = static_cast<float>(PathRooms.Num()) / RoomCount;
		const bool bPassed = Ratio >= MinRatio;
		if (!bPassed)
		{
			for (int32 Step = 0; Step < PathRooms.Num(); ++Step)
			{
				Report.Add(ETDValidationSeverity::Warning, CodeMainPathRatio, FString::Printf(TEXT("main path step %d room %s"), Step, *PathRooms[Step]->RoomId.ToString()), RoomLocation(Layout, *PathRooms[Step]), PathRooms[Step]->RoomId);
			}
		}
		AddSummary(Report, CodeMainPathRatio, ETDValidationSeverity::Warning, bPassed, FString::Printf(TEXT("%d/%d = %.2f (min %.2f) path %s"), PathRooms.Num(), RoomCount, Ratio, MinRatio, *FString::Join(PathRoomIds, TEXT(">"))), Layout, Boss);
	}

	void CheckProgression(const FTDDungeonLayout& Layout, const FTDRoomAdjacency& Adjacency, const FTDPlacedRoom* Entrance, const FTDPlacedRoom* Boss, const FTDPlacedRoom* Anchor, FTDValidationReport& Report)
	{
		if (!Entrance || !Boss)
		{
			AddSummary(Report, CodeProgression, ETDValidationSeverity::Error, false, TEXT("entrance or boss room missing"), Layout, Anchor);
			return;
		}
		TSet<FName> Visited;
		TSet<FName> HeldKeys;
		ReachWithKeys(Layout, Adjacency, Entrance->RoomId, true, Visited, HeldKeys);
		int32 Problems = 0;
		int32 LockCount = 0;
		for (const FTDPlacedDoor& Door : Layout.Doors)
		{
			if (!Door.IsLocked())
			{
				continue;
			}
			++LockCount;
			const FTDPlacedRoom* KeyRoom = Layout.Rooms.FindByPredicate([&Door](const FTDPlacedRoom& Room) { return Room.HeldKeyId == Door.LockId; });
			if (!KeyRoom)
			{
				++Problems;
				Report.Add(ETDValidationSeverity::Error, CodeProgression, FString::Printf(TEXT("lock %s has no key room"), *Door.LockId.ToString()), Layout.CellCenterLocal(Door.CellA), Door.RoomA);
				continue;
			}
			if (!Visited.Contains(KeyRoom->RoomId))
			{
				++Problems;
				Report.Add(ETDValidationSeverity::Error, CodeProgression, FString::Printf(TEXT("key room %s (%s) unreachable before its lock"), *KeyRoom->RoomId.ToString(), *Door.LockId.ToString()), RoomLocation(Layout, *KeyRoom), KeyRoom->RoomId);
			}
		}
		if (!Visited.Contains(Boss->RoomId))
		{
			++Problems;
			Report.Add(ETDValidationSeverity::Error, CodeProgression, FString::Printf(TEXT("boss %s unreachable when locks are respected"), *Boss->RoomId.ToString()), RoomLocation(Layout, *Boss), Boss->RoomId);
		}
		for (const FTDPlacedRoom& Room : Layout.Rooms)
		{
			if (Visited.Contains(Room.RoomId))
			{
				continue;
			}
			++Problems;
			Report.Add(ETDValidationSeverity::Error, CodeProgression, FString::Printf(TEXT("room %s never reachable with keys"), *Room.RoomId.ToString()), RoomLocation(Layout, Room), Room.RoomId);
		}
		AddSummary(Report, CodeProgression, ETDValidationSeverity::Error, Problems == 0, FString::Printf(TEXT("locks=%d keys_held=%d"), LockCount, HeldKeys.Num()), Layout, Boss);
	}
}

FTDValidationReport FTDDungeonValidator::Validate(const FTDDungeonLayout& Layout, const FSettings& Settings, const FVector& WorldOriginCm)
{
	FTDValidationReport Report;
	const FTDRoomAdjacency Adjacency(Layout);
	const FTDPlacedRoom* Entrance = Layout.FindRoomByRole(ETDRoomRole::Entrance);
	const FTDPlacedRoom* Boss = Layout.FindRoomByRole(ETDRoomRole::Boss);
	const FTDPlacedRoom* FirstRoom = Layout.Rooms.Num() > 0 ? &Layout.Rooms[0] : nullptr;
	const FTDPlacedRoom* Anchor = Entrance ? Entrance : (Boss ? Boss : FirstRoom);
	CheckRequiredRooms(Layout, Anchor, Report);
	CheckOverlap(Layout, Anchor, Report);
	CheckDoorIntegrity(Layout, Anchor, Report);
	CheckConnectivity(Layout, Adjacency, Entrance, Boss, Anchor, Report);
	CheckRoomCount(Layout, Settings.RoomCountRange, Anchor, Report);
	CheckDeadEndRatio(Layout, Adjacency, Settings.MaxDeadEndRatio, Anchor, Report);
	CheckMainPathRatio(Layout, Adjacency, Entrance, Boss, Settings.MinMainPathRatio, Anchor, Report);
	CheckProgression(Layout, Adjacency, Entrance, Boss, Anchor, Report);
	TSet<FName> ErrorCodes;
	TSet<FName> WarningCodes;
	for (FTDValidationItem& Item : Report.Items)
	{
		Item.WorldLocation += WorldOriginCm;
		if (Item.Severity == ETDValidationSeverity::Error)
		{
			ErrorCodes.Add(Item.Code);
		}
		if (Item.Severity == ETDValidationSeverity::Warning)
		{
			WarningCodes.Add(Item.Code);
		}
	}
	const int32 WarningOnlyCount = WarningCodes.Difference(ErrorCodes).Num();
	Report.Score = 100.0f * (CheckCount - ErrorCodes.Num() - WarningCheckPenalty * WarningOnlyCount) / CheckCount;
	Report.bPassed = !Report.HasErrors();
	return Report;
}
