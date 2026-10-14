#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Framework/TDTopDownOcclusionSubsystem.h"
#include "Framework/TDTopDownSettings.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/AutomationTest.h"
#include "Tests/TDPieWaitUntilCommand.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"

namespace
{
	const TCHAR* const TDTopDownPieCutoutCVarName = TEXT("td.TopDown.OcclusionCutout");
	const FName TDTopDownPieFocusName(TEXT("OcclusionFocus"));
	const FName TDTopDownPieRadiusName(TEXT("OcclusionRadius"));
	const FName TDTopDownPieSoftnessName(TEXT("OcclusionSoftness"));
	const FName TDTopDownPieKeepBaseName(TEXT("OcclusionKeepBase"));
	const FName TDTopDownPieMaxCutName(TEXT("OcclusionMaxCut"));
	const FName TDTopDownPieFeetZName(TEXT("OcclusionFeetZ"));
	const FVector TDTopDownPieMoveOffsetCm(400.0, 300.0, 0.0);
	constexpr float TDTopDownPieWarmupSeconds = 10.0f;
	constexpr double TDTopDownPieMinimumMoveCm = 200.0;
	constexpr double TDTopDownPieSettleToleranceCm = 0.1;
	constexpr double TDTopDownPieSettleHoldSeconds = 1.0;
	constexpr double TDTopDownPieSettleTimeoutSeconds = 20.0;
	constexpr double TDTopDownPieUpdateTimeoutSeconds = 5.0;
	constexpr float TDTopDownPieLocationToleranceCm = 1.0f;
	constexpr float TDTopDownPieScalarTolerance = 0.001f;
	constexpr float TDTopDownPieActivationTolerance = 0.001f;
	constexpr float TDTopDownPieActivationThreshold = 0.5f;

	struct FTDTopDownOcclusionPieTestState
	{
		bool bIsAborted = false;
		FVector TeleportStartLocation = FVector::ZeroVector;
		FVector LastSampledLocation = FVector::ZeroVector;
		double StableSinceWorldSeconds = 0.0;
	};

	UMaterialParameterCollectionInstance* FindTopDownPieInstance(UWorld* World)
	{
		const UTDTopDownOcclusionSubsystem* Subsystem = World ? World->GetSubsystem<UTDTopDownOcclusionSubsystem>() : nullptr;
		const UMaterialParameterCollection* Collection = Subsystem ? Subsystem->GetParameterCollection() : nullptr;
		return Collection ? World->GetParameterCollectionInstance(Collection) : nullptr;
	}

	TOptional<float> ReadTopDownPieActivation(UWorld* World)
	{
		UMaterialParameterCollectionInstance* Instance = FindTopDownPieInstance(World);
		FLinearColor Focus(0.0f, 0.0f, 0.0f, 0.0f);
		if (!Instance || !Instance->GetVectorParameterValue(TDTopDownPieFocusName, Focus))
		{
			return TOptional<float>();
		}
		return TOptional<float>(Focus.A);
	}

	void SetTopDownPieCutoutCVar(int32 Value)
	{
		IConsoleVariable* CutoutCVar = IConsoleManager::Get().FindConsoleVariable(TDTopDownPieCutoutCVarName);
		if (!CutoutCVar)
		{
			return;
		}
		CutoutCVar->Set(Value, ECVF_SetByConsole);
	}

	void CheckTopDownPieScalar(FAutomationTestBase& Test, UMaterialParameterCollectionInstance& Instance, const FName ParameterName, float ExpectedValue, float Tolerance, const FString& Prefix)
	{
		float ActualValue = 0.0f;
		if (!Instance.GetScalarParameterValue(ParameterName, ActualValue))
		{
			Test.AddError(FString::Printf(TEXT("%s: the parameter collection has no scalar parameter '%s'."), *Prefix, *ParameterName.ToString()));
			return;
		}
		Test.TestNearlyEqual(FString::Printf(TEXT("%s: %s"), *Prefix, *ParameterName.ToString()), ActualValue, ExpectedValue, Tolerance);
	}

