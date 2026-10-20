#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/PointLight.h"
#include "FluidFX/TDFluidVolumeTransmittance.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "TDFluidGpuBackendSelection.h"
#include "TDFluidGpuInputs.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuBackendTests, Log, All);

namespace TDFluidGpuBackendTestPrivate
{
	constexpr float BackendTestStepSeconds = 1.0f / 60.0f;
	constexpr int32 ParityStepCount = 120;
	constexpr int32 ParityDrainStepLimit = 8;
	constexpr int32 DensityHistogramBinCount = 16;
	constexpr double DensityParityTolerance = 0.03;
	constexpr double KineticEnergyParityTolerance = 0.10;
	constexpr double DivergenceParityRatio = 1.2;
	constexpr double HalfFloatStepBelowOne = 1.0 / 2048.0;
	constexpr double HistogramParityTolerance = 0.05;
	constexpr int32 MaximumReadbackLatencyFrames = 3;
	constexpr int32 LatencyTraceStepCount = 24;
	constexpr int32 PainterWalkStepCount = 60;
	constexpr int32 PainterIdleStepCount = 30;
	constexpr int32 PainterEraseStepCount = 20;
	constexpr double PainterPathHalfSpanXCm = 1400.0;
	constexpr double PainterPathHalfSpanYCm = 400.0;
	constexpr double ObstacleDeepInsideMarginFieldCells = 2.0;
	constexpr double ObstacleBandWidthFieldCells = 3.0;
	constexpr double WindowCenterMatchToleranceCm = 0.01;
	const TCHAR* const SolverBackendVariableName = TEXT("TD.FluidFX.SolverBackend");
	const TCHAR* const GpuPressureSolverVariableName = TEXT("TD.FluidFX.GpuPressureSolver");
	const TCHAR* const GpuPressureCyclesVariableName = TEXT("TD.FluidFX.GpuPressureCycles");
	constexpr int32 AutomaticPressureSolverMode = 0;
	constexpr int32 ForcedJacobiPressureSolverMode = 1;
	constexpr double RealisticEncodingFloorFactor = 0.790569415042;
	const FVector ParityAnchorLocation(310.0, -145.0, 0.0);

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	bool CanRunRhiParityTest(FAutomationTestBase& Test, const TCHAR* RhiTestLabel)
	{
		const bool bHasRenderingRhi = HasRenderingRhi();
		UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), RhiTestLabel, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), RhiTestLabel));
		return false;
	}

	struct FTDFluidSolverBackendVariableScope
	{
		explicit FTDFluidSolverBackendVariableScope(int32 OverrideValue)
			: Variable(IConsoleManager::Get().FindConsoleVariable(SolverBackendVariableName))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetInt();
			Variable->Set(OverrideValue, ECVF_SetByCode);
		}

		~FTDFluidSolverBackendVariableScope()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		void SetValue(int32 NewValue)
		{
			if (Variable)
			{
				Variable->Set(NewValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
	};

	struct FTDFluidGpuPressureVariableScope
	{
		FTDFluidGpuPressureVariableScope(int32 SolverMode, int32 CycleCount)
			: SolverVariable(IConsoleManager::Get().FindConsoleVariable(GpuPressureSolverVariableName))
			, CycleVariable(IConsoleManager::Get().FindConsoleVariable(GpuPressureCyclesVariableName))
		{
			if (!SolverVariable || !CycleVariable)
			{
				return;
			}

			PreviousSolverMode = SolverVariable->GetInt();
			PreviousCycleCount = CycleVariable->GetInt();
			SolverVariable->Set(SolverMode, ECVF_SetByCode);
			CycleVariable->Set(CycleCount, ECVF_SetByCode);
			bIsApplied = SolverVariable->GetInt() == SolverMode && CycleVariable->GetInt() == CycleCount;
		}

		~FTDFluidGpuPressureVariableScope()
		{
			if (!SolverVariable || !CycleVariable)
			{
				return;
			}

			SolverVariable->Set(PreviousSolverMode, ECVF_SetByCode);
			CycleVariable->Set(PreviousCycleCount, ECVF_SetByCode);
		}

		bool IsApplied() const
		{
			return bIsApplied;
		}

		IConsoleVariable* SolverVariable = nullptr;
		IConsoleVariable* CycleVariable = nullptr;
		int32 PreviousSolverMode = 0;
		int32 PreviousCycleCount = 0;
		bool bIsApplied = false;
	};

	struct FTDFluidDivergenceRunPlan
	{
		const TCHAR* SolverName = TEXT("");
		int32 PressureSolverMode = 0;
		int32 MultigridCycles = 0;
		TArray<int32> PressureIterationCounts;
		bool bIsOracle = false;
	};

	struct FTDFluidGpuBackendTestWorld
	{
		FTDFluidGpuBackendTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidGpuBackendWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidGpuBackendTestWorld()
		{
			if (!World)
			{
				return;
			}

			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}

		UTDFluidLiveSubsystem* GetFluidSubsystem() const
		{
			return World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		}

		void StepWorld(float DeltaSeconds, bool bShouldWaitForGpu)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			if (bShouldWaitForGpu)
			{
				ENQUEUE_RENDER_COMMAND(TDFluidGpuBackendTestWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.SubmitAndBlockUntilGPUIdle();
				});
			}
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	FTDFluidSimulationConfig MakeSmokeParityConfig(int32 RequestedResolution, ETDFluidSolverBackend Backend)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
		Config.GridResolution = RequestedResolution;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		Config.DensityDissipation = 0.1f;
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = false;
		Config.SolverBackend = Backend;
		return Config;
	}

	FTDFluidSimulationConfig MakePainterParityConfig(ETDFluidSolverBackend Backend)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FootprintPainter);
		Config.GridResolution = 128;
		Config.DensityDissipation = 0.5f;
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = false;
		Config.SolverBackend = Backend;
		return Config;
	}

	void AppendParityStamps(int32 StepIndex, const FVector& AnchorLocation, double ExtentCm, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		constexpr ETDFluidBrushChannel EmitterChannels[] = { ETDFluidBrushChannel::ChannelR_Mud, ETDFluidBrushChannel::ChannelB_Fire, ETDFluidBrushChannel::ChannelG_Blood };
		constexpr int32 EmitterCount = UE_ARRAY_COUNT(EmitterChannels);
		const double OrbitRadiusCm = ExtentCm * 0.25;
		for (int32 EmitterIndex = 0; EmitterIndex < EmitterCount; ++EmitterIndex)
		{
			const double Angle = StepIndex * 0.045 + EmitterIndex * (UE_DOUBLE_TWO_PI / EmitterCount);
			FTDFluidBrushStamp EmitterStamp;
			EmitterStamp.WorldLocation = AnchorLocation + FVector(FMath::Cos(Angle) * OrbitRadiusCm, FMath::Sin(Angle) * OrbitRadiusCm, 0.0);
			EmitterStamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 260.0;
			EmitterStamp.RadiusCm = static_cast<float>(ExtentCm * 0.06);
			EmitterStamp.DensityStrength = 0.9f;
			EmitterStamp.VelocityStrength = 1.0f;
			EmitterStamp.Channel = EmitterChannels[EmitterIndex];
			OutStamps.Add(EmitterStamp);
		}

		FTDFluidBrushStamp JetStamp;
		JetStamp.WorldLocation = AnchorLocation + FVector(-ExtentCm * 0.3, ExtentCm * 0.05 * FMath::Sin(StepIndex * 0.1), 0.0);
		JetStamp.WorldVelocity = FVector(420.0, 40.0, 0.0);
		JetStamp.RadiusCm = static_cast<float>(ExtentCm * 0.05);
		JetStamp.DensityStrength = 0.8f;
		JetStamp.VelocityStrength = 1.2f;
		JetStamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		OutStamps.Add(JetStamp);
	}

	FVector ResolvePainterPathStart(const FVector& AnchorLocation)
	{
		return AnchorLocation - FVector(PainterPathHalfSpanXCm, PainterPathHalfSpanYCm, 0.0);
	}

	FVector ResolvePainterPathDelta()
	{
		return FVector(2.0 * PainterPathHalfSpanXCm, 2.0 * PainterPathHalfSpanYCm, 0.0);
	}

	void AppendPainterStamps(int32 StepIndex, const FVector& AnchorLocation, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		const FVector PathStart = ResolvePainterPathStart(AnchorLocation);
		const FVector PathDelta = ResolvePainterPathDelta();
		if (StepIndex < PainterWalkStepCount)
		{
			const double WalkAlpha = static_cast<double>(StepIndex) / static_cast<double>(PainterWalkStepCount - 1);
			FTDFluidBrushStamp FootStamp;
			FootStamp.WorldLocation = PathStart + PathDelta * WalkAlpha;
			FootStamp.WorldVelocity = PathDelta / (static_cast<double>(PainterWalkStepCount - 1) * BackendTestStepSeconds);
			FootStamp.RadiusCm = 70.0f;
			FootStamp.DensityStrength = 0.8f;
			FootStamp.VelocityStrength = 1.0f;
			FootStamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
			OutStamps.Add(FootStamp);
			return;
		}

		const int32 EraseStepIndex = StepIndex - PainterWalkStepCount - PainterIdleStepCount;
		if (EraseStepIndex < 0 || EraseStepIndex >= PainterEraseStepCount)
		{
			return;
		}

		const double EraseAlpha = 0.4 + 0.2 * static_cast<double>(EraseStepIndex) / static_cast<double>(PainterEraseStepCount - 1);
		FTDFluidBrushStamp EraseStamp;
		EraseStamp.WorldLocation = PathStart + PathDelta * EraseAlpha;
		EraseStamp.RadiusCm = 150.0f;
		EraseStamp.DensityStrength = -1.0f;
		EraseStamp.VelocityStrength = 0.0f;
		EraseStamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		OutStamps.Add(EraseStamp);
	}

	TArray<FTDFluidSdfObstacle> MakeParityObstacles(const FVector& AnchorLocation)
	{
		TArray<FTDFluidSdfObstacle> Obstacles;

		FTDFluidSdfObstacle Pillar;
		Pillar.WorldCenter = AnchorLocation + FVector(110.0, 20.0, 0.0);
		Pillar.RadiusCm = 70.0f;
		Obstacles.Add(Pillar);

		FTDFluidSdfObstacle Crate;
		Crate.WorldCenter = AnchorLocation + FVector(-60.0, -150.0, 0.0);
		Crate.BoxHalfExtentsCm = FVector2D(90.0, 40.0);
		Crate.bIsBox = true;
		Obstacles.Add(Crate);

		return Obstacles;
	}

	double EvaluateObstacleDistanceCm(const FTDFluidSdfObstacle& Obstacle, const FVector2D& PlanePoint)
	{
		const FVector2D Offset = PlanePoint - FVector2D(Obstacle.WorldCenter.X, Obstacle.WorldCenter.Y);
		if (!Obstacle.bIsBox)
		{
			return Offset.Size() - Obstacle.RadiusCm;
		}

		const FVector2D OutsideDistance(FMath::Abs(Offset.X) - Obstacle.BoxHalfExtentsCm.X, FMath::Abs(Offset.Y) - Obstacle.BoxHalfExtentsCm.Y);
		const FVector2D PositiveDistance(FMath::Max(OutsideDistance.X, 0.0), FMath::Max(OutsideDistance.Y, 0.0));
		return PositiveDistance.Size() + FMath::Min(FMath::Max(OutsideDistance.X, OutsideDistance.Y), 0.0);
	}

	ATDFluidLiveActor* SpawnConfiguredLiveActor(UWorld& TargetWorld, const FVector& Location, const FTDFluidSimulationConfig& Config)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		ATDFluidLiveActor* LiveActor = TargetWorld.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}

		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);
		return LiveActor;
	}

	AActor* SpawnAnchorActor(UWorld& TargetWorld, const FVector& Location)
	{
		AActor* AnchorActor = TargetWorld.SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!AnchorActor)
		{
			return nullptr;
		}

		USceneComponent* AnchorRoot = NewObject<USceneComponent>(AnchorActor);
		AnchorActor->AddInstanceComponent(AnchorRoot);
		AnchorActor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();
		AnchorActor->SetActorLocation(Location);
		return AnchorActor;
	}

	void QueueStampsOnActor(const ATDFluidLiveActor& LiveActor, TConstArrayView<FTDFluidBrushStamp> Stamps)
	{
		for (const FTDFluidBrushStamp& Stamp : Stamps)
		{
			LiveActor.LiveComponent->QueueBrushStamp(Stamp);
		}
	}

	uint32 ComputeOutputPixelChecksum(const UTDFluidLiveComponent& Component)
	{
		const TArray<FFloat16Color>& DensityPressurePixels = Component.GetPackedDensityPressurePixels();
		const TArray<FFloat16Color>& VelocityPixels = Component.GetPackedVelocityPixels();
		const uint32 DensityChecksum = FCrc::MemCrc32(DensityPressurePixels.GetData(), DensityPressurePixels.Num() * sizeof(FFloat16Color));
		return FCrc::MemCrc32(VelocityPixels.GetData(), VelocityPixels.Num() * sizeof(FFloat16Color), DensityChecksum);
	}

	struct FTDFluidGpuOutputTargetInfo
	{
		bool bHasDensityRenderTarget = false;
		bool bHasVelocityRenderTarget = false;
		FIntPoint DensitySize = FIntPoint::ZeroValue;
		FIntPoint VelocitySize = FIntPoint::ZeroValue;
		EPixelFormat DensityFormat = PF_Unknown;
		EPixelFormat VelocityFormat = PF_Unknown;
	};

	FTDFluidGpuOutputTargetInfo DescribeGpuOutputTargets(const UTDFluidLiveComponent& Component)
	{
		FTDFluidGpuOutputTargetInfo Info;
		if (const UTextureRenderTarget2D* DensityTarget = Cast<UTextureRenderTarget2D>(Component.GetDensityPressureTexture()))
		{
			Info.bHasDensityRenderTarget = true;
			Info.DensitySize = FIntPoint(DensityTarget->SizeX, DensityTarget->SizeY);
			Info.DensityFormat = DensityTarget->GetFormat();
		}
		if (const UTextureRenderTarget2D* VelocityTarget = Cast<UTextureRenderTarget2D>(Component.GetVelocityTexture()))
		{
			Info.bHasVelocityRenderTarget = true;
			Info.VelocitySize = FIntPoint(VelocityTarget->SizeX, VelocityTarget->SizeY);
			Info.VelocityFormat = VelocityTarget->GetFormat();
		}
		return Info;
	}

	void ExpectGpuOutputTargets(FAutomationTestBase& Test, const FString& Label, const FTDFluidGpuOutputTargetInfo& Info, int32 ExpectedResolution, int32 ExpectedVelocitySize)
	{
		Test.TestTrue(*(Label + TEXT(" density-pressure output is a render target")), Info.bHasDensityRenderTarget);
		Test.TestTrue(*(Label + TEXT(" velocity output is a render target")), Info.bHasVelocityRenderTarget);
		Test.TestEqual(*(Label + TEXT(" density-pressure target width is the GPU tier")), static_cast<int32>(Info.DensitySize.X), static_cast<int32>(ExpectedResolution));
		Test.TestEqual(*(Label + TEXT(" density-pressure target height is the GPU tier")), static_cast<int32>(Info.DensitySize.Y), static_cast<int32>(ExpectedResolution));
		Test.TestEqual(*(Label + TEXT(" velocity target width")), static_cast<int32>(Info.VelocitySize.X), static_cast<int32>(ExpectedVelocitySize));
		Test.TestTrue(*(Label + TEXT(" density-pressure target is half float RGBA")), Info.DensityFormat == PF_FloatRGBA);
		Test.TestTrue(*(Label + TEXT(" velocity target is half float RGBA")), Info.VelocityFormat == PF_FloatRGBA);
	}

	bool ReadOutputRenderTargetPixels(UTexture* OutputTexture, TArray<FFloat16Color>& OutPixels)
	{
		OutPixels.Reset();
		UTextureRenderTarget2D* OutputTarget = Cast<UTextureRenderTarget2D>(OutputTexture);
		FTextureRenderTargetResource* TargetResource = OutputTarget ? OutputTarget->GameThread_GetRenderTargetResource() : nullptr;
		if (!TargetResource)
		{
			return false;
		}

		FRenderTarget* ReadableTarget = TargetResource;
		return ReadableTarget->ReadFloat16Pixels(OutPixels) && !OutPixels.IsEmpty();
	}

	bool ReadGpuDensityPressurePixels(const UTDFluidLiveComponent& Component, TArray<FFloat16Color>& OutPixels)
	{
		return ReadOutputRenderTargetPixels(Component.GetDensityPressureTexture(), OutPixels);
	}

	bool ReadGpuVelocityPixels(const UTDFluidLiveComponent& Component, TArray<FFloat16Color>& OutPixels)
	{
		return ReadOutputRenderTargetPixels(Component.GetVelocityTexture(), OutPixels);
	}

	struct FTDFluidVelocityGrid
	{
		int32 Resolution = 0;
		TArray<FVector2D> VelocitiesCmPerSec;
		TArray<bool> SaturatedX;
		TArray<bool> SaturatedY;
	};

	struct FTDFluidSimulationDivergence
	{
		double DivergenceL2 = 0.0;
		int32 InteriorCellCount = 0;
		int32 SampledCellCount = 0;

		double ComputeExcludedFraction() const
		{
			return InteriorCellCount > 0 ? 1.0 - static_cast<double>(SampledCellCount) / static_cast<double>(InteriorCellCount) : 1.0;
		}
	};

	bool IsPackedVelocityComponentSaturated(float EncodedComponent)
	{
		return EncodedComponent <= 0.0f || EncodedComponent >= 1.0f;
	}

	double DecodePackedVelocityComponentCmPerSec(float EncodedComponent)
	{
		return (static_cast<double>(EncodedComponent) * 2.0 - 1.0) * static_cast<double>(FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec());
	}

	FTDFluidVelocityGrid DecodePackedVelocityGrid(TConstArrayView<FFloat16Color> VelocityPixels, int32 GridResolution)
	{
		FTDFluidVelocityGrid Grid;
		if (GridResolution <= 0 || VelocityPixels.Num() != GridResolution * GridResolution)
		{
			return Grid;
		}

		Grid.Resolution = GridResolution;
		Grid.VelocitiesCmPerSec.SetNumUninitialized(VelocityPixels.Num());
		Grid.SaturatedX.SetNumUninitialized(VelocityPixels.Num());
		Grid.SaturatedY.SetNumUninitialized(VelocityPixels.Num());
		for (int32 CellIndex = 0; CellIndex < VelocityPixels.Num(); ++CellIndex)
		{
			const float EncodedX = VelocityPixels[CellIndex].R.GetFloat();
			const float EncodedY = VelocityPixels[CellIndex].G.GetFloat();
			Grid.VelocitiesCmPerSec[CellIndex] = FVector2D(DecodePackedVelocityComponentCmPerSec(EncodedX), DecodePackedVelocityComponentCmPerSec(EncodedY));
			Grid.SaturatedX[CellIndex] = IsPackedVelocityComponentSaturated(EncodedX);
			Grid.SaturatedY[CellIndex] = IsPackedVelocityComponentSaturated(EncodedY);
		}
		return Grid;
	}

	FTDFluidVelocityGrid ReadSolverVelocityGrid(const FTDFluidGrid2DSolver& Solver)
	{
		FTDFluidVelocityGrid Grid;
		const int32 GridResolution = Solver.GetResolution();
		if (GridResolution <= 0)
		{
			return Grid;
		}

		const int32 CellCount = GridResolution * GridResolution;
		Grid.Resolution = GridResolution;
		Grid.VelocitiesCmPerSec.SetNumUninitialized(CellCount);
		Grid.SaturatedX.SetNumZeroed(CellCount);
		Grid.SaturatedY.SetNumZeroed(CellCount);
		for (int32 CellY = 0; CellY < GridResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < GridResolution; ++CellX)
			{
				Grid.VelocitiesCmPerSec[CellY * GridResolution + CellX] = Solver.SampleAtCell(CellX, CellY).VelocityCmPerSec;
			}
		}
		return Grid;
	}

	FTDFluidSimulationDivergence ComputeSimulationDivergence(const FTDFluidVelocityGrid& Grid, const FVector2D& SimulationCellSizeCm)
	{
		FTDFluidSimulationDivergence Divergence;
		const int32 GridResolution = Grid.Resolution;
		if (GridResolution < 3 || Grid.VelocitiesCmPerSec.Num() != GridResolution * GridResolution)
		{
			return Divergence;
		}

		double DivergenceSquaredSum = 0.0;
		for (int32 CellY = 1; CellY < GridResolution - 1; ++CellY)
		{
			for (int32 CellX = 1; CellX < GridResolution - 1; ++CellX)
			{
				++Divergence.InteriorCellCount;
				const int32 CenterIndex = CellY * GridResolution + CellX;
				const int32 RightIndex = CenterIndex + 1;
				const int32 LeftIndex = CenterIndex - 1;
				const int32 UpIndex = CenterIndex + GridResolution;
				const int32 DownIndex = CenterIndex - GridResolution;
				if (Grid.SaturatedX[RightIndex] || Grid.SaturatedX[LeftIndex] || Grid.SaturatedY[UpIndex] || Grid.SaturatedY[DownIndex])
				{
					continue;
				}

				const double DerivativeX = (Grid.VelocitiesCmPerSec[RightIndex].X - Grid.VelocitiesCmPerSec[LeftIndex].X) / (2.0 * SimulationCellSizeCm.X);
				const double DerivativeY = (Grid.VelocitiesCmPerSec[UpIndex].Y - Grid.VelocitiesCmPerSec[DownIndex].Y) / (2.0 * SimulationCellSizeCm.Y);
				const double CellDivergence = DerivativeX + DerivativeY;
				DivergenceSquaredSum += CellDivergence * CellDivergence;
				++Divergence.SampledCellCount;
			}
		}

		Divergence.DivergenceL2 = Divergence.SampledCellCount > 0 ? FMath::Sqrt(DivergenceSquaredSum / Divergence.SampledCellCount) : 0.0;
		return Divergence;
	}

	double ComputePackedVelocityDivergenceFloorBound(const FVector2D& SimulationCellSizeCm)
	{
		const double LargestStepCmPerSec = HalfFloatStepBelowOne * 2.0 * static_cast<double>(FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec());
		const double ComponentVariance = FMath::Square(LargestStepCmPerSec) / 12.0;
		const double DerivativeXVariance = 2.0 * ComponentVariance / FMath::Square(2.0 * SimulationCellSizeCm.X);
		const double DerivativeYVariance = 2.0 * ComponentVariance / FMath::Square(2.0 * SimulationCellSizeCm.Y);
		return FMath::Sqrt(DerivativeXVariance + DerivativeYVariance);
	}

	struct FTDFluidFieldStatistics
	{
		double TotalDensity = 0.0;
		double TotalSelectiveDensity = 0.0;
		double KineticEnergy = 0.0;
		double DivergenceL2 = 0.0;
		FVector2D DensityCentroid = FVector2D::ZeroVector;
		bool bHasDensityCentroid = false;
		TArray<double> DensityHistogram;
	};

	double ComputeDivergenceL2(const FTDFluidReadbackField& Field)
	{
		const int32 FieldResolution = FTDFluidReadbackField::GetResolution();
		const FVector2D FieldCellSizeCm = Field.ExtentCm / static_cast<double>(FieldResolution);
		double DivergenceSquaredSum = 0.0;
		int32 DivergenceSampleCount = 0;
		for (int32 FieldY = 1; FieldY < FieldResolution - 1; ++FieldY)
		{
			for (int32 FieldX = 1; FieldX < FieldResolution - 1; ++FieldX)
			{
				const int32 CenterIndex = FieldY * FieldResolution + FieldX;
				const double DerivativeX = (static_cast<double>(Field.VelocityCmPerSec[CenterIndex + 1].X) - static_cast<double>(Field.VelocityCmPerSec[CenterIndex - 1].X)) / (2.0 * FieldCellSizeCm.X);
				const double DerivativeY = (static_cast<double>(Field.VelocityCmPerSec[CenterIndex + FieldResolution].Y) - static_cast<double>(Field.VelocityCmPerSec[CenterIndex - FieldResolution].Y)) / (2.0 * FieldCellSizeCm.Y);
				const double CellDivergence = DerivativeX + DerivativeY;
				DivergenceSquaredSum += CellDivergence * CellDivergence;
				++DivergenceSampleCount;
			}
		}
		return DivergenceSampleCount > 0 ? FMath::Sqrt(DivergenceSquaredSum / DivergenceSampleCount) : 0.0;
	}

	FTDFluidFieldStatistics ComputeFieldStatistics(const FTDFluidReadbackField& Field)
	{
		FTDFluidFieldStatistics Statistics;
		Statistics.DensityHistogram.SetNumZeroed(DensityHistogramBinCount);
		if (!Field.IsValid())
		{
			return Statistics;
		}

		const int32 FieldResolution = FTDFluidReadbackField::GetResolution();
		const int32 FieldCellCount = FieldResolution * FieldResolution;
		const FVector2D FieldCellSizeCm = Field.ExtentCm / static_cast<double>(FieldResolution);
		const double FieldCellAreaCm2 = FieldCellSizeCm.X * FieldCellSizeCm.Y;
		for (int32 FieldIndex = 0; FieldIndex < FieldCellCount; ++FieldIndex)
		{
			const double PrimaryDensity = Field.PrimaryDensity[FieldIndex];
			Statistics.TotalDensity += PrimaryDensity * FieldCellAreaCm2;
			Statistics.TotalSelectiveDensity += static_cast<double>(Field.SelectiveDensity[FieldIndex]) * FieldCellAreaCm2;
			Statistics.KineticEnergy += 0.5 * static_cast<double>(Field.VelocityCmPerSec[FieldIndex].SizeSquared()) * FieldCellAreaCm2;
			const int32 BinIndex = FMath::Clamp(FMath::FloorToInt32(FMath::Clamp(PrimaryDensity, 0.0, 1.0) * DensityHistogramBinCount), 0, DensityHistogramBinCount - 1);
			Statistics.DensityHistogram[BinIndex] += 1.0 / FieldCellCount;
		}

		Statistics.DivergenceL2 = ComputeDivergenceL2(Field);
		float CentroidAreaCm2 = 0.0f;
		Statistics.bHasDensityCentroid = Field.ComputeDensityCentroid(false, Statistics.DensityCentroid, CentroidAreaCm2);
		return Statistics;
	}

	double ComputeRelativeDifference(double CpuValue, double GpuValue)
	{
		return FMath::Abs(GpuValue - CpuValue) / FMath::Max(FMath::Abs(CpuValue), UE_DOUBLE_SMALL_NUMBER);
	}

	double ComputeHistogramL1Distance(const FTDFluidFieldStatistics& CpuStatistics, const FTDFluidFieldStatistics& GpuStatistics)
	{
		double Distance = 0.0;
		for (int32 BinIndex = 0; BinIndex < DensityHistogramBinCount; ++BinIndex)
		{
			Distance += FMath::Abs(GpuStatistics.DensityHistogram[BinIndex] - CpuStatistics.DensityHistogram[BinIndex]);
		}
		return Distance;
	}

	FVector2D ComputeFieldCellCenter(const FTDFluidReadbackField& Field, int32 FieldX, int32 FieldY)
	{
		const FVector2D FieldCellSizeCm = Field.ExtentCm / static_cast<double>(FTDFluidReadbackField::GetResolution());
		const FVector2D MinPlane = Field.WindowCenterPlaneCm - 0.5 * Field.ExtentCm;
		return MinPlane + FVector2D((FieldX + 0.5) * FieldCellSizeCm.X, (FieldY + 0.5) * FieldCellSizeCm.Y);
	}

	struct FTDFluidParityCapture
	{
		int32 TargetFrame = 0;
		bool bIsCaptured = false;
		uint32 CapturedFrame = 0;
		FTDFluidReadbackField CpuField;
		FTDFluidReadbackField GpuField;
	};

	struct FTDFluidParityRunSettings
	{
		FTDFluidSimulationConfig CpuConfig;
		TArray<FTDFluidSimulationConfig> GpuConfigs;
		TArray<FTDFluidSdfObstacle> Obstacles;
		TArray<int32> CaptureFrames;
		TFunction<FVector(int32)> ResolveAnchorLocation;
		TFunction<void(int32, const FVector&, TArray<FTDFluidBrushStamp>&)> AppendStamps;
		bool bFollowsAnchor = false;
		bool bCapturesFinalOutputPixels = false;
		bool bCapturesFinalVelocityGrids = false;
		TFunction<void(ATDFluidLiveActor&)> ConfigureActor;
		TFunction<void(const ATDFluidLiveActor&, const TArray<ATDFluidLiveActor*>&)> InspectFinalActors;
	};

	struct FTDFluidParityRunResult
	{
		TArray<int32> GpuResolutions;
		TArray<FTDFluidGpuOutputTargetInfo> GpuOutputTargets;
		TArray<TArray<FTDFluidParityCapture>> CapturesPerGpuActor;
		FVector2D InitialCpuCenterWorld = FVector2D::ZeroVector;
		FVector2D FinalCpuCenterWorld = FVector2D::ZeroVector;
		double MaximumCenterMismatchCm = 0.0;
		int32 ExecutedStepCount = 0;
		TArray<FFloat16Color> FinalCpuDensityPressurePixels;
		TArray<TArray<FFloat16Color>> FinalGpuDensityPressurePixels;
		FTDFluidVelocityGrid FinalCpuPackedVelocityGrid;
		FTDFluidVelocityGrid FinalCpuSolverVelocityGrid;
		TArray<FTDFluidVelocityGrid> FinalGpuPackedVelocityGrids;
	};

	void TryCaptureParityFrames(const FTDFluidReadbackField& GpuField, const TMap<uint32, FTDFluidReadbackField>& CpuSnapshots, TArray<FTDFluidParityCapture>& InOutCaptures)
	{
		if (!GpuField.IsValid())
		{
			return;
		}

		for (FTDFluidParityCapture& Capture : InOutCaptures)
		{
			if (Capture.bIsCaptured || GpuField.FrameNumber < static_cast<uint32>(Capture.TargetFrame))
			{
				continue;
			}

			const FTDFluidReadbackField* CpuSnapshot = CpuSnapshots.Find(GpuField.FrameNumber);
			if (!CpuSnapshot)
			{
				continue;
			}

			Capture.bIsCaptured = true;
			Capture.CapturedFrame = GpuField.FrameNumber;
			Capture.CpuField = *CpuSnapshot;
			Capture.GpuField = GpuField;
		}
	}

	bool HasPendingCapture(const FTDFluidParityRunResult& Result)
	{
		for (const TArray<FTDFluidParityCapture>& Captures : Result.CapturesPerGpuActor)
		{
			for (const FTDFluidParityCapture& Capture : Captures)
			{
				if (!Capture.bIsCaptured)
				{
					return true;
				}
			}
		}
		return false;
	}

	bool RunParityScenario(FAutomationTestBase& Test, const FString& Label, const FTDFluidParityRunSettings& Settings, FTDFluidParityRunResult& OutResult)
	{
		FTDFluidSolverBackendVariableScope BackendScope(0);
		FTDFluidGpuBackendTestWorld TestWorld;
		UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
		if (!Test.TestNotNull(*(Label + TEXT(" has a fluid subsystem")), FluidSubsystem))
		{
			return false;
		}

		const FVector InitialAnchorLocation = Settings.ResolveAnchorLocation(0);
		AActor* AnchorActor = Settings.bFollowsAnchor ? SpawnAnchorActor(*TestWorld.World, InitialAnchorLocation) : nullptr;
		ATDFluidLiveActor* CpuActor = SpawnConfiguredLiveActor(*TestWorld.World, InitialAnchorLocation, Settings.CpuConfig);
		if (!Test.TestNotNull(*(Label + TEXT(" spawned the CPU reference actor")), CpuActor))
		{
			return false;
		}
		if (!Test.TestFalse(*(Label + TEXT(" CPU reference actor uses the CPU solver")), CpuActor->LiveComponent->IsUsingGpuSolver()))
		{
			return false;
		}
		if (Settings.ConfigureActor)
		{
			Settings.ConfigureActor(*CpuActor);
		}

		TArray<ATDFluidLiveActor*> GpuActors;
		for (const FTDFluidSimulationConfig& GpuConfig : Settings.GpuConfigs)
		{
			ATDFluidLiveActor* GpuActor = SpawnConfiguredLiveActor(*TestWorld.World, InitialAnchorLocation, GpuConfig);
			if (!Test.TestNotNull(*(Label + TEXT(" spawned a GPU actor")), GpuActor))
			{
				return false;
			}
			if (!GpuActor->LiveComponent->IsUsingGpuSolver())
			{
				Test.AddError(FString::Printf(TEXT("%s: the GPU actor (requested %d) did not select the GPU solver. Check SM5 support and %s."), *Label, GpuConfig.GridResolution, SolverBackendVariableName));
				return false;
			}
			if (Settings.ConfigureActor)
			{
				Settings.ConfigureActor(*GpuActor);
			}
			GpuActors.Add(GpuActor);
			OutResult.GpuResolutions.Add(GpuActor->LiveComponent->GetSimulationResolution());
		}

		TArray<ATDFluidLiveActor*> AllActors(GpuActors);
		AllActors.Add(CpuActor);
		for (ATDFluidLiveActor* LiveActor : AllActors)
		{
			if (AnchorActor)
			{
				LiveActor->bShouldFollowPlayerPawn = true;
				LiveActor->FollowTargetActor = AnchorActor;
			}
			if (!Settings.Obstacles.IsEmpty())
			{
				LiveActor->LiveComponent->SetSdfObstacles(Settings.Obstacles);
			}
		}

		OutResult.CapturesPerGpuActor.SetNum(GpuActors.Num());
		int32 LastTargetFrame = 0;
		for (TArray<FTDFluidParityCapture>& Captures : OutResult.CapturesPerGpuActor)
		{
			for (const int32 CaptureFrame : Settings.CaptureFrames)
			{
				FTDFluidParityCapture& Capture = Captures.AddDefaulted_GetRef();
				Capture.TargetFrame = CaptureFrame;
				LastTargetFrame = FMath::Max(LastTargetFrame, CaptureFrame);
			}
		}

		OutResult.InitialCpuCenterWorld = CpuActor->LiveComponent->GetSimulationCenterWorld();
		TMap<uint32, FTDFluidReadbackField> CpuSnapshots;
		TArray<FTDFluidBrushStamp> StepStamps;
		for (int32 StepIndex = 0; StepIndex < LastTargetFrame + ParityDrainStepLimit; ++StepIndex)
		{
			const FVector AnchorLocation = Settings.ResolveAnchorLocation(StepIndex);
			if (AnchorActor)
			{
				AnchorActor->SetActorLocation(AnchorLocation);
			}

			StepStamps.Reset();
			Settings.AppendStamps(StepIndex, AnchorLocation, StepStamps);
			for (const ATDFluidLiveActor* LiveActor : AllActors)
			{
				QueueStampsOnActor(*LiveActor, StepStamps);
			}

			TestWorld.StepWorld(BackendTestStepSeconds, true);
			++OutResult.ExecutedStepCount;

			const uint32 SubmittedFrame = FluidSubsystem->GetLastGpuSolverFrameNumber();
			CpuSnapshots.FindOrAdd(SubmittedFrame).DownsampleFromSolver(CpuActor->LiveComponent->GetSolver());

			const FVector2D CpuCenterWorld = CpuActor->LiveComponent->GetSimulationCenterWorld();
			for (int32 GpuIndex = 0; GpuIndex < GpuActors.Num(); ++GpuIndex)
			{
				const UTDFluidLiveComponent& GpuComponent = *GpuActors[GpuIndex]->LiveComponent;
				OutResult.MaximumCenterMismatchCm = FMath::Max(OutResult.MaximumCenterMismatchCm, (GpuComponent.GetSimulationCenterWorld() - CpuCenterWorld).GetAbsMax());
				TryCaptureParityFrames(GpuComponent.GetGpuReadbackField(), CpuSnapshots, OutResult.CapturesPerGpuActor[GpuIndex]);
			}

			if (StepIndex + 1 >= LastTargetFrame && !HasPendingCapture(OutResult))
			{
				break;
			}
		}

		OutResult.FinalCpuCenterWorld = CpuActor->LiveComponent->GetSimulationCenterWorld();
		for (const ATDFluidLiveActor* GpuActor : GpuActors)
		{
			OutResult.GpuOutputTargets.Add(DescribeGpuOutputTargets(*GpuActor->LiveComponent));
		}

		if (Settings.bCapturesFinalOutputPixels)
		{
			OutResult.FinalCpuDensityPressurePixels = CpuActor->LiveComponent->GetPackedDensityPressurePixels();
			for (const ATDFluidLiveActor* GpuActor : GpuActors)
			{
				TArray<FFloat16Color>& GpuPixels = OutResult.FinalGpuDensityPressurePixels.AddDefaulted_GetRef();
				ReadGpuDensityPressurePixels(*GpuActor->LiveComponent, GpuPixels);
			}
		}

		if (Settings.bCapturesFinalVelocityGrids)
		{
			const UTDFluidLiveComponent& CpuComponent = *CpuActor->LiveComponent;
			OutResult.FinalCpuPackedVelocityGrid = DecodePackedVelocityGrid(CpuComponent.GetPackedVelocityPixels(), CpuComponent.GetSimulationResolution());
			OutResult.FinalCpuSolverVelocityGrid = ReadSolverVelocityGrid(CpuComponent.GetSolver());
			TArray<FFloat16Color> GpuVelocityPixels;
			for (const ATDFluidLiveActor* GpuActor : GpuActors)
			{
				ReadGpuVelocityPixels(*GpuActor->LiveComponent, GpuVelocityPixels);
				OutResult.FinalGpuPackedVelocityGrids.Add(DecodePackedVelocityGrid(GpuVelocityPixels, GpuActor->LiveComponent->GetSimulationResolution()));
			}
		}

		if (Settings.InspectFinalActors)
		{
			Settings.InspectFinalActors(*CpuActor, GpuActors);
		}

		return true;
	}

	bool ExpectStatisticalParity(FAutomationTestBase& Test, const FString& Label, const FTDFluidParityCapture& Capture, const FVector2D& SimulationCellSizeCm)
	{
		if (!Test.TestTrue(*FString::Printf(TEXT("%s captured a GPU readback at or after frame %d"), *Label, Capture.TargetFrame), Capture.bIsCaptured))
		{
			return false;
		}

		const FTDFluidFieldStatistics CpuStatistics = ComputeFieldStatistics(Capture.CpuField);
		const FTDFluidFieldStatistics GpuStatistics = ComputeFieldStatistics(Capture.GpuField);
		const double DensityDifference = ComputeRelativeDifference(CpuStatistics.TotalDensity, GpuStatistics.TotalDensity);
		const double KineticEnergyDifference = ComputeRelativeDifference(CpuStatistics.KineticEnergy, GpuStatistics.KineticEnergy);
		const double DivergenceRatio = GpuStatistics.DivergenceL2 / FMath::Max(CpuStatistics.DivergenceL2, UE_DOUBLE_SMALL_NUMBER);
		const FVector2D CentroidOffsetCm = GpuStatistics.DensityCentroid - CpuStatistics.DensityCentroid;
		const double HistogramDistance = ComputeHistogramL1Distance(CpuStatistics, GpuStatistics);

		const FString ParityLine = FString::Printf(
			TEXT("TDFluidGpuParity: case=%s frame=%u density_cpu=%.2f density_gpu=%.2f density_diff=%.4f ke_cpu=%.4e ke_gpu=%.4e ke_diff=%.4f div_cpu=%.5f div_gpu=%.5f div_ratio=%.3f centroid_dx_cells=%.3f centroid_dy_cells=%.3f hist_l1=%.4f"),
			*Label,
			Capture.CapturedFrame,
			CpuStatistics.TotalDensity,
			GpuStatistics.TotalDensity,
			DensityDifference,
			CpuStatistics.KineticEnergy,
			GpuStatistics.KineticEnergy,
			KineticEnergyDifference,
			CpuStatistics.DivergenceL2,
			GpuStatistics.DivergenceL2,
			DivergenceRatio,
			CentroidOffsetCm.X / SimulationCellSizeCm.X,
			CentroidOffsetCm.Y / SimulationCellSizeCm.Y,
			HistogramDistance);
		UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *ParityLine);
		Test.AddInfo(ParityLine);

		Test.TestTrue(*(Label + TEXT(" CPU reference carries density")), CpuStatistics.TotalDensity > UE_DOUBLE_KINDA_SMALL_NUMBER);
		Test.TestTrue(*(Label + TEXT(" CPU reference carries kinetic energy")), CpuStatistics.KineticEnergy > UE_DOUBLE_KINDA_SMALL_NUMBER);
		Test.TestTrue(*(Label + TEXT(" both fields have a density centroid")), CpuStatistics.bHasDensityCentroid && GpuStatistics.bHasDensityCentroid);
		Test.TestTrue(*(Label + TEXT(" total density matches the CPU within 3%")), DensityDifference <= DensityParityTolerance);
		Test.TestTrue(*(Label + TEXT(" kinetic energy matches the CPU within 10%")), KineticEnergyDifference <= KineticEnergyParityTolerance);
		Test.TestTrue(*(Label + TEXT(" post-projection divergence L2 is at most 1.2 times the CPU")), DivergenceRatio <= DivergenceParityRatio);
		Test.TestTrue(*(Label + TEXT(" density centroid X is within one simulation cell")), FMath::Abs(CentroidOffsetCm.X) <= SimulationCellSizeCm.X);
		Test.TestTrue(*(Label + TEXT(" density centroid Y is within one simulation cell")), FMath::Abs(CentroidOffsetCm.Y) <= SimulationCellSizeCm.Y);
		Test.TestTrue(*(Label + TEXT(" density histogram L1 distance is at most 0.05")), HistogramDistance <= HistogramParityTolerance);
		return true;
	}

	FTDFluidParityRunSettings MakeSmokeParitySettings(int32 CpuResolution, int32 GpuResolution)
	{
		FTDFluidParityRunSettings Settings;
		Settings.CpuConfig = MakeSmokeParityConfig(CpuResolution, ETDFluidSolverBackend::Cpu);
		Settings.GpuConfigs.Add(MakeSmokeParityConfig(GpuResolution, ETDFluidSolverBackend::Gpu));
		Settings.CaptureFrames.Add(ParityStepCount);
		Settings.ResolveAnchorLocation = [](int32)
		{
			return ParityAnchorLocation;
		};
		const double StampExtentCm = Settings.CpuConfig.SimExtentCm.X;
		Settings.AppendStamps = [StampExtentCm](int32 StepIndex, const FVector& AnchorLocation, TArray<FTDFluidBrushStamp>& OutStamps)
		{
			AppendParityStamps(StepIndex, AnchorLocation, StampExtentCm, OutStamps);
		};
		return Settings;
	}

	FVector2D ComputeSimulationCellSizeCm(const FTDFluidSimulationConfig& Config, int32 Resolution)
	{
		return Config.SimExtentCm / static_cast<double>(FMath::Max(Resolution, 1));
	}

	bool RunSmokeStatisticalParity(FAutomationTestBase& Test, const TCHAR* RhiTestLabel, int32 Resolution)
	{
		if (!CanRunRhiParityTest(Test, RhiTestLabel))
		{
			return false;
		}

		const FString Label = FString::Printf(TEXT("Smoke%d"), Resolution);
		const FTDFluidParityRunSettings Settings = MakeSmokeParitySettings(Resolution, Resolution);
		FTDFluidParityRunResult Result;
		if (!RunParityScenario(Test, Label, Settings, Result))
		{
			return false;
		}

		Test.TestEqual(*(Label + TEXT(" GPU resolution is the requested tier")), static_cast<int32>(Result.GpuResolutions[0]), static_cast<int32>(Resolution));
		ExpectGpuOutputTargets(Test, Label, Result.GpuOutputTargets[0], Resolution, Resolution);
		ExpectStatisticalParity(Test, Label, Result.CapturesPerGpuActor[0][0], ComputeSimulationCellSizeCm(Settings.CpuConfig, Resolution));
		return true;
	}

	struct FTDFluidHeadlessRunChecksums
	{
		bool bIsValid = false;
		bool bUsedGpu = false;
		bool bHasRenderTargetOutput = false;
		int32 Resolution = 0;
		uint32 StateChecksum = 0;
		uint32 PixelChecksum = 0;
	};

	FTDFluidHeadlessRunChecksums RunHeadlessBackendScenario(FAutomationTestBase& Test, ETDFluidSolverBackend Backend, int32 RequestedResolution, int32 StepCount)
	{
		FTDFluidHeadlessRunChecksums Checksums;
		FTDFluidGpuBackendTestWorld TestWorld;
		const FTDFluidSimulationConfig Config = MakeSmokeParityConfig(RequestedResolution, Backend);
		const ATDFluidLiveActor* LiveActor = SpawnConfiguredLiveActor(*TestWorld.World, ParityAnchorLocation, Config);
		if (!Test.TestNotNull(TEXT("Headless backend scenario spawned a live actor"), LiveActor))
		{
			return Checksums;
		}

		TArray<FTDFluidBrushStamp> StepStamps;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			StepStamps.Reset();
			AppendParityStamps(StepIndex, ParityAnchorLocation, Config.SimExtentCm.X, StepStamps);
			QueueStampsOnActor(*LiveActor, StepStamps);
			TestWorld.StepWorld(BackendTestStepSeconds, false);
		}

		const UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
		Checksums.bIsValid = Component.GetSolver().IsInitialized() && !Component.GetPackedDensityPressurePixels().IsEmpty();
		Checksums.bUsedGpu = Component.IsUsingGpuSolver();
		Checksums.bHasRenderTargetOutput = Cast<UTextureRenderTarget2D>(Component.GetDensityPressureTexture()) != nullptr;
		Checksums.Resolution = Component.GetSimulationResolution();
		Checksums.StateChecksum = Component.GetSolver().ComputeStateChecksum();
		Checksums.PixelChecksum = ComputeOutputPixelChecksum(Component);
		return Checksums;
	}

	FTDFluidGpuBackendEnvironment MakeBackendEnvironment(bool bHasRenderingRhi, bool bSupportsShaderModel5, ETDFluidGpuBackendOverride Override)
	{
		FTDFluidGpuBackendEnvironment Environment;
		Environment.bHasRenderingRhi = bHasRenderingRhi;
		Environment.bSupportsShaderModel5 = bSupportsShaderModel5;
		Environment.Override = Override;
		return Environment;
	}

	FTDFluidGpuBackendDecision DecideBackend(ETDFluidGpuSolverPreference Preference, int32 RequestedResolution, bool bIsWaterLayerActive, const FTDFluidGpuBackendEnvironment& Environment)
	{
		FTDFluidGpuBackendQuery Query;
		Query.Preference = Preference;
		Query.RequestedResolution = RequestedResolution;
		Query.bIsWaterLayerActive = bIsWaterLayerActive;
		return FTDFluidGpuBackendSelection::Decide(Query, Environment);
	}

	void RunSolverWithParityStamps(FTDFluidGrid2DSolver& Solver, int32 StepCount, const FVector& AnchorLocation)
	{
		TArray<FTDFluidBrushStamp> StepStamps;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			StepStamps.Reset();
			AppendParityStamps(StepIndex, AnchorLocation, Solver.GetExtentCm().X, StepStamps);
			Solver.ApplyBrushStamps(StepStamps, BackendTestStepSeconds);
			Solver.StepSimulation(BackendTestStepSeconds);
		}
	}

	int32 ComputeReadbackBlockStartCell(int32 FieldIndex, int32 SimulationResolution)
	{
		return (FieldIndex * SimulationResolution) / FTDFluidReadbackField::GetResolution();
	}

	int32 ComputeReadbackBlockEndCell(int32 FieldIndex, int32 SimulationResolution)
	{
		return FMath::Max(ComputeReadbackBlockStartCell(FieldIndex, SimulationResolution) + 1, ComputeReadbackBlockStartCell(FieldIndex + 1, SimulationResolution));
	}

	int32 MeasureDistanceToContainingBlock(int32 MappedFieldIndex, int32 CellIndex, int32 SimulationResolution)
	{
		int32 BestDistance = MAX_int32;
		for (int32 FieldIndex = 0; FieldIndex < FTDFluidReadbackField::GetResolution(); ++FieldIndex)
		{
			const bool bContainsCell = CellIndex >= ComputeReadbackBlockStartCell(FieldIndex, SimulationResolution) && CellIndex < ComputeReadbackBlockEndCell(FieldIndex, SimulationResolution);
			if (bContainsCell)
			{
				BestDistance = FMath::Min(BestDistance, FMath::Abs(MappedFieldIndex - FieldIndex));
			}
		}
		return BestDistance;
	}

	double SumSolverDensity(const FTDFluidGrid2DSolver& Solver, bool bUseSelectiveDensity)
	{
		double DensitySum = 0.0;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				const FTDFluidCellSample Cell = Solver.SampleAtCell(CellX, CellY);
				DensitySum += bUseSelectiveDensity ? Cell.SelectiveDensity : Cell.PrimaryDensity;
			}
		}
		return DensitySum;
	}

	double SumFieldDensity(const FTDFluidReadbackField& Field, bool bUseSelectiveDensity)
	{
		const TArray<float>& Densities = bUseSelectiveDensity ? Field.SelectiveDensity : Field.PrimaryDensity;
		double DensitySum = 0.0;
		for (const float Density : Densities)
		{
			DensitySum += Density;
		}
		return DensitySum;
	}

	template <typename ValueType>
	bool AreBitIdentical(const ValueType& Left, const ValueType& Right)
	{
		return FMemory::Memcmp(&Left, &Right, sizeof(ValueType)) == 0;
	}

	bool AreCellSamplesBitIdentical(const FTDFluidCellSample& Left, const FTDFluidCellSample& Right)
	{
		return AreBitIdentical(Left.PrimaryDensity, Right.PrimaryDensity)
			&& AreBitIdentical(Left.SelectiveDensity, Right.SelectiveDensity)
			&& AreBitIdentical(Left.Pressure, Right.Pressure)
			&& AreBitIdentical(Left.ObstacleSdf, Right.ObstacleSdf)
			&& AreBitIdentical(Left.Velocity, Right.Velocity)
			&& AreBitIdentical(Left.VelocityCmPerSec, Right.VelocityCmPerSec)
			&& AreBitIdentical(Left.TerrainSlope, Right.TerrainSlope)
			&& AreBitIdentical(Left.WaterDepthCm, Right.WaterDepthCm)
			&& AreBitIdentical(Left.Wetness, Right.Wetness)
			&& AreBitIdentical(Left.FlowGuideWeight, Right.FlowGuideWeight)
			&& AreBitIdentical(Left.FlowTargetVelocity, Right.FlowTargetVelocity)
			&& AreBitIdentical(Left.RippleHeightCm, Right.RippleHeightCm)
			&& Left.bIsTerrainKnown == Right.bIsTerrainKnown;
	}

	constexpr int32 QueryGoldenStepCount = 120;
	constexpr int32 QueryGoldenElevationRefreshInterval = 40;
	constexpr float QueryGoldenStepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 45.0f, 0.15f };
	constexpr int32 QueryGoldenStepSecondsCycleLength = UE_ARRAY_COUNT(QueryGoldenStepSecondsCycle);

	enum class ETDFluidQueryGoldenSlopeSource : uint8
	{
		None,
		Uniform,
		Elevation
	};

	struct FTDFluidQueryGoldenScenario
	{
		const TCHAR* Name;
		ETDFluidPresetKind PresetKind;
		int32 Resolution;
		FVector2D ExtentCm;
		bool bHasObstacles;
		bool bSlidesWindow;
		ETDFluidQueryGoldenSlopeSource SlopeSource;
		bool bDisablesOptionalForces;
		uint32 ExpectedStateChecksum;
		uint32 ExpectedPixelChecksum;
	};

	const FTDFluidQueryGoldenScenario QueryGoldenScenarios[] = {
		{ TEXT("WaterNonSquareObstaclesSlidingUniformSlope"), ETDFluidPresetKind::Water, 96, FVector2D(1000.0, 800.0), true, true, ETDFluidQueryGoldenSlopeSource::Uniform, false, 0x24BE4A74u, 0xA9C79CD5u },
		{ TEXT("MudSquareElevationSlope"), ETDFluidPresetKind::Mud, 64, FVector2D(900.0, 900.0), false, false, ETDFluidQueryGoldenSlopeSource::Elevation, false, 0xB89FEB00u, 0x476B365Au },
		{ TEXT("FireNonSquareObstaclesSlidingElevationSlope"), ETDFluidPresetKind::Fire, 128, FVector2D(1000.0, 800.0), true, true, ETDFluidQueryGoldenSlopeSource::Elevation, false, 0x40FC23E6u, 0x0C5E736Cu },
		{ TEXT("BloodTallObstaclesWithoutOptionalForces"), ETDFluidPresetKind::Blood, 48, FVector2D(600.0, 1000.0), true, false, ETDFluidQueryGoldenSlopeSource::None, true, 0xFC35DA8Bu, 0x8616E732u },
		{ TEXT("FootprintNonSquareObstaclesSliding"), ETDFluidPresetKind::FootprintPainter, 64, FVector2D(1000.0, 800.0), true, true, ETDFluidQueryGoldenSlopeSource::None, false, 0xABB40516u, 0x748F15C6u },
	};

	TArray<FTDFluidSdfObstacle> MakeQueryGoldenObstacles()
	{
		TArray<FTDFluidSdfObstacle> Obstacles;

		FTDFluidSdfObstacle Pillar;
		Pillar.WorldCenter = FVector(150.0, -100.0, 0.0);
		Pillar.RadiusCm = 90.0f;
		Obstacles.Add(Pillar);

		FTDFluidSdfObstacle Crate;
		Crate.WorldCenter = FVector(-250.0, 120.0, 0.0);
		Crate.BoxHalfExtentsCm = FVector2D(80.0, 50.0);
		Crate.bIsBox = true;
		Obstacles.Add(Crate);

		FTDFluidSdfObstacle EdgeWall;
		EdgeWall.WorldCenter = FVector(470.0, 0.0, 0.0);
		EdgeWall.BoxHalfExtentsCm = FVector2D(60.0, 250.0);
		EdgeWall.bIsBox = true;
		Obstacles.Add(EdgeWall);

		FTDFluidSdfObstacle TopLedge;
		TopLedge.WorldCenter = FVector(0.0, 450.0, 0.0);
		TopLedge.BoxHalfExtentsCm = FVector2D(200.0, 80.0);
		TopLedge.bIsBox = true;
		Obstacles.Add(TopLedge);

		return Obstacles;
	}

	float EvaluateQueryGoldenElevationCm(const FVector2D& PlanePoint)
	{
		return static_cast<float>(40.0 * FMath::Sin(PlanePoint.X * 0.004) + 25.0 * FMath::Cos(PlanePoint.Y * 0.006) + 0.03 * PlanePoint.X);
	}

	void RefreshQueryGoldenElevation(UTDFluidLiveComponent& Component)
	{
		const int32 Resolution = Component.GetSimulationResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Component.SetTerrainElevationAtCell(CellX, CellY, EvaluateQueryGoldenElevationCm(Component.CellToPlaneCenter(CellX, CellY)));
			}
		}
		Component.CompleteTerrainElevationSampling();
	}

	TArray<FTDFluidBrushStamp> MakeQueryGoldenStamps(const FVector2D& OrbitCenter, float OrbitRadiusCm, int32 StepIndex)
	{
		constexpr ETDFluidBrushChannel StampChannels[] = { ETDFluidBrushChannel::ChannelA_Water, ETDFluidBrushChannel::ChannelG_Blood, ETDFluidBrushChannel::ChannelB_Fire };
		constexpr float StampRadiiCm[] = { 60.0f, 90.0f, 120.0f };
		constexpr int32 OrbitingStampCount = UE_ARRAY_COUNT(StampChannels);

		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StampIndex = 0; StampIndex < OrbitingStampCount; ++StampIndex)
		{
			const double Angle = StepIndex * 0.11 + StampIndex * (UE_DOUBLE_TWO_PI / 3.0);
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(OrbitCenter.X + FMath::Cos(Angle) * OrbitRadiusCm, OrbitCenter.Y + FMath::Sin(Angle) * OrbitRadiusCm, 0.0);
			Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 900.0;
			Stamp.RadiusCm = StampRadiiCm[StampIndex];
			Stamp.DensityStrength = 0.6f + 0.2f * StampIndex;
			Stamp.VelocityStrength = 1.0f;
			Stamp.Channel = StampChannels[StampIndex];
			Stamps.Add(Stamp);
		}

		FTDFluidBrushStamp FastStamp;
		FastStamp.WorldLocation = FVector(OrbitCenter.X - OrbitRadiusCm * 0.5, OrbitCenter.Y + OrbitRadiusCm * 0.25, 0.0);
		FastStamp.WorldVelocity = FVector(1500.0, -700.0, 0.0);
		FastStamp.RadiusCm = 70.0f;
		FastStamp.DensityStrength = 1.0f;
		FastStamp.VelocityStrength = 1.5f;
		FastStamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		Stamps.Add(FastStamp);

		return Stamps;
	}

	struct FTDFluidQueryScenarioResult
	{
		uint32 StateChecksum = 0;
		uint32 PixelChecksum = 0;
		int32 ComparedCellCount = 0;
		int32 CellCenterMismatchCount = 0;
		int32 CellSampleMismatchCount = 0;
		int32 WorldSampleMismatchCount = 0;
		bool bScalarQueriesMatch = false;
	};

	int32 CountWorldSampleMismatches(const UTDFluidLiveComponent& Component)
	{
		const FTDFluidGrid2DSolver& Solver = Component.GetSolver();
		const FVector2D CenterWorld = Solver.GetCenterWorld();
		const FVector2D ExtentCm = Solver.GetExtentCm();
		const FVector SamplePoints[] = {
			FVector(CenterWorld.X, CenterWorld.Y, 0.0),
			FVector(CenterWorld.X + ExtentCm.X * 0.21, CenterWorld.Y - ExtentCm.Y * 0.17, 35.0),
			FVector(CenterWorld.X - ExtentCm.X * 0.37, CenterWorld.Y + ExtentCm.Y * 0.29, -12.0),
			FVector(CenterWorld.X + ExtentCm.X * 0.499, CenterWorld.Y + ExtentCm.Y * 0.499, 0.0),
			FVector(CenterWorld.X + ExtentCm.X * 0.75, CenterWorld.Y, 0.0),
		};

		int32 MismatchCount = 0;
		for (const FVector& SamplePoint : SamplePoints)
		{
			FTDFluidCellSample QuerySample;
			FTDFluidCellSample SolverSample;
			const bool bQueryInside = Component.SampleFluidAtWorldLocation(SamplePoint, QuerySample);
			const bool bSolverInside = Solver.SampleAtWorldPosition(Component.ProjectWorldToSolverPlane(SamplePoint), SolverSample);
			if (bQueryInside != bSolverInside || !AreCellSamplesBitIdentical(QuerySample, SolverSample))
			{
				++MismatchCount;
			}
		}
		return MismatchCount;
	}

	FTDFluidQueryScenarioResult RunQueryGoldenScenario(const FTDFluidQueryGoldenScenario& Scenario)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Scenario.PresetKind);
		Config.GridResolution = Scenario.Resolution;
		Config.SimExtentCm = Scenario.ExtentCm;
		if (Scenario.bDisablesOptionalForces)
		{
			Config.BorderFadeRatio = 0.0f;
			Config.VorticityStrength = 0.0f;
			Config.TerrainSlopeGravity = 0.0f;
		}

		TStrongObjectPtr<UTDFluidLiveComponent> Component(NewObject<UTDFluidLiveComponent>(GetTransientPackage()));
		FTDFluidGrid2DSolver& Solver = Component->GetMutableSolver();
		Solver.Initialize(Config, FVector2D::ZeroVector);

		const TArray<FTDFluidSdfObstacle> Obstacles = Scenario.bHasObstacles ? MakeQueryGoldenObstacles() : TArray<FTDFluidSdfObstacle>();
		if (Scenario.bHasObstacles)
		{
			Solver.RasterizeSdfObstacles(Obstacles);
		}
		if (Scenario.SlopeSource == ETDFluidQueryGoldenSlopeSource::Uniform)
		{
			Solver.SetUniformTerrainSlope(FVector2D(0.12, -0.07));
		}

		const float OrbitRadiusCm = static_cast<float>(FMath::Min(Scenario.ExtentCm.X, Scenario.ExtentCm.Y) * 0.3);
		for (int32 StepIndex = 0; StepIndex < QueryGoldenStepCount; ++StepIndex)
		{
			const float DeltaTime = QueryGoldenStepSecondsCycle[StepIndex % QueryGoldenStepSecondsCycleLength];
			const FVector2D DesiredCenter = Scenario.bSlidesWindow ? FVector2D(StepIndex * 4.3, StepIndex * -2.7) : FVector2D::ZeroVector;

			if (Scenario.bSlidesWindow && Solver.ShiftOriginForSlidingWindow(DesiredCenter) && Scenario.bHasObstacles)
			{
				Solver.RasterizeSdfObstacles(Obstacles);
			}
			if (Scenario.SlopeSource == ETDFluidQueryGoldenSlopeSource::Elevation && StepIndex % QueryGoldenElevationRefreshInterval == 0)
			{
				RefreshQueryGoldenElevation(*Component);
			}

			Solver.ApplyBrushStamps(MakeQueryGoldenStamps(DesiredCenter, OrbitRadiusCm, StepIndex), DeltaTime);
			Solver.StepSimulation(DeltaTime);
		}

		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		Solver.PackOutputPixels(DensityPressurePixels, VelocityPixels);

		FTDFluidQueryScenarioResult Result;
		Result.StateChecksum = Solver.ComputeStateChecksum();
		Result.PixelChecksum = FCrc::MemCrc32(DensityPressurePixels.GetData(), DensityPressurePixels.Num() * sizeof(FFloat16Color));
		Result.PixelChecksum = FCrc::MemCrc32(VelocityPixels.GetData(), VelocityPixels.Num() * sizeof(FFloat16Color), Result.PixelChecksum);

		const UTDFluidLiveComponent& QueryComponent = *Component;
		Result.bScalarQueriesMatch = QueryComponent.IsSimulationInitialized() == Solver.IsInitialized()
			&& !QueryComponent.IsUsingGpuSolver()
			&& QueryComponent.GetSimulationResolution() == Solver.GetResolution()
			&& AreBitIdentical(QueryComponent.GetSimulationExtentCm(), Solver.GetExtentCm())
			&& AreBitIdentical(QueryComponent.GetSimulationCenterWorld(), Solver.GetCenterWorld())
			&& QueryComponent.GetSimulationGeneration() == Solver.GetReinitializationGeneration();

		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				++Result.ComparedCellCount;
				Result.CellCenterMismatchCount += AreBitIdentical(QueryComponent.CellToPlaneCenter(CellX, CellY), Solver.CellToWorldCenter(CellX, CellY)) ? 0 : 1;
				Result.CellSampleMismatchCount += AreCellSamplesBitIdentical(QueryComponent.SampleSimulationCell(CellX, CellY), Solver.SampleAtCell(CellX, CellY)) ? 0 : 1;
			}
		}
		Result.WorldSampleMismatchCount = CountWorldSampleMismatches(QueryComponent);
		return Result;
	}

	struct FTDFluidObstacleRegionStatistics
	{
		int32 DeepInsideCellCount = 0;
		double DeepInsideMeanDensity = 0.0;
		double DeepInsideMeanSpeed = 0.0;
		int32 BandCellCount = 0;
		double BandMeanDensity = 0.0;
		double BandMeanSpeed = 0.0;
	};

	FTDFluidObstacleRegionStatistics MeasureObstacleRegions(const FTDFluidReadbackField& Field, TConstArrayView<FTDFluidSdfObstacle> Obstacles)
	{
		FTDFluidObstacleRegionStatistics RegionStatistics;
		if (!Field.IsValid() || Obstacles.IsEmpty())
		{
			return RegionStatistics;
		}

		const int32 FieldResolution = FTDFluidReadbackField::GetResolution();
		const double FieldCellSizeCm = (Field.ExtentCm / static_cast<double>(FieldResolution)).GetMax();
		const double DeepInsideDistanceCm = -ObstacleDeepInsideMarginFieldCells * FieldCellSizeCm;
		const double BandDistanceCm = ObstacleBandWidthFieldCells * FieldCellSizeCm;
		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				const FVector2D CellCenter = ComputeFieldCellCenter(Field, FieldX, FieldY);
				double NearestDistanceCm = TNumericLimits<double>::Max();
				for (const FTDFluidSdfObstacle& Obstacle : Obstacles)
				{
					NearestDistanceCm = FMath::Min(NearestDistanceCm, EvaluateObstacleDistanceCm(Obstacle, CellCenter));
				}

				const int32 FieldIndex = FieldY * FieldResolution + FieldX;
				const double CellDensity = Field.PrimaryDensity[FieldIndex];
				const double CellSpeed = Field.VelocityCmPerSec[FieldIndex].Size();
				if (NearestDistanceCm < DeepInsideDistanceCm)
				{
					++RegionStatistics.DeepInsideCellCount;
					RegionStatistics.DeepInsideMeanDensity += CellDensity;
					RegionStatistics.DeepInsideMeanSpeed += CellSpeed;
				}
				else if (NearestDistanceCm > 0.0 && NearestDistanceCm <= BandDistanceCm)
				{
					++RegionStatistics.BandCellCount;
					RegionStatistics.BandMeanDensity += CellDensity;
					RegionStatistics.BandMeanSpeed += CellSpeed;
				}
			}
		}

		if (RegionStatistics.DeepInsideCellCount > 0)
		{
			RegionStatistics.DeepInsideMeanDensity /= RegionStatistics.DeepInsideCellCount;
			RegionStatistics.DeepInsideMeanSpeed /= RegionStatistics.DeepInsideCellCount;
		}
		if (RegionStatistics.BandCellCount > 0)
		{
			RegionStatistics.BandMeanDensity /= RegionStatistics.BandCellCount;
			RegionStatistics.BandMeanSpeed /= RegionStatistics.BandCellCount;
		}
		return RegionStatistics;
	}

	double MeasureTrailLengthCm(const FTDFluidReadbackField& Field, const FVector2D& PathCenter, const FVector2D& PathDirection)
	{
		if (!Field.IsValid())
		{
			return 0.0;
		}

		const int32 FieldResolution = FTDFluidReadbackField::GetResolution();
		double WeightSum = 0.0;
		double WeightedPosition = 0.0;
		double WeightedPositionSquared = 0.0;
		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				const double CellDensity = Field.PrimaryDensity[FieldY * FieldResolution + FieldX];
				if (CellDensity <= 0.0)
				{
					continue;
				}

				const double AlongPathCm = FVector2D::DotProduct(ComputeFieldCellCenter(Field, FieldX, FieldY) - PathCenter, PathDirection);
				WeightSum += CellDensity;
				WeightedPosition += CellDensity * AlongPathCm;
				WeightedPositionSquared += CellDensity * AlongPathCm * AlongPathCm;
			}
		}

		if (WeightSum <= UE_DOUBLE_SMALL_NUMBER)
		{
			return 0.0;
		}

		const double MeanPosition = WeightedPosition / WeightSum;
		const double PositionVariance = FMath::Max(WeightedPositionSquared / WeightSum - MeanPosition * MeanPosition, 0.0);
		return FMath::Sqrt(12.0 * PositionVariance);
	}

	double MeasureMeanChannelDifference(TConstArrayView<FFloat16Color> CpuPixels, TConstArrayView<FFloat16Color> GpuPixels, bool bUseGreenChannel)
	{
		if (CpuPixels.Num() != GpuPixels.Num() || CpuPixels.IsEmpty())
		{
			return -1.0;
		}

		double DifferenceSum = 0.0;
		for (int32 PixelIndex = 0; PixelIndex < CpuPixels.Num(); ++PixelIndex)
		{
			const float CpuValue = bUseGreenChannel ? CpuPixels[PixelIndex].G.GetFloat() : CpuPixels[PixelIndex].R.GetFloat();
			const float GpuValue = bUseGreenChannel ? GpuPixels[PixelIndex].G.GetFloat() : GpuPixels[PixelIndex].R.GetFloat();
			DifferenceSum += FMath::Abs(static_cast<double>(GpuValue) - static_cast<double>(CpuValue));
		}
		return DifferenceSum / CpuPixels.Num();
	}

	struct FTDFluidLatencyTrace
	{
		int32 FirstValidStep = INDEX_NONE;
		int32 MinimumLatency = MAX_int32;
		int32 MaximumLatency = 0;
		int32 InvalidAfterFirstValidCount = 0;
		int32 OverBudgetCount = 0;
		uint32 FirstObservedFieldFrame = 0;
		uint32 LastObservedFieldFrame = 0;
		bool bFrameNumbersAreMonotonic = true;
	};

	FTDFluidLatencyTrace TraceReadbackLatency(FTDFluidGpuBackendTestWorld& TestWorld, const UTDFluidLiveSubsystem& FluidSubsystem, const ATDFluidLiveActor& GpuActor, int32 StepOffset, bool bShouldWaitForGpu)
	{
		FTDFluidLatencyTrace Trace;
		uint32 PreviousFieldFrame = 0;
		TArray<FTDFluidBrushStamp> StepStamps;
		const double StampExtentCm = GpuActor.LiveComponent->GetSimulationExtentCm().X;
		for (int32 TraceStep = 0; TraceStep < LatencyTraceStepCount; ++TraceStep)
		{
			StepStamps.Reset();
			AppendParityStamps(StepOffset + TraceStep, ParityAnchorLocation, StampExtentCm, StepStamps);
			QueueStampsOnActor(GpuActor, StepStamps);
			TestWorld.StepWorld(BackendTestStepSeconds, bShouldWaitForGpu);

			const FTDFluidReadbackField& Field = GpuActor.LiveComponent->GetGpuReadbackField();
			if (!Field.IsValid())
			{
				Trace.InvalidAfterFirstValidCount += Trace.FirstValidStep != INDEX_NONE ? 1 : 0;
				continue;
			}

			if (Trace.FirstValidStep == INDEX_NONE)
			{
				Trace.FirstValidStep = TraceStep + 1;
				Trace.FirstObservedFieldFrame = Field.FrameNumber;
			}
			Trace.LastObservedFieldFrame = Field.FrameNumber;

			const int32 LatencyFrames = static_cast<int32>(FluidSubsystem.GetLastGpuSolverFrameNumber()) - static_cast<int32>(Field.FrameNumber);
			Trace.MinimumLatency = FMath::Min(Trace.MinimumLatency, LatencyFrames);
			Trace.MaximumLatency = FMath::Max(Trace.MaximumLatency, LatencyFrames);
			Trace.OverBudgetCount += LatencyFrames > MaximumReadbackLatencyFrames ? 1 : 0;
			Trace.bFrameNumbersAreMonotonic = Trace.bFrameNumbersAreMonotonic && Field.FrameNumber >= PreviousFieldFrame;
			PreviousFieldFrame = Field.FrameNumber;
		}
		return Trace;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuBackendFallsBackToCpuWithoutRhiTest,
	"TDGame.FluidFX.Gpu.BackendFallsBackToCpuWithoutRhi",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuBackendFallsBackToCpuWithoutRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;

	const FTDFluidGpuBackendEnvironment NullRhiEnvironment = MakeBackendEnvironment(false, false, ETDFluidGpuBackendOverride::FollowPreset);
	const FTDFluidGpuBackendDecision NullGpuDecision = DecideBackend(ETDFluidGpuSolverPreference::Gpu, 512, false, NullRhiEnvironment);
	TestFalse(TEXT("Gpu preference without a rendering RHI decides the CPU"), NullGpuDecision.bUsesGpu);
	TestEqual(TEXT("Gpu preference without a rendering RHI clamps 512 to the CPU limit"), static_cast<int32>(NullGpuDecision.Resolution), static_cast<int32>(256));
	const FTDFluidGpuBackendDecision NullForcedDecision = DecideBackend(ETDFluidGpuSolverPreference::Gpu, 96, false, MakeBackendEnvironment(false, false, ETDFluidGpuBackendOverride::ForceGpuWhenPossible));
	TestFalse(TEXT("Forcing the GPU without a rendering RHI still decides the CPU"), NullForcedDecision.bUsesGpu);
	TestEqual(TEXT("CPU fallback keeps a requested resolution under the CPU limit"), static_cast<int32>(NullForcedDecision.Resolution), static_cast<int32>(96));

	const bool bHasRenderingRhi = HasRenderingRhi();
	FTDFluidSolverBackendVariableScope BackendScope(bHasRenderingRhi ? 2 : 0);
	if (bHasRenderingRhi && !BackendScope.Variable)
	{
		AddError(FString::Printf(TEXT("%s is not registered, so the CPU fallback cannot be forced under a rendering RHI."), SolverBackendVariableName));
		return false;
	}
	AddInfo(bHasRenderingRhi
		? TEXT("A rendering RHI is active: the CPU fallback is exercised through TD.FluidFX.SolverBackend=2.")
		: TEXT("The null RHI is active: the CPU fallback is exercised by the environment."));

	struct FTDFluidFallbackCase
	{
		int32 RequestedResolution;
		int32 ExpectedResolution;
		int32 StepCount;
	};
	const FTDFluidFallbackCase FallbackCases[] = { { 96, 96, 40 }, { 512, 256, 12 } };
	for (const FTDFluidFallbackCase& FallbackCase : FallbackCases)
	{
		const FString CaseLabel = FString::Printf(TEXT("Requested %d"), FallbackCase.RequestedResolution);
		const FTDFluidHeadlessRunChecksums GpuPreferenceRun = RunHeadlessBackendScenario(*this, ETDFluidSolverBackend::Gpu, FallbackCase.RequestedResolution, FallbackCase.StepCount);
		const FTDFluidHeadlessRunChecksums CpuPreferenceRun = RunHeadlessBackendScenario(*this, ETDFluidSolverBackend::Cpu, FallbackCase.RequestedResolution, FallbackCase.StepCount);
		AddInfo(FString::Printf(TEXT("TDFluidGpuFallback: %s gpu_pref state=0x%08X pixels=0x%08X cpu_pref state=0x%08X pixels=0x%08X"), *CaseLabel, GpuPreferenceRun.StateChecksum, GpuPreferenceRun.PixelChecksum, CpuPreferenceRun.StateChecksum, CpuPreferenceRun.PixelChecksum));

		TestTrue(*(CaseLabel + TEXT(" Gpu preference run produced CPU state and pixels")), GpuPreferenceRun.bIsValid);
		TestTrue(*(CaseLabel + TEXT(" Cpu preference run produced CPU state and pixels")), CpuPreferenceRun.bIsValid);
		TestFalse(*(CaseLabel + TEXT(" Gpu preference falls back to the CPU solver")), GpuPreferenceRun.bUsedGpu);
		TestFalse(*(CaseLabel + TEXT(" Gpu preference fallback publishes no GPU render target")), GpuPreferenceRun.bHasRenderTargetOutput);
		TestEqual(*(CaseLabel + TEXT(" fallback resolution is clamped to the CPU range")), static_cast<int32>(GpuPreferenceRun.Resolution), static_cast<int32>(FallbackCase.ExpectedResolution));
		TestEqual(*(CaseLabel + TEXT(" fallback state checksum is bit-identical to the Cpu setting")), static_cast<uint32>(GpuPreferenceRun.StateChecksum), static_cast<uint32>(CpuPreferenceRun.StateChecksum));
		TestEqual(*(CaseLabel + TEXT(" fallback output pixels are bit-identical to the Cpu setting")), static_cast<uint32>(GpuPreferenceRun.PixelChecksum), static_cast<uint32>(CpuPreferenceRun.PixelChecksum));
	}

	if (!bHasRenderingRhi)
	{
		return true;
	}

	BackendScope.SetValue(0);
	FTDFluidGpuBackendTestWorld TestWorld;
	ATDFluidLiveActor* SwitchingActor = SpawnConfiguredLiveActor(*TestWorld.World, ParityAnchorLocation, MakeSmokeParityConfig(128, ETDFluidSolverBackend::Gpu));
	if (!TestNotNull(TEXT("Spawned the backend switching actor"), SwitchingActor))
	{
		return false;
	}

	TestTrue(TEXT("Gpu setting selects the GPU solver while the console variable follows the preset"), SwitchingActor->LiveComponent->IsUsingGpuSolver());
	TestWorld.StepWorld(BackendTestStepSeconds, true);
	TestWorld.StepWorld(BackendTestStepSeconds, true);
	BackendScope.SetValue(2);
	TestWorld.StepWorld(BackendTestStepSeconds, true);
	TestFalse(TEXT("Forcing the CPU through the console variable switches back on the next step"), SwitchingActor->LiveComponent->IsUsingGpuSolver());
	TestEqual(TEXT("The CPU solver after the switch keeps the requested resolution"), static_cast<int32>(SwitchingActor->LiveComponent->GetSimulationResolution()), static_cast<int32>(128));
	TestFalse(TEXT("The CPU solver after the switch publishes no GPU render target"), Cast<UTextureRenderTarget2D>(SwitchingActor->LiveComponent->GetDensityPressureTexture()) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuResolutionClampPerBackendTest,
	"TDGame.FluidFX.Gpu.ResolutionClampPerBackend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuResolutionClampPerBackendTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;

	struct FTDFluidDecisionCase
	{
		const TCHAR* Name;
		ETDFluidGpuSolverPreference Preference;
		int32 RequestedResolution;
		bool bIsWaterLayerActive;
		bool bHasRenderingRhi;
		bool bSupportsShaderModel5;
		ETDFluidGpuBackendOverride Override;
		bool bExpectsGpu;
		int32 ExpectedResolution;
	};

	const FTDFluidDecisionCase DecisionCases[] = {
		{ TEXT("Gpu 8 rounds up to the minimum tier"), ETDFluidGpuSolverPreference::Gpu, 8, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 16 },
		{ TEXT("Gpu 16 keeps the minimum tier"), ETDFluidGpuSolverPreference::Gpu, 16, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 16 },
		{ TEXT("Gpu 100 rounds up to 128"), ETDFluidGpuSolverPreference::Gpu, 100, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 128 },
		{ TEXT("Gpu 128 keeps 128"), ETDFluidGpuSolverPreference::Gpu, 128, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 128 },
		{ TEXT("Gpu 129 rounds up to 256"), ETDFluidGpuSolverPreference::Gpu, 129, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 256 },
		{ TEXT("Gpu 300 rounds up to 512"), ETDFluidGpuSolverPreference::Gpu, 300, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 512 },
		{ TEXT("Gpu 512 keeps 512"), ETDFluidGpuSolverPreference::Gpu, 512, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 512 },
		{ TEXT("Gpu 1000 rounds up to 1024"), ETDFluidGpuSolverPreference::Gpu, 1000, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 1024 },
		{ TEXT("Gpu 2048 keeps the maximum tier"), ETDFluidGpuSolverPreference::Gpu, 2048, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 2048 },
		{ TEXT("Gpu 4096 clamps to the maximum tier"), ETDFluidGpuSolverPreference::Gpu, 4096, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 2048 },
		{ TEXT("Cpu 8 clamps to the CPU minimum"), ETDFluidGpuSolverPreference::Cpu, 8, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, false, 16 },
		{ TEXT("Cpu 100 keeps 100"), ETDFluidGpuSolverPreference::Cpu, 100, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, false, 100 },
		{ TEXT("Cpu 512 clamps to the CPU maximum"), ETDFluidGpuSolverPreference::Cpu, 512, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, false, 256 },
		{ TEXT("Cpu 2048 clamps to the CPU maximum"), ETDFluidGpuSolverPreference::Cpu, 2048, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, false, 256 },
		{ TEXT("Auto 256 stays on the CPU"), ETDFluidGpuSolverPreference::Auto, 256, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, false, 256 },
		{ TEXT("Auto 257 moves to the GPU 512 tier"), ETDFluidGpuSolverPreference::Auto, 257, false, true, true, ETDFluidGpuBackendOverride::FollowPreset, true, 512 },
		{ TEXT("Gpu 512 without a rendering RHI falls back to CPU 256"), ETDFluidGpuSolverPreference::Gpu, 512, false, false, false, ETDFluidGpuBackendOverride::FollowPreset, false, 256 },
		{ TEXT("Gpu 8 without a rendering RHI falls back to CPU 16"), ETDFluidGpuSolverPreference::Gpu, 8, false, false, false, ETDFluidGpuBackendOverride::FollowPreset, false, 16 },
		{ TEXT("Gpu 512 below SM5 falls back to CPU 256"), ETDFluidGpuSolverPreference::Gpu, 512, false, true, false, ETDFluidGpuBackendOverride::FollowPreset, false, 256 },
		{ TEXT("Gpu 512 with the water layer stays on CPU 256"), ETDFluidGpuSolverPreference::Gpu, 512, true, true, true, ETDFluidGpuBackendOverride::FollowPreset, false, 256 },
		{ TEXT("Console force CPU overrides a Gpu 300 setting"), ETDFluidGpuSolverPreference::Gpu, 300, false, true, true, ETDFluidGpuBackendOverride::ForceCpu, false, 256 },
		{ TEXT("Console force GPU overrides a Cpu 300 setting"), ETDFluidGpuSolverPreference::Cpu, 300, false, true, true, ETDFluidGpuBackendOverride::ForceGpuWhenPossible, true, 512 },
		{ TEXT("Console force GPU still respects the water layer"), ETDFluidGpuSolverPreference::Cpu, 300, true, true, true, ETDFluidGpuBackendOverride::ForceGpuWhenPossible, false, 256 },
	};

	for (const FTDFluidDecisionCase& DecisionCase : DecisionCases)
	{
		const FTDFluidGpuBackendDecision Decision = DecideBackend(
			DecisionCase.Preference,
			DecisionCase.RequestedResolution,
			DecisionCase.bIsWaterLayerActive,
			MakeBackendEnvironment(DecisionCase.bHasRenderingRhi, DecisionCase.bSupportsShaderModel5, DecisionCase.Override));
		TestTrue(*FString::Printf(TEXT("%s: backend"), DecisionCase.Name), Decision.bUsesGpu == DecisionCase.bExpectsGpu);
		TestEqual(*FString::Printf(TEXT("%s: resolution"), DecisionCase.Name), static_cast<int32>(Decision.Resolution), static_cast<int32>(DecisionCase.ExpectedResolution));
	}

	TestTrue(TEXT("Cpu backend maps to the Cpu solver preference"), FTDFluidGpuSolverProxy::ToSolverPreference(ETDFluidSolverBackend::Cpu) == ETDFluidGpuSolverPreference::Cpu);
	TestTrue(TEXT("Gpu backend maps to the Gpu solver preference"), FTDFluidGpuSolverProxy::ToSolverPreference(ETDFluidSolverBackend::Gpu) == ETDFluidGpuSolverPreference::Gpu);
	TestTrue(TEXT("Auto backend maps to the Auto solver preference"), FTDFluidGpuSolverProxy::ToSolverPreference(ETDFluidSolverBackend::Auto) == ETDFluidGpuSolverPreference::Auto);
	TestTrue(TEXT("The default simulation config keeps the CPU backend"), FTDFluidSimulationConfig().SolverBackend == ETDFluidSolverBackend::Cpu);

#if WITH_METADATA
	const FProperty* GridResolutionProperty = FindFProperty<FProperty>(FTDFluidSimulationConfig::StaticStruct(), GET_MEMBER_NAME_CHECKED(FTDFluidSimulationConfig, GridResolution));
	if (TestNotNull(TEXT("GridResolution property exists"), GridResolutionProperty))
	{
		TestEqual(TEXT("GridResolution editor ClampMax allows the 2048 GPU tier"), GridResolutionProperty->GetMetaData(TEXT("ClampMax")), FString(TEXT("2048")));
		TestEqual(TEXT("GridResolution editor ClampMin keeps 16"), GridResolutionProperty->GetMetaData(TEXT("ClampMin")), FString(TEXT("16")));
	}
#endif

	const bool bHasRenderingRhi = HasRenderingRhi();
	FTDFluidSolverBackendVariableScope BackendScope(0);
	FTDFluidGpuBackendTestWorld TestWorld;

	struct FTDFluidComponentCase
	{
		ETDFluidSolverBackend Backend;
		int32 RequestedResolution;
		bool bEnablesWaterLayer;
		bool bExpectsGpu;
		int32 ExpectedResolution;
	};

	const FTDFluidComponentCase ComponentCases[] = {
		{ ETDFluidSolverBackend::Cpu, 512, false, false, 256 },
		{ ETDFluidSolverBackend::Cpu, 8, false, false, 16 },
		{ ETDFluidSolverBackend::Cpu, 100, false, false, 100 },
		{ ETDFluidSolverBackend::Gpu, 300, false, bHasRenderingRhi, bHasRenderingRhi ? 512 : 256 },
		{ ETDFluidSolverBackend::Gpu, 100, false, bHasRenderingRhi, bHasRenderingRhi ? 128 : 100 },
		{ ETDFluidSolverBackend::Gpu, 4096, false, bHasRenderingRhi, bHasRenderingRhi ? 2048 : 256 },
		{ ETDFluidSolverBackend::Gpu, 300, true, false, 256 },
		{ ETDFluidSolverBackend::Auto, 200, false, false, 200 },
	};

	for (const FTDFluidComponentCase& ComponentCase : ComponentCases)
	{
		FTDFluidSimulationConfig Config = MakeSmokeParityConfig(ComponentCase.RequestedResolution, ComponentCase.Backend);
		Config.bEnableWaterLayer = ComponentCase.bEnablesWaterLayer;
		const ATDFluidLiveActor* LiveActor = SpawnConfiguredLiveActor(*TestWorld.World, ParityAnchorLocation, Config);
		const FString CaseLabel = FString::Printf(TEXT("Component backend %d requested %d water %d"), static_cast<int32>(ComponentCase.Backend), ComponentCase.RequestedResolution, ComponentCase.bEnablesWaterLayer ? 1 : 0);
		if (!TestNotNull(*(CaseLabel + TEXT(" spawned")), LiveActor))
		{
			continue;
		}

		TestTrue(*(CaseLabel + TEXT(" backend")), LiveActor->LiveComponent->IsUsingGpuSolver() == ComponentCase.bExpectsGpu);
		TestTrue(*(CaseLabel + TEXT(" is initialized")), LiveActor->LiveComponent->IsSimulationInitialized());
		TestEqual(*(CaseLabel + TEXT(" simulation resolution")), static_cast<int32>(LiveActor->LiveComponent->GetSimulationResolution()), static_cast<int32>(ComponentCase.ExpectedResolution));
	}

	if (!bHasRenderingRhi)
	{
		AddInfo(TEXT("The null RHI is active: GPU component tiers were checked through the decision table only."));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuReadbackFieldMappingTest,
	"TDGame.FluidFX.Gpu.ReadbackFieldMappingMatchesCpuDownsample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuReadbackFieldMappingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;

	const int32 FieldResolution = FTDFluidReadbackField::GetResolution();
	TestEqual(TEXT("Readback field resolution is 64"), static_cast<int32>(FieldResolution), static_cast<int32>(64));

	FTDFluidReadbackField EmptyField;
	FIntPoint EmptyFieldCell;
	TestFalse(TEXT("An empty readback field is not valid"), EmptyField.IsValid());
	TestFalse(TEXT("An empty readback field maps no point"), EmptyField.TryMapPlanePointToFieldCell(FVector2D::ZeroVector, EmptyFieldCell));
	FVector2D EmptyCentroid;
	float EmptyAreaCm2 = 0.0f;
	TestFalse(TEXT("An empty readback field has no centroid"), EmptyField.ComputeDensityCentroid(false, EmptyCentroid, EmptyAreaCm2));

	const int32 SimulationResolutions[] = { 32, 64, 96, 128, 256 };
	for (const int32 SimulationResolution : SimulationResolutions)
	{
		const FString CaseLabel = FString::Printf(TEXT("Resolution %d"), SimulationResolution);
		TStrongObjectPtr<UTDFluidLiveComponent> Component(NewObject<UTDFluidLiveComponent>(GetTransientPackage()));
		FTDFluidGrid2DSolver& Solver = Component->GetMutableSolver();
		Solver.Initialize(MakeSmokeParityConfig(SimulationResolution, ETDFluidSolverBackend::Cpu), FVector2D(ParityAnchorLocation.X, ParityAnchorLocation.Y));
		RunSolverWithParityStamps(Solver, 30, ParityAnchorLocation);

		FTDFluidReadbackField Field;
		Field.DownsampleFromSolver(Component->GetSolver());
		if (!TestTrue(*(CaseLabel + TEXT(" downsampled field is valid")), Field.IsValid()))
		{
			continue;
		}

		TestTrue(*(CaseLabel + TEXT(" field window center is the solver center")), Field.WindowCenterPlaneCm.Equals(Solver.GetCenterWorld(), UE_DOUBLE_KINDA_SMALL_NUMBER));
		TestTrue(*(CaseLabel + TEXT(" field extent is the solver extent")), Field.ExtentCm.Equals(Solver.GetExtentCm(), UE_DOUBLE_KINDA_SMALL_NUMBER));

		int32 UnmappedCellCount = 0;
		int32 ExactCellMappingCount = 0;
		int32 FarCellMappingCount = 0;
		for (int32 CellY = 0; CellY < SimulationResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < SimulationResolution; ++CellX)
			{
				FIntPoint FieldCell;
				if (!Field.TryMapPlanePointToFieldCell(Component->CellToPlaneCenter(CellX, CellY), FieldCell))
				{
					++UnmappedCellCount;
					continue;
				}

				const int32 DistanceX = MeasureDistanceToContainingBlock(FieldCell.X, CellX, SimulationResolution);
				const int32 DistanceY = MeasureDistanceToContainingBlock(FieldCell.Y, CellY, SimulationResolution);
				ExactCellMappingCount += DistanceX == 0 && DistanceY == 0 ? 1 : 0;
				FarCellMappingCount += DistanceX > 1 || DistanceY > 1 ? 1 : 0;
			}
		}

		const int32 SimulationCellCount = SimulationResolution * SimulationResolution;
		AddInfo(FString::Printf(TEXT("TDFluidGpuReadbackMapping: resolution=%d exact=%d of %d"), SimulationResolution, ExactCellMappingCount, SimulationCellCount));
		TestEqual(*(CaseLabel + TEXT(" every simulation cell center maps into the field")), static_cast<int32>(UnmappedCellCount), static_cast<int32>(0));
		TestEqual(*(CaseLabel + TEXT(" every mapped field cell is within one cell of its readback block")), static_cast<int32>(FarCellMappingCount), static_cast<int32>(0));

		const FVector2D WindowMin = Solver.GetCenterWorld() - 0.5 * Solver.GetExtentCm();
		FIntPoint OutsideFieldCell;
		TestFalse(*(CaseLabel + TEXT(" a point left of the window maps to no field cell")), Field.TryMapPlanePointToFieldCell(WindowMin - FVector2D(1.0, 0.0), OutsideFieldCell));
		TestFalse(*(CaseLabel + TEXT(" a point above the window maps to no field cell")), Field.TryMapPlanePointToFieldCell(WindowMin + Solver.GetExtentCm() + FVector2D(0.0, 1.0), OutsideFieldCell));

		if (SimulationResolution == FieldResolution)
		{
			int32 ValueMismatchCount = 0;
			for (int32 CellY = 0; CellY < SimulationResolution; ++CellY)
			{
				for (int32 CellX = 0; CellX < SimulationResolution; ++CellX)
				{
					const FTDFluidCellSample SolverCell = Solver.SampleAtCell(CellX, CellY);
					const FTDFluidCellSample FieldCell = Field.SampleFieldCell(CellX, CellY);
					const bool bMatches = SolverCell.PrimaryDensity == FieldCell.PrimaryDensity
						&& SolverCell.SelectiveDensity == FieldCell.SelectiveDensity
						&& FVector2f(SolverCell.VelocityCmPerSec) == FVector2f(FieldCell.VelocityCmPerSec);
					ValueMismatchCount += bMatches ? 0 : 1;
				}
			}
			TestEqual(*(CaseLabel + TEXT(" field cells equal the solver cells when the resolutions match")), static_cast<int32>(ValueMismatchCount), static_cast<int32>(0));
		}

		const bool bHasEvenBlocks = SimulationResolution % FieldResolution == 0 || FieldResolution % SimulationResolution == 0;
		const double SimulationCellAreaCm2 = (Solver.GetExtentCm().X / SimulationResolution) * (Solver.GetExtentCm().Y / SimulationResolution);
		const double FieldCellAreaCm2 = (Field.ExtentCm.X / FieldResolution) * (Field.ExtentCm.Y / FieldResolution);
		const FVector2D FieldCellSizeCm = Field.ExtentCm / static_cast<double>(FieldResolution);
		for (const bool bUseSelectiveDensity : { false, true })
		{
			const FString ChannelLabel = CaseLabel + (bUseSelectiveDensity ? TEXT(" selective") : TEXT(" primary"));
			const double SolverMass = SumSolverDensity(Solver, bUseSelectiveDensity) * SimulationCellAreaCm2;
			const double FieldMass = SumFieldDensity(Field, bUseSelectiveDensity) * FieldCellAreaCm2;
			if (bHasEvenBlocks)
			{
				TestTrue(*(ChannelLabel + TEXT(" density mass is conserved by the downsample")), ComputeRelativeDifference(SolverMass, FieldMass) <= 1.0e-4);
			}

			FVector2D CpuCentroid;
			float CpuAreaCm2 = 0.0f;
			FVector2D FieldCentroid;
			float FieldAreaCm2 = 0.0f;
			const bool bHasCpuCentroid = Component->ComputeDensityCentroidPlane(bUseSelectiveDensity, CpuCentroid, CpuAreaCm2);
			const bool bHasFieldCentroid = Field.ComputeDensityCentroid(bUseSelectiveDensity, FieldCentroid, FieldAreaCm2);
			if (!TestTrue(*(ChannelLabel + TEXT(" CPU and field centroids exist")), bHasCpuCentroid && bHasFieldCentroid))
			{
				continue;
			}

			const FVector2D CentroidOffsetCm = FieldCentroid - CpuCentroid;
			AddInfo(FString::Printf(TEXT("TDFluidGpuReadbackCentroid: %s dx_field_cells=%.4f dy_field_cells=%.4f area_cpu=%.1f area_field=%.1f"), *ChannelLabel, CentroidOffsetCm.X / FieldCellSizeCm.X, CentroidOffsetCm.Y / FieldCellSizeCm.Y, CpuAreaCm2, FieldAreaCm2));
			TestTrue(*(ChannelLabel + TEXT(" field centroid X is within one field cell of the CPU centroid")), FMath::Abs(CentroidOffsetCm.X) <= FieldCellSizeCm.X);
			TestTrue(*(ChannelLabel + TEXT(" field centroid Y is within one field cell of the CPU centroid")), FMath::Abs(CentroidOffsetCm.Y) <= FieldCellSizeCm.Y);
			if (bHasEvenBlocks)
			{
				TestTrue(*(ChannelLabel + TEXT(" field centroid area matches the CPU area")), ComputeRelativeDifference(CpuAreaCm2, FieldAreaCm2) <= 1.0e-3);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuQueryFunctionsBitIdenticalTest,
	"TDGame.FluidFX.Gpu.QueryFunctionsAreBitIdenticalOnCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuQueryFunctionsBitIdenticalTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;

	for (const FTDFluidQueryGoldenScenario& Scenario : QueryGoldenScenarios)
	{
		const FTDFluidQueryScenarioResult Result = RunQueryGoldenScenario(Scenario);
		const FString ChecksumLine = FString::Printf(TEXT("TDFluidGpuQueryGolden: scenario=%s state=0x%08X pixels=0x%08X"), Scenario.Name, Result.StateChecksum, Result.PixelChecksum);
		AddInfo(ChecksumLine);

		const bool bStateMatches = TestEqual(*FString::Printf(TEXT("%s state checksum through query functions matches the golden value"), Scenario.Name), static_cast<uint32>(Result.StateChecksum), static_cast<uint32>(Scenario.ExpectedStateChecksum));
		const bool bPixelsMatch = TestEqual(*FString::Printf(TEXT("%s pixel checksum through query functions matches the golden value"), Scenario.Name), static_cast<uint32>(Result.PixelChecksum), static_cast<uint32>(Scenario.ExpectedPixelChecksum));
		if (!bStateMatches || !bPixelsMatch)
		{
			AddError(ChecksumLine);
		}

		TestTrue(*FString::Printf(TEXT("%s scalar query functions equal the solver"), Scenario.Name), Result.bScalarQueriesMatch);
		TestEqual(*FString::Printf(TEXT("%s compared every cell"), Scenario.Name), static_cast<int32>(Result.ComparedCellCount), static_cast<int32>(Scenario.Resolution * Scenario.Resolution));
		TestEqual(*FString::Printf(TEXT("%s CellToPlaneCenter is bit-identical to CellToWorldCenter"), Scenario.Name), static_cast<int32>(Result.CellCenterMismatchCount), static_cast<int32>(0));
		TestEqual(*FString::Printf(TEXT("%s SampleSimulationCell is bit-identical to SampleAtCell"), Scenario.Name), static_cast<int32>(Result.CellSampleMismatchCount), static_cast<int32>(0));
		TestEqual(*FString::Printf(TEXT("%s SampleFluidAtWorldLocation is bit-identical to SampleAtWorldPosition"), Scenario.Name), static_cast<int32>(Result.WorldSampleMismatchCount), static_cast<int32>(0));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuStatisticalParity128Test,
	"TDGame.FluidFXRhi.StatisticalParityWithCpu128",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuStatisticalParity128Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	return RunSmokeStatisticalParity(*this, TEXT("TDGame.FluidFXRhi.StatisticalParityWithCpu128"), 128);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuStatisticalParity256Test,
	"TDGame.FluidFXRhi.StatisticalParityWithCpu256",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuStatisticalParity256Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	return RunSmokeStatisticalParity(*this, TEXT("TDGame.FluidFXRhi.StatisticalParityWithCpu256"), 256);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuResolution512DivergenceTest,
	"TDGame.FluidFXRhi.Resolution512DivergenceResidual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuResolution512DivergenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.Resolution512DivergenceResidual")))
	{
		return false;
	}

	constexpr int32 CpuReferenceResolution = 128;
	constexpr int32 GpuMeasuredResolution = 512;
	const FTDFluidDivergenceRunPlan RunPlans[] = {
		{ TEXT("jacobi"), ForcedJacobiPressureSolverMode, TDFluidGpuSolverConstants::DefaultMultigridCycles, { 8, 16, 32 }, false },
		{ TEXT("multigrid"), AutomaticPressureSolverMode, 1, { 8 }, false },
		{ TEXT("multigrid"), AutomaticPressureSolverMode, 2, { 8 }, false },
		{ TEXT("multigrid"), AutomaticPressureSolverMode, TDFluidGpuSolverConstants::MaximumMultigridCycles, { 8 }, true },
	};

	double FirstCpuDivergenceL2 = -1.0;
	double CpuEncodingFloorBound = 0.0;
	double GpuEncodingFloorBound = 0.0;
	double DefaultCycleRatio = -1.0;
	double OracleDivergenceL2 = -1.0;
	double OracleRatio = -1.0;
	FString SmallestPassingSetting = TEXT("none");
	for (const FTDFluidDivergenceRunPlan& RunPlan : RunPlans)
	{
		const bool bIsMultigridRun = RunPlan.PressureSolverMode != ForcedJacobiPressureSolverMode;
		FTDFluidGpuPressureVariableScope PressureScope(RunPlan.PressureSolverMode, RunPlan.MultigridCycles);
		if (!PressureScope.IsApplied())
		{
			AddError(FString::Printf(TEXT("%s=%d and %s=%d could not be applied (variable missing or overridden by a higher priority setting)."), GpuPressureSolverVariableName, RunPlan.PressureSolverMode, GpuPressureCyclesVariableName, RunPlan.MultigridCycles));
			return false;
		}

		FTDFluidParityRunSettings Settings = MakeSmokeParitySettings(CpuReferenceResolution, GpuMeasuredResolution);
		Settings.bCapturesFinalVelocityGrids = true;
		Settings.GpuConfigs.Reset();
		for (const int32 PressureIterationCount : RunPlan.PressureIterationCounts)
		{
			FTDFluidSimulationConfig GpuConfig = MakeSmokeParityConfig(GpuMeasuredResolution, ETDFluidSolverBackend::Gpu);
			GpuConfig.PressureIterations = PressureIterationCount;
			Settings.GpuConfigs.Add(GpuConfig);
		}

		const FString RunLabel = FString::Printf(TEXT("Smoke512Divergence_%s%d"), RunPlan.SolverName, bIsMultigridRun ? RunPlan.MultigridCycles : 0);
		FTDFluidParityRunResult Result;
		if (!RunParityScenario(*this, RunLabel, Settings, Result))
		{
			return false;
		}

		const FTDFluidVelocityGrid& CpuPackedGrid = Result.FinalCpuPackedVelocityGrid;
		const FVector2D CpuCellSizeCm = ComputeSimulationCellSizeCm(Settings.CpuConfig, CpuReferenceResolution);
		const FTDFluidSimulationDivergence CpuDivergence = ComputeSimulationDivergence(CpuPackedGrid, CpuCellSizeCm);
		if (FirstCpuDivergenceL2 < 0.0)
		{
			const FTDFluidSimulationDivergence CpuSolverDivergence = ComputeSimulationDivergence(Result.FinalCpuSolverVelocityGrid, CpuCellSizeCm);
			FirstCpuDivergenceL2 = CpuDivergence.DivergenceL2;
			CpuEncodingFloorBound = ComputePackedVelocityDivergenceFloorBound(CpuCellSizeCm);
			GpuEncodingFloorBound = ComputePackedVelocityDivergenceFloorBound(ComputeSimulationCellSizeCm(Settings.CpuConfig, GpuMeasuredResolution));
			const FString ReferenceLine = FString::Printf(
				TEXT("TDFluidGpuDivergenceCurve: measure=simulation_resolution_packed_velocity steps=%d cpu_reference_iterations=%d cpu128_sim_div_l2=%.5f cpu128_solver_div_l2=%.5f cpu_excluded=%.4f cpu128_encoding_floor_bound=%.5f gpu512_encoding_floor_bound=%.5f cpu_rss_check=%.5f"),
				Result.ExecutedStepCount,
				Settings.CpuConfig.PressureIterations,
				CpuDivergence.DivergenceL2,
				CpuSolverDivergence.DivergenceL2,
				CpuDivergence.ComputeExcludedFraction(),
				CpuEncodingFloorBound,
				GpuEncodingFloorBound,
				FMath::Sqrt(FMath::Square(CpuSolverDivergence.DivergenceL2) + FMath::Square(RealisticEncodingFloorFactor * CpuEncodingFloorBound)));
			UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *ReferenceLine);
			AddInfo(ReferenceLine);
			TestEqual(TEXT("CPU reference packed velocity grid is 128 by 128"), static_cast<int32>(CpuPackedGrid.Resolution), static_cast<int32>(CpuReferenceResolution));
			TestTrue(TEXT("CPU 128 reference has sampled interior cells"), CpuDivergence.SampledCellCount > 0);
			TestTrue(TEXT("CPU 128 reference carries simulation-resolution divergence"), CpuDivergence.DivergenceL2 > 0.0);
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("%s CPU 128 reference is bit identical to the first run"), *RunLabel), CpuDivergence.DivergenceL2 == FirstCpuDivergenceL2);
		}

		for (int32 GpuIndex = 0; GpuIndex < Settings.GpuConfigs.Num(); ++GpuIndex)
		{
			const int32 PressureIterationCount = Settings.GpuConfigs[GpuIndex].PressureIterations;
			const FString SettingLabel = FString::Printf(TEXT("%s:%d"), RunPlan.SolverName, bIsMultigridRun ? RunPlan.MultigridCycles : PressureIterationCount);
			TestEqual(*FString::Printf(TEXT("%s GPU actor runs at 512"), *SettingLabel), static_cast<int32>(Result.GpuResolutions[GpuIndex]), static_cast<int32>(GpuMeasuredResolution));
			if (!Result.FinalGpuPackedVelocityGrids.IsValidIndex(GpuIndex))
			{
				AddError(FString::Printf(TEXT("%s has no final GPU velocity grid."), *SettingLabel));
				continue;
			}

			const FTDFluidVelocityGrid& GpuPackedGrid = Result.FinalGpuPackedVelocityGrids[GpuIndex];
			if (!TestEqual(*FString::Printf(TEXT("%s GPU velocity output was read back at 512 by 512"), *SettingLabel), static_cast<int32>(GpuPackedGrid.Resolution), static_cast<int32>(GpuMeasuredResolution)))
			{
				continue;
			}

			const FTDFluidSimulationDivergence GpuDivergence = ComputeSimulationDivergence(GpuPackedGrid, ComputeSimulationCellSizeCm(Settings.GpuConfigs[GpuIndex], GpuMeasuredResolution));
			TestTrue(*FString::Printf(TEXT("%s GPU 512 has sampled interior cells"), *SettingLabel), GpuDivergence.SampledCellCount > 0);
			const double DivergenceRatio = GpuDivergence.DivergenceL2 / FMath::Max(CpuDivergence.DivergenceL2, UE_DOUBLE_SMALL_NUMBER);
			const bool bPasses = GpuDivergence.SampledCellCount > 0 && DivergenceRatio <= DivergenceParityRatio;

			double ReadbackProxyRatio = -1.0;
			double KineticEnergyDifference = -1.0;
			double HistogramDistance = -1.0;
			const FTDFluidParityCapture& Capture = Result.CapturesPerGpuActor[GpuIndex][0];
			if (TestTrue(*FString::Printf(TEXT("%s captured a readback at or after frame %d"), *SettingLabel, ParityStepCount), Capture.bIsCaptured))
			{
				const FTDFluidFieldStatistics CpuReadbackStatistics = ComputeFieldStatistics(Capture.CpuField);
				const FTDFluidFieldStatistics GpuReadbackStatistics = ComputeFieldStatistics(Capture.GpuField);
				ReadbackProxyRatio = GpuReadbackStatistics.DivergenceL2 / FMath::Max(CpuReadbackStatistics.DivergenceL2, UE_DOUBLE_SMALL_NUMBER);
				KineticEnergyDifference = ComputeRelativeDifference(CpuReadbackStatistics.KineticEnergy, GpuReadbackStatistics.KineticEnergy);
				HistogramDistance = ComputeHistogramL1Distance(CpuReadbackStatistics, GpuReadbackStatistics);
			}

			const FString CurveLine = FString::Printf(
				TEXT("TDFluidGpuDivergenceCurve: solver=%s iterations=%d cycles=%d oracle=%d steps=%d gpu512_sim_div_l2=%.5f cpu128_sim_div_l2=%.5f ratio=%.3f gpu_excluded=%.4f readback64_proxy_ratio=%.3f readback64_ke_diff=%.4f readback64_hist_l1=%.4f passes=%d"),
				RunPlan.SolverName,
				bIsMultigridRun ? 0 : PressureIterationCount,
				bIsMultigridRun ? RunPlan.MultigridCycles : 0,
				RunPlan.bIsOracle ? 1 : 0,
				Result.ExecutedStepCount,
				GpuDivergence.DivergenceL2,
				CpuDivergence.DivergenceL2,
				DivergenceRatio,
				GpuDivergence.ComputeExcludedFraction(),
				ReadbackProxyRatio,
				KineticEnergyDifference,
				HistogramDistance,
				bPasses ? 1 : 0);
			UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *CurveLine);
			AddInfo(CurveLine);
			if (bPasses && SmallestPassingSetting == TEXT("none"))
			{
				SmallestPassingSetting = SettingLabel;
			}
			if (bIsMultigridRun && RunPlan.MultigridCycles == TDFluidGpuSolverConstants::DefaultMultigridCycles)
			{
				DefaultCycleRatio = DivergenceRatio;
			}
			if (RunPlan.bIsOracle)
			{
				OracleDivergenceL2 = GpuDivergence.DivergenceL2;
				OracleRatio = DivergenceRatio;
			}
		}
	}

	const double PassingDivergenceL2 = DivergenceParityRatio * FirstCpuDivergenceL2;
	const double RealisticGpuEncodingFloor = RealisticEncodingFloorFactor * GpuEncodingFloorBound;
	const FString FloorLine = FString::Printf(
		TEXT("TDFluidGpuDivergenceFloor: encoding_floor_bound512=%.5f encoding_floor_realistic512=%.5f allowance=%.5f oracle_sim_component=%.5f oracle_ratio=%.3f"),
		GpuEncodingFloorBound,
		RealisticGpuEncodingFloor,
		FMath::Sqrt(FMath::Max(FMath::Square(PassingDivergenceL2) - FMath::Square(RealisticGpuEncodingFloor), 0.0)),
		FMath::Sqrt(FMath::Max(FMath::Square(OracleDivergenceL2) - FMath::Square(RealisticGpuEncodingFloor), 0.0)),
		OracleRatio);
	UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *FloorLine);
	AddInfo(FloorLine);

	const bool bDefaultPasses = DefaultCycleRatio >= 0.0 && DefaultCycleRatio <= DivergenceParityRatio;
	const double EstimatedMegabytes = static_cast<double>(FTDFluidGpuSolverProxy::EstimateActorMemoryBytes(ETDFluidSimMode::FluidSim, GpuMeasuredResolution)) / (1024.0 * 1024.0);
	const FString SummaryLine = FString::Printf(
		TEXT("TDFluidGpuDivergenceCurve: smallest_passing=%s default_cycles=%d default_ratio=%.3f default_passes=%d multigrid_measured=1 estimated_actor_mib_512=%.2f"),
		*SmallestPassingSetting,
		TDFluidGpuSolverConstants::DefaultMultigridCycles,
		DefaultCycleRatio,
		bDefaultPasses ? 1 : 0,
		EstimatedMegabytes);
	UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *SummaryLine);
	AddInfo(SummaryLine);
	if (!bDefaultPasses)
	{
		const bool bIsFloorLimited = OracleRatio > DivergenceParityRatio;
		AddError(FString::Printf(TEXT("Multigrid with the default %d cycle(s) keeps the 512 divergence L2 at %.3f times the CPU 128 value, above %.1f%s."), TDFluidGpuSolverConstants::DefaultMultigridCycles, DefaultCycleRatio, DivergenceParityRatio, bIsFloorLimited ? TEXT(" (floor-limited: the 4 cycle oracle also fails)") : TEXT("")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuSlidingShiftParityTest,
	"TDGame.FluidFXRhi.SlidingShiftMatchesCpuStatistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuSlidingShiftParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.SlidingShiftMatchesCpuStatistics")))
	{
		return false;
	}

	FTDFluidParityRunSettings Settings = MakeSmokeParitySettings(128, 128);
	Settings.bFollowsAnchor = true;
	Settings.ResolveAnchorLocation = [](int32 StepIndex)
	{
		return ParityAnchorLocation + FVector(StepIndex * 3.7, StepIndex * -2.3, 0.0);
	};

	FTDFluidParityRunResult Result;
	if (!RunParityScenario(*this, TEXT("Smoke128Sliding"), Settings, Result))
	{
		return false;
	}

	const FTDFluidParityCapture& Capture = Result.CapturesPerGpuActor[0][0];
	const double WindowTravelCm = (Result.FinalCpuCenterWorld - Result.InitialCpuCenterWorld).Size();
	const FString ShiftLine = FString::Printf(TEXT("TDFluidGpuSliding: window_travel_cm=%.2f max_center_mismatch_cm=%.5f"), WindowTravelCm, Result.MaximumCenterMismatchCm);
	UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *ShiftLine);
	AddInfo(ShiftLine);
	TestTrue(TEXT("The CPU window slid during the scenario"), WindowTravelCm > 100.0);
	TestTrue(TEXT("The GPU window center follows the CPU rounding rule on every step"), Result.MaximumCenterMismatchCm <= WindowCenterMatchToleranceCm);
	if (Capture.bIsCaptured)
	{
		TestTrue(TEXT("The readback field window is the CPU window of the same frame"), Capture.GpuField.WindowCenterPlaneCm.Equals(Capture.CpuField.WindowCenterPlaneCm, WindowCenterMatchToleranceCm));
	}

	ExpectStatisticalParity(*this, TEXT("Smoke128Sliding"), Capture, ComputeSimulationCellSizeCm(Settings.CpuConfig, 128));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuSimplePainterParityTest,
	"TDGame.FluidFXRhi.SimplePainterParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuSimplePainterParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.SimplePainterParity")))
	{
		return false;
	}

	constexpr int32 WalkEndFrame = PainterWalkStepCount;
	constexpr int32 IdleEndFrame = PainterWalkStepCount + PainterIdleStepCount;
	constexpr int32 EraseEndFrame = PainterWalkStepCount + PainterIdleStepCount + PainterEraseStepCount;

	FTDFluidParityRunSettings Settings;
	Settings.CpuConfig = MakePainterParityConfig(ETDFluidSolverBackend::Cpu);
	Settings.GpuConfigs.Add(MakePainterParityConfig(ETDFluidSolverBackend::Gpu));
	Settings.CaptureFrames = { WalkEndFrame, IdleEndFrame, EraseEndFrame };
	Settings.bCapturesFinalOutputPixels = true;
	Settings.ResolveAnchorLocation = [](int32)
	{
		return ParityAnchorLocation;
	};
	Settings.AppendStamps = [](int32 StepIndex, const FVector& AnchorLocation, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		AppendPainterStamps(StepIndex, AnchorLocation, OutStamps);
	};

	FTDFluidParityRunResult Result;
	if (!RunParityScenario(*this, TEXT("Painter128"), Settings, Result))
	{
		return false;
	}

	ExpectGpuOutputTargets(*this, TEXT("Painter128"), Result.GpuOutputTargets[0], 128, 4);
	const TArray<FTDFluidParityCapture>& Captures = Result.CapturesPerGpuActor[0];
	for (const FTDFluidParityCapture& Capture : Captures)
	{
		TestTrue(*FString::Printf(TEXT("Painter captured a readback at or after frame %d"), Capture.TargetFrame), Capture.bIsCaptured);
	}
	if (!Captures[0].bIsCaptured || !Captures[1].bIsCaptured || !Captures[2].bIsCaptured)
	{
		return false;
	}

	const FVector2D PathCenter(ParityAnchorLocation.X, ParityAnchorLocation.Y);
	const FVector2D PathDirection = FVector2D(ResolvePainterPathDelta().X, ResolvePainterPathDelta().Y).GetSafeNormal();
	const FTDFluidFieldStatistics WalkCpu = ComputeFieldStatistics(Captures[0].CpuField);
	const FTDFluidFieldStatistics WalkGpu = ComputeFieldStatistics(Captures[0].GpuField);
	const FTDFluidFieldStatistics IdleCpu = ComputeFieldStatistics(Captures[1].CpuField);
	const FTDFluidFieldStatistics IdleGpu = ComputeFieldStatistics(Captures[1].GpuField);
	const FTDFluidFieldStatistics EraseCpu = ComputeFieldStatistics(Captures[2].CpuField);
	const FTDFluidFieldStatistics EraseGpu = ComputeFieldStatistics(Captures[2].GpuField);
	const double TrailLengthCpuCm = MeasureTrailLengthCm(Captures[0].CpuField, PathCenter, PathDirection);
	const double TrailLengthGpuCm = MeasureTrailLengthCm(Captures[0].GpuField, PathCenter, PathDirection);
	const double DissipationRatioCpu = IdleCpu.TotalDensity / FMath::Max(WalkCpu.TotalDensity, UE_DOUBLE_SMALL_NUMBER);
	const double DissipationRatioGpu = IdleGpu.TotalDensity / FMath::Max(WalkGpu.TotalDensity, UE_DOUBLE_SMALL_NUMBER);
	const double EraseDensityDifference = ComputeRelativeDifference(EraseCpu.TotalDensity, EraseGpu.TotalDensity);

	const FString PainterLine = FString::Printf(
		TEXT("TDFluidGpuPainterParity: walk_frame=%u density_cpu=%.1f density_gpu=%.1f density_diff=%.4f trail_cpu_cm=%.1f trail_gpu_cm=%.1f trail_diff=%.4f idle_frame=%u dissipation_cpu=%.4f dissipation_gpu=%.4f dissipation_diff=%.4f"),
		Captures[0].CapturedFrame,
		WalkCpu.TotalDensity,
		WalkGpu.TotalDensity,
		ComputeRelativeDifference(WalkCpu.TotalDensity, WalkGpu.TotalDensity),
		TrailLengthCpuCm,
		TrailLengthGpuCm,
		ComputeRelativeDifference(TrailLengthCpuCm, TrailLengthGpuCm),
		Captures[1].CapturedFrame,
		DissipationRatioCpu,
		DissipationRatioGpu,
		ComputeRelativeDifference(DissipationRatioCpu, DissipationRatioGpu));
	UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *PainterLine);
	AddInfo(PainterLine);

	TestTrue(TEXT("Painter CPU trail carries density"), WalkCpu.TotalDensity > UE_DOUBLE_KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Painter CPU trail has a length"), TrailLengthCpuCm > 1000.0);
	TestTrue(TEXT("Painter CPU density dissipated while idle"), DissipationRatioCpu < 0.98);
	TestTrue(TEXT("Painter trail density matches the CPU within 3%"), ComputeRelativeDifference(WalkCpu.TotalDensity, WalkGpu.TotalDensity) <= DensityParityTolerance);
	TestTrue(TEXT("Painter trail length matches the CPU within 3%"), ComputeRelativeDifference(TrailLengthCpuCm, TrailLengthGpuCm) <= DensityParityTolerance);
	TestTrue(TEXT("Painter dissipation matches the CPU within 3%"), ComputeRelativeDifference(DissipationRatioCpu, DissipationRatioGpu) <= DensityParityTolerance);

	const double SurfaceDifference = Result.FinalGpuDensityPressurePixels.IsEmpty() ? -1.0 : MeasureMeanChannelDifference(Result.FinalCpuDensityPressurePixels, Result.FinalGpuDensityPressurePixels[0], true);
	const double DensityPixelDifference = Result.FinalGpuDensityPressurePixels.IsEmpty() ? -1.0 : MeasureMeanChannelDifference(Result.FinalCpuDensityPressurePixels, Result.FinalGpuDensityPressurePixels[0], false);
	const bool bEraseDensityAccepted = EraseDensityDifference <= DensityParityTolerance;
	const FString EraseLine = FString::Printf(
		TEXT("TDFluidGpuPainterErase: frame=%u density_cpu=%.1f density_gpu=%.1f density_diff=%.4f density_verdict=%s final_step=%d mean_abs_r=%.5f mean_abs_g=%.5f g_verdict=known_difference_recorded"),
		Captures[2].CapturedFrame,
		EraseCpu.TotalDensity,
		EraseGpu.TotalDensity,
		EraseDensityDifference,
		bEraseDensityAccepted ? TEXT("accepted") : TEXT("rejected"),
		Result.ExecutedStepCount,
		DensityPixelDifference,
		SurfaceDifference);
	UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *EraseLine);
	AddInfo(EraseLine);
	TestTrue(TEXT("Painter erased density is below the trail density"), EraseCpu.TotalDensity < IdleCpu.TotalDensity);
	TestTrue(TEXT("Painter erase density matches the CPU within 3%"), bEraseDensityAccepted);
	if (SurfaceDifference < 0.0)
	{
		AddWarning(TEXT("Painter GPU output pixels could not be read, so the erase surface (G) difference was not recorded."));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuObstacleBoundaryParityTest,
	"TDGame.FluidFXRhi.ObstacleBoundaryParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuObstacleBoundaryParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.ObstacleBoundaryParity")))
	{
		return false;
	}

	FTDFluidParityRunSettings Settings = MakeSmokeParitySettings(128, 128);
	Settings.Obstacles = MakeParityObstacles(ParityAnchorLocation);

	FTDFluidParityRunResult Result;
	if (!RunParityScenario(*this, TEXT("Smoke128Obstacles"), Settings, Result))
	{
		return false;
	}

	const FTDFluidParityCapture& Capture = Result.CapturesPerGpuActor[0][0];
	ExpectStatisticalParity(*this, TEXT("Smoke128Obstacles"), Capture, ComputeSimulationCellSizeCm(Settings.CpuConfig, 128));
	if (!Capture.bIsCaptured)
	{
		return false;
	}

	const FTDFluidObstacleRegionStatistics CpuRegions = MeasureObstacleRegions(Capture.CpuField, Settings.Obstacles);
	const FTDFluidObstacleRegionStatistics GpuRegions = MeasureObstacleRegions(Capture.GpuField, Settings.Obstacles);
	const FString ObstacleLine = FString::Printf(
		TEXT("TDFluidGpuObstacleParity: deep_cells=%d deep_density_cpu=%.5f deep_density_gpu=%.5f deep_speed_cpu=%.3f deep_speed_gpu=%.3f band_cells=%d band_density_cpu=%.4f band_density_gpu=%.4f band_speed_cpu=%.2f band_speed_gpu=%.2f"),
		GpuRegions.DeepInsideCellCount,
		CpuRegions.DeepInsideMeanDensity,
		GpuRegions.DeepInsideMeanDensity,
		CpuRegions.DeepInsideMeanSpeed,
		GpuRegions.DeepInsideMeanSpeed,
		GpuRegions.BandCellCount,
		CpuRegions.BandMeanDensity,
		GpuRegions.BandMeanDensity,
		CpuRegions.BandMeanSpeed,
		GpuRegions.BandMeanSpeed);
	UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *ObstacleLine);
	AddInfo(ObstacleLine);

	TestTrue(TEXT("The obstacle scenario has field cells deep inside obstacles"), GpuRegions.DeepInsideCellCount > 0);
	TestTrue(TEXT("The obstacle scenario has field cells in the boundary band"), GpuRegions.BandCellCount > 0);
	TestTrue(TEXT("GPU density deep inside obstacles stays within 0.02 of the CPU"), GpuRegions.DeepInsideMeanDensity <= CpuRegions.DeepInsideMeanDensity + 0.02);
	TestTrue(TEXT("GPU speed deep inside obstacles stays within 5 cm/s of the CPU"), GpuRegions.DeepInsideMeanSpeed <= CpuRegions.DeepInsideMeanSpeed + 5.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuReadbackLatencyTest,
	"TDGame.FluidFXRhi.ReadbackLatencyAtMostThreeFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuReadbackLatencyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.ReadbackLatencyAtMostThreeFrames")))
	{
		return false;
	}

	FTDFluidSolverBackendVariableScope BackendScope(0);
	FTDFluidGpuBackendTestWorld TestWorld;
	const UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
	if (!TestNotNull(TEXT("Latency world has a fluid subsystem"), FluidSubsystem))
	{
		return false;
	}

	const ATDFluidLiveActor* GpuActor = SpawnConfiguredLiveActor(*TestWorld.World, ParityAnchorLocation, MakeSmokeParityConfig(128, ETDFluidSolverBackend::Gpu));
	if (!TestNotNull(TEXT("Spawned the latency GPU actor"), GpuActor) || !TestTrue(TEXT("Latency actor uses the GPU solver"), GpuActor->LiveComponent->IsUsingGpuSolver()))
	{
		return false;
	}

	TestFalse(TEXT("The readback field is empty before the first GPU step"), GpuActor->LiveComponent->GetGpuReadbackField().IsValid());
	const FTDFluidLatencyTrace PacedTrace = TraceReadbackLatency(TestWorld, *FluidSubsystem, *GpuActor, 0, true);
	const FTDFluidLatencyTrace UnpacedTrace = TraceReadbackLatency(TestWorld, *FluidSubsystem, *GpuActor, LatencyTraceStepCount, false);

	const FString LatencyLine = FString::Printf(
		TEXT("TDFluidGpuReadbackLatency: paced_first_valid_step=%d paced_min=%d paced_max=%d paced_over_budget=%d unpaced_min=%d unpaced_max=%d unpaced_over_budget=%d unpaced_field_frames=%u..%u"),
		PacedTrace.FirstValidStep,
		PacedTrace.MinimumLatency,
		PacedTrace.MaximumLatency,
		PacedTrace.OverBudgetCount,
		UnpacedTrace.MinimumLatency,
		UnpacedTrace.MaximumLatency,
		UnpacedTrace.OverBudgetCount,
		UnpacedTrace.FirstObservedFieldFrame,
		UnpacedTrace.LastObservedFieldFrame);
	UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *LatencyLine);
	AddInfo(LatencyLine);

	TestTrue(TEXT("The first readback arrives within three frames when the GPU keeps pace"), PacedTrace.FirstValidStep != INDEX_NONE && PacedTrace.FirstValidStep <= MaximumReadbackLatencyFrames + 1);
	TestTrue(TEXT("The readback never comes from the current or a future frame"), PacedTrace.MinimumLatency >= 1);
	TestTrue(TEXT("The readback latency is at most three frames when the GPU keeps pace"), PacedTrace.MaximumLatency <= MaximumReadbackLatencyFrames);
	TestEqual(TEXT("The readback field stays valid after the first readback"), static_cast<int32>(PacedTrace.InvalidAfterFirstValidCount), static_cast<int32>(0));
	TestTrue(TEXT("Readback frame numbers never go backwards"), PacedTrace.bFrameNumbersAreMonotonic && UnpacedTrace.bFrameNumbersAreMonotonic);
	TestTrue(TEXT("Readbacks keep advancing without an explicit GPU wait"), UnpacedTrace.LastObservedFieldFrame > PacedTrace.LastObservedFieldFrame);
	return true;
}

