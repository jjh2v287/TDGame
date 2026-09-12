#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "TDBTTask_CombatTokenRequestAndRelease.generated.h"

UCLASS()
class TDGAME_API UTDBTTask_CombatTokenRequestAndRelease : public UBTTaskNode
{
	GENERATED_BODY()

public:
	explicit UTDBTTask_CombatTokenRequestAndRelease(const FObjectInitializer& ObjectInitializer);

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
