#include "World/TDWorldGeneration.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> VectorToJson(const FVector& Vector)
	{
		return { MakeShared<FJsonValueNumber>(Vector.X), MakeShared<FJsonValueNumber>(Vector.Y), MakeShared<FJsonValueNumber>(Vector.Z) };
	}

	TArray<TSharedPtr<FJsonValue>> Vector2DToJson(const FVector2D& Vector)
	{
		return { MakeShared<FJsonValueNumber>(Vector.X), MakeShared<FJsonValueNumber>(Vector.Y) };
	}

	template <typename TEnum>
	FString EnumToString(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	TSharedRef<FJsonObject> AnchorToJson(const FTDWorldAnchor& Anchor)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Anchor.AnchorId.ToString());
		Object->SetStringField(TEXT("kind"), EnumToString(Anchor.Kind));
		Object->SetArrayField(TEXT("location_cm"), VectorToJson(Anchor.LocationCm));
		Object->SetNumberField(TEXT("exclusion_cm"), Anchor.ExclusionRadiusCm);
		Object->SetBoolField(TEXT("locked"), Anchor.bLocked);
		return Object;
	}

	TSharedRef<FJsonObject> PoiToJson(const FTDPoiPlacement& Poi)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Poi.PoiId.ToString());
		Object->SetStringField(TEXT("archetype"), Poi.ArchetypeId.ToString());
		Object->SetStringField(TEXT("kind"), EnumToString(Poi.Kind));
		Object->SetArrayField(TEXT("location_cm"), VectorToJson(Poi.LocationCm));
		Object->SetNumberField(TEXT("yaw"), Poi.Yaw);
		Object->SetNumberField(TEXT("exclusion_cm"), Poi.ExclusionRadiusCm);
		Object->SetBoolField(TEXT("locked"), Poi.bLocked);
		Object->SetStringField(TEXT("stable_id"), Poi.StableId.ToString(EGuidFormats::DigitsWithHyphens));
		return Object;
	}

	TSharedRef<FJsonObject> EntranceToJson(const FTDDungeonEntrancePlacement& Entrance)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Entrance.DungeonId.ToString());
		Object->SetArrayField(TEXT("location_cm"), VectorToJson(Entrance.LocationCm));
		Object->SetNumberField(TEXT("yaw"), Entrance.Yaw);
		Object->SetBoolField(TEXT("main"), Entrance.bIsMain);
		Object->SetBoolField(TEXT("locked"), Entrance.bLocked);
		Object->SetStringField(TEXT("stable_id"), Entrance.StableId.ToString(EGuidFormats::DigitsWithHyphens));
		return Object;
	}

	TSharedRef<FJsonObject> RoadToJson(const FTDRoadPolyline& Road)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Road.RoadId.ToString());
		TArray<TSharedPtr<FJsonValue>> Points;
		for (const FVector& Point : Road.PointsCm)
		{
			Points.Add(MakeShared<FJsonValueArray>(VectorToJson(Point)));
		}
		Object->SetArrayField(TEXT("points_cm"), Points);
		Object->SetNumberField(TEXT("width_cm"), Road.WidthCm);
		Object->SetNumberField(TEXT("clearance_cm"), Road.ClearanceCm);
		Object->SetBoolField(TEXT("primary"), Road.bIsPrimary);
		Object->SetBoolField(TEXT("locked"), Road.bLocked);
		Object->SetStringField(TEXT("stable_id"), Road.StableId.ToString(EGuidFormats::DigitsWithHyphens));
		return Object;
	}

	TSharedRef<FJsonObject> ExclusionToJson(const FTDExclusionArea& Exclusion)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetArrayField(TEXT("center_cm"), VectorToJson(Exclusion.CenterCm));
		Object->SetNumberField(TEXT("radius_cm"), Exclusion.RadiusCm);
		Object->SetStringField(TEXT("reason"), Exclusion.Reason.ToString());
		return Object;
	}

	TSharedRef<FJsonObject> ValidationToJson(const FTDValidationReport& Report)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(TEXT("passed"), Report.bPassed);
		Object->SetNumberField(TEXT("score"), Report.Score);
		Object->SetNumberField(TEXT("errors"), Report.CountBySeverity(ETDValidationSeverity::Error));
		Object->SetNumberField(TEXT("warnings"), Report.CountBySeverity(ETDValidationSeverity::Warning));
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FTDValidationItem& Item : Report.Items)
		{
			TSharedRef<FJsonObject> ItemObject = MakeShared<FJsonObject>();
			ItemObject->SetStringField(TEXT("severity"), EnumToString(Item.Severity));
			ItemObject->SetStringField(TEXT("code"), Item.Code.ToString());
			ItemObject->SetStringField(TEXT("message"), Item.Message);
			ItemObject->SetArrayField(TEXT("location_cm"), VectorToJson(Item.WorldLocation));
			ItemObject->SetStringField(TEXT("related"), Item.RelatedId.ToString());
			Items.Add(MakeShared<FJsonValueObject>(ItemObject));
		}
		Object->SetArrayField(TEXT("items"), Items);
		return Object;
	}

	void BuildRoadSettings(const UTDRegionDefinition& Region, FTDRoadGenerator::FSettings& OutSettings)
	{
		OutSettings.SecondaryRoadRatio = Region.SecondaryRoadRatio;
		OutSettings.RoadWidthCm = Region.RoadWidthCm;
		const UTDBiomeDefinition* Biome = Region.Biome.LoadSynchronous();
		if (Biome != nullptr)
		{
			OutSettings.RoadClearanceCm = Biome->RoadClearanceCm;
		}
	}

	void MergeLockedRoads(const FTDLockedLayoutElements& Locked, FTDWorldLayout& Layout)
	{
		for (const FTDRoadPolyline& LockedRoad : Locked.Roads)
		{
			if (LockedRoad.PointsCm.Num() < 2)
			{
				continue;
			}
			FTDRoadPolyline* Existing = Layout.Roads.FindByPredicate([&LockedRoad](const FTDRoadPolyline& Road) { return Road.RoadId == LockedRoad.RoadId; });
			FTDRoadPolyline& Target = Existing ? *Existing : Layout.Roads.AddDefaulted_GetRef();
			Target = LockedRoad;
			Target.bLocked = true;
		}
	}

	void BuildValidatorSettings(const UTDRegionDefinition& Region, FTDWorldValidator::FSettings& OutSettings)
	{
		OutSettings.MinEntranceSpacingCm = Region.MinEntranceSpacingCm;
		OutSettings.MinPoiSpacingCm = Region.MinPoiSpacingCm;
		OutSettings.MaxSlopeDeg = Region.MaxPlacementSlopeDeg;
		if (Region.PoiQuotas.Num() == 0)
		{
			return;
		}
		FIntPoint Range(0, 0);
		for (const FTDPoiQuota& Quota : Region.PoiQuotas)
		{
			Range.X += Quota.CountRange.X;
			Range.Y += FMath::Max(Quota.CountRange.X, Quota.CountRange.Y);
		}
		OutSettings.PoiCountRange = Range;
	}
}

