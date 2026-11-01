#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterParticleActor.h"
#include "FluidFX/TDFluidWaterParticleTypes.h"
#include "FluidFX/TDFluidWaterSplashRules.h"
#include "Algo/Reverse.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

namespace TDFluidWaterParticleTestPrivate
{
	constexpr float SplashStepSeconds = 1.0f / 60.0f;
	constexpr float SplashSurfaceHeightCm = 0.0f;
	constexpr float SplashPointRadiusCm = 10.0f;
	constexpr int32 ContactResolution = 64;
	constexpr double ContactCellSizeCm = 25.0;
	constexpr float ContactStepSeconds = 1.0f / 60.0f;
	constexpr int32 ContactPointCount = 600;
	constexpr int32 ContactStampSteps = 6;
	constexpr int32 ContactSettleSteps = 4;

	FTDFluidWaterParticleSettings MakeSplashSettings()
	{
		FTDFluidWaterParticleSettings Settings;
		Settings.SplashEntrySpeedThresholdCmPerSec = 150.0f;
		Settings.SplashExitSpeedThresholdCmPerSec = 200.0f;
		Settings.SplashMoveSpeedThresholdCmPerSec = 250.0f;
		Settings.SplashMoveIntervalSeconds = 0.15f;
		Settings.SplashCooldownSeconds = 0.2f;
		Settings.SplashSurfaceBandCm = 4.0f;
		Settings.SplashMinRadiusCm = 5.0f;
		Settings.SplashMaxRadiusCm = 60.0f;
		Settings.MaxContactStampsPerTick = 16;
		Settings.ContactReadFallbackRadiusCm = 12.0f;
		return Settings;
	}

	FVector MakeBottomLocation(double X, double Y, double BottomHeightCm)
	{
		return FVector(X, Y, SplashSurfaceHeightCm + SplashPointRadiusCm + BottomHeightCm);
	}

	void AppendVerticalTrack(TArray<FVector>& InOutLocations, double StartBottomCm, double SpeedCmPerSec, int32 StepCount)
	{
		for (int32 StepIndex = 0; StepIndex <= StepCount; ++StepIndex)
		{
			InOutLocations.Add(MakeBottomLocation(0.0, 0.0, StartBottomCm + SpeedCmPerSec * SplashStepSeconds * StepIndex));
		}
	}

	TArray<FTDFluidWaterSplashEvent> EvaluateTrack(const FTDFluidWaterParticleSettings& Settings, const TArray<FVector>& Locations, FTDFluidWaterSplashPointState& InOutState)
	{
		TArray<FTDFluidWaterSplashEvent> Events;
		for (const FVector& Location : Locations)
		{
			FTDFluidWaterSplashEvent Event;
			if (FTDFluidWaterSplashRules::EvaluateSplashPoint(Settings, Location, SplashPointRadiusCm, SplashSurfaceHeightCm, SplashStepSeconds, InOutState, Event))
			{
				Events.Add(Event);
			}
		}
		return Events;
	}

	TArray<FTDFluidWaterSplashEvent> EvaluateTrack(const FTDFluidWaterParticleSettings& Settings, const TArray<FVector>& Locations)
	{
		FTDFluidWaterSplashPointState State;
		return EvaluateTrack(Settings, Locations, State);
	}

