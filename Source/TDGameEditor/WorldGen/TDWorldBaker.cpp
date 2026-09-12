#include "TDWorldBaker.h"

#include <initializer_list>

#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Editor/EditorEngine.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "World/Generation/TDRegionVolume.h"
#include "World/Generation/TDRoadSplineActor.h"

#define LOCTEXT_NAMESPACE "TDWorldBaker"

DEFINE_LOG_CATEGORY_STATIC(LogTDWorldBaker, Log, All);

namespace
{
	const TCHAR* WorldLabelPrefix = TEXT("TDGenWorld_");
	const TCHAR* PoiLabelInfix = TEXT("Poi_");
	const TCHAR* EntranceLabelInfix = TEXT("Entrance_");
	const TCHAR* RoadLabelInfix = TEXT("Road_");
	const TCHAR* ExclusionLabelInfix = TEXT("Exclusion_");
	const TCHAR* GuidTagPrefix = TEXT("TDGuid:");
	const TCHAR* SeedTagPrefix = TEXT("TDSeed:");
	const TCHAR* RadiusTagPrefix = TEXT("R");
	const FName WorldFolder(TEXT("TDGen/World"));
	const FName TagGen(TEXT("TDGen"));
	const FName TagLocked(TEXT("TDLocked"));
	const FName TagPoi(TEXT("TDPoi"));
	const FName TagEntrance(TEXT("TDDungeonEntrance"));
	const FName TagRoad(TEXT("TDRoad"));
	const FName TagExclusion(TEXT("TDExclusion"));
	const FName TagMain(TEXT("Main"));
	const FName TagSide(TEXT("Side"));
	const FName TagPrimary(TEXT("Primary"));
	const FName TagSecondary(TEXT("Secondary"));

	const TCHAR* PoiKindName(ETDPoiKind Kind)
	{
		switch (Kind)
		{
		case ETDPoiKind::Camp: return TEXT("Camp");
		case ETDPoiKind::Shrine: return TEXT("Shrine");
		case ETDPoiKind::Ruin: return TEXT("Ruin");
		case ETDPoiKind::Graveyard: return TEXT("Graveyard");
		case ETDPoiKind::EventArea: return TEXT("EventArea");
		case ETDPoiKind::Arena: return TEXT("Arena");
		default: return TEXT("Custom");
		}
	}

	FName MakeRadiusTag(float RadiusCm)
	{
		return FName(*FString::Printf(TEXT("%s%d"), RadiusTagPrefix, FMath::RoundToInt(RadiusCm)));
	}

	FName MakeGuidTag(const FGuid& StableId)
	{
		return FName(*(FString(GuidTagPrefix) + StableId.ToString(EGuidFormats::Digits)));
	}

	FName MakeSeedTag(int32 Seed)
	{
		return FName(*FString::Printf(TEXT("%s%d"), SeedTagPrefix, Seed));
	}

	bool ReadPrefixedTag(const AActor& Actor, const TCHAR* Prefix, FString& OutValue)
	{
		for (const FName& Tag : Actor.Tags)
		{
			const FString TagText = Tag.ToString();
			if (TagText.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				OutValue = TagText.Mid(FCString::Strlen(Prefix));
				return true;
			}
		}
		return false;
	}

	bool ReadRadiusTag(const AActor& Actor, float& OutRadiusCm)
	{
		for (const FName& Tag : Actor.Tags)
		{
			const FString TagText = Tag.ToString();
			if (TagText.Len() > 1 && TagText.StartsWith(RadiusTagPrefix, ESearchCase::CaseSensitive) && TagText.Mid(1).IsNumeric())
			{
				OutRadiusCm = FCString::Atof(*TagText.Mid(1));
				return true;
			}
		}
		return false;
	}

	bool ReadPoiKindTag(const AActor& Actor, ETDPoiKind& OutKind)
	{
		const UEnum* KindEnum = StaticEnum<ETDPoiKind>();
		for (const FName& Tag : Actor.Tags)
		{
			const int64 Value = KindEnum->GetValueByName(Tag);
			if (Value != INDEX_NONE)
			{
				OutKind = static_cast<ETDPoiKind>(Value);
				return true;
			}
		}
		return false;
	}

	FString MakeLabel(const TCHAR* Infix, const FString& Id)
	{
		return FString::Printf(TEXT("%s%s%s"), WorldLabelPrefix, Infix, *Id);
	}

