#include "FluidFX/TDFog3DTypes.h"

namespace TDFog3DTypesPrivate
{
	constexpr int32 MinimumResolution = 64;
	constexpr int32 MaximumResolution = 512;
	constexpr double MaximumStepsPerSecond = 240.0;
	constexpr double RequiredCellSizeCm = 15.625;
	constexpr double RequiredLayerThicknessCm = 30.0;
	constexpr double RequiredValueTolerance = 1.0e-6;
	constexpr double MaximumStepSecondsLimit = 0.05f;
	constexpr int32 MinimumFollowMarginCells = 8;
	constexpr int32 MaximumFollowMarginCells = 56;
	constexpr double MinimumRadiusCm = 16.0;
	constexpr int32 MaximumSphereCount = 16;
	constexpr int32 MaximumObstacleLimit = 32;
	constexpr int32 MaximumEdgeBandCells = 16;
	constexpr int32 MaximumNoiseSeed = 8388607;
	constexpr int32 FollowMarginSafetyCells = 8;
	constexpr int32 QualityLevelCount = 3;
	constexpr int32 MinimumGroundCaptureBorderCells = 17;
	constexpr int32 MinimumGroundCaptureLatencyFrames = 1;
	constexpr int32 MaximumGroundCaptureLatencyFrames = 4;

	struct FNamedValue
	{
		const TCHAR* Name;
		double Value;
	};

	class FProblemSink
	{
	public:
		explicit FProblemSink(TArray<FString>& InProblems)
			: Problems(InProblems)
		{
		}

		void Report(const TCHAR* FieldName, const TCHAR* Rule)
		{
			Problems.Add(FString::Printf(TEXT("%s %s"), FieldName, Rule));
		}

		void RequireRange(const TCHAR* FieldName, double Value, double Minimum, double Maximum)
		{
			if (Value >= Minimum && Value <= Maximum)
			{
				return;
			}
			Problems.Add(FString::Printf(TEXT("%s must be within [%g, %g] but is %g"), FieldName, Minimum, Maximum, Value));
		}

		void RequireAtLeast(const TCHAR* FieldName, double Value, double Minimum)
		{
			if (Value >= Minimum)
			{
				return;
			}
			Problems.Add(FString::Printf(TEXT("%s must be at least %g but is %g"), FieldName, Minimum, Value));
		}

		void RequireGreaterThan(const TCHAR* FieldName, double Value, double Lower)
		{
			if (Value > Lower)
			{
				return;
			}
			Problems.Add(FString::Printf(TEXT("%s must be greater than %g but is %g"), FieldName, Lower, Value));
		}

		void RequireEqual(const TCHAR* FieldName, double Value, double Expected)
		{
			if (FMath::Abs(Value - Expected) <= RequiredValueTolerance)
			{
				return;
			}
			Problems.Add(FString::Printf(TEXT("%s must be %g in stage 1 but is %g"), FieldName, Expected, Value));
		}

		void RequireResolution(const TCHAR* FieldName, int32 Value)
		{
			const bool bIsPowerOfTwo = Value > 0 && (Value & (Value - 1)) == 0;
			if (bIsPowerOfTwo && Value >= MinimumResolution && Value <= MaximumResolution)
			{
				return;
			}
			Problems.Add(FString::Printf(TEXT("%s must be a power of two within [%d, %d] but is %d"), FieldName, MinimumResolution, MaximumResolution, Value));
		}

	private:
		TArray<FString>& Problems;
	};

