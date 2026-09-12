#include "TDWorldGenEditorBridgeImpl.h"

#include "Components/SplineComponent.h"
#include "Dungeon/TDDungeonDefinitions.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "TDWorldBaker.h"
#include "TDWorldGenEditorLibrary.h"
#include "World/Generation/TDRoadSplineActor.h"
#include "World/TDWorldDefinitions.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDWorldGenBridge, Log, All);

namespace
{
	const FTDDungeonSlot* FindSlotByIndex(const UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex)
	{
		if (!Atlas)
		{
			return nullptr;
		}
		return Atlas->Slots.FindByPredicate([SlotIndex](const FTDDungeonSlot& Candidate) { return Candidate.SlotIndex == SlotIndex; });
	}

	FString MakeFailureReport(const FString& Title, const FString& Reason)
	{
		UE_LOG(LogTDWorldGenBridge, Warning, TEXT("%s: %s"), *Title, *Reason);
		return FString::Printf(TEXT("# %s\n\n실패: %s\n"), *Title, *Reason);
	}

	bool ResolveSlotAssets(const FTDDungeonSlot& Slot, UTDDungeonTheme*& OutTheme, UTDDungeonFlowTemplate*& OutFlow, FString& OutReason)
	{
		OutTheme = Slot.Theme.LoadSynchronous();
		OutFlow = Slot.FlowTemplate.LoadSynchronous();
		if (!OutTheme)
		{
			OutReason = FString::Printf(TEXT("슬롯 %d에 Theme 에셋이 없습니다"), Slot.SlotIndex);
			return false;
		}
		if (!OutFlow)
		{
			OutReason = FString::Printf(TEXT("슬롯 %d에 FlowTemplate 에셋이 없습니다"), Slot.SlotIndex);
			return false;
		}
		return true;
	}

	FName ResolveDungeonId(const FTDDungeonSlot& Slot)
	{
		return Slot.DungeonId.IsNone() ? FName(*FString::Printf(TEXT("Slot_%d"), Slot.SlotIndex)) : Slot.DungeonId;
	}

