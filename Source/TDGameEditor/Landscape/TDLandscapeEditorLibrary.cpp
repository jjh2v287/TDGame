#include "TDLandscapeEditorLibrary.h"

#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeSubsystem.h"
#include "LandscapeUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDLandscape, Log, All);

namespace
{
	bool LoadHeightmapRaw(const FString& Path, int32 ExpectedVertexCount, TArray<uint16>& OutHeights)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			UE_LOG(LogTDLandscape, Error, TEXT("Heightmap raw file not readable: %s"), *Path);
			return false;
		}
		if (Bytes.Num() != ExpectedVertexCount * 2)
		{
			UE_LOG(LogTDLandscape, Error, TEXT("Heightmap raw size %d bytes, expected %d (uint16 x %d)"), Bytes.Num(), ExpectedVertexCount * 2, ExpectedVertexCount);
			return false;
		}
		OutHeights.SetNumUninitialized(ExpectedVertexCount);
		FMemory::Memcpy(OutHeights.GetData(), Bytes.GetData(), Bytes.Num());
		return true;
	}

	bool LoadWeightRaw(const FString& Path, int32 ExpectedVertexCount, TArray<uint8>& OutWeights)
	{
		if (!FFileHelper::LoadFileToArray(OutWeights, *Path))
		{
			UE_LOG(LogTDLandscape, Error, TEXT("Weight raw file not readable: %s"), *Path);
			return false;
		}
		if (OutWeights.Num() != ExpectedVertexCount)
		{
			UE_LOG(LogTDLandscape, Error, TEXT("Weight raw %s size %d, expected %d"), *Path, OutWeights.Num(), ExpectedVertexCount);
			return false;
		}
		return true;
	}

	ULandscapeLayerInfoObject* FindOrCreateLayerInfo(const FName& LayerName, const FString& PackagePath)
	{
		const FString AssetName = FString::Printf(TEXT("LI_TD_%s"), *LayerName.ToString());
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);
		if (ULandscapeLayerInfoObject* Existing = LoadObject<ULandscapeLayerInfoObject>(nullptr, *ObjectPath))
		{
			return Existing;
		}
		return UE::Landscape::CreateTargetLayerInfo(LayerName, PackagePath, AssetName);
	}
}

int32 UTDLandscapeEditorLibrary::GetExpectedVertexCountPerSide(const FTDLandscapeCreateRequest& Request)
{
	const int32 QuadsPerComponent = Request.SectionsPerComponent * Request.QuadsPerSection;
	return Request.ComponentCountX * QuadsPerComponent + 1;
}

