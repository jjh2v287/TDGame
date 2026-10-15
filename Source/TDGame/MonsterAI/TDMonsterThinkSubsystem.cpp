#include "MonsterAI/TDMonsterThinkSubsystem.h"
#include "Algo/Sort.h"
#include "Characters/TDCompanionCharacter.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Core/TDGameplayTags.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "MonsterAI/TDMonsterActionExecutor.h"
#include "MonsterAI/TDMonsterBody.h"
#include "MonsterAI/TDUtilityScorer.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMonsterAIDebugDraw(
		TEXT("TD.MonsterAI.DebugDraw"),
		0,
		TEXT("Draws each monster's FSM state and current action above its head. 0 = off, 1 = on."),
		ECVF_Cheat);

	TAutoConsoleVariable<int32> CVarMonsterAILocalAvoidance(
		TEXT("TD.MonsterAI.LocalAvoidance"),
		1,
		TEXT("Selects monster local avoidance. 0 = legacy separation steering, 1 = velocity-aware circle avoidance with yielding."),
		ECVF_Cheat);

	ITDMonsterBody* ResolveBody(const FTDMonsterBodyFragment& Body)
	{
		return Cast<ITDMonsterBody>(Body.Actor.Get());
	}

	float PlanarDistance(const FVector& Left, const FVector& Right)
	{
		return FVector::Dist2D(Left, Right);
	}

	bool IsPositionBefore(const FVector& Left, const FVector& Right)
	{
		if (Left.X != Right.X)
		{
			return Left.X < Right.X;
		}
		if (Left.Y != Right.Y)
		{
			return Left.Y < Right.Y;
		}
		return Left.Z < Right.Z;
	}

	const TCHAR* GetMonsterEngagementName(const ETDMonsterEngagement Engagement)
	{
		switch (Engagement)
		{
		case ETDMonsterEngagement::Engaged:
			return TEXT("Engaged");
		case ETDMonsterEngagement::Returning:
			return TEXT("Returning");
		default:
			return TEXT("Home");
		}
	}
}

void FTDMonsterThinkTickFunction::ExecuteTick(const float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Owner)
	{
		Owner->AdvanceFrame(DeltaTime);
	}
}

FString FTDMonsterThinkTickFunction::DiagnosticMessage()
{
	return TEXT("UTDMonsterThinkSubsystem::Tick");
}

FName FTDMonsterThinkTickFunction::DiagnosticContext(bool bDetailed)
{
	return FName(TEXT("TDMonsterThink"));
}

bool UTDMonsterThinkSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTDMonsterThinkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	TickFunction.Owner = this;
	TickFunction.TickGroup = TG_PrePhysics;
	TickFunction.bCanEverTick = true;
	TickFunction.bStartWithTickEnabled = true;
	TickFunction.bHighPriority = true;
	TickFunction.bTickEvenWhenPaused = false;
	TickFunction.bAllowTickBatching = false;
	TickFunction.RegisterTickFunction(InWorld.PersistentLevel);
}

void UTDMonsterThinkSubsystem::Deinitialize()
{
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
	TickFunction.Owner = nullptr;
	Bodies.Reset();
	Brains.Reset();
	Intents.Reset();
	Avoidances.Reset();
	FreeSlots.Reset();
	OrderedSlots.Reset();
	PendingAlertSlots.Reset();
	Super::Deinitialize();
}

FTDMonsterSlotHandle UTDMonsterThinkSubsystem::RegisterMonster(AActor* BodyActor, TSharedPtr<const FTDResolvedMonsterDefinition> Definition)
{
	FTDMonsterSlotHandle Handle;
	if (!IsValid(BodyActor) || !Definition.IsValid() || !Cast<ITDMonsterBody>(BodyActor))
	{
		return Handle;
	}
	int32 SlotIndex = INDEX_NONE;
	if (FreeSlots.Num() > 0)
	{
		SlotIndex = FreeSlots.Pop(EAllowShrinking::No);
	}
	else
	{
		SlotIndex = Bodies.AddDefaulted();
		Brains.AddDefaulted();
		Intents.AddDefaulted();
		Avoidances.AddDefaulted();
	}

	FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
	const uint32 Generation = Body.Generation + 1;
	Body = FTDMonsterBodyFragment();
	Body.Actor = BodyActor;
	Body.SimulationId = NextSimulationId++;
	Body.Generation = Generation;
	Body.bIsActive = true;

	FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	Brain = FTDMonsterBrainFragment();
	Brain.Definition = Definition;
	FTDMonsterActionExecutor::ResetForDefinition(Brain);
	Brain.NextThinkStep = StepIndex + 1 + Body.SimulationId % static_cast<uint32>(Definition->ThinkPeriodSteps);
	Intents[SlotIndex] = FTDMonsterMoveIntent();
	Avoidances[SlotIndex] = FTDMonsterAvoidanceFragment();

	ITDMonsterBody* MonsterBody = Cast<ITDMonsterBody>(BodyActor);
	MonsterBody->ApplyBodyStats(Definition->Stats);
	Body.Location = MonsterBody->GetBodyLocation();
	Body.SpawnLocation = Body.Location;
	Body.HomeLocation = Body.Location;
	Body.Yaw = MonsterBody->GetBodyYaw();
	Body.Radius = MonsterBody->GetBodyRadius();

	Handle.Index = SlotIndex;
	Handle.Generation = Generation;
	RebuildOrder();
	return Handle;
}