namespace TDFluidGpuBackendTestPrivate
{
	constexpr int32 HeatHistogramBinCount = 16;
	constexpr float HeatPresenceThreshold = 0.01f;
	constexpr double HeatTotalParityTolerance = 0.03;
	constexpr double HeatHistogramParityTolerance = 0.05;
	constexpr float HeatParityStampStrength = 0.8f;
	constexpr double TransmittanceAbsoluteTolerance = 2.0e-3;
	constexpr double TransmittanceRelativeTolerance = 1.0e-2;
	constexpr double TransmittanceWithinToleranceFraction = 0.99;

	struct FTDFluidHeatFieldStatistics
	{
		double TotalHeat = 0.0;
		FVector2D CentroidCells = FVector2D::ZeroVector;
		double Histogram[HeatHistogramBinCount] = {};
		bool bHasHeat = false;
	};

	FTDFluidHeatFieldStatistics ComputeHeatStatistics(TConstArrayView<FFloat16Color> AuxPixels, int32 Resolution)
	{
		FTDFluidHeatFieldStatistics Statistics;
		if (Resolution <= 0 || AuxPixels.Num() != Resolution * Resolution)
		{
			return Statistics;
		}

		int32 HeatCellCount = 0;
		FVector2D WeightedCells = FVector2D::ZeroVector;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const float CellHeat = AuxPixels[CellY * Resolution + CellX].R.GetFloat();
				Statistics.TotalHeat += CellHeat;
				WeightedCells += FVector2D(CellX, CellY) * CellHeat;
				if (CellHeat <= HeatPresenceThreshold)
				{
					continue;
				}

				const int32 Bin = FMath::Clamp(static_cast<int32>(CellHeat * HeatHistogramBinCount), 0, HeatHistogramBinCount - 1);
				Statistics.Histogram[Bin] += 1.0;
				++HeatCellCount;
			}
		}

		Statistics.bHasHeat = Statistics.TotalHeat > UE_DOUBLE_KINDA_SMALL_NUMBER && HeatCellCount > 0;
		if (!Statistics.bHasHeat)
		{
			return Statistics;
		}

		Statistics.CentroidCells = WeightedCells / Statistics.TotalHeat;
		for (double& BinValue : Statistics.Histogram)
		{
			BinValue /= HeatCellCount;
		}
		return Statistics;
	}

	void ExpectHeatParity(FAutomationTestBase& Test, const FString& Label, TConstArrayView<FFloat16Color> CpuAuxPixels, TConstArrayView<FFloat16Color> GpuAuxPixels, int32 Resolution)
	{
		const FTDFluidHeatFieldStatistics Cpu = ComputeHeatStatistics(CpuAuxPixels, Resolution);
		const FTDFluidHeatFieldStatistics Gpu = ComputeHeatStatistics(GpuAuxPixels, Resolution);
		double HistogramDistance = 0.0;
		for (int32 Bin = 0; Bin < HeatHistogramBinCount; ++Bin)
		{
			HistogramDistance += FMath::Abs(Cpu.Histogram[Bin] - Gpu.Histogram[Bin]);
		}
		const double TotalDifference = ComputeRelativeDifference(Cpu.TotalHeat, Gpu.TotalHeat);
		const FVector2D CentroidOffset = Gpu.CentroidCells - Cpu.CentroidCells;
		const FString Line = FString::Printf(TEXT("TDFluidGpuHeatParity: case=%s heat_cpu=%.3f heat_gpu=%.3f heat_diff=%.4f centroid_dx_cells=%.3f centroid_dy_cells=%.3f hist_l1=%.4f"), *Label, Cpu.TotalHeat, Gpu.TotalHeat, TotalDifference, CentroidOffset.X, CentroidOffset.Y, HistogramDistance);
		UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		Test.TestTrue(*(Label + TEXT(" both backends carry heat")), Cpu.bHasHeat && Gpu.bHasHeat);
		Test.TestTrue(*(Label + TEXT(" total heat matches the CPU within 3%")), TotalDifference <= HeatTotalParityTolerance);
		Test.TestTrue(*(Label + TEXT(" heat centroid X is within one cell")), FMath::Abs(CentroidOffset.X) <= 1.0);
		Test.TestTrue(*(Label + TEXT(" heat centroid Y is within one cell")), FMath::Abs(CentroidOffset.Y) <= 1.0);
		Test.TestTrue(*(Label + TEXT(" heat histogram L1 distance is at most 0.05")), HistogramDistance <= HeatHistogramParityTolerance);
	}

	void EnableParityHeat(FTDFluidSimulationConfig& Config)
	{
		Config.bEnableHeatChannel = true;
		Config.HeatCoolingPerSec = 1.5f;
		Config.HeatBuoyancyCmPerSec2 = 300.0f;
	}

	FTDFluidParityRunSettings MakeHeatParitySettings(int32 Resolution)
	{
		FTDFluidParityRunSettings Settings = MakeSmokeParitySettings(Resolution, Resolution);
		EnableParityHeat(Settings.CpuConfig);
		for (FTDFluidSimulationConfig& GpuConfig : Settings.GpuConfigs)
		{
			EnableParityHeat(GpuConfig);
		}
		const double StampExtentCm = Settings.CpuConfig.SimExtentCm.X;
		Settings.AppendStamps = [StampExtentCm](int32 StepIndex, const FVector& AnchorLocation, TArray<FTDFluidBrushStamp>& OutStamps)
		{
			AppendParityStamps(StepIndex, AnchorLocation, StampExtentCm, OutStamps);
			for (FTDFluidBrushStamp& Stamp : OutStamps)
			{
				Stamp.HeatStrength = HeatParityStampStrength;
			}
		};
		return Settings;
	}

	void AddHeatInspection(FAutomationTestBase& Test, const FString& Label, int32 Resolution, FTDFluidParityRunSettings& InOutSettings)
	{
		InOutSettings.InspectFinalActors = [&Test, Label, Resolution](const ATDFluidLiveActor& CpuActor, const TArray<ATDFluidLiveActor*>& GpuActors)
		{
			TArray<FFloat16Color> GpuAuxPixels;
			const bool bHasGpuAux = !GpuActors.IsEmpty() && ReadOutputRenderTargetPixels(GpuActors[0]->LiveComponent->GetAuxTexture(), GpuAuxPixels);
			if (!Test.TestTrue(*(Label + TEXT(" read the GPU aux output")), bHasGpuAux))
			{
				return;
			}
			ExpectHeatParity(Test, Label, CpuActor.LiveComponent->GetPackedAuxPixels(), GpuAuxPixels, Resolution);
		};
	}

	void ApplyToAllConfigs(FTDFluidParityRunSettings& InOutSettings, TFunctionRef<void(FTDFluidSimulationConfig&)> Modify)
	{
		Modify(InOutSettings.CpuConfig);
		for (FTDFluidSimulationConfig& GpuConfig : InOutSettings.GpuConfigs)
		{
			Modify(GpuConfig);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuHeatParityTest,
	"TDGame.FluidFXRhi.Heat.CpuGpuParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuHeatParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.Heat.CpuGpuParity")))
	{
		return false;
	}

	FTDFluidParityRunSettings Settings = MakeHeatParitySettings(128);
	AddHeatInspection(*this, TEXT("Heat128"), 128, Settings);
	FTDFluidParityRunResult Result;
	if (!RunParityScenario(*this, TEXT("Heat128"), Settings, Result))
	{
		return false;
	}

	ExpectStatisticalParity(*this, TEXT("Heat128"), Result.CapturesPerGpuActor[0][0], ComputeSimulationCellSizeCm(Settings.CpuConfig, 128));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuHeatShiftParityTest,
	"TDGame.FluidFXRhi.Heat.ShiftParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuHeatShiftParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.Heat.ShiftParity")))
	{
		return false;
	}

	FTDFluidParityRunSettings Settings = MakeHeatParitySettings(128);
	Settings.bFollowsAnchor = true;
	Settings.ResolveAnchorLocation = [](int32 StepIndex)
	{
		return ParityAnchorLocation + FVector(StepIndex * 3.7, StepIndex * -2.3, 0.0);
	};
	AddHeatInspection(*this, TEXT("Heat128Sliding"), 128, Settings);
	FTDFluidParityRunResult Result;
	if (!RunParityScenario(*this, TEXT("Heat128Sliding"), Settings, Result))
	{
		return false;
	}

	TestTrue(TEXT("The heat window slid during the scenario"), (Result.FinalCpuCenterWorld - Result.InitialCpuCenterWorld).Size() > 100.0);
	ExpectStatisticalParity(*this, TEXT("Heat128Sliding"), Result.CapturesPerGpuActor[0][0], ComputeSimulationCellSizeCm(Settings.CpuConfig, 128));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuEdgeBounceParityTest,
	"TDGame.FluidFXRhi.Bounds.EdgeBounceParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuEdgeBounceParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.Bounds.EdgeBounceParity")))
	{
		return false;
	}

	FTDFluidParityRunSettings Settings = MakeSmokeParitySettings(128, 128);
	ApplyToAllConfigs(Settings, [](FTDFluidSimulationConfig& Config)
	{
		Config.bReflectsAtBoundaries = true;
		Config.EdgeBounciness = 0.6f;
		Config.BorderFadeExponent = 2.0f;
	});
	FTDFluidParityRunResult Result;
	if (!RunParityScenario(*this, TEXT("EdgeBounce128"), Settings, Result))
	{
		return false;
	}

	ExpectStatisticalParity(*this, TEXT("EdgeBounce128"), Result.CapturesPerGpuActor[0][0], ComputeSimulationCellSizeCm(Settings.CpuConfig, 128));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuVortexParityTest,
	"TDGame.FluidFXRhi.Bounds.VortexParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuVortexParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.Bounds.VortexParity")))
	{
		return false;
	}

	FTDFluidParityRunSettings Settings = MakeSmokeParitySettings(128, 128);
	ApplyToAllConfigs(Settings, [](FTDFluidSimulationConfig& Config)
	{
		Config.VortexTangentialAccelerationCmPerSec2 = 400.0f;
		Config.VortexRadialDriftCmPerSec = 40.0f;
		Config.VortexRadiusRatio = 0.3f;
	});
	FTDFluidParityRunResult Result;
	if (!RunParityScenario(*this, TEXT("Vortex128"), Settings, Result))
	{
		return false;
	}

	ExpectStatisticalParity(*this, TEXT("Vortex128"), Result.CapturesPerGpuActor[0][0], ComputeSimulationCellSizeCm(Settings.CpuConfig, 128));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuAuxOutputTest,
	"TDGame.FluidFXRhi.AuxOutput.WrittenOnlyWhenEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuAuxOutputTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.AuxOutput.WrittenOnlyWhenEnabled")))
	{
		return false;
	}

	FTDFluidParityRunSettings HeatOffSettings = MakeSmokeParitySettings(128, 128);
	HeatOffSettings.InspectFinalActors = [this](const ATDFluidLiveActor& CpuActor, const TArray<ATDFluidLiveActor*>& GpuActors)
	{
		TestTrue(TEXT("Heat off leaves the GPU aux target unallocated"), GpuActors.Num() == 1 && GpuActors[0]->LiveComponent->GetAuxTexture() == nullptr);
		TestTrue(TEXT("Heat off leaves the CPU aux pixels empty"), CpuActor.LiveComponent->GetPackedAuxPixels().IsEmpty());
	};
	FTDFluidParityRunResult HeatOffResult;
	RunParityScenario(*this, TEXT("AuxOff"), HeatOffSettings, HeatOffResult);

	FTDFluidParityRunSettings HeatOnSettings = MakeHeatParitySettings(128);
	HeatOnSettings.InspectFinalActors = [this](const ATDFluidLiveActor& CpuActor, const TArray<ATDFluidLiveActor*>& GpuActors)
	{
		TArray<FFloat16Color> GpuAuxPixels;
		const bool bHasAux = GpuActors.Num() == 1 && ReadOutputRenderTargetPixels(GpuActors[0]->LiveComponent->GetAuxTexture(), GpuAuxPixels);
		TestTrue(TEXT("Heat on allocates and writes the GPU aux target"), bHasAux);
		float MaximumHeat = 0.0f;
		float MaximumReserved = 0.0f;
		for (const FFloat16Color& Pixel : GpuAuxPixels)
		{
			MaximumHeat = FMath::Max(MaximumHeat, Pixel.R.GetFloat());
			MaximumReserved = FMath::Max(MaximumReserved, FMath::Max(FMath::Abs(Pixel.G.GetFloat()), FMath::Max(FMath::Abs(Pixel.B.GetFloat()), FMath::Abs(Pixel.A.GetFloat()))));
		}
		TestTrue(TEXT("Aux R carries heat"), MaximumHeat > 0.05f);
		TestEqual(TEXT("Aux G, B and A stay reserved"), MaximumReserved, 0.0f);
	};
	FTDFluidParityRunResult HeatOnResult;
	RunParityScenario(*this, TEXT("AuxOn"), HeatOnSettings, HeatOnResult);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuTransmittanceParityTest,
	"TDGame.FluidFXRhi.Transmittance.CpuGpuParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuTransmittanceParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.Transmittance.CpuGpuParity")))
	{
		return false;
	}

	UTDFluidPresetAsset* SmokePreset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::CampfireSmoke);
	TStrongObjectPtr<UTDFluidPresetAsset> PresetGuard(SmokePreset);
	FTDFluidParityRunSettings Settings = MakeSmokeParitySettings(128, 128);
	Settings.ConfigureActor = [SmokePreset](ATDFluidLiveActor& LiveActor)
	{
		LiveActor.LiveComponent->ActivePreset = SmokePreset;
		APointLight* Light = LiveActor.GetWorld()->SpawnActor<APointLight>(LiveActor.GetActorLocation() + FVector(-250.0, 40.0, 120.0), FRotator::ZeroRotator);
		LiveActor.VolumeLightActor = Light;
	};
	Settings.InspectFinalActors = [this](const ATDFluidLiveActor& CpuActor, const TArray<ATDFluidLiveActor*>& GpuActors)
	{
		TestFalse(TEXT("The CPU actor computed transmittance"), CpuActor.LiveComponent->GetPackedTransmittancePixels().IsEmpty());
		if (!TestEqual(TEXT("One GPU actor"), GpuActors.Num(), 1))
		{
			return;
		}

		const UTDFluidLiveComponent& GpuComponent = *GpuActors[0]->LiveComponent;
		TArray<FFloat16Color> GpuDensityPixels;
		TArray<FFloat16Color> GpuTransmittancePixels;
		FTDFluidTransmittanceInputs Inputs;
		const bool bHasInputs = ReadGpuDensityPressurePixels(GpuComponent, GpuDensityPixels)
			&& ReadOutputRenderTargetPixels(GpuComponent.GetTransmittanceTexture(), GpuTransmittancePixels)
			&& GpuComponent.BuildTransmittanceInputs(Inputs);
		if (!TestTrue(TEXT("Read the GPU density, transmittance and inputs"), bHasInputs))
		{
			return;
		}

		TArray<FFloat16Color> CpuFromGpuDensity;
		TDFluidVolumeTransmittance::ComputePixels(Inputs, GpuDensityPixels, CpuFromGpuDensity);
		if (!TestEqual(TEXT("Transmittance sizes match"), GpuTransmittancePixels.Num(), CpuFromGpuDensity.Num()))
		{
			return;
		}

		int32 WithinCount = 0;
		int32 ChannelCount = 0;
		double MaximumDifference = 0.0;
		double MaximumDepth = 0.0;
		for (int32 PixelIndex = 0; PixelIndex < CpuFromGpuDensity.Num(); ++PixelIndex)
		{
			const FLinearColor Cpu(CpuFromGpuDensity[PixelIndex]);
			const FLinearColor Gpu(GpuTransmittancePixels[PixelIndex]);
			for (int32 Channel = 0; Channel < 4; ++Channel)
			{
				const double CpuDepth = Cpu.Component(Channel);
				const double Difference = FMath::Abs(CpuDepth - Gpu.Component(Channel));
				MaximumDifference = FMath::Max(MaximumDifference, Difference);
				MaximumDepth = FMath::Max(MaximumDepth, CpuDepth);
				WithinCount += Difference <= TransmittanceAbsoluteTolerance + TransmittanceRelativeTolerance * CpuDepth ? 1 : 0;
				++ChannelCount;
			}
		}
		const double WithinFraction = static_cast<double>(WithinCount) / FMath::Max(ChannelCount, 1);
		AddInfo(FString::Printf(TEXT("TDFluidGpuTransmittanceParity: max_depth=%.4f max_diff=%.5f within_fraction=%.4f"), MaximumDepth, MaximumDifference, WithinFraction));
		TestTrue(TEXT("The smoke casts a measurable optical depth"), MaximumDepth > 0.01);
		TestTrue(TEXT("At least 99% of GPU optical depths match the CPU reference on the same input"), WithinFraction >= TransmittanceWithinToleranceFraction);
	};
	FTDFluidParityRunResult Result;
	RunParityScenario(*this, TEXT("Transmittance128"), Settings, Result);
	return true;
}

