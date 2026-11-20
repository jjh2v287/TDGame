#include "World/Streaming/TDTexturePrestreamSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "ContentStreaming.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace TDTexturePrestream
{
	static int32 GEnableDestinationPrestream = 1;
	static FAutoConsoleVariableRef CVarEnableDestinationPrestream(
		TEXT("TD.Streaming.Prestream"),
		GEnableDestinationPrestream,
		TEXT("1: before respawn and realm travel, ask the texture streamer for the destination camera view. 0: off."));

	static float GDestinationBoost = 1.5f;
	static FAutoConsoleVariableRef CVarDestinationBoost(
		TEXT("TD.Streaming.PrestreamBoost"),
		GDestinationBoost,
		TEXT("Texture streaming boost for the destination camera view. The engine applies at most r.Streaming.MaxHiddenPrimitiveViewBoost (1.5) to not yet rendered objects, so a larger value only over-requests mips for visible ones."));

	static float GLeadSeconds = 1.5f;
	static FAutoConsoleVariableRef CVarLeadSeconds(
		TEXT("TD.Streaming.LeadSeconds"),
		GLeadSeconds,
		TEXT("Seconds of camera travel to look ahead for texture streaming while the player moves. 0: off."));

	static float GLeadBoost = 1.0f;
	static FAutoConsoleVariableRef CVarLeadBoost(
		TEXT("TD.Streaming.LeadBoost"),
		GLeadBoost,
		TEXT("Texture streaming boost for the look-ahead camera view."));

	constexpr double MaximumContinuousStepCm = 500.0;
	constexpr double MinimumLeadSpeedCmPerSecond = 50.0;
	constexpr double MaximumLeadDistanceCm = 2000.0;
	constexpr float VelocitySmoothingPerSecond = 8.0f;
}

bool UTDTexturePrestreamSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UTDTexturePrestreamSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDTexturePrestreamSubsystem, STATGROUP_Tickables);
}

void UTDTexturePrestreamSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (TDTexturePrestream::GLeadSeconds <= 0.0f)
	{
		bHasLastCameraLocation = false;
		SmoothedCameraVelocity = FVector::ZeroVector;
		return;
	}
	AddLeadViewLocation(DeltaTime);
}

void UTDTexturePrestreamSubsystem::RequestPrestreamAtPawnLocation(const UObject* WorldContextObject, FVector PawnLocation, float Seconds)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UTDTexturePrestreamSubsystem* Subsystem = World ? World->GetSubsystem<UTDTexturePrestreamSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}
	Subsystem->PrestreamAtPawnLocation(PawnLocation, Seconds);
}

void UTDTexturePrestreamSubsystem::PrestreamAtPawnLocation(const FVector& PawnLocation, float Seconds) const
{
	if (TDTexturePrestream::GEnableDestinationPrestream == 0 || Seconds < 0.0f)
	{
		return;
	}
	IStreamingManager::Get().AddViewLocation(PawnLocation + FindCameraOffsetFromPawn(), TDTexturePrestream::GDestinationBoost, false, Seconds);
}

const APlayerController* UTDTexturePrestreamSubsystem::FindLocalPlayerController() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetFirstPlayerController() : nullptr;
}

FVector UTDTexturePrestreamSubsystem::FindCameraOffsetFromPawn() const
{
	const APlayerController* PlayerController = FindLocalPlayerController();
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn || !PlayerController->PlayerCameraManager)
	{
		return FVector::ZeroVector;
	}
	return PlayerController->PlayerCameraManager->GetCameraLocation() - Pawn->GetActorLocation();
}

void UTDTexturePrestreamSubsystem::AddLeadViewLocation(float DeltaTime)
{
	const APlayerController* PlayerController = FindLocalPlayerController();
	if (!PlayerController || !PlayerController->PlayerCameraManager || DeltaTime <= 0.0f)
	{
		bHasLastCameraLocation = false;
		return;
	}
	const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
	const FVector PlanarStep(CameraLocation.X - LastCameraLocation.X, CameraLocation.Y - LastCameraLocation.Y, 0.0);
	const bool bIsContinuous = bHasLastCameraLocation && PlanarStep.Size() <= TDTexturePrestream::MaximumContinuousStepCm;
	LastCameraLocation = CameraLocation;
	bHasLastCameraLocation = true;
	if (!bIsContinuous)
	{
		SmoothedCameraVelocity = FVector::ZeroVector;
		return;
	}
	const float Blend = FMath::Clamp(DeltaTime * TDTexturePrestream::VelocitySmoothingPerSecond, 0.0f, 1.0f);
	SmoothedCameraVelocity = FMath::Lerp(SmoothedCameraVelocity, PlanarStep / DeltaTime, Blend);
	if (SmoothedCameraVelocity.SizeSquared() < FMath::Square(TDTexturePrestream::MinimumLeadSpeedCmPerSecond))
	{
		return;
	}
	const FVector Lead = (SmoothedCameraVelocity * TDTexturePrestream::GLeadSeconds).GetClampedToMaxSize(TDTexturePrestream::MaximumLeadDistanceCm);
	IStreamingManager::Get().AddViewLocation(CameraLocation + Lead, TDTexturePrestream::GLeadBoost, false, 0.0f);
}
