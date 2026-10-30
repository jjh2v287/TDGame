#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureDefines.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidRuntimeOps.generated.h"

class UTextureRenderTarget2D;
class UWorld;

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidExternalOutputTargets
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalOutput")
	TSoftObjectPtr<UTextureRenderTarget2D> DensityPressureTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalOutput")
	TSoftObjectPtr<UTextureRenderTarget2D> VelocityTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalOutput")
	TSoftObjectPtr<UTextureRenderTarget2D> AuxTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalOutput")
	TSoftObjectPtr<UTextureRenderTarget2D> WaterSurfaceTarget;

	bool HasAnyTarget() const;
};

struct FTDFluidStepClock
{
	float RateClockSeconds = 0.0f;
	float ElapsedSinceStepSeconds = 0.0f;
};

namespace TDFluidRuntimeOps
{
	TDGAME_API float AdvanceStepClock(FTDFluidStepClock& InOutClock, float FrameDeltaSeconds, float StepRateHz);
	TDGAME_API float ResolveMaxStepRateHz(const FTDFluidRuntimeOpsSettings& Settings);
	TDGAME_API float ComputeLodStepRateHz(const FTDFluidRuntimeOpsSettings& Settings, float MaxStepRateHz, double ObserverDistanceCm);
	TDGAME_API int32 ResolvePauseWhenNotVisibleMode(const FTDFluidRuntimeOpsSettings& Settings);
	TDGAME_API bool CanJudgeVisibility(const UWorld& World);
	TDGAME_API bool ShouldPauseForVisibility(int32 PauseMode, bool bIsEligible, bool bCanJudgeVisibility, bool bWasRecentlyRendered);
	TDGAME_API bool IsUsableExternalOutputTarget(const UTextureRenderTarget2D* Target, int32 Resolution, const FLinearColor& ExpectedClearColor, TextureAddress ExpectedAddress);
	TDGAME_API bool IsSharedMpcArbitrationEnabled();
	TDGAME_API FTDFluidSimulationConfig MakeCellSizeNormalizedConfig(const FTDFluidSimulationConfig& Config, int32 EffectiveResolution, const FTDFluidRuntimeOpsSettings& Settings, bool bScalesPressureIterations);
}
