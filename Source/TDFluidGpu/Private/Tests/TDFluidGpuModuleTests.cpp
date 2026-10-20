#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "DynamicRHI.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHIGlobals.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuStampBins.h"
#include "TDFluidGpuMultigridProbeReadbacks.h"
#include "TDFluidGpuPassTimestamps.h"
#include "TDFluidGpuStepPasses.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuRhiTests, Log, All);

namespace TDFluidGpuModuleTestSupport
{
	FTDFluidGpuStampBinWindow MakeWindow(const FVector2D& CenterPlaneCm, const FVector2D& ExtentCm, int32 Resolution)
	{
		FTDFluidGpuStampBinWindow Window;
		Window.CenterPlaneCm = CenterPlaneCm;
		Window.ExtentCm = ExtentCm;
		Window.Resolution = Resolution;
		return Window;
	}

	bool IsReferenceFiniteStamp(const FTDFluidGpuStampInput& Stamp)
	{
		return FMath::IsFinite(Stamp.CenterPlaneCm.X)
			&& FMath::IsFinite(Stamp.CenterPlaneCm.Y)
			&& FMath::IsFinite(Stamp.VelocityPlaneCmPerSec.X)
			&& FMath::IsFinite(Stamp.VelocityPlaneCmPerSec.Y)
			&& FMath::IsFinite(Stamp.RadiusCm)
			&& FMath::IsFinite(Stamp.DensityStrength)
			&& FMath::IsFinite(Stamp.VelocityStrength)
			&& FMath::IsFinite(Stamp.RadialVelocityCmPerSec);
	}

	float ComputeReferenceEffectiveRadius(const FTDFluidGpuStampBinWindow& Window, float RadiusCm)
	{
		const float CellSizeX = static_cast<float>(Window.ExtentCm.X / static_cast<float>(Window.Resolution));
		const float CellSizeY = static_cast<float>(Window.ExtentCm.Y / static_cast<float>(Window.Resolution));
		const float MinRadius = FMath::Max(CellSizeX, CellSizeY) * 0.5f;
		const float MaxRadius = static_cast<float>(FMath::Max(Window.ExtentCm.X, Window.ExtentCm.Y));
		return FMath::Clamp(RadiusCm, MinRadius, MaxRadius);
	}

	bool DoesStampTouchCell(const FTDFluidGpuStampBinWindow& Window, const FTDFluidGpuStampInput& Stamp, float EffectiveRadiusCm, int32 CellX, int32 CellY)
	{
		const float CellSizeX = static_cast<float>(Window.ExtentCm.X / static_cast<float>(Window.Resolution));
		const float CellSizeY = static_cast<float>(Window.ExtentCm.Y / static_cast<float>(Window.Resolution));
		const FVector2D MinWorld = Window.CenterPlaneCm - 0.5f * Window.ExtentCm;
		const FVector2D CellCenter(
			MinWorld.X + (static_cast<float>(CellX) + 0.5f) * CellSizeX,
			MinWorld.Y + (static_cast<float>(CellY) + 0.5f) * CellSizeY);
		return FVector2D::Distance(CellCenter, Stamp.CenterPlaneCm) <= EffectiveRadiusCm;
	}

	float ComputeReferencePriorityDistanceSquared(const FTDFluidGpuStampBinWindow& Window, const FTDFluidGpuStampInput& Stamp)
	{
		return static_cast<float>(FVector2D::DistSquared(Stamp.CenterPlaneCm, Window.CenterPlaneCm));
	}

	TArray<int32> SortByReferencePriority(const FTDFluidGpuStampBinWindow& Window, const TArray<FTDFluidGpuStampInput>& Stamps, const TArray<int32>& SourceIndices)
	{
		TArray<int32> OrderedIndices = SourceIndices;
		OrderedIndices.StableSort([&Window, &Stamps](int32 Left, int32 Right)
		{
			return ComputeReferencePriorityDistanceSquared(Window, Stamps[Left]) < ComputeReferencePriorityDistanceSquared(Window, Stamps[Right]);
		});
		return OrderedIndices;
	}

