#include "TDFluidGpuStampBins.h"

namespace
{
	struct FTDFluidGpuStampWindowGeometry
	{
		int32 Resolution = 0;
		int32 BinSpanCells = 1;
		float CellSizeX = 0.0f;
		float CellSizeY = 0.0f;
		float MinBoundsX = 0.0f;
		float MinBoundsY = 0.0f;
	};

	struct FTDFluidGpuStampCandidate
	{
		int32 SourceIndex = 0;
		float EffectiveRadiusCm = 0.0f;
		float PriorityDistanceSquared = 0.0f;
	};

	struct FTDFluidGpuStampBinCell
	{
		int32 MinBinX = 0;
		int32 MinBinY = 0;
		int32 MaxBinX = 0;
		int32 MaxBinY = 0;
	};

	bool IsUsableWindow(const FTDFluidGpuStampBinWindow& Window)
	{
		return Window.Resolution > 0
			&& !Window.CenterPlaneCm.ContainsNaN()
			&& !Window.ExtentCm.ContainsNaN()
			&& Window.ExtentCm.X > 0.0
			&& Window.ExtentCm.Y > 0.0;
	}

	bool IsFiniteStamp(const FTDFluidGpuStampInput& Stamp)
	{
		return !Stamp.CenterPlaneCm.ContainsNaN()
			&& !Stamp.VelocityPlaneCmPerSec.ContainsNaN()
			&& FMath::IsFinite(Stamp.RadiusCm)
			&& FMath::IsFinite(Stamp.DensityStrength)
			&& FMath::IsFinite(Stamp.VelocityStrength)
			&& FMath::IsFinite(Stamp.RadialVelocityCmPerSec)
			&& FMath::IsFinite(Stamp.HeatStrength);
	}

	FTDFluidGpuStampWindowGeometry MakeWindowGeometry(const FTDFluidGpuStampBinWindow& Window)
	{
		FTDFluidGpuStampWindowGeometry Geometry;
		Geometry.Resolution = Window.Resolution;
		Geometry.BinSpanCells = FTDFluidGpuStampBins::GetBinSpanCells(Window.Resolution);
		Geometry.CellSizeX = static_cast<float>(Window.ExtentCm.X / static_cast<float>(Window.Resolution));
		Geometry.CellSizeY = static_cast<float>(Window.ExtentCm.Y / static_cast<float>(Window.Resolution));
		Geometry.MinBoundsX = static_cast<float>(Window.CenterPlaneCm.X - 0.5f * Window.ExtentCm.X);
		Geometry.MinBoundsY = static_cast<float>(Window.CenterPlaneCm.Y - 0.5f * Window.ExtentCm.Y);
		return Geometry;
	}

	bool IsStampOutsideWindow(const FTDFluidGpuStampBinWindow& Window, const FTDFluidGpuStampWindowGeometry& Geometry, const FVector2D& StampCenter, float EffectiveRadiusCm)
	{
		return StampCenter.X + EffectiveRadiusCm < Geometry.MinBoundsX
			|| StampCenter.X - EffectiveRadiusCm > Geometry.MinBoundsX + Window.ExtentCm.X
			|| StampCenter.Y + EffectiveRadiusCm < Geometry.MinBoundsY
			|| StampCenter.Y - EffectiveRadiusCm > Geometry.MinBoundsY + Window.ExtentCm.Y;
	}

	FTDFluidGpuStampBinCell ComputeStampBinCell(const FTDFluidGpuStampWindowGeometry& Geometry, const FVector2D& StampCenter, float EffectiveRadiusCm)
	{
		const int32 LastCell = Geometry.Resolution - 1;
		const int32 MinCellX = FMath::Clamp(FMath::FloorToInt32((StampCenter.X - EffectiveRadiusCm - Geometry.MinBoundsX) / Geometry.CellSizeX), 0, LastCell);
		const int32 MaxCellX = FMath::Clamp(FMath::CeilToInt32((StampCenter.X + EffectiveRadiusCm - Geometry.MinBoundsX) / Geometry.CellSizeX), 0, LastCell);
		const int32 MinCellY = FMath::Clamp(FMath::FloorToInt32((StampCenter.Y - EffectiveRadiusCm - Geometry.MinBoundsY) / Geometry.CellSizeY), 0, LastCell);
		const int32 MaxCellY = FMath::Clamp(FMath::CeilToInt32((StampCenter.Y + EffectiveRadiusCm - Geometry.MinBoundsY) / Geometry.CellSizeY), 0, LastCell);

		FTDFluidGpuStampBinCell BinCell;
		BinCell.MinBinX = MinCellX / Geometry.BinSpanCells;
		BinCell.MinBinY = MinCellY / Geometry.BinSpanCells;
		BinCell.MaxBinX = MaxCellX / Geometry.BinSpanCells;
		BinCell.MaxBinY = MaxCellY / Geometry.BinSpanCells;
		return BinCell;
	}

