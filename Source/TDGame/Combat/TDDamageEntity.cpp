#include "Combat/TDDamageEntity.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDDamageDefinition.h"
#include "Combat/TDDamageSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"

ATDDamageEntity::ATDDamageEntity()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetVisibility(false);
	EffectComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Effect"));
	EffectComponent->SetupAttachment(RootComponent);
	EffectComponent->SetAutoActivate(false);
}

void ATDDamageEntity::Initialize(UTDDamageDefinition* InDefinition, const FTDDamageContext& InContext)
{
	if (HasActorBegunPlay())
	{
		return;
	}
	Definition = InDefinition;
	Context = InContext;
	Context.Direction = Context.Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	TravelDirection = Context.Direction;
	SetActorRotation(TravelDirection.Rotation());
}

UTDDamageDefinition* ATDDamageEntity::GetDefinition() const
{
	return Definition;
}

void ATDDamageEntity::BeginPlay()
{
	Super::BeginPlay();
	FString Error;
	if (!IsValid(Definition) || !Definition->ValidateDefinition(Error))
	{
		Destroy();
		return;
	}
	SimulationTime = GetWorld()->GetTimeSeconds();
	EventTime = SimulationTime;
	ActivationTime = SimulationTime + Definition->ActivationDelay;
	ExpirationTime = SimulationTime + Definition->Lifetime;
	RingInnerRadius = Definition->InnerRadius;
	RingOuterRadius = Definition->Radius;
	MeshComponent->SetStaticMesh(Definition->Mesh);
	if (Definition->Material)
	{
		MeshComponent->SetMaterial(0, Definition->Material);
	}
	MeshComponent->SetRelativeScale3D(Definition->VisualScale);
	EffectComponent->SetAsset(Definition->VisualEffect);
	EffectComponent->SetRelativeScale3D(Definition->VisualScale);
	{
		TGuardValue<bool> ProcessingGuard(bIsProcessingTimeline, true);
		EmitEvent(ETDDamageEvent::Spawn, nullptr, GetActorLocation());
	}
	SetActorTickEnabled(CanContinue());
	ProcessTimeline();
}

void ATDDamageEntity::Activate()
{
	if (!CanContinue() || bIsActive)
	{
		return;
	}
	bIsActive = true;
	NextPulseTime = SimulationTime;
	MeshComponent->SetVisibility(true);
	if (Definition->VisualEffect)
	{
		EffectComponent->Activate(true);
	}
	EmitEvent(ETDDamageEvent::Activate, nullptr, GetActorLocation());
	if (CanContinue() && Definition->Mode == ETDDamageEntityMode::Shockwave)
	{
		ExpandShockwave(0.f, SimulationTime);
	}
}

void ATDDamageEntity::ProcessTimeline()
{
	if (!CanContinue() || bIsProcessingTimeline)
	{
		return;
	}
	TGuardValue<bool> ProcessingGuard(bIsProcessingTimeline, true);
	const double CurrentTime = GetWorld()->GetTimeSeconds();
	int32 PulseCount = 0;
	for (int32 Step = 0; Step < 4096 && CanContinue(); ++Step)
	{
		if (bIsActive && PulseCount >= 8 && NextPulseTime <= CurrentTime)
		{
			NextPulseTime = CurrentTime + Definition->PulseInterval;
		}
		double NextTime = FMath::Min(CurrentTime, ExpirationTime);
		NextTime = FMath::Min(NextTime, bIsActive ? NextPulseTime : ActivationTime);
		if (!ScheduledActions.IsEmpty())
		{
			NextTime = FMath::Min(NextTime, ScheduledActions[0].ExecuteTime);
		}
		const bool bNeedsTarget = bIsHoming && HomingSettings.TargetLossPolicy == ETDHomingTargetLossPolicy::Reacquire
			&& !IsValidHomingTarget(HomingTarget.Get());
		if (bNeedsTarget)
		{
			NextTime = FMath::Min(NextTime, NextHomingSearchTime);
		}
		NextTime = FMath::Max(SimulationTime, NextTime);
		AdvanceMotion(NextTime);
		EventTime = SimulationTime;
		if (!CanContinue())
		{
			return;
		}
		if (SimulationTime < NextTime)
		{
			continue;
		}
		if (SimulationTime >= ExpirationTime)
		{
			Expire();
			return;
		}
		if (!bIsActive && ActivationTime <= SimulationTime)
		{
			Activate();
			continue;
		}
		if (!ScheduledActions.IsEmpty() && ScheduledActions[0].ExecuteTime <= SimulationTime)
		{
			ExecuteNextScheduledAction();
			continue;
		}
		if (bIsActive && NextPulseTime <= SimulationTime)
		{
			NextPulseTime += Definition->PulseInterval;
			++PulseCount;
			Pulse(SimulationTime);
			continue;
		}
		if (bNeedsTarget && NextHomingSearchTime <= SimulationTime)
		{
			AcquireHomingTarget(nullptr);
			continue;
		}
		return;
	}
	if (CanContinue())
	{
		Finish();
	}
}

