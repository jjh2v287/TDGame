#include "Characters/TDCapsuleModifierComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"

UTDCapsuleModifierComponent::UTDCapsuleModifierComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTDCapsuleModifierComponent::BeginPlay()
{
	Super::BeginPlay();

	CacheDefaultCapsuleSize();
	RefreshCapsuleSize();
}

void UTDCapsuleModifierComponent::SetJumpModifierEnabled(bool bEnabled)
{
	if (bIsJumpModifierEnabled == bEnabled)
	{
		return;
	}

	bIsJumpModifierEnabled = bEnabled;
	RefreshCapsuleSize();
}

void UTDCapsuleModifierComponent::SetRollModifierEnabled(bool bEnabled)
{
	if (bIsRollModifierEnabled == bEnabled)
	{
		return;
	}

	bIsRollModifierEnabled = bEnabled;
	RefreshCapsuleSize();
}

void UTDCapsuleModifierComponent::ResetModifiers()
{
	bIsJumpModifierEnabled = false;
	bIsRollModifierEnabled = false;
	RefreshCapsuleSize();
}

void UTDCapsuleModifierComponent::CacheDefaultCapsuleSize()
{
	if (AActor* Owner = GetOwner())
	{
		UCapsuleComponent* Capsule = Owner->FindComponentByClass<UCapsuleComponent>();
		OwnerCapsule = Capsule;
		if (!Capsule)
		{
			return;
		}

		DefaultCapsuleRadius = Capsule->GetUnscaledCapsuleRadius();
		DefaultCapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
		DefaultCapsuleScaleZ = FMath::Max(Capsule->GetComponentScale().Z, UE_KINDA_SMALL_NUMBER);
	}

	if (USkeletalMeshComponent* Mesh = GetOwnerMesh())
	{
		OwnerMesh = Mesh;
		DefaultMeshRelativeLocation = Mesh->GetRelativeLocation();
	}
}

void UTDCapsuleModifierComponent::RefreshCapsuleSize()
{
	UCapsuleComponent* Capsule = GetOwnerCapsule();
	if (!Capsule)
	{
		return;
	}

	if (DefaultCapsuleRadius <= 0.f || DefaultCapsuleHalfHeight <= 0.f)
	{
		CacheDefaultCapsuleSize();
	}

	float TargetRadius = DefaultCapsuleRadius;
	float TargetHalfHeight = DefaultCapsuleHalfHeight;

	if (bIsJumpModifierEnabled)
	{
		TargetRadius = ResolveRadius(JumpCapsuleRadius);
		TargetHalfHeight = ResolveHalfHeight(JumpCapsuleHalfHeight, TargetRadius);
	}

	if (bIsRollModifierEnabled)
	{
		TargetRadius = ResolveRadius(RollCapsuleRadius);
		TargetHalfHeight = ResolveHalfHeight(RollCapsuleHalfHeight, TargetRadius);
	}

	const bool bHasActiveModifier = bIsJumpModifierEnabled || bIsRollModifierEnabled;
	const bool bShouldMaintainCapsuleBase = bIsRollModifierEnabled || (!bHasActiveModifier && bIsMaintainCapsuleBaseApplied);

	if (bIsMaintainCapsuleBaseApplied && !bShouldMaintainCapsuleBase)
	{
		RestoreOwnerFromBaseMaintainedState();
	}

	const float CurrentScaledHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float TargetScaledHalfHeight = TargetHalfHeight * Capsule->GetShapeScale();
	const float HalfHeightAdjustment = TargetScaledHalfHeight - CurrentScaledHalfHeight;

	if (FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), TargetRadius) &&
		FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), TargetHalfHeight))
	{
		RefreshMeshOffset(TargetScaledHalfHeight, bShouldMaintainCapsuleBase);
		bIsMaintainCapsuleBaseApplied = bShouldMaintainCapsuleBase;
		return;
	}

	if (!bShouldMaintainCapsuleBase)
	{
		Capsule->SetCapsuleSize(TargetRadius, TargetHalfHeight, true);
		RefreshMeshOffset(TargetScaledHalfHeight, false);
		bIsMaintainCapsuleBaseApplied = false;
		return;
	}

	if (HalfHeightAdjustment > 0.f)
	{
		MoveOwnerToMaintainCapsuleBase(HalfHeightAdjustment);
		Capsule->SetCapsuleSize(TargetRadius, TargetHalfHeight, true);
		RefreshMeshOffset(TargetScaledHalfHeight, true);
		bIsMaintainCapsuleBaseApplied = true;
		return;
	}

	Capsule->SetCapsuleSize(TargetRadius, TargetHalfHeight, true);
	MoveOwnerToMaintainCapsuleBase(HalfHeightAdjustment);
	RefreshMeshOffset(TargetScaledHalfHeight, true);
	bIsMaintainCapsuleBaseApplied = true;
}

UCapsuleComponent* UTDCapsuleModifierComponent::GetOwnerCapsule() const
{
	if (UCapsuleComponent* Capsule = OwnerCapsule.Get())
	{
		return Capsule;
	}

	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UCapsuleComponent>() : nullptr;
}

USkeletalMeshComponent* UTDCapsuleModifierComponent::GetOwnerMesh() const
{
	if (USkeletalMeshComponent* Mesh = OwnerMesh.Get())
	{
		return Mesh;
	}

	if (const ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		return CharacterOwner->GetMesh();
	}

	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

float UTDCapsuleModifierComponent::ResolveRadius(float RequestedRadius) const
{
	return RequestedRadius > 0.f ? RequestedRadius : DefaultCapsuleRadius;
}

float UTDCapsuleModifierComponent::ResolveHalfHeight(float RequestedHalfHeight, float ResolvedRadius) const
{
	const float HalfHeight = RequestedHalfHeight > 0.f ? RequestedHalfHeight : DefaultCapsuleHalfHeight;
	return FMath::Max(HalfHeight, ResolvedRadius);
}

void UTDCapsuleModifierComponent::MoveOwnerToMaintainCapsuleBase(float HalfHeightAdjustment) const
{
	if (FMath::IsNearlyZero(HalfHeightAdjustment))
	{
		return;
	}

	if (AActor* Owner = GetOwner())
	{
		Owner->AddActorWorldOffset(FVector(0.f, 0.f, HalfHeightAdjustment), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UTDCapsuleModifierComponent::RefreshMeshOffset(float TargetScaledHalfHeight, bool bMaintainCapsuleBase) const
{
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	if (!Mesh)
	{
		return;
	}

	FVector MeshRelativeLocation = DefaultMeshRelativeLocation;
	if (bMaintainCapsuleBase)
	{
		const float MeshOffsetZ = (DefaultCapsuleHalfHeight * DefaultCapsuleScaleZ) - TargetScaledHalfHeight;
		MeshRelativeLocation.Z += MeshOffsetZ / DefaultCapsuleScaleZ;
	}

	Mesh->SetRelativeLocation(MeshRelativeLocation);
}

void UTDCapsuleModifierComponent::RestoreOwnerFromBaseMaintainedState() const
{
	UCapsuleComponent* Capsule = GetOwnerCapsule();
	if (!Capsule)
	{
		return;
	}

	const float DefaultScaledHalfHeight = DefaultCapsuleHalfHeight * Capsule->GetShapeScale();
	const float CurrentScaledHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	MoveOwnerToMaintainCapsuleBase(DefaultScaledHalfHeight - CurrentScaledHalfHeight);
	RefreshMeshOffset(CurrentScaledHalfHeight, false);
}
