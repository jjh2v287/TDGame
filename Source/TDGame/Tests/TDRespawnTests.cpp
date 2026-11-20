#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Characters/TDMonsterCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/TDGameGameMode.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDRespawnTests
{
	const FVector RespawnCheckpointLocation(0.f, 0.f, 100.f);
	const FVector RespawnPlayerStartLocation(1000.f, 0.f, 100.f);
	constexpr float RespawnFloorHalfThicknessCm = 50.f;
	constexpr float RespawnPinchDepthCm = 10.f;
	constexpr float RespawnPinchHalfWidthCm = 50.f;
	constexpr float RespawnLowPinchTopAboveFeetCm = 118.f;
	constexpr float RespawnTallPinchTopCm = 600.f;
	constexpr float RespawnCheckpointReachCm = 30.f;
	constexpr float RespawnPlayerStartReachCm = 100.f;
	constexpr float RespawnExactToleranceCm = 1.f;
	const TCHAR* const RespawnPlayerStartFallbackLog = TEXT("using player start");
	const TCHAR* const RespawnBlockedSpotFallbackLog = TEXT("spawning there anyway");

	struct FTDRespawnTestWorld
	{
		FTDRespawnTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDRespawnTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			SpawnBlock(FVector(0.f, 0.f, -RespawnFloorHalfThicknessCm), FVector(5000.f, 5000.f, RespawnFloorHalfThicknessCm));
			GameMode = SpawnConcreteGameMode();
			Controller = World->SpawnActor<AAIController>();
		}

		~FTDRespawnTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ATDGameGameMode* SpawnConcreteGameMode() const
		{
			UClass* GameModeClass = ATDGameGameMode::StaticClass();
			const bool bWasAbstract = GameModeClass->HasAnyClassFlags(CLASS_Abstract);
			GameModeClass->ClassFlags &= ~CLASS_Abstract;
			ATDGameGameMode* SpawnedGameMode = World->SpawnActor<ATDGameGameMode>(GameModeClass);
			if (bWasAbstract)
			{
				GameModeClass->ClassFlags |= CLASS_Abstract;
			}
			if (SpawnedGameMode)
			{
				SpawnedGameMode->DefaultPawnClass = ATDMonsterCharacter::StaticClass();
			}
			return SpawnedGameMode;
		}

		AActor* SpawnBlock(const FVector& Center, const FVector& Extent) const
		{
			AActor* Block = World->SpawnActor<AActor>();
			UBoxComponent* BlockBox = NewObject<UBoxComponent>(Block);
			Block->AddInstanceComponent(BlockBox);
			Block->SetRootComponent(BlockBox);
			BlockBox->SetBoxExtent(Extent);
			BlockBox->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			BlockBox->RegisterComponent();
			Block->SetActorLocation(Center);
			return Block;
		}

		static const UCapsuleComponent* GetPawnCapsuleDefaults()
		{
			const ATDMonsterCharacter* PawnDefaults = GetDefault<ATDMonsterCharacter>();
			return PawnDefaults ? PawnDefaults->GetCapsuleComponent() : nullptr;
		}

		static float GetLowPinchTopZ()
		{
			const UCapsuleComponent* Capsule = GetPawnCapsuleDefaults();
			const float HalfHeight = Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 88.f;
			return static_cast<float>(RespawnCheckpointLocation.Z) - HalfHeight + RespawnLowPinchTopAboveFeetCm;
		}

		void SpawnPinch(const float TopZ) const
		{
			const UCapsuleComponent* Capsule = GetPawnCapsuleDefaults();
			const float CapsuleRadius = Capsule ? Capsule->GetUnscaledCapsuleRadius() : 34.f;
			const FVector Extent(RespawnPinchHalfWidthCm, 100.f, TopZ * 0.5f);
			const float CenterOffsetX = CapsuleRadius - RespawnPinchDepthCm + RespawnPinchHalfWidthCm;
			SpawnBlock(FVector(RespawnCheckpointLocation.X + CenterOffsetX, RespawnCheckpointLocation.Y, Extent.Z), Extent);
			SpawnBlock(FVector(RespawnCheckpointLocation.X - CenterOffsetX, RespawnCheckpointLocation.Y, Extent.Z), Extent);
		}

		APawn* Respawn() const
		{
			GameMode->RestartPlayerAtTransform(Controller, FTransform(FRotator::ZeroRotator, RespawnCheckpointLocation));
			return Controller->GetPawn();
		}

		bool IsPawnCapsuleBlocked(const APawn& Pawn) const
		{
			const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Pawn.GetRootComponent());
			if (!Capsule)
			{
				return true;
			}
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDRespawnTestOverlap), false, &Pawn);
			FCollisionResponseParams ResponseParams;
			Capsule->InitSweepCollisionParams(QueryParams, ResponseParams);
			return World->OverlapBlockingTestByChannel(Capsule->GetComponentLocation(), Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), QueryParams, ResponseParams);
		}

		bool IsReady(FAutomationTestBase& Test) const
		{
			return Test.TestNotNull(TEXT("The concrete test game mode spawns"), GameMode) && Test.TestNotNull(TEXT("The respawning controller spawns"), Controller);
		}

		UWorld* World = nullptr;
		ATDGameGameMode* GameMode = nullptr;
		AAIController* Controller = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRespawnPinchedCheckpointTest, "TDGame.Framework.Respawn.PinchedCheckpointStillSpawnsPawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRespawnPinchedCheckpointTest::RunTest(const FString& Parameters)
{
	using namespace TDRespawnTests;
	FTDRespawnTestWorld Fixture;
	if (!Fixture.IsReady(*this))
	{
		return false;
	}
	Fixture.SpawnPinch(FTDRespawnTestWorld::GetLowPinchTopZ());
	APawn* Pawn = Fixture.Respawn();
	if (!TestNotNull(TEXT("A checkpoint pinched from both sides still gives the controller a pawn"), Pawn))
	{
		return false;
	}
	TestTrue(TEXT("The lifted pawn stays above the checkpoint"), FVector::Dist2D(Pawn->GetActorLocation(), RespawnCheckpointLocation) <= RespawnCheckpointReachCm);
	TestFalse(TEXT("The lifted pawn capsule is clear of the pinching blocks"), Fixture.IsPawnCapsuleBlocked(*Pawn));
	TestTrue(TEXT("The controller possesses the respawned pawn"), Pawn->GetController() == Fixture.Controller);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRespawnFallsBackToPlayerStartTest, "TDGame.Framework.Respawn.FallsBackToPlayerStart", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRespawnFallsBackToPlayerStartTest::RunTest(const FString& Parameters)
{
	using namespace TDRespawnTests;
	FTDRespawnTestWorld Fixture;
	if (!Fixture.IsReady(*this))
	{
		return false;
	}
	Fixture.SpawnPinch(RespawnTallPinchTopCm);
	const APlayerStart* PlayerStart = Fixture.World->SpawnActor<APlayerStart>(RespawnPlayerStartLocation, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The fallback player start spawns"), PlayerStart))
	{
		return false;
	}
	AddExpectedError(RespawnPlayerStartFallbackLog, EAutomationExpectedErrorFlags::Contains, 0);
	APawn* Pawn = Fixture.Respawn();
	if (!TestNotNull(TEXT("A checkpoint blocked at every lift still gives the controller a pawn"), Pawn))
	{
		return false;
	}
	TestTrue(TEXT("The pawn falls back to the player start"), FVector::Dist(Pawn->GetActorLocation(), PlayerStart->GetActorLocation()) <= RespawnPlayerStartReachCm);
	TestFalse(TEXT("The fallback pawn capsule is clear"), Fixture.IsPawnCapsuleBlocked(*Pawn));
	TestTrue(TEXT("The controller possesses the fallback pawn"), Pawn->GetController() == Fixture.Controller);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRespawnNeverLeavesControllerWithoutPawnTest, "TDGame.Framework.Respawn.NeverLeavesControllerWithoutPawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRespawnNeverLeavesControllerWithoutPawnTest::RunTest(const FString& Parameters)
{
	using namespace TDRespawnTests;
	FTDRespawnTestWorld Fixture;
	if (!Fixture.IsReady(*this))
	{
		return false;
	}
	Fixture.SpawnPinch(RespawnTallPinchTopCm);
	AddExpectedError(RespawnBlockedSpotFallbackLog, EAutomationExpectedErrorFlags::Contains, 0);
	APawn* Pawn = Fixture.Respawn();
	if (!TestNotNull(TEXT("A blocked checkpoint without any player start still gives the controller a pawn"), Pawn))
	{
		return false;
	}
	TestTrue(TEXT("The controller possesses the last-resort pawn"), Pawn->GetController() == Fixture.Controller);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRespawnClearCheckpointTest, "TDGame.Framework.Respawn.ClearCheckpointSpawnsExactly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRespawnClearCheckpointTest::RunTest(const FString& Parameters)
{
	using namespace TDRespawnTests;
	FTDRespawnTestWorld Fixture;
	if (!Fixture.IsReady(*this))
	{
		return false;
	}
	APawn* Pawn = Fixture.Respawn();
	if (!TestNotNull(TEXT("A clear checkpoint gives the controller a pawn"), Pawn))
	{
		return false;
	}
	TestTrue(TEXT("A clear checkpoint is used exactly"), Pawn->GetActorLocation().Equals(RespawnCheckpointLocation, RespawnExactToleranceCm));
	TestTrue(TEXT("The controller possesses the pawn"), Pawn->GetController() == Fixture.Controller);
	return true;
}

#endif
