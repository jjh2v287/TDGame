#include "FluidFX/TDFluidWaterPropActor.h"

#include "FluidFX/TDFluidBandLimitedNoise.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidWaterProp, Log, All);

namespace
{
	constexpr int32 TDFluidWaterPropMaxSourceStampCount = 16;
	constexpr int32 TDFluidWaterPropMaxCouplingColumnCount = 32;
	constexpr float TDFluidWaterPropMinStampRadiusCm = 1.0f;

	float ResolveTDFluidWaterPropPulse(const FTDFluidWaterPropSettings& Settings, int32 StampIndex, double TimeSeconds)
	{
		if (Settings.SourcePulseFrequencyHz <= 0.0f)
		{
			return 1.0f;
		}

		const float Noise = TDFluidDeterministic::EvaluateBandLimitedNoise(Settings.RandomSeed, StampIndex, TimeSeconds, Settings.SourcePulseFrequencyHz);
		return FMath::Max(0.0f, 1.0f + Settings.SourcePulseAmplitude * Noise);
	}
}

void ATDFluidWaterPropActor::BeginPlay()
{
	Super::BeginPlay();
	RefreshWaterPropSettings(nullptr);
}

void ATDFluidWaterPropActor::ReapplyPresetTuning(FTDFluidPresetReapplyReport& InOutReport)
{
	Super::ReapplyPresetTuning(InOutReport);
	RefreshWaterPropSettings(&InOutReport);
}

void ATDFluidWaterPropActor::RefreshWaterPropSettings(FTDFluidPresetReapplyReport* InOutReport)
{
	const UTDFluidPresetAsset* Preset = LiveComponent ? LiveComponent->ActivePreset.Get() : nullptr;
	const FTDFluidWaterPropSettings BuiltSettings = UTDFluidPresetAsset::BuildWaterPropForPresetKind(Preset ? Preset->PresetKind : ETDFluidPresetKind::Custom);
	SettingsSourcePresetKey = FObjectKey(Preset);
	bHasRefreshedWaterPropSettings = true;
	if (!InOutReport)
	{
		WaterPropSettings = BuiltSettings;
		return;
	}

	InOutReport->AssignTrackedStruct(WaterPropSettings, BuiltSettings);
}

bool ATDFluidWaterPropActor::HasPresetChangedSinceRefresh() const
{
	const UTDFluidPresetAsset* Preset = LiveComponent ? LiveComponent->ActivePreset.Get() : nullptr;
	return !bHasRefreshedWaterPropSettings || SettingsSourcePresetKey != FObjectKey(Preset);
}

void ATDFluidWaterPropActor::PrepareActorSpecificInputs(float DeltaTime)
{
	if (HasPresetChangedSinceRefresh())
	{
		RefreshWaterPropSettings(nullptr);
	}

	PropSimulationTimeSeconds += DeltaTime;
	QueueSourceStamps();
}

void ATDFluidWaterPropActor::FinishActorSpecificOutputs(float DeltaTime)
{
	QueueCouplingStamps();
}

void ATDFluidWaterPropActor::BuildSourcePlaneStamps(const FTDFluidWaterPropSettings& Settings, const FVector2D& WindowCenterPlane, const FVector2D& ExtentCm, double TimeSeconds, TArray<FTDFluidWaterPropPlaneStamp>& OutStamps)
{
	OutStamps.Reset();
	if (!Settings.bEnableSource)
	{
		return;
	}

	const bool bIsLine = Settings.SourceShape == ETDFluidWaterPropSourceShape::Line;
	const int32 StampCount = bIsLine ? FMath::Clamp(Settings.SourceStampCount, 1, TDFluidWaterPropMaxSourceStampCount) : 1;
	const double HalfLengthCm = bIsLine ? 0.5 * static_cast<double>(FMath::Max(Settings.SourceLengthCm, 0.0f)) : 0.0;
	const FVector2D SourceCenterPlane = WindowCenterPlane + Settings.SourceWindowRatio * ExtentCm;
	OutStamps.Reserve(StampCount);
	for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
	{
		const double LineRatio = StampCount == 1 ? 0.0 : 2.0 * static_cast<double>(StampIndex) / static_cast<double>(StampCount - 1) - 1.0;
		const float Pulse = ResolveTDFluidWaterPropPulse(Settings, StampIndex, TimeSeconds);

		FTDFluidWaterPropPlaneStamp& Stamp = OutStamps.AddDefaulted_GetRef();
		Stamp.PlanePointCm = SourceCenterPlane + FVector2D(LineRatio * HalfLengthCm, 0.0);
		Stamp.PlaneVelocityCmPerSec = Settings.SourceVelocityCmPerSec;
		Stamp.RadiusCm = FMath::Max(Settings.SourceRadiusCm, TDFluidWaterPropMinStampRadiusCm);
		Stamp.DensityStrength = Settings.SourceDensityStrength * FMath::Min(1.0f, Pulse);
		Stamp.RadialVelocityCmPerSec = Settings.SourceRadialVelocityCmPerSec;
		Stamp.RippleImpulseCm = Settings.SourceRippleImpulseCm * Pulse;
	}
}

