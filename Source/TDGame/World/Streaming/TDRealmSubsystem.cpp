#include "World/Streaming/TDRealmSubsystem.h"

#include "Camera/PlayerCameraManager.h"
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
#include "Framework/TDGamePlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "TDGame.h"
#include "TDWorldGenSettings.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"

namespace
{
	constexpr double TDSeamCrossingMarginCm = 50.0;
	constexpr double TDPassageVerticalToleranceCm = 6000.0;
	constexpr float TDAtmosphereQuantizationSteps = 64.0f;
	constexpr double TDPostProcessScanIntervalSeconds = 1.0;
	constexpr double TDFallRecoveryTraceUpCm = 3000.0;
	constexpr double TDFallRecoveryTraceDownCm = 6000.0;
	constexpr double TDFallRecoveryClearanceCm = 10.0;

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

	FVector GetSeamLocation(const FTDRealmGate& Gate, bool bIsRealmSide)
	{
		return bIsRealmSide ? Gate.ArrivalTransform.GetLocation() : Gate.FieldReturnTransform.GetLocation();
	}

	bool TryMeasurePassage(const FTDRealmGate& Gate, bool bIsRealmSide, const FVector& LocationCm, double& OutAlongCm, double& OutDistanceSquared)
	{
		const FVector Direction = Gate.GetPassageDirection();
		if (Direction.IsNearlyZero())
		{
			return false;
		}

		const FVector Seam = GetSeamLocation(Gate, bIsRealmSide);
		if (FMath::Abs(LocationCm.Z - Seam.Z) > TDPassageVerticalToleranceCm)
		{
			return false;
		}

		const FVector Delta(LocationCm.X - Seam.X, LocationCm.Y - Seam.Y, 0.0);
		const FVector Lateral(-Direction.Y, Direction.X, 0.0);
		const double AlongCm = FVector::DotProduct(Delta, Direction);
		const double AcrossCm = FVector::DotProduct(Delta, Lateral);
		if (FMath::Abs(AcrossCm) > Gate.PassageHalfWidthCm)
		{
			return false;
		}

		const double MinAlongCm = bIsRealmSide ? -Gate.SeamBufferCm : -(Gate.ApproachLengthCm + Gate.PreloadLeadCm);
		const double MaxAlongCm = bIsRealmSide ? Gate.DepartureLengthCm + Gate.PreloadLeadCm : Gate.SeamBufferCm;
		if (AlongCm < MinAlongCm || AlongCm > MaxAlongCm)
		{
			return false;
		}

		OutAlongCm = AlongCm;
		OutDistanceSquared = Delta.SizeSquared();
		return true;
	}

	float GetPassageWeight(const FTDRealmGate& Gate, bool bIsRealmSide, double AlongCm)
	{
		const float Along = static_cast<float>(AlongCm);
		if (bIsRealmSide)
		{
			return 0.5f + 0.5f * FMath::SmoothStep(0.0f, Gate.DepartureLengthCm, Along);
		}
		return 0.5f * FMath::SmoothStep(-Gate.ApproachLengthCm, 0.0f, Along);
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
	LoadedRealmDefinitions.Reset();
	RealmPostProcessVolumes.Reset();
	Super::Deinitialize();
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

	FTDPassageLocation Passage = FindPassageLocation(Pawn->GetActorLocation());
	UpdatePassagePreload(Passage);
	if (TryCrossSeam(*Pawn, Passage))
	{
		Passage = FindPassageLocation(Pawn->GetActorLocation());
	}

	UpdateAtmosphere(Pawn->GetActorLocation(), Passage);
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
			for (const bool bIsRealmSide : { false, true })
			{
				double AlongCm = 0.0;
				double DistanceSquared = 0.0;
				if (!TryMeasurePassage(Gate, bIsRealmSide, LocationCm, AlongCm, DistanceSquared) || DistanceSquared >= NearestDistanceSquared)
				{
					continue;
				}

				NearestDistanceSquared = DistanceSquared;
				Nearest.Realm = Realm;
				Nearest.Gate = &Gate;
				Nearest.bIsRealmSide = bIsRealmSide;
				Nearest.AlongCm = AlongCm;
			}
		}
	}
	return Nearest;
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
	const bool bToEntry = !Passage.bIsRealmSide;
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
			OwnedPreloadGateId = GateId;
			bIsOwnedPreloadToEntry = bToEntry;
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

	OwnedPreloadGateId = GateId;
	bIsOwnedPreloadToEntry = bToEntry;
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

