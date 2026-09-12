#pragma once

#include "CoreMinimal.h"
#include "TDWorldGenTypes.h"
#include "TDDungeonTypes.generated.h"

UENUM(BlueprintType)
enum class ETDDoorDirection : uint8
{
	North UMETA(DisplayName = "North (-Y)"),
	East UMETA(DisplayName = "East (+X)"),
	South UMETA(DisplayName = "South (+Y)"),
	West UMETA(DisplayName = "West (-X)")
};

UENUM(BlueprintType)
enum class ETDRoomRole : uint8
{
	Entrance,
	Corridor,
	Combat,
	Hub,
	Elite,
	Treasure,
	Key,
	DeadEnd,
	Boss
};

UENUM(BlueprintType)
enum class ETDDungeonFlowKind : uint8
{
	Linear,
	Branch,
	Loop,
	Hub,
	KeyLock
};

UENUM(BlueprintType)
enum class ETDDungeonSize : uint8
{
	Small,
	Medium,
	Large
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDDoorSocket
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FIntPoint Cell = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ETDDoorDirection Direction = ETDDoorDirection::North;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Tag = TEXT("Normal");
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDRoomModuleDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ModuleId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FIntPoint> Cells;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FTDDoorSocket> Sockets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<ETDRoomRole> Roles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAllowRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UWorld> LevelAsset;

	bool HasRole(ETDRoomRole Role) const { return Roles.Contains(Role); }
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDFlowNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly)
	ETDRoomRole Role = ETDRoomRole::Combat;

	UPROPERTY(BlueprintReadOnly)
	int32 Depth = 0;

	UPROPERTY(BlueprintReadOnly)
	FName HeldKeyId;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDFlowEdge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 From = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 To = 0;

	UPROPERTY(BlueprintReadOnly)
	FName LockId;

	UPROPERTY(BlueprintReadOnly)
	bool bIsLoop = false;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDDungeonFlowGraph
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDFlowNode> Nodes;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDFlowEdge> Edges;

	UPROPERTY(BlueprintReadOnly)
	int32 StartNode = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 BossNode = 0;

	uint32 ComputeHash() const;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDPlacedDoor
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FIntPoint CellA = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly)
	FIntPoint CellB = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly)
	ETDDoorDirection DirectionFromA = ETDDoorDirection::North;

	UPROPERTY(BlueprintReadOnly)
	FName RoomA;

	UPROPERTY(BlueprintReadOnly)
	FName RoomB;

	UPROPERTY(BlueprintReadOnly)
	FName LockId;

	bool IsLocked() const { return !LockId.IsNone(); }
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDPlacedRoom
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName RoomId;

	UPROPERTY(BlueprintReadOnly)
	int32 FlowNode = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	FName ModuleId;

	UPROPERTY(BlueprintReadOnly)
	int32 Rotation = 0;

	UPROPERTY(BlueprintReadOnly)
	FIntPoint CellOrigin = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly)
	TArray<FIntPoint> Cells;

	UPROPERTY(BlueprintReadOnly)
	TArray<ETDRoomRole> Roles;

	UPROPERTY(BlueprintReadOnly)
	FName HeldKeyId;

	bool HasRole(ETDRoomRole Role) const { return Roles.Contains(Role); }
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDDungeonLayout
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Seed = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 GeneratorVersion = TD_WORLDGEN_VERSION;

	UPROPERTY(BlueprintReadOnly)
	FName ThemeId;

	UPROPERTY(BlueprintReadOnly)
	ETDDungeonFlowKind Flow = ETDDungeonFlowKind::Linear;

	UPROPERTY(BlueprintReadOnly)
	ETDDungeonSize Size = ETDDungeonSize::Medium;

	UPROPERTY(BlueprintReadOnly)
	int32 CellSizeCm = 400;

	UPROPERTY(BlueprintReadOnly)
	FTDDungeonFlowGraph Graph;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDPlacedRoom> Rooms;

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDPlacedDoor> Doors;

	UPROPERTY(BlueprintReadOnly)
	FIntPoint MinCell = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly)
	FIntPoint MaxCell = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly)
	FTransform EntryTransform;

	UPROPERTY(BlueprintReadOnly)
	FTransform ExitTransform;

	UPROPERTY(BlueprintReadOnly)
	int32 LayoutRestarts = 0;

	UPROPERTY(BlueprintReadOnly)
	FTDValidationReport Validation;

	const FTDPlacedRoom* FindRoom(FName RoomId) const;
	const FTDPlacedRoom* FindRoomByRole(ETDRoomRole Role) const;
	FVector CellCenterLocal(const FIntPoint& Cell) const;
	FIntPoint SizeCells() const { return FIntPoint(MaxCell.X - MinCell.X + 1, MaxCell.Y - MinCell.Y + 1); }
	uint32 ComputeHash() const;
};

namespace TDDungeon
{
	TDWORLDGEN_API FIntPoint DirectionOffset(ETDDoorDirection Direction);
	TDWORLDGEN_API ETDDoorDirection Opposite(ETDDoorDirection Direction);
	TDWORLDGEN_API ETDDoorDirection RotateDirection(ETDDoorDirection Direction, int32 QuarterTurns);
	TDWORLDGEN_API FIntPoint RotateCell(const FIntPoint& Cell, int32 QuarterTurns);
	TDWORLDGEN_API float DirectionYaw(ETDDoorDirection Direction);
	TDWORLDGEN_API FIntPoint RoomCountRange(ETDDungeonSize Size);
	TDWORLDGEN_API const TCHAR* RoleName(ETDRoomRole Role);
	TDWORLDGEN_API const TCHAR* FlowName(ETDDungeonFlowKind Flow);
	TDWORLDGEN_API const TCHAR* SizeName(ETDDungeonSize Size);
}
