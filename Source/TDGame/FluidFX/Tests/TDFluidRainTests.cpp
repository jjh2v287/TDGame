#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidInputBrushRules.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidRainImpacts.h"
#include "FluidFX/TDFluidRainTypes.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterWetnessRules.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Tickable.h"
#include <limits>

namespace TDFluidRainTestPrivate
{
	struct FTDFluidRainTestWorldScope
	{
		FTDFluidRainTestWorldScope()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidRainTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidRainTestWorldScope()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void TickWorldOnce(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
		}

		UWorld* World = nullptr;
	};

	FTDFluidMassInputSettings MakeMassInputSettings(bool bAcceptMassInput)
	{
		FTDFluidMassInputSettings Settings;
		Settings.bAcceptMassInput = bAcceptMassInput;
		return Settings;
	}

	FTDFluidMassInputPoint MakeDefaultFieldPoint()
	{
		FTDFluidMassInputPoint Point;
		Point.WorldLocation = FVector::ZeroVector;
		Point.WorldVelocity = FVector(120.0, -40.0, 0.0);
		Point.RadiusCm = 30.0f;
		Point.DensityStrength = 0.8f;
		Point.HeatStrength = 0.25f;
		return Point;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainMassPointDefaultsKeepFx5Test,
	"TDGame.FluidFX.Rain.MassPointDefaultsKeepFx5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainMassPointDefaultsKeepFx5Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	FTDFluidRainTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	if (!TestNotNull(TEXT("Fluid subsystem exists in the test world"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	if (!TestNotNull(TEXT("A fluid actor is spawned"), LiveActor))
	{
		return false;
	}

	TestWorld.TickWorldOnce(0.05f);
	UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
	if (!TestTrue(TEXT("The CPU solver is initialized"), LiveComponent->IsSimulationInitialized() && !LiveComponent->IsUsingGpuSolver()))
	{
		return false;
	}

	const FTDFluidInputBrushSettings& BrushSettings = LiveComponent->GetInputBrushSettings();
	const FTDFluidMassInputPoint FieldPoint = MakeDefaultFieldPoint();
	LiveComponent->SetMassInputSettingsForTests(MakeMassInputSettings(true));
	const int32 AcceptedStartCount = LiveComponent->GetPendingBrushStamps().Num();
	LiveComponent->QueueMassInputPoints(MakeArrayView(&FieldPoint, 1));
	if (!TestEqual(TEXT("A default field point queues one stamp"), LiveComponent->GetPendingBrushStamps().Num(), AcceptedStartCount + 1))
	{
		return false;
	}

	const FTDFluidBrushStamp FieldStamp = LiveComponent->GetPendingBrushStamps().Last();
	const ETDFluidBrushChannel ExpectedChannel = BloodPreset->DefaultChannel;
	TestTrue(TEXT("Field stamp keeps the point location"), FieldStamp.WorldLocation == FieldPoint.WorldLocation);
	TestTrue(TEXT("Field stamp keeps the point velocity"), FieldStamp.WorldVelocity == FieldPoint.WorldVelocity);
	TestTrue(TEXT("Field stamp radius keeps the FX-5 formula"), FieldStamp.RadiusCm == FMath::Max(FieldPoint.RadiusCm, 0.0f) * BrushSettings.MassPointRadiusScale);
	TestTrue(TEXT("Field stamp density keeps the FX-5 formula"), FieldStamp.DensityStrength == FieldPoint.DensityStrength * BrushSettings.MassPointDensityStrength);
	TestTrue(TEXT("Field stamp velocity strength keeps the FX-5 value"), FieldStamp.VelocityStrength == BrushSettings.MassPointVelocityStrength);
	TestTrue(TEXT("Field stamp heat keeps the FX-5 formula"), FieldStamp.HeatStrength == FieldPoint.HeatStrength * BrushSettings.MassPointHeatStrength);
	TestTrue(TEXT("Field stamp ripple stays zero"), FieldStamp.RippleImpulseCm == 0.0f);
	TestTrue(TEXT("Field stamp uses the preset channel"), FieldStamp.Channel == ExpectedChannel);
	TestTrue(TEXT("Field stamp source stays the data channel"), FieldStamp.Source == ETDFluidBrushSource::DataChannel);

	LiveComponent->SetMassInputSettingsForTests(MakeMassInputSettings(false));
	const int32 RefusedStartCount = LiveComponent->GetPendingBrushStamps().Num();
	LiveComponent->QueueMassInputPoints(MakeArrayView(&FieldPoint, 1));
	TestEqual(TEXT("A data channel point is refused while mass input is off"), LiveComponent->GetPendingBrushStamps().Num(), RefusedStartCount);

	FTDFluidMassInputPoint RainPoint = FieldPoint;
	RainPoint.RippleImpulseCm = 0.5f;
	RainPoint.VelocityStrengthScale = 0.0f;
	LiveComponent->QueueMassInputPoints(MakeArrayView(&RainPoint, 1), ETDFluidBrushSource::Rain);
	if (!TestEqual(TEXT("A rain point enters while mass input is off"), LiveComponent->GetPendingBrushStamps().Num(), RefusedStartCount + 1))
	{
		return false;
	}

	const FTDFluidBrushStamp RainStamp = LiveComponent->GetPendingBrushStamps().Last();
	TestTrue(TEXT("Rain stamp keeps the rain source"), RainStamp.Source == ETDFluidBrushSource::Rain);
	TestTrue(TEXT("Rain stamp velocity strength follows the point scale"), RainStamp.VelocityStrength == BrushSettings.MassPointVelocityStrength * RainPoint.VelocityStrengthScale);
	TestTrue(TEXT("Rain stamp ripple adds the point ripple"), RainStamp.RippleImpulseCm == RainPoint.RippleImpulseCm + BrushSettings.MassPointRippleImpulseCm);
	TestTrue(TEXT("Rain stamp density keeps the FX-5 formula"), RainStamp.DensityStrength == RainPoint.DensityStrength * BrushSettings.MassPointDensityStrength);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainVelocityOnlyAllIncludesRainTest,
	"TDGame.FluidFX.Rain.VelocityOnlyAllIncludesRain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainVelocityOnlyAllIncludesRainTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("All selection includes rain"), FTDFluidInputBrushRules::IsVelocityOnlySource(ETDFluidVelocityOnlySources::All, ETDFluidBrushSource::Rain));
	TestFalse(TEXT("Data channel selection excludes rain"), FTDFluidInputBrushRules::IsVelocityOnlySource(ETDFluidVelocityOnlySources::DataChannel, ETDFluidBrushSource::Rain));
	return true;
}

namespace TDFluidRainTestPrivate
{
	struct FTDFluidRainHashReference
	{
		uint32 Input = 0;
		uint32 Expected = 0;
	};

