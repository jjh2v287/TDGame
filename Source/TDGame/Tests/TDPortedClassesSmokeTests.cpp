#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Actors/TDCaravanActor.h"
#include "AI/CombatToken/TDCombatTokenSubsystem.h"
#include "Backends/MoverStandaloneLiaison.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDGameCharacter.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Component/AnimNextComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "Performance/BudgetTick/TDBudgetTickSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPortedClassesSmokeTest, "TDGame.Smoke.PortedClassesExist", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPortedClassesSmokeTest::RunTest(const FString& Parameters)
{
	TestNotNull(TEXT("Player character class is registered"), ATDGameCharacter::StaticClass());
	TestNotNull(TEXT("Caravan actor class is registered"), ATDCaravanActor::StaticClass());
	TestNotNull(TEXT("Budget tick subsystem class is registered"), UTDBudgetTickSubsystem::StaticClass());
	TestNotNull(TEXT("Skill component class is registered"), UTDSkillComponent::StaticClass());
	TestNotNull(TEXT("Combat token subsystem class is registered"), UTDCombatTokenSubsystem::StaticClass());

	const ATDGameCharacter* CharacterCDO = GetDefault<ATDGameCharacter>();
	if (!TestNotNull(TEXT("Player character default object exists"), CharacterCDO))
	{
		return false;
	}

	const UCharacterMoverComponent* MoverComponent = CharacterCDO->GetMoverComponent();
	if (TestNotNull(TEXT("Player character has a Mover component"), MoverComponent))
	{
		TestTrue(TEXT("Mover uses the standalone backend"), MoverComponent->BackendClass == UMoverStandaloneLiaisonComponent::StaticClass());
		TestTrue(TEXT("Mover accepts external movement"), MoverComponent->bAcceptExternalMovement != 0);
	}

	const UNavMoverComponent* NavMoverComponent = CharacterCDO->GetNavMoverComponent();
	if (TestNotNull(TEXT("Player character has a NavMover component"), NavMoverComponent))
	{
		TestTrue(TEXT("bUseAccelerationForPaths is enabled"), NavMoverComponent->GetNavMovementProperties().bUseAccelerationForPaths);
	}

	TestNotNull(TEXT("Player character has a UAF component"), CharacterCDO->GetUAFComponent());
	TestNotNull(TEXT("Player character has an animation component"), CharacterCDO->GetCharacterAnimation());

	const USkeletalMeshComponent* Mesh = CharacterCDO->GetMesh();
	if (TestNotNull(TEXT("Player character has a mesh"), Mesh))
	{
		TestFalse(TEXT("Mesh built-in animation is disabled"), Mesh->bEnableAnimation != 0);
	}
	return true;
}

#endif