void ATDDamageEntity::ScheduleAction(const FTDDamageAction& Action, const TArray<FTDDamageRule>& Rules,
	ETDDamageEvent Event, const FTDDamageContext& ActionContext, AActor* Target, const FVector& Location)
{
	if (!CanContinue() || !FMath::IsFinite(Action.DelaySeconds) || Action.DelaySeconds <= 0.f
		|| Event == ETDDamageEvent::Expire || Event == ETDDamageEvent::End || ScheduledActions.Num() >= 2048)
	{
		return;
	}
	const double ExecuteTime = (bIsProcessingTimeline ? EventTime : GetWorld()->GetTimeSeconds()) + Action.DelaySeconds;
	if (ExecuteTime >= ExpirationTime)
	{
		return;
	}
	FTDScheduledDamageAction Scheduled;
	Scheduled.Action = Action;
	Scheduled.Action.DelaySeconds = 0.f;
	Scheduled.Rules = Rules;
	Scheduled.Event = Event;
	Scheduled.Context = ActionContext;
	Scheduled.Target = Target;
	Scheduled.Location = Location;
	Scheduled.ExecuteTime = ExecuteTime;
	Scheduled.Sequence = NextActionSequence++;
	ScheduledActions.Add(MoveTemp(Scheduled));
	ScheduledActions.Sort([](const FTDScheduledDamageAction& Left, const FTDScheduledDamageAction& Right)
	{
		return Left.ExecuteTime < Right.ExecuteTime
			|| (Left.ExecuteTime == Right.ExecuteTime && Left.Sequence < Right.Sequence);
	});
}

void ATDDamageEntity::ExecuteNextScheduledAction()
{
	FTDScheduledDamageAction Scheduled = MoveTemp(ScheduledActions[0]);
	ScheduledActions.RemoveAt(0);
	if (UTDDamageSubsystem* Subsystem = GetWorld()->GetSubsystem<UTDDamageSubsystem>())
	{
		Subsystem->ExecuteScheduledAction(Scheduled.Action, Scheduled.Rules, Scheduled.Event,
			Scheduled.Context, Scheduled.Target.Get(), Scheduled.Location, this);
	}
}

void ATDDamageEntity::ApplyHoming(const FTDHomingSettings& Settings, AActor* EventTarget)
{
	FString Error;
	if (!CanContinue() || Definition->Mode != ETDDamageEntityMode::Projectile || !Settings.Validate(Error))
	{
		return;
	}
	ProcessTimeline();
	if (!CanContinue())
	{
		return;
	}
	const bool bKeepTarget = !Settings.bRetargetOnApply && IsValidHomingTarget(HomingTarget.Get());
	HomingSettings = Settings;
	bIsHoming = true;
	++HomingRevision;
	if (bKeepTarget)
	{
		NextHomingSearchTime = EventTime + HomingSettings.RetargetInterval;
		return;
	}
	AcquireHomingTarget(EventTarget);
}

void ATDDamageEntity::StopHoming()
{
	ProcessTimeline();
	bIsHoming = false;
	HomingTarget.Reset();
	++HomingRevision;
}

bool ATDDamageEntity::IsHoming() const
{
	return CanContinue() && bIsHoming;
}

AActor* ATDDamageEntity::GetHomingTarget() const
{
	return IsHoming() && IsValidHomingTarget(HomingTarget.Get()) ? HomingTarget.Get() : nullptr;
}

