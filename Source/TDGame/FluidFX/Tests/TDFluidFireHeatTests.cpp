#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/TDBreakableActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidFireActor.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidFragmentEmitterActor.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include <limits>

namespace TDFluidFireHeatTestPrivate
{
	constexpr float FireHeatTestStepSeconds = 1.0f / 60.0f;

	FTDFluidSimulationConfig MakeTorchConfig()
	{
		return UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Torch);
	}

	void AppendTorchStamps(const FTDFluidSourceEmitterSettings& Emitter, const FVector2D& LastAnchor, const FVector2D& Anchor, const FVector2D& AnchorVelocity, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		const int32 SubstepCount = FMath::Clamp(Emitter.SubstepCount, 1, 4);
		for (int32 SubstepIndex = 1; SubstepIndex <= SubstepCount; ++SubstepIndex)
		{
			const FVector2D EmitAnchor = SubstepIndex == SubstepCount ? Anchor : FMath::Lerp(LastAnchor, Anchor, static_cast<double>(SubstepIndex) / SubstepCount);
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(EmitAnchor.X, EmitAnchor.Y, 0.0);
			const FVector2D Velocity = FVector2D(0.0, Emitter.EmitSpeedCmPerSec) - AnchorVelocity * Emitter.MotionInertiaScale;
			Stamp.WorldVelocity = FVector(Velocity.X, Velocity.Y, 0.0);
			Stamp.RadiusCm = Emitter.RadiusCm;
			Stamp.DensityStrength = Emitter.DensityStrength;
			Stamp.VelocityStrength = 1.0f;
			Stamp.HeatStrength = Emitter.HeatStrength;
			Stamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
			OutStamps.Add(Stamp);
		}

		if (Emitter.CoreRadiusCm <= 0.0f)
		{
			return;
		}

		FTDFluidBrushStamp CoreStamp;
		CoreStamp.WorldLocation = FVector(Anchor.X, Anchor.Y, 0.0);
		CoreStamp.RadiusCm = Emitter.CoreRadiusCm;
		CoreStamp.DensityStrength = Emitter.CoreDensityStrength;
		CoreStamp.VelocityStrength = 0.0f;
		CoreStamp.HeatStrength = Emitter.CoreHeatStrength;
		CoreStamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		OutStamps.Add(CoreStamp);
	}

	struct FTDTorchMeasurement
	{
		double MaximumHeatDistanceCm = 0.0;
		double TailLengthCm = 0.0;
		double CellSizeCm = 0.0;
	};

	FTDTorchMeasurement RunTorch(double AnchorSpeedCmPerSec, int32 StepCount)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(MakeTorchConfig(), FVector2D::ZeroVector);
		Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));
		const FTDFluidSourceEmitterSettings Emitter = UTDFluidPresetAsset::BuildSourceEmitterForPresetKind(ETDFluidPresetKind::Torch);
		const FVector2D StartAnchor(-0.5 * AnchorSpeedCmPerSec * StepCount * FireHeatTestStepSeconds, -50.0);
		FVector2D LastAnchor = StartAnchor;
		FVector2D Anchor = StartAnchor;
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			LastAnchor = Anchor;
			Anchor = StartAnchor + FVector2D(AnchorSpeedCmPerSec * (StepIndex + 1) * FireHeatTestStepSeconds, 0.0);
			Stamps.Reset();
			AppendTorchStamps(Emitter, LastAnchor, Anchor, FVector2D(AnchorSpeedCmPerSec, 0.0), Stamps);
			Solver.ApplyBrushStamps(Stamps, FireHeatTestStepSeconds);
			Solver.StepSimulation(FireHeatTestStepSeconds);
		}

		FTDTorchMeasurement Measurement;
		Measurement.CellSizeCm = Solver.GetExtentCm().X / Solver.GetResolution();
		float MaximumHeat = -1.0f;
		FVector2D MaximumHeatCell = FVector2D::ZeroVector;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				const float CellHeat = Solver.SampleAtCell(CellX, CellY).Heat;
				const FVector2D CellCenter = Solver.CellToWorldCenter(CellX, CellY);
				if (CellHeat > MaximumHeat)
				{
					MaximumHeat = CellHeat;
					MaximumHeatCell = CellCenter;
				}
				if (CellHeat >= 0.8f)
				{
					Measurement.TailLengthCm = FMath::Max(Measurement.TailLengthCm, (CellCenter - Anchor).Size());
				}
			}
		}
		Measurement.MaximumHeatDistanceCm = (MaximumHeatCell - Anchor).Size();
		return Measurement;
	}

	int32 MeasureLongestHeatGapCells(int32 SubstepCount)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(MakeTorchConfig(), FVector2D::ZeroVector);
		FTDFluidSourceEmitterSettings Emitter = UTDFluidPresetAsset::BuildSourceEmitterForPresetKind(ETDFluidPresetKind::Torch);
		Emitter.SubstepCount = SubstepCount;
		Emitter.CoreRadiusCm = 0.0f;
		Emitter.EmitSpeedCmPerSec = 0.0f;
		const double AnchorSpeedCmPerSec = 1200.0;
		const FVector2D StartAnchor(-40.0, 0.0);
		FVector2D Anchor = StartAnchor;
		TArray<FTDFluidBrushStamp> Stamps;
		constexpr int32 StepCount = 4;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			const FVector2D LastAnchor = Anchor;
			Anchor = StartAnchor + FVector2D(AnchorSpeedCmPerSec * (StepIndex + 1) * FireHeatTestStepSeconds, 0.0);
			Stamps.Reset();
			AppendTorchStamps(Emitter, LastAnchor, Anchor, FVector2D::ZeroVector, Stamps);
			Solver.ApplyBrushStamps(Stamps, FireHeatTestStepSeconds);
		}

		const double CellSizeCm = Solver.GetExtentCm().X / Solver.GetResolution();
		int32 LongestGap = 0;
		int32 CurrentGap = 0;
		for (double PathX = StartAnchor.X + 20.0; PathX <= Anchor.X; PathX += CellSizeCm)
		{
			FTDFluidCellSample Sample;
			Solver.SampleAtWorldPosition(FVector2D(PathX, 0.0), Sample);
			CurrentGap = Sample.Heat >= 0.3f ? 0 : CurrentGap + 1;
			LongestGap = FMath::Max(LongestGap, CurrentGap);
		}
		return LongestGap;
	}

	void MakeFragmentPaths(TArray<FVector>& OutPrevious, TArray<FVector>& OutCurrent, TArray<FVector>& OutLagged)
	{
		for (int32 LeafIndex = 0; LeafIndex < 12; ++LeafIndex)
		{
			const FVector Start(LeafIndex * 13.0, -LeafIndex * 7.0, 20.0 + LeafIndex * 9.0);
			const FVector Step(4.0 + LeafIndex * 1.5, 2.0 - LeafIndex * 0.4, 3.0 * (LeafIndex % 3));
			OutPrevious.Add(Start);
			OutCurrent.Add(Start + Step);
			OutLagged.Add(LeafIndex == 5 ? FVector(std::numeric_limits<double>::quiet_NaN()) : Start - Step * 2.0);
		}
	}

	bool AreStampsIdentical(const FTDFluidBrushStamp& Left, const FTDFluidBrushStamp& Right)
	{
		return Left.WorldLocation == Right.WorldLocation
			&& Left.WorldVelocity == Right.WorldVelocity
			&& Left.RadiusCm == Right.RadiusCm
			&& Left.DensityStrength == Right.DensityStrength
			&& Left.VelocityStrength == Right.VelocityStrength
			&& Left.RadialVelocityCmPerSec == Right.RadialVelocityCmPerSec
			&& Left.Channel == Right.Channel
			&& Left.RippleImpulseCm == Right.RippleImpulseCm
			&& Left.HeatStrength == Right.HeatStrength;
	}

	struct FTDFireHeatTestWorld
	{
		FTDFireHeatTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidFireHeatWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFireHeatTestWorld()
		{
			if (!World)
			{
				return;
			}

			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTorchCoreHeatTest,
	"TDGame.FluidFX.Fire.TorchCoreHeatPinnedToAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTorchCoreHeatTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireHeatTestPrivate;
	const FTDTorchMeasurement Still = RunTorch(0.0, 60);
	const FTDTorchMeasurement Walking = RunTorch(600.0, 12);
	AddInfo(FString::Printf(TEXT("TDFluidTorch: still_peak_distance_cm=%.2f still_tail_cm=%.2f walking_peak_distance_cm=%.2f walking_tail_cm=%.2f cell_cm=%.3f"), Still.MaximumHeatDistanceCm, Still.TailLengthCm, Walking.MaximumHeatDistanceCm, Walking.TailLengthCm, Still.CellSizeCm));
	TestTrue(TEXT("The hottest cell of a still torch sits on the socket"), Still.MaximumHeatDistanceCm <= FMath::Max(1.5 * Still.CellSizeCm, 5.0));
	TestTrue(TEXT("The hottest cell of a walking torch stays within 12 cm of the socket"), Walking.MaximumHeatDistanceCm <= 12.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSubstepEmissionTest,
	"TDGame.FluidFX.Fire.SubstepEmissionContinuous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSubstepEmissionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireHeatTestPrivate;
	const int32 SingleGap = MeasureLongestHeatGapCells(1);
	const int32 SubstepGap = MeasureLongestHeatGapCells(3);
	AddInfo(FString::Printf(TEXT("TDFluidTorch: swing_gap_cells_single=%d swing_gap_cells_substep3=%d"), SingleGap, SubstepGap));
	TestTrue(TEXT("Three emission substeps leave at most a two cell gap along a 1200 cm/s swing"), SubstepGap <= 2);
	TestTrue(TEXT("Substeps close the gaps that single emission leaves"), SubstepGap < SingleGap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidExtraCardCountTest,
	"TDGame.FluidFX.Fire.ExtraCardCountFromPreset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidExtraCardCountTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Torch uses two extra cards"), UTDFluidPresetAsset::BuildFlameCardLayersForPresetKind(ETDFluidPresetKind::Torch).ExtraCardCount, 2);
	TestEqual(TEXT("Campfire uses one extra card"), UTDFluidPresetAsset::BuildFlameCardLayersForPresetKind(ETDFluidPresetKind::Campfire).ExtraCardCount, 1);
	TestEqual(TEXT("Older kinds use no extra card"), UTDFluidPresetAsset::BuildFlameCardLayersForPresetKind(ETDFluidPresetKind::Fire).ExtraCardCount, 0);
	ATDFluidFireActor* FireActor = GetMutableDefault<ATDFluidFireActor>();
	const FTDFluidFlameCardLayerSettings SavedLayers = FireActor->FlameCardLayers;
	FireActor->FlameCardLayers.ExtraCardCount = 7;
	TestEqual(TEXT("The extra card count is clamped to two"), FireActor->GetExtraFlameCardCount(), 2);
	FireActor->FlameCardLayers = SavedLayers;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentTargetLegacyTest,
	"TDGame.FluidFX.Fragment.TargetArrayMatchesLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentTargetLegacyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireHeatTestPrivate;
	TArray<FVector> Previous;
	TArray<FVector> Current;
	TArray<FVector> Lagged;
	MakeFragmentPaths(Previous, Current, Lagged);
	const FTDFluidFragmentTrailSettings Settings;
	constexpr float DeltaTime = 0.05f;

	TArray<FTDFluidBrushStamp> LegacyFire;
	TArray<FTDFluidBrushStamp> LegacySmoke;
	TArray<int32> LegacyLeaves;
	ATDFluidFragmentEmitterActor::BuildFragmentTrailStamps(Previous, Current, Lagged, DeltaTime, Settings, LegacyFire, LegacySmoke, &LegacyLeaves);

	TArray<FTDFluidFragmentTarget> Targets;
	ATDFluidFragmentEmitterActor::BuildLegacyTargets(nullptr, nullptr, Targets);
	TArray<FTDFluidBrushStamp> TargetFire;
	TArray<FTDFluidBrushStamp> TargetSmoke;
	TArray<int32> TargetLeaves;
	ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(Targets[0], Previous, Current, Lagged, DeltaTime, 0.0f, Settings, TargetFire, &TargetLeaves);
	ATDFluidFragmentEmitterActor::BuildTargetTrailStamps(Targets[1], Previous, Current, Lagged, DeltaTime, 0.0f, Settings, TargetSmoke);

	TestTrue(TEXT("The legacy path produced fire and smoke stamps"), LegacyFire.Num() > 0 && LegacySmoke.Num() > 0);
	TestEqual(TEXT("Fire stamp count matches"), TargetFire.Num(), LegacyFire.Num());
	TestEqual(TEXT("Smoke stamp count matches"), TargetSmoke.Num(), LegacySmoke.Num());
	TestTrue(TEXT("Stamped leaves match"), TargetLeaves == LegacyLeaves);
	for (int32 StampIndex = 0; StampIndex < FMath::Min(TargetFire.Num(), LegacyFire.Num()); ++StampIndex)
	{
		TestTrue(TEXT("Fire stamps match field by field"), AreStampsIdentical(TargetFire[StampIndex], LegacyFire[StampIndex]));
	}
	for (int32 StampIndex = 0; StampIndex < FMath::Min(TargetSmoke.Num(), LegacySmoke.Num()); ++StampIndex)
	{
		TestTrue(TEXT("Smoke stamps match field by field"), AreStampsIdentical(TargetSmoke[StampIndex], LegacySmoke[StampIndex]));
	}
	TestTrue(TEXT("Legacy fire channel"), Targets[0].Channel == ETDFluidBrushChannel::ChannelB_Fire);
	TestEqual(TEXT("Legacy smoke burst keeps the 1.5 radius scale"), Targets[1].BurstRadiusScale, 1.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFragmentHeightFalloffTest,
	"TDGame.FluidFX.Fragment.HeightFalloffForGroundTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFragmentHeightFalloffTest::RunTest(const FString& Parameters)
{
	FTDFluidFragmentTarget GroundTarget;
	GroundTarget.HeightFalloffCm = 60.0f;
	TestEqual(TEXT("At the burst height the weight is one"), ATDFluidFragmentEmitterActor::ComputeTargetHeightWeight(GroundTarget, 0.0, 0.0), 1.0f);
	TestEqual(TEXT("Below the burst height the weight is one"), ATDFluidFragmentEmitterActor::ComputeTargetHeightWeight(GroundTarget, -40.0, 0.0), 1.0f);
	TestEqual(TEXT("Halfway up the weight is one half"), ATDFluidFragmentEmitterActor::ComputeTargetHeightWeight(GroundTarget, 30.0, 0.0), 0.5f);
	TestEqual(TEXT("At the falloff height the weight is zero"), ATDFluidFragmentEmitterActor::ComputeTargetHeightWeight(GroundTarget, 60.0, 0.0), 0.0f);
	TestEqual(TEXT("Above the falloff height the weight is zero"), ATDFluidFragmentEmitterActor::ComputeTargetHeightWeight(GroundTarget, 200.0, 0.0), 0.0f);
	const FTDFluidFragmentTarget AirTarget;
	TestEqual(TEXT("Without falloff the weight is always one"), ATDFluidFragmentEmitterActor::ComputeTargetHeightWeight(AirTarget, 500.0, 0.0), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRestingFragmentHeatTest,
	"TDGame.FluidFX.Fragment.RestingFragmentsKeepHeating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRestingFragmentHeatTest::RunTest(const FString& Parameters)
{
	FTDFluidFragmentTrailSettings Settings;
	Settings.RestingHeatSeconds = 5.0f;
	Settings.RestingHeatStrength = 0.55f;
	Settings.RestingDensityStrength = 0.4f;
	FTDFluidFragmentTarget FireTarget;
	FireTarget.Channel = ETDFluidBrushChannel::ChannelB_Fire;
	FireTarget.HeatScale = 1.0f;

	const TArray<FVector> Previous = { FVector(0.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0) };
	const TArray<FVector> Current = { FVector(0.0, 0.0, 0.0), FVector(120.0, 0.0, 0.0) };
	constexpr float DeltaTime = 1.0f / 60.0f;
	for (const float ElapsedSeconds : { 0.0f, 2.5f, 4.0f })
	{
		TArray<FTDFluidBrushStamp> Stamps;
		ATDFluidFragmentEmitterActor::BuildRestingHeatStamps(FireTarget, Previous, Current, DeltaTime, ElapsedSeconds, Settings, Stamps);
		const float ExpectedHeat = 0.55f * (1.0f - ElapsedSeconds / 5.0f);
		if (!TestEqual(*FString::Printf(TEXT("Only the resting fragment heats at %.1f s"), ElapsedSeconds), Stamps.Num(), 1))
		{
			continue;
		}
		TestTrue(TEXT("Resting heat decays linearly"), FMath::IsNearlyEqual(Stamps[0].HeatStrength, ExpectedHeat, 1.0e-5f));
		TestTrue(TEXT("Resting density follows the same decay"), FMath::IsNearlyEqual(Stamps[0].DensityStrength, 0.4f * (1.0f - ElapsedSeconds / 5.0f), 1.0e-5f));
		TestEqual(TEXT("Resting stamps do not push velocity"), Stamps[0].VelocityStrength, 0.0f);
	}

	TArray<FTDFluidBrushStamp> LateStamps;
	ATDFluidFragmentEmitterActor::BuildRestingHeatStamps(FireTarget, Previous, Current, DeltaTime, 5.0f, Settings, LateStamps);
	TestEqual(TEXT("No resting heat after the resting window"), LateStamps.Num(), 0);
	FTDFluidFragmentTarget ColdTarget = FireTarget;
	ColdTarget.HeatScale = 0.0f;
	ATDFluidFragmentEmitterActor::BuildRestingHeatStamps(ColdTarget, Previous, Current, DeltaTime, 1.0f, Settings, LateStamps);
	TestEqual(TEXT("Targets without heat scale receive no resting stamps"), LateStamps.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBurstHeatTest,
	"TDGame.FluidFX.Fragment.BurstHeatSustainsWhileDensityDecays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBurstHeatTest::RunTest(const FString& Parameters)
{
	const FTDFluidPlaneBasis Basis = FTDFluidPlaneBasis::MakeWorldFacing();
	const FTDFluidFragmentTrailSettings Settings = UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion);
	TestTrue(TEXT("The explosion keeps burst heat while the burst density fades"), Settings.bSustainsBurstHeat);

	TArray<FTDFluidBrushStamp> LegacyStamps;
	ATDFluidFragmentEmitterActor::BuildBurstStamps(FVector::ZeroVector, Basis, Settings, ETDFluidBrushChannel::ChannelB_Fire, 1.0f, 0.4f, LegacyStamps);
	TArray<FTDFluidBrushStamp> HeatStamps;
	ATDFluidFragmentEmitterActor::BuildBurstStamps(FVector::ZeroVector, Basis, Settings, ETDFluidBrushChannel::ChannelB_Fire, 1.0f, 0.4f, 1.0f, HeatStamps);
	TestEqual(TEXT("Both overloads build the same stamp count"), HeatStamps.Num(), LegacyStamps.Num());
	for (int32 StampIndex = 0; StampIndex < FMath::Min(HeatStamps.Num(), LegacyStamps.Num()); ++StampIndex)
	{
		FTDFluidBrushStamp WithoutHeat = HeatStamps[StampIndex];
		TestEqual(TEXT("Heat overload carries the burst heat"), WithoutHeat.HeatStrength, 1.0f);
		TestEqual(TEXT("Legacy overload carries no heat"), LegacyStamps[StampIndex].HeatStrength, 0.0f);
		WithoutHeat.HeatStrength = 0.0f;
		TestTrue(TEXT("Other stamp fields are unchanged"), TDFluidFireHeatTestPrivate::AreStampsIdentical(WithoutHeat, LegacyStamps[StampIndex]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBreakDelegateTest,
	"TDGame.FluidFX.Fragment.BreakDelegateStartsSameFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBreakDelegateTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireHeatTestPrivate;
	FTDFireHeatTestWorld TestWorld;
	ATDBreakableActor* Breakable = TestWorld.World->SpawnActor<ATDBreakableActor>(FVector(0.0, 0.0, 50.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Spawned the breakable actor"), Breakable))
	{
		return false;
	}

	const FTransform EmitterTransform(FVector::ZeroVector);
	ATDFluidFragmentEmitterActor* Emitter = TestWorld.World->SpawnActorDeferred<ATDFluidFragmentEmitterActor>(ATDFluidFragmentEmitterActor::StaticClass(), EmitterTransform);
	if (!TestNotNull(TEXT("Spawned the fragment emitter"), Emitter))
	{
		return false;
	}
	Emitter->SourceBreakableActor = Breakable;
	Emitter->FinishSpawning(EmitterTransform);
	TestFalse(TEXT("The emitter waits before the break"), Emitter->IsTrackingFragments());
	Breakable->OnBrokenNative.Broadcast(FVector(0.0, 0.0, 50.0));
	TestTrue(TEXT("The native break delegate starts tracking in the same frame"), Emitter->IsTrackingFragments());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireFamilyPresetTest,
	"TDGame.FluidFX.Preset.FireFamilyKindsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireFamilyPresetTest::RunTest(const FString& Parameters)
{
	const ETDFluidPresetKind FireFamilyKinds[] = {
		ETDFluidPresetKind::Torch, ETDFluidPresetKind::Campfire, ETDFluidPresetKind::CampfireSmoke, ETDFluidPresetKind::Explosion,
		ETDFluidPresetKind::ExplosionSmoke, ETDFluidPresetKind::ExplosionGroundDust, ETDFluidPresetKind::Candle, ETDFluidPresetKind::Fireball,
		ETDFluidPresetKind::PlasmaExplosion, ETDFluidPresetKind::SteamPuff, ETDFluidPresetKind::Vortex, ETDFluidPresetKind::InvertedVortex,
		ETDFluidPresetKind::PortalVortex
	};
	for (const ETDFluidPresetKind Kind : FireFamilyKinds)
	{
		const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		const FString Label = UEnum::GetValueAsString(Kind);
		TestTrue(*(Label + TEXT(" is a fire family kind")), UTDFluidPresetAsset::IsFireFamilyPresetKind(Kind));
		TestTrue(*(Label + TEXT(" runs the fluid solver")), Config.SimMode == ETDFluidSimMode::FluidSim);
		TestTrue(*(Label + TEXT(" heat cooling is inside its range")), Config.HeatCoolingPerSec >= 0.0f && Config.HeatCoolingPerSec <= 50.0f);
		TestTrue(*(Label + TEXT(" velocity damping is set")), Config.VelocityDamping > 0.0f);
		TestTrue(*(Label + TEXT(" pressure iterations are set")), Config.PressureIterations >= 6);
		if (Config.bEnableHeatChannel)
		{
			TestEqual(*(Label + TEXT(" lets heat alone drive buoyancy")), Config.BuoyancyCmPerSec2, 0.0f);
			TestTrue(*(Label + TEXT(" has heat buoyancy")), Config.HeatBuoyancyCmPerSec2 > 0.0f);
		}
		if (Config.SolverBackend == ETDFluidSolverBackend::Gpu)
		{
			TestTrue(*(Label + TEXT(" names a CPU fallback resolution")), Config.CpuFallbackResolution > 0 && Config.CpuFallbackResolution <= 256);
		}
		else
		{
			TestTrue(*(Label + TEXT(" CPU resolution is at most 256")), Config.GridResolution <= 256);
		}
	}

	TestEqual(TEXT("Torch cools fast enough to pin the hot core"), UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Torch).HeatCoolingPerSec, 11.0f);
	TestEqual(TEXT("Explosion falls back to a 128 CPU grid"), UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Explosion).CpuFallbackResolution, 128);
	TestEqual(TEXT("Explosion smoke falls back to a 112 CPU grid"), UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::ExplosionSmoke).CpuFallbackResolution, 112);
	TestEqual(TEXT("Portal drift is inward"), UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::PortalVortex).VortexRadialDriftCmPerSec, 250.0f);
	TestEqual(TEXT("Inverted vortex spins backwards"), UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::InvertedVortex).VortexTangentialAccelerationCmPerSec2, -600.0f);
	TestEqual(TEXT("Torch emits with three substeps"), UTDFluidPresetAsset::BuildSourceEmitterForPresetKind(ETDFluidPresetKind::Torch).SubstepCount, 3);
	TestEqual(TEXT("Torch has a socket core"), UTDFluidPresetAsset::BuildSourceEmitterForPresetKind(ETDFluidPresetKind::Torch).CoreRadiusCm, 9.0f);
	TestTrue(TEXT("Campfire smoke uses transmittance"), UTDFluidPresetAsset::BuildVolumeRenderForPresetKind(ETDFluidPresetKind::CampfireSmoke).bEnableTransmittance);
	TestEqual(TEXT("Explosion resting fragments add density"), UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion).RestingDensityStrength, 0.4f);
	TestEqual(TEXT("Explosion burst has no ring stamps"), UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion).BurstRingStampCount, 0);
	TestEqual(TEXT("Explosion burst core radius is 200 cm"), UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind::Explosion).BurstCoreRadiusCm, 200.0f);
	TestEqual(TEXT("Explosion smoke heat glows deep red"), UTDFluidPresetAsset::BuildVolumeRenderForPresetKind(ETDFluidPresetKind::ExplosionSmoke).HeatEmissionColor, FLinearColor(1.0f, 0.06f, 0.01f, 1.0f));
	TestEqual(TEXT("Flame cards keep one scale per layer"), FTDFluidFlameCardLayerSettings().CardScaleStep, 1.0f);

	const ETDFluidPresetKind OlderKinds[] = {
		ETDFluidPresetKind::Custom, ETDFluidPresetKind::Water, ETDFluidPresetKind::Mud, ETDFluidPresetKind::Blood, ETDFluidPresetKind::Fire,
		ETDFluidPresetKind::FootprintPainter, ETDFluidPresetKind::Smoke, ETDFluidPresetKind::River, ETDFluidPresetKind::Sea
	};
	const FTDFluidSimulationConfig DefaultConfig;
	const FTDFluidVolumeRenderSettings DefaultVolumeRender;
	for (const ETDFluidPresetKind Kind : OlderKinds)
	{
		const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		const FString Label = UEnum::GetValueAsString(Kind);
		TestFalse(*(Label + TEXT(" is not a fire family kind")), UTDFluidPresetAsset::IsFireFamilyPresetKind(Kind));
		TestFalse(*(Label + TEXT(" keeps heat off")), Config.bEnableHeatChannel);
		TestFalse(*(Label + TEXT(" keeps boundary reflection off")), Config.bReflectsAtBoundaries);
		TestEqual(*(Label + TEXT(" keeps the border fade exponent")), Config.BorderFadeExponent, DefaultConfig.BorderFadeExponent);
		TestEqual(*(Label + TEXT(" keeps the vortex force off")), Config.VortexTangentialAccelerationCmPerSec2, 0.0f);
		TestEqual(*(Label + TEXT(" keeps the vortex drift off")), Config.VortexRadialDriftCmPerSec, 0.0f);
		TestEqual(*(Label + TEXT(" keeps the CPU fallback resolution unset")), Config.CpuFallbackResolution, 0);
		TestEqual(*(Label + TEXT(" keeps transmittance at the default")), UTDFluidPresetAsset::BuildVolumeRenderForPresetKind(Kind).bEnableTransmittance, DefaultVolumeRender.bEnableTransmittance);
	}
	return true;
}

#endif