	struct FTDFluidRainCombineReference
	{
		uint32 First = 0;
		uint32 Second = 0;
		uint32 Third = 0;
		uint32 Expected = 0;
	};

	const FVector RainWindowCenterWorld(1000.0, -500.0, 20.0);
	const FVector2D RainWindowExtentCm(1000.0, 1000.0);
	constexpr int32 RainUniformCellsPerAxis = 4;
	constexpr int32 RainUniformPointCount = 10000;
	constexpr float RainCollectStepSeconds = 0.0125f;

	float MakeMixedDeltaSeconds(int32 StepIndex)
	{
		return static_cast<float>(1 + (StepIndex * 5) % 7) / 240.0f;
	}

	float MakeQuietNaN()
	{
		return std::numeric_limits<float>::quiet_NaN();
	}

	FTDFluidRainSettings MakeDenseRainSettings(int32 RandomSeed)
	{
		FTDFluidRainSettings Settings;
		Settings.bEnabled = true;
		Settings.DropsPerSquareMeterPerSec = 400.0f;
		Settings.MaxDropsPerTick = 512;
		Settings.DropRadiusCm = 6.0f;
		Settings.DropDensityStrength = 0.3f;
		Settings.DropRippleImpulseCm = 0.5f;
		Settings.RandomSeed = RandomSeed;
		return Settings;
	}

	TArray<FTDFluidMassInputPoint> CollectRainPoints(const FTDFluidRainSettings& Settings, int32 PointCount)
	{
		FTDFluidRainImpactState State;
		TArray<FTDFluidMassInputPoint> StepPoints;
		TArray<FTDFluidMassInputPoint> Points;
		for (int32 StepIndex = 0; StepIndex < 1000 && Points.Num() < PointCount; ++StepIndex)
		{
			FTDFluidRainImpacts::GenerateImpacts(Settings, 1.0f, RainCollectStepSeconds, RainWindowCenterWorld, RainWindowExtentCm, Settings.MaxDropsPerTick, State, StepPoints);
			Points.Append(StepPoints);
		}

		Points.SetNum(FMath::Min(Points.Num(), PointCount));
		return Points;
	}

	bool ArePointsBitIdentical(const TArray<FTDFluidMassInputPoint>& FirstPoints, const TArray<FTDFluidMassInputPoint>& SecondPoints)
	{
		if (FirstPoints.Num() != SecondPoints.Num())
		{
			return false;
		}

		for (int32 PointIndex = 0; PointIndex < FirstPoints.Num(); ++PointIndex)
		{
			const FTDFluidMassInputPoint& FirstPoint = FirstPoints[PointIndex];
			const FTDFluidMassInputPoint& SecondPoint = SecondPoints[PointIndex];
			if (FirstPoint.WorldLocation != SecondPoint.WorldLocation
				|| FirstPoint.RadiusCm != SecondPoint.RadiusCm
				|| FirstPoint.DensityStrength != SecondPoint.DensityStrength
				|| FirstPoint.RippleImpulseCm != SecondPoint.RippleImpulseCm)
			{
				return false;
			}
		}

		return true;
	}

	int32 CountSharedLocations(const TArray<FTDFluidMassInputPoint>& FirstPoints, const TArray<FTDFluidMassInputPoint>& SecondPoints)
	{
		int32 SharedCount = 0;
		const int32 PointCount = FMath::Min(FirstPoints.Num(), SecondPoints.Num());
		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			SharedCount += FirstPoints[PointIndex].WorldLocation == SecondPoints[PointIndex].WorldLocation ? 1 : 0;
		}

		return SharedCount;
	}

	int32 FindUniformCellCoordinate(double Unit)
	{
		return FMath::Clamp(static_cast<int32>(FMath::FloorToDouble(Unit * RainUniformCellsPerAxis)), 0, RainUniformCellsPerAxis - 1);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainHashMatchesReferenceTest,
	"TDGame.FluidFX.Rain.HashMatchesReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainHashMatchesReferenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	const FTDFluidRainHashReference HashReferences[] = {
		{ 0u, 129708002u },
		{ 1u, 2831084092u },
		{ 0x9E3779B9u, 2419168863u },
		{ 0x85EBCA6Bu, 2306120883u },
		{ 0xFFFFFFFFu, 3861530882u }
	};
	for (const FTDFluidRainHashReference& Reference : HashReferences)
	{
		TestEqual(*FString::Printf(TEXT("Hash of %u matches the reference"), Reference.Input), static_cast<int64>(FTDFluidRainImpacts::HashUint32(Reference.Input)), static_cast<int64>(Reference.Expected));
	}

	const FTDFluidRainCombineReference CombineReferences[] = {
		{ 1u, 0u, 0x9E3779B9u, 3966961912u },
		{ 1u, 0u, 0x85EBCA6Bu, 2171200257u },
		{ 2u, 12345u, 0x9E3779B9u, 4285183621u }
	};
	for (const FTDFluidRainCombineReference& Reference : CombineReferences)
	{
		TestEqual(*FString::Printf(TEXT("Combined hash of %u, %u, %u matches the reference"), Reference.First, Reference.Second, Reference.Third), static_cast<int64>(FTDFluidRainImpacts::HashCombine3(Reference.First, Reference.Second, Reference.Third)), static_cast<int64>(Reference.Expected));
	}

