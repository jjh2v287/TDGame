#include "AI/NPC/TDSignificanceComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UTDSignificanceComponent::UTDSignificanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTDSignificanceComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bIsActivated)
	{
		return;
	}

	OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		return;
	}

	USignificanceManager* SignificanceManager = USignificanceManager::Get(GetWorld());
	if (!SignificanceManager)
	{
		return;
	}

	auto SignificanceLambda = [this](USignificanceManager::FManagedObjectInfo* ObjectInfo, const FTransform& ViewPoint)
	{
		return CalculateSignificance(ObjectInfo, ViewPoint);
	};

	auto PostSignificanceLambda = [this](USignificanceManager::FManagedObjectInfo* ObjectInfo, const float OldSignificance, const float Significance, const bool bIsFinal)
	{
		OnPostSignificance(ObjectInfo, OldSignificance, Significance, bIsFinal);
	};

	SignificanceManager->RegisterObject(
		OwnerActor,
		TEXT("TD.NPC"),
		MoveTemp(SignificanceLambda),
		USignificanceManager::EPostSignificanceType::Sequential,
		MoveTemp(PostSignificanceLambda));
}

void UTDSignificanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (OwnerActor)
	{
		if (USignificanceManager* SignificanceManager = USignificanceManager::Get(GetWorld()))
		{
			SignificanceManager->UnregisterObject(OwnerActor);
		}
	}

	Super::EndPlay(EndPlayReason);
}

float UTDSignificanceComponent::CalculateSignificance(
	USignificanceManager::FManagedObjectInfo* ObjectInfo,
	const FTransform& ViewPoint)
{
	if (!OwnerActor)
	{
		return 0.f;
	}

	const FVector Direction = OwnerActor->GetActorLocation() - ViewPoint.GetLocation();
	CurrentDistance = Direction.Size();

	if (bUseViewDirectionPenalty)
	{
		const float Dot = FVector::DotProduct(ViewPoint.GetRotation().GetForwardVector(), Direction.GetSafeNormal());
		if (Dot < 0.f)
		{
			CurrentDistance *= 2.5f;
		}
	}

	return ResolveSignificanceByDistance(CurrentDistance);
}

void UTDSignificanceComponent::OnPostSignificance(
	USignificanceManager::FManagedObjectInfo* ObjectInfo,
	const float OldSignificance,
	const float Significance,
	const bool bIsFinal)
{
	CurrentSignificance = Significance;
}

float UTDSignificanceComponent::ResolveSignificanceByDistance(const float Distance) const
{
	if (Thresholds.IsEmpty())
	{
		return 0.f;
	}

	for (const FTDSignificanceThreshold& Threshold : Thresholds)
	{
		if (Distance <= Threshold.MaxDistance)
		{
			return Threshold.Significance;
		}
	}

	return Thresholds.Last().Significance;
}
