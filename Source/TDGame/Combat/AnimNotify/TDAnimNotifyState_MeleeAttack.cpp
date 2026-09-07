#include "Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimCurveFilter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDDamageSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/MemStack.h"
#include "TDGame.h"

namespace
{
	constexpr int32 MaxSweepStepsPerTick = 256;

	void AddBoneChain(TArray<FBoneIndexType>& RequiredBones, const FReferenceSkeleton& ReferenceSkeleton, int32 BoneIndex)
	{
		while (BoneIndex != INDEX_NONE)
		{
			RequiredBones.AddUnique(static_cast<FBoneIndexType>(BoneIndex));
			BoneIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
		}
	}
}

UTDAnimNotifyState_MeleeAttack::UTDAnimNotifyState_MeleeAttack()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(220, 60, 60);
#endif
}

FString UTDAnimNotifyState_MeleeAttack::GetNotifyName_Implementation() const
{
	if (WeaponBaseSocketName.IsNone())
	{
		return TEXT("TD Melee Attack");
	}
	if (WeaponTipSocketName.IsNone())
	{
		return FString::Printf(TEXT("TD Melee Attack (%s)"), *WeaponBaseSocketName.ToString());
	}
	return FString::Printf(TEXT("TD Melee Attack (%s - %s)"), *WeaponBaseSocketName.ToString(), *WeaponTipSocketName.ToString());
}

void UTDAnimNotifyState_MeleeAttack::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (!MeshComp || !Animation)
	{
		return;
	}

	RemoveStaleStates();
	FTDMeleeSweepState State;
	if (!InitializeSweepState(State, MeshComp, Animation, EventReference))
	{
		return;
	}
	SweepStates.Add(MeshComp, MoveTemp(State));
}

void UTDAnimNotifyState_MeleeAttack::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
	if (!MeshComp || !Animation)
	{
		return;
	}

	FTDMeleeSweepState* State = SweepStates.Find(MeshComp);
	if (!State)
	{
		return;
	}
	if (State->SkinnedAsset.Get() != MeshComp->GetSkinnedAsset())
	{
		SweepStates.Remove(MeshComp);
		return;
	}

	const float CurrentTime = FMath::Clamp(EventReference.GetCurrentAnimationTime(), State->TriggerTime, State->EndTriggerTime);
	if (CurrentTime < State->LastSampledTime)
	{
		RestartSweep(*State, MeshComp, Animation);
	}
	SweepTimeRange(*State, MeshComp, Animation, CurrentTime);
}

void UTDAnimNotifyState_MeleeAttack::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (!MeshComp)
	{
		return;
	}

	FTDMeleeSweepState* State = SweepStates.Find(MeshComp);
	if (!State)
	{
		return;
	}

	const bool bCanFinishSweep = Animation
		&& State->SkinnedAsset.Get() == MeshComp->GetSkinnedAsset()
		&& UAnimNotifyLibrary::NotifyStateReachedEnd(EventReference);
	if (bCanFinishSweep)
	{
		SweepTimeRange(*State, MeshComp, Animation, State->EndTriggerTime);
	}
	SweepStates.Remove(MeshComp);
}