bool FTDWorldGenerator::GenerateAndValidate(const UTDRegionDefinition& Region, const TArray<FTDWorldAnchor>& Anchors, const FBox2D& BoundsCm, int32 Seed, const FTDTerrainSampler* Terrain, const UTDDungeonAtlasDefinition* Atlas, FTDWorldLayout& OutLayout, FString& OutError)
{
	return GenerateAndValidate(Region, Anchors, FTDLockedLayoutElements(), BoundsCm, Seed, Terrain, Atlas, OutLayout, OutError);
}

bool FTDWorldGenerator::GenerateAndValidate(const UTDRegionDefinition& Region, const TArray<FTDWorldAnchor>& Anchors, const FTDLockedLayoutElements& Locked, const FBox2D& BoundsCm, int32 Seed, const FTDTerrainSampler* Terrain, const UTDDungeonAtlasDefinition* Atlas, FTDWorldLayout& OutLayout, FString& OutError)
{
	OutError.Reset();
	const FTDSeedContext SeedContext(Seed);
	FString GraphError;
	if (!FTDWorldGraphGenerator::Generate(Region, Anchors, Locked, BoundsCm, SeedContext, Terrain, FTDWorldGraphGenerator::FSettings(), OutLayout, GraphError))
	{
		OutError = GraphError;
		return false;
	}
	FTDRoadGenerator::FSettings RoadSettings;
	BuildRoadSettings(Region, RoadSettings);
	FString RoadError;
	if (!FTDRoadGenerator::Generate(OutLayout, Terrain, RoadSettings, SeedContext, RoadError))
	{
		OutError = RoadError;
		return false;
	}
	MergeLockedRoads(Locked, OutLayout);
	FTDWorldValidator::FSettings ValidatorSettings;
	BuildValidatorSettings(Region, ValidatorSettings);
	OutLayout.Validation = FTDWorldValidator::Validate(OutLayout, Atlas, Terrain, ValidatorSettings);
	if (!GraphError.IsEmpty())
	{
		OutLayout.Validation.Add(ETDValidationSeverity::Warning, TEXT("placement"), GraphError);
		OutError += GraphError;
	}
	if (!RoadError.IsEmpty())
	{
		OutLayout.Validation.Add(ETDValidationSeverity::Warning, TEXT("roads"), RoadError);
		OutError += RoadError;
	}
	return true;
}

