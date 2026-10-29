#include "FluidFX/TDFluidGrid2DSolver.h"

#include "Async/ParallelFor.h"
#include "FluidFX/TDFluidKernelPressure.h"
#include "FluidFX/TDFluidSolverOptionRules.h"
#include "HAL/IConsoleManager.h"
#include "Math/VectorRegister.h"
#include "TDFluidGpuSolverOptions.h"

namespace TDFluidGrid2DSolverSolverOptionsPrivate
{
	constexpr int32 KernelPressureRowsPerBatch = 16;

	struct FTDFluidSolverOptionsDenormalScope
	{
		FTDFluidSolverOptionsDenormalScope()
			: CallerControlRegister(VectorGetControlRegister())
		{
			VectorSetControlRegister(CallerControlRegister & ~static_cast<uint32>(VECTOR_DENORMALS_FLUSH_TO_ZERO));
		}

		~FTDFluidSolverOptionsDenormalScope()
		{
			VectorSetControlRegister(CallerControlRegister);
		}

		uint32 CallerControlRegister;
	};

	bool IsSolverRowParallelEnabled()
	{
		static IConsoleVariable* const RowParallelVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.FluidFX.SolverRowParallel"));
		return RowParallelVariable == nullptr || RowParallelVariable->GetInt() != 0;
	}

	void ForEachKernelPressureRow(const TCHAR* DebugName, int32 RowCount, TFunctionRef<void(int32)> RowBody)
	{
		const EParallelForFlags Flags = IsSolverRowParallelEnabled() ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread;
		ParallelFor(DebugName, RowCount, KernelPressureRowsPerBatch, [&RowBody](int32 RowIndex)
		{
			const FTDFluidSolverOptionsDenormalScope DenormalScope;
			RowBody(RowIndex);
		}, Flags);
	}

	constexpr double AmplitudeNoiseFrequencyMinimum = 0.5;

	struct FTDFluidAmplitudeNoiseAxis
	{
		TArray<int32, TInlineAllocator<256>> LatticeIndices;
		TArray<float, TInlineAllocator<256>> Fades;
		int32 MinIndex = 0;
		int32 Period = 0;
	};

	struct FTDFluidAmplitudeNoiseLattice
	{
		TArray<float> Values;
		int32 Width = 0;
	};

	int32 ResolveAmplitudeNoisePeriod(double ExtentCm, float SpacingCm)
	{
		return FMath::Max(1, FMath::RoundToInt(static_cast<float>(ExtentCm) / SpacingCm));
	}

	uint32 HashAmplitudeNoisePoint(int32 IndexX, int32 IndexY, uint32 Seed)
	{
		uint32 Hash = Seed ^ (static_cast<uint32>(IndexX) * 0x8DA6B343u) ^ (static_cast<uint32>(IndexY) * 0xD8163841u);
		Hash ^= Hash >> 16;
		Hash *= 0x7FEB352Du;
		Hash ^= Hash >> 15;
		Hash *= 0x846CA68Bu;
		Hash ^= Hash >> 16;
		return Hash;
	}

	float EvaluateAmplitudeNoiseLatticeValue(int32 IndexX, int32 IndexY, double NoiseTime)
	{
		const uint32 Hash = HashAmplitudeNoisePoint(IndexX, IndexY, TDFluidGpuSolverOptionConstants::FlowAmplitudeNoiseSeed);
		const double Phase = static_cast<double>(Hash & 0xFFFFu) / 65536.0;
		const double Frequency = AmplitudeNoiseFrequencyMinimum + static_cast<double>(Hash >> 16) / 65536.0;
		const double Cycles = Phase + Frequency * NoiseTime;
		const double Turns = Cycles - FMath::FloorToDouble(Cycles);
		return FMath::Sin(static_cast<float>(UE_DOUBLE_TWO_PI * Turns));
	}

	float EvaluateAmplitudeNoiseFade(float Fraction)
	{
		return Fraction * Fraction * Fraction * (Fraction * (Fraction * 6.0f - 15.0f) + 10.0f);
	}

