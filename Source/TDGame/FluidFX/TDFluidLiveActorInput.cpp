#include "FluidFX/TDFluidLiveActor.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidInputActorFilter.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "GameFramework/PlayerController.h"

namespace
{
	constexpr int32 HarvestMultiTouchFingerCount = 10;

	bool HarvestIsStampableVelocity(const FVector& Velocity, float MinSpeedCmPerSec, float TeleportSpeedCmPerSec)
	{
		const float PlanarSpeed = Velocity.Size2D();
		return PlanarSpeed >= MinSpeedCmPerSec && PlanarSpeed <= TeleportSpeedCmPerSec;
	}

	void HarvestApplyActorBrushSettings(FTDFluidBrushStamp& Stamp, const FTDFluidSimulationConfig& Config)
	{
		Stamp.DensityStrength *= Config.ActorBrushDensityStrength;
		Stamp.RadialVelocityCmPerSec = Config.ActorBrushPunctureCmPerSec;
		Stamp.RippleImpulseCm = Config.ActorBrushRippleImpulseCm;
	}
}

void ATDFluidLiveActor::QueuePointerBrushAt(int32 PointerIndex, const FVector& PointerWorldLocation, float DeltaTime)
{
	if (!LiveComponent || DeltaTime <= 0.0f)
	{
		return;
	}

	FVector2D PointerPlanePoint;
	if (!LiveComponent->TryProjectWorldPointIntoSlab(PointerWorldLocation, PointerPlanePoint))
	{
		ForgetPointerStroke(PointerIndex);
		return;
	}

	FVector PreviousLocation;
	if (!AdvancePointerStroke(PointerIndex, PointerWorldLocation, PreviousLocation))
	{
		return;
	}

	const FVector PointerDelta = PointerWorldLocation - PreviousLocation;
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent->GetPlaneBasis();
	const FVector2D PlaneDelta = PlaneBasis.ProjectVector(PointerDelta);
	const FVector2D PlaneVelocity(PlaneDelta.X / DeltaTime, PlaneDelta.Y / DeltaTime);
	const FVector PlanarVelocity(PlaneVelocity.X, PlaneVelocity.Y, 0.0f);
	const FTDFluidInputBrushSettings& InputBrushSettings = LiveComponent->GetInputBrushSettings();
	if (!HarvestIsStampableVelocity(PlanarVelocity, MinInteractionSpeedCmPerSec, InputBrushSettings.TeleportSpeedCmPerSec))
	{
		return;
	}

	FTDFluidBrushStamp PointerStamp;
	PointerStamp.WorldLocation = PointerWorldLocation;
	PointerStamp.WorldVelocity = PlaneBasis.UnprojectVector(PlaneVelocity);
	PointerStamp.RadiusCm = MouseBrushRadiusCm;
	PointerStamp.DensityStrength = InputBrushSettings.MouseBrushDensityStrength;
	PointerStamp.VelocityStrength = 1.0f;
	PointerStamp.Channel = InteractionBrushChannel;
	PointerStamp.Source = ETDFluidBrushSource::Pointer;
	HarvestApplyActorBrushSettings(PointerStamp, LiveComponent->SimulationConfig);
	RecordLingeringInteractionStamp(FTDFluidInputSourceKey{FObjectKey(), NAME_None, PointerIndex}, PointerStamp);
	if (InputBrushSettings.bInterpolatePivotAndPointerStrokes)
	{
		QueueTrackStroke(PointerStamp, PreviousLocation, InputBrushSettings, FTDFluidTrackGroundPlane(), true);
		return;
	}

	LiveComponent->QueueBrushStamp(PointerStamp);
}

bool ATDFluidLiveActor::AdvancePointerStroke(int32 PointerIndex, const FVector& PointerWorldLocation, FVector& OutPreviousLocation)
{
	if (PointerIndex < 0)
	{
		const bool bHadPreviousLocation = bHasPreviousMouseBrushLocation;
		OutPreviousLocation = PreviousMouseBrushLocation;
		PreviousMouseBrushLocation = PointerWorldLocation;
		bHasPreviousMouseBrushLocation = true;
		return bHadPreviousLocation;
	}

	const FVector* StoredLocation = PreviousPointerLocations.Find(PointerIndex);
	const bool bHadPreviousLocation = StoredLocation != nullptr;
	OutPreviousLocation = bHadPreviousLocation ? *StoredLocation : PointerWorldLocation;
	PreviousPointerLocations.Add(PointerIndex, PointerWorldLocation);
	return bHadPreviousLocation;
}

void ATDFluidLiveActor::ForgetPointerStroke(int32 PointerIndex)
{
	if (PointerIndex < 0)
	{
		ResetMouseBrushStroke();
		return;
	}

	PreviousPointerLocations.Remove(PointerIndex);
}

