#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Combat/Feedback/TDDeathLaunchRules.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Pawn.h"
#include "MoverSimulationTypes.h"
#include "TDCombatCharacter.generated.h"

class UCapsuleComponent;
class UCharacterMoverComponent;
class UGameplayAbility;
class UGameplayEffect;
class UMaterialInterface;
class UNavMoverComponent;
class USkeletalMeshComponent;
class UTDCharacterAnimationComponent;
class UTDCombatAttributeSet;
class UTDCombatComponent;
class UTDDamageDefinition;
class UUAFComponent;
struct FTDDamageContext;
struct FTDDamageResult;

UENUM(BlueprintType)
enum class ETDFacingMode : uint8
{
	MovementDirection,
	ControlRotation
};

UCLASS(Abstract)
class TDGAME_API ATDCombatCharacter : public APawn, public IAbilitySystemInterface, public IMoverInputProducerInterface
{
	GENERATED_BODY()

public:
	ATDCombatCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static const FName CapsuleComponentName;
	static const FName MeshComponentName;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void PostInitializeComponents() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual FVector GetNavAgentLocation() const override;
	virtual void UpdateNavigationRelevance() override;

	UTDCombatComponent* GetCombatComponent() const { return CombatComponent.Get(); }
	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent.Get(); }
	USkeletalMeshComponent* GetMesh() const { return Mesh.Get(); }
	UCharacterMoverComponent* GetMoverComponent() const { return MoverComponent.Get(); }
	UNavMoverComponent* GetNavMoverComponent() const { return NavMoverComponent.Get(); }
	UUAFComponent* GetUAFComponent() const { return UAFComponent.Get(); }
	UTDCharacterAnimationComponent* GetCharacterAnimation() const { return CharacterAnimation.Get(); }

	UFUNCTION(BlueprintCallable, Category="Combat")
	bool CastDamageSpell(int32 Slot, const FVector& Target);

	UFUNCTION(BlueprintPure, Category="Movement")
	bool IsMovingOnGround() const;

	UFUNCTION(BlueprintPure, Category="Movement")
	bool IsAirborne() const;

	UFUNCTION(BlueprintPure, Category="Movement")
	bool CanJump() const;

	UFUNCTION(BlueprintCallable, Category="Movement")
	bool Jump();

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetMaxMoveSpeed(float NewMaxSpeed);

	UFUNCTION(BlueprintPure, Category="Movement")
	float GetMaxMoveSpeed() const;

	UFUNCTION(BlueprintCallable, Category="Movement")
	void StopMovementImmediately();

	UFUNCTION(BlueprintCallable, Category="Movement")
	void DisableMovement();

	UFUNCTION(BlueprintCallable, Category="Movement")
	void EnableMovement();

	UFUNCTION(BlueprintPure, Category="Movement")
	bool IsMovementDisabled() const { return bIsMovementDisabled; }

	UFUNCTION(BlueprintCallable, Category="Movement")
	void AddImpulseVelocity(const FVector& VelocityChange);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void FaceRotationImmediately(const FRotator& NewRotation);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void TeleportPawn(const FVector& NewLocation, const FRotator& NewRotation);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetFacingMode(ETDFacingMode NewFacingMode) { FacingMode = NewFacingMode; }

	UFUNCTION(BlueprintPure, Category="Movement")
	ETDFacingMode GetFacingMode() const { return FacingMode; }

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetDesiredFacingDirection(const FVector& Direction);

	UFUNCTION(BlueprintCallable, Category="Movement")
	void ClearDesiredFacingDirection();

	UFUNCTION(BlueprintCallable, Category="Movement")
	void SetMovementFrozen(bool bIsFrozen);

	void SetHitFlashOverlay(UMaterialInterface* Material);
	void SetPresentationOffset(const FVector& WorldOffset);
	void ApplyKnockback(const FVector& PlanarVelocity, float Seconds);
	void ApplyLinearKnockback(const FVector& PlanarVelocity, float Seconds);
	void ClearDeferredKnockback();
	FTDDeathLaunchInput MakeDeathLaunchInput(const FTDDamageResult& Result) const;
	bool IsRagdolling() const { return bIsRagdolling; }
	bool IsRagdollPendingOrActive() const { return bIsRagdolling || bIsRagdollPending; }
	bool HasActiveDeathLaunchTimersForTest() const;
	bool HasRagdollPosePushForTest() const { return RagdollPoseFinalizedHandle.IsValid(); }

	UFUNCTION(BlueprintPure, Category="Combat")
	int64 GetMeshBoneRevisionForTest() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult) override;

	UFUNCTION()
	virtual void HandleMovementModeChanged(const FName& PreviousMovementModeName, const FName& NewMovementModeName);

	FVector ResolveHitPlanarDirection(const FTDDamageResult& Result) const;
	void StopRagdollSettle();
	void StopDeathLaunchWatch();
	void ScheduleRagdollEntry(float HoldRealSeconds);
	void CancelScheduledRagdollEntry();
	bool RequestDeathRagdoll(const FTDDeathLaunchInput& Input);
	bool StartRagdoll(const FTDDeathLaunchInput& Input);
	void StartPendingDeathRagdollNow(bool bStartsAtRest);
	virtual void HandleDeathRagdollFailed();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UCapsuleComponent> CapsuleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UCharacterMoverComponent> MoverComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UNavMoverComponent> NavMoverComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UUAFComponent> UAFComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UTDCharacterAnimationComponent> CharacterAnimation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UTDCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UTDCombatAttributeSet> CombatAttributes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	TArray<TObjectPtr<UTDDamageDefinition>> DamageSpells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> StartupAbilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0", ForceUnits="cm/s"))
	float DefaultMaxMoveSpeed = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0"))
	float MoveAcceleration = 2048.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0"))
	float MoveDeceleration = 2048.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.0", ForceUnits="deg/s"))
	float TurningRate = 640.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement")
	ETDFacingMode FacingMode = ETDFacingMode::MovementDirection;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	bool bBlockMoveInputWhileStunned = false;

