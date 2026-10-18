#include "FluidFX/TDFluidLiveSubsystem.h"
#include "Core/TDGameplayMessages.h"
#include "Camera/CameraComponent.h"
#include "Core/TDGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Async/ParallelFor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TDGame.h"

namespace
{
	constexpr double TickCostLogIntervalSeconds = 2.0;

	TAutoConsoleVariable<int32> CVarLogTickCost(
		TEXT("TD.FluidFX.LogTickCost"),
		0,
		TEXT("Log the FluidFX subsystem tick cost every 2 seconds of world time. 0 = Off, 1 = On."));

	TAutoConsoleVariable<int32> CVarParallelActorSteps(
		TEXT("TD.FluidFX.ParallelActorSteps"),
		1,
		TEXT("Run the solver steps of awake FluidFX actors as parallel worker tasks. 0 = One by one on the game thread, 1 = Parallel."));

	struct FTDFluidActorStep
	{
		TWeakObjectPtr<ATDFluidLiveActor> LiveActor;
		bool bIsSolving = false;
	};

	ATDFluidLiveActor* GetStillActiveActor(const FTDFluidActorStep& ActorStep)
	{
		ATDFluidLiveActor* LiveActor = ActorStep.LiveActor.Get();
		return LiveActor && !LiveActor->IsPooledInactive() ? LiveActor : nullptr;
	}

	FVector ResolveHitDirection(const AActor* Instigator, const AActor* Target, const FVector& AuthoredDirection = FVector::ZeroVector)
	{
		const FVector AuthoredPlanarDirection = AuthoredDirection.ContainsNaN() ? FVector::ZeroVector : AuthoredDirection.GetSafeNormal2D();
		if (!AuthoredPlanarDirection.IsNearlyZero())
		{
			return AuthoredPlanarDirection;
		}

		FVector HitDirection = Instigator
			? (Target->GetActorLocation() - Instigator->GetActorLocation()).GetSafeNormal2D()
			: Target->GetActorForwardVector().GetSafeNormal2D();
		if (HitDirection.IsNearlyZero())
		{
			HitDirection = FVector(1.0f, 0.0f, 0.0f);
		}
		return HitDirection;
	}

	FVector ResolveDeathLocation(const AActor* DeadActor, const FVector& DeathLocation)
	{
		if (!DeathLocation.IsNearlyZero() || !DeadActor)
		{
			return DeathLocation;
		}
		return DeadActor->GetActorLocation();
	}

	FVector ResolveSplashDirection(const AActor* Killer, const FVector& DeathLocation)
	{
		FVector SplashDirection = Killer
			? (DeathLocation - Killer->GetActorLocation()).GetSafeNormal2D()
			: FVector(1.0f, 0.0f, 0.0f);
		if (SplashDirection.IsNearlyZero())
		{
			SplashDirection = FVector(1.0f, 0.0f, 0.0f);
		}
		return SplashDirection;
	}

	bool IsUsableDamageAmount(float DamageAmount)
	{
		return FMath::IsFinite(DamageAmount) && DamageAmount > 0.0f;
	}
}

UTDFluidLiveSubsystem* UTDFluidLiveSubsystem::GetFluidLiveSubsystem(const UObject* WorldContextObject)
{
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}

	return World->GetSubsystem<UTDFluidLiveSubsystem>();
}

void UTDFluidLiveSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (UGameplayMessageSubsystem::HasInstance(&InWorld))
	{
		UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(&InWorld);
		DamageAppliedListenerHandle = MessageSubsystem.RegisterListener<FTDDamageAppliedMessage>(
			TDGameplayTags::Event_Damage_Applied,
			this,
			&ThisClass::HandleDamageAppliedMessage);
		ActorDeathListenerHandle = MessageSubsystem.RegisterListener<FTDActorDeathMessage>(
			TDGameplayTags::Event_Actor_Death,
			this,
			&ThisClass::HandleActorDeathMessage);
	}
}

void UTDFluidLiveSubsystem::Deinitialize()
{
	DamageAppliedListenerHandle.Unregister();
	ActorDeathListenerHandle.Unregister();
	QueuedDamageByTarget.Reset();
	QueuedDeathByActor.Reset();
	RegisteredActors.Reset();
	Super::Deinitialize();
}

TStatId UTDFluidLiveSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDFluidLiveSubsystem, STATGROUP_Tickables);
}

bool UTDFluidLiveSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::EditorPreview;
}

