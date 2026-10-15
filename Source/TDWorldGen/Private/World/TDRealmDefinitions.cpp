#include "World/TDRealmDefinitions.h"

#include "TDWorldGenSettings.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	constexpr double TDRealmLiftExitDistanceCm = 400.0;

	const UTDRealmDefinition* LoadRealmDefinitionQuietly(const TSoftObjectPtr<UTDRealmDefinition>& RealmReference)
	{
		if (RealmReference.IsNull())
		{
			return nullptr;
		}

		if (const UTDRealmDefinition* LoadedRealm = RealmReference.Get())
		{
			return LoadedRealm;
		}

		return Cast<UTDRealmDefinition>(RealmReference.ToSoftObjectPath().TryLoad(nullptr, static_cast<ELoadFlags>(LOAD_NoWarn | LOAD_Quiet)));
	}

	template <typename TPredicate>
	const UTDRealmDefinition* FindConfiguredRealm(TPredicate&& Predicate)
	{
		const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
		if (!Settings)
		{
			return nullptr;
		}

		for (const TSoftObjectPtr<UTDRealmDefinition>& RealmReference : Settings->RealmDefinitions)
		{
			const UTDRealmDefinition* Realm = LoadRealmDefinitionQuietly(RealmReference);
			if (Realm && Predicate(*Realm))
			{
				return Realm;
			}
		}
		return nullptr;
	}

#if WITH_EDITOR
	constexpr double TDRealmLiftLandingAlignmentToleranceCm = 1.0;
	constexpr double TDRealmLiftVisibleAboveDeckCm = 810.0;
	constexpr double TDRealmLiftMaxVisibleRingSlipCm = 2.0;
	constexpr double TDRealmLiftLinerToleranceCm = 0.01;

	void CollectRealmLiftLinerErrors(const FTDRealmLift& Lift, const FString& GateName, double TravelLengthCm, TArray<FString>& OutErrors)
	{
		const double CruiseSpeedCmPerSecond = Lift.CruiseSpeedCmPerSecond;
		const double BrakeDistanceCm = CruiseSpeedCmPerSecond * CruiseSpeedCmPerSecond / (2.0 * Lift.BrakeDecelerationCmPerSecondSquared);
		const double ResumeDistanceCm = CruiseSpeedCmPerSecond * CruiseSpeedCmPerSecond / (2.0 * Lift.AccelerationCmPerSecondSquared);
		const double RingTopDepthCm = Lift.RingTopDepthCm;
		const double AlwaysCoveredBottomDepthCm = RingTopDepthCm + static_cast<double>(Lift.RingModuleCount - 1) * Lift.RingModuleHeightCm;
		const double DescentBrakeDepthCm = Lift.HoldDepthCm - BrakeDistanceCm;
		const double DescentResumeDepthCm = Lift.HoldDepthCm + ResumeDistanceCm;

		auto RequireVisibleTopOnLiner = [&](const TCHAR* PhaseName, double DeckDepthCm, double RampDistanceCm)
		{
			const double VisibleTopDepthCm = DeckDepthCm - TDRealmLiftVisibleAboveDeckCm;
			const double ExposedDepthCm = RingTopDepthCm - VisibleTopDepthCm;
			if (ExposedDepthCm <= 0.0)
			{
				return;
			}
			if (ExposedDepthCm > RampDistanceCm)
			{
				OutErrors.Add(FString::Printf(TEXT("승강 관문 '%s'의 %s 덱 깊이 %.1fcm에서 보이는 벽 위 끝 %.1fcm가 라이너 위 끝 %.1fcm보다 위라 덱이 멈춘 동안 링 감김이 보입니다"), *GateName, PhaseName, DeckDepthCm, VisibleTopDepthCm, RingTopDepthCm));
				return;
			}

			const double VisibleRingSlipCm = ExposedDepthCm * ExposedDepthCm / (4.0 * RampDistanceCm);
			if (VisibleRingSlipCm > TDRealmLiftMaxVisibleRingSlipCm + TDRealmLiftLinerToleranceCm)
			{
				OutErrors.Add(FString::Printf(TEXT("승강 관문 '%s'의 %s 덱 깊이 %.1fcm에서 보이는 벽 위 끝 %.1fcm가 라이너 위 끝 %.1fcm보다 위라 보이는 링 미끄러짐 %.2fcm가 허용 %.1fcm를 넘습니다"), *GateName, PhaseName, DeckDepthCm, VisibleTopDepthCm, RingTopDepthCm, VisibleRingSlipCm, TDRealmLiftMaxVisibleRingSlipCm));
			}
		};
		auto RequireVisibleBottomOnLiner = [&](const TCHAR* PhaseName, double DeckDepthCm)
		{
			const double VisibleBottomDepthCm = DeckDepthCm + Lift.AbyssScreenDepthCm;
			if (VisibleBottomDepthCm - TDRealmLiftLinerToleranceCm > AlwaysCoveredBottomDepthCm)
			{
				OutErrors.Add(FString::Printf(TEXT("승강 관문 '%s'의 %s 덱 깊이 %.1fcm에서 보이는 벽 아래 끝 %.1fcm가 링이 늘 덮는 아래 끝 %.1fcm보다 아래입니다"), *GateName, PhaseName, DeckDepthCm, VisibleBottomDepthCm, AlwaysCoveredBottomDepthCm));
			}
		};

		RequireVisibleTopOnLiner(TEXT("하행 제동 시작"), DescentBrakeDepthCm, BrakeDistanceCm);
		RequireVisibleBottomOnLiner(TEXT("하행 재개 끝"), DescentResumeDepthCm);
		RequireVisibleBottomOnLiner(TEXT("상행 제동 시작"), TravelLengthCm - DescentBrakeDepthCm);
		RequireVisibleTopOnLiner(TEXT("상행 재개 끝"), TravelLengthCm - DescentResumeDepthCm, ResumeDistanceCm);
	}

	void CollectRealmLiftGateErrors(const FTDRealmGate& Gate, TArray<FString>& OutErrors)
	{
		const FTDRealmLift& Lift = Gate.Lift;
		const FString GateName = Gate.GateId.ToString();
		const FVector FieldLandingLocation = Lift.FieldLandingTransform.GetLocation();
		const FVector RealmLandingLocation = Lift.RealmLandingTransform.GetLocation();
		const double TravelLengthCm = FieldLandingLocation.Z - RealmLandingLocation.Z;
		const double LandingOffsetXYCm = FVector::Dist2D(FieldLandingLocation, RealmLandingLocation);
		const bool bHasPositiveTravel = TravelLengthCm > 0.0;
		const bool bHasPositiveSpeeds = Lift.CruiseSpeedCmPerSecond > 0.f && Lift.AccelerationCmPerSecondSquared > 0.f && Lift.BrakeDecelerationCmPerSecondSquared > 0.f && Lift.CallSpeedCmPerSecond > 0.f;

		if (!bHasPositiveTravel)
		{
			OutErrors.Add(FString::Printf(TEXT("승강 관문 '%s'의 필드 착지 Z가 이계 착지 Z보다 높아야 합니다(차이 %.2fcm)"), *GateName, TravelLengthCm));
		}
		if (LandingOffsetXYCm > TDRealmLiftLandingAlignmentToleranceCm)
		{
			OutErrors.Add(FString::Printf(TEXT("승강 관문 '%s'의 두 착지 XY 차이 %.2fcm가 %.0fcm를 넘습니다"), *GateName, LandingOffsetXYCm, TDRealmLiftLandingAlignmentToleranceCm));
		}
		if (!bHasPositiveSpeeds)
		{
			OutErrors.Add(FString::Printf(TEXT("승강 관문 '%s'의 속도·가속 값은 모두 0보다 커야 합니다"), *GateName));
		}
		if (!bHasPositiveTravel || !bHasPositiveSpeeds)
		{
			return;
		}

		CollectRealmLiftLinerErrors(Lift, GateName, TravelLengthCm, OutErrors);
	}
