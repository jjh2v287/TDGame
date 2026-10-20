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
	if (IsValid(SourceBreakableActor))
	{
		SourceBrokenDelegateHandle = SourceBreakableActor->OnBrokenNative.AddUObject(this, &ATDFluidFragmentEmitterActor::HandleSourceBroken);
	}

	if (!IsValid(SourceBreakableActor) || !ResolveGeometryCollectionComponent())
	{
		return;
	}

	SetActorTickInterval(TrailSettings.ArmedPollIntervalSeconds);
	SetActorTickEnabled(true);
}

void ATDFluidFragmentEmitterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(SourceBreakableActor) && SourceBrokenDelegateHandle.IsValid())
	{
		SourceBreakableActor->OnBrokenNative.Remove(SourceBrokenDelegateHandle);
	}
	SourceBrokenDelegateHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATDFluidFragmentEmitterActor::HandleSourceBroken(const FVector& BreakLocation)
{
	if (bHasStartedTracking || bIsTriggeringBreak)
	{
		return;
	}

	const UGeometryCollectionComponent* Component = ResolveGeometryCollectionComponent();
	BeginFragmentTracking(Component ? Component->GetComponentLocation() : BreakLocation);
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

	bIsTriggeringBreak = true;
	SourceBreakableActor->StartBreak(HitLocation);
	bIsTriggeringBreak = false;
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

	TArray<FTDFluidFragmentTarget> ActiveTargets;
	ResolveActiveTargets(ActiveTargets);
	for (const FTDFluidFragmentTarget& Target : ActiveTargets)
	{
		WakeTarget(Target.TargetActor);
	}
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
	BurstCenterWorld = CenterWorld + FVector::UpVector * TrailSettings.BurstCenterHeightOffsetCm;
	QueueBurstOnTargets(1.0f, 1.0f);

	if (FireTargetActor && FireTargetActor->EmberComponent)
	{
		FireTargetActor->EmberComponent->SpawnBurst(CenterWorld, TrailSettings.EmberBurstCount, TrailSettings.BurstRingSpeedCmPerSec);
	}
}

void ATDFluidFragmentEmitterActor::QueueBurstOnTargets(float DensityRatio, float HeatRatio)
{
	TArray<FTDFluidFragmentTarget> ActiveTargets;
	ResolveActiveTargets(ActiveTargets);
	for (const FTDFluidFragmentTarget& Target : ActiveTargets)
	{
		ATDFluidLiveActor* TargetActor = Target.TargetActor;
		if (!Target.bReceivesBurst || !TargetActor || !TargetActor->LiveComponent)
		{
			continue;
		}

		const float BaseDensity = Target.bUsesLaggedPositions ? TrailSettings.SmokeDensityStrength : TrailSettings.FireDensityStrength;
		const float BaseHeat = Target.bUsesLaggedPositions ? TrailSettings.SmokeHeatStrength : TrailSettings.FireHeatStrength;
		TArray<FTDFluidBrushStamp> BurstStamps;
		BuildBurstStamps(
			BurstCenterWorld,
			TargetActor->LiveComponent->GetPlaneBasis(),
			TrailSettings,
			Target.Channel,
			Target.BurstRadiusScale,
			BaseDensity * Target.DensityScale * DensityRatio,
			FMath::Clamp(BaseHeat * Target.HeatScale * HeatRatio, 0.0f, 1.0f),
			BurstStamps);
		for (int32 StampIndex = BurstStamps.Num() - 1; StampIndex >= 0; --StampIndex)
		{
			const float HeightWeight = ComputeTargetHeightWeight(Target, BurstStamps[StampIndex].WorldLocation.Z, BurstCenterWorld.Z);
			if (HeightWeight <= 0.0f)
			{
				BurstStamps.RemoveAt(StampIndex);
				continue;
			}

			BurstStamps[StampIndex].DensityStrength *= HeightWeight;
			BurstStamps[StampIndex].HeatStrength *= HeightWeight;
		}
		QueueStampsOnTarget(TargetActor, BurstStamps);
	}
}