ETDFluidPointerInputMode ATDFluidLiveActor::ResolveEffectivePointerInputMode() const
{
	if (PointerInputMode != ETDFluidPointerInputMode::Off)
	{
		return PointerInputMode;
	}
	return bAcceptMouseBrush ? ETDFluidPointerInputMode::Mouse : ETDFluidPointerInputMode::Off;
}

void ATDFluidLiveActor::PollPointerInput(float DeltaTime)
{
	const ETDFluidPointerInputMode EffectivePointerMode = ResolveEffectivePointerInputMode();
	if (EffectivePointerMode == ETDFluidPointerInputMode::Off)
	{
		ResetMouseBrushStroke();
		PreviousPointerLocations.Reset();
		return;
	}

	const APlayerController* Controller = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	if (EffectivePointerMode != ETDFluidPointerInputMode::Mouse)
	{
		ResetMouseBrushStroke();
		PollTouchPointers(Controller, EffectivePointerMode, DeltaTime);
		return;
	}

	if (!Controller || !Controller->IsInputKeyDown(MouseBrushKey))
	{
		ResetMouseBrushStroke();
		return;
	}

	FVector CursorWorldLocation;
	if (!ResolveMouseBrushWorldLocation(*Controller, CursorWorldLocation))
	{
		ResetMouseBrushStroke();
		return;
	}

	QueuePointerBrushAt(-1, CursorWorldLocation, DeltaTime);
}

void ATDFluidLiveActor::PollTouchPointers(const APlayerController* Controller, ETDFluidPointerInputMode EffectivePointerMode, float DeltaTime)
{
	if (!Controller)
	{
		PreviousPointerLocations.Reset();
		return;
	}

	const int32 FingerCount = EffectivePointerMode == ETDFluidPointerInputMode::MultiTouch ? HarvestMultiTouchFingerCount : 1;
	for (int32 FingerIndex = 0; FingerIndex < FingerCount; ++FingerIndex)
	{
		float ScreenX = 0.0f;
		float ScreenY = 0.0f;
		bool bIsPressed = false;
		Controller->GetInputTouchState(static_cast<ETouchIndex::Type>(ETouchIndex::Touch1 + FingerIndex), ScreenX, ScreenY, bIsPressed);
		FVector TouchWorldLocation;
		if (!bIsPressed || !ResolveTouchWorldLocation(*Controller, FVector2D(ScreenX, ScreenY), TouchWorldLocation))
		{
			PreviousPointerLocations.Remove(FingerIndex);
			continue;
		}

		QueuePointerBrushAt(FingerIndex, TouchWorldLocation, DeltaTime);
	}
}

bool ATDFluidLiveActor::ResolveTouchWorldLocation(const APlayerController& Controller, const FVector2D& ScreenPosition, FVector& OutLocation) const
{
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent->GetPlaneBasis();
	if (PlaneBasis.IsWorldFacing())
	{
		FHitResult TouchHit;
		if (!Controller.GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, false, TouchHit))
		{
			return false;
		}

		OutLocation = TouchHit.ImpactPoint;
		return true;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!Controller.DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, RayOrigin, RayDirection))
	{
		return false;
	}

	return PlaneBasis.IntersectRay(RayOrigin, RayDirection, LiveComponent->GetPlaneDepthCm(), OutLocation);
}

void ATDFluidLiveActor::EmitLingeringInteractionStamps(float DeltaTime)
{
	const float PersistenceSeconds = LiveComponent ? LiveComponent->GetInputBrushSettings().InputPersistenceSeconds : 0.0f;
	if (PersistenceSeconds <= 0.0f)
	{
		if (InputPersistence.GetLingeringCount() > 0)
		{
			InputPersistence.Reset();
		}
		return;
	}

	TArray<FTDFluidBrushStamp> LingeringStamps;
	InputPersistence.EmitLingering(DeltaTime, PersistenceSeconds, LingeringStamps);
	for (const FTDFluidBrushStamp& LingeringStamp : LingeringStamps)
	{
		LiveComponent->QueueBrushStamp(LingeringStamp);
	}
}

void ATDFluidLiveActor::RecordLingeringInteractionStamp(const FTDFluidInputSourceKey& SourceKey, const FTDFluidBrushStamp& Stamp)
{
	if (LiveComponent->GetInputBrushSettings().InputPersistenceSeconds <= 0.0f)
	{
		return;
	}

	InputPersistence.RecordEmitted(SourceKey, Stamp);
}

AActor* ATDFluidLiveActor::ResolveInteractionOwnerCandidate() const
{
	if (!bInteractWithOwner)
	{
		return nullptr;
	}

	AActor* ParentActor = GetAttachParentActor();
	return ParentActor ? ParentActor : GetOwner();
}

