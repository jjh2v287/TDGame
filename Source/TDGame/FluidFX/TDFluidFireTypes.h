#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidFireTypes.generated.h"

class ATDFluidLiveActor;

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidSourceEmitterSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bIsEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidBrushChannel Channel = ETDFluidBrushChannel::ChannelB_Fire;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float RadiusCm = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DensityStrength = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
	float EmitSpeedCmPerSec = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MotionInertiaScale = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlickerAmplitude = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float FlickerFrequencyHz = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector SourceOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SourceStampCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float SourceSpreadCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrailLeadSeconds = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float MaxTrailLeadRatio = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	int32 RandomSeed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeatStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "4"))
	int32 SubstepCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float CoreRadiusCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoreDensityStrength = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoreHeatStrength = 1.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidDownstreamFeedSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bIsEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float BandStartRatio = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinFeedDensity = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FadedDensityThreshold = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxStampsPerStep = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DensityScale = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1"))
	float RadiusScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float VelocityInheritance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeatScale = 0.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidFragmentTrailSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1"))
	float TrackingDurationSeconds = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float ArmedPollIntervalSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bShouldCrumbleOnTrigger = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bShouldTrackWithWorldClock = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float MinFragmentSpeedCmPerSec = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float MaxFragmentSpeedCmPerSec = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float FireStampRadiusCm = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FireDensityStrength = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FireTrailHeatScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FireTrailDensityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float SmokeStampRadiusCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeDensityStrength = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "7"))
	int32 SmokeTrailLagFrames = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1"))
	int32 MaxFragmentsPerStep = 48;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1"))
	int32 MaxStampsPerFragment = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float VelocityInheritance = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float BurstCoreRadiusCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float BurstRingRadiusCm = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "32"))
	int32 BurstRingStampCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float BurstRingSpeedCmPerSec = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float BurstSustainSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "12"))
	int32 BurstLobeCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float BurstLobeCenterRatioMin = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float BurstLobeCenterRatioMax = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.05"))
	float BurstLobeRadiusRatioMin = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.05"))
	float BurstLobeRadiusRatioMax = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float BurstLobeSpeedMinCmPerSec = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float BurstLobeSpeedMaxCmPerSec = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	int32 BurstLobeSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "512"))
	int32 EmberBurstCount = 80;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float EmberBurstHeightOffsetCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FireHeatStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeHeatStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bSustainsBurstHeat = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float BurstCenterHeightOffsetCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float RestingHeatSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RestingHeatStrength = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float RestingHeatRadiusCm = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RestingDensityStrength = 0.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidFragmentTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail")
	TObjectPtr<ATDFluidLiveActor> TargetActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail")
	ETDFluidBrushChannel Channel = ETDFluidBrushChannel::ChannelR_Mud;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail", meta = (ClampMin = "0.0"))
	float DensityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail", meta = (ClampMin = "0.0"))
	float HeatScale = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail", meta = (ClampMin = "0.1"))
	float RadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail", meta = (ClampMin = "0.1"))
	float BurstRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail")
	bool bReceivesBurst = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail")
	bool bReceivesTrails = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail")
	bool bUsesLaggedPositions = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail", meta = (ClampMin = "0.0"))
	float HeightFalloffCm = 0.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidFlameCardLayerSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireCards", meta = (ClampMin = "0", ClampMax = "2"))
	int32 ExtraCardCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireCards", meta = (ClampMin = "0.0"))
	float CardDepthSpacingCm = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireCards", meta = (ClampMin = "0.5", ClampMax = "1.5"))
	float CardScaleStep = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireCards", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float CardIntensityScale = 0.7f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidFragmentGlowSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow", meta = (ClampMin = "0.0"))
	float HoldSeconds = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow", meta = (ClampMin = "0.0"))
	float CoolSeconds = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CharredHeatRatio = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow", meta = (ClampMin = "0.0"))
	float GlowIntensity = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow", meta = (ClampMin = "0.0"))
	float FireHeatWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow", meta = (ClampMin = "1.0"))
	float FireHeatSlabCm = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow")
	FLinearColor ColdGlowColor = FLinearColor(1.0f, 0.012f, 0.03f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow")
	FLinearColor HotGlowColor = FLinearColor(1.0f, 0.03f, 0.035f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FacingContrast = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow", meta = (ClampMin = "0.001", ClampMax = "0.5"))
	float EdgeCharWidth = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentGlow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeCharStrength = 0.5f;
};
