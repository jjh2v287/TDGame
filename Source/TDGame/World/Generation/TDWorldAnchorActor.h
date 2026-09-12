#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TDWorldTypes.h"
#include "TDWorldAnchorActor.generated.h"

UCLASS()
class TDGAME_API ATDWorldAnchorActor : public AActor
{
	GENERATED_BODY()

public:
	ATDWorldAnchorActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Anchor")
	FName AnchorId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Anchor")
	ETDWorldAnchorKind Kind = ETDWorldAnchorKind::Landmark;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Anchor", meta = (ClampMin = "0"))
	float ExclusionRadiusCm = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Anchor")
	bool bLocked = true;
};