	void CheckTopDownPieOcclusionState(FAutomationTestBase& Test, UWorld& World, const APawn& Pawn, const FString& Prefix)
	{
		const UTDTopDownSettings* Settings = GetDefault<UTDTopDownSettings>();
		UMaterialParameterCollectionInstance* Instance = FindTopDownPieInstance(&World);
		if (!Test.TestNotNull(FString::Printf(TEXT("%s: parameter collection instance exists"), *Prefix), Instance))
		{
			return;
		}

		FLinearColor Focus(0.0f, 0.0f, 0.0f, 0.0f);
		if (!Instance->GetVectorParameterValue(TDTopDownPieFocusName, Focus))
		{
			Test.AddError(FString::Printf(TEXT("%s: the parameter collection has no vector parameter 'OcclusionFocus'."), *Prefix));
			return;
		}

		const FVector PawnLocation = Pawn.GetActorLocation();
		const FVector ExpectedFocus = PawnLocation + FVector(0.0, 0.0, Settings->OcclusionFocusHeightCm);
		const FVector ActualFocus(Focus.R, Focus.G, Focus.B);
		const double FocusErrorCm = (ActualFocus - ExpectedFocus).GetAbsMax();
		Test.AddInfo(FString::Printf(TEXT("%s: pawn %s, focus %s, expected %s, error %.3f cm."), *Prefix, *PawnLocation.ToCompactString(), *ActualFocus.ToCompactString(), *ExpectedFocus.ToCompactString(), FocusErrorCm));
		Test.TestTrue(FString::Printf(TEXT("%s: focus is within %.1f cm of pawn location + focus height"), *Prefix, TDTopDownPieLocationToleranceCm), FocusErrorCm <= TDTopDownPieLocationToleranceCm);
		Test.TestNearlyEqual(FString::Printf(TEXT("%s: focus activation"), *Prefix), Focus.A, 1.0f, TDTopDownPieActivationTolerance);

		const float ExpectedFeetZ = static_cast<float>(PawnLocation.Z - Pawn.GetSimpleCollisionHalfHeight());
		CheckTopDownPieScalar(Test, *Instance, TDTopDownPieFeetZName, ExpectedFeetZ, TDTopDownPieLocationToleranceCm, Prefix);
		CheckTopDownPieScalar(Test, *Instance, TDTopDownPieRadiusName, Settings->OcclusionRadiusCm, TDTopDownPieScalarTolerance, Prefix);
		CheckTopDownPieScalar(Test, *Instance, TDTopDownPieSoftnessName, Settings->OcclusionSoftnessCm, TDTopDownPieScalarTolerance, Prefix);
		CheckTopDownPieScalar(Test, *Instance, TDTopDownPieKeepBaseName, Settings->OcclusionKeepBaseCm, TDTopDownPieScalarTolerance, Prefix);
		CheckTopDownPieScalar(Test, *Instance, TDTopDownPieMaxCutName, Settings->OcclusionMaxCut, TDTopDownPieScalarTolerance, Prefix);
	}

