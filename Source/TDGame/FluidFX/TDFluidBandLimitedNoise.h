#pragma once

#include "CoreMinimal.h"

namespace TDFluidDeterministic
{
	inline uint32 MixBits(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7FEB352Du;
		Value ^= Value >> 15;
		Value *= 0x846CA68Bu;
		Value ^= Value >> 16;
		return Value;
	}

	inline uint32 CombineHash(uint32 Left, uint32 Right)
	{
		return MixBits(Left ^ (Right + 0x9E3779B9u + (Left << 6) + (Left >> 2)));
	}

	inline float HashToSignedUnit(uint32 Hash)
	{
		return static_cast<float>(Hash >> 8) * (2.0f / 16777215.0f) - 1.0f;
	}

	inline float NextUnitFloat(uint32& InOutState)
	{
		InOutState ^= InOutState << 13;
		InOutState ^= InOutState >> 17;
		InOutState ^= InOutState << 5;
		return static_cast<float>(InOutState >> 8) * (1.0f / 16777216.0f);
	}

	inline uint32 MakeNonZeroState(int32 Seed)
	{
		const uint32 State = MixBits(static_cast<uint32>(Seed) + 0x6D2B79F5u);
		return State != 0u ? State : 0x1u;
	}

	inline float EvaluateKnotOctave(int32 Seed, int32 StreamIndex, int32 OctaveIndex, double TimeSeconds, double FrequencyHz)
	{
		const double KnotPosition = TimeSeconds * FrequencyHz;
		const int32 KnotIndex = FMath::FloorToInt(KnotPosition);
		const float Fraction = static_cast<float>(KnotPosition - static_cast<double>(KnotIndex));
		const float Blend = Fraction * Fraction * (3.0f - 2.0f * Fraction);
		const uint32 StreamHash = CombineHash(CombineHash(static_cast<uint32>(Seed), static_cast<uint32>(StreamIndex)), static_cast<uint32>(OctaveIndex));
		const float LowerKnot = HashToSignedUnit(CombineHash(StreamHash, static_cast<uint32>(KnotIndex)));
		const float UpperKnot = HashToSignedUnit(CombineHash(StreamHash, static_cast<uint32>(KnotIndex + 1)));
		return FMath::Lerp(LowerKnot, UpperKnot, Blend);
	}

	inline float EvaluateBandLimitedNoise(int32 Seed, int32 StreamIndex, double TimeSeconds, double FrequencyHz)
	{
		constexpr double SecondOctaveFrequencyRatio = 2.3;
		constexpr float FirstOctaveWeight = 0.7f;
		constexpr float SecondOctaveWeight = 0.3f;
		return FirstOctaveWeight * EvaluateKnotOctave(Seed, StreamIndex, 0, TimeSeconds, FrequencyHz)
			+ SecondOctaveWeight * EvaluateKnotOctave(Seed, StreamIndex, 1, TimeSeconds, FrequencyHz * SecondOctaveFrequencyRatio);
	}
}
