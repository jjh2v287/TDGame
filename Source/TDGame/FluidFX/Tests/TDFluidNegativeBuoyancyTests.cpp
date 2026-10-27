#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace TDFluidNegativeBuoyancyTestPrivate
{
	struct FBuoyancyRunResult
	{
		double CentroidYCm = 0.0;
		double TotalDensity = 0.0;
		uint32 Checksum = 0;
	};

	FBuoyancyRunResult RunBuoyancyColumn(float BuoyancyCmPerSec2)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(400.0, 400.0);
		Config.DensityDissipation = 0.0f;
		Config.VelocityDamping = 0.02f;
		Config.PressureIterations = 6;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.BorderFadeRatio = 0.02f;
		Config.BuoyancyCmPerSec2 = BuoyancyCmPerSec2;

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));

		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector::ZeroVector;
		Stamp.RadiusCm = 40.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;

		constexpr float DeltaTime = 1.0f / 30.0f;
		for (int32 StepIndex = 0; StepIndex < 45; ++StepIndex)
		{
			if (StepIndex < 5)
			{
				Solver.ApplyBrushStamps({ Stamp }, DeltaTime);
			}
			Solver.StepSimulation(DeltaTime);
		}

		FBuoyancyRunResult Result;
		double WeightedY = 0.0;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
				const double Density = FMath::Max(Sample.PrimaryDensity, Sample.SelectiveDensity);
				WeightedY += Density * Solver.CellToWorldCenter(CellX, CellY).Y;
				Result.TotalDensity += Density;
			}
		}
		Result.CentroidYCm = Result.TotalDensity > 0.0 ? WeightedY / Result.TotalDensity : 0.0;
		Result.Checksum = Solver.ComputeStateChecksum();
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNegativeBuoyancySinksTest,
	"TDGame.FluidFX.Solver.NegativeBuoyancySinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNegativeBuoyancySinksTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNegativeBuoyancyTestPrivate;
	const FBuoyancyRunResult Neutral = RunBuoyancyColumn(0.0f);
	const FBuoyancyRunResult Rising = RunBuoyancyColumn(160.0f);
	const FBuoyancyRunResult Sinking = RunBuoyancyColumn(-160.0f);
	const FBuoyancyRunResult SinkingAgain = RunBuoyancyColumn(-160.0f);

	TestTrue(TEXT("Neutral run keeps density in the grid"), Neutral.TotalDensity > 1.0);
	TestTrue(*FString::Printf(TEXT("Positive buoyancy lifts the centroid (%.2f > %.2f)"), Rising.CentroidYCm, Neutral.CentroidYCm + 2.0), Rising.CentroidYCm > Neutral.CentroidYCm + 2.0);
	TestTrue(*FString::Printf(TEXT("Negative buoyancy sinks the centroid (%.2f < %.2f)"), Sinking.CentroidYCm, Neutral.CentroidYCm - 2.0), Sinking.CentroidYCm < Neutral.CentroidYCm - 2.0);
	TestEqual(TEXT("Negative buoyancy run is deterministic"), Sinking.Checksum, SinkingAgain.Checksum);
	TestNotEqual(TEXT("Negative buoyancy changes the state"), Sinking.Checksum, Neutral.Checksum);
	return true;
}

namespace TDFluidNegativeBuoyancyRhiTestPrivate
{
	constexpr int32 ParityResolution = 128;
	constexpr float ParityStepSeconds = 1.0f / 30.0f;
	constexpr int32 ParityStampStepCount = 5;
	constexpr int32 ParityStepCount = 30;
	constexpr int32 ParityDrainStepLimit = 12;
	constexpr float SinkingBuoyancyCmPerSec2 = -160.0f;
	constexpr double MinimumSinkCells = 2.0;
	const FVector ParityAnchorLocation(0.0, 0.0, 200.0);

