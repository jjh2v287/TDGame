#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidFragmentEmitterActor.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"

namespace TDFluidFragmentFootprintTestPrivate
{
	constexpr double FootprintYawDegrees = 30.0;
	constexpr double FootprintHalfLengthCm = 100.0;
	constexpr double FootprintHalfWidthCm = 25.0;
	constexpr double FootprintHalfHeightCm = 10.0;
	constexpr double MassOffsetCm = 30.0;
	constexpr float FootprintStepSeconds = 1.0f / 30.0f;

	FBox MakeLeafBox(const FVector& Offset)
	{
		const FVector HalfSize(FootprintHalfLengthCm, FootprintHalfWidthCm, FootprintHalfHeightCm);
		return FBox(Offset - HalfSize, Offset + HalfSize);
	}

	FTransform MakeLeafPose(const FVector& Location)
	{
		return FTransform(FRotator(0.0, FootprintYawDegrees, 0.0), Location);
	}

	double ResolveAxisAngleDegrees(const FVector& Axis)
	{
		const double AngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(Axis.Y, Axis.X));
		return FMath::Fmod(AngleDegrees + 360.0, 180.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFootprintStampMatchesLeafBoxTest,
	"TDGame.FluidFX.Fragment.FootprintStampMatchesLeafBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFootprintStampMatchesLeafBoxTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentFootprintTestPrivate;

	const FTDFluidFragmentTrailSettings Settings;
	const FTDFluidFragmentTarget Target;
	const FVector LeafLocation(300.0, 200.0, 50.0);
	FTDFluidBrushStamp Stamp;
	if (!TestTrue(TEXT("A valid leaf box builds a footprint stamp"), ATDFluidFragmentEmitterActor::BuildFootprintStamp(MakeLeafBox(FVector::ZeroVector), FTransform::Identity, MakeLeafPose(LeafLocation), FTransform::Identity, FVector::ZeroVector, 0.0, Target, Settings, Stamp)))
	{
		return false;
	}

	const float ExpectedRadiusCm = static_cast<float>(FootprintHalfLengthCm * 2.0 / FMath::Sqrt(UE_DOUBLE_PI));
	TestNearlyEqual(TEXT("The radius keeps the box area"), Stamp.RadiusCm, ExpectedRadiusCm, 1.0e-3f);
	TestNearlyEqual(TEXT("A 200 by 50 box gives aspect 4"), Stamp.ShapeAspectRatio, 4.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("The major axis follows the 30 degree yaw"), ResolveAxisAngleDegrees(Stamp.ShapeMajorAxis), FootprintYawDegrees, 0.5);
	TestNearlyEqual(TEXT("The stamp sits at the leaf center X"), Stamp.WorldLocation.X, LeafLocation.X, 1.0e-3);
	TestNearlyEqual(TEXT("The stamp sits at the leaf center Y"), Stamp.WorldLocation.Y, LeafLocation.Y, 1.0e-3);
	TestNearlyEqual(TEXT("The stamp sits at the box bottom"), Stamp.WorldLocation.Z, LeafLocation.Z - FootprintHalfHeightCm, 1.0e-3);
	TestNearlyEqual(TEXT("The hardness comes from the settings"), Stamp.ShapeHardness, Settings.FootprintHardness, 1.0e-6f);
	TestTrue(TEXT("The footprint stamp is a fragment source"), Stamp.Source == ETDFluidBrushSource::Fragment);

	const FBox PivotFrameBox = MakeLeafBox(FVector(MassOffsetCm, 0.0, 0.0));
	const FTransform MassToLocal(FVector(MassOffsetCm, 0.0, 0.0));
	FTDFluidBrushStamp MassFrameStamp;
	ATDFluidFragmentEmitterActor::BuildFootprintStamp(PivotFrameBox, MassToLocal, MakeLeafPose(LeafLocation), FTransform::Identity, FVector::ZeroVector, 0.0, Target, Settings, MassFrameStamp);
	TestNearlyEqual(TEXT("A mass frame offset still centers the footprint on the leaf X"), MassFrameStamp.WorldLocation.X, LeafLocation.X, 1.0e-3);
	TestNearlyEqual(TEXT("A mass frame offset still centers the footprint on the leaf Y"), MassFrameStamp.WorldLocation.Y, LeafLocation.Y, 1.0e-3);

	FTDFluidBrushStamp IdentityMassStamp;
	ATDFluidFragmentEmitterActor::BuildFootprintStamp(PivotFrameBox, FTransform::Identity, MakeLeafPose(LeafLocation), FTransform::Identity, FVector::ZeroVector, 0.0, Target, Settings, IdentityMassStamp);
	const double IdentityOffsetCm = FVector::Dist2D(IdentityMassStamp.WorldLocation, MassFrameStamp.WorldLocation);
	TestTrue(*FString::Printf(TEXT("Ignoring MassToLocal would shift the footprint by the mass offset (%.2f cm)"), IdentityOffsetCm), FMath::Abs(IdentityOffsetCm - MassOffsetCm) < 0.01);

	FTDFluidBrushStamp ComponentStamp;
	ATDFluidFragmentEmitterActor::BuildFootprintStamp(MakeLeafBox(FVector::ZeroVector), FTransform::Identity, MakeLeafPose(LeafLocation), FTransform(FVector(1000.0, 0.0, 0.0)), FVector::ZeroVector, 0.0, Target, Settings, ComponentStamp);
	TestNearlyEqual(TEXT("The component transform moves the footprint"), ComponentStamp.WorldLocation.X, LeafLocation.X + 1000.0, 1.0e-3);

	FTDFluidFragmentTrailSettings HeightLimitedSettings = Settings;
	HeightLimitedSettings.FootprintMaxHeightCm = 20.0f;
	FTDFluidBrushStamp HeightStamp;
	TestFalse(TEXT("A leaf 40 cm above the target exceeds a 20 cm height limit"), ATDFluidFragmentEmitterActor::BuildFootprintStamp(MakeLeafBox(FVector::ZeroVector), FTransform::Identity, MakeLeafPose(LeafLocation), FTransform::Identity, FVector::ZeroVector, 0.0, Target, HeightLimitedSettings, HeightStamp));
	TestTrue(TEXT("A leaf 10 cm above the target stays inside a 20 cm height limit"), ATDFluidFragmentEmitterActor::BuildFootprintStamp(MakeLeafBox(FVector::ZeroVector), FTransform::Identity, MakeLeafPose(LeafLocation), FTransform::Identity, FVector::ZeroVector, 30.0, Target, HeightLimitedSettings, HeightStamp));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentInputModeSelectsStampKindsTest,
	"TDGame.FluidFX.Fragment.InputModeSelectsStampKinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentInputModeSelectsStampKindsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentFootprintTestPrivate;

	TArray<FVector> PreviousPositions = { FVector(0.0, 0.0, 100.0), FVector(200.0, 0.0, 100.0) };
	TArray<FVector> CurrentPositions = { FVector(20.0, 0.0, 100.0), FVector(220.0, 0.0, 100.0) };
	FTDFluidFragmentLeafPoses LeafPoses;
	for (const FVector& CurrentPosition : CurrentPositions)
	{
		LeafPoses.LocalBounds.Add(MakeLeafBox(FVector::ZeroVector));
		LeafPoses.MassToLocalTransforms.Add(FTransform::Identity);
		LeafPoses.CurrentTransforms.Add(MakeLeafPose(CurrentPosition));
	}

	const FTDFluidFragmentTarget Target;
	FTDFluidFragmentTrailSettings Settings;
	TArray<FTDFluidBrushStamp> DirectTrailStamps;
	ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(Target, PreviousPositions, CurrentPositions, PreviousPositions, FootprintStepSeconds, 0.0f, Settings, DirectTrailStamps);

	TArray<FTDFluidBrushStamp> PointStamps;
	Settings.FragmentInputMode = ETDFluidFragmentInputMode::Points;
	ATDFluidFragmentEmitterActor::BuildTargetInputStamps(Target, LeafPoses, PreviousPositions, CurrentPositions, PreviousPositions, FootprintStepSeconds, 0.0f, Settings, PointStamps);
	TestTrue(TEXT("Moving leaves give point trail stamps"), DirectTrailStamps.Num() > 0);
	TestEqual(TEXT("Points mode gives exactly the point trail stamps"), PointStamps.Num(), DirectTrailStamps.Num());

	TArray<FTDFluidBrushStamp> FootprintStamps;
	Settings.FragmentInputMode = ETDFluidFragmentInputMode::Footprint;
	ATDFluidFragmentEmitterActor::BuildTargetInputStamps(Target, LeafPoses, PreviousPositions, CurrentPositions, PreviousPositions, FootprintStepSeconds, 0.0f, Settings, FootprintStamps);
	TestEqual(TEXT("Footprint mode gives one stamp per leaf"), FootprintStamps.Num(), LeafPoses.LocalBounds.Num());
	bool bAreAllFootprints = true;
	for (const FTDFluidBrushStamp& Stamp : FootprintStamps)
	{
		bAreAllFootprints &= Stamp.ShapeAspectRatio > 1.0f && Stamp.ShapeHardness == Settings.FootprintHardness && Stamp.Source == ETDFluidBrushSource::Fragment;
	}
	TestTrue(TEXT("Footprint mode stamps carry the leaf shape"), bAreAllFootprints);
	TestTrue(TEXT("The footprint carries the leaf velocity"), FootprintStamps.Num() > 0 && FMath::IsNearlyEqual(FootprintStamps[0].WorldVelocity.X, 20.0 / FootprintStepSeconds, 1.0e-2));

	TArray<FTDFluidBrushStamp> CombinedStamps;
	Settings.FragmentInputMode = ETDFluidFragmentInputMode::PointsAndFootprint;
	ATDFluidFragmentEmitterActor::BuildTargetInputStamps(Target, LeafPoses, PreviousPositions, CurrentPositions, PreviousPositions, FootprintStepSeconds, 0.0f, Settings, CombinedStamps);
	TestEqual(TEXT("Both mode gives point and footprint stamps"), CombinedStamps.Num(), DirectTrailStamps.Num() + LeafPoses.LocalBounds.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFootprintPaintedAreaMatchesBoxTest,
	"TDGame.FluidFX.Fragment.FootprintPaintedAreaMatchesBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFootprintPaintedAreaMatchesBoxTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFragmentFootprintTestPrivate;

	FTDFluidSimulationConfig Config;
	Config.GridResolution = 256;
	Config.SimExtentCm = FVector2D(1000.0f, 1000.0f);
	Config.DensityDissipation = 0.0f;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);

	const FVector LeafLocation(40.0, -20.0, 0.0);
	FTDFluidBrushStamp Stamp;
	if (!TestTrue(TEXT("The leaf builds a footprint stamp"), ATDFluidFragmentEmitterActor::BuildFootprintStamp(MakeLeafBox(FVector::ZeroVector), FTransform::Identity, MakeLeafPose(LeafLocation), FTransform::Identity, FVector::ZeroVector, 0.0, FTDFluidFragmentTarget(), FTDFluidFragmentTrailSettings(), Stamp)))
	{
		return false;
	}
	Solver.ApplyBrushStamps({ Stamp }, FootprintStepSeconds);

	const int32 Resolution = Solver.GetResolution();
	const double CellSizeCm = Config.SimExtentCm.X / static_cast<double>(Resolution);
	int32 PaintedCellCount = 0;
	FVector2D PaintedCentroid = FVector2D::ZeroVector;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			if (Solver.SampleAtCell(CellX, CellY).PrimaryDensity <= 0.0f)
			{
				continue;
			}
			++PaintedCellCount;
			PaintedCentroid += Solver.CellToWorldCenter(CellX, CellY);
		}
	}

	if (!TestTrue(TEXT("The footprint paints cells"), PaintedCellCount > 0))
	{
		return false;
	}

	const double BoxAreaCm2 = 4.0 * FootprintHalfLengthCm * FootprintHalfWidthCm;
	const double PaintedAreaCm2 = PaintedCellCount * CellSizeCm * CellSizeCm;
	PaintedCentroid /= static_cast<double>(PaintedCellCount);
	TestTrue(*FString::Printf(TEXT("The painted area matches the box within 15 percent (%.1f of %.1f cm2)"), PaintedAreaCm2, BoxAreaCm2), FMath::Abs(PaintedAreaCm2 / BoxAreaCm2 - 1.0) <= 0.15);
	TestTrue(*FString::Printf(TEXT("The painted centroid lies within one cell of the leaf (%s)"), *PaintedCentroid.ToString()), FVector2D::Distance(PaintedCentroid, FVector2D(LeafLocation.X, LeafLocation.Y)) <= CellSizeCm);
	return true;
}

#endif
