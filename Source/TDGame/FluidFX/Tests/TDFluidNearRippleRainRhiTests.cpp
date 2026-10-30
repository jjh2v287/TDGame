#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FluidFX/TDFluidNearRippleLayer.h"
#include "FluidFX/TDFluidTypes.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidNearRippleRainRhiTests, Log, All);

namespace TDFluidNearRippleRainRhiTestPrivate
{
	constexpr int32 NearRainRhiResolution = 256;
	constexpr float NearRainRhiStepSeconds = 1.0f / 60.0f;
	constexpr float NearRainRhiDefaultExtentCm = 600.0f;
	constexpr float NearRainRhiExactCellExtentCm = 512.0f;
	constexpr double NearRainRhiOpenWaterExtentCm = 100000.0;
	constexpr float NearRainRhiDepthReferenceCm = 300.0f;
	constexpr float NearRainRhiObstacleRangeCm = 400.0f;
	constexpr int32 NearRainRhiMaskSize = 4;
	constexpr int32 NearRainRhiReplayFrameCount = 40;
	constexpr int32 NearRainRhiReplayShiftStartFrame = 20;
	constexpr int32 NearRainRhiShiftCellCount = 16;
	constexpr int32 NearRainRhiShiftFramesBefore = 60;
	constexpr int32 NearRainRhiShiftFramesAfter = 30;
	constexpr int32 NearRainRhiDependenceMarginCells = 2;
	constexpr int32 NearRainRhiMinimumActiveCells = 500;
	constexpr float NearRainRhiActiveHeightCm = 1.0e-5f;
	constexpr float NearRainRhiAbsoluteToleranceCm = 1.0e-4f;
	constexpr float NearRainRhiRelativeTolerance = 1.0f / 1024.0f;

