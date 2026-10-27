#pragma once

#include "CoreMinimal.h"

struct FTDFluidHeterogeneousVolumeRenderProfile;

struct FTDFluidRenderProfileCVarBinding
{
	const TCHAR* FieldName = nullptr;
	const TCHAR* CVarName = nullptr;
};

class TDGAME_API FTDFluidRenderProfile
{
public:
	static TConstArrayView<FTDFluidRenderProfileCVarBinding> GetCVarBindings();
	static void ApplyIfTableChanged();
	static void ApplyProfile(const FTDFluidHeterogeneousVolumeRenderProfile& Profile);
	static void AcquireDriver();
	static void ReleaseDriver();
	static void RestoreAll();
	static int32 GetActiveDriverCount();
	static int32 GetOverriddenCVarCount();
};