void UTDMonsterThinkSubsystem::UnregisterMonster(const FTDMonsterSlotHandle Handle)
{
	if (!IsHandleCurrent(Handle))
	{
		return;
	}
	FTDMonsterBrainFragment& Brain = Brains[Handle.Index];
	FTDMonsterStepContext Context = MakeContext(Handle.Index);
	FTDMonsterActionExecutor::InterruptActivity(Brain, Context);
	Bodies[Handle.Index].bIsActive = false;
	Bodies[Handle.Index].Actor.Reset();
	Brain.Definition.Reset();
	Brain.Target.Reset();
	PendingAlertSlots.Remove(Handle.Index);
	FreeSlots.Add(Handle.Index);
	RebuildOrder();
}

void UTDMonsterThinkSubsystem::NotifyMonsterDamaged(const FTDMonsterSlotHandle Handle, const float Damage, const FVector& SourceLocation)
{
	if (!IsHandleCurrent(Handle) || Damage <= 0.f)
	{
		return;
	}
	FTDMonsterBrainFragment& Brain = Brains[Handle.Index];
	if (Brain.Engagement == ETDMonsterEngagement::Returning && PlanarDistance(Bodies[Handle.Index].HomeLocation, SourceLocation) > ReturningReengageRadius)
	{
		return;
	}
	if (!Brain.IsEngaged())
	{
		PendingAlertSlots.AddUnique(Handle.Index);
	}
	Engage(Handle.Index);
	Brain.EngageLockSteps = TDMonsterAI::SecondsToSteps(DamageEngageSeconds);
	const float MaxHealth = Brain.Definition->GetStatsForPhase(Brain.PhaseIndex).MaxHealth;
	const bool bIsHeavyHit = Damage >= MaxHealth * StaggerHealthRatio;
	if (!bIsHeavyHit || Brain.bIsFrozen || Brain.State == ETDMonsterFsmState::Stagger)
	{
		return;
	}
	FTDMonsterActionExecutor::EnterStagger(Brain, SourceLocation, MakeContext(Handle.Index));
}

void UTDMonsterThinkSubsystem::NotifyMonsterFrozen(const FTDMonsterSlotHandle Handle, const bool bIsFrozen)
{
	if (!IsHandleCurrent(Handle))
	{
		return;
	}
	FTDMonsterBrainFragment& Brain = Brains[Handle.Index];
	if (bIsFrozen)
	{
		FTDMonsterActionExecutor::InterruptActivity(Brain, MakeContext(Handle.Index));
	}
	Brain.bIsFrozen = bIsFrozen;
	Brain.bForceThink = true;
}

void UTDMonsterThinkSubsystem::AdvanceFrame(const float DeltaSeconds)
{
	if (OrderedSlots.IsEmpty())
	{
		AccumulatedSeconds = 0.f;
		return;
	}
	AccumulatedSeconds += FMath::Max(0.f, DeltaSeconds);
	int32 StepsThisFrame = 0;
	while (AccumulatedSeconds >= TDMonsterAI::StepSeconds && StepsThisFrame < MaxStepsPerFrame)
	{
		RunKernelStep();
		AccumulatedSeconds -= TDMonsterAI::StepSeconds;
		++StepsThisFrame;
	}
	if (StepsThisFrame == MaxStepsPerFrame)
	{
		AccumulatedSeconds = FMath::Min(AccumulatedSeconds, TDMonsterAI::StepSeconds);
	}
	PresentFrame(DeltaSeconds);
}

ETDMonsterFsmState UTDMonsterThinkSubsystem::GetMonsterState(const FTDMonsterSlotHandle Handle) const
{
	return IsHandleCurrent(Handle) ? Brains[Handle.Index].GetReportedState() : ETDMonsterFsmState::Idle;
}

FName UTDMonsterThinkSubsystem::GetMonsterActionId(const FTDMonsterSlotHandle Handle) const
{
	return IsHandleCurrent(Handle) ? Brains[Handle.Index].GetCurrentActionId() : NAME_None;
}

bool UTDMonsterThinkSubsystem::IsMonsterEngaged(const FTDMonsterSlotHandle Handle) const
{
	return IsHandleCurrent(Handle) && Brains[Handle.Index].IsEngaged();
}

UTDMonsterThinkSubsystem* UTDMonsterThinkSubsystem::GetMonsterThinkSubsystem(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UTDMonsterThinkSubsystem>() : nullptr;
}

int32 UTDMonsterThinkSubsystem::GetActiveMonsterCount() const
{
	return OrderedSlots.Num();
}

