#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "MonsterAI/TDMonsterDefinition.h"
#include "TDMonsterDefinitionLibrary.generated.h"

class UTDDamageDefinition;

using FTDMonsterSpellResolver = TFunctionRef<UTDDamageDefinition*(FName)>;

UCLASS()
class TDGAME_API UTDMonsterDefinitionLibrary : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UTDMonsterDefinitionLibrary* Get();
	static FString GetDefinitionDirectory();
	static void ResolveDefinitionTexts(const TMap<FString, FString>& FileTexts, FTDMonsterSpellResolver SpellResolver, TMap<FName, TSharedPtr<const FTDResolvedMonsterDefinition>>& OutDefinitions, TArray<FString>& OutErrors);

	TSharedPtr<const FTDResolvedMonsterDefinition> FindDefinition(FName DefinitionId);
	int32 ReloadDefinitions();
	const TArray<FString>& GetLoadErrors() const { return LoadErrors; }
	TArray<FName> GetDefinitionIds();
	UTDDamageDefinition* FindSpell(FName SpellName);

private:
	void EnsureLoaded();
	void EnsureAttackCatalog();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTDDamageDefinition>> OwnedSpells;

	TMap<FName, UTDDamageDefinition*> SpellsByName;
	TMap<FName, TSharedPtr<const FTDResolvedMonsterDefinition>> Definitions;
	TArray<FString> LoadErrors;
	bool bHasLoaded = false;
	IConsoleObject* ReloadCommand = nullptr;
};
