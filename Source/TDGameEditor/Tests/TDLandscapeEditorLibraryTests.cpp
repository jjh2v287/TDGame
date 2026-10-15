#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "Landscape/TDLandscapeEditorLibrary.h"
#include "LandscapeStreamingProxy.h"
#include "Misc/AutomationTest.h"

namespace
{
	const FName TDLandscapeTestRealmTag(TEXT("TDRealm"));
	const FName TDLandscapeTestStarfieldTag(TEXT("TDRealm:SunkenStarfield"));
	const FName TDLandscapeTestPaleHeightsTag(TEXT("TDRealm:PaleHeights"));
	const FVector TDLandscapeTestFieldParentCm(-50400.0, -50400.0, 0.0);
	const FVector TDLandscapeTestFieldProxyCm(-44100.0, -44100.0, 0.0);
	const FVector TDLandscapeTestPaleHeightsParentCm(50400.0, 20000.0, 1800.0);
	const FVector TDLandscapeTestStarfieldParentCm(-31500.0, -100800.0, -3400.0);
	const FVector TDLandscapeTestStarfieldProxyCm(-25200.0, -56700.0, -3400.0);

	struct FTDLandscapeTagTestWorld
	{
		FTDLandscapeTagTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDLandscapeTagTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Editor, false, WorldName, GetTransientPackage());
			if (World)
			{
				GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
			}
		}

		~FTDLandscapeTagTestWorld()
		{
			if (!World)
			{
				return;
			}
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		template <typename TLandscapeActor>
		TLandscapeActor* SpawnComponentlessLandscape(const FVector& LocationCm, const FGuid& LandscapeGuid, const TArray<FName>& Tags) const
		{
			const FTransform SpawnTransform(FRotator::ZeroRotator, LocationCm);
			TLandscapeActor* Landscape = World->SpawnActorDeferred<TLandscapeActor>(TLandscapeActor::StaticClass(), SpawnTransform);
			if (!Landscape)
			{
				return nullptr;
			}
			Landscape->SetLandscapeGuid(LandscapeGuid);
			Landscape->Tags = Tags;
			Landscape->FinishSpawning(SpawnTransform);
			return Landscape;
		}

		UWorld* World = nullptr;
	};

	bool IsTDLandscapeTestActorAlive(const AActor* Actor)
	{
		return IsValid(Actor) && !Actor->IsActorBeingDestroyed();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDestroyLandscapeWithTagTest, "TDGame.WorldGen.Realm.DestroyLandscapeWithTag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDDestroyLandscapeWithTagTest::RunTest(const FString& Parameters)
{
	FTDLandscapeTagTestWorld TestWorld;
	if (!TestNotNull(TEXT("Transient editor world is created"), TestWorld.World))
	{
		return false;
	}

	const FGuid FieldGuid = FGuid::NewGuid();
	const FGuid StarfieldGuid = FGuid::NewGuid();
	ALandscape* FieldParent = TestWorld.SpawnComponentlessLandscape<ALandscape>(TDLandscapeTestFieldParentCm, FieldGuid, {});
	ALandscapeStreamingProxy* FieldProxy = TestWorld.SpawnComponentlessLandscape<ALandscapeStreamingProxy>(TDLandscapeTestFieldProxyCm, FieldGuid, {});
	ALandscape* PaleHeightsParent = TestWorld.SpawnComponentlessLandscape<ALandscape>(TDLandscapeTestPaleHeightsParentCm, FGuid::NewGuid(), { TDLandscapeTestRealmTag, TDLandscapeTestPaleHeightsTag });
	ALandscape* StarfieldParent = TestWorld.SpawnComponentlessLandscape<ALandscape>(TDLandscapeTestStarfieldParentCm, StarfieldGuid, { TDLandscapeTestRealmTag, TDLandscapeTestStarfieldTag });
	ALandscapeStreamingProxy* StarfieldProxy = TestWorld.SpawnComponentlessLandscape<ALandscapeStreamingProxy>(TDLandscapeTestStarfieldProxyCm, StarfieldGuid, { TDLandscapeTestRealmTag, TDLandscapeTestStarfieldTag });
	if (!TestNotNull(TEXT("Field parent spawns"), FieldParent)
		|| !TestNotNull(TEXT("Field streaming proxy spawns"), FieldProxy)
		|| !TestNotNull(TEXT("Pale heights parent spawns"), PaleHeightsParent)
		|| !TestNotNull(TEXT("Starfield parent spawns"), StarfieldParent)
		|| !TestNotNull(TEXT("Starfield streaming proxy spawns"), StarfieldProxy))
	{
		return false;
	}

	const TArray<ALandscapeProxy*> StarfieldActors = UTDLandscapeEditorLibrary::GetLandscapeProxiesOf(StarfieldParent);
	TestEqual(TEXT("Starfield landscape returns itself and its proxy"), StarfieldActors.Num(), 2);
	TestTrue(TEXT("Starfield landscape list starts with the parent"), StarfieldActors.Num() > 0 && StarfieldActors[0] == StarfieldParent);
	TestTrue(TEXT("Starfield landscape list holds its streaming proxy"), StarfieldActors.Contains(StarfieldProxy));
	TestFalse(TEXT("Starfield landscape list skips other landscapes"), StarfieldActors.Contains(PaleHeightsParent) || StarfieldActors.Contains(FieldProxy));
	TestEqual(TEXT("A null landscape has no proxies"), UTDLandscapeEditorLibrary::GetLandscapeProxiesOf(nullptr).Num(), 0);

	AddExpectedMessagePlain(TEXT("needs a world and a tag"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestEqual(TEXT("A none tag destroys nothing"), UTDLandscapeEditorLibrary::DestroyLandscapeActorsWithoutTag(TestWorld.World, NAME_None), 0);
	const int32 FieldDestroyedCount = UTDLandscapeEditorLibrary::DestroyLandscapeActorsWithoutTag(TestWorld.World, TDLandscapeTestRealmTag);
	TestEqual(TEXT("Field rebake destroys the untagged parent and proxy"), FieldDestroyedCount, 2);
	TestFalse(TEXT("Field parent is destroyed"), IsTDLandscapeTestActorAlive(FieldParent));
	TestFalse(TEXT("Field proxy is destroyed"), IsTDLandscapeTestActorAlive(FieldProxy));
	TestTrue(TEXT("Pale heights parent at (50400, 20000) survives the field rebake"), IsTDLandscapeTestActorAlive(PaleHeightsParent));
	TestTrue(TEXT("Starfield parent survives the field rebake"), IsTDLandscapeTestActorAlive(StarfieldParent));
	TestTrue(TEXT("Starfield proxy centred at y -56700 survives the field rebake"), IsTDLandscapeTestActorAlive(StarfieldProxy));

	const int32 StarfieldDestroyedCount = UTDLandscapeEditorLibrary::DestroyLandscapeActorsWithTag(TestWorld.World, TDLandscapeTestStarfieldTag);
	TestEqual(TEXT("Starfield rebake destroys its parent and proxy"), StarfieldDestroyedCount, 2);
	TestFalse(TEXT("Starfield parent is destroyed by its tag"), IsTDLandscapeTestActorAlive(StarfieldParent));
	TestFalse(TEXT("Starfield proxy is destroyed by its tag"), IsTDLandscapeTestActorAlive(StarfieldProxy));
	TestTrue(TEXT("Pale heights parent survives the starfield rebake"), IsTDLandscapeTestActorAlive(PaleHeightsParent));
	TestEqual(TEXT("A none tag destroys nothing with a tag filter"), UTDLandscapeEditorLibrary::DestroyLandscapeActorsWithTag(TestWorld.World, NAME_None), 0);
	return true;
}

#endif
