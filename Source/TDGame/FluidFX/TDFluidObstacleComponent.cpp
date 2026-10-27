#include "FluidFX/TDFluidObstacleComponent.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "GameFramework/Actor.h"

UTDFluidObstacleComponent::UTDFluidObstacleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FTDFluidSdfObstacle UTDFluidObstacleComponent::MakeOrientedBoxObstacle(const FVector& WorldCenter, const FVector& HalfExtentsCm, const FRotator& WorldRotation)
{
	const FRotationMatrix RotationMatrix(WorldRotation);
	const FVector AxisX = RotationMatrix.GetScaledAxis(EAxis::X);
	const FVector AxisY = RotationMatrix.GetScaledAxis(EAxis::Y);
	const FVector AxisZ = RotationMatrix.GetScaledAxis(EAxis::Z);
	const double WorldHalfX = FMath::Abs(AxisX.X) * HalfExtentsCm.X + FMath::Abs(AxisY.X) * HalfExtentsCm.Y + FMath::Abs(AxisZ.X) * HalfExtentsCm.Z;
	const double WorldHalfY = FMath::Abs(AxisX.Y) * HalfExtentsCm.X + FMath::Abs(AxisY.Y) * HalfExtentsCm.Y + FMath::Abs(AxisZ.Y) * HalfExtentsCm.Z;

	FTDFluidSdfObstacle Obstacle;
	Obstacle.WorldCenter = WorldCenter;
	Obstacle.bIsBox = true;
	Obstacle.RadiusCm = static_cast<float>(HalfExtentsCm.GetMax());
	Obstacle.BoxHalfExtentsCm = FVector2D(WorldHalfX, WorldHalfY);
	Obstacle.BoxHalfExtents3DCm = HalfExtentsCm;
	Obstacle.WorldRotation = WorldRotation;
	return Obstacle;
}

bool UTDFluidObstacleComponent::BuildObstacle(FTDFluidSdfObstacle& OutObstacle) const
{
	const AActor* Owner = GetOwner();
	const USceneComponent* RootComponent = Owner ? Owner->GetRootComponent() : nullptr;
	if (!RootComponent)
	{
		return false;
	}

	FVector LocalCenter = BoxOffsetCm;
	FVector LocalHalfExtents = BoxHalfExtentsCm;
	if (bUseOwnerBounds)
	{
		const FBoxSphereBounds LocalBounds = RootComponent->CalcBounds(FTransform::Identity);
		LocalCenter = LocalBounds.Origin + BoxOffsetCm;
		LocalHalfExtents = LocalBounds.BoxExtent;
	}

	const FTransform& RootTransform = RootComponent->GetComponentTransform();
	const FVector WorldHalfExtents = LocalHalfExtents * RootTransform.GetScale3D().GetAbs();
	if (WorldHalfExtents.GetMax() <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutObstacle = MakeOrientedBoxObstacle(RootTransform.TransformPosition(LocalCenter), WorldHalfExtents, RootTransform.Rotator());
	return true;
}

void UTDFluidObstacleComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	if (FluidSubsystem)
	{
		FluidSubsystem->RegisterObstacleSource(this);
	}

	AActor* Owner = GetOwner();
	USceneComponent* RootComponent = Owner ? Owner->GetRootComponent() : nullptr;
	if (!RootComponent)
	{
		return;
	}

	BoundRootComponent = RootComponent;
	RootTransformUpdatedHandle = RootComponent->TransformUpdated.AddUObject(this, &UTDFluidObstacleComponent::HandleRootTransformUpdated);
}

void UTDFluidObstacleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USceneComponent* RootComponent = BoundRootComponent.Get())
	{
		RootComponent->TransformUpdated.Remove(RootTransformUpdatedHandle);
	}
	BoundRootComponent.Reset();
	RootTransformUpdatedHandle.Reset();

	UWorld* World = GetWorld();
	UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	if (FluidSubsystem)
	{
		FluidSubsystem->UnregisterObstacleSource(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UTDFluidObstacleComponent::HandleRootTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	MarkFluidObstaclesDirty();
}

void UTDFluidObstacleComponent::MarkFluidObstaclesDirty() const
{
	UWorld* World = GetWorld();
	UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	if (FluidSubsystem)
	{
		FluidSubsystem->MarkObstacleSourcesDirty();
	}
}
