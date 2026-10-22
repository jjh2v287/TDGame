#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidPresetTuning, Log, All);

namespace
{
	const TCHAR* TDFluidPresetTuningStructDefaultsKey = TEXT("StructDefaults");
	const TCHAR* TDFluidPresetTuningGroupsKey = TEXT("Groups");
	const TCHAR* TDFluidPresetTuningKindsKey = TEXT("Kinds");
	const TCHAR* TDFluidPresetTuningGroupMembersKey = TEXT("Kinds");

	TArray<const UScriptStruct*> TDFluidPresetTuningStructs()
	{
		return {
			FTDFluidSimulationConfig::StaticStruct(),
			FTDFluidSourceEmitterSettings::StaticStruct(),
			FTDFluidFragmentTrailSettings::StaticStruct(),
			FTDFluidVolumeRenderSettings::StaticStruct(),
			FTDFluidFlameCardLayerSettings::StaticStruct()
		};
	}

	TArray<ETDFluidPresetKind> TDFluidPresetTuningAllKinds()
	{
		const UEnum* KindEnum = StaticEnum<ETDFluidPresetKind>();
		TArray<ETDFluidPresetKind> Kinds;
		for (int32 Index = 0; Index < KindEnum->NumEnums() - 1; ++Index)
		{
			Kinds.Add(static_cast<ETDFluidPresetKind>(KindEnum->GetValueByIndex(Index)));
		}
		return Kinds;
	}

	bool TDFluidPresetTuningFindKind(const FString& Name, ETDFluidPresetKind& OutKind)
	{
		const UEnum* KindEnum = StaticEnum<ETDFluidPresetKind>();
		for (int32 Index = 0; Index < KindEnum->NumEnums() - 1; ++Index)
		{
			if (KindEnum->GetNameStringByIndex(Index).Equals(Name, ESearchCase::CaseSensitive))
			{
				OutKind = static_cast<ETDFluidPresetKind>(KindEnum->GetValueByIndex(Index));
				return true;
			}
		}
		return false;
	}

	const UScriptStruct* TDFluidPresetTuningFindStruct(const FString& Name)
	{
		for (const UScriptStruct* Struct : TDFluidPresetTuningStructs())
		{
			if (Struct->GetName().Equals(Name, ESearchCase::CaseSensitive))
			{
				return Struct;
			}
		}
		return nullptr;
	}

	bool TDFluidPresetTuningIsIntegerInRange(const FNumericProperty* Property, double Number)
	{
		if (Property->IsA<FIntProperty>())
		{
			return Number >= static_cast<double>(MIN_int32) && Number <= static_cast<double>(MAX_int32);
		}
		if (Property->IsA<FByteProperty>())
		{
			return Number >= 0.0 && Number <= static_cast<double>(MAX_uint8);
		}
		if (Property->IsA<FUInt32Property>())
		{
			return Number >= 0.0 && Number <= static_cast<double>(MAX_uint32);
		}
		return false;
	}

