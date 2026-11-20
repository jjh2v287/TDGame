#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/TDMonsterCharacter.h"
#include "Characters/TDTurnSmoother.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "UObject/Package.h"

namespace TDTurnSmoothingTestSupport
{
	constexpr float ChangeStartSeconds = 0.5f;
	constexpr float TraceSeconds = 2.f;
	constexpr float SettledDegrees = 1.f;
	constexpr float StallStepDegrees = 0.05f;
	constexpr float StallOffsetDegrees = 2.f;
	constexpr float RateToleranceDegreesPerSecond = 1.f;
	constexpr float OvershootToleranceDegrees = 0.5f;
	constexpr float RateJumpLimitDegreesPerSecond = 400.f;
	constexpr float SteppedTargetDegrees = 54.f;
	constexpr float SteppedTargetIntervalSeconds = 0.1f;
	constexpr float RotatingTargetDegreesPerSecond = 360.f;
	constexpr float ReverseAfterSeconds = 0.25f;
	constexpr int32 AlternatingDirectionFrames = 8;
	constexpr float DuelWarmUpSeconds = 1.f;
	constexpr float MonsterTurningRate = 540.f;
	constexpr float WorldFrameSeconds = 1.f / 60.f;
	constexpr float DuelStepSeconds = 1.f / 64.f;
	constexpr float DuelSeconds = 12.f;
	constexpr float SnapDegrees = 30.f;
	constexpr float MonsterSpawnHeight = 90.f;
	constexpr float TargetBodyHeight = 95.f;
	constexpr float TargetMaxHealth = 50000.f;
	constexpr float FloorHalfThickness = 50.f;
	constexpr float FreezeSeconds = 0.5f;
	constexpr float AbilityWindupSeconds = 0.6f;
	constexpr float AbilityRadius = 150.f;
	constexpr int32 MaxFreezeWaitFrames = 120;
	const TCHAR* const SmoothTurnVariableName = TEXT("TD.Monster.SmoothTurn");

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

	float SnapTargetYaw(const float Seconds)
	{
		return Seconds < ChangeStartSeconds - UE_KINDA_SMALL_NUMBER ? 0.f : 150.f;
	}

	float SteppedTargetYaw(const float Seconds)
	{
		if (Seconds < ChangeStartSeconds - UE_KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		const float StepCount = FMath::FloorToFloat((Seconds - ChangeStartSeconds) / SteppedTargetIntervalSeconds + UE_KINDA_SMALL_NUMBER);
		return FMath::Min(180.f, SteppedTargetDegrees * StepCount);
	}

	float RotatingTargetYaw(const float Seconds)
	{
		return Seconds < ChangeStartSeconds - UE_KINDA_SMALL_NUMBER ? 0.f : FMath::Min(180.f, RotatingTargetDegreesPerSecond * (Seconds - ChangeStartSeconds));
	}

	float ReversedTargetYaw(const float Seconds)
	{
		if (Seconds < ChangeStartSeconds - UE_KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		return Seconds < ChangeStartSeconds + ReverseAfterSeconds - UE_KINDA_SMALL_NUMBER ? 150.f : -30.f;
	}

	FTDTurnTrace TraceTurn(float (*TargetYawAt)(float), const float FramesPerSecond, const float FinalYaw)
	{
		const FTDTurnSmoothingSettings Settings;
		FTDTurnSmoother Smoother;
		Smoother.Reset(0.f);
		const float FrameSeconds = 1.f / FramesPerSecond;
		const int32 FrameCount = FMath::RoundToInt(TraceSeconds * FramesPerSecond);
		FTDTurnTrace Trace;
		TArray<float> Yaws;
		TArray<float> TargetYaws;
		float PreviousRate = 0.f;
		for (int32 FrameIndex = 0; FrameIndex <= FrameCount; ++FrameIndex)
		{
			const float TargetYaw = TargetYawAt(FrameIndex * FrameSeconds);
			const float Yaw = Smoother.Advance(TargetYaw, FrameIndex == 0 ? 0.f : FrameSeconds, Settings);
			const float Offset = FRotator::NormalizeAxis(Yaw - TargetYaw);
			if (!Yaws.IsEmpty())
			{
				const float Step = FRotator::NormalizeAxis(Yaw - Yaws.Last());
				const float Rate = Step / FrameSeconds;
				Trace.MaxRate = FMath::Max(Trace.MaxRate, FMath::Abs(Rate));
				Trace.MaxRateJump = FMath::Max(Trace.MaxRateJump, FMath::Abs(Rate - PreviousRate));
				Trace.StallFrames += FMath::Abs(Offset) > StallOffsetDegrees && FMath::Abs(Step) < StallStepDegrees ? 1 : 0;
				PreviousRate = Rate;
			}
			if (FMath::IsNearlyEqual(TargetYaw, FinalYaw))
			{
				Trace.Overshoot = FMath::Max(Trace.Overshoot, FMath::Sign(FinalYaw) * FRotator::NormalizeAxis(Yaw - FinalYaw));
			}
			Trace.MaxLag = FMath::Max(Trace.MaxLag, FMath::Abs(Offset));
			Yaws.Add(Yaw);
			TargetYaws.Add(TargetYaw);
		}
		int32 LastChangeIndex = 0;
		for (int32 FrameIndex = 0; FrameIndex < TargetYaws.Num(); ++FrameIndex)
		{
			LastChangeIndex = FMath::IsNearlyEqual(TargetYaws[FrameIndex], FinalYaw) ? LastChangeIndex : FrameIndex + 1;
		}
		for (int32 FrameIndex = Yaws.Num() - 1; FrameIndex >= LastChangeIndex; --FrameIndex)
		{
			if (FMath::Abs(FRotator::NormalizeAxis(Yaws[FrameIndex] - FinalYaw)) > SettledDegrees)
			{
				break;
			}
			Trace.SettleSeconds = (FrameIndex - LastChangeIndex) * FrameSeconds;
		}
		Trace.FinalError = FMath::Abs(FRotator::NormalizeAxis(Yaws.Last() - FinalYaw));
		return Trace;
	}

	FString DescribeTrace(const FTDTurnTrace& Trace, const float FramesPerSecond)
	{
		return FString::Printf(TEXT("%.0f fps: settle %.3f s, max rate %.1f deg/s, max rate jump %.1f deg/s, overshoot %.3f deg, max lag %.1f deg, stalls %d, final error %.4f deg"),
			FramesPerSecond, Trace.SettleSeconds, Trace.MaxRate, Trace.MaxRateJump, Trace.Overshoot, Trace.MaxLag, Trace.StallFrames, Trace.FinalError);
	}

	struct FTDScopedSmoothTurnValue
	{
		explicit FTDScopedSmoothTurnValue(const int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(SmoothTurnVariableName))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetInt();
			Variable->Set(Value, ECVF_SetByConsole);
		}

		~FTDScopedSmoothTurnValue()
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

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 1;
	};

	struct FTDScopedTurnWorld
	{
		FTDScopedTurnWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDTurnSmoothingTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			AActor* Floor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
			Floor->AddInstanceComponent(Box);
			Floor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(6000.f, 6000.f, FloorHalfThickness));
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->RegisterComponent();
			Floor->SetActorLocation(FVector(0.f, 0.f, -FloorHalfThickness));
		}

