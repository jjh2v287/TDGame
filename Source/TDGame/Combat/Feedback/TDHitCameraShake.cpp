#include "Combat/Feedback/TDHitCameraShake.h"

#include "Camera/PlayerCameraManager.h"
#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Engine/World.h"
#include "Misc/App.h"

namespace TDHitCameraShakeNoise
{
	constexpr float ChannelOffsets[] = { 0.37f, 13.71f, 27.13f, 41.53f, 59.89f, 73.27f };
	constexpr double MeasureHalfScreenWidthPx = 960.;
	constexpr float DefaultFieldOfViewDegrees = 90.f;
}

float TDHitCameraShakeMath::ComputeShakeAmount(const float Trauma)
{
	const float ClampedTrauma = FMath::Clamp(Trauma, 0.f, 1.f);
	return ClampedTrauma * FMath::Sqrt(ClampedTrauma);
}

float TDHitCameraShakeMath::CombineChannelTrauma(const TConstArrayView<float> ChannelTrauma, const float OtherChannelWeight, const float TraumaCap)
{
	float StrongestTrauma = 0.f;
	float TotalTrauma = 0.f;
	for (const float Trauma : ChannelTrauma)
	{
		const float PositiveTrauma = FMath::Max(Trauma, 0.f);
		StrongestTrauma = FMath::Max(StrongestTrauma, PositiveTrauma);
		TotalTrauma += PositiveTrauma;
	}
	const float CombinedTrauma = StrongestTrauma + FMath::Max(OtherChannelWeight, 0.f) * (TotalTrauma - StrongestTrauma);
	return FMath::Clamp(CombinedTrauma, 0.f, FMath::Max(TraumaCap, 0.f));
}

float TDHitCameraShakeMath::NormalizeNoise(const float RawNoise, const float NoiseGain)
{
	return FMath::Clamp(RawNoise * FMath::Max(NoiseGain, 0.f), -1.f, 1.f);
}

float TDHitCameraShakeMath::ComputeArmLocationScale(const float ArmLengthCm, const UTDCombatFeedbackSettings& FeedbackSettings)
{
	const float ReferenceArmLengthCm = FMath::Max(FeedbackSettings.ReferenceCameraArmLengthCm, 1.f);
	if (ArmLengthCm <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(ArmLengthCm / ReferenceArmLengthCm, FeedbackSettings.MinArmLocationScale, FMath::Max(FeedbackSettings.MaxArmLocationScale, FeedbackSettings.MinArmLocationScale));
}

float TDHitCameraShakeMath::ComputeKickDegrees(const float DamageRatio, const UTDCombatFeedbackSettings& FeedbackSettings)
{
	const float RatioSpan = FeedbackSettings.KickMaxDamageRatio - FeedbackSettings.KickMinDamageRatio;
	const float Alpha = RatioSpan > KINDA_SMALL_NUMBER
		? FMath::Clamp((DamageRatio - FeedbackSettings.KickMinDamageRatio) / RatioSpan, 0.f, 1.f)
		: (DamageRatio >= FeedbackSettings.KickMaxDamageRatio ? 1.f : 0.f);
	return FMath::Lerp(FeedbackSettings.KickMinDegrees, FeedbackSettings.KickMaxDegrees, Alpha);
}

float TDHitCameraShakeMath::ComputeKickEnvelope(const float ElapsedSeconds, const UTDCombatFeedbackSettings& FeedbackSettings)
{
	const float RiseSeconds = FMath::Max(FeedbackSettings.KickRiseSeconds, KINDA_SMALL_NUMBER);
	const float ReturnSeconds = FMath::Max(FeedbackSettings.KickReturnSeconds, KINDA_SMALL_NUMBER);
	if (ElapsedSeconds <= 0.f)
	{
		return 0.f;
	}
	if (ElapsedSeconds < RiseSeconds)
	{
		return ElapsedSeconds / RiseSeconds;
	}
	const float ReturnAlpha = (ElapsedSeconds - RiseSeconds) / ReturnSeconds;
	if (ReturnAlpha >= 1.f)
	{
		return 0.f;
	}
	return 1.f - FMath::InterpEaseInOut(0.f, 1.f, ReturnAlpha, 2.f);
}

FVector2D TDHitCameraShakeMath::ProjectShakeOffsetPx(const FRotator& Rotation, const FVector& Location, const float FieldOfViewDegrees, const float ArmLengthCm)
{
	const float FieldOfView = FieldOfViewDegrees > 0.f ? FMath::Clamp(FieldOfViewDegrees, 1.f, 170.f) : TDHitCameraShakeNoise::DefaultFieldOfViewDegrees;
	const double FocalLengthPx = TDHitCameraShakeNoise::MeasureHalfScreenWidthPx / FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5));
	const double SafeArmLengthCm = FMath::Max(ArmLengthCm, 1.f);
	return FVector2D(
		FocalLengthPx * (FMath::Tan(FMath::DegreesToRadians(Rotation.Yaw)) + Location.Y / SafeArmLengthCm),
		FocalLengthPx * (FMath::Tan(FMath::DegreesToRadians(Rotation.Pitch)) + Location.Z / SafeArmLengthCm));
}

