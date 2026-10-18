#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/StaticMesh.h"
#include "FluidFX/TDFluidFractureEditorLibrary.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "GeometryCollectionProxyData.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const BrickTestMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const BrickTestPackagePath = TEXT("/Game/FluidFX/AutomationTransient");
	constexpr float BrickTestSizeCm = 25.0f;
	constexpr double WallEdgeToleranceCm = 1.0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBrickGeometryCollectionTest,
	"TDGameEditor.FluidFX.BrickGeometryCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBrickGeometryCollectionTest::RunTest(const FString& Parameters)
{
	UStaticMesh* BrickMesh = LoadObject<UStaticMesh>(nullptr, BrickTestMeshPath);
	if (!TestNotNull(TEXT("The engine cube mesh loads"), BrickMesh))
	{
		return false;
	}

	AddExpectedError(TEXT("needs a brick mesh"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("A missing brick mesh creates nothing"), UTDFluidFractureEditorLibrary::CreateBrickGeometryCollection(nullptr, BrickTestPackagePath, TEXT("GC_TD_BrickTestNone"), FIntVector(4, 4, 4), BrickTestSizeCm));

	const FString AssetName = FString::Printf(TEXT("GC_TD_BrickTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UGeometryCollection* BrickCollection = UTDFluidFractureEditorLibrary::CreateBrickGeometryCollection(BrickMesh, BrickTestPackagePath, AssetName, FIntVector(4, 4, 4), BrickTestSizeCm);
	if (!TestNotNull(TEXT("A 4x4x4 brick collection is created"), BrickCollection))
	{
		return false;
	}

	UPackage* BrickPackage = BrickCollection->GetPackage();
	ON_SCOPE_EXIT
	{
		BrickCollection->ClearFlags(RF_Public | RF_Standalone);
		BrickCollection->MarkAsGarbage();
		BrickPackage->MarkAsGarbage();
	};

	const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> Collection = BrickCollection->GetGeometryCollection();
	if (!TestTrue(TEXT("The collection holds geometry data"), Collection.IsValid()))
	{
		return false;
	}

	int32 RigidLeafCount = 0;
	int32 RootCount = 0;
	const int32 TransformCount = Collection->NumElements(FGeometryCollection::TransformGroup);
	for (int32 TransformIndex = 0; TransformIndex < TransformCount; ++TransformIndex)
	{
		RigidLeafCount += Collection->IsRigid(TransformIndex) ? 1 : 0;
		RootCount += Collection->Parent[TransformIndex] == FGeometryCollection::Invalid ? 1 : 0;
	}

	int32 CollisionShapeCount = 0;
	if (const TManagedArray<Chaos::FImplicitObjectPtr>* Implicits = Collection->FindAttribute<Chaos::FImplicitObjectPtr>(FGeometryDynamicCollection::ImplicitsAttribute, FTransformCollection::TransformGroup))
	{
		for (const Chaos::FImplicitObjectPtr& Implicit : *Implicits)
		{
			CollisionShapeCount += Implicit.IsValid() ? 1 : 0;
		}
	}

	TestEqual(TEXT("Every brick and the root carry a collision shape"), CollisionShapeCount, TransformCount);
	TestEqual(TEXT("A 4x4x4 wall has 64 rigid leaves"), RigidLeafCount, 64);
	TestEqual(TEXT("The bricks hang under one root"), RootCount, 1);

	const FBoxSphereBounds WallBounds = Collection->GetBoundingBox();
	const FVector WallSizeCm = WallBounds.GetBox().GetSize();
	TestTrue(TEXT("The wall is a 100 cm cube within 1 cm"),
		FMath::IsNearlyEqual(WallSizeCm.X, 100.0, WallEdgeToleranceCm)
		&& FMath::IsNearlyEqual(WallSizeCm.Y, 100.0, WallEdgeToleranceCm)
		&& FMath::IsNearlyEqual(WallSizeCm.Z, 100.0, WallEdgeToleranceCm));
	TestTrue(TEXT("The wall is centred on its origin within 1 cm"), WallBounds.Origin.Size() <= WallEdgeToleranceCm);
	return true;
}

#endif
