#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/PlayerCameraManager.h"
#include "Characters/TDCombatCharacter.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "TDWorldGenSettings.h"
#include "Tests/TDPieWaitUntilCommand.h"
#include "Tests/TDTravelStateRecorder.h"
#include "UObject/StrongObjectPtr.h"
#include "World/Streaming/TDRealmLiftDeck.h"
#include "World/Streaming/TDRealmShaftLiner.h"
#include "World/Streaming/TDRealmSubsystem.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"
#include "World/TDRealmDefinitions.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace
{
	const FName TDPreloadCancelFirstTargetId(TEXT("MainCrypt"));
	const FName TDPreloadCancelSecondTargetId(TEXT("HollowCave"));
	constexpr float TDRealmPieWarmupSeconds = 10.0f;
	constexpr float TDPreloadCancelStreamingSeconds = 1.0f;
	constexpr double TDRealmTravelTimeoutSeconds = 90.0;
	constexpr double TDRealmPieFieldHalfExtentCm = 50400.0;
	constexpr double TDRealmPieAdjacencyToleranceCm = 100.0;
	constexpr float TDRealmSunIntensityToleranceLux = 0.01f;
	constexpr double TDRealmLiftPieExitArrivalRadiusCm = 300.0;
	constexpr double TDRealmLiftPieExitArrivalHeightToleranceCm = 150.0;

	constexpr double TDRealmWalkSpeedCmPerSecond = 600.0;
	constexpr double TDRealmWalkMaxStepSeconds = 0.1;
	constexpr double TDRealmWalkTraceAboveCm = 200.0;
	constexpr double TDRealmWalkTraceBelowCm = 2000.0;
	constexpr double TDRealmWalkStartProbeAboveCm = 1500.0;
	constexpr double TDRealmWalkStartProbeBelowCm = 4000.0;
	constexpr double TDRealmWalkGroundClearanceCm = 2.0;
	constexpr double TDRealmWalkHoverCm = 300.0;
	constexpr double TDRealmWalkStartBeforeWindowCm = 300.0;
	constexpr double TDRealmWalkMinimumHoldSeconds = 3.0;
	constexpr double TDRealmWalkSettleSeconds = 1.0;
	constexpr double TDRealmWalkStreamingTimeoutSeconds = 90.0;
	constexpr double TDRealmWalkTimeoutSeconds = 180.0;
	constexpr double TDRealmWalkProgressReportSeconds = 10.0;

	constexpr double TDRealmContinuityMaxStepCm = 5000.0;
	constexpr float TDRealmContinuityMaxWeightStep = 0.01f;
	constexpr float TDRealmContinuityMaxFadeAmount = 0.001f;

	constexpr float TDRealmConnectorBoundaryWeight = 0.5f;
	constexpr float TDRealmConnectorBoundaryWeightTolerance = 0.02f;
	constexpr double TDRealmConnectorMaxReadinessSeconds = 1.0;
	constexpr float TDRealmConnectorRealmEndMinWeight = 0.99f;
	constexpr float TDRealmConnectorFieldEndMaxWeight = 0.01f;

	struct FTDRealmGateTravelCase
	{
		FName RealmId;
		FName GateId;
		FBox RealmBoundsCm = FBox(ForceInit);
		float RealmSunIntensityLux = 0.0f;
		bool bIsLift = false;
		FVector FieldReturnCm = FVector::ZeroVector;
		FVector ArrivalCm = FVector::ZeroVector;
		FVector BoundaryCm = FVector::ZeroVector;
		FVector PassageDirection = FVector::ForwardVector;
		double ApproachLengthCm = 0.0;
		double DepartureLengthCm = 0.0;
		double PreloadLeadCm = 0.0;
		FVector FieldLiftExitCm = FVector::ZeroVector;
		FVector RealmLiftExitCm = FVector::ZeroVector;
		FVector FieldLandingCm = FVector::ZeroVector;
		FVector RealmLandingCm = FVector::ZeroVector;
		double HoldDepthCm = 0.0;
		double RingModuleHeightCm = 0.0;
		double RingTopDepthCm = 0.0;
		double CruiseSpeedCmPerSecond = 0.0;
		TSoftObjectPtr<UDataLayerAsset> GameplayDataLayer;
	};

	struct FTDRealmTravelPieTestState
	{
		bool bIsAborted = false;
		TWeakObjectPtr<UTDSeamlessTravelSubsystem> Travel;
		TStrongObjectPtr<UTDTravelStateRecorder> Recorder;
		TArray<FBox> AllRealmBoundsCm;
		int32 ExpectedSettledTravelCount = 0;
	};

	class FTDGameCameraCutCounter
	{
	public:
		FTDGameCameraCutCounter() = default;
		FTDGameCameraCutCounter(const FTDGameCameraCutCounter&) = delete;
		FTDGameCameraCutCounter& operator=(const FTDGameCameraCutCounter&) = delete;

		~FTDGameCameraCutCounter()
		{
			Stop();
		}

		void Start()
		{
			if (TickHandle.IsValid())
			{
				return;
			}
			TickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FTDGameCameraCutCounter::CountWorldTick);
		}

		void Stop()
		{
			if (!TickHandle.IsValid())
			{
				return;
			}
			FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
			TickHandle.Reset();
		}

		int32 GetObservedFrameCount() const { return ObservedFrameCount; }
		int32 GetCutFrameCount() const { return CutFrameCount; }

	private:
		void CountWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
		{
			if (!World || World->WorldType != EWorldType::PIE)
			{
				return;
			}
			const APlayerController* PlayerController = World->GetFirstPlayerController();
			const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
			if (!CameraManager)
			{
				return;
			}
			++ObservedFrameCount;
			CutFrameCount += CameraManager->bGameCameraCutThisFrame ? 1 : 0;
		}

		FDelegateHandle TickHandle;
		int32 ObservedFrameCount = 0;
		int32 CutFrameCount = 0;
	};

	struct FTDRealmTravelCommitWatch
	{
		TWeakObjectPtr<UTDSeamlessTravelSubsystem> Travel;
		TStrongObjectPtr<UTDTravelStateRecorder> Recorder;
	};

	struct FTDRealmContinuityMetrics
	{
		bool bHasPreviousSample = false;
		FVector PreviousPawnCm = FVector::ZeroVector;
		FVector PreviousCameraCm = FVector::ZeroVector;
		float PreviousWeight = 0.0f;
		int32 SampleCount = 0;
		int32 JumpFrameCount = 0;
		int32 CameraCutCountAtStart = 0;
		int32 CameraObservedCountAtStart = 0;
		int32 CameraObservedFrameCount = 0;
		int32 CameraCutFrameCount = 0;
		int32 FadeFrameCount = 0;
		int32 WeightStepFrameCount = 0;
		double MaxPawnStepCm = 0.0;
		double MaxCameraStepCm = 0.0;
		float MaxWeightStep = 0.0f;
	};

	struct FTDRealmWalkPath
	{
		FString Label;
		FName RealmId;
		FVector StartCm = FVector::ZeroVector;
		FVector EndCm = FVector::ZeroVector;

		double GetLengthCm() const { return FVector::Dist2D(StartCm, EndCm); }
		FVector GetDirection() const { return (EndCm - StartCm).GetSafeNormal2D(); }
		FVector GetPointCm(double DistanceCm) const { return StartCm + GetDirection() * DistanceCm; }
	};

	struct FTDRealmWalkState
	{
		bool bIsAborted = false;
		bool bIsForward = true;
		double DistanceCm = 0.0;
		double GroundZCm = 0.0;
		double HoldStartWorldSeconds = 0.0;
		double LastStepWorldSeconds = 0.0;
		int32 MissingGroundFrameCount = 0;
		double LastProgressReportPlatformSeconds = 0.0;
		FTDRealmContinuityMetrics Continuity;
	};

	struct FTDRealmConnectorObservation
	{
		bool bIsWalkingIntoRealm = true;
		double PhaseStartWorldSeconds = -1.0;
		double ReadyWorldSeconds = -1.0;
		bool bHasPreviousSample = false;
		double PreviousAlongCm = 0.0;
		float PreviousWeight = 0.0f;
		int32 BoundaryCrossingCount = 0;
		double BoundaryAlongCm = 0.0;
		float BoundaryWeight = -1.0f;
		int32 InitialPacingCount = 0;
		bool bHasGameplayLayer = false;
		EDataLayerRuntimeState InitialLayerState = EDataLayerRuntimeState::Unloaded;
		EDataLayerRuntimeState LastLayerState = EDataLayerRuntimeState::Unloaded;
		int32 LayerActivationCount = 0;
	};

	struct FTDRealmConnectorWalkState
	{
		FTDRealmWalkPath Path;
		FTDRealmWalkState Walk;
		FTDRealmConnectorObservation Observation;
		TArray<FBox> AllRealmBoundsCm;
		FTDGameCameraCutCounter CameraCuts;
		FTDRealmTravelCommitWatch TravelCommits;
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

	bool BeginTravelCommitWatch(FAutomationTestBase& Test, FTDRealmTravelCommitWatch& Watch)
	{
		UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(TDPieTestUtils::GetPieWorld());
		if (!Test.TestNotNull(TEXT("PIE world has a seamless travel subsystem to watch for travel commits"), Travel))
		{
			return false;
		}

		Watch.Travel = Travel;
		Watch.Recorder.Reset(NewObject<UTDTravelStateRecorder>(GetTransientPackage()));
		Watch.Recorder->BindTo(Travel);
		return true;
	}

	void TestNoTravelCommit(FAutomationTestBase& Test, const FTDRealmTravelCommitWatch& Watch, const FString& Prefix)
	{
		if (!Test.TestTrue(FString::Printf(TEXT("%s: travel states were recorded"), *Prefix), Watch.Recorder.IsValid()))
		{
			return;
		}

		const int32 SettledTravelCount = CountSettledTravels(*Watch.Recorder);
		const int32 TravelingCount = Watch.Recorder->CountTransitionsTo(ETDSeamlessTravelState::Traveling);
		Test.AddInfo(FString::Printf(TEXT("%s travel states: %d settled travels, %d transitions into Traveling. Transitions: %s"), *Prefix, SettledTravelCount, TravelingCount, *Watch.Recorder->DescribeTransitions()));
		Test.TestEqual(FString::Printf(TEXT("%s: settled travel commits"), *Prefix), SettledTravelCount, 0);
		Test.TestEqual(FString::Printf(TEXT("%s: transitions into Traveling"), *Prefix), TravelingCount, 0);
	}

	void StopTravelCommitWatch(FTDRealmTravelCommitWatch& Watch)
	{
		if (!Watch.Recorder.IsValid())
		{
			return;
		}
		Watch.Recorder->UnbindFrom(Watch.Travel.Get());
		Watch.Recorder.Reset();
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

	bool IsInsideFieldXY(const FVector& LocationCm)
	{
		return FMath::Abs(LocationCm.X) <= TDRealmPieFieldHalfExtentCm && FMath::Abs(LocationCm.Y) <= TDRealmPieFieldHalfExtentCm;
	}

	FTDRealmGateTravelCase MakeRealmGateTravelCase(const UTDRealmDefinition& Realm, const FTDRealmGate& Gate)
	{
		FTDRealmGateTravelCase Case;
		Case.RealmId = Realm.RealmId;
		Case.GateId = Gate.GateId;
		Case.RealmBoundsCm = Realm.GetBoundsCm();
		Case.RealmSunIntensityLux = Realm.Atmosphere.SunIntensityLux;
		Case.bIsLift = Gate.GateKind == ETDRealmGateKind::Lift;
		Case.FieldReturnCm = Gate.FieldReturnTransform.GetLocation();
		Case.ArrivalCm = Gate.ArrivalTransform.GetLocation();
		Case.BoundaryCm = Gate.GetBoundaryCrossingCm();
		Case.PassageDirection = Gate.GetPassageDirection();
		Case.ApproachLengthCm = Gate.ApproachLengthCm;
		Case.DepartureLengthCm = Gate.DepartureLengthCm;
		Case.PreloadLeadCm = Gate.PreloadLeadCm;
		Case.FieldLiftExitCm = Gate.GetTravelDestination(false).GetLocation();
		Case.RealmLiftExitCm = Gate.GetTravelDestination(true).GetLocation();
		Case.FieldLandingCm = Gate.Lift.FieldLandingTransform.GetLocation();
		Case.RealmLandingCm = Gate.Lift.RealmLandingTransform.GetLocation();
		Case.HoldDepthCm = Gate.Lift.HoldDepthCm;
		Case.RingModuleHeightCm = Gate.Lift.RingModuleHeightCm;
		Case.RingTopDepthCm = Gate.Lift.RingTopDepthCm;
		Case.CruiseSpeedCmPerSecond = Gate.Lift.CruiseSpeedCmPerSecond;
		Case.GameplayDataLayer = Realm.GameplayDataLayer;
		return Case;
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
				Cases.Add(MakeRealmGateTravelCase(*Realm, Gate));
			}
		}
		return Cases;
	}

	bool TestRealmGateCasesLoaded(FAutomationTestBase& Test, const TArray<FTDRealmGateTravelCase>& Cases, const TArray<FBox>& AllRealmBoundsCm, const FString& ConfiguredRealmPaths)
	{
		if (AllRealmBoundsCm.IsEmpty())
		{
			Test.AddError(FString::Printf(TEXT("No realm definition could be loaded from UTDWorldGenSettings.RealmDefinitions (configured:%s). Bake the realms with Tools/WorldGen/editor_build_realm.py before running this test."),
				ConfiguredRealmPaths.IsEmpty() ? TEXT(" none") : *ConfiguredRealmPaths));
			return false;
		}
		if (Cases.IsEmpty())
		{
			Test.AddError(TEXT("Realm definitions were loaded but none of them has a gate."));
			return false;
		}
		return true;
	}

	bool IsRealmAdjacentToField(const FBox& RealmBoundsCm)
	{
		const double GapXCm = FMath::Max3(-TDRealmPieFieldHalfExtentCm - RealmBoundsCm.Max.X, RealmBoundsCm.Min.X - TDRealmPieFieldHalfExtentCm, 0.0);
		const double GapYCm = FMath::Max3(-TDRealmPieFieldHalfExtentCm - RealmBoundsCm.Max.Y, RealmBoundsCm.Min.Y - TDRealmPieFieldHalfExtentCm, 0.0);
		return GapXCm <= TDRealmPieAdjacencyToleranceCm && GapYCm <= TDRealmPieAdjacencyToleranceCm;
	}

	bool TestContinuousRealmLayout(FAutomationTestBase& Test, const TArray<FBox>& AllRealmBoundsCm)
	{
		const FBox* DetachedRealmBoundsCm = AllRealmBoundsCm.FindByPredicate([](const FBox& RealmBoundsCm) { return !IsRealmAdjacentToField(RealmBoundsCm); });
		if (!DetachedRealmBoundsCm)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("Realm bounds %s do not touch the field box (|X|,|Y| <= %.0f cm): the continuous-world layout (D-45) baked by S4 is missing or has regressed. Rebake with the S4 steps in Tools/WorldGen/README.md, then run this test again."),
			*DetachedRealmBoundsCm->ToString(), TDRealmPieFieldHalfExtentCm));
		return false;
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

	void TestPawnAtRealmLiftExit(FAutomationTestBase& Test, const APawn& Pawn, const FTDRealmGateTravelCase& Case, const FVector& ExitCm, const TCHAR* LandingName)
	{
		if (!Case.bIsLift)
		{
			return;
		}

		const FVector PawnLocation = Pawn.GetActorLocation();
		const double HorizontalDistanceCm = FVector::Dist2D(PawnLocation, ExitCm);
		const double StandingHeightErrorCm = PawnLocation.Z - (ExitCm.Z + Pawn.GetSimpleCollisionHalfHeight());
		Test.TestTrue(FString::Printf(TEXT("Gate '%s': pawn arrived %.1f cm from the %s lift landing exit %s (<= %.0f)"), *Case.GateId.ToString(), HorizontalDistanceCm, LandingName, *ExitCm.ToCompactString(), TDRealmLiftPieExitArrivalRadiusCm),
			HorizontalDistanceCm <= TDRealmLiftPieExitArrivalRadiusCm);
		Test.TestTrue(FString::Printf(TEXT("Gate '%s': pawn stands %.1f cm from the %s lift landing floor height (|error| <= %.0f)"), *Case.GateId.ToString(), StandingHeightErrorCm, LandingName, TDRealmLiftPieExitArrivalHeightToleranceCm),
			FMath::Abs(StandingHeightErrorCm) <= TDRealmLiftPieExitArrivalHeightToleranceCm);
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

	void BeginRealmContinuity(FTDRealmContinuityMetrics& Metrics, const FTDGameCameraCutCounter& CameraCuts)
	{
		Metrics = FTDRealmContinuityMetrics();
		Metrics.CameraCutCountAtStart = CameraCuts.GetCutFrameCount();
		Metrics.CameraObservedCountAtStart = CameraCuts.GetObservedFrameCount();
	}

	void SampleRealmContinuity(FTDRealmContinuityMetrics& Metrics, UWorld& World, const APawn& Pawn, FName RealmId, const FTDGameCameraCutCounter& CameraCuts)
	{
		const APlayerController* PlayerController = World.GetFirstPlayerController();
		const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(&World);
		const FVector PawnCm = Pawn.GetActorLocation();
		const FVector CameraCm = CameraManager ? CameraManager->GetCameraLocation() : PawnCm;
		const float Weight = Realm ? Realm->GetRealmWeight(RealmId) : 0.0f;

		++Metrics.SampleCount;
		Metrics.CameraCutFrameCount = CameraCuts.GetCutFrameCount() - Metrics.CameraCutCountAtStart;
		Metrics.CameraObservedFrameCount = CameraCuts.GetObservedFrameCount() - Metrics.CameraObservedCountAtStart;
		Metrics.FadeFrameCount += CameraManager && CameraManager->FadeAmount > TDRealmContinuityMaxFadeAmount ? 1 : 0;
		if (Metrics.bHasPreviousSample)
		{
			const double PawnStepCm = FVector::Dist(PawnCm, Metrics.PreviousPawnCm);
			const double CameraStepCm = FVector::Dist(CameraCm, Metrics.PreviousCameraCm);
			const float WeightStep = FMath::Abs(Weight - Metrics.PreviousWeight);
			Metrics.MaxPawnStepCm = FMath::Max(Metrics.MaxPawnStepCm, PawnStepCm);
			Metrics.MaxCameraStepCm = FMath::Max(Metrics.MaxCameraStepCm, CameraStepCm);
			Metrics.MaxWeightStep = FMath::Max(Metrics.MaxWeightStep, WeightStep);
			Metrics.JumpFrameCount += PawnStepCm > TDRealmContinuityMaxStepCm || CameraStepCm > TDRealmContinuityMaxStepCm ? 1 : 0;
			Metrics.WeightStepFrameCount += WeightStep > TDRealmContinuityMaxWeightStep ? 1 : 0;
		}

		Metrics.bHasPreviousSample = true;
		Metrics.PreviousPawnCm = PawnCm;
		Metrics.PreviousCameraCm = CameraCm;
		Metrics.PreviousWeight = Weight;
	}

	void TestRealmContinuity(FAutomationTestBase& Test, const FString& Prefix, const FTDRealmContinuityMetrics& Metrics)
	{
		Test.AddInfo(FString::Printf(TEXT("%s continuity: %d frames, max pawn step %.1f cm, max camera step %.1f cm, max realm weight step %.4f, camera cut frames %d of %d world ticks read after the camera update, faded frames %d."),
			*Prefix, Metrics.SampleCount, Metrics.MaxPawnStepCm, Metrics.MaxCameraStepCm, Metrics.MaxWeightStep, Metrics.CameraCutFrameCount, Metrics.CameraObservedFrameCount, Metrics.FadeFrameCount));
		Test.TestTrue(FString::Printf(TEXT("%s: more than one frame was sampled"), *Prefix), Metrics.SampleCount > 1);
		Test.TestEqual(FString::Printf(TEXT("%s: frames whose pawn or camera moved more than %.0f cm"), *Prefix, TDRealmContinuityMaxStepCm), Metrics.JumpFrameCount, 0);
		Test.TestTrue(FString::Printf(TEXT("%s: the camera cut flag was read after the camera update on %d world ticks"), *Prefix, Metrics.CameraObservedFrameCount), Metrics.CameraObservedFrameCount > 0);
		Test.TestEqual(FString::Printf(TEXT("%s: world ticks with a game camera cut"), *Prefix), Metrics.CameraCutFrameCount, 0);
		Test.TestEqual(FString::Printf(TEXT("%s: frames with a camera fade above %.3f"), *Prefix, TDRealmContinuityMaxFadeAmount), Metrics.FadeFrameCount, 0);
		Test.TestEqual(FString::Printf(TEXT("%s: frames whose realm weight changed more than %.2f (max %.4f)"), *Prefix, TDRealmContinuityMaxWeightStep, Metrics.MaxWeightStep), Metrics.WeightStepFrameCount, 0);
	}

	bool TryTraceWalkGroundZ(UWorld& World, const APawn& Pawn, const FVector& PointCm, double AboveCm, double BelowCm, double& OutGroundZ)
	{
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDRealmWalkGround), false, &Pawn);
		FHitResult Hit;
		const FVector TraceStart(PointCm.X, PointCm.Y, PointCm.Z + AboveCm);
		const FVector TraceEnd(PointCm.X, PointCm.Y, PointCm.Z - BelowCm);
		if (!World.LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams) || Hit.bStartPenetrating)
		{
			return false;
		}
		OutGroundZ = Hit.Location.Z;
		return true;
	}

	FVector MakeWalkStandingCm(const APawn& Pawn, const FVector& PointCm, double GroundZ)
	{
		return FVector(PointCm.X, PointCm.Y, GroundZ + Pawn.GetSimpleCollisionHalfHeight() + TDRealmWalkGroundClearanceCm);
	}

	FString DescribeRealmWalk(const FTDRealmWalkState& State, const FTDRealmWalkPath& Path)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(World);
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(World);
		if (!Pawn || !Travel || !Realm)
		{
			return TEXT("PIE world, player pawn or a realm subsystem is gone");
		}

		return FString::Printf(TEXT("pawn %s, walked %.0f of %.0f cm %s, ground Z %.1f cm, %d frames without ground, travel state %d, active '%s', realm weight %.3f"),
			*Pawn->GetActorLocation().ToCompactString(), State.DistanceCm, Path.GetLengthCm(), State.bIsForward ? TEXT("forward") : TEXT("backward"), State.GroundZCm,
			State.MissingGroundFrameCount, static_cast<int32>(Travel->GetTravelState()), *Travel->GetActiveDungeonId().ToString(), Realm->GetRealmWeight(Path.RealmId));
	}

	void ReportRealmWalkProgressPeriodically(FAutomationTestBase& Test, FTDRealmWalkState& State, const FTDRealmWalkPath& Path)
	{
		const double NowPlatformSeconds = FPlatformTime::Seconds();
		if (NowPlatformSeconds - State.LastProgressReportPlatformSeconds < TDRealmWalkProgressReportSeconds)
		{
			return;
		}
		State.LastProgressReportPlatformSeconds = NowPlatformSeconds;
		Test.AddInfo(FString::Printf(TEXT("%s progress: %s."), *Path.Label, *DescribeRealmWalk(State, Path)));
	}

	bool PlacePawnAboveWalkStart(FAutomationTestBase& Test, FTDRealmWalkState& State, const FTDRealmWalkPath& Path)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		if (!World || !Test.TestNotNull(FString::Printf(TEXT("%s: player pawn exists before the walk"), *Path.Label), Pawn))
		{
			return false;
		}

		State.HoldStartWorldSeconds = World->GetTimeSeconds();
		return Test.TestTrue(FString::Printf(TEXT("%s: pawn moved above the walk start %s"), *Path.Label, *Path.StartCm.ToCompactString()),
			UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, Path.StartCm + FVector(0.0, 0.0, TDRealmWalkHoverCm), Path.GetDirection().Rotation()));
	}

	bool HoldPawnAboveWalkStartUntilLoaded(FTDRealmWalkState& State, const FTDRealmWalkPath& Path)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		if (!World || !Pawn)
		{
			return false;
		}

		const UWorldPartitionSubsystem* WorldPartition = World->GetSubsystem<UWorldPartitionSubsystem>();
		const bool bHasHeldLongEnough = World->GetTimeSeconds() - State.HoldStartWorldSeconds >= TDRealmWalkMinimumHoldSeconds;
		const bool bIsStreamingCompleted = !WorldPartition || WorldPartition->IsStreamingCompleted();
		double GroundZ = 0.0;
		if (bHasHeldLongEnough && bIsStreamingCompleted && TryTraceWalkGroundZ(*World, *Pawn, Path.StartCm, TDRealmWalkStartProbeAboveCm, TDRealmWalkStartProbeBelowCm, GroundZ))
		{
			State.GroundZCm = GroundZ;
			State.DistanceCm = 0.0;
			State.HoldStartWorldSeconds = World->GetTimeSeconds();
			UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, MakeWalkStandingCm(*Pawn, Path.StartCm, GroundZ), Path.GetDirection().Rotation());
			return true;
		}

		UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, Path.StartCm + FVector(0.0, 0.0, TDRealmWalkHoverCm), Path.GetDirection().Rotation());
		return false;
	}

	bool HasSettledOnWalkStart(const FTDRealmWalkState& State)
	{
		const UWorld* World = TDPieTestUtils::GetPieWorld();
		return !World || World->GetTimeSeconds() - State.HoldStartWorldSeconds >= TDRealmWalkSettleSeconds;
	}

	void BeginRealmWalkPhase(FTDRealmWalkState& State, bool bIsForward, const FTDGameCameraCutCounter& CameraCuts)
	{
		const UWorld* World = TDPieTestUtils::GetPieWorld();
		State.bIsForward = bIsForward;
		State.MissingGroundFrameCount = 0;
		BeginRealmContinuity(State.Continuity, CameraCuts);
		State.LastStepWorldSeconds = World ? World->GetTimeSeconds() : 0.0;
		State.LastProgressReportPlatformSeconds = FPlatformTime::Seconds();
	}

	bool TickRealmWalk(FAutomationTestBase& Test, FTDRealmWalkState& State, const FTDRealmWalkPath& Path, const FTDGameCameraCutCounter& CameraCuts)
	{
		if (State.bIsAborted)
		{
			return true;
		}

		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		if (!World || !Pawn)
		{
			Test.AddError(FString::Printf(TEXT("%s: PIE world or player pawn is gone during the walk."), *Path.Label));
			State.bIsAborted = true;
			return true;
		}

		SampleRealmContinuity(State.Continuity, *World, *Pawn, Path.RealmId, CameraCuts);
		ReportRealmWalkProgressPeriodically(Test, State, Path);
		const double EndDistanceCm = State.bIsForward ? Path.GetLengthCm() : 0.0;
		if (FMath::IsNearlyEqual(State.DistanceCm, EndDistanceCm))
		{
			return true;
		}

		const double NowSeconds = World->GetTimeSeconds();
		const double StepSeconds = FMath::Clamp(NowSeconds - State.LastStepWorldSeconds, 0.0, TDRealmWalkMaxStepSeconds);
		State.LastStepWorldSeconds = NowSeconds;
		const double DirectionSign = State.bIsForward ? 1.0 : -1.0;
		const double NextDistanceCm = FMath::Clamp(State.DistanceCm + DirectionSign * TDRealmWalkSpeedCmPerSecond * StepSeconds, 0.0, Path.GetLengthCm());
		const FVector NextPointCm = Path.GetPointCm(NextDistanceCm);
		const FRotator WalkRotation = (Path.GetDirection() * DirectionSign).Rotation();
		double GroundZ = State.GroundZCm;
		if (!TryTraceWalkGroundZ(*World, *Pawn, FVector(NextPointCm.X, NextPointCm.Y, State.GroundZCm), TDRealmWalkTraceAboveCm, TDRealmWalkTraceBelowCm, GroundZ))
		{
			++State.MissingGroundFrameCount;
			UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, MakeWalkStandingCm(*Pawn, Path.GetPointCm(State.DistanceCm), State.GroundZCm), WalkRotation);
			return false;
		}

		State.DistanceCm = NextDistanceCm;
		State.GroundZCm = GroundZ;
		UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, MakeWalkStandingCm(*Pawn, NextPointCm, GroundZ), WalkRotation);
		return false;
	}

	double MeasureConnectorAlongCm(const FTDRealmGateTravelCase& Case, const FVector& LocationCm)
	{
		return FVector::DotProduct(FVector(LocationCm.X - Case.BoundaryCm.X, LocationCm.Y - Case.BoundaryCm.Y, 0.0), Case.PassageDirection);
	}

	FTDRealmWalkPath MakeConnectorWalkPath(const FTDRealmGateTravelCase& Case)
	{
		FTDRealmWalkPath Path;
		Path.Label = FString::Printf(TEXT("Gate '%s' connector walk"), *Case.GateId.ToString());
		Path.RealmId = Case.RealmId;
		const double StartAlongCm = -(Case.ApproachLengthCm + Case.PreloadLeadCm + TDRealmWalkStartBeforeWindowCm);
		const FVector StartCm = Case.BoundaryCm + Case.PassageDirection * StartAlongCm;
		const FVector EndCm = Case.BoundaryCm + Case.PassageDirection * Case.DepartureLengthCm;
		Path.StartCm = FVector(StartCm.X, StartCm.Y, Case.FieldReturnCm.Z);
		Path.EndCm = FVector(EndCm.X, EndCm.Y, Case.ArrivalCm.Z);
		return Path;
	}

	bool TryGetGameplayLayerState(UWorld& World, const FTDRealmGateTravelCase& Case, EDataLayerRuntimeState& OutState)
	{
		const UDataLayerAsset* LayerAsset = Case.GameplayDataLayer.IsNull() ? nullptr : Case.GameplayDataLayer.LoadSynchronous();
		const UDataLayerManager* LayerManager = UDataLayerManager::GetDataLayerManager(&World);
		const UDataLayerInstance* LayerInstance = LayerAsset && LayerManager ? LayerManager->GetDataLayerInstanceFromAsset(LayerAsset) : nullptr;
		if (!LayerInstance)
		{
			return false;
		}
		OutState = LayerManager->GetDataLayerInstanceRuntimeState(LayerInstance);
		return true;
	}

	bool BeginConnectorObservation(FAutomationTestBase& Test, FTDRealmConnectorObservation& Observation, const FTDRealmGateTravelCase& Case)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(World);
		if (!World || !Test.TestNotNull(TEXT("PIE world has a realm subsystem"), Realm) || !Test.TestNotNull(TEXT("PIE world has a seamless travel subsystem"), UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(World)))
		{
			return false;
		}

		Observation = FTDRealmConnectorObservation();
		Observation.InitialPacingCount = Realm->GetPassagePacingEngagedCount();
		EDataLayerRuntimeState LayerState = EDataLayerRuntimeState::Unloaded;
		Observation.bHasGameplayLayer = Test.TestTrue(FString::Printf(TEXT("Gate '%s': gameplay data layer '%s' of realm '%s' resolves to a data layer instance in the PIE world"),
			*Case.GateId.ToString(), *Case.GameplayDataLayer.ToSoftObjectPath().ToString(), *Case.RealmId.ToString()), TryGetGameplayLayerState(*World, Case, LayerState));
		Observation.InitialLayerState = LayerState;
		Observation.LastLayerState = LayerState;
		return true;
	}

	void BeginConnectorPhase(FTDRealmConnectorObservation& Observation, bool bIsWalkingIntoRealm)
	{
		Observation.bIsWalkingIntoRealm = bIsWalkingIntoRealm;
		Observation.PhaseStartWorldSeconds = -1.0;
		Observation.ReadyWorldSeconds = -1.0;
		Observation.bHasPreviousSample = false;
		Observation.BoundaryCrossingCount = 0;
		Observation.BoundaryAlongCm = 0.0;
		Observation.BoundaryWeight = -1.0f;
	}

	void RecordConnectorLayerState(FTDRealmConnectorObservation& Observation, const FTDRealmGateTravelCase& Case, UWorld& World)
	{
		EDataLayerRuntimeState LayerState = EDataLayerRuntimeState::Unloaded;
		if (!Observation.bHasGameplayLayer || !TryGetGameplayLayerState(World, Case, LayerState))
		{
			return;
		}

		const bool bHasActivated = LayerState == EDataLayerRuntimeState::Activated && Observation.LastLayerState != EDataLayerRuntimeState::Activated;
		Observation.LayerActivationCount += bHasActivated ? 1 : 0;
		Observation.LastLayerState = LayerState;
	}

	void ObserveConnectorWalk(FTDRealmConnectorObservation& Observation, const FTDRealmGateTravelCase& Case)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(World);
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(World);
		if (!World || !Pawn || !Travel || !Realm)
		{
			return;
		}

		const double AlongCm = MeasureConnectorAlongCm(Case, Pawn->GetActorLocation());
		const float Weight = Realm->GetRealmWeight(Case.RealmId);
		const double NowSeconds = World->GetTimeSeconds();
		const bool bHasEnteredConnector = !Observation.bIsWalkingIntoRealm || AlongCm >= -(Case.ApproachLengthCm + Case.PreloadLeadCm);
		if (Observation.PhaseStartWorldSeconds < 0.0 && bHasEnteredConnector)
		{
			Observation.PhaseStartWorldSeconds = NowSeconds;
		}
		if (Observation.PhaseStartWorldSeconds >= 0.0 && Observation.ReadyWorldSeconds < 0.0 && Travel->IsReadyFor(Case.GateId, Observation.bIsWalkingIntoRealm))
		{
			Observation.ReadyWorldSeconds = NowSeconds;
		}
		if (Observation.bHasPreviousSample && (Observation.PreviousAlongCm < 0.0) != (AlongCm < 0.0))
		{
			const bool bIsPreviousNearer = FMath::Abs(Observation.PreviousAlongCm) < FMath::Abs(AlongCm);
			++Observation.BoundaryCrossingCount;
			Observation.BoundaryAlongCm = bIsPreviousNearer ? Observation.PreviousAlongCm : AlongCm;
			Observation.BoundaryWeight = bIsPreviousNearer ? Observation.PreviousWeight : Weight;
		}

		Observation.bHasPreviousSample = true;
		Observation.PreviousAlongCm = AlongCm;
		Observation.PreviousWeight = Weight;
		RecordConnectorLayerState(Observation, Case, *World);
	}

	bool TickConnectorWalk(FAutomationTestBase& Test, FTDRealmConnectorWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		if (State.Walk.bIsAborted)
		{
			return true;
		}
		ObserveConnectorWalk(State.Observation, Case);
		return TickRealmWalk(Test, State.Walk, State.Path, State.CameraCuts);
	}

	void CancelLeftoverPreload(FAutomationTestBase& Test, const FTDRealmGateTravelCase& Case)
	{
		UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(TDPieTestUtils::GetPieWorld());
		if (!Travel || !Travel->IsUnrequestedPreload())
		{
			return;
		}

		Test.AddInfo(FString::Printf(TEXT("Gate '%s': cancelling leftover preload of '%s' before the walk."), *Case.GateId.ToString(), *Travel->GetActiveDungeonId().ToString()));
		Travel->CancelTravel();
	}

	bool BeginConnectorWalk(FAutomationTestBase& Test, FTDRealmConnectorWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		CancelLeftoverPreload(Test, Case);
		if (!BeginConnectorObservation(Test, State.Observation, Case) || !BeginTravelCommitWatch(Test, State.TravelCommits))
		{
			return false;
		}

		State.CameraCuts.Start();
		BeginConnectorPhase(State.Observation, true);
		BeginRealmWalkPhase(State.Walk, true, State.CameraCuts);
		Test.AddInfo(FString::Printf(TEXT("%s start: boundary %s, approach %.0f cm, departure %.0f cm, preload lead %.0f cm, gameplay layer %s; %s."),
			*State.Path.Label, *Case.BoundaryCm.ToCompactString(), Case.ApproachLengthCm, Case.DepartureLengthCm, Case.PreloadLeadCm,
			GetDataLayerRuntimeStateName(State.Observation.InitialLayerState), *DescribeRealmWalk(State.Walk, State.Path)));
		return true;
	}

	void TestConnectorWalkEnd(FAutomationTestBase& Test, const FTDRealmConnectorWalkState& State, const FTDRealmGateTravelCase& Case, const FString& Prefix)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(World);
		if (!Test.TestNotNull(FString::Printf(TEXT("%s: player pawn exists at the walk end"), *Prefix), Pawn) || !Realm)
		{
			return;
		}

		const FVector PawnCm = Pawn->GetActorLocation();
		const float Weight = Realm->GetRealmWeight(Case.RealmId);
		if (State.Observation.bIsWalkingIntoRealm)
		{
			Test.TestTrue(FString::Printf(TEXT("%s: pawn %s is inside realm '%s' at the arrival"), *Prefix, *PawnCm.ToCompactString(), *Case.RealmId.ToString()), Case.RealmBoundsCm.IsInsideOrOnXY(PawnCm));
			Test.TestTrue(FString::Printf(TEXT("%s: realm weight %.3f at the arrival is at least %.2f"), *Prefix, Weight, TDRealmConnectorRealmEndMinWeight), Weight >= TDRealmConnectorRealmEndMinWeight);
			return;
		}

		Test.TestTrue(FString::Printf(TEXT("%s: pawn %s is inside the field and outside every realm at the field road"), *Prefix, *PawnCm.ToCompactString()),
			IsInsideFieldXY(PawnCm) && !IsInsideAnyRealmXY(State.AllRealmBoundsCm, PawnCm));
		Test.TestTrue(FString::Printf(TEXT("%s: realm weight %.3f back on the field road is at most %.2f"), *Prefix, Weight, TDRealmConnectorFieldEndMaxWeight), Weight >= 0.0f && Weight <= TDRealmConnectorFieldEndMaxWeight);
	}

	void FinishConnectorPhase(FAutomationTestBase& Test, const FTDRealmConnectorWalkState& State, const FTDRealmGateTravelCase& Case)
	{
		const FTDRealmConnectorObservation& Observation = State.Observation;
		const FString Prefix = FString::Printf(TEXT("Gate '%s' %s"), *Case.GateId.ToString(), Observation.bIsWalkingIntoRealm ? TEXT("walk into the realm") : TEXT("walk back to the field"));
		const bool bWasReady = Observation.PhaseStartWorldSeconds >= 0.0 && Observation.ReadyWorldSeconds >= 0.0;
		const double ReadinessSeconds = bWasReady ? Observation.ReadyWorldSeconds - Observation.PhaseStartWorldSeconds : -1.0;
		const FTDRealmContinuityMetrics& Continuity = State.Walk.Continuity;
		Test.AddInfo(FString::Printf(TEXT("Measured %s: readiness %.2f s after entering the connector, max pawn step %.1f cm, max camera step %.1f cm, max realm weight step %.4f, boundary weight %.3f at %.1f cm (%d crossings), %d frames without ground."),
			*Prefix, ReadinessSeconds, Continuity.MaxPawnStepCm, Continuity.MaxCameraStepCm, Continuity.MaxWeightStep, Observation.BoundaryWeight, Observation.BoundaryAlongCm,
			Observation.BoundaryCrossingCount, State.Walk.MissingGroundFrameCount));

		TestRealmContinuity(Test, Prefix, Continuity);
		Test.TestEqual(FString::Printf(TEXT("%s: frames without walkable ground ahead"), *Prefix), State.Walk.MissingGroundFrameCount, 0);
		Test.TestTrue(FString::Printf(TEXT("%s: destination became ready %.2f s after entering the connector (<= %.1f)"), *Prefix, ReadinessSeconds, TDRealmConnectorMaxReadinessSeconds),
			bWasReady && ReadinessSeconds <= TDRealmConnectorMaxReadinessSeconds);
		Test.TestEqual(FString::Printf(TEXT("%s: boundary crossings"), *Prefix), Observation.BoundaryCrossingCount, 1);
		Test.TestTrue(FString::Printf(TEXT("%s: realm weight %.3f at the boundary (%.1f cm from it) is %.2f +- %.2f"), *Prefix, Observation.BoundaryWeight, Observation.BoundaryAlongCm, TDRealmConnectorBoundaryWeight, TDRealmConnectorBoundaryWeightTolerance),
			FMath::Abs(Observation.BoundaryWeight - TDRealmConnectorBoundaryWeight) <= TDRealmConnectorBoundaryWeightTolerance);
		TestConnectorWalkEnd(Test, State, Case, Prefix);
	}

	void FinishConnectorObservation(FAutomationTestBase& Test, const FTDRealmConnectorObservation& Observation, const FTDRealmGateTravelCase& Case)
	{
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(TDPieTestUtils::GetPieWorld());
		const int32 PacingCount = Realm ? Realm->GetPassagePacingEngagedCount() - Observation.InitialPacingCount : -1;
		Test.AddInfo(FString::Printf(TEXT("Measured gate '%s' round trip: pacing engaged %d times, gameplay layer %s -> %s with %d activations."),
			*Case.GateId.ToString(), PacingCount, GetDataLayerRuntimeStateName(Observation.InitialLayerState), GetDataLayerRuntimeStateName(Observation.LastLayerState), Observation.LayerActivationCount));
		Test.TestEqual(FString::Printf(TEXT("Gate '%s': passage pacing engagements with default settings"), *Case.GateId.ToString()), PacingCount, 0);
		if (!Observation.bHasGameplayLayer)
		{
			return;
		}

		Test.TestTrue(FString::Printf(TEXT("Gate '%s': gameplay data layer starts Loaded (was %s)"), *Case.GateId.ToString(), GetDataLayerRuntimeStateName(Observation.InitialLayerState)),
			Observation.InitialLayerState == EDataLayerRuntimeState::Loaded);
		Test.TestEqual(FString::Printf(TEXT("Gate '%s': gameplay data layer activations during the round trip"), *Case.GateId.ToString()), Observation.LayerActivationCount, 1);
		Test.TestTrue(FString::Printf(TEXT("Gate '%s': gameplay data layer stays Activated after the round trip (is %s)"), *Case.GateId.ToString(), GetDataLayerRuntimeStateName(Observation.LastLayerState)),
			Observation.LastLayerState == EDataLayerRuntimeState::Activated);
	}

	void EnqueueConnectorWalk(FAutomationTestBase& Test, const FTDRealmGateTravelCase& Case, const TArray<FBox>& AllRealmBoundsCm)
	{
		using namespace TDPieTestUtils;
		TSharedRef<FTDRealmConnectorWalkState> State = MakeShared<FTDRealmConnectorWalkState>();
		State->Path = MakeConnectorWalkPath(Case);
		State->AllRealmBoundsCm = AllRealmBoundsCm;
		const auto Abort = [State]() { State->Walk.bIsAborted = true; };
		const auto ReportAndAbort = [&Test, State]()
		{
			Test.AddInfo(FString::Printf(TEXT("%s state at the timeout: %s."), *State->Path.Label, *DescribeRealmWalk(State->Walk, State->Path)));
			State->Walk.bIsAborted = true;
		};

		EnqueueStep([&Test, State]()
		{
			State->Walk.bIsAborted = !PlacePawnAboveWalkStart(Test, State->Walk, State->Path);
			return true;
		});
		EnqueueWaitUntil(Test, FString::Printf(TEXT("Field road of gate '%s' streams in"), *Case.GateId.ToString()), [State]()
		{
			return State->Walk.bIsAborted || HoldPawnAboveWalkStartUntilLoaded(State->Walk, State->Path);
		}, TDRealmWalkStreamingTimeoutSeconds, Abort);
		EnqueueWaitUntil(Test, FString::Printf(TEXT("Pawn settles on the field road of gate '%s'"), *Case.GateId.ToString()), [State]()
		{
			return State->Walk.bIsAborted || HasSettledOnWalkStart(State->Walk);
		}, TDRealmWalkStreamingTimeoutSeconds, Abort);
		EnqueueStep([&Test, State, Case]()
		{
			if (!State->Walk.bIsAborted)
			{
				State->Walk.bIsAborted = !BeginConnectorWalk(Test, *State, Case);
			}
			return true;
		});
		EnqueueWaitUntil(Test, FString::Printf(TEXT("Walk through connector '%s' into realm '%s'"), *Case.GateId.ToString(), *Case.RealmId.ToString()), [&Test, State, Case]()
		{
			return TickConnectorWalk(Test, *State, Case);
		}, TDRealmWalkTimeoutSeconds, ReportAndAbort);
		EnqueueStep([&Test, State, Case]()
		{
			if (!State->Walk.bIsAborted)
			{
				FinishConnectorPhase(Test, *State, Case);
				BeginConnectorPhase(State->Observation, false);
				BeginRealmWalkPhase(State->Walk, false, State->CameraCuts);
			}
			return true;
		});
		EnqueueWaitUntil(Test, FString::Printf(TEXT("Walk back through connector '%s' to the field road"), *Case.GateId.ToString()), [&Test, State, Case]()
		{
			return TickConnectorWalk(Test, *State, Case);
		}, TDRealmWalkTimeoutSeconds, ReportAndAbort);
		EnqueueStep([&Test, State, Case]()
		{
			if (!State->Walk.bIsAborted)
			{
				FinishConnectorPhase(Test, *State, Case);
				FinishConnectorObservation(Test, State->Observation, Case);
				TestNoTravelCommit(Test, State->TravelCommits, FString::Printf(TEXT("Gate '%s' connector round trip"), *Case.GateId.ToString()));
			}
			StopTravelCommitWatch(State->TravelCommits);
			State->CameraCuts.Stop();
			return true;
		});
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
	if (!TestRealmGateCasesLoaded(*this, Cases, State->AllRealmBoundsCm, ConfiguredRealmPaths))
	{
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
			if (!Case.bIsLift)
			{
				TestTrue(FString::Printf(TEXT("Gate '%s': pawn XY is inside realm '%s' bounds %s"), *Case.GateId.ToString(), *Case.RealmId.ToString(), *Case.RealmBoundsCm.ToString()), Case.RealmBoundsCm.IsInsideOrOnXY(PawnLocation));
			}
			TestSunIntensityBetween(*this, World, FString::Printf(TEXT("Gate '%s': sun intensity lies between the field and realm values"), *Case.GateId.ToString()), Realm->GetFieldAtmosphere().SunIntensityLux, Case.RealmSunIntensityLux);
			TestPawnAtRealmLiftExit(*this, *Pawn, Case, Case.RealmLiftExitCm, TEXT("realm"));

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
			TestTrue(FString::Printf(TEXT("Gate '%s': pawn XY is inside the field (|X|,|Y| <= %.0f)"), *Case.GateId.ToString(), TDRealmPieFieldHalfExtentCm), IsInsideFieldXY(PawnLocation));
			TestSunIntensityBetween(*this, World, FString::Printf(TEXT("Gate '%s': sun intensity after return lies between the field and realm values"), *Case.GateId.ToString()), Realm->GetFieldAtmosphere().SunIntensityLux, Case.RealmSunIntensityLux);
			TestPawnAtRealmLiftExit(*this, *Pawn, Case, Case.FieldLiftExitCm, TEXT("field"));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmConnectorWalkPieTest, "TDGame.SeamlessTravel.RealmConnectorWalk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDRealmConnectorWalkPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	TArray<FBox> AllRealmBoundsCm;
	FString ConfiguredRealmPaths;
	const TArray<FTDRealmGateTravelCase> Cases = CollectRealmGateTravelCases(AllRealmBoundsCm, ConfiguredRealmPaths);
	if (!TestRealmGateCasesLoaded(*this, Cases, AllRealmBoundsCm, ConfiguredRealmPaths))
	{
		return false;
	}
	if (!TestContinuousRealmLayout(*this, AllRealmBoundsCm))
	{
		return false;
	}

	const TArray<FTDRealmGateTravelCase> ConnectorCases = Cases.FilterByPredicate([](const FTDRealmGateTravelCase& Case) { return !Case.bIsLift; });
	if (!TestTrue(TEXT("At least one realm gate is a walking connector"), !ConnectorCases.IsEmpty()))
	{
		return false;
	}

	if (!OpenMapAndStartPie(*this, OpenWorldMapPackagePath))
	{
		return false;
	}

	EnqueueWaitSeconds(TDRealmPieWarmupSeconds);
	for (const FTDRealmGateTravelCase& Case : ConnectorCases)
	{
		EnqueueConnectorWalk(*this, Case, AllRealmBoundsCm);
	}

	EnqueueEndPie();
	return true;
}

namespace
{
	const TCHAR* TDRealmLiftPieDelayVariableName = TEXT("td.Realm.SimulatedPreloadDelaySeconds");
	constexpr float TDRealmLiftPieTreadmillDelaySeconds = 15.0f;
	constexpr double TDRealmLiftPieTimeoutSeconds = 120.0;
	constexpr double TDRealmLiftPieMinimumSettleSeconds = 3.0;
	constexpr double TDRealmLiftPieSpawnClearanceCm = 5.0;
	constexpr double TDRealmLiftPieMinDeckFloorGapCm = 2.0;
	constexpr double TDRealmLiftPieMaxLiftStepSeconds = 0.05;
	constexpr double TDRealmLiftPieMaxDeckFrameStepCm = 100.0;
	constexpr double TDRealmLiftPieLandingToleranceCm = 10.0;
	constexpr double TDRealmLiftPieExpectedRideSeconds = 16.7;
	constexpr double TDRealmLiftPieRideSecondsTolerance = 0.2;
	constexpr double TDRealmLiftPieMaxReadinessSeconds = 1.0;
	constexpr double TDRealmLiftPieShaftMidDepthCm = 2000.0;
	constexpr float TDRealmLiftPieShaftMidWeight = 0.5f;
	constexpr float TDRealmLiftPieShaftMidWeightTolerance = 0.02f;
	constexpr double TDRealmLiftPieHoldDepthToleranceCm = 0.5;
	constexpr double TDRealmLiftPieStoppedDriftToleranceCm = 0.01;
	constexpr double TDRealmLiftPieInvariantToleranceCmPerSecond = 1.0;
	constexpr double TDRealmLiftPieModuleAlignmentToleranceCm = 0.5;
	constexpr double TDRealmLiftPieMaxBoundaryWaitSeconds = 1.5;
	constexpr double TDRealmLiftPieLinerSearchRadiusCm = 100.0;
	constexpr double TDRealmLiftPieSlotEndRadiusCm = 300.0;
	constexpr float TDRealmLiftPieSlotEndMinWeight = 0.99f;

	struct FTDRealmLiftRideMetrics
	{
		bool bHasPreviousSample = false;
		double PreviousWorldSeconds = 0.0;
		FVector PreviousDeckCm = FVector::ZeroVector;
		ETDRealmLiftState PreviousLiftState = ETDRealmLiftState::Resting;
		bool bWasPreviousStopped = false;
		double PreviousPositionCm = 0.0;
		float PreviousWeight = 0.0f;
		TArray<double> PreviousRingDepthsCm;
		int32 CarriedFrameCount = 0;
		int32 AirborneFrameCount = 0;
		int32 OffBaseFrameCount = 0;
		bool bWasBasedAtRideStart = false;
		double MinFootAboveDeckCm = TNumericLimits<double>::Max();
		bool bHasLostRider = false;
		double LostRiderPositionCm = -1.0;
		int32 DeckJumpCount = 0;
		double MaxDeckStepCm = 0.0;
		bool bHasStartedRide = false;
		double RideLiftSeconds = 0.0;
		double RideStartWorldSeconds = 0.0;
		double RideEndWorldSeconds = -1.0;
		int32 ClampedFrameCount = 0;
		double ReadyDelaySeconds = -1.0;
		double MidDepthPositionCm = -1.0;
		float MidDepthWeight = -1.0f;
		FTDRealmContinuityMetrics Continuity;
		int32 TreadmillEntryCount = 0;
		bool bWasReadyAtTreadmillEntry = false;
		double TreadmillLiftSeconds = 0.0;
		int32 StoppedFrameCount = 0;
		double MaxStoppedDepthErrorCm = 0.0;
		double MaxStoppedDeckDriftCm = 0.0;
		int32 InvariantFrameCount = 0;
		double MaxInvariantErrorCmPerSecond = 0.0;
		double MaxReportedInvariantErrorCmPerSecond = 0.0;
		double ResumeArmedLiftSeconds = -1.0;
		double RingResumeLiftSeconds = -1.0;
		bool bWasReadyLatchedAtResume = false;
		double ResumePhaseErrorCm = -1.0;
		double ResumeRingAlignmentErrorCm = -1.0;
	};

	struct FTDRealmLiftPieState
	{
		bool bIsAborted = false;
		TWeakObjectPtr<ATDRealmLiftDeck> Lift;
		TWeakObjectPtr<ATDRealmShaftLiner> Liner;
		double SettleStartWorldSeconds = 0.0;
		FTDRealmLiftRideMetrics Ride;
		FTDRealmWalkPath SlotPath;
		FTDRealmWalkState SlotWalk;
		FTDGameCameraCutCounter CameraCuts;
		FTDRealmTravelCommitWatch TravelCommits;
	};

	TArray<FTDRealmGateTravelCase> CollectRealmLiftPieCases()
	{
		TArray<FBox> AllRealmBoundsCm;
		FString ConfiguredRealmPaths;
		return CollectRealmGateTravelCases(AllRealmBoundsCm, ConfiguredRealmPaths).FilterByPredicate([](const FTDRealmGateTravelCase& Case) { return Case.bIsLift; });
	}

	bool TestRealmLiftPieCasesReady(FAutomationTestBase& Test, const TArray<FTDRealmGateTravelCase>& Cases)
	{
		if (Cases.IsEmpty())
		{
			Test.AddError(TEXT("No realm definition in UTDWorldGenSettings.RealmDefinitions has a gate with GateKind Lift, but the continuous-world layout (D-45) baked by S4 has the southeast lift. Rebake the lift gate with Tools/WorldGen/editor_build_realm.py, then run this test again."));
			return false;
		}

		TArray<FBox> AllRealmBoundsCm;
		FString ConfiguredRealmPaths;
		CollectRealmGateTravelCases(AllRealmBoundsCm, ConfiguredRealmPaths);
		return TestContinuousRealmLayout(Test, AllRealmBoundsCm);
	}

	ATDRealmShaftLiner* FindRealmShaftLiner(UWorld& World, const FTDRealmGateTravelCase& Case)
	{
		for (TActorIterator<ATDRealmShaftLiner> It(&World); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), Case.FieldLandingCm) <= TDRealmLiftPieLinerSearchRadiusCm)
			{
				return *It;
			}
		}
		return nullptr;
	}

	bool BindRealmLiftPieState(FAutomationTestBase& Test, FTDRealmLiftPieState& State, const FTDRealmGateTravelCase& Case)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const UTDRealmSubsystem* RealmSubsystem = UTDRealmSubsystem::GetRealmSubsystem(World);
		ATDRealmLiftDeck* Lift = RealmSubsystem ? RealmSubsystem->FindRealmLiftDeck(Case.GateId) : nullptr;
		if (!World || !Test.TestNotNull(FString::Printf(TEXT("Gate '%s': the realm subsystem spawned a lift"), *Case.GateId.ToString()), Lift)
			|| !Test.TestNotNull(TEXT("PIE world has a player pawn"), TDPieTestUtils::FindPlayerPawn(World)))
		{
			return false;
		}

		State.Lift = Lift;
		State.Liner = FindRealmShaftLiner(*World, Case);
		State.CameraCuts.Start();
		return BeginTravelCommitWatch(Test, State.TravelCommits);
	}

	void FinishRealmLiftPieCase(FAutomationTestBase& Test, FTDRealmLiftPieState& State, const FTDRealmGateTravelCase& Case, const TCHAR* CaseName)
	{
		if (!State.bIsAborted)
		{
			TestNoTravelCommit(Test, State.TravelCommits, FString::Printf(TEXT("Gate '%s' %s"), *Case.GateId.ToString(), CaseName));
		}
		StopTravelCommitWatch(State.TravelCommits);
		State.CameraCuts.Stop();
	}

	TArray<double> ReadRingDepthsCm(const ATDRealmShaftLiner* Liner)
	{
		TArray<double> DepthsCm;
		if (!Liner)
		{
			return DepthsCm;
		}
		for (int32 RingIndex = 0; RingIndex < Liner->GetRingCount(); ++RingIndex)
		{
			DepthsCm.Add(Liner->GetRingTopDepthCm(RingIndex));
		}
		return DepthsCm;
	}

	double MeasureModuleAlignmentErrorCm(double OffsetCm, double ModuleHeightCm)
	{
		if (ModuleHeightCm <= 0.0)
		{
			return 0.0;
		}
		return FMath::Abs(OffsetCm - FMath::RoundToDouble(OffsetCm / ModuleHeightCm) * ModuleHeightCm);
	}

	bool TryMeasureRingProgressCm(const TArray<double>& PreviousDepthsCm, const TArray<double>& DepthsCm, double ModuleHeightCm, bool bIsDescending, double& OutProgressCm)
	{
		if (DepthsCm.IsEmpty() || PreviousDepthsCm.Num() != DepthsCm.Num())
		{
			return false;
		}

		double DeltaSumCm = 0.0;
		int32 DeltaCount = 0;
		for (int32 RingIndex = 0; RingIndex < DepthsCm.Num(); ++RingIndex)
		{
			const double DeltaCm = DepthsCm[RingIndex] - PreviousDepthsCm[RingIndex];
			if (FMath::Abs(DeltaCm) >= ModuleHeightCm * 0.5)
			{
				continue;
			}
			DeltaSumCm += DeltaCm;
			++DeltaCount;
		}
		if (DeltaCount == 0)
		{
			return false;
		}

		OutProgressCm = (bIsDescending ? -1.0 : 1.0) * DeltaSumCm / DeltaCount;
		return true;
	}

	bool PlacePawnAtRealmLiftExit(FAutomationTestBase& Test, FTDRealmLiftPieState& State, const FVector& ExitCm)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		if (!World || !Pawn)
		{
			Test.AddError(TEXT("PIE world or player pawn is gone before moving to a lift landing exit."));
			return false;
		}

		State.SettleStartWorldSeconds = World->GetTimeSeconds();
		const FVector StandCm = ExitCm + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + TDRealmLiftPieSpawnClearanceCm);
		return Test.TestTrue(FString::Printf(TEXT("pawn moved to the lift landing exit %s"), *ExitCm.ToCompactString()),
			UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, StandCm, Pawn->GetActorRotation()));
	}

	bool HoldPawnAtRealmLiftExitUntilReady(const FTDRealmLiftPieState& State, const FVector& ExitCm, bool bAtRealmLanding)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const ATDRealmLiftDeck* Lift = State.Lift.Get();
		if (!World || !Pawn || !Lift)
		{
			return false;
		}

		const FTDRealmLiftMotion& Motion = Lift->GetMotion();
		const bool bIsLiftWaiting = Motion.State == ETDRealmLiftState::Resting && (bAtRealmLanding ? Motion.IsAtRealmLanding() : Motion.IsAtFieldLanding());
		const UWorldPartitionSubsystem* WorldPartition = World->GetSubsystem<UWorldPartitionSubsystem>();
		const bool bIsStreamingCompleted = !WorldPartition || WorldPartition->IsStreamingCompleted();
		const bool bHasHeldLongEnough = World->GetTimeSeconds() - State.SettleStartWorldSeconds >= TDRealmLiftPieMinimumSettleSeconds;
		double GroundZ = 0.0;
		if (bIsLiftWaiting && bIsStreamingCompleted && bHasHeldLongEnough && TryTraceWalkGroundZ(*World, *Pawn, ExitCm, TDRealmWalkTraceAboveCm, TDRealmWalkTraceBelowCm, GroundZ))
		{
			return true;
		}

		const FVector StandCm = ExitCm + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + TDRealmLiftPieSpawnClearanceCm);
		UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, StandCm, Pawn->GetActorRotation());
		return false;
	}

	bool PlacePawnOnRealmLiftDeck(FAutomationTestBase& Test, FTDRealmLiftPieState& State)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const ATDRealmLiftDeck* Lift = State.Lift.Get();
		if (!World || !Pawn || !Lift)
		{
			Test.AddError(TEXT("PIE world, player pawn or lift is gone before boarding."));
			return false;
		}

		State.Ride = FTDRealmLiftRideMetrics();
		BeginRealmContinuity(State.Ride.Continuity, State.CameraCuts);
		const FVector DeckTopCm = Lift->GetDeckTopLocation();
		FCollisionQueryParams FloorQueryParams(SCENE_QUERY_STAT(TDRealmLiftFloor), false, Pawn);
		FloorQueryParams.AddIgnoredActor(Lift);
		FHitResult FloorHit;
		const bool bHasFloorUnderDeck = World->LineTraceSingleByChannel(FloorHit, DeckTopCm + FVector(0.0, 0.0, TDRealmWalkTraceAboveCm), DeckTopCm - FVector(0.0, 0.0, TDRealmWalkTraceBelowCm), ECC_Visibility, FloorQueryParams);
		const double FloorGapCm = bHasFloorUnderDeck ? DeckTopCm.Z - FloorHit.Location.Z : TDRealmWalkTraceBelowCm;
		Test.AddInfo(FString::Printf(TEXT("Lift deck top %s: surface under it without the deck is %s at %.1f cm below the deck top."),
			*DeckTopCm.ToCompactString(), bHasFloorUnderDeck ? *GetNameSafe(FloorHit.GetActor()) : TEXT("none"), FloorGapCm));
		Test.TestTrue(FString::Printf(TEXT("the lift deck top is %.1f cm above any other surface under it (>= %.1f), so the deck is the floor a rider bases on"), FloorGapCm, TDRealmLiftPieMinDeckFloorGapCm),
			FloorGapCm >= TDRealmLiftPieMinDeckFloorGapCm);

		const FVector StandCm = DeckTopCm + FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight() + TDRealmLiftPieSpawnClearanceCm);
		Test.AddInfo(TEXT("Pawn boards with SetActorLocation and no movement teleport, so its movement keeps the floor it found at the landing exit until the deck makes it ride on the deck."));
		return Test.TestTrue(FString::Printf(TEXT("pawn moved onto the lift deck at %s"), *DeckTopCm.ToCompactString()),
			Pawn->SetActorLocation(StandCm, false, nullptr, ETeleportType::TeleportPhysics));
	}

	void SampleRealmLiftClock(FTDRealmLiftRideMetrics& Ride, UWorld& World, const FTDRealmGateTravelCase& Case, const FTDRealmLiftMotion& Motion, double LiftStepSeconds, bool bHasArrived, bool bIsDescending)
	{
		const bool bIsRideFrame = Motion.IsCarryingRider() || (Ride.bHasStartedRide && bHasArrived);
		if (!bIsRideFrame)
		{
			return;
		}

		if (!Ride.bHasStartedRide)
		{
			Ride.bHasStartedRide = true;
			Ride.RideStartWorldSeconds = World.GetTimeSeconds() - World.GetDeltaSeconds();
		}
		const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(&World);
		if (Ride.ReadyDelaySeconds < 0.0 && Travel && Travel->IsReadyFor(Case.GateId, bIsDescending))
		{
			Ride.ReadyDelaySeconds = Ride.RideLiftSeconds;
		}
		Ride.RideLiftSeconds += LiftStepSeconds;
		Ride.ClampedFrameCount += World.GetDeltaSeconds() > TDRealmLiftPieMaxLiftStepSeconds ? 1 : 0;
		if (bHasArrived)
		{
			Ride.RideEndWorldSeconds = World.GetTimeSeconds();
		}
	}

	void SampleRealmLiftRider(FTDRealmLiftRideMetrics& Ride, const ATDRealmLiftDeck& Lift, const APawn& Pawn, bool bHasArrived)
	{
		const FTDRealmLiftMotion& Motion = Lift.GetMotion();
		if (!Motion.IsCarryingRider())
		{
			Ride.bHasLostRider = Ride.bHasLostRider || (Ride.CarriedFrameCount > 0 && !bHasArrived);
			Ride.LostRiderPositionCm = Ride.bHasLostRider && Ride.LostRiderPositionCm < 0.0 ? Motion.PositionCm : Ride.LostRiderPositionCm;
			return;
		}

		const bool bIsBasedOnDeck = Lift.IsPawnBasedOnDeck(Pawn);
		const ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(&Pawn);
		const double FootAboveDeckCm = Pawn.GetActorLocation().Z - Pawn.GetSimpleCollisionHalfHeight() - Lift.GetDeckTopLocation().Z;
		Ride.bWasBasedAtRideStart = Ride.CarriedFrameCount == 0 ? bIsBasedOnDeck : Ride.bWasBasedAtRideStart;
		Ride.MinFootAboveDeckCm = FMath::Min(Ride.MinFootAboveDeckCm, FootAboveDeckCm);
		++Ride.CarriedFrameCount;
		Ride.AirborneFrameCount += CombatCharacter && CombatCharacter->IsAirborne() ? 1 : 0;
		Ride.OffBaseFrameCount += bIsBasedOnDeck ? 0 : 1;
	}

	void SampleRealmLiftDeck(FAutomationTestBase& Test, FTDRealmLiftRideMetrics& Ride, const FTDRealmGateTravelCase& Case, const ATDRealmLiftDeck& Lift)
	{
		if (!Ride.bHasPreviousSample)
		{
			return;
		}

		const FVector DeckCm = Lift.GetActorLocation();
		const double DeckStepCm = FVector::Dist(DeckCm, Ride.PreviousDeckCm);
		Ride.MaxDeckStepCm = FMath::Max(Ride.MaxDeckStepCm, DeckStepCm);
		if (DeckStepCm <= TDRealmLiftPieMaxDeckFrameStepCm)
		{
			return;
		}

		++Ride.DeckJumpCount;
		Test.AddInfo(FString::Printf(TEXT("Gate '%s': deck jumped %.1f cm from %s to %s in one frame."), *Case.GateId.ToString(), DeckStepCm, *Ride.PreviousDeckCm.ToCompactString(), *DeckCm.ToCompactString()));
	}

	void SampleRealmLiftShaftWeight(FTDRealmLiftRideMetrics& Ride, UWorld& World, const FTDRealmGateTravelCase& Case, const FTDRealmLiftMotion& Motion)
	{
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(&World);
		const float Weight = Realm ? Realm->GetRealmWeight(Case.RealmId) : -1.0f;
		const bool bHasCrossedMidDepth = Ride.bHasPreviousSample && Motion.IsCarryingRider()
			&& (Ride.PreviousPositionCm < TDRealmLiftPieShaftMidDepthCm) != (Motion.PositionCm < TDRealmLiftPieShaftMidDepthCm);
		if (bHasCrossedMidDepth)
		{
			const bool bIsPreviousNearer = FMath::Abs(Ride.PreviousPositionCm - TDRealmLiftPieShaftMidDepthCm) < FMath::Abs(Motion.PositionCm - TDRealmLiftPieShaftMidDepthCm);
			Ride.MidDepthPositionCm = bIsPreviousNearer ? Ride.PreviousPositionCm : Motion.PositionCm;
			Ride.MidDepthWeight = bIsPreviousNearer ? Ride.PreviousWeight : Weight;
		}
		Ride.PreviousWeight = Weight;
	}

	bool IsTreadmillStopped(const FTDRealmLiftMotion& Motion)
	{
		return Motion.State == ETDRealmLiftState::Treadmill && Motion.SpeedCmPerSecond <= 0.0 && !Motion.bIsRingResuming;
	}

	void SampleRealmLiftTreadmillStop(FTDRealmLiftRideMetrics& Ride, const FTDRealmGateTravelCase& Case, const FVector& DeckCm, bool bIsDescending)
	{
		const double ProgressCm = bIsDescending ? Case.FieldLandingCm.Z - DeckCm.Z : DeckCm.Z - Case.RealmLandingCm.Z;
		++Ride.StoppedFrameCount;
		Ride.MaxStoppedDepthErrorCm = FMath::Max(Ride.MaxStoppedDepthErrorCm, FMath::Abs(ProgressCm - Case.HoldDepthCm));
		if (Ride.bWasPreviousStopped)
		{
			Ride.MaxStoppedDeckDriftCm = FMath::Max(Ride.MaxStoppedDeckDriftCm, FMath::Abs(DeckCm.Z - Ride.PreviousDeckCm.Z));
		}
	}

	void SampleRealmLiftTreadmillInvariant(FTDRealmLiftRideMetrics& Ride, const FTDRealmGateTravelCase& Case, const FVector& DeckCm, const TArray<double>& RingDepthsCm, double LiftStepSeconds, bool bIsDescending)
	{
		double RingProgressCm = 0.0;
		if (LiftStepSeconds <= 0.0 || !TryMeasureRingProgressCm(Ride.PreviousRingDepthsCm, RingDepthsCm, Case.RingModuleHeightCm, bIsDescending, RingProgressCm))
		{
			return;
		}

		const double DeckProgressCm = (bIsDescending ? 1.0 : -1.0) * (Ride.PreviousDeckCm.Z - DeckCm.Z);
		const double CombinedSpeedCmPerSecond = (DeckProgressCm + RingProgressCm) / LiftStepSeconds;
		++Ride.InvariantFrameCount;
		Ride.MaxInvariantErrorCmPerSecond = FMath::Max(Ride.MaxInvariantErrorCmPerSecond, FMath::Abs(CombinedSpeedCmPerSecond - Case.CruiseSpeedCmPerSecond));
	}

	void SampleRealmLiftTreadmillResume(FTDRealmLiftRideMetrics& Ride, const FTDRealmLiftMotion& Motion)
	{
		if (Motion.bIsResumeArmed && Ride.ResumeArmedLiftSeconds < 0.0)
		{
			Ride.ResumeArmedLiftSeconds = Ride.TreadmillLiftSeconds;
			Ride.bWasReadyLatchedAtResume = Motion.bIsReadyLatched;
		}
		if (Motion.bIsRingResuming && Ride.RingResumeLiftSeconds < 0.0)
		{
			Ride.RingResumeLiftSeconds = Ride.TreadmillLiftSeconds;
		}
	}

	void SampleRealmLiftTreadmillExit(FTDRealmLiftRideMetrics& Ride, const FTDRealmGateTravelCase& Case, const FTDRealmLiftMotion& Motion, const TArray<double>& RingDepthsCm)
	{
		Ride.ResumePhaseErrorCm = MeasureModuleAlignmentErrorCm(Motion.RingPhaseCm, Case.RingModuleHeightCm);
		Ride.ResumeRingAlignmentErrorCm = RingDepthsCm.IsEmpty() ? -1.0 : 0.0;
		for (const double RingDepthCm : RingDepthsCm)
		{
			Ride.ResumeRingAlignmentErrorCm = FMath::Max(Ride.ResumeRingAlignmentErrorCm, MeasureModuleAlignmentErrorCm(RingDepthCm - Case.RingTopDepthCm, Case.RingModuleHeightCm));
		}
	}

	void SampleRealmLiftTreadmill(FTDRealmLiftRideMetrics& Ride, UWorld& World, const FTDRealmGateTravelCase& Case, const ATDRealmLiftDeck& Lift, const TArray<double>& RingDepthsCm, double LiftStepSeconds, bool bIsDescending)
	{
		const FTDRealmLiftMotion& Motion = Lift.GetMotion();
		const FVector DeckCm = Lift.GetActorLocation();
		const bool bIsTreadmill = Motion.State == ETDRealmLiftState::Treadmill;
		const bool bWasTreadmill = Ride.bHasPreviousSample && Ride.PreviousLiftState == ETDRealmLiftState::Treadmill;
		if (bIsTreadmill && !bWasTreadmill)
		{
			const UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(&World);
			++Ride.TreadmillEntryCount;
			Ride.bWasReadyAtTreadmillEntry = Travel && Travel->IsReadyFor(Case.GateId, bIsDescending);
		}
		if (bIsTreadmill)
		{
			Ride.TreadmillLiftSeconds += LiftStepSeconds;
			Ride.MaxReportedInvariantErrorCmPerSecond = FMath::Max(Ride.MaxReportedInvariantErrorCmPerSecond, FMath::Abs(Motion.SpeedCmPerSecond + Motion.RingSpeedCmPerSecond - Case.CruiseSpeedCmPerSecond));
			SampleRealmLiftTreadmillResume(Ride, Motion);
		}
		if (IsTreadmillStopped(Motion))
		{
			SampleRealmLiftTreadmillStop(Ride, Case, DeckCm, bIsDescending);
		}
		if (bIsTreadmill && bWasTreadmill)
		{
			SampleRealmLiftTreadmillInvariant(Ride, Case, DeckCm, RingDepthsCm, LiftStepSeconds, bIsDescending);
		}
		if (!bIsTreadmill && bWasTreadmill)
		{
			SampleRealmLiftTreadmillExit(Ride, Case, Motion, RingDepthsCm);
		}
	}

	bool SampleRealmLiftRide(FAutomationTestBase& Test, FTDRealmLiftPieState& State, const FTDRealmGateTravelCase& Case, bool bIsDescending)
	{
		if (State.bIsAborted)
		{
			return true;
		}

		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const ATDRealmLiftDeck* Lift = State.Lift.Get();
		if (!World || !Pawn || !Lift)
		{
			Test.AddError(FString::Printf(TEXT("Gate '%s': PIE world, player pawn or lift is gone during the ride."), *Case.GateId.ToString()));
			State.bIsAborted = true;
			return true;
		}

		FTDRealmLiftRideMetrics& Ride = State.Ride;
		const double NowSeconds = World->GetTimeSeconds();
		if (Ride.bHasPreviousSample && NowSeconds <= Ride.PreviousWorldSeconds)
		{
			return false;
		}

		const FTDRealmLiftMotion& Motion = Lift->GetMotion();
		const double LiftStepSeconds = FMath::Min(static_cast<double>(World->GetDeltaSeconds()), TDRealmLiftPieMaxLiftStepSeconds);
		const bool bHasArrived = Motion.State == ETDRealmLiftState::Open && (bIsDescending ? Motion.IsAtRealmLanding() : Motion.IsAtFieldLanding());
		const TArray<double> RingDepthsCm = ReadRingDepthsCm(State.Liner.Get());
		SampleRealmContinuity(Ride.Continuity, *World, *Pawn, Case.RealmId, State.CameraCuts);
		SampleRealmLiftClock(Ride, *World, Case, Motion, LiftStepSeconds, bHasArrived, bIsDescending);
		SampleRealmLiftRider(Ride, *Lift, *Pawn, bHasArrived);
		SampleRealmLiftDeck(Test, Ride, Case, *Lift);
		SampleRealmLiftShaftWeight(Ride, *World, Case, Motion);
		SampleRealmLiftTreadmill(Ride, *World, Case, *Lift, RingDepthsCm, LiftStepSeconds, bIsDescending);

		Ride.bHasPreviousSample = true;
		Ride.PreviousWorldSeconds = NowSeconds;
		Ride.PreviousDeckCm = Lift->GetActorLocation();
		Ride.PreviousLiftState = Motion.State;
		Ride.bWasPreviousStopped = IsTreadmillStopped(Motion);
		Ride.PreviousPositionCm = Motion.PositionCm;
		Ride.PreviousRingDepthsCm = RingDepthsCm;
		return bHasArrived || Ride.bHasLostRider;
	}

	FString MakeRealmLiftRidePrefix(const FTDRealmGateTravelCase& Case, bool bIsDescending)
	{
		return FString::Printf(TEXT("Gate '%s' %s"), *Case.GateId.ToString(), bIsDescending ? TEXT("descent") : TEXT("ascent"));
	}

	void FinishRealmLiftRide(FAutomationTestBase& Test, const FTDRealmLiftPieState& State, const FTDRealmGateTravelCase& Case, bool bIsDescending)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const ATDRealmLiftDeck* Lift = State.Lift.Get();
		if (!Pawn || !Lift)
		{
			Test.AddError(FString::Printf(TEXT("Gate '%s': player pawn or lift is gone after the ride."), *Case.GateId.ToString()));
			return;
		}

		const FTDRealmLiftRideMetrics& Ride = State.Ride;
		const FString Prefix = MakeRealmLiftRidePrefix(Case, bIsDescending);
		const FVector LandingCm = bIsDescending ? Case.RealmLandingCm : Case.FieldLandingCm;
		const double LandingErrorCm = Lift->GetDeckTopLocation().Z - LandingCm.Z;
		const double MotionLandingErrorCm = Lift->GetActorLocation().Z - LandingCm.Z;
		const double RideWorldSeconds = Ride.RideEndWorldSeconds >= 0.0 ? Ride.RideEndWorldSeconds - Ride.RideStartWorldSeconds : -1.0;
		Test.AddInfo(FString::Printf(TEXT("Measured %s: ride %.2f s on the lift clock (%.2f s world, %d frames clamped to %.2f s), readiness %.2f s after the ride started, treadmill %d times for %.2f s, max deck step %.1f cm, realm weight %.3f at depth %.1f cm, deck top %.2f cm from the landing (motion %.2f cm); %d carried frames, %d airborne, %d off the deck base; deck top at %s, pawn at %s."),
			*Prefix, Ride.RideLiftSeconds, RideWorldSeconds, Ride.ClampedFrameCount, TDRealmLiftPieMaxLiftStepSeconds, Ride.ReadyDelaySeconds, Ride.TreadmillEntryCount, Ride.TreadmillLiftSeconds,
			Ride.MaxDeckStepCm, Ride.MidDepthWeight, Ride.MidDepthPositionCm, LandingErrorCm, MotionLandingErrorCm, Ride.CarriedFrameCount, Ride.AirborneFrameCount, Ride.OffBaseFrameCount,
			*Lift->GetDeckTopLocation().ToCompactString(), *Pawn->GetActorLocation().ToCompactString()));

		Test.AddInfo(FString::Printf(TEXT("%s rider: based on the deck at the ride start %d, lowest foot height above the deck top %.1f cm, rider lost %d at %.1f cm."),
			*Prefix, Ride.bWasBasedAtRideStart ? 1 : 0, Ride.MinFootAboveDeckCm, Ride.bHasLostRider ? 1 : 0, Ride.LostRiderPositionCm));
		TestRealmContinuity(Test, Prefix, Ride.Continuity);
		Test.TestTrue(FString::Printf(TEXT("%s: the lift carried the pawn"), *Prefix), Ride.CarriedFrameCount > 0);
		Test.TestFalse(FString::Printf(TEXT("%s: the rider stayed on the deck until arrival (lost at %.1f cm, lowest foot height above the deck %.1f cm, based at the ride start %d)"),
			*Prefix, Ride.LostRiderPositionCm, Ride.MinFootAboveDeckCm, Ride.bWasBasedAtRideStart ? 1 : 0), Ride.bHasLostRider);
		Test.TestEqual(FString::Printf(TEXT("%s: deck frame steps over %.0f cm (max %.1f)"), *Prefix, TDRealmLiftPieMaxDeckFrameStepCm, Ride.MaxDeckStepCm), Ride.DeckJumpCount, 0);
		Test.TestEqual(FString::Printf(TEXT("%s: airborne frames while carried"), *Prefix), Ride.AirborneFrameCount, 0);
		Test.TestEqual(FString::Printf(TEXT("%s: frames whose movement base was not the deck"), *Prefix), Ride.OffBaseFrameCount, 0);
		Test.TestTrue(FString::Printf(TEXT("%s: the exit door is open after arrival"), *Prefix), Lift->IsAnyDoorOpen());
		Test.TestTrue(FString::Printf(TEXT("%s: deck top stopped %.2f cm from the landing height (<= %.0f)"), *Prefix, LandingErrorCm, TDRealmLiftPieLandingToleranceCm), FMath::Abs(LandingErrorCm) <= TDRealmLiftPieLandingToleranceCm);
		Test.TestTrue(FString::Printf(TEXT("%s: realm weight %.3f at depth %.1f cm is %.2f +- %.2f"), *Prefix, Ride.MidDepthWeight, Ride.MidDepthPositionCm, TDRealmLiftPieShaftMidWeight, TDRealmLiftPieShaftMidWeightTolerance),
			Ride.MidDepthPositionCm >= 0.0 && FMath::Abs(Ride.MidDepthWeight - TDRealmLiftPieShaftMidWeight) <= TDRealmLiftPieShaftMidWeightTolerance);
	}

	void TestRealmLiftDefaultRide(FAutomationTestBase& Test, const FTDRealmLiftPieState& State, const FTDRealmGateTravelCase& Case, bool bIsDescending)
	{
		const FTDRealmLiftRideMetrics& Ride = State.Ride;
		const FString Prefix = MakeRealmLiftRidePrefix(Case, bIsDescending);
		Test.TestEqual(FString::Printf(TEXT("%s: treadmill engagements with default settings"), *Prefix), Ride.TreadmillEntryCount, 0);
		Test.TestTrue(FString::Printf(TEXT("%s: destination became ready %.2f s after the ride started (<= %.1f)"), *Prefix, Ride.ReadyDelaySeconds, TDRealmLiftPieMaxReadinessSeconds),
			Ride.ReadyDelaySeconds >= 0.0 && Ride.ReadyDelaySeconds <= TDRealmLiftPieMaxReadinessSeconds);
		Test.TestTrue(FString::Printf(TEXT("%s: ride took %.2f s (%.1f +- %.1f)"), *Prefix, Ride.RideLiftSeconds, TDRealmLiftPieExpectedRideSeconds, TDRealmLiftPieRideSecondsTolerance),
			FMath::Abs(Ride.RideLiftSeconds - TDRealmLiftPieExpectedRideSeconds) <= TDRealmLiftPieRideSecondsTolerance);
	}

	void TestRealmLiftTreadmillRide(FAutomationTestBase& Test, const FTDRealmLiftPieState& State, const FTDRealmGateTravelCase& Case)
	{
		const FTDRealmLiftRideMetrics& Ride = State.Ride;
		const FString Prefix = MakeRealmLiftRidePrefix(Case, true);
		const bool bHasResumed = Ride.ResumeArmedLiftSeconds >= 0.0 && Ride.RingResumeLiftSeconds >= 0.0;
		const double BoundaryWaitSeconds = bHasResumed ? Ride.RingResumeLiftSeconds - Ride.ResumeArmedLiftSeconds : -1.0;
		const double BoundaryWaitLimitSeconds = TDRealmLiftPieMaxBoundaryWaitSeconds + TDRealmLiftPieMaxLiftStepSeconds;
		Test.AddInfo(FString::Printf(TEXT("Measured %s treadmill: %d engagements, %.2f s on the treadmill, %d stopped frames (depth error %.3f cm, drift %.4f cm), deck + ring speed error %.3f cm/s over %d frames (motion %.3f cm/s), boundary wait %.2f s, resumed by %s, ring phase error %.3f cm, ring alignment error %.3f cm."),
			*Prefix, Ride.TreadmillEntryCount, Ride.TreadmillLiftSeconds, Ride.StoppedFrameCount, Ride.MaxStoppedDepthErrorCm, Ride.MaxStoppedDeckDriftCm, Ride.MaxInvariantErrorCmPerSecond,
			Ride.InvariantFrameCount, Ride.MaxReportedInvariantErrorCmPerSecond, BoundaryWaitSeconds, Ride.bWasReadyLatchedAtResume ? TEXT("readiness") : TEXT("timeout"),
			Ride.ResumePhaseErrorCm, Ride.ResumeRingAlignmentErrorCm));

		Test.TestNotNull(FString::Printf(TEXT("%s: the shaft liner of the lift exists"), *Prefix), State.Liner.Get());
		Test.TestEqual(FString::Printf(TEXT("%s: treadmill engagements"), *Prefix), Ride.TreadmillEntryCount, 1);
		Test.TestFalse(FString::Printf(TEXT("%s: the realm side was not ready when the treadmill engaged"), *Prefix), Ride.bWasReadyAtTreadmillEntry);
		Test.TestTrue(FString::Printf(TEXT("%s: the deck stood still while the rings scrolled"), *Prefix), Ride.StoppedFrameCount > 0);
		Test.TestTrue(FString::Printf(TEXT("%s: the deck stopped %.3f cm from the hold depth %.1f cm (<= %.1f)"), *Prefix, Ride.MaxStoppedDepthErrorCm, Case.HoldDepthCm, TDRealmLiftPieHoldDepthToleranceCm),
			Ride.MaxStoppedDepthErrorCm <= TDRealmLiftPieHoldDepthToleranceCm);
		Test.TestTrue(FString::Printf(TEXT("%s: the stopped deck drifted %.4f cm per frame (<= %.2f)"), *Prefix, Ride.MaxStoppedDeckDriftCm, TDRealmLiftPieStoppedDriftToleranceCm),
			Ride.MaxStoppedDeckDriftCm <= TDRealmLiftPieStoppedDriftToleranceCm);
		Test.TestTrue(FString::Printf(TEXT("%s: deck + ring speed measured in the world stayed %.1f +- %.1f cm/s (max error %.3f over %d frames)"), *Prefix, Case.CruiseSpeedCmPerSecond, TDRealmLiftPieInvariantToleranceCmPerSecond, Ride.MaxInvariantErrorCmPerSecond, Ride.InvariantFrameCount),
			Ride.InvariantFrameCount > 0 && Ride.MaxInvariantErrorCmPerSecond <= TDRealmLiftPieInvariantToleranceCmPerSecond);
		Test.TestTrue(FString::Printf(TEXT("%s: deck + ring speed reported by the motion stayed within %.1f cm/s (max error %.3f)"), *Prefix, TDRealmLiftPieInvariantToleranceCmPerSecond, Ride.MaxReportedInvariantErrorCmPerSecond),
			Ride.MaxReportedInvariantErrorCmPerSecond <= TDRealmLiftPieInvariantToleranceCmPerSecond);
		Test.TestTrue(FString::Printf(TEXT("%s: the treadmill resumed because the realm side became ready"), *Prefix), Ride.bWasReadyLatchedAtResume);
		Test.TestTrue(FString::Printf(TEXT("%s: the rings waited %.2f s for a module boundary (<= %.2f including one lift step)"), *Prefix, BoundaryWaitSeconds, BoundaryWaitLimitSeconds),
			bHasResumed && BoundaryWaitSeconds <= BoundaryWaitLimitSeconds);
		Test.TestTrue(FString::Printf(TEXT("%s: ring phase %.3f cm off a module boundary when the rings stopped (<= %.1f)"), *Prefix, Ride.ResumePhaseErrorCm, TDRealmLiftPieModuleAlignmentToleranceCm),
			Ride.ResumePhaseErrorCm >= 0.0 && Ride.ResumePhaseErrorCm <= TDRealmLiftPieModuleAlignmentToleranceCm);
		Test.TestTrue(FString::Printf(TEXT("%s: liner rings %.3f cm off the module grid after the treadmill (<= %.1f)"), *Prefix, Ride.ResumeRingAlignmentErrorCm, TDRealmLiftPieModuleAlignmentToleranceCm),
			Ride.ResumeRingAlignmentErrorCm >= 0.0 && Ride.ResumeRingAlignmentErrorCm <= TDRealmLiftPieModuleAlignmentToleranceCm);
	}

	FTDRealmWalkPath MakeLiftSlotWalkPath(const FTDRealmGateTravelCase& Case)
	{
		FTDRealmWalkPath Path;
		Path.Label = FString::Printf(TEXT("Gate '%s' slot walk"), *Case.GateId.ToString());
		Path.RealmId = Case.RealmId;
		Path.StartCm = Case.RealmLiftExitCm;
		Path.EndCm = Case.ArrivalCm;
		return Path;
	}

	bool PlacePawnAtSlotWalkStart(FAutomationTestBase& Test, FTDRealmLiftPieState& State)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		if (!World || !Pawn)
		{
			Test.AddError(TEXT("PIE world or player pawn is gone before the slot walk."));
			return false;
		}

		const FTDRealmWalkPath& Path = State.SlotPath;
		FTDRealmWalkState& Walk = State.SlotWalk;
		Walk = FTDRealmWalkState();
		double GroundZ = 0.0;
		if (!Test.TestTrue(FString::Printf(TEXT("%s: floor found at the realm landing exit %s"), *Path.Label, *Path.StartCm.ToCompactString()),
			TryTraceWalkGroundZ(*World, *Pawn, Path.StartCm, TDRealmWalkTraceAboveCm, TDRealmWalkTraceBelowCm, GroundZ)))
		{
			return false;
		}

		Walk.GroundZCm = GroundZ;
		Walk.DistanceCm = 0.0;
		UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, MakeWalkStandingCm(*Pawn, Path.StartCm, GroundZ), Path.GetDirection().Rotation());
		BeginRealmWalkPhase(Walk, true, State.CameraCuts);
		return true;
	}

	void FinishSlotWalkPhase(FAutomationTestBase& Test, const FTDRealmLiftPieState& State, const FTDRealmGateTravelCase& Case)
	{
		const FTDRealmWalkState& Walk = State.SlotWalk;
		const FString Prefix = FString::Printf(TEXT("%s %s"), *State.SlotPath.Label, Walk.bIsForward ? TEXT("out to the slot end") : TEXT("back to the lift"));
		TestRealmContinuity(Test, Prefix, Walk.Continuity);
		Test.TestEqual(FString::Printf(TEXT("%s: frames without walkable ground ahead"), *Prefix), Walk.MissingGroundFrameCount, 0);
		if (!Walk.bIsForward)
		{
			return;
		}

		UWorld* World = TDPieTestUtils::GetPieWorld();
		const APawn* Pawn = TDPieTestUtils::FindPlayerPawn(World);
		const UTDRealmSubsystem* Realm = UTDRealmSubsystem::GetRealmSubsystem(World);
		if (!Test.TestNotNull(FString::Printf(TEXT("%s: player pawn exists at the slot end"), *Prefix), Pawn) || !Realm)
		{
			return;
		}

		const FVector PawnCm = Pawn->GetActorLocation();
		const double SlotEndDistanceCm = FVector::Dist2D(PawnCm, Case.ArrivalCm);
		const float Weight = Realm->GetRealmWeight(Case.RealmId);
		Test.AddInfo(FString::Printf(TEXT("Measured %s: pawn %.1f cm from the slot end %s, realm weight %.3f."), *Prefix, SlotEndDistanceCm, *Case.ArrivalCm.ToCompactString(), Weight));
		Test.TestTrue(FString::Printf(TEXT("%s: pawn is %.1f cm from the slot end (<= %.0f)"), *Prefix, SlotEndDistanceCm, TDRealmLiftPieSlotEndRadiusCm), SlotEndDistanceCm <= TDRealmLiftPieSlotEndRadiusCm);
		Test.TestTrue(FString::Printf(TEXT("%s: pawn %s is inside realm '%s' at the slot end"), *Prefix, *PawnCm.ToCompactString(), *Case.RealmId.ToString()), Case.RealmBoundsCm.IsInsideOrOnXY(PawnCm));
		Test.TestTrue(FString::Printf(TEXT("%s: realm weight %.3f at the slot end is at least %.2f"), *Prefix, Weight, TDRealmLiftPieSlotEndMinWeight), Weight >= TDRealmLiftPieSlotEndMinWeight);
	}

	void SetRealmLiftPieDelay(float DelaySeconds)
	{
		if (IConsoleVariable* DelayVariable = IConsoleManager::Get().FindConsoleVariable(TDRealmLiftPieDelayVariableName))
		{
			DelayVariable->Set(DelaySeconds, ECVF_SetByCode);
		}
	}

	void EnqueueRealmLiftApproach(FAutomationTestBase& Test, TSharedRef<FTDRealmLiftPieState> State, const FTDRealmGateTravelCase& Case, bool bAtRealmLanding)
	{
		using namespace TDPieTestUtils;
		const FVector ExitCm = bAtRealmLanding ? Case.RealmLiftExitCm : Case.FieldLiftExitCm;
		const auto Abort = [State]() { State->bIsAborted = true; };
		EnqueueStep([&Test, State, ExitCm]()
		{
			if (!State->bIsAborted)
			{
				State->bIsAborted = !PlacePawnAtRealmLiftExit(Test, *State, ExitCm);
			}
			return true;
		});
		EnqueueWaitUntil(Test, FString::Printf(TEXT("Gate '%s': %s landing streams in and the deck rests there"), *Case.GateId.ToString(), bAtRealmLanding ? TEXT("realm") : TEXT("field")), [State, ExitCm, bAtRealmLanding]()
		{
			return State->bIsAborted || HoldPawnAtRealmLiftExitUntilReady(*State, ExitCm, bAtRealmLanding);
		}, TDRealmLiftPieTimeoutSeconds, Abort);
	}

	void EnqueueRealmLiftRide(FAutomationTestBase& Test, TSharedRef<FTDRealmLiftPieState> State, const FTDRealmGateTravelCase& Case, bool bIsDescending, bool bIsTreadmillRide)
	{
		using namespace TDPieTestUtils;
		const auto Abort = [State]() { State->bIsAborted = true; };
		EnqueueStep([&Test, State]()
		{
			if (!State->bIsAborted)
			{
				State->bIsAborted = !PlacePawnOnRealmLiftDeck(Test, *State);
			}
			return true;
		});
		EnqueueWaitUntil(Test, FString::Printf(TEXT("Gate '%s': lift %s and opens"), *Case.GateId.ToString(), bIsDescending ? TEXT("descends into the realm") : TEXT("ascends to the field")), [&Test, State, Case, bIsDescending]()
		{
			return SampleRealmLiftRide(Test, *State, Case, bIsDescending);
		}, TDRealmLiftPieTimeoutSeconds, Abort);
		EnqueueStep([&Test, State, Case, bIsDescending, bIsTreadmillRide]()
		{
			if (State->bIsAborted)
			{
				return true;
			}

			FinishRealmLiftRide(Test, *State, Case, bIsDescending);
			if (bIsTreadmillRide)
			{
				TestRealmLiftTreadmillRide(Test, *State, Case);
				return true;
			}
			TestRealmLiftDefaultRide(Test, *State, Case, bIsDescending);
			return true;
		});
	}

	void EnqueueRealmLiftSlotWalk(FAutomationTestBase& Test, TSharedRef<FTDRealmLiftPieState> State, const FTDRealmGateTravelCase& Case)
	{
		using namespace TDPieTestUtils;
		State->SlotPath = MakeLiftSlotWalkPath(Case);
		const auto Abort = [State]() { State->bIsAborted = true; };
		const auto TickSlotWalk = [&Test, State]()
		{
			const bool bIsDone = State->bIsAborted || TickRealmWalk(Test, State->SlotWalk, State->SlotPath, State->CameraCuts);
			State->bIsAborted = State->bIsAborted || State->SlotWalk.bIsAborted;
			return bIsDone;
		};
		EnqueueStep([&Test, State]()
		{
			if (!State->bIsAborted)
			{
				State->bIsAborted = !PlacePawnAtSlotWalkStart(Test, *State);
			}
			return true;
		});
		EnqueueWaitUntil(Test, FString::Printf(TEXT("Gate '%s': walk out of the shaft bottom to the slot end"), *Case.GateId.ToString()), TickSlotWalk, TDRealmWalkTimeoutSeconds, Abort);
		EnqueueStep([&Test, State, Case]()
		{
			if (!State->bIsAborted)
			{
				FinishSlotWalkPhase(Test, *State, Case);
				BeginRealmWalkPhase(State->SlotWalk, false, State->CameraCuts);
			}
			return true;
		});
		EnqueueWaitUntil(Test, FString::Printf(TEXT("Gate '%s': walk back from the slot end to the lift"), *Case.GateId.ToString()), TickSlotWalk, TDRealmWalkTimeoutSeconds, Abort);
		EnqueueStep([&Test, State, Case]()
		{
			if (!State->bIsAborted)
			{
				FinishSlotWalkPhase(Test, *State, Case);
			}
			return true;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftRidePieTest, "TDGame.SeamlessTravel.RealmLiftRide", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDRealmLiftRidePieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	const TArray<FTDRealmGateTravelCase> Cases = CollectRealmLiftPieCases();
	if (!TestRealmLiftPieCasesReady(*this, Cases))
	{
		return false;
	}

	if (!OpenMapAndStartPie(*this, OpenWorldMapPackagePath))
	{
		return false;
	}

	EnqueueWaitSeconds(TDRealmPieWarmupSeconds);

	for (const FTDRealmGateTravelCase& Case : Cases)
	{
		TSharedRef<FTDRealmLiftPieState> State = MakeShared<FTDRealmLiftPieState>();
		EnqueueStep([this, State, Case]()
		{
			State->bIsAborted = !BindRealmLiftPieState(*this, *State, Case);
			return true;
		});
		EnqueueRealmLiftApproach(*this, State, Case, false);
		EnqueueRealmLiftRide(*this, State, Case, true, false);
		EnqueueRealmLiftSlotWalk(*this, State, Case);
		EnqueueRealmLiftApproach(*this, State, Case, true);
		EnqueueRealmLiftRide(*this, State, Case, false, false);
		EnqueueStep([this, State, Case]()
		{
			FinishRealmLiftPieCase(*this, *State, Case, TEXT("lift round trip"));
			return true;
		});
	}

	EnqueueEndPie();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftTreadmillPieTest, "TDGame.SeamlessTravel.RealmLiftTreadmill", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDRealmLiftTreadmillPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	const TArray<FTDRealmGateTravelCase> Cases = CollectRealmLiftPieCases();
	if (!TestRealmLiftPieCasesReady(*this, Cases))
	{
		return false;
	}

	if (!OpenMapAndStartPie(*this, OpenWorldMapPackagePath))
	{
		return false;
	}

	EnqueueWaitSeconds(TDRealmPieWarmupSeconds);

	const FTDRealmGateTravelCase Case = Cases[0];
	TSharedRef<FTDRealmLiftPieState> State = MakeShared<FTDRealmLiftPieState>();
	EnqueueStep([this, State, Case]()
	{
		State->bIsAborted = !BindRealmLiftPieState(*this, *State, Case);
		SetRealmLiftPieDelay(TDRealmLiftPieTreadmillDelaySeconds);
		AddInfo(FString::Printf(TEXT("%s set to %.0f s for the treadmill test."), TDRealmLiftPieDelayVariableName, TDRealmLiftPieTreadmillDelaySeconds));
		return true;
	});
	EnqueueRealmLiftApproach(*this, State, Case, false);
	EnqueueStep([this, State]()
	{
		UTDSeamlessTravelSubsystem* Travel = UTDSeamlessTravelSubsystem::GetSeamlessTravelSubsystem(GetPieWorld());
		if (!State->bIsAborted && Travel && IsTravelPreloading(*Travel))
		{
			AddInfo(FString::Printf(TEXT("Cancelling preload of '%s' so the simulated delay restarts at boarding."), *Travel->GetActiveDungeonId().ToString()));
			Travel->CancelTravel();
		}
		return true;
	});
	EnqueueRealmLiftRide(*this, State, Case, true, true);
	EnqueueStep([this, State, Case]()
	{
		SetRealmLiftPieDelay(0.0f);
		FinishRealmLiftPieCase(*this, *State, Case, TEXT("treadmill ride"));
		return true;
	});

	EnqueueEndPie();
	return true;
}

#endif