	TestTrue(TEXT("Unit of zero is zero"), FTDFluidRainImpacts::HashToUnitFloat(0u) == 0.0f);
	TestTrue(TEXT("Unit of the largest hash keeps the top 24 bits"), FTDFluidRainImpacts::HashToUnitFloat(0xFFFFFFFFu) == 1.0f - 1.0f / 16777216.0f);
	bool bAllUnitsInRange = true;
	for (uint32 Value = 0; Value < 4096u; ++Value)
	{
		const float Unit = FTDFluidRainImpacts::HashToUnitFloat(FTDFluidRainImpacts::HashUint32(Value));
		bAllUnitsInRange &= Unit >= 0.0f && Unit < 1.0f;
	}
	TestTrue(TEXT("Hashed units stay in [0, 1)"), bAllUnitsInRange);
	TestTrue(TEXT("Horizontal and vertical salts give different hashes"), FTDFluidRainImpacts::HashCombine3(1u, 0u, 0x9E3779B9u) != FTDFluidRainImpacts::HashCombine3(1u, 0u, 0x85EBCA6Bu));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainImpactCountConservesRateTest,
	"TDGame.FluidFX.Rain.ImpactCountConservesRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainImpactCountConservesRateTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	FTDFluidRainSettings Settings;
	Settings.bEnabled = true;
	Settings.DropsPerSquareMeterPerSec = 60.0f;
	Settings.MaxDropsPerTick = 512;
	const float Intensity = 0.75f;
	const FVector2D ExtentCm(600.0, 600.0);
	const int32 CapCount = 20;
	const double DropRatePerSec = static_cast<double>(Settings.DropsPerSquareMeterPerSec) * Intensity * ExtentCm.X * ExtentCm.Y * 1.0e-4;

	FTDFluidRainImpactState OpenState;
	FTDFluidRainImpactState CappedState;
	TArray<FTDFluidMassInputPoint> Points;
	double ExpectedDropCount = 0.0;
	double ElapsedSeconds = 0.0;
	int64 OpenDropCount = 0;
	int64 CappedQueuedCount = 0;
	int64 CappedDiscardedCount = 0;
	bool bReturnedCountsMatchPoints = true;
	bool bCapRespected = true;
	for (int32 StepIndex = 0; StepIndex < 600; ++StepIndex)
	{
		const float DeltaSeconds = MakeMixedDeltaSeconds(StepIndex);
		ExpectedDropCount += DropRatePerSec * DeltaSeconds;
		ElapsedSeconds += DeltaSeconds;

		const int32 OpenCount = FTDFluidRainImpacts::GenerateImpacts(Settings, Intensity, DeltaSeconds, RainWindowCenterWorld, ExtentCm, Settings.MaxDropsPerTick, OpenState, Points);
		bReturnedCountsMatchPoints &= Points.Num() == OpenCount && OpenState.LastQueuedDropCount == OpenCount && OpenState.LastCappedDropCount == 0;
		OpenDropCount += OpenCount;

		const int32 CappedCount = FTDFluidRainImpacts::GenerateImpacts(Settings, Intensity, DeltaSeconds, RainWindowCenterWorld, ExtentCm, CapCount, CappedState, Points);
		bReturnedCountsMatchPoints &= Points.Num() == CappedCount && CappedState.LastQueuedDropCount == CappedCount;
		bCapRespected &= CappedCount <= CapCount;
		CappedQueuedCount += CappedCount;
		CappedDiscardedCount += CappedState.LastCappedDropCount;
	}

	const int64 ExpectedWholeDropCount = static_cast<int64>(FMath::FloorToDouble(ExpectedDropCount));
	TestTrue(TEXT("Returned counts match the generated points"), bReturnedCountsMatchPoints);
	TestTrue(*FString::Printf(TEXT("Total drops %lld follow the floor of rate times time %lld within one"), OpenDropCount, ExpectedWholeDropCount), FMath::Abs(OpenDropCount - ExpectedWholeDropCount) <= 1);
	TestTrue(TEXT("The cap is never exceeded"), bCapRespected);
	TestTrue(TEXT("The cap discards some drops"), CappedDiscardedCount > 0);
	TestEqual(TEXT("Queued plus discarded drops equal the uncapped total"), CappedQueuedCount + CappedDiscardedCount, OpenDropCount);
	TestEqual(TEXT("The drop counter counts only queued drops"), static_cast<int64>(CappedState.DropCounter), CappedQueuedCount);
	TestTrue(TEXT("The rain clock follows the steps"), FMath::IsNearlyEqual(OpenState.ClockSeconds, ElapsedSeconds, 1.0e-9));

