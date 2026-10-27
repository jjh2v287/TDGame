#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidInputBrushRules.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "UObject/Package.h"

namespace TDFluidInputBrushRulesTestPrivate
{
	constexpr int32 SeededStampCount = 64;
	constexpr int32 SeededStampRandomSeed = 1027;
	constexpr float StirStepSeconds = 1.0f / 60.0f;
	constexpr int32 StirStepCount = 30;
	constexpr int32 StirResolution = 64;
	constexpr double StirExtentCm = 2000.0;
	constexpr double StirDensityTolerance = 0.005;
	constexpr int32 CostRunCount = 50;
	constexpr int32 SmallCostStampCount = 1000;
	constexpr int32 LargeCostStampCount = 16000;
	const FVector StirActorLocation(0.0, 0.0, 0.0);
	const FVector ControlActorLocation(100000.0, 0.0, 0.0);
	const ETDFluidBrushSource AllSources[] = { ETDFluidBrushSource::Other, ETDFluidBrushSource::PointArray, ETDFluidBrushSource::DataChannel, ETDFluidBrushSource::Fragment };

	FTDFluidBrushStamp MakeSeededStamp(FRandomStream& Random, int32 Index)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(Random.FRandRange(-500.0f, 500.0f), Random.FRandRange(-500.0f, 500.0f), Random.FRandRange(-200.0f, 200.0f));
		Stamp.WorldVelocity = FVector(Random.FRandRange(-400.0f, 400.0f), Random.FRandRange(-400.0f, 400.0f), Random.FRandRange(-100.0f, 100.0f));
		Stamp.RadiusCm = Random.FRandRange(10.0f, 120.0f);
		Stamp.DensityStrength = Random.FRandRange(-1.0f, 1.0f);
		Stamp.VelocityStrength = Random.FRandRange(0.0f, 2.0f);
		Stamp.RadialVelocityCmPerSec = Random.FRandRange(-300.0f, 300.0f);
		Stamp.Channel = static_cast<ETDFluidBrushChannel>(Index % 4);
		Stamp.RippleImpulseCm = Random.FRandRange(0.0f, 5.0f);
		Stamp.HeatStrength = Random.FRandRange(0.0f, 1.0f);
		Stamp.RingInnerRadiusRatio = Random.FRandRange(0.0f, 0.9f);
		Stamp.RingSideArcWeight = Random.FRandRange(0.0f, 2.0f);
		Stamp.RingInnerEraseRatePerSec = Random.FRandRange(0.0f, 50.0f);
		Stamp.Source = AllSources[Index % static_cast<int32>(UE_ARRAY_COUNT(AllSources))];
		return Stamp;
	}

	TArray<FTDFluidBrushStamp> MakeSeededStamps(int32 Count)
	{
		FRandomStream Random(SeededStampRandomSeed);
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Stamps.Add(MakeSeededStamp(Random, Index));
		}
		return Stamps;
	}

	bool AreFloatsBitIdentical(float A, float B)
	{
		return FMemory::Memcmp(&A, &B, sizeof(float)) == 0;
	}

	bool AreVectorsBitIdentical(const FVector& A, const FVector& B)
	{
		return FMemory::Memcmp(&A.X, &B.X, sizeof(A.X)) == 0 && FMemory::Memcmp(&A.Y, &B.Y, sizeof(A.Y)) == 0 && FMemory::Memcmp(&A.Z, &B.Z, sizeof(A.Z)) == 0;
	}

	bool AreStampsBitIdentical(const FTDFluidBrushStamp& A, const FTDFluidBrushStamp& B)
	{
		return AreVectorsBitIdentical(A.WorldLocation, B.WorldLocation) && AreVectorsBitIdentical(A.WorldVelocity, B.WorldVelocity)
			&& AreFloatsBitIdentical(A.RadiusCm, B.RadiusCm) && AreFloatsBitIdentical(A.DensityStrength, B.DensityStrength)
			&& AreFloatsBitIdentical(A.VelocityStrength, B.VelocityStrength) && AreFloatsBitIdentical(A.RadialVelocityCmPerSec, B.RadialVelocityCmPerSec)
			&& A.Channel == B.Channel && AreFloatsBitIdentical(A.RippleImpulseCm, B.RippleImpulseCm) && AreFloatsBitIdentical(A.HeatStrength, B.HeatStrength)
			&& AreFloatsBitIdentical(A.RingInnerRadiusRatio, B.RingInnerRadiusRatio) && AreFloatsBitIdentical(A.RingSideArcWeight, B.RingSideArcWeight)
			&& AreFloatsBitIdentical(A.RingInnerEraseRatePerSec, B.RingInnerEraseRatePerSec) && A.Source == B.Source;
	}

	FTDFluidBrushStamp MakeRuleStamp(const FVector& Location, const FVector& Velocity, ETDFluidBrushSource Source)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Location;
		Stamp.WorldVelocity = Velocity;
		Stamp.RadiusCm = 60.0f;
		Stamp.DensityStrength = 0.8f;
		Stamp.VelocityStrength = 1.25f;
		Stamp.RadialVelocityCmPerSec = 140.0f;
		Stamp.RippleImpulseCm = 2.0f;
		Stamp.HeatStrength = 0.6f;
		Stamp.RingInnerRadiusRatio = 0.4f;
		Stamp.RingInnerEraseRatePerSec = 20.0f;
		Stamp.Source = Source;
		return Stamp;
	}

	FTDFluidPlaneBasis MakeCameraFacingBasis()
	{
		return FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(-30.0, 40.0, 0.0), 0.0f);
	}

	bool IsScaledStamp(const FTDFluidBrushStamp& Actual, const FTDFluidBrushStamp& Original, float Factor)
	{
		constexpr float Tolerance = 1.0e-5f;
		return FMath::IsNearlyEqual(Actual.DensityStrength, Original.DensityStrength * Factor, Tolerance)
			&& FMath::IsNearlyEqual(Actual.VelocityStrength, Original.VelocityStrength * Factor, Tolerance)
			&& FMath::IsNearlyEqual(Actual.RadialVelocityCmPerSec, Original.RadialVelocityCmPerSec * Factor, Tolerance * 100.0f)
			&& FMath::IsNearlyEqual(Actual.HeatStrength, Original.HeatStrength * Factor, Tolerance)
			&& FMath::IsNearlyEqual(Actual.RippleImpulseCm, Original.RippleImpulseCm * Factor, Tolerance)
			&& Actual.RadiusCm == Original.RadiusCm && Actual.RingInnerRadiusRatio == Original.RingInnerRadiusRatio;
	}

	FTDFluidSimulationConfig MakeStirConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = StirResolution;
		Config.SimExtentCm = FVector2D(StirExtentCm, StirExtentCm);
		Config.SolverBackend = ETDFluidSolverBackend::Cpu;
		Config.bEnableWaterLayer = false;
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.0f;
		Config.PressureIterations = 12;
		Config.TerrainSlopeGravity = 0.0f;
		Config.BorderFadeRatio = 0.0f;
		Config.MaxPointsPerTick = 64;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.AmbientDensity = 0.5f;
		Config.AmbientRefillPerSec = 2.0f;
		Config.ActorBrushDensityStrength = 0.0f;
		Config.ActorBrushPunctureCmPerSec = 0.0f;
		return Config;
	}

	struct FTDFluidInputBrushRulesTestWorld
	{
		FTDFluidInputBrushRulesTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidInputBrushRulesWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidInputBrushRulesTestWorld()
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

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, StirStepSeconds);
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	ATDFluidLiveActor* SpawnStirPresetActor(UWorld& TargetWorld, const FVector& Location, UTDFluidPresetAsset* Preset)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		ATDFluidLiveActor* LiveActor = TargetWorld.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}

		LiveActor->LiveComponent->SimulationConfig = Preset->SimulationConfig;
		LiveActor->FinishSpawning(SpawnTransform);
		LiveActor->InitializeFromPreset(Preset);
		return LiveActor;
	}

	void QueueStirStamps(ATDFluidLiveActor& LiveActor, const FVector& ActorLocation)
	{
		FTDFluidBrushStamp OtherStamp;
		OtherStamp.WorldLocation = ActorLocation;
		OtherStamp.WorldVelocity = FVector(300.0, 0.0, 0.0);
		OtherStamp.RadiusCm = 150.0f;
		OtherStamp.DensityStrength = 1.0f;
		OtherStamp.VelocityStrength = 1.0f;
		OtherStamp.Source = ETDFluidBrushSource::Other;
		LiveActor.LiveComponent->QueueBrushStamp(OtherStamp);

		const TArray<FVector> ParticlePositions = { ActorLocation + FVector(-200.0, 150.0, 0.0), ActorLocation + FVector(200.0, -150.0, 0.0) };
		const TArray<FVector> ParticleVelocities = { FVector(0.0, 250.0, 0.0), FVector(0.0, -250.0, 0.0) };
		LiveActor.LiveComponent->QueueNiagaraParticleStamps(ParticlePositions, ParticleVelocities, 100.0f, ETDFluidBrushChannel::ChannelR_Mud);
	}

	void MeasureCpuField(const FTDFluidGrid2DSolver& Solver, double& OutDensitySum, double& OutKineticEnergy)
	{
		OutDensitySum = 0.0;
		OutKineticEnergy = 0.0;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
				OutDensitySum += Sample.PrimaryDensity;
				OutKineticEnergy += Sample.VelocityCmPerSec.SizeSquared();
			}
		}
	}

	double MeasureMedianApplyMilliseconds(const TArray<FTDFluidBrushStamp>& SourceStamps, const FTDFluidInputBrushSettings& Settings, const FTDFluidPlaneBasis& PlaneBasis)
	{
		TArray<double> Samples;
		Samples.Reserve(CostRunCount);
		TArray<FTDFluidBrushStamp> WorkingStamps;
		for (int32 RunIndex = 0; RunIndex < CostRunCount; ++RunIndex)
		{
			WorkingStamps = SourceStamps;
			const double StartSeconds = FPlatformTime::Seconds();
			FTDFluidInputBrushRules::ApplyToStamps(WorkingStamps, Settings, PlaneBasis, 0.0);
			Samples.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		}
		Samples.Sort();
		return Samples[Samples.Num() / 2];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidInputBrushRulesDefaultsLeaveStampsUnchangedTest,
	"TDGame.FluidFX.InputBrushRules.DefaultsLeaveStampsUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidInputBrushRulesDefaultsLeaveStampsUnchangedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputBrushRulesTestPrivate;

	const FTDFluidInputBrushSettings DefaultSettings;
	TestFalse(TEXT("Default input brush settings carry no active rule"), FTDFluidInputBrushRules::HasActiveRules(DefaultSettings));

	const TArray<FTDFluidBrushStamp> OriginalStamps = MakeSeededStamps(SeededStampCount);
	const FTDFluidPlaneBasis Bases[] = { FTDFluidPlaneBasis::MakeWorldFacing(), MakeCameraFacingBasis() };
	for (const FTDFluidPlaneBasis& Basis : Bases)
	{
		TArray<FTDFluidBrushStamp> Stamps = OriginalStamps;
		FTDFluidInputBrushRules::ApplyToStamps(Stamps, DefaultSettings, Basis, 37.0);
		if (!TestEqual(TEXT("Default rules keep every stamp"), Stamps.Num(), OriginalStamps.Num()))
		{
			return false;
		}

		int32 ChangedCount = 0;
		for (int32 Index = 0; Index < Stamps.Num(); ++Index)
		{
			ChangedCount += AreStampsBitIdentical(Stamps[Index], OriginalStamps[Index]) ? 0 : 1;
		}
		TestEqual(TEXT("Default rules leave every stamp field and order bit identical"), ChangedCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidInputBrushRulesSourceSelectionTest,
	"TDGame.FluidFX.InputBrushRules.SourceSelectionMatchesLive2Table",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidInputBrushRulesSourceSelectionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputBrushRulesTestPrivate;

	const ETDFluidVelocityOnlySources Selections[] = {
		ETDFluidVelocityOnlySources::Off, ETDFluidVelocityOnlySources::PointArrays, ETDFluidVelocityOnlySources::DataChannel,
		ETDFluidVelocityOnlySources::Fragments, ETDFluidVelocityOnlySources::PointArraysAndFragments, ETDFluidVelocityOnlySources::All
	};
	const bool ExpectedTable[6][4] = {
		{ false, false, false, false },
		{ false, true, false, false },
		{ false, false, true, false },
		{ false, false, false, true },
		{ false, true, false, true },
		{ true, true, true, true }
	};

	for (int32 SelectionIndex = 0; SelectionIndex < static_cast<int32>(UE_ARRAY_COUNT(Selections)); ++SelectionIndex)
	{
		TestEqual(TEXT("Selection order matches the LIVE-2 integer"), static_cast<int32>(Selections[SelectionIndex]), SelectionIndex);
		for (int32 SourceIndex = 0; SourceIndex < static_cast<int32>(UE_ARRAY_COUNT(AllSources)); ++SourceIndex)
		{
			const FString Label = FString::Printf(TEXT("%s selects %s"), *UEnum::GetValueAsString(Selections[SelectionIndex]), *UEnum::GetValueAsString(AllSources[SourceIndex]));
			TestTrue(*Label, FTDFluidInputBrushRules::IsVelocityOnlySource(Selections[SelectionIndex], AllSources[SourceIndex]) == ExpectedTable[SelectionIndex][SourceIndex]);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidInputBrushRulesVelocityOnlyTest,
	"TDGame.FluidFX.InputBrushRules.VelocityOnlyZeroesDensityAndHeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidInputBrushRulesVelocityOnlyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputBrushRulesTestPrivate;

	FTDFluidInputBrushSettings Settings;
	Settings.VelocityOnlySources = ETDFluidVelocityOnlySources::Fragments;
	TArray<FTDFluidBrushStamp> OriginalStamps;
	for (const ETDFluidBrushSource Source : AllSources)
	{
		OriginalStamps.Add(MakeRuleStamp(FVector(100.0, 50.0, 0.0), FVector(200.0, 0.0, 0.0), Source));
	}

	TArray<FTDFluidBrushStamp> Stamps = OriginalStamps;
	FTDFluidInputBrushRules::ApplyToStamps(Stamps, Settings, FTDFluidPlaneBasis::MakeWorldFacing(), 0.0);
	if (!TestEqual(TEXT("Velocity only keeps every stamp"), Stamps.Num(), OriginalStamps.Num()))
	{
		return false;
	}

	for (int32 Index = 0; Index < Stamps.Num(); ++Index)
	{
		const FTDFluidBrushStamp& Stamp = Stamps[Index];
		const FTDFluidBrushStamp& Original = OriginalStamps[Index];
		const FString Label = UEnum::GetValueAsString(Original.Source);
		if (Original.Source != ETDFluidBrushSource::Fragment)
		{
			TestTrue(*(Label + TEXT(" stays bit identical when its source is not selected")), AreStampsBitIdentical(Stamp, Original));
			continue;
		}

		TestEqual(*(Label + TEXT(" density becomes zero")), Stamp.DensityStrength, 0.0f);
		TestEqual(*(Label + TEXT(" heat becomes zero")), Stamp.HeatStrength, 0.0f);
		TestEqual(*(Label + TEXT(" ring inner erase becomes zero")), Stamp.RingInnerEraseRatePerSec, 0.0f);
		TestEqual(*(Label + TEXT(" keeps the velocity strength")), Stamp.VelocityStrength, Original.VelocityStrength);
		TestEqual(*(Label + TEXT(" keeps the radial velocity")), Stamp.RadialVelocityCmPerSec, Original.RadialVelocityCmPerSec);
		TestEqual(*(Label + TEXT(" keeps the ripple impulse")), Stamp.RippleImpulseCm, Original.RippleImpulseCm);
		TestTrue(*(Label + TEXT(" keeps the world velocity")), Stamp.WorldVelocity.Equals(Original.WorldVelocity, 0.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidInputBrushRulesInvertTest,
	"TDGame.FluidFX.InputBrushRules.InvertFlipsDensitySign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidInputBrushRulesInvertTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputBrushRulesTestPrivate;

	FTDFluidInputBrushSettings Settings;
	Settings.bShouldInvertInputDensity = true;
	const float OriginalDensities[] = { 0.75f, -1.0f, 0.0f };
	const float ExpectedDensities[] = { -0.75f, 1.0f, 0.0f };
	TArray<FTDFluidBrushStamp> Stamps;
	for (const float Density : OriginalDensities)
	{
		FTDFluidBrushStamp& Stamp = Stamps.Add_GetRef(MakeRuleStamp(FVector::ZeroVector, FVector(100.0, 0.0, 0.0), ETDFluidBrushSource::Other));
		Stamp.DensityStrength = Density;
	}

	FTDFluidInputBrushRules::ApplyToStamps(Stamps, Settings, FTDFluidPlaneBasis::MakeWorldFacing(), 0.0);
	if (!TestEqual(TEXT("Invert keeps every stamp"), Stamps.Num(), static_cast<int32>(UE_ARRAY_COUNT(OriginalDensities))))
	{
		return false;
	}

	for (int32 Index = 0; Index < Stamps.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Density %.2f inverts"), OriginalDensities[Index]), Stamps[Index].DensityStrength, ExpectedDensities[Index]);
		TestEqual(TEXT("Invert keeps the velocity strength"), Stamps[Index].VelocityStrength, 1.25f);
	}
	TestFalse(TEXT("A zero density stays positive zero"), FMath::IsNegativeOrNegativeZero(Stamps[2].DensityStrength));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidInputBrushRulesSlowSpeedTest,
	"TDGame.FluidFX.InputBrushRules.SlowSpeedScalesDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidInputBrushRulesSlowSpeedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputBrushRulesTestPrivate;

	constexpr float FadeBelowSpeedCmPerSec = 300.0f;
	FTDFluidInputBrushSettings Settings;
	Settings.DensityFadeBelowSpeedCmPerSec = FadeBelowSpeedCmPerSec;

	const FTDFluidPlaneBasis WorldBasis = FTDFluidPlaneBasis::MakeWorldFacing();
	const FTDFluidPlaneBasis CameraBasis = MakeCameraFacingBasis();
	struct FTDFluidSlowSpeedCase
	{
		const TCHAR* Label;
		FTDFluidPlaneBasis Basis;
		FVector Velocity;
		float ExpectedFactor;
	};
	const FTDFluidSlowSpeedCase Cases[] = {
		{ TEXT("World half speed"), WorldBasis, FVector(0.5 * FadeBelowSpeedCmPerSec, 0.0, 0.0), 0.5f },
		{ TEXT("World rest"), WorldBasis, FVector::ZeroVector, 0.0f },
		{ TEXT("World above threshold"), WorldBasis, FVector(0.0, 400.0, 0.0), 1.0f },
		{ TEXT("World vertical only"), WorldBasis, FVector(0.0, 0.0, 1000.0), 0.0f },
		{ TEXT("Camera half speed along U"), CameraBasis, CameraBasis.U * (0.5 * FadeBelowSpeedCmPerSec), 0.5f },
		{ TEXT("Camera depth only"), CameraBasis, CameraBasis.N * 1000.0, 0.0f }
	};

	for (const FTDFluidSlowSpeedCase& Case : Cases)
	{
		TArray<FTDFluidBrushStamp> Stamps = { MakeRuleStamp(FVector::ZeroVector, Case.Velocity, ETDFluidBrushSource::Other) };
		FTDFluidInputBrushRules::ApplyToStamps(Stamps, Settings, Case.Basis, 0.0);
		if (!TestEqual(*FString::Printf(TEXT("%s keeps the stamp"), Case.Label), Stamps.Num(), 1))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s scales density by %.2f"), Case.Label, Case.ExpectedFactor), FMath::IsNearlyEqual(Stamps[0].DensityStrength, 0.8f * Case.ExpectedFactor, 1.0e-6f));
		TestEqual(*FString::Printf(TEXT("%s keeps the velocity strength"), Case.Label), Stamps[0].VelocityStrength, 1.25f);
		TestEqual(*FString::Printf(TEXT("%s keeps the radial velocity"), Case.Label), Stamps[0].RadialVelocityCmPerSec, 140.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidInputBrushRulesHeightFadeTest,
	"TDGame.FluidFX.InputBrushRules.HeightFadeScalesAndDrops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidInputBrushRulesHeightFadeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputBrushRulesTestPrivate;

	constexpr float FadeDistanceCm = 200.0f;
	FTDFluidInputBrushSettings Settings;
	Settings.BrushHeightFadeDistanceCm = FadeDistanceCm;

	constexpr double WorldSurfaceCm = 1000.0;
	const double WorldHeights[] = { 1100.0, 1250.0, 900.0, 1200.0, 1000.0 };
	TArray<FTDFluidBrushStamp> OriginalStamps;
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(WorldHeights)); ++Index)
	{
		FTDFluidBrushStamp& Stamp = OriginalStamps.Add_GetRef(MakeRuleStamp(FVector(10.0 * Index, -20.0, WorldHeights[Index]), FVector(150.0, 0.0, 0.0), ETDFluidBrushSource::Other));
		Stamp.RadiusCm = 40.0f + Index;
	}

	TArray<FTDFluidBrushStamp> Stamps = OriginalStamps;
	FTDFluidInputBrushRules::ApplyToStamps(Stamps, Settings, FTDFluidPlaneBasis::MakeWorldFacing(), WorldSurfaceCm);
	if (TestEqual(TEXT("World facing drops the stamps at or beyond the fade distance"), Stamps.Num(), 3))
	{
		TestTrue(TEXT("World stamp above by half the distance halves five strengths"), IsScaledStamp(Stamps[0], OriginalStamps[0], 0.5f));
		TestTrue(TEXT("World stamp below by half the distance halves five strengths and keeps order"), IsScaledStamp(Stamps[1], OriginalStamps[2], 0.5f));
		TestTrue(TEXT("World stamp on the surface stays whole and keeps order"), IsScaledStamp(Stamps[2], OriginalStamps[4], 1.0f));
	}

	const FTDFluidPlaneBasis CameraBasis = MakeCameraFacingBasis();
	constexpr double CameraSurfaceDepthCm = 500.0;
	const FVector PlaneOffset = CameraBasis.U * 37.0 + CameraBasis.V * -12.0;
	TArray<FTDFluidBrushStamp> CameraOriginalStamps = {
		MakeRuleStamp(PlaneOffset + CameraBasis.N * (CameraSurfaceDepthCm + 300.0), FVector::ZeroVector, ETDFluidBrushSource::PointArray),
		MakeRuleStamp(PlaneOffset + CameraBasis.N * (CameraSurfaceDepthCm + 100.0), FVector::ZeroVector, ETDFluidBrushSource::Fragment)
	};
	TArray<FTDFluidBrushStamp> CameraStamps = CameraOriginalStamps;
	FTDFluidInputBrushRules::ApplyToStamps(CameraStamps, Settings, CameraBasis, CameraSurfaceDepthCm);
	if (TestEqual(TEXT("Camera facing drops the stamp beyond the fade depth"), CameraStamps.Num(), 1))
	{
		TestTrue(TEXT("Camera stamp at half the fade depth halves five strengths"), IsScaledStamp(CameraStamps[0], CameraOriginalStamps[1], 0.5f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidInputBrushRulesComponentStirTest,
	"TDGame.FluidFX.InputBrushRules.ComponentStirKeepsDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidInputBrushRulesComponentStirTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputBrushRulesTestPrivate;

	UTDFluidPresetAsset* StirPreset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::FogMistStir);
	if (!TestNotNull(TEXT("Created the stir preset"), StirPreset))
	{
		return false;
	}
	StirPreset->bOverridePresetDefaults = true;
	StirPreset->SimulationConfig = MakeStirConfig();

	FTDFluidInputBrushRulesTestWorld TestWorld;
	ATDFluidLiveActor* StirActor = SpawnStirPresetActor(*TestWorld.World, StirActorLocation, StirPreset);
	ATDFluidLiveActor* ControlActor = SpawnStirPresetActor(*TestWorld.World, ControlActorLocation, StirPreset);
	if (!TestNotNull(TEXT("Spawned the stir actor"), StirActor) || !TestNotNull(TEXT("Spawned the control actor"), ControlActor))
	{
		return false;
	}

	const UTDFluidLiveComponent& StirComponent = *StirActor->LiveComponent;
	TestTrue(TEXT("The stir kind selects every source as velocity only"), StirComponent.GetInputBrushSettings().VelocityOnlySources == ETDFluidVelocityOnlySources::All);
	for (int32 StepIndex = 0; StepIndex < StirStepCount; ++StepIndex)
	{
		QueueStirStamps(*StirActor, StirActorLocation);
		TestWorld.StepWorld();
	}

	if (!TestFalse(TEXT("The stir actor runs on the CPU"), StirComponent.IsUsingGpuSolver()) || !TestFalse(TEXT("The control actor runs on the CPU"), ControlActor->LiveComponent->IsUsingGpuSolver()))
	{
		return false;
	}

	double StirDensity = 0.0;
	double StirKineticEnergy = 0.0;
	double ControlDensity = 0.0;
	double ControlKineticEnergy = 0.0;
	MeasureCpuField(StirComponent.GetSolver(), StirDensity, StirKineticEnergy);
	MeasureCpuField(ControlActor->LiveComponent->GetSolver(), ControlDensity, ControlKineticEnergy);
	AddInfo(FString::Printf(TEXT("TDFluidStirDensity: stir=%.6f control=%.6f stir_energy=%.3f control_energy=%.3f"), StirDensity, ControlDensity, StirKineticEnergy, ControlKineticEnergy));

	TestTrue(TEXT("The control field refills ambient density"), ControlDensity > 0.0);
	TestTrue(TEXT("Stirring keeps the density sum within half a percent of the control"), FMath::Abs(StirDensity - ControlDensity) <= StirDensityTolerance * ControlDensity);
	TestTrue(TEXT("Stirring injects kinetic energy"), StirKineticEnergy > ControlKineticEnergy);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidInputBrushRulesApplyCostTest,
	"TDGame.FluidFX.InputBrushRules.ApplyCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidInputBrushRulesApplyCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputBrushRulesTestPrivate;

	FTDFluidInputBrushSettings Settings;
	Settings.VelocityOnlySources = ETDFluidVelocityOnlySources::PointArraysAndFragments;
	Settings.bShouldInvertInputDensity = true;
	Settings.DensityFadeBelowSpeedCmPerSec = 300.0f;
	Settings.BrushHeightFadeDistanceCm = 150.0f;

	const FTDFluidPlaneBasis WorldBasis = FTDFluidPlaneBasis::MakeWorldFacing();
	const double SmallMedianMs = MeasureMedianApplyMilliseconds(MakeSeededStamps(SmallCostStampCount), Settings, WorldBasis);
	const double LargeMedianMs = MeasureMedianApplyMilliseconds(MakeSeededStamps(LargeCostStampCount), Settings, WorldBasis);
	const FString Report = FString::Printf(TEXT("{\n  \"test\": \"TDGame.FluidFX.InputBrushRules.ApplyCost\",\n  \"runs\": %d,\n  \"rules\": \"all four on\",\n  \"median_ms\": {\"%d\": %.6f, \"%d\": %.6f},\n  \"proposed_budget_ms\": {\"%d\": 0.02, \"%d\": 0.3}\n}\n"),
		CostRunCount, SmallCostStampCount, SmallMedianMs, LargeCostStampCount, LargeMedianMs, SmallCostStampCount, LargeCostStampCount);
	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("fx10c-input-rules-cost.json"));
	TestTrue(TEXT("Wrote the input rules cost report"), FFileHelper::SaveStringToFile(Report, *ReportPath));
	AddInfo(FString::Printf(TEXT("TDFluidInputRulesCost: stamps_%d_median_ms=%.6f stamps_%d_median_ms=%.6f budget_ms=0.02/0.3 (record only)"), SmallCostStampCount, SmallMedianMs, LargeCostStampCount, LargeMedianMs));
	return true;
}

#endif
