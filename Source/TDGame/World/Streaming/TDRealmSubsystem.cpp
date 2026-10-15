#include "World/Streaming/TDRealmSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Characters/TDGameCharacter.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TDGame.h"
#include "TDWorldGenSettings.h"
#include "World/Streaming/TDRealmLiftDeck.h"
#include "World/Streaming/TDRealmShaftLiner.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

namespace
{
	constexpr double TDPassageVerticalToleranceCm = 6000.0;
	constexpr float TDAtmosphereQuantizationSteps = 64.0f;
	constexpr double TDPostProcessScanIntervalSeconds = 1.0;
	constexpr double TDFallRecoveryTraceUpCm = 3000.0;
	constexpr double TDFallRecoveryTraceDownCm = 6000.0;
	constexpr double TDFallRecoveryClearanceCm = 10.0;
	constexpr double TDRealmShaftInteriorBackVCm = -1300.0;
	constexpr double TDRealmShaftInteriorFaceVCm = 450.0;
	constexpr double TDRealmShaftInteriorHalfWidthCm = 800.0;
	constexpr double TDRealmShaftInteriorMarginCm = 100.0;
	constexpr double TDRealmShaftVerticalMarginCm = 300.0;
	constexpr double TDRealmLiftSlotHalfWidthCm = 700.0;
	constexpr double TDRealmLiftSlotHeightToleranceCm = 600.0;
	constexpr double TDRealmLiftPitRecoveryRadiusCm = 1600.0;
	constexpr double TDRealmLiftPitRecoveryMarginCm = 300.0;
	constexpr double TDRealmLiftExitTraceUpCm = 200.0;
	constexpr double TDRealmPassageDirectionSpeedCm = 50.0;
	constexpr double TDRealmPawnVelocityJumpDistanceCm = 5000.0;
	constexpr float TDRealmPassagePaceMinimumScale = 0.3f;
	constexpr float TDRealmPassagePaceSlowStartCm = -2500.0f;
	constexpr float TDRealmPassagePaceSlowEndCm = -300.0f;
	constexpr float TDRealmPassagePaceDecreasePerSecond = 0.5f;
	constexpr float TDRealmPassagePaceIncreasePerSecond = 1.0f;
	constexpr float TDRealmPassagePaceApplyThreshold = 0.005f;
	constexpr double TDRealmPassagePaceMovingAwaySpeedCm = 10.0;

	bool IsInsideRealmLiftShaft(const FVector& ShaftLocalCm, double TravelLengthCm)
	{
		return ShaftLocalCm.X >= TDRealmShaftInteriorBackVCm - TDRealmShaftInteriorMarginCm
			&& ShaftLocalCm.X <= TDRealmShaftInteriorFaceVCm + TDRealmShaftInteriorMarginCm
			&& FMath::Abs(ShaftLocalCm.Y) <= TDRealmShaftInteriorHalfWidthCm + TDRealmShaftInteriorMarginCm
			&& ShaftLocalCm.Z >= -TravelLengthCm - TDRealmShaftVerticalMarginCm
			&& ShaftLocalCm.Z <= TDRealmShaftVerticalMarginCm;
	}

	bool IsInsideRealmLiftPit(const FTDRealmGate& Gate, const FVector& LocationCm)
	{
		const FVector TopLandingCm = Gate.Lift.FieldLandingTransform.GetLocation();
		const double BottomLandingZCm = Gate.Lift.RealmLandingTransform.GetLocation().Z;
		return FVector::DistSquared2D(LocationCm, TopLandingCm) <= FMath::Square(TDRealmLiftPitRecoveryRadiusCm)
			&& LocationCm.Z > BottomLandingZCm + TDRealmLiftPitRecoveryMarginCm
			&& LocationCm.Z < TopLandingCm.Z - TDRealmLiftPitRecoveryMarginCm;
	}

	template <typename TActor, typename TComponent>
	TComponent* FindFirstActorComponent(UWorld& World)
	{
		for (TActorIterator<TActor> ActorIterator(&World); ActorIterator; ++ActorIterator)
		{
			if (TComponent* Component = ActorIterator->template FindComponentByClass<TComponent>())
			{
				return Component;
			}
		}
		return nullptr;
	}

	int32 QuantizeAtmosphereStep(float Value)
	{
		return FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * TDAtmosphereQuantizationSteps);
	}

	float GetAtmosphereStepValue(int32 Step)
	{
		return static_cast<float>(Step) / TDAtmosphereQuantizationSteps;
	}

	FName MakeRealmPostProcessTag(FName RealmId)
	{
		return FName(*FString::Printf(TEXT("TDRealm:%s"), *RealmId.ToString()));
	}
}

bool UTDRealmSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UTDRealmSubsystem* UTDRealmSubsystem::GetRealmSubsystem(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UTDRealmSubsystem>() : nullptr;
}

void UTDRealmSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	LoadConfiguredRealmDefinitions();
	SpawnRealmLifts();
	CaptureFieldAtmosphere();
	if (!bHasCapturedFieldAtmosphere)
	{
		UE_LOG(LogTDGame, Log, TEXT("Realm: no directional light, sky light, sky atmosphere or height fog found at begin play; capture is retried on demand."));
	}
	bHasBegunPlay = true;
}

void UTDRealmSubsystem::Deinitialize()
{
	bHasBegunPlay = false;
	RestorePassagePace();
	PacedCharacter.Reset();
	LoadedRealmDefinitions.Reset();
	RealmLiftDecks.Reset();
	RealmShaftLiners.Reset();
	RealmPostProcessVolumes.Reset();
	ActivatedGameplayRealmIds.Reset();
	bHasPreviousPawnLocation = false;
	Super::Deinitialize();
}

ATDRealmLiftDeck* UTDRealmSubsystem::FindRealmLiftDeck(FName GateId) const
{
	for (ATDRealmLiftDeck* Lift : RealmLiftDecks)
	{
		if (Lift && Lift->GetGateId() == GateId)
		{
			return Lift;
		}
	}
	return nullptr;
}

float UTDRealmSubsystem::ComputeConnectorWeight(double AlongCm, double ApproachLengthCm, double DepartureLengthCm)
{
	if (AlongCm <= 0.0)
	{
		return static_cast<float>(0.5 * FMath::SmoothStep(-ApproachLengthCm, 0.0, AlongCm));
	}
	return static_cast<float>(0.5 + 0.5 * FMath::SmoothStep(0.0, DepartureLengthCm, AlongCm));
}

float UTDRealmSubsystem::ComputeLiftShaftWeight(double DepthCm, double TravelLengthCm)
{
	return static_cast<float>(FMath::SmoothStep(0.0, TravelLengthCm, DepthCm));
}