	FTDFluidRainImpactState IdleState = OpenState;
	TestEqual(TEXT("A zero step queues no drops"), FTDFluidRainImpacts::GenerateImpacts(Settings, Intensity, 0.0f, RainWindowCenterWorld, ExtentCm, CapCount, IdleState, Points), 0);
	TestTrue(TEXT("A zero step keeps the carry"), IdleState.ExpectedDropCarry == OpenState.ExpectedDropCarry);
	TestEqual(TEXT("Zero intensity queues no drops"), FTDFluidRainImpacts::GenerateImpacts(Settings, 0.0f, 1.0f, RainWindowCenterWorld, ExtentCm, CapCount, IdleState, Points), 0);
	TestEqual(TEXT("NaN intensity queues no drops"), FTDFluidRainImpacts::GenerateImpacts(Settings, MakeQuietNaN(), 1.0f, RainWindowCenterWorld, ExtentCm, CapCount, IdleState, Points), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainImpactsDeterministicAndUniformTest,
	"TDGame.FluidFX.Rain.ImpactsDeterministicAndUniform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainImpactsDeterministicAndUniformTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	const FTDFluidRainSettings Settings = MakeDenseRainSettings(1);
	const TArray<FTDFluidMassInputPoint> FirstPoints = CollectRainPoints(Settings, RainUniformPointCount);
	const TArray<FTDFluidMassInputPoint> SecondPoints = CollectRainPoints(Settings, RainUniformPointCount);
	if (!TestEqual(TEXT("Enough drops are generated"), FirstPoints.Num(), RainUniformPointCount))
	{
		return false;
	}

	TestTrue(TEXT("Two runs are bit identical"), ArePointsBitIdentical(FirstPoints, SecondPoints));

	const double MinXCm = RainWindowCenterWorld.X - 0.5 * RainWindowExtentCm.X;
	const double MinYCm = RainWindowCenterWorld.Y - 0.5 * RainWindowExtentCm.Y;
	TArray<int32> CellCounts;
	CellCounts.Init(0, RainUniformCellsPerAxis * RainUniformCellsPerAxis);
	bool bPointsInsideWindow = true;
	bool bPointFieldsFollowSettings = true;
	int32 DiagonalPointCount = 0;
	for (const FTDFluidMassInputPoint& Point : FirstPoints)
	{
		const double HorizontalUnit = (Point.WorldLocation.X - MinXCm) / RainWindowExtentCm.X;
		const double VerticalUnit = (Point.WorldLocation.Y - MinYCm) / RainWindowExtentCm.Y;
		bPointsInsideWindow &= HorizontalUnit >= 0.0 && HorizontalUnit < 1.0 && VerticalUnit >= 0.0 && VerticalUnit < 1.0 && Point.WorldLocation.Z == RainWindowCenterWorld.Z;
		bPointFieldsFollowSettings &= Point.RadiusCm == Settings.DropRadiusCm
			&& Point.DensityStrength == Settings.DropDensityStrength
			&& Point.RippleImpulseCm == Settings.DropRippleImpulseCm
			&& Point.HeatStrength == 0.0f
			&& Point.VelocityStrengthScale == 0.0f
			&& Point.WorldVelocity.IsZero();
		DiagonalPointCount += FMath::Abs(HorizontalUnit - VerticalUnit) < 0.01 ? 1 : 0;
		++CellCounts[FindUniformCellCoordinate(VerticalUnit) * RainUniformCellsPerAxis + FindUniformCellCoordinate(HorizontalUnit)];
	}

	TestTrue(TEXT("Drops stay inside the window at the window height"), bPointsInsideWindow);
	TestTrue(TEXT("Drop points follow the settings and write no velocity"), bPointFieldsFollowSettings);
	const double ExpectedPerCell = static_cast<double>(RainUniformPointCount) / CellCounts.Num();
	for (int32 CellIndex = 0; CellIndex < CellCounts.Num(); ++CellIndex)
	{
		TestTrue(*FString::Printf(TEXT("Cell %d holds %d drops within 15%% of %.1f"), CellIndex, CellCounts[CellIndex], ExpectedPerCell), FMath::Abs(CellCounts[CellIndex] - ExpectedPerCell) <= 0.15 * ExpectedPerCell);
	}
	TestTrue(*FString::Printf(TEXT("Horizontal and vertical positions are independent (%d near the diagonal)"), DiagonalPointCount), DiagonalPointCount < RainUniformPointCount / 20);

	const TArray<FTDFluidMassInputPoint> OtherSeedPoints = CollectRainPoints(MakeDenseRainSettings(2), RainUniformPointCount);
	const int32 SharedLocationCount = CountSharedLocations(FirstPoints, OtherSeedPoints);
	TestTrue(*FString::Printf(TEXT("Seed 2 moves the drops (%d shared)"), SharedLocationCount), SharedLocationCount < RainUniformPointCount / 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainPuddleLevelIntegrationTest,
	"TDGame.FluidFX.Rain.PuddleLevelIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainPuddleLevelIntegrationTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	FTDFluidRainSettings Settings;
	Settings.bEnabled = true;
	Settings.bShouldAccumulatePuddles = true;
	Settings.PuddleFillCmPerSec = 0.1f;
	Settings.PuddleDrainCmPerSec = 0.03f;
	Settings.PuddleMaxLevelCm = 3.0f;

	const float FilledLevelCm = FTDFluidRainImpacts::AdvancePuddleLevel(Settings, 1.0f, 0.5f, 0.0f);
	TestEqual(TEXT("Full rain fills by fill minus drain"), FilledLevelCm, (Settings.PuddleFillCmPerSec - Settings.PuddleDrainCmPerSec) * 0.5f, 1.0e-6f);
	TestTrue(TEXT("Over-unit intensity fills like full rain"), FTDFluidRainImpacts::AdvancePuddleLevel(Settings, 5.0f, 0.5f, 0.0f) == FilledLevelCm);

	float LevelCm = 0.0f;
	bool bLevelNeverFalls = true;
	bool bLevelStaysUnderMax = true;
	for (int32 StepIndex = 0; StepIndex < 6000; ++StepIndex)
	{
		const float NextLevelCm = FTDFluidRainImpacts::AdvancePuddleLevel(Settings, 1.0f, 1.0f / 60.0f, LevelCm);
		bLevelNeverFalls &= NextLevelCm >= LevelCm;
		bLevelStaysUnderMax &= NextLevelCm <= Settings.PuddleMaxLevelCm;
		LevelCm = NextLevelCm;
	}
	TestTrue(TEXT("Steady rain never lowers the level"), bLevelNeverFalls);
	TestTrue(TEXT("The level never passes the max"), bLevelStaysUnderMax);
	TestTrue(TEXT("Long rain reaches the max level"), LevelCm == Settings.PuddleMaxLevelCm);

	const float DrainedLevelCm = FTDFluidRainImpacts::AdvancePuddleLevel(Settings, 0.0f, 1.0f, Settings.PuddleMaxLevelCm);
	TestEqual(TEXT("Dry weather drains by the drain rate"), DrainedLevelCm, Settings.PuddleMaxLevelCm - Settings.PuddleDrainCmPerSec, 1.0e-6f);
	TestTrue(TEXT("NaN intensity drains like dry weather"), FTDFluidRainImpacts::AdvancePuddleLevel(Settings, MakeQuietNaN(), 1.0f, Settings.PuddleMaxLevelCm) == DrainedLevelCm);

	bool bLevelNeverNegative = true;
	for (int32 StepIndex = 0; StepIndex < 200; ++StepIndex)
	{
		LevelCm = FTDFluidRainImpacts::AdvancePuddleLevel(Settings, 0.0f, 1.0f, LevelCm);
		bLevelNeverNegative &= LevelCm >= 0.0f;
	}
	TestTrue(TEXT("Draining never goes below zero"), bLevelNeverNegative);
	TestTrue(TEXT("Long dry weather empties the puddle"), LevelCm == 0.0f);

	const float WeakIntensity = 0.5f * Settings.PuddleDrainCmPerSec / Settings.PuddleFillCmPerSec;
	float WeakLevelCm = 0.0f;
	for (int32 StepIndex = 0; StepIndex < 600; ++StepIndex)
	{
		WeakLevelCm = FTDFluidRainImpacts::AdvancePuddleLevel(Settings, WeakIntensity, 1.0f / 60.0f, WeakLevelCm);
	}
	TestTrue(TEXT("Weak rain below the drain rate keeps the level at zero"), WeakLevelCm == 0.0f);
	TestTrue(TEXT("A zero step keeps the level"), FTDFluidRainImpacts::AdvancePuddleLevel(Settings, 1.0f, 0.0f, 1.25f) == 1.25f);

	FTDFluidRainSettings OffSettings = Settings;
	OffSettings.bShouldAccumulatePuddles = false;
	TestTrue(TEXT("Puddles off keeps the level at zero"), FTDFluidRainImpacts::AdvancePuddleLevel(OffSettings, 1.0f, 1.0f, 2.0f) == 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainPresetKindsAndDefaultsTest,
	"TDGame.FluidFX.Rain.PresetKindsAndDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainPresetKindsAndDefaultsTest::RunTest(const FString& Parameters)
{
	const FTDFluidRainSettings HeaderSettings;
	const FTDFluidRainSettings UnlistedSettings = UTDFluidPresetAsset::BuildRainForPresetKind(ETDFluidPresetKind::Torch);
	TestTrue(TEXT("A kind without rain values matches the header defaults"), FTDFluidRainSettings::StaticStruct()->CompareScriptStruct(&UnlistedSettings, &HeaderSettings, PPF_None));
	TestFalse(TEXT("Rain is off by default"), HeaderSettings.bEnabled);
	TestFalse(TEXT("Puddles are off by default"), HeaderSettings.bShouldAccumulatePuddles);

	const ETDFluidPresetKind WaterLayerKinds[] = { ETDFluidPresetKind::River, ETDFluidPresetKind::Sea, ETDFluidPresetKind::RainPond };
	for (const ETDFluidPresetKind Kind : WaterLayerKinds)
	{
		const FTDFluidRainSettings Settings = UTDFluidPresetAsset::BuildRainForPresetKind(Kind);
		TestTrue(*FString::Printf(TEXT("Water layer kind %d has rain on"), static_cast<int32>(Kind)), Settings.bEnabled);
		TestTrue(*FString::Printf(TEXT("Water layer kind %d accumulates puddles"), static_cast<int32>(Kind)), Settings.bShouldAccumulatePuddles);
	}

	const FTDFluidRainSettings RiverSettings = UTDFluidPresetAsset::BuildRainForPresetKind(ETDFluidPresetKind::River);
	const FTDFluidRainSettings SeaSettings = UTDFluidPresetAsset::BuildRainForPresetKind(ETDFluidPresetKind::Sea);
	const FTDFluidRainSettings PondSettings = UTDFluidPresetAsset::BuildRainForPresetKind(ETDFluidPresetKind::RainPond);
	const FTDFluidRainSettings BenchSettings = UTDFluidPresetAsset::BuildRainForPresetKind(ETDFluidPresetKind::RainAccumulationBench);
	TestEqual(TEXT("River and sea share the drop cap"), RiverSettings.MaxDropsPerTick, SeaSettings.MaxDropsPerTick);
	TestTrue(TEXT("The rain pond allows more drops per tick than the river"), PondSettings.MaxDropsPerTick > RiverSettings.MaxDropsPerTick);
	TestTrue(TEXT("The bench has rain on"), BenchSettings.bEnabled);
	TestFalse(TEXT("The bench does not accumulate puddles"), BenchSettings.bShouldAccumulatePuddles);
	TestTrue(TEXT("Bench drops carry more density than pond drops"), BenchSettings.DropDensityStrength > PondSettings.DropDensityStrength);
	TestTrue(TEXT("Pond drops ripple more than bench drops"), PondSettings.DropRippleImpulseCm > BenchSettings.DropRippleImpulseCm);
	TestTrue(TEXT("The bench uses its own seed"), BenchSettings.RandomSeed != PondSettings.RandomSeed);

	const FTDFluidSimulationConfig HeaderConfig;
	const FTDFluidSimulationConfig PondConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::RainPond);
	const FTDFluidSimulationConfig BenchConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::RainAccumulationBench);
	TestTrue(TEXT("The rain pond is a water layer kind"), PondConfig.bEnableWaterLayer);
	TestTrue(TEXT("The rain pond raises the point budget"), PondConfig.MaxPointsPerTick > HeaderConfig.MaxPointsPerTick);
	TestTrue(TEXT("The rain pond has a ripple wave speed"), PondConfig.RippleWaveSpeedCmPerSec > HeaderConfig.RippleWaveSpeedCmPerSec);
	TestTrue(TEXT("The bench raises the point budget"), BenchConfig.MaxPointsPerTick > HeaderConfig.MaxPointsPerTick);
	TestTrue(TEXT("The bench picks the solver automatically"), BenchConfig.SolverBackend == ETDFluidSolverBackend::Auto);

	TestFalse(TEXT("The rain pond is not a fire kind"), UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind::RainPond));
	TestFalse(TEXT("The bench is not a fire kind"), UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind::RainAccumulationBench));
	const UTDFluidPresetAsset* BenchPreset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::RainAccumulationBench);
	const UTDFluidPresetAsset* PondPreset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::RainPond);
	if (!TestNotNull(TEXT("Bench preset"), BenchPreset) || !TestNotNull(TEXT("Pond preset"), PondPreset))
	{
		return false;
	}

	TestTrue(TEXT("The bench paints the mud channel"), BenchPreset->DefaultChannel == ETDFluidBrushChannel::ChannelR_Mud);
	TestTrue(TEXT("The rain pond paints the water channel"), PondPreset->DefaultChannel == ETDFluidBrushChannel::ChannelA_Water);
	return true;
}

