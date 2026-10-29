#include "FluidFX/TDFluidFoliageBendLayer.h"

#include "Async/ParallelFor.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/Crc.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuFoliageBend.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY(LogTDFluidFoliageBend);

namespace TDFluidFoliageBendLayerPrivate
{
	constexpr float MinimumRiseSeconds = 0.02f;
	constexpr float MinimumRecoverySeconds = 0.05f;
	constexpr float DirectionEpsilon = 1.0e-4f;
	constexpr double RippleGradientNumerator = 50.0;
	constexpr double ShiftToleranceCells = 0.01;

	FVector2f LimitBendLength(const FVector2f& Value)
	{
		const float ValueLength = Value.Size();
		if (ValueLength > 1.0f)
		{
			return Value / ValueLength;
		}

		return Value;
	}

	FVector2f ResolvePressDirection(const FVector2f& PreviousBend, const FVector2f& Target, const FVector2f& DensityGradient)
	{
		const float PreviousLength = PreviousBend.Size();
		if (PreviousLength > DirectionEpsilon)
		{
			return PreviousBend / PreviousLength;
		}

		const float TargetLength = Target.Size();
		if (TargetLength > DirectionEpsilon)
		{
			return Target / TargetLength;
		}

		const float GradientLength = DensityGradient.Size();
		if (GradientLength > DirectionEpsilon)
		{
			return -DensityGradient / GradientLength;
		}

		return FVector2f::ZeroVector;
	}

	bool ResolveAxisShift(double ShiftInCells, int32 Resolution, int32& OutShiftCells)
	{
		if (!FMath::IsFinite(ShiftInCells) || FMath::Abs(ShiftInCells) >= static_cast<double>(Resolution) + 0.5)
		{
			return false;
		}

		const double RoundedCells = FMath::FloorToDouble(ShiftInCells + 0.5);
		if (FMath::Abs(ShiftInCells - RoundedCells) > ShiftToleranceCells || FMath::Abs(RoundedCells) >= static_cast<double>(Resolution))
		{
			return false;
		}

		OutShiftCells = static_cast<int32>(RoundedCells);
		return true;
	}

	int32 ResolveNeighborCoordinate(int32 Coordinate, int32 Resolution, bool bWrapsArea)
	{
		if (bWrapsArea)
		{
			return (Coordinate % Resolution + Resolution) % Resolution;
		}

		return FMath::Clamp(Coordinate, 0, Resolution - 1);
	}

	struct FTDFluidFoliageBendCpuFrame
	{
		TConstArrayView<FFloat16Color> VelocityPixels;
		TConstArrayView<FFloat16Color> DensityPressurePixels;
		TConstArrayView<FFloat16Color> WaterSurfacePixels;
		int32 Resolution = 0;
		bool bReadsVelocity = false;
		bool bReadsDensity = false;
		bool bReadsWaterSurface = false;
		bool bWrapsArea = false;

		int32 ToNeighborIndex(int32 CellX, int32 CellY) const
		{
			return ResolveNeighborCoordinate(CellY, Resolution, bWrapsArea) * Resolution + ResolveNeighborCoordinate(CellX, Resolution, bWrapsArea);
		}

		FVector2f LoadVelocity(int32 CellIndex) const
		{
			if (!bReadsVelocity)
			{
				return FVector2f::ZeroVector;
			}

			const FFloat16Color& Pixel = VelocityPixels[CellIndex];
			return FVector2f(2.0f * Pixel.R.GetFloat() - 1.0f, 2.0f * Pixel.G.GetFloat() - 1.0f);
		}

		float LoadDensity(int32 CellX, int32 CellY) const
		{
			if (!bReadsDensity)
			{
				return 0.0f;
			}

			return DensityPressurePixels[ToNeighborIndex(CellX, CellY)].R.GetFloat();
		}

		float LoadWaterHeight(int32 CellX, int32 CellY) const
		{
			return 2.0f * WaterSurfacePixels[ToNeighborIndex(CellX, CellY)].B.GetFloat() - 1.0f;
		}

		FVector2f ComputeDensityGradient(int32 CellX, int32 CellY) const
		{
			return FVector2f(
				LoadDensity(CellX + 1, CellY) - LoadDensity(CellX - 1, CellY),
				LoadDensity(CellX, CellY + 1) - LoadDensity(CellX, CellY - 1));
		}

		FVector2f ComputeWaterGradient(int32 CellX, int32 CellY, const FVector2f& GradientScale) const
		{
			if (!bReadsWaterSurface)
			{
				return FVector2f::ZeroVector;
			}

			return FVector2f(
				(LoadWaterHeight(CellX + 1, CellY) - LoadWaterHeight(CellX - 1, CellY)) * GradientScale.X,
				(LoadWaterHeight(CellX, CellY + 1) - LoadWaterHeight(CellX, CellY - 1)) * GradientScale.Y);
		}
	};
}

