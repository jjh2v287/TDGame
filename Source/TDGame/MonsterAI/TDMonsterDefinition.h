#pragma once

#include "CoreMinimal.h"
#include "MonsterAI/TDBrainInputRegistry.h"
#include "MonsterAI/TDMonsterActionRegistry.h"
#include "MonsterAI/TDResponseCurve.h"

class UTDDamageDefinition;

namespace TDMonsterAI
{
	constexpr int32 StepsPerSecond = 64;
	constexpr float StepSeconds = 1.f / StepsPerSecond;

	inline int32 SecondsToSteps(const float Seconds)
	{
		return FMath::Max(0, FMath::RoundToInt(Seconds * StepsPerSecond));
	}
}

enum class ETDCompareOp : uint8
{
	Less,
	LessOrEqual,
	Greater,
	GreaterOrEqual,
	Equal
};

struct FTDMonsterStatValues
{
	float MaxHealth = 100.f;
	float AttackPower = 10.f;
	float Armor = 0.f;
	float MoveSpeed = 400.f;
	int32 Team = 2;
};

struct FTDMonsterStatOverride
{
	TOptional<float> MaxHealth;
	TOptional<float> AttackPower;
	TOptional<float> Armor;
	TOptional<float> MoveSpeed;

	void ApplyTo(FTDMonsterStatValues& Stats) const
	{
		Stats.MaxHealth = MaxHealth.Get(Stats.MaxHealth);
		Stats.AttackPower = AttackPower.Get(Stats.AttackPower);
		Stats.Armor = Armor.Get(Stats.Armor);
		Stats.MoveSpeed = MoveSpeed.Get(Stats.MoveSpeed);
	}
};

struct FTDResolvedConsideration
{
	const FTDBrainInputFunction* Input = nullptr;
	FTDInputArgs Args;
	FTDResponseCurve Curve;
	float RangeMin = 0.f;
	float RangeMax = 1.f;

	float Evaluate(const FTDBrainInputs& Inputs) const
	{
		const float RawValue = Input->Function(Inputs, Args);
		const float Span = RangeMax - RangeMin;
		const float Normalized = FMath::IsNearlyZero(Span) ? 0.f : FMath::Clamp((RawValue - RangeMin) / Span, 0.f, 1.f);
		return Curve.Evaluate(Normalized);
	}
};

struct FTDResolvedStep
{
	ETDMonsterPrimitive Primitive = ETDMonsterPrimitive::Wait;
	FTDActionArgs Args;
};

struct FTDResolvedAction
{
	FName Id;
	ETDMonsterPrimitive Primitive = ETDMonsterPrimitive::Wait;
	FTDActionArgs Args;
	float Weight = 1.f;
	int32 CooldownSteps = 0;
	TArray<FTDResolvedConsideration> Considerations;
};

struct FTDResolvedAbility
{
	FName Name;
	FName SpellName;
	UTDDamageDefinition* Spell = nullptr;
	float Range = 0.f;
	int32 CooldownSteps = 0;
};

struct FTDResolvedPhase
{
	FName Id;
	const FTDBrainInputFunction* Input = nullptr;
	FTDInputArgs InputArgs;
	ETDCompareOp Op = ETDCompareOp::LessOrEqual;
	float Value = 0.f;
	int32 OnEnterSequenceIndex = INDEX_NONE;
	FTDMonsterStatOverride Stats;

	bool IsSatisfied(const FTDBrainInputs& Inputs) const
	{
		const float RawValue = Input->Function(Inputs, InputArgs);
		switch (Op)
		{
		case ETDCompareOp::Less:
			return RawValue < Value;
		case ETDCompareOp::LessOrEqual:
			return RawValue <= Value;
		case ETDCompareOp::Greater:
			return RawValue > Value;
		case ETDCompareOp::GreaterOrEqual:
			return RawValue >= Value;
		case ETDCompareOp::Equal:
			return FMath::IsNearlyEqual(RawValue, Value);
		}
		return false;
	}
};

struct FTDResolvedMonsterDefinition
{
	FName Id;
	FString SourceFile;
	uint64 DefinitionHash = 0;
	FTDMonsterStatValues Stats;
	int32 ThinkPeriodSteps = 6;
	float SwitchRatio = 1.15f;
	int32 MinHoldSteps = 26;
	TArray<FTDResolvedAbility> Abilities;
	TArray<FName> SequenceNames;
	TArray<TArray<FTDResolvedStep>> Sequences;
	TArray<TArray<FTDResolvedAction>> ActionsByPhase;
	TArray<FTDResolvedPhase> Phases;

	const TArray<FTDResolvedAction>& GetActions(const int32 PhaseIndex) const
	{
		return ActionsByPhase[FMath::Clamp(PhaseIndex, 0, ActionsByPhase.Num() - 1)];
	}

	FTDMonsterStatValues GetStatsForPhase(const int32 PhaseIndex) const
	{
		FTDMonsterStatValues PhaseStats = Stats;
		for (int32 Index = 0; Index < PhaseIndex && Index < Phases.Num(); ++Index)
		{
			Phases[Index].Stats.ApplyTo(PhaseStats);
		}
		return PhaseStats;
	}

	int32 GetMaxActionCount() const
	{
		int32 MaxCount = 0;
		for (const TArray<FTDResolvedAction>& Actions : ActionsByPhase)
		{
			MaxCount = FMath::Max(MaxCount, Actions.Num());
		}
		return MaxCount;
	}
};