	FTDFluidGpuPackedStamp PackStamp(const FTDFluidGpuStampBinWindow& Window, const FTDFluidGpuStampInput& Stamp, const FTDFluidGpuStampCandidate& Candidate)
	{
		const FVector2D TargetVelocity = (Stamp.VelocityPlaneCmPerSec * Stamp.VelocityStrength).GetClampedToMaxSize(TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec);

		ETDFluidGpuStampFlags Flags = ETDFluidGpuStampFlags::None;
		if (Stamp.bWritesSelectiveDensity)
		{
			Flags |= ETDFluidGpuStampFlags::WritesSelectiveDensity;
		}
		if (Stamp.DensityStrength < 0.0f)
		{
			Flags |= ETDFluidGpuStampFlags::ErasesDensity;
		}
		if (Stamp.HeatStrength > 0.0f)
		{
			Flags |= ETDFluidGpuStampFlags::WritesHeat;
		}

		FTDFluidGpuPackedStamp Packed;
		Packed.CenterFromWindowCenterCm = FVector2f(Stamp.CenterPlaneCm - Window.CenterPlaneCm);
		Packed.EffectiveRadiusCm = Candidate.EffectiveRadiusCm;
		Packed.DensityStrength = Stamp.DensityStrength;
		Packed.TargetVelocityCmPerSec = FVector2f(TargetVelocity);
		Packed.VelocityStrength = Stamp.VelocityStrength;
		Packed.RadialVelocityCmPerSec = Stamp.RadialVelocityCmPerSec;
		Packed.Flags = static_cast<uint32>(Flags);
		Packed.SourceIndex = static_cast<uint32>(Candidate.SourceIndex);
		Packed.EraseRatePerSec = FMath::Abs(Stamp.DensityStrength) * TDFluidGpuSolverConstants::BrushEraseRatePerSec;
		Packed.HeatStrength = FMath::Clamp(Stamp.HeatStrength, 0.0f, 1.0f);
		return Packed;
	}

	void CollectCandidates(const FTDFluidGpuStampBinWindow& Window, const FTDFluidGpuStampWindowGeometry& Geometry, TConstArrayView<FTDFluidGpuStampInput> Stamps, FTDFluidGpuStampBinSet& InOutBinSet, TArray<FTDFluidGpuStampCandidate>& OutCandidates)
	{
		OutCandidates.Reset(Stamps.Num());
		for (int32 SourceIndex = 0; SourceIndex < Stamps.Num(); ++SourceIndex)
		{
			const FTDFluidGpuStampInput& Stamp = Stamps[SourceIndex];
			if (!IsFiniteStamp(Stamp))
			{
				++InOutBinSet.NonFiniteStampCount;
				continue;
			}

			const float EffectiveRadiusCm = FTDFluidGpuStampBins::ComputeEffectiveRadiusCm(Window, Stamp.RadiusCm);
			if (IsStampOutsideWindow(Window, Geometry, Stamp.CenterPlaneCm, EffectiveRadiusCm))
			{
				++InOutBinSet.OutsideWindowStampCount;
				continue;
			}

			FTDFluidGpuStampCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
			Candidate.SourceIndex = SourceIndex;
			Candidate.EffectiveRadiusCm = EffectiveRadiusCm;
			Candidate.PriorityDistanceSquared = FTDFluidGpuStampBins::ComputePriorityDistanceSquared(Window, Stamp);
		}
	}

	void KeepNearestCandidates(TArray<FTDFluidGpuStampCandidate>& InOutCandidates, int32 MaxStampsPerTick, FTDFluidGpuStampBinSet& InOutBinSet)
	{
		if (InOutCandidates.Num() <= MaxStampsPerTick)
		{
			return;
		}

		InOutCandidates.StableSort([](const FTDFluidGpuStampCandidate& Left, const FTDFluidGpuStampCandidate& Right)
		{
			return Left.PriorityDistanceSquared < Right.PriorityDistanceSquared;
		});

		InOutBinSet.OverTickLimitStampCount += InOutCandidates.Num() - MaxStampsPerTick;
		InOutCandidates.SetNum(MaxStampsPerTick, EAllowShrinking::No);
	}

