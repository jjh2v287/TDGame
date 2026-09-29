#pragma once

#include "CoreMinimal.h"
#include "MonsterAI/TDBrainInputs.h"

enum class ETDInputCost : uint8
{
	Cheap,
	Grid,
	Trace
};

enum class ETDInputArgKind : uint8
{
	None,
	Ability,
	Radius
};

using FTDBrainInputFn = float (*)(const FTDBrainInputs&, const FTDInputArgs&);

struct FTDBrainInputFunction
{
	const TCHAR* Name = nullptr;
	FTDBrainInputFn Function = nullptr;
	ETDInputCost Cost = ETDInputCost::Cheap;
	float DefaultRangeMin = 0.f;
	float DefaultRangeMax = 1.f;
	ETDInputArgKind ArgKind = ETDInputArgKind::None;
	float DefaultRadius = 0.f;
};

class TDGAME_API FTDBrainInputRegistry
{
public:
	static const FTDBrainInputFunction* Find(FName Name);
	static TArray<FString> GetNames();
};