	TArray<int32> MakeSequentialIndices(int32 Count)
	{
		TArray<int32> Indices;
		Indices.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Indices.Add(Index);
		}
		return Indices;
	}

	TArray<int32> CollectKeptSourceIndices(const FTDFluidGpuStampBinSet& BinSet)
	{
		TArray<int32> SourceIndices;
		SourceIndices.Reserve(BinSet.Stamps.Num());
		for (const FTDFluidGpuPackedStamp& Stamp : BinSet.Stamps)
		{
			SourceIndices.Add(static_cast<int32>(Stamp.SourceIndex));
		}
		return SourceIndices;
	}

	TArray<int32> CollectBinSourceIndices(const FTDFluidGpuStampBinSet& BinSet, int32 BinIndex)
	{
		TArray<int32> SourceIndices;
		const FTDFluidGpuStampBinRange& Range = BinSet.BinRanges[BinIndex];
		for (uint32 EntryOffset = 0; EntryOffset < Range.Count; ++EntryOffset)
		{
			const int32 StampIndex = static_cast<int32>(BinSet.BinStampIndices[static_cast<int32>(Range.FirstIndex + EntryOffset)]);
			SourceIndices.Add(static_cast<int32>(BinSet.Stamps[StampIndex].SourceIndex));
		}
		return SourceIndices;
	}

	FTDFluidGpuStampInput MakeRandomStamp(FRandomStream& Random, const FTDFluidGpuStampBinWindow& Window, double SpreadRatio, float MinRadiusCm, float MaxRadiusCm)
	{
		FTDFluidGpuStampInput Stamp;
		Stamp.CenterPlaneCm = Window.CenterPlaneCm + FVector2D(
			Random.FRandRange(-SpreadRatio, SpreadRatio) * Window.ExtentCm.X,
			Random.FRandRange(-SpreadRatio, SpreadRatio) * Window.ExtentCm.Y);
		Stamp.VelocityPlaneCmPerSec = FVector2D(Random.FRandRange(-1500.0, 1500.0), Random.FRandRange(-1500.0, 1500.0));
		Stamp.RadiusCm = static_cast<float>(Random.FRandRange(MinRadiusCm, MaxRadiusCm));
		Stamp.DensityStrength = static_cast<float>(Random.FRandRange(-1.0, 1.0));
		Stamp.VelocityStrength = static_cast<float>(Random.FRandRange(0.0, 1.0));
		Stamp.RadialVelocityCmPerSec = static_cast<float>(Random.FRandRange(-600.0, 600.0));
		Stamp.bWritesSelectiveDensity = Random.RandRange(0, 1) == 1;
		return Stamp;
	}

	void VerifyCellStampListsMatchBruteForce(FAutomationTestBase& Test)
	{
		constexpr int32 StampCount = 2000;
		constexpr int32 NonFiniteStride = 97;
		const FTDFluidGpuStampBinWindow Window = MakeWindow(FVector2D(12345.5, -6789.25), FVector2D(4000.0, 3000.0), 120);
		FRandomStream Random(0x5EED1234);

		TArray<FTDFluidGpuStampInput> Stamps;
		Stamps.Reserve(StampCount);
		int32 ExpectedNonFiniteCount = 0;
		for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
		{
			FTDFluidGpuStampInput& Stamp = Stamps.Add_GetRef(MakeRandomStamp(Random, Window, 0.6, 5.0f, 400.0f));
			if (StampIndex % NonFiniteStride != 0)
			{
				continue;
			}

			Stamp.RadiusCm = std::numeric_limits<float>::quiet_NaN();
			++ExpectedNonFiniteCount;
		}

		const FTDFluidGpuStampBinSet BinSet = FTDFluidGpuStampBins::Build(Window, Stamps, FTDFluidGpuStampBinLimits());
		Test.TestEqual(TEXT("Bin table has one range per bin"), BinSet.BinRanges.Num(), TDFluidGpuSolverConstants::StampBinCount);
		Test.TestEqual(TEXT("Non-finite stamps are dropped"), BinSet.NonFiniteStampCount, ExpectedNonFiniteCount);
		Test.TestEqual(TEXT("No stamp is over the tick limit"), BinSet.OverTickLimitStampCount, 0);
		Test.TestEqual(TEXT("No bin overflows"), BinSet.BinOverflowEntryCount, 0);
		Test.TestEqual(TEXT("Kept and dropped stamps add up to the input"), BinSet.Stamps.Num() + BinSet.NonFiniteStampCount + BinSet.OutsideWindowStampCount, StampCount);
		Test.TestTrue(TEXT("Some stamps fall outside the window"), BinSet.OutsideWindowStampCount > 0);

		const TArray<int32> KeptSourceIndices = CollectKeptSourceIndices(BinSet);
		bool bKeepsInputOrder = true;
		for (int32 KeptIndex = 1; KeptIndex < KeptSourceIndices.Num(); ++KeptIndex)
		{
			bKeepsInputOrder = bKeepsInputOrder && KeptSourceIndices[KeptIndex - 1] < KeptSourceIndices[KeptIndex];
		}
		Test.TestTrue(TEXT("Kept stamps keep the input order below the tick limit"), bKeepsInputOrder);

		TArray<float> EffectiveRadii;
		TArray<bool> FiniteStamps;
		EffectiveRadii.SetNumZeroed(StampCount);
		FiniteStamps.SetNumZeroed(StampCount);
		for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
		{
			EffectiveRadii[StampIndex] = ComputeReferenceEffectiveRadius(Window, Stamps[StampIndex].RadiusCm);
			FiniteStamps[StampIndex] = IsReferenceFiniteStamp(Stamps[StampIndex]);
		}

		const int32 BinSpanCells = FTDFluidGpuStampBins::GetBinSpanCells(Window.Resolution);
		int32 MismatchedCellCount = 0;
		int32 TouchedCellCount = 0;
		TArray<int32> ReferenceSourceIndices;
		TArray<int32> BinnedSourceIndices;
		for (int32 CellY = 0; CellY < Window.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Window.Resolution; ++CellX)
			{
				ReferenceSourceIndices.Reset();
				for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
				{
					if (FiniteStamps[StampIndex] && DoesStampTouchCell(Window, Stamps[StampIndex], EffectiveRadii[StampIndex], CellX, CellY))
					{
						ReferenceSourceIndices.Add(StampIndex);
					}
				}

				BinnedSourceIndices.Reset();
				const int32 BinIndex = (CellY / BinSpanCells) * TDFluidGpuSolverConstants::StampBinsPerAxis + CellX / BinSpanCells;
				for (const int32 SourceIndex : CollectBinSourceIndices(BinSet, BinIndex))
				{
					if (DoesStampTouchCell(Window, Stamps[SourceIndex], EffectiveRadii[SourceIndex], CellX, CellY))
					{
						BinnedSourceIndices.Add(SourceIndex);
					}
				}

				TouchedCellCount += ReferenceSourceIndices.IsEmpty() ? 0 : 1;
				if (ReferenceSourceIndices == BinnedSourceIndices)
				{
					continue;
				}

				if (MismatchedCellCount == 0)
				{
					Test.AddError(FString::Printf(TEXT("Cell (%d, %d): brute force sees %d stamps, bins see %d stamps"), CellX, CellY, ReferenceSourceIndices.Num(), BinnedSourceIndices.Num()));
				}
				++MismatchedCellCount;
			}
		}

		Test.TestEqual(TEXT("Every cell sees the brute force stamp list in order"), MismatchedCellCount, 0);
		Test.TestTrue(TEXT("The random stamps touch most cells"), TouchedCellCount * 2 > Window.Resolution * Window.Resolution);

		if (BinSet.Stamps.IsEmpty())
		{
			Test.AddError(TEXT("No stamp was kept"));
			return;
		}

		const FTDFluidGpuPackedStamp& FirstPacked = BinSet.Stamps[0];
		const int32 FirstSourceIndex = static_cast<int32>(FirstPacked.SourceIndex);
		const FTDFluidGpuStampInput& FirstSource = Stamps[FirstSourceIndex];
		const FVector2f ExpectedOffset(FirstSource.CenterPlaneCm - Window.CenterPlaneCm);
		Test.TestEqual(TEXT("Packed stamp center is window relative"), FirstPacked.CenterFromWindowCenterCm.X, ExpectedOffset.X);
		Test.TestEqual(TEXT("Packed stamp center is window relative on Y"), FirstPacked.CenterFromWindowCenterCm.Y, ExpectedOffset.Y);
		Test.TestEqual(TEXT("Packed stamp radius is the clamped radius"), FirstPacked.EffectiveRadiusCm, EffectiveRadii[FirstSourceIndex]);
		Test.TestTrue(TEXT("Packed target velocity respects the cell speed limit"), FirstPacked.TargetVelocityCmPerSec.Size() <= TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec + 0.01f);
	}

	void VerifyTickLimitDropsFarthestFirst(FAutomationTestBase& Test)
	{
		constexpr int32 StampCount = 300;
		constexpr int32 TickLimit = 100;
		const FTDFluidGpuStampBinWindow Window = MakeWindow(FVector2D(-2500.0, 800.0), FVector2D(3000.0, 3000.0), 64);
		FRandomStream Random(0x1D0C0DE);

		TArray<FTDFluidGpuStampInput> Stamps;
		Stamps.Reserve(StampCount);
		for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
		{
			Stamps.Add(MakeRandomStamp(Random, Window, 0.4, 10.0f, 150.0f));
		}

		FTDFluidGpuStampBinLimits Limits;
		Limits.MaxStampsPerTick = TickLimit;
		const FTDFluidGpuStampBinSet BinSet = FTDFluidGpuStampBins::Build(Window, Stamps, Limits);

		TArray<int32> ExpectedSourceIndices = SortByReferencePriority(Window, Stamps, MakeSequentialIndices(StampCount));
		ExpectedSourceIndices.SetNum(TickLimit);

		Test.TestEqual(TEXT("Tick limit keeps the limit count"), BinSet.Stamps.Num(), TickLimit);
		Test.TestEqual(TEXT("Tick limit drops the rest"), BinSet.OverTickLimitStampCount, StampCount - TickLimit);
		Test.TestTrue(TEXT("Tick limit keeps the nearest stamps in nearest first order like the CPU clamp"), CollectKeptSourceIndices(BinSet) == ExpectedSourceIndices);
	}

	void VerifyBinOverflowDropsFarthestFirst(FAutomationTestBase& Test)
	{
		constexpr int32 StampCount = 400;
		constexpr int32 TargetBinX = 5;
		constexpr int32 TargetBinY = 6;
		const FTDFluidGpuStampBinWindow Window = MakeWindow(FVector2D::ZeroVector, FVector2D(4000.0, 4000.0), 64);
		const double CellSizeCm = Window.ExtentCm.X / Window.Resolution;
		const double MinWorldCm = -0.5 * Window.ExtentCm.X;
		const double MarginCm = 0.1;
		FRandomStream Random(0x2B1A5);

		TArray<FTDFluidGpuStampInput> Stamps;
		Stamps.Reserve(StampCount);
		for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
		{
			FTDFluidGpuStampInput Stamp;
			Stamp.CenterPlaneCm = FVector2D(
				Random.FRandRange(MinWorldCm + 21.5 * CellSizeCm + MarginCm, MinWorldCm + 22.5 * CellSizeCm - MarginCm),
				Random.FRandRange(MinWorldCm + 25.5 * CellSizeCm + MarginCm, MinWorldCm + 26.5 * CellSizeCm - MarginCm));
			Stamp.RadiusCm = 1.0f;
			Stamps.Add(Stamp);
		}

		const FTDFluidGpuStampBinSet BinSet = FTDFluidGpuStampBins::Build(Window, Stamps, FTDFluidGpuStampBinLimits());
		const int32 TargetBinIndex = TargetBinY * TDFluidGpuSolverConstants::StampBinsPerAxis + TargetBinX;

		TArray<int32> ExpectedSourceIndices = SortByReferencePriority(Window, Stamps, MakeSequentialIndices(StampCount));
		ExpectedSourceIndices.SetNum(TDFluidGpuSolverConstants::MaximumStampsPerBin);
		ExpectedSourceIndices.Sort();

		Test.TestEqual(TEXT("All overflow stamps stay in the tick"), BinSet.Stamps.Num(), StampCount);
		Test.TestEqual(TEXT("Overflowing bin drops the extra entries"), BinSet.BinOverflowEntryCount, StampCount - TDFluidGpuSolverConstants::MaximumStampsPerBin);
		Test.TestEqual(TEXT("Only the target bin has entries"), BinSet.BinStampIndices.Num(), TDFluidGpuSolverConstants::MaximumStampsPerBin);
		Test.TestTrue(TEXT("Overflowing bin keeps the stamps nearest the window center in application order"), CollectBinSourceIndices(BinSet, TargetBinIndex) == ExpectedSourceIndices);
	}

	FTDFluidGpuActorInput MakeBatchTestActor(uint64 ActorKey, ETDFluidGpuSimMode SimMode, int32 Resolution, int32 PressureIterations)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ActorKey;
		Actor.Settings.SimMode = SimMode;
		Actor.Settings.Resolution = Resolution;
		Actor.Settings.PressureIterations = PressureIterations;
		Actor.DeltaTimeSeconds = 1.0f / 60.0f;
		Actor.WindowCenterPlaneCm = FVector2D(100.0 * static_cast<double>(ActorKey), 0.0);
		return Actor;
	}

	const FTDFluidGpuBatch* FindBatch(const TArray<FTDFluidGpuBatch>& Batches, const FTDFluidGpuBatchKey& Key)
	{
		return Batches.FindByPredicate([&Key](const FTDFluidGpuBatch& Batch)
		{
			return Batch.GetKey() == Key;
		});
	}

	const FTDFluidGpuSliceEntry* FindEntryBySlice(const FTDFluidGpuBatch& Batch, int32 SliceIndex)
	{
		return Batch.GetFrame().Entries.FindByPredicate([SliceIndex](const FTDFluidGpuSliceEntry& Entry)
		{
			return Entry.SliceIndex == SliceIndex;
		});
	}

	bool IsSliceReset(const FTDFluidGpuBatch& Batch, int32 SliceIndex)
	{
		const FTDFluidGpuSliceEntry* Entry = FindEntryBySlice(Batch, SliceIndex);
		return Entry && Entry->bNeedsReset;
	}

	bool HasActorFlag(const FTDFluidGpuActorParameters& Parameters, ETDFluidGpuActorFlags Flag)
	{
		return EnumHasAnyFlags(static_cast<ETDFluidGpuActorFlags>(Parameters.Flags), Flag);
	}

	struct FTDFluidGpuConsoleVariableOverride
	{
		FTDFluidGpuConsoleVariableOverride(const TCHAR* VariableName, int32 OverrideValue)
			: Variable(IConsoleManager::Get().FindConsoleVariable(VariableName))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetInt();
			Variable->Set(OverrideValue, ECVF_SetByCode);
			bIsApplied = Variable->GetInt() == OverrideValue;
		}

		~FTDFluidGpuConsoleVariableOverride()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
		bool bIsApplied = false;
	};

	const TCHAR* const GpuPressureSolverVariableName = TEXT("TD.FluidFX.GpuPressureSolver");
	const TCHAR* const GpuPressureCyclesVariableName = TEXT("TD.FluidFX.GpuPressureCycles");
	constexpr int32 ForcedJacobiPressureSolverMode = 1;
	constexpr int32 ForcedMultigridPressureSolverMode = 2;

	void VerifyPressureSolverKeys(FAutomationTestBase& Test)
	{
		const FTDFluidGpuBatchKey Auto256Key = FTDFluidGpuBatch::MakeKey(MakeBatchTestActor(21, ETDFluidGpuSimMode::FluidSim, 256, 8).Settings);
		const FTDFluidGpuBatchKey Auto512Key = FTDFluidGpuBatch::MakeKey(MakeBatchTestActor(22, ETDFluidGpuSimMode::FluidSim, 512, 8).Settings);
		const FTDFluidGpuBatchKey Auto512OtherIterationsKey = FTDFluidGpuBatch::MakeKey(MakeBatchTestActor(23, ETDFluidGpuSimMode::FluidSim, 512, 20).Settings);
		const FTDFluidGpuBatchKey Auto1024Key = FTDFluidGpuBatch::MakeKey(MakeBatchTestActor(24, ETDFluidGpuSimMode::FluidSim, 1024, 8).Settings);
		const FTDFluidGpuBatchKey PainterKey = FTDFluidGpuBatch::MakeKey(MakeBatchTestActor(25, ETDFluidGpuSimMode::SimplePainter, 1024, 8).Settings);
		Test.TestTrue(TEXT("Automatic solver keeps Jacobi at 256"), Auto256Key.PressureSolver == ETDFluidGpuPressureSolver::Jacobi);
		Test.TestEqual(TEXT("Jacobi keys carry the iterations"), Auto256Key.PressureIterations, 8);
		Test.TestEqual(TEXT("Jacobi keys carry no multigrid cycles"), Auto256Key.MultigridCycles, 0);
		Test.TestTrue(TEXT("Automatic solver uses multigrid at 512"), Auto512Key.PressureSolver == ETDFluidGpuPressureSolver::Multigrid);
		Test.TestTrue(TEXT("Automatic solver uses multigrid at 1024"), Auto1024Key.PressureSolver == ETDFluidGpuPressureSolver::Multigrid);
		Test.TestEqual(TEXT("Multigrid keys ignore pressure iterations"), Auto512Key.PressureIterations, 0);
		Test.TestEqual(TEXT("Multigrid keys carry the default cycle count"), Auto512Key.MultigridCycles, TDFluidGpuSolverConstants::DefaultMultigridCycles);
		Test.TestTrue(TEXT("Multigrid actors with different iterations share a key"), Auto512Key == Auto512OtherIterationsKey);
		Test.TestTrue(TEXT("Painter keys normalize the pressure solver"), PainterKey.PressureSolver == ETDFluidGpuPressureSolver::Jacobi && PainterKey.MultigridCycles == 0 && PainterKey.PressureIterations == 0);
		Test.TestEqual(TEXT("512 has five multigrid levels"), FTDFluidGpuBatch::ComputeMultigridLevelCount(512), 5);
		Test.TestEqual(TEXT("64 has two multigrid levels"), FTDFluidGpuBatch::ComputeMultigridLevelCount(64), 2);

		{
			FTDFluidGpuConsoleVariableOverride SolverOverride(GpuPressureSolverVariableName, ForcedJacobiPressureSolverMode);
			Test.TestTrue(TEXT("Pressure solver variable is registered and settable"), SolverOverride.bIsApplied);
			const FTDFluidGpuBatchKey ForcedJacobiKey = FTDFluidGpuBatch::MakeKey(MakeBatchTestActor(22, ETDFluidGpuSimMode::FluidSim, 512, 8).Settings);
			Test.TestTrue(TEXT("Forced Jacobi uses Jacobi at 512"), ForcedJacobiKey.PressureSolver == ETDFluidGpuPressureSolver::Jacobi && ForcedJacobiKey.PressureIterations == 8);
			Test.TestFalse(TEXT("Forced Jacobi changes the 512 key"), ForcedJacobiKey == Auto512Key);
		}

		{
			FTDFluidGpuConsoleVariableOverride SolverOverride(GpuPressureSolverVariableName, ForcedMultigridPressureSolverMode);
			Test.TestTrue(TEXT("Forced multigrid uses multigrid at 64"), FTDFluidGpuBatch::MakeKey(MakeBatchTestActor(26, ETDFluidGpuSimMode::FluidSim, 64, 8).Settings).PressureSolver == ETDFluidGpuPressureSolver::Multigrid);
			Test.TestTrue(TEXT("Forced multigrid falls back to Jacobi below 64"), FTDFluidGpuBatch::MakeKey(MakeBatchTestActor(27, ETDFluidGpuSimMode::FluidSim, 32, 8).Settings).PressureSolver == ETDFluidGpuPressureSolver::Jacobi);
		}

		const int32 CycleOverrides[] = { 0, 9 };
		const int32 ExpectedCycles[] = { TDFluidGpuSolverConstants::MinimumMultigridCycles, TDFluidGpuSolverConstants::MaximumMultigridCycles };
		for (int32 OverrideIndex = 0; OverrideIndex < UE_ARRAY_COUNT(CycleOverrides); ++OverrideIndex)
		{
			FTDFluidGpuConsoleVariableOverride CycleOverride(GpuPressureCyclesVariableName, CycleOverrides[OverrideIndex]);
			const FTDFluidGpuBatchKey ClampedKey = FTDFluidGpuBatch::MakeKey(MakeBatchTestActor(22, ETDFluidGpuSimMode::FluidSim, 512, 8).Settings);
			Test.TestEqual(*FString::Printf(TEXT("Cycle override %d is clamped"), CycleOverrides[OverrideIndex]), ClampedKey.MultigridCycles, ExpectedCycles[OverrideIndex]);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuStampBinsMatchBruteForceTest,
	"TDGame.FluidFX.Gpu.StampBinsMatchBruteForce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuStampBinsMatchBruteForceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuModuleTestSupport;

	VerifyCellStampListsMatchBruteForce(*this);
	VerifyTickLimitDropsFarthestFirst(*this);
	VerifyBinOverflowDropsFarthestFirst(*this);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuBatchKeyGroupsByResolutionAndIterationsTest,
	"TDGame.FluidFX.Gpu.BatchKeyGroupsByResolutionAndIterations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuBatchKeyGroupsByResolutionAndIterationsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuModuleTestSupport;

	FTDFluidGpuFrameInputs FirstFrame;
	FirstFrame.Actors.Add(MakeBatchTestActor(11, ETDFluidGpuSimMode::FluidSim, 256, 8));
	FirstFrame.Actors.Add(MakeBatchTestActor(12, ETDFluidGpuSimMode::FluidSim, 200, 8));
	FirstFrame.Actors.Add(MakeBatchTestActor(13, ETDFluidGpuSimMode::FluidSim, 256, 12));
	FirstFrame.Actors.Add(MakeBatchTestActor(14, ETDFluidGpuSimMode::FluidSim, 512, 8));
	FirstFrame.Actors.Add(MakeBatchTestActor(15, ETDFluidGpuSimMode::SimplePainter, 1024, 8));
	FirstFrame.Actors.Add(MakeBatchTestActor(16, ETDFluidGpuSimMode::SimplePainter, 1000, 12));

	const FTDFluidGpuBatchKey SharedKey = FTDFluidGpuBatch::MakeKey(FirstFrame.Actors[0].Settings);
	const FTDFluidGpuBatchKey TwelveIterationKey = FTDFluidGpuBatch::MakeKey(FirstFrame.Actors[2].Settings);
	const FTDFluidGpuBatchKey PainterKey = FTDFluidGpuBatch::MakeKey(FirstFrame.Actors[4].Settings);
	TestEqual(TEXT("256 stays in the 256 tier"), SharedKey.ResolutionTier, 256);
	TestEqual(TEXT("200 rounds up to the 256 tier"), FTDFluidGpuBatch::MakeKey(FirstFrame.Actors[1].Settings).ResolutionTier, 256);
	TestTrue(TEXT("Same tier and iterations share a key"), FTDFluidGpuBatch::MakeKey(FirstFrame.Actors[1].Settings) == SharedKey);
	TestFalse(TEXT("Different iterations split the key"), TwelveIterationKey == SharedKey);
	TestFalse(TEXT("Different resolution tiers split the key"), FTDFluidGpuBatch::MakeKey(FirstFrame.Actors[3].Settings) == SharedKey);
	TestTrue(TEXT("Painter keys ignore pressure iterations"), FTDFluidGpuBatch::MakeKey(FirstFrame.Actors[5].Settings) == PainterKey);
	VerifyPressureSolverKeys(*this);
	TestFalse(TEXT("Painter and fluid keys differ"), PainterKey == FTDFluidGpuBatch::MakeKey(FirstFrame.Actors[3].Settings));

	TArray<FTDFluidGpuBatch> Batches;
	FTDFluidGpuPreparedFrame PreparedFrame;
	FTDFluidGpuBatch::PrepareFrame(FirstFrame, Batches, PreparedFrame);
	TestEqual(TEXT("First frame forms four batches"), Batches.Num(), 4);
	TestEqual(TEXT("Every actor gets one bin table section"), PreparedFrame.StampBins.BinRanges.Num(), FirstFrame.Actors.Num() * TDFluidGpuSolverConstants::StampBinCount);
	TestEqual(TEXT("Every actor gets one readback tile"), PreparedFrame.ReadbackLayout.TileCount, FirstFrame.Actors.Num());

	const FTDFluidGpuBatch* SharedBatch = FindBatch(Batches, SharedKey);
	const FTDFluidGpuBatch* PainterBatch = FindBatch(Batches, PainterKey);
	if (!SharedBatch || !PainterBatch || SharedBatch->GetFrame().Parameters.IsEmpty() || PainterBatch->GetFrame().Parameters.IsEmpty())
	{
		AddError(TEXT("Shared or painter batch is missing or empty after the first frame"));
		return false;
	}

	TestEqual(TEXT("Shared batch steps two actors"), SharedBatch->GetFrame().Entries.Num(), 2);
	TestEqual(TEXT("First shared actor takes slice 0"), SharedBatch->FindSlice(11), 0);
	TestEqual(TEXT("Second shared actor takes slice 1"), SharedBatch->FindSlice(12), 1);
	TestTrue(TEXT("New slice 0 is reset"), IsSliceReset(*SharedBatch, 0));
	TestTrue(TEXT("New slice 1 is reset"), IsSliceReset(*SharedBatch, 1));
	TestEqual(TEXT("Slice capacity rounds up"), SharedBatch->GetRequiredSliceCapacity(), 4);
	TestEqual(TEXT("Shared parameters carry the tier"), SharedBatch->GetFrame().Parameters[0].Resolution, 256);
	TestEqual(TEXT("Shared parameters carry the iterations"), static_cast<int32>(SharedBatch->GetFrame().Parameters[0].PressureIterations), 8);
	TestTrue(TEXT("Shared parameters mark the reset"), HasActorFlag(SharedBatch->GetFrame().Parameters[0], ETDFluidGpuActorFlags::NeedsReset));
	TestEqual(TEXT("Painter batch steps two actors"), PainterBatch->GetFrame().Entries.Num(), 2);
	TestEqual(TEXT("Painter parameters carry the 1024 tier"), PainterBatch->GetFrame().Parameters[0].Resolution, 1024);
	TestTrue(TEXT("Painter parameters mark the painter"), HasActorFlag(PainterBatch->GetFrame().Parameters[0], ETDFluidGpuActorFlags::IsSimplePainter));

	FTDFluidGpuFrameInputs SecondFrame;
	SecondFrame.Actors.Add(MakeBatchTestActor(11, ETDFluidGpuSimMode::FluidSim, 256, 8));
	SecondFrame.Actors.Add(MakeBatchTestActor(13, ETDFluidGpuSimMode::FluidSim, 256, 12));
	SecondFrame.Actors.Add(MakeBatchTestActor(14, ETDFluidGpuSimMode::FluidSim, 512, 8));
	SecondFrame.Actors.Add(MakeBatchTestActor(15, ETDFluidGpuSimMode::SimplePainter, 1024, 8));
	SecondFrame.Actors.Add(MakeBatchTestActor(16, ETDFluidGpuSimMode::SimplePainter, 1000, 12));
	SecondFrame.Actors.Add(MakeBatchTestActor(17, ETDFluidGpuSimMode::FluidSim, 256, 8));
	SecondFrame.ReleasedActorKeys.Add(12);
	FTDFluidGpuBatch::PrepareFrame(SecondFrame, Batches, PreparedFrame);

	SharedBatch = FindBatch(Batches, SharedKey);
	if (!SharedBatch)
	{
		AddError(TEXT("Shared batch is missing after the second frame"));
		return false;
	}

	TestEqual(TEXT("Second frame keeps four batches"), Batches.Num(), 4);
	TestEqual(TEXT("Existing actor keeps its slice"), SharedBatch->FindSlice(11), 0);
	TestEqual(TEXT("Released actor loses its slice"), SharedBatch->FindSlice(12), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("New actor reuses the released slice"), SharedBatch->FindSlice(17), 1);
	TestFalse(TEXT("Existing slice is not reset again"), IsSliceReset(*SharedBatch, 0));
	TestTrue(TEXT("Reused slice is reset"), IsSliceReset(*SharedBatch, 1));

	FTDFluidGpuFrameInputs ThirdFrame = SecondFrame;
	ThirdFrame.ReleasedActorKeys.Reset();
	ThirdFrame.Actors[1].Settings.PressureIterations = 8;
	FTDFluidGpuBatch::PrepareFrame(ThirdFrame, Batches, PreparedFrame);

	SharedBatch = FindBatch(Batches, SharedKey);
	if (!SharedBatch)
	{
		AddError(TEXT("Shared batch is missing after the third frame"));
		return false;
	}

	TestEqual(TEXT("Emptied batch is removed"), Batches.Num(), 3);
	TestNull(TEXT("Twelve iteration batch is gone"), FindBatch(Batches, TwelveIterationKey));
	TestEqual(TEXT("Actor that changed iterations moves into the shared batch"), SharedBatch->FindSlice(13), 2);
	TestTrue(TEXT("Moved actor is reset"), IsSliceReset(*SharedBatch, 2));
	TestFalse(TEXT("Stable actors are not reset"), IsSliceReset(*SharedBatch, 1));

	const TArray<FTDFluidGpuActorParameters>& SharedParameters = SharedBatch->GetFrame().Parameters;
	TestEqual(TEXT("Shared batch steps three actors"), SharedParameters.Num(), 3);
	for (int32 EntryIndex = 0; EntryIndex < SharedParameters.Num(); ++EntryIndex)
	{
		TestEqual(TEXT("Entries are ordered by slice"), SharedParameters[EntryIndex].SliceIndex, EntryIndex);
	}

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuMemoryEstimateWithinBudgetTest,
	"TDGame.FluidFX.Gpu.MemoryEstimateWithinBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuMemoryEstimateWithinBudgetTest::RunTest(const FString& Parameters)
{
	constexpr int64 MebiByte = 1024 * 1024;
	constexpr int64 FluidSimResidentBytesPerCell = 58;
	constexpr int64 MultigridLevelKibibytes512 = 1360;
	constexpr int64 PainterBytesPerCell = 16;

	const FTDFluidGpuMemoryEstimate FluidEstimate = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 512);
	const int64 FluidResidentBytes = FluidEstimate.StateBytes + FluidEstimate.OutputBytes;
	TestEqual(TEXT("512 FluidSim keeps 58 resident and output bytes per cell"), FluidResidentBytes, static_cast<int64>(512) * 512 * FluidSimResidentBytesPerCell);
	TestEqual(TEXT("512 FluidSim counts the multigrid level textures as transient bytes"), FluidEstimate.TransientBytes, FTDFluidGpuBatch::ComputeMultigridTransientBytes(512));
	TestEqual(TEXT("512 multigrid level textures take 1360 KiB"), FluidEstimate.TransientBytes, MultigridLevelKibibytes512 * 1024);
	AddInfo(FString::Printf(TEXT("512 FluidSim estimate: state %lld B, transient %lld B, output %lld B, readback %lld B, total %.2f MiB, margin %.2f MiB"), FluidEstimate.StateBytes, FluidEstimate.TransientBytes, FluidEstimate.OutputBytes, FluidEstimate.ReadbackBytes, static_cast<double>(FluidEstimate.GetTotalBytes()) / MebiByte, static_cast<double>(16 * MebiByte - FluidEstimate.GetTotalBytes()) / MebiByte));
	TestTrue(FString::Printf(TEXT("512 FluidSim total %lld bytes is within 16 MiB"), FluidEstimate.GetTotalBytes()), FluidEstimate.GetTotalBytes() <= 16 * MebiByte);

	const FTDFluidGpuMemoryEstimate PainterEstimate = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::SimplePainter, 1024);
	TestTrue(TEXT("1024 painter uses at least 16 bytes per cell"), PainterEstimate.StateBytes + PainterEstimate.OutputBytes >= static_cast<int64>(1024) * 1024 * PainterBytesPerCell);
	TestTrue(FString::Printf(TEXT("1024 painter total %lld bytes is within 17 MiB"), PainterEstimate.GetTotalBytes()), PainterEstimate.GetTotalBytes() <= 17 * MebiByte);

	const FTDFluidGpuMemoryEstimate RoundedEstimate = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 400);
	TestEqual(TEXT("Estimates use the resolution tier"), RoundedEstimate.GetTotalBytes(), FluidEstimate.GetTotalBytes());
	return !HasAnyErrors();
}

