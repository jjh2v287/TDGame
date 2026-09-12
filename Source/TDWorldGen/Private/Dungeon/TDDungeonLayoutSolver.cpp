#include "Dungeon/TDDungeonGeneration.h"
#include "Algo/Reverse.h"

namespace
{
	constexpr int32 MaxLoopPathLength = 16;
	constexpr int32 LoopRegionMargin = 3;
	const float CorridorLengthWeights[] = {0.3f, 0.4f, 0.2f, 0.1f};
	const float CorridorTurnWeights[] = {0.6f, 0.2f, 0.2f};

	int32 PickWeightedIndex(FRandomStream& Rng, TArrayView<const float> Weights)
	{
		float Total = 0.0f;
		for (const float Weight : Weights)
		{
			Total += Weight;
		}
		const float Roll = Rng.FRand() * Total;
		float Accumulated = 0.0f;
		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			Accumulated += Weights[Index];
			if (Roll < Accumulated)
			{
				return Index;
			}
		}
		return Weights.Num() - 1;
	}

	template <typename T>
	void ShuffleArray(FRandomStream& Rng, TArray<T>& Items)
	{
		for (int32 Index = Items.Num() - 1; Index > 0; --Index)
		{
			Items.Swap(Index, Rng.RandHelper(Index + 1));
		}
	}

	FIntVector SocketKey(const FIntPoint& Cell, ETDDoorDirection Direction)
	{
		return FIntVector(Cell.X, Cell.Y, static_cast<int32>(Direction));
	}

	bool FindDirectionBetween(const FIntPoint& From, const FIntPoint& To, ETDDoorDirection& OutDirection)
	{
		const FIntPoint Delta = To - From;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const ETDDoorDirection Direction = static_cast<ETDDoorDirection>(Index);
			if (TDDungeon::DirectionOffset(Direction) == Delta)
			{
				OutDirection = Direction;
				return true;
			}
		}
		return false;
	}

	struct FTDRotatedModule
	{
		TArray<FIntPoint> Cells;
		TArray<FTDDoorSocket> Sockets;
	};

	FTDRotatedModule RotateModule(const FTDRoomModuleDefinition& Module, int32 Rotation)
	{
		FTDRotatedModule Result;
		FIntPoint MinCell(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
		for (const FIntPoint& Cell : Module.Cells)
		{
			const FIntPoint Rotated = TDDungeon::RotateCell(Cell, Rotation);
			MinCell.X = FMath::Min(MinCell.X, Rotated.X);
			MinCell.Y = FMath::Min(MinCell.Y, Rotated.Y);
			Result.Cells.Add(Rotated);
		}
		for (FIntPoint& Cell : Result.Cells)
		{
			Cell -= MinCell;
		}
		for (const FTDDoorSocket& Socket : Module.Sockets)
		{
			FTDDoorSocket& Rotated = Result.Sockets.AddDefaulted_GetRef();
			Rotated.Cell = TDDungeon::RotateCell(Socket.Cell, Rotation) - MinCell;
			Rotated.Direction = TDDungeon::RotateDirection(Socket.Direction, Rotation);
			Rotated.Tag = Socket.Tag;
		}
		return Result;
	}

	struct FTDPlacementCandidate
	{
		int32 Rotation = 0;
		FIntPoint Origin = FIntPoint::ZeroValue;
	};

	void CollectPlacements(const FTDRoomModuleDefinition& Module, const FIntPoint& WorldCell, ETDDoorDirection WorldDirection, TArray<FTDPlacementCandidate>& OutPlacements)
	{
		const int32 RotationCount = Module.bAllowRotation ? 4 : 1;
		for (int32 Rotation = 0; Rotation < RotationCount; ++Rotation)
		{
			const FTDRotatedModule Rotated = RotateModule(Module, Rotation);
			for (const FTDDoorSocket& Socket : Rotated.Sockets)
			{
				if (Socket.Direction != WorldDirection)
				{
					continue;
				}
				FTDPlacementCandidate& Candidate = OutPlacements.AddDefaulted_GetRef();
				Candidate.Rotation = Rotation;
				Candidate.Origin = WorldCell - Socket.Cell;
			}
		}
	}

	struct FTDSolverSocket
	{
		FIntPoint Cell = FIntPoint::ZeroValue;
		ETDDoorDirection Direction = ETDDoorDirection::North;
		int32 ConnectedRoom = INDEX_NONE;
	};

	struct FTDSolverRoom
	{
		const FTDRoomModuleDefinition* Module = nullptr;
		int32 Rotation = 0;
		FIntPoint Origin = FIntPoint::ZeroValue;
		TArray<FIntPoint> Cells;
		TArray<FTDSolverSocket> Sockets;
		int32 FlowNode = INDEX_NONE;
		TArray<ETDRoomRole> Roles;
		FName HeldKeyId;
	};

	struct FTDSolverDoor
	{
		int32 RoomA = INDEX_NONE;
		int32 RoomB = INDEX_NONE;
		FIntPoint CellA = FIntPoint::ZeroValue;
		ETDDoorDirection DirectionFromA = ETDDoorDirection::North;
		FName LockId;
	};

	struct FTDCorridorStep
	{
		FIntPoint Cell = FIntPoint::ZeroValue;
		ETDDoorDirection In = ETDDoorDirection::North;
		ETDDoorDirection Out = ETDDoorDirection::South;
	};

	struct FTDCorridorLink
	{
		int32 Room = INDEX_NONE;
		FIntPoint OutCell = FIntPoint::ZeroValue;
		ETDDoorDirection OutDirection = ETDDoorDirection::North;
	};

	struct FTDPlacementRecord
	{
		int32 Node = INDEX_NONE;
		int32 ParentRoom = INDEX_NONE;
		int32 ParentSocket = INDEX_NONE;
		int32 RoomCountBefore = 0;
		int32 DoorCountBefore = 0;
		FName LockId;
	};

	struct FTDSolverState
	{
		const UTDDungeonTheme& Theme;
		const FTDDungeonLayoutSolver::FSettings& Settings;
		TArray<FTDSolverRoom> Rooms;
		TArray<FTDSolverDoor> Doors;
		TSet<FIntPoint> Occupied;
		TMap<int32, int32> NodeToRoom;
		TArray<const FTDRoomModuleDefinition*> CorridorSingles;
		TArray<const FTDRoomModuleDefinition*> CorridorPairs;

		FTDSolverState(const UTDDungeonTheme& InTheme, const FTDDungeonLayoutSolver::FSettings& InSettings)
			: Theme(InTheme), Settings(InSettings)
		{
			TArray<const FTDRoomModuleDefinition*> Corridors;
			Theme.CollectModulesWithRole(ETDRoomRole::Corridor, Corridors);
			for (const FTDRoomModuleDefinition* Corridor : Corridors)
			{
				if (Corridor->Cells.Num() == 1)
				{
					CorridorSingles.Add(Corridor);
				}
				else if (Corridor->Cells.Num() == 2)
				{
					CorridorPairs.Add(Corridor);
				}
			}
		}

		bool AreCellsFree(const TArray<FIntPoint>& Cells) const
		{
			for (const FIntPoint& Cell : Cells)
			{
				if (Occupied.Contains(Cell))
				{
					return false;
				}
			}
			return true;
		}

		int32 PlaceRoom(const FTDRoomModuleDefinition& Module, int32 Rotation, const FIntPoint& Origin, const TArray<ETDRoomRole>& Roles, int32 FlowNode, FName HeldKeyId)
		{
			const FTDRotatedModule Rotated = RotateModule(Module, Rotation);
			FTDSolverRoom& Room = Rooms.AddDefaulted_GetRef();
			const int32 RoomIndex = Rooms.Num() - 1;
			Room.Module = &Module;
			Room.Rotation = Rotation;
			Room.Origin = Origin;
			Room.FlowNode = FlowNode;
			Room.Roles = Roles;
			Room.HeldKeyId = HeldKeyId;
			for (const FIntPoint& Cell : Rotated.Cells)
			{
				const FIntPoint WorldCell = Origin + Cell;
				Room.Cells.Add(WorldCell);
				Occupied.Add(WorldCell);
			}
			for (const FTDDoorSocket& Socket : Rotated.Sockets)
			{
				FTDSolverSocket& RoomSocket = Room.Sockets.AddDefaulted_GetRef();
				RoomSocket.Cell = Origin + Socket.Cell;
				RoomSocket.Direction = Socket.Direction;
			}
			if (FlowNode != INDEX_NONE)
			{
				NodeToRoom.Add(FlowNode, RoomIndex);
			}
			return RoomIndex;
		}

		int32 FindSocket(int32 RoomIndex, const FIntPoint& Cell, ETDDoorDirection Direction) const
		{
			const FTDSolverRoom& Room = Rooms[RoomIndex];
			for (int32 Index = 0; Index < Room.Sockets.Num(); ++Index)
			{
				if (Room.Sockets[Index].Cell == Cell && Room.Sockets[Index].Direction == Direction)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		void CollectFreeSockets(int32 RoomIndex, TArray<int32>& OutSockets) const
		{
			const FTDSolverRoom& Room = Rooms[RoomIndex];
			for (int32 Index = 0; Index < Room.Sockets.Num(); ++Index)
			{
				if (Room.Sockets[Index].ConnectedRoom == INDEX_NONE)
				{
					OutSockets.Add(Index);
				}
			}
		}

		bool Connect(int32 RoomA, const FIntPoint& CellA, ETDDoorDirection DirectionA, int32 RoomB, FName LockId)
		{
			const FIntPoint CellB = CellA + TDDungeon::DirectionOffset(DirectionA);
			const int32 SocketA = FindSocket(RoomA, CellA, DirectionA);
			const int32 SocketB = FindSocket(RoomB, CellB, TDDungeon::Opposite(DirectionA));
			if (SocketA == INDEX_NONE || SocketB == INDEX_NONE)
			{
				return false;
			}
			Rooms[RoomA].Sockets[SocketA].ConnectedRoom = RoomB;
			Rooms[RoomB].Sockets[SocketB].ConnectedRoom = RoomA;
			FTDSolverDoor& Door = Doors.AddDefaulted_GetRef();
			Door.RoomA = RoomA;
			Door.RoomB = RoomB;
			Door.CellA = CellA;
			Door.DirectionFromA = DirectionA;
			Door.LockId = LockId;
			return true;
		}

		bool BuildCorridorPath(const FIntPoint& StartCell, ETDDoorDirection InDirection, int32 Length, FRandomStream& Rng, TArray<FTDCorridorStep>& OutPath) const
		{
			FIntPoint Cell = StartCell;
			ETDDoorDirection DoorIn = InDirection;
			TSet<FIntPoint> Used;
			for (int32 Index = 0; Index < Length; ++Index)
			{
				if (Occupied.Contains(Cell) || Used.Contains(Cell))
				{
					return false;
				}
				const ETDDoorDirection Forward = TDDungeon::Opposite(DoorIn);
				const ETDDoorDirection Options[] = {Forward, TDDungeon::RotateDirection(Forward, 3), TDDungeon::RotateDirection(Forward, 1)};
				const ETDDoorDirection DoorOut = Options[PickWeightedIndex(Rng, CorridorTurnWeights)];
				FTDCorridorStep& Step = OutPath.AddDefaulted_GetRef();
				Step.Cell = Cell;
				Step.In = DoorIn;
				Step.Out = DoorOut;
				Used.Add(Cell);
				Cell += TDDungeon::DirectionOffset(DoorOut);
				DoorIn = TDDungeon::Opposite(DoorOut);
			}
			return true;
		}

		bool FindCorridorPlacement(const FTDRoomModuleDefinition& Module, TArrayView<const FTDCorridorStep> Group, int32& OutRotation, FIntPoint& OutOrigin) const
		{
			TSet<FIntPoint> WantCells;
			for (const FTDCorridorStep& Step : Group)
			{
				WantCells.Add(Step.Cell);
			}
			TSet<FIntVector> WantSockets;
			WantSockets.Add(SocketKey(Group[0].Cell, Group[0].In));
			WantSockets.Add(SocketKey(Group.Last().Cell, Group.Last().Out));
			TArray<FTDPlacementCandidate> Placements;
			CollectPlacements(Module, Group[0].Cell, Group[0].In, Placements);
			for (const FTDPlacementCandidate& Placement : Placements)
			{
				const FTDRotatedModule Rotated = RotateModule(Module, Placement.Rotation);
				if (Rotated.Cells.Num() != WantCells.Num() || Rotated.Sockets.Num() != WantSockets.Num())
				{
					continue;
				}
				bool bMatches = true;
				for (const FIntPoint& Cell : Rotated.Cells)
				{
					bMatches &= WantCells.Contains(Placement.Origin + Cell);
				}
				for (const FTDDoorSocket& Socket : Rotated.Sockets)
				{
					bMatches &= WantSockets.Contains(SocketKey(Placement.Origin + Socket.Cell, Socket.Direction));
				}
				if (!bMatches)
				{
					continue;
				}
				OutRotation = Placement.Rotation;
				OutOrigin = Placement.Origin;
				return true;
			}
			return false;
		}

		bool TryPlaceCorridorGroup(const TArray<const FTDRoomModuleDefinition*>& Modules, TArrayView<const FTDCorridorStep> Group, TArray<FTDCorridorLink>& OutLinks)
		{
			for (const FTDRoomModuleDefinition* Module : Modules)
			{
				int32 Rotation = 0;
				FIntPoint Origin;
				if (!FindCorridorPlacement(*Module, Group, Rotation, Origin))
				{
					continue;
				}
				FTDCorridorLink& Link = OutLinks.AddDefaulted_GetRef();
				Link.Room = PlaceRoom(*Module, Rotation, Origin, {ETDRoomRole::Corridor}, INDEX_NONE, NAME_None);
				Link.OutCell = Group.Last().Cell;
				Link.OutDirection = Group.Last().Out;
				return true;
			}
			return false;
		}

		bool CommitCorridor(const TArray<FTDCorridorStep>& Path, FRandomStream& Rng, TArray<FTDCorridorLink>& OutLinks)
		{
			int32 Index = 0;
			while (Index < Path.Num())
			{
				if (Index + 1 < Path.Num() && Rng.FRand() < 0.5f && TryPlaceCorridorGroup(CorridorPairs, TArrayView<const FTDCorridorStep>(Path.GetData() + Index, 2), OutLinks))
				{
					Index += 2;
					continue;
				}
				if (!TryPlaceCorridorGroup(CorridorSingles, TArrayView<const FTDCorridorStep>(Path.GetData() + Index, 1), OutLinks))
				{
					return false;
				}
				Index += 1;
			}
			return true;
		}

		bool LinkChain(int32 ParentRoom, int32 ParentSocket, const TArray<FTDCorridorLink>& Links, int32 ChildRoom, FName LockId)
		{
			int32 PreviousRoom = ParentRoom;
			FIntPoint PreviousCell = Rooms[ParentRoom].Sockets[ParentSocket].Cell;
			ETDDoorDirection PreviousDirection = Rooms[ParentRoom].Sockets[ParentSocket].Direction;
			for (const FTDCorridorLink& Link : Links)
			{
				if (!Connect(PreviousRoom, PreviousCell, PreviousDirection, Link.Room, NAME_None))
				{
					return false;
				}
				PreviousRoom = Link.Room;
				PreviousCell = Link.OutCell;
				PreviousDirection = Link.OutDirection;
			}
			return Connect(PreviousRoom, PreviousCell, PreviousDirection, ChildRoom, LockId);
		}

		void TruncateTo(int32 RoomCount, int32 DoorCount)
		{
			for (int32 Index = RoomCount; Index < Rooms.Num(); ++Index)
			{
				for (const FIntPoint& Cell : Rooms[Index].Cells)
				{
					Occupied.Remove(Cell);
				}
				if (Rooms[Index].FlowNode != INDEX_NONE)
				{
					NodeToRoom.Remove(Rooms[Index].FlowNode);
				}
			}
			Rooms.SetNum(RoomCount);
			Doors.SetNum(DoorCount);
		}

		void Undo(const FTDPlacementRecord& Record)
		{
			TruncateTo(Record.RoomCountBefore, Record.DoorCountBefore);
			Rooms[Record.ParentRoom].Sockets[Record.ParentSocket].ConnectedRoom = INDEX_NONE;
		}

		bool TryAttach(int32 ParentRoom, const FTDFlowNode& Node, int32 DegreeRequired, FName LockId, FRandomStream& Rng, FTDPlacementRecord& OutRecord)
		{
			TArray<const FTDRoomModuleDefinition*> Modules;
			Theme.CollectModulesWithRole(Node.Role, Modules);
			Modules.RemoveAll([DegreeRequired](const FTDRoomModuleDefinition* Module) { return Module->Sockets.Num() < DegreeRequired; });
			if (Modules.Num() == 0)
			{
				return false;
			}
			const int32 WeightCount = FMath::Clamp(Settings.MaxCorridorCells + 1, 1, static_cast<int32>(UE_ARRAY_COUNT(CorridorLengthWeights)));
			const TArrayView<const float> LengthWeights(CorridorLengthWeights, WeightCount);
			for (int32 Try = 0; Try < Settings.MaxCandidateTries; ++Try)
			{
				TArray<int32> FreeSockets;
				CollectFreeSockets(ParentRoom, FreeSockets);
				if (FreeSockets.Num() == 0)
				{
					return false;
				}
				const int32 ParentSocket = FreeSockets[Rng.RandHelper(FreeSockets.Num())];
				const FTDSolverSocket Socket = Rooms[ParentRoom].Sockets[ParentSocket];
				const int32 Length = PickWeightedIndex(Rng, LengthWeights);
				const FIntPoint FirstCell = Socket.Cell + TDDungeon::DirectionOffset(Socket.Direction);
				TArray<FTDCorridorStep> Path;
				if (!BuildCorridorPath(FirstCell, TDDungeon::Opposite(Socket.Direction), Length, Rng, Path))
				{
					continue;
				}
				FIntPoint EndCell = FirstCell;
				ETDDoorDirection EndDirection = TDDungeon::Opposite(Socket.Direction);
				TSet<FIntPoint> PathCells;
				for (const FTDCorridorStep& Step : Path)
				{
					PathCells.Add(Step.Cell);
				}
				if (Path.Num() > 0)
				{
					EndCell = Path.Last().Cell + TDDungeon::DirectionOffset(Path.Last().Out);
					EndDirection = TDDungeon::Opposite(Path.Last().Out);
				}
				if (PathCells.Contains(EndCell))
				{
					continue;
				}
				ShuffleArray(Rng, Modules);
				for (const FTDRoomModuleDefinition* Module : Modules)
				{
					TArray<FTDPlacementCandidate> Placements;
					CollectPlacements(*Module, EndCell, EndDirection, Placements);
					ShuffleArray(Rng, Placements);
					for (const FTDPlacementCandidate& Placement : Placements)
					{
						const FTDRotatedModule Rotated = RotateModule(*Module, Placement.Rotation);
						TArray<FIntPoint> WorldCells;
						bool bTouchesPath = false;
						for (const FIntPoint& Cell : Rotated.Cells)
						{
							WorldCells.Add(Placement.Origin + Cell);
							bTouchesPath |= PathCells.Contains(WorldCells.Last());
						}
						if (bTouchesPath || !AreCellsFree(WorldCells))
						{
							continue;
						}
						const int32 RoomCountBefore = Rooms.Num();
						const int32 DoorCountBefore = Doors.Num();
						TArray<FTDCorridorLink> Links;
						if (!CommitCorridor(Path, Rng, Links))
						{
							TruncateTo(RoomCountBefore, DoorCountBefore);
							continue;
						}
						const int32 ChildRoom = PlaceRoom(*Module, Placement.Rotation, Placement.Origin, {Node.Role}, Node.Index, Node.HeldKeyId);
						if (!LinkChain(ParentRoom, ParentSocket, Links, ChildRoom, LockId))
						{
							TruncateTo(RoomCountBefore, DoorCountBefore);
							Rooms[ParentRoom].Sockets[ParentSocket].ConnectedRoom = INDEX_NONE;
							continue;
						}
						OutRecord.Node = Node.Index;
						OutRecord.ParentRoom = ParentRoom;
						OutRecord.ParentSocket = ParentSocket;
						OutRecord.RoomCountBefore = RoomCountBefore;
						OutRecord.DoorCountBefore = DoorCountBefore;
						OutRecord.LockId = LockId;
						return true;
					}
				}
			}
			return false;
		}

		void ComputeBounds(FIntPoint& OutMin, FIntPoint& OutMax) const
		{
			OutMin = FIntPoint(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
			OutMax = FIntPoint(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
			for (const FIntPoint& Cell : Occupied)
			{
				OutMin.X = FMath::Min(OutMin.X, Cell.X);
				OutMin.Y = FMath::Min(OutMin.Y, Cell.Y);
				OutMax.X = FMath::Max(OutMax.X, Cell.X);
				OutMax.Y = FMath::Max(OutMax.Y, Cell.Y);
			}
		}

		bool FindFreeCellPath(const FIntPoint& Start, const FIntPoint& Goal, const FIntPoint& RegionMin, const FIntPoint& RegionMax, TArray<FIntPoint>& OutPath) const
		{
			if (Start == Goal)
			{
				OutPath = {Start};
				return true;
			}
			if (Occupied.Contains(Start) || Occupied.Contains(Goal))
			{
				return false;
			}
			TMap<FIntPoint, FIntPoint> Previous;
			Previous.Add(Start, Start);
			TArray<FIntPoint> Frontier = {Start};
			for (int32 Depth = 0; Frontier.Num() > 0 && Depth < MaxLoopPathLength; ++Depth)
			{
				TArray<FIntPoint> NextFrontier;
				for (const FIntPoint& Cell : Frontier)
				{
					for (int32 DirectionIndex = 0; DirectionIndex < 4; ++DirectionIndex)
					{
						const FIntPoint Neighbor = Cell + TDDungeon::DirectionOffset(static_cast<ETDDoorDirection>(DirectionIndex));
						if (Previous.Contains(Neighbor) || Occupied.Contains(Neighbor))
						{
							continue;
						}
						if (Neighbor.X < RegionMin.X || Neighbor.X > RegionMax.X || Neighbor.Y < RegionMin.Y || Neighbor.Y > RegionMax.Y)
						{
							continue;
						}
						Previous.Add(Neighbor, Cell);
						if (Neighbor == Goal)
						{
							OutPath.Reset();
							for (FIntPoint Walk = Goal; Walk != Start; Walk = Previous[Walk])
							{
								OutPath.Add(Walk);
							}
							OutPath.Add(Start);
							Algo::Reverse(OutPath);
							return true;
						}
						NextFrontier.Add(Neighbor);
					}
				}
				Frontier = MoveTemp(NextFrontier);
			}
			return false;
		}

		bool TryCloseLoop(int32 RoomA, int32 RoomB, FRandomStream& Rng)
		{
			FIntPoint BoundsMin;
			FIntPoint BoundsMax;
			ComputeBounds(BoundsMin, BoundsMax);
			const FIntPoint RegionMin = BoundsMin - FIntPoint(LoopRegionMargin, LoopRegionMargin);
			const FIntPoint RegionMax = BoundsMax + FIntPoint(LoopRegionMargin, LoopRegionMargin);
			TArray<int32> FreeA;
			TArray<int32> FreeB;
			CollectFreeSockets(RoomA, FreeA);
			CollectFreeSockets(RoomB, FreeB);
			TArray<FIntPoint> BestPath;
			int32 BestSocketA = INDEX_NONE;
			int32 BestSocketB = INDEX_NONE;
			for (const int32 SocketA : FreeA)
			{
				const FTDSolverSocket& DoorA = Rooms[RoomA].Sockets[SocketA];
				const FIntPoint OutA = DoorA.Cell + TDDungeon::DirectionOffset(DoorA.Direction);
				for (const int32 SocketB : FreeB)
				{
					const FTDSolverSocket& DoorB = Rooms[RoomB].Sockets[SocketB];
					const FIntPoint OutB = DoorB.Cell + TDDungeon::DirectionOffset(DoorB.Direction);
					if (OutA == DoorB.Cell && DoorB.Direction == TDDungeon::Opposite(DoorA.Direction))
					{
						return Connect(RoomA, DoorA.Cell, DoorA.Direction, RoomB, NAME_None);
					}
					TArray<FIntPoint> Path;
					if (!FindFreeCellPath(OutA, OutB, RegionMin, RegionMax, Path))
					{
						continue;
					}
					if (BestSocketA == INDEX_NONE || Path.Num() < BestPath.Num())
					{
						BestPath = MoveTemp(Path);
						BestSocketA = SocketA;
						BestSocketB = SocketB;
					}
				}
			}
			if (BestSocketA == INDEX_NONE)
			{
				return false;
			}
			const FTDSolverSocket DoorA = Rooms[RoomA].Sockets[BestSocketA];
			const FTDSolverSocket DoorB = Rooms[RoomB].Sockets[BestSocketB];
			TArray<FTDCorridorStep> Corridor;
			for (int32 Index = 0; Index < BestPath.Num(); ++Index)
			{
				FTDCorridorStep& Step = Corridor.AddDefaulted_GetRef();
				Step.Cell = BestPath[Index];
				Step.In = TDDungeon::Opposite(DoorA.Direction);
				Step.Out = TDDungeon::Opposite(DoorB.Direction);
				if (Index > 0 && !FindDirectionBetween(BestPath[Index], BestPath[Index - 1], Step.In))
				{
					return false;
				}
				if (Index + 1 < BestPath.Num() && !FindDirectionBetween(BestPath[Index], BestPath[Index + 1], Step.Out))
				{
					return false;
				}
				if (Step.In == Step.Out)
				{
					return false;
				}
			}
			TArray<FTDCorridorLink> Links;
			if (!CommitCorridor(Corridor, Rng, Links))
			{
				return false;
			}
			return LinkChain(RoomA, BestSocketA, Links, RoomB, NAME_None);
		}
	};

	bool SolveAttempt(const FTDDungeonFlowGraph& Graph, const TArray<TArray<const FTDFlowEdge*>>& TreeChildren, const TArray<int32>& Degrees, const FTDRoomModuleDefinition& EntranceModule, FTDSolverState& State, FRandomStream& Rng)
	{
		const FTDFlowNode& StartNode = Graph.Nodes[Graph.StartNode];
		State.PlaceRoom(EntranceModule, 0, FIntPoint::ZeroValue, {StartNode.Role}, StartNode.Index, StartNode.HeldKeyId);
		TArray<int32> Queue = {Graph.StartNode};
		TArray<FTDPlacementRecord> Records;
		int32 BacktracksUsed = 0;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 ParentNode = Queue[Head];
			for (const FTDFlowEdge* Edge : TreeChildren[ParentNode])
			{
				const FTDFlowNode& Child = Graph.Nodes[Edge->To];
				while (true)
				{
					FTDPlacementRecord Record;
					if (State.TryAttach(State.NodeToRoom[ParentNode], Child, Degrees[Child.Index], Edge->LockId, Rng, Record))
					{
						Records.Add(Record);
						break;
					}
					if (Records.Num() == 0 || BacktracksUsed >= State.Settings.MaxBacktrackDepth)
					{
						return false;
					}
					const FTDPlacementRecord Undone = Records.Pop();
					State.Undo(Undone);
					++BacktracksUsed;
					FTDPlacementRecord Replaced;
					if (!State.TryAttach(Undone.ParentRoom, Graph.Nodes[Undone.Node], Degrees[Undone.Node], Undone.LockId, Rng, Replaced))
					{
						return false;
					}
					Records.Add(Replaced);
				}
				Queue.Add(Child.Index);
			}
		}
		for (const FTDFlowEdge& Edge : Graph.Edges)
		{
			if (!Edge.bIsLoop)
			{
				continue;
			}
			if (!State.TryCloseLoop(State.NodeToRoom[Edge.From], State.NodeToRoom[Edge.To], Rng))
			{
				return false;
			}
		}
		return true;
	}

	FName MakeRoomId(int32 Index)
	{
		return FName(*FString::Printf(TEXT("r%d"), Index));
	}

	void FillLayout(const FTDSolverState& State, const FTDDungeonFlowGraph& Graph, const UTDDungeonTheme& Theme, const FTDSeedContext& Seed, FTDDungeonLayout& OutLayout)
	{
		FIntPoint BoundsMin;
		FIntPoint BoundsMax;
		State.ComputeBounds(BoundsMin, BoundsMax);
		OutLayout.Seed = Seed.MasterSeed;
		OutLayout.GeneratorVersion = TD_WORLDGEN_VERSION;
		OutLayout.ThemeId = Theme.ThemeId;
		OutLayout.CellSizeCm = Theme.CellSizeCm;
		OutLayout.Graph = Graph;
		OutLayout.Rooms.Reset();
		OutLayout.Doors.Reset();
		OutLayout.MinCell = FIntPoint::ZeroValue;
		OutLayout.MaxCell = BoundsMax - BoundsMin;
		for (int32 Index = 0; Index < State.Rooms.Num(); ++Index)
		{
			const FTDSolverRoom& Source = State.Rooms[Index];
			FTDPlacedRoom& Room = OutLayout.Rooms.AddDefaulted_GetRef();
			Room.RoomId = MakeRoomId(Index);
			Room.FlowNode = Source.FlowNode;
			Room.ModuleId = Source.Module->ModuleId;
			Room.Rotation = Source.Rotation;
			Room.CellOrigin = Source.Origin - BoundsMin;
			for (const FIntPoint& Cell : Source.Cells)
			{
				Room.Cells.Add(Cell - BoundsMin);
			}
			Room.Roles = Source.Roles;
			Room.HeldKeyId = Source.HeldKeyId;
		}
		for (const FTDSolverDoor& Source : State.Doors)
		{
			FTDPlacedDoor& Door = OutLayout.Doors.AddDefaulted_GetRef();
			Door.CellA = Source.CellA - BoundsMin;
			Door.CellB = Door.CellA + TDDungeon::DirectionOffset(Source.DirectionFromA);
			Door.DirectionFromA = Source.DirectionFromA;
			Door.RoomA = MakeRoomId(Source.RoomA);
			Door.RoomB = MakeRoomId(Source.RoomB);
			Door.LockId = Source.LockId;
		}
		const FTDSolverRoom* Entrance = State.Rooms.FindByPredicate([](const FTDSolverRoom& Room) { return Room.Roles.Contains(ETDRoomRole::Entrance); });
		const FTDSolverRoom* Boss = State.Rooms.FindByPredicate([](const FTDSolverRoom& Room) { return Room.Roles.Contains(ETDRoomRole::Boss); });
		if (Entrance && Entrance->Cells.Num() > 0)
		{
			const ETDDoorDirection Facing = Entrance->Sockets.Num() > 0 ? Entrance->Sockets[0].Direction : ETDDoorDirection::North;
			OutLayout.EntryTransform = FTransform(FRotator(0.0f, TDDungeon::DirectionYaw(Facing), 0.0f), OutLayout.CellCenterLocal(Entrance->Cells[0] - BoundsMin));
		}
		if (Boss && Boss->Cells.Num() > 0)
		{
			FIntPoint BossMin(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
			FIntPoint BossMax(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
			for (const FIntPoint& Cell : Boss->Cells)
			{
				const FIntPoint Shifted = Cell - BoundsMin;
				BossMin = FIntPoint(FMath::Min(BossMin.X, Shifted.X), FMath::Min(BossMin.Y, Shifted.Y));
				BossMax = FIntPoint(FMath::Max(BossMax.X, Shifted.X), FMath::Max(BossMax.Y, Shifted.Y));
			}
			const FVector Center((BossMin.X + BossMax.X + 1) * 0.5 * OutLayout.CellSizeCm, (BossMin.Y + BossMax.Y + 1) * 0.5 * OutLayout.CellSizeCm, 0.0);
			const ETDDoorDirection Facing = Boss->Sockets.Num() > 0 ? TDDungeon::Opposite(Boss->Sockets[0].Direction) : ETDDoorDirection::North;
			OutLayout.ExitTransform = FTransform(FRotator(0.0f, TDDungeon::DirectionYaw(Facing), 0.0f), Center);
		}
	}
}

bool FTDDungeonLayoutSolver::Solve(const FTDDungeonFlowGraph& Graph, const UTDDungeonTheme& Theme, const FTDSeedContext& Seed, const FSettings& Settings, FTDDungeonLayout& OutLayout, FString& OutError)
{
	if (!Graph.Nodes.IsValidIndex(Graph.StartNode))
	{
		OutError = TEXT("흐름 그래프에 시작 노드가 없습니다");
		return false;
	}
	TArray<const FTDRoomModuleDefinition*> EntranceModules;
	Theme.CollectModulesWithRole(ETDRoomRole::Entrance, EntranceModules);
	if (EntranceModules.Num() == 0)
	{
		OutError = TEXT("테마에 입구 모듈이 없습니다");
		return false;
	}
	TArray<const FTDRoomModuleDefinition*> CorridorModules;
	Theme.CollectModulesWithRole(ETDRoomRole::Corridor, CorridorModules);
	if (CorridorModules.Num() == 0)
	{
		OutError = TEXT("테마에 복도 모듈이 없습니다");
		return false;
	}
	TArray<TArray<const FTDFlowEdge*>> TreeChildren;
	TreeChildren.SetNum(Graph.Nodes.Num());
	TArray<int32> Degrees;
	Degrees.Init(0, Graph.Nodes.Num());
	for (const FTDFlowEdge& Edge : Graph.Edges)
	{
		if (!Graph.Nodes.IsValidIndex(Edge.From) || !Graph.Nodes.IsValidIndex(Edge.To))
		{
			OutError = TEXT("흐름 간선이 잘못된 노드를 가리킵니다");
			return false;
		}
		Degrees[Edge.From] += 1;
		Degrees[Edge.To] += 1;
		if (!Edge.bIsLoop)
		{
			TreeChildren[Edge.From].Add(&Edge);
		}
	}
	for (int32 Restart = 0; Restart <= Settings.MaxRestarts; ++Restart)
	{
		FRandomStream Rng = Seed.Derive(TEXT("Layout"), Restart);
		FTDSolverState State(Theme, Settings);
		if (!SolveAttempt(Graph, TreeChildren, Degrees, *EntranceModules[0], State, Rng))
		{
			continue;
		}
		FillLayout(State, Graph, Theme, Seed, OutLayout);
		OutLayout.LayoutRestarts = Restart;
		return true;
	}
	OutError = FString::Printf(TEXT("배치 실패: 재시작 %d회 초과"), Settings.MaxRestarts);
	return false;
}