void UTDFluidLiveSubsystem::RegisterLiveActor(ATDFluidLiveActor* LiveActor)
{
	if (!LiveActor)
	{
		return;
	}

	for (const TWeakObjectPtr<ATDFluidLiveActor>& Entry : RegisteredActors)
	{
		if (Entry.Get() == LiveActor)
		{
			return;
		}
	}

	RegisteredActors.Add(LiveActor);
}

void UTDFluidLiveSubsystem::UnregisterLiveActor(ATDFluidLiveActor* LiveActor)
{
	if (!LiveActor)
	{
		return;
	}

	RegisteredActors.RemoveAllSwap([LiveActor](const TWeakObjectPtr<ATDFluidLiveActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == LiveActor;
	});
}

void UTDFluidLiveSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	RefreshPlaneViewRotation();

	if (CVarLogTickCost.GetValueOnGameThread() == 0)
	{
		if (TickCostFrameCount > 0)
		{
			ResetTickCostWindow();
		}
		StepFluidWorld(DeltaTime);
		return;
	}

	const uint64 StartCycles = FPlatformTime::Cycles64();
	StepFluidWorld(DeltaTime);
	RecordTickCost(DeltaTime, FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycles));
}

FRotator UTDFluidLiveSubsystem::GetPlaneViewRotation() const
{
	return PlaneViewRotationOverride.IsSet() ? PlaneViewRotationOverride.GetValue() : CachedPlaneViewRotation;
}

void UTDFluidLiveSubsystem::SetPlaneViewRotationOverrideForTests(const FRotator& ViewRotation)
{
	PlaneViewRotationOverride = ViewRotation;
}

void UTDFluidLiveSubsystem::ClearPlaneViewRotationOverrideForTests()
{
	PlaneViewRotationOverride.Reset();
}

void UTDFluidLiveSubsystem::RefreshPlaneViewRotation()
{
	CachedPlaneViewRotation = FTDFluidPlaneBasis::GetDefaultViewRotation();

	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World && GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	const AActor* ViewTarget = PlayerController ? PlayerController->GetViewTarget() : nullptr;
	const UCameraComponent* ViewCamera = ViewTarget ? ViewTarget->FindComponentByClass<UCameraComponent>() : nullptr;
	if (!ViewCamera)
	{
		return;
	}

	CachedPlaneViewRotation = ViewCamera->GetComponentRotation();
}

void UTDFluidLiveSubsystem::RecordTickCost(float DeltaTime, double TickSeconds)
{
	++TickCostFrameCount;
	TickCostTotalSeconds += TickSeconds;
	TickCostMaxSeconds = FMath::Max(TickCostMaxSeconds, TickSeconds);
	TickCostTotalDeltaSeconds += DeltaTime;

	if (TickCostTotalDeltaSeconds < TickCostLogIntervalSeconds)
	{
		return;
	}

	LogTickCostWindow();
	ResetTickCostWindow();
}

void UTDFluidLiveSubsystem::LogTickCostWindow() const
{
	int32 AwakeActorCount = 0;
	int32 TotalCellCount = 0;
	for (const TWeakObjectPtr<ATDFluidLiveActor>& Entry : RegisteredActors)
	{
		const ATDFluidLiveActor* LiveActor = Entry.Get();
		if (!LiveActor || LiveActor->IsPooledInactive() || !LiveActor->LiveComponent || !LiveActor->LiveComponent->IsSimulationAwake())
		{
			continue;
		}

		const int32 Resolution = LiveActor->LiveComponent->GetSolver().GetResolution();
		++AwakeActorCount;
		TotalCellCount += Resolution * Resolution;
	}

	const double FrameCount = static_cast<double>(TickCostFrameCount);
	UE_LOG(LogTDGame, Display, TEXT("TDFluidPerf: frames=%d avg_tick_ms=%.4f max_tick_ms=%.4f avg_frame_ms=%.3f awake_actors=%d total_cells=%d"),
		TickCostFrameCount,
		TickCostTotalSeconds * 1000.0 / FrameCount,
		TickCostMaxSeconds * 1000.0,
		TickCostTotalDeltaSeconds * 1000.0 / FrameCount,
		AwakeActorCount,
		TotalCellCount);
}

void UTDFluidLiveSubsystem::ResetTickCostWindow()
{
	TickCostFrameCount = 0;
	TickCostTotalSeconds = 0.0;
	TickCostMaxSeconds = 0.0;
	TickCostTotalDeltaSeconds = 0.0;
}