	void AppendFiniteProblems(const FTDFog3DSettings& Settings, FProblemSink& Sink)
	{
		const FNamedValue Values[] = {
			{ TEXT("QualityLowStepsPerSecond"), Settings.QualityLowStepsPerSecond },
			{ TEXT("QualityMediumStepsPerSecond"), Settings.QualityMediumStepsPerSecond },
			{ TEXT("QualityHighStepsPerSecond"), Settings.QualityHighStepsPerSecond },
			{ TEXT("CellSizeCm"), Settings.CellSizeCm },
			{ TEXT("LayerThicknessCm"), Settings.LayerThicknessCm },
			{ TEXT("MaximumStepSeconds"), Settings.MaximumStepSeconds },
			{ TEXT("BaseDensity"), Settings.BaseDensity },
			{ TEXT("BaseThicknessCm"), Settings.BaseThicknessCm },
			{ TEXT("BaseTopBlurCm"), Settings.BaseTopBlurCm },
			{ TEXT("WindRefillPerSec"), Settings.WindRefillPerSec },
			{ TEXT("DisplacementRefillHorizontalPerSec"), Settings.DisplacementRefillHorizontalPerSec },
			{ TEXT("DisplacementRefillVerticalPerSec"), Settings.DisplacementRefillVerticalPerSec },
			{ TEXT("FillRefillPerSec"), Settings.FillRefillPerSec },
			{ TEXT("EdgeBandAbsorptionPerSec"), Settings.EdgeBandAbsorptionPerSec },
			{ TEXT("BladeBaseLocalCm.X"), Settings.BladeBaseLocalCm.X },
			{ TEXT("BladeBaseLocalCm.Y"), Settings.BladeBaseLocalCm.Y },
			{ TEXT("BladeBaseLocalCm.Z"), Settings.BladeBaseLocalCm.Z },
			{ TEXT("BladeTipLocalCm.X"), Settings.BladeTipLocalCm.X },
			{ TEXT("BladeTipLocalCm.Y"), Settings.BladeTipLocalCm.Y },
			{ TEXT("BladeTipLocalCm.Z"), Settings.BladeTipLocalCm.Z },
			{ TEXT("BladeRadiusCm"), Settings.BladeRadiusCm },
			{ TEXT("BladeWindStrength"), Settings.BladeWindStrength },
			{ TEXT("BladeWindGain"), Settings.BladeWindGain },
			{ TEXT("BladeErosionPerSec"), Settings.BladeErosionPerSec },
			{ TEXT("HandRadiusCm"), Settings.HandRadiusCm },
			{ TEXT("FootRadiusCm"), Settings.FootRadiusCm },
			{ TEXT("PelvisRadiusCm"), Settings.PelvisRadiusCm },
			{ TEXT("BodyWindStrength"), Settings.BodyWindStrength },
			{ TEXT("BodyWindGain"), Settings.BodyWindGain },
			{ TEXT("BodyErosionPerSec"), Settings.BodyErosionPerSec },
			{ TEXT("MaximumInputSpeedCmPerSec"), Settings.MaximumInputSpeedCmPerSec },
			{ TEXT("ObstacleBaseHalfThicknessCm"), Settings.ObstacleBaseHalfThicknessCm },
			{ TEXT("ObstacleUpwardExtraCm"), Settings.ObstacleUpwardExtraCm },
			{ TEXT("ZoneEdgeFadeCm"), Settings.ZoneEdgeFadeCm },
			{ TEXT("ZoneEntryVerticalMarginCm"), Settings.ZoneEntryVerticalMarginCm },
			{ TEXT("FineNoiseAmplitude"), Settings.FineNoiseAmplitude },
			{ TEXT("FineNoiseSpacingCm"), Settings.FineNoiseSpacingCm },
			{ TEXT("StainNoiseAmplitude"), Settings.StainNoiseAmplitude },
			{ TEXT("StainNoiseSpacingCm"), Settings.StainNoiseSpacingCm },
			{ TEXT("GroundWallStepCm"), Settings.GroundWallStepCm },
			{ TEXT("GroundCaptureHeightAboveZoneCm"), Settings.GroundCaptureHeightAboveZoneCm },
			{ TEXT("GroundMissingHeightCm"), Settings.GroundMissingHeightCm },
			{ TEXT("GroundMissingBelowFloorCm"), Settings.GroundMissingBelowFloorCm },
		};
		for (const FNamedValue& NamedValue : Values)
		{
			if (!FMath::IsFinite(NamedValue.Value))
			{
				Sink.Report(NamedValue.Name, TEXT("must be a finite number"));
			}
		}
	}

