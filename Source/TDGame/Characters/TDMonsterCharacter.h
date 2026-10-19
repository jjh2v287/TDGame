#pragma once

#include "CoreMinimal.h"
#include "Characters/TDCombatCharacter.h"
#include "Engine/TimerHandle.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "MonsterAI/TDMonsterBody.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "MonsterAI/TDMonsterThinkSubsystem.h"
#include "TDMonsterCharacter.generated.h"

class ATDDamageEntity;
class UAnimSequence;
class UTDDamageDefinition;
class UNiagaraComponent;
class UTDSignificanceComponent;
class UTDSkillComponent;
struct FTDDamageContext;
struct FTDDamageResult;
struct FStreamableHandle;

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
	virtual float BeginBodyFlight(float AltitudeCm, float Seconds) override;
	virtual float EndBodyFlight(float Seconds) override;
	virtual float BeginBodySummon(const FTDMonsterSummonRequest& Request) override;
	virtual int32 CountBodyMinionsAlive() const override;
	virtual void NotifyBodyPhaseEntered(int32 PhaseIndex, FName PhaseId) override;
	virtual void SetBodyChanneling(bool bIsChanneling) override;
	virtual void SetBodyDashing(bool bIsDashing) override;

	UFUNCTION(BlueprintPure, Category="MonsterAI")
	UTDMonsterSpeciesAsset* GetSpecies() const { return Species; }

	UFUNCTION(BlueprintCallable, Category="MonsterAI")
	void SetSpecies(UTDMonsterSpeciesAsset* NewSpecies);

	UFUNCTION(BlueprintPure, Category="MonsterAI")
	bool HasMonsterBrain() const { return BrainHandle.IsValid(); }

	UFUNCTION(BlueprintPure, Category="MonsterAI")
	int32 GetMonsterPhaseIndex() const;

	UFUNCTION(BlueprintPure, Category="MonsterAI")
	TArray<float> GetPhaseHealthMarks() const;

	UFUNCTION(BlueprintPure, Category="MonsterAI")
	bool IsMonsterFlying() const;

	UFUNCTION(BlueprintPure, Category="MonsterAI")
	bool IsBossMonster() const;

	UFUNCTION(BlueprintPure, Category="MonsterAI")
	bool IsEliteMonster() const;

	bool IsInCommittedAttack() const;
	bool IsInDangerWindup() const;
	static float ComputeStaggerPushSpeed(bool bIsMeleeHit, bool bIsComboFinisher, int32 ComboStageIndex);
	static float ComputeStaggerPushSeconds(bool bIsMeleeHit, bool bIsComboFinisher, int32 ComboStageIndex);
	void PushBodyAwayFrom(const FVector& SourceLocation, float Speed, float Seconds);
	static bool IsMeleeSwingSpell(const UTDDamageDefinition& Spell, float Range);

	UFUNCTION(BlueprintCallable, Category="MonsterAI")
	void EngageMonsterBrain(AActor* Target);

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

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeathRagdollFailed() override;

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
	UTDMonsterThinkSubsystem* GetThinkSubsystem() const;
	void ApplySpeciesBody();
	void ApplySpeciesFollowerMeshes();
	void ApplyFollowerMeshes(const TArray<TObjectPtr<USkeletalMesh>>& Meshes);
	void ApplyPhasePresentation(FName PhaseId);
	const FTDMonsterPhasePresentation* FindActivePhasePresentation() const;
	const FTDMonsterAnimClip& SelectHitClip() const;
	const FTDMonsterAnimClip& SelectDeathClip() const;
	void RequestChannelEffect();
	USkeletalMeshComponent* CreateFollowerMeshComponent();
	void ApplySpeciesAnimation();
	void StartMonsterBrain();
	void StopMonsterBrain();
	void HandleMonsterDamaged(const FTDDamageResult& Result, const FTDDamageContext& Context);
	void HandleMonsterFrozen(bool bIsFrozen);
	void PlayDeathPresentation();
	void PlayDeathClipOrRagdoll();
	float ComputeKillPoseHoldSeconds() const;
	void ReleaseKillPoseWhenHoldEnds();
	bool StartDeathRagdoll(ETDDeathPresentation Presentation);
	void ScheduleDeathClipEnd(float DeathClipSeconds, bool bShouldRagdollAfterClip);
	void PlayDeathClipCorpseLanding();
	float PlayDeathClip();
	ETDDeathPresentation ResolveMonsterDeathPresentation() const;
	ETDKillRank ResolveDeathKillRank() const;
	void RecordKillingBlow(const FTDDamageContext& Context);
	void ScheduleCorpseSink();
	void BeginCorpseSink();
	void AdvanceCorpseSink();
	void BroadcastDeathMessage(const FTDDamageContext& Context);
	FVector ComputeAbilityOrigin(const FTDMonsterAbilityRequest& Request) const;
	float PlayAbilityClip(const FTDMonsterAbilityRequest& Request, float WindupSeconds, float& OutRecoverySeconds);
	float PlaySpeciesClip(const FTDMonsterAnimClip& Clip, float PlayRate);
	float PlaySpeciesReactionClip(const FTDMonsterAnimClip& Clip);
	void ScheduleMeleeSwingMessage(const UTDDamageDefinition& Spell, float Range, float WindupSeconds);
	void BroadcastMeleeSwingMessage();
	void CatchUpAbilityClip();
	void RestoreAbilityClipPlayRate();
	void ApplyFlightMoveInput(const FVector2D& Direction, float SpeedScale);
	void FinishBodyLandingIfArrived();
	bool FindGroundHeight(const FVector& Location, const AActor* SpawnedActor, float& OutGroundZ) const;
	void CompleteSummonLoad(const FSoftObjectPath& SpeciesPath, const FTDMonsterSummonRequest& Request);
	void SpawnSummonedMinions(UTDMonsterSpeciesAsset* MinionSpecies, const FTDMonsterSummonRequest& Request);
	ATDMonsterCharacter* SpawnSummonedMinion(UTDMonsterSpeciesAsset* MinionSpecies, const FVector& Location, const FRotator& Rotation);
	void CancelSummonLoads();

	bool bHasGrantedDefaultActionAbilities = false;
	bool bHasAppliedMonsterStats = false;
	float BaseMoveSpeed = 400.f;
	float AbilityImpactTime = 0.f;
	float DamageStaggerPushSpeed = 0.f;
	float DamageStaggerPushSeconds = 0.f;
	bool bIsAbilityDanger = false;

	TWeakObjectPtr<UAnimSequence> AbilityClipAnimation;
	float AbilityClipPlayRate = 1.f;
	float AbilityClipImpactAnimationSeconds = 0.f;
	float AbilityClipEndTime = 0.f;
	FTimerHandle AbilityClipRestoreTimerHandle;
	FTimerHandle MeleeSwingTimerHandle;
	FTimerHandle KillPoseHoldTimerHandle;
	bool bWasHitStopHoldingBody = false;
	bool bWasKilledByComboFinisher = false;
	bool bWasKilledByGroundArea = false;
	int32 MultiKillIndex = 0;
	float GroundAreaKillDistance = 0.f;

	FTimerHandle DeathRagdollTimerHandle;
	FTimerHandle CorpseSinkTimerHandle;
	FVector CorpseSinkStartLocation = FVector::ZeroVector;
	double CorpseSinkStartSeconds = 0.0;
	float CorpseSinkDurationSeconds = 0.f;
	float CorpseSinkDepth = 0.f;
	FTDMonsterSlotHandle BrainHandle;
	TWeakObjectPtr<ATDDamageEntity> ActiveAbilityEntity;
	bool bIsBodyFlying = false;
	bool bIsBodyLanding = false;
	float BodyFlightTargetZ = 0.f;
	float BodyFlightBoostSpeed = 0.f;
	double BodyFlightTransitionEndSeconds = 0.0;
	int32 PendingMinionCount = 0;
	TArray<TWeakObjectPtr<ATDMonsterCharacter>> SummonedMinions;
	TArray<TSharedPtr<FStreamableHandle>> SummonLoadHandles;
	TArray<FTimerHandle> SummonTimerHandles;
	FName ActivePresentationPhaseId;
	TSharedPtr<FStreamableHandle> ChannelEffectHandle;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ChannelEffectComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> FollowerMeshComponents;
};
