#include "FluidFX/TDFluidWaterSplashRules.h"

#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "FluidFX/TDFluidTypes.h"

namespace TDFluidWaterSplashRulesPrivate
{
	constexpr int32 SplashSortKeyCount = 13;

	struct FTDFluidWaterSplashSortEntry
	{
		int64 Keys[SplashSortKeyCount];
		int32 EventIndex = 0;
	};

	int64 ToSplashMillimeters(double ValueCm)
	{
		return FMath::IsFinite(ValueCm) ? FMath::RoundToInt64(FMath::Clamp(ValueCm * 10.0, -9.0e15, 9.0e15)) : MAX_int64;
	}

	int64 ToSplashBitPattern(double Value)
	{
		int64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return Bits;
	}

	void FillSplashSortKeys(const FTDFluidWaterSplashEvent& Event, const FVector& CenterWorld, FTDFluidWaterSplashSortEntry& OutEntry)
	{
		const double DistanceSquared = FVector::DistSquared(Event.WorldLocation, CenterWorld);
		OutEntry.Keys[0] = FMath::IsFinite(DistanceSquared) ? FMath::RoundToInt64(FMath::Min(DistanceSquared, 9.0e15)) : MAX_int64;
		OutEntry.Keys[1] = ToSplashMillimeters(Event.WorldLocation.X);
		OutEntry.Keys[2] = ToSplashMillimeters(Event.WorldLocation.Y);
		OutEntry.Keys[3] = static_cast<int64>(Event.Kind);
		OutEntry.Keys[4] = ToSplashBitPattern(Event.WorldLocation.X);
		OutEntry.Keys[5] = ToSplashBitPattern(Event.WorldLocation.Y);
		OutEntry.Keys[6] = ToSplashBitPattern(Event.WorldLocation.Z);
		OutEntry.Keys[7] = ToSplashBitPattern(Event.ImpactVelocity.X);
		OutEntry.Keys[8] = ToSplashBitPattern(Event.ImpactVelocity.Y);
		OutEntry.Keys[9] = ToSplashBitPattern(Event.ImpactVelocity.Z);
		OutEntry.Keys[10] = ToSplashBitPattern(Event.ImpactSpeedCmPerSec);
		OutEntry.Keys[11] = ToSplashBitPattern(Event.RadiusCm);
		OutEntry.Keys[12] = ToSplashBitPattern(Event.SurfaceHeightCm);
	}
}

float FTDFluidWaterSplashRules::ResolveSplashRadiusCm(const FTDFluidWaterParticleSettings& Settings, float SourceRadiusCm)
{
	const float MinRadiusCm = FMath::Max(Settings.SplashMinRadiusCm, 0.0f);
	const float MaxRadiusCm = FMath::Max(Settings.SplashMaxRadiusCm, MinRadiusCm);
	if (!FMath::IsFinite(SourceRadiusCm))
	{
		return MinRadiusCm;
	}

	return FMath::Clamp(SourceRadiusCm, MinRadiusCm, MaxRadiusCm);
}

