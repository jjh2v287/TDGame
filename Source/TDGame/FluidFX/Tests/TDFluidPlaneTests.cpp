#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPlaneTypes.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidPlaneTestPrivate
{
	constexpr float StepSeconds = 0.05f;
	constexpr double GeometryTolerance = 1.0e-6;
	constexpr double ScreenHalfHeightTangent = 0.5625;
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const PlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* const BasicMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	FRotator GetPitchMinus60View()
	{
		return FRotator(-60.0, 0.0, 0.0);
	}

	double NextSignedUnit(uint32& InOutState)
	{
		InOutState ^= InOutState << 13;
		InOutState ^= InOutState >> 17;
		InOutState ^= InOutState << 5;
		return static_cast<double>(InOutState) / 2147483648.0 - 1.0;
	}

	FVector NextPoint(uint32& InOutState, double RangeXYCm, double RangeZCm)
	{
		const double X = NextSignedUnit(InOutState) * RangeXYCm;
		const double Y = NextSignedUnit(InOutState) * RangeXYCm;
		const double Z = NextSignedUnit(InOutState) * RangeZCm;
		return FVector(X, Y, Z);
	}

	struct FTDFluidPlaneTestWorld
	{
		FTDFluidPlaneTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidPlaneTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			Subsystem = World->GetSubsystem<UTDFluidLiveSubsystem>();
		}

		~FTDFluidPlaneTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		void TickWorld(float TotalSeconds)
		{
			for (float Remaining = TotalSeconds; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, StepSeconds);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		UWorld* World = nullptr;
		UTDFluidLiveSubsystem* Subsystem = nullptr;
	};

	UTDFluidPresetAsset* MakePlanePreset(UObject* Outer, ETDFluidPlaneOrientation Orientation)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, ETDFluidPresetKind::Custom);
		Preset->SimulationConfig.GridResolution = 64;
		Preset->SimulationConfig.SimExtentCm = FVector2D(800.0, 800.0);
		Preset->SimulationConfig.DensityDissipation = 0.2f;
		Preset->PlaneSettings.Orientation = Orientation;
		Preset->PlaneSettings.InteractionDepthCm = 150.0f;
		return Preset;
	}

	ATDFluidLiveActor* SpawnPlaneActor(FTDFluidPlaneTestWorld& TestWorld, const FVector& Location, UTDFluidPresetAsset* Preset)
	{
		ATDFluidLiveActor* LiveActor = TestWorld.World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (LiveActor)
		{
			LiveActor->InitializeFromPreset(Preset, 0.0f);
		}
		return LiveActor;
	}

	FTDFluidBrushStamp MakeStampAt(const FVector& WorldLocation, float RadiusCm = 60.0f, float DensityStrength = 0.8f)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = WorldLocation;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = DensityStrength;
		return Stamp;
	}

	float SamplePrimaryDensity(const ATDFluidLiveActor* LiveActor, const FVector& WorldLocation)
	{
		FTDFluidCellSample Sample;
		LiveActor->LiveComponent->SampleFluidAtWorldLocation(WorldLocation, Sample);
		return Sample.PrimaryDensity;
	}

	bool AreVectorsNear(const FVector& Left, const FVector& Right, double Tolerance)
	{
		return (Left - Right).GetAbsMax() <= Tolerance;
	}

	double MeasureAngleDegrees(const FQuat& Left, const FQuat& Right)
	{
		return FMath::RadiansToDegrees(Left.AngularDistance(Right));
	}

	double MeasureScreenScale(const FRotator& ViewRotation, const FVector& PlaneAxisV, double NdcY)
	{
		const FRotationMatrix ViewMatrix(ViewRotation);
		const FVector Forward = ViewMatrix.GetUnitAxis(EAxis::X);
		const FVector Up = ViewMatrix.GetUnitAxis(EAxis::Z);
		const FVector RayDirection = (Forward + Up * (NdcY * ScreenHalfHeightTangent)).GetSafeNormal();
		const double AlongRay = FVector::DotProduct(PlaneAxisV, RayDirection);
		return FMath::Sqrt(FMath::Max(0.0, 1.0 - AlongRay * AlongRay));
	}

	bool WaitUntilVolumeVisible(FTDFluidPlaneTestWorld& TestWorld, const ATDFluidLiveActor* LiveActor)
	{
		for (int32 Attempt = 0; Attempt < 200 && !LiveActor->VolumeMeshComponent->IsVisible(); ++Attempt)
		{
			FlushAsyncLoading();
			TestWorld.TickWorld(StepSeconds);
		}
		return LiveActor->VolumeMeshComponent->IsVisible();
	}

	float MeasureMaxTerrainSlope(const ATDFluidLiveActor* LiveActor)
	{
		const FTDFluidGrid2DSolver& Solver = LiveActor->LiveComponent->GetSolver();
		const int32 Resolution = Solver.GetResolution();
		float MaxSlope = 0.0f;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				MaxSlope = FMath::Max(MaxSlope, static_cast<float>(Solver.SampleAtCell(CellX, CellY).TerrainSlope.Size()));
			}
		}
		return MaxSlope;
	}

	AActor* SpawnStaticRamp(UWorld* World, UStaticMesh* Mesh)
	{
		AActor* Block = World->SpawnActor<AActor>();
		if (!Block)
		{
			return nullptr;
		}

		UStaticMeshComponent* BlockMesh = NewObject<UStaticMeshComponent>(Block);
		Block->AddInstanceComponent(BlockMesh);
		Block->SetRootComponent(BlockMesh);
		BlockMesh->SetMobility(EComponentMobility::Static);
		BlockMesh->SetStaticMesh(Mesh);
		BlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BlockMesh->SetCollisionObjectType(ECC_WorldStatic);
		BlockMesh->SetCollisionResponseToAllChannels(ECR_Block);
		BlockMesh->SetWorldTransform(FTransform(FRotator(10.0, 0.0, 0.0), FVector(0.0, 0.0, -100.0), FVector(80.0, 80.0, 1.0)));
		BlockMesh->RegisterComponent();
		return Block;
	}

	AActor* SpawnMovableAnchor(UWorld* World, const FVector& Location)
	{
		AActor* Anchor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Anchor)
		{
			return nullptr;
		}

		USceneComponent* AnchorRoot = NewObject<USceneComponent>(Anchor);
		Anchor->AddInstanceComponent(AnchorRoot);
		Anchor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();
		Anchor->SetActorLocation(Location);
		return Anchor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneWorldFacingIsIdentityTest,
	"TDGame.FluidFX.Plane.WorldFacingIsIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneWorldFacingIsIdentityTest::RunTest(const FString& Parameters)
{
	const FTDFluidPlaneBasis Basis = FTDFluidPlaneBasis::MakeWorldFacing();
	TestTrue(TEXT("World facing basis reports world facing"), Basis.IsWorldFacing());

	const FTDFluidPlaneBasis ViewlessBasis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::WorldFacing, FRotator(-33.0, 71.0, 5.0), 40.0f);
	TestTrue(TEXT("A world facing orientation ignores the view rotation"), ViewlessBasis.IsWorldFacing() && ViewlessBasis.U == Basis.U && ViewlessBasis.V == Basis.V && ViewlessBasis.N == Basis.N);

	uint32 PointState = 2463534242u;
	int32 MismatchCount = 0;
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const FVector Point = TDFluidPlaneTestPrivate::NextPoint(PointState, 100000.0, 100000.0);
		const FVector2D ProjectedPoint = Basis.ProjectPoint(Point);
		const FVector2D ProjectedVector = Basis.ProjectVector(Point);
		const double Depth = Basis.ProjectDepth(Point);
		const FVector2D ExpectedPlane(Point.X, Point.Y);
		const FVector UnprojectedPoint = Basis.UnprojectPoint(ExpectedPlane, Point.Z);
		const FVector UnprojectedVector = Basis.UnprojectVector(ExpectedPlane);
		const FVector ExpectedVector(Point.X, Point.Y, 0.0);

		const bool bIsIdentical = FMemory::Memcmp(&ProjectedPoint, &ExpectedPlane, sizeof(FVector2D)) == 0
			&& FMemory::Memcmp(&ProjectedVector, &ExpectedPlane, sizeof(FVector2D)) == 0
			&& FMemory::Memcmp(&Depth, &Point.Z, sizeof(double)) == 0
			&& FMemory::Memcmp(&UnprojectedPoint, &Point, sizeof(FVector)) == 0
			&& FMemory::Memcmp(&UnprojectedVector, &ExpectedVector, sizeof(FVector)) == 0;
		MismatchCount += bIsIdentical ? 0 : 1;
	}
	TestEqual(TEXT("Projection and unprojection of 1000 points are bitwise identical"), MismatchCount, 0);

	const FQuat Rotation = Basis.GetRotation();
	TestEqual(TEXT("World facing rotation is bitwise identity"), FMemory::Memcmp(&Rotation, &FQuat::Identity, sizeof(FQuat)), 0);
	TestTrue(TEXT("World facing has no plane up direction"), Basis.GetPlaneUpDirection().IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneCameraFacingBasisTest,
	"TDGame.FluidFX.Plane.CameraFacingBasisAtPitchMinus60",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneCameraFacingBasisTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	const FTDFluidPlaneBasis Basis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, GetPitchMinus60View(), 0.0f);
	TestFalse(TEXT("Camera facing is not world facing"), Basis.IsWorldFacing());
	TestTrue(*FString::Printf(TEXT("U is horizontal along the view right (%s)"), *Basis.U.ToString()), AreVectorsNear(Basis.U, FVector(0.0, 1.0, 0.0), 1.0e-9));
	TestTrue(*FString::Printf(TEXT("V is the camera up (%s)"), *Basis.V.ToString()), AreVectorsNear(Basis.V, FVector(0.8660254, 0.0, 0.5), 1.0e-6));
	TestTrue(*FString::Printf(TEXT("N is the camera forward (%s)"), *Basis.N.ToString()), AreVectorsNear(Basis.N, FVector(0.5, 0.0, -0.8660254), 1.0e-6));
	TestNearlyEqual(TEXT("V keeps half of its length along world up"), Basis.V.Z, 0.5, 1.0e-4);

	const FRotator ViewRotations[] = { GetPitchMinus60View(), FRotator(-45.0, 137.0, 0.0), FRotator(-80.0, -33.0, 12.0) };
	for (const FRotator& ViewRotation : ViewRotations)
	{
		const FTDFluidPlaneBasis ViewBasis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, ViewRotation, 0.0f);
		TestNearlyEqual(TEXT("U is a unit vector"), ViewBasis.U.Size(), 1.0, GeometryTolerance);
		TestNearlyEqual(TEXT("V is a unit vector"), ViewBasis.V.Size(), 1.0, GeometryTolerance);
		TestNearlyEqual(TEXT("N is a unit vector"), ViewBasis.N.Size(), 1.0, GeometryTolerance);
		TestNearlyEqual(TEXT("U and V are orthogonal"), FVector::DotProduct(ViewBasis.U, ViewBasis.V), 0.0, GeometryTolerance);
		TestNearlyEqual(TEXT("U and N are orthogonal"), FVector::DotProduct(ViewBasis.U, ViewBasis.N), 0.0, GeometryTolerance);
		TestNearlyEqual(TEXT("V and N are orthogonal"), FVector::DotProduct(ViewBasis.V, ViewBasis.N), 0.0, GeometryTolerance);
		TestNearlyEqual(TEXT("The basis is right handed"), FVector::DotProduct(ViewBasis.U, FVector::CrossProduct(ViewBasis.V, ViewBasis.N)), 1.0, GeometryTolerance);
	}

	const FTDFluidPlaneBasis FlatBasis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(20.0, 0.0, 0.0), 0.0f);
	TestTrue(TEXT("A camera looking upward clamps the lean to zero so V is world up"), AreVectorsNear(FlatBasis.V, FVector(0.0, 0.0, 1.0), GeometryTolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneRotationMatchesAxesTest,
	"TDGame.FluidFX.Plane.RotationMatchesAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneRotationMatchesAxesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	const FRotator ViewRotations[] = { GetPitchMinus60View(), FRotator(-50.0, 25.0, 0.0), FRotator(-35.0, -140.0, 0.0) };
	for (const FRotator& ViewRotation : ViewRotations)
	{
		const FTDFluidPlaneBasis Basis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, ViewRotation, 0.0f);
		const FQuat Rotation = Basis.GetRotation();
		TestTrue(TEXT("Rotated X axis is U"), AreVectorsNear(Rotation.GetAxisX(), Basis.U, GeometryTolerance));
		TestTrue(TEXT("Rotated Y axis is V"), AreVectorsNear(Rotation.GetAxisY(), Basis.V, GeometryTolerance));
		TestTrue(TEXT("Rotated Z axis is N"), AreVectorsNear(Rotation.GetAxisZ(), Basis.N, GeometryTolerance));
	}

	const FTDFluidPlaneBasis Billboard = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::VerticalBillboard, FRotator(-60.0, 90.0, 0.0), 20.0f);
	const FQuat BillboardRotation = Billboard.GetRotation();
	TestTrue(TEXT("Billboard rotated Z axis is N"), AreVectorsNear(BillboardRotation.GetAxisZ(), Billboard.N, GeometryTolerance));
	TestTrue(TEXT("Billboard plane up direction is +V"), Billboard.GetPlaneUpDirection().Equals(FVector2D(0.0, 1.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneBillboardLeanScreenFactorTest,
	"TDGame.FluidFX.Plane.BillboardLeanScreenFactor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneBillboardLeanScreenFactorTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	const FVector CameraUp = FRotationMatrix(GetPitchMinus60View()).GetUnitAxis(EAxis::Z);
	const float LeanDegrees[] = { 0.0f, 30.0f, 60.0f };
	const double ExpectedFactors[] = { 0.5, 0.8660254, 1.0 };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FTDFluidPlaneBasis Basis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::VerticalBillboard, GetPitchMinus60View(), LeanDegrees[Index]);
		const double ScreenFactor = FVector::DotProduct(Basis.V, CameraUp);
		TestNearlyEqual(*FString::Printf(TEXT("Screen factor at lean %.0f"), LeanDegrees[Index]), ScreenFactor, ExpectedFactors[Index], 1.0e-4);
		TestNearlyEqual(*FString::Printf(TEXT("Rise per plane unit at lean %.0f"), LeanDegrees[Index]), Basis.V.Z, FMath::Cos(FMath::DegreesToRadians(static_cast<double>(LeanDegrees[Index]))), GeometryTolerance);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneScreenHeightStabilityTest,
	"TDGame.FluidFX.Plane.ScreenHeightStabilityAcrossViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneScreenHeightStabilityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	const FTDFluidPlaneBasis CameraFacing = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, GetPitchMinus60View(), 0.0f);
	const double ScreenPositions[] = { 1.0, 0.0, -1.0 };
	const TCHAR* const ScreenNames[] = { TEXT("top"), TEXT("center"), TEXT("bottom") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const double Scale = MeasureScreenScale(GetPitchMinus60View(), CameraFacing.V, ScreenPositions[Index]);
		TestTrue(*FString::Printf(TEXT("Camera facing keeps at least 0.85 of the height at the %s (%.3f)"), ScreenNames[Index], Scale), Scale >= 0.85);
	}

	const FTDFluidPlaneBasis Leaned = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::VerticalBillboard, GetPitchMinus60View(), 30.0f);
	AddInfo(FString::Printf(TEXT("Lean 30 screen scale top %.3f center %.3f bottom %.3f"),
		MeasureScreenScale(GetPitchMinus60View(), Leaned.V, 1.0),
		MeasureScreenScale(GetPitchMinus60View(), Leaned.V, 0.0),
		MeasureScreenScale(GetPitchMinus60View(), Leaned.V, -1.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneRoundTripLargeCoordinatesTest,
	"TDGame.FluidFX.Plane.RoundTripLargeCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneRoundTripLargeCoordinatesTest::RunTest(const FString& Parameters)
{
	const FTDFluidPlaneBasis Bases[] = {
		FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(-60.0, 37.0, 0.0), 0.0f),
		FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::VerticalBillboard, FRotator(-60.0, -122.0, 0.0), 25.0f),
		FTDFluidPlaneBasis::MakeWorldFacing(),
	};

	uint32 PointState = 88172645u;
	double WorstErrorCm = 0.0;
	for (const FTDFluidPlaneBasis& Basis : Bases)
	{
		for (int32 Index = 0; Index < 200; ++Index)
		{
			const FVector Point = TDFluidPlaneTestPrivate::NextPoint(PointState, 100000.0, 50000.0);
			const FVector RoundTrip = Basis.UnprojectPoint(Basis.ProjectPoint(Point), Basis.ProjectDepth(Point));
			WorstErrorCm = FMath::Max(WorstErrorCm, (RoundTrip - Point).GetAbsMax());
		}
	}

	TestTrue(*FString::Printf(TEXT("Round trip error stays within 1e-3 cm at 1e5 cm (%.3e)"), WorstErrorCm), WorstErrorCm <= 1.0e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneStampDepthCullingTest,
	"TDGame.FluidFX.LiveComponent.StampDepthCulling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneStampDepthCullingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	FTDFluidPlaneTestWorld TestWorld;
	if (!TestNotNull(TEXT("Fluid subsystem exists"), TestWorld.Subsystem))
	{
		return false;
	}
	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(GetPitchMinus60View());

	UTDFluidPresetAsset* Preset = MakePlanePreset(TestWorld.World, ETDFluidPlaneOrientation::CameraFacing);
	Preset->PlaneSettings.InteractionDepthCm = 100.0f;
	ATDFluidLiveActor* LiveActor = SpawnPlaneActor(TestWorld, FVector::ZeroVector, Preset);
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	LiveActor->StepLiveActor(StepSeconds);
	UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
	const FTDFluidPlaneBasis Basis = LiveComponent->GetPlaneBasis();
	const double PlaneDepth = LiveComponent->GetPlaneDepthCm();
	TestFalse(TEXT("The actor switched the component to a plane basis"), Basis.IsWorldFacing());

	LiveComponent->QueueBrushStamp(MakeStampAt(Basis.UnprojectPoint(FVector2D::ZeroVector, PlaneDepth + 160.0)));
	LiveActor->StepLiveActor(StepSeconds);
	TestEqual(TEXT("A stamp beyond the slab depth is dropped"), LiveComponent->GetLastProcessedPointCount(), 0);
	TestNearlyEqual(TEXT("A dropped stamp leaves no density"), SamplePrimaryDensity(LiveActor, LiveComponent->UnprojectSolverPlaneToWorld(FVector2D::ZeroVector)), 0.0f, 1.0e-6f);

	LiveComponent->QueueBrushStamp(MakeStampAt(Basis.UnprojectPoint(FVector2D::ZeroVector, PlaneDepth + 60.0)));
	LiveComponent->QueueBrushStamp(MakeStampAt(Basis.UnprojectPoint(FVector2D::ZeroVector, PlaneDepth - 60.0)));
	LiveComponent->QueueBrushStamp(MakeStampAt(Basis.UnprojectPoint(FVector2D::ZeroVector, PlaneDepth - 160.0)));
	LiveComponent->QueueBrushStamp(MakeStampAt(Basis.UnprojectPoint(FVector2D::ZeroVector, PlaneDepth + 160.0)));
	LiveActor->StepLiveActor(StepSeconds);
	TestEqual(TEXT("Only the two stamps inside the slab are processed"), LiveComponent->GetLastProcessedPointCount(), 2);
	TestTrue(TEXT("The stamps inside the slab write density"), SamplePrimaryDensity(LiveActor, LiveComponent->UnprojectSolverPlaneToWorld(FVector2D::ZeroVector)) > 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneSampleIsDepthInvariantTest,
	"TDGame.FluidFX.LiveComponent.SampleIsDepthInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneSampleIsDepthInvariantTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	FTDFluidPlaneTestWorld TestWorld;
	if (!TestNotNull(TEXT("Fluid subsystem exists"), TestWorld.Subsystem))
	{
		return false;
	}
	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(GetPitchMinus60View());

	ATDFluidLiveActor* LiveActor = SpawnPlaneActor(TestWorld, FVector::ZeroVector, MakePlanePreset(TestWorld.World, ETDFluidPlaneOrientation::CameraFacing));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	LiveActor->StepLiveActor(StepSeconds);
	UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
	const FTDFluidPlaneBasis Basis = LiveComponent->GetPlaneBasis();
	const FVector PlaneOrigin = LiveComponent->UnprojectSolverPlaneToWorld(FVector2D::ZeroVector);
	LiveComponent->QueueBrushStamp(MakeStampAt(PlaneOrigin));
	LiveActor->StepLiveActor(StepSeconds);

	const float CellSizeCm = LiveComponent->GetSolver().GetExtentCm().X / LiveComponent->GetSolver().GetResolution();
	const float CenterDensity = SamplePrimaryDensity(LiveActor, PlaneOrigin);
	const float DeeperDensity = SamplePrimaryDensity(LiveActor, PlaneOrigin + Basis.N * 50.0);
	const float NearerDensity = SamplePrimaryDensity(LiveActor, PlaneOrigin - Basis.N * 90.0);
	const float SideDensity = SamplePrimaryDensity(LiveActor, PlaneOrigin + Basis.U * (5.0 * CellSizeCm));
	TestTrue(TEXT("The stamped point holds density"), CenterDensity > 0.1f);
	TestNearlyEqual(TEXT("Sampling 50 cm deeper along N returns the same value"), DeeperDensity, CenterDensity, 1.0e-5f);
	TestNearlyEqual(TEXT("Sampling 90 cm nearer along N returns the same value"), NearerDensity, CenterDensity, 1.0e-5f);
	TestTrue(*FString::Printf(TEXT("Sampling five cells along U returns a different value (%.4f vs %.4f)"), SideDensity, CenterDensity), SideDensity < CenterDensity - 0.05f);

	FVector CentroidWorld;
	float DensityAreaCm2 = 0.0f;
	TestTrue(TEXT("The density centroid exists"), LiveComponent->ComputeDensityCentroidWorld(false, CentroidWorld, DensityAreaCm2));
	TestTrue(*FString::Printf(TEXT("The centroid sits on the stamp (%s)"), *CentroidWorld.ToString()), AreVectorsNear(CentroidWorld, PlaneOrigin, 5.0));
	TestTrue(TEXT("The density area is positive"), DensityAreaCm2 > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneComponentsFollowBasisTest,
	"TDGame.FluidFX.LiveActor.PlaneComponentsFollowBasis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneComponentsFollowBasisTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	FTDFluidPlaneTestWorld TestWorld;
	if (!TestNotNull(TEXT("Fluid subsystem exists"), TestWorld.Subsystem))
	{
		return false;
	}
	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(FRotator(-60.0, 30.0, 0.0));

	UTDFluidPresetAsset* Preset = MakePlanePreset(TestWorld.World, ETDFluidPlaneOrientation::CameraFacing);
	Preset->SurfaceMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(PlaneMeshPath));
	Preset->OutputMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(BasicMaterialPath));
	Preset->VolumeMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CubeMeshPath));
	Preset->VolumeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(BasicMaterialPath));
	Preset->VolumeHeightCm = 160.0f;
	Preset->PlaneSettings.CameraDepthBiasCm = 40.0f;
	Preset->TranslucencySortPriority = 3;

	ATDFluidLiveActor* LiveActor = SpawnPlaneActor(TestWorld, FVector(300.0, -200.0, 50.0), Preset);
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor) || !TestTrue(TEXT("Volume mesh becomes visible after the async load"), WaitUntilVolumeVisible(TestWorld, LiveActor)))
	{
		return false;
	}

	LiveActor->StepLiveActor(StepSeconds);
	LiveActor->StepLiveActor(StepSeconds);

	const UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
	const FTDFluidPlaneBasis& Basis = LiveComponent->GetPlaneBasis();
	const FTDFluidGrid2DSolver& Solver = LiveComponent->GetSolver();
	const FVector2D ExtentCm = Solver.GetExtentCm();
	const FVector ExpectedCenter = Basis.UnprojectPoint(Solver.GetCenterWorld(), LiveComponent->GetPlaneDepthCm());
	const FQuat ExpectedRotation = Basis.GetRotation();

	TestNearlyEqual(TEXT("The plane depth includes the camera depth bias"), LiveComponent->GetPlaneDepthCm(), Basis.ProjectDepth(FVector(300.0, -200.0, 50.0)) - 40.0, 1.0e-6);
	TestTrue(TEXT("Interaction box center follows the plane"), AreVectorsNear(LiveActor->InteractionVolume->GetComponentLocation(), ExpectedCenter, 0.05));
	TestTrue(TEXT("Interaction box rotation follows the basis"), MeasureAngleDegrees(LiveActor->InteractionVolume->GetComponentQuat(), ExpectedRotation) < 0.01);
	TestTrue(TEXT("Interaction box has the plane extents and the slab half depth"), AreVectorsNear(LiveActor->InteractionVolume->GetUnscaledBoxExtent(), FVector(ExtentCm.X * 0.5, ExtentCm.Y * 0.5, 150.0), 0.01));

	TestTrue(TEXT("Surface card center follows the plane"), AreVectorsNear(LiveActor->InternalSurfaceMesh->GetComponentLocation(), ExpectedCenter, 0.05));
	TestTrue(TEXT("Surface card rotation follows the basis"), MeasureAngleDegrees(LiveActor->InternalSurfaceMesh->GetComponentQuat(), ExpectedRotation) < 0.01);
	TestTrue(TEXT("Surface card scale covers the plane extents"), AreVectorsNear(LiveActor->InternalSurfaceMesh->GetComponentScale(), FVector(ExtentCm.X / 100.0, ExtentCm.Y / 100.0, 1.0), 1.0e-3));
	TestFalse(TEXT("Surface card casts no shadow"), LiveActor->InternalSurfaceMesh->CastShadow);
	TestFalse(TEXT("Surface card is hidden from ray tracing"), LiveActor->InternalSurfaceMesh->bVisibleInRayTracing);
	TestFalse(TEXT("Surface card skips distance field lighting"), LiveActor->InternalSurfaceMesh->bAffectDistanceFieldLighting);
	TestEqual(TEXT("Surface card uses the preset sort priority"), LiveActor->InternalSurfaceMesh->TranslucencySortPriority, 3);

	const UStaticMeshComponent* VolumeMesh = LiveActor->VolumeMeshComponent;
	const FBoxSphereBounds MeshBounds = VolumeMesh->GetStaticMesh()->GetBounds();
	const FVector MeshSize = MeshBounds.BoxExtent * 2.0;
	const FVector ExpectedScale = FVector(ExtentCm.X, ExtentCm.Y, 160.0) / MeshSize;
	const FVector ExpectedLocation = ExpectedCenter - ExpectedRotation.RotateVector(ExpectedScale * MeshBounds.Origin);
	TestTrue(TEXT("Volume box rotation follows the basis"), MeasureAngleDegrees(VolumeMesh->GetComponentQuat(), ExpectedRotation) < 0.01);
	TestTrue(TEXT("Volume box scale is extents times slab thickness"), AreVectorsNear(VolumeMesh->GetComponentScale(), ExpectedScale, 1.0e-3));
	TestTrue(TEXT("Volume box position is the center minus the rotated pivot offset"), AreVectorsNear(VolumeMesh->GetComponentLocation(), ExpectedLocation, 0.05));
	TestFalse(TEXT("Volume box casts no shadow"), VolumeMesh->CastShadow);
	TestFalse(TEXT("Volume box is hidden from ray tracing"), VolumeMesh->bVisibleInRayTracing);
	TestFalse(TEXT("Volume box skips distance field lighting"), VolumeMesh->bAffectDistanceFieldLighting);
	TestEqual(TEXT("Volume box uses the preset sort priority"), VolumeMesh->TranslucencySortPriority, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneModeDisablesTerrainTest,
	"TDGame.FluidFX.LiveActor.PlaneModeDisablesTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneModeDisablesTerrainTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	FTDFluidPlaneTestWorld TestWorld;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
	if (!TestNotNull(TEXT("Fluid subsystem exists"), TestWorld.Subsystem) || !TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}
	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(GetPitchMinus60View());

	if (!TestNotNull(TEXT("Spawned ramp"), SpawnStaticRamp(TestWorld.World, CubeMesh)))
	{
		return false;
	}
	TestWorld.TickWorld(0.1f);

	UTDFluidPresetAsset* FlatPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	UTDFluidPresetAsset* PlanePreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	PlanePreset->PlaneSettings.Orientation = ETDFluidPlaneOrientation::CameraFacing;
	ATDFluidLiveActor* GroundActor = TestWorld.Subsystem->SpawnPooledFluidActor(FlatPreset, FVector::ZeroVector, 120.0f);
	ATDFluidLiveActor* PlaneActor = TestWorld.Subsystem->SpawnPooledFluidActor(PlanePreset, FVector::ZeroVector, 120.0f);
	if (!TestNotNull(TEXT("Spawned world facing actor"), GroundActor) || !TestNotNull(TEXT("Spawned plane actor"), PlaneActor))
	{
		return false;
	}
	GroundActor->bSampleTerrainElevation = true;
	PlaneActor->bSampleTerrainElevation = true;

	TestWorld.TickWorld(0.05f * 12.0f);

	TestFalse(TEXT("The plane actor runs on a plane basis"), PlaneActor->LiveComponent->GetPlaneBasis().IsWorldFacing());
	TestTrue(TEXT("Control: the world facing actor traces the ramp"), MeasureMaxTerrainSlope(GroundActor) > 0.02f);
	TestNearlyEqual(TEXT("The plane actor never receives a terrain slope"), MeasureMaxTerrainSlope(PlaneActor), 0.0f, 1.0e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneSlidingWindowTest,
	"TDGame.FluidFX.LiveActor.SlidingWindowInPlaneCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneSlidingWindowTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	FTDFluidPlaneTestWorld TestWorld;
	if (!TestNotNull(TEXT("Fluid subsystem exists"), TestWorld.Subsystem))
	{
		return false;
	}
	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(GetPitchMinus60View());

	AActor* Anchor = SpawnMovableAnchor(TestWorld.World, FVector::ZeroVector);
	ATDFluidLiveActor* LiveActor = SpawnPlaneActor(TestWorld, FVector::ZeroVector, MakePlanePreset(TestWorld.World, ETDFluidPlaneOrientation::CameraFacing));
	if (!TestNotNull(TEXT("Spawned anchor"), Anchor) || !TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}
	LiveActor->bShouldFollowPlayerPawn = true;
	LiveActor->FollowTargetActor = Anchor;

	LiveActor->StepLiveActor(StepSeconds);
	UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
	const FTDFluidPlaneBasis Basis = LiveComponent->GetPlaneBasis();
	const FVector StampPoint = LiveComponent->UnprojectSolverPlaneToWorld(FVector2D(20.0, -10.0));
	LiveComponent->QueueBrushStamp(MakeStampAt(StampPoint));
	LiveActor->StepLiveActor(StepSeconds);

	const float DensityBefore = SamplePrimaryDensity(LiveActor, StampPoint);
	const FVector2D CenterBefore = LiveComponent->GetSolver().GetCenterWorld();
	const double CellSizeCm = LiveComponent->GetSolver().GetExtentCm().X / LiveComponent->GetSolver().GetResolution();
	TestTrue(TEXT("The stamp wrote density before the window moved"), DensityBefore > 0.1f);

	const FVector AnchorTarget = Basis.UnprojectVector(FVector2D(2.4 * CellSizeCm, -1.3 * CellSizeCm)) + Basis.N * 30.0;
	Anchor->SetActorLocation(AnchorTarget);
	LiveActor->BeginLiveActorStep(StepSeconds);

	const FVector2D CenterShift = LiveComponent->GetSolver().GetCenterWorld() - CenterBefore;
	TestNearlyEqual(TEXT("The window moved two cells along U"), CenterShift.X / CellSizeCm, 2.0, 1.0e-4);
	TestNearlyEqual(TEXT("The window moved minus one cell along V"), CenterShift.Y / CellSizeCm, -1.0, 1.0e-4);
	TestNearlyEqual(TEXT("The slab depth follows the anchor without touching the plane coordinates"), LiveComponent->GetPlaneDepthCm(), 30.0, 1.0e-6);
	TestNearlyEqual(TEXT("A fixed world point keeps its sample after the shift"), SamplePrimaryDensity(LiveActor, StampPoint), DensityBefore, 1.0e-5f);
	TestTrue(TEXT("The actor follows the anchor"), AreVectorsNear(LiveActor->GetActorLocation(), AnchorTarget, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneMouseRayIntersectionTest,
	"TDGame.FluidFX.LiveActor.MouseRayPlaneIntersection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneMouseRayIntersectionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	const FTDFluidPlaneBasis Basis = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(-60.0, 20.0, 0.0), 0.0f);
	const FVector CameraPosition(-300.0, 50.0, 500.0);
	const double PlaneDepth = Basis.ProjectDepth(FVector(100.0, 100.0, 100.0));
	const FVector Target = Basis.UnprojectPoint(FVector2D(120.0, 40.0), PlaneDepth);

	FVector HitPoint;
	TestTrue(TEXT("A ray toward the plane hits it"), Basis.IntersectRay(CameraPosition, (Target - CameraPosition).GetSafeNormal(), PlaneDepth, HitPoint));
	TestTrue(*FString::Printf(TEXT("The hit lands on the target (%s)"), *HitPoint.ToString()), AreVectorsNear(HitPoint, Target, 1.0e-6));
	TestNearlyEqual(TEXT("The hit lies at the plane depth"), Basis.ProjectDepth(HitPoint), PlaneDepth, 1.0e-6);

	FVector Unchanged = FVector(7.0, 8.0, 9.0);
	TestFalse(TEXT("A ray parallel to the plane never hits"), Basis.IntersectRay(CameraPosition, Basis.U, PlaneDepth, Unchanged));
	TestFalse(TEXT("A ray pointing away from the plane never hits"), Basis.IntersectRay(CameraPosition, (CameraPosition - Target).GetSafeNormal(), PlaneDepth, Unchanged));

	FTDFluidPlaneTestWorld TestWorld;
	if (!TestNotNull(TEXT("Fluid subsystem exists"), TestWorld.Subsystem))
	{
		return false;
	}
	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(GetPitchMinus60View());

	ATDFluidLiveActor* LiveActor = SpawnPlaneActor(TestWorld, FVector::ZeroVector, MakePlanePreset(TestWorld.World, ETDFluidPlaneOrientation::CameraFacing));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	LiveActor->StepLiveActor(StepSeconds);
	UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
	const FTDFluidPlaneBasis& LiveBasis = LiveComponent->GetPlaneBasis();
	const double LiveDepth = LiveComponent->GetPlaneDepthCm();

	LiveActor->QueueMouseBrushAt(LiveBasis.UnprojectPoint(FVector2D(0.0, 0.0), LiveDepth), StepSeconds);
	LiveActor->QueueMouseBrushAt(LiveBasis.UnprojectPoint(FVector2D(20.0, 0.0), LiveDepth), StepSeconds);
	LiveActor->StepLiveActor(StepSeconds);
	TestEqual(TEXT("A mouse stroke on the plane queues one stamp"), LiveComponent->GetLastProcessedPointCount(), 1);
	TestTrue(TEXT("The mouse stamp writes density where the cursor is"), SamplePrimaryDensity(LiveActor, LiveBasis.UnprojectPoint(FVector2D(20.0, 0.0), LiveDepth)) > 0.05f);

	LiveActor->QueueMouseBrushAt(LiveBasis.UnprojectPoint(FVector2D(0.0, 0.0), LiveDepth + 400.0), StepSeconds);
	LiveActor->QueueMouseBrushAt(LiveBasis.UnprojectPoint(FVector2D(20.0, 0.0), LiveDepth + 400.0), StepSeconds);
	LiveActor->StepLiveActor(StepSeconds);
	TestEqual(TEXT("A cursor point outside the slab queues nothing"), LiveComponent->GetLastProcessedPointCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneCombatStampRoutingTest,
	"TDGame.FluidFX.Subsystem.CombatStampRoutesPerActorBasis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneCombatStampRoutingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	FTDFluidPlaneTestWorld TestWorld;
	if (!TestNotNull(TEXT("Fluid subsystem exists"), TestWorld.Subsystem))
	{
		return false;
	}
	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(GetPitchMinus60View());

	ATDFluidLiveActor* GroundActor = SpawnPlaneActor(TestWorld, FVector::ZeroVector, MakePlanePreset(TestWorld.World, ETDFluidPlaneOrientation::WorldFacing));
	ATDFluidLiveActor* PlaneActor = SpawnPlaneActor(TestWorld, FVector::ZeroVector, MakePlanePreset(TestWorld.World, ETDFluidPlaneOrientation::CameraFacing));
	if (!TestNotNull(TEXT("Spawned world facing actor"), GroundActor) || !TestNotNull(TEXT("Spawned plane actor"), PlaneActor))
	{
		return false;
	}
	TestWorld.TickWorld(StepSeconds);
	TestFalse(TEXT("The second actor runs on a plane basis"), PlaneActor->LiveComponent->GetPlaneBasis().IsWorldFacing());
	TestTrue(TEXT("The first actor stays world facing"), GroundActor->LiveComponent->GetPlaneBasis().IsWorldFacing());

	const FVector InsideBothPoint(100.0, 0.0, 0.0);
	const FVector WorldOnlyPoint(300.0, 0.0, 600.0);
	const FVector OutsideSlabPoint(0.0, 0.0, 400.0);
	const FVector OutsideBothPoint(0.0, 700.0, 0.0);
	TestEqual(TEXT("A point inside both windows reaches both actors"), TestWorld.Subsystem->InjectCombatBrushStamp(MakeStampAt(InsideBothPoint, 40.0f)), 2);
	TestEqual(TEXT("A point above the plane window reaches only the world facing actor"), TestWorld.Subsystem->InjectCombatBrushStamp(MakeStampAt(WorldOnlyPoint, 40.0f)), 1);
	TestEqual(TEXT("A point outside the plane slab reaches only the world facing actor"), TestWorld.Subsystem->InjectCombatBrushStamp(MakeStampAt(OutsideSlabPoint, 40.0f)), 1);
	TestEqual(TEXT("A point outside both windows reaches nobody"), TestWorld.Subsystem->InjectCombatBrushStamp(MakeStampAt(OutsideBothPoint, 40.0f)), 0);

	const FVector PlaneAxisU = PlaneActor->LiveComponent->GetPlaneBasis().U;
	TestWorld.TickWorld(StepSeconds);
	TestTrue(TEXT("The world facing actor holds density at the stamp"), SamplePrimaryDensity(GroundActor, InsideBothPoint) > 0.1f);
	TestTrue(TEXT("The plane actor holds density at the stamp"), SamplePrimaryDensity(PlaneActor, InsideBothPoint) > 0.1f);
	TestNearlyEqual(TEXT("The plane actor did not use the world axes for the stamp"), SamplePrimaryDensity(PlaneActor, PlaneAxisU * 100.0), 0.0f, 1.0e-3f);
	TestTrue(TEXT("The world facing actor holds density at the upper stamp"), SamplePrimaryDensity(GroundActor, WorldOnlyPoint) > 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPlaneSmallViewChangeTest,
	"TDGame.FluidFX.Subsystem.SmallViewChangeKeepsSolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPlaneSmallViewChangeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPlaneTestPrivate;

	FTDFluidPlaneTestWorld TestWorld;
	if (!TestNotNull(TEXT("Fluid subsystem exists"), TestWorld.Subsystem))
	{
		return false;
	}
	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(GetPitchMinus60View());

	ATDFluidLiveActor* LiveActor = SpawnPlaneActor(TestWorld, FVector::ZeroVector, MakePlanePreset(TestWorld.World, ETDFluidPlaneOrientation::CameraFacing));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	LiveActor->StepLiveActor(StepSeconds);
	UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
	LiveComponent->QueueBrushStamp(MakeStampAt(LiveComponent->UnprojectSolverPlaneToWorld(FVector2D::ZeroVector)));
	LiveActor->StepLiveActor(StepSeconds);
	LiveActor->StepLiveActor(StepSeconds);

	const uint32 ChecksumBefore = LiveComponent->GetSolver().ComputeStateChecksum();
	const int32 GenerationBefore = LiveComponent->GetSolver().GetReinitializationGeneration();
	TestTrue(TEXT("The solver holds density before the view changes"), SamplePrimaryDensity(LiveActor, LiveComponent->UnprojectSolverPlaneToWorld(FVector2D::ZeroVector)) > 0.1f);

	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(FRotator(-60.8, 0.9, 0.7));
	LiveActor->BeginLiveActorStep(StepSeconds);
	TestEqual(TEXT("A shake sized view change keeps the solver generation"), LiveComponent->GetSolver().GetReinitializationGeneration(), GenerationBefore);
	TestEqual(TEXT("A shake sized view change keeps the solver state checksum"), LiveComponent->GetSolver().ComputeStateChecksum(), ChecksumBefore);

	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(FRotator(-45.0, 25.0, 0.0));
	LiveActor->BeginLiveActorStep(StepSeconds);
	TestEqual(TEXT("A large view change reinitializes the solver once"), LiveComponent->GetSolver().GetReinitializationGeneration(), GenerationBefore + 1);
	TestTrue(TEXT("A large view change clears the state"), LiveComponent->GetSolver().ComputeStateChecksum() != ChecksumBefore);
	TestNearlyEqual(TEXT("The cleared solver holds no density"), SamplePrimaryDensity(LiveActor, LiveComponent->UnprojectSolverPlaneToWorld(FVector2D::ZeroVector)), 0.0f, 1.0e-6f);
	return true;
}

#endif
