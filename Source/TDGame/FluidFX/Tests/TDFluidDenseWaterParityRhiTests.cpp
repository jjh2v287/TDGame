#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/TextureRenderTarget2D.h"
#include "FluidFX/TDFluidDenseWaterCacheAsset.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuStepPasses.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace TDFluidDenseWaterParityRhiTestPrivate
{
	constexpr int32 ParityResolution = 192;
	constexpr double ParityExtentCm = 1920.0;
	constexpr float ParityStepSeconds = 1.0f / 60.0f;
	constexpr int32 ParityStepCount = 180;
	constexpr float ParityDepthReferenceCm = 100.0f;
	constexpr float ValleySlope = 0.1f;
	constexpr float SeedDepthCm = 20.0f;
	constexpr uint64 ParityActorKey = 6201;
	constexpr double MinimumWetIntersectionOverUnion = 0.9;
	constexpr double MaximumVolumeRatioDifference = 0.03;
	constexpr double MaximumCenterOfMassCells = 2.0;

	struct FTDDenseParityField
	{
		TArray<float> DepthCm;
		int32 Resolution = 0;
	};

	struct FTDDenseParityMeasure
	{
		double VolumeCm3 = 0.0;
		FVector2D CenterOfMassCells = FVector2D::ZeroVector;
		int32 WetCellCount = 0;
	};

	FVector2D ComputeCellCenterPlane(int32 CellX, int32 CellY, int32 Resolution)
	{
		const double CellCm = ParityExtentCm / static_cast<double>(Resolution);
		return FVector2D(-0.5 * ParityExtentCm + (CellX + 0.5) * CellCm, -0.5 * ParityExtentCm + (CellY + 0.5) * CellCm);
	}

	float ComputeValleyElevationCm(const FVector2D& PlanePoint)
	{
		return ValleySlope * static_cast<float>(FMath::Abs(PlanePoint.X));
	}

	float ComputeSeedDepthCm(const FVector2D& PlanePoint)
	{
		const bool bIsInsideSeed = PlanePoint.X >= -700.0 && PlanePoint.X <= -400.0 && FMath::Abs(PlanePoint.Y) <= 300.0;
		return bIsInsideSeed ? SeedDepthCm : 0.0f;
	}

	FTDFluidSimulationConfig MakeParityConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = ParityResolution;
		Config.SimExtentCm = FVector2D(ParityExtentCm, ParityExtentCm);
		Config.bEnableWaterLayer = true;
		Config.bEnableDenseWater = true;
		Config.bTreatDryCellsAsSolid = false;
		Config.bOpenWaterBorders = false;
		Config.bEnableWaterWetness = false;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.RippleWaveSpeedCmPerSec = 0.0f;
		Config.WaterDepthReferenceCm = ParityDepthReferenceCm;
		return Config;
	}

	FTDFluidDenseWaterSeed MakeParitySeed()
	{
		FTDFluidDenseWaterSeed Seed;
		Seed.Resolution = ParityResolution;
		Seed.CenterCm = FVector2D::ZeroVector;
		Seed.ExtentCm = FVector2D(ParityExtentCm, ParityExtentCm);
		const int32 CellCount = ParityResolution * ParityResolution;
		Seed.DepthCm.SetNumZeroed(CellCount);
		Seed.FaceVelocityXCmPerSec.SetNumZeroed(CellCount);
		Seed.FaceVelocityYCmPerSec.SetNumZeroed(CellCount);
		for (int32 CellY = 0; CellY < ParityResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < ParityResolution; ++CellX)
			{
				Seed.DepthCm[CellY * ParityResolution + CellX] = ComputeSeedDepthCm(ComputeCellCenterPlane(CellX, CellY, ParityResolution));
			}
		}
		return Seed;
	}

	void DecodeDepthPixels(const TArray<FFloat16Color>& Pixels, int32 Resolution, FTDDenseParityField& OutField)
	{
		OutField.Resolution = Resolution;
		OutField.DepthCm.SetNumZeroed(Resolution * Resolution);
		const int32 PixelCount = FMath::Min(Pixels.Num(), Resolution * Resolution);
		for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
		{
			OutField.DepthCm[PixelIndex] = Pixels[PixelIndex].R.GetFloat() * ParityDepthReferenceCm;
		}
	}

	float SampleFieldAtReferenceCell(const FTDDenseParityField& Field, int32 CellX, int32 CellY)
	{
		const int32 FieldX = FMath::Clamp(FMath::FloorToInt32((CellX + 0.5) * Field.Resolution / ParityResolution), 0, Field.Resolution - 1);
		const int32 FieldY = FMath::Clamp(FMath::FloorToInt32((CellY + 0.5) * Field.Resolution / ParityResolution), 0, Field.Resolution - 1);
		return Field.DepthCm[FieldY * Field.Resolution + FieldX];
	}

	FTDDenseParityMeasure MeasureField(const FTDDenseParityField& Field, float WetThresholdCm)
	{
		FTDDenseParityMeasure Measure;
		const double CellAreaCm2 = FMath::Square(ParityExtentCm / ParityResolution);
		FVector2D WeightedCells = FVector2D::ZeroVector;
		for (int32 CellY = 0; CellY < ParityResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < ParityResolution; ++CellX)
			{
				const float DepthCm = SampleFieldAtReferenceCell(Field, CellX, CellY);
				if (DepthCm <= WetThresholdCm)
				{
					continue;
				}

				++Measure.WetCellCount;
				Measure.VolumeCm3 += DepthCm * CellAreaCm2;
				WeightedCells += FVector2D(CellX + 0.5, CellY + 0.5) * DepthCm;
			}
		}
		const double DepthSum = Measure.VolumeCm3 / CellAreaCm2;
		Measure.CenterOfMassCells = DepthSum > 0.0 ? WeightedCells / DepthSum : FVector2D::ZeroVector;
		return Measure;
	}

	double ComputeWetIntersectionOverUnion(const FTDDenseParityField& First, const FTDDenseParityField& Second, float WetThresholdCm)
	{
		int32 IntersectionCount = 0;
		int32 UnionCount = 0;
		for (int32 CellY = 0; CellY < ParityResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < ParityResolution; ++CellX)
			{
				const bool bIsFirstWet = SampleFieldAtReferenceCell(First, CellX, CellY) > WetThresholdCm;
				const bool bIsSecondWet = SampleFieldAtReferenceCell(Second, CellX, CellY) > WetThresholdCm;
				IntersectionCount += bIsFirstWet && bIsSecondWet ? 1 : 0;
				UnionCount += bIsFirstWet || bIsSecondWet ? 1 : 0;
			}
		}
		return UnionCount > 0 ? static_cast<double>(IntersectionCount) / UnionCount : 0.0;
	}

	bool RunCpuParity(FAutomationTestBase& Test, FTDDenseParityField& OutField)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(MakeParityConfig(), FVector2D::ZeroVector);
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Solver.SetTerrainElevationAtCell(CellX, CellY, ComputeValleyElevationCm(Solver.CellToWorldCenter(CellX, CellY)));
			}
		}
		Solver.ComputeTerrainSlopeFromElevation();
		if (!Test.TestTrue(TEXT("The CPU solver runs dense water"), Solver.IsDenseWaterActive()))
		{
			return false;
		}

		Solver.SetDenseWaterSeed(MakeParitySeed());
		for (int32 StepIndex = 0; StepIndex < ParityStepCount; ++StepIndex)
		{
			Solver.StepSimulation(ParityStepSeconds);
		}

		TArray<FFloat16Color> Pixels;
		Solver.PackWaterSurfacePixels(Pixels, 0.0f);
		DecodeDepthPixels(Pixels, Resolution, OutField);
		return Test.TestEqual(TEXT("CPU water surface pixel count"), Pixels.Num(), Resolution * Resolution);
	}

	UTextureRenderTarget2D* MakeOutputTarget(int32 Resolution)
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

	struct FTDDenseParityOutputs
	{
		FTextureRenderTargetResource* DensityPressure = nullptr;
		FTextureRenderTargetResource* Velocity = nullptr;
		FTextureRenderTargetResource* WaterSurface = nullptr;
	};

	FTDFluidGpuActorInput MakeParityActor(const FTDFluidGpuSimulationSettings& Settings, int32 FrameIndex, const TArray<FVector4f>& SeedTexels, const FTDDenseParityOutputs& Outputs)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ParityActorKey;
		Actor.Settings = Settings;
		Actor.DeltaTimeSeconds = ParityStepSeconds;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(ParityStepSeconds);
		Actor.bHasObstacleListChanged = FrameIndex == 0;
		Actor.DensityPressureOutput = Outputs.DensityPressure;
		Actor.VelocityOutput = Outputs.Velocity;
		Actor.WaterSurfaceOutput = Outputs.WaterSurface;
		if (FrameIndex != 0)
		{
			return Actor;
		}

		const int32 ResolutionTier = FTDFluidGpuBatch::ResolveWaterResolutionTier(Settings.Resolution);
		const int32 FieldResolution = ResolutionTier / FTDFluidGpuBatch::ComputeWaterFieldDivisor(ResolutionTier);
		Actor.WaterTerrainUpdates.Reserve(FieldResolution * FieldResolution);
		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				FTDFluidGpuTerrainCellUpdate& TerrainUpdate = Actor.WaterTerrainUpdates.AddDefaulted_GetRef();
				TerrainUpdate.Cell = FIntPoint(FieldX, FieldY);
				TerrainUpdate.ElevationCm = ComputeValleyElevationCm(ComputeCellCenterPlane(FieldX, FieldY, FieldResolution));
			}
		}
		Actor.DenseSeedResolution = ParityResolution;
		Actor.DenseSeedTexels = SeedTexels;
		return Actor;
	}

	bool RunGpuParity(FAutomationTestBase& Test, FTDDenseParityField& OutField)
	{
		const int32 ResolutionTier = FTDFluidGpuBatch::ResolveWaterResolutionTier(ParityResolution);
		FTDFluidGpuSimulationSettings Settings = FTDFluidGpuSolverProxy::MakeSimulationSettings(MakeParityConfig(), ResolutionTier);
		FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Settings);
		if (!Test.TestTrue(TEXT("The GPU settings run dense water"), Settings.bUsesDenseWater))
		{
			return false;
		}

		TStrongObjectPtr<UTextureRenderTarget2D> DensityPressureTarget(MakeOutputTarget(ResolutionTier));
		TStrongObjectPtr<UTextureRenderTarget2D> VelocityTarget(MakeOutputTarget(ResolutionTier));
		TStrongObjectPtr<UTextureRenderTarget2D> WaterSurfaceTarget(MakeOutputTarget(ResolutionTier));
		FTDDenseParityOutputs Outputs;
		Outputs.DensityPressure = DensityPressureTarget->GameThread_GetRenderTargetResource();
		Outputs.Velocity = VelocityTarget->GameThread_GetRenderTargetResource();
		Outputs.WaterSurface = WaterSurfaceTarget->GameThread_GetRenderTargetResource();
		if (!Test.TestTrue(TEXT("GPU output targets exist"), Outputs.DensityPressure && Outputs.Velocity && Outputs.WaterSurface))
		{
			return false;
		}

		const FTDFluidDenseWaterSeed Seed = MakeParitySeed();
		TArray<FVector4f> SeedTexels;
		SeedTexels.SetNumUninitialized(Seed.DepthCm.Num());
		for (int32 SeedIndex = 0; SeedIndex < Seed.DepthCm.Num(); ++SeedIndex)
		{
			SeedTexels[SeedIndex] = FVector4f(Seed.DepthCm[SeedIndex], Seed.FaceVelocityXCmPerSec[SeedIndex], Seed.FaceVelocityYCmPerSec[SeedIndex], 0.0f);
		}

		ENQUEUE_RENDER_COMMAND(TDFluidDenseWaterParityFrames)(
			[Settings, SeedTexels, Outputs](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				for (int32 FrameIndex = 0; FrameIndex < ParityStepCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					FrameInputs.Actors.Add(MakeParityActor(Settings, FrameIndex, SeedTexels, Outputs));
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidDenseWaterParityFrame"));
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
						GraphBuilder.Execute();
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				Batches.Empty();
			});
		FlushRenderingCommands();

		TArray<FFloat16Color> Pixels;
		if (!Test.TestTrue(TEXT("GPU water surface pixels read"), Outputs.WaterSurface->ReadFloat16Pixels(Pixels)))
		{
			return false;
		}

		DecodeDepthPixels(Pixels, ResolutionTier, OutField);
		return Test.TestEqual(TEXT("GPU water surface pixel count"), Pixels.Num(), ResolutionTier * ResolutionTier);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDenseWaterCpuGpuParity192Test,
	"TDGame.FluidFXRhi.DenseWater.CpuGpuParity192",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidDenseWaterCpuGpuParity192Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidDenseWaterParityRhiTestPrivate;

	if (GUsingNullRHI || !FApp::CanEverRender())
	{
		AddError(TEXT("CpuGpuParity192 needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	FTDDenseParityField CpuField;
	FTDDenseParityField GpuField;
	if (!RunCpuParity(*this, CpuField) || !RunGpuParity(*this, GpuField))
	{
		return false;
	}

	const float WetThresholdCm = FTDFluidSimulationConfig().DenseWaterWetThresholdCm;
	const FTDDenseParityMeasure CpuMeasure = MeasureField(CpuField, WetThresholdCm);
	const FTDDenseParityMeasure GpuMeasure = MeasureField(GpuField, WetThresholdCm);
	const double WetIntersectionOverUnion = ComputeWetIntersectionOverUnion(CpuField, GpuField, WetThresholdCm);
	const double VolumeRatioDifference = CpuMeasure.VolumeCm3 > 0.0 ? FMath::Abs(GpuMeasure.VolumeCm3 - CpuMeasure.VolumeCm3) / CpuMeasure.VolumeCm3 : 1.0;
	const double CenterOfMassDistanceCells = FVector2D::Distance(CpuMeasure.CenterOfMassCells, GpuMeasure.CenterOfMassCells);
	AddInfo(FString::Printf(TEXT("CpuGpuParity192: wet IoU %.4f, CPU wet %d / GPU wet %d, volume CPU %.1f / GPU %.1f cm3 (difference %.4f), center of mass CPU (%.2f, %.2f) / GPU (%.2f, %.2f) cells, distance %.3f"),
		WetIntersectionOverUnion, CpuMeasure.WetCellCount, GpuMeasure.WetCellCount, CpuMeasure.VolumeCm3, GpuMeasure.VolumeCm3, VolumeRatioDifference,
		CpuMeasure.CenterOfMassCells.X, CpuMeasure.CenterOfMassCells.Y, GpuMeasure.CenterOfMassCells.X, GpuMeasure.CenterOfMassCells.Y, CenterOfMassDistanceCells));

	TestTrue(TEXT("The CPU valley holds water"), CpuMeasure.WetCellCount > 0);
	TestTrue(TEXT("Wet cells overlap between CPU and GPU"), WetIntersectionOverUnion >= MinimumWetIntersectionOverUnion);
	TestTrue(TEXT("CPU and GPU water volumes agree"), VolumeRatioDifference <= MaximumVolumeRatioDifference);
	TestTrue(TEXT("CPU and GPU centers of mass agree"), CenterOfMassDistanceCells <= MaximumCenterOfMassCells);
	return true;
}

#endif
