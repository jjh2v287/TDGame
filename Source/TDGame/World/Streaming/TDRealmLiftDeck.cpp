#include "World/Streaming/TDRealmLiftDeck.h"

#include "Backends/MoverStandaloneLiaison.h"
#include "Characters/TDCombatCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "MoverSimulationTypes.h"
#include "MoverTypes.h"
#include "TDGame.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"

namespace
{
	constexpr double TDRealmLiftMaxStepSeconds = 0.05;
	constexpr double TDRealmLiftReadyHysteresisSeconds = 0.3;
	constexpr double TDRealmLiftTimeToleranceSeconds = 1e-9;
	constexpr double TDRealmLiftLandingToleranceCm = 0.01;
	constexpr double TDRealmLiftPhaseToleranceRatio = 1e-9;
	constexpr int32 TDRealmLiftEdgeCount = 4;
	constexpr uint8 TDRealmLiftAllEdgesMask = 0x0F;
	constexpr double TDRealmLiftDeckStandToleranceCm = 30.0;
	constexpr double TDRealmLiftDeckTopClearanceCm = 2.5;
	constexpr double TDRealmLiftRiderSnapGapCm = 2.0;
	constexpr double TDRealmLiftRideFootprintMarginCm = 50.0;
	constexpr double TDRealmLiftRideHeadroomCm = 500.0;
	constexpr double TDRealmLiftCallZoneMarginCm = 600.0;
	constexpr double TDRealmLiftCallZoneHeightCm = 300.0;
	constexpr double TDRealmLiftEdgeGapCm = 5.0;
	constexpr double TDRealmLiftBlockerHalfThicknessCm = 10.0;
	constexpr double TDRealmLiftBlockerBottomCm = -30.0;
	constexpr double TDRealmLiftBlockerTopCm = 300.0;
	constexpr double TDRealmLiftRailHeightCm = 100.0;
	constexpr double TDRealmLiftRailThicknessCm = 10.0;
	constexpr double TDRealmLiftTeleportThresholdCm = 1000.0;
	constexpr double TDRealmLiftShaftBackVCm = -1300.0;
	constexpr double TDRealmLiftShaftFaceVCm = 450.0;
	constexpr double TDRealmLiftShaftHalfWidthCm = 800.0;
	constexpr double TDRealmLiftAbyssScreenHalfThicknessCm = 0.5;
	const TCHAR* TDRealmLiftFallbackMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* TDRealmLiftAbyssScreenMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* TDRealmLiftAbyssScreenFallbackMaterialPath = TEXT("/Engine/EngineDebugMaterials/BlackUnlitMaterial.BlackUnlitMaterial");

	const FVector TDRealmLiftEdgeNormals[TDRealmLiftEdgeCount] =
	{
		FVector(1.0, 0.0, 0.0),
		FVector(-1.0, 0.0, 0.0),
		FVector(0.0, 1.0, 0.0),
		FVector(0.0, -1.0, 0.0)
	};

	uint8 MakeRealmLiftEdgeBit(int32 EdgeIndex)
	{
		return static_cast<uint8>(1 << EdgeIndex);
	}

	UStaticMesh* ResolveRealmLiftMesh(const TSoftObjectPtr<UStaticMesh>& MeshReference, UStaticMesh* FallbackMesh)
	{
		UStaticMesh* LoadedMesh = MeshReference.IsNull() ? nullptr : MeshReference.Get();
		return LoadedMesh ? LoadedMesh : FallbackMesh;
	}

	UMaterialInterface* ResolveRealmLiftMaterial(const TSoftObjectPtr<UMaterialInterface>& MaterialReference, const TCHAR* FallbackMaterialPath)
	{
		UMaterialInterface* LoadedMaterial = MaterialReference.IsNull() ? nullptr : MaterialReference.Get();
		if (LoadedMaterial || !FallbackMaterialPath)
		{
			return LoadedMaterial;
		}
		return Cast<UMaterialInterface>(FSoftObjectPath(FallbackMaterialPath).ResolveObject());
	}

	void FitRealmLiftMeshToBox(UStaticMeshComponent& Component, UStaticMesh* Mesh, const FVector& CenterCm, const FVector& HalfExtentCm, UMaterialInterface* Material, const FQuat& Rotation = FQuat::Identity)
	{
		Component.SetStaticMesh(Mesh);
		if (!Mesh)
		{
			return;
		}

		const FBox MeshBounds = Mesh->GetBoundingBox();
		const FVector MeshSize = MeshBounds.GetSize().ComponentMax(FVector(1.0));
		const FVector Scale(2.0 * HalfExtentCm.X / MeshSize.X, 2.0 * HalfExtentCm.Y / MeshSize.Y, 2.0 * HalfExtentCm.Z / MeshSize.Z);
		Component.SetRelativeScale3D(Scale);
		Component.SetRelativeRotation(Rotation);
		Component.SetRelativeLocation(CenterCm - Rotation.RotateVector(MeshBounds.GetCenter() * Scale));
		if (!Material)
		{
			return;
		}

		for (int32 MaterialIndex = 0; MaterialIndex < Component.GetNumMaterials(); ++MaterialIndex)
		{
			Component.SetMaterial(MaterialIndex, Material);
		}
	}

	FVector GetRealmLiftPawnFootLocation(const APawn& Pawn)
	{
		return Pawn.GetActorLocation() - FVector(0.0, 0.0, Pawn.GetSimpleCollisionHalfHeight());
	}

	const TCHAR* GetRealmLiftDirectionName(ETDRealmLiftDirection Direction)
	{
		return Direction == ETDRealmLiftDirection::Down ? TEXT("down") : TEXT("up");
	}

	void ForgetRealmLiftRiderFloor(const APawn& Rider)
	{
		const ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(&Rider);
		const UCharacterMoverComponent* MoverComponent = CombatCharacter ? CombatCharacter->GetMoverComponent() : nullptr;
		UMoverBlackboard* SimBlackboard = MoverComponent ? MoverComponent->GetSimBlackboard_Mutable() : nullptr;
		if (!SimBlackboard)
		{
			return;
		}
		SimBlackboard->Invalidate(CommonBlackboard::LastFloorResult);
	}
}

