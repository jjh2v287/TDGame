#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TDGameplayRecorderSubsystem.generated.h"

class FJsonObject;

UCLASS()
class TDGAME_API UTDGameplayRecorderSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	bool BeginSession(const FString& Origin, const FString& MapName, int64 Seed = 0);
	void SetDirectoryOverride(const FString& Directory) { DirectoryOverride = Directory; }
	void RecordEvent(const FString& EventKind, double TimeSeconds, const TSharedPtr<FJsonObject>& Fields);
	void Flush();
	void EndSession();

	bool IsSessionOpen() const { return bIsSessionOpen; }
	const FString& GetSessionFilePath() const { return SessionFilePath; }
	int32 GetRecordedLineCount() const { return RecordedLineCount; }

	static bool IsRecorderEnabled();

private:
	FString MakeUniqueSessionFilePath(const FString& Directory, const FString& MapName) const;
	void AppendLine(const TSharedRef<FJsonObject>& Line);

	TArray<FString> PendingLines;
	FString SessionFilePath;
	FString DirectoryOverride;
	int32 RecordedLineCount = 0;
	bool bIsSessionOpen = false;
};
