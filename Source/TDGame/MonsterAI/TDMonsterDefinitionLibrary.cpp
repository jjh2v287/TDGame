#include "MonsterAI/TDMonsterDefinitionLibrary.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MonsterAI/TDMonsterAttackCatalog.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDMonsterDefinition, Log, All);

namespace
{
	constexpr int32 SupportedSchema = 1;
	constexpr int32 MaxExtendsDepth = 3;
	constexpr int32 MaxSequenceLength = 8;
	constexpr int32 MaxAbilities = 64;
	constexpr float DefaultThinkHz = 10.f;
	constexpr float DefaultSwitchRatio = 1.15f;
	constexpr float DefaultMinHoldSeconds = 0.4f;

	const TArray<FString> TopLevelKeys = { TEXT("schema"), TEXT("id"), TEXT("kind"), TEXT("extends"), TEXT("stats"), TEXT("think_hz"), TEXT("lod_periods"), TEXT("inertia"), TEXT("select"), TEXT("top_n"), TEXT("abilities"), TEXT("sequences"), TEXT("actions"), TEXT("phases"), TEXT("persona") };
	const TArray<FString> StatKeys = { TEXT("max_health"), TEXT("attack_power"), TEXT("armor"), TEXT("move_speed"), TEXT("team") };
	const TArray<FString> PhaseStatKeys = { TEXT("max_health"), TEXT("attack_power"), TEXT("armor"), TEXT("move_speed") };
	const TArray<FString> InertiaKeys = { TEXT("switch_ratio"), TEXT("min_hold_seconds") };
	const TArray<FString> AbilityKeys = { TEXT("spell"), TEXT("range"), TEXT("cooldown"), TEXT("timetable") };
	const TArray<FString> StepKeys = { TEXT("do"), TEXT("args") };
	const TArray<FString> ActionKeys = { TEXT("id"), TEXT("do"), TEXT("args"), TEXT("weight"), TEXT("cooldown"), TEXT("considerations"), TEXT("remove") };
	const TArray<FString> RemoveKeys = { TEXT("id"), TEXT("remove") };
	const TArray<FString> ConsiderationKeys = { TEXT("input"), TEXT("args"), TEXT("curve"), TEXT("m"), TEXT("k"), TEXT("b"), TEXT("c"), TEXT("range"), TEXT("invert") };
	const TArray<FString> PhaseKeys = { TEXT("id"), TEXT("enter_when"), TEXT("on_enter"), TEXT("stats"), TEXT("actions") };
	const TArray<FString> EnterWhenKeys = { TEXT("input"), TEXT("args"), TEXT("op"), TEXT("value") };
	const TArray<FString> TargetNames = { TEXT("Player"), TEXT("NearestEnemy") };
	const TArray<FString> KeyMergedObjects = { TEXT("stats"), TEXT("abilities"), TEXT("sequences"), TEXT("persona"), TEXT("inertia"), TEXT("lod_periods") };

	int32 ComputeEditDistance(const FString& Left, const FString& Right)
	{
		TArray<int32> Previous;
		TArray<int32> Current;
		Previous.SetNum(Right.Len() + 1);
		Current.SetNum(Right.Len() + 1);
		for (int32 Column = 0; Column <= Right.Len(); ++Column)
		{
			Previous[Column] = Column;
		}
		for (int32 Row = 1; Row <= Left.Len(); ++Row)
		{
			Current[0] = Row;
			for (int32 Column = 1; Column <= Right.Len(); ++Column)
			{
				const int32 SubstitutionCost = FChar::ToLower(Left[Row - 1]) == FChar::ToLower(Right[Column - 1]) ? 0 : 1;
				Current[Column] = FMath::Min3(Previous[Column] + 1, Current[Column - 1] + 1, Previous[Column - 1] + SubstitutionCost);
			}
			Swap(Previous, Current);
		}
		return Previous[Right.Len()];
	}

