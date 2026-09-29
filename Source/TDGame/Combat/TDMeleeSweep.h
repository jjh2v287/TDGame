#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "BoneIndices.h"
#include "CollisionQueryParams.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Engine/HitResult.h"

class UAnimSequence;
class USkinnedAsset;
class USkeletalMeshComponent;

struct FTDMeleeSweepSocket
{
	FTransform LocalTransform = FTransform::Identity;
	FCompactPoseBoneIndex CompactBoneIndex = FCompactPoseBoneIndex(INDEX_NONE);
};

class TDGAME_API FTDMeleeSweep
{
public:
	bool Begin(USkeletalMeshComponent* MeshComponent, const UAnimSequence* Animation, const FTDMeleeSweepSettings& InSettings, const FTDDamageContext& InDamageContext, float AnimationTime, bool bShouldLockRootBone);
	void Advance(float AnimationTime);
	void End(float AnimationTime);
	bool IsActive() const { return bIsActive; }
	int32 GetHitActorCount() const { return HitActors.Num(); }

private:
	int32 ResolveSocketBoneIndex(FTransform& OutSocketLocalTransform, FName SocketName) const;
	bool SampleBladePoints(double AnimationTime, const FTransform& ComponentToWorld, TArray<FVector>& OutBladePoints) const;
	FVector ApplyHeightLock(const FTransform& ComponentToWorld, const FVector& WorldLocation) const;
	void SweepTimeRange(float ToTime);
	void SweepBladeStep(UWorld* World, const TArray<FVector>& NewBladePoints);
	void ApplyHitResults(UWorld* World, const TArray<FHitResult>& HitResults, FCollisionQueryParams& QueryParams);

	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TWeakObjectPtr<const UAnimSequence> SweepAnimation;
	TWeakObjectPtr<const USkinnedAsset> SkinnedAsset;
	FTDMeleeSweepSettings Settings;
	FTDDamageContext DamageContext;
	FBoneContainer BoneContainer;
	FTDMeleeSweepSocket BaseSocket;
	FTDMeleeSweepSocket TipSocket;
	FTransform LastComponentTransform = FTransform::Identity;
	TArray<FVector> LastBladePoints;
	TArray<TWeakObjectPtr<AActor>> IgnoredActors;
	TSet<TWeakObjectPtr<AActor>> HitActors;
	float LockedHeightAboveComponent = 0.f;
	float LastSampledTime = 0.f;
	bool bHasTipSocket = false;
	bool bShouldLockRootBone = false;
	bool bIsActive = false;
};
