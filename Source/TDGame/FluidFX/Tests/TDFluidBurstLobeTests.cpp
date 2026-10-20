#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidFragmentEmitterActor.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"

namespace TDFluidBurstLobeTestPrivate
{
	constexpr double LocationToleranceCm = 0.01;
	constexpr double SpeedToleranceCmPerSec = 0.01;
	constexpr double RatioTolerance = 1.0e-4;
	constexpr double AngleToleranceDegrees = 1.0e-3;
	constexpr float VisibleStampRadiusRatio = 0.6f;
	constexpr int32 SilhouetteBinCount = 72;
	constexpr int32 SilhouetteSmoothingHalfWidthBins = 2;
	constexpr int32 SilhouettePeakSpanBins = 3;
	constexpr double SilhouettePeakProminenceRatio = 0.08;
	constexpr int32 MinExplosionSilhouettePeakCount = 4;
	constexpr double MinExplosionSilhouetteVariation = 0.10;
	constexpr double MaxExplosionSilhouetteVariation = 0.20;
	constexpr double FirstLobeSectorStartDegrees = -90.0;
	constexpr int32 ExplosionLobeCount = 6;
	constexpr float LobedExplosionCoreRadiusCm = 160.0f;
	const FVector StageBurstCenterWorld(-55.0, 3200.0, 175.0);
	const FVector SampleBurstCentersWorld[] = {
		FVector(-55.0, 3200.0, 175.0),
		FVector(0.0, 0.0, 175.0),
		FVector(0.0, -180.0, 0.0),
		FVector(400.0, -250.0, 175.0),
		FVector(-1234.0, 567.0, 190.0),
		FVector(2500.0, 3200.0, 175.0),
		FVector(-55.0, 3337.0, 175.0),
		FVector(10000.0, -8000.0, 300.0),
	};

	struct FTDBurstSilhouette
	{
		int32 PeakCount = 0;
		double Variation = 0.0;
	};