bool UTDAnimNotifyState_MeleeAttack::InitializeSweepState(FTDMeleeSweepState& State, USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) const
{
	const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
	if (!NotifyEvent)
	{
		return false;
	}

	const bool bIsSequenceInsideMontage = !Animation->IsA<UAnimMontage>()
		&& EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>() != nullptr;
	if (bIsSequenceInsideMontage)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDMeleeAttack: notify on '%s' is inside a montage slot; place it on the montage notify track instead."), *Animation->GetName());
		return false;
	}

	if (WeaponBaseSocketName.IsNone())
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDMeleeAttack: WeaponBaseSocketName is not set on '%s'."), *Animation->GetName());
		return false;
	}

	AActor* Owner = MeshComp->GetOwner();
	const USkinnedAsset* SkinnedAsset = MeshComp->GetSkinnedAsset();
	if (!Owner || !SkinnedAsset)
	{
		return false;
	}

	const UTDCombatComponent* Combatant = Owner->FindComponentByClass<UTDCombatComponent>();
	if (!Combatant || !Combatant->IsAlive())
	{
		return false;
	}

	FTransform BaseSocketLocalTransform;
	const int32 BaseBoneIndex = ResolveSocketBoneIndex(BaseSocketLocalTransform, MeshComp, WeaponBaseSocketName);
	if (BaseBoneIndex == INDEX_NONE)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDMeleeAttack: socket or bone '%s' not found on '%s'."), *WeaponBaseSocketName.ToString(), *SkinnedAsset->GetName());
		return false;
	}

	FTransform TipSocketLocalTransform;
	const int32 TipBoneIndex = WeaponTipSocketName.IsNone() ? INDEX_NONE : ResolveSocketBoneIndex(TipSocketLocalTransform, MeshComp, WeaponTipSocketName);
	if (!WeaponTipSocketName.IsNone() && TipBoneIndex == INDEX_NONE)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDMeleeAttack: socket or bone '%s' not found on '%s'."), *WeaponTipSocketName.ToString(), *SkinnedAsset->GetName());
	}

	const FReferenceSkeleton& ReferenceSkeleton = SkinnedAsset->GetRefSkeleton();
	TArray<FBoneIndexType> RequiredBones;
	AddBoneChain(RequiredBones, ReferenceSkeleton, BaseBoneIndex);
	AddBoneChain(RequiredBones, ReferenceSkeleton, TipBoneIndex);
	RequiredBones.Sort();

	State.BoneContainer.InitializeTo(RequiredBones, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), *SkinnedAsset);
	State.SkinnedAsset = SkinnedAsset;
	State.BaseSocket.LocalTransform = BaseSocketLocalTransform;
	State.BaseSocket.CompactBoneIndex = State.BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BaseBoneIndex));
	State.bHasTipSocket = TipBoneIndex != INDEX_NONE;
	if (State.bHasTipSocket)
	{
		State.TipSocket.LocalTransform = TipSocketLocalTransform;
		State.TipSocket.CompactBoneIndex = State.BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(TipBoneIndex));
	}

	State.bShouldLockRootBone = ShouldLockRootBone(MeshComp->GetAnimInstance(), Animation);
	State.TriggerTime = NotifyEvent->GetTriggerTime();
	State.EndTriggerTime = FMath::Max(NotifyEvent->GetEndTriggerTime(), State.TriggerTime);
	RestartSweep(State, MeshComp, Animation);
	if (State.LastBladePoints.IsEmpty())
	{
		return false;
	}

	State.DamageContext.Caster = Owner;
	State.DamageContext.Stats = Combatant->GetStats();
	State.DamageContext.CastTarget = Owner->GetActorLocation();
	State.DamageContext.Direction = Owner->GetActorForwardVector();
	State.DamageContext.Budget = MakeShared<FTDDamageChainBudget>();

	State.IgnoredActors.Add(Owner);
	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		State.IgnoredActors.Add(AttachedActor);
	}
	return true;
}

int32 UTDAnimNotifyState_MeleeAttack::ResolveSocketBoneIndex(FTransform& OutSocketLocalTransform, const USkeletalMeshComponent* MeshComp, FName SocketName) const
{
	int32 BoneIndex = INDEX_NONE;
	if (MeshComp->GetSocketInfoByName(SocketName, OutSocketLocalTransform, BoneIndex))
	{
		return BoneIndex;
	}
	OutSocketLocalTransform = FTransform::Identity;
	return MeshComp->GetBoneIndex(SocketName);
}

const FAnimTrack* UTDAnimNotifyState_MeleeAttack::FindMontageTrack(const UAnimMontage* Montage) const
{
	if (!MontageSlotName.IsNone())
	{
		return Montage->GetAnimationData(MontageSlotName);
	}
	if (Montage->SlotAnimTracks.IsEmpty())
	{
		return nullptr;
	}
	return &Montage->SlotAnimTracks[0].AnimTrack;
}