namespace TDFluidGpuBackendTestPrivate
{
	const FVector GpuReanchorAnchorLocation(1500.0, 2300.0, 120.0);
	constexpr int32 GpuReanchorStampStepCount = 3;
	constexpr int32 GpuReanchorWarmupStepCount = 10;
	constexpr int32 GpuReanchorSettleStepCount = 4;
	constexpr double GpuReanchorMinimumAreaRatio = 0.9;
	constexpr double GpuReanchorMinimumOffsetCm = 100.0;
	constexpr double GpuReanchorPivotToleranceCm = 1.0e-5;

	struct FTDFluidGpuReanchorRun
	{
		bool bIsValid = false;
		bool bHasReadbackAfter = false;
		int32 GenerationBefore = 0;
		int32 GenerationAfter = 0;
		FVector2D AnchorSolverPointBefore = FVector2D::ZeroVector;
		FVector2D AnchorSolverPointAfter = FVector2D::ZeroVector;
		FVector2D WindowCenterBefore = FVector2D::ZeroVector;
		FVector2D WindowCenterAfter = FVector2D::ZeroVector;
		FVector2D PlaneCoordinateOffset = FVector2D::ZeroVector;
		float DensityAreaBeforeCm2 = 0.0f;
		float DensityAreaAfterCm2 = 0.0f;
	};

