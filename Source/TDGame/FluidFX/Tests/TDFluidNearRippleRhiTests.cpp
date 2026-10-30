#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FluidFX/TDFluidNearRippleLayer.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/FileManager.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TDFluidGpuNearRipple.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidNearRippleRhiTests, Log, All);

namespace TDFluidNearRippleRhiTestPrivate
{
	constexpr int32 NearRippleTestResolution = 256;
	constexpr float NearRippleTestStepSeconds = 1.0f / 60.0f;
	constexpr float NearRippleDefaultExtentCm = 600.0f;
	constexpr float NearRippleExactCellExtentCm = 512.0f;
	constexpr double OpenWaterMainExtentCm = 100000.0;
	constexpr float NearRippleDepthReferenceCm = 300.0f;
	constexpr float NearRippleObstacleRangeCm = 400.0f;
	constexpr int32 OpenWaterMaskSize = 4;
	constexpr int32 ReplayFrameCount = 150;
	constexpr int32 ReplayShiftStartFrame = 75;
	constexpr int32 ShiftCellCount = 16;
	constexpr int32 ShiftFramesBefore = 60;
	constexpr int32 ShiftFramesAfter = 30;
	constexpr int32 ShiftDependenceMarginCells = 2;
	constexpr int32 ShiftMinimumActiveCells = 50;
	constexpr int32 MaskSize = 64;
	constexpr double MaskMainExtentCm = 1200.0;
	constexpr double MaskDryBelowYCm = -150.0;
	constexpr double MaskBlendMarginCm = 28.0;
	constexpr double MaskBoundaryBandCm = 35.0;
	constexpr double MaskDiskRadiusCm = 90.0;
	constexpr int32 MaskFramesBeforeShift = 120;
	constexpr int32 CostWarmupFrameCount = 120;
	constexpr int32 CostMeasuredFrameCount = 240;
	constexpr int32 CostMovingSourceCount = 16;
	constexpr int32 CostIdleContactCount = 4;
	constexpr double CostTargetMilliseconds = 0.1;
	constexpr double CostReportLimitMilliseconds = 0.2;
	constexpr float ActiveHeightThresholdCm = 1.0e-5f;
	const FVector2D MaskDiskCenter(100.0, 60.0);

