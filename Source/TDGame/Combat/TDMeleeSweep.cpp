#include "Combat/TDMeleeSweep.h"

#include "Animation/AnimCurveFilter.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/MemStack.h"
#include "TDGame.h"

namespace
{
	constexpr int32 MaxSweepStepsPerRange = 256;

	void AddBoneChain(TArray<FBoneIndexType>& RequiredBones, const FReferenceSkeleton& ReferenceSkeleton, int32 BoneIndex)
	{
		while (BoneIndex != INDEX_NONE)
		{
			RequiredBones.AddUnique(static_cast<FBoneIndexType>(BoneIndex));
			BoneIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
		}
	}

	bool IsSweepSourceUnchanged(const TWeakObjectPtr<USkeletalMeshComponent>& Mesh, const TWeakObjectPtr<const USkinnedAsset>& SkinnedAsset, const TWeakObjectPtr<const UAnimSequence>& Animation)
	{
		const USkeletalMeshComponent* MeshComponent = Mesh.Get();
		return MeshComponent && Animation.IsValid() && SkinnedAsset.IsValid() && MeshComponent->GetSkinnedAsset() == SkinnedAsset.Get();
	}
}

bool FTDMeleeSweep::Begin(USkeletalMeshComponent* MeshComponent, const UAnimSequence* Animation, const FTDMeleeSweepSettings& InSettings, const FTDDamageContext& InDamageContext, const float AnimationTime, const bool bLockRootBone)
{
	bIsActive = false;
	HitActors.Reset();
	IgnoredActors.Reset();
	LastBladePoints.Reset();
	if (!MeshComponent || !Animation)
	{
		return false;
	}

	if (InSettings.WeaponBaseSocketName.IsNone())
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDMeleeSweep: WeaponBaseSocketName is not set for '%s'."), *Animation->GetName());
		return false;
	}

	AActor* Owner = MeshComponent->GetOwner();
	const USkinnedAsset* MeshAsset = MeshComponent->GetSkinnedAsset();
	if (!Owner || !MeshAsset)
	{
		return false;
	}

	const UTDCombatComponent* Combatant = Owner->FindComponentByClass<UTDCombatComponent>();
	if (!Combatant || !Combatant->IsAlive())
	{
		return false;
	}

	Mesh = MeshComponent;
	Settings = InSettings;
	FTransform BaseSocketLocalTransform;
	const int32 BaseBoneIndex = ResolveSocketBoneIndex(BaseSocketLocalTransform, Settings.WeaponBaseSocketName);
	if (BaseBoneIndex == INDEX_NONE)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDMeleeSweep: socket or bone '%s' not found on '%s'."), *Settings.WeaponBaseSocketName.ToString(), *MeshAsset->GetName());
		return false;
	}

	FTransform TipSocketLocalTransform;
	const int32 TipBoneIndex = Settings.WeaponTipSocketName.IsNone() ? INDEX_NONE : ResolveSocketBoneIndex(TipSocketLocalTransform, Settings.WeaponTipSocketName);
	if (!Settings.WeaponTipSocketName.IsNone() && TipBoneIndex == INDEX_NONE)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDMeleeSweep: socket or bone '%s' not found on '%s'."), *Settings.WeaponTipSocketName.ToString(), *MeshAsset->GetName());
	}

	const FReferenceSkeleton& ReferenceSkeleton = MeshAsset->GetRefSkeleton();
	TArray<FBoneIndexType> RequiredBones;
	AddBoneChain(RequiredBones, ReferenceSkeleton, BaseBoneIndex);
	AddBoneChain(RequiredBones, ReferenceSkeleton, TipBoneIndex);
	RequiredBones.Sort();

	BoneContainer.InitializeTo(RequiredBones, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), *MeshAsset);
	SkinnedAsset = MeshAsset;
	SweepAnimation = Animation;
	BaseSocket.LocalTransform = BaseSocketLocalTransform;
	BaseSocket.CompactBoneIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BaseBoneIndex));
	bHasTipSocket = TipBoneIndex != INDEX_NONE;
	if (bHasTipSocket)
	{
		TipSocket.LocalTransform = TipSocketLocalTransform;
		TipSocket.CompactBoneIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(TipBoneIndex));
	}

	bShouldLockRootBone = bLockRootBone;
	LockedHeightAboveComponent = Owner->GetActorLocation().Z - MeshComponent->GetComponentLocation().Z + Settings.LockedHeightOffset;
	LastSampledTime = AnimationTime;
	LastComponentTransform = MeshComponent->GetComponentTransform();
	if (!SampleBladePoints(AnimationTime, LastComponentTransform, LastBladePoints) || LastBladePoints.IsEmpty())
	{
		LastBladePoints.Reset();
		return false;
	}

	DamageContext = InDamageContext;
	IgnoredActors.Add(Owner);
	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		IgnoredActors.Add(AttachedActor);
	}

	bIsActive = true;
	return true;
}

