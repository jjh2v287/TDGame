#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/StaticMesh.h"
#include "FluidFX/TDFluidWaterMeshEditorLibrary.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const WaterGridTestPackagePath = TEXT("/Game/FluidFX/AutomationTransient");
	constexpr float WaterGridTestSizeCm = 100.0f;
	constexpr int32 WaterGridTestQuads = 4;
	constexpr double WaterGridBoundsToleranceCm = 0.5;
	constexpr int32 WaterGridRegeneratedQuads = 2;

	int32 CountDownFacingTriangles(const FMeshDescription& MeshDescription)
	{
		FStaticMeshConstAttributes Attributes(MeshDescription);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		int32 DownFacingTriangleCount = 0;
		for (const FTriangleID Triangle : MeshDescription.Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> Corners = MeshDescription.GetTriangleVertices(Triangle);
			const FVector3f FirstEdge = Positions[Corners[1]] - Positions[Corners[0]];
			const FVector3f SecondEdge = Positions[Corners[2]] - Positions[Corners[0]];
			DownFacingTriangleCount += FVector3f::CrossProduct(SecondEdge, FirstEdge).Z <= 0.0f ? 1 : 0;
		}
		return DownFacingTriangleCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterGridMeshTest,
	"TDGameEditor.FluidFX.WaterGridMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterGridMeshTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Water grid needs"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("A zero quad grid creates nothing"), UTDFluidWaterMeshEditorLibrary::CreateGridStaticMesh(WaterGridTestPackagePath, TEXT("SM_TD_WaterGridTestNone"), WaterGridTestSizeCm, 0, nullptr));

	const FString AssetName = FString::Printf(TEXT("SM_TD_WaterGridTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UStaticMesh* GridMesh = UTDFluidWaterMeshEditorLibrary::CreateGridStaticMesh(WaterGridTestPackagePath, AssetName, WaterGridTestSizeCm, WaterGridTestQuads, nullptr);
	if (!TestNotNull(TEXT("A 4x4 water grid is created"), GridMesh))
	{
		return false;
	}

	UPackage* GridPackage = GridMesh->GetPackage();
	ON_SCOPE_EXIT
	{
		GridMesh->ClearFlags(RF_Public | RF_Standalone);
		GridMesh->MarkAsGarbage();
		GridPackage->MarkAsGarbage();
	};

	TestEqual(TEXT("The grid has one material slot so translucent and water passes see it"), GridMesh->GetStaticMaterials().Num(), 1);
	TestEqual(TEXT("The grid keeps one source model"), GridMesh->GetNumSourceModels(), 1);

	const FMeshDescription* MeshDescription = GridMesh->GetMeshDescription(0);
	if (!TestNotNull(TEXT("The grid stores its source mesh description"), MeshDescription))
	{
		return false;
	}

	TestEqual(TEXT("Every triangle faces up under the engine normal rule"), CountDownFacingTriangles(*MeshDescription), 0);
	TestEqual(TEXT("A 4x4 grid has 32 triangles"), MeshDescription->Triangles().Num(), WaterGridTestQuads * WaterGridTestQuads * 2);

	const FStaticMeshRenderData* RenderData = GridMesh->GetRenderData();
	if (!TestTrue(TEXT("The grid has built render data"), RenderData && RenderData->LODResources.Num() > 0))
	{
		return false;
	}

	const FStaticMeshLODResources& LodResources = RenderData->LODResources[0];
	TestEqual(TEXT("Shared grid corners are welded into 25 render vertices"), static_cast<int32>(LodResources.GetNumVertices()), (WaterGridTestQuads + 1) * (WaterGridTestQuads + 1));
	TestTrue(TEXT("Render normals point up"), LodResources.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(0).Z > 0.99f);

	const FBoxSphereBounds GridBounds = GridMesh->GetBounds();
	TestTrue(TEXT("The grid spans 100 cm and is flat"),
		FMath::IsNearlyEqual(GridBounds.BoxExtent.X, 50.0, WaterGridBoundsToleranceCm)
		&& FMath::IsNearlyEqual(GridBounds.BoxExtent.Y, 50.0, WaterGridBoundsToleranceCm)
		&& GridBounds.BoxExtent.Z <= WaterGridBoundsToleranceCm);

	UStaticMesh* RegeneratedMesh = UTDFluidWaterMeshEditorLibrary::CreateGridStaticMesh(WaterGridTestPackagePath, AssetName, WaterGridTestSizeCm, WaterGridRegeneratedQuads, nullptr);
	if (!TestNotNull(TEXT("Regenerating the same asset name creates a grid"), RegeneratedMesh))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		RegeneratedMesh->ClearFlags(RF_Public | RF_Standalone);
		RegeneratedMesh->MarkAsGarbage();
	};

	TestTrue(TEXT("Regeneration replaces the old object instead of reusing it in place"), RegeneratedMesh != GridMesh);
	TestTrue(TEXT("The package now holds the regenerated grid"), FindObject<UStaticMesh>(GridPackage, *AssetName) == RegeneratedMesh);
	TestTrue(TEXT("The replaced grid left the asset package"), GridMesh->GetPackage() != GridPackage);
	const FMeshDescription* RegeneratedDescription = RegeneratedMesh->GetMeshDescription(0);
	if (!TestNotNull(TEXT("The regenerated grid stores its source mesh description"), RegeneratedDescription))
	{
		return false;
	}

	TestEqual(TEXT("The regenerated 2x2 grid has 8 triangles"), RegeneratedDescription->Triangles().Num(), WaterGridRegeneratedQuads * WaterGridRegeneratedQuads * 2);
	TestEqual(TEXT("Every regenerated triangle faces up"), CountDownFacingTriangles(*RegeneratedDescription), 0);
	return true;
}

#endif
