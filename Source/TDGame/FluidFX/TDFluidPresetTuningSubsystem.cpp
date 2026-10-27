#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidPlaneTypes.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidPresetTuning, Log, All);

namespace
{
	const TCHAR* TDFluidPresetTuningStructDefaultsKey = TEXT("StructDefaults");
	const TCHAR* TDFluidPresetTuningGroupsKey = TEXT("Groups");
	const TCHAR* TDFluidPresetTuningKindsKey = TEXT("Kinds");
	const TCHAR* TDFluidPresetTuningGlobalsKey = TEXT("Globals");
	const TCHAR* TDFluidPresetTuningGroupMembersKey = TEXT("Kinds");
	const TCHAR* TDFluidMaterialLookMaterialsKey = TEXT("Materials");
	const TCHAR* TDFluidMaterialLookNiagaraSystemsKey = TEXT("NiagaraSystems");
	const TCHAR* TDFluidGamePathPrefix = TEXT("/Game/");

#if WITH_EDITOR
	constexpr float TDFluidAutoReloadPollSeconds = 0.25f;

	TAutoConsoleVariable<float> CVarTDFluidPresetTuningAutoReloadSeconds(
		TEXT("TD.FluidFX.PresetTuningAutoReloadSeconds"),
		0.0f,
		TEXT("Editor only. Seconds between checks of the preset tuning and material look files; a changed file is reloaded. 0 turns the check off. A failed reload keeps the previous tables."),
		ECVF_Default);
#endif

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

	const FTDFluidPresetTuningRegisteredStruct* TDFluidPresetTuningFindStruct(const TArray<FTDFluidPresetTuningRegisteredStruct>& Registry, const FString& Name)
	{
		return Registry.FindByPredicate([&Name](const FTDFluidPresetTuningRegisteredStruct& Entry)
		{
			return Entry.Struct->GetName().Equals(Name, ESearchCase::CaseSensitive);
		});
	}