float UTDRealmSubsystem::ComputePassagePaceTarget(double ApproachCm, bool bIsReady, bool bIsPreloadRejected, bool bIsMovingAway)
{
	if (bIsReady || bIsPreloadRejected || bIsMovingAway)
	{
		return 1.0f;
	}

	const float SlowAlpha = FMath::SmoothStep(TDRealmPassagePaceSlowStartCm, TDRealmPassagePaceSlowEndCm, static_cast<float>(ApproachCm));
	return FMath::Lerp(1.0f, TDRealmPassagePaceMinimumScale, SlowAlpha);
}

float UTDRealmSubsystem::StepPassagePaceScale(float CurrentScale, float TargetScale, float DeltaSeconds)
{
	const float StepSeconds = FMath::Max(DeltaSeconds, 0.0f);
	if (TargetScale < CurrentScale)
	{
		return FMath::Max(TargetScale, CurrentScale - TDRealmPassagePaceDecreasePerSecond * StepSeconds);
	}
	return FMath::Min(TargetScale, CurrentScale + TDRealmPassagePaceIncreasePerSecond * StepSeconds);
}

bool UTDRealmSubsystem::IsTickable() const
{
	return bHasBegunPlay && FindLocalPlayerPawn() != nullptr;
}

TStatId UTDRealmSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDRealmSubsystem, STATGROUP_Tickables);
}

void UTDRealmSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APawn* Pawn = FindLocalPlayerPawn();
	if (!Pawn)
	{
		return;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	const FVector PawnVelocityCm = MeasurePawnVelocity(PawnLocation, DeltaTime);
	const FTDPassageLocation Passage = FindPassageLocation(PawnLocation);
	UpdatePassageDirection(*Pawn, Passage, PawnVelocityCm);
	UpdatePassagePreload(Passage);
	ActivateGameplayDataLayers(PawnLocation, Passage);
	UpdatePassagePace(*Pawn, Passage, PawnVelocityCm, DeltaTime);
	UpdateAtmosphere(PawnLocation, Passage);
	if (RefreshRealmPostProcessVolumes())
	{
		ApplyPostProcessWeights();
	}
	RecoverFallenPawnIfNeeded(*Pawn);
}

void UTDRealmSubsystem::LoadConfiguredRealmDefinitions()
{
	LoadedRealmDefinitions.Reset();
	const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
	if (!Settings)
	{
		return;
	}

	for (const TSoftObjectPtr<UTDRealmDefinition>& RealmReference : Settings->RealmDefinitions)
	{
		UObject* LoadedObject = RealmReference.IsNull() ? nullptr : RealmReference.ToSoftObjectPath().TryLoad(nullptr, static_cast<ELoadFlags>(LOAD_NoWarn | LOAD_Quiet));
		if (UTDRealmDefinition* Realm = Cast<UTDRealmDefinition>(LoadedObject))
		{
			LoadedRealmDefinitions.Add(Realm);
		}
	}
	UE_LOG(LogTDGame, Log, TEXT("Realm: %d of %d configured realm definitions loaded."), LoadedRealmDefinitions.Num(), Settings->RealmDefinitions.Num());
}

void UTDRealmSubsystem::SpawnRealmLifts()
{
	RealmLiftDecks.Reset();
	RealmShaftLiners.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (const UTDRealmDefinition* Realm : LoadedRealmDefinitions)
	{
		if (!Realm)
		{
			continue;
		}

		for (const FTDRealmGate& Gate : Realm->Gates)
		{
			if (Gate.GateKind != ETDRealmGateKind::Lift)
			{
				continue;
			}

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParameters.ObjectFlags |= RF_Transient;
			const FTransform SpawnTransform(FRotator(0.0, Gate.Lift.FieldLandingTransform.Rotator().Yaw, 0.0), Gate.Lift.FieldLandingTransform.GetLocation());
			ATDRealmLiftDeck* Lift = World->SpawnActor<ATDRealmLiftDeck>(ATDRealmLiftDeck::StaticClass(), SpawnTransform, SpawnParameters);
			if (!Lift)
			{
				UE_LOG(LogTDGame, Warning, TEXT("Realm: failed to spawn the lift of gate '%s' in realm '%s'."), *Gate.GateId.ToString(), *Realm->RealmId.ToString());
				continue;
			}

			Lift->InitializeLift(Gate);
			RealmLiftDecks.Add(Lift);
			SpawnShaftLiner(*World, Gate, *Lift);
			UE_LOG(LogTDGame, Log, TEXT("Realm: lift of gate '%s' spawned at the field landing %s."), *Gate.GateId.ToString(), *Lift->GetActorLocation().ToCompactString());
		}
	}
}

void UTDRealmSubsystem::SpawnShaftLiner(UWorld& World, const FTDRealmGate& Gate, ATDRealmLiftDeck& Lift)
{
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	ATDRealmShaftLiner* Liner = World.SpawnActor<ATDRealmShaftLiner>(ATDRealmShaftLiner::StaticClass(), ATDRealmLiftDeck::MakeShaftFrameTransform(Gate), SpawnParameters);
	if (!Liner)
	{
		UE_LOG(LogTDGame, Warning, TEXT("Realm: failed to spawn the shaft liner of lift gate '%s'."), *Gate.GateId.ToString());
		return;
	}

	Liner->InitializeLiner(Gate, Lift);
	RealmShaftLiners.Add(Liner);
}

