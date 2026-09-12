#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TDPersistentActor.generated.h"

USTRUCT(BlueprintType)
struct FTDActorStateRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	FGuid StableId;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	TMap<FName, int32> IntValues;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	TMap<FName, bool> BoolValues;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	TMap<FName, float> FloatValues;

	bool GetBool(FName Key, bool bDefaultValue = false) const
	{
		const bool* Found = BoolValues.Find(Key);
		return Found ? *Found : bDefaultValue;
	}

	int32 GetInt(FName Key, int32 DefaultValue = 0) const
	{
		const int32* Found = IntValues.Find(Key);
		return Found ? *Found : DefaultValue;
	}

	float GetFloat(FName Key, float DefaultValue = 0.0f) const
	{
		const float* Found = FloatValues.Find(Key);
		return Found ? *Found : DefaultValue;
	}
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTDPersistentActor : public UInterface
{
	GENERATED_BODY()
};

class ITDPersistentActor
{
	GENERATED_BODY()

public:
	virtual void WriteState(FTDActorStateRecord& OutRecord) const = 0;
	virtual void ReadState(const FTDActorStateRecord& Record) = 0;
};