UTDTraumaCameraShakePattern::UTDTraumaCameraShakePattern(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UTDTraumaCameraShakePattern::AddChannelTrauma(const ETDShakeChannel Channel, const float Amount, const float ChannelCap)
{
	float& Trauma = ChannelTrauma[static_cast<int32>(Channel)];
	Trauma = FMath::Max(Trauma, FMath::Min(Trauma + FMath::Max(Amount, 0.f), FMath::Max(ChannelCap, 0.f)));
}

void UTDTraumaCameraShakePattern::AllowFullTrauma()
{
	bAllowsFullTrauma = true;
}

void UTDTraumaCameraShakePattern::AddKick(const FRotator& NewKickRotation)
{
	KickRotation = NewKickRotation;
	KickElapsedSeconds = 0.f;
}

void UTDTraumaCameraShakePattern::SetNoisePhase(const float Phase)
{
	NoiseTime = Phase;
}

void UTDTraumaCameraShakePattern::SetCameraArmLength(const float ArmLengthCm)
{
	CameraArmLengthCm = ArmLengthCm;
}

void UTDTraumaCameraShakePattern::SetFeedbackSettings(const UTDCombatFeedbackSettings* FeedbackSettings)
{
	FeedbackSettingsOverride = FeedbackSettings;
}

void UTDTraumaCameraShakePattern::DecayTrauma(const float DeltaSeconds)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = ResolveFeedbackSettings();
	const float DecayAmount = FeedbackSettings.ShakeTraumaDecayPerSecond * FMath::Max(DeltaSeconds, 0.f);
	for (float& Trauma : ChannelTrauma)
	{
		Trauma = FMath::Max(0.f, Trauma - DecayAmount);
	}
	if (!bAllowsFullTrauma)
	{
		return;
	}
	const float UncappedTrauma = TDHitCameraShakeMath::CombineChannelTrauma(MakeArrayView(ChannelTrauma), FeedbackSettings.ShakeOtherChannelWeight, FeedbackSettings.BossKillShakeTraumaCap);
	bAllowsFullTrauma = UncappedTrauma > FeedbackSettings.ShakeTraumaCap;
}

float UTDTraumaCameraShakePattern::GetTrauma() const
{
	const UTDCombatFeedbackSettings& FeedbackSettings = ResolveFeedbackSettings();
	const float TraumaCap = bAllowsFullTrauma ? FeedbackSettings.BossKillShakeTraumaCap : FeedbackSettings.ShakeTraumaCap;
	return TDHitCameraShakeMath::CombineChannelTrauma(MakeArrayView(ChannelTrauma), FeedbackSettings.ShakeOtherChannelWeight, TraumaCap);
}

float UTDTraumaCameraShakePattern::GetChannelTrauma(const ETDShakeChannel Channel) const
{
	return ChannelTrauma[static_cast<int32>(Channel)];
}

bool UTDTraumaCameraShakePattern::IsKicking() const
{
	if (KickRotation.IsNearlyZero())
	{
		return false;
	}
	const UTDCombatFeedbackSettings& FeedbackSettings = ResolveFeedbackSettings();
	return KickElapsedSeconds < FeedbackSettings.KickRiseSeconds + FeedbackSettings.KickReturnSeconds;
}

void UTDTraumaCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration::Custom();
	OutInfo.BlendIn = 0.f;
	OutInfo.BlendOut = 0.f;
}

void UTDTraumaCameraShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = ResolveFeedbackSettings();
	const float RealDeltaSeconds = ResolveRealDeltaSeconds(Params);
	NoiseTime += RealDeltaSeconds * FeedbackSettings.ShakeNoiseFrequency;

	const float OutputScale = FeedbackSettings.GetEffectiveShakeScale() * FMath::Max(Params.DynamicScale, 0.f);
	const float Intensity = TDHitCameraShakeMath::ComputeShakeAmount(GetTrauma()) * OutputScale;
	const float ArmLengthCm = ResolveCameraArmLength(FeedbackSettings);
	const float LocationCm = FeedbackSettings.ShakeMaxLocationCm * TDHitCameraShakeMath::ComputeArmLocationScale(ArmLengthCm, FeedbackSettings);
	const float KickWeight = TDHitCameraShakeMath::ComputeKickEnvelope(KickElapsedSeconds, FeedbackSettings) * OutputScale;
	const FRotator& MaxRotation = FeedbackSettings.ShakeMaxRotationDegrees;
	const float NoiseGain = FeedbackSettings.ShakeNoiseGain;

	OutResult.Location = FVector(
		0.f,
		LocationCm * SampleNoise(1, NoiseGain),
		LocationCm * SampleNoise(2, NoiseGain)) * Intensity;
	OutResult.Rotation = FRotator(
		MaxRotation.Pitch * SampleNoise(3, NoiseGain) * Intensity + KickRotation.Pitch * KickWeight,
		MaxRotation.Yaw * SampleNoise(4, NoiseGain) * Intensity + KickRotation.Yaw * KickWeight,
		MaxRotation.Roll * SampleNoise(5, NoiseGain) * Intensity);
	OutResult.Flags = ECameraShakePatternUpdateResultFlags::SkipAutoScale;
	LastScreenOffset = TDHitCameraShakeMath::ProjectShakeOffsetPx(OutResult.Rotation, OutResult.Location, Params.POV.FOV, ArmLengthCm);
	const double ScreenOffsetPx = LastScreenOffset.Size();
	if (ScreenOffsetPx > FeedbackSettings.MaxShakeScreenOffsetPx && ScreenOffsetPx > UE_SMALL_NUMBER)
	{
		const double FitScale = FeedbackSettings.MaxShakeScreenOffsetPx / ScreenOffsetPx;
		OutResult.Rotation *= FitScale;
		OutResult.Location *= FitScale;
		LastScreenOffset = TDHitCameraShakeMath::ProjectShakeOffsetPx(OutResult.Rotation, OutResult.Location, Params.POV.FOV, ArmLengthCm);
	}

	KickElapsedSeconds += RealDeltaSeconds;
	DecayTrauma(RealDeltaSeconds);
}

bool UTDTraumaCameraShakePattern::IsFinishedImpl() const
{
	return GetTrauma() <= KINDA_SMALL_NUMBER && !IsKicking();
}

void UTDTraumaCameraShakePattern::StopShakePatternImpl(const FCameraShakePatternStopParams& Params)
{
	if (!Params.bImmediately)
	{
		return;
	}
	ClearShakeState();
}

void UTDTraumaCameraShakePattern::TeardownShakePatternImpl()
{
	ClearShakeState();
	NoiseTime = 0.f;
}

const UTDCombatFeedbackSettings& UTDTraumaCameraShakePattern::ResolveFeedbackSettings() const
{
	if (const UTDCombatFeedbackSettings* FeedbackSettings = FeedbackSettingsOverride.Get())
	{
		return *FeedbackSettings;
	}
	return *GetDefault<UTDCombatFeedbackSettings>();
}

float UTDTraumaCameraShakePattern::ResolveRealDeltaSeconds(const FCameraShakePatternUpdateParams& Params) const
{
	const UCameraShakeBase* ShakeInstance = GetShakeInstance();
	const APlayerCameraManager* CameraManager = ShakeInstance ? ShakeInstance->GetCameraManager() : nullptr;
	const UWorld* CameraWorld = CameraManager ? CameraManager->GetWorld() : nullptr;
	if (!CameraWorld)
	{
		return FMath::Max(Params.DeltaTime, 0.f);
	}
	if (FApp::UseFixedTimeStep())
	{
		return FMath::Max(static_cast<float>(FApp::GetDeltaTime()), 0.f);
	}
	return FMath::Max(CameraWorld->DeltaRealTimeSeconds, 0.f);
}

float UTDTraumaCameraShakePattern::ResolveCameraArmLength(const UTDCombatFeedbackSettings& FeedbackSettings) const
{
	return CameraArmLengthCm > 0.f ? CameraArmLengthCm : FeedbackSettings.ReferenceCameraArmLengthCm;
}

float UTDTraumaCameraShakePattern::SampleNoise(const int32 Channel, const float NoiseGain) const
{
	const float RawNoise = FMath::PerlinNoise1D(NoiseTime + TDHitCameraShakeNoise::ChannelOffsets[Channel]);
	return TDHitCameraShakeMath::NormalizeNoise(RawNoise, NoiseGain);
}

void UTDTraumaCameraShakePattern::ClearShakeState()
{
	for (float& Trauma : ChannelTrauma)
	{
		Trauma = 0.f;
	}
	KickRotation = FRotator::ZeroRotator;
	KickElapsedSeconds = 0.f;
	LastScreenOffset = FVector2D::ZeroVector;
	bAllowsFullTrauma = false;
}

UTDHitCameraShake::UTDHitCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTDTraumaCameraShakePattern>(TEXT("RootShakePattern")))
{
	bSingleInstance = true;
}
