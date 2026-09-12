#pragma once

#include "WorldPartition/WorldPartitionBuilder.h"
#include "World/TDWorldTypes.h"
#include "TDWorldGenBuilder.generated.h"

class UTDDungeonAtlasDefinition;
class UTDRegionDefinition;
class UTDWorldDefinition;

struct FTDWorldGenBuilderArgs
{
	int32 Seed = 7;
	bool bShouldValidate = false;
	bool bShouldBake = false;
	bool bClearExisting = true;
	FName RegionId;
	FString ReportPath;
	FString WorldDefinitionPath;
};

struct FTDWorldGenBuilderInputs
{
	UTDWorldDefinition* WorldDefinition = nullptr;
	UTDRegionDefinition* Region = nullptr;
	UTDDungeonAtlasDefinition* Atlas = nullptr;
	TArray<FTDWorldAnchor> Anchors;
	FTDLockedLayoutElements Locked;
	FBox2D BoundsCm = FBox2D(ForceInit);
	FName RegionFilter;
};

UCLASS()
class UTDWorldGenBuilder : public UWorldPartitionBuilder
{
	GENERATED_BODY()

public:
	UTDWorldGenBuilder(const FObjectInitializer& ObjectInitializer);

	virtual bool RequiresCommandletRendering() const override { return false; }
	virtual ELoadingMode GetLoadingMode() const override { return ELoadingMode::EntireWorld; }

	static FString MakeDefaultReportPath();

protected:
	virtual bool CanProcessNonPartitionedWorlds() const override { return true; }
	virtual bool RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper) override;

private:
	void ParseArgs(FTDWorldGenBuilderArgs& OutArgs) const;
	bool ResolveInputs(UWorld* World, const FTDWorldGenBuilderArgs& Args, FTDWorldGenBuilderInputs& OutInputs, FString& OutError) const;
	bool SaveDirtyWorldPackages(UWorld* World, FPackageSourceControlHelper& PackageHelper) const;
	static FString BuildReportMarkdown(const FTDWorldGenBuilderArgs& Args, const FTDWorldGenBuilderInputs& Inputs, const FTDWorldLayout& Layout, const FTDValidationReport& StandaloneValidation, bool bValidationPassed, const FString& BakeSummary);
};