bool UTDRealmSubsystem::TryCrossSeam(APawn& Pawn, const FTDPassageLocation& Passage)
{
	if (!Passage.Gate)
	{
		bHasWarnedSeamNotReady = false;
		return false;
	}

	const bool bIsPastSeam = Passage.bIsRealmSide ? Passage.AlongCm <= -TDSeamCrossingMarginCm : Passage.AlongCm >= TDSeamCrossingMarginCm;
	if (!bIsPastSeam)
	{
		bHasWarnedSeamNotReady = false;
		return false;
	}

	const FTDRealmGate& Gate = *Passage.Gate;
	const bool bToEntry = !Passage.bIsRealmSide;
	UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(this);
	if (!Travel || !Travel->IsReadyFor(Gate.GateId, bToEntry))
	{
		if (!bHasWarnedSeamNotReady)
		{
			bHasWarnedSeamNotReady = true;
			UE_LOG(LogTDGame, Warning, TEXT("Realm: pawn passed the seam of gate '%s' (%s) before the other side was ready; waiting."), *Gate.GateId.ToString(), bToEntry ? TEXT("entry") : TEXT("field return"));
		}
		return false;
	}

	const FVector SeamOffsetCm = Gate.GetSeamOffsetCm();
	const FVector OffsetCm = bToEntry ? SeamOffsetCm : -SeamOffsetCm;
	if (!ShiftPawnAcrossSeam(Pawn, OffsetCm))
	{
		return false;
	}

	Travel->CommitSeamCrossing(Gate.GateId, bToEntry, GetSeamLocation(Gate, Passage.bIsRealmSide));
	OwnedPreloadGateId = NAME_None;
	bHasWarnedSeamNotReady = false;
	UE_LOG(LogTDGame, Log, TEXT("Realm: pawn '%s' crossed the seam of gate '%s' to %s by %s."), *Pawn.GetName(), *Gate.GateId.ToString(), bToEntry ? TEXT("the realm") : TEXT("the field"), *OffsetCm.ToCompactString());
	return true;
}

bool UTDRealmSubsystem::ShiftPawnAcrossSeam(APawn& Pawn, const FVector& OffsetCm) const
{
	const FVector TargetLocation = Pawn.GetActorLocation() + OffsetCm;
	if (!UTDSeamlessTravelSubsystem::TeleportPawnToDestination(Pawn, TargetLocation, Pawn.GetActorRotation()))
	{
		UE_LOG(LogTDGame, Warning, TEXT("Realm: failed to shift pawn '%s' across the seam to %s."), *Pawn.GetName(), *TargetLocation.ToCompactString());
		return false;
	}

	if (USpringArmComponent* SpringArm = Pawn.FindComponentByClass<USpringArmComponent>())
	{
		SpringArm->PreviousDesiredLoc += OffsetCm;
		SpringArm->PreviousArmOrigin += OffsetCm;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Pawn.GetController());
	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->SetGameCameraCutThisFrame();
	}

	if (ATDGamePlayerController* GamePlayerController = Cast<ATDGamePlayerController>(PlayerController))
	{
		GamePlayerController->ShiftMovementTargetsBy(OffsetCm);
	}
	return true;
}

void UTDRealmSubsystem::ApplyAtmosphereForLocation(const FVector& LocationCm)
{
	CaptureFieldAtmosphere();
	UpdateAtmosphere(LocationCm, FindPassageLocation(LocationCm));
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
	const bool bIsRealmSide = Passage.Gate ? Passage.bIsRealmSide : Realm != nullptr;
	const float Weight = Passage.Gate ? GetPassageWeight(*Passage.Gate, Passage.bIsRealmSide, Passage.AlongCm) : (Realm ? 1.0f : 0.0f);
	const float MistRadiusCm = Realm ? Realm->SeamMistRadiusCm : 0.0f;
	const float Mist = Passage.Gate ? 1.0f - FMath::SmoothStep(0.0f, MistRadiusCm, static_cast<float>(FMath::Abs(Passage.AlongCm))) : 0.0f;

	const FName RealmId = Realm ? Realm->RealmId : NAME_None;
	const FName GateId = Passage.Gate ? Passage.Gate->GateId : NAME_None;
	const int32 WeightStep = QuantizeAtmosphereStep(Weight);
	const int32 MistStep = QuantizeAtmosphereStep(Mist);

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

	const bool bHasAtmosphereChanged = RealmId != AppliedRealmId || GateId != AppliedGateId || bIsRealmSide != bIsAppliedRealmSide
		|| WeightStep != AppliedWeightStep || MistStep != AppliedMistStep;
	if (!bHasAtmosphereChanged)
	{
		return;
	}

	AppliedRealmId = RealmId;
	AppliedGateId = GateId;
	bIsAppliedRealmSide = bIsRealmSide;
	AppliedWeightStep = WeightStep;
	AppliedMistStep = MistStep;
	CaptureFieldAtmosphere();
	ApplyAtmosphere(Realm ? MakeBlendedAtmosphere(*Realm, Passage, GetAtmosphereStepValue(WeightStep), GetAtmosphereStepValue(MistStep)) : FieldAtmosphere);
	ApplyPostProcessWeights();
}