FTDFluidFoliageBendLayer::~FTDFluidFoliageBendLayer()
{
	Release();
}

FTDFluidFoliageBendParameters FTDFluidFoliageBendLayer::MakeParameters(const FTDFluidSimulationConfig& Config, float DeltaSeconds, const FVector2D& CellSizeCm, bool bHasWaterSurface)
{
	using namespace TDFluidFoliageBendLayerPrivate;

	const float SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds) ? FMath::Max(DeltaSeconds, 0.0f) : 0.0f;
	const bool bIsSimplePainter = Config.SimMode == ETDFluidSimMode::SimplePainter;

	FTDFluidFoliageBendParameters Parameters;
	Parameters.RiseBlend = 1.0f - FMath::Exp(-SafeDeltaSeconds / FMath::Max(Config.BendVectorRiseSeconds, MinimumRiseSeconds));
	Parameters.RecoveryBlend = 1.0f - FMath::Exp(-SafeDeltaSeconds / FMath::Max(Config.BendVectorRecoverySeconds, MinimumRecoverySeconds));
	Parameters.PainterDensityWeight = bIsSimplePainter ? Config.BendFromPainterDensity : 0.0f;
	Parameters.RippleSlopeWeight = Config.BendFromRippleSlope;
	Parameters.RippleGradientScale = FVector2f(
		CellSizeCm.X > 0.0 ? static_cast<float>(RippleGradientNumerator / CellSizeCm.X) : 0.0f,
		CellSizeCm.Y > 0.0 ? static_cast<float>(RippleGradientNumerator / CellSizeCm.Y) : 0.0f);
	Parameters.bUsesVelocity = !bIsSimplePainter || Config.bWritesPainterVelocity;
	Parameters.bHasWaterSurface = bHasWaterSurface;
	Parameters.bWrapsArea = Config.AreaAddressMode == ETDFluidAreaAddressMode::Wrap;
	return Parameters;
}

FVector2f FTDFluidFoliageBendLayer::ComputeTarget(const FVector2f& Velocity, float Density, const FVector2f& DensityGradient, const FVector2f& RippleGradient, const FVector2f& PreviousBend, const FTDFluidFoliageBendParameters& Parameters)
{
	using namespace TDFluidFoliageBendLayerPrivate;

	FVector2f Target = LimitBendLength(Velocity);
	const float PainterPress = Parameters.PainterDensityWeight * Density;
	if (PainterPress > Target.Size())
	{
		const FVector2f PressDirection = ResolvePressDirection(PreviousBend, Target, DensityGradient);
		if (!PressDirection.IsZero())
		{
			Target = PressDirection * PainterPress;
		}
	}

	return LimitBendLength(Target - RippleGradient * Parameters.RippleSlopeWeight);
}

FVector2f FTDFluidFoliageBendLayer::AdvanceBend(const FVector2f& PreviousBend, const FVector2f& Target, const FTDFluidFoliageBendParameters& Parameters)
{
	const float Blend = Target.SizeSquared() > PreviousBend.SizeSquared() ? Parameters.RiseBlend : Parameters.RecoveryBlend;
	return PreviousBend + (Target - PreviousBend) * Blend;
}

bool FTDFluidFoliageBendLayer::ResolveShift(const FVector2D& PreviousCenterWorld, const FVector2D& CurrentCenterWorld, const FVector2D& CellSizeCm, int32 Resolution, FIntPoint& OutShiftCells)
{
	using namespace TDFluidFoliageBendLayerPrivate;

	OutShiftCells = FIntPoint::ZeroValue;
	if (Resolution <= 0 || !(CellSizeCm.X > 0.0) || !(CellSizeCm.Y > 0.0))
	{
		return false;
	}

	FIntPoint ShiftCells = FIntPoint::ZeroValue;
	if (!ResolveAxisShift((CurrentCenterWorld.X - PreviousCenterWorld.X) / CellSizeCm.X, Resolution, ShiftCells.X)
		|| !ResolveAxisShift((CurrentCenterWorld.Y - PreviousCenterWorld.Y) / CellSizeCm.Y, Resolution, ShiftCells.Y))
	{
		return false;
	}

	OutShiftCells = ShiftCells;
	return true;
}

bool FTDFluidFoliageBendLayer::ResolveEnabled(int32 ConsoleValue, bool bPresetEnabled)
{
	return ConsoleValue != 0 && bPresetEnabled;
}