FVector ATDDamageEntity::GetTravelDirection() const
{
	return TravelDirection;
}

bool ATDDamageEntity::IsValidHomingTarget(const AActor* Target) const
{
	if (!IsValid(Target) || Target->IsActorBeingDestroyed() || !CanContinue())
	{
		return false;
	}
	UTDDamageSubsystem* Subsystem = GetWorld()->GetSubsystem<UTDDamageSubsystem>();
	return Subsystem && Subsystem->CanTarget(Target->FindComponentByClass<UTDCombatComponent>(),
		Context, Definition->TargetPolicy);
}

void ATDDamageEntity::AcquireHomingTarget(AActor* EventTarget)
{
	HomingTarget.Reset();
	NextHomingSearchTime = EventTime + HomingSettings.RetargetInterval;
	const double SearchRadiusSquared = FMath::Square(static_cast<double>(HomingSettings.SearchRadius));
	if (HomingSettings.TargetSelection == ETDHomingTargetSelection::EventTarget)
	{
		if (IsValidHomingTarget(EventTarget)
			&& FVector::DistSquared(GetActorLocation(), EventTarget->GetActorLocation()) <= SearchRadiusSquared)
		{
			HomingTarget = EventTarget;
			return;
		}
		if (HomingSettings.TargetLossPolicy == ETDHomingTargetLossPolicy::ContinueStraight)
		{
			return;
		}
	}
	UTDDamageSubsystem* Subsystem = GetWorld()->GetSubsystem<UTDDamageSubsystem>();
	if (!Subsystem)
	{
		return;
	}
	TArray<UTDCombatComponent*> Targets;
	Subsystem->GatherTargets(GetActorLocation(), HomingSettings.SearchRadius, HomingSettings.SearchRadius,
		Context, Definition->TargetPolicy, Targets);
	double ClosestDistanceSquared = SearchRadiusSquared;
	uint32 ClosestId = MAX_uint32;
	for (UTDCombatComponent* Target : Targets)
	{
		if (!IsValid(Target) || !IsValid(Target->GetOwner()))
		{
			continue;
		}
		AActor* Candidate = Target->GetOwner();
		const double DistanceSquared = FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared > ClosestDistanceSquared
			|| (DistanceSquared == ClosestDistanceSquared && Candidate->GetUniqueID() >= ClosestId))
		{
			continue;
		}
		ClosestDistanceSquared = DistanceSquared;
		ClosestId = Candidate->GetUniqueID();
		HomingTarget = Candidate;
	}
}

void ATDDamageEntity::UpdateHomingDirection(float DeltaSeconds)
{
	if (!bIsHoming || !IsValidHomingTarget(HomingTarget.Get()))
	{
		HomingTarget.Reset();
		return;
	}
	const FVector DesiredDirection = (HomingTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal(
		UE_SMALL_NUMBER, TravelDirection);
	const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(TravelDirection, DesiredDirection), -1., 1.));
	if (Angle <= UE_SMALL_NUMBER)
	{
		return;
	}
	const double AllowedAngle = FMath::DegreesToRadians(HomingSettings.TurnRateDegreesPerSecond) * DeltaSeconds;
	const FQuat Rotation = FQuat::FindBetweenNormals(TravelDirection, DesiredDirection);
	const FQuat AppliedRotation = FQuat::Slerp(FQuat::Identity, Rotation, FMath::Min(1., AllowedAngle / Angle));
	TravelDirection = AppliedRotation.RotateVector(TravelDirection).GetSafeNormal(UE_SMALL_NUMBER, TravelDirection);
	SetActorRotation(TravelDirection.Rotation());
}