	ALandscapeProxy* FindFirstLandscape(UWorld* World)
	{
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	bool ResolveOutdoorInputs(UWorld* World, UTDWorldDefinition* WorldDefinition, const FString& Title, UTDRegionDefinition*& OutRegion, UTDDungeonAtlasDefinition*& OutAtlas, FString& OutReport)
	{
		if (!World)
		{
			OutReport = MakeFailureReport(Title, TEXT("World가 null입니다"));
			return false;
		}
		if (!WorldDefinition)
		{
			OutReport = MakeFailureReport(Title, TEXT("WorldDefinition이 null입니다"));
			return false;
		}
		OutRegion = WorldDefinition->Regions.Num() > 0 ? WorldDefinition->Regions[0].LoadSynchronous() : nullptr;
		if (!OutRegion)
		{
			OutReport = MakeFailureReport(Title, TEXT("WorldDefinition.Regions[0]을 로드할 수 없습니다"));
			return false;
		}
		OutAtlas = WorldDefinition->DungeonAtlas.LoadSynchronous();
		return true;
	}

	float ParseRadiusTagCm(const AActor& Actor, float DefaultCm)
	{
		for (const FName& Tag : Actor.Tags)
		{
			const FString TagText = Tag.ToString();
			if (TagText.Len() > 1 && TagText[0] == TEXT('R') && TagText.Mid(1).IsNumeric())
			{
				return FCString::Atof(*TagText.Mid(1));
			}
		}
		return DefaultCm;
	}

	ETDPoiKind ParsePoiKindTag(const AActor& Actor)
	{
		const UEnum* KindEnum = StaticEnum<ETDPoiKind>();
		for (const FName& Tag : Actor.Tags)
		{
			const int64 Value = KindEnum->GetValueByName(Tag);
			if (Value != INDEX_NONE)
			{
				return static_cast<ETDPoiKind>(Value);
			}
		}
		return ETDPoiKind::Custom;
	}

	void CollectBakedWorldLayout(UWorld* World, const UTDWorldDefinition& WorldDefinition, const UTDRegionDefinition& Region, FTDWorldLayout& OutLayout)
	{
		OutLayout = FTDWorldLayout();
		OutLayout.RegionId = Region.RegionId;
		OutLayout.BoundsCm = WorldDefinition.FieldBoundsCm;
		OutLayout.Anchors = WorldDefinition.HandAuthoredAnchors;

		const FString Prefix = FTDWorldBaker::GetLabelPrefix();
		const FString PoiPrefix = Prefix + TEXT("Poi_");
		const FString EntrancePrefix = Prefix + TEXT("Entrance_");
		const FString ExclusionPrefix = Prefix + TEXT("Exclusion_");
		const FName LockedTag = FTDWorldBaker::GetLockedTagName();

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			const FString Label = Actor->GetActorLabel();
			if (!Label.StartsWith(Prefix))
			{
				continue;
			}
			const bool bIsLocked = Actor->Tags.Contains(LockedTag);
			if (Label.StartsWith(PoiPrefix))
			{
				FTDPoiPlacement& Poi = OutLayout.Pois.AddDefaulted_GetRef();
				Poi.PoiId = FName(*Label.Mid(PoiPrefix.Len()));
				Poi.Kind = ParsePoiKindTag(*Actor);
				Poi.LocationCm = Actor->GetActorLocation();
				Poi.Yaw = Actor->GetActorRotation().Yaw;
				Poi.ExclusionRadiusCm = ParseRadiusTagCm(*Actor, 2000.0f);
				Poi.bLocked = bIsLocked;
				FTDExclusionArea& Exclusion = OutLayout.Exclusions.AddDefaulted_GetRef();
				Exclusion.CenterCm = Poi.LocationCm;
				Exclusion.RadiusCm = Poi.ExclusionRadiusCm;
				Exclusion.Reason = TEXT("Poi");
				continue;
			}
			if (Label.StartsWith(EntrancePrefix))
			{
				FTDDungeonEntrancePlacement& Entrance = OutLayout.Entrances.AddDefaulted_GetRef();
				Entrance.DungeonId = FName(*Label.Mid(EntrancePrefix.Len()));
				Entrance.LocationCm = Actor->GetActorLocation();
				Entrance.Yaw = Actor->GetActorRotation().Yaw;
				Entrance.bIsMain = Actor->Tags.Contains(FName(TEXT("Main")));
				Entrance.bLocked = bIsLocked;
				continue;
			}
			if (Label.StartsWith(ExclusionPrefix))
			{
				FTDExclusionArea& Exclusion = OutLayout.Exclusions.AddDefaulted_GetRef();
				Exclusion.CenterCm = Actor->GetActorLocation();
				Exclusion.RadiusCm = ParseRadiusTagCm(*Actor, 1000.0f);
				Exclusion.Reason = Actor->Tags.Num() > 2 ? Actor->Tags.Last() : FName(TEXT("Unknown"));
				continue;
			}
			ATDRoadSplineActor* RoadActor = Cast<ATDRoadSplineActor>(Actor);
			if (!RoadActor || !RoadActor->Spline)
			{
				continue;
			}
			FTDRoadPolyline& Road = OutLayout.Roads.AddDefaulted_GetRef();
			Road.RoadId = RoadActor->RoadId;
			Road.WidthCm = RoadActor->WidthCm;
			Road.bIsPrimary = Actor->Tags.Contains(FName(TEXT("Primary")));
			const int32 PointCount = RoadActor->Spline->GetNumberOfSplinePoints();
			for (int32 Index = 0; Index < PointCount; ++Index)
			{
				Road.PointsCm.Add(RoadActor->Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World));
			}
		}
	}

	FString AppendLayoutSummary(const FTDWorldLayout& Layout, int32 LockedAnchorCount)
	{
		int32 LockedPois = 0;
		for (const FTDPoiPlacement& Poi : Layout.Pois)
		{
			LockedPois += Poi.bLocked ? 1 : 0;
		}
		return FString::Printf(TEXT("\n- Seed: %d\n- POI: %d (잠금 %d), 입구: %d, 도로: %d, 배제: %d\n- 앵커: %d (월드에서 수집한 잠금 앵커 %d)\n"),
			Layout.Seed, Layout.Pois.Num(), LockedPois, Layout.Entrances.Num(), Layout.Roads.Num(), Layout.Exclusions.Num(), Layout.Anchors.Num(), LockedAnchorCount);
	}
}

bool FTDWorldGenEditorBridgeImpl::GenerateAndBakeDungeon(UWorld* World, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, int32 SeedOverride, FString& OutReport)
{
	const FString Title = FString::Printf(TEXT("Dungeon slot %d generate+bake"), SlotIndex);
	if (!World)
	{
		OutReport = MakeFailureReport(Title, TEXT("World가 null입니다"));
		return false;
	}
	const FTDDungeonSlot* Slot = FindSlotByIndex(Atlas, SlotIndex);
	if (!Slot)
	{
		OutReport = MakeFailureReport(Title, TEXT("아틀라스에 해당 SlotIndex가 없습니다 (Atlas null 또는 슬롯 미정의)"));
		return false;
	}
	UTDDungeonTheme* Theme = nullptr;
	UTDDungeonFlowTemplate* Flow = nullptr;
	FString Reason;
	if (!ResolveSlotAssets(*Slot, Theme, Flow, Reason))
	{
		OutReport = MakeFailureReport(Title, Reason);
		return false;
	}
	const int32 Seed = SeedOverride >= 0 ? SeedOverride : Slot->Seed;
	const bool bPassed = UTDWorldGenEditorLibrary::GenerateAndBakeDungeon(World, Theme, Flow, Slot->Size, Seed, Atlas, SlotIndex, ResolveDungeonId(*Slot), OutReport);
	UE_LOG(LogTDWorldGenBridge, Display, TEXT("%s: seed %d %s"), *Title, Seed, bPassed ? TEXT("PASS") : TEXT("FAIL"));
	return bPassed;
}

