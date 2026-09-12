#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TDBudgetTickable.generated.h"

UINTERFACE()
class TDGAME_API UTDBudgetTickable : public UInterface
{
	GENERATED_BODY()
};

class TDGAME_API ITDBudgetTickable
{
	GENERATED_BODY()

public:
	virtual void OnBudgetTickManagedStateChanged(bool bIsManaged) {}
	virtual void BudgetTick(float DeltaTimeSinceLastUpdate) = 0;
	virtual bool CanBudgetTick() const { return true; }
	virtual float GetBudgetImportanceBias() const { return 0.f; }
};
