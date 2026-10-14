#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "Landscape/TDLandscapeEditorLibrary.h"
#include "Misc/AutomationTest.h"

namespace
{
	const FVector TDLandscapeTestFieldParentCm(-50400.0, -50400.0, 0.0);
	const FVector TDLandscapeTestFieldMinCm(-60000.0, -60000.0, -1000000.0);
	const FVector TDLandscapeTestFieldMaxCm(60000.0, 60000.0, 1000000.0);
	const FVector TDLandscapeTestRealmCenterCm(-302400.0, 302400.0, 0.0);
	const FVector TDLandscapeTestRealmHalfExtentCm(25200.0, 25200.0, 1000000.0);
	const FVector TDLandscapeTestOtherRealmCenterCm(-302400.0, -302400.0, 0.0);

	struct FTDLandscapeBoundsTestWorld
	{
		FTDLandscapeBoundsTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDLandscapeBoundsTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Editor, false, WorldName, GetTransientPackage());
			if (World)
			{
				GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
			}
		}

		~FTDLandscapeBoundsTestWorld()
		{
			if (!World)
			{
				return;
			}
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ALandscape* SpawnComponentlessLandscape(const FVector& LocationCm) const
		{
			const FTransform SpawnTransform(FRotator::ZeroRotator, LocationCm);
			ALandscape* Landscape = World->SpawnActorDeferred<ALandscape>(ALandscape::StaticClass(), SpawnTransform);
			if (!Landscape)
			{
				return nullptr;
			}
			Landscape->SetLandscapeGuid(FGuid::NewGuid());
			Landscape->FinishSpawning(SpawnTransform);
			return Landscape;
		}

		ALandscape* SpawnParentLandscapeAtMinCorner(const FVector& CenterCm) const
		{
			return SpawnComponentlessLandscape(CenterCm - FVector(TDLandscapeTestRealmHalfExtentCm.X, TDLandscapeTestRealmHalfExtentCm.Y, 0.0));
		}

		UWorld* World = nullptr;
	};

	bool IsLandscapeAlive(const ALandscape* Landscape)
	{
		return IsValid(Landscape) && !Landscape->IsActorBeingDestroyed();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDestroyLandscapeInBoundsParentLocationTest, "TDGame.WorldGen.Realm.DestroyLandscapeInBoundsUsesParentLocation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDDestroyLandscapeInBoundsParentLocationTest::RunTest(const FString& Parameters)
{
	FTDLandscapeBoundsTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient editor world is created"), TestWorld.World))
	{
		return false;
	}

	ALandscape* FieldParent = TestWorld.SpawnComponentlessLandscape(TDLandscapeTestFieldParentCm);
	ALandscape* RealmParent = TestWorld.SpawnParentLandscapeAtMinCorner(TDLandscapeTestRealmCenterCm);
	ALandscape* OtherRealmParent = TestWorld.SpawnParentLandscapeAtMinCorner(TDLandscapeTestOtherRealmCenterCm);
	if (!TestNotNull(TEXT("Field parent landscape spawns"), FieldParent)
		|| !TestNotNull(TEXT("Realm parent landscape spawns"), RealmParent)
		|| !TestNotNull(TEXT("Other realm parent landscape spawns"), OtherRealmParent))
	{
		return false;
	}
	TestFalse(TEXT("Spawned parent landscape has no registered component bounds"), RealmParent->GetProxyBounds().IsValid != 0);

	const int32 FieldDestroyedCount = UTDLandscapeEditorLibrary::DestroyLandscapeActorsInBounds(TestWorld.World, TDLandscapeTestFieldMinCm, TDLandscapeTestFieldMaxCm);
	TestEqual(TEXT("Field box destroys only the field parent"), FieldDestroyedCount, 1);
	TestFalse(TEXT("Field parent is destroyed by the field box"), IsLandscapeAlive(FieldParent));
	TestTrue(TEXT("Realm parent at the realm min corner survives the field box"), IsLandscapeAlive(RealmParent));
	TestTrue(TEXT("Other realm parent survives the field box"), IsLandscapeAlive(OtherRealmParent));

	const int32 RealmDestroyedCount = UTDLandscapeEditorLibrary::DestroyLandscapeActorsInBounds(TestWorld.World,
		TDLandscapeTestRealmCenterCm - TDLandscapeTestRealmHalfExtentCm, TDLandscapeTestRealmCenterCm + TDLandscapeTestRealmHalfExtentCm);
	TestEqual(TEXT("Realm box destroys only its own parent"), RealmDestroyedCount, 1);
	TestFalse(TEXT("Realm parent on the closed min edge is destroyed by its realm box"), IsLandscapeAlive(RealmParent));
	TestTrue(TEXT("Other realm parent survives a different realm box"), IsLandscapeAlive(OtherRealmParent));
	return true;
}

#endif
