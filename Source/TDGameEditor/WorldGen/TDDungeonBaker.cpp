#include "TDDungeonBaker.h"

#include <initializer_list>

#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor/EditorEngine.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "ScopedTransaction.h"
#include "World/Generation/TDInstancedMeshActor.h"

#define LOCTEXT_NAMESPACE "TDDungeonBaker"

DEFINE_LOG_CATEGORY_STATIC(LogTDDungeonBaker, Log, All);

namespace
{
	const TCHAR* MeshRoot = TEXT("/Game/DarkFantasyTopDown/StaticMeshes");
	const TCHAR* CubeMeshPath = TEXT("/Engine/BasicShapes/Cube");
	const TCHAR* FloorMaterialPath = TEXT("/Game/DarkFantasyTopDown/Materials/Nature/Surfaces/MI_Bedrock2");
	const TCHAR* WallMaterialPath = TEXT("/Game/DarkFantasyTopDown/Materials/Nature/MI_Cliffs2");
	constexpr float WallHeightCm = 350.0f;
	constexpr float WallThicknessCm = 30.0f;
	const FName TagGen(TEXT("TDGen"));
	const FName TagGenDungeon(TEXT("TDGenDungeon"));

	struct FPropPlacement
	{
		const TCHAR* MeshSubPath;
		FVector2D OffsetCm;
	};

	const FPropPlacement CombatProps[] = {
		{ TEXT("Barrels/SM_Barrels1"), FVector2D(120, 120) },
		{ TEXT("Containers/SM_WoodenCrate"), FVector2D(-130, 110) },
		{ TEXT("SmallProps/SM_Skull"), FVector2D(60, -120) } };
	const FPropPlacement EliteProps[] = {
		{ TEXT("WoodenParts/SM_Armory"), FVector2D(0, 140) },
		{ TEXT("Weapons/SM_Shield"), FVector2D(-120, -120) },
		{ TEXT("SmallProps/SM_StickedSkull"), FVector2D(130, -100) } };
	const FPropPlacement TreasureProps[] = {
		{ TEXT("Containers/SM_WoodenChest"), FVector2D(0, 0) },
		{ TEXT("Lanterns/SM_CandlesGroup"), FVector2D(90, 70) },
		{ TEXT("Containers/SM_WoodenChestEmpty"), FVector2D(-110, 90) } };
	const FPropPlacement BossProps[] = {
		{ TEXT("Props2/SM_Cauldron"), FVector2D(0, 0) },
		{ TEXT("SmallProps/SM_RamSkull2"), FVector2D(0, -140) },
		{ TEXT("SmallProps/SM_Bone"), FVector2D(120, 100) },
		{ TEXT("SmallProps/SM_Skull"), FVector2D(-130, 60) } };
	const FPropPlacement DeadEndProps[] = {
		{ TEXT("SmallProps/SM_Bone"), FVector2D(40, 40) },
		{ TEXT("SmallProps/SM_Skull"), FVector2D(-60, 30) } };
	const FPropPlacement StartProps[] = {
		{ TEXT("Lanterns/SM_Lamppost"), FVector2D(150, 150) } };

	enum class ETDBakeRoomKind : uint8
	{
		None,
		Boss,
		Start,
		Treasure,
		Elite,
		Combat,
		DeadEnd
	};

	ETDBakeRoomKind ClassifyRoom(const FTDPlacedRoom& Room)
	{
		if (Room.HasRole(ETDRoomRole::Boss)) return ETDBakeRoomKind::Boss;
		if (Room.HasRole(ETDRoomRole::Entrance)) return ETDBakeRoomKind::Start;
		if (Room.HasRole(ETDRoomRole::Treasure)) return ETDBakeRoomKind::Treasure;
		if (Room.HasRole(ETDRoomRole::Elite)) return ETDBakeRoomKind::Elite;
		if (Room.HasRole(ETDRoomRole::Combat) || Room.HasRole(ETDRoomRole::Hub)) return ETDBakeRoomKind::Combat;
		if (Room.HasRole(ETDRoomRole::DeadEnd)) return ETDBakeRoomKind::DeadEnd;
		return ETDBakeRoomKind::None;
	}