TArray<FString> UTDMonsterThinkSubsystem::GetMonsterDebugLines() const
{
	TArray<FString> Lines;
	for (const int32 SlotIndex : OrderedSlots)
	{
		const FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
		const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
		const AActor* Actor = Body.Actor.Get();
		const UTDCombatComponent* Combat = UTDCombatLibrary::GetCombatComponent(Actor);
		Lines.Add(FString::Printf(TEXT("sim=%u actor=%s def=%s state=%s action=%s engaged=%d engagement=%s phase=%d hp=%.1f pos=(%.0f,%.0f)"),
			Body.SimulationId,
			Actor ? *Actor->GetName() : TEXT("None"),
			Brain.Definition.IsValid() ? *Brain.Definition->Id.ToString() : TEXT("None"),
			FTDMonsterActionRegistry::GetStateName(Brain.GetReportedState()),
			*Brain.GetCurrentActionId().ToString(),
			Brain.IsEngaged() ? 1 : 0,
			GetMonsterEngagementName(Brain.Engagement),
			Brain.PhaseIndex,
			Combat ? Combat->GetCurrentHealth() : 0.f,
			Body.Location.X,
			Body.Location.Y));
	}
	return Lines;
}

bool UTDMonsterThinkSubsystem::IsHandleCurrent(const FTDMonsterSlotHandle Handle) const
{
	return Bodies.IsValidIndex(Handle.Index) && Bodies[Handle.Index].bIsActive && Bodies[Handle.Index].Generation == Handle.Generation;
}

void UTDMonsterThinkSubsystem::RebuildOrder()
{
	OrderedSlots.Reset();
	for (int32 SlotIndex = 0; SlotIndex < Bodies.Num(); ++SlotIndex)
	{
		if (Bodies[SlotIndex].bIsActive)
		{
			OrderedSlots.Add(SlotIndex);
		}
	}
	OrderedSlots.Sort([this](const int32 Left, const int32 Right)
	{
		return Bodies[Left].SimulationId < Bodies[Right].SimulationId;
	});
	bIsNeighborGridStale = true;
}

void UTDMonsterThinkSubsystem::RunKernelStep()
{
	++StepIndex;
	TArray<int32> StaleSlots;
	for (const int32 SlotIndex : TArray<int32>(OrderedSlots))
	{
		if (!ResolveBody(Bodies[SlotIndex]))
		{
			StaleSlots.Add(SlotIndex);
			continue;
		}
		SenseSlot(SlotIndex);
	}
	for (const int32 SlotIndex : StaleSlots)
	{
		FTDMonsterSlotHandle Handle;
		Handle.Index = SlotIndex;
		Handle.Generation = Bodies[SlotIndex].Generation;
		UnregisterMonster(Handle);
	}
	RebuildNeighborGrid();
	UpdateEngagement();
	CountAttackersByTarget();
	for (const int32 SlotIndex : TArray<int32>(OrderedSlots))
	{
		if (!Bodies[SlotIndex].bIsActive)
		{
			continue;
		}
		const bool bWasCasting = Brains[SlotIndex].State == ETDMonsterFsmState::Cast;
		ThinkAndAct(SlotIndex);
		const AActor* Target = Brains[SlotIndex].Target.Get();
		if (!bWasCasting && Brains[SlotIndex].State == ETDMonsterFsmState::Cast && Target)
		{
			++AttackersByTarget.FindOrAdd(Target);
		}
	}
	ApplyLocalAvoidance();
}

void UTDMonsterThinkSubsystem::SenseSlot(const int32 SlotIndex)
{
	FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
	const ITDMonsterBody* MonsterBody = ResolveBody(Body);
	Body.Location = MonsterBody->GetBodyLocation();
	Body.Yaw = MonsterBody->GetBodyYaw();
	FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	FTDMonsterActionExecutor::TickCounters(Brain);
	const AActor* Target = Brain.Target.Get();
	if (!Target || !UTDCombatLibrary::IsActorAlive(Target))
	{
		RefreshTarget(SlotIndex);
	}
}