bool UTDAnimNotifyState_MeleeAttack::SampleBladePoints(const FTDMeleeSweepState& State, const UAnimSequenceBase* Animation, double AnimationTime, const FTransform& ComponentToWorld, TArray<FVector>& OutBladePoints) const
{
	FMemMark Mark(FMemStack::Get());
	FCompactPose Pose;
	Pose.SetBoneContainer(&State.BoneContainer);
	Pose.ResetToRefPose();
	FBlendedCurve Curve;
	Curve.InitFrom(State.BoneContainer);
	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData PoseData(Pose, Curve, Attributes);
	const FAnimExtractContext ExtractContext(AnimationTime, State.bShouldLockRootBone);

	if (const UAnimMontage* Montage = Cast<UAnimMontage>(Animation))
	{
		const FAnimTrack* Track = FindMontageTrack(Montage);
		if (!Track)
		{
			return false;
		}
		Track->GetAnimationPose(PoseData, ExtractContext);
	}
	else
	{
		Animation->GetAnimationPose(PoseData, ExtractContext);
	}

	FCSPose<FCompactPose> ComponentSpacePose;
	ComponentSpacePose.InitPose(MoveTemp(Pose));
	const FVector BaseLocation = (State.BaseSocket.LocalTransform * ComponentSpacePose.GetComponentSpaceTransform(State.BaseSocket.CompactBoneIndex) * ComponentToWorld).GetLocation();
	OutBladePoints.Reset();
	if (!State.bHasTipSocket)
	{
		OutBladePoints.Add(BaseLocation);
		return true;
	}

	const FVector TipLocation = (State.TipSocket.LocalTransform * ComponentSpacePose.GetComponentSpaceTransform(State.TipSocket.CompactBoneIndex) * ComponentToWorld).GetLocation();
	const int32 PointCount = FMath::Clamp(BladeSampleCount, 2, 16);
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		const float Alpha = static_cast<float>(PointIndex) / static_cast<float>(PointCount - 1);
		OutBladePoints.Add(FMath::Lerp(BaseLocation, TipLocation, Alpha));
	}
	return true;
}

void UTDAnimNotifyState_MeleeAttack::RestartSweep(FTDMeleeSweepState& State, const USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation) const
{
	State.LastSampledTime = State.TriggerTime;
	State.LastComponentTransform = MeshComp->GetComponentTransform();
	State.HitActors.Reset();
	if (!SampleBladePoints(State, Animation, State.TriggerTime, State.LastComponentTransform, State.LastBladePoints))
	{
		State.LastBladePoints.Reset();
	}
}

void UTDAnimNotifyState_MeleeAttack::SweepTimeRange(FTDMeleeSweepState& State, USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float ToTime)
{
	UWorld* World = MeshComp->GetWorld();
	if (!World || State.LastBladePoints.IsEmpty())
	{
		return;
	}

	const float FromTime = State.LastSampledTime;
	const float Duration = FMath::Max(ToTime - FromTime, 0.f);
	const FTransform StartTransform = State.LastComponentTransform;
	const FTransform EndTransform = MeshComp->GetComponentTransform();
	const float Interval = FMath::Max(SampleIntervalSeconds, 0.001f);
	const int32 StepCount = FMath::Clamp(FMath::CeilToInt(Duration / Interval - UE_KINDA_SMALL_NUMBER), 1, MaxSweepStepsPerTick);
	const float StepDuration = Duration / static_cast<float>(StepCount);

	TArray<FVector> NewBladePoints;
	for (int32 Step = 1; Step <= StepCount; ++Step)
	{
		const float StepTime = (Step == StepCount) ? ToTime : FromTime + StepDuration * static_cast<float>(Step);
		const float Alpha = Duration > UE_KINDA_SMALL_NUMBER ? (StepTime - FromTime) / Duration : 1.f;
		FTransform StepTransform;
		StepTransform.Blend(StartTransform, EndTransform, Alpha);
		if (!SampleBladePoints(State, Animation, StepTime, StepTransform, NewBladePoints))
		{
			break;
		}
		SweepBladeStep(State, World, NewBladePoints);
		State.LastBladePoints = NewBladePoints;
	}

	State.LastSampledTime = ToTime;
	State.LastComponentTransform = EndTransform;
}

