#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/TDBreakableActor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Field/FieldSystemObjects.h"
#include "FluidFX/TDFluidEmberComponent.h"
#include "FluidFX/TDFluidFireActor.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidFragmentEmitterActor.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "TDFluidGpuStampBins.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

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

		void TickSeconds(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
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
		explicit FTDFluidFragmentRig(FTDFluidFragmentTestWorld& TestWorld, float TrackingDurationSeconds, const TCHAR* GlowMaterialPath = nullptr)
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
				if (GlowMaterialPath)
				{
					Emitter->FragmentGlowMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(GlowMaterialPath));
				}
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

namespace TDFluidFragmentTestPrivate
{
	const TCHAR* const GlowMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	constexpr float GlowTolerance = 1e-4f;
	constexpr int32 GlowMissedFrameAllowance = 8;

	bool WaitForGlowMaterial(FTDFluidFragmentTestWorld& TestWorld, const ATDFluidFragmentEmitterActor& Emitter)
	{
		for (int32 Attempt = 0; Attempt < 200 && !Emitter.FragmentGlowMaterialAsset.Get(); ++Attempt)
		{
			FlushAsyncLoading();
			TestWorld.TickFrames(1);
		}
		return Emitter.FragmentGlowMaterialAsset.Get() != nullptr;
	}

	UGeometryCollectionComponent* FindSourceCollection(const FTDFluidFragmentRig& Rig)
	{
		return Rig.Source ? Rig.Source->FindComponentByClass<UGeometryCollectionComponent>() : nullptr;
	}

