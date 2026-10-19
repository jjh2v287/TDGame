#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "TDHitCameraShake.generated.h"

class UTDCombatFeedbackSettings;

enum class ETDShakeChannel : uint8
{
	Dealt,
	Received,
	Event
};

namespace TDHitCameraShakeMath
{
	inline constexpr int32 ShakeChannelCount = 3;

	TDGAME_API float ComputeShakeAmount(float Trauma);
	TDGAME_API float CombineChannelTrauma(TConstArrayView<float> ChannelTrauma, float OtherChannelWeight, float TraumaCap);
	TDGAME_API float NormalizeNoise(float RawNoise, float NoiseGain);
	TDGAME_API float ComputeArmLocationScale(float ArmLengthCm, const UTDCombatFeedbackSettings& FeedbackSettings);
	TDGAME_API float ComputeKickDegrees(float DamageRatio, const UTDCombatFeedbackSettings& FeedbackSettings);
	TDGAME_API float ComputeKickEnvelope(float ElapsedSeconds, const UTDCombatFeedbackSettings& FeedbackSettings);
	TDGAME_API FVector2D ProjectShakeOffsetPx(const FRotator& Rotation, const FVector& Location, float FieldOfViewDegrees, float ArmLengthCm);
}

UCLASS()
class TDGAME_API UTDTraumaCameraShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	UTDTraumaCameraShakePattern(const FObjectInitializer& ObjectInitializer);

	void AddChannelTrauma(ETDShakeChannel Channel, float Amount, float ChannelCap);
	void AllowFullTrauma();
	void AddKick(const FRotator& NewKickRotation);
	void SetNoisePhase(float Phase);
	void SetCameraArmLength(float ArmLengthCm);
	void SetFeedbackSettings(const UTDCombatFeedbackSettings* FeedbackSettings);
	void DecayTrauma(float DeltaSeconds);
	float GetTrauma() const;
	float GetChannelTrauma(ETDShakeChannel Channel) const;
	bool IsKicking() const;
	FVector2D GetLastScreenOffset() const { return LastScreenOffset; }

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;
	virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override;
	virtual void TeardownShakePatternImpl() override;

	const UTDCombatFeedbackSettings& ResolveFeedbackSettings() const;
	float ResolveRealDeltaSeconds(const FCameraShakePatternUpdateParams& Params) const;
	float ResolveCameraArmLength(const UTDCombatFeedbackSettings& FeedbackSettings) const;
	float SampleNoise(int32 Channel, float NoiseGain) const;
	void ClearShakeState();

	UPROPERTY(Transient)
	TObjectPtr<const UTDCombatFeedbackSettings> FeedbackSettingsOverride;

	float ChannelTrauma[TDHitCameraShakeMath::ShakeChannelCount] = {};
	FRotator KickRotation = FRotator::ZeroRotator;
	FVector2D LastScreenOffset = FVector2D::ZeroVector;
	float KickElapsedSeconds = 0.f;
	float NoiseTime = 0.f;
	float CameraArmLengthCm = 0.f;
	bool bAllowsFullTrauma = false;
};

UCLASS()
class TDGAME_API UTDHitCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UTDHitCameraShake(const FObjectInitializer& ObjectInitializer);
};