	FString MakeSuggestion(const FString& Unknown, const TArray<FString>& Candidates)
	{
		FString BestCandidate;
		int32 BestDistance = MAX_int32;
		for (const FString& Candidate : Candidates)
		{
			const int32 Distance = ComputeEditDistance(Unknown, Candidate);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				BestCandidate = Candidate;
			}
		}
		const int32 Tolerance = FMath::Max(2, Unknown.Len() / 3);
		if (BestCandidate.IsEmpty() || BestDistance > Tolerance)
		{
			return FString();
		}
		return FString::Printf(TEXT(" (did you mean '%s'?)"), *BestCandidate);
	}

	FString JoinPath(const FString& Base, const FString& Child)
	{
		return Base.IsEmpty() ? Child : Base + TEXT(".") + Child;
	}

	FString IndexPath(const FString& Base, const int32 Index)
	{
		return FString::Printf(TEXT("%s[%d]"), *Base, Index);
	}

	uint64 HashText(const FString& Text)
	{
		uint64 Hash = 14695981039346656037ull;
		for (const TCHAR Character : Text)
		{
			const uint32 Code = static_cast<uint32>(Character);
			for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
			{
				Hash ^= static_cast<uint64>((Code >> (ByteIndex * 8)) & 0xFF);
				Hash *= 1099511628211ull;
			}
		}
		return Hash;
	}

	FString SerializeCondensed(const TSharedPtr<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	class FTDDefinitionReader
	{
	public:
		FTDDefinitionReader(const FString& InFile, TArray<FString>& InErrors)
			: File(InFile)
			, Errors(InErrors)
		{
		}

		void Error(const FString& Path, const FString& Message)
		{
			Errors.Add(FString::Printf(TEXT("%s: %s: %s"), *File, *Path, *Message));
			++ErrorCount;
		}

		int32 GetErrorCount() const
		{
			return ErrorCount;
		}

		void CheckKeys(const TSharedPtr<FJsonObject>& Object, const FString& Path, const TArray<FString>& AllowedKeys)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Object->Values)
			{
				if (!AllowedKeys.Contains(Entry.Key))
				{
					Error(JoinPath(Path, Entry.Key), FString::Printf(TEXT("unknown key%s"), *MakeSuggestion(Entry.Key, AllowedKeys)));
				}
			}
		}

		bool ReadNumberValue(const TSharedPtr<FJsonValue>& Value, const FString& Path, float& OutNumber)
		{
			if (Value.IsValid() && Value->Type == EJson::Number)
			{
				OutNumber = static_cast<float>(Value->AsNumber());
				return true;
			}
			const TSharedPtr<FJsonObject>* TunableObject = nullptr;
			if (Value.IsValid() && Value->TryGetObject(TunableObject) && TunableObject)
			{
				CheckKeys(*TunableObject, Path, { TEXT("value"), TEXT("tune") });
				double TunableValue = 0.0;
				if ((*TunableObject)->TryGetNumberField(TEXT("value"), TunableValue))
				{
					OutNumber = static_cast<float>(TunableValue);
					return true;
				}
			}
			Error(Path, TEXT("expected a number or {\"value\": number, \"tune\": bool}"));
			return false;
		}

		bool ReadOptionalNumber(const TSharedPtr<FJsonObject>& Object, const FString& Key, const FString& Path, float& InOutNumber)
		{
			const TSharedPtr<FJsonValue> Value = Object->TryGetField(Key);
			if (!Value.IsValid())
			{
				return false;
			}
			return ReadNumberValue(Value, JoinPath(Path, Key), InOutNumber);
		}

		bool ReadRequiredNumber(const TSharedPtr<FJsonObject>& Object, const FString& Key, const FString& Path, float& OutNumber)
		{
			if (!Object->HasField(Key))
			{
				Error(JoinPath(Path, Key), TEXT("required number is missing"));
				return false;
			}
			return ReadOptionalNumber(Object, Key, Path, OutNumber);
		}

		bool ReadRequiredString(const TSharedPtr<FJsonObject>& Object, const FString& Key, const FString& Path, FString& OutText)
		{
			if (Object->TryGetStringField(Key, OutText) && !OutText.IsEmpty())
			{
				return true;
			}
			Error(JoinPath(Path, Key), TEXT("required non-empty string is missing"));
			return false;
		}

		TSharedPtr<FJsonObject> ReadOptionalObject(const TSharedPtr<FJsonObject>& Object, const FString& Key, const FString& Path)
		{
			const TSharedPtr<FJsonValue> Value = Object->TryGetField(Key);
			if (!Value.IsValid())
			{
				return nullptr;
			}
			const TSharedPtr<FJsonObject>* ChildObject = nullptr;
			if (!Value->TryGetObject(ChildObject) || !ChildObject)
			{
				Error(JoinPath(Path, Key), TEXT("expected an object"));
				return nullptr;
			}
			return *ChildObject;
		}

		const TArray<TSharedPtr<FJsonValue>>* ReadOptionalArray(const TSharedPtr<FJsonObject>& Object, const FString& Key, const FString& Path)
		{
			const TSharedPtr<FJsonValue> Value = Object->TryGetField(Key);
			if (!Value.IsValid())
			{
				return nullptr;
			}
			const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
			if (!Value->TryGetArray(Array))
			{
				Error(JoinPath(Path, Key), TEXT("expected an array"));
				return nullptr;
			}
			return Array;
		}

	private:
		FString File;
		TArray<FString>& Errors;
		int32 ErrorCount = 0;
	};

	TArray<TSharedPtr<FJsonValue>> MergeActionArrays(const TArray<TSharedPtr<FJsonValue>>& BaseActions, const TArray<TSharedPtr<FJsonValue>>& OverlayActions, const FString& Path, FTDDefinitionReader& Reader)
	{
		TArray<TSharedPtr<FJsonValue>> Result = BaseActions;
		for (int32 OverlayIndex = 0; OverlayIndex < OverlayActions.Num(); ++OverlayIndex)
		{
			const FString ActionPath = IndexPath(Path, OverlayIndex);
			const TSharedPtr<FJsonObject>* ActionObject = nullptr;
			if (!OverlayActions[OverlayIndex]->TryGetObject(ActionObject) || !ActionObject)
			{
				Reader.Error(ActionPath, TEXT("expected an action object"));
				continue;
			}
			FString ActionId;
			if (!Reader.ReadRequiredString(*ActionObject, TEXT("id"), ActionPath, ActionId))
			{
				continue;
			}
			bool bIsRemoval = false;
			(*ActionObject)->TryGetBoolField(TEXT("remove"), bIsRemoval);
			const int32 ExistingIndex = Result.IndexOfByPredicate([&ActionId](const TSharedPtr<FJsonValue>& Existing)
			{
				const TSharedPtr<FJsonObject>* ExistingObject = nullptr;
				FString ExistingId;
				return Existing->TryGetObject(ExistingObject) && ExistingObject && (*ExistingObject)->TryGetStringField(TEXT("id"), ExistingId) && ExistingId == ActionId;
			});
			if (bIsRemoval)
			{
				Reader.CheckKeys(*ActionObject, ActionPath, RemoveKeys);
				if (ExistingIndex == INDEX_NONE)
				{
					Reader.Error(ActionPath, FString::Printf(TEXT("cannot remove unknown action '%s'"), *ActionId));
					continue;
				}
				Result.RemoveAt(ExistingIndex);
				continue;
			}
			if (ExistingIndex == INDEX_NONE)
			{
				Result.Add(OverlayActions[OverlayIndex]);
				continue;
			}
			Result[ExistingIndex] = OverlayActions[OverlayIndex];
		}
		return Result;
	}

	TSharedPtr<FJsonObject> MergeDefinitionObjects(const TSharedPtr<FJsonObject>& Parent, const TSharedPtr<FJsonObject>& Child, FTDDefinitionReader& Reader)
	{
		TSharedPtr<FJsonObject> Merged = MakeShared<FJsonObject>();
		Merged->Values = Parent->Values;
		Merged->RemoveField(TEXT("extends"));
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Child->Values)
		{
			if (Entry.Key == TEXT("extends"))
			{
				continue;
			}
			const TSharedPtr<FJsonValue> ParentValue = Parent->TryGetField(Entry.Key);
			if (Entry.Key == TEXT("actions") && ParentValue.IsValid() && ParentValue->Type == EJson::Array && Entry.Value->Type == EJson::Array)
			{
				Merged->SetArrayField(Entry.Key, MergeActionArrays(ParentValue->AsArray(), Entry.Value->AsArray(), TEXT("actions"), Reader));
				continue;
			}
			const bool bMergesByKey = KeyMergedObjects.Contains(Entry.Key) && ParentValue.IsValid() && ParentValue->Type == EJson::Object && Entry.Value->Type == EJson::Object;
			if (!bMergesByKey)
			{
				Merged->SetField(Entry.Key, Entry.Value);
				continue;
			}
			TSharedPtr<FJsonObject> MergedChild = MakeShared<FJsonObject>();
			MergedChild->Values = ParentValue->AsObject()->Values;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& ChildEntry : Entry.Value->AsObject()->Values)
			{
				MergedChild->SetField(ChildEntry.Key, ChildEntry.Value);
			}
			Merged->SetObjectField(Entry.Key, MergedChild);
		}
		return Merged;
	}

	class FTDDefinitionResolver
	{
	public:
		FTDDefinitionResolver(FTDDefinitionReader& InReader, FTDMonsterSpellResolver InSpellResolver)
			: Reader(InReader)
			, SpellResolver(InSpellResolver)
		{
		}

		TSharedPtr<FTDResolvedMonsterDefinition> Resolve(const FString& SourceFile, const TSharedPtr<FJsonObject>& Root)
		{
			const int32 StartErrors = Reader.GetErrorCount();
			TSharedPtr<FTDResolvedMonsterDefinition> Definition = MakeShared<FTDResolvedMonsterDefinition>();
			Definition->SourceFile = SourceFile;
			Definition->DefinitionHash = HashText(SerializeCondensed(Root));
			Reader.CheckKeys(Root, FString(), TopLevelKeys);

			FString Id;
			Reader.ReadRequiredString(Root, TEXT("id"), FString(), Id);
			Definition->Id = FName(*Id);

			ResolveHeader(*Definition, Root);
			ResolveStats(*Definition, Root);
			ResolveAbilities(*Definition, Root);
			ResolveSequences(*Definition, Root);

			const TArray<TSharedPtr<FJsonValue>>* ActionValues = Reader.ReadOptionalArray(Root, TEXT("actions"), FString());
			TArray<TSharedPtr<FJsonValue>> BaseActionValues = ActionValues ? *ActionValues : TArray<TSharedPtr<FJsonValue>>();
			if (BaseActionValues.IsEmpty())
			{
				Reader.Error(TEXT("actions"), TEXT("at least one action is required"));
			}
			Definition->ActionsByPhase.Add(ResolveActions(*Definition, BaseActionValues, TEXT("actions")));
			ResolvePhases(*Definition, Root, BaseActionValues);

			if (Reader.GetErrorCount() != StartErrors)
			{
				return nullptr;
			}
			return Definition;
		}

	private:
		void ResolveHeader(FTDResolvedMonsterDefinition& Definition, const TSharedPtr<FJsonObject>& Root)
		{
			float SchemaNumber = 0.f;
			if (Reader.ReadRequiredNumber(Root, TEXT("schema"), FString(), SchemaNumber) && FMath::RoundToInt(SchemaNumber) != SupportedSchema)
			{
				Reader.Error(TEXT("schema"), FString::Printf(TEXT("unsupported schema %d, expected %d"), FMath::RoundToInt(SchemaNumber), SupportedSchema));
			}
			FString Kind = TEXT("monster");
			Root->TryGetStringField(TEXT("kind"), Kind);
			if (Kind != TEXT("monster"))
			{
				Reader.Error(TEXT("kind"), FString::Printf(TEXT("kind '%s' cannot drive a game monster"), *Kind));
			}
			float ThinkHz = DefaultThinkHz;
			Reader.ReadOptionalNumber(Root, TEXT("think_hz"), FString(), ThinkHz);
			if (ThinkHz <= 0.f || ThinkHz > 32.f)
			{
				Reader.Error(TEXT("think_hz"), TEXT("must be in (0, 32]"));
				ThinkHz = DefaultThinkHz;
			}
			Definition.ThinkPeriodSteps = FMath::Max(2, FMath::RoundToInt(TDMonsterAI::StepsPerSecond / ThinkHz));

			float SwitchRatio = DefaultSwitchRatio;
			float MinHoldSeconds = DefaultMinHoldSeconds;
			if (const TSharedPtr<FJsonObject> Inertia = Reader.ReadOptionalObject(Root, TEXT("inertia"), FString()))
			{
				Reader.CheckKeys(Inertia, TEXT("inertia"), InertiaKeys);
				Reader.ReadOptionalNumber(Inertia, TEXT("switch_ratio"), TEXT("inertia"), SwitchRatio);
				Reader.ReadOptionalNumber(Inertia, TEXT("min_hold_seconds"), TEXT("inertia"), MinHoldSeconds);
			}
			if (SwitchRatio < 1.f)
			{
				Reader.Error(TEXT("inertia.switch_ratio"), TEXT("must be >= 1"));
			}
			if (MinHoldSeconds < 0.f)
			{
				Reader.Error(TEXT("inertia.min_hold_seconds"), TEXT("must be >= 0"));
			}
			Definition.SwitchRatio = SwitchRatio;
			Definition.MinHoldSteps = TDMonsterAI::SecondsToSteps(MinHoldSeconds);

			FString SelectMode = TEXT("max");
			Root->TryGetStringField(TEXT("select"), SelectMode);
			if (SelectMode != TEXT("max"))
			{
				Reader.Error(TEXT("select"), FString::Printf(TEXT("select '%s' is not supported by the game brain yet, use 'max'"), *SelectMode));
			}
			if (Root->HasField(TEXT("top_n")))
			{
				Reader.Error(TEXT("top_n"), TEXT("top_n is only valid with weighted_random"));
			}
			if (Root->HasField(TEXT("persona")))
			{
				Reader.Error(TEXT("persona"), TEXT("persona belongs to player_proxy definitions"));
			}
		}

		void ResolveStats(FTDResolvedMonsterDefinition& Definition, const TSharedPtr<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Stats = Reader.ReadOptionalObject(Root, TEXT("stats"), FString());
			if (!Stats)
			{
				Reader.Error(TEXT("stats"), TEXT("stats object is required for monsters"));
				return;
			}
			Reader.CheckKeys(Stats, TEXT("stats"), StatKeys);
			float TeamNumber = 2.f;
			Reader.ReadRequiredNumber(Stats, TEXT("max_health"), TEXT("stats"), Definition.Stats.MaxHealth);
			Reader.ReadRequiredNumber(Stats, TEXT("attack_power"), TEXT("stats"), Definition.Stats.AttackPower);
			Reader.ReadRequiredNumber(Stats, TEXT("armor"), TEXT("stats"), Definition.Stats.Armor);
			Reader.ReadRequiredNumber(Stats, TEXT("move_speed"), TEXT("stats"), Definition.Stats.MoveSpeed);
			Reader.ReadRequiredNumber(Stats, TEXT("team"), TEXT("stats"), TeamNumber);
			Definition.Stats.Team = FMath::RoundToInt(TeamNumber);
			if (Definition.Stats.MaxHealth <= 0.f)
			{
				Reader.Error(TEXT("stats.max_health"), TEXT("must be > 0"));
			}
			if (Definition.Stats.MoveSpeed < 0.f)
			{
				Reader.Error(TEXT("stats.move_speed"), TEXT("must be >= 0"));
			}
		}

		void ResolveAbilities(FTDResolvedMonsterDefinition& Definition, const TSharedPtr<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Abilities = Reader.ReadOptionalObject(Root, TEXT("abilities"), FString());
			if (!Abilities)
			{
				return;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Abilities->Values)
			{
				const FString Path = JoinPath(TEXT("abilities"), Entry.Key);
				const TSharedPtr<FJsonObject>* AbilityObject = nullptr;
				if (!Entry.Value->TryGetObject(AbilityObject) || !AbilityObject)
				{
					Reader.Error(Path, TEXT("expected an ability object"));
					continue;
				}
				Reader.CheckKeys(*AbilityObject, Path, AbilityKeys);
				FTDResolvedAbility& Ability = Definition.Abilities.AddDefaulted_GetRef();
				Ability.Name = FName(*Entry.Key);
				FString SpellName;
				if (Reader.ReadRequiredString(*AbilityObject, TEXT("spell"), Path, SpellName))
				{
					Ability.SpellName = FName(*SpellName);
					Ability.Spell = SpellResolver(Ability.SpellName);
					if (!Ability.Spell)
					{
						Reader.Error(JoinPath(Path, TEXT("spell")), FString::Printf(TEXT("unknown spell '%s' (not in the monster attack catalog or /Game/Combat/Examples)"), *SpellName));
					}
				}
				Reader.ReadRequiredNumber(*AbilityObject, TEXT("range"), Path, Ability.Range);
				float CooldownSeconds = 0.f;
				Reader.ReadRequiredNumber(*AbilityObject, TEXT("cooldown"), Path, CooldownSeconds);
				Ability.CooldownSteps = TDMonsterAI::SecondsToSteps(CooldownSeconds);
				if (Ability.Range <= 0.f)
				{
					Reader.Error(JoinPath(Path, TEXT("range")), TEXT("must be > 0"));
				}
			}
			if (Definition.Abilities.Num() > MaxAbilities)
			{
				Reader.Error(TEXT("abilities"), FString::Printf(TEXT("at most %d abilities"), MaxAbilities));
			}
		}

		void ResolveSequences(FTDResolvedMonsterDefinition& Definition, const TSharedPtr<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Sequences = Reader.ReadOptionalObject(Root, TEXT("sequences"), FString());
			if (!Sequences)
			{
				return;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Sequences->Values)
			{
				Definition.SequenceNames.Add(FName(*Entry.Key));
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Sequences->Values)
			{
				const FString Path = JoinPath(TEXT("sequences"), Entry.Key);
				TArray<FTDResolvedStep>& Steps = Definition.Sequences.AddDefaulted_GetRef();
				const TArray<TSharedPtr<FJsonValue>>* StepValues = nullptr;
				if (!Entry.Value->TryGetArray(StepValues) || !StepValues || StepValues->IsEmpty())
				{
					Reader.Error(Path, TEXT("expected a non-empty array of steps"));
					continue;
				}
				if (StepValues->Num() > MaxSequenceLength)
				{
					Reader.Error(Path, FString::Printf(TEXT("at most %d steps"), MaxSequenceLength));
				}
				for (int32 StepIndex = 0; StepIndex < StepValues->Num(); ++StepIndex)
				{
					const FString StepPath = IndexPath(Path, StepIndex);
					const TSharedPtr<FJsonObject>* StepObject = nullptr;
					if (!(*StepValues)[StepIndex]->TryGetObject(StepObject) || !StepObject)
					{
						Reader.Error(StepPath, TEXT("expected a step object"));
						continue;
					}
					Reader.CheckKeys(*StepObject, StepPath, StepKeys);
					FTDResolvedStep& Step = Steps.AddDefaulted_GetRef();
					const FTDMonsterPrimitiveInfo* Primitive = ResolvePrimitive(Definition, *StepObject, StepPath, Step.Args);
					if (!Primitive)
					{
						continue;
					}
					if (Primitive->Primitive == ETDMonsterPrimitive::PlaySequence)
					{
						Reader.Error(JoinPath(StepPath, TEXT("do")), TEXT("sequences cannot play other sequences"));
					}
					Step.Primitive = Primitive->Primitive;
				}
			}
		}

		const FTDMonsterPrimitiveInfo* ResolvePrimitive(const FTDResolvedMonsterDefinition& Definition, const TSharedPtr<FJsonObject>& Object, const FString& Path, FTDActionArgs& OutArgs)
		{
			FString PrimitiveName;
			if (!Reader.ReadRequiredString(Object, TEXT("do"), Path, PrimitiveName))
			{
				return nullptr;
			}
			const FTDMonsterPrimitiveInfo* Primitive = FTDMonsterActionRegistry::Find(FName(*PrimitiveName));
			if (!Primitive)
			{
				Reader.Error(JoinPath(Path, TEXT("do")), FString::Printf(TEXT("unknown action primitive '%s'%s"), *PrimitiveName, *MakeSuggestion(PrimitiveName, FTDMonsterActionRegistry::GetNames())));
				return nullptr;
			}
			ResolveActionArgs(Definition, *Primitive, Reader.ReadOptionalObject(Object, TEXT("args"), Path), JoinPath(Path, TEXT("args")), OutArgs);
			return Primitive;
		}

		void ResolveActionArgs(const FTDResolvedMonsterDefinition& Definition, const FTDMonsterPrimitiveInfo& Primitive, const TSharedPtr<FJsonObject>& Args, const FString& Path, FTDActionArgs& OutArgs)
		{
			TArray<FString> AllowedNames;
			for (const FTDActionArgSchema& Schema : Primitive.Args)
			{
				AllowedNames.Add(Schema.Name);
			}
			if (Args)
			{
				Reader.CheckKeys(Args, Path, AllowedNames);
			}
			for (const FTDActionArgSchema& Schema : Primitive.Args)
			{
				const FString ArgName = Schema.Name;
				const FString ArgPath = JoinPath(Path, ArgName);
				const TSharedPtr<FJsonValue> Value = Args ? Args->TryGetField(ArgName) : nullptr;
				if (!Value.IsValid())
				{
					if (Schema.bIsRequired)
					{
						Reader.Error(ArgPath, TEXT("required argument is missing"));
					}
					continue;
				}
				ResolveActionArg(Definition, Schema, ArgName, Value, ArgPath, OutArgs);
			}
		}

		void ResolveActionArg(const FTDResolvedMonsterDefinition& Definition, const FTDActionArgSchema& Schema, const FString& ArgName, const TSharedPtr<FJsonValue>& Value, const FString& ArgPath, FTDActionArgs& OutArgs)
		{
			FString Text;
			float Number = 0.f;
			bool bFlag = false;
			switch (Schema.Kind)
			{
			case ETDActionArgKind::Target:
				if (!Value->TryGetString(Text) || !TargetNames.Contains(Text))
				{
					Reader.Error(ArgPath, FString::Printf(TEXT("target must be one of Player, NearestEnemy%s"), *MakeSuggestion(Text, TargetNames)));
				}
				return;
			case ETDActionArgKind::Number:
			case ETDActionArgKind::Seconds:
				if (!Reader.ReadNumberValue(Value, ArgPath, Number))
				{
					return;
				}
				if (Number < 0.f)
				{
					Reader.Error(ArgPath, TEXT("must be >= 0"));
				}
				AssignNumberArg(ArgName, Number, OutArgs);
				return;
			case ETDActionArgKind::Ability:
				if (!Value->TryGetString(Text))
				{
					Reader.Error(ArgPath, TEXT("expected an ability name"));
					return;
				}
				OutArgs.AbilityIndex = Definition.Abilities.IndexOfByPredicate([&Text](const FTDResolvedAbility& Ability) { return Ability.Name == FName(*Text); });
				if (OutArgs.AbilityIndex == INDEX_NONE)
				{
					Reader.Error(ArgPath, FString::Printf(TEXT("unknown ability '%s'"), *Text));
				}
				return;
			case ETDActionArgKind::Sequence:
				if (!Value->TryGetString(Text))
				{
					Reader.Error(ArgPath, TEXT("expected a sequence name"));
					return;
				}
				OutArgs.SequenceIndex = Definition.SequenceNames.IndexOfByKey(FName(*Text));
				if (OutArgs.SequenceIndex == INDEX_NONE)
				{
					Reader.Error(ArgPath, FString::Printf(TEXT("unknown sequence '%s'"), *Text));
				}
				return;
			case ETDActionArgKind::Flag:
				if (!Value->TryGetBool(bFlag))
				{
					Reader.Error(ArgPath, TEXT("expected true or false"));
					return;
				}
				OutArgs.bLeadTarget = bFlag;
				return;
			}
		}

		static void AssignNumberArg(const FString& ArgName, const float Number, FTDActionArgs& OutArgs)
		{
			if (ArgName == TEXT("stop_at"))
			{
				OutArgs.StopAtDistance = Number;
				return;
			}
			if (ArgName == TEXT("speed_scale"))
			{
				OutArgs.SpeedScale = Number;
				return;
			}
			if (ArgName == TEXT("min"))
			{
				OutArgs.BandMin = Number;
				return;
			}
			if (ArgName == TEXT("max"))
			{
				OutArgs.BandMax = Number;
				return;
			}
			if (ArgName == TEXT("seconds"))
			{
				OutArgs.Seconds = Number;
			}
		}

		TArray<FTDResolvedAction> ResolveActions(const FTDResolvedMonsterDefinition& Definition, const TArray<TSharedPtr<FJsonValue>>& ActionValues, const FString& Path)
		{
			TArray<FTDResolvedAction> Actions;
			TSet<FName> SeenIds;
			for (int32 ActionIndex = 0; ActionIndex < ActionValues.Num(); ++ActionIndex)
			{
				const FString ActionPath = IndexPath(Path, ActionIndex);
				const TSharedPtr<FJsonObject>* ActionObject = nullptr;
				if (!ActionValues[ActionIndex]->TryGetObject(ActionObject) || !ActionObject)
				{
					Reader.Error(ActionPath, TEXT("expected an action object"));
					continue;
				}
				bool bIsRemoval = false;
				(*ActionObject)->TryGetBoolField(TEXT("remove"), bIsRemoval);
				if (bIsRemoval)
				{
					Reader.Error(ActionPath, TEXT("remove is only valid when a parent or earlier phase defines the action"));
					continue;
				}
				Reader.CheckKeys(*ActionObject, ActionPath, ActionKeys);
				FTDResolvedAction& Action = Actions.AddDefaulted_GetRef();
				FString ActionId;
				Reader.ReadRequiredString(*ActionObject, TEXT("id"), ActionPath, ActionId);
				Action.Id = FName(*ActionId);
				if (SeenIds.Contains(Action.Id))
				{
					Reader.Error(ActionPath, FString::Printf(TEXT("duplicate action id '%s'"), *ActionId));
				}
				SeenIds.Add(Action.Id);
				const FTDMonsterPrimitiveInfo* Primitive = ResolvePrimitive(Definition, *ActionObject, ActionPath, Action.Args);
				if (Primitive)
				{
					Action.Primitive = Primitive->Primitive;
				}
				Reader.ReadRequiredNumber(*ActionObject, TEXT("weight"), ActionPath, Action.Weight);
				if (Action.Weight <= 0.f)
				{
					Reader.Error(JoinPath(ActionPath, TEXT("weight")), TEXT("must be > 0"));
				}
				float CooldownSeconds = 0.f;
				Reader.ReadOptionalNumber(*ActionObject, TEXT("cooldown"), ActionPath, CooldownSeconds);
				Action.CooldownSteps = TDMonsterAI::SecondsToSteps(CooldownSeconds);
				ResolveConsiderations(Definition, *ActionObject, ActionPath, Action);
			}
			return Actions;
		}

		void ResolveConsiderations(const FTDResolvedMonsterDefinition& Definition, const TSharedPtr<FJsonObject>& ActionObject, const FString& ActionPath, FTDResolvedAction& Action)
		{
			const FString Path = JoinPath(ActionPath, TEXT("considerations"));
			const TArray<TSharedPtr<FJsonValue>>* Values = Reader.ReadOptionalArray(ActionObject, TEXT("considerations"), ActionPath);
			if (!Values)
			{
				return;
			}
			for (int32 Index = 0; Index < Values->Num(); ++Index)
			{
				const FString ConsiderationPath = IndexPath(Path, Index);
				const TSharedPtr<FJsonObject>* Object = nullptr;
				if (!(*Values)[Index]->TryGetObject(Object) || !Object)
				{
					Reader.Error(ConsiderationPath, TEXT("expected a consideration object"));
					continue;
				}
				Reader.CheckKeys(*Object, ConsiderationPath, ConsiderationKeys);
				FTDResolvedConsideration Consideration;
				const bool bHasInput = ResolveInput(Definition, *Object, ConsiderationPath, Consideration.Input, Consideration.Args);
				ResolveCurve(*Object, ConsiderationPath, Consideration.Curve);
				if (!bHasInput)
				{
					continue;
				}
				Consideration.RangeMin = Consideration.Input->DefaultRangeMin;
				Consideration.RangeMax = Consideration.Input->DefaultRangeMax;
				ResolveRange(*Object, ConsiderationPath, Consideration);
				Action.Considerations.Add(Consideration);
			}
			Action.Considerations.StableSort([](const FTDResolvedConsideration& Left, const FTDResolvedConsideration& Right)
			{
				return static_cast<uint8>(Left.Input->Cost) < static_cast<uint8>(Right.Input->Cost);
			});
		}

		bool ResolveInput(const FTDResolvedMonsterDefinition& Definition, const TSharedPtr<FJsonObject>& Object, const FString& Path, const FTDBrainInputFunction*& OutInput, FTDInputArgs& OutArgs)
		{
			FString InputName;
			if (!Reader.ReadRequiredString(Object, TEXT("input"), Path, InputName))
			{
				return false;
			}
			OutInput = FTDBrainInputRegistry::Find(FName(*InputName));
			if (!OutInput)
			{
				Reader.Error(JoinPath(Path, TEXT("input")), FString::Printf(TEXT("unknown input '%s'%s"), *InputName, *MakeSuggestion(InputName, FTDBrainInputRegistry::GetNames())));
				return false;
			}
			const TSharedPtr<FJsonObject> Args = Reader.ReadOptionalObject(Object, TEXT("args"), Path);
			const FString ArgsPath = JoinPath(Path, TEXT("args"));
			switch (OutInput->ArgKind)
			{
			case ETDInputArgKind::None:
				if (Args && Args->Values.Num() > 0)
				{
					Reader.Error(ArgsPath, FString::Printf(TEXT("input '%s' takes no arguments"), *InputName));
				}
				return true;
			case ETDInputArgKind::Radius:
				OutArgs.Radius = OutInput->DefaultRadius;
				if (Args)
				{
					Reader.CheckKeys(Args, ArgsPath, { TEXT("radius") });
					Reader.ReadOptionalNumber(Args, TEXT("radius"), ArgsPath, OutArgs.Radius);
				}
				return true;
			case ETDInputArgKind::Ability:
			{
				FString AbilityName;
				if (!Args || !Args->TryGetStringField(TEXT("ability"), AbilityName))
				{
					Reader.Error(JoinPath(ArgsPath, TEXT("ability")), TEXT("required ability name is missing"));
					return false;
				}
				Reader.CheckKeys(Args, ArgsPath, { TEXT("ability") });
				OutArgs.AbilityIndex = Definition.Abilities.IndexOfByPredicate([&AbilityName](const FTDResolvedAbility& Ability) { return Ability.Name == FName(*AbilityName); });
				if (OutArgs.AbilityIndex == INDEX_NONE)
				{
					Reader.Error(JoinPath(ArgsPath, TEXT("ability")), FString::Printf(TEXT("unknown ability '%s'"), *AbilityName));
					return false;
				}
				return true;
			}
			}
			return true;
		}

		void ResolveRange(const TSharedPtr<FJsonObject>& Object, const FString& Path, FTDResolvedConsideration& Consideration)
		{
			const TArray<TSharedPtr<FJsonValue>>* Range = Reader.ReadOptionalArray(Object, TEXT("range"), Path);
			if (!Range)
			{
				return;
			}
			const FString RangePath = JoinPath(Path, TEXT("range"));
			if (Range->Num() != 2)
			{
				Reader.Error(RangePath, TEXT("expected [min, max]"));
				return;
			}
			Reader.ReadNumberValue((*Range)[0], IndexPath(RangePath, 0), Consideration.RangeMin);
			Reader.ReadNumberValue((*Range)[1], IndexPath(RangePath, 1), Consideration.RangeMax);
			if (Consideration.RangeMax <= Consideration.RangeMin)
			{
				Reader.Error(RangePath, TEXT("max must be greater than min"));
			}
		}

		void ResolveCurve(const TSharedPtr<FJsonObject>& Object, const FString& Path, FTDResponseCurve& OutCurve)
		{
			FString CurveName;
			if (Reader.ReadRequiredString(Object, TEXT("curve"), Path, CurveName) && !FTDResponseCurve::TryParseType(CurveName, OutCurve.Type))
			{
				Reader.Error(JoinPath(Path, TEXT("curve")), FString::Printf(TEXT("unknown curve '%s'%s"), *CurveName, *MakeSuggestion(CurveName, FTDResponseCurve::GetTypeNames())));
			}
			Reader.ReadOptionalNumber(Object, TEXT("m"), Path, OutCurve.M);
			Reader.ReadOptionalNumber(Object, TEXT("k"), Path, OutCurve.K);
			Reader.ReadOptionalNumber(Object, TEXT("b"), Path, OutCurve.B);
			Reader.ReadOptionalNumber(Object, TEXT("c"), Path, OutCurve.C);
			bool bInvert = false;
			if (Object->TryGetBoolField(TEXT("invert"), bInvert))
			{
				OutCurve.bInvert = bInvert;
			}
		}

		void ResolvePhases(FTDResolvedMonsterDefinition& Definition, const TSharedPtr<FJsonObject>& Root, const TArray<TSharedPtr<FJsonValue>>& BaseActionValues)
		{
			const TArray<TSharedPtr<FJsonValue>>* PhaseValues = Reader.ReadOptionalArray(Root, TEXT("phases"), FString());
			if (!PhaseValues)
			{
				return;
			}
			TArray<TSharedPtr<FJsonValue>> CurrentActionValues = BaseActionValues;
			for (int32 PhaseIndex = 0; PhaseIndex < PhaseValues->Num(); ++PhaseIndex)
			{
				const FString Path = IndexPath(TEXT("phases"), PhaseIndex);
				const TSharedPtr<FJsonObject>* PhaseObject = nullptr;
				if (!(*PhaseValues)[PhaseIndex]->TryGetObject(PhaseObject) || !PhaseObject)
				{
					Reader.Error(Path, TEXT("expected a phase object"));
					continue;
				}
				Reader.CheckKeys(*PhaseObject, Path, PhaseKeys);
				FTDResolvedPhase& Phase = Definition.Phases.AddDefaulted_GetRef();
				FString PhaseId;
				Reader.ReadRequiredString(*PhaseObject, TEXT("id"), Path, PhaseId);
				Phase.Id = FName(*PhaseId);
				ResolveEnterCondition(Definition, *PhaseObject, Path, Phase);
				FString OnEnterName;
				if ((*PhaseObject)->TryGetStringField(TEXT("on_enter"), OnEnterName))
				{
					Phase.OnEnterSequenceIndex = Definition.SequenceNames.IndexOfByKey(FName(*OnEnterName));
					if (Phase.OnEnterSequenceIndex == INDEX_NONE)
					{
						Reader.Error(JoinPath(Path, TEXT("on_enter")), FString::Printf(TEXT("unknown sequence '%s'"), *OnEnterName));
					}
				}
				ResolvePhaseStats(*PhaseObject, Path, Phase);
				if (const TArray<TSharedPtr<FJsonValue>>* PhaseActions = Reader.ReadOptionalArray(*PhaseObject, TEXT("actions"), Path))
				{
					CurrentActionValues = MergeActionArrays(CurrentActionValues, *PhaseActions, JoinPath(Path, TEXT("actions")), Reader);
				}
				Definition.ActionsByPhase.Add(ResolveActions(Definition, CurrentActionValues, JoinPath(Path, TEXT("actions"))));
			}
		}

		void ResolveEnterCondition(const FTDResolvedMonsterDefinition& Definition, const TSharedPtr<FJsonObject>& PhaseObject, const FString& Path, FTDResolvedPhase& Phase)
		{
			const TSharedPtr<FJsonObject> EnterWhen = Reader.ReadOptionalObject(PhaseObject, TEXT("enter_when"), Path);
			const FString ConditionPath = JoinPath(Path, TEXT("enter_when"));
			if (!EnterWhen)
			{
				Reader.Error(ConditionPath, TEXT("enter_when is required"));
				return;
			}
			Reader.CheckKeys(EnterWhen, ConditionPath, EnterWhenKeys);
			ResolveInput(Definition, EnterWhen, ConditionPath, Phase.Input, Phase.InputArgs);
			FString OperatorText;
			if (Reader.ReadRequiredString(EnterWhen, TEXT("op"), ConditionPath, OperatorText))
			{
				static const TMap<FString, ETDCompareOp> Operators =
				{
					{ TEXT("<"), ETDCompareOp::Less }, { TEXT("<="), ETDCompareOp::LessOrEqual }, { TEXT(">"), ETDCompareOp::Greater },
					{ TEXT(">="), ETDCompareOp::GreaterOrEqual }, { TEXT("=="), ETDCompareOp::Equal }
				};
				if (const ETDCompareOp* Operator = Operators.Find(OperatorText))
				{
					Phase.Op = *Operator;
				}
				else
				{
					Reader.Error(JoinPath(ConditionPath, TEXT("op")), TEXT("op must be one of <, <=, >, >=, =="));
				}
			}
			Reader.ReadRequiredNumber(EnterWhen, TEXT("value"), ConditionPath, Phase.Value);
		}

		void ResolvePhaseStats(const TSharedPtr<FJsonObject>& PhaseObject, const FString& Path, FTDResolvedPhase& Phase)
		{
			const TSharedPtr<FJsonObject> Stats = Reader.ReadOptionalObject(PhaseObject, TEXT("stats"), Path);
			if (!Stats)
			{
				return;
			}
			const FString StatsPath = JoinPath(Path, TEXT("stats"));
			Reader.CheckKeys(Stats, StatsPath, PhaseStatKeys);
			float Number = 0.f;
			if (Reader.ReadOptionalNumber(Stats, TEXT("max_health"), StatsPath, Number))
			{
				Phase.Stats.MaxHealth = Number;
			}
			if (Reader.ReadOptionalNumber(Stats, TEXT("attack_power"), StatsPath, Number))
			{
				Phase.Stats.AttackPower = Number;
			}
			if (Reader.ReadOptionalNumber(Stats, TEXT("armor"), StatsPath, Number))
			{
				Phase.Stats.Armor = Number;
			}
			if (Reader.ReadOptionalNumber(Stats, TEXT("move_speed"), StatsPath, Number))
			{
				Phase.Stats.MoveSpeed = Number;
			}
		}

		FTDDefinitionReader& Reader;
		FTDMonsterSpellResolver SpellResolver;
	};

	TSharedPtr<FJsonObject> BuildMergedRoot(const FString& Id, const TMap<FString, TSharedPtr<FJsonObject>>& Roots, FTDDefinitionReader& Reader)
	{
		TArray<TSharedPtr<FJsonObject>> Chain;
		TSet<FString> Visited;
		FString CurrentId = Id;
		while (!CurrentId.IsEmpty())
		{
			if (Visited.Contains(CurrentId))
			{
				Reader.Error(TEXT("extends"), FString::Printf(TEXT("extends cycle through '%s'"), *CurrentId));
				return nullptr;
			}
			const TSharedPtr<FJsonObject>* Root = Roots.Find(CurrentId);
			if (!Root)
			{
				Reader.Error(TEXT("extends"), FString::Printf(TEXT("unknown parent definition '%s'"), *CurrentId));
				return nullptr;
			}
			Visited.Add(CurrentId);
			Chain.Add(*Root);
			CurrentId.Reset();
			(*Root)->TryGetStringField(TEXT("extends"), CurrentId);
		}
		if (Chain.Num() - 1 > MaxExtendsDepth)
		{
			Reader.Error(TEXT("extends"), FString::Printf(TEXT("extends depth %d exceeds %d"), Chain.Num() - 1, MaxExtendsDepth));
			return nullptr;
		}
		TSharedPtr<FJsonObject> Merged = Chain.Last();
		for (int32 ChainIndex = Chain.Num() - 2; ChainIndex >= 0; --ChainIndex)
		{
			Merged = MergeDefinitionObjects(Merged, Chain[ChainIndex], Reader);
		}
		Merged->SetStringField(TEXT("id"), Id);
		return Merged;
	}
}

