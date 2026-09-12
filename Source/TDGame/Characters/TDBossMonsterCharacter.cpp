#include "Characters/TDBossMonsterCharacter.h"

#include "Characters/TDPartComponent.h"
#include "Components/SkeletalMeshComponent.h"

ATDBossMonsterCharacter::ATDBossMonsterCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTDPartComponent>(ACharacter::MeshComponentName))
{
}

USkeletalMeshComponent* ATDBossMonsterCharacter::GetPart(FName BoneName)
{
	TArray<USkeletalMeshComponent*> MeshComponents = { GetMesh() };
	TArray<USceneComponent*> ChildComponents;
	GetMesh()->GetChildrenComponents(false, ChildComponents);

	for (USceneComponent* Child : ChildComponents)
	{
		if (USkeletalMeshComponent* MeshComponent = Cast<USkeletalMeshComponent>(Child))
		{
			MeshComponents.Emplace(MeshComponent);
		}
	}

	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		const UTDPartComponent* Part = Cast<UTDPartComponent>(MeshComponent);
		if (Part && Part->IsBoneWeighted(BoneName))
		{
			return MeshComponent;
		}
	}

	return nullptr;
}
