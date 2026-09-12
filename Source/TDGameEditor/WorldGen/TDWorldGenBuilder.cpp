#include "TDWorldGenBuilder.h"

#include "Dungeon/TDDungeonDefinitions.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "LandscapeProxy.h"
#include "Misc/Paths.h"
#include "TDWorldBaker.h"
#include "TDWorldGenEditorLibrary.h"
#include "TDWorldGenSettings.h"
#include "UObject/Package.h"
#include "World/TDWorldDefinitions.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDWorldGenBuilder, Log, All);

namespace
{
	void AppendAnchorsWithoutDuplicates(const TArray<FTDWorldAnchor>& Source, TArray<FTDWorldAnchor>& InOutAnchors)
	{
		for (const FTDWorldAnchor& Anchor : Source)
		{
			const bool bExists = InOutAnchors.ContainsByPredicate([&Anchor](const FTDWorldAnchor& Existing) { return !Anchor.AnchorId.IsNone() && Existing.AnchorId == Anchor.AnchorId; });
			if (!bExists)
			{
				InOutAnchors.Add(Anchor);
			}
		}
	}

	UTDRegionDefinition* PickRegion(const UTDWorldDefinition& WorldDefinition, FName RegionId, FString& OutAvailable)
	{
		UTDRegionDefinition* First = nullptr;
		for (const TSoftObjectPtr<UTDRegionDefinition>& RegionPtr : WorldDefinition.Regions)
		{
			UTDRegionDefinition* Region = RegionPtr.LoadSynchronous();
			if (!Region)
			{
				continue;
			}
			OutAvailable += Region->RegionId.ToString() + TEXT(" ");
			First = First ? First : Region;
			if (!RegionId.IsNone() && Region->RegionId == RegionId)
			{
				return Region;
			}
		}
		return RegionId.IsNone() ? First : nullptr;
	}
}