bool FTDWorldGenEditorBridgeImpl::ValidateDungeonSlot(UWorld* World, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, FString& OutReport)
{
	const FString Title = FString::Printf(TEXT("Dungeon slot %d validate"), SlotIndex);
	const FTDDungeonSlot* Slot = FindSlotByIndex(Atlas, SlotIndex);
	if (!Slot)
	{
		OutReport = MakeFailureReport(Title, TEXT("아틀라스에 해당 SlotIndex가 없습니다 (Atlas null 또는 슬롯 미정의)"));
		return false;
	}
	UTDDungeonTheme* Theme = nullptr;
	UTDDungeonFlowTemplate* Flow = nullptr;
	FString Reason;
	if (!ResolveSlotAssets(*Slot, Theme, Flow, Reason))
	{
		OutReport = MakeFailureReport(Title, Reason);
		return false;
	}
	FTDDungeonLayout Layout;
	if (!UTDWorldGenEditorLibrary::GenerateDungeonLayout(Theme, Flow, Slot->Size, Slot->Seed, Layout, Reason))
	{
		OutReport = MakeFailureReport(Title, Reason);
		return false;
	}
	const FString LayoutTitle = FString::Printf(TEXT("%s (seed %d, %s, %s)"), *Title, Slot->Seed, TDDungeon::FlowName(Layout.Flow), TDDungeon::SizeName(Layout.Size));
	UTDWorldGenEditorLibrary::LogReportToMessageLog(Layout.Validation, LayoutTitle);
	OutReport = Layout.Validation.ToMarkdown(LayoutTitle);
	OutReport += FString::Printf(TEXT("\n- Rooms: %d, Doors: %d\n"), Layout.Rooms.Num(), Layout.Doors.Num());

	const FTDValidationReport NavReport = UTDWorldGenEditorLibrary::ValidateDungeonNavigation(World, Atlas, SlotIndex);
	OutReport += TEXT("\n") + NavReport.ToMarkdown(FString::Printf(TEXT("Dungeon slot %d navigation"), SlotIndex));
	return Layout.Validation.bPassed && !NavReport.HasErrors();
}

bool FTDWorldGenEditorBridgeImpl::GenerateOutdoor(UWorld* World, UTDWorldDefinition* WorldDefinition, int32 Seed, bool bBake, FString& OutReport)
{
	const FString Title = FString::Printf(TEXT("Outdoor seed %d %s"), Seed, bBake ? TEXT("generate+bake") : TEXT("generate"));
	UTDRegionDefinition* Region = nullptr;
	UTDDungeonAtlasDefinition* Atlas = nullptr;
	if (!ResolveOutdoorInputs(World, WorldDefinition, Title, Region, Atlas, OutReport))
	{
		return false;
	}

	TArray<FTDWorldAnchor> Anchors = WorldDefinition->HandAuthoredAnchors;
	TArray<FTDWorldAnchor> LockedAnchors;
	UTDWorldGenEditorLibrary::CollectLockedPlacements(World, LockedAnchors);
	Anchors.Append(LockedAnchors);

	FTDWorldLayout Layout;
	FString Error;
	if (!UTDWorldGenEditorLibrary::GenerateWorldLayout(World, Region, Anchors, WorldDefinition->FieldBoundsCm, Seed, FindFirstLandscape(World), Atlas, Layout, Error))
	{
		OutReport = MakeFailureReport(Title, Error);
		return false;
	}
	UTDWorldGenEditorLibrary::LogReportToMessageLog(Layout.Validation, Title);
	OutReport = Layout.Validation.ToMarkdown(Title) + AppendLayoutSummary(Layout, LockedAnchors.Num());
	if (!bBake)
	{
		return Layout.Validation.bPassed;
	}
	if (!UTDWorldGenEditorLibrary::BakeWorldLayout(World, Layout, true, Error))
	{
		OutReport += FString::Printf(TEXT("\nBake failed: %s\n"), *Error);
		UE_LOG(LogTDWorldGenBridge, Error, TEXT("%s: bake failed: %s"), *Title, *Error);
		return false;
	}
	OutReport += TEXT("\n- Bake: 완료 (기존 생성물 제거, TDLocked 유지)\n");
	return Layout.Validation.bPassed;
}

bool FTDWorldGenEditorBridgeImpl::ValidateOutdoor(UWorld* World, UTDWorldDefinition* WorldDefinition, FString& OutReport)
{
	const FString Title = TEXT("Outdoor validate (baked actors)");
	UTDRegionDefinition* Region = nullptr;
	UTDDungeonAtlasDefinition* Atlas = nullptr;
	if (!ResolveOutdoorInputs(World, WorldDefinition, Title, Region, Atlas, OutReport))
	{
		return false;
	}
	FTDWorldLayout Layout;
	CollectBakedWorldLayout(World, *WorldDefinition, *Region, Layout);
	const FTDValidationReport Report = UTDWorldGenEditorLibrary::ValidateWorldLayout(Layout, Atlas);
	UTDWorldGenEditorLibrary::LogReportToMessageLog(Report, Title);
	OutReport = Report.ToMarkdown(Title) + AppendLayoutSummary(Layout, 0);
	return Report.bPassed;
}