void ATDDamageEntity::Pulse(double PulseTime)
{
	EmitEvent(ETDDamageEvent::Pulse, nullptr, GetActorLocation());
	if (!CanContinue())
	{
		return;
	}
	if (Definition->Mode == ETDDamageEntityMode::Area)
	{
		HitArea(Definition->Radius, 0.f, PulseTime);
		return;
	}
	if (Definition->Mode != ETDDamageEntityMode::Mine)
	{
		return;
	}
	UTDDamageSubsystem* Subsystem = GetWorld()->GetSubsystem<UTDDamageSubsystem>();
	if (!Subsystem)
	{
		return;
	}
	TArray<UTDCombatComponent*> Targets;
	Subsystem->GatherTargets(GetActorLocation(), Definition->Radius, Definition->HalfHeight,
		Context, Definition->TargetPolicy, Targets);
	for (UTDCombatComponent* Target : Targets)
	{
		if (!IsValid(Target) || !HasLineOfSight(Target->GetOwner()))
		{
			continue;
		}
		TWeakObjectPtr<AActor> TriggeringActor = Target->GetOwner();
		EmitEvent(ETDDamageEvent::Trigger, TriggeringActor.Get(), GetActorLocation());
		if (CanContinue())
		{
			HitArea(Definition->Radius, 0.f, PulseTime);
			Complete(false, TriggeringActor.Get());
		}
		return;
	}
}

void ATDDamageEntity::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!CanContinue())
	{
		return;
	}
	ProcessTimeline();
	if (CanContinue() && Definition->bDrawDebug)
	{
		DrawShape();
	}
}

void ATDDamageEntity::AdvanceMotion(double CurrentTime)
{
	if (!CanContinue())
	{
		return;
	}
	const double MotionTime = FMath::Min(CurrentTime, ExpirationTime);
	const float Elapsed = static_cast<float>(FMath::Max(0., MotionTime - SimulationTime));
	SimulationTime = MotionTime;
	EventTime = SimulationTime;
	if (!bIsActive || Elapsed <= 0.f)
	{
		return;
	}
	if (Definition->Mode == ETDDamageEntityMode::Projectile)
	{
		UpdateHomingDirection(Elapsed);
		MoveProjectile(Elapsed, MotionTime);
	}
	else if (Definition->Mode == ETDDamageEntityMode::Shockwave)
	{
		ExpandShockwave(Elapsed, MotionTime);
	}
}