void UTDFluidLiveSubsystem::StepFluidWorld(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_StepFluidWorld);

	FlushQueuedCombatEvents();

	if (DeltaTime <= 0.0f)
	{
		return;
	}

	TArray<FTDFluidActorStep, TInlineAllocator<16>> ActorSteps;
	for (int32 Idx = RegisteredActors.Num() - 1; Idx >= 0; --Idx)
	{
		ATDFluidLiveActor* LiveActor = RegisteredActors[Idx].Get();
		if (!LiveActor)
		{
			RegisteredActors.RemoveAtSwap(Idx);
			continue;
		}

		if (LiveActor->IsPooledInactive())
		{
			continue;
		}

		FTDFluidActorStep& ActorStep = ActorSteps.AddDefaulted_GetRef();
		ActorStep.LiveActor = LiveActor;
		ActorStep.bIsSolving = LiveActor->BeginLiveActorStep(DeltaTime);
	}

	TArray<ATDFluidLiveActor*, TInlineAllocator<16>> SolvingActors;
	for (const FTDFluidActorStep& ActorStep : ActorSteps)
	{
		ATDFluidLiveActor* LiveActor = GetStillActiveActor(ActorStep);
		if (ActorStep.bIsSolving && LiveActor)
		{
			SolvingActors.Add(LiveActor);
		}
	}

	RunSolverSteps(SolvingActors, DeltaTime);

	for (const FTDFluidActorStep& ActorStep : ActorSteps)
	{
		ATDFluidLiveActor* LiveActor = GetStillActiveActor(ActorStep);
		if (!LiveActor)
		{
			continue;
		}

		if (ActorStep.bIsSolving)
		{
			LiveActor->FinishLiveActorStep();
		}

		if (LiveActor->HasFiniteLifetimeExpired())
		{
			ReleaseFluidActorToPool(LiveActor);
		}
	}
}

void UTDFluidLiveSubsystem::RunSolverSteps(TConstArrayView<ATDFluidLiveActor*> SolvingActors, float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_RunSolverSteps);

	const EParallelForFlags Flags = CVarParallelActorSteps.GetValueOnGameThread() != 0 ? EParallelForFlags::Unbalanced : EParallelForFlags::ForceSingleThread;
	ParallelFor(TEXT("TDFluidSolverSteps"), SolvingActors.Num(), 1, [SolvingActors, DeltaTime](int32 ActorIndex)
	{
		SolvingActors[ActorIndex]->RunLiveActorSolverStep(DeltaTime);
	}, Flags);
}

ATDFluidLiveActor* UTDFluidLiveSubsystem::SpawnPooledFluidActor(
	UTDFluidPresetAsset* PresetAsset,
	const FVector& WorldLocation,
	float ActiveDurationSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	ATDFluidLiveActor* TargetActor = AcquireInactivePooledActor();
	if (TargetActor)
	{
		TargetActor->SetActorLocation(WorldLocation);
		TargetActor->SetPooledInactive(false);
	}
	else
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TargetActor = World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), WorldLocation, FRotator::ZeroRotator, SpawnParams);
		if (TargetActor)
		{
			RegisterLiveActor(TargetActor);
		}
	}

	if (TargetActor)
	{
		TargetActor->InitializeFromPreset(PresetAsset, ActiveDurationSeconds);
	}

	return TargetActor;
}

void UTDFluidLiveSubsystem::ReleaseFluidActorToPool(ATDFluidLiveActor* LiveActor)
{
	if (!LiveActor)
	{
		return;
	}

	LiveActor->SetPooledInactive(true);
}

int32 UTDFluidLiveSubsystem::InjectCombatBrushStamp(const FTDFluidBrushStamp& Stamp)
{
	int32 ReceiverCount = 0;

	for (int32 Idx = RegisteredActors.Num() - 1; Idx >= 0; --Idx)
	{
		ATDFluidLiveActor* LiveActor = RegisteredActors[Idx].Get();
		if (!LiveActor)
		{
			RegisteredActors.RemoveAtSwap(Idx);
			continue;
		}

		if (LiveActor->IsPooledInactive() || !LiveActor->LiveComponent || !LiveActor->LiveComponent->IsSimulationAwake())
		{
			continue;
		}

		FVector2D PlanePosition;
		if (!LiveActor->LiveComponent->TryProjectWorldPointIntoSlab(Stamp.WorldLocation, PlanePosition))
		{
			continue;
		}

		LiveActor->LiveComponent->QueueBrushStamp(Stamp);
		++ReceiverCount;
	}

	return ReceiverCount;
}

