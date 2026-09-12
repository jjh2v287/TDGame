#include "TDWorldGenCommandlet.h"

#include "Dungeon/TDDungeonDefinitions.h"
#include "Dungeon/TDDungeonGeneration.h"
#include "Misc/Parse.h"
#include "TDWorldGenEditorLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDWorldGenCommandlet, Log, All);

UTDWorldGenCommandlet::UTDWorldGenCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

UTDDungeonTheme* UTDWorldGenCommandlet::ResolveTheme(const FString& ThemeArgument)
{
	if (!ThemeArgument.IsEmpty() && ThemeArgument.StartsWith(TEXT("/")))
	{
		if (UTDDungeonTheme* Loaded = LoadObject<UTDDungeonTheme>(nullptr, *ThemeArgument))
		{
			return Loaded;
		}
		UE_LOG(LogTDWorldGenCommandlet, Warning, TEXT("Theme asset not found: %s, using transient Crypt placeholder"), *ThemeArgument);
	}
	UTDDungeonTheme* Theme = NewObject<UTDDungeonTheme>(GetTransientPackage(), NAME_None, RF_Transient);
	Theme->ThemeId = ThemeArgument.IsEmpty() || ThemeArgument.StartsWith(TEXT("/")) ? FName(TEXT("Crypt")) : FName(*ThemeArgument);
	Theme->FillCryptPlaceholderModules();
	return Theme;
}

UTDDungeonFlowTemplate* UTDWorldGenCommandlet::ResolveFlowTemplate(const FString& FlowArgument, FString& OutFlowName)
{
	if (!FlowArgument.IsEmpty() && FlowArgument.StartsWith(TEXT("/")))
	{
		if (UTDDungeonFlowTemplate* Loaded = LoadObject<UTDDungeonFlowTemplate>(nullptr, *FlowArgument))
		{
			OutFlowName = TDDungeon::FlowName(Loaded->Kind);
			return Loaded;
		}
		UE_LOG(LogTDWorldGenCommandlet, Warning, TEXT("Flow asset not found: %s, using transient Linear template"), *FlowArgument);
	}
	ETDDungeonFlowKind Kind = ETDDungeonFlowKind::Linear;
	if (FlowArgument.Equals(TEXT("Branch"), ESearchCase::IgnoreCase)) Kind = ETDDungeonFlowKind::Branch;
	else if (FlowArgument.Equals(TEXT("Loop"), ESearchCase::IgnoreCase)) Kind = ETDDungeonFlowKind::Loop;
	else if (FlowArgument.Equals(TEXT("Hub"), ESearchCase::IgnoreCase)) Kind = ETDDungeonFlowKind::Hub;
	else if (FlowArgument.Equals(TEXT("KeyLock"), ESearchCase::IgnoreCase)) Kind = ETDDungeonFlowKind::KeyLock;
	UTDDungeonFlowTemplate* Template = NewObject<UTDDungeonFlowTemplate>(GetTransientPackage(), NAME_None, RF_Transient);
	Template->ApplyKindDefaults(Kind);
	OutFlowName = TDDungeon::FlowName(Kind);
	return Template;
}