	bool HasTopDownPiePawnSettled(FTDTopDownOcclusionPieTestState& State, const UWorld& World, const APawn& Pawn)
	{
		const FVector Location = Pawn.GetActorLocation();
		const double NowSeconds = World.GetTimeSeconds();
		if (FVector::DistSquared(Location, State.LastSampledLocation) > FMath::Square(TDTopDownPieSettleToleranceCm))
		{
			State.LastSampledLocation = Location;
			State.StableSinceWorldSeconds = NowSeconds;
			return false;
		}

		const bool bHasMovedAwayFromStart = FVector::DistSquared(Location, State.TeleportStartLocation) >= FMath::Square(TDTopDownPieMinimumMoveCm);
		return bHasMovedAwayFromStart && NowSeconds - State.StableSinceWorldSeconds >= TDTopDownPieSettleHoldSeconds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTopDownOcclusionFocusFollowsPawnPieTest, "TDGame.TopDown.OcclusionFocusFollowsPawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDTopDownOcclusionFocusFollowsPawnPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	TSharedRef<FTDTopDownOcclusionPieTestState> State = MakeShared<FTDTopDownOcclusionPieTestState>();
	if (!OpenMapAndStartPie(*this, OpenWorldMapPackagePath))
	{
		return false;
	}

	const auto Abort = [State]() { State->bIsAborted = true; };
	const auto WaitForFocusActivation = [this, State, Abort](const FString& Description, bool bShouldBeActive)
	{
		EnqueueWaitUntil(*this, Description, [State, bShouldBeActive]()
		{
			if (State->bIsAborted)
			{
				return true;
			}
			const TOptional<float> Activation = ReadTopDownPieActivation(GetPieWorld());
			if (!Activation.IsSet())
			{
				return false;
			}
			return (Activation.GetValue() > TDTopDownPieActivationThreshold) == bShouldBeActive;
		}, TDTopDownPieUpdateTimeoutSeconds, Abort);
	};

	EnqueueWaitSeconds(TDTopDownPieWarmupSeconds);

	EnqueueStep([this, State]()
	{
		UWorld* World = GetPieWorld();
		APawn* Pawn = FindPlayerPawn(World);
		const UTDTopDownOcclusionSubsystem* Subsystem = World ? World->GetSubsystem<UTDTopDownOcclusionSubsystem>() : nullptr;
		const UTDTopDownSettings* Settings = GetDefault<UTDTopDownSettings>();
		if (!TestNotNull(TEXT("PIE world has a player pawn"), Pawn) || !TestNotNull(TEXT("PIE world has a top down occlusion subsystem"), Subsystem))
		{
			State->bIsAborted = true;
			return true;
		}

		if (!Subsystem->GetParameterCollection())
		{
			AddError(FString::Printf(TEXT("The PIE world did not load the top down parameter collection '%s'. Create and save it (Tools/WorldGen/editor_make_topdown_materials.py) before running this test."), *Settings->TopDownParameterCollection.ToString()));
			State->bIsAborted = true;
			return true;
		}

		const bool bIsCutoutConfigured = TestTrue(TEXT("Top down settings enable the occlusion cutout"), Settings->bEnableOcclusionCutout);
		const bool bIsCVarRegistered = TestNotNull(TEXT("Console variable td.TopDown.OcclusionCutout is registered"), IConsoleManager::Get().FindConsoleVariable(TDTopDownPieCutoutCVarName));
		if (!bIsCutoutConfigured || !bIsCVarRegistered)
		{
			State->bIsAborted = true;
			return true;
		}

		State->TeleportStartLocation = Pawn->GetActorLocation();
		State->LastSampledLocation = State->TeleportStartLocation;
		State->StableSinceWorldSeconds = World->GetTimeSeconds();
		const FVector TargetLocation = State->TeleportStartLocation + TDTopDownPieMoveOffsetCm;
		if (!TestTrue(TEXT("Pawn teleport is accepted"), UTDSeamlessTravelSubsystem::TeleportPawnToDestination(*Pawn, TargetLocation, Pawn->GetActorRotation())))
		{
			State->bIsAborted = true;
		}
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Pawn settles at the teleport target"), [this, State]()
	{
		if (State->bIsAborted)
		{
			return true;
		}

		UWorld* World = GetPieWorld();
		const APawn* Pawn = FindPlayerPawn(World);
		if (!TestNotNull(TEXT("Player pawn exists while settling"), Pawn))
		{
			State->bIsAborted = true;
			return true;
		}
		return HasTopDownPiePawnSettled(*State, *World, *Pawn);
	}, TDTopDownPieSettleTimeoutSeconds, Abort);

	EnqueueStep([this, State]()
	{
		if (State->bIsAborted)
		{
			return true;
		}

		UWorld* World = GetPieWorld();
		const APawn* Pawn = FindPlayerPawn(World);
		if (!TestNotNull(TEXT("Player pawn exists after settling"), Pawn))
		{
			State->bIsAborted = true;
			return true;
		}

		CheckTopDownPieOcclusionState(*this, *World, *Pawn, TEXT("After the pawn moved"));
		SetTopDownPieCutoutCVar(0);
		return true;
	});

	WaitForFocusActivation(TEXT("Focus deactivates when the console variable is 0"), false);

	EnqueueStep([this, State]()
	{
		if (State->bIsAborted)
		{
			return true;
		}

		const TOptional<float> Activation = ReadTopDownPieActivation(GetPieWorld());
		if (TestTrue(TEXT("Focus activation is readable while the console variable is 0"), Activation.IsSet()))
		{
			TestNearlyEqual(TEXT("Focus activation is 0 while the console variable is 0"), Activation.GetValue(), 0.0f, TDTopDownPieActivationTolerance);
		}
		SetTopDownPieCutoutCVar(1);
		return true;
	});

	WaitForFocusActivation(TEXT("Focus activates again when the console variable is 1"), true);

	EnqueueStep([this, State]()
	{
		if (State->bIsAborted)
		{
			return true;
		}

		UWorld* World = GetPieWorld();
		const APawn* Pawn = FindPlayerPawn(World);
		if (TestNotNull(TEXT("Player pawn exists after the console variable is restored"), Pawn))
		{
			CheckTopDownPieOcclusionState(*this, *World, *Pawn, TEXT("After the console variable is restored"));
		}
		return true;
	});

	EnqueueStep([]()
	{
		SetTopDownPieCutoutCVar(1);
		return true;
	});

	EnqueueEndPie();
	return true;
}

#endif