	void KeepNearestBinEntries(TArray<uint32>& InOutBinEntries, const TArray<FTDFluidGpuStampCandidate>& Candidates, int32 MaxStampsPerBin, FTDFluidGpuStampBinSet& InOutBinSet)
	{
		if (InOutBinEntries.Num() <= MaxStampsPerBin)
		{
			return;
		}

		InOutBinEntries.StableSort([&Candidates](uint32 Left, uint32 Right)
		{
			return Candidates[static_cast<int32>(Left)].PriorityDistanceSquared < Candidates[static_cast<int32>(Right)].PriorityDistanceSquared;
		});

		InOutBinSet.BinOverflowEntryCount += InOutBinEntries.Num() - MaxStampsPerBin;
		InOutBinEntries.SetNum(MaxStampsPerBin, EAllowShrinking::No);
		InOutBinEntries.Sort();
	}

	void FillBins(const FTDFluidGpuStampWindowGeometry& Geometry, const TArray<FTDFluidGpuStampCandidate>& Candidates, TConstArrayView<FTDFluidGpuStampInput> Stamps, int32 MaxStampsPerBin, FTDFluidGpuStampBinSet& InOutBinSet)
	{
		constexpr int32 BinsPerAxis = TDFluidGpuSolverConstants::StampBinsPerAxis;
		TArray<FTDFluidGpuStampBinCell> BinCells;
		BinCells.SetNumUninitialized(Candidates.Num());

		int32 EntryCounts[TDFluidGpuSolverConstants::StampBinCount] = {};
		for (int32 StampIndex = 0; StampIndex < Candidates.Num(); ++StampIndex)
		{
			const FTDFluidGpuStampCandidate& Candidate = Candidates[StampIndex];
			const FTDFluidGpuStampBinCell BinCell = ComputeStampBinCell(Geometry, Stamps[Candidate.SourceIndex].CenterPlaneCm, Candidate.EffectiveRadiusCm);
			BinCells[StampIndex] = BinCell;
			for (int32 BinY = BinCell.MinBinY; BinY <= BinCell.MaxBinY; ++BinY)
			{
				for (int32 BinX = BinCell.MinBinX; BinX <= BinCell.MaxBinX; ++BinX)
				{
					++EntryCounts[BinY * BinsPerAxis + BinX];
				}
			}
		}

		int32 EntryOffsets[TDFluidGpuSolverConstants::StampBinCount] = {};
		int32 TotalEntryCount = 0;
		for (int32 BinIndex = 0; BinIndex < TDFluidGpuSolverConstants::StampBinCount; ++BinIndex)
		{
			EntryOffsets[BinIndex] = TotalEntryCount;
			TotalEntryCount += EntryCounts[BinIndex];
		}

		TArray<uint32> UnboundedEntries;
		UnboundedEntries.SetNumUninitialized(TotalEntryCount);
		int32 WriteCursors[TDFluidGpuSolverConstants::StampBinCount];
		FMemory::Memcpy(WriteCursors, EntryOffsets, sizeof(EntryOffsets));
		for (int32 StampIndex = 0; StampIndex < Candidates.Num(); ++StampIndex)
		{
			const FTDFluidGpuStampBinCell& BinCell = BinCells[StampIndex];
			for (int32 BinY = BinCell.MinBinY; BinY <= BinCell.MaxBinY; ++BinY)
			{
				for (int32 BinX = BinCell.MinBinX; BinX <= BinCell.MaxBinX; ++BinX)
				{
					UnboundedEntries[WriteCursors[BinY * BinsPerAxis + BinX]++] = static_cast<uint32>(StampIndex);
				}
			}
		}

		InOutBinSet.BinStampIndices.Reset(FMath::Min(TotalEntryCount, MaxStampsPerBin * TDFluidGpuSolverConstants::StampBinCount));
		TArray<uint32> BinEntries;
		for (int32 BinIndex = 0; BinIndex < TDFluidGpuSolverConstants::StampBinCount; ++BinIndex)
		{
			BinEntries.Reset();
			BinEntries.Append(UnboundedEntries.GetData() + EntryOffsets[BinIndex], EntryCounts[BinIndex]);
			KeepNearestBinEntries(BinEntries, Candidates, MaxStampsPerBin, InOutBinSet);

			FTDFluidGpuStampBinRange& Range = InOutBinSet.BinRanges[BinIndex];
			Range.FirstIndex = static_cast<uint32>(InOutBinSet.BinStampIndices.Num());
			Range.Count = static_cast<uint32>(BinEntries.Num());
			InOutBinSet.BinStampIndices.Append(BinEntries);
		}
	}
}