void UTDMonsterThinkSubsystem::UpdateEngagement()
{
	TArray<int32> NewlyEngaged = MoveTemp(PendingAlertSlots);
	PendingAlertSlots.Reset();
	NewlyEngaged.RemoveAll([this](const int32 SlotIndex) { return !Bodies.IsValidIndex(SlotIndex) || !Bodies[SlotIndex].bIsActive; });
	for (const int32 SlotIndex : OrderedSlots)
	{
		FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
		FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
		const AActor* Target = Brain.Target.Get();
		const bool bHasLivingTarget = Target && UTDCombatLibrary::IsActorAlive(Target);
		const FVector TargetLocation = bHasLivingTarget ? Target->GetActorLocation() : FVector::ZeroVector;
		const float DistanceToTarget = bHasLivingTarget ? PlanarDistance(Body.Location, TargetLocation) : TNumericLimits<float>::Max();
		const float HomeDistance = PlanarDistance(Body.Location, Body.HomeLocation);
		switch (Brain.Engagement)
		{
		case ETDMonsterEngagement::Home:
		{
			const bool bCanEngage = bHasLivingTarget && DistanceToTarget <= EngageRadius && PlanarDistance(Body.HomeLocation, TargetLocation) <= LeashRadius && Brain.ReengageBlockSteps == 0;
			if (bCanEngage)
			{
				Engage(SlotIndex);
				NewlyEngaged.Add(SlotIndex);
			}
			break;
		}
		case ETDMonsterEngagement::Engaged:
		{
			if (!bHasLivingTarget)
			{
				BeginReturn(SlotIndex);
				Brain.ReengageBlockSteps = TDMonsterAI::SecondsToSteps(ReengageBlockSeconds);
				break;
			}
			if (HomeDistance > MaxChaseDistanceFromHome)
			{
				BeginReturn(SlotIndex);
				break;
			}
			const bool bIsOutOfReach = PlanarDistance(Body.HomeLocation, TargetLocation) > LeashRadius || DistanceToTarget > LoseTargetRadius;
			Brain.OutOfReachStepCount = bIsOutOfReach ? Brain.OutOfReachStepCount + 1 : 0;
			if (Brain.OutOfReachStepCount >= TDMonsterAI::SecondsToSteps(GiveUpSeconds) && Brain.EngageLockSteps == 0)
			{
				BeginReturn(SlotIndex);
			}
			break;
		}
		case ETDMonsterEngagement::Returning:
		{
			if (HomeDistance <= HomeArrivalRadius)
			{
				FinishReturn(SlotIndex);
				break;
			}
			if (Brain.bIsFrozen)
			{
				break;
			}
			if (IsReturnStallExcusedByAvoidance(SlotIndex))
			{
				break;
			}
			if (HomeDistance < Brain.ClosestReturnDistance - 1.f)
			{
				Brain.ClosestReturnDistance = HomeDistance;
				Brain.ReturnStallStepCount = 0;
				break;
			}
			++Brain.ReturnStallStepCount;
			if (Brain.ReturnStallStepCount >= TDMonsterAI::SecondsToSteps(ReturnStallSeconds))
			{
				Body.HomeLocation = Body.Location;
				FinishReturn(SlotIndex);
			}
			break;
		}
		}
	}
	for (const int32 AlertingSlot : NewlyEngaged)
	{
		const AActor* AlertTarget = Brains[AlertingSlot].Target.Get();
		if (!Brains[AlertingSlot].IsEngaged() || !AlertTarget)
		{
			continue;
		}
		const FVector AlertTargetLocation = AlertTarget->GetActorLocation();
		for (const int32 SlotIndex : OrderedSlots)
		{
			const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
			const FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
			const bool bCanHearAlert = Brain.Engagement == ETDMonsterEngagement::Home
				&& Brain.ReengageBlockSteps == 0
				&& PlanarDistance(Body.Location, Bodies[AlertingSlot].Location) <= AlertRadius
				&& PlanarDistance(Body.HomeLocation, AlertTargetLocation) <= LeashRadius;
			if (!bCanHearAlert)
			{
				continue;
			}
			Brains[SlotIndex].Target = Brains[AlertingSlot].Target;
			Engage(SlotIndex);
		}
	}
}

void UTDMonsterThinkSubsystem::ThinkAndAct(const int32 SlotIndex)
{
	FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	FTDMonsterMoveIntent& Intent = Intents[SlotIndex];
	Intent = FTDMonsterMoveIntent();
	if (Brain.bIsFrozen)
	{
		return;
	}
	if (!Brain.IsEngaged())
	{
		Intent = MakeReturnHomeIntent(SlotIndex);
		return;
	}
	const bool bIsThinkDue = StepIndex >= Brain.NextThinkStep || Brain.bForceThink;
	if (bIsThinkDue && Brain.CanThink())
	{
		RefreshTarget(SlotIndex);
	}
	const FTDMonsterStepContext Context = MakeContext(SlotIndex);
	if (bIsThinkDue && Brain.CanThink())
	{
		Brain.NextThinkStep = StepIndex + static_cast<uint64>(Brain.Definition->ThinkPeriodSteps);
		Brain.bForceThink = false;
		RefreshNeighborGridIfStale();
		const FTDBrainInputs Inputs = BuildInputs(SlotIndex, Context);
		const TArray<FTDResolvedPhase>& Phases = Brain.Definition->Phases;
		if (Phases.IsValidIndex(Brain.PhaseIndex) && Phases[Brain.PhaseIndex].IsSatisfied(Inputs))
		{
			FTDMonsterActionExecutor::EnterPhase(Brain, Brain.PhaseIndex + 1, Context);
		}
		if (Brain.CanThink())
		{
			FTDBrainMemory Memory;
			Memory.CurrentActionIndex = Brain.CurrentActionIndex;
			Memory.HoldRemainingSteps = Brain.HoldSteps;
			Memory.ActionCooldownSteps = Brain.ActionCooldownSteps;
			const FTDBrainDecision Decision = FTDUtilityScorer::Think(*Brain.Definition, Brain.PhaseIndex, Inputs, Memory);
			FTDMonsterActionExecutor::ApplyDecision(Brain, Decision, Context);
		}
	}
	Intent = FTDMonsterActionExecutor::StepState(Brain, Context);
}

