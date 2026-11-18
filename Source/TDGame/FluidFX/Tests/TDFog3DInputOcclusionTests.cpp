#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFog3DLayer.h"
#include "FluidFX/TDFog3DTypes.h"
#include "FluidFX/TDFog3DZoneActor.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFog3DInputOcclusionTestPrivate
{
	constexpr double AnchorCellSizeCm = 15.625;
	constexpr int32 AnchorResolution = 64;

	FTDFog3DInputSample MakeAnchorInput(int32 SlotId, const FVector& CurrentWorld, float RadiusCm, const TOptional<FVector>& AnchorWorld)
	{
		FTDFog3DInputSample Input;
		Input.SlotId = SlotId;
		Input.CurrentWorld = CurrentWorld;
		Input.RadiusCm = RadiusCm;
		Input.OcclusionAnchorWorld = AnchorWorld.Get(FVector::ZeroVector);
		Input.bHasOcclusionAnchor = AnchorWorld.IsSet();
		return Input;
	}

	void PackAnchorBatch(const TArray<FTDFog3DInputSample>& Inputs, const FVector2D& WindowOriginWorldXY, double FloorZ, int32 MaximumSphereCount,
		TArray<FTDFluidGpuFog3DSphere>& OutSpheres, TArray<FTDFluidGpuFog3DSphereAnchor>& OutAnchors, int32& OutDroppedCount)
	{
		const TMap<int32, FVector> NoPreviousPositions;
		FTDFog3DLayer::PackSpheres(Inputs, NoPreviousPositions, WindowOriginWorldXY, FloorZ, true, 1.0f / 60.0f, false, 3000.0f, MaximumSphereCount,
			AnchorCellSizeCm, AnchorResolution, OutSpheres, OutAnchors, OutDroppedCount);
	}

	FTDFluidGpuFog3DSphereAnchor PackOneAnchor(const FVector& AnchorWorld, const FVector2D& WindowOriginWorldXY, double FloorZ)
	{
		TArray<FTDFog3DInputSample> Inputs;
		Inputs.Add(MakeAnchorInput(0, FVector(WindowOriginWorldXY, FloorZ) + FVector(100.0, 100.0, 60.0), 20.0f, AnchorWorld));
		TArray<FTDFluidGpuFog3DSphere> Spheres;
		TArray<FTDFluidGpuFog3DSphereAnchor> Anchors;
		int32 DroppedCount = 0;
		PackAnchorBatch(Inputs, WindowOriginWorldXY, FloorZ, 16, Spheres, Anchors, DroppedCount);
		return Anchors.Num() == 1 ? Anchors[0] : FTDFluidGpuFog3DSphereAnchor();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DInputOcclusionAnchorPackingTest,
	"TDGame.FluidFX.Fog3D.InputOcclusionAnchorPacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DInputOcclusionAnchorPackingTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DInputOcclusionTestPrivate;

	const FVector2D WindowOrigin(1000.0, -2000.0);
	const double FloorZ = 50.0;
	const FVector Center(WindowOrigin, FloorZ);
	const FVector FirstAnchor = Center + FVector(15.625 * 3.0 + 0.001, 15.625 * 10.0 - 0.001, 91.0);
	const FVector LastAnchor = Center + FVector(15.625 * 40.0, 15.625 * 2.5, 200.0);
	TArray<FTDFog3DInputSample> Inputs;
	Inputs.Add(MakeAnchorInput(1, Center + FVector(100.0, 100.0, 60.0), 20.0f, FirstAnchor));
	Inputs.Add(MakeAnchorInput(2, FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0), 20.0f, Center));
	Inputs.Add(MakeAnchorInput(3, Center + FVector(200.0, 100.0, 60.0), 0.0f, Center));
	Inputs.Add(MakeAnchorInput(4, Center + FVector(300.0, 100.0, 60.0), 35.0f, TOptional<FVector>()));
	Inputs.Add(MakeAnchorInput(5, Center + FVector(400.0, 100.0, 60.0), 45.0f, LastAnchor));

	TArray<FTDFluidGpuFog3DSphere> Spheres;
	TArray<FTDFluidGpuFog3DSphereAnchor> Anchors;
	int32 DroppedCount = 0;
	PackAnchorBatch(Inputs, WindowOrigin, FloorZ, 16, Spheres, Anchors, DroppedCount);
	TestEqual(TEXT("Skipped inputs leave three spheres"), Spheres.Num(), 3);
	TestEqual(TEXT("The anchor array has one entry per packed sphere"), Anchors.Num(), Spheres.Num());
	if (Spheres.Num() == 3 && Anchors.Num() == 3)
	{
		TestEqual(TEXT("The first packed sphere is the first valid input"), Spheres[0].StartCmRadiusCm.W, 20.0f);
		TestEqual(TEXT("The second packed sphere is the input without an anchor"), Spheres[1].StartCmRadiusCm.W, 35.0f);
		TestEqual(TEXT("The third packed sphere is the last input"), Spheres[2].StartCmRadiusCm.W, 45.0f);
		TestEqual(TEXT("The first anchor cell x floors in the window"), Anchors[0].CellX, 3);
		TestEqual(TEXT("The first anchor cell y floors just below a cell edge"), Anchors[0].CellY, 9);
		TestEqual(TEXT("The first anchor height is measured from the zone floor"), Anchors[0].ZCm, 91.0f);
		TestEqual(TEXT("The first anchor is marked present"), Anchors[0].Flags, TDFluidGpuFog3DSphereAnchorPresentFlag);
		TestEqual(TEXT("An input without an anchor packs flags 0"), Anchors[1].Flags, 0u);
		TestEqual(TEXT("An input without an anchor packs cell x 0"), Anchors[1].CellX, 0);
		TestEqual(TEXT("An input without an anchor packs cell y 0"), Anchors[1].CellY, 0);
		TestEqual(TEXT("An input without an anchor packs height 0"), Anchors[1].ZCm, 0.0f);
		TestEqual(TEXT("The last anchor keeps its own cell x"), Anchors[2].CellX, 40);
		TestEqual(TEXT("The last anchor keeps its own cell y"), Anchors[2].CellY, 2);
		TestEqual(TEXT("The last anchor keeps its own height"), Anchors[2].ZCm, 200.0f);
	}

	TArray<FTDFluidGpuFog3DSphere> LimitedSpheres;
	TArray<FTDFluidGpuFog3DSphereAnchor> LimitedAnchors;
	int32 LimitedDroppedCount = 0;
	PackAnchorBatch(Inputs, WindowOrigin, FloorZ, 2, LimitedSpheres, LimitedAnchors, LimitedDroppedCount);
	TestEqual(TEXT("The sphere limit drops the third valid input"), LimitedDroppedCount, 1);
	TestEqual(TEXT("The anchor array follows the sphere limit"), LimitedAnchors.Num(), LimitedSpheres.Num());
	TestTrue(TEXT("The limited batch keeps the input without an anchor second"), LimitedAnchors.Num() == 2 && LimitedAnchors[1].Flags == 0u);

	const FTDFluidGpuFog3DSphereAnchor BelowWindow = PackOneAnchor(Center + FVector(-1.0, -500.0, 10.0), WindowOrigin, FloorZ);
	TestEqual(TEXT("An anchor west of the window clamps to cell x 0"), BelowWindow.CellX, 0);
	TestEqual(TEXT("An anchor south of the window clamps to cell y 0"), BelowWindow.CellY, 0);
	TestEqual(TEXT("A clamped anchor is still present"), BelowWindow.Flags, TDFluidGpuFog3DSphereAnchorPresentFlag);
	const FTDFluidGpuFog3DSphereAnchor AboveWindow = PackOneAnchor(Center + FVector(15.625 * 64.0, 15.625 * 64.0 + 1000.0, 10.0), WindowOrigin, FloorZ);
	TestEqual(TEXT("An anchor east of the window clamps to the last cell x"), AboveWindow.CellX, AnchorResolution - 1);
	TestEqual(TEXT("An anchor north of the window clamps to the last cell y"), AboveWindow.CellY, AnchorResolution - 1);
	const FTDFluidGpuFog3DSphereAnchor OnEdge = PackOneAnchor(Center + FVector(15.625 * 5.0, 15.625 * 6.0, -30.0), WindowOrigin, FloorZ);
	TestEqual(TEXT("An anchor exactly on a cell edge floors into the upper cell x"), OnEdge.CellX, 5);
	TestEqual(TEXT("An anchor exactly on a cell edge floors into the upper cell y"), OnEdge.CellY, 6);
	TestEqual(TEXT("An anchor below the floor keeps a negative height"), OnEdge.ZCm, -30.0f);

	const FVector2D FarOrigin(1000000.0, 0.0);
	const FTDFluidGpuFog3DSphereAnchor FarAnchor = PackOneAnchor(FVector(FarOrigin, 0.0) + FVector(15.625 * 7.0 - 1.0e-7, 0.0, 0.0), FarOrigin, 0.0);
	TestEqual(TEXT("The cell formula runs in 64 bit so a hair below a far cell edge stays in the lower cell"), FarAnchor.CellX, 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DInputOcclusionSamplesTest,
	"TDGame.FluidFX.Fog3D.InputOcclusionSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DInputOcclusionSamplesTest::RunTest(const FString& Parameters)
{
	const FTDFog3DSettings Settings;
	FTDFog3DInputPose Pose;
	Pose.BladeSocketWorld = FTransform(FVector(100.0, 0.0, 120.0));
	Pose.HandWorld = FVector(90.0, 10.0, 110.0);
	Pose.LeftFootWorld = FVector(0.0, -15.0, 5.0);
	Pose.RightFootWorld = FVector(0.0, 15.0, 5.0);
	Pose.PelvisWorld = FVector(5.0, 0.0, 95.0);
	Pose.PawnWorld = FVector(0.0, 0.0, 90.0);

	TArray<FTDFog3DInputSample> Samples;
	ATDFog3DZoneActor::BuildInputSamples(Pose, Settings, Samples);
	TestTrue(TEXT("The pose makes blade and body samples"), Samples.Num() > 4);
	int32 PelvisAnchoredCount = 0;
	for (const FTDFog3DInputSample& Sample : Samples)
	{
		PelvisAnchoredCount += Sample.bHasOcclusionAnchor && Sample.OcclusionAnchorWorld == Pose.PelvisWorld.GetValue() ? 1 : 0;
	}
	TestEqual(TEXT("Every blade and body sample anchors at the pelvis"), PelvisAnchoredCount, Samples.Num());

	FTDFog3DInputPose NoPelvisPose = Pose;
	NoPelvisPose.PelvisWorld.Reset();
	ATDFog3DZoneActor::BuildInputSamples(NoPelvisPose, Settings, Samples);
	int32 PawnAnchoredCount = 0;
	for (const FTDFog3DInputSample& Sample : Samples)
	{
		PawnAnchoredCount += Sample.bHasOcclusionAnchor && Sample.OcclusionAnchorWorld == Pose.PawnWorld.GetValue() ? 1 : 0;
	}
	TestTrue(TEXT("The pose without a pelvis still makes samples"), Samples.Num() > 0);
	TestEqual(TEXT("Without a pelvis every sample anchors at the pawn"), PawnAnchoredCount, Samples.Num());

	FTDFog3DInputPose NoAnchorPose = NoPelvisPose;
	NoAnchorPose.PawnWorld.Reset();
	ATDFog3DZoneActor::BuildInputSamples(NoAnchorPose, Settings, Samples);
	int32 AnchoredCount = 0;
	for (const FTDFog3DInputSample& Sample : Samples)
	{
		AnchoredCount += Sample.bHasOcclusionAnchor ? 1 : 0;
	}
	TestEqual(TEXT("Without a pelvis or a pawn no sample has an anchor"), AnchoredCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DInputOcclusionSettingsTest,
	"TDGame.FluidFX.Fog3D.InputOcclusionSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DInputOcclusionSettingsTest::RunTest(const FString& Parameters)
{
	const FTDFog3DSettings Defaults;
	TestFalse(TEXT("Input occlusion is off by default"), Defaults.bOccludesInputBehindObstacles);

	FTDFog3DSettings Scribbled;
	Scribbled.bOccludesInputBehindObstacles = !Defaults.bOccludesInputBehindObstacles;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFog3DSettings::StaticStruct(), &Scribbled);
	TestEqual(TEXT("The canonical table keeps input occlusion at its default"), Scribbled.bOccludesInputBehindObstacles, Defaults.bOccludesInputBehindObstacles);

	const FString DefaultLogText = ATDFog3DZoneActor::MakeSettingsLogText(Defaults, 2, 256, 60.0f);
	TestEqual(TEXT("The settings log line keeps the earlier fields and adds occludesInput"), DefaultLogText,
		FString(TEXT("Fog3D settings: quality=2 resolution=256 stepsPerSecond=60 clampedTimeWindBlend=0 bodyWindGain=1 bladeWindGain=1 followsGround=0 occludesInput=0")));
	FTDFog3DSettings Occluding;
	Occluding.bOccludesInputBehindObstacles = true;
	TestTrue(TEXT("The settings log line shows occludesInput=1 when on"), ATDFog3DZoneActor::MakeSettingsLogText(Occluding, 2, 256, 60.0f).EndsWith(TEXT(" occludesInput=1")));
	return true;
}

#endif