namespace TDFluidGpuRhiTestSupport
{
	constexpr int32 FramesPerRenderCommand = 256;
	constexpr int32 RichTestResolution = 64;
	constexpr int32 ShiftIntervalFrames = 20;
	constexpr float SixtyHertzDeltaTime = 1.0f / 60.0f;

	using FTDFluidGpuRhiFrameBuilder = TFunction<void(int32, FTDFluidGpuFrameInputs&)>;

	struct FTDFluidGpuRhiRunResult
	{
		bool bHasReadback = false;
		FTDFluidGpuReadbackFrame Readback;
		int64 StateBytes = 0;
		int32 SliceCapacity = 0;
		int32 BatchCount = 0;
		int32 FinalSteppedEntryCount = 0;
		int32 WrittenOutputCount = 0;
	};

	struct FTDFluidGpuRhiRunState
	{
		TArray<FTDFluidGpuBatch> Batches;
		TUniquePtr<FTDFluidGpuReadbackRing> ReadbackRing = MakeUnique<FTDFluidGpuReadbackRing>();
		FTDFluidGpuRhiRunResult Result;
	};

	bool CanRunRhiTest(FAutomationTestBase& Test, const TCHAR* TestName)
	{
		const bool bHasRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidGpuRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), TestName, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), TestName));
		return false;
	}

	FTDFluidGpuRhiRunResult RunGpuFrames(int32 FrameCount, const FTDFluidGpuRhiFrameBuilder& BuildFrame)
	{
		TSharedRef<FTDFluidGpuRhiRunState, ESPMode::ThreadSafe> RunState = MakeShared<FTDFluidGpuRhiRunState, ESPMode::ThreadSafe>();
		for (int32 FirstFrame = 0; FirstFrame < FrameCount; FirstFrame += FramesPerRenderCommand)
		{
			const int32 EndFrame = FMath::Min(FirstFrame + FramesPerRenderCommand, FrameCount);
			ENQUEUE_RENDER_COMMAND(TDFluidGpuRhiTestFrames)(
				[RunState, BuildFrame, FirstFrame, EndFrame, FrameCount](FRHICommandListImmediate& RHICmdList)
				{
					for (int32 FrameIndex = FirstFrame; FrameIndex < EndFrame; ++FrameIndex)
					{
						FTDFluidGpuFrameInputs FrameInputs;
						FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
						BuildFrame(FrameIndex, FrameInputs);

						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuRhiTest"));
						const FTDFluidGpuFrameOutputs Outputs = AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, RunState->Batches);
						RunState->Result.FinalSteppedEntryCount = Outputs.SteppedEntryCount;
						RunState->Result.WrittenOutputCount += Outputs.WrittenOutputCount;
						if (FrameIndex == FrameCount - 1)
						{
							RunState->ReadbackRing->Enqueue(GraphBuilder, Outputs);
						}
						GraphBuilder.Execute();
						RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
					}

					if (EndFrame < FrameCount)
					{
						return;
					}

					RHICmdList.SubmitAndBlockUntilGPUIdle();
					RunState->Result.bHasReadback = RunState->ReadbackRing->TryReadLatest(RunState->Result.Readback);
					for (const FTDFluidGpuBatch& Batch : RunState->Batches)
					{
						RunState->Result.StateBytes += ComputeTDFluidGpuBatchStateBytes(Batch);
						RunState->Result.SliceCapacity += Batch.StateTextures.SliceCapacity;
					}
					RunState->Result.BatchCount = RunState->Batches.Num();
					RunState->Batches.Empty();
					RunState->ReadbackRing.Reset();
				});
			FlushRenderingCommands();
		}
		return RunState->Result;
	}

	TArray<FFloat16Color> ExtractTile(const FTDFluidGpuReadbackFrame& Frame, int32 TileIndex)
	{
		TArray<FFloat16Color> Tile;
		const int32 TileResolution = TDFluidGpuSolverConstants::ReadbackTileResolution;
		const FIntPoint AtlasSize = Frame.Layout.GetAtlasSize();
		const FIntPoint Origin = Frame.Layout.GetTileOrigin(TileIndex);
		if (Frame.Texels.Num() < AtlasSize.X * AtlasSize.Y || TileIndex >= Frame.Layout.TileCount)
		{
			return Tile;
		}

		Tile.Reserve(TileResolution * TileResolution);
		for (int32 TexelY = 0; TexelY < TileResolution; ++TexelY)
		{
			for (int32 TexelX = 0; TexelX < TileResolution; ++TexelX)
			{
				Tile.Add(Frame.Texels[(Origin.Y + TexelY) * AtlasSize.X + Origin.X + TexelX]);
			}
		}
		return Tile;
	}

	bool AreTileTexelsBitIdentical(const TArray<FFloat16Color>& Left, const TArray<FFloat16Color>& Right)
	{
		return Left.Num() == Right.Num() && FMemory::Memcmp(Left.GetData(), Right.GetData(), Left.Num() * sizeof(FFloat16Color)) == 0;
	}

	bool IsTexelBitIdentical(const FFloat16Color& Left, const FFloat16Color& Right)
	{
		return FMemory::Memcmp(&Left, &Right, sizeof(FFloat16Color)) == 0;
	}

	float FindMaximumPrimaryDensity(const TArray<FFloat16Color>& Tile)
	{
		float MaximumDensity = 0.0f;
		for (const FFloat16Color& Texel : Tile)
		{
			MaximumDensity = FMath::Max(MaximumDensity, Texel.R.GetFloat());
		}
		return MaximumDensity;
	}

	float SumPrimaryDensity(const TArray<FFloat16Color>& Tile)
	{
		float DensitySum = 0.0f;
		for (const FFloat16Color& Texel : Tile)
		{
			DensitySum += Texel.R.GetFloat();
		}
		return DensitySum;
	}

	FTDFluidGpuStampInput MakeDensityStamp(const FVector2D& CenterPlaneCm, float RadiusCm, float DensityStrength)
	{
		FTDFluidGpuStampInput Stamp;
		Stamp.CenterPlaneCm = CenterPlaneCm;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = DensityStrength;
		Stamp.VelocityStrength = 0.0f;
		return Stamp;
	}

	FVector2D ComputeCellSize(const FTDFluidGpuSimulationSettings& Settings)
	{
		return Settings.ExtentCm / static_cast<double>(Settings.Resolution);
	}

	void AddRichStamps(FTDFluidGpuActorInput& Actor, int32 FrameIndex)
	{
		FRandomStream Random(static_cast<int32>(Actor.ActorKey * 7919u + static_cast<uint64>(FrameIndex) * 104729u));
		for (int32 StampIndex = 0; StampIndex < 3; ++StampIndex)
		{
			const FVector2D Offset(Random.FRandRange(-1200.0f, 1200.0f), Random.FRandRange(-1200.0f, 1200.0f));
			FTDFluidGpuStampInput& Stamp = Actor.Stamps.Add_GetRef(MakeDensityStamp(Actor.WindowCenterPlaneCm + Offset, Random.FRandRange(150.0f, 420.0f), Random.FRandRange(0.6f, 1.0f)));
			Stamp.VelocityPlaneCmPerSec = FVector2D(Random.FRandRange(-400.0f, 400.0f), Random.FRandRange(-400.0f, 400.0f));
			Stamp.VelocityStrength = 1.0f;
			Stamp.RadialVelocityCmPerSec = StampIndex == 1 ? Random.FRandRange(-300.0f, 300.0f) : 0.0f;
			Stamp.bWritesSelectiveDensity = StampIndex == 2;
		}

		if (FrameIndex % 7 == 3)
		{
			const FVector2D EraseOffset(Random.FRandRange(-800.0f, 800.0f), Random.FRandRange(-800.0f, 800.0f));
			FTDFluidGpuStampInput& EraseStamp = Actor.Stamps.Add_GetRef(MakeDensityStamp(Actor.WindowCenterPlaneCm + EraseOffset, 350.0f, -0.8f));
			EraseStamp.bWritesSelectiveDensity = true;
		}
	}

	void AddTerrainForWholeWindow(FTDFluidGpuActorInput& Actor)
	{
		const int32 Resolution = Actor.Settings.Resolution;
		const FVector2D CellSize = ComputeCellSize(Actor.Settings);
		const FVector2D MinimumCorner = Actor.WindowCenterPlaneCm - 0.5 * Actor.Settings.ExtentCm;
		Actor.TerrainElevationUpdates.Reserve(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FVector2D CellCenter = MinimumCorner + FVector2D(CellX + 0.5, CellY + 0.5) * CellSize;
				FTDFluidGpuTerrainCellUpdate& Update = Actor.TerrainElevationUpdates.AddDefaulted_GetRef();
				Update.Cell = FIntPoint(CellX, CellY);
				Update.ElevationCm = static_cast<float>(0.2 * CellCenter.X + 0.05 * CellCenter.Y + 30000.0);
			}
		}
	}

	FTDFluidGpuActorInput MakeRichFluidActor(uint64 ActorKey, int32 Variant, int32 FrameIndex)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ActorKey;
		Actor.Settings.Resolution = RichTestResolution;
		Actor.Settings.ExtentCm = FVector2D(3200.0 + 40.0 * Variant, 3000.0);
		Actor.Settings.PressureIterations = 8;
		Actor.Settings.VorticityStrength = 1.5f;
		Actor.Settings.DensityDissipation = 0.2f;
		Actor.Settings.VelocityDamping = 0.05f;
		Actor.Settings.TerrainSlopeGravity = 300.0f;
		Actor.Settings.FlowNoiseAccelerationCmPerSec2 = 250.0f + 5.0f * Variant;
		Actor.Settings.BuoyancyCmPerSec2 = 180.0f;
		Actor.Settings.AmbientDensity = 0.04f;
		Actor.Settings.AmbientRefillPerSec = 0.5f;
		Actor.Settings.AmbientNoiseAmount = 0.4f;
		Actor.Settings.OutflowBandRatio = 0.1f;
		Actor.Settings.OutflowRatePerSec = 1.5f;
		Actor.PlaneUpDirection = FVector2D(0.0, 1.0);
		Actor.DeltaTimeSeconds = SixtyHertzDeltaTime;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(SixtyHertzDeltaTime);

		const int32 ShiftCount = FrameIndex / ShiftIntervalFrames;
		const bool bShiftsThisFrame = FrameIndex > 0 && FrameIndex % ShiftIntervalFrames == 0;
		const FIntPoint ShiftPerInterval(2, -1);
		const FVector2D CellSize = ComputeCellSize(Actor.Settings);
		Actor.WindowCenterPlaneCm = FVector2D(ShiftPerInterval.X * ShiftCount * CellSize.X, ShiftPerInterval.Y * ShiftCount * CellSize.Y);
		Actor.ShiftCells = bShiftsThisFrame ? ShiftPerInterval : FIntPoint::ZeroValue;

		Actor.bHasObstacleListChanged = FrameIndex == 0;
		FTDFluidGpuObstacleInput& Circle = Actor.Obstacles.AddDefaulted_GetRef();
		Circle.CenterPlaneCm = FVector2D(300.0 + 10.0 * Variant, -200.0);
		Circle.RadiusCm = 250.0f;
		FTDFluidGpuObstacleInput& Box = Actor.Obstacles.AddDefaulted_GetRef();
		Box.CenterPlaneCm = FVector2D(-600.0, 500.0);
		Box.BoxHalfExtentsCm = FVector2D(200.0, 120.0);
		Box.bIsBox = true;

		Actor.bHasTerrainElevation = true;
		if (FrameIndex % ShiftIntervalFrames == 0)
		{
			AddTerrainForWholeWindow(Actor);
		}

		AddRichStamps(Actor, FrameIndex);
		return Actor;
	}

	FTDFluidGpuActorInput MakePainterActor(uint64 ActorKey, int32 FrameIndex)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ActorKey;
		Actor.Settings.SimMode = ETDFluidGpuSimMode::SimplePainter;
		Actor.Settings.Resolution = RichTestResolution;
		Actor.Settings.ExtentCm = FVector2D(2400.0, 2400.0);
		Actor.Settings.DensityDissipation = 0.3f;
		Actor.Settings.AmbientDensity = 0.02f;
		Actor.Settings.AmbientRefillPerSec = 0.2f;
		Actor.Settings.AmbientNoiseAmount = 0.5f;
		Actor.DeltaTimeSeconds = SixtyHertzDeltaTime;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(SixtyHertzDeltaTime);
		Actor.bHasObstacleListChanged = FrameIndex == 0;
		FTDFluidGpuObstacleInput& Circle = Actor.Obstacles.AddDefaulted_GetRef();
		Circle.CenterPlaneCm = FVector2D(-200.0, 150.0);
		Circle.RadiusCm = 180.0f;
		AddRichStamps(Actor, FrameIndex);
		return Actor;
	}

	FTDFluidGpuActorInput MakeStillFluidActor(uint64 ActorKey, int32 Resolution, float DeltaTimeSeconds, float DensityDissipation)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ActorKey;
		Actor.Settings.Resolution = Resolution;
		Actor.Settings.ExtentCm = FVector2D(3200.0, 3200.0);
		Actor.Settings.PressureIterations = 8;
		Actor.Settings.VorticityStrength = 0.0f;
		Actor.Settings.DensityDissipation = DensityDissipation;
		Actor.Settings.VelocityDamping = 0.0f;
		Actor.Settings.TerrainSlopeGravity = 0.0f;
		Actor.DeltaTimeSeconds = DeltaTimeSeconds;
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuRunToRunDeterministicTest,
	"TDGame.FluidFXRhi.RunToRunDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuRunToRunDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuRhiTestSupport;

	if (!CanRunRhiTest(*this, TEXT("RunToRunDeterministic")))
	{
		return true;
	}

	constexpr int32 FrameCount = 61;
	const FTDFluidGpuRhiFrameBuilder BuildFrame = [](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		OutFrame.Actors.Add(MakeRichFluidActor(1, 0, FrameIndex));
		OutFrame.Actors.Add(MakePainterActor(2, FrameIndex));
	};

	const FTDFluidGpuRhiRunResult FirstRun = RunGpuFrames(FrameCount, BuildFrame);
	const FTDFluidGpuRhiRunResult SecondRun = RunGpuFrames(FrameCount, BuildFrame);
	if (!FirstRun.bHasReadback || !SecondRun.bHasReadback)
	{
		AddError(TEXT("Readback atlas was not ready after the GPU idled"));
		return false;
	}

	TestEqual(TEXT("Both actors stepped on the final frame"), FirstRun.FinalSteppedEntryCount, 2);
	TestTrue(TEXT("Fluid tile holds density after 61 frames"), SumPrimaryDensity(ExtractTile(FirstRun.Readback, 0)) > 0.0f);
	TestTrue(TEXT("Painter tile holds density after 61 frames"), SumPrimaryDensity(ExtractTile(FirstRun.Readback, 1)) > 0.0f);
	TestTrue(TEXT("Two runs produce bit identical readback atlases"), AreTileTexelsBitIdentical(FirstRun.Readback.Texels, SecondRun.Readback.Texels));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuBatchSliceIndependenceTest,
	"TDGame.FluidFXRhi.BatchSliceIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuBatchSliceIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuRhiTestSupport;

	if (!CanRunRhiTest(*this, TEXT("BatchSliceIndependence")))
	{
		return true;
	}

	constexpr int32 FrameCount = 41;
	constexpr int32 BatchActorCount = 49;
	constexpr int32 ObservedVariant = 24;
	const FTDFluidGpuRhiRunResult AloneRun = RunGpuFrames(FrameCount, [](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		OutFrame.Actors.Add(MakeRichFluidActor(100 + ObservedVariant, ObservedVariant, FrameIndex));
	});
	const FTDFluidGpuRhiRunResult BatchRun = RunGpuFrames(FrameCount, [](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		for (int32 Variant = 0; Variant < BatchActorCount; ++Variant)
		{
			OutFrame.Actors.Add(MakeRichFluidActor(100 + Variant, Variant, FrameIndex));
		}
	});
	if (!AloneRun.bHasReadback || !BatchRun.bHasReadback)
	{
		AddError(TEXT("Readback atlas was not ready after the GPU idled"));
		return false;
	}

	TestEqual(TEXT("All 49 actors share one batch"), BatchRun.BatchCount, 1);
	TestEqual(TEXT("All 49 actors stepped on the final frame"), BatchRun.FinalSteppedEntryCount, BatchActorCount);
	const TArray<FFloat16Color> AloneTile = ExtractTile(AloneRun.Readback, 0);
	const TArray<FFloat16Color> BatchTile = ExtractTile(BatchRun.Readback, ObservedVariant);
	TestTrue(TEXT("Observed tile holds density"), SumPrimaryDensity(AloneTile) > 0.0f);
	TestTrue(TEXT("Slice result is bit identical alone and inside a 49 actor batch"), AreTileTexelsBitIdentical(AloneTile, BatchTile));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuShiftMovesStateExactlyTest,
	"TDGame.FluidFXRhi.ShiftMovesStateExactly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuShiftMovesStateExactlyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuRhiTestSupport;

	if (!CanRunRhiTest(*this, TEXT("ShiftMovesStateExactly")))
	{
		return true;
	}

	constexpr int32 Resolution = 64;
	const FIntPoint ShiftCells(3, -2);
	const FTDFluidGpuRhiFrameBuilder BuildFrame = [ShiftCells](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		FTDFluidGpuActorInput Actor = MakeStillFluidActor(7, Resolution, SixtyHertzDeltaTime, 0.0f);
		const FVector2D CellSize = ComputeCellSize(Actor.Settings);
		if (FrameIndex == 0)
		{
			Actor.Stamps.Add(MakeDensityStamp(FVector2D(-400.0, 300.0), 600.0f, 0.9f));
			Actor.Stamps.Add(MakeDensityStamp(FVector2D(1450.0, -1400.0), 300.0f, 1.0f));
			FTDFluidGpuStampInput& SelectiveStamp = Actor.Stamps.Add_GetRef(MakeDensityStamp(FVector2D(-1500.0, 1500.0), 400.0f, 0.7f));
			SelectiveStamp.bWritesSelectiveDensity = true;
		}
		if (FrameIndex == 1)
		{
			Actor.ShiftCells = ShiftCells;
			Actor.WindowCenterPlaneCm = FVector2D(ShiftCells.X * CellSize.X, ShiftCells.Y * CellSize.Y);
		}
		OutFrame.Actors.Add(Actor);
	};

	const FTDFluidGpuRhiRunResult BeforeShift = RunGpuFrames(1, BuildFrame);
	const FTDFluidGpuRhiRunResult AfterShift = RunGpuFrames(2, BuildFrame);
	if (!BeforeShift.bHasReadback || !AfterShift.bHasReadback)
	{
		AddError(TEXT("Readback atlas was not ready after the GPU idled"));
		return false;
	}

	const TArray<FFloat16Color> BeforeTile = ExtractTile(BeforeShift.Readback, 0);
	const TArray<FFloat16Color> AfterTile = ExtractTile(AfterShift.Readback, 0);
	if (BeforeTile.Num() != Resolution * Resolution || AfterTile.Num() != Resolution * Resolution)
	{
		AddError(TEXT("Readback tiles do not cover the 64 cell grid"));
		return false;
	}

	TestTrue(TEXT("Stamped state holds density before the shift"), SumPrimaryDensity(BeforeTile) > 0.0f);
	int32 MovedMismatchCount = 0;
	int32 IncomingMismatchCount = 0;
	const FFloat16Color InitialTexel(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const FIntPoint SourceCell(CellX + ShiftCells.X, CellY + ShiftCells.Y);
			const FFloat16Color& AfterTexel = AfterTile[CellY * Resolution + CellX];
			const bool bHasSourceCell = SourceCell.X >= 0 && SourceCell.Y >= 0 && SourceCell.X < Resolution && SourceCell.Y < Resolution;
			if (!bHasSourceCell)
			{
				IncomingMismatchCount += IsTexelBitIdentical(AfterTexel, InitialTexel) ? 0 : 1;
				continue;
			}

			MovedMismatchCount += IsTexelBitIdentical(AfterTexel, BeforeTile[SourceCell.Y * Resolution + SourceCell.X]) ? 0 : 1;
		}
	}

	TestEqual(TEXT("Every kept cell moved by exactly the shift"), MovedMismatchCount, 0);
	TestEqual(TEXT("Every incoming cell holds the initial value"), IncomingMismatchCount, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuSlowDissipationReachesZeroTest,
	"TDGame.FluidFXRhi.SlowDissipationReachesZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuSlowDissipationReachesZeroTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuRhiTestSupport;

	if (!CanRunRhiTest(*this, TEXT("SlowDissipationReachesZero")))
	{
		return true;
	}

	constexpr float DissipationPerSecond = 0.01f;
	constexpr int32 HundredTwentyHertzFrameCount = 72000;
	constexpr float MaximumRemainingDensity = 0.005f;
	constexpr float MinimumRemainingDensity = 0.0005f;
	const FTDFluidGpuRhiRunResult Run = RunGpuFrames(HundredTwentyHertzFrameCount, [](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		const bool bStepsSixtyHertzActor = FrameIndex % 2 == 1;
		FTDFluidGpuActorInput SixtyHertzActor = MakeStillFluidActor(60, 16, bStepsSixtyHertzActor ? 1.0f / 60.0f : 0.0f, DissipationPerSecond);
		FTDFluidGpuActorInput HundredTwentyHertzActor = MakeStillFluidActor(120, 16, 1.0f / 120.0f, DissipationPerSecond);
		if (FrameIndex <= 1)
		{
			SixtyHertzActor.Stamps.Add(MakeDensityStamp(FVector2D::ZeroVector, 3200.0f, 50.0f));
			HundredTwentyHertzActor.Stamps.Add(MakeDensityStamp(FVector2D::ZeroVector, 3200.0f, 50.0f));
		}
		OutFrame.Actors.Add(SixtyHertzActor);
		OutFrame.Actors.Add(HundredTwentyHertzActor);
	});
	if (!Run.bHasReadback)
	{
		AddError(TEXT("Readback atlas was not ready after the GPU idled"));
		return false;
	}

	const float SixtyHertzDensity = FindMaximumPrimaryDensity(ExtractTile(Run.Readback, 0));
	const float HundredTwentyHertzDensity = FindMaximumPrimaryDensity(ExtractTile(Run.Readback, 1));
	AddInfo(FString::Printf(TEXT("After 600 virtual seconds at k=0.01/s: 60 Hz max density %.6f, 120 Hz max density %.6f"), SixtyHertzDensity, HundredTwentyHertzDensity));
	TestTrue(TEXT("60 Hz density dissipates to at most 0.005"), SixtyHertzDensity <= MaximumRemainingDensity);
	TestTrue(TEXT("120 Hz density dissipates to at most 0.005"), HundredTwentyHertzDensity <= MaximumRemainingDensity);
	TestTrue(TEXT("60 Hz density follows the multiplicative decay instead of vanishing early"), SixtyHertzDensity >= MinimumRemainingDensity);
	TestTrue(TEXT("120 Hz density follows the multiplicative decay instead of vanishing early"), HundredTwentyHertzDensity >= MinimumRemainingDensity);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuResolution512RunsWithinMemoryTest,
	"TDGame.FluidFXRhi.Resolution512RunsWithinMemory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuResolution512RunsWithinMemoryTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuRhiTestSupport;

	if (!CanRunRhiTest(*this, TEXT("Resolution512RunsWithinMemory")))
	{
		return true;
	}

	constexpr int32 Resolution = 512;
	constexpr int32 FrameCount = 3;
	constexpr int64 MebiByte = 1024 * 1024;
	const auto CreateOutputTarget = []()
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		Target->bSupportsUAV = true;
		Target->bCanCreateUAV = true;
		Target->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return TStrongObjectPtr<UTextureRenderTarget2D>(Target);
	};
	const TStrongObjectPtr<UTextureRenderTarget2D> DensityPressureTarget = CreateOutputTarget();
	const TStrongObjectPtr<UTextureRenderTarget2D> VelocityTarget = CreateOutputTarget();
	FTextureRenderTargetResource* DensityPressureResource = DensityPressureTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* VelocityResource = VelocityTarget->GameThread_GetRenderTargetResource();
	FlushRenderingCommands();

	const FTDFluidGpuRhiRunResult Run = RunGpuFrames(FrameCount, [DensityPressureResource, VelocityResource](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		FTDFluidGpuActorInput Actor = MakeRichFluidActor(512, 0, FrameIndex);
		Actor.Settings.Resolution = Resolution;
		Actor.TerrainElevationUpdates.Reset();
		Actor.DensityPressureOutput = DensityPressureResource;
		Actor.VelocityOutput = VelocityResource;
		OutFrame.Actors.Add(Actor);
	});

	const int64 CellCount = static_cast<int64>(Resolution) * Resolution;
	const int64 OutputBytes = 2 * CellCount * GPixelFormats[PF_FloatRGBA].BlockBytes;
	const int64 TransientBytes = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, Resolution).TransientBytes;
	const int64 StateBytesPerActor = Run.SliceCapacity > 0 ? Run.StateBytes / Run.SliceCapacity : 0;
	const int64 TotalBytesPerActor = StateBytesPerActor + OutputBytes + TransientBytes;
	AddInfo(FString::Printf(TEXT("512 actor: state %lld B per slice (capacity %d, %lld B allocated), outputs %lld B, pressure solver transient %lld B (multigrid levels %lld B), total %lld B"), StateBytesPerActor, Run.SliceCapacity, Run.StateBytes, OutputBytes, TransientBytes, FTDFluidGpuBatch::ComputeMultigridTransientBytes(Resolution), TotalBytesPerActor));
	TestEqual(TEXT("A lone 512 actor gets an exact one slice capacity"), Run.SliceCapacity, 1);
	TestEqual(TEXT("Output render targets were written every frame"), Run.WrittenOutputCount, FrameCount);
	TestTrue(TEXT("Readback atlas arrived"), Run.bHasReadback);
	TestTrue(TEXT("State textures were allocated"), StateBytesPerActor > 0);
	TestTrue(FString::Printf(TEXT("512 actor render target bytes %lld are within 16 MiB"), TotalBytesPerActor), TotalBytesPerActor <= 16 * MebiByte);
	return !HasAnyErrors();
}


