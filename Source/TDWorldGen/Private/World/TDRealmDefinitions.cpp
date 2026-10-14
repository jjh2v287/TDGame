#include "World/TDRealmDefinitions.h"

#include "TDWorldGenSettings.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
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
}

FVector FTDRealmGate::GetPassageDirection() const
{
	const FVector Forward = ArrivalTransform.GetRotation().GetForwardVector();
	return FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
}

FVector FTDRealmGate::GetSeamOffsetCm() const
{
	return ArrivalTransform.GetLocation() - FieldReturnTransform.GetLocation();
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
		const double YawDifferenceDegrees = FMath::Abs(FRotator::NormalizeAxis(Gate.ArrivalTransform.Rotator().Yaw - Gate.FieldReturnTransform.Rotator().Yaw));
		if (YawDifferenceDegrees > 0.5)
		{
			AddError(FString::Printf(TEXT("관문 '%s'의 두 Transform yaw 차이 %.2f도가 0.5도를 넘습니다"), *Gate.GateId.ToString(), YawDifferenceDegrees));
		}
		if (Gate.ApproachLengthCm <= 0.f || Gate.DepartureLengthCm <= 0.f || Gate.SeamBufferCm <= 0.f || Gate.PassageHalfWidthCm <= 0.f || Gate.PreloadLeadCm <= 0.f)
		{
			AddError(FString::Printf(TEXT("관문 '%s'의 통로 길이 값은 모두 0보다 커야 합니다"), *Gate.GateId.ToString()));
		}
	}
	return Result;
}
#endif