	void BuildAmplitudeNoiseAxis(FTDFluidAmplitudeNoiseAxis& OutAxis, int32 Resolution, double MinWorld, double CellSize, double InverseSpacing, int32 LatticeOffsetCells, int32 Period)
	{
		OutAxis.LatticeIndices.SetNumUninitialized(Resolution);
		OutAxis.Fades.SetNumUninitialized(Resolution);
		for (int32 Coordinate = 0; Coordinate < Resolution; ++Coordinate)
		{
			const double LatticePosition = (MinWorld + (static_cast<double>(Coordinate) + 0.5) * CellSize) * InverseSpacing;
			const double LatticeFloor = FMath::FloorToDouble(LatticePosition);
			OutAxis.LatticeIndices[Coordinate] = static_cast<int32>(LatticeFloor);
			OutAxis.Fades[Coordinate] = EvaluateAmplitudeNoiseFade(static_cast<float>(LatticePosition - LatticeFloor));
		}

		const int32 FirstIndex = OutAxis.LatticeIndices[0];
		for (int32 Coordinate = 0; Coordinate < Resolution; ++Coordinate)
		{
			OutAxis.LatticeIndices[Coordinate] -= FirstIndex;
		}
		OutAxis.MinIndex = FirstIndex + LatticeOffsetCells;
		OutAxis.Period = Period;
	}

	int32 ResolveAmplitudeNoiseLatticeIndex(const FTDFluidAmplitudeNoiseAxis& Axis, int32 RelativeIndex)
	{
		const int32 LatticeIndex = Axis.MinIndex + RelativeIndex;
		if (Axis.Period <= 0)
		{
			return LatticeIndex;
		}
		return ((LatticeIndex % Axis.Period) + Axis.Period) % Axis.Period;
	}

	FTDFluidAmplitudeNoiseLattice BuildAmplitudeNoiseLattice(const FTDFluidAmplitudeNoiseAxis& Columns, const FTDFluidAmplitudeNoiseAxis& Rows, double NoiseTime)
	{
		FTDFluidAmplitudeNoiseLattice Lattice;
		Lattice.Width = Columns.LatticeIndices.Last() + 2;
		const int32 Height = Rows.LatticeIndices.Last() + 2;
		Lattice.Values.SetNumUninitialized(Lattice.Width * Height);
		for (int32 RowIndex = 0; RowIndex < Height; ++RowIndex)
		{
			const int32 LatticeY = ResolveAmplitudeNoiseLatticeIndex(Rows, RowIndex);
			for (int32 ColumnIndex = 0; ColumnIndex < Lattice.Width; ++ColumnIndex)
			{
				Lattice.Values[RowIndex * Lattice.Width + ColumnIndex] = EvaluateAmplitudeNoiseLatticeValue(ResolveAmplitudeNoiseLatticeIndex(Columns, ColumnIndex), LatticeY, NoiseTime);
			}
		}
		return Lattice;
	}

	float EvaluateAmplitudeNoiseValue(const FTDFluidAmplitudeNoiseLattice& Lattice, const FTDFluidAmplitudeNoiseAxis& Columns, const FTDFluidAmplitudeNoiseAxis& Rows, int32 Column, int32 Row)
	{
		const float* Corner = Lattice.Values.GetData() + Rows.LatticeIndices[Row] * Lattice.Width + Columns.LatticeIndices[Column];
		const float ColumnFade = Columns.Fades[Column];
		const float LowerEdge = FMath::Lerp(Corner[0], Corner[1], ColumnFade);
		const float UpperEdge = FMath::Lerp(Corner[Lattice.Width], Corner[Lattice.Width + 1], ColumnFade);
		return FMath::Lerp(LowerEdge, UpperEdge, Rows.Fades[Row]);
	}
}

void FTDFluidGrid2DSolver::SetAdvectionOffsetPlane(const FVector2D& InAdvectionOffsetPlaneCmPerSec)
{
	AdvectionOffsetPlaneCmPerSec = InAdvectionOffsetPlaneCmPerSec;
}