FTDRealmLiftMotion FTDRealmLiftMotion::MakeFromGate(const FTDRealmGate& Gate)
{
	const FTDRealmLift& Lift = Gate.Lift;
	FTDRealmLiftMotion Motion;
	Motion.TravelLengthCm = FMath::Max(Lift.FieldLandingTransform.GetLocation().Z - Lift.RealmLandingTransform.GetLocation().Z, 0.0);
	Motion.CruiseSpeedCmPerSecond = Lift.CruiseSpeedCmPerSecond;
	Motion.AccelerationCmPerSecondSquared = Lift.AccelerationCmPerSecondSquared;
	Motion.BrakeDecelerationCmPerSecondSquared = Lift.BrakeDecelerationCmPerSecondSquared;
	Motion.HoldDepthCm = Lift.HoldDepthCm;
	Motion.RingModuleHeightCm = Lift.RingModuleHeightCm;
	Motion.MaxTreadmillSeconds = Lift.MaxTreadmillSeconds;
	Motion.BoardDwellSeconds = Lift.BoardDwellSeconds;
	Motion.CallSpeedCmPerSecond = Lift.CallSpeedCmPerSecond;
	return Motion;
}

double FTDRealmLiftMotion::GetDirectionSign() const
{
	return Direction == ETDRealmLiftDirection::Down ? 1.0 : -1.0;
}

double FTDRealmLiftMotion::GetProgressCm() const
{
	return Direction == ETDRealmLiftDirection::Down ? PositionCm : TravelLengthCm - PositionCm;
}

double FTDRealmLiftMotion::GetBrakeDistanceCm() const
{
	return FMath::Square(CruiseSpeedCmPerSecond) / (2.0 * BrakeDecelerationCmPerSecondSquared);
}

double FTDRealmLiftMotion::GetResumeDistanceCm() const
{
	return FMath::Square(CruiseSpeedCmPerSecond) / (2.0 * AccelerationCmPerSecondSquared);
}

double FTDRealmLiftMotion::GetTreadmillDecisionProgressCm() const
{
	return HoldDepthCm - GetBrakeDistanceCm();
}

bool FTDRealmLiftMotion::CanUseTreadmill() const
{
	if (CruiseSpeedCmPerSecond <= 0.0 || AccelerationCmPerSecondSquared <= 0.0 || BrakeDecelerationCmPerSecondSquared <= 0.0 || RingModuleHeightCm <= 0.0)
	{
		return false;
	}
	return GetTreadmillDecisionProgressCm() >= 0.0 && HoldDepthCm + GetResumeDistanceCm() + GetBrakeDistanceCm() <= TravelLengthCm;
}

bool FTDRealmLiftMotion::IsCarryingRider() const
{
	return State == ETDRealmLiftState::Riding || State == ETDRealmLiftState::Treadmill;
}

bool FTDRealmLiftMotion::ShouldShowAbyssScreen(double AbyssScreenDepthCm) const
{
	if (!IsCarryingRider())
	{
		return false;
	}
	return TravelLengthCm - PositionCm > AbyssScreenDepthCm;
}

bool FTDRealmLiftMotion::IsAtFieldLanding() const
{
	return PositionCm <= TDRealmLiftLandingToleranceCm;
}

bool FTDRealmLiftMotion::IsAtRealmLanding() const
{
	return PositionCm >= TravelLengthCm - TDRealmLiftLandingToleranceCm;
}

void FTDRealmLiftMotion::SnapRingPhaseToModule()
{
	if (RingModuleHeightCm <= 0.0)
	{
		return;
	}
	RingPhaseCm = FMath::RoundToDouble(RingPhaseCm / RingModuleHeightCm) * RingModuleHeightCm;
}

void FTDRealmLiftMotion::ResetRideProgress()
{
	bIsReadyLatched = false;
	ReadyChangeSeconds = 0.0;
	BoardElapsedSeconds = 0.0;
	TreadmillSeconds = 0.0;
	bIsResumeArmed = false;
	bIsRingResuming = false;
	RingSpeedCmPerSecond = 0.0;
	SnapRingPhaseToModule();
}

void FTDRealmLiftMotion::StartRide()
{
	Direction = PositionCm * 2.0 < TravelLengthCm ? ETDRealmLiftDirection::Down : ETDRealmLiftDirection::Up;
	ResetRideProgress();
	SpeedCmPerSecond = 0.0;
	State = ETDRealmLiftState::Riding;
}

void FTDRealmLiftMotion::UpdateRider(bool bHasRider, double DeltaSeconds)
{
	switch (State)
	{
	case ETDRealmLiftState::Resting:
		if (bHasRider)
		{
			State = ETDRealmLiftState::Boarding;
			BoardElapsedSeconds = 0.0;
		}
		return;
	case ETDRealmLiftState::Boarding:
		if (!bHasRider)
		{
			State = ETDRealmLiftState::Resting;
			BoardElapsedSeconds = 0.0;
			return;
		}
		BoardElapsedSeconds += FMath::Max(DeltaSeconds, 0.0);
		if (BoardElapsedSeconds + TDRealmLiftTimeToleranceSeconds >= BoardDwellSeconds)
		{
			StartRide();
		}
		return;
	case ETDRealmLiftState::Open:
		if (!bHasRider)
		{
			State = ETDRealmLiftState::Resting;
		}
		return;
	case ETDRealmLiftState::Calling:
		if (bHasRider)
		{
			ResetRideProgress();
			State = ETDRealmLiftState::Riding;
		}
		return;
	case ETDRealmLiftState::Riding:
	case ETDRealmLiftState::Treadmill:
		if (!bHasRider)
		{
			ResetRideProgress();
			State = ETDRealmLiftState::Calling;
		}
		return;
	}
}

