#include "FluidFX/TDFluidGpuWaterFieldState.h"
#include "Algo/Reverse.h"
#include "Async/ParallelFor.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(TDFluid);

namespace
{
	constexpr int32 GpuWaterFieldStateRowsPerBatch = 16;
	constexpr float GpuWaterFieldStateMaximumBankFalloffCm = 10000.0f;
	constexpr float GpuWaterFieldStateMaximumBankSpeedRatio = 1.0f;
	const TCHAR* const GpuWaterFieldStateRowParallelVariableName = TEXT("TD.FluidFX.SolverRowParallel");

	struct FTDGpuWaterFieldStateDenormalScope
	{
		FTDGpuWaterFieldStateDenormalScope()
			: CallerControlRegister(VectorGetControlRegister())
		{
			VectorSetControlRegister(CallerControlRegister & ~static_cast<uint32>(VECTOR_DENORMALS_FLUSH_TO_ZERO));
		}

		~FTDGpuWaterFieldStateDenormalScope()
		{
			VectorSetControlRegister(CallerControlRegister);
		}

		uint32 CallerControlRegister;
	};

	float GpuWaterFieldStateClampFinite(float Value, float MinValue, float MaxValue)
	{
		if (!FMath::IsFinite(Value))
		{
			return MinValue;
		}
		return FMath::Clamp(Value, MinValue, MaxValue);
	}

	float GpuWaterFieldStateSanitizeBankFalloffCm(const FTDFluidSimulationConfig& Config)
	{
		return GpuWaterFieldStateClampFinite(Config.FlowGuideBankFalloffCm, 0.0f, GpuWaterFieldStateMaximumBankFalloffCm);
	}

	float GpuWaterFieldStateSanitizeBankSpeedRatio(const FTDFluidSimulationConfig& Config)
	{
		return GpuWaterFieldStateClampFinite(Config.FlowGuideBankSpeedRatio, 0.0f, GpuWaterFieldStateMaximumBankSpeedRatio);
	}

	EParallelForFlags GpuWaterFieldStateResolveRowFlags()
	{
		static IConsoleVariable* const RowParallelVariable = IConsoleManager::Get().FindConsoleVariable(GpuWaterFieldStateRowParallelVariableName);
		const bool bRunsRowsInParallel = !RowParallelVariable || RowParallelVariable->GetInt() != 0;
		return bRunsRowsInParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread;
	}

	float GpuWaterFieldStateToGpuSurfaceHeight(float SurfaceHeightCm)
	{
		return SurfaceHeightCm == FTDFluidGrid2DSolver::NoWaterSurfaceHeight ? TDFluidGpuSolverConstants::WaterNoSurfaceHeightCm : SurfaceHeightCm;
	}

	int32 GpuWaterFieldStateWrapIndex(int32 Value, int32 Count)
	{
		return ((Value % Count) + Count) % Count;
	}

	bool GpuWaterFieldStateIsInsideField(const FIntPoint& Cell, int32 GridSize)
	{
		return Cell.X >= 0 && Cell.X < GridSize && Cell.Y >= 0 && Cell.Y < GridSize;
	}

	template <typename ElementType>
	void GpuWaterFieldStateShiftGrid(TArray<ElementType>& Values, int32 GridSize, const FIntPoint& FieldShift, const ElementType& NewCellValue)
	{
		TArray<ElementType> ShiftedValues;
		ShiftedValues.Init(NewCellValue, GridSize * GridSize);
		const int32 CopyWidth = GridSize - FMath::Abs(FieldShift.X);
		if (CopyWidth > 0)
		{
			const int32 DestinationStartX = FMath::Max(-FieldShift.X, 0);
			const int32 SourceStartX = FMath::Max(FieldShift.X, 0);
			for (int32 NewY = 0; NewY < GridSize; ++NewY)
			{
				const int32 OldY = NewY + FieldShift.Y;
				if (OldY < 0 || OldY >= GridSize)
				{
					continue;
				}

				FMemory::Memcpy(ShiftedValues.GetData() + NewY * GridSize + DestinationStartX, Values.GetData() + OldY * GridSize + SourceStartX, sizeof(ElementType) * CopyWidth);
			}
		}
		Values = MoveTemp(ShiftedValues);
	}