	bool CanRunNearRainRhiTest(FAutomationTestBase& Test, const FString& CaseLabel)
	{
		const bool bHasActiveRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidNearRippleRainRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), *CaseLabel, bHasActiveRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasActiveRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
		return false;
	}

	void ReportNearRainLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidNearRippleRainRhiTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	UTextureRenderTarget2D* CreateNearRainOutputTarget(int32 Resolution)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = FLinearColor::Transparent;
		Target->bAutoGenerateMips = false;
		Target->bSupportsUAV = true;
		Target->bCanCreateUAV = true;
		Target->Filter = TF_Bilinear;
		Target->AddressX = TA_Wrap;
		Target->AddressY = TA_Wrap;
		Target->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	UTexture2D* CreateOpenWaterSurfaceTexture()
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(NearRainRhiMaskSize, NearRainRhiMaskSize, PF_FloatRGBA);
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}

		Texture->SRGB = false;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FFloat16Color* Pixels = static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 PixelIndex = 0; PixelIndex < NearRainRhiMaskSize * NearRainRhiMaskSize; ++PixelIndex)
		{
			Pixels[PixelIndex] = FFloat16Color(FLinearColor(1.0f, 0.0f, 1.0f, 0.0f));
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	FTDFluidWaterNearRippleSettings MakeNearRainSettings(float ExtentCm)
	{
		FTDFluidWaterNearRippleSettings Settings;
		Settings.bEnabled = true;
		Settings.Resolution = NearRainRhiResolution;
		Settings.ExtentCm = ExtentCm;
		return Settings;
	}

	FTDFluidNearRippleMaskInputs MakeNearRainMasks(UTexture& SurfaceTexture)
	{
		FTDFluidNearRippleMaskInputs Masks;
		Masks.WaterSurfaceTexture = &SurfaceTexture;
		Masks.MainWindowExtentCm = FVector2D(NearRainRhiOpenWaterExtentCm, NearRainRhiOpenWaterExtentCm);
		Masks.WaterDepthReferenceCm = NearRainRhiDepthReferenceCm;
		Masks.ObstacleDistanceOutputRangeCm = NearRainRhiObstacleRangeCm;
		return Masks;
	}

	FTDFluidNearRippleRainInput MakeNearRainInput(float DropsPerSquareMeterPerSec)
	{
		FTDFluidNearRippleRainInput Input;
		Input.DropsPerSquareMeterPerSec = DropsPerSquareMeterPerSec;
		Input.DropImpulseCm = 0.15f;
		Input.DropRadiusCm = 4.0f;
		Input.RandomSeed = 29u;
		return Input;
	}

	struct FTDNearRainRun
	{
		explicit FTDNearRainRun(const FTDFluidWaterNearRippleSettings& Settings)
			: Output(CreateNearRainOutputTarget(FTDFluidNearRippleLayer::SanitizeResolution(Settings.Resolution)))
		{
			Layer.Configure(Settings);
		}

		FTDNearRainRun(const FTDNearRainRun&) = delete;
		FTDNearRainRun& operator=(const FTDNearRainRun&) = delete;

		void Submit(const FTDFluidNearRippleMaskInputs& Masks, const FVector2D& CenterWorldXY)
		{
			Layer.SubmitStep(Masks, CenterWorldXY, NearRainRhiStepSeconds, *Output);
		}

		bool ReadPixels(TArray<FFloat16Color>& OutPixels)
		{
			ENQUEUE_RENDER_COMMAND(TDFluidNearRainRhiWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
			OutPixels.Reset();
			FTextureRenderTargetResource* Resource = Output->GameThread_GetRenderTargetResource();
			if (!Resource)
			{
				return false;
			}

			FRenderTarget* ReadableTarget = Resource;
			return ReadableTarget->ReadFloat16Pixels(OutPixels) && OutPixels.Num() == Output->SizeX * Output->SizeY;
		}

		FTDFluidNearRippleLayer Layer;
		TStrongObjectPtr<UTextureRenderTarget2D> Output;
	};

	int32 ToNearRainPixelIndex(const FIntPoint& WorldCell, int32 Resolution)
	{
		const FIntPoint StorageCell = FTDFluidNearRippleLayer::ComputeStorageCell(WorldCell, Resolution);
		return StorageCell.Y * Resolution + StorageCell.X;
	}

	int32 CountNearRainActiveHeights(const TArray<FFloat16Color>& Pixels)
	{
		int32 ActiveCount = 0;
		for (const FFloat16Color& Pixel : Pixels)
		{
			ActiveCount += FMath::Abs(Pixel.R.GetFloat()) > NearRainRhiActiveHeightCm ? 1 : 0;
		}
		return ActiveCount;
	}

	bool AreNearRainPixelsIdentical(const TArray<FFloat16Color>& Left, const TArray<FFloat16Color>& Right)
	{
		return Left.Num() > 0 && Left.Num() == Right.Num() && FMemory::Memcmp(Left.GetData(), Right.GetData(), Left.Num() * sizeof(FFloat16Color)) == 0;
	}

	void AddNearRainReplaySource(FTDFluidNearRippleLayer& Layer, int32 FrameIndex)
	{
		Layer.AddMovingSource(FVector2D(-60.0 + 2.0 * FrameIndex, 30.0), FVector2D(2.0 / NearRainRhiStepSeconds, 0.0), 30.0f);
	}

	FVector2D ResolveNearRainReplayCenter(int32 FrameIndex)
	{
		return FVector2D(2.0 * FMath::Max(FrameIndex - NearRainRhiReplayShiftStartFrame, 0), -1.0 * FMath::Max(FrameIndex - NearRainRhiReplayShiftStartFrame, 0));
	}

	void RunNearRainReplayFrames(FTDNearRainRun& Run, const FTDFluidNearRippleMaskInputs& Masks, const FTDFluidNearRippleRainInput* RainInput)
	{
		if (RainInput)
		{
			Run.Layer.SetRainInput(*RainInput);
		}

		for (int32 FrameIndex = 0; FrameIndex < NearRainRhiReplayFrameCount; ++FrameIndex)
		{
			AddNearRainReplaySource(Run.Layer, FrameIndex);
			Run.Submit(Masks, ResolveNearRainReplayCenter(FrameIndex));
		}
	}

	int32 CountNearRainPixelMismatches(const TArray<FFloat16Color>& Left, const TArray<FFloat16Color>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return FMath::Max(Left.Num(), Right.Num());
		}

		int32 MismatchCount = 0;
		for (int32 PixelIndex = 0; PixelIndex < Left.Num(); ++PixelIndex)
		{
			MismatchCount += FMemory::Memcmp(&Left[PixelIndex], &Right[PixelIndex], sizeof(FFloat16Color)) == 0 ? 0 : 1;
		}
		return MismatchCount;
	}

	void WarmUpNearRainPipeline(const FTDFluidWaterNearRippleSettings& Settings, const FTDFluidNearRippleMaskInputs& Masks, const FTDFluidNearRippleRainInput& RainInput)
	{
		FTDNearRainRun Run(Settings);
		Run.Layer.SetRainInput(RainInput);
		TArray<FFloat16Color> DiscardedPixels;
		for (int32 FrameIndex = 0; FrameIndex < NearRainRhiReplayFrameCount; ++FrameIndex)
		{
			AddNearRainReplaySource(Run.Layer, FrameIndex);
			Run.Submit(Masks, ResolveNearRainReplayCenter(FrameIndex));
			Run.ReadPixels(DiscardedPixels);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleRainRhiDeterministicReplayTest,
	"TDGame.FluidFXRhi.Rain.NearRainDeterministicReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleRainRhiDeterministicReplayTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNearRippleRainRhiTestPrivate;
	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Rain.NearRainDeterministicReplay");
	if (!CanRunNearRainRhiTest(*this, CaseLabel))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> SurfaceTexture(CreateOpenWaterSurfaceTexture());
	FlushRenderingCommands();
	if (!TestNotNull(*(CaseLabel + TEXT(" created the water surface mask")), SurfaceTexture.Get()))
	{
		return false;
	}

	const FTDFluidWaterNearRippleSettings Settings = MakeNearRainSettings(NearRainRhiDefaultExtentCm);
	const FTDFluidNearRippleMaskInputs Masks = MakeNearRainMasks(*SurfaceTexture);
	const FTDFluidNearRippleRainInput RainOn = MakeNearRainInput(2000.0f);
	const FTDFluidNearRippleRainInput RainZero = MakeNearRainInput(0.0f);

	WarmUpNearRainPipeline(Settings, Masks, RainOn);

	TArray<FFloat16Color> RainPixels;
	TArray<FFloat16Color> RainAfterReleasePixels;
	TArray<FFloat16Color> RainFreshPixels;
	TArray<FFloat16Color> NoRainPixels;
	TArray<FFloat16Color> ZeroRainPixels;
	{
		FTDNearRainRun Run(Settings);
		RunNearRainReplayFrames(Run, Masks, &RainOn);
		TestTrue(*(CaseLabel + TEXT(" read the rain output")), Run.ReadPixels(RainPixels));
		Run.Layer.Release();
		RunNearRainReplayFrames(Run, Masks, &RainOn);
		TestTrue(*(CaseLabel + TEXT(" read the rain output after release")), Run.ReadPixels(RainAfterReleasePixels));
	}
	{
		FTDNearRainRun Run(Settings);
		RunNearRainReplayFrames(Run, Masks, &RainOn);
		TestTrue(*(CaseLabel + TEXT(" read the fresh rain output")), Run.ReadPixels(RainFreshPixels));
	}
	{
		FTDNearRainRun Run(Settings);
		RunNearRainReplayFrames(Run, Masks, nullptr);
		TestTrue(*(CaseLabel + TEXT(" read the no rain output")), Run.ReadPixels(NoRainPixels));
	}
	{
		FTDNearRainRun Run(Settings);
		RunNearRainReplayFrames(Run, Masks, &RainZero);
		TestTrue(*(CaseLabel + TEXT(" read the zero rain output")), Run.ReadPixels(ZeroRainPixels));
	}

	int32 RainChangedCellCount = 0;
	for (int32 PixelIndex = 0; PixelIndex < FMath::Min(RainPixels.Num(), NoRainPixels.Num()); ++PixelIndex)
	{
		RainChangedCellCount += RainPixels[PixelIndex].R.Encoded == NoRainPixels[PixelIndex].R.Encoded ? 0 : 1;
	}
	ReportNearRainLine(*this, FString::Printf(TEXT("TDFluidNearRainReplay: rain_active=%d no_rain_active=%d rain_changed=%d"), CountNearRainActiveHeights(RainPixels), CountNearRainActiveHeights(NoRainPixels), RainChangedCellCount));
	ReportNearRainLine(*this, FString::Printf(TEXT("TDFluidNearRainReplayMismatches: fresh=%d after_release=%d zero_vs_no_rain=%d warmup_frames=%d"), CountNearRainPixelMismatches(RainPixels, RainFreshPixels), CountNearRainPixelMismatches(RainPixels, RainAfterReleasePixels), CountNearRainPixelMismatches(NoRainPixels, ZeroRainPixels), NearRainRhiReplayFrameCount));
	TestTrue(*(CaseLabel + TEXT(" rain replays bit identically in a fresh layer")), AreNearRainPixelsIdentical(RainPixels, RainFreshPixels));
	TestTrue(*(CaseLabel + TEXT(" rain replays bit identically after the layer is released")), AreNearRainPixelsIdentical(RainPixels, RainAfterReleasePixels));
	TestTrue(*(CaseLabel + TEXT(" zero rain is bit identical to no rain input")), AreNearRainPixelsIdentical(NoRainPixels, ZeroRainPixels));
	TestTrue(*(CaseLabel + TEXT(" rain changes the surface")), RainChangedCellCount > NearRainRhiMinimumActiveCells);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleRainRhiMatchesCpuReferenceTest,
	"TDGame.FluidFXRhi.Rain.NearRainMatchesCpuReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleRainRhiMatchesCpuReferenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNearRippleRainRhiTestPrivate;
	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Rain.NearRainMatchesCpuReference");
	if (!CanRunNearRainRhiTest(*this, CaseLabel))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> SurfaceTexture(CreateOpenWaterSurfaceTexture());
	FlushRenderingCommands();
	if (!TestNotNull(*(CaseLabel + TEXT(" created the water surface mask")), SurfaceTexture.Get()))
	{
		return false;
	}

	FTDFluidWaterNearRippleSettings Settings = MakeNearRainSettings(NearRainRhiDefaultExtentCm);
	Settings.DampingPerSec = 0.0f;
	const int32 Resolution = FTDFluidNearRippleLayer::SanitizeResolution(Settings.Resolution);
	const float CellSizeCm = Settings.ExtentCm / static_cast<float>(Resolution);
	const FTDFluidNearRippleSubstepPlan Plan = FTDFluidNearRippleLayer::ComputeSubstepPlan(Settings.WaveSpeedCmPerSec, NearRainRhiStepSeconds, CellSizeCm, Settings.MaxSubsteps);
	if (!TestEqual(*(CaseLabel + TEXT(" one frame is one substep")), Plan.SubstepCount, 1))
	{
		return false;
	}

	const FTDFluidNearRippleRainInput RainInput = MakeNearRainInput(1.0e6f);
	const FTDFluidNearRippleRainFrame Frame = FTDFluidNearRippleLayer::MakeRainFrame(RainInput, CellSizeCm, Plan.SubstepDeltaSeconds);
	FTDNearRainRun Run(Settings);
	Run.Layer.SetRainInput(RainInput);
	const FTDFluidNearRippleMaskInputs Masks = MakeNearRainMasks(*SurfaceTexture);
	const FVector2D CenterWorld = FVector2D::ZeroVector;
	Run.Submit(Masks, CenterWorld);
	TArray<FFloat16Color> Pixels;
	if (!TestTrue(*(CaseLabel + TEXT(" read the output")), Run.ReadPixels(Pixels)))
	{
		return false;
	}

	const FIntPoint WindowMinCell = FTDFluidNearRippleLayer::ComputeWindowMinCell(CenterWorld, static_cast<double>(Settings.ExtentCm) / Resolution, Resolution);
	int32 ReferenceDropCellCount = 0;
	int32 MismatchCount = 0;
	double MaximumError = 0.0;
	for (int32 LocalY = 0; LocalY < Resolution; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < Resolution; ++LocalX)
		{
			const FIntPoint WorldCell = WindowMinCell + FIntPoint(LocalX, LocalY);
			const float Reference = -Frame.DropImpulseCm * FTDFluidNearRippleLayer::ComputeRainDropWeight(WorldCell, 0u, Frame);
			const float Actual = Pixels[ToNearRainPixelIndex(WorldCell, Resolution)].R.GetFloat();
			const float Error = FMath::Abs(Actual - Reference);
			ReferenceDropCellCount += Reference != 0.0f ? 1 : 0;
			MismatchCount += Error <= FMath::Max(NearRainRhiAbsoluteToleranceCm, FMath::Abs(Reference) * NearRainRhiRelativeTolerance) ? 0 : 1;
			MaximumError = FMath::Max(MaximumError, static_cast<double>(Error));
		}
	}

	ReportNearRainLine(*this, FString::Printf(TEXT("TDFluidNearRainCpuReference: p=%.4f block=%d radius_cells=%.4f drop_cells=%d mismatches=%d max_error=%.3g"), Frame.DropProbability, Frame.BlockSizeCells, Frame.DropRadiusCells, ReferenceDropCellCount, MismatchCount, MaximumError));
	TestEqual(*(CaseLabel + TEXT(" dense rain uses the clamped probability")), Frame.DropProbability, 0.5f);
	TestTrue(*(CaseLabel + TEXT(" reference has many drop cells")), ReferenceDropCellCount > NearRainRhiMinimumActiveCells);
	TestEqual(*(CaseLabel + TEXT(" GPU heights match the CPU drop weights")), MismatchCount, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleRainRhiShiftInvariantTest,
	"TDGame.FluidFXRhi.Rain.NearRainShiftInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleRainRhiShiftInvariantTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNearRippleRainRhiTestPrivate;
	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Rain.NearRainShiftInvariant");
	if (!CanRunNearRainRhiTest(*this, CaseLabel))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> SurfaceTexture(CreateOpenWaterSurfaceTexture());
	FlushRenderingCommands();
	if (!TestNotNull(*(CaseLabel + TEXT(" created the water surface mask")), SurfaceTexture.Get()))
	{
		return false;
	}

	const FTDFluidWaterNearRippleSettings Settings = MakeNearRainSettings(NearRainRhiExactCellExtentCm);
	const FTDFluidNearRippleMaskInputs Masks = MakeNearRainMasks(*SurfaceTexture);
	const int32 Resolution = FTDFluidNearRippleLayer::SanitizeResolution(Settings.Resolution);
	const double CellSizeCm = static_cast<double>(Settings.ExtentCm) / Resolution;
	const FTDFluidNearRippleSubstepPlan Plan = FTDFluidNearRippleLayer::ComputeSubstepPlan(Settings.WaveSpeedCmPerSec, NearRainRhiStepSeconds, static_cast<float>(CellSizeCm), Settings.MaxSubsteps);
	const FVector2D FixedCenter = FVector2D::ZeroVector;
	const FVector2D ShiftedCenter(NearRainRhiShiftCellCount * CellSizeCm, 0.0);
	const FTDFluidNearRippleRainInput RainInput = MakeNearRainInput(2000.0f);
	FTDNearRainRun FixedRun(Settings);
	FTDNearRainRun ShiftedRun(Settings);
	FixedRun.Layer.SetRainInput(RainInput);
	ShiftedRun.Layer.SetRainInput(RainInput);
	for (int32 FrameIndex = 0; FrameIndex < NearRainRhiShiftFramesBefore + NearRainRhiShiftFramesAfter; ++FrameIndex)
	{
		FixedRun.Submit(Masks, FixedCenter);
		ShiftedRun.Submit(Masks, FrameIndex < NearRainRhiShiftFramesBefore ? FixedCenter : ShiftedCenter);
	}

	TArray<FFloat16Color> FixedPixels;
	TArray<FFloat16Color> ShiftedPixels;
	if (!TestTrue(*(CaseLabel + TEXT(" read both outputs")), FixedRun.ReadPixels(FixedPixels) && ShiftedRun.ReadPixels(ShiftedPixels)))
	{
		return false;
	}

	const FIntPoint FixedMinCell = FTDFluidNearRippleLayer::ComputeWindowMinCell(FixedCenter, CellSizeCm, Resolution);
	const FIntPoint ShiftedMinCell = FTDFluidNearRippleLayer::ComputeWindowMinCell(ShiftedCenter, CellSizeCm, Resolution);
	TestEqual(*(CaseLabel + TEXT(" window moved by the requested cells")), ShiftedMinCell.X - FixedMinCell.X, NearRainRhiShiftCellCount);
	TestEqual(*(CaseLabel + TEXT(" one substep per frame keeps the dependence cone one cell per frame")), Plan.SubstepCount, 1);
	const int32 EdgeBandCells = FMath::CeilToInt32(FMath::Max(Settings.EdgeFadeRatio * Resolution, 1.0f) - 0.5f);
	const int32 DependenceCells = NearRainRhiShiftFramesAfter * Plan.SubstepCount + NearRainRhiDependenceMarginCells;
	const int32 CompareMinX = ShiftedMinCell.X + EdgeBandCells + DependenceCells;
	const int32 CompareMaxX = FixedMinCell.X + Resolution - EdgeBandCells - DependenceCells;
	int32 ComparedCellCount = 0;
	int32 MismatchCount = 0;
	int32 ActiveCellCount = 0;
	for (int32 WorldY = FixedMinCell.Y; WorldY < FixedMinCell.Y + Resolution; ++WorldY)
	{
		for (int32 WorldX = CompareMinX; WorldX < CompareMaxX; ++WorldX)
		{
			const int32 PixelIndex = ToNearRainPixelIndex(FIntPoint(WorldX, WorldY), Resolution);
			++ComparedCellCount;
			MismatchCount += FixedPixels[PixelIndex].R.Encoded == ShiftedPixels[PixelIndex].R.Encoded ? 0 : 1;
			ActiveCellCount += FMath::Abs(FixedPixels[PixelIndex].R.GetFloat()) > NearRainRhiActiveHeightCm ? 1 : 0;
		}
	}

	ReportNearRainLine(*this, FString::Printf(TEXT("TDFluidNearRainShift: compare_x=[%d,%d) edge_band=%d dependence=%d compared=%d mismatches=%d active=%d"), CompareMinX, CompareMaxX, EdgeBandCells, DependenceCells, ComparedCellCount, MismatchCount, ActiveCellCount));
	TestTrue(*(CaseLabel + TEXT(" interior comparison region is not empty")), ComparedCellCount > 0);
	TestEqual(*(CaseLabel + TEXT(" interior world cells keep bit identical rain heights after the shift")), MismatchCount, 0);
	TestTrue(*(CaseLabel + TEXT(" interior comparison region carries rain ripples")), ActiveCellCount > NearRainRhiMinimumActiveCells);
	return !HasAnyErrors();
}

#endif
