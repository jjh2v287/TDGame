#include "FluidFX/TDFluidWaterMeshEditorLibrary.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidWaterMesh, Log, All);

namespace
{
	const FName WaterGridSlotName(TEXT("WaterSurface"));
	constexpr int32 WaterGridMaxQuadsPerSide = 512;

	FMeshDescription BuildUpFacingGrid(float SizeCm, int32 QuadsPerSide)
	{
		FMeshDescription MeshDescription;
		FStaticMeshAttributes Attributes(MeshDescription);
		Attributes.Register();

		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);

		const FPolygonGroupID PolygonGroup = MeshDescription.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[PolygonGroup] = WaterGridSlotName;

		const int32 VerticesPerSide = QuadsPerSide + 1;
		const float HalfSizeCm = SizeCm * 0.5f;
		const float StepCm = SizeCm / static_cast<float>(QuadsPerSide);
		MeshDescription.ReserveNewVertices(VerticesPerSide * VerticesPerSide);
		MeshDescription.ReserveNewVertexInstances(VerticesPerSide * VerticesPerSide);
		MeshDescription.ReserveNewTriangles(QuadsPerSide * QuadsPerSide * 2);

		TArray<FVertexInstanceID> GridInstances;
		GridInstances.Reserve(VerticesPerSide * VerticesPerSide);
		for (int32 RowIndex = 0; RowIndex < VerticesPerSide; ++RowIndex)
		{
			for (int32 ColumnIndex = 0; ColumnIndex < VerticesPerSide; ++ColumnIndex)
			{
				const FVertexID Vertex = MeshDescription.CreateVertex();
				Positions[Vertex] = FVector3f(-HalfSizeCm + StepCm * RowIndex, -HalfSizeCm + StepCm * ColumnIndex, 0.0f);
				const FVertexInstanceID Instance = MeshDescription.CreateVertexInstance(Vertex);
				Normals[Instance] = FVector3f::UnitZ();
				Tangents[Instance] = FVector3f::UnitX();
				BinormalSigns[Instance] = 1.0f;
				UVs.Set(Instance, 0, FVector2f(static_cast<float>(RowIndex) / QuadsPerSide, static_cast<float>(ColumnIndex) / QuadsPerSide));
				GridInstances.Add(Instance);
			}
		}

		const auto InstanceAt = [&GridInstances, VerticesPerSide](int32 RowIndex, int32 ColumnIndex)
		{
			return GridInstances[RowIndex * VerticesPerSide + ColumnIndex];
		};
		for (int32 RowIndex = 0; RowIndex < QuadsPerSide; ++RowIndex)
		{
			for (int32 ColumnIndex = 0; ColumnIndex < QuadsPerSide; ++ColumnIndex)
			{
				MeshDescription.CreateTriangle(PolygonGroup, { InstanceAt(RowIndex, ColumnIndex), InstanceAt(RowIndex, ColumnIndex + 1), InstanceAt(RowIndex + 1, ColumnIndex) });
				MeshDescription.CreateTriangle(PolygonGroup, { InstanceAt(RowIndex + 1, ColumnIndex), InstanceAt(RowIndex, ColumnIndex + 1), InstanceAt(RowIndex + 1, ColumnIndex + 1) });
			}
		}

		return MeshDescription;
	}
}

UStaticMesh* UTDFluidWaterMeshEditorLibrary::CreateGridStaticMesh(const FString& PackagePath, const FString& AssetName, float SizeCm, int32 QuadsPerSide, UMaterialInterface* SlotMaterial)
{
	if (AssetName.IsEmpty() || SizeCm <= UE_KINDA_SMALL_NUMBER || QuadsPerSide < 1 || QuadsPerSide > WaterGridMaxQuadsPerSide)
	{
		UE_LOG(LogTDFluidWaterMesh, Error, TEXT("Water grid needs a name, a positive size and 1..%d quads per side"), WaterGridMaxQuadsPerSide);
		return nullptr;
	}

	FString PackageRoot = PackagePath;
	PackageRoot.RemoveFromEnd(TEXT("/"));
	const FString LongPackageName = PackageRoot / AssetName;
	if (!FPackageName::IsValidLongPackageName(LongPackageName))
	{
		UE_LOG(LogTDFluidWaterMesh, Error, TEXT("Invalid package name %s"), *LongPackageName);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*LongPackageName);
	if (!Package)
	{
		UE_LOG(LogTDFluidWaterMesh, Error, TEXT("Package %s is unavailable"), *LongPackageName);
		return nullptr;
	}

	if (UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), Package, *AssetName))
	{
		ExistingObject->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		ExistingObject->ClearFlags(RF_Public | RF_Standalone);
	}

	UStaticMesh* GridMesh = NewObject<UStaticMesh>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	GridMesh->GetStaticMaterials().Reset();
	GridMesh->GetStaticMaterials().Add(FStaticMaterial(SlotMaterial, WaterGridSlotName, WaterGridSlotName));
	GridMesh->SetNumSourceModels(1);
	FStaticMeshSourceModel& SourceModel = GridMesh->GetSourceModel(0);
	SourceModel.BuildSettings.bRecomputeNormals = false;
	SourceModel.BuildSettings.bRecomputeTangents = false;
	SourceModel.BuildSettings.bGenerateLightmapUVs = false;
	SourceModel.BuildSettings.bRemoveDegenerates = false;

	FMeshDescription* MeshDescription = GridMesh->CreateMeshDescription(0, BuildUpFacingGrid(SizeCm, QuadsPerSide));
	if (!MeshDescription)
	{
		UE_LOG(LogTDFluidWaterMesh, Error, TEXT("Could not store the mesh description of %s"), *LongPackageName);
		return nullptr;
	}

	GridMesh->CommitMeshDescription(0);
	GridMesh->Build(true);
	GridMesh->PostEditChange();
	GridMesh->MarkPackageDirty();
	return GridMesh;
}