	TArrayView<const FPropPlacement> PropsForKind(ETDBakeRoomKind Kind)
	{
		switch (Kind)
		{
		case ETDBakeRoomKind::Boss: return BossProps;
		case ETDBakeRoomKind::Start: return StartProps;
		case ETDBakeRoomKind::Treasure: return TreasureProps;
		case ETDBakeRoomKind::Elite: return EliteProps;
		case ETDBakeRoomKind::Combat: return CombatProps;
		case ETDBakeRoomKind::DeadEnd: return DeadEndProps;
		default: return TArrayView<const FPropPlacement>();
		}
	}

	const TCHAR* KindName(ETDBakeRoomKind Kind)
	{
		switch (Kind)
		{
		case ETDBakeRoomKind::Boss: return TEXT("boss");
		case ETDBakeRoomKind::Start: return TEXT("start");
		case ETDBakeRoomKind::Treasure: return TEXT("treasure");
		case ETDBakeRoomKind::Elite: return TEXT("elite");
		case ETDBakeRoomKind::Combat: return TEXT("combat");
		case ETDBakeRoomKind::DeadEnd: return TEXT("deadend");
		default: return TEXT("none");
		}
	}

	template <typename T>
	T* LoadAssetByPackagePath(const FString& PackagePath)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, *FPackageName::GetShortName(PackagePath));
		T* Asset = LoadObject<T>(nullptr, *ObjectPath);
		if (!Asset)
		{
			UE_LOG(LogTDDungeonBaker, Warning, TEXT("Asset not found: %s"), *ObjectPath);
		}
		return Asset;
	}

	UStaticMesh* LoadMesh(const TCHAR* SubPath)
	{
		return LoadAssetByPackagePath<UStaticMesh>(FString::Printf(TEXT("%s/%s"), MeshRoot, SubPath));
	}

	struct FTDDungeonBakeContext
	{
		UWorld* World = nullptr;
		FVector Origin = FVector::ZeroVector;
		float Cell = 400.0f;
		FString Prefix;
		FName Folder;
		int32 PropCount = 0;
		int32 LightCount = 0;

		void FinishActor(AActor* Actor, const FString& Label, std::initializer_list<FName> ExtraTags) const
		{
			FActorLabelUtilities::SetActorLabelUnique(Actor, Label);
			Actor->SetFolderPath(Folder);
			Actor->Tags.Add(TagGen);
			for (const FName& Tag : ExtraTags)
			{
				Actor->Tags.AddUnique(Tag);
			}
		}

		FVector CellCenter(const FIntPoint& CellCoord) const
		{
			return Origin + FVector((CellCoord.X + 0.5f) * Cell, (CellCoord.Y + 0.5f) * Cell, 0.0f);
		}

		FVector RoomCenter(const FTDPlacedRoom& Room) const
		{
			if (Room.Cells.Num() == 0)
			{
				return Origin;
			}
			FVector Sum = FVector::ZeroVector;
			for (const FIntPoint& CellCoord : Room.Cells)
			{
				Sum += FVector(CellCoord.X, CellCoord.Y, 0.0f);
			}
			const FVector Mean = Sum / Room.Cells.Num();
			return Origin + FVector((Mean.X + 0.5f) * Cell, (Mean.Y + 0.5f) * Cell, 0.0f);
		}

		AActor* SpawnInstancedMesh(const TCHAR* MeshPackagePath, const TCHAR* MaterialPackagePath, const TArray<FTransform>& Transforms, const FString& Label) const
		{
			if (Transforms.Num() == 0)
			{
				return nullptr;
			}
			UStaticMesh* Mesh = LoadAssetByPackagePath<UStaticMesh>(MeshPackagePath);
			if (!Mesh)
			{
				return nullptr;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATDInstancedMeshActor* Actor = World->SpawnActor<ATDInstancedMeshActor>(Transforms[0].GetLocation(), FRotator::ZeroRotator, Params);
			if (!Actor)
			{
				return nullptr;
			}
			UHierarchicalInstancedStaticMeshComponent* Component = Actor->InstancedMeshComponent;
			Component->SetStaticMesh(Mesh);
			if (UMaterialInterface* Material = LoadAssetByPackagePath<UMaterialInterface>(MaterialPackagePath))
			{
				Component->SetMaterial(0, Material);
			}
			Component->AddInstances(Transforms, false, true);
			FinishActor(Actor, Label, { TagGenDungeon });
			return Actor;
		}

		AStaticMeshActor* SpawnMesh(const TCHAR* MeshSubPath, const FVector& Location, float Yaw, const FString& Label, float Scale = 1.0f)
		{
			UStaticMesh* Mesh = LoadMesh(MeshSubPath);
			if (!Mesh)
			{
				return nullptr;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator(0.0f, Yaw, 0.0f), Params);
			if (!Actor)
			{
				return nullptr;
			}
			UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
			Component->UnregisterComponent();
			Component->SetStaticMesh(Mesh);
			Component->SetMobility(EComponentMobility::Static);
			Component->RegisterComponent();
			Actor->SetActorScale3D(FVector(Scale));
			FinishActor(Actor, Label, { TagGenDungeon });
			++PropCount;
			return Actor;
		}

		APointLight* SpawnLight(const FVector& Location, float IntensityCandela, const FLinearColor& Color, float RadiusCm, const FString& Label, float Volumetric = 2.0f)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APointLight* Actor = World->SpawnActor<APointLight>(Location, FRotator::ZeroRotator, Params);
			if (!Actor)
			{
				return nullptr;
			}
			UPointLightComponent* Component = Actor->PointLightComponent;
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetIntensityUnits(ELightUnits::Candelas);
			Component->SetIntensity(IntensityCandela);
			Component->SetLightColor(Color);
			Component->SetAttenuationRadius(RadiusCm);
			Component->SetSourceRadius(10.0f);
			Component->SetVolumetricScatteringIntensity(Volumetric);
			Component->SetCastShadows(false);
			FinishActor(Actor, Label, { TagGenDungeon });
			++LightCount;
			return Actor;
		}

		ATargetPoint* SpawnTargetPoint(const FVector& Location, float Yaw, const FString& Label, std::initializer_list<FName> ExtraTags) const
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATargetPoint* Actor = World->SpawnActor<ATargetPoint>(Location, FRotator(0.0f, Yaw, 0.0f), Params);
			if (!Actor)
			{
				return nullptr;
			}
			FinishActor(Actor, Label, ExtraTags);
			return Actor;
		}
	};

	const FIntPoint NeighborOffsets[4] = { FIntPoint(0, -1), FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(-1, 0) };

	void BuildFloorAndWalls(const FTDDungeonBakeContext& Context, const FTDDungeonLayout& Layout, int32& OutFloorCount, int32& OutWallCount)
	{
		TMap<FIntPoint, FName> RoomOfCell;
		for (const FTDPlacedRoom& Room : Layout.Rooms)
		{
			for (const FIntPoint& CellCoord : Room.Cells)
			{
				RoomOfCell.Add(CellCoord, Room.RoomId);
			}
		}
		TSet<TPair<FIntPoint, FIntPoint>> DoorEdges;
		for (const FTDPlacedDoor& Door : Layout.Doors)
		{
			DoorEdges.Add(TPair<FIntPoint, FIntPoint>(Door.CellA, Door.CellB));
			DoorEdges.Add(TPair<FIntPoint, FIntPoint>(Door.CellB, Door.CellA));
		}

		TArray<FTransform> FloorTransforms;
		TArray<FTransform> WallTransforms;
		const float CellMeters = Context.Cell / 100.0f;
		for (const TPair<FIntPoint, FName>& Entry : RoomOfCell)
		{
			const FIntPoint& CellCoord = Entry.Key;
			const FVector Center = Context.CellCenter(CellCoord);
			FloorTransforms.Add(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, Context.Origin.Z - 10.0f), FVector(CellMeters, CellMeters, 0.2f)));
			for (const FIntPoint& Offset : NeighborOffsets)
			{
				const FIntPoint Neighbor = CellCoord + Offset;
				const FName* NeighborRoom = RoomOfCell.Find(Neighbor);
				if (NeighborRoom && *NeighborRoom == Entry.Value)
				{
					continue;
				}
				if (DoorEdges.Contains(TPair<FIntPoint, FIntPoint>(CellCoord, Neighbor)))
				{
					continue;
				}
				const float Yaw = Offset.X != 0 ? 90.0f : 0.0f;
				const FVector WallLocation(Center.X + Offset.X * Context.Cell * 0.5f, Center.Y + Offset.Y * Context.Cell * 0.5f, Context.Origin.Z + WallHeightCm * 0.5f);
				WallTransforms.Add(FTransform(FRotator(0.0f, Yaw, 0.0f), WallLocation, FVector(CellMeters + WallThicknessCm / 100.0f, WallThicknessCm / 100.0f, WallHeightCm / 100.0f)));
			}
		}
		Context.SpawnInstancedMesh(CubeMeshPath, FloorMaterialPath, FloorTransforms, Context.Prefix + TEXT("Floor"));
		Context.SpawnInstancedMesh(CubeMeshPath, WallMaterialPath, WallTransforms, Context.Prefix + TEXT("Walls"));
		OutFloorCount = FloorTransforms.Num();
		OutWallCount = WallTransforms.Num();
	}

	void BuildRoomDressing(FTDDungeonBakeContext& Context, const FTDDungeonLayout& Layout)
	{
		for (int32 RoomIndex = 0; RoomIndex < Layout.Rooms.Num(); ++RoomIndex)
		{
			const FTDPlacedRoom& Room = Layout.Rooms[RoomIndex];
			const FString RoomName = Room.RoomId.ToString();
			const FVector Center = Context.RoomCenter(Room);
			const float FloorZ = Context.Origin.Z;
			const ETDBakeRoomKind Kind = ClassifyRoom(Room);
			const TArrayView<const FPropPlacement> Props = PropsForKind(Kind);
			for (int32 PropIndex = 0; PropIndex < Props.Num(); ++PropIndex)
			{
				const FPropPlacement& Prop = Props[PropIndex];
				const FString Label = FString::Printf(TEXT("%s%s_%s_%d"), *Context.Prefix, *RoomName, KindName(Kind), PropIndex);
				Context.SpawnMesh(Prop.MeshSubPath, FVector(Center.X + Prop.OffsetCm.X, Center.Y + Prop.OffsetCm.Y, FloorZ), static_cast<float>((PropIndex * 73) % 360), Label);
			}

			const FString LightLabel = FString::Printf(TEXT("%s%s_Light"), *Context.Prefix, *RoomName);
			if (Kind == ETDBakeRoomKind::Boss)
			{
				Context.SpawnLight(FVector(Center.X, Center.Y, FloorZ + 180.0f), 60.0f, FLinearColor(1.0f, 0.25f, 0.12f), 1400.0f, LightLabel, 6.0f);
			}
			else if (Kind == ETDBakeRoomKind::Treasure || Kind == ETDBakeRoomKind::Elite || Kind == ETDBakeRoomKind::Start)
			{
				Context.SpawnLight(FVector(Center.X, Center.Y, FloorZ + 220.0f), 25.0f, FLinearColor(1.0f, 0.72f, 0.42f), 1000.0f, LightLabel);
			}
			else if (Room.HasRole(ETDRoomRole::Corridor) && RoomIndex % 3 == 0)
			{
				Context.SpawnMesh(TEXT("Lanterns/SM_Lamp"), FVector(Center.X, Center.Y + Context.Cell * 0.5f - 40.0f, FloorZ + 200.0f), 0.0f, FString::Printf(TEXT("%s%s_Lamp"), *Context.Prefix, *RoomName));
				Context.SpawnLight(FVector(Center.X, Center.Y + Context.Cell * 0.5f - 60.0f, FloorZ + 210.0f), 14.0f, FLinearColor(1.0f, 0.68f, 0.38f), 800.0f, LightLabel, 1.5f);
			}

			const bool bIsKeyRoom = !Room.HeldKeyId.IsNone() || Room.HasRole(ETDRoomRole::Key);
			if (bIsKeyRoom)
			{
				Context.SpawnMesh(TEXT("Containers/SM_WoodenChest"), FVector(Center.X, Center.Y, FloorZ), 0.0f, FString::Printf(TEXT("%s%s_KeyChest"), *Context.Prefix, *RoomName));
				Context.SpawnLight(FVector(Center.X, Center.Y, FloorZ + 120.0f), 20.0f, FLinearColor(0.9f, 0.85f, 0.4f), 700.0f, FString::Printf(TEXT("%s%s_KeyLight"), *Context.Prefix, *RoomName), 3.0f);
			}
		}
	}

	void BuildLockedDoors(FTDDungeonBakeContext& Context, const FTDDungeonLayout& Layout)
	{
		int32 LockIndex = 0;
		for (const FTDPlacedDoor& Door : Layout.Doors)
		{
			if (!Door.IsLocked())
			{
				continue;
			}
			const FVector Mid = Context.Origin + FVector(((Door.CellA.X + Door.CellB.X) * 0.5f + 0.5f) * Context.Cell, ((Door.CellA.Y + Door.CellB.Y) * 0.5f + 0.5f) * Context.Cell, 0.0f);
			const bool bAlongX = Door.CellA.Y == Door.CellB.Y;
			const float Yaw = bAlongX ? 0.0f : 90.0f;
			const float PanelOffsets[2] = { -100.0f, 100.0f };
			for (int32 PanelIndex = 0; PanelIndex < 2; ++PanelIndex)
			{
				const float Offset = PanelOffsets[PanelIndex];
				const FVector PanelLocation(Mid.X + (bAlongX ? 0.0f : Offset), Mid.Y + (bAlongX ? Offset : 0.0f), Context.Origin.Z);
				Context.SpawnMesh(TEXT("WoodenParts/SM_WoodenConstruction7"), PanelLocation, Yaw, FString::Printf(TEXT("%sLock%d_Panel%d"), *Context.Prefix, LockIndex, PanelIndex));
			}
			Context.SpawnMesh(TEXT("SmallProps/SM_RamSkull2"), FVector(Mid.X, Mid.Y, Context.Origin.Z + 260.0f), Yaw, FString::Printf(TEXT("%sLock%d_Skull"), *Context.Prefix, LockIndex), 1.6f);
			Context.SpawnLight(FVector(Mid.X, Mid.Y, Context.Origin.Z + 240.0f), 18.0f, FLinearColor(1.0f, 0.2f, 0.1f), 600.0f, FString::Printf(TEXT("%sLock%d_Light"), *Context.Prefix, LockIndex), 4.0f);
			++LockIndex;
		}
	}

	void BuildMarkers(const FTDDungeonBakeContext& Context, const FTDDungeonLayout& Layout, int32 SlotIndex, const FBox& WorldBounds)
	{
		const FName ThemeTag = Layout.ThemeId;
		const FName FlowTag(TDDungeon::FlowName(Layout.Flow));
		const FVector EntryLocation = Context.Origin + Layout.EntryTransform.GetLocation() + FVector(0.0f, 0.0f, 50.0f);
		Context.SpawnTargetPoint(EntryLocation, Layout.EntryTransform.Rotator().Yaw, FString::Printf(TEXT("TDDungeonEntry_%d"), SlotIndex), { FName(TEXT("TDDungeonEntry")), ThemeTag, FlowTag });
		const FVector ExitLocation = Context.Origin + Layout.ExitTransform.GetLocation() + FVector(0.0f, 0.0f, 50.0f);
		Context.SpawnTargetPoint(ExitLocation, Layout.ExitTransform.Rotator().Yaw, FString::Printf(TEXT("TDDungeonExit_%d"), SlotIndex), { FName(TEXT("TDDungeonExit")) });

		const FVector SlotCenter(WorldBounds.GetCenter().X, WorldBounds.GetCenter().Y, Context.Origin.Z + 100.0f);
		const FName SeedTag(*FString::Printf(TEXT("seed%d"), Layout.Seed));
		Context.SpawnTargetPoint(SlotCenter, 0.0f, FString::Printf(TEXT("TDDungeonSlot_%d"), SlotIndex), { FName(TEXT("TDDungeonSlot")), ThemeTag, FlowTag, SeedTag });

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANavMeshBoundsVolume* NavVolume = Context.World->SpawnActor<ANavMeshBoundsVolume>(SlotCenter, FRotator::ZeroRotator, Params);
		if (!NavVolume)
		{
			return;
		}
		UCubeBuilder* CubeBuilder = NewObject<UCubeBuilder>();
		CubeBuilder->X = 200.0f;
		CubeBuilder->Y = 200.0f;
		CubeBuilder->Z = 200.0f;
		UActorFactory::CreateBrushForVolumeActor(NavVolume, CubeBuilder);
		const FVector SizeCm = WorldBounds.GetSize();
		NavVolume->SetActorScale3D(FVector(SizeCm.X / 200.0f + 4.0f, SizeCm.Y / 200.0f + 4.0f, 6.0f));
		Context.FinishActor(NavVolume, Context.Prefix + TEXT("NavBounds"), {});
	}
}