	void AppendSwitchAndQualityProblems(const FTDFog3DSettings& Settings, FProblemSink& Sink)
	{
		Sink.RequireRange(TEXT("Mode"), Settings.Mode, 0.0, 2.0);
		Sink.RequireRange(TEXT("DefaultQualityLevel"), Settings.DefaultQualityLevel, 0.0, QualityLevelCount - 1);
		Sink.RequireResolution(TEXT("QualityLowResolution"), Settings.QualityLowResolution);
		Sink.RequireResolution(TEXT("QualityMediumResolution"), Settings.QualityMediumResolution);
		Sink.RequireResolution(TEXT("QualityHighResolution"), Settings.QualityHighResolution);
		Sink.RequireRange(TEXT("QualityLowStepsPerSecond"), Settings.QualityLowStepsPerSecond, UE_SMALL_NUMBER, MaximumStepsPerSecond);
		Sink.RequireRange(TEXT("QualityMediumStepsPerSecond"), Settings.QualityMediumStepsPerSecond, UE_SMALL_NUMBER, MaximumStepsPerSecond);
		Sink.RequireRange(TEXT("QualityHighStepsPerSecond"), Settings.QualityHighStepsPerSecond, UE_SMALL_NUMBER, MaximumStepsPerSecond);
	}

	void AppendGridAndClockProblems(const FTDFog3DSettings& Settings, FProblemSink& Sink)
	{
		Sink.RequireEqual(TEXT("CellSizeCm"), Settings.CellSizeCm, RequiredCellSizeCm);
		Sink.RequireEqual(TEXT("LayerThicknessCm"), Settings.LayerThicknessCm, RequiredLayerThicknessCm);
		Sink.RequireRange(TEXT("MaximumStepSeconds"), Settings.MaximumStepSeconds, UE_SMALL_NUMBER, MaximumStepSecondsLimit);
		Sink.RequireRange(TEXT("WindowFollowMarginCells"), Settings.WindowFollowMarginCells, MinimumFollowMarginCells, MaximumFollowMarginCells);

		const int32 SmallestResolution = FMath::Min3(Settings.QualityLowResolution, Settings.QualityMediumResolution, Settings.QualityHighResolution);
		const int64 FollowMarginLimit = static_cast<int64>(SmallestResolution) / 2 - Settings.EdgeBandCells - FollowMarginSafetyCells;
		if (Settings.WindowFollowMarginCells > FollowMarginLimit)
		{
			Sink.Report(TEXT("WindowFollowMarginCells"), *FString::Printf(TEXT("must be at most half the smallest quality resolution minus EdgeBandCells minus %d (%lld) but is %d"), FollowMarginSafetyCells, FollowMarginLimit, Settings.WindowFollowMarginCells));
		}
	}

	void AppendBaseAndRefillProblems(const FTDFog3DSettings& Settings, FProblemSink& Sink)
	{
		Sink.RequireAtLeast(TEXT("BaseDensity"), Settings.BaseDensity, 0.0);
		Sink.RequireAtLeast(TEXT("BaseTopBlurCm"), Settings.BaseTopBlurCm, 0.0);
		Sink.RequireGreaterThan(TEXT("BaseThicknessCm"), Settings.BaseThicknessCm, Settings.BaseTopBlurCm);
		Sink.RequireAtLeast(TEXT("WindRefillPerSec"), Settings.WindRefillPerSec, 0.0);
		Sink.RequireAtLeast(TEXT("DisplacementRefillHorizontalPerSec"), Settings.DisplacementRefillHorizontalPerSec, 0.0);
		Sink.RequireAtLeast(TEXT("DisplacementRefillVerticalPerSec"), Settings.DisplacementRefillVerticalPerSec, 0.0);
		Sink.RequireAtLeast(TEXT("FillRefillPerSec"), Settings.FillRefillPerSec, 0.0);
		Sink.RequireRange(TEXT("EdgeBandCells"), Settings.EdgeBandCells, 0.0, MaximumEdgeBandCells);
		Sink.RequireAtLeast(TEXT("EdgeBandAbsorptionPerSec"), Settings.EdgeBandAbsorptionPerSec, 0.0);
	}

