#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDMonsterCharacter.h"
#include "Characters/TDVisualTurnSmoother.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

namespace TDVisualTurnTestSupport
{
	constexpr float ChangeStartSeconds = 0.5f;
	constexpr float TraceSeconds = 2.f;
	constexpr float SettledDegrees = 1.f;
	constexpr float StallStepDegrees = 0.05f;
	constexpr float StallOffsetDegrees = 2.f;
	constexpr float RateToleranceDegreesPerSecond = 1.f;
	constexpr float OvershootToleranceDegrees = 0.5f;
	constexpr float RateJumpLimitDegreesPerSecond = 400.f;
	constexpr float LogicalTurnDegreesPerSecond = 540.f;
	constexpr float BudgetTickSeconds = 0.1f;
	constexpr float WorldFrameSeconds = 1.f / 60.f;
	constexpr float MonsterSpawnHeight = 90.f;
	constexpr float FloorHalfThickness = 50.f;
	constexpr float FreezeSeconds = 0.5f;
	constexpr int32 MaxFreezeWaitFrames = 120;
	const TCHAR* const VisualTurnVariableName = TEXT("TD.Monster.VisualTurn");

	struct FTDTurnTrace
	{
		float SettleSeconds = -1.f;
		float MaxRate = 0.f;
		float MaxRateJump = 0.f;
		float Overshoot = 0.f;
		float MaxLag = 0.f;
		float FinalError = 0.f;
		int32 StallFrames = 0;
	};

	float SnapLogicalYaw(const float Seconds)
	{
		return Seconds < ChangeStartSeconds - UE_KINDA_SMALL_NUMBER ? 0.f : 150.f;
	}

