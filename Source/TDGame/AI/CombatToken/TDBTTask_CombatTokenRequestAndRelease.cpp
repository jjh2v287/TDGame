#include "AI/CombatToken/TDBTTask_CombatTokenRequestAndRelease.h"
#include "AI/CombatToken/TDCombatTokenSubsystem.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
	UTDCombatTokenSubsystem* ResolveCombatTokenSubsystem(const UBehaviorTreeComponent& OwnerComp)
	{
		const UWorld* World = OwnerComp.GetWorld();
		return World ? UGameInstance::GetSubsystem<UTDCombatTokenSubsystem>(World->GetGameInstance()) : nullptr;
	}

	APawn* ResolveOwnerPawn(const UBehaviorTreeComponent& OwnerComp)
	{
		const AAIController* AIController = OwnerComp.GetAIOwner();
		return AIController ? AIController->GetPawn() : nullptr;
	}
}

UTDBTTask_CombatTokenRequestAndRelease::UTDBTTask_CombatTokenRequestAndRelease(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = "TD Combat Token Request And Release";
}

EBTNodeResult::Type UTDBTTask_CombatTokenRequestAndRelease::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UTDCombatTokenSubsystem* TokenSubsystem = ResolveCombatTokenSubsystem(OwnerComp);
	if (!TokenSubsystem)
	{
		return EBTNodeResult::Succeeded;
	}

	if (TokenSubsystem->RequestToken(ResolveOwnerPawn(OwnerComp)) == INDEX_NONE)
	{
		return EBTNodeResult::Failed;
	}

	return EBTNodeResult::Succeeded;
}

void UTDBTTask_CombatTokenRequestAndRelease::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);

	if (UTDCombatTokenSubsystem* TokenSubsystem = ResolveCombatTokenSubsystem(OwnerComp))
	{
		TokenSubsystem->ReleaseTokenByUser(ResolveOwnerPawn(OwnerComp));
	}
}

EBTNodeResult::Type UTDBTTask_CombatTokenRequestAndRelease::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (UTDCombatTokenSubsystem* TokenSubsystem = ResolveCombatTokenSubsystem(OwnerComp))
	{
		TokenSubsystem->ReleaseTokenByUser(ResolveOwnerPawn(OwnerComp));
	}

	return Super::AbortTask(OwnerComp, NodeMemory);
}
