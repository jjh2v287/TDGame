#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuMassInput.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TDFluidGpuPassTimestamps.h"
#include "TDFluidGpuStepPasses.h"

namespace TDFluidGpuMassInputTestPrivate
{
	struct FTDMassSplatCase
	{
		FVector2D LocalCm = FVector2D::ZeroVector;
		FVector2D VelocityCmPerSec = FVector2D::ZeroVector;
		float RadiusCm = 0.0f;
		float Density = 0.0f;
		float Heat = 0.0f;
		bool bWritesVelocity = true;
	};

	TArray<FTDMassSplatCase> MakeCases(int32 Count, int32 InputResolution, const FVector2D& InputCellCm)
	{
		FRandomStream Stream(0x5EED5u);
		TArray<FTDMassSplatCase> Cases;
		Cases.Reserve(Count);
		const FVector2D AreaCm = InputCellCm * static_cast<double>(InputResolution);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FTDMassSplatCase& Case = Cases.AddDefaulted_GetRef();
			const bool bNearBorder = Index % 7 == 0;
			Case.LocalCm = bNearBorder
				? FVector2D(Stream.FRandRange(-20.0f, 20.0f), Stream.FRandRange(AreaCm.Y - 20.0, AreaCm.Y + 20.0))
				: FVector2D(Stream.FRandRange(0.0f, AreaCm.X), Stream.FRandRange(0.0f, AreaCm.Y));
			Case.VelocityCmPerSec = FVector2D(Stream.FRandRange(-1200.0f, 1200.0f), Stream.FRandRange(-1200.0f, 1200.0f));
			Case.RadiusCm = TDFluidGpuMassInput::ResolveSplatRadius(Stream.FRandRange(1.0f, 60.0f), InputCellCm, 8);
			Case.Density = Stream.FRandRange(0.0f, 1.5f);
			Case.Heat = Index % 3 == 0 ? Stream.FRandRange(0.0f, 1.0f) : 0.0f;
			Case.bWritesVelocity = Index % 5 != 0;
		}
		return Cases;
	}

	TArray<uint32> SplatInOrder(const TArray<FTDMassSplatCase>& Cases, const TArray<int32>& Order, int32 InputResolution, const FVector2D& InputCellCm, bool bWrapsArea)
	{
		TArray<uint32> Accumulation;
		Accumulation.SetNumZeroed(InputResolution * InputResolution * TDFluidGpuSolverConstants::MassInputChannelCount);
		for (const int32 CaseIndex : Order)
		{
			const FTDMassSplatCase& Case = Cases[CaseIndex];
			TDFluidGpuMassInput::SplatPointReference(Accumulation, InputResolution, InputCellCm, bWrapsArea, Case.LocalCm, Case.VelocityCmPerSec, Case.RadiusCm, Case.Density, Case.Heat, Case.bWritesVelocity);
		}
		return Accumulation;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputGpuQuantizeAndSafeCountTest,
	"TDGame.FluidFX.MassInput.Gpu.QuantizeAndSafeCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputGpuQuantizeAndSafeCountTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Half density quantizes to 2^23"), TDFluidGpuMassInput::QuantizeUnit(0.5f), 8388608u);
	TestEqual(TEXT("Density saturates at 2^24"), TDFluidGpuMassInput::QuantizeUnit(1.5f), 16777216u);
	TestEqual(TEXT("Negative density clamps to zero"), TDFluidGpuMassInput::QuantizeUnit(-1.0f), 0u);
	TestEqual(TEXT("Velocity 1 cm/s quantizes to 8"), TDFluidGpuMassInput::QuantizeVelocity(1.0f), 8);
	TestEqual(TEXT("Velocity sign folds symmetrically"), TDFluidGpuMassInput::QuantizeVelocity(-1.0f), -8);
	TestEqual(TEXT("Half quantum rounds away from zero"), TDFluidGpuMassInput::QuantizeVelocity(0.0625f), 1);
	TestEqual(TEXT("Negative half quantum rounds away from zero"), TDFluidGpuMassInput::QuantizeVelocity(-0.0625f), -1);
	TestTrue(TEXT("Huge velocity stays inside int32"), TDFluidGpuMassInput::QuantizeVelocity(1.0e12f) > 0 && TDFluidGpuMassInput::QuantizeVelocity(-1.0e12f) < 0);
	TestEqual(TEXT("Full weight quantizes to 4096"), TDFluidGpuMassInput::QuantizeWeight(1.0f), 4096u);
	TestEqual(TEXT("SafeCount at 1200 cm/s"), TDFluidGpuMassInput::ComputeSafeCount(1200.0f), 223696u);
	TestEqual(TEXT("SafeCount at 1e6 cm/s"), TDFluidGpuMassInput::ComputeSafeCount(1.0e6f), 268u);
	TestTrue(TEXT("SafeCount never reaches zero"), TDFluidGpuMassInput::ComputeSafeCount(TNumericLimits<float>::Max()) >= 1u);
	TestEqual(TEXT("Divisor 3 rounds up to 4"), TDFluidGpuMassInput::ResolveDivisor(3), 4);
	TestEqual(TEXT("Divisor 0 resolves to 1"), TDFluidGpuMassInput::ResolveDivisor(0), 1);
	TestEqual(TEXT("Radius lower bound is half a cell"), TDFluidGpuMassInput::ResolveSplatRadius(0.1f, FVector2D(10.0, 10.0), 8), 5.0f);
	TestEqual(TEXT("Radius upper bound is the cap"), TDFluidGpuMassInput::ResolveSplatRadius(1000.0f, FVector2D(10.0, 10.0), 8), 80.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputGpuReferenceSplatOrderIndependentTest,
	"TDGame.FluidFX.MassInput.Gpu.ReferenceSplatOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputGpuReferenceSplatOrderIndependentTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuMassInputTestPrivate;
	constexpr int32 InputResolution = 32;
	const FVector2D InputCellCm(10.0, 12.5);
	const TArray<FTDMassSplatCase> Cases = MakeCases(1000, InputResolution, InputCellCm);

	TArray<int32> Forward;
	for (int32 Index = 0; Index < Cases.Num(); ++Index)
	{
		Forward.Add(Index);
	}
	TArray<int32> Reverse = Forward;
	Algo::Reverse(Reverse);
	TArray<int32> Shuffled = Forward;
	FRandomStream ShuffleStream(0xC0FFEEu);
	for (int32 Index = Shuffled.Num() - 1; Index > 0; --Index)
	{
		Shuffled.Swap(Index, ShuffleStream.RandRange(0, Index));
	}

	for (const bool bWrapsArea : { false, true })
	{
		const TArray<uint32> ForwardResult = SplatInOrder(Cases, Forward, InputResolution, InputCellCm, bWrapsArea);
		const TArray<uint32> ReverseResult = SplatInOrder(Cases, Reverse, InputResolution, InputCellCm, bWrapsArea);
		const TArray<uint32> ShuffledResult = SplatInOrder(Cases, Shuffled, InputResolution, InputCellCm, bWrapsArea);
		const FString Label = bWrapsArea ? TEXT("wrapped") : TEXT("clamped");
		TestTrue(FString::Printf(TEXT("Reverse order matches forward (%s)"), *Label), ReverseResult == ForwardResult);
		TestTrue(FString::Printf(TEXT("Shuffled order matches forward (%s)"), *Label), ShuffledResult == ForwardResult);
		uint32 MaximumDensity = 0;
		for (int32 Index = 0; Index < InputResolution * InputResolution; ++Index)
		{
			MaximumDensity = FMath::Max(MaximumDensity, ForwardResult[Index]);
		}
		TestTrue(FString::Printf(TEXT("Splat wrote density (%s)"), *Label), MaximumDensity > 0u);
	}

	TArray<uint32> Single;
	Single.SetNumZeroed(InputResolution * InputResolution * TDFluidGpuSolverConstants::MassInputChannelCount);
	const FVector2D CenterCm(155.0, 206.25);
	TDFluidGpuMassInput::SplatPointReference(Single, InputResolution, InputCellCm, false, CenterCm, FVector2D(100.0, -50.0), 30.0f, 1.0f, 0.0f, true);
	const FTDFluidGpuMassInputSample Sample = TDFluidGpuMassInput::SampleReference(Single, InputResolution, InputCellCm, false, CenterCm);
	TestTrue(TEXT("Sample at the splat center has density"), Sample.Density > 0.5f);
	TestTrue(TEXT("Sample weighted mean velocity points along +X"), Sample.Weight > 0.0f && Sample.WeightedVelocity.X / Sample.Weight > 50.0);
	TestTrue(TEXT("Sample weighted mean velocity points along -Y"), Sample.WeightedVelocity.Y / Sample.Weight < -25.0);

	TArray<uint32> Outside;
	Outside.SetNumZeroed(InputResolution * InputResolution * TDFluidGpuSolverConstants::MassInputChannelCount);
	TestFalse(TEXT("Point outside a clamped area touches no cell"), TDFluidGpuMassInput::SplatPointReference(Outside, InputResolution, InputCellCm, false, FVector2D(-500.0, -500.0), FVector2D::ZeroVector, 20.0f, 1.0f, 0.0f, true));
	TestTrue(TEXT("Same point wraps into a wrapped area"), TDFluidGpuMassInput::SplatPointReference(Outside, InputResolution, InputCellCm, true, FVector2D(-500.0, -500.0), FVector2D::ZeroVector, 20.0f, 1.0f, 0.0f, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputGpuKeyAndMemoryUnchangedWhenOffTest,
	"TDGame.FluidFX.MassInput.Gpu.KeyAndMemoryUnchangedWhenOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputGpuKeyAndMemoryUnchangedWhenOffTest::RunTest(const FString& Parameters)
{
	FTDFluidGpuSimulationSettings Settings;
	Settings.Resolution = 512;
	TestEqual(TEXT("Default settings keep divisor 0"), FTDFluidGpuBatch::MakeKey(Settings).MassInputDivisor, 0);

	Settings.bAcceptsMassInput = true;
	TestEqual(TEXT("Accepting settings use divisor 2"), FTDFluidGpuBatch::MakeKey(Settings).MassInputDivisor, 2);
	Settings.MassInputDivisor = 3;
	TestEqual(TEXT("Divisor 3 resolves to 4 in the key"), FTDFluidGpuBatch::MakeKey(Settings).MassInputDivisor, 4);

	FTDFluidGpuSimulationSettings PainterSettings;
	PainterSettings.SimMode = ETDFluidGpuSimMode::SimplePainter;
	PainterSettings.bAcceptsMassInput = true;
	TestEqual(TEXT("Painter batches never consume mass input"), FTDFluidGpuBatch::MakeKey(PainterSettings).MassInputDivisor, 0);

	FTDFluidGpuSimulationSettings DefaultSettings;
	FTDFluidGpuSimulationSettings OffSettings = DefaultSettings;
	OffSettings.bAcceptsMassInput = false;
	OffSettings.MassInputDivisor = 4;
	TestTrue(TEXT("Mass fields do not change the key while off"), FTDFluidGpuBatch::MakeKey(OffSettings) == FTDFluidGpuBatch::MakeKey(DefaultSettings));

	const FTDFluidGpuMemoryEstimate Legacy = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 512, false, false, 0);
	const FTDFluidGpuMemoryEstimate Off = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 512, false, false, 0, 0);
	const FTDFluidGpuMemoryEstimate On = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 512, false, false, 0, 2);
	TestEqual(TEXT("Memory estimate is unchanged while off"), Off.GetTotalBytes(), Legacy.GetTotalBytes());
	TestEqual(TEXT("512 with divisor 2 adds 1.25 MiB plus counters"), On.GetTotalBytes() - Legacy.GetTotalBytes(), static_cast<int64>(5 * 256 * 256 * 4 + 16));
	return true;
}

