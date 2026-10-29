#if WITH_DEV_AUTOMATION_TESTS

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

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidViscousRhiTests, Log, All);

namespace
{
	constexpr float ViscousRhiStepSeconds = 1.0f / 60.0f;
	constexpr int32 ViscousRhiResolution = 128;
	constexpr int32 ViscousRhiStillStepCount = 30;
	constexpr int32 ViscousRhiFlowStepCount = 120;
	constexpr int32 ViscousRhiFlowStampCount = 2;
	constexpr float ViscousRhiDenseCeiling = 3.0f;
	constexpr float ViscousRhiDenseMargin = 1.0e-3f;
	constexpr double ViscousRhiTexelTolerance = 1.0e-3;
	constexpr double ViscousRhiDensityParityTolerance = 0.03;
	constexpr double ViscousRhiKineticEnergyParityTolerance = 0.10;
	constexpr double ViscousRhiDenseAreaParityTolerance = 0.10;
	constexpr int32 ViscousRhiCostStampCount = 32;
	constexpr int32 ViscousRhiCostWarmupFrameCount = 120;
	constexpr int32 ViscousRhiCostMeasuredFrameCount = 240;
	constexpr double ViscousRhiStrongStampXCm = -800.0;
	constexpr double ViscousRhiWeakStampXCm = 800.0;
	constexpr float ViscousRhiStrongStampStrength = 2.5f;
	constexpr float ViscousRhiWeakStampStrength = 0.12f;

	struct FTDViscousRhiChannelError
	{
		double Rms = 0.0;
		double Maximum = 0.0;
	};

	struct FTDViscousRhiPixelStatistics
	{
		double TotalDensity = 0.0;
		double KineticEnergy = 0.0;
		FVector2D DensityCentroidCells = FVector2D::ZeroVector;
		bool bHasDensityCentroid = false;
		double DenseAreaRatio = 0.0;
	};

	struct FTDViscousRhiPixels
	{
		TArray<FFloat16Color> Density;
		TArray<FFloat16Color> Velocity;
	};

	bool CanRunViscousRhiTest(FAutomationTestBase& Test, const TCHAR* TestName)
	{
		if (GUsingNullRHI || !FApp::CanEverRender())
		{
			Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI."), TestName));
			return false;
		}
		return true;
	}

