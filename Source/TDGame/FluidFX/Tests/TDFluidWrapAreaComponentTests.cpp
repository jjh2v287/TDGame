#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidTypes.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "UObject/Package.h"

namespace TDFluidWrapAreaComponentTestPrivate
{
	constexpr float WrapComponentStepSeconds = 1.0f / 60.0f;
	constexpr int32 GhostTestResolution = 64;
	constexpr double WrapComponentExtentCm = 3000.0;
	constexpr double SeamDensityTolerance = 0.05;
	constexpr double DensityParityTolerance = 0.03;
	constexpr double KineticEnergyParityTolerance = 0.10;
	constexpr double DivergenceParityRatio = 1.2;
	constexpr int32 ParityResolution = 128;
	constexpr int32 ParityStampStepCount = 60;
	constexpr int32 ParityStepCount = 120;
	constexpr int32 ParityDrainStepLimit = 8;
	const FVector CpuActorLocation(0.0, 0.0, 0.0);
	const FVector GpuActorLocation(200000.0, 0.0, 0.0);

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	FTDFluidSimulationConfig MakeWrapComponentConfig(int32 Resolution, ETDFluidAreaAddressMode AddressMode, ETDFluidSolverBackend Backend)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.AreaAddressMode = AddressMode;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(WrapComponentExtentCm, WrapComponentExtentCm);
		Config.SolverBackend = Backend;
		Config.bEnableWaterLayer = false;
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.1f;
		Config.PressureIterations = 12;
		Config.TerrainSlopeGravity = 0.0f;
		Config.BorderFadeRatio = 0.0f;
		Config.MaxPointsPerTick = 64;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		return Config;
	}

	FTDFluidBrushStamp MakeStampAt(const FVector& Location, const FVector& Velocity, float RadiusCm)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Location;
		Stamp.WorldVelocity = Velocity;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		return Stamp;
	}

	double SumDensityOverOffsets(const FTDFluidGrid2DSolver& Solver, int32 CenterX, int32 CenterY, int32 MinimumOffsetX, int32 MaximumOffsetX, int32 HalfSpan)
	{
		const int32 Resolution = Solver.GetResolution();
		double DensitySum = 0.0;
		for (int32 OffsetY = -HalfSpan; OffsetY <= HalfSpan; ++OffsetY)
		{
			for (int32 OffsetX = MinimumOffsetX; OffsetX <= MaximumOffsetX; ++OffsetX)
			{
				const int32 CellX = ((CenterX + OffsetX) % Resolution + Resolution) % Resolution;
				const int32 CellY = ((CenterY + OffsetY) % Resolution + Resolution) % Resolution;
				DensitySum += Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
			}
		}
		return DensitySum;
	}

	struct FTDFluidWrapComponentTestWorld
	{
		FTDFluidWrapComponentTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidWrapComponentWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidWrapComponentTestWorld()
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

		void StepWorld(bool bShouldWaitForGpu)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, WrapComponentStepSeconds);
			if (bShouldWaitForGpu)
			{
				ENQUEUE_RENDER_COMMAND(TDFluidWrapComponentWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.SubmitAndBlockUntilGPUIdle();
				});
			}
			FlushRenderingCommands();
		}

		UTDFluidLiveSubsystem* GetFluidSubsystem() const
		{
			return World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		}

		UWorld* World = nullptr;
	};

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

	struct FTDFluidWrapFieldMetrics
	{
		double TotalDensity = 0.0;
		double KineticEnergy = 0.0;
		double DivergenceL2 = 0.0;
	};

	FTDFluidWrapFieldMetrics MeasureWrappedField(const FTDFluidReadbackField& Field)
	{
		FTDFluidWrapFieldMetrics Metrics;
		const int32 FieldResolution = FTDFluidReadbackField::GetResolution();
		if (Field.PrimaryDensity.Num() != FieldResolution * FieldResolution || Field.VelocityCmPerSec.Num() != FieldResolution * FieldResolution)
		{
			return Metrics;
		}

		const FVector2D CellSizeCm = Field.ExtentCm / static_cast<double>(FieldResolution);
		for (int32 CellY = 0; CellY < FieldResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < FieldResolution; ++CellX)
			{
				const int32 CellIndex = CellY * FieldResolution + CellX;
				const FVector2f Velocity = Field.VelocityCmPerSec[CellIndex];
				Metrics.TotalDensity += Field.PrimaryDensity[CellIndex];
				Metrics.KineticEnergy += static_cast<double>(Velocity.SizeSquared());
				const int32 RightIndex = CellY * FieldResolution + (CellX + 1) % FieldResolution;
				const int32 LeftIndex = CellY * FieldResolution + (CellX + FieldResolution - 1) % FieldResolution;
				const int32 UpIndex = ((CellY + 1) % FieldResolution) * FieldResolution + CellX;
				const int32 DownIndex = ((CellY + FieldResolution - 1) % FieldResolution) * FieldResolution + CellX;
				const double Divergence = (Field.VelocityCmPerSec[RightIndex].X - Field.VelocityCmPerSec[LeftIndex].X) / (2.0 * CellSizeCm.X)
					+ (Field.VelocityCmPerSec[UpIndex].Y - Field.VelocityCmPerSec[DownIndex].Y) / (2.0 * CellSizeCm.Y);
				Metrics.DivergenceL2 += Divergence * Divergence;
			}
		}
		Metrics.DivergenceL2 = FMath::Sqrt(Metrics.DivergenceL2);
		return Metrics;
	}

	double ComputeRelativeDifference(double Reference, double Candidate)
	{
		return FMath::Abs(Candidate - Reference) / FMath::Max(FMath::Abs(Reference), 1.0e-6);
	}

	void QueueSeamStamp(const ATDFluidLiveActor& LiveActor, const FVector& ActorLocation)
	{
		const FVector StampLocation = ActorLocation + FVector(0.5 * WrapComponentExtentCm - 40.0, 0.0, 0.0);
		LiveActor.LiveComponent->QueueBrushStamp(MakeStampAt(StampLocation, FVector(400.0, 0.0, 0.0), 120.0f));
	}

	uint32 HashReadbackField(const FTDFluidReadbackField& Field)
	{
		uint32 Hash = FCrc::MemCrc32(Field.PrimaryDensity.GetData(), Field.PrimaryDensity.Num() * sizeof(float));
		Hash = FCrc::MemCrc32(Field.SelectiveDensity.GetData(), Field.SelectiveDensity.Num() * sizeof(float), Hash);
		return FCrc::MemCrc32(Field.VelocityCmPerSec.GetData(), Field.VelocityCmPerSec.Num() * sizeof(FVector2f), Hash);
	}

	bool RunGpuWrapScenario(FAutomationTestBase& Test, uint32& OutHash, TextureAddress& OutWrapAddress, TextureAddress& OutClampAddress)
	{
		FTDFluidWrapComponentTestWorld TestWorld;
		ATDFluidLiveActor* WrapActor = SpawnConfiguredLiveActor(*TestWorld.World, GpuActorLocation, MakeWrapComponentConfig(ParityResolution, ETDFluidAreaAddressMode::Wrap, ETDFluidSolverBackend::Gpu));
		ATDFluidLiveActor* ClampActor = SpawnConfiguredLiveActor(*TestWorld.World, CpuActorLocation, MakeWrapComponentConfig(ParityResolution, ETDFluidAreaAddressMode::Clamp, ETDFluidSolverBackend::Gpu));
		if (!Test.TestNotNull(TEXT("Spawned the GPU wrap actor"), WrapActor) || !Test.TestNotNull(TEXT("Spawned the GPU clamp actor"), ClampActor))
		{
			return false;
		}

		for (int32 StepIndex = 0; StepIndex < ParityStampStepCount; ++StepIndex)
		{
			QueueSeamStamp(*WrapActor, GpuActorLocation);
			TestWorld.StepWorld(true);
		}
		for (int32 StepIndex = 0; StepIndex < ParityDrainStepLimit; ++StepIndex)
		{
			TestWorld.StepWorld(true);
		}

		if (!Test.TestTrue(TEXT("The wrap actor runs on the GPU"), WrapActor->LiveComponent->IsUsingGpuSolver()))
		{
			return false;
		}

		const UTextureRenderTarget2D* WrapTarget = Cast<UTextureRenderTarget2D>(WrapActor->LiveComponent->GetDensityPressureTexture());
		const UTextureRenderTarget2D* ClampTarget = Cast<UTextureRenderTarget2D>(ClampActor->LiveComponent->GetDensityPressureTexture());
		if (!Test.TestNotNull(TEXT("The wrap actor publishes a render target"), WrapTarget) || !Test.TestNotNull(TEXT("The clamp actor publishes a render target"), ClampTarget))
		{
			return false;
		}

		OutWrapAddress = WrapTarget->AddressX;
		OutClampAddress = ClampTarget->AddressX;
		OutHash = HashReadbackField(WrapActor->LiveComponent->GetGpuReadbackField());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaGhostStampsCoverSeamTest,
	"TDGame.FluidFX.WrapArea.Component.GhostStampsCoverSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaGhostStampsCoverSeamTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaComponentTestPrivate;

	const FTDFluidSimulationConfig Config = MakeWrapComponentConfig(GhostTestResolution, ETDFluidAreaAddressMode::Wrap, ETDFluidSolverBackend::Cpu);
	FTDFluidGrid2DSolver SeamSolver;
	FTDFluidGrid2DSolver ReferenceSolver;
	SeamSolver.Initialize(Config, FVector2D::ZeroVector);
	ReferenceSolver.Initialize(Config, FVector2D::ZeroVector);
	if (!TestTrue(TEXT("Both wrap solvers initialize"), SeamSolver.IsInitialized() && ReferenceSolver.IsInitialized()))
	{
		return false;
	}

	const FVector2D WindowExtentCm = SeamSolver.GetExtentCm();
	const FVector2D WindowMinimumCm = SeamSolver.GetCenterWorld() - 0.5 * WindowExtentCm;
	const int32 SeamCellX = GhostTestResolution - 2;
	const int32 ReferenceCellX = GhostTestResolution / 2 - 2;
	const int32 CenterCellY = GhostTestResolution / 2;
	const FVector2D SeamCenter = SeamSolver.CellToWorldCenter(SeamCellX, CenterCellY);
	const FVector2D ReferenceCenter = ReferenceSolver.CellToWorldCenter(ReferenceCellX, CenterCellY);
	constexpr float StampRadiusCm = 150.0f;

	TArray<FTDFluidBrushStamp> SeamStamps;
	const FVector2D OutsideSeamCenter = SeamCenter + FVector2D(WindowExtentCm.X, 0.0);
	const FVector2D WrappedSeamCenter = UTDFluidLiveComponent::WrapPlanePointIntoWindow(OutsideSeamCenter, WindowMinimumCm, WindowExtentCm);
	TestTrue(TEXT("A stamp one period outside wraps back onto the seam cell"), WrappedSeamCenter.Equals(SeamCenter, 1.0e-6));
	SeamStamps.Add(MakeStampAt(FVector(WrappedSeamCenter.X, WrappedSeamCenter.Y, 0.0), FVector::ZeroVector, StampRadiusCm));
	UTDFluidLiveComponent::AppendWrappedStampGhosts(SeamStamps, WindowMinimumCm, WindowExtentCm);
	TestEqual(TEXT("A stamp near one edge gets exactly one ghost"), SeamStamps.Num(), 2);

	TArray<FTDFluidBrushStamp> ReferenceStamps;
	ReferenceStamps.Add(MakeStampAt(FVector(ReferenceCenter.X, ReferenceCenter.Y, 0.0), FVector::ZeroVector, StampRadiusCm));
	UTDFluidLiveComponent::AppendWrappedStampGhosts(ReferenceStamps, WindowMinimumCm, WindowExtentCm);
	TestEqual(TEXT("A stamp in the middle gets no ghost"), ReferenceStamps.Num(), 1);

	SeamSolver.ApplyBrushStamps(SeamStamps, WrapComponentStepSeconds);
	ReferenceSolver.ApplyBrushStamps(ReferenceStamps, WrapComponentStepSeconds);

	constexpr int32 HalfSpan = 5;
	const int32 OutsideOffsetStart = GhostTestResolution - SeamCellX;
	const double SeamOutsideBand = SumDensityOverOffsets(SeamSolver, SeamCellX, CenterCellY, OutsideOffsetStart, HalfSpan, HalfSpan);
	const double ReferenceOutsideBand = SumDensityOverOffsets(ReferenceSolver, ReferenceCellX, CenterCellY, OutsideOffsetStart, HalfSpan, HalfSpan);
	const double SeamInsideBand = SumDensityOverOffsets(SeamSolver, SeamCellX, CenterCellY, -HalfSpan, OutsideOffsetStart - 1, HalfSpan);
	const double ReferenceInsideBand = SumDensityOverOffsets(ReferenceSolver, ReferenceCellX, CenterCellY, -HalfSpan, OutsideOffsetStart - 1, HalfSpan);
	AddInfo(FString::Printf(TEXT("TDFluidWrapGhost: outside seam=%.6f reference=%.6f inside seam=%.6f reference=%.6f"), SeamOutsideBand, ReferenceOutsideBand, SeamInsideBand, ReferenceInsideBand));
	TestTrue(TEXT("The reference stamp reaches past the mirrored seam offset"), ReferenceOutsideBand > 1.0e-3);
	TestTrue(TEXT("The wrapped band on the left edge matches the reference within 5%"), ComputeRelativeDifference(ReferenceOutsideBand, SeamOutsideBand) <= SeamDensityTolerance);
	TestTrue(TEXT("The band inside the right edge matches the reference within 5%"), ComputeRelativeDifference(ReferenceInsideBand, SeamInsideBand) <= SeamDensityTolerance);

	TArray<FTDFluidBrushStamp> CornerStamps;
	CornerStamps.Add(MakeStampAt(FVector(WindowMinimumCm.X + 10.0, WindowMinimumCm.Y + 10.0, 0.0), FVector::ZeroVector, StampRadiusCm));
	UTDFluidLiveComponent::AppendWrappedStampGhosts(CornerStamps, WindowMinimumCm, WindowExtentCm);
	TestEqual(TEXT("A corner stamp gets three ghosts"), CornerStamps.Num(), 4);

	TArray<FTDFluidBrushStamp> CappedStamps;
	for (int32 StampIndex = 0; StampIndex < Config.MaxPointsPerTick; ++StampIndex)
	{
		CappedStamps.Add(MakeStampAt(FVector(WindowMinimumCm.X + 5.0, WindowMinimumCm.Y + 5.0, 0.0), FVector::ZeroVector, StampRadiusCm));
	}
	UTDFluidLiveComponent::AppendWrappedStampGhosts(CappedStamps, WindowMinimumCm, WindowExtentCm);
	TestTrue(TEXT("Ghosts never exceed four times the real stamp limit"), CappedStamps.Num() <= Config.MaxPointsPerTick * 4);
	for (int32 StampIndex = 0; StampIndex < Config.MaxPointsPerTick; ++StampIndex)
	{
		if (!CappedStamps[StampIndex].WorldLocation.Equals(FVector(WindowMinimumCm.X + 5.0, WindowMinimumCm.Y + 5.0, 0.0)))
		{
			AddError(TEXT("Real stamps keep their order and position ahead of the ghosts"));
			break;
		}
	}

	TArray<FTDFluidSdfObstacle> Obstacles;
	FTDFluidSdfObstacle& EdgeObstacle = Obstacles.AddDefaulted_GetRef();
	EdgeObstacle.WorldCenter = FVector(WindowMinimumCm.X + WindowExtentCm.X - 20.0, 0.0, 0.0);
	EdgeObstacle.RadiusCm = 100.0f;
	UTDFluidLiveComponent::AppendWrappedObstacleGhosts(Obstacles, WindowMinimumCm, WindowExtentCm);
	TestEqual(TEXT("An obstacle on the right edge gets one ghost on the left"), Obstacles.Num(), 2);
	if (Obstacles.Num() == 2)
	{
		TestNearlyEqual(TEXT("The obstacle ghost sits one period to the left"), Obstacles[1].WorldCenter.X, EdgeObstacle.WorldCenter.X - WindowExtentCm.X, 1.0e-6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaTileMapOriginTest,
	"TDGame.FluidFX.WrapArea.Component.TileMapOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaTileMapOriginTest::RunTest(const FString& Parameters)
{
	const FVector2D AnchorCm(-3900.0, 1500.0);
	const FVector2D PeriodCm(3000.0, 2500.0);
	const FVector2D Observers[] = {
		FVector2D(0.0, 0.0), FVector2D(-3900.0, 1500.0), FVector2D(-900.0, 4000.0), FVector2D(-900.000001, 3999.999999),
		FVector2D(-6900.0, -1000.0), FVector2D(-6900.000001, -1000.000001), FVector2D(123456.789, -98765.4321), FVector2D(-123456.789, 98765.4321),
		FVector2D(1.0e6, 1.0e6), FVector2D(-1.0e6, -1.0e6), FVector2D(2100.0, 6500.0), FVector2D(2099.999, 6499.999),
		FVector2D(-3899.999, 1500.001), FVector2D(-3900.001, 1499.999), FVector2D(50.5, -50.5), FVector2D(-12345.0, 0.0),
		FVector2D(0.0, -12345.0), FVector2D(8100.0, -8500.0), FVector2D(-10000000.0, 10000000.0), FVector2D(299.0, 799.0)
	};

	for (const FVector2D& ObserverCm : Observers)
	{
		const FVector2D OriginCm = UTDFluidLiveComponent::ComputeTileMapOriginCm(AnchorCm, PeriodCm, ObserverCm);
		const FVector2D LocalCm = ObserverCm - OriginCm;
		const FVector2D PeriodIndex = (OriginCm - AnchorCm) / PeriodCm;
		const FString Label = FString::Printf(TEXT("Observer (%.6f, %.6f)"), ObserverCm.X, ObserverCm.Y);
		TestTrue(*(Label + TEXT(" lies inside the tile that starts at the origin")), LocalCm.X >= 0.0 && LocalCm.X < PeriodCm.X && LocalCm.Y >= 0.0 && LocalCm.Y < PeriodCm.Y);
		TestTrue(*(Label + TEXT(" keeps the origin a whole number of periods from the anchor")), FMath::Abs(PeriodIndex.X - FMath::RoundToDouble(PeriodIndex.X)) <= 1.0e-9 && FMath::Abs(PeriodIndex.Y - FMath::RoundToDouble(PeriodIndex.Y)) <= 1.0e-9);
	}

	TestTrue(TEXT("A zero period leaves the anchor as the origin"), UTDFluidLiveComponent::ComputeTileMapOriginCm(AnchorCm, FVector2D::ZeroVector, FVector2D(5000.0, -5000.0)).Equals(AnchorCm, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaCombatStampIgnoredByWrapTest,
	"TDGame.FluidFX.WrapArea.Component.CombatStampIgnoredByWrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaCombatStampIgnoredByWrapTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaComponentTestPrivate;

	FTDFluidWrapComponentTestWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
	if (!TestNotNull(TEXT("Fluid subsystem exists"), FluidSubsystem))
	{
		return false;
	}

	ATDFluidLiveActor* ClampActor = SpawnConfiguredLiveActor(*TestWorld.World, FVector::ZeroVector, MakeWrapComponentConfig(GhostTestResolution, ETDFluidAreaAddressMode::Clamp, ETDFluidSolverBackend::Cpu));
	ATDFluidLiveActor* WrapActor = SpawnConfiguredLiveActor(*TestWorld.World, FVector::ZeroVector, MakeWrapComponentConfig(GhostTestResolution, ETDFluidAreaAddressMode::Wrap, ETDFluidSolverBackend::Cpu));
	if (!TestNotNull(TEXT("Spawned the clamp actor"), ClampActor) || !TestNotNull(TEXT("Spawned the wrap actor"), WrapActor))
	{
		return false;
	}

	TestWorld.StepWorld(false);
	TestFalse(TEXT("The clamp actor is not wrapped"), ClampActor->LiveComponent->IsAreaWrapped());
	TestTrue(TEXT("The wrap actor is wrapped"), WrapActor->LiveComponent->IsAreaWrapped());
	TestFalse(TEXT("The wrap actor never samples terrain"), WrapActor->ShouldSampleTerrainElevation());

	const FVector InsidePoint(100.0, 0.0, 0.0);
	TestEqual(TEXT("A combat stamp inside both windows reaches only the clamp actor"), FluidSubsystem->InjectCombatBrushStamp(MakeStampAt(InsidePoint, FVector::ZeroVector, 60.0f)), 1);

	const TArray<FVector> ParticlePositions = { InsidePoint, FVector(-200.0, 50.0, 0.0) };
	const TArray<FVector> ParticleVelocities = { FVector(100.0, 0.0, 0.0), FVector(0.0, 100.0, 0.0) };
	TestEqual(TEXT("A particle splash inside both windows reaches only the clamp actor"), FluidSubsystem->InjectCombatParticleSplash(ParticlePositions, ParticleVelocities, 40.0f, ETDFluidBrushChannel::ChannelR_Mud), 1);

	TestWorld.StepWorld(false);
	FTDFluidCellSample WrapSample;
	WrapActor->LiveComponent->SampleFluidAtWorldLocation(InsidePoint, WrapSample);
	FTDFluidCellSample ClampSample;
	ClampActor->LiveComponent->SampleFluidAtWorldLocation(InsidePoint, ClampSample);
	TestTrue(TEXT("The clamp actor holds density from the combat stamp"), ClampSample.PrimaryDensity > 0.05f);
	TestTrue(TEXT("The wrap actor holds no combat density"), WrapSample.PrimaryDensity < 1.0e-4f);

	FTDFluidCellSample WrappedQuerySample;
	const bool bWrappedQueryHit = WrapActor->LiveComponent->SampleFluidAtWorldLocation(InsidePoint + FVector(7.0 * WrapComponentExtentCm, -3.0 * WrapComponentExtentCm, 0.0), WrappedQuerySample);
	TestTrue(TEXT("A query point many periods away wraps into the wrap window"), bWrappedQueryHit);
	return true;
}

namespace TDFluidWrapAreaComponentTestPrivate
{
	const FVector SeamStampOffset(0.5 * WrapComponentExtentCm - 40.0, 0.0, 0.0);
	const FVector ParityStampVelocity(400.0, 0.0, 0.0);
	constexpr float ParityStampRadiusCm = 120.0f;

	bool MeasureCpuGpuParityPair(FAutomationTestBase& Test, ETDFluidAreaAddressMode AddressMode, const FVector& StampOffset, FTDFluidWrapFieldMetrics& OutCpuMetrics, FTDFluidWrapFieldMetrics& OutGpuMetrics)
	{
		FTDFluidWrapComponentTestWorld TestWorld;
		UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
		ATDFluidLiveActor* CpuActor = SpawnConfiguredLiveActor(*TestWorld.World, CpuActorLocation, MakeWrapComponentConfig(ParityResolution, AddressMode, ETDFluidSolverBackend::Cpu));
		ATDFluidLiveActor* GpuActor = SpawnConfiguredLiveActor(*TestWorld.World, GpuActorLocation, MakeWrapComponentConfig(ParityResolution, AddressMode, ETDFluidSolverBackend::Gpu));
		if (!Test.TestNotNull(TEXT("Fluid subsystem exists"), FluidSubsystem) || !Test.TestNotNull(TEXT("Spawned the CPU parity actor"), CpuActor) || !Test.TestNotNull(TEXT("Spawned the GPU parity actor"), GpuActor))
		{
			return false;
		}

		TMap<uint32, FTDFluidReadbackField> CpuSnapshots;
		for (int32 StepIndex = 0; StepIndex < ParityStepCount + ParityDrainStepLimit; ++StepIndex)
		{
			if (StepIndex < ParityStampStepCount)
			{
				CpuActor->LiveComponent->QueueBrushStamp(MakeStampAt(CpuActorLocation + StampOffset, ParityStampVelocity, ParityStampRadiusCm));
				GpuActor->LiveComponent->QueueBrushStamp(MakeStampAt(GpuActorLocation + StampOffset, ParityStampVelocity, ParityStampRadiusCm));
			}
			TestWorld.StepWorld(true);
			const uint32 SubmittedFrame = FluidSubsystem->GetLastGpuSolverFrameNumber();
			CpuSnapshots.FindOrAdd(SubmittedFrame).DownsampleFromSolver(CpuActor->LiveComponent->GetSolver());
			if (StepIndex + 1 < ParityStepCount)
			{
				continue;
			}

			const FTDFluidReadbackField& GpuField = GpuActor->LiveComponent->GetGpuReadbackField();
			const FTDFluidReadbackField* CpuField = GpuField.IsValid() ? CpuSnapshots.Find(GpuField.FrameNumber) : nullptr;
			if (!CpuField)
			{
				continue;
			}

			OutCpuMetrics = MeasureWrappedField(*CpuField);
			OutGpuMetrics = MeasureWrappedField(GpuField);
			return Test.TestTrue(TEXT("The GPU parity actor runs on the GPU"), GpuActor->LiveComponent->IsUsingGpuSolver());
		}

		Test.AddError(TEXT("No GPU readback frame matched a CPU snapshot"));
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaCpuGpuParityTest,
	"TDGame.FluidFXRhi.WrapArea.CpuGpuParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaCpuGpuParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaComponentTestPrivate;

	if (!HasRenderingRhi())
	{
		AddInfo(TEXT("TDGame.FluidFXRhi.WrapArea.CpuGpuParity needs a rendering RHI; skipped under the null RHI."));
		return true;
	}

	FTDFluidWrapFieldMetrics CpuMetrics;
	FTDFluidWrapFieldMetrics GpuMetrics;
	if (!MeasureCpuGpuParityPair(*this, ETDFluidAreaAddressMode::Wrap, SeamStampOffset, CpuMetrics, GpuMetrics))
	{
		return false;
	}

	FTDFluidWrapFieldMetrics ClampCpuMetrics;
	FTDFluidWrapFieldMetrics ClampGpuMetrics;
	if (!MeasureCpuGpuParityPair(*this, ETDFluidAreaAddressMode::Clamp, FVector::ZeroVector, ClampCpuMetrics, ClampGpuMetrics))
	{
		return false;
	}

	const double WrapEnergyRatio = GpuMetrics.KineticEnergy / FMath::Max(CpuMetrics.KineticEnergy, 1.0e-6);
	const double ClampEnergyRatio = ClampGpuMetrics.KineticEnergy / FMath::Max(ClampCpuMetrics.KineticEnergy, 1.0e-6);
	const double ClampCorrectedEnergyGap = ComputeRelativeDifference(ClampEnergyRatio, WrapEnergyRatio);
	AddInfo(FString::Printf(TEXT("TDFluidWrapParityClampControl: density cpu=%.6f gpu=%.6f energy cpu=%.3f gpu=%.3f energy_gap=%.4f divergence cpu=%.6f gpu=%.6f"), ClampCpuMetrics.TotalDensity, ClampGpuMetrics.TotalDensity, ClampCpuMetrics.KineticEnergy, ClampGpuMetrics.KineticEnergy, ComputeRelativeDifference(ClampCpuMetrics.KineticEnergy, ClampGpuMetrics.KineticEnergy), ClampCpuMetrics.DivergenceL2, ClampGpuMetrics.DivergenceL2));
	AddInfo(FString::Printf(TEXT("TDFluidWrapParity: density cpu=%.6f gpu=%.6f energy cpu=%.3f gpu=%.3f energy_gap=%.4f clamp_corrected_energy_gap=%.4f divergence cpu=%.6f gpu=%.6f"), CpuMetrics.TotalDensity, GpuMetrics.TotalDensity, CpuMetrics.KineticEnergy, GpuMetrics.KineticEnergy, ComputeRelativeDifference(CpuMetrics.KineticEnergy, GpuMetrics.KineticEnergy), ClampCorrectedEnergyGap, CpuMetrics.DivergenceL2, GpuMetrics.DivergenceL2));
	TestTrue(TEXT("The CPU wrap run holds density"), CpuMetrics.TotalDensity > 1.0e-3);
	TestTrue(TEXT("The CPU wrap run and the CPU clamp control hold kinetic energy"), CpuMetrics.KineticEnergy > 1.0e-3 && ClampCpuMetrics.KineticEnergy > 1.0e-3);
	TestTrue(TEXT("Wrapped total density matches the CPU within 3%"), ComputeRelativeDifference(CpuMetrics.TotalDensity, GpuMetrics.TotalDensity) <= DensityParityTolerance);
	TestTrue(TEXT("Wrapped GPU to CPU kinetic energy ratio matches the clamp control's ratio within 10%"), ClampCorrectedEnergyGap <= KineticEnergyParityTolerance);
	TestTrue(TEXT("Wrapped GPU divergence stays within 1.2 times the CPU"), GpuMetrics.DivergenceL2 <= CpuMetrics.DivergenceL2 * DivergenceParityRatio + 1.0e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaGpuRepeatAndAddressTest,
	"TDGame.FluidFXRhi.WrapArea.GpuRepeatAndAddress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaGpuRepeatAndAddressTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaComponentTestPrivate;

	if (!HasRenderingRhi())
	{
		AddInfo(TEXT("TDGame.FluidFXRhi.WrapArea.GpuRepeatAndAddress needs a rendering RHI; skipped under the null RHI."));
		return true;
	}

	uint32 FirstHash = 0;
	uint32 SecondHash = 0;
	TextureAddress FirstWrapAddress = TA_Clamp;
	TextureAddress FirstClampAddress = TA_Wrap;
	TextureAddress SecondWrapAddress = TA_Clamp;
	TextureAddress SecondClampAddress = TA_Wrap;
	if (!RunGpuWrapScenario(*this, FirstHash, FirstWrapAddress, FirstClampAddress) || !RunGpuWrapScenario(*this, SecondHash, SecondWrapAddress, SecondClampAddress))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("TDFluidWrapRepeat: first=0x%08X second=0x%08X"), FirstHash, SecondHash));
	TestEqual(TEXT("Two GPU wrap runs read back the same field"), FirstHash, SecondHash);
	TestEqual(TEXT("The wrap actor render target wraps"), static_cast<int32>(FirstWrapAddress), static_cast<int32>(TA_Wrap));
	TestEqual(TEXT("The clamp actor render target clamps"), static_cast<int32>(FirstClampAddress), static_cast<int32>(TA_Clamp));
	return true;
}

namespace TDFluidWrapAreaComponentTestPrivate
{
	const TCHAR* SeaWaterInstancePath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidWater_Sea.MI_TD_FluidWater_Sea");
	const FVector TileMapObserverLocation(-52345.0, 31234.0, 900.0);
	constexpr int32 TileMapStepCount = 6;
	constexpr float TileMapParameterToleranceCm = 0.01f;

	bool ReadTileMapVector(FAutomationTestBase& Test, const UMaterialInstanceDynamic& TargetMID, const TCHAR* ParameterName, FLinearColor& OutValue)
	{
		const bool bHasParameter = TargetMID.GetVectorParameterValue(FHashedMaterialParameterInfo(ParameterName), OutValue);
		return Test.TestTrue(FString::Printf(TEXT("The sea water instance exposes %s"), ParameterName), bHasParameter);
	}

	bool IsLinearColorNear(const FLinearColor& Actual, const FLinearColor& Expected, float Tolerance)
	{
		return FMath::IsNearlyEqual(Actual.R, Expected.R, Tolerance) && FMath::IsNearlyEqual(Actual.G, Expected.G, Tolerance)
			&& FMath::IsNearlyEqual(Actual.B, Expected.B, Tolerance) && FMath::IsNearlyEqual(Actual.A, Expected.A, Tolerance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWrapAreaTileMapTargetParametersTest,
	"TDGame.FluidFXRhi.WrapArea.TileMapTargetParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWrapAreaTileMapTargetParametersTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWrapAreaComponentTestPrivate;

	if (!HasRenderingRhi())
	{
		AddInfo(TEXT("TDGame.FluidFXRhi.WrapArea.TileMapTargetParameters needs a rendering RHI; skipped under the null RHI."));
		return true;
	}

	UMaterialInterface* SeaWaterInstance = LoadObject<UMaterialInterface>(nullptr, SeaWaterInstancePath);
	if (!TestNotNull(TEXT("Loaded the sea water instance"), SeaWaterInstance))
	{
		return false;
	}

	FTDFluidWrapComponentTestWorld TestWorld;
	ATDFluidLiveActor* WrapActor = SpawnConfiguredLiveActor(*TestWorld.World, GpuActorLocation, MakeWrapComponentConfig(ParityResolution, ETDFluidAreaAddressMode::Wrap, ETDFluidSolverBackend::Gpu));
	ATDFluidLiveActor* ClampActor = SpawnConfiguredLiveActor(*TestWorld.World, CpuActorLocation, MakeWrapComponentConfig(ParityResolution, ETDFluidAreaAddressMode::Clamp, ETDFluidSolverBackend::Gpu));
	APlayerController* ObserverController = TestWorld.World->SpawnActor<APlayerController>();
	UMaterialInstanceDynamic* WrapTargetMID = UMaterialInstanceDynamic::Create(SeaWaterInstance, GetTransientPackage());
	UMaterialInstanceDynamic* ClampTargetMID = UMaterialInstanceDynamic::Create(SeaWaterInstance, GetTransientPackage());
	if (!TestNotNull(TEXT("Spawned the GPU wrap actor"), WrapActor) || !TestNotNull(TEXT("Spawned the GPU clamp actor"), ClampActor)
		|| !TestNotNull(TEXT("Spawned the observer controller"), ObserverController) || !TestNotNull(TEXT("Created the wrap target MID"), WrapTargetMID)
		|| !TestNotNull(TEXT("Created the clamp target MID"), ClampTargetMID))
	{
		return false;
	}

	APlayerCameraManager* ObserverCamera = ObserverController->PlayerCameraManager;
	if (!TestNotNull(TEXT("The observer controller owns a camera manager"), ObserverCamera))
	{
		return false;
	}

	WrapActor->LiveComponent->RegisterTileMapTarget(WrapTargetMID);
	ClampActor->LiveComponent->RegisterTileMapTarget(ClampTargetMID);

	FMinimalViewInfo ObserverView;
	ObserverView.Location = TileMapObserverLocation;
	for (int32 StepIndex = 0; StepIndex < TileMapStepCount; ++StepIndex)
	{
		ObserverCamera->SetCameraCachePOV(ObserverView);
		QueueSeamStamp(*WrapActor, GpuActorLocation);
		TestWorld.StepWorld(true);
	}

	const UTDFluidLiveComponent& WrapComponent = *WrapActor->LiveComponent;
	if (!TestTrue(TEXT("The wrap actor runs on the GPU"), WrapComponent.IsUsingGpuSolver()) || !TestTrue(TEXT("The wrap actor is wrapped"), WrapComponent.IsAreaWrapped()))
	{
		return false;
	}

	const FVector2D PeriodCm = WrapComponent.GetSimulationExtentCm();
	const FVector2D AnchorCm = WrapComponent.GetSimulationCenterWorld() - 0.5 * PeriodCm;
	const FVector2D ObserverCm(TileMapObserverLocation.X, TileMapObserverLocation.Y);
	const FVector2D ExpectedOriginCm = UTDFluidLiveComponent::ComputeTileMapOriginCm(AnchorCm, PeriodCm, ObserverCm);
	const FLinearColor ExpectedOrigin(static_cast<float>(ExpectedOriginCm.X), static_cast<float>(ExpectedOriginCm.Y), 0.0f, 0.0f);
	const FLinearColor ExpectedPeriod(static_cast<float>(PeriodCm.X), static_cast<float>(PeriodCm.Y), static_cast<float>(WrapComponent.GetSimulationResolution()), 0.0f);

	FLinearColor WrapOrigin;
	FLinearColor WrapPeriod;
	FLinearColor ClampPeriod;
	if (!ReadTileMapVector(*this, *WrapTargetMID, TEXT("TileMap1OriginCm"), WrapOrigin) || !ReadTileMapVector(*this, *WrapTargetMID, TEXT("TileMap1PeriodCm"), WrapPeriod)
		|| !ReadTileMapVector(*this, *ClampTargetMID, TEXT("TileMap1PeriodCm"), ClampPeriod))
	{
		return false;
	}

	UTexture* WrapTileTexture = nullptr;
	UTexture* ClampTileTexture = nullptr;
	const bool bWrapHasTexture = WrapTargetMID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("TileMap1Texture")), WrapTileTexture);
	const bool bClampHasTexture = ClampTargetMID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("TileMap1Texture")), ClampTileTexture);
	UTexture* WrapSourceTexture = WrapComponent.GetDensityPressureTexture();

	AddInfo(FString::Printf(TEXT("TDFluidTileMapTarget: anchor=(%.3f, %.3f) period=(%.3f, %.3f, %.0f) observer=(%.3f, %.3f) expected_origin=(%.3f, %.3f) origin=(%.3f, %.3f, %.3f, %.3f) clamp_period=(%.3f, %.3f, %.3f, %.3f)"),
		AnchorCm.X, AnchorCm.Y, WrapPeriod.R, WrapPeriod.G, WrapPeriod.B, ObserverCm.X, ObserverCm.Y, ExpectedOriginCm.X, ExpectedOriginCm.Y,
		WrapOrigin.R, WrapOrigin.G, WrapOrigin.B, WrapOrigin.A, ClampPeriod.R, ClampPeriod.G, ClampPeriod.B, ClampPeriod.A));

	const FVector2D ObserverInTileCm = ObserverCm - ExpectedOriginCm;
	TestTrue(TEXT("The observer lies inside the tile that starts at the expected origin"), ObserverInTileCm.X >= 0.0 && ObserverInTileCm.X < PeriodCm.X && ObserverInTileCm.Y >= 0.0 && ObserverInTileCm.Y < PeriodCm.Y);
	TestFalse(TEXT("The observer sits whole periods away from the anchor tile"), ExpectedOriginCm.Equals(AnchorCm, 1.0));
	TestTrue(TEXT("The wrap target origin matches the pure tile origin function for the camera observer"), IsLinearColorNear(WrapOrigin, ExpectedOrigin, TileMapParameterToleranceCm));
	TestTrue(TEXT("The wrap target period carries the wrap extent and grid resolution"), IsLinearColorNear(WrapPeriod, ExpectedPeriod, TileMapParameterToleranceCm));
	TestTrue(TEXT("The wrap source publishes a density pressure texture"), WrapSourceTexture != nullptr);
	TestTrue(TEXT("The wrap target samples the wrap source output texture"), bWrapHasTexture && WrapTileTexture == WrapSourceTexture);
	TestTrue(TEXT("The clamp target keeps a zero tile period"), IsLinearColorNear(ClampPeriod, FLinearColor::Transparent, 0.0f));
	TestTrue(TEXT("The clamp target never receives the clamp source texture"), !bClampHasTexture || ClampTileTexture != ClampActor->LiveComponent->GetDensityPressureTexture());
	return true;
}

#endif
