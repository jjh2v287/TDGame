#include "Dungeon/TDDungeonDefinitions.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	FTDRoomModuleDefinition MakeModule(const TCHAR* ModuleId, std::initializer_list<FIntPoint> Cells, std::initializer_list<TPair<FIntPoint, ETDDoorDirection>> Doors, std::initializer_list<ETDRoomRole> Roles)
	{
		FTDRoomModuleDefinition Module;
		Module.ModuleId = ModuleId;
		Module.Cells = Cells;
		for (const TPair<FIntPoint, ETDDoorDirection>& Door : Doors)
		{
			FTDDoorSocket& Socket = Module.Sockets.AddDefaulted_GetRef();
			Socket.Cell = Door.Key;
			Socket.Direction = Door.Value;
		}
		Module.Roles = Roles;
		return Module;
	}
}

const FTDRoomModuleDefinition* UTDDungeonTheme::FindModule(FName ModuleId) const
{
	return Modules.FindByPredicate([ModuleId](const FTDRoomModuleDefinition& Module) { return Module.ModuleId == ModuleId; });
}

void UTDDungeonTheme::CollectModulesWithRole(ETDRoomRole Role, TArray<const FTDRoomModuleDefinition*>& OutModules) const
{
	for (const FTDRoomModuleDefinition& Module : Modules)
	{
		if (Module.HasRole(Role))
		{
			OutModules.Add(&Module);
		}
	}
}

void UTDDungeonTheme::FillCryptPlaceholderModules()
{
	using D = ETDDoorDirection;
	using R = ETDRoomRole;
	const FIntPoint C00(0, 0), C10(1, 0), C20(2, 0), C01(0, 1), C11(1, 1), C21(2, 1);
	Modules.Reset();
	Modules.Add(MakeModule(TEXT("Entrance"), {C00}, {{C00, D::North}}, {R::Entrance}));
	Modules.Add(MakeModule(TEXT("Straight_A"), {C00}, {{C00, D::North}, {C00, D::South}}, {R::Corridor}));
	Modules.Add(MakeModule(TEXT("Straight_B"), {C00, C01}, {{C00, D::North}, {C01, D::South}}, {R::Corridor}));
	Modules.Add(MakeModule(TEXT("Corner_A"), {C00}, {{C00, D::North}, {C00, D::East}}, {R::Corridor}));
	Modules.Add(MakeModule(TEXT("Corner_B"), {C00, C10}, {{C00, D::South}, {C10, D::North}}, {R::Corridor}));
	Modules.Add(MakeModule(TEXT("T_Junction"), {C00}, {{C00, D::North}, {C00, D::East}, {C00, D::West}}, {R::Combat}));
	Modules.Add(MakeModule(TEXT("Large_A"), {C00, C10, C01, C11}, {{C00, D::North}, {C10, D::East}, {C11, D::South}, {C01, D::West}}, {R::Combat, R::Hub}));
	Modules.Add(MakeModule(TEXT("Large_B"), {C00, C10, C20, C01, C11, C21}, {{C10, D::North}, {C21, D::East}, {C11, D::South}, {C00, D::West}}, {R::Combat, R::Hub}));
	Modules.Add(MakeModule(TEXT("DeadEnd"), {C00}, {{C00, D::South}}, {R::DeadEnd}));
	Modules.Add(MakeModule(TEXT("Treasure"), {C00}, {{C00, D::South}}, {R::Treasure, R::Key}));
	Modules.Add(MakeModule(TEXT("Elite"), {C00, C10, C01, C11}, {{C01, D::South}, {C10, D::North}, {C11, D::East}}, {R::Elite}));
	Modules.Add(MakeModule(TEXT("Boss"), {C00, C10, C20, C01, C11, C21}, {{C11, D::South}}, {R::Boss}));
}

#if WITH_EDITOR
EDataValidationResult UTDDungeonTheme::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Modules.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("룸 모듈이 하나도 없습니다. FillCryptPlaceholderModules 또는 아트 모듈을 등록하세요.")));
		return EDataValidationResult::Invalid;
	}
	for (ETDRoomRole Required : {ETDRoomRole::Entrance, ETDRoomRole::Boss, ETDRoomRole::Corridor, ETDRoomRole::Combat})
	{
		TArray<const FTDRoomModuleDefinition*> Found;
		CollectModulesWithRole(Required, Found);
		if (Found.Num() == 0)
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("필수 역할 모듈 누락: %s"), TDDungeon::RoleName(Required))));
			Result = EDataValidationResult::Invalid;
		}
	}
	for (const FTDRoomModuleDefinition& Module : Modules)
	{
		if (Module.Cells.Num() == 0 || Module.Sockets.Num() == 0)
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("모듈 %s: 셀 또는 도어 소켓이 비어 있습니다"), *Module.ModuleId.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		for (const FTDDoorSocket& Socket : Module.Sockets)
		{
			if (!Module.Cells.Contains(Socket.Cell))
			{
				Context.AddError(FText::FromString(FString::Printf(TEXT("모듈 %s: 도어 소켓 셀 (%d,%d)이 모듈 셀에 없습니다"), *Module.ModuleId.ToString(), Socket.Cell.X, Socket.Cell.Y)));
				Result = EDataValidationResult::Invalid;
			}
		}
	}
	return Result;
}
#endif