void UTDRealmSubsystem::CaptureFieldAtmosphere()
{
	UWorld* World = GetWorld();
	if (!World || bHasCapturedFieldAtmosphere)
	{
		return;
	}

	SunComponent = FindFirstActorComponent<ADirectionalLight, UDirectionalLightComponent>(*World);
	SkyLightComponent = FindFirstActorComponent<ASkyLight, USkyLightComponent>(*World);
	SkyAtmosphereComponent = FindFirstActorComponent<ASkyAtmosphere, USkyAtmosphereComponent>(*World);
	HeightFogComponent = FindFirstActorComponent<AExponentialHeightFog, UExponentialHeightFogComponent>(*World);

	if (const UDirectionalLightComponent* Sun = SunComponent.Get())
	{
		FieldAtmosphere.SunRotation = Sun->GetOwner() ? Sun->GetOwner()->GetActorRotation() : Sun->GetComponentRotation();
		FieldAtmosphere.SunIntensityLux = Sun->Intensity;
		FieldAtmosphere.SunColor = Sun->GetLightColor();
	}
	if (const USkyLightComponent* SkyLight = SkyLightComponent.Get())
	{
		FieldAtmosphere.SkyLightIntensity = SkyLight->Intensity;
		FieldAtmosphere.SkyLightColor = SkyLight->GetLightColor();
	}
	if (const USkyAtmosphereComponent* SkyAtmosphere = SkyAtmosphereComponent.Get())
	{
		FieldAtmosphere.SkyLuminanceFactor = SkyAtmosphere->SkyLuminanceFactor;
		FieldAtmosphere.RayleighScattering = SkyAtmosphere->RayleighScattering;
		FieldAtmosphere.MieScatteringScale = SkyAtmosphere->MieScatteringScale;
	}
	if (const UExponentialHeightFogComponent* Fog = HeightFogComponent.Get())
	{
		FieldAtmosphere.FogDensity = Fog->FogDensity;
		FieldAtmosphere.FogHeightFalloff = Fog->FogHeightFalloff;
		FieldAtmosphere.FogHeightCm = Fog->GetComponentLocation().Z;
		FieldAtmosphere.FogInscatteringColor = Fog->FogInscatteringLuminance;
		FieldAtmosphere.FogMaxOpacity = Fog->FogMaxOpacity;
		FieldAtmosphere.FogStartDistanceCm = Fog->StartDistance;
		FieldAtmosphere.SecondFogDensity = Fog->SecondFogData.FogDensity;
		FieldAtmosphere.SecondFogHeightFalloff = Fog->SecondFogData.FogHeightFalloff;
		FieldAtmosphere.SecondFogHeightOffsetCm = Fog->SecondFogData.FogHeightOffset;
		FieldAtmosphere.VolumetricFogAlbedo = FLinearColor(Fog->VolumetricFogAlbedo);
		FieldAtmosphere.VolumetricFogEmissive = Fog->VolumetricFogEmissive;
		FieldAtmosphere.VolumetricFogExtinctionScale = Fog->VolumetricFogExtinctionScale;
	}

	bHasCapturedFieldAtmosphere = SunComponent.IsValid() || SkyLightComponent.IsValid() || SkyAtmosphereComponent.IsValid() || HeightFogComponent.IsValid();
}

UTDRealmSubsystem::FTDPassageLocation UTDRealmSubsystem::MakePassageLocation(const UTDRealmDefinition& Realm, const FTDRealmGate& Gate, double AlongCm, float Weight, bool bIsLift)
{
	FTDPassageLocation Passage;
	Passage.Realm = &Realm;
	Passage.Gate = &Gate;
	Passage.AlongCm = AlongCm;
	Passage.Weight = Weight;
	Passage.bIsLift = bIsLift;
	return Passage;
}

UTDRealmSubsystem::FTDPassageLocation UTDRealmSubsystem::FindPassageLocation(const FVector& LocationCm) const
{
	FTDPassageLocation Nearest;
	double NearestDistanceSquared = TNumericLimits<double>::Max();
	for (const UTDRealmDefinition* Realm : LoadedRealmDefinitions)
	{
		if (!Realm)
		{
			continue;
		}

		for (const FTDRealmGate& Gate : Realm->Gates)
		{
			FTDPassageLocation Measured;
			double DistanceSquared = 0.0;
			const bool bHasMeasured = Gate.GateKind == ETDRealmGateKind::Lift
				? TryMeasureLiftGate(*Realm, Gate, LocationCm, Measured, DistanceSquared)
				: TryMeasureConnector(*Realm, Gate, LocationCm, Measured, DistanceSquared);
			if (!bHasMeasured || DistanceSquared >= NearestDistanceSquared)
			{
				continue;
			}

			NearestDistanceSquared = DistanceSquared;
			Nearest = Measured;
		}
	}
	return Nearest;
}

bool UTDRealmSubsystem::TryMeasureConnector(const UTDRealmDefinition& Realm, const FTDRealmGate& Gate, const FVector& LocationCm, FTDPassageLocation& OutPassage, double& OutDistanceSquared)
{
	const FVector Direction = Gate.GetPassageDirection();
	const double ApproachLengthCm = Gate.ApproachLengthCm;
	const double DepartureLengthCm = Gate.DepartureLengthCm;
	const double ConnectorLengthCm = ApproachLengthCm + DepartureLengthCm;
	if (Direction.IsNearlyZero() || ConnectorLengthCm <= 0.0)
	{
		return false;
	}

	const FVector BoundaryCm = Gate.GetBoundaryCrossingCm();
	const FVector OffsetCm(LocationCm.X - BoundaryCm.X, LocationCm.Y - BoundaryCm.Y, 0.0);
	const double AlongCm = FVector::DotProduct(OffsetCm, Direction);
	const double AcrossCm = FVector::DotProduct(OffsetCm, FVector(-Direction.Y, Direction.X, 0.0));
	const bool bIsInsideWindow = FMath::Abs(AcrossCm) <= Gate.PassageHalfWidthCm
		&& AlongCm >= -(ApproachLengthCm + Gate.PreloadLeadCm)
		&& AlongCm <= DepartureLengthCm + Gate.PreloadLeadCm;
	if (!bIsInsideWindow)
	{
		return false;
	}

	const double FloorAlpha = FMath::Clamp((AlongCm + ApproachLengthCm) / ConnectorLengthCm, 0.0, 1.0);
	const double ConnectorZCm = FMath::Lerp(Gate.FieldReturnTransform.GetLocation().Z, Gate.ArrivalTransform.GetLocation().Z, FloorAlpha);
	if (FMath::Abs(LocationCm.Z - ConnectorZCm) > TDPassageVerticalToleranceCm)
	{
		return false;
	}

	OutPassage = MakePassageLocation(Realm, Gate, AlongCm, ComputeConnectorWeight(AlongCm, ApproachLengthCm, DepartureLengthCm), false);
	OutDistanceSquared = FMath::Square(AcrossCm);
	return true;
}

