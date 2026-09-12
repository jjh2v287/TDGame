#include "Dungeon/TDDungeonGeneration.h"

namespace
{
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
			const int32 Other = Rng.RandHelper(Index + 1);
			Items.Swap(Index, Other);
		}
	}

	struct FTDFlowBuilder
	{
		FTDDungeonFlowGraph& Graph;
		const UTDDungeonFlowTemplate& Template;

		FTDFlowBuilder(FTDDungeonFlowGraph& InGraph, const UTDDungeonFlowTemplate& InTemplate)
			: Graph(InGraph), Template(InTemplate)
		{
		}

		int32 AddNode(ETDRoomRole Role)
		{
			FTDFlowNode& Node = Graph.Nodes.AddDefaulted_GetRef();
			Node.Index = Graph.Nodes.Num() - 1;
			Node.Role = Role;
			return Node.Index;
		}

		void AddEdge(int32 From, int32 To, bool bIsLoop = false, FName LockId = NAME_None)
		{
			FTDFlowEdge& Edge = Graph.Edges.AddDefaulted_GetRef();
			Edge.From = From;
			Edge.To = To;
			Edge.bIsLoop = bIsLoop;
			Edge.LockId = LockId;
		}

		TArray<int32> AddChain(const TArray<ETDRoomRole>& Roles)
		{
			TArray<int32> Ids;
			for (const ETDRoomRole Role : Roles)
			{
				Ids.Add(AddNode(Role));
			}
			for (int32 Index = 1; Index < Ids.Num(); ++Index)
			{
				AddEdge(Ids[Index - 1], Ids[Index]);
			}
			return Ids;
		}

		TArray<int32> AddBranch(int32 AttachNode, const TArray<ETDRoomRole>& Roles)
		{
			TArray<int32> Ids;
			int32 Previous = AttachNode;
			for (const ETDRoomRole Role : Roles)
			{
				const int32 NodeId = AddNode(Role);
				AddEdge(Previous, NodeId);
				Ids.Add(NodeId);
				Previous = NodeId;
			}
			return Ids;
		}

		TArray<ETDRoomRole> MainChainRoles(int32 Count) const
		{
			TArray<ETDRoomRole> Roles;
			Roles.Add(ETDRoomRole::Entrance);
			for (int32 Index = 0; Index < Count - 3; ++Index)
			{
				Roles.Add(ETDRoomRole::Combat);
			}
			Roles.Add(Template.bRequireElite ? ETDRoomRole::Elite : ETDRoomRole::Combat);
			Roles.Add(ETDRoomRole::Boss);
			return Roles;
		}

		static ETDRoomRole PickLeafRole(FRandomStream& Rng)
		{
			const float Weights[] = {0.5f, 0.25f, 0.25f};
			const ETDRoomRole Leaves[] = {ETDRoomRole::Treasure, ETDRoomRole::Elite, ETDRoomRole::DeadEnd};
			return Leaves[PickWeightedIndex(Rng, Weights)];
		}

		static TArray<ETDRoomRole> CombatsThen(int32 CombatCount, ETDRoomRole Leaf)
		{
			TArray<ETDRoomRole> Roles;
			for (int32 Index = 0; Index < CombatCount; ++Index)
			{
				Roles.Add(ETDRoomRole::Combat);
			}
			Roles.Add(Leaf);
			return Roles;
		}

		void BuildLinear(int32 Total, FRandomStream& Rng)
		{
			AddChain(MainChainRoles(Total));
		}

		void BuildBranch(int32 Total, FRandomStream& Rng)
		{
			int32 BranchCount = 3;
			if (Total <= 6)
			{
				BranchCount = 1;
			}
			else if (Total <= 9)
			{
				BranchCount = 2;
			}
			else if (Total >= 16)
			{
				BranchCount = 4;
			}
			TArray<int32> BranchLengths;
			for (int32 Index = 0; Index < BranchCount; ++Index)
			{
				BranchLengths.Add(1 + Rng.RandHelper(2));
			}
			auto SumLengths = [&BranchLengths]()
			{
				int32 Sum = 0;
				for (const int32 Length : BranchLengths)
				{
					Sum += Length;
				}
				return Sum;
			};
			while (BranchLengths.Num() > 0 && (Total - SumLengths() - 3) * Template.MaxBranches < BranchLengths.Num())
			{
				BranchLengths.Pop();
			}
			const int32 MainCount = Total - SumLengths();
			const TArray<int32> MainIds = AddChain(MainChainRoles(MainCount));
			TArray<int32> AttachOrder;
			for (int32 Index = 1; Index <= MainCount - 3; ++Index)
			{
				AttachOrder.Add(MainIds[Index]);
			}
			ShuffleArray(Rng, AttachOrder);
			for (int32 Index = 0; Index < BranchLengths.Num(); ++Index)
			{
				const int32 AttachNode = AttachOrder[Index % AttachOrder.Num()];
				const ETDRoomRole Leaf = (Index == 0 && Template.bRequireTreasure) ? ETDRoomRole::Treasure : PickLeafRole(Rng);
				AddBranch(AttachNode, CombatsThen(BranchLengths[Index] - 1, Leaf));
			}
		}

		void BuildLoop(int32 Total, FRandomStream& Rng)
		{
			const int32 LoopLength = FMath::Min(Rng.RandHelper(3), FMath::Max(0, Total - 6));
			const bool bHasTreasureRoom = Total - LoopLength - 6 >= 1;
			const bool bTreasureBranch = bHasTreasureRoom && (Template.bRequireTreasure || Rng.FRand() < 0.7f);
			const int32 MainCount = Total - LoopLength - (bTreasureBranch ? 1 : 0);
			const TArray<int32> MainIds = AddChain(MainChainRoles(MainCount));
			const int32 CombatLast = MainCount - 3;
			const int32 LoopFrom = Rng.RandRange(1, CombatLast - 2);
			const int32 LoopTo = Rng.RandRange(LoopFrom + 2, CombatLast);
			const bool bAllowLoop = Template.MaxLoops >= 1;
			if (LoopLength == 0)
			{
				if (bAllowLoop)
				{
					AddEdge(MainIds[LoopFrom], MainIds[LoopTo], true);
				}
			}
			else
			{
				const TArray<int32> LoopIds = AddBranch(MainIds[LoopFrom], CombatsThen(LoopLength - 1, ETDRoomRole::Combat));
				if (bAllowLoop)
				{
					AddEdge(LoopIds.Last(), MainIds[LoopTo], true);
				}
			}
			if (!bTreasureBranch)
			{
				return;
			}
			TArray<int32> Candidates;
			for (int32 Index = 1; Index <= CombatLast; ++Index)
			{
				if (Index != LoopFrom && Index != LoopTo)
				{
					Candidates.Add(MainIds[Index]);
				}
			}
			const int32 AttachNode = Candidates.Num() > 0 ? Candidates[Rng.RandHelper(Candidates.Num())] : MainIds[1];
			AddBranch(AttachNode, {ETDRoomRole::Treasure});
		}

		void BuildHub(int32 Total, FRandomStream& Rng)
		{
			const int32 StartId = AddNode(ETDRoomRole::Entrance);
			const int32 HubId = AddNode(ETDRoomRole::Hub);
			AddEdge(StartId, HubId);
			const int32 Spare = FMath::Max(0, Total - 6);
			int32 Counts[3] = {0, 0, 0};
			for (int32 Index = 0; Index < Spare; ++Index)
			{
				Counts[Rng.RandHelper(3)] += 1;
			}
			TArray<int32> WingOrder = {0, 1, 2};
			ShuffleArray(Rng, WingOrder);
			for (const int32 Wing : WingOrder)
			{
				TArray<ETDRoomRole> Roles;
				for (int32 Index = 0; Index < Counts[Wing]; ++Index)
				{
					Roles.Add(ETDRoomRole::Combat);
				}
				if (Wing == 0)
				{
					Roles.Add(Template.bRequireElite ? ETDRoomRole::Elite : ETDRoomRole::Combat);
					Roles.Add(ETDRoomRole::Boss);
				}
				else if (Wing == 1)
				{
					Roles.Add(ETDRoomRole::Treasure);
				}
				else
				{
					Roles.Add(PickLeafRole(Rng));
				}
				AddBranch(HubId, Roles);
			}
		}

		void BuildKeyLock(int32 Total, FRandomStream& Rng)
		{
			int32 KeyBranchLength = 1 + Rng.RandHelper(2);
			bool bTreasureBranch = Total - KeyBranchLength - 5 >= 1 && (Template.bRequireTreasure || Rng.FRand() < 0.5f);
			int32 MainCount = Total - KeyBranchLength - (bTreasureBranch ? 1 : 0);
			if (MainCount < 5)
			{
				KeyBranchLength = 1;
				bTreasureBranch = false;
				MainCount = Total - 1;
			}
			const TArray<int32> MainIds = AddChain(MainChainRoles(MainCount));
			const int32 LockIndex = Rng.FRand() < 0.5f ? MainCount - 3 : MainCount - 2;
			const FName KeyId(TEXT("key_0"));
			for (FTDFlowEdge& Edge : Graph.Edges)
			{
				if (Edge.From == MainIds[LockIndex] && Edge.To == MainIds[LockIndex + 1])
				{
					Edge.LockId = KeyId;
				}
			}
			const int32 AttachIndex = Rng.RandRange(1, LockIndex);
			const TArray<int32> KeyIds = AddBranch(MainIds[AttachIndex], CombatsThen(KeyBranchLength - 1, ETDRoomRole::Key));
			Graph.Nodes[KeyIds.Last()].HeldKeyId = KeyId;
			if (!bTreasureBranch)
			{
				return;
			}
			TArray<int32> Candidates;
			for (int32 Index = 1; Index <= MainCount - 3; ++Index)
			{
				if (Index != AttachIndex)
				{
					Candidates.Add(MainIds[Index]);
				}
			}
			const int32 AttachNode = Candidates.Num() > 0 ? Candidates[Rng.RandHelper(Candidates.Num())] : MainIds[1];
			AddBranch(AttachNode, {ETDRoomRole::Treasure});
		}
	};

	void AssignDepths(FTDDungeonFlowGraph& Graph)
	{
		TArray<int32> Queue;
		TArray<bool> Visited;
		Visited.Init(false, Graph.Nodes.Num());
		Queue.Add(Graph.StartNode);
		Visited[Graph.StartNode] = true;
		Graph.Nodes[Graph.StartNode].Depth = 0;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 Current = Queue[Head];
			for (const FTDFlowEdge& Edge : Graph.Edges)
			{
				if (Edge.bIsLoop)
				{
					continue;
				}
				const int32 Next = Edge.From == Current ? Edge.To : (Edge.To == Current ? Edge.From : INDEX_NONE);
				if (Next == INDEX_NONE || Visited[Next])
				{
					continue;
				}
				Visited[Next] = true;
				Graph.Nodes[Next].Depth = Graph.Nodes[Current].Depth + 1;
				Queue.Add(Next);
			}
		}
	}
}