bool FTDFluidWaterSplashRules::EvaluateSplashPoint(const FTDFluidWaterParticleSettings& Settings, const FVector& Location, float RadiusCm, float SurfaceHeightCm, float DeltaSeconds, FTDFluidWaterSplashPointState& InOutState, FTDFluidWaterSplashEvent& OutEvent)
{
	if (Location.ContainsNaN() || !FMath::IsFinite(SurfaceHeightCm) || !FMath::IsFinite(RadiusCm))
	{
		InOutState = FTDFluidWaterSplashPointState();
		return false;
	}

	const float BottomHeightCm = static_cast<float>(Location.Z - RadiusCm - SurfaceHeightCm);
	if (!InOutState.bHasPrevious)
	{
		InOutState.PreviousLocation = Location;
		InOutState.PreviousBottomHeightCm = BottomHeightCm;
		InOutState.bHasPrevious = true;
		return false;
	}

	if (!(DeltaSeconds > 0.0f) || !FMath::IsFinite(DeltaSeconds))
	{
		return false;
	}

	const FVector Velocity = (Location - InOutState.PreviousLocation) / DeltaSeconds;
	const float PreviousBottomHeightCm = InOutState.PreviousBottomHeightCm;
	InOutState.PreviousLocation = Location;
	InOutState.PreviousBottomHeightCm = BottomHeightCm;
	InOutState.CooldownSeconds = FMath::Max(InOutState.CooldownSeconds - DeltaSeconds, 0.0f);
	InOutState.MoveIntervalSeconds = FMath::Min(InOutState.MoveIntervalSeconds + DeltaSeconds, FMath::Max(Settings.SplashMoveIntervalSeconds, 0.0f));

	const float BandCm = Settings.SplashSurfaceBandCm;
	const bool bIsCooledDown = InOutState.CooldownSeconds <= 0.0f;
	const float VerticalSpeed = static_cast<float>(Velocity.Z);
	const float HorizontalSpeed = static_cast<float>(Velocity.Size2D());

	if (PreviousBottomHeightCm > BandCm && BottomHeightCm <= BandCm && VerticalSpeed <= -Settings.SplashEntrySpeedThresholdCmPerSec && bIsCooledDown)
	{
		OutEvent.Kind = ETDFluidWaterSplashKind::Entry;
		OutEvent.ImpactSpeedCmPerSec = -VerticalSpeed;
		InOutState.CooldownSeconds = FMath::Max(Settings.SplashCooldownSeconds, 0.0f);
	}
	else if (Settings.SplashExitSpeedThresholdCmPerSec > 0.0f && PreviousBottomHeightCm <= BandCm && BottomHeightCm > BandCm && VerticalSpeed >= Settings.SplashExitSpeedThresholdCmPerSec && bIsCooledDown)
	{
		OutEvent.Kind = ETDFluidWaterSplashKind::Exit;
		OutEvent.ImpactSpeedCmPerSec = VerticalSpeed;
		InOutState.CooldownSeconds = FMath::Max(Settings.SplashCooldownSeconds, 0.0f);
	}
	else if (Settings.SplashMoveSpeedThresholdCmPerSec > 0.0f && FMath::Abs(Location.Z - SurfaceHeightCm) <= RadiusCm && HorizontalSpeed >= Settings.SplashMoveSpeedThresholdCmPerSec && InOutState.MoveIntervalSeconds >= Settings.SplashMoveIntervalSeconds)
	{
		OutEvent.Kind = ETDFluidWaterSplashKind::Move;
		OutEvent.ImpactSpeedCmPerSec = HorizontalSpeed;
		InOutState.MoveIntervalSeconds = 0.0f;
	}
	else
	{
		return false;
	}

	OutEvent.WorldLocation = FVector(Location.X, Location.Y, SurfaceHeightCm);
	OutEvent.ImpactVelocity = Velocity;
	OutEvent.RadiusCm = RadiusCm;
	OutEvent.SurfaceHeightCm = SurfaceHeightCm;
	return true;
}