	template <typename UpdateType>
	void GpuWaterFieldStateKeepLastUpdatePerCell(TArray<UpdateType>& InOutUpdates, FIntPoint UpdateType::* CellMember, int32 GridSize)
	{
		if (InOutUpdates.Num() < 2)
		{
			return;
		}

		TBitArray<> SeenCells(false, GridSize * GridSize);
		TArray<UpdateType> KeptUpdates;
		KeptUpdates.Reserve(InOutUpdates.Num());
		for (int32 UpdateIndex = InOutUpdates.Num() - 1; UpdateIndex >= 0; --UpdateIndex)
		{
			const FIntPoint& Cell = InOutUpdates[UpdateIndex].*CellMember;
			const int32 CellIndex = Cell.Y * GridSize + Cell.X;
			if (SeenCells[CellIndex])
			{
				continue;
			}

			SeenCells[CellIndex] = true;
			KeptUpdates.Add(InOutUpdates[UpdateIndex]);
		}
		Algo::Reverse(KeptUpdates);
		InOutUpdates = MoveTemp(KeptUpdates);
	}
}

void FTDFluidGpuWaterFieldState::Initialize(const FTDFluidSimulationConfig& Config, int32 SimulationResolution, int32 InFieldDivisor, const FVector2D& InCenterPlaneCm, const FVector2D& InExtentCm)
{
	const int32 SafeFieldDivisor = FMath::Max(InFieldDivisor, 1);
	const int32 NewFieldResolution = SimulationResolution / SafeFieldDivisor;
	if (NewFieldResolution <= 0)
	{
		Release();
		return;
	}

	FieldDivisor = SafeFieldDivisor;
	FieldResolution = NewFieldResolution;
	CenterPlaneCm = InCenterPlaneCm;
	ExtentCm = InExtentCm;
	BankFalloffCm = GpuWaterFieldStateSanitizeBankFalloffCm(Config);
	BankSpeedRatio = GpuWaterFieldStateSanitizeBankSpeedRatio(Config);
	FieldOrigin = FIntPoint::ZeroValue;

	const int32 CellCount = FieldResolution * FieldResolution;
	SurfaceHeight.Init(FTDFluidGrid2DSolver::NoWaterSurfaceHeight, CellCount);
	TargetVelocityX.Init(0.0f, CellCount);
	TargetVelocityY.Init(0.0f, CellCount);
	GuideWeight.Init(0.0f, CellCount);
	GuideDescent.Init(0.0f, CellCount);
	TerrainElevation.Init(0.0f, CellCount);
	TerrainKnown.Init(0, CellCount);
	DirtyColumns.Init(0, FieldResolution);
	DirtyRows.Init(0, FieldResolution);
	PendingGuideUpdates.Reset();
	PendingTerrainUpdates.Reset();
	FullUploadGathersRemaining = 0;
	bRasterFullDirty = true;
	bHasDirtyStrips = false;
}

void FTDFluidGpuWaterFieldState::UpdateConfig(const FTDFluidSimulationConfig& Config)
{
	const float NewBankFalloffCm = GpuWaterFieldStateSanitizeBankFalloffCm(Config);
	const float NewBankSpeedRatio = GpuWaterFieldStateSanitizeBankSpeedRatio(Config);
	if (NewBankFalloffCm == BankFalloffCm && NewBankSpeedRatio == BankSpeedRatio)
	{
		return;
	}

	BankFalloffCm = NewBankFalloffCm;
	BankSpeedRatio = NewBankSpeedRatio;
	bRasterFullDirty = true;
}

