#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TDWorldTypes.h"
#include "TDPoiAnchor.generated.h"

UCLASS()
class ATDPoiAnchor : public AActor
{
	GENERATED_BODY()

public:
	ATDPoiAnchor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Poi")
	FName PoiId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Poi")
	ETDPoiKind Kind = ETDPoiKind::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Poi", meta = (ClampMin = "0"))
	float ExclusionRadiusCm = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Poi")
	bool bLocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Poi")
	FGuid StableId;
};