	bool TDFluidIsAsciiNameCharacter(TCHAR Character)
	{
		return (Character >= TEXT('A') && Character <= TEXT('Z')) || (Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('_');
	}

	bool TDFluidIsGamePath(const FString& Text)
	{
		const FString Prefix(TDFluidGamePathPrefix);
		if (!Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			return false;
		}
		bool bHasObjectName = false;
		TCHAR Previous = TEXT('/');
		for (int32 Index = Prefix.Len(); Index < Text.Len(); ++Index)
		{
			const TCHAR Character = Text[Index];
			const bool bIsSeparator = Character == TEXT('/') || Character == TEXT('.');
			if (!bIsSeparator && !TDFluidIsAsciiNameCharacter(Character))
			{
				return false;
			}
			if (bIsSeparator && (!TDFluidIsAsciiNameCharacter(Previous) || bHasObjectName))
			{
				return false;
			}
			bHasObjectName = bHasObjectName || Character == TEXT('.');
			Previous = Character;
		}
		return TDFluidIsAsciiNameCharacter(Previous);
	}

	bool TDFluidPresetTuningIsSoftPathProperty(const FProperty* Property)
	{
		if (Property->IsA<FSoftObjectProperty>())
		{
			return true;
		}
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty && StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get();
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

	void TDFluidPresetTuningValidateSoftPath(const FJsonValue& Value, const FString& Path, TArray<FString>& OutErrors)
	{
		if (Value.Type != EJson::String)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: expected a /Game/ path string"), *Path));
			return;
		}
		const FString Text = Value.AsString();
		if (!Text.IsEmpty() && !TDFluidIsGamePath(Text))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: '%s' is not an empty string or a /Game/Folder/Asset[.Object] path"), *Path, *Text));
		}
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
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (Value->Type != EJson::Array)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: expected an array"), *Path));
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>& Elements = Value->AsArray();
			for (int32 Index = 0; Index < Elements.Num(); ++Index)
			{
				TDFluidPresetTuningValidateValue(ArrayProperty->Inner, Elements[Index], FString::Printf(TEXT("%s[%d]"), *Path, Index), OutErrors);
			}
			return;
		}
		if (Property->IsA<FNameProperty>())
		{
			if (Value->Type != EJson::String)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: expected a name string"), *Path));
			}
			return;
		}
		if (TDFluidPresetTuningIsSoftPathProperty(Property))
		{
			TDFluidPresetTuningValidateSoftPath(*Value, Path, OutErrors);
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

	void TDFluidPresetTuningRejectUnknownKeys(const TSharedPtr<FJsonObject>& Object, const TArray<FString>& AllowedKeys, const FString& Path, TArray<FString>& OutErrors)
	{
		for (const auto& Entry : Object->Values)
		{
			const FString Key(*Entry.Key);
			const bool bIsAllowedKey = AllowedKeys.ContainsByPredicate([&Key](const FString& AllowedKey)
			{
				return AllowedKey.Equals(Key, ESearchCase::CaseSensitive);
			});
			if (!bIsAllowedKey)
			{
				OutErrors.Add(FString::Printf(TEXT("%skey '%s' is not allowed"), *Path, *Key));
			}
		}
	}

	const UScriptStruct* TDFluidPresetTuningFindScopedStruct(const TArray<FTDFluidPresetTuningRegisteredStruct>& Registry, const FString& Name, ETDFluidPresetTuningScope Scope, const FString& Path, TArray<FString>& OutErrors)
	{
		const FTDFluidPresetTuningRegisteredStruct* Entry = TDFluidPresetTuningFindStruct(Registry, Name);
		if (!Entry)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: unknown struct name (case sensitive)"), *Path));
			return nullptr;
		}
		if (Entry->Scope != Scope)
		{
			const TCHAR* Place = Entry->Scope == ETDFluidPresetTuningScope::Global ? TDFluidPresetTuningGlobalsKey : TDFluidPresetTuningStructDefaultsKey;
			OutErrors.Add(FString::Printf(TEXT("%s: %s is set under %s only"), *Path, *Name, Place));
			return nullptr;
		}
		return Entry->Struct;
	}

	void TDFluidPresetTuningAddLayers(const TArray<FTDFluidPresetTuningRegisteredStruct>& Registry, const TSharedPtr<FJsonObject>& Entries, const TArray<ETDFluidPresetKind>& Kinds, const FString& Path, const FString& IgnoredKey, FTDFluidPresetTuningTable& Table, TMap<FString, FString>& OwnerByTarget, TArray<FString>& OutErrors)
	{
		for (const auto& Entry : Entries->Values)
		{
			const FString Key(*Entry.Key);
			if (!IgnoredKey.IsEmpty() && Key.Equals(IgnoredKey, ESearchCase::CaseSensitive))
			{
				continue;
			}
			const FString EntryPath = Path + TEXT(".") + Key;
			const UScriptStruct* Struct = TDFluidPresetTuningFindScopedStruct(Registry, Key, ETDFluidPresetTuningScope::PresetKind, EntryPath, OutErrors);
			if (!Struct)
			{
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

	bool TDFluidReadFiniteNumbers(const TSharedPtr<FJsonValue>& Value, int32 Count, TArray<double>& OutNumbers)
	{
		if (!Value.IsValid() || Value->Type != EJson::Array || Value->AsArray().Num() != Count)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Element : Value->AsArray())
		{
			if (!Element.IsValid() || Element->Type != EJson::Number || !FMath::IsFinite(Element->AsNumber()))
			{
				return false;
			}
			OutNumbers.Add(Element->AsNumber());
		}
		return true;
	}

	template <typename TValue>
	void TDFluidReadLookMap(const TSharedPtr<FJsonObject>& Owner, const TCHAR* Key, const FString& Path, TMap<FName, TValue>& OutValues, bool (*ReadValue)(const TSharedPtr<FJsonValue>&, TValue&), const TCHAR* ExpectedText, TArray<FString>& OutErrors)
	{
		const TSharedPtr<FJsonObject> Values = TDFluidPresetTuningObjectField(Owner, Key, Path + TEXT("."), OutErrors);
		if (!Values.IsValid())
		{
			return;
		}
		for (const auto& Entry : Values->Values)
		{
			const FString Name(*Entry.Key);
			TValue Parsed{};
			if (Name.IsEmpty() || !ReadValue(Entry.Value, Parsed))
			{
				OutErrors.Add(FString::Printf(TEXT("%s.%s.%s: expected %s"), *Path, Key, *Name, ExpectedText));
				continue;
			}
			OutValues.Add(FName(*Name), Parsed);
		}
	}

	bool TDFluidReadLookFloat(const TSharedPtr<FJsonValue>& Value, float& OutValue)
	{
		if (!Value.IsValid() || Value->Type != EJson::Number || !FMath::IsFinite(Value->AsNumber()) || FMath::Abs(Value->AsNumber()) > static_cast<double>(MAX_flt))
		{
			return false;
		}
		OutValue = static_cast<float>(Value->AsNumber());
		return true;
	}

	bool TDFluidReadLookInt(const TSharedPtr<FJsonValue>& Value, int32& OutValue)
	{
		if (!Value.IsValid() || Value->Type != EJson::Number)
		{
			return false;
		}
		const double Number = Value->AsNumber();
		if (!FMath::IsFinite(Number) || FMath::FloorToDouble(Number) != Number || Number < static_cast<double>(MIN_int32) || Number > static_cast<double>(MAX_int32))
		{
			return false;
		}
		OutValue = static_cast<int32>(Number);
		return true;
	}

	bool TDFluidReadLookBool(const TSharedPtr<FJsonValue>& Value, bool& OutValue)
	{
		if (!Value.IsValid() || Value->Type != EJson::Boolean)
		{
			return false;
		}
		OutValue = Value->AsBool();
		return true;
	}

	bool TDFluidReadLookColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutValue)
	{
		TArray<double> Numbers;
		if (!TDFluidReadFiniteNumbers(Value, 4, Numbers))
		{
			return false;
		}
		OutValue = FLinearColor(static_cast<float>(Numbers[0]), static_cast<float>(Numbers[1]), static_cast<float>(Numbers[2]), static_cast<float>(Numbers[3]));
		return true;
	}

	bool TDFluidReadLookVector2(const TSharedPtr<FJsonValue>& Value, FVector2D& OutValue)
	{
		TArray<double> Numbers;
		if (!TDFluidReadFiniteNumbers(Value, 2, Numbers))
		{
			return false;
		}
		OutValue = FVector2D(Numbers[0], Numbers[1]);
		return true;
	}

	bool TDFluidReadLookVector3(const TSharedPtr<FJsonValue>& Value, FVector& OutValue)
	{
		TArray<double> Numbers;
		if (!TDFluidReadFiniteNumbers(Value, 3, Numbers))
		{
			return false;
		}
		OutValue = FVector(Numbers[0], Numbers[1], Numbers[2]);
		return true;
	}

	TSharedPtr<FJsonObject> TDFluidReadLookEntryObject(const FString& Key, const TSharedPtr<FJsonValue>& Value, const FString& SectionPath, const TArray<FString>& AllowedKeys, FString& OutPath, TArray<FString>& OutErrors)
	{
		OutPath = SectionPath + TEXT(".") + Key;
		if (!TDFluidIsGamePath(Key) || Key.Contains(TEXT(".")))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: key must be a /Game/Folder/Asset package path"), *OutPath));
			return nullptr;
		}
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: expected an object"), *OutPath));
			return nullptr;
		}
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		TDFluidPresetTuningRejectUnknownKeys(Object, AllowedKeys, OutPath + TEXT(": "), OutErrors);
		return Object;
	}

