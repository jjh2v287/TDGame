#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "FluidFX/TDFluidEmberComponent.h"
#include "FluidFX/TDFluidFireLightComponent.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidPlaneTypes.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/Tests/TDFluidPresetTuningTestTypes.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/EnumProperty.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace TDFluidPresetTuningTestPrivate
{
	const TCHAR* const NaNPlaceholder = TEXT("TDFluidPresetTuningNaNPlaceholder");

	using FTDFluidPresetTuningBuild = TFunction<void(ETDFluidPresetKind, void*)>;

	struct FTDFluidPresetTuningFixture
	{
		FString Name;
		TFunction<void(const TSharedPtr<FJsonObject>&)> Mutate;
	};

	template <typename TSettings>
	FTDFluidPresetTuningBuild MakeBuild(TSettings (*Build)(ETDFluidPresetKind))
	{
		return [Build](ETDFluidPresetKind Kind, void* Out) { *static_cast<TSettings*>(Out) = Build(Kind); };
	}

	TMap<const UScriptStruct*, FTDFluidPresetTuningBuild> MakeBuilders()
	{
		TMap<const UScriptStruct*, FTDFluidPresetTuningBuild> Builders;
		Builders.Add(FTDFluidSimulationConfig::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildConfigForPresetKind));
		Builders.Add(FTDFluidSourceEmitterSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildSourceEmitterForPresetKind));
		Builders.Add(FTDFluidFragmentTrailSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildFragmentTrailForPresetKind));
		Builders.Add(FTDFluidVolumeRenderSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildVolumeRenderForPresetKind));
		Builders.Add(FTDFluidFlameCardLayerSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildFlameCardLayersForPresetKind));
		Builders.Add(FTDFluidPlaneSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildPlaneSettingsForPresetKind));
		Builders.Add(FTDFluidDownstreamFeedSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildDownstreamFeedForPresetKind));
		Builders.Add(FTDFluidFragmentGlowSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildFragmentGlowForPresetKind));
		Builders.Add(FTDFluidInputBrushSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildInputBrushForPresetKind));
		Builders.Add(FTDFluidRuntimeOpsSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildRuntimeOpsForPresetKind));
		Builders.Add(FTDFluidEmberSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildEmberSettingsForPresetKind));
		Builders.Add(FTDFluidFireLightSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildFireLightSettingsForPresetKind));
		Builders.Add(FTDFluidWaterLodGridSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildWaterLodGridSettingsForPresetKind));
		Builders.Add(FTDFluidWaterNearRippleSettings::StaticStruct(), MakeBuild(&UTDFluidPresetAsset::BuildWaterNearRippleSettingsForPresetKind));
		return Builders;
	}

	TArray<const UScriptStruct*> GetPresetKindStructs()
	{
		TArray<const UScriptStruct*> Structs;
		for (const FTDFluidPresetTuningRegisteredStruct& Registered : UTDFluidPresetTuningSubsystem::GetRegisteredStructs())
		{
			if (Registered.Scope == ETDFluidPresetTuningScope::PresetKind)
			{
				Structs.Add(Registered.Struct);
			}
		}
		return Structs;
	}

	FString GetActiveTuningFilePath()
	{
		const UTDFluidPresetTuningSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
		if (!Subsystem || Subsystem->GetTuningSourcePath().IsEmpty())
		{
			return UTDFluidPresetTuningSubsystem::GetTuningFilePath();
		}
		return Subsystem->GetTuningSourcePath();
	}

	bool ReadTuningText(FString& OutText)
	{
		return FFileHelper::LoadFileToString(OutText, *GetActiveTuningFilePath());
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
		{
			return TSharedPtr<FJsonObject>();
		}
		return Root;
	}

	FString SerializeObject(const TSharedPtr<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Text;
	}

	TSharedPtr<FJsonObject> FindObject(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		if (!Object.IsValid() || !Object->TryGetObjectField(Key, Found))
		{
			return TSharedPtr<FJsonObject>();
		}
		return *Found;
	}

	TSharedPtr<FJsonObject> FindOrAddObject(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		const TSharedPtr<FJsonObject> Existing = FindObject(Object, Key);
		if (Existing.IsValid())
		{
			return Existing;
		}
		const TSharedPtr<FJsonObject> Added = MakeShared<FJsonObject>();
		Object->SetObjectField(Key, Added);
		return Added;
	}

	void RewriteKeyCase(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		TSharedPtr<FJsonValue> Value = Object->TryGetField(Key);
		if (!Value.IsValid())
		{
			Value = MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
		}
		Object->RemoveField(Key);
		Object->SetField(Key, Value);
	}

	TSharedPtr<FJsonObject> FindStructDefaults(const TSharedPtr<FJsonObject>& Root, const FString& StructName)
	{
		return FindObject(FindObject(Root, TEXT("StructDefaults")), StructName);
	}

	TArray<TSharedPtr<FJsonValue>> MakeNumberArray(std::initializer_list<double> Numbers)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const double Number : Numbers)
		{
			Values.Add(MakeShared<FJsonValueNumber>(Number));
		}
		return Values;
	}

	int32 CountParseErrors(const FString& Text)
	{
		FTDFluidPresetTuningTable Table;
		TArray<FString> Errors;
		UTDFluidPresetTuningSubsystem::ParsePresetTuningText(Text, Table, Errors);
		return Errors.Num();
	}

	FString RestoreNaN(const TSharedPtr<FJsonObject>& Root)
	{
		const FString QuotedPlaceholder = FString::Printf(TEXT("\"%s\""), NaNPlaceholder);
		return SerializeObject(Root).Replace(*QuotedPlaceholder, TEXT("NaN"), ESearchCase::CaseSensitive);
	}

	TMap<FString, TSharedPtr<FJsonValue>> CollectExpectedFields(const TSharedPtr<FJsonObject>& Root, const FString& KindName, const FString& StructName)
	{
		TMap<FString, TSharedPtr<FJsonValue>> Expected;
		auto MergeFields = [&Expected](const TSharedPtr<FJsonObject>& Fields)
		{
			if (!Fields.IsValid())
			{
				return;
			}
			for (const auto& Field : Fields->Values)
			{
				Expected.Add(FString(*Field.Key), Field.Value);
			}
		};
		MergeFields(FindStructDefaults(Root, StructName));
		const TSharedPtr<FJsonObject> Groups = FindObject(Root, TEXT("Groups"));
		if (Groups.IsValid())
		{
			for (const auto& Group : Groups->Values)
			{
				const TSharedPtr<FJsonObject> GroupObject = Group.Value->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
				if (!GroupObject.IsValid() || !GroupObject->TryGetArrayField(TEXT("Kinds"), Members))
				{
					continue;
				}
				const bool bHasKind = Members->ContainsByPredicate([&KindName](const TSharedPtr<FJsonValue>& Member)
				{
					return Member.IsValid() && Member->AsString().Equals(KindName, ESearchCase::CaseSensitive);
				});
				if (bHasKind)
				{
					MergeFields(FindObject(GroupObject, StructName));
				}
			}
		}
		MergeFields(FindObject(FindObject(FindObject(Root, TEXT("Kinds")), KindName), StructName));
		return Expected;
	}

	bool MatchesJsonValue(const FProperty* Property, const void* ValuePtr, const TSharedPtr<FJsonValue>& Expected, FString& OutActual)
	{
		if (!Expected.IsValid())
		{
			return false;
		}
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			const bool bValue = BoolProperty->GetPropertyValue(ValuePtr);
			OutActual = bValue ? TEXT("true") : TEXT("false");
			return Expected->Type == EJson::Boolean && Expected->AsBool() == bValue;
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const int64 Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			OutActual = EnumProperty->GetEnum()->GetNameStringByValue(Value);
			return Expected->Type == EJson::String && OutActual.Equals(Expected->AsString(), ESearchCase::CaseSensitive);
		}
		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			const float Value = FloatProperty->GetPropertyValue(ValuePtr);
			OutActual = FString::SanitizeFloat(Value);
			return Expected->Type == EJson::Number && static_cast<float>(Expected->AsNumber()) == Value;
		}
		if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
		{
			const double Value = DoubleProperty->GetPropertyValue(ValuePtr);
			OutActual = FString::SanitizeFloat(Value);
			return Expected->Type == EJson::Number && Expected->AsNumber() == Value;
		}
		if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			const int32 Value = IntProperty->GetPropertyValue(ValuePtr);
			OutActual = FString::FromInt(Value);
			return Expected->Type == EJson::Number && Expected->AsNumber() == static_cast<double>(Value);
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			StructProperty->ExportTextItem_Direct(OutActual, ValuePtr, nullptr, nullptr, PPF_None);
			if (Expected->Type != EJson::Object)
			{
				return false;
			}
			FStructOnScope ExpectedStruct(StructProperty->Struct);
			const bool bConverted = FJsonObjectConverter::JsonObjectToUStruct(Expected->AsObject().ToSharedRef(), StructProperty->Struct, ExpectedStruct.GetStructMemory());
			return bConverted && StructProperty->Identical(ValuePtr, ExpectedStruct.GetStructMemory(), PPF_None);
		}
		OutActual = TEXT("unsupported property type");
		return false;
	}

	int32 CountProperties(const UScriptStruct* Struct)
	{
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	TArray<FTDFluidPresetTuningFixture> MakeBadInputFixtures()
	{
		const FString Trail = TEXT("TDFluidFragmentTrailSettings");
		TArray<FTDFluidPresetTuningFixture> Fixtures;
		Fixtures.Add({ TEXT("misspelled field"), [Trail](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, Trail)->SetNumberField(TEXT("BurstCoreRadiusCmm"), 1.0); } });
		Fixtures.Add({ TEXT("misspelled kind Explosin"), [](const TSharedPtr<FJsonObject>& Root) { FindOrAddObject(FindObject(Root, TEXT("Kinds")), TEXT("Explosin")); } });
		Fixtures.Add({ TEXT("lower case kind explosion"), [](const TSharedPtr<FJsonObject>& Root) { RewriteKeyCase(FindObject(Root, TEXT("Kinds")), TEXT("explosion")); } });
		Fixtures.Add({ TEXT("lower case field burstcoreradiuscm"), [Trail](const TSharedPtr<FJsonObject>& Root) { RewriteKeyCase(FindStructDefaults(Root, Trail), TEXT("burstcoreradiuscm")); } });
		Fixtures.Add({ TEXT("unknown struct TDFluidTrail"), [](const TSharedPtr<FJsonObject>& Root) { FindOrAddObject(FindOrAddObject(FindObject(Root, TEXT("Kinds")), TEXT("Torch")), TEXT("TDFluidTrail")); } });
		Fixtures.Add({ TEXT("string in an integer field"), [Trail](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, Trail)->SetStringField(TEXT("BurstRingStampCount"), TEXT("x")); } });
		Fixtures.Add({ TEXT("fraction in an integer field"), [Trail](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, Trail)->SetNumberField(TEXT("BurstRingStampCount"), 1.5); } });
		Fixtures.Add({ TEXT("number in a bool field"), [Trail](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, Trail)->SetNumberField(TEXT("bSustainsBurstHeat"), 1.0); } });
		Fixtures.Add({ TEXT("null in a float field"), [Trail](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, Trail)->SetField(TEXT("BurstSustainSeconds"), MakeShared<FJsonValueNull>()); } });
		Fixtures.Add({ TEXT("NaN in a float field"), [Trail](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, Trail)->SetStringField(TEXT("BurstSustainSeconds"), NaNPlaceholder); } });
		Fixtures.Add({ TEXT("color without alpha"), [](const TSharedPtr<FJsonObject>& Root) { FindObject(FindStructDefaults(Root, TEXT("TDFluidVolumeRenderSettings")), TEXT("HeatEmissionColor"))->RemoveField(TEXT("A")); } });
		Fixtures.Add({ TEXT("field set in two places"), [](const TSharedPtr<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Group = FindOrAddObject(FindObject(Root, TEXT("Groups")), TEXT("TDFluidPresetTuningTestGroup"));
			TArray<TSharedPtr<FJsonValue>> Members;
			Members.Add(MakeShared<FJsonValueString>(TEXT("Torch")));
			Group->SetArrayField(TEXT("Kinds"), Members);
			FindOrAddObject(Group, TEXT("TDFluidFlameCardLayerSettings"))->SetNumberField(TEXT("CardScaleStep"), 1.0);
			FindOrAddObject(FindOrAddObject(FindObject(Root, TEXT("Kinds")), TEXT("Torch")), TEXT("TDFluidFlameCardLayerSettings"))->SetNumberField(TEXT("CardScaleStep"), 1.0);
		} });
		Fixtures.Add({ TEXT("struct default field missing"), [](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, TEXT("TDFluidFlameCardLayerSettings"))->RemoveField(TEXT("CardIntensityScale")); } });
		Fixtures.Add({ TEXT("Globals key missing"), [](const TSharedPtr<FJsonObject>& Root) { Root->RemoveField(TEXT("Globals")); } });
		Fixtures.Add({ TEXT("Globals is not an object"), [](const TSharedPtr<FJsonObject>& Root) { Root->SetNumberField(TEXT("Globals"), 1.0); } });
		Fixtures.Add({ TEXT("preset kind struct under Globals"), [](const TSharedPtr<FJsonObject>& Root) { FindOrAddObject(FindObject(Root, TEXT("Globals")), TEXT("TDFluidPlaneSettings")); } });
		Fixtures.Add({ TEXT("unknown struct under Globals"), [](const TSharedPtr<FJsonObject>& Root) { FindOrAddObject(FindObject(Root, TEXT("Globals")), TEXT("TDFluidUnknownGlobals")); } });
		Fixtures.Add({ TEXT("misspelled field in a new struct"), [](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, TEXT("TDFluidInputBrushSettings"))->SetNumberField(TEXT("PivotDensityStrengthh"), 1.0); } });
		Fixtures.Add({ TEXT("mirror struct field missing"), [](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, TEXT("TDFluidEmberSettings"))->RemoveField(TEXT("MaxEmbers")); } });
		Fixtures.Add({ TEXT("unknown pressure solver name"), [](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, TEXT("TDFluidSimulationConfig"))->SetStringField(TEXT("GpuPressureSolverMode"), TEXT("Multigrid")); } });
		Fixtures.Add({ TEXT("fraction in the multigrid cycle count"), [](const TSharedPtr<FJsonObject>& Root) { FindStructDefaults(Root, TEXT("TDFluidSimulationConfig"))->SetNumberField(TEXT("GpuMultigridCycles"), 1.5); } });
		Fixtures.Add({ TEXT("plane orientation in lower case"), [](const TSharedPtr<FJsonObject>& Root) { FindOrAddObject(FindOrAddObject(FindObject(Root, TEXT("Kinds")), TEXT("Water")), TEXT("TDFluidPlaneSettings"))->SetStringField(TEXT("Orientation"), TEXT("camerafacing")); } });
		return Fixtures;
	}

	const FString FormatsStructName = TEXT("TDFluidPresetTuningTestFormats");
	const FString GlobalsStructName = TEXT("TDFluidPresetTuningTestGlobals");

	TArray<FTDFluidPresetTuningRegisteredStruct> MakeTestRegistry()
	{
		return {
			{ FTDFluidPresetTuningTestFormats::StaticStruct(), ETDFluidPresetTuningScope::PresetKind },
			{ FTDFluidPresetTuningTestGlobals::StaticStruct(), ETDFluidPresetTuningScope::Global }
		};
	}

	FString MakeTestFormatsText()
	{
		return TEXT(R"json({
	"StructDefaults": { "TDFluidPresetTuningTestFormats": { "Scale": 1.0, "Weights": [1.0, 2.0, 3.0], "Points": [{ "X": 1.0, "Y": 2.0 }], "Label": "Base", "AssetReference": "", "AssetPath": "/Game/FluidFX/Presets/DA_TDFluid_Sea.DA_TDFluid_Sea" } },
	"Groups": {},
	"Kinds": { "Torch": { "TDFluidPresetTuningTestFormats": { "Weights": [4.0], "Label": "TorchLabel" } } },
	"Globals": { "TDFluidPresetTuningTestGlobals": { "GlobalScale": 2.5, "GlobalCount": 3 } }
})json");
	}

	int32 CountTestRegistryErrors(const FString& Text)
	{
		FTDFluidPresetTuningTable Table;
		TArray<FString> Errors;
		UTDFluidPresetTuningSubsystem::ParsePresetTuningTextWithRegistry(Text, MakeTestRegistry(), Table, Errors);
		return Errors.Num();
	}

	TArray<FTDFluidPresetTuningFixture> MakeExtendedFormatFixtures()
	{
		auto Formats = [](const TSharedPtr<FJsonObject>& Root) { return FindStructDefaults(Root, FormatsStructName); };
		TArray<FTDFluidPresetTuningFixture> Fixtures;
		Fixtures.Add({ TEXT("number in an array field"), [Formats](const TSharedPtr<FJsonObject>& Root) { Formats(Root)->SetNumberField(TEXT("Weights"), 1.0); } });
		Fixtures.Add({ TEXT("string inside a float array"), [Formats](const TSharedPtr<FJsonObject>& Root)
		{
			TArray<TSharedPtr<FJsonValue>> Values = MakeNumberArray({ 1.0 });
			Values.Add(MakeShared<FJsonValueString>(TEXT("x")));
			Formats(Root)->SetArrayField(TEXT("Weights"), Values);
		} });
		Fixtures.Add({ TEXT("null inside a float array"), [Formats](const TSharedPtr<FJsonObject>& Root)
		{
			TArray<TSharedPtr<FJsonValue>> Values = MakeNumberArray({ 1.0 });
			Values.Add(MakeShared<FJsonValueNull>());
			Formats(Root)->SetArrayField(TEXT("Weights"), Values);
		} });
		Fixtures.Add({ TEXT("array element struct without Y"), [Formats](const TSharedPtr<FJsonObject>& Root)
		{
			const TSharedPtr<FJsonObject> Point = MakeShared<FJsonObject>();
			Point->SetNumberField(TEXT("X"), 1.0);
			TArray<TSharedPtr<FJsonValue>> Values;
			Values.Add(MakeShared<FJsonValueObject>(Point));
			Formats(Root)->SetArrayField(TEXT("Points"), Values);
		} });
		Fixtures.Add({ TEXT("number in a name field"), [Formats](const TSharedPtr<FJsonObject>& Root) { Formats(Root)->SetNumberField(TEXT("Label"), 3.0); } });
		Fixtures.Add({ TEXT("soft pointer outside /Game"), [Formats](const TSharedPtr<FJsonObject>& Root) { Formats(Root)->SetStringField(TEXT("AssetReference"), TEXT("C:/Windows/Asset.Asset")); } });
		Fixtures.Add({ TEXT("soft pointer under /Engine"), [Formats](const TSharedPtr<FJsonObject>& Root) { Formats(Root)->SetStringField(TEXT("AssetReference"), TEXT("/Engine/BasicShapes/Cube.Cube")); } });
		Fixtures.Add({ TEXT("soft path with a space"), [Formats](const TSharedPtr<FJsonObject>& Root) { Formats(Root)->SetStringField(TEXT("AssetPath"), TEXT("/Game/Bad Folder/Asset.Asset")); } });
		Fixtures.Add({ TEXT("soft path with two object names"), [Formats](const TSharedPtr<FJsonObject>& Root) { Formats(Root)->SetStringField(TEXT("AssetPath"), TEXT("/Game/Folder/Asset.Asset.Sub")); } });
		Fixtures.Add({ TEXT("soft path ending in a slash"), [Formats](const TSharedPtr<FJsonObject>& Root) { Formats(Root)->SetStringField(TEXT("AssetPath"), TEXT("/Game/Folder/")); } });
		Fixtures.Add({ TEXT("soft path as an object"), [Formats](const TSharedPtr<FJsonObject>& Root) { FindOrAddObject(Formats(Root), TEXT("AssetPath")); } });
		Fixtures.Add({ TEXT("global struct under Kinds"), [](const TSharedPtr<FJsonObject>& Root) { FindOrAddObject(FindOrAddObject(FindObject(Root, TEXT("Kinds")), TEXT("Torch")), GlobalsStructName)->SetNumberField(TEXT("GlobalScale"), 1.0); } });
		Fixtures.Add({ TEXT("global struct under StructDefaults"), [](const TSharedPtr<FJsonObject>& Root) { FindOrAddObject(FindObject(Root, TEXT("StructDefaults")), GlobalsStructName); } });
		Fixtures.Add({ TEXT("global field missing"), [](const TSharedPtr<FJsonObject>& Root) { FindObject(FindObject(Root, TEXT("Globals")), GlobalsStructName)->RemoveField(TEXT("GlobalCount")); } });
		Fixtures.Add({ TEXT("global struct missing"), [](const TSharedPtr<FJsonObject>& Root) { FindObject(Root, TEXT("Globals"))->RemoveField(GlobalsStructName); } });
		Fixtures.Add({ TEXT("preset kind struct under Globals"), [](const TSharedPtr<FJsonObject>& Root) { FindOrAddObject(FindObject(Root, TEXT("Globals")), FormatsStructName); } });
		return Fixtures;
	}

	FString MakeMaterialLookText()
	{
		return TEXT(R"json({
	"Materials": { "/Game/FluidFX/Materials/MI_TDFluidLookTest": { "Parent": "/Game/FluidFX/Materials/M_TDFluidLookTest", "Scalars": { "Glow": 2.0 }, "Vectors": { "Tint": [1.0, 0.5, 0.25, 1.0] }, "Switches": { "UseFoam": true } } },
	"NiagaraSystems": { "/Game/FluidFX/Niagara/NS_TDFluidLookTest": { "Floats": { "User.Rate": 10.0 }, "Ints": { "User.Count": 3 }, "Bools": { "User.IsOn": false }, "Vectors2": { "User.Offset": [0.0, 1.0] }, "Vectors3": { "User.Axis": [0.0, 0.0, 1.0] }, "Colors": { "User.Color": [1.0, 0.9, 0.8, 1.0] } } }
})json");
	}

	int32 CountMaterialLookErrors(const FString& Text)
	{
		FTDFluidMaterialLookTable Table;
		TArray<FString> Errors;
		UTDFluidPresetTuningSubsystem::ParseMaterialLookText(Text, Table, Errors);
		return Errors.Num();
	}

	TArray<FTDFluidPresetTuningFixture> MakeMaterialLookFixtures()
	{
		const FString MaterialKey = TEXT("/Game/FluidFX/Materials/MI_TDFluidLookTest");
		const FString SystemKey = TEXT("/Game/FluidFX/Niagara/NS_TDFluidLookTest");
		auto Material = [MaterialKey](const TSharedPtr<FJsonObject>& Root) { return FindObject(FindObject(Root, TEXT("Materials")), MaterialKey); };
		auto System = [SystemKey](const TSharedPtr<FJsonObject>& Root) { return FindObject(FindObject(Root, TEXT("NiagaraSystems")), SystemKey); };
		TArray<FTDFluidPresetTuningFixture> Fixtures;
		Fixtures.Add({ TEXT("unknown top level key"), [](const TSharedPtr<FJsonObject>& Root) { Root->SetObjectField(TEXT("DumpInfo"), MakeShared<FJsonObject>()); } });
		Fixtures.Add({ TEXT("NiagaraSystems missing"), [](const TSharedPtr<FJsonObject>& Root) { Root->RemoveField(TEXT("NiagaraSystems")); } });
		Fixtures.Add({ TEXT("material key outside /Game"), [](const TSharedPtr<FJsonObject>& Root) { FindObject(Root, TEXT("Materials"))->SetObjectField(TEXT("/Engine/MI_Test"), MakeShared<FJsonObject>()); } });
		Fixtures.Add({ TEXT("material key with an object name"), [](const TSharedPtr<FJsonObject>& Root) { FindObject(Root, TEXT("Materials"))->SetObjectField(TEXT("/Game/FluidFX/MI_Test.MI_Test"), MakeShared<FJsonObject>()); } });
		Fixtures.Add({ TEXT("parent missing"), [Material](const TSharedPtr<FJsonObject>& Root) { Material(Root)->RemoveField(TEXT("Parent")); } });
		Fixtures.Add({ TEXT("switches missing"), [Material](const TSharedPtr<FJsonObject>& Root) { Material(Root)->RemoveField(TEXT("Switches")); } });
		Fixtures.Add({ TEXT("texture section is not allowed"), [Material](const TSharedPtr<FJsonObject>& Root) { Material(Root)->SetObjectField(TEXT("Textures"), MakeShared<FJsonObject>()); } });
		Fixtures.Add({ TEXT("string scalar"), [Material](const TSharedPtr<FJsonObject>& Root) { FindObject(Material(Root), TEXT("Scalars"))->SetStringField(TEXT("Glow"), TEXT("x")); } });
		Fixtures.Add({ TEXT("vector with three numbers"), [Material](const TSharedPtr<FJsonObject>& Root) { FindObject(Material(Root), TEXT("Vectors"))->SetArrayField(TEXT("Tint"), MakeNumberArray({ 1.0, 0.5, 0.25 })); } });
		Fixtures.Add({ TEXT("number switch"), [Material](const TSharedPtr<FJsonObject>& Root) { FindObject(Material(Root), TEXT("Switches"))->SetNumberField(TEXT("UseFoam"), 1.0); } });
		Fixtures.Add({ TEXT("fraction in a Niagara int"), [System](const TSharedPtr<FJsonObject>& Root) { FindObject(System(Root), TEXT("Ints"))->SetNumberField(TEXT("User.Count"), 1.5); } });
		Fixtures.Add({ TEXT("Niagara vector2 with three numbers"), [System](const TSharedPtr<FJsonObject>& Root) { FindObject(System(Root), TEXT("Vectors2"))->SetArrayField(TEXT("User.Offset"), MakeNumberArray({ 0.0, 1.0, 2.0 })); } });
		Fixtures.Add({ TEXT("Niagara colors missing"), [System](const TSharedPtr<FJsonObject>& Root) { System(Root)->RemoveField(TEXT("Colors")); } });
		Fixtures.Add({ TEXT("NaN Niagara float"), [System](const TSharedPtr<FJsonObject>& Root) { FindObject(System(Root), TEXT("Floats"))->SetStringField(TEXT("User.Rate"), NaNPlaceholder); } });
		return Fixtures;
	}

	bool HasEmptyArrayDefaults(const UScriptStruct* Struct, FString& OutFieldName)
	{
		FStructOnScope Defaults(Struct);
		for (TFieldIterator<FArrayProperty> It(Struct); It; ++It)
		{
			FScriptArrayHelper Helper(*It, It->ContainerPtrToValuePtr<void>(Defaults.GetStructMemory()));
			if (Helper.Num() != 0)
			{
				OutFieldName = It->GetName();
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetTuningLoadsWithoutErrorsTest,
	"TDGame.FluidFX.PresetData.LoadsWithoutErrors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetTuningLoadsWithoutErrorsTest::RunTest(const FString& Parameters)
{
	UTDFluidPresetTuningSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Preset tuning subsystem exists"), Subsystem))
	{
		return false;
	}
	TestEqual(TEXT("Reloading the preset tuning file reports no errors"), Subsystem->ReloadPresetTuning(), 0);
	TestEqual(TEXT("No preset tuning load errors remain"), Subsystem->GetLoadErrors().Num(), 0);
	const FTDFluidFragmentTrailSettings BuiltTrail = UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion);
	const FTDFluidFragmentTrailSettings HeaderTrail;
	TestFalse(TEXT("Explosion trail values come from the tuning file"), FTDFluidFragmentTrailSettings::StaticStruct()->CompareScriptStruct(&BuiltTrail, &HeaderTrail, PPF_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetTuningRejectsBadInputTest,
	"TDGame.FluidFX.PresetData.RejectsBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetTuningRejectsBadInputTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPresetTuningTestPrivate;
	FString Text;
	if (!TestTrue(TEXT("Read the preset tuning file"), ReadTuningText(Text)))
	{
		return false;
	}
	if (!TestTrue(TEXT("Preset tuning file parses as JSON"), ParseObject(Text).IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Unchanged round trip has no errors"), CountParseErrors(SerializeObject(ParseObject(Text))), 0);
	for (const FTDFluidPresetTuningFixture& Fixture : MakeBadInputFixtures())
	{
		const TSharedPtr<FJsonObject> Root = ParseObject(Text);
		Fixture.Mutate(Root);
		TestTrue(*FString::Printf(TEXT("Rejects %s"), *Fixture.Name), CountParseErrors(RestoreNaN(Root)) >= 1);
	}

	const FString FormatsText = MakeTestFormatsText();
	TestEqual(TEXT("Test format text has no errors"), CountTestRegistryErrors(FormatsText), 0);
	for (const FTDFluidPresetTuningFixture& Fixture : MakeExtendedFormatFixtures())
	{
		const TSharedPtr<FJsonObject> Root = ParseObject(FormatsText);
		Fixture.Mutate(Root);
		TestTrue(*FString::Printf(TEXT("Rejects %s"), *Fixture.Name), CountTestRegistryErrors(RestoreNaN(Root)) >= 1);
	}

	const FString LookText = MakeMaterialLookText();
	TestEqual(TEXT("Test material look text has no errors"), CountMaterialLookErrors(LookText), 0);
	for (const FTDFluidPresetTuningFixture& Fixture : MakeMaterialLookFixtures())
	{
		const TSharedPtr<FJsonObject> Root = ParseObject(LookText);
		Fixture.Mutate(Root);
		TestTrue(*FString::Printf(TEXT("Rejects material look with %s"), *Fixture.Name), CountMaterialLookErrors(RestoreNaN(Root)) >= 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetTuningExtendedFormatsTest,
	"TDGame.FluidFX.PresetData.ExtendedFormatsLayerByReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetTuningExtendedFormatsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPresetTuningTestPrivate;
	FTDFluidPresetTuningTable Table;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("Test format text parses"), UTDFluidPresetTuningSubsystem::ParsePresetTuningTextWithRegistry(MakeTestFormatsText(), MakeTestRegistry(), Table, Errors)))
	{
		AddError(FString::Join(Errors, TEXT(" | ")));
		return false;
	}
	FTDFluidPresetTuningTestFormats Torch;
	Torch.Weights = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };
	UTDFluidPresetTuningSubsystem::ApplyPresetTuningFromTable(Table, ETDFluidPresetKind::Torch, FTDFluidPresetTuningTestFormats::StaticStruct(), &Torch);
	TestEqual(TEXT("A kind layer replaces the whole array"), Torch.Weights.Num(), 1);
	TestTrue(TEXT("The replaced array holds the kind value"), Torch.Weights.Num() == 1 && Torch.Weights[0] == 4.0f);
	TestEqual(TEXT("The kind layer sets the name"), Torch.Label, FName(TEXT("TorchLabel")));
	TestEqual(TEXT("Struct arrays come from the defaults"), Torch.Points.Num(), 1);
	TestTrue(TEXT("Struct array element values"), Torch.Points.Num() == 1 && Torch.Points[0] == FVector2D(1.0, 2.0));
	TestTrue(TEXT("An empty soft pointer string is a null reference"), Torch.AssetReference.IsNull());
	TestEqual(TEXT("Soft object path is read as text"), Torch.AssetPath.ToString(), FString(TEXT("/Game/FluidFX/Presets/DA_TDFluid_Sea.DA_TDFluid_Sea")));

	FTDFluidPresetTuningTestFormats Water;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuningFromTable(Table, ETDFluidPresetKind::Water, FTDFluidPresetTuningTestFormats::StaticStruct(), &Water);
	TestEqual(TEXT("Kinds without a layer keep the default array"), Water.Weights.Num(), 3);
	TestEqual(TEXT("Kinds without a layer keep the default name"), Water.Label, FName(TEXT("Base")));

	FTDFluidPresetTuningTestGlobals Globals;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuningFromTable(Table, ETDFluidPresetKind::Water, FTDFluidPresetTuningTestGlobals::StaticStruct(), &Globals);
	TestEqual(TEXT("Global scale comes from Globals"), Globals.GlobalScale, 2.5f);
	TestEqual(TEXT("Global count comes from Globals"), Globals.GlobalCount, 3);

	FTDFluidMaterialLookTable LookTable;
	TArray<FString> LookErrors;
	if (!TestTrue(TEXT("Test material look text parses"), UTDFluidPresetTuningSubsystem::ParseMaterialLookText(MakeMaterialLookText(), LookTable, LookErrors)))
	{
		AddError(FString::Join(LookErrors, TEXT(" | ")));
		return false;
	}
	const FTDFluidMaterialLookEntry* MaterialLook = LookTable.MaterialsByPackage.Find(FName(TEXT("/Game/FluidFX/Materials/MI_TDFluidLookTest")));
	if (!TestNotNull(TEXT("Material look is keyed by package path"), MaterialLook))
	{
		return false;
	}
	TestEqual(TEXT("Material parent path"), MaterialLook->ParentPath, FString(TEXT("/Game/FluidFX/Materials/M_TDFluidLookTest")));
	TestTrue(TEXT("Material scalar value"), MaterialLook->Scalars.FindRef(FName(TEXT("Glow"))) == 2.0f);
	TestTrue(TEXT("Material vector value"), MaterialLook->Vectors.FindRef(FName(TEXT("Tint"))) == FLinearColor(1.0f, 0.5f, 0.25f, 1.0f));
	TestTrue(TEXT("Material switch value"), MaterialLook->Switches.FindRef(FName(TEXT("UseFoam"))));
	const FTDFluidNiagaraLookEntry* SystemLook = LookTable.NiagaraSystemsByPackage.Find(FName(TEXT("/Game/FluidFX/Niagara/NS_TDFluidLookTest")));
	if (!TestNotNull(TEXT("Niagara look is keyed by package path"), SystemLook))
	{
		return false;
	}
	TestEqual(TEXT("Niagara int value"), SystemLook->Ints.FindRef(FName(TEXT("User.Count"))), 3);
	TestTrue(TEXT("Niagara vector3 value"), SystemLook->Vectors3.FindRef(FName(TEXT("User.Axis"))) == FVector(0.0, 0.0, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetTuningBuildersMatchTest,
	"TDGame.FluidFX.PresetData.BuildersMatchTuningFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetTuningBuildersMatchTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPresetTuningTestPrivate;
	FString Text;
	if (!TestTrue(TEXT("Read the preset tuning file"), ReadTuningText(Text)))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Root = ParseObject(Text);
	if (!TestTrue(TEXT("Preset tuning file parses as JSON"), Root.IsValid()))
	{
		return false;
	}
	const TMap<const UScriptStruct*, FTDFluidPresetTuningBuild> Builders = MakeBuilders();
	const UEnum* KindEnum = StaticEnum<ETDFluidPresetKind>();
	for (const UScriptStruct* Struct : GetPresetKindStructs())
	{
		const FString StructName = Struct->GetName();
		const FTDFluidPresetTuningBuild* Build = Builders.Find(Struct);
		if (!TestNotNull(*FString::Printf(TEXT("Registered struct %s has a Build*ForPresetKind builder"), *StructName), Build))
		{
			continue;
		}
		for (int32 KindIndex = 0; KindIndex < KindEnum->NumEnums() - 1; ++KindIndex)
		{
			const ETDFluidPresetKind Kind = static_cast<ETDFluidPresetKind>(KindEnum->GetValueByIndex(KindIndex));
			const FString KindName = KindEnum->GetNameStringByIndex(KindIndex);
			FStructOnScope Built(Struct);
			(*Build)(Kind, Built.GetStructMemory());
			const TMap<FString, TSharedPtr<FJsonValue>> Expected = CollectExpectedFields(Root, KindName, StructName);
			TestEqual(*FString::Printf(TEXT("%s %s has every field in the tuning file"), *KindName, *StructName), Expected.Num(), CountProperties(Struct));
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Expected)
			{
				const FProperty* Property = Struct->FindPropertyByName(FName(*Field.Key));
				if (!TestNotNull(*FString::Printf(TEXT("%s has field %s"), *StructName, *Field.Key), Property))
				{
					continue;
				}
				FString Actual;
				const bool bMatches = MatchesJsonValue(Property, Property->ContainerPtrToValuePtr<void>(Built.GetStructMemory()), Field.Value, Actual);
				TestTrue(*FString::Printf(TEXT("%s %s.%s matches the tuning file (built %s)"), *KindName, *StructName, *Field.Key, *Actual), bMatches);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetTuningStructDefaultsMatchHeaderTest,
	"TDGame.FluidFX.PresetData.StructDefaultsMatchHeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetTuningStructDefaultsMatchHeaderTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPresetTuningTestPrivate;
	FString Text;
	if (!TestTrue(TEXT("Read the preset tuning file"), ReadTuningText(Text)))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Root = ParseObject(Text);
	if (!TestTrue(TEXT("Preset tuning file parses as JSON"), Root.IsValid()))
	{
		return false;
	}
	for (const FTDFluidPresetTuningRegisteredStruct& Registered : UTDFluidPresetTuningSubsystem::GetRegisteredStructs())
	{
		const UScriptStruct* Struct = Registered.Struct;
		const FString StructName = Struct->GetName();
		FString NonEmptyArrayName;
		const bool bHasEmptyArrayDefaults = HasEmptyArrayDefaults(Struct, NonEmptyArrayName);
		TestTrue(*FString::Printf(TEXT("%s array fields default to empty arrays (%s)"), *StructName, *NonEmptyArrayName), bHasEmptyArrayDefaults);
		const FString SectionKey = Registered.Scope == ETDFluidPresetTuningScope::Global ? TEXT("Globals") : TEXT("StructDefaults");
		const TSharedPtr<FJsonObject> Defaults = FindObject(FindObject(Root, SectionKey), StructName);
		if (!TestTrue(*FString::Printf(TEXT("%s has %s"), *SectionKey, *StructName), Defaults.IsValid()))
		{
			continue;
		}
		FStructOnScope HeaderDefaults(Struct);
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FString Actual;
			const bool bMatches = MatchesJsonValue(*It, It->ContainerPtrToValuePtr<void>(HeaderDefaults.GetStructMemory()), Defaults->TryGetField(It->GetName()), Actual);
			TestTrue(*FString::Printf(TEXT("%s.%s.%s equals the header default %s (a header change does not move presets; update the tuning file on purpose)"), *SectionKey, *StructName, *It->GetName(), *Actual), bMatches);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetTuningMirrorStructsTest,
	"TDGame.FluidFX.PresetData.MirrorStructsMatchComponentDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetTuningMirrorStructsTest::RunTest(const FString& Parameters)
{
	const TArray<TPair<const UScriptStruct*, const UClass*>> Mirrors = {
		{ FTDFluidEmberSettings::StaticStruct(), UTDFluidEmberComponent::StaticClass() },
		{ FTDFluidFireLightSettings::StaticStruct(), UTDFluidFireLightComponent::StaticClass() }
	};
	for (const TPair<const UScriptStruct*, const UClass*>& Mirror : Mirrors)
	{
		const UScriptStruct* Struct = Mirror.Key;
		const UClass* ComponentClass = Mirror.Value;
		const UObject* ComponentDefaults = ComponentClass->GetDefaultObject();
		FStructOnScope StructDefaults(Struct);
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* ComponentProperty = ComponentClass->FindPropertyByName(It->GetFName());
			if (!TestNotNull(*FString::Printf(TEXT("%s has the mirrored property %s"), *ComponentClass->GetName(), *It->GetName()), ComponentProperty))
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s.%s has the component type"), *Struct->GetName(), *It->GetName()), ComponentProperty->SameType(*It));
			const bool bIsIdentical = ComponentProperty->Identical(ComponentProperty->ContainerPtrToValuePtr<void>(ComponentDefaults), It->ContainerPtrToValuePtr<void>(StructDefaults.GetStructMemory()), PPF_None);
			TestTrue(*FString::Printf(TEXT("%s.%s default equals the component default"), *Struct->GetName(), *It->GetName()), bIsIdentical);
		}
		for (TFieldIterator<FProperty> It(ComponentClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (It->IsA<FObjectPropertyBase>())
			{
				continue;
			}
			TestNotNull(*FString::Printf(TEXT("%s mirrors the component value property %s"), *Struct->GetName(), *It->GetName()), Struct->FindPropertyByName(It->GetFName()));
		}
	}

	UTDFluidEmberComponent* Ember = NewObject<UTDFluidEmberComponent>(GetTransientPackage());
	FTDFluidEmberSettings EmberSettings;
	EmberSettings.MaxEmbers = 64;
	EmberSettings.BaseScale = 0.01f;
	TestEqual(TEXT("Copying mirror values reports the changed properties"), UTDFluidPresetTuningSubsystem::CopyMatchingProperties(FTDFluidEmberSettings::StaticStruct(), &EmberSettings, *Ember), 2);
	TestEqual(TEXT("Copied ember count"), Ember->MaxEmbers, 64);
	TestEqual(TEXT("Copied ember scale"), Ember->BaseScale, 0.01f);
	TestEqual(TEXT("Copying the same values again changes nothing"), UTDFluidPresetTuningSubsystem::CopyMatchingProperties(FTDFluidEmberSettings::StaticStruct(), &EmberSettings, *Ember), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetTuningReloadFromPathsTest,
	"TDGame.FluidFX.PresetData.ReloadFromPathsUsesSavedFiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetTuningReloadFromPathsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPresetTuningTestPrivate;
	UTDFluidPresetTuningSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Preset tuning subsystem exists"), Subsystem))
	{
		return false;
	}
	const FString CopyPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/PresetTuningTests/TDFluidPresetTuningCopy.json")));
	ON_SCOPE_EXIT
	{
		Subsystem->ReloadPresetTuning();
		IFileManager::Get().Delete(*CopyPath);
	};
	Subsystem->ReloadPresetTuning();
	const FString BaseSourcePath = Subsystem->GetTuningSourcePath();
	const int32 BaseGeneration = Subsystem->GetTableGeneration();
	TestFalse(TEXT("The base files are not an override"), Subsystem->IsUsingOverridePaths());

	const FString OutsidePath = FPaths::ConvertRelativePathToFull(UTDFluidPresetTuningSubsystem::GetTuningFilePath());
	AddExpectedError(TEXT("refused: it must be under"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestTrue(TEXT("A path outside Saved is refused"), Subsystem->ReloadPresetTuningFromPaths(OutsidePath, FString()) >= 1);
	TestEqual(TEXT("A refused path keeps the table generation"), Subsystem->GetTableGeneration(), BaseGeneration);
	TestEqual(TEXT("A refused path keeps the source"), Subsystem->GetTuningSourcePath(), BaseSourcePath);

	FString Text;
	if (!TestTrue(TEXT("Read the active tuning file"), ReadTuningText(Text)) || !TestTrue(TEXT("Write the tuning copy under Saved"), FFileHelper::SaveStringToFile(Text, *CopyPath)))
	{
		return false;
	}
	int32 BroadcastGeneration = INDEX_NONE;
	const FDelegateHandle BroadcastHandle = Subsystem->OnPresetTuningReloaded.AddLambda([&BroadcastGeneration](int32 Generation)
	{
		BroadcastGeneration = Generation;
	});
	Subsystem->ReloadPresetTuningFromPaths(CopyPath, FString());
	Subsystem->OnPresetTuningReloaded.Remove(BroadcastHandle);
	TestEqual(TEXT("The tuning copy is the source"), Subsystem->GetTuningSourcePath(), CopyPath);
	TestTrue(TEXT("The tuning copy is an override"), Subsystem->IsUsingOverridePaths());
	TestTrue(TEXT("A successful load raises the generation"), Subsystem->GetTableGeneration() > BaseGeneration);
	TestEqual(TEXT("The reload broadcast carries the new generation"), BroadcastGeneration, Subsystem->GetTableGeneration());

	Subsystem->ReloadPresetTuning();
	TestEqual(TEXT("Reloading without paths returns to the base file"), Subsystem->GetTuningSourcePath(), BaseSourcePath);
	TestFalse(TEXT("Reloading without paths ends the override"), Subsystem->IsUsingOverridePaths());
	return true;
}

#endif
