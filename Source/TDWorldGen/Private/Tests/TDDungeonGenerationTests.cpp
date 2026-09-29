#if WITH_DEV_AUTOMATION_TESTS

#include "Dungeon/TDDungeonGeneration.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	UTDDungeonTheme* MakeCryptTheme()
	{
		UTDDungeonTheme* Theme = NewObject<UTDDungeonTheme>(GetTransientPackage());
		Theme->FillCryptPlaceholderModules();
		return Theme;
	}

	UTDDungeonFlowTemplate* MakeFlowTemplate(ETDDungeonFlowKind Kind)
	{
		UTDDungeonFlowTemplate* Template = NewObject<UTDDungeonFlowTemplate>(GetTransientPackage());
		Template->ApplyKindDefaults(Kind);
		return Template;
	}

	const ETDDungeonFlowKind AllFlows[] = {ETDDungeonFlowKind::Linear, ETDDungeonFlowKind::Branch, ETDDungeonFlowKind::Loop, ETDDungeonFlowKind::Hub, ETDDungeonFlowKind::KeyLock};
	const ETDDungeonSize AllSizes[] = {ETDDungeonSize::Small, ETDDungeonSize::Medium, ETDDungeonSize::Large};

	FString ExtractFailurePrefix(const FString& Error)
	{
		int32 BracketIndex = INDEX_NONE;
		if (Error.FindChar(TEXT(']'), BracketIndex))
		{
			return Error.Left(BracketIndex + 1);
		}
		int32 ColonIndex = INDEX_NONE;
		if (Error.FindChar(TEXT(':'), ColonIndex))
		{
			return Error.Left(ColonIndex);
		}
		return Error;
	}

	bool HasValidationItem(const FTDValidationReport& Report, const TCHAR* Code, ETDValidationSeverity Severity)
	{
		const FName CodeName(Code);
		return Report.Items.ContainsByPredicate([&CodeName, Severity](const FTDValidationItem& Item) { return Item.Code == CodeName && Item.Severity == Severity; });
	}

	void AddSingleCellRoom(FTDDungeonLayout& Layout, const FIntPoint& Cell, ETDRoomRole Role)
	{
		FTDPlacedRoom& Room = Layout.Rooms.AddDefaulted_GetRef();
		Room.RoomId = FName(*FString::Printf(TEXT("r%d"), Layout.Rooms.Num() - 1));
		Room.CellOrigin = Cell;
		Room.Cells = {Cell};
		Room.Roles = {Role};
	}

	void AddDoorBetween(FTDDungeonLayout& Layout, int32 RoomA, int32 RoomB, ETDDoorDirection DirectionFromA)
	{
		FTDPlacedDoor& Door = Layout.Doors.AddDefaulted_GetRef();
		Door.RoomA = Layout.Rooms[RoomA].RoomId;
		Door.RoomB = Layout.Rooms[RoomB].RoomId;
		Door.CellA = Layout.Rooms[RoomA].Cells[0];
		Door.CellB = Door.CellA + TDDungeon::DirectionOffset(DirectionFromA);
		Door.DirectionFromA = DirectionFromA;
	}

	FTDDungeonLayout MakeSideBranchLayout()
	{
		FTDDungeonLayout Layout;
		AddSingleCellRoom(Layout, FIntPoint(0, 1), ETDRoomRole::Entrance);
		AddSingleCellRoom(Layout, FIntPoint(1, 1), ETDRoomRole::Combat);
		AddSingleCellRoom(Layout, FIntPoint(2, 1), ETDRoomRole::Boss);
		AddSingleCellRoom(Layout, FIntPoint(1, 2), ETDRoomRole::Combat);
		AddSingleCellRoom(Layout, FIntPoint(1, 3), ETDRoomRole::Treasure);
		AddSingleCellRoom(Layout, FIntPoint(1, 0), ETDRoomRole::Combat);
		AddDoorBetween(Layout, 0, 1, ETDDoorDirection::East);
		AddDoorBetween(Layout, 1, 2, ETDDoorDirection::East);
		AddDoorBetween(Layout, 1, 3, ETDDoorDirection::South);
		AddDoorBetween(Layout, 3, 4, ETDDoorDirection::South);
		AddDoorBetween(Layout, 1, 5, ETDDoorDirection::North);
		Layout.MaxCell = FIntPoint(2, 3);
		return Layout;
	}

	FTDDungeonValidator::FSettings MakeSideBranchSettings()
	{
		FTDDungeonValidator::FSettings Settings;
		Settings.RoomCountRange = FIntPoint(1, 20);
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonAllFlowsSizesSeedsTest, "TDGame.WorldGen.Dungeon.AllFlowsSizesSeeds1To500SuccessRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonAllFlowsSizesSeedsTest::RunTest(const FString& Parameters)
{
	constexpr int32 LastSeed = 500;
	constexpr int32 LastAlwaysPassingSeed = 100;
	constexpr float MinSuccessPercent = 90.0f;
	constexpr int32 MaxFailureSamples = 5;
	UTDDungeonTheme* Theme = MakeCryptTheme();
	int32 Total = 0;
	int32 Failures = 0;
	int32 AlwaysPassingRangeFailures = 0;
	int32 MaxRestarts = 0;
	TMap<FString, int32> FailuresByPrefix;
	TArray<FString> FailureSamples;
	for (const ETDDungeonFlowKind Flow : AllFlows)
	{
		UTDDungeonFlowTemplate* Template = MakeFlowTemplate(Flow);
		for (const ETDDungeonSize Size : AllSizes)
		{
			for (int32 Seed = 1; Seed <= LastSeed; ++Seed)
			{
				++Total;
				FTDDungeonLayout Layout;
				FString Error;
				if (FTDDungeonGenerator::GenerateAndValidate(*Theme, *Template, Size, Seed, Layout, Error))
				{
					MaxRestarts = FMath::Max(MaxRestarts, Layout.LayoutRestarts);
					continue;
				}
				++Failures;
				AlwaysPassingRangeFailures += Seed <= LastAlwaysPassingSeed ? 1 : 0;
				FailuresByPrefix.FindOrAdd(ExtractFailurePrefix(Error)) += 1;
				if (FailureSamples.Num() < MaxFailureSamples)
				{
					FailureSamples.Add(FString::Printf(TEXT("%s %s seed=%d: %s"), TDDungeon::FlowName(Flow), TDDungeon::SizeName(Size), Seed, *Error));
				}
			}
		}
	}
	const float SuccessPercent = 100.0f * (Total - Failures) / Total;
	AddInfo(FString::Printf(TEXT("생성·검증 %d건 중 통과 %d건 (%.2f%%, 기준 %.0f%%), 최대 재시작 %d회"), Total, Total - Failures, SuccessPercent, MinSuccessPercent, MaxRestarts));
	FailuresByPrefix.KeySort(TLess<FString>());
	for (const TPair<FString, int32>& Entry : FailuresByPrefix)
	{
		AddInfo(FString::Printf(TEXT("실패 사유 %s: %d건"), *Entry.Key, Entry.Value));
	}
	for (const FString& Sample : FailureSamples)
	{
		AddInfo(FString::Printf(TEXT("실패 예: %s"), *Sample));
	}
	TestEqual(TEXT("시드 1~100 실패 건수"), AlwaysPassingRangeFailures, 0);
	TestTrue(FString::Printf(TEXT("성공률 %.2f%% >= %.0f%%"), SuccessPercent, MinSuccessPercent), SuccessPercent >= MinSuccessPercent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonDeterminismTest, "TDGame.WorldGen.Dungeon.SameSeedProducesSameHash", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonDeterminismTest::RunTest(const FString& Parameters)
{
	UTDDungeonTheme* Theme = MakeCryptTheme();
	for (const ETDDungeonFlowKind Flow : AllFlows)
	{
		UTDDungeonFlowTemplate* Template = MakeFlowTemplate(Flow);
		for (const int32 Seed : {7, 42, 1234})
		{
			FTDDungeonLayout First;
			FTDDungeonLayout Second;
			FString Error;
			TestTrue(TEXT("첫 생성"), FTDDungeonGenerator::GenerateAndValidate(*Theme, *Template, ETDDungeonSize::Medium, Seed, First, Error));
			TestTrue(TEXT("둘째 생성"), FTDDungeonGenerator::GenerateAndValidate(*Theme, *Template, ETDDungeonSize::Medium, Seed, Second, Error));
			TestEqual(FString::Printf(TEXT("%s seed=%d 해시"), TDDungeon::FlowName(Flow), Seed), First.ComputeHash(), Second.ComputeHash());
			TestEqual(TEXT("JSON 동일"), FTDDungeonGenerator::ToJson(First, FVector::ZeroVector), FTDDungeonGenerator::ToJson(Second, FVector::ZeroVector));
			const FString JsonPath = FPaths::AutomationTransientDir() / FString::Printf(TEXT("TDDungeon/%s_%d/layout.json"), TDDungeon::FlowName(Flow), Seed);
			TestTrue(TEXT("JSON 파일 저장"), FTDDungeonGenerator::WriteJsonFile(First, FVector(300000.0, 300000.0, 0.0), JsonPath));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonKeyLockOrderTest, "TDGame.WorldGen.Dungeon.KeyLockSeeds1To500KeyBeforeLock", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonKeyLockOrderTest::RunTest(const FString& Parameters)
{
	UTDDungeonTheme* Theme = MakeCryptTheme();
	UTDDungeonFlowTemplate* Template = MakeFlowTemplate(ETDDungeonFlowKind::KeyLock);
	int32 Violations = 0;
	for (int32 Seed = 1; Seed <= 500; ++Seed)
	{
		const ETDDungeonSize Size = AllSizes[Seed % 3];
		FTDDungeonLayout Layout;
		FString Error;
		if (!FTDDungeonGenerator::GenerateAndValidate(*Theme, *Template, Size, Seed, Layout, Error))
		{
			++Violations;
			AddError(FString::Printf(TEXT("KeyLock seed=%d 생성 실패: %s"), Seed, *Error));
			continue;
		}
		const bool bHasLockedDoor = Layout.Doors.ContainsByPredicate([](const FTDPlacedDoor& Door) { return Door.IsLocked(); });
		const bool bHasKeyRoom = Layout.Rooms.ContainsByPredicate([](const FTDPlacedRoom& Room) { return !Room.HeldKeyId.IsNone(); });
		const bool bOrderValid = FTDDungeonFlowGenerator::IsKeyBeforeLockOrderValid(Layout.Graph);
		if (bHasLockedDoor && bHasKeyRoom && bOrderValid)
		{
			continue;
		}
		++Violations;
		AddError(FString::Printf(TEXT("KeyLock seed=%d: locked=%d key=%d order=%d"), Seed, bHasLockedDoor, bHasKeyRoom, bOrderValid));
	}
	TestEqual(TEXT("열쇠→잠금 순서 위반"), Violations, 0);
	return Violations == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonCorruptionDetectedTest, "TDGame.WorldGen.Dungeon.RemovedDoorFailsValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonCorruptionDetectedTest::RunTest(const FString& Parameters)
{
	UTDDungeonTheme* Theme = MakeCryptTheme();
	UTDDungeonFlowTemplate* Template = MakeFlowTemplate(ETDDungeonFlowKind::Linear);
	FTDDungeonLayout Layout;
	FString Error;
	if (!TestTrue(TEXT("정상 생성"), FTDDungeonGenerator::GenerateAndValidate(*Theme, *Template, ETDDungeonSize::Medium, 3, Layout, Error)))
	{
		return false;
	}
	const FTDPlacedRoom* Boss = Layout.FindRoomByRole(ETDRoomRole::Boss);
	if (!TestNotNull(TEXT("보스 방"), Boss))
	{
		return false;
	}
	const int32 BossDoorIndex = Layout.Doors.IndexOfByPredicate([Boss](const FTDPlacedDoor& Door) { return Door.RoomA == Boss->RoomId || Door.RoomB == Boss->RoomId; });
	if (!TestNotEqual(TEXT("보스 문 존재"), BossDoorIndex, static_cast<int32>(INDEX_NONE)))
	{
		return false;
	}
	Layout.Doors.RemoveAt(BossDoorIndex);
	FTDDungeonValidator::FSettings Settings;
	Settings.RoomCountRange = TDDungeon::RoomCountRange(ETDDungeonSize::Medium);
	const FTDValidationReport Report = FTDDungeonValidator::Validate(Layout, Settings);
	TestFalse(TEXT("문 제거 후 검증 실패"), Report.bPassed);
	TestTrue(TEXT("연결성 오류 보고"), Report.Items.ContainsByPredicate([](const FTDValidationItem& Item) { return Item.Code == FName(TEXT("connectivity")) && Item.Severity == ETDValidationSeverity::Error; }));
	TestTrue(TEXT("점수 100 미만"), Report.Score < 100.0f);
	Layout.Validation = Report;
	FTDDungeonLayout Intact;
	FTDDungeonGenerator::GenerateAndValidate(*Theme, *Template, ETDDungeonSize::Medium, 3, Intact, Error);
	TestTrue(TEXT("손상 전 점수가 더 높음"), FTDCandidateSelector::ScoreDungeon(Intact) > FTDCandidateSelector::ScoreDungeon(Layout));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonMissingBossRoomTest, "TDGame.WorldGen.Dungeon.ValidatorMissingBossRoomIsError", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonMissingBossRoomTest::RunTest(const FString& Parameters)
{
	UTDDungeonTheme* Theme = MakeCryptTheme();
	UTDDungeonFlowTemplate* Template = MakeFlowTemplate(ETDDungeonFlowKind::Linear);
	FTDDungeonLayout Layout;
	FString Error;
	if (!TestTrue(TEXT("정상 생성"), FTDDungeonGenerator::GenerateAndValidate(*Theme, *Template, ETDDungeonSize::Medium, 3, Layout, Error)))
	{
		return false;
	}
	const FTDPlacedRoom* Boss = Layout.FindRoomByRole(ETDRoomRole::Boss);
	if (!TestNotNull(TEXT("보스 방"), Boss))
	{
		return false;
	}
	const FName BossId = Boss->RoomId;
	Layout.Doors.RemoveAll([BossId](const FTDPlacedDoor& Door) { return Door.RoomA == BossId || Door.RoomB == BossId; });
	Layout.Rooms.RemoveAll([BossId](const FTDPlacedRoom& Room) { return Room.RoomId == BossId; });
	FTDDungeonValidator::FSettings Settings;
	Settings.RoomCountRange = TDDungeon::RoomCountRange(ETDDungeonSize::Medium);
	const FTDValidationReport Report = FTDDungeonValidator::Validate(Layout, Settings);
	TestFalse(TEXT("보스 방 제거 후 검증 실패"), Report.bPassed);
	TestTrue(TEXT("필수 방 오류(Error)"), HasValidationItem(Report, TEXT("required_rooms"), ETDValidationSeverity::Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonShortMainPathTest, "TDGame.WorldGen.Dungeon.ValidatorShortMainPathIsWarning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonShortMainPathTest::RunTest(const FString& Parameters)
{
	const FTDDungeonLayout Layout = MakeSideBranchLayout();
	FTDDungeonValidator::FSettings Settings = MakeSideBranchSettings();
	const FTDValidationReport Report = FTDDungeonValidator::Validate(Layout, Settings);
	TestTrue(TEXT("주경로 비율 3/6 < 0.6 경고(Warning)"), HasValidationItem(Report, TEXT("main_path_ratio"), ETDValidationSeverity::Warning));
	TestFalse(TEXT("주경로 비율은 오류(Error) 아님"), HasValidationItem(Report, TEXT("main_path_ratio"), ETDValidationSeverity::Error));
	TestTrue(TEXT("경고만 있으면 통과"), Report.bPassed);
	const FName MainPathCode(TEXT("main_path_ratio"));
	for (const TCHAR* PathRoomId : {TEXT("r0"), TEXT("r1"), TEXT("r2")})
	{
		const FName RoomId(PathRoomId);
		TestTrue(FString::Printf(TEXT("경로 방 %s 첨부"), PathRoomId), Report.Items.ContainsByPredicate([&MainPathCode, &RoomId](const FTDValidationItem& Item) { return Item.Code == MainPathCode && Item.RelatedId == RoomId && Item.Severity == ETDValidationSeverity::Warning; }));
	}
	Settings.MinMainPathRatio = 0.5f;
	const FTDValidationReport Relaxed = FTDDungeonValidator::Validate(Layout, Settings);
	TestFalse(TEXT("기준 0.5면 경고 없음"), HasValidationItem(Relaxed, TEXT("main_path_ratio"), ETDValidationSeverity::Warning));
	TestTrue(TEXT("경고가 점수를 깎음"), Report.Score < Relaxed.Score);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonDeadEndSoftTest, "TDGame.WorldGen.Dungeon.ValidatorDeadEndExcessIsWarningNotError", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonDeadEndSoftTest::RunTest(const FString& Parameters)
{
	const FTDDungeonLayout Layout = MakeSideBranchLayout();
	FTDDungeonValidator::FSettings Settings = MakeSideBranchSettings();
	Settings.MinMainPathRatio = 0.5f;
	Settings.MaxDeadEndRatio = 0.2f;
	const FTDValidationReport Report = FTDDungeonValidator::Validate(Layout, Settings);
	TestTrue(TEXT("막다른 길 2/6 > 0.2 경고(Warning)"), HasValidationItem(Report, TEXT("deadend_ratio"), ETDValidationSeverity::Warning));
	TestFalse(TEXT("막다른 길은 오류(Error) 아님"), HasValidationItem(Report, TEXT("deadend_ratio"), ETDValidationSeverity::Error));
	TestTrue(TEXT("경고만 있으면 통과"), Report.bPassed);
	TestTrue(TEXT("경고 감점으로 100점 미만"), Report.Score < 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonValidationLocationTest, "TDGame.WorldGen.Dungeon.ValidatorItemsCarryWorldLocationAndRoomId", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonValidationLocationTest::RunTest(const FString& Parameters)
{
	UTDDungeonTheme* Theme = MakeCryptTheme();
	UTDDungeonFlowTemplate* Template = MakeFlowTemplate(ETDDungeonFlowKind::Branch);
	FTDDungeonLayout Layout;
	FString Error;
	if (!TestTrue(TEXT("정상 생성"), FTDDungeonGenerator::GenerateAndValidate(*Theme, *Template, ETDDungeonSize::Medium, 11, Layout, Error)))
	{
		return false;
	}
	FTDDungeonValidator::FSettings Settings;
	Settings.RoomCountRange = TDDungeon::RoomCountRange(ETDDungeonSize::Medium);
	Settings.MinMainPathRatio = 1.0f;
	const FVector SlotOriginCm(300000.0, 200000.0, 500.0);
	const FTDValidationReport Local = FTDDungeonValidator::Validate(Layout, Settings);
	const FTDValidationReport World = FTDDungeonValidator::Validate(Layout, Settings, SlotOriginCm);
	if (!TestEqual(TEXT("항목 수 동일"), World.Items.Num(), Local.Items.Num()))
	{
		return false;
	}
	for (int32 Index = 0; Index < World.Items.Num(); ++Index)
	{
		const FTDValidationItem& Item = World.Items[Index];
		TestFalse(FString::Printf(TEXT("%s 항목 %d 방 ID"), *Item.Code.ToString(), Index), Item.RelatedId.IsNone());
		TestTrue(FString::Printf(TEXT("%s 항목 %d 위치 = 슬롯 원점 + 로컬"), *Item.Code.ToString(), Index), Item.WorldLocation.Equals(Local.Items[Index].WorldLocation + SlotOriginCm));
	}
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonThemeRequiredRoleTest, "TDGame.WorldGen.Dungeon.ThemeMissingRequiredRoleIsInvalid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonThemeRequiredRoleTest::RunTest(const FString& Parameters)
{
	UTDDungeonTheme* Theme = MakeCryptTheme();
	FDataValidationContext CompleteContext;
	TestTrue(TEXT("플레이스홀더 테마는 Valid"), Theme->IsDataValid(CompleteContext) == EDataValidationResult::Valid);
	const int32 RemovedCount = Theme->Modules.RemoveAll([](const FTDRoomModuleDefinition& Module) { return Module.HasRole(ETDRoomRole::Boss); });
	TestTrue(TEXT("보스 역할 모듈 제거"), RemovedCount > 0);
	FDataValidationContext MissingBossContext;
	TestTrue(TEXT("보스 역할 모듈이 없으면 Invalid"), Theme->IsDataValid(MissingBossContext) == EDataValidationResult::Invalid);
	return true;
}
#endif

#endif