void ATDFluidFragmentEmitterActor::ResolveActiveTargets(TArray<FTDFluidFragmentTarget>& OutTargets) const
{
	if (Targets.IsEmpty())
	{
		BuildLegacyTargets(FireTargetActor, SmokeTargetActor, OutTargets);
		return;
	}

	OutTargets = Targets;
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

	TArray<FTDFluidFragmentTarget> ActiveTargets;
	ResolveActiveTargets(ActiveTargets);
	TArray<int32> StampedLeafIndices;
	for (int32 TargetIndex = 0; TargetIndex < ActiveTargets.Num(); ++TargetIndex)
	{
		const FTDFluidFragmentTarget& Target = ActiveTargets[TargetIndex];
		TArray<FTDFluidBrushStamp> TargetStamps;
		if (Target.bReceivesTrails)
		{
			BuildTargetTrailStamps(Target, PreviousPositions, CurrentPositions, LaggedPositions, DeltaSeconds, static_cast<float>(BurstCenterWorld.Z), TrailSettings, TargetStamps, TargetIndex == 0 ? &StampedLeafIndices : nullptr);
		}

		TArray<FTDFluidBrushStamp> RestingStamps;
		BuildRestingHeatStamps(Target, PreviousPositions, CurrentPositions, DeltaSeconds, TrackingElapsedSeconds, TrailSettings, RestingStamps);
		TargetStamps.Append(RestingStamps);
		QueueStampsOnTarget(Target.TargetActor, TargetStamps);
	}
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
	QueueBurstOnTargets(RemainingRatio, TrailSettings.bSustainsBurstHeat ? 1.0f : RemainingRatio);
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
	TArray<FTDFluidFragmentTarget> LegacyTargets;
	BuildLegacyTargets(nullptr, nullptr, LegacyTargets);
	BuildTargetTrailStamps(LegacyTargets[0], PreviousPositions, CurrentPositions, LaggedPositions, DeltaTime, 0.0f, Settings, OutFireStamps, OutStampedLeafIndices);
	BuildTargetTrailStamps(LegacyTargets[1], PreviousPositions, CurrentPositions, LaggedPositions, DeltaTime, 0.0f, Settings, OutSmokeStamps);
}

void ATDFluidFragmentEmitterActor::BuildLegacyTargets(ATDFluidFireActor* FireTarget, ATDFluidLiveActor* SmokeTarget, TArray<FTDFluidFragmentTarget>& OutTargets)
{
	OutTargets.Reset();

	FTDFluidFragmentTarget& FireTrailTarget = OutTargets.AddDefaulted_GetRef();
	FireTrailTarget.TargetActor = FireTarget;
	FireTrailTarget.Channel = ETDFluidBrushChannel::ChannelB_Fire;
	FireTrailTarget.HeatScale = 1.0f;

	FTDFluidFragmentTarget& SmokeTrailTarget = OutTargets.AddDefaulted_GetRef();
	SmokeTrailTarget.TargetActor = SmokeTarget;
	SmokeTrailTarget.Channel = ETDFluidBrushChannel::ChannelR_Mud;
	SmokeTrailTarget.HeatScale = 1.0f;
	SmokeTrailTarget.BurstRadiusScale = SmokeBurstRadiusScale;
	SmokeTrailTarget.bUsesLaggedPositions = true;
}

float ATDFluidFragmentEmitterActor::ComputeTargetHeightWeight(const FTDFluidFragmentTarget& Target, double StampZ, double BurstCenterZ)
{
	if (Target.HeightFalloffCm <= 0.0f)
	{
		return 1.0f;
	}

	const double HeightAboveCenterCm = FMath::Max(StampZ - BurstCenterZ, 0.0);
	return FMath::Clamp(1.0f - static_cast<float>(HeightAboveCenterCm) / Target.HeightFalloffCm, 0.0f, 1.0f);
}

void ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(
	const FTDFluidFragmentTarget& Target,
	const TArray<FVector>& PreviousPositions,
	const TArray<FVector>& CurrentPositions,
	const TArray<FVector>& LaggedPositions,
	float DeltaTime,
	float BurstCenterZ,
	const FTDFluidFragmentTrailSettings& Settings,
	TArray<FTDFluidBrushStamp>& OutStamps,
	TArray<int32>* OutStampedLeafIndices)
{
	OutStamps.Reset();
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

	const bool bUsesLaggedPositions = Target.bUsesLaggedPositions;
	const float StampRadiusCm = (bUsesLaggedPositions ? Settings.SmokeStampRadiusCm : Settings.FireStampRadiusCm) * Target.RadiusScale;
	const float DensityStrength = (bUsesLaggedPositions ? Settings.SmokeDensityStrength : Settings.FireDensityStrength) * Target.DensityScale;
	const float HeatStrength = (bUsesLaggedPositions ? Settings.SmokeHeatStrength : Settings.FireHeatStrength) * Target.HeatScale;
	const int32 SelectedCount = FMath::Min(Candidates.Num(), FMath::Max(Settings.MaxFragmentsPerStep, 0));
	const int32 MaxStampsPerFragment = FMath::Max(Settings.MaxStampsPerFragment, 1);
	for (int32 CandidateIndex = 0; CandidateIndex < SelectedCount; ++CandidateIndex)
	{
		const int32 LeafIndex = Candidates[CandidateIndex].LeafIndex;
		const FVector& PreviousPosition = PreviousPositions[LeafIndex];
		const FVector& CurrentPosition = CurrentPositions[LeafIndex];
		const FVector StampVelocity = (CurrentPosition - PreviousPosition) / DeltaTime * Settings.VelocityInheritance;
		if (bUsesLaggedPositions)
		{
			const FVector& LaggedPosition = LaggedPositions[LeafIndex];
			const float HeightWeight = ComputeTargetHeightWeight(Target, LaggedPosition.Z, BurstCenterZ);
			if (!LaggedPosition.ContainsNaN() && HeightWeight > 0.0f)
			{
				FTDFluidBrushStamp& Stamp = OutStamps.Add_GetRef(MakeTrailStamp(LaggedPosition, StampVelocity, StampRadiusCm, DensityStrength * HeightWeight, Target.Channel));
				Stamp.HeatStrength = FMath::Clamp(HeatStrength * HeightWeight, 0.0f, 1.0f);
			}
		}
		else
		{
			const double DistanceCm = (CurrentPosition - PreviousPosition).Size();
			const int32 SubStepCount = FMath::Clamp(FMath::CeilToInt(DistanceCm / (FireStampSpacingRatio * StampRadiusCm)), 1, MaxStampsPerFragment);
			for (int32 SubStepIndex = 1; SubStepIndex <= SubStepCount; ++SubStepIndex)
			{
				const double Alpha = static_cast<double>(SubStepIndex) / static_cast<double>(SubStepCount);
				const FVector StampLocation = FMath::Lerp(PreviousPosition, CurrentPosition, Alpha);
				const float HeightWeight = ComputeTargetHeightWeight(Target, StampLocation.Z, BurstCenterZ);
				if (HeightWeight <= 0.0f)
				{
					continue;
				}

				FTDFluidBrushStamp& Stamp = OutStamps.Add_GetRef(MakeTrailStamp(StampLocation, StampVelocity, StampRadiusCm, DensityStrength * HeightWeight, Target.Channel));
				Stamp.HeatStrength = FMath::Clamp(HeatStrength * HeightWeight, 0.0f, 1.0f);
			}
		}

		if (OutStampedLeafIndices)
		{
			OutStampedLeafIndices->Add(LeafIndex);
		}
	}
}

