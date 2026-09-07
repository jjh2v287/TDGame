#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "BoneContainer.h"
#include "BoneIndices.h"
#include "Combat/TDDamageTypes.h"
#include "TDAnimNotifyState_MeleeAttack.generated.h"

class UAnimInstance;
class UAnimMontage;
class USkinnedAsset;
struct FAnimTrack;

struct FTDMeleeSweepSocket
{
	FTransform LocalTransform = FTransform::Identity;
	FCompactPoseBoneIndex CompactBoneIndex = FCompactPoseBoneIndex(INDEX_NONE);
};

struct FTDMeleeSweepState
{
	TWeakObjectPtr<const USkinnedAsset> SkinnedAsset;
	FBoneContainer BoneContainer;
	FTDMeleeSweepSocket BaseSocket;
	FTDMeleeSweepSocket TipSocket;
	bool bHasTipSocket = false;
	bool bShouldLockRootBone = false;
	float TriggerTime = 0.f;
	float EndTriggerTime = 0.f;
	float LastSampledTime = 0.f;
	FTransform LastComponentTransform = FTransform::Identity;
	TArray<FVector> LastBladePoints;
	FTDDamageContext DamageContext;
	TArray<TWeakObjectPtr<AActor>> IgnoredActors;
	TSet<TWeakObjectPtr<AActor>> HitActors;
};

UCLASS(meta=(DisplayName="TD Melee Attack"))
class TDGAME_API UTDAnimNotifyState_MeleeAttack : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UTDAnimNotifyState_MeleeAttack();

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FName WeaponBaseSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FName WeaponTipSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon", meta=(ClampMin="2", ClampMax="16"))
	int32 BladeSampleCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep", meta=(ClampMin="0.1"))
	float SweepRadius = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep")
	TEnumAsByte<ECollisionChannel> SweepChannel = ECC_Pawn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep", meta=(ClampMin="0.001", ClampMax="0.1"))
	float SampleIntervalSeconds = 1.f / 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep")
	FName MontageSlotName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	ETDDamageTargetPolicy TargetPolicy = ETDDamageTargetPolicy::Enemies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	TArray<FTDDamageRule> HitRules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug")
	bool bDrawDebugSweep = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug", meta=(ClampMin="0", EditCondition="bDrawDebugSweep"))
	float DebugDrawDuration = 1.f;

private:
	bool InitializeSweepState(FTDMeleeSweepState& State, USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) const;
	int32 ResolveSocketBoneIndex(FTransform& OutSocketLocalTransform, const USkeletalMeshComponent* MeshComp, FName SocketName) const;
	const FAnimTrack* FindMontageTrack(const UAnimMontage* Montage) const;
	bool SampleBladePoints(const FTDMeleeSweepState& State, const UAnimSequenceBase* Animation, double AnimationTime, const FTransform& ComponentToWorld, TArray<FVector>& OutBladePoints) const;
	void RestartSweep(FTDMeleeSweepState& State, const USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation) const;
	void SweepTimeRange(FTDMeleeSweepState& State, USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float ToTime);
	void SweepBladeStep(FTDMeleeSweepState& State, UWorld* World, const TArray<FVector>& NewBladePoints);
	void ApplyHitResults(FTDMeleeSweepState& State, UWorld* World, const TArray<FHitResult>& HitResults, FCollisionQueryParams& QueryParams);
	bool ShouldLockRootBone(const UAnimInstance* AnimInstance, const UAnimSequenceBase* Animation) const;
	void RemoveStaleStates();

	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FTDMeleeSweepState> SweepStates;
};
