#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TDNPCUpdatable.generated.h"

UINTERFACE(MinimalAPI)
class UTDNPCUpdatable : public UInterface
{
	GENERATED_BODY()
};

class TDGAME_API ITDNPCUpdatable
{
	GENERATED_BODY()

public:
	virtual void SetManagedByNPCUpdateSubsystem(bool bIsManaged) = 0;
	virtual void ManualUpdateMovement(float DeltaTime) = 0;
	virtual void ManualUpdateAnimation(float DeltaTime) = 0;
};