void FTDFluidGrid2DSolver::SetNoiseLatticeOffsetCells(const FIntPoint& InNoiseLatticeOffsetCells)
{
	NoiseLatticeOffsetCells = InNoiseLatticeOffsetCells;
}

bool FTDFluidGrid2DSolver::MoveWindowWithoutShift(const FVector2D& NewCenterWorld)
{
	if (!IsInitialized() || IsAreaWrapped() || IsWaterLayerActive() || NewCenterWorld.ContainsNaN() || NewCenterWorld == CenterWorld)
	{
		return false;
	}

	CenterWorld = NewCenterWorld;
	return true;
}

bool FTDFluidGrid2DSolver::UsesKernelPressure() const
{
	return TDFluidSolverOptionRules::ResolveCpuPressureMethod(Config) == ETDFluidPressureMethod::KernelFeedback
		&& TDFluidSolverOptionRules::CanUseKernelPressure(Config);
}

void FTDFluidGrid2DSolver::RelaxPressureKernelFeedback()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ProjectionKernel);
	using namespace TDFluidGrid2DSolverSolverOptionsPrivate;

	const int32 GridSize = Resolution;
	const int32 CellCount = GridSize * GridSize;
	if (GridSize <= 0 || PressureSolution.Num() != CellCount || ScratchPressure.Num() != CellCount || Divergence.Num() != CellCount || SolidCellMask.Num() != CellCount)
	{
		return;
	}

	KernelPartialSums.SetNumUninitialized(CellCount);
	const int32 RadiusTexels = FMath::Clamp(Config.PressureKernelRadiusTexels, TDFluidGpuSolverOptionConstants::MinimumKernelRadiusTexels, TDFluidGpuSolverOptionConstants::MaximumKernelRadiusTexels);
	FTDFluidKernelPressureGrid Grid;
	Grid.Resolution = GridSize;
	Grid.Divergence = Divergence.GetData();
	Grid.SolidMask = SolidCellMask.GetData();
	Grid.RadiusTexels = RadiusTexels;
	Grid.DivergenceRadiusTexels = FMath::Clamp(Config.PressureKernelDivergenceRadiusTexels, TDFluidGpuSolverOptionConstants::MinimumKernelDivergenceRadiusTexels, TDFluidGpuSolverOptionConstants::MaximumKernelRadiusTexels);
	Grid.Feedback = FMath::Clamp(Config.PressureKernelFeedback, 0.0f, 1.0f);
	Grid.DivergenceScale = ComputeTDFluidKernelDivergenceScale(RadiusTexels, FMath::Max(Config.PressureKernelScale, 0.0f));

	const int32 PassCount = FMath::Clamp(Config.PressureKernelPassesPerProjection, 1, TDFluidGpuSolverOptionConstants::MaximumKernelPassesPerProjection);
	FVector4f* PartialSums = KernelPartialSums.GetData();
	for (int32 PassIndex = 0; PassIndex < PassCount; ++PassIndex)
	{
		Grid.Pressure = PressureSolution.GetData();
		float* TargetPressure = ScratchPressure.GetData();
		ForEachKernelPressureRow(TEXT("TDFluidKernelPressureHorizontalRows"), GridSize, [&](int32 Row)
		{
			TDFluidKernelPressure::ComputeHorizontalRow(Grid, Row, PartialSums + Row * GridSize);
		});
		ForEachKernelPressureRow(TEXT("TDFluidKernelPressureVerticalRows"), GridSize, [&](int32 Row)
		{
			TDFluidKernelPressure::ComputeVerticalRow(Grid, PartialSums, Row, TargetPressure + Row * GridSize);
		});
		Swap(PressureSolution, ScratchPressure);
	}
}

