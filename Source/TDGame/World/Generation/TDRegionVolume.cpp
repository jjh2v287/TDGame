#include "World/Generation/TDRegionVolume.h"

#include "Components/BoxComponent.h"

ATDRegionVolume::ATDRegionVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	RegionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RegionBounds"));
	RegionBounds->SetBoxExtent(FVector(5000.0f, 5000.0f, 2000.0f));
	RegionBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RegionBounds->SetGenerateOverlapEvents(false);
	SetRootComponent(RegionBounds);
	Tags.Add(TEXT("TDRegion"));
}