void UTDDungeonFlowTemplate::ApplyKindDefaults(ETDDungeonFlowKind InKind)
{
	Kind = InKind;
	switch (InKind)
	{
	case ETDDungeonFlowKind::Linear:
		MaxBranches = 0; MaxLoops = 0; bRequireElite = true; bRequireTreasure = false; break;
	case ETDDungeonFlowKind::Branch:
		MaxBranches = 2; MaxLoops = 0; bRequireElite = true; bRequireTreasure = true; break;
	case ETDDungeonFlowKind::Loop:
		MaxBranches = 1; MaxLoops = 1; bRequireElite = true; bRequireTreasure = true; break;
	case ETDDungeonFlowKind::Hub:
		MaxBranches = 3; MaxLoops = 0; bRequireElite = true; bRequireTreasure = true; break;
	default:
		MaxBranches = 1; MaxLoops = 0; bRequireElite = true; bRequireTreasure = true; break;
	}
}

#if WITH_EDITOR
EDataValidationResult UTDDungeonFlowTemplate::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Kind == ETDDungeonFlowKind::Branch && MaxBranches < 1)
	{
		Context.AddWarning(FText::FromString(TEXT("Branch 흐름인데 MaxBranches가 0입니다")));
	}
	if (Kind == ETDDungeonFlowKind::Loop && MaxLoops < 1)
	{
		Context.AddWarning(FText::FromString(TEXT("Loop 흐름인데 MaxLoops가 0입니다")));
	}
	return Result;
}
#endif

FVector UTDDungeonAtlasDefinition::GetSlotOriginCm(int32 SlotIndex) const
{
	const int32 SafeColumns = FMath::Max(1, Columns);
	const int32 Column = SlotIndex % SafeColumns;
	const int32 Row = SlotIndex / SafeColumns;
	return OriginCm + FVector(Column * SlotPitchCm, Row * SlotPitchCm, 0.0);
}

bool UTDDungeonAtlasDefinition::FindSlot(FName DungeonId, FTDDungeonSlot& OutSlot) const
{
	if (const FTDDungeonSlot* Slot = FindSlotPtr(DungeonId))
	{
		OutSlot = *Slot;
		return true;
	}
	return false;
}

const FTDDungeonSlot* UTDDungeonAtlasDefinition::FindSlotPtr(FName DungeonId) const
{
	return Slots.FindByPredicate([DungeonId](const FTDDungeonSlot& Slot) { return Slot.DungeonId == DungeonId; });
}

FTDDungeonSlot* UTDDungeonAtlasDefinition::FindSlotPtr(FName DungeonId)
{
	return Slots.FindByPredicate([DungeonId](const FTDDungeonSlot& Slot) { return Slot.DungeonId == DungeonId; });
}

bool UTDDungeonAtlasDefinition::IsSlotSpacingSafe(FString& OutWarning) const
{
	const float Required = 2.0f * LoadingRangeCm;
	if (SlotPitchCm < Required)
	{
		OutWarning = FString::Printf(TEXT("슬롯 간격 %.0f cm < 로딩 범위 2배 %.0f cm"), SlotPitchCm, Required);
		return false;
	}
	for (int32 IndexA = 0; IndexA < Slots.Num(); ++IndexA)
	{
		for (int32 IndexB = IndexA + 1; IndexB < Slots.Num(); ++IndexB)
		{
			const double Distance = FVector::Dist2D(Slots[IndexA].WorldTransform.GetLocation(), Slots[IndexB].WorldTransform.GetLocation());
			const double SlotRadiusA = Slots[IndexA].Bounds.IsValid ? Slots[IndexA].Bounds.GetExtent().Size2D() : 0.0;
			const double SlotRadiusB = Slots[IndexB].Bounds.IsValid ? Slots[IndexB].Bounds.GetExtent().Size2D() : 0.0;
			const double Needed = 2.0 * LoadingRangeCm + SlotRadiusA + SlotRadiusB;
			if (Distance > 0.0 && Distance < Needed)
			{
				OutWarning = FString::Printf(TEXT("슬롯 %s와 %s 거리 %.0f cm < 필요 %.0f cm"), *Slots[IndexA].DungeonId.ToString(), *Slots[IndexB].DungeonId.ToString(), Distance, Needed);
				return false;
			}
		}
	}
	return true;
}

#if WITH_EDITOR
void UTDDungeonAtlasDefinition::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	FString Warning;
	if (!IsSlotSpacingSafe(Warning))
	{
		UE_LOG(LogTemp, Warning, TEXT("[TDDungeonAtlas] %s"), *Warning);
	}
}

EDataValidationResult UTDDungeonAtlasDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FString Warning;
	if (!IsSlotSpacingSafe(Warning))
	{
		Context.AddWarning(FText::FromString(Warning));
	}
	TSet<FName> Ids;
	for (const FTDDungeonSlot& Slot : Slots)
	{
		if (Slot.DungeonId.IsNone() || Ids.Contains(Slot.DungeonId))
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("슬롯 DungeonId가 비었거나 중복: %s"), *Slot.DungeonId.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		Ids.Add(Slot.DungeonId);
	}
	return Result;
}
#endif