#if !UE_BUILD_SHIPPING
	FString TDFluidToFullPath(const FString& Path)
	{
		if (FPaths::IsRelative(Path))
		{
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
		}
		return FPaths::ConvertRelativePathToFull(Path);
	}
#endif

	bool TDFluidLoadTextFile(const FString& Path, const TCHAR* Description, FString& OutText, TArray<FString>& OutErrors)
	{
		if (FFileHelper::LoadFileToString(OutText, *Path))
		{
			return true;
		}
		OutErrors.Add(FString::Printf(TEXT("cannot read %s file %s"), Description, *Path));
		return false;
	}
}

void UTDFluidPresetTuningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResolveBasePaths();
	ReloadCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("TD.FluidFX.ReloadPresetTuning"),
		TEXT("TD.FluidFX.ReloadPresetTuning [TuningPath] [LookPath]: reloads the preset tuning and material look files. Paths must be under the project Saved folder and are refused in shipping builds; without paths the default files are read. A failed load keeps the previous table."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			ReloadPresetTuningFromPaths(Args.IsValidIndex(0) ? Args[0] : FString(), Args.IsValidIndex(1) ? Args[1] : FString());
		}),
		ECVF_Default);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UTDFluidPresetTuningSubsystem::HandleWorldCleanup);
#if WITH_EDITOR
	AutoReloadTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTDFluidPresetTuningSubsystem::PollBaseFiles), TDFluidAutoReloadPollSeconds);
#endif
	if (ReloadPresetTuning() == 0)
	{
		return;
	}
	ReportFirstLoadFailure();
}