	FTDFluidGpuReanchorRun RunGpuReanchorScenario(FAutomationTestBase& Test, const FRotator& FinalViewRotation)
	{
		FTDFluidGpuReanchorRun Run;
		FTDFluidGpuBackendTestWorld TestWorld;
		UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
		if (!Test.TestNotNull(TEXT("Reanchor world has a fluid subsystem"), FluidSubsystem))
		{
			return Run;
		}
		FluidSubsystem->SetPlaneViewRotationOverrideForTests(FRotator(-60.0, 0.0, 0.0));

		UTDFluidPresetAsset* PlanePreset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::Custom);
		TStrongObjectPtr<UTDFluidPresetAsset> PresetGuard(PlanePreset);
		PlanePreset->PlaneSettings.Orientation = ETDFluidPlaneOrientation::CameraFacing;

		FTDFluidSimulationConfig Config = MakeSmokeParityConfig(128, ETDFluidSolverBackend::Gpu);
		Config.DensityDissipation = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		ATDFluidLiveActor* GpuActor = SpawnConfiguredLiveActor(*TestWorld.World, GpuReanchorAnchorLocation, Config);
		if (!Test.TestNotNull(TEXT("Spawned the reanchor GPU actor"), GpuActor) || !Test.TestTrue(TEXT("The reanchor actor uses the GPU solver"), GpuActor->LiveComponent->IsUsingGpuSolver()))
		{
			return Run;
		}
		GpuActor->LiveComponent->ActivePreset = PlanePreset;
		TestWorld.StepWorld(BackendTestStepSeconds, true);

