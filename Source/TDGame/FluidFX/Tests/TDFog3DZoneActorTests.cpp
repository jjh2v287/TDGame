#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/World.h"
#include "FluidFX/TDFog3DZoneActor.h"
#include "FluidFX/Tests/TDFog3DLayerTestAccess.h"
#include "GameFramework/Pawn.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

struct FTDFog3DZoneActorTestAccess
{
	static bool IsRepresentative(const ATDFog3DZoneActor& Actor)
	{
		return Actor.bIsRepresentative;
	}

	static const UTextureRenderTargetVolume* GetStateTarget(const ATDFog3DZoneActor& Actor)
	{
		return Actor.StateTarget;
	}

	static int32 GetLayerResolution(const ATDFog3DZoneActor& Actor)
	{
		return Actor.Layer.GetResolution();
	}

	static bool HasValidSettings(const ATDFog3DZoneActor& Actor)
	{
		return Actor.bHasValidSettings;
	}

	static FTDFog3DLayer& GetLayer(ATDFog3DZoneActor& Actor)
	{
		return Actor.Layer;
	}

	static FTDFog3DMaterialParameters& GetDesiredMaterialParameters(ATDFog3DZoneActor& Actor)
	{
		return Actor.DesiredMaterialParameters;
	}

	static void TrackResetCauses(ATDFog3DZoneActor& Actor, const FTDFog3DZoneTickGates& Gates)
	{
		Actor.TrackResetCauses(Gates, ATDFog3DZoneActor::DecideTickOutcome(Gates));
	}

	static void ForgetInputHistoryWhenPawnChanges(ATDFog3DZoneActor& Actor, APawn* Pawn)
	{
		Actor.ForgetInputHistoryWhenPawnChanges(Pawn);
	}

	static void AttachMaterialInstance(ATDFog3DZoneActor& Actor, UMaterialInstanceDynamic* MaterialInstance)
	{
		Actor.VolumeMID = MaterialInstance;
	}

	static void WriteMaterialParameters(ATDFog3DZoneActor& Actor)
	{
		Actor.WriteMaterialParameters();
	}

	static void SetActiveParameter(ATDFog3DZoneActor& Actor, bool bIsActive)
	{
		Actor.SetActiveParameter(bIsActive);
	}
};

namespace TDFog3DZoneActorTestPrivate
{
	struct FTDFog3DZoneTestWorldScope
	{
		FTDFog3DZoneTestWorldScope()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFog3DZoneActorTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFog3DZoneTestWorldScope()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UWorld* World = nullptr;
	};

	FTDFog3DZoneTickGates MakeSimulatingGates()
	{
		FTDFog3DZoneTickGates Gates;
		Gates.bIsRepresentative = true;
		Gates.bHasValidSettings = true;
		Gates.bIsEnabled = true;
		Gates.bIsMaterialReady = true;
		Gates.bHasPawn = true;
		Gates.bIsPawnInsideZone = true;
		Gates.ConsoleVolumetricFog = 1;
		Gates.bHasHeightFog = true;
		Gates.bHeightFogEnablesVolumetricFog = true;
		Gates.bHeightFogRenders = true;
		Gates.Mode = 2;
		return Gates;
	}

	bool IsNear(const FVector4f& Actual, const FVector4f& Expected, float Tolerance)
	{
		return FMath::Abs(Actual.X - Expected.X) <= Tolerance
			&& FMath::Abs(Actual.Y - Expected.Y) <= Tolerance
			&& FMath::Abs(Actual.Z - Expected.Z) <= Tolerance
			&& FMath::Abs(Actual.W - Expected.W) <= Tolerance;
	}

	FString DescribeVector4(const FVector4f& Value)
	{
		return FString::Printf(TEXT("(%.4f, %.4f, %.4f, %.4f)"), Value.X, Value.Y, Value.Z, Value.W);
	}