void UTDFluidPresetTuningSubsystem::Deinitialize()
{
#if WITH_EDITOR
	FTSTicker::GetCoreTicker().RemoveTicker(AutoReloadTickerHandle);
#endif
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	if (ReloadCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ReloadCommand);
		ReloadCommand = nullptr;
	}
	Super::Deinitialize();
}

int32 UTDFluidPresetTuningSubsystem::ReloadPresetTuning()
{
	return LoadTables(BaseTuningPath, BaseLookPath);
}

int32 UTDFluidPresetTuningSubsystem::ReloadPresetTuningFromPaths(const FString& TuningPath, const FString& LookPath)
{
	TArray<FString> PathErrors;
	auto ResolveRequestedPath = [&PathErrors](const FString& RequestedPath, const FString& BasePath)
	{
		if (RequestedPath.IsEmpty())
		{
			return BasePath;
		}
#if UE_BUILD_SHIPPING
		PathErrors.Add(FString::Printf(TEXT("path %s refused: preset tuning path overrides are off in shipping builds"), *RequestedPath));
		return BasePath;
#else
		const FString FullPath = TDFluidToFullPath(RequestedPath);
		if (!IsAllowedOverridePath(FullPath))
		{
			PathErrors.Add(FString::Printf(TEXT("path %s refused: it must be under %s"), *FullPath, *FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())));
		}
		return FullPath;
#endif
	};
	const FString ResolvedTuningPath = ResolveRequestedPath(TuningPath, BaseTuningPath);
	const FString ResolvedLookPath = ResolveRequestedPath(LookPath, BaseLookPath);
	if (PathErrors.IsEmpty())
	{
		return LoadTables(ResolvedTuningPath, ResolvedLookPath);
	}
	LoadErrors = PathErrors;
	for (const FString& Error : LoadErrors)
	{
		UE_LOG(LogTDFluidPresetTuning, Error, TEXT("%s"), *Error);
	}
	return LoadErrors.Num();
}

TArray<FString> UTDFluidPresetTuningSubsystem::GetLoadErrors() const
{
	return LoadErrors;
}

bool UTDFluidPresetTuningSubsystem::IsUsingOverridePaths() const
{
	const bool bIsTuningOverridden = bHasTable && TuningSourcePath != BaseTuningPath;
	const bool bIsLookOverridden = bHasLookTable && LookSourcePath != BaseLookPath;
	return bIsTuningOverridden || bIsLookOverridden;
}

const TArray<FTDFluidPresetTuningRegisteredStruct>& UTDFluidPresetTuningSubsystem::GetRegisteredStructs()
{
	static const TArray<FTDFluidPresetTuningRegisteredStruct> RegisteredStructs = {
		{ FTDFluidSimulationConfig::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidSourceEmitterSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidFragmentTrailSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidVolumeRenderSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidFlameCardLayerSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidPlaneSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidDownstreamFeedSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidFragmentGlowSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidInputBrushSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidRuntimeOpsSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidEmberSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidFireLightSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidWaterLodGridSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidWaterNearRippleSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidVolumeDriverSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidOutputTargetSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidTerrainHeightSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidMassInputSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidSurfaceRenderSettings::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
		{ FTDFluidHeterogeneousVolumeRenderProfile::StaticStruct(), ETDFluidPresetTuningScope::Global }
	};
	return RegisteredStructs;
}

FString UTDFluidPresetTuningSubsystem::GetTuningFilePath()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("FluidFX/PresetData/TDFluidPresetTuning.json"));
}

FString UTDFluidPresetTuningSubsystem::GetMaterialLookFilePath()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("FluidFX/PresetData/TDFluidMaterialLook.json"));
}

bool UTDFluidPresetTuningSubsystem::IsAllowedOverridePath(const FString& FullPath)
{
	return FPaths::IsUnderDirectory(FullPath, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));
}

bool UTDFluidPresetTuningSubsystem::ParsePresetTuningText(const FString& Text, FTDFluidPresetTuningTable& OutTable, TArray<FString>& OutErrors)
{
	return ParsePresetTuningTextWithRegistry(Text, GetRegisteredStructs(), OutTable, OutErrors);
}