int32 UTDFluidLiveSubsystem::InjectCombatParticleSplash(
	const TArray<FVector>& ParticlePositions,
	const TArray<FVector>& ParticleVelocities,
	float BrushRadiusCm,
	ETDFluidBrushChannel Channel)
{
	if (ParticlePositions.IsEmpty() || !FMath::IsFinite(BrushRadiusCm))
	{
		return 0;
	}

	int32 ReceiverCount = 0;
	TArray<FVector> ReceivedPositions;
	TArray<FVector> ReceivedVelocities;
	ReceivedPositions.Reserve(ParticlePositions.Num());
	ReceivedVelocities.Reserve(ParticlePositions.Num());

	for (const TWeakObjectPtr<ATDFluidLiveActor>& Entry : RegisteredActors)
	{
		ATDFluidLiveActor* LiveActor = Entry.Get();
		if (!LiveActor || LiveActor->IsPooledInactive() || !LiveActor->LiveComponent || !LiveActor->LiveComponent->IsSimulationAwake())
		{
			continue;
		}

		ReceivedPositions.Reset();
		ReceivedVelocities.Reset();

		for (int32 ParticleIndex = 0; ParticleIndex < ParticlePositions.Num(); ++ParticleIndex)
		{
			const FVector& Position = ParticlePositions[ParticleIndex];
			const FVector Velocity = ParticleVelocities.IsValidIndex(ParticleIndex) ? ParticleVelocities[ParticleIndex] : FVector::ZeroVector;
			if (Position.ContainsNaN() || Velocity.ContainsNaN())
			{
				continue;
			}

			FVector2D PlanePosition;
			if (!LiveActor->LiveComponent->TryProjectWorldPointIntoSlab(Position, PlanePosition))
			{
				continue;
			}

			ReceivedPositions.Add(Position);
			ReceivedVelocities.Add(Velocity);
		}

		if (ReceivedPositions.IsEmpty())
		{
			continue;
		}

		LiveActor->LiveComponent->QueueNiagaraParticleStamps(ReceivedPositions, ReceivedVelocities, BrushRadiusCm, Channel);
		++ReceiverCount;
	}

	return ReceiverCount;
}

void UTDFluidLiveSubsystem::QueueCombatDamage(
	AActor* Instigator,
	AActor* Target,
	float DamageAmount,
	ETDDamageElement Element,
	bool bHasKnownElement,
	bool bWasCritical,
	ETDDamageDelivery Delivery,
	bool bHasHitLocation,
	const FVector& HitLocation,
	const FVector& HitDirection)
{
	if (!Target || Delivery == ETDDamageDelivery::Status || !IsUsableDamageAmount(DamageAmount) || RegisteredActors.IsEmpty())
	{
		return;
	}

	FQueuedCombatDamage Incoming;
	Incoming.TargetLocation = Target->GetActorLocation();
	Incoming.HitLocation = HitLocation;
	Incoming.HitDirection = ResolveHitDirection(Instigator, Target, HitDirection);
	Incoming.DamageAmount = DamageAmount;
	Incoming.Element = Element;
	Incoming.bHasKnownElement = bHasKnownElement;
	Incoming.bHasHitLocation = bHasHitLocation && !HitLocation.ContainsNaN();
	Incoming.bWasCritical = bWasCritical;

	const FObjectKey TargetKey(Target);
	FQueuedCombatDamage* Existing = QueuedDamageByTarget.Find(TargetKey);
	if (!Existing)
	{
		QueuedDamageByTarget.Add(TargetKey, Incoming);
		return;
	}

	const bool bIncomingIsStronger = Incoming.DamageAmount > Existing->DamageAmount;
	if (Incoming.bHasKnownElement && (!Existing->bHasKnownElement || bIncomingIsStronger))
	{
		Existing->Element = Incoming.Element;
		Existing->bHasKnownElement = true;
	}
	if (Incoming.bHasHitLocation && (bIncomingIsStronger || !Existing->bHasHitLocation))
	{
		Existing->HitLocation = Incoming.HitLocation;
		Existing->bHasHitLocation = true;
	}
	if (bIncomingIsStronger)
	{
		Existing->DamageAmount = Incoming.DamageAmount;
		Existing->TargetLocation = Incoming.TargetLocation;
		Existing->HitDirection = Incoming.HitDirection;
	}
	Existing->bWasCritical = Existing->bWasCritical || Incoming.bWasCritical;
}