	FTDFluidPlaneBasis MakeStageCameraBasis()
	{
		return FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(0.0, 90.0, 0.0), 0.0f);
	}

	FTDFluidFragmentTrailSettings MakeLobedExplosionTrailSettings()
	{
		FTDFluidFragmentTrailSettings Settings = UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion);
		Settings.BurstCoreRadiusCm = LobedExplosionCoreRadiusCm;
		Settings.BurstLobeCount = ExplosionLobeCount;
		return Settings;
	}

	bool AreStampsIdentical(const FTDFluidBrushStamp& Left, const FTDFluidBrushStamp& Right)
	{
		return Left.WorldLocation == Right.WorldLocation
			&& Left.WorldVelocity == Right.WorldVelocity
			&& Left.RadiusCm == Right.RadiusCm
			&& Left.DensityStrength == Right.DensityStrength
			&& Left.VelocityStrength == Right.VelocityStrength
			&& Left.RadialVelocityCmPerSec == Right.RadialVelocityCmPerSec
			&& Left.Channel == Right.Channel
			&& Left.RippleImpulseCm == Right.RippleImpulseCm
			&& Left.HeatStrength == Right.HeatStrength;
	}

	bool AreStampListsIdentical(const TArray<FTDFluidBrushStamp>& Left, const TArray<FTDFluidBrushStamp>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}

		for (int32 StampIndex = 0; StampIndex < Left.Num(); ++StampIndex)
		{
			if (!AreStampsIdentical(Left[StampIndex], Right[StampIndex]))
			{
				return false;
			}
		}
		return true;
	}

	TArray<FTDFluidBrushStamp> BuildExplosionFireBurst(const FVector& CenterWorld, const FTDFluidPlaneBasis& Basis, const FTDFluidFragmentTrailSettings& Settings, float RadiusScale = 1.0f)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		ATDFluidFragmentEmitterActor::BuildBurstStamps(CenterWorld, Basis, Settings, ETDFluidBrushChannel::ChannelB_Fire, RadiusScale, Settings.FireDensityStrength, Settings.FireHeatStrength, Stamps);
		return Stamps;
	}

	double ComputeStampReachAlongDirection(const FTDFluidBrushStamp& Stamp, const FVector& CenterWorld, const FTDFluidPlaneBasis& Basis, const FVector2D& Direction)
	{
		const FVector2D Offset = Basis.ProjectVector(Stamp.WorldLocation - CenterWorld);
		const double VisibleRadiusCm = VisibleStampRadiusRatio * Stamp.RadiusCm;
		const double AlongCm = FVector2D::DotProduct(Offset, Direction);
		const double AcrossSquaredCm2 = Offset.SizeSquared() - AlongCm * AlongCm;
		if (AcrossSquaredCm2 > VisibleRadiusCm * VisibleRadiusCm)
		{
			return 0.0;
		}
		return AlongCm + FMath::Sqrt(FMath::Max(VisibleRadiusCm * VisibleRadiusCm - AcrossSquaredCm2, 0.0));
	}

	FTDBurstSilhouette MeasureBurstSilhouette(const TArray<FTDFluidBrushStamp>& Stamps, const FVector& CenterWorld, const FTDFluidPlaneBasis& Basis)
	{
		TArray<double> ReachByBin;
		ReachByBin.Init(0.0, SilhouetteBinCount);
		for (int32 BinIndex = 0; BinIndex < SilhouetteBinCount; ++BinIndex)
		{
			const double AngleRadians = FMath::DegreesToRadians((static_cast<double>(BinIndex) + 0.5) * 360.0 / SilhouetteBinCount);
			const FVector2D Direction(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians));
			for (const FTDFluidBrushStamp& Stamp : Stamps)
			{
				ReachByBin[BinIndex] = FMath::Max(ReachByBin[BinIndex], ComputeStampReachAlongDirection(Stamp, CenterWorld, Basis, Direction));
			}
		}

		TArray<double> SmoothedReach;
		SmoothedReach.Init(0.0, SilhouetteBinCount);
		for (int32 BinIndex = 0; BinIndex < SilhouetteBinCount; ++BinIndex)
		{
			for (int32 Offset = -SilhouetteSmoothingHalfWidthBins; Offset <= SilhouetteSmoothingHalfWidthBins; ++Offset)
			{
				SmoothedReach[BinIndex] += ReachByBin[(BinIndex + Offset + SilhouetteBinCount) % SilhouetteBinCount];
			}
			SmoothedReach[BinIndex] /= static_cast<double>(2 * SilhouetteSmoothingHalfWidthBins + 1);
		}

		double MeanReach = 0.0;
		for (const double Reach : SmoothedReach)
		{
			MeanReach += Reach / SilhouetteBinCount;
		}
		double Variance = 0.0;
		for (const double Reach : SmoothedReach)
		{
			Variance += FMath::Square(Reach - MeanReach) / SilhouetteBinCount;
		}

		FTDBurstSilhouette Silhouette;
		if (MeanReach <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Silhouette;
		}

		Silhouette.Variation = FMath::Sqrt(Variance) / MeanReach;
		for (int32 BinIndex = 0; BinIndex < SilhouetteBinCount; ++BinIndex)
		{
			const double Previous = SmoothedReach[(BinIndex - 1 + SilhouetteBinCount) % SilhouetteBinCount];
			const double Next = SmoothedReach[(BinIndex + 1) % SilhouetteBinCount];
			const double FarLeft = SmoothedReach[(BinIndex - SilhouettePeakSpanBins + SilhouetteBinCount) % SilhouetteBinCount];
			const double FarRight = SmoothedReach[(BinIndex + SilhouettePeakSpanBins) % SilhouetteBinCount];
			const bool bIsLocalPeak = SmoothedReach[BinIndex] >= Previous && SmoothedReach[BinIndex] > Next;
			const bool bIsProminent = SmoothedReach[BinIndex] - FMath::Min(FarLeft, FarRight) > SilhouettePeakProminenceRatio * MeanReach;
			Silhouette.PeakCount += bIsLocalPeak && bIsProminent ? 1 : 0;
		}
		return Silhouette;
	}

	double ComputePlaneAngleDegrees(const FVector& Offset, const FTDFluidPlaneBasis& Basis)
	{
		const FVector2D PlaneOffset = Basis.ProjectVector(Offset);
		return FMath::RadiansToDegrees(FMath::Atan2(PlaneOffset.Y, PlaneOffset.X));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBurstLobesOffByDefaultTest,
	"TDGame.FluidFX.Fragment.BurstLobesOffByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBurstLobesOffByDefaultTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBurstLobeTestPrivate;

	const FTDFluidFragmentTrailSettings DefaultSettings;
	TestEqual(TEXT("Trail settings have no burst lobes by default"), DefaultSettings.BurstLobeCount, 0);

	FTDFluidFragmentTrailSettings TunedRangeSettings = DefaultSettings;
	TunedRangeSettings.BurstLobeCenterRatioMin = 0.9f;
	TunedRangeSettings.BurstLobeCenterRatioMax = 1.4f;
	TunedRangeSettings.BurstLobeRadiusRatioMin = 0.1f;
	TunedRangeSettings.BurstLobeRadiusRatioMax = 0.2f;
	TunedRangeSettings.BurstLobeSpeedMinCmPerSec = 900.0f;
	TunedRangeSettings.BurstLobeSpeedMaxCmPerSec = 950.0f;
	TunedRangeSettings.BurstLobeSeed = 77;

	const FTDFluidPlaneBasis Basis = MakeStageCameraBasis();
	const TArray<FTDFluidBrushStamp> DefaultStamps = BuildExplosionFireBurst(StageBurstCenterWorld, Basis, DefaultSettings);
	const TArray<FTDFluidBrushStamp> TunedRangeStamps = BuildExplosionFireBurst(StageBurstCenterWorld, Basis, TunedRangeSettings);
	TestEqual(TEXT("A burst without lobes is one core stamp plus the ring"), DefaultStamps.Num(), 1 + DefaultSettings.BurstRingStampCount);
	TestTrue(TEXT("Lobe ranges are inert while the lobe count is zero"), AreStampListsIdentical(DefaultStamps, TunedRangeStamps));

	FTDFluidFragmentTrailSettings ExplosionWithoutLobes = UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion);
	ExplosionWithoutLobes.BurstLobeCount = 0;
	const TArray<FTDFluidBrushStamp> CoreOnlyStamps = BuildExplosionFireBurst(StageBurstCenterWorld, Basis, ExplosionWithoutLobes);
	if (!TestEqual(TEXT("The explosion burst without lobes is the core stamp alone"), CoreOnlyStamps.Num(), 1))
	{
		return false;
	}

	FTDFluidBrushStamp ExpectedCore;
	ExpectedCore.WorldLocation = StageBurstCenterWorld;
	ExpectedCore.WorldVelocity = FVector::ZeroVector;
	ExpectedCore.RadiusCm = ExplosionWithoutLobes.BurstCoreRadiusCm;
	ExpectedCore.DensityStrength = ExplosionWithoutLobes.FireDensityStrength;
	ExpectedCore.VelocityStrength = 0.0f;
	ExpectedCore.Channel = ETDFluidBrushChannel::ChannelB_Fire;
	ExpectedCore.HeatStrength = ExplosionWithoutLobes.FireHeatStrength;
	TestTrue(TEXT("The core stamp is unchanged from the lobe-free burst"), AreStampsIdentical(CoreOnlyStamps[0], ExpectedCore));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBurstLobesFollowSettingsTest,
	"TDGame.FluidFX.Fragment.BurstLobesFollowSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBurstLobesFollowSettingsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBurstLobeTestPrivate;

	const FTDFluidFragmentTrailSettings Settings = MakeLobedExplosionTrailSettings();
	TestEqual(TEXT("The explosion burst keeps no ring stamps"), Settings.BurstRingStampCount, 0);

	const FTDFluidPlaneBasis Basis = MakeStageCameraBasis();
	for (const float RadiusScale : { 1.0f, 1.5f })
	{
		const TArray<FTDFluidBrushStamp> Stamps = BuildExplosionFireBurst(StageBurstCenterWorld, Basis, Settings, RadiusScale);
		if (!TestEqual(TEXT("A lobed burst is one core stamp plus the lobes"), Stamps.Num(), 1 + Settings.BurstLobeCount))
		{
			return false;
		}

		const double CoreRadiusCm = Settings.BurstCoreRadiusCm * RadiusScale;
		const double SectorDegrees = 360.0 / Settings.BurstLobeCount;
		for (int32 LobeIndex = 0; LobeIndex < Settings.BurstLobeCount; ++LobeIndex)
		{
			const FTDFluidBrushStamp& Lobe = Stamps[1 + LobeIndex];
			const FVector Offset = Lobe.WorldLocation - StageBurstCenterWorld;
			const double CenterRatio = Offset.Size() / CoreRadiusCm;
			const double RadiusRatio = Lobe.RadiusCm / CoreRadiusCm;
			TestTrue(TEXT("Lobe centres stay inside the centre ratio range"), CenterRatio >= Settings.BurstLobeCenterRatioMin - RatioTolerance && CenterRatio <= Settings.BurstLobeCenterRatioMax + RatioTolerance);
			TestTrue(TEXT("Lobe radii stay inside the radius ratio range"), RadiusRatio >= Settings.BurstLobeRadiusRatioMin - RatioTolerance && RadiusRatio <= Settings.BurstLobeRadiusRatioMax + RatioTolerance);
			TestTrue(TEXT("Lobe speeds stay inside the speed range"), Lobe.WorldVelocity.Size() >= Settings.BurstLobeSpeedMinCmPerSec - SpeedToleranceCmPerSec && Lobe.WorldVelocity.Size() <= Settings.BurstLobeSpeedMaxCmPerSec + SpeedToleranceCmPerSec);
			TestTrue(TEXT("Lobe velocity points straight outward"), FVector::DotProduct(Lobe.WorldVelocity.GetSafeNormal(), Offset.GetSafeNormal()) > 0.999);
			TestTrue(TEXT("Lobes lie in the fluid plane"), FMath::Abs(FVector::DotProduct(Offset.GetSafeNormal(), Basis.N)) < 1e-3);
			TestNearlyEqual(TEXT("Lobes drive the field"), Lobe.VelocityStrength, 1.0f, 1e-4f);
			TestNearlyEqual(TEXT("Lobes carry the burst density"), Lobe.DensityStrength, Settings.FireDensityStrength, 1e-6f);
			TestNearlyEqual(TEXT("Lobes carry the burst heat"), Lobe.HeatStrength, Settings.FireHeatStrength, 1e-6f);
			TestEqual(TEXT("Lobes use the fire channel"), Lobe.Channel, ETDFluidBrushChannel::ChannelB_Fire);

			const double SectorStartDegrees = FirstLobeSectorStartDegrees + SectorDegrees * LobeIndex;
			const double AngleDegrees = SectorStartDegrees + FMath::Fmod(ComputePlaneAngleDegrees(Offset, Basis) - SectorStartDegrees + 720.0, 360.0);
			TestTrue(TEXT("Each lobe stays inside its own sector"), AngleDegrees >= SectorStartDegrees - AngleToleranceDegrees && AngleDegrees <= SectorStartDegrees + SectorDegrees + AngleToleranceDegrees);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBurstLobesDeterministicTest,
	"TDGame.FluidFX.Fragment.BurstLobesAreDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBurstLobesDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBurstLobeTestPrivate;

	const FTDFluidFragmentTrailSettings Settings = MakeLobedExplosionTrailSettings();
	const FTDFluidPlaneBasis Basis = MakeStageCameraBasis();
	const TArray<FTDFluidBrushStamp> FirstStamps = BuildExplosionFireBurst(StageBurstCenterWorld, Basis, Settings);
	const TArray<FTDFluidBrushStamp> RepeatedStamps = BuildExplosionFireBurst(StageBurstCenterWorld, Basis, Settings);
	TestTrue(TEXT("The same burst centre builds identical lobes"), AreStampListsIdentical(FirstStamps, RepeatedStamps));

	auto HasSameLayout = [&FirstStamps](const TArray<FTDFluidBrushStamp>& OtherStamps, const FVector& OtherCenterWorld)
	{
		if (OtherStamps.Num() != FirstStamps.Num())
		{
			return false;
		}

		for (int32 StampIndex = 0; StampIndex < FirstStamps.Num(); ++StampIndex)
		{
			const FVector FirstOffset = FirstStamps[StampIndex].WorldLocation - StageBurstCenterWorld;
			const FVector OtherOffset = OtherStamps[StampIndex].WorldLocation - OtherCenterWorld;
			if (!FirstOffset.Equals(OtherOffset, LocationToleranceCm) || !FMath::IsNearlyEqual(FirstStamps[StampIndex].RadiusCm, OtherStamps[StampIndex].RadiusCm, 1e-3f))
			{
				return false;
			}
		}
		return true;
	};

	const FVector JitteredCenterWorld = StageBurstCenterWorld + FVector(0.2, -0.3, 0.1);
	TestTrue(TEXT("Sub-centimetre jitter of the burst centre keeps the lobe layout"), HasSameLayout(BuildExplosionFireBurst(JitteredCenterWorld, Basis, Settings), JitteredCenterWorld));

	const FVector MovedCenterWorld = StageBurstCenterWorld + FVector(137.0, 0.0, 0.0);
	TestFalse(TEXT("A different burst centre builds a different lobe layout"), HasSameLayout(BuildExplosionFireBurst(MovedCenterWorld, Basis, Settings), MovedCenterWorld));

	FTDFluidFragmentTrailSettings ReseededSettings = Settings;
	ReseededSettings.BurstLobeSeed = Settings.BurstLobeSeed + 1;
	TestFalse(TEXT("A different lobe seed builds a different lobe layout"), HasSameLayout(BuildExplosionFireBurst(StageBurstCenterWorld, Basis, ReseededSettings), StageBurstCenterWorld));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBurstLobesFireOnlyTest,
	"TDGame.FluidFX.Fragment.BurstLobesOnlyOnFireChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBurstLobesFireOnlyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBurstLobeTestPrivate;

	const FTDFluidFragmentTrailSettings Settings = MakeLobedExplosionTrailSettings();
	const FTDFluidPlaneBasis Basis = MakeStageCameraBasis();
	TArray<FTDFluidBrushStamp> SmokeStamps;
	ATDFluidFragmentEmitterActor::BuildBurstStamps(StageBurstCenterWorld, Basis, Settings, ETDFluidBrushChannel::ChannelR_Mud, 1.75f, Settings.SmokeDensityStrength, Settings.SmokeHeatStrength, SmokeStamps);
	if (!TestEqual(TEXT("Smoke and dust bursts keep the lobe-free core"), SmokeStamps.Num(), 1 + Settings.BurstRingStampCount))
	{
		return false;
	}

	TestNearlyEqual(TEXT("The smoke core keeps its scaled radius"), SmokeStamps[0].RadiusCm, Settings.BurstCoreRadiusCm * 1.75f, 1e-3f);
	TestEqual(TEXT("The fire burst adds the lobes"), BuildExplosionFireBurst(StageBurstCenterWorld, Basis, Settings).Num(), 1 + Settings.BurstRingStampCount + ExplosionLobeCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBurstLobeSilhouetteTest,
	"TDGame.FluidFX.Explosion.BurstLobeSilhouette",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBurstLobeSilhouetteTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidBurstLobeTestPrivate;

	const FTDFluidFragmentTrailSettings Settings = MakeLobedExplosionTrailSettings();
	FTDFluidFragmentTrailSettings CoreOnlySettings = Settings;
	CoreOnlySettings.BurstLobeCount = 0;
	const FTDFluidPlaneBasis Basis = MakeStageCameraBasis();
	const FTDBurstSilhouette CoreOnly = MeasureBurstSilhouette(BuildExplosionFireBurst(StageBurstCenterWorld, Basis, CoreOnlySettings), StageBurstCenterWorld, Basis);
	TestEqual(TEXT("The lobe-free core is a round silhouette without peaks"), CoreOnly.PeakCount, 0);
	TestTrue(TEXT("The lobe-free core has no radial variation"), CoreOnly.Variation < 1e-3);

	for (const FVector& CenterWorld : SampleBurstCentersWorld)
	{
		const FTDBurstSilhouette Silhouette = MeasureBurstSilhouette(BuildExplosionFireBurst(CenterWorld, Basis, Settings), CenterWorld, Basis);
		const FString Line = FString::Printf(TEXT("TDFluidBurstLobeSilhouette: center=(%.0f,%.0f,%.0f) peaks=%d variation=%.4f"), CenterWorld.X, CenterWorld.Y, CenterWorld.Z, Silhouette.PeakCount, Silhouette.Variation);
		AddInfo(Line);
		TestTrue(*FString::Printf(TEXT("The lobed burst shows at least %d silhouette peaks at %s"), MinExplosionSilhouettePeakCount, *CenterWorld.ToString()), Silhouette.PeakCount >= MinExplosionSilhouettePeakCount);
		TestTrue(*FString::Printf(TEXT("The lobed burst silhouette varies between %.2f and %.2f at %s"), MinExplosionSilhouetteVariation, MaxExplosionSilhouetteVariation, *CenterWorld.ToString()), Silhouette.Variation >= MinExplosionSilhouetteVariation && Silhouette.Variation <= MaxExplosionSilhouetteVariation);
	}
	return true;
}

#endif