	int32 CountEventsOfKind(const TArray<FTDFluidWaterSplashEvent>& Events, ETDFluidWaterSplashKind Kind)
	{
		int32 Count = 0;
		for (const FTDFluidWaterSplashEvent& Event : Events)
		{
			Count += Event.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	bool AreSplashEventsIdentical(const FTDFluidWaterSplashEvent& Left, const FTDFluidWaterSplashEvent& Right)
	{
		return Left.WorldLocation == Right.WorldLocation
			&& Left.ImpactVelocity == Right.ImpactVelocity
			&& Left.ImpactSpeedCmPerSec == Right.ImpactSpeedCmPerSec
			&& Left.RadiusCm == Right.RadiusCm
			&& Left.SurfaceHeightCm == Right.SurfaceHeightCm
			&& Left.Kind == Right.Kind;
	}

	TArray<FTDFluidWaterSplashEvent> MakeSplashEventSet()
	{
		TArray<FTDFluidWaterSplashEvent> Events;
		const ETDFluidWaterSplashKind Kinds[] = { ETDFluidWaterSplashKind::Entry, ETDFluidWaterSplashKind::Exit, ETDFluidWaterSplashKind::Move };
		for (int32 RingIndex = 0; RingIndex < 5; ++RingIndex)
		{
			for (int32 AngleIndex = 0; AngleIndex < 8; ++AngleIndex)
			{
				const double Angle = AngleIndex * UE_DOUBLE_PI * 0.25;
				const double Distance = 40.0 + RingIndex * 35.0;
				FTDFluidWaterSplashEvent& Event = Events.AddDefaulted_GetRef();
				Event.WorldLocation = FVector(Distance * FMath::Cos(Angle), Distance * FMath::Sin(Angle), 0.0);
				Event.ImpactVelocity = FVector(0.0, 0.0, -150.0 - AngleIndex * 10.0);
				Event.ImpactSpeedCmPerSec = 150.0f + AngleIndex * 10.0f;
				Event.RadiusCm = 10.0f + RingIndex;
				Event.Kind = Kinds[(RingIndex + AngleIndex) % 3];
			}
		}
		FTDFluidWaterSplashEvent SameCellOtherKind = Events[0];
		SameCellOtherKind.Kind = ETDFluidWaterSplashKind::Move;
		Events.Add(SameCellOtherKind);
		FTDFluidWaterSplashEvent SubMillimeterTwin = Events[1];
		SubMillimeterTwin.WorldLocation.Z += 0.01;
		Events.Add(SubMillimeterTwin);
		return Events;
	}

	template <typename ElementType>
	TArray<ElementType> MakeShuffledCopy(const TArray<ElementType>& Source, int32 Seed)
	{
		TArray<ElementType> Shuffled = Source;
		FRandomStream Stream(Seed);
		for (int32 Index = Shuffled.Num() - 1; Index > 0; --Index)
		{
			Shuffled.Swap(Index, Stream.RandRange(0, Index));
		}
		return Shuffled;
	}

	TArray<FTDFluidMassInputPoint> MakeContactPoints(const FVector& CenterWorld)
	{
		TArray<FTDFluidMassInputPoint> Points;
		Points.Reserve(ContactPointCount);
		for (int32 PointIndex = 0; PointIndex < ContactPointCount; ++PointIndex)
		{
			const int32 Column = PointIndex % 25;
			const int32 Row = PointIndex / 25;
			FTDFluidMassInputPoint& Point = Points.AddDefaulted_GetRef();
			Point.WorldLocation = CenterWorld + FVector((Column - 12) * 6.0, (Row - 12) * 6.0, 0.0);
			Point.WorldVelocity = FVector(30.0 + Column, -20.0 + Row, -50.0);
			Point.RadiusCm = PointIndex % 5 == 0 ? 0.0f : 8.0f + (PointIndex % 7);
			Point.DensityStrength = 0.2f + 0.1f * (PointIndex % 9);
			Point.HeatStrength = 0.4f;
			Point.RippleImpulseCm = 3.0f;
			if (PointIndex % 4 == 0)
			{
				Point.DensityStrength = PointIndex % 8 == 0 ? 0.0f : -0.5f;
			}
		}
		return Points;
	}

	bool AreStampsIdentical(const FTDFluidBrushStamp& Left, const FTDFluidBrushStamp& Right)
	{
		return Left.WorldLocation == Right.WorldLocation
			&& Left.WorldVelocity == Right.WorldVelocity
			&& Left.RadiusCm == Right.RadiusCm
			&& Left.DensityStrength == Right.DensityStrength
			&& Left.VelocityStrength == Right.VelocityStrength
			&& Left.RippleImpulseCm == Right.RippleImpulseCm
			&& Left.HeatStrength == Right.HeatStrength
			&& Left.Channel == Right.Channel
			&& Left.Source == Right.Source;
	}

	FTDFluidSimulationConfig MakeContactWaterConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = ContactResolution;
		Config.SimExtentCm = FVector2D(ContactResolution * ContactCellSizeCm, ContactResolution * ContactCellSizeCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.35f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = true;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.RippleWaveSpeedCmPerSec = 0.0f;
		Config.SwashHeightCm = 0.0f;
		Config.BankWetHeightCm = 0.0f;
		Config.ActorBrushRippleImpulseCm = 0.0f;
		return Config;
	}

	void StartContactWater(FTDFluidGrid2DSolver& Solver)
	{
		Solver.Initialize(MakeContactWaterConfig(), FVector2D::ZeroVector);
		for (int32 CellY = 0; CellY < ContactResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < ContactResolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, -100.0f);
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();

		const double HalfExtentCm = Solver.GetExtentCm().X * 0.5 + 2.0 * ContactCellSizeCm;
		FTDFluidWaterRegion Region;
		Region.MinXY = Solver.GetCenterWorld() - FVector2D(HalfExtentCm, HalfExtentCm);
		Region.MaxXY = Solver.GetCenterWorld() + FVector2D(HalfExtentCm, HalfExtentCm);
		Region.SurfaceHeightCm = 0.0f;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(Region);
		Solver.SetWaterRegions(Regions);
		Solver.StepSimulation(ContactStepSeconds);
	}

	TArray<float> RunContactWater(const TArray<FTDFluidBrushStamp>& Stamps)
	{
		FTDFluidGrid2DSolver Solver;
		StartContactWater(Solver);
		for (int32 StepIndex = 0; StepIndex < ContactStampSteps; ++StepIndex)
		{
			if (!Stamps.IsEmpty())
			{
				Solver.ApplyBrushStamps(Stamps, ContactStepSeconds);
			}
			Solver.StepSimulation(ContactStepSeconds);
		}
		for (int32 StepIndex = 0; StepIndex < ContactSettleSteps; ++StepIndex)
		{
			Solver.StepSimulation(ContactStepSeconds);
		}

		TArray<float> Whitewater;
		Whitewater.Reserve(ContactResolution * ContactResolution);
		for (int32 CellY = 0; CellY < ContactResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < ContactResolution; ++CellX)
			{
				Whitewater.Add(Solver.SampleAtCell(CellX, CellY).PrimaryDensity);
			}
		}
		return Whitewater;
	}

	bool HaveSameBits(float Left, float Right)
	{
		return FMemory::Memcmp(&Left, &Right, sizeof(float)) == 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleSplashEntryCrossingEmitsOnceTest,
	"TDGame.FluidFX.WaterParticle.Splash.EntryCrossingEmitsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleSplashEntryCrossingEmitsOnceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleTestPrivate;

	const FTDFluidWaterParticleSettings Settings = MakeSplashSettings();
	TArray<FVector> FastTrack;
	AppendVerticalTrack(FastTrack, 20.0, -300.0, 30);
	const TArray<FTDFluidWaterSplashEvent> FastEvents = EvaluateTrack(Settings, FastTrack);
	if (!TestEqual(TEXT("A fast downward crossing emits exactly one event"), FastEvents.Num(), 1))
	{
		return false;
	}

	const FTDFluidWaterSplashEvent& Entry = FastEvents[0];
	TestTrue(TEXT("The crossing event is an entry"), Entry.Kind == ETDFluidWaterSplashKind::Entry);
	TestTrue(TEXT("The entry speed is the downward speed"), FMath::IsNearlyEqual(Entry.ImpactSpeedCmPerSec, 300.0f, 0.05f));
	TestTrue(TEXT("The entry sits on the water surface"), Entry.WorldLocation.Z == SplashSurfaceHeightCm && Entry.SurfaceHeightCm == SplashSurfaceHeightCm);
	TestTrue(TEXT("The entry keeps the point radius"), Entry.RadiusCm == SplashPointRadiusCm);
	TestTrue(TEXT("The entry velocity points down"), Entry.ImpactVelocity.Z < 0.0);

	TArray<FVector> SlowTrack;
	AppendVerticalTrack(SlowTrack, 20.0, -100.0, 60);
	TestEqual(TEXT("A slow downward crossing emits nothing"), EvaluateTrack(Settings, SlowTrack).Num(), 0);

	FTDFluidWaterSplashPointState FirstSampleState;
	FTDFluidWaterSplashEvent FirstSampleEvent;
	TestFalse(TEXT("The first sample only stores the state"), FTDFluidWaterSplashRules::EvaluateSplashPoint(Settings, MakeBottomLocation(0.0, 0.0, -50.0), SplashPointRadiusCm, SplashSurfaceHeightCm, SplashStepSeconds, FirstSampleState, FirstSampleEvent));
	TestTrue(TEXT("The first sample marks the state as tracked"), FirstSampleState.bHasPrevious);

	TestTrue(TEXT("Radius below the minimum is raised"), FTDFluidWaterSplashRules::ResolveSplashRadiusCm(Settings, 1.0f) == Settings.SplashMinRadiusCm);
	TestTrue(TEXT("Radius above the maximum is lowered"), FTDFluidWaterSplashRules::ResolveSplashRadiusCm(Settings, 1000.0f) == Settings.SplashMaxRadiusCm);
	TestTrue(TEXT("Radius inside the range is kept"), FTDFluidWaterSplashRules::ResolveSplashRadiusCm(Settings, 20.0f) == 20.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleSplashBandAndCooldownSuppressTest,
	"TDGame.FluidFX.WaterParticle.Splash.BandAndCooldownSuppress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleSplashBandAndCooldownSuppressTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleTestPrivate;

	const FTDFluidWaterParticleSettings Settings = MakeSplashSettings();
	TArray<FVector> OscillationTrack;
	for (int32 StepIndex = 0; StepIndex < 120; ++StepIndex)
	{
		OscillationTrack.Add(MakeBottomLocation(0.0, 0.0, StepIndex % 2 == 0 ? 2.0 : -2.0));
	}
	TestEqual(TEXT("Bobbing inside the surface band emits nothing"), EvaluateTrack(Settings, OscillationTrack).Num(), 0);

	FTDFluidWaterSplashPointState State;
	const TArray<FVector> CooldownTrack = {
		MakeBottomLocation(0.0, 0.0, 20.0),
		MakeBottomLocation(0.0, 0.0, 15.0),
		MakeBottomLocation(0.0, 0.0, 10.0),
		MakeBottomLocation(0.0, 0.0, 5.0),
		MakeBottomLocation(0.0, 0.0, 0.0),
		MakeBottomLocation(0.0, 0.0, 5.0),
		MakeBottomLocation(0.0, 0.0, 10.0),
		MakeBottomLocation(0.0, 0.0, 5.0),
		MakeBottomLocation(0.0, 0.0, 0.0)
	};
	const TArray<FTDFluidWaterSplashEvent> CooldownEvents = EvaluateTrack(Settings, CooldownTrack, State);
	TestEqual(TEXT("Leaving and re-entering inside the cooldown emits one event"), CooldownEvents.Num(), 1);
	TestEqual(TEXT("The single cooldown event is the first entry"), CountEventsOfKind(CooldownEvents, ETDFluidWaterSplashKind::Entry), 1);

	TArray<FVector> RecoveryTrack;
	for (int32 StepIndex = 1; StepIndex <= 20; ++StepIndex)
	{
		RecoveryTrack.Add(MakeBottomLocation(0.0, 0.0, StepIndex));
	}
	RecoveryTrack.Add(MakeBottomLocation(0.0, 0.0, 15.0));
	RecoveryTrack.Add(MakeBottomLocation(0.0, 0.0, 10.0));
	RecoveryTrack.Add(MakeBottomLocation(0.0, 0.0, 5.0));
	RecoveryTrack.Add(MakeBottomLocation(0.0, 0.0, 0.0));
	const TArray<FTDFluidWaterSplashEvent> RecoveryEvents = EvaluateTrack(Settings, RecoveryTrack, State);
	TestEqual(TEXT("A slow rise emits no exit and a later fast crossing emits a new entry"), RecoveryEvents.Num(), 1);
	TestEqual(TEXT("The post-cooldown event is an entry"), CountEventsOfKind(RecoveryEvents, ETDFluidWaterSplashKind::Entry), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleSplashExitAndMoveRulesTest,
	"TDGame.FluidFX.WaterParticle.Splash.ExitAndMoveRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleSplashExitAndMoveRulesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleTestPrivate;

	const FTDFluidWaterParticleSettings Settings = MakeSplashSettings();
	TArray<FVector> RiseTrack;
	AppendVerticalTrack(RiseTrack, -20.0, 250.0, 15);
	const TArray<FTDFluidWaterSplashEvent> RiseEvents = EvaluateTrack(Settings, RiseTrack);
	TestEqual(TEXT("A fast rise through the band emits one event"), RiseEvents.Num(), 1);
	TestEqual(TEXT("The rise event is an exit"), CountEventsOfKind(RiseEvents, ETDFluidWaterSplashKind::Exit), 1);
	if (RiseEvents.Num() == 1)
	{
		TestTrue(TEXT("The exit speed is the upward speed"), FMath::IsNearlyEqual(RiseEvents[0].ImpactSpeedCmPerSec, 250.0f, 0.05f));
	}

	FTDFluidWaterParticleSettings NoExitSettings = Settings;
	NoExitSettings.SplashExitSpeedThresholdCmPerSec = 0.0f;
	TestEqual(TEXT("A zero exit threshold disables exits"), EvaluateTrack(NoExitSettings, RiseTrack).Num(), 0);

	TArray<FVector> SlowRiseTrack;
	AppendVerticalTrack(SlowRiseTrack, -20.0, 100.0, 40);
	TestEqual(TEXT("A slow rise emits nothing"), EvaluateTrack(Settings, SlowRiseTrack).Num(), 0);

	TArray<FVector> SkimTrack;
	for (int32 StepIndex = 0; StepIndex <= 60; ++StepIndex)
	{
		SkimTrack.Add(FVector(400.0 * SplashStepSeconds * StepIndex, 0.0, SplashSurfaceHeightCm));
	}
	const TArray<FTDFluidWaterSplashEvent> SkimEvents = EvaluateTrack(Settings, SkimTrack);
	const int32 MoveCount = CountEventsOfKind(SkimEvents, ETDFluidWaterSplashKind::Move);
	TestTrue(*FString::Printf(TEXT("Skimming for one second emits six or seven moves (got %d)"), MoveCount), MoveCount >= 6 && MoveCount <= 7);
	TestEqual(TEXT("Skimming emits only moves"), SkimEvents.Num(), MoveCount);
	if (SkimEvents.Num() > 0)
	{
		TestTrue(TEXT("The move speed is the horizontal speed"), FMath::IsNearlyEqual(SkimEvents[0].ImpactSpeedCmPerSec, 400.0f, 0.05f));
	}

	FTDFluidWaterParticleSettings NoMoveSettings = Settings;
	NoMoveSettings.SplashMoveSpeedThresholdCmPerSec = 0.0f;
	TestEqual(TEXT("A zero move threshold disables moves"), EvaluateTrack(NoMoveSettings, SkimTrack).Num(), 0);

	TArray<FVector> HighSkimTrack;
	for (int32 StepIndex = 0; StepIndex <= 60; ++StepIndex)
	{
		HighSkimTrack.Add(FVector(400.0 * SplashStepSeconds * StepIndex, 0.0, SplashSurfaceHeightCm + 3.0 * SplashPointRadiusCm));
	}
	TestEqual(TEXT("Skimming above the surface emits nothing"), EvaluateTrack(Settings, HighSkimTrack).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleSplashEventOrderIndependentTest,
	"TDGame.FluidFX.WaterParticle.Splash.EventOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleSplashEventOrderIndependentTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleTestPrivate;

	constexpr int32 MaxEvents = 12;
	const FVector CenterWorld(5.0, -3.0, 0.0);
	const TArray<FTDFluidWaterSplashEvent> Events = MakeSplashEventSet();
	TArray<FTDFluidWaterSplashEvent> Forward = Events;
	TArray<FTDFluidWaterSplashEvent> Shuffled = MakeShuffledCopy(Events, 1234);
	TArray<FTDFluidWaterSplashEvent> Reversed = Events;
	Algo::Reverse(Reversed);
	FTDFluidWaterSplashRules::SortAndClampSplashEvents(Forward, CenterWorld, MaxEvents);
	FTDFluidWaterSplashRules::SortAndClampSplashEvents(Shuffled, CenterWorld, MaxEvents);
	FTDFluidWaterSplashRules::SortAndClampSplashEvents(Reversed, CenterWorld, MaxEvents);
	if (!TestEqual(TEXT("Events are clamped to the maximum"), Forward.Num(), MaxEvents) || !TestEqual(TEXT("Every order keeps the same count"), Shuffled.Num(), Forward.Num()) || !TestEqual(TEXT("Reversed order keeps the same count"), Reversed.Num(), Forward.Num()))
	{
		return false;
	}

	bool bAllIdentical = true;
	bool bIsSortedByDistance = true;
	for (int32 EventIndex = 0; EventIndex < Forward.Num(); ++EventIndex)
	{
		bAllIdentical &= AreSplashEventsIdentical(Forward[EventIndex], Shuffled[EventIndex]) && AreSplashEventsIdentical(Forward[EventIndex], Reversed[EventIndex]);
		if (EventIndex > 0)
		{
			bIsSortedByDistance &= FVector::DistSquared(Forward[EventIndex - 1].WorldLocation, CenterWorld) <= FVector::DistSquared(Forward[EventIndex].WorldLocation, CenterWorld) + 1.0;
		}
	}
	TestTrue(TEXT("Every input order yields bit-identical events"), bAllIdentical);
	TestTrue(TEXT("Kept events are the nearest to the center"), bIsSortedByDistance);

	TArray<FTDFluidWaterSplashEvent> Cleared = Events;
	FTDFluidWaterSplashRules::SortAndClampSplashEvents(Cleared, CenterWorld, 0);
	TestEqual(TEXT("A zero maximum clears the events"), Cleared.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleContactStampsDeterministicDensityOnlyTest,
	"TDGame.FluidFX.WaterParticle.Contact.StampsDeterministicDensityOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleContactStampsDeterministicDensityOnlyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleTestPrivate;

	constexpr int32 MaxStamps = 64;
	constexpr float FallbackRadiusCm = 12.5f;
	const FVector CenterWorld = FVector::ZeroVector;
	const TArray<FTDFluidMassInputPoint> Points = MakeContactPoints(CenterWorld);
	TArray<FTDFluidMassInputPoint> ForwardPoints = Points;
	TArray<FTDFluidMassInputPoint> ShuffledPoints = MakeShuffledCopy(Points, 4321);
	TArray<FTDFluidBrushStamp> ForwardStamps;
	TArray<FTDFluidBrushStamp> ShuffledStamps;
	FTDFluidWaterSplashRules::BuildContactWhitewaterStamps(ForwardPoints, CenterWorld, MaxStamps, FallbackRadiusCm, ForwardStamps);
	FTDFluidWaterSplashRules::BuildContactWhitewaterStamps(ShuffledPoints, CenterWorld, MaxStamps, FallbackRadiusCm, ShuffledStamps);
	if (!TestEqual(TEXT("Contact stamps are clamped to the maximum after dropping empty points"), ForwardStamps.Num(), MaxStamps) || !TestEqual(TEXT("Both orders keep the same stamp count"), ShuffledStamps.Num(), ForwardStamps.Num()))
	{
		return false;
	}

	bool bAllIdentical = true;
	bool bAllDensityOnly = true;
	bool bAllPositiveDensity = true;
	bool bHasFallbackRadius = false;
	for (int32 StampIndex = 0; StampIndex < ForwardStamps.Num(); ++StampIndex)
	{
		const FTDFluidBrushStamp& Stamp = ForwardStamps[StampIndex];
		bAllIdentical &= AreStampsIdentical(Stamp, ShuffledStamps[StampIndex]);
		bAllDensityOnly &= Stamp.VelocityStrength == 0.0f && Stamp.RippleImpulseCm == 0.0f && Stamp.HeatStrength == 0.0f && Stamp.WorldVelocity.IsZero()
			&& Stamp.Channel == ETDFluidBrushChannel::ChannelR_Mud && Stamp.Source == ETDFluidBrushSource::DataChannel && Stamp.RadiusCm >= 1.0f;
		bAllPositiveDensity &= Stamp.DensityStrength > 0.0f && Stamp.DensityStrength <= 1.0f;
		bHasFallbackRadius |= Stamp.RadiusCm == FallbackRadiusCm;
	}
	TestTrue(TEXT("Both point orders yield bit-identical stamps"), bAllIdentical);
	TestTrue(TEXT("Contact stamps write density only"), bAllDensityOnly);
	TestTrue(TEXT("Points without density are dropped before the clamp"), bAllPositiveDensity);
	TestTrue(TEXT("Points without a radius use the fallback radius"), bHasFallbackRadius);

	TArray<FTDFluidMassInputPoint> EmptyPoints;
	for (int32 PointIndex = 0; PointIndex < 8; ++PointIndex)
	{
		FTDFluidMassInputPoint& Point = EmptyPoints.AddDefaulted_GetRef();
		Point.WorldLocation = FVector(PointIndex, 0.0, 0.0);
		Point.DensityStrength = 0.0f;
	}
	TArray<FTDFluidBrushStamp> EmptyStamps;
	FTDFluidWaterSplashRules::BuildContactWhitewaterStamps(EmptyPoints, CenterWorld, MaxStamps, FallbackRadiusCm, EmptyStamps);
	TestEqual(TEXT("Points without density make no stamps"), EmptyStamps.Num(), 0);

	FTDFluidWaterParticleSettings Settings = MakeSplashSettings();
	TestEqual(TEXT("An empty queue gets the full contact budget"), FTDFluidWaterSplashRules::ResolveContactStampBudget(Settings, 64, 0), Settings.MaxContactStampsPerTick);
	TestEqual(TEXT("A nearly full queue gets only the remaining slots"), FTDFluidWaterSplashRules::ResolveContactStampBudget(Settings, 64, 60), 4);
	TestEqual(TEXT("A full queue gets no contact stamps"), FTDFluidWaterSplashRules::ResolveContactStampBudget(Settings, 64, 64), 0);
	TestEqual(TEXT("An overfull queue gets no contact stamps"), FTDFluidWaterSplashRules::ResolveContactStampBudget(Settings, 64, 70), 0);
	Settings.MaxContactStampsPerTick = 0;
	TestEqual(TEXT("A zero contact budget makes no stamps"), FTDFluidWaterSplashRules::ResolveContactStampBudget(Settings, 64, 0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleContactCpuWaterWhitewaterRisesTest,
	"TDGame.FluidFX.WaterParticle.Contact.CpuWaterWhitewaterRises",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleContactCpuWaterWhitewaterRisesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleTestPrivate;

	const FTDFluidWaterParticleSettings Settings = MakeSplashSettings();
	const FTDFluidSimulationConfig Config = MakeContactWaterConfig();
	const FVector CenterWorld = FVector::ZeroVector;

	TArray<FTDFluidBrushStamp> PendingStamps;
	for (int32 StampIndex = 0; StampIndex < Config.MaxPointsPerTick - 4; ++StampIndex)
	{
		FTDFluidBrushStamp& Stamp = PendingStamps.AddDefaulted_GetRef();
		Stamp.WorldLocation = FVector(-600.0 + StampIndex * 3.0, 600.0, 0.0);
		Stamp.RadiusCm = 20.0f;
	}
	const int32 ExistingCount = PendingStamps.Num();
	const int32 NearlyFullBudget = FTDFluidWaterSplashRules::ResolveContactStampBudget(Settings, Config.MaxPointsPerTick, ExistingCount);
	TArray<FTDFluidMassInputPoint> NearlyFullPoints = MakeContactPoints(CenterWorld);
	TArray<FTDFluidBrushStamp> NearlyFullStamps;
	FTDFluidWaterSplashRules::BuildContactWhitewaterStamps(NearlyFullPoints, CenterWorld, NearlyFullBudget, Settings.ContactReadFallbackRadiusCm, NearlyFullStamps);
	TestEqual(TEXT("Contact stamps fill only the remaining slots"), NearlyFullStamps.Num(), Config.MaxPointsPerTick - ExistingCount);
	PendingStamps.Append(NearlyFullStamps);
	TestTrue(TEXT("Existing stamps plus contact stamps stay within the water stamp budget, so no existing stamp is cut"), PendingStamps.Num() <= Config.MaxPointsPerTick);
	TestEqual(TEXT("Every existing stamp is still queued"), PendingStamps.Num() - NearlyFullStamps.Num(), ExistingCount);

	const int32 OpenBudget = FTDFluidWaterSplashRules::ResolveContactStampBudget(Settings, Config.MaxPointsPerTick, 0);
	TArray<FTDFluidMassInputPoint> ContactPoints = MakeContactPoints(CenterWorld);
	TArray<FTDFluidBrushStamp> ContactStamps;
	FTDFluidWaterSplashRules::BuildContactWhitewaterStamps(ContactPoints, CenterWorld, OpenBudget, Settings.ContactReadFallbackRadiusCm, ContactStamps);
	if (!TestEqual(TEXT("An open queue takes the full contact budget"), ContactStamps.Num(), Settings.MaxContactStampsPerTick))
	{
		return false;
	}

	const TArray<float> Control = RunContactWater(TArray<FTDFluidBrushStamp>());
	const TArray<float> Contact = RunContactWater(ContactStamps);
	const TArray<float> ContactRepeat = RunContactWater(ContactStamps);
	if (!TestEqual(TEXT("Every run samples the whole grid"), Contact.Num(), ContactResolution * ContactResolution) || Control.Num() != Contact.Num() || ContactRepeat.Num() != Contact.Num())
	{
		return false;
	}

	int32 RiseCellIndex = 0;
	for (int32 CellIndex = 1; CellIndex < Contact.Num(); ++CellIndex)
	{
		if (Contact[CellIndex] - Control[CellIndex] > Contact[RiseCellIndex] - Control[RiseCellIndex])
		{
			RiseCellIndex = CellIndex;
		}
	}
	const FIntPoint RiseCell(RiseCellIndex % ContactResolution, RiseCellIndex / ContactResolution);
	const FIntPoint GridCenterCell(ContactResolution / 2, ContactResolution / 2);
	TestTrue(*FString::Printf(TEXT("Whitewater at the contact cell rises above the control (%.5f > %.5f)"), Contact[RiseCellIndex], Control[RiseCellIndex]), Contact[RiseCellIndex] > Control[RiseCellIndex] + 0.05f);
	TestTrue(TEXT("The strongest rise sits under the contact stamps"), FMath::Abs(RiseCell.X - GridCenterCell.X) <= 2 && FMath::Abs(RiseCell.Y - GridCenterCell.Y) <= 2);

	const int32 FarCellIndex = 2 * ContactResolution + 2;
	TestTrue(TEXT("A far cell stays bit-identical to the control"), HaveSameBits(Contact[FarCellIndex], Control[FarCellIndex]));
	TestTrue(TEXT("Two contact runs are bit-identical"), FMemory::Memcmp(Contact.GetData(), ContactRepeat.GetData(), Contact.Num() * sizeof(float)) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleFieldCellLookupTest,
	"TDGame.FluidFX.WaterParticle.FieldCellLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleFieldCellLookupTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleTestPrivate;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeContactWaterConfig(), FVector2D(130.0, -70.0));
	const FVector2D FirstCellCenter = Solver.CellToWorldCenter(0, 0);
	const double CellSizeCm = Solver.CellToWorldCenter(1, 0).X - FirstCellCenter.X;
	if (!TestTrue(TEXT("The solver cell size is positive"), CellSizeCm > 0.0))
	{
		return false;
	}

	bool bAllCentersResolve = true;
	for (int32 CellY = 0; CellY < ContactResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < ContactResolution; ++CellX)
		{
			FIntPoint Cell;
			const bool bResolved = FTDFluidWaterSplashRules::TryResolveFieldCell(Solver.CellToWorldCenter(CellX, CellY), FirstCellCenter, CellSizeCm, ContactResolution, Cell);
			bAllCentersResolve &= bResolved && Cell == FIntPoint(CellX, CellY);
		}
	}
	TestTrue(TEXT("Every solver cell center resolves to its own cell"), bAllCentersResolve);

	FIntPoint Cell;
	TestTrue(TEXT("A point inside a cell resolves to that cell"), FTDFluidWaterSplashRules::TryResolveFieldCell(FirstCellCenter + FVector2D(10.2 * CellSizeCm, 3.4 * CellSizeCm), FirstCellCenter, CellSizeCm, ContactResolution, Cell) && Cell == FIntPoint(10, 3));
	TestFalse(TEXT("A point before the first cell is outside"), FTDFluidWaterSplashRules::TryResolveFieldCell(FirstCellCenter - FVector2D(0.5 * CellSizeCm + 0.01, 0.0), FirstCellCenter, CellSizeCm, ContactResolution, Cell));
	TestFalse(TEXT("A point past the last cell is outside"), FTDFluidWaterSplashRules::TryResolveFieldCell(FirstCellCenter + FVector2D(0.0, (ContactResolution - 0.5) * CellSizeCm + 0.01), FirstCellCenter, CellSizeCm, ContactResolution, Cell));
	TestFalse(TEXT("A zero cell size is rejected"), FTDFluidWaterSplashRules::TryResolveFieldCell(FirstCellCenter, FirstCellCenter, 0.0, ContactResolution, Cell));
	TestFalse(TEXT("A negative cell size is rejected"), FTDFluidWaterSplashRules::TryResolveFieldCell(FirstCellCenter, FirstCellCenter, -CellSizeCm, ContactResolution, Cell));
	TestFalse(TEXT("A zero resolution is rejected"), FTDFluidWaterSplashRules::TryResolveFieldCell(FirstCellCenter, FirstCellCenter, CellSizeCm, 0, Cell));
	TestFalse(TEXT("A non-finite point is rejected"), FTDFluidWaterSplashRules::TryResolveFieldCell(FVector2D(std::numeric_limits<double>::infinity(), 0.0), FirstCellCenter, CellSizeCm, ContactResolution, Cell));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleTimeOriginRestartsTest,
	"TDGame.FluidFX.WaterParticle.TimeOriginRestarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleTimeOriginRestartsTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("water time counts from the origin"), ATDFluidWaterParticleActor::ComputeWaterTimeSeconds(61.43, 15.39), 46.04f, 1e-4f);
	TestNearlyEqual(TEXT("a zero origin keeps the world time"), ATDFluidWaterParticleActor::ComputeWaterTimeSeconds(12.5, 0.0), 12.5f, 1e-6f);
	TestNearlyEqual(TEXT("two stages at the same offset get the same water time"), ATDFluidWaterParticleActor::ComputeWaterTimeSeconds(108.20 + 6.0, 108.20), ATDFluidWaterParticleActor::ComputeWaterTimeSeconds(15.39 + 6.0, 15.39), 1e-5f);
	TestNearlyEqual(TEXT("water time keeps the fraction late in a session"), ATDFluidWaterParticleActor::ComputeWaterTimeSeconds(100000.25, 100000.0), 0.25f, 1e-6f);
	TestNearlyEqual(TEXT("water time before the origin stays signed like the material"), ATDFluidWaterParticleActor::ComputeWaterTimeSeconds(4.0, 10.0), -6.0f, 1e-6f);
	return true;
}

#endif
