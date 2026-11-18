#include "FluidFX/TDFog3DTypes.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFog3DSettingsTestPrivate
{
	struct FTDFog3DBadSettingsCase
	{
		FString Name;
		TFunction<void(FTDFog3DSettings&)> Mutate;
	};

	struct FTDFog3DBadZoneCase
	{
		FString Name;
		FVector ZoneHalfExtentCm;
	};

	FTDFog3DSettings ReadSettingsFromTable()
	{
		FTDFog3DSettings Settings;
		UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFog3DSettings::StaticStruct(), &Settings);
		return Settings;
	}

	TArray<FTDFog3DBadSettingsCase> MakeBadSettingsCases()
	{
		TArray<FTDFog3DBadSettingsCase> Cases;
		Cases.Add({ TEXT("mode above range"), [](FTDFog3DSettings& S) { S.Mode = 3; } });
		Cases.Add({ TEXT("mode below range"), [](FTDFog3DSettings& S) { S.Mode = -1; } });
		Cases.Add({ TEXT("default quality above range"), [](FTDFog3DSettings& S) { S.DefaultQualityLevel = 3; } });
		Cases.Add({ TEXT("low resolution not a power of two"), [](FTDFog3DSettings& S) { S.QualityLowResolution = 100; } });
		Cases.Add({ TEXT("medium resolution not a power of two"), [](FTDFog3DSettings& S) { S.QualityMediumResolution = 192; } });
		Cases.Add({ TEXT("high resolution below minimum"), [](FTDFog3DSettings& S) { S.QualityHighResolution = 32; } });
		Cases.Add({ TEXT("high resolution above maximum"), [](FTDFog3DSettings& S) { S.QualityHighResolution = 1024; } });
		Cases.Add({ TEXT("zero resolution"), [](FTDFog3DSettings& S) { S.QualityLowResolution = 0; } });
		Cases.Add({ TEXT("negative resolution"), [](FTDFog3DSettings& S) { S.QualityLowResolution = -128; } });
		Cases.Add({ TEXT("zero steps per second"), [](FTDFog3DSettings& S) { S.QualityLowStepsPerSecond = 0.0f; } });
		Cases.Add({ TEXT("steps per second above 240"), [](FTDFog3DSettings& S) { S.QualityHighStepsPerSecond = 241.0f; } });
		Cases.Add({ TEXT("negative steps per second"), [](FTDFog3DSettings& S) { S.QualityMediumStepsPerSecond = -30.0f; } });
		Cases.Add({ TEXT("cell size is not 15.625"), [](FTDFog3DSettings& S) { S.CellSizeCm = 20.0f; } });
		Cases.Add({ TEXT("layer thickness is not 30"), [](FTDFog3DSettings& S) { S.LayerThicknessCm = 25.0f; } });
		Cases.Add({ TEXT("maximum step seconds above 0.05"), [](FTDFog3DSettings& S) { S.MaximumStepSeconds = 0.1f; } });
		Cases.Add({ TEXT("maximum step seconds zero"), [](FTDFog3DSettings& S) { S.MaximumStepSeconds = 0.0f; } });
		Cases.Add({ TEXT("follow margin below snap error"), [](FTDFog3DSettings& S) { S.WindowFollowMarginCells = 4; } });
		Cases.Add({ TEXT("follow margin above half window"), [](FTDFog3DSettings& S) { S.WindowFollowMarginCells = 64; } });
		Cases.Add({ TEXT("follow margin past the band of a 64 cell low quality window"), [](FTDFog3DSettings& S) { S.QualityLowResolution = 64; S.WindowFollowMarginCells = 40; } });
		Cases.Add({ TEXT("follow margin one cell past the limit of a 64 cell window"), [](FTDFog3DSettings& S) { S.QualityLowResolution = 64; S.WindowFollowMarginCells = 17; } });
		Cases.Add({ TEXT("follow margin 56 with a 16 cell band on a 128 cell window"), [](FTDFog3DSettings& S) { S.EdgeBandCells = 16; S.WindowFollowMarginCells = 56; } });
		Cases.Add({ TEXT("follow margin past the limit when the smallest window is the high tier"), [](FTDFog3DSettings& S) { S.QualityLowResolution = 256; S.QualityHighResolution = 64; S.WindowFollowMarginCells = 17; } });
		Cases.Add({ TEXT("negative base density"), [](FTDFog3DSettings& S) { S.BaseDensity = -0.1f; } });
		Cases.Add({ TEXT("top blur equals thickness"), [](FTDFog3DSettings& S) { S.BaseTopBlurCm = S.BaseThicknessCm; } });
		Cases.Add({ TEXT("negative top blur"), [](FTDFog3DSettings& S) { S.BaseTopBlurCm = -1.0f; } });
		Cases.Add({ TEXT("negative wind refill"), [](FTDFog3DSettings& S) { S.WindRefillPerSec = -0.1f; } });
		Cases.Add({ TEXT("negative horizontal displacement refill"), [](FTDFog3DSettings& S) { S.DisplacementRefillHorizontalPerSec = -0.1f; } });
		Cases.Add({ TEXT("negative vertical displacement refill"), [](FTDFog3DSettings& S) { S.DisplacementRefillVerticalPerSec = -0.1f; } });
		Cases.Add({ TEXT("negative fill refill"), [](FTDFog3DSettings& S) { S.FillRefillPerSec = -0.1f; } });
		Cases.Add({ TEXT("edge band above 16"), [](FTDFog3DSettings& S) { S.EdgeBandCells = 17; } });
		Cases.Add({ TEXT("negative edge band"), [](FTDFog3DSettings& S) { S.EdgeBandCells = -1; } });
		Cases.Add({ TEXT("negative edge absorption"), [](FTDFog3DSettings& S) { S.EdgeBandAbsorptionPerSec = -1.0f; } });
		Cases.Add({ TEXT("blade with zero length"), [](FTDFog3DSettings& S) { S.BladeTipLocalCm = S.BladeBaseLocalCm; } });
		Cases.Add({ TEXT("blade radius below 16"), [](FTDFog3DSettings& S) { S.BladeRadiusCm = 15.9f; } });
		Cases.Add({ TEXT("hand radius below 16"), [](FTDFog3DSettings& S) { S.HandRadiusCm = 10.0f; } });
		Cases.Add({ TEXT("foot radius below 16"), [](FTDFog3DSettings& S) { S.FootRadiusCm = 0.0f; } });
		Cases.Add({ TEXT("pelvis radius below 16"), [](FTDFog3DSettings& S) { S.PelvisRadiusCm = -30.0f; } });
		Cases.Add({ TEXT("negative blade wind"), [](FTDFog3DSettings& S) { S.BladeWindStrength = -1.0f; } });
		Cases.Add({ TEXT("negative blade erosion"), [](FTDFog3DSettings& S) { S.BladeErosionPerSec = -1.0f; } });
		Cases.Add({ TEXT("negative body wind"), [](FTDFog3DSettings& S) { S.BodyWindStrength = -1.0f; } });
		Cases.Add({ TEXT("negative body erosion"), [](FTDFog3DSettings& S) { S.BodyErosionPerSec = -1.0f; } });
		Cases.Add({ TEXT("negative body wind gain"), [](FTDFog3DSettings& S) { S.BodyWindGain = -0.25f; } });
		Cases.Add({ TEXT("negative blade wind gain"), [](FTDFog3DSettings& S) { S.BladeWindGain = -0.25f; } });
		Cases.Add({ TEXT("not a number body wind gain"), [](FTDFog3DSettings& S) { S.BodyWindGain = std::numeric_limits<float>::quiet_NaN(); } });
		Cases.Add({ TEXT("not a number blade wind gain"), [](FTDFog3DSettings& S) { S.BladeWindGain = std::numeric_limits<float>::quiet_NaN(); } });
		Cases.Add({ TEXT("infinite body wind gain"), [](FTDFog3DSettings& S) { S.BodyWindGain = std::numeric_limits<float>::infinity(); } });
		Cases.Add({ TEXT("zero speed limit"), [](FTDFog3DSettings& S) { S.MaximumInputSpeedCmPerSec = 0.0f; } });
		Cases.Add({ TEXT("zero input spheres"), [](FTDFog3DSettings& S) { S.MaximumInputSphereCount = 0; } });
		Cases.Add({ TEXT("seventeen input spheres"), [](FTDFog3DSettings& S) { S.MaximumInputSphereCount = 17; } });
		Cases.Add({ TEXT("thirty-three obstacles"), [](FTDFog3DSettings& S) { S.MaximumObstacleCount = 33; } });
		Cases.Add({ TEXT("negative obstacle count"), [](FTDFog3DSettings& S) { S.MaximumObstacleCount = -1; } });
		Cases.Add({ TEXT("zero obstacle half thickness"), [](FTDFog3DSettings& S) { S.ObstacleBaseHalfThicknessCm = 0.0f; } });
		Cases.Add({ TEXT("negative upward extra"), [](FTDFog3DSettings& S) { S.ObstacleUpwardExtraCm = -1.0f; } });
		Cases.Add({ TEXT("negative zone edge fade"), [](FTDFog3DSettings& S) { S.ZoneEdgeFadeCm = -1.0f; } });
		Cases.Add({ TEXT("negative entry margin"), [](FTDFog3DSettings& S) { S.ZoneEntryVerticalMarginCm = -1.0f; } });
		Cases.Add({ TEXT("fine noise amplitude 1"), [](FTDFog3DSettings& S) { S.FineNoiseAmplitude = 1.0f; } });
		Cases.Add({ TEXT("fine noise spacing zero"), [](FTDFog3DSettings& S) { S.FineNoiseSpacingCm = 0.0f; } });
		Cases.Add({ TEXT("fine noise seed too large"), [](FTDFog3DSettings& S) { S.FineNoiseSeed = 16777216; } });
		Cases.Add({ TEXT("fine noise seed at two to the twenty third"), [](FTDFog3DSettings& S) { S.FineNoiseSeed = 8388608; } });
		Cases.Add({ TEXT("stain noise seed at two to the twenty third"), [](FTDFog3DSettings& S) { S.StainNoiseSeed = 8388608; } });
		Cases.Add({ TEXT("fine noise seed negative"), [](FTDFog3DSettings& S) { S.FineNoiseSeed = -1; } });
		Cases.Add({ TEXT("stain noise amplitude negative"), [](FTDFog3DSettings& S) { S.StainNoiseAmplitude = -0.1f; } });
		Cases.Add({ TEXT("stain noise spacing negative"), [](FTDFog3DSettings& S) { S.StainNoiseSpacingCm = -5.0f; } });
		Cases.Add({ TEXT("stain noise seed too large"), [](FTDFog3DSettings& S) { S.StainNoiseSeed = 20000000; } });
		Cases.Add({ TEXT("not a number base density"), [](FTDFog3DSettings& S) { S.BaseDensity = std::numeric_limits<float>::quiet_NaN(); } });
		Cases.Add({ TEXT("infinite base thickness"), [](FTDFog3DSettings& S) { S.BaseThicknessCm = std::numeric_limits<float>::infinity(); } });
		Cases.Add({ TEXT("not a number blade tip"), [](FTDFog3DSettings& S) { S.BladeTipLocalCm.Y = std::numeric_limits<double>::quiet_NaN(); } });
		Cases.Add({ TEXT("infinite zone edge fade"), [](FTDFog3DSettings& S) { S.ZoneEdgeFadeCm = std::numeric_limits<float>::infinity(); } });
		Cases.Add({ TEXT("not a number noise spacing"), [](FTDFog3DSettings& S) { S.StainNoiseSpacingCm = std::numeric_limits<float>::quiet_NaN(); } });
		return Cases;
	}

	TArray<FTDFog3DBadZoneCase> MakeBadZoneCases()
	{
		TArray<FTDFog3DBadZoneCase> Cases;
		Cases.Add({ TEXT("side X below twice the fade"), FVector(400.0, 3000.0, 150.0) });
		Cases.Add({ TEXT("side Y below twice the fade"), FVector(3000.0, 499.0, 150.0) });
		Cases.Add({ TEXT("zero height"), FVector(3000.0, 3000.0, 0.0) });
		Cases.Add({ TEXT("negative height"), FVector(3000.0, 3000.0, -150.0) });
		Cases.Add({ TEXT("not a number"), FVector(std::numeric_limits<double>::quiet_NaN(), 3000.0, 150.0) });
		Cases.Add({ TEXT("infinite"), FVector(3000.0, std::numeric_limits<double>::infinity(), 150.0) });
		return Cases;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DSettingsValidationTest,
	"TDGame.FluidFX.Fog3D.SettingsValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DSettingsValidationTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DSettingsTestPrivate;

	TArray<FString> DefaultProblems;
	CollectTDFog3DSettingsProblems(FTDFog3DSettings(), DefaultProblems);
	TestEqual(TEXT("Header defaults have no problems"), DefaultProblems.Num(), 0);
	for (const FString& Problem : DefaultProblems)
	{
		AddError(Problem);
	}

	const FTDFog3DSettings Defaults;
	TestFalse(TEXT("The clamped time wind blend is off by default"), Defaults.bUsesClampedTimeWindBlend);
	TestEqual(TEXT("The body wind gain is 1 by default"), Defaults.BodyWindGain, 1.0f);
	TestEqual(TEXT("The blade wind gain is 1 by default"), Defaults.BladeWindGain, 1.0f);

	const TArray<FTDFog3DBadSettingsCase> BadCases = MakeBadSettingsCases();
	for (const FTDFog3DBadSettingsCase& BadCase : BadCases)
	{
		FTDFog3DSettings Mutated;
		BadCase.Mutate(Mutated);
		TArray<FString> Problems;
		CollectTDFog3DSettingsProblems(Mutated, Problems);
		TestTrue(*FString::Printf(TEXT("Rejects: %s"), *BadCase.Name), Problems.Num() >= 1);
	}

	const auto ExpectAccepted = [this](const TCHAR* Name, const FTDFog3DSettings& Settings)
	{
		TArray<FString> Problems;
		CollectTDFog3DSettingsProblems(Settings, Problems);
		TestEqual(*FString::Printf(TEXT("Accepts: %s (%s)"), Name, *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
	};
	{
		FTDFog3DSettings LargestSeeds;
		LargestSeeds.FineNoiseSeed = 8388607;
		LargestSeeds.StainNoiseSeed = 8388607;
		ExpectAccepted(TEXT("seeds of two to the twenty third minus one"), LargestSeeds);
	}
	{
		FTDFog3DSettings MarginAtLimit;
		MarginAtLimit.QualityLowResolution = 64;
		MarginAtLimit.WindowFollowMarginCells = 16;
		ExpectAccepted(TEXT("follow margin 16 equals 64 / 2 - 8 - 8"), MarginAtLimit);
	}
	{
		FTDFog3DSettings WideBandAtLimit;
		WideBandAtLimit.EdgeBandCells = 16;
		WideBandAtLimit.WindowFollowMarginCells = 40;
		ExpectAccepted(TEXT("follow margin 40 equals 128 / 2 - 16 - 8"), WideBandAtLimit);
	}

	TArray<FString> DefaultZoneProblems;
	CollectTDFog3DZoneProblems(FVector(3000.0, 3000.0, 150.0), FTDFog3DSettings(), DefaultZoneProblems);
	TestEqual(TEXT("The planned zone size has no problems"), DefaultZoneProblems.Num(), 0);

	TArray<FString> EdgeZoneProblems;
	CollectTDFog3DZoneProblems(FVector(500.0, 500.0, 1.0), FTDFog3DSettings(), EdgeZoneProblems);
	TestEqual(TEXT("A side exactly twice the fade width is accepted"), EdgeZoneProblems.Num(), 0);

	for (const FTDFog3DBadZoneCase& BadZone : MakeBadZoneCases())
	{
		TArray<FString> Problems;
		CollectTDFog3DZoneProblems(BadZone.ZoneHalfExtentCm, FTDFog3DSettings(), Problems);
		TestTrue(*FString::Printf(TEXT("Rejects zone: %s"), *BadZone.Name), Problems.Num() >= 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DSettingsTableTest,
	"TDGame.FluidFX.Fog3D.SettingsTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DSettingsTableTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DSettingsTestPrivate;

	const FTDFluidPresetTuningRegisteredStruct* Registered = UTDFluidPresetTuningSubsystem::GetRegisteredStructs().FindByPredicate([](const FTDFluidPresetTuningRegisteredStruct& Entry)
	{
		return Entry.Struct == FTDFog3DSettings::StaticStruct();
	});
	if (!TestNotNull(TEXT("The settings struct is registered"), Registered))
	{
		return false;
	}
	TestTrue(TEXT("The settings struct is global scope"), Registered->Scope == ETDFluidPresetTuningScope::Global);

	FTDFog3DSettings Scribbled;
	Scribbled.bEnabled = false;
	Scribbled.Mode = 0;
	Scribbled.QualityHighResolution = 64;
	Scribbled.BladeSocketName = TEXT("ScribbledSocket");
	Scribbled.BladeTipLocalCm = FVector(1.0, 2.0, 3.0);
	Scribbled.HandBoneName = TEXT("ScribbledBone");
	Scribbled.StainNoiseSeed = 99;
	Scribbled.bUsesClampedTimeWindBlend = !FTDFog3DSettings().bUsesClampedTimeWindBlend;
	Scribbled.BodyWindGain = 1.75f;
	Scribbled.BladeWindGain = 0.5f;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFog3DSettings::StaticStruct(), &Scribbled);

	const FTDFog3DSettings Defaults;
	TestEqual(TEXT("bEnabled comes from the table"), Scribbled.bEnabled, Defaults.bEnabled);
	TestEqual(TEXT("Mode comes from the table"), Scribbled.Mode, Defaults.Mode);
	TestEqual(TEXT("QualityHighResolution comes from the table"), Scribbled.QualityHighResolution, Defaults.QualityHighResolution);
	TestTrue(TEXT("BladeSocketName comes from the table"), Scribbled.BladeSocketName == Defaults.BladeSocketName);
	TestTrue(TEXT("BladeTipLocalCm comes from the table"), Scribbled.BladeTipLocalCm == Defaults.BladeTipLocalCm);
	TestTrue(TEXT("HandBoneName comes from the table"), Scribbled.HandBoneName == Defaults.HandBoneName);
	TestEqual(TEXT("StainNoiseSeed comes from the table"), Scribbled.StainNoiseSeed, Defaults.StainNoiseSeed);
	TestEqual(TEXT("The canonical table keeps the clamped time wind blend at its default"), Scribbled.bUsesClampedTimeWindBlend, Defaults.bUsesClampedTimeWindBlend);
	TestEqual(TEXT("The canonical table keeps the body wind gain at its default"), Scribbled.BodyWindGain, Defaults.BodyWindGain);
	TestEqual(TEXT("The canonical table keeps the blade wind gain at its default"), Scribbled.BladeWindGain, Defaults.BladeWindGain);

	TArray<FString> Problems;
	CollectTDFog3DSettingsProblems(ReadSettingsFromTable(), Problems);
	TestEqual(TEXT("The table values have no problems"), Problems.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DQualityTierTest,
	"TDGame.FluidFX.Fog3D.QualityTierResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DQualityTierTest::RunTest(const FString& Parameters)
{
	const FTDFog3DSettings Settings;

	const FTDFog3DQualityTier Low = ResolveTDFog3DQualityTier(Settings, 0);
	TestEqual(TEXT("Low resolution"), Low.Resolution, 128);
	TestEqual(TEXT("Low steps per second"), Low.StepsPerSecond, 30.0f);

	const FTDFog3DQualityTier Medium = ResolveTDFog3DQualityTier(Settings, 1);
	TestEqual(TEXT("Medium resolution"), Medium.Resolution, 256);
	TestEqual(TEXT("Medium steps per second"), Medium.StepsPerSecond, 30.0f);

	const FTDFog3DQualityTier High = ResolveTDFog3DQualityTier(Settings, 2);
	TestEqual(TEXT("High resolution"), High.Resolution, 256);
	TestEqual(TEXT("High steps per second"), High.StepsPerSecond, 60.0f);

	TestEqual(TEXT("Below range uses low"), ResolveTDFog3DQualityTier(Settings, -5).Resolution, 128);
	TestEqual(TEXT("Above range uses high"), ResolveTDFog3DQualityTier(Settings, 9).StepsPerSecond, 60.0f);

	FTDFog3DSettings Custom;
	Custom.QualityMediumResolution = 64;
	Custom.QualityMediumStepsPerSecond = 12.0f;
	const FTDFog3DQualityTier CustomMedium = ResolveTDFog3DQualityTier(Custom, 1);
	TestEqual(TEXT("Custom medium resolution"), CustomMedium.Resolution, 64);
	TestEqual(TEXT("Custom medium steps"), CustomMedium.StepsPerSecond, 12.0f);
	return true;
}

#endif