	FTDFog3DInputPose MakeFullPose(const FTransform& SocketWorld)
	{
		FTDFog3DInputPose Pose;
		Pose.BladeSocketWorld = SocketWorld;
		Pose.HandWorld = FVector(10.0, 20.0, 110.0);
		Pose.LeftFootWorld = FVector(-15.0, 10.0, 5.0);
		Pose.RightFootWorld = FVector(15.0, -10.0, 5.0);
		Pose.PelvisWorld = FVector(0.0, 0.0, 95.0);
		return Pose;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DZoneInsideTest,
	"TDGame.FluidFX.Fog3D.ZoneInside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DZoneInsideTest::RunTest(const FString& Parameters)
{
	const FVector HalfExtentCm(3000.0, 3000.0, 150.0);
	const double MarginCm = 200.0;
	for (const FVector& Center : { FVector(0.0, 0.0, 150.0), FVector(1.0e7, -1.0e7, 150.0) })
	{
		const FString Where = FString::Printf(TEXT("center %s"), *Center.ToString());
		TestTrue(*(TEXT("Center is inside at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center, Center, HalfExtentCm, MarginCm));
		TestTrue(*(TEXT("Side +X face is inside at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(3000.0, 0.0, 0.0), Center, HalfExtentCm, MarginCm));
		TestFalse(*(TEXT("Just past +X face is outside at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(3000.01, 0.0, 0.0), Center, HalfExtentCm, MarginCm));
		TestTrue(*(TEXT("Side -Y face is inside at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(0.0, -3000.0, 0.0), Center, HalfExtentCm, MarginCm));
		TestFalse(*(TEXT("Just past -Y face is outside at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(0.0, -3000.01, 0.0), Center, HalfExtentCm, MarginCm));
		TestTrue(*(TEXT("Top plus margin is inside at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(0.0, 0.0, 350.0), Center, HalfExtentCm, MarginCm));
		TestFalse(*(TEXT("Above top plus margin is outside at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(0.0, 0.0, 350.01), Center, HalfExtentCm, MarginCm));
		TestTrue(*(TEXT("Floor minus margin is inside at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(2999.0, -2999.0, -350.0), Center, HalfExtentCm, MarginCm));
		TestFalse(*(TEXT("Below floor minus margin is outside at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(0.0, 0.0, -350.01), Center, HalfExtentCm, MarginCm));
		TestFalse(*(TEXT("Corner outside on both axes at ") + Where), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(3001.0, 3001.0, 0.0), Center, HalfExtentCm, MarginCm));
	}

	const FVector Center(0.0, 0.0, 150.0);
	TestTrue(TEXT("Negative margin counts as zero at the top face"), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(0.0, 0.0, 150.0), Center, HalfExtentCm, -50.0));
	TestFalse(TEXT("Negative margin does not shrink below the box"), ATDFog3DZoneActor::IsPointInsideZone(Center + FVector(0.0, 0.0, 150.01), Center, HalfExtentCm, -50.0));
	TestFalse(TEXT("A point with NaN is outside"), ATDFog3DZoneActor::IsPointInsideZone(FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 150.0), Center, HalfExtentCm, MarginCm));
	TestTrue(TEXT("Player capsule height at the stage start point is inside"), ATDFog3DZoneActor::IsPointInsideZone(FVector(-1000.0, 0.0, 100.0), Center, HalfExtentCm, MarginCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DZoneBladeSamplesFromSocketTest,
	"TDGame.FluidFX.Fog3D.BladeSamplesFromSocket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DZoneBladeSamplesFromSocketTest::RunTest(const FString& Parameters)
{
	const FTDFog3DSettings Settings;
	const FTransform SocketWorld(FRotator(10.0, 35.0, -20.0), FVector(1234.5, -678.9, 90.0), FVector(1.0));
	const FVector BaseWorld = SocketWorld.TransformPosition(Settings.BladeBaseLocalCm);
	const FVector TipWorld = SocketWorld.TransformPosition(Settings.BladeTipLocalCm);

	TArray<FTDFog3DInputSample> Samples;
	FTDFog3DInputSample& Existing = Samples.AddDefaulted_GetRef();
	Existing.SlotId = 77;
	ATDFog3DZoneActor::AppendBladeSamples(SocketWorld, Settings.BladeBaseLocalCm, Settings.BladeTipLocalCm, Settings.BladeRadiusCm, Settings.BladeWindStrength, Settings.BladeWindGain, Settings.BladeErosionPerSec, Samples);

	if (!TestEqual(TEXT("Blade of about 96 cm with radius 35 appends four spheres after the existing sample"), Samples.Num(), 5))
	{
		return false;
	}
	TestEqual(TEXT("Existing sample is kept in front"), Samples[0].SlotId, 77);
	TestTrue(TEXT("First blade sphere is the tip"), Samples[1].CurrentWorld.Equals(TipWorld, 1.0e-6));
	TestTrue(TEXT("Last blade sphere is the base"), Samples[4].CurrentWorld.Equals(BaseWorld, 1.0e-6));
	for (int32 SphereIndex = 0; SphereIndex < 4; ++SphereIndex)
	{
		const FTDFog3DInputSample& Sample = Samples[SphereIndex + 1];
		TestEqual(FString::Printf(TEXT("Blade sphere %d slot"), SphereIndex), Sample.SlotId, SphereIndex);
		TestEqual(FString::Printf(TEXT("Blade sphere %d radius"), SphereIndex), Sample.RadiusCm, Settings.BladeRadiusCm);
		TestEqual(FString::Printf(TEXT("Blade sphere %d wind strength"), SphereIndex), Sample.WindStrength, Settings.BladeWindStrength);
		TestEqual(FString::Printf(TEXT("Blade sphere %d erosion"), SphereIndex), Sample.ErosionPerSec, Settings.BladeErosionPerSec);
		if (SphereIndex == 0)
		{
			continue;
		}
		const double Spacing = FVector::Dist(Sample.CurrentWorld, Samples[SphereIndex].CurrentWorld);
		TestTrue(FString::Printf(TEXT("Blade sphere %d spacing %.3f is at most the radius"), SphereIndex, Spacing), Spacing <= Settings.BladeRadiusCm + 1.0e-6);
		const double TipDistance = FVector::Dist(Sample.CurrentWorld, TipWorld);
		const double PreviousTipDistance = FVector::Dist(Samples[SphereIndex].CurrentWorld, TipWorld);
		TestTrue(FString::Printf(TEXT("Blade sphere %d walks from tip toward base"), SphereIndex), TipDistance > PreviousTipDistance);
	}

	TArray<FTDFog3DInputSample> IdentitySamples;
	ATDFog3DZoneActor::AppendBladeSamples(FTransform::Identity, Settings.BladeBaseLocalCm, Settings.BladeTipLocalCm, Settings.BladeRadiusCm, 1.0f, 1.0f, 30.0f, IdentitySamples);
	if (TestEqual(TEXT("Identity socket also gives four spheres"), IdentitySamples.Num(), 4))
	{
		TestTrue(TEXT("Identity socket tip equals the local tip offset"), IdentitySamples[0].CurrentWorld.Equals(Settings.BladeTipLocalCm, 1.0e-9));
		TestTrue(TEXT("Identity socket base equals the local base offset"), IdentitySamples[3].CurrentWorld.Equals(Settings.BladeBaseLocalCm, 1.0e-9));
	}

	TArray<FTDFog3DInputSample> WideSamples;
	ATDFog3DZoneActor::AppendBladeSamples(SocketWorld, Settings.BladeBaseLocalCm, Settings.BladeTipLocalCm, 50.0f, 1.0f, 1.0f, 30.0f, WideSamples);
	TestEqual(TEXT("Radius 50 gives ceil(96.1 / 50) + 1 = 3 spheres"), WideSamples.Num(), 3);

	const FTransform ScaledSocket(FRotator::ZeroRotator, FVector::ZeroVector, FVector(2.0));
	TArray<FTDFog3DInputSample> ScaledSamples;
	ATDFog3DZoneActor::AppendBladeSamples(ScaledSocket, Settings.BladeBaseLocalCm, Settings.BladeTipLocalCm, Settings.BladeRadiusCm, 1.0f, 1.0f, 30.0f, ScaledSamples);
	TestEqual(TEXT("A socket scaled by two doubles the blade and gives ceil(192.3 / 35) + 1 = 7 spheres"), ScaledSamples.Num(), 7);

	TArray<FTDFog3DInputSample> ZeroLengthSamples;
	ATDFog3DZoneActor::AppendBladeSamples(SocketWorld, Settings.BladeBaseLocalCm, Settings.BladeBaseLocalCm, Settings.BladeRadiusCm, 1.0f, 1.0f, 30.0f, ZeroLengthSamples);
	TestEqual(TEXT("A zero length blade gives no spheres"), ZeroLengthSamples.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DZoneBodySamplesOrderTest,
	"TDGame.FluidFX.Fog3D.BodySamplesOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DZoneBodySamplesOrderTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DZoneActorTestPrivate;

	FTDFog3DSettings Settings;
	Settings.BodyWindStrength = 0.75f;
	Settings.BodyErosionPerSec = 2.0f;
	const FTransform SocketWorld(FRotator(0.0, 90.0, 0.0), FVector(30.0, 0.0, 100.0), FVector(1.0));
	const FTDFog3DInputPose FullPose = MakeFullPose(SocketWorld);

	TArray<FTDFog3DInputSample> Samples;
	Samples.AddDefaulted(3);
	ATDFog3DZoneActor::BuildInputSamples(FullPose, Settings, Samples);
	const TArray<int32> ExpectedSlots = { 0, 1, 2, 3, ATDFog3DZoneActor::HandSlotId, ATDFog3DZoneActor::LeftFootSlotId, ATDFog3DZoneActor::RightFootSlotId, ATDFog3DZoneActor::PelvisSlotId };
	if (!TestEqual(TEXT("Full pose gives four blade and four body samples and drops older content"), Samples.Num(), ExpectedSlots.Num()))
	{
		return false;
	}
	for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
	{
		TestEqual(FString::Printf(TEXT("Sample %d slot follows blade tip to base, hand, left foot, right foot, pelvis"), SampleIndex), Samples[SampleIndex].SlotId, ExpectedSlots[SampleIndex]);
	}
	TestTrue(TEXT("First sample is the blade tip"), Samples[0].CurrentWorld.Equals(SocketWorld.TransformPosition(Settings.BladeTipLocalCm), 1.0e-6));
	TestEqual(TEXT("Blade erosion"), Samples[0].ErosionPerSec, Settings.BladeErosionPerSec);
	TestTrue(TEXT("Hand position"), Samples[4].CurrentWorld.Equals(FullPose.HandWorld.GetValue()));
	TestEqual(TEXT("Hand radius"), Samples[4].RadiusCm, Settings.HandRadiusCm);
	TestTrue(TEXT("Left foot position"), Samples[5].CurrentWorld.Equals(FullPose.LeftFootWorld.GetValue()));
	TestEqual(TEXT("Left foot radius"), Samples[5].RadiusCm, Settings.FootRadiusCm);
	TestTrue(TEXT("Right foot position"), Samples[6].CurrentWorld.Equals(FullPose.RightFootWorld.GetValue()));
	TestEqual(TEXT("Right foot radius"), Samples[6].RadiusCm, Settings.FootRadiusCm);
	TestTrue(TEXT("Pelvis position"), Samples[7].CurrentWorld.Equals(FullPose.PelvisWorld.GetValue()));
	TestEqual(TEXT("Pelvis radius"), Samples[7].RadiusCm, Settings.PelvisRadiusCm);
	for (int32 SampleIndex = 4; SampleIndex < Samples.Num(); ++SampleIndex)
	{
		TestEqual(FString::Printf(TEXT("Body sample %d wind strength"), SampleIndex), Samples[SampleIndex].WindStrength, Settings.BodyWindStrength);
		TestEqual(FString::Printf(TEXT("Body sample %d erosion"), SampleIndex), Samples[SampleIndex].ErosionPerSec, Settings.BodyErosionPerSec);
	}

	FTDFog3DInputPose MissingFootPose = FullPose;
	MissingFootPose.LeftFootWorld.Reset();
	ATDFog3DZoneActor::BuildInputSamples(MissingFootPose, Settings, Samples);
	TestEqual(TEXT("A missing left foot removes only that sample"), Samples.Num(), 7);
	TestFalse(TEXT("No left foot slot remains"), Samples.ContainsByPredicate([](const FTDFog3DInputSample& Sample) { return Sample.SlotId == ATDFog3DZoneActor::LeftFootSlotId; }));
	if (Samples.Num() == 7)
	{
		TestEqual(TEXT("Right foot follows the hand when the left foot is missing"), Samples[5].SlotId, ATDFog3DZoneActor::RightFootSlotId);
	}

	FTDFog3DInputPose BodyOnlyPose = FullPose;
	BodyOnlyPose.BladeSocketWorld.Reset();
	ATDFog3DZoneActor::BuildInputSamples(BodyOnlyPose, Settings, Samples);
	if (TestEqual(TEXT("Without the blade socket only four body samples remain"), Samples.Num(), 4))
	{
		TestEqual(TEXT("Hand comes first without the blade"), Samples[0].SlotId, ATDFog3DZoneActor::HandSlotId);
	}

	ATDFog3DZoneActor::BuildInputSamples(FTDFog3DInputPose(), Settings, Samples);
	TestEqual(TEXT("An empty pose gives no samples"), Samples.Num(), 0);

	ATDFog3DZoneActor::BuildInputSamples(FullPose, Settings, Samples);
	TArray<FTDFluidGpuFog3DSphere> Spheres;
	int32 DroppedCount = 0;
	FTDFog3DLayer::PackSpheres(Samples, TMap<int32, FVector>(), FVector2D::ZeroVector, 0.0, true, 1.0f / 60.0f, Settings.bUsesClampedTimeWindBlend, Settings.MaximumInputSpeedCmPerSec, Settings.MaximumInputSphereCount, Spheres, DroppedCount);
	TestEqual(TEXT("Default full pose fits under the sphere limit"), Spheres.Num(), 8);
	TestEqual(TEXT("Default full pose drops nothing"), DroppedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DZoneSampleWindGainsTest,
	"TDGame.FluidFX.Fog3D.SampleWindGains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DZoneSampleWindGainsTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DZoneActorTestPrivate;

	FTDFog3DSettings Settings;
	Settings.BodyWindGain = 1.25f;
	Settings.BladeWindGain = 1.5f;
	const FTransform SocketWorld(FRotator(0.0, 90.0, 0.0), FVector(30.0, 0.0, 100.0), FVector(1.0));
	TArray<FTDFog3DInputSample> Samples;
	ATDFog3DZoneActor::BuildInputSamples(MakeFullPose(SocketWorld), Settings, Samples);
	if (!TestEqual(TEXT("Full pose gives four blade and four body samples"), Samples.Num(), 8))
	{
		return false;
	}
	for (int32 SampleIndex = 0; SampleIndex < 4; ++SampleIndex)
	{
		TestEqual(FString::Printf(TEXT("Blade sample %d wind gain comes from BladeWindGain"), SampleIndex), Samples[SampleIndex].WindGain, Settings.BladeWindGain);
	}
	for (int32 SampleIndex = 4; SampleIndex < Samples.Num(); ++SampleIndex)
	{
		TestEqual(FString::Printf(TEXT("Body sample %d wind gain comes from BodyWindGain"), SampleIndex), Samples[SampleIndex].WindGain, Settings.BodyWindGain);
	}

	TArray<FTDFog3DInputSample> BladeSamples;
	ATDFog3DZoneActor::AppendBladeSamples(SocketWorld, Settings.BladeBaseLocalCm, Settings.BladeTipLocalCm, Settings.BladeRadiusCm, Settings.BladeWindStrength, 0.75f, Settings.BladeErosionPerSec, BladeSamples);
	TestTrue(TEXT("Blade samples carry the gain they are given"), BladeSamples.Num() > 0 && !BladeSamples.ContainsByPredicate([](const FTDFog3DInputSample& Sample) { return Sample.WindGain != 0.75f; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DZoneMaterialParametersTest,
	"TDGame.FluidFX.Fog3D.ZoneMaterialParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DZoneMaterialParametersTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DZoneActorTestPrivate;

	const FVector HalfExtentCm(3000.0, 3000.0, 150.0);
	FTDFog3DMaterialParameters MaterialParameters;
	MaterialParameters.Window = FVector4f(1.0f, 2.0f, 3.0f, 4.0f);
	MaterialParameters.Active = 1.0f;
	ATDFog3DZoneActor::ComputeConstantMaterialParameters(FTDFog3DSettings(), HalfExtentCm, 256, MaterialParameters);

	const auto ExpectVector = [this](const TCHAR* Name, const FVector4f& Actual, const FVector4f& Expected)
	{
		TestTrue(FString::Printf(TEXT("%s is %s (got %s)"), Name, *DescribeVector4(Expected), *DescribeVector4(Actual)), IsNear(Actual, Expected, 1.0e-6f));
	};
	ExpectVector(TEXT("Fog3DGrid"), MaterialParameters.Grid, FVector4f(15.625f, 30.0f, 8.0f, 8.0f));
	ExpectVector(TEXT("Fog3DBaseShape"), MaterialParameters.BaseShape, FVector4f(0.8f, 90.0f, 45.0f, 0.0f));
	ExpectVector(TEXT("Fog3DZone"), MaterialParameters.Zone, FVector4f(3000.0f, 3000.0f, 150.0f, 500.0f));
	ExpectVector(TEXT("Fog3DFineNoise"), MaterialParameters.FineNoise, FVector4f(0.2f, 50.0f, 1.0f, 0.0f));
	ExpectVector(TEXT("Fog3DStainNoise with the default stains"), MaterialParameters.StainNoise, FVector4f(0.75f, 400.0f, 0.0f, 0.0f));
	FTDFog3DSettings StainsOffSettings;
	StainsOffSettings.bStainNoiseEnabled = false;
	ATDFog3DZoneActor::ComputeConstantMaterialParameters(StainsOffSettings, HalfExtentCm, 256, MaterialParameters);
	ExpectVector(TEXT("Fog3DStainNoise with stains off"), MaterialParameters.StainNoise, FVector4f(0.0f, 400.0f, 0.0f, 0.0f));
	ExpectVector(TEXT("Fog3DWindow keeps x and y and sets the floor height and the window size"), MaterialParameters.Window, FVector4f(1.0f, 2.0f, -150.0f, 256.0f));
	TestEqual(TEXT("Fog3DActive is left untouched"), MaterialParameters.Active, 1.0f);

	FTDFog3DSettings ChangedSettings;
	ChangedSettings.bFineNoiseEnabled = false;
	ChangedSettings.bStainNoiseEnabled = true;
	ChangedSettings.StainNoiseAmplitude = 0.3f;
	ChangedSettings.StainNoiseSeed = 7;
	ChangedSettings.EdgeBandCells = 40;
	ChangedSettings.ZoneEdgeFadeCm = 0.0f;
	ATDFog3DZoneActor::ComputeConstantMaterialParameters(ChangedSettings, FVector(800.0, 1200.0, 300.0), 128, MaterialParameters);
	ExpectVector(TEXT("Fog3DWindow follows a new zone height and resolution"), MaterialParameters.Window, FVector4f(1.0f, 2.0f, -300.0f, 128.0f));
	ExpectVector(TEXT("Fog3DFineNoise with fine noise off"), MaterialParameters.FineNoise, FVector4f(0.0f, 50.0f, 1.0f, 0.0f));
	ExpectVector(TEXT("Fog3DStainNoise with stains on"), MaterialParameters.StainNoise, FVector4f(0.3f, 400.0f, 7.0f, 0.0f));
	ExpectVector(TEXT("Fog3DGrid band cells clamp to the module limit"), MaterialParameters.Grid, FVector4f(15.625f, 30.0f, 8.0f, static_cast<float>(TDFluidGpuFog3DMaximumEdgeBandCells)));
	ExpectVector(TEXT("Fog3DZone follows the half extent and a hard edge"), MaterialParameters.Zone, FVector4f(800.0f, 1200.0f, 300.0f, 0.0f));

	const FVector4f Window = FTDFog3DLayer::ComputeMaterialWindow(FIntPoint(-128, -128), 15.625, FVector(0.0, 0.0, 150.0), 0.0, 256);
	ExpectVector(TEXT("Fog3DWindow for a stage zone centred at (0, 0, 150)"), Window, FVector4f(-2000.0f, -2000.0f, -150.0f, 256.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DZoneVolumeScaleTest,
	"TDGame.FluidFX.Fog3D.ZoneVolumeScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DZoneVolumeScaleTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Engine cube (extent 50) scales to the stage zone"), ATDFog3DZoneActor::ComputeZoneVolumeScale(FVector(3000.0, 3000.0, 150.0), FVector(50.0)).Equals(FVector(60.0, 60.0, 3.0), 1.0e-9));
	TestTrue(TEXT("Uneven mesh extent scales each axis"), ATDFog3DZoneActor::ComputeZoneVolumeScale(FVector(100.0, 200.0, 300.0), FVector(10.0, 20.0, 150.0)).Equals(FVector(10.0, 10.0, 2.0), 1.0e-9));
	TestTrue(TEXT("A flat mesh axis keeps scale one"), ATDFog3DZoneActor::ComputeZoneVolumeScale(FVector(100.0, 100.0, 100.0), FVector(50.0, 50.0, 0.0)).Equals(FVector(2.0, 2.0, 1.0), 1.0e-9));
	TestTrue(TEXT("A non-positive half extent keeps scale one"), ATDFog3DZoneActor::ComputeZoneVolumeScale(FVector(-100.0, 0.0, 100.0), FVector(50.0)).Equals(FVector(1.0, 1.0, 2.0), 1.0e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DZoneTickGatesTest,
	"TDGame.FluidFX.Fog3D.ZoneTickGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DZoneTickGatesTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DZoneActorTestPrivate;

	const FTDFog3DZoneTickGates Simulating = MakeSimulatingGates();
	TestTrue(TEXT("All gates open simulates"), ATDFog3DZoneActor::DecideTickOutcome(Simulating) == ETDFog3DZoneTickOutcome::Simulate);
	TestTrue(TEXT("All gates open draws volumetric fog"), ATDFog3DZoneActor::IsVolumetricFogDrawn(Simulating));

	struct FGateCase
	{
		const TCHAR* Name;
		TFunction<void(FTDFog3DZoneTickGates&)> Change;
		ETDFog3DZoneTickOutcome Expected;
	};
	const TArray<FGateCase> Cases = {
		{ TEXT("Not the representative zone hides"), [](FTDFog3DZoneTickGates& Gates) { Gates.bIsRepresentative = false; }, ETDFog3DZoneTickOutcome::Hidden },
		{ TEXT("Invalid settings hide"), [](FTDFog3DZoneTickGates& Gates) { Gates.bHasValidSettings = false; }, ETDFog3DZoneTickOutcome::Hidden },
		{ TEXT("Disabled data hides"), [](FTDFog3DZoneTickGates& Gates) { Gates.bIsEnabled = false; }, ETDFog3DZoneTickOutcome::Hidden },
		{ TEXT("Material not loaded hides"), [](FTDFog3DZoneTickGates& Gates) { Gates.bIsMaterialReady = false; }, ETDFog3DZoneTickOutcome::Hidden },
		{ TEXT("Hidden wins over an outside pawn"), [](FTDFog3DZoneTickGates& Gates) { Gates.bIsEnabled = false; Gates.bIsPawnInsideZone = false; }, ETDFog3DZoneTickOutcome::Hidden },
		{ TEXT("No pawn rests with base fog"), [](FTDFog3DZoneTickGates& Gates) { Gates.bHasPawn = false; }, ETDFog3DZoneTickOutcome::Inactive },
		{ TEXT("Pawn outside the zone rests with base fog"), [](FTDFog3DZoneTickGates& Gates) { Gates.bIsPawnInsideZone = false; }, ETDFog3DZoneTickOutcome::Inactive },
		{ TEXT("r.VolumetricFog 0 rests"), [](FTDFog3DZoneTickGates& Gates) { Gates.ConsoleVolumetricFog = 0; }, ETDFog3DZoneTickOutcome::Inactive },
		{ TEXT("No height fog rests"), [](FTDFog3DZoneTickGates& Gates) { Gates.bHasHeightFog = false; }, ETDFog3DZoneTickOutcome::Inactive },
		{ TEXT("Height fog without volumetric fog rests"), [](FTDFog3DZoneTickGates& Gates) { Gates.bHeightFogEnablesVolumetricFog = false; }, ETDFog3DZoneTickOutcome::Inactive },
		{ TEXT("Hidden height fog rests"), [](FTDFog3DZoneTickGates& Gates) { Gates.bHeightFogRenders = false; }, ETDFog3DZoneTickOutcome::Inactive },
		{ TEXT("Mode 0 rests with base fog"), [](FTDFog3DZoneTickGates& Gates) { Gates.Mode = 0; }, ETDFog3DZoneTickOutcome::Inactive },
		{ TEXT("Mode 1 simulates"), [](FTDFog3DZoneTickGates& Gates) { Gates.Mode = 1; }, ETDFog3DZoneTickOutcome::Simulate },
		{ TEXT("r.VolumetricFog 2 still counts as on"), [](FTDFog3DZoneTickGates& Gates) { Gates.ConsoleVolumetricFog = 2; }, ETDFog3DZoneTickOutcome::Simulate },
	};
	for (const FGateCase& Case : Cases)
	{
		FTDFog3DZoneTickGates Gates = Simulating;
		Case.Change(Gates);
		TestTrue(Case.Name, ATDFog3DZoneActor::DecideTickOutcome(Gates) == Case.Expected);
	}

	FTDFog3DZoneTickGates FogOff = Simulating;
	FogOff.bHeightFogEnablesVolumetricFog = false;
	TestFalse(TEXT("Volumetric fog switched off on the height fog is not drawn"), ATDFog3DZoneActor::IsVolumetricFogDrawn(FogOff));

	FTDFog3DHeightFogState DrawingFog;
	DrawingFog.bIsRegistered = true;
	DrawingFog.bRenders = true;
	DrawingFog.bEnablesVolumetricFog = true;
	DrawingFog.FogDensity = 0.02f;
	DrawingFog.FogMaxOpacity = 1.0f;
	DrawingFog.VolumetricFogDistance = 6000.0f;
	TestTrue(TEXT("A drawn height fog with volumetric fog, density, opacity and distance is usable"), ATDFog3DZoneActor::IsHeightFogDrawingVolumetricFog(DrawingFog));
	TestFalse(TEXT("An empty height fog state is not usable"), ATDFog3DZoneActor::IsHeightFogDrawingVolumetricFog(FTDFog3DHeightFogState()));
	FTDFog3DHeightFogState SecondFogOnly = DrawingFog;
	SecondFogOnly.FogDensity = 0.0f;
	SecondFogOnly.SecondFogDensity = 0.02f;
	TestTrue(TEXT("A height fog whose only density is in the second fog data is usable"), ATDFog3DZoneActor::IsHeightFogDrawingVolumetricFog(SecondFogOnly));
	struct FHeightFogCase
	{
		const TCHAR* Name;
		TFunction<void(FTDFog3DHeightFogState&)> Change;
	};
	const TArray<FHeightFogCase> HeightFogCases = {
		{ TEXT("An unregistered height fog is not usable"), [](FTDFog3DHeightFogState& State) { State.bIsRegistered = false; } },
		{ TEXT("A hidden height fog is not usable"), [](FTDFog3DHeightFogState& State) { State.bRenders = false; } },
		{ TEXT("A height fog without volumetric fog is not usable"), [](FTDFog3DHeightFogState& State) { State.bEnablesVolumetricFog = false; } },
		{ TEXT("A height fog with zero density is not usable"), [](FTDFog3DHeightFogState& State) { State.FogDensity = 0.0f; } },
		{ TEXT("A height fog with density below the engine threshold in both fog data is not usable"), [](FTDFog3DHeightFogState& State) { State.FogDensity = 0.0f; State.SecondFogDensity = UE_DELTA * 0.0005f; } },
		{ TEXT("A height fog with not a number density is not usable"), [](FTDFog3DHeightFogState& State) { State.FogDensity = std::numeric_limits<float>::quiet_NaN(); } },
		{ TEXT("A height fog with zero maximum opacity is not usable"), [](FTDFog3DHeightFogState& State) { State.FogMaxOpacity = 0.0f; } },
		{ TEXT("A height fog with zero volumetric fog distance is not usable"), [](FTDFog3DHeightFogState& State) { State.VolumetricFogDistance = 0.0f; } },
	};
	for (const FHeightFogCase& Case : HeightFogCases)
	{
		FTDFog3DHeightFogState State = DrawingFog;
		Case.Change(State);
		TestFalse(Case.Name, ATDFog3DZoneActor::IsHeightFogDrawingVolumetricFog(State));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DZoneActorLifecycleTest,
	"TDGame.FluidFX.Fog3D.ZoneActorLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DZoneActorLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DZoneActorTestPrivate;

	AddExpectedMessagePlain(TEXT("has no VolumeMaterial and stays hidden"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("only one 3D fog zone is supported"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("Fog3D zone summary:\\s*submitted=0\\s+rejected=0"), ELogVerbosity::Display, EAutomationExpectedMessageFlags::Contains, 1);

	FTDFog3DZoneTestWorldScope WorldScope;
	UWorld& World = *WorldScope.World;
	ATDFog3DZoneActor* FirstZone = World.SpawnActor<ATDFog3DZoneActor>(FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator);
	ATDFog3DZoneActor* SecondZone = World.SpawnActor<ATDFog3DZoneActor>(FVector(10000.0, 0.0, 150.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("First zone spawns"), FirstZone) || !TestNotNull(TEXT("Second zone spawns"), SecondZone))
	{
		return false;
	}

	TestTrue(TEXT("Tick runs after animation and sockets"), FirstZone->PrimaryActorTick.TickGroup == TG_PostUpdateWork);
	TestTrue(TEXT("Actor can tick"), FirstZone->PrimaryActorTick.bCanEverTick);
	const UStaticMeshComponent* ZoneVolume = FirstZone->ZoneVolume;
	if (TestNotNull(TEXT("Zone volume component exists"), ZoneVolume))
	{
		TestTrue(TEXT("Zone volume has no collision"), ZoneVolume->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Zone volume casts no shadow"), ZoneVolume->CastShadow);
		TestFalse(TEXT("Zone volume is hidden until the material is ready"), ZoneVolume->IsVisible());
		TestTrue(TEXT("Zone volume keeps world rotation"), ZoneVolume->IsUsingAbsoluteRotation());
		TestTrue(TEXT("Zone volume keeps world scale"), ZoneVolume->IsUsingAbsoluteScale());
		TestTrue(TEXT("Zone volume is attached to the zone root"), ZoneVolume->GetAttachParent() == FirstZone->ZoneRoot);
	}
	TestTrue(TEXT("Default half extent is the stage zone"), FirstZone->ZoneHalfExtentCm.Equals(FVector(3000.0, 3000.0, 150.0)));

	TestTrue(TEXT("First zone becomes the representative"), FTDFog3DZoneActorTestAccess::IsRepresentative(*FirstZone));
	TestFalse(TEXT("Second zone rests"), FTDFog3DZoneActorTestAccess::IsRepresentative(*SecondZone));
	TestTrue(TEXT("Representative reads valid settings"), FTDFog3DZoneActorTestAccess::HasValidSettings(*FirstZone));
	TestNull(TEXT("Resting zone creates no state target"), FTDFog3DZoneActorTestAccess::GetStateTarget(*SecondZone));

	const int32 Resolution = FTDFog3DZoneActorTestAccess::GetLayerResolution(*FirstZone);
	TestTrue(TEXT("Representative configures a positive resolution"), Resolution > 0);
	const UTextureRenderTargetVolume* StateTarget = FTDFog3DZoneActorTestAccess::GetStateTarget(*FirstZone);
	if (TestNotNull(TEXT("Representative creates the state target at begin play"), StateTarget))
	{
		TestEqual(TEXT("State target width"), StateTarget->SizeX, Resolution);
		TestEqual(TEXT("State target height"), StateTarget->SizeY, Resolution);
		TestEqual(TEXT("State target layers"), StateTarget->SizeZ, TDFluidGpuFog3DLayerCount);
		TestTrue(TEXT("State target is half float RGBA"), StateTarget->OverrideFormat == PF_FloatRGBA);
		TestTrue(TEXT("State target supports UAV writes"), StateTarget->bSupportsUAV != 0);
		TestTrue(TEXT("State target is transient"), StateTarget->HasAnyFlags(RF_Transient));
	}

	World.DestroyActor(FirstZone);
	TestFalse(TEXT("Ended zone gives up the representative role"), FTDFog3DZoneActorTestAccess::IsRepresentative(*FirstZone));
	TestNull(TEXT("Ended zone drops its state target"), FTDFog3DZoneActorTestAccess::GetStateTarget(*FirstZone));
	TestFalse(TEXT("The resting zone is not promoted in stage one"), FTDFog3DZoneActorTestAccess::IsRepresentative(*SecondZone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DZoneTickTrackingTest,
	"TDGame.FluidFX.Fog3D.ZoneTickTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DZoneTickTrackingTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DZoneActorTestPrivate;

	AddExpectedMessagePlain(TEXT("has no VolumeMaterial and stays hidden"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("Fog3D zone summary:\\s*submitted=0\\s+rejected=0"), ELogVerbosity::Display, EAutomationExpectedMessageFlags::Contains, 1);

	FTDFog3DZoneTestWorldScope WorldScope;
	UWorld& World = *WorldScope.World;
	ATDFog3DZoneActor* Zone = World.SpawnActor<ATDFog3DZoneActor>(FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The zone spawns"), Zone) || !TestTrue(TEXT("The zone is the representative"), FTDFog3DZoneActorTestAccess::IsRepresentative(*Zone)))
	{
		return false;
	}

	FTDFog3DLayer& Layer = FTDFog3DZoneActorTestAccess::GetLayer(*Zone);
	const int32 Resolution = Layer.GetResolution();
	if (!TestTrue(TEXT("The layer is configured at begin play"), Resolution > 0))
	{
		return false;
	}

	const FTDFog3DMaterialParameters& Desired = FTDFog3DZoneActorTestAccess::GetDesiredMaterialParameters(*Zone);
	TestNearlyEqual(TEXT("Before the first step the window height is minus the half height"), Desired.Window.Z, static_cast<float>(-Zone->ZoneHalfExtentCm.Z), 1.0e-4f);
	TestNearlyEqual(TEXT("Before the first step the window size is the layer resolution"), Desired.Window.W, static_cast<float>(Resolution), 1.0e-4f);

	const FVector RestingSample(100.0, 0.0, 50.0);
	const FVector WalkedSample(110.0, 0.0, 50.0);
	const auto MakeRequest = [](const FVector& SampleWorld)
	{
		FTDFog3DStepRequest Request;
		Request.Clock.bStepsNow = true;
		Request.Clock.StepDeltaSeconds = 1.0f / 60.0f;
		Request.Clock.ElapsedSeconds = 1.0f / 60.0f;
		FTDFog3DInputSample Sample;
		Sample.CurrentWorld = SampleWorld;
		Request.Inputs.Add(Sample);
		return Request;
	};
	const auto CommitStep = [&Layer, &MakeRequest, &RestingSample]()
	{
		FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, MakeRequest(RestingSample));
	};
	const auto ExpectPending = [this, &Layer](const TCHAR* Name, bool bExpectedZoneReentry, bool bExpectedFogReenable)
	{
		TestTrue(FString::Printf(TEXT("%s: zone re-entry request is %s"), Name, bExpectedZoneReentry ? TEXT("pending") : TEXT("not pending")), FTDFog3DLayerTestAccess::IsZoneReentryPending(Layer) == bExpectedZoneReentry);
		TestTrue(FString::Printf(TEXT("%s: volumetric fog re-enable request is %s"), Name, bExpectedFogReenable ? TEXT("pending") : TEXT("not pending")), FTDFog3DLayerTestAccess::IsVolumetricFogReenablePending(Layer) == bExpectedFogReenable);
	};
	const auto Track = [Zone](const FTDFog3DZoneTickGates& Gates)
	{
		FTDFog3DZoneActorTestAccess::TrackResetCauses(*Zone, Gates);
	};

	CommitStep();
	ExpectPending(TEXT("After the first submitted step"), false, false);
	Track(MakeSimulatingGates());
	ExpectPending(TEXT("An ordinary tick"), false, false);

	{
		FTDFog3DZoneTickGates Outside = MakeSimulatingGates();
		Outside.bIsPawnInsideZone = false;
		Track(Outside);
		ExpectPending(TEXT("The pawn walks outside the zone"), false, false);
		Track(MakeSimulatingGates());
		ExpectPending(TEXT("The pawn comes back inside"), true, false);
		CommitStep();
		Track(MakeSimulatingGates());
		ExpectPending(TEXT("The tick after the re-entry was submitted"), false, false);
	}

	{
		FTDFog3DZoneTickGates DisabledGates = MakeSimulatingGates();
		DisabledGates.bIsEnabled = false;
		Track(DisabledGates);
		ExpectPending(TEXT("The zone is hidden"), false, false);
		Track(MakeSimulatingGates());
		ExpectPending(TEXT("The zone is computed again after hiding"), true, false);
		CommitStep();
	}

	{
		FTDFog3DZoneTickGates FogOff = MakeSimulatingGates();
		FogOff.bHeightFogEnablesVolumetricFog = false;
		Track(FogOff);
		ExpectPending(TEXT("Volumetric fog is switched off"), false, false);
		Track(MakeSimulatingGates());
		ExpectPending(TEXT("Volumetric fog is switched on again"), false, true);
		CommitStep();
		Track(MakeSimulatingGates());
		ExpectPending(TEXT("The tick after the fog re-enable was submitted"), false, false);

		FTDFog3DZoneTickGates ConsoleOff = MakeSimulatingGates();
		ConsoleOff.ConsoleVolumetricFog = 0;
		Track(ConsoleOff);
		Track(MakeSimulatingGates());
		ExpectPending(TEXT("r.VolumetricFog is switched back on"), false, true);
		CommitStep();

		FTDFog3DZoneTickGates NoHeightFog = MakeSimulatingGates();
		NoHeightFog.bHasHeightFog = false;
		NoHeightFog.bHeightFogEnablesVolumetricFog = false;
		NoHeightFog.bHeightFogRenders = false;
		Track(NoHeightFog);
		Track(MakeSimulatingGates());
		ExpectPending(TEXT("A height fog shows up again"), false, true);
		CommitStep();
	}

	{
		FTDFog3DZoneTickGates NoPawn = MakeSimulatingGates();
		NoPawn.bHasPawn = false;
		NoPawn.bIsPawnInsideZone = false;
		Track(NoPawn);
		Track(MakeSimulatingGates());
		ExpectPending(TEXT("The pawn appears after none"), false, false);
	}

	APawn* FirstPawn = World.SpawnActor<APawn>();
	APawn* SecondPawn = World.SpawnActor<APawn>();
	if (!TestTrue(TEXT("Two pawns spawn"), FirstPawn && SecondPawn))
	{
		return false;
	}
	const auto ExpectSphereRemembered = [this, &Layer, &MakeRequest, &WalkedSample](const TCHAR* Name, bool bExpectedRemembered)
	{
		const FTDFluidGpuFog3DFrameInput Built = Layer.BuildFrameInput(MakeRequest(WalkedSample));
		TestFalse(FString::Printf(TEXT("%s: the state is not reset"), Name), Built.bResetsState);
		if (!TestEqual(FString::Printf(TEXT("%s: one sphere"), Name), Built.Spheres.Num(), 1))
		{
			return;
		}
		const FTDFluidGpuFog3DSphere& Sphere = Built.Spheres[0];
		const bool bAtRest = Sphere.StartCmRadiusCm.X == Sphere.EndCmWindStrength.X && Sphere.VelocityCmPerSecErosionPerSec.X == 0.0f;
		TestTrue(FString::Printf(TEXT("%s: the sphere %s its last position"), Name, bExpectedRemembered ? TEXT("remembers") : TEXT("forgot")), !bAtRest == bExpectedRemembered);
	};

	CommitStep();
	ExpectSphereRemembered(TEXT("Before any pawn change"), true);
	FTDFog3DZoneActorTestAccess::ForgetInputHistoryWhenPawnChanges(*Zone, FirstPawn);
	ExpectSphereRemembered(TEXT("The first pawn appears"), false);
	CommitStep();
	FTDFog3DZoneActorTestAccess::ForgetInputHistoryWhenPawnChanges(*Zone, FirstPawn);
	ExpectSphereRemembered(TEXT("The same pawn ticks again"), true);
	FTDFog3DZoneActorTestAccess::ForgetInputHistoryWhenPawnChanges(*Zone, nullptr);
	ExpectSphereRemembered(TEXT("The pawn is gone"), true);
	FTDFog3DZoneActorTestAccess::ForgetInputHistoryWhenPawnChanges(*Zone, SecondPawn);
	ExpectSphereRemembered(TEXT("Another pawn appears after none"), false);
	CommitStep();
	FTDFog3DZoneActorTestAccess::ForgetInputHistoryWhenPawnChanges(*Zone, FirstPawn);
	ExpectSphereRemembered(TEXT("The pawn is swapped"), false);
	ExpectPending(TEXT("Pawn changes never request a restart"), false, false);

	UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), Zone);
	if (!TestNotNull(TEXT("A material instance is created"), MaterialInstance))
	{
		return false;
	}
	FTDFog3DZoneActorTestAccess::AttachMaterialInstance(*Zone, MaterialInstance);
	FTDFog3DZoneActorTestAccess::WriteMaterialParameters(*Zone);

	struct FVectorParameter
	{
		const TCHAR* Name;
		FVector4f FTDFog3DMaterialParameters::* Member;
	};
	const TArray<FVectorParameter> VectorParameters = {
		{ TEXT("Fog3DWindow"), &FTDFog3DMaterialParameters::Window },
		{ TEXT("Fog3DGrid"), &FTDFog3DMaterialParameters::Grid },
		{ TEXT("Fog3DBaseShape"), &FTDFog3DMaterialParameters::BaseShape },
		{ TEXT("Fog3DZone"), &FTDFog3DMaterialParameters::Zone },
		{ TEXT("Fog3DFineNoise"), &FTDFog3DMaterialParameters::FineNoise },
		{ TEXT("Fog3DStainNoise"), &FTDFog3DMaterialParameters::StainNoise },
	};
	const FLinearColor Sentinel(-7.0f, -7.0f, -7.0f, -7.0f);
	const auto ReadVector = [MaterialInstance](const TCHAR* Name, FLinearColor& OutValue)
	{
		return MaterialInstance->GetVectorParameterValue(FName(Name), OutValue);
	};
	for (const FVectorParameter& Parameter : VectorParameters)
	{
		FLinearColor Value;
		const FVector4f Expected = Desired.*(Parameter.Member);
		if (TestTrue(FString::Printf(TEXT("%s is written on the first write"), Parameter.Name), ReadVector(Parameter.Name, Value)))
		{
			TestTrue(FString::Printf(TEXT("%s holds the desired value"), Parameter.Name), IsNear(FVector4f(Value.R, Value.G, Value.B, Value.A), Expected, 1.0e-5f));
		}
		MaterialInstance->SetVectorParameterValue(FName(Parameter.Name), Sentinel);
	}
	float ActiveValue = 0.0f;
	if (TestTrue(TEXT("Fog3DActive is written on the first write"), MaterialInstance->GetScalarParameterValue(FName(TEXT("Fog3DActive")), ActiveValue)))
	{
		TestEqual(TEXT("Fog3DActive starts at zero"), ActiveValue, 0.0f);
	}
	MaterialInstance->SetScalarParameterValue(FName(TEXT("Fog3DActive")), -7.0f);

	const auto ExpectSentinels = [this, &ReadVector, &VectorParameters, &Sentinel](const TCHAR* Name, const TCHAR* ChangedName)
	{
		for (const FVectorParameter& Parameter : VectorParameters)
		{
			FLinearColor Value;
			const bool bIsChanged = ChangedName && FCString::Strcmp(Parameter.Name, ChangedName) == 0;
			if (!ReadVector(Parameter.Name, Value))
			{
				AddError(FString::Printf(TEXT("%s: %s cannot be read"), Name, Parameter.Name));
				continue;
			}
			const bool bIsSentinel = Value.R == Sentinel.R && Value.A == Sentinel.A;
			TestTrue(FString::Printf(TEXT("%s: %s is %s"), Name, Parameter.Name, bIsChanged ? TEXT("written again") : TEXT("left alone")), bIsSentinel == !bIsChanged);
		}
	};

	FTDFog3DZoneActorTestAccess::WriteMaterialParameters(*Zone);
	ExpectSentinels(TEXT("An unchanged write"), nullptr);
	float SentinelActive = 0.0f;
	MaterialInstance->GetScalarParameterValue(FName(TEXT("Fog3DActive")), SentinelActive);
	TestEqual(TEXT("An unchanged write leaves Fog3DActive alone"), SentinelActive, -7.0f);

	FTDFog3DZoneActorTestAccess::GetDesiredMaterialParameters(*Zone).Grid.X += 1.0f;
	FTDFog3DZoneActorTestAccess::WriteMaterialParameters(*Zone);
	ExpectSentinels(TEXT("A changed grid"), TEXT("Fog3DGrid"));
	FLinearColor GridValue;
	if (ReadVector(TEXT("Fog3DGrid"), GridValue))
	{
		TestNearlyEqual(TEXT("The changed grid carries the new value"), GridValue.R, Desired.Grid.X, 1.0e-5f);
	}

	FTDFog3DZoneActorTestAccess::SetActiveParameter(*Zone, true);
	MaterialInstance->GetScalarParameterValue(FName(TEXT("Fog3DActive")), SentinelActive);
	TestEqual(TEXT("Turning the zone on writes Fog3DActive"), SentinelActive, 1.0f);
	MaterialInstance->SetScalarParameterValue(FName(TEXT("Fog3DActive")), -7.0f);
	FTDFog3DZoneActorTestAccess::SetActiveParameter(*Zone, true);
	MaterialInstance->GetScalarParameterValue(FName(TEXT("Fog3DActive")), SentinelActive);
	TestEqual(TEXT("Turning the zone on twice writes Fog3DActive once"), SentinelActive, -7.0f);
	FTDFog3DZoneActorTestAccess::SetActiveParameter(*Zone, false);
	MaterialInstance->GetScalarParameterValue(FName(TEXT("Fog3DActive")), SentinelActive);
	TestEqual(TEXT("Turning the zone off writes Fog3DActive"), SentinelActive, 0.0f);

	FTDFog3DZoneActorTestAccess::AttachMaterialInstance(*Zone, nullptr);
	World.DestroyActor(Zone);
	return true;
}

#endif
