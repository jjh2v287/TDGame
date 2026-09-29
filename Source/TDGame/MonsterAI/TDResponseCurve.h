#pragma once

#include "CoreMinimal.h"

enum class ETDResponseCurveType : uint8
{
	Constant,
	Binary,
	Linear,
	Quadratic,
	Logistic,
	Logit,
	Gaussian
};

struct TDGAME_API FTDResponseCurve
{
	ETDResponseCurveType Type = ETDResponseCurveType::Linear;
	float M = 1.f;
	float K = 1.f;
	float B = 0.f;
	float C = 0.f;
	bool bInvert = false;

	float Evaluate(float NormalizedInput) const;

	static bool TryParseType(const FString& Text, ETDResponseCurveType& OutType);
	static TArray<FString> GetTypeNames();
};
