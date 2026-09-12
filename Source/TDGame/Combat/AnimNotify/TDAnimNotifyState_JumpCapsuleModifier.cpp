#include "Combat/AnimNotify/TDAnimNotifyState_JumpCapsuleModifier.h"

#include "Characters/TDCapsuleModifierComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace
{
UTDCapsuleModifierComponent* FindOwnerCapsuleModifier(const USkeletalMeshComponent* MeshComp)
{
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	return Owner ? Owner->FindComponentByClass<UTDCapsuleModifierComponent>() : nullptr;
}
}

void UTDAnimNotifyState_JumpCapsuleModifier::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (UTDCapsuleModifierComponent* CapsuleModifier = FindOwnerCapsuleModifier(MeshComp))
	{
		CapsuleModifier->SetJumpModifierEnabled(true);
	}
}

void UTDAnimNotifyState_JumpCapsuleModifier::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (UTDCapsuleModifierComponent* CapsuleModifier = FindOwnerCapsuleModifier(MeshComp))
	{
		CapsuleModifier->SetJumpModifierEnabled(false);
	}
}

FString UTDAnimNotifyState_JumpCapsuleModifier::GetNotifyName_Implementation() const
{
	return TEXT("TD Jump Capsule Modifier");
}
