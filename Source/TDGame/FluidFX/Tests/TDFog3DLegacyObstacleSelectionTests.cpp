#include "FluidFX/TDFog3DLayer.h"
#include "FluidFX/TDFluidObstacleComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFog3DLegacyObstacleSelectionPrivate
{
	enum class EInputKind : uint8
	{
		Upright,
		TiltedPitch,
		TiltedNegativePitch,
		TiltedRoll,
		Sphere,
		Sideways
	};

	struct FInputSpec
	{
		EInputKind Kind;
		double CenterX;
		double CenterY;
		double CenterZ;
		double HalfX;
		double HalfY;
		double HalfZ;
		double YawDegrees;
	};

	struct FExpectedObstacle
	{
		int32 InputIndex;
		double WindowX;
		double WindowY;
		double WindowZ;
		double YawCosine;
		double HalfX;
		double HalfY;
		double HalfZ;
		double YawSine;
	};

	constexpr double TiltRadians = 0.02;
	constexpr double PackTolerance = 1.0e-4;
	constexpr int32 RegisteredCount = 40;
	constexpr int32 ExpectedSkippedCount = 6;
	constexpr int32 ExpectedUprightCount = 34;
	constexpr int32 TieSwapFirstIndex = 39;
	constexpr int32 TieSwapSecondIndex = 16;
	constexpr int32 DistinctSwapFirstIndex = 3;
	constexpr int32 DistinctSwapSecondIndex = 20;

	const FInputSpec InputSpecs[] = {
		{EInputKind::Upright, 0.0, 1150.0, 70.0, 4.0, 6.0, 5.0, 60.0},
		{EInputKind::Upright, -800.0, 0.0, 100.0, 30.0, 30.0, 60.0, 45.0},
		{EInputKind::Upright, 0.0, -450.0, 80.0, 20.0, 12.0, 25.0, 30.0},
		{EInputKind::Upright, 100.0, 0.0, 60.0, 4.0, 6.0, 5.0, 0.0},
		{EInputKind::Upright, 1425.0, 0.0, 90.0, 21.0, 22.0, 23.0, 0.0},
		{EInputKind::Upright, -1400.0, 0.0, 70.0, 30.0, 30.0, 60.0, 45.0},
		{EInputKind::Upright, 0.0, -1050.0, 100.0, 20.0, 12.0, 25.0, 30.0},
		{EInputKind::Upright, 700.0, 0.0, 80.0, 4.0, 6.0, 5.0, 0.0},
		{EInputKind::Upright, 0.0, 350.0, 60.0, 30.0, 30.0, 60.0, 120.0},
		{EInputKind::Sphere, 0.0, -40.0, 50.0, 20.0, 20.0, 20.0, 0.0},
		{EInputKind::Upright, -725.0, 0.0, 90.0, 15.0, 16.0, 17.0, 120.0},
		{EInputKind::Upright, 1300.0, 0.0, 100.0, 4.0, 6.0, 5.0, 0.0},
		{EInputKind::Upright, 0.0, 950.0, 80.0, 30.0, 30.0, 60.0, 120.0},
		{EInputKind::Upright, -600.0, 0.0, 60.0, 20.0, 12.0, 25.0, 90.0},
		{EInputKind::Upright, 0.0, -250.0, 90.0, 4.0, 6.0, 5.0, 60.0},
		{EInputKind::TiltedRoll, -80.0, 0.0, 50.0, 20.0, 20.0, 20.0, 0.0},
		{EInputKind::Upright, 0.0, 275.0, 90.0, 11.0, 11.0, 11.0, 45.0},
		{EInputKind::Upright, -1200.0, 0.0, 80.0, 20.0, 12.0, 25.0, 90.0},
		{EInputKind::Upright, 0.0, -850.0, 60.0, 4.0, 6.0, 5.0, 60.0},
		{EInputKind::Upright, 500.0, 0.0, 90.0, 30.0, 30.0, 60.0, 45.0},
		{EInputKind::Upright, 0.0, 150.0, 70.0, 20.0, 12.0, 25.0, 30.0},
		{EInputKind::TiltedPitch, 60.0, 0.0, 50.0, 20.0, 20.0, 20.0, 0.0},
		{EInputKind::Upright, 0.0, -1450.0, 80.0, 4.0, 6.0, 5.0, 60.0},
		{EInputKind::Upright, 1100.0, 0.0, 60.0, 30.0, 30.0, 60.0, 45.0},
		{EInputKind::Upright, 0.0, 750.0, 90.0, 20.0, 12.0, 25.0, 30.0},
		{EInputKind::Upright, -400.0, 0.0, 70.0, 4.0, 6.0, 5.0, 0.0},
		{EInputKind::Sideways, 0.0, 90.0, 50.0, 20.0, 20.0, 20.0, 0.0},
		{EInputKind::Upright, 0.0, -1425.0, 80.0, 18.0, 19.0, 20.0, 90.0},
		{EInputKind::Upright, 0.0, 1350.0, 60.0, 20.0, 12.0, 25.0, 30.0},
		{EInputKind::Upright, -1000.0, 0.0, 90.0, 4.0, 6.0, 5.0, 0.0},
		{EInputKind::Upright, 0.0, -650.0, 70.0, 30.0, 30.0, 60.0, 120.0},
		{EInputKind::Upright, 300.0, 0.0, 100.0, 20.0, 12.0, 25.0, 90.0},
		{EInputKind::Sphere, 30.0, 0.0, 50.0, 20.0, 20.0, 20.0, 0.0},
		{EInputKind::Upright, 0.0, -725.0, 80.0, 12.0, 13.0, 14.0, 60.0},
		{EInputKind::Upright, 0.0, -1250.0, 90.0, 30.0, 30.0, 60.0, 120.0},
		{EInputKind::Upright, 900.0, 0.0, 70.0, 20.0, 12.0, 25.0, 90.0},
		{EInputKind::Upright, 0.0, 550.0, 100.0, 4.0, 6.0, 5.0, 60.0},
		{EInputKind::Upright, -200.0, 0.0, 80.0, 30.0, 30.0, 60.0, 45.0},
		{EInputKind::TiltedNegativePitch, 0.0, 70.0, 50.0, 20.0, 20.0, 20.0, 40.0},
		{EInputKind::Upright, 275.0, 0.0, 80.0, 10.0, 10.0, 10.0, 30.0},
	};

	const int32 ExpectedSelectedInputIndices[] = {3, 20, 37, 14, 16, 39, 31, 8, 25, 2, 19, 36, 13, 30, 7, 10, 33, 24, 1, 18, 35, 12, 29, 6, 23, 0, 17, 34, 11, 28, 5, 4};
	const int32 ExpectedDroppedInputIndices[] = {27, 22};

	const FExpectedObstacle ExpectedObstacles[] = {
		{3, 2100.0, 2000.0, -40.0, 1.0, 7.9, 7.9, 15.1, 0.0},
		{20, 2000.0, 2150.0, -30.0, 0.8660254, 20.0, 12.0, 25.0, 0.5},
		{37, 1800.0, 2000.0, -20.0, 0.7071068, 30.0, 30.0, 60.0, 0.7071068},
		{14, 2000.0, 1750.0, -10.0, 0.5, 7.9, 7.9, 15.1, 0.8660254},
		{16, 2000.0, 2275.0, -10.0, 0.7071068, 11.0, 11.0, 15.1, 0.7071068},
		{39, 2275.0, 2000.0, -20.0, 0.8660254, 10.0, 10.0, 15.1, 0.5},
		{31, 2300.0, 2000.0, 0.0, 0.0, 20.0, 12.0, 25.0, 1.0},
		{8, 2000.0, 2350.0, -40.0, -0.5, 30.0, 30.0, 60.0, 0.8660254},
		{25, 1600.0, 2000.0, -30.0, 1.0, 7.9, 7.9, 15.1, 0.0},
		{2, 2000.0, 1550.0, -20.0, 0.8660254, 20.0, 12.0, 25.0, 0.5},
		{19, 2500.0, 2000.0, -10.0, 0.7071068, 30.0, 30.0, 60.0, 0.7071068},
		{36, 2000.0, 2550.0, 0.0, 0.5, 7.9, 7.9, 15.1, 0.8660254},
		{13, 1400.0, 2000.0, -40.0, 0.0, 20.0, 12.0, 25.0, 1.0},
		{30, 2000.0, 1350.0, -30.0, -0.5, 30.0, 30.0, 60.0, 0.8660254},
		{7, 2700.0, 2000.0, -20.0, 1.0, 7.9, 7.9, 15.1, 0.0},
		{10, 1275.0, 2000.0, -10.0, -0.5, 15.0, 16.0, 17.0, 0.8660254},
		{33, 2000.0, 1275.0, -20.0, 0.5, 12.0, 13.0, 15.1, 0.8660254},
		{24, 2000.0, 2750.0, -10.0, 0.8660254, 20.0, 12.0, 25.0, 0.5},
		{1, 1200.0, 2000.0, 0.0, 0.7071068, 30.0, 30.0, 60.0, 0.7071068},
		{18, 2000.0, 1150.0, -40.0, 0.5, 7.9, 7.9, 15.1, 0.8660254},
		{35, 2900.0, 2000.0, -30.0, 0.0, 20.0, 12.0, 25.0, 1.0},
		{12, 2000.0, 2950.0, -20.0, -0.5, 30.0, 30.0, 60.0, 0.8660254},
		{29, 1000.0, 2000.0, -10.0, 1.0, 7.9, 7.9, 15.1, 0.0},
		{6, 2000.0, 950.0, 0.0, 0.8660254, 20.0, 12.0, 25.0, 0.5},
		{23, 3100.0, 2000.0, -40.0, 0.7071068, 30.0, 30.0, 60.0, 0.7071068},
		{0, 2000.0, 3150.0, -30.0, 0.5, 7.9, 7.9, 15.1, 0.8660254},
		{17, 800.0, 2000.0, -20.0, 0.0, 20.0, 12.0, 25.0, 1.0},
		{34, 2000.0, 750.0, -10.0, -0.5, 30.0, 30.0, 60.0, 0.8660254},
		{11, 3300.0, 2000.0, 0.0, 1.0, 7.9, 7.9, 15.1, 0.0},
		{28, 2000.0, 3350.0, -40.0, 0.8660254, 20.0, 12.0, 25.0, 0.5},
		{5, 600.0, 2000.0, -30.0, 0.7071068, 30.0, 30.0, 60.0, 0.7071068},
		{4, 3425.0, 2000.0, -10.0, 1.0, 21.0, 22.0, 23.0, 0.0},
	};

	FTDFluidSdfObstacle BuildObstacle(const FInputSpec& Spec)
	{
		const FVector Center(Spec.CenterX, Spec.CenterY, Spec.CenterZ);
		const FVector HalfExtents(Spec.HalfX, Spec.HalfY, Spec.HalfZ);
		const double TiltDegrees = FMath::RadiansToDegrees(TiltRadians);
		switch (Spec.Kind)
		{
		case EInputKind::TiltedPitch:
			return UTDFluidObstacleComponent::MakeOrientedBoxObstacle(Center, HalfExtents, FRotator(TiltDegrees, Spec.YawDegrees, 0.0));
		case EInputKind::TiltedNegativePitch:
			return UTDFluidObstacleComponent::MakeOrientedBoxObstacle(Center, HalfExtents, FRotator(-TiltDegrees, Spec.YawDegrees, 0.0));
		case EInputKind::TiltedRoll:
			return UTDFluidObstacleComponent::MakeOrientedBoxObstacle(Center, HalfExtents, FRotator(0.0, Spec.YawDegrees, TiltDegrees));
		case EInputKind::Sideways:
			return UTDFluidObstacleComponent::MakeOrientedBoxObstacle(Center, HalfExtents, FRotator(0.0, Spec.YawDegrees, 90.0));
		case EInputKind::Sphere:
		{
			FTDFluidSdfObstacle Sphere;
			Sphere.bIsBox = false;
			Sphere.WorldCenter = Center;
			Sphere.RadiusCm = static_cast<float>(Spec.HalfX);
			return Sphere;
		}
		default:
			return UTDFluidObstacleComponent::MakeOrientedBoxObstacle(Center, HalfExtents, FRotator(0.0, Spec.YawDegrees, 0.0));
		}
	}

	TArray<FTDFluidSdfObstacle> BuildRegisteredObstacles()
	{
		TArray<FTDFluidSdfObstacle> Registered;
		for (const FInputSpec& Spec : InputSpecs)
		{
			Registered.Add(BuildObstacle(Spec));
		}
		return Registered;
	}

	int32 FindInputIndexByCenter(const FVector& WorldCenter)
	{
		for (int32 InputIndex = 0; InputIndex < UE_ARRAY_COUNT(InputSpecs); ++InputIndex)
		{
			const FInputSpec& Spec = InputSpecs[InputIndex];
			if (FVector(Spec.CenterX, Spec.CenterY, Spec.CenterZ).Equals(WorldCenter, 1.0e-9))
			{
				return InputIndex;
			}
		}
		return INDEX_NONE;
	}

	TArray<FTDFog3DObstacleKey> SelectKeys(const TArray<FTDFluidSdfObstacle>& Registered)
	{
		const FTDFluidGpuFog3DInflation Inflation;
		TArray<FTDFluidGpuFog3DObstacle> Packed;
		TArray<FTDFog3DObstacleKey> Keys;
		int32 Skipped = 0;
		FTDFog3DLayer::SelectAndPackObstacles(Registered, FVector2D(-2000.0, -2000.0), FVector2D(0.0, 0.0), 100.0, Inflation, 32, Packed, Keys, Skipped);
		return Keys;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLegacyObstacleSelectionTest,
	"TDGame.FluidFX.Fog3D.LegacyObstacleSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLegacyObstacleSelectionTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DLegacyObstacleSelectionPrivate;

	const FTDFluidGpuFog3DInflation Inflation;
	const FVector2D WindowOrigin(-2000.0, -2000.0);
	const FVector2D WindowCenter(0.0, 0.0);
	const double FloorZ = 100.0;
	const int32 SelectedCount = UE_ARRAY_COUNT(ExpectedSelectedInputIndices);

	const TArray<FTDFluidSdfObstacle> Registered = BuildRegisteredObstacles();
	TestEqual(TEXT("Forty obstacles are registered"), Registered.Num(), RegisteredCount);

	TArray<FTDFluidGpuFog3DObstacle> Packed;
	TArray<FTDFog3DObstacleKey> Keys;
	int32 Skipped = -1;
	FTDFog3DLayer::SelectAndPackObstacles(Registered, WindowOrigin, WindowCenter, FloorZ, Inflation, 32, Packed, Keys, Skipped);
	TestEqual(TEXT("Tilted boxes, spheres and the sideways box are skipped"), Skipped, ExpectedSkippedCount);
	TestEqual(TEXT("Upright boxes are the registered inputs minus the skipped ones"), Registered.Num() - Skipped, ExpectedUprightCount);
	TestEqual(TEXT("The limit of 32 cuts the upright boxes"), Keys.Num(), SelectedCount);
	TestEqual(TEXT("Packed count matches key count"), Packed.Num(), Keys.Num());
	if (Keys.Num() != SelectedCount || Packed.Num() != SelectedCount)
	{
		return false;
	}

	TArray<int32> SelectedIndices;
	for (const FTDFog3DObstacleKey& Key : Keys)
	{
		SelectedIndices.Add(FindInputIndexByCenter(Key.WorldCenter));
	}
	for (int32 SlotIndex = 0; SlotIndex < SelectedCount; ++SlotIndex)
	{
		TestEqual(*FString::Printf(TEXT("Selected input index at slot %d"), SlotIndex), SelectedIndices[SlotIndex], ExpectedSelectedInputIndices[SlotIndex]);
	}
	for (const int32 DroppedIndex : ExpectedDroppedInputIndices)
	{
		TestTrue(*FString::Printf(TEXT("Input %d is cut by the limit"), DroppedIndex), !SelectedIndices.Contains(DroppedIndex));
	}

	for (int32 SlotIndex = 0; SlotIndex < SelectedCount; ++SlotIndex)
	{
		const FExpectedObstacle& Expected = ExpectedObstacles[SlotIndex];
		const FInputSpec& Spec = InputSpecs[Expected.InputIndex];
		const FString SlotLabel = FString::Printf(TEXT("slot %d (input %d)"), SlotIndex, Expected.InputIndex);
		const FTDFog3DObstacleKey& Key = Keys[SlotIndex];
		const FTDFluidGpuFog3DObstacle& Obstacle = Packed[SlotIndex];
		TestEqual(*(SlotLabel + TEXT(" expected input matches selection")), SelectedIndices[SlotIndex], Expected.InputIndex);
		TestTrue(*(SlotLabel + TEXT(" key half extents")), Key.HalfExtentsCm.Equals(FVector(Spec.HalfX, Spec.HalfY, Spec.HalfZ), 1.0e-9));
		TestTrue(*(SlotLabel + TEXT(" key yaw")), FMath::IsNearlyEqual(Key.YawDegrees, Spec.YawDegrees, 1.0e-6));
		TestTrue(*(SlotLabel + TEXT(" window x")), FMath::IsNearlyEqual(static_cast<double>(Obstacle.CenterCmYawCosine.X), Expected.WindowX, PackTolerance));
		TestTrue(*(SlotLabel + TEXT(" window y")), FMath::IsNearlyEqual(static_cast<double>(Obstacle.CenterCmYawCosine.Y), Expected.WindowY, PackTolerance));
		TestTrue(*(SlotLabel + TEXT(" window z")), FMath::IsNearlyEqual(static_cast<double>(Obstacle.CenterCmYawCosine.Z), Expected.WindowZ, PackTolerance));
		TestTrue(*(SlotLabel + TEXT(" yaw cosine")), FMath::IsNearlyEqual(static_cast<double>(Obstacle.CenterCmYawCosine.W), Expected.YawCosine, PackTolerance));
		TestTrue(*(SlotLabel + TEXT(" half x")), FMath::IsNearlyEqual(static_cast<double>(Obstacle.HalfExtentCmYawSine.X), Expected.HalfX, PackTolerance));
		TestTrue(*(SlotLabel + TEXT(" half y")), FMath::IsNearlyEqual(static_cast<double>(Obstacle.HalfExtentCmYawSine.Y), Expected.HalfY, PackTolerance));
		TestTrue(*(SlotLabel + TEXT(" half z")), FMath::IsNearlyEqual(static_cast<double>(Obstacle.HalfExtentCmYawSine.Z), Expected.HalfZ, PackTolerance));
		TestTrue(*(SlotLabel + TEXT(" yaw sine")), FMath::IsNearlyEqual(static_cast<double>(Obstacle.HalfExtentCmYawSine.W), Expected.YawSine, PackTolerance));
	}

	const TArray<FTDFog3DObstacleKey> RepeatedKeys = SelectKeys(Registered);
	TestTrue(TEXT("The same list twice is not a change"), !FTDFog3DLayer::HaveObstaclesChanged(Keys, RepeatedKeys));

	TArray<FTDFluidSdfObstacle> TieSwapped = Registered;
	TieSwapped.Swap(TieSwapFirstIndex, TieSwapSecondIndex);
	TestTrue(TEXT("Swapping the registration order of an equal distance pair is a change"), FTDFog3DLayer::HaveObstaclesChanged(Keys, SelectKeys(TieSwapped)));

	TArray<FTDFluidSdfObstacle> DistinctSwapped = Registered;
	DistinctSwapped.Swap(DistinctSwapFirstIndex, DistinctSwapSecondIndex);
	TestTrue(TEXT("Swapping two boxes of different distances is not a change"), !FTDFog3DLayer::HaveObstaclesChanged(Keys, SelectKeys(DistinctSwapped)));

	return true;
}

#endif
