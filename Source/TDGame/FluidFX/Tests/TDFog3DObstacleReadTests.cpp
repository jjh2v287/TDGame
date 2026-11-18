#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidObstacleComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFog3DObstacleReadTestPrivate
{
	struct FTDFog3DObstacleTestWorldScope
	{
		FTDFog3DObstacleTestWorldScope()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFog3DObstacleReadTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFog3DObstacleTestWorldScope()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UWorld* World = nullptr;
	};

	AActor* SpawnObstacleActor(UWorld& World, const FVector& Location, const FRotator& Rotation, const FVector& HalfExtentsCm, bool bWithRoot)
	{
		AActor* Actor = World.SpawnActor<AActor>(Location, Rotation);
		if (!Actor)
		{
			return nullptr;
		}
		if (bWithRoot)
		{
			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Root->SetWorldLocationAndRotation(Location, Rotation);
		}
		UTDFluidObstacleComponent* Obstacle = NewObject<UTDFluidObstacleComponent>(Actor, TEXT("Obstacle"));
		Obstacle->bUseOwnerBounds = false;
		Obstacle->BoxHalfExtentsCm = HalfExtentsCm;
		Obstacle->RegisterComponent();
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DSubsystemObstacleReadTest,
	"TDGame.FluidFX.Fog3D.SubsystemObstacleRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DSubsystemObstacleReadTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DObstacleReadTestPrivate;

	FTDFog3DObstacleTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	if (!TestNotNull(TEXT("Fluid subsystem exists in the test world"), Subsystem))
	{
		return false;
	}

	TArray<FTDFluidSdfObstacle> Obstacles;
	Subsystem->CollectRegisteredObstacles(Obstacles);
	TestEqual(TEXT("An empty world has no obstacles"), Obstacles.Num(), 0);

	AActor* FirstActor = SpawnObstacleActor(*TestWorld.World, FVector(500.0, -400.0, 15.0), FRotator::ZeroRotator, FVector(75.0, 75.0, 15.0), true);
	AActor* SecondActor = SpawnObstacleActor(*TestWorld.World, FVector(500.0, 400.0, 90.0), FRotator(0.0, 30.0, 0.0), FVector(75.0, 75.0, 90.0), true);
	AActor* ThirdActor = SpawnObstacleActor(*TestWorld.World, FVector(1100.0, 0.0, 125.0), FRotator::ZeroRotator, FVector(5.0, 150.0, 125.0), true);
	if (!TestTrue(TEXT("Three obstacle actors spawn"), FirstActor && SecondActor && ThirdActor))
	{
		return false;
	}
	TestEqual(TEXT("The subsystem registered three sources"), Subsystem->GetObstacleSourceCount(), 3);

	Subsystem->CollectRegisteredObstacles(Obstacles);
	if (!TestEqual(TEXT("Three obstacles are read"), Obstacles.Num(), 3))
	{
		return false;
	}
	TestTrue(TEXT("First obstacle center follows registration order"), Obstacles[0].WorldCenter.Equals(FVector(500.0, -400.0, 15.0), 0.01));
	TestTrue(TEXT("Second obstacle center follows registration order"), Obstacles[1].WorldCenter.Equals(FVector(500.0, 400.0, 90.0), 0.01));
	TestTrue(TEXT("Third obstacle center follows registration order"), Obstacles[2].WorldCenter.Equals(FVector(1100.0, 0.0, 125.0), 0.01));
	TestTrue(TEXT("Obstacles are boxes"), Obstacles[0].bIsBox && Obstacles[1].bIsBox && Obstacles[2].bIsBox);
	TestTrue(TEXT("Third obstacle keeps its three axis half sizes"), Obstacles[2].BoxHalfExtents3DCm.Equals(FVector(5.0, 150.0, 125.0), 0.01));
	TestTrue(TEXT("Second obstacle keeps its yaw"), FMath::IsNearlyEqual(Obstacles[1].WorldRotation.Yaw, 30.0, 0.01));

	TArray<FTDFluidSdfObstacle> SecondRead;
	Subsystem->CollectRegisteredObstacles(SecondRead);
	TestEqual(TEXT("Reading twice gives the same count"), SecondRead.Num(), Obstacles.Num());
	TestEqual(TEXT("Reading does not change the registered source count"), Subsystem->GetObstacleSourceCount(), 3);

	SecondActor->Destroy();
	Subsystem->CollectRegisteredObstacles(Obstacles);
	if (!TestEqual(TEXT("A destroyed actor leaves two obstacles"), Obstacles.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("The survivors keep registration order (first)"), Obstacles[0].WorldCenter.Equals(FVector(500.0, -400.0, 15.0), 0.01));
	TestTrue(TEXT("The survivors keep registration order (third)"), Obstacles[1].WorldCenter.Equals(FVector(1100.0, 0.0, 125.0), 0.01));

	AActor* RootlessActor = SpawnObstacleActor(*TestWorld.World, FVector(0.0, 0.0, 0.0), FRotator::ZeroRotator, FVector(50.0, 50.0, 50.0), false);
	TestNotNull(TEXT("A rootless obstacle actor spawns"), RootlessActor);
	Subsystem->CollectRegisteredObstacles(Obstacles);
	TestEqual(TEXT("A source that cannot build an obstacle is skipped"), Obstacles.Num(), 2);

	Obstacles.Add(FTDFluidSdfObstacle());
	Subsystem->CollectRegisteredObstacles(Obstacles);
	TestEqual(TEXT("The output list is reset before reading"), Obstacles.Num(), 2);

	UTDFluidObstacleComponent* OwnedSource = NewObject<UTDFluidObstacleComponent>(FirstActor, NAME_None);
	OwnedSource->bUseOwnerBounds = false;
	OwnedSource->BoxHalfExtentsCm = FVector(20.0, 20.0, 20.0);
	Subsystem->RegisterObstacleSource(OwnedSource);
	Subsystem->CollectRegisteredObstacles(Obstacles);
	if (!TestEqual(TEXT("A live source with an owner adds one obstacle"), Obstacles.Num(), 3))
	{
		return false;
	}
	OwnedSource->MarkAsGarbage();
	Subsystem->CollectRegisteredObstacles(Obstacles);
	TestEqual(TEXT("The obstacle count returns to two once the owned source is garbage"), Obstacles.Num(), 2);

	UTDFluidObstacleComponent* DeadSource = NewObject<UTDFluidObstacleComponent>(GetTransientPackage(), NAME_None);
	Subsystem->RegisterObstacleSource(DeadSource);
	const int32 SourceCountWhileAlive = Subsystem->GetObstacleSourceCount();
	DeadSource->MarkAsGarbage();
	TestEqual(TEXT("A source whose component died is no longer counted"), Subsystem->GetObstacleSourceCount(), SourceCountWhileAlive - 1);
	Subsystem->CollectRegisteredObstacles(Obstacles);
	TestEqual(TEXT("A registration whose component died is skipped"), Obstacles.Num(), 2);
	TestTrue(TEXT("The survivors are unchanged after a dead registration (first)"), Obstacles.Num() == 2 && Obstacles[0].WorldCenter.Equals(FVector(500.0, -400.0, 15.0), 0.01));
	TestTrue(TEXT("The survivors are unchanged after a dead registration (third)"), Obstacles.Num() == 2 && Obstacles[1].WorldCenter.Equals(FVector(1100.0, 0.0, 125.0), 0.01));
	return true;
}

#endif
