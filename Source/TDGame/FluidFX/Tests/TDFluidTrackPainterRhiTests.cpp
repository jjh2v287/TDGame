#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/PlatformTime.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TDFluidGpuBatch.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidTrackPainterRhiTests, Log, All);

namespace TDFluidTrackPainterRhiTestPrivate
{
	constexpr float TrackStepSeconds = 1.0f / 60.0f;
	constexpr int32 TrackCompareResolution = 256;
	constexpr int32 TrackCompareStepCount = 10;
	constexpr int32 TrackCompareStampCount = 12;
	constexpr int32 TrackShiftCellCount = 16;
	constexpr int32 TrackCostResolution = 1024;
	constexpr int32 TrackCostStampCount = 64;
	constexpr int32 TrackCostWarmupFrameCount = 120;
	constexpr int32 TrackCostMeasuredFrameCount = 240;
	constexpr uint8 RedChannelMask = 1;
	constexpr uint8 GreenChannelMask = 2;
	constexpr uint8 BlueChannelMask = 4;

	struct FTDTrackChannelError
	{
		double Rms = 0.0;
		double Maximum = 0.0;
		int32 SampleCount = 0;
	};

	bool CanRunTrackRhiTest(FAutomationTestBase& Test, const TCHAR* TestName)
	{
		if (GUsingNullRHI || !FApp::CanEverRender())
		{
			Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI."), TestName));
			return false;
		}
		return true;
	}

