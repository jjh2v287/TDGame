#include "Combat/Characters/TDMonsterCharacter.h"
#include "AIController.h"
#include "Combat/TDCombatComponent.h"

ATDMonsterCharacter::ATDMonsterCharacter()
{
	FTDCombatStats MonsterStats;
	MonsterStats.TeamId = 2;
	CombatComponent->SetStats(MonsterStats);
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();
}
