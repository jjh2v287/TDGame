#include "Combat/Characters/TDCompanionCharacter.h"
#include "AIController.h"
#include "Combat/TDCombatComponent.h"

ATDCompanionCharacter::ATDCompanionCharacter()
{
	FTDCombatStats CompanionStats;
	CompanionStats.TeamId = 1;
	CombatComponent->SetStats(CompanionStats);
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();
}