	bool CanRunNearRippleRhiTest(FAutomationTestBase& Test, const FString& CaseLabel)
	{
		const bool bHasActiveRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidNearRippleRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), *CaseLabel, bHasActiveRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasActiveRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
		return false;
	}

	void ReportNearRippleLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidNearRippleRhiTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	UTextureRenderTarget2D* CreateNearRippleOutputTarget(int32 Resolution)
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

	UTexture2D* CreateMaskTexture(int32 Size, TFunctionRef<FLinearColor(int32, int32)> PixelAt)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_FloatRGBA);
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}

		Texture->SRGB = false;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FFloat16Color* Pixels = static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 PixelY = 0; PixelY < Size; ++PixelY)
		{
			for (int32 PixelX = 0; PixelX < Size; ++PixelX)
			{
				Pixels[PixelY * Size + PixelX] = FFloat16Color(PixelAt(PixelX, PixelY));
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UTexture2D* CreateUniformMaskTexture(const FLinearColor& Color)
	{
		return CreateMaskTexture(OpenWaterMaskSize, [&Color](int32, int32) { return Color; });
	}

	FTDFluidWaterNearRippleSettings MakeNearRippleTestSettings(float ExtentCm)
	{
		FTDFluidWaterNearRippleSettings Settings;
		Settings.bEnabled = true;
		Settings.Resolution = NearRippleTestResolution;
		Settings.ExtentCm = ExtentCm;
		return Settings;
	}

	FTDFluidNearRippleMaskInputs MakeOpenWaterMasks(UTexture& SurfaceTexture)
	{
		FTDFluidNearRippleMaskInputs Masks;
		Masks.WaterSurfaceTexture = &SurfaceTexture;
		Masks.MainWindowCenterCm = FVector2D::ZeroVector;
		Masks.MainWindowExtentCm = FVector2D(OpenWaterMainExtentCm, OpenWaterMainExtentCm);
		Masks.WaterDepthReferenceCm = NearRippleDepthReferenceCm;
		Masks.ObstacleDistanceOutputRangeCm = NearRippleObstacleRangeCm;
		return Masks;
	}

	struct FTDNearRippleRun
	{
		explicit FTDNearRippleRun(const FTDFluidWaterNearRippleSettings& Settings)
			: Output(CreateNearRippleOutputTarget(FTDFluidNearRippleLayer::SanitizeResolution(Settings.Resolution)))
		{
			Layer.Configure(Settings);
		}

		FTDNearRippleRun(const FTDNearRippleRun&) = delete;
		FTDNearRippleRun& operator=(const FTDNearRippleRun&) = delete;

		void Submit(const FTDFluidNearRippleMaskInputs& Masks, const FVector2D& CenterWorldXY)
		{
			Layer.SubmitStep(Masks, CenterWorldXY, NearRippleTestStepSeconds, *Output);
		}

		bool ReadPixels(TArray<FFloat16Color>& OutPixels)
		{
			ENQUEUE_RENDER_COMMAND(TDFluidNearRippleRhiWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
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

	int32 ToStoragePixelIndex(const FIntPoint& WorldCell, int32 Resolution)
	{
		const FIntPoint StorageCell = FTDFluidNearRippleLayer::ComputeStorageCell(WorldCell, Resolution);
		return StorageCell.Y * Resolution + StorageCell.X;
	}

	int32 CountActiveHeights(const TArray<FFloat16Color>& Pixels)
	{
		int32 ActiveCount = 0;
		for (const FFloat16Color& Pixel : Pixels)
		{
			ActiveCount += FMath::Abs(Pixel.R.GetFloat()) > ActiveHeightThresholdCm ? 1 : 0;
		}
		return ActiveCount;
	}

	void AddReplaySources(FTDFluidNearRippleLayer& Layer, int32 FrameIndex)
	{
		const double Angle = 0.08 * FrameIndex;
		const double AngularSpeed = 0.08 / NearRippleTestStepSeconds;
		Layer.AddMovingSource(FVector2D(60.0 * FMath::Cos(Angle), 60.0 * FMath::Sin(Angle)), FVector2D(-60.0 * AngularSpeed * FMath::Sin(Angle), 60.0 * AngularSpeed * FMath::Cos(Angle)), 40.0f);
		Layer.AddMovingSource(FVector2D(-90.0 + 2.0 * FrameIndex, 40.0), FVector2D(2.0 / NearRippleTestStepSeconds, 0.0), 30.0f);
		Layer.AddIdleContact(FVector2D(70.0, -80.0));
	}

	FVector2D ResolveReplayCenter(int32 FrameIndex)
	{
		return FVector2D(2.0 * FMath::Max(FrameIndex - ReplayShiftStartFrame, 0), 0.0);
	}

	void AddShiftSources(FTDFluidNearRippleLayer& Layer, int32 FrameIndex)
	{
		Layer.AddMovingSource(FVector2D(-20.0 + FrameIndex, 6.0), FVector2D(1.0 / NearRippleTestStepSeconds, 0.0), 40.0f);
		Layer.AddIdleContact(FVector2D(30.0, -40.0));
	}

	struct FTDNearRippleMaskClass
	{
		bool bIsClearlyMasked = false;
		bool bIsClearlyWater = false;
		bool bIsNearBoundary = false;
	};

	FTDNearRippleMaskClass ClassifyMaskCell(const FVector2D& WorldPoint)
	{
		const double DiskDistanceCm = FVector2D::Distance(WorldPoint, MaskDiskCenter);
		FTDNearRippleMaskClass Class;
		Class.bIsClearlyMasked = WorldPoint.Y < MaskDryBelowYCm - MaskBlendMarginCm || DiskDistanceCm < MaskDiskRadiusCm - MaskBlendMarginCm;
		Class.bIsClearlyWater = WorldPoint.Y > MaskDryBelowYCm + MaskBlendMarginCm && DiskDistanceCm > MaskDiskRadiusCm + MaskBlendMarginCm;
		Class.bIsNearBoundary = Class.bIsClearlyWater && (WorldPoint.Y < MaskDryBelowYCm + MaskBlendMarginCm + MaskBoundaryBandCm || DiskDistanceCm < MaskDiskRadiusCm + MaskBlendMarginCm + MaskBoundaryBandCm);
		return Class;
	}

	FVector2D ComputeMaskTexelWorldCenter(int32 PixelX, int32 PixelY)
	{
		const double TexelSizeCm = MaskMainExtentCm / MaskSize;
		return FVector2D(-0.5 * MaskMainExtentCm + (PixelX + 0.5) * TexelSizeCm, -0.5 * MaskMainExtentCm + (PixelY + 0.5) * TexelSizeCm);
	}

	struct FTDNearRippleCostCase
	{
		float StepSeconds = 0.0f;
		FTDFluidNearRippleSubstepPlan Plan;
		TArray<double> Milliseconds;
		double MedianMilliseconds = 0.0;
		double MeanMilliseconds = 0.0;
		double P95Milliseconds = 0.0;
	};

	void SummarizeCostCase(FTDNearRippleCostCase& CostCase)
	{
		if (CostCase.Milliseconds.IsEmpty())
		{
			return;
		}

		TArray<double> Sorted = CostCase.Milliseconds;
		Sorted.Sort();
		double Sum = 0.0;
		for (const double Value : Sorted)
		{
			Sum += Value;
		}
		CostCase.MedianMilliseconds = Sorted[Sorted.Num() / 2];
		CostCase.MeanMilliseconds = Sum / Sorted.Num();
		CostCase.P95Milliseconds = Sorted[FMath::Clamp(FMath::CeilToInt(0.95 * Sorted.Num()) - 1, 0, Sorted.Num() - 1)];
	}

	FTDFluidGpuNearRippleFrameInput MakeCostFrameInput(int32 FrameIndex, const FTDNearRippleCostCase& CostCase, FTextureResource* SurfaceResource, FTextureResource* AuxResource, FTextureRenderTargetResource* OutputResource)
	{
		const FTDFluidWaterNearRippleSettings Settings = MakeNearRippleTestSettings(NearRippleDefaultExtentCm);
		const float CellSizeCm = NearRippleDefaultExtentCm / NearRippleTestResolution;
		const float WindowSizeCm = NearRippleDefaultExtentCm;
		FTDFluidGpuNearRippleFrameInput FrameInput;
		FrameInput.Resolution = NearRippleTestResolution;
		FrameInput.CellSizeCm = CellSizeCm;
		FrameInput.WindowMinCell = FIntPoint(-NearRippleTestResolution / 2, -NearRippleTestResolution / 2);
		FrameInput.PreviousWindowMinCell = FrameInput.WindowMinCell;
		FrameInput.bResetsState = FrameIndex == 0;
		FrameInput.SubstepCount = CostCase.Plan.SubstepCount;
		FrameInput.SubstepDeltaSeconds = CostCase.Plan.SubstepDeltaSeconds;
		FrameInput.WaveSpeedCmPerSec = Settings.WaveSpeedCmPerSec;
		FrameInput.DampingPerSec = Settings.DampingPerSec;
		FrameInput.EdgeFadeRatio = Settings.EdgeFadeRatio;
		FrameInput.EdgeAbsorptionStrength = Settings.EdgeAbsorptionStrength;
		FrameInput.IdleContactAmplitudeCm = Settings.IdleContactAmplitudeCm;
		FrameInput.IdleContactStartPhase = FTDFluidNearRippleLayer::ComputeIdleContactPhase(static_cast<double>(FrameIndex) * CostCase.StepSeconds, Settings.IdleContactHz);
		FrameInput.IdleContactPhasePerSubstep = UE_DOUBLE_TWO_PI * Settings.IdleContactHz * CostCase.Plan.SubstepDeltaSeconds;
		FrameInput.MinWaterDepthCm = Settings.MinWaterDepthCm;
		FrameInput.MainWindowCenterFromOriginCm = FVector2f(0.5f * WindowSizeCm, 0.5f * WindowSizeCm);
		FrameInput.MainWindowExtentCm = FVector2f(2.0f * WindowSizeCm, 2.0f * WindowSizeCm);
		FrameInput.MainWaterDepthReferenceCm = NearRippleDepthReferenceCm;
		FrameInput.ObstacleDistanceOutputRangeCm = NearRippleObstacleRangeCm;
		FrameInput.bHasObstacleDistanceAux = true;
		FrameInput.WaterSurfaceTexture = SurfaceResource;
		FrameInput.AuxTexture = AuxResource;
		FrameInput.OutputTarget = OutputResource;
		for (int32 SourceIndex = 0; SourceIndex < CostMovingSourceCount; ++SourceIndex)
		{
			const double Angle = UE_DOUBLE_TWO_PI * SourceIndex / CostMovingSourceCount + 0.03 * FrameIndex;
			FTDFluidGpuNearRippleSource& Source = FrameInput.Sources.AddDefaulted_GetRef();
			Source.PositionCm = FVector2f(0.5f * WindowSizeCm + 150.0f * static_cast<float>(FMath::Cos(Angle)), 0.5f * WindowSizeCm + 150.0f * static_cast<float>(FMath::Sin(Angle)));
			Source.RadiusCm = 10.0f;
			Source.ImpulseCmPerSec = Settings.MovingImpulseCmPerSec;
		}
		for (int32 ContactIndex = 0; ContactIndex < CostIdleContactCount; ++ContactIndex)
		{
			FTDFluidGpuNearRippleSource& Source = FrameInput.Sources.AddDefaulted_GetRef();
			Source.PositionCm = FVector2f(0.5f * WindowSizeCm + ((ContactIndex & 1) ? 60.0f : -60.0f), 0.5f * WindowSizeCm + ((ContactIndex & 2) ? 60.0f : -60.0f));
			Source.RadiusCm = Settings.IdleContactRadiusCm;
			Source.IdleWeight = 1.0f;
		}
		return FrameInput;
	}

	TArray<double> MeasureCostFrameMilliseconds(const FTDNearRippleCostCase& CostCase, FTextureResource* SurfaceResource, FTextureResource* AuxResource, FTextureRenderTargetResource* OutputResource)
	{
		TSharedRef<TArray<double>, ESPMode::ThreadSafe> Milliseconds = MakeShared<TArray<double>, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidNearRippleCostFrames)(
			[Milliseconds, CostCase, SurfaceResource, AuxResource, OutputResource](FRHICommandListImmediate& RHICmdList)
			{
				FTDFluidGpuNearRippleState State;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FRenderQueryRHIRef> EndQueries;
				for (int32 FrameIndex = 0; FrameIndex < CostWarmupFrameCount + CostMeasuredFrameCount; ++FrameIndex)
				{
					const FTDFluidGpuNearRippleFrameInput FrameInput = MakeCostFrameInput(FrameIndex, CostCase, SurfaceResource, AuxResource, OutputResource);
					const bool bIsMeasured = FrameIndex >= CostWarmupFrameCount;
					if (bIsMeasured)
					{
						RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					}
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidNearRippleCostFrame"));
						AddTDFluidGpuNearRipplePasses(GraphBuilder, State, FrameInput);
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
				State.Release();
			});
		FlushRenderingCommands();
		return *Milliseconds;
	}

	FString GetNearRippleCostPath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("w7"), TEXT("near_ripple_cost.json"));
	}

	bool WriteNearRippleCostJson(const FString& CostPath, const TArray<FTDNearRippleCostCase>& CostCases)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("test"), TEXT("TDGame.FluidFXRhi.NearRipple.OffscreenCost"));
		Root->SetStringField(TEXT("recorded_at"), FDateTime::Now().ToIso8601());
		Root->SetStringField(TEXT("rhi_adapter"), GRHIAdapterName);
		Root->SetStringField(TEXT("rhi_driver"), GRHIAdapterUserDriverVersion);
		Root->SetStringField(TEXT("timing"), TEXT("RQT_AbsoluteTime around one near ripple graph per frame"));
		Root->SetStringField(TEXT("verdict"), TEXT("record only (D-64)"));
		Root->SetNumberField(TEXT("resolution"), NearRippleTestResolution);
		Root->SetNumberField(TEXT("extent_cm"), NearRippleDefaultExtentCm);
		Root->SetNumberField(TEXT("moving_sources"), CostMovingSourceCount);
		Root->SetNumberField(TEXT("idle_contacts"), CostIdleContactCount);
		Root->SetNumberField(TEXT("warmup_frames"), CostWarmupFrameCount);
		Root->SetNumberField(TEXT("measured_frames"), CostMeasuredFrameCount);
		Root->SetNumberField(TEXT("target_ms"), CostTargetMilliseconds);
		Root->SetNumberField(TEXT("stop_report_ms"), CostReportLimitMilliseconds);
		TArray<TSharedPtr<FJsonValue>> CaseValues;
		for (const FTDNearRippleCostCase& CostCase : CostCases)
		{
			TSharedRef<FJsonObject> CaseObject = MakeShared<FJsonObject>();
			CaseObject->SetNumberField(TEXT("step_seconds"), CostCase.StepSeconds);
			CaseObject->SetNumberField(TEXT("substeps"), CostCase.Plan.SubstepCount);
			CaseObject->SetNumberField(TEXT("substep_seconds"), CostCase.Plan.SubstepDeltaSeconds);
			CaseObject->SetNumberField(TEXT("samples"), CostCase.Milliseconds.Num());
			CaseObject->SetNumberField(TEXT("median_ms"), CostCase.MedianMilliseconds);
			CaseObject->SetNumberField(TEXT("mean_ms"), CostCase.MeanMilliseconds);
			CaseObject->SetNumberField(TEXT("p95_ms"), CostCase.P95Milliseconds);
			CaseObject->SetBoolField(TEXT("within_target"), CostCase.MedianMilliseconds <= CostTargetMilliseconds);
			CaseObject->SetBoolField(TEXT("above_stop_report"), CostCase.MedianMilliseconds > CostReportLimitMilliseconds);
			CaseValues.Add(MakeShared<FJsonValueObject>(CaseObject));
		}
		Root->SetArrayField(TEXT("cases"), CaseValues);

		FString JsonText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
		if (!FJsonSerializer::Serialize(Root, Writer))
		{
			return false;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(CostPath), true);
		return FFileHelper::SaveStringToFile(JsonText, *CostPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleRhiDeterministicReplayTest,
	"TDGame.FluidFXRhi.NearRipple.DeterministicReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleRhiDeterministicReplayTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNearRippleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.NearRipple.DeterministicReplay");
	if (!CanRunNearRippleRhiTest(*this, CaseLabel))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> SurfaceTexture(CreateUniformMaskTexture(FLinearColor(1.0f, 0.0f, 0.0f, 0.0f)));
	FlushRenderingCommands();
	if (!TestNotNull(*(CaseLabel + TEXT(" created the water surface mask")), SurfaceTexture.Get()))
	{
		return false;
	}

	const FTDFluidWaterNearRippleSettings Settings = MakeNearRippleTestSettings(NearRippleDefaultExtentCm);
	const FTDFluidNearRippleMaskInputs Masks = MakeOpenWaterMasks(*SurfaceTexture);
	TArray<FFloat16Color> CheckpointPixels[2];
	TArray<FFloat16Color> FinalPixels[2];
	for (int32 RunIndex = 0; RunIndex < 2; ++RunIndex)
	{
		FTDNearRippleRun Run(Settings);
		for (int32 FrameIndex = 0; FrameIndex < ReplayFrameCount; ++FrameIndex)
		{
			AddReplaySources(Run.Layer, FrameIndex);
			Run.Submit(Masks, ResolveReplayCenter(FrameIndex));
			if (FrameIndex == ReplayShiftStartFrame)
			{
				TestTrue(*FString::Printf(TEXT("%s run %d read the checkpoint output"), *CaseLabel, RunIndex), Run.ReadPixels(CheckpointPixels[RunIndex]));
			}
		}
		TestTrue(*FString::Printf(TEXT("%s run %d read the final output"), *CaseLabel, RunIndex), Run.ReadPixels(FinalPixels[RunIndex]));
	}

	const uint32 CheckpointChecksums[2] = { FCrc::MemCrc32(CheckpointPixels[0].GetData(), CheckpointPixels[0].Num() * sizeof(FFloat16Color)), FCrc::MemCrc32(CheckpointPixels[1].GetData(), CheckpointPixels[1].Num() * sizeof(FFloat16Color)) };
	const uint32 FinalChecksums[2] = { FCrc::MemCrc32(FinalPixels[0].GetData(), FinalPixels[0].Num() * sizeof(FFloat16Color)), FCrc::MemCrc32(FinalPixels[1].GetData(), FinalPixels[1].Num() * sizeof(FFloat16Color)) };
	const int32 ActiveCellCount = CountActiveHeights(FinalPixels[0]);
	ReportNearRippleLine(*this, FString::Printf(TEXT("TDFluidNearRippleReplay: checkpoint_crc=%08x/%08x final_crc=%08x/%08x active_cells=%d"), CheckpointChecksums[0], CheckpointChecksums[1], FinalChecksums[0], FinalChecksums[1], ActiveCellCount));
	TestEqual(*(CaseLabel + TEXT(" checkpoint output CRC matches across runs")), CheckpointChecksums[1], CheckpointChecksums[0]);
	TestEqual(*(CaseLabel + TEXT(" final output CRC matches across runs")), FinalChecksums[1], FinalChecksums[0]);
	TestTrue(*(CaseLabel + TEXT(" final outputs are bit identical")), FinalPixels[0].Num() == FinalPixels[1].Num() && FMemory::Memcmp(FinalPixels[0].GetData(), FinalPixels[1].GetData(), FinalPixels[0].Num() * sizeof(FFloat16Color)) == 0);
	TestTrue(*(CaseLabel + TEXT(" sources produced ripples")), ActiveCellCount > ShiftMinimumActiveCells);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleRhiWindowShiftTorusInvariantTest,
	"TDGame.FluidFXRhi.NearRipple.WindowShiftTorusInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleRhiWindowShiftTorusInvariantTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNearRippleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.NearRipple.WindowShiftTorusInvariant");
	if (!CanRunNearRippleRhiTest(*this, CaseLabel))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> SurfaceTexture(CreateUniformMaskTexture(FLinearColor(1.0f, 0.0f, 0.0f, 0.0f)));
	FlushRenderingCommands();
	if (!TestNotNull(*(CaseLabel + TEXT(" created the water surface mask")), SurfaceTexture.Get()))
	{
		return false;
	}

	const FTDFluidWaterNearRippleSettings Settings = MakeNearRippleTestSettings(NearRippleExactCellExtentCm);
	const FTDFluidNearRippleMaskInputs Masks = MakeOpenWaterMasks(*SurfaceTexture);
	const int32 Resolution = FTDFluidNearRippleLayer::SanitizeResolution(Settings.Resolution);
	const double CellSizeCm = static_cast<double>(Settings.ExtentCm) / Resolution;
	const FTDFluidNearRippleSubstepPlan Plan = FTDFluidNearRippleLayer::ComputeSubstepPlan(Settings.WaveSpeedCmPerSec, NearRippleTestStepSeconds, static_cast<float>(CellSizeCm), Settings.MaxSubsteps);
	const FVector2D FixedCenter = FVector2D::ZeroVector;
	const FVector2D ShiftedCenter(ShiftCellCount * CellSizeCm, 0.0);

	FTDNearRippleRun FixedRun(Settings);
	FTDNearRippleRun ShiftedRun(Settings);
	for (int32 FrameIndex = 0; FrameIndex < ShiftFramesBefore + ShiftFramesAfter; ++FrameIndex)
	{
		AddShiftSources(FixedRun.Layer, FrameIndex);
		AddShiftSources(ShiftedRun.Layer, FrameIndex);
		FixedRun.Submit(Masks, FixedCenter);
		ShiftedRun.Submit(Masks, FrameIndex < ShiftFramesBefore ? FixedCenter : ShiftedCenter);
	}

	TArray<FFloat16Color> FixedPixels;
	TArray<FFloat16Color> ShiftedPixels;
	if (!TestTrue(*(CaseLabel + TEXT(" read both outputs")), FixedRun.ReadPixels(FixedPixels) && ShiftedRun.ReadPixels(ShiftedPixels)))
	{
		return false;
	}

	const FIntPoint FixedMinCell = FTDFluidNearRippleLayer::ComputeWindowMinCell(FixedCenter, CellSizeCm, Resolution);
	const FIntPoint ShiftedMinCell = FTDFluidNearRippleLayer::ComputeWindowMinCell(ShiftedCenter, CellSizeCm, Resolution);
	TestEqual(*(CaseLabel + TEXT(" window moved by the requested cells")), ShiftedMinCell.X - FixedMinCell.X, ShiftCellCount);
	TestEqual(*(CaseLabel + TEXT(" one substep per frame keeps the dependence cone one cell per frame")), Plan.SubstepCount, 1);

	const int32 EdgeBandCells = FMath::CeilToInt32(FMath::Max(Settings.EdgeFadeRatio * Resolution, 1.0f) - 0.5f);
	const int32 DependenceCells = ShiftFramesAfter * Plan.SubstepCount + ShiftDependenceMarginCells;
	const int32 CompareMinX = ShiftedMinCell.X + EdgeBandCells + DependenceCells;
	const int32 CompareMaxX = FixedMinCell.X + Resolution - EdgeBandCells - DependenceCells;
	int32 ComparedCellCount = 0;
	int32 MismatchCount = 0;
	int32 ActiveCellCount = 0;
	int32 OutsideMismatchCount = 0;
	for (int32 WorldY = FixedMinCell.Y; WorldY < FixedMinCell.Y + Resolution; ++WorldY)
	{
		for (int32 WorldX = ShiftedMinCell.X; WorldX < FixedMinCell.X + Resolution; ++WorldX)
		{
			const FIntPoint WorldCell(WorldX, WorldY);
			const int32 PixelIndex = ToStoragePixelIndex(WorldCell, Resolution);
			const bool bIsSameHeight = FixedPixels[PixelIndex].R.Encoded == ShiftedPixels[PixelIndex].R.Encoded;
			if (WorldX < CompareMinX || WorldX >= CompareMaxX)
			{
				OutsideMismatchCount += bIsSameHeight ? 0 : 1;
				continue;
			}

			++ComparedCellCount;
			MismatchCount += bIsSameHeight ? 0 : 1;
			ActiveCellCount += FMath::Abs(FixedPixels[PixelIndex].R.GetFloat()) > ActiveHeightThresholdCm ? 1 : 0;
		}
	}

	ReportNearRippleLine(*this, FString::Printf(TEXT("TDFluidNearRippleShift: compare_x=[%d,%d) edge_band=%d dependence=%d compared=%d mismatches=%d active=%d outside_mismatches=%d"), CompareMinX, CompareMaxX, EdgeBandCells, DependenceCells, ComparedCellCount, MismatchCount, ActiveCellCount, OutsideMismatchCount));
	TestTrue(*(CaseLabel + TEXT(" interior comparison region is not empty")), ComparedCellCount > 0);
	TestEqual(*(CaseLabel + TEXT(" interior world cells keep bit identical heights after the shift")), MismatchCount, 0);
	TestTrue(*(CaseLabel + TEXT(" interior comparison region carries ripples")), ActiveCellCount > ShiftMinimumActiveCells);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleRhiMaskedCellsStayFlatTest,
	"TDGame.FluidFXRhi.NearRipple.MaskedCellsStayFlat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleRhiMaskedCellsStayFlatTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNearRippleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.NearRipple.MaskedCellsStayFlat");
	if (!CanRunNearRippleRhiTest(*this, CaseLabel))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> SurfaceTexture(CreateMaskTexture(MaskSize, [](int32 PixelX, int32 PixelY)
	{
		return FLinearColor(ComputeMaskTexelWorldCenter(PixelX, PixelY).Y < MaskDryBelowYCm ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f);
	}));
	TStrongObjectPtr<UTexture2D> AuxTexture(CreateMaskTexture(MaskSize, [](int32 PixelX, int32 PixelY)
	{
		return FLinearColor(0.0f, FVector2D::Distance(ComputeMaskTexelWorldCenter(PixelX, PixelY), MaskDiskCenter) < MaskDiskRadiusCm ? -1.0f : 1.0f, 0.0f, 0.0f);
	}));
	FlushRenderingCommands();
	if (!TestTrue(*(CaseLabel + TEXT(" created the synthetic masks")), SurfaceTexture.IsValid() && AuxTexture.IsValid()))
	{
		return false;
	}

	FTDFluidNearRippleMaskInputs Masks;
	Masks.WaterSurfaceTexture = SurfaceTexture.Get();
	Masks.AuxTexture = AuxTexture.Get();
	Masks.MainWindowCenterCm = FVector2D::ZeroVector;
	Masks.MainWindowExtentCm = FVector2D(MaskMainExtentCm, MaskMainExtentCm);
	Masks.WaterDepthReferenceCm = NearRippleDepthReferenceCm;
	Masks.ObstacleDistanceOutputRangeCm = NearRippleObstacleRangeCm;
	Masks.bHasObstacleDistanceAux = true;

	const FTDFluidWaterNearRippleSettings Settings = MakeNearRippleTestSettings(NearRippleDefaultExtentCm);
	const int32 Resolution = FTDFluidNearRippleLayer::SanitizeResolution(Settings.Resolution);
	const double CellSizeCm = static_cast<double>(Settings.ExtentCm) / Resolution;
	const FIntPoint WindowMinCell = FTDFluidNearRippleLayer::ComputeWindowMinCell(FVector2D::ZeroVector, CellSizeCm, Resolution);
	const FVector2D EdgeSourceWorld((WindowMinCell.X + 8.5) * CellSizeCm, 150.0);
	FTDNearRippleRun Run(Settings);
	for (int32 FrameIndex = 0; FrameIndex < MaskFramesBeforeShift; ++FrameIndex)
	{
		const double Angle = 0.06 * FrameIndex;
		Run.Layer.AddMovingSource(FVector2D(-200.0 + 4.0 * FrameIndex, MaskDryBelowYCm + 30.0), FVector2D(4.0 / NearRippleTestStepSeconds, 0.0), 40.0f);
		Run.Layer.AddMovingSource(MaskDiskCenter + FVector2D(115.0 * FMath::Cos(Angle), 115.0 * FMath::Sin(Angle)), FVector2D(-115.0 * 0.06 / NearRippleTestStepSeconds * FMath::Sin(Angle), 115.0 * 0.06 / NearRippleTestStepSeconds * FMath::Cos(Angle)), 40.0f);
		Run.Layer.AddMovingSource(EdgeSourceWorld, FVector2D(60.0, 0.0), 40.0f);
		Run.Layer.AddIdleContact(MaskDiskCenter + FVector2D(MaskDiskRadiusCm + 5.0, 0.0));
		Run.Submit(Masks, FVector2D::ZeroVector);
	}

	TArray<FFloat16Color> Pixels;
	if (!TestTrue(*(CaseLabel + TEXT(" read the masked output")), Run.ReadPixels(Pixels)))
	{
		return false;
	}

	int32 MaskedCellCount = 0;
	int32 MaskedNonFlatCount = 0;
	int32 WaterCellCount = 0;
	int32 WaterWithoutFadeCount = 0;
	int32 ActiveBoundaryCount = 0;
	for (int32 WorldY = WindowMinCell.Y; WorldY < WindowMinCell.Y + Resolution; ++WorldY)
	{
		for (int32 WorldX = WindowMinCell.X; WorldX < WindowMinCell.X + Resolution; ++WorldX)
		{
			const FTDNearRippleMaskClass Class = ClassifyMaskCell(FVector2D((WorldX + 0.5) * CellSizeCm, (WorldY + 0.5) * CellSizeCm));
			const FFloat16Color& Pixel = Pixels[ToStoragePixelIndex(FIntPoint(WorldX, WorldY), Resolution)];
			if (Class.bIsClearlyMasked)
			{
				++MaskedCellCount;
				MaskedNonFlatCount += Pixel.R.GetFloat() == 0.0f && Pixel.A.GetFloat() == 0.0f ? 0 : 1;
				continue;
			}
			if (!Class.bIsClearlyWater)
			{
				continue;
			}

			++WaterCellCount;
			WaterWithoutFadeCount += Pixel.A.GetFloat() > 0.0f ? 0 : 1;
			ActiveBoundaryCount += Class.bIsNearBoundary && FMath::Abs(Pixel.R.GetFloat()) > ActiveHeightThresholdCm ? 1 : 0;
		}
	}

	int32 EdgeActiveCount = 0;
	for (int32 WorldY = WindowMinCell.Y; WorldY < WindowMinCell.Y + Resolution; ++WorldY)
	{
		for (int32 OffsetX = 1; OffsetX < ShiftCellCount; ++OffsetX)
		{
			EdgeActiveCount += FMath::Abs(Pixels[ToStoragePixelIndex(FIntPoint(WindowMinCell.X + OffsetX, WorldY), Resolution)].R.GetFloat()) > ActiveHeightThresholdCm ? 1 : 0;
		}
	}

	Run.Submit(Masks, FVector2D(ShiftCellCount * CellSizeCm, 0.0));
	TArray<FFloat16Color> ShiftedPixels;
	if (!TestTrue(*(CaseLabel + TEXT(" read the shifted output")), Run.ReadPixels(ShiftedPixels)))
	{
		return false;
	}

	int32 IncomingCellCount = 0;
	int32 IncomingNonFlatCount = 0;
	for (int32 WorldY = WindowMinCell.Y; WorldY < WindowMinCell.Y + Resolution; ++WorldY)
	{
		for (int32 OffsetX = 1; OffsetX < ShiftCellCount; ++OffsetX)
		{
			++IncomingCellCount;
			IncomingNonFlatCount += ShiftedPixels[ToStoragePixelIndex(FIntPoint(WindowMinCell.X + Resolution + OffsetX, WorldY), Resolution)].R.GetFloat() == 0.0f ? 0 : 1;
		}
	}

	ReportNearRippleLine(*this, FString::Printf(TEXT("TDFluidNearRippleMask: masked=%d masked_nonflat=%d water=%d water_without_fade=%d active_boundary=%d edge_active_before_shift=%d incoming=%d incoming_nonflat=%d"), MaskedCellCount, MaskedNonFlatCount, WaterCellCount, WaterWithoutFadeCount, ActiveBoundaryCount, EdgeActiveCount, IncomingCellCount, IncomingNonFlatCount));
	TestTrue(*(CaseLabel + TEXT(" masks cover dry and obstacle cells")), MaskedCellCount > 1000);
	TestEqual(*(CaseLabel + TEXT(" dry and obstacle cells keep zero height and zero fade")), MaskedNonFlatCount, 0);
	TestEqual(*(CaseLabel + TEXT(" water cells carry a positive fade")), WaterWithoutFadeCount, 0);
	TestTrue(*(CaseLabel + TEXT(" ripples reach the mask boundaries")), ActiveBoundaryCount > 0);
	TestTrue(*(CaseLabel + TEXT(" storage reused by the incoming band held ripples before the shift")), EdgeActiveCount > 0);
	TestEqual(*(CaseLabel + TEXT(" incoming band cells start flat")), IncomingNonFlatCount, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleRhiOffscreenCostTest,
	"TDGame.FluidFXRhi.NearRipple.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleRhiOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNearRippleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.NearRipple.OffscreenCost");
	if (!CanRunNearRippleRhiTest(*this, CaseLabel))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> SurfaceTexture(CreateUniformMaskTexture(FLinearColor(1.0f, 0.0f, 0.0f, 0.0f)));
	TStrongObjectPtr<UTexture2D> AuxTexture(CreateUniformMaskTexture(FLinearColor(0.0f, 1.0f, 0.0f, 0.0f)));
	TStrongObjectPtr<UTextureRenderTarget2D> OutputTarget(CreateNearRippleOutputTarget(NearRippleTestResolution));
	FlushRenderingCommands();
	FTextureResource* SurfaceResource = SurfaceTexture.IsValid() ? SurfaceTexture->GetResource() : nullptr;
	FTextureResource* AuxResource = AuxTexture.IsValid() ? AuxTexture->GetResource() : nullptr;
	FTextureRenderTargetResource* OutputResource = OutputTarget->GameThread_GetRenderTargetResource();
	if (!TestTrue(*(CaseLabel + TEXT(" created the masks and the output target")), SurfaceResource && AuxResource && OutputResource))
	{
		return false;
	}

	const FTDFluidWaterNearRippleSettings Settings = MakeNearRippleTestSettings(NearRippleDefaultExtentCm);
	const float CellSizeCm = NearRippleDefaultExtentCm / NearRippleTestResolution;
	TArray<FTDNearRippleCostCase> CostCases;
	for (const float StepSeconds : { 1.0f / 60.0f, 1.0f / 30.0f })
	{
		FTDNearRippleCostCase& CostCase = CostCases.AddDefaulted_GetRef();
		CostCase.StepSeconds = StepSeconds;
		CostCase.Plan = FTDFluidNearRippleLayer::ComputeSubstepPlan(Settings.WaveSpeedCmPerSec, StepSeconds, CellSizeCm, Settings.MaxSubsteps);
		CostCase.Milliseconds = MeasureCostFrameMilliseconds(CostCase, SurfaceResource, AuxResource, OutputResource);
		SummarizeCostCase(CostCase);
		ReportNearRippleLine(*this, FString::Printf(TEXT("TDFluidNearRippleCost: step=%.6f substeps=%d samples=%d median_ms=%.4f mean_ms=%.4f p95_ms=%.4f target_ms=%.2f"), StepSeconds, CostCase.Plan.SubstepCount, CostCase.Milliseconds.Num(), CostCase.MedianMilliseconds, CostCase.MeanMilliseconds, CostCase.P95Milliseconds, CostTargetMilliseconds));
		TestTrue(*FString::Printf(TEXT("%s step %.4f collected GPU timestamps"), *CaseLabel, StepSeconds), CostCase.Milliseconds.Num() >= CostMeasuredFrameCount / 2);
	}
	TestEqual(*(CaseLabel + TEXT(" 60 Hz runs one substep")), CostCases[0].Plan.SubstepCount, 1);
	TestEqual(*(CaseLabel + TEXT(" 30 Hz runs two substeps")), CostCases[1].Plan.SubstepCount, 2);

	const FString CostPath = GetNearRippleCostPath();
	TestTrue(*FString::Printf(TEXT("%s wrote %s"), *CaseLabel, *CostPath), WriteNearRippleCostJson(CostPath, CostCases));
	ReportNearRippleLine(*this, FString::Printf(TEXT("TDFluidNearRippleCost: json=%s"), *CostPath));
	return !HasAnyErrors();
}

#endif