void FTDFluidGpuWaterFieldState::Release()
{
	Raster.ReleaseRowSegmentLists();
	FieldResolution = 0;
	FieldOrigin = FIntPoint::ZeroValue;
	SurfaceHeight.Empty();
	TargetVelocityX.Empty();
	TargetVelocityY.Empty();
	GuideWeight.Empty();
	GuideDescent.Empty();
	TerrainElevation.Empty();
	TerrainKnown.Empty();
	DirtyColumns.Empty();
	DirtyRows.Empty();
	PendingGuideUpdates.Empty();
	PendingTerrainUpdates.Empty();
	FullUploadGathersRemaining = 0;
	bRasterFullDirty = true;
	bHasDirtyStrips = false;
}

bool FTDFluidGpuWaterFieldState::IsInitialized() const
{
	return FieldResolution > 0 && SurfaceHeight.Num() == FieldResolution * FieldResolution;
}

bool FTDFluidGpuWaterFieldState::IsValidFieldCell(int32 FieldX, int32 FieldY) const
{
	return IsInitialized() && GpuWaterFieldStateIsInsideField(FIntPoint(FieldX, FieldY), FieldResolution);
}

void FTDFluidGpuWaterFieldState::SetFlowGuidesAndRegions(const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidWaterRegion>& Regions)
{
	Raster.SetFlowGuides(Guides);
	Raster.SetWaterRegions(Regions);
	bRasterFullDirty = true;
}

void FTDFluidGpuWaterFieldState::ShiftWindow(const FIntPoint& ShiftSimulationCells, const FVector2D& NewCenterPlaneCm)
{
	CenterPlaneCm = NewCenterPlaneCm;
	if (!IsInitialized() || ShiftSimulationCells == FIntPoint::ZeroValue)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_WaterFieldShift);

	const bool bIsWholeFieldShift = ShiftSimulationCells.X % FieldDivisor == 0 && ShiftSimulationCells.Y % FieldDivisor == 0;
	ensureMsgf(bIsWholeFieldShift, TEXT("TDFluidGpuWaterFieldState: window shift (%d, %d) is not a multiple of the field divisor %d, so every field cell restarts"), ShiftSimulationCells.X, ShiftSimulationCells.Y, FieldDivisor);
	const FIntPoint RequestedFieldShift = bIsWholeFieldShift ? ShiftSimulationCells / FieldDivisor : FIntPoint(FieldResolution, FieldResolution);
	const FIntPoint FieldShift(
		FMath::Clamp(RequestedFieldShift.X, -FieldResolution, FieldResolution),
		FMath::Clamp(RequestedFieldShift.Y, -FieldResolution, FieldResolution));

	ShiftDirtyFlags(DirtyColumns, FieldShift.X);
	ShiftDirtyFlags(DirtyRows, FieldShift.Y);
	bHasDirtyStrips = true;

	GpuWaterFieldStateShiftGrid(SurfaceHeight, FieldResolution, FieldShift, FTDFluidGrid2DSolver::NoWaterSurfaceHeight);
	GpuWaterFieldStateShiftGrid(TargetVelocityX, FieldResolution, FieldShift, 0.0f);
	GpuWaterFieldStateShiftGrid(TargetVelocityY, FieldResolution, FieldShift, 0.0f);
	GpuWaterFieldStateShiftGrid(GuideWeight, FieldResolution, FieldShift, 0.0f);
	GpuWaterFieldStateShiftGrid(GuideDescent, FieldResolution, FieldShift, 0.0f);
	GpuWaterFieldStateShiftGrid(TerrainElevation, FieldResolution, FieldShift, 0.0f);
	GpuWaterFieldStateShiftGrid(TerrainKnown, FieldResolution, FieldShift, static_cast<uint8>(0));

	FieldOrigin.X = GpuWaterFieldStateWrapIndex(FieldOrigin.X + FieldShift.X, FieldResolution);
	FieldOrigin.Y = GpuWaterFieldStateWrapIndex(FieldOrigin.Y + FieldShift.Y, FieldResolution);
	ShiftPendingUpdates(FieldShift);
	QueueTerrainResetForNewCells(FieldShift);
}

