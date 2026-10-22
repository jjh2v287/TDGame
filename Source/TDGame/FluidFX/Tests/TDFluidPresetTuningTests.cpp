#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "JsonObjectConverter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/EnumProperty.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace TDFluidPresetTuningTestPrivate
{
	const TCHAR* const NaNPlaceholder = TEXT("TDFluidPresetTuningNaNPlaceholder");

	struct FTDFluidPresetTuningBuilder
	{
		const UScriptStruct* Struct = nullptr;
		TFunction<void(ETDFluidPresetKind, void*)> Build;
	};

	struct FTDFluidPresetTuningFixture
	{
		FString Name;
		TFunction<void(const TSharedPtr<FJsonObject>&)> Mutate;
	};

	TArray<FTDFluidPresetTuningBuilder> MakeBuilders()
	{
		TArray<FTDFluidPresetTuningBuilder> Builders;
		Builders.Add({ FTDFluidSimulationConfig::StaticStruct(), [](ETDFluidPresetKind Kind, void* Out) { *static_cast<FTDFluidSimulationConfig*>(Out) = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind); } });
		Builders.Add({ FTDFluidSourceEmitterSettings::StaticStruct(), [](ETDFluidPresetKind Kind, void* Out) { *static_cast<FTDFluidSourceEmitterSettings*>(Out) = UTDFluidPresetAsset::BuildSourceEmitterForPresetKind(Kind); } });
		Builders.Add({ FTDFluidFragmentTrailSettings::StaticStruct(), [](ETDFluidPresetKind Kind, void* Out) { *static_cast<FTDFluidFragmentTrailSettings*>(Out) = UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(Kind); } });
		Builders.Add({ FTDFluidVolumeRenderSettings::StaticStruct(), [](ETDFluidPresetKind Kind, void* Out) { *static_cast<FTDFluidVolumeRenderSettings*>(Out) = UTDFluidPresetAsset::BuildVolumeRenderForPresetKind(Kind); } });
		Builders.Add({ FTDFluidFlameCardLayerSettings::StaticStruct(), [](ETDFluidPresetKind Kind, void* Out) { *static_cast<FTDFluidFlameCardLayerSettings*>(Out) = UTDFluidPresetAsset::BuildFlameCardLayersForPresetKind(Kind); } });
		return Builders;
	}

	bool ReadTuningText(FString& OutText)
	{
		return FFileHelper::LoadFileToString(OutText, *UTDFluidPresetTuningSubsystem::GetTuningFilePath());
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

	int32 CountParseErrors(const FString& Text)
	{
		FTDFluidPresetTuningTable Table;
		TArray<FString> Errors;
		UTDFluidPresetTuningSubsystem::ParsePresetTuningText(Text, Table, Errors);
		return Errors.Num();
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
		return Fixtures;
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
		const FString QuotedPlaceholder = FString::Printf(TEXT("\"%s\""), NaNPlaceholder);
		const FString FixtureText = SerializeObject(Root).Replace(*QuotedPlaceholder, TEXT("NaN"), ESearchCase::CaseSensitive);
		TestTrue(*FString::Printf(TEXT("Rejects %s"), *Fixture.Name), CountParseErrors(FixtureText) >= 1);
	}
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
	const UEnum* KindEnum = StaticEnum<ETDFluidPresetKind>();
	for (int32 KindIndex = 0; KindIndex < KindEnum->NumEnums() - 1; ++KindIndex)
	{
		const ETDFluidPresetKind Kind = static_cast<ETDFluidPresetKind>(KindEnum->GetValueByIndex(KindIndex));
		const FString KindName = KindEnum->GetNameStringByIndex(KindIndex);
		for (const FTDFluidPresetTuningBuilder& Builder : MakeBuilders())
		{
			FStructOnScope Built(Builder.Struct);
			Builder.Build(Kind, Built.GetStructMemory());
			const FString StructName = Builder.Struct->GetName();
			const TMap<FString, TSharedPtr<FJsonValue>> Expected = CollectExpectedFields(Root, KindName, StructName);
			TestEqual(*FString::Printf(TEXT("%s %s has every field in the tuning file"), *KindName, *StructName), Expected.Num(), CountProperties(Builder.Struct));
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Expected)
			{
				const FProperty* Property = Builder.Struct->FindPropertyByName(FName(*Field.Key));
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
	for (const FTDFluidPresetTuningBuilder& Builder : MakeBuilders())
	{
		const FString StructName = Builder.Struct->GetName();
		const TSharedPtr<FJsonObject> Defaults = FindStructDefaults(Root, StructName);
		if (!TestTrue(*FString::Printf(TEXT("StructDefaults has %s"), *StructName), Defaults.IsValid()))
		{
			continue;
		}
		FStructOnScope HeaderDefaults(Builder.Struct);
		for (TFieldIterator<FProperty> It(Builder.Struct); It; ++It)
		{
			FString Actual;
			const bool bMatches = MatchesJsonValue(*It, It->ContainerPtrToValuePtr<void>(HeaderDefaults.GetStructMemory()), Defaults->TryGetField(It->GetName()), Actual);
			TestTrue(*FString::Printf(TEXT("StructDefaults.%s.%s equals the header default %s (a header change does not move presets; update StructDefaults on purpose)"), *StructName, *It->GetName(), *Actual), bMatches);
		}
	}
	return true;
}

#endif
