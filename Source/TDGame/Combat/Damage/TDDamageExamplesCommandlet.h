#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "TDDamageExamplesCommandlet.generated.h"

UCLASS()
class TDGAME_API UTDDamageExamplesCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UTDDamageExamplesCommandlet();
	virtual int32 Main(const FString& Params) override;
};
