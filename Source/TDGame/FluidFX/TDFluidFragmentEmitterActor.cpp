#include "FluidFX/TDFluidFragmentEmitterActor.h"
#include "Actors/TDBreakableActor.h"
#include "Components/SceneComponent.h"
#include "FluidFX/TDFluidEmberComponent.h"
#include "FluidFX/TDFluidFireActor.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"

namespace
{
	constexpr float MovedLeafDistanceCm = 30.0f;
	constexpr float SmokeBurstRadiusScale = 1.5f;
	constexpr float FireStampSpacingRatio = 0.5f;
	constexpr float BurstRingStampRadiusRatio = 0.5f;

	struct FTDFragmentCandidate
	{
		int32 LeafIndex = 0;
		double SpeedCmPerSec = 0.0;
	};

	FTDFluidBrushStamp MakeTrailStamp(const FVector& Location, const FVector& Velocity, float RadiusCm, float DensityStrength, ETDFluidBrushChannel Channel)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Location;
		Stamp.WorldVelocity = Velocity;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = DensityStrength;
		Stamp.VelocityStrength = 1.0f;
		Stamp.Channel = Channel;
		return Stamp;
	}

	void WakeTarget(ATDFluidLiveActor* TargetActor)
	{
		if (TargetActor && TargetActor->LiveComponent)
		{
			TargetActor->LiveComponent->SetSimulationAwake(true);
		}
	}
}

void FTDFluidFragmentPositionHistory::Reset(int32 InLeafCount)
{
	LeafCount = FMath::Max(InLeafCount, 0);
	WrittenFrameCount = 0;
	Positions.Init(FVector::ZeroVector, FrameCapacity * LeafCount);
}

void FTDFluidFragmentPositionHistory::PushFrame(const TArray<FVector>& FramePositions)
{
	if (FramePositions.Num() != LeafCount)
	{
		return;
	}

	const int32 SlotOffset = (WrittenFrameCount % FrameCapacity) * LeafCount;
	for (int32 LeafIndex = 0; LeafIndex < LeafCount; ++LeafIndex)
	{
		Positions[SlotOffset + LeafIndex] = FramePositions[LeafIndex];
	}

	++WrittenFrameCount;
}

void FTDFluidFragmentPositionHistory::GetLaggedFrame(int32 LagFrames, TArray<FVector>& OutPositions) const
{
	OutPositions.Reset();
	if (WrittenFrameCount <= 0 || LeafCount <= 0)
	{
		return;
	}

	const int32 ClampedLagFrames = FMath::Clamp(LagFrames, 0, FrameCapacity - 1);
	const int32 FrameNumber = FMath::Max(WrittenFrameCount - 1 - ClampedLagFrames, 0);
	const int32 SlotOffset = (FrameNumber % FrameCapacity) * LeafCount;
	OutPositions.SetNumUninitialized(LeafCount);
	for (int32 LeafIndex = 0; LeafIndex < LeafCount; ++LeafIndex)
	{
		OutPositions[LeafIndex] = Positions[SlotOffset + LeafIndex];
	}
}

ATDFluidFragmentEmitterActor::ATDFluidFragmentEmitterActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void ATDFluidFragmentEmitterActor::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(SourceBreakableActor) || !ResolveGeometryCollectionComponent())
	{
		return;
	}

	SetActorTickInterval(TrailSettings.ArmedPollIntervalSeconds);
	SetActorTickEnabled(true);
}

void ATDFluidFragmentEmitterActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsValid(SourceBreakableActor) || !GeometryCollectionComponent.IsValid())
	{
		StopFragmentTracking();
		return;
	}

	if (bIsTracking)
	{
		StepFragmentTracking(DeltaSeconds);
		return;
	}

	PollForRootBreak();
}

void ATDFluidFragmentEmitterActor::TriggerBreakAt(const FVector& HitLocation)
{
	if (bHasStartedTracking || !IsValid(SourceBreakableActor))
	{
		return;
	}

	UGeometryCollectionComponent* Component = ResolveGeometryCollectionComponent();
	if (Component && TrailSettings.bShouldCrumbleOnTrigger)
	{
		Component->CrumbleActiveClusters();
	}

	SourceBreakableActor->StartBreak(HitLocation);
	BeginFragmentTracking(HitLocation);
}

UGeometryCollectionComponent* ATDFluidFragmentEmitterActor::ResolveGeometryCollectionComponent()
{
	if (UGeometryCollectionComponent* CachedComponent = GeometryCollectionComponent.Get())
	{
		return CachedComponent;
	}

	if (!IsValid(SourceBreakableActor))
	{
		return nullptr;
	}

	GeometryCollectionComponent = SourceBreakableActor->FindComponentByClass<UGeometryCollectionComponent>();
	return GeometryCollectionComponent.Get();
}