UTDWorldGenBuilder::UTDWorldGenBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FString UTDWorldGenBuilder::MakeDefaultReportPath()
{
	return FString::Printf(TEXT("Saved/WorldGen/%s.md"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M")));
}

void UTDWorldGenBuilder::ParseArgs(FTDWorldGenBuilderArgs& OutArgs) const
{
	GetParamValue(TEXT("Seed="), OutArgs.Seed);
	OutArgs.bShouldValidate = HasParam(TEXT("Validate"));
	OutArgs.bShouldBake = HasParam(TEXT("Bake"));
	OutArgs.bClearExisting = !HasParam(TEXT("KeepExisting"));
	FString RegionText;
	if (GetParamValue(TEXT("Region="), RegionText) && !RegionText.IsEmpty())
	{
		OutArgs.RegionId = FName(*RegionText);
	}
	GetParamValue(TEXT("Report="), OutArgs.ReportPath);
	if (OutArgs.ReportPath.IsEmpty())
	{
		OutArgs.ReportPath = MakeDefaultReportPath();
	}
	GetParamValue(TEXT("WorldDef="), OutArgs.WorldDefinitionPath);
}

bool UTDWorldGenBuilder::ResolveInputs(UWorld* World, const FTDWorldGenBuilderArgs& BuilderArgs, FTDWorldGenBuilderInputs& OutInputs, FString& OutError) const
{
	const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
	if (!BuilderArgs.WorldDefinitionPath.IsEmpty())
	{
		OutInputs.WorldDefinition = LoadObject<UTDWorldDefinition>(nullptr, *BuilderArgs.WorldDefinitionPath);
	}
	else if (Settings)
	{
		OutInputs.WorldDefinition = Settings->DefaultWorldDefinition.LoadSynchronous();
	}
	if (!OutInputs.WorldDefinition)
	{
		OutError = TEXT("World definition not found (-WorldDef=/Game/... or Project Settings > TD World Generation > DefaultWorldDefinition)");
		return false;
	}

	FString AvailableRegions;
	OutInputs.Region = PickRegion(*OutInputs.WorldDefinition, BuilderArgs.RegionId, AvailableRegions);
	if (!OutInputs.Region)
	{
		OutError = FString::Printf(TEXT("Region '%s' not found in world definition %s (available: %s)"), *BuilderArgs.RegionId.ToString(), *OutInputs.WorldDefinition->GetPathName(), *AvailableRegions);
		return false;
	}

	OutInputs.Atlas = OutInputs.WorldDefinition->DungeonAtlas.LoadSynchronous();
	if (!OutInputs.Atlas && Settings)
	{
		OutInputs.Atlas = Settings->DefaultDungeonAtlas.LoadSynchronous();
	}

	OutInputs.Anchors = OutInputs.WorldDefinition->HandAuthoredAnchors;
	AppendAnchorsWithoutDuplicates(UTDWorldGenEditorLibrary::CollectWorldAnchorActors(World), OutInputs.Anchors);
	FTDWorldBaker::CollectLockedElements(World, OutInputs.Locked);

	OutInputs.BoundsCm = OutInputs.WorldDefinition->FieldBoundsCm;
	FBox2D RegionVolumeBounds(ForceInit);
	if (!BuilderArgs.RegionId.IsNone() && FTDWorldBaker::ResolveRegionBounds(World, BuilderArgs.RegionId, RegionVolumeBounds))
	{
		OutInputs.BoundsCm = RegionVolumeBounds;
		OutInputs.RegionFilter = BuilderArgs.RegionId;
	}
	else if (!BuilderArgs.RegionId.IsNone())
	{
		UE_LOG(LogTDWorldGenBuilder, Warning, TEXT("No ATDRegionVolume with RegionId '%s': using FieldBoundsCm and a full-world bake"), *BuilderArgs.RegionId.ToString());
	}
	if (!OutInputs.BoundsCm.bIsValid || OutInputs.BoundsCm.GetArea() <= 0.0)
	{
		OutError = TEXT("Generation bounds are empty");
		return false;
	}
	return true;
}

bool UTDWorldGenBuilder::SaveDirtyWorldPackages(UWorld* World, FPackageSourceControlHelper& PackageHelper) const
{
	TArray<UPackage*> DirtyPackages;
	FEditorFileUtils::GetDirtyWorldPackages(DirtyPackages);
	TArray<UPackage*> PackagesToSave;
	TArray<UPackage*> PackagesToDelete;
	for (UPackage* Package : DirtyPackages)
	{
		if (!Package)
		{
			continue;
		}
		if (UPackage::IsEmptyPackage(Package))
		{
			PackagesToDelete.Add(Package);
			continue;
		}
		PackagesToSave.Add(Package);
	}
	UE_LOG(LogTDWorldGenBuilder, Display, TEXT("Saving %d packages, deleting %d empty packages"), PackagesToSave.Num(), PackagesToDelete.Num());
	if (PackagesToSave.Num() > 0 && !SavePackages(PackagesToSave, PackageHelper))
	{
		return false;
	}
	if (PackagesToDelete.Num() > 0 && !DeletePackages(PackagesToDelete, PackageHelper))
	{
		return false;
	}
	return true;
}

FString UTDWorldGenBuilder::BuildReportMarkdown(const FTDWorldGenBuilderArgs& BuilderArgs, const FTDWorldGenBuilderInputs& Inputs, const FTDWorldLayout& Layout, const FTDValidationReport& StandaloneValidation, bool bValidationPassed, const FString& BakeSummary)
{
	const FString Title = FString::Printf(TEXT("TDWorldGenBuilder %s seed %d"), *Layout.RegionId.ToString(), Layout.Seed);
	FString Markdown = Layout.Validation.ToMarkdown(Title);
	Markdown += FString::Printf(TEXT("\n## 입력\n\n- 월드 정의: %s\n- 지역 정의: %s\n- 아틀라스: %s\n- 범위(cm): (%.0f, %.0f) - (%.0f, %.0f)\n- 앵커 %d, 잠긴 POI %d, 잠긴 입구 %d, 잠긴 도로 %d\n- 지역 필터: %s\n"),
		*Inputs.WorldDefinition->GetPathName(), *Inputs.Region->GetPathName(), Inputs.Atlas ? *Inputs.Atlas->GetPathName() : TEXT("(없음)"),
		Inputs.BoundsCm.Min.X, Inputs.BoundsCm.Min.Y, Inputs.BoundsCm.Max.X, Inputs.BoundsCm.Max.Y,
		Inputs.Anchors.Num(), Inputs.Locked.Pois.Num(), Inputs.Locked.Entrances.Num(), Inputs.Locked.Roads.Num(),
		Inputs.RegionFilter.IsNone() ? TEXT("(전체)") : *Inputs.RegionFilter.ToString());
	Markdown += FString::Printf(TEXT("\n## 결과\n\n- POI %d, 입구 %d, 도로 %d, 배제 %d\n- 레이아웃 해시: %u\n"),
		Layout.Pois.Num(), Layout.Entrances.Num(), Layout.Roads.Num(), Layout.Exclusions.Num(), Layout.ComputeHash());
	if (BuilderArgs.bShouldValidate)
	{
		Markdown += FString::Printf(TEXT("- 검증(-Validate): %s (생성 시 지형 검증 %s, 독립 검증 점수 %.1f %s)\n"),
			bValidationPassed ? TEXT("PASS") : TEXT("FAIL"), Layout.Validation.bPassed ? TEXT("PASS") : TEXT("FAIL"), StandaloneValidation.Score, StandaloneValidation.bPassed ? TEXT("PASS") : TEXT("FAIL"));
	}
	Markdown += FString::Printf(TEXT("- 베이크(-Bake): %s\n"), BakeSummary.IsEmpty() ? TEXT("실행 안 함") : *BakeSummary);
	return Markdown;
}

bool UTDWorldGenBuilder::RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper)
{
	if (!World)
	{
		UE_LOG(LogTDWorldGenBuilder, Error, TEXT("World is null"));
		return false;
	}
	FTDWorldGenBuilderArgs BuilderArgs;
	ParseArgs(BuilderArgs);
	UE_LOG(LogTDWorldGenBuilder, Display, TEXT("TDWorldGenBuilder: map %s seed %d validate %d bake %d region %s report %s"),
		*World->GetPackage()->GetName(), BuilderArgs.Seed, BuilderArgs.bShouldValidate ? 1 : 0, BuilderArgs.bShouldBake ? 1 : 0, *BuilderArgs.RegionId.ToString(), *BuilderArgs.ReportPath);

	FTDWorldGenBuilderInputs Inputs;
	FString Error;
	if (!ResolveInputs(World, BuilderArgs, Inputs, Error))
	{
		UE_LOG(LogTDWorldGenBuilder, Error, TEXT("%s"), *Error);
		return false;
	}

	ALandscapeProxy* Landscape = UTDWorldGenEditorLibrary::FindLandscapeForSampling(World);
	FTDWorldLayout Layout;
	if (!UTDWorldGenEditorLibrary::GenerateWorldLayoutWithLocked(World, Inputs.Region, Inputs.Anchors, Inputs.Locked, Inputs.BoundsCm, BuilderArgs.Seed, Landscape, Inputs.Atlas, Layout, Error))
	{
		UE_LOG(LogTDWorldGenBuilder, Error, TEXT("Generation failed: %s"), *Error);
		return false;
	}
	if (!Error.IsEmpty())
	{
		UE_LOG(LogTDWorldGenBuilder, Warning, TEXT("Generation warnings: %s"), *Error);
	}

	FTDValidationReport StandaloneValidation;
	bool bValidationPassed = true;
	if (BuilderArgs.bShouldValidate)
	{
		StandaloneValidation = UTDWorldGenEditorLibrary::ValidateWorldLayout(Layout, Inputs.Atlas);
		bValidationPassed = Layout.Validation.bPassed && StandaloneValidation.bPassed;
		UTDWorldGenEditorLibrary::LogReportToMessageLog(Layout.Validation, FString::Printf(TEXT("TDWorldGenBuilder seed %d"), BuilderArgs.Seed));
	}

	FString BakeSummary;
	bool bBakeSucceeded = true;
	if (BuilderArgs.bShouldBake && BuilderArgs.bShouldValidate && !bValidationPassed)
	{
		BakeSummary = TEXT("검증 실패로 건너뜀");
		UE_LOG(LogTDWorldGenBuilder, Error, TEXT("Bake skipped: validation failed"));
	}
	else if (BuilderArgs.bShouldBake)
	{
		FTDWorldBakeOptions Options;
		Options.bClearExisting = BuilderArgs.bClearExisting;
		Options.RegionFilter = Inputs.RegionFilter;
		FTDWorldBakeStats Stats;
		bBakeSucceeded = FTDWorldBaker::BakeWithOptions(World, Layout, Options, Stats, Error);
		BakeSummary = bBakeSucceeded ? Stats.ToString() : FString::Printf(TEXT("실패: %s"), *Error);
		if (bBakeSucceeded)
		{
			bBakeSucceeded = SaveDirtyWorldPackages(World, PackageHelper);
			BakeSummary += bBakeSucceeded ? TEXT("; 저장 완료") : TEXT("; 저장 실패");
		}
		if (bBakeSucceeded)
		{
			UE_LOG(LogTDWorldGenBuilder, Display, TEXT("Bake: %s"), *BakeSummary);
		}
		else
		{
			UE_LOG(LogTDWorldGenBuilder, Error, TEXT("Bake: %s"), *BakeSummary);
		}
	}

	const FString Markdown = BuildReportMarkdown(BuilderArgs, Inputs, Layout, StandaloneValidation, bValidationPassed, BakeSummary);
	const FString ReportPath = UTDWorldGenEditorLibrary::ResolveProjectRelativePath(BuilderArgs.ReportPath);
	if (!UTDWorldGenEditorLibrary::WriteTextFile(ReportPath, Markdown))
	{
		UE_LOG(LogTDWorldGenBuilder, Error, TEXT("Report could not be written: %s"), *ReportPath);
		return false;
	}
	UE_LOG(LogTDWorldGenBuilder, Display, TEXT("Report written: %s"), *ReportPath);

	if (BuilderArgs.bShouldValidate && !bValidationPassed)
	{
		UE_LOG(LogTDWorldGenBuilder, Error, TEXT("Validation failed: seed %d score %.1f errors %d"), BuilderArgs.Seed, Layout.Validation.Score, Layout.Validation.CountBySeverity(ETDValidationSeverity::Error));
		return false;
	}
	return bBakeSucceeded;
}
