#include "Dungeon/TDDungeonTypes.h"

namespace TDDungeon
{
	FIntPoint DirectionOffset(ETDDoorDirection Direction)
	{
		switch (Direction)
		{
		case ETDDoorDirection::North: return FIntPoint(0, -1);
		case ETDDoorDirection::East: return FIntPoint(1, 0);
		case ETDDoorDirection::South: return FIntPoint(0, 1);
		default: return FIntPoint(-1, 0);
		}
	}

	ETDDoorDirection Opposite(ETDDoorDirection Direction)
	{
		return RotateDirection(Direction, 2);
	}

	ETDDoorDirection RotateDirection(ETDDoorDirection Direction, int32 QuarterTurns)
	{
		const int32 Turns = ((QuarterTurns % 4) + 4) % 4;
		return static_cast<ETDDoorDirection>((static_cast<int32>(Direction) + Turns) % 4);
	}

	FIntPoint RotateCell(const FIntPoint& Cell, int32 QuarterTurns)
	{
		FIntPoint Result = Cell;
		const int32 Turns = ((QuarterTurns % 4) + 4) % 4;
		for (int32 Step = 0; Step < Turns; ++Step)
		{
			Result = FIntPoint(-Result.Y, Result.X);
		}
		return Result;
	}

	float DirectionYaw(ETDDoorDirection Direction)
	{
		switch (Direction)
		{
		case ETDDoorDirection::North: return -90.0f;
		case ETDDoorDirection::East: return 0.0f;
		case ETDDoorDirection::South: return 90.0f;
		default: return 180.0f;
		}
	}

	FIntPoint RoomCountRange(ETDDungeonSize Size)
	{
		switch (Size)
		{
		case ETDDungeonSize::Small: return FIntPoint(6, 9);
		case ETDDungeonSize::Large: return FIntPoint(16, 24);
		default: return FIntPoint(10, 15);
		}
	}

	const TCHAR* RoleName(ETDRoomRole Role)
	{
		switch (Role)
		{
		case ETDRoomRole::Entrance: return TEXT("start");
		case ETDRoomRole::Corridor: return TEXT("corridor");
		case ETDRoomRole::Combat: return TEXT("combat");
		case ETDRoomRole::Hub: return TEXT("hub");
		case ETDRoomRole::Elite: return TEXT("elite");
		case ETDRoomRole::Treasure: return TEXT("treasure");
		case ETDRoomRole::Key: return TEXT("key");
		case ETDRoomRole::DeadEnd: return TEXT("deadend");
		default: return TEXT("boss");
		}
	}

	const TCHAR* FlowName(ETDDungeonFlowKind Flow)
	{
		switch (Flow)
		{
		case ETDDungeonFlowKind::Linear: return TEXT("Linear");
		case ETDDungeonFlowKind::Branch: return TEXT("Branch");
		case ETDDungeonFlowKind::Loop: return TEXT("Loop");
		case ETDDungeonFlowKind::Hub: return TEXT("Hub");
		default: return TEXT("KeyLock");
		}
	}

	const TCHAR* SizeName(ETDDungeonSize Size)
	{
		switch (Size)
		{
		case ETDDungeonSize::Small: return TEXT("Small");
		case ETDDungeonSize::Large: return TEXT("Large");
		default: return TEXT("Medium");
		}
	}
}

uint32 FTDDungeonFlowGraph::ComputeHash() const
{
	uint32 Hash = HashCombine(static_cast<uint32>(StartNode), static_cast<uint32>(BossNode));
	for (const FTDFlowNode& Node : Nodes)
	{
		Hash = HashCombine(Hash, HashCombine(static_cast<uint32>(Node.Index), static_cast<uint32>(Node.Role)));
		Hash = HashCombine(Hash, GetTypeHash(Node.HeldKeyId));
	}
	for (const FTDFlowEdge& Edge : Edges)
	{
		Hash = HashCombine(Hash, HashCombine(static_cast<uint32>(Edge.From), static_cast<uint32>(Edge.To)));
		Hash = HashCombine(Hash, HashCombine(GetTypeHash(Edge.LockId), Edge.bIsLoop ? 1u : 0u));
	}
	return Hash;
}

const FTDPlacedRoom* FTDDungeonLayout::FindRoom(FName RoomId) const
{
	return Rooms.FindByPredicate([RoomId](const FTDPlacedRoom& Room) { return Room.RoomId == RoomId; });
}

const FTDPlacedRoom* FTDDungeonLayout::FindRoomByRole(ETDRoomRole Role) const
{
	return Rooms.FindByPredicate([Role](const FTDPlacedRoom& Room) { return Room.HasRole(Role); });
}

FVector FTDDungeonLayout::CellCenterLocal(const FIntPoint& Cell) const
{
	return FVector((Cell.X + 0.5f) * CellSizeCm, (Cell.Y + 0.5f) * CellSizeCm, 0.0f);
}

uint32 FTDDungeonLayout::ComputeHash() const
{
	uint32 Hash = HashCombine(static_cast<uint32>(Seed), Graph.ComputeHash());
	for (const FTDPlacedRoom& Room : Rooms)
	{
		Hash = HashCombine(Hash, GetTypeHash(Room.ModuleId));
		Hash = HashCombine(Hash, HashCombine(static_cast<uint32>(Room.Rotation), GetTypeHash(Room.CellOrigin)));
	}
	for (const FTDPlacedDoor& Door : Doors)
	{
		Hash = HashCombine(Hash, HashCombine(GetTypeHash(Door.CellA), GetTypeHash(Door.CellB)));
		Hash = HashCombine(Hash, GetTypeHash(Door.LockId));
	}
	return Hash;
}