void ATDFluidFragmentEmitterActor::PollForRootBreak()
{
	const UGeometryCollectionComponent* Component = GeometryCollectionComponent.Get();
	if (!Component || !Component->IsRootBroken())
	{
		return;
	}

	BeginFragmentTracking(Component->GetComponentLocation());
}

void ATDFluidFragmentEmitterActor::BeginFragmentTracking(const FVector& CenterWorld)
{
	if (bHasStartedTracking)
	{
		return;
	}

	bHasStartedTracking = true;
	TArray<FVector> StartPositions;
	if (UGeometryCollectionComponent* Component = GeometryCollectionComponent.Get())
	{
		CollectLeafTransformIndices(*Component);
		CaptureLeafWorldPositions(*Component, StartPositions);
	}

	InitialLeafPositions = StartPositions;
	PositionHistory.Reset(LeafTransformIndices.Num());
	PositionHistory.PushFrame(StartPositions);
	LeafStampedFlags.Init(false, LeafTransformIndices.Num());

	WakeTarget(FireTargetActor);
	WakeTarget(SmokeTargetActor);
	StampBurst(CenterWorld);

	TrackingElapsedSeconds = 0.0f;
	bIsTracking = true;
	SetActorTickInterval(0.0f);
	SetActorTickEnabled(true);
}

void ATDFluidFragmentEmitterActor::CollectLeafTransformIndices(const UGeometryCollectionComponent& Component)
{
	LeafTransformIndices.Reset();

	const UGeometryCollection* RestCollection = Component.GetRestCollection();
	if (!RestCollection)
	{
		return;
	}

	const FGeometryCollection* Collection = RestCollection->GetGeometryCollection().Get();
	if (!Collection)
	{
		return;
	}

	const int32 TransformCount = Collection->NumElements(FGeometryCollection::TransformGroup);
	for (int32 TransformIndex = 0; TransformIndex < TransformCount; ++TransformIndex)
	{
		if (Collection->IsRigid(TransformIndex))
		{
			LeafTransformIndices.Add(TransformIndex);
		}
	}
}

void ATDFluidFragmentEmitterActor::CaptureLeafWorldPositions(UGeometryCollectionComponent& Component, TArray<FVector>& OutPositions) const
{
	OutPositions.Reset();
	if (LeafTransformIndices.IsEmpty())
	{
		return;
	}

	const FTransform ComponentTransform = Component.GetComponentTransform();
	const TArray<FTransform> LeafTransforms = Component.GetCurrentTransformsFromIndices(LeafTransformIndices);
	OutPositions.Reserve(LeafTransforms.Num());
	for (const FTransform& LeafTransform : LeafTransforms)
	{
		OutPositions.Add(ComponentTransform.TransformPosition(LeafTransform.GetTranslation()));
	}
}

void ATDFluidFragmentEmitterActor::StampBurst(const FVector& CenterWorld)
{
	BurstCenterWorld = CenterWorld;
	QueueBurstOnTarget(FireTargetActor, CenterWorld, ETDFluidBrushChannel::ChannelB_Fire, 1.0f, TrailSettings.FireDensityStrength);
	QueueBurstOnTarget(SmokeTargetActor, CenterWorld, ETDFluidBrushChannel::ChannelR_Mud, SmokeBurstRadiusScale, TrailSettings.SmokeDensityStrength);

	if (FireTargetActor && FireTargetActor->EmberComponent)
	{
		FireTargetActor->EmberComponent->SpawnBurst(CenterWorld, TrailSettings.EmberBurstCount, TrailSettings.BurstRingSpeedCmPerSec);
	}
}

void ATDFluidFragmentEmitterActor::QueueBurstOnTarget(ATDFluidLiveActor* TargetActor, const FVector& CenterWorld, ETDFluidBrushChannel Channel, float RadiusScale, float DensityStrength)
{
	if (!TargetActor || !TargetActor->LiveComponent)
	{
		return;
	}

	TArray<FTDFluidBrushStamp> BurstStamps;
	BuildBurstStamps(CenterWorld, TargetActor->LiveComponent->GetPlaneBasis(), TrailSettings, Channel, RadiusScale, DensityStrength, BurstStamps);
	QueueStampsOnTarget(TargetActor, BurstStamps);
}

