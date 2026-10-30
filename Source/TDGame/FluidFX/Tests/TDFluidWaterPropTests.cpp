#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPlaneTypes.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterPropActor.h"
#include "FluidFX/TDFluidWaterPropTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidWaterPropTestPrivate
{
	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr double PlaneTolerance = 1.0e-6;
	constexpr double BasisTolerance = 1.0e-12;
	constexpr int32 SheetStepCount = 180;
	constexpr float SheetBuoyancyCmPerSec2 = -900.0f;
	constexpr int32 CouplingStepCount = 240;
	constexpr int32 NaturalCouplingStepCount = 600;
	constexpr int32 CouplingResolution = 128;
	constexpr float BandVelocityYCmPerSec = -400.0f;
	const TCHAR* const SelfReceiverWarning = TEXT("coupling receiver is the water prop itself");

	struct FTDFluidWaterPropTestWorld
	{
		FTDFluidWaterPropTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidWaterPropTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			Subsystem = World->GetSubsystem<UTDFluidLiveSubsystem>();
		}

		~FTDFluidWaterPropTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		UWorld* World = nullptr;
		UTDFluidLiveSubsystem* Subsystem = nullptr;
	};

	FTDFluidWaterPropSettings MakeLineSourceSettings()
	{
		FTDFluidWaterPropSettings Settings;
		Settings.bEnableSource = true;
		Settings.SourceShape = ETDFluidWaterPropSourceShape::Line;
		Settings.SourceWindowRatio = FVector2D(0.1, -0.2);
		Settings.SourceLengthCm = 200.0f;
		Settings.SourceStampCount = 5;
		Settings.SourceRadiusCm = 30.0f;
		Settings.SourceDensityStrength = 0.6f;
		Settings.SourceVelocityCmPerSec = FVector2D(0.0, -80.0);
		Settings.SourceRadialVelocityCmPerSec = 40.0f;
		Settings.SourceRippleImpulseCm = 2.0f;
		Settings.SourcePulseFrequencyHz = 0.0f;
		Settings.SourcePulseAmplitude = 0.7f;
		return Settings;
	}

	FTDFluidWaterPropSettings MakeSheetSourceSettings()
	{
		FTDFluidWaterPropSettings Settings;
		Settings.bEnableSource = true;
		Settings.SourceShape = ETDFluidWaterPropSourceShape::Line;
		Settings.SourceWindowRatio = FVector2D(0.0, 0.34);
		Settings.SourceLengthCm = 100.0f;
		Settings.SourceStampCount = 3;
		Settings.SourceRadiusCm = 25.0f;
		Settings.SourceDensityStrength = 0.8f;
		return Settings;
	}

	FTDFluidCellSample MakeCellSample(float Density, float VelocityYCmPerSec)
	{
		FTDFluidCellSample Sample;
		Sample.PrimaryDensity = Density;
		Sample.VelocityCmPerSec = FVector2D(0.0, VelocityYCmPerSec);
		return Sample;
	}

	struct FTDFluidWaterPropSheetResult
	{
		double WeightedVelocityYCmPerSec = 0.0;
		double FieldVelocityYCmPerSec = 0.0;
		double BelowSourceDensity = 0.0;
		double TotalDensity = 0.0;
		uint32 Checksum = 0;
	};

	FTDFluidWaterPropSheetResult RunWaterfallSheet(float BuoyancyCmPerSec2, int32 StepCount)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(500.0, 800.0);
		Config.BuoyancyCmPerSec2 = BuoyancyCmPerSec2;
		Config.BorderFadeRatio = 0.12f;
		Config.DensityDissipation = 0.3f;
		Config.VelocityDamping = 0.02f;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));

		const FTDFluidWaterPropSettings SourceSettings = MakeSheetSourceSettings();
		TArray<FTDFluidWaterPropPlaneStamp> PlaneStamps;
		TArray<FTDFluidBrushStamp> BrushStamps;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			ATDFluidWaterPropActor::BuildSourcePlaneStamps(SourceSettings, Solver.GetCenterWorld(), Config.SimExtentCm, StepIndex * StepSeconds, PlaneStamps);
			BrushStamps.Reset();
			for (const FTDFluidWaterPropPlaneStamp& PlaneStamp : PlaneStamps)
			{
				FTDFluidBrushStamp& Stamp = BrushStamps.AddDefaulted_GetRef();
				Stamp.WorldLocation = FVector(PlaneStamp.PlanePointCm.X, PlaneStamp.PlanePointCm.Y, 0.0);
				Stamp.RadiusCm = PlaneStamp.RadiusCm;
				Stamp.DensityStrength = PlaneStamp.DensityStrength;
				Stamp.VelocityStrength = 0.0f;
				Stamp.Channel = SourceSettings.SourceChannel;
			}
			Solver.ApplyBrushStamps(BrushStamps, StepSeconds);
			Solver.StepSimulation(StepSeconds);
		}

		FTDFluidWaterPropSheetResult Result;
		double WeightedVelocityY = 0.0;
		double BelowWeightedVelocityY = 0.0;
		const double BelowSourceLimitY = Solver.GetCenterWorld().Y + SourceSettings.SourceWindowRatio.Y * Config.SimExtentCm.Y - 2.0 * SourceSettings.SourceRadiusCm;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
				const double Density = FMath::Max(Sample.PrimaryDensity, Sample.SelectiveDensity);
				WeightedVelocityY += Density * Sample.VelocityCmPerSec.Y;
				Result.TotalDensity += Density;
				if (Solver.CellToWorldCenter(CellX, CellY).Y < BelowSourceLimitY)
				{
					BelowWeightedVelocityY += Density * Sample.VelocityCmPerSec.Y;
					Result.BelowSourceDensity += Density;
				}
			}
		}
		Result.FieldVelocityYCmPerSec = Result.TotalDensity > 0.0 ? WeightedVelocityY / Result.TotalDensity : 0.0;
		Result.WeightedVelocityYCmPerSec = Result.BelowSourceDensity > 0.0 ? BelowWeightedVelocityY / Result.BelowSourceDensity : 0.0;
		Result.Checksum = Solver.ComputeStateChecksum();
		return Result;
	}

	int32 ResolveStampsPerStep(const FTDFluidWaterPropSettings& Settings)
	{
		if (!Settings.bEnableSource)
		{
			return 0;
		}

		return Settings.SourceShape == ETDFluidWaterPropSourceShape::Line ? FMath::Clamp(Settings.SourceStampCount, 1, 16) : 1;
	}

	double SumPrimaryDensity(const UTDFluidLiveComponent& LiveComponent)
	{
		double DensitySum = 0.0;
		const int32 Resolution = LiveComponent.GetSimulationResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				DensitySum += LiveComponent.SampleSimulationCell(CellX, CellY).PrimaryDensity;
			}
		}
		return DensitySum;
	}

	struct FTDFluidWaterPropCouplingRun
	{
		bool bIsValid = false;
		bool bIsSourceEnabled = false;
		bool bIsCouplingEnabled = false;
		int32 ExpectedSourceStamps = 0;
		int32 TotalSourceStamps = 0;
		int32 TotalCouplingStamps = 0;
		double ReceiverDensity = 0.0;
		uint32 ReceiverChecksum = 0;
		double SourceDensity = 0.0;
		double SourceVelocityYCmPerSec = 0.0;
		double SourceHeightRatio = 0.0;
		float MaxBandMeanDensity = 0.0f;
	};

	void MeasureWaterfallField(const UTDFluidLiveComponent& WaterfallComponent, const FTDFluidWaterPropSettings& Settings, FTDFluidWaterPropCouplingRun& InOutRun)
	{
		double WeightedVelocityY = 0.0;
		double WeightedHeight = 0.0;
		const int32 Resolution = WaterfallComponent.GetSimulationResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = WaterfallComponent.SampleSimulationCell(CellX, CellY);
				InOutRun.SourceDensity += Sample.PrimaryDensity;
				WeightedVelocityY += Sample.PrimaryDensity * Sample.VelocityCmPerSec.Y;
				WeightedHeight += Sample.PrimaryDensity * (static_cast<double>(CellY) + 0.5) / static_cast<double>(Resolution);
			}
		}

		if (InOutRun.SourceDensity > 0.0)
		{
			InOutRun.SourceVelocityYCmPerSec = WeightedVelocityY / InOutRun.SourceDensity;
			InOutRun.SourceHeightRatio = WeightedHeight / InOutRun.SourceDensity;
		}

		FTDFluidWaterPropSettings ProbeSettings = Settings;
		ProbeSettings.CouplingMinDensity = 0.0f;
		TArray<FTDFluidWaterPropCouplingColumn> BandColumns;
		ATDFluidWaterPropActor::SelectCouplingColumns(Resolution, ProbeSettings, [&WaterfallComponent](int32 CellX, int32 CellY) { return WaterfallComponent.SampleSimulationCell(CellX, CellY); }, BandColumns);
		for (const FTDFluidWaterPropCouplingColumn& Column : BandColumns)
		{
			InOutRun.MaxBandMeanDensity = FMath::Max(InOutRun.MaxBandMeanDensity, Column.MeanDensity);
		}
	}

	void QueueOutflowBandStamp(UTDFluidLiveComponent& WaterfallComponent, const FTDFluidWaterPropSettings& Settings)
	{
		const int32 Resolution = WaterfallComponent.GetSimulationResolution();
		const int32 BandRowCount = FMath::Max(1, FMath::CeilToInt32(Settings.CouplingBandRatio * static_cast<float>(Resolution)));
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = WaterfallComponent.UnprojectSolverPlaneToWorld(WaterfallComponent.CellToPlaneCenter(Resolution / 2, BandRowCount / 2));
		Stamp.RadiusCm = static_cast<float>(0.25 * WaterfallComponent.GetSimulationExtentCm().X);
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.Channel = Settings.SourceChannel;
		WaterfallComponent.QueueBrushStamp(Stamp);
	}

	FTDFluidWaterPropCouplingRun RunWaterfallIntoPool(bool bShouldReceiveSelf, bool bShouldFeedOutflowBand, int32 StepCount)
	{
		FTDFluidWaterPropCouplingRun Run;
		FTDFluidWaterPropTestWorld TestWorld;
		if (!TestWorld.World)
		{
			return Run;
		}

		UTDFluidPresetAsset* WaterfallPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Waterfall);
		ATDFluidWaterPropActor* Waterfall = TestWorld.World->SpawnActor<ATDFluidWaterPropActor>(ATDFluidWaterPropActor::StaticClass(), FVector(0.0, 0.0, 800.0), FRotator::ZeroRotator);
		if (!WaterfallPreset || !Waterfall)
		{
			return Run;
		}

		Waterfall->InitializeFromPreset(WaterfallPreset, 0.0f);
		Waterfall->StepLiveActor(StepSeconds);

		ATDFluidLiveActor* Pool = nullptr;
		if (bShouldReceiveSelf)
		{
			Waterfall->CouplingReceiverActor = Waterfall;
		}
		else
		{
			const UTDFluidLiveComponent& WaterfallComponent = *Waterfall->LiveComponent;
			const FTDFluidWaterPropSettings Settings = Waterfall->GetWaterPropSettings();
			const FVector OutflowWorld = WaterfallComponent.UnprojectSolverPlaneToWorld(WaterfallComponent.CellToPlaneCenter(WaterfallComponent.GetSimulationResolution() / 2, 0))
				- WaterfallComponent.GetPlaneBasis().N * static_cast<double>(Settings.CouplingForwardOffsetCm);
			UTDFluidPresetAsset* PoolPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::WaterPool);
			Pool = TestWorld.World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), OutflowWorld - FVector(0.0, 0.0, 20.0), FRotator::ZeroRotator);
			if (!PoolPreset || !Pool)
			{
				return Run;
			}

			Pool->InitializeFromPreset(PoolPreset, 0.0f);
			Waterfall->CouplingReceiverActor = Pool;
		}

		for (int32 StepIndex = 1; StepIndex < StepCount; ++StepIndex)
		{
			if (bShouldFeedOutflowBand)
			{
				QueueOutflowBandStamp(*Waterfall->LiveComponent, Waterfall->GetWaterPropSettings());
			}
			Waterfall->StepLiveActor(StepSeconds);
			if (Pool)
			{
				Pool->StepLiveActor(StepSeconds);
			}
		}

		const FTDFluidWaterPropSettings Settings = Waterfall->GetWaterPropSettings();
		const FTDFluidWaterPropStats Stats = Waterfall->GetWaterPropStats();
		Run.bIsValid = true;
		Run.bIsSourceEnabled = Settings.bEnableSource;
		Run.bIsCouplingEnabled = Settings.bEnableOutflowCoupling;
		Run.ExpectedSourceStamps = StepCount * ResolveStampsPerStep(Settings);
		Run.TotalSourceStamps = Stats.TotalSourceStamps;
		Run.TotalCouplingStamps = Stats.TotalCouplingStamps;
		MeasureWaterfallField(*Waterfall->LiveComponent, Settings, Run);
		if (Pool && Pool->LiveComponent)
		{
			Run.ReceiverDensity = SumPrimaryDensity(*Pool->LiveComponent);
			Run.ReceiverChecksum = Pool->LiveComponent->GetSolver().ComputeStateChecksum();
		}
		return Run;
	}

	bool AreBasesEqual(const FTDFluidPlaneBasis& Left, const FTDFluidPlaneBasis& Right, double Tolerance)
	{
		return Left.U.Equals(Right.U, Tolerance) && Left.V.Equals(Right.V, Tolerance) && Left.N.Equals(Right.N, Tolerance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterPropSourceStampsLineAndPointTest,
	"TDGame.FluidFX.WaterProps.SourceStampsLineAndPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterPropSourceStampsLineAndPointTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterPropTestPrivate;

	const FVector2D WindowCenter(300.0, -150.0);
	const FVector2D ExtentCm(500.0, 800.0);
	const FTDFluidWaterPropSettings LineSettings = MakeLineSourceSettings();
	const FVector2D ExpectedCenter = WindowCenter + LineSettings.SourceWindowRatio * ExtentCm;

	TArray<FTDFluidWaterPropPlaneStamp> LineStamps;
	ATDFluidWaterPropActor::BuildSourcePlaneStamps(LineSettings, WindowCenter, ExtentCm, 1.25, LineStamps);
	if (!TestEqual(TEXT("A line source emits SourceStampCount stamps"), LineStamps.Num(), LineSettings.SourceStampCount))
	{
		return false;
	}

	const double ExpectedSpacingCm = LineSettings.SourceLengthCm / static_cast<double>(LineSettings.SourceStampCount - 1);
	FVector2D PointSum = FVector2D::ZeroVector;
	for (int32 StampIndex = 0; StampIndex < LineStamps.Num(); ++StampIndex)
	{
		const FTDFluidWaterPropPlaneStamp& Stamp = LineStamps[StampIndex];
		PointSum += Stamp.PlanePointCm;
		TestNearlyEqual(TEXT("Line stamps lie on the plane U axis through P0"), Stamp.PlanePointCm.Y, ExpectedCenter.Y, PlaneTolerance);
		TestTrue(TEXT("Without a pulse the density is exactly the source density"), Stamp.DensityStrength == LineSettings.SourceDensityStrength);
		TestTrue(TEXT("Without a pulse the ripple is exactly the source ripple"), Stamp.RippleImpulseCm == LineSettings.SourceRippleImpulseCm);
		TestEqual(TEXT("The radial velocity is copied"), Stamp.RadialVelocityCmPerSec, LineSettings.SourceRadialVelocityCmPerSec);
		TestEqual(TEXT("The radius is copied"), Stamp.RadiusCm, LineSettings.SourceRadiusCm);
		TestTrue(TEXT("The plane velocity is copied"), Stamp.PlaneVelocityCmPerSec.Equals(LineSettings.SourceVelocityCmPerSec, 0.0));
		if (StampIndex > 0)
		{
			TestNearlyEqual(TEXT("Line stamps are evenly spaced by length / (count - 1)"), Stamp.PlanePointCm.X - LineStamps[StampIndex - 1].PlanePointCm.X, ExpectedSpacingCm, PlaneTolerance);
		}
	}
	TestTrue(TEXT("The line is centred on P0"), (PointSum / static_cast<double>(LineStamps.Num())).Equals(ExpectedCenter, PlaneTolerance));
	TestNearlyEqual(TEXT("The line starts half a length before P0"), LineStamps[0].PlanePointCm.X, ExpectedCenter.X - 0.5 * LineSettings.SourceLengthCm, PlaneTolerance);

	FTDFluidWaterPropSettings PointSettings = LineSettings;
	PointSettings.SourceShape = ETDFluidWaterPropSourceShape::Point;
	TArray<FTDFluidWaterPropPlaneStamp> PointStamps;
	ATDFluidWaterPropActor::BuildSourcePlaneStamps(PointSettings, WindowCenter, ExtentCm, 1.25, PointStamps);
	if (TestEqual(TEXT("A point source emits one stamp"), PointStamps.Num(), 1))
	{
		TestTrue(TEXT("The point stamp sits at P0"), PointStamps[0].PlanePointCm.Equals(ExpectedCenter, PlaneTolerance));
	}

	FTDFluidWaterPropSettings DisabledSettings = LineSettings;
	DisabledSettings.bEnableSource = false;
	TArray<FTDFluidWaterPropPlaneStamp> DisabledStamps;
	ATDFluidWaterPropActor::BuildSourcePlaneStamps(DisabledSettings, WindowCenter, ExtentCm, 1.25, DisabledStamps);
	TestEqual(TEXT("A disabled source emits nothing"), DisabledStamps.Num(), 0);

	FTDFluidWaterPropSettings PulseSettings = LineSettings;
	PulseSettings.SourcePulseFrequencyHz = 3.0f;
	PulseSettings.SourcePulseAmplitude = 0.6f;
	PulseSettings.RandomSeed = 7;
	TArray<FTDFluidWaterPropPlaneStamp> FirstPulse;
	TArray<FTDFluidWaterPropPlaneStamp> SecondPulse;
	ATDFluidWaterPropActor::BuildSourcePlaneStamps(PulseSettings, WindowCenter, ExtentCm, 1.37, FirstPulse);
	ATDFluidWaterPropActor::BuildSourcePlaneStamps(PulseSettings, WindowCenter, ExtentCm, 1.37, SecondPulse);
	FTDFluidWaterPropSettings OtherSeedSettings = PulseSettings;
	OtherSeedSettings.RandomSeed = PulseSettings.RandomSeed + 1;
	TArray<FTDFluidWaterPropPlaneStamp> OtherSeedPulse;
	ATDFluidWaterPropActor::BuildSourcePlaneStamps(OtherSeedSettings, WindowCenter, ExtentCm, 1.37, OtherSeedPulse);
	if (!TestEqual(TEXT("Pulsed runs emit the same count"), FirstPulse.Num(), SecondPulse.Num()) || !TestEqual(TEXT("Another seed emits the same count"), FirstPulse.Num(), OtherSeedPulse.Num()))
	{
		return false;
	}

	bool bDoesOtherSeedDiffer = false;
	for (int32 StampIndex = 0; StampIndex < FirstPulse.Num(); ++StampIndex)
	{
		TestTrue(TEXT("The same seed and time give the same density"), FirstPulse[StampIndex].DensityStrength == SecondPulse[StampIndex].DensityStrength);
		TestTrue(TEXT("The same seed and time give the same ripple"), FirstPulse[StampIndex].RippleImpulseCm == SecondPulse[StampIndex].RippleImpulseCm);
		TestTrue(TEXT("A pulse never raises density above the source density"), FirstPulse[StampIndex].DensityStrength <= PulseSettings.SourceDensityStrength && FirstPulse[StampIndex].DensityStrength >= 0.0f);
		bDoesOtherSeedDiffer |= FirstPulse[StampIndex].RippleImpulseCm != OtherSeedPulse[StampIndex].RippleImpulseCm;
	}
	TestTrue(TEXT("Another seed changes the pulse"), bDoesOtherSeedDiffer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterPropCouplingColumnsFromBottomBandTest,
	"TDGame.FluidFX.WaterProps.CouplingColumnsFromBottomBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterPropCouplingColumnsFromBottomBandTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterPropTestPrivate;

	FTDFluidWaterPropSettings Settings;
	Settings.bEnableOutflowCoupling = true;
	Settings.CouplingColumnCount = 8;
	Settings.CouplingBandRatio = 0.1f;
	Settings.CouplingMinDensity = 0.05f;
	const int32 BandRowCount = FMath::CeilToInt32(Settings.CouplingBandRatio * static_cast<float>(CouplingResolution));
	const int32 ColumnWidth = CouplingResolution / Settings.CouplingColumnCount;
	const int32 DenseFirstCellX = 2 * ColumnWidth;
	const int32 DenseEndCellX = 4 * ColumnWidth;

	auto MakeBandSampler = [BandRowCount, DenseFirstCellX, DenseEndCellX](float BandDensity)
	{
		return [BandRowCount, DenseFirstCellX, DenseEndCellX, BandDensity](int32 CellX, int32 CellY)
		{
			const bool bIsInsideBand = CellY < BandRowCount;
			if (bIsInsideBand && CellX >= DenseFirstCellX && CellX < DenseEndCellX)
			{
				return MakeCellSample(BandDensity, BandVelocityYCmPerSec);
			}
			if (!bIsInsideBand && CellX < DenseFirstCellX)
			{
				return MakeCellSample(1.0f, BandVelocityYCmPerSec);
			}
			return MakeCellSample(0.0f, 0.0f);
		};
	};

	TArray<FTDFluidWaterPropCouplingColumn> Columns;
	ATDFluidWaterPropActor::SelectCouplingColumns(CouplingResolution, Settings, MakeBandSampler(1.0f), Columns);
	if (!TestEqual(TEXT("Only the two dense bottom-band columns are selected"), Columns.Num(), 2))
	{
		return false;
	}

	TestTrue(TEXT("The first selected column is column 2"), Columns[0].CenterCell.X >= DenseFirstCellX && Columns[0].CenterCell.X < DenseFirstCellX + ColumnWidth);
	TestTrue(TEXT("The second selected column is column 3"), Columns[1].CenterCell.X >= DenseFirstCellX + ColumnWidth && Columns[1].CenterCell.X < DenseEndCellX);
	TestTrue(TEXT("Column centres lie inside the bottom band"), Columns[0].CenterCell.Y >= 0 && Columns[0].CenterCell.Y < BandRowCount);
	for (const FTDFluidWaterPropCouplingColumn& Column : Columns)
	{
		TestNearlyEqual(TEXT("The column mean density is the band density"), Column.MeanDensity, 1.0f, 1.0e-6f);
		TestNearlyEqual(TEXT("The column mean down speed is the band fall speed"), Column.MeanDownSpeedCmPerSec, -BandVelocityYCmPerSec, 1.0e-3f);
	}

	ATDFluidWaterPropActor::SelectCouplingColumns(CouplingResolution, Settings, [](int32 CellX, int32 CellY)
	{
		return MakeCellSample(0.0f, 0.0f);
	}, Columns);
	TestEqual(TEXT("An empty field selects no column"), Columns.Num(), 0);

	FTDFluidWaterPropSettings ThresholdSettings = Settings;
	ThresholdSettings.CouplingMinDensity = 0.5f;
	ATDFluidWaterPropActor::SelectCouplingColumns(CouplingResolution, ThresholdSettings, MakeBandSampler(ThresholdSettings.CouplingMinDensity), Columns);
	TestEqual(TEXT("A column exactly at the threshold is selected"), Columns.Num(), 2);

	ThresholdSettings.CouplingMinDensity = 0.5f + 1.0e-3f;
	ATDFluidWaterPropActor::SelectCouplingColumns(CouplingResolution, ThresholdSettings, MakeBandSampler(0.5f), Columns);
	TestEqual(TEXT("A column just below the threshold is skipped"), Columns.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterPropWaterfallSheetFallsTest,
	"TDGame.FluidFX.WaterProps.WaterfallSheetFalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterPropWaterfallSheetFallsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterPropTestPrivate;

	const FTDFluidWaterPropSheetResult Falling = RunWaterfallSheet(SheetBuoyancyCmPerSec2, SheetStepCount);
	const FTDFluidWaterPropSheetResult Repeated = RunWaterfallSheet(SheetBuoyancyCmPerSec2, SheetStepCount);
	const FTDFluidWaterPropSheetResult Weaker = RunWaterfallSheet(0.5f * SheetBuoyancyCmPerSec2, SheetStepCount);
	const FTDFluidWaterPropSheetResult Still = RunWaterfallSheet(0.0f, SheetStepCount);
	const FTDFluidWaterPropSheetResult Settling = RunWaterfallSheet(SheetBuoyancyCmPerSec2, 2 * SheetStepCount);
	const FTDFluidWaterPropSheetResult Settled = RunWaterfallSheet(SheetBuoyancyCmPerSec2, 4 * SheetStepCount);

	AddInfo(FString::Printf(TEXT("Sheet v_y below source full %.2f half %.2f still %.2f; field v_y %.2f; below density full %.3f half %.3f still %.3f; density 180 %.3f 360 %.3f 720 %.3f"),
		Falling.WeightedVelocityYCmPerSec, Weaker.WeightedVelocityYCmPerSec, Still.WeightedVelocityYCmPerSec, Falling.FieldVelocityYCmPerSec,
		Falling.BelowSourceDensity, Weaker.BelowSourceDensity, Still.BelowSourceDensity, Falling.TotalDensity, Settling.TotalDensity, Settled.TotalDensity));
	TestTrue(TEXT("The line source writes density"), Falling.TotalDensity > 0.0);
	TestTrue(TEXT("Density leaves the source footprint downwards"), Falling.BelowSourceDensity > 0.0);
	TestTrue(TEXT("The sheet below the source moves down the plane"), Falling.WeightedVelocityYCmPerSec < 0.0 && Falling.FieldVelocityYCmPerSec < 0.0);
	TestTrue(TEXT("Half the negative buoyancy still falls"), Weaker.WeightedVelocityYCmPerSec < 0.0);
	TestTrue(TEXT("Stronger negative buoyancy falls faster"), Falling.WeightedVelocityYCmPerSec < Weaker.WeightedVelocityYCmPerSec);
	TestTrue(TEXT("Negative buoyancy carries at least twice the neutral control's density below the source"), Falling.BelowSourceDensity >= 2.0 * Still.BelowSourceDensity);
	TestTrue(TEXT("Dissipation saturates the sheet so the second 6 s add at most half of the first 6 s"), Settled.TotalDensity - Settling.TotalDensity <= 0.5 * Settling.TotalDensity);
	TestTrue(TEXT("The sheet run is deterministic"), Falling.Checksum == Repeated.Checksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterPropActorQueuesSourceAndCouplingTest,
	"TDGame.FluidFX.WaterProps.ActorQueuesSourceAndCoupling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterPropActorQueuesSourceAndCouplingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterPropTestPrivate;

	const FTDFluidWaterPropSettings WaterfallSettings = UTDFluidPresetAsset::BuildWaterPropForPresetKind(ETDFluidPresetKind::Waterfall);
	const FTDFluidWaterPropCouplingRun Natural = RunWaterfallIntoPool(false, false, NaturalCouplingStepCount);
	const FTDFluidWaterPropCouplingRun First = RunWaterfallIntoPool(false, true, CouplingStepCount);
	const FTDFluidWaterPropCouplingRun Second = RunWaterfallIntoPool(false, true, CouplingStepCount);
	AddInfo(FString::Printf(TEXT("Waterfall kind without band feed over %d steps: source %d coupling %d pool density %.3f; waterfall density %.3f v_y %.2f height %.3f band max mean %.4f threshold %.4f"),
		NaturalCouplingStepCount, Natural.TotalSourceStamps, Natural.TotalCouplingStamps, Natural.ReceiverDensity,
		Natural.SourceDensity, Natural.SourceVelocityYCmPerSec, Natural.SourceHeightRatio, Natural.MaxBandMeanDensity, WaterfallSettings.CouplingMinDensity));
	if (!TestTrue(TEXT("The waterfall and pool actors were spawned"), First.bIsValid && Second.bIsValid))
	{
		return false;
	}

	TestTrue(TEXT("The actor caches the Waterfall builder source flag"), First.bIsSourceEnabled == WaterfallSettings.bEnableSource);
	TestTrue(TEXT("The actor caches the Waterfall builder coupling flag"), First.bIsCouplingEnabled == WaterfallSettings.bEnableOutflowCoupling);
	TestTrue(TEXT("The Waterfall kind enables its source and coupling"), First.bIsSourceEnabled && First.bIsCouplingEnabled);
	TestEqual(TEXT("Every step queues the configured source stamps"), First.TotalSourceStamps, First.ExpectedSourceStamps);
	TestEqual(TEXT("Every step queues the configured source stamps without a band feed"), Natural.TotalSourceStamps, Natural.ExpectedSourceStamps);
	TestTrue(TEXT("The dense outflow band couples into the pool"), First.TotalCouplingStamps > 0);
	TestTrue(TEXT("The pool receives density"), First.ReceiverDensity > 0.0);
	TestEqual(TEXT("Coupling stamp totals repeat"), First.TotalCouplingStamps, Second.TotalCouplingStamps);
	TestTrue(TEXT("Pool state repeats"), First.ReceiverChecksum == Second.ReceiverChecksum);

	AddExpectedMessage(SelfReceiverWarning, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	const FTDFluidWaterPropCouplingRun SelfRun = RunWaterfallIntoPool(true, true, CouplingStepCount);
	if (!TestTrue(TEXT("The self-coupled waterfall was spawned"), SelfRun.bIsValid))
	{
		return false;
	}

	TestEqual(TEXT("A self receiver still runs the source"), SelfRun.TotalSourceStamps, SelfRun.ExpectedSourceStamps);
	TestEqual(TEXT("A self receiver gets no coupling stamps"), SelfRun.TotalCouplingStamps, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterPropWorldFixedVerticalUsesActorYawTest,
	"TDGame.FluidFX.Plane.WorldFixedVerticalUsesActorYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterPropWorldFixedVerticalUsesActorYawTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterPropTestPrivate;

	const FRotator Rotations[] = { FRotator(-60.0, 0.0, 0.0), FRotator(-35.0, 50.0, 0.0), FRotator(-10.0, -122.0, 0.0) };
	const float LeanDegrees[] = { 0.0f, 10.0f, 25.0f };
	for (const FRotator& Rotation : Rotations)
	{
		for (const float Lean : LeanDegrees)
		{
			const FTDFluidPlaneBasis Fixed = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::WorldFixedVertical, Rotation, Lean);
			const FTDFluidPlaneBasis Billboard = FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::VerticalBillboard, Rotation, Lean);
			TestTrue(TEXT("WorldFixedVertical builds the VerticalBillboard basis for the same rotation"), AreBasesEqual(Fixed, Billboard, BasisTolerance));
			TestFalse(TEXT("WorldFixedVertical is not world facing"), Fixed.IsWorldFacing());
		}
	}

	FTDFluidWaterPropTestWorld TestWorld;
	if (!TestNotNull(TEXT("Fluid subsystem exists"), TestWorld.Subsystem))
	{
		return false;
	}
	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(FRotator(-60.0, 90.0, 0.0));

	UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Custom);
	Preset->SimulationConfig.GridResolution = 64;
	Preset->SimulationConfig.SimExtentCm = FVector2D(500.0, 800.0);
	Preset->PlaneSettings.Orientation = ETDFluidPlaneOrientation::WorldFixedVertical;
	Preset->PlaneSettings.BillboardLeanBackDegrees = 0.0f;
	constexpr double ActorYawDegrees = 30.0;
	ATDFluidLiveActor* LiveActor = TestWorld.World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), FVector(200.0, -100.0, 300.0), FRotator(0.0, ActorYawDegrees, 0.0));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	LiveActor->InitializeFromPreset(Preset, 0.0f);
	LiveActor->StepLiveActor(StepSeconds);
	const UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
	const FTDFluidPlaneBasis FirstBasis = LiveComponent->GetPlaneBasis();
	const int32 FirstGeneration = LiveComponent->GetSimulationGeneration();
	double SinYaw = 0.0;
	double CosYaw = 1.0;
	FMath::SinCos(&SinYaw, &CosYaw, FMath::DegreesToRadians(ActorYawDegrees));
	TestTrue(TEXT("U follows the actor yaw, not the view yaw"), FirstBasis.U.Equals(FVector(-SinYaw, CosYaw, 0.0), PlaneTolerance));
	TestTrue(TEXT("The basis equals a VerticalBillboard built from the actor yaw"), AreBasesEqual(FirstBasis, FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::VerticalBillboard, FRotator(0.0, ActorYawDegrees, 0.0), Preset->PlaneSettings.BillboardLeanBackDegrees), PlaneTolerance));

	TestWorld.Subsystem->SetPlaneViewRotationOverrideForTests(FRotator(-60.0, 120.0, 0.0));
	LiveActor->StepLiveActor(StepSeconds);
	TestTrue(TEXT("Turning the view leaves the basis unchanged"), AreBasesEqual(LiveComponent->GetPlaneBasis(), FirstBasis, BasisTolerance));
	TestEqual(TEXT("Turning the view does not reinitialise the simulation"), LiveComponent->GetSimulationGeneration(), FirstGeneration);
	TestWorld.Subsystem->ClearPlaneViewRotationOverrideForTests();
	return true;
}

#endif
