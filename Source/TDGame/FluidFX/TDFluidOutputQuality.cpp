#include "FluidFX/TDFluidOutputQuality.h"
#include "Engine/Engine.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidOutputQuality, Log, All);

namespace TDFluidOutputQualityPrivate
{
	constexpr int32 OutputQualityFollowEffectsQuality = -1;

	TAutoConsoleVariable<int32> CVarTDFluidOutputQuality(
		TEXT("TD.FluidFX.OutputQuality"),
		FTDFluidOutputQuality::HighestLevel,
		TEXT("Fluid output quality level 0..3 that scales material loop scalars (3 = authored values). -1 follows sg.EffectsQuality. A change of the resolved level reloads the preset tuning tables."),
		ECVF_Default);

	int32 OutputQualityAppliedLevel = INDEX_NONE;
	int32 OutputQualitySettingsGeneration = INDEX_NONE;
	FTDFluidOutputQualitySettings OutputQualityCachedSettings;

	UTDFluidPresetTuningSubsystem* FindOutputQualityTuningSubsystem()
	{
		return GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	}

	const FTDFluidOutputQualitySettings& ResolveOutputQualitySettings()
	{
		const UTDFluidPresetTuningSubsystem* TuningSubsystem = FindOutputQualityTuningSubsystem();
		if (!TuningSubsystem)
		{
			return OutputQualityCachedSettings;
		}
		const int32 Generation = TuningSubsystem->GetTableGeneration();
		if (Generation == OutputQualitySettingsGeneration)
		{
			return OutputQualityCachedSettings;
		}
		OutputQualitySettingsGeneration = Generation;
		OutputQualityCachedSettings = FTDFluidOutputQualitySettings();
		UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFluidOutputQualitySettings::StaticStruct(), &OutputQualityCachedSettings);
		return OutputQualityCachedSettings;
	}

	void ReloadTuningForOutputQualityChange()
	{
		const int32 Level = FTDFluidOutputQuality::ResolveLevel();
		if (OutputQualityAppliedLevel == INDEX_NONE)
		{
			OutputQualityAppliedLevel = Level;
			return;
		}
		if (Level == OutputQualityAppliedLevel)
		{
			return;
		}
		UTDFluidPresetTuningSubsystem* TuningSubsystem = FindOutputQualityTuningSubsystem();
		if (!TuningSubsystem)
		{
			return;
		}
		OutputQualityAppliedLevel = Level;
		const int32 PreviousGeneration = TuningSubsystem->GetTableGeneration();
		const int32 ErrorCount = TuningSubsystem->IsUsingOverridePaths()
			? TuningSubsystem->ReloadPresetTuningFromPaths(TuningSubsystem->GetTuningSourcePath(), TuningSubsystem->GetLookSourcePath())
			: TuningSubsystem->ReloadPresetTuning();
		if (TuningSubsystem->GetTableGeneration() != PreviousGeneration)
		{
			return;
		}
		UE_LOG(LogTDFluidOutputQuality, Warning, TEXT("Fluid output quality level %d is not applied: the preset tuning reload failed with %d errors and kept the previous tables."), Level, ErrorCount);
	}

	FAutoConsoleVariableSink CVarTDFluidOutputQualitySink(FConsoleCommandDelegate::CreateStatic(&ReloadTuningForOutputQualityChange));
}

TConstArrayView<FName> FTDFluidOutputQuality::GetLoopScalarNames()
{
	static const FName LoopScalarNames[] = {TEXT("StepCount"), TEXT("ShadowSteps"), TEXT("TrackPomMinSteps"), TEXT("TrackPomMaxSteps"), TEXT("TrackPomShadowSteps")};
	return MakeArrayView(LoopScalarNames);
}

int32 FTDFluidOutputQuality::ResolveLevel()
{
	using namespace TDFluidOutputQualityPrivate;
	const int32 RequestedLevel = CVarTDFluidOutputQuality.GetValueOnGameThread();
	if (RequestedLevel != OutputQualityFollowEffectsQuality)
	{
		return FMath::Clamp(RequestedLevel, 0, HighestLevel);
	}
	const IConsoleVariable* EffectsQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
	return EffectsQuality ? FMath::Clamp(EffectsQuality->GetInt(), 0, HighestLevel) : HighestLevel;
}

float FTDFluidOutputQuality::ResolveLoopScale(int32 Level, const FTDFluidOutputQualitySettings& Settings)
{
	switch (Level)
	{
	case 0:
		return Settings.Level0LoopScale;
	case 1:
		return Settings.Level1LoopScale;
	case 2:
		return Settings.Level2LoopScale;
	default:
		return 1.0f;
	}
}

float FTDFluidOutputQuality::ComputeScaledLoopValue(float Base, float Scale, float Minimum)
{
	if (Scale >= 1.0f)
	{
		return Base;
	}
	return FMath::Max(Minimum, FMath::RoundToFloat(Base * Scale));
}

int32 FTDFluidOutputQuality::ApplyToMaterial(UMaterialInstanceDynamic& Material, const FTDFluidMaterialLookEntry* Look)
{
	const UMaterialInterface* Parent = Material.Parent;
	if (!Parent)
	{
		return 0;
	}
	const FTDFluidOutputQualitySettings& Settings = TDFluidOutputQualityPrivate::ResolveOutputQualitySettings();
	const float Scale = ResolveLoopScale(ResolveLevel(), Settings);
	int32 SetCount = 0;
	for (const FName& ScalarName : GetLoopScalarNames())
	{
		const FHashedMaterialParameterInfo ParameterInfo(ScalarName);
		float BaseValue = 0.0f;
		const float* LookValue = Look ? Look->Scalars.Find(ScalarName) : nullptr;
		if (LookValue)
		{
			BaseValue = *LookValue;
		}
		else if (!Parent->GetScalarParameterValue(ParameterInfo, BaseValue))
		{
			continue;
		}
		const float TargetValue = ComputeScaledLoopValue(BaseValue, Scale, Settings.MinimumLoopScalar);
		float CurrentValue = 0.0f;
		if (Material.GetScalarParameterValue(ParameterInfo, CurrentValue) && CurrentValue == TargetValue)
		{
			continue;
		}
		Material.SetScalarParameterValue(ScalarName, TargetValue);
		++SetCount;
	}
	return SetCount;
}