bool UTDFluidPresetTuningSubsystem::ParsePresetTuningTextWithRegistry(const FString& Text, const TArray<FTDFluidPresetTuningRegisteredStruct>& Registry, FTDFluidPresetTuningTable& OutTable, TArray<FString>& OutErrors)
{
	const int32 InitialErrorCount = OutErrors.Num();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("preset tuning text is not a JSON object"));
		return false;
	}
	TDFluidPresetTuningRejectUnknownKeys(Root, { TDFluidPresetTuningStructDefaultsKey, TDFluidPresetTuningGroupsKey, TDFluidPresetTuningKindsKey, TDFluidPresetTuningGlobalsKey }, TEXT("top level "), OutErrors);
	const TSharedPtr<FJsonObject> StructDefaults = TDFluidPresetTuningObjectField(Root, TDFluidPresetTuningStructDefaultsKey, FString(), OutErrors);
	const TSharedPtr<FJsonObject> Groups = TDFluidPresetTuningObjectField(Root, TDFluidPresetTuningGroupsKey, FString(), OutErrors);
	const TSharedPtr<FJsonObject> Kinds = TDFluidPresetTuningObjectField(Root, TDFluidPresetTuningKindsKey, FString(), OutErrors);
	const TSharedPtr<FJsonObject> Globals = TDFluidPresetTuningObjectField(Root, TDFluidPresetTuningGlobalsKey, FString(), OutErrors);
	if (!StructDefaults.IsValid() || !Groups.IsValid() || !Kinds.IsValid() || !Globals.IsValid())
	{
		return false;
	}

	FTDFluidPresetTuningTable ParsedTable;
	const TArray<ETDFluidPresetKind> AllKinds = TDFluidPresetTuningAllKinds();
	for (const auto& Entry : StructDefaults->Values)
	{
		const FString Key(*Entry.Key);
		TDFluidPresetTuningFindScopedStruct(Registry, Key, ETDFluidPresetTuningScope::PresetKind, FString(TDFluidPresetTuningStructDefaultsKey) + TEXT(".") + Key, OutErrors);
	}
	for (const auto& Entry : Globals->Values)
	{
		const FString Key(*Entry.Key);
		TDFluidPresetTuningFindScopedStruct(Registry, Key, ETDFluidPresetTuningScope::Global, FString(TDFluidPresetTuningGlobalsKey) + TEXT(".") + Key, OutErrors);
	}
	for (const FTDFluidPresetTuningRegisteredStruct& Registered : Registry)
	{
		const UScriptStruct* Struct = Registered.Struct;
		const bool bIsGlobal = Registered.Scope == ETDFluidPresetTuningScope::Global;
		const TCHAR* SectionKey = bIsGlobal ? TDFluidPresetTuningGlobalsKey : TDFluidPresetTuningStructDefaultsKey;
		const TSharedPtr<FJsonValue> DefaultsValue = TDFluidPresetTuningFindExactField(bIsGlobal ? Globals : StructDefaults, Struct->GetName());
		const TSharedPtr<FJsonObject> Defaults = DefaultsValue.IsValid() && DefaultsValue->Type == EJson::Object ? DefaultsValue->AsObject() : TSharedPtr<FJsonObject>();
		TDFluidPresetTuningValidateFields(Struct, Defaults, FString(SectionKey) + TEXT(".") + Struct->GetName(), true, OutErrors);
		if (!Defaults.IsValid())
		{
			continue;
		}
		if (bIsGlobal)
		{
			ParsedTable.GlobalLayers.Add(Struct, Defaults);
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
		TDFluidPresetTuningAddLayers(Registry, Group, Members, GroupPath, TDFluidPresetTuningGroupMembersKey, ParsedTable, OwnerByTarget, OutErrors);
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
		TDFluidPresetTuningAddLayers(Registry, Entry.Value->AsObject(), { Kind }, KindPath, FString(), ParsedTable, OwnerByTarget, OutErrors);
	}

	if (OutErrors.Num() != InitialErrorCount)
	{
		return false;
	}
	OutTable = MoveTemp(ParsedTable);
	return true;
}