		UTDFluidLiveComponent& Component = *GpuActor->LiveComponent;
		FTDFluidBrushStamp AnchorStamp;
		AnchorStamp.WorldLocation = GpuReanchorAnchorLocation;
		AnchorStamp.RadiusCm = 80.0f;
		AnchorStamp.DensityStrength = 0.9f;
		for (int32 StepIndex = 0; StepIndex < GpuReanchorWarmupStepCount; ++StepIndex)
		{
			if (StepIndex < GpuReanchorStampStepCount)
			{
				Component.QueueBrushStamp(AnchorStamp);
			}
			TestWorld.StepWorld(BackendTestStepSeconds, true);
		}

		FVector2D Centroid = FVector2D::ZeroVector;
		Run.GenerationBefore = Component.GetSimulationGeneration();
		Run.AnchorSolverPointBefore = Component.ProjectWorldToSolverPlane(GpuReanchorAnchorLocation);
		Run.WindowCenterBefore = Component.GetGpuReadbackField().WindowCenterPlaneCm;
		Component.ComputeDensityCentroidPlane(false, Centroid, Run.DensityAreaBeforeCm2);

		FluidSubsystem->SetPlaneViewRotationOverrideForTests(FinalViewRotation);
		for (int32 StepIndex = 0; StepIndex < GpuReanchorSettleStepCount; ++StepIndex)
		{
			TestWorld.StepWorld(BackendTestStepSeconds, true);
		}