bool UTDRealmSubsystem::TryMeasureLiftGate(const UTDRealmDefinition& Realm, const FTDRealmGate& Gate, const FVector& LocationCm, FTDPassageLocation& OutPassage, double& OutDistanceSquared) const
{
	const FVector TopLandingCm = Gate.Lift.FieldLandingTransform.GetLocation();
	const FVector BottomLandingCm = Gate.Lift.RealmLandingTransform.GetLocation();
	const double TravelLengthCm = TopLandingCm.Z - BottomLandingCm.Z;
	if (TravelLengthCm <= 0.0)
	{
		return false;
	}

	const double HalfTravelCm = TravelLengthCm * 0.5;
	const ATDRealmLiftDeck* Lift = FindRealmLiftDeck(Gate.GateId);
	const bool bIsCarried = Lift && Lift->IsCarryingLocation(LocationCm);
	const FVector ShaftLocalCm = ATDRealmLiftDeck::MakeShaftFrameTransform(Gate).InverseTransformPositionNoScale(LocationCm);
	if (bIsCarried || IsInsideRealmLiftShaft(ShaftLocalCm, TravelLengthCm))
	{
		const double DepthCm = bIsCarried ? Lift->GetMotion().PositionCm : FMath::Clamp(-ShaftLocalCm.Z, 0.0, TravelLengthCm);
		OutPassage = MakePassageLocation(Realm, Gate, DepthCm - HalfTravelCm, ComputeLiftShaftWeight(DepthCm, TravelLengthCm), true);
		OutDistanceSquared = 0.0;
		return true;
	}

	const FVector SlotEndCm = Gate.ArrivalTransform.GetLocation();
	const FVector SlotSpanCm(SlotEndCm.X - BottomLandingCm.X, SlotEndCm.Y - BottomLandingCm.Y, 0.0);
	const double SlotLengthCm = SlotSpanCm.Size();
	if (SlotLengthCm > UE_KINDA_SMALL_NUMBER)
	{
		const FVector SlotDirection = SlotSpanCm / SlotLengthCm;
		const FVector SlotOffsetCm(LocationCm.X - BottomLandingCm.X, LocationCm.Y - BottomLandingCm.Y, 0.0);
		const double SlotAlongCm = FVector::DotProduct(SlotOffsetCm, SlotDirection);
		const double SlotAcrossCm = FVector::DotProduct(SlotOffsetCm, FVector(-SlotDirection.Y, SlotDirection.X, 0.0));
		const bool bIsInsideSlot = SlotAlongCm >= 0.0 && SlotAlongCm <= SlotLengthCm + Gate.PreloadLeadCm
			&& FMath::Abs(SlotAcrossCm) <= TDRealmLiftSlotHalfWidthCm
			&& FMath::Abs(LocationCm.Z - BottomLandingCm.Z) <= TDRealmLiftSlotHeightToleranceCm;
		if (bIsInsideSlot)
		{
			const bool bIsRealmWeighted = SlotAlongCm <= SlotLengthCm || Realm.ContainsLocationXY(LocationCm);
			OutPassage = MakePassageLocation(Realm, Gate, HalfTravelCm + SlotAlongCm, bIsRealmWeighted ? 1.0f : 0.0f, true);
			OutDistanceSquared = FMath::Square(SlotAcrossCm);
			return true;
		}
	}

	const double TopDistanceSquared = FVector::DistSquared2D(LocationCm, TopLandingCm);
	const bool bIsNearTopLanding = TopDistanceSquared <= FMath::Square(static_cast<double>(Gate.Lift.ApproachRadiusCm))
		&& LocationCm.Z >= TopLandingCm.Z - TDRealmShaftVerticalMarginCm
		&& LocationCm.Z <= TopLandingCm.Z + TDPassageVerticalToleranceCm;
	if (!bIsNearTopLanding)
	{
		return false;
	}

	OutPassage = MakePassageLocation(Realm, Gate, -(HalfTravelCm + FMath::Sqrt(TopDistanceSquared)), Realm.ContainsLocationXY(LocationCm) ? 1.0f : 0.0f, true);
	OutDistanceSquared = TopDistanceSquared;
	return true;
}

FVector UTDRealmSubsystem::MeasurePawnVelocity(const FVector& PawnLocationCm, float DeltaTime)
{
	const FVector DisplacementCm = PawnLocationCm - PreviousPawnLocationCm;
	const bool bCanMeasure = bHasPreviousPawnLocation && DeltaTime > UE_SMALL_NUMBER && DisplacementCm.SizeSquared() <= FMath::Square(TDRealmPawnVelocityJumpDistanceCm);
	PreviousPawnLocationCm = PawnLocationCm;
	bHasPreviousPawnLocation = true;
	return bCanMeasure ? DisplacementCm / DeltaTime : FVector::ZeroVector;
}

void UTDRealmSubsystem::UpdatePassageDirection(const APawn& Pawn, const FTDPassageLocation& Passage, const FVector& PawnVelocityCm)
{
	if (!Passage.Gate)
	{
		TrackedPassageGateId = NAME_None;
		return;
	}

	const FTDRealmGate& Gate = *Passage.Gate;
	const bool bWasTracking = TrackedPassageGateId == Gate.GateId;
	TrackedPassageGateId = Gate.GateId;
	if (Passage.bIsLift)
	{
		const ATDRealmLiftDeck* Lift = FindRealmLiftDeck(Gate.GateId);
		const bool bIsRiding = Lift && Lift->IsCarryingPawn(Pawn);
		bIsTrackedPassageToEntry = bIsRiding ? Lift->GetMotion().Direction == ETDRealmLiftDirection::Down : Passage.AlongCm < 0.0;
		return;
	}

	const double AxisSpeedCmPerSecond = FVector::DotProduct(PawnVelocityCm, Gate.GetPassageDirection());
	if (FMath::Abs(AxisSpeedCmPerSecond) > TDRealmPassageDirectionSpeedCm)
	{
		bIsTrackedPassageToEntry = AxisSpeedCmPerSecond > 0.0;
		return;
	}
	if (!bWasTracking)
	{
		bIsTrackedPassageToEntry = Passage.AlongCm < 0.0;
	}
}

void UTDRealmSubsystem::UpdatePassagePreload(const FTDPassageLocation& Passage)
{
	UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(this);
	if (!Travel)
	{
		return;
	}

	if (!Passage.Gate)
	{
		FailedPreloadGateId = NAME_None;
		CancelOwnedPreload(*Travel);
		return;
	}

	const FName GateId = Passage.Gate->GateId;
	const bool bToEntry = bIsTrackedPassageToEntry;
	if (FailedPreloadGateId == GateId && bIsFailedPreloadToEntry == bToEntry)
	{
		return;
	}

	if (Travel->GetTravelState() != ETDSeamlessTravelState::Idle)
	{
		if (!Travel->IsUnrequestedPreload())
		{
			return;
		}
		if (Travel->BeginPreload(GateId, bToEntry))
		{
			RecordOwnedPreload(GateId, bToEntry);
			return;
		}

		UE_LOG(LogTDGame, Log, TEXT("Realm: replacing unrequested preload '%s' with passage '%s' (%s)."), *Travel->GetActiveDungeonId().ToString(), *GateId.ToString(), bToEntry ? TEXT("entry") : TEXT("field return"));
		Travel->CancelTravel();
	}

	if (!Travel->BeginPreload(GateId, bToEntry))
	{
		FailedPreloadGateId = GateId;
		bIsFailedPreloadToEntry = bToEntry;
		UE_LOG(LogTDGame, Warning, TEXT("Realm: passage preload '%s' (%s) was refused."), *GateId.ToString(), bToEntry ? TEXT("entry") : TEXT("field return"));
		return;
	}

	RecordOwnedPreload(GateId, bToEntry);
}

