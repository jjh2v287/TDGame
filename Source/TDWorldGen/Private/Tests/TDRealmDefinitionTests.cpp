#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "TDWorldGenSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "World/TDRealmDefinitions.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	const FVector TDTestStarfieldCenterCm(-6300.0, -75600.0, -3400.0);
	const FVector TDTestStarfieldHalfExtentCm(25200.0, 25200.0, 20000.0);

	const FVector TDTestSouthwestFieldReturnCm(-25000.0, -44000.0, 1244.5);
	const FVector TDTestSouthwestArrivalCm(-25000.0, -74567.0, -3400.0);
	constexpr float TDTestSouthwestApproachCm = 6400.f;
	constexpr float TDTestSouthwestDepartureCm = 24167.f;

	const FVector TDTestSoutheastFieldReturnCm(5452.0, -44052.0, 623.0);
	const FVector TDTestSoutheastArrivalCm(893.0, -56107.0, -3400.0);
	const FVector TDTestSoutheastShaftAxisCm(9200.0, -47800.0, 0.0);
	constexpr double TDTestSoutheastFieldLandingZCm = 600.0;
	constexpr double TDTestSoutheastRealmLandingZCm = -3400.0;

	FTDRealmGate MakeTestSouthwestGate()
	{
		FTDRealmGate Gate;
		Gate.GateId = TEXT("TestGateSouthwest");
		Gate.ArrivalTransform = FTransform(FRotator(0.0, 270.0, 0.0), TDTestSouthwestArrivalCm);
		Gate.FieldReturnTransform = FTransform(FRotator(0.0, 270.0, 0.0), TDTestSouthwestFieldReturnCm);
		Gate.ApproachLengthCm = TDTestSouthwestApproachCm;
		Gate.DepartureLengthCm = TDTestSouthwestDepartureCm;
		return Gate;
	}

	FTDRealmGate MakeTestSoutheastLiftGate()
	{
		FTDRealmGate Gate;
		Gate.GateId = TEXT("TestGateSoutheast");
		Gate.GateKind = ETDRealmGateKind::Lift;
		Gate.ArrivalTransform = FTransform(FRotator(0.0, 225.0, 0.0), TDTestSoutheastArrivalCm);
		Gate.FieldReturnTransform = FTransform(FRotator(0.0, 315.0, 0.0), TDTestSoutheastFieldReturnCm);
		Gate.ApproachLengthCm = 5300.f;
		Gate.DepartureLengthCm = 11748.f;
		Gate.Lift.FieldLandingTransform = FTransform(FRotator(0.0, 135.0, 0.0), FVector(TDTestSoutheastShaftAxisCm.X, TDTestSoutheastShaftAxisCm.Y, TDTestSoutheastFieldLandingZCm));
		Gate.Lift.RealmLandingTransform = FTransform(FRotator(0.0, 225.0, 0.0), FVector(TDTestSoutheastShaftAxisCm.X, TDTestSoutheastShaftAxisCm.Y, TDTestSoutheastRealmLandingZCm));
		return Gate;
	}

	TStrongObjectPtr<UTDRealmDefinition> MakeTestStarfieldRealm()
	{
		TStrongObjectPtr<UTDRealmDefinition> Realm(NewObject<UTDRealmDefinition>(GetTransientPackage()));
		Realm->RealmId = TEXT("TestStarfield");
		Realm->CenterCm = TDTestStarfieldCenterCm;
		Realm->HalfExtentCm = TDTestStarfieldHalfExtentCm;
		Realm->Gates.Add(MakeTestSouthwestGate());
		Realm->Gates.Add(MakeTestSoutheastLiftGate());
		return Realm;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmFindGateAndBoundsTest, "TDGame.WorldGen.Realm.FindGateAndBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmFindGateAndBoundsTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTDRealmDefinition> Realm = MakeTestStarfieldRealm();

	const FBox Bounds = Realm->GetBoundsCm();
	TestTrue(TEXT("경계 최솟값 = 중심 - 반폭"), Bounds.Min.Equals(FVector(-31500.0, -100800.0, -23400.0)));
	TestTrue(TEXT("경계 최댓값 = 중심 + 반폭"), Bounds.Max.Equals(FVector(18900.0, -50400.0, 16600.0)));

	TestTrue(TEXT("중심은 경계 안"), Realm->ContainsLocationXY(TDTestStarfieldCenterCm));
	TestTrue(TEXT("Z가 경계 밖이어도 XY 안이면 포함"), Realm->ContainsLocationXY(TDTestStarfieldCenterCm + FVector(1000.0, -1000.0, 500000.0)));
	TestTrue(TEXT("경계 모서리는 포함"), Realm->ContainsLocationXY(TDTestStarfieldCenterCm + FVector(TDTestStarfieldHalfExtentCm.X, -TDTestStarfieldHalfExtentCm.Y, 0.0)));
	TestFalse(TEXT("X가 경계 밖이면 제외"), Realm->ContainsLocationXY(TDTestStarfieldCenterCm + FVector(TDTestStarfieldHalfExtentCm.X + 1.0, 0.0, 0.0)));
	TestFalse(TEXT("필드 원점은 제외"), Realm->ContainsLocationXY(FVector::ZeroVector));
	TestTrue(TEXT("필드와 공유하는 경계선 Y=-50400 위의 점은 포함"), Realm->ContainsLocationXY(FVector(-25000.0, -50400.0, 0.0)));
	TestFalse(TEXT("공유 경계선에서 필드 쪽으로 1cm 넘어가면 제외"), Realm->ContainsLocationXY(FVector(-25000.0, -50399.0, 0.0)));
	TestTrue(TEXT("기본 게임플레이 데이터 레이어는 비어 있음"), Realm->GameplayDataLayer.IsNull());

	const FTDRealmGate* SoutheastGate = Realm->FindGate(TEXT("TestGateSoutheast"));
	if (TestNotNull(TEXT("남동 관문을 ID로 찾음"), SoutheastGate))
	{
		TestEqual(TEXT("찾은 관문 ID"), SoutheastGate->GateId, FName(TEXT("TestGateSoutheast")));
	}
	TestNull(TEXT("없는 관문 ID는 nullptr"), Realm->FindGate(TEXT("MissingGate")));
	TestNull(TEXT("NAME_None 관문 ID는 nullptr"), Realm->FindGate(NAME_None));

	const FTDRealmGate* NearestToSouthwest = Realm->FindNearestGate(FVector(-24000.0, -72000.0, -3400.0));
	if (TestNotNull(TEXT("남서 도착점 근처에서 가장 가까운 관문"), NearestToSouthwest))
	{
		TestEqual(TEXT("남서 도착점 근처는 남서 관문"), NearestToSouthwest->GateId, FName(TEXT("TestGateSouthwest")));
	}
	const FTDRealmGate* NearestToSoutheast = Realm->FindNearestGate(FVector(0.0, -57000.0, -3400.0));
	if (TestNotNull(TEXT("남동 슬롯 끝 근처에서 가장 가까운 관문"), NearestToSoutheast))
	{
		TestEqual(TEXT("남동 슬롯 끝 근처는 남동 관문"), NearestToSoutheast->GateId, FName(TEXT("TestGateSoutheast")));
	}

	Realm->Gates.Reset();
	TestNull(TEXT("관문이 없으면 가장 가까운 관문도 nullptr"), Realm->FindNearestGate(TDTestStarfieldCenterCm));
	TestNull(TEXT("NAME_None으로 설정의 이계를 찾으면 nullptr"), UTDRealmDefinition::FindRealmByGateId(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmGatePassageGeometryTest, "TDGame.WorldGen.Realm.GatePassageGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmGatePassageGeometryTest::RunTest(const FString& Parameters)
{
	FTDRealmGate Gate = MakeTestSouthwestGate();
	Gate.ArrivalTransform.SetRotation(FRotator(10.0, 270.0, 0.0).Quaternion());

	const FVector Direction = Gate.GetPassageDirection();
	TestTrue(TEXT("통로 방향은 yaw 270의 수평 단위벡터"), Direction.Equals(FVector(0.0, -1.0, 0.0), 1.0e-4));
	TestTrue(TEXT("통로 방향의 Z는 0"), FMath::IsNearlyZero(Direction.Z));

	const FVector BoundaryCrossing = Gate.GetBoundaryCrossingCm();
	TestTrue(TEXT("남서 경계 통과점 B의 XY = (-25000, -50400) ± 1cm"), FVector2D(BoundaryCrossing).Equals(FVector2D(-25000.0, -50400.0), 1.0));
	TestTrue(TEXT("경계 통과점 Z = 필드 귀환과 도착 Z를 접근 비율 6400/30567로 보간한 272.05cm"), FMath::IsNearlyEqual(BoundaryCrossing.Z, 272.0526, 0.01));

	FTDRealmGate ZeroLengthGate = MakeTestSouthwestGate();
	ZeroLengthGate.ApproachLengthCm = 0.f;
	ZeroLengthGate.DepartureLengthCm = 0.f;
	TestTrue(TEXT("접근·이탈 길이가 모두 0이면 경계 통과점 = 필드 귀환 위치"), ZeroLengthGate.GetBoundaryCrossingCm().Equals(TDTestSouthwestFieldReturnCm, 1.0e-6));

	TestTrue(TEXT("진입 선로딩 위치 = 도착 위치"), Gate.GetPreloadLocation(true).Equals(TDTestSouthwestArrivalCm, 0.0));
	TestTrue(TEXT("귀환 선로딩 위치 = 필드 귀환 위치"), Gate.GetPreloadLocation(false).Equals(TDTestSouthwestFieldReturnCm, 0.0));
	const FTDRealmGate LiftGate = MakeTestSoutheastLiftGate();
	TestTrue(TEXT("승강 관문 진입 선로딩 위치 = 슬롯 끝 도착 위치"), LiftGate.GetPreloadLocation(true).Equals(TDTestSoutheastArrivalCm, 0.0));
	TestTrue(TEXT("승강 관문 귀환 선로딩 위치 = 상단 협곡 입구"), LiftGate.GetPreloadLocation(false).Equals(TDTestSoutheastFieldReturnCm, 0.0));

	const FTDRealmGate DefaultGate;
	TestEqual(TEXT("기본 접근 길이"), DefaultGate.ApproachLengthCm, 9000.f);
	TestEqual(TEXT("기본 이계 통로 길이"), DefaultGate.DepartureLengthCm, 4500.f);
	TestEqual(TEXT("기본 통로 반폭"), DefaultGate.PassageHalfWidthCm, 1600.f);
	TestEqual(TEXT("기본 선로딩 앞섬"), DefaultGate.PreloadLeadCm, 4000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftDefaultsTest, "TDGame.WorldGen.Realm.LiftDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmLiftDefaultsTest::RunTest(const FString& Parameters)
{
	const FTDRealmGate DefaultGate;
	TestTrue(TEXT("기본 관문 종류는 Passage"), DefaultGate.GateKind == ETDRealmGateKind::Passage);

	const FTDRealmLift& Lift = DefaultGate.Lift;
	TestTrue(TEXT("기본 필드 착지 자세는 항등"), Lift.FieldLandingTransform.Equals(FTransform::Identity, 0.0));
	TestTrue(TEXT("기본 이계 착지 자세는 항등"), Lift.RealmLandingTransform.Equals(FTransform::Identity, 0.0));
	TestEqual(TEXT("기본 정속"), Lift.CruiseSpeedCmPerSecond, 300.f);
	TestEqual(TEXT("기본 가속"), Lift.AccelerationCmPerSecondSquared, 100.f);
	TestEqual(TEXT("기본 제동 감속"), Lift.BrakeDecelerationCmPerSecondSquared, 80.f);
	TestEqual(TEXT("기본 탑승 대기 시간"), Lift.BoardDwellSeconds, 1.2f);
	TestEqual(TEXT("기본 접근 반경"), Lift.ApproachRadiusCm, 4000.f);
	TestEqual(TEXT("기본 호출 속도"), Lift.CallSpeedCmPerSecond, 600.f);
	TestEqual(TEXT("기본 트레드밀 대기 진행 거리"), Lift.HoldDepthCm, 1912.5f);
	TestEqual(TEXT("기본 트레드밀 최대 시간"), Lift.MaxTreadmillSeconds, 20.f);
	TestEqual(TEXT("기본 링 모듈 높이"), Lift.RingModuleHeightCm, 450.f);
	TestEqual(TEXT("기본 링 모듈 개수"), Lift.RingModuleCount, 7);
	TestEqual(TEXT("기본 링 위 끝 깊이"), Lift.RingTopDepthCm, 600.f);
	TestEqual(TEXT("기본 심연 가림막 깊이"), Lift.AbyssScreenDepthCm, 600.f);
	TestTrue(TEXT("기본 덱 반크기"), Lift.DeckHalfExtentCm.Equals(FVector(300.0, 300.0, 15.0), 0.0));
	TestTrue(TEXT("기본 덱 메시는 비어 있음"), Lift.DeckMesh.IsNull());
	TestTrue(TEXT("기본 난간 메시는 비어 있음"), Lift.RailMesh.IsNull());
	TestTrue(TEXT("기본 덱 머티리얼은 비어 있음"), Lift.DeckMaterial.IsNull());
	TestTrue(TEXT("기본 라이너 판 메시는 비어 있음"), Lift.LinerPanelMesh.IsNull());
	TestTrue(TEXT("기본 라이너 머티리얼은 비어 있음"), Lift.LinerMaterial.IsNull());
	TestTrue(TEXT("기본 심연 가림막 머티리얼은 비어 있음"), Lift.AbyssScreenMaterial.IsNull());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmReflectedPropertyNamesTest, "TDGame.WorldGen.Realm.ReflectedPropertyNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmReflectedPropertyNamesTest::RunTest(const FString& Parameters)
{
	auto HasProperty = [](const UStruct* Struct, const TCHAR* PropertyName)
	{
		return Struct->FindPropertyByName(FName(PropertyName)) != nullptr;
	};

	const UStruct* LiftStruct = FTDRealmLift::StaticStruct();
	for (const TCHAR* PropertyName : { TEXT("FieldLandingTransform"), TEXT("RealmLandingTransform"), TEXT("CruiseSpeedCmPerSecond"), TEXT("AccelerationCmPerSecondSquared"), TEXT("BrakeDecelerationCmPerSecondSquared"), TEXT("BoardDwellSeconds"), TEXT("ApproachRadiusCm"), TEXT("CallSpeedCmPerSecond"), TEXT("HoldDepthCm"), TEXT("MaxTreadmillSeconds"), TEXT("RingModuleHeightCm"), TEXT("RingModuleCount"), TEXT("RingTopDepthCm"), TEXT("AbyssScreenDepthCm"), TEXT("DeckHalfExtentCm"), TEXT("DeckMesh"), TEXT("RailMesh"), TEXT("DeckMaterial"), TEXT("LinerPanelMesh"), TEXT("LinerMaterial"), TEXT("AbyssScreenMaterial") })
	{
		TestTrue(FString::Printf(TEXT("FTDRealmLift에 반사 프로퍼티 %s가 있음"), PropertyName), HasProperty(LiftStruct, PropertyName));
	}

	const UStruct* GateStruct = FTDRealmGate::StaticStruct();
	for (const TCHAR* PropertyName : { TEXT("GateId"), TEXT("GateKind"), TEXT("ArrivalTransform"), TEXT("FieldReturnTransform"), TEXT("ApproachLengthCm"), TEXT("DepartureLengthCm"), TEXT("PassageHalfWidthCm"), TEXT("PreloadLeadCm"), TEXT("Lift") })
	{
		TestTrue(FString::Printf(TEXT("FTDRealmGate에 반사 프로퍼티 %s가 있음"), PropertyName), HasProperty(GateStruct, PropertyName));
	}

	const UStruct* RealmClass = UTDRealmDefinition::StaticClass();
	TestTrue(TEXT("UTDRealmDefinition에 반사 프로퍼티 GameplayDataLayer가 있음"), HasProperty(RealmClass, TEXT("GameplayDataLayer")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmConfiguredDefinitionsLoadTest, "TDGame.WorldGen.Realm.ConfiguredDefinitionsLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmConfiguredDefinitionsLoadTest::RunTest(const FString& Parameters)
{
	const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
	if (!TestNotNull(TEXT("월드 생성 설정"), Settings))
	{
		return false;
	}
	if (Settings->RealmDefinitions.IsEmpty())
	{
		AddWarning(TEXT("설정에 이계 정의가 없어 불러오기 검사를 건너뜀"));
		return true;
	}

	for (const TSoftObjectPtr<UTDRealmDefinition>& RealmReference : Settings->RealmDefinitions)
	{
		const UTDRealmDefinition* Realm = RealmReference.LoadSynchronous();
		const FString RealmPath = RealmReference.ToString();
		if (!TestNotNull(FString::Printf(TEXT("이계 정의 %s 불러오기"), *RealmPath), Realm))
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("이계 정의 %s의 RealmId가 있음"), *RealmPath), Realm->RealmId.IsNone());
		TestTrue(FString::Printf(TEXT("이계 정의 %s에 관문이 있음"), *RealmPath), Realm->Gates.Num() > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftTravelDestinationTest, "TDGame.WorldGen.Realm.LiftTravelDestination", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmLiftTravelDestinationTest::RunTest(const FString& Parameters)
{
	FTDRealmGate PassageGate = MakeTestSouthwestGate();
	PassageGate.Lift = MakeTestSoutheastLiftGate().Lift;
	TestTrue(TEXT("통로 진입 목적지 = ArrivalTransform"), PassageGate.GetTravelDestination(true).Equals(PassageGate.ArrivalTransform, 0.0));
	TestTrue(TEXT("통로 귀환 목적지 = FieldReturnTransform"), PassageGate.GetTravelDestination(false).Equals(PassageGate.FieldReturnTransform, 0.0));

	const FTDRealmGate LiftGate = MakeTestSoutheastLiftGate();
	const double ExitOffsetCm = 400.0 * UE_HALF_SQRT_2;
	const FTransform RealmDestination = LiftGate.GetTravelDestination(true);
	const FVector ExpectedRealmLocation(TDTestSoutheastShaftAxisCm.X - ExitOffsetCm, TDTestSoutheastShaftAxisCm.Y - ExitOffsetCm, TDTestSoutheastRealmLandingZCm);
	TestTrue(TEXT("승강 진입 목적지 = 하단 착지 + 출구 방향(요 225) 400cm"), RealmDestination.GetLocation().Equals(ExpectedRealmLocation, 1.0e-3));
	TestTrue(TEXT("승강 진입 목적지 회전 = 하단 착지 회전"), RealmDestination.GetRotation().Equals(LiftGate.Lift.RealmLandingTransform.GetRotation(), 1.0e-6));

	const FTransform FieldDestination = LiftGate.GetTravelDestination(false);
	const FVector ExpectedFieldLocation(TDTestSoutheastShaftAxisCm.X - ExitOffsetCm, TDTestSoutheastShaftAxisCm.Y + ExitOffsetCm, TDTestSoutheastFieldLandingZCm);
	TestTrue(TEXT("승강 귀환 목적지 = 상단 착지 + 출구 방향(요 135) 400cm"), FieldDestination.GetLocation().Equals(ExpectedFieldLocation, 1.0e-3));
	TestTrue(TEXT("승강 귀환 목적지 회전 = 상단 착지 회전"), FieldDestination.GetRotation().Equals(LiftGate.Lift.FieldLandingTransform.GetRotation(), 1.0e-6));

	FTDRealmGate PitchedLiftGate = MakeTestSoutheastLiftGate();
	PitchedLiftGate.Lift.RealmLandingTransform.SetRotation(FRotator(20.0, 90.0, 0.0).Quaternion());
	const FVector PitchedLocation = PitchedLiftGate.GetTravelDestination(true).GetLocation();
	const FVector ExpectedPitchedLocation(TDTestSoutheastShaftAxisCm.X, TDTestSoutheastShaftAxisCm.Y + 400.0, TDTestSoutheastRealmLandingZCm);
	TestTrue(TEXT("착지 자세에 피치가 있어도 출구는 수평 400cm, 높이는 착지 바닥"), PitchedLocation.Equals(ExpectedPitchedLocation, 1.0e-3));
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

	TStrongObjectPtr<UTDRealmDefinition> Realm = MakeTestStarfieldRealm();
	TestTrue(TEXT("올바른 이계 정의는 Valid(승강 관문의 두 Transform yaw 차이 90도 포함)"), Validate(*Realm) == EDataValidationResult::Valid);

	Realm = MakeTestStarfieldRealm();
	Realm->RealmId = NAME_None;
	TestTrue(TEXT("RealmId가 비면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->HalfExtentCm = FVector(0.0, 25200.0, 20000.0);
	TestTrue(TEXT("반폭 X가 0이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[0].GateId = NAME_None;
	TestTrue(TEXT("GateId가 비면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[1].GateId = Realm->Gates[0].GateId;
	TestTrue(TEXT("GateId가 중복되면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[0].ArrivalTransform.SetLocation(FVector(1000.0, 1000.0, 0.0));
	TestTrue(TEXT("도착점이 경계 밖이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[1].FieldReturnTransform.SetLocation(TDTestStarfieldCenterCm);
	TestTrue(TEXT("필드 귀환점이 경계 안이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[0].ArrivalTransform.SetRotation(FRotator(0.0, 270.4, 0.0).Quaternion());
	TestTrue(TEXT("통로 관문 두 Transform yaw 차이 0.4도는 Valid"), Validate(*Realm) == EDataValidationResult::Valid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[0].ArrivalTransform.SetRotation(FRotator(0.0, 271.0, 0.0).Quaternion());
	TestTrue(TEXT("통로 관문 두 Transform yaw 차이 1도는 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[0].ArrivalTransform.SetRotation(FRotator(0.0, 179.8, 0.0).Quaternion());
	Realm->Gates[0].FieldReturnTransform.SetRotation(FRotator(0.0, -179.9, 0.0).Quaternion());
	TestTrue(TEXT("yaw가 ±180을 넘나들어도 차이 0.3도면 Valid"), Validate(*Realm) == EDataValidationResult::Valid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[1].GateKind = ETDRealmGateKind::Passage;
	TestTrue(TEXT("yaw 차이 90도인 관문을 통로 종류로 바꾸면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[0].ApproachLengthCm = 0.f;
	TestTrue(TEXT("접근 길이가 0이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[0].DepartureLengthCm = 0.f;
	TestTrue(TEXT("이계 통로 길이가 0이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[0].PassageHalfWidthCm = 0.f;
	TestTrue(TEXT("통로 반폭이 0이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);

	Realm = MakeTestStarfieldRealm();
	Realm->Gates[0].PreloadLeadCm = 0.f;
	TestTrue(TEXT("선로딩 앞섬이 0이면 Invalid"), Validate(*Realm) == EDataValidationResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftDataValidationTest, "TDGame.WorldGen.Realm.LiftDataValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDRealmLiftDataValidationTest::RunTest(const FString& Parameters)
{
	auto CollectLiftRealmErrors = [this](TFunctionRef<void(FTDRealmGate&)> ChangeLiftGate)
	{
		TStrongObjectPtr<UTDRealmDefinition> Realm = MakeTestStarfieldRealm();
		ChangeLiftGate(Realm->Gates.Last());
		FDataValidationContext Context;
		const EDataValidationResult Result = Realm->IsDataValid(Context);
		TArray<FText> Warnings;
		TArray<FText> Errors;
		Context.SplitIssues(Warnings, Errors);
		TArray<FString> ErrorMessages;
		for (const FText& Error : Errors)
		{
			ErrorMessages.Add(Error.ToString());
		}
		TestEqual(TEXT("결과가 Invalid인 것과 오류가 있는 것은 같음"), Result == EDataValidationResult::Invalid, !ErrorMessages.IsEmpty());
		return ErrorMessages;
	};
	auto HasErrorContaining = [](const TArray<FString>& ErrorMessages, const TCHAR* Phrase)
	{
		return ErrorMessages.ContainsByPredicate([Phrase](const FString& Message) { return Message.Contains(Phrase); });
	};

	TestEqual(TEXT("명세 수치의 승강 관문은 오류 없음(접근·이탈 길이가 낙차와 달라도 됨)"), CollectLiftRealmErrors([](FTDRealmGate&) {}).Num(), 0);
	TestEqual(TEXT("통로 관문은 승강 값이 잘못돼도 승강 규칙을 받지 않음"), CollectLiftRealmErrors([](FTDRealmGate& Gate)
	{
		Gate.GateKind = ETDRealmGateKind::Passage;
		Gate.ArrivalTransform.SetRotation(Gate.FieldReturnTransform.GetRotation());
		Gate.Lift.CruiseSpeedCmPerSecond = 0.f;
		Gate.Lift.HoldDepthCm = 0.f;
		Gate.Lift.RingModuleCount = 0;
		Gate.Lift.RealmLandingTransform.SetLocation(Gate.Lift.FieldLandingTransform.GetLocation() + FVector(500.0, 0.0, 100.0));
	}).Num(), 0);

	const TCHAR* LandingHeightPhrase = TEXT("착지 Z");
	const TArray<FString> LevelLandingErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate)
	{
		Gate.Lift.RealmLandingTransform.SetLocation(Gate.Lift.FieldLandingTransform.GetLocation());
	});
	TestTrue(TEXT("상단·하단 착지 높이가 같으면 착지 높이 오류 하나"), LevelLandingErrors.Num() == 1 && HasErrorContaining(LevelLandingErrors, LandingHeightPhrase));
	const TArray<FString> InvertedLandingErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate)
	{
		Gate.Lift.FieldLandingTransform.SetLocation(FVector(TDTestSoutheastShaftAxisCm.X, TDTestSoutheastShaftAxisCm.Y, TDTestSoutheastRealmLandingZCm - 100.0));
	});
	TestTrue(TEXT("상단 착지가 하단보다 낮으면 착지 높이 오류 하나"), InvertedLandingErrors.Num() == 1 && HasErrorContaining(InvertedLandingErrors, LandingHeightPhrase));

	TestEqual(TEXT("두 착지 XY 차이 0.9cm는 오류 없음"), CollectLiftRealmErrors([](FTDRealmGate& Gate)
	{
		Gate.Lift.RealmLandingTransform.AddToTranslation(FVector(0.9, 0.0, 0.0));
	}).Num(), 0);
	const TArray<FString> MisalignedErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate)
	{
		Gate.Lift.RealmLandingTransform.AddToTranslation(FVector(0.0, 1.5, 0.0));
	});
	TestTrue(TEXT("두 착지 XY 차이 1.5cm는 XY 오류 하나"), MisalignedErrors.Num() == 1 && HasErrorContaining(MisalignedErrors, TEXT("XY")));

	const TCHAR* SpeedPhrase = TEXT("속도·가속");
	const TArray<FString> CruiseErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate) { Gate.Lift.CruiseSpeedCmPerSecond = 0.f; });
	TestTrue(TEXT("정속이 0이면 속도 오류 하나"), CruiseErrors.Num() == 1 && HasErrorContaining(CruiseErrors, SpeedPhrase));
	const TArray<FString> AccelerationErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate) { Gate.Lift.AccelerationCmPerSecondSquared = 0.f; });
	TestTrue(TEXT("가속이 0이면 속도 오류 하나(라이너 검사는 건너뜀)"), AccelerationErrors.Num() == 1 && HasErrorContaining(AccelerationErrors, SpeedPhrase));
	const TArray<FString> BrakeErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate) { Gate.Lift.BrakeDecelerationCmPerSecondSquared = -80.f; });
	TestTrue(TEXT("제동 감속이 음수면 속도 오류 하나(라이너 검사는 건너뜀)"), BrakeErrors.Num() == 1 && HasErrorContaining(BrakeErrors, SpeedPhrase));
	const TArray<FString> CallErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate) { Gate.Lift.CallSpeedCmPerSecond = 0.f; });
	TestTrue(TEXT("호출 속도가 0이면 속도 오류 하나"), CallErrors.Num() == 1 && HasErrorContaining(CallErrors, SpeedPhrase));

	const TCHAR* SlipPhrase = TEXT("미끄러짐");
	const TCHAR* StoppedPhrase = TEXT("멈춘 동안");
	auto SetTravelLength = [](FTDRealmGate& Gate, double TravelLengthCm)
	{
		Gate.Lift.RealmLandingTransform.SetLocation(FVector(TDTestSoutheastShaftAxisCm.X, TDTestSoutheastShaftAxisCm.Y, TDTestSoutheastFieldLandingZCm - TravelLengthCm));
	};

	TestEqual(TEXT("링 위 끝 605cm는 보이는 링 미끄러짐 1.88cm로 허용 2cm 안이라 오류 없음"), CollectLiftRealmErrors([](FTDRealmGate& Gate) { Gate.Lift.RingTopDepthCm = 605.f; }).Num(), 0);
	const TArray<FString> RingTopErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate) { Gate.Lift.RingTopDepthCm = 610.f; });
	TestTrue(TEXT("링 위 끝 610cm는 보이는 링 미끄러짐 2.18cm라 하행 제동 시작 오류 하나"), RingTopErrors.Num() == 1 && HasErrorContaining(RingTopErrors, TEXT("하행 제동 시작")) && HasErrorContaining(RingTopErrors, SlipPhrase));
	const TArray<FString> ShallowHoldErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate) { Gate.Lift.HoldDepthCm = 1900.f; });
	TestTrue(TEXT("대기 진행 거리 1900cm는 보이는 링 미끄러짐 2.34cm라 하행 제동 시작 오류 하나"), ShallowHoldErrors.Num() == 1 && HasErrorContaining(ShallowHoldErrors, TEXT("하행 제동 시작")) && HasErrorContaining(ShallowHoldErrors, SlipPhrase));

	const TArray<FString> SixRingErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate) { Gate.Lift.RingModuleCount = 6; });
	TestTrue(TEXT("링 모듈 6개(늘 덮는 아래 끝 2850cm, 옛 기준 3300cm면 통과)는 하행 재개 끝과 상행 제동 시작 오류 둘"), SixRingErrors.Num() == 2 && HasErrorContaining(SixRingErrors, TEXT("하행 재개 끝")) && HasErrorContaining(SixRingErrors, TEXT("상행 제동 시작")));
	const TArray<FString> ShortLinerErrors = CollectLiftRealmErrors([](FTDRealmGate& Gate) { Gate.Lift.RingModuleCount = 5; });
	TestTrue(TEXT("링 모듈 5개(늘 덮는 아래 끝 2400cm)는 하행 재개 끝과 상행 제동 시작 라이너 오류 둘"), ShortLinerErrors.Num() == 2 && HasErrorContaining(ShortLinerErrors, TEXT("하행 재개 끝")) && HasErrorContaining(ShortLinerErrors, TEXT("상행 제동 시작")));

	TestEqual(TEXT("낙차 4040cm는 상행 제동 시작 아래 끝 3290cm라 오류 없음"), CollectLiftRealmErrors([&SetTravelLength](FTDRealmGate& Gate) { SetTravelLength(Gate, 4040.0); }).Num(), 0);
	const TArray<FString> LongTravelErrors = CollectLiftRealmErrors([&SetTravelLength](FTDRealmGate& Gate) { SetTravelLength(Gate, 4060.0); });
	TestTrue(TEXT("낙차 4060cm는 상행 제동 시작 아래 끝 3310cm가 3300cm를 넘어 오류 하나(옛 기준 3750cm면 통과)"), LongTravelErrors.Num() == 1 && HasErrorContaining(LongTravelErrors, TEXT("상행 제동 시작")));
	const TArray<FString> ShortTravelErrors = CollectLiftRealmErrors([&SetTravelLength](FTDRealmGate& Gate) { SetTravelLength(Gate, 3700.0); });
	TestTrue(TEXT("낙차 3700cm는 보이는 링 미끄러짐 2.92cm라 상행 재개 끝 오류 하나"), ShortTravelErrors.Num() == 1 && HasErrorContaining(ShortTravelErrors, TEXT("상행 재개 끝")) && HasErrorContaining(ShortTravelErrors, SlipPhrase));
	const TArray<FString> ExposedStopErrors = CollectLiftRealmErrors([&SetTravelLength](FTDRealmGate& Gate) { SetTravelLength(Gate, 3300.0); });
	TestTrue(TEXT("낙차 3300cm는 상행 대기 중에도 라이너 위 끝 위가 보여 상행 재개 끝 오류 하나"), ExposedStopErrors.Num() == 1 && HasErrorContaining(ExposedStopErrors, TEXT("상행 재개 끝")) && HasErrorContaining(ExposedStopErrors, StoppedPhrase));
	return true;
}
#endif

#endif
