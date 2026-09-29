#pragma once

#include "CoreMinimal.h"
#include "Characters/TDCombatCharacter.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "MonsterAI/TDMonsterBody.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MonsterAI/TDMonsterThinkSubsystem.h"
#include "TDMonsterCharacter.generated.h"

class ATDDamageEntity;
class UTDCombatTokenSubsystem;
class UTDSignificanceComponent;
class UTDSkillComponent;
struct FTDDamageContext;
struct FTDDamageResult;

UCLASS()
class TDGAME_API ATDMonsterCharacter : public ATDCombatCharacter, public IGenericTeamAgentInterface, public ITDMonsterBody
{
	GENERATED_BODY()

public:
	ATDMonsterCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual FVector GetBodyLocation() const override;
	virtual float GetBodyYaw() const override;
	virtual float GetBodyRadius() const override;
	virtual UTDCombatComponent* GetBodyCombatComponent() const override;
	virtual void ApplyBodyStats(const FTDMonsterStatValues& Stats) override;
	virtual void ApplyBodyMoveIntent(const FVector2D& Direction, float SpeedScale) override;
	virtual void FaceBodyToward(const FVector& Location) override;
	virtual float BeginBodyAbility(const FTDMonsterAbilityRequest& Request) override;
	virtual void CancelBodyAbility() override;
	virtual void BeginBodyStagger(const FVector& SourceLocation, float Seconds) override;
	virtual void PresentBody(float DeltaSeconds, ETDMonsterFsmState State) override;

	UFUNCTION(BlueprintPure, Category="MonsterAI")
	UTDMonsterSpeciesAsset* GetSpecies() const { return Species; }

	UFUNCTION(BlueprintCallable, Category="MonsterAI")
	void SetSpecies(UTDMonsterSpeciesAsset* NewSpecies);

	UFUNCTION(BlueprintPure, Category="MonsterAI")
	bool HasMonsterBrain() const { return BrainHandle.IsValid(); }

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MonsterAI", meta=(ToolTip="Body, animation and brain definition of this monster. Monsters without a species keep the legacy behaviour and do not think."))
	TObjectPtr<UTDMonsterSpeciesAsset> Species;

private:
	bool ActivateCombatAbility(FGameplayTag AbilityTag, AActor* TargetActor = nullptr);
	FGameplayTag ResolveAbilityTag(FGameplayTag RequestedActionTag) const;
	void GrantDefaultActionAbilities();
	void HandleDeath(const FTDDamageContext& Context);
	UTDCombatTokenSubsystem* GetCombatTokenSubsystem() const;
	UTDMonsterThinkSubsystem* GetThinkSubsystem() const;
	void ApplySpeciesBody();
	void ApplySpeciesAnimation();
	void StartMonsterBrain();
	void StopMonsterBrain();
	void HandleMonsterDamaged(const FTDDamageResult& Result, const FTDDamageContext& Context);
	void HandleMonsterFrozen(bool bIsFrozen);
	void PlayDeathPresentation();
	FVector ComputeAbilityOrigin(const FTDMonsterAbilityRequest& Request) const;
	float PlayAbilityClip(const FTDMonsterAbilityRequest& Request, float WindupSeconds, float& OutRecoverySeconds);
	float PlaySpeciesClip(const FTDMonsterAnimClip& Clip, float PlayRate);
	void StartRagdoll();

	bool bHasGrantedDefaultActionAbilities = false;
	bool bHasAppliedMonsterStats = false;
	float BaseMoveSpeed = 400.f;
	float AbilityImpactTime = 0.f;
	FTDMonsterSlotHandle BrainHandle;
	TWeakObjectPtr<ATDDamageEntity> ActiveAbilityEntity;
};
