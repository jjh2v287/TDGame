#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Foundation/Recording/TDGameplayRecorderSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TDGameplayRecorderTests
{
	struct FTDRecorderTestDirectory
	{
		FTDRecorderTestDirectory()
			: Path(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests"), TEXT("RecorderTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FTDRecorderTestDirectory()
		{
			IFileManager::Get().DeleteDirectory(*Path, false, true);
		}

		FTDRecorderTestDirectory(const FTDRecorderTestDirectory&) = delete;
		FTDRecorderTestDirectory& operator=(const FTDRecorderTestDirectory&) = delete;

		FString Path;
	};

	struct FTDRecorderEnabledScope
	{
		explicit FTDRecorderEnabledScope(int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(TEXT("TD.Recorder.Enabled")))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetInt();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDRecorderEnabledScope()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		FTDRecorderEnabledScope(const FTDRecorderEnabledScope&) = delete;
		FTDRecorderEnabledScope& operator=(const FTDRecorderEnabledScope&) = delete;

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 1;
	};

	TSharedPtr<FJsonObject> ParseJsonLine(const FString& Line)
	{
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!FJsonSerializer::Deserialize(Reader, Parsed))
		{
			return nullptr;
		}
		return Parsed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDGameplayRecorderWritesHeaderAndLinesTest, "TDGame.Foundation.Recorder.WritesHeaderAndLines", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDGameplayRecorderWritesHeaderAndLinesTest::RunTest(const FString& Parameters)
{
	TDGameplayRecorderTests::FTDRecorderTestDirectory Directory;
	TDGameplayRecorderTests::FTDRecorderEnabledScope EnabledScope(1);
	UGameInstance* OwnerGameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UTDGameplayRecorderSubsystem* Recorder = NewObject<UTDGameplayRecorderSubsystem>(OwnerGameInstance);
	Recorder->SetDirectoryOverride(Directory.Path);

	TestTrue(TEXT("The session opens"), Recorder->BeginSession(TEXT("live"), TEXT("TestMap"), 42));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
		Fields->SetNumberField(TEXT("amount"), 10.0 + Index);
		Recorder->RecordEvent(TEXT("damage"), 1.5 + Index, Fields);
	}
	TestFalse(TEXT("Lines stay buffered until the flush"), IFileManager::Get().FileExists(*Recorder->GetSessionFilePath()));
	Recorder->EndSession();

	FString FileText;
	TestTrue(TEXT("The session file exists"), FFileHelper::LoadFileToString(FileText, *Recorder->GetSessionFilePath()));
	TArray<FString> Lines;
	FileText.ParseIntoArrayLines(Lines);
	TestEqual(TEXT("Header plus three events"), Lines.Num(), 4);

	for (const FString& Line : Lines)
	{
		TestTrue(TEXT("Every line is valid JSON"), TDGameplayRecorderTests::ParseJsonLine(Line).IsValid());
	}
	if (Lines.Num() != 4)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Header = TDGameplayRecorderTests::ParseJsonLine(Lines[0]);
	TestEqual(TEXT("The first line is the session header"), Header->GetStringField(TEXT("ev")), FString(TEXT("session")));
	TestEqual(TEXT("The header carries the origin"), Header->GetStringField(TEXT("origin")), FString(TEXT("live")));
	TestEqual(TEXT("The header carries the map"), Header->GetStringField(TEXT("map")), FString(TEXT("TestMap")));
	TestEqual(TEXT("The header carries the seed"), Header->GetIntegerField(TEXT("seed")), 42);

	const TSharedPtr<FJsonObject> SecondEvent = TDGameplayRecorderTests::ParseJsonLine(Lines[2]);
	TestEqual(TEXT("The event kind is stored"), SecondEvent->GetStringField(TEXT("ev")), FString(TEXT("damage")));
	TestEqual(TEXT("The event time is stored in seconds"), SecondEvent->GetNumberField(TEXT("t")), 2.5);
	TestEqual(TEXT("The event fields are merged"), SecondEvent->GetNumberField(TEXT("amount")), 11.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDGameplayRecorderDisabledWritesNothingTest, "TDGame.Foundation.Recorder.DisabledWritesNothing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDGameplayRecorderDisabledWritesNothingTest::RunTest(const FString& Parameters)
{
	TDGameplayRecorderTests::FTDRecorderTestDirectory Directory;
	TDGameplayRecorderTests::FTDRecorderEnabledScope EnabledScope(0);
	UGameInstance* OwnerGameInstance = NewObject<UGameInstance>(GetTransientPackage());
	UTDGameplayRecorderSubsystem* Recorder = NewObject<UTDGameplayRecorderSubsystem>(OwnerGameInstance);
	Recorder->SetDirectoryOverride(Directory.Path);

	TestFalse(TEXT("The session does not open while disabled"), Recorder->BeginSession(TEXT("live"), TEXT("TestMap")));
	Recorder->RecordEvent(TEXT("damage"), 1.0, MakeShared<FJsonObject>());
	Recorder->EndSession();

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory.Path, TEXT("*.jsonl")), true, false);
	TestEqual(TEXT("No file is written while disabled"), Files.Num(), 0);
	TestEqual(TEXT("No line is recorded while disabled"), Recorder->GetRecordedLineCount(), 0);
	return true;
}

#endif
