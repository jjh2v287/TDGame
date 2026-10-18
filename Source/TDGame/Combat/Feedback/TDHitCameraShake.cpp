#include "Combat/Feedback/TDHitCameraShake.h"

namespace TDHitCameraShakeNoise
{
	constexpr float ChannelOffsets[] = { 0.37f, 13.71f, 27.13f, 41.53f, 59.89f, 73.27f };
}

float TDHitCameraShakeMath::ComputeShakeAmount(const float Trauma)
{
	const float ClampedTrauma = FMath::Clamp(Trauma, 0.f, 1.f);
	return ClampedTrauma * FMath::Sqrt(ClampedTrauma);
}

UTDTraumaCameraShakePattern::UTDTraumaCameraShakePattern(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UTDTraumaCameraShakePattern::AddTrauma(const float Amount)
{
	Trauma = FMath::Clamp(Trauma + FMath::Max(Amount, 0.f), 0.f, 1.f);
}

void UTDTraumaCameraShakePattern::DecayTrauma(const float DeltaSeconds)
{
	Trauma = FMath::Max(0.f, Trauma - TraumaDecayPerSecond * FMath::Max(DeltaSeconds, 0.f));
}

void UTDTraumaCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration::Custom();
	OutInfo.BlendIn = 0.f;
	OutInfo.BlendOut = 0.f;
}

void UTDTraumaCameraShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	const UCameraShakeBase* ShakeInstance = GetShakeInstance();
	AddTrauma(ShakeInstance ? ShakeInstance->ShakeScale : 1.f);
}

void UTDTraumaCameraShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	NoiseTime += Params.DeltaTime * NoiseFrequency;
	const float Intensity = TDHitCameraShakeMath::ComputeShakeAmount(Trauma) * FMath::Max(Params.DynamicScale, 0.f);
	OutResult.Location = FVector(
		MaxLocationOffset.X * SampleNoise(0),
		MaxLocationOffset.Y * SampleNoise(1),
		MaxLocationOffset.Z * SampleNoise(2)) * Intensity;
	OutResult.Rotation = FRotator(
		MaxRotationOffset.Pitch * SampleNoise(3) * Intensity,
		MaxRotationOffset.Yaw * SampleNoise(4) * Intensity,
		MaxRotationOffset.Roll * SampleNoise(5) * Intensity);
	OutResult.Flags = ECameraShakePatternUpdateResultFlags::SkipAutoScale;
	DecayTrauma(Params.DeltaTime);
}

bool UTDTraumaCameraShakePattern::IsFinishedImpl() const
{
	return Trauma <= KINDA_SMALL_NUMBER;
}

void UTDTraumaCameraShakePattern::StopShakePatternImpl(const FCameraShakePatternStopParams& Params)
{
	if (!Params.bImmediately)
	{
		return;
	}
	Trauma = 0.f;
}

void UTDTraumaCameraShakePattern::TeardownShakePatternImpl()
{
	Trauma = 0.f;
	NoiseTime = 0.f;
}

float UTDTraumaCameraShakePattern::SampleNoise(const int32 Channel) const
{
	return FMath::PerlinNoise1D(NoiseTime + TDHitCameraShakeNoise::ChannelOffsets[Channel]);
}

UTDHitCameraShake::UTDHitCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTDTraumaCameraShakePattern>(TEXT("RootShakePattern")))
{
	bSingleInstance = true;
}