	void AppendInputProblems(const FTDFog3DSettings& Settings, FProblemSink& Sink)
	{
		if (!Settings.BladeSocketName.IsNone())
		{
			Sink.RequireGreaterThan(TEXT("Blade length"), FVector::Dist(Settings.BladeBaseLocalCm, Settings.BladeTipLocalCm), UE_KINDA_SMALL_NUMBER);
		}
		Sink.RequireAtLeast(TEXT("BladeRadiusCm"), Settings.BladeRadiusCm, MinimumRadiusCm);
		Sink.RequireAtLeast(TEXT("HandRadiusCm"), Settings.HandRadiusCm, MinimumRadiusCm);
		Sink.RequireAtLeast(TEXT("FootRadiusCm"), Settings.FootRadiusCm, MinimumRadiusCm);
		Sink.RequireAtLeast(TEXT("PelvisRadiusCm"), Settings.PelvisRadiusCm, MinimumRadiusCm);
		Sink.RequireAtLeast(TEXT("BladeWindStrength"), Settings.BladeWindStrength, 0.0);
		Sink.RequireAtLeast(TEXT("BladeWindGain"), Settings.BladeWindGain, 0.0);
		Sink.RequireAtLeast(TEXT("BladeErosionPerSec"), Settings.BladeErosionPerSec, 0.0);
		Sink.RequireAtLeast(TEXT("BodyWindStrength"), Settings.BodyWindStrength, 0.0);
		Sink.RequireAtLeast(TEXT("BodyWindGain"), Settings.BodyWindGain, 0.0);
		Sink.RequireAtLeast(TEXT("BodyErosionPerSec"), Settings.BodyErosionPerSec, 0.0);
		Sink.RequireGreaterThan(TEXT("MaximumInputSpeedCmPerSec"), Settings.MaximumInputSpeedCmPerSec, 0.0);
		Sink.RequireRange(TEXT("MaximumInputSphereCount"), Settings.MaximumInputSphereCount, 1.0, MaximumSphereCount);
	}

	void AppendObstacleZoneAndNoiseProblems(const FTDFog3DSettings& Settings, FProblemSink& Sink)
	{
		Sink.RequireRange(TEXT("MaximumObstacleCount"), Settings.MaximumObstacleCount, 0.0, MaximumObstacleLimit);
		Sink.RequireGreaterThan(TEXT("ObstacleBaseHalfThicknessCm"), Settings.ObstacleBaseHalfThicknessCm, 0.0);
		Sink.RequireAtLeast(TEXT("ObstacleUpwardExtraCm"), Settings.ObstacleUpwardExtraCm, 0.0);
		Sink.RequireAtLeast(TEXT("ZoneEdgeFadeCm"), Settings.ZoneEdgeFadeCm, 0.0);
		Sink.RequireAtLeast(TEXT("ZoneEntryVerticalMarginCm"), Settings.ZoneEntryVerticalMarginCm, 0.0);
		Sink.RequireRange(TEXT("FineNoiseAmplitude"), Settings.FineNoiseAmplitude, 0.0, 1.0 - UE_DOUBLE_SMALL_NUMBER);
		Sink.RequireGreaterThan(TEXT("FineNoiseSpacingCm"), Settings.FineNoiseSpacingCm, 0.0);
		Sink.RequireRange(TEXT("FineNoiseSeed"), Settings.FineNoiseSeed, 0.0, MaximumNoiseSeed);
		Sink.RequireRange(TEXT("StainNoiseAmplitude"), Settings.StainNoiseAmplitude, 0.0, 1.0 - UE_DOUBLE_SMALL_NUMBER);
		Sink.RequireGreaterThan(TEXT("StainNoiseSpacingCm"), Settings.StainNoiseSpacingCm, 0.0);
		Sink.RequireRange(TEXT("StainNoiseSeed"), Settings.StainNoiseSeed, 0.0, MaximumNoiseSeed);
	}