void FTDRealmLiftMotion::RequestCall(bool bToRealmLanding)
{
	if (State != ETDRealmLiftState::Resting && State != ETDRealmLiftState::Calling)
	{
		return;
	}

	const bool bIsAlreadyThere = bToRealmLanding ? IsAtRealmLanding() : IsAtFieldLanding();
	const ETDRealmLiftDirection CallDirection = bToRealmLanding ? ETDRealmLiftDirection::Down : ETDRealmLiftDirection::Up;
	if (bIsAlreadyThere || (State == ETDRealmLiftState::Calling && Direction == CallDirection))
	{
		return;
	}

	ResetRideProgress();
	SpeedCmPerSecond = 0.0;
	Direction = CallDirection;
	State = ETDRealmLiftState::Calling;
}

void FTDRealmLiftMotion::UpdateReadyLatch(bool bIsReady, double DeltaSeconds)
{
	if (bIsReady == bIsReadyLatched)
	{
		ReadyChangeSeconds = 0.0;
		return;
	}

	ReadyChangeSeconds += DeltaSeconds;
	if (ReadyChangeSeconds + TDRealmLiftTimeToleranceSeconds < TDRealmLiftReadyHysteresisSeconds)
	{
		return;
	}

	bIsReadyLatched = bIsReady;
	ReadyChangeSeconds = 0.0;
}

void FTDRealmLiftMotion::Step(double DeltaSeconds, bool bIsReady)
{
	const double StepSeconds = FMath::Min(DeltaSeconds, TDRealmLiftMaxStepSeconds);
	const bool bIsCalling = State == ETDRealmLiftState::Calling;
	if (StepSeconds <= 0.0 || (!bIsCalling && !IsCarryingRider()))
	{
		return;
	}

	if (bIsCalling)
	{
		StepTravel(StepSeconds, true);
		return;
	}

	UpdateReadyLatch(bIsReady, StepSeconds);
	if (State == ETDRealmLiftState::Treadmill)
	{
		StepTreadmill(StepSeconds);
		return;
	}
	StepTravel(StepSeconds, false);
}

void FTDRealmLiftMotion::StepTravel(double StepSeconds, bool bIsCalling)
{
	const double LandingPositionCm = Direction == ETDRealmLiftDirection::Down ? TravelLengthCm : 0.0;
	const double RemainingCm = FMath::Max((LandingPositionCm - PositionCm) * GetDirectionSign(), 0.0);
	const double PreviousProgressCm = GetProgressCm();
	const double TravelSpeedCm = bIsCalling ? CallSpeedCmPerSecond : CruiseSpeedCmPerSecond;
	const double BrakeSpeedCm = FMath::Sqrt(2.0 * BrakeDecelerationCmPerSecondSquared * RemainingCm);
	const double TargetSpeedCm = FMath::Min(TravelSpeedCm, BrakeSpeedCm);
	SpeedCmPerSecond = SpeedCmPerSecond < TargetSpeedCm ? FMath::Min(SpeedCmPerSecond + AccelerationCmPerSecondSquared * StepSeconds, TargetSpeedCm) : TargetSpeedCm;

	const double MoveCm = SpeedCmPerSecond * StepSeconds;
	if (MoveCm >= RemainingCm)
	{
		FinishAtLanding(bIsCalling);
		return;
	}

	PositionCm += GetDirectionSign() * MoveCm;
	const double DecisionProgressCm = GetTreadmillDecisionProgressCm();
	const bool bHasReachedDecision = PreviousProgressCm < DecisionProgressCm && GetProgressCm() >= DecisionProgressCm;
	if (bIsCalling || !bHasReachedDecision || bIsReadyLatched || !CanUseTreadmill())
	{
		return;
	}
	EnterTreadmill();
}

void FTDRealmLiftMotion::FinishAtLanding(bool bIsCalling)
{
	PositionCm = Direction == ETDRealmLiftDirection::Down ? TravelLengthCm : 0.0;
	SpeedCmPerSecond = 0.0;
	State = bIsCalling ? ETDRealmLiftState::Resting : ETDRealmLiftState::Open;
}

void FTDRealmLiftMotion::EnterTreadmill()
{
	State = ETDRealmLiftState::Treadmill;
	TreadmillSeconds = 0.0;
	bIsResumeArmed = false;
	bIsRingResuming = false;
	RingSpeedCmPerSecond = FMath::Max(CruiseSpeedCmPerSecond - SpeedCmPerSecond, 0.0);
}

void FTDRealmLiftMotion::StepTreadmill(double StepSeconds)
{
	TreadmillSeconds += StepSeconds;
	if (!bIsResumeArmed && (bIsReadyLatched || TreadmillSeconds + TDRealmLiftTimeToleranceSeconds >= MaxTreadmillSeconds))
	{
		bIsResumeArmed = true;
	}

	if (bIsRingResuming)
	{
		StepTreadmillResume(StepSeconds);
		return;
	}
	if (SpeedCmPerSecond > 0.0)
	{
		StepTreadmillBrake(StepSeconds);
		return;
	}

	const double RingMoveCm = CruiseSpeedCmPerSecond * StepSeconds;
	RingSpeedCmPerSecond = CruiseSpeedCmPerSecond;
	if (!bIsResumeArmed)
	{
		RingPhaseCm += RingMoveCm;
		return;
	}

	const double BoundaryWaitCm = FMath::Max(ComputeResumeTriggerPhaseCm() - RingPhaseCm, 0.0);
	if (BoundaryWaitCm >= RingMoveCm)
	{
		RingPhaseCm += RingMoveCm;
		return;
	}

	RingPhaseCm += BoundaryWaitCm;
	StepTreadmillResume(StepSeconds - BoundaryWaitCm / CruiseSpeedCmPerSecond);
}