FTDRealmAtmosphere UTDRealmSubsystem::MakeBlendedAtmosphere(const UTDRealmDefinition& Realm, const FTDPassageLocation& Passage, float Weight, float Mist) const
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
	Blended.FogInscatteringColor = FMath::Lerp(Field.FogInscatteringColor, Target.FogInscatteringColor, Weight);
	Blended.FogMaxOpacity = FMath::Lerp(Field.FogMaxOpacity, Target.FogMaxOpacity, Weight);
	Blended.FogStartDistanceCm = FMath::Lerp(Field.FogStartDistanceCm, Target.FogStartDistanceCm, Weight);
	Blended.SecondFogDensity = FMath::Lerp(Field.SecondFogDensity, Target.SecondFogDensity, Weight);
	Blended.SecondFogHeightFalloff = FMath::Lerp(Field.SecondFogHeightFalloff, Target.SecondFogHeightFalloff, Weight);
	Blended.SecondFogHeightOffsetCm = FMath::Lerp(Field.SecondFogHeightOffsetCm, Target.SecondFogHeightOffsetCm, Weight);
	Blended.VolumetricFogAlbedo = FMath::Lerp(Field.VolumetricFogAlbedo, Target.VolumetricFogAlbedo, Weight);
	Blended.VolumetricFogEmissive = FMath::Lerp(Field.VolumetricFogEmissive, Target.VolumetricFogEmissive, Weight);
	Blended.VolumetricFogExtinctionScale = FMath::Lerp(Field.VolumetricFogExtinctionScale, Target.VolumetricFogExtinctionScale, Weight);

	const float SeamHeightOffsetCm = Passage.Gate ? static_cast<float>(Passage.Gate->GetSeamOffsetCm().Z) : 0.0f;
	const bool bIsRealmSide = Passage.Gate ? Passage.bIsRealmSide : true;
	const float FieldFogHeightCm = bIsRealmSide ? Field.FogHeightCm + SeamHeightOffsetCm : Field.FogHeightCm;
	const float RealmFogHeightCm = bIsRealmSide ? Target.FogHeightCm : Target.FogHeightCm - SeamHeightOffsetCm;
	Blended.FogHeightCm = FMath::Lerp(FieldFogHeightCm, RealmFogHeightCm, Weight);

	Blended.FogDensity += Realm.SeamMistDensity * Mist;
	Blended.FogMaxOpacity = FMath::Lerp(Blended.FogMaxOpacity, 1.0f, Mist);
	Blended.FogStartDistanceCm = FMath::Lerp(Blended.FogStartDistanceCm, 0.0f, Mist);
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

void UTDRealmSubsystem::RecoverFallenPawnIfNeeded(APawn& Pawn) const
{
	const FVector PawnLocation = Pawn.GetActorLocation();
	const UTDRealmDefinition* Realm = FindLoadedRealmContainingLocation(PawnLocation);
	if (!Realm || PawnLocation.Z >= Realm->FallRecoveryHeightCm)
	{
		return;
	}

	const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(this);
	if (Travel && Travel->GetTravelState() != ETDSeamlessTravelState::Idle && !Travel->IsUnrequestedPreload())
	{
		return;
	}

	RecoverFallenPawn(Pawn, *Realm);
}

void UTDRealmSubsystem::RecoverFallenPawn(APawn& Pawn, const UTDRealmDefinition& Realm) const
{
	const FVector FallenLocation = Pawn.GetActorLocation();
	const FTDRealmGate* NearestGate = Realm.FindNearestGate(FallenLocation);
	if (!NearestGate)
	{
		UE_LOG(LogTDGame, Warning, TEXT("Realm: pawn '%s' fell below %.0f in '%s' but the realm has no gates."), *Pawn.GetName(), Realm.FallRecoveryHeightCm, *Realm.RealmId.ToString());
		return;
	}

	const FVector RecoveryLocation = FindFallRecoveryLocation(Pawn, *NearestGate);
	const FRotator RecoveryRotation = NearestGate->ArrivalTransform.Rotator();
	if (!UTDSeamlessTravelSubsystem::TeleportPawnToDestination(Pawn, RecoveryLocation, RecoveryRotation))
	{
		UE_LOG(LogTDGame, Warning, TEXT("Realm: failed to recover pawn '%s' to gate '%s' passage end %s."), *Pawn.GetName(), *NearestGate->GateId.ToString(), *RecoveryLocation.ToCompactString());
		return;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(Pawn.GetController());
	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
		const float FadeInSeconds = Settings ? FMath::Max(Settings->TravelFadeInSeconds, 0.0f) : 0.0f;
		PlayerController->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, FadeInSeconds, Realm.TransitionFadeColor, false, false);
	}
	UE_LOG(LogTDGame, Warning, TEXT("Realm: pawn '%s' fell to %s below %.0f in '%s'; recovered to gate '%s' passage end %s."), *Pawn.GetName(), *FallenLocation.ToCompactString(), Realm.FallRecoveryHeightCm, *Realm.RealmId.ToString(), *NearestGate->GateId.ToString(), *RecoveryLocation.ToCompactString());
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