namespace TDFluidGpuMultigridTestSupport
{
	using namespace TDFluidGpuRhiTestSupport;

	constexpr int32 MultigridRichTestResolution = 128;

	struct FTDFluidGpuHalfPairTexel
	{
		FFloat16 X;
		FFloat16 Y;
	};

	struct FTDFluidGpuMultigridProbeResult
	{
		bool bIsValid = false;
		int32 Resolution = 0;
		FVector2f RelativeCellSpacing = FVector2f::ZeroVector;
		FVector2f AxisWeights = FVector2f::ZeroVector;
		TArray<FVector2f> Velocity;
		TArray<float> ObstacleSdf;
		TArray<int32> RecordedCycleCounts;
		TArray<TArray<float>> Pressures;
	};

	struct FTDFluidGpuPoissonSystem
	{
		int32 Resolution = 0;
		FVector2D AxisWeights = FVector2D::ZeroVector;
		TArray<bool> FluidCells;
		TArray<double> Rhs;
		TArray<int32> ComponentIds;
		TArray<int32> ComponentSizes;
	};

	struct FTDFluidGpuMultigridCase
	{
		const TCHAR* Name = TEXT("");
		int32 Resolution = 0;
		FVector2D ExtentCm = FVector2D::ZeroVector;
		bool bKeepsRichObstacles = true;
		bool bAddsAdversarialObstacles = false;
		double MaximumErrorReductionRate = 1.0;
	};