bool UTDFluidPresetTuningSubsystem::ParseMaterialLookText(const FString& Text, FTDFluidMaterialLookTable& OutTable, TArray<FString>& OutErrors)
{
	const int32 InitialErrorCount = OutErrors.Num();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("material look text is not a JSON object"));
		return false;
	}
	TDFluidPresetTuningRejectUnknownKeys(Root, { TDFluidMaterialLookMaterialsKey, TDFluidMaterialLookNiagaraSystemsKey }, TEXT("material look top level "), OutErrors);
	const TSharedPtr<FJsonObject> Materials = TDFluidPresetTuningObjectField(Root, TDFluidMaterialLookMaterialsKey, FString(), OutErrors);
	const TSharedPtr<FJsonObject> NiagaraSystems = TDFluidPresetTuningObjectField(Root, TDFluidMaterialLookNiagaraSystemsKey, FString(), OutErrors);
	if (!Materials.IsValid() || !NiagaraSystems.IsValid())
	{
		return false;
	}

	FTDFluidMaterialLookTable ParsedTable;
	for (const auto& Entry : Materials->Values)
	{
		const FString Key(*Entry.Key);
		FString EntryPath;
		const TSharedPtr<FJsonObject> Object = TDFluidReadLookEntryObject(Key, Entry.Value, TDFluidMaterialLookMaterialsKey, { TEXT("Parent"), TEXT("Scalars"), TEXT("Vectors"), TEXT("Switches") }, EntryPath, OutErrors);
		if (!Object.IsValid())
		{
			continue;
		}
		FTDFluidMaterialLookEntry Look;
		const TSharedPtr<FJsonValue> ParentValue = TDFluidPresetTuningFindExactField(Object, TEXT("Parent"));
		const bool bHasParentPath = ParentValue.IsValid() && ParentValue->Type == EJson::String && TDFluidIsGamePath(ParentValue->AsString());
		if (!bHasParentPath)
		{
			OutErrors.Add(FString::Printf(TEXT("%s.Parent: expected a /Game/ path string"), *EntryPath));
		}
		Look.ParentPath = bHasParentPath ? ParentValue->AsString() : FString();
		TDFluidReadLookMap<float>(Object, TEXT("Scalars"), EntryPath, Look.Scalars, TDFluidReadLookFloat, TEXT("a finite float"), OutErrors);
		TDFluidReadLookMap<FLinearColor>(Object, TEXT("Vectors"), EntryPath, Look.Vectors, TDFluidReadLookColor, TEXT("an array of 4 finite numbers"), OutErrors);
		TDFluidReadLookMap<bool>(Object, TEXT("Switches"), EntryPath, Look.Switches, TDFluidReadLookBool, TEXT("true or false"), OutErrors);
		ParsedTable.MaterialsByPackage.Add(FName(*Key), MoveTemp(Look));
	}
	for (const auto& Entry : NiagaraSystems->Values)
	{
		const FString Key(*Entry.Key);
		FString EntryPath;
		const TSharedPtr<FJsonObject> Object = TDFluidReadLookEntryObject(Key, Entry.Value, TDFluidMaterialLookNiagaraSystemsKey, { TEXT("Floats"), TEXT("Ints"), TEXT("Bools"), TEXT("Vectors2"), TEXT("Vectors3"), TEXT("Colors") }, EntryPath, OutErrors);
		if (!Object.IsValid())
		{
			continue;
		}
		FTDFluidNiagaraLookEntry Look;
		TDFluidReadLookMap<float>(Object, TEXT("Floats"), EntryPath, Look.Floats, TDFluidReadLookFloat, TEXT("a finite float"), OutErrors);
		TDFluidReadLookMap<int32>(Object, TEXT("Ints"), EntryPath, Look.Ints, TDFluidReadLookInt, TEXT("an int32 integer"), OutErrors);
		TDFluidReadLookMap<bool>(Object, TEXT("Bools"), EntryPath, Look.Bools, TDFluidReadLookBool, TEXT("true or false"), OutErrors);
		TDFluidReadLookMap<FVector2D>(Object, TEXT("Vectors2"), EntryPath, Look.Vectors2, TDFluidReadLookVector2, TEXT("an array of 2 finite numbers"), OutErrors);
		TDFluidReadLookMap<FVector>(Object, TEXT("Vectors3"), EntryPath, Look.Vectors3, TDFluidReadLookVector3, TEXT("an array of 3 finite numbers"), OutErrors);
		TDFluidReadLookMap<FLinearColor>(Object, TEXT("Colors"), EntryPath, Look.Colors, TDFluidReadLookColor, TEXT("an array of 4 finite numbers"), OutErrors);
		ParsedTable.NiagaraSystemsByPackage.Add(FName(*Key), MoveTemp(Look));
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
	ApplyPresetTuningFromTable(Subsystem->Table, Kind, Struct, OutStruct);
}