void ATDFluidFragmentEmitterActor::BuildRestingHeatStamps(
	const FTDFluidFragmentTarget& Target,
	const TArray<FVector>& PreviousPositions,
	const TArray<FVector>& CurrentPositions,
	float DeltaTime,
	float ElapsedSeconds,
	const FTDFluidFragmentTrailSettings& Settings,
	TArray<FTDFluidBrushStamp>& OutStamps)
{
	OutStamps.Reset();
	const bool bIsRestingWindow = Settings.RestingHeatSeconds > 0.0f && ElapsedSeconds < Settings.RestingHeatSeconds;
	if (!bIsRestingWindow || Target.HeatScale <= 0.0f || DeltaTime <= UE_SMALL_NUMBER || CurrentPositions.Num() != PreviousPositions.Num())
	{
		return;
	}

	const float RemainingRatio = 1.0f - FMath::Max(ElapsedSeconds, 0.0f) / Settings.RestingHeatSeconds;
	const float HeatStrength = FMath::Clamp(Settings.RestingHeatStrength * RemainingRatio * Target.HeatScale, 0.0f, 1.0f);
	if (HeatStrength <= 0.0f)
	{
		return;
	}

	const float DensityStrength = Settings.RestingDensityStrength * RemainingRatio * Target.DensityScale;
	const float StampRadiusCm = Settings.RestingHeatRadiusCm * Target.RadiusScale;
	const int32 MaxRestingStampCount = FMath::Max(Settings.MaxFragmentsPerStep, 0);
	for (int32 LeafIndex = 0; LeafIndex < CurrentPositions.Num() && OutStamps.Num() < MaxRestingStampCount; ++LeafIndex)
	{
		const double SpeedCmPerSec = (CurrentPositions[LeafIndex] - PreviousPositions[LeafIndex]).Size() / DeltaTime;
		if (!FMath::IsFinite(SpeedCmPerSec) || SpeedCmPerSec >= Settings.MinFragmentSpeedCmPerSec || CurrentPositions[LeafIndex].ContainsNaN())
		{
			continue;
		}

		FTDFluidBrushStamp& Stamp = OutStamps.Add_GetRef(MakeTrailStamp(CurrentPositions[LeafIndex], FVector::ZeroVector, StampRadiusCm, DensityStrength, Target.Channel));
		Stamp.VelocityStrength = 0.0f;
		Stamp.HeatStrength = HeatStrength;
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
	BuildBurstStamps(CenterWorld, PlaneBasis, Settings, Channel, RadiusScale, DensityStrength, 0.0f, OutStamps);
}

void ATDFluidFragmentEmitterActor::BuildBurstStamps(
	const FVector& CenterWorld,
	const FTDFluidPlaneBasis& PlaneBasis,
	const FTDFluidFragmentTrailSettings& Settings,
	ETDFluidBrushChannel Channel,
	float RadiusScale,
	float DensityStrength,
	float HeatStrength,
	TArray<FTDFluidBrushStamp>& OutStamps)
{
	OutStamps.Reset();

	FTDFluidBrushStamp CoreStamp = MakeTrailStamp(CenterWorld, FVector::ZeroVector, Settings.BurstCoreRadiusCm * RadiusScale, DensityStrength, Channel);
	CoreStamp.HeatStrength = HeatStrength;
	CoreStamp.VelocityStrength = 0.0f;
	OutStamps.Add(CoreStamp);

	const int32 RingStampCount = FMath::Max(Settings.BurstRingStampCount, 0);
	for (int32 RingIndex = 0; RingIndex < RingStampCount; ++RingIndex)
	{
		const double Angle = UE_DOUBLE_TWO_PI * static_cast<double>(RingIndex) / static_cast<double>(RingStampCount);
		const FVector Outward = PlaneBasis.U * FMath::Cos(Angle) + PlaneBasis.V * FMath::Sin(Angle);
		FTDFluidBrushStamp& RingStamp = OutStamps.Add_GetRef(MakeTrailStamp(
			CenterWorld + Outward * Settings.BurstRingRadiusCm,
			Outward * Settings.BurstRingSpeedCmPerSec,
			Settings.BurstRingRadiusCm * BurstRingStampRadiusRatio * RadiusScale,
			DensityStrength,
			Channel));
		RingStamp.HeatStrength = HeatStrength;
	}
}