void FTDFluidWaterSplashRules::SortAndClampSplashEvents(TArray<FTDFluidWaterSplashEvent>& InOutEvents, const FVector& CenterWorld, int32 MaxEvents)
{
	using namespace TDFluidWaterSplashRulesPrivate;

	if (MaxEvents <= 0)
	{
		InOutEvents.Reset();
		return;
	}

	TArray<FTDFluidWaterSplashSortEntry> SortEntries;
	SortEntries.Reserve(InOutEvents.Num());
	for (int32 EventIndex = 0; EventIndex < InOutEvents.Num(); ++EventIndex)
	{
		FTDFluidWaterSplashSortEntry& Entry = SortEntries.AddDefaulted_GetRef();
		FillSplashSortKeys(InOutEvents[EventIndex], CenterWorld, Entry);
		Entry.EventIndex = EventIndex;
	}

	SortEntries.StableSort([](const FTDFluidWaterSplashSortEntry& Left, const FTDFluidWaterSplashSortEntry& Right)
	{
		for (int32 KeyIndex = 0; KeyIndex < SplashSortKeyCount; ++KeyIndex)
		{
			if (Left.Keys[KeyIndex] != Right.Keys[KeyIndex])
			{
				return Left.Keys[KeyIndex] < Right.Keys[KeyIndex];
			}
		}
		return false;
	});

	const int32 KeptCount = FMath::Min(MaxEvents, SortEntries.Num());
	TArray<FTDFluidWaterSplashEvent> SortedEvents;
	SortedEvents.Reserve(KeptCount);
	for (int32 SortedIndex = 0; SortedIndex < KeptCount; ++SortedIndex)
	{
		SortedEvents.Add(InOutEvents[SortEntries[SortedIndex].EventIndex]);
	}
	InOutEvents = MoveTemp(SortedEvents);
}

int32 FTDFluidWaterSplashRules::ResolveContactStampBudget(const FTDFluidWaterParticleSettings& Settings, int32 MaxPointsPerTick, int32 PendingCount)
{
	return FMath::Max(0, FMath::Min(Settings.MaxContactStampsPerTick, MaxPointsPerTick - PendingCount));
}

void FTDFluidWaterSplashRules::BuildContactWhitewaterStamps(TArray<FTDFluidMassInputPoint>& InOutPoints, const FVector& CenterWorld, int32 MaxStamps, float FallbackRadiusCm, TArray<FTDFluidBrushStamp>& OutStamps)
{
	OutStamps.Reset();
	InOutPoints.RemoveAll([](const FTDFluidMassInputPoint& Point)
	{
		return !(Point.DensityStrength > 0.0f) || Point.WorldLocation.ContainsNaN();
	});
	UTDFluidLiveComponent::SortAndClampMassPoints(InOutPoints, CenterWorld, MaxStamps);

	OutStamps.Reserve(InOutPoints.Num());
	for (const FTDFluidMassInputPoint& Point : InOutPoints)
	{
		const float SourceRadiusCm = Point.RadiusCm > 0.0f ? Point.RadiusCm : FallbackRadiusCm;
		FTDFluidBrushStamp& Stamp = OutStamps.AddDefaulted_GetRef();
		Stamp.WorldLocation = Point.WorldLocation;
		Stamp.WorldVelocity = FVector::ZeroVector;
		Stamp.RadiusCm = FMath::Max(SourceRadiusCm, 1.0f);
		Stamp.DensityStrength = FMath::Clamp(Point.DensityStrength, 0.0f, 1.0f);
		Stamp.VelocityStrength = 0.0f;
		Stamp.RippleImpulseCm = 0.0f;
		Stamp.HeatStrength = 0.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		Stamp.Source = ETDFluidBrushSource::DataChannel;
	}
}

bool FTDFluidWaterSplashRules::TryResolveFieldCell(const FVector2D& PlanePoint, const FVector2D& FirstCellCenterPlane, double CellSizeCm, int32 Resolution, FIntPoint& OutCell)
{
	if (Resolution <= 0 || !(CellSizeCm > 0.0) || !FMath::IsFinite(CellSizeCm) || PlanePoint.ContainsNaN() || FirstCellCenterPlane.ContainsNaN())
	{
		return false;
	}

	const double CellX = FMath::FloorToDouble((PlanePoint.X - FirstCellCenterPlane.X) / CellSizeCm + 0.5);
	const double CellY = FMath::FloorToDouble((PlanePoint.Y - FirstCellCenterPlane.Y) / CellSizeCm + 0.5);
	if (!(CellX >= 0.0) || !(CellY >= 0.0) || CellX >= Resolution || CellY >= Resolution)
	{
		return false;
	}

	OutCell = FIntPoint(static_cast<int32>(CellX), static_cast<int32>(CellY));
	return true;
}
