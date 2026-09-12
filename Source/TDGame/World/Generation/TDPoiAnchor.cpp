#include "World/Generation/TDPoiAnchor.h"

#include "Components/SceneComponent.h"

ATDPoiAnchor::ATDPoiAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Tags.Add(TEXT("TDPoi"));
}