void UTDRealmSubsystem::RecordOwnedPreload(FName GateId, bool bToEntry)
{
	if (OwnedPreloadGateId == GateId && bIsOwnedPreloadToEntry == bToEntry)
	{
		return;
	}

	OwnedPreloadGateId = GateId;
	bIsOwnedPreloadToEntry = bToEntry;
	const UWorld* World = GetWorld();
	OwnedPreloadStartWorldSeconds = World ? World->GetTimeSeconds() : 0.0;
}

void UTDRealmSubsystem::CancelOwnedPreload(UTDSeamlessTravelSubsystem& Travel)
{
	if (OwnedPreloadGateId.IsNone())
	{
		return;
	}

	Travel.CancelPreload(OwnedPreloadGateId, bIsOwnedPreloadToEntry);
	OwnedPreloadGateId = NAME_None;
}

void UTDRealmSubsystem::ActivateGameplayDataLayers(const FVector& LocationCm, const FTDPassageLocation& Passage)
{
	if (Passage.Realm && Passage.Weight > 0.0f)
	{
		ActivateGameplayDataLayer(*Passage.Realm, TEXT("connector"));
	}
	if (const UTDRealmDefinition* Realm = FindLoadedRealmContainingLocation(LocationCm))
	{
		ActivateGameplayDataLayer(*Realm, TEXT("inside the realm"));
	}
}

void UTDRealmSubsystem::ActivateGameplayDataLayer(const UTDRealmDefinition& Realm, const TCHAR* Reason)
{
	if (ActivatedGameplayRealmIds.Contains(Realm.RealmId))
	{
		return;
	}

	ActivatedGameplayRealmIds.Add(Realm.RealmId);
	if (Realm.GameplayDataLayer.IsNull())
	{
		UE_LOG(LogTDGame, Log, TEXT("Realm: realm '%s' has no gameplay data layer to activate (%s)."), *Realm.RealmId.ToString(), Reason);
		return;
	}

	const UDataLayerAsset* DataLayerAsset = Realm.GameplayDataLayer.LoadSynchronous();
	UDataLayerManager* DataLayerManager = UDataLayerManager::GetDataLayerManager(GetWorld());
	if (!DataLayerAsset || !DataLayerManager)
	{
		UE_LOG(LogTDGame, Warning, TEXT("Realm: gameplay data layer '%s' of realm '%s' was not activated (asset loaded %d, data layer manager %d)."),
			*Realm.GameplayDataLayer.ToSoftObjectPath().ToString(), *Realm.RealmId.ToString(), DataLayerAsset ? 1 : 0, DataLayerManager ? 1 : 0);
		return;
	}

	const bool bIsAccepted = DataLayerManager->SetDataLayerRuntimeState(DataLayerAsset, EDataLayerRuntimeState::Activated);
	UE_LOG(LogTDGame, Log, TEXT("Realm: gameplay data layer '%s' of realm '%s' set to Activated (%s, accepted %d)."), *DataLayerAsset->GetName(), *Realm.RealmId.ToString(), Reason, bIsAccepted ? 1 : 0);
}

void UTDRealmSubsystem::UpdatePassagePace(APawn& Pawn, const FTDPassageLocation& Passage, const FVector& PawnVelocityCm, float DeltaTime)
{
	ATDGameCharacter* Character = Cast<ATDGameCharacter>(&Pawn);
	if (PacedCharacter.Get() != Character)
	{
		RestorePassagePace();
		PacedCharacter = Character;
	}
	if (!Character)
	{
		return;
	}

	if (!Passage.Gate || Passage.bIsLift)
	{
		StopPassagePacing();
		RestorePassagePace();
		return;
	}

	const FTDRealmGate& Gate = *Passage.Gate;
	const bool bToEntry = bIsTrackedPassageToEntry;
	const double ApproachCm = bToEntry ? Passage.AlongCm : -Passage.AlongCm;
	if (PacingGateId != Gate.GateId || bIsPacingToEntry != bToEntry)
	{
		StopPassagePacing();
		PacingGateId = Gate.GateId;
		bIsPacingToEntry = bToEntry;
		bIsPacingArmed = ApproachCm <= 0.0;
	}

	const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(this);
	const bool bIsReady = Travel && Travel->IsReadyFor(Gate.GateId, bToEntry);
	const bool bIsPreloadRejected = FailedPreloadGateId == Gate.GateId && bIsFailedPreloadToEntry == bToEntry;
	const FVector TowardBoundaryDirection = Gate.GetPassageDirection() * (bToEntry ? 1.0 : -1.0);
	const bool bIsMovingAway = FVector::DotProduct(PawnVelocityCm, TowardBoundaryDirection) < -TDRealmPassagePaceMovingAwaySpeedCm;
	const float TargetScale = bIsPacingArmed ? ComputePassagePaceTarget(ApproachCm, bIsReady, bIsPreloadRejected, bIsMovingAway) : 1.0f;
	PacingAlongCm = Passage.AlongCm;
	SetPassagePacingEngaged(TargetScale < 1.0f, bIsReady);
	CurrentPassagePaceScale = StepPassagePaceScale(CurrentPassagePaceScale, TargetScale, DeltaTime);
	ApplyPassagePace();
}

void UTDRealmSubsystem::SetPassagePacingEngaged(bool bIsEngaged, bool bIsReady)
{
	if (bIsEngaged == bIsPassagePacingEngaged)
	{
		return;
	}

	bIsPassagePacingEngaged = bIsEngaged;
	PassagePacingEngagedCount += bIsEngaged ? 1 : 0;
	const UWorld* World = GetWorld();
	const double ElapsedSeconds = World && !OwnedPreloadGateId.IsNone() ? World->GetTimeSeconds() - OwnedPreloadStartWorldSeconds : 0.0;
	UE_LOG(LogTDGame, Warning, TEXT("Realm: pacing %s gate=%s along=%.0f ready=%d elapsed=%.2f"),
		bIsEngaged ? TEXT("engaged") : TEXT("released"), *PacingGateId.ToString(), PacingAlongCm, bIsReady ? 1 : 0, ElapsedSeconds);
}

void UTDRealmSubsystem::StopPassagePacing()
{
	SetPassagePacingEngaged(false, false);
	PacingGateId = NAME_None;
	bIsPacingArmed = false;
}

void UTDRealmSubsystem::ApplyPassagePace()
{
	const bool bHasChangedEnough = FMath::Abs(CurrentPassagePaceScale - AppliedPassagePaceScale) > TDRealmPassagePaceApplyThreshold;
	const bool bHasReturnedToFull = CurrentPassagePaceScale >= 1.0f && AppliedPassagePaceScale < 1.0f;
	if (!bHasChangedEnough && !bHasReturnedToFull)
	{
		return;
	}

	AppliedPassagePaceScale = CurrentPassagePaceScale;
	if (ATDGameCharacter* Character = PacedCharacter.Get())
	{
		Character->SetPassagePaceScale(CurrentPassagePaceScale);
	}
}