void UTDMonsterDefinitionLibrary::ResolveDefinitionTexts(const TMap<FString, FString>& FileTexts, FTDMonsterSpellResolver SpellResolver, TMap<FName, TSharedPtr<const FTDResolvedMonsterDefinition>>& OutDefinitions, TArray<FString>& OutErrors)
{
	OutDefinitions.Reset();
	TMap<FString, TSharedPtr<FJsonObject>> Roots;
	TMap<FString, FString> FilesById;
	for (const TPair<FString, FString>& FileText : FileTexts)
	{
		FTDDefinitionReader Reader(FileText.Key, OutErrors);
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(FileText.Value);
		if (!FJsonSerializer::Deserialize(JsonReader, Root) || !Root.IsValid())
		{
			Reader.Error(FString::Printf(TEXT("line %d"), JsonReader->GetLineNumber()), FString::Printf(TEXT("invalid JSON: %s"), *JsonReader->GetErrorMessage()));
			continue;
		}
		FString Id;
		if (!Reader.ReadRequiredString(Root, TEXT("id"), FString(), Id))
		{
			continue;
		}
		const FString ExpectedId = FPaths::GetBaseFilename(FileText.Key);
		if (Id != ExpectedId)
		{
			Reader.Error(TEXT("id"), FString::Printf(TEXT("id '%s' must match the file name '%s'"), *Id, *ExpectedId));
			continue;
		}
		Roots.Add(Id, Root);
		FilesById.Add(Id, FileText.Key);
	}

	TArray<FString> SortedIds;
	Roots.GetKeys(SortedIds);
	SortedIds.Sort();
	for (const FString& Id : SortedIds)
	{
		FTDDefinitionReader Reader(FilesById[Id], OutErrors);
		const TSharedPtr<FJsonObject> Merged = BuildMergedRoot(Id, Roots, Reader);
		if (!Merged)
		{
			continue;
		}
		FTDDefinitionResolver Resolver(Reader, SpellResolver);
		const TSharedPtr<FTDResolvedMonsterDefinition> Definition = Resolver.Resolve(FilesById[Id], Merged);
		if (Definition)
		{
			OutDefinitions.Add(Definition->Id, Definition);
		}
	}
}