namespace TDFluidRainTestPrivate
{
	constexpr float RainRuntimeStepSeconds = 1.0f / 60.0f;
	constexpr int32 RainRuntimeResolution = 64;
	constexpr double RainRuntimeCellSizeCm = 10.0;
	constexpr int32 RainRuntimeWarmupSteps = 4;
	constexpr int32 RainOffStepCount = 120;
	constexpr int32 RainRippleStepCount = 60;
	constexpr int32 RainDensityStepCount = 120;
	constexpr float RainRuntimeWaterSurfaceZCm = 0.0f;
	constexpr float RainRuntimeFloorTopZCm = -100.0f;
	constexpr float RainWetnessDryingSeconds = 8.0f;
	constexpr float RainWetnessPerSecond = 0.05f;
	constexpr int32 RainWetnessRainSteps = 120;
	constexpr int32 RainWetnessDrySteps = 60;
	constexpr int32 RainWetnessDryCellX = 4;
	constexpr float RainWetnessTolerance = 1.0e-5f;
	constexpr double RainDensityMonotonicTolerance = 1.0e-6;

	struct FTDRainRuntimeArmOptions
	{
		FTDFluidRainSettings RainSettings;
		float Intensity = 0.0f;
		int32 ConsoleRain = -1;
		bool bUsesWaterLayer = true;
		int32 StepCount = RainOffStepCount;
	};

