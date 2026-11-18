#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDNoGlobalRandomTest, "TDGame.MonsterAI.NoGlobalRandom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FString StripCommentsFromLine(const FString& Line, bool& bInBlockComment)
	{
		FString Result;
		TCHAR QuoteCharacter = 0;
		int32 Index = 0;
		while (Index < Line.Len())
		{
			if (bInBlockComment)
			{
				const int32 EndIndex = Line.Find(TEXT("*/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				if (EndIndex == INDEX_NONE)
				{
					return Result;
				}
				bInBlockComment = false;
				Index = EndIndex + 2;
				continue;
			}
			if (QuoteCharacter != 0)
			{
				if (Line[Index] == TEXT('\\') && Index + 1 < Line.Len())
				{
					Result.AppendChar(Line[Index]);
					Result.AppendChar(Line[Index + 1]);
					Index += 2;
					continue;
				}
				if (Line[Index] == QuoteCharacter)
				{
					QuoteCharacter = 0;
				}
				Result.AppendChar(Line[Index]);
				++Index;
				continue;
			}
			if (Line[Index] == TEXT('"') || Line[Index] == TEXT('\''))
			{
				QuoteCharacter = Line[Index];
				Result.AppendChar(Line[Index]);
				++Index;
				continue;
			}
			if (Line.Mid(Index, 2) == TEXT("//"))
			{
				return Result;
			}
			if (Line.Mid(Index, 2) == TEXT("/*"))
			{
				bInBlockComment = true;
				Index += 2;
				continue;
			}
			Result.AppendChar(Line[Index]);
			++Index;
		}
		return Result;
	}

	bool ContainsForbiddenCall(const TArray<FString>& Lines, const FString& Call)
	{
		bool bInBlockComment = false;
		for (const FString& Line : Lines)
		{
			if (StripCommentsFromLine(Line, bInBlockComment).Contains(Call))
			{
				return true;
			}
		}
		return false;
	}
}

bool FTDNoGlobalRandomTest::RunTest(const FString& Parameters)
{
	const TArray<FString> ScannedFolders = { TEXT("Combat"), TEXT("MonsterAI"), TEXT("CombatSim") };
	const TArray<FString> ForbiddenCalls = { TEXT("FMath::FRand"), TEXT("FMath::Rand"), TEXT("FMath::SRand"), TEXT("FMath::VRand") };
	const TArray<FString> StringHidesBlockCommentSource = { TEXT("const TCHAR* Text = TEXT(\"a/*b\");"), TEXT("const float Value = FMath::FRand();") };
	const TArray<FString> StringHidesLineCommentSource = { TEXT("Log(TEXT(\"http://x\")); FMath::FRand();") };
	const TArray<FString> EscapedQuoteSource = { TEXT("Log(TEXT(\"a\\\"/*\")); FMath::FRand();") };
	const TArray<FString> RealCommentSource = { TEXT("/* FMath::FRand();"), TEXT("FMath::FRand(); */ int32 Value = 0;"), TEXT("int32 Other = 0; // FMath::FRand();") };
	TestTrue(TEXT("A block comment symbol inside a string does not hide the next line"), ContainsForbiddenCall(StringHidesBlockCommentSource, TEXT("FMath::FRand")));
	TestTrue(TEXT("A line comment symbol inside a string does not hide the call"), ContainsForbiddenCall(StringHidesLineCommentSource, TEXT("FMath::FRand")));
	TestTrue(TEXT("An escaped quote keeps the string open"), ContainsForbiddenCall(EscapedQuoteSource, TEXT("FMath::FRand")));
	TestFalse(TEXT("A call inside real comments is ignored"), ContainsForbiddenCall(RealCommentSource, TEXT("FMath::FRand")));
	TArray<FString> Files;
	for (const FString& Folder : ScannedFolders)
	{
		const FString Directory = FPaths::Combine(FPaths::GameSourceDir(), TEXT("TDGame"), Folder);
		if (!IFileManager::Get().DirectoryExists(*Directory))
		{
			continue;
		}
		IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.cpp"), true, false, false);
		IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.h"), true, false, false);
	}
	TestTrue(TEXT("Combat and MonsterAI sources were found"), Files.Num() > 5);
	for (const FString& File : Files)
	{
		if (FPaths::GetCleanFilename(File) == TEXT("TDNoGlobalRandomTests.cpp"))
		{
			continue;
		}
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *File))
		{
			AddError(FString::Printf(TEXT("%s could not be read"), *File));
			continue;
		}
		bool bInBlockComment = false;
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString Code = StripCommentsFromLine(Lines[LineIndex], bInBlockComment);
			for (const FString& Call : ForbiddenCalls)
			{
				if (Code.Contains(Call))
				{
					AddError(FString::Printf(TEXT("%s:%d uses global random %s"), *FPaths::GetCleanFilename(File), LineIndex + 1, *Call));
				}
			}
		}
	}
	return true;
}
