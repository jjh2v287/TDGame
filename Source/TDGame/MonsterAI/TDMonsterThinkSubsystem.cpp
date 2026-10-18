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
#include "ProfilingDebugging/CpuProfilerTrace.h"
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

	TAutoConsoleVariable<int32> CVarMonsterAITactics(
		TEXT("TD.MonsterAI.Tactics"),
		1,
		TEXT("Selects the pack tactics layer. 0 = legacy cap of concurrent attackers per target, 1 = weighted attack tokens, siege slots and combat director."),
		ECVF_Cheat);

	TAutoConsoleVariable<int32> CVarMonsterAITacticsDebug(
		TEXT("TD.MonsterAI.Tactics.Debug"),
		0,
		TEXT("Draws tactics rings, slots and tokens. 0 = off, 1 = on."),
		ECVF_Cheat);

	constexpr int32 TacticsBudgetLowerPassCount = 2;

	int64 SquareCm(const int32 Value)
	{
		return static_cast<int64>(Value) * Value;
	}

	int32 GetTacticsPoolIndex(const ETDTacticsRole Role)
	{
		return Role == ETDTacticsRole::Ranged ? FTDTargetTacticsRecord::RangedPoolIndex : FTDTargetTacticsRecord::MeleePoolIndex;
	}

	int32 GetGrantRadiusCm(const FTDMonsterTacticsFragment& Fragment)
	{
		return Fragment.Role == ETDTacticsRole::Ranged ? Fragment.MaxReachCm + FTDCombatTactics::RangedGrantMargin : FTDCombatTactics::MeleeGrantRadius;
	}

	void ClearTacticsToken(FTDMonsterTacticsFragment& Fragment)
	{
		Fragment.TokenState = ETDTacticsTokenState::None;
		Fragment.TokenRecordId = 0;
		Fragment.CommittedCostQuarters = 0;
		Fragment.InnerSector = -1;
	}

	void ClearTacticsSectors(FTDMonsterTacticsFragment& Fragment)
	{
		Fragment.WaitRing = -1;
		Fragment.WaitSector = -1;
		Fragment.InnerSector = -1;
		Fragment.RangedSector = -1;
		Fragment.SlotProgressAnchorStep = 0;
	}

	bool HasReachableReadyAbility(const FTDMonsterBrainFragment& Brain, const int64 DistanceSquared)
	{
		const FTDResolvedMonsterDefinition& Definition = *Brain.Definition;
		const uint64 ReferencedMask = FTDCombatTactics::GetReferencedAbilityMask(Definition);
		for (int32 AbilityIndex = 0; AbilityIndex < Definition.Abilities.Num() && AbilityIndex < Brain.AbilityCooldownSteps.Num() && AbilityIndex < 64; ++AbilityIndex)
		{
			if ((ReferencedMask & (uint64(1) << AbilityIndex)) == 0 || Brain.AbilityCooldownSteps[AbilityIndex] != 0)
			{
				continue;
			}
			const int32 ReachCm = FMath::FloorToInt32(Definition.Abilities[AbilityIndex].Range) + FTDCombatTactics::ReachSlackCm;
			if (ReachCm > 0 && SquareCm(ReachCm) >= DistanceSquared)
			{
				return true;
			}
		}
		return false;
	}

	void UpdatePoolBudget(FTDTacticsPool& Pool, const int32 BaseTokens, const int32 BudgetPercent, const bool bIsPeriodic)
	{
		Pool.BaseTokens = BaseTokens;
		const int32 ComputedQuarters = FTDCombatTactics::ComputePoolTokens(BaseTokens, BudgetPercent) * TDMonsterAI::QuartersPerToken;
		if (ComputedQuarters >= Pool.BudgetQuarters)
		{
			Pool.BudgetQuarters = ComputedQuarters;
			Pool.LowerPassCount = bIsPeriodic ? 0 : Pool.LowerPassCount;
			return;
		}
		if (!bIsPeriodic)
		{
			return;
		}
		++Pool.LowerPassCount;
		if (Pool.LowerPassCount < TacticsBudgetLowerPassCount)
		{
			return;
		}
		Pool.BudgetQuarters = ComputedQuarters;
		Pool.LowerPassCount = 0;
	}

	void UpdateRecordBudgets(FTDTargetTacticsRecord& Record, const bool bIsPeriodic)
	{
		int32 BaseTokens[2] = { 0, 0 };
		FTDCombatTactics::ComputePoolBaseTokens(
			Record.Pools[FTDTargetTacticsRecord::MeleePoolIndex].EngagedCount,
			Record.Pools[FTDTargetTacticsRecord::RangedPoolIndex].EngagedCount,
			BaseTokens[FTDTargetTacticsRecord::MeleePoolIndex],
			BaseTokens[FTDTargetTacticsRecord::RangedPoolIndex]);
		const int32 BudgetPercent = FTDCombatDirector::GetBudgetPercent(Record.Director.Phase);
		for (int32 PoolIndex = 0; PoolIndex < 2; ++PoolIndex)
		{
			UpdatePoolBudget(Record.Pools[PoolIndex], BaseTokens[PoolIndex], BudgetPercent, bIsPeriodic);
		}
	}

	bool IsOrderedBefore(const FTDTacticsCandidateKey& Left, const FTDTacticsCandidateKey& Right)
	{
		if (Left.DistanceSquared != Right.DistanceSquared)
		{
			return Left.DistanceSquared < Right.DistanceSquared;
		}
		return Left.SimulationId < Right.SimulationId;
	}

	const TCHAR* GetTacticsTokenLabel(const bool bIsTacticsActive, const FTDMonsterTacticsFragment& Fragment)
	{
		if (!bIsTacticsActive)
		{
			return TEXT("-");
		}
		if (Fragment.Role == ETDTacticsRole::Boss)
		{
			return TEXT("B");
		}
		switch (Fragment.TokenState)
		{
		case ETDTacticsTokenState::Reserved:
			return TEXT("R");
		case ETDTacticsTokenState::Committed:
			return TEXT("C");
		default:
			return TEXT("-");
		}
	}

	FColor GetTacticsSlotColor(const ETDTacticsSlotKind Kind)
	{
		switch (Kind)
		{
		case ETDTacticsSlotKind::Wait:
			return FColor::Yellow;
		case ETDTacticsSlotKind::Inner:
			return FColor::Red;
		case ETDTacticsSlotKind::Ranged:
			return FColor::Cyan;
		default:
			return FColor::White;
		}
	}

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
	Tactics.Reset();
	TacticsRecords.Reset();
	PendingKillLocations.Reset();
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
		Tactics.AddDefaulted();
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
	Tactics[SlotIndex] = FTDCombatTactics::MakeFragment(*Definition);

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
	const AActor* DyingActor = Bodies[Handle.Index].Actor.Get();
	if (bIsTacticsActive && DyingActor && !UTDCombatLibrary::IsActorAlive(DyingActor))
	{
		PendingKillLocations.Add(Bodies[Handle.Index].Location);
	}
	DetachTacticsSlot(Handle.Index);
	ClearTacticsToken(Tactics[Handle.Index]);
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
	const AActor* CurrentTarget = Brain.Target.Get();
	if (CurrentTarget && FTDCombatTactics::ComputePlanarDistanceSquared(CurrentTarget->GetActorLocation(), SourceLocation) <= SquareCm(FTDCombatTactics::RetaliationSourceRadiusCm))
	{
		Tactics[Handle.Index].LastDamagedStep = StepIndex;
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
		Lines.Add(FString::Printf(TEXT("sim=%u actor=%s def=%s state=%s action=%s engaged=%d engagement=%s phase=%d hp=%.1f pos=(%.0f,%.0f)%s"),
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
			Body.Location.Y,
			*MakeTacticsDebugSuffix(SlotIndex)));
	}
	return Lines;
}

