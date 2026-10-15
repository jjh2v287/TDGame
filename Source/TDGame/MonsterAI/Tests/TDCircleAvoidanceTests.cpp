#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDCompanionCharacter.h"
#include "Characters/TDMonsterCharacter.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "MonsterAI/TDCircleAvoidance.h"
#include "MonsterAI/TDMonsterActionExecutor.h"
#include "MonsterAI/TDMonsterDefinition.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MonsterAI/TDMonsterThinkSubsystem.h"
#include "MonsterAI/TDNeighborGrid.h"
#include "MoverSimulationTypes.h"

namespace
{
	struct FTDCircleAvoidanceSnapshot
	{
		TArray<FTDMonsterBrainFragment> Brains;
		TArray<FTDMonsterMoveIntent> Intents;
		TArray<FTDMonsterAvoidanceFragment> Avoidances;
	};
}

struct FTDMonsterThinkSubsystemTestAccess
{
	static int32 FindSlot(const UTDMonsterThinkSubsystem& Subsystem, const AActor* Monster)
	{
		for (const int32 SlotIndex : Subsystem.OrderedSlots)
		{
			if (Subsystem.Bodies[SlotIndex].Actor.Get() == Monster)
			{
				return SlotIndex;
			}
		}
		return INDEX_NONE;
	}

	static FTDMonsterBrainFragment& GetMutableBrain(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.Brains[SlotIndex];
	}

	static FTDMonsterMoveIntent& GetMutableIntent(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.Intents[SlotIndex];
	}

	static FTDMonsterAvoidanceFragment& GetMutableAvoidance(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.Avoidances[SlotIndex];
	}

	static FTDCircleAvoidanceSnapshot CaptureSnapshot(const UTDMonsterThinkSubsystem& Subsystem)
	{
		return { Subsystem.Brains, Subsystem.Intents, Subsystem.Avoidances };
	}

	static void RestoreSnapshot(UTDMonsterThinkSubsystem& Subsystem, const FTDCircleAvoidanceSnapshot& Snapshot)
	{
		Subsystem.Brains = Snapshot.Brains;
		Subsystem.Intents = Snapshot.Intents;
		Subsystem.Avoidances = Snapshot.Avoidances;
	}

	static void ApplyCircleAvoidanceInOrder(UTDMonsterThinkSubsystem& Subsystem, const TArray<int32>& Order)
	{
		const TArray<int32> SavedOrder = Subsystem.OrderedSlots;
		Subsystem.OrderedSlots = Order;
		Subsystem.bIsNeighborGridStale = true;
		Subsystem.ApplyCircleAvoidance();
		Subsystem.OrderedSlots = SavedOrder;
	}

	static void ApplyCircleAvoidanceWritingEachCommandImmediately(UTDMonsterThinkSubsystem& Subsystem, const TArray<int32>& Order)
	{
		const TArray<int32> SavedOrder = Subsystem.OrderedSlots;
		Subsystem.OrderedSlots = Order;
		Subsystem.bIsNeighborGridStale = true;
		Subsystem.RefreshNeighborGridIfStale();
		Subsystem.OrderedSlots = SavedOrder;
		TArray<FTDAvoidanceObstacle> ExternalBodies;
		TArray<const AActor*> ExternalActors;
		Subsystem.GatherExternalBodies(ExternalBodies, ExternalActors);
		for (const int32 SlotIndex : Order)
		{
			Subsystem.Avoidances[SlotIndex].PreviousCommandVelocity = Subsystem.ApplyCircleAvoidanceToSlot(SlotIndex, ExternalBodies, ExternalActors);
		}
	}

	static int32 CountExternalBodies(const UTDMonsterThinkSubsystem& Subsystem)
	{
		TArray<FTDAvoidanceObstacle> ExternalBodies;
		TArray<const AActor*> ExternalActors;
		Subsystem.GatherExternalBodies(ExternalBodies, ExternalActors);
		return ExternalBodies.Num();
	}

	static int32 CountSlotsYieldingLastStep(const UTDMonsterThinkSubsystem& Subsystem)
	{
		int32 YieldingSlots = 0;
		for (const int32 SlotIndex : Subsystem.OrderedSlots)
		{
			const FTDMonsterAvoidanceFragment& Avoidance = Subsystem.Avoidances[SlotIndex];
			YieldingSlots += !Avoidance.PreviousCommandVelocity.IsZero() && Avoidance.PreviousCandidateIndex == INDEX_NONE ? 1 : 0;
		}
		return YieldingSlots;
	}

	static const TArray<FTDMonsterBodyFragment>& GetBodies(const UTDMonsterThinkSubsystem& Subsystem)
	{
		return Subsystem.Bodies;
	}

	static const TArray<FTDMonsterBrainFragment>& GetBrains(const UTDMonsterThinkSubsystem& Subsystem)
	{
		return Subsystem.Brains;
	}

	static const TArray<int32>& GetOrderedSlots(const UTDMonsterThinkSubsystem& Subsystem)
	{
		return Subsystem.OrderedSlots;
	}

	static FTDBrainInputs BuildInputs(const UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		return Subsystem.BuildInputs(SlotIndex, Subsystem.MakeContext(SlotIndex));
	}

	static bool MoveMonsterHome(UTDMonsterThinkSubsystem& Subsystem, const AActor* Monster, const FVector& HomeLocation)
	{
		const int32 SlotIndex = FindSlot(Subsystem, Monster);
		if (SlotIndex == INDEX_NONE)
		{
			return false;
		}
		FTDMonsterBodyFragment& Body = Subsystem.Bodies[SlotIndex];
		Body.HomeLocation = HomeLocation;
		Body.SpawnLocation = HomeLocation;
		return true;
	}
};

namespace
{
	constexpr float CircleAvoidanceTestStepSeconds = 1.f / 64.f;
	constexpr float CircleAvoidanceTestRadius = 40.f;
	constexpr float CircleAvoidanceTestSpeed = 400.f;
	constexpr int32 CircleAvoidanceTestSeed = 20261014;
	constexpr int32 CircleAvoidanceSimulationIdStride = 7919;
	constexpr float CircleAvoidanceSceneArrivalTolerance = 40.f;
	constexpr double CircleAvoidanceSceneOverlapTolerance = 0.1;
	constexpr double CircleAvoidanceStandingYieldDistance = 5.0;
	constexpr double CircleAvoidanceTestNumericTolerance = 1e-6;
	constexpr float CircleAvoidanceTestSpeedScaleTolerance = 1e-4f;
	constexpr double CircleAvoidanceTestTimeToCollisionSeconds = 0.25;
	constexpr double CircleAvoidanceTestNearGap = 15.0;

	struct FTDCircleAvoidanceTestWorld
	{
		FTDCircleAvoidanceTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDCircleAvoidanceTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDCircleAvoidanceTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ATDMonsterCharacter* SpawnMonster(const FName DefinitionId, const FVector& Location)
		{
			UTDMonsterSpeciesAsset* Species = NewObject<UTDMonsterSpeciesAsset>(World);
			Species->DefinitionId = DefinitionId;
			const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
			ATDMonsterCharacter* Monster = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
			Monster->SetSpecies(Species);
			Monster->GetMoverComponent()->StartingMovementMode = DefaultModeNames::Flying;
			Monster->FinishSpawning(SpawnTransform);
			return Monster;
		}

