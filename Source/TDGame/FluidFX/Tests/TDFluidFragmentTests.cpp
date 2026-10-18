#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/TDBreakableActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidEmberComponent.h"
#include "FluidFX/TDFluidFireActor.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidFragmentEmitterActor.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include <limits>

namespace TDFluidFragmentTestPrivate
{
	constexpr float FixedStepSeconds = 1.0f / 60.0f;
	constexpr double SpeedToleranceCmPerSec = 1.0;
	constexpr double LocationToleranceCm = 0.01;

	struct FTDFluidFragmentTestWorld
	{
		FTDFluidFragmentTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidFragmentTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidFragmentTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		void TickFrames(int32 FrameCount)
		{
			for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, FixedStepSeconds);
			}
		}

		UWorld* World = nullptr;
	};

	UTDFluidPresetAsset* MakeTargetPreset(UObject* Outer)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, ETDFluidPresetKind::Custom);
		Preset->SimulationConfig.GridResolution = 64;
		Preset->SimulationConfig.SimExtentCm = FVector2D(800.0, 800.0);
		Preset->SimulationConfig.MaxPointsPerTick = 256;
		Preset->PlaneSettings.Orientation = ETDFluidPlaneOrientation::CameraFacing;
		Preset->PlaneSettings.CameraDepthBiasCm = 60.0f;
		Preset->PlaneSettings.InteractionDepthCm = 400.0f;
		return Preset;
	}

	struct FTDFluidFragmentRig
	{
		explicit FTDFluidFragmentRig(FTDFluidFragmentTestWorld& TestWorld, float TrackingDurationSeconds)
		{
			UWorld* World = TestWorld.World;
			FireActor = World->SpawnActor<ATDFluidFireActor>(ATDFluidFireActor::StaticClass(), FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
			SmokeActor = World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), FVector(0.0, 0.0, 180.0), FRotator::ZeroRotator);
			Source = World->SpawnActor<ATDBreakableActor>(ATDBreakableActor::StaticClass(), FVector(0.0, 400.0, 50.0), FRotator::ZeroRotator);
			if (FireActor)
			{
				FireActor->InitializeFromPreset(MakeTargetPreset(GetTransientPackage()), 0.0f);
			}
			if (SmokeActor)
			{
				SmokeActor->InitializeFromPreset(MakeTargetPreset(GetTransientPackage()), 0.0f);
			}

			const FTransform EmitterTransform(FRotator::ZeroRotator, FVector(0.0, 200.0, 0.0));
			Emitter = World->SpawnActorDeferred<ATDFluidFragmentEmitterActor>(ATDFluidFragmentEmitterActor::StaticClass(), EmitterTransform);
			if (Emitter)
			{
				Emitter->SourceBreakableActor = Source;
				Emitter->FireTargetActor = FireActor;
				Emitter->SmokeTargetActor = SmokeActor;
				Emitter->TrailSettings.TrackingDurationSeconds = TrackingDurationSeconds;
				Emitter->FinishSpawning(EmitterTransform);
			}
		}

		bool IsComplete() const
		{
			return FireActor && SmokeActor && Source && Emitter;
		}

		ATDFluidFireActor* FireActor = nullptr;
		ATDFluidLiveActor* SmokeActor = nullptr;
		ATDBreakableActor* Source = nullptr;
		ATDFluidFragmentEmitterActor* Emitter = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentBuildStampsTest,
	"TDGame.FluidFX.Fragment.BuildStampsFromTransforms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentBuildStampsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	const FTDFluidFragmentTrailSettings Settings;
	constexpr int32 LeafCount = 7;
	const double NotANumber = std::numeric_limits<double>::quiet_NaN();
	const FVector Displacements[LeafCount] = {
		FVector(10.0, 0.0, 0.0),
		FVector::ZeroVector,
		FVector(60.0, 0.0, 0.0),
		FVector(NotANumber, 0.0, 0.0),
		FVector(0.0, 2000.0 * FixedStepSeconds, 0.0),
		FVector(0.0, 0.0, 3.0),
		FVector(-10.0, 0.0, 0.0)
	};

	TArray<FVector> Previous;
	TArray<FVector> Current;
	TArray<FVector> Lagged;
	for (int32 LeafIndex = 0; LeafIndex < LeafCount; ++LeafIndex)
	{
		const FVector Start(LeafIndex * 500.0, 0.0, 100.0);
		Previous.Add(Start);
		Current.Add(Start + Displacements[LeafIndex]);
		Lagged.Add(Start - FVector(0.0, 0.0, 40.0));
	}

	TArray<FTDFluidBrushStamp> FireStamps;
	TArray<FTDFluidBrushStamp> SmokeStamps;
	TArray<int32> StampedLeaves;
	ATDFluidFragmentEmitterActor::BuildFragmentTrailStamps(Previous, Current, Lagged, FixedStepSeconds, Settings, FireStamps, SmokeStamps, &StampedLeaves);

	TestEqual(TEXT("Only in-range finite leaves are stamped, fastest first, ties by index"), StampedLeaves, TArray<int32>({ 4, 0, 6, 5 }));
	TestEqual(TEXT("Fire stamps follow the sub-step counts 4+2+2+1"), FireStamps.Num(), 9);
	TestEqual(TEXT("One smoke stamp per stamped leaf"), SmokeStamps.Num(), 4);
	if (FireStamps.Num() != 9 || SmokeStamps.Num() != 4)
	{
		return false;
	}

	TestTrue(TEXT("The last fire stamp of the fastest leaf sits on its current position"), FireStamps[3].WorldLocation.Equals(Current[4], LocationToleranceCm));
	TestTrue(TEXT("The first fire stamp of the fastest leaf is a quarter along the segment"), FireStamps[0].WorldLocation.Equals(FMath::Lerp(Previous[4], Current[4], 0.25), LocationToleranceCm));
	TestNearlyEqual(TEXT("Stamp velocity is the leaf velocity times the inheritance"), FireStamps[0].WorldVelocity.Y, 2000.0 * Settings.VelocityInheritance, SpeedToleranceCmPerSec);
	TestTrue(TEXT("The slow leaf gets one fire stamp on its current position"), FireStamps[8].WorldLocation.Equals(Current[5], LocationToleranceCm));

	for (const FTDFluidBrushStamp& Stamp : FireStamps)
	{
		TestEqual(TEXT("Fire stamps use the fire channel"), Stamp.Channel, ETDFluidBrushChannel::ChannelB_Fire);
		TestNearlyEqual(TEXT("Fire stamp radius"), Stamp.RadiusCm, Settings.FireStampRadiusCm, 1e-4f);
		TestNearlyEqual(TEXT("Fire stamp density"), Stamp.DensityStrength, Settings.FireDensityStrength, 1e-4f);
		TestNearlyEqual(TEXT("Fire stamp velocity strength"), Stamp.VelocityStrength, 1.0f, 1e-4f);
		TestNearlyEqual(TEXT("Fire stamps carry no radial push"), Stamp.RadialVelocityCmPerSec, 0.0f, 1e-4f);
	}

	for (const FTDFluidBrushStamp& Stamp : SmokeStamps)
	{
		TestEqual(TEXT("Smoke stamps use the mud channel"), Stamp.Channel, ETDFluidBrushChannel::ChannelR_Mud);
		TestNearlyEqual(TEXT("Smoke stamp radius"), Stamp.RadiusCm, Settings.SmokeStampRadiusCm, 1e-4f);
		TestNearlyEqual(TEXT("Smoke stamp density"), Stamp.DensityStrength, Settings.SmokeDensityStrength, 1e-4f);
	}

	FTDFluidFragmentTrailSettings CappedLeafSettings = Settings;
	CappedLeafSettings.MaxFragmentsPerStep = 2;
	ATDFluidFragmentEmitterActor::BuildFragmentTrailStamps(Previous, Current, Lagged, FixedStepSeconds, CappedLeafSettings, FireStamps, SmokeStamps, &StampedLeaves);
	TestEqual(TEXT("The per-step leaf budget keeps the two fastest leaves"), StampedLeaves, TArray<int32>({ 4, 0 }));
	TestEqual(TEXT("The per-step budget limits fire stamps to 4+2"), FireStamps.Num(), 6);

	FTDFluidFragmentTrailSettings SingleStampSettings = Settings;
	SingleStampSettings.MaxStampsPerFragment = 1;
	ATDFluidFragmentEmitterActor::BuildFragmentTrailStamps(Previous, Current, Lagged, FixedStepSeconds, SingleStampSettings, FireStamps, SmokeStamps, &StampedLeaves);
	TestEqual(TEXT("One stamp per fragment gives one fire stamp per selected leaf"), FireStamps.Num(), 4);

	TArray<FVector> ShortLagged = Lagged;
	ShortLagged.Pop();
	ATDFluidFragmentEmitterActor::BuildFragmentTrailStamps(Previous, Current, ShortLagged, FixedStepSeconds, Settings, FireStamps, SmokeStamps, &StampedLeaves);
	TestEqual(TEXT("Mismatched input sizes produce no stamps"), FireStamps.Num() + SmokeStamps.Num() + StampedLeaves.Num(), 0);

	ATDFluidFragmentEmitterActor::BuildFragmentTrailStamps(Previous, Current, Lagged, 0.0f, Settings, FireStamps, SmokeStamps, &StampedLeaves);
	TestEqual(TEXT("A zero time step produces no stamps"), FireStamps.Num() + SmokeStamps.Num() + StampedLeaves.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentSmokeLagTest,
	"TDGame.FluidFX.Fragment.SmokeStampsUseLaggedPositions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentSmokeLagTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	const auto MakeFrame = [](int32 FrameNumber)
	{
		return TArray<FVector>({ FVector(FrameNumber * 10.0, 0.0, 0.0), FVector(0.0, FrameNumber * 20.0, 0.0) });
	};

	FTDFluidFragmentPositionHistory History;
	History.Reset(2);
	for (int32 FrameNumber = 0; FrameNumber < 10; ++FrameNumber)
	{
		History.PushFrame(MakeFrame(FrameNumber));
	}

	TArray<FVector> LaggedFrame;
	History.GetLaggedFrame(6, LaggedFrame);
	TestTrue(TEXT("Six frames of lag after ten frames reads frame three"), LaggedFrame.Num() == 2 && LaggedFrame[0].Equals(FVector(30.0, 0.0, 0.0)) && LaggedFrame[1].Equals(FVector(0.0, 60.0, 0.0)));
	History.GetLaggedFrame(0, LaggedFrame);
	TestTrue(TEXT("Zero lag reads the newest frame"), LaggedFrame.Num() == 2 && LaggedFrame[0].Equals(FVector(90.0, 0.0, 0.0)));
	History.GetLaggedFrame(99, LaggedFrame);
	TestTrue(TEXT("A lag beyond the ring clamps to seven frames"), LaggedFrame.Num() == 2 && LaggedFrame[0].Equals(FVector(20.0, 0.0, 0.0)));
	History.GetLaggedFrame(-3, LaggedFrame);
	TestTrue(TEXT("A negative lag clamps to the newest frame"), LaggedFrame.Num() == 2 && LaggedFrame[0].Equals(FVector(90.0, 0.0, 0.0)));

	FTDFluidFragmentPositionHistory ShortHistory;
	ShortHistory.Reset(2);
	for (int32 FrameNumber = 0; FrameNumber < 3; ++FrameNumber)
	{
		ShortHistory.PushFrame(MakeFrame(FrameNumber));
	}
	ShortHistory.GetLaggedFrame(6, LaggedFrame);
	TestTrue(TEXT("A lag longer than the history reads the first frame"), LaggedFrame.Num() == 2 && LaggedFrame[0].Equals(FVector::ZeroVector));

	const FTDFluidFragmentTrailSettings Settings;
	TArray<FVector> Previous = MakeFrame(8);
	TArray<FVector> Current = MakeFrame(9);
	History.GetLaggedFrame(Settings.SmokeTrailLagFrames, LaggedFrame);

	TArray<FTDFluidBrushStamp> FireStamps;
	TArray<FTDFluidBrushStamp> SmokeStamps;
	ATDFluidFragmentEmitterActor::BuildFragmentTrailStamps(Previous, Current, LaggedFrame, FixedStepSeconds, Settings, FireStamps, SmokeStamps);
	if (!TestEqual(TEXT("Both leaves leave a smoke stamp"), SmokeStamps.Num(), 2))
	{
		return false;
	}

	const auto HasSmokeStampAt = [&SmokeStamps](const FVector& Location)
	{
		return SmokeStamps.ContainsByPredicate([&Location](const FTDFluidBrushStamp& Stamp)
		{
			return Stamp.WorldLocation.Equals(Location, LocationToleranceCm);
		});
	};
	TestTrue(TEXT("The slow leaf leaves smoke six frames behind it"), HasSmokeStampAt(FVector(30.0, 0.0, 0.0)));
	TestTrue(TEXT("The fast leaf leaves smoke six frames behind it"), HasSmokeStampAt(FVector(0.0, 60.0, 0.0)));
	TestFalse(TEXT("No smoke stamp sits on a current leaf position"), HasSmokeStampAt(Current[0]) || HasSmokeStampAt(Current[1]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentBurstRingTest,
	"TDGame.FluidFX.Fragment.BurstUsesRingVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentBurstRingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	const FTDFluidFragmentTrailSettings Settings;
	const FTDFluidPlaneBasis Basis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(-25.0, 40.0, 0.0), 0.0f);
	const FVector Center(100.0, 200.0, 300.0);

	TArray<FTDFluidBrushStamp> FireStamps;
	ATDFluidFragmentEmitterActor::BuildBurstStamps(Center, Basis, Settings, ETDFluidBrushChannel::ChannelB_Fire, 1.0f, Settings.FireDensityStrength, FireStamps);
	if (!TestEqual(TEXT("A burst is one core stamp plus the ring"), FireStamps.Num(), 1 + Settings.BurstRingStampCount))
	{
		return false;
	}

	TestTrue(TEXT("The core stamp sits on the burst centre"), FireStamps[0].WorldLocation.Equals(Center, LocationToleranceCm));
	TestNearlyEqual(TEXT("The core stamp does not push the field"), FireStamps[0].VelocityStrength, 0.0f, 1e-4f);
	TestNearlyEqual(TEXT("The core stamp radius"), FireStamps[0].RadiusCm, Settings.BurstCoreRadiusCm, 1e-4f);

	FVector RingVelocitySum = FVector::ZeroVector;
	for (int32 StampIndex = 1; StampIndex < FireStamps.Num(); ++StampIndex)
	{
		const FTDFluidBrushStamp& Stamp = FireStamps[StampIndex];
		const FVector Offset = Stamp.WorldLocation - Center;
		RingVelocitySum += Stamp.WorldVelocity;
		TestNearlyEqual(TEXT("Ring stamps sit at the ring distance"), Offset.Size(), static_cast<double>(Settings.BurstRingRadiusCm), LocationToleranceCm);
		TestTrue(TEXT("Ring velocity points straight outward"), FVector::DotProduct(Stamp.WorldVelocity.GetSafeNormal(), Offset.GetSafeNormal()) > 0.999);
		TestNearlyEqual(TEXT("Ring speed"), Stamp.WorldVelocity.Size(), static_cast<double>(Settings.BurstRingSpeedCmPerSec), SpeedToleranceCmPerSec);
		TestTrue(TEXT("Ring velocity lies in the fluid plane"), FMath::Abs(FVector::DotProduct(Stamp.WorldVelocity.GetSafeNormal(), Basis.N)) < 1e-3);
		TestNearlyEqual(TEXT("Ring stamps rely on world velocity, not radial push"), Stamp.RadialVelocityCmPerSec, 0.0f, 1e-6f);
		TestNearlyEqual(TEXT("Ring stamps drive the field"), Stamp.VelocityStrength, 1.0f, 1e-4f);
		TestNearlyEqual(TEXT("Ring stamp radius is half the ring distance"), Stamp.RadiusCm, Settings.BurstRingRadiusCm * 0.5f, 1e-4f);
	}

	TestTrue(TEXT("The ring is symmetric around the centre"), RingVelocitySum.Size() < SpeedToleranceCmPerSec);

	TArray<FTDFluidBrushStamp> SmokeStamps;
	ATDFluidFragmentEmitterActor::BuildBurstStamps(Center, Basis, Settings, ETDFluidBrushChannel::ChannelR_Mud, 1.5f, Settings.SmokeDensityStrength, SmokeStamps);
	if (!TestEqual(TEXT("The smoke burst has the same stamp count"), SmokeStamps.Num(), FireStamps.Num()))
	{
		return false;
	}

	TestEqual(TEXT("The smoke burst uses the mud channel"), SmokeStamps[1].Channel, ETDFluidBrushChannel::ChannelR_Mud);
	TestNearlyEqual(TEXT("The smoke burst radius is 1.5 times larger"), SmokeStamps[0].RadiusCm, Settings.BurstCoreRadiusCm * 1.5f, 1e-4f);
	TestNearlyEqual(TEXT("The smoke ring radius is 1.5 times larger"), SmokeStamps[1].RadiusCm, FireStamps[1].RadiusCm * 1.5f, 1e-4f);
	TestNearlyEqual(TEXT("The smoke ring keeps the ring distance"), (SmokeStamps[1].WorldLocation - Center).Size(), static_cast<double>(Settings.BurstRingRadiusCm), LocationToleranceCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentTrackerStopsTest,
	"TDGame.FluidFX.Fragment.TrackerStopsWhenSourceGone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentTrackerStopsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	FTDFluidFragmentTestWorld TestWorld;
	FTDFluidFragmentRig Rig(TestWorld, 10.0f);
	if (!TestTrue(TEXT("The fragment rig spawns"), Rig.IsComplete()))
	{
		return false;
	}

	TestWorld.TickFrames(30);
	TestFalse(TEXT("An intact source is only polled"), Rig.Emitter->IsTrackingFragments());
	TestTrue(TEXT("The armed emitter keeps ticking"), Rig.Emitter->IsActorTickEnabled());
	TestNearlyEqual(TEXT("The armed emitter polls at the configured interval"), Rig.Emitter->GetActorTickInterval(), Rig.Emitter->TrailSettings.ArmedPollIntervalSeconds, 1e-4f);

	Rig.Emitter->TriggerBreakAt(FVector(0.0, 400.0, 50.0));
	TestTrue(TEXT("Triggering the break starts tracking"), Rig.Emitter->IsTrackingFragments());
	TestEqual(TEXT("The source collection has no rigid leaves yet"), Rig.Emitter->GetTrackedLeafCount(), 0);
	TestEqual(TEXT("The burst queues a core and a ring on both targets"), Rig.Emitter->GetTotalStampCount(), 2 * (1 + Rig.Emitter->TrailSettings.BurstRingStampCount));
	TestEqual(TEXT("The fire embers burst"), Rig.FireActor->EmberComponent->GetAliveEmberCount(), Rig.Emitter->TrailSettings.EmberBurstCount);
	TestTrue(TEXT("Both targets are awake"), Rig.FireActor->LiveComponent->IsSimulationAwake() && Rig.SmokeActor->LiveComponent->IsSimulationAwake());
	TestNearlyEqual(TEXT("Tracking ticks every frame"), Rig.Emitter->GetActorTickInterval(), 0.0f, 1e-6f);

	const int32 StampCountAfterBurst = Rig.Emitter->GetTotalStampCount();
	Rig.Emitter->TriggerBreakAt(FVector(0.0, 400.0, 50.0));
	TestEqual(TEXT("A second trigger does nothing"), Rig.Emitter->GetTotalStampCount(), StampCountAfterBurst);

	TestWorld.TickFrames(10);
	TestTrue(TEXT("Tracking continues while the source lives"), Rig.Emitter->IsTrackingFragments());

	Rig.Source->Destroy();
	TestWorld.TickFrames(2);
	TestFalse(TEXT("Tracking stops once the source is gone"), Rig.Emitter->IsTrackingFragments());
	TestFalse(TEXT("The emitter stops ticking once the source is gone"), Rig.Emitter->IsActorTickEnabled());

	FTDFluidFragmentRig ArmedRig(TestWorld, 10.0f);
	if (!TestTrue(TEXT("The second rig spawns"), ArmedRig.IsComplete()))
	{
		return false;
	}

	TestWorld.TickFrames(1);
	TestTrue(TEXT("The second emitter is armed"), ArmedRig.Emitter->IsActorTickEnabled());
	ArmedRig.Source->Destroy();
	TestWorld.TickFrames(12);
	TestFalse(TEXT("An armed emitter stops ticking once the source is gone"), ArmedRig.Emitter->IsActorTickEnabled());
	TestFalse(TEXT("An armed emitter never started tracking"), ArmedRig.Emitter->IsTrackingFragments());

	FTDFluidFragmentRig ShortRig(TestWorld, 0.25f);
	if (!TestTrue(TEXT("The third rig spawns"), ShortRig.IsComplete()))
	{
		return false;
	}

	ShortRig.Emitter->TriggerBreakAt(FVector(0.0, 400.0, 50.0));
	TestWorld.TickFrames(30);
	TestTrue(TEXT("The source is still alive after the short tracking window"), IsValid(ShortRig.Source));
	TestFalse(TEXT("Tracking stops when the duration ends"), ShortRig.Emitter->IsTrackingFragments());
	TestFalse(TEXT("The emitter stops ticking when the duration ends"), ShortRig.Emitter->IsActorTickEnabled());
	return true;
}

#endif