void FTDFluidGrid2DSolver::ApplyFlowAmplitudeNoiseDamping(float DeltaTime)
{
	using namespace TDFluidGrid2DSolverSolverOptionsPrivate;

	const float DampingPerSec = Config.FlowAmplitudeNoiseDampingPerSec;
	if (!(DampingPerSec > 0.0f) || !(DeltaTime > 0.0f) || !IsInitialized())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_FlowAmplitudeNoise);

	const int32 GridSize = Resolution;
	const double ExtentX = Config.SimExtentCm.X;
	const double ExtentY = Config.SimExtentCm.Y;
	const float LargestCellSizeCm = static_cast<float>(FMath::Max(ExtentX, ExtentY)) / static_cast<float>(GridSize);
	const float SpacingCm = FMath::Max(Config.FlowAmplitudeNoiseScaleCm, LargestCellSizeCm);
	FTDFluidAmplitudeNoiseAxis Columns;
	FTDFluidAmplitudeNoiseAxis Rows;
	if (IsAreaWrapped())
	{
		const int32 PeriodX = ResolveAmplitudeNoisePeriod(ExtentX, SpacingCm);
		const int32 PeriodY = ResolveAmplitudeNoisePeriod(ExtentY, SpacingCm);
		BuildAmplitudeNoiseAxis(Columns, GridSize, 0.0, static_cast<double>(PeriodX) / GridSize, 1.0, NoiseLatticeOffsetCells.X, PeriodX);
		BuildAmplitudeNoiseAxis(Rows, GridSize, 0.0, static_cast<double>(PeriodY) / GridSize, 1.0, NoiseLatticeOffsetCells.Y, PeriodY);
	}
	else
	{
		const double InverseSpacing = 1.0 / static_cast<double>(SpacingCm);
		BuildAmplitudeNoiseAxis(Columns, GridSize, CenterWorld.X - 0.5 * ExtentX, ExtentX / GridSize, InverseSpacing, NoiseLatticeOffsetCells.X, 0);
		BuildAmplitudeNoiseAxis(Rows, GridSize, CenterWorld.Y - 0.5 * ExtentY, ExtentY / GridSize, InverseSpacing, NoiseLatticeOffsetCells.Y, 0);
	}

	const FTDFluidAmplitudeNoiseLattice Lattice = BuildAmplitudeNoiseLattice(Columns, Rows, static_cast<double>(Config.FlowNoiseSpeed) * SimulationTimeSeconds);
	const float HalfDampingStep = 0.5f * DampingPerSec * DeltaTime;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* VelX = VelocityX.GetData();
	float* VelY = VelocityY.GetData();
	ForEachKernelPressureRow(TEXT("TDFluidFlowAmplitudeNoiseRows"), GridSize, [&](int32 Row)
	{
		const int32 RowStart = Row * GridSize;
		for (int32 Column = 0; Column < GridSize; ++Column)
		{
			const int32 CellIndex = RowStart + Column;
			if (SolidCells[CellIndex])
			{
				continue;
			}

			const float NoiseValue = EvaluateAmplitudeNoiseValue(Lattice, Columns, Rows, Column, Row);
			const float DampingFactor = FMath::Exp(-HalfDampingStep * (1.0f - NoiseValue));
			VelX[CellIndex] *= DampingFactor;
			VelY[CellIndex] *= DampingFactor;
		}
	});
}

void FTDFluidGrid2DSolver::AccumulateAdvectionOffset()
{
	const float OffsetX = static_cast<float>(AdvectionOffsetPlaneCmPerSec.X);
	const float OffsetY = static_cast<float>(AdvectionOffsetPlaneCmPerSec.Y);
	if ((OffsetX == 0.0f && OffsetY == 0.0f) || !FMath::IsFinite(OffsetX) || !FMath::IsFinite(OffsetY) || !IsInitialized())
	{
		return;
	}

	const int32 CellCount = Resolution * Resolution;
	const uint8* SolidCells = SolidCellMask.GetData();
	float* PushX = PendingRadialPushX.GetData();
	float* PushY = PendingRadialPushY.GetData();
	for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
	{
		if (SolidCells[CellIndex])
		{
			continue;
		}

		PushX[CellIndex] += OffsetX;
		PushY[CellIndex] += OffsetY;
		bHasPendingRadialPush = true;
	}
}