void FTDFluidGpuWaterFieldState::SetTerrainElevationAtFieldCell(int32 FieldX, int32 FieldY, float ElevationCm)
{
	if (!IsValidFieldCell(FieldX, FieldY))
	{
		return;
	}

	const int32 CellIndex = FieldY * FieldResolution + FieldX;
	TerrainElevation[CellIndex] = ElevationCm;
	TerrainKnown[CellIndex] = 1;

	FTDFluidGpuTerrainCellUpdate& Update = PendingTerrainUpdates.AddDefaulted_GetRef();
	Update.Cell = FIntPoint(FieldX, FieldY);
	Update.ElevationCm = ElevationCm;
}

void FTDFluidGpuWaterFieldState::RequestFullUpload(int32 GatherCount)
{
	FullUploadGathersRemaining = FMath::Max(FullUploadGathersRemaining, GatherCount);
}

void FTDFluidGpuWaterFieldState::RasterizeDirtyCells()
{
	CSV_SCOPED_TIMING_STAT(TDFluid, WaterRasterDirtyCells);
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_WaterRasterDirtyCells);

	if (!IsInitialized() || (!bRasterFullDirty && !bHasDirtyStrips))
	{
		return;
	}

	const int32 GridSize = FieldResolution;
	TArray<double> RowWorldY;
	RowWorldY.SetNumUninitialized(GridSize);
	for (int32 FieldY = 0; FieldY < GridSize; ++FieldY)
	{
		RowWorldY[FieldY] = FieldCellToPlaneCenter(0, FieldY).Y;
	}
	const double RowSlackCm = 0.5 * ExtentCm.Y / GridSize + BankFalloffCm;
	Raster.BuildRowSegmentLists(RowWorldY, RowSlackCm);

	const bool bRasterizesAllCells = bRasterFullDirty;
	TArray<TArray<FTDFluidGpuWaterGuideCellUpdate>> RowUpdates;
	RowUpdates.SetNum(GridSize);
	ParallelFor(TEXT("TDFluidGpuWaterRasterRows"), GridSize, GpuWaterFieldStateRowsPerBatch, [&](int32 FieldY)
	{
		const FTDGpuWaterFieldStateDenormalScope DenormalScope;
		const bool bIsDirtyRow = DirtyRows[FieldY] != 0;
		TArray<FTDFluidGpuWaterGuideCellUpdate>& Updates = RowUpdates[FieldY];
		for (int32 FieldX = 0; FieldX < GridSize; ++FieldX)
		{
			const bool bIsNewCell = bIsDirtyRow || DirtyColumns[FieldX] != 0;
			if (!bRasterizesAllCells && !bIsNewCell)
			{
				continue;
			}

			const FTDFluidWaterRasterCell RasterCell = Raster.RasterizeCell(FieldCellToPlaneCenter(FieldX, FieldY), FieldY, BankFalloffCm, BankSpeedRatio, FTDFluidGrid2DSolver::NoWaterSurfaceHeight);
			const int32 CellIndex = FieldY * GridSize + FieldX;
			SurfaceHeight[CellIndex] = RasterCell.SurfaceHeightCm;
			TargetVelocityX[CellIndex] = RasterCell.TargetVelocityX;
			TargetVelocityY[CellIndex] = RasterCell.TargetVelocityY;
			GuideWeight[CellIndex] = RasterCell.GuideWeight;
			GuideDescent[CellIndex] = RasterCell.GuideDescent;

			FTDFluidGpuWaterGuideCellUpdate& Update = Updates.AddDefaulted_GetRef();
			Update.FieldCell = FIntPoint(FieldX, FieldY);
			Update.SurfaceHeightCm = RasterCell.SurfaceHeightCm;
			Update.TargetVelocityCmPerSec = FVector2f(RasterCell.TargetVelocityX, RasterCell.TargetVelocityY);
			Update.GuideWeight = RasterCell.GuideWeight;
			Update.GuideDescent = RasterCell.GuideDescent;
		}
	}, GpuWaterFieldStateResolveRowFlags());

	for (TArray<FTDFluidGpuWaterGuideCellUpdate>& Updates : RowUpdates)
	{
		PendingGuideUpdates.Append(MoveTemp(Updates));
	}

	DirtyColumns.Init(0, GridSize);
	DirtyRows.Init(0, GridSize);
	bRasterFullDirty = false;
	bHasDirtyStrips = false;
}

