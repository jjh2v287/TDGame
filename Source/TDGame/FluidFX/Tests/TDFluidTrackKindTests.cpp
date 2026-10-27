#include "Components/StaticMeshComponent.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidInputBrushRules.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTrackDeformMeshActor.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidTrackKindTestPrivate
{
	const ETDFluidPresetKind TrackPainterKinds[] = { ETDFluidPresetKind::SnowTrack, ETDFluidPresetKind::SandTrack };
	const ETDFluidPresetKind TrackDustKinds[] = { ETDFluidPresetKind::SnowDust, ETDFluidPresetKind::SandDust };

	FTDFluidSimulationConfig MakeTrackPainterConfig(int32 Resolution, float ExtentCm)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::SimplePainter;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(ExtentCm, ExtentCm);
		Config.DensityDissipation = 0.0f;
		Config.VelocityDamping = 1.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 0.0f;
		Config.BorderFadeRatio = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		Config.MaxPointsPerTick = 512;
		return Config;
	}

	FTDFluidBrushStamp MakeTrackStamp(const FVector& Location, float RadiusCm)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Location;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 0.0f;
		return Stamp;
	}

	float SampleDensity(const TArray<FFloat16Color>& Pixels, int32 Resolution, int32 CellX, int32 CellY)
	{
		return Pixels[CellY * Resolution + CellX].R.GetFloat();
	}

	TArray<FFloat16Color> PackDensity(const FTDFluidGrid2DSolver& Solver)
	{
		TArray<FFloat16Color> DensityPixels;
		TArray<FFloat16Color> VelocityPixels;
		Solver.PackOutputPixels(DensityPixels, VelocityPixels);
		return DensityPixels;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackKindsBuildersAndFamiliesTest,
	"TDGame.FluidFX.Track.Kinds.BuildersAndFamilies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackKindsBuildersAndFamiliesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackKindTestPrivate;

	for (const ETDFluidPresetKind Kind : TrackPainterKinds)
	{
		const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		TestFalse(TEXT("Track painter kinds are not fire family"), UTDFluidPresetAsset::IsFireFamilyPresetKind(Kind));
		TestTrue(TEXT("Track painter kinds use the painter mode"), Config.SimMode == ETDFluidSimMode::SimplePainter);
		TestTrue(TEXT("Track painter kinds run on the GPU backend"), Config.SolverBackend == ETDFluidSolverBackend::Gpu);
		TestEqual(TEXT("Track painter kinds use 1024 cells"), Config.GridResolution, 1024);
		const UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), Kind);
		TestTrue(TEXT("Track painter kinds write the R channel"), Preset->DefaultChannel == ETDFluidBrushChannel::ChannelR_Mud);
	}
	for (const ETDFluidPresetKind Kind : TrackDustKinds)
	{
		const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		TestFalse(TEXT("Track dust kinds are not fire family"), UTDFluidPresetAsset::IsFireFamilyPresetKind(Kind));
		TestTrue(TEXT("Track dust kinds run the fluid solver"), Config.SimMode == ETDFluidSimMode::FluidSim);
		const UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), Kind);
		TestTrue(TEXT("Track dust kinds write the R channel"), Preset->DefaultChannel == ETDFluidBrushChannel::ChannelR_Mud);
	}
	TestTrue(TEXT("Flamethrower stays in the fire family"), UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind::Flamethrower));
	TestFalse(TEXT("Footprint painter keeps the deform mesh off"), UTDFluidPresetAsset::BuildSurfaceRenderForPresetKind(ETDFluidPresetKind::FootprintPainter).bUsesTrackDeformMesh);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackStampEllipseAxesAndHardnessTest,
	"TDGame.FluidFX.Track.Stamp.EllipseAxesAndHardness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackStampEllipseAxesAndHardnessTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackKindTestPrivate;

	constexpr int32 Resolution = 128;
	constexpr float ExtentCm = 1280.0f;
	constexpr float RadiusCm = 120.0f;
	const FTDFluidSimulationConfig Config = MakeTrackPainterConfig(Resolution, ExtentCm);

	FTDFluidGrid2DSolver EllipseSolver;
	EllipseSolver.Initialize(Config, FVector2D::ZeroVector);
	FTDFluidBrushStamp EllipseStamp = MakeTrackStamp(FVector(5.0, 5.0, 0.0), RadiusCm);
	EllipseStamp.ShapeAspectRatio = 3.0f;
	EllipseStamp.ShapeMajorAxis = FVector(1.0, 0.0, 0.0);
	EllipseSolver.ApplyBrushStamps({ EllipseStamp }, 1.0f / 30.0f);
	const TArray<FFloat16Color> EllipsePixels = PackDensity(EllipseSolver);
	int32 LengthCells = 0;
	int32 WidthCells = 0;
	for (int32 Cell = 0; Cell < Resolution; ++Cell)
	{
		LengthCells += SampleDensity(EllipsePixels, Resolution, Cell, Resolution / 2) > 0.0f ? 1 : 0;
		WidthCells += SampleDensity(EllipsePixels, Resolution, Resolution / 2, Cell) > 0.0f ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("Aspect 3 gives length/width near 3 (%d/%d)"), LengthCells, WidthCells), FMath::Abs(LengthCells - 3 * WidthCells) <= 3);

	FTDFluidGrid2DSolver HardSolver;
	HardSolver.Initialize(Config, FVector2D::ZeroVector);
	FTDFluidBrushStamp HardStamp = MakeTrackStamp(FVector(5.0, 5.0, 0.0), RadiusCm);
	HardStamp.ShapeHardness = 0.8f;
	HardSolver.ApplyBrushStamps({ HardStamp }, 1.0f / 30.0f);
	const TArray<FFloat16Color> HardPixels = PackDensity(HardSolver);
	const int32 HalfRadiusCells = FMath::FloorToInt(0.5f * RadiusCm / (ExtentCm / Resolution));
	float MinInnerDensity = 1.0f;
	for (int32 Offset = -HalfRadiusCells + 1; Offset < HalfRadiusCells; ++Offset)
	{
		MinInnerDensity = FMath::Min(MinInnerDensity, SampleDensity(HardPixels, Resolution, Resolution / 2 + Offset, Resolution / 2));
	}
	TestTrue(FString::Printf(TEXT("Hardness 0.8 keeps the inner half at least 0.95 (%.4f)"), MinInnerDensity), MinInnerDensity >= 0.95f);

	FTDFluidGrid2DSolver DefaultSolver;
	DefaultSolver.Initialize(Config, FVector2D::ZeroVector);
	FTDFluidGrid2DSolver ExplicitSolver;
	ExplicitSolver.Initialize(Config, FVector2D::ZeroVector);
	FTDFluidBrushStamp MovingStamp = MakeTrackStamp(FVector(37.0, -21.0, 0.0), 90.0f);
	MovingStamp.WorldVelocity = FVector(300.0, 120.0, 0.0);
	MovingStamp.VelocityStrength = 1.0f;
	FTDFluidBrushStamp ExplicitDefaultStamp = MovingStamp;
	ExplicitDefaultStamp.ShapeAspectRatio = 1.0f;
	ExplicitDefaultStamp.ShapeHardness = 0.0f;
	ExplicitDefaultStamp.ShapeMajorAxis = FVector(0.0, 1.0, 0.0);
	DefaultSolver.ApplyBrushStamps({ MovingStamp }, 1.0f / 30.0f);
	ExplicitSolver.ApplyBrushStamps({ ExplicitDefaultStamp }, 1.0f / 30.0f);
	TestTrue(TEXT("Default shape keeps the round stamp checksum"), ExplicitSolver.ComputeStateChecksum() == DefaultSolver.ComputeStateChecksum());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackBankCpuProfileTest,
	"TDGame.FluidFX.Track.Bank.CpuProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackBankCpuProfileTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackKindTestPrivate;

	constexpr int32 Resolution = 256;
	constexpr float ExtentCm = 1250.0f;
	constexpr float RadiusCm = 15.0f;
	const float CellSizeCm = ExtentCm / Resolution;
	FTDFluidSimulationConfig Config = MakeTrackPainterConfig(Resolution, ExtentCm);

	TArray<FTDFluidBrushStamp> LineStamps;
	for (float X = -400.0f; X <= 400.0f; X += 4.0f)
	{
		LineStamps.Add(MakeTrackStamp(FVector(X, 0.5 * CellSizeCm, 0.0), RadiusCm));
	}

	FTDFluidGrid2DSolver FlatSolver;
	FlatSolver.Initialize(Config, FVector2D::ZeroVector);
	FlatSolver.ApplyBrushStamps(LineStamps, 1.0f / 30.0f);
	const TArray<FFloat16Color> FlatPixels = PackDensity(FlatSolver);

	FTDFluidSimulationConfig ZeroBankConfig = Config;
	ZeroBankConfig.TrackBankStrength = 0.0f;
	ZeroBankConfig.TrackBankBlurRadiusCm = 30.0f;
	FTDFluidGrid2DSolver ZeroBankSolver;
	ZeroBankSolver.Initialize(ZeroBankConfig, FVector2D::ZeroVector);
	ZeroBankSolver.ApplyBrushStamps(LineStamps, 1.0f / 30.0f);
	TestTrue(TEXT("Bank strength 0 keeps the packed bytes"), PackDensity(ZeroBankSolver) == FlatPixels);

	Config.TrackBankStrength = 1.0f;
	Config.TrackBankBlurRadiusCm = RadiusCm;
	Config.TrackBankHeightLimit = 0.5f;
	FTDFluidGrid2DSolver BankSolver;
	BankSolver.Initialize(Config, FVector2D::ZeroVector);
	BankSolver.ApplyBrushStamps(LineStamps, 1.0f / 30.0f);
	const TArray<FFloat16Color> BankPixels = PackDensity(BankSolver);

	const int32 CenterX = Resolution / 2;
	const int32 CenterY = Resolution / 2;
	int32 GrooveCells = 0;
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		GrooveCells += SampleDensity(BankPixels, Resolution, CenterX, Y) > 0.0f ? 1 : 0;
	}
	const float GrooveWidthCm = GrooveCells * CellSizeCm;
	TestTrue(FString::Printf(TEXT("Covered groove width is 25..40 cm (%.2f)"), GrooveWidthCm), GrooveWidthCm >= 25.0f && GrooveWidthCm <= 40.0f);

	float WorstAsymmetry = 0.0f;
	float PeakBank = 0.0f;
	for (int32 Offset = 1; Offset < 16; ++Offset)
	{
		const float Upper = BankPixels[(CenterY + Offset) * Resolution + CenterX].G.GetFloat();
		const float Lower = BankPixels[(CenterY - Offset) * Resolution + CenterX].G.GetFloat();
		const float UpperDensity = SampleDensity(BankPixels, Resolution, CenterX, CenterY + Offset);
		PeakBank = FMath::Max(PeakBank, 2.0f * Upper - 1.0f + UpperDensity);
		if (Offset >= 1)
		{
			const float LowerDensity = SampleDensity(BankPixels, Resolution, CenterX, CenterY - Offset);
			WorstAsymmetry = FMath::Max(WorstAsymmetry, FMath::Abs((2.0f * Upper - 1.0f + UpperDensity) - (2.0f * Lower - 1.0f + LowerDensity)));
		}
	}
	TestTrue(FString::Printf(TEXT("Bank rises beside the groove (%.4f)"), PeakBank), PeakBank > 0.0f);
	TestTrue(FString::Printf(TEXT("Bank profile is near symmetric (%.5f)"), WorstAsymmetry), WorstAsymmetry <= 2.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackInputHeightFadeCurveTest,
	"TDGame.FluidFX.Track.Input.HeightFadeCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackInputHeightFadeCurveTest::RunTest(const FString& Parameters)
{
	FTDFluidInputBrushSettings Settings;
	Settings.BrushHeightFadeDistanceCm = 100.0f;
	const FTDFluidPlaneBasis PlaneBasis = FTDFluidPlaneBasis::MakeWorldFacing();
	const double Heights[] = { 0.0, 25.0, 50.0, 100.0 };
	const float Expected[] = { 1.0f, 0.75f, 0.5f, 0.0f };
	constexpr double GroundElevationCm = 230.0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Heights); ++Index)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		FTDFluidBrushStamp& Stamp = Stamps.AddDefaulted_GetRef();
		Stamp.WorldLocation = FVector(0.0, 0.0, GroundElevationCm + Heights[Index]);
		Stamp.bHasGroundElevation = true;
		Stamp.GroundElevationCm = GroundElevationCm;
		FTDFluidInputBrushRules::ApplyToStamps(Stamps, Settings, PlaneBasis, 0.0);
		if (Expected[Index] <= 0.0f)
		{
			TestEqual(TEXT("Fade distance removes the stamp"), Stamps.Num(), 0);
			continue;
		}
		TestEqual(TEXT("Stamp kept"), Stamps.Num(), 1);
		if (Stamps.Num() == 1)
		{
			TestNearlyEqual(FString::Printf(TEXT("Fade at %.0f cm"), Heights[Index]), Stamps[0].DensityStrength, Expected[Index], 0.02f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackInputStrokeNoGapsTest,
	"TDGame.FluidFX.Track.Input.StrokeNoGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackInputStrokeNoGapsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackKindTestPrivate;

	constexpr float RadiusCm = 15.0f;
	constexpr float SpacingRatio = 0.5f;
	const double SegmentCm = 833.0 / 30.0;
	const int32 StampCount = ATDFluidLiveActor::ComputeStrokeStampCount(SegmentCm, RadiusCm, SpacingRatio);
	TestEqual(TEXT("833 cm/s at 30 Hz needs 4 stamps"), StampCount, 4);
	TestEqual(TEXT("Stroke count caps at 16"), ATDFluidLiveActor::ComputeStrokeStampCount(10000.0, RadiusCm, SpacingRatio), 16);

	constexpr int32 Resolution = 128;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeTrackPainterConfig(Resolution, 640.0f), FVector2D::ZeroVector);
	TArray<FTDFluidBrushStamp> Stamps;
	for (int32 Step = 0; Step <= 6; ++Step)
	{
		const double StartX = -100.0 + Step * SegmentCm;
		for (int32 StampIndex = 1; StampIndex <= StampCount; ++StampIndex)
		{
			Stamps.Add(MakeTrackStamp(FVector(StartX + SegmentCm * StampIndex / StampCount, 2.5, 0.0), RadiusCm));
		}
	}
	Solver.ApplyBrushStamps(Stamps, 1.0f / 30.0f);
	const TArray<FFloat16Color> Pixels = PackDensity(Solver);
	float MinDensity = 1.0f;
	float MaxDensity = 0.0f;
	for (int32 X = Resolution / 2 - 10; X <= Resolution / 2 + 10; ++X)
	{
		const float Density = SampleDensity(Pixels, Resolution, X, Resolution / 2);
		MinDensity = FMath::Min(MinDensity, Density);
		MaxDensity = FMath::Max(MaxDensity, Density);
	}
	TestTrue(FString::Printf(TEXT("Centerline minimum stays above half the maximum (%.3f / %.3f)"), MinDensity, MaxDensity), MinDensity >= 0.5f * MaxDensity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackDeformMeshSnapPolicyActivationTest,
	"TDGame.FluidFX.Track.DeformMesh.SnapPolicyActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackDeformMeshSnapPolicyActivationTest::RunTest(const FString& Parameters)
{
	const double SpacingCm = ATDFluidTrackDeformMeshActor::ComputeGridSpacingCm(2500.0, 257 * 257);
	TestNearlyEqual(TEXT("257 squared vertices give 256 cells"), SpacingCm, 2500.0 / 256.0, 1.0e-9);
	const FVector2D Snapped = ATDFluidTrackDeformMeshActor::SnapDeformCenterCm(FVector2D(1234.5, -987.6), SpacingCm);
	TestNearlyEqual(TEXT("Snapped X is a spacing multiple"), FMath::Frac(FMath::Abs(Snapped.X / SpacingCm) + 0.5), 0.5, 1.0e-6);
	TestNearlyEqual(TEXT("Snapped Y is a spacing multiple"), FMath::Frac(FMath::Abs(Snapped.Y / SpacingCm) + 0.5), 0.5, 1.0e-6);
	TestTrue(TEXT("First placement always recenters"), ATDFluidTrackDeformMeshActor::ShouldRecenterDeformMesh(FVector2D::ZeroVector, Snapped, false, 50.0));
	TestFalse(TEXT("Moves under the threshold stay"), ATDFluidTrackDeformMeshActor::ShouldRecenterDeformMesh(Snapped, Snapped + FVector2D(SpacingCm, 0.0), true, 50.0));
	TestTrue(TEXT("Moves at the threshold recenter"), ATDFluidTrackDeformMeshActor::ShouldRecenterDeformMesh(Snapped, Snapped + FVector2D(6.0 * SpacingCm, 0.0), true, 50.0));

	TestFalse(TEXT("Console 0 hides"), ATDFluidTrackDeformMeshActor::ResolveDeformMeshEnabled(true, 0));
	TestTrue(TEXT("Console 1 forces on"), ATDFluidTrackDeformMeshActor::ResolveDeformMeshEnabled(false, 1));
	TestFalse(TEXT("Preset false hides"), ATDFluidTrackDeformMeshActor::ResolveDeformMeshEnabled(false, -1));
	TestTrue(TEXT("Preset true shows"), ATDFluidTrackDeformMeshActor::ResolveDeformMeshEnabled(true, -1));

	UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage());
	ATDFluidTrackDeformMeshActor::ApplyShadowPolicy(*MeshComponent, ETDFluidTrackDeformShadowPolicy::NoShadow);
	TestFalse(TEXT("NoShadow drops dynamic shadows"), static_cast<bool>(MeshComponent->bCastDynamicShadow));
	TestTrue(TEXT("NoShadow keeps contact shadows"), static_cast<bool>(MeshComponent->bCastContactShadow));
	ATDFluidTrackDeformMeshActor::ApplyShadowPolicy(*MeshComponent, ETDFluidTrackDeformShadowPolicy::Rigid);
	TestTrue(TEXT("Rigid casts dynamic shadows"), static_cast<bool>(MeshComponent->bCastDynamicShadow));
	TestTrue(TEXT("Rigid cache behavior"), MeshComponent->ShadowCacheInvalidationBehavior == EShadowCacheInvalidationBehavior::Rigid);
	ATDFluidTrackDeformMeshActor::ApplyShadowPolicy(*MeshComponent, ETDFluidTrackDeformShadowPolicy::Auto);
	TestTrue(TEXT("Auto cache behavior"), MeshComponent->ShadowCacheInvalidationBehavior == EShadowCacheInvalidationBehavior::Auto);
	return true;
}

#endif