void ATDFluidLiveActor::HarvestOwnerInteraction(AActor* OwnerCandidate, float DeltaTime)
{
	if (!OwnerCandidate || OwnerCandidate->IsA<ATDFluidLiveActor>())
	{
		return;
	}

	const FBox OwnerBounds = ResolveHarvestBounds(*OwnerCandidate);
	if (!PassesHarvestFilter(*OwnerCandidate, false, OwnerBounds))
	{
		return;
	}

	HarvestActorInteractionStamps(OwnerCandidate, DeltaTime, OwnerBounds, true);
}

FBox ATDFluidLiveActor::ResolveHarvestBounds(const AActor& Candidate) const
{
	const FTDFluidInputBrushSettings& Settings = LiveComponent->GetInputBrushSettings();
	const bool bNeedsBounds = Settings.ObjectSizeMode == ETDFluidObjectSizeMode::Bounds || Settings.LargeObjectExcludeRatio > 0.0f || Settings.bMovePivotToBoundsBottom;
	if (!bNeedsBounds)
	{
		return FBox(ForceInit);
	}

	FVector BoundsOrigin;
	FVector BoundsExtent;
	Candidate.GetActorBounds(true, BoundsOrigin, BoundsExtent);
	return FBox(BoundsOrigin - BoundsExtent, BoundsOrigin + BoundsExtent);
}

bool ATDFluidLiveActor::PassesHarvestFilter(const AActor& Candidate, bool bIsOverlapCandidate, const FBox& CandidateBounds) const
{
	FTDFluidInputActorFilterRules Rules;
	Rules.ObjectTypes = InteractionObjectTypes;
	Rules.Tags = TrackedActorTags;
	Rules.ExcludedActors = ExcludedActors;
	Rules.LargeObjectExcludeRatio = LiveComponent->GetInputBrushSettings().LargeObjectExcludeRatio;
	if (!FTDFluidInputActorFilter::HasActiveRules(Rules))
	{
		return true;
	}

	Rules.WindowExtentCm = LiveComponent->GetSimulationExtentCm();
	bool bIsExplicit = !bIsOverlapCandidate;
	for (const TWeakObjectPtr<AActor>& RegisteredActor : RegisteredInteractingActors)
	{
		bIsExplicit |= RegisteredActor.Get() == &Candidate;
	}
	return FTDFluidInputActorFilter::ShouldHarvest(Candidate, bIsExplicit, CandidateBounds.GetExtent(), CandidateBounds.IsValid != 0, Rules);
}

FVector ATDFluidLiveActor::ResolveHarvestPivotLocation(const AActor& TargetActor, const FBox& TargetBounds) const
{
	if (!TargetBounds.IsValid || !LiveComponent->GetInputBrushSettings().bMovePivotToBoundsBottom)
	{
		return TargetActor.GetActorLocation();
	}

	const FVector BoundsCenter = TargetBounds.GetCenter();
	return FVector(BoundsCenter.X, BoundsCenter.Y, TargetBounds.Min.Z);
}

void ATDFluidLiveActor::ApplyHarvestObjectSize(FTDFluidBrushStamp& Stamp, const FVector& WorldScale, const FBox* TargetBounds) const
{
	const FTDFluidInputBrushSettings& Settings = LiveComponent->GetInputBrushSettings();
	if (Settings.ObjectSizeMode == ETDFluidObjectSizeMode::Fixed && !Settings.bHardnessScalesWithObjectSize)
	{
		return;
	}

	const float FixedRadiusCm = Stamp.RadiusCm;
	float ResolvedRadiusCm = FixedRadiusCm;
	const bool bUsesBounds = Settings.ObjectSizeMode == ETDFluidObjectSizeMode::Bounds && TargetBounds && TargetBounds->IsValid;
	if (bUsesBounds)
	{
		const FVector BoundsExtent = TargetBounds->GetExtent();
		const double LargestExtentCm = FMath::Max(BoundsExtent.X, BoundsExtent.Y);
		ResolvedRadiusCm = LargestExtentCm > UE_KINDA_SMALL_NUMBER ? static_cast<float>(LargestExtentCm) * Settings.ObjectSizeRadiusRatio : FixedRadiusCm;
	}
	else if (Settings.ObjectSizeMode != ETDFluidObjectSizeMode::Fixed)
	{
		ResolvedRadiusCm = FixedRadiusCm * static_cast<float>(FMath::Max(FMath::Abs(WorldScale.X), FMath::Abs(WorldScale.Y)));
	}

	Stamp.RadiusCm = ResolvedRadiusCm;
	if (Settings.bHardnessScalesWithObjectSize && FixedRadiusCm > UE_KINDA_SMALL_NUMBER)
	{
		Stamp.ShapeHardness = FMath::Clamp(Settings.BrushHardness * (ResolvedRadiusCm / FixedRadiusCm), 0.0f, 0.95f);
	}
}