void FTDFluidFoliageBendLayer::RequestReset()
{
	bNeedsReset = true;
}

bool FTDFluidFoliageBendLayer::ResolvesStateReset(ETDFluidFoliageBendBackend Backend, int32 Resolution, const FVector2D& CenterWorld, const FVector2D& CellSizeCm, FIntPoint& OutShiftCells) const
{
	OutShiftCells = FIntPoint::ZeroValue;
	if (bNeedsReset || CurrentBackend != Backend || CurrentResolution != Resolution)
	{
		return true;
	}

	return !ResolveShift(CurrentCenterWorld, CenterWorld, CellSizeCm, Resolution, OutShiftCells);
}

void FTDFluidFoliageBendLayer::CommitFrame(ETDFluidFoliageBendBackend Backend, int32 Resolution, const FVector2D& CenterWorld)
{
	CurrentBackend = Backend;
	CurrentResolution = Resolution;
	CurrentCenterWorld = CenterWorld;
	bNeedsReset = false;
	bHasOutput = true;
}

void FTDFluidFoliageBendLayer::AdvanceCpu(TConstArrayView<FFloat16Color> VelocityPixels, TConstArrayView<FFloat16Color> DensityPressurePixels, TConstArrayView<FFloat16Color> WaterSurfacePixels, int32 Resolution, const FVector2D& CenterWorld, const FVector2D& CellSizeCm, const FTDFluidFoliageBendParameters& Parameters)
{
	using namespace TDFluidFoliageBendLayerPrivate;

	if (Resolution <= 0 || CenterWorld.ContainsNaN())
	{
		RequestReset();
		return;
	}

	const int32 CellCount = Resolution * Resolution;
	FIntPoint ShiftCells = FIntPoint::ZeroValue;
	const bool bResetsState = ResolvesStateReset(ETDFluidFoliageBendBackend::Cpu, Resolution, CenterWorld, CellSizeCm, ShiftCells) || BendState.Num() != CellCount;
	if (BendState.Num() != CellCount)
	{
		BendState.Reset();
		BendState.SetNumZeroed(CellCount);
	}
	ScratchBendState.SetNumUninitialized(CellCount);

	FTDFluidFoliageBendCpuFrame Frame;
	Frame.VelocityPixels = VelocityPixels;
	Frame.DensityPressurePixels = DensityPressurePixels;
	Frame.WaterSurfacePixels = WaterSurfacePixels;
	Frame.Resolution = Resolution;
	Frame.bReadsVelocity = Parameters.bUsesVelocity && VelocityPixels.Num() == CellCount;
	Frame.bReadsDensity = DensityPressurePixels.Num() == CellCount;
	Frame.bReadsWaterSurface = Parameters.bHasWaterSurface && WaterSurfacePixels.Num() == CellCount;
	Frame.bWrapsArea = Parameters.bWrapsArea;

	const TArray<FVector2f>& PreviousState = BendState;
	TArray<FVector2f>& NextState = ScratchBendState;
	ParallelFor(Resolution, [&Frame, &PreviousState, &NextState, &Parameters, ShiftCells, bResetsState, Resolution](int32 CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const int32 CellIndex = CellY * Resolution + CellX;
			const int32 PreviousX = CellX + ShiftCells.X;
			const int32 PreviousY = CellY + ShiftCells.Y;
			const bool bHasPrevious = !bResetsState && PreviousX >= 0 && PreviousX < Resolution && PreviousY >= 0 && PreviousY < Resolution;
			const FVector2f PreviousBend = bHasPrevious ? PreviousState[PreviousY * Resolution + PreviousX] : FVector2f::ZeroVector;
			const FVector2f Target = ComputeTarget(
				Frame.LoadVelocity(CellIndex),
				Frame.LoadDensity(CellX, CellY),
				Frame.ComputeDensityGradient(CellX, CellY),
				Frame.ComputeWaterGradient(CellX, CellY, Parameters.RippleGradientScale),
				PreviousBend,
				Parameters);
			NextState[CellIndex] = AdvanceBend(PreviousBend, Target, Parameters);
		}
	});

	Swap(BendState, ScratchBendState);
	CommitFrame(ETDFluidFoliageBendBackend::Cpu, Resolution, CenterWorld);
}