	bool SplitGeneratedLabel(const FString& Label, const TCHAR* Infix, FString& OutId)
	{
		const FString FullPrefix = FString(WorldLabelPrefix) + Infix;
		if (!Label.StartsWith(FullPrefix))
		{
			return false;
		}
		OutId = Label.Mid(FullPrefix.Len());
		return !OutId.IsEmpty();
	}

	void ApplyTags(AActor& Actor, std::initializer_list<FName> ExtraTags, int32 Seed, const FGuid& StableId)
	{
		Actor.Tags.Reset();
		Actor.Tags.Add(TagGen);
		for (const FName& Tag : ExtraTags)
		{
			if (!Tag.IsNone())
			{
				Actor.Tags.AddUnique(Tag);
			}
		}
		Actor.Tags.AddUnique(MakeSeedTag(Seed));
		if (StableId.IsValid())
		{
			Actor.Tags.AddUnique(MakeGuidTag(StableId));
		}
	}

	void FinishActor(AActor& Actor, const FString& Label, std::initializer_list<FName> ExtraTags, int32 Seed, const FGuid& StableId)
	{
		if (Actor.GetActorLabel() != Label)
		{
			FActorLabelUtilities::SetActorLabelUnique(&Actor, Label);
		}
		Actor.SetFolderPath(WorldFolder);
		ApplyTags(Actor, ExtraTags, Seed, StableId);
	}