void UTDAnimNotifyState_MeleeAttack::SweepBladeStep(FTDMeleeSweepState& State, UWorld* World, const TArray<FVector>& NewBladePoints)
{
	if (State.LastBladePoints.Num() != NewBladePoints.Num())
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDMeleeAttackSweep), false);
	for (const TWeakObjectPtr<AActor>& IgnoredActor : State.IgnoredActors)
	{
		if (IgnoredActor.IsValid())
		{
			QueryParams.AddIgnoredActor(IgnoredActor.Get());
		}
	}
	for (const TWeakObjectPtr<AActor>& HitActor : State.HitActors)
	{
		if (HitActor.IsValid())
		{
			QueryParams.AddIgnoredActor(HitActor.Get());
		}
	}

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(FMath::Max(SweepRadius, 0.1f));
	TArray<FHitResult> HitResults;
	for (int32 PointIndex = 0; PointIndex < NewBladePoints.Num(); ++PointIndex)
	{
		const FVector& Start = State.LastBladePoints[PointIndex];
		const FVector& End = NewBladePoints[PointIndex];
		HitResults.Reset();
		World->SweepMultiByChannel(HitResults, Start, End, FQuat::Identity, SweepChannel, SweepShape, QueryParams);
#if ENABLE_DRAW_DEBUG
		if (bDrawDebugSweep)
		{
			DrawDebugLine(World, Start, End, FColor::Red, false, DebugDrawDuration, 0, 0.5f);
			DrawDebugSphere(World, End, SweepShape.GetSphereRadius(), 8, FColor::Orange, false, DebugDrawDuration, 0, 0.25f);
		}
#endif
		ApplyHitResults(State, World, HitResults, QueryParams);
	}
}

void UTDAnimNotifyState_MeleeAttack::ApplyHitResults(FTDMeleeSweepState& State, UWorld* World, const TArray<FHitResult>& HitResults, FCollisionQueryParams& QueryParams)
{
	UTDDamageSubsystem* DamageSubsystem = World->GetSubsystem<UTDDamageSubsystem>();
	if (!DamageSubsystem)
	{
		return;
	}

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || State.HitActors.Contains(HitActor))
		{
			continue;
		}

		UTDCombatComponent* Combatant = HitActor->FindComponentByClass<UTDCombatComponent>();
		if (!Combatant || !DamageSubsystem->CanTarget(Combatant, State.DamageContext, TargetPolicy))
		{
			continue;
		}

		State.HitActors.Add(HitActor);
		QueryParams.AddIgnoredActor(HitActor);
		const FVector HitLocation = Hit.ImpactPoint.ContainsNaN() ? HitActor->GetActorLocation() : FVector(Hit.ImpactPoint);
#if ENABLE_DRAW_DEBUG
		if (bDrawDebugSweep)
		{
			DrawDebugSphere(World, HitLocation, SweepRadius * 2.f, 12, FColor::Green, false, DebugDrawDuration, 0, 1.f);
		}
#endif
		DamageSubsystem->ExecuteRules(HitRules, ETDDamageEvent::Hit, State.DamageContext, HitActor, HitLocation);
	}
}

bool UTDAnimNotifyState_MeleeAttack::ShouldLockRootBone(const UAnimInstance* AnimInstance, const UAnimSequenceBase* Animation) const
{
	if (!AnimInstance)
	{
		return true;
	}
	if (AnimInstance->ShouldExtractRootMotion())
	{
		return true;
	}

	const UAnimMontage* Montage = Cast<UAnimMontage>(Animation);
	const FAnimMontageInstance* RootMotionMontageInstance = AnimInstance->GetRootMotionMontageInstance();
	return Montage && RootMotionMontageInstance && RootMotionMontageInstance->Montage == Montage;
}

void UTDAnimNotifyState_MeleeAttack::RemoveStaleStates()
{
	for (auto It = SweepStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
