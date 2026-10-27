#include "FluidFX/TDFluidRenderProfile.h"
#include "Engine/Engine.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidRenderProfile, Log, All);

namespace
{
	const FTDFluidRenderProfileCVarBinding TDFluidRenderProfileBindings[] = {
		{ TEXT("MaxStepCount"), TEXT("r.HeterogeneousVolumes.MaxStepCount") },
		{ TEXT("VolumeResolutionZ"), TEXT("r.HeterogeneousVolumes.VolumeResolution.Z") },
		{ TEXT("Preshading"), TEXT("r.HeterogeneousVolumes.Preshading") },
		{ TEXT("LightingCacheDownsampleFactor"), TEXT("r.HeterogeneousVolumes.LightingCache.DownsampleFactor") },
		{ TEXT("Shadows"), TEXT("r.HeterogeneousVolumes.Shadows") },
		{ TEXT("IndirectLighting"), TEXT("r.HeterogeneousVolumes.IndirectLighting") },
		{ TEXT("AmbientOcclusion"), TEXT("r.HeterogeneousVolumes.AmbientOcclusion") },
		{ TEXT("MaxTraceDistance"), TEXT("r.HeterogeneousVolumes.MaxTraceDistance") },
		{ TEXT("Composition"), TEXT("r.HeterogeneousVolumes.Composition") },
		{ TEXT("VolumetricFog"), TEXT("r.HeterogeneousVolumes.VolumetricFog") }
	};

	struct FTDFluidSavedCVarValue
	{
		FString OriginalValue;
		EConsoleVariableFlags OriginalSetBy = ECVF_SetByConstructor;
	};

	TMap<FString, FTDFluidSavedCVarValue> TDFluidSavedCVarValues;
	TSet<FString> TDFluidWarnedMissingCVars;
	int32 TDFluidAppliedTableGeneration = INDEX_NONE;
	int32 TDFluidActiveDriverCount = 0;

	bool TDFluidReadProfileField(const FTDFluidHeterogeneousVolumeRenderProfile& Profile, const TCHAR* FieldName, double& OutValue, FString& OutText)
	{
		const FProperty* Property = FTDFluidHeterogeneousVolumeRenderProfile::StaticStruct()->FindPropertyByName(FName(FieldName));
		const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
		if (!NumericProperty)
		{
			return false;
		}
		const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(&Profile);
		if (NumericProperty->IsFloatingPoint())
		{
			OutValue = NumericProperty->GetFloatingPointPropertyValue(ValuePtr);
			OutText = FString::SanitizeFloat(OutValue);
			return true;
		}
		const int64 IntegerValue = NumericProperty->GetSignedIntPropertyValue(ValuePtr);
		OutValue = static_cast<double>(IntegerValue);
		OutText = FString::Printf(TEXT("%lld"), IntegerValue);
		return true;
	}

	IConsoleVariable* TDFluidFindProfileCVar(const TCHAR* CVarName)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(CVarName);
		if (!CVar && !TDFluidWarnedMissingCVars.Contains(CVarName))
		{
			TDFluidWarnedMissingCVars.Add(CVarName);
			UE_LOG(LogTDFluidRenderProfile, Warning, TEXT("Render profile console variable %s is not registered; the field is ignored"), CVarName);
		}
		return CVar;
	}

	void TDFluidRestoreCVar(const FString& CVarName, const FTDFluidSavedCVarValue& Saved)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
		if (!CVar)
		{
			return;
		}
		if (Saved.OriginalSetBy > ECVF_SetByCode)
		{
			return;
		}
		if (Saved.OriginalSetBy == ECVF_SetByCode)
		{
			CVar->Set(*Saved.OriginalValue, ECVF_SetByCode);
			return;
		}
		CVar->Unset(ECVF_SetByCode);
		const EConsoleVariableFlags CurrentSetBy = static_cast<EConsoleVariableFlags>(CVar->GetFlags() & ECVF_SetByMask);
		if (CurrentSetBy == Saved.OriginalSetBy && CVar->GetString() == Saved.OriginalValue)
		{
			return;
		}
		CVar->Set(*Saved.OriginalValue, ECVF_SetByCode);
	}

	void TDFluidApplyBinding(const FTDFluidRenderProfileCVarBinding& Binding, const FTDFluidHeterogeneousVolumeRenderProfile& Profile)
	{
		double Value = -1.0;
		FString ValueText;
		if (!TDFluidReadProfileField(Profile, Binding.FieldName, Value, ValueText))
		{
			return;
		}
		const FString CVarName(Binding.CVarName);
		if (Value < 0.0)
		{
			const FTDFluidSavedCVarValue* Saved = TDFluidSavedCVarValues.Find(CVarName);
			if (!Saved)
			{
				return;
			}
			TDFluidRestoreCVar(CVarName, *Saved);
			TDFluidSavedCVarValues.Remove(CVarName);
			return;
		}
		IConsoleVariable* CVar = TDFluidFindProfileCVar(Binding.CVarName);
		if (!CVar)
		{
			return;
		}
		if (!TDFluidSavedCVarValues.Contains(CVarName))
		{
			FTDFluidSavedCVarValue& Saved = TDFluidSavedCVarValues.Add(CVarName);
			Saved.OriginalValue = CVar->GetString();
			Saved.OriginalSetBy = static_cast<EConsoleVariableFlags>(CVar->GetFlags() & ECVF_SetByMask);
		}
		CVar->Set(*ValueText, ECVF_SetByCode);
	}
}

TConstArrayView<FTDFluidRenderProfileCVarBinding> FTDFluidRenderProfile::GetCVarBindings()
{
	return MakeArrayView(TDFluidRenderProfileBindings);
}

void FTDFluidRenderProfile::ApplyIfTableChanged()
{
	const UTDFluidPresetTuningSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	const int32 Generation = Subsystem ? Subsystem->GetTableGeneration() : 0;
	if (Generation == TDFluidAppliedTableGeneration)
	{
		return;
	}
	TDFluidAppliedTableGeneration = Generation;
	FTDFluidHeterogeneousVolumeRenderProfile Profile;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFluidHeterogeneousVolumeRenderProfile::StaticStruct(), &Profile);
	ApplyProfile(Profile);
}

void FTDFluidRenderProfile::ApplyProfile(const FTDFluidHeterogeneousVolumeRenderProfile& Profile)
{
	for (const FTDFluidRenderProfileCVarBinding& Binding : TDFluidRenderProfileBindings)
	{
		TDFluidApplyBinding(Binding, Profile);
	}
}

void FTDFluidRenderProfile::AcquireDriver()
{
	++TDFluidActiveDriverCount;
}

void FTDFluidRenderProfile::ReleaseDriver()
{
	if (TDFluidActiveDriverCount <= 0)
	{
		return;
	}
	--TDFluidActiveDriverCount;
	if (TDFluidActiveDriverCount == 0)
	{
		RestoreAll();
	}
}

void FTDFluidRenderProfile::RestoreAll()
{
	for (const TPair<FString, FTDFluidSavedCVarValue>& Saved : TDFluidSavedCVarValues)
	{
		TDFluidRestoreCVar(Saved.Key, Saved.Value);
	}
	TDFluidSavedCVarValues.Reset();
	TDFluidAppliedTableGeneration = INDEX_NONE;
}

int32 FTDFluidRenderProfile::GetActiveDriverCount()
{
	return TDFluidActiveDriverCount;
}

int32 FTDFluidRenderProfile::GetOverriddenCVarCount()
{
	return TDFluidSavedCVarValues.Num();
}
