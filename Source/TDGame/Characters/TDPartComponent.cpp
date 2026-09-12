#include "Characters/TDPartComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"

bool UTDPartComponent::IsBoneWeighted(const FName& BoneName) const
{
	const USkeletalMesh* MeshAsset = GetSkeletalMeshAsset();
	if (!MeshAsset)
	{
		return false;
	}

	const FSkeletalMeshRenderData* RenderData = MeshAsset->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.IsEmpty())
	{
		return false;
	}

	const int32 MeshBoneIndex = GetBoneIndex(BoneName);
	if (MeshBoneIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 LODIndex = FMath::Clamp(GetPredictedLODLevel(), 0, RenderData->LODRenderData.Num() - 1);
	const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
	return LODData.ActiveBoneIndices.Contains(IntCastChecked<FBoneIndexType>(MeshBoneIndex));
}