	bool IsRoadTouchingRegion(const ATDRoadSplineActor& Road, const FBox2D& RegionBoundsCm)
	{
		if (!Road.Spline)
		{
			return FTDWorldLayoutRegionFilter::ContainsLocation(RegionBoundsCm, Road.GetActorLocation());
		}
		for (int32 Index = 0; Index < Road.Spline->GetNumberOfSplinePoints(); ++Index)
		{
			if (FTDWorldLayoutRegionFilter::ContainsLocation(RegionBoundsCm, Road.Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World)))
			{
				return true;
			}
		}
		return false;
	}

	bool IsActorInsideRegion(const AActor& Actor, const FTDWorldBakeOptions& Options)
	{
		if (Options.RegionFilter.IsNone())
		{
			return true;
		}
		if (const ATDRoadSplineActor* Road = Cast<ATDRoadSplineActor>(&Actor))
		{
			return IsRoadTouchingRegion(*Road, Options.RegionBoundsCm);
		}
		return FTDWorldLayoutRegionFilter::ContainsLocation(Options.RegionBoundsCm, Actor.GetActorLocation());
	}

	struct FTDExistingGeneratedActors
	{
		TSet<FString> LockedLabels;
		TMap<FGuid, AActor*> ByStableId;
		TArray<AActor*> WithoutStableId;
	};

	void CollectExistingGeneratedActors(UWorld* World, const FTDWorldBakeOptions& Options, FTDExistingGeneratedActors& OutExisting, FTDWorldBakeStats& Stats)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->GetActorLabel().StartsWith(WorldLabelPrefix))
			{
				continue;
			}
			if (FTDWorldBaker::IsLockedActor(**It))
			{
				OutExisting.LockedLabels.Add(It->GetActorLabel());
				++Stats.LockedKeptCount;
				continue;
			}
			if (!IsActorInsideRegion(**It, Options))
			{
				++Stats.ActorsOutsideRegionCount;
				continue;
			}
			FGuid StableId;
			if (FTDWorldBaker::ReadStableId(**It, StableId) && !OutExisting.ByStableId.Contains(StableId))
			{
				OutExisting.ByStableId.Add(StableId, *It);
				continue;
			}
			OutExisting.WithoutStableId.Add(*It);
		}
	}

	struct FTDBakeSession
	{
		UWorld* World = nullptr;
		int32 Seed = 0;
		bool bClearExisting = true;
		FTDExistingGeneratedActors Existing;
		FTDWorldBakeStats* Stats = nullptr;

		AActor* TakeExistingByStableId(const FGuid& StableId)
		{
			if (!StableId.IsValid())
			{
				return nullptr;
			}
			AActor* Found = nullptr;
			Existing.ByStableId.RemoveAndCopyValue(StableId, Found);
			return Found;
		}

		bool IsLockedLabel(const FString& Label) const
		{
			if (!Existing.LockedLabels.Contains(Label))
			{
				return false;
			}
			++Stats->SkippedForLockedCount;
			return true;
		}

		void DestroyActor(AActor* Actor)
		{
			if (!Actor)
			{
				return;
			}
			World->EditorDestroyActor(Actor, true);
			++Stats->RemovedCount;
		}

		void PlaceTargetPoint(const FVector& Location, float Yaw, const FString& Label, std::initializer_list<FName> ExtraTags, const FGuid& StableId)
		{
			if (AActor* ExistingActor = TakeExistingByStableId(StableId))
			{
				ExistingActor->Modify();
				ExistingActor->SetActorLocationAndRotation(Location, FRotator(0.0f, Yaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
				FinishActor(*ExistingActor, Label, ExtraTags, Seed, StableId);
				++Stats->UpdatedCount;
				return;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATargetPoint* Actor = World->SpawnActor<ATargetPoint>(Location, FRotator(0.0f, Yaw, 0.0f), Params);
			if (!Actor)
			{
				return;
			}
			FinishActor(*Actor, Label, ExtraTags, Seed, StableId);
			++Stats->CreatedCount;
		}

		void PlaceRoad(const FTDRoadPolyline& Road, const FString& Label)
		{
			const std::initializer_list<FName> RoadTags = { TagRoad, Road.bIsPrimary ? TagPrimary : TagSecondary };
			ATDRoadSplineActor* RoadActor = Cast<ATDRoadSplineActor>(TakeExistingByStableId(Road.StableId));
			const bool bIsUpdate = RoadActor != nullptr;
			if (!RoadActor)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				RoadActor = World->SpawnActor<ATDRoadSplineActor>(Road.PointsCm[0], FRotator::ZeroRotator, Params);
			}
			if (!RoadActor)
			{
				return;
			}
			RoadActor->Modify();
			RoadActor->SetActorLocation(Road.PointsCm[0], false, nullptr, ETeleportType::TeleportPhysics);
			RoadActor->RoadId = Road.RoadId;
			RoadActor->WidthCm = Road.WidthCm;
			RoadActor->SetRoadPoints(Road.PointsCm);
			FinishActor(*RoadActor, Label, RoadTags, Seed, Road.StableId);
			if (bIsUpdate)
			{
				++Stats->UpdatedCount;
				return;
			}
			++Stats->CreatedCount;
		}
	};

	void ReadLockedPoi(const AActor& Actor, const FString& PoiId, FTDLockedLayoutElements& OutLocked)
	{
		FTDPoiPlacement& Poi = OutLocked.Pois.AddDefaulted_GetRef();
		Poi.PoiId = FName(*PoiId);
		int32 SeparatorIndex = INDEX_NONE;
		Poi.ArchetypeId = PoiId.FindLastChar(TEXT('_'), SeparatorIndex) && SeparatorIndex > 0 ? FName(*PoiId.Left(SeparatorIndex)) : Poi.PoiId;
		ReadPoiKindTag(Actor, Poi.Kind);
		ReadRadiusTag(Actor, Poi.ExclusionRadiusCm);
		Poi.LocationCm = Actor.GetActorLocation();
		Poi.Yaw = Actor.GetActorRotation().Yaw;
		Poi.bLocked = true;
		FTDWorldBaker::ReadStableId(Actor, Poi.StableId);
	}

	void ReadLockedEntrance(const AActor& Actor, const FString& DungeonId, FTDLockedLayoutElements& OutLocked)
	{
		FTDDungeonEntrancePlacement& Entrance = OutLocked.Entrances.AddDefaulted_GetRef();
		Entrance.DungeonId = FName(*DungeonId);
		Entrance.LocationCm = Actor.GetActorLocation();
		Entrance.Yaw = Actor.GetActorRotation().Yaw;
		Entrance.bIsMain = Actor.Tags.Contains(TagMain);
		Entrance.bLocked = true;
		FTDWorldBaker::ReadStableId(Actor, Entrance.StableId);
	}

	void ReadLockedRoad(const AActor& Actor, const FString& RoadId, FTDLockedLayoutElements& OutLocked)
	{
		const ATDRoadSplineActor* RoadActor = Cast<ATDRoadSplineActor>(&Actor);
		if (!RoadActor || !RoadActor->Spline)
		{
			return;
		}
		FTDRoadPolyline& Road = OutLocked.Roads.AddDefaulted_GetRef();
		Road.RoadId = RoadActor->RoadId.IsNone() ? FName(*RoadId) : RoadActor->RoadId;
		Road.WidthCm = RoadActor->WidthCm;
		Road.bIsPrimary = !Actor.Tags.Contains(TagSecondary);
		Road.bLocked = true;
		for (int32 Index = 0; Index < RoadActor->Spline->GetNumberOfSplinePoints(); ++Index)
		{
			Road.PointsCm.Add(RoadActor->Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World));
		}
		FTDWorldBaker::ReadStableId(Actor, Road.StableId);
	}
}