void ATDFluidFragmentEmitterActor::StepFragmentTracking(float DeltaSeconds)
{
	if (DeltaSeconds <= UE_SMALL_NUMBER)
	{
		return;
	}

	UGeometryCollectionComponent* Component = GeometryCollectionComponent.Get();
	TrackingElapsedSeconds += DeltaSeconds;

	TArray<FVector> PreviousPositions;
	PositionHistory.GetLaggedFrame(0, PreviousPositions);
	TArray<FVector> CurrentPositions;
	CaptureLeafWorldPositions(*Component, CurrentPositions);
	PositionHistory.PushFrame(CurrentPositions);
	TArray<FVector> LaggedPositions;
	PositionHistory.GetLaggedFrame(TrailSettings.SmokeTrailLagFrames, LaggedPositions);

	SustainBurst();

	TArray<FTDFluidBrushStamp> FireStamps;
	TArray<FTDFluidBrushStamp> SmokeStamps;
	TArray<int32> StampedLeafIndices;
	BuildFragmentTrailStamps(PreviousPositions, CurrentPositions, LaggedPositions, DeltaSeconds, TrailSettings, FireStamps, SmokeStamps, &StampedLeafIndices);
	QueueStampsOnTarget(FireTargetActor, FireStamps);
	QueueStampsOnTarget(SmokeTargetActor, SmokeStamps);
	RecordStampedLeaves(StampedLeafIndices);
	UpdateMovedLeafCount(CurrentPositions);

	if (TrackingElapsedSeconds >= TrailSettings.TrackingDurationSeconds)
	{
		StopFragmentTracking();
	}
}

void ATDFluidFragmentEmitterActor::SustainBurst()
{
	if (TrailSettings.BurstSustainSeconds <= UE_SMALL_NUMBER || TrackingElapsedSeconds >= TrailSettings.BurstSustainSeconds)
	{
		return;
	}

	const float RemainingRatio = 1.0f - TrackingElapsedSeconds / TrailSettings.BurstSustainSeconds;
	QueueBurstOnTarget(FireTargetActor, BurstCenterWorld, ETDFluidBrushChannel::ChannelB_Fire, 1.0f, TrailSettings.FireDensityStrength * RemainingRatio);
	QueueBurstOnTarget(SmokeTargetActor, BurstCenterWorld, ETDFluidBrushChannel::ChannelR_Mud, SmokeBurstRadiusScale, TrailSettings.SmokeDensityStrength * RemainingRatio);
}

void ATDFluidFragmentEmitterActor::QueueStampsOnTarget(ATDFluidLiveActor* TargetActor, const TArray<FTDFluidBrushStamp>& Stamps)
{
	if (!TargetActor || !TargetActor->LiveComponent)
	{
		return;
	}

	for (const FTDFluidBrushStamp& Stamp : Stamps)
	{
		TargetActor->LiveComponent->QueueBrushStamp(Stamp);
	}

	TotalStampCount += Stamps.Num();
}

void ATDFluidFragmentEmitterActor::RecordStampedLeaves(const TArray<int32>& StampedLeafIndices)
{
	for (const int32 LeafIndex : StampedLeafIndices)
	{
		if (!LeafStampedFlags.IsValidIndex(LeafIndex) || LeafStampedFlags[LeafIndex])
		{
			continue;
		}

		LeafStampedFlags[LeafIndex] = true;
		++StampedLeafCount;
	}
}

void ATDFluidFragmentEmitterActor::UpdateMovedLeafCount(const TArray<FVector>& CurrentPositions)
{
	MovedLeafCount = 0;
	const int32 LeafCount = FMath::Min(CurrentPositions.Num(), InitialLeafPositions.Num());
	for (int32 LeafIndex = 0; LeafIndex < LeafCount; ++LeafIndex)
	{
		if (FVector::DistSquared(CurrentPositions[LeafIndex], InitialLeafPositions[LeafIndex]) >= FMath::Square(MovedLeafDistanceCm))
		{
			++MovedLeafCount;
		}
	}
}

void ATDFluidFragmentEmitterActor::StopFragmentTracking()
{
	bIsTracking = false;
	SetActorTickEnabled(false);
}