	void ReportViscousRhiLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidViscousRhiTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	FTDFluidSimulationConfig MakeViscousRhiStillConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = ViscousRhiResolution;
		Config.bEnableWaterLayer = false;
		Config.DensityDissipation = 0.0f;
		Config.VelocityDamping = 0.0f;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.BuoyancyCmPerSec2 = 0.0f;
		Config.VortexTangentialAccelerationCmPerSec2 = 0.0f;
		Config.VortexRadialDriftCmPerSec = 0.0f;
		Config.DensityAccumulationPerSec = 2.0f;
		Config.DenseDensityCeiling = ViscousRhiDenseCeiling;
		Config.DenseGrowthThreshold = 0.15f;
		return Config;
	}

	FTDFluidSimulationConfig MakeViscousRhiFlowConfig()
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
		Config.GridResolution = ViscousRhiResolution;
		Config.bEnableWaterLayer = false;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		Config.DensityDissipation = 0.1f;
		Config.MaxPointsPerTick = 64;
		Config.FlowNoiseAccelerationCmPerSec2 = 70.0f;
		Config.FlowNoiseSpeedMaskReferenceCmPerSec = 120.0f;
		Config.DensityAccumulationPerSec = 1.5f;
		Config.DenseDensityCeiling = ViscousRhiDenseCeiling;
		Config.DenseGrowthThreshold = 0.15f;
		return Config;
	}

	FTDFluidSimulationConfig MakeViscousRhiLegacyConfig(const FTDFluidSimulationConfig& Config)
	{
		FTDFluidSimulationConfig LegacyConfig = Config;
		LegacyConfig.DensityAccumulationPerSec = 0.0f;
		LegacyConfig.DenseDensityCeiling = 1.0f;
		LegacyConfig.FlowNoiseSpeedMaskReferenceCmPerSec = 0.0f;
		return LegacyConfig;
	}

	FTDFluidBrushStamp MakeViscousRhiStillStamp(double LocationXCm, float Strength)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(LocationXCm, 0.0, 0.0);
		Stamp.RadiusCm = 300.0f;
		Stamp.DensityStrength = Strength;
		Stamp.VelocityStrength = 0.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		return Stamp;
	}

	void AppendViscousRhiFlowStamps(const FTDFluidSimulationConfig& Config, const FVector2D& AnchorCm, int32 StepIndex, int32 StampCount, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		const double ExtentCm = Config.SimExtentCm.X;
		const double OrbitRadiusCm = ExtentCm * 0.25;
		for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
		{
			const double Angle = StepIndex * 0.045 + StampIndex * (UE_DOUBLE_TWO_PI / StampCount);
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(AnchorCm.X + FMath::Cos(Angle) * OrbitRadiusCm, AnchorCm.Y + FMath::Sin(Angle) * OrbitRadiusCm, 0.0);
			Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 260.0;
			Stamp.RadiusCm = static_cast<float>(ExtentCm * 0.06);
			Stamp.DensityStrength = 2.0f;
			Stamp.VelocityStrength = 1.0f;
			Stamp.Channel = StampIndex % 2 == 0 ? ETDFluidBrushChannel::ChannelR_Mud : ETDFluidBrushChannel::ChannelG_Blood;
			OutStamps.Add(Stamp);
		}
	}

	UTextureRenderTarget2D* CreateViscousRhiTarget(int32 Resolution)
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

	bool ReadViscousRhiTargetPixels(UTextureRenderTarget2D* Target, TArray<FFloat16Color>& OutPixels)
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

	uint64 ViscousRhiNextActorKey = 0;

	struct FTDViscousRhiCpuArm
	{
		void Initialize(const FTDFluidSimulationConfig& Config)
		{
			FTDFluidSimulationConfig CpuConfig = Config;
			CpuConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
			Solver.Initialize(CpuConfig, FVector2D::ZeroVector);
		}

		void Step(const TArray<FTDFluidBrushStamp>& Stamps)
		{
			Solver.ApplyBrushStamps(Stamps, ViscousRhiStepSeconds);
			Solver.StepSimulation(ViscousRhiStepSeconds);
		}

		void Pack(FTDViscousRhiPixels& OutPixels) const
		{
			Solver.PackOutputPixels(OutPixels.Density, OutPixels.Velocity);
		}

		FTDFluidGrid2DSolver Solver;
	};

	struct FTDViscousRhiGpuArm
	{
		FTDViscousRhiGpuArm() = default;
		FTDViscousRhiGpuArm(const FTDViscousRhiGpuArm&) = delete;
		FTDViscousRhiGpuArm& operator=(const FTDViscousRhiGpuArm&) = delete;

		~FTDViscousRhiGpuArm()
		{
			ENQUEUE_RENDER_COMMAND(TDFluidViscousRhiRelease)([State = MoveTemp(RenderState)](FRHICommandListImmediate&) mutable
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
			Proxy.AssignActorKey(++ViscousRhiNextActorKey);
			DensityTarget = TStrongObjectPtr<UTextureRenderTarget2D>(CreateViscousRhiTarget(Proxy.GetResolution()));
			VelocityTarget = TStrongObjectPtr<UTextureRenderTarget2D>(CreateViscousRhiTarget(Proxy.GetResolution()));
		}

		bool Step(const TArray<FTDFluidBrushStamp>& Stamps)
		{
			Proxy.PrepareStep(ViscousRhiStepSeconds, TConstArrayView<FTDFluidBrushStamp>(Stamps), FVector2D::ZeroVector, false);
			FTDFluidGpuFrameInputs FrameInputs;
			FrameInputs.FrameNumber = ++FrameNumber;
			FTDFluidGpuActorInput& ActorInput = FrameInputs.Actors.AddDefaulted_GetRef();
			if (!Proxy.GatherStep(FrameInputs.FrameNumber, 0, DensityTarget->GameThread_GetRenderTargetResource(), VelocityTarget->GameThread_GetRenderTargetResource(), ActorInput))
			{
				return false;
			}

			ENQUEUE_RENDER_COMMAND(TDFluidViscousRhiStep)([State = RenderState, SubmittedInputs = MoveTemp(FrameInputs)](FRHICommandListImmediate& RHICmdList)
			{
				State->ExecuteFrame(RHICmdList, SubmittedInputs);
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
			return true;
		}

		bool Read(FTDViscousRhiPixels& OutPixels)
		{
			return ReadViscousRhiTargetPixels(DensityTarget.Get(), OutPixels.Density) && ReadViscousRhiTargetPixels(VelocityTarget.Get(), OutPixels.Velocity);
		}

		FTDFluidGpuSolverProxy Proxy;
		TSharedPtr<FTDFluidGpuSolverRenderState, ESPMode::ThreadSafe> RenderState = MakeShared<FTDFluidGpuSolverRenderState, ESPMode::ThreadSafe>();
		TStrongObjectPtr<UTextureRenderTarget2D> DensityTarget;
		TStrongObjectPtr<UTextureRenderTarget2D> VelocityTarget;
		uint32 FrameNumber = 0;
	};

	bool RunViscousRhiPair(FAutomationTestBase& Test, const FString& Label, const FTDFluidSimulationConfig& Config, int32 StepCount, TFunctionRef<void(int32, TArray<FTDFluidBrushStamp>&)> AppendStamps, FTDViscousRhiPixels& OutCpuPixels, FTDViscousRhiPixels& OutGpuPixels)
	{
		FTDViscousRhiCpuArm CpuArm;
		FTDViscousRhiGpuArm GpuArm;
		CpuArm.Initialize(Config);
		GpuArm.Initialize(Config);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			TArray<FTDFluidBrushStamp> Stamps;
			AppendStamps(StepIndex, Stamps);
			CpuArm.Step(Stamps);
			if (!Test.TestTrue(*FString::Printf(TEXT("%s GPU step %d gathered"), *Label, StepIndex), GpuArm.Step(Stamps)))
			{
				return false;
			}
		}

		CpuArm.Pack(OutCpuPixels);
		if (!Test.TestTrue(*(Label + TEXT(" read the GPU output targets")), GpuArm.Read(OutGpuPixels)))
		{
			return false;
		}
		return Test.TestEqual(*(Label + TEXT(" CPU and GPU outputs hold the same cell count")), OutGpuPixels.Density.Num(), OutCpuPixels.Density.Num())
			&& Test.TestEqual(*(Label + TEXT(" CPU and GPU velocity outputs hold the same cell count")), OutGpuPixels.Velocity.Num(), OutCpuPixels.Velocity.Num());
	}

	bool RunViscousRhiGpuOnly(FAutomationTestBase& Test, const FTDFluidSimulationConfig& Config, int32 StepCount, FTDViscousRhiPixels& OutPixels)
	{
		FTDViscousRhiGpuArm GpuArm;
		GpuArm.Initialize(Config);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			TArray<FTDFluidBrushStamp> Stamps;
			AppendViscousRhiFlowStamps(Config, FVector2D::ZeroVector, StepIndex, ViscousRhiFlowStampCount, Stamps);
			if (!GpuArm.Step(Stamps))
			{
				return false;
			}
		}
		return Test.TestTrue(TEXT("Replay read the GPU output targets"), GpuArm.Read(OutPixels));
	}

	FTDViscousRhiChannelError MeasureViscousRhiDensityError(const TArray<FFloat16Color>& CpuPixels, const TArray<FFloat16Color>& GpuPixels)
	{
		FTDViscousRhiChannelError Error;
		if (CpuPixels.Num() != GpuPixels.Num() || CpuPixels.IsEmpty())
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
			const double PrimaryDifference = Cpu.R - Gpu.R;
			const double SelectiveDifference = Cpu.B - Gpu.B;
			SquaredSum += PrimaryDifference * PrimaryDifference + SelectiveDifference * SelectiveDifference;
			Error.Maximum = FMath::Max(Error.Maximum, FMath::Max(FMath::Abs(PrimaryDifference), FMath::Abs(SelectiveDifference)));
		}
		Error.Rms = FMath::Sqrt(SquaredSum / (2.0 * CpuPixels.Num()));
		return Error;
	}

	double MeasureViscousRhiPrimaryPeak(const TArray<FFloat16Color>& Pixels)
	{
		double Peak = 0.0;
		for (const FFloat16Color& Pixel : Pixels)
		{
			Peak = FMath::Max(Peak, static_cast<double>(Pixel.GetFloats().R));
		}
		return Peak;
	}

	int32 FindViscousRhiNearestCellIndex(const FTDFluidGrid2DSolver& Solver, const FVector2D& PlanePointCm)
	{
		const int32 Resolution = Solver.GetResolution();
		int32 NearestIndex = INDEX_NONE;
		double NearestDistanceSquared = TNumericLimits<double>::Max();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const double DistanceSquared = FVector2D::DistSquared(Solver.CellToWorldCenter(CellX, CellY), PlanePointCm);
				if (DistanceSquared >= NearestDistanceSquared)
				{
					continue;
				}
				NearestDistanceSquared = DistanceSquared;
				NearestIndex = CellY * Resolution + CellX;
			}
		}
		return NearestIndex;
	}

	double SampleViscousRhiPrimaryAtPlane(const FTDFluidGrid2DSolver& Solver, const TArray<FFloat16Color>& Pixels, double PlaneXCm)
	{
		const int32 PixelIndex = FindViscousRhiNearestCellIndex(Solver, FVector2D(PlaneXCm, 0.0));
		return Pixels.IsValidIndex(PixelIndex) ? static_cast<double>(Pixels[PixelIndex].GetFloats().R) : -1.0;
	}

	double WrapViscousRhiCellOffset(double OffsetCells, int32 Resolution)
	{
		return OffsetCells - Resolution * FMath::RoundToDouble(OffsetCells / Resolution);
	}

	FTDViscousRhiPixelStatistics ComputeViscousRhiPixelStatistics(const FTDViscousRhiPixels& Pixels, int32 Resolution, float DenseCeiling)
	{
		FTDViscousRhiPixelStatistics Statistics;
		const int32 CellCount = Resolution * Resolution;
		if (Pixels.Density.Num() != CellCount || Pixels.Velocity.Num() != CellCount)
		{
			return Statistics;
		}

		const double DenseEncodedThreshold = (1.0 + ViscousRhiDenseMargin) / DenseCeiling;
		const double AngleStep = UE_DOUBLE_TWO_PI / Resolution;
		FVector2D CosineSum = FVector2D::ZeroVector;
		FVector2D SineSum = FVector2D::ZeroVector;
		int32 DenseCellCount = 0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const int32 CellIndex = CellY * Resolution + CellX;
				const FLinearColor Density = Pixels.Density[CellIndex].GetFloats();
				const FLinearColor Velocity = Pixels.Velocity[CellIndex].GetFloats();
				const double Primary = Density.R;
				const double VelocityX = Velocity.R * 2.0 - 1.0;
				const double VelocityY = Velocity.G * 2.0 - 1.0;
				Statistics.TotalDensity += Primary;
				Statistics.KineticEnergy += 0.5 * (VelocityX * VelocityX + VelocityY * VelocityY);
				CosineSum += FVector2D(FMath::Cos((CellX + 0.5) * AngleStep), FMath::Cos((CellY + 0.5) * AngleStep)) * Primary;
				SineSum += FVector2D(FMath::Sin((CellX + 0.5) * AngleStep), FMath::Sin((CellY + 0.5) * AngleStep)) * Primary;
				DenseCellCount += Primary > DenseEncodedThreshold ? 1 : 0;
			}
		}

		Statistics.DenseAreaRatio = static_cast<double>(DenseCellCount) / CellCount;
		Statistics.bHasDensityCentroid = Statistics.TotalDensity > UE_DOUBLE_KINDA_SMALL_NUMBER;
		if (!Statistics.bHasDensityCentroid)
		{
			return Statistics;
		}
		Statistics.DensityCentroidCells = FVector2D(
			FMath::Atan2(SineSum.X, CosineSum.X) / AngleStep,
			FMath::Atan2(SineSum.Y, CosineSum.Y) / AngleStep);
		return Statistics;
	}

	double ComputeViscousRhiRelativeDifference(double CpuValue, double GpuValue)
	{
		return FMath::Abs(GpuValue - CpuValue) / FMath::Max(FMath::Abs(CpuValue), UE_DOUBLE_SMALL_NUMBER);
	}

	void ExpectViscousRhiStatisticalParity(FAutomationTestBase& Test, const FString& Label, const FTDViscousRhiPixels& CpuPixels, const FTDViscousRhiPixels& GpuPixels, int32 Resolution)
	{
		const FTDViscousRhiPixelStatistics CpuStatistics = ComputeViscousRhiPixelStatistics(CpuPixels, Resolution, ViscousRhiDenseCeiling);
		const FTDViscousRhiPixelStatistics GpuStatistics = ComputeViscousRhiPixelStatistics(GpuPixels, Resolution, ViscousRhiDenseCeiling);
		const double DensityDifference = ComputeViscousRhiRelativeDifference(CpuStatistics.TotalDensity, GpuStatistics.TotalDensity);
		const double KineticEnergyDifference = ComputeViscousRhiRelativeDifference(CpuStatistics.KineticEnergy, GpuStatistics.KineticEnergy);
		const double DenseAreaDifference = ComputeViscousRhiRelativeDifference(CpuStatistics.DenseAreaRatio, GpuStatistics.DenseAreaRatio);
		const double CentroidOffsetX = WrapViscousRhiCellOffset(GpuStatistics.DensityCentroidCells.X - CpuStatistics.DensityCentroidCells.X, Resolution);
		const double CentroidOffsetY = WrapViscousRhiCellOffset(GpuStatistics.DensityCentroidCells.Y - CpuStatistics.DensityCentroidCells.Y, Resolution);

		ReportViscousRhiLine(Test, FString::Printf(
			TEXT("TDFluidViscousParity: case=%s density_cpu=%.3f density_gpu=%.3f density_diff=%.4f ke_cpu=%.4f ke_gpu=%.4f ke_diff=%.4f dense_area_cpu=%.5f dense_area_gpu=%.5f dense_area_diff=%.4f centroid_dx_cells=%.3f centroid_dy_cells=%.3f"),
			*Label,
			CpuStatistics.TotalDensity,
			GpuStatistics.TotalDensity,
			DensityDifference,
			CpuStatistics.KineticEnergy,
			GpuStatistics.KineticEnergy,
			KineticEnergyDifference,
			CpuStatistics.DenseAreaRatio,
			GpuStatistics.DenseAreaRatio,
			DenseAreaDifference,
			CentroidOffsetX,
			CentroidOffsetY));

		Test.TestTrue(*(Label + TEXT(" CPU reference carries density")), CpuStatistics.TotalDensity > UE_DOUBLE_KINDA_SMALL_NUMBER);
		Test.TestTrue(*(Label + TEXT(" CPU reference carries kinetic energy")), CpuStatistics.KineticEnergy > UE_DOUBLE_KINDA_SMALL_NUMBER);
		Test.TestTrue(*(Label + TEXT(" CPU reference holds cells above the legacy ceiling")), CpuStatistics.DenseAreaRatio > 0.0);
		Test.TestTrue(*(Label + TEXT(" both fields have a density centroid")), CpuStatistics.bHasDensityCentroid && GpuStatistics.bHasDensityCentroid);
		Test.TestTrue(*(Label + TEXT(" total density matches the CPU within 3%")), DensityDifference <= ViscousRhiDensityParityTolerance);
		Test.TestTrue(*(Label + TEXT(" kinetic energy matches the CPU within 10%")), KineticEnergyDifference <= ViscousRhiKineticEnergyParityTolerance);
		Test.TestTrue(*(Label + TEXT(" dense area ratio matches the CPU within 10%")), DenseAreaDifference <= ViscousRhiDenseAreaParityTolerance);
		Test.TestTrue(*(Label + TEXT(" density centroid X is within one simulation cell")), FMath::Abs(CentroidOffsetX) <= 1.0);
		Test.TestTrue(*(Label + TEXT(" density centroid Y is within one simulation cell")), FMath::Abs(CentroidOffsetY) <= 1.0);
	}

	void ReportViscousRhiLegacyKineticEnergy(FAutomationTestBase& Test, const FString& Label, const FTDViscousRhiPixels& CpuPixels, const FTDViscousRhiPixels& GpuPixels, int32 Resolution)
	{
		const FTDViscousRhiPixelStatistics CpuStatistics = ComputeViscousRhiPixelStatistics(CpuPixels, Resolution, 1.0f);
		const FTDViscousRhiPixelStatistics GpuStatistics = ComputeViscousRhiPixelStatistics(GpuPixels, Resolution, 1.0f);
		ReportViscousRhiLine(Test, FString::Printf(
			TEXT("TDFluidViscousLegacyParity: case=%s density_diff=%.4f ke_cpu=%.4f ke_gpu=%.4f ke_diff=%.4f"),
			*Label,
			ComputeViscousRhiRelativeDifference(CpuStatistics.TotalDensity, GpuStatistics.TotalDensity),
			CpuStatistics.KineticEnergy,
			GpuStatistics.KineticEnergy,
			ComputeViscousRhiRelativeDifference(CpuStatistics.KineticEnergy, GpuStatistics.KineticEnergy)));
	}

	double ComputeViscousRhiMedian(TArray<double> Values)
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
	FTDFluidViscousGpuMatchesCpuRhiTest,
	"TDGame.FluidFXRhi.Viscous.GpuMatchesCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousGpuMatchesCpuRhiTest::RunTest(const FString& Parameters)
{
	if (!CanRunViscousRhiTest(*this, TEXT("TDGame.FluidFXRhi.Viscous.GpuMatchesCpu")))
	{
		return false;
	}

	const FTDFluidSimulationConfig StillConfig = MakeViscousRhiStillConfig();
	FTDViscousRhiPixels StillCpu;
	FTDViscousRhiPixels StillGpu;
	const bool bStillRan = RunViscousRhiPair(*this, TEXT("Still growth"), StillConfig, ViscousRhiStillStepCount, [](int32, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		OutStamps.Add(MakeViscousRhiStillStamp(ViscousRhiStrongStampXCm, ViscousRhiStrongStampStrength));
		OutStamps.Add(MakeViscousRhiStillStamp(ViscousRhiWeakStampXCm, ViscousRhiWeakStampStrength));
	}, StillCpu, StillGpu);
	if (!bStillRan)
	{
		return false;
	}

	FTDFluidGrid2DSolver StillLayout;
	StillLayout.Initialize(StillConfig, FVector2D::ZeroVector);
	const FTDViscousRhiChannelError StillError = MeasureViscousRhiDensityError(StillCpu.Density, StillGpu.Density);
	const double CpuWeakCenter = SampleViscousRhiPrimaryAtPlane(StillLayout, StillCpu.Density, ViscousRhiWeakStampXCm);
	const double GpuWeakCenter = SampleViscousRhiPrimaryAtPlane(StillLayout, StillGpu.Density, ViscousRhiWeakStampXCm);
	const FTDViscousRhiPixelStatistics StillCpuStatistics = ComputeViscousRhiPixelStatistics(StillCpu, StillLayout.GetResolution(), ViscousRhiDenseCeiling);
	ReportViscousRhiLine(*this, FString::Printf(TEXT("TDFluidViscousStill: rms=%.6f max=%.6f cpu_peak=%.4f gpu_peak=%.4f cpu_weak=%.5f gpu_weak=%.5f cpu_dense_area=%.5f"), StillError.Rms, StillError.Maximum, MeasureViscousRhiPrimaryPeak(StillCpu.Density), MeasureViscousRhiPrimaryPeak(StillGpu.Density), CpuWeakCenter, GpuWeakCenter, StillCpuStatistics.DenseAreaRatio));
	TestTrue(TEXT("Still growth CPU reaches the dense ceiling"), MeasureViscousRhiPrimaryPeak(StillCpu.Density) >= 1.0 - ViscousRhiTexelTolerance);
	TestTrue(TEXT("Still growth GPU reaches the dense ceiling"), MeasureViscousRhiPrimaryPeak(StillGpu.Density) >= 1.0 - ViscousRhiTexelTolerance);
	TestTrue(TEXT("Still growth CPU holds cells above the legacy ceiling"), StillCpuStatistics.DenseAreaRatio > 0.0);
	TestTrue(TEXT("Still growth keeps the weak stamp below the threshold on the CPU"), CpuWeakCenter >= 0.0 && CpuWeakCenter <= ViscousRhiWeakStampStrength / ViscousRhiDenseCeiling + ViscousRhiTexelTolerance);
	TestTrue(*FString::Printf(TEXT("Still growth density RMS %.6f <= 1e-3"), StillError.Rms), StillError.Rms <= ViscousRhiTexelTolerance);
	TestTrue(*FString::Printf(TEXT("Still growth weak stamp center difference %.6f <= 1e-3"), FMath::Abs(CpuWeakCenter - GpuWeakCenter)), FMath::Abs(CpuWeakCenter - GpuWeakCenter) <= ViscousRhiTexelTolerance);

	const FTDFluidSimulationConfig FlowConfig = MakeViscousRhiFlowConfig();
	FTDViscousRhiPixels FlowCpu;
	FTDViscousRhiPixels FlowGpu;
	const bool bFlowRan = RunViscousRhiPair(*this, TEXT("Dense flow"), FlowConfig, ViscousRhiFlowStepCount, [&FlowConfig](int32 StepIndex, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		AppendViscousRhiFlowStamps(FlowConfig, FVector2D::ZeroVector, StepIndex, ViscousRhiFlowStampCount, OutStamps);
	}, FlowCpu, FlowGpu);
	if (bFlowRan)
	{
		ExpectViscousRhiStatisticalParity(*this, TEXT("Dense flow"), FlowCpu, FlowGpu, ViscousRhiResolution);
	}

	const FTDFluidSimulationConfig LegacyFlowConfig = MakeViscousRhiLegacyConfig(FlowConfig);
	FTDViscousRhiPixels LegacyCpu;
	FTDViscousRhiPixels LegacyGpu;
	const bool bLegacyRan = RunViscousRhiPair(*this, TEXT("Legacy flow"), LegacyFlowConfig, ViscousRhiFlowStepCount, [&LegacyFlowConfig](int32 StepIndex, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		AppendViscousRhiFlowStamps(LegacyFlowConfig, FVector2D::ZeroVector, StepIndex, ViscousRhiFlowStampCount, OutStamps);
	}, LegacyCpu, LegacyGpu);
	if (bLegacyRan)
	{
		ReportViscousRhiLegacyKineticEnergy(*this, TEXT("Legacy flow"), LegacyCpu, LegacyGpu, ViscousRhiResolution);
	}

	FTDFluidSimulationConfig WrapConfig = FlowConfig;
	WrapConfig.AreaAddressMode = ETDFluidAreaAddressMode::Wrap;
	const FVector2D WrapAnchorCm(WrapConfig.SimExtentCm.X * 0.4, 0.0);
	FTDViscousRhiPixels WrapCpu;
	FTDViscousRhiPixels WrapGpu;
	const bool bWrapRan = RunViscousRhiPair(*this, TEXT("Dense wrap"), WrapConfig, ViscousRhiFlowStepCount, [&WrapConfig, &WrapAnchorCm](int32 StepIndex, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		AppendViscousRhiFlowStamps(WrapConfig, WrapAnchorCm, StepIndex, ViscousRhiFlowStampCount, OutStamps);
	}, WrapCpu, WrapGpu);
	if (bWrapRan)
	{
		ExpectViscousRhiStatisticalParity(*this, TEXT("Dense wrap"), WrapCpu, WrapGpu, ViscousRhiResolution);
	}
	return bFlowRan && bWrapRan;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousDeterministicReplayRhiTest,
	"TDGame.FluidFXRhi.Viscous.DeterministicReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousDeterministicReplayRhiTest::RunTest(const FString& Parameters)
{
	if (!CanRunViscousRhiTest(*this, TEXT("TDGame.FluidFXRhi.Viscous.DeterministicReplay")))
	{
		return false;
	}

	const FTDFluidSimulationConfig Config = MakeViscousRhiFlowConfig();
	FTDViscousRhiPixels First;
	FTDViscousRhiPixels Second;
	FTDViscousRhiPixels Legacy;
	if (!RunViscousRhiGpuOnly(*this, Config, ViscousRhiFlowStepCount, First) || !RunViscousRhiGpuOnly(*this, Config, ViscousRhiFlowStepCount, Second) || !RunViscousRhiGpuOnly(*this, MakeViscousRhiLegacyConfig(Config), ViscousRhiFlowStepCount, Legacy))
	{
		return false;
	}

	const uint32 FirstDensityCrc = FCrc::MemCrc32(First.Density.GetData(), First.Density.Num() * sizeof(FFloat16Color));
	const uint32 SecondDensityCrc = FCrc::MemCrc32(Second.Density.GetData(), Second.Density.Num() * sizeof(FFloat16Color));
	const uint32 LegacyDensityCrc = FCrc::MemCrc32(Legacy.Density.GetData(), Legacy.Density.Num() * sizeof(FFloat16Color));
	const uint32 FirstVelocityCrc = FCrc::MemCrc32(First.Velocity.GetData(), First.Velocity.Num() * sizeof(FFloat16Color));
	const uint32 SecondVelocityCrc = FCrc::MemCrc32(Second.Velocity.GetData(), Second.Velocity.Num() * sizeof(FFloat16Color));
	ReportViscousRhiLine(*this, FString::Printf(TEXT("TDFluidViscousReplay: density_crc=%08x/%08x legacy_density_crc=%08x velocity_crc=%08x/%08x"), FirstDensityCrc, SecondDensityCrc, LegacyDensityCrc, FirstVelocityCrc, SecondVelocityCrc));
	TestTrue(TEXT("Replay density carries a trail"), MeasureViscousRhiPrimaryPeak(First.Density) > 0.1);
	TestEqual(TEXT("Replay density output CRC matches"), SecondDensityCrc, FirstDensityCrc);
	TestEqual(TEXT("Replay velocity output CRC matches"), SecondVelocityCrc, FirstVelocityCrc);
	TestTrue(TEXT("Dense density and noise mask change the GPU output against the legacy settings"), LegacyDensityCrc != FirstDensityCrc);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousOffscreenCostRhiTest,
	"TDGame.FluidFXRhi.Viscous.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousOffscreenCostRhiTest::RunTest(const FString& Parameters)
{
	if (!CanRunViscousRhiTest(*this, TEXT("TDGame.FluidFXRhi.Viscous.OffscreenCost")))
	{
		return false;
	}

	struct FTDViscousRhiCostCase
	{
		ETDFluidPresetKind Kind;
		const TCHAR* Name;
		int32 Resolution;
	};
	const FTDViscousRhiCostCase CostCases[] = {
		{ ETDFluidPresetKind::ViscousMud, TEXT("ViscousMud"), 256 },
		{ ETDFluidPresetKind::LavaTile, TEXT("LavaTile"), 128 },
		{ ETDFluidPresetKind::LavaLocal, TEXT("LavaLocal"), 256 }
	};

	TArray<TSharedPtr<FJsonValue>> CaseValues;
	for (const FTDViscousRhiCostCase& CostCase : CostCases)
	{
		for (const bool bUsesDenseDensity : { false, true })
		{
			FTDFluidSimulationConfig KindConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(CostCase.Kind);
			KindConfig.GridResolution = CostCase.Resolution;
			const FTDFluidSimulationConfig Config = bUsesDenseDensity ? KindConfig : MakeViscousRhiLegacyConfig(KindConfig);
			FTDViscousRhiGpuArm GpuArm;
			GpuArm.Initialize(Config);
			TArray<double> FrameMilliseconds;
			FrameMilliseconds.Reserve(ViscousRhiCostMeasuredFrameCount);
			for (int32 FrameIndex = 0; FrameIndex < ViscousRhiCostWarmupFrameCount + ViscousRhiCostMeasuredFrameCount; ++FrameIndex)
			{
				TArray<FTDFluidBrushStamp> Stamps;
				AppendViscousRhiFlowStamps(Config, FVector2D::ZeroVector, FrameIndex, ViscousRhiCostStampCount, Stamps);
				const double StartSeconds = FPlatformTime::Seconds();
				if (!TestTrue(TEXT("Cost run GPU step gathered"), GpuArm.Step(Stamps)))
				{
					return false;
				}
				if (FrameIndex >= ViscousRhiCostWarmupFrameCount)
				{
					FrameMilliseconds.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
				}
			}

			const double MedianMilliseconds = ComputeViscousRhiMedian(FrameMilliseconds);
			ReportViscousRhiLine(*this, FString::Printf(TEXT("TDFluidViscousCost: kind=%s resolution=%d stamps=%d dense=%d median_frame_ms=%.4f"), CostCase.Name, GpuArm.Proxy.GetResolution(), ViscousRhiCostStampCount, bUsesDenseDensity ? 1 : 0, MedianMilliseconds));
			TSharedRef<FJsonObject> CaseObject = MakeShared<FJsonObject>();
			CaseObject->SetStringField(TEXT("kind"), CostCase.Name);
			CaseObject->SetBoolField(TEXT("dense"), bUsesDenseDensity);
			CaseObject->SetNumberField(TEXT("resolution"), GpuArm.Proxy.GetResolution());
			CaseObject->SetNumberField(TEXT("median_frame_ms"), MedianMilliseconds);
			CaseValues.Add(MakeShared<FJsonValueObject>(CaseObject));
			TestTrue(TEXT("Cost run measured frames"), FrameMilliseconds.Num() == ViscousRhiCostMeasuredFrameCount);
		}
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("measure"), TEXT("wall time of one submitted and GPU idle blocked frame, median of 240 after 120 warmup"));
	RootObject->SetNumberField(TEXT("stamps"), ViscousRhiCostStampCount);
	RootObject->SetArrayField(TEXT("cases"), CaseValues);
	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(RootObject, Writer);
	const FString OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("wave3"), TEXT("fx14_viscous_cost.json"));
	TestTrue(TEXT("Cost JSON written"), FFileHelper::SaveStringToFile(JsonText, *OutputPath));
	ReportViscousRhiLine(*this, FString::Printf(TEXT("TDFluidViscousCost: json=%s"), *OutputPath));
	return true;
}

#endif