void FTDFluidGpuWaterFieldState::MoveUpdatesInto(FTDFluidGpuActorInput& OutActorInput)
{
	CSV_SCOPED_TIMING_STAT(TDFluid, WaterMoveUpdates);
	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_WaterMoveUpdates);

	OutActorInput.WaterGuideUpdates.Reset();
	OutActorInput.WaterTerrainUpdates.Reset();
	if (!IsInitialized())
	{
		return;
	}

	OutActorInput.WaterFieldOrigin = FieldOrigin;
	if (FullUploadGathersRemaining > 0)
	{
		AppendFullGuideUpload(OutActorInput.WaterGuideUpdates);
		AppendFullTerrainUpload(OutActorInput.WaterTerrainUpdates);
		PendingGuideUpdates.Reset();
		PendingTerrainUpdates.Reset();
		--FullUploadGathersRemaining;
		return;
	}

	GpuWaterFieldStateKeepLastUpdatePerCell(PendingGuideUpdates, &FTDFluidGpuWaterGuideCellUpdate::FieldCell, FieldResolution);
	GpuWaterFieldStateKeepLastUpdatePerCell(PendingTerrainUpdates, &FTDFluidGpuTerrainCellUpdate::Cell, FieldResolution);
	OutActorInput.WaterGuideUpdates = MoveTemp(PendingGuideUpdates);
	OutActorInput.WaterTerrainUpdates = MoveTemp(PendingTerrainUpdates);
	PendingGuideUpdates.Reset();
	PendingTerrainUpdates.Reset();
	for (FTDFluidGpuWaterGuideCellUpdate& Update : OutActorInput.WaterGuideUpdates)
	{
		Update.SurfaceHeightCm = GpuWaterFieldStateToGpuSurfaceHeight(Update.SurfaceHeightCm);
	}
}

FVector2D FTDFluidGpuWaterFieldState::FieldCellToPlaneCenter(int32 FieldX, int32 FieldY) const
{
	if (FieldResolution <= 0)
	{
		return CenterPlaneCm;
	}

	const FVector2D MinPlane = CenterPlaneCm - 0.5f * ExtentCm;
	const float CellSizeX = ExtentCm.X / static_cast<float>(FieldResolution);
	const float CellSizeY = ExtentCm.Y / static_cast<float>(FieldResolution);
	return FVector2D(
		MinPlane.X + (static_cast<float>(FieldX) + 0.5f) * CellSizeX,
		MinPlane.Y + (static_cast<float>(FieldY) + 0.5f) * CellSizeY);
}

float FTDFluidGpuWaterFieldState::GetSurfaceHeightAtFieldCell(int32 FieldX, int32 FieldY) const
{
	if (!IsValidFieldCell(FieldX, FieldY))
	{
		return FTDFluidGrid2DSolver::NoWaterSurfaceHeight;
	}
	return SurfaceHeight[FieldY * FieldResolution + FieldX];
}