		const FTDFluidReadbackField& Field = Component.GetGpuReadbackField();
		const FTDFluidPlaneBasis& Basis = Component.GetPlaneBasis();
		Run.GenerationAfter = Component.GetSimulationGeneration();
		Run.AnchorSolverPointAfter = Component.ProjectWorldToSolverPlane(GpuReanchorAnchorLocation);
		Run.bHasReadbackAfter = Field.IsValid();
		Run.WindowCenterAfter = Field.WindowCenterPlaneCm;
		Run.PlaneCoordinateOffset = Run.AnchorSolverPointAfter - FVector2D(FVector::DotProduct(GpuReanchorAnchorLocation, Basis.U), FVector::DotProduct(GpuReanchorAnchorLocation, Basis.V));
		Component.ComputeDensityCentroidPlane(false, Centroid, Run.DensityAreaAfterCm2);
		Run.bIsValid = true;
		return Run;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuViewRotationKeepsStateTest,
	"TDGame.FluidFXRhi.Plane.ViewRotationKeepsGpuState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuViewRotationKeepsStateTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBackendTestPrivate;
	if (!CanRunRhiParityTest(*this, TEXT("TDGame.FluidFXRhi.Plane.ViewRotationKeepsGpuState")))
	{
		return false;
	}

	FTDFluidSolverBackendVariableScope BackendScope(0);
	const FTDFluidGpuReanchorRun ReferenceRun = RunGpuReanchorScenario(*this, FRotator(-60.0, 0.0, 0.0));
	const FTDFluidGpuReanchorRun RotatedRun = RunGpuReanchorScenario(*this, FRotator(-60.0, 40.0, 0.0));
	if (!ReferenceRun.bIsValid || !RotatedRun.bIsValid)
	{
		return false;
	}