void UTDMonsterThinkSubsystem::ApplySeparation()
{
	for (const int32 SlotIndex : OrderedSlots)
	{
		FTDMonsterMoveIntent& Intent = Intents[SlotIndex];
		if (Intent.Direction.IsNearlyZero())
		{
			continue;
		}
		Intent.Direction = SteerAroundBlockingAlly(SlotIndex, Intent.Direction);
		const FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
		FVector2D Push = FVector2D::ZeroVector;
		for (const int32 OtherIndex : OrderedSlots)
		{
			if (OtherIndex == SlotIndex)
			{
				continue;
			}
			const FTDMonsterBodyFragment& Other = Bodies[OtherIndex];
			const FVector2D Offset(Body.Location.X - Other.Location.X, Body.Location.Y - Other.Location.Y);
			const float Distance = Offset.Size();
			const float DesiredGap = Body.Radius + Other.Radius + 20.f;
			if (Distance >= DesiredGap || Distance <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Push += (Offset / Distance) * ((DesiredGap - Distance) / DesiredGap);
		}
		const FVector2D Steered = Intent.Direction + Push * 1.5f;
		Intent.Direction = Steered.IsNearlyZero() ? Intent.Direction : Steered.GetSafeNormal();
	}
}

FVector2D UTDMonsterThinkSubsystem::SteerAroundBlockingAlly(const int32 SlotIndex, const FVector2D& Direction) const
{
	const FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
	int32 BlockerIndex = INDEX_NONE;
	float BlockerDistance = TNumericLimits<float>::Max();
	for (const int32 OtherIndex : OrderedSlots)
	{
		if (OtherIndex == SlotIndex)
		{
			continue;
		}
		const FTDMonsterBodyFragment& Other = Bodies[OtherIndex];
		const FVector2D Offset(Other.Location.X - Body.Location.X, Other.Location.Y - Body.Location.Y);
		const float Ahead = FVector2D::DotProduct(Offset, Direction);
		const float Lateral = FMath::Abs(FVector2D::CrossProduct(Direction, Offset));
		const float BodyWidth = Body.Radius + Other.Radius;
		if (Ahead <= 0.f || Ahead > BodyWidth + AvoidanceLookAhead || Lateral >= BodyWidth)
		{
			continue;
		}
		if (Ahead < BlockerDistance)
		{
			BlockerDistance = Ahead;
			BlockerIndex = OtherIndex;
		}
	}
	if (BlockerIndex == INDEX_NONE)
	{
		return Direction;
	}
	const FTDMonsterBodyFragment& Blocker = Bodies[BlockerIndex];
	const FVector2D ToBlocker(Blocker.Location.X - Body.Location.X, Blocker.Location.Y - Body.Location.Y);
	const float Side = FVector2D::CrossProduct(Direction, ToBlocker);
	const bool bTurnLeft = FMath::IsNearlyZero(Side) ? (Body.SimulationId % 2) == 0 : Side < 0.f;
	return Direction.GetRotated(bTurnLeft ? AvoidanceTurnDegrees : -AvoidanceTurnDegrees).GetSafeNormal();
}

void UTDMonsterThinkSubsystem::ApplyLocalAvoidance()
{
	if (CVarMonsterAILocalAvoidance.GetValueOnGameThread() == 0)
	{
		ApplySeparation();
		for (const int32 SlotIndex : OrderedSlots)
		{
			Avoidances[SlotIndex] = FTDMonsterAvoidanceFragment();
		}
		return;
	}
	ApplyCircleAvoidance();
}

void UTDMonsterThinkSubsystem::ApplyCircleAvoidance()
{
	RefreshNeighborGridIfStale();
	TArray<FTDAvoidanceObstacle> ExternalBodies;
	TArray<const AActor*> ExternalActors;
	GatherExternalBodies(ExternalBodies, ExternalActors);
	TArray<FVector2D> NextCommandVelocities;
	NextCommandVelocities.SetNumZeroed(Bodies.Num());
	for (const int32 SlotIndex : OrderedSlots)
	{
		NextCommandVelocities[SlotIndex] = ApplyCircleAvoidanceToSlot(SlotIndex, ExternalBodies, ExternalActors);
	}
	for (const int32 SlotIndex : OrderedSlots)
	{
		Avoidances[SlotIndex].PreviousCommandVelocity = NextCommandVelocities[SlotIndex];
	}
}

FVector2D UTDMonsterThinkSubsystem::ApplyCircleAvoidanceToSlot(const int32 SlotIndex, const TConstArrayView<FTDAvoidanceObstacle> ExternalBodies, const TConstArrayView<const AActor*> ExternalActors)
{
	const FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	FTDMonsterAvoidanceFragment& Avoidance = Avoidances[SlotIndex];
	FTDMonsterMoveIntent& Intent = Intents[SlotIndex];
	const float MoveSpeed = Brain.Definition->GetStatsForPhase(Brain.PhaseIndex).MoveSpeed;
	if (MoveSpeed <= 0.f)
	{
		Avoidance.SlowedWaitStepCount = 0;
		return FVector2D::ZeroVector;
	}
	FTDAvoidanceAgent Agent;
	Agent.Location = FVector2D(Body.Location.X, Body.Location.Y);
	Agent.Radius = Body.Radius;
	Agent.PreferredVelocity = Intent.Direction * Intent.SpeedScale * MoveSpeed;
	Agent.PreviousCommandVelocity = Avoidance.PreviousCommandVelocity;
	Agent.PreviousCandidateIndex = Avoidance.PreviousCandidateIndex;
	Agent.MaxSpeed = MoveSpeed;
	Agent.SimulationId = Body.SimulationId;
	FTDAvoidanceObstacleList Obstacles;
	GatherMonsterObstacles(SlotIndex, Obstacles);
	if (Agent.PreferredVelocity.Size() < FTDCircleAvoidance::MinimumPreferredSpeed)
	{
		Avoidance.SlowedWaitStepCount = 0;
		if (Brain.State != ETDMonsterFsmState::Cast)
		{
			Avoidance.PreviousCandidateIndex = INDEX_NONE;
		}
		if (!FTDCircleAvoidance::CanYield(Brain.GetReportedState(), Brain.bIsFrozen, IsHoldingEngagedAttacker(SlotIndex)))
		{
			return FVector2D::ZeroVector;
		}
		return FTDCircleAvoidance::ApplyCommandToIntent(FTDCircleAvoidance::ComputeYieldVelocity(Agent, Obstacles), MoveSpeed, Intent);
	}
	const AActor* EngagedTarget = Brain.IsEngaged() ? Brain.Target.Get() : nullptr;
	const int32 ExcludedExternalIndex = EngagedTarget ? ExternalActors.Find(EngagedTarget) : INDEX_NONE;
	FTDCircleAvoidance::AppendNearbyExternalObstacles(Agent.Location, Agent.Radius, ExternalBodies, ExcludedExternalIndex, Obstacles);
	const FTDAvoidanceCommand Command = FTDCircleAvoidance::ComputeAvoidanceCommand(Agent, Obstacles);
	Avoidance.PreviousCandidateIndex = Command.CandidateIndex;
	Avoidance.SlowedWaitStepCount = Command.bIsSlowedWaiting ? Avoidance.SlowedWaitStepCount + 1 : 0;
	return FTDCircleAvoidance::ApplyCommandToIntent(Command.Velocity, MoveSpeed, Intent);
}

void UTDMonsterThinkSubsystem::GatherExternalBodies(TArray<FTDAvoidanceObstacle>& OutBodies, TArray<const AActor*>& OutActors) const
{
	OutBodies.Reset();
	OutActors.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TArray<AActor*> Candidates;
	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		const APlayerController* PlayerController = Iterator->Get();
		APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (PlayerPawn)
		{
			Candidates.AddUnique(PlayerPawn);
		}
	}
	for (TActorIterator<ATDCompanionCharacter> Iterator(World); Iterator; ++Iterator)
	{
		Candidates.AddUnique(*Iterator);
	}
	Candidates.RemoveAll([](AActor* Candidate)
	{
		return !IsValid(Candidate) || Cast<ITDMonsterBody>(Candidate) || !UTDCombatLibrary::IsActorAlive(Candidate);
	});
	Algo::Sort(Candidates, [](const AActor* Left, const AActor* Right)
	{
		return Left->GetUniqueID() < Right->GetUniqueID();
	});
	for (const AActor* Candidate : Candidates)
	{
		float CollisionRadius = 0.f;
		float CollisionHalfHeight = 0.f;
		Candidate->GetSimpleCollisionCylinder(CollisionRadius, CollisionHalfHeight);
		const FVector Location = Candidate->GetActorLocation();
		const FVector Velocity = Candidate->GetVelocity();
		FTDAvoidanceObstacle& ExternalBody = OutBodies.AddDefaulted_GetRef();
		ExternalBody.Location = FVector2D(Location.X, Location.Y);
		ExternalBody.Velocity = FVector2D(Velocity.X, Velocity.Y);
		ExternalBody.Radius = CollisionRadius;
		OutActors.Add(Candidate);
	}
}