FString FTDDungeonBaker::MakeLabelPrefix(int32 SlotIndex)
{
	return FString::Printf(TEXT("TDGenDungeon_%d_"), SlotIndex);
}

FString FTDDungeonBaker::MakeFolderPath(int32 SlotIndex)
{
	return FString::Printf(TEXT("TDGen/Dungeon/Slot%d"), SlotIndex);
}

FBox FTDDungeonBaker::ComputeWorldBounds(const FTDDungeonLayout& Layout, const FVector& SlotOriginCm)
{
	const float Cell = static_cast<float>(Layout.CellSizeCm);
	const FVector Min = SlotOriginCm + FVector(Layout.MinCell.X * Cell, Layout.MinCell.Y * Cell, -10.0f);
	const FVector Max = SlotOriginCm + FVector((Layout.MaxCell.X + 1) * Cell, (Layout.MaxCell.Y + 1) * Cell, WallHeightCm);
	return FBox(Min, Max);
}

int32 FTDDungeonBaker::ClearSlot(UWorld* World, int32 SlotIndex)
{
	if (!World)
	{
		return 0;
	}
	const FString Prefix = MakeLabelPrefix(SlotIndex);
	const TArray<FString> MarkerLabels = {
		FString::Printf(TEXT("TDDungeonEntry_%d"), SlotIndex),
		FString::Printf(TEXT("TDDungeonExit_%d"), SlotIndex),
		FString::Printf(TEXT("TDDungeonSlot_%d"), SlotIndex) };
	TArray<AActor*> ToRemove;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const FString Label = It->GetActorLabel();
		if (Label.StartsWith(Prefix))
		{
			ToRemove.Add(*It);
			continue;
		}
		for (const FString& MarkerLabel : MarkerLabels)
		{
			if (Label == MarkerLabel || Label.StartsWith(MarkerLabel + TEXT("_")))
			{
				ToRemove.Add(*It);
				break;
			}
		}
	}
	for (AActor* Actor : ToRemove)
	{
		World->EditorDestroyActor(Actor, true);
	}
	return ToRemove.Num();
}

