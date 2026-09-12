#include "Dungeon/TDDungeonGeneration.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* DirectionLetter(ETDDoorDirection Direction)
	{
		switch (Direction)
		{
		case ETDDoorDirection::North: return TEXT("N");
		case ETDDoorDirection::East: return TEXT("E");
		case ETDDoorDirection::South: return TEXT("S");
		default: return TEXT("W");
		}
	}

	TSharedPtr<FJsonValue> CellValue(const FIntPoint& Cell)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueNumber>(Cell.X));
		Values.Add(MakeShared<FJsonValueNumber>(Cell.Y));
		return MakeShared<FJsonValueArray>(Values);
	}

	TSharedPtr<FJsonValue> VectorValue(const FVector& Vector)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
		Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
		Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
		return MakeShared<FJsonValueArray>(Values);
	}

	TSharedPtr<FJsonValue> NameOrNull(FName Name)
	{
		if (Name.IsNone())
		{
			return MakeShared<FJsonValueNull>();
		}
		return MakeShared<FJsonValueString>(Name.ToString());
	}

	TSharedPtr<FJsonObject> TransformObject(const FTransform& Transform, FName RoomId)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetField(TEXT("location_cm"), VectorValue(Transform.GetLocation()));
		Object->SetNumberField(TEXT("yaw"), Transform.Rotator().Yaw);
		Object->SetField(TEXT("room"), NameOrNull(RoomId));
		return Object;
	}

	TSharedPtr<FJsonObject> RoomDoorObject(const FTDPlacedDoor& Door, FName RoomId)
	{
		const bool bIsSideA = Door.RoomA == RoomId;
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetField(TEXT("cell"), CellValue(bIsSideA ? Door.CellA : Door.CellB));
		Object->SetStringField(TEXT("dir"), DirectionLetter(bIsSideA ? Door.DirectionFromA : TDDungeon::Opposite(Door.DirectionFromA)));
		Object->SetStringField(TEXT("connected_room"), (bIsSideA ? Door.RoomB : Door.RoomA).ToString());
		Object->SetBoolField(TEXT("locked"), Door.IsLocked());
		Object->SetField(TEXT("key_id"), NameOrNull(Door.LockId));
		return Object;
	}

	TSharedPtr<FJsonObject> RoomObject(const FTDDungeonLayout& Layout, const FTDPlacedRoom& Room)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Room.RoomId.ToString());
		Object->SetStringField(TEXT("module"), Room.ModuleId.ToString());
		Object->SetNumberField(TEXT("rotation"), Room.Rotation);
		Object->SetField(TEXT("cell_origin"), CellValue(Room.CellOrigin));
		TArray<TSharedPtr<FJsonValue>> Cells;
		for (const FIntPoint& Cell : Room.Cells)
		{
			Cells.Add(CellValue(Cell));
		}
		Object->SetArrayField(TEXT("cells"), Cells);
		TArray<TSharedPtr<FJsonValue>> Tags;
		for (const ETDRoomRole Role : Room.Roles)
		{
			Tags.Add(MakeShared<FJsonValueString>(TDDungeon::RoleName(Role)));
		}
		Object->SetArrayField(TEXT("tags"), Tags);
		if (Room.FlowNode == INDEX_NONE)
		{
			Object->SetField(TEXT("flow_node"), MakeShared<FJsonValueNull>());
		}
		else
		{
			Object->SetStringField(TEXT("flow_node"), FString::Printf(TEXT("n%d"), Room.FlowNode));
		}
		Object->SetField(TEXT("held_key_id"), NameOrNull(Room.HeldKeyId));
		TArray<TSharedPtr<FJsonValue>> Doors;
		for (const FTDPlacedDoor& Door : Layout.Doors)
		{
			if (Door.RoomA == Room.RoomId || Door.RoomB == Room.RoomId)
			{
				Doors.Add(MakeShared<FJsonValueObject>(RoomDoorObject(Door, Room.RoomId)));
			}
		}
		Object->SetArrayField(TEXT("doors"), Doors);
		return Object;
	}

	TSharedPtr<FJsonObject> DoorObject(const FTDPlacedDoor& Door)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("room_a"), Door.RoomA.ToString());
		Object->SetField(TEXT("cell_a"), CellValue(Door.CellA));
		Object->SetStringField(TEXT("dir_a"), DirectionLetter(Door.DirectionFromA));
		Object->SetStringField(TEXT("room_b"), Door.RoomB.ToString());
		Object->SetField(TEXT("cell_b"), CellValue(Door.CellB));
		Object->SetStringField(TEXT("dir_b"), DirectionLetter(TDDungeon::Opposite(Door.DirectionFromA)));
		Object->SetBoolField(TEXT("locked"), Door.IsLocked());
		Object->SetField(TEXT("key_id"), NameOrNull(Door.LockId));
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> KeysLocksArray(const FTDDungeonLayout& Layout)
	{
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (int32 DoorIndex = 0; DoorIndex < Layout.Doors.Num(); ++DoorIndex)
		{
			const FTDPlacedDoor& Door = Layout.Doors[DoorIndex];
			if (!Door.IsLocked())
			{
				continue;
			}
			const FTDPlacedRoom* KeyRoom = Layout.Rooms.FindByPredicate([&Door](const FTDPlacedRoom& Room) { return Room.HeldKeyId == Door.LockId; });
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("key_id"), Door.LockId.ToString());
			Entry->SetField(TEXT("key_room"), NameOrNull(KeyRoom ? KeyRoom->RoomId : NAME_None));
			Entry->SetNumberField(TEXT("lock_door"), DoorIndex);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Entries;
	}

	TSharedPtr<FJsonObject> ValidationObject(const FTDValidationReport& Report)
	{
		static const FName CheckOrder[] = {
			FName(TEXT("required_rooms")), FName(TEXT("overlap")), FName(TEXT("door_integrity")), FName(TEXT("connectivity")),
			FName(TEXT("room_count")), FName(TEXT("deadend_ratio")), FName(TEXT("progression_key_lock"))};
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(TEXT("passed"), Report.bPassed);
		Object->SetNumberField(TEXT("score"), Report.Score);
		TArray<TSharedPtr<FJsonValue>> Checks;
		for (const FName& Code : CheckOrder)
		{
			bool bFound = false;
			bool bPassed = true;
			FString Summary;
			TArray<TSharedPtr<FJsonValue>> Details;
			for (const FTDValidationItem& Item : Report.Items)
			{
				if (Item.Code != Code)
				{
					continue;
				}
				bFound = true;
				bPassed &= Item.Severity != ETDValidationSeverity::Error;
				Details.Add(MakeShared<FJsonValueString>(Item.Message));
				Summary = Item.Message;
			}
			if (!bFound)
			{
				continue;
			}
			Details.Pop();
			TSharedPtr<FJsonObject> Check = MakeShared<FJsonObject>();
			Check->SetStringField(TEXT("name"), Code.ToString());
			Check->SetBoolField(TEXT("passed"), bPassed);
			Check->SetStringField(TEXT("message"), Summary);
			Check->SetArrayField(TEXT("details"), Details);
			Checks.Add(MakeShared<FJsonValueObject>(Check));
		}
		Object->SetArrayField(TEXT("checks"), Checks);
		return Object;
	}
}