void ATDDamageEntity::MoveProjectile(float DeltaSeconds, double HitTime)
{
	UTDDamageSubsystem* Subsystem = GetWorld()->GetSubsystem<UTDDamageSubsystem>();
	if (!Subsystem)
	{
		return;
	}
	const FVector Start = GetActorLocation();
	const FVector End = Start + TravelDirection * Definition->ProjectileSpeed * DeltaSeconds;
	const double StartTime = HitTime - DeltaSeconds;
	const FVector Segment = End - Start;
	const double SegmentLengthSquared = Segment.SizeSquared();
	const float SearchRadius = static_cast<float>(Segment.Size() * 0.5) + Definition->ProjectileRadius;
	TArray<UTDCombatComponent*> Targets;
	Subsystem->GatherTargets((Start + End) * 0.5, SearchRadius, SearchRadius,
		Context, Definition->TargetPolicy, Targets);
	TMap<TWeakObjectPtr<UTDCombatComponent>, float> TargetFractions;
	for (UTDCombatComponent* Target : Targets)
	{
		if (!IsValid(Target) || !IsValid(Target->GetOwner()))
		{
			continue;
		}
		const FVector Offset = Start - Target->GetOwner()->GetActorLocation();
		const double C = Offset.SizeSquared() - FMath::Square(Definition->ProjectileRadius);
		if (C <= 0.)
		{
			TargetFractions.Add(Target, 0.f);
			continue;
		}
		const double B = FVector::DotProduct(Offset, Segment);
		const double Discriminant = B * B - SegmentLengthSquared * C;
		if (SegmentLengthSquared <= UE_SMALL_NUMBER || Discriminant < 0.)
		{
			continue;
		}
		const double Fraction = (-B - FMath::Sqrt(Discriminant)) / SegmentLengthSquared;
		if (Fraction >= 0. && Fraction <= 1.)
		{
			TargetFractions.Add(Target, static_cast<float>(Fraction));
		}
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDDamageProjectile), false, this);
	QueryParams.AddIgnoredActor(Context.Caster.Get());
	FHitResult WorldHit;
	float BlockingFraction = 1.f;
	bool bHasWorldHit = false;
	for (int32 SweepCount = 0; SweepCount < 128; ++SweepCount)
	{
		if (!GetWorld()->SweepSingleByChannel(WorldHit, Start, End, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(Definition->ProjectileRadius), QueryParams))
		{
			break;
		}
		AActor* HitActor = WorldHit.GetActor();
		UTDCombatComponent* Combatant = IsValid(HitActor) ? HitActor->FindComponentByClass<UTDCombatComponent>() : nullptr;
		if (Combatant && Subsystem->CanTarget(Combatant, Context, Definition->TargetPolicy))
		{
			float& Fraction = TargetFractions.FindOrAdd(Combatant, WorldHit.Time);
			Fraction = FMath::Min(Fraction, WorldHit.Time);
		}
		if (Combatant || (IsValid(HitActor) && HitActor->IsA<ATDDamageEntity>()))
		{
			QueryParams.AddIgnoredActor(HitActor);
			if (SweepCount < 127)
			{
				continue;
			}
		}
		BlockingFraction = WorldHit.Time;
		bHasWorldHit = true;
		break;
	}
	TArray<TWeakObjectPtr<UTDCombatComponent>> OrderedTargets;
	TargetFractions.GenerateKeyArray(OrderedTargets);
	OrderedTargets.Sort([&TargetFractions](const TWeakObjectPtr<UTDCombatComponent>& Left,
		const TWeakObjectPtr<UTDCombatComponent>& Right)
	{
		const float LeftFraction = TargetFractions.FindChecked(Left);
		const float RightFraction = TargetFractions.FindChecked(Right);
		return LeftFraction < RightFraction || (LeftFraction == RightFraction
			&& Left.IsValid() && Right.IsValid() && Left->GetUniqueID() < Right->GetUniqueID());
	});
	for (const TWeakObjectPtr<UTDCombatComponent>& Target : OrderedTargets)
	{
		const float Fraction = TargetFractions.FindChecked(Target);
		if (Fraction > BlockingFraction || (bHasWorldHit && Fraction == BlockingFraction))
		{
			break;
		}
		SetActorLocation(FMath::Lerp(Start, End, Fraction));
		EventTime = StartTime + DeltaSeconds * Fraction;
		const uint64 PreviousHomingRevision = HomingRevision;
		if (HitTarget(Target.Get(), GetActorLocation(), EventTime) && CanContinue() && Definition->bDestroyOnHit)
		{
			Complete(false, Target.IsValid() ? Target->GetOwner() : nullptr);
		}
		if (!CanContinue())
		{
			return;
		}
		if (PreviousHomingRevision != HomingRevision
			|| (!ScheduledActions.IsEmpty() && ScheduledActions[0].ExecuteTime < HitTime))
		{
			SimulationTime = EventTime;
			return;
		}
	}
	SetActorLocation(FMath::Lerp(Start, End, BlockingFraction));
	if (bHasWorldHit)
	{
		EventTime = StartTime + DeltaSeconds * BlockingFraction;
		Complete(false, nullptr);
	}
}

void ATDDamageEntity::ExpandShockwave(float DeltaSeconds, double HitTime)
{
	const float PreviousInnerRadius = RingInnerRadius;
	const float Expansion = Definition->ExpansionSpeed * DeltaSeconds;
	RingInnerRadius += Expansion;
	RingOuterRadius += Expansion;
	HitArea(RingOuterRadius, PreviousInnerRadius, HitTime);
}

void ATDDamageEntity::HitArea(float OuterRadius, float InnerRadius, double HitTime)
{
	if (!CanContinue())
	{
		return;
	}
	UTDDamageSubsystem* Subsystem = GetWorld()->GetSubsystem<UTDDamageSubsystem>();
	if (!Subsystem)
	{
		return;
	}
	TArray<UTDCombatComponent*> Targets;
	Subsystem->GatherTargets(GetActorLocation(), OuterRadius, Definition->HalfHeight,
		Context, Definition->TargetPolicy, Targets);
	for (UTDCombatComponent* Target : Targets)
	{
		if (!CanContinue())
		{
			return;
		}
		if (!IsValid(Target) || !IsValid(Target->GetOwner()))
		{
			continue;
		}
		const FVector TargetLocation = Target->GetOwner()->GetActorLocation();
		if (FVector::DistSquared2D(GetActorLocation(), TargetLocation) < FMath::Square(InnerRadius))
		{
			continue;
		}
		HitTarget(Target, TargetLocation, HitTime);
	}
}