ALandscape* UTDLandscapeEditorLibrary::CreateLandscapeFromRawFiles(UObject* WorldContextObject, const FTDLandscapeCreateRequest& Request)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	if (Request.ComponentCountX <= 0 || Request.ComponentCountY <= 0 || Request.QuadsPerSection <= 0 || Request.SectionsPerComponent <= 0)
	{
		UE_LOG(LogTDLandscape, Error, TEXT("Invalid landscape size request"));
		return nullptr;
	}
	if (Request.LayerNames.Num() != Request.LayerRawPaths.Num())
	{
		UE_LOG(LogTDLandscape, Error, TEXT("LayerNames (%d) and LayerRawPaths (%d) count mismatch"), Request.LayerNames.Num(), Request.LayerRawPaths.Num());
		return nullptr;
	}

	const int32 QuadsPerComponent = Request.SectionsPerComponent * Request.QuadsPerSection;
	const int32 SizeX = Request.ComponentCountX * QuadsPerComponent + 1;
	const int32 SizeY = Request.ComponentCountY * QuadsPerComponent + 1;
	const int32 VertexCount = SizeX * SizeY;

	TArray<uint16> Heights;
	if (!Request.HeightmapRawPath.IsEmpty() && !LoadHeightmapRaw(Request.HeightmapRawPath, VertexCount, Heights))
	{
		return nullptr;
	}

	TArray<FLandscapeImportLayerInfo> ImportLayers;
	for (int32 Index = 0; Index < Request.LayerNames.Num(); ++Index)
	{
		const FName LayerName = Request.LayerNames[Index];
		ULandscapeLayerInfoObject* LayerInfo = FindOrCreateLayerInfo(LayerName, Request.LayerInfoPackagePath);
		if (!LayerInfo)
		{
			UE_LOG(LogTDLandscape, Error, TEXT("Layer info creation failed for %s"), *LayerName.ToString());
			return nullptr;
		}
		FLandscapeImportLayerInfo& Layer = ImportLayers.Emplace_GetRef(LayerName);
		Layer.LayerInfo = LayerInfo;
		Layer.SourceFilePath = Request.LayerRawPaths[Index];
		if (!Request.LayerRawPaths[Index].IsEmpty() && !LoadWeightRaw(Request.LayerRawPaths[Index], VertexCount, Layer.LayerData))
		{
			return nullptr;
		}
	}

	const FVector Offset = FTransform(FRotator::ZeroRotator, FVector::ZeroVector, Request.Scale)
		.TransformVector(FVector(-Request.ComponentCountX * QuadsPerComponent / 2.0, -Request.ComponentCountY * QuadsPerComponent / 2.0, 0.0));

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Request.Location + Offset, FRotator::ZeroRotator);
	if (!Landscape)
	{
		return nullptr;
	}
	Landscape->LandscapeMaterial = Request.Material;
	Landscape->SetActorRelativeScale3D(Request.Scale);
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);

	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), ImportLayers);

	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, Request.SectionsPerComponent, Request.QuadsPerSection,
		HeightDataPerLayers, *Request.HeightmapRawPath, MaterialLayerDataPerLayers,
		ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		UE_LOG(LogTDLandscape, Error, TEXT("Landscape info missing after import"));
		return Landscape;
	}
	FActorLabelUtilities::SetActorLabelUnique(Landscape, ALandscape::StaticClass()->GetName());
	LandscapeInfo->UpdateLayerInfoMap(Landscape);

	for (const FLandscapeImportLayerInfo& Layer : ImportLayers)
	{
		Landscape->AddTargetLayer(Layer.LayerInfo->GetLayerName(), FLandscapeTargetLayerSettings(Layer.LayerInfo));
		const int32 LayerInfoIndex = LandscapeInfo->GetLayerInfoIndex(Layer.LayerName);
		if (LayerInfoIndex != INDEX_NONE)
		{
			LandscapeInfo->Layers[LayerInfoIndex].LayerInfoObj = Layer.LayerInfo;
		}
	}

	if (ULandscapeSubsystem* LandscapeSubsystem = World->GetSubsystem<ULandscapeSubsystem>())
	{
		LandscapeSubsystem->ChangeGridSize(LandscapeInfo, Request.WorldPartitionGridSizeInComponents);
	}

	LandscapeInfo->ForceLayersFullUpdate();

	UE_LOG(LogTDLandscape, Log, TEXT("Created landscape %dx%d verts, %d layers, grid size %d"), SizeX, SizeY, ImportLayers.Num(), Request.WorldPartitionGridSizeInComponents);
	return Landscape;
}

bool UTDLandscapeEditorLibrary::TryGetLandscapeHeightAtLocation(ALandscapeProxy* Landscape, FVector Location, float& OutHeight)
{
	OutHeight = 0.0f;
	if (!Landscape)
	{
		return false;
	}
	const TOptional<float> Height = Landscape->GetHeightAtLocation(Location, EHeightfieldSource::Editor);
	if (!Height.IsSet())
	{
		return false;
	}
	OutHeight = Height.GetValue();
	return true;
}

int32 UTDLandscapeEditorLibrary::DestroyAllLandscapeActors(UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return 0;
	}
	TArray<AActor*> ToDestroy;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ToDestroy.Add(*It);
	}
	for (AActor* Actor : ToDestroy)
	{
		World->EditorDestroyActor(Actor, true);
	}
	return ToDestroy.Num();
}