	void AppendGroundProblems(const FTDFog3DSettings& Settings, FProblemSink& Sink)
	{
		Sink.RequireGreaterThan(TEXT("GroundWallStepCm"), Settings.GroundWallStepCm, 0.0);
		Sink.RequireAtLeast(TEXT("GroundCaptureBorderCells"), Settings.GroundCaptureBorderCells, MinimumGroundCaptureBorderCells);
		Sink.RequireAtLeast(TEXT("GroundCaptureHeightAboveZoneCm"), Settings.GroundCaptureHeightAboveZoneCm, 0.0);
		Sink.RequireRange(TEXT("GroundCaptureLatencyFrames"), Settings.GroundCaptureLatencyFrames, MinimumGroundCaptureLatencyFrames, MaximumGroundCaptureLatencyFrames);
		Sink.RequireGreaterThan(TEXT("GroundMissingBelowFloorCm"), Settings.GroundMissingBelowFloorCm, 0.0);
		if (Settings.bFollowsGround && Settings.GroundFollowVolumeMaterial.IsNull())
		{
			Sink.Report(TEXT("GroundFollowVolumeMaterial"), TEXT("must be set when bFollowsGround is on"));
		}
	}
}

void CollectTDFog3DSettingsProblems(const FTDFog3DSettings& Settings, TArray<FString>& OutProblems)
{
	using namespace TDFog3DTypesPrivate;
	FProblemSink Sink(OutProblems);
	AppendFiniteProblems(Settings, Sink);
	AppendSwitchAndQualityProblems(Settings, Sink);
	AppendGridAndClockProblems(Settings, Sink);
	AppendBaseAndRefillProblems(Settings, Sink);
	AppendInputProblems(Settings, Sink);
	AppendObstacleZoneAndNoiseProblems(Settings, Sink);
	AppendGroundProblems(Settings, Sink);
}

void CollectTDFog3DZoneProblems(const FVector& ZoneHalfExtentCm, const FTDFog3DSettings& Settings, TArray<FString>& OutProblems)
{
	using namespace TDFog3DTypesPrivate;
	FProblemSink Sink(OutProblems);
	const bool bIsFinite = FMath::IsFinite(ZoneHalfExtentCm.X) && FMath::IsFinite(ZoneHalfExtentCm.Y) && FMath::IsFinite(ZoneHalfExtentCm.Z);
	if (!bIsFinite)
	{
		Sink.Report(TEXT("ZoneHalfExtentCm"), TEXT("must be a finite vector"));
		return;
	}
	Sink.RequireGreaterThan(TEXT("ZoneHalfExtentCm.X"), ZoneHalfExtentCm.X, 0.0);
	Sink.RequireGreaterThan(TEXT("ZoneHalfExtentCm.Y"), ZoneHalfExtentCm.Y, 0.0);
	Sink.RequireGreaterThan(TEXT("ZoneHalfExtentCm.Z"), ZoneHalfExtentCm.Z, 0.0);
	Sink.RequireAtLeast(TEXT("Zone side X (twice ZoneHalfExtentCm.X) against twice ZoneEdgeFadeCm"), 2.0 * ZoneHalfExtentCm.X, 2.0 * Settings.ZoneEdgeFadeCm);
	Sink.RequireAtLeast(TEXT("Zone side Y (twice ZoneHalfExtentCm.Y) against twice ZoneEdgeFadeCm"), 2.0 * ZoneHalfExtentCm.Y, 2.0 * Settings.ZoneEdgeFadeCm);
}

FTDFog3DQualityTier ResolveTDFog3DQualityTier(const FTDFog3DSettings& Settings, int32 Level)
{
	FTDFog3DQualityTier Tier;
	const int32 ClampedLevel = FMath::Clamp(Level, 0, TDFog3DTypesPrivate::QualityLevelCount - 1);
	if (ClampedLevel == 0)
	{
		Tier.Resolution = Settings.QualityLowResolution;
		Tier.StepsPerSecond = Settings.QualityLowStepsPerSecond;
		return Tier;
	}
	if (ClampedLevel == 1)
	{
		Tier.Resolution = Settings.QualityMediumResolution;
		Tier.StepsPerSecond = Settings.QualityMediumStepsPerSecond;
		return Tier;
	}
	Tier.Resolution = Settings.QualityHighResolution;
	Tier.StepsPerSecond = Settings.QualityHighStepsPerSecond;
	return Tier;
}