void UTDMonsterThinkSubsystem::GatherMonsterObstacles(const int32 SlotIndex, FTDAvoidanceObstacleList& OutObstacles) const
{
	OutObstacles.Reset();
	FTDNeighborCandidateList Neighbors;
	NeighborGrid.GatherAvoidanceNeighbors(Bodies, SlotIndex, Neighbors);
	for (const FTDNeighborCandidate& Neighbor : Neighbors)
	{
		const FTDMonsterBodyFragment& Other = Bodies[Neighbor.SlotIndex];
		FTDAvoidanceObstacle& Obstacle = OutObstacles.AddDefaulted_GetRef();
		Obstacle.Location = FVector2D(Other.Location.X, Other.Location.Y);
		Obstacle.Velocity = Avoidances[Neighbor.SlotIndex].PreviousCommandVelocity;
		Obstacle.Radius = Other.Radius;
		Obstacle.SimulationId = Other.SimulationId;
		Obstacle.bIsMonster = true;
	}
}

bool UTDMonsterThinkSubsystem::IsHoldingEngagedAttacker(const int32 SlotIndex) const
{
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	return Brain.IsEngaged() && Brain.Target.IsValid();
}

bool UTDMonsterThinkSubsystem::IsReturnStallExcusedByAvoidance(const int32 SlotIndex) const
{
	const int32 SlowedWaitStepCount = Avoidances[SlotIndex].SlowedWaitStepCount;
	return SlowedWaitStepCount > 0 && SlowedWaitStepCount <= TDMonsterAI::SecondsToSteps(AvoidanceWaitGraceSeconds);
}

void UTDMonsterThinkSubsystem::RebuildNeighborGrid()
{
	NeighborGrid.Rebuild(Bodies, OrderedSlots);
	bIsNeighborGridStale = false;
}

void UTDMonsterThinkSubsystem::RefreshNeighborGridIfStale()
{
	if (bIsNeighborGridStale)
	{
		RebuildNeighborGrid();
	}
}

void UTDMonsterThinkSubsystem::CountAttackersByTarget()
{
	AttackersByTarget.Reset();
	for (const int32 SlotIndex : OrderedSlots)
	{
		const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
		const AActor* Target = Brain.Target.Get();
		if (Brain.State == ETDMonsterFsmState::Cast && Target)
		{
			++AttackersByTarget.FindOrAdd(Target);
		}
	}
}