void FTDMeleeSweep::Advance(const float AnimationTime)
{
	if (!bIsActive)
	{
		return;
	}

	if (!IsSweepSourceUnchanged(Mesh, SkinnedAsset, SweepAnimation))
	{
		bIsActive = false;
		return;
	}

	if (AnimationTime < LastSampledTime)
	{
		return;
	}

	SweepTimeRange(AnimationTime);
}

void FTDMeleeSweep::End(const float AnimationTime)
{
	if (!bIsActive)
	{
		return;
	}

	if (IsSweepSourceUnchanged(Mesh, SkinnedAsset, SweepAnimation) && AnimationTime >= LastSampledTime)
	{
		SweepTimeRange(AnimationTime);
	}
	bIsActive = false;
}

int32 FTDMeleeSweep::ResolveSocketBoneIndex(FTransform& OutSocketLocalTransform, const FName SocketName) const
{
	const USkeletalMeshComponent* MeshComponent = Mesh.Get();
	if (!MeshComponent)
	{
		OutSocketLocalTransform = FTransform::Identity;
		return INDEX_NONE;
	}

	int32 BoneIndex = INDEX_NONE;
	if (MeshComponent->GetSocketInfoByName(SocketName, OutSocketLocalTransform, BoneIndex))
	{
		return BoneIndex;
	}
	OutSocketLocalTransform = FTransform::Identity;
	return MeshComponent->GetBoneIndex(SocketName);
}

bool FTDMeleeSweep::SampleBladePoints(const double AnimationTime, const FTransform& ComponentToWorld, TArray<FVector>& OutBladePoints) const
{
	const UAnimSequence* Animation = SweepAnimation.Get();
	if (!Animation)
	{
		return false;
	}

	FMemMark Mark(FMemStack::Get());
	FCompactPose Pose;
	Pose.SetBoneContainer(&BoneContainer);
	Pose.ResetToRefPose();
	FBlendedCurve Curve;
	Curve.InitFrom(BoneContainer);
	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData PoseData(Pose, Curve, Attributes);
	const FAnimExtractContext ExtractContext(AnimationTime, bShouldLockRootBone);
	Animation->GetAnimationPose(PoseData, ExtractContext);

	FCSPose<FCompactPose> ComponentSpacePose;
	ComponentSpacePose.InitPose(MoveTemp(Pose));
	const FVector BaseLocation = ApplyHeightLock(ComponentToWorld, (BaseSocket.LocalTransform * ComponentSpacePose.GetComponentSpaceTransform(BaseSocket.CompactBoneIndex) * ComponentToWorld).GetLocation());
	OutBladePoints.Reset();
	if (!bHasTipSocket)
	{
		OutBladePoints.Add(BaseLocation);
		return true;
	}

	const FVector TipLocation = ApplyHeightLock(ComponentToWorld, (TipSocket.LocalTransform * ComponentSpacePose.GetComponentSpaceTransform(TipSocket.CompactBoneIndex) * ComponentToWorld).GetLocation());
	const int32 PointCount = FMath::Clamp(Settings.BladeSampleCount, 2, 16);
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		const float Alpha = static_cast<float>(PointIndex) / static_cast<float>(PointCount - 1);
		OutBladePoints.Add(FMath::Lerp(BaseLocation, TipLocation, Alpha));
	}
	return true;
}

FVector FTDMeleeSweep::ApplyHeightLock(const FTransform& ComponentToWorld, const FVector& WorldLocation) const
{
	if (!Settings.bLockHeightToOwner)
	{
		return WorldLocation;
	}
	return FVector(WorldLocation.X, WorldLocation.Y, ComponentToWorld.GetLocation().Z + LockedHeightAboveComponent);
}