bool FTDDungeonFlowGenerator::Generate(const UTDDungeonFlowTemplate& Template, ETDDungeonSize Size, const FTDSeedContext& Seed, FTDDungeonFlowGraph& OutGraph, FString& OutError)
{
	OutGraph = FTDDungeonFlowGraph();
	FRandomStream Rng = Seed.Derive(TEXT("Flow"));
	const FIntPoint Range = TDDungeon::RoomCountRange(Size);
	const int32 Total = Rng.RandRange(Range.X, Range.Y);
	if (Total < 6)
	{
		OutError = FString::Printf(TEXT("노드 수 %d는 최소 6 미만입니다"), Total);
		return false;
	}
	FTDFlowBuilder Builder(OutGraph, Template);
	switch (Template.Kind)
	{
	case ETDDungeonFlowKind::Linear: Builder.BuildLinear(Total, Rng); break;
	case ETDDungeonFlowKind::Branch: Builder.BuildBranch(Total, Rng); break;
	case ETDDungeonFlowKind::Loop: Builder.BuildLoop(Total, Rng); break;
	case ETDDungeonFlowKind::Hub: Builder.BuildHub(Total, Rng); break;
	default: Builder.BuildKeyLock(Total, Rng); break;
	}
	OutGraph.StartNode = 0;
	OutGraph.BossNode = INDEX_NONE;
	for (const FTDFlowNode& Node : OutGraph.Nodes)
	{
		if (Node.Role == ETDRoomRole::Boss)
		{
			OutGraph.BossNode = Node.Index;
			break;
		}
	}
	if (OutGraph.BossNode == INDEX_NONE)
	{
		OutError = TEXT("보스 노드가 생성되지 않았습니다");
		return false;
	}
	AssignDepths(OutGraph);
	if (!IsKeyBeforeLockOrderValid(OutGraph))
	{
		OutError = TEXT("열쇠가 잠금 앞에 오지 않습니다");
		return false;
	}
	return true;
}