void UTDFluidPresetTuningSubsystem::ApplyPresetTuningFromTable(const FTDFluidPresetTuningTable& SourceTable, ETDFluidPresetKind Kind, const UScriptStruct* Struct, void* OutStruct)
{
	if (const TSharedPtr<FJsonObject>* GlobalLayer = SourceTable.GlobalLayers.Find(Struct))
	{
		FJsonObjectConverter::JsonObjectToUStruct(GlobalLayer->ToSharedRef(), Struct, OutStruct);
		return;
	}
	const TMap<const UScriptStruct*, TArray<TSharedPtr<FJsonObject>>>* LayersByStruct = SourceTable.LayersByKind.Find(Kind);
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

const FTDFluidMaterialLookEntry* UTDFluidPresetTuningSubsystem::FindMaterialLook(FName PackageName)
{
	const UTDFluidPresetTuningSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	if (!Subsystem || !Subsystem->bHasLookTable)
	{
		return nullptr;
	}
	return Subsystem->LookTable.MaterialsByPackage.Find(PackageName);
}

const FTDFluidNiagaraLookEntry* UTDFluidPresetTuningSubsystem::FindNiagaraLook(FName PackageName)
{
	const UTDFluidPresetTuningSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	if (!Subsystem || !Subsystem->bHasLookTable)
	{
		return nullptr;
	}
	return Subsystem->LookTable.NiagaraSystemsByPackage.Find(PackageName);
}

int32 UTDFluidPresetTuningSubsystem::ApplyMaterialLook(UMaterialInstanceDynamic& Material)
{
	const UMaterialInterface* Parent = Material.Parent;
	if (!Parent)
	{
		return 0;
	}
	const FTDFluidMaterialLookEntry* Look = FindMaterialLook(Parent->GetPackage()->GetFName());
	if (!Look)
	{
		return 0;
	}
	for (const TPair<FName, float>& Scalar : Look->Scalars)
	{
		Material.SetScalarParameterValue(Scalar.Key, Scalar.Value);
	}
	for (const TPair<FName, FLinearColor>& Vector : Look->Vectors)
	{
		Material.SetVectorParameterValue(Vector.Key, Vector.Value);
	}
	return Look->Scalars.Num() + Look->Vectors.Num();
}

int32 UTDFluidPresetTuningSubsystem::CopyMatchingProperties(const UScriptStruct* SourceStruct, const void* Source, UObject& Target)
{
	int32 ChangedPropertyCount = 0;
	for (TFieldIterator<FProperty> It(SourceStruct); It; ++It)
	{
		const FProperty* SourceProperty = *It;
		const FProperty* TargetProperty = Target.GetClass()->FindPropertyByName(SourceProperty->GetFName());
		if (!TargetProperty || !TargetProperty->SameType(SourceProperty))
		{
			continue;
		}
		const void* SourceValue = SourceProperty->ContainerPtrToValuePtr<void>(Source);
		void* TargetValue = TargetProperty->ContainerPtrToValuePtr<void>(&Target);
		if (TargetProperty->Identical(TargetValue, SourceValue, PPF_None))
		{
			continue;
		}
		TargetProperty->CopyCompleteValue(TargetValue, SourceValue);
		++ChangedPropertyCount;
	}
	return ChangedPropertyCount;
}

int32 UTDFluidPresetTuningSubsystem::LoadTables(const FString& TuningPath, const FString& LookPath)
{
	TArray<FString> Errors;
	FString TuningText;
	FTDFluidPresetTuningTable ParsedTable;
	const bool bIsTuningLoaded = TDFluidLoadTextFile(TuningPath, TEXT("preset tuning"), TuningText, Errors) && ParsePresetTuningText(TuningText, ParsedTable, Errors);
	FString LookText;
	FTDFluidMaterialLookTable ParsedLookTable;
	const bool bIsLookLoaded = TDFluidLoadTextFile(LookPath, TEXT("material look"), LookText, Errors) && ParseMaterialLookText(LookText, ParsedLookTable, Errors);
	if (bIsTuningLoaded)
	{
		Table = MoveTemp(ParsedTable);
		bHasTable = true;
		TuningSourcePath = TuningPath;
	}
	if (bIsLookLoaded)
	{
		LookTable = MoveTemp(ParsedLookTable);
		bHasLookTable = true;
		LookSourcePath = LookPath;
	}
	const bool bHasNewTable = bIsTuningLoaded || bIsLookLoaded;
	if (bHasNewTable)
	{
		++TableGeneration;
	}
	RememberBaseFileTimestamps();
	LoadErrors = MoveTemp(Errors);
	for (const FString& Error : LoadErrors)
	{
		UE_LOG(LogTDFluidPresetTuning, Error, TEXT("%s"), *Error);
	}
	UE_LOG(LogTDFluidPresetTuning, Log, TEXT("Preset tuning %s (%s), material look %s (%s): %d errors, generation %d"), *TuningPath, bIsTuningLoaded ? TEXT("loaded") : TEXT("kept previous"), *LookPath, bIsLookLoaded ? TEXT("loaded") : TEXT("kept previous"), LoadErrors.Num(), TableGeneration);
	if (bHasNewTable)
	{
		OnPresetTuningReloaded.Broadcast(TableGeneration);
	}
	return LoadErrors.Num();
}

void UTDFluidPresetTuningSubsystem::ResolveBasePaths()
{
	BaseTuningPath = GetTuningFilePath();
	BaseLookPath = GetMaterialLookFilePath();
#if !UE_BUILD_SHIPPING
	FString CommandLineTuningPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("TDFluidTuningFile="), CommandLineTuningPath) && !CommandLineTuningPath.IsEmpty())
	{
		BaseTuningPath = TDFluidToFullPath(CommandLineTuningPath);
		UE_LOG(LogTDFluidPresetTuning, Display, TEXT("Preset tuning file from the command line: %s"), *BaseTuningPath);
	}
	FString CommandLineLookPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("TDFluidLookFile="), CommandLineLookPath) && !CommandLineLookPath.IsEmpty())
	{
		BaseLookPath = TDFluidToFullPath(CommandLineLookPath);
		UE_LOG(LogTDFluidPresetTuning, Display, TEXT("Material look file from the command line: %s"), *BaseLookPath);
	}