FString FTDWorldGenerator::ToJson(const FTDWorldLayout& Layout)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("seed"), Layout.Seed);
	Root->SetNumberField(TEXT("generator_version"), Layout.GeneratorVersion);
	Root->SetStringField(TEXT("region"), Layout.RegionId.ToString());

	TSharedRef<FJsonObject> Bounds = MakeShared<FJsonObject>();
	Bounds->SetArrayField(TEXT("min"), Vector2DToJson(Layout.BoundsCm.Min));
	Bounds->SetArrayField(TEXT("max"), Vector2DToJson(Layout.BoundsCm.Max));
	Root->SetObjectField(TEXT("bounds_cm"), Bounds);

	TArray<TSharedPtr<FJsonValue>> Anchors;
	for (const FTDWorldAnchor& Anchor : Layout.Anchors)
	{
		Anchors.Add(MakeShared<FJsonValueObject>(AnchorToJson(Anchor)));
	}
	Root->SetArrayField(TEXT("anchors"), Anchors);

	TArray<TSharedPtr<FJsonValue>> Pois;
	for (const FTDPoiPlacement& Poi : Layout.Pois)
	{
		Pois.Add(MakeShared<FJsonValueObject>(PoiToJson(Poi)));
	}
	Root->SetArrayField(TEXT("pois"), Pois);

	TArray<TSharedPtr<FJsonValue>> Entrances;
	for (const FTDDungeonEntrancePlacement& Entrance : Layout.Entrances)
	{
		Entrances.Add(MakeShared<FJsonValueObject>(EntranceToJson(Entrance)));
	}
	Root->SetArrayField(TEXT("entrances"), Entrances);

	TArray<TSharedPtr<FJsonValue>> Roads;
	for (const FTDRoadPolyline& Road : Layout.Roads)
	{
		Roads.Add(MakeShared<FJsonValueObject>(RoadToJson(Road)));
	}
	Root->SetArrayField(TEXT("roads"), Roads);

	TArray<TSharedPtr<FJsonValue>> Exclusions;
	for (const FTDExclusionArea& Exclusion : Layout.Exclusions)
	{
		Exclusions.Add(MakeShared<FJsonValueObject>(ExclusionToJson(Exclusion)));
	}
	Root->SetArrayField(TEXT("exclusions"), Exclusions);
	Root->SetObjectField(TEXT("validation"), ValidationToJson(Layout.Validation));

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

bool FTDWorldGenerator::WriteJsonFile(const FTDWorldLayout& Layout, const FString& FilePath)
{
	if (FilePath.IsEmpty())
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(ToJson(Layout), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