void FTDRealmLiftMotion::StepTreadmillBrake(double StepSeconds)
{
	const double RemainingCm = FMath::Max(HoldDepthCm - GetProgressCm(), 0.0);
	const double BrakeSpeedCm = FMath::Sqrt(2.0 * BrakeDecelerationCmPerSecondSquared * RemainingCm);
	const double TargetSpeedCm = FMath::Min(CruiseSpeedCmPerSecond, BrakeSpeedCm);
	SpeedCmPerSecond = SpeedCmPerSecond < TargetSpeedCm ? FMath::Min(SpeedCmPerSecond + AccelerationCmPerSecondSquared * StepSeconds, TargetSpeedCm) : TargetSpeedCm;

	double MoveCm = SpeedCmPerSecond * StepSeconds;
	if (MoveCm >= RemainingCm)
	{
		MoveCm = RemainingCm;
		SpeedCmPerSecond = 0.0;
	}

	PositionCm += GetDirectionSign() * MoveCm;
	if (SpeedCmPerSecond <= 0.0)
	{
		PositionCm = Direction == ETDRealmLiftDirection::Down ? HoldDepthCm : TravelLengthCm - HoldDepthCm;
	}
	RingPhaseCm += CruiseSpeedCmPerSecond * StepSeconds - MoveCm;
	RingSpeedCmPerSecond = CruiseSpeedCmPerSecond - SpeedCmPerSecond;
}

double FTDRealmLiftMotion::ComputeResumeTriggerPhaseCm() const
{
	const double ResumeDistanceCm = GetResumeDistanceCm();
	const double ModuleIndex = FMath::CeilToDouble((RingPhaseCm + ResumeDistanceCm) / RingModuleHeightCm - TDRealmLiftPhaseToleranceRatio);
	return ModuleIndex * RingModuleHeightCm - ResumeDistanceCm;
}

void FTDRealmLiftMotion::StepTreadmillResume(double StepSeconds)
{
	bIsRingResuming = true;
	const double SecondsToCruise = FMath::Max(CruiseSpeedCmPerSecond - SpeedCmPerSecond, 0.0) / AccelerationCmPerSecondSquared;
	const double AccelerationSeconds = FMath::Clamp(StepSeconds, 0.0, SecondsToCruise);
	const double MoveCm = SpeedCmPerSecond * AccelerationSeconds + 0.5 * AccelerationCmPerSecondSquared * FMath::Square(AccelerationSeconds);
	SpeedCmPerSecond += AccelerationCmPerSecondSquared * AccelerationSeconds;
	PositionCm += GetDirectionSign() * MoveCm;
	RingPhaseCm += CruiseSpeedCmPerSecond * AccelerationSeconds - MoveCm;
	RingSpeedCmPerSecond = CruiseSpeedCmPerSecond - SpeedCmPerSecond;
	if (AccelerationSeconds < SecondsToCruise)
	{
		return;
	}
	FinishTreadmill(StepSeconds - AccelerationSeconds);
}

void FTDRealmLiftMotion::FinishTreadmill(double RemainingSeconds)
{
	SpeedCmPerSecond = CruiseSpeedCmPerSecond;
	RingSpeedCmPerSecond = 0.0;
	SnapRingPhaseToModule();
	bIsResumeArmed = false;
	bIsRingResuming = false;
	State = ETDRealmLiftState::Riding;
	if (RemainingSeconds > 0.0)
	{
		StepTravel(RemainingSeconds, false);
	}
}

void FTDRealmLiftMotion::PlaceAtLanding(bool bAtRealmLanding)
{
	PositionCm = bAtRealmLanding ? TravelLengthCm : 0.0;
	SpeedCmPerSecond = 0.0;
	Direction = bAtRealmLanding ? ETDRealmLiftDirection::Up : ETDRealmLiftDirection::Down;
	ResetRideProgress();
	State = ETDRealmLiftState::Resting;
}

ATDRealmLiftDeck::ATDRealmLiftDeck()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetCanBeDamaged(false);

	DeckRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DeckRoot"));
	DeckRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = DeckRoot;

	DeckComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Deck"));
	DeckComponent->SetupAttachment(DeckRoot);
	DeckComponent->SetMobility(EComponentMobility::Movable);
	DeckComponent->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	DeckComponent->SetCanEverAffectNavigation(false);
	DeckComponent->SetGenerateOverlapEvents(false);

	AbyssScreenComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AbyssScreen"));
	AbyssScreenComponent->SetupAttachment(DeckRoot);
	AbyssScreenComponent->SetMobility(EComponentMobility::Movable);
	AbyssScreenComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	AbyssScreenComponent->SetCanEverAffectNavigation(false);
	AbyssScreenComponent->SetGenerateOverlapEvents(false);
	AbyssScreenComponent->SetCastShadow(false);
	AbyssScreenComponent->SetVisibility(false);

	for (int32 EdgeIndex = 0; EdgeIndex < TDRealmLiftEdgeCount; ++EdgeIndex)
	{
		UStaticMeshComponent* Rail = CreateDefaultSubobject<UStaticMeshComponent>(FName(*FString::Printf(TEXT("Rail%d"), EdgeIndex)));
		Rail->SetupAttachment(DeckRoot);
		Rail->SetMobility(EComponentMobility::Movable);
		Rail->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Rail->SetCanEverAffectNavigation(false);
		Rail->SetGenerateOverlapEvents(false);
		EdgeRails.Add(Rail);

		UBoxComponent* Blocker = CreateDefaultSubobject<UBoxComponent>(FName(*FString::Printf(TEXT("EdgeBlocker%d"), EdgeIndex)));
		Blocker->SetupAttachment(DeckRoot);
		Blocker->SetMobility(EComponentMobility::Movable);
		Blocker->SetCollisionObjectType(ECC_WorldDynamic);
		Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
		Blocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Blocker->SetCanEverAffectNavigation(false);
		Blocker->SetGenerateOverlapEvents(false);
		EdgeBlockers.Add(Blocker);
	}
	BlockedEdgeMask = TDRealmLiftAllEdgesMask;
}