void ATDFluidWaterPropActor::SelectCouplingColumns(int32 Resolution, const FTDFluidWaterPropSettings& Settings, TFunctionRef<FTDFluidCellSample(int32, int32)> SampleCell, TArray<FTDFluidWaterPropCouplingColumn>& OutColumns)
{
	OutColumns.Reset();
	if (Resolution <= 0)
	{
		return;
	}

	const int32 BandRowCount = FMath::Clamp(FMath::CeilToInt32(Settings.CouplingBandRatio * static_cast<float>(Resolution)), 1, Resolution);
	const int32 ColumnCount = FMath::Clamp(Settings.CouplingColumnCount, 1, FMath::Min(Resolution, TDFluidWaterPropMaxCouplingColumnCount));
	for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
	{
		const int32 FirstCellX = ColumnIndex * Resolution / ColumnCount;
		const int32 EndCellX = (ColumnIndex + 1) * Resolution / ColumnCount;
		double DensitySum = 0.0;
		double DownSpeedSum = 0.0;
		for (int32 CellY = 0; CellY < BandRowCount; ++CellY)
		{
			for (int32 CellX = FirstCellX; CellX < EndCellX; ++CellX)
			{
				const FTDFluidCellSample Sample = SampleCell(CellX, CellY);
				DensitySum += Sample.PrimaryDensity;
				DownSpeedSum += FMath::Max(0.0f, -Sample.VelocityCmPerSec.Y);
			}
		}

		const double CellCount = static_cast<double>((EndCellX - FirstCellX) * BandRowCount);
		const float MeanDensity = static_cast<float>(DensitySum / CellCount);
		if (MeanDensity < Settings.CouplingMinDensity)
		{
			continue;
		}

		FTDFluidWaterPropCouplingColumn& Column = OutColumns.AddDefaulted_GetRef();
		Column.CenterCell = FIntPoint((FirstCellX + EndCellX) / 2, BandRowCount / 2);
		Column.MeanDensity = MeanDensity;
		Column.MeanDownSpeedCmPerSec = static_cast<float>(DownSpeedSum / CellCount);
	}
}

void ATDFluidWaterPropActor::QueueSourceStamps()
{
	WaterPropStats.SourceStampCount = 0;
	if (!WaterPropSettings.bEnableSource || !LiveComponent || !LiveComponent->IsSimulationInitialized())
	{
		return;
	}

	TArray<FTDFluidWaterPropPlaneStamp> PlaneStamps;
	BuildSourcePlaneStamps(WaterPropSettings, LiveComponent->GetSimulationCenterWorld(), LiveComponent->GetSimulationExtentCm(), PropSimulationTimeSeconds, PlaneStamps);

	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent->GetPlaneBasis();
	for (const FTDFluidWaterPropPlaneStamp& PlaneStamp : PlaneStamps)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = LiveComponent->UnprojectSolverPlaneToWorld(PlaneStamp.PlanePointCm);
		Stamp.WorldVelocity = PlaneBasis.UnprojectVector(PlaneStamp.PlaneVelocityCmPerSec);
		Stamp.RadiusCm = PlaneStamp.RadiusCm;
		Stamp.DensityStrength = PlaneStamp.DensityStrength;
		Stamp.VelocityStrength = 1.0f;
		Stamp.RadialVelocityCmPerSec = PlaneStamp.RadialVelocityCmPerSec;
		Stamp.RippleImpulseCm = PlaneStamp.RippleImpulseCm;
		Stamp.Channel = WaterPropSettings.SourceChannel;
		LiveComponent->QueueBrushStamp(Stamp);
	}

	WaterPropStats.SourceStampCount = PlaneStamps.Num();
	WaterPropStats.TotalSourceStamps += PlaneStamps.Num();
}

