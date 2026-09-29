#include "MonsterAI/TDMonsterActionRegistry.h"

namespace
{
	const TArray<FTDMonsterPrimitiveInfo>& GetPrimitiveTable()
	{
		static const TArray<FTDMonsterPrimitiveInfo> Table =
		{
			{ TEXT("MoveToward"), ETDMonsterPrimitive::MoveToward, ETDMonsterFsmState::Move, true,
				{ { TEXT("target"), ETDActionArgKind::Target }, { TEXT("stop_at"), ETDActionArgKind::Number }, { TEXT("speed_scale"), ETDActionArgKind::Number } } },
			{ TEXT("MoveAway"), ETDMonsterPrimitive::MoveAway, ETDMonsterFsmState::Move, true,
				{ { TEXT("target"), ETDActionArgKind::Target }, { TEXT("speed_scale"), ETDActionArgKind::Number }, { TEXT("seconds"), ETDActionArgKind::Seconds } } },
			{ TEXT("MoveToBand"), ETDMonsterPrimitive::MoveToBand, ETDMonsterFsmState::Move, true,
				{ { TEXT("target"), ETDActionArgKind::Target }, { TEXT("min"), ETDActionArgKind::Number, true }, { TEXT("max"), ETDActionArgKind::Number, true }, { TEXT("speed_scale"), ETDActionArgKind::Number } } },
			{ TEXT("FaceTarget"), ETDMonsterPrimitive::FaceTarget, ETDMonsterFsmState::Idle, true,
				{ { TEXT("target"), ETDActionArgKind::Target } } },
			{ TEXT("Wait"), ETDMonsterPrimitive::Wait, ETDMonsterFsmState::Idle, true,
				{ { TEXT("seconds"), ETDActionArgKind::Seconds } } },
			{ TEXT("CastAbility"), ETDMonsterPrimitive::CastAbility, ETDMonsterFsmState::Cast, true,
				{ { TEXT("ability"), ETDActionArgKind::Ability, true }, { TEXT("lead_target"), ETDActionArgKind::Flag } } },
			{ TEXT("PlaySequence"), ETDMonsterPrimitive::PlaySequence, ETDMonsterFsmState::Sequence, true,
				{ { TEXT("sequence"), ETDActionArgKind::Sequence, true } } },
			{ TEXT("PlayEmote"), ETDMonsterPrimitive::PlayEmote, ETDMonsterFsmState::Idle, false,
				{ { TEXT("seconds"), ETDActionArgKind::Seconds } } }
		};
		return Table;
	}
}

const FTDMonsterPrimitiveInfo* FTDMonsterActionRegistry::Find(const FName Name)
{
	for (const FTDMonsterPrimitiveInfo& Entry : GetPrimitiveTable())
	{
		if (Name.ToString().Equals(Entry.Name, ESearchCase::IgnoreCase))
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FTDMonsterPrimitiveInfo& FTDMonsterActionRegistry::Get(const ETDMonsterPrimitive Primitive)
{
	const TArray<FTDMonsterPrimitiveInfo>& Table = GetPrimitiveTable();
	for (const FTDMonsterPrimitiveInfo& Entry : Table)
	{
		if (Entry.Primitive == Primitive)
		{
			return Entry;
		}
	}
	return Table[0];
}

TArray<FString> FTDMonsterActionRegistry::GetNames()
{
	TArray<FString> Names;
	for (const FTDMonsterPrimitiveInfo& Entry : GetPrimitiveTable())
	{
		Names.Add(Entry.Name);
	}
	return Names;
}

const TCHAR* FTDMonsterActionRegistry::GetStateName(const ETDMonsterFsmState State)
{
	switch (State)
	{
	case ETDMonsterFsmState::Idle:
		return TEXT("Idle");
	case ETDMonsterFsmState::Move:
		return TEXT("Move");
	case ETDMonsterFsmState::Cast:
		return TEXT("Cast");
	case ETDMonsterFsmState::Sequence:
		return TEXT("Sequence");
	case ETDMonsterFsmState::Stagger:
		return TEXT("Stagger");
	}
	return TEXT("Unknown");
}
