#include "MonsterAI/TDResponseCurve.h"

namespace
{
	struct FTDCurveTypeName
	{
		const TCHAR* Name;
		ETDResponseCurveType Type;
	};

	const FTDCurveTypeName CurveTypeNames[] =
	{
		{ TEXT("Constant"), ETDResponseCurveType::Constant },
		{ TEXT("Binary"), ETDResponseCurveType::Binary },
		{ TEXT("Linear"), ETDResponseCurveType::Linear },
		{ TEXT("Quadratic"), ETDResponseCurveType::Quadratic },
		{ TEXT("Logistic"), ETDResponseCurveType::Logistic },
		{ TEXT("Logit"), ETDResponseCurveType::Logit },
		{ TEXT("Gaussian"), ETDResponseCurveType::Gaussian }
	};

	float EvaluateRaw(const FTDResponseCurve& Curve, float X)
	{
		switch (Curve.Type)
		{
		case ETDResponseCurveType::Constant:
			return Curve.B;
		case ETDResponseCurveType::Binary:
			return X > Curve.C ? 1.f : 0.f;
		case ETDResponseCurveType::Linear:
			return Curve.M * (X - Curve.C) + Curve.B;
		case ETDResponseCurveType::Quadratic:
			return Curve.M * FMath::Pow(X - Curve.C, Curve.K) + Curve.B;
		case ETDResponseCurveType::Logistic:
			return Curve.K / (1.f + FMath::Exp(-Curve.M * (X - Curve.C))) + Curve.B;
		case ETDResponseCurveType::Logit:
		{
			if (FMath::IsNearlyZero(Curve.M))
			{
				return Curve.B;
			}
			const float ClampedX = FMath::Clamp(X, 0.001f, 0.999f);
			return Curve.K * (0.5f + FMath::Loge(ClampedX / (1.f - ClampedX)) / Curve.M) + Curve.B;
		}
		case ETDResponseCurveType::Gaussian:
		{
			if (FMath::IsNearlyZero(Curve.M))
			{
				return Curve.B;
			}
			const float Offset = X - Curve.C;
			return Curve.K * FMath::Exp(-(Offset * Offset) / (2.f * Curve.M * Curve.M)) + Curve.B;
		}
		}
		return 0.f;
	}
}

float FTDResponseCurve::Evaluate(const float NormalizedInput) const
{
	const float RawOutput = EvaluateRaw(*this, FMath::Clamp(NormalizedInput, 0.f, 1.f));
	const float ClampedOutput = FMath::IsFinite(RawOutput) ? FMath::Clamp(RawOutput, 0.f, 1.f) : 0.f;
	return bInvert ? 1.f - ClampedOutput : ClampedOutput;
}

bool FTDResponseCurve::TryParseType(const FString& Text, ETDResponseCurveType& OutType)
{
	for (const FTDCurveTypeName& Entry : CurveTypeNames)
	{
		if (Text.Equals(Entry.Name, ESearchCase::CaseSensitive))
		{
			OutType = Entry.Type;
			return true;
		}
	}
	return false;
}

TArray<FString> FTDResponseCurve::GetTypeNames()
{
	TArray<FString> Names;
	for (const FTDCurveTypeName& Entry : CurveTypeNames)
	{
		Names.Add(Entry.Name);
	}
	return Names;
}
