#include "MonsterAI/TDBrainInputRegistry.h"

namespace
{
	float ReadDistanceToTarget(const FTDBrainInputs& Inputs, const FTDInputArgs& Args)
	{
		return Inputs.bHasTarget ? Inputs.DistanceToTarget : TNumericLimits<float>::Max();
	}

	float ReadSelfHealthRatio(const FTDBrainInputs& Inputs, const FTDInputArgs& Args)
	{
		return Inputs.SelfHealthRatio;
	}

	float ReadAbilityReady(const FTDBrainInputs& Inputs, const FTDInputArgs& Args)
	{
		return Inputs.IsAbilityReady(Args.AbilityIndex) ? 1.f : 0.f;
	}

	float ReadAllyCountNearby(const FTDBrainInputs& Inputs, const FTDInputArgs& Args)
	{
		return static_cast<float>(Inputs.CountAlliesWithin(Args.Radius));
	}

	float ReadLineOfSightToTarget(const FTDBrainInputs& Inputs, const FTDInputArgs& Args)
	{
		return Inputs.bHasTarget && Inputs.bHasLineOfSight ? 1.f : 0.f;
	}

	float ReadFacingTarget(const FTDBrainInputs& Inputs, const FTDInputArgs& Args)
	{
		return Inputs.bHasTarget ? Inputs.FacingDot : -1.f;
	}

	float ReadTargetIsAttacking(const FTDBrainInputs& Inputs, const FTDInputArgs& Args)
	{
		return Inputs.bHasTarget && Inputs.bIsTargetAttacking ? 1.f : 0.f;
	}

	const FTDBrainInputFunction InputFunctions[] =
	{
		{ TEXT("DistanceToTarget"), &ReadDistanceToTarget, ETDInputCost::Cheap, 0.f, 1200.f, ETDInputArgKind::None, 0.f },
		{ TEXT("SelfHealthRatio"), &ReadSelfHealthRatio, ETDInputCost::Cheap, 0.f, 1.f, ETDInputArgKind::None, 0.f },
		{ TEXT("AbilityReady"), &ReadAbilityReady, ETDInputCost::Cheap, 0.f, 1.f, ETDInputArgKind::Ability, 0.f },
		{ TEXT("FacingTarget"), &ReadFacingTarget, ETDInputCost::Cheap, -1.f, 1.f, ETDInputArgKind::None, 0.f },
		{ TEXT("TargetIsAttacking"), &ReadTargetIsAttacking, ETDInputCost::Cheap, 0.f, 1.f, ETDInputArgKind::None, 0.f },
		{ TEXT("AllyCountNearby"), &ReadAllyCountNearby, ETDInputCost::Grid, 0.f, 8.f, ETDInputArgKind::Radius, 600.f },
		{ TEXT("LineOfSightToTarget"), &ReadLineOfSightToTarget, ETDInputCost::Trace, 0.f, 1.f, ETDInputArgKind::None, 0.f }
	};
}

const FTDBrainInputFunction* FTDBrainInputRegistry::Find(const FName Name)
{
	for (const FTDBrainInputFunction& Entry : InputFunctions)
	{
		if (Name.ToString().Equals(Entry.Name, ESearchCase::IgnoreCase))
		{
			return &Entry;
		}
	}
	return nullptr;
}

TArray<FString> FTDBrainInputRegistry::GetNames()
{
	TArray<FString> Names;
	for (const FTDBrainInputFunction& Entry : InputFunctions)
	{
		Names.Add(Entry.Name);
	}
	return Names;
}