		~FTDScopedTurnWorld()
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

		ATDMonsterCharacter* SpawnMonsterOfDefinition(const FName DefinitionId) const
		{
			UTDMonsterSpeciesAsset* Species = NewObject<UTDMonsterSpeciesAsset>(World);
			Species->DefinitionId = DefinitionId;
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, TargetBodyHeight));
			ATDMonsterCharacter* Monster = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
			Monster->SetSpecies(Species);
			Monster->FinishSpawning(SpawnTransform);
			return Monster;
		}

		UTDCombatComponent* SpawnTarget(const FVector& Location) const
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Actor->AddInstanceComponent(Sphere);
			Actor->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(30.f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Location + FVector(0.f, 0.f, TargetBodyHeight));
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = 1;
			Stats.BaseMaxHealth = TargetMaxHealth;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Combatant;
		}

		void TickFrames(const int32 FrameCount) const
		{
			for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, WorldFrameSeconds);
			}
		}

		void StepDuel() const
		{
			FlushAsyncLoading();
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DuelStepSeconds);
		}

		UWorld* World = nullptr;
	};

	float ReadYaw(const ATDMonsterCharacter& Monster)
	{
		return static_cast<float>(Monster.GetActorRotation().Yaw);
	}

	float MeasureMeshFollowError(const ATDMonsterCharacter& Monster)
	{
		const float BaseYaw = static_cast<float>(Monster.GetMoverComponent()->GetBaseVisualComponentTransform().Rotator().Yaw);
		return FRotator::NormalizeAxis(static_cast<float>(Monster.GetMesh()->GetComponentRotation().Yaw - Monster.GetActorRotation().Yaw) - BaseYaw);
	}

	struct FTDCapsuleTurnTrace
	{
		float MaxFrameTurn = 0.f;
		float MaxRateJump = 0.f;
		float MaxMeshFollowError = 0.f;
		float FinalError = 0.f;
		float SecondsToWithinFiveDegrees = -1.f;
	};

	FTDCapsuleTurnTrace TraceCapsuleTurn(const FTDScopedTurnWorld& Fixture, const ATDMonsterCharacter& Monster, const float TargetYaw, const int32 FrameCount)
	{
		FTDCapsuleTurnTrace Trace;
		float PreviousYaw = ReadYaw(Monster);
		float PreviousRate = 0.f;
		for (int32 FrameIndex = 1; FrameIndex <= FrameCount; ++FrameIndex)
		{
			Fixture.TickFrames(1);
			const float Yaw = ReadYaw(Monster);
			const float Rate = FRotator::NormalizeAxis(Yaw - PreviousYaw) / WorldFrameSeconds;
			Trace.MaxFrameTurn = FMath::Max(Trace.MaxFrameTurn, FMath::Abs(FRotator::NormalizeAxis(Yaw - PreviousYaw)));
			Trace.MaxRateJump = FMath::Max(Trace.MaxRateJump, FMath::Abs(Rate - PreviousRate));
			Trace.MaxMeshFollowError = FMath::Max(Trace.MaxMeshFollowError, FMath::Abs(MeasureMeshFollowError(Monster)));
			Trace.FinalError = FMath::Abs(FRotator::NormalizeAxis(Yaw - TargetYaw));
			if (Trace.SecondsToWithinFiveDegrees < 0.f && Trace.FinalError <= 5.f)
			{
				Trace.SecondsToWithinFiveDegrees = FrameIndex * WorldFrameSeconds;
			}
			PreviousYaw = Yaw;
			PreviousRate = Rate;
		}
		return Trace;
	}

	FString DescribeCapsuleTrace(const FTDCapsuleTurnTrace& Trace)
	{
		return FString::Printf(TEXT("max frame turn %.2f deg, max rate jump %.1f deg/s, mesh follow error %.4f deg, within 5 deg after %.3f s, final error %.3f deg"),
			Trace.MaxFrameTurn, Trace.MaxRateJump, Trace.MaxMeshFollowError, Trace.SecondsToWithinFiveDegrees, Trace.FinalError);
	}

	bool PausedTurnDirection(const int32 FrameIndex, FVector2D& OutDirection)
	{
		const FVector2D Directions[] = { FVector2D(1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(-1.f, 0.2f).GetSafeNormal(), FVector2D(0.6f, -0.8f) };
		OutDirection = Directions[(FrameIndex / 50) % static_cast<int32>(UE_ARRAY_COUNT(Directions))];
		return FrameIndex % 50 < 35;
	}

	bool AlternatingDirection(const int32 FrameIndex, FVector2D& OutDirection)
	{
		OutDirection = (FrameIndex / AlternatingDirectionFrames) % 2 == 0 ? FVector2D(1.f, 0.f) : FVector2D(-1.f, 0.1f).GetSafeNormal();
		return true;
	}

	float ScriptMoveTurnsLargestRateJump(const int32 SmoothTurnValue, bool (*DirectionAt)(int32, FVector2D&), const int32 FrameCount, float& OutLargestFrameTurn)
	{
		const FTDScopedSmoothTurnValue SmoothTurn(SmoothTurnValue);
		OutLargestFrameTurn = 0.f;
		if (!SmoothTurn.Holds(SmoothTurnValue))
		{
			return -1.f;
		}
		FTDScopedTurnWorld Fixture;
		ATDMonsterCharacter* Monster = Fixture.SpawnMonster();
		if (!Monster)
		{
			return -1.f;
		}
		Fixture.TickFrames(18);
		float PreviousYaw = ReadYaw(*Monster);
		float PreviousRate = 0.f;
		float LargestRateJump = 0.f;
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			FVector2D Direction;
			if (DirectionAt(FrameIndex, Direction))
			{
				Monster->ApplyBodyMoveIntent(Direction, 1.f);
			}
			Fixture.TickFrames(1);
			const float Yaw = ReadYaw(*Monster);
			const float Rate = FRotator::NormalizeAxis(Yaw - PreviousYaw) / WorldFrameSeconds;
			OutLargestFrameTurn = FMath::Max(OutLargestFrameTurn, FMath::Abs(FRotator::NormalizeAxis(Yaw - PreviousYaw)));
			LargestRateJump = FMath::Max(LargestRateJump, FMath::Abs(Rate - PreviousRate));
			PreviousYaw = Yaw;
			PreviousRate = Rate;
		}
		return LargestRateJump;
	}

	void IgnoreEngineInjectionEnsures(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("TimelineTrait.GetState(Context, ChildState)"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Event.SerialNumber != 0"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("=== Handled ensure: ==="), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Stack:"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("[Callstack]"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Exact, -1, false);
	}

	struct FTDDuelRecord
	{
		float FirstHitSeconds = -1.f;
		int32 HitCount = 0;
		float Damage = 0.f;
		float MaxStepTurn = 0.f;
		int32 SnapCount = 0;
	};

	FTDDuelRecord RunDuel(const FName DefinitionId, const FVector& TargetLocation, const int32 SmoothTurnValue, const float Seconds)
	{
		FTDDuelRecord Record;
		const FTDScopedSmoothTurnValue SmoothTurn(SmoothTurnValue);
		if (!SmoothTurn.Holds(SmoothTurnValue))
		{
			return Record;
		}
		FTDScopedTurnWorld Fixture;
		UTDCombatComponent* Target = Fixture.SpawnTarget(TargetLocation);
		ATDMonsterCharacter* Monster = Fixture.SpawnMonsterOfDefinition(DefinitionId);
		if (!Target || !Monster)
		{
			return Record;
		}
		float PreviousHealth = Target->GetCurrentHealth();
		float PreviousYaw = ReadYaw(*Monster);
		const int32 StepCount = FMath::RoundToInt(Seconds / DuelStepSeconds);
		for (int32 StepIndex = 1; StepIndex <= StepCount; ++StepIndex)
		{
			Fixture.StepDuel();
			const float Health = Target->GetCurrentHealth();
			if (Health < PreviousHealth - UE_KINDA_SMALL_NUMBER)
			{
				Record.FirstHitSeconds = Record.FirstHitSeconds < 0.f ? StepIndex * DuelStepSeconds : Record.FirstHitSeconds;
				++Record.HitCount;
				Record.Damage += PreviousHealth - Health;
			}
			PreviousHealth = Health;
			const float Yaw = ReadYaw(*Monster);
			const float StepTurn = FMath::Abs(FRotator::NormalizeAxis(Yaw - PreviousYaw));
			Record.MaxStepTurn = FMath::Max(Record.MaxStepTurn, StepTurn);
			Record.SnapCount += StepTurn > SnapDegrees ? 1 : 0;
			PreviousYaw = Yaw;
		}
		return Record;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingTurnSettlesAtAnyFrameRateTest, "TDGame.MonsterAI.TurnSmoothing.TurnSettlesAtAnyFrameRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingTurnSettlesAtAnyFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	const FTDTurnSmoothingSettings Settings;
	float EarliestSettle = TNumericLimits<float>::Max();
	float LatestSettle = 0.f;
	for (const float FramesPerSecond : { 20.f, 30.f, 60.f, 144.f })
	{
		const FTDTurnTrace Trace = TraceTurn(&SnapTargetYaw, FramesPerSecond, 150.f);
		AddInfo(DescribeTrace(Trace, FramesPerSecond));
		TestTrue(TEXT("The turn never exceeds the top turning speed"), Trace.MaxRate <= Settings.MaxDegreesPerSecond + RateToleranceDegreesPerSecond);
		TestTrue(TEXT("The turn does not overshoot the new facing"), Trace.Overshoot <= OvershootToleranceDegrees);
		TestTrue(TEXT("A 150 degree turn settles within 0.45 s"), Trace.SettleSeconds >= 0.f && Trace.SettleSeconds <= 0.45f);
		TestTrue(TEXT("The turn ends on the requested facing"), Trace.FinalError <= 0.05f);
		if (FramesPerSecond >= 60.f)
		{
			TestTrue(TEXT("Turn speed changes stay gradual between frames"), Trace.MaxRateJump < RateJumpLimitDegreesPerSecond);
		}
		EarliestSettle = FMath::Min(EarliestSettle, Trace.SettleSeconds);
		LatestSettle = FMath::Max(LatestSettle, Trace.SettleSeconds);
	}
	AddInfo(FString::Printf(TEXT("Settle spread across frame rates %.3f s"), LatestSettle - EarliestSettle));
	TestTrue(TEXT("Settle time does not depend on the frame rate beyond one coarse frame"), LatestSettle - EarliestSettle <= 0.06f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingSteppedTargetKeepsTurningTest, "TDGame.MonsterAI.TurnSmoothing.SteppedTargetKeepsTurning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingSteppedTargetKeepsTurningTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	const FTDTurnSmoothingSettings Settings;
	for (const float FramesPerSecond : { 30.f, 60.f, 144.f })
	{
		const FTDTurnTrace Trace = TraceTurn(&SteppedTargetYaw, FramesPerSecond, 180.f);
		AddInfo(DescribeTrace(Trace, FramesPerSecond));
		TestEqual(TEXT("The body keeps turning on frames between target updates"), Trace.StallFrames, 0);
		TestTrue(TEXT("The turn never exceeds the top turning speed"), Trace.MaxRate <= Settings.MaxDegreesPerSecond + RateToleranceDegreesPerSecond);
		TestTrue(TEXT("The turn does not overshoot"), Trace.Overshoot <= OvershootToleranceDegrees);
		TestTrue(TEXT("The turn settles within 0.25 s after the last target update"), Trace.SettleSeconds >= 0.f && Trace.SettleSeconds <= 0.25f);
		if (FramesPerSecond >= 60.f)
		{
			TestTrue(TEXT("Turn speed changes stay gradual between frames"), Trace.MaxRateJump < RateJumpLimitDegreesPerSecond);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingRotatingTargetHasBoundedLagTest, "TDGame.MonsterAI.TurnSmoothing.RotatingTargetHasBoundedLag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingRotatingTargetHasBoundedLagTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	const FTDTurnTrace Trace = TraceTurn(&RotatingTargetYaw, 60.f, 180.f);
	AddInfo(DescribeTrace(Trace, 60.f));
	TestTrue(TEXT("Tracking a turning target starts and stops without a speed jump"), Trace.MaxRateJump < RateJumpLimitDegreesPerSecond);
	TestTrue(TEXT("The body stays within 25 degrees of a target turning at 360 deg/s"), Trace.MaxLag <= 25.f);
	TestTrue(TEXT("The turn does not overshoot"), Trace.Overshoot <= OvershootToleranceDegrees);
	TestTrue(TEXT("The turn settles within 0.25 s after the target stops"), Trace.SettleSeconds >= 0.f && Trace.SettleSeconds <= 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingReversedTargetBrakesGraduallyTest, "TDGame.MonsterAI.TurnSmoothing.ReversedTargetBrakesGradually", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingReversedTargetBrakesGraduallyTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	const FTDTurnSmoothingSettings Settings;
	for (const float FramesPerSecond : { 30.f, 60.f, 144.f })
	{
		const FTDTurnTrace Trace = TraceTurn(&ReversedTargetYaw, FramesPerSecond, -30.f);
		AddInfo(DescribeTrace(Trace, FramesPerSecond));
		TestTrue(TEXT("The reversed turn never exceeds the top turning speed"), Trace.MaxRate <= Settings.MaxDegreesPerSecond + RateToleranceDegreesPerSecond);
		TestTrue(TEXT("The reversed turn does not overshoot the new facing"), Trace.Overshoot <= OvershootToleranceDegrees);
		TestTrue(TEXT("The reversed turn settles within 0.55 s"), Trace.SettleSeconds >= 0.f && Trace.SettleSeconds <= 0.55f);
		TestTrue(TEXT("The reversed turn ends on the new facing"), Trace.FinalError <= 0.05f);
		if (FramesPerSecond >= 60.f)
		{
			TestTrue(TEXT("Braking through a reversal stays gradual between frames"), Trace.MaxRateJump < RateJumpLimitDegreesPerSecond);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingStopHasNoBackSwingTest, "TDGame.MonsterAI.TurnSmoothing.StopHasNoBackSwing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingStopHasNoBackSwingTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	FTDTurnSmoothingSettings Settings;
	for (const float FramesPerSecond : { 30.f, 60.f, 144.f })
	{
		FTDTurnSmoother Smoother;
		Smoother.Reset(0.f);
		const float FrameSeconds = 1.f / FramesPerSecond;
		Smoother.Advance(150.f, 0.15f, Settings);
		const float StartYaw = Smoother.GetYaw();
		const float StartRate = Smoother.GetYawRate();
		float PreviousYaw = StartYaw;
		float LargestBackStep = 0.f;
		float SecondsToStop = -1.f;
		for (int32 FrameIndex = 1; FrameIndex <= FMath::RoundToInt(0.6f * FramesPerSecond); ++FrameIndex)
		{
			const float Yaw = Smoother.Advance(Smoother.ComputeStopYaw(Settings), FrameSeconds, Settings);
			LargestBackStep = FMath::Max(LargestBackStep, -FMath::Sign(StartRate) * FRotator::NormalizeAxis(Yaw - PreviousYaw));
			SecondsToStop = SecondsToStop < 0.f && FMath::Abs(Smoother.GetYawRate()) < 1.f ? FrameIndex * FrameSeconds : SecondsToStop;
			PreviousYaw = Yaw;
		}
		const float ExtraTurn = FMath::Abs(FRotator::NormalizeAxis(PreviousYaw - StartYaw));
		AddInfo(FString::Printf(TEXT("%.0f fps: stop from %.1f deg/s, extra turn %.2f deg, largest back step %.4f deg, stopped after %.3f s"), FramesPerSecond, StartRate, ExtraTurn, LargestBackStep, SecondsToStop));
		TestTrue(TEXT("The stop begins while the body is still turning"), FMath::Abs(StartRate) > 100.f);
		TestTrue(TEXT("Stopping never swings the body back"), LargestBackStep <= 1e-3f);
		TestTrue(TEXT("Stopping coasts no further than the eased stopping distance"), ExtraTurn <= FMath::Abs(StartRate) * Settings.SmoothingSeconds * 0.5f + 0.5f);
		TestTrue(TEXT("The turn comes to rest within 0.3 s"), SecondsToStop >= 0.f && SecondsToStop <= 0.3f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingCapsuleEasesTowardFacingTest, "TDGame.MonsterAI.TurnSmoothing.CapsuleEasesTowardFacing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingCapsuleEasesTowardFacingTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	FTDCapsuleTurnTrace SmoothTrace;
	FTDCapsuleTurnTrace PlainTrace;
	for (const int32 SmoothTurnValue : { 1, 0 })
	{
		const FTDScopedSmoothTurnValue SmoothTurn(SmoothTurnValue);
		if (!TestTrue(TEXT("Smooth turn console variable takes the test value"), SmoothTurn.Holds(SmoothTurnValue)))
		{
			return false;
		}
		FTDScopedTurnWorld Fixture;
		ATDMonsterCharacter* Monster = Fixture.SpawnMonster();
		if (!TestNotNull(TEXT("Monster pawn spawns"), Monster))
		{
			return false;
		}
		Fixture.TickFrames(18);
		Monster->SetDesiredFacingDirection(FRotator(0.f, 150.f, 0.f).Vector());
		(SmoothTurnValue == 1 ? SmoothTrace : PlainTrace) = TraceCapsuleTurn(Fixture, *Monster, 150.f, 60);
	}
	AddInfo(TEXT("Smooth: ") + DescribeCapsuleTrace(SmoothTrace));
	AddInfo(TEXT("Legacy: ") + DescribeCapsuleTrace(PlainTrace));
	TestTrue(TEXT("The capsule never turns faster than the monster turning rate"), SmoothTrace.MaxFrameTurn <= MonsterTurningRate * WorldFrameSeconds + 0.5f);
	TestTrue(TEXT("The capsule speeds up and slows down gradually"), SmoothTrace.MaxRateJump < RateJumpLimitDegreesPerSecond);
	TestTrue(TEXT("The mesh follows the capsule exactly"), SmoothTrace.MaxMeshFollowError <= 0.01f);
	TestTrue(TEXT("The capsule reaches the facing within 5 degrees in 0.45 s"), SmoothTrace.SecondsToWithinFiveDegrees >= 0.f && SmoothTrace.SecondsToWithinFiveDegrees <= 0.45f);
	TestTrue(TEXT("The capsule ends on the facing"), SmoothTrace.FinalError <= 0.5f);
	TestTrue(TEXT("Legacy turning still starts at full speed"), PlainTrace.MaxRateJump >= MonsterTurningRate * 0.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingAbilityStartEasesTowardAimTest, "TDGame.MonsterAI.TurnSmoothing.AbilityStartEasesTowardAim", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingAbilityStartEasesTowardAimTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	float SmoothFirstFrameTurn = -1.f;
	float PlainFirstFrameTurn = -1.f;
	FTDCapsuleTurnTrace SmoothTrace;
	for (const int32 SmoothTurnValue : { 1, 0 })
	{
		const FTDScopedSmoothTurnValue SmoothTurn(SmoothTurnValue);
		if (!TestTrue(TEXT("Smooth turn console variable takes the test value"), SmoothTurn.Holds(SmoothTurnValue)))
		{
			return false;
		}
		FTDScopedTurnWorld Fixture;
		ATDMonsterCharacter* Monster = Fixture.SpawnMonster();
		if (!TestNotNull(TEXT("Monster pawn spawns"), Monster))
		{
			return false;
		}
		Fixture.TickFrames(18);
		UTDDamageDefinition* Spell = NewObject<UTDDamageDefinition>(GetTransientPackage());
		Spell->Mode = ETDDamageEntityMode::Area;
		Spell->AreaShape = ETDDamageAreaShape::Circle;
		Spell->Radius = AbilityRadius;
		Spell->ActivationDelay = AbilityWindupSeconds;
		FTDMonsterAbilityRequest Request;
		Request.Spell = Spell;
		Request.Range = AbilityRadius;
		Request.TargetLocation = Monster->GetActorLocation() + FRotator(0.f, 150.f, 0.f).Vector() * AbilityRadius;
		Request.WindupSeconds = AbilityWindupSeconds;
		const float YawBefore = ReadYaw(*Monster);
		if (!TestTrue(TEXT("The ability starts"), Monster->BeginBodyAbility(Request) > 0.f))
		{
			return false;
		}
		Fixture.TickFrames(1);
		const float FirstFrameTurn = FMath::Abs(FRotator::NormalizeAxis(ReadYaw(*Monster) - YawBefore));
		if (SmoothTurnValue == 1)
		{
			SmoothFirstFrameTurn = FirstFrameTurn;
			SmoothTrace = TraceCapsuleTurn(Fixture, *Monster, 150.f, 35);
		}
		else
		{
			PlainFirstFrameTurn = FirstFrameTurn;
		}
	}
	AddInfo(FString::Printf(TEXT("First frame turn after the ability starts: smooth %.2f deg, legacy %.2f deg. Smooth: %s"), SmoothFirstFrameTurn, PlainFirstFrameTurn, *DescribeCapsuleTrace(SmoothTrace)));
	TestTrue(TEXT("The ability start no longer snaps the capsule"), SmoothFirstFrameTurn <= MonsterTurningRate * WorldFrameSeconds + 0.5f);
	TestTrue(TEXT("The capsule turns toward the aim during the windup"), SmoothTrace.SecondsToWithinFiveDegrees >= 0.f && SmoothTrace.SecondsToWithinFiveDegrees + WorldFrameSeconds <= AbilityWindupSeconds);
	TestTrue(TEXT("The windup turn stays within the turning rate"), SmoothTrace.MaxFrameTurn <= MonsterTurningRate * WorldFrameSeconds + 0.5f);
	TestTrue(TEXT("The legacy ability start still snaps to the aim"), FMath::Abs(PlainFirstFrameTurn - 150.f) <= 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingMoveDirectionTurnsAreEasedTest, "TDGame.MonsterAI.TurnSmoothing.MoveDirectionTurnsAreEased", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingMoveDirectionTurnsAreEasedTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	float SmoothLargestFrameTurn = 0.f;
	float PlainLargestFrameTurn = 0.f;
	const float SmoothRateJump = ScriptMoveTurnsLargestRateJump(1, &PausedTurnDirection, 200, SmoothLargestFrameTurn);
	const float PlainRateJump = ScriptMoveTurnsLargestRateJump(0, &PausedTurnDirection, 200, PlainLargestFrameTurn);
	AddInfo(FString::Printf(TEXT("Largest rate jump smooth %.1f deg/s (frame turn %.2f deg), legacy %.1f deg/s (frame turn %.2f deg)"), SmoothRateJump, SmoothLargestFrameTurn, PlainRateJump, PlainLargestFrameTurn));
	if (!TestTrue(TEXT("Both scripted runs record"), SmoothRateJump >= 0.f && PlainRateJump >= 0.f))
	{
		return false;
	}
	TestTrue(TEXT("The scripted run turns the monster"), SmoothLargestFrameTurn > 3.f);
	TestTrue(TEXT("Move direction changes turn the capsule without speed jumps"), SmoothRateJump < RateJumpLimitDegreesPerSecond);
	TestTrue(TEXT("The capsule never turns faster than the turning rate"), SmoothLargestFrameTurn <= MonsterTurningRate * WorldFrameSeconds + 0.5f);
	TestTrue(TEXT("Legacy turning jumps to full speed"), PlainRateJump >= MonsterTurningRate * 0.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingAlternatingMoveDirectionsBrakeGraduallyTest, "TDGame.MonsterAI.TurnSmoothing.AlternatingMoveDirectionsBrakeGradually", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingAlternatingMoveDirectionsBrakeGraduallyTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	float SmoothLargestFrameTurn = 0.f;
	float PlainLargestFrameTurn = 0.f;
	const float SmoothRateJump = ScriptMoveTurnsLargestRateJump(1, &AlternatingDirection, 160, SmoothLargestFrameTurn);
	const float PlainRateJump = ScriptMoveTurnsLargestRateJump(0, &AlternatingDirection, 160, PlainLargestFrameTurn);
	AddInfo(FString::Printf(TEXT("Direction reversed every %d frames: largest rate jump smooth %.1f deg/s (frame turn %.2f deg), legacy %.1f deg/s (frame turn %.2f deg)"), AlternatingDirectionFrames, SmoothRateJump, SmoothLargestFrameTurn, PlainRateJump, PlainLargestFrameTurn));
	if (!TestTrue(TEXT("Both scripted runs record"), SmoothRateJump >= 0.f && PlainRateJump >= 0.f))
	{
		return false;
	}
	TestTrue(TEXT("The scripted reversals turn the monster"), SmoothLargestFrameTurn > 3.f);
	TestTrue(TEXT("Reversing move directions brake the capsule gradually"), SmoothRateJump < RateJumpLimitDegreesPerSecond);
	TestTrue(TEXT("The capsule never turns faster than the turning rate"), SmoothLargestFrameTurn <= MonsterTurningRate * WorldFrameSeconds + 0.5f);
	TestTrue(TEXT("Legacy turning flips straight to full reverse speed"), PlainRateJump >= MonsterTurningRate * 1.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingExternalTurnResyncsTest, "TDGame.MonsterAI.TurnSmoothing.ExternalTurnResyncs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingExternalTurnResyncsTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	const FTDScopedSmoothTurnValue SmoothTurn(1);
	if (!TestTrue(TEXT("Smooth turning is on for the test"), SmoothTurn.Holds(1)))
	{
		return false;
	}
	FTDScopedTurnWorld Fixture;
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster();
	if (!TestNotNull(TEXT("Monster pawn spawns"), Monster))
	{
		return false;
	}
	Fixture.TickFrames(18);
	Monster->SetDesiredFacingDirection(FRotator(0.f, 150.f, 0.f).Vector());
	Fixture.TickFrames(8);
	const float YawMidTurn = ReadYaw(*Monster);
	Monster->FaceRotationImmediately(FRotator(0.f, -60.f, 0.f));
	float LargestDrift = 0.f;
	for (int32 FrameIndex = 0; FrameIndex < 30; ++FrameIndex)
	{
		Fixture.TickFrames(1);
		LargestDrift = FMath::Max(LargestDrift, FMath::Abs(FRotator::NormalizeAxis(ReadYaw(*Monster) + 60.f)));
	}
	AddInfo(FString::Printf(TEXT("Yaw mid-turn %.2f deg, largest drift from the instant facing %.3f deg"), YawMidTurn, LargestDrift));
	TestTrue(TEXT("The turn was under way before the instant facing"), YawMidTurn > 5.f && YawMidTurn < 145.f);
	TestTrue(TEXT("An instant facing from other code is kept without coasting or swinging back"), LargestDrift <= 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingFreezeHoldsTurnTest, "TDGame.MonsterAI.TurnSmoothing.FreezeHoldsTurn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingFreezeHoldsTurnTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	const FTDScopedSmoothTurnValue SmoothTurn(1);
	if (!TestTrue(TEXT("Smooth turning is on for the test"), SmoothTurn.Holds(1)))
	{
		return false;
	}
	FTDScopedTurnWorld Fixture;
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster();
	if (!TestNotNull(TEXT("Monster pawn spawns"), Monster))
	{
		return false;
	}
	Fixture.TickFrames(18);
	Monster->SetDesiredFacingDirection(FRotator(0.f, 150.f, 0.f).Vector());
	Fixture.TickFrames(8);
	const float YawBeforeFreeze = ReadYaw(*Monster);

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
	const float YawWhileFrozen = ReadYaw(*Monster);
	AddInfo(FString::Printf(TEXT("Yaw before freeze %.2f deg, while frozen %.2f deg"), YawBeforeFreeze, YawWhileFrozen));
	TestTrue(TEXT("The turn was under way when the freeze hit"), YawBeforeFreeze > 5.f && YawBeforeFreeze < 145.f);
	TestTrue(TEXT("The frozen capsule keeps its facing"), FMath::Abs(FRotator::NormalizeAxis(YawWhileFrozen - YawBeforeFreeze)) <= 0.5f);

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
	const FTDCapsuleTurnTrace ThawTrace = TraceCapsuleTurn(Fixture, *Monster, 150.f, 60);
	AddInfo(FString::Printf(TEXT("Thaw after %d frames: %s"), WaitedFrames, *DescribeCapsuleTrace(ThawTrace)));
	TestTrue(TEXT("The capsule does not jump when the freeze ends"), ThawTrace.MaxFrameTurn <= MonsterTurningRate * WorldFrameSeconds + 0.5f);
	TestTrue(TEXT("The capsule restarts the turn gradually after the freeze"), ThawTrace.MaxRateJump < RateJumpLimitDegreesPerSecond);
	TestTrue(TEXT("The capsule finishes the turn after the freeze"), ThawTrace.FinalError <= 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDTurnSmoothingDuelComparisonTest, "TDGame.MonsterAI.TurnSmoothing.DuelComparison", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDTurnSmoothingDuelComparisonTest::RunTest(const FString& Parameters)
{
	using namespace TDTurnSmoothingTestSupport;
	IgnoreEngineInjectionEnsures(*this);
	const FName Species[] = { TEXT("Goblin_Melee"), TEXT("Swordsman"), TEXT("Wukong"), TEXT("Kwang"), TEXT("Griffon"), TEXT("Greystone"), TEXT("Barghest") };
	const FVector TargetLocations[] = { FVector(500.f, 0.f, 0.f), FVector(0.f, 500.f, 0.f), FVector(-450.f, 150.f, 0.f) };
	float LargestSmoothStepTurn = 0.f;
	int32 LegacySnapCount = 0;
	for (const FName DefinitionId : Species)
	{
		RunDuel(DefinitionId, TargetLocations[0], 1, DuelWarmUpSeconds);
		int32 SmoothHits = 0;
		int32 LegacyHits = 0;
		for (int32 PlacementIndex = 0; PlacementIndex < static_cast<int32>(UE_ARRAY_COUNT(TargetLocations)); ++PlacementIndex)
		{
			const bool bRunsLegacyFirst = PlacementIndex % 2 == 1;
			const FTDDuelRecord First = RunDuel(DefinitionId, TargetLocations[PlacementIndex], bRunsLegacyFirst ? 0 : 1, DuelSeconds);
			const FTDDuelRecord Second = RunDuel(DefinitionId, TargetLocations[PlacementIndex], bRunsLegacyFirst ? 1 : 0, DuelSeconds);
			const FTDDuelRecord& Smooth = bRunsLegacyFirst ? Second : First;
			const FTDDuelRecord& Legacy = bRunsLegacyFirst ? First : Second;
			AddInfo(FString::Printf(TEXT("DUEL %s placement %d | smooth first hit %.3f s hits %d damage %.1f max step turn %.2f snaps %d | legacy first hit %.3f s hits %d damage %.1f max step turn %.2f snaps %d"),
				*DefinitionId.ToString(), PlacementIndex, Smooth.FirstHitSeconds, Smooth.HitCount, Smooth.Damage, Smooth.MaxStepTurn, Smooth.SnapCount,
				Legacy.FirstHitSeconds, Legacy.HitCount, Legacy.Damage, Legacy.MaxStepTurn, Legacy.SnapCount));
			LargestSmoothStepTurn = FMath::Max(LargestSmoothStepTurn, Smooth.MaxStepTurn);
			LegacySnapCount += Legacy.SnapCount;
			SmoothHits += Smooth.HitCount;
			LegacyHits += Legacy.HitCount;
		}
		if (LegacyHits > 0)
		{
			TestTrue(FString::Printf(TEXT("%s still lands hits with smooth turning"), *DefinitionId.ToString()), SmoothHits > 0);
		}
	}
	AddInfo(FString::Printf(TEXT("Largest smooth step turn %.2f deg, legacy snaps %d"), LargestSmoothStepTurn, LegacySnapCount));
	TestTrue(TEXT("Smooth turning never snaps the capsule in a fight"), LargestSmoothStepTurn <= MonsterTurningRate * DuelStepSeconds + 0.5f);
	TestTrue(TEXT("The legacy fights contain instant facings the comparison can see"), LegacySnapCount > 0);
	return true;
}

#endif
