#include "FluidFX/TDFluidFractureEditorLibrary.h"
#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidFracture, Log, All);

namespace
{
	bool IsValidBrickRequest(const UStaticMesh* BrickMesh, const FString& AssetName, const FIntVector& BrickCount, float BrickSizeCm)
	{
		if (!BrickMesh || AssetName.IsEmpty() || BrickSizeCm <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		return FMath::Min3(BrickCount.X, BrickCount.Y, BrickCount.Z) >= 1;
	}

	TArray<UMaterialInterface*> CollectBrickMaterials(const UStaticMesh& BrickMesh)
	{
		TArray<UMaterialInterface*> Materials;
		for (const FStaticMaterial& StaticMaterial : BrickMesh.GetStaticMaterials())
		{
			UMaterialInterface* MaterialInterface = StaticMaterial.MaterialInterface;
			Materials.Add(MaterialInterface ? MaterialInterface : UMaterial::GetDefaultMaterial(MD_Surface));
		}
		return Materials;
	}

	void AddSingleRootNodeIfRequired(UGeometryCollection& BrickCollection)
	{
		const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> Collection = BrickCollection.GetGeometryCollection();
		if (!Collection.IsValid() || !FGeometryCollectionClusteringUtility::ContainsMultipleRootBones(Collection.Get()))
		{
			return;
		}

		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(Collection.Get());
	}
}

UGeometryCollection* UTDFluidFractureEditorLibrary::CreateBrickGeometryCollection(UStaticMesh* BrickMesh, const FString& PackagePath, const FString& AssetName, FIntVector BrickCount, float BrickSizeCm)
{
	if (!IsValidBrickRequest(BrickMesh, AssetName, BrickCount, BrickSizeCm))
	{
		UE_LOG(LogTDFluidFracture, Error, TEXT("CreateBrickGeometryCollection needs a brick mesh, an asset name, at least one brick per axis and a positive brick size"));
		return nullptr;
	}

	const FBox MeshBounds = BrickMesh->GetBoundingBox();
	const FVector MeshSizeCm = MeshBounds.GetSize();
	if (MeshSizeCm.GetMin() <= UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTDFluidFracture, Error, TEXT("Brick mesh %s has a degenerate bounding box"), *BrickMesh->GetName());
		return nullptr;
	}

	FString PackageRoot = PackagePath;
	PackageRoot.RemoveFromEnd(TEXT("/"));
	const FString LongPackageName = PackageRoot / AssetName;
	if (!FPackageName::IsValidLongPackageName(LongPackageName))
	{
		UE_LOG(LogTDFluidFracture, Error, TEXT("Invalid package name %s"), *LongPackageName);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*LongPackageName);
	if (!Package || FindObject<UObject>(Package, *AssetName))
	{
		UE_LOG(LogTDFluidFracture, Error, TEXT("Package %s is unavailable or already holds %s"), *LongPackageName, *AssetName);
		return nullptr;
	}

	UGeometryCollection* BrickCollection = NewObject<UGeometryCollection>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	const TArray<UMaterialInterface*> Materials = CollectBrickMaterials(*BrickMesh);
	const FVector BrickScale = FVector(BrickSizeCm) / MeshSizeCm;
	const FVector WallCenterOffsetCm = FVector(BrickCount.X, BrickCount.Y, BrickCount.Z) * (BrickSizeCm * 0.5f);
	const FVector MeshCenterCm = MeshBounds.GetCenter();

	for (int32 BrickZ = 0; BrickZ < BrickCount.Z; ++BrickZ)
	{
		for (int32 BrickY = 0; BrickY < BrickCount.Y; ++BrickY)
		{
			for (int32 BrickX = 0; BrickX < BrickCount.X; ++BrickX)
			{
				const FVector BrickCenterCm = (FVector(BrickX, BrickY, BrickZ) + FVector(0.5)) * BrickSizeCm - WallCenterOffsetCm;
				const FTransform BrickTransform(FQuat::Identity, BrickCenterCm - MeshCenterCm * BrickScale, BrickScale);
				FGeometryCollectionEngineConversion::AppendStaticMesh(BrickMesh, Materials, BrickTransform, BrickCollection, false, false, false, false);
			}
		}
	}

	BrickCollection->InitializeMaterials(false);
	AddSingleRootNodeIfRequired(*BrickCollection);
	for (FGeometryCollectionSizeSpecificData& SizeData : BrickCollection->SizeSpecificData)
	{
		for (FGeometryCollectionCollisionTypeData& CollisionShape : SizeData.CollisionShapes)
		{
			CollisionShape.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
		}
	}
	BrickCollection->InvalidateCollection();
	BrickCollection->RebuildRenderData();
	::GeometryCollection::GenerateTemporaryGuids(BrickCollection->GetGeometryCollection().Get(), 0, true);
	BrickCollection->EnsureDataIsCooked(false, false, false, false);
	BrickCollection->MarkPackageDirty();
	return BrickCollection;
}
