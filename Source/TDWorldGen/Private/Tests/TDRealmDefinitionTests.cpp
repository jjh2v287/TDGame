#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "World/TDRealmDefinitions.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	const FVector TDTestRealmCenterCm(-302400.0, 302400.0, 0.0);
	const FVector TDTestRealmHalfExtentCm(25200.0, 25200.0, 20000.0);

	FTDRealmGate MakeTestGate(const TCHAR* GateId, const FVector& ArrivalLocationCm, const FVector& FieldReturnLocationCm)
	{
		FTDRealmGate Gate;
		Gate.GateId = GateId;
		Gate.ArrivalTransform = FTransform(FRotator::ZeroRotator, ArrivalLocationCm);
		Gate.FieldReturnTransform = FTransform(FRotator::ZeroRotator, FieldReturnLocationCm);
		return Gate;
	}

	TStrongObjectPtr<UTDRealmDefinition> MakeTestRealm()
	{
		TStrongObjectPtr<UTDRealmDefinition> Realm(NewObject<UTDRealmDefinition>(GetTransientPackage()));
		Realm->RealmId = TEXT("TestStarfield");
		Realm->CenterCm = TDTestRealmCenterCm;
		Realm->HalfExtentCm = TDTestRealmHalfExtentCm;
		Realm->Gates.Add(MakeTestGate(TEXT("TestGateWest"), TDTestRealmCenterCm + FVector(-17300.0, 1000.0, 120.0), FVector(-36500.0, -26000.0, 650.0)));
		Realm->Gates.Add(MakeTestGate(TEXT("TestGateEast"), TDTestRealmCenterCm + FVector(18300.0, 7000.0, 120.0), FVector(31000.0, 40500.0, 370.0)));
		return Realm;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmFindGateAndBoundsTest, "TDGame.WorldGen.Realm.FindGateAndBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmFindGateAndBoundsTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTDRealmDefinition> Realm = MakeTestRealm();

	const FBox Bounds = Realm->GetBoundsCm();
	TestTrue(TEXT("경계 최솟값 = 중심 - 반폭"), Bounds.Min.Equals(TDTestRealmCenterCm - TDTestRealmHalfExtentCm));
	TestTrue(TEXT("경계 최댓값 = 중심 + 반폭"), Bounds.Max.Equals(TDTestRealmCenterCm + TDTestRealmHalfExtentCm));

	TestTrue(TEXT("중심은 경계 안"), Realm->ContainsLocationXY(TDTestRealmCenterCm));
	TestTrue(TEXT("Z가 경계 밖이어도 XY 안이면 포함"), Realm->ContainsLocationXY(TDTestRealmCenterCm + FVector(1000.0, -1000.0, 500000.0)));
	TestTrue(TEXT("경계 모서리는 포함"), Realm->ContainsLocationXY(TDTestRealmCenterCm + FVector(TDTestRealmHalfExtentCm.X, -TDTestRealmHalfExtentCm.Y, 0.0)));
	TestFalse(TEXT("X가 경계 밖이면 제외"), Realm->ContainsLocationXY(TDTestRealmCenterCm + FVector(TDTestRealmHalfExtentCm.X + 1.0, 0.0, 0.0)));
	TestFalse(TEXT("필드 원점은 제외"), Realm->ContainsLocationXY(FVector::ZeroVector));

	const FTDRealmGate* EastGate = Realm->FindGate(TEXT("TestGateEast"));
	if (TestNotNull(TEXT("동쪽 관문을 ID로 찾음"), EastGate))
	{
		TestEqual(TEXT("찾은 관문 ID"), EastGate->GateId, FName(TEXT("TestGateEast")));
	}
	TestNull(TEXT("없는 관문 ID는 nullptr"), Realm->FindGate(TEXT("MissingGate")));
	TestNull(TEXT("NAME_None 관문 ID는 nullptr"), Realm->FindGate(NAME_None));

	const FTDRealmGate* NearestToWest = Realm->FindNearestGate(TDTestRealmCenterCm + FVector(-15000.0, -3000.0, -4000.0));
	if (TestNotNull(TEXT("서쪽 근처에서 가장 가까운 관문"), NearestToWest))
	{
		TestEqual(TEXT("서쪽 근처는 서쪽 관문"), NearestToWest->GateId, FName(TEXT("TestGateWest")));
	}
	const FTDRealmGate* NearestToEast = Realm->FindNearestGate(TDTestRealmCenterCm + FVector(12000.0, 9000.0, 0.0));
	if (TestNotNull(TEXT("동쪽 근처에서 가장 가까운 관문"), NearestToEast))
	{
		TestEqual(TEXT("동쪽 근처는 동쪽 관문"), NearestToEast->GateId, FName(TEXT("TestGateEast")));
	}

	Realm->Gates.Reset();
	TestNull(TEXT("관문이 없으면 가장 가까운 관문도 nullptr"), Realm->FindNearestGate(TDTestRealmCenterCm));
	TestNull(TEXT("NAME_None으로 설정의 이계를 찾으면 nullptr"), UTDRealmDefinition::FindRealmByGateId(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmGatePassageGeometryTest, "TDGame.WorldGen.Realm.GatePassageGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmGatePassageGeometryTest::RunTest(const FString& Parameters)
{
	FTDRealmGate Gate;
	Gate.ArrivalTransform = FTransform(FRotator(10.0, 90.0, 0.0), FVector(-280000.0, 310000.0, 1520.0));
	Gate.FieldReturnTransform = FTransform(FRotator(0.0, 90.0, 0.0), FVector(-45600.0, -24100.0, 2300.0));

	const FVector Direction = Gate.GetPassageDirection();
	TestTrue(TEXT("통로 방향은 yaw 90의 수평 단위벡터"), Direction.Equals(FVector(0.0, 1.0, 0.0), 1.0e-4));
	TestTrue(TEXT("통로 방향의 Z는 0"), FMath::IsNearlyZero(Direction.Z));

	const FVector SeamOffset = Gate.GetSeamOffsetCm();
	TestTrue(TEXT("이음매 평행이동량 = 도착 - 필드 귀환"), SeamOffset.Equals(FVector(-234400.0, 334100.0, -780.0), 1.0e-3));
	TestTrue(TEXT("필드 귀환 + 평행이동량 = 도착"), (Gate.FieldReturnTransform.GetLocation() + SeamOffset).Equals(Gate.ArrivalTransform.GetLocation(), 1.0e-3));

	FTDRealmGate DefaultGate;
	TestEqual(TEXT("기본 접근 길이"), DefaultGate.ApproachLengthCm, 9000.f);
	TestEqual(TEXT("기본 이계 통로 길이"), DefaultGate.DepartureLengthCm, 4500.f);
	TestEqual(TEXT("기본 이음매 버퍼"), DefaultGate.SeamBufferCm, 3000.f);
	TestEqual(TEXT("기본 통로 반폭"), DefaultGate.PassageHalfWidthCm, 1600.f);
	TestEqual(TEXT("기본 선로딩 앞섬"), DefaultGate.PreloadLeadCm, 4000.f);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmDataValidationTest, "TDGame.WorldGen.Realm.DataValidationRejectsBadGates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmDataValidationTest::RunTest(const FString& Parameters)
{
	auto Validate = [](const UTDRealmDefinition& Realm)
	{
		FDataValidationContext Context;
		return Realm.IsDataValid(Context);
	};

	TStrongObjectPtr<UTDRealmDefinition> Realm = MakeTestRealm();
	TestTrue(TEXT("올바른 이계 정의는 Valid"), Validate(*Realm) == EDataValidationResult::Valid);

	Realm = MakeTestRealm();
	Realm->RealmId = NAME_None;
	TestTrue(TEXT("RealmId가 비면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestRealm();
	Realm->HalfExtentCm = FVector(0.0, 25200.0, 20000.0);
	TestTrue(TEXT("반폭 X가 0이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestRealm();
	Realm->Gates[0].GateId = NAME_None;
	TestTrue(TEXT("GateId가 비면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestRealm();
	Realm->Gates[1].GateId = Realm->Gates[0].GateId;
	TestTrue(TEXT("GateId가 중복되면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestRealm();
	Realm->Gates[0].ArrivalTransform.SetLocation(FVector(1000.0, 1000.0, 0.0));
	TestTrue(TEXT("도착점이 경계 밖이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestRealm();
	Realm->Gates[1].FieldReturnTransform.SetLocation(TDTestRealmCenterCm);
	TestTrue(TEXT("필드 귀환점이 경계 안이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestRealm();
	Realm->Gates[0].ArrivalTransform.SetRotation(FRotator(0.0, 0.4, 0.0).Quaternion());
	TestTrue(TEXT("두 Transform yaw 차이 0.4도는 Valid"), Validate(*Realm) == EDataValidationResult::Valid);

	Realm = MakeTestRealm();
	Realm->Gates[0].ArrivalTransform.SetRotation(FRotator(0.0, 1.0, 0.0).Quaternion());
	TestTrue(TEXT("두 Transform yaw 차이 1도는 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestRealm();
	Realm->Gates[0].ArrivalTransform.SetRotation(FRotator(0.0, 179.8, 0.0).Quaternion());
	Realm->Gates[0].FieldReturnTransform.SetRotation(FRotator(0.0, -179.9, 0.0).Quaternion());
	TestTrue(TEXT("yaw가 ±180을 넘나들어도 차이 0.3도면 Valid"), Validate(*Realm) == EDataValidationResult::Valid);

	Realm = MakeTestRealm();
	Realm->Gates[1].ApproachLengthCm = 0.f;
	TestTrue(TEXT("접근 길이가 0이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestRealm();
	Realm->Gates[1].DepartureLengthCm = 0.f;
	TestTrue(TEXT("이계 통로 길이가 0이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestRealm();
	Realm->Gates[1].PassageHalfWidthCm = 0.f;
	TestTrue(TEXT("통로 반폭이 0이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);
	return true;
}
#endif

#endif