void UTDFluidLiveSubsystem::QueueCombatDeath(AActor* DeadActor, AActor* Killer, const FVector& DeathLocation)
{
	if (!DeadActor || DeathLocation.ContainsNaN() || RegisteredActors.IsEmpty())
	{
		return;
	}

	const FObjectKey DeadActorKey(DeadActor);
	const FQueuedCombatDeath* Existing = QueuedDeathByActor.Find(DeadActorKey);
	if (Existing && (Existing->bHasKiller || !Killer))
	{
		return;
	}

	FQueuedCombatDeath Incoming;
	Incoming.DeathLocation = ResolveDeathLocation(DeadActor, DeathLocation);
	Incoming.SplashDirection = ResolveSplashDirection(Killer, Incoming.DeathLocation);
	Incoming.bHasKiller = Killer != nullptr;
	QueuedDeathByActor.Add(DeadActorKey, Incoming);
}

void UTDFluidLiveSubsystem::FlushQueuedCombatEvents()
{
	if (QueuedDamageByTarget.IsEmpty() && QueuedDeathByActor.IsEmpty())
	{
		return;
	}

	TMap<FObjectKey, FQueuedCombatDamage> DamageToStamp = MoveTemp(QueuedDamageByTarget);
	TMap<FObjectKey, FQueuedCombatDeath> DeathToStamp = MoveTemp(QueuedDeathByActor);
	QueuedDamageByTarget.Reset();
	QueuedDeathByActor.Reset();

	for (const TPair<FObjectKey, FQueuedCombatDamage>& Entry : DamageToStamp)
	{
		const FQueuedCombatDamage& Damage = Entry.Value;
		const FVector& StampLocation = Damage.bHasHitLocation ? Damage.HitLocation : Damage.TargetLocation;
		InjectDamageStamp(StampLocation, Damage.HitDirection, Damage.DamageAmount, Damage.Element, Damage.bWasCritical);
	}

	for (const TPair<FObjectKey, FQueuedCombatDeath>& Entry : DeathToStamp)
	{
		InjectDeathStamp(Entry.Value.DeathLocation, Entry.Value.SplashDirection);
	}
}

int32 UTDFluidLiveSubsystem::NotifyCombatDamageApplied(
	AActor* Instigator,
	AActor* Target,
	float DamageAmount,
	ETDDamageElement Element,
	bool bWasCritical)
{
	if (!Target || !IsUsableDamageAmount(DamageAmount))
	{
		return 0;
	}

	return InjectDamageStamp(Target->GetActorLocation(), ResolveHitDirection(Instigator, Target), DamageAmount, Element, bWasCritical);
}

int32 UTDFluidLiveSubsystem::NotifyCombatActorDeath(
	AActor* DeadActor,
	AActor* Killer,
	const FVector& DeathLocation)
{
	const FVector ResolvedLocation = ResolveDeathLocation(DeadActor, DeathLocation);
	return InjectDeathStamp(ResolvedLocation, ResolveSplashDirection(Killer, ResolvedLocation));
}

int32 UTDFluidLiveSubsystem::InjectDamageStamp(
	const FVector& StampLocation,
	const FVector& HitDirection,
	float DamageAmount,
	ETDDamageElement Element,
	bool bWasCritical)
{
	const float CritMultiplier = bWasCritical ? 1.4f : 1.0f;
	const float SpeedCmPerSec = FMath::Clamp(DamageAmount * 14.0f * CritMultiplier, 300.0f, 1800.0f);

	FTDFluidBrushStamp CombatStamp;
	CombatStamp.WorldLocation = StampLocation;
	CombatStamp.WorldVelocity = HitDirection * SpeedCmPerSec;
	CombatStamp.RadiusCm = FMath::Clamp(100.0f + DamageAmount * 0.85f, 100.0f, 260.0f) * (bWasCritical ? 1.25f : 1.0f);
	CombatStamp.DensityStrength = FMath::Clamp(0.75f + DamageAmount * 0.01f, 0.75f, 2.0f) * CritMultiplier;
	CombatStamp.VelocityStrength = 1.0f * CritMultiplier;
	CombatStamp.Channel = ResolveChannelForDamageElement(Element);

	return InjectCombatBrushStamp(CombatStamp);
}