FString FTDWorldBakeStats::ToString() const
{
	return FString::Printf(TEXT("removed %d, updated %d, created %d, locked kept %d, skipped for locked %d, actors outside region %d, layout elements outside region %d"),
		RemovedCount, UpdatedCount, CreatedCount, LockedKeptCount, SkippedForLockedCount, ActorsOutsideRegionCount, LayoutElementsOutsideRegionCount);
}

const TCHAR* FTDWorldBaker::GetLabelPrefix()
{
	return WorldLabelPrefix;
}

FName FTDWorldBaker::GetLockedTagName()
{
	return TagLocked;
}

FName FTDWorldBaker::GetGeneratedTagName()
{
	return TagGen;
}

bool FTDWorldBaker::IsLockedActor(const AActor& Actor)
{
	if (Actor.Tags.Contains(TagLocked))
	{
		return true;
	}
	const FBoolProperty* LockedProperty = FindFProperty<FBoolProperty>(Actor.GetClass(), TEXT("bLocked"));
	return LockedProperty != nullptr && LockedProperty->GetPropertyValue_InContainer(&Actor);
}

bool FTDWorldBaker::ReadStableId(const AActor& Actor, FGuid& OutStableId)
{
	FString GuidText;
	return ReadPrefixedTag(Actor, GuidTagPrefix, GuidText) && FGuid::Parse(GuidText, OutStableId) && OutStableId.IsValid();
}

bool FTDWorldBaker::ResolveRegionBounds(UWorld* World, FName RegionId, FBox2D& OutBoundsCm)
{
	if (!World || RegionId.IsNone())
	{
		return false;
	}
	for (TActorIterator<ATDRegionVolume> It(World); It; ++It)
	{
		if (It->RegionId != RegionId || !It->GetRegionBounds())
		{
			continue;
		}
		const FBox Box = It->GetRegionBounds()->Bounds.GetBox();
		OutBoundsCm = FBox2D(FVector2D(Box.Min.X, Box.Min.Y), FVector2D(Box.Max.X, Box.Max.Y));
		return OutBoundsCm.bIsValid && OutBoundsCm.GetArea() > 0.0;
	}
	return false;
}

void FTDWorldBaker::CollectLockedElements(UWorld* World, FTDLockedLayoutElements& OutLocked)
{
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const FString Label = It->GetActorLabel();
		if (!Label.StartsWith(WorldLabelPrefix) || !IsLockedActor(**It))
		{
			continue;
		}
		FString Id;
		if (SplitGeneratedLabel(Label, PoiLabelInfix, Id))
		{
			ReadLockedPoi(**It, Id, OutLocked);
		}
		else if (SplitGeneratedLabel(Label, EntranceLabelInfix, Id))
		{
			ReadLockedEntrance(**It, Id, OutLocked);
		}
		else if (SplitGeneratedLabel(Label, RoadLabelInfix, Id))
		{
			ReadLockedRoad(**It, Id, OutLocked);
		}
	}
}

int32 FTDWorldBaker::ClearGenerated(UWorld* World)
{
	if (!World)
	{
		return 0;
	}
	TArray<AActor*> ToRemove;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().StartsWith(WorldLabelPrefix) && !IsLockedActor(**It))
		{
			ToRemove.Add(*It);
		}
	}
	for (AActor* Actor : ToRemove)
	{
		World->EditorDestroyActor(Actor, true);
	}
	return ToRemove.Num();
}

bool FTDWorldBaker::Bake(UWorld* World, const FTDWorldLayout& Layout, bool bClearExisting, FString& OutError)
{
	FTDWorldBakeOptions Options;
	Options.bClearExisting = bClearExisting;
	FTDWorldBakeStats Stats;
	return BakeWithOptions(World, Layout, Options, Stats, OutError);
}