	float BudgetSteppedLogicalYaw(const float Seconds)
	{
		if (Seconds < ChangeStartSeconds - UE_KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		const float TickCount = FMath::FloorToFloat((Seconds - ChangeStartSeconds) / BudgetTickSeconds + UE_KINDA_SMALL_NUMBER);
		return FMath::Min(180.f, LogicalTurnDegreesPerSecond * BudgetTickSeconds * TickCount);
	}

	float SteadyTurnLogicalYaw(const float Seconds)
	{
		return Seconds < ChangeStartSeconds - UE_KINDA_SMALL_NUMBER ? 0.f : FMath::Min(180.f, LogicalTurnDegreesPerSecond * (Seconds - ChangeStartSeconds));
	}

	FTDTurnTrace TraceTurn(float (*LogicalYawAt)(float), const float FramesPerSecond, const float FinalYaw)
	{
		const FTDVisualTurnSettings Settings;
		FTDVisualTurnSmoother Smoother;
		const float FrameSeconds = 1.f / FramesPerSecond;
		const int32 FrameCount = FMath::RoundToInt(TraceSeconds * FramesPerSecond);
		FTDTurnTrace Trace;
		TArray<float> VisualYaws;
		TArray<float> LogicalYaws;
		float PreviousRate = 0.f;
		for (int32 FrameIndex = 0; FrameIndex <= FrameCount; ++FrameIndex)
		{
			const float LogicalYaw = LogicalYawAt(FrameIndex * FrameSeconds);
			const float Offset = Smoother.Advance(LogicalYaw, FrameIndex == 0 ? 0.f : FrameSeconds, Settings);
			const float VisualYaw = Smoother.GetVisualYaw();
			if (!VisualYaws.IsEmpty())
			{
				const float Step = FRotator::NormalizeAxis(VisualYaw - VisualYaws.Last());
				const float Rate = Step / FrameSeconds;
				Trace.MaxRate = FMath::Max(Trace.MaxRate, FMath::Abs(Rate));
				Trace.MaxRateJump = FMath::Max(Trace.MaxRateJump, FMath::Abs(Rate - PreviousRate));
				Trace.StallFrames += FMath::Abs(Offset) > StallOffsetDegrees && FMath::Abs(Step) < StallStepDegrees ? 1 : 0;
				PreviousRate = Rate;
			}
			if (FMath::IsNearlyEqual(LogicalYaw, FinalYaw))
			{
				Trace.Overshoot = FMath::Max(Trace.Overshoot, FMath::Sign(FinalYaw) * FRotator::NormalizeAxis(VisualYaw - FinalYaw));
			}
			Trace.MaxLag = FMath::Max(Trace.MaxLag, FMath::Abs(Offset));
			VisualYaws.Add(VisualYaw);
			LogicalYaws.Add(LogicalYaw);
		}
		int32 LastChangeIndex = 0;
		for (int32 FrameIndex = 0; FrameIndex < LogicalYaws.Num(); ++FrameIndex)
		{
			LastChangeIndex = FMath::IsNearlyEqual(LogicalYaws[FrameIndex], FinalYaw) ? LastChangeIndex : FrameIndex + 1;
		}
		for (int32 FrameIndex = VisualYaws.Num() - 1; FrameIndex >= LastChangeIndex; --FrameIndex)
		{
			if (FMath::Abs(FRotator::NormalizeAxis(VisualYaws[FrameIndex] - FinalYaw)) > SettledDegrees)
			{
				break;
			}
			Trace.SettleSeconds = (FrameIndex - LastChangeIndex) * FrameSeconds;
		}
		Trace.FinalError = FMath::Abs(FRotator::NormalizeAxis(VisualYaws.Last() - FinalYaw));
		return Trace;
	}

	FString DescribeTrace(const FTDTurnTrace& Trace, const float FramesPerSecond)
	{
		return FString::Printf(TEXT("%.0f fps: settle %.3f s, max rate %.1f deg/s, max rate jump %.1f deg/s, overshoot %.3f deg, max lag %.1f deg, stalls %d, final error %.4f deg"),
			FramesPerSecond, Trace.SettleSeconds, Trace.MaxRate, Trace.MaxRateJump, Trace.Overshoot, Trace.MaxLag, Trace.StallFrames, Trace.FinalError);
	}

	struct FTDScopedVisualTurnValue
	{
		explicit FTDScopedVisualTurnValue(const int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(VisualTurnVariableName))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetInt();
			Variable->Set(Value, ECVF_SetByConsole);
		}

		~FTDScopedVisualTurnValue()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByConsole);
			}
		}

		bool Holds(const int32 Value) const
		{
			return Variable && Variable->GetInt() == Value;
		}

		void Set(const int32 Value) const
		{
			if (Variable)
			{
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 1;
	};

	struct FTDScopedVisualTurnWorld
	{
		FTDScopedVisualTurnWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDVisualTurnTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			AActor* Floor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
			Floor->AddInstanceComponent(Box);
			Floor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(5000.f, 5000.f, FloorHalfThickness));
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->RegisterComponent();
			Floor->SetActorLocation(FVector(0.f, 0.f, -FloorHalfThickness));
		}

		~FTDScopedVisualTurnWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ATDMonsterCharacter* SpawnMonster() const
		{
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, MonsterSpawnHeight));
			return World->SpawnActor<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
		}

		void TickFrames(const int32 FrameCount) const
		{
			for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, WorldFrameSeconds);
			}
		}

		UWorld* World = nullptr;
	};

	float MeasureVisualYawOffset(const ATDMonsterCharacter& Monster)
	{
		const float BaseYaw = static_cast<float>(Monster.GetMoverComponent()->GetBaseVisualComponentTransform().Rotator().Yaw);
		return FRotator::NormalizeAxis(static_cast<float>(Monster.GetMesh()->GetComponentRotation().Yaw - Monster.GetActorRotation().Yaw) - BaseYaw);
	}

	float MeasureMeshYaw(const ATDMonsterCharacter& Monster)
	{
		return static_cast<float>(Monster.GetMesh()->GetComponentRotation().Yaw);
	}

	TArray<float> RecordScriptedLogicalYaws(const int32 VisualTurnValue)
	{
		const FTDScopedVisualTurnValue VisualTurn(VisualTurnValue);
		TArray<float> LogicalYaws;
		if (!VisualTurn.Holds(VisualTurnValue))
		{
			return LogicalYaws;
		}
		FTDScopedVisualTurnWorld Fixture;
		ATDMonsterCharacter* Monster = Fixture.SpawnMonster();
		if (!Monster)
		{
			return LogicalYaws;
		}
		Fixture.TickFrames(18);
		const FVector2D Directions[] = { FVector2D(1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(-1.f, 0.2f).GetSafeNormal(), FVector2D(0.6f, -0.8f) };
		for (int32 FrameIndex = 0; FrameIndex < 150; ++FrameIndex)
		{
			if (FrameIndex == 75)
			{
				Monster->FaceRotationImmediately(FRotator(0.f, 120.f, 0.f));
			}
			if (FrameIndex % 50 < 35)
			{
				Monster->ApplyBodyMoveIntent(Directions[(FrameIndex / 50) % static_cast<int32>(UE_ARRAY_COUNT(Directions))], 1.f);
			}
			Fixture.TickFrames(1);
			LogicalYaws.Add(static_cast<float>(Monster->GetActorRotation().Yaw));
		}
		return LogicalYaws;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDVisualTurnSnapSettlesAtAnyFrameRateTest, "TDGame.MonsterAI.VisualTurn.SnapSettlesAtAnyFrameRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDVisualTurnSnapSettlesAtAnyFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace TDVisualTurnTestSupport;
	const FTDVisualTurnSettings Settings;
	float EarliestSettle = TNumericLimits<float>::Max();
	float LatestSettle = 0.f;
	for (const float FramesPerSecond : { 20.f, 30.f, 60.f, 144.f })
	{
		const FTDTurnTrace Trace = TraceTurn(&SnapLogicalYaw, FramesPerSecond, 150.f);
		AddInfo(DescribeTrace(Trace, FramesPerSecond));
		TestTrue(TEXT("The visible body never turns faster than the visual limit"), Trace.MaxRate <= Settings.MaxDegreesPerSecond + RateToleranceDegreesPerSecond);
		TestTrue(TEXT("The visible body does not overshoot the new facing"), Trace.Overshoot <= OvershootToleranceDegrees);
		TestTrue(TEXT("The visible body settles within 0.45 s"), Trace.SettleSeconds >= 0.f && Trace.SettleSeconds <= 0.45f);
		TestTrue(TEXT("The visible body ends on the logical facing"), Trace.FinalError <= 0.05f);
		EarliestSettle = FMath::Min(EarliestSettle, Trace.SettleSeconds);
		LatestSettle = FMath::Max(LatestSettle, Trace.SettleSeconds);
	}
	AddInfo(FString::Printf(TEXT("Settle spread across frame rates %.3f s"), LatestSettle - EarliestSettle));
	TestTrue(TEXT("Settle time does not depend on the frame rate beyond one coarse frame"), LatestSettle - EarliestSettle <= 0.06f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDVisualTurnLongTickIntervalKeepsTurningTest, "TDGame.MonsterAI.VisualTurn.LongTickIntervalKeepsTurning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDVisualTurnLongTickIntervalKeepsTurningTest::RunTest(const FString& Parameters)
{
	using namespace TDVisualTurnTestSupport;
	const FTDVisualTurnSettings Settings;
	for (const float FramesPerSecond : { 30.f, 60.f, 144.f })
	{
		const FTDTurnTrace Trace = TraceTurn(&BudgetSteppedLogicalYaw, FramesPerSecond, 180.f);
		AddInfo(DescribeTrace(Trace, FramesPerSecond));
		TestEqual(TEXT("The visible body keeps turning on frames between logical updates"), Trace.StallFrames, 0);
		TestTrue(TEXT("The visible body never turns faster than the visual limit"), Trace.MaxRate <= Settings.MaxDegreesPerSecond + RateToleranceDegreesPerSecond);
		TestTrue(TEXT("The visible body does not overshoot"), Trace.Overshoot <= OvershootToleranceDegrees);
		TestTrue(TEXT("The visible body settles within 0.3 s after the last logical update"), Trace.SettleSeconds >= 0.f && Trace.SettleSeconds <= 0.3f);
		if (FramesPerSecond >= 60.f)
		{
			TestTrue(TEXT("Turn speed changes stay gradual between frames"), Trace.MaxRateJump < RateJumpLimitDegreesPerSecond);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDVisualTurnSteadyTurnHasBoundedLagTest, "TDGame.MonsterAI.VisualTurn.SteadyTurnHasBoundedLag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDVisualTurnSteadyTurnHasBoundedLagTest::RunTest(const FString& Parameters)
{
	using namespace TDVisualTurnTestSupport;
	const FTDTurnTrace Trace = TraceTurn(&SteadyTurnLogicalYaw, 60.f, 180.f);
	AddInfo(DescribeTrace(Trace, 60.f));
	TestTrue(TEXT("The turn starts and stops without a speed jump the logical turn has"), Trace.MaxRateJump < RateJumpLimitDegreesPerSecond);
	TestTrue(TEXT("The visible body stays within 35 degrees of the logical facing while turning"), Trace.MaxLag <= 35.f);
	TestTrue(TEXT("The visible body reaches the logical turn speed"), Trace.MaxRate >= 500.f && Trace.MaxRate <= 600.f);
	TestTrue(TEXT("The visible body does not overshoot"), Trace.Overshoot <= OvershootToleranceDegrees);
	TestTrue(TEXT("The visible body settles within 0.25 s after the logical turn ends"), Trace.SettleSeconds >= 0.f && Trace.SettleSeconds <= 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDVisualTurnMonsterMeshEasesInstantFacingTest, "TDGame.MonsterAI.VisualTurn.MonsterMeshEasesInstantFacing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDVisualTurnMonsterMeshEasesInstantFacingTest::RunTest(const FString& Parameters)
{
	using namespace TDVisualTurnTestSupport;
	const FTDScopedVisualTurnValue VisualTurn(1);
	if (!TestTrue(TEXT("Visual turn smoothing is on for the test"), VisualTurn.Holds(1)))
	{
		return false;
	}
	const FTDVisualTurnSettings Settings;
	FTDScopedVisualTurnWorld Fixture;
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster();
	if (!TestNotNull(TEXT("Monster pawn spawns"), Monster))
	{
		return false;
	}
	Fixture.TickFrames(18);
	const float MeshYawBefore = MeasureMeshYaw(*Monster);
	Monster->FaceRotationImmediately(FRotator(0.f, 150.f, 0.f));
	Fixture.TickFrames(1);
	const float FirstFrameMeshStep = FMath::Abs(FRotator::NormalizeAxis(MeasureMeshYaw(*Monster) - MeshYawBefore));
	AddInfo(FString::Printf(TEXT("Actor yaw %.2f, first frame mesh step %.2f deg, visual offset %.2f deg"), Monster->GetActorRotation().Yaw, FirstFrameMeshStep, MeasureVisualYawOffset(*Monster)));
	TestTrue(TEXT("The logical facing still turns at once"), FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(Monster->GetActorRotation().Yaw) - 150.f)) <= 0.5f);
	TestTrue(TEXT("The visible body moves at most one visual limit step on the first frame"), FirstFrameMeshStep <= Settings.MaxDegreesPerSecond * WorldFrameSeconds + 0.5f);

	Fixture.TickFrames(60);
	const float SettledOffset = MeasureVisualYawOffset(*Monster);
	AddInfo(FString::Printf(TEXT("Visual offset after 1 s %.3f deg"), SettledOffset));
	TestTrue(TEXT("The visible body catches up with the logical facing"), FMath::Abs(SettledOffset) <= 0.5f);

	Monster->FaceRotationImmediately(FRotator(0.f, -60.f, 0.f));
	Fixture.TickFrames(1);
	const float TurningOffset = MeasureVisualYawOffset(*Monster);
	VisualTurn.Set(0);
	TestTrue(TEXT("Visual turn smoothing switches off"), VisualTurn.Holds(0));
	Fixture.TickFrames(1);
	const float DisabledOffset = MeasureVisualYawOffset(*Monster);
	AddInfo(FString::Printf(TEXT("Visual offset mid-turn %.2f deg, after switching smoothing off %.3f deg"), TurningOffset, DisabledOffset));
	TestTrue(TEXT("The second instant facing is eased too"), FMath::Abs(TurningOffset) >= 90.f);
	TestTrue(TEXT("Switching smoothing off puts the mesh back on the logical facing"), FMath::Abs(DisabledOffset) <= 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDVisualTurnKeepsLogicalFacingTest, "TDGame.MonsterAI.VisualTurn.KeepsLogicalFacing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDVisualTurnKeepsLogicalFacingTest::RunTest(const FString& Parameters)
{
	using namespace TDVisualTurnTestSupport;
	if (!TestNotNull(TEXT("Visual turn console variable exists"), IConsoleManager::Get().FindConsoleVariable(VisualTurnVariableName)))
	{
		return false;
	}
	const TArray<float> SmoothedRun = RecordScriptedLogicalYaws(1);
	const TArray<float> PlainRun = RecordScriptedLogicalYaws(0);
	if (!TestEqual(TEXT("Both runs record every frame"), SmoothedRun.Num(), PlainRun.Num()) || !TestTrue(TEXT("Runs are not empty"), SmoothedRun.Num() > 0))
	{
		return false;
	}
	float LargestDifference = 0.f;
	float LargestFrameTurn = 0.f;
	for (int32 FrameIndex = 0; FrameIndex < SmoothedRun.Num(); ++FrameIndex)
	{
		LargestDifference = FMath::Max(LargestDifference, FMath::Abs(FRotator::NormalizeAxis(SmoothedRun[FrameIndex] - PlainRun[FrameIndex])));
		if (FrameIndex > 0)
		{
			LargestFrameTurn = FMath::Max(LargestFrameTurn, FMath::Abs(FRotator::NormalizeAxis(PlainRun[FrameIndex] - PlainRun[FrameIndex - 1])));
		}
	}
	AddInfo(FString::Printf(TEXT("Largest logical yaw difference %.5f deg over %d frames, largest logical frame turn %.2f deg"), LargestDifference, SmoothedRun.Num(), LargestFrameTurn));
	TestTrue(TEXT("The scripted run turns the monster"), LargestFrameTurn > 5.f);
	TestTrue(TEXT("Visual smoothing leaves the logical facing unchanged frame by frame"), LargestDifference <= 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDVisualTurnFreezeHoldsVisibleBodyTest, "TDGame.MonsterAI.VisualTurn.FreezeHoldsVisibleBody", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDVisualTurnFreezeHoldsVisibleBodyTest::RunTest(const FString& Parameters)
{
	using namespace TDVisualTurnTestSupport;
	const FTDScopedVisualTurnValue VisualTurn(1);
	if (!TestTrue(TEXT("Visual turn smoothing is on for the test"), VisualTurn.Holds(1)))
	{
		return false;
	}
	const FTDVisualTurnSettings Settings;
	FTDScopedVisualTurnWorld Fixture;
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster();
	if (!TestNotNull(TEXT("Monster pawn spawns"), Monster))
	{
		return false;
	}
	Fixture.TickFrames(18);
	Monster->FaceRotationImmediately(FRotator(0.f, 150.f, 0.f));
	Fixture.TickFrames(3);
	const float OffsetBeforeFreeze = MeasureVisualYawOffset(*Monster);

	UTDStatusDefinition* Freeze = NewObject<UTDStatusDefinition>(Fixture.World);
	Freeze->Duration = FreezeSeconds;
	Freeze->bFreezesTarget = true;
	FTDDamageContext FreezeContext;
	FreezeContext.Caster = Monster;
	FreezeContext.Budget = MakeShared<FTDDamageChainBudget>();
	UTDCombatComponent* Combat = Monster->GetBodyCombatComponent();
	Combat->ApplyStatus(Freeze, FreezeContext);
	if (!TestTrue(TEXT("Freeze status is applied"), Combat->IsFrozen()))
	{
		return false;
	}
	Fixture.TickFrames(10);
	const float OffsetWhileFrozen = MeasureVisualYawOffset(*Monster);
	AddInfo(FString::Printf(TEXT("Visual offset before freeze %.2f deg, while frozen %.2f deg"), OffsetBeforeFreeze, OffsetWhileFrozen));
	TestTrue(TEXT("The turn was still under way when the freeze hit"), FMath::Abs(OffsetBeforeFreeze) >= 90.f);
	TestTrue(TEXT("The frozen body keeps its visible facing"), FMath::Abs(FRotator::NormalizeAxis(OffsetWhileFrozen - OffsetBeforeFreeze)) <= 0.5f);

	int32 WaitedFrames = 0;
	while (Combat->IsFrozen() && WaitedFrames < MaxFreezeWaitFrames)
	{
		Fixture.TickFrames(1);
		++WaitedFrames;
	}
	if (!TestFalse(TEXT("The freeze ends"), Combat->IsFrozen()))
	{
		return false;
	}
	const float MeshYawAtThaw = MeasureMeshYaw(*Monster);
	Fixture.TickFrames(1);
	const float ThawStep = FMath::Abs(FRotator::NormalizeAxis(MeasureMeshYaw(*Monster) - MeshYawAtThaw));
	Fixture.TickFrames(60);
	const float SettledOffset = MeasureVisualYawOffset(*Monster);
	AddInfo(FString::Printf(TEXT("Thaw after %d frames, first step after thaw %.2f deg, offset 1 s later %.3f deg"), WaitedFrames, ThawStep, SettledOffset));
	TestTrue(TEXT("The body does not jump when the freeze ends"), ThawStep <= Settings.MaxDegreesPerSecond * WorldFrameSeconds + 0.5f);
	TestTrue(TEXT("The body catches up after the freeze"), FMath::Abs(SettledOffset) <= 0.5f);
	return true;
}

#endif