bool ATDFluidWaterPropActor::CanQueueCouplingInto(const ATDFluidLiveActor& ReceiverActor)
{
	if (&ReceiverActor == this)
	{
		if (!bHasWarnedSelfCouplingReceiver)
		{
			bHasWarnedSelfCouplingReceiver = true;
			UE_LOG(LogTDFluidWaterProp, Warning, TEXT("%s: coupling receiver is the water prop itself; outflow coupling is skipped."), *GetName());
		}
		return false;
	}

	return ReceiverActor.LiveComponent && !ReceiverActor.IsPooledInactive() && !ReceiverActor.HasFiniteLifetimeExpired();
}

void ATDFluidWaterPropActor::QueueCouplingStamps()
{
	WaterPropStats.CouplingStampCount = 0;
	if (!WaterPropSettings.bEnableOutflowCoupling || !CouplingReceiverActor || !LiveComponent || !LiveComponent->IsSimulationInitialized())
	{
		return;
	}

	if (!CanQueueCouplingInto(*CouplingReceiverActor))
	{
		return;
	}

	TArray<FTDFluidWaterPropCouplingColumn> Columns;
	const UTDFluidLiveComponent& SourceComponent = *LiveComponent;
	SelectCouplingColumns(SourceComponent.GetSimulationResolution(), WaterPropSettings, [&SourceComponent](int32 CellX, int32 CellY)
	{
		return SourceComponent.SampleSimulationCell(CellX, CellY);
	}, Columns);

	UTDFluidLiveComponent* ReceiverComponent = CouplingReceiverActor->LiveComponent;
	const FVector ForwardOffsetWorld = SourceComponent.GetPlaneBasis().N * static_cast<double>(WaterPropSettings.CouplingForwardOffsetCm);
	double MeanDensitySum = 0.0;
	double MeanDownSpeedSum = 0.0;
	for (const FTDFluidWaterPropCouplingColumn& Column : Columns)
	{
		const float Weight = FMath::Clamp(Column.MeanDensity, 0.0f, 1.0f);
		const FVector OutflowWorld = SourceComponent.UnprojectSolverPlaneToWorld(SourceComponent.CellToPlaneCenter(Column.CenterCell.X, Column.CenterCell.Y)) - ForwardOffsetWorld;

		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = ReceiverComponent->UnprojectSolverPlaneToWorld(ReceiverComponent->ProjectWorldToSolverPlane(OutflowWorld), 0.0);
		Stamp.WorldVelocity = FVector::ZeroVector;
		Stamp.RadiusCm = FMath::Max(WaterPropSettings.CouplingRadiusCm, TDFluidWaterPropMinStampRadiusCm);
		Stamp.DensityStrength = WaterPropSettings.CouplingDensityScale * Weight;
		Stamp.VelocityStrength = 0.0f;
		Stamp.RadialVelocityCmPerSec = WaterPropSettings.CouplingRadialVelocityCmPerSec * Weight;
		Stamp.RippleImpulseCm = WaterPropSettings.CouplingRippleImpulseCm * Weight;
		Stamp.Channel = WaterPropSettings.CouplingChannel;
		ReceiverComponent->QueueBrushStamp(Stamp);

		MeanDensitySum += Column.MeanDensity;
		MeanDownSpeedSum += Column.MeanDownSpeedCmPerSec;
	}

	const int32 ColumnCount = Columns.Num();
	WaterPropStats.CouplingStampCount = ColumnCount;
	WaterPropStats.TotalCouplingStamps += ColumnCount;
	WaterPropStats.LastCouplingMeanDensity = ColumnCount > 0 ? static_cast<float>(MeanDensitySum / ColumnCount) : 0.0f;
	WaterPropStats.LastCouplingMeanDownSpeedCmPerSec = ColumnCount > 0 ? static_cast<float>(MeanDownSpeedSum / ColumnCount) : 0.0f;
}