bool FTDWorldBaker::BakeWithOptions(UWorld* World, const FTDWorldLayout& Layout, const FTDWorldBakeOptions& Options, FTDWorldBakeStats& OutStats, FString& OutError)
{
	OutStats = FTDWorldBakeStats();
	if (!World)
	{
		OutError = TEXT("World is null");
		return false;
	}
	FTDWorldBakeOptions ResolvedOptions = Options;
	if (!ResolvedOptions.RegionFilter.IsNone() && !ResolvedOptions.RegionBoundsCm.bIsValid && !ResolveRegionBounds(World, ResolvedOptions.RegionFilter, ResolvedOptions.RegionBoundsCm))
	{
		OutError = FString::Printf(TEXT("Region volume not found or empty: %s"), *ResolvedOptions.RegionFilter.ToString());
		return false;
	}

	FTDWorldLayout Target = Layout;
	if (!ResolvedOptions.RegionFilter.IsNone())
	{
		FTDWorldLayout Outside;
		FTDWorldLayoutRegionFilter::Split(Layout, ResolvedOptions.RegionBoundsCm, Target, Outside);
		OutStats.LayoutElementsOutsideRegionCount = Outside.Pois.Num() + Outside.Entrances.Num() + Outside.Roads.Num() + Outside.Exclusions.Num();
	}

	const FScopedTransaction Transaction(LOCTEXT("BakeWorld", "TD Bake World Layout"));
	FTDBakeSession Session;
	Session.World = World;
	Session.Seed = Layout.Seed;
	Session.bClearExisting = ResolvedOptions.bClearExisting;
	Session.Stats = &OutStats;
	CollectExistingGeneratedActors(World, ResolvedOptions, Session.Existing, OutStats);
	if (ResolvedOptions.bClearExisting)
	{
		for (AActor* Actor : Session.Existing.WithoutStableId)
		{
			Session.DestroyActor(Actor);
		}
		Session.Existing.WithoutStableId.Reset();
	}

	for (const FTDPoiPlacement& Poi : Target.Pois)
	{
		const FString Label = MakeLabel(PoiLabelInfix, Poi.PoiId.ToString());
		if (Session.IsLockedLabel(Label))
		{
			continue;
		}
		Session.PlaceTargetPoint(Poi.LocationCm, Poi.Yaw, Label,
			{ TagPoi, FName(PoiKindName(Poi.Kind)), Poi.ArchetypeId, MakeRadiusTag(Poi.ExclusionRadiusCm) }, Poi.StableId);
	}

	for (const FTDDungeonEntrancePlacement& Entrance : Target.Entrances)
	{
		const FString Label = MakeLabel(EntranceLabelInfix, Entrance.DungeonId.ToString());
		if (Session.IsLockedLabel(Label))
		{
			continue;
		}
		Session.PlaceTargetPoint(Entrance.LocationCm, Entrance.Yaw, Label,
			{ TagEntrance, Entrance.DungeonId, Entrance.bIsMain ? TagMain : TagSide }, Entrance.StableId);
	}

	for (const FTDRoadPolyline& Road : Target.Roads)
	{
		if (Road.PointsCm.Num() < 2)
		{
			continue;
		}
		const FString Label = MakeLabel(RoadLabelInfix, Road.RoadId.ToString());
		if (Session.IsLockedLabel(Label))
		{
			continue;
		}
		Session.PlaceRoad(Road, Label);
	}

	for (int32 Index = 0; Index < Target.Exclusions.Num(); ++Index)
	{
		const FTDExclusionArea& Exclusion = Target.Exclusions[Index];
		Session.PlaceTargetPoint(Exclusion.CenterCm, 0.0f, MakeLabel(ExclusionLabelInfix, FString::FromInt(Index)),
			{ TagExclusion, MakeRadiusTag(Exclusion.RadiusCm), Exclusion.Reason }, FGuid());
	}

	if (ResolvedOptions.bClearExisting)
	{
		for (const TPair<FGuid, AActor*>& Stale : Session.Existing.ByStableId)
		{
			Session.DestroyActor(Stale.Value);
		}
		Session.Existing.ByStableId.Reset();
	}

	UE_LOG(LogTDWorldBaker, Log, TEXT("World bake (%s seed %d, region filter %s): %s; pois %d, entrances %d, roads %d, exclusions %d, anchors untouched %d"),
		*Layout.RegionId.ToString(), Layout.Seed, ResolvedOptions.RegionFilter.IsNone() ? TEXT("none") : *ResolvedOptions.RegionFilter.ToString(), *OutStats.ToString(),
		Target.Pois.Num(), Target.Entrances.Num(), Target.Roads.Num(), Target.Exclusions.Num(), Layout.Anchors.Num());
	return true;
}

#undef LOCTEXT_NAMESPACE