FTDFluidGpuStampBinSet FTDFluidGpuStampBins::Build(const FTDFluidGpuStampBinWindow& Window, TConstArrayView<FTDFluidGpuStampInput> Stamps, const FTDFluidGpuStampBinLimits& Limits)
{
	FTDFluidGpuStampBinSet BinSet;
	BinSet.BinRanges.SetNum(TDFluidGpuSolverConstants::StampBinCount);
	if (!IsUsableWindow(Window) || Stamps.IsEmpty())
	{
		return BinSet;
	}

	const int32 MaxStampsPerTick = FMath::Clamp(Limits.MaxStampsPerTick, 1, TDFluidGpuSolverConstants::MaximumStampsPerTick);
	const int32 MaxStampsPerBin = FMath::Clamp(Limits.MaxStampsPerBin, 1, TDFluidGpuSolverConstants::MaximumStampsPerBin);
	const FTDFluidGpuStampWindowGeometry Geometry = MakeWindowGeometry(Window);

	TArray<FTDFluidGpuStampCandidate> Candidates;
	CollectCandidates(Window, Geometry, Stamps, BinSet, Candidates);
	KeepNearestCandidates(Candidates, MaxStampsPerTick, BinSet);

	BinSet.Stamps.Reserve(Candidates.Num());
	for (const FTDFluidGpuStampCandidate& Candidate : Candidates)
	{
		BinSet.Stamps.Add(PackStamp(Window, Stamps[Candidate.SourceIndex], Candidate));
	}

	FillBins(Geometry, Candidates, Stamps, MaxStampsPerBin, BinSet);
	return BinSet;
}

uint32 FTDFluidGpuStampBins::AppendToFrame(const FTDFluidGpuStampBinSet& ActorBins, FTDFluidGpuStampBinSet& InOutFrameBins)
{
	const uint32 BinTableOffset = static_cast<uint32>(InOutFrameBins.BinRanges.Num());
	const uint32 StampOffset = static_cast<uint32>(InOutFrameBins.Stamps.Num());
	const uint32 EntryOffset = static_cast<uint32>(InOutFrameBins.BinStampIndices.Num());

	InOutFrameBins.Stamps.Append(ActorBins.Stamps);

	InOutFrameBins.BinRanges.Reserve(InOutFrameBins.BinRanges.Num() + ActorBins.BinRanges.Num());
	for (const FTDFluidGpuStampBinRange& Range : ActorBins.BinRanges)
	{
		FTDFluidGpuStampBinRange& FrameRange = InOutFrameBins.BinRanges.Add_GetRef(Range);
		FrameRange.FirstIndex += EntryOffset;
	}

	InOutFrameBins.BinStampIndices.Reserve(InOutFrameBins.BinStampIndices.Num() + ActorBins.BinStampIndices.Num());
	for (const uint32 StampIndex : ActorBins.BinStampIndices)
	{
		InOutFrameBins.BinStampIndices.Add(StampIndex + StampOffset);
	}

	InOutFrameBins.NonFiniteStampCount += ActorBins.NonFiniteStampCount;
	InOutFrameBins.OutsideWindowStampCount += ActorBins.OutsideWindowStampCount;
	InOutFrameBins.OverTickLimitStampCount += ActorBins.OverTickLimitStampCount;
	InOutFrameBins.BinOverflowEntryCount += ActorBins.BinOverflowEntryCount;
	return BinTableOffset;
}

int32 FTDFluidGpuStampBins::GetBinSpanCells(int32 Resolution)
{
	return FMath::Max(1, FMath::DivideAndRoundUp(Resolution, TDFluidGpuSolverConstants::StampBinsPerAxis));
}

float FTDFluidGpuStampBins::ComputeEffectiveRadiusCm(const FTDFluidGpuStampBinWindow& Window, float RadiusCm)
{
	if (Window.Resolution <= 0)
	{
		return RadiusCm;
	}

	const float CellSizeX = static_cast<float>(Window.ExtentCm.X / static_cast<float>(Window.Resolution));
	const float CellSizeY = static_cast<float>(Window.ExtentCm.Y / static_cast<float>(Window.Resolution));
	const float MinRadius = FMath::Max(CellSizeX, CellSizeY) * 0.5f;
	const float MaxRadius = static_cast<float>(FMath::Max(Window.ExtentCm.X, Window.ExtentCm.Y));
	return FMath::Clamp(RadiusCm, MinRadius, MaxRadius);
}

float FTDFluidGpuStampBins::ComputePriorityDistanceSquared(const FTDFluidGpuStampBinWindow& Window, const FTDFluidGpuStampInput& Stamp)
{
	return static_cast<float>(FVector2D::DistSquared(Stamp.CenterPlaneCm, Window.CenterPlaneCm));
}