void UTDMonsterDefinitionLibrary::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReloadCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("TD.MonsterAI.Reload"),
		TEXT("Reloads Content/MonsterAI/Definitions/*.json. Monsters spawned afterwards use the new definitions."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			ReloadDefinitions();
		}),
		ECVF_Default);
}

void UTDMonsterDefinitionLibrary::Deinitialize()
{
	if (ReloadCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ReloadCommand);
		ReloadCommand = nullptr;
	}
	Definitions.Reset();
	SpellsByName.Reset();
	OwnedSpells.Reset();
	Super::Deinitialize();
}

UTDMonsterDefinitionLibrary* UTDMonsterDefinitionLibrary::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UTDMonsterDefinitionLibrary>() : nullptr;
}

FString UTDMonsterDefinitionLibrary::GetDefinitionDirectory()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MonsterAI"), TEXT("Definitions"));
}

TSharedPtr<const FTDResolvedMonsterDefinition> UTDMonsterDefinitionLibrary::FindDefinition(const FName DefinitionId)
{
	EnsureLoaded();
	const TSharedPtr<const FTDResolvedMonsterDefinition>* Definition = Definitions.Find(DefinitionId);
	return Definition ? *Definition : nullptr;
}

TArray<FName> UTDMonsterDefinitionLibrary::GetDefinitionIds()
{
	EnsureLoaded();
	TArray<FName> Ids;
	Definitions.GetKeys(Ids);
	return Ids;
}