bool FTDDungeonGenerator::GenerateAndValidate(const UTDDungeonTheme& Theme, const UTDDungeonFlowTemplate& Template, ETDDungeonSize Size, int32 Seed, FTDDungeonLayout& OutLayout, FString& OutError)
{
	OutLayout = FTDDungeonLayout();
	const FTDSeedContext SeedContext(Seed);
	FTDDungeonFlowGraph Graph;
	if (!FTDDungeonFlowGenerator::Generate(Template, Size, SeedContext, Graph, OutError))
	{
		return false;
	}
	const FTDDungeonLayoutSolver::FSettings SolverSettings;
	if (!FTDDungeonLayoutSolver::Solve(Graph, Theme, SeedContext, SolverSettings, OutLayout, OutError))
	{
		return false;
	}
	OutLayout.Flow = Template.Kind;
	OutLayout.Size = Size;
	FTDDungeonValidator::FSettings ValidatorSettings;
	ValidatorSettings.RoomCountRange = TDDungeon::RoomCountRange(Size);
	ValidatorSettings.MaxDeadEndRatio = Template.MaxDeadEndRatio;
	OutLayout.Validation = FTDDungeonValidator::Validate(OutLayout, ValidatorSettings);
	if (OutLayout.Validation.bPassed)
	{
		return true;
	}
	for (const FTDValidationItem& Item : OutLayout.Validation.Items)
	{
		if (Item.Severity == ETDValidationSeverity::Error)
		{
			OutError = FString::Printf(TEXT("검증 실패 [%s] %s"), *Item.Code.ToString(), *Item.Message);
			break;
		}
	}
	return false;
}

