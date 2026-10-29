#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidBrushModel.h"
#include "FluidFX/TDFluidInputBrushRules.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidTypes.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

namespace TDFluidBrushModelTestPrivate
{
	constexpr int32 SeededStampCount = 32;
	constexpr int32 SeededStampRandomSeed = 1531;
	constexpr int32 JitterSampleCount = 64;
	constexpr float RadiusTolerance = 1.0e-4f;
	constexpr double SpeedTolerance = 1.0e-3;
	constexpr double PositionTolerance = 1.0e-4;
	constexpr float StrengthTolerance = 1.0e-5f;
	const ETDFluidBrushSource AllSources[] = {
		ETDFluidBrushSource::Other, ETDFluidBrushSource::PointArray, ETDFluidBrushSource::DataChannel, ETDFluidBrushSource::Fragment,
		ETDFluidBrushSource::Pivot, ETDFluidBrushSource::Bone, ETDFluidBrushSource::TrackPoint, ETDFluidBrushSource::Pointer,
		ETDFluidBrushSource::Shared, ETDFluidBrushSource::Field, ETDFluidBrushSource::Rain };

	FTDFluidPlaneBasis MakeCameraFacingBasis()
	{
		return FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(-30.0, 40.0, 0.0), 0.0f);
	}

	FTDFluidBrushStamp MakeSeededStamp(FRandomStream& Random, int32 Index)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(Random.FRandRange(-500.0f, 500.0f), Random.FRandRange(-500.0f, 500.0f), Random.FRandRange(-200.0f, 200.0f));
		Stamp.WorldVelocity = FVector(Random.FRandRange(-400.0f, 400.0f), Random.FRandRange(-400.0f, 400.0f), Random.FRandRange(-100.0f, 100.0f));
		Stamp.RadiusCm = Random.FRandRange(10.0f, 120.0f);
		Stamp.DensityStrength = Random.FRandRange(-1.0f, 1.0f);
		Stamp.VelocityStrength = Random.FRandRange(0.0f, 2.0f);
		Stamp.RadialVelocityCmPerSec = Random.FRandRange(-300.0f, 300.0f);
		Stamp.Channel = static_cast<ETDFluidBrushChannel>(Index % 4);
		Stamp.RippleImpulseCm = Random.FRandRange(0.0f, 5.0f);
		Stamp.HeatStrength = Random.FRandRange(0.0f, 1.0f);
		Stamp.ShapeAspectRatio = Random.FRandRange(1.0f, 4.0f);
		Stamp.ShapeHardness = Random.FRandRange(0.0f, 0.95f);
		Stamp.ShapeMajorAxis = FVector(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f), 0.0);
		Stamp.Source = AllSources[Index % static_cast<int32>(UE_ARRAY_COUNT(AllSources))];
		return Stamp;
	}

	TArray<FTDFluidBrushStamp> MakeSeededStamps(int32 Count)
	{
		FRandomStream Random(SeededStampRandomSeed);
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Stamps.Add(MakeSeededStamp(Random, Index));
		}
		return Stamps;
	}

	FTDFluidBrushStamp MakeModelStamp(ETDFluidBrushSource Source, const FVector& Location, const FVector& Velocity)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Location;
		Stamp.WorldVelocity = Velocity;
		Stamp.RadiusCm = 40.0f;
		Stamp.DensityStrength = 0.8f;
		Stamp.VelocityStrength = 1.25f;
		Stamp.Source = Source;
		return Stamp;
	}

	bool AreFloatsBitIdentical(float A, float B)
	{
		return FMemory::Memcmp(&A, &B, sizeof(float)) == 0;
	}

	bool AreDoublesBitIdentical(double A, double B)
	{
		return FMemory::Memcmp(&A, &B, sizeof(double)) == 0;
	}

	bool AreVectorsBitIdentical(const FVector& A, const FVector& B)
	{
		return AreDoublesBitIdentical(A.X, B.X) && AreDoublesBitIdentical(A.Y, B.Y) && AreDoublesBitIdentical(A.Z, B.Z);
	}

	bool AreStampsBitIdentical(const FTDFluidBrushStamp& A, const FTDFluidBrushStamp& B)
	{
		return AreVectorsBitIdentical(A.WorldLocation, B.WorldLocation) && AreVectorsBitIdentical(A.WorldVelocity, B.WorldVelocity)
			&& AreFloatsBitIdentical(A.RadiusCm, B.RadiusCm) && AreFloatsBitIdentical(A.DensityStrength, B.DensityStrength)
			&& AreFloatsBitIdentical(A.VelocityStrength, B.VelocityStrength) && AreFloatsBitIdentical(A.RadialVelocityCmPerSec, B.RadialVelocityCmPerSec)
			&& A.Channel == B.Channel && AreFloatsBitIdentical(A.RippleImpulseCm, B.RippleImpulseCm) && AreFloatsBitIdentical(A.HeatStrength, B.HeatStrength)
			&& AreFloatsBitIdentical(A.RingInnerRadiusRatio, B.RingInnerRadiusRatio) && AreFloatsBitIdentical(A.RingSideArcWeight, B.RingSideArcWeight)
			&& AreFloatsBitIdentical(A.RingInnerEraseRatePerSec, B.RingInnerEraseRatePerSec) && A.Source == B.Source
			&& AreFloatsBitIdentical(A.ShapeAspectRatio, B.ShapeAspectRatio) && AreFloatsBitIdentical(A.ShapeHardness, B.ShapeHardness)
			&& AreVectorsBitIdentical(A.ShapeMajorAxis, B.ShapeMajorAxis) && A.bHasGroundElevation == B.bHasGroundElevation
			&& AreDoublesBitIdentical(A.GroundElevationCm, B.GroundElevationCm) && A.bAddsDensity == B.bAddsDensity;
	}

	FTDFluidBrushStamp ApplyModel(const FTDFluidBrushStamp& Stamp, const FTDFluidInputBrushSettings& Settings, const FTDFluidPlaneBasis& Basis)
	{
		FTDFluidBrushStamp Result = Stamp;
		FTDFluidBrushModel::ApplyToStamp(Result, Settings, Basis);
		return Result;
	}

	double MeasurePlaneSpeed(const FTDFluidBrushStamp& Stamp, const FTDFluidPlaneBasis& Basis)
	{
		return Basis.ProjectVector(Stamp.WorldVelocity).Size();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBrushModelDefaultsAreIdentityTest,
	"TDGame.FluidFX.InputBrush.ModelDefaultsAreIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBrushModelDefaultsAreIdentityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBrushModelTestPrivate;

	const FTDFluidInputBrushSettings DefaultSettings;
	TestFalse(TEXT("Default settings carry no active brush model"), FTDFluidBrushModel::HasActiveModel(DefaultSettings));
	TestFalse(TEXT("Default settings keep input brush rules inactive"), FTDFluidInputBrushRules::HasActiveRules(DefaultSettings));

	FTDFluidInputBrushSettings InvertOnlySettings;
	InvertOnlySettings.bShouldInvertInputDensity = true;
	FTDFluidInputBrushSettings ScaledSettings;
	ScaledSettings.GlobalBrushRadiusScale = 2.0f;
	TestTrue(TEXT("A global radius scale activates the input brush rules"), FTDFluidInputBrushRules::HasActiveRules(ScaledSettings));

	const TArray<FTDFluidBrushStamp> OriginalStamps = MakeSeededStamps(SeededStampCount);
	const FTDFluidPlaneBasis Bases[] = { FTDFluidPlaneBasis::MakeWorldFacing(), MakeCameraFacingBasis() };
	for (const FTDFluidPlaneBasis& Basis : Bases)
	{
		int32 ModelChangedCount = 0;
		for (const FTDFluidBrushStamp& Original : OriginalStamps)
		{
			ModelChangedCount += AreStampsBitIdentical(ApplyModel(Original, DefaultSettings, Basis), Original) ? 0 : 1;
		}
		TestEqual(TEXT("Default brush model leaves every stamp field bit identical"), ModelChangedCount, 0);

		TArray<FTDFluidBrushStamp> InvertedStamps = OriginalStamps;
		FTDFluidInputBrushRules::ApplyToStamps(InvertedStamps, InvertOnlySettings, Basis, 37.0);
		if (!TestEqual(TEXT("Invert-only rules keep every stamp"), InvertedStamps.Num(), OriginalStamps.Num()))
		{
			return false;
		}
		int32 InvertMismatchCount = 0;
		for (int32 Index = 0; Index < OriginalStamps.Num(); ++Index)
		{
			FTDFluidBrushStamp Expected = OriginalStamps[Index];
			Expected.DensityStrength = Expected.DensityStrength != 0.0f ? -Expected.DensityStrength : Expected.DensityStrength;
			InvertMismatchCount += AreStampsBitIdentical(InvertedStamps[Index], Expected) ? 0 : 1;
		}
		TestEqual(TEXT("Default brush model adds nothing when other input rules are active"), InvertMismatchCount, 0);

		TArray<FTDFluidBrushStamp> ScaledStamps = OriginalStamps;
		FTDFluidInputBrushRules::ApplyToStamps(ScaledStamps, ScaledSettings, Basis, 37.0);
		int32 ScaledChangedCount = 0;
		for (int32 Index = 0; Index < OriginalStamps.Num(); ++Index)
		{
			ScaledChangedCount += AreFloatsBitIdentical(ScaledStamps[Index].RadiusCm, OriginalStamps[Index].RadiusCm) ? 0 : 1;
		}
		TestTrue(TEXT("An active brush model changes stamps through ApplyToStamps"), ScaledChangedCount > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBrushModelSourceRadiusScalesTest,
	"TDGame.FluidFX.InputBrush.SourceRadiusScales",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBrushModelSourceRadiusScalesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBrushModelTestPrivate;

	FTDFluidInputBrushSettings Settings;
	Settings.GlobalBrushRadiusScale = 1.5f;
	Settings.PivotBrushRadiusScale = 2.0f;
	Settings.BoneBrushRadiusScale = 3.0f;
	Settings.PointerBrushRadiusScale = 0.5f;
	Settings.ParticleBrushRadiusScale = 1.25f;
	Settings.FragmentBrushRadiusScale = 4.0f;
	Settings.SharedBrushRadiusScale = 0.75f;
	TestTrue(TEXT("Radius scales activate the brush model"), FTDFluidBrushModel::HasActiveModel(Settings));

	TestEqual(TEXT("Pivot source uses the pivot scale"), FTDFluidBrushModel::ResolveSourceRadiusScale(Settings, ETDFluidBrushSource::Pivot), Settings.PivotBrushRadiusScale);
	TestEqual(TEXT("Bone source uses the bone scale"), FTDFluidBrushModel::ResolveSourceRadiusScale(Settings, ETDFluidBrushSource::Bone), Settings.BoneBrushRadiusScale);
	TestEqual(TEXT("Pointer source uses the pointer scale"), FTDFluidBrushModel::ResolveSourceRadiusScale(Settings, ETDFluidBrushSource::Pointer), Settings.PointerBrushRadiusScale);
	TestEqual(TEXT("Point array source uses the particle scale"), FTDFluidBrushModel::ResolveSourceRadiusScale(Settings, ETDFluidBrushSource::PointArray), Settings.ParticleBrushRadiusScale);
	TestEqual(TEXT("Fragment source uses the fragment scale"), FTDFluidBrushModel::ResolveSourceRadiusScale(Settings, ETDFluidBrushSource::Fragment), Settings.FragmentBrushRadiusScale);
	TestEqual(TEXT("Shared source uses the shared scale"), FTDFluidBrushModel::ResolveSourceRadiusScale(Settings, ETDFluidBrushSource::Shared), Settings.SharedBrushRadiusScale);
	const ETDFluidBrushSource UnscaledSources[] = { ETDFluidBrushSource::Other, ETDFluidBrushSource::DataChannel, ETDFluidBrushSource::TrackPoint, ETDFluidBrushSource::Field, ETDFluidBrushSource::Rain };
	for (const ETDFluidBrushSource Source : UnscaledSources)
	{
		TestEqual(TEXT("Sources without their own scale resolve to one"), FTDFluidBrushModel::ResolveSourceRadiusScale(Settings, Source), 1.0f);
	}

	const FTDFluidPlaneBasis Basis = FTDFluidPlaneBasis::MakeWorldFacing();
	TArray<FTDFluidBrushStamp> RuleStamps;
	for (const ETDFluidBrushSource Source : AllSources)
	{
		const FTDFluidBrushStamp Original = MakeModelStamp(Source, FVector(120.0, -80.0, 15.0), FVector::ZeroVector);
		RuleStamps.Add(Original);
		const FTDFluidBrushStamp Scaled = ApplyModel(Original, Settings, Basis);
		const float ExpectedRadius = Source == ETDFluidBrushSource::DataChannel
			? Original.RadiusCm
			: Original.RadiusCm * (Settings.GlobalBrushRadiusScale * FTDFluidBrushModel::ResolveSourceRadiusScale(Settings, Source));
		TestNearlyEqual(*FString::Printf(TEXT("Radius for source %d equals original times global times source scale"), static_cast<int32>(Source)), Scaled.RadiusCm, ExpectedRadius, RadiusTolerance);
	}

	FTDFluidInputBrushRules::ApplyToStamps(RuleStamps, Settings, Basis, 0.0);
	if (!TestEqual(TEXT("Radius scaling keeps every stamp"), RuleStamps.Num(), static_cast<int32>(UE_ARRAY_COUNT(AllSources))))
	{
		return false;
	}
	TestNearlyEqual(TEXT("ApplyToStamps applies the bone radius scale"), RuleStamps[5].RadiusCm, 40.0f * 1.5f * 3.0f, RadiusTolerance);
	TestNearlyEqual(TEXT("ApplyToStamps skips data channel stamps"), RuleStamps[2].RadiusCm, 40.0f, RadiusTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBrushModelVelocityExponentAndClampTest,
	"TDGame.FluidFX.InputBrush.VelocityExponentAndClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBrushModelVelocityExponentAndClampTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBrushModelTestPrivate;

	const FTDFluidPlaneBasis WorldBasis = FTDFluidPlaneBasis::MakeWorldFacing();
	FTDFluidInputBrushSettings SquaredSettings;
	SquaredSettings.BrushVelocityReferenceCmPerSec = 100.0f;
	SquaredSettings.BrushVelocityExponent = 2.0f;
	TestTrue(TEXT("A velocity exponent activates the brush model"), FTDFluidBrushModel::HasActiveModel(SquaredSettings));

	const FTDFluidBrushStamp Moving = MakeModelStamp(ETDFluidBrushSource::Pivot, FVector(0.0, 0.0, 0.0), FVector(200.0, 0.0, 0.0));
	TestNearlyEqual(TEXT("Speed 200 with reference 100 and exponent 2 becomes 400"), MeasurePlaneSpeed(ApplyModel(Moving, SquaredSettings, WorldBasis), WorldBasis), 400.0, SpeedTolerance);

	FTDFluidInputBrushSettings ClampedSettings = SquaredSettings;
	ClampedSettings.BrushVelocityClampCmPerSec = 300.0f;
	TestNearlyEqual(TEXT("Speed clamp 300 caps the shaped speed"), MeasurePlaneSpeed(ApplyModel(Moving, ClampedSettings, WorldBasis), WorldBasis), 300.0, SpeedTolerance);

	FTDFluidInputBrushSettings RootSettings;
	RootSettings.BrushVelocityReferenceCmPerSec = 100.0f;
	RootSettings.BrushVelocityExponent = 0.5f;
	const FTDFluidBrushStamp Fast = MakeModelStamp(ETDFluidBrushSource::Bone, FVector::ZeroVector, FVector(400.0, 0.0, 0.0));
	TestNearlyEqual(TEXT("Speed 400 with exponent 0.5 becomes 200"), MeasurePlaneSpeed(ApplyModel(Fast, RootSettings, WorldBasis), WorldBasis), 200.0, SpeedTolerance);

	const FTDFluidBrushStamp Diagonal = MakeModelStamp(ETDFluidBrushSource::Pivot, FVector::ZeroVector, FVector(120.0, 160.0, 0.0));
	const FVector DiagonalShaped = ApplyModel(Diagonal, SquaredSettings, WorldBasis).WorldVelocity;
	TestTrue(TEXT("Speed shaping keeps the velocity direction"), DiagonalShaped.Equals(FVector(240.0, 320.0, 0.0), SpeedTolerance));

	FTDFluidInputBrushSettings IdentitySettings;
	IdentitySettings.BrushVelocityExponent = 1.0f;
	IdentitySettings.BrushVelocityClampCmPerSec = 0.0f;
	TestTrue(TEXT("Exponent 1 with clamp 0 leaves the velocity bit identical"), AreVectorsBitIdentical(ApplyModel(Moving, IdentitySettings, WorldBasis).WorldVelocity, Moving.WorldVelocity));

	const FTDFluidPlaneBasis CameraBasis = MakeCameraFacingBasis();
	FTDFluidBrushStamp CameraStamp = Moving;
	CameraStamp.WorldVelocity = CameraBasis.UnprojectVector(FVector2D(0.0, 200.0)) + CameraBasis.N * 50.0;
	TestNearlyEqual(TEXT("Camera-facing plane speed 200 becomes 400"), MeasurePlaneSpeed(ApplyModel(CameraStamp, SquaredSettings, CameraBasis), CameraBasis), 400.0, SpeedTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBrushModelLeadJitterNoiseDeterministicTest,
	"TDGame.FluidFX.InputBrush.LeadJitterNoiseDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBrushModelLeadJitterNoiseDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBrushModelTestPrivate;

	const FTDFluidPlaneBasis WorldBasis = FTDFluidPlaneBasis::MakeWorldFacing();
	FTDFluidInputBrushSettings LeadSettings;
	LeadSettings.MotionLeadRatio = 0.5f;
	const FTDFluidBrushStamp Moving = MakeModelStamp(ETDFluidBrushSource::Pivot, FVector(10.0, 20.0, 30.0), FVector(300.0, 0.0, 0.0));
	TestTrue(TEXT("Lead 0.5 with radius 40 moves the stamp 20 cm along its motion"), ApplyModel(Moving, LeadSettings, WorldBasis).WorldLocation.Equals(FVector(30.0, 20.0, 30.0), PositionTolerance));
	const FTDFluidBrushStamp Resting = MakeModelStamp(ETDFluidBrushSource::Pivot, FVector(10.0, 20.0, 30.0), FVector::ZeroVector);
	TestTrue(TEXT("A resting stamp has no lead"), AreVectorsBitIdentical(ApplyModel(Resting, LeadSettings, WorldBasis).WorldLocation, Resting.WorldLocation));

	const FTDFluidPlaneBasis CameraBasis = MakeCameraFacingBasis();
	FTDFluidBrushStamp CameraMoving = Moving;
	CameraMoving.WorldVelocity = CameraBasis.UnprojectVector(FVector2D(0.0, 250.0)) + CameraBasis.N * 80.0;
	const FTDFluidBrushStamp CameraLed = ApplyModel(CameraMoving, LeadSettings, CameraBasis);
	const FVector2D CameraLeadOffset = CameraBasis.ProjectPoint(CameraLed.WorldLocation) - CameraBasis.ProjectPoint(CameraMoving.WorldLocation);
	TestTrue(TEXT("Camera-facing lead follows the plane velocity"), CameraLeadOffset.Equals(FVector2D(0.0, 20.0), PositionTolerance));
	TestNearlyEqual(TEXT("Camera-facing lead keeps the plane depth"), CameraBasis.ProjectDepth(CameraLed.WorldLocation), CameraBasis.ProjectDepth(CameraMoving.WorldLocation), PositionTolerance);

	FTDFluidInputBrushSettings JitterSettings;
	JitterSettings.PositionJitterRatio = 0.25f;
	JitterSettings.BrushNoiseSeed = 17;
	FTDFluidInputBrushSettings OtherSeedSettings = JitterSettings;
	OtherSeedSettings.BrushNoiseSeed = 18;
	const double JitterLimitCm = 0.25 * 40.0 + PositionTolerance;
	const TArray<FTDFluidBrushStamp> SeededStamps = MakeSeededStamps(JitterSampleCount);
	int32 MovedCount = 0;
	int32 OutOfRangeCount = 0;
	int32 RepeatMismatchCount = 0;
	int32 SeedDifferenceCount = 0;
	for (const FTDFluidBrushStamp& Seeded : SeededStamps)
	{
		const FTDFluidBrushStamp Original = MakeModelStamp(ETDFluidBrushSource::Pivot, Seeded.WorldLocation, FVector::ZeroVector);
		const FTDFluidBrushStamp First = ApplyModel(Original, JitterSettings, WorldBasis);
		const FTDFluidBrushStamp Second = ApplyModel(Original, JitterSettings, WorldBasis);
		const FVector Offset = First.WorldLocation - Original.WorldLocation;
		MovedCount += Offset.IsNearlyZero(PositionTolerance) ? 0 : 1;
		OutOfRangeCount += (FMath::Abs(Offset.X) > JitterLimitCm || FMath::Abs(Offset.Y) > JitterLimitCm || FMath::Abs(Offset.Z) > PositionTolerance) ? 1 : 0;
		RepeatMismatchCount += AreStampsBitIdentical(First, Second) ? 0 : 1;
		SeedDifferenceCount += AreVectorsBitIdentical(First.WorldLocation, ApplyModel(Original, OtherSeedSettings, WorldBasis).WorldLocation) ? 0 : 1;
	}
	TestTrue(TEXT("Jitter moves stamps"), MovedCount > 0);
	TestEqual(TEXT("Jitter stays within ratio times radius on each plane axis"), OutOfRangeCount, 0);
	TestEqual(TEXT("Jitter repeats bit identically for the same input"), RepeatMismatchCount, 0);
	TestTrue(TEXT("A different seed changes the jitter"), SeedDifferenceCount > 0);

	FTDFluidInputBrushSettings SpeedJitterSettings = JitterSettings;
	SpeedJitterSettings.bJitterScalesWithSpeed = true;
	TestTrue(TEXT("Speed-scaled jitter leaves a resting stamp in place"), AreVectorsBitIdentical(ApplyModel(Resting, SpeedJitterSettings, WorldBasis).WorldLocation, Resting.WorldLocation));
	const FTDFluidBrushStamp FastStamp = MakeModelStamp(ETDFluidBrushSource::Pivot, FVector(10.0, 20.0, 30.0), FVector(500.0, 0.0, 0.0));
	TestTrue(TEXT("Speed-scaled jitter at or above the reference speed equals full jitter"),
		ApplyModel(FastStamp, SpeedJitterSettings, WorldBasis).WorldLocation.Equals(ApplyModel(FastStamp, JitterSettings, WorldBasis).WorldLocation, PositionTolerance));

	FTDFluidInputBrushSettings DensityNoiseSettings;
	DensityNoiseSettings.DensityNoiseAmount = 0.4f;
	DensityNoiseSettings.DensityNoiseScaleCm = 50.0f;
	FTDFluidInputBrushSettings VelocityNoiseSettings;
	VelocityNoiseSettings.VelocityNoiseAmount = 0.5f;
	VelocityNoiseSettings.VelocityNoiseScaleCm = 50.0f;
	int32 DensityOutOfRangeCount = 0;
	int32 DistinctDensityCount = 0;
	int32 SpeedChangedCount = 0;
	int32 OverRotatedCount = 0;
	int32 RotatedCount = 0;
	float FirstDensityFactor = -1.0f;
	for (const FTDFluidBrushStamp& Seeded : SeededStamps)
	{
		FTDFluidBrushStamp Original = MakeModelStamp(ETDFluidBrushSource::Bone, Seeded.WorldLocation, FVector(Seeded.WorldVelocity.X, Seeded.WorldVelocity.Y, 0.0) + FVector(150.0, 0.0, 0.0));
		Original.DensityStrength = 1.0f;
		const float DensityFactor = ApplyModel(Original, DensityNoiseSettings, WorldBasis).DensityStrength;
		DensityOutOfRangeCount += (DensityFactor < 0.6f - StrengthTolerance || DensityFactor > 1.0f + StrengthTolerance) ? 1 : 0;
		FirstDensityFactor = FirstDensityFactor < 0.0f ? DensityFactor : FirstDensityFactor;
		DistinctDensityCount += FMath::IsNearlyEqual(DensityFactor, FirstDensityFactor, StrengthTolerance) ? 0 : 1;

		const FVector2D OriginalVelocity = WorldBasis.ProjectVector(Original.WorldVelocity);
		const FVector2D NoisyVelocity = WorldBasis.ProjectVector(ApplyModel(Original, VelocityNoiseSettings, WorldBasis).WorldVelocity);
		SpeedChangedCount += FMath::IsNearlyEqual(NoisyVelocity.Size(), OriginalVelocity.Size(), SpeedTolerance) ? 0 : 1;
		const double CosAngle = FMath::Clamp(OriginalVelocity.GetSafeNormal() | NoisyVelocity.GetSafeNormal(), -1.0, 1.0);
		const double AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(CosAngle));
		OverRotatedCount += AngleDegrees > 45.0 + 1.0e-2 ? 1 : 0;
		RotatedCount += AngleDegrees > 1.0e-2 ? 1 : 0;
	}
	TestEqual(TEXT("Density noise factor stays within 1 - amount and 1"), DensityOutOfRangeCount, 0);
	TestTrue(TEXT("Density noise varies with position"), DistinctDensityCount > 0);
	TestEqual(TEXT("Velocity noise keeps the plane speed"), SpeedChangedCount, 0);
	TestEqual(TEXT("Velocity noise turns at most amount times 90 degrees"), OverRotatedCount, 0);
	TestTrue(TEXT("Velocity noise turns some stamps"), RotatedCount > 0);

	int32 NoiseOutOfRangeCount = 0;
	int32 NoiseRepeatMismatchCount = 0;
	for (int32 Index = 0; Index < JitterSampleCount; ++Index)
	{
		const FVector2D Position(-731.25 + 37.5 * Index, 412.5 - 23.75 * Index);
		const float Noise = FTDFluidBrushModel::SampleValueNoise(Position, 80.0f, 5, 3);
		NoiseOutOfRangeCount += (Noise < 0.0f || Noise > 1.0f) ? 1 : 0;
		NoiseRepeatMismatchCount += AreFloatsBitIdentical(Noise, FTDFluidBrushModel::SampleValueNoise(Position, 80.0f, 5, 3)) ? 0 : 1;
	}
	TestEqual(TEXT("Value noise stays within zero and one"), NoiseOutOfRangeCount, 0);
	TestEqual(TEXT("Value noise repeats bit identically"), NoiseRepeatMismatchCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBrushModelAdditiveAndSharedScalesTest,
	"TDGame.FluidFX.InputBrush.AdditiveAndSharedScales",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBrushModelAdditiveAndSharedScalesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBrushModelTestPrivate;

	const FTDFluidPlaneBasis Basis = FTDFluidPlaneBasis::MakeWorldFacing();
	FTDFluidInputBrushSettings AdditiveSettings;
	AdditiveSettings.bAdditiveStrokeBlend = true;
	AdditiveSettings.AdditiveDensityRatePerSec = 6.0f;
	TestTrue(TEXT("Additive blending activates the brush model"), FTDFluidBrushModel::HasActiveModel(AdditiveSettings));

	FTDFluidBrushStamp Positive = MakeModelStamp(ETDFluidBrushSource::Pivot, FVector::ZeroVector, FVector::ZeroVector);
	Positive.DensityStrength = 0.5f;
	const FTDFluidBrushStamp PositiveResult = ApplyModel(Positive, AdditiveSettings, Basis);
	TestTrue(TEXT("Positive density becomes additive"), PositiveResult.bAddsDensity);
	TestNearlyEqual(TEXT("Additive density is strength times rate"), PositiveResult.DensityStrength, 0.5f * 6.0f, StrengthTolerance);

	FTDFluidBrushStamp Eraser = Positive;
	Eraser.DensityStrength = -0.5f;
	const FTDFluidBrushStamp EraserResult = ApplyModel(Eraser, AdditiveSettings, Basis);
	TestFalse(TEXT("Erasing density stays a max-blend stamp"), EraserResult.bAddsDensity);
	TestTrue(TEXT("Erasing density is unchanged"), AreFloatsBitIdentical(EraserResult.DensityStrength, Eraser.DensityStrength));

	FTDFluidBrushStamp VelocityOnly = Positive;
	VelocityOnly.DensityStrength = 0.0f;
	TestFalse(TEXT("Zero density stays a max-blend stamp"), ApplyModel(VelocityOnly, AdditiveSettings, Basis).bAddsDensity);

	FTDFluidBrushStamp DataChannel = Positive;
	DataChannel.Source = ETDFluidBrushSource::DataChannel;
	const FTDFluidBrushStamp DataChannelResult = ApplyModel(DataChannel, AdditiveSettings, Basis);
	TestFalse(TEXT("Data channel stamps skip the brush model"), DataChannelResult.bAddsDensity);
	TestTrue(TEXT("Data channel density is unchanged"), AreFloatsBitIdentical(DataChannelResult.DensityStrength, DataChannel.DensityStrength));

	FTDFluidInputBrushSettings SharedSettings;
	SharedSettings.SharedInputDensityScale = 2.0f;
	SharedSettings.SharedInputVelocityScale = 0.5f;
	TestTrue(TEXT("Shared input scales activate the brush model"), FTDFluidBrushModel::HasActiveModel(SharedSettings));
	const FTDFluidBrushStamp SharedStamp = MakeModelStamp(ETDFluidBrushSource::Shared, FVector::ZeroVector, FVector::ZeroVector);
	const FTDFluidBrushStamp SharedResult = ApplyModel(SharedStamp, SharedSettings, Basis);
	TestNearlyEqual(TEXT("Shared density scale multiplies density"), SharedResult.DensityStrength, SharedStamp.DensityStrength * 2.0f, StrengthTolerance);
	TestNearlyEqual(TEXT("Shared velocity scale multiplies velocity strength"), SharedResult.VelocityStrength, SharedStamp.VelocityStrength * 0.5f, StrengthTolerance);
	const FTDFluidBrushStamp PivotStamp = MakeModelStamp(ETDFluidBrushSource::Pivot, FVector::ZeroVector, FVector::ZeroVector);
	TestTrue(TEXT("Shared scales leave other sources bit identical"), AreStampsBitIdentical(ApplyModel(PivotStamp, SharedSettings, Basis), PivotStamp));

	FTDFluidInputBrushSettings SharedAdditiveSettings = SharedSettings;
	SharedAdditiveSettings.bAdditiveStrokeBlend = true;
	SharedAdditiveSettings.AdditiveDensityRatePerSec = 6.0f;
	FTDFluidBrushStamp SharedPositive = SharedStamp;
	SharedPositive.DensityStrength = 0.5f;
	const FTDFluidBrushStamp SharedAdditiveResult = ApplyModel(SharedPositive, SharedAdditiveSettings, Basis);
	TestTrue(TEXT("Shared additive stamp is additive"), SharedAdditiveResult.bAddsDensity);
	TestNearlyEqual(TEXT("Shared scale applies before the additive rate"), SharedAdditiveResult.DensityStrength, 0.5f * 2.0f * 6.0f, StrengthTolerance);

	TArray<FTDFluidBrushStamp> RuleStamps = { Positive, DataChannel };
	FTDFluidInputBrushRules::ApplyToStamps(RuleStamps, AdditiveSettings, Basis, 0.0);
	if (!TestEqual(TEXT("Additive rules keep every stamp"), RuleStamps.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("ApplyToStamps marks pivot stamps additive"), RuleStamps[0].bAddsDensity);
	TestFalse(TEXT("ApplyToStamps leaves data channel stamps unmarked"), RuleStamps[1].bAddsDensity);
	return true;
}

#endif