bool UTDMonsterThinkSubsystem::HasAttackToken(const int32 SlotIndex) const
{
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	const AActor* Target = Brain.Target.Get();
	if (!Target)
	{
		return false;
	}
	const int32* AttackerCount = AttackersByTarget.Find(Target);
	const int32 OtherAttackers = (AttackerCount ? *AttackerCount : 0) - (Brain.State == ETDMonsterFsmState::Cast ? 1 : 0);
	return OtherAttackers < MaxConcurrentAttackersPerTarget;
}

bool UTDMonsterThinkSubsystem::RefreshTarget(const int32 SlotIndex)
{
	FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	AActor* NewTarget = AcquireTarget(SlotIndex);
	const bool bHasChanged = Brain.Target.Get() != NewTarget;
	Brain.Target = NewTarget;
	return bHasChanged;
}

void UTDMonsterThinkSubsystem::PresentFrame(const float DeltaSeconds)
{
	const bool bShouldDrawDebug = CVarMonsterAIDebugDraw.GetValueOnGameThread() != 0;
	for (const int32 SlotIndex : TArray<int32>(OrderedSlots))
	{
		if (!Bodies[SlotIndex].bIsActive)
		{
			continue;
		}
		ITDMonsterBody* MonsterBody = ResolveBody(Bodies[SlotIndex]);
		if (!MonsterBody)
		{
			continue;
		}
		const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
		const FTDMonsterMoveIntent& Intent = Intents[SlotIndex];
		MonsterBody->ApplyBodyMoveIntent(Intent.Direction, Intent.SpeedScale);
		const AActor* Target = Brain.Target.Get();
		if (Intent.bShouldFaceTarget && Intent.Direction.IsNearlyZero() && Target)
		{
			MonsterBody->FaceBodyToward(Target->GetActorLocation());
		}
		MonsterBody->PresentBody(DeltaSeconds, Brain.GetReportedState());
		if (bShouldDrawDebug)
		{
			DrawDebugState(SlotIndex);
		}
	}
}

AActor* UTDMonsterThinkSubsystem::AcquireTarget(const int32 SlotIndex) const
{
	AActor* Self = Bodies[SlotIndex].Actor.Get();
	UWorld* World = GetWorld();
	if (!Self || !World)
	{
		return nullptr;
	}
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (PlayerPawn && UTDCombatLibrary::IsActorAlive(PlayerPawn) && UTDCombatLibrary::AreActorsHostile(Self, PlayerPawn))
	{
		return PlayerPawn;
	}
	const UTDDamageSubsystem* DamageSubsystem = World->GetSubsystem<UTDDamageSubsystem>();
	const UTDCombatComponent* SelfCombat = UTDCombatLibrary::GetCombatComponent(Self);
	if (!DamageSubsystem || !SelfCombat)
	{
		return nullptr;
	}
	FTDDamageContext Context;
	Context.Caster = Self;
	Context.Stats = SelfCombat->GetStats();
	TArray<UTDCombatComponent*> Candidates;
	DamageSubsystem->GatherTargets(Bodies[SlotIndex].Location, EngageRadius, 1000.f, Context, ETDDamageTargetPolicy::Enemies, Candidates);
	AActor* BestTarget = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const UTDCombatComponent* Candidate : Candidates)
	{
		AActor* CandidateActor = Candidate ? Candidate->GetOwner() : nullptr;
		if (!CandidateActor || !Candidate->IsAlive())
		{
			continue;
		}
		const float Distance = PlanarDistance(Bodies[SlotIndex].Location, CandidateActor->GetActorLocation());
		const bool bIsCloser = Distance < BestDistance;
		const bool bIsTieBrokenByPosition = Distance == BestDistance && BestTarget && IsPositionBefore(CandidateActor->GetActorLocation(), BestTarget->GetActorLocation());
		if (bIsCloser || bIsTieBrokenByPosition)
		{
			BestTarget = CandidateActor;
			BestDistance = Distance;
		}
	}
	return BestTarget;
}

