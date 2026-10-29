#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidViscousKindTestPrivate
{
	struct FTDFluidViscousKindExpectation
	{
		ETDFluidPresetKind Kind;
		ETDFluidBrushChannel Channel;
	};

	const FTDFluidViscousKindExpectation NewKindExpectations[] = {
		{ ETDFluidPresetKind::ViscousMud, ETDFluidBrushChannel::ChannelR_Mud },
		{ ETDFluidPresetKind::ViscousBlood, ETDFluidBrushChannel::ChannelG_Blood },
		{ ETDFluidPresetKind::LavaTile, ETDFluidBrushChannel::ChannelR_Mud },
		{ ETDFluidPresetKind::LavaLocal, ETDFluidBrushChannel::ChannelR_Mud },
	};

	const ETDFluidPresetKind ViscousGroupKinds[] = { ETDFluidPresetKind::ViscousMud, ETDFluidPresetKind::ViscousBlood };

	bool UsesDenseDensity(const FTDFluidSimulationConfig& Config)
	{
		return Config.DensityAccumulationPerSec > 0.0f && Config.DenseDensityCeiling > 1.0f;
	}

	bool HasDefaultViscousFields(ETDFluidPresetKind Kind)
	{
		const FTDFluidSimulationConfig DefaultConfig;
		const FTDFluidInputBrushSettings DefaultBrush;
		const FTDFluidSurfaceRenderSettings DefaultSurface;
		const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		const FTDFluidInputBrushSettings Brush = UTDFluidPresetAsset::BuildInputBrushForPresetKind(Kind);
		const FTDFluidSurfaceRenderSettings Surface = UTDFluidPresetAsset::BuildSurfaceRenderForPresetKind(Kind);
		return Config.DensityAccumulationPerSec == DefaultConfig.DensityAccumulationPerSec
			&& Config.DenseDensityCeiling == DefaultConfig.DenseDensityCeiling
			&& Config.DenseGrowthThreshold == DefaultConfig.DenseGrowthThreshold
			&& Config.FlowNoiseSpeedMaskReferenceCmPerSec == DefaultConfig.FlowNoiseSpeedMaskReferenceCmPerSec
			&& Brush.BrushVelocityScale == DefaultBrush.BrushVelocityScale
			&& Surface.bUsesDecalOutput == DefaultSurface.bUsesDecalOutput
			&& Surface.DecalBoxHeightCm == DefaultSurface.DecalBoxHeightCm;
	}

	FString DescribeKind(ETDFluidPresetKind Kind)
	{
		return StaticEnum<ETDFluidPresetKind>()->GetNameStringByValue(static_cast<int64>(Kind));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousKindsBuildersAndFamiliesTest,
	"TDGame.FluidFX.Viscous.Kinds.BuildersAndFamilies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousKindsBuildersAndFamiliesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidViscousKindTestPrivate;

	const FTDFluidSimulationConfig DefaultConfig;
	const FTDFluidInputBrushSettings DefaultBrush;
	const FTDFluidSurfaceRenderSettings DefaultSurface;

	TestEqual(TEXT("Dense accumulation is off by default"), DefaultConfig.DensityAccumulationPerSec, 0.0f);
	TestEqual(TEXT("Dense ceiling is one by default"), DefaultConfig.DenseDensityCeiling, 1.0f);
	TestEqual(TEXT("Flow noise speed mask is off by default"), DefaultConfig.FlowNoiseSpeedMaskReferenceCmPerSec, 0.0f);
	TestEqual(TEXT("Brush velocity scale is one by default"), DefaultBrush.BrushVelocityScale, 1.0f);
	TestFalse(TEXT("Decal output is off by default"), DefaultSurface.bUsesDecalOutput);

	for (const FTDFluidViscousKindExpectation& Expectation : NewKindExpectations)
	{
		const FString KindName = DescribeKind(Expectation.Kind);
		TestFalse(*FString::Printf(TEXT("%s is not a fire family kind"), *KindName), UTDFluidPresetAsset::IsFireFamilyPresetKind(Expectation.Kind));
		const UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), Expectation.Kind);
		if (!TestNotNull(*FString::Printf(TEXT("%s transient preset"), *KindName), Preset))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s default channel"), *KindName), Preset->DefaultChannel == Expectation.Channel);
		TestTrue(*FString::Printf(TEXT("%s transient config matches the builder"), *KindName), Preset->SimulationConfig.DenseDensityCeiling == UTDFluidPresetAsset::BuildConfigForPresetKind(Expectation.Kind).DenseDensityCeiling);
	}

	const FTDFluidSimulationConfig FirstViscousConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ViscousGroupKinds[0]);
	for (const ETDFluidPresetKind Kind : ViscousGroupKinds)
	{
		const FString KindName = DescribeKind(Kind);
		const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		const FTDFluidInputBrushSettings Brush = UTDFluidPresetAsset::BuildInputBrushForPresetKind(Kind);
		TestTrue(*FString::Printf(TEXT("%s runs the fluid simulation"), *KindName), Config.SimMode == ETDFluidSimMode::FluidSim);
		TestTrue(*FString::Printf(TEXT("%s prefers the GPU backend"), *KindName), Config.SolverBackend == ETDFluidSolverBackend::Gpu);
		TestTrue(*FString::Printf(TEXT("%s has a smaller CPU fallback grid"), *KindName), Config.CpuFallbackResolution > 0 && Config.CpuFallbackResolution < Config.GridResolution);
		TestTrue(*FString::Printf(TEXT("%s uses dense density"), *KindName), UsesDenseDensity(Config));
		TestTrue(*FString::Printf(TEXT("%s masks flow noise by speed"), *KindName), Config.FlowNoiseSpeedMaskReferenceCmPerSec > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s fades slow brushes"), *KindName), Brush.DensityFadeBelowSpeedCmPerSec > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s boosts brush velocity"), *KindName), Brush.BrushVelocityScale > DefaultBrush.BrushVelocityScale);
		TestTrue(*FString::Printf(TEXT("%s shares the group grid"), *KindName), Config.GridResolution == FirstViscousConfig.GridResolution);
		TestTrue(*FString::Printf(TEXT("%s shares the group ceiling"), *KindName), Config.DenseDensityCeiling == FirstViscousConfig.DenseDensityCeiling);
	}

	const FTDFluidSurfaceRenderSettings BloodSurface = UTDFluidPresetAsset::BuildSurfaceRenderForPresetKind(ETDFluidPresetKind::ViscousBlood);
	TestTrue(TEXT("Viscous blood outputs a decal"), BloodSurface.bUsesDecalOutput);
	TestTrue(TEXT("Viscous blood decal box is deeper than the default"), BloodSurface.DecalBoxHeightCm > DefaultSurface.DecalBoxHeightCm);
	TestFalse(TEXT("Viscous mud has no decal output"), UTDFluidPresetAsset::BuildSurfaceRenderForPresetKind(ETDFluidPresetKind::ViscousMud).bUsesDecalOutput);

	const FTDFluidSimulationConfig LavaTileConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::LavaTile);
	TestTrue(TEXT("Lava tile wraps its area"), LavaTileConfig.AreaAddressMode == ETDFluidAreaAddressMode::Wrap);
	TestTrue(TEXT("Lava tile prefers the GPU backend"), LavaTileConfig.SolverBackend == ETDFluidSolverBackend::Gpu);
	TestFalse(TEXT("Lava tile keeps dense density off"), UsesDenseDensity(LavaTileConfig));
	TestTrue(TEXT("Lava tile keeps an ambient crust"), LavaTileConfig.AmbientDensity > 0.0f);

	const FTDFluidSimulationConfig LavaLocalConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::LavaLocal);
	TestTrue(TEXT("Lava local clamps its area"), LavaLocalConfig.AreaAddressMode == ETDFluidAreaAddressMode::Clamp);
	TestTrue(TEXT("Lava local uses dense density"), UsesDenseDensity(LavaLocalConfig));
	TestTrue(TEXT("Lava local masks flow noise by speed"), LavaLocalConfig.FlowNoiseSpeedMaskReferenceCmPerSec > 0.0f);

	int32 ChangedOlderKindCount = 0;
	for (uint8 KindValue = 0; KindValue <= static_cast<uint8>(ETDFluidPresetKind::SandDust); ++KindValue)
	{
		const ETDFluidPresetKind Kind = static_cast<ETDFluidPresetKind>(KindValue);
		if (!HasDefaultViscousFields(Kind))
		{
			AddError(FString::Printf(TEXT("%s changed a viscous field away from the code default"), *DescribeKind(Kind)));
			++ChangedOlderKindCount;
		}
	}
	TestEqual(TEXT("Older kinds keep every viscous field at the code default"), ChangedOlderKindCount, 0);
	return true;
}

#endif