void UTDRealmSubsystem::RestorePassagePace()
{
	CurrentPassagePaceScale = 1.0f;
	ApplyPassagePace();
}

void UTDRealmSubsystem::ApplyAtmosphereForLocation(const FVector& LocationCm)
{
	CaptureFieldAtmosphere();
	const FTDPassageLocation Passage = FindPassageLocation(LocationCm);
	ActivateGameplayDataLayers(LocationCm, Passage);
	UpdateAtmosphere(LocationCm, Passage);
}

float UTDRealmSubsystem::GetRealmWeight(FName RealmId) const
{
	if (RealmId.IsNone() || RealmId != CurrentRealmId)
	{
		return 0.0f;
	}
	return CurrentRealmWeight;
}

void UTDRealmSubsystem::UpdateAtmosphere(const FVector& LocationCm, const FTDPassageLocation& Passage)
{
	const UTDRealmDefinition* Realm = Passage.Realm ? Passage.Realm : FindLoadedRealmContainingLocation(LocationCm);
	const float Weight = Passage.Gate ? Passage.Weight : (Realm ? 1.0f : 0.0f);
	const FName RealmId = Realm ? Realm->RealmId : NAME_None;
	const int32 WeightStep = QuantizeAtmosphereStep(Weight);

	CurrentRealmId = RealmId;
	CurrentRealmWeight = Realm ? Weight : 0.0f;
	const FName NewActiveRealmId = Realm && Weight >= 0.5f ? RealmId : NAME_None;
	if (NewActiveRealmId != ActiveRealmId)
	{
		ActiveRealmId = NewActiveRealmId;
		UE_LOG(LogTDGame, Log, TEXT("Realm: active atmosphere '%s' (weight %.2f)."), ActiveRealmId.IsNone() ? TEXT("Field") : *ActiveRealmId.ToString(), Weight);
	}

	const int32 SunStep = WeightStep / 2;
	if (RealmId != AppliedSunRealmId || SunStep != AppliedSunStep)
	{
		CaptureFieldAtmosphere();
		AppliedSunRealmId = RealmId;
		AppliedSunStep = SunStep;
		const float SunAlpha = static_cast<float>(SunStep) / (TDAtmosphereQuantizationSteps * 0.5f);
		ApplySunRotation(Realm ? FQuat::Slerp(FieldAtmosphere.SunRotation.Quaternion(), Realm->Atmosphere.SunRotation.Quaternion(), SunAlpha).Rotator() : FieldAtmosphere.SunRotation);
	}

	if (RealmId == AppliedRealmId && WeightStep == AppliedWeightStep)
	{
		return;
	}

	AppliedRealmId = RealmId;
	AppliedWeightStep = WeightStep;
	CaptureFieldAtmosphere();
	ApplyAtmosphere(Realm ? MakeBlendedAtmosphere(*Realm, GetAtmosphereStepValue(WeightStep)) : FieldAtmosphere);
	ApplyPostProcessWeights();
}

FTDRealmAtmosphere UTDRealmSubsystem::MakeBlendedAtmosphere(const UTDRealmDefinition& Realm, float Weight) const
{
	const FTDRealmAtmosphere& Field = FieldAtmosphere;
	const FTDRealmAtmosphere& Target = Realm.Atmosphere;
	FTDRealmAtmosphere Blended = Field;
	Blended.SunIntensityLux = FMath::Lerp(Field.SunIntensityLux, Target.SunIntensityLux, Weight);
	Blended.SunColor = FMath::Lerp(Field.SunColor, Target.SunColor, Weight);
	Blended.SkyLightIntensity = FMath::Lerp(Field.SkyLightIntensity, Target.SkyLightIntensity, Weight);
	Blended.SkyLightColor = FMath::Lerp(Field.SkyLightColor, Target.SkyLightColor, Weight);
	Blended.SkyLuminanceFactor = FMath::Lerp(Field.SkyLuminanceFactor, Target.SkyLuminanceFactor, Weight);
	Blended.RayleighScattering = FMath::Lerp(Field.RayleighScattering, Target.RayleighScattering, Weight);
	Blended.MieScatteringScale = FMath::Lerp(Field.MieScatteringScale, Target.MieScatteringScale, Weight);
	Blended.FogDensity = FMath::Lerp(Field.FogDensity, Target.FogDensity, Weight);
	Blended.FogHeightFalloff = FMath::Lerp(Field.FogHeightFalloff, Target.FogHeightFalloff, Weight);
	Blended.FogHeightCm = FMath::Lerp(Field.FogHeightCm, Target.FogHeightCm, Weight);
	Blended.FogInscatteringColor = FMath::Lerp(Field.FogInscatteringColor, Target.FogInscatteringColor, Weight);
	Blended.FogMaxOpacity = FMath::Lerp(Field.FogMaxOpacity, Target.FogMaxOpacity, Weight);
	Blended.FogStartDistanceCm = FMath::Lerp(Field.FogStartDistanceCm, Target.FogStartDistanceCm, Weight);
	Blended.SecondFogDensity = FMath::Lerp(Field.SecondFogDensity, Target.SecondFogDensity, Weight);
	Blended.SecondFogHeightFalloff = FMath::Lerp(Field.SecondFogHeightFalloff, Target.SecondFogHeightFalloff, Weight);
	Blended.SecondFogHeightOffsetCm = FMath::Lerp(Field.SecondFogHeightOffsetCm, Target.SecondFogHeightOffsetCm, Weight);
	Blended.VolumetricFogAlbedo = FMath::Lerp(Field.VolumetricFogAlbedo, Target.VolumetricFogAlbedo, Weight);
	Blended.VolumetricFogEmissive = FMath::Lerp(Field.VolumetricFogEmissive, Target.VolumetricFogEmissive, Weight);
	Blended.VolumetricFogExtinctionScale = FMath::Lerp(Field.VolumetricFogExtinctionScale, Target.VolumetricFogExtinctionScale, Weight);
	return Blended;
}

void UTDRealmSubsystem::ApplyAtmosphere(const FTDRealmAtmosphere& Atmosphere)
{
	if (UDirectionalLightComponent* Sun = SunComponent.Get())
	{
		ApplySun(*Sun, Atmosphere);
	}
	if (USkyLightComponent* SkyLight = SkyLightComponent.Get())
	{
		SkyLight->SetIntensity(Atmosphere.SkyLightIntensity);
		SkyLight->SetLightColor(Atmosphere.SkyLightColor);
	}
	if (USkyAtmosphereComponent* SkyAtmosphere = SkyAtmosphereComponent.Get())
	{
		SkyAtmosphere->SetRayleighScattering(Atmosphere.RayleighScattering);
		SkyAtmosphere->SetMieScatteringScale(Atmosphere.MieScatteringScale);
		SkyAtmosphere->SetSkyLuminanceFactor(Atmosphere.SkyLuminanceFactor);
	}
	if (UExponentialHeightFogComponent* Fog = HeightFogComponent.Get())
	{
		ApplyFog(*Fog, Atmosphere);
	}
}

