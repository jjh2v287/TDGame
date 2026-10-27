#if WITH_DEV_AUTOMATION_TESTS

#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidHeightCacheAsset.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "Misc/AutomationTest.h"

namespace TDFluidHeightCacheTesting
{
	struct FHeightCacheTestWorld
	{
		FHeightCacheTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidHeightCacheWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FHeightCacheTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void TickSteps(int32 StepCount)
		{
			for (int32 Step = 0; Step < StepCount; ++Step)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, 0.05f);
			}
		}

		UWorld* World = nullptr;
	};

	AActor* SpawnBlock(UWorld* World, UStaticMesh* Mesh, const FVector& Center, const FRotator& Rotation, const FVector& Scale)
	{
		AActor* Block = World->SpawnActor<AActor>();
		UStaticMeshComponent* BlockMesh = NewObject<UStaticMeshComponent>(Block);
		Block->AddInstanceComponent(BlockMesh);
		Block->SetRootComponent(BlockMesh);
		BlockMesh->SetMobility(EComponentMobility::Static);
		BlockMesh->SetStaticMesh(Mesh);
		BlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BlockMesh->SetCollisionObjectType(ECC_WorldStatic);
		BlockMesh->SetCollisionResponseToAllChannels(ECR_Block);
		BlockMesh->SetWorldTransform(FTransform(Rotation, Center, Scale));
		BlockMesh->RegisterComponent();
		return Block;
	}

	float PlateauTerrainHeightCm(const FVector2D& PointCm)
	{
		const float PlateauLiftCm = PointCm.X > 500.0 && PointCm.Y > 500.0 ? 1000.0f : 0.0f;
		return static_cast<float>(0.1 * PointCm.X) + PlateauLiftCm;
	}

	UTDFluidHeightCacheAsset* MakePlateauCache()
	{
		constexpr int32 CacheResolution = 120;
		constexpr float CacheTexelCm = 100.0f;
		const FVector2D CacheOriginCm(-6000.0, -6000.0);
		TArray<float> HeightsCm;
		TArray<bool> HitMask;
		for (int32 CellY = 0; CellY < CacheResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < CacheResolution; ++CellX)
			{
				const FVector2D TexelCenter = CacheOriginCm + FVector2D((CellX + 0.5) * CacheTexelCm, (CellY + 0.5) * CacheTexelCm);
				HeightsCm.Add(PlateauTerrainHeightCm(TexelCenter));
				HitMask.Add(true);
			}
		}
		UTDFluidHeightCacheAsset* Cache = NewObject<UTDFluidHeightCacheAsset>(GetTransientPackage());
		Cache->SetEncodedFromHeights(CacheOriginCm, CacheResolution, CacheTexelCm, HeightsCm, HitMask);
		return Cache;
	}

	bool HaveSameBits(float Left, float Right)
	{
		return FMemory::Memcmp(&Left, &Right, sizeof(float)) == 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHeightCacheEncodeSampleBakeTest,
	"TDGame.FluidFX.HeightCache.EncodeSampleBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHeightCacheEncodeSampleBakeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeightCacheTesting;

	const float MinZCm = -250.0f;
	const float RangeCm = 100000.0f;
	const float AllowedErrorCm = UTDFluidHeightCacheAsset::GetQuantizationErrorCm(RangeCm) + 0.01f;
	for (const float HeightCm : { -250.0f, -249.99f, 0.0f, 1234.567f, 50000.25f, 99749.9f, 99750.0f })
	{
		const uint16 Encoded = UTDFluidHeightCacheAsset::EncodeHeight(HeightCm, MinZCm, RangeCm);
		const float Decoded = UTDFluidHeightCacheAsset::DecodeHeight(Encoded, MinZCm, RangeCm);
		TestTrue(*FString::Printf(TEXT("Round trip of %.3f stays within the quantization error (%.4f)"), HeightCm, Decoded - HeightCm), FMath::Abs(Decoded - HeightCm) <= AllowedErrorCm);
		TestTrue(TEXT("A hit never encodes as the miss value"), Encoded != UTDFluidHeightCacheAsset::MissEncodedHeight);
	}
	TestEqual(TEXT("Below the range clamps to the first step"), static_cast<int32>(UTDFluidHeightCacheAsset::EncodeHeight(-1000.0f, MinZCm, RangeCm)), 1);
	TestEqual(TEXT("Above the range clamps to the last step"), static_cast<int32>(UTDFluidHeightCacheAsset::EncodeHeight(200000.0f, MinZCm, RangeCm)), 65535);

	UTDFluidHeightCacheAsset* SmallCache = NewObject<UTDFluidHeightCacheAsset>(GetTransientPackage());
	TArray<float> SmallHeights;
	TArray<bool> SmallHits;
	for (int32 CellY = 0; CellY < 4; ++CellY)
	{
		for (int32 CellX = 0; CellX < 4; ++CellX)
		{
			SmallHeights.Add(10.0f * CellX + 5.0f * CellY);
			SmallHits.Add(!(CellX == 3 && CellY == 3));
		}
	}
	SmallCache->SetEncodedFromHeights(FVector2D(-200.0, -200.0), 4, 100.0f, SmallHeights, SmallHits);
	const float SmallErrorCm = UTDFluidHeightCacheAsset::GetQuantizationErrorCm(SmallCache->EncodingRangeCm) + 1e-3f;
	double SampledCm = 0.0;
	TestTrue(TEXT("A texel center inside the cache samples"), SmallCache->SampleHeightCm(FVector2D(-150.0, -150.0), SampledCm));
	TestTrue(TEXT("A texel center returns its own height"), FMath::Abs(SampledCm - 0.0) <= SmallErrorCm);
	TestTrue(TEXT("Halfway between texel centers samples"), SmallCache->SampleHeightCm(FVector2D(-100.0, -150.0), SampledCm));
	TestTrue(*FString::Printf(TEXT("Halfway between texel centers interpolates bilinearly (%.4f)"), SampledCm), FMath::Abs(SampledCm - 5.0) <= SmallErrorCm);
	TestFalse(TEXT("Outside the cache does not sample"), SmallCache->SampleHeightCm(FVector2D(-201.0, 0.0), SampledCm));
	TestFalse(TEXT("Past the far edge does not sample"), SmallCache->SampleHeightCm(FVector2D(0.0, 201.0), SampledCm));
	TestFalse(TEXT("A miss tap does not sample"), SmallCache->SampleHeightCm(FVector2D(150.0, 150.0), SampledCm));
	TestTrue(TEXT("Taps away from the miss sample"), SmallCache->SampleHeightCm(FVector2D(-150.0, 150.0), SampledCm));

	FHeightCacheTestWorld TestWorld;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}
	SpawnBlock(TestWorld.World, CubeMesh, FVector(0.0f, 0.0f, -100.0f), FRotator(10.0f, 0.0f, 0.0f), FVector(80.0f, 80.0f, 1.0f));
	AActor* IgnoredRoof = SpawnBlock(TestWorld.World, CubeMesh, FVector(0.0f, 0.0f, 600.0f), FRotator::ZeroRotator, FVector(4.0f, 4.0f, 1.0f));
	IgnoredRoof->Tags.Add(FName(TEXT("TDFluidTraceIgnore")));
	AActor* WaterSurface = SpawnBlock(TestWorld.World, CubeMesh, FVector(600.0f, 0.0f, 400.0f), FRotator::ZeroRotator, FVector(4.0f, 4.0f, 1.0f));
	WaterSurface->Tags.Add(FName(TEXT("TDFluidWaterSurface")));
	TestWorld.TickSteps(2);

	UTDFluidHeightCacheAsset* BakedCache = NewObject<UTDFluidHeightCacheAsset>(GetTransientPackage());
	if (!TestTrue(TEXT("Baking over the ramp succeeds"), BakedCache->BakeFromLineTraces(TestWorld.World, FVector2D::ZeroVector, FVector2D(2000.0, 2000.0), 100.0f, 2000.0f, -4000.0f)))
	{
		return false;
	}
	TestEqual(TEXT("The bake covers the extent with 100 cm texels"), BakedCache->Resolution, 20);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDFluidHeightCacheTest), false);
	UTDFluidHeightCacheAsset::AddBakeIgnoredActors(*TestWorld.World, QueryParams);
	const FCollisionObjectQueryParams ObjectQueryParams(ECC_WorldStatic);
	const float BakedErrorCm = UTDFluidHeightCacheAsset::GetQuantizationErrorCm(BakedCache->EncodingRangeCm) + 0.01f;
	for (const FVector2D& Probe : { FVector2D(-950.0, -950.0), FVector2D(-50.0, -50.0), FVector2D(50.0, 50.0), FVector2D(650.0, 50.0), FVector2D(950.0, 950.0) })
	{
		FHitResult Hit;
		const bool bDidHit = TestWorld.World->LineTraceSingleByObjectType(Hit, FVector(Probe, 2000.0), FVector(Probe, -4000.0), ObjectQueryParams, QueryParams);
		double CachedCm = 0.0;
		const bool bDidSample = BakedCache->SampleHeightCm(Probe, CachedCm);
		TestTrue(*FString::Printf(TEXT("Probe (%.0f, %.0f) hits the ramp and samples the cache"), Probe.X, Probe.Y), bDidHit && bDidSample);
		TestTrue(*FString::Printf(TEXT("Probe (%.0f, %.0f) cache matches the runtime trace within quantization (%.4f vs %.4f)"), Probe.X, Probe.Y, CachedCm, Hit.ImpactPoint.Z), FMath::Abs(CachedCm - Hit.ImpactPoint.Z) <= BakedErrorCm);
		TestTrue(TEXT("Ignored roof and water surface do not enter the cache"), CachedCm < 300.0);
	}

	const int64 FirstChecksum = BakedCache->SourceChecksum;
	BakedCache->BakeFromLineTraces(TestWorld.World, FVector2D::ZeroVector, FVector2D(2000.0, 2000.0), 100.0f, 2000.0f, -4000.0f);
	TestEqual(TEXT("Baking twice gives the same checksum"), BakedCache->SourceChecksum, FirstChecksum);
	TestFalse(TEXT("A bake without a world fails"), BakedCache->BakeFromLineTraces(nullptr, FVector2D::ZeroVector, FVector2D(2000.0, 2000.0), 100.0f, 2000.0f, -4000.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTerrainCacheSourceAndOpsTest,
	"TDGame.FluidFX.Terrain.CacheSourceAndOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTerrainCacheSourceAndOpsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidHeightCacheTesting;

	const FTDFluidTerrainHeightSettings DefaultSettings;
	for (const float ElevationCm : { -0.0f, 0.0f, 123.456f, -98765.4f })
	{
		TestTrue(*FString::Printf(TEXT("Default ops keep %.3f bit identical"), ElevationCm), HaveSameBits(ATDFluidLiveActor::ApplyTerrainHeightOps(ElevationCm, DefaultSettings), ElevationCm));
	}
	FTDFluidTerrainHeightSettings OpsSettings;
	OpsSettings.bClampHeightLowerValues = true;
	OpsSettings.ClampingValueCm = 10.0f;
	OpsSettings.HeightOffsetCm = 5.0f;
	TestEqual(TEXT("Clamp raises low values and the offset is added"), ATDFluidLiveActor::ApplyTerrainHeightOps(3.0f, OpsSettings), 15.0f);
	TestEqual(TEXT("Values above the clamp only get the offset"), ATDFluidLiveActor::ApplyTerrainHeightOps(20.0f, OpsSettings), 25.0f);

	FHeightCacheTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("Fluid subsystem exists"), Subsystem))
	{
		return false;
	}
	UTDFluidPresetAsset* WaterPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(WaterPreset, FVector::ZeroVector, 120.0f);
	if (!TestNotNull(TEXT("Spawned water fluid actor"), LiveActor))
	{
		return false;
	}
	TestNull(TEXT("An existing kind does not create a volume driver"), LiveActor->VolumeDriver.Get());
	TestWorld.TickSteps(2);
	const int32 Resolution = LiveActor->LiveComponent->GetTerrainSampleResolution();
	if (!TestTrue(TEXT("Terrain grid is initialized"), Resolution > 0))
	{
		return false;
	}

	UTDFluidHeightCacheAsset* PlateauCache = MakePlateauCache();
	LiveActor->TerrainHeightCache = PlateauCache;
	FTDFluidTerrainHeightSettings CacheSettings = LiveActor->GetTerrainHeightSettings();
	CacheSettings.HeightSource = ETDFluidTerrainHeightSource::HeightCache;
	CacheSettings.MaxCacheSamplesPerTick = FMath::DivideAndRoundUp(Resolution * Resolution, 4);
	LiveActor->SetTerrainHeightSettings(CacheSettings);
	LiveActor->bUseAsyncTerrainTraces = false;
	LiveActor->bSampleTerrainElevation = true;

	const int32 TracesBeforeSync = LiveActor->GetTerrainTraceRequestCount();
	const int32 CompletionsBeforeSync = LiveActor->GetTerrainSamplingCompletionCount();
	const int32 CacheSamplesBeforeSync = LiveActor->GetTerrainCacheSampleCount();
	TestWorld.TickSteps(6);
	TestEqual(TEXT("Synchronous cache source fills every cell"), LiveActor->GetTerrainCacheSampleCount() - CacheSamplesBeforeSync, Resolution * Resolution);
	TestEqual(TEXT("Synchronous cache source issues no traces"), LiveActor->GetTerrainTraceRequestCount() - TracesBeforeSync, 0);
	TestEqual(TEXT("Synchronous cache source completes exactly once"), LiveActor->GetTerrainSamplingCompletionCount() - CompletionsBeforeSync, 1);
	FTDFluidCellSample CenterSample;
	LiveActor->LiveComponent->SampleFluidAtWorldLocation(FVector::ZeroVector, CenterSample);
	TestTrue(*FString::Printf(TEXT("Completion computed the cached slope (%.4f)"), CenterSample.TerrainSlope.X), FMath::Abs(CenterSample.TerrainSlope.X - 0.1f) < 0.01f);
	TestEqual(TEXT("Collision mask off sends no terrain boxes"), LiveActor->LiveComponent->GetTerrainCollisionObstacles().Num(), 0);
	TestWorld.TickSteps(4);
	TestEqual(TEXT("A settled cache does not complete again"), LiveActor->GetTerrainSamplingCompletionCount() - CompletionsBeforeSync, 1);

	double QueriedHeightCm = 0.0;
	TestTrue(TEXT("TrySampleTerrainHeightCm reads the cache"), LiveActor->TrySampleTerrainHeightCm(FVector2D(-300.0, 200.0), QueriedHeightCm));
	TestTrue(*FString::Printf(TEXT("TrySampleTerrainHeightCm returns the cached height (%.3f)"), QueriedHeightCm), FMath::Abs(QueriedHeightCm + 30.0) < 0.1);

	LiveActor->bUseAsyncTerrainTraces = true;
	CacheSettings.MaxCacheSamplesPerTick += 1;
	LiveActor->SetTerrainHeightSettings(CacheSettings);
	const int32 TracesBeforeAsync = LiveActor->GetTerrainTraceRequestCount();
	const int32 CompletionsBeforeAsync = LiveActor->GetTerrainSamplingCompletionCount();
	TestWorld.TickSteps(6);
	TestEqual(TEXT("Asynchronous cache source issues no traces"), LiveActor->GetTerrainTraceRequestCount() - TracesBeforeAsync, 0);
	TestEqual(TEXT("Asynchronous cache source completes exactly once"), LiveActor->GetTerrainSamplingCompletionCount() - CompletionsBeforeAsync, 1);

	CacheSettings.bUseHeightAsCollisionMask = true;
	CacheSettings.CollisionHeightAboveBaseCm = 300.0f;
	LiveActor->SetTerrainHeightSettings(CacheSettings);
	TestWorld.TickSteps(6);
	const TArray<FTDFluidSdfObstacle>& TerrainBoxes = LiveActor->LiveComponent->GetTerrainCollisionObstacles();
	TestTrue(*FString::Printf(TEXT("Collision mask builds terrain boxes over the plateau (%d)"), TerrainBoxes.Num()), TerrainBoxes.Num() > 0);
	const double CellSizeCm = LiveActor->LiveComponent->GetSimulationExtentCm().X / Resolution;
	for (const FTDFluidSdfObstacle& Box : TerrainBoxes)
	{
		const bool bIsInsidePlateau = Box.WorldCenter.X - Box.BoxHalfExtentsCm.X > 400.0 - CellSizeCm && Box.WorldCenter.Y - Box.BoxHalfExtentsCm.Y > 400.0 - CellSizeCm;
		TestTrue(TEXT("Every terrain box lies on the plateau"), Box.bIsBox && bIsInsidePlateau);
	}

	CacheSettings.bUseHeightAsCollisionMask = false;
	LiveActor->SetTerrainHeightSettings(CacheSettings);
	TestEqual(TEXT("Turning the collision mask off clears the terrain boxes"), LiveActor->LiveComponent->GetTerrainCollisionObstacles().Num(), 0);
	return true;
}

#endif