namespace TDFluidGpuMassInputTestPrivate
{
	constexpr int32 RhiResolution = 64;
	constexpr double RhiExtentCm = 3200.0;
	constexpr float RhiDeltaTimeSeconds = 1.0f / 60.0f;
	constexpr uint64 RhiActorKey = 0x4D415353ull;

	using FTDMassFrameBuilder = TFunction<void(int32, FTDFluidGpuFrameInputs&)>;

	struct FTDMassRhiRun
	{
		bool bHasReadback = false;
		FTDFluidGpuReadbackFrame Readback;
	};

	struct FTDMassPassTiming
	{
		TMap<FString, TArray<double>> SegmentMilliseconds;
		int32 MeasuredFrameCount = 0;
		int32 RejectedFrameCount = 0;
	};

	bool CanRunMassRhiTest(FAutomationTestBase& Test, const TCHAR* TestName)
	{
		if (!GUsingNullRHI && FApp::CanEverRender())
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI. Run with build_and_test.py --rhi offscreen."), TestName));
		return false;
	}

	FTDMassRhiRun RunMassFrames(int32 FrameCount, const FTDMassFrameBuilder& BuildFrame)
	{
		TSharedRef<FTDMassRhiRun, ESPMode::ThreadSafe> Run = MakeShared<FTDMassRhiRun, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuMassInputRhiFrames)(
			[Run, BuildFrame, FrameCount](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				TUniquePtr<FTDFluidGpuReadbackRing> ReadbackRing = MakeUnique<FTDFluidGpuReadbackRing>();
				for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					BuildFrame(FrameIndex, FrameInputs);
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuMassInputRhiTest"));
					const FTDFluidGpuFrameOutputs Outputs = AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
					if (FrameIndex == FrameCount - 1)
					{
						ReadbackRing->Enqueue(GraphBuilder, Outputs);
					}
					GraphBuilder.Execute();
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}
				RHICmdList.SubmitAndBlockUntilGPUIdle();
				Run->bHasReadback = ReadbackRing->TryReadLatest(Run->Readback);
				Batches.Empty();
				ReadbackRing.Reset();
			});
		FlushRenderingCommands();
		return *Run;
	}

	FTDMassRhiRun RunTwoFrames(const FTDFluidGpuActorInput& FirstFrameActor, const FTDFluidGpuActorInput& SecondFrameActor)
	{
		return RunMassFrames(2, [FirstFrameActor, SecondFrameActor](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			OutFrame.Actors.Add(FrameIndex == 0 ? FirstFrameActor : SecondFrameActor);
		});
	}

	FTDFluidGpuActorInput MakeMassActor(int32 Divisor, float MaxCellSpeedCmPerSec)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = RhiActorKey;
		Actor.Settings.Resolution = RhiResolution;
		Actor.Settings.ExtentCm = FVector2D(RhiExtentCm, RhiExtentCm);
		Actor.Settings.PressureIterations = 8;
		Actor.Settings.VorticityStrength = 0.0f;
		Actor.Settings.DensityDissipation = 0.0f;
		Actor.Settings.VelocityDamping = 0.0f;
		Actor.Settings.TerrainSlopeGravity = 0.0f;
		Actor.Settings.AdvectionSpeedScale = 0.0f;
		Actor.Settings.BorderFadeRatio = 0.0f;
		Actor.Settings.MaxCellSpeedCmPerSec = MaxCellSpeedCmPerSec;
		Actor.Settings.bAcceptsMassInput = true;
		Actor.Settings.MassInputDivisor = Divisor;
		Actor.DeltaTimeSeconds = RhiDeltaTimeSeconds;
		return Actor;
	}

	FTDFluidGpuActorInput MakeSecondFrameActor(const FTDFluidGpuActorInput& FirstFrameActor)
	{
		FTDFluidGpuActorInput Actor = FirstFrameActor;
		Actor.SimulationTimeSeconds = RhiDeltaTimeSeconds;
		Actor.bHasObstacleListChanged = false;
		return Actor;
	}

	double GetRhiCellCm()
	{
		return RhiExtentCm / RhiResolution;
	}

	FVector2D GetWindowMinimum(const FTDFluidGpuActorInput& Actor)
	{
		return Actor.WindowCenterPlaneCm - 0.5 * Actor.Settings.ExtentCm;
	}

	FVector2D GetCellCenter(int32 CellX, int32 CellY, const FVector2D& WindowMinimum)
	{
		return WindowMinimum + FVector2D(CellX + 0.5, CellY + 0.5) * GetRhiCellCm();
	}

	TArray<FFloat16Color> ExtractFirstTile(const FTDMassRhiRun& Run)
	{
		TArray<FFloat16Color> Tile;
		const FTDFluidGpuReadbackFrame& Frame = Run.Readback;
		const int32 TileResolution = TDFluidGpuSolverConstants::ReadbackTileResolution;
		const FIntPoint AtlasSize = Frame.Layout.GetAtlasSize();
		if (!Run.bHasReadback || Frame.Layout.TileCount < 1 || Frame.Texels.Num() < AtlasSize.X * AtlasSize.Y)
		{
			return Tile;
		}
		const FIntPoint Origin = Frame.Layout.GetTileOrigin(0);
		Tile.Reserve(TileResolution * TileResolution);
		for (int32 TexelY = 0; TexelY < TileResolution; ++TexelY)
		{
			for (int32 TexelX = 0; TexelX < TileResolution; ++TexelX)
			{
				Tile.Add(Frame.Texels[(Origin.Y + TexelY) * AtlasSize.X + Origin.X + TexelX]);
			}
		}
		return Tile;
	}

	bool AreTilesBitIdentical(const TArray<FFloat16Color>& Left, const TArray<FFloat16Color>& Right)
	{
		return !Left.IsEmpty() && Left.Num() == Right.Num() && FMemory::Memcmp(Left.GetData(), Right.GetData(), Left.Num() * sizeof(FFloat16Color)) == 0;
	}

	FTDFluidGpuMassInputCounters FindRhiCounters(const FTDMassRhiRun& Run)
	{
		const FTDFluidGpuMassInputCounters* Found = Run.Readback.MassInputCounters.FindByPredicate([](const FTDFluidGpuMassInputCounters& Counters)
		{
			return Counters.ActorKey == RhiActorKey;
		});
		return Found ? *Found : FTDFluidGpuMassInputCounters();
	}

	bool AreCountersEqual(const FTDFluidGpuMassInputCounters& Left, const FTDFluidGpuMassInputCounters& Right)
	{
		return Left.SplatCount == Right.SplatCount && Left.OutsideCount == Right.OutsideCount && Left.bSaturated == Right.bSaturated;
	}

	float FindMaximumDensity(const TArray<FFloat16Color>& Tile)
	{
		float MaximumDensity = 0.0f;
		for (const FFloat16Color& Texel : Tile)
		{
			MaximumDensity = FMath::Max(MaximumDensity, Texel.R.GetFloat());
		}
		return MaximumDensity;
	}

	float FindMaximumSpeed(const TArray<FFloat16Color>& Tile)
	{
		float MaximumSpeed = 0.0f;
		for (const FFloat16Color& Texel : Tile)
		{
			MaximumSpeed = FMath::Max(MaximumSpeed, FVector2f(Texel.B.GetFloat(), Texel.A.GetFloat()).Size());
		}
		return MaximumSpeed;
	}

	TArray<FTDFluidGpuMassPointInput> MakeRandomMassPoints(int32 Count, bool bWithVelocity)
	{
		FRandomStream Stream(0x51A7u);
		const float HalfExtent = static_cast<float>(0.5 * RhiExtentCm);
		TArray<FTDFluidGpuMassPointInput> Points;
		Points.Reserve(Count);
		for (int32 PointIndex = 0; PointIndex < Count; ++PointIndex)
		{
			FTDFluidGpuMassPointInput& Point = Points.AddDefaulted_GetRef();
			Point.CenterPlaneCm = FVector2D(Stream.FRandRange(-HalfExtent - 150.0f, HalfExtent + 150.0f), Stream.FRandRange(-HalfExtent - 150.0f, HalfExtent + 150.0f));
			const FVector2D Velocity(Stream.FRandRange(-30.0f, 30.0f), Stream.FRandRange(-30.0f, 30.0f));
			Point.VelocityPlaneCmPerSec = bWithVelocity ? Velocity : FVector2D::ZeroVector;
			Point.RadiusCm = Stream.FRandRange(20.0f, 220.0f);
			Point.DensityStrength = Stream.FRandRange(0.05f, 1.2f);
			Point.bWritesVelocity = bWithVelocity && PointIndex % 4 != 0;
		}
		return Points;
	}

	TArray<FTDFluidGpuMassPointInput> ShufflePoints(const TArray<FTDFluidGpuMassPointInput>& Points, int32 Seed)
	{
		FRandomStream Stream(Seed);
		TArray<FTDFluidGpuMassPointInput> Shuffled = Points;
		for (int32 Index = Shuffled.Num() - 1; Index > 0; --Index)
		{
			Shuffled.Swap(Index, Stream.RandRange(0, Index));
		}
		return Shuffled;
	}

	FTDMassRhiRun RunMassPoints(const FTDFluidGpuActorInput& FirstFrameActor, const TArray<FTDFluidGpuMassPointInput>& Points)
	{
		FTDFluidGpuActorInput SecondFrameActor = MakeSecondFrameActor(FirstFrameActor);
		SecondFrameActor.MassPoints = Points;
		return RunTwoFrames(FirstFrameActor, SecondFrameActor);
	}

	FTDMassPassTiming MeasureMassPassTimings(int32 WarmupFrameCount, int32 MeasuredFrameCount, const FTDMassFrameBuilder& BuildFrame)
	{
		TSharedRef<FTDMassPassTiming, ESPMode::ThreadSafe> Timing = MakeShared<FTDMassPassTiming, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuMassInputPassTiming)(
			[Timing, BuildFrame, WarmupFrameCount, MeasuredFrameCount](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				TArray<FRenderQueryRHIRef> StartQueries;
				TArray<FTDFluidGpuPassTimestamps> FrameTimestamps;
				FrameTimestamps.Reserve(MeasuredFrameCount);
				for (int32 FrameIndex = 0; FrameIndex < WarmupFrameCount + MeasuredFrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					BuildFrame(FrameIndex, FrameInputs);
					if (FrameIndex < WarmupFrameCount)
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuMassInputWarmupFrame"));
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
						GraphBuilder.Execute();
						RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
						continue;
					}

					RHICmdList.EndRenderQuery(StartQueries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuMassInputTimedFrame"));
						AddTDFluidGpuTimestampedFramePasses(GraphBuilder, FrameInputs, Batches, FrameTimestamps.AddDefaulted_GetRef());
						GraphBuilder.Execute();
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				for (int32 TimedIndex = 0; TimedIndex < FrameTimestamps.Num(); ++TimedIndex)
				{
					uint64 PreviousMicroseconds = 0;
					if (!RHIGetRenderQueryResult(StartQueries[TimedIndex], PreviousMicroseconds, true))
					{
						++Timing->RejectedFrameCount;
						continue;
					}

					const FTDFluidGpuPassTimestamps& Timestamps = FrameTimestamps[TimedIndex];
					TArray<TPair<FString, double>> Segments;
					bool bIsValidFrame = true;
					for (int32 LabelIndex = 0; LabelIndex < Timestamps.Queries.Num(); ++LabelIndex)
					{
						uint64 Microseconds = 0;
						if (!RHIGetRenderQueryResult(Timestamps.Queries[LabelIndex], Microseconds, true) || Microseconds < PreviousMicroseconds)
						{
							bIsValidFrame = false;
							break;
						}
						Segments.Emplace(Timestamps.Labels[LabelIndex], static_cast<double>(Microseconds - PreviousMicroseconds) / 1000.0);
						PreviousMicroseconds = Microseconds;
					}
					if (!bIsValidFrame)
					{
						++Timing->RejectedFrameCount;
						continue;
					}

					++Timing->MeasuredFrameCount;
					for (const TPair<FString, double>& Segment : Segments)
					{
						Timing->SegmentMilliseconds.FindOrAdd(Segment.Key).Add(Segment.Value);
					}
				}
				Batches.Empty();
			});
		FlushRenderingCommands();
		return *Timing;
	}

	double ComputeMeanSegmentMilliseconds(const FTDMassPassTiming& Timing, const TCHAR* Label)
	{
		const TArray<double>* Values = Timing.SegmentMilliseconds.Find(Label);
		if (!Values || Values->IsEmpty())
		{
			return -1.0;
		}
		double Sum = 0.0;
		for (const double Value : *Values)
		{
			Sum += Value;
		}
		return Sum / Values->Num();
	}

	bool WriteCostSection(const FString& SectionName, const TSharedRef<FJsonObject>& Section)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/fx5/cost-mass-input.json"));
		TSharedPtr<FJsonObject> Root;
		FString ExistingText;
		if (FFileHelper::LoadFileToString(ExistingText, *Path))
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExistingText);
			FJsonSerializer::Deserialize(Reader, Root);
		}
		if (!Root.IsValid())
		{
			Root = MakeShared<FJsonObject>();
		}
		Root->SetObjectField(SectionName, Section);
		FString OutputText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputText);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
		return FFileHelper::SaveStringToFile(OutputText, *Path);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputRhiSplatOrderIndependentTest,
	"TDGame.FluidFXRhi.MassInput.SplatOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputRhiSplatOrderIndependentTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuMassInputTestPrivate;
	if (!CanRunMassRhiTest(*this, TEXT("MassInput.SplatOrderIndependent")))
	{
		return false;
	}

	constexpr int32 PointCount = 10000;
	const FTDFluidGpuActorInput Actor = MakeMassActor(TDFluidGpuSolverConstants::DefaultMassInputDivisor, TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec);
	const TArray<FTDFluidGpuMassPointInput> Forward = MakeRandomMassPoints(PointCount, true);
	TArray<FTDFluidGpuMassPointInput> Reversed = Forward;
	Algo::Reverse(Reversed);
	const TArray<FTDFluidGpuMassPointInput> Shuffled = ShufflePoints(Forward, 0xC0FFEE);

	const FTDMassRhiRun ForwardRun = RunMassPoints(Actor, Forward);
	const FTDMassRhiRun ReversedRun = RunMassPoints(Actor, Reversed);
	const FTDMassRhiRun ShuffledRun = RunMassPoints(Actor, Shuffled);
	const TArray<FFloat16Color> ForwardTile = ExtractFirstTile(ForwardRun);
	TestTrue(TEXT("Readback exists"), ForwardRun.bHasReadback && ForwardTile.Num() == RhiResolution * RhiResolution);
	TestTrue(TEXT("Reversed order is bit identical"), AreTilesBitIdentical(ForwardTile, ExtractFirstTile(ReversedRun)));
	TestTrue(TEXT("Shuffled order is bit identical"), AreTilesBitIdentical(ForwardTile, ExtractFirstTile(ShuffledRun)));

	const FTDFluidGpuMassInputCounters ForwardCounters = FindRhiCounters(ForwardRun);
	TestTrue(TEXT("Counters match across orders"), AreCountersEqual(ForwardCounters, FindRhiCounters(ReversedRun)) && AreCountersEqual(ForwardCounters, FindRhiCounters(ShuffledRun)));
	TestEqual(TEXT("Splat and outside counts cover every point"), static_cast<int32>(ForwardCounters.SplatCount + ForwardCounters.OutsideCount), PointCount);
	TestTrue(TEXT("Some points land inside and some outside"), ForwardCounters.SplatCount > 0 && ForwardCounters.OutsideCount > 0);
	TestTrue(TEXT("Velocity was injected"), FindMaximumSpeed(ForwardTile) > 0.0f);

	const TArray<FTDFluidGpuMassPointInput> DensityPoints = MakeRandomMassPoints(PointCount, false);
	const TArray<FFloat16Color> DensityTile = ExtractFirstTile(RunMassPoints(Actor, DensityPoints));
	const int32 InputResolution = TDFluidGpuMassInput::ComputeInputResolution(RhiResolution, TDFluidGpuSolverConstants::DefaultMassInputDivisor);
	const FVector2D InputCellCm = FVector2D(GetRhiCellCm(), GetRhiCellCm()) * static_cast<double>(TDFluidGpuSolverConstants::DefaultMassInputDivisor);
	const FVector2D Origin = GetWindowMinimum(Actor);
	TArray<uint32> Reference;
	Reference.SetNumZeroed(InputResolution * InputResolution * TDFluidGpuSolverConstants::MassInputChannelCount);
	for (const FTDFluidGpuMassPointInput& Point : DensityPoints)
	{
		const FVector2D LocalCm = FVector2D(FVector2f(Point.CenterPlaneCm - Origin));
		const float RadiusCm = TDFluidGpuMassInput::ResolveSplatRadius(Point.RadiusCm, InputCellCm, TDFluidGpuSolverConstants::DefaultMassSplatRadiusCapCells);
		TDFluidGpuMassInput::SplatPointReference(Reference, InputResolution, InputCellCm, false, LocalCm, FVector2D::ZeroVector, RadiusCm, Point.DensityStrength, 0.0f, false);
	}

	int32 MismatchCount = 0;
	double MaximumError = 0.0;
	if (DensityTile.Num() == RhiResolution * RhiResolution)
	{
		for (int32 CellY = 1; CellY < RhiResolution - 1; ++CellY)
		{
			for (int32 CellX = 1; CellX < RhiResolution - 1; ++CellX)
			{
				const FVector2D SamplePositionCm = FVector2D(CellX + 0.5, CellY + 0.5) * GetRhiCellCm();
				const double Expected = FMath::Max(0.0f, TDFluidGpuMassInput::SampleReference(Reference, InputResolution, InputCellCm, false, SamplePositionCm).Density);
				const double Actual = DensityTile[CellY * RhiResolution + CellX].R.GetFloat();
				const double Error = FMath::Abs(Actual - Expected);
				MaximumError = FMath::Max(MaximumError, Error);
				MismatchCount += Error > 4.0 / TDFluidGpuSolverConstants::MassInputDensityScale + Expected / 1024.0 ? 1 : 0;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("Reference density max error %.3g, mismatches %d"), MaximumError, MismatchCount));
	TestTrue(TEXT("GPU density matches the reference splat within quantization and half precision"), DensityTile.Num() == RhiResolution * RhiResolution && MismatchCount == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputRhiSaturationGuardDeterministicTest,
	"TDGame.FluidFXRhi.MassInput.SaturationGuardDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputRhiSaturationGuardDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuMassInputTestPrivate;
	if (!CanRunMassRhiTest(*this, TEXT("MassInput.SaturationGuardDeterministic")))
	{
		return false;
	}

	constexpr float ExtremeCellSpeed = 1.0e6f;
	const uint32 SafeCount = TDFluidGpuMassInput::ComputeSafeCount(ExtremeCellSpeed);
	TestEqual(TEXT("SafeCount for 1e6 cm/s"), SafeCount, 268u);

	const FTDFluidGpuActorInput Actor = MakeMassActor(TDFluidGpuSolverConstants::DefaultMassInputDivisor, ExtremeCellSpeed);
	FTDFluidGpuMassPointInput StackedPoint;
	StackedPoint.CenterPlaneCm = FVector2D(10.0, -10.0);
	StackedPoint.VelocityPlaneCmPerSec = FVector2D(300.0, 0.0);
	StackedPoint.RadiusCm = 120.0f;
	StackedPoint.DensityStrength = 0.8f;
	TArray<FTDFluidGpuMassPointInput> SafePoints;
	SafePoints.Init(StackedPoint, static_cast<int32>(SafeCount));
	TArray<FTDFluidGpuMassPointInput> OverPoints;
	OverPoints.Init(StackedPoint, static_cast<int32>(SafeCount) + 1);

	const FTDMassRhiRun SafeRun = RunMassPoints(Actor, SafePoints);
	const FTDMassRhiRun SafeRepeat = RunMassPoints(Actor, SafePoints);
	const FTDMassRhiRun OverRun = RunMassPoints(Actor, OverPoints);
	const FTDMassRhiRun OverRepeat = RunMassPoints(Actor, OverPoints);
	const TArray<FFloat16Color> SafeTile = ExtractFirstTile(SafeRun);
	const TArray<FFloat16Color> OverTile = ExtractFirstTile(OverRun);
	TestTrue(TEXT("Safe count repeat is bit identical"), AreTilesBitIdentical(SafeTile, ExtractFirstTile(SafeRepeat)) && AreCountersEqual(FindRhiCounters(SafeRun), FindRhiCounters(SafeRepeat)));
	TestTrue(TEXT("Over count repeat is bit identical"), AreTilesBitIdentical(OverTile, ExtractFirstTile(OverRepeat)) && AreCountersEqual(FindRhiCounters(OverRun), FindRhiCounters(OverRepeat)));

	const FTDFluidGpuMassInputCounters SafeCounters = FindRhiCounters(SafeRun);
	const FTDFluidGpuMassInputCounters OverCounters = FindRhiCounters(OverRun);
	TestEqual(TEXT("Safe run splats every point"), SafeCounters.SplatCount, SafeCount);
	TestFalse(TEXT("Safe run is not saturated"), SafeCounters.bSaturated);
	TestEqual(TEXT("Over run splats every point"), OverCounters.SplatCount, SafeCount + 1u);
	TestTrue(TEXT("Over run raises the saturation flag"), OverCounters.bSaturated);
	TestTrue(TEXT("Safe run applies velocity"), FindMaximumSpeed(SafeTile) > 1.0f);
	TestTrue(TEXT("Over run skips velocity"), !OverTile.IsEmpty() && FindMaximumSpeed(OverTile) < 1.0e-6f);
	TestTrue(TEXT("Both runs apply density"), FindMaximumDensity(SafeTile) > 0.2f && FindMaximumDensity(OverTile) > 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputRhiMatchesStampPathForSparsePointsTest,
	"TDGame.FluidFXRhi.MassInput.MatchesStampPathForSparsePoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputRhiMatchesStampPathForSparsePointsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuMassInputTestPrivate;
	if (!CanRunMassRhiTest(*this, TEXT("MassInput.MatchesStampPathForSparsePoints")))
	{
		return false;
	}

	const FTDFluidGpuActorInput Actor = MakeMassActor(1, TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec);
	FTDFluidGpuActorInput StampActor = MakeSecondFrameActor(Actor);
	FTDFluidGpuActorInput MassActor = MakeSecondFrameActor(Actor);
	FRandomStream Stream(0x5A3);
	for (int32 Row = 0; Row < 5; ++Row)
	{
		for (int32 Column = 0; Column < 10; ++Column)
		{
			const FVector2D Center(-1350.0 + 300.0 * Column, -1000.0 + 500.0 * Row);
			const FVector2D Velocity(Stream.FRandRange(-30.0f, 30.0f), Stream.FRandRange(-30.0f, 30.0f));
			const float Density = Stream.FRandRange(0.3f, 1.0f);
			FTDFluidGpuStampInput& Stamp = StampActor.Stamps.AddDefaulted_GetRef();
			Stamp.CenterPlaneCm = Center;
			Stamp.VelocityPlaneCmPerSec = Velocity;
			Stamp.RadiusCm = 100.0f;
			Stamp.DensityStrength = Density;
			Stamp.VelocityStrength = 1.0f;
			FTDFluidGpuMassPointInput& Point = MassActor.MassPoints.AddDefaulted_GetRef();
			Point.CenterPlaneCm = Center;
			Point.VelocityPlaneCmPerSec = Velocity;
			Point.RadiusCm = 100.0f;
			Point.DensityStrength = Density;
		}
	}

	const TArray<FFloat16Color> StampTile = ExtractFirstTile(RunTwoFrames(Actor, StampActor));
	const TArray<FFloat16Color> MassTile = ExtractFirstTile(RunTwoFrames(Actor, MassActor));
	const float StampVelocityBlend = FMath::Min(1.0f, TDFluidGpuSolverConstants::BrushVelocityResponsePerSec * RhiDeltaTimeSeconds);
	const double VelocityTolerance = StampVelocityBlend * (1.0 / 16.0 + TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec / 8192.0) + 1.0e-4;
	const double DensityTolerance = FMath::Pow(2.0, -20.0);
	TestTrue(TEXT("Both runs read back"), StampTile.Num() == RhiResolution * RhiResolution && MassTile.Num() == StampTile.Num());
	TestTrue(TEXT("Stamps injected density"), FindMaximumDensity(StampTile) > 0.25f);

	double MaximumDensityError = 0.0;
	double MaximumVelocityError = 0.0;
	int32 MismatchCount = 0;
	for (int32 TexelIndex = 0; TexelIndex < FMath::Min(StampTile.Num(), MassTile.Num()); ++TexelIndex)
	{
		const FFloat16Color& StampTexel = StampTile[TexelIndex];
		const FFloat16Color& MassTexel = MassTile[TexelIndex];
		const double DensityError = FMath::Abs(static_cast<double>(MassTexel.R.GetFloat()) - StampTexel.R.GetFloat());
		const double VelocityError = FMath::Max(FMath::Abs(static_cast<double>(MassTexel.B.GetFloat()) - StampTexel.B.GetFloat()), FMath::Abs(static_cast<double>(MassTexel.A.GetFloat()) - StampTexel.A.GetFloat()));
		const double HalfPrecisionDensity = FMath::Abs(StampTexel.R.GetFloat()) / 1024.0;
		const double HalfPrecisionVelocity = FMath::Max(FMath::Abs(StampTexel.B.GetFloat()), FMath::Abs(StampTexel.A.GetFloat())) / 1024.0;
		MaximumDensityError = FMath::Max(MaximumDensityError, DensityError);
		MaximumVelocityError = FMath::Max(MaximumVelocityError, VelocityError);
		MismatchCount += DensityError > DensityTolerance + HalfPrecisionDensity || VelocityError > VelocityTolerance + HalfPrecisionVelocity ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Stamp vs mass: density max error %.3g, velocity max error %.3g (tolerance %.3g), mismatches %d"), MaximumDensityError, MaximumVelocityError, VelocityTolerance, MismatchCount));
	TestEqual(TEXT("Mass path matches the stamp path within quantization"), MismatchCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputRhiWindowShiftKeepsAlignmentTest,
	"TDGame.FluidFXRhi.MassInput.WindowShiftKeepsAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputRhiWindowShiftKeepsAlignmentTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuMassInputTestPrivate;
	if (!CanRunMassRhiTest(*this, TEXT("MassInput.WindowShiftKeepsAlignment")))
	{
		return false;
	}

	const FTDFluidGpuActorInput Actor = MakeMassActor(TDFluidGpuSolverConstants::DefaultMassInputDivisor, TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec);
	FTDFluidGpuActorInput ShiftedActor = MakeSecondFrameActor(Actor);
	const FIntPoint ShiftCells(7, -7);
	ShiftedActor.ShiftCells = ShiftCells;
	ShiftedActor.WindowCenterPlaneCm = FVector2D(ShiftCells.X, ShiftCells.Y) * GetRhiCellCm();
	FTDFluidGpuMassPointInput& Point = ShiftedActor.MassPoints.AddDefaulted_GetRef();
	Point.CenterPlaneCm = FVector2D(-200.0, 300.0);
	Point.RadiusCm = 150.0f;
	Point.DensityStrength = 1.0f;
	Point.bWritesVelocity = false;

	const TArray<FFloat16Color> Tile = ExtractFirstTile(RunTwoFrames(Actor, ShiftedActor));
	double DensitySum = 0.0;
	FVector2D WeightedCell = FVector2D::ZeroVector;
	for (int32 TexelIndex = 0; TexelIndex < Tile.Num(); ++TexelIndex)
	{
		const double Density = Tile[TexelIndex].R.GetFloat();
		DensitySum += Density;
		WeightedCell += FVector2D(TexelIndex % RhiResolution, TexelIndex / RhiResolution) * Density;
	}
	TestTrue(TEXT("Shifted splat injected density"), DensitySum > 0.0);
	const FVector2D Centroid = DensitySum > 0.0 ? WeightedCell / DensitySum : FVector2D(-1.0, -1.0);
	const FVector2D Expected = (Point.CenterPlaneCm - GetWindowMinimum(ShiftedActor)) / GetRhiCellCm() - FVector2D(0.5, 0.5);
	const double ToleranceCells = 0.5 * TDFluidGpuSolverConstants::DefaultMassInputDivisor;
	AddInfo(FString::Printf(TEXT("Centroid (%.3f, %.3f), expected (%.3f, %.3f)"), Centroid.X, Centroid.Y, Expected.X, Expected.Y));
	TestTrue(TEXT("Centroid stays within half an input cell after a 7 cell shift"), FMath::Abs(Centroid.X - Expected.X) <= ToleranceCells && FMath::Abs(Centroid.Y - Expected.Y) <= ToleranceCells);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputRhiObstacleCellsStayEmptyTest,
	"TDGame.FluidFXRhi.MassInput.ObstacleCellsStayEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputRhiObstacleCellsStayEmptyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuMassInputTestPrivate;
	if (!CanRunMassRhiTest(*this, TEXT("MassInput.ObstacleCellsStayEmpty")))
	{
		return false;
	}

	constexpr double ObstacleRadiusCm = 400.0;
	FTDFluidGpuActorInput Actor = MakeMassActor(TDFluidGpuSolverConstants::DefaultMassInputDivisor, TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec);
	Actor.bHasObstacleListChanged = true;
	FTDFluidGpuObstacleInput& Obstacle = Actor.Obstacles.AddDefaulted_GetRef();
	Obstacle.CenterPlaneCm = FVector2D::ZeroVector;
	Obstacle.RadiusCm = static_cast<float>(ObstacleRadiusCm);
	FTDFluidGpuActorInput SplatActor = MakeSecondFrameActor(Actor);
	for (int32 PointIndex = 0; PointIndex < 16; ++PointIndex)
	{
		const double Angle = PointIndex * UE_DOUBLE_PI / 8.0;
		FTDFluidGpuMassPointInput& Point = SplatActor.MassPoints.AddDefaulted_GetRef();
		Point.CenterPlaneCm = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 250.0;
		Point.VelocityPlaneCmPerSec = FVector2D(60.0, 0.0);
		Point.RadiusCm = 220.0f;
		Point.DensityStrength = 1.0f;
	}

	const TArray<FFloat16Color> Tile = ExtractFirstTile(RunTwoFrames(Actor, SplatActor));
	const FVector2D WindowMinimum = GetWindowMinimum(SplatActor);
	int32 SolidCellCount = 0;
	int32 NonEmptySolidCellCount = 0;
	double OutsideDensity = 0.0;
	for (int32 TexelIndex = 0; TexelIndex < Tile.Num(); ++TexelIndex)
	{
		const double Distance = GetCellCenter(TexelIndex % RhiResolution, TexelIndex / RhiResolution, WindowMinimum).Size();
		const FFloat16Color& Texel = Tile[TexelIndex];
		if (Distance < ObstacleRadiusCm - 1.5 * GetRhiCellCm())
		{
			++SolidCellCount;
			NonEmptySolidCellCount += Texel.R.GetFloat() != 0.0f || Texel.B.GetFloat() != 0.0f || Texel.A.GetFloat() != 0.0f ? 1 : 0;
		}
		else if (Distance > ObstacleRadiusCm + GetRhiCellCm())
		{
			OutsideDensity += Texel.R.GetFloat();
		}
	}
	TestTrue(TEXT("Obstacle covers cells"), SolidCellCount > 0);
	TestEqual(TEXT("Obstacle cells stay empty"), NonEmptySolidCellCount, 0);
	TestTrue(TEXT("Splats reach fluid cells outside the obstacle"), OutsideDensity > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputRhiOffscreenGpuPassCostTest,
	"TDGame.FluidFXRhi.MassInput.OffscreenGpuPassCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputRhiOffscreenGpuPassCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuMassInputTestPrivate;
	if (!CanRunMassRhiTest(*this, TEXT("MassInput.OffscreenGpuPassCost")))
	{
		return false;
	}

	constexpr int32 CostResolution = 256;
	constexpr int32 WarmupFrameCount = 4;
	constexpr int32 MeasuredFrameCount = 30;
	FTDFluidGpuActorInput Template = MakeMassActor(TDFluidGpuSolverConstants::DefaultMassInputDivisor, TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec);
	Template.Settings.Resolution = CostResolution;
	auto MeasureConfiguration = [&Template](bool bAcceptsMassInput, int32 PointCount)
	{
		FTDFluidGpuActorInput Actor = Template;
		Actor.Settings.bAcceptsMassInput = bAcceptsMassInput;
		const TArray<FTDFluidGpuMassPointInput> Points = MakeRandomMassPoints(PointCount, true);
		return MeasureMassPassTimings(WarmupFrameCount, MeasuredFrameCount, [Actor, Points](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			FTDFluidGpuActorInput& FrameActor = OutFrame.Actors.Add_GetRef(Actor);
			FrameActor.SimulationTimeSeconds = FrameIndex * static_cast<double>(RhiDeltaTimeSeconds);
			FrameActor.MassPoints = Points;
		});
	};

	const FTDMassPassTiming Baseline = MeasureConfiguration(false, 0);
	const double BaselineApplyMilliseconds = ComputeMeanSegmentMilliseconds(Baseline, TEXT("apply_stamps"));
	TSharedRef<FJsonObject> Section = MakeShared<FJsonObject>();
	Section->SetNumberField(TEXT("resolution"), CostResolution);
	Section->SetNumberField(TEXT("divisor"), TDFluidGpuSolverConstants::DefaultMassInputDivisor);
	Section->SetNumberField(TEXT("measured_frames"), MeasuredFrameCount);
	Section->SetNumberField(TEXT("baseline_apply_stamps_ms"), BaselineApplyMilliseconds);
	bool bAllFinite = Baseline.MeasuredFrameCount > 0 && BaselineApplyMilliseconds >= 0.0;
	for (const int32 PointCount : {1000, 10000})
	{
		const FTDMassPassTiming Timing = MeasureConfiguration(true, PointCount);
		const double SplatMilliseconds = ComputeMeanSegmentMilliseconds(Timing, TEXT("mass_splat"));
		const double ApplyMilliseconds = ComputeMeanSegmentMilliseconds(Timing, TEXT("apply_stamps"));
		const double ClearMilliseconds = ComputeMeanSegmentMilliseconds(Timing, TEXT("mass_clear"));
		TSharedRef<FJsonObject> PointSection = MakeShared<FJsonObject>();
		PointSection->SetNumberField(TEXT("mass_splat_ms"), SplatMilliseconds);
		PointSection->SetNumberField(TEXT("apply_stamps_ms"), ApplyMilliseconds);
		PointSection->SetNumberField(TEXT("apply_increment_ms"), ApplyMilliseconds - BaselineApplyMilliseconds);
		PointSection->SetNumberField(TEXT("mass_clear_ms"), ClearMilliseconds);
		PointSection->SetNumberField(TEXT("rejected_frames"), Timing.RejectedFrameCount);
		Section->SetObjectField(FString::Printf(TEXT("points_%d"), PointCount), PointSection);
		AddInfo(FString::Printf(TEXT("%d points: mass_splat %.4f ms, apply_stamps %.4f ms (baseline %.4f), mass_clear %.4f ms"), PointCount, SplatMilliseconds, ApplyMilliseconds, BaselineApplyMilliseconds, ClearMilliseconds));
		bAllFinite &= Timing.MeasuredFrameCount > 0 && SplatMilliseconds >= 0.0 && ApplyMilliseconds >= 0.0 && ClearMilliseconds >= 0.0
			&& FMath::IsFinite(SplatMilliseconds) && FMath::IsFinite(ApplyMilliseconds) && FMath::IsFinite(ClearMilliseconds);
	}
	TestTrue(TEXT("GPU pass costs are finite"), bAllFinite);
	TestTrue(TEXT("Cost record is written"), WriteCostSection(TEXT("gpu_passes"), Section));
	return true;
}

#endif