	template <typename TTexel, typename TValue, typename TConvert>
	bool ReadProbeTexels(FRHIGPUTextureReadback* Readback, int32 Resolution, TConvert Convert, TArray<TValue>& OutValues)
	{
		if (!Readback || !Readback->IsReady())
		{
			return false;
		}

		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const TTexel* Texels = static_cast<const TTexel*>(Readback->Lock(RowPitchInPixels, &BufferHeight));
		if (!Texels || RowPitchInPixels < Resolution || BufferHeight < Resolution)
		{
			Readback->Unlock();
			return false;
		}

		OutValues.SetNum(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				OutValues[CellY * Resolution + CellX] = Convert(Texels[CellY * RowPitchInPixels + CellX]);
			}
		}
		Readback->Unlock();
		return true;
	}

	FTDFluidGpuMultigridProbeResult RunMultigridProbe(int32 WarmupFrameCount, const FTDFluidGpuRhiFrameBuilder& BuildFrame, const TArray<int32>& RecordedCycleCounts)
	{
		TSharedRef<FTDFluidGpuMultigridProbeResult, ESPMode::ThreadSafe> Result = MakeShared<FTDFluidGpuMultigridProbeResult, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuMultigridProbe)(
			[Result, BuildFrame, WarmupFrameCount, RecordedCycleCounts](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				for (int32 FrameIndex = 0; FrameIndex < WarmupFrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					BuildFrame(FrameIndex, FrameInputs);
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuMultigridProbeWarmup"));
					AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
					GraphBuilder.Execute();
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				FTDFluidGpuBatch* FluidBatch = Batches.FindByPredicate([](const FTDFluidGpuBatch& Batch)
				{
					return Batch.GetKey().SimMode == ETDFluidGpuSimMode::FluidSim && !Batch.GetFrame().Parameters.IsEmpty();
				});
				if (!FluidBatch)
				{
					return;
				}

				const FTDFluidGpuActorParameters& Parameters = FluidBatch->GetFrame().Parameters[0];
				Result->Resolution = FluidBatch->GetKey().ResolutionTier;
				Result->RelativeCellSpacing = Parameters.RelativeCellSpacing;
				Result->AxisWeights = FVector2f(Parameters.InvSpacingXSquared, Parameters.InvSpacingYSquared);
				FTDFluidGpuMultigridProbeReadbacks Readbacks;
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuMultigridProbe"));
					AddTDFluidGpuMultigridProbePasses(GraphBuilder, *FluidBatch, 0, RecordedCycleCounts, Readbacks);
					GraphBuilder.Execute();
				}
				RHICmdList.SubmitAndBlockUntilGPUIdle();

				const int32 Resolution = Result->Resolution;
				bool bHasReadAll = ReadProbeTexels<FTDFluidGpuHalfPairTexel>(Readbacks.Velocity.Get(), Resolution, [](const FTDFluidGpuHalfPairTexel& Texel)
				{
					return FVector2f(Texel.X.GetFloat(), Texel.Y.GetFloat());
				}, Result->Velocity);
				bHasReadAll &= ReadProbeTexels<FFloat16>(Readbacks.ObstacleSdf.Get(), Resolution, [](const FFloat16& Texel)
				{
					return Texel.GetFloat();
				}, Result->ObstacleSdf);
				for (const TUniquePtr<FRHIGPUTextureReadback>& PressureReadback : Readbacks.Pressures)
				{
					bHasReadAll &= ReadProbeTexels<float>(PressureReadback.Get(), Resolution, [](const float& Texel)
					{
						return Texel;
					}, Result->Pressures.AddDefaulted_GetRef());
				}
				Result->RecordedCycleCounts = Readbacks.RecordedCycleCounts;
				Result->bIsValid = bHasReadAll && Readbacks.RecordedCycleCounts == RecordedCycleCounts;
			});
		FlushRenderingCommands();
		return *Result;
	}

	bool IsProbeFluidCell(const FTDFluidGpuMultigridProbeResult& Probe, int32 CellX, int32 CellY)
	{
		const int32 Resolution = Probe.Resolution;
		return CellX >= 0 && CellY >= 0 && CellX < Resolution && CellY < Resolution && Probe.ObstacleSdf[CellY * Resolution + CellX] > 0.0f;
	}

	double ReadProbeFluidVelocity(const FTDFluidGpuMultigridProbeResult& Probe, int32 CellX, int32 CellY, int32 Axis)
	{
		if (!IsProbeFluidCell(Probe, CellX, CellY))
		{
			return 0.0;
		}

		const FVector2f& Velocity = Probe.Velocity[CellY * Probe.Resolution + CellX];
		return Axis == 0 ? Velocity.X : Velocity.Y;
	}

	FTDFluidGpuPoissonSystem BuildCpuPoissonSystem(const FTDFluidGpuMultigridProbeResult& Probe)
	{
		const int32 Resolution = Probe.Resolution;
		const int32 CellCount = Resolution * Resolution;
		const FIntPoint NeighborOffsets[] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		FTDFluidGpuPoissonSystem System;
		System.Resolution = Resolution;
		System.AxisWeights = FVector2D(Probe.AxisWeights.X, Probe.AxisWeights.Y);
		System.FluidCells.SetNumZeroed(CellCount);
		System.Rhs.SetNumZeroed(CellCount);
		System.ComponentIds.Init(INDEX_NONE, CellCount);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const int32 CellIndex = CellY * Resolution + CellX;
				System.FluidCells[CellIndex] = IsProbeFluidCell(Probe, CellX, CellY);
				if (!System.FluidCells[CellIndex])
				{
					continue;
				}

				System.Rhs[CellIndex] = (ReadProbeFluidVelocity(Probe, CellX + 1, CellY, 0) - ReadProbeFluidVelocity(Probe, CellX - 1, CellY, 0)) / (2.0 * Probe.RelativeCellSpacing.X)
					+ (ReadProbeFluidVelocity(Probe, CellX, CellY + 1, 1) - ReadProbeFluidVelocity(Probe, CellX, CellY - 1, 1)) / (2.0 * Probe.RelativeCellSpacing.Y);
			}
		}

		TArray<int32> Queue;
		for (int32 SeedIndex = 0; SeedIndex < CellCount; ++SeedIndex)
		{
			if (!System.FluidCells[SeedIndex] || System.ComponentIds[SeedIndex] != INDEX_NONE)
			{
				continue;
			}

			const int32 ComponentId = System.ComponentSizes.Num();
			int32 ComponentSize = 0;
			Queue.Reset();
			Queue.Add(SeedIndex);
			System.ComponentIds[SeedIndex] = ComponentId;
			for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
			{
				const int32 CellIndex = Queue[QueueIndex];
				++ComponentSize;
				for (const FIntPoint& Offset : NeighborOffsets)
				{
					const int32 NeighborX = CellIndex % Resolution + Offset.X;
					const int32 NeighborY = CellIndex / Resolution + Offset.Y;
					if (!IsProbeFluidCell(Probe, NeighborX, NeighborY))
					{
						continue;
					}

					const int32 NeighborIndex = NeighborY * Resolution + NeighborX;
					if (System.ComponentIds[NeighborIndex] == INDEX_NONE)
					{
						System.ComponentIds[NeighborIndex] = ComponentId;
						Queue.Add(NeighborIndex);
					}
				}
			}
			System.ComponentSizes.Add(ComponentSize);
		}
		return System;
	}

	TArray<double> ComputeCpuResidual(const FTDFluidGpuPoissonSystem& System, const TArray<float>& Pressure)
	{
		const int32 Resolution = System.Resolution;
		const FIntPoint NeighborOffsets[] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		TArray<double> Residual;
		Residual.SetNumZeroed(Resolution * Resolution);
		for (int32 CellIndex = 0; CellIndex < Residual.Num(); ++CellIndex)
		{
			if (!System.FluidCells[CellIndex] || System.ComponentSizes[System.ComponentIds[CellIndex]] < 2)
			{
				continue;
			}

			double AppliedOperator = 0.0;
			for (const FIntPoint& Offset : NeighborOffsets)
			{
				const int32 NeighborX = CellIndex % Resolution + Offset.X;
				const int32 NeighborY = CellIndex / Resolution + Offset.Y;
				const bool bIsInside = NeighborX >= 0 && NeighborY >= 0 && NeighborX < Resolution && NeighborY < Resolution;
				if (!bIsInside || !System.FluidCells[NeighborY * Resolution + NeighborX])
				{
					continue;
				}

				const double AxisWeight = Offset.X != 0 ? System.AxisWeights.X : System.AxisWeights.Y;
				AppliedOperator += AxisWeight * (static_cast<double>(Pressure[NeighborY * Resolution + NeighborX]) - static_cast<double>(Pressure[CellIndex]));
			}
			Residual[CellIndex] = System.Rhs[CellIndex] - AppliedOperator;
		}
		return Residual;
	}

	TArray<double> ComputeComponentMeans(const FTDFluidGpuPoissonSystem& System, const TArray<double>& Values)
	{
		TArray<double> Means;
		Means.SetNumZeroed(System.ComponentSizes.Num());
		for (int32 CellIndex = 0; CellIndex < Values.Num(); ++CellIndex)
		{
			if (System.ComponentIds[CellIndex] != INDEX_NONE)
			{
				Means[System.ComponentIds[CellIndex]] += Values[CellIndex];
			}
		}
		for (int32 ComponentId = 0; ComponentId < Means.Num(); ++ComponentId)
		{
			Means[ComponentId] /= FMath::Max(System.ComponentSizes[ComponentId], 1);
		}
		return Means;
	}

	double ComputeComponentCenteredNorm(const FTDFluidGpuPoissonSystem& System, const TArray<double>& Values)
	{
		const TArray<double> Means = ComputeComponentMeans(System, Values);
		double SumOfSquares = 0.0;
		for (int32 CellIndex = 0; CellIndex < Values.Num(); ++CellIndex)
		{
			const int32 ComponentId = System.ComponentIds[CellIndex];
			if (ComponentId == INDEX_NONE || System.ComponentSizes[ComponentId] < 2)
			{
				continue;
			}

			SumOfSquares += FMath::Square(Values[CellIndex] - Means[ComponentId]);
		}
		return FMath::Sqrt(SumOfSquares);
	}

	TArray<double> ToDoubleValues(const TArray<float>& Values)
	{
		TArray<double> DoubleValues;
		DoubleValues.Reserve(Values.Num());
		for (const float Value : Values)
		{
			DoubleValues.Add(Value);
		}
		return DoubleValues;
	}

	TArray<double> SubtractValues(const TArray<float>& Left, const TArray<float>& Right)
	{
		TArray<double> Difference;
		Difference.SetNumZeroed(Left.Num());
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			Difference[Index] = static_cast<double>(Left[Index]) - static_cast<double>(Right[Index]);
		}
		return Difference;
	}

	bool AreAllFinite(const TArray<float>& Values, double& OutMaximumMagnitude)
	{
		OutMaximumMagnitude = 0.0;
		for (const float Value : Values)
		{
			if (!FMath::IsFinite(Value))
			{
				return false;
			}
			OutMaximumMagnitude = FMath::Max(OutMaximumMagnitude, static_cast<double>(FMath::Abs(Value)));
		}
		return true;
	}

	void AddObstacleBox(FTDFluidGpuActorInput& Actor, const FVector2D& CenterPlaneCm, const FVector2D& HalfExtentsCm)
	{
		FTDFluidGpuObstacleInput& Box = Actor.Obstacles.AddDefaulted_GetRef();
		Box.CenterPlaneCm = CenterPlaneCm;
		Box.BoxHalfExtentsCm = HalfExtentsCm;
		Box.bIsBox = true;
	}

	void AddObstacleCircle(FTDFluidGpuActorInput& Actor, const FVector2D& CenterPlaneCm, float RadiusCm)
	{
		FTDFluidGpuObstacleInput& Circle = Actor.Obstacles.AddDefaulted_GetRef();
		Circle.CenterPlaneCm = CenterPlaneCm;
		Circle.RadiusCm = RadiusCm;
	}

	void AddClosedFrame(FTDFluidGpuActorInput& Actor, const FVector2D& CenterPlaneCm, double InnerHalfSizeCm, double WallHalfThicknessCm)
	{
		const double WallOffset = InnerHalfSizeCm + WallHalfThicknessCm;
		const double WallHalfLength = WallOffset + WallHalfThicknessCm;
		AddObstacleBox(Actor, CenterPlaneCm + FVector2D(0.0, WallOffset), FVector2D(WallHalfLength, WallHalfThicknessCm));
		AddObstacleBox(Actor, CenterPlaneCm - FVector2D(0.0, WallOffset), FVector2D(WallHalfLength, WallHalfThicknessCm));
		AddObstacleBox(Actor, CenterPlaneCm + FVector2D(WallOffset, 0.0), FVector2D(WallHalfThicknessCm, WallHalfLength));
		AddObstacleBox(Actor, CenterPlaneCm - FVector2D(WallOffset, 0.0), FVector2D(WallHalfThicknessCm, WallHalfLength));
	}

	void AddAdversarialObstacles(FTDFluidGpuActorInput& Actor)
	{
		AddObstacleBox(Actor, FVector2D(-200.0, -750.0), FVector2D(2.5, 450.0));
		AddObstacleBox(Actor, FVector2D(-200.0, 60.0), FVector2D(2.5, 340.0));
		AddClosedFrame(Actor, FVector2D(700.0, 700.0), 40.0, 5.0);
		AddClosedFrame(Actor, FVector2D(-903.0, 897.0), 6.0, 4.0);
		AddClosedFrame(Actor, FVector2D(-1100.0, -300.0), 13.0, 4.0);
		AddObstacleCircle(Actor, FVector2D(-600.0, -1100.0), 150.0f);
		AddObstacleCircle(Actor, FVector2D(-290.0, -1100.0), 150.0f);
	}

	FTDFluidGpuActorInput MakeMultigridCaseActor(const FTDFluidGpuMultigridCase& Case, int32 FrameIndex)
	{
		FTDFluidGpuActorInput Actor = MakeRichFluidActor(700, 0, FrameIndex);
		Actor.Settings.Resolution = Case.Resolution;
		Actor.Settings.ExtentCm = Case.ExtentCm;
		Actor.TerrainElevationUpdates.Reset();
		if (!Case.bKeepsRichObstacles)
		{
			Actor.Obstacles.Reset();
		}
		if (Case.bAddsAdversarialObstacles)
		{
			AddAdversarialObstacles(Actor);
		}
		return Actor;
	}

	FTDFluidGpuActorInput MakeRichFluidActorAtResolution(uint64 ActorKey, int32 Variant, int32 FrameIndex, int32 Resolution)
	{
		FTDFluidGpuActorInput Actor = MakeRichFluidActor(ActorKey, Variant, FrameIndex);
		Actor.Settings.Resolution = Resolution;
		Actor.TerrainElevationUpdates.Reset();
		if (FrameIndex % ShiftIntervalFrames == 0)
		{
			AddTerrainForWholeWindow(Actor);
		}
		return Actor;
	}

	struct FTDFluidGpuTimingSummary
	{
		int32 SampleCount = 0;
		double MedianMilliseconds = 0.0;
		double P95Milliseconds = 0.0;
	};

	FTDFluidGpuTimingSummary SummarizeTimings(TArray<double> Milliseconds)
	{
		FTDFluidGpuTimingSummary Summary;
		if (Milliseconds.IsEmpty())
		{
			return Summary;
		}

		Milliseconds.Sort();
		Summary.SampleCount = Milliseconds.Num();
		Summary.MedianMilliseconds = Milliseconds[Milliseconds.Num() / 2];
		Summary.P95Milliseconds = Milliseconds[FMath::Clamp(FMath::CeilToInt(0.95 * Milliseconds.Num()) - 1, 0, Milliseconds.Num() - 1)];
		return Summary;
	}

	FTDFluidGpuTimingSummary MeasureGpuFrameTimings(int32 WarmupFrameCount, int32 MeasuredFrameCount, const FTDFluidGpuRhiFrameBuilder& BuildFrame)
	{
		TSharedRef<TArray<double>, ESPMode::ThreadSafe> Milliseconds = MakeShared<TArray<double>, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuTimedFrames)(
			[Milliseconds, BuildFrame, WarmupFrameCount, MeasuredFrameCount](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FRenderQueryRHIRef> EndQueries;
				for (int32 FrameIndex = 0; FrameIndex < WarmupFrameCount + MeasuredFrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					BuildFrame(FrameIndex, FrameInputs);
					const bool bIsMeasured = FrameIndex >= WarmupFrameCount;
					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					}
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuTimedFrame"));
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
						GraphBuilder.Execute();
					}
					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(EndQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				for (int32 QueryIndex = 0; QueryIndex < StartQueries.Num(); ++QueryIndex)
				{
					uint64 StartMicroseconds = 0;
					uint64 EndMicroseconds = 0;
					const bool bHasStart = RHIGetRenderQueryResult(StartQueries[QueryIndex], StartMicroseconds, true);
					const bool bHasEnd = RHIGetRenderQueryResult(EndQueries[QueryIndex], EndMicroseconds, true);
					if (bHasStart && bHasEnd && EndMicroseconds >= StartMicroseconds)
					{
						Milliseconds->Add(static_cast<double>(EndMicroseconds - StartMicroseconds) / 1000.0);
					}
				}
			});
		FlushRenderingCommands();
		return SummarizeTimings(*Milliseconds);
	}

	struct FTDFluidGpuPassSegmentSamples
	{
		FString Name;
		TArray<double> Milliseconds;
	};

	struct FTDFluidGpuPassTimingProfile
	{
		TArray<FTDFluidGpuPassSegmentSamples> Segments;
		TArray<double> FrameMilliseconds;
		TArray<int32> FrameIndices;
		TArray<FString> WorstSegmentNames;
		TArray<double> WorstSegmentMilliseconds;
		uint64 FirstStartMicroseconds = 0;
		uint64 LastEndMicroseconds = 0;
		int32 RejectedFrameCount = 0;
	};

	double ComputeMeanMilliseconds(const TArray<double>& Milliseconds)
	{
		if (Milliseconds.IsEmpty())
		{
			return 0.0;
		}

		double Sum = 0.0;
		for (const double Value : Milliseconds)
		{
			Sum += Value;
		}
		return Sum / Milliseconds.Num();
	}

	TArray<FString> MakeSegmentNames(const TArray<FString>& MarkerLabels)
	{
		TArray<FString> Names;
		TMap<FString, int32> OccurrenceCounts;
		for (const FString& Label : MarkerLabels)
		{
			const int32 Occurrence = ++OccurrenceCounts.FindOrAdd(Label);
			Names.Add(Occurrence == 1 ? Label : FString::Printf(TEXT("%s#%d"), *Label, Occurrence));
		}
		Names.Add(TEXT("graph_end"));
		return Names;
	}

	bool ReadFrameMicroseconds(const FRenderQueryRHIRef& StartQuery, const FTDFluidGpuPassTimestamps& Timestamps, const FRenderQueryRHIRef& EndQuery, TArray<uint64>& OutMicroseconds)
	{
		OutMicroseconds.Reset();
		TArray<FRenderQueryRHIRef> Queries;
		Queries.Add(StartQuery);
		Queries.Append(Timestamps.Queries);
		Queries.Add(EndQuery);
		for (const FRenderQueryRHIRef& Query : Queries)
		{
			uint64 Microseconds = 0;
			if (!RHIGetRenderQueryResult(Query, Microseconds, true))
			{
				return false;
			}
			if (!OutMicroseconds.IsEmpty() && Microseconds < OutMicroseconds.Last())
			{
				return false;
			}
			OutMicroseconds.Add(Microseconds);
		}
		return true;
	}

	void AddFrameSegments(int32 FrameIndex, const TArray<FString>& SegmentNames, const TArray<uint64>& Microseconds, FTDFluidGpuPassTimingProfile& InOutProfile)
	{
		FString WorstSegmentName;
		double WorstSegmentMilliseconds = -1.0;
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentNames.Num(); ++SegmentIndex)
		{
			const FString& Name = SegmentNames[SegmentIndex];
			const double SegmentMilliseconds = static_cast<double>(Microseconds[SegmentIndex + 1] - Microseconds[SegmentIndex]) / 1000.0;
			if (SegmentMilliseconds > WorstSegmentMilliseconds)
			{
				WorstSegmentMilliseconds = SegmentMilliseconds;
				WorstSegmentName = Name;
			}
			FTDFluidGpuPassSegmentSamples* Segment = InOutProfile.Segments.FindByPredicate([&Name](const FTDFluidGpuPassSegmentSamples& Candidate)
			{
				return Candidate.Name == Name;
			});
			if (!Segment)
			{
				Segment = &InOutProfile.Segments.AddDefaulted_GetRef();
				Segment->Name = Name;
			}
			Segment->Milliseconds.Add(SegmentMilliseconds);
		}
		InOutProfile.FrameMilliseconds.Add(static_cast<double>(Microseconds.Last() - Microseconds[0]) / 1000.0);
		InOutProfile.FrameIndices.Add(FrameIndex);
		InOutProfile.WorstSegmentNames.Add(WorstSegmentName);
		InOutProfile.WorstSegmentMilliseconds.Add(WorstSegmentMilliseconds);
		if (InOutProfile.FirstStartMicroseconds == 0)
		{
			InOutProfile.FirstStartMicroseconds = Microseconds[0];
		}
		InOutProfile.LastEndMicroseconds = Microseconds.Last();
	}

	FTDFluidGpuPassTimingProfile MeasureGpuPassTimings(int32 WarmupFrameCount, int32 MeasuredFrameCount, const FTDFluidGpuRhiFrameBuilder& BuildFrame)
	{
		TSharedRef<FTDFluidGpuPassTimingProfile, ESPMode::ThreadSafe> Profile = MakeShared<FTDFluidGpuPassTimingProfile, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuPassTimedFrames)(
			[Profile, BuildFrame, WarmupFrameCount, MeasuredFrameCount](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FRenderQueryRHIRef> EndQueries;
				TArray<FTDFluidGpuPassTimestamps> FrameTimestamps;
				FrameTimestamps.Reserve(MeasuredFrameCount);
				for (int32 FrameIndex = 0; FrameIndex < WarmupFrameCount + MeasuredFrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					BuildFrame(FrameIndex, FrameInputs);
					if (FrameIndex < WarmupFrameCount)
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuPassWarmupFrame"));
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
						GraphBuilder.Execute();
						RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
						continue;
					}

					RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuPassTimedFrame"));
						AddTDFluidGpuTimestampedFramePasses(GraphBuilder, FrameInputs, Batches, FrameTimestamps.AddDefaulted_GetRef());
						GraphBuilder.Execute();
					}
					RHICmdList.EndRenderQuery(EndQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				TArray<uint64> Microseconds;
				for (int32 FrameIndex = 0; FrameIndex < FrameTimestamps.Num(); ++FrameIndex)
				{
					const FTDFluidGpuPassTimestamps& Timestamps = FrameTimestamps[FrameIndex];
					if (!ReadFrameMicroseconds(StartQueries[FrameIndex], Timestamps, EndQueries[FrameIndex], Microseconds))
					{
						++Profile->RejectedFrameCount;
						continue;
					}

					AddFrameSegments(WarmupFrameCount + FrameIndex, MakeSegmentNames(Timestamps.Labels), Microseconds, *Profile);
				}
			});
		FlushRenderingCommands();
		return *Profile;
	}

	struct FTDFluidGpuStateSnapshotRun
	{
		TArray<FTDFluidGpuBatch> Batches;
		TArray<TUniquePtr<FRHIGPUTextureReadback>> VelocityReadbacks;
		TArray<TUniquePtr<FRHIGPUTextureReadback>> ObstacleSdfReadbacks;
		TArray<TUniquePtr<FRHIGPUTextureReadback>> PressureReadbacks;
		TArray<FTDFluidGpuMultigridProbeResult> Snapshots;
	};

	void EnqueueStateSnapshot(FRHICommandListImmediate& RHICmdList, FTDFluidGpuStateSnapshotRun& Run)
	{
		FTDFluidGpuBatch* FluidBatch = Run.Batches.FindByPredicate([](const FTDFluidGpuBatch& Batch)
		{
			return Batch.GetKey().SimMode == ETDFluidGpuSimMode::FluidSim && !Batch.GetFrame().Parameters.IsEmpty();
		});
		if (!FluidBatch)
		{
			return;
		}

		const FTDFluidGpuBatchStateTextures& State = FluidBatch->StateTextures;
		const FTDFluidGpuActorParameters& Parameters = FluidBatch->GetFrame().Parameters[0];
		const uint32 SliceIndex = static_cast<uint32>(Parameters.SliceIndex);
		FTDFluidGpuMultigridProbeResult& Snapshot = Run.Snapshots.AddDefaulted_GetRef();
		Snapshot.Resolution = FluidBatch->GetKey().ResolutionTier;
		Snapshot.RelativeCellSpacing = Parameters.RelativeCellSpacing;
		Snapshot.AxisWeights = FVector2f(Parameters.InvSpacingXSquared, Parameters.InvSpacingYSquared);

		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuStateSnapshot"));
		const auto EnqueueCopy = [&GraphBuilder, SliceIndex](const TRefCountPtr<IPooledRenderTarget>& Source, const TCHAR* Name, TArray<TUniquePtr<FRHIGPUTextureReadback>>& Readbacks)
		{
			FRHIGPUTextureReadback* Readback = Readbacks.Add_GetRef(MakeUnique<FRHIGPUTextureReadback>(Name)).Get();
			AddEnqueueCopyPass(GraphBuilder, Readback, GraphBuilder.RegisterExternalTexture(Source, Name), FResolveRect(), SliceIndex);
		};
		EnqueueCopy(State.Velocity[1], TEXT("TDFluidGpu.SnapshotVelocity"), Run.VelocityReadbacks);
		EnqueueCopy(State.ObstacleSdf, TEXT("TDFluidGpu.SnapshotObstacleSdf"), Run.ObstacleSdfReadbacks);
		EnqueueCopy(State.PressureSolution[0], TEXT("TDFluidGpu.SnapshotPressure"), Run.PressureReadbacks);
		GraphBuilder.Execute();
	}

	bool ReadStateSnapshots(FTDFluidGpuStateSnapshotRun& Run)
	{
		bool bHasReadAll = Run.Snapshots.Num() == Run.PressureReadbacks.Num();
		for (int32 SnapshotIndex = 0; bHasReadAll && SnapshotIndex < Run.Snapshots.Num(); ++SnapshotIndex)
		{
			FTDFluidGpuMultigridProbeResult& Snapshot = Run.Snapshots[SnapshotIndex];
			const int32 Resolution = Snapshot.Resolution;
			bHasReadAll &= ReadProbeTexels<FTDFluidGpuHalfPairTexel>(Run.VelocityReadbacks[SnapshotIndex].Get(), Resolution, [](const FTDFluidGpuHalfPairTexel& Texel)
			{
				return FVector2f(Texel.X.GetFloat(), Texel.Y.GetFloat());
			}, Snapshot.Velocity);
			bHasReadAll &= ReadProbeTexels<FFloat16>(Run.ObstacleSdfReadbacks[SnapshotIndex].Get(), Resolution, [](const FFloat16& Texel)
			{
				return Texel.GetFloat();
			}, Snapshot.ObstacleSdf);
			bHasReadAll &= ReadProbeTexels<float>(Run.PressureReadbacks[SnapshotIndex].Get(), Resolution, [](const float& Texel)
			{
				return Texel;
			}, Snapshot.Pressures.AddDefaulted_GetRef());
			Snapshot.bIsValid = bHasReadAll;
		}
		return bHasReadAll;
	}

	TArray<FTDFluidGpuMultigridProbeResult> RunFramesWithStateSnapshots(int32 FrameCount, const FTDFluidGpuRhiFrameBuilder& BuildFrame, const TArray<int32>& SnapshotFrameCounts)
	{
		TSharedRef<FTDFluidGpuStateSnapshotRun, ESPMode::ThreadSafe> Run = MakeShared<FTDFluidGpuStateSnapshotRun, ESPMode::ThreadSafe>();
		for (int32 FirstFrame = 0; FirstFrame < FrameCount; FirstFrame += FramesPerRenderCommand)
		{
			const int32 EndFrame = FMath::Min(FirstFrame + FramesPerRenderCommand, FrameCount);
			ENQUEUE_RENDER_COMMAND(TDFluidGpuStateSnapshotFrames)(
				[Run, BuildFrame, FirstFrame, EndFrame, FrameCount, SnapshotFrameCounts](FRHICommandListImmediate& RHICmdList)
				{
					for (int32 FrameIndex = FirstFrame; FrameIndex < EndFrame; ++FrameIndex)
					{
						FTDFluidGpuFrameInputs FrameInputs;
						FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
						BuildFrame(FrameIndex, FrameInputs);
						{
							FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuStateSnapshotRun"));
							AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Run->Batches);
							GraphBuilder.Execute();
						}
						if (SnapshotFrameCounts.Contains(FrameIndex + 1))
						{
							EnqueueStateSnapshot(RHICmdList, *Run);
						}
						RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
					}

					if (EndFrame < FrameCount)
					{
						return;
					}

					RHICmdList.SubmitAndBlockUntilGPUIdle();
					ReadStateSnapshots(*Run);
					Run->Batches.Empty();
				});
			FlushRenderingCommands();
		}
		return Run->Snapshots;
	}

	struct FTDFluidGpuPocketHealth
	{
		bool bIsFinite = false;
		double MaximumMagnitude = 0.0;
		int32 CheckedPocketCount = 0;
		double MainRelativeResidual = 0.0;
		double WorstPocketRelativeResidual = 0.0;
		double LargestPocketOffset = 0.0;
	};

	FTDFluidGpuPocketHealth MeasurePocketHealth(const FTDFluidGpuMultigridProbeResult& Snapshot, int32 MinimumPocketCells)
	{
		FTDFluidGpuPocketHealth Health;
		if (!Snapshot.bIsValid || Snapshot.Pressures.IsEmpty())
		{
			return Health;
		}

		const FTDFluidGpuPoissonSystem System = BuildCpuPoissonSystem(Snapshot);
		const TArray<float>& Pressure = Snapshot.Pressures[0];
		Health.bIsFinite = AreAllFinite(Pressure, Health.MaximumMagnitude);
		if (!Health.bIsFinite || System.ComponentSizes.IsEmpty())
		{
			return Health;
		}

		const TArray<double> Residual = ComputeCpuResidual(System, Pressure);
		const TArray<double> ResidualMeans = ComputeComponentMeans(System, Residual);
		const TArray<double> RhsMeans = ComputeComponentMeans(System, System.Rhs);
		const TArray<double> PressureMeans = ComputeComponentMeans(System, ToDoubleValues(Pressure));
		TArray<double> ResidualSquares;
		TArray<double> RhsSquares;
		ResidualSquares.SetNumZeroed(System.ComponentSizes.Num());
		RhsSquares.SetNumZeroed(System.ComponentSizes.Num());
		for (int32 CellIndex = 0; CellIndex < Residual.Num(); ++CellIndex)
		{
			const int32 ComponentId = System.ComponentIds[CellIndex];
			if (ComponentId == INDEX_NONE)
			{
				continue;
			}

			ResidualSquares[ComponentId] += FMath::Square(Residual[CellIndex] - ResidualMeans[ComponentId]);
			RhsSquares[ComponentId] += FMath::Square(System.Rhs[CellIndex] - RhsMeans[ComponentId]);
		}

		int32 MainComponentId = 0;
		for (int32 ComponentId = 0; ComponentId < System.ComponentSizes.Num(); ++ComponentId)
		{
			MainComponentId = System.ComponentSizes[ComponentId] > System.ComponentSizes[MainComponentId] ? ComponentId : MainComponentId;
		}

		for (int32 ComponentId = 0; ComponentId < System.ComponentSizes.Num(); ++ComponentId)
		{
			const double RelativeResidual = FMath::Sqrt(ResidualSquares[ComponentId]) / FMath::Max(FMath::Sqrt(RhsSquares[ComponentId]), UE_DOUBLE_SMALL_NUMBER);
			if (ComponentId == MainComponentId)
			{
				Health.MainRelativeResidual = RelativeResidual;
				continue;
			}

			Health.LargestPocketOffset = FMath::Max(Health.LargestPocketOffset, FMath::Abs(PressureMeans[ComponentId] - PressureMeans[MainComponentId]));
			if (System.ComponentSizes[ComponentId] < MinimumPocketCells)
			{
				continue;
			}

			++Health.CheckedPocketCount;
			Health.WorstPocketRelativeResidual = FMath::Max(Health.WorstPocketRelativeResidual, RelativeResidual);
		}
		return Health;
	}

	FTDFluidGpuActorInput MakeClosedPocketActor(int32 FrameIndex)
	{
		const FVector2D CourtyardCenterPlaneCm(550.0, -450.0);
		const FVector2D SmallPocketCenterPlaneCm(700.0, 700.0);
		FTDFluidGpuActorInput Actor = MakeRichFluidActor(800, 0, FrameIndex);
		Actor.Settings.Resolution = 512;
		Actor.Settings.ExtentCm = FVector2D(3200.0, 3200.0);
		Actor.WindowCenterPlaneCm = FVector2D::ZeroVector;
		Actor.ShiftCells = FIntPoint::ZeroValue;
		Actor.TerrainElevationUpdates.Reset();
		if (FrameIndex == 0)
		{
			AddTerrainForWholeWindow(Actor);
		}

		Actor.Obstacles.Reset();
		AddAdversarialObstacles(Actor);
		AddClosedFrame(Actor, CourtyardCenterPlaneCm, 300.0, 10.0);
		Actor.Stamps.Reset();
		FTDFluidGpuStampInput& CourtyardEmitter = Actor.Stamps.Add_GetRef(MakeDensityStamp(CourtyardCenterPlaneCm - FVector2D(0.0, 180.0), 110.0f, 0.8f));
		CourtyardEmitter.VelocityPlaneCmPerSec = FVector2D(120.0, 350.0);
		CourtyardEmitter.VelocityStrength = 1.0f;
		FTDFluidGpuStampInput& PocketEmitter = Actor.Stamps.Add_GetRef(MakeDensityStamp(SmallPocketCenterPlaneCm, 30.0f, 0.8f));
		PocketEmitter.VelocityPlaneCmPerSec = FVector2D(250.0, 150.0);
		PocketEmitter.VelocityStrength = 1.0f;
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuMultigridConvergenceTest,
	"TDGame.FluidFXRhi.MultigridConvergence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuMultigridConvergenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuMultigridTestSupport;

	if (!CanRunRhiTest(*this, TEXT("MultigridConvergence")))
	{
		return true;
	}

	constexpr int32 WarmupFrameCount = 30;
	constexpr int32 EarlyRateFirstSlot = 2;
	constexpr int32 EarlyRateLastSlot = 6;
	constexpr int32 SettledSlot = 7;
	constexpr double MaximumCompatibleRelativeResidual = 1.0e-3;
	constexpr double IncompatibleResidualAllowanceFactor = 10.0;
	constexpr double MaximumDriftToJacobiRatio = 64.0;
	constexpr int32 SaturationSlot = 11;
	constexpr double JacobiReferenceIterations = 32.0;
	constexpr double MaximumPressureMagnitude = TDFluidGpuSolverConstants::MultigridPressureLimit;
	const TArray<int32> RecordedCycleCounts = { 0, 1, 2, 3, 4, 5, 6, 8, 100, 200, 300, 3600 };
	const FTDFluidGpuMultigridCase Cases[] = {
		{ TEXT("Open512"), 512, FVector2D(3200.0, 3200.0), false, false, 0.25 },
		{ TEXT("Adversarial512"), 512, FVector2D(3200.0, 3200.0), true, true, 0.35 },
		{ TEXT("Anisotropic256"), 256, FVector2D(3200.0, 1600.0), true, false, 0.25 },
		{ TEXT("Minimum64"), 64, FVector2D(3200.0, 3200.0), true, false, 0.2 },
	};

	for (const FTDFluidGpuMultigridCase& Case : Cases)
	{
		const FTDFluidGpuMultigridProbeResult Probe = RunMultigridProbe(WarmupFrameCount, [&Case](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			OutFrame.Actors.Add(MakeMultigridCaseActor(Case, FrameIndex));
		}, RecordedCycleCounts);
		if (!TestTrue(*FString::Printf(TEXT("%s probe read back velocity, obstacles and every recorded pressure"), Case.Name), Probe.bIsValid))
		{
			continue;
		}

		TestEqual(*FString::Printf(TEXT("%s probe runs at the requested tier"), Case.Name), Probe.Resolution, Case.Resolution);
		const FTDFluidGpuPoissonSystem System = BuildCpuPoissonSystem(Probe);
		const int32 FinalSlot = SaturationSlot - 1;
		const TArray<float>& FinalPressure = Probe.Pressures[FinalSlot];
		bool bIsEveryPressureFinite = true;
		double MaximumMagnitude = 0.0;
		TArray<double> ResidualNorms;
		TArray<double> ErrorNorms;
		for (int32 Slot = 0; Slot <= FinalSlot; ++Slot)
		{
			double SlotMaximumMagnitude = 0.0;
			bIsEveryPressureFinite = AreAllFinite(Probe.Pressures[Slot], SlotMaximumMagnitude) && bIsEveryPressureFinite;
			MaximumMagnitude = FMath::Max(MaximumMagnitude, SlotMaximumMagnitude);
			ResidualNorms.Add(ComputeComponentCenteredNorm(System, ComputeCpuResidual(System, Probe.Pressures[Slot])));
			ErrorNorms.Add(ComputeComponentCenteredNorm(System, SubtractValues(Probe.Pressures[Slot], FinalPressure)));
		}

		const double RhsNorm = FMath::Max(ResidualNorms[0], UE_DOUBLE_SMALL_NUMBER);
		const double InitialErrorNorm = FMath::Max(ErrorNorms[0], UE_DOUBLE_SMALL_NUMBER);
		const double ErrorReductionRate = FMath::Pow(ErrorNorms[EarlyRateLastSlot] / FMath::Max(ErrorNorms[EarlyRateFirstSlot], UE_DOUBLE_SMALL_NUMBER), 1.0 / (EarlyRateLastSlot - EarlyRateFirstSlot));
		const double FinalRelativeResidual = ResidualNorms[FinalSlot] / RhsNorm;
		const TArray<double> RhsMeans = ComputeComponentMeans(System, System.Rhs);
		double IncompatibleSumOfSquares = 0.0;
		for (int32 ComponentId = 0; ComponentId < System.ComponentSizes.Num(); ++ComponentId)
		{
			IncompatibleSumOfSquares += System.ComponentSizes[ComponentId] >= 2 ? System.ComponentSizes[ComponentId] * FMath::Square(RhsMeans[ComponentId]) : 0.0;
		}
		const double IncompatibleFraction = FMath::Sqrt(IncompatibleSumOfSquares) / RhsNorm;
		const double ResidualAllowance = MaximumCompatibleRelativeResidual + IncompatibleResidualAllowanceFactor * IncompatibleFraction;

		int32 MainComponentId = 0;
		for (int32 ComponentId = 0; ComponentId < System.ComponentSizes.Num(); ++ComponentId)
		{
			MainComponentId = System.ComponentSizes[ComponentId] > System.ComponentSizes[MainComponentId] ? ComponentId : MainComponentId;
		}
		const TArray<double> SettledMeans = ComputeComponentMeans(System, ToDoubleValues(Probe.Pressures[SettledSlot]));
		const TArray<double> FinalMeans = ComputeComponentMeans(System, ToDoubleValues(FinalPressure));
		const double SettledCycleSpan = static_cast<double>(RecordedCycleCounts[FinalSlot] - RecordedCycleCounts[SettledSlot]);
		const double JacobiConstantStepScale = JacobiReferenceIterations / (2.0 * (System.AxisWeights.X + System.AxisWeights.Y));
		double OffsetDriftPerCycle = 0.0;
		double ObservedDriftToJacobiRatio = 0.0;
		for (int32 ComponentId = 0; ComponentId < System.ComponentSizes.Num(); ++ComponentId)
		{
			if (ComponentId == MainComponentId || System.ComponentSizes[ComponentId] < 2 || SettledMeans.IsEmpty())
			{
				continue;
			}

			const double SettledOffset = SettledMeans[ComponentId] - SettledMeans[MainComponentId];
			const double FinalOffset = FinalMeans[ComponentId] - FinalMeans[MainComponentId];
			const double ComponentDriftPerCycle = FMath::Abs(FinalOffset - SettledOffset) / SettledCycleSpan;
			const double JacobiDriftPerProjection = JacobiConstantStepScale * FMath::Abs(RhsMeans[ComponentId] - RhsMeans[MainComponentId]);
			OffsetDriftPerCycle = FMath::Max(OffsetDriftPerCycle, ComponentDriftPerCycle);
			ObservedDriftToJacobiRatio = FMath::Max(ObservedDriftToJacobiRatio, ComponentDriftPerCycle / FMath::Max(JacobiDriftPerProjection, UE_DOUBLE_SMALL_NUMBER));
		}

		FString ResidualSeries;
		FString ErrorSeries;
		double SaturatedMaximumMagnitude = 0.0;
		const bool bIsSaturatedPressureFinite = AreAllFinite(Probe.Pressures[SaturationSlot], SaturatedMaximumMagnitude);
		for (int32 Slot = 1; Slot <= FinalSlot; ++Slot)
		{
			ResidualSeries += FString::Printf(TEXT("%s%d:%.3e"), Slot > 1 ? TEXT(",") : TEXT(""), RecordedCycleCounts[Slot], ResidualNorms[Slot] / RhsNorm);
			if (Slot < FinalSlot)
			{
				ErrorSeries += FString::Printf(TEXT("%s%d:%.3e"), Slot > 1 ? TEXT(",") : TEXT(""), RecordedCycleCounts[Slot], ErrorNorms[Slot] / InitialErrorNorm);
			}
		}

		const FString ConvergenceLine = FString::Printf(
			TEXT("TDFluidGpuMultigridConvergence: case=%s resolution=%d axis_weights=%.3f/%.3f components=%d main_component_cells=%d rhs_l2=%.4e residual_rel=[%s] error_rel=[%s] error_rate_cycles_%d_to_%d=%.3f final_relative_residual=%.3e incompatible_fraction=%.3e max_abs_pressure=%.4e component_offset_drift_per_cycle=%.4e drift_to_jacobi32=%.3f finite=%d max_abs_pressure_cycle_%d=%.4e"),
			Case.Name,
			Probe.Resolution,
			System.AxisWeights.X,
			System.AxisWeights.Y,
			System.ComponentSizes.Num(),
			System.ComponentSizes.IsEmpty() ? 0 : System.ComponentSizes[MainComponentId],
			RhsNorm,
			*ResidualSeries,
			*ErrorSeries,
			RecordedCycleCounts[EarlyRateFirstSlot],
			RecordedCycleCounts[EarlyRateLastSlot],
			ErrorReductionRate,
			FinalRelativeResidual,
			IncompatibleFraction,
			MaximumMagnitude,
			OffsetDriftPerCycle,
			ObservedDriftToJacobiRatio,
			bIsEveryPressureFinite ? 1 : 0,
			RecordedCycleCounts[SaturationSlot],
			SaturatedMaximumMagnitude);
		UE_LOG(LogTDFluidGpuRhiTests, Display, TEXT("%s"), *ConvergenceLine);
		AddInfo(ConvergenceLine);

		TestTrue(*FString::Printf(TEXT("%s keeps every recorded pressure finite"), Case.Name), bIsEveryPressureFinite);
		TestTrue(*FString::Printf(TEXT("%s keeps pressure magnitude below %.0e after 300 warm cycles"), Case.Name, MaximumPressureMagnitude), MaximumMagnitude < MaximumPressureMagnitude);
		TestTrue(*FString::Printf(TEXT("%s stays finite and within the %.0e pressure guard after %d warm cycles"), Case.Name, MaximumPressureMagnitude, RecordedCycleCounts[SaturationSlot]), bIsSaturatedPressureFinite && SaturatedMaximumMagnitude <= MaximumPressureMagnitude);
		TestTrue(*FString::Printf(TEXT("%s solves the CPU discrete system to a relative residual of at most %.3e (1e-3 plus 10 times the incompatible fraction)"), Case.Name, ResidualAllowance), FinalRelativeResidual <= ResidualAllowance);
		TestTrue(*FString::Printf(TEXT("%s closed component offsets drift at most %.0f times the Jacobi 32 constant step"), Case.Name, MaximumDriftToJacobiRatio), ObservedDriftToJacobiRatio <= MaximumDriftToJacobiRatio);
		TestTrue(*FString::Printf(TEXT("%s error falls by at least the case rate %.2f per cycle over cycles 2 to 6"), Case.Name, Case.MaximumErrorReductionRate), ErrorReductionRate <= Case.MaximumErrorReductionRate);
		TestTrue(*FString::Printf(TEXT("%s error falls monotonically over the first four cycles"), Case.Name), ErrorNorms[1] < ErrorNorms[0] && ErrorNorms[2] < ErrorNorms[1] && ErrorNorms[3] < ErrorNorms[2] && ErrorNorms[4] < ErrorNorms[3]);
		TestTrue(*FString::Printf(TEXT("%s residual does not grow from cycle 8 to cycle 300"), Case.Name), ResidualNorms[FinalSlot] <= ResidualNorms[SettledSlot] + 1.0e-3 * RhsNorm);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuMultigridRunToRunDeterministicTest,
	"TDGame.FluidFXRhi.MultigridRunToRunDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuMultigridRunToRunDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuModuleTestSupport;
	using namespace TDFluidGpuMultigridTestSupport;

	if (!CanRunRhiTest(*this, TEXT("MultigridRunToRunDeterministic")))
	{
		return true;
	}

	FTDFluidGpuConsoleVariableOverride SolverOverride(GpuPressureSolverVariableName, ForcedMultigridPressureSolverMode);
	if (!TestTrue(TEXT("Forced multigrid solver is applied"), SolverOverride.bIsApplied))
	{
		return false;
	}

	constexpr int32 FrameCount = 61;
	const FTDFluidGpuRhiFrameBuilder BuildFrame = [](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		OutFrame.Actors.Add(MakeRichFluidActorAtResolution(1, 0, FrameIndex, MultigridRichTestResolution));
		OutFrame.Actors.Add(MakePainterActor(2, FrameIndex));
	};

	const FTDFluidGpuRhiRunResult FirstRun = RunGpuFrames(FrameCount, BuildFrame);
	const FTDFluidGpuRhiRunResult SecondRun = RunGpuFrames(FrameCount, BuildFrame);
	if (!FirstRun.bHasReadback || !SecondRun.bHasReadback)
	{
		AddError(TEXT("Readback atlas was not ready after the GPU idled"));
		return false;
	}

	TestEqual(TEXT("Both actors stepped on the final frame"), FirstRun.FinalSteppedEntryCount, 2);
	TestTrue(TEXT("Multigrid fluid tile holds density after 61 frames"), SumPrimaryDensity(ExtractTile(FirstRun.Readback, 0)) > 0.0f);
	TestTrue(TEXT("Two multigrid runs produce bit identical readback atlases"), AreTileTexelsBitIdentical(FirstRun.Readback.Texels, SecondRun.Readback.Texels));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuMultigridBatchSliceIndependenceTest,
	"TDGame.FluidFXRhi.MultigridBatchSliceIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuMultigridBatchSliceIndependenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuModuleTestSupport;
	using namespace TDFluidGpuMultigridTestSupport;

	if (!CanRunRhiTest(*this, TEXT("MultigridBatchSliceIndependence")))
	{
		return true;
	}

	FTDFluidGpuConsoleVariableOverride SolverOverride(GpuPressureSolverVariableName, ForcedMultigridPressureSolverMode);
	if (!TestTrue(TEXT("Forced multigrid solver is applied"), SolverOverride.bIsApplied))
	{
		return false;
	}

	constexpr int32 FrameCount = 41;
	constexpr int32 BatchActorCount = 49;
	constexpr int32 ObservedVariant = 24;
	const FTDFluidGpuRhiRunResult AloneRun = RunGpuFrames(FrameCount, [](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		OutFrame.Actors.Add(MakeRichFluidActorAtResolution(100 + ObservedVariant, ObservedVariant, FrameIndex, MultigridRichTestResolution));
	});
	const FTDFluidGpuRhiRunResult BatchRun = RunGpuFrames(FrameCount, [](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		for (int32 Variant = 0; Variant < BatchActorCount; ++Variant)
		{
			OutFrame.Actors.Add(MakeRichFluidActorAtResolution(100 + Variant, Variant, FrameIndex, MultigridRichTestResolution));
		}
	});
	if (!AloneRun.bHasReadback || !BatchRun.bHasReadback)
	{
		AddError(TEXT("Readback atlas was not ready after the GPU idled"));
		return false;
	}

	TestEqual(TEXT("All 49 multigrid actors share one batch"), BatchRun.BatchCount, 1);
	TestEqual(TEXT("All 49 multigrid actors stepped on the final frame"), BatchRun.FinalSteppedEntryCount, BatchActorCount);
	const TArray<FFloat16Color> AloneTile = ExtractTile(AloneRun.Readback, 0);
	const TArray<FFloat16Color> BatchTile = ExtractTile(BatchRun.Readback, ObservedVariant);
	TestTrue(TEXT("Observed multigrid tile holds density"), SumPrimaryDensity(AloneTile) > 0.0f);
	TestTrue(TEXT("Multigrid slice result is bit identical alone and inside a 49 actor batch"), AreTileTexelsBitIdentical(AloneTile, BatchTile));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuPressureSolveCost512Test,
	"TDGame.FluidFXRhi.PressureSolveCost512",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuPressureSolveCost512Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuModuleTestSupport;
	using namespace TDFluidGpuMultigridTestSupport;

	if (!CanRunRhiTest(*this, TEXT("PressureSolveCost512")))
	{
		return true;
	}

	constexpr int32 WarmupFrameCount = 8;
	constexpr int32 MeasuredFrameCount = 96;
	constexpr int32 AutomaticSolverMode = 0;
	constexpr double SingleStepMedianBudgetMilliseconds = 0.45;
	constexpr double SingleStepP95BudgetMilliseconds = 0.55;
	constexpr double FourActorMedianBudgetMilliseconds = 1.6;
	struct FTDFluidGpuCostCase
	{
		const TCHAR* Name = TEXT("");
		int32 SolverMode = 0;
		int32 CycleCount = 1;
		int32 PressureIterations = 32;
	};
	const FTDFluidGpuCostCase Cases[] = {
		{ TEXT("jacobi6"), ForcedJacobiPressureSolverMode, 1, 6 },
		{ TEXT("jacobi32"), ForcedJacobiPressureSolverMode, 1, 32 },
		{ TEXT("multigrid1"), AutomaticSolverMode, 1, 32 },
		{ TEXT("multigrid2"), AutomaticSolverMode, 2, 32 },
	};
	const int32 ActorCounts[] = { 1, 4 };

	TMap<FString, FTDFluidGpuTimingSummary> Summaries;
	bool bIsEveryOverrideApplied = true;
	FString CostLine = TEXT("TDFluidGpuPressureCost: measure=gpu_timestamp_per_frame resolution=512");
	for (const FTDFluidGpuCostCase& Case : Cases)
	{
		FTDFluidGpuConsoleVariableOverride SolverOverride(GpuPressureSolverVariableName, Case.SolverMode);
		FTDFluidGpuConsoleVariableOverride CycleOverride(GpuPressureCyclesVariableName, Case.CycleCount);
		bIsEveryOverrideApplied &= SolverOverride.bIsApplied && CycleOverride.bIsApplied;
		for (const int32 ActorCount : ActorCounts)
		{
			const int32 PressureIterations = Case.PressureIterations;
			const FTDFluidGpuTimingSummary Summary = MeasureGpuFrameTimings(WarmupFrameCount, MeasuredFrameCount, [ActorCount, PressureIterations](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
			{
				for (int32 Variant = 0; Variant < ActorCount; ++Variant)
				{
					FTDFluidGpuActorInput Actor = MakeRichFluidActorAtResolution(900 + Variant, Variant, FrameIndex, 512);
					Actor.TerrainElevationUpdates.Reset();
					Actor.Settings.PressureIterations = PressureIterations;
					OutFrame.Actors.Add(Actor);
				}
			});
			const FString Key = FString::Printf(TEXT("%s_x%d"), Case.Name, ActorCount);
			Summaries.Add(Key, Summary);
			CostLine += FString::Printf(TEXT(" %s_median_ms=%.3f %s_p95_ms=%.3f %s_samples=%d"), *Key, Summary.MedianMilliseconds, *Key, Summary.P95Milliseconds, *Key, Summary.SampleCount);
		}
	}
	UE_LOG(LogTDFluidGpuRhiTests, Display, TEXT("%s"), *CostLine);
	AddInfo(CostLine);

	const FTDFluidGpuTimingSummary& MultigridSingle = Summaries.FindChecked(TEXT("multigrid1_x1"));
	const FTDFluidGpuTimingSummary& MultigridFour = Summaries.FindChecked(TEXT("multigrid1_x4"));
	const FTDFluidGpuTimingSummary& JacobiFour = Summaries.FindChecked(TEXT("jacobi32_x4"));
	TestTrue(TEXT("Every solver override was applied"), bIsEveryOverrideApplied);
	for (const TPair<FString, FTDFluidGpuTimingSummary>& Pair : Summaries)
	{
		TestEqual(*FString::Printf(TEXT("%s read a GPU timestamp pair for every measured frame"), *Pair.Key), Pair.Value.SampleCount, MeasuredFrameCount);
	}
	TestTrue(TEXT("One multigrid cycle per projection costs less GPU time than 32 Jacobi iterations for four 512 actors"), MultigridFour.MedianMilliseconds < JacobiFour.MedianMilliseconds);
	const FString BudgetLine = FString::Printf(
		TEXT("TDFluidGpuPressureBudget: multigrid1_x1_median_ms=%.3f budget_ms=%.2f met=%d multigrid1_x1_p95_ms=%.3f budget_ms=%.2f met=%d multigrid1_x4_median_ms=%.3f budget_ms=%.2f met=%d"),
		MultigridSingle.MedianMilliseconds,
		SingleStepMedianBudgetMilliseconds,
		MultigridSingle.MedianMilliseconds <= SingleStepMedianBudgetMilliseconds ? 1 : 0,
		MultigridSingle.P95Milliseconds,
		SingleStepP95BudgetMilliseconds,
		MultigridSingle.P95Milliseconds <= SingleStepP95BudgetMilliseconds ? 1 : 0,
		MultigridFour.MedianMilliseconds,
		FourActorMedianBudgetMilliseconds,
		MultigridFour.MedianMilliseconds <= FourActorMedianBudgetMilliseconds ? 1 : 0);
	UE_LOG(LogTDFluidGpuRhiTests, Display, TEXT("%s"), *BudgetLine);
	AddInfo(BudgetLine);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuPassTimingProfile512Test,
	"TDGame.FluidFXRhi.PassTimingProfile512",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuPassTimingProfile512Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuModuleTestSupport;
	using namespace TDFluidGpuMultigridTestSupport;

	if (!CanRunRhiTest(*this, TEXT("PassTimingProfile512")))
	{
		return true;
	}

	constexpr int32 WarmupFrameCount = 8;
	constexpr int32 MeasuredFrameCount = 96;
	constexpr int32 AutomaticSolverMode = 0;
	constexpr int32 ForcedMultigridSolverMode = 2;
	struct FTDFluidGpuProfileCase
	{
		const TCHAR* Name = TEXT("");
		int32 SolverMode = 0;
		int32 Resolution = 512;
		int32 ActorCount = 1;
		int32 PressureIterations = 32;
	};
	const FTDFluidGpuProfileCase Cases[] = {
		{ TEXT("multigrid1_x1"), AutomaticSolverMode, 512, 1, 32 },
		{ TEXT("multigrid1_x4"), AutomaticSolverMode, 512, 4, 32 },
		{ TEXT("jacobi6_x1"), ForcedJacobiPressureSolverMode, 512, 1, 6 },
		{ TEXT("multigrid1_res256_x1"), ForcedMultigridSolverMode, 256, 1, 32 },
	};

	bool bIsEveryOverrideApplied = true;
	for (const FTDFluidGpuProfileCase& Case : Cases)
	{
		FTDFluidGpuConsoleVariableOverride SolverOverride(GpuPressureSolverVariableName, Case.SolverMode);
		FTDFluidGpuConsoleVariableOverride CycleOverride(GpuPressureCyclesVariableName, 1);
		bIsEveryOverrideApplied &= SolverOverride.bIsApplied && CycleOverride.bIsApplied;
		const FTDFluidGpuRhiFrameBuilder BuildFrame = [Case](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			for (int32 Variant = 0; Variant < Case.ActorCount; ++Variant)
			{
				FTDFluidGpuActorInput Actor = MakeRichFluidActorAtResolution(900 + Variant, Variant, FrameIndex, Case.Resolution);
				Actor.TerrainElevationUpdates.Reset();
				Actor.Settings.PressureIterations = Case.PressureIterations;
				OutFrame.Actors.Add(Actor);
			}
		};

		const FTDFluidGpuTimingSummary Unmarked = MeasureGpuFrameTimings(WarmupFrameCount, MeasuredFrameCount, BuildFrame);
		const FTDFluidGpuPassTimingProfile Profile = MeasureGpuPassTimings(WarmupFrameCount, MeasuredFrameCount, BuildFrame);
		const FTDFluidGpuTimingSummary Marked = SummarizeTimings(Profile.FrameMilliseconds);
		const double MarkedMeanMilliseconds = ComputeMeanMilliseconds(Profile.FrameMilliseconds);
		double SegmentMedianSum = 0.0;
		double SegmentMeanSum = 0.0;
		for (const FTDFluidGpuPassSegmentSamples& Segment : Profile.Segments)
		{
			const FTDFluidGpuTimingSummary SegmentSummary = SummarizeTimings(Segment.Milliseconds);
			const double SegmentMean = ComputeMeanMilliseconds(Segment.Milliseconds);
			SegmentMedianSum += SegmentSummary.MedianMilliseconds;
			SegmentMeanSum += SegmentMean;
			double SegmentMaximum = 0.0;
			for (const double Value : Segment.Milliseconds)
			{
				SegmentMaximum = FMath::Max(SegmentMaximum, Value);
			}
			UE_LOG(LogTDFluidGpuRhiTests, Display, TEXT("TDFluidGpuPassSegment: case=%s segment=%s samples=%d median_ms=%.4f mean_ms=%.4f p95_ms=%.4f max_ms=%.4f share_of_mean=%.4f"),
				Case.Name,
				*Segment.Name,
				SegmentSummary.SampleCount,
				SegmentSummary.MedianMilliseconds,
				SegmentMean,
				SegmentSummary.P95Milliseconds,
				SegmentMaximum,
				MarkedMeanMilliseconds > 0.0 ? SegmentMean / MarkedMeanMilliseconds : 0.0);
		}

		const FString ProfileLine = FString::Printf(
			TEXT("TDFluidGpuPassProfile: case=%s resolution=%d actors=%d frames=%d rejected=%d segments=%d unmarked_median_ms=%.3f unmarked_p95_ms=%.3f marked_median_ms=%.3f marked_mean_ms=%.3f marked_p95_ms=%.3f segment_median_sum_ms=%.3f segment_mean_sum_ms=%.3f"),
			Case.Name,
			Case.Resolution,
			Case.ActorCount,
			Profile.FrameMilliseconds.Num(),
			Profile.RejectedFrameCount,
			Profile.Segments.Num(),
			Unmarked.MedianMilliseconds,
			Unmarked.P95Milliseconds,
			Marked.MedianMilliseconds,
			MarkedMeanMilliseconds,
			Marked.P95Milliseconds,
			SegmentMedianSum,
			SegmentMeanSum);
		UE_LOG(LogTDFluidGpuRhiTests, Display, TEXT("%s"), *ProfileLine);
		AddInfo(ProfileLine);

		TArray<double> ShiftFrameMilliseconds;
		TArray<double> SteadyFrameMilliseconds;
		TArray<int32> FrameOrder;
		for (int32 SampleIndex = 0; SampleIndex < Profile.FrameMilliseconds.Num(); ++SampleIndex)
		{
			const bool bIsShiftFrame = Profile.FrameIndices[SampleIndex] % ShiftIntervalFrames == 0;
			(bIsShiftFrame ? ShiftFrameMilliseconds : SteadyFrameMilliseconds).Add(Profile.FrameMilliseconds[SampleIndex]);
			FrameOrder.Add(SampleIndex);
		}
		FrameOrder.Sort([&Profile](int32 Left, int32 Right)
		{
			return Profile.FrameMilliseconds[Left] > Profile.FrameMilliseconds[Right];
		});
		const FTDFluidGpuTimingSummary ShiftSummary = SummarizeTimings(ShiftFrameMilliseconds);
		const FTDFluidGpuTimingSummary SteadySummary = SummarizeTimings(SteadyFrameMilliseconds);
		const double SpanMilliseconds = static_cast<double>(Profile.LastEndMicroseconds - Profile.FirstStartMicroseconds) / 1000.0;
		FString FrameLine = FString::Printf(
			TEXT("TDFluidGpuPassFrames: case=%s gpu_span_ms=%.2f shift_frames=%d shift_median_ms=%.3f steady_frames=%d steady_median_ms=%.3f steady_p95_ms=%.3f worst="),
			Case.Name,
			SpanMilliseconds,
			ShiftSummary.SampleCount,
			ShiftSummary.MedianMilliseconds,
			SteadySummary.SampleCount,
			SteadySummary.MedianMilliseconds,
			SteadySummary.P95Milliseconds);
		for (int32 OrderIndex = 0; OrderIndex < FMath::Min(10, FrameOrder.Num()); ++OrderIndex)
		{
			const int32 SampleIndex = FrameOrder[OrderIndex];
			FrameLine += FString::Printf(TEXT(" f%d:%.3f[%s:%.3f]"), Profile.FrameIndices[SampleIndex], Profile.FrameMilliseconds[SampleIndex], *Profile.WorstSegmentNames[SampleIndex], Profile.WorstSegmentMilliseconds[SampleIndex]);
		}
		UE_LOG(LogTDFluidGpuRhiTests, Display, TEXT("%s"), *FrameLine);
		AddInfo(FrameLine);
		TestEqual(*FString::Printf(TEXT("%s read every pass timestamp of every measured frame"), Case.Name), Profile.FrameMilliseconds.Num(), MeasuredFrameCount);
		TestEqual(*FString::Printf(TEXT("%s read every unmarked frame timestamp pair"), Case.Name), Unmarked.SampleCount, MeasuredFrameCount);
		TestTrue(*FString::Printf(TEXT("%s split the step into named pass segments"), Case.Name), Profile.Segments.Num() >= 16);
	}
	TestTrue(TEXT("Every solver override was applied"), bIsEveryOverrideApplied);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuMultigridClosedPocketLongRunTest,
	"TDGame.FluidFXRhi.MultigridClosedPocketLongRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuMultigridClosedPocketLongRunTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuModuleTestSupport;
	using namespace TDFluidGpuMultigridTestSupport;

	if (!CanRunRhiTest(*this, TEXT("MultigridClosedPocketLongRun")))
	{
		return true;
	}

	constexpr int32 FrameCount = 3600;
	constexpr int32 MinimumPocketCells = 32;
	constexpr int32 KeepsFullWarmStart = 0;
	constexpr double MaximumHealthyPressureMagnitude = 0.1 * TDFluidGpuSolverConstants::MultigridPressureLimit;
	constexpr double MaximumLateGrowthRatio = 1.5;
	constexpr double MaximumPocketRelativeResidual = 0.5;
	const TCHAR* const WarmStartDecayVariableName = TEXT("TD.FluidFX.GpuPressureWarmStartDecayFrames");
	const TArray<int32> SnapshotFrameCounts = { 1200, 2400, 3600 };
	const FTDFluidGpuRhiFrameBuilder BuildFrame = [](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		OutFrame.Actors.Add(MakeClosedPocketActor(FrameIndex));
	};

	const auto RunAndDescribe = [this, &SnapshotFrameCounts, &BuildFrame, MinimumPocketCells](const TCHAR* Label, TArray<FTDFluidGpuPocketHealth>& OutHealth)
	{
		const TArray<FTDFluidGpuMultigridProbeResult> Snapshots = RunFramesWithStateSnapshots(FrameCount, BuildFrame, SnapshotFrameCounts);
		FString HealthLine = FString::Printf(TEXT("TDFluidGpuClosedPocketLongRun: run=%s snapshots=%d"), Label, Snapshots.Num());
		for (int32 SnapshotIndex = 0; SnapshotIndex < Snapshots.Num(); ++SnapshotIndex)
		{
			const FTDFluidGpuPocketHealth& Health = OutHealth.Add_GetRef(MeasurePocketHealth(Snapshots[SnapshotIndex], MinimumPocketCells));
			HealthLine += FString::Printf(TEXT(" frame%d[valid=%d finite=%d max_abs_pressure=%.4e pocket_offset=%.4e checked_pockets=%d main_rel_residual=%.3e worst_pocket_rel_residual=%.3e]"),
				SnapshotFrameCounts.IsValidIndex(SnapshotIndex) ? SnapshotFrameCounts[SnapshotIndex] : -1,
				Snapshots[SnapshotIndex].bIsValid ? 1 : 0,
				Health.bIsFinite ? 1 : 0,
				Health.MaximumMagnitude,
				Health.LargestPocketOffset,
				Health.CheckedPocketCount,
				Health.MainRelativeResidual,
				Health.WorstPocketRelativeResidual);
		}
		UE_LOG(LogTDFluidGpuRhiTests, Display, TEXT("%s"), *HealthLine);
		AddInfo(HealthLine);
	};

	TArray<FTDFluidGpuPocketHealth> FullWarmStartHealth;
	{
		FTDFluidGpuConsoleVariableOverride DecayOverride(WarmStartDecayVariableName, KeepsFullWarmStart);
		TestTrue(TEXT("Full warm start override is applied"), DecayOverride.bIsApplied);
		RunAndDescribe(TEXT("full_warm_start"), FullWarmStartHealth);
	}

	TArray<FTDFluidGpuPocketHealth> DefaultHealth;
	RunAndDescribe(TEXT("default_decay"), DefaultHealth);
	if (!TestEqual(TEXT("Default run read every snapshot"), DefaultHealth.Num(), SnapshotFrameCounts.Num()))
	{
		return false;
	}

	for (int32 SnapshotIndex = 0; SnapshotIndex < DefaultHealth.Num(); ++SnapshotIndex)
	{
		const FTDFluidGpuPocketHealth& Health = DefaultHealth[SnapshotIndex];
		const int32 SnapshotFrame = SnapshotFrameCounts[SnapshotIndex];
		TestTrue(*FString::Printf(TEXT("Pressure stays finite at frame %d"), SnapshotFrame), Health.bIsFinite);
		TestTrue(*FString::Printf(TEXT("Pressure magnitude %.4e stays below %.0e at frame %d"), Health.MaximumMagnitude, MaximumHealthyPressureMagnitude, SnapshotFrame), Health.MaximumMagnitude <= MaximumHealthyPressureMagnitude);
		TestTrue(*FString::Printf(TEXT("At least two closed pockets are checked at frame %d"), SnapshotFrame), Health.CheckedPocketCount >= 2);
		TestTrue(*FString::Printf(TEXT("Worst closed pocket relative residual %.3e stays at most %.2f at frame %d"), Health.WorstPocketRelativeResidual, MaximumPocketRelativeResidual, SnapshotFrame), Health.WorstPocketRelativeResidual <= MaximumPocketRelativeResidual);
	}
	const double FirstMagnitude = DefaultHealth[0].MaximumMagnitude;
	const double LastMagnitude = DefaultHealth.Last().MaximumMagnitude;
	TestTrue(*FString::Printf(TEXT("Pressure magnitude stops growing: frame %d %.4e is at most %.1f times frame %d %.4e"), SnapshotFrameCounts.Last(), LastMagnitude, MaximumLateGrowthRatio, SnapshotFrameCounts[0], FirstMagnitude), LastMagnitude <= MaximumLateGrowthRatio * FirstMagnitude);
	return !HasAnyErrors();
}

#endif
