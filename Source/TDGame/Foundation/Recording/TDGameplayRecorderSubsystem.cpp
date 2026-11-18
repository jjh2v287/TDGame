#include "Foundation/Recording/TDGameplayRecorderSubsystem.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

namespace TDGameplayRecorder
{
	constexpr int32 FlushLineCount = 64;

	TAutoConsoleVariable<int32> CVarRecorderEnabled(
		TEXT("TD.Recorder.Enabled"),
		1,
		TEXT("1 records gameplay events to Saved/Telemetry, 0 disables the recorder."),
		ECVF_Default);

	FString SerializeLine(const TSharedRef<FJsonObject>& Line)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Line, Writer);
		Writer->Close();
		return Text;
	}
}

bool UTDGameplayRecorderSubsystem::IsRecorderEnabled()
{
	return TDGameplayRecorder::CVarRecorderEnabled.GetValueOnAnyThread() != 0;
}

void UTDGameplayRecorderSubsystem::Deinitialize()
{
	EndSession();
	Super::Deinitialize();
}

bool UTDGameplayRecorderSubsystem::BeginSession(const FString& Origin, const FString& MapName, int64 Seed)
{
	EndSession();
	if (!IsRecorderEnabled())
	{
		return false;
	}

	const FString Directory = DirectoryOverride.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry")) : DirectoryOverride;
	IFileManager::Get().MakeDirectory(*Directory, true);
	SessionFilePath = MakeUniqueSessionFilePath(Directory, MapName);
	RecordedLineCount = 0;
	bIsSessionOpen = true;

	const TSharedRef<FJsonObject> Header = MakeShared<FJsonObject>();
	Header->SetStringField(TEXT("ev"), TEXT("session"));
	Header->SetStringField(TEXT("origin"), Origin);
	Header->SetStringField(TEXT("map"), MapName);
	Header->SetStringField(TEXT("start"), FDateTime::Now().ToIso8601());
	Header->SetNumberField(TEXT("seed"), static_cast<double>(Seed));
	AppendLine(Header);
	return true;
}

void UTDGameplayRecorderSubsystem::RecordEvent(const FString& EventKind, double TimeSeconds, const TSharedPtr<FJsonObject>& Fields)
{
	if (!bIsSessionOpen || !IsRecorderEnabled())
	{
		return;
	}

	const TSharedRef<FJsonObject> Line = MakeShared<FJsonObject>();
	Line->SetNumberField(TEXT("t"), FMath::RoundToDouble(TimeSeconds * 1000.0) / 1000.0);
	Line->SetStringField(TEXT("ev"), EventKind);
	if (Fields.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Fields->Values)
		{
			Line->SetField(Field.Key, Field.Value);
		}
	}
	AppendLine(Line);
}

void UTDGameplayRecorderSubsystem::Flush()
{
	if (PendingLines.IsEmpty() || SessionFilePath.IsEmpty())
	{
		return;
	}

	const FString Block = FString::Join(PendingLines, TEXT("\n")) + TEXT("\n");
	PendingLines.Reset();
	FFileHelper::SaveStringToFile(Block, *SessionFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

void UTDGameplayRecorderSubsystem::EndSession()
{
	if (!bIsSessionOpen)
	{
		return;
	}

	Flush();
	bIsSessionOpen = false;
}

FString UTDGameplayRecorderSubsystem::MakeUniqueSessionFilePath(const FString& Directory, const FString& MapName) const
{
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	const FString SafeMapName = FPaths::MakeValidFileName(MapName.IsEmpty() ? TEXT("Unknown") : MapName);
	FString Candidate = FPaths::Combine(Directory, FString::Printf(TEXT("%s_%s.jsonl"), *Stamp, *SafeMapName));
	for (int32 Suffix = 2; IFileManager::Get().FileExists(*Candidate); ++Suffix)
	{
		Candidate = FPaths::Combine(Directory, FString::Printf(TEXT("%s_%s_%d.jsonl"), *Stamp, *SafeMapName, Suffix));
	}
	return Candidate;
}

void UTDGameplayRecorderSubsystem::AppendLine(const TSharedRef<FJsonObject>& Line)
{
	PendingLines.Add(TDGameplayRecorder::SerializeLine(Line));
	++RecordedLineCount;
	if (PendingLines.Num() >= TDGameplayRecorder::FlushLineCount)
	{
		Flush();
	}
}
