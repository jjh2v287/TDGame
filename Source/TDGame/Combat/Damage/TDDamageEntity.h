#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "TDDamageEntity.generated.h"

class UTDCombatComponent;
class UNiagaraComponent;
class UStaticMeshComponent;

USTRUCT()
struct FTDScheduledDamageAction
{
	GENERATED_BODY()

	UPROPERTY()
	FTDDamageAction Action;

	UPROPERTY()
	TArray<FTDDamageRule> Rules;

	UPROPERTY()
	FTDDamageContext Context;

	UPROPERTY()
	TWeakObjectPtr<AActor> Target;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	ETDDamageEvent Event = ETDDamageEvent::Spawn;
	double ExecuteTime = 0.;
	uint64 Sequence = 0;
};

UCLASS(NotBlueprintable)
class TDGAME_API ATDDamageEntity : public AActor
{
	GENERATED_BODY()

public:
	ATDDamageEntity();
	void Initialize(UTDDamageDefinition* InDefinition, const FTDDamageContext& InContext);
	UTDDamageDefinition* GetDefinition() const;
	void Finish();
	void ApplyHoming(const FTDHomingSettings& Settings, AActor* EventTarget = nullptr);
	void StopHoming();
	bool IsHoming() const;
	AActor* GetHomingTarget() const;
	FVector GetTravelDirection() const;
	void ScheduleAction(const FTDDamageAction& Action, const TArray<FTDDamageRule>& Rules,
		ETDDamageEvent Event, const FTDDamageContext& ActionContext, AActor* Target, const FVector& Location);
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Activate();
	void ProcessTimeline();
	void ExecuteNextScheduledAction();
	void AcquireHomingTarget(AActor* EventTarget);
	bool IsValidHomingTarget(const AActor* Target) const;
	void UpdateHomingDirection(float DeltaSeconds);
	void Pulse(double PulseTime);
	void Expire();
	void Complete(bool bHasExpired, AActor* Target);
	void EmitEvent(ETDDamageEvent Event, AActor* Target, const FVector& Location);
	void AdvanceMotion(double CurrentTime);
	void MoveProjectile(float DeltaSeconds, double HitTime);
	void ExpandShockwave(float DeltaSeconds, double HitTime);
	void HitArea(float OuterRadius, float InnerRadius, double HitTime);
	bool HitTarget(UTDCombatComponent* Target, const FVector& Location, double HitTime);
	bool HasLineOfSight(const AActor* Target) const;
	bool CanContinue() const;
	void DrawShape() const;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Damage", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UTDDamageDefinition> Definition;

	UPROPERTY()
	FTDDamageContext Context;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> EffectComponent;

	UPROPERTY()
	TArray<FTDScheduledDamageAction> ScheduledActions;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Homing", meta=(AllowPrivateAccess="true"))
	FTDHomingSettings HomingSettings;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Homing", meta=(AllowPrivateAccess="true"))
	TWeakObjectPtr<AActor> HomingTarget;

	TMap<TWeakObjectPtr<UTDCombatComponent>, int32> TargetHitCounts;
	TMap<TWeakObjectPtr<UTDCombatComponent>, double> TargetHitTimes;
	FVector TravelDirection = FVector::ForwardVector;
	double SimulationTime = 0.;
	double EventTime = 0.;
	double ActivationTime = 0.;
	double ExpirationTime = 0.;
	double NextPulseTime = 0.;
	double NextHomingSearchTime = 0.;
	uint64 NextActionSequence = 0;
	uint64 HomingRevision = 0;
	float RingInnerRadius = 0.f;
	float RingOuterRadius = 0.f;
	bool bIsActive = false;
	bool bHasFinished = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Homing", meta=(AllowPrivateAccess="true"))
	bool bIsHoming = false;
	bool bIsProcessingTimeline = false;
};