#endif
}

void UTDFluidPresetTuningSubsystem::RememberBaseFileTimestamps()
{
	PolledTuningTimestamp = IFileManager::Get().GetTimeStamp(*BaseTuningPath);
	PolledLookTimestamp = IFileManager::Get().GetTimeStamp(*BaseLookPath);
}

void UTDFluidPresetTuningSubsystem::ReportFirstLoadFailure() const
{
	const FString Summary = FString::Join(LoadErrors, TEXT(" | "));
	if (!GIsEditor)
	{
		UE_LOG(LogTDFluidPresetTuning, Fatal, TEXT("Preset tuning files %s and %s failed to load (%d errors): %s"), *BaseTuningPath, *BaseLookPath, LoadErrors.Num(), *Summary);
		return;
	}
	ensureMsgf(false, TEXT("Preset tuning files %s and %s failed to load (%d errors); builders return header defaults and materials keep their asset values until TD.FluidFX.ReloadPresetTuning succeeds: %s"), *BaseTuningPath, *BaseLookPath, LoadErrors.Num(), *Summary);
}

void UTDFluidPresetTuningSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!World || World->WorldType != EWorldType::PIE || !bSessionEnded || !IsUsingOverridePaths())
	{
		return;
	}
	UE_LOG(LogTDFluidPresetTuning, Log, TEXT("Play in editor ended; preset tuning returns to %s and %s"), *BaseTuningPath, *BaseLookPath);
	ReloadPresetTuning();
}

#if WITH_EDITOR
bool UTDFluidPresetTuningSubsystem::PollBaseFiles(float DeltaTime)
{
	const float IntervalSeconds = CVarTDFluidPresetTuningAutoReloadSeconds.GetValueOnGameThread();
	if (IntervalSeconds <= 0.0f || IsUsingOverridePaths())
	{
		AutoReloadElapsedSeconds = 0.0;
		return true;
	}
	AutoReloadElapsedSeconds += DeltaTime;
	if (AutoReloadElapsedSeconds < IntervalSeconds)
	{
		return true;
	}
	AutoReloadElapsedSeconds = 0.0;
	const bool bHasTuningFileChanged = IFileManager::Get().GetTimeStamp(*BaseTuningPath) != PolledTuningTimestamp;
	const bool bHasLookFileChanged = IFileManager::Get().GetTimeStamp(*BaseLookPath) != PolledLookTimestamp;
	if (!bHasTuningFileChanged && !bHasLookFileChanged)
	{
		return true;
	}
	UE_LOG(LogTDFluidPresetTuning, Log, TEXT("Preset tuning files changed on disk; reloading"));
	ReloadPresetTuning();
	return true;
}
#endif