	void TDFluidPresetTuningValidateNumber(const FNumericProperty* Property, const FJsonValue& Value, const FString& Path, TArray<FString>& OutErrors)
	{
		if (Value.Type != EJson::Number)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: expected a number"), *Path));
			return;
		}
		const double Number = Value.AsNumber();
		if (!FMath::IsFinite(Number))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: number must be finite"), *Path));
			return;
		}
		if (Property->IsInteger())
		{
			if (FMath::FloorToDouble(Number) != Number || !TDFluidPresetTuningIsIntegerInRange(Property, Number))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: expected an integer inside the %s range"), *Path, *Property->GetCPPType()));
			}
			return;
		}
		if (Property->IsA<FFloatProperty>() && FMath::Abs(Number) > static_cast<double>(MAX_flt))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: number is outside the float range"), *Path));
		}
	}

	void TDFluidPresetTuningValidateEnumName(const UEnum* Enum, const FJsonValue& Value, const FString& Path, TArray<FString>& OutErrors)
	{
		if (Value.Type != EJson::String)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: expected a %s name string"), *Path, *Enum->GetName()));
			return;
		}
		const FString Name = Value.AsString();
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			if (Enum->GetNameStringByIndex(Index).Equals(Name, ESearchCase::CaseSensitive))
			{
				return;
			}
		}
		OutErrors.Add(FString::Printf(TEXT("%s: %s has no name '%s' (case sensitive)"), *Path, *Enum->GetName(), *Name));
	}

	void TDFluidPresetTuningValidateFields(const UStruct* Struct, const TSharedPtr<FJsonObject>& Object, const FString& Path, bool bRequiresAllFields, TArray<FString>& OutErrors);

	void TDFluidPresetTuningValidateValue(const FProperty* Property, const TSharedPtr<FJsonValue>& Value, const FString& Path, TArray<FString>& OutErrors)
	{
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: null is not a value"), *Path));
			return;
		}
		if (Property->IsA<FBoolProperty>())
		{
			if (Value->Type != EJson::Boolean)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: expected true or false"), *Path));
			}
			return;
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			TDFluidPresetTuningValidateEnumName(EnumProperty->GetEnum(), *Value, Path, OutErrors);
			return;
		}
		const FByteProperty* ByteProperty = CastField<FByteProperty>(Property);
		if (ByteProperty && ByteProperty->Enum)
		{
			TDFluidPresetTuningValidateEnumName(ByteProperty->Enum, *Value, Path, OutErrors);
			return;
		}
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			TDFluidPresetTuningValidateNumber(NumericProperty, *Value, Path, OutErrors);
			return;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (Value->Type != EJson::Object)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: expected a %s component object"), *Path, *StructProperty->Struct->GetName()));
				return;
			}
			TDFluidPresetTuningValidateFields(StructProperty->Struct, Value->AsObject(), Path, true, OutErrors);
			return;
		}
		OutErrors.Add(FString::Printf(TEXT("%s: unsupported property type %s"), *Path, *Property->GetCPPType()));
	}

	void TDFluidPresetTuningValidateFields(const UStruct* Struct, const TSharedPtr<FJsonObject>& Object, const FString& Path, bool bRequiresAllFields, TArray<FString>& OutErrors)
	{
		if (!Object.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("%s: expected an object"), *Path));
			return;
		}
		for (const auto& Field : Object->Values)
		{
			const FString Key(*Field.Key);
			const FProperty* Property = Struct->FindPropertyByName(FName(*Key));
			if (!Property || !Property->GetName().Equals(Key, ESearchCase::CaseSensitive))
			{
				OutErrors.Add(FString::Printf(TEXT("%s.%s: %s has no such field (case sensitive)"), *Path, *Key, *Struct->GetName()));
				continue;
			}
			TDFluidPresetTuningValidateValue(Property, Field.Value, Path + TEXT(".") + Key, OutErrors);
		}
		if (!bRequiresAllFields)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (!Object->HasField(It->GetName()))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: field %s is missing"), *Path, *It->GetName()));
			}
		}
	}

	void TDFluidPresetTuningClaimFields(const TSharedPtr<FJsonObject>& Fields, ETDFluidPresetKind Kind, const UScriptStruct* Struct, const FString& Owner, TMap<FString, FString>& OwnerByTarget, TArray<FString>& OutErrors)
	{
		for (const auto& Field : Fields->Values)
		{
			const FString Target = FString::Printf(TEXT("%s.%s.%s"), *StaticEnum<ETDFluidPresetKind>()->GetNameStringByValue(static_cast<int64>(Kind)), *Struct->GetName(), *FString(*Field.Key));
			if (const FString* FirstOwner = OwnerByTarget.Find(Target))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: set in both %s and %s (one place rule)"), *Target, **FirstOwner, *Owner));
				continue;
			}
			OwnerByTarget.Add(Target, Owner);
		}
	}

	TSharedPtr<FJsonValue> TDFluidPresetTuningFindExactField(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		for (const auto& Field : Object->Values)
		{
			if (FString(*Field.Key).Equals(Key, ESearchCase::CaseSensitive))
			{
				return Field.Value;
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> TDFluidPresetTuningObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, const FString& Path, TArray<FString>& OutErrors)
	{
		const TSharedPtr<FJsonValue> Value = TDFluidPresetTuningFindExactField(Object, Key);
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			OutErrors.Add(FString::Printf(TEXT("%s%s: expected an object"), *Path, Key));
			return nullptr;
		}
		return Value->AsObject();
	}

	void TDFluidPresetTuningAddLayers(const TSharedPtr<FJsonObject>& Entries, const TArray<ETDFluidPresetKind>& Kinds, const FString& Path, const FString& IgnoredKey, FTDFluidPresetTuningTable& Table, TMap<FString, FString>& OwnerByTarget, TArray<FString>& OutErrors)
	{
		for (const auto& Entry : Entries->Values)
		{
			const FString Key(*Entry.Key);
			if (!IgnoredKey.IsEmpty() && Key.Equals(IgnoredKey, ESearchCase::CaseSensitive))
			{
				continue;
			}
			const FString EntryPath = Path + TEXT(".") + Key;
			const UScriptStruct* Struct = TDFluidPresetTuningFindStruct(Key);
			if (!Struct)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: unknown struct name (case sensitive)"), *EntryPath));
				continue;
			}
			const TSharedPtr<FJsonObject> Fields = Entry.Value.IsValid() && Entry.Value->Type == EJson::Object ? Entry.Value->AsObject() : TSharedPtr<FJsonObject>();
			TDFluidPresetTuningValidateFields(Struct, Fields, EntryPath, false, OutErrors);
			if (!Fields.IsValid())
			{
				continue;
			}
			for (const ETDFluidPresetKind Kind : Kinds)
			{
				TDFluidPresetTuningClaimFields(Fields, Kind, Struct, Path, OwnerByTarget, OutErrors);
				Table.LayersByKind.FindOrAdd(Kind).FindOrAdd(Struct).Add(Fields);
			}
		}
	}

	TArray<ETDFluidPresetKind> TDFluidPresetTuningReadGroupMembers(const TSharedPtr<FJsonObject>& Group, const FString& Path, TArray<FString>& OutErrors)
	{
		TArray<ETDFluidPresetKind> Members;
		const TSharedPtr<FJsonValue> MembersValue = TDFluidPresetTuningFindExactField(Group, TDFluidPresetTuningGroupMembersKey);
		if (!MembersValue.IsValid() || MembersValue->Type != EJson::Array || MembersValue->AsArray().IsEmpty())
		{
			OutErrors.Add(FString::Printf(TEXT("%s.%s: expected a non empty array of kind names"), *Path, TDFluidPresetTuningGroupMembersKey));
			return Members;
		}
		for (const TSharedPtr<FJsonValue>& MemberValue : MembersValue->AsArray())
		{
			ETDFluidPresetKind Kind = ETDFluidPresetKind::Custom;
			const FString Name = MemberValue.IsValid() && MemberValue->Type == EJson::String ? MemberValue->AsString() : FString();
			if (!TDFluidPresetTuningFindKind(Name, Kind))
			{
				OutErrors.Add(FString::Printf(TEXT("%s.%s: unknown kind '%s' (case sensitive)"), *Path, TDFluidPresetTuningGroupMembersKey, *Name));
				continue;
			}
			if (Members.Contains(Kind))
			{
				OutErrors.Add(FString::Printf(TEXT("%s.%s: kind %s is listed twice"), *Path, TDFluidPresetTuningGroupMembersKey, *Name));
				continue;
			}
			Members.Add(Kind);
		}
		return Members;
	}
}

void UTDFluidPresetTuningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReloadCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("TD.FluidFX.ReloadPresetTuning"),
		TEXT("Reloads Content/FluidFX/PresetData/TDFluidPresetTuning.json. Presets built afterwards use the new values; a failed reload keeps the previous table."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			ReloadPresetTuning();
		}),
		ECVF_Default);
	if (ReloadPresetTuning() == 0)
	{
		return;
	}
	ReportFirstLoadFailure();
}

void UTDFluidPresetTuningSubsystem::Deinitialize()
{
	if (ReloadCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ReloadCommand);
		ReloadCommand = nullptr;
	}
	Super::Deinitialize();
}

int32 UTDFluidPresetTuningSubsystem::ReloadPresetTuning()
{
	TArray<FString> Errors;
	FTDFluidPresetTuningTable ParsedTable;
	FString Text;
	const FString FilePath = GetTuningFilePath();
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		Errors.Add(FString::Printf(TEXT("cannot read preset tuning file %s"), *FilePath));
	}
	if (Errors.IsEmpty() && ParsePresetTuningText(Text, ParsedTable, Errors))
	{
		Table = MoveTemp(ParsedTable);
		bHasTable = true;
	}
	LoadErrors = Errors;
	for (const FString& Error : LoadErrors)
	{
		UE_LOG(LogTDFluidPresetTuning, Error, TEXT("%s"), *Error);
	}
	UE_LOG(LogTDFluidPresetTuning, Log, TEXT("Preset tuning %s: %d errors, table %s"), *FilePath, LoadErrors.Num(), bHasTable ? TEXT("loaded") : TEXT("missing"));
	return LoadErrors.Num();
}