FTransform ATDRealmLiftDeck::MakeShaftFrameTransform(const FTDRealmGate& Gate)
{
	const double FrameYawDeg = FRotator::NormalizeAxis(Gate.Lift.RealmLandingTransform.Rotator().Yaw + 180.0);
	return FTransform(FRotator(0.0, FrameYawDeg, 0.0), Gate.Lift.FieldLandingTransform.GetLocation());
}

void ATDRealmLiftDeck::GatherRequiredAssetPaths(const FTDRealmGate& Gate, TArray<FSoftObjectPath>& OutPaths)
{
	const FSoftObjectPath GateAssetPaths[] =
	{
		Gate.Lift.DeckMesh.ToSoftObjectPath(),
		Gate.Lift.RailMesh.ToSoftObjectPath(),
		Gate.Lift.DeckMaterial.ToSoftObjectPath(),
		Gate.Lift.AbyssScreenMaterial.ToSoftObjectPath()
	};
	for (const FSoftObjectPath& GateAssetPath : GateAssetPaths)
	{
		if (!GateAssetPath.IsNull())
		{
			OutPaths.AddUnique(GateAssetPath);
		}
	}
	OutPaths.AddUnique(FSoftObjectPath(TDRealmLiftFallbackMeshPath));
	OutPaths.AddUnique(FSoftObjectPath(TDRealmLiftAbyssScreenMeshPath));
	OutPaths.AddUnique(FSoftObjectPath(TDRealmLiftAbyssScreenFallbackMaterialPath));
}

void ATDRealmLiftDeck::InitializeLift(const FTDRealmGate& Gate)
{
	GateId = Gate.GateId;
	Motion = FTDRealmLiftMotion::MakeFromGate(Gate);
	FieldLandingLocationCm = Gate.Lift.FieldLandingTransform.GetLocation();
	DeckHalfExtentCm = Gate.Lift.DeckHalfExtentCm.ComponentMax(FVector(1.0));
	SetActorRotation(FRotator(0.0, Gate.Lift.FieldLandingTransform.Rotator().Yaw, 0.0));

	UStaticMesh* FallbackMesh = Cast<UStaticMesh>(FSoftObjectPath(TDRealmLiftFallbackMeshPath).ResolveObject());
	UStaticMesh* DeckMesh = ResolveRealmLiftMesh(Gate.Lift.DeckMesh, FallbackMesh);
	UStaticMesh* RailMesh = ResolveRealmLiftMesh(Gate.Lift.RailMesh, FallbackMesh);
	UMaterialInterface* DeckMaterial = ResolveRealmLiftMaterial(Gate.Lift.DeckMaterial, nullptr);
	FitRealmLiftMeshToBox(*DeckComponent, DeckMesh, FVector(0.0, 0.0, TDRealmLiftDeckTopClearanceCm - DeckHalfExtentCm.Z), DeckHalfExtentCm, DeckMaterial);
	ConfigureEdges(Gate, RailMesh, DeckMaterial);
	ConfigureAbyssScreen(Gate);
	MoveDeckToMotion();
	UpdateDoors();
	UpdateAbyssScreen();
	UE_LOG(LogTDGame, Log, TEXT("RealmLift '%s': travel %.1f cm, hold depth %.1f cm, treadmill %s, max treadmill %.1f s, deck mesh '%s', deck top %.1f cm above the landings, field exit edges 0x%X, realm exit edges 0x%X."),
		*GateId.ToString(), Motion.TravelLengthCm, Motion.HoldDepthCm, Motion.CanUseTreadmill() ? TEXT("enabled") : TEXT("disabled"), Motion.MaxTreadmillSeconds,
		DeckMesh ? *DeckMesh->GetName() : TEXT("<none>"), TDRealmLiftDeckTopClearanceCm, FieldExitEdgeMask, RealmExitEdgeMask);
}

