#pragma once

#include "CoreMinimal.h"

enum class ETDMonsterFsmState : uint8
{
	Idle,
	Move,
	Cast,
	Sequence,
	Stagger
};

enum class ETDMonsterPrimitive : uint8
{
	MoveToward,
	MoveAway,
	MoveToBand,
	FaceTarget,
	Wait,
	CastAbility,
	PlaySequence,
	PlayEmote
};

enum class ETDActionArgKind : uint8
{
	Target,
	Number,
	Seconds,
	Ability,
	Sequence,
	Flag
};

struct FTDActionArgSchema
{
	const TCHAR* Name = nullptr;
	ETDActionArgKind Kind = ETDActionArgKind::Number;
	bool bIsRequired = false;
};

struct FTDMonsterPrimitiveInfo
{
	const TCHAR* Name = nullptr;
	ETDMonsterPrimitive Primitive = ETDMonsterPrimitive::Wait;
	ETDMonsterFsmState State = ETDMonsterFsmState::Idle;
	bool bIsSimulatable = true;
	TArray<FTDActionArgSchema> Args;
};

struct FTDActionArgs
{
	float StopAtDistance = 0.f;
	float SpeedScale = 1.f;
	float BandMin = 0.f;
	float BandMax = 0.f;
	float Seconds = 0.f;
	int32 AbilityIndex = INDEX_NONE;
	int32 SequenceIndex = INDEX_NONE;
	bool bLeadTarget = false;
};

class TDGAME_API FTDMonsterActionRegistry
{
public:
	static const FTDMonsterPrimitiveInfo* Find(FName Name);
	static const FTDMonsterPrimitiveInfo& Get(ETDMonsterPrimitive Primitive);
	static TArray<FString> GetNames();
	static const TCHAR* GetStateName(ETDMonsterFsmState State);
};