bool FTDDungeonFlowGenerator::IsKeyBeforeLockOrderValid(const FTDDungeonFlowGraph& Graph)
{
	if (!Graph.Nodes.IsValidIndex(Graph.StartNode) || !Graph.Nodes.IsValidIndex(Graph.BossNode))
	{
		return false;
	}
	TArray<bool> Visited;
	Visited.Init(false, Graph.Nodes.Num());
	Visited[Graph.StartNode] = true;
	TSet<FName> HeldKeys;
	bool bChanged = true;
	while (bChanged)
	{
		bChanged = false;
		for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
		{
			if (!Visited[NodeIndex])
			{
				continue;
			}
			const FTDFlowNode& Node = Graph.Nodes[NodeIndex];
			if (!Node.HeldKeyId.IsNone() && !HeldKeys.Contains(Node.HeldKeyId))
			{
				HeldKeys.Add(Node.HeldKeyId);
				bChanged = true;
			}
			for (const FTDFlowEdge& Edge : Graph.Edges)
			{
				const int32 Next = Edge.From == NodeIndex ? Edge.To : (Edge.To == NodeIndex ? Edge.From : INDEX_NONE);
				if (Next == INDEX_NONE || !Graph.Nodes.IsValidIndex(Next) || Visited[Next])
				{
					continue;
				}
				if (!Edge.LockId.IsNone() && !HeldKeys.Contains(Edge.LockId))
				{
					continue;
				}
				Visited[Next] = true;
				bChanged = true;
			}
		}
	}
	return Visited[Graph.BossNode];
}
