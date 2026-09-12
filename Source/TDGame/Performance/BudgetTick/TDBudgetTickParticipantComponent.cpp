#include "Performance/BudgetTick/TDBudgetTickParticipantComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Performance/BudgetTick/TDBudgetTickSubsystem.h"
#include "Performance/BudgetTick/TDBudgetTickable.h"

UTDBudgetTickParticipantComponent::UTDBudgetTickParticipantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTDBudgetTickParticipantComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterWithScheduler();
}

void UTDBudgetTickParticipantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterWithScheduler();
	Super::EndPlay(EndPlayReason);
}

void UTDBudgetTickParticipantComponent::SetRuntimeImportanceBias(const float InBias)
{
	RuntimeImportanceBias = InBias;
}

float UTDBudgetTickParticipantComponent::GetEffectiveImportance() const
{
	return BaseImportance + RuntimeImportanceBias;
}

void UTDBudgetTickParticipantComponent::RegisterWithScheduler()
{
	AActor* OwnerActor = GetOwner();
	if (!bEnableBudgetTick || !IsValid(OwnerActor) || !Cast<ITDBudgetTickable>(OwnerActor))
	{
		return;
	}

	UTDBudgetTickSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UTDBudgetTickSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	bIsRegisteredWithScheduler = Subsystem->RegisterParticipant(OwnerActor, this);
	if (bIsRegisteredWithScheduler)
	{
		ApplyManagedTickState(true);
	}
}

void UTDBudgetTickParticipantComponent::UnregisterWithScheduler()
{
	AActor* OwnerActor = GetOwner();
	if (!bIsRegisteredWithScheduler || !IsValid(OwnerActor))
	{
		ApplyManagedTickState(false);
		bIsRegisteredWithScheduler = false;
		return;
	}

	if (UTDBudgetTickSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UTDBudgetTickSubsystem>() : nullptr)
	{
		Subsystem->UnregisterParticipant(OwnerActor);
	}

	ApplyManagedTickState(false);
	bIsRegisteredWithScheduler = false;
}

void UTDBudgetTickParticipantComponent::ApplyManagedTickState(const bool bEnableManagement)
{
	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor) || !bDisableOwnerTickWhileManaged)
	{
		return;
	}

	if (bEnableManagement)
	{
		if (!bHasSavedOwnerTickState)
		{
			bSavedOwnerTickEnabled = OwnerActor->IsActorTickEnabled();
			bHasSavedOwnerTickState = true;
		}

		if (OwnerActor->PrimaryActorTick.bCanEverTick)
		{
			OwnerActor->SetActorTickEnabled(false);
		}
		return;
	}

	if (bHasSavedOwnerTickState && OwnerActor->PrimaryActorTick.bCanEverTick)
	{
		OwnerActor->SetActorTickEnabled(bSavedOwnerTickEnabled);
		bHasSavedOwnerTickState = false;
	}
}
