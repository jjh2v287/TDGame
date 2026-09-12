#include "Dungeon/TDDungeonGeneration.h"

namespace
{
	const FName CodeRequiredRooms(TEXT("required_rooms"));
	const FName CodeOverlap(TEXT("overlap"));
	const FName CodeDoorIntegrity(TEXT("door_integrity"));
	const FName CodeConnectivity(TEXT("connectivity"));
	const FName CodeRoomCount(TEXT("room_count"));
	const FName CodeDeadEndRatio(TEXT("deadend_ratio"));
	const FName CodeProgression(TEXT("progression_key_lock"));
	constexpr int32 CheckCount = 7;

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

	void AddSummary(FTDValidationReport& Report, FName Code, bool bPassed, const FString& Message)
	{
		Report.Add(bPassed ? ETDValidationSeverity::Info : ETDValidationSeverity::Error, Code, Message);
	}

	void CheckRequiredRooms(const FTDDungeonLayout& Layout, FTDValidationReport& Report)
	{
		int32 Entrances = 0;
		int32 Bosses = 0;
		for (const FTDPlacedRoom& Room : Layout.Rooms)
		{
			Entrances += Room.HasRole(ETDRoomRole::Entrance) ? 1 : 0;
			Bosses += Room.HasRole(ETDRoomRole::Boss) ? 1 : 0;
		}
		AddSummary(Report, CodeRequiredRooms, Entrances == 1 && Bosses >= 1, FString::Printf(TEXT("start=%d boss=%d"), Entrances, Bosses));
	}

	void CheckOverlap(const FTDDungeonLayout& Layout, FTDValidationReport& Report)
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
		AddSummary(Report, CodeOverlap, Problems == 0, FString::Printf(TEXT("%d overlapping cells"), Problems));
	}

	void CheckDoorIntegrity(const FTDDungeonLayout& Layout, FTDValidationReport& Report)
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
		AddSummary(Report, CodeDoorIntegrity, Problems == 0, FString::Printf(TEXT("%d door connections checked, %d problems"), Layout.Doors.Num(), Problems));
	}

	void CheckConnectivity(const FTDDungeonLayout& Layout, const FTDRoomAdjacency& Adjacency, const FTDPlacedRoom* Entrance, const FTDPlacedRoom* Boss, FTDValidationReport& Report)
	{
		if (!Entrance || !Boss)
		{
			AddSummary(Report, CodeConnectivity, false, TEXT("entrance or boss room missing"));
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
		AddSummary(Report, CodeConnectivity, bBossReachable && Unreachable == 0, FString::Printf(TEXT("boss reachable=%s, unreachable rooms=%d"), bBossReachable ? TEXT("true") : TEXT("false"), Unreachable));
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

	void CheckRoomCount(const FTDDungeonLayout& Layout, const FIntPoint& Range, FTDValidationReport& Report)
	{
		const int32 Count = CountNonCorridorRooms(Layout);
		const bool bPassed = Count >= Range.X && Count <= Range.Y;
		AddSummary(Report, CodeRoomCount, bPassed, FString::Printf(TEXT("%d rooms (allowed %d..%d for %s)"), Count, Range.X, Range.Y, TDDungeon::SizeName(Layout.Size)));
	}

	void CheckDeadEndRatio(const FTDDungeonLayout& Layout, const FTDRoomAdjacency& Adjacency, float MaxRatio, FTDValidationReport& Report)
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
		AddSummary(Report, CodeDeadEndRatio, bPassed, FString::Printf(TEXT("%d/%d = %.2f (max %.2f)"), DeadEnds.Num(), RoomCount, Ratio, MaxRatio));
	}

	void CheckProgression(const FTDDungeonLayout& Layout, const FTDRoomAdjacency& Adjacency, const FTDPlacedRoom* Entrance, const FTDPlacedRoom* Boss, FTDValidationReport& Report)
	{
		if (!Entrance || !Boss)
		{
			AddSummary(Report, CodeProgression, false, TEXT("entrance or boss room missing"));
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
		AddSummary(Report, CodeProgression, Problems == 0, FString::Printf(TEXT("locks=%d keys_held=%d"), LockCount, HeldKeys.Num()));
	}
}

FTDValidationReport FTDDungeonValidator::Validate(const FTDDungeonLayout& Layout, const FSettings& Settings)
{
	FTDValidationReport Report;
	const FTDRoomAdjacency Adjacency(Layout);
	const FTDPlacedRoom* Entrance = Layout.FindRoomByRole(ETDRoomRole::Entrance);
	const FTDPlacedRoom* Boss = Layout.FindRoomByRole(ETDRoomRole::Boss);
	CheckRequiredRooms(Layout, Report);
	CheckOverlap(Layout, Report);
	CheckDoorIntegrity(Layout, Report);
	CheckConnectivity(Layout, Adjacency, Entrance, Boss, Report);
	CheckRoomCount(Layout, Settings.RoomCountRange, Report);
	CheckDeadEndRatio(Layout, Adjacency, Settings.MaxDeadEndRatio, Report);
	CheckProgression(Layout, Adjacency, Entrance, Boss, Report);
	TSet<FName> FailedCodes;
	for (const FTDValidationItem& Item : Report.Items)
	{
		if (Item.Severity == ETDValidationSeverity::Error)
		{
			FailedCodes.Add(Item.Code);
		}
	}
	Report.Score = 100.0f * static_cast<float>(CheckCount - FailedCodes.Num()) / CheckCount;
	Report.bPassed = !Report.HasErrors();
	return Report;
}
