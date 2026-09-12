#include "World/Generation/TDExclusionVolume.h"

#include "Components/SceneComponent.h"

ATDExclusionVolume::ATDExclusionVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Tags.Add(TEXT("TDExclusion"));
}