TArray<FString> UTDMonsterThinkSubsystem::GetTacticsDebugLines() const
{
	TArray<FString> Lines;
	for (const FTDTargetTacticsRecord& Record : TacticsRecords)
	{
		const AActor* Target = Record.Target.Get();
		const FTDTacticsPool& MeleePool = Record.Pools[FTDTargetTacticsRecord::MeleePoolIndex];
		const FTDTacticsPool& RangedPool = Record.Pools[FTDTargetTacticsRecord::RangedPoolIndex];
		Lines.Add(FString::Printf(TEXT("record=%u target=%s melee=%d ranged=%d base_tokens=%d/%d budget=%d/%d held=%d/%d phase=%s intensity=%d spacing=%d mercy=%d freeze=%d grace=%d"),
			Record.RecordId,
			Target ? *Target->GetName() : TEXT("None"),
			MeleePool.EngagedCount,
			RangedPool.EngagedCount,
			MeleePool.BaseTokens,
			RangedPool.BaseTokens,
			MeleePool.BudgetQuarters,
			RangedPool.BudgetQuarters,
			CountHeldQuarters(Record.RecordId, FTDTargetTacticsRecord::MeleePoolIndex),
			CountHeldQuarters(Record.RecordId, FTDTargetTacticsRecord::RangedPoolIndex),
			FTDCombatDirector::GetPhaseName(Record.Director.Phase),
			Record.Director.Intensity,
			FTDCombatDirector::GetStartSpacingSteps(Record.Director),
			Record.Director.bIsMercyOpen ? 1 : 0,
			StepIndex < Record.GrantFreezeUntilStep ? 1 : 0,
			StepIndex < Record.OpeningGraceUntilStep ? 1 : 0));
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
	SnapshotTacticsSettings();
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
	if (bIsTacticsActive)
	{
		UpdateTactics();
	}
	else
	{
		CountAttackersByTarget();
	}
	TArray<FTDMonsterSlotHandle> ThinkOrder;
	for (const int32 SlotIndex : OrderedSlots)
	{
		FTDMonsterSlotHandle& Handle = ThinkOrder.AddDefaulted_GetRef();
		Handle.Index = SlotIndex;
		Handle.Generation = Bodies[SlotIndex].Generation;
	}
	for (const FTDMonsterSlotHandle& Handle : ThinkOrder)
	{
		if (!IsHandleCurrent(Handle))
		{
			continue;
		}
		const int32 SlotIndex = Handle.Index;
		const bool bWasCasting = Brains[SlotIndex].State == ETDMonsterFsmState::Cast;
		ThinkAndAct(SlotIndex);
		if (bIsTacticsActive)
		{
			if (Brains[SlotIndex].CastStartCount != Tactics[SlotIndex].LastCastStartCount)
			{
				ConfirmTacticsAttackStart(SlotIndex);
			}
			continue;
		}
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
	const AActor* EngagedTarget = Brain.IsEngaged() && !IsAvoidingTacticsTarget(SlotIndex) ? Brain.Target.Get() : nullptr;
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
	if (!Brain.IsEngaged() || !Brain.Target.IsValid())
	{
		return false;
	}
	const FTDTargetTacticsRecord* Record = nullptr;
	const bool bIsWaitingInRing = ResolveTacticsSlotKind(SlotIndex, Record) == ETDTacticsSlotKind::Wait && Brain.State != ETDMonsterFsmState::Cast;
	return !bIsWaitingInRing;
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
	if (bIsTacticsActive)
	{
		return HasTacticsAttackToken(SlotIndex);
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
		if (bShouldDrawTacticsDebug && bIsTacticsActive)
		{
			DrawTacticsDebugState(SlotIndex);
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
	Inputs.bHasAttackToken = Context.bHasAttackToken;
	Inputs.bHasRingSlot = Context.TacticsSlotKind != ETDTacticsSlotKind::None;

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
	FillTacticsContext(SlotIndex, Context);
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

void UTDMonsterThinkSubsystem::SnapshotTacticsSettings()
{
	bShouldDrawTacticsDebug = CVarMonsterAITacticsDebug.GetValueOnGameThread() != 0;
	const bool bShouldUseTactics = CVarMonsterAITactics.GetValueOnGameThread() != 0;
	if (bShouldUseTactics == bIsTacticsActive)
	{
		return;
	}
	bIsTacticsActive = bShouldUseTactics;
	ResetTactics();
}

void UTDMonsterThinkSubsystem::ResetTactics()
{
	for (int32 SlotIndex = 0; SlotIndex < Tactics.Num(); ++SlotIndex)
	{
		const FTDMonsterTacticsFragment Previous = Tactics[SlotIndex];
		FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
		Fragment = FTDMonsterTacticsFragment();
		Fragment.Role = Previous.Role;
		Fragment.MeleeReachCm = Previous.MeleeReachCm;
		Fragment.MaxReachCm = Previous.MaxReachCm;
		Fragment.ReserveCostQuarters = Previous.ReserveCostQuarters;
		Fragment.LastCastStartCount = Brains[SlotIndex].CastStartCount;
	}
	TacticsRecords.Reset();
	PendingKillLocations.Reset();
}

void UTDMonsterThinkSubsystem::UpdateTactics()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDMonsterAI_Tactics);
	ApplyPendingKills();
	const bool bHasNewRecord = LinkTacticsRecords();
	const bool bHasReturnedToken = SyncTacticsSlots();
	const bool bIsRingPass = StepIndex % FTDCombatTactics::RingPassPeriodSteps == 0;
	UpdateTacticsDirectors(bIsRingPass);
	for (FTDTargetTacticsRecord& Record : TacticsRecords)
	{
		UpdateRecordBudgets(Record, false);
	}
	if (bIsRingPass)
	{
		AssignWaitAndRangedSectors();
	}
	const bool bIsTokenPass = StepIndex % FTDCombatTactics::TokenPassPeriodSteps == 0;
	if (bIsTokenPass || bHasReturnedToken || bHasNewRecord)
	{
		RunTokenPass(bIsTokenPass);
	}
	RemoveFinishedTacticsRecords();
}

void UTDMonsterThinkSubsystem::ApplyPendingKills()
{
	for (const FVector& KillLocation : PendingKillLocations)
	{
		for (FTDTargetTacticsRecord& Record : TacticsRecords)
		{
			const AActor* Target = Record.Target.Get();
			if (Target && FTDCombatTactics::ComputePlanarDistanceSquared(KillLocation, Target->GetActorLocation()) <= SquareCm(FTDCombatDirector::KillRadiusCm))
			{
				FTDCombatDirector::AddIntensity(Record.Director, FTDCombatDirector::KillIntensity);
			}
		}
	}
	PendingKillLocations.Reset();
}

bool UTDMonsterThinkSubsystem::LinkTacticsRecords()
{
	bool bHasNewRecord = false;
	for (FTDTargetTacticsRecord& Record : TacticsRecords)
	{
		Record.MemberSlots.Reset();
	}
	for (const int32 SlotIndex : OrderedSlots)
	{
		const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
		AActor* Target = Brain.Target.Get();
		if (!Brain.IsEngaged() || !Target || !UTDCombatLibrary::IsActorAlive(Target))
		{
			DetachTacticsSlot(SlotIndex);
			continue;
		}
		int32 RecordIndex = FindTacticsRecordIndex(Target);
		if (RecordIndex == INDEX_NONE)
		{
			RecordIndex = CreateTacticsRecord(Target);
			bHasNewRecord = true;
		}
		const uint32 RecordId = TacticsRecords[RecordIndex].RecordId;
		if (Tactics[SlotIndex].TargetRecordId != RecordId)
		{
			const bool bIsFirstLink = Tactics[SlotIndex].TargetRecordId == 0;
			DetachTacticsSlot(SlotIndex);
			Tactics[SlotIndex].TargetRecordId = RecordId;
			if (bIsFirstLink)
			{
				Tactics[SlotIndex].LastAttackEndStep = StepIndex;
			}
		}
		TacticsRecords[RecordIndex].MemberSlots.Add(SlotIndex);
	}
	for (FTDTargetTacticsRecord& Record : TacticsRecords)
	{
		CountTacticsEngaged(Record);
	}
	return bHasNewRecord;
}

int32 UTDMonsterThinkSubsystem::CreateTacticsRecord(AActor* Target)
{
	FTDTargetTacticsRecord& Record = TacticsRecords.AddDefaulted_GetRef();
	Record.RecordId = NextTacticsRecordId++;
	Record.Target = Target;
	Record.OpeningGraceUntilStep = StepIndex + FTDCombatDirector::OpeningGraceSteps;
	const UTDCombatComponent* Combat = UTDCombatLibrary::GetCombatComponent(Target);
	Record.PrevHealthCenti = Combat ? FMath::FloorToInt64(static_cast<double>(Combat->GetCurrentHealth()) * 100.0) : 0;
	const FVector TargetLocation = Target->GetActorLocation();
	Record.PreviousTargetXCm = FMath::FloorToInt32(TargetLocation.X);
	Record.PreviousTargetYCm = FMath::FloorToInt32(TargetLocation.Y);
	Record.PreviousTargetStep = StepIndex;
	return TacticsRecords.Num() - 1;
}

void UTDMonsterThinkSubsystem::CountTacticsEngaged(FTDTargetTacticsRecord& Record)
{
	Record.Pools[FTDTargetTacticsRecord::MeleePoolIndex].EngagedCount = 0;
	Record.Pools[FTDTargetTacticsRecord::RangedPoolIndex].EngagedCount = 0;
	Record.Pools[FTDTargetTacticsRecord::MeleePoolIndex].EngagedCostQuarters = 0;
	Record.Pools[FTDTargetTacticsRecord::RangedPoolIndex].EngagedCostQuarters = 0;
	const AActor* Target = Record.Target.Get();
	const FVector TargetLocation = Target ? Target->GetActorLocation() : FVector::ZeroVector;
	for (const int32 SlotIndex : Record.MemberSlots)
	{
		const ETDTacticsRole Role = Tactics[SlotIndex].Role;
		if (!Target || Role == ETDTacticsRole::Boss || FTDCombatTactics::ComputePlanarDistanceSquared(Bodies[SlotIndex].Location, TargetLocation) > SquareCm(FTDCombatTactics::EngagedCountRadius))
		{
			continue;
		}
		FTDTacticsPool& Pool = Record.Pools[GetTacticsPoolIndex(Role)];
		++Pool.EngagedCount;
		Pool.EngagedCostQuarters += Tactics[SlotIndex].ReserveCostQuarters;
	}
	if (!Record.MemberSlots.IsEmpty())
	{
		Record.ZeroEngagedSinceStep = 0;
		return;
	}
	if (Record.ZeroEngagedSinceStep == 0)
	{
		Record.ZeroEngagedSinceStep = StepIndex;
	}
}

bool UTDMonsterThinkSubsystem::SyncTacticsSlots()
{
	bool bHasReturnedToken = false;
	for (const int32 SlotIndex : OrderedSlots)
	{
		if (Tactics[SlotIndex].Role == ETDTacticsRole::Boss)
		{
			continue;
		}
		bHasReturnedToken |= ReturnFinishedTacticsToken(SlotIndex);
		RevokeInvalidReservation(SlotIndex);
		DetectBlockedTacticsSector(SlotIndex);
	}
	return bHasReturnedToken;
}

bool UTDMonsterThinkSubsystem::ReturnFinishedTacticsToken(const int32 SlotIndex)
{
	FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	if (Fragment.TokenState != ETDTacticsTokenState::Committed || Brain.State == ETDMonsterFsmState::Cast || Brain.SequenceIndex != INDEX_NONE)
	{
		return false;
	}
	Fragment.LastAttackEndStep = StepIndex;
	Fragment.RegrantReadyStep = StepIndex + FTDCombatTactics::RegrantCooldownSteps;
	const int32 RecordIndex = FindTacticsRecordIndexById(Fragment.TokenRecordId);
	const bool bCanKeepToken = RecordIndex != INDEX_NONE
		&& Fragment.TargetRecordId == Fragment.TokenRecordId
		&& CanKeepTacticsToken(SlotIndex)
		&& !HasOtherTokenCandidate(SlotIndex, TacticsRecords[RecordIndex]);
	if (!bCanKeepToken)
	{
		ClearTacticsToken(Fragment);
		return true;
	}
	Fragment.TokenState = ETDTacticsTokenState::Reserved;
	Fragment.CommittedCostQuarters = 0;
	Fragment.GrantStep = StepIndex;
	return true;
}

void UTDMonsterThinkSubsystem::RevokeInvalidReservation(const int32 SlotIndex)
{
	FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	if (Fragment.TokenState != ETDTacticsTokenState::Reserved)
	{
		return;
	}
	const int32 RecordIndex = FindTacticsRecordIndexById(Fragment.TokenRecordId);
	const bool bHasLostReservation = RecordIndex == INDEX_NONE || !Brain.IsEngaged() || Fragment.TargetRecordId != Fragment.TokenRecordId || Brain.bIsFrozen || Brain.State == ETDMonsterFsmState::Stagger;
	if (bHasLostReservation)
	{
		ClearTacticsToken(Fragment);
		return;
	}
	const AActor* Target = TacticsRecords[RecordIndex].Target.Get();
	const int32 RevokeRadiusCm = GetGrantRadiusCm(Fragment) * 5 / 4;
	const bool bIsExpired = StepIndex - Fragment.GrantStep >= static_cast<uint64>(FTDCombatTactics::ReservationExpirySteps);
	const bool bIsTooFar = !Target || FTDCombatTactics::ComputePlanarDistanceSquared(Bodies[SlotIndex].Location, Target->GetActorLocation()) > SquareCm(RevokeRadiusCm);
	const bool bIsFleeing = Brain.ActivePrimitive == ETDMonsterPrimitive::MoveAway;
	if (!bIsExpired && !bIsTooFar && !bIsFleeing)
	{
		return;
	}
	ClearTacticsToken(Fragment);
	Fragment.RegrantReadyStep = StepIndex + FTDCombatTactics::RegrantCooldownSteps;
}

void UTDMonsterThinkSubsystem::DetectBlockedTacticsSector(const int32 SlotIndex)
{
	FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	const FTDTargetTacticsRecord* SlotRecord = nullptr;
	const ETDTacticsSlotKind Kind = ResolveTacticsSlotKind(SlotIndex, SlotRecord);
	if (Kind != ETDTacticsSlotKind::Wait && Kind != ETDTacticsSlotKind::Ranged)
	{
		Fragment.SlotProgressAnchorStep = 0;
		return;
	}
	const FTDMonsterStepContext Context = MakeContext(SlotIndex);
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	const double MoveSpeed = Brain.Definition.IsValid() ? static_cast<double>(Brain.Definition->GetStatsForPhase(Brain.PhaseIndex).MoveSpeed) : 0.0;
	const bool bIsTargetMoving = Context.TargetVelocity.SizeSquared2D() >= FMath::Square(MoveSpeed);
	int32 ProgressValue = 0;
	int32 ProgressThreshold = 0;
	if (bIsTargetMoving || !MeasureTacticsSlotProgress(SlotIndex, Kind, Context, ProgressValue, ProgressThreshold))
	{
		Fragment.SlotProgressAnchorStep = 0;
		return;
	}
	if (Fragment.SlotProgressAnchorStep == 0 || ProgressValue < Fragment.SlotProgressBestCm - ProgressThreshold)
	{
		Fragment.SlotProgressAnchorStep = StepIndex;
		Fragment.SlotProgressBestCm = ProgressValue;
		return;
	}
	if (StepIndex - Fragment.SlotProgressAnchorStep < static_cast<uint64>(FTDCombatTactics::BlockedProgressSteps))
	{
		return;
	}
	Fragment.SlotProgressAnchorStep = 0;
	const int32 RecordIndex = FindTacticsRecordIndexById(Fragment.TargetRecordId);
	if (RecordIndex == INDEX_NONE)
	{
		return;
	}
	FTDTargetTacticsRecord& Record = TacticsRecords[RecordIndex];
	const uint64 BlockedUntilStep = StepIndex + FTDCombatTactics::BlockedSectorSteps;
	if (Kind == ETDTacticsSlotKind::Wait)
	{
		Record.WaitBlockedUntilStep[Fragment.WaitRing][Fragment.WaitSector] = BlockedUntilStep;
		return;
	}
	Record.RangedBlockedUntilStep[Fragment.RangedSector] = BlockedUntilStep;
}

bool UTDMonsterThinkSubsystem::MeasureTacticsSlotProgress(const int32 SlotIndex, const ETDTacticsSlotKind Kind, const FTDMonsterStepContext& Context, int32& OutProgressValue, int32& OutProgressThreshold) const
{
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	if (Kind == ETDTacticsSlotKind::Wait)
	{
		float Gap = 0.f;
		if (!FTDMonsterActionExecutor::MeasureTacticsSlotGap(Brain, Context, Gap) || Gap <= FTDCombatTactics::BlockedArrivalCm)
		{
			return false;
		}
		OutProgressValue = FMath::FloorToInt32(Gap);
		OutProgressThreshold = FTDCombatTactics::BlockedProgressCm;
		return true;
	}
	if (Brain.State != ETDMonsterFsmState::Move || Brain.ActivePrimitive != ETDMonsterPrimitive::MoveToBand)
	{
		return false;
	}
	const double Distance = FVector::Dist2D(Context.SelfLocation, Context.TargetLocation);
	if (Distance < Brain.ActiveArgs.BandMin || Distance > Brain.ActiveArgs.BandMax)
	{
		return false;
	}
	const int32 Bearing = FTDCombatTactics::ToDecidegrees(Context.SelfLocation.X - Context.TargetLocation.X, Context.SelfLocation.Y - Context.TargetLocation.Y);
	const int32 Center = FTDCombatTactics::GetSectorCenter(FTDCombatTactics::RangedSectorCount, FTDCombatTactics::RangedSectorOffsetDecidegrees, Tactics[SlotIndex].RangedSector);
	const int32 Error = FTDCombatTactics::GetAngleDifference(Bearing, Center);
	if (Error <= FMath::FloorToInt32(Context.TacticsSlotHalfAngleDegrees * 10.f))
	{
		return false;
	}
	OutProgressValue = Error;
	OutProgressThreshold = FTDCombatTactics::BlockedAngleProgressDecidegrees;
	return true;
}

void UTDMonsterThinkSubsystem::UpdateTacticsDirectors(const bool bIsRingPass)
{
	for (FTDTargetTacticsRecord& Record : TacticsRecords)
	{
		const AActor* Target = Record.Target.Get();
		if (!Target || !UTDCombatLibrary::IsActorAlive(Target))
		{
			continue;
		}
		const int32 HealthPermille = PollTargetHealth(Record, Target);
		if (bIsRingPass)
		{
			FTDCombatDirector::RunRingPass(Record.Director, StepIndex, HealthPermille, Record.GrantFreezeUntilStep);
		}
	}
}

int32 UTDMonsterThinkSubsystem::PollTargetHealth(FTDTargetTacticsRecord& Record, const AActor* Target) const
{
	const UTDCombatComponent* Combat = UTDCombatLibrary::GetCombatComponent(Target);
	if (!Combat)
	{
		return INDEX_NONE;
	}
	const int64 CurrentCenti = FMath::FloorToInt64(static_cast<double>(Combat->GetCurrentHealth()) * 100.0);
	const int64 MaxCenti = FMath::FloorToInt64(static_cast<double>(Combat->GetStats().GetMaxHealth()) * 100.0);
	const int64 DropCenti = FMath::Max<int64>(0, Record.PrevHealthCenti - CurrentCenti);
	Record.PrevHealthCenti = CurrentCenti;
	if (MaxCenti <= 0)
	{
		return INDEX_NONE;
	}
	if (DropCenti > 0)
	{
		FTDCombatDirector::ApplyHealthDrop(Record.Director, static_cast<int32>(DropCenti * 1000 / MaxCenti), StepIndex);
	}
	return static_cast<int32>(CurrentCenti * 1000 / MaxCenti);
}

void UTDMonsterThinkSubsystem::AssignWaitAndRangedSectors()
{
	for (FTDTargetTacticsRecord& Record : TacticsRecords)
	{
		const AActor* Target = Record.Target.Get();
		if (!Target)
		{
			continue;
		}
		const FVector TargetLocation = Target->GetActorLocation();
		const int32 TargetXCm = FMath::FloorToInt32(TargetLocation.X);
		const int32 TargetYCm = FMath::FloorToInt32(TargetLocation.Y);
		const int64 ElapsedSteps = FMath::Max<int64>(1, static_cast<int64>(StepIndex - Record.PreviousTargetStep));
		const int64 VelocityX = static_cast<int64>(TargetXCm - Record.PreviousTargetXCm) * TDMonsterAI::StepsPerSecond / ElapsedSteps;
		const int64 VelocityY = static_cast<int64>(TargetYCm - Record.PreviousTargetYCm) * TDMonsterAI::StepsPerSecond / ElapsedSteps;
		Record.PreviousTargetXCm = TargetXCm;
		Record.PreviousTargetYCm = TargetYCm;
		Record.PreviousTargetStep = StepIndex;
		FTDSectorSearch Search;
		Search.StepIndex = StepIndex;
		Search.bHasMovingTarget = VelocityX * VelocityX + VelocityY * VelocityY >= SquareCm(FTDCombatTactics::MovingTargetSpeed);
		Search.MovingDecidegrees = FTDCombatTactics::ToDecidegrees(static_cast<double>(VelocityX), static_cast<double>(VelocityY));
		AssignWaitSectors(Record, TargetLocation, Search);
		AssignRangedSectors(Record, TargetLocation, Search);
	}
}

void UTDMonsterThinkSubsystem::AssignWaitSectors(FTDTargetTacticsRecord& Record, const FVector& TargetLocation, const FTDSectorSearch& Search)
{
	for (int32 Ring = 0; Ring < FTDTargetTacticsRecord::WaitRingCount; ++Ring)
	{
		FTDCombatTactics::ClearOccupants(FTDCombatTactics::MakeWaitTable(Record, Ring));
	}
	const bool bHasQueue = Record.HasMeleeQueue();
	const TArray<FTDTacticsCandidateKey> Members = GatherOrderedTacticsMembers(Record, TargetLocation, [this](const int32 SlotIndex)
	{
		return Tactics[SlotIndex].Role == ETDTacticsRole::Melee;
	});
	for (const FTDTacticsCandidateKey& Member : Members)
	{
		FTDMonsterTacticsFragment& Fragment = Tactics[Member.SlotIndex];
		if (!bHasQueue)
		{
			Fragment.WaitRing = -1;
			Fragment.WaitSector = -1;
			continue;
		}
		FTDSectorSearch MemberSearch = Search;
		const FVector& Location = Bodies[Member.SlotIndex].Location;
		MemberSearch.BearingDecidegrees = FTDCombatTactics::ToDecidegrees(Location.X - TargetLocation.X, Location.Y - TargetLocation.Y);
		ChooseWaitSector(Member.SlotIndex, Record, MemberSearch);
	}
}

void UTDMonsterThinkSubsystem::ChooseWaitSector(const int32 SlotIndex, FTDTargetTacticsRecord& Record, const FTDSectorSearch& Search)
{
	FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	const int32 BodyRadiusCm = FMath::FloorToInt32(Bodies[SlotIndex].Radius);
	auto ComputeRingWidth = [&Fragment, BodyRadiusCm](const int32 Ring)
	{
		const int32 RingRadius = FTDCombatTactics::ComputeWaitRingRadius(Fragment.MeleeReachCm, BodyRadiusCm, Ring);
		return FTDCombatTactics::ComputeSectorWidth(RingRadius, FTDCombatTactics::WaitSectorCounts[Ring], BodyRadiusCm);
	};
	FTDSectorSearch RingSearch = Search;
	int32 ChosenRing = -1;
	int32 ChosenSector = -1;
	if (Fragment.WaitRing >= 0)
	{
		RingSearch.Width = ComputeRingWidth(Fragment.WaitRing);
		if (FTDCombatTactics::CanKeepSector(FTDCombatTactics::MakeWaitTable(Record, Fragment.WaitRing), Fragment.WaitSector, RingSearch))
		{
			ChosenRing = Fragment.WaitRing;
			ChosenSector = Fragment.WaitSector;
		}
	}
	for (int32 Ring = 0; ChosenRing < 0 && Ring < FTDTargetTacticsRecord::WaitRingCount; ++Ring)
	{
		RingSearch.Width = ComputeRingWidth(Ring);
		const int32 Sector = FTDCombatTactics::FindBestSector(FTDCombatTactics::MakeWaitTable(Record, Ring), RingSearch);
		if (Sector >= 0)
		{
			ChosenRing = Ring;
			ChosenSector = Sector;
		}
	}
	if (ChosenRing >= 0)
	{
		FTDCombatTactics::ClaimSpan(FTDCombatTactics::MakeWaitTable(Record, ChosenRing), ChosenSector, ComputeRingWidth(ChosenRing), Bodies[SlotIndex].SimulationId);
	}
	if (ChosenRing != Fragment.WaitRing || ChosenSector != Fragment.WaitSector)
	{
		Fragment.SlotProgressAnchorStep = 0;
	}
	Fragment.WaitRing = static_cast<int8>(ChosenRing);
	Fragment.WaitSector = static_cast<int8>(ChosenSector);
}

void UTDMonsterThinkSubsystem::AssignRangedSectors(FTDTargetTacticsRecord& Record, const FVector& TargetLocation, const FTDSectorSearch& Search)
{
	const FTDSectorTableView Table = FTDCombatTactics::MakeRangedTable(Record);
	FTDCombatTactics::ClearOccupants(Table);
	const TArray<FTDTacticsCandidateKey> Members = GatherOrderedTacticsMembers(Record, TargetLocation, [this](const int32 SlotIndex)
	{
		return Tactics[SlotIndex].Role == ETDTacticsRole::Ranged;
	});
	for (const FTDTacticsCandidateKey& Member : Members)
	{
		FTDMonsterTacticsFragment& Fragment = Tactics[Member.SlotIndex];
		const FVector& Location = Bodies[Member.SlotIndex].Location;
		FTDSectorSearch MemberSearch = Search;
		MemberSearch.bHasSouthPenalty = true;
		MemberSearch.BearingDecidegrees = FTDCombatTactics::ToDecidegrees(Location.X - TargetLocation.X, Location.Y - TargetLocation.Y);
		MemberSearch.Width = FTDCombatTactics::ComputeSectorWidth(FMath::Max(1, Fragment.MaxReachCm), FTDCombatTactics::RangedSectorCount, FMath::FloorToInt32(Bodies[Member.SlotIndex].Radius));
		const bool bCanKeep = FTDCombatTactics::CanKeepSector(Table, Fragment.RangedSector, MemberSearch);
		const int32 ChosenSector = bCanKeep ? Fragment.RangedSector : FTDCombatTactics::FindBestSector(Table, MemberSearch);
		if (ChosenSector >= 0)
		{
			FTDCombatTactics::ClaimSpan(Table, ChosenSector, MemberSearch.Width, Bodies[Member.SlotIndex].SimulationId);
		}
		if (ChosenSector != Fragment.RangedSector)
		{
			Fragment.SlotProgressAnchorStep = 0;
		}
		Fragment.RangedSector = static_cast<int8>(ChosenSector);
	}
}

void UTDMonsterThinkSubsystem::AssignInnerSectors(FTDTargetTacticsRecord& Record, const FVector& TargetLocation)
{
	const FTDSectorTableView Table = FTDCombatTactics::MakeInnerTable(Record);
	FTDCombatTactics::ClearOccupants(Table);
	const uint32 RecordId = Record.RecordId;
	auto IsInnerHolder = [this, RecordId](const int32 SlotIndex)
	{
		const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
		return Fragment.Role == ETDTacticsRole::Melee && Fragment.TokenState != ETDTacticsTokenState::None && Fragment.TokenRecordId == RecordId;
	};
	for (const int32 SlotIndex : Record.MemberSlots)
	{
		if (!IsInnerHolder(SlotIndex))
		{
			Tactics[SlotIndex].InnerSector = -1;
		}
	}
	const TArray<FTDTacticsCandidateKey> Holders = GatherOrderedTacticsMembers(Record, TargetLocation, IsInnerHolder);
	for (const FTDTacticsCandidateKey& Holder : Holders)
	{
		FTDMonsterTacticsFragment& Fragment = Tactics[Holder.SlotIndex];
		const FVector& Location = Bodies[Holder.SlotIndex].Location;
		FTDSectorSearch Search;
		Search.StepIndex = StepIndex;
		Search.BearingDecidegrees = FTDCombatTactics::ToDecidegrees(Location.X - TargetLocation.X, Location.Y - TargetLocation.Y);
		Search.Width = FTDCombatTactics::ComputeSectorWidth(FMath::Max(1, Fragment.MeleeReachCm), FTDCombatTactics::InnerSectorCount, FMath::FloorToInt32(Bodies[Holder.SlotIndex].Radius));
		const bool bCanKeep = FTDCombatTactics::CanKeepSector(Table, Fragment.InnerSector, Search);
		const int32 ChosenSector = bCanKeep ? Fragment.InnerSector : FTDCombatTactics::FindBestSector(Table, Search);
		if (ChosenSector >= 0)
		{
			FTDCombatTactics::ClaimSpan(Table, ChosenSector, Search.Width, Bodies[Holder.SlotIndex].SimulationId);
		}
		Fragment.InnerSector = static_cast<int8>(ChosenSector);
	}
}

void UTDMonsterThinkSubsystem::RunTokenPass(const bool bIsPeriodic)
{
	for (FTDTargetTacticsRecord& Record : TacticsRecords)
	{
		const AActor* Target = Record.Target.Get();
		if (!Target)
		{
			continue;
		}
		const FVector TargetLocation = Target->GetActorLocation();
		UpdateRecordBudgets(Record, bIsPeriodic);
		for (int32 PoolIndex = 0; PoolIndex < 2; ++PoolIndex)
		{
			GrantPoolTokens(Record, PoolIndex, TargetLocation);
		}
		AssignInnerSectors(Record, TargetLocation);
	}
}

void UTDMonsterThinkSubsystem::GrantPoolTokens(FTDTargetTacticsRecord& Record, const int32 PoolIndex, const FVector& TargetLocation)
{
	FTDTacticsPool& Pool = Record.Pools[PoolIndex];
	TArray<FTDTacticsCandidateKey> Holders;
	int32 HeldQuarters = 0;
	for (const int32 SlotIndex : OrderedSlots)
	{
		const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
		const bool bIsPoolHolder = Fragment.Role != ETDTacticsRole::Boss && GetTacticsPoolIndex(Fragment.Role) == PoolIndex && Fragment.TokenState != ETDTacticsTokenState::None && Fragment.TokenRecordId == Record.RecordId;
		if (!bIsPoolHolder)
		{
			continue;
		}
		HeldQuarters += FTDCombatTactics::GetHeldCostQuarters(Fragment);
		if (Fragment.TokenState == ETDTacticsTokenState::Reserved)
		{
			Holders.Add(MakeTacticsCandidateKey(SlotIndex, TargetLocation));
		}
	}
	Algo::Sort(Holders, &FTDCombatTactics::IsCandidateBefore);
	while (HeldQuarters > Pool.BudgetQuarters && !Holders.IsEmpty())
	{
		FTDMonsterTacticsFragment& ReleasedFragment = Tactics[Holders.Pop(EAllowShrinking::No).SlotIndex];
		HeldQuarters -= FTDCombatTactics::GetHeldCostQuarters(ReleasedFragment);
		ClearTacticsToken(ReleasedFragment);
	}
	TArray<FTDTacticsCandidateKey> Candidates;
	for (const int32 SlotIndex : Record.MemberSlots)
	{
		FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
		if (Fragment.Role == ETDTacticsRole::Boss || GetTacticsPoolIndex(Fragment.Role) != PoolIndex)
		{
			continue;
		}
		Fragment.bIsOnDeck = false;
		if (IsTokenCandidate(SlotIndex, Record))
		{
			Candidates.Add(MakeTacticsCandidateKey(SlotIndex, TargetLocation));
		}
	}
	Algo::Sort(Candidates, &FTDCombatTactics::IsCandidateBefore);
	int32 NextCandidate = 0;
	while (NextCandidate < Candidates.Num())
	{
		const int32 CandidateSlot = Candidates[NextCandidate].SlotIndex;
		const int32 CostQuarters = Tactics[CandidateSlot].ReserveCostQuarters;
		if (Pool.BudgetQuarters - HeldQuarters < CostQuarters)
		{
			break;
		}
		GrantTacticsToken(CandidateSlot, Record.RecordId);
		HeldQuarters += CostQuarters;
		++NextCandidate;
	}
	Pool.HeldQuarters = HeldQuarters;
	if (NextCandidate >= Candidates.Num())
	{
		return;
	}
	const TConstArrayView<FTDTacticsCandidateKey> RemainingCandidates = MakeArrayView(Candidates).RightChop(NextCandidate);
	InterceptForRetaliation(Record, PoolIndex, TargetLocation, RemainingCandidates);
	for (const FTDTacticsCandidateKey& Candidate : RemainingCandidates)
	{
		if (Tactics[Candidate.SlotIndex].TokenState == ETDTacticsTokenState::None)
		{
			Tactics[Candidate.SlotIndex].bIsOnDeck = true;
			return;
		}
	}
}

void UTDMonsterThinkSubsystem::InterceptForRetaliation(FTDTargetTacticsRecord& Record, const int32 PoolIndex, const FVector& TargetLocation, const TConstArrayView<FTDTacticsCandidateKey> RemainingCandidates)
{
	FTDTacticsPool& Pool = Record.Pools[PoolIndex];
	for (const FTDTacticsCandidateKey& Candidate : RemainingCandidates)
	{
		if (!Candidate.bIsRetaliating)
		{
			return;
		}
		const int32 VictimSlot = FindRetaliationVictim(Record, PoolIndex, TargetLocation, Candidate.DistanceSquared);
		if (VictimSlot == INDEX_NONE)
		{
			continue;
		}
		const int32 VictimQuarters = FTDCombatTactics::GetHeldCostQuarters(Tactics[VictimSlot]);
		const int32 CandidateQuarters = Tactics[Candidate.SlotIndex].ReserveCostQuarters;
		if (Pool.BudgetQuarters - Pool.HeldQuarters + VictimQuarters < CandidateQuarters)
		{
			continue;
		}
		ClearTacticsToken(Tactics[VictimSlot]);
		GrantTacticsToken(Candidate.SlotIndex, Record.RecordId);
		Pool.HeldQuarters += CandidateQuarters - VictimQuarters;
	}
}

int32 UTDMonsterThinkSubsystem::FindRetaliationVictim(const FTDTargetTacticsRecord& Record, const int32 PoolIndex, const FVector& TargetLocation, const int64 CandidateDistanceSquared) const
{
	int32 VictimSlot = INDEX_NONE;
	int64 VictimDistanceSquared = CandidateDistanceSquared * 4 - 1;
	for (const int32 SlotIndex : OrderedSlots)
	{
		const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
		const bool bIsReservedHolder = Fragment.Role != ETDTacticsRole::Boss && GetTacticsPoolIndex(Fragment.Role) == PoolIndex && Fragment.TokenState == ETDTacticsTokenState::Reserved && Fragment.TokenRecordId == Record.RecordId && Fragment.GrantStep != StepIndex;
		if (!bIsReservedHolder)
		{
			continue;
		}
		const int64 DistanceSquared = FTDCombatTactics::ComputePlanarDistanceSquared(Bodies[SlotIndex].Location, TargetLocation);
		if (DistanceSquared > VictimDistanceSquared)
		{
			VictimSlot = SlotIndex;
			VictimDistanceSquared = DistanceSquared;
		}
	}
	return VictimSlot;
}

void UTDMonsterThinkSubsystem::GrantTacticsToken(const int32 SlotIndex, const uint32 RecordId)
{
	FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	Fragment.TokenState = ETDTacticsTokenState::Reserved;
	Fragment.TokenRecordId = RecordId;
	Fragment.GrantStep = StepIndex;
	Fragment.bIsOnDeck = false;
}

void UTDMonsterThinkSubsystem::RemoveFinishedTacticsRecords()
{
	for (int32 RecordIndex = 0; RecordIndex < TacticsRecords.Num();)
	{
		const FTDTargetTacticsRecord& Record = TacticsRecords[RecordIndex];
		const AActor* Target = Record.Target.Get();
		const bool bHasLivingTarget = Target && UTDCombatLibrary::IsActorAlive(Target);
		const bool bHasEncounterEnded = Record.ZeroEngagedSinceStep != 0 && StepIndex - Record.ZeroEngagedSinceStep >= static_cast<uint64>(FTDCombatTactics::EncounterResetSteps);
		if (bHasLivingTarget && !bHasEncounterEnded)
		{
			++RecordIndex;
			continue;
		}
		const uint32 RecordId = Record.RecordId;
		for (const int32 SlotIndex : OrderedSlots)
		{
			FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
			if (Fragment.TargetRecordId == RecordId)
			{
				DetachTacticsSlot(SlotIndex);
			}
			if (Fragment.TokenRecordId == RecordId && Fragment.TokenState == ETDTacticsTokenState::Reserved)
			{
				ClearTacticsToken(Fragment);
			}
		}
		TacticsRecords.RemoveAt(RecordIndex);
	}
}

void UTDMonsterThinkSubsystem::ConfirmTacticsAttackStart(const int32 SlotIndex)
{
	FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	Fragment.LastCastStartCount = Brains[SlotIndex].CastStartCount;
	const int32 RecordIndex = FindTacticsRecordIndex(Brains[SlotIndex].Target.Get());
	int32 CastCostQuarters = TDMonsterAI::QuartersPerToken;
	if (Brains[SlotIndex].Definition.IsValid())
	{
		CastCostQuarters = FTDCombatTactics::GetAbilityCostQuarters(*Brains[SlotIndex].Definition, Brains[SlotIndex].LastCastAbilityIndex);
	}
	FTDCombatTactics::CommitToken(Fragment, CastCostQuarters);
	if (RecordIndex == INDEX_NONE)
	{
		return;
	}
	FTDTargetTacticsRecord& Record = TacticsRecords[RecordIndex];
	FTDCombatDirector::AddIntensity(Record.Director, FTDCombatDirector::AttackStartIntensity);
	if (Fragment.Role == ETDTacticsRole::Boss)
	{
		Record.LastBossStartStep = StepIndex;
		return;
	}
	Record.LastAttackStartStep = StepIndex;
}

void UTDMonsterThinkSubsystem::DetachTacticsSlot(const int32 SlotIndex)
{
	FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	if (Fragment.TokenState == ETDTacticsTokenState::Reserved)
	{
		ClearTacticsToken(Fragment);
	}
	Fragment.bIsOnDeck = false;
	const int32 RecordIndex = FindTacticsRecordIndexById(Fragment.TargetRecordId);
	Fragment.TargetRecordId = 0;
	ClearTacticsSectors(Fragment);
	if (RecordIndex == INDEX_NONE)
	{
		return;
	}
	FTDTargetTacticsRecord& Record = TacticsRecords[RecordIndex];
	const uint32 SimulationId = Bodies[SlotIndex].SimulationId;
	for (int32 Ring = 0; Ring < FTDTargetTacticsRecord::WaitRingCount; ++Ring)
	{
		FTDCombatTactics::ReleaseOccupant(FTDCombatTactics::MakeWaitTable(Record, Ring), SimulationId);
	}
	FTDCombatTactics::ReleaseOccupant(FTDCombatTactics::MakeInnerTable(Record), SimulationId);
	FTDCombatTactics::ReleaseOccupant(FTDCombatTactics::MakeRangedTable(Record), SimulationId);
}

bool UTDMonsterThinkSubsystem::IsTokenCandidate(const int32 SlotIndex, const FTDTargetTacticsRecord& Record) const
{
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	if (!Brain.IsEngaged() || !Brain.Definition.IsValid() || Fragment.Role == ETDTacticsRole::Boss || Fragment.TokenState != ETDTacticsTokenState::None || Fragment.TargetRecordId != Record.RecordId)
	{
		return false;
	}
	if (Brain.bIsFrozen || Brain.State == ETDMonsterFsmState::Stagger || Brain.ActivePrimitive == ETDMonsterPrimitive::MoveAway)
	{
		return false;
	}
	const AActor* Target = Record.Target.Get();
	if (!Target)
	{
		return false;
	}
	const int64 DistanceSquared = FTDCombatTactics::ComputePlanarDistanceSquared(Bodies[SlotIndex].Location, Target->GetActorLocation());
	if (DistanceSquared > SquareCm(GetGrantRadiusCm(Fragment)))
	{
		return false;
	}
	return HasReachableReadyAbility(Brain, DistanceSquared);
}

bool UTDMonsterThinkSubsystem::CanKeepTacticsToken(const int32 SlotIndex) const
{
	const FTDMonsterBrainFragment& Brain = Brains[SlotIndex];
	return Brain.IsEngaged() && !Brain.bIsFrozen && Brain.State != ETDMonsterFsmState::Stagger && Brain.ActivePrimitive != ETDMonsterPrimitive::MoveAway;
}

bool UTDMonsterThinkSubsystem::HasOtherTokenCandidate(const int32 SlotIndex, const FTDTargetTacticsRecord& Record) const
{
	const int32 PoolIndex = GetTacticsPoolIndex(Tactics[SlotIndex].Role);
	for (const int32 OtherIndex : Record.MemberSlots)
	{
		if (OtherIndex != SlotIndex && GetTacticsPoolIndex(Tactics[OtherIndex].Role) == PoolIndex && IsTokenCandidate(OtherIndex, Record))
		{
			return true;
		}
	}
	return false;
}

FTDTacticsCandidateKey UTDMonsterThinkSubsystem::MakeTacticsCandidateKey(const int32 SlotIndex, const FVector& TargetLocation) const
{
	const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	FTDTacticsCandidateKey Key;
	Key.bIsRetaliating = Fragment.LastDamagedStep != 0 && StepIndex - Fragment.LastDamagedStep <= static_cast<uint64>(FTDCombatTactics::RetaliationSteps);
	Key.bIsRegrantReady = StepIndex >= Fragment.RegrantReadyStep;
	Key.WaitStage = FTDCombatTactics::ComputeWaitStage(StepIndex, Fragment.LastAttackEndStep);
	Key.DistanceSquared = FTDCombatTactics::ComputePlanarDistanceSquared(Bodies[SlotIndex].Location, TargetLocation);
	Key.SimulationId = Bodies[SlotIndex].SimulationId;
	Key.SlotIndex = SlotIndex;
	return Key;
}

TArray<FTDTacticsCandidateKey> UTDMonsterThinkSubsystem::GatherOrderedTacticsMembers(const FTDTargetTacticsRecord& Record, const FVector& TargetLocation, const TFunctionRef<bool(int32)> Filter) const
{
	TArray<FTDTacticsCandidateKey> Members;
	for (const int32 SlotIndex : Record.MemberSlots)
	{
		if (Filter(SlotIndex))
		{
			Members.Add(MakeTacticsCandidateKey(SlotIndex, TargetLocation));
		}
	}
	Algo::Sort(Members, &IsOrderedBefore);
	return Members;
}

int32 UTDMonsterThinkSubsystem::CountHeldQuarters(const uint32 RecordId, const int32 PoolIndex) const
{
	int32 HeldQuarters = 0;
	for (const int32 SlotIndex : OrderedSlots)
	{
		const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
		if (Fragment.Role != ETDTacticsRole::Boss && GetTacticsPoolIndex(Fragment.Role) == PoolIndex && Fragment.TokenState != ETDTacticsTokenState::None && Fragment.TokenRecordId == RecordId)
		{
			HeldQuarters += FTDCombatTactics::GetHeldCostQuarters(Fragment);
		}
	}
	return HeldQuarters;
}

int32 UTDMonsterThinkSubsystem::FindTacticsRecordIndex(const AActor* Target) const
{
	if (!Target)
	{
		return INDEX_NONE;
	}
	for (int32 RecordIndex = 0; RecordIndex < TacticsRecords.Num(); ++RecordIndex)
	{
		if (TacticsRecords[RecordIndex].Target.Get() == Target)
		{
			return RecordIndex;
		}
	}
	return INDEX_NONE;
}

int32 UTDMonsterThinkSubsystem::FindTacticsRecordIndexById(const uint32 RecordId) const
{
	if (RecordId == 0)
	{
		return INDEX_NONE;
	}
	for (int32 RecordIndex = 0; RecordIndex < TacticsRecords.Num(); ++RecordIndex)
	{
		if (TacticsRecords[RecordIndex].RecordId == RecordId)
		{
			return RecordIndex;
		}
	}
	return INDEX_NONE;
}

bool UTDMonsterThinkSubsystem::HasTacticsAttackToken(const int32 SlotIndex) const
{
	const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	if (Fragment.Role == ETDTacticsRole::Boss)
	{
		return true;
	}
	const int32 RecordIndex = FindTacticsRecordIndex(Brains[SlotIndex].Target.Get());
	if (RecordIndex == INDEX_NONE || Fragment.TokenRecordId != TacticsRecords[RecordIndex].RecordId)
	{
		return false;
	}
	if (Fragment.TokenState == ETDTacticsTokenState::Committed)
	{
		return true;
	}
	if (Fragment.TokenState != ETDTacticsTokenState::Reserved)
	{
		return false;
	}
	const FTDTargetTacticsRecord& Record = TacticsRecords[RecordIndex];
	if (!IsStepElapsed(Record.LastAttackStartStep, FTDCombatDirector::GetStartSpacingSteps(Record.Director)))
	{
		return false;
	}
	if (!IsStepElapsed(Record.LastBossStartStep, FTDCombatTactics::BossSpacingSteps))
	{
		return false;
	}
	if (StepIndex < Record.OpeningGraceUntilStep && Record.GetEngagedCount() >= 2)
	{
		return false;
	}
	return StepIndex >= Record.GrantFreezeUntilStep;
}

bool UTDMonsterThinkSubsystem::IsStepElapsed(const uint64 SinceStep, const int32 Steps) const
{
	return SinceStep == 0 || StepIndex - SinceStep >= static_cast<uint64>(Steps);
}

bool UTDMonsterThinkSubsystem::IsAvoidingTacticsTarget(const int32 SlotIndex) const
{
	if (!bIsTacticsActive)
	{
		return false;
	}
	const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	if (Fragment.Role == ETDTacticsRole::Boss || Fragment.TokenState != ETDTacticsTokenState::None || Brains[SlotIndex].State == ETDMonsterFsmState::Cast)
	{
		return false;
	}
	const FTDTargetTacticsRecord* Record = nullptr;
	const ETDTacticsSlotKind Kind = ResolveTacticsSlotKind(SlotIndex, Record);
	return Kind != ETDTacticsSlotKind::None || (Record && Fragment.Role == ETDTacticsRole::Melee && Record->HasMeleeQueue());
}

ETDTacticsSlotKind UTDMonsterThinkSubsystem::ResolveTacticsSlotKind(const int32 SlotIndex, const FTDTargetTacticsRecord*& OutRecord) const
{
	OutRecord = nullptr;
	if (!bIsTacticsActive)
	{
		return ETDTacticsSlotKind::None;
	}
	const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	const int32 RecordIndex = FindTacticsRecordIndexById(Fragment.TargetRecordId);
	if (RecordIndex == INDEX_NONE || TacticsRecords[RecordIndex].Target.Get() != Brains[SlotIndex].Target.Get())
	{
		return ETDTacticsSlotKind::None;
	}
	OutRecord = &TacticsRecords[RecordIndex];
	if (Fragment.Role == ETDTacticsRole::Melee && Fragment.TokenState != ETDTacticsTokenState::None && Fragment.InnerSector >= 0)
	{
		return ETDTacticsSlotKind::Inner;
	}
	if (Fragment.TokenState == ETDTacticsTokenState::None && Fragment.WaitRing >= 0 && Fragment.WaitSector >= 0)
	{
		return ETDTacticsSlotKind::Wait;
	}
	if (Fragment.RangedSector >= 0)
	{
		return ETDTacticsSlotKind::Ranged;
	}
	return ETDTacticsSlotKind::None;
}

void UTDMonsterThinkSubsystem::FillTacticsContext(const int32 SlotIndex, FTDMonsterStepContext& Context) const
{
	Context.bIsTacticsActive = bIsTacticsActive;
	if (!bIsTacticsActive || !Context.Target)
	{
		return;
	}
	const FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
	const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	const int32 BodyRadiusCm = FMath::FloorToInt32(Body.Radius);
	Context.bHoldsAttackToken = Fragment.TokenState != ETDTacticsTokenState::None;
	const FVector2D AwayFromTarget = FVector2D(Context.SelfLocation.X - Context.TargetLocation.X, Context.SelfLocation.Y - Context.TargetLocation.Y).GetSafeNormal();
	const FVector2D TargetVelocity(Context.TargetVelocity.X, Context.TargetVelocity.Y);
	Context.bIsTargetClosingIn = FVector2D::DotProduct(TargetVelocity, AwayFromTarget) > FTDCombatTactics::MovingTargetSpeed;
	const FTDTargetTacticsRecord* Record = nullptr;
	Context.TacticsSlotKind = ResolveTacticsSlotKind(SlotIndex, Record);
	switch (Context.TacticsSlotKind)
	{
	case ETDTacticsSlotKind::Inner:
	{
		const int32 Width = FTDCombatTactics::ComputeSectorWidth(FMath::Max(1, Fragment.MeleeReachCm), FTDCombatTactics::InnerSectorCount, BodyRadiusCm);
		Context.TacticsSlotDirection = FTDCombatTactics::MakeDirection(FTDCombatTactics::GetSectorCenter(FTDCombatTactics::InnerSectorCount, 0, Fragment.InnerSector));
		Context.TacticsSlotHalfAngleDegrees = FTDCombatTactics::ComputeSectorHalfAngleDegrees(FTDCombatTactics::InnerSectorCount, Width);
		return;
	}
	case ETDTacticsSlotKind::Wait:
	{
		const int32 SectorCount = FTDCombatTactics::WaitSectorCounts[Fragment.WaitRing];
		const int32 RingRadius = FTDCombatTactics::ComputeWaitRingRadius(Fragment.MeleeReachCm, BodyRadiusCm, Fragment.WaitRing);
		const int32 Center = FTDCombatTactics::GetSectorCenter(SectorCount, 0, Fragment.WaitSector) + FTDCombatTactics::ComputeDriftDecidegrees(Body.SimulationId, StepIndex);
		const int32 FeintDepth = FTDCombatTactics::IsFeinting(Body.SimulationId, StepIndex) ? FTDCombatTactics::FeintDepthCm : 0;
		Context.bIsFeinting = FeintDepth > 0;
		Context.TacticsSlotDirection = FTDCombatTactics::MakeDirection(Center);
		Context.TacticsSlotRadius = static_cast<float>(RingRadius - FeintDepth);
		Context.TacticsSlotHalfAngleDegrees = FTDCombatTactics::ComputeSectorHalfAngleDegrees(SectorCount, FTDCombatTactics::ComputeSectorWidth(RingRadius, SectorCount, BodyRadiusCm));
		return;
	}
	case ETDTacticsSlotKind::Ranged:
	{
		const int32 Center = FTDCombatTactics::GetSectorCenter(FTDCombatTactics::RangedSectorCount, FTDCombatTactics::RangedSectorOffsetDecidegrees, Fragment.RangedSector);
		const int32 Width = FTDCombatTactics::ComputeSectorWidth(FMath::Max(1, Fragment.MaxReachCm), FTDCombatTactics::RangedSectorCount, BodyRadiusCm);
		Context.TacticsSlotDirection = FTDCombatTactics::MakeDirection(Center);
		Context.TacticsSlotHalfAngleDegrees = FTDCombatTactics::ComputeSectorHalfAngleDegrees(FTDCombatTactics::RangedSectorCount, Width);
		return;
	}
	default:
		break;
	}
	if (Fragment.Role != ETDTacticsRole::Melee || Context.bHoldsAttackToken || !Record || !Record->HasMeleeQueue())
	{
		return;
	}
	Context.TacticsSlotRadius = static_cast<float>(FTDCombatTactics::ComputeWaitRingRadius(Fragment.MeleeReachCm, BodyRadiusCm, 0));
}

FString UTDMonsterThinkSubsystem::MakeTacticsDebugSuffix(const int32 SlotIndex) const
{
	const FTDMonsterTacticsFragment& Fragment = Tactics[SlotIndex];
	const FTDTargetTacticsRecord* Record = nullptr;
	FString SlotLabel = TEXT("-");
	switch (ResolveTacticsSlotKind(SlotIndex, Record))
	{
	case ETDTacticsSlotKind::Wait:
		SlotLabel = FString::Printf(TEXT("W%d:%d"), Fragment.WaitRing, Fragment.WaitSector);
		break;
	case ETDTacticsSlotKind::Inner:
		SlotLabel = FString::Printf(TEXT("I%d"), Fragment.InnerSector);
		break;
	case ETDTacticsSlotKind::Ranged:
		SlotLabel = FString::Printf(TEXT("R%d"), Fragment.RangedSector);
		break;
	default:
		break;
	}
	return FString::Printf(TEXT(" token=%s slot=%s ondeck=%d"), GetTacticsTokenLabel(bIsTacticsActive, Fragment), *SlotLabel, Fragment.bIsOnDeck ? 1 : 0);
}

void UTDMonsterThinkSubsystem::DrawTacticsDebugState(const int32 SlotIndex) const
{
	UWorld* World = GetWorld();
	const FTDMonsterStepContext Context = MakeContext(SlotIndex);
	if (!World || !Context.Target)
	{
		return;
	}
	const FVector& SelfLocation = Bodies[SlotIndex].Location;
	const FVector RingCenter(Context.TargetLocation.X, Context.TargetLocation.Y, SelfLocation.Z);
	const FColor SlotColor = GetTacticsSlotColor(Context.TacticsSlotKind);
	if (Context.TacticsSlotRadius > 0.f)
	{
		DrawDebugCircle(World, RingCenter, Context.TacticsSlotRadius, 48, SlotColor, false, -1.f, 0, 0.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
	}
	if (Context.TacticsSlotKind != ETDTacticsSlotKind::None)
	{
		const double PointRadius = Context.TacticsSlotRadius > 0.f ? static_cast<double>(Context.TacticsSlotRadius) : FVector::Dist2D(SelfLocation, RingCenter);
		const FVector SlotPoint = RingCenter + FVector(Context.TacticsSlotDirection * PointRadius, 0.0);
		DrawDebugLine(World, RingCenter, SlotPoint, SlotColor, false, -1.f, 0, 2.f);
	}
	DrawDebugString(World, SelfLocation + FVector(0.f, 0.f, 170.f), MakeTacticsDebugSuffix(SlotIndex), nullptr, SlotColor, 0.f, true, 1.f);
}
