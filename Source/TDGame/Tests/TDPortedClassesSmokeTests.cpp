#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Actors/TDCaravanActor.h"
#include "AI/CombatToken/TDCombatTokenSubsystem.h"
#include "Characters/TDGameCharacter.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Performance/BudgetTick/TDBudgetTickSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPortedClassesSmokeTest, "TDGame.Smoke.PortedClassesExist", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPortedClassesSmokeTest::RunTest(const FString& Parameters)
{
	TestNotNull(TEXT("Player character class is registered"), ATDGameCharacter::StaticClass());
	TestNotNull(TEXT("Caravan actor class is registered"), ATDCaravanActor::StaticClass());
	TestNotNull(TEXT("Budget tick subsystem class is registered"), UTDBudgetTickSubsystem::StaticClass());
	TestNotNull(TEXT("Skill component class is registered"), UTDSkillComponent::StaticClass());
	TestNotNull(TEXT("Combat token subsystem class is registered"), UTDCombatTokenSubsystem::StaticClass());
	return true;
}

#endif
