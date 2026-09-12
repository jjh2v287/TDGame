#pragma once

#include "GameFramework/Actor.h"
#include "TDInstancedMeshActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

UCLASS()
class TDGAME_API ATDInstancedMeshActor : public AActor
{
	GENERATED_BODY()

public:
	ATDInstancedMeshActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> InstancedMeshComponent;
};
