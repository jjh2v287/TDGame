#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidRiverActor.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidWaterActorTestPrivate
{
	constexpr float StepSeconds = 0.05f;
	constexpr int32 TestGridResolution = 32;
	constexpr float TestExtentCm = 600.0f;
	constexpr float RegionBoxHalfHeightCm = 50.0f;
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const PlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* const ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	FName WaterSurfaceTagName()
	{
		return FName(TEXT("TDFluidWaterSurface"));
	}

	FName WaterRegionTagName()
	{
		return FName(TEXT("TDFluidWaterRegion"));
	}

	FName TraceIgnoreTagName()
	{
		return FName(TEXT("TDFluidTraceIgnore"));
	}

	struct FTDFluidWaterActorTestWorld
	{
		FTDFluidWaterActorTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidWaterActorTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidWaterActorTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		void TickWorld(float TotalSeconds)
		{
			for (float Remaining = TotalSeconds; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, StepSeconds);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		UTDFluidLiveSubsystem* GetFluidSubsystem() const
		{
			return World->GetSubsystem<UTDFluidLiveSubsystem>();
		}

		UWorld* World = nullptr;
	};

	AActor* SpawnFollowAnchor(UWorld* World, const FVector& Location)
	{
		AActor* Anchor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Anchor)
		{
			return nullptr;
		}

		USceneComponent* AnchorRoot = NewObject<USceneComponent>(Anchor);
		Anchor->AddInstanceComponent(AnchorRoot);
		Anchor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();
		Anchor->SetActorLocation(Location);
		return Anchor;
	}

	UTDFluidPresetAsset* MakeWaterTestPreset(UObject* Outer, ETDFluidPresetKind Kind)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, Kind);
		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig.GridResolution = TestGridResolution;
		Preset->SimulationConfig.SimExtentCm = FVector2D(TestExtentCm, TestExtentCm);
		Preset->SimulationConfig.PressureIterations = 8;
		return Preset;
	}

	ATDFluidWaterActor* SpawnWaterActor(UWorld* World, AActor* Anchor, UTDFluidPresetAsset* Preset, const FVector& Location)
	{
		ATDFluidWaterActor* WaterActor = World->SpawnActor<ATDFluidWaterActor>(ATDFluidWaterActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!WaterActor)
		{
			return nullptr;
		}

		WaterActor->FollowTargetActor = Anchor;
		WaterActor->ViewLookAheadCm = 0.0f;
		WaterActor->InitializeFromPreset(Preset, 0.0f);
		return WaterActor;
	}

	AActor* SpawnWaterRegionBox(UWorld* World, const FVector2D& Center, float HalfSizeCm, float TopZ)
	{
		AActor* RegionActor = World->SpawnActor<AActor>();
		if (!RegionActor)
		{
			return nullptr;
		}

		UBoxComponent* RegionBox = NewObject<UBoxComponent>(RegionActor);
		RegionActor->AddInstanceComponent(RegionBox);
		RegionActor->SetRootComponent(RegionBox);
		RegionBox->SetBoxExtent(FVector(HalfSizeCm, HalfSizeCm, RegionBoxHalfHeightCm));
		RegionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RegionBox->RegisterComponent();
		RegionActor->SetActorLocation(FVector(Center.X, Center.Y, TopZ - RegionBoxHalfHeightCm));
		RegionActor->Tags.Add(WaterRegionTagName());
		return RegionActor;
	}

	UStaticMeshComponent* SpawnSurfaceMesh(UWorld* World, UStaticMesh* Mesh, UMaterialInterface* Material, const FVector& Location)
	{
		AActor* SurfaceActor = World->SpawnActor<AActor>();
		if (!SurfaceActor)
		{
			return nullptr;
		}

		UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(SurfaceActor);
		SurfaceActor->AddInstanceComponent(MeshComponent);
		SurfaceActor->SetRootComponent(MeshComponent);
		MeshComponent->SetMobility(EComponentMobility::Movable);
		MeshComponent->SetStaticMesh(Mesh);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Material)
		{
			MeshComponent->SetMaterial(0, Material);
		}
		MeshComponent->RegisterComponent();
		SurfaceActor->SetActorLocation(Location);
		SurfaceActor->Tags.Add(WaterSurfaceTagName());
		return MeshComponent;
	}

	AActor* SpawnCollidingBlock(UWorld* World, UStaticMesh* Mesh, const FVector& Center, const FVector& Scale, FName Tag)
	{
		AActor* BlockActor = World->SpawnActor<AActor>();
		if (!BlockActor)
		{
			return nullptr;
		}

		UStaticMeshComponent* BlockMesh = NewObject<UStaticMeshComponent>(BlockActor);
		BlockActor->AddInstanceComponent(BlockMesh);
		BlockActor->SetRootComponent(BlockMesh);
		BlockMesh->SetMobility(EComponentMobility::Movable);
		BlockMesh->SetStaticMesh(Mesh);
		BlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BlockMesh->SetCollisionObjectType(ECC_WorldStatic);
		BlockMesh->SetCollisionResponseToAllChannels(ECR_Block);
		BlockMesh->SetWorldTransform(FTransform(FRotator::ZeroRotator, Center, Scale));
		BlockMesh->RegisterComponent();
		if (!Tag.IsNone())
		{
			BlockActor->Tags.Add(Tag);
		}
		return BlockActor;
	}

	FTDFluidCellSample SampleWaterAt(const ATDFluidWaterActor* WaterActor, const FVector2D& WorldXY)
	{
		FTDFluidCellSample Sample;
		WaterActor->LiveComponent->GetSolver().SampleAtWorldPosition(WorldXY, Sample);
		return Sample;
	}

	UMaterialInstanceDynamic* GetSurfaceMaterialInstance(const UStaticMeshComponent* MeshComponent)
	{
		return Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(0));
	}

	struct FTDFluidTestRiverSetup
	{
		TArray<FVector> Points;
		TArray<float> WidthScales;
		bool bIsLinear = true;
		float WidthCm = 800.0f;
		float SpacingCm = 100.0f;
		float SegmentLengthCm = 200.0f;
		UStaticMesh* RibbonMesh = nullptr;
		UMaterialInterface* RibbonMaterial = nullptr;
	};

	ATDFluidRiverActor* SpawnRiver(UWorld* World, const FTDFluidTestRiverSetup& Setup)
	{
		ATDFluidRiverActor* River = World->SpawnActorDeferred<ATDFluidRiverActor>(ATDFluidRiverActor::StaticClass(), FTransform::Identity);
		if (!River)
		{
			return nullptr;
		}

		River->RiverWidthCm = Setup.WidthCm;
		River->GuideSampleSpacingCm = Setup.SpacingCm;
		River->RibbonSegmentLengthCm = Setup.SegmentLengthCm;
		River->bBuildRibbonSurface = Setup.RibbonMesh != nullptr;
		River->RibbonMesh = TSoftObjectPtr<UStaticMesh>(Setup.RibbonMesh);
		River->RibbonMaterial = TSoftObjectPtr<UMaterialInterface>(Setup.RibbonMaterial);

		USplineComponent* Spline = River->RiverSpline;
		Spline->ClearSplinePoints(false);
		for (int32 PointIndex = 0; PointIndex < Setup.Points.Num(); ++PointIndex)
		{
			Spline->AddSplinePoint(Setup.Points[PointIndex], ESplineCoordinateSpace::World, false);
			if (Setup.bIsLinear)
			{
				Spline->SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
			}
			if (Setup.WidthScales.IsValidIndex(PointIndex))
			{
				Spline->SetScaleAtSplinePoint(PointIndex, FVector(1.0, Setup.WidthScales[PointIndex], 1.0), false);
			}
		}
		Spline->UpdateSpline();

		River->FinishSpawning(FTransform::Identity);
		return River;
	}

	FTDFluidTestRiverSetup MakeStraightRiverSetup(float LengthCm, float EndWidthScale)
	{
		FTDFluidTestRiverSetup Setup;
		Setup.Points = { FVector(0.0, 0.0, 50.0), FVector(LengthCm, 0.0, 50.0) };
		Setup.WidthScales = { 1.0f, EndWidthScale };
		return Setup;
	}

	float MeasureDepthBeneathBlockers(bool bTagBlockers)
	{
		FTDFluidWaterActorTestWorld TestWorld;
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		if (!CubeMesh)
		{
			return -1.0f;
		}

		AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
		SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 250.0f, 0.0f);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(0.0, 0.0, -150.0), FVector(20.0, 20.0, 1.0), NAME_None);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(0.0, 0.0, 60.0), FVector(20.0, 20.0, 1.0), bTagBlockers ? WaterSurfaceTagName() : NAME_None);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(0.0, 0.0, 120.0), FVector(20.0, 20.0, 1.0), bTagBlockers ? TraceIgnoreTagName() : NAME_None);

		ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
		if (!WaterActor)
		{
			return -1.0f;
		}

		TestWorld.TickWorld(0.4f);
		return SampleWaterAt(WaterActor, FVector2D::ZeroVector).WaterDepthCm;
	}

	struct FTDFluidTerrainGridSnapshot
	{
		TArray<FTDFluidCellSample> Samples;
		int32 TicksUntilAllKnown = INDEX_NONE;
	};

	FTDFluidTerrainGridSnapshot CaptureTerrainGridSnapshot(bool bUseAsyncTerrainTraces, int32 TickCount)
	{
		FTDFluidTerrainGridSnapshot Snapshot;
		FTDFluidWaterActorTestWorld TestWorld;
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		if (!CubeMesh)
		{
			return Snapshot;
		}

		AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
		SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 250.0f, 0.0f);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(0.0, 0.0, -150.0), FVector(20.0, 20.0, 1.0), NAME_None);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(0.0, 0.0, -70.0), FVector(2.0, 2.0, 1.0), NAME_None);
		ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
		if (!WaterActor)
		{
			return Snapshot;
		}

		WaterActor->bUseAsyncTerrainTraces = bUseAsyncTerrainTraces;
		const FTDFluidGrid2DSolver& Solver = WaterActor->LiveComponent->GetSolver();
		for (int32 Tick = 1; Tick <= TickCount; ++Tick)
		{
			TestWorld.TickWorld(StepSeconds);
			if (Snapshot.TicksUntilAllKnown == INDEX_NONE && Solver.IsTerrainSamplingCompleteForWater())
			{
				Snapshot.TicksUntilAllKnown = Tick;
			}
		}

		for (int32 CellY = 0; CellY < TestGridResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < TestGridResolution; ++CellX)
			{
				Snapshot.Samples.Add(Solver.SampleAtCell(CellX, CellY));
			}
		}
		return Snapshot;
	}

	struct FTDFluidStripSlopeTiming
	{
		int32 FirstInWindowTick = INDEX_NONE;
		int32 SlopeUpdatedTick = INDEX_NONE;
	};

	FTDFluidStripSlopeTiming MeasureStripSlopeTiming()
	{
		constexpr int32 StationaryTickCount = 3;
		constexpr int32 TotalTickCount = 16;
		constexpr double SlabEdgeProbeXCm = 405.0;
		constexpr double SlabEdgeSlopeThreshold = 1.0;
		constexpr float CellSizeCm = TestExtentCm / TestGridResolution;

		FTDFluidStripSlopeTiming Timing;
		FTDFluidWaterActorTestWorld TestWorld;
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		if (!CubeMesh)
		{
			return Timing;
		}

		AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
		SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 250.0f, 0.0f);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(0.0, 0.0, -150.0), FVector(20.0, 20.0, 1.0), NAME_None);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(600.0, 0.0, -70.0), FVector(4.0, 4.0, 1.0), NAME_None);
		ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
		if (!WaterActor)
		{
			return Timing;
		}

		WaterActor->bUseAsyncTerrainTraces = true;
		const FTDFluidGrid2DSolver& Solver = WaterActor->LiveComponent->GetSolver();
		for (int32 Tick = 1; Tick <= TotalTickCount; ++Tick)
		{
			const int32 ShiftCount = FMath::Max(Tick - StationaryTickCount, 0);
			Anchor->SetActorLocation(FVector(CellSizeCm * ShiftCount, 0.0, 0.0));
			TestWorld.TickWorld(StepSeconds);

			FTDFluidCellSample Sample;
			if (!Solver.SampleAtWorldPosition(FVector2D(SlabEdgeProbeXCm, 0.0), Sample))
			{
				continue;
			}

			if (Timing.FirstInWindowTick == INDEX_NONE)
			{
				Timing.FirstInWindowTick = Tick;
			}
			if (Timing.SlopeUpdatedTick == INDEX_NONE && Sample.TerrainSlope.X > SlabEdgeSlopeThreshold)
			{
				Timing.SlopeUpdatedTick = Tick;
			}
		}
		return Timing;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorSharesMaterialTest,
	"TDGame.FluidFX.Water.WaterActor.SharesMIDPerParentMaterial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorSharesMaterialTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
	UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
	UMaterialInterface* DefaultMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	if (!TestNotNull(TEXT("Cube mesh"), CubeMesh) || !TestNotNull(TEXT("Shape material"), ShapeMaterial) || !TestNotNull(TEXT("Default material"), DefaultMaterial))
	{
		return false;
	}
	TestTrue(TEXT("The two parent materials differ"), ShapeMaterial != DefaultMaterial);

	AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
	SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 200.0f, 0.0f);
	ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
	UStaticMeshComponent* FirstMesh = SpawnSurfaceMesh(TestWorld.World, CubeMesh, ShapeMaterial, FVector(100.0, 0.0, 0.0));
	UStaticMeshComponent* SecondMesh = SpawnSurfaceMesh(TestWorld.World, CubeMesh, ShapeMaterial, FVector(-100.0, 0.0, 0.0));
	if (!TestNotNull(TEXT("Water actor"), WaterActor) || !TestNotNull(TEXT("First mesh"), FirstMesh) || !TestNotNull(TEXT("Second mesh"), SecondMesh))
	{
		return false;
	}

	TestWorld.TickWorld(0.2f);

	TestEqual(TEXT("Two meshes with one parent share one material instance"), WaterActor->GetSharedSurfaceMaterialCount(), 1);
	TestEqual(TEXT("Both meshes are driven"), WaterActor->GetDrivenSurfaceCount(), 2);

	UMaterialInstanceDynamic* FirstInstance = GetSurfaceMaterialInstance(FirstMesh);
	UMaterialInstanceDynamic* SecondInstance = GetSurfaceMaterialInstance(SecondMesh);
	if (!TestNotNull(TEXT("First mesh uses a dynamic instance"), FirstInstance) || !TestNotNull(TEXT("Second mesh uses a dynamic instance"), SecondInstance))
	{
		return false;
	}

	TestTrue(TEXT("Both meshes use the same instance"), FirstInstance == SecondInstance);
	TestTrue(TEXT("The instance derives from the original parent"), FirstInstance->Parent == ShapeMaterial);
	TestFalse(TEXT("Water surfaces do not receive decals"), FirstMesh->bReceivesDecals);
	TestFalse(TEXT("The internal single layer surface casts no shadow onto its own bed"), static_cast<bool>(WaterActor->InternalSurfaceMesh->CastShadow));
	TestFalse(TEXT("The flat internal plane stays hidden because tagged surfaces draw the water"), WaterActor->InternalSurfaceMesh->IsVisible());

	const FLinearColor SimCenter = FirstInstance->K2_GetVectorParameterValue(TEXT("SimCenterWorld"));
	const FLinearColor SimExtents = FirstInstance->K2_GetVectorParameterValue(TEXT("SimExtentsCm"));
	TestNearlyEqual(TEXT("SimCenterWorld x"), SimCenter.R, 0.0f, 0.01f);
	TestNearlyEqual(TEXT("SimCenterWorld y"), SimCenter.G, 0.0f, 0.01f);
	TestNearlyEqual(TEXT("SimExtentsCm x"), SimExtents.R, TestExtentCm, 0.01f);

	if (const UTexture2D* WaterSurfaceTexture = WaterActor->LiveComponent->GetWaterSurfaceTexture())
	{
		TestTrue(TEXT("The shared instance receives the water surface texture"), FirstInstance->K2_GetTextureParameterValue(TEXT("WaterSurfaceTexture")) == WaterSurfaceTexture);
	}

	SpawnSurfaceMesh(TestWorld.World, CubeMesh, DefaultMaterial, FVector(0.0, 150.0, 0.0));
	TestWorld.GetFluidSubsystem()->NotifyWaterSurfacesChanged();
	TestWorld.TickWorld(0.1f);

	TestEqual(TEXT("A new parent material adds a second instance"), WaterActor->GetSharedSurfaceMaterialCount(), 2);
	TestEqual(TEXT("Three meshes are driven"), WaterActor->GetDrivenSurfaceCount(), 3);
	TestTrue(TEXT("The first two meshes keep the shared instance"), GetSurfaceMaterialInstance(FirstMesh) == FirstInstance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorMaterialParametersTest,
	"TDGame.FluidFX.Water.WaterActor.DrivesWaterMaterialParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorMaterialParametersTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
	UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
	if (!TestNotNull(TEXT("Cube mesh"), CubeMesh) || !TestNotNull(TEXT("Shape material"), ShapeMaterial))
	{
		return false;
	}

	AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector(0.0, 0.0, 40.0));
	SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 200.0f, 40.0f);
	UTDFluidPresetAsset* Preset = MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::Sea);
	ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, Preset, FVector(0.0, 0.0, 40.0));
	UStaticMeshComponent* SurfaceMesh = SpawnSurfaceMesh(TestWorld.World, CubeMesh, ShapeMaterial, FVector(0.0, 0.0, 40.0));
	if (!TestNotNull(TEXT("Water actor"), WaterActor) || !TestNotNull(TEXT("Surface mesh"), SurfaceMesh))
	{
		return false;
	}

	TestWorld.TickWorld(0.3f);

	UMaterialInstanceDynamic* SurfaceInstance = GetSurfaceMaterialInstance(SurfaceMesh);
	if (!TestNotNull(TEXT("Surface instance"), SurfaceInstance))
	{
		return false;
	}

	const FTDFluidSimulationConfig& Config = Preset->SimulationConfig;
	TestNearlyEqual(TEXT("WaterDepthReferenceCm"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("WaterDepthReferenceCm")), Config.WaterDepthReferenceCm, 0.01f);
	TestNearlyEqual(TEXT("RippleHeightReferenceCm"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("RippleHeightReferenceCm")), Config.RippleHeightReferenceCm, 0.01f);
	TestNearlyEqual(TEXT("VelocitySpeedReferenceCmPerSec"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("VelocitySpeedReferenceCmPerSec")), FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec(), 0.01f);
	TestNearlyEqual(TEXT("CoastWaveDepthSpacingCm"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("CoastWaveDepthSpacingCm")), Config.CoastWaveDepthSpacingCm, 0.01f);
	TestNearlyEqual(TEXT("CoastWaveMaxDepthCm"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("CoastWaveMaxDepthCm")), Config.CoastWaveMaxDepthCm, 0.01f);
	TestNearlyEqual(TEXT("SwashHeightCm"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("SwashHeightCm")), Config.SwashHeightCm, 0.01f);
	TestNearlyEqual(TEXT("SimGridResolution"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("SimGridResolution")), static_cast<float>(TestGridResolution), 0.01f);
	TestNearlyEqual(TEXT("TerrainReferenceZCm follows the actor height"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("TerrainReferenceZCm")), 40.0f, 0.01f);

	const float CoastWavePhase = SurfaceInstance->K2_GetScalarParameterValue(TEXT("CoastWavePhase"));
	TestTrue(TEXT("CoastWavePhase is a fraction of the wave period"), CoastWavePhase >= 0.0f && CoastWavePhase < 1.0f);
	TestTrue(TEXT("CoastWavePhase advanced with simulation time"), CoastWavePhase > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorRegionTagExcludesRibbonsTest,
	"TDGame.FluidFX.Water.WaterActor.RegionTagExcludesRibbons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorRegionTagExcludesRibbonsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, PlaneMeshPath);
	UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
	if (!TestNotNull(TEXT("Plane mesh"), PlaneMesh) || !TestNotNull(TEXT("Shape material"), ShapeMaterial))
	{
		return false;
	}

	FTDFluidTestRiverSetup Setup;
	Setup.Points = { FVector(-280.0, -150.0, 0.0), FVector(0.0, 150.0, 0.0), FVector(280.0, -150.0, 0.0) };
	Setup.WidthScales = { 1.0f, 1.0f, 1.0f };
	Setup.bIsLinear = false;
	Setup.WidthCm = 100.0f;
	Setup.SpacingCm = 40.0f;
	Setup.SegmentLengthCm = 150.0f;
	Setup.RibbonMesh = PlaneMesh;
	Setup.RibbonMaterial = ShapeMaterial;
	ATDFluidRiverActor* River = SpawnRiver(TestWorld.World, Setup);
	AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
	ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
	if (!TestNotNull(TEXT("River actor"), River) || !TestNotNull(TEXT("Water actor"), WaterActor))
	{
		return false;
	}

	FlushAsyncLoading();
	TestWorld.TickWorld(0.6f);

	TestTrue(TEXT("The river built ribbon segments"), River->GetRibbonSegmentCount() > 0);
	TestEqual(TEXT("Every ribbon segment is a driven surface"), WaterActor->GetDrivenSurfaceCount(), River->GetRibbonSegmentCount());
	TestEqual(TEXT("All ribbon segments share one material instance"), WaterActor->GetSharedSurfaceMaterialCount(), 1);

	const FTDFluidCellSample CornerSample = SampleWaterAt(WaterActor, FVector2D(-270.0, 140.0));
	TestTrue(TEXT("The corner of the ribbon bounding box was sampled"), CornerSample.bIsTerrainKnown);
	TestNearlyEqual(TEXT("The bounding box corner holds no water"), CornerSample.WaterDepthCm, 0.0f, 0.01f);
	TestNearlyEqual(TEXT("The bounding box corner has no flow guide weight"), CornerSample.FlowGuideWeight, 0.0f, 0.001f);

	const FTDFluidCellSample BendSample = SampleWaterAt(WaterActor, FVector2D(0.0, 150.0));
	TestTrue(TEXT("The river bend holds water"), BendSample.WaterDepthCm > 100.0f);
	TestTrue(TEXT("The river bend has flow guide weight"), BendSample.FlowGuideWeight > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorTraceMissDeepFallbackTest,
	"TDGame.FluidFX.Water.WaterActor.TraceMissUsesDeepFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorTraceMissDeepFallbackTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
	SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 200.0f, 0.0f);
	ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
	if (!TestNotNull(TEXT("Water actor"), WaterActor))
	{
		return false;
	}

	WaterActor->DeepWaterFallbackDepthCm = 250.0f;
	TestWorld.TickWorld(0.4f);

	const FTDFluidCellSample InsideSample = SampleWaterAt(WaterActor, FVector2D::ZeroVector);
	TestTrue(TEXT("The cell inside the region was sampled"), InsideSample.bIsTerrainKnown);
	TestNearlyEqual(TEXT("A trace miss inside water gets the deep fallback depth"), InsideSample.WaterDepthCm, 250.0f, 0.5f);

	const FTDFluidCellSample OutsideSample = SampleWaterAt(WaterActor, FVector2D(260.0, 0.0));
	TestTrue(TEXT("The cell outside the region was sampled"), OutsideSample.bIsTerrainKnown);
	TestNearlyEqual(TEXT("A trace miss outside water stays dry"), OutsideSample.WaterDepthCm, 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorTraceIgnoresTaggedActorsTest,
	"TDGame.FluidFX.Water.WaterActor.TraceIgnoresTaggedActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorTraceIgnoresTaggedActorsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	const float DepthWithoutTags = MeasureDepthBeneathBlockers(false);
	const float DepthWithTags = MeasureDepthBeneathBlockers(true);

	TestNearlyEqual(TEXT("Untagged blockers above the water hide the depth"), DepthWithoutTags, 0.0f, 0.01f);
	TestNearlyEqual(TEXT("Tagged surface and trace-ignore blockers leave the ground depth"), DepthWithTags, 100.0f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorAsyncTerrainTracesTest,
	"TDGame.FluidFX.WaterActor.AsyncTerrainTracesMatchSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorAsyncTerrainTracesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	constexpr int32 SnapshotTickCount = 8;
	constexpr int32 MaximumAsyncTicksUntilAllKnown = 3;
	constexpr int32 CellCount = TestGridResolution * TestGridResolution;

	const FTDFluidTerrainGridSnapshot SyncSnapshot = CaptureTerrainGridSnapshot(false, SnapshotTickCount);
	const FTDFluidTerrainGridSnapshot AsyncSnapshot = CaptureTerrainGridSnapshot(true, SnapshotTickCount);
	if (!TestEqual(TEXT("Sync snapshot covers the grid"), SyncSnapshot.Samples.Num(), CellCount) || !TestEqual(TEXT("Async snapshot covers the grid"), AsyncSnapshot.Samples.Num(), CellCount))
	{
		return false;
	}

	TestTrue(TEXT("The synchronous run knows every cell"), SyncSnapshot.TicksUntilAllKnown != INDEX_NONE);
	TestTrue(*FString::Printf(TEXT("The asynchronous run knows every cell within %d ticks (%d)"), MaximumAsyncTicksUntilAllKnown, AsyncSnapshot.TicksUntilAllKnown), AsyncSnapshot.TicksUntilAllKnown != INDEX_NONE && AsyncSnapshot.TicksUntilAllKnown <= MaximumAsyncTicksUntilAllKnown);
	TestTrue(*FString::Printf(TEXT("Asynchronous results arrive at least one tick after the synchronous ones (%d vs %d)"), AsyncSnapshot.TicksUntilAllKnown, SyncSnapshot.TicksUntilAllKnown), AsyncSnapshot.TicksUntilAllKnown >= SyncSnapshot.TicksUntilAllKnown + 1);

	int32 MismatchedCellCount = 0;
	for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
	{
		const FTDFluidCellSample& SyncSample = SyncSnapshot.Samples[CellIndex];
		const FTDFluidCellSample& AsyncSample = AsyncSnapshot.Samples[CellIndex];
		const bool bIsSame = SyncSample.bIsTerrainKnown == AsyncSample.bIsTerrainKnown
			&& FMath::IsNearlyEqual(SyncSample.WaterDepthCm, AsyncSample.WaterDepthCm, 0.01f)
			&& SyncSample.TerrainSlope.Equals(AsyncSample.TerrainSlope, 0.0001);
		MismatchedCellCount += bIsSame ? 0 : 1;
	}
	TestEqual(TEXT("Every cell holds the same terrain depth and slope as the synchronous run"), MismatchedCellCount, 0);

	const int32 CenterCellIndex = (TestGridResolution / 2) * TestGridResolution + TestGridResolution / 2;
	const int32 GroundCellIndex = (TestGridResolution / 2) * TestGridResolution + 4;
	const int32 SlabEdgeCellIndex = (TestGridResolution / 2) * TestGridResolution + 10;
	TestNearlyEqual(TEXT("The raised slab leaves 20 cm of water at the center"), AsyncSnapshot.Samples[CenterCellIndex].WaterDepthCm, 20.0f, 0.5f);
	TestNearlyEqual(TEXT("The ground leaves 100 cm of water away from the slab"), AsyncSnapshot.Samples[GroundCellIndex].WaterDepthCm, 100.0f, 0.5f);
	TestTrue(TEXT("The slab edge produced a terrain slope"), AsyncSnapshot.Samples[SlabEdgeCellIndex].TerrainSlope.X > 1.0);

	constexpr int32 MaximumStripSlopeTicks = 2;
	const FTDFluidStripSlopeTiming StripTiming = MeasureStripSlopeTiming();
	TestTrue(TEXT("The slab edge enters the window while it follows the moving anchor"), StripTiming.FirstInWindowTick != INDEX_NONE);
	TestTrue(
		*FString::Printf(TEXT("During repeated window shifts the slope of a newly sampled strip cell updates within %d ticks (in window at tick %d, slope at tick %d)"), MaximumStripSlopeTicks, StripTiming.FirstInWindowTick, StripTiming.SlopeUpdatedTick),
		StripTiming.SlopeUpdatedTick != INDEX_NONE && StripTiming.SlopeUpdatedTick - StripTiming.FirstInWindowTick <= MaximumStripSlopeTicks);
	return true;
}

namespace TDFluidAsyncTerrainTestPrivate
{
	using namespace TDFluidWaterActorTestPrivate;

	constexpr float AsyncTestCellSizeCm = TestExtentCm / TestGridResolution;
	constexpr float SettleSeconds = 0.4f;
	constexpr int32 MeasuredTickCount = 10;
	constexpr int32 PartialBedTickCount = 8;
	constexpr int32 WakeGridResolution = 48;
	constexpr int32 WakeMeasuredTickCount = 16;
	constexpr float PartialBedDeepFallbackDepthCm = 250.0f;

	struct FTDFluidAsyncTerrainRun
	{
		TArray<FTDFluidCellSample> Samples;
		FVector2D WindowCenterWorld = FVector2D::ZeroVector;
		float WaterLayerDisplayAlpha = 0.0f;
		int32 TicksUntilAllKnown = INDEX_NONE;
		bool bIsSceneReady = false;
	};

	struct FTDFluidAsyncTerrainScene
	{
		AActor* Anchor = nullptr;
		UTDFluidPresetAsset* Preset = nullptr;
		ATDFluidWaterActor* WaterActor = nullptr;
	};

	FTDFluidAsyncTerrainScene SpawnLargeWaterScene(UWorld* World, UStaticMesh* CubeMesh, const FVector& SlabCenter, const FVector& SlabScale, bool bUseAsyncTerrainTraces)
	{
		FTDFluidAsyncTerrainScene Scene;
		Scene.Anchor = SpawnFollowAnchor(World, FVector::ZeroVector);
		SpawnWaterRegionBox(World, FVector2D::ZeroVector, 1000.0f, 0.0f);
		SpawnCollidingBlock(World, CubeMesh, FVector(0.0, 0.0, -150.0), FVector(20.0, 20.0, 1.0), NAME_None);
		SpawnCollidingBlock(World, CubeMesh, SlabCenter, SlabScale, NAME_None);
		Scene.Preset = MakeWaterTestPreset(World, ETDFluidPresetKind::River);
		Scene.WaterActor = SpawnWaterActor(World, Scene.Anchor, Scene.Preset, FVector::ZeroVector);
		if (Scene.WaterActor)
		{
			Scene.WaterActor->bUseAsyncTerrainTraces = bUseAsyncTerrainTraces;
		}
		return Scene;
	}

	void RecordTicksUntilAllKnown(FTDFluidWaterActorTestWorld& TestWorld, ATDFluidWaterActor& WaterActor, int32 TickCount, FTDFluidAsyncTerrainRun& InOutRun)
	{
		const FTDFluidGrid2DSolver& Solver = WaterActor.LiveComponent->GetSolver();
		for (int32 Tick = 1; Tick <= TickCount; ++Tick)
		{
			TestWorld.TickWorld(StepSeconds);
			const bool bIsAllKnown = WaterActor.LiveComponent->IsSimulationAwake() && Solver.IsTerrainSamplingCompleteForWater();
			if (InOutRun.TicksUntilAllKnown == INDEX_NONE && bIsAllKnown)
			{
				InOutRun.TicksUntilAllKnown = Tick;
			}
		}
	}

	void ReadGridSamples(ATDFluidWaterActor& WaterActor, FTDFluidAsyncTerrainRun& InOutRun)
	{
		const FTDFluidGrid2DSolver& Solver = WaterActor.LiveComponent->GetSolver();
		const int32 Resolution = Solver.GetResolution();
		InOutRun.Samples.Reset(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				InOutRun.Samples.Add(Solver.SampleAtCell(CellX, CellY));
			}
		}
		InOutRun.WindowCenterWorld = Solver.GetCenterWorld();
		InOutRun.WaterLayerDisplayAlpha = WaterActor.GetWaterLayerDisplayAlpha();
	}

	int32 CountMismatchedCells(const FTDFluidAsyncTerrainRun& Reference, const FTDFluidAsyncTerrainRun& Candidate, int32& OutFirstMismatchIndex)
	{
		OutFirstMismatchIndex = INDEX_NONE;
		const int32 CellCount = FMath::Min(Reference.Samples.Num(), Candidate.Samples.Num());
		int32 MismatchedCellCount = 0;
		for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			const FTDFluidCellSample& ReferenceSample = Reference.Samples[CellIndex];
			const FTDFluidCellSample& CandidateSample = Candidate.Samples[CellIndex];
			const bool bIsSame = ReferenceSample.bIsTerrainKnown == CandidateSample.bIsTerrainKnown
				&& FMath::IsNearlyEqual(ReferenceSample.WaterDepthCm, CandidateSample.WaterDepthCm, 0.01f)
				&& ReferenceSample.TerrainSlope.Equals(CandidateSample.TerrainSlope, 0.0001);
			if (bIsSame)
			{
				continue;
			}

			if (OutFirstMismatchIndex == INDEX_NONE)
			{
				OutFirstMismatchIndex = CellIndex;
			}
			++MismatchedCellCount;
		}
		return MismatchedCellCount;
	}

	int32 CountKnownCells(const FTDFluidAsyncTerrainRun& Run)
	{
		int32 KnownCellCount = 0;
		for (const FTDFluidCellSample& Sample : Run.Samples)
		{
			KnownCellCount += Sample.bIsTerrainKnown ? 1 : 0;
		}
		return KnownCellCount;
	}

	void ReactivateWaterActor(FTDFluidWaterActorTestWorld& TestWorld, ATDFluidWaterActor& WaterActor, UTDFluidPresetAsset* Preset)
	{
		WaterActor.SetPooledInactive(true);
		WaterActor.SetPooledInactive(false);
		WaterActor.InitializeFromPreset(Preset, 0.0f);
		TestWorld.GetFluidSubsystem()->NotifyWaterSurfacesChanged();
	}

	FTDFluidAsyncTerrainRun CapturePartialBedRun(bool bUseAsyncTerrainTraces)
	{
		FTDFluidAsyncTerrainRun Run;
		FTDFluidWaterActorTestWorld TestWorld;
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		if (!CubeMesh)
		{
			return Run;
		}

		AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
		SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 200.0f, 0.0f);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(-150.0, 0.0, -150.0), FVector(3.0, 20.0, 1.0), NAME_None);
		ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
		if (!WaterActor)
		{
			return Run;
		}

		WaterActor->bUseAsyncTerrainTraces = bUseAsyncTerrainTraces;
		WaterActor->DeepWaterFallbackDepthCm = PartialBedDeepFallbackDepthCm;
		RecordTicksUntilAllKnown(TestWorld, *WaterActor, PartialBedTickCount, Run);
		ReadGridSamples(*WaterActor, Run);
		return Run;
	}

	FTDFluidAsyncTerrainRun CaptureWakeRefillRun(bool bUseAsyncTerrainTraces)
	{
		FTDFluidAsyncTerrainRun Run;
		FTDFluidWaterActorTestWorld TestWorld;
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		if (!CubeMesh)
		{
			return Run;
		}

		AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(0.0, 0.0, -150.0), FVector(20.0, 20.0, 1.0), NAME_None);
		SpawnCollidingBlock(TestWorld.World, CubeMesh, FVector(0.0, 0.0, -70.0), FVector(2.0, 2.0, 1.0), NAME_None);
		UTDFluidPresetAsset* Preset = MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River);
		Preset->SimulationConfig.GridResolution = WakeGridResolution;
		ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, Preset, FVector::ZeroVector);
		if (!WaterActor)
		{
			return Run;
		}

		WaterActor->bUseAsyncTerrainTraces = bUseAsyncTerrainTraces;
		WaterActor->SleepDelaySeconds = 0.1f;
		TestWorld.TickWorld(1.0f);
		Run.bIsSceneReady = !WaterActor->LiveComponent->IsSimulationAwake();

		SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 1000.0f, 0.0f);
		TestWorld.GetFluidSubsystem()->NotifyWaterSurfacesChanged();
		RecordTicksUntilAllKnown(TestWorld, *WaterActor, WakeMeasuredTickCount, Run);
		ReadGridSamples(*WaterActor, Run);
		return Run;
	}

	FTDFluidAsyncTerrainRun CaptureInFlightWindowShiftRun(bool bUseAsyncTerrainTraces)
	{
		constexpr int32 FirstShiftCellCount = 3;
		constexpr int32 SecondShiftCellCount = 2;

		FTDFluidAsyncTerrainRun Run;
		FTDFluidWaterActorTestWorld TestWorld;
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		if (!CubeMesh)
		{
			return Run;
		}

		const FTDFluidAsyncTerrainScene Scene = SpawnLargeWaterScene(TestWorld.World, CubeMesh, FVector(305.0, 0.0, -70.0), FVector(1.1, 20.0, 1.0), bUseAsyncTerrainTraces);
		if (!Scene.WaterActor)
		{
			return Run;
		}

		TestWorld.TickWorld(SettleSeconds);
		Run.bIsSceneReady = Scene.WaterActor->LiveComponent->GetSolver().IsTerrainSamplingCompleteForWater();

		Scene.Anchor->SetActorLocation(FVector(FirstShiftCellCount * AsyncTestCellSizeCm, 0.0, 0.0));
		Scene.WaterActor->StepLiveActor(StepSeconds);
		Scene.Anchor->SetActorLocation(FVector((FirstShiftCellCount + SecondShiftCellCount) * AsyncTestCellSizeCm, 0.0, 0.0));
		RecordTicksUntilAllKnown(TestWorld, *Scene.WaterActor, MeasuredTickCount, Run);
		ReadGridSamples(*Scene.WaterActor, Run);
		return Run;
	}

	FTDFluidAsyncTerrainRun CaptureInFlightReactivationRun(bool bUseAsyncTerrainTraces)
	{
		constexpr int32 ReactivationShiftCellCount = 8;

		FTDFluidAsyncTerrainRun Run;
		FTDFluidWaterActorTestWorld TestWorld;
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		if (!CubeMesh)
		{
			return Run;
		}

		const FTDFluidAsyncTerrainScene Scene = SpawnLargeWaterScene(TestWorld.World, CubeMesh, FVector(0.0, 0.0, -70.0), FVector(2.0, 2.0, 1.0), bUseAsyncTerrainTraces);
		if (!Scene.WaterActor)
		{
			return Run;
		}

		TestWorld.TickWorld(SettleSeconds);
		Run.bIsSceneReady = Scene.WaterActor->LiveComponent->GetSolver().IsTerrainSamplingCompleteForWater();

		ReactivateWaterActor(TestWorld, *Scene.WaterActor, Scene.Preset);
		Scene.WaterActor->StepLiveActor(StepSeconds);
		Scene.Anchor->SetActorLocation(FVector(ReactivationShiftCellCount * AsyncTestCellSizeCm, 0.0, 0.0));
		ReactivateWaterActor(TestWorld, *Scene.WaterActor, Scene.Preset);
		RecordTicksUntilAllKnown(TestWorld, *Scene.WaterActor, MeasuredTickCount, Run);
		ReadGridSamples(*Scene.WaterActor, Run);
		return Run;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorAsyncTerrainMissTest,
	"TDGame.FluidFX.WaterActor.AsyncTerrainMissMatchesSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorAsyncTerrainMissTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;
	using namespace TDFluidAsyncTerrainTestPrivate;

	constexpr int32 MaximumAsyncTicksUntilAllKnown = 3;
	constexpr int32 CellCount = TestGridResolution * TestGridResolution;
	constexpr int32 CenterRowOffset = (TestGridResolution / 2) * TestGridResolution;
	constexpr int32 BedColumn = 10;
	constexpr int32 BedEdgeColumn = 15;
	constexpr int32 MissInsideColumn = 22;
	constexpr int32 RegionEdgeColumn = 26;
	constexpr int32 MissOutsideColumn = 29;

	const FTDFluidAsyncTerrainRun SyncRun = CapturePartialBedRun(false);
	const FTDFluidAsyncTerrainRun AsyncRun = CapturePartialBedRun(true);
	if (!TestEqual(TEXT("Sync run covers the grid"), SyncRun.Samples.Num(), CellCount) || !TestEqual(TEXT("Async run covers the grid"), AsyncRun.Samples.Num(), CellCount))
	{
		return false;
	}

	TestTrue(TEXT("The synchronous run knows every cell"), SyncRun.TicksUntilAllKnown != INDEX_NONE);
	TestTrue(*FString::Printf(TEXT("The asynchronous run knows every cell within %d ticks (%d)"), MaximumAsyncTicksUntilAllKnown, AsyncRun.TicksUntilAllKnown), AsyncRun.TicksUntilAllKnown != INDEX_NONE && AsyncRun.TicksUntilAllKnown <= MaximumAsyncTicksUntilAllKnown);

	int32 FirstMismatchIndex = INDEX_NONE;
	const int32 MismatchedCellCount = CountMismatchedCells(SyncRun, AsyncRun, FirstMismatchIndex);
	TestEqual(*FString::Printf(TEXT("Every cell holds the same knowledge, depth and slope as the synchronous run (first mismatch at cell %d)"), FirstMismatchIndex), MismatchedCellCount, 0);

	const FTDFluidCellSample& BedSample = AsyncRun.Samples[CenterRowOffset + BedColumn];
	const FTDFluidCellSample& BedEdgeSample = AsyncRun.Samples[CenterRowOffset + BedEdgeColumn];
	const FTDFluidCellSample& MissInsideSample = AsyncRun.Samples[CenterRowOffset + MissInsideColumn];
	const FTDFluidCellSample& RegionEdgeSample = AsyncRun.Samples[CenterRowOffset + RegionEdgeColumn];
	const FTDFluidCellSample& MissOutsideSample = AsyncRun.Samples[CenterRowOffset + MissOutsideColumn];
	TestNearlyEqual(TEXT("A traced bed cell inside the region leaves 100 cm of water"), BedSample.WaterDepthCm, 100.0f, 0.5f);
	TestNearlyEqual(TEXT("A missed cell inside the region gets the deep fallback depth"), MissInsideSample.WaterDepthCm, PartialBedDeepFallbackDepthCm, 0.5f);
	TestTrue(TEXT("A missed cell outside the region is known"), MissOutsideSample.bIsTerrainKnown);
	TestNearlyEqual(TEXT("A missed cell outside the region stays dry"), MissOutsideSample.WaterDepthCm, 0.0f, 0.01f);
	TestTrue(*FString::Printf(TEXT("The bed edge drops toward the deep fallback elevation (%.3f)"), BedEdgeSample.TerrainSlope.X), BedEdgeSample.TerrainSlope.X < -1.0);
	TestTrue(*FString::Printf(TEXT("The region edge rises from the deep fallback to the actor height (%.3f)"), RegionEdgeSample.TerrainSlope.X), RegionEdgeSample.TerrainSlope.X > 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorAsyncTerrainWakeRefillTest,
	"TDGame.FluidFX.WaterActor.AsyncTerrainWakeRefillMatchesSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorAsyncTerrainWakeRefillTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;
	using namespace TDFluidAsyncTerrainTestPrivate;

	constexpr int32 MaximumExtraAsyncTicks = 2;
	constexpr int32 CellCount = WakeGridResolution * WakeGridResolution;
	constexpr int32 CenterRowOffset = (WakeGridResolution / 2) * WakeGridResolution;
	constexpr int32 SlabEdgeColumn = 15;

	const FTDFluidAsyncTerrainRun SyncRun = CaptureWakeRefillRun(false);
	const FTDFluidAsyncTerrainRun AsyncRun = CaptureWakeRefillRun(true);
	if (!TestTrue(TEXT("Both water actors fell asleep before the water appeared"), SyncRun.bIsSceneReady && AsyncRun.bIsSceneReady)
		|| !TestEqual(TEXT("Sync run covers the larger grid"), SyncRun.Samples.Num(), CellCount)
		|| !TestEqual(TEXT("Async run covers the larger grid"), AsyncRun.Samples.Num(), CellCount))
	{
		return false;
	}

	TestTrue(*FString::Printf(TEXT("The woken grid needs more than one trace budget (synchronous ticks %d)"), SyncRun.TicksUntilAllKnown), SyncRun.TicksUntilAllKnown >= 2);
	TestTrue(
		*FString::Printf(TEXT("The asynchronous refill knows every cell within %d ticks of the synchronous one (%d vs %d)"), MaximumExtraAsyncTicks, AsyncRun.TicksUntilAllKnown, SyncRun.TicksUntilAllKnown),
		AsyncRun.TicksUntilAllKnown != INDEX_NONE && AsyncRun.TicksUntilAllKnown <= SyncRun.TicksUntilAllKnown + MaximumExtraAsyncTicks);
	TestEqual(TEXT("Every cell is known after the asynchronous refill"), CountKnownCells(AsyncRun), CellCount);

	int32 FirstMismatchIndex = INDEX_NONE;
	const int32 MismatchedCellCount = CountMismatchedCells(SyncRun, AsyncRun, FirstMismatchIndex);
	TestEqual(*FString::Printf(TEXT("Every cell holds the same knowledge, depth and slope as the synchronous run (first mismatch at cell %d)"), FirstMismatchIndex), MismatchedCellCount, 0);

	TestTrue(TEXT("The synchronous water layer fades in after the refill"), SyncRun.WaterLayerDisplayAlpha > 0.0f);
	TestTrue(TEXT("The asynchronous water layer fades in after the refill"), AsyncRun.WaterLayerDisplayAlpha > 0.0f);

	const double SlabEdgeSlope = FMath::Max(AsyncRun.Samples[CenterRowOffset + SlabEdgeColumn].TerrainSlope.X, AsyncRun.Samples[CenterRowOffset + SlabEdgeColumn + 1].TerrainSlope.X);
	TestTrue(*FString::Printf(TEXT("The slab edge produced a terrain slope after the refill (%.3f)"), SlabEdgeSlope), SlabEdgeSlope > 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorAsyncTerrainStaleResultsTest,
	"TDGame.FluidFX.WaterActor.AsyncTerrainStaleResultsAreDiscarded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorAsyncTerrainStaleResultsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;
	using namespace TDFluidAsyncTerrainTestPrivate;

	constexpr int32 MaximumAsyncTicksUntilAllKnown = 4;
	constexpr int32 CellCount = TestGridResolution * TestGridResolution;
	constexpr int32 CenterRowOffset = (TestGridResolution / 2) * TestGridResolution;
	constexpr double ShiftedWindowCenterX = 5.0 * AsyncTestCellSizeCm;
	constexpr double ReactivatedWindowCenterX = 8.0 * AsyncTestCellSizeCm;

	const FTDFluidAsyncTerrainRun SyncShiftRun = CaptureInFlightWindowShiftRun(false);
	const FTDFluidAsyncTerrainRun AsyncShiftRun = CaptureInFlightWindowShiftRun(true);
	if (!TestTrue(TEXT("Both window shift runs finished their first terrain pass"), SyncShiftRun.bIsSceneReady && AsyncShiftRun.bIsSceneReady)
		|| !TestEqual(TEXT("Sync shift run covers the grid"), SyncShiftRun.Samples.Num(), CellCount)
		|| !TestEqual(TEXT("Async shift run covers the grid"), AsyncShiftRun.Samples.Num(), CellCount))
	{
		return false;
	}

	TestNearlyEqual(TEXT("The synchronous window followed both shifts"), SyncShiftRun.WindowCenterWorld.X, ShiftedWindowCenterX, 0.5);
	TestNearlyEqual(TEXT("The asynchronous window followed both shifts"), AsyncShiftRun.WindowCenterWorld.X, ShiftedWindowCenterX, 0.5);
	TestTrue(*FString::Printf(TEXT("Window shift: the synchronous run knows every cell (%d)"), SyncShiftRun.TicksUntilAllKnown), SyncShiftRun.TicksUntilAllKnown != INDEX_NONE);
	TestTrue(
		*FString::Printf(TEXT("Window shift: results still in flight during the second shift land on the moved cells within %d ticks (%d)"), MaximumAsyncTicksUntilAllKnown, AsyncShiftRun.TicksUntilAllKnown),
		AsyncShiftRun.TicksUntilAllKnown != INDEX_NONE && AsyncShiftRun.TicksUntilAllKnown <= MaximumAsyncTicksUntilAllKnown);
	TestTrue(
		*FString::Printf(TEXT("Window shift: the asynchronous results arrive after the synchronous ones (%d vs %d)"), AsyncShiftRun.TicksUntilAllKnown, SyncShiftRun.TicksUntilAllKnown),
		AsyncShiftRun.TicksUntilAllKnown > SyncShiftRun.TicksUntilAllKnown);

	int32 FirstShiftMismatchIndex = INDEX_NONE;
	const int32 ShiftMismatchedCellCount = CountMismatchedCells(SyncShiftRun, AsyncShiftRun, FirstShiftMismatchIndex);
	TestEqual(*FString::Printf(TEXT("Window shift: every cell matches the synchronous run (first mismatch at cell %d)"), FirstShiftMismatchIndex), ShiftMismatchedCellCount, 0);
	TestNearlyEqual(TEXT("Window shift: a strip cell requested before the second shift keeps the slab depth (column 27)"), AsyncShiftRun.Samples[CenterRowOffset + 27].WaterDepthCm, 20.0f, 0.5f);
	TestNearlyEqual(TEXT("Window shift: a strip cell requested before the second shift keeps the slab depth (column 28)"), AsyncShiftRun.Samples[CenterRowOffset + 28].WaterDepthCm, 20.0f, 0.5f);
	TestNearlyEqual(TEXT("Window shift: a strip cell requested after the second shift sees the ground"), AsyncShiftRun.Samples[CenterRowOffset + 31].WaterDepthCm, 100.0f, 0.5f);

	const FTDFluidAsyncTerrainRun SyncReactivationRun = CaptureInFlightReactivationRun(false);
	const FTDFluidAsyncTerrainRun AsyncReactivationRun = CaptureInFlightReactivationRun(true);
	if (!TestTrue(TEXT("Both reactivation runs finished their first terrain pass"), SyncReactivationRun.bIsSceneReady && AsyncReactivationRun.bIsSceneReady)
		|| !TestEqual(TEXT("Sync reactivation run covers the grid"), SyncReactivationRun.Samples.Num(), CellCount)
		|| !TestEqual(TEXT("Async reactivation run covers the grid"), AsyncReactivationRun.Samples.Num(), CellCount))
	{
		return false;
	}

	TestNearlyEqual(TEXT("The synchronous window followed the anchor after reactivation"), SyncReactivationRun.WindowCenterWorld.X, ReactivatedWindowCenterX, 0.5);
	TestNearlyEqual(TEXT("The asynchronous window followed the anchor after reactivation"), AsyncReactivationRun.WindowCenterWorld.X, ReactivatedWindowCenterX, 0.5);
	TestTrue(*FString::Printf(TEXT("Reactivation: the synchronous run knows every cell (%d)"), SyncReactivationRun.TicksUntilAllKnown), SyncReactivationRun.TicksUntilAllKnown != INDEX_NONE);
	TestTrue(
		*FString::Printf(TEXT("Reactivation: the new request knows every cell within %d ticks (%d)"), MaximumAsyncTicksUntilAllKnown, AsyncReactivationRun.TicksUntilAllKnown),
		AsyncReactivationRun.TicksUntilAllKnown != INDEX_NONE && AsyncReactivationRun.TicksUntilAllKnown <= MaximumAsyncTicksUntilAllKnown);
	TestTrue(
		*FString::Printf(TEXT("Reactivation: the asynchronous results arrive after the synchronous ones (%d vs %d)"), AsyncReactivationRun.TicksUntilAllKnown, SyncReactivationRun.TicksUntilAllKnown),
		AsyncReactivationRun.TicksUntilAllKnown > SyncReactivationRun.TicksUntilAllKnown);

	int32 FirstReactivationMismatchIndex = INDEX_NONE;
	const int32 ReactivationMismatchedCellCount = CountMismatchedCells(SyncReactivationRun, AsyncReactivationRun, FirstReactivationMismatchIndex);
	TestEqual(*FString::Printf(TEXT("Reactivation: only the new request shaped the grid, every cell matches the synchronous run (first mismatch at cell %d)"), FirstReactivationMismatchIndex), ReactivationMismatchedCellCount, 0);

	constexpr int32 SlabEdgeColumn = 2;
	const double SlabEdgeSlope = FMath::Max(AsyncReactivationRun.Samples[CenterRowOffset + SlabEdgeColumn].TerrainSlope.X, AsyncReactivationRun.Samples[CenterRowOffset + SlabEdgeColumn + 1].TerrainSlope.X);
	TestTrue(*FString::Printf(TEXT("Reactivation: the slab edge sits where the moved window sees it (%.3f)"), SlabEdgeSlope), SlabEdgeSlope > 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorSleepsWithoutWaterTest,
	"TDGame.FluidFX.Water.WaterActor.SleepsWithoutWaterInWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorSleepsWithoutWaterTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
	UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
	if (!TestNotNull(TEXT("Cube mesh"), CubeMesh) || !TestNotNull(TEXT("Shape material"), ShapeMaterial))
	{
		return false;
	}

	{
		FTDFluidWaterActorTestWorld TestWorld;
		AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
		ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
		UStaticMeshComponent* SurfaceMesh = SpawnSurfaceMesh(TestWorld.World, CubeMesh, ShapeMaterial, FVector(0.0, 400.0, 0.0));
		if (!TestNotNull(TEXT("Water actor"), WaterActor) || !TestNotNull(TEXT("Surface mesh"), SurfaceMesh))
		{
			return false;
		}

		WaterActor->SleepDelaySeconds = 0.5f;
		TestWorld.TickWorld(0.3f);
		TestTrue(TEXT("The water actor is awake before the sleep delay"), WaterActor->LiveComponent->IsSimulationAwake());

		TestWorld.TickWorld(1.0f);
		TestFalse(TEXT("The water actor sleeps after the delay without water"), WaterActor->LiveComponent->IsSimulationAwake());

		UMaterialInstanceDynamic* SurfaceInstance = GetSurfaceMaterialInstance(SurfaceMesh);
		if (TestNotNull(TEXT("Surface instance"), SurfaceInstance))
		{
			TestNearlyEqual(TEXT("A sleeping water layer is hidden in the material"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("FluidWaterLayerActive")), 0.0f, 0.001f);
		}

		SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 200.0f, 0.0f);
		TestWorld.GetFluidSubsystem()->NotifyWaterSurfacesChanged();
		TestWorld.TickWorld(0.5f);
		TestTrue(TEXT("New water in the window wakes the water actor"), WaterActor->LiveComponent->IsSimulationAwake());

		TestWorld.TickWorld(0.5f);
		TestTrue(TEXT("The woken water actor simulates the new region"), SampleWaterAt(WaterActor, FVector2D::ZeroVector).WaterDepthCm > 100.0f);
	}

	{
		FTDFluidWaterActorTestWorld TestWorld;
		AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
		ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
		if (!TestNotNull(TEXT("Second water actor"), WaterActor))
		{
			return false;
		}

		WaterActor->bSleepWhenNoWaterInWindow = false;
		WaterActor->SleepDelaySeconds = 0.2f;
		TestWorld.TickWorld(1.0f);
		TestTrue(TEXT("Sleeping can be disabled"), WaterActor->LiveComponent->IsSimulationAwake());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorLookAheadTest,
	"TDGame.FluidFX.Water.WaterActor.LookAheadOffsetsCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorLookAheadTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector(1000.0, 2000.0, 0.0));
	ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector(1000.0, 2000.0, 0.0));
	if (!TestNotNull(TEXT("Water actor"), WaterActor))
	{
		return false;
	}

	WaterActor->bSleepWhenNoWaterInWindow = false;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();

	TestWorld.TickWorld(0.1f);
	FVector2D Center = WaterActor->LiveComponent->GetSolver().GetCenterWorld();
	TestNearlyEqual(TEXT("Without look ahead the window is centered on the anchor x"), Center.X, 1000.0, 10.0);
	TestNearlyEqual(TEXT("Without look ahead the window is centered on the anchor y"), Center.Y, 2000.0, 10.0);

	WaterActor->ViewLookAheadCm = 190.0f;
	FluidSubsystem->SetPlaneViewRotationOverrideForTests(FRotator(-60.0, 90.0, 0.0));
	TestWorld.TickWorld(0.1f);
	Center = WaterActor->LiveComponent->GetSolver().GetCenterWorld();
	TestNearlyEqual(TEXT("A camera looking along +Y leaves x"), Center.X, 1000.0, 10.0);
	TestNearlyEqual(TEXT("A camera looking along +Y leads y"), Center.Y, 2190.0, 10.0);

	FluidSubsystem->SetPlaneViewRotationOverrideForTests(FRotator(-60.0, 0.0, 0.0));
	TestWorld.TickWorld(0.1f);
	Center = WaterActor->LiveComponent->GetSolver().GetCenterWorld();
	TestNearlyEqual(TEXT("A camera looking along +X leads x"), Center.X, 1190.0, 10.0);
	TestNearlyEqual(TEXT("A camera looking along +X leaves y"), Center.Y, 2000.0, 10.0);

	FluidSubsystem->SetPlaneViewRotationOverrideForTests(FRotator(-60.0, 180.0, 0.0));
	TestWorld.TickWorld(0.1f);
	Center = WaterActor->LiveComponent->GetSolver().GetCenterWorld();
	TestNearlyEqual(TEXT("A camera looking along -X trails x"), Center.X, 810.0, 10.0);

	FluidSubsystem->ClearPlaneViewRotationOverrideForTests();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorFadeInTest,
	"TDGame.FluidFX.Water.WaterActor.FadesInAfterTerrainComplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorFadeInTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
	UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
	if (!TestNotNull(TEXT("Cube mesh"), CubeMesh) || !TestNotNull(TEXT("Shape material"), ShapeMaterial))
	{
		return false;
	}

	AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
	SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 200.0f, 0.0f);
	ATDFluidWaterActor* WaterActor = SpawnWaterActor(TestWorld.World, Anchor, MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), FVector::ZeroVector);
	UStaticMeshComponent* SurfaceMesh = SpawnSurfaceMesh(TestWorld.World, CubeMesh, ShapeMaterial, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Water actor"), WaterActor) || !TestNotNull(TEXT("Surface mesh"), SurfaceMesh))
	{
		return false;
	}

	WaterActor->WaterLayerFadeInSeconds = 1.0f;
	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("The layer starts hidden"), WaterActor->GetWaterLayerDisplayAlpha(), 0.0f, 0.001f);

	UMaterialInstanceDynamic* SurfaceInstance = GetSurfaceMaterialInstance(SurfaceMesh);
	if (!TestNotNull(TEXT("Surface instance"), SurfaceInstance))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The material starts hidden"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("FluidWaterLayerActive")), 0.0f, 0.001f);

	TestWorld.TickWorld(0.5f);
	const float HalfwayAlpha = WaterActor->GetWaterLayerDisplayAlpha();
	TestTrue(TEXT("The layer is fading in half a second after the terrain finished"), HalfwayAlpha > 0.3f && HalfwayAlpha < 0.7f);

	TestWorld.TickWorld(1.0f);
	TestNearlyEqual(TEXT("The layer is fully shown after the fade"), WaterActor->GetWaterLayerDisplayAlpha(), 1.0f, 0.001f);
	TestNearlyEqual(TEXT("The material is fully shown after the fade"), SurfaceInstance->K2_GetScalarParameterValue(TEXT("FluidWaterLayerActive")), 1.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterActorSingleInstanceTest,
	"TDGame.FluidFX.Water.WaterActor.SingleInstancePerWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterActorSingleInstanceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	AActor* Anchor = SpawnFollowAnchor(TestWorld.World, FVector::ZeroVector);
	SpawnWaterRegionBox(TestWorld.World, FVector2D::ZeroVector, 200.0f, 0.0f);
	UTDFluidPresetAsset* Preset = MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River);
	ATDFluidWaterActor* FirstActor = SpawnWaterActor(TestWorld.World, Anchor, Preset, FVector::ZeroVector);
	ATDFluidWaterActor* SecondActor = SpawnWaterActor(TestWorld.World, Anchor, Preset, FVector::ZeroVector);
	if (!TestNotNull(TEXT("First water actor"), FirstActor) || !TestNotNull(TEXT("Second water actor"), SecondActor))
	{
		return false;
	}

	TestWorld.TickWorld(0.4f);

	TestTrue(TEXT("The first water actor keeps simulating"), FirstActor->LiveComponent->IsSimulationAwake());
	TestFalse(TEXT("The second water actor sleeps even after being initialized"), SecondActor->LiveComponent->IsSimulationAwake());
	TestTrue(TEXT("The first water actor produced water"), SampleWaterAt(FirstActor, FVector2D::ZeroVector).WaterDepthCm > 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRiverActorBuildsGuideTest,
	"TDGame.FluidFX.Water.RiverActor.BuildsGuideFromSpline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRiverActorBuildsGuideTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	ATDFluidRiverActor* River = SpawnRiver(TestWorld.World, MakeStraightRiverSetup(1000.0f, 1.0f));
	if (!TestNotNull(TEXT("River actor"), River))
	{
		return false;
	}

	FTDFluidFlowGuide Guide;
	River->BuildFlowGuide(Guide);
	if (!TestEqual(TEXT("A 1000 cm spline sampled every 100 cm has 11 points"), Guide.Points.Num(), 11))
	{
		return false;
	}

	for (int32 PointIndex = 0; PointIndex < Guide.Points.Num(); ++PointIndex)
	{
		const FTDFluidFlowGuidePoint& Point = Guide.Points[PointIndex];
		TestNearlyEqual(TEXT("Point x follows the arc length spacing"), Point.WorldLocation.X, 100.0 * PointIndex, 1.0);
		TestNearlyEqual(TEXT("Point z is the spline height"), Point.WorldLocation.Z, 50.0, 0.01);
		TestNearlyEqual(TEXT("Half width is half the river width at scale 1"), Point.HalfWidthCm, 400.0f, 0.5f);
		TestNearlyEqual(TEXT("Flow speed is the base speed at scale 1"), Point.FlowSpeedCmPerSec, 250.0f, 0.5f);
	}

	River->RiverSpline->SetScaleAtSplinePoint(1, FVector(1.0, 0.5, 1.0));
	River->BuildFlowGuide(Guide);
	if (!TestEqual(TEXT("Rescaling keeps the point count"), Guide.Points.Num(), 11))
	{
		return false;
	}

	TestNearlyEqual(TEXT("The narrow end has half the half width"), Guide.Points.Last().HalfWidthCm, 200.0f, 0.5f);
	TestNearlyEqual(TEXT("The wide end keeps its half width"), Guide.Points[0].HalfWidthCm, 400.0f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRiverActorSpeedScalesWithWidthTest,
	"TDGame.FluidFX.Water.RiverActor.SpeedScalesInverselyWithWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRiverActorSpeedScalesWithWidthTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	ATDFluidRiverActor* River = SpawnRiver(TestWorld.World, MakeStraightRiverSetup(1000.0f, 0.5f));
	if (!TestNotNull(TEXT("River actor"), River))
	{
		return false;
	}

	FTDFluidFlowGuide Guide;
	River->BuildFlowGuide(Guide);
	if (!TestEqual(TEXT("Guide point count"), Guide.Points.Num(), 11))
	{
		return false;
	}

	TestNearlyEqual(TEXT("Half the width doubles the speed"), Guide.Points.Last().FlowSpeedCmPerSec / Guide.Points[0].FlowSpeedCmPerSec, 2.0f, 0.01f);
	const float ExpectedDischarge = Guide.Points[0].HalfWidthCm * Guide.Points[0].FlowSpeedCmPerSec;
	for (const FTDFluidFlowGuidePoint& Point : Guide.Points)
	{
		TestNearlyEqual(TEXT("Width times speed stays constant along the river"), Point.HalfWidthCm * Point.FlowSpeedCmPerSec, ExpectedDischarge, ExpectedDischarge * 0.001f);
	}

	River->bConserveDischargeAcrossWidth = false;
	River->BuildFlowGuide(Guide);
	for (const FTDFluidFlowGuidePoint& Point : Guide.Points)
	{
		TestNearlyEqual(TEXT("Without conservation the speed stays at the base speed"), Point.FlowSpeedCmPerSec, 250.0f, 0.01f);
	}

	River->bConserveDischargeAcrossWidth = true;
	River->RiverSpline->SetScaleAtSplinePoint(1, FVector(1.0, 0.1, 1.0));
	River->BuildFlowGuide(Guide);
	TestNearlyEqual(TEXT("A very narrow end clamps the width scale at 0.25"), Guide.Points.Last().FlowSpeedCmPerSec, 1000.0f, 0.5f);
	TestNearlyEqual(TEXT("The narrow end still has its real half width"), Guide.Points.Last().HalfWidthCm, 40.0f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRiverActorRegistersTest,
	"TDGame.FluidFX.Water.RiverActor.RegistersWithSubsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRiverActorRegistersTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
	if (!TestNotNull(TEXT("Fluid subsystem"), FluidSubsystem))
	{
		return false;
	}

	const int32 VersionBeforeSpawn = FluidSubsystem->GetWaterRegistryVersion();
	ATDFluidRiverActor* River = SpawnRiver(TestWorld.World, MakeStraightRiverSetup(1000.0f, 1.0f));
	if (!TestNotNull(TEXT("River actor"), River))
	{
		return false;
	}

	const int32 VersionAfterSpawn = FluidSubsystem->GetWaterRegistryVersion();
	TestTrue(TEXT("Registering a river bumps the registry version"), VersionAfterSpawn > VersionBeforeSpawn);

	TArray<FTDFluidFlowGuide> Guides;
	FluidSubsystem->CollectFlowGuidesInBounds(FBox2D(FVector2D(400.0, -100.0), FVector2D(600.0, 100.0)), Guides);
	TestEqual(TEXT("A window over the river collects its guide"), Guides.Num(), 1);
	if (!Guides.IsEmpty())
	{
		TestEqual(TEXT("The collected guide is a copy of the built guide"), Guides[0].Points.Num(), 11);
	}

	FluidSubsystem->CollectFlowGuidesInBounds(FBox2D(FVector2D(5000.0, 5000.0), FVector2D(5200.0, 5200.0)), Guides);
	TestEqual(TEXT("A window away from the river collects nothing"), Guides.Num(), 0);

	FluidSubsystem->NotifyWaterSurfacesChanged();
	TestTrue(TEXT("Notifying bumps the registry version"), FluidSubsystem->GetWaterRegistryVersion() > VersionAfterSpawn);

	const int32 VersionBeforeDestroy = FluidSubsystem->GetWaterRegistryVersion();
	River->Destroy();
	TestTrue(TEXT("Unregistering a river bumps the registry version"), FluidSubsystem->GetWaterRegistryVersion() > VersionBeforeDestroy);

	FluidSubsystem->CollectFlowGuidesInBounds(FBox2D(FVector2D(400.0, -100.0), FVector2D(600.0, 100.0)), Guides);
	TestEqual(TEXT("A destroyed river no longer provides a guide"), Guides.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRiverActorRibbonNotDuplicatedTest,
	"TDGame.FluidFX.Water.RiverActor.RibbonNotDuplicatedAfterConstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRiverActorRibbonNotDuplicatedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, PlaneMeshPath);
	UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
	if (!TestNotNull(TEXT("Plane mesh"), PlaneMesh) || !TestNotNull(TEXT("Shape material"), ShapeMaterial))
	{
		return false;
	}

	FTDFluidTestRiverSetup Setup = MakeStraightRiverSetup(1000.0f, 1.0f);
	Setup.SegmentLengthCm = 200.0f;
	Setup.RibbonMesh = PlaneMesh;
	Setup.RibbonMaterial = ShapeMaterial;
	ATDFluidRiverActor* River = SpawnRiver(TestWorld.World, Setup);
	if (!TestNotNull(TEXT("River actor"), River))
	{
		return false;
	}

	const auto CountRibbonSegments = [River]()
	{
		TInlineComponentArray<USplineMeshComponent*> Segments(River);
		return Segments.Num();
	};

	TestEqual(TEXT("Construction with a loaded mesh builds five segments"), CountRibbonSegments(), 5);

	River->OnConstruction(River->GetActorTransform());
	River->OnConstruction(River->GetActorTransform());
	TestEqual(TEXT("Repeated construction does not duplicate segments"), CountRibbonSegments(), 5);
	TestEqual(TEXT("The river tracks exactly its segments"), River->GetRibbonSegmentCount(), 5);

	FlushAsyncLoading();
	TestWorld.TickWorld(0.2f);
	TestEqual(TEXT("The asynchronous load does not duplicate segments"), CountRibbonSegments(), 5);

	TInlineComponentArray<USplineMeshComponent*> Segments(River);
	for (const USplineMeshComponent* Segment : Segments)
	{
		const TArray<float>& PrimitiveData = Segment->GetCustomPrimitiveData().Data;
		if (!TestTrue(TEXT("Each segment carries custom primitive data"), PrimitiveData.IsValidIndex(0)))
		{
			continue;
		}
		TestNearlyEqual(TEXT("Primitive data 0 is the passive flow speed"), PrimitiveData[0], 250.0f, 0.5f);
		TestFalse(TEXT("Ribbon segments have no collision"), Segment->IsCollisionEnabled());
		TestFalse(TEXT("Ribbon segments cast no shadow onto the river bed"), static_cast<bool>(Segment->CastShadow));
	}

	River->bBuildRibbonSurface = false;
	River->OnConstruction(River->GetActorTransform());
	TestEqual(TEXT("Disabling the ribbon removes the segments"), CountRibbonSegments(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidComponentPoolReturnClearsWaterTest,
	"TDGame.FluidFX.Water.Component.PoolReturnClearsWaterSurfaceTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidComponentPoolReturnClearsWaterTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterActorTestPrivate;

	FTDFluidWaterActorTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = TestWorld.World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Live actor"), LiveActor))
	{
		return false;
	}

	LiveActor->InitializeFromPreset(MakeWaterTestPreset(TestWorld.World, ETDFluidPresetKind::River), 0.0f);

	FTDFluidGrid2DSolver& Solver = LiveActor->LiveComponent->GetMutableSolver();
	FTDFluidWaterRegion Region;
	Region.MinXY = FVector2D(-TestExtentCm, -TestExtentCm);
	Region.MaxXY = FVector2D(TestExtentCm, TestExtentCm);
	Region.SurfaceHeightCm = 0.0f;
	Solver.SetWaterRegions({ Region });
	for (int32 CellY = 0; CellY < TestGridResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < TestGridResolution; ++CellX)
		{
			Solver.SetTerrainElevationAtCell(CellX, CellY, -100.0f);
		}
	}
	Solver.ComputeTerrainSlopeFromElevation();

	for (int32 Step = 0; Step < 5; ++Step)
	{
		LiveActor->StepLiveActor(StepSeconds);
	}

	const TArray<FFloat16Color>& WaterPixels = LiveActor->LiveComponent->GetPackedWaterSurfacePixels();
	if (!TestEqual(TEXT("The water surface pixels cover the grid"), WaterPixels.Num(), TestGridResolution * TestGridResolution))
	{
		return false;
	}

	bool bHasDepth = false;
	for (const FFloat16Color& Pixel : WaterPixels)
	{
		bHasDepth = bHasDepth || Pixel.R.GetFloat() > 0.1f;
	}
	TestTrue(TEXT("The simulated water has depth in the output"), bHasDepth);

	LiveActor->SetPooledInactive(true);

	const TArray<FFloat16Color>& ClearedPixels = LiveActor->LiveComponent->GetPackedWaterSurfacePixels();
	if (!TestEqual(TEXT("The cleared water surface pixels still cover the grid"), ClearedPixels.Num(), TestGridResolution * TestGridResolution))
	{
		return false;
	}

	bool bIsCleared = true;
	for (const FFloat16Color& Pixel : ClearedPixels)
	{
		bIsCleared = bIsCleared && Pixel.R.GetFloat() == 0.0f && Pixel.G.GetFloat() == 0.0f && FMath::Abs(Pixel.B.GetFloat() - 0.5f) < 0.001f;
	}
	TestTrue(TEXT("Pool return clears depth, wetness and ripples in the water surface output"), bIsCleared);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterPresetKindsTest,
	"TDGame.FluidFX.Water.Preset.RiverAndSeaEnableWaterLayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterPresetKindsTest::RunTest(const FString& Parameters)
{
	const FTDFluidSimulationConfig RiverConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::River);
	TestTrue(TEXT("River enables the water layer"), RiverConfig.bEnableWaterLayer);
	TestEqual(TEXT("River resolution"), RiverConfig.GridResolution, 192);
	TestNearlyEqual(TEXT("River range x"), RiverConfig.SimExtentCm.X, 3600.0, 0.01);
	TestNearlyEqual(TEXT("River range y"), RiverConfig.SimExtentCm.Y, 3600.0, 0.01);
	TestEqual(TEXT("River pressure iterations"), RiverConfig.PressureIterations, 12);
	TestNearlyEqual(TEXT("River velocity damping"), RiverConfig.VelocityDamping, 0.02f, 0.0001f);
	TestNearlyEqual(TEXT("River density dissipation"), RiverConfig.DensityDissipation, 0.35f, 0.0001f);
	TestNearlyEqual(TEXT("River does not inherit the water advection scale"), RiverConfig.AdvectionSpeedScale, 1.0f, 0.0001f);
	TestNearlyEqual(TEXT("River has no slope gravity"), RiverConfig.TerrainSlopeGravity, 0.0f, 0.0001f);
	TestNearlyEqual(TEXT("River vorticity"), RiverConfig.VorticityStrength, 0.3f, 0.0001f);
	TestNearlyEqual(TEXT("River whitewater from vorticity"), RiverConfig.WhitewaterFromVorticity, 0.3f, 0.0001f);
	TestNearlyEqual(TEXT("River rapids whitewater"), RiverConfig.RapidsWhitewaterPerSec, 2.0f, 0.0001f);
	TestNearlyEqual(TEXT("River ripple wave speed"), RiverConfig.RippleWaveSpeedCmPerSec, 120.0f, 0.0001f);
	TestNearlyEqual(TEXT("River actor ripple impulse"), RiverConfig.ActorBrushRippleImpulseCm, 2.5f, 0.0001f);
	TestNearlyEqual(TEXT("River actor density strength"), RiverConfig.ActorBrushDensityStrength, 0.6f, 0.0001f);
	TestNearlyEqual(TEXT("River flow noise acceleration"), RiverConfig.FlowNoiseAccelerationCmPerSec2, 150.0f, 0.0001f);
	TestNearlyEqual(TEXT("River bank wet height"), RiverConfig.BankWetHeightCm, 10.0f, 0.0001f);
	TestNearlyEqual(TEXT("River swash height"), RiverConfig.SwashHeightCm, 0.0f, 0.0001f);

	const FTDFluidSimulationConfig SeaConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Sea);
	TestTrue(TEXT("Sea enables the water layer"), SeaConfig.bEnableWaterLayer);
	TestEqual(TEXT("Sea resolution"), SeaConfig.GridResolution, 192);
	TestNearlyEqual(TEXT("Sea range x"), SeaConfig.SimExtentCm.X, 3600.0, 0.01);
	TestNearlyEqual(TEXT("Sea ripple wave speed"), SeaConfig.RippleWaveSpeedCmPerSec, 100.0f, 0.0001f);
	TestNearlyEqual(TEXT("Sea swash height"), SeaConfig.SwashHeightCm, 25.0f, 0.0001f);
	TestNearlyEqual(TEXT("Sea wetness drying"), SeaConfig.WetnessDryingSeconds, 8.0f, 0.0001f);
	TestNearlyEqual(TEXT("Sea bank wet height"), SeaConfig.BankWetHeightCm, 0.0f, 0.0001f);

	const FTDFluidSimulationConfig WaterConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Water);
	TestFalse(TEXT("The classic water preset keeps the water layer off"), WaterConfig.bEnableWaterLayer);
	TestEqual(TEXT("The classic water preset keeps its resolution"), WaterConfig.GridResolution, 64);
	TestEqual(TEXT("The classic water preset keeps its pressure iterations"), WaterConfig.PressureIterations, 8);
	TestNearlyEqual(TEXT("The classic water preset keeps its advection scale"), WaterConfig.AdvectionSpeedScale, 1.2f, 0.0001f);
	TestNearlyEqual(TEXT("The classic water preset keeps its slope gravity"), WaterConfig.TerrainSlopeGravity, 600.0f, 0.0001f);

	const UTDFluidPresetAsset* RiverPreset = UTDFluidPresetAsset::CreateTransientPreset(nullptr, ETDFluidPresetKind::River);
	TestTrue(TEXT("River presets paint the water channel"), RiverPreset && RiverPreset->DefaultChannel == ETDFluidBrushChannel::ChannelA_Water);
	return true;
}

#endif