void FTDFluidFoliageBendLayer::WriteCpuBendToAux(TArray<FFloat16Color>& InOutAuxPixels, int32 Resolution) const
{
	if (Resolution <= 0)
	{
		return;
	}

	const int32 CellCount = Resolution * Resolution;
	if (InOutAuxPixels.Num() != CellCount)
	{
		InOutAuxPixels.Reset(CellCount);
		InOutAuxPixels.AddZeroed(CellCount);
	}

	const bool bWritesState = !bNeedsReset && CurrentBackend == ETDFluidFoliageBendBackend::Cpu && BendState.Num() == CellCount;
	for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
	{
		FFloat16Color& Pixel = InOutAuxPixels[CellIndex];
		const FVector2f Bend = bWritesState ? BendState[CellIndex] : FVector2f::ZeroVector;
		Pixel.B = FFloat16(Bend.X);
		Pixel.A = FFloat16(Bend.Y);
	}
}

void FTDFluidFoliageBendLayer::SubmitGpu(UTexture* VelocityTexture, UTexture* DensityPressureTexture, UTexture* WaterSurfaceTexture, UTextureRenderTarget2D* AuxTarget, bool bPreservesAuxRedGreen, int32 Resolution, const FVector2D& CenterWorld, const FVector2D& CellSizeCm, const FTDFluidFoliageBendParameters& Parameters)
{
	FTextureRenderTargetResource* AuxResource = AuxTarget ? AuxTarget->GameThread_GetRenderTargetResource() : nullptr;
	FTextureResource* DensityPressureResource = DensityPressureTexture ? DensityPressureTexture->GetResource() : nullptr;
	if (!AuxResource || !DensityPressureResource || Resolution <= 0 || CenterWorld.ContainsNaN())
	{
		RequestReset();
		return;
	}

	if (!RenderState.IsValid())
	{
		RenderState = MakeShared<FTDFluidGpuFoliageBendState, ESPMode::ThreadSafe>();
		bNeedsReset = true;
	}

	FIntPoint ShiftCells = FIntPoint::ZeroValue;
	const bool bResetsState = ResolvesStateReset(ETDFluidFoliageBendBackend::Gpu, Resolution, CenterWorld, CellSizeCm, ShiftCells);

	FTextureResource* VelocityResource = VelocityTexture ? VelocityTexture->GetResource() : nullptr;
	FTextureResource* WaterSurfaceResource = WaterSurfaceTexture ? WaterSurfaceTexture->GetResource() : nullptr;

	FTDFluidGpuFoliageBendFrameInput FrameInput;
	FrameInput.Resolution = Resolution;
	FrameInput.ShiftCells = bResetsState ? FIntPoint::ZeroValue : ShiftCells;
	FrameInput.bResetsState = bResetsState;
	FrameInput.bUsesVelocity = Parameters.bUsesVelocity && VelocityResource != nullptr;
	FrameInput.bHasWaterSurface = Parameters.bHasWaterSurface && WaterSurfaceResource != nullptr;
	FrameInput.bPreservesAuxRedGreen = bPreservesAuxRedGreen;
	FrameInput.bWrapsArea = Parameters.bWrapsArea;
	FrameInput.RiseBlend = Parameters.RiseBlend;
	FrameInput.RecoveryBlend = Parameters.RecoveryBlend;
	FrameInput.PainterDensityWeight = Parameters.PainterDensityWeight;
	FrameInput.RippleSlopeWeight = Parameters.RippleSlopeWeight;
	FrameInput.RippleGradientScale = Parameters.RippleGradientScale;
	FrameInput.VelocityTexture = VelocityResource;
	FrameInput.DensityPressureTexture = DensityPressureResource;
	FrameInput.WaterSurfaceTexture = WaterSurfaceResource;
	FrameInput.AuxTarget = AuxResource;

	ENQUEUE_RENDER_COMMAND(TDFluidFoliageBendStep)([State = RenderState, FrameInput](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		AddTDFluidGpuFoliageBendPasses(GraphBuilder, *State, FrameInput);
		GraphBuilder.Execute();
	});

	if (CurrentBackend != ETDFluidFoliageBendBackend::Gpu)
	{
		BendState.Empty();
		ScratchBendState.Empty();
	}
	CommitFrame(ETDFluidFoliageBendBackend::Gpu, Resolution, CenterWorld);
}

void FTDFluidFoliageBendLayer::Release()
{
	bNeedsReset = true;
	bHasOutput = false;
	CurrentBackend = ETDFluidFoliageBendBackend::None;
	CurrentResolution = 0;
	BendState.Empty();
	ScratchBendState.Empty();
	if (!RenderState.IsValid())
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(TDFluidFoliageBendRelease)([State = MoveTemp(RenderState)](FRHICommandListImmediate&)
	{
		State->Release();
	});
	RenderState.Reset();
}

uint32 FTDFluidFoliageBendLayer::ComputeStateChecksum() const
{
	return FCrc::MemCrc32(BendState.GetData(), BendState.Num() * sizeof(FVector2f));
}
