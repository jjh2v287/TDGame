#include "FluidFX/TDFluidHullWakeComponent.h"
#include "FluidFX/TDFluidHullWakeRules.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "GameFramework/Actor.h"

UTDFluidHullWakeComponent::UTDFluidHullWakeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UTDFluidHullWakeComponent::BeginPlay()
{
	Super::BeginPlay();
	PreviousHullCenter.Reset();
	CacheOwnerLocalBounds();
	if (!bDrivesWaterWindow || !TargetWaterActor)
	{
		return;
	}

	TargetWaterActor->bShouldFollowPlayerPawn = true;
	TargetWaterActor->FollowTargetActor = GetOwner();
}

void UTDFluidHullWakeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	QueueHullStamps(DeltaTime);
}

void UTDFluidHullWakeComponent::CacheOwnerLocalBounds()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FBox LocalBounds = Owner->CalculateComponentsBoundingBoxInLocalSpace(true);
	if (!LocalBounds.IsValid)
	{
		return;
	}

	LocalHullBoundsCenter = LocalBounds.GetCenter();
	LocalHullBoundsSize = LocalBounds.GetSize();
}

void UTDFluidHullWakeComponent::QueueHullStamps(float DeltaTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !TargetWaterActor || !TargetWaterActor->LiveComponent || DeltaTime <= 0.0f)
	{
		return;
	}

	FTransform HullTransform = Owner->GetActorTransform();
	const FVector HullCenter = HullTransform.TransformPosition(LocalHullBoundsCenter);
	const TOptional<FVector> LastHullCenter = PreviousHullCenter;
	PreviousHullCenter = HullCenter;
	if (!LastHullCenter.IsSet())
	{
		return;
	}

	const FVector HullVelocityCmPerSec = (HullCenter - LastHullCenter.GetValue()) / DeltaTime;
	const FVector2D HullLengthAndBeam = FTDFluidHullWakeRules::ResolveHullLengthAndBeam(LocalHullBoundsSize * HullTransform.GetScale3D(), HullLengthCm, HullBeamCm);
	HullTransform.SetLocation(HullCenter);
	UTDFluidLiveComponent& WaterLiveComponent = *TargetWaterActor->LiveComponent;
	FTDFluidHullWakeRules::BuildHullStamps(HullTransform, HullVelocityCmPerSec, static_cast<float>(HullLengthAndBeam.X), static_cast<float>(HullLengthAndBeam.Y), TargetWaterActor->InteractionBrushChannel, WaterLiveComponent.GetInputBrushSettings(), HullStamps);
	for (const FTDFluidBrushStamp& Stamp : HullStamps)
	{
		WaterLiveComponent.QueueBrushStamp(Stamp);
	}
}