void FTDFluidGpuWaterFieldState::ShiftDirtyFlags(TArray<uint8>& Flags, int32 ShiftCells) const
{
	const int32 FlagCount = Flags.Num();
	TArray<uint8> ShiftedFlags;
	ShiftedFlags.Init(1, FlagCount);
	for (int32 FlagIndex = 0; FlagIndex < FlagCount; ++FlagIndex)
	{
		const int32 SourceIndex = FlagIndex + ShiftCells;
		if (SourceIndex >= 0 && SourceIndex < FlagCount)
		{
			ShiftedFlags[FlagIndex] = Flags[SourceIndex];
		}
	}
	Flags = MoveTemp(ShiftedFlags);
}

void FTDFluidGpuWaterFieldState::ShiftPendingUpdates(const FIntPoint& FieldShift)
{
	for (FTDFluidGpuWaterGuideCellUpdate& Update : PendingGuideUpdates)
	{
		Update.FieldCell -= FieldShift;
	}
	PendingGuideUpdates.RemoveAll([this](const FTDFluidGpuWaterGuideCellUpdate& Update)
	{
		return !GpuWaterFieldStateIsInsideField(Update.FieldCell, FieldResolution);
	});

	for (FTDFluidGpuTerrainCellUpdate& Update : PendingTerrainUpdates)
	{
		Update.Cell -= FieldShift;
	}
	PendingTerrainUpdates.RemoveAll([this](const FTDFluidGpuTerrainCellUpdate& Update)
	{
		return !GpuWaterFieldStateIsInsideField(Update.Cell, FieldResolution);
	});
}

void FTDFluidGpuWaterFieldState::QueueTerrainResetForNewCells(const FIntPoint& FieldShift)
{
	for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
	{
		const int32 SourceY = FieldY + FieldShift.Y;
		const bool bIsNewRow = SourceY < 0 || SourceY >= FieldResolution;
		for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
		{
			const int32 SourceX = FieldX + FieldShift.X;
			if (!bIsNewRow && SourceX >= 0 && SourceX < FieldResolution)
			{
				continue;
			}

			FTDFluidGpuTerrainCellUpdate& Update = PendingTerrainUpdates.AddDefaulted_GetRef();
			Update.Cell = FIntPoint(FieldX, FieldY);
			Update.ElevationCm = TDFluidGpuSolverConstants::TerrainUnknownElevationCm;
		}
	}
}

void FTDFluidGpuWaterFieldState::AppendFullGuideUpload(TArray<FTDFluidGpuWaterGuideCellUpdate>& OutUpdates) const
{
	OutUpdates.Reserve(OutUpdates.Num() + FieldResolution * FieldResolution);
	for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
	{
		for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
		{
			const int32 CellIndex = FieldY * FieldResolution + FieldX;
			FTDFluidGpuWaterGuideCellUpdate& Update = OutUpdates.AddDefaulted_GetRef();
			Update.FieldCell = FIntPoint(FieldX, FieldY);
			Update.SurfaceHeightCm = GpuWaterFieldStateToGpuSurfaceHeight(SurfaceHeight[CellIndex]);
			Update.TargetVelocityCmPerSec = FVector2f(TargetVelocityX[CellIndex], TargetVelocityY[CellIndex]);
			Update.GuideWeight = GuideWeight[CellIndex];
			Update.GuideDescent = GuideDescent[CellIndex];
		}
	}
}

void FTDFluidGpuWaterFieldState::AppendFullTerrainUpload(TArray<FTDFluidGpuTerrainCellUpdate>& OutUpdates) const
{
	OutUpdates.Reserve(OutUpdates.Num() + FieldResolution * FieldResolution);
	for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
	{
		for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
		{
			const int32 CellIndex = FieldY * FieldResolution + FieldX;
			FTDFluidGpuTerrainCellUpdate& Update = OutUpdates.AddDefaulted_GetRef();
			Update.Cell = FIntPoint(FieldX, FieldY);
			Update.ElevationCm = TerrainKnown[CellIndex] != 0 ? TerrainElevation[CellIndex] : TDFluidGpuSolverConstants::TerrainUnknownElevationCm;
		}
	}
}