bool UTDWorldGenCommandlet::ParseSize(const FString& SizeArgument, ETDDungeonSize& OutSize)
{
	if (SizeArgument.IsEmpty() || SizeArgument.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
	{
		OutSize = ETDDungeonSize::Medium;
		return true;
	}
	if (SizeArgument.Equals(TEXT("Small"), ESearchCase::IgnoreCase))
	{
		OutSize = ETDDungeonSize::Small;
		return true;
	}
	if (SizeArgument.Equals(TEXT("Large"), ESearchCase::IgnoreCase))
	{
		OutSize = ETDDungeonSize::Large;
		return true;
	}
	return false;
}

bool UTDWorldGenCommandlet::ParseSeedRange(const FString& SeedsArgument, TArray<int32>& OutSeeds)
{
	OutSeeds.Reset();
	if (SeedsArgument.IsEmpty())
	{
		for (int32 Seed = 1; Seed <= 20; ++Seed)
		{
			OutSeeds.Add(Seed);
		}
		return true;
	}
	TArray<FString> Parts;
	SeedsArgument.ParseIntoArray(Parts, TEXT(","), true);
	for (const FString& Part : Parts)
	{
		FString FromText;
		FString ToText;
		if (Part.Split(TEXT("-"), &FromText, &ToText))
		{
			const int32 From = FCString::Atoi(*FromText);
			const int32 To = FCString::Atoi(*ToText);
			if (To < From || To - From > 10000)
			{
				return false;
			}
			for (int32 Seed = From; Seed <= To; ++Seed)
			{
				OutSeeds.Add(Seed);
			}
			continue;
		}
		if (!Part.IsNumeric())
		{
			return false;
		}
		OutSeeds.Add(FCString::Atoi(*Part));
	}
	return OutSeeds.Num() > 0;
}

FString UTDWorldGenCommandlet::BuildReportMarkdown(const FString& ThemeName, const FString& FlowName, const FString& SizeName, const TArray<FTDWorldGenSeedResult>& Results)
{
	int32 PassedCount = 0;
	for (const FTDWorldGenSeedResult& Result : Results)
	{
		PassedCount += Result.bPassed ? 1 : 0;
	}

	FString Markdown;
	Markdown += FString::Printf(TEXT("# TDWorldGen Dungeon Seed Sweep\n\n"));
	Markdown += FString::Printf(TEXT("- Theme: %s\n- Flow: %s\n- Size: %s\n- Seeds: %d\n- Passed: %d / %d\n\n"), *ThemeName, *FlowName, *SizeName, Results.Num(), PassedCount, Results.Num());
	Markdown += TEXT("| Seed | Result | Score | Rooms | Doors | Locked | Restarts | Errors | Warnings | Note |\n");
	Markdown += TEXT("|---:|:---:|---:|---:|---:|---:|---:|---:|---:|:---|\n");
	for (const FTDWorldGenSeedResult& Result : Results)
	{
		const TCHAR* ResultText = !Result.bGenerated ? TEXT("GEN-FAIL") : (Result.bPassed ? TEXT("PASS") : TEXT("FAIL"));
		Markdown += FString::Printf(TEXT("| %d | %s | %.2f | %d | %d | %d | %d | %d | %d | %s |\n"),
			Result.Seed, ResultText, Result.Score, Result.RoomCount, Result.DoorCount, Result.LockedDoorCount, Result.Restarts, Result.ErrorCount, Result.WarningCount, *Result.Error);
	}

	TArray<FTDCandidateSelector::FCandidate> Candidates;
	for (const FTDWorldGenSeedResult& Result : Results)
	{
		if (!Result.bGenerated)
		{
			continue;
		}
		FTDCandidateSelector::FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Seed = Result.Seed;
		Candidate.Score = Result.Score;
		Candidate.bPassed = Result.bPassed;
		Candidate.Summary = FString::Printf(TEXT("rooms %d, doors %d, locked %d"), Result.RoomCount, Result.DoorCount, Result.LockedDoorCount);
	}
	FTDCandidateSelector::Rank(Candidates);

	Markdown += TEXT("\n## Top 3 Recommended Seeds\n\n");
	int32 Listed = 0;
	for (const FTDCandidateSelector::FCandidate& Candidate : Candidates)
	{
		if (!Candidate.bPassed)
		{
			continue;
		}
		Markdown += FString::Printf(TEXT("%d. Seed %d (score %.2f) - %s\n"), Listed + 1, Candidate.Seed, Candidate.Score, *Candidate.Summary);
		if (++Listed >= 3)
		{
			break;
		}
	}
	if (Listed == 0)
	{
		Markdown += TEXT("No passing seed.\n");
	}
	return Markdown;
}

int32 UTDWorldGenCommandlet::Main(const FString& Params)
{
	FString ThemeArgument;
	FParse::Value(*Params, TEXT("Theme="), ThemeArgument);
	FString FlowArgument;
	FParse::Value(*Params, TEXT("Flow="), FlowArgument);
	FString SizeArgument;
	FParse::Value(*Params, TEXT("Size="), SizeArgument);
	FString SeedsArgument;
	FParse::Value(*Params, TEXT("Seeds="), SeedsArgument);
	FString ReportArgument;
	FParse::Value(*Params, TEXT("Report="), ReportArgument);
	if (ReportArgument.IsEmpty())
	{
		ReportArgument = TEXT("Saved/WorldGen/dungeon_seed_sweep.md");
	}

	ETDDungeonSize Size = ETDDungeonSize::Medium;
	if (!ParseSize(SizeArgument, Size))
	{
		UE_LOG(LogTDWorldGenCommandlet, Error, TEXT("Unknown -Size=%s (Small|Medium|Large)"), *SizeArgument);
		return 1;
	}
	TArray<int32> Seeds;
	if (!ParseSeedRange(SeedsArgument, Seeds))
	{
		UE_LOG(LogTDWorldGenCommandlet, Error, TEXT("Invalid -Seeds=%s (example: 1-20 or 3,7,9)"), *SeedsArgument);
		return 1;
	}
	UTDDungeonTheme* Theme = ResolveTheme(ThemeArgument);
	FString FlowName;
	UTDDungeonFlowTemplate* FlowTemplate = ResolveFlowTemplate(FlowArgument, FlowName);
	if (!Theme || !FlowTemplate)
	{
		UE_LOG(LogTDWorldGenCommandlet, Error, TEXT("Theme or flow template could not be resolved"));
		return 1;
	}
	const FString ThemeName = Theme->ThemeId.ToString();
	const FString SizeName = TDDungeon::SizeName(Size);
	UE_LOG(LogTDWorldGenCommandlet, Display, TEXT("Sweep theme=%s flow=%s size=%s seeds=%d"), *ThemeName, *FlowName, *SizeName, Seeds.Num());

	TArray<FTDWorldGenSeedResult> Results;
	Results.Reserve(Seeds.Num());
	for (int32 Seed : Seeds)
	{
		FTDWorldGenSeedResult& Result = Results.AddDefaulted_GetRef();
		Result.Seed = Seed;
		FTDDungeonLayout Layout;
		Result.bGenerated = FTDDungeonGenerator::GenerateAndValidate(*Theme, *FlowTemplate, Size, Seed, Layout, Result.Error);
		if (!Result.bGenerated)
		{
			UE_LOG(LogTDWorldGenCommandlet, Warning, TEXT("seed %d: generation failed: %s"), Seed, *Result.Error);
			continue;
		}
		Result.bPassed = Layout.Validation.bPassed;
		Result.Score = FTDCandidateSelector::ScoreDungeon(Layout);
		Result.RoomCount = Layout.Rooms.Num();
		Result.DoorCount = Layout.Doors.Num();
		Result.Restarts = Layout.LayoutRestarts;
		Result.ErrorCount = Layout.Validation.CountBySeverity(ETDValidationSeverity::Error);
		Result.WarningCount = Layout.Validation.CountBySeverity(ETDValidationSeverity::Warning);
		for (const FTDPlacedDoor& Door : Layout.Doors)
		{
			Result.LockedDoorCount += Door.IsLocked() ? 1 : 0;
		}
		UE_LOG(LogTDWorldGenCommandlet, Display, TEXT("seed %d: %s score %.2f rooms %d doors %d locked %d restarts %d errors %d warnings %d"),
			Seed, Result.bPassed ? TEXT("PASS") : TEXT("FAIL"), Result.Score, Result.RoomCount, Result.DoorCount, Result.LockedDoorCount, Result.Restarts, Result.ErrorCount, Result.WarningCount);
	}

	const FString Markdown = BuildReportMarkdown(ThemeName, FlowName, SizeName, Results);
	const FString ReportPath = UTDWorldGenEditorLibrary::ResolveProjectRelativePath(ReportArgument);
	if (!UTDWorldGenEditorLibrary::WriteTextFile(ReportPath, Markdown))
	{
		UE_LOG(LogTDWorldGenCommandlet, Error, TEXT("Report could not be written: %s"), *ReportPath);
		return 1;
	}
	UE_LOG(LogTDWorldGenCommandlet, Display, TEXT("Report written: %s"), *ReportPath);
	UE_LOG(LogTDWorldGenCommandlet, Display, TEXT("%s"), *Markdown);

	const bool bAnyPassed = Results.ContainsByPredicate([](const FTDWorldGenSeedResult& Result) { return Result.bPassed; });
	return bAnyPassed ? 0 : 1;
}