UTDDamageDefinition* UTDMonsterDefinitionLibrary::FindSpell(const FName SpellName)
{
	EnsureAttackCatalog();
	if (UTDDamageDefinition* const* Spell = SpellsByName.Find(SpellName))
	{
		return *Spell;
	}
	const FString NameText = SpellName.ToString();
	const FString CandidatePaths[] =
	{
		FString::Printf(TEXT("/Game/MonsterAI/Attacks/%s.%s"), *NameText, *NameText),
		FString::Printf(TEXT("/Game/Combat/Examples/%s.%s"), *NameText, *NameText)
	};
	for (const FString& CandidatePath : CandidatePaths)
	{
		if (UTDDamageDefinition* Loaded = LoadObject<UTDDamageDefinition>(nullptr, *CandidatePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			OwnedSpells.Add(Loaded);
			SpellsByName.Add(SpellName, Loaded);
			return Loaded;
		}
	}
	return nullptr;
}

int32 UTDMonsterDefinitionLibrary::ReloadDefinitions()
{
	EnsureAttackCatalog();
	TMap<FString, FString> FileTexts;
	TArray<FString> FileNames;
	const FString Directory = GetDefinitionDirectory();
	IFileManager::Get().FindFiles(FileNames, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
	FileNames.Sort();
	for (const FString& FileName : FileNames)
	{
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(Directory, FileName)))
		{
			FileTexts.Add(FileName, Text);
		}
	}

	LoadErrors.Reset();
	ResolveDefinitionTexts(FileTexts, [this](const FName SpellName) { return FindSpell(SpellName); }, Definitions, LoadErrors);
	bHasLoaded = true;
	for (const FString& Error : LoadErrors)
	{
		UE_LOG(LogTDMonsterDefinition, Error, TEXT("%s"), *Error);
	}
	UE_LOG(LogTDMonsterDefinition, Log, TEXT("Loaded %d monster definitions from %s with %d errors"), Definitions.Num(), *Directory, LoadErrors.Num());
	return LoadErrors.Num();
}

void UTDMonsterDefinitionLibrary::EnsureLoaded()
{
	if (bHasLoaded)
	{
		return;
	}
	ReloadDefinitions();
}

void UTDMonsterDefinitionLibrary::EnsureAttackCatalog()
{
	if (!SpellsByName.IsEmpty())
	{
		return;
	}
	TDMonsterAttackCatalog::CreateAttacks(this, SpellsByName);
	for (const TPair<FName, UTDDamageDefinition*>& Entry : SpellsByName)
	{
		OwnedSpells.Add(Entry.Value);
	}
}