void UTDRealmSubsystem::ApplySun(UDirectionalLightComponent& Sun, const FTDRealmAtmosphere& Atmosphere)
{
	Sun.SetIntensity(Atmosphere.SunIntensityLux);
	Sun.SetLightColor(Atmosphere.SunColor);
}

void UTDRealmSubsystem::ApplySunRotation(const FRotator& SunRotation)
{
	UDirectionalLightComponent* Sun = SunComponent.Get();
	if (!Sun)
	{
		return;
	}

	if (Sun->Mobility == EComponentMobility::Static)
	{
		if (!bHasWarnedStaticSun)
		{
			bHasWarnedStaticSun = true;
			UE_LOG(LogTDGame, Warning, TEXT("Realm: directional light '%s' is static; sun rotation is not changed."), *Sun->GetPathName());
		}
		return;
	}

	if (AActor* SunActor = Sun->GetOwner())
	{
		SunActor->SetActorRotation(SunRotation);
	}
}

void UTDRealmSubsystem::ApplyFog(UExponentialHeightFogComponent& Fog, const FTDRealmAtmosphere& Atmosphere)
{
	Fog.SetFogDensity(Atmosphere.FogDensity);
	Fog.SetFogHeightFalloff(Atmosphere.FogHeightFalloff);
	Fog.SetFogInscatteringColor(Atmosphere.FogInscatteringColor);
	Fog.SetFogMaxOpacity(Atmosphere.FogMaxOpacity);
	Fog.SetStartDistance(Atmosphere.FogStartDistanceCm);
	Fog.SetSecondFogDensity(Atmosphere.SecondFogDensity);
	Fog.SetSecondFogHeightFalloff(Atmosphere.SecondFogHeightFalloff);
	Fog.SetSecondFogHeightOffset(Atmosphere.SecondFogHeightOffsetCm);
	Fog.SetVolumetricFogAlbedo(Atmosphere.VolumetricFogAlbedo.ToFColor(true));
	Fog.SetVolumetricFogEmissive(Atmosphere.VolumetricFogEmissive);
	Fog.SetVolumetricFogExtinctionScale(Atmosphere.VolumetricFogExtinctionScale);

	if (Fog.Mobility == EComponentMobility::Static)
	{
		if (!bHasWarnedStaticFog)
		{
			bHasWarnedStaticFog = true;
			UE_LOG(LogTDGame, Warning, TEXT("Realm: height fog '%s' is static; fog height is not changed."), *Fog.GetPathName());
		}
		return;
	}

	FVector FogLocation = Fog.GetComponentLocation();
	FogLocation.Z = Atmosphere.FogHeightCm;
	Fog.SetWorldLocation(FogLocation);
}

bool UTDRealmSubsystem::RefreshRealmPostProcessVolumes()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const bool bHasMissingVolume = LoadedRealmDefinitions.ContainsByPredicate([this](const UTDRealmDefinition* Realm)
	{
		const TWeakObjectPtr<APostProcessVolume>* Volume = Realm ? RealmPostProcessVolumes.Find(Realm->RealmId) : nullptr;
		return Realm && (!Volume || !Volume->IsValid());
	});
	const double NowSeconds = World->GetTimeSeconds();
	if (!bHasMissingVolume || NowSeconds < NextPostProcessScanSeconds)
	{
		return false;
	}

	NextPostProcessScanSeconds = NowSeconds + TDPostProcessScanIntervalSeconds;
	bool bHasFoundVolume = false;
	for (const UTDRealmDefinition* Realm : LoadedRealmDefinitions)
	{
		if (!Realm)
		{
			continue;
		}

		TWeakObjectPtr<APostProcessVolume>& CachedVolume = RealmPostProcessVolumes.FindOrAdd(Realm->RealmId);
		if (CachedVolume.IsValid())
		{
			continue;
		}

		const FName RealmTag = MakeRealmPostProcessTag(Realm->RealmId);
		for (TActorIterator<APostProcessVolume> VolumeIterator(World); VolumeIterator; ++VolumeIterator)
		{
			if (!VolumeIterator->ActorHasTag(RealmTag))
			{
				continue;
			}

			CachedVolume = *VolumeIterator;
			bHasFoundVolume = true;
			UE_LOG(LogTDGame, Log, TEXT("Realm: post process volume '%s' bound to realm '%s'."), *VolumeIterator->GetName(), *Realm->RealmId.ToString());
			break;
		}
	}
	return bHasFoundVolume;
}

void UTDRealmSubsystem::ApplyPostProcessWeights()
{
	const float AppliedWeight = GetAtmosphereStepValue(AppliedWeightStep);
	for (const UTDRealmDefinition* Realm : LoadedRealmDefinitions)
	{
		const TWeakObjectPtr<APostProcessVolume>* CachedVolume = Realm ? RealmPostProcessVolumes.Find(Realm->RealmId) : nullptr;
		APostProcessVolume* Volume = CachedVolume ? CachedVolume->Get() : nullptr;
		if (!Volume)
		{
			continue;
		}

		Volume->bUnbound = true;
		Volume->BlendWeight = Realm->RealmId == AppliedRealmId ? AppliedWeight : 0.0f;
	}
}

void UTDRealmSubsystem::RecoverFallenPawnIfNeeded(APawn& Pawn)
{
	if (RecoverPawnFromLiftPitIfNeeded(Pawn))
	{
		return;
	}

	const FVector PawnLocation = Pawn.GetActorLocation();
	const UTDRealmDefinition* Realm = FindLoadedRealmContainingLocation(PawnLocation);
	if (!Realm || PawnLocation.Z >= Realm->FallRecoveryHeightCm)
	{
		return;
	}

	if (IsTravelBlockingRecovery())
	{
		return;
	}

	RecoverFallenPawn(Pawn, *Realm);
}

