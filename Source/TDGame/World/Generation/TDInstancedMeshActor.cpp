#include "World/Generation/TDInstancedMeshActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"

ATDInstancedMeshActor::ATDInstancedMeshActor()
{
	PrimaryActorTick.bCanEverTick = false;
	InstancedMeshComponent = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("InstancedMeshComponent"));
	InstancedMeshComponent->SetMobility(EComponentMobility::Static);
	SetRootComponent(InstancedMeshComponent);
}
