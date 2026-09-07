#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Combat/TDDamageTypes.h"
#include "TDDamageSubsystem.generated.h"

class ATDDamageEntity;
class UTDCombatComponent;

UCLASS()
class TDGAME_API UTDDamageSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	ATDDamageEntity* Cast(UTDDamageDefinition* Definition, AActor* Caster, const FVector& Origin, const FVector& Target);
	ATDDamageEntity* SpawnEntity(UTDDamageDefinition* Definition, const FTDDamageContext& Context, const FVector& Location);
	void ExecuteRules(const TArray<FTDDamageRule>& Rules, ETDDamageEvent Event, const FTDDamageContext& Context, AActor* Target, const FVector& Location, ATDDamageEntity* SourceEntity = nullptr);
	void ExecuteScheduledAction(const FTDDamageAction& Action, const TArray<FTDDamageRule>& Rules, ETDDamageEvent Event, const FTDDamageContext& Context, AActor* Target, const FVector& Location, ATDDamageEntity* SourceEntity);
	void RegisterCombatant(UTDCombatComponent* Combatant);
	void UnregisterCombatant(UTDCombatComponent* Combatant);
	void GatherTargets(const FVector& Center, float Radius, float HalfHeight, const FTDDamageContext& Context, ETDDamageTargetPolicy Policy, TArray<UTDCombatComponent*>& OutTargets) const;
	bool CanTarget(const UTDCombatComponent* Target, const FTDDamageContext& Context, ETDDamageTargetPolicy Policy) const;
	virtual void Deinitialize() override;

private:
	void ExecuteAction(const FTDDamageAction& Action, const TArray<FTDDamageRule>& Rules, ETDDamageEvent Event, const FTDDamageContext& Context, AActor* Target, const FVector& Location, ATDDamageEntity* SourceEntity);
	TSet<TWeakObjectPtr<UTDCombatComponent>> Combatants;
	int32 ExecutionDepth = 0;
};