	struct FSolverBackendScope
	{
		FSolverBackendScope()
			: Variable(IConsoleManager::Get().FindConsoleVariable(TEXT("TD.FluidFX.SolverBackend")))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetInt();
			Variable->Set(0, ECVF_SetByCode);
		}

		~FSolverBackendScope()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
	};

	struct FParityWorld
	{
		UWorld* World = nullptr;

		FParityWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidNegativeBuoyancyWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FParityWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}

		void Step()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, ParityStepSeconds);
			ENQUEUE_RENDER_COMMAND(TDFluidNegativeBuoyancyWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
		}
	};

	FTDFluidSimulationConfig MakeSinkingConfig(ETDFluidSolverBackend Backend)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
		Config.GridResolution = ParityResolution;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		Config.DensityDissipation = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.VorticityStrength = 0.0f;
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = false;
		Config.BuoyancyCmPerSec2 = SinkingBuoyancyCmPerSec2;
		Config.SolverBackend = Backend;
		return Config;
	}

	ATDFluidLiveActor* SpawnConfiguredLiveActor(UWorld& World, const FTDFluidSimulationConfig& Config, UTDFluidPresetAsset* PlanePreset)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, ParityAnchorLocation);
		ATDFluidLiveActor* LiveActor = World.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}
		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);
		LiveActor->LiveComponent->ActivePreset = PlanePreset;
		return LiveActor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNegativeBuoyancyMatchesCpuRhiTest,
	"TDGame.FluidFXRhi.Solver.NegativeBuoyancyMatchesCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNegativeBuoyancyMatchesCpuRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNegativeBuoyancyRhiTestPrivate;
	if (GUsingNullRHI || !FApp::CanEverRender())
	{
		AddError(TEXT("TDGame.FluidFXRhi.Solver.NegativeBuoyancyMatchesCpu needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	FSolverBackendScope BackendScope;
	FParityWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	if (!TestNotNull(TEXT("The world has a fluid subsystem"), FluidSubsystem))
	{
		return false;
	}
	FluidSubsystem->SetPlaneViewRotationOverrideForTests(FRotator(-60.0, 0.0, 0.0));

	UTDFluidPresetAsset* PlanePreset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::Custom);
	TStrongObjectPtr<UTDFluidPresetAsset> PresetGuard(PlanePreset);
	PlanePreset->PlaneSettings.Orientation = ETDFluidPlaneOrientation::CameraFacing;

	ATDFluidLiveActor* CpuActor = SpawnConfiguredLiveActor(*TestWorld.World, MakeSinkingConfig(ETDFluidSolverBackend::Cpu), PlanePreset);
	ATDFluidLiveActor* GpuActor = SpawnConfiguredLiveActor(*TestWorld.World, MakeSinkingConfig(ETDFluidSolverBackend::Gpu), PlanePreset);
	if (!TestNotNull(TEXT("Spawned the CPU actor"), CpuActor) || !TestNotNull(TEXT("Spawned the GPU actor"), GpuActor))
	{
		return false;
	}
	if (!TestFalse(TEXT("CPU actor uses the CPU solver"), CpuActor->LiveComponent->IsUsingGpuSolver()) || !TestTrue(TEXT("GPU actor uses the GPU solver"), GpuActor->LiveComponent->IsUsingGpuSolver()))
	{
		return false;
	}
	TestWorld.Step();

	const FVector2D PlaneUp = GpuActor->LiveComponent->GetPlaneBasis().GetPlaneUpDirection().GetSafeNormal();
	if (!TestFalse(TEXT("Camera-facing plane has an up direction"), PlaneUp.IsNearlyZero()))
	{
		return false;
	}

	FTDFluidBrushStamp Stamp;
	Stamp.WorldLocation = ParityAnchorLocation;
	Stamp.RadiusCm = 60.0f;
	Stamp.DensityStrength = 1.0f;
	TMap<uint32, FTDFluidReadbackField> CpuSnapshots;
	uint32 TargetFrame = 0;
	for (int32 StepIndex = 0; StepIndex < ParityStepCount + ParityDrainStepLimit; ++StepIndex)
	{
		if (StepIndex < ParityStampStepCount)
		{
			CpuActor->LiveComponent->QueueBrushStamp(Stamp);
			GpuActor->LiveComponent->QueueBrushStamp(Stamp);
		}
		TestWorld.Step();
		const uint32 SubmittedFrame = FluidSubsystem->GetLastGpuSolverFrameNumber();
		CpuSnapshots.FindOrAdd(SubmittedFrame).DownsampleFromSolver(CpuActor->LiveComponent->GetSolver());
		if (StepIndex == ParityStepCount - 1)
		{
			TargetFrame = SubmittedFrame;
		}
		const FTDFluidReadbackField& StepField = GpuActor->LiveComponent->GetGpuReadbackField();
		if (StepIndex >= ParityStepCount - 1 && StepField.IsValid() && StepField.FrameNumber >= TargetFrame)
		{
			break;
		}
	}
	FluidSubsystem->ClearPlaneViewRotationOverrideForTests();

	const FTDFluidReadbackField& GpuField = GpuActor->LiveComponent->GetGpuReadbackField();
	const FTDFluidReadbackField* CpuSnapshot = CpuSnapshots.Find(GpuField.FrameNumber);
	if (!TestTrue(TEXT("GPU readback is valid"), GpuField.IsValid()) || !TestNotNull(*FString::Printf(TEXT("CPU snapshot exists for GPU frame %u (target %u)"), GpuField.FrameNumber, TargetFrame), CpuSnapshot))
	{
		return false;
	}

	FVector2D CpuCentroid = FVector2D::ZeroVector;
	FVector2D GpuCentroid = FVector2D::ZeroVector;
	float CpuAreaCm2 = 0.0f;
	float GpuAreaCm2 = 0.0f;
	const bool bHasCpuCentroid = CpuSnapshot->ComputeDensityCentroid(false, CpuCentroid, CpuAreaCm2);
	const bool bHasGpuCentroid = GpuField.ComputeDensityCentroid(false, GpuCentroid, GpuAreaCm2);
	if (!TestTrue(TEXT("Both fields have a density centroid"), bHasCpuCentroid && bHasGpuCentroid))
	{
		return false;
	}

	const FVector2D CellSizeCm = GpuActor->LiveComponent->GetSimulationExtentCm() / static_cast<double>(ParityResolution);
	const FVector2D StampPlane = GpuActor->LiveComponent->ProjectWorldToSolverPlane(ParityAnchorLocation);
	const FVector2D CentroidOffsetCm = GpuCentroid - CpuCentroid;
	const double GpuSinkCells = FVector2D::DotProduct(GpuCentroid - StampPlane, PlaneUp) / CellSizeCm.Y;
	const double CpuSinkCells = FVector2D::DotProduct(CpuCentroid - StampPlane, PlaneUp) / CellSizeCm.Y;
	AddInfo(FString::Printf(TEXT("Frame %u: CPU centroid %s, GPU centroid %s, offset (%.2f, %.2f) cells, along up CPU %.2f GPU %.2f cells"),
		GpuField.FrameNumber, *CpuCentroid.ToString(), *GpuCentroid.ToString(), CentroidOffsetCm.X / CellSizeCm.X, CentroidOffsetCm.Y / CellSizeCm.Y, CpuSinkCells, GpuSinkCells));

	TestTrue(*FString::Printf(TEXT("GPU density sinks below the stamp (%.2f cells)"), GpuSinkCells), GpuSinkCells < -MinimumSinkCells);
	TestTrue(TEXT("GPU centroid X is within one cell of the CPU"), FMath::Abs(CentroidOffsetCm.X) <= CellSizeCm.X);
	TestTrue(TEXT("GPU centroid Y is within one cell of the CPU"), FMath::Abs(CentroidOffsetCm.Y) <= CellSizeCm.Y);
	return true;
}

#endif