int32 UTDFluidLiveSubsystem::InjectDeathStamp(const FVector& DeathLocation, const FVector& SplashDirection)
{
	FTDFluidBrushStamp DeathStamp;
	DeathStamp.WorldLocation = DeathLocation;
	DeathStamp.WorldVelocity = SplashDirection * 950.0f;
	DeathStamp.RadiusCm = 220.0f;
	DeathStamp.DensityStrength = 1.85f;
	DeathStamp.VelocityStrength = 1.35f;
	DeathStamp.Channel = ETDFluidBrushChannel::ChannelG_Blood;

	return InjectCombatBrushStamp(DeathStamp);
}

bool UTDFluidLiveSubsystem::BindLandscapeMaterialToLiveActor(ATDFluidLiveActor* SourceActor, UMaterialInstanceDynamic* LandscapeMID)
{
	if (!SourceActor || !SourceActor->LiveComponent || !LandscapeMID)
	{
		return false;
	}

	SourceActor->LiveComponent->RegisterExternalMaterialTarget(LandscapeMID);
	return true;
}

int32 UTDFluidLiveSubsystem::BindLandscapeMaterialViaTag(FName LiveActorTag, UMaterialInstanceDynamic* LandscapeMID)
{
	if (LiveActorTag.IsNone() || !LandscapeMID)
	{
		return 0;
	}

	int32 BoundCount = 0;
	for (const TWeakObjectPtr<ATDFluidLiveActor>& Entry : RegisteredActors)
	{
		ATDFluidLiveActor* LiveActor = Entry.Get();
		if (!LiveActor || !LiveActor->ActorHasTag(LiveActorTag) || !LiveActor->LiveComponent)
		{
			continue;
		}

		LiveActor->LiveComponent->RegisterExternalMaterialTarget(LandscapeMID);
		++BoundCount;
	}

	return BoundCount;
}

int32 UTDFluidLiveSubsystem::GetActiveActorCount() const
{
	int32 ActiveCount = 0;
	for (const TWeakObjectPtr<ATDFluidLiveActor>& Entry : RegisteredActors)
	{
		const ATDFluidLiveActor* LiveActor = Entry.Get();
		if (LiveActor && !LiveActor->IsPooledInactive())
		{
			++ActiveCount;
		}
	}
	return ActiveCount;
}

int32 UTDFluidLiveSubsystem::GetPooledInactiveActorCount() const
{
	int32 InactiveCount = 0;
	for (const TWeakObjectPtr<ATDFluidLiveActor>& Entry : RegisteredActors)
	{
		const ATDFluidLiveActor* LiveActor = Entry.Get();
		if (LiveActor && LiveActor->IsPooledInactive())
		{
			++InactiveCount;
		}
	}
	return InactiveCount;
}

ATDFluidLiveActor* UTDFluidLiveSubsystem::AcquireInactivePooledActor()
{
	for (const TWeakObjectPtr<ATDFluidLiveActor>& Entry : RegisteredActors)
	{
		ATDFluidLiveActor* LiveActor = Entry.Get();
		if (LiveActor && LiveActor->IsPooledInactive())
		{
			return LiveActor;
		}
	}
	return nullptr;
}

void UTDFluidLiveSubsystem::HandleDamageAppliedMessage(FGameplayTag Channel, const FTDDamageAppliedMessage& Message)
{
	QueueCombatDamage(
		Message.Instigator,
		Message.Target,
		Message.DamageAmount,
		Message.Element,
		true,
		Message.bWasCritical,
		Message.Delivery,
		Message.bHasHitLocation,
		Message.HitLocation,
		Message.HitDirection);

	if (!Message.bWasKilled || !Message.Target)
	{
		return;
	}

	const bool bHasUsableHitLocation = Message.bHasHitLocation && !Message.HitLocation.ContainsNaN();
	const FVector DeathLocation = bHasUsableHitLocation ? Message.HitLocation : Message.Target->GetActorLocation();
	QueueCombatDeath(Message.Target, Message.Instigator, DeathLocation);
}

void UTDFluidLiveSubsystem::HandleActorDeathMessage(FGameplayTag Channel, const FTDActorDeathMessage& Message)
{
	QueueCombatDeath(Message.DeadActor, Message.Killer, Message.DeathLocation);
}

ETDFluidBrushChannel UTDFluidLiveSubsystem::ResolveChannelForDamageElement(ETDDamageElement Element)
{
	switch (Element)
	{
	case ETDDamageElement::Fire:
	case ETDDamageElement::Arcane:
		return ETDFluidBrushChannel::ChannelB_Fire;
	case ETDDamageElement::Frost:
		return ETDFluidBrushChannel::ChannelA_Water;
	case ETDDamageElement::Physical:
	default:
		return ETDFluidBrushChannel::ChannelG_Blood;
	}
}