	struct FTDRainRuntimeArmResult
	{
		uint32 StateChecksum = 0;
		uint32 WaterStateChecksum = 0;
		uint32 DropCounter = 0;
		TArray<FTDFluidCellSample> Cells;
		TArray<FFloat16Color> WaterSurfacePixels;
		TArray<double> DensitySums;
	};

	struct FTDRainConsoleOverrideScope
	{
		FTDRainConsoleOverrideScope(const TCHAR* VariableName, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(VariableName))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetString();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDRainConsoleOverrideScope()
		{
			if (Variable)
			{
				Variable->Set(*PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		FString PreviousValue;
	};

	FTDFluidSimulationConfig MakeRainRuntimeConfig(bool bUsesWaterLayer)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.SolverBackend = ETDFluidSolverBackend::Cpu;
		Config.GridResolution = RainRuntimeResolution;
		Config.SimExtentCm = FVector2D(RainRuntimeResolution * RainRuntimeCellSizeCm, RainRuntimeResolution * RainRuntimeCellSizeCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = bUsesWaterLayer ? 0.35f : 0.0f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = bUsesWaterLayer;
		Config.bEnableWaterWetness = bUsesWaterLayer;
		Config.WetnessDryingSeconds = RainWetnessDryingSeconds;
		Config.WetnessFromWhitewater = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.AmbientDensity = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.SwashHeightCm = 0.0f;
		Config.BankWetHeightCm = 0.0f;
		return Config;
	}

	FTDFluidRainSettings MakeRuntimeRainSettings(bool bEnabled, float DropDensityStrength)
	{
		FTDFluidRainSettings Settings;
		Settings.bEnabled = bEnabled;
		Settings.DropsPerSquareMeterPerSec = 400.0f;
		Settings.DropRadiusCm = 15.0f;
		Settings.DropRippleImpulseCm = 0.4f;
		Settings.DropDensityStrength = DropDensityStrength;
		Settings.MaxDropsPerTick = 64;
		Settings.WetnessFromRainPerSec = 0.5f;
		Settings.RandomSeed = 7;
		return Settings;
	}

	FTDFluidWaterRegion MakeRainWaterRegion(double MinX, double HalfExtentCm)
	{
		FTDFluidWaterRegion Region;
		Region.MinXY = FVector2D(MinX, -HalfExtentCm);
		Region.MaxXY = FVector2D(HalfExtentCm, HalfExtentCm);
		Region.SurfaceHeightCm = RainRuntimeWaterSurfaceZCm;
		return Region;
	}

	void ApplyRainRuntimeWater(UTDFluidLiveComponent& Component, const FTDFluidSimulationConfig& Config)
	{
		const double HalfExtentCm = 0.5 * Config.SimExtentCm.X + 2.0 * RainRuntimeCellSizeCm;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(MakeRainWaterRegion(-HalfExtentCm, HalfExtentCm));
		Component.SetWaterGuidesAndRegions(TArray<FTDFluidFlowGuide>(), Regions);
		const int32 Resolution = Component.GetSimulationResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Component.SetTerrainElevationAtCell(CellX, CellY, RainRuntimeFloorTopZCm);
			}
		}
		Component.CompleteTerrainElevationSampling();
	}

	void StepRainRuntimeWorld(UWorld& World)
	{
		++GFrameCounter;
		World.Tick(LEVELTICK_All, RainRuntimeStepSeconds);
		FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, RainRuntimeStepSeconds);
	}

