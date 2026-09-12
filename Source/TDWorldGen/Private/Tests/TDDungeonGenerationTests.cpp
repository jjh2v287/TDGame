#if WITH_DEV_AUTOMATION_TESTS

#include "Dungeon/TDDungeonGeneration.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonAllFlowsSizesSeedsTest, "TDGame.WorldGen.Dungeon.AllFlowsSizesSeeds1To100Pass", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonAllFlowsSizesSeedsTest::RunTest(const FString& Parameters)
{
	UTDDungeonTheme* Theme = MakeCryptTheme();
	int32 Total = 0;
	int32 Failures = 0;
	int32 MaxRestarts = 0;
	for (const ETDDungeonFlowKind Flow : AllFlows)
	{
		UTDDungeonFlowTemplate* Template = MakeFlowTemplate(Flow);
		for (const ETDDungeonSize Size : AllSizes)
		{
			for (int32 Seed = 1; Seed <= 100; ++Seed)
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
				AddError(FString::Printf(TEXT("%s %s seed=%d: %s"), TDDungeon::FlowName(Flow), TDDungeon::SizeName(Size), Seed, *Error));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("생성·검증 %d건 중 통과 %d건 (%.1f%%), 최대 재시작 %d회"), Total, Total - Failures, 100.0f * (Total - Failures) / Total, MaxRestarts));
	TestEqual(TEXT("실패 건수"), Failures, 0);
	return Failures == 0;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDungeonKeyLockOrderTest, "TDGame.WorldGen.Dungeon.KeyLockSeeds1To200KeyBeforeLock", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTDDungeonKeyLockOrderTest::RunTest(const FString& Parameters)
{
	UTDDungeonTheme* Theme = MakeCryptTheme();
	UTDDungeonFlowTemplate* Template = MakeFlowTemplate(ETDDungeonFlowKind::KeyLock);
	int32 Violations = 0;
	for (int32 Seed = 1; Seed <= 200; ++Seed)
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

#endif