FTDBrainInputs UTDMonsterThinkSubsystem::BuildInputs(const int32 SlotIndex, const FTDMonsterStepContext& Context) const
{
	const FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	FTDBrainInputs Inputs;
	const UTDCombatComponent* SelfCombat = UTDCombatLibrary::GetCombatComponent(Body.Actor.Get());
	const float MaxHealth = Brain.Definition->GetStatsForPhase(Brain.PhaseIndex).MaxHealth;
	Inputs.SelfHealthRatio = SelfCombat && MaxHealth > 0.f ? FMath::Clamp(SelfCombat->GetCurrentHealth() / MaxHealth, 0.f, 1.f) : 1.f;
	if (Context.Target)
	{
		Inputs.bHasTarget = true;
		const FVector2D Offset(Context.TargetLocation.X - Body.Location.X, Context.TargetLocation.Y - Body.Location.Y);
		Inputs.DistanceToTarget = Offset.Size();
		const FVector2D Forward(FMath::Cos(FMath::DegreesToRadians(Body.Yaw)), FMath::Sin(FMath::DegreesToRadians(Body.Yaw)));
		Inputs.FacingDot = Inputs.DistanceToTarget > KINDA_SMALL_NUMBER ? FVector2D::DotProduct(Forward, Offset / Inputs.DistanceToTarget) : 1.f;
		const UTDCombatComponent* TargetCombat = UTDCombatLibrary::GetCombatComponent(Context.Target);
		Inputs.bIsTargetAttacking = TargetCombat && TargetCombat->HasMatchingGameplayTag(TDGameplayTags::State_Attacking);
	}
	for (int32 AbilityIndex = 0; Context.bHasAttackToken && AbilityIndex < Brain.AbilityCooldownSteps.Num() && AbilityIndex < 64; ++AbilityIndex)
	{
		if (Brain.AbilityCooldownSteps[AbilityIndex] == 0 && Brain.Definition->Abilities[AbilityIndex].Spell)
		{
			Inputs.ReadyAbilityMask |= uint64(1) << AbilityIndex;
		}
	}

	FTDNeighborCandidateList Neighbors;
	const int32 SelfTeam = Brain.Definition->Stats.Team;
	NeighborGrid.GatherNeighborsWithinDistance(Bodies, SlotIndex, NeighborRadius, FTDBrainInputs::MaxNeighbors, [this, SelfTeam](const int32 OtherIndex)
	{
		return Brains[OtherIndex].Definition->Stats.Team == SelfTeam;
	}, Neighbors);
	Inputs.NeighborCount = Neighbors.Num();
	for (int32 NeighborIndex = 0; NeighborIndex < Inputs.NeighborCount; ++NeighborIndex)
	{
		Inputs.Neighbors[NeighborIndex] = { static_cast<float>(Neighbors[NeighborIndex].SortDistance), Neighbors[NeighborIndex].SimulationId };
	}
	return Inputs;
}

FTDMonsterStepContext UTDMonsterThinkSubsystem::MakeContext(const int32 SlotIndex) const
{
	FTDMonsterStepContext Context;
	Context.Body = ResolveBody(Bodies[SlotIndex]);
	Context.SelfLocation = Bodies[SlotIndex].Location;
	AActor* Target = Brains[SlotIndex].Target.Get();
	if (Target && UTDCombatLibrary::IsActorAlive(Target))
	{
		Context.Target = Target;
		Context.TargetLocation = Target->GetActorLocation();
		Context.TargetVelocity = Target->GetVelocity();
		Context.bHasAttackToken = HasAttackToken(SlotIndex);
	}
	return Context;
}

void UTDMonsterThinkSubsystem::Engage(const int32 SlotIndex)
{
	FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	if (Brain.IsEngaged())
	{
		return;
	}
	if (!Brain.Target.IsValid())
	{
		Brain.Target = AcquireTarget(SlotIndex);
	}
	Brain.Engagement = ETDMonsterEngagement::Engaged;
	Brain.bForceThink = true;
	Brain.OutOfReachStepCount = 0;
}

void UTDMonsterThinkSubsystem::BeginReturn(const int32 SlotIndex)
{
	FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
	Body.HomeLocation = Body.SpawnLocation;
	FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	FTDMonsterActionExecutor::InterruptActivity(Brain, MakeContext(SlotIndex));
	Brain.Engagement = ETDMonsterEngagement::Returning;
	Brain.Target.Reset();
	Intents[SlotIndex] = FTDMonsterMoveIntent();
	Brain.EngageLockSteps = 0;
	Brain.OutOfReachStepCount = 0;
	Brain.ReturnStallStepCount = 0;
	Brain.ClosestReturnDistance = PlanarDistance(Body.Location, Body.HomeLocation);
}

void UTDMonsterThinkSubsystem::FinishReturn(const int32 SlotIndex)
{
	FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	Brain.Engagement = ETDMonsterEngagement::Home;
	Brain.ReturnStallStepCount = 0;
	Brain.ReengageBlockSteps = FMath::Max(Brain.ReengageBlockSteps, TDMonsterAI::SecondsToSteps(ArrivalSettleSeconds));
}

FTDMonsterMoveIntent UTDMonsterThinkSubsystem::MakeReturnHomeIntent(const int32 SlotIndex) const
{
	FTDMonsterMoveIntent Intent;
	const FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
	const FVector2D ToHome(Body.HomeLocation.X - Body.Location.X, Body.HomeLocation.Y - Body.Location.Y);
	if (ToHome.Size() <= HomeArrivalRadius)
	{
		return Intent;
	}
	Intent.Direction = ToHome.GetSafeNormal();
	Intent.SpeedScale = ReturnHomeSpeedScale;
	return Intent;
}

void UTDMonsterThinkSubsystem::DrawDebugState(const int32 SlotIndex) const
{
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	const FString Label = FString::Printf(TEXT("%s | %s"), FTDMonsterActionRegistry::GetStateName(Brain.GetReportedState()), *Brain.GetCurrentActionId().ToString());
	const FColor ReturningOrHomeColor = Brain.Engagement == ETDMonsterEngagement::Returning ? FColor::Cyan : FColor::Silver;
	const FColor Color = Brain.IsEngaged() ? FColor::Orange : ReturningOrHomeColor;
	DrawDebugString(GetWorld(), Bodies[SlotIndex].Location + FVector(0.f, 0.f, 140.f), Label, nullptr, Color, 0.f, true, 1.1f);
}