TArray<FString> UTDFluidPresetTuningSubsystem::GetLoadErrors() const
{
	return LoadErrors;
}

FString UTDFluidPresetTuningSubsystem::GetTuningFilePath()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("FluidFX/PresetData/TDFluidPresetTuning.json"));
}

bool UTDFluidPresetTuningSubsystem::ParsePresetTuningText(const FString& Text, FTDFluidPresetTuningTable& OutTable, TArray<FString>& OutErrors)
{
	const int32 InitialErrorCount = OutErrors.Num();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("preset tuning text is not a JSON object"));
		return false;
	}
	const TArray<FString> TopLevelKeys = { TDFluidPresetTuningStructDefaultsKey, TDFluidPresetTuningGroupsKey, TDFluidPresetTuningKindsKey };
	for (const auto& Entry : Root->Values)
	{
		const FString Key(*Entry.Key);
		const bool bIsAllowedKey = TopLevelKeys.ContainsByPredicate([&Key](const FString& AllowedKey)
		{
			return AllowedKey.Equals(Key, ESearchCase::CaseSensitive);
		});
		if (!bIsAllowedKey)
		{
			OutErrors.Add(FString::Printf(TEXT("top level key '%s' is not allowed"), *Key));
		}
	}
	const TSharedPtr<FJsonObject> StructDefaults = TDFluidPresetTuningObjectField(Root, TDFluidPresetTuningStructDefaultsKey, FString(), OutErrors);
	const TSharedPtr<FJsonObject> Groups = TDFluidPresetTuningObjectField(Root, TDFluidPresetTuningGroupsKey, FString(), OutErrors);
	const TSharedPtr<FJsonObject> Kinds = TDFluidPresetTuningObjectField(Root, TDFluidPresetTuningKindsKey, FString(), OutErrors);
	if (!StructDefaults.IsValid() || !Groups.IsValid() || !Kinds.IsValid())
	{
		return false;
	}

	FTDFluidPresetTuningTable ParsedTable;
	const TArray<ETDFluidPresetKind> AllKinds = TDFluidPresetTuningAllKinds();
	for (const auto& Entry : StructDefaults->Values)
	{
		const FString Key(*Entry.Key);
		if (!TDFluidPresetTuningFindStruct(Key))
		{
			OutErrors.Add(FString::Printf(TEXT("StructDefaults.%s: unknown struct name (case sensitive)"), *Key));
		}
	}
	for (const UScriptStruct* Struct : TDFluidPresetTuningStructs())
	{
		const TSharedPtr<FJsonValue> DefaultsValue = TDFluidPresetTuningFindExactField(StructDefaults, Struct->GetName());
		const TSharedPtr<FJsonObject> Defaults = DefaultsValue.IsValid() && DefaultsValue->Type == EJson::Object ? DefaultsValue->AsObject() : TSharedPtr<FJsonObject>();
		TDFluidPresetTuningValidateFields(Struct, Defaults, FString(TDFluidPresetTuningStructDefaultsKey) + TEXT(".") + Struct->GetName(), true, OutErrors);
		if (!Defaults.IsValid())
		{
			continue;
		}
		for (const ETDFluidPresetKind Kind : AllKinds)
		{
			ParsedTable.LayersByKind.FindOrAdd(Kind).FindOrAdd(Struct).Add(Defaults);
		}
	}

	TMap<FString, FString> OwnerByTarget;
	for (const auto& Entry : Groups->Values)
	{
		const FString GroupPath = FString(TDFluidPresetTuningGroupsKey) + TEXT(".") + FString(*Entry.Key);
		if (!Entry.Value.IsValid() || Entry.Value->Type != EJson::Object)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: expected an object"), *GroupPath));
			continue;
		}
		const TSharedPtr<FJsonObject> Group = Entry.Value->AsObject();
		const TArray<ETDFluidPresetKind> Members = TDFluidPresetTuningReadGroupMembers(Group, GroupPath, OutErrors);
		TDFluidPresetTuningAddLayers(Group, Members, GroupPath, TDFluidPresetTuningGroupMembersKey, ParsedTable, OwnerByTarget, OutErrors);
	}

	for (const auto& Entry : Kinds->Values)
	{
		const FString KindName(*Entry.Key);
		const FString KindPath = FString(TDFluidPresetTuningKindsKey) + TEXT(".") + KindName;
		ETDFluidPresetKind Kind = ETDFluidPresetKind::Custom;
		if (!TDFluidPresetTuningFindKind(KindName, Kind))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: unknown kind (case sensitive)"), *KindPath));
			continue;
		}
		if (!Entry.Value.IsValid() || Entry.Value->Type != EJson::Object)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: expected an object"), *KindPath));
			continue;
		}
		TDFluidPresetTuningAddLayers(Entry.Value->AsObject(), { Kind }, KindPath, FString(), ParsedTable, OwnerByTarget, OutErrors);
	}

	if (OutErrors.Num() != InitialErrorCount)
	{
		return false;
	}
	OutTable = MoveTemp(ParsedTable);
	return true;
}

void UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind Kind, const UScriptStruct* Struct, void* OutStruct)
{
	const UTDFluidPresetTuningSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	if (!ensureMsgf(Subsystem, TEXT("UTDFluidPresetTuningSubsystem is not available; %s keeps header defaults"), *Struct->GetName()))
	{
		UE_LOG(LogTDFluidPresetTuning, Error, TEXT("Preset tuning subsystem is not available; %s for kind %d keeps header defaults"), *Struct->GetName(), static_cast<int32>(Kind));
		return;
	}
	if (!Subsystem->bHasTable)
	{
		return;
	}
	const TMap<const UScriptStruct*, TArray<TSharedPtr<FJsonObject>>>* LayersByStruct = Subsystem->Table.LayersByKind.Find(Kind);
	const TArray<TSharedPtr<FJsonObject>>* Layers = LayersByStruct ? LayersByStruct->Find(Struct) : nullptr;
	if (!Layers)
	{
		return;
	}
	for (const TSharedPtr<FJsonObject>& Layer : *Layers)
	{
		FJsonObjectConverter::JsonObjectToUStruct(Layer.ToSharedRef(), Struct, OutStruct);
	}
}

void UTDFluidPresetTuningSubsystem::ReportFirstLoadFailure() const
{
	const FString Summary = FString::Join(LoadErrors, TEXT(" | "));
	if (!GIsEditor)
	{
		UE_LOG(LogTDFluidPresetTuning, Fatal, TEXT("Preset tuning file %s failed to load (%d errors): %s"), *GetTuningFilePath(), LoadErrors.Num(), *Summary);
		return;
	}
	ensureMsgf(false, TEXT("Preset tuning file %s failed to load (%d errors); builders return header defaults until TD.FluidFX.ReloadPresetTuning succeeds: %s"), *GetTuningFilePath(), LoadErrors.Num(), *Summary);
}