	const FString ReanchorLine = FString::Printf(
		TEXT("TDFluidGpuReanchor: reference_area=%.1f rotated_area_before=%.1f rotated_area_after=%.1f generation=%d->%d offset=%s window=%s->%s"),
		ReferenceRun.DensityAreaAfterCm2,
		RotatedRun.DensityAreaBeforeCm2,
		RotatedRun.DensityAreaAfterCm2,
		RotatedRun.GenerationBefore,
		RotatedRun.GenerationAfter,
		*RotatedRun.PlaneCoordinateOffset.ToString(),
		*RotatedRun.WindowCenterBefore.ToString(),
		*RotatedRun.WindowCenterAfter.ToString());
	UE_LOG(LogTDFluidGpuBackendTests, Display, TEXT("%s"), *ReanchorLine);
	AddInfo(ReanchorLine);

	TestTrue(TEXT("The unrotated reference keeps density on the GPU"), ReferenceRun.DensityAreaAfterCm2 > 0.0f);
	TestTrue(TEXT("The rotated run holds density before the rotation"), RotatedRun.DensityAreaBeforeCm2 > 0.0f);
	TestTrue(*FString::Printf(TEXT("The rotation needs a plane coordinate offset above 100 cm (%s)"), *RotatedRun.PlaneCoordinateOffset.ToString()), RotatedRun.PlaneCoordinateOffset.Size() > GpuReanchorMinimumOffsetCm);
	TestEqual(TEXT("The rotation keeps the GPU solver generation"), RotatedRun.GenerationAfter, RotatedRun.GenerationBefore);
	TestTrue(TEXT("The anchor keeps its solver plane point"), (RotatedRun.AnchorSolverPointAfter - RotatedRun.AnchorSolverPointBefore).GetAbsMax() <= GpuReanchorPivotToleranceCm);
	TestTrue(TEXT("The readback field stays valid through the rotation"), RotatedRun.bHasReadbackAfter);
	TestTrue(TEXT("The readback window center stays in solver coordinates"), RotatedRun.WindowCenterAfter.Equals(RotatedRun.WindowCenterBefore, WindowCenterMatchToleranceCm));
	TestTrue(*FString::Printf(TEXT("The rotated density area stays within 10%% of the unrotated twin (%.1f vs %.1f)"), RotatedRun.DensityAreaAfterCm2, ReferenceRun.DensityAreaAfterCm2), RotatedRun.DensityAreaAfterCm2 >= GpuReanchorMinimumAreaRatio * ReferenceRun.DensityAreaAfterCm2);
	return true;
}

#endif