bool ATDDamageEntity::HitTarget(UTDCombatComponent* Target, const FVector& Location, double HitTime)
{
	if (!CanContinue() || !IsValid(Target))
	{
		return false;
	}
	UTDDamageSubsystem* Subsystem = GetWorld()->GetSubsystem<UTDDamageSubsystem>();
	if (!Subsystem || !Subsystem->CanTarget(Target, Context, Definition->TargetPolicy)
		|| !HasLineOfSight(Target->GetOwner()))
	{
		return false;
	}
	const int32 Count = TargetHitCounts.FindRef(Target);
	const double* PreviousHitTime = TargetHitTimes.Find(Target);
	if ((Definition->MaxHitsPerTarget > 0 && Count >= Definition->MaxHitsPerTarget)
		|| (PreviousHitTime && HitTime - *PreviousHitTime + UE_SMALL_NUMBER < Definition->HitInterval))
	{
		return false;
	}
	TargetHitCounts.Add(Target, Count + 1);
	TargetHitTimes.Add(Target, HitTime);
	EmitEvent(ETDDamageEvent::Hit, Target->GetOwner(), Location);
	return true;
}

bool ATDDamageEntity::HasLineOfSight(const AActor* Target) const
{
	if (!IsValid(Target))
	{
		return false;
	}
	if (!Definition->bRequireLineOfSight)
	{
		return true;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDDamageLineOfSight), false, this);
	QueryParams.AddIgnoredActor(Context.Caster.Get());
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), Target->GetActorLocation(),
		ECC_Visibility, QueryParams) || Hit.GetActor() == Target;
}

void ATDDamageEntity::EmitEvent(ETDDamageEvent Event, AActor* Target, const FVector& Location)
{
	if (!IsValid(Definition) || IsActorBeingDestroyed() || !GetWorld())
	{
		return;
	}
	if (UTDDamageSubsystem* Subsystem = GetWorld()->GetSubsystem<UTDDamageSubsystem>())
	{
		FTDDamageContext EventContext = Context;
		EventContext.Direction = TravelDirection;
		Subsystem->ExecuteRules(Definition->Rules, Event, EventContext, Target, Location, this);
	}
}

void ATDDamageEntity::Expire()
{
	if (!CanContinue())
	{
		return;
	}
	Complete(true, nullptr);
}

void ATDDamageEntity::Finish()
{
	Complete(false, nullptr);
}

void ATDDamageEntity::Complete(bool bHasExpired, AActor* Target)
{
	if (!CanContinue())
	{
		return;
	}
	bHasFinished = true;
	ScheduledActions.Reset();
	bIsHoming = false;
	HomingTarget.Reset();
	SetActorTickEnabled(false);
	TWeakObjectPtr<AActor> EventTarget = Target;
	if (bHasExpired)
	{
		EmitEvent(ETDDamageEvent::Expire, EventTarget.Get(), GetActorLocation());
	}
	EmitEvent(ETDDamageEvent::End, EventTarget.Get(), GetActorLocation());
	Destroy();
}

bool ATDDamageEntity::CanContinue() const
{
	return !bHasFinished && !IsActorBeingDestroyed() && IsValid(Definition) && GetWorld();
}

void ATDDamageEntity::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bHasFinished = true;
	ScheduledActions.Reset();
	bIsHoming = false;
	HomingTarget.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATDDamageEntity::DrawShape() const
{
	const FColor Color = Definition->DebugColor.ToFColor(true);
	if (Definition->Mode == ETDDamageEntityMode::Projectile)
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), Definition->ProjectileRadius, 12, Color);
		return;
	}
	const FVector Height(0.f, 0.f, Definition->HalfHeight);
	const bool bIsShockwave = Definition->Mode == ETDDamageEntityMode::Shockwave;
	const float OuterRadius = bIsShockwave ? RingOuterRadius : Definition->Radius;
	DrawDebugCylinder(GetWorld(), GetActorLocation() - Height, GetActorLocation() + Height,
		OuterRadius, 32, Color);
	if (bIsShockwave && RingInnerRadius > 0.f)
	{
		DrawDebugCylinder(GetWorld(), GetActorLocation() - Height, GetActorLocation() + Height,
			RingInnerRadius, 32, Color);
	}
}