void ATDRealmLiftDeck::ConfigureEdges(const FTDRealmGate& Gate, UStaticMesh* RailMesh, UMaterialInterface* Material)
{
	const FVector RealmExitDirection = Gate.Lift.RealmLandingTransform.GetRotation().GetForwardVector().GetSafeNormal2D();
	const FVector LocalRealmExitDirection = GetActorQuat().UnrotateVector(RealmExitDirection);
	int32 RealmExitEdge = 0;
	double BestAlignment = -TNumericLimits<double>::Max();
	for (int32 EdgeIndex = 0; EdgeIndex < TDRealmLiftEdgeCount; ++EdgeIndex)
	{
		const double Alignment = FVector::DotProduct(LocalRealmExitDirection, TDRealmLiftEdgeNormals[EdgeIndex]);
		if (Alignment > BestAlignment)
		{
			BestAlignment = Alignment;
			RealmExitEdge = EdgeIndex;
		}
	}
	FieldExitEdgeMask = MakeRealmLiftEdgeBit(0);
	RealmExitEdgeMask = MakeRealmLiftEdgeBit(RealmExitEdge);

	const double RailHalfThicknessCm = TDRealmLiftRailThicknessCm * 0.5;
	const double RailHalfHeightCm = TDRealmLiftRailHeightCm * 0.5;
	const double BlockerHalfHeightCm = (TDRealmLiftBlockerTopCm - TDRealmLiftBlockerBottomCm) * 0.5;
	const double BlockerCenterZCm = (TDRealmLiftBlockerTopCm + TDRealmLiftBlockerBottomCm) * 0.5;
	for (int32 EdgeIndex = 0; EdgeIndex < TDRealmLiftEdgeCount; ++EdgeIndex)
	{
		const FVector& Normal = TDRealmLiftEdgeNormals[EdgeIndex];
		const bool bIsAlongX = FMath::Abs(Normal.X) > 0.5;
		const double EdgeDistanceCm = bIsAlongX ? DeckHalfExtentCm.X : DeckHalfExtentCm.Y;
		const double EdgeHalfLengthCm = bIsAlongX ? DeckHalfExtentCm.Y : DeckHalfExtentCm.X;
		const bool bIsExitEdge = ((FieldExitEdgeMask | RealmExitEdgeMask) & MakeRealmLiftEdgeBit(EdgeIndex)) != 0;

		const FVector RailCenterCm = Normal * (EdgeDistanceCm + RailHalfThicknessCm) + FVector(0.0, 0.0, TDRealmLiftDeckTopClearanceCm + RailHalfHeightCm);
		const FVector RailHalfExtentCm = bIsAlongX ? FVector(RailHalfThicknessCm, EdgeHalfLengthCm, RailHalfHeightCm) : FVector(EdgeHalfLengthCm, RailHalfThicknessCm, RailHalfHeightCm);
		FitRealmLiftMeshToBox(*EdgeRails[EdgeIndex], bIsExitEdge ? nullptr : RailMesh, RailCenterCm, RailHalfExtentCm, Material);

		const double BlockerHalfLengthCm = EdgeHalfLengthCm + TDRealmLiftEdgeGapCm + 2.0 * TDRealmLiftBlockerHalfThicknessCm;
		const FVector BlockerCenterCm = Normal * (EdgeDistanceCm + TDRealmLiftEdgeGapCm + TDRealmLiftBlockerHalfThicknessCm) + FVector(0.0, 0.0, BlockerCenterZCm);
		const FVector BlockerExtentCm = bIsAlongX ? FVector(TDRealmLiftBlockerHalfThicknessCm, BlockerHalfLengthCm, BlockerHalfHeightCm) : FVector(BlockerHalfLengthCm, TDRealmLiftBlockerHalfThicknessCm, BlockerHalfHeightCm);
		UBoxComponent* Blocker = EdgeBlockers[EdgeIndex];
		Blocker->SetRelativeLocation(BlockerCenterCm);
		Blocker->SetBoxExtent(BlockerExtentCm);
		Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	BlockedEdgeMask = TDRealmLiftAllEdgesMask;
}

void ATDRealmLiftDeck::ConfigureAbyssScreen(const FTDRealmGate& Gate)
{
	AbyssScreenDepthCm = Gate.Lift.AbyssScreenDepthCm;
	UStaticMesh* ScreenMesh = Cast<UStaticMesh>(FSoftObjectPath(TDRealmLiftAbyssScreenMeshPath).ResolveObject());
	UMaterialInterface* ScreenMaterial = ResolveRealmLiftMaterial(Gate.Lift.AbyssScreenMaterial, TDRealmLiftAbyssScreenFallbackMaterialPath);
	const FQuat DeckToFrameRotation = GetActorQuat().Inverse() * MakeShaftFrameTransform(Gate).GetRotation();
	const FVector FrameCenterCm((TDRealmLiftShaftBackVCm + TDRealmLiftShaftFaceVCm) * 0.5, 0.0, 0.0);
	const FVector ScreenCenterCm = DeckToFrameRotation.RotateVector(FrameCenterCm) - FVector(0.0, 0.0, AbyssScreenDepthCm);
	const FVector ScreenHalfExtentCm((TDRealmLiftShaftFaceVCm - TDRealmLiftShaftBackVCm) * 0.5, TDRealmLiftShaftHalfWidthCm, TDRealmLiftAbyssScreenHalfThicknessCm);
	FitRealmLiftMeshToBox(*AbyssScreenComponent, ScreenMesh, ScreenCenterCm, ScreenHalfExtentCm, ScreenMaterial, DeckToFrameRotation);
}

void ATDRealmLiftDeck::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GateId.IsNone())
	{
		return;
	}

	const ETDRealmLiftState PreviousState = Motion.State;
	const bool bWasResumeArmed = Motion.bIsResumeArmed;
	const double StepSeconds = FMath::Min(static_cast<double>(DeltaSeconds), TDRealmLiftMaxStepSeconds);
	APawn* Pawn = FindLocalPlayerPawn();
	APawn* Rider = Pawn && IsPawnAboard(*Pawn) ? Pawn : nullptr;
	Motion.UpdateRider(Rider != nullptr, StepSeconds);
	if (Pawn && !Rider)
	{
		RequestCallForPawn(*Pawn);
	}
	Motion.Step(StepSeconds, Motion.IsCarryingRider() && IsDestinationReady());
	LogTreadmillChange(PreviousState, bWasResumeArmed);
	UpdateDoors();
	UpdateAbyssScreen();
	MoveDeckToMotion();
	UpdateRiderLock(Rider);
	KeepRiderOnDeck(Rider);
}

void ATDRealmLiftDeck::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bHasLockedRider)
	{
		ReleaseRider();
	}
	Super::EndPlay(EndPlayReason);
}

FVector ATDRealmLiftDeck::ComputeDeckLocation(double PositionCm) const
{
	return FieldLandingLocationCm - FVector(0.0, 0.0, PositionCm);
}

FVector ATDRealmLiftDeck::GetDeckTopLocation() const
{
	return GetActorLocation() + FVector(0.0, 0.0, TDRealmLiftDeckTopClearanceCm);
}

bool ATDRealmLiftDeck::IsCarryingPawn(const APawn& Pawn) const
{
	return IsCarryingLocation(Pawn.GetActorLocation());
}

bool ATDRealmLiftDeck::IsCarryingLocation(const FVector& LocationCm) const
{
	return Motion.IsCarryingRider() && IsWithinRideVolume(LocationCm);
}

bool ATDRealmLiftDeck::IsPawnBasedOnDeck(const APawn& Pawn) const
{
	const ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(&Pawn);
	const UCharacterMoverComponent* MoverComponent = CombatCharacter ? CombatCharacter->GetMoverComponent() : nullptr;
	return MoverComponent && DeckComponent && MoverComponent->GetMovementBase() == DeckComponent;
}

bool ATDRealmLiftDeck::IsAnyDoorOpen() const
{
	return ((FieldExitEdgeMask | RealmExitEdgeMask) & ~BlockedEdgeMask) != 0;
}

APawn* ATDRealmLiftDeck::FindLocalPlayerPawn() const
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return PlayerController ? PlayerController->GetPawn() : nullptr;
}

