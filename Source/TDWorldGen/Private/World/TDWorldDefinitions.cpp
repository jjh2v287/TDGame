#include "World/TDWorldDefinitions.h"

#include "Dungeon/TDDungeonDefinitions.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

float FTDRoadPolyline::LengthCm() const
{
	float Length = 0.0f;
	for (int32 Index = 1; Index < PointsCm.Num(); ++Index)
	{
		Length += FVector::Dist2D(PointsCm[Index - 1], PointsCm[Index]);
	}
	return Length;
}

const FTDWorldAnchor* FTDWorldLayout::FindAnchor(ETDWorldAnchorKind Kind) const
{
	return Anchors.FindByPredicate([Kind](const FTDWorldAnchor& Anchor) { return Anchor.Kind == Kind; });
}

bool FTDWorldLayoutRegionFilter::ContainsLocation(const FBox2D& RegionBoundsCm, const FVector& LocationCm)
{
	return RegionBoundsCm.bIsValid && RegionBoundsCm.IsInside(FVector2D(LocationCm.X, LocationCm.Y));
}

bool FTDWorldLayoutRegionFilter::TouchesRoad(const FBox2D& RegionBoundsCm, const FTDRoadPolyline& Road)
{
	for (const FVector& Point : Road.PointsCm)
	{
		if (ContainsLocation(RegionBoundsCm, Point))
		{
			return true;
		}
	}
	return false;
}

void FTDWorldLayoutRegionFilter::Split(const FTDWorldLayout& Layout, const FBox2D& RegionBoundsCm, FTDWorldLayout& OutInside, FTDWorldLayout& OutOutside)
{
	OutInside = FTDWorldLayout();
	OutOutside = FTDWorldLayout();
	for (FTDWorldLayout* Target : { &OutInside, &OutOutside })
	{
		Target->Seed = Layout.Seed;
		Target->GeneratorVersion = Layout.GeneratorVersion;
		Target->RegionId = Layout.RegionId;
		Target->BoundsCm = Layout.BoundsCm;
		Target->Anchors = Layout.Anchors;
	}
	for (const FTDPoiPlacement& Poi : Layout.Pois)
	{
		(ContainsLocation(RegionBoundsCm, Poi.LocationCm) ? OutInside : OutOutside).Pois.Add(Poi);
	}
	for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
	{
		(ContainsLocation(RegionBoundsCm, Entrance.LocationCm) ? OutInside : OutOutside).Entrances.Add(Entrance);
	}
	for (const FTDRoadPolyline& Road : Layout.Roads)
	{
		(TouchesRoad(RegionBoundsCm, Road) ? OutInside : OutOutside).Roads.Add(Road);
	}
	for (const FTDExclusionArea& Exclusion : Layout.Exclusions)
	{
		(ContainsLocation(RegionBoundsCm, Exclusion.CenterCm) ? OutInside : OutOutside).Exclusions.Add(Exclusion);
	}
}

uint32 FTDWorldLayout::ComputeHash() const
{
	uint32 Hash = HashCombine(static_cast<uint32>(Seed), static_cast<uint32>(GeneratorVersion));
	auto HashVector = [&Hash](const FVector& Vector)
	{
		Hash = HashCombine(Hash, GetTypeHash(FIntVector(FMath::RoundToInt(Vector.X), FMath::RoundToInt(Vector.Y), FMath::RoundToInt(Vector.Z))));
	};
	for (const FTDPoiPlacement& Poi : Pois)
	{
		Hash = HashCombine(Hash, GetTypeHash(Poi.ArchetypeId));
		HashVector(Poi.LocationCm);
	}
	for (const FTDDungeonEntrancePlacement& Entrance : Entrances)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entrance.DungeonId));
		HashVector(Entrance.LocationCm);
	}
	for (const FTDRoadPolyline& Road : Roads)
	{
		for (const FVector& Point : Road.PointsCm)
		{
			HashVector(Point);
		}
	}
	return Hash;
}

#if WITH_EDITOR
EDataValidationResult UTDPoiArchetype::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (PoiId.IsNone())
	{
		Context.AddError(FText::FromString(TEXT("PoiId가 비어 있습니다")));
		Result = EDataValidationResult::Invalid;
	}
	if (RequiredClearRadiusCm > ExclusionRadiusCm)
	{
		Context.AddWarning(FText::FromString(TEXT("RequiredClearRadiusCm가 ExclusionRadiusCm보다 큽니다")));
	}
	return Result;
}

EDataValidationResult UTDBiomeDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (TreeSet.Num() == 0)
	{
		Context.AddWarning(FText::FromString(TEXT("TreeSet이 비어 있습니다")));
	}
	if (MinTreeDistanceCm <= 0.0f)
	{
		Context.AddError(FText::FromString(TEXT("MinTreeDistanceCm는 0보다 커야 합니다")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}

EDataValidationResult UTDRegionDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Biome.IsNull())
	{
		Context.AddError(FText::FromString(TEXT("Biome이 비어 있습니다")));
		Result = EDataValidationResult::Invalid;
	}
	for (const FTDPoiQuota& Quota : PoiQuotas)
	{
		if (Quota.Archetype.IsNull())
		{
			Context.AddError(FText::FromString(TEXT("PoiQuotas에 Archetype이 비어 있는 항목이 있습니다")));
			Result = EDataValidationResult::Invalid;
		}
		if (Quota.CountRange.X > Quota.CountRange.Y || Quota.CountRange.X < 0)
		{
			Context.AddError(FText::FromString(TEXT("PoiQuotas CountRange가 잘못되었습니다(min ≤ max, min ≥ 0)")));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}

EDataValidationResult UTDWorldDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Regions.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("Regions가 비어 있습니다")));
		Result = EDataValidationResult::Invalid;
	}
	if (DungeonAtlas.IsNull())
	{
		Context.AddWarning(FText::FromString(TEXT("DungeonAtlas가 비어 있습니다")));
	}
	bool bHasTown = false;
	for (const FTDWorldAnchor& Anchor : HandAuthoredAnchors)
	{
		bHasTown |= Anchor.Kind == ETDWorldAnchorKind::Town;
	}
	if (!bHasTown)
	{
		Context.AddWarning(FText::FromString(TEXT("마을(Town) 앵커가 없습니다")));
	}
	return Result;
}
#endif
