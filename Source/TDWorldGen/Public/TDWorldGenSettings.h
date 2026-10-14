#pragma once

#include "Engine/DeveloperSettings.h"
#include "TDWorldGenSettings.generated.h"

class UTDDungeonAtlasDefinition;
class UTDRealmDefinition;
class UTDWorldDefinition;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "TD World Generation"))
class TDWORLDGEN_API UTDWorldGenSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Dungeon")
	TSoftObjectPtr<UTDDungeonAtlasDefinition> DefaultDungeonAtlas;

	UPROPERTY(Config, EditAnywhere, Category = "World")
	TSoftObjectPtr<UTDWorldDefinition> DefaultWorldDefinition;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = "1.0"))
	float SeamlessTravelTimeoutSeconds = 20.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = "0.0"))
	float TravelFadeOutSeconds = 0.35f;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = "0.0"))
	float TravelFadeInSeconds = 0.6f;

	UPROPERTY(Config, EditAnywhere, Category = "Streaming", meta = (ClampMin = "0.0"))
	float RealmDepartureHoldSeconds = 10.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Realm")
	TArray<TSoftObjectPtr<UTDRealmDefinition>> RealmDefinitions;

	static const UTDWorldGenSettings* Get();
};