bool ATDRealmLiftDeck::IsPawnAboard(const APawn& Pawn) const
{
	if (Motion.IsCarryingRider())
	{
		return IsWithinRideVolume(Pawn.GetActorLocation());
	}

	const FVector LocalFootCm = GetActorTransform().InverseTransformPositionNoScale(GetRealmLiftPawnFootLocation(Pawn));
	const bool bIsStandingOnDeck = IsPawnBasedOnDeck(Pawn) || (IsInsideDeckFootprint(LocalFootCm, 0.0) && FMath::Abs(LocalFootCm.Z - TDRealmLiftDeckTopClearanceCm) <= TDRealmLiftDeckStandToleranceCm);
	if (!bIsStandingOnDeck)
	{
		return false;
	}

	const bool bIsWaitingToBoard = Motion.State == ETDRealmLiftState::Resting || Motion.State == ETDRealmLiftState::Boarding;
	return !bIsWaitingToBoard || IsInsideDeckFootprint(LocalFootCm, -Pawn.GetSimpleCollisionRadius());
}

bool ATDRealmLiftDeck::IsWithinRideVolume(const FVector& LocationCm) const
{
	const FVector LocalLocationCm = GetActorTransform().InverseTransformPositionNoScale(LocationCm);
	return IsInsideDeckFootprint(LocalLocationCm, TDRealmLiftRideFootprintMarginCm)
		&& LocalLocationCm.Z >= -TDRealmLiftDeckStandToleranceCm
		&& LocalLocationCm.Z <= TDRealmLiftRideHeadroomCm;
}

bool ATDRealmLiftDeck::IsInsideDeckFootprint(const FVector& LocalLocationCm, double MarginCm) const
{
	return FMath::Abs(LocalLocationCm.X) <= DeckHalfExtentCm.X + MarginCm && FMath::Abs(LocalLocationCm.Y) <= DeckHalfExtentCm.Y + MarginCm;
}

bool ATDRealmLiftDeck::IsPawnInCallZone(const APawn& Pawn, bool bIsRealmLanding) const
{
	const FVector LandingCm = ComputeDeckLocation(bIsRealmLanding ? Motion.TravelLengthCm : 0.0);
	const FVector FootCm = GetRealmLiftPawnFootLocation(Pawn);
	const double CallRadiusCm = FMath::Max(DeckHalfExtentCm.X, DeckHalfExtentCm.Y) * UE_DOUBLE_SQRT_2 + TDRealmLiftCallZoneMarginCm;
	if (FVector::DistSquared2D(FootCm, LandingCm) > FMath::Square(CallRadiusCm) || FMath::Abs(FootCm.Z - LandingCm.Z) > TDRealmLiftCallZoneHeightCm)
	{
		return false;
	}

	const FVector FootFromLandingCm = GetActorQuat().UnrotateVector(FootCm - LandingCm);
	return !IsInsideDeckFootprint(FootFromLandingCm, Pawn.GetSimpleCollisionRadius());
}

void ATDRealmLiftDeck::RequestCallForPawn(const APawn& Pawn)
{
	if (Motion.State != ETDRealmLiftState::Resting && Motion.State != ETDRealmLiftState::Calling)
	{
		return;
	}

	if (IsPawnInCallZone(Pawn, false))
	{
		Motion.RequestCall(false);
		return;
	}
	if (IsPawnInCallZone(Pawn, true))
	{
		Motion.RequestCall(true);
	}
}

bool ATDRealmLiftDeck::IsDestinationReady() const
{
	const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(this);
	return Travel && Travel->IsReadyFor(GateId, Motion.Direction == ETDRealmLiftDirection::Down);
}

void ATDRealmLiftDeck::LogTreadmillChange(ETDRealmLiftState PreviousState, bool bWasResumeArmed) const
{
	const bool bWasTreadmill = PreviousState == ETDRealmLiftState::Treadmill;
	const bool bIsTreadmill = Motion.State == ETDRealmLiftState::Treadmill;
	if (bIsTreadmill && !bWasTreadmill)
	{
		UE_LOG(LogTDGame, Log, TEXT("RealmLift '%s': treadmill engaged at %.1f cm going %s, destination not ready."),
			*GateId.ToString(), Motion.PositionCm, GetRealmLiftDirectionName(Motion.Direction));
	}
	if (bIsTreadmill && !bWasResumeArmed && Motion.bIsResumeArmed && !Motion.bIsReadyLatched)
	{
		UE_LOG(LogTDGame, Warning, TEXT("RealmLift '%s': treadmill timeout, proceeding after %.2f s at %.1f cm."),
			*GateId.ToString(), Motion.TreadmillSeconds, Motion.PositionCm);
	}
	if (bWasTreadmill && !bIsTreadmill)
	{
		UE_LOG(LogTDGame, Log, TEXT("RealmLift '%s': treadmill released after %.2f s, ring phase %.1f cm, deck at %.1f cm, state %d."),
			*GateId.ToString(), Motion.TreadmillSeconds, Motion.RingPhaseCm, Motion.PositionCm, static_cast<int32>(Motion.State));
	}
}

void ATDRealmLiftDeck::UpdateRiderLock(APawn* Rider)
{
	APawn* RiderToLock = Motion.IsCarryingRider() ? Rider : nullptr;
	if (bHasLockedRider && LockedPawn.Get() == RiderToLock)
	{
		return;
	}

	if (bHasLockedRider)
	{
		ReleaseRider();
	}
	if (RiderToLock)
	{
		LockRider(*RiderToLock);
	}
}

void ATDRealmLiftDeck::LockRider(APawn& Rider)
{
	bHasLockedRider = true;
	LockedPawn = &Rider;
	if (AController* Controller = Rider.GetController())
	{
		Controller->SetIgnoreMoveInput(true);
		LockedController = Controller;
	}

	UMoverStandaloneLiaisonComponent* Liaison = Rider.FindComponentByClass<UMoverStandaloneLiaisonComponent>();
	FTickFunction* SimulateMovementTick = Liaison ? Liaison->FindTickFunction(EMoverTickPhase::SimulateMovement) : nullptr;
	if (SimulateMovementTick)
	{
		SimulateMovementTick->AddPrerequisite(this, PrimaryActorTick);
		LockedLiaison = Liaison;
	}
	UE_LOG(LogTDGame, Log, TEXT("RealmLift '%s': rider '%s' locked at %.1f cm going %s, movement tick linked %d."),
		*GateId.ToString(), *Rider.GetName(), Motion.PositionCm, GetRealmLiftDirectionName(Motion.Direction), SimulateMovementTick ? 1 : 0);
}

