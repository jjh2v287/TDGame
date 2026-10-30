#include "Engine/Engine.h"
#include "FluidFX/TDFluidOutputQuality.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidOutputQualityTestPrivate
{
	const TCHAR* const OutputQualityVariableName = TEXT("TD.FluidFX.OutputQuality");
	const TCHAR* const OutputQualitySmokeSoftPath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidVolume_SmokeSoft.MI_TD_FluidVolume_SmokeSoft");
	const FName OutputQualityStepCountName(TEXT("StepCount"));
	const FName OutputQualityShadowStepsName(TEXT("ShadowSteps"));
	constexpr int32 OutputQualityReducedLevel = 1;

	FTDFluidOutputQualitySettings ParseOutputQualitySettings()
	{
		FTDFluidOutputQualitySettings Settings;
		UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFluidOutputQualitySettings::StaticStruct(), &Settings);
		return Settings;
	}

	bool ReadBaseLoopValue(const UMaterialInterface& SourceMaterial, const FTDFluidMaterialLookEntry* Look, FName ScalarName, float& OutValue)
	{
		if (const float* LookValue = Look ? Look->Scalars.Find(ScalarName) : nullptr)
		{
			OutValue = *LookValue;
			return true;
		}
		return SourceMaterial.GetScalarParameterValue(FHashedMaterialParameterInfo(ScalarName), OutValue);
	}

	float ReadMaterialScalar(const UMaterialInstanceDynamic& Material, FName ScalarName)
	{
		float Value = -1.0f;
		Material.GetScalarParameterValue(FHashedMaterialParameterInfo(ScalarName), Value);
		return Value;
	}

	void SetOutputQualityLevel(IConsoleVariable& QualityVariable, int32 Level)
	{
		QualityVariable.Set(Level, ECVF_SetByCode);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOutputQualityLoopScaleMathTest,
	"TDGame.FluidFX.OutputQuality.LoopScaleMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOutputQualityLoopScaleMathTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOutputQualityTestPrivate;

	TestEqual(TEXT("48 loop steps at half scale become 24"), FTDFluidOutputQuality::ComputeScaledLoopValue(48.0f, 0.5f, 2.0f), 24.0f);
	TestEqual(TEXT("A scaled loop never drops below the minimum"), FTDFluidOutputQuality::ComputeScaledLoopValue(3.0f, 0.35f, 2.0f), 2.0f);
	TestEqual(TEXT("Full scale keeps the authored value even below the minimum"), FTDFluidOutputQuality::ComputeScaledLoopValue(1.0f, 1.0f, 2.0f), 1.0f);

	const FTDFluidOutputQualitySettings Settings = ParseOutputQualitySettings();
	TestTrue(TEXT("StepCount is a loop scalar"), FTDFluidOutputQuality::GetLoopScalarNames().Contains(OutputQualityStepCountName));
	TestTrue(TEXT("ShadowSteps is a loop scalar"), FTDFluidOutputQuality::GetLoopScalarNames().Contains(OutputQualityShadowStepsName));
	TestTrue(TEXT("Level scales rise with the level and stay at or below 1"), Settings.Level0LoopScale <= Settings.Level1LoopScale && Settings.Level1LoopScale <= Settings.Level2LoopScale && Settings.Level2LoopScale <= 1.0f);
	TestTrue(TEXT("Minimum loop scalar is at least 1"), Settings.MinimumLoopScalar >= 1.0f);
	TestTrue(TEXT("Reduced level scale is below 1"), FTDFluidOutputQuality::ResolveLoopScale(OutputQualityReducedLevel, Settings) < 1.0f);
	TestEqual(TEXT("Highest level scale is 1"), FTDFluidOutputQuality::ResolveLoopScale(FTDFluidOutputQuality::HighestLevel, Settings), 1.0f);

	UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	IConsoleVariable* QualityVariable = IConsoleManager::Get().FindConsoleVariable(OutputQualityVariableName);
	if (!TestNotNull(TEXT("Preset tuning subsystem exists"), TuningSubsystem) || !TestNotNull(TEXT("Output quality console variable exists"), QualityVariable))
	{
		return false;
	}
	const FString OriginalLevelText = QualityVariable->GetString();
	ON_SCOPE_EXIT
	{
		QualityVariable->Set(*OriginalLevelText, ECVF_SetByCode);
		IConsoleManager::Get().CallAllConsoleVariableSinks();
	};

	SetOutputQualityLevel(*QualityVariable, FTDFluidOutputQuality::HighestLevel);
	IConsoleManager::Get().CallAllConsoleVariableSinks();
	const int32 BaseGeneration = TuningSubsystem->GetTableGeneration();
	TestEqual(TEXT("Highest level resolves to itself"), FTDFluidOutputQuality::ResolveLevel(), FTDFluidOutputQuality::HighestLevel);

	SetOutputQualityLevel(*QualityVariable, OutputQualityReducedLevel);
	IConsoleManager::Get().CallAllConsoleVariableSinks();
	TestEqual(TEXT("Lowering the level reloads the tables once"), TuningSubsystem->GetTableGeneration(), BaseGeneration + 1);

	SetOutputQualityLevel(*QualityVariable, FTDFluidOutputQuality::HighestLevel);
	IConsoleManager::Get().CallAllConsoleVariableSinks();
	TestEqual(TEXT("Restoring the level reloads the tables again"), TuningSubsystem->GetTableGeneration(), BaseGeneration + 2);

	SetOutputQualityLevel(*QualityVariable, FTDFluidOutputQuality::HighestLevel);
	IConsoleManager::Get().CallAllConsoleVariableSinks();
	TestEqual(TEXT("Setting the same level does not reload"), TuningSubsystem->GetTableGeneration(), BaseGeneration + 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOutputQualityVolumeLoopScalarsTest,
	"TDGame.FluidFX.OutputQuality.VolumeLoopScalarsScaleAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOutputQualityVolumeLoopScalarsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOutputQualityTestPrivate;

	IConsoleVariable* QualityVariable = IConsoleManager::Get().FindConsoleVariable(OutputQualityVariableName);
	if (!TestNotNull(TEXT("Output quality console variable exists"), QualityVariable))
	{
		return false;
	}
	const FString OriginalLevelText = QualityVariable->GetString();
	ON_SCOPE_EXIT
	{
		QualityVariable->Set(*OriginalLevelText, ECVF_SetByCode);
	};

	UMaterialInterface* SourceMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(OutputQualitySmokeSoftPath)).LoadSynchronous();
	if (!TestNotNull(TEXT("SmokeSoft volume material instance loads"), SourceMaterial))
	{
		return false;
	}
	const FTDFluidMaterialLookEntry* Look = UTDFluidPresetTuningSubsystem::FindMaterialLook(SourceMaterial->GetPackage()->GetFName());
	float BaseStepCount = 0.0f;
	float BaseShadowSteps = 0.0f;
	const bool bHasStepCount = ReadBaseLoopValue(*SourceMaterial, Look, OutputQualityStepCountName, BaseStepCount);
	const bool bHasShadowSteps = ReadBaseLoopValue(*SourceMaterial, Look, OutputQualityShadowStepsName, BaseShadowSteps);
	if (!TestTrue(TEXT("SmokeSoft exposes StepCount and ShadowSteps"), bHasStepCount && bHasShadowSteps))
	{
		return false;
	}

	SetOutputQualityLevel(*QualityVariable, FTDFluidOutputQuality::HighestLevel);
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(SourceMaterial, GetTransientPackage());
	UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*Material);
	TestEqual(TEXT("Highest level sets no loop scalar after the first look application"), FTDFluidOutputQuality::ApplyToMaterial(*Material, Look), 0);
	TestEqual(TEXT("Highest level keeps StepCount"), ReadMaterialScalar(*Material, OutputQualityStepCountName), BaseStepCount);
	TestEqual(TEXT("Highest level keeps ShadowSteps"), ReadMaterialScalar(*Material, OutputQualityShadowStepsName), BaseShadowSteps);

	const FTDFluidOutputQualitySettings Settings = ParseOutputQualitySettings();
	const float ReducedScale = FTDFluidOutputQuality::ResolveLoopScale(OutputQualityReducedLevel, Settings);
	const float ExpectedStepCount = FTDFluidOutputQuality::ComputeScaledLoopValue(BaseStepCount, ReducedScale, Settings.MinimumLoopScalar);
	const float ExpectedShadowSteps = FTDFluidOutputQuality::ComputeScaledLoopValue(BaseShadowSteps, ReducedScale, Settings.MinimumLoopScalar);
	TestTrue(TEXT("Reduced level lowers the authored StepCount"), ExpectedStepCount < BaseStepCount);

	SetOutputQualityLevel(*QualityVariable, OutputQualityReducedLevel);
	TestTrue(TEXT("Reduced level sets loop scalars"), FTDFluidOutputQuality::ApplyToMaterial(*Material, Look) > 0);
	TestEqual(TEXT("Reduced level scales StepCount"), ReadMaterialScalar(*Material, OutputQualityStepCountName), ExpectedStepCount);
	TestEqual(TEXT("Reduced level scales ShadowSteps"), ReadMaterialScalar(*Material, OutputQualityShadowStepsName), ExpectedShadowSteps);
	TestEqual(TEXT("Reapplying the same level sets nothing"), FTDFluidOutputQuality::ApplyToMaterial(*Material, Look), 0);
	UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*Material);
	TestEqual(TEXT("Full look reapply keeps the scaled StepCount"), ReadMaterialScalar(*Material, OutputQualityStepCountName), ExpectedStepCount);

	SetOutputQualityLevel(*QualityVariable, FTDFluidOutputQuality::HighestLevel);
	FTDFluidOutputQuality::ApplyToMaterial(*Material, Look);
	TestEqual(TEXT("Highest level restores StepCount"), ReadMaterialScalar(*Material, OutputQualityStepCountName), BaseStepCount);
	TestEqual(TEXT("Highest level restores ShadowSteps"), ReadMaterialScalar(*Material, OutputQualityShadowStepsName), BaseShadowSteps);
	return true;
}

#endif
