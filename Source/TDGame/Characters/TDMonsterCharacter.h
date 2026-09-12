#pragma once

#include "CoreMinimal.h"
#include "AI/NPC/TDNPCUpdatable.h"
#include "Characters/TDCombatCharacter.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "TDMonsterCharacter.generated.h"

class UTDCombatTokenSubsystem;
class UTDSignificanceComponent;
class UTDSkillComponent;
struct FTDDamageContext;

UCLASS()
class TDGAME_API ATDMonsterCharacter : public ATDCombatCharacter, public IGenericTeamAgentInterface, public ITDNPCUpdatable
{
	GENERATED_BODY()

public:
	ATDMonsterCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual void SetManagedByNPCUpdateSubsystem(bool bIsManaged) override;
	virtual void ManualUpdateMovement(float DeltaTime) override;
	virtual void ManualUpdateAnimation(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category="Combat")
	void SetCurrentTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category="Combat")
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	UFUNCTION(BlueprintPure, Category="Combat")
	UTDSkillComponent* GetSkillComponent() const { return SkillComponent; }

	UFUNCTION(BlueprintCallable, Category="Combat")
	bool ExecutePrimaryAttack(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="Combat")
	bool ExecuteCombatAction(FGameplayTag ActionTag, AActor* TargetActor);

	UFUNCTION(BlueprintPure, Category="Combat")
	float GetDesiredAttackRange() const;

	UFUNCTION(BlueprintCallable, Category="CombatToken")
	void RegisterCombatTokenAggro();

	UFUNCTION(BlueprintCallable, Category="CombatToken")
	void UnregisterCombatTokenAggro();

	UFUNCTION(BlueprintCallable, Category="CombatToken")
	bool TryAcquireCombatToken();

	UFUNCTION(BlueprintCallable, Category="CombatToken")
	void ReleaseCombatToken();

	UFUNCTION(BlueprintPure, Category="CombatToken")
	bool HasAvailableCombatToken() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTDSkillComponent> SkillComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTDSignificanceComponent> SignificanceComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	float ContactDamage = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0", ToolTip="Seconds until a dead monster is destroyed. 0 keeps the body."))
	float DeathLifeSpanSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI")
	bool bPrioritizeCaravan = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI")
	TObjectPtr<AActor> CurrentTarget;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	bool bGrantDefaultActionAbilities = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC Update")
	bool bUseNPCUpdateSubsystem = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC Update", meta=(ClampMin="0.0"))
	float ManagedAnimationRenderTolerance = 0.2f;

private:
	bool ActivateCombatAbility(FGameplayTag AbilityTag, AActor* TargetActor = nullptr);
	FGameplayTag ResolveAbilityTag(FGameplayTag RequestedActionTag) const;
	void GrantDefaultActionAbilities();
	void UnregisterFromNPCUpdateSubsystem();
	void HandleDeath(const FTDDamageContext& Context);
	UTDCombatTokenSubsystem* GetCombatTokenSubsystem() const;

	bool bHasGrantedDefaultActionAbilities = false;
	bool bIsManagedByNPCUpdateSubsystem = false;
};