bool FTDDungeonBaker::Bake(UWorld* World, const FTDDungeonLayout& Layout, const FVector& SlotOriginCm, FName DungeonId, int32 SlotIndex, bool bClearExisting, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("World is null");
		return false;
	}
	if (Layout.Rooms.Num() == 0)
	{
		OutError = TEXT("Layout has no rooms");
		return false;
	}
	if (Layout.CellSizeCm <= 0)
	{
		OutError = TEXT("Layout cell size must be positive");
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("BakeDungeon", "TD Bake Dungeon Slot"));
	const int32 RemovedCount = bClearExisting ? ClearSlot(World, SlotIndex) : 0;

	FTDDungeonBakeContext Context;
	Context.World = World;
	Context.Origin = SlotOriginCm;
	Context.Cell = static_cast<float>(Layout.CellSizeCm);
	Context.Prefix = MakeLabelPrefix(SlotIndex);
	Context.Folder = FName(*MakeFolderPath(SlotIndex));

	int32 FloorCount = 0;
	int32 WallCount = 0;
	BuildFloorAndWalls(Context, Layout, FloorCount, WallCount);
	BuildRoomDressing(Context, Layout);
	BuildLockedDoors(Context, Layout);
	const FBox WorldBounds = ComputeWorldBounds(Layout, SlotOriginCm);
	BuildMarkers(Context, Layout, SlotIndex, WorldBounds);

	UE_LOG(LogTDDungeonBaker, Log, TEXT("Slot %d (%s): removed %d, floor %d, walls %d, props %d, lights %d, rooms %d, flow %s, seed %d"),
		SlotIndex, *DungeonId.ToString(), RemovedCount, FloorCount, WallCount, Context.PropCount, Context.LightCount, Layout.Rooms.Num(), TDDungeon::FlowName(Layout.Flow), Layout.Seed);
	return true;
}

#undef LOCTEXT_NAMESPACE
