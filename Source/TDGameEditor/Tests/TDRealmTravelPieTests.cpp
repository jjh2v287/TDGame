#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "TDWorldGenSettings.h"
#include "Tests/TDPieWaitUntilCommand.h"
#include "Tests/TDTravelStateRecorder.h"
#include "UObject/StrongObjectPtr.h"
#include "World/Streaming/TDRealmSubsystem.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"
#include "World/TDRealmDefinitions.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace
{
	const FName TDPreloadCancelFirstTargetId(TEXT("MainCrypt"));
	const FName TDPreloadCancelSecondTargetId(TEXT("HollowCave"));
	constexpr float TDRealmPieWarmupSeconds = 10.0f;
	constexpr float TDPreloadCancelStreamingSeconds = 1.0f;
	constexpr double TDRealmTravelTimeoutSeconds = 90.0;
	constexpr double TDRealmPieFieldHalfExtentCm = 60000.0;
	constexpr float TDRealmSunIntensityToleranceLux = 0.01f;
	constexpr double TDSeamFloorOffsetCm = 120.0;
	constexpr double TDSeamWalkTimeoutSeconds = 90.0;
	constexpr double TDSeamWalkMinimumStreamingHoldSeconds = 3.0;
	constexpr double TDSeamWalkSpeedCmPerSecond = 600.0;
	constexpr double TDSeamWalkMaxStepSeconds = 0.1;
	constexpr double TDSeamWalkStartInsetCm = 100.0;
	constexpr double TDSeamWalkPastSeamHoldCm = 100.0;
	constexpr double TDSeamWalkMarchStepCm = 100.0;
	constexpr double TDSeamWalkTraceAboveCm = 300.0;
	constexpr double TDSeamWalkTraceBelowCm = 2000.0;
	constexpr double TDSeamWalkGroundClearanceCm = 2.0;
	constexpr double TDSeamWalkJumpDistanceCm = 5000.0;
	constexpr double TDSeamWalkMaxAlongChangeCm = 100.0;
	constexpr double TDSeamWalkProgressReportSeconds = 5.0;
	constexpr float TDSeamWalkSeamWeight = 0.5f;
	constexpr float TDSeamWalkSeamWeightTolerance = 0.1f;
	constexpr float TDSeamWalkRealmEndMinWeight = 0.95f;
	constexpr float TDSeamWalkFieldMaxWeight = 0.05f;
	constexpr float TDSeamWalkMaxFadeAmount = 0.001f;

	struct FTDRealmGateTravelCase
	{
		FName RealmId;
		FName GateId;
		FBox RealmBoundsCm = FBox(ForceInit);
		float RealmSunIntensityLux = 0.0f;
		FVector FieldSeamCm = FVector::ZeroVector;
		FVector RealmSeamCm = FVector::ZeroVector;
		FVector PassageDirection = FVector::ForwardVector;
		double ApproachLengthCm = 0.0;
		double DepartureLengthCm = 0.0;
	};

	struct FTDRealmSeamWalkState
	{
		bool bIsAborted = false;
		TArray<FBox> AllRealmBoundsCm;
		bool bIsWalkingIntoRealm = true;
		bool bHasCrossed = false;
		bool bSawPreloadBeforeSeam = false;
		bool bHasReportedSeamWait = false;
		double SeamHoldStartWorldSeconds = 0.0;
		FVector LastSetLocationCm = FVector::ZeroVector;
		double LastAlongCm = 0.0;
		double LastStepWorldSeconds = 0.0;
		double LastStepCorrectionCm = 0.0;
		double LastProgressReportPlatformSeconds = 0.0;
	};

	struct FTDRealmTravelPieTestState
	{
		bool bIsAborted = false;
		TWeakObjectPtr<UTDSeamlessTravelSubsystem> Travel;
		TStrongObjectPtr<UTDTravelStateRecorder> Recorder;
		TArray<FBox> AllRealmBoundsCm;
		int32 ExpectedSettledTravelCount = 0;
	};

	bool IsTravelPreloading(const UTDSeamlessTravelSubsystem& Travel)
	{
		const ETDSeamlessTravelState TravelState = Travel.GetTravelState();
		return TravelState == ETDSeamlessTravelState::Preloading || TravelState == ETDSeamlessTravelState::ReadyToTravel;
	}

	int32 CountSettledTravels(const UTDTravelStateRecorder& Recorder)
	{
		int32 Count = 0;
		for (const FTDTravelStateTransition& Transition : Recorder.GetTransitions())
		{
			if (Transition.OldState == ETDSeamlessTravelState::Settling && Transition.NewState == ETDSeamlessTravelState::Idle)
			{
				++Count;
			}
		}
		return Count;
	}

	bool TryGetFirstSunIntensityLux(UWorld* World, float& OutIntensityLux)
	{
		OutIntensityLux = 0.0f;
		if (!World)
		{
			return false;
		}
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (const UDirectionalLightComponent* Sun = It->FindComponentByClass<UDirectionalLightComponent>())
			{
				OutIntensityLux = Sun->Intensity;
				return true;
			}
		}
		return false;
	}

	bool IsInsideAnyRealmXY(const TArray<FBox>& AllRealmBoundsCm, const FVector& LocationCm)
	{
		return AllRealmBoundsCm.ContainsByPredicate([&LocationCm](const FBox& RealmBoundsCm) { return RealmBoundsCm.IsInsideOrOnXY(LocationCm); });
	}

	TArray<FTDRealmGateTravelCase> CollectRealmGateTravelCases(TArray<FBox>& OutAllRealmBoundsCm, FString& OutConfiguredPaths)
	{
		TArray<FTDRealmGateTravelCase> Cases;
		const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
		if (!Settings)
		{
			return Cases;
		}

		for (const TSoftObjectPtr<UTDRealmDefinition>& RealmReference : Settings->RealmDefinitions)
		{
			OutConfiguredPaths += FString::Printf(TEXT(" %s"), *RealmReference.ToSoftObjectPath().ToString());
			UObject* LoadedObject = RealmReference.IsNull() ? nullptr : RealmReference.ToSoftObjectPath().TryLoad(nullptr, static_cast<ELoadFlags>(LOAD_NoWarn | LOAD_Quiet));
			const UTDRealmDefinition* Realm = Cast<UTDRealmDefinition>(LoadedObject);
			if (!Realm)
			{
				continue;
			}

			OutAllRealmBoundsCm.Add(Realm->GetBoundsCm());
			for (const FTDRealmGate& Gate : Realm->Gates)
			{
				FTDRealmGateTravelCase& Case = Cases.AddDefaulted_GetRef();
				Case.RealmId = Realm->RealmId;
				Case.GateId = Gate.GateId;
				Case.RealmBoundsCm = Realm->GetBoundsCm();
				Case.RealmSunIntensityLux = Realm->Atmosphere.SunIntensityLux;
				Case.FieldSeamCm = Gate.FieldReturnTransform.GetLocation();
				Case.RealmSeamCm = Gate.ArrivalTransform.GetLocation();
				Case.PassageDirection = Gate.GetPassageDirection();
				Case.ApproachLengthCm = Gate.ApproachLengthCm;
				Case.DepartureLengthCm = Gate.DepartureLengthCm;
			}
		}
		return Cases;
	}

	bool RequestRealmGateTravel(FAutomationTestBase& Test, FTDRealmTravelPieTestState& State, FName GateId, bool bToEntry)
	{
		UTDSeamlessTravelSubsystem* Travel = State.Travel.Get();
		if (!Travel)
		{
			Test.AddError(TEXT("Seamless travel subsystem is gone before a realm gate request."));
			return false;
		}

		if (IsTravelPreloading(*Travel))
		{
			Test.AddInfo(FString::Printf(TEXT("Cancelling preload of '%s' started by a nearby gate before requesting '%s'."), *Travel->GetActiveDungeonId().ToString(), *GateId.ToString()));
			Travel->CancelTravel();
		}

		const FString Description = FString::Printf(TEXT("RequestTravel('%s', %s) is accepted"), *GateId.ToString(), bToEntry ? TEXT("entry") : TEXT("field return"));
		if (!Test.TestTrue(Description, Travel->RequestTravel(GateId, bToEntry)))
		{
			return false;
		}
		++State.ExpectedSettledTravelCount;
		return true;
	}

	void TestSunIntensityBetween(FAutomationTestBase& Test, UWorld* World, const FString& Description, float FirstIntensityLux, float SecondIntensityLux)
	{
		float SunIntensityLux = 0.0f;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: PIE world has a directional light"), *Description), TryGetFirstSunIntensityLux(World, SunIntensityLux)))
		{
			return;
		}
		const float MinIntensityLux = FMath::Min(FirstIntensityLux, SecondIntensityLux) - TDRealmSunIntensityToleranceLux;
		const float MaxIntensityLux = FMath::Max(FirstIntensityLux, SecondIntensityLux) + TDRealmSunIntensityToleranceLux;
		Test.TestTrue(FString::Printf(TEXT("%s (%.3f in [%.3f, %.3f])"), *Description, SunIntensityLux, MinIntensityLux, MaxIntensityLux), SunIntensityLux >= MinIntensityLux && SunIntensityLux <= MaxIntensityLux);
	}

	double MeasureAlongCm(const FVector& LocationCm, const FVector& SeamCm, const FVector& PassageDirection)
	{
		return FVector::DotProduct(FVector(LocationCm.X - SeamCm.X, LocationCm.Y - SeamCm.Y, 0.0), PassageDirection);
	}

	double MeasureAcrossCm(const FVector& LocationCm, const FVector& SeamCm, const FVector& PassageDirection)
	{
		return FVector::DotProduct(FVector(LocationCm.X - SeamCm.X, LocationCm.Y - SeamCm.Y, 0.0), FVector(-PassageDirection.Y, PassageDirection.X, 0.0));
	}

	FString MakeSeamWalkPrefix(const FTDRealmSeamWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		return FString::Printf(TEXT("Gate '%s' %s"), *Case.GateId.ToString(), State.bIsWalkingIntoRealm ? TEXT("walk into realm") : TEXT("walk back to field"));
	}

	FString DescribeSeamWalk(const FTDRealmSeamWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(World);
		if (!Pawn || !Travel)
		{
			return TEXT("PIE world, player pawn or travel subsystem is gone");
		}

		const FVector PawnLocation = Pawn->GetActorLocation();
		const bool bIsInRealm = Case.RealmBoundsCm.IsInsideOrOnXY(PawnLocation);
		const FVector& SeamCm = bIsInRealm ? Case.RealmSeamCm : Case.FieldSeamCm;
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(World);
		return FString::Printf(TEXT("pawn %s on the %s side (s %.1f cm, l %.1f cm, Z %.1f cm from the seam), last step corrected by %.1f cm, moved %.1f cm since the last step, crossed %d, travel state %d, active '%s', ready %d, realm weight %.3f"),
			*PawnLocation.ToCompactString(), bIsInRealm ? TEXT("realm") : TEXT("field"),
			MeasureAlongCm(PawnLocation, SeamCm, Case.PassageDirection), MeasureAcrossCm(PawnLocation, SeamCm, Case.PassageDirection), PawnLocation.Z - SeamCm.Z,
			State.LastStepCorrectionCm, FVector::Dist(PawnLocation, State.LastSetLocationCm), State.bHasCrossed ? 1 : 0,
			static_cast<int32>(Travel->GetTravelState()), *Travel->GetActiveDungeonId().ToString(), Travel->IsReadyFor(Case.GateId, State.bIsWalkingIntoRealm) ? 1 : 0,
			Realm ? Realm->GetRealmWeight(Case.RealmId) : -1.0f);
	}

	void ReportSeamWalkProgressPeriodically(FAutomationTestBase& Test, FTDRealmSeamWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		const double NowPlatformSeconds = FPlatformTime::Seconds();
		if (NowPlatformSeconds - State.LastProgressReportPlatformSeconds < TDSeamWalkProgressReportSeconds)
		{
			return;
		}
		State.LastProgressReportPlatformSeconds = NowPlatformSeconds;
		Test.AddInfo(FString::Printf(TEXT("%s progress: %s."), *MakeSeamWalkPrefix(State, Case), *DescribeSeamWalk(State, Case)));
	}

	bool TryTraceGroundZ(UWorld& World, const APawn& Pawn, const FVector& ReferenceGroundCm, double& OutGroundZ)
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDRealmSeamWalkGround), false, &Pawn);
		FHitResult Hit;
		const FVector TraceStart(ReferenceGroundCm.X, ReferenceGroundCm.Y, ReferenceGroundCm.Z + TDSeamWalkTraceAboveCm);
		const FVector TraceEnd(ReferenceGroundCm.X, ReferenceGroundCm.Y, ReferenceGroundCm.Z - TDSeamWalkTraceBelowCm);
		if (!World.LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
		{
			return false;
		}
		OutGroundZ = Hit.Location.Z;
		return true;
	}

	FVector GetFieldSeamFloorCm(const FTDRealmGateTravelCase& Case)
	{
		return Case.FieldSeamCm - FVector(0.0, 0.0, TDSeamFloorOffsetCm);
	}

	bool HoldPawnAtFieldSeamUntilLoaded(const FTDRealmSeamWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		if (!Pawn)
		{
			return false;
		}

		const UWorldPartitionSubsystem* WorldPartition = World->GetSubsystem<UWorldPartitionSubsystem>();
		double GroundZ = 0.0;
		const bool bHasHeldLongEnough = World->GetTimeSeconds() - State.SeamHoldStartWorldSeconds >= TDSeamWalkMinimumStreamingHoldSeconds;
		const bool bIsStreamingCompleted = !WorldPartition || WorldPartition->IsStreamingCompleted();
		if (bHasHeldLongEnough && bIsStreamingCompleted && TryTraceGroundZ(*World, *Pawn, GetFieldSeamFloorCm(Case), GroundZ))
		{
			return true;
		}

		UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, Case.FieldSeamCm, Case.PassageDirection.Rotation());
		return false;
	}

	bool PlacePawnAtFieldSeam(FAutomationTestBase& Test, FTDRealmSeamWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		if (!Test.TestNotNull(FString::Printf(TEXT("Gate '%s': player pawn exists before the seam walk"), *Case.GateId.ToString()), Pawn))
		{
			return false;
		}
		State.SeamHoldStartWorldSeconds = World->GetTimeSeconds();
		return Test.TestTrue(FString::Printf(TEXT("Gate '%s': pawn moved to the field seam %s"), *Case.GateId.ToString(), *Case.FieldSeamCm.ToCompactString()),
			UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, Case.FieldSeamCm, Case.PassageDirection.Rotation()));
	}

	bool PlacePawnAtPassageStart(FAutomationTestBase& Test, FTDRealmSeamWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		if (!World || !Test.TestNotNull(FString::Printf(TEXT("Gate '%s': player pawn exists at the passage start"), *Case.GateId.ToString()), Pawn))
		{
			return false;
		}

		const FVector SeamFloorCm = GetFieldSeamFloorCm(Case);
		const double StartAlongCm = -(Case.ApproachLengthCm - TDSeamWalkStartInsetCm);
		double GroundZ = SeamFloorCm.Z;
		for (double AlongCm = 0.0; AlongCm > StartAlongCm; AlongCm -= TDSeamWalkMarchStepCm)
		{
			const FVector ProbeCm = SeamFloorCm + Case.PassageDirection * AlongCm;
			TryTraceGroundZ(*World, *Pawn, FVector(ProbeCm.X, ProbeCm.Y, GroundZ), GroundZ);
		}

		const FVector StartFloorCm = SeamFloorCm + Case.PassageDirection * StartAlongCm;
		TryTraceGroundZ(*World, *Pawn, FVector(StartFloorCm.X, StartFloorCm.Y, GroundZ), GroundZ);
		const FVector StartCm(StartFloorCm.X, StartFloorCm.Y, GroundZ + Pawn->GetSimpleCollisionHalfHeight() + TDSeamWalkGroundClearanceCm);
		if (!Test.TestTrue(FString::Printf(TEXT("Gate '%s': pawn moved to the field passage start %s"), *Case.GateId.ToString(), *StartCm.ToCompactString()),
			UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, StartCm, Case.PassageDirection.Rotation())))
		{
			return false;
		}

		State.bIsWalkingIntoRealm = true;
		State.bHasCrossed = false;
		State.bSawPreloadBeforeSeam = false;
		State.bHasReportedSeamWait = false;
		State.LastSetLocationCm = Pawn->GetActorLocation();
		State.LastAlongCm = MeasureAlongCm(State.LastSetLocationCm, Case.FieldSeamCm, Case.PassageDirection);
		State.LastStepWorldSeconds = World->GetTimeSeconds();
		State.LastStepCorrectionCm = FVector::Dist(State.LastSetLocationCm, StartCm);
		State.LastProgressReportPlatformSeconds = FPlatformTime::Seconds();
		Test.AddInfo(FString::Printf(TEXT("%s start: %s."), *MakeSeamWalkPrefix(State, Case), *DescribeSeamWalk(State, Case)));
		return true;
	}

	void CheckSeamCrossing(FAutomationTestBase& Test, const FTDRealmSeamWalkState& State, const FTDRealmGateTravelCase& Case, UWorld& World, const FVector& PawnLocation, double AlongCm, const FString& Prefix)
	{
		Test.AddInfo(FString::Printf(TEXT("%s: crossed the seam to %s (passage coordinate %.1f -> %.1f cm)."), *Prefix, *PawnLocation.ToCompactString(), State.LastAlongCm, AlongCm));
		if (State.bIsWalkingIntoRealm)
		{
			Test.TestTrue(FString::Printf(TEXT("%s: pawn XY is inside realm '%s' right after the crossing"), *Prefix, *Case.RealmId.ToString()), Case.RealmBoundsCm.IsInsideOrOnXY(PawnLocation));
			Test.TestTrue(FString::Printf(TEXT("%s: preload of the realm side started before reaching the seam"), *Prefix), State.bSawPreloadBeforeSeam);
		}
		else
		{
			Test.TestFalse(FString::Printf(TEXT("%s: pawn XY is outside every realm right after the crossing"), *Prefix), IsInsideAnyRealmXY(State.AllRealmBoundsCm, PawnLocation));
		}

		const double AlongChangeCm = FMath::Abs(AlongCm - State.LastAlongCm);
		Test.TestTrue(FString::Printf(TEXT("%s: passage coordinate changed %.1f cm across the seam (< %.0f)"), *Prefix, AlongChangeCm, TDSeamWalkMaxAlongChangeCm), AlongChangeCm < TDSeamWalkMaxAlongChangeCm);

		const APlayerController* PlayerController = World.GetFirstPlayerController();
		const float FadeAmount = PlayerController && PlayerController->PlayerCameraManager ? PlayerController->PlayerCameraManager->FadeAmount : 0.0f;
		Test.TestTrue(FString::Printf(TEXT("%s: camera fade amount %.3f is zero at the crossing"), *Prefix, FadeAmount), FadeAmount <= TDSeamWalkMaxFadeAmount);

		if (const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(&World))
		{
			Test.TestNearlyEqual(FString::Printf(TEXT("%s: realm weight at the seam"), *Prefix), Realm->GetRealmWeight(Case.RealmId), TDSeamWalkSeamWeight, TDSeamWalkSeamWeightTolerance);
		}
		if (const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(&World))
		{
			Test.TestEqual(FString::Printf(TEXT("%s: last traveled id after the crossing"), *Prefix), Travel->GetLastTraveledDungeonId(), Case.GateId);
		}
	}

	bool FinishSeamWalkPhase(FAutomationTestBase& Test, FTDRealmSeamWalkState& State, const FTDRealmGateTravelCase& Case, UWorld& World, const FVector& PawnLocation, const FString& Prefix)
	{
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(&World);
		const float RealmWeight = Realm ? Realm->GetRealmWeight(Case.RealmId) : -1.0f;
		Test.AddInfo(FString::Printf(TEXT("%s: finished at %s with realm weight %.3f."), *Prefix, *PawnLocation.ToCompactString(), RealmWeight));
		if (State.bIsWalkingIntoRealm)
		{
			Test.TestTrue(FString::Printf(TEXT("%s: realm weight %.3f at the realm passage end is at least %.2f"), *Prefix, RealmWeight, TDSeamWalkRealmEndMinWeight), RealmWeight >= TDSeamWalkRealmEndMinWeight);
			State.bIsWalkingIntoRealm = false;
			State.bHasCrossed = false;
			State.bHasReportedSeamWait = false;
			State.LastSetLocationCm = PawnLocation;
			State.LastAlongCm = MeasureAlongCm(PawnLocation, Case.RealmSeamCm, Case.PassageDirection);
			State.LastStepWorldSeconds = World.GetTimeSeconds();
			State.LastStepCorrectionCm = 0.0;
			State.LastProgressReportPlatformSeconds = FPlatformTime::Seconds();
			return true;
		}

		Test.TestFalse(FString::Printf(TEXT("%s: pawn XY is outside every realm at the field passage start"), *Prefix), IsInsideAnyRealmXY(State.AllRealmBoundsCm, PawnLocation));
		Test.TestTrue(FString::Printf(TEXT("%s: pawn XY is inside the field (|X|,|Y| <= %.0f)"), *Prefix, TDRealmPieFieldHalfExtentCm),
			FMath::Abs(PawnLocation.X) <= TDRealmPieFieldHalfExtentCm && FMath::Abs(PawnLocation.Y) <= TDRealmPieFieldHalfExtentCm);
		Test.TestTrue(FString::Printf(TEXT("%s: realm weight %.3f back in the field is at most %.2f"), *Prefix, RealmWeight, TDSeamWalkFieldMaxWeight), RealmWeight >= 0.0f && RealmWeight <= TDSeamWalkFieldMaxWeight);
		return true;
	}

	bool TickSeamWalk(FAutomationTestBase& Test, FTDRealmSeamWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		if (State.bIsAborted)
		{
			return true;
		}

		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(World);
		if (!World || !Pawn || !Travel)
		{
			Test.AddError(FString::Printf(TEXT("Gate '%s': PIE world, player pawn or travel subsystem is gone during the seam walk."), *Case.GateId.ToString()));
			State.bIsAborted = true;
			return true;
		}

		const FString Prefix = MakeSeamWalkPrefix(State, Case);
		const FVector PawnLocation = Pawn->GetActorLocation();
		const bool bIsInRealm = Case.RealmBoundsCm.IsInsideOrOnXY(PawnLocation);
		const FVector& SeamCm = bIsInRealm ? Case.RealmSeamCm : Case.FieldSeamCm;
		const double AlongCm = MeasureAlongCm(PawnLocation, SeamCm, Case.PassageDirection);
		const bool bHasJumped = FVector::Dist(PawnLocation, State.LastSetLocationCm) > TDSeamWalkJumpDistanceCm;
		if (bHasJumped && State.bHasCrossed)
		{
			Test.AddError(FString::Printf(TEXT("%s: pawn jumped again to %s after the crossing."), *Prefix, *PawnLocation.ToCompactString()));
			State.bIsAborted = true;
			return true;
		}
		if (bHasJumped)
		{
			CheckSeamCrossing(Test, State, Case, *World, PawnLocation, AlongCm, Prefix);
			State.bHasCrossed = true;
		}

		const bool bIsPreloadingThisGate = Travel->GetActiveDungeonId() == Case.GateId && IsTravelPreloading(*Travel);
		if (State.bIsWalkingIntoRealm && !State.bHasCrossed && AlongCm < 0.0 && bIsPreloadingThisGate)
		{
			State.bSawPreloadBeforeSeam = true;
		}
		ReportSeamWalkProgressPeriodically(Test, State, Case);

		const bool bHasReachedRealmEnd = State.bIsWalkingIntoRealm && State.bHasCrossed && bIsInRealm && AlongCm >= Case.DepartureLengthCm + TDSeamWalkStartInsetCm;
		const bool bHasReachedFieldStart = !State.bIsWalkingIntoRealm && State.bHasCrossed && !bIsInRealm && AlongCm <= -(Case.ApproachLengthCm - TDSeamWalkStartInsetCm);
		if (bHasReachedRealmEnd || bHasReachedFieldStart)
		{
			return FinishSeamWalkPhase(Test, State, Case, *World, PawnLocation, Prefix);
		}

		const double DirectionSign = State.bIsWalkingIntoRealm ? 1.0 : -1.0;
		const double NowSeconds = World->GetTimeSeconds();
		const double StepSeconds = FMath::Clamp(NowSeconds - State.LastStepWorldSeconds, 0.0, TDSeamWalkMaxStepSeconds);
		State.LastStepWorldSeconds = NowSeconds;
		const bool bIsWaitingPastSeam = !State.bHasCrossed && DirectionSign * AlongCm >= TDSeamWalkPastSeamHoldCm;
		if (bIsWaitingPastSeam)
		{
			if (!State.bHasReportedSeamWait)
			{
				State.bHasReportedSeamWait = true;
				Test.AddInfo(FString::Printf(TEXT("%s: waiting past the seam at %s (travel state %d, active '%s', ready %d)."), *Prefix, *PawnLocation.ToCompactString(),
					static_cast<int32>(Travel->GetTravelState()), *Travel->GetActiveDungeonId().ToString(), Travel->IsReadyFor(Case.GateId, State.bIsWalkingIntoRealm) ? 1 : 0));
			}
			State.LastSetLocationCm = PawnLocation;
			State.LastAlongCm = AlongCm;
			return false;
		}

		const double HalfHeightCm = Pawn->GetSimpleCollisionHalfHeight();
		const FVector WalkDirection = Case.PassageDirection * DirectionSign;
		FVector TargetCm = PawnLocation + WalkDirection * (TDSeamWalkSpeedCmPerSecond * StepSeconds);
		double GroundZ = PawnLocation.Z - HalfHeightCm;
		if (TryTraceGroundZ(*World, *Pawn, FVector(TargetCm.X, TargetCm.Y, GroundZ), GroundZ))
		{
			TargetCm.Z = GroundZ + HalfHeightCm + TDSeamWalkGroundClearanceCm;
		}
		UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, TargetCm, WalkDirection.Rotation());
		State.LastSetLocationCm = Pawn->GetActorLocation();
		State.LastStepCorrectionCm = FVector::Dist(State.LastSetLocationCm, TargetCm);
		State.LastAlongCm = MeasureAlongCm(State.LastSetLocationCm, SeamCm, Case.PassageDirection);
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPreloadCancelAllowsOtherTargetPieTest, "TDGame.SeamlessTravel.PreloadCancelAllowsOtherTarget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDPreloadCancelAllowsOtherTargetPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	TSharedRef<FTDRealmTravelPieTestState> State = MakeShared<FTDRealmTravelPieTestState>();
	if (!OpenMapAndStartPie(*this, OpenWorldMapPackagePath))
	{
		return false;
	}

	EnqueueWaitSeconds(TDRealmPieWarmupSeconds);

	EnqueueStep([this, State]()
	{
		UWorld* World = GetPieWorld();
		UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(World);
		if (!TestNotNull(TEXT("PIE world has a seamless travel subsystem"), Travel))
		{
			State->bIsAborted = true;
			return true;
		}
		State->Travel = Travel;

		if (IsTravelPreloading(*Travel))
		{
			AddInfo(FString::Printf(TEXT("Cancelling preload of '%s' started at spawn."), *Travel->GetActiveDungeonId().ToString()));
			Travel->CancelTravel();
		}
		if (!TestTrue(TEXT("Travel is Idle before the first preload"), Travel->GetTravelState() == ETDSeamlessTravelState::Idle))
		{
			State->bIsAborted = true;
			return true;
		}

		if (!TestTrue(TEXT("BeginPreload(MainCrypt, entry) is accepted"), Travel->BeginPreload(TDPreloadCancelFirstTargetId, true)))
		{
			State->bIsAborted = true;
			return true;
		}
		TestTrue(TEXT("Travel is preloading the first target"), IsTravelPreloading(*Travel));
		TestEqual(TEXT("Active target after the first preload"), Travel->GetActiveDungeonId(), TDPreloadCancelFirstTargetId);
		return true;
	});

	EnqueueWaitSeconds(TDPreloadCancelStreamingSeconds);

	EnqueueStep([this, State]()
	{
		UTDSeamlessTravelSubsystem* Travel = State->Travel.Get();
		if (State->bIsAborted || !Travel)
		{
			return true;
		}

		AddInfo(FString::Printf(TEXT("State before cancel: %d."), static_cast<int32>(Travel->GetTravelState())));
		TestFalse(TEXT("CancelPreload of a different target is rejected"), Travel->CancelPreload(TDPreloadCancelSecondTargetId, true));
		TestFalse(TEXT("CancelPreload of the other direction is rejected"), Travel->CancelPreload(TDPreloadCancelFirstTargetId, false));
		TestTrue(TEXT("CancelPreload(MainCrypt, entry) succeeds"), Travel->CancelPreload(TDPreloadCancelFirstTargetId, true));
		TestTrue(TEXT("Travel is Idle after CancelPreload"), Travel->GetTravelState() == ETDSeamlessTravelState::Idle);

		TestTrue(TEXT("BeginPreload(HollowCave, entry) is accepted after the cancel"), Travel->BeginPreload(TDPreloadCancelSecondTargetId, true));
		TestEqual(TEXT("Active target after the second preload"), Travel->GetActiveDungeonId(), TDPreloadCancelSecondTargetId);

		Travel->CancelTravel();
		TestTrue(TEXT("Travel is Idle after CancelTravel"), Travel->GetTravelState() == ETDSeamlessTravelState::Idle);
		return true;
	});

	EnqueueEndPie();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmRoundTripPieTest, "TDGame.SeamlessTravel.RealmRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDRealmRoundTripPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	TSharedRef<FTDRealmTravelPieTestState> State = MakeShared<FTDRealmTravelPieTestState>();
	FString ConfiguredRealmPaths;
	const TArray<FTDRealmGateTravelCase> Cases = CollectRealmGateTravelCases(State->AllRealmBoundsCm, ConfiguredRealmPaths);
	if (State->AllRealmBoundsCm.IsEmpty())
	{
		AddError(FString::Printf(TEXT("No realm definition could be loaded from UTDWorldGenSettings.RealmDefinitions (configured:%s). Bake the realms with Tools/WorldGen/editor_build_realm.py before running this test."),
			ConfiguredRealmPaths.IsEmpty() ? TEXT(" none") : *ConfiguredRealmPaths));
		return false;
	}
	if (Cases.IsEmpty())
	{
		AddError(TEXT("Realm definitions were loaded but none of them has a gate."));
		return false;
	}

	if (!OpenMapAndStartPie(*this, OpenWorldMapPackagePath))
	{
		return false;
	}

	const auto Abort = [State]() { State->bIsAborted = true; };
	const auto WaitForSettledTravel = [this, State, Abort](const FString& Description)
	{
		EnqueueWaitUntil(*this, Description, [State]()
		{
			return State->bIsAborted || !State->Recorder.IsValid() || CountSettledTravels(*State->Recorder) >= State->ExpectedSettledTravelCount;
		}, TDRealmTravelTimeoutSeconds, Abort);
	};

	EnqueueWaitSeconds(TDRealmPieWarmupSeconds);

	EnqueueStep([this, State]()
	{
		UWorld* World = GetPieWorld();
		UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(World);
		if (!TestNotNull(TEXT("PIE world has a seamless travel subsystem"), Travel)
			|| !TestNotNull(TEXT("PIE world has a realm subsystem"), UTDRealmSubsystem::GetRealmSubsystem(World))
			|| !TestNotNull(TEXT("PIE world has a player pawn"), FindPlayerPawn(World)))
		{
			State->bIsAborted = true;
			return true;
		}

		State->Travel = Travel;
		State->Recorder.Reset(NewObject<UTDTravelStateRecorder>(GetTransientPackage()));
		State->Recorder->BindTo(Travel);
		AddInfo(FString::Printf(TEXT("Pawn starts at %s, travel state %d."), *FindPlayerPawn(World)->GetActorLocation().ToCompactString(), static_cast<int32>(Travel->GetTravelState())));
		return true;
	});

	for (const FTDRealmGateTravelCase& Case : Cases)
	{
		EnqueueStep([this, State, Case]()
		{
			if (State->bIsAborted)
			{
				return true;
			}
			State->bIsAborted = !RequestRealmGateTravel(*this, *State, Case.GateId, true);
			return true;
		});

		WaitForSettledTravel(FString::Printf(TEXT("Travel through gate '%s' into realm '%s' settles"), *Case.GateId.ToString(), *Case.RealmId.ToString()));

		EnqueueStep([this, State, Case]()
		{
			if (State->bIsAborted)
			{
				return true;
			}

			UWorld* World = GetPieWorld();
			APawn* Pawn = FindPlayerPawn(World);
			UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(World);
			if (!TestNotNull(TEXT("Player pawn exists inside the realm"), Pawn) || !Realm || !State->Travel.IsValid())
			{
				State->bIsAborted = true;
				return true;
			}

			const FVector PawnLocation = Pawn->GetActorLocation();
			AddInfo(FString::Printf(TEXT("Gate '%s': pawn inside realm at %s. Transitions: %s"), *Case.GateId.ToString(), *PawnLocation.ToCompactString(), *State->Recorder->DescribeTransitions()));
			TestEqual(FString::Printf(TEXT("Gate '%s': last traveled id"), *Case.GateId.ToString()), State->Travel->GetLastTraveledDungeonId(), Case.GateId);
			TestTrue(FString::Printf(TEXT("Gate '%s': pawn XY is inside realm '%s' bounds %s"), *Case.GateId.ToString(), *Case.RealmId.ToString(), *Case.RealmBoundsCm.ToString()), Case.RealmBoundsCm.IsInsideOrOnXY(PawnLocation));
			TestSunIntensityBetween(*this, World, FString::Printf(TEXT("Gate '%s': sun intensity lies between the field and realm values"), *Case.GateId.ToString()), Realm->GetFieldAtmosphere().SunIntensityLux, Case.RealmSunIntensityLux);

			State->bIsAborted = !RequestRealmGateTravel(*this, *State, Case.GateId, false);
			return true;
		});

		WaitForSettledTravel(FString::Printf(TEXT("Travel through gate '%s' back to the field settles"), *Case.GateId.ToString()));

		EnqueueStep([this, State, Case]()
		{
			if (State->bIsAborted)
			{
				return true;
			}

			UWorld* World = GetPieWorld();
			APawn* Pawn = FindPlayerPawn(World);
			UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(World);
			if (!TestNotNull(TEXT("Player pawn exists after returning to the field"), Pawn) || !Realm)
			{
				State->bIsAborted = true;
				return true;
			}

			const FVector PawnLocation = Pawn->GetActorLocation();
			AddInfo(FString::Printf(TEXT("Gate '%s': pawn back in field at %s. Transitions: %s"), *Case.GateId.ToString(), *PawnLocation.ToCompactString(), *State->Recorder->DescribeTransitions()));
			TestFalse(FString::Printf(TEXT("Gate '%s': pawn is outside every realm after return"), *Case.GateId.ToString()), IsInsideAnyRealmXY(State->AllRealmBoundsCm, PawnLocation));
			TestTrue(FString::Printf(TEXT("Gate '%s': pawn XY is inside the field (|X|,|Y| <= %.0f)"), *Case.GateId.ToString(), TDRealmPieFieldHalfExtentCm),
				FMath::Abs(PawnLocation.X) <= TDRealmPieFieldHalfExtentCm && FMath::Abs(PawnLocation.Y) <= TDRealmPieFieldHalfExtentCm);
			TestSunIntensityBetween(*this, World, FString::Printf(TEXT("Gate '%s': sun intensity after return lies between the field and realm values"), *Case.GateId.ToString()), Realm->GetFieldAtmosphere().SunIntensityLux, Case.RealmSunIntensityLux);
			return true;
		});
	}

	EnqueueStep([State]()
	{
		if (State->Recorder.IsValid())
		{
			State->Recorder->UnbindFrom(State->Travel.Get());
			State->Recorder.Reset();
		}
		return true;
	});

	EnqueueEndPie();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmSeamWalkPieTest, "TDGame.SeamlessTravel.RealmSeamWalk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDRealmSeamWalkPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	TSharedRef<FTDRealmSeamWalkState> State = MakeShared<FTDRealmSeamWalkState>();
	FString ConfiguredRealmPaths;
	const TArray<FTDRealmGateTravelCase> Cases = CollectRealmGateTravelCases(State->AllRealmBoundsCm, ConfiguredRealmPaths);
	if (State->AllRealmBoundsCm.IsEmpty())
	{
		AddError(FString::Printf(TEXT("No realm definition could be loaded from UTDWorldGenSettings.RealmDefinitions (configured:%s). Bake the realms with Tools/WorldGen/editor_build_realm.py before running this test."),
			ConfiguredRealmPaths.IsEmpty() ? TEXT(" none") : *ConfiguredRealmPaths));
		return false;
	}
	if (Cases.IsEmpty())
	{
		AddError(TEXT("Realm definitions were loaded but none of them has a gate."));
		return false;
	}

	if (!OpenMapAndStartPie(*this, OpenWorldMapPackagePath))
	{
		return false;
	}

	const auto Abort = [State]() { State->bIsAborted = true; };

	EnqueueWaitSeconds(TDRealmPieWarmupSeconds);

	EnqueueStep([this, State]()
	{
		UWorld* World = GetPieWorld();
		if (!TestNotNull(TEXT("PIE world has a seamless travel subsystem"), UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(World))
			|| !TestNotNull(TEXT("PIE world has a realm subsystem"), UTDRealmSubsystem::GetRealmSubsystem(World))
			|| !TestNotNull(TEXT("PIE world has a player pawn"), FindPlayerPawn(World)))
		{
			State->bIsAborted = true;
		}
		return true;
	});

	for (const FTDRealmGateTravelCase& Case : Cases)
	{
		const auto ReportAndAbortSeamWalk = [this, State, Case]()
		{
			AddInfo(FString::Printf(TEXT("%s state at the timeout: %s."), *MakeSeamWalkPrefix(*State, Case), *DescribeSeamWalk(*State, Case)));
			State->bIsAborted = true;
		};

		EnqueueStep([this, State, Case]()
		{
			if (!State->bIsAborted)
			{
				State->bIsAborted = !PlacePawnAtFieldSeam(*this, *State, Case);
			}
			return true;
		});

		EnqueueWaitUntil(*this, FString::Printf(TEXT("Field seam area of gate '%s' streams in"), *Case.GateId.ToString()), [State, Case]()
		{
			return State->bIsAborted || HoldPawnAtFieldSeamUntilLoaded(*State, Case);
		}, TDSeamWalkTimeoutSeconds, Abort);

		EnqueueStep([this, State, Case]()
		{
			if (!State->bIsAborted)
			{
				State->bIsAborted = !PlacePawnAtPassageStart(*this, *State, Case);
			}
			return true;
		});

		EnqueueWaitUntil(*this, FString::Printf(TEXT("Walk through gate '%s' into realm '%s'"), *Case.GateId.ToString(), *Case.RealmId.ToString()), [this, State, Case]()
		{
			return TickSeamWalk(*this, *State, Case);
		}, TDSeamWalkTimeoutSeconds, ReportAndAbortSeamWalk);

		EnqueueWaitUntil(*this, FString::Printf(TEXT("Walk back through gate '%s' to the field"), *Case.GateId.ToString()), [this, State, Case]()
		{
			return TickSeamWalk(*this, *State, Case);
		}, TDSeamWalkTimeoutSeconds, ReportAndAbortSeamWalk);
	}

	EnqueueEndPie();
	return true;
}

#endif