#endif
}

FVector FTDRealmGate::GetPassageDirection() const
{
	const FVector Forward = ArrivalTransform.GetRotation().GetForwardVector();
	return FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
}

FVector FTDRealmGate::GetBoundaryCrossingCm() const
{
	const FVector FieldReturnLocation = FieldReturnTransform.GetLocation();
	const FVector CrossingLocation = FieldReturnLocation + GetPassageDirection() * ApproachLengthCm;
	const double ConnectorLengthCm = static_cast<double>(ApproachLengthCm) + DepartureLengthCm;
	if (ConnectorLengthCm <= 0.0)
	{
		return CrossingLocation;
	}

	const double CrossingAlpha = ApproachLengthCm / ConnectorLengthCm;
	return FVector(CrossingLocation.X, CrossingLocation.Y, FMath::Lerp(FieldReturnLocation.Z, ArrivalTransform.GetLocation().Z, CrossingAlpha));
}

FVector FTDRealmGate::GetPreloadLocation(bool bToEntry) const
{
	return bToEntry ? ArrivalTransform.GetLocation() : FieldReturnTransform.GetLocation();
}

FTransform FTDRealmGate::GetTravelDestination(bool bToEntry) const
{
	if (GateKind != ETDRealmGateKind::Lift)
	{
		return bToEntry ? ArrivalTransform : FieldReturnTransform;
	}

	const FTransform& LandingTransform = bToEntry ? Lift.RealmLandingTransform : Lift.FieldLandingTransform;
	const FVector ExitDirection = LandingTransform.GetRotation().GetForwardVector().GetSafeNormal2D();
	FTransform ExitTransform = LandingTransform;
	ExitTransform.SetLocation(LandingTransform.GetLocation() + ExitDirection * TDRealmLiftExitDistanceCm);
	return ExitTransform;
}