private:
	struct FTDDeferredKnockback
	{
		FVector PlanarVelocity = FVector::ZeroVector;
		float Seconds = 0.f;
		bool bUsesLinearDecay = false;
	};

	void ScheduleDeferredMovement();
	void ResolveDeferredMovement();
	void HandleDeferredMovementOwnerDeath(const FTDDamageContext& Context);
	void ApplyImpulseVelocityNow(const FVector& VelocityChange);
	void QueueKnockback(const FVector& PlanarVelocity, float Seconds, bool bUsesLinearDecay);
	void ApplyKnockbackNow(const FVector& PlanarVelocity, float Seconds, bool bUsesLinearDecay);
	void ApplyMovementSettings();
	void LinkAnimationBeforeMovement();
	FVector ConsumeMoveIntent();
	bool IsMoveInputBlockedByStun() const;
	bool IsCapsuleModifierActive() const;
	bool IsKnockbackSuppressed() const;
	void UpdateRagdollSettle();
	void PushRagdollPoseToRenderer();
	void StopRagdollPosePush();

	FVector DesiredFacingDirection = FVector::ZeroVector;
	FVector LastPresentationOffsetLocal = FVector::ZeroVector;
	FName MovementModeBeforeFreeze = NAME_None;
	TArray<FVector, TInlineAllocator<2>> DeferredImpulseVelocities;
	TArray<FTDDeferredKnockback, TInlineAllocator<2>> DeferredKnockbacks;

	FTimerHandle DeferredMovementTimerHandle;
	FTimerHandle RagdollSettleTimerHandle;
	FDelegateHandle RagdollPoseFinalizedHandle;
	double RagdollStartSeconds = 0.0;

	bool bIsMovementDisabled = false;
	bool bIsMovementFrozen = false;
	bool bIsRagdolling = false;
	bool bIsRagdollSettling = false;
	bool bIsRagdollFirm = false;
	bool bWasRagdollAwakeAtLastPoseFinalize = false;
	bool bHasUnpushedRagdollPose = false;

	struct FTDDeathLaunchBody
	{
		int32 BodyIndex = INDEX_NONE;
		float Mass = 0.f;
	};

	FTDDeathLaunch MakeDeathLaunchFromMassCenter(const FTDDeathLaunchInput& Input, const FVector& MassCenter) const;
	void ApplyDeathLaunch(USkeletalMeshComponent& MeshComponent, const FTDDeathLaunchInput& Input);
	void CacheDeathLaunchBodies(const USkeletalMeshComponent& MeshComponent);
	bool MeasureDeathLaunchMassCenter(const USkeletalMeshComponent& MeshComponent, FVector& OutLocation, FVector& OutVelocity) const;
	bool TraceDeathGroundHeight(const FVector& From, float& OutGroundHeight) const;
	void ApplyCorpsePhysicalMaterial(USkeletalMeshComponent& MeshComponent);
	void EnableDeathLaunchCcd(USkeletalMeshComponent& MeshComponent);
	void DisableDeathLaunchCcd();
	void AddDeathLaunchGravity(USkeletalMeshComponent& MeshComponent);
	void UpdateDeathLaunchWatch();
	void LandDeathLaunch(USkeletalMeshComponent& MeshComponent, const FVector& MassCenter, const FTDDeathLandingSample& Sample);
	void PlayCorpseLanding(const FVector& MassCenter, const FTDDeathLandingSample& Sample);
	void StartDeathLaunchBite(USkeletalMeshComponent& MeshComponent);
	void EndDeathLaunchBite();
	void CacheDeathRollBodies(const USkeletalMeshComponent& MeshComponent, const FTDDeathLaunchInput& Input);
	void StartLandingRollLimit();
	void LimitLandingRoll();
	float MeasureTorsoRollRadiansPerSecond(const USkeletalMeshComponent& MeshComponent, const FVector& RollAxis) const;
	void ApplyRagdollDamping(USkeletalMeshComponent& MeshComponent) const;
	bool ShouldDeferDeathRagdoll() const;
	void RetryPendingDeathRagdoll();
	void ClearPendingDeathRagdoll();

	TArray<FTDDeathLaunchBody> DeathLaunchBodies;
	TArray<FTDDeathLaunchBody, TInlineAllocator<8>> DeathRollTorsoBodies;
	TArray<int32, TInlineAllocator<3>> DeathLaunchCcdBodyIndices;
	FVector DeathRollAxisInAxisBody = FVector::ForwardVector;
	int32 DeathRollAxisBodyIndex = INDEX_NONE;
	FTDDeathLaunch ActiveDeathLaunch;
	FTDDeathLaunchInput PendingDeathLaunchInput;
	FTimerHandle DeathLaunchWatchTimerHandle;
	FTimerHandle DeathLaunchBiteTimerHandle;
	FTimerHandle PendingDeathRagdollTimerHandle;
	double DeathLaunchStartSeconds = 0.0;
	double PendingDeathRagdollStartRealSeconds = 0.0;
	double ScheduledRagdollEntryRealSeconds = 0.0;
	uint64 DeathLaunchGravityFrame = TNumericLimits<uint64>::Max();
	float DeathLaunchMinVerticalSpeed = 0.f;
	ETDKillRank DeathLaunchKillRank = ETDKillRank::Normal;
	bool bHasHorizontalDeathBodyPlan = false;
	bool bIsRagdollPending = false;
	bool bHasScheduledRagdollEntry = false;
};