bool UTDRealmSubsystem::RecoverPawnFromLiftPitIfNeeded(APawn& Pawn) const
{
	const FVector PawnLocation = Pawn.GetActorLocation();
	for (const UTDRealmDefinition* Realm : LoadedRealmDefinitions)
	{
		if (!Realm)
		{
			continue;
		}

		for (const FTDRealmGate& Gate : Realm->Gates)
		{
			if (Gate.GateKind != ETDRealmGateKind::Lift || !IsInsideRealmLiftPit(Gate, PawnLocation))
			{
				continue;
			}

			const ATDRealmLiftDeck* Lift = FindRealmLiftDeck(Gate.GateId);
			const bool bIsOnDeck = Lift && (Lift->IsCarryingPawn(Pawn) || Lift->IsPawnBasedOnDeck(Pawn));
			if (bIsOnDeck || IsTravelBlockingRecovery())
			{
				return false;
			}

			const FTransform ExitTransform = Gate.GetTravelDestination(false);
			const FVector RecoveryLocation = FindLiftExitRecoveryLocation(Pawn, ExitTransform.GetLocation());
			if (!TeleportFallenPawn(Pawn, RecoveryLocation, ExitTransform.Rotator(), Realm->TransitionFadeColor))
			{
				UE_LOG(LogTDGame, Warning, TEXT("Realm: failed to recover pawn '%s' from the shaft of lift gate '%s' to %s."), *Pawn.GetName(), *Gate.GateId.ToString(), *RecoveryLocation.ToCompactString());
				return true;
			}

			UE_LOG(LogTDGame, Warning, TEXT("Realm: pawn '%s' fell to %s in the shaft of lift gate '%s'; recovered to the field landing exit %s."), *Pawn.GetName(), *PawnLocation.ToCompactString(), *Gate.GateId.ToString(), *RecoveryLocation.ToCompactString());
			return true;
		}
	}
	return false;
}

void UTDRealmSubsystem::RecoverFallenPawn(APawn& Pawn, const UTDRealmDefinition& Realm)
{
	const FVector FallenLocation = Pawn.GetActorLocation();
	const FTDRealmGate* NearestGate = Realm.FindNearestGate(FallenLocation);
	if (!NearestGate)
	{
		UE_LOG(LogTDGame, Warning, TEXT("Realm: pawn '%s' fell below %.0f in '%s' but the realm has no gates."), *Pawn.GetName(), Realm.FallRecoveryHeightCm, *Realm.RealmId.ToString());
		return;
	}

	const bool bIsLiftGate = NearestGate->GateKind == ETDRealmGateKind::Lift;
	const FTransform LiftExitTransform = NearestGate->GetTravelDestination(true);
	const FVector RecoveryLocation = bIsLiftGate ? FindLiftExitRecoveryLocation(Pawn, LiftExitTransform.GetLocation()) : FindFallRecoveryLocation(Pawn, *NearestGate);
	const FRotator RecoveryRotation = bIsLiftGate ? LiftExitTransform.Rotator() : NearestGate->ArrivalTransform.Rotator();
	if (!TeleportFallenPawn(Pawn, RecoveryLocation, RecoveryRotation, Realm.TransitionFadeColor))
	{
		UE_LOG(LogTDGame, Warning, TEXT("Realm: failed to recover pawn '%s' to gate '%s' passage end %s."), *Pawn.GetName(), *NearestGate->GateId.ToString(), *RecoveryLocation.ToCompactString());
		return;
	}

	UE_LOG(LogTDGame, Warning, TEXT("Realm: pawn '%s' fell to %s below %.0f in '%s'; recovered to gate '%s' passage end %s."), *Pawn.GetName(), *FallenLocation.ToCompactString(), Realm.FallRecoveryHeightCm, *Realm.RealmId.ToString(), *NearestGate->GateId.ToString(), *RecoveryLocation.ToCompactString());
	ActivateGameplayDataLayer(Realm, TEXT("fall recovery"));
}

bool UTDRealmSubsystem::TeleportFallenPawn(APawn& Pawn, const FVector& RecoveryLocation, const FRotator& RecoveryRotation, const FLinearColor& FadeColor) const
{
	if (!UTDSeamlessTravelSubsystem::TeleportPawnToDestination(Pawn, RecoveryLocation, RecoveryRotation))
	{
		return false;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(Pawn.GetController());
	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
		const float FadeInSeconds = Settings ? FMath::Max(Settings->TravelFadeInSeconds, 0.0f) : 0.0f;
		PlayerController->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, FadeInSeconds, FadeColor, false, false);
	}
	return true;
}

bool UTDRealmSubsystem::IsTravelBlockingRecovery() const
{
	const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(this);
	return Travel && Travel->GetTravelState() != ETDSeamlessTravelState::Idle && !Travel->IsUnrequestedPreload();
}

FVector UTDRealmSubsystem::FindFallRecoveryLocation(const APawn& Pawn, const FTDRealmGate& Gate) const
{
	const FVector ArrivalLocation = Gate.ArrivalTransform.GetLocation();
	const FVector PassageEnd = ArrivalLocation + Gate.GetPassageDirection() * Gate.DepartureLengthCm;
	UWorld* World = GetWorld();
	if (!World)
	{
		return ArrivalLocation;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDRealmFallRecovery), false, &Pawn);
	FHitResult Hit;
	const FVector TraceStart(PassageEnd.X, PassageEnd.Y, ArrivalLocation.Z + TDFallRecoveryTraceUpCm);
	const FVector TraceEnd(PassageEnd.X, PassageEnd.Y, ArrivalLocation.Z - TDFallRecoveryTraceDownCm);
	if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		return ArrivalLocation;
	}

	return FVector(PassageEnd.X, PassageEnd.Y, Hit.Location.Z + Pawn.GetSimpleCollisionHalfHeight() + TDFallRecoveryClearanceCm);
}

FVector UTDRealmSubsystem::FindLiftExitRecoveryLocation(const APawn& Pawn, const FVector& ExitLocationCm) const
{
	const double StandingOffsetCm = Pawn.GetSimpleCollisionHalfHeight() + TDFallRecoveryClearanceCm;
	const FVector StandingLocation = ExitLocationCm + FVector(0.0, 0.0, StandingOffsetCm);
	UWorld* World = GetWorld();
	if (!World)
	{
		return StandingLocation;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDRealmLiftFallRecovery), false, &Pawn);
	FHitResult Hit;
	const FVector TraceStart(ExitLocationCm.X, ExitLocationCm.Y, ExitLocationCm.Z + TDRealmLiftExitTraceUpCm);
	const FVector TraceEnd(ExitLocationCm.X, ExitLocationCm.Y, ExitLocationCm.Z - TDFallRecoveryTraceDownCm);
	if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		return StandingLocation;
	}

	return FVector(ExitLocationCm.X, ExitLocationCm.Y, Hit.Location.Z + StandingOffsetCm);
}

const UTDRealmDefinition* UTDRealmSubsystem::FindLoadedRealmContainingLocation(const FVector& LocationCm) const
{
	for (const UTDRealmDefinition* Realm : LoadedRealmDefinitions)
	{
		if (Realm && Realm->ContainsLocationXY(LocationCm))
		{
			return Realm;
		}
	}
	return nullptr;
}

APawn* UTDRealmSubsystem::FindLocalPlayerPawn() const
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return PlayerController ? PlayerController->GetPawn() : nullptr;
}