	void ReportTrackLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidTrackPainterRhiTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	FTDFluidSimulationConfig MakeTrackPainterConfig(int32 Resolution)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FootprintPainter);
		Config.GridResolution = Resolution;
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = false;
		Config.DensityDissipation = 0.5f;
		Config.TrackBankStrength = 0.0f;
		Config.bWritesPainterVelocity = false;
		return Config;
	}

	void AppendTrackStamps(const FTDFluidSimulationConfig& Config, int32 StepIndex, int32 StampCount, bool bShaped, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		const double SpreadCm = Config.SimExtentCm.X * 0.2;
		for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
		{
			const double Angle = StepIndex * 0.07 + StampIndex * (UE_DOUBLE_TWO_PI / StampCount);
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(FMath::Cos(Angle) * SpreadCm, FMath::Sin(Angle) * SpreadCm * 0.6, 0.0);
			Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 240.0;
			Stamp.RadiusCm = static_cast<float>(Config.SimExtentCm.X * 0.02);
			Stamp.DensityStrength = 0.8f;
			Stamp.VelocityStrength = 1.0f;
			Stamp.ShapeAspectRatio = bShaped ? 2.5f : 1.0f;
			Stamp.ShapeHardness = bShaped ? 0.6f : 0.0f;
			Stamp.ShapeMajorAxis = bShaped ? FVector(FMath::Cos(Angle + 0.5), FMath::Sin(Angle + 0.5), 0.0) : FVector::ZeroVector;
			OutStamps.Add(Stamp);
		}
	}

	UTextureRenderTarget2D* CreateTrackTarget(int32 Resolution)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = FLinearColor::Transparent;
		Target->bAutoGenerateMips = false;
		Target->bSupportsUAV = true;
		Target->bCanCreateUAV = true;
		Target->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	bool ReadTrackTargetPixels(UTextureRenderTarget2D* Target, TArray<FFloat16Color>& OutPixels)
	{
		OutPixels.Reset();
		FTextureRenderTargetResource* TargetResource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		if (!TargetResource)
		{
			return false;
		}

		FRenderTarget* ReadableTarget = TargetResource;
		return ReadableTarget->ReadFloat16Pixels(OutPixels) && !OutPixels.IsEmpty();
	}

	FTDTrackChannelError MeasureChannelError(const TArray<FFloat16Color>& CpuPixels, const TArray<FFloat16Color>& GpuPixels, uint8 ChannelMask)
	{
		FTDTrackChannelError Error;
		if (CpuPixels.Num() != GpuPixels.Num())
		{
			Error.Rms = TNumericLimits<double>::Max();
			Error.Maximum = TNumericLimits<double>::Max();
			return Error;
		}

		double SquaredSum = 0.0;
		for (int32 PixelIndex = 0; PixelIndex < CpuPixels.Num(); ++PixelIndex)
		{
			const FLinearColor Cpu = CpuPixels[PixelIndex].GetFloats();
			const FLinearColor Gpu = GpuPixels[PixelIndex].GetFloats();
			const double Differences[3] = { Cpu.R - Gpu.R, Cpu.G - Gpu.G, Cpu.B - Gpu.B };
			for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
			{
				if ((ChannelMask & (1u << ChannelIndex)) == 0)
				{
					continue;
				}
				SquaredSum += Differences[ChannelIndex] * Differences[ChannelIndex];
				Error.Maximum = FMath::Max(Error.Maximum, FMath::Abs(Differences[ChannelIndex]));
				++Error.SampleCount;
			}
		}
		Error.Rms = Error.SampleCount > 0 ? FMath::Sqrt(SquaredSum / Error.SampleCount) : 0.0;
		return Error;
	}

	double MeasureChannelPeak(const TArray<FFloat16Color>& Pixels, uint8 ChannelIndex, float Neutral)
	{
		double Peak = 0.0;
		for (const FFloat16Color& Pixel : Pixels)
		{
			const FLinearColor Value = Pixel.GetFloats();
			const float Channel = ChannelIndex == 0 ? Value.R : (ChannelIndex == 1 ? Value.G : Value.B);
			Peak = FMath::Max(Peak, static_cast<double>(FMath::Abs(Channel - Neutral)));
		}
		return Peak;
	}

	struct FTDTrackCpuArm
	{
		void Initialize(const FTDFluidSimulationConfig& Config)
		{
			FTDFluidSimulationConfig CpuConfig = Config;
			CpuConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
			Solver.Initialize(CpuConfig, FVector2D::ZeroVector);
		}

		void Step(const TArray<FTDFluidBrushStamp>& Stamps)
		{
			Solver.ApplyBrushStamps(Stamps, TrackStepSeconds);
			Solver.StepSimulation(TrackStepSeconds);
		}

		void ShiftCells(int32 CellCount)
		{
			const FVector2D CellStep = Solver.CellToWorldCenter(1, 0) - Solver.CellToWorldCenter(0, 0);
			Solver.ShiftOriginForSlidingWindow(Solver.GetCenterWorld() + CellStep * CellCount);
		}

		void Pack(TArray<FFloat16Color>& OutDensityPixels, TArray<FFloat16Color>& OutVelocityPixels) const
		{
			Solver.PackOutputPixels(OutDensityPixels, OutVelocityPixels);
		}

		FTDFluidGrid2DSolver Solver;
	};

	uint64 NextTrackActorKey = 0;

	struct FTDTrackGpuArm
	{
		FTDTrackGpuArm() = default;
		FTDTrackGpuArm(const FTDTrackGpuArm&) = delete;
		FTDTrackGpuArm& operator=(const FTDTrackGpuArm&) = delete;

		~FTDTrackGpuArm()
		{
			ENQUEUE_RENDER_COMMAND(TDFluidTrackRhiRelease)([State = MoveTemp(RenderState)](FRHICommandListImmediate&) mutable
			{
				State.Reset();
			});
			FlushRenderingCommands();
		}

		void Initialize(const FTDFluidSimulationConfig& Config)
		{
			FTDFluidSimulationConfig GpuConfig = Config;
			GpuConfig.SolverBackend = ETDFluidSolverBackend::Gpu;
			Proxy.Initialize(GpuConfig, GpuConfig.GridResolution, FVector2D::ZeroVector);
			Proxy.AssignActorKey(++NextTrackActorKey);
			DensityTarget = TStrongObjectPtr<UTextureRenderTarget2D>(CreateTrackTarget(Proxy.GetResolution()));
			VelocityTarget = TStrongObjectPtr<UTextureRenderTarget2D>(CreateTrackTarget(Proxy.GetResolution()));
		}

		bool Step(const TArray<FTDFluidBrushStamp>& Stamps)
		{
			Proxy.PrepareStep(TrackStepSeconds, TConstArrayView<FTDFluidBrushStamp>(Stamps), FVector2D::ZeroVector, false);
			FTDFluidGpuFrameInputs FrameInputs;
			FrameInputs.FrameNumber = ++FrameNumber;
			FTDFluidGpuActorInput& ActorInput = FrameInputs.Actors.AddDefaulted_GetRef();
			if (!Proxy.GatherStep(FrameInputs.FrameNumber, 0, DensityTarget->GameThread_GetRenderTargetResource(), VelocityTarget->GameThread_GetRenderTargetResource(), ActorInput))
			{
				return false;
			}

			ENQUEUE_RENDER_COMMAND(TDFluidTrackRhiStep)([State = RenderState, SubmittedInputs = MoveTemp(FrameInputs)](FRHICommandListImmediate& RHICmdList)
			{
				State->ExecuteFrame(RHICmdList, SubmittedInputs);
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
			return true;
		}

		void ShiftCells(int32 CellCount)
		{
			const FVector2D CellStep = Proxy.CellToPlaneCenter(1, 0) - Proxy.CellToPlaneCenter(0, 0);
			Proxy.ShiftWindowTowards(Proxy.GetCenterPlaneCm() + CellStep * CellCount);
		}

		bool Read(TArray<FFloat16Color>& OutDensityPixels, TArray<FFloat16Color>& OutVelocityPixels)
		{
			return ReadTrackTargetPixels(DensityTarget.Get(), OutDensityPixels) && ReadTrackTargetPixels(VelocityTarget.Get(), OutVelocityPixels);
		}

		FTDFluidGpuSolverProxy Proxy;
		TSharedPtr<FTDFluidGpuSolverRenderState, ESPMode::ThreadSafe> RenderState = MakeShared<FTDFluidGpuSolverRenderState, ESPMode::ThreadSafe>();
		TStrongObjectPtr<UTextureRenderTarget2D> DensityTarget;
		TStrongObjectPtr<UTextureRenderTarget2D> VelocityTarget;
		uint32 FrameNumber = 0;
	};

	struct FTDTrackPairResult
	{
		TArray<FFloat16Color> CpuDensity;
		TArray<FFloat16Color> CpuVelocity;
		TArray<FFloat16Color> GpuDensity;
		TArray<FFloat16Color> GpuVelocity;
	};

	bool RunTrackPair(FAutomationTestBase& Test, const FString& Label, const FTDFluidSimulationConfig& Config, int32 StepCount, bool bShaped, FTDTrackCpuArm& CpuArm, FTDTrackGpuArm& GpuArm, int32 FirstStepIndex, FTDTrackPairResult& OutResult)
	{
		for (int32 StepIndex = FirstStepIndex; StepIndex < FirstStepIndex + StepCount; ++StepIndex)
		{
			TArray<FTDFluidBrushStamp> Stamps;
			AppendTrackStamps(Config, StepIndex, TrackCompareStampCount, bShaped, Stamps);
			CpuArm.Step(Stamps);
			if (!Test.TestTrue(*FString::Printf(TEXT("%s GPU step %d gathered"), *Label, StepIndex), GpuArm.Step(Stamps)))
			{
				return false;
			}
		}

		CpuArm.Pack(OutResult.CpuDensity, OutResult.CpuVelocity);
		if (!Test.TestTrue(*(Label + TEXT(" read the GPU output targets")), GpuArm.Read(OutResult.GpuDensity, OutResult.GpuVelocity)))
		{
			return false;
		}
		return Test.TestEqual(*(Label + TEXT(" CPU and GPU outputs hold the same cell count")), OutResult.GpuDensity.Num(), OutResult.CpuDensity.Num());
	}

	bool RunGpuOnly(FAutomationTestBase& Test, const FTDFluidSimulationConfig& Config, int32 StepCount, TArray<FFloat16Color>& OutDensityPixels, TArray<FFloat16Color>& OutVelocityPixels)
	{
		FTDTrackGpuArm GpuArm;
		GpuArm.Initialize(Config);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			TArray<FTDFluidBrushStamp> Stamps;
			AppendTrackStamps(Config, StepIndex, TrackCompareStampCount, true, Stamps);
			if (!GpuArm.Step(Stamps))
			{
				return false;
			}
		}
		return Test.TestTrue(TEXT("Replay read the GPU output targets"), GpuArm.Read(OutDensityPixels, OutVelocityPixels));
	}

	double ComputeMedian(TArray<double> Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		return Values[Values.Num() / 2];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackEllipseStampMatchesCpuRhiTest,
	"TDGame.FluidFXRhi.Track.EllipseStampMatchesCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackEllipseStampMatchesCpuRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackPainterRhiTestPrivate;
	if (!CanRunTrackRhiTest(*this, TEXT("TDGame.FluidFXRhi.Track.EllipseStampMatchesCpu")))
	{
		return false;
	}

	const FTDFluidSimulationConfig PainterConfig = MakeTrackPainterConfig(TrackCompareResolution);
	FTDTrackCpuArm PainterCpu;
	FTDTrackGpuArm PainterGpu;
	PainterCpu.Initialize(PainterConfig);
	PainterGpu.Initialize(PainterConfig);
	FTDTrackPairResult PainterResult;
	if (!RunTrackPair(*this, TEXT("Painter ellipse"), PainterConfig, TrackCompareStepCount, true, PainterCpu, PainterGpu, 0, PainterResult))
	{
		return false;
	}
	const FTDTrackChannelError PainterError = MeasureChannelError(PainterResult.CpuDensity, PainterResult.GpuDensity, RedChannelMask);

	FTDFluidSimulationConfig FluidConfig = PainterConfig;
	FluidConfig.SimMode = ETDFluidSimMode::FluidSim;
	FTDTrackCpuArm FluidCpu;
	FTDTrackGpuArm FluidGpu;
	FluidCpu.Initialize(FluidConfig);
	FluidGpu.Initialize(FluidConfig);
	FTDTrackPairResult FluidResult;
	if (!RunTrackPair(*this, TEXT("FluidSim ellipse"), FluidConfig, 1, true, FluidCpu, FluidGpu, 0, FluidResult))
	{
		return false;
	}
	const FTDTrackChannelError FluidError = MeasureChannelError(FluidResult.CpuDensity, FluidResult.GpuDensity, RedChannelMask);

	ReportTrackLine(*this, FString::Printf(TEXT("TDFluidTrackEllipse: painter_rms=%.6f painter_max=%.6f painter_peak=%.4f fluid_rms=%.6f fluid_max=%.6f fluid_peak=%.4f"), PainterError.Rms, PainterError.Maximum, MeasureChannelPeak(PainterResult.CpuDensity, 0, 0.0f), FluidError.Rms, FluidError.Maximum, MeasureChannelPeak(FluidResult.CpuDensity, 0, 0.0f)));
	TestTrue(TEXT("Painter ellipse reference carries density"), MeasureChannelPeak(PainterResult.CpuDensity, 0, 0.0f) > 0.1);
	TestTrue(*FString::Printf(TEXT("Painter ellipse density RMS %.6f <= 1e-3"), PainterError.Rms), PainterError.Rms <= 1.0e-3);
	TestTrue(*FString::Printf(TEXT("FluidSim ellipse density RMS %.6f <= 1e-3"), FluidError.Rms), FluidError.Rms <= 1.0e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackBankMatchesCpuRhiTest,
	"TDGame.FluidFXRhi.Track.BankMatchesCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackBankMatchesCpuRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackPainterRhiTestPrivate;
	if (!CanRunTrackRhiTest(*this, TEXT("TDGame.FluidFXRhi.Track.BankMatchesCpu")))
	{
		return false;
	}

	FTDFluidSimulationConfig ClampConfig = MakeTrackPainterConfig(TrackCompareResolution);
	ClampConfig.TrackBankStrength = 1.0f;
	ClampConfig.AreaAddressMode = ETDFluidAreaAddressMode::Clamp;
	FTDTrackCpuArm CpuArm;
	FTDTrackGpuArm ClampGpuArm;
	CpuArm.Initialize(ClampConfig);
	ClampGpuArm.Initialize(ClampConfig);
	FTDTrackPairResult ClampResult;
	if (!RunTrackPair(*this, TEXT("Bank clamp"), ClampConfig, TrackCompareStepCount, true, CpuArm, ClampGpuArm, 0, ClampResult))
	{
		return false;
	}

	const FTDTrackChannelError Error = MeasureChannelError(ClampResult.CpuDensity, ClampResult.GpuDensity, GreenChannelMask);
	const double BankPeak = MeasureChannelPeak(ClampResult.CpuDensity, 1, 0.5f);
	const FTDTrackChannelError DensityError = MeasureChannelError(ClampResult.CpuDensity, ClampResult.GpuDensity, RedChannelMask);
	ReportTrackLine(*this, FString::Printf(TEXT("TDFluidTrackBank: Bank clamp g_rms=%.6f g_max=%.6f g_peak=%.4f r_rms=%.6f r_max=%.6f r_peak_cpu=%.4f r_peak_gpu=%.4f"), Error.Rms, Error.Maximum, BankPeak, DensityError.Rms, DensityError.Maximum, MeasureChannelPeak(ClampResult.CpuDensity, 0, 0.0f), MeasureChannelPeak(ClampResult.GpuDensity, 0, 0.0f)));
	TestTrue(TEXT("Bank clamp CPU bank moves G away from neutral"), BankPeak > 0.01);
	TestTrue(*FString::Printf(TEXT("Bank clamp G RMS %.6f <= 2e-3"), Error.Rms), Error.Rms <= 2.0e-3);
	TestTrue(*FString::Printf(TEXT("Bank clamp G max %.6f <= 1e-2"), Error.Maximum), Error.Maximum <= 1.0e-2);

	FTDFluidSimulationConfig WrapConfig = ClampConfig;
	WrapConfig.AreaAddressMode = ETDFluidAreaAddressMode::Wrap;
	TArray<FFloat16Color> WrapDensity;
	TArray<FFloat16Color> WrapVelocity;
	FTDTrackGpuArm WrapGpuArm;
	WrapGpuArm.Initialize(WrapConfig);
	for (int32 StepIndex = 0; StepIndex < TrackCompareStepCount; ++StepIndex)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		AppendTrackStamps(WrapConfig, StepIndex, TrackCompareStampCount, true, Stamps);
		if (!TestTrue(*FString::Printf(TEXT("Bank wrap GPU step %d gathered"), StepIndex), WrapGpuArm.Step(Stamps)))
		{
			return false;
		}
	}
	if (!TestTrue(TEXT("Bank wrap read the GPU output targets"), WrapGpuArm.Read(WrapDensity, WrapVelocity)) || !TestEqual(TEXT("Bank wrap and clamp hold the same cell count"), WrapDensity.Num(), ClampResult.GpuDensity.Num()))
	{
		return false;
	}
	const FTDTrackChannelError WrapError = MeasureChannelError(ClampResult.GpuDensity, WrapDensity, GreenChannelMask);
	ReportTrackLine(*this, FString::Printf(TEXT("TDFluidTrackBank: Bank wrap_vs_clamp g_rms=%.6f g_max=%.6f g_peak_wrap=%.4f"), WrapError.Rms, WrapError.Maximum, MeasureChannelPeak(WrapDensity, 1, 0.5f)));
	TestTrue(TEXT("Bank wrap GPU bank moves G away from neutral"), MeasureChannelPeak(WrapDensity, 1, 0.5f) > 0.01);
	TestTrue(*FString::Printf(TEXT("Bank wrap interior G RMS against clamp %.6f <= 2e-3"), WrapError.Rms), WrapError.Rms <= 2.0e-3);
	TestTrue(*FString::Printf(TEXT("Bank wrap interior G max against clamp %.6f <= 1e-2"), WrapError.Maximum), WrapError.Maximum <= 1.0e-2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackPainterVelocityMatchesCpuRhiTest,
	"TDGame.FluidFXRhi.Track.PainterVelocityMatchesCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackPainterVelocityMatchesCpuRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackPainterRhiTestPrivate;
	const int64 CellCount = static_cast<int64>(TrackCostResolution) * TrackCostResolution;
	const int64 VelocityOffBytes = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::SimplePainter, TrackCostResolution).GetTotalBytes();
	const int64 VelocityOnBytes = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::SimplePainter, TrackCostResolution, false, false, 0, 0, true).GetTotalBytes();
	TestEqual(TEXT("Painter velocity adds 16 bytes per cell and drops the 4x4 placeholder"), VelocityOnBytes - VelocityOffBytes, CellCount * 16 - 4 * 4 * 8);
	if (!CanRunTrackRhiTest(*this, TEXT("TDGame.FluidFXRhi.Track.PainterVelocityMatchesCpu")))
	{
		return false;
	}

	FTDFluidSimulationConfig Config = MakeTrackPainterConfig(TrackCompareResolution);
	Config.bWritesPainterVelocity = true;
	FTDTrackCpuArm CpuArm;
	FTDTrackGpuArm GpuArm;
	CpuArm.Initialize(Config);
	GpuArm.Initialize(Config);
	TestTrue(TEXT("Painter velocity proxy asks for a velocity output"), GpuArm.Proxy.UsesVelocityOutput());

	FTDTrackPairResult Result;
	if (!RunTrackPair(*this, TEXT("Painter velocity"), Config, TrackCompareStepCount, false, CpuArm, GpuArm, 0, Result))
	{
		return false;
	}
	const FTDTrackChannelError Error = MeasureChannelError(Result.CpuVelocity, Result.GpuVelocity, RedChannelMask | GreenChannelMask | BlueChannelMask);
	const double SpeedPeak = MeasureChannelPeak(Result.CpuVelocity, 2, 0.0f);

	CpuArm.ShiftCells(TrackShiftCellCount);
	GpuArm.ShiftCells(TrackShiftCellCount);
	FTDTrackPairResult ShiftedResult;
	if (!RunTrackPair(*this, TEXT("Painter velocity shifted"), Config, 2, false, CpuArm, GpuArm, TrackCompareStepCount, ShiftedResult))
	{
		return false;
	}
	const FTDTrackChannelError ShiftedError = MeasureChannelError(ShiftedResult.CpuVelocity, ShiftedResult.GpuVelocity, RedChannelMask | GreenChannelMask | BlueChannelMask);

	ReportTrackLine(*this, FString::Printf(TEXT("TDFluidTrackPainterVelocity: rgb_rms=%.6f rgb_max=%.6f speed_peak=%.4f shifted_rms=%.6f shifted_max=%.6f"), Error.Rms, Error.Maximum, SpeedPeak, ShiftedError.Rms, ShiftedError.Maximum));
	TestTrue(TEXT("CPU painter velocity reference moves"), SpeedPeak > 0.05);
	TestTrue(*FString::Printf(TEXT("Painter velocity RMS %.6f <= 1e-3"), Error.Rms), Error.Rms <= 1.0e-3);
	TestTrue(*FString::Printf(TEXT("Painter velocity after a 16 cell window shift RMS %.6f <= 1e-3"), ShiftedError.Rms), ShiftedError.Rms <= 1.0e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackDeterministicReplayRhiTest,
	"TDGame.FluidFXRhi.Track.DeterministicReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackDeterministicReplayRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackPainterRhiTestPrivate;
	if (!CanRunTrackRhiTest(*this, TEXT("TDGame.FluidFXRhi.Track.DeterministicReplay")))
	{
		return false;
	}

	FTDFluidSimulationConfig Config = MakeTrackPainterConfig(TrackCompareResolution);
	Config.TrackBankStrength = 1.0f;
	Config.bWritesPainterVelocity = true;
	TArray<FFloat16Color> FirstDensity;
	TArray<FFloat16Color> FirstVelocity;
	TArray<FFloat16Color> SecondDensity;
	TArray<FFloat16Color> SecondVelocity;
	if (!RunGpuOnly(*this, Config, TrackCompareStepCount, FirstDensity, FirstVelocity) || !RunGpuOnly(*this, Config, TrackCompareStepCount, SecondDensity, SecondVelocity))
	{
		return false;
	}

	const uint32 FirstDensityCrc = FCrc::MemCrc32(FirstDensity.GetData(), FirstDensity.Num() * sizeof(FFloat16Color));
	const uint32 SecondDensityCrc = FCrc::MemCrc32(SecondDensity.GetData(), SecondDensity.Num() * sizeof(FFloat16Color));
	const uint32 FirstVelocityCrc = FCrc::MemCrc32(FirstVelocity.GetData(), FirstVelocity.Num() * sizeof(FFloat16Color));
	const uint32 SecondVelocityCrc = FCrc::MemCrc32(SecondVelocity.GetData(), SecondVelocity.Num() * sizeof(FFloat16Color));
	ReportTrackLine(*this, FString::Printf(TEXT("TDFluidTrackReplay: density_crc=%08x/%08x velocity_crc=%08x/%08x"), FirstDensityCrc, SecondDensityCrc, FirstVelocityCrc, SecondVelocityCrc));
	TestTrue(TEXT("Replay density carries a trail"), MeasureChannelPeak(FirstDensity, 0, 0.0f) > 0.1);
	TestEqual(TEXT("Replay density output CRC matches"), SecondDensityCrc, FirstDensityCrc);
	TestEqual(TEXT("Replay velocity output CRC matches"), SecondVelocityCrc, FirstVelocityCrc);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackPainterOffscreenCostRhiTest,
	"TDGame.FluidFXRhi.Track.PainterOffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackPainterOffscreenCostRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackPainterRhiTestPrivate;
	if (!CanRunTrackRhiTest(*this, TEXT("TDGame.FluidFXRhi.Track.PainterOffscreenCost")))
	{
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> CaseValues;
	for (const bool bUsesBank : { false, true })
	{
		for (const bool bWritesVelocity : { false, true })
		{
			FTDFluidSimulationConfig Config = MakeTrackPainterConfig(TrackCostResolution);
			Config.TrackBankStrength = bUsesBank ? 1.0f : 0.0f;
			Config.bWritesPainterVelocity = bWritesVelocity;
			FTDTrackGpuArm GpuArm;
			GpuArm.Initialize(Config);
			TArray<double> FrameMilliseconds;
			FrameMilliseconds.Reserve(TrackCostMeasuredFrameCount);
			for (int32 FrameIndex = 0; FrameIndex < TrackCostWarmupFrameCount + TrackCostMeasuredFrameCount; ++FrameIndex)
			{
				TArray<FTDFluidBrushStamp> Stamps;
				AppendTrackStamps(Config, FrameIndex, TrackCostStampCount, true, Stamps);
				const double StartSeconds = FPlatformTime::Seconds();
				if (!TestTrue(TEXT("Cost run GPU step gathered"), GpuArm.Step(Stamps)))
				{
					return false;
				}
				if (FrameIndex >= TrackCostWarmupFrameCount)
				{
					FrameMilliseconds.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
				}
			}

			const double MedianMilliseconds = ComputeMedian(FrameMilliseconds);
			ReportTrackLine(*this, FString::Printf(TEXT("TDFluidTrackPainterCost: resolution=%d stamps=%d bank=%d velocity=%d median_frame_ms=%.4f"), GpuArm.Proxy.GetResolution(), TrackCostStampCount, bUsesBank ? 1 : 0, bWritesVelocity ? 1 : 0, MedianMilliseconds));
			TSharedRef<FJsonObject> CaseObject = MakeShared<FJsonObject>();
			CaseObject->SetBoolField(TEXT("bank"), bUsesBank);
			CaseObject->SetBoolField(TEXT("velocity"), bWritesVelocity);
			CaseObject->SetNumberField(TEXT("resolution"), GpuArm.Proxy.GetResolution());
			CaseObject->SetNumberField(TEXT("median_frame_ms"), MedianMilliseconds);
			CaseValues.Add(MakeShared<FJsonValueObject>(CaseObject));
			TestTrue(TEXT("Cost run measured frames"), FrameMilliseconds.Num() == TrackCostMeasuredFrameCount);
		}
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("measure"), TEXT("wall time of one submitted and GPU idle blocked frame, median of 240 after 120 warmup"));
	RootObject->SetNumberField(TEXT("stamps"), TrackCostStampCount);
	RootObject->SetArrayField(TEXT("cases"), CaseValues);
	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(RootObject, Writer);
	const FString OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("wave2"), TEXT("fx12c_painter_cost.json"));
	TestTrue(TEXT("Cost JSON written"), FFileHelper::SaveStringToFile(JsonText, *OutputPath));
	ReportTrackLine(*this, FString::Printf(TEXT("TDFluidTrackPainterCost: json=%s"), *OutputPath));
	return true;
}

#endif