void ATDFluidFragmentEmitterActor::BuildFragmentTrailStamps(
	const TArray<FVector>& PreviousPositions,
	const TArray<FVector>& CurrentPositions,
	const TArray<FVector>& LaggedPositions,
	float DeltaTime,
	const FTDFluidFragmentTrailSettings& Settings,
	TArray<FTDFluidBrushStamp>& OutFireStamps,
	TArray<FTDFluidBrushStamp>& OutSmokeStamps,
	TArray<int32>* OutStampedLeafIndices)
{
	OutFireStamps.Reset();
	OutSmokeStamps.Reset();
	if (OutStampedLeafIndices)
	{
		OutStampedLeafIndices->Reset();
	}

	const int32 LeafCount = PreviousPositions.Num();
	if (DeltaTime <= UE_SMALL_NUMBER || CurrentPositions.Num() != LeafCount || LaggedPositions.Num() != LeafCount)
	{
		return;
	}

	TArray<FTDFragmentCandidate> Candidates;
	for (int32 LeafIndex = 0; LeafIndex < LeafCount; ++LeafIndex)
	{
		const double SpeedCmPerSec = (CurrentPositions[LeafIndex] - PreviousPositions[LeafIndex]).Size() / DeltaTime;
		if (!FMath::IsFinite(SpeedCmPerSec) || SpeedCmPerSec < Settings.MinFragmentSpeedCmPerSec || SpeedCmPerSec > Settings.MaxFragmentSpeedCmPerSec)
		{
			continue;
		}

		FTDFragmentCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.LeafIndex = LeafIndex;
		Candidate.SpeedCmPerSec = SpeedCmPerSec;
	}

	Candidates.Sort([](const FTDFragmentCandidate& Left, const FTDFragmentCandidate& Right)
	{
		if (Left.SpeedCmPerSec != Right.SpeedCmPerSec)
		{
			return Left.SpeedCmPerSec > Right.SpeedCmPerSec;
		}
		return Left.LeafIndex < Right.LeafIndex;
	});

	const int32 SelectedCount = FMath::Min(Candidates.Num(), FMath::Max(Settings.MaxFragmentsPerStep, 0));
	const int32 MaxStampsPerFragment = FMath::Max(Settings.MaxStampsPerFragment, 1);
	for (int32 CandidateIndex = 0; CandidateIndex < SelectedCount; ++CandidateIndex)
	{
		const int32 LeafIndex = Candidates[CandidateIndex].LeafIndex;
		const FVector& PreviousPosition = PreviousPositions[LeafIndex];
		const FVector& CurrentPosition = CurrentPositions[LeafIndex];
		const FVector StampVelocity = (CurrentPosition - PreviousPosition) / DeltaTime * Settings.VelocityInheritance;
		const double DistanceCm = (CurrentPosition - PreviousPosition).Size();
		const int32 SubStepCount = FMath::Clamp(FMath::CeilToInt(DistanceCm / (FireStampSpacingRatio * Settings.FireStampRadiusCm)), 1, MaxStampsPerFragment);
		for (int32 SubStepIndex = 1; SubStepIndex <= SubStepCount; ++SubStepIndex)
		{
			const double Alpha = static_cast<double>(SubStepIndex) / static_cast<double>(SubStepCount);
			OutFireStamps.Add(MakeTrailStamp(FMath::Lerp(PreviousPosition, CurrentPosition, Alpha), StampVelocity, Settings.FireStampRadiusCm, Settings.FireDensityStrength, ETDFluidBrushChannel::ChannelB_Fire));
		}

		if (!LaggedPositions[LeafIndex].ContainsNaN())
		{
			OutSmokeStamps.Add(MakeTrailStamp(LaggedPositions[LeafIndex], StampVelocity, Settings.SmokeStampRadiusCm, Settings.SmokeDensityStrength, ETDFluidBrushChannel::ChannelR_Mud));
		}

		if (OutStampedLeafIndices)
		{
			OutStampedLeafIndices->Add(LeafIndex);
		}
	}
}

void ATDFluidFragmentEmitterActor::BuildBurstStamps(
	const FVector& CenterWorld,
	const FTDFluidPlaneBasis& PlaneBasis,
	const FTDFluidFragmentTrailSettings& Settings,
	ETDFluidBrushChannel Channel,
	float RadiusScale,
	float DensityStrength,
	TArray<FTDFluidBrushStamp>& OutStamps)
{
	OutStamps.Reset();

	FTDFluidBrushStamp CoreStamp = MakeTrailStamp(CenterWorld, FVector::ZeroVector, Settings.BurstCoreRadiusCm * RadiusScale, DensityStrength, Channel);
	CoreStamp.VelocityStrength = 0.0f;
	OutStamps.Add(CoreStamp);

	const int32 RingStampCount = FMath::Max(Settings.BurstRingStampCount, 0);
	for (int32 RingIndex = 0; RingIndex < RingStampCount; ++RingIndex)
	{
		const double Angle = UE_DOUBLE_TWO_PI * static_cast<double>(RingIndex) / static_cast<double>(RingStampCount);
		const FVector Outward = PlaneBasis.U * FMath::Cos(Angle) + PlaneBasis.V * FMath::Sin(Angle);
		OutStamps.Add(MakeTrailStamp(
			CenterWorld + Outward * Settings.BurstRingRadiusCm,
			Outward * Settings.BurstRingSpeedCmPerSec,
			Settings.BurstRingRadiusCm * BurstRingStampRadiusRatio * RadiusScale,
			DensityStrength,
			Channel));
	}
}
