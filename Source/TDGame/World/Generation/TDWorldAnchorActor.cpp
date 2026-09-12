#include "World/Generation/TDWorldAnchorActor.h"

#include "Components/SceneComponent.h"

ATDWorldAnchorActor::ATDWorldAnchorActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Tags.Add(TEXT("TDWorldAnchor"));
}