void ATDRealmLiftDeck::ReleaseRider()
{
	if (AController* Controller = LockedController.Get())
	{
		Controller->SetIgnoreMoveInput(false);
	}

	UMoverStandaloneLiaisonComponent* Liaison = LockedLiaison.Get();
	FTickFunction* SimulateMovementTick = Liaison ? Liaison->FindTickFunction(EMoverTickPhase::SimulateMovement) : nullptr;
	if (SimulateMovementTick)
	{
		SimulateMovementTick->RemovePrerequisite(this, PrimaryActorTick);
	}

	UE_LOG(LogTDGame, Log, TEXT("RealmLift '%s': rider released at %.1f cm going %s after %d deck snaps."), *GateId.ToString(), Motion.PositionCm, GetRealmLiftDirectionName(Motion.Direction), RiderSnapCount);
	bHasLockedRider = false;
	LockedPawn.Reset();
	LockedController.Reset();
	LockedLiaison.Reset();
}

void ATDRealmLiftDeck::KeepRiderOnDeck(APawn* Rider)
{
	const bool bIsBoardingOrCarrying = Motion.State == ETDRealmLiftState::Boarding || Motion.IsCarryingRider();
	if (!bIsBoardingOrCarrying)
	{
		RiderSnapCount = 0;
		return;
	}
	if (!Rider || IsPawnBasedOnDeck(*Rider))
	{
		return;
	}

	const ATDCombatCharacter* CombatRider = Cast<ATDCombatCharacter>(Rider);
	const bool bIsJumpingWhileBoarding = Motion.State == ETDRealmLiftState::Boarding && CombatRider && CombatRider->IsAirborne();
	if (bIsJumpingWhileBoarding)
	{
		return;
	}

	const FVector RiderLocationCm = Rider->GetActorLocation();
	if (!IsInsideDeckFootprint(GetActorTransform().InverseTransformPositionNoScale(RiderLocationCm), 0.0))
	{
		return;
	}

	const double StandZCm = GetDeckTopLocation().Z + Rider->GetSimpleCollisionHalfHeight() + TDRealmLiftRiderSnapGapCm;
	Rider->SetActorLocation(FVector(RiderLocationCm.X, RiderLocationCm.Y, StandZCm), false, nullptr, ETeleportType::TeleportPhysics);
	ForgetRealmLiftRiderFloor(*Rider);
	++RiderSnapCount;
	if (RiderSnapCount > 1)
	{
		return;
	}
	UE_LOG(LogTDGame, Log, TEXT("RealmLift '%s': rider '%s' was not based on the deck at %.1f cm (state %d), snapped %.1f cm onto the deck top."),
		*GateId.ToString(), *Rider->GetName(), Motion.PositionCm, static_cast<int32>(Motion.State), StandZCm - RiderLocationCm.Z);
}

void ATDRealmLiftDeck::UpdateDoors()
{
	const bool bIsDoorOpenState = Motion.State == ETDRealmLiftState::Resting || Motion.State == ETDRealmLiftState::Boarding || Motion.State == ETDRealmLiftState::Open;
	const bool bIsFieldDoorOpen = bIsDoorOpenState && Motion.IsAtFieldLanding();
	const bool bIsRealmDoorOpen = bIsDoorOpenState && Motion.IsAtRealmLanding();
	uint8 NewBlockedEdgeMask = 0;
	for (int32 EdgeIndex = 0; EdgeIndex < TDRealmLiftEdgeCount; ++EdgeIndex)
	{
		const uint8 EdgeBit = MakeRealmLiftEdgeBit(EdgeIndex);
		const bool bIsOpen = ((FieldExitEdgeMask & EdgeBit) != 0 && bIsFieldDoorOpen) || ((RealmExitEdgeMask & EdgeBit) != 0 && bIsRealmDoorOpen);
		if (!bIsOpen)
		{
			NewBlockedEdgeMask |= EdgeBit;
		}
	}

	if (NewBlockedEdgeMask == BlockedEdgeMask)
	{
		return;
	}

	for (int32 EdgeIndex = 0; EdgeIndex < TDRealmLiftEdgeCount; ++EdgeIndex)
	{
		const uint8 EdgeBit = MakeRealmLiftEdgeBit(EdgeIndex);
		if (((NewBlockedEdgeMask ^ BlockedEdgeMask) & EdgeBit) == 0)
		{
			continue;
		}
		EdgeBlockers[EdgeIndex]->SetCollisionEnabled((NewBlockedEdgeMask & EdgeBit) != 0 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	BlockedEdgeMask = NewBlockedEdgeMask;
}

void ATDRealmLiftDeck::UpdateAbyssScreen()
{
	AbyssScreenComponent->SetVisibility(Motion.ShouldShowAbyssScreen(AbyssScreenDepthCm));
}

void ATDRealmLiftDeck::MoveDeckToMotion()
{
	const FVector TargetLocationCm = ComputeDeckLocation(Motion.PositionCm);
	const FVector CurrentLocationCm = GetActorLocation();
	if (TargetLocationCm.Equals(CurrentLocationCm, UE_KINDA_SMALL_NUMBER))
	{
		return;
	}

	const bool bIsJump = FVector::DistSquared(TargetLocationCm, CurrentLocationCm) > FMath::Square(TDRealmLiftTeleportThresholdCm);
	DeckRoot->SetWorldLocation(TargetLocationCm, false, nullptr, bIsJump ? ETeleportType::TeleportPhysics : ETeleportType::None);
}