		void TickOneStep()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, CircleAvoidanceTestStepSeconds);
		}

		UWorld* World = nullptr;
	};

	uint32 MakeCircleAvoidanceSimulationId(const int32 Index, const int32 Count)
	{
		return static_cast<uint32>((static_cast<int64>(Index) * CircleAvoidanceSimulationIdStride) % Count) + 1;
	}

	TArray<FTDMonsterBodyFragment> MakeCircleAvoidanceTestBodies(FRandomStream& Stream, const int32 Count, const float AreaSide, const bool bShouldIncludeLargeBodies)
	{
		TArray<FTDMonsterBodyFragment> Bodies;
		Bodies.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FTDMonsterBodyFragment& Body = Bodies[Index];
			Body.SimulationId = MakeCircleAvoidanceSimulationId(Index, Count);
			Body.bIsActive = Stream.FRand() > 0.05f;
			Body.Location = FVector(Stream.FRandRange(0.f, AreaSide), Stream.FRandRange(0.f, AreaSide), 0.f);
			Body.Radius = Stream.FRandRange(28.f, 58.f);
			if (bShouldIncludeLargeBodies && Index % 50 == 0)
			{
				Body.Radius = Index % 100 == 0 ? 700.f : 150.f;
			}
		}
		return Bodies;
	}

	struct FTDCircleAvoidanceLattice
	{
		TArray<FTDMonsterBodyFragment> Bodies;
		int32 AvoidanceTieCenterSlot = INDEX_NONE;
		int32 AvoidanceReachProbeSlot = INDEX_NONE;
		int32 AvoidanceReachBoundarySlot = INDEX_NONE;
		int32 AllyTieCenterSlot = INDEX_NONE;
		int32 AllyReachProbeSlot = INDEX_NONE;
		int32 AllyReachBoundarySlot = INDEX_NONE;
	};

	void AddCircleAvoidanceTieRing(TArray<FVector>& Locations, const FVector& Center, const double ShortLeg, const double LongLeg, const double Hypotenuse)
	{
		for (const double SignX : { -1.0, 1.0 })
		{
			for (const double SignY : { -1.0, 1.0 })
			{
				Locations.Add(Center + FVector(SignX * ShortLeg, SignY * LongLeg, 0.0));
				Locations.Add(Center + FVector(SignX * LongLeg, SignY * ShortLeg, 0.0));
			}
			Locations.Add(Center + FVector(SignX * Hypotenuse, 0.0, 0.0));
			Locations.Add(Center + FVector(0.0, SignX * Hypotenuse, 0.0));
		}
	}

	FTDCircleAvoidanceLattice MakeCircleAvoidanceLattice()
	{
		const double AvoidanceReach = FTDNeighborGrid::AvoidanceSurfaceGap + 2.0 * CircleAvoidanceTestRadius;
		const double AllyReach = UTDMonsterThinkSubsystem::NeighborRadius;
		const FVector AvoidanceTieCenter(0.0, 0.0, 0.0);
		const FVector AvoidanceReachProbe(3000.0, 0.0, 0.0);
		const FVector AllyTieCenter(10000.0, 0.0, 0.0);
		const FVector AllyReachProbe(20000.0, 0.0, 0.0);
		FTDCircleAvoidanceLattice Lattice;
		TArray<FVector> Locations;
		Lattice.AvoidanceTieCenterSlot = Locations.Add(AvoidanceTieCenter);
		AddCircleAvoidanceTieRing(Locations, AvoidanceTieCenter, 150.0, 200.0, 250.0);
		Lattice.AvoidanceReachProbeSlot = Locations.Add(AvoidanceReachProbe);
		Lattice.AvoidanceReachBoundarySlot = Locations.Add(AvoidanceReachProbe + FVector(AvoidanceReach, 0.0, 0.0));
		Locations.Add(AvoidanceReachProbe + FVector(0.0, AvoidanceReach + 1.0, 0.0));
		Lattice.AllyTieCenterSlot = Locations.Add(AllyTieCenter);
		AddCircleAvoidanceTieRing(Locations, AllyTieCenter, 300.0, 400.0, 500.0);
		Lattice.AllyReachProbeSlot = Locations.Add(AllyReachProbe);
		Lattice.AllyReachBoundarySlot = Locations.Add(AllyReachProbe + FVector(0.0, AllyReach, 0.0));
		Locations.Add(AllyReachProbe + FVector(AllyReach + 1.0, 0.0, 0.0));
		Lattice.Bodies.SetNum(Locations.Num());
		for (int32 Index = 0; Index < Locations.Num(); ++Index)
		{
			FTDMonsterBodyFragment& Body = Lattice.Bodies[Index];
			Body.SimulationId = MakeCircleAvoidanceSimulationId(Index, Locations.Num());
			Body.bIsActive = true;
			Body.Location = Locations[Index];
			Body.Radius = CircleAvoidanceTestRadius;
		}
		return Lattice;
	}

	TArray<int32> MakeCircleAvoidanceSimulationOrder(const TArray<FTDMonsterBodyFragment>& Bodies)
	{
		TArray<int32> Order;
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Order.Sort([&Bodies](const int32 Left, const int32 Right)
		{
			return Bodies[Left].SimulationId < Bodies[Right].SimulationId;
		});
		return Order;
	}

	struct FTDBruteForceNeighbor
	{
		double SortDistance = 0.0;
		uint32 SimulationId = 0;
		int32 SlotIndex = INDEX_NONE;
	};

	void SortAndTruncateBruteForceNeighbors(TArray<FTDBruteForceNeighbor>& Neighbors, const int32 MaxCount)
	{
		Neighbors.Sort([](const FTDBruteForceNeighbor& Left, const FTDBruteForceNeighbor& Right)
		{
			return Left.SortDistance < Right.SortDistance || (Left.SortDistance == Right.SortDistance && Left.SimulationId < Right.SimulationId);
		});
		if (Neighbors.Num() > MaxCount)
		{
			Neighbors.SetNum(MaxCount);
		}
	}

	TArray<FTDBruteForceNeighbor> GatherBruteForceAvoidanceNeighbors(const TArray<FTDMonsterBodyFragment>& Bodies, const int32 SelfSlot)
	{
		TArray<FTDBruteForceNeighbor> Neighbors;
		const FTDMonsterBodyFragment& Self = Bodies[SelfSlot];
		for (int32 OtherSlot = 0; OtherSlot < Bodies.Num(); ++OtherSlot)
		{
			const FTDMonsterBodyFragment& Other = Bodies[OtherSlot];
			if (OtherSlot == SelfSlot || !Other.bIsActive)
			{
				continue;
			}
			const double Reach = FTDNeighborGrid::AvoidanceSurfaceGap + Self.Radius + Other.Radius;
			const double DistanceSquared = FVector::DistSquared2D(Self.Location, Other.Location);
			if (DistanceSquared > Reach * Reach)
			{
				continue;
			}
			Neighbors.Add({ DistanceSquared, Other.SimulationId, OtherSlot });
		}
		SortAndTruncateBruteForceNeighbors(Neighbors, FTDNeighborGrid::MaxAvoidanceNeighbors);
		return Neighbors;
	}

	TArray<FTDBruteForceNeighbor> GatherBruteForceAllyNeighbors(const TArray<FTDMonsterBodyFragment>& Bodies, const TArray<int32>& Teams, const int32 SelfSlot)
	{
		TArray<FTDBruteForceNeighbor> Neighbors;
		const FTDMonsterBodyFragment& Self = Bodies[SelfSlot];
		for (int32 OtherSlot = 0; OtherSlot < Bodies.Num(); ++OtherSlot)
		{
			if (OtherSlot == SelfSlot || !Bodies[OtherSlot].bIsActive || Teams[OtherSlot] != Teams[SelfSlot])
			{
				continue;
			}
			const float Distance = FVector::Dist2D(Self.Location, Bodies[OtherSlot].Location);
			if (Distance > UTDMonsterThinkSubsystem::NeighborRadius)
			{
				continue;
			}
			Neighbors.Add({ Distance, Bodies[OtherSlot].SimulationId, OtherSlot });
		}
		SortAndTruncateBruteForceNeighbors(Neighbors, FTDBrainInputs::MaxNeighbors);
		return Neighbors;
	}

	bool AreNeighborListsEqual(const FTDNeighborCandidateList& GridNeighbors, const TArray<FTDBruteForceNeighbor>& BruteForceNeighbors)
	{
		if (GridNeighbors.Num() != BruteForceNeighbors.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < GridNeighbors.Num(); ++Index)
		{
			const bool bIsSame = GridNeighbors[Index].SlotIndex == BruteForceNeighbors[Index].SlotIndex
				&& GridNeighbors[Index].SimulationId == BruteForceNeighbors[Index].SimulationId
				&& GridNeighbors[Index].SortDistance == BruteForceNeighbors[Index].SortDistance;
			if (!bIsSame)
			{
				return false;
			}
		}
		return true;
	}

	bool IsFullDistanceTie(const TArray<FTDBruteForceNeighbor>& Neighbors, const int32 MaxCount)
	{
		if (Neighbors.Num() != MaxCount)
		{
			return false;
		}
		for (const FTDBruteForceNeighbor& Neighbor : Neighbors)
		{
			if (Neighbor.SortDistance != Neighbors[0].SortDistance)
			{
				return false;
			}
		}
		return true;
	}

	bool IsOnlyNeighbor(const TArray<FTDBruteForceNeighbor>& Neighbors, const int32 SlotIndex)
	{
		return Neighbors.Num() == 1 && Neighbors[0].SlotIndex == SlotIndex;
	}

	FTDAvoidanceObstacle MakeCircleAvoidanceTestObstacle(const FVector2D& Location, const FVector2D& Velocity, const uint32 SimulationId, const bool bIsMonster = true)
	{
		FTDAvoidanceObstacle Obstacle;
		Obstacle.Location = Location;
		Obstacle.Velocity = Velocity;
		Obstacle.Radius = CircleAvoidanceTestRadius;
		Obstacle.SimulationId = SimulationId;
		Obstacle.bIsMonster = bIsMonster;
		return Obstacle;
	}

	FTDAvoidanceAgent MakeCircleAvoidanceTestAgent(const FVector2D& Location, const FVector2D& PreferredVelocity, const uint32 SimulationId)
	{
		FTDAvoidanceAgent Agent;
		Agent.Location = Location;
		Agent.Radius = CircleAvoidanceTestRadius;
		Agent.PreferredVelocity = PreferredVelocity;
		Agent.PreviousCommandVelocity = PreferredVelocity;
		Agent.MaxSpeed = CircleAvoidanceTestSpeed;
		Agent.SimulationId = SimulationId;
		return Agent;
	}

	struct FTDCircleAvoidanceTestAgent
	{
		FVector2D Location = FVector2D::ZeroVector;
		FVector2D Goal = FVector2D::ZeroVector;
		FVector2D PreviousCommandVelocity = FVector2D::ZeroVector;
		int32 PreviousCandidateIndex = INDEX_NONE;
		uint32 SimulationId = 0;
	};

	FVector2D MakeTestAgentPreferredVelocity(const FTDCircleAvoidanceTestAgent& TestAgent)
	{
		const FVector2D ToGoal = TestAgent.Goal - TestAgent.Location;
		const double Distance = ToGoal.Size();
		return Distance <= 1.0 ? FVector2D::ZeroVector : ToGoal / Distance * CircleAvoidanceTestSpeed;
	}

	FTDAvoidanceCommand ComputeTestAgentCommand(const TArray<FTDCircleAvoidanceTestAgent>& TestAgents, const int32 AgentIndex)
	{
		const FTDCircleAvoidanceTestAgent& Self = TestAgents[AgentIndex];
		FTDAvoidanceCommand Command;
		Command.CandidateIndex = INDEX_NONE;
		const FVector2D PreferredVelocity = MakeTestAgentPreferredVelocity(Self);
		if (PreferredVelocity.IsZero())
		{
			return Command;
		}
		FTDNeighborCandidateList Candidates;
		for (int32 OtherIndex = 0; OtherIndex < TestAgents.Num(); ++OtherIndex)
		{
			const double Reach = FTDNeighborGrid::AvoidanceSurfaceGap + 2.0 * CircleAvoidanceTestRadius;
			const double DistanceSquared = FVector2D::DistSquared(Self.Location, TestAgents[OtherIndex].Location);
			if (OtherIndex != AgentIndex && DistanceSquared <= Reach * Reach)
			{
				Candidates.Add({ DistanceSquared, TestAgents[OtherIndex].SimulationId, OtherIndex });
			}
		}
		FTDNeighborGrid::KeepNearestNeighbors(Candidates, FTDNeighborGrid::MaxAvoidanceNeighbors);
		FTDAvoidanceObstacleList Obstacles;
		for (const FTDNeighborCandidate& Candidate : Candidates)
		{
			const FTDCircleAvoidanceTestAgent& Other = TestAgents[Candidate.SlotIndex];
			Obstacles.Add(MakeCircleAvoidanceTestObstacle(Other.Location, Other.PreviousCommandVelocity, Other.SimulationId));
		}
		FTDAvoidanceAgent Agent = MakeCircleAvoidanceTestAgent(Self.Location, PreferredVelocity, Self.SimulationId);
		Agent.PreviousCommandVelocity = Self.PreviousCommandVelocity;
		Agent.PreviousCandidateIndex = Self.PreviousCandidateIndex;
		return FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, Obstacles);
	}

	TArray<FTDAvoidanceCommand> StepTestAgents(TArray<FTDCircleAvoidanceTestAgent>& TestAgents)
	{
		TArray<FTDAvoidanceCommand> Commands;
		Commands.SetNum(TestAgents.Num());
		for (int32 AgentIndex = 0; AgentIndex < TestAgents.Num(); ++AgentIndex)
		{
			Commands[AgentIndex] = ComputeTestAgentCommand(TestAgents, AgentIndex);
		}
		for (int32 AgentIndex = 0; AgentIndex < TestAgents.Num(); ++AgentIndex)
		{
			const FVector2D Velocity = Commands[AgentIndex].Velocity.Size() > FTDCircleAvoidance::MinimumCommandSpeed ? Commands[AgentIndex].Velocity : FVector2D::ZeroVector;
			TestAgents[AgentIndex].PreviousCommandVelocity = Velocity;
			TestAgents[AgentIndex].PreviousCandidateIndex = Commands[AgentIndex].CandidateIndex;
			TestAgents[AgentIndex].Location += Velocity * CircleAvoidanceTestStepSeconds;
		}
		return Commands;
	}

	double MeasureTestAgentMinimumGap(const TArray<FTDCircleAvoidanceTestAgent>& TestAgents)
	{
		double MinimumGap = TNumericLimits<double>::Max();
		for (int32 Left = 0; Left < TestAgents.Num(); ++Left)
		{
			for (int32 Right = Left + 1; Right < TestAgents.Num(); ++Right)
			{
				MinimumGap = FMath::Min(MinimumGap, FVector2D::Distance(TestAgents[Left].Location, TestAgents[Right].Location) - 2.0 * CircleAvoidanceTestRadius);
			}
		}
		return MinimumGap;
	}

	struct FTDCircleAvoidanceSceneBody
	{
		FVector Start = FVector::ZeroVector;
		FVector Home = FVector::ZeroVector;
		bool bIsPasser = false;
	};

	struct FTDCircleAvoidanceSceneReport
	{
		bool bIsSetupValid = false;
		int32 Steps = 0;
		int32 ContactSteps = 0;
		double MaxOverlap = 0.0;
		double MinGap = TNumericLimits<double>::Max();
		int32 Passers = 0;
		int32 ReachedPassers = 0;
		double MaxStandingDisplacement = 0.0;
		int32 YieldSlotSteps = 0;
		TArray<FVector> FinalLocations;

		double GetContactStepRatio() const
		{
			return Steps > 0 ? static_cast<double>(ContactSteps) / Steps : 0.0;
		}

		FString Describe() const
		{
			return FString::Printf(TEXT("steps %d, contact step ratio %.4f, max overlap %.2f cm, min gap %.2f cm, reached %d/%d, max standing displacement %.1f cm, yield slot steps %d"),
				Steps, GetContactStepRatio(), MaxOverlap, MinGap, ReachedPassers, Passers, MaxStandingDisplacement, YieldSlotSteps);
		}
	};

	void MeasureSceneStep(const TArray<ATDMonsterCharacter*>& Monsters, const TArray<FTDCircleAvoidanceSceneBody>& SceneBodies, FTDCircleAvoidanceSceneReport& Report)
	{
		bool bHasContact = false;
		for (int32 Left = 0; Left < Monsters.Num(); ++Left)
		{
			const FVector LeftLocation = Monsters[Left]->GetActorLocation();
			if (!SceneBodies[Left].bIsPasser)
			{
				Report.MaxStandingDisplacement = FMath::Max(Report.MaxStandingDisplacement, FVector::Dist2D(LeftLocation, SceneBodies[Left].Home));
			}
			for (int32 Right = Left + 1; Right < Monsters.Num(); ++Right)
			{
				const double Gap = FVector::Dist2D(LeftLocation, Monsters[Right]->GetActorLocation()) - Monsters[Left]->GetBodyRadius() - Monsters[Right]->GetBodyRadius();
				Report.MinGap = FMath::Min(Report.MinGap, Gap);
				Report.MaxOverlap = FMath::Max(Report.MaxOverlap, -Gap);
				bHasContact |= Gap < FTDCircleAvoidance::TouchGap;
			}
		}
		Report.ContactSteps += bHasContact ? 1 : 0;
	}

	FTDCircleAvoidanceSceneReport RunCircleAvoidanceScene(const TArray<FTDCircleAvoidanceSceneBody>& SceneBodies, const float Seconds, const int32 AvoidanceMode)
	{
		FTDCircleAvoidanceSceneReport Report;
		IConsoleVariable* ModeVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.MonsterAI.LocalAvoidance"));
		if (!ModeVariable)
		{
			return Report;
		}
		const int32 PreviousMode = ModeVariable->GetInt();
		ModeVariable->Set(AvoidanceMode, ECVF_SetByCode);
		{
			FTDCircleAvoidanceTestWorld Fixture;
			UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
			TArray<ATDMonsterCharacter*> Monsters;
			bool bAreHomesSet = ThinkSubsystem != nullptr;
			for (const FTDCircleAvoidanceSceneBody& SceneBody : SceneBodies)
			{
				ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), SceneBody.Start);
				Monsters.Add(Monster);
				bAreHomesSet = bAreHomesSet && Monster && FTDMonsterThinkSubsystemTestAccess::MoveMonsterHome(*ThinkSubsystem, Monster, SceneBody.Home);
				Report.Passers += SceneBody.bIsPasser ? 1 : 0;
			}
			Report.bIsSetupValid = bAreHomesSet && ThinkSubsystem->GetActiveMonsterCount() == SceneBodies.Num();
			if (Report.bIsSetupValid)
			{
				Report.Steps = FMath::RoundToInt(Seconds / CircleAvoidanceTestStepSeconds);
				uint64 LastCountedStepIndex = ThinkSubsystem->GetStepIndex();
				for (int32 Step = 0; Step < Report.Steps; ++Step)
				{
					Fixture.TickOneStep();
					MeasureSceneStep(Monsters, SceneBodies, Report);
					if (ThinkSubsystem->GetStepIndex() != LastCountedStepIndex)
					{
						LastCountedStepIndex = ThinkSubsystem->GetStepIndex();
						Report.YieldSlotSteps += FTDMonsterThinkSubsystemTestAccess::CountSlotsYieldingLastStep(*ThinkSubsystem);
					}
				}
				for (int32 Index = 0; Index < Monsters.Num(); ++Index)
				{
					const FVector FinalLocation = Monsters[Index]->GetActorLocation();
					Report.FinalLocations.Add(FinalLocation);
					const bool bHasReached = FVector::Dist2D(FinalLocation, SceneBodies[Index].Home) <= UTDMonsterThinkSubsystem::HomeArrivalRadius + CircleAvoidanceSceneArrivalTolerance;
					Report.ReachedPassers += SceneBodies[Index].bIsPasser && bHasReached ? 1 : 0;
				}
			}
		}
		ModeVariable->Set(PreviousMode, ECVF_SetByCode);
		return Report;
	}

	TArray<FTDCircleAvoidanceSceneBody> MakeCrossingStreamsScene()
	{
		TArray<FTDCircleAvoidanceSceneBody> SceneBodies;
		for (int32 Lane = 0; Lane < 6; ++Lane)
		{
			const float LaneOffset = (Lane - 2.5f) * 130.f;
			SceneBodies.Add({ FVector(-1000.f, LaneOffset, 0.f), FVector(1000.f, LaneOffset, 0.f), true });
			SceneBodies.Add({ FVector(LaneOffset, -1000.f, 0.f), FVector(LaneOffset, 1000.f, 0.f), true });
		}
		return SceneBodies;
	}

	TArray<FTDCircleAvoidanceSceneBody> MakeStandingGroupScene()
	{
		TArray<FTDCircleAvoidanceSceneBody> SceneBodies;
		for (const float Column : { -130.f, 0.f, 130.f })
		{
			for (const float Row : { -65.f, 65.f })
			{
				const FVector Home(Column, Row, 0.f);
				SceneBodies.Add({ Home, Home, false });
			}
		}
		const float Lanes[] = { -65.f, 0.f, 65.f };
		for (int32 LaneIndex = 0; LaneIndex < 3; ++LaneIndex)
		{
			SceneBodies.Add({ FVector(-900.f - 150.f * LaneIndex, Lanes[LaneIndex], 0.f), FVector(900.f, Lanes[LaneIndex], 0.f), true });
		}
		return SceneBodies;
	}

	float GetSlotMoveSpeed(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex)
	{
		const FTDMonsterBrainFragment& Brain = FTDMonsterThinkSubsystemTestAccess::GetMutableBrain(Subsystem, SlotIndex);
		return Brain.Definition.IsValid() ? Brain.Definition->GetStatsForPhase(Brain.PhaseIndex).MoveSpeed : 0.f;
	}

	void SetSlotMotion(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex, const FVector2D& Direction, const float SpeedScale, const FVector2D& PreviousCommandVelocity)
	{
		FTDMonsterMoveIntent& Intent = FTDMonsterThinkSubsystemTestAccess::GetMutableIntent(Subsystem, SlotIndex);
		Intent.Direction = Direction;
		Intent.SpeedScale = SpeedScale;
		FTDMonsterThinkSubsystemTestAccess::GetMutableAvoidance(Subsystem, SlotIndex).PreviousCommandVelocity = PreviousCommandVelocity;
	}

	void SetSlotEngagement(UTDMonsterThinkSubsystem& Subsystem, const int32 SlotIndex, const ETDMonsterEngagement Engagement, AActor* Target)
	{
		FTDMonsterBrainFragment& Brain = FTDMonsterThinkSubsystemTestAccess::GetMutableBrain(Subsystem, SlotIndex);
		Brain.Engagement = Engagement;
		Brain.Target = Target;
	}

	int32 CountAvoidanceSnapshotMismatches(const FTDCircleAvoidanceSnapshot& Expected, const FTDCircleAvoidanceSnapshot& Actual, const TArray<int32>& Slots)
	{
		int32 Mismatches = 0;
		for (const int32 SlotIndex : Slots)
		{
			const FTDMonsterMoveIntent& ExpectedIntent = Expected.Intents[SlotIndex];
			const FTDMonsterMoveIntent& ActualIntent = Actual.Intents[SlotIndex];
			const FTDMonsterAvoidanceFragment& ExpectedAvoidance = Expected.Avoidances[SlotIndex];
			const FTDMonsterAvoidanceFragment& ActualAvoidance = Actual.Avoidances[SlotIndex];
			const bool bIsSame = ExpectedIntent.Direction == ActualIntent.Direction
				&& ExpectedIntent.SpeedScale == ActualIntent.SpeedScale
				&& ExpectedIntent.bShouldFaceTarget == ActualIntent.bShouldFaceTarget
				&& ExpectedAvoidance.PreviousCommandVelocity == ActualAvoidance.PreviousCommandVelocity
				&& ExpectedAvoidance.PreviousCandidateIndex == ActualAvoidance.PreviousCandidateIndex
				&& ExpectedAvoidance.SlowedWaitStepCount == ActualAvoidance.SlowedWaitStepCount;
			Mismatches += bIsSame ? 0 : 1;
		}
		return Mismatches;
	}

	int32 CountChangedIntents(const FTDCircleAvoidanceSnapshot& Before, const FTDCircleAvoidanceSnapshot& After, const TArray<int32>& Slots)
	{
		int32 ChangedIntents = 0;
		for (const int32 SlotIndex : Slots)
		{
			const bool bIsSame = Before.Intents[SlotIndex].Direction == After.Intents[SlotIndex].Direction && Before.Intents[SlotIndex].SpeedScale == After.Intents[SlotIndex].SpeedScale;
			ChangedIntents += bIsSame ? 0 : 1;
		}
		return ChangedIntents;
	}

	TArray<int32> MakeShuffledOrder(const TArray<int32>& Order, FRandomStream& Stream)
	{
		TArray<int32> Shuffled = Order;
		for (int32 Index = Shuffled.Num() - 1; Index > 0; --Index)
		{
			Shuffled.Swap(Index, Stream.RandRange(0, Index));
		}
		return Shuffled;
	}

	TArray<int32> MakeReversedOrder(const TArray<int32>& Order)
	{
		TArray<int32> Reversed;
		for (int32 Index = Order.Num() - 1; Index >= 0; --Index)
		{
			Reversed.Add(Order[Index]);
		}
		return Reversed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDNeighborGridMatchesBruteForceTest, "TDGame.MonsterAI.Grid.MatchesBruteForce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDNeighborGridMatchesBruteForceTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream(CircleAvoidanceTestSeed);
	FTDNeighborGrid Grid;
	int32 Comparisons = 0;
	int32 Mismatches = 0;
	int32 FullLists = 0;
	FString FirstMismatch;
	for (int32 Iteration = 0; Iteration < 100; ++Iteration)
	{
		const TArray<FTDMonsterBodyFragment> Bodies = MakeCircleAvoidanceTestBodies(Stream, 300, Stream.FRandRange(1500.f, 8000.f), Iteration % 2 == 0);
		Grid.Rebuild(Bodies, MakeCircleAvoidanceSimulationOrder(Bodies));
		for (int32 SlotIndex = 0; SlotIndex < Bodies.Num(); ++SlotIndex)
		{
			if (!Bodies[SlotIndex].bIsActive)
			{
				continue;
			}
			FTDNeighborCandidateList GridNeighbors;
			Grid.GatherAvoidanceNeighbors(Bodies, SlotIndex, GridNeighbors);
			const TArray<FTDBruteForceNeighbor> BruteForceNeighbors = GatherBruteForceAvoidanceNeighbors(Bodies, SlotIndex);
			++Comparisons;
			FullLists += GridNeighbors.Num() == FTDNeighborGrid::MaxAvoidanceNeighbors ? 1 : 0;
			if (AreNeighborListsEqual(GridNeighbors, BruteForceNeighbors))
			{
				continue;
			}
			++Mismatches;
			if (FirstMismatch.IsEmpty())
			{
				FirstMismatch = FString::Printf(TEXT("iteration %d slot %d grid %d brute %d"), Iteration, SlotIndex, GridNeighbors.Num(), BruteForceNeighbors.Num());
			}
		}
	}
	AddInfo(FString::Printf(TEXT("avoidance neighbor comparisons %d, full lists of %d %d, mismatches %d"), Comparisons, FTDNeighborGrid::MaxAvoidanceNeighbors, FullLists, Mismatches));
	TestTrue(TEXT("Some agents have more candidates than the neighbor limit"), FullLists > 0);
	TestEqual(FString::Printf(TEXT("Grid avoidance neighbors equal brute force (%s)"), *FirstMismatch), Mismatches, 0);

	const FTDCircleAvoidanceLattice Lattice = MakeCircleAvoidanceLattice();
	Grid.Rebuild(Lattice.Bodies, MakeCircleAvoidanceSimulationOrder(Lattice.Bodies));
	int32 LatticeMismatches = 0;
	for (int32 SlotIndex = 0; SlotIndex < Lattice.Bodies.Num(); ++SlotIndex)
	{
		FTDNeighborCandidateList GridNeighbors;
		Grid.GatherAvoidanceNeighbors(Lattice.Bodies, SlotIndex, GridNeighbors);
		LatticeMismatches += AreNeighborListsEqual(GridNeighbors, GatherBruteForceAvoidanceNeighbors(Lattice.Bodies, SlotIndex)) ? 0 : 1;
	}
	TestTrue(TEXT("The lattice truncates a full distance tie by SimulationId"), IsFullDistanceTie(GatherBruteForceAvoidanceNeighbors(Lattice.Bodies, Lattice.AvoidanceTieCenterSlot), FTDNeighborGrid::MaxAvoidanceNeighbors));
	TestTrue(TEXT("A body exactly at the avoidance reach is the only neighbor of its probe"), IsOnlyNeighbor(GatherBruteForceAvoidanceNeighbors(Lattice.Bodies, Lattice.AvoidanceReachProbeSlot), Lattice.AvoidanceReachBoundarySlot));
	TestEqual(TEXT("Grid avoidance neighbors equal brute force on distance ties and reach boundaries"), LatticeMismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDNeighborGridBuildInputsUnchangedTest, "TDGame.MonsterAI.Grid.BuildInputsNeighborsUnchanged", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDNeighborGridBuildInputsUnchangedTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream(CircleAvoidanceTestSeed + 1);
	FTDNeighborGrid Grid;
	int32 SyntheticMismatches = 0;
	int32 SyntheticTruncations = 0;
	for (int32 Iteration = 0; Iteration < 20; ++Iteration)
	{
		const TArray<FTDMonsterBodyFragment> Bodies = MakeCircleAvoidanceTestBodies(Stream, 300, Stream.FRandRange(3000.f, 12000.f), false);
		TArray<int32> Teams;
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			Teams.Add(Stream.RandRange(1, 3));
		}
		Grid.Rebuild(Bodies, MakeCircleAvoidanceSimulationOrder(Bodies));
		for (int32 SlotIndex = 0; SlotIndex < Bodies.Num(); ++SlotIndex)
		{
			if (!Bodies[SlotIndex].bIsActive)
			{
				continue;
			}
			FTDNeighborCandidateList GridNeighbors;
			Grid.GatherNeighborsWithinDistance(Bodies, SlotIndex, UTDMonsterThinkSubsystem::NeighborRadius, FTDBrainInputs::MaxNeighbors, [&Teams, SlotIndex](const int32 OtherSlot)
			{
				return Teams[OtherSlot] == Teams[SlotIndex];
			}, GridNeighbors);
			const TArray<FTDBruteForceNeighbor> BruteForceNeighbors = GatherBruteForceAllyNeighbors(Bodies, Teams, SlotIndex);
			SyntheticTruncations += GridNeighbors.Num() == FTDBrainInputs::MaxNeighbors ? 1 : 0;
			SyntheticMismatches += AreNeighborListsEqual(GridNeighbors, BruteForceNeighbors) ? 0 : 1;
		}
	}
	AddInfo(FString::Printf(TEXT("synthetic ally neighbor lists: full %d, mismatches %d"), SyntheticTruncations, SyntheticMismatches));
	TestTrue(TEXT("Synthetic layouts exercise the neighbor limit"), SyntheticTruncations > 0);
	TestEqual(TEXT("Synthetic grid ally neighbors equal the legacy full scan"), SyntheticMismatches, 0);

	const FTDCircleAvoidanceLattice Lattice = MakeCircleAvoidanceLattice();
	TArray<int32> LatticeTeams;
	LatticeTeams.Init(1, Lattice.Bodies.Num());
	Grid.Rebuild(Lattice.Bodies, MakeCircleAvoidanceSimulationOrder(Lattice.Bodies));
	int32 LatticeMismatches = 0;
	for (int32 SlotIndex = 0; SlotIndex < Lattice.Bodies.Num(); ++SlotIndex)
	{
		FTDNeighborCandidateList GridNeighbors;
		Grid.GatherNeighborsWithinDistance(Lattice.Bodies, SlotIndex, UTDMonsterThinkSubsystem::NeighborRadius, FTDBrainInputs::MaxNeighbors, [&LatticeTeams, SlotIndex](const int32 OtherSlot)
		{
			return LatticeTeams[OtherSlot] == LatticeTeams[SlotIndex];
		}, GridNeighbors);
		LatticeMismatches += AreNeighborListsEqual(GridNeighbors, GatherBruteForceAllyNeighbors(Lattice.Bodies, LatticeTeams, SlotIndex)) ? 0 : 1;
	}
	TestTrue(TEXT("The lattice truncates a full ally distance tie by SimulationId"), IsFullDistanceTie(GatherBruteForceAllyNeighbors(Lattice.Bodies, LatticeTeams, Lattice.AllyTieCenterSlot), FTDBrainInputs::MaxNeighbors));
	TestTrue(TEXT("A body exactly at NeighborRadius is the only ally neighbor of its probe"), IsOnlyNeighbor(GatherBruteForceAllyNeighbors(Lattice.Bodies, LatticeTeams, Lattice.AllyReachProbeSlot), Lattice.AllyReachBoundarySlot));
	TestEqual(TEXT("Grid ally neighbors equal the legacy full scan on distance ties and the NeighborRadius boundary"), LatticeMismatches, 0);

	FTDCircleAvoidanceTestWorld Fixture;
	for (int32 Column = 0; Column < 8; ++Column)
	{
		for (int32 Row = 0; Row < 5; ++Row)
		{
			const FVector Location(Column * 400.f + Stream.FRandRange(-100.f, 100.f), Row * 400.f + Stream.FRandRange(-100.f, 100.f), 0.f);
			Fixture.SpawnMonster(TEXT("Goblin_Melee"), Location);
		}
	}
	const FVector LatticeSpawnOffset(0.0, 20000.0, 0.0);
	const ATDMonsterCharacter* SpawnedAllyTieCenter = nullptr;
	for (int32 LatticeIndex = Lattice.AllyTieCenterSlot; LatticeIndex < Lattice.Bodies.Num(); ++LatticeIndex)
	{
		const ATDMonsterCharacter* Monster = Fixture.SpawnMonster(TEXT("Goblin_Melee"), Lattice.Bodies[LatticeIndex].Location + LatticeSpawnOffset);
		SpawnedAllyTieCenter = LatticeIndex == Lattice.AllyTieCenterSlot ? Monster : SpawnedAllyTieCenter;
	}
	const int32 ExpectedMonsterCount = 40 + Lattice.Bodies.Num() - Lattice.AllyTieCenterSlot;
	Fixture.TickOneStep();
	const UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem) || !TestEqual(TEXT("All spawned monsters are registered"), ThinkSubsystem->GetActiveMonsterCount(), ExpectedMonsterCount))
	{
		return false;
	}
	const TArray<FTDMonsterBodyFragment>& Bodies = FTDMonsterThinkSubsystemTestAccess::GetBodies(*ThinkSubsystem);
	const TArray<FTDMonsterBrainFragment>& Brains = FTDMonsterThinkSubsystemTestAccess::GetBrains(*ThinkSubsystem);
	TArray<int32> Teams;
	for (const FTDMonsterBrainFragment& Brain : Brains)
	{
		Teams.Add(Brain.Definition.IsValid() ? Brain.Definition->Stats.Team : INDEX_NONE);
	}
	int32 SubsystemMismatches = 0;
	int32 SubsystemTruncations = 0;
	for (const int32 SlotIndex : FTDMonsterThinkSubsystemTestAccess::GetOrderedSlots(*ThinkSubsystem))
	{
		const FTDBrainInputs Inputs = FTDMonsterThinkSubsystemTestAccess::BuildInputs(*ThinkSubsystem, SlotIndex);
		const TArray<FTDBruteForceNeighbor> BruteForceNeighbors = GatherBruteForceAllyNeighbors(Bodies, Teams, SlotIndex);
		bool bIsSame = Inputs.NeighborCount == BruteForceNeighbors.Num();
		for (int32 NeighborIndex = 0; bIsSame && NeighborIndex < Inputs.NeighborCount; ++NeighborIndex)
		{
			bIsSame = Inputs.Neighbors[NeighborIndex].SimulationId == BruteForceNeighbors[NeighborIndex].SimulationId
				&& Inputs.Neighbors[NeighborIndex].Distance == static_cast<float>(BruteForceNeighbors[NeighborIndex].SortDistance);
		}
		SubsystemTruncations += Inputs.NeighborCount == FTDBrainInputs::MaxNeighbors ? 1 : 0;
		SubsystemMismatches += bIsSame ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("BuildInputs neighbor lists: full %d, mismatches %d"), SubsystemTruncations, SubsystemMismatches));
	TestTrue(TEXT("The spawned layout exercises the neighbor limit"), SubsystemTruncations > 0);
	TestEqual(TEXT("BuildInputs neighbors equal the legacy full scan"), SubsystemMismatches, 0);
	const int32 TieCenterSlot = FTDMonsterThinkSubsystemTestAccess::FindSlot(*ThinkSubsystem, SpawnedAllyTieCenter);
	bool bIsSpawnedTieFull = false;
	if (TieCenterSlot != INDEX_NONE)
	{
		const FTDBrainInputs Inputs = FTDMonsterThinkSubsystemTestAccess::BuildInputs(*ThinkSubsystem, TieCenterSlot);
		bIsSpawnedTieFull = Inputs.NeighborCount == FTDBrainInputs::MaxNeighbors;
		for (int32 NeighborIndex = 0; bIsSpawnedTieFull && NeighborIndex < Inputs.NeighborCount; ++NeighborIndex)
		{
			bIsSpawnedTieFull = Inputs.Neighbors[NeighborIndex].Distance == Inputs.Neighbors[0].Distance;
		}
	}
	TestTrue(TEXT("BuildInputs truncates a full distance tie around the spawned ring center"), bIsSpawnedTieFull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDNeighborGridSearchCostTest, "TDGame.MonsterAI.Grid.SearchCost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDNeighborGridSearchCostTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream(CircleAvoidanceTestSeed + 2);
	FTDNeighborGrid Grid;
	constexpr int32 Repeats = 10;
	AddInfo(TEXT("| agents | layout | brute force ms/step | grid build+query ms/step | avoidance us/agent | mean neighbors |"));
	for (const int32 Count : { 300, 1000 })
	{
		for (const float Spacing : { 400.f, 110.f })
		{
			const float AreaSide = FMath::Sqrt(static_cast<float>(Count)) * Spacing;
			TArray<FTDMonsterBodyFragment> Bodies = MakeCircleAvoidanceTestBodies(Stream, Count, AreaSide, false);
			TArray<FVector2D> Velocities;
			for (FTDMonsterBodyFragment& Body : Bodies)
			{
				Body.bIsActive = true;
				const float Angle = Stream.FRandRange(0.f, 2.f * PI);
				Velocities.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * CircleAvoidanceTestSpeed);
			}
			const TArray<int32> Order = MakeCircleAvoidanceSimulationOrder(Bodies);
			int32 Mismatches = 0;
			int32 NeighborTotal = 0;
			double BruteForceSeconds = 0.0;
			double GridSeconds = 0.0;
			double AvoidanceSeconds = 0.0;
			for (int32 Repeat = 0; Repeat < Repeats; ++Repeat)
			{
				TArray<TArray<FTDBruteForceNeighbor>> BruteForceLists;
				BruteForceLists.SetNum(Count);
				const double BruteForceStart = FPlatformTime::Seconds();
				for (int32 SlotIndex = 0; SlotIndex < Count; ++SlotIndex)
				{
					BruteForceLists[SlotIndex] = GatherBruteForceAvoidanceNeighbors(Bodies, SlotIndex);
				}
				BruteForceSeconds += FPlatformTime::Seconds() - BruteForceStart;
				TArray<FTDNeighborCandidateList> GridLists;
				GridLists.SetNum(Count);
				const double GridStart = FPlatformTime::Seconds();
				Grid.Rebuild(Bodies, Order);
				for (int32 SlotIndex = 0; SlotIndex < Count; ++SlotIndex)
				{
					Grid.GatherAvoidanceNeighbors(Bodies, SlotIndex, GridLists[SlotIndex]);
				}
				GridSeconds += FPlatformTime::Seconds() - GridStart;
				const double AvoidanceStart = FPlatformTime::Seconds();
				for (int32 SlotIndex = 0; SlotIndex < Count; ++SlotIndex)
				{
					FTDAvoidanceObstacleList Obstacles;
					for (const FTDNeighborCandidate& Neighbor : GridLists[SlotIndex])
					{
						const FTDMonsterBodyFragment& Other = Bodies[Neighbor.SlotIndex];
						FTDAvoidanceObstacle& Obstacle = Obstacles.Add_GetRef(MakeCircleAvoidanceTestObstacle(FVector2D(Other.Location.X, Other.Location.Y), Velocities[Neighbor.SlotIndex], Other.SimulationId));
						Obstacle.Radius = Other.Radius;
					}
					FTDAvoidanceAgent Agent = MakeCircleAvoidanceTestAgent(FVector2D(Bodies[SlotIndex].Location.X, Bodies[SlotIndex].Location.Y), Velocities[SlotIndex], Bodies[SlotIndex].SimulationId);
					Agent.Radius = Bodies[SlotIndex].Radius;
					FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, Obstacles);
				}
				AvoidanceSeconds += FPlatformTime::Seconds() - AvoidanceStart;
				for (int32 SlotIndex = 0; SlotIndex < Count; ++SlotIndex)
				{
					Mismatches += AreNeighborListsEqual(GridLists[SlotIndex], BruteForceLists[SlotIndex]) ? 0 : 1;
					NeighborTotal += GridLists[SlotIndex].Num();
				}
			}
			AddInfo(FString::Printf(TEXT("| %d | %s | %.3f | %.3f | %.3f | %.2f |"),
				Count,
				Spacing > 200.f ? TEXT("sparse") : TEXT("dense"),
				1000.0 * BruteForceSeconds / Repeats,
				1000.0 * GridSeconds / Repeats,
				1e6 * AvoidanceSeconds / (Repeats * Count),
				static_cast<double>(NeighborTotal) / (Repeats * Count)));
			TestEqual(FString::Printf(TEXT("Grid neighbors equal brute force for %d agents at %.0f cm spacing"), Count, Spacing), Mismatches, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceClearPathTest, "TDGame.MonsterAI.CircleAvoidance.ClearPathKeepsPreferredVelocity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceClearPathTest::RunTest(const FString& Parameters)
{
	const FTDAvoidanceAgent Agent = MakeCircleAvoidanceTestAgent(FVector2D::ZeroVector, FVector2D(CircleAvoidanceTestSpeed, 0.0), 2);
	FTDAvoidanceObstacleList Obstacles;
	Obstacles.Add(MakeCircleAvoidanceTestObstacle(FVector2D(0.0, 300.0), FVector2D::ZeroVector, 3));
	Obstacles.Add(MakeCircleAvoidanceTestObstacle(FVector2D(-300.0, 0.0), FVector2D(-100.0, 0.0), 4));
	Obstacles.Add(MakeCircleAvoidanceTestObstacle(FVector2D(900.0, 0.0), FVector2D::ZeroVector, 5));
	TestTrue(TEXT("Every obstacle is at least one horizon away"), FTDCircleAvoidance::ComputeMinimumTimeToCollision(Agent.Location, Agent.Radius, Agent.PreferredVelocity, Obstacles) >= FTDCircleAvoidance::TimeHorizonSeconds);
	const FTDAvoidanceCommand Command = FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, Obstacles);
	TestTrue(TEXT("The preferred velocity passes through unchanged"), Command.Velocity == Agent.PreferredVelocity);
	TestEqual(TEXT("The early exit reports candidate 0"), Command.CandidateIndex, 0);
	TestFalse(TEXT("A clear path is not a slowed wait"), Command.bIsSlowedWaiting);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceHeadOnTest, "TDGame.MonsterAI.CircleAvoidance.HeadOnPairTurnsSameSide", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceHeadOnTest::RunTest(const FString& Parameters)
{
	TArray<FTDCircleAvoidanceTestAgent> TestAgents;
	TestAgents.Add({ FVector2D(-400.0, 0.0), FVector2D(800.0, 0.0), FVector2D(CircleAvoidanceTestSpeed, 0.0), INDEX_NONE, 1 });
	TestAgents.Add({ FVector2D(400.0, 0.0), FVector2D(-800.0, 0.0), FVector2D(-CircleAvoidanceTestSpeed, 0.0), INDEX_NONE, 2 });
	double MinimumGap = MeasureTestAgentMinimumGap(TestAgents);
	int32 FirstTurnStep = INDEX_NONE;
	for (int32 Step = 0; Step < 256; ++Step)
	{
		const FVector2D FirstPreferred = MakeTestAgentPreferredVelocity(TestAgents[0]);
		const FVector2D SecondPreferred = MakeTestAgentPreferredVelocity(TestAgents[1]);
		const TArray<FTDAvoidanceCommand> Commands = StepTestAgents(TestAgents);
		MinimumGap = FMath::Min(MinimumGap, MeasureTestAgentMinimumGap(TestAgents));
		const bool bHasTurned = Commands[0].CandidateIndex > 0 || Commands[1].CandidateIndex > 0;
		if (FirstTurnStep != INDEX_NONE || !bHasTurned)
		{
			continue;
		}
		FirstTurnStep = Step;
		const double FirstTurn = FVector2D::CrossProduct(FirstPreferred, Commands[0].Velocity);
		const double SecondTurn = FVector2D::CrossProduct(SecondPreferred, Commands[1].Velocity);
		TestEqual(TEXT("Both agents pick the same candidate"), Commands[0].CandidateIndex, Commands[1].CandidateIndex);
		TestTrue(FString::Printf(TEXT("Both agents turn to the same side of their own heading (%.3f, %.3f)"), FirstTurn, SecondTurn), FirstTurn * SecondTurn > 0.0);
	}
	AddInfo(FString::Printf(TEXT("first turn step %d, minimum surface gap %.2f cm, final x %.1f and %.1f"), FirstTurnStep, MinimumGap, TestAgents[0].Location.X, TestAgents[1].Location.X));
	TestTrue(TEXT("The pair starts avoiding"), FirstTurnStep != INDEX_NONE);
	TestTrue(FString::Printf(TEXT("The pair never touches (%.2f cm)"), MinimumGap), MinimumGap >= FTDCircleAvoidance::TouchGap);
	TestTrue(TEXT("The pair passes each other"), TestAgents[0].Location.X > TestAgents[1].Location.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceOverlapTest, "TDGame.MonsterAI.CircleAvoidance.OverlapNeverApproaches", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceOverlapTest::RunTest(const FString& Parameters)
{
	const FTDAvoidanceAgent Agent = MakeCircleAvoidanceTestAgent(FVector2D::ZeroVector, FVector2D(CircleAvoidanceTestSpeed, 0.0), 2);
	const FTDAvoidanceObstacle Cases[] = {
		MakeCircleAvoidanceTestObstacle(FVector2D(50.0, 0.0), FVector2D::ZeroVector, 3),
		MakeCircleAvoidanceTestObstacle(FVector2D(60.0, 20.0), FVector2D(-100.0, 0.0), 4) };
	for (const FTDAvoidanceObstacle& Obstacle : Cases)
	{
		FTDAvoidanceObstacleList Obstacles;
		Obstacles.Add(Obstacle);
		const FTDAvoidanceCommand Command = FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, Obstacles);
		const FVector2D RelativeLocation = Obstacle.Location - Agent.Location;
		const double Approach = FVector2D::DotProduct(Command.Velocity - Obstacle.Velocity, RelativeLocation);
		AddInfo(FString::Printf(TEXT("obstacle %u: candidate %d, velocity (%.1f, %.1f), approach %.3f"), Obstacle.SimulationId, Command.CandidateIndex, Command.Velocity.X, Command.Velocity.Y, Approach));
		TestTrue(FString::Printf(TEXT("The overlapped agent does not approach obstacle %u"), Obstacle.SimulationId), Approach <= CircleAvoidanceTestNumericTolerance);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceStopTest, "TDGame.MonsterAI.CircleAvoidance.StopChoiceHasZeroSpeedScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceStopTest::RunTest(const FString& Parameters)
{
	const FTDAvoidanceAgent Agent = MakeCircleAvoidanceTestAgent(FVector2D::ZeroVector, FVector2D(CircleAvoidanceTestSpeed, 0.0), 2);
	FTDAvoidanceObstacleList Obstacles;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const double Angle = Index * PI / 4.0;
		Obstacles.Add(MakeCircleAvoidanceTestObstacle(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 60.0, FVector2D::ZeroVector, static_cast<uint32>(Index + 3)));
	}
	const FTDAvoidanceCommand Command = FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, Obstacles);
	TestEqual(TEXT("A fully surrounded agent picks the stop candidate"), Command.CandidateIndex, FTDCircleAvoidance::StopCandidateIndex);
	TestTrue(TEXT("The stop candidate has zero velocity"), Command.Velocity.IsZero());
	TestTrue(TEXT("Stopping is a slowed wait"), Command.bIsSlowedWaiting);
	FTDMonsterMoveIntent Intent;
	Intent.Direction = FVector2D(1.0, 0.0);
	Intent.SpeedScale = 1.f;
	const FVector2D AppliedVelocity = FTDCircleAvoidance::ApplyCommandToIntent(Command.Velocity, CircleAvoidanceTestSpeed, Intent);
	TestEqual(TEXT("A stop sets SpeedScale to zero"), Intent.SpeedScale, 0.f);
	TestTrue(TEXT("A stop keeps the preferred direction"), Intent.Direction == FVector2D(1.0, 0.0));
	TestTrue(TEXT("A stop records zero command velocity"), AppliedVelocity.IsZero());
	FTDMonsterMoveIntent YieldIntent;
	FTDCircleAvoidance::ApplyCommandToIntent(FVector2D(0.0, 200.0), CircleAvoidanceTestSpeed, YieldIntent);
	TestTrue(TEXT("A yield command sets its own direction"), YieldIntent.Direction.Equals(FVector2D(0.0, 1.0)));
	TestEqual(TEXT("A yield command at half speed sets SpeedScale 0.5"), YieldIntent.SpeedScale, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceEngagedTargetTest, "TDGame.MonsterAI.CircleAvoidance.EngagedTargetIsNotAnObstacle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceEngagedTargetTest::RunTest(const FString& Parameters)
{
	TArray<FTDAvoidanceObstacle> ExternalBodies;
	ExternalBodies.Add(MakeCircleAvoidanceTestObstacle(FVector2D(100.0, 0.0), FVector2D::ZeroVector, 0, false));
	ExternalBodies.Add(MakeCircleAvoidanceTestObstacle(FVector2D(0.0, 150.0), FVector2D::ZeroVector, 0, false));
	ExternalBodies.Add(MakeCircleAvoidanceTestObstacle(FVector2D(1000.0, 0.0), FVector2D::ZeroVector, 0, false));
	FTDAvoidanceObstacleList AllNearby;
	FTDCircleAvoidance::AppendNearbyExternalObstacles(FVector2D::ZeroVector, CircleAvoidanceTestRadius, ExternalBodies, INDEX_NONE, AllNearby);
	TestEqual(TEXT("Only external bodies within the surface gap are obstacles"), AllNearby.Num(), 2);
	FTDAvoidanceObstacleList WithoutTarget;
	FTDCircleAvoidance::AppendNearbyExternalObstacles(FVector2D::ZeroVector, CircleAvoidanceTestRadius, ExternalBodies, 0, WithoutTarget);
	if (!TestEqual(TEXT("The engaged target is excluded"), WithoutTarget.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("The remaining obstacle is the bystander"), WithoutTarget[0].Location == ExternalBodies[1].Location);
	FTDAvoidanceObstacleList TargetOnly;
	FTDCircleAvoidance::AppendNearbyExternalObstacles(FVector2D::ZeroVector, CircleAvoidanceTestRadius, MakeArrayView(ExternalBodies.GetData(), 1), 0, TargetOnly);
	const FTDAvoidanceAgent Agent = MakeCircleAvoidanceTestAgent(FVector2D::ZeroVector, FVector2D(CircleAvoidanceTestSpeed, 0.0), 2);
	const FTDAvoidanceCommand Command = FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, TargetOnly);
	TestTrue(TEXT("Walking straight at the excluded target keeps the preferred velocity"), Command.Velocity == Agent.PreferredVelocity);

	FTDCircleAvoidanceTestWorld Fixture;
	ATDMonsterCharacter* Walker = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	ATDCompanionCharacter* Companion = Fixture.World->SpawnActor<ATDCompanionCharacter>(FVector(1000.0, 0.0, 0.0), FRotator::ZeroRotator);
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem) || !TestNotNull(TEXT("Walker exists"), Walker) || !TestNotNull(TEXT("Companion exists"), Companion))
	{
		return false;
	}
	float CompanionRadius = 0.f;
	float CompanionHalfHeight = 0.f;
	Companion->GetSimpleCollisionCylinder(CompanionRadius, CompanionHalfHeight);
	const double CompanionDistance = Walker->GetBodyRadius() + CompanionRadius + CircleAvoidanceTestNearGap;
	Companion->SetActorLocation(FVector(CompanionDistance, 0.0, Companion->GetActorLocation().Z));
	const int32 WalkerSlot = FTDMonsterThinkSubsystemTestAccess::FindSlot(*ThinkSubsystem, Walker);
	if (!TestTrue(TEXT("Walker has a slot"), WalkerSlot != INDEX_NONE) || !TestEqual(TEXT("The live companion is gathered as an external body"), FTDMonsterThinkSubsystemTestAccess::CountExternalBodies(*ThinkSubsystem), 1))
	{
		return false;
	}
	const FVector2D Heading(1.0, 0.0);
	SetSlotMotion(*ThinkSubsystem, WalkerSlot, Heading, 1.f, Heading * GetSlotMoveSpeed(*ThinkSubsystem, WalkerSlot));
	const FTDCircleAvoidanceSnapshot Initial = FTDMonsterThinkSubsystemTestAccess::CaptureSnapshot(*ThinkSubsystem);
	const TArray<int32> Order = FTDMonsterThinkSubsystemTestAccess::GetOrderedSlots(*ThinkSubsystem);
	struct FTDEngagedTargetCase
	{
		const TCHAR* Name;
		ETDMonsterEngagement Engagement;
		AActor* Target;
		bool bShouldKeepPreferredVelocity;
	};
	const FTDEngagedTargetCase Cases[] = {
		{ TEXT("engaged with the companion as target"), ETDMonsterEngagement::Engaged, Companion, true },
		{ TEXT("at home with the companion as a stale target"), ETDMonsterEngagement::Home, Companion, false },
		{ TEXT("engaged without a target"), ETDMonsterEngagement::Engaged, nullptr, false } };
	for (const FTDEngagedTargetCase& Case : Cases)
	{
		FTDMonsterThinkSubsystemTestAccess::RestoreSnapshot(*ThinkSubsystem, Initial);
		SetSlotEngagement(*ThinkSubsystem, WalkerSlot, Case.Engagement, Case.Target);
		FTDMonsterThinkSubsystemTestAccess::ApplyCircleAvoidanceInOrder(*ThinkSubsystem, Order);
		const FTDMonsterMoveIntent& Intent = FTDMonsterThinkSubsystemTestAccess::GetMutableIntent(*ThinkSubsystem, WalkerSlot);
		const int32 CandidateIndex = FTDMonsterThinkSubsystemTestAccess::GetMutableAvoidance(*ThinkSubsystem, WalkerSlot).PreviousCandidateIndex;
		const bool bHasKeptPreferredVelocity = Intent.Direction == Heading && Intent.SpeedScale == 1.f && CandidateIndex == 0;
		AddInfo(FString::Printf(TEXT("walker %s: candidate %d, direction (%.3f, %.3f), speed scale %.3f"), Case.Name, CandidateIndex, Intent.Direction.X, Intent.Direction.Y, Intent.SpeedScale));
		TestTrue(FString::Printf(TEXT("A walker %s keeps its preferred velocity only when the companion is its engaged target"), Case.Name), bHasKeptPreferredVelocity == Case.bShouldKeepPreferredVelocity);
	}
	FTDMonsterThinkSubsystemTestAccess::RestoreSnapshot(*ThinkSubsystem, Initial);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceYieldEligibilityTest, "TDGame.MonsterAI.CircleAvoidance.HoldingAttackerDoesNotYield", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceYieldEligibilityTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("An idle monster at home yields"), FTDCircleAvoidance::CanYield(ETDMonsterFsmState::Idle, false, false));
	TestTrue(TEXT("A monster holding inside its move band without a target yields"), FTDCircleAvoidance::CanYield(ETDMonsterFsmState::Move, false, false));
	TestFalse(TEXT("An engaged attacker holding in range does not yield"), FTDCircleAvoidance::CanYield(ETDMonsterFsmState::Move, false, true));
	TestFalse(TEXT("An engaged attacker waiting in range does not yield"), FTDCircleAvoidance::CanYield(ETDMonsterFsmState::Idle, false, true));
	TestFalse(TEXT("A casting monster does not yield"), FTDCircleAvoidance::CanYield(ETDMonsterFsmState::Cast, false, false));
	TestFalse(TEXT("A staggered monster does not yield"), FTDCircleAvoidance::CanYield(ETDMonsterFsmState::Stagger, false, false));
	TestFalse(TEXT("A monster in a sequence does not yield"), FTDCircleAvoidance::CanYield(ETDMonsterFsmState::Sequence, false, false));
	TestFalse(TEXT("A frozen monster does not yield"), FTDCircleAvoidance::CanYield(ETDMonsterFsmState::Idle, true, false));

	FTDCircleAvoidanceTestWorld Fixture;
	ATDMonsterCharacter* Holder = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector::ZeroVector);
	if (!TestNotNull(TEXT("Holder exists"), Holder))
	{
		return false;
	}
	const double MoverSpeed = CircleAvoidanceTestSpeed;
	const double MoverDistance = 2.0 * Holder->GetBodyRadius() + FTDCircleAvoidance::CombinedRadiusMargin + MoverSpeed * CircleAvoidanceTestTimeToCollisionSeconds;
	ATDMonsterCharacter* Mover = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(-MoverDistance, 0.0, 0.0));
	ATDMonsterCharacter* Target = Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(0.0, 5000.0, 0.0));
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem))
	{
		return false;
	}
	const int32 HolderSlot = FTDMonsterThinkSubsystemTestAccess::FindSlot(*ThinkSubsystem, Holder);
	const int32 MoverSlot = FTDMonsterThinkSubsystemTestAccess::FindSlot(*ThinkSubsystem, Mover);
	const int32 TargetSlot = FTDMonsterThinkSubsystemTestAccess::FindSlot(*ThinkSubsystem, Target);
	if (!TestTrue(TEXT("Every monster has a slot"), HolderSlot != INDEX_NONE && MoverSlot != INDEX_NONE && TargetSlot != INDEX_NONE))
	{
		return false;
	}
	const FVector2D Heading(1.0, 0.0);
	SetSlotMotion(*ThinkSubsystem, HolderSlot, Heading, 0.f, FVector2D::ZeroVector);
	SetSlotMotion(*ThinkSubsystem, MoverSlot, Heading, 1.f, Heading * MoverSpeed);
	SetSlotMotion(*ThinkSubsystem, TargetSlot, Heading, 0.f, FVector2D::ZeroVector);
	const FTDCircleAvoidanceSnapshot Initial = FTDMonsterThinkSubsystemTestAccess::CaptureSnapshot(*ThinkSubsystem);
	const TArray<int32> Order = FTDMonsterThinkSubsystemTestAccess::GetOrderedSlots(*ThinkSubsystem);
	struct FTDHoldingCase
	{
		const TCHAR* Name;
		ETDMonsterEngagement Engagement;
		AActor* Target;
		float ExpectedSpeedScale;
	};
	const float YieldSpeedScale = static_cast<float>(FTDCircleAvoidance::YieldSpeedRatio);
	const FTDHoldingCase Cases[] = {
		{ TEXT("at home without a target"), ETDMonsterEngagement::Home, nullptr, YieldSpeedScale },
		{ TEXT("engaged and holding with a target"), ETDMonsterEngagement::Engaged, Target, 0.f },
		{ TEXT("engaged without a target"), ETDMonsterEngagement::Engaged, nullptr, YieldSpeedScale },
		{ TEXT("returning with a stale target"), ETDMonsterEngagement::Returning, Target, YieldSpeedScale } };
	for (const FTDHoldingCase& Case : Cases)
	{
		FTDMonsterThinkSubsystemTestAccess::RestoreSnapshot(*ThinkSubsystem, Initial);
		SetSlotEngagement(*ThinkSubsystem, HolderSlot, Case.Engagement, Case.Target);
		FTDMonsterThinkSubsystemTestAccess::ApplyCircleAvoidanceInOrder(*ThinkSubsystem, Order);
		const FTDMonsterMoveIntent& Intent = FTDMonsterThinkSubsystemTestAccess::GetMutableIntent(*ThinkSubsystem, HolderSlot);
		AddInfo(FString::Printf(TEXT("holder %s: direction (%.3f, %.3f), speed scale %.3f"), Case.Name, Intent.Direction.X, Intent.Direction.Y, Intent.SpeedScale));
		TestEqual(FString::Printf(TEXT("A zero-intent monster %s has the expected speed scale"), Case.Name), Intent.SpeedScale, Case.ExpectedSpeedScale, CircleAvoidanceTestSpeedScaleTolerance);
		if (Case.ExpectedSpeedScale > 0.f)
		{
			TestTrue(FString::Printf(TEXT("A yielding monster %s steps perpendicular to the mover"), Case.Name), FMath::Abs(Intent.Direction.X) < CircleAvoidanceTestNumericTolerance);
		}
	}
	FTDMonsterThinkSubsystemTestAccess::RestoreSnapshot(*ThinkSubsystem, Initial);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceYieldSideTest, "TDGame.MonsterAI.CircleAvoidance.YieldSideFollowsCrossProduct", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceYieldSideTest::RunTest(const FString& Parameters)
{
	const FVector2D MoverVelocity(CircleAvoidanceTestSpeed, 0.0);
	FTDAvoidanceAgent EvenAgent = MakeCircleAvoidanceTestAgent(FVector2D::ZeroVector, FVector2D::ZeroVector, 2);
	FTDAvoidanceAgent OddAgent = MakeCircleAvoidanceTestAgent(FVector2D::ZeroVector, FVector2D::ZeroVector, 3);
	const auto YieldFrom = [](const FTDAvoidanceAgent& Agent, const FTDAvoidanceObstacle& Neighbor)
	{
		FTDAvoidanceObstacleList Neighbors;
		Neighbors.Add(Neighbor);
		return FTDCircleAvoidance::ComputeYieldVelocity(Agent, Neighbors);
	};
	const FVector2D AboveMover = YieldFrom(EvenAgent, MakeCircleAvoidanceTestObstacle(FVector2D(-300.0, -50.0), MoverVelocity, 5));
	const FVector2D BelowMover = YieldFrom(EvenAgent, MakeCircleAvoidanceTestObstacle(FVector2D(-300.0, 50.0), MoverVelocity, 5));
	const FVector2D EvenOnLine = YieldFrom(EvenAgent, MakeCircleAvoidanceTestObstacle(FVector2D(-300.0, 0.0), MoverVelocity, 5));
	const FVector2D OddOnLine = YieldFrom(OddAgent, MakeCircleAvoidanceTestObstacle(FVector2D(-300.0, 0.0), MoverVelocity, 5));
	TestTrue(FString::Printf(TEXT("An agent left of the mover's path steps further left (%.1f, %.1f)"), AboveMover.X, AboveMover.Y), AboveMover.Y > 0.0 && FMath::Abs(AboveMover.X) < CircleAvoidanceTestNumericTolerance);
	TestTrue(FString::Printf(TEXT("An agent right of the mover's path steps further right (%.1f, %.1f)"), BelowMover.X, BelowMover.Y), BelowMover.Y < 0.0);
	TestTrue(TEXT("An even SimulationId on the path steps left"), EvenOnLine.Y > 0.0);
	TestTrue(TEXT("An odd SimulationId on the path steps right"), OddOnLine.Y < 0.0);
	TestEqual(TEXT("Yield speed is half the yielder's top speed"), AboveMover.Size(), FTDCircleAvoidance::YieldSpeedRatio * CircleAvoidanceTestSpeed, 1e-6);
	TestTrue(TEXT("A nearly still neighbor causes no yield"), YieldFrom(EvenAgent, MakeCircleAvoidanceTestObstacle(FVector2D(-100.0, 0.0), FVector2D(0.5, 0.0), 5)).IsZero());
	TestTrue(TEXT("A neighbor more than one second away causes no yield"), YieldFrom(EvenAgent, MakeCircleAvoidanceTestObstacle(FVector2D(-1000.0, 0.0), MoverVelocity, 5)).IsZero());
	TestTrue(TEXT("Monsters do not yield to external bodies"), YieldFrom(EvenAgent, MakeCircleAvoidanceTestObstacle(FVector2D(-300.0, 0.0), MoverVelocity, 0, false)).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceOrderTest, "TDGame.MonsterAI.CircleAvoidance.ProcessingOrderDoesNotMatter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceOrderTest::RunTest(const FString& Parameters)
{
	FTDCircleAvoidanceTestWorld Fixture;
	TArray<ATDMonsterCharacter*> Monsters;
	for (int32 Column = 0; Column < 4; ++Column)
	{
		for (int32 Row = 0; Row < 4; ++Row)
		{
			Monsters.Add(Fixture.SpawnMonster(TEXT("Goblin_Melee"), FVector(Column * 100.0, Row * 100.0, 0.0)));
		}
	}
	UTDMonsterThinkSubsystem* ThinkSubsystem = Fixture.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	if (!TestNotNull(TEXT("Think subsystem exists"), ThinkSubsystem) || !TestEqual(TEXT("All spawned monsters are registered"), ThinkSubsystem->GetActiveMonsterCount(), Monsters.Num()))
	{
		return false;
	}
	FRandomStream Stream(CircleAvoidanceTestSeed + 3);
	for (int32 Index = 0; Index < Monsters.Num(); ++Index)
	{
		const int32 SlotIndex = FTDMonsterThinkSubsystemTestAccess::FindSlot(*ThinkSubsystem, Monsters[Index]);
		const float HeadingAngle = Stream.FRandRange(0.f, 2.f * PI);
		const float PreviousAngle = Stream.FRandRange(0.f, 2.f * PI);
		const float PreviousSpeedRatio = Stream.FRandRange(0.2f, 1.f);
		const float SpeedScale = Index % 4 == 0 ? 0.f : Stream.FRandRange(0.3f, 1.f);
		const FVector2D Heading(FMath::Cos(HeadingAngle), FMath::Sin(HeadingAngle));
		const FVector2D PreviousCommandVelocity = FVector2D(FMath::Cos(PreviousAngle), FMath::Sin(PreviousAngle)) * GetSlotMoveSpeed(*ThinkSubsystem, SlotIndex) * PreviousSpeedRatio;
		SetSlotMotion(*ThinkSubsystem, SlotIndex, Heading, SpeedScale, PreviousCommandVelocity);
		FTDMonsterAvoidanceFragment& Avoidance = FTDMonsterThinkSubsystemTestAccess::GetMutableAvoidance(*ThinkSubsystem, SlotIndex);
		Avoidance.PreviousCandidateIndex = Stream.RandRange(INDEX_NONE, FTDCircleAvoidance::CandidateCount - 1);
		Avoidance.SlowedWaitStepCount = Stream.RandRange(0, 8);
	}
	const FTDCircleAvoidanceSnapshot Initial = FTDMonsterThinkSubsystemTestAccess::CaptureSnapshot(*ThinkSubsystem);
	const TArray<int32> ForwardOrder = FTDMonsterThinkSubsystemTestAccess::GetOrderedSlots(*ThinkSubsystem);
	TArray<TArray<int32>> OtherOrders;
	OtherOrders.Add(MakeReversedOrder(ForwardOrder));
	for (int32 Shuffle = 0; Shuffle < 3; ++Shuffle)
	{
		OtherOrders.Add(MakeShuffledOrder(ForwardOrder, Stream));
	}
	FTDMonsterThinkSubsystemTestAccess::ApplyCircleAvoidanceInOrder(*ThinkSubsystem, ForwardOrder);
	const FTDCircleAvoidanceSnapshot ForwardResult = FTDMonsterThinkSubsystemTestAccess::CaptureSnapshot(*ThinkSubsystem);
	int32 OrderMismatches = 0;
	for (const TArray<int32>& Order : OtherOrders)
	{
		FTDMonsterThinkSubsystemTestAccess::RestoreSnapshot(*ThinkSubsystem, Initial);
		FTDMonsterThinkSubsystemTestAccess::ApplyCircleAvoidanceInOrder(*ThinkSubsystem, Order);
		OrderMismatches += CountAvoidanceSnapshotMismatches(ForwardResult, FTDMonsterThinkSubsystemTestAccess::CaptureSnapshot(*ThinkSubsystem), ForwardOrder);
	}
	FTDMonsterThinkSubsystemTestAccess::RestoreSnapshot(*ThinkSubsystem, Initial);
	FTDMonsterThinkSubsystemTestAccess::ApplyCircleAvoidanceWritingEachCommandImmediately(*ThinkSubsystem, ForwardOrder);
	const FTDCircleAvoidanceSnapshot ImmediateForwardResult = FTDMonsterThinkSubsystemTestAccess::CaptureSnapshot(*ThinkSubsystem);
	FTDMonsterThinkSubsystemTestAccess::RestoreSnapshot(*ThinkSubsystem, Initial);
	FTDMonsterThinkSubsystemTestAccess::ApplyCircleAvoidanceWritingEachCommandImmediately(*ThinkSubsystem, OtherOrders[0]);
	const int32 ImmediateWriteMismatches = CountAvoidanceSnapshotMismatches(ImmediateForwardResult, FTDMonsterThinkSubsystemTestAccess::CaptureSnapshot(*ThinkSubsystem), ForwardOrder);
	FTDMonsterThinkSubsystemTestAccess::RestoreSnapshot(*ThinkSubsystem, Initial);
	const int32 ChangedIntents = CountChangedIntents(Initial, ForwardResult, ForwardOrder);
	AddInfo(FString::Printf(TEXT("slots %d, intents changed by avoidance %d, order mismatches %d over %d orders, immediate-write control mismatches %d"), ForwardOrder.Num(), ChangedIntents, OrderMismatches, OtherOrders.Num(), ImmediateWriteMismatches));
	TestTrue(TEXT("The snapshot makes avoidance change some intents"), ChangedIntents > 0);
	TestEqual(TEXT("Subsystem avoidance results do not depend on slot order"), OrderMismatches, 0);
	TestTrue(TEXT("The comparison detects order dependence when each command is written inside the loop"), ImmediateWriteMismatches > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceHysteresisTest, "TDGame.MonsterAI.CircleAvoidance.HysteresisKeepsPreviousChoice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceHysteresisTest::RunTest(const FString& Parameters)
{
	FTDAvoidanceAgent Agent = MakeCircleAvoidanceTestAgent(FVector2D::ZeroVector, FVector2D(CircleAvoidanceTestSpeed, 0.0), 2);
	FTDAvoidanceObstacleList Obstacles;
	Obstacles.Add(MakeCircleAvoidanceTestObstacle(FVector2D(150.0, 0.0), FVector2D::ZeroVector, 3));
	TStaticArray<double, FTDCircleAvoidance::CandidateCount> Costs;
	FTDCircleAvoidance::ComputeCandidateCosts(Agent, Obstacles, Costs);
	const int32 BestIndex = FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, Obstacles).CandidateIndex;
	int32 NearIndex = INDEX_NONE;
	int32 FarIndex = INDEX_NONE;
	for (int32 CandidateIndex = 0; CandidateIndex < FTDCircleAvoidance::CandidateCount; ++CandidateIndex)
	{
		if (CandidateIndex == BestIndex)
		{
			continue;
		}
		if (NearIndex == INDEX_NONE && Costs[CandidateIndex] <= Costs[BestIndex] + FTDCircleAvoidance::HysteresisCost)
		{
			NearIndex = CandidateIndex;
		}
		if (FarIndex == INDEX_NONE && Costs[CandidateIndex] > Costs[BestIndex] + FTDCircleAvoidance::HysteresisCost)
		{
			FarIndex = CandidateIndex;
		}
	}
	AddInfo(FString::Printf(TEXT("best %d (%.4f), near %d, far %d"), BestIndex, Costs[BestIndex], NearIndex, FarIndex));
	if (!TestTrue(TEXT("The setup has a candidate within the hysteresis margin"), NearIndex != INDEX_NONE) || !TestTrue(TEXT("The setup has a candidate outside the hysteresis margin"), FarIndex != INDEX_NONE))
	{
		return false;
	}
	Agent.PreviousCandidateIndex = NearIndex;
	TestEqual(TEXT("A previous choice within 0.1 of the best cost is kept"), FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, Obstacles).CandidateIndex, NearIndex);
	Agent.PreviousCandidateIndex = FarIndex;
	TestEqual(TEXT("A previous choice more than 0.1 above the best cost is dropped"), FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, Obstacles).CandidateIndex, BestIndex);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceCrossingSceneTest, "TDGame.MonsterAI.CircleAvoidance.Scene.CrossingStreams", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceCrossingSceneTest::RunTest(const FString& Parameters)
{
	const TArray<FTDCircleAvoidanceSceneBody> SceneBodies = MakeCrossingStreamsScene();
	const FTDCircleAvoidanceSceneReport LegacyReport = RunCircleAvoidanceScene(SceneBodies, 14.f, 0);
	const FTDCircleAvoidanceSceneReport CircleReport = RunCircleAvoidanceScene(SceneBodies, 14.f, 1);
	AddInfo(FString::Printf(TEXT("6+6 crossing, legacy separation: %s"), *LegacyReport.Describe()));
	AddInfo(FString::Printf(TEXT("6+6 crossing, circle avoidance: %s"), *CircleReport.Describe()));
	AddInfo(TEXT("fixture limits: Flying movement mode, capsule radius 40 for every monster"));
	if (!TestTrue(TEXT("Both scene runs set up"), LegacyReport.bIsSetupValid && CircleReport.bIsSetupValid))
	{
		return false;
	}
	TestEqual(TEXT("Every crossing monster reaches its goal with circle avoidance"), CircleReport.ReachedPassers, CircleReport.Passers);
	if (LegacyReport.ContactSteps == 0)
	{
		AddWarning(TEXT("Inconclusive contact comparison: legacy separation produced no contact step, so Mover capsules may be blocking each other and the comparison below cannot tell the modes apart"));
	}
	TestTrue(TEXT("Circle avoidance touches no more often than legacy separation"), CircleReport.GetContactStepRatio() <= LegacyReport.GetContactStepRatio());
	TestTrue(TEXT("Circle avoidance overlaps no deeper than legacy separation"), CircleReport.MaxOverlap <= LegacyReport.MaxOverlap + CircleAvoidanceSceneOverlapTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceStandingGroupSceneTest, "TDGame.MonsterAI.CircleAvoidance.Scene.PassThroughStandingGroup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceStandingGroupSceneTest::RunTest(const FString& Parameters)
{
	const TArray<FTDCircleAvoidanceSceneBody> SceneBodies = MakeStandingGroupScene();
	const FTDCircleAvoidanceSceneReport LegacyReport = RunCircleAvoidanceScene(SceneBodies, 12.f, 0);
	const FTDCircleAvoidanceSceneReport CircleReport = RunCircleAvoidanceScene(SceneBodies, 12.f, 1);
	AddInfo(FString::Printf(TEXT("3 passers through 6 standing, legacy separation: %s"), *LegacyReport.Describe()));
	AddInfo(FString::Printf(TEXT("3 passers through 6 standing, circle avoidance: %s"), *CircleReport.Describe()));
	if (!TestTrue(TEXT("Both scene runs set up"), LegacyReport.bIsSetupValid && CircleReport.bIsSetupValid))
	{
		return false;
	}
	TestEqual(TEXT("Every passer reaches its goal with circle avoidance"), CircleReport.ReachedPassers, CircleReport.Passers);
	TestTrue(FString::Printf(TEXT("Standing monsters yield in circle avoidance mode (%d yield slot steps)"), CircleReport.YieldSlotSteps), CircleReport.YieldSlotSteps > 0);
	TestTrue(FString::Printf(TEXT("Standing monsters step aside (%.1f cm, legacy %.1f cm)"), CircleReport.MaxStandingDisplacement, LegacyReport.MaxStandingDisplacement), CircleReport.MaxStandingDisplacement > CircleAvoidanceStandingYieldDistance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCircleAvoidanceDeterministicSceneTest, "TDGame.MonsterAI.CircleAvoidance.Scene.Deterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCircleAvoidanceDeterministicSceneTest::RunTest(const FString& Parameters)
{
	const TArray<FTDCircleAvoidanceSceneBody> SceneBodies = MakeCrossingStreamsScene();
	const FTDCircleAvoidanceSceneReport FirstReport = RunCircleAvoidanceScene(SceneBodies, 8.f, 1);
	const FTDCircleAvoidanceSceneReport SecondReport = RunCircleAvoidanceScene(SceneBodies, 8.f, 1);
	if (!TestTrue(TEXT("Both scene runs set up"), FirstReport.bIsSetupValid && SecondReport.bIsSetupValid) || !TestEqual(TEXT("Both runs report every monster"), FirstReport.FinalLocations.Num(), SecondReport.FinalLocations.Num()))
	{
		return false;
	}
	int32 Differences = 0;
	double LargestDifference = 0.0;
	for (int32 Index = 0; Index < FirstReport.FinalLocations.Num(); ++Index)
	{
		const double Difference = FVector::Dist(FirstReport.FinalLocations[Index], SecondReport.FinalLocations[Index]);
		LargestDifference = FMath::Max(LargestDifference, Difference);
		Differences += FirstReport.FinalLocations[Index] == SecondReport.FinalLocations[Index] ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("first run: %s; differing final positions %d, largest difference %.6f cm"), *FirstReport.Describe(), Differences, LargestDifference));
	TestEqual(TEXT("Two identical runs end in identical positions"), Differences, 0);
	return true;
}

#endif