	float ReadGlowScalar(UMaterialInstanceDynamic* GlowMaterial, const TCHAR* ParameterName)
	{
		return GlowMaterial ? GlowMaterial->K2_GetScalarParameterValue(ParameterName) : -1.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentGlowHeatRatioTest,
	"TDGame.FluidFX.Fragment.GlowHeatRatioFollowsBreakAge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentGlowHeatRatioTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	FTDFluidFragmentGlowSettings Settings;
	Settings.HoldSeconds = 0.25f;
	Settings.CoolSeconds = 2.0f;
	Settings.CharredHeatRatio = 0.05f;
	const auto RatioAt = [&Settings](float BreakAgeSeconds)
	{
		return ATDFluidFragmentEmitterActor::ComputeFragmentGlowHeatRatio(Settings, BreakAgeSeconds);
	};

	TestNearlyEqual(TEXT("An unbroken fragment has no glow"), RatioAt(-0.01f), 0.0f, GlowTolerance);
	TestNearlyEqual(TEXT("A non-finite age has no glow"), RatioAt(std::numeric_limits<float>::quiet_NaN()), 0.0f, GlowTolerance);
	TestNearlyEqual(TEXT("Glow is full at the break"), RatioAt(0.0f), 1.0f, GlowTolerance);
	TestNearlyEqual(TEXT("Glow holds until the hold time ends"), RatioAt(0.25f), 1.0f, GlowTolerance);
	TestNearlyEqual(TEXT("Halfway through cooling the glow is halfway to charred"), RatioAt(1.25f), 0.525f, GlowTolerance);
	TestNearlyEqual(TEXT("Cooling ends at the charred ratio"), RatioAt(2.25f), 0.05f, GlowTolerance);
	TestNearlyEqual(TEXT("A long-cooled fragment stays charred"), RatioAt(100.0f), 0.05f, GlowTolerance);
	TestNearlyEqual(TEXT("The largest age stays charred"), RatioAt(TNumericLimits<float>::Max()), 0.05f, GlowTolerance);

	bool bNeverRises = true;
	float PreviousRatio = RatioAt(0.0f);
	for (int32 FrameIndex = 1; FrameIndex <= 180; ++FrameIndex)
	{
		const float CurrentRatio = RatioAt(static_cast<float>(FrameIndex) * FixedStepSeconds);
		bNeverRises &= CurrentRatio <= PreviousRatio + 1e-6f;
		PreviousRatio = CurrentRatio;
	}
	TestTrue(TEXT("Glow never rises while the fragment cools"), bNeverRises);

	FTDFluidFragmentGlowSettings InstantSettings = Settings;
	InstantSettings.CoolSeconds = 0.0f;
	TestNearlyEqual(TEXT("Zero cooling time keeps full glow during the hold"), ATDFluidFragmentEmitterActor::ComputeFragmentGlowHeatRatio(InstantSettings, 0.2f), 1.0f, GlowTolerance);
	TestNearlyEqual(TEXT("Zero cooling time drops to charred after the hold"), ATDFluidFragmentEmitterActor::ComputeFragmentGlowHeatRatio(InstantSettings, 0.3f), 0.05f, GlowTolerance);

	FTDFluidFragmentGlowSettings OverbrightSettings = Settings;
	OverbrightSettings.CharredHeatRatio = 2.0f;
	TestNearlyEqual(TEXT("The charred ratio is clamped to full glow"), ATDFluidFragmentEmitterActor::ComputeFragmentGlowHeatRatio(OverbrightSettings, 5.0f), 1.0f, GlowTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentGlowMaterialTest,
	"TDGame.FluidFX.Fragment.GlowMaterialAppliesOnBreakAndCools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentGlowMaterialTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	FTDFluidFragmentTestWorld TestWorld;
	FTDFluidFragmentRig Rig(TestWorld, 10.0f, GlowMaterialPath);
	if (!TestTrue(TEXT("The glow rig spawns"), Rig.IsComplete()))
	{
		return false;
	}

	UGeometryCollectionComponent* Collection = FindSourceCollection(Rig);
	if (!TestNotNull(TEXT("The source has a geometry collection"), Collection))
	{
		return false;
	}

	if (!TestTrue(TEXT("The glow material loads asynchronously"), WaitForGlowMaterial(TestWorld, *Rig.Emitter)))
	{
		return false;
	}

	TestWorld.TickFrames(10);
	TestNull(TEXT("No glow material exists before the break"), Rig.Emitter->GetFragmentGlowMaterial());
	TestNull(TEXT("The intact collection keeps its own material"), Cast<UMaterialInstanceDynamic>(Collection->GetMaterial(0)));

	Rig.Emitter->TriggerBreakAt(FVector(0.0, 400.0, 50.0));
	UMaterialInstanceDynamic* GlowMaterial = Rig.Emitter->GetFragmentGlowMaterial();
	if (!TestNotNull(TEXT("The break creates the glow material"), GlowMaterial))
	{
		return false;
	}

	const FTDFluidFragmentGlowSettings& Settings = Rig.Emitter->GlowSettings;
	TestEqual(TEXT("The glow does not change the burst stamps"), Rig.Emitter->GetTotalStampCount(), 2 * (1 + Rig.Emitter->TrailSettings.BurstRingStampCount));
	TestTrue(TEXT("The fragment slot uses the glow material"), Collection->GetMaterial(0) == GlowMaterial);
	TestTrue(TEXT("The glow material derives from the configured asset"), GlowMaterial->Parent == Rig.Emitter->FragmentGlowMaterialAsset.Get());
	TestNearlyEqual(TEXT("Glow is full at the break"), ReadGlowScalar(GlowMaterial, TEXT("FragmentHeatRatio")), 1.0f, GlowTolerance);
	TestNearlyEqual(TEXT("Glow intensity is pushed"), ReadGlowScalar(GlowMaterial, TEXT("FragmentGlowIntensity")), Settings.GlowIntensity, GlowTolerance);
	TestNearlyEqual(TEXT("Fire heat weight is pushed"), ReadGlowScalar(GlowMaterial, TEXT("FragmentFireHeatWeight")), Settings.FireHeatWeight, GlowTolerance);
	TestNearlyEqual(TEXT("Fire heat slab is pushed"), ReadGlowScalar(GlowMaterial, TEXT("FragmentFireHeatSlabCm")), Settings.FireHeatSlabCm, GlowTolerance);
	TestTrue(TEXT("Hot glow color is pushed"), GlowMaterial->K2_GetVectorParameterValue(TEXT("FragmentHotGlowColor")).Equals(Settings.HotGlowColor, GlowTolerance));
	TestTrue(TEXT("Cold glow color is pushed"), GlowMaterial->K2_GetVectorParameterValue(TEXT("FragmentColdGlowColor")).Equals(Settings.ColdGlowColor, GlowTolerance));

	const float FireResolution = static_cast<float>(Rig.FireActor->LiveComponent->GetSimulationResolution());
	TestTrue(TEXT("The fire simulation is initialised"), FireResolution > 0.0f);
	TestNearlyEqual(TEXT("The fire heat field drives the glow material"), ReadGlowScalar(GlowMaterial, TEXT("SimGridResolution")), FireResolution, GlowTolerance);

	constexpr int32 CoolingFrames = 60;
	TestWorld.TickFrames(CoolingFrames);
	const float CoolingRatio = ReadGlowScalar(GlowMaterial, TEXT("FragmentHeatRatio"));
	const float EarliestRatio = ATDFluidFragmentEmitterActor::ComputeFragmentGlowHeatRatio(Settings, static_cast<float>(CoolingFrames) * FixedStepSeconds);
	const float LatestRatio = ATDFluidFragmentEmitterActor::ComputeFragmentGlowHeatRatio(Settings, static_cast<float>(CoolingFrames - GlowMissedFrameAllowance) * FixedStepSeconds);
	TestTrue(TEXT("One second after the break the fragment is cooling"), CoolingRatio < 1.0f - GlowTolerance && CoolingRatio > Settings.CharredHeatRatio + GlowTolerance);
	TestTrue(TEXT("The pushed glow follows the break age"), CoolingRatio >= EarliestRatio - 1e-3f && CoolingRatio <= LatestRatio + 1e-3f);

	const bool bCanCreateTextures = !IsRunningCommandlet() && FApp::CanEverRender();
	UTexture* FireDensityTexture = Rig.FireActor->LiveComponent->GetDensityPressureTexture();
	UTexture* GlowDensityTexture = GlowMaterial->K2_GetTextureParameterValue(TEXT("DensityPressureTexture"));
	TestTrue(TEXT("The glow density texture exists whenever rendering is possible"), (GlowDensityTexture != nullptr) == bCanCreateTextures);
	TestTrue(TEXT("The glow material samples the fire density texture"), GlowDensityTexture == FireDensityTexture);

	TestWorld.TickFrames(FMath::CeilToInt32((Settings.HoldSeconds + Settings.CoolSeconds) / FixedStepSeconds));
	TestNearlyEqual(TEXT("A cooled fragment is charred"), ReadGlowScalar(GlowMaterial, TEXT("FragmentHeatRatio")), Settings.CharredHeatRatio, GlowTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentGlowDefaultsTest,
	"TDGame.FluidFX.Fragment.GlowOffWithoutMaterialAndCharredAfterTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentGlowDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	FTDFluidFragmentTestWorld TestWorld;
	FTDFluidFragmentRig PlainRig(TestWorld, 10.0f);
	if (!TestTrue(TEXT("The plain rig spawns"), PlainRig.IsComplete()))
	{
		return false;
	}

	UGeometryCollectionComponent* PlainCollection = FindSourceCollection(PlainRig);
	if (!TestNotNull(TEXT("The plain source has a geometry collection"), PlainCollection))
	{
		return false;
	}

	PlainRig.Emitter->TriggerBreakAt(FVector(0.0, 400.0, 50.0));
	TestEqual(TEXT("Without a glow asset the burst stamps keep the tracker contract"), PlainRig.Emitter->GetTotalStampCount(), 2 * (1 + PlainRig.Emitter->TrailSettings.BurstRingStampCount));
	TestWorld.TickFrames(30);
	TestNull(TEXT("Without a glow asset no glow material is made"), PlainRig.Emitter->GetFragmentGlowMaterial());
	TestNull(TEXT("Without a glow asset the collection material is untouched"), PlainCollection->GetMaterial(0));

	FTDFluidFragmentRig ShortRig(TestWorld, 0.1f, GlowMaterialPath);
	if (!TestTrue(TEXT("The short glow rig spawns"), ShortRig.IsComplete()))
	{
		return false;
	}

	if (!TestTrue(TEXT("The short rig glow material loads"), WaitForGlowMaterial(TestWorld, *ShortRig.Emitter)))
	{
		return false;
	}

	ShortRig.Emitter->TriggerBreakAt(FVector(0.0, 400.0, 50.0));
	UMaterialInstanceDynamic* GlowMaterial = ShortRig.Emitter->GetFragmentGlowMaterial();
	if (!TestNotNull(TEXT("The short rig creates the glow material"), GlowMaterial))
	{
		return false;
	}

	TestNearlyEqual(TEXT("The short rig glows at the break"), ReadGlowScalar(GlowMaterial, TEXT("FragmentHeatRatio")), 1.0f, GlowTolerance);
	TestWorld.TickFrames(30);
	TestFalse(TEXT("The short tracking window has ended"), ShortRig.Emitter->IsTrackingFragments());
	TestNearlyEqual(TEXT("Glow is charred once tracking stops"), ReadGlowScalar(GlowMaterial, TEXT("FragmentHeatRatio")), ShortRig.Emitter->GlowSettings.CharredHeatRatio, GlowTolerance);
	return true;
}

namespace TDFluidFragmentTestPrivate
{
	constexpr int32 FaceShadingValueCount = 3;
	const TCHAR* const FaceShadingPropertyNames[FaceShadingValueCount] = {TEXT("FacingContrast"), TEXT("EdgeCharWidth"), TEXT("EdgeCharStrength")};
	const TCHAR* const FaceShadingParameterNames[FaceShadingValueCount] = {TEXT("FragmentFacingContrast"), TEXT("FragmentEdgeCharWidth"), TEXT("FragmentEdgeCharStrength")};
	constexpr float FaceShadingDefaultValues[FaceShadingValueCount] = {0.0f, 0.12f, 0.5f};
	constexpr float FaceShadingTunedValues[FaceShadingValueCount] = {0.6f, 0.2f, 0.25f};
	constexpr float KeyCenterToleranceCm = 0.01f;
	constexpr float TunedBurstCenterHeightOffsetCm = 30.0f;

	FFloatProperty* FindFaceShadingProperty(FAutomationTestBase& Test, const TCHAR* PropertyName)
	{
		FFloatProperty* Property = FindFProperty<FFloatProperty>(FTDFluidFragmentGlowSettings::StaticStruct(), PropertyName);
		if (!Property)
		{
			Test.AddError(FString::Printf(TEXT("FTDFluidFragmentGlowSettings has no float property %s"), PropertyName));
		}
		return Property;
	}

	bool TestPushedFaceShading(FAutomationTestBase& Test, const ATDFluidFragmentEmitterActor& Emitter, const float (&ExpectedValues)[FaceShadingValueCount], const FVector& ExpectedKeyCenterWorld, const TCHAR* Moment)
	{
		UMaterialInstanceDynamic* GlowMaterial = Emitter.GetFragmentGlowMaterial();
		if (!GlowMaterial)
		{
			Test.AddError(FString::Printf(TEXT("%s: the break made no glow material"), Moment));
			return false;
		}

		bool bAllPushed = true;
		for (int32 ValueIndex = 0; ValueIndex < FaceShadingValueCount; ++ValueIndex)
		{
			const float PushedValue = ReadGlowScalar(GlowMaterial, FaceShadingParameterNames[ValueIndex]);
			bAllPushed &= Test.TestNearlyEqual(FString::Printf(TEXT("%s: %s is pushed"), Moment, FaceShadingParameterNames[ValueIndex]), PushedValue, ExpectedValues[ValueIndex], GlowTolerance);
		}

		const FLinearColor PushedKeyCenter = GlowMaterial->K2_GetVectorParameterValue(TEXT("FragmentKeyCenterWorld"));
		const FVector PushedKeyCenterWorld(PushedKeyCenter.R, PushedKeyCenter.G, PushedKeyCenter.B);
		bAllPushed &= Test.TestNearlyEqual(FString::Printf(TEXT("%s: the burst center is the hit raised by the burst height offset"), Moment), Emitter.GetBurstCenterWorld(), ExpectedKeyCenterWorld, KeyCenterToleranceCm);
		bAllPushed &= Test.TestNearlyEqual(FString::Printf(TEXT("%s: the key center is the burst center"), Moment), PushedKeyCenterWorld, ExpectedKeyCenterWorld, KeyCenterToleranceCm);
		return bAllPushed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentFaceShadingDefaultsTest,
	"TDGame.FluidFX.Fragment.FaceShadingDefaultsKeepLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentFaceShadingDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	const FTDFluidFragmentGlowSettings DefaultSettings;
	for (int32 ValueIndex = 0; ValueIndex < FaceShadingValueCount; ++ValueIndex)
	{
		const FFloatProperty* Property = FindFaceShadingProperty(*this, FaceShadingPropertyNames[ValueIndex]);
		if (!Property)
		{
			return false;
		}

		TestTrue(FString::Printf(TEXT("%s is editable"), FaceShadingPropertyNames[ValueIndex]), Property->HasAnyPropertyFlags(CPF_Edit));
		TestNearlyEqual(FString::Printf(TEXT("%s keeps the master material default"), FaceShadingPropertyNames[ValueIndex]), Property->GetPropertyValue_InContainer(&DefaultSettings), FaceShadingDefaultValues[ValueIndex], 1e-6f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentGlowFaceShadingPushTest,
	"TDGame.FluidFX.Fragment.GlowPushesFaceShading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentGlowFaceShadingPushTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	FTDFluidFragmentTestWorld TestWorld;
	FTDFluidFragmentRig DefaultRig(TestWorld, 10.0f, GlowMaterialPath);
	if (!TestTrue(TEXT("The default glow rig spawns"), DefaultRig.IsComplete()))
	{
		return false;
	}

	if (!TestTrue(TEXT("The default rig glow material loads"), WaitForGlowMaterial(TestWorld, *DefaultRig.Emitter)))
	{
		return false;
	}

	const FVector DefaultHitLocation(0.0, 400.0, 50.0);
	DefaultRig.Emitter->TriggerBreakAt(DefaultHitLocation);
	TestPushedFaceShading(*this, *DefaultRig.Emitter, FaceShadingDefaultValues, DefaultHitLocation, TEXT("Default settings at the break"));

	FTDFluidFragmentRig TunedRig(TestWorld, 10.0f, GlowMaterialPath);
	if (!TestTrue(TEXT("The tuned glow rig spawns"), TunedRig.IsComplete()))
	{
		return false;
	}

	for (int32 ValueIndex = 0; ValueIndex < FaceShadingValueCount; ++ValueIndex)
	{
		const FFloatProperty* Property = FindFaceShadingProperty(*this, FaceShadingPropertyNames[ValueIndex]);
		if (!Property)
		{
			return false;
		}
		Property->SetPropertyValue_InContainer(&TunedRig.Emitter->GlowSettings, FaceShadingTunedValues[ValueIndex]);
	}

	TunedRig.Emitter->TrailSettings.BurstCenterHeightOffsetCm = TunedBurstCenterHeightOffsetCm;
	const FVector TunedHitLocation(40.0, 360.0, 70.0);
	const FVector TunedKeyCenterWorld = TunedHitLocation + FVector::UpVector * TunedBurstCenterHeightOffsetCm;
	TestFalse(TEXT("The tuned key center is not the source location"), TunedKeyCenterWorld.Equals(TunedRig.Source->GetActorLocation(), 1.0));

	if (!TestTrue(TEXT("The tuned rig glow material loads"), WaitForGlowMaterial(TestWorld, *TunedRig.Emitter)))
	{
		return false;
	}

	TunedRig.Emitter->TriggerBreakAt(TunedHitLocation);
	TestPushedFaceShading(*this, *TunedRig.Emitter, FaceShadingTunedValues, TunedKeyCenterWorld, TEXT("Tuned settings right after the break"));
	TestWorld.TickFrames(10);
	TestPushedFaceShading(*this, *TunedRig.Emitter, FaceShadingTunedValues, TunedKeyCenterWorld, TEXT("Tuned settings ten frames after the break"));
	return true;
}

namespace TDFluidFragmentTestPrivate
{
	const TCHAR* const EmberCubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	constexpr float EmberBurstRaisedOffsetCm = 100.0f;
	constexpr double EmberBurstHeightToleranceCm = 5.0;
	constexpr int32 MinVisibleBurstEmbers = 40;
	constexpr int32 MaxBurstSettleFrames = 10;

	FStructProperty* FindBreakDirectionOffsetProperty(FAutomationTestBase& Test)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(ATDBreakableActor::StaticClass(), TEXT("BreakDirectionOffsetCm"));
		if (!Property || Property->Struct != TBaseStructure<FVector>::Get())
		{
			Test.AddError(TEXT("ATDBreakableActor has no FVector property BreakDirectionOffsetCm"));
			return nullptr;
		}
		return Property;
	}

	struct FTDFluidEmberBurstHeightSample
	{
		int32 VisibleEmberCount = 0;
		int32 SettleFrameCount = 0;
		double MeanHeightCm = 0.0;
		FVector BurstCenterWorld = FVector::ZeroVector;
	};

	void CollectVisibleEmberHeights(const UTDFluidEmberComponent& Ember, FTDFluidEmberBurstHeightSample& OutSample)
	{
		double HeightSumCm = 0.0;
		OutSample.VisibleEmberCount = 0;
		for (int32 InstanceIndex = 0; InstanceIndex < Ember.GetInstanceCount(); ++InstanceIndex)
		{
			FTransform InstanceTransform;
			Ember.GetInstanceTransform(InstanceIndex, InstanceTransform, true);
			if (InstanceTransform.GetScale3D().X <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			HeightSumCm += InstanceTransform.GetLocation().Z;
			++OutSample.VisibleEmberCount;
		}
		OutSample.MeanHeightCm = OutSample.VisibleEmberCount > 0 ? HeightSumCm / OutSample.VisibleEmberCount : 0.0;
	}

	bool MeasureEmberBurstHeight(FAutomationTestBase& Test, float HeightOffsetCm, FTDFluidEmberBurstHeightSample& OutSample)
	{
		FTDFluidFragmentTestWorld TestWorld;
		FTDFluidFragmentRig Rig(TestWorld, 10.0f);
		if (!Rig.IsComplete() || !Rig.FireActor->EmberComponent)
		{
			Test.AddError(TEXT("The ember height rig did not spawn"));
			return false;
		}

		FFloatProperty* Property = FindFProperty<FFloatProperty>(FTDFluidFragmentTrailSettings::StaticStruct(), TEXT("EmberBurstHeightOffsetCm"));
		if (!Property)
		{
			Test.AddError(TEXT("FTDFluidFragmentTrailSettings has no float property EmberBurstHeightOffsetCm"));
			return false;
		}
		Property->SetPropertyValue_InContainer(&Rig.Emitter->TrailSettings, HeightOffsetCm);

		UTDFluidEmberComponent* Ember = Rig.FireActor->EmberComponent;
		Ember->SpawnPerSecond = 0.0f;
		Ember->SpawnPerDensityArea = 0.0f;
		Ember->BuoyancyCmPerSec2 = 0.0f;
		Ember->GravityCmPerSec2 = 0.0f;
		Ember->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, EmberCubeMeshPath));

		Rig.Emitter->TriggerBreakAt(FVector(40.0, 360.0, 70.0));
		for (OutSample.SettleFrameCount = 1; OutSample.SettleFrameCount <= MaxBurstSettleFrames; ++OutSample.SettleFrameCount)
		{
			TestWorld.TickFrames(1);
			CollectVisibleEmberHeights(*Ember, OutSample);
			if (OutSample.VisibleEmberCount >= MinVisibleBurstEmbers)
			{
				break;
			}
		}
		OutSample.BurstCenterWorld = Rig.Emitter->GetBurstCenterWorld();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBreakableDirectionOffsetDefaultTest,
	"TDGame.FluidFX.Fragment.BreakDirectionOffsetDefaultsToHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBreakableDirectionOffsetDefaultTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	const FStructProperty* Property = FindBreakDirectionOffsetProperty(*this);
	if (!Property)
	{
		return false;
	}

	TestTrue(TEXT("BreakDirectionOffsetCm is editable"), Property->HasAnyPropertyFlags(CPF_Edit));
	TestTrue(TEXT("BreakDirectionOffsetCm defaults to zero so the push starts at the hit"), *Property->ContainerPtrToValuePtr<FVector>(GetDefault<ATDBreakableActor>()) == FVector::ZeroVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBreakableDirectionOffsetFieldTest,
	"TDGame.FluidFX.Fragment.BreakDirectionOffsetMovesOnlyRadialVector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBreakableDirectionOffsetFieldTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	const FStructProperty* Property = FindBreakDirectionOffsetProperty(*this);
	if (!Property)
	{
		return false;
	}

	FTDFluidFragmentTestWorld TestWorld;
	ATDBreakableActor* Breakable = TestWorld.World->SpawnActor<ATDBreakableActor>(ATDBreakableActor::StaticClass(), FVector(0.0, 400.0, 50.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The breakable spawns"), Breakable))
	{
		return false;
	}

	const FVector DirectionOffsetCm(0.0, 0.0, -200.0);
	const FVector HitLocation(40.0, 360.0, 70.0);
	*Property->ContainerPtrToValuePtr<FVector>(Breakable) = DirectionOffsetCm;
	Breakable->StartBreak(HitLocation);
	TestWorld.TickFrames(2);

	TArray<const URadialVector*> RadialVectors;
	TArray<const URadialFalloff*> RadialFalloffs;
	ForEachObjectWithOuter(Breakable, [&RadialVectors, &RadialFalloffs](UObject* Object)
	{
		if (const URadialVector* RadialVector = Cast<URadialVector>(Object))
		{
			RadialVectors.Add(RadialVector);
		}
		if (const URadialFalloff* RadialFalloff = Cast<URadialFalloff>(Object))
		{
			RadialFalloffs.Add(RadialFalloff);
		}
	});

	if (!TestTrue(TEXT("The break applies a radial vector field"), RadialVectors.Num() > 0) || !TestTrue(TEXT("The break applies a radial falloff"), RadialFalloffs.Num() > 0))
	{
		return false;
	}

	for (const URadialVector* RadialVector : RadialVectors)
	{
		TestNearlyEqual(TEXT("The radial push starts at the hit plus the direction offset"), RadialVector->Position, HitLocation + DirectionOffsetCm, KeyCenterToleranceCm);
	}
	for (const URadialFalloff* RadialFalloff : RadialFalloffs)
	{
		TestNearlyEqual(TEXT("The falloff stays centered on the hit"), RadialFalloff->Position, HitLocation, KeyCenterToleranceCm);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentEmberBurstHeightOffsetTest,
	"TDGame.FluidFX.Fragment.EmberBurstHeightOffsetRaisesBurst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentEmberBurstHeightOffsetTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	FTDFluidEmberBurstHeightSample GroundSample;
	FTDFluidEmberBurstHeightSample RaisedSample;
	if (!MeasureEmberBurstHeight(*this, 0.0f, GroundSample) || !MeasureEmberBurstHeight(*this, EmberBurstRaisedOffsetCm, RaisedSample))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("Ember burst height: offset 0 mean %.2f cm over %d embers after %d frames, offset %.0f mean %.2f cm over %d embers after %d frames"),
		GroundSample.MeanHeightCm, GroundSample.VisibleEmberCount, GroundSample.SettleFrameCount, EmberBurstRaisedOffsetCm, RaisedSample.MeanHeightCm, RaisedSample.VisibleEmberCount, RaisedSample.SettleFrameCount));
	TestTrue(TEXT("The default burst shows enough embers"), GroundSample.VisibleEmberCount >= MinVisibleBurstEmbers);
	TestTrue(TEXT("The raised burst shows enough embers"), RaisedSample.VisibleEmberCount >= MinVisibleBurstEmbers);
	TestEqual(TEXT("Both bursts are measured after the same frame count"), RaisedSample.SettleFrameCount, GroundSample.SettleFrameCount);
	TestNearlyEqual(TEXT("The height offset raises the mean burst height"), RaisedSample.MeanHeightCm - GroundSample.MeanHeightCm, static_cast<double>(EmberBurstRaisedOffsetCm), EmberBurstHeightToleranceCm);
	TestNearlyEqual(TEXT("The height offset leaves the fluid burst center where it was"), RaisedSample.BurstCenterWorld, GroundSample.BurstCenterWorld, KeyCenterToleranceCm);
	return true;
}

namespace TDFluidFragmentTestPrivate
{
	constexpr float TrailScaleTestValue = 0.4f;
	constexpr float WorldClockSustainSeconds = 0.2f;
	constexpr float WorldClockFirstFrameSeconds = 0.117f;
	constexpr int32 WorldClockPollGapFrames = 3;
	constexpr int32 WorldClockMaxFrames = 30;
	constexpr int32 BinBudgetMovingLeafCount = 128;
	constexpr int32 BinBudgetRestingLeafCount = 128;
	constexpr double BinBudgetMovingRingRadiusCm = 150.0;
	constexpr double BinBudgetRestingRingRadiusCm = 220.0;
	constexpr double BinBudgetLeafStepCm = 20.0;
	constexpr int32 BurstFieldSettleFrames = 2;
	constexpr double BurstFieldTolerance = 1e-4;
	constexpr double BurstFieldControlRatio = 0.9;

	FFloatProperty* FindTrailFloatProperty(FAutomationTestBase& Test, const TCHAR* PropertyName)
	{
		FFloatProperty* Property = FindFProperty<FFloatProperty>(FTDFluidFragmentTrailSettings::StaticStruct(), PropertyName);
		Test.TestNotNull(FString::Printf(TEXT("FTDFluidFragmentTrailSettings has %s"), PropertyName), Property);
		return Property;
	}

	void BuildTrailScaleScenario(TArray<FVector>& OutPrevious, TArray<FVector>& OutCurrent, TArray<FVector>& OutLagged)
	{
		for (int32 LeafIndex = 0; LeafIndex < 6; ++LeafIndex)
		{
			const FVector Start(LeafIndex * 300.0, 0.0, 100.0);
			OutPrevious.Add(Start);
			OutCurrent.Add(Start + FVector(0.0, 20.0 + LeafIndex * 5.0, 0.0));
			OutLagged.Add(Start - FVector(0.0, 0.0, 40.0));
		}
	}

	bool AreStampArraysIdentical(const TArray<FTDFluidBrushStamp>& Expected, const TArray<FTDFluidBrushStamp>& Actual)
	{
		if (Expected.Num() != Actual.Num())
		{
			return false;
		}

		for (int32 StampIndex = 0; StampIndex < Expected.Num(); ++StampIndex)
		{
			const FTDFluidBrushStamp& Left = Expected[StampIndex];
			const FTDFluidBrushStamp& Right = Actual[StampIndex];
			const bool bIsSame = Left.WorldLocation == Right.WorldLocation
				&& Left.WorldVelocity == Right.WorldVelocity
				&& Left.RadiusCm == Right.RadiusCm
				&& Left.DensityStrength == Right.DensityStrength
				&& Left.VelocityStrength == Right.VelocityStrength
				&& Left.HeatStrength == Right.HeatStrength
				&& Left.Channel == Right.Channel;
			if (!bIsSame)
			{
				return false;
			}
		}
		return true;
	}

	void BuildBinBudgetLeaves(TArray<FVector>& OutPrevious, TArray<FVector>& OutCurrent)
	{
		for (int32 LeafIndex = 0; LeafIndex < BinBudgetMovingLeafCount; ++LeafIndex)
		{
			const double Angle = UE_DOUBLE_TWO_PI * static_cast<double>(LeafIndex) / static_cast<double>(BinBudgetMovingLeafCount);
			const FVector Outward(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
			OutPrevious.Add(Outward * BinBudgetMovingRingRadiusCm);
			OutCurrent.Add(Outward * (BinBudgetMovingRingRadiusCm + BinBudgetLeafStepCm));
		}

		for (int32 LeafIndex = 0; LeafIndex < BinBudgetRestingLeafCount; ++LeafIndex)
		{
			const double Angle = UE_DOUBLE_TWO_PI * static_cast<double>(LeafIndex) / static_cast<double>(BinBudgetRestingLeafCount);
			const FVector RestingPosition = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * BinBudgetRestingRingRadiusCm;
			OutPrevious.Add(RestingPosition);
			OutCurrent.Add(RestingPosition);
		}
	}

	void CountHeatWritingStamps(const FTDFluidGpuStampBinSet& BinSet, int32& OutKeptHeatStampCount, int32& OutHeatBinEntryCount)
	{
		const auto WritesHeat = [&BinSet](int32 StampIndex)
		{
			return BinSet.Stamps.IsValidIndex(StampIndex) && EnumHasAnyFlags(static_cast<ETDFluidGpuStampFlags>(BinSet.Stamps[StampIndex].Flags), ETDFluidGpuStampFlags::WritesHeat);
		};

		OutKeptHeatStampCount = 0;
		for (int32 StampIndex = 0; StampIndex < BinSet.Stamps.Num(); ++StampIndex)
		{
			OutKeptHeatStampCount += WritesHeat(StampIndex) ? 1 : 0;
		}

		OutHeatBinEntryCount = 0;
		for (const uint32 StampIndex : BinSet.BinStampIndices)
		{
			OutHeatBinEntryCount += WritesHeat(static_cast<int32>(StampIndex)) ? 1 : 0;
		}
	}

	struct FTDBurstFieldTotals
	{
		double Heat = 0.0;
		double Density = 0.0;
		bool bIsMeasured = false;
	};

	FTDBurstFieldTotals MeasureBurstOnlyFireField(const FFloatProperty& HeatScaleProperty, const FFloatProperty& DensityScaleProperty, float TrailScale, float FireStrengthRatio)
	{
		FTDBurstFieldTotals Totals;
		FTDFluidFragmentTestWorld TestWorld;
		FTDFluidFragmentRig Rig(TestWorld, 10.0f);
		if (!Rig.IsComplete())
		{
			return Totals;
		}

		UTDFluidPresetAsset* HeatedPreset = MakeTargetPreset(GetTransientPackage());
		HeatedPreset->SimulationConfig.bEnableHeatChannel = true;
		Rig.FireActor->InitializeFromPreset(HeatedPreset, 0.0f);
		const UTDFluidLiveComponent& FireComponent = *Rig.FireActor->LiveComponent;
		if (FireComponent.IsUsingGpuSolver())
		{
			return Totals;
		}

		FTDFluidFragmentTrailSettings& Settings = Rig.Emitter->TrailSettings;
		Settings.BurstSustainSeconds = 0.0f;
		Settings.RestingHeatSeconds = 0.0f;
		Settings.FireHeatStrength *= FireStrengthRatio;
		Settings.FireDensityStrength *= FireStrengthRatio;
		HeatScaleProperty.SetPropertyValue_InContainer(&Settings, TrailScale);
		DensityScaleProperty.SetPropertyValue_InContainer(&Settings, TrailScale);
		FTDFluidFragmentTarget BurstOnlyTarget;
		BurstOnlyTarget.TargetActor = Rig.FireActor;
		BurstOnlyTarget.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		BurstOnlyTarget.HeatScale = 1.0f;
		BurstOnlyTarget.bReceivesTrails = false;
		Rig.Emitter->Targets = { BurstOnlyTarget };

		TestWorld.TickFrames(1);
		Rig.Emitter->TriggerBreakAt(Rig.FireActor->GetActorLocation());
		TestWorld.TickFrames(BurstFieldSettleFrames);
		const int32 Resolution = FireComponent.GetSimulationResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = FireComponent.SampleSimulationCell(CellX, CellY);
				Totals.Heat += Sample.Heat;
				Totals.Density += Sample.PrimaryDensity + Sample.SelectiveDensity;
			}
		}
		Totals.bIsMeasured = Resolution > 0;
		return Totals;
	}

	double MeasureSustainEndAgeSeconds(FAutomationTestBase& Test, const FString& Label, const FBoolProperty* WorldClockProperty)
	{
		FTDFluidFragmentTestWorld TestWorld;
		FTDFluidFragmentRig Rig(TestWorld, 10.0f);
		if (!Test.TestTrue(*(Label + TEXT(": the rig spawns")), Rig.IsComplete()))
		{
			return -1.0;
		}

		if (WorldClockProperty)
		{
			WorldClockProperty->SetPropertyValue_InContainer(&Rig.Emitter->TrailSettings, true);
		}
		Rig.Emitter->TrailSettings.BurstSustainSeconds = WorldClockSustainSeconds;
		FTDFluidFragmentTarget BurstOnlyTarget;
		BurstOnlyTarget.TargetActor = Rig.FireActor;
		BurstOnlyTarget.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		BurstOnlyTarget.HeatScale = 1.0f;
		BurstOnlyTarget.bReceivesTrails = false;
		Rig.Emitter->Targets = { BurstOnlyTarget };

		double LastPollSeconds = Rig.Emitter->PrimaryActorTick.GetLastIntervalTickGameTime();
		bool bSawFreshPoll = false;
		for (int32 FrameIndex = 0; FrameIndex < WorldClockMaxFrames && !bSawFreshPoll; ++FrameIndex)
		{
			TestWorld.TickFrames(1);
			const double PollSeconds = Rig.Emitter->PrimaryActorTick.GetLastIntervalTickGameTime();
			bSawFreshPoll = PollSeconds >= 0.0 && PollSeconds != LastPollSeconds;
			LastPollSeconds = PollSeconds;
		}
		if (!Test.TestTrue(*(Label + TEXT(": the armed emitter polls on its interval")), bSawFreshPoll))
		{
			return -1.0;
		}

		TestWorld.TickFrames(WorldClockPollGapFrames);
		const double BreakWorldSeconds = TestWorld.World->GetTimeSeconds();
		const double ClockLagSeconds = BreakWorldSeconds - Rig.Emitter->PrimaryActorTick.GetLastIntervalTickGameTime();
		Test.AddInfo(FString::Printf(TEXT("%s sustain: the break lags the last armed poll by %.4f s"), *Label, ClockLagSeconds));
		if (!Test.TestTrue(*(Label + TEXT(": the break lags the last armed poll by at least two frames")), ClockLagSeconds >= 2.0 * FixedStepSeconds - 1e-4))
		{
			return -1.0;
		}

		Rig.Emitter->TriggerBreakAt(FVector(0.0, 400.0, 50.0));
		if (!Test.TestTrue(*(Label + TEXT(": the break starts tracking")), Rig.Emitter->IsTrackingFragments()))
		{
			return -1.0;
		}

		int32 PreviousStampCount = Rig.Emitter->GetTotalStampCount();
		TestWorld.TickSeconds(WorldClockFirstFrameSeconds);
		double SustainEndAgeSeconds = -1.0;
		for (int32 FrameIndex = 0; FrameIndex < WorldClockMaxFrames; ++FrameIndex)
		{
			const int32 StampCount = Rig.Emitter->GetTotalStampCount();
			if (StampCount == PreviousStampCount)
			{
				SustainEndAgeSeconds = TestWorld.World->GetTimeSeconds() - BreakWorldSeconds;
				break;
			}

			PreviousStampCount = StampCount;
			TestWorld.TickFrames(1);
		}

		Test.AddInfo(FString::Printf(TEXT("%s sustain: the first frame without burst stamps is %.4f s after the break"), *Label, SustainEndAgeSeconds));
		return SustainEndAgeSeconds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentFireTrailScaleTest,
	"TDGame.FluidFX.Fragment.FireTrailScalesTouchOnlyTrailStamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentFireTrailScaleTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	FFloatProperty* HeatScaleProperty = FindTrailFloatProperty(*this, TEXT("FireTrailHeatScale"));
	FFloatProperty* DensityScaleProperty = FindTrailFloatProperty(*this, TEXT("FireTrailDensityScale"));
	if (!HeatScaleProperty || !DensityScaleProperty)
	{
		return false;
	}

	FTDFluidFragmentTrailSettings DefaultSettings;
	DefaultSettings.SmokeHeatStrength = 0.5f;
	TestTrue(TEXT("FireTrailHeatScale defaults to 1"), HeatScaleProperty->GetPropertyValue_InContainer(&DefaultSettings) == 1.0f);
	TestTrue(TEXT("FireTrailDensityScale defaults to 1"), DensityScaleProperty->GetPropertyValue_InContainer(&DefaultSettings) == 1.0f);

	FTDFluidFragmentTarget FireTarget;
	FireTarget.Channel = ETDFluidBrushChannel::ChannelB_Fire;
	FireTarget.HeatScale = 1.0f;
	FTDFluidFragmentTarget SmokeTarget;
	SmokeTarget.Channel = ETDFluidBrushChannel::ChannelR_Mud;
	SmokeTarget.HeatScale = 1.0f;
	SmokeTarget.bUsesLaggedPositions = true;

	TArray<FVector> Previous;
	TArray<FVector> Current;
	TArray<FVector> Lagged;
	BuildTrailScaleScenario(Previous, Current, Lagged);

	TArray<FTDFluidBrushStamp> DefaultFireStamps;
	TArray<FTDFluidBrushStamp> DefaultSmokeStamps;
	ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(FireTarget, Previous, Current, Lagged, FixedStepSeconds, 0.0f, DefaultSettings, DefaultFireStamps);
	ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(SmokeTarget, Previous, Current, Lagged, FixedStepSeconds, 0.0f, DefaultSettings, DefaultSmokeStamps);
	if (!TestTrue(TEXT("The scenario produces fire and smoke trail stamps"), DefaultFireStamps.Num() > 0 && DefaultSmokeStamps.Num() > 0))
	{
		return false;
	}

	bool bDefaultKeepsHeadFormula = true;
	for (const FTDFluidBrushStamp& Stamp : DefaultFireStamps)
	{
		bDefaultKeepsHeadFormula &= Stamp.DensityStrength == DefaultSettings.FireDensityStrength * FireTarget.DensityScale;
		bDefaultKeepsHeadFormula &= Stamp.HeatStrength == FMath::Clamp(DefaultSettings.FireHeatStrength * FireTarget.HeatScale, 0.0f, 1.0f);
	}
	TestTrue(TEXT("Scale 1 keeps every fire trail stamp on the pre-scale formula bit for bit"), bDefaultKeepsHeadFormula);

	FTDFluidFragmentTrailSettings ExplicitUnitSettings = DefaultSettings;
	HeatScaleProperty->SetPropertyValue_InContainer(&ExplicitUnitSettings, 1.0f);
	DensityScaleProperty->SetPropertyValue_InContainer(&ExplicitUnitSettings, 1.0f);
	TArray<FTDFluidBrushStamp> UnitFireStamps;
	ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(FireTarget, Previous, Current, Lagged, FixedStepSeconds, 0.0f, ExplicitUnitSettings, UnitFireStamps);
	TestTrue(TEXT("Scale 1 gives the same fire trail stamp array"), AreStampArraysIdentical(DefaultFireStamps, UnitFireStamps));

	FTDFluidFragmentTrailSettings ScaledSettings = DefaultSettings;
	HeatScaleProperty->SetPropertyValue_InContainer(&ScaledSettings, TrailScaleTestValue);
	DensityScaleProperty->SetPropertyValue_InContainer(&ScaledSettings, TrailScaleTestValue);
	TArray<FTDFluidBrushStamp> ScaledFireStamps;
	TArray<FTDFluidBrushStamp> ScaledSmokeStamps;
	ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(FireTarget, Previous, Current, Lagged, FixedStepSeconds, 0.0f, ScaledSettings, ScaledFireStamps);
	ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(SmokeTarget, Previous, Current, Lagged, FixedStepSeconds, 0.0f, ScaledSettings, ScaledSmokeStamps);

	if (TestEqual(TEXT("Scaling keeps the fire trail stamp count"), ScaledFireStamps.Num(), DefaultFireStamps.Num()))
	{
		for (int32 StampIndex = 0; StampIndex < ScaledFireStamps.Num(); ++StampIndex)
		{
			TestTrue(TEXT("Scaling keeps fire trail stamp positions"), ScaledFireStamps[StampIndex].WorldLocation == DefaultFireStamps[StampIndex].WorldLocation);
			TestNearlyEqual(TEXT("Fire trail heat is scaled by 0.4"), ScaledFireStamps[StampIndex].HeatStrength, DefaultFireStamps[StampIndex].HeatStrength * TrailScaleTestValue, 1e-6f);
			TestNearlyEqual(TEXT("Fire trail density is scaled by 0.4"), ScaledFireStamps[StampIndex].DensityStrength, DefaultFireStamps[StampIndex].DensityStrength * TrailScaleTestValue, 1e-6f);
		}
	}
	TestTrue(TEXT("Smoke trail stamps ignore the fire trail scales"), AreStampArraysIdentical(DefaultSmokeStamps, ScaledSmokeStamps));

	const FTDBurstFieldTotals UnitBurst = MeasureBurstOnlyFireField(*HeatScaleProperty, *DensityScaleProperty, 1.0f, 1.0f);
	const FTDBurstFieldTotals TrailScaledBurst = MeasureBurstOnlyFireField(*HeatScaleProperty, *DensityScaleProperty, TrailScaleTestValue, 1.0f);
	const FTDBurstFieldTotals WeakerFireBurst = MeasureBurstOnlyFireField(*HeatScaleProperty, *DensityScaleProperty, 1.0f, TrailScaleTestValue);
	AddInfo(FString::Printf(TEXT("Burst-only fire field after %d frames (trail scales 1 / trail scales 0.4 / fire strengths 0.4): heat %.4f / %.4f / %.4f, density %.4f / %.4f / %.4f"),
		BurstFieldSettleFrames, UnitBurst.Heat, TrailScaledBurst.Heat, WeakerFireBurst.Heat, UnitBurst.Density, TrailScaledBurst.Density, WeakerFireBurst.Density));
	if (!TestTrue(TEXT("The burst-only fire field is measured on the CPU solver in all three runs"), UnitBurst.bIsMeasured && TrailScaledBurst.bIsMeasured && WeakerFireBurst.bIsMeasured))
	{
		return false;
	}

	TestTrue(TEXT("The queued burst leaves heat and density in the fire field"), UnitBurst.Heat > 0.0 && UnitBurst.Density > 0.0);
	TestTrue(TEXT("Weaker fire strengths lower the burst heat, so the field sees the queued burst strength"), WeakerFireBurst.Heat < UnitBurst.Heat * BurstFieldControlRatio);
	TestTrue(TEXT("Weaker fire strengths lower the burst density, so the field sees the queued burst strength"), WeakerFireBurst.Density < UnitBurst.Density * BurstFieldControlRatio);
	TestNearlyEqual(TEXT("Fire trail scales leave the queued burst heat unchanged"), TrailScaledBurst.Heat, UnitBurst.Heat, UnitBurst.Heat * BurstFieldTolerance);
	TestNearlyEqual(TEXT("Fire trail scales leave the queued burst density unchanged"), TrailScaledBurst.Density, UnitBurst.Density, UnitBurst.Density * BurstFieldTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentWorldClockSustainTest,
	"TDGame.FluidFX.Fragment.WorldClockEndsSustainAtBreakAge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentWorldClockSustainTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	const double DefaultSustainEndAgeSeconds = MeasureSustainEndAgeSeconds(*this, TEXT("Default accumulator"), nullptr);
	FBoolProperty* WorldClockProperty = FindFProperty<FBoolProperty>(FTDFluidFragmentTrailSettings::StaticStruct(), TEXT("bShouldTrackWithWorldClock"));
	if (!TestNotNull(TEXT("FTDFluidFragmentTrailSettings has bShouldTrackWithWorldClock"), WorldClockProperty))
	{
		return false;
	}

	const FTDFluidFragmentTrailSettings DefaultSettings;
	TestFalse(TEXT("bShouldTrackWithWorldClock defaults to false"), WorldClockProperty->GetPropertyValue_InContainer(&DefaultSettings));

	const double WorldClockSustainEndAgeSeconds = MeasureSustainEndAgeSeconds(*this, TEXT("World clock"), WorldClockProperty);
	if (!TestTrue(TEXT("Both sustained bursts end within the measured frames"), DefaultSustainEndAgeSeconds >= 0.0 && WorldClockSustainEndAgeSeconds >= 0.0))
	{
		return false;
	}

	const double OneFrameToleranceSeconds = static_cast<double>(FixedStepSeconds) + 1e-3;
	TestNearlyEqual(TEXT("With the world clock the sustained burst ends 0.2 s after the break within one frame"), WorldClockSustainEndAgeSeconds, static_cast<double>(WorldClockSustainSeconds), OneFrameToleranceSeconds);
	TestTrue(TEXT("The default accumulator misses 0.2 s by more than one frame here, so the scenario tells the two clocks apart"), FMath::Abs(DefaultSustainEndAgeSeconds - static_cast<double>(WorldClockSustainSeconds)) > OneFrameToleranceSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentExplosionStampBinBudgetTest,
	"TDGame.FluidFX.Fragment.ExplosionStampBinBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentExplosionStampBinBudgetTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentTestPrivate;

	const FTDFluidFragmentTrailSettings Trail = UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion);
	const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Explosion);

	TArray<FTDFluidBrushStamp> BurstStamps;
	ATDFluidFragmentEmitterActor::BuildBurstStamps(FVector::ZeroVector, FTDFluidPlaneBasis::MakeWorldFacing(), Trail, ETDFluidBrushChannel::ChannelB_Fire, 1.0f, Trail.FireDensityStrength, Trail.FireHeatStrength, BurstStamps);

	TArray<FVector> Previous;
	TArray<FVector> Current;
	BuildBinBudgetLeaves(Previous, Current);
	TArray<FTDFluidFragmentTarget> LegacyTargets;
	ATDFluidFragmentEmitterActor::BuildLegacyTargets(nullptr, nullptr, LegacyTargets);
	TArray<FTDFluidBrushStamp> TrailStamps;
	ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(LegacyTargets[0], Previous, Current, Previous, FixedStepSeconds, 0.0f, Trail, TrailStamps);
	TArray<FTDFluidBrushStamp> RestingStamps;
	ATDFluidFragmentEmitterActor::BuildRestingHeatStamps(LegacyTargets[0], Previous, Current, FixedStepSeconds, 0.0f, Trail, RestingStamps);

	TArray<FTDFluidGpuStampInput> StampInputs;
	const auto AppendStampInputs = [&StampInputs](const TArray<FTDFluidBrushStamp>& Stamps)
	{
		for (const FTDFluidBrushStamp& Stamp : Stamps)
		{
			StampInputs.Add(FTDFluidGpuSolverProxy::MakeStampInput(Stamp));
		}
	};
	AppendStampInputs(BurstStamps);
	AppendStampInputs(TrailStamps);
	AppendStampInputs(RestingStamps);

	FTDFluidGpuStampBinWindow Window;
	Window.CenterPlaneCm = FVector2D::ZeroVector;
	Window.ExtentCm = Config.SimExtentCm;
	Window.Resolution = Config.GridResolution;
	const FTDFluidGpuStampBinSet BinSet = FTDFluidGpuStampBins::Build(Window, StampInputs, FTDFluidGpuStampBinLimits());
	uint32 FullestBinCount = 0;
	for (const FTDFluidGpuStampBinRange& Range : BinSet.BinRanges)
	{
		FullestBinCount = FMath::Max(FullestBinCount, Range.Count);
	}

	int32 KeptHeatStampCount = 0;
	int32 HeatBinEntryCount = 0;
	CountHeatWritingStamps(BinSet, KeptHeatStampCount, HeatBinEntryCount);
	AddInfo(FString::Printf(TEXT("Explosion stamp bin budget (fire target, one actor, one tick): leaves %d moving + %d resting, burst %d, trail %d, resting %d, kept %d, non-finite %d, outside window %d, over tick limit %d, bin overflow entries %d, fullest bin %u of %d, kept heat stamps %d, heat bin entries %d"),
		BinBudgetMovingLeafCount, BinBudgetRestingLeafCount, BurstStamps.Num(), TrailStamps.Num(), RestingStamps.Num(), BinSet.Stamps.Num(), BinSet.NonFiniteStampCount, BinSet.OutsideWindowStampCount, BinSet.OverTickLimitStampCount, BinSet.BinOverflowEntryCount, FullestBinCount, TDFluidGpuSolverConstants::MaximumStampsPerBin, KeptHeatStampCount, HeatBinEntryCount));
	TestEqual(TEXT("The bin table has one range per bin"), BinSet.BinRanges.Num(), TDFluidGpuSolverConstants::StampBinCount);
	return true;
}

#endif
