#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "FluidFX/TDFluidWaterLodGridComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace TDFluidWaterLodGridTestPrivate
{
	constexpr int32 ObserverSampleCount = 50;
	constexpr int32 ObserverSeed = 7177;
	constexpr double SharedEdgeToleranceCm = 0.01;
	constexpr double CounterexampleRangeRatio = 0.35;
	constexpr float WorldStepSeconds = 1.0f / 30.0f;
	const FBox2D CoastCoverageCm(FVector2D(-6400.0, -6400.0), FVector2D(1600.0, 6400.0));
	constexpr double CoastSurfaceHeightCm = 100.0;

	struct FTDFluidLodCellGrid
	{
		FIntPoint FirstCell = FIntPoint::ZeroValue;
		FIntPoint CellCount = FIntPoint::ZeroValue;
		double CellSizeCm = 1.0;
		TArray<int32> Levels;
		TArray<int32> CoverCounts;

		int32 ToIndex(int32 CellX, int32 CellY) const
		{
			if (CellX < FirstCell.X || CellY < FirstCell.Y || CellX >= FirstCell.X + CellCount.X || CellY >= FirstCell.Y + CellCount.Y)
			{
				return INDEX_NONE;
			}
			return (CellY - FirstCell.Y) * CellCount.X + (CellX - FirstCell.X);
		}

		int32 GetLevel(int32 CellX, int32 CellY) const
		{
			const int32 CellIndex = ToIndex(CellX, CellY);
			return CellIndex == INDEX_NONE ? INDEX_NONE : Levels[CellIndex];
		}
	};

	FTDFluidLodCellGrid RasterizePatches(const FTDFluidWaterLodSelectionResult& Result, const FTDFluidWaterLodGridSettings& Settings, const FBox2D& CoverageCm)
	{
		FTDFluidLodCellGrid Grid;
		Grid.CellSizeCm = static_cast<double>(Settings.PatchQuads) * Settings.BaseQuadSizeCm;
		const double RootSizeCm = Grid.CellSizeCm * FMath::Pow(2.0, static_cast<double>(Settings.LevelCount - 1));
		const int32 RootCells = 1 << (Settings.LevelCount - 1);
		const int32 FirstRootX = FMath::FloorToInt32(CoverageCm.Min.X / RootSizeCm) - 1;
		const int32 FirstRootY = FMath::FloorToInt32(CoverageCm.Min.Y / RootSizeCm) - 1;
		const int32 LastRootX = FMath::CeilToInt32(CoverageCm.Max.X / RootSizeCm);
		const int32 LastRootY = FMath::CeilToInt32(CoverageCm.Max.Y / RootSizeCm);
		Grid.FirstCell = FIntPoint(FirstRootX * RootCells, FirstRootY * RootCells);
		Grid.CellCount = FIntPoint((LastRootX - FirstRootX + 1) * RootCells, (LastRootY - FirstRootY + 1) * RootCells);
		Grid.Levels.Init(INDEX_NONE, Grid.CellCount.X * Grid.CellCount.Y);
		Grid.CoverCounts.Init(0, Grid.CellCount.X * Grid.CellCount.Y);
		for (const FTDFluidWaterLodPatch& Patch : Result.Patches)
		{
			const int32 PatchCellX = FMath::RoundToInt32(Patch.MinCornerCm.X / Grid.CellSizeCm);
			const int32 PatchCellY = FMath::RoundToInt32(Patch.MinCornerCm.Y / Grid.CellSizeCm);
			const int32 PatchCells = 1 << Patch.Level;
			for (int32 OffsetY = 0; OffsetY < PatchCells; ++OffsetY)
			{
				for (int32 OffsetX = 0; OffsetX < PatchCells; ++OffsetX)
				{
					const int32 CellIndex = Grid.ToIndex(PatchCellX + OffsetX, PatchCellY + OffsetY);
					if (CellIndex == INDEX_NONE)
					{
						continue;
					}
					Grid.Levels[CellIndex] = Patch.Level;
					++Grid.CoverCounts[CellIndex];
				}
			}
		}
		return Grid;
	}

	TArray<FVector> MakeSeededObservers(const FBox2D& CoverageCm, double SurfaceHeightCm)
	{
		FRandomStream RandomStream(ObserverSeed);
		TArray<FVector> Observers;
		for (int32 ObserverIndex = 0; ObserverIndex < ObserverSampleCount; ++ObserverIndex)
		{
			Observers.Add(FVector(
				RandomStream.FRandRange(CoverageCm.Min.X - 2000.0, CoverageCm.Max.X + 2000.0),
				RandomStream.FRandRange(CoverageCm.Min.Y - 2000.0, CoverageCm.Max.Y + 2000.0),
				SurfaceHeightCm + RandomStream.FRandRange(50.0, 3000.0)));
		}
		return Observers;
	}

	FTDFluidWaterLodSelectionInput MakeCoastInput(const FVector& ObserverWorld)
	{
		FTDFluidWaterLodSelectionInput Input;
		Input.ObserverWorld = ObserverWorld;
		Input.CoverageCm = CoastCoverageCm;
		Input.SurfaceHeightCm = CoastSurfaceHeightCm;
		Input.Settings = FTDFluidWaterLodGridSettings();
		return Input;
	}

	int32 CountSharedEdgeMismatches(const FTDFluidWaterLodSelectionResult& Result, const FTDFluidWaterLodSelectionInput& Input, const FTDFluidWaterLodGridSettings& Settings, int32& OutComparedVertexCount)
	{
		const FTDFluidLodCellGrid Grid = RasterizePatches(Result, Settings, Input.CoverageCm.ExpandBy(static_cast<double>(Settings.CoverageMarginCm)));
		int32 MismatchCount = 0;
		for (const FTDFluidWaterLodPatch& Patch : Result.Patches)
		{
			const double FineSpacingCm = Settings.BaseQuadSizeCm * FMath::Pow(2.0, static_cast<double>(Patch.Level));
			const double CoarseSpacingCm = 2.0 * FineSpacingCm;
			const int32 PatchCellX = FMath::RoundToInt32(Patch.MinCornerCm.X / Grid.CellSizeCm);
			const int32 PatchCellY = FMath::RoundToInt32(Patch.MinCornerCm.Y / Grid.CellSizeCm);
			const int32 PatchCells = 1 << Patch.Level;
			for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
			{
				const bool bIsVerticalEdge = EdgeIndex < 2;
				const bool bIsMaximumSide = EdgeIndex % 2 == 1;
				for (int32 SegmentIndex = 0; SegmentIndex < PatchCells; ++SegmentIndex)
				{
					const int32 NeighborCellX = bIsVerticalEdge ? (bIsMaximumSide ? PatchCellX + PatchCells : PatchCellX - 1) : PatchCellX + SegmentIndex;
					const int32 NeighborCellY = bIsVerticalEdge ? PatchCellY + SegmentIndex : (bIsMaximumSide ? PatchCellY + PatchCells : PatchCellY - 1);
					if (Grid.GetLevel(NeighborCellX, NeighborCellY) != Patch.Level + 1)
					{
						continue;
					}

					const double EdgeLineCm = bIsVerticalEdge
						? Patch.MinCornerCm.X + (bIsMaximumSide ? Patch.SizeCm : 0.0)
						: Patch.MinCornerCm.Y + (bIsMaximumSide ? Patch.SizeCm : 0.0);
					const double SegmentStartCm = (bIsVerticalEdge ? Patch.MinCornerCm.Y : Patch.MinCornerCm.X) + SegmentIndex * Grid.CellSizeCm;
					const int32 SegmentVertexCount = FMath::RoundToInt32(Grid.CellSizeCm / FineSpacingCm);
					for (int32 VertexIndex = 0; VertexIndex <= SegmentVertexCount; ++VertexIndex)
					{
						const double AlongCm = SegmentStartCm + VertexIndex * FineSpacingCm;
						const double CoarseAlongCm = CoarseSpacingCm * FMath::FloorToDouble(AlongCm / CoarseSpacingCm + 1.0e-9);
						const FVector2D FineVertex = bIsVerticalEdge ? FVector2D(EdgeLineCm, AlongCm) : FVector2D(AlongCm, EdgeLineCm);
						const FVector2D CoarseVertex = bIsVerticalEdge ? FVector2D(EdgeLineCm, CoarseAlongCm) : FVector2D(CoarseAlongCm, EdgeLineCm);
						const FVector2D FineMorphed = UTDFluidWaterLodGridComponent::ComputeMorphedVertexXY(FineVertex, FineSpacingCm, Input.ObserverWorld, Input.SurfaceHeightCm, Result.EffectiveBaseRangeCm, Settings);
						const FVector2D CoarseMorphed = UTDFluidWaterLodGridComponent::ComputeMorphedVertexXY(CoarseVertex, CoarseSpacingCm, Input.ObserverWorld, Input.SurfaceHeightCm, Result.EffectiveBaseRangeCm, Settings);
						++OutComparedVertexCount;
						if ((FineMorphed - CoarseMorphed).GetAbsMax() > SharedEdgeToleranceCm)
						{
							++MismatchCount;
						}
					}
				}
			}
		}
		return MismatchCount;
	}

	struct FTDFluidLodTestWorld
	{
		FTDFluidLodTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidWaterLodGridWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidLodTestWorld()
		{
			if (!World)
			{
				return;
			}
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void TickFrames(int32 FrameCount)
		{
			for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, WorldStepSeconds);
			}
		}

		UWorld* World = nullptr;
	};

	struct FTDFluidLodConsoleScope
	{
		explicit FTDFluidLodConsoleScope(int32 Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.FluidFX.WaterLodGrid"));
			if (Variable)
			{
				PreviousValue = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByCode);
			}
		}

		~FTDFluidLodConsoleScope()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = -1;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLodSelectionCoversTest,
	"TDGame.FluidFX.WaterLodGrid.SelectionCoversWithoutGapsOrOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLodSelectionCoversTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterLodGridTestPrivate;

	for (const FVector& ObserverWorld : MakeSeededObservers(CoastCoverageCm, CoastSurfaceHeightCm))
	{
		const FTDFluidWaterLodSelectionInput Input = MakeCoastInput(ObserverWorld);
		const FTDFluidWaterLodGridSettings Settings = UTDFluidWaterLodGridComponent::SanitizeSettings(Input.Settings);
		const FTDFluidWaterLodSelectionResult Result = UTDFluidWaterLodGridComponent::SelectPatches(Input);
		const FTDFluidLodCellGrid Grid = RasterizePatches(Result, Settings, Input.CoverageCm);
		int32 GapCount = 0;
		int32 OverlapCount = 0;
		for (int32 CellY = Grid.FirstCell.Y; CellY < Grid.FirstCell.Y + Grid.CellCount.Y; ++CellY)
		{
			for (int32 CellX = Grid.FirstCell.X; CellX < Grid.FirstCell.X + Grid.CellCount.X; ++CellX)
			{
				const int32 CoverCount = Grid.CoverCounts[Grid.ToIndex(CellX, CellY)];
				const FBox2D CellBox(FVector2D(CellX, CellY) * Grid.CellSizeCm, FVector2D(CellX + 1, CellY + 1) * Grid.CellSizeCm);
				const bool bOverlapsCoverage = CellBox.Min.X < Input.CoverageCm.Max.X && CellBox.Max.X > Input.CoverageCm.Min.X && CellBox.Min.Y < Input.CoverageCm.Max.Y && CellBox.Max.Y > Input.CoverageCm.Min.Y;
				GapCount += bOverlapsCoverage && CoverCount == 0 ? 1 : 0;
				OverlapCount += CoverCount > 1 ? 1 : 0;
			}
		}

		double PatchAreaSum = 0.0;
		int32 CoveredCellCount = 0;
		for (const FTDFluidWaterLodPatch& Patch : Result.Patches)
		{
			PatchAreaSum += Patch.SizeCm * Patch.SizeCm;
		}
		for (const int32 CoverCount : Grid.CoverCounts)
		{
			CoveredCellCount += CoverCount > 0 ? 1 : 0;
		}

		const FString Label = FString::Printf(TEXT("Observer (%.0f, %.0f, %.0f)"), ObserverWorld.X, ObserverWorld.Y, ObserverWorld.Z);
		TestEqual(*(Label + TEXT(" leaves no gap over the coverage")), GapCount, 0);
		TestEqual(*(Label + TEXT(" has no overlapping patches")), OverlapCount, 0);
		TestNearlyEqual(*(Label + TEXT(" patch area equals the union area")), PatchAreaSum, CoveredCellCount * Grid.CellSizeCm * Grid.CellSizeCm, 1.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLodNeighborLevelsTest,
	"TDGame.FluidFX.WaterLodGrid.NeighborLevelsDifferByAtMostOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLodNeighborLevelsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterLodGridTestPrivate;

	int32 ComparedPairCount = 0;
	int32 ViolationCount = 0;
	for (const FVector& ObserverWorld : MakeSeededObservers(CoastCoverageCm, CoastSurfaceHeightCm))
	{
		const FTDFluidWaterLodSelectionInput Input = MakeCoastInput(ObserverWorld);
		const FTDFluidWaterLodGridSettings Settings = UTDFluidWaterLodGridComponent::SanitizeSettings(Input.Settings);
		const FTDFluidLodCellGrid Grid = RasterizePatches(UTDFluidWaterLodGridComponent::SelectPatches(Input), Settings, Input.CoverageCm);
		for (int32 CellY = Grid.FirstCell.Y; CellY < Grid.FirstCell.Y + Grid.CellCount.Y; ++CellY)
		{
			for (int32 CellX = Grid.FirstCell.X; CellX < Grid.FirstCell.X + Grid.CellCount.X; ++CellX)
			{
				const int32 Level = Grid.GetLevel(CellX, CellY);
				if (Level == INDEX_NONE)
				{
					continue;
				}
				const int32 NeighborLevels[2] = { Grid.GetLevel(CellX + 1, CellY), Grid.GetLevel(CellX, CellY + 1) };
				for (const int32 NeighborLevel : NeighborLevels)
				{
					if (NeighborLevel == INDEX_NONE)
					{
						continue;
					}
					++ComparedPairCount;
					ViolationCount += FMath::Abs(NeighborLevel - Level) > 1 ? 1 : 0;
				}
			}
		}
	}

	AddInfo(FString::Printf(TEXT("TDFluidWaterLodNeighbors: pairs=%d violations=%d"), ComparedPairCount, ViolationCount));
	TestTrue(TEXT("Neighbor pairs were compared"), ComparedPairCount > 0);
	TestEqual(TEXT("No neighbor pair differs by more than one level"), ViolationCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLodSharedEdgeTest,
	"TDGame.FluidFX.WaterLodGrid.SharedEdgeVerticesMatchAfterMorph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLodSharedEdgeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterLodGridTestPrivate;

	int32 ComparedVertexCount = 0;
	int32 MismatchCount = 0;
	int32 CounterexampleComparedCount = 0;
	int32 CounterexampleMismatchCount = 0;
	for (const FVector& ObserverWorld : MakeSeededObservers(CoastCoverageCm, CoastSurfaceHeightCm))
	{
		const FTDFluidWaterLodSelectionInput Input = MakeCoastInput(ObserverWorld);
		const FTDFluidWaterLodGridSettings Settings = UTDFluidWaterLodGridComponent::SanitizeSettings(Input.Settings);
		MismatchCount += CountSharedEdgeMismatches(UTDFluidWaterLodGridComponent::SelectPatches(Input), Input, Settings, ComparedVertexCount);

		FTDFluidWaterLodSelectionInput CounterexampleInput = Input;
		CounterexampleInput.bSkipsCrackFreeRange = true;
		CounterexampleInput.Settings.TotalVertexBudget = 1000000;
		CounterexampleInput.Settings.BaseRangeCm = static_cast<float>(CounterexampleRangeRatio * UTDFluidWaterLodGridComponent::ComputeCrackFreeMinimumRangeCm(Settings));
		FTDFluidWaterLodGridSettings CounterexampleSettings = Settings;
		CounterexampleSettings.BaseRangeCm = CounterexampleInput.Settings.BaseRangeCm;
		CounterexampleMismatchCount += CountSharedEdgeMismatches(UTDFluidWaterLodGridComponent::SelectPatches(CounterexampleInput), CounterexampleInput, CounterexampleSettings, CounterexampleComparedCount);
	}

	AddInfo(FString::Printf(TEXT("TDFluidWaterLodSharedEdge: compared=%d mismatches=%d counterexample compared=%d mismatches=%d"), ComparedVertexCount, MismatchCount, CounterexampleComparedCount, CounterexampleMismatchCount));
	TestTrue(TEXT("Shared edge vertices were compared"), ComparedVertexCount > 0);
	TestEqual(TEXT("Every shared edge vertex matches after the morph"), MismatchCount, 0);
	TestTrue(TEXT("A base range below the crack free minimum produces a mismatch"), CounterexampleMismatchCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLodDeterminismTest,
	"TDGame.FluidFX.WaterLodGrid.SelectionIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLodDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterLodGridTestPrivate;

	const FTDFluidWaterLodSelectionInput Input = MakeCoastInput(FVector(-457.0, 0.0, 800.0));
	const FTDFluidWaterLodSelectionResult FirstResult = UTDFluidWaterLodGridComponent::SelectPatches(Input);
	const FTDFluidWaterLodSelectionResult SecondResult = UTDFluidWaterLodGridComponent::SelectPatches(Input);
	TestTrue(TEXT("The selection is not empty"), FirstResult.Patches.Num() > 0);
	TestTrue(TEXT("Two selections give the same patches in the same order"), FirstResult.Patches == SecondResult.Patches);
	TestEqual(TEXT("Two selections give the same effective range"), FirstResult.EffectiveBaseRangeCm, SecondResult.EffectiveBaseRangeCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLodBudgetTest,
	"TDGame.FluidFX.WaterLodGrid.BudgetFallbackKeepsConstraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLodBudgetTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterLodGridTestPrivate;

	FTDFluidWaterLodSelectionInput Input = MakeCoastInput(FVector(0.0, 0.0, 1000.0));
	Input.CoverageCm = FBox2D(FVector2D(-1000000.0, -1000000.0), FVector2D(1000000.0, 1000000.0));
	const FTDFluidWaterLodGridSettings Settings = UTDFluidWaterLodGridComponent::SanitizeSettings(Input.Settings);
	const FTDFluidWaterLodSelectionResult Result = UTDFluidWaterLodGridComponent::SelectPatches(Input);
	const int32 MaximumPatchCount = Settings.TotalVertexBudget / ((Settings.PatchQuads + 1) * (Settings.PatchQuads + 1));
	AddInfo(FString::Printf(TEXT("TDFluidWaterLodBudget: patches=%d limit=%d reached=%d range=%.1f"), Result.Patches.Num(), MaximumPatchCount, Result.bReachedBudget ? 1 : 0, Result.EffectiveBaseRangeCm));
	TestTrue(TEXT("The selection fits the budget or reports reaching it"), Result.Patches.Num() <= MaximumPatchCount || Result.bReachedBudget);
	TestTrue(TEXT("The effective range never drops below the crack free minimum"), Result.EffectiveBaseRangeCm >= UTDFluidWaterLodGridComponent::ComputeCrackFreeMinimumRangeCm(Settings) - 1.0e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLodDisabledByDefaultTest,
	"TDGame.FluidFX.WaterLodGrid.DisabledByDefaultKeepsTiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLodDisabledByDefaultTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterLodGridTestPrivate;

	FTDFluidLodTestWorld TestWorld;
	UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	Preset->bOverridePresetDefaults = true;
	Preset->SimulationConfig.GridResolution = 64;
	Preset->SimulationConfig.SimExtentCm = FVector2D(3000.0, 3000.0);
	ATDFluidWaterActor* WaterActor = TestWorld.World->SpawnActor<ATDFluidWaterActor>(ATDFluidWaterActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Spawned the water actor"), WaterActor) || !TestNotNull(TEXT("The water actor owns a LOD grid"), WaterActor->LodGridComponent.Get()))
	{
		return false;
	}
	WaterActor->InitializeFromPreset(Preset, 0.0f);

	AActor* RegionActor = TestWorld.World->SpawnActor<AActor>();
	UBoxComponent* RegionBox = NewObject<UBoxComponent>(RegionActor);
	RegionActor->AddInstanceComponent(RegionBox);
	RegionActor->SetRootComponent(RegionBox);
	RegionBox->SetBoxExtent(FVector(1000.0, 1000.0, 50.0));
	RegionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RegionBox->RegisterComponent();
	RegionActor->SetActorLocation(FVector(0.0, 0.0, 50.0));
	RegionActor->Tags.Add(WaterActor->WaterRegionActorTag);

	AActor* SurfaceActor = TestWorld.World->SpawnActor<AActor>();
	UStaticMeshComponent* SurfaceMesh = NewObject<UStaticMeshComponent>(SurfaceActor);
	SurfaceActor->AddInstanceComponent(SurfaceMesh);
	SurfaceActor->SetRootComponent(SurfaceMesh);
	SurfaceMesh->SetMobility(EComponentMobility::Movable);
	SurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SurfaceMesh->RegisterComponent();
	SurfaceActor->SetActorLocation(FVector(0.0, 0.0, 100.0));
	SurfaceActor->Tags.Add(WaterActor->WaterSurfaceActorTag);

	TestWorld.TickFrames(4);
	TestEqual(TEXT("By default the LOD grid has no instances"), WaterActor->LodGridComponent->GetInstanceCount(), 0);
	TestFalse(TEXT("By default the tagged water surface stays visible"), SurfaceActor->IsHidden());
	TestEqual(TEXT("By default the LOD grid reports no patches"), WaterActor->GetLodGridStats().PatchCount, 0);

	{
		FTDFluidLodConsoleScope ConsoleScope(1);
		if (!TestNotNull(TEXT("TD.FluidFX.WaterLodGrid is registered"), ConsoleScope.Variable))
		{
			return false;
		}
		AddExpectedMessage(TEXT("stays off because it has no patch mesh"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		TestWorld.TickFrames(4);
		TestEqual(TEXT("Forced on without a patch mesh the LOD grid has no instances"), WaterActor->LodGridComponent->GetInstanceCount(), 0);
		TestFalse(TEXT("Forced on without a patch mesh the tagged water surface stays visible"), SurfaceActor->IsHidden());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterLodSelectionCostTest,
	"TDGame.FluidFX.WaterLodGrid.SelectionCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterLodSelectionCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterLodGridTestPrivate;

	constexpr int32 RepeatCount = 50;
	struct FTDFluidLodCostCase
	{
		const TCHAR* Name;
		FVector ObserverWorld;
		float CoverageMarginCm;
	};
	const FTDFluidLodCostCase CostCases[] = {
		{ TEXT("coast_game"), FVector(-457.0, 0.0, 800.0), 0.0f },
		{ TEXT("coast_horizon"), FVector(-457.0, 0.0, 1500.0), 20000.0f }
	};

	FString CostJson = TEXT("{\n  \"cases\": [\n");
	for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(CostCases); ++CaseIndex)
	{
		const FTDFluidLodCostCase& CostCase = CostCases[CaseIndex];
		FTDFluidWaterLodSelectionInput Input = MakeCoastInput(CostCase.ObserverWorld);
		Input.Settings.CoverageMarginCm = CostCase.CoverageMarginCm;
		FTDFluidWaterLodSelectionResult Result;
		const double StartSeconds = FPlatformTime::Seconds();
		for (int32 RepeatIndex = 0; RepeatIndex < RepeatCount; ++RepeatIndex)
		{
			Result = UTDFluidWaterLodGridComponent::SelectPatches(Input);
		}
		const double AverageMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0 / RepeatCount;
		TestTrue(*FString::Printf(TEXT("%s selects patches"), CostCase.Name), Result.Patches.Num() > 0);
		AddInfo(FString::Printf(TEXT("TDFluidWaterLodCost: %s average_ms=%.4f patches=%d reached=%d"), CostCase.Name, AverageMilliseconds, Result.Patches.Num(), Result.bReachedBudget ? 1 : 0));
		CostJson += FString::Printf(TEXT("    {\"name\": \"%s\", \"average_ms\": %.5f, \"patches\": %d, \"reached_budget\": %s, \"effective_base_range_cm\": %.2f}%s\n"),
			CostCase.Name, AverageMilliseconds, Result.Patches.Num(), Result.bReachedBudget ? TEXT("true") : TEXT("false"), Result.EffectiveBaseRangeCm, CaseIndex + 1 < UE_ARRAY_COUNT(CostCases) ? TEXT(",") : TEXT(""));
	}
	CostJson += TEXT("  ]\n}\n");
	FFileHelper::SaveStringToFile(CostJson, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("w2s-bc-lod-cost.json")));
	return true;
}

#endif