FBox UTDRealmDefinition::GetBoundsCm() const
{
	return FBox(CenterCm - HalfExtentCm, CenterCm + HalfExtentCm);
}

bool UTDRealmDefinition::ContainsLocationXY(const FVector& LocationCm) const
{
	if (HalfExtentCm.X <= 0.0 || HalfExtentCm.Y <= 0.0)
	{
		return false;
	}

	return GetBoundsCm().IsInsideOrOnXY(LocationCm);
}

const FTDRealmGate* UTDRealmDefinition::FindGate(FName GateId) const
{
	if (GateId.IsNone())
	{
		return nullptr;
	}

	return Gates.FindByPredicate([GateId](const FTDRealmGate& Gate) { return Gate.GateId == GateId; });
}

const FTDRealmGate* UTDRealmDefinition::FindNearestGate(const FVector& LocationCm) const
{
	const FTDRealmGate* NearestGate = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();
	for (const FTDRealmGate& Gate : Gates)
	{
		const double DistanceSquared = FVector::DistSquared(Gate.ArrivalTransform.GetLocation(), LocationCm);
		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			NearestGate = &Gate;
		}
	}
	return NearestGate;
}

const UTDRealmDefinition* UTDRealmDefinition::FindRealmByGateId(FName GateId)
{
	if (GateId.IsNone())
	{
		return nullptr;
	}

	return FindConfiguredRealm([GateId](const UTDRealmDefinition& Realm) { return Realm.FindGate(GateId) != nullptr; });
}

const UTDRealmDefinition* UTDRealmDefinition::FindRealmContainingLocation(const FVector& LocationCm)
{
	return FindConfiguredRealm([&LocationCm](const UTDRealmDefinition& Realm) { return Realm.ContainsLocationXY(LocationCm); });
}

#if WITH_EDITOR
EDataValidationResult UTDRealmDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);
	auto AddError = [&Context, &Result](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};

	if (RealmId.IsNone())
	{
		AddError(TEXT("RealmId가 비어 있습니다"));
	}
	if (HalfExtentCm.X <= 0.0 || HalfExtentCm.Y <= 0.0)
	{
		AddError(TEXT("HalfExtentCm의 X·Y는 0보다 커야 합니다"));
	}

	TSet<FName> SeenGateIds;
	for (int32 GateIndex = 0; GateIndex < Gates.Num(); ++GateIndex)
	{
		const FTDRealmGate& Gate = Gates[GateIndex];
		if (Gate.GateId.IsNone())
		{
			AddError(FString::Printf(TEXT("Gates[%d]의 GateId가 비어 있습니다"), GateIndex));
			continue;
		}

		bool bIsDuplicate = false;
		SeenGateIds.Add(Gate.GateId, &bIsDuplicate);
		if (bIsDuplicate)
		{
			AddError(FString::Printf(TEXT("GateId '%s'가 중복됩니다"), *Gate.GateId.ToString()));
		}
		if (!ContainsLocationXY(Gate.ArrivalTransform.GetLocation()))
		{
			AddError(FString::Printf(TEXT("관문 '%s'의 ArrivalTransform이 이계 경계 밖에 있습니다"), *Gate.GateId.ToString()));
		}
		if (ContainsLocationXY(Gate.FieldReturnTransform.GetLocation()))
		{
			AddError(FString::Printf(TEXT("관문 '%s'의 FieldReturnTransform이 이계 경계 안에 있습니다"), *Gate.GateId.ToString()));
		}
		if (Gate.ApproachLengthCm <= 0.f || Gate.DepartureLengthCm <= 0.f || Gate.PassageHalfWidthCm <= 0.f || Gate.PreloadLeadCm <= 0.f)
		{
			AddError(FString::Printf(TEXT("관문 '%s'의 통로 길이 값은 모두 0보다 커야 합니다"), *Gate.GateId.ToString()));
		}
		if (Gate.GateKind != ETDRealmGateKind::Lift)
		{
			const double YawDifferenceDegrees = FMath::Abs(FRotator::NormalizeAxis(Gate.ArrivalTransform.Rotator().Yaw - Gate.FieldReturnTransform.Rotator().Yaw));
			if (YawDifferenceDegrees > 0.5)
			{
				AddError(FString::Printf(TEXT("관문 '%s'의 두 Transform yaw 차이 %.2f도가 0.5도를 넘습니다"), *Gate.GateId.ToString(), YawDifferenceDegrees));
			}
			continue;
		}

		TArray<FString> LiftErrors;
		CollectRealmLiftGateErrors(Gate, LiftErrors);
		for (const FString& LiftError : LiftErrors)
		{
			AddError(LiftError);
		}
	}
	return Result;
}
#endif
