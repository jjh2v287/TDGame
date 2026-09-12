#include "World/Generation/TDRoadSplineActor.h"

#include "Components/SplineComponent.h"

ATDRoadSplineActor::ATDRoadSplineActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	Spline->SetMobility(EComponentMobility::Static);
	SetRootComponent(Spline);
	Tags.Add(TEXT("TDRoad"));
}

void ATDRoadSplineActor::SetRoadPoints(const TArray<FVector>& WorldPointsCm)
{
	if (!Spline)
	{
		return;
	}
	Spline->ClearSplinePoints(false);
	for (int32 Index = 0; Index < WorldPointsCm.Num(); ++Index)
	{
		Spline->AddSplinePoint(WorldPointsCm[Index], ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(Index, ESplinePointType::CurveClamped, false);
	}
	Spline->SetClosedLoop(false, false);
	Spline->UpdateSpline();
}