FString FTDDungeonGenerator::ToJson(const FTDDungeonLayout& Layout, const FVector& WorldOffsetCm)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("seed"), Layout.Seed);
	Root->SetStringField(TEXT("theme"), Layout.ThemeId.ToString());
	Root->SetStringField(TEXT("flow"), TDDungeon::FlowName(Layout.Flow));
	Root->SetStringField(TEXT("size"), TDDungeon::SizeName(Layout.Size));
	Root->SetNumberField(TEXT("cell_size_cm"), Layout.CellSizeCm);
	Root->SetNumberField(TEXT("generator_version"), Layout.GeneratorVersion);
	Root->SetNumberField(TEXT("layout_restarts"), Layout.LayoutRestarts);
	TArray<TSharedPtr<FJsonValue>> Rooms;
	for (const FTDPlacedRoom& Room : Layout.Rooms)
	{
		Rooms.Add(MakeShared<FJsonValueObject>(RoomObject(Layout, Room)));
	}
	Root->SetArrayField(TEXT("rooms"), Rooms);
	TArray<TSharedPtr<FJsonValue>> Doors;
	for (const FTDPlacedDoor& Door : Layout.Doors)
	{
		Doors.Add(MakeShared<FJsonValueObject>(DoorObject(Door)));
	}
	Root->SetArrayField(TEXT("doors"), Doors);
	Root->SetArrayField(TEXT("keys_locks"), KeysLocksArray(Layout));
	TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
	const FIntPoint SizeCells = Layout.SizeCells();
	Bounds->SetField(TEXT("min_cell"), CellValue(Layout.MinCell));
	Bounds->SetField(TEXT("max_cell"), CellValue(Layout.MaxCell));
	Bounds->SetField(TEXT("size_cells"), CellValue(SizeCells));
	Bounds->SetField(TEXT("size_cm"), CellValue(SizeCells * Layout.CellSizeCm));
	Root->SetObjectField(TEXT("bounds"), Bounds);
	const FTDPlacedRoom* Entrance = Layout.FindRoomByRole(ETDRoomRole::Entrance);
	const FTDPlacedRoom* Boss = Layout.FindRoomByRole(ETDRoomRole::Boss);
	Root->SetObjectField(TEXT("entry_transform"), TransformObject(Layout.EntryTransform, Entrance ? Entrance->RoomId : NAME_None));
	Root->SetObjectField(TEXT("exit_transform"), TransformObject(Layout.ExitTransform, Boss ? Boss->RoomId : NAME_None));
	Root->SetField(TEXT("world_offset_cm"), VectorValue(WorldOffsetCm));
	Root->SetObjectField(TEXT("validation"), ValidationObject(Layout.Validation));
	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

bool FTDDungeonGenerator::WriteJsonFile(const FTDDungeonLayout& Layout, const FVector& WorldOffsetCm, const FString& FilePath)
{
	const FString Directory = FPaths::GetPath(FilePath);
	if (!Directory.IsEmpty() && !IFileManager::Get().MakeDirectory(*Directory, true))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(ToJson(Layout, WorldOffsetCm), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
