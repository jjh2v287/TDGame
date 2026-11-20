#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimationAsset.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/TDGameCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace TDCameraTeleportSnapTests
{
	constexpr float FloorHalfThickness = 50.f;
	constexpr float PlayerSpawnHeight = 98.f;
	constexpr float CameraLagSpeed = 10.f;
	constexpr float ToleranceCm = 1.f;
	constexpr float FrameSeconds = 1.f / 60.f;
	const FVector FirstDestination(3000.f, -2000.f, PlayerSpawnHeight);
	const FVector SecondDestination(-3000.f, 2000.f, PlayerSpawnHeight);
	const TCHAR* const SnapVariableName = TEXT("TD.Camera.SnapOnTeleport");

	struct FTDCameraSnapWorld
	{
		FTDCameraSnapWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDCameraSnapTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			AActor* FloorActor = World->SpawnActor<AActor>();
			UBoxComponent* Floor = NewObject<UBoxComponent>(FloorActor);
			FloorActor->AddInstanceComponent(Floor);
			FloorActor->SetRootComponent(Floor);
			Floor->SetBoxExtent(FVector(5000.f, 5000.f, FloorHalfThickness));
			Floor->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Floor->RegisterComponent();
			FloorActor->SetActorLocation(FVector(0.f, 0.f, -FloorHalfThickness));
		}

		~FTDCameraSnapWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ATDGameCharacter* SpawnPlayer() const
		{
			FString GameModePath;
			GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), GameModePath, GEngineIni);
			const UClass* GameModeClass = LoadClass<AGameModeBase>(nullptr, *GameModePath);
			const AGameModeBase* GameModeDefaults = GameModeClass ? GameModeClass->GetDefaultObject<AGameModeBase>() : nullptr;
			UClass* PlayerPawnClass = GameModeDefaults ? GameModeDefaults->DefaultPawnClass.Get() : nullptr;
			if (!PlayerPawnClass || !PlayerPawnClass->IsChildOf(ATDGameCharacter::StaticClass()))
			{
				return nullptr;
			}
			const UObject* PlayerPawnDefaults = PlayerPawnClass->GetDefaultObject();
			for (TFieldIterator<FSoftObjectProperty> PropertyIt(PlayerPawnClass); PropertyIt; ++PropertyIt)
			{
				if (PropertyIt->PropertyClass->IsChildOf(UAnimationAsset::StaticClass()))
				{
					PropertyIt->GetPropertyValue_InContainer(PlayerPawnDefaults).LoadSynchronous();
				}
			}
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, PlayerSpawnHeight));
			return World->SpawnActor<ATDGameCharacter>(PlayerPawnClass, SpawnTransform);
		}

		UWorld* World = nullptr;
	};

	struct FTDSnapVariableScope
	{
		explicit FTDSnapVariableScope(int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(SnapVariableName))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetInt();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDSnapVariableScope()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 1;
	};

	FVector ComputeArmOrigin(const USpringArmComponent& Boom)
	{
		return Boom.GetComponentLocation() + Boom.TargetOffset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCameraTeleportSnapsLagTest, "TDGame.Camera.TeleportSnapsCameraLag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCameraTeleportSnapsLagTest::RunTest(const FString& Parameters)
{
	using namespace TDCameraTeleportSnapTests;
	TestNotNull(TEXT("TD.Camera.SnapOnTeleport is registered"), IConsoleManager::Get().FindConsoleVariable(SnapVariableName));
	FTDCameraSnapWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawned"), Player))
	{
		return false;
	}
	USpringArmComponent* Boom = Player->GetCameraBoom();
	if (!TestNotNull(TEXT("Camera boom exists"), Boom))
	{
		return false;
	}
	Boom->bEnableCameraLag = true;
	Boom->CameraLagSpeed = CameraLagSpeed;
	const FVector LagOriginBeforeTeleport = Boom->PreviousDesiredLoc;
	{
		FTDSnapVariableScope SnapOff(0);
		TestTrue(TEXT("Teleport without snap succeeds"), Player->TeleportTo(FirstDestination, FRotator::ZeroRotator));
		TestTrue(TEXT("Without snap the lag origin stays at the old place"), Boom->PreviousDesiredLoc.Equals(LagOriginBeforeTeleport, ToleranceCm));
		TestFalse(TEXT("Without snap the lag origin is not the new arm origin"), Boom->PreviousDesiredLoc.Equals(ComputeArmOrigin(*Boom), ToleranceCm));
	}
	FTDSnapVariableScope SnapOn(1);
	TestTrue(TEXT("Teleport with snap succeeds"), Player->TeleportTo(SecondDestination, FRotator::ZeroRotator));
	TestTrue(TEXT("With snap the lag origin is the new arm origin"), Boom->PreviousDesiredLoc.Equals(ComputeArmOrigin(*Boom), ToleranceCm));
	TestTrue(TEXT("With snap the previous arm origin is the new arm origin"), Boom->PreviousArmOrigin.Equals(ComputeArmOrigin(*Boom), ToleranceCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCameraLargeJumpSnapsLagTest, "TDGame.Camera.LargeJumpWithoutTeleportSnapsCameraLag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCameraLargeJumpSnapsLagTest::RunTest(const FString& Parameters)
{
	using namespace TDCameraTeleportSnapTests;
	FTDCameraSnapWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawned"), Player))
	{
		return false;
	}
	USpringArmComponent* Boom = Player->GetCameraBoom();
	if (!TestNotNull(TEXT("Camera boom exists"), Boom))
	{
		return false;
	}
	Boom->bEnableCameraLag = true;
	Boom->CameraLagSpeed = CameraLagSpeed;
	FTDSnapVariableScope SnapOn(1);
	Player->Tick(FrameSeconds);
	const FVector LagOriginBeforeJump = Boom->PreviousDesiredLoc;
	Player->SetActorLocation(FirstDestination, false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("Moving without TeleportTo leaves the lag origin behind until the next tick"), Boom->PreviousDesiredLoc.Equals(LagOriginBeforeJump, ToleranceCm));
	Player->Tick(FrameSeconds);
	TestTrue(TEXT("The next tick after a large jump snaps the lag origin to the new arm origin"), Boom->PreviousDesiredLoc.Equals(ComputeArmOrigin(*Boom), ToleranceCm));
	const FVector LagOriginAfterSnap = Boom->PreviousDesiredLoc;
	Player->SetActorLocation(FirstDestination + FVector(50.f, 0.f, 0.f), false, nullptr, ETeleportType::None);
	Player->Tick(FrameSeconds);
	TestTrue(TEXT("A small step does not snap the lag origin"), Boom->PreviousDesiredLoc.Equals(LagOriginAfterSnap, ToleranceCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCameraTeleportRequestsCutTest, "TDGame.Camera.PossessAndTeleportRequestCameraCut", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDCameraTeleportRequestsCutTest::RunTest(const FString& Parameters)
{
	using namespace TDCameraTeleportSnapTests;
	FTDCameraSnapWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Player pawn spawned"), Player) || !TestNotNull(TEXT("Player controller spawned"), Controller))
	{
		return false;
	}
	if (!Controller->PlayerCameraManager)
	{
		Controller->SpawnPlayerCameraManager();
	}
	APlayerCameraManager* CameraManager = Controller->PlayerCameraManager;
	if (!TestNotNull(TEXT("Camera manager exists"), CameraManager))
	{
		return false;
	}
	FTDSnapVariableScope SnapOn(1);
	CameraManager->bGameCameraCutThisFrame = false;
	Controller->Possess(Player);
	TestTrue(TEXT("Possession requests a camera cut"), CameraManager->bGameCameraCutThisFrame);
	CameraManager->bGameCameraCutThisFrame = false;
	TestTrue(TEXT("Teleport succeeds"), Player->TeleportTo(FirstDestination, FRotator::ZeroRotator));
	TestTrue(TEXT("Teleport requests a camera cut"), CameraManager->bGameCameraCutThisFrame);
	{
		FTDSnapVariableScope SnapOff(0);
		CameraManager->bGameCameraCutThisFrame = false;
		TestTrue(TEXT("Teleport without snap succeeds"), Player->TeleportTo(SecondDestination, FRotator::ZeroRotator));
		TestFalse(TEXT("Without snap no camera cut is requested"), CameraManager->bGameCameraCutThisFrame);
	}
	return true;
}

#endif