	double SumPrimaryDensity(const FTDFluidGrid2DSolver& Solver)
	{
		double Sum = 0.0;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Sum += Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
			}
		}
		return Sum;
	}

	void CaptureRainRuntimeResult(const UTDFluidLiveComponent& Component, bool bUsesWaterLayer, FTDRainRuntimeArmResult& OutResult)
	{
		const FTDFluidGrid2DSolver& Solver = Component.GetSolver();
		OutResult.StateChecksum = Solver.ComputeStateChecksum();
		OutResult.WaterStateChecksum = Solver.ComputeWaterStateChecksum();
		OutResult.DropCounter = Component.GetRainImpactState().DropCounter;
		const int32 Resolution = Solver.GetResolution();
		OutResult.Cells.Reset(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				OutResult.Cells.Add(Solver.SampleAtCell(CellX, CellY));
			}
		}
		if (bUsesWaterLayer)
		{
			Solver.PackWaterSurfacePixels(OutResult.WaterSurfacePixels, RainRuntimeFloorTopZCm);
		}
	}

	bool RunRainRuntimeArm(FAutomationTestBase& Test, const FString& ArmLabel, const FTDRainRuntimeArmOptions& Options, FTDRainRuntimeArmResult& OutResult)
	{
		FTDRainConsoleOverrideScope ConsoleScope(TEXT("TD.FluidFX.Rain"), Options.ConsoleRain);
		if (!Test.TestNotNull(*(ArmLabel + TEXT(" finds the TD.FluidFX.Rain console variable")), ConsoleScope.Variable))
		{
			return false;
		}

		FTDFluidRainTestWorldScope ArmWorld;
		UTDFluidLiveSubsystem* Subsystem = ArmWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
		if (!Test.TestNotNull(*(ArmLabel + TEXT(" has the fluid subsystem")), Subsystem))
		{
			return false;
		}

		const FTDFluidSimulationConfig Config = MakeRainRuntimeConfig(Options.bUsesWaterLayer);
		const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
		ATDFluidLiveActor* LiveActor = ArmWorld.World->SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!Test.TestTrue(*(ArmLabel + TEXT(" spawned the live actor")), LiveActor && LiveActor->LiveComponent))
		{
			return false;
		}
		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);

		UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
		if (Options.bUsesWaterLayer)
		{
			ApplyRainRuntimeWater(Component, Config);
		}
		for (int32 StepIndex = 0; StepIndex < RainRuntimeWarmupSteps; ++StepIndex)
		{
			StepRainRuntimeWorld(*ArmWorld.World);
		}
		if (!Test.TestTrue(*(ArmLabel + TEXT(" runs the CPU solver")), Component.IsSimulationInitialized() && !Component.IsUsingGpuSolver()))
		{
			return false;
		}

		Component.SetRainSettingsForTests(Options.RainSettings);
		Subsystem->SetRainIntensity(Options.Intensity);
		OutResult.DensitySums.Reset(Options.StepCount);
		for (int32 StepIndex = 0; StepIndex < Options.StepCount; ++StepIndex)
		{
			StepRainRuntimeWorld(*ArmWorld.World);
			OutResult.DensitySums.Add(SumPrimaryDensity(Component.GetSolver()));
		}
		CaptureRainRuntimeResult(Component, Options.bUsesWaterLayer, OutResult);
		return true;
	}

	FTDRainRuntimeArmOptions MakeRainArmOptions(bool bEnabled, float DropDensityStrength, float Intensity, int32 StepCount)
	{
		FTDRainRuntimeArmOptions Options;
		Options.RainSettings = MakeRuntimeRainSettings(bEnabled, DropDensityStrength);
		Options.Intensity = Intensity;
		Options.StepCount = StepCount;
		return Options;
	}

	int32 CountDensityOrVelocityMismatches(const FTDRainRuntimeArmResult& First, const FTDRainRuntimeArmResult& Second)
	{
		if (First.Cells.Num() != Second.Cells.Num())
		{
			return FMath::Max(First.Cells.Num(), Second.Cells.Num());
		}

		int32 MismatchCount = 0;
		for (int32 Index = 0; Index < First.Cells.Num(); ++Index)
		{
			const FTDFluidCellSample& FirstCell = First.Cells[Index];
			const FTDFluidCellSample& SecondCell = Second.Cells[Index];
			const bool bIsSame = FirstCell.PrimaryDensity == SecondCell.PrimaryDensity
				&& FirstCell.SelectiveDensity == SecondCell.SelectiveDensity
				&& FirstCell.Velocity == SecondCell.Velocity
				&& FirstCell.VelocityCmPerSec == SecondCell.VelocityCmPerSec;
			MismatchCount += bIsSame ? 0 : 1;
		}
		return MismatchCount;
	}

	double ComputeWaterSurfaceL1(const FTDRainRuntimeArmResult& First, const FTDRainRuntimeArmResult& Second)
	{
		if (First.WaterSurfacePixels.Num() != Second.WaterSurfacePixels.Num())
		{
			return 0.0;
		}

		double Sum = 0.0;
		for (int32 Index = 0; Index < First.WaterSurfacePixels.Num(); ++Index)
		{
			const FLinearColor FirstColor = First.WaterSurfacePixels[Index].GetFloats();
			const FLinearColor SecondColor = Second.WaterSurfacePixels[Index].GetFloats();
			Sum += FMath::Abs(FirstColor.R - SecondColor.R) + FMath::Abs(FirstColor.G - SecondColor.G) + FMath::Abs(FirstColor.B - SecondColor.B) + FMath::Abs(FirstColor.A - SecondColor.A);
		}
		return Sum;
	}

	void StartRainWetnessSolver(FTDFluidGrid2DSolver& Solver)
	{
		Solver.Initialize(MakeRainRuntimeConfig(true), FVector2D::ZeroVector);
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, RainRuntimeFloorTopZCm);
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
		const double HalfExtentCm = 0.5 * Solver.GetExtentCm().X + 2.0 * RainRuntimeCellSizeCm;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(MakeRainWaterRegion(0.0, HalfExtentCm));
		Solver.SetWaterRegions(Regions);
	}

	float AdvanceExpectedRainWetness(float Wetness, float RainWetnessPerSec, int32 StepCount)
	{
		const float RetainFactor = FTDFluidWaterWetnessRules::ComputeRetainFactor(RainRuntimeStepSeconds, RainWetnessDryingSeconds);
		const float RainWetnessStep = RainWetnessPerSec * RainRuntimeStepSeconds;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Wetness = FMath::Clamp(FMath::Max(Wetness * RetainFactor + RainWetnessStep, 0.0f), 0.0f, 1.0f);
		}
		return Wetness;
	}

	void RunRainWetnessSteps(FTDFluidGrid2DSolver& Solver, int32 StepCount)
	{
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Solver.StepSimulation(RainRuntimeStepSeconds);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainOffIsBitIdenticalTest,
	"TDGame.FluidFX.Rain.OffIsBitIdentical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainOffIsBitIdenticalTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	FTDRainRuntimeArmResult NoRain;
	FTDRainRuntimeArmResult ZeroIntensity;
	FTDRainRuntimeArmResult ConsoleOff;
	FTDRainRuntimeArmResult Raining;
	FTDRainRuntimeArmOptions ConsoleOffOptions = MakeRainArmOptions(true, 0.5f, 1.0f, RainOffStepCount);
	ConsoleOffOptions.ConsoleRain = 0;
	if (!RunRainRuntimeArm(*this, TEXT("No rain"), MakeRainArmOptions(false, 0.5f, 1.0f, RainOffStepCount), NoRain)
		|| !RunRainRuntimeArm(*this, TEXT("Rain at zero intensity"), MakeRainArmOptions(true, 0.5f, 0.0f, RainOffStepCount), ZeroIntensity)
		|| !RunRainRuntimeArm(*this, TEXT("Rain with console off"), ConsoleOffOptions, ConsoleOff)
		|| !RunRainRuntimeArm(*this, TEXT("Raining"), MakeRainArmOptions(true, 0.5f, 1.0f, RainOffStepCount), Raining))
	{
		return false;
	}

	TestEqual(TEXT("Zero intensity keeps the solver state checksum"), ZeroIntensity.StateChecksum, NoRain.StateChecksum);
	TestEqual(TEXT("Zero intensity keeps the water state checksum"), ZeroIntensity.WaterStateChecksum, NoRain.WaterStateChecksum);
	TestEqual(TEXT("Console off keeps the solver state checksum"), ConsoleOff.StateChecksum, NoRain.StateChecksum);
	TestEqual(TEXT("Console off keeps the water state checksum"), ConsoleOff.WaterStateChecksum, NoRain.WaterStateChecksum);
	TestEqual(TEXT("No rain queues no drops"), NoRain.DropCounter, 0u);
	TestEqual(TEXT("Zero intensity queues no drops"), ZeroIntensity.DropCounter, 0u);
	TestEqual(TEXT("Console off queues no drops"), ConsoleOff.DropCounter, 0u);
	TestTrue(TEXT("The raining control arm queues drops"), Raining.DropCounter > 0u);
	TestTrue(TEXT("The raining control arm changes the simulation"), Raining.StateChecksum != NoRain.StateChecksum || Raining.WaterStateChecksum != NoRain.WaterStateChecksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainRippleOnlyKeepsDensityAndVelocityTest,
	"TDGame.FluidFX.Rain.RippleOnlyKeepsDensityAndVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainRippleOnlyKeepsDensityAndVelocityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	FTDRainRuntimeArmResult NoRain;
	FTDRainRuntimeArmResult RippleOnly;
	if (!RunRainRuntimeArm(*this, TEXT("No rain"), MakeRainArmOptions(false, 0.0f, 1.0f, RainRippleStepCount), NoRain)
		|| !RunRainRuntimeArm(*this, TEXT("Ripple-only rain"), MakeRainArmOptions(true, 0.0f, 1.0f, RainRippleStepCount), RippleOnly))
	{
		return false;
	}

	TestTrue(TEXT("Ripple-only rain queues drops"), RippleOnly.DropCounter > 0u);
	TestEqual(TEXT("Ripple-only rain keeps density and velocity in every cell"), CountDensityOrVelocityMismatches(NoRain, RippleOnly), 0);
	const double RippleL1 = ComputeWaterSurfaceL1(NoRain, RippleOnly);
	AddInfo(FString::Printf(TEXT("Ripple-only water surface L1 against no rain: %.6f"), RippleL1));
	TestTrue(TEXT("Ripple-only rain moves the water surface"), RippleL1 > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainDensityModeAccumulatesTest,
	"TDGame.FluidFX.Rain.DensityModeAccumulates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainDensityModeAccumulatesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	FTDRainRuntimeArmOptions Options = MakeRainArmOptions(true, 0.5f, 1.0f, RainDensityStepCount);
	Options.bUsesWaterLayer = false;
	FTDRainRuntimeArmResult DensityRain;
	if (!RunRainRuntimeArm(*this, TEXT("Density rain"), Options, DensityRain))
	{
		return false;
	}

	TestTrue(TEXT("Density rain queues drops"), DensityRain.DropCounter > 0u);
	int32 DecreaseCount = 0;
	for (int32 StepIndex = 1; StepIndex < DensityRain.DensitySums.Num(); ++StepIndex)
	{
		const double Previous = DensityRain.DensitySums[StepIndex - 1];
		const double Tolerance = RainDensityMonotonicTolerance * FMath::Max(1.0, Previous);
		DecreaseCount += DensityRain.DensitySums[StepIndex] + Tolerance < Previous ? 1 : 0;
	}
	TestEqual(TEXT("Density sum never decreases while it rains"), DecreaseCount, 0);
	const double FinalSum = DensityRain.DensitySums.Num() > 0 ? DensityRain.DensitySums.Last() : 0.0;
	AddInfo(FString::Printf(TEXT("Density rain final density sum: %.6f"), FinalSum));
	TestTrue(TEXT("Density rain accumulates density"), FinalSum > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainWetnessFromRainTest,
	"TDGame.FluidFX.Rain.WetnessFromRain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainWetnessFromRainTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	FTDFluidGrid2DSolver Solver;
	StartRainWetnessSolver(Solver);
	const int32 DryCellY = Solver.GetResolution() / 2;
	const int32 WetCellX = Solver.GetResolution() - 4;
	Solver.SetRainWetnessPerSec(RainWetnessPerSecond);
	RunRainWetnessSteps(Solver, RainWetnessRainSteps);
	if (!TestTrue(TEXT("The wetness harness keeps the dry cell dry"), Solver.SampleAtCell(RainWetnessDryCellX, DryCellY).WaterDepthCm <= 0.0f)
		|| !TestTrue(TEXT("The wetness harness wets water cells"), Solver.SampleAtCell(WetCellX, DryCellY).Wetness == 1.0f))
	{
		return false;
	}

	const float RainedExpected = AdvanceExpectedRainWetness(0.0f, RainWetnessPerSecond, RainWetnessRainSteps);
	const float RainedWetness = Solver.SampleAtCell(RainWetnessDryCellX, DryCellY).Wetness;
	AddInfo(FString::Printf(TEXT("Dry cell wetness after rain: %.6f expected %.6f"), RainedWetness, RainedExpected));
	TestTrue(TEXT("Rain wets a dry cell along the discrete retain series"), FMath::Abs(RainedWetness - RainedExpected) <= RainWetnessTolerance && RainedWetness > 0.0f);

	Solver.SetRainWetnessPerSec(MakeQuietNaN());
	RunRainWetnessSteps(Solver, RainWetnessDrySteps);
	const float DriedExpected = AdvanceExpectedRainWetness(RainedWetness, 0.0f, RainWetnessDrySteps);
	const float DriedWetness = Solver.SampleAtCell(RainWetnessDryCellX, DryCellY).Wetness;
	AddInfo(FString::Printf(TEXT("Dry cell wetness after the rain stops: %.6f expected %.6f"), DriedWetness, DriedExpected));
	TestTrue(TEXT("A NaN rain rate stops rain and the dry cell decays by the retain factor"), FMath::Abs(DriedWetness - DriedExpected) <= RainWetnessTolerance && DriedWetness < RainedWetness);
	return true;
}

#endif