void FTDMeleeSweep::SweepTimeRange(const float ToTime)
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	UWorld* World = MeshComponent ? MeshComponent->GetWorld() : nullptr;
	if (!World || LastBladePoints.IsEmpty())
	{
		return;
	}

	const float FromTime = LastSampledTime;
	const float Duration = FMath::Max(ToTime - FromTime, 0.f);
	const FTransform StartTransform = LastComponentTransform;
	const FTransform EndTransform = MeshComponent->GetComponentTransform();
	const float Interval = FMath::Max(Settings.SampleIntervalSeconds, 0.001f);
	const int32 StepCount = FMath::Clamp(FMath::CeilToInt(Duration / Interval - UE_KINDA_SMALL_NUMBER), 1, MaxSweepStepsPerRange);
	const float StepDuration = Duration / static_cast<float>(StepCount);

	TArray<FVector> NewBladePoints;
	for (int32 Step = 1; Step <= StepCount && bIsActive; ++Step)
	{
		const float StepTime = (Step == StepCount) ? ToTime : FromTime + StepDuration * static_cast<float>(Step);
		const float Alpha = Duration > UE_KINDA_SMALL_NUMBER ? (StepTime - FromTime) / Duration : 1.f;
		FTransform StepTransform;
		StepTransform.Blend(StartTransform, EndTransform, Alpha);
		if (!SampleBladePoints(StepTime, StepTransform, NewBladePoints))
		{
			break;
		}
		SweepBladeStep(World, NewBladePoints);
		LastBladePoints = NewBladePoints;
		LastSampledTime = StepTime;
		LastComponentTransform = StepTransform;
	}

	if (!bIsActive)
	{
		return;
	}
	LastSampledTime = ToTime;
	LastComponentTransform = EndTransform;
}

void FTDMeleeSweep::SweepBladeStep(UWorld* World, const TArray<FVector>& NewBladePoints)
{
	if (LastBladePoints.Num() != NewBladePoints.Num())
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDMeleeSweep), false);
	for (const TWeakObjectPtr<AActor>& IgnoredActor : IgnoredActors)
	{
		if (IgnoredActor.IsValid())
		{
			QueryParams.AddIgnoredActor(IgnoredActor.Get());
		}
	}
	for (const TWeakObjectPtr<AActor>& HitActor : HitActors)
	{
		if (HitActor.IsValid())
		{
			QueryParams.AddIgnoredActor(HitActor.Get());
		}
	}

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(FMath::Max(Settings.SweepRadius, 0.1f));
	TArray<FHitResult> HitResults;
	for (int32 PointIndex = 0; PointIndex < NewBladePoints.Num() && bIsActive; ++PointIndex)
	{
		const FVector SweepStart = LastBladePoints[PointIndex];
		const FVector SweepEnd = NewBladePoints[PointIndex];
		HitResults.Reset();
		World->SweepMultiByChannel(HitResults, SweepStart, SweepEnd, FQuat::Identity, Settings.SweepChannel, SweepShape, QueryParams);
#if ENABLE_DRAW_DEBUG
		if (Settings.bDrawDebugSweep)
		{
			DrawDebugLine(World, SweepStart, SweepEnd, FColor::Red, false, Settings.DebugDrawDuration, 0, 0.5f);
			DrawDebugSphere(World, SweepEnd, SweepShape.GetSphereRadius(), 8, FColor::Orange, false, Settings.DebugDrawDuration, 0, 0.25f);
		}
#endif
		ApplyHitResults(World, HitResults, QueryParams);
	}
}

void FTDMeleeSweep::ApplyHitResults(UWorld* World, const TArray<FHitResult>& HitResults, FCollisionQueryParams& QueryParams)
{
	UTDDamageSubsystem* DamageSubsystem = World->GetSubsystem<UTDDamageSubsystem>();
	if (!DamageSubsystem)
	{
		return;
	}

	for (const FHitResult& Hit : HitResults)
	{
		if (!bIsActive)
		{
			return;
		}

		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActors.Contains(HitActor))
		{
			continue;
		}

		UTDCombatComponent* Combatant = HitActor->FindComponentByClass<UTDCombatComponent>();
		if (!Combatant || !DamageSubsystem->CanTarget(Combatant, DamageContext, Settings.TargetPolicy))
		{
			continue;
		}

		HitActors.Add(HitActor);
		QueryParams.AddIgnoredActor(HitActor);
		const FVector HitLocation = Hit.ImpactPoint.ContainsNaN() ? HitActor->GetActorLocation() : FVector(Hit.ImpactPoint);
#if ENABLE_DRAW_DEBUG
		if (Settings.bDrawDebugSweep)
		{
			DrawDebugSphere(World, HitLocation, Settings.SweepRadius * 2.f, 12, FColor::Green, false, Settings.DebugDrawDuration, 0, 1.f);
		}
#endif
		DamageSubsystem->ExecuteRules(Settings.HitRules, ETDDamageEvent::Hit, DamageContext, HitActor, HitLocation);
	}
}
