#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidDebugDrawSubsystem.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidDebugDrawTestPrivate
{
	const TCHAR* const DebugDrawVariableName = TEXT("TD.FluidFX.Debug");
	constexpr int32 DebugDrawAllFlags = 31;
	constexpr int32 DebugDrawActorCount = 2;
	constexpr int32 DebugDrawResolution = 32;
	constexpr double DebugDrawSpacingCm = 3000.0;
	constexpr float DebugDrawTickSeconds = 1.0f / 60.0f;

	struct FTDFluidDebugDrawTestWorld
	{
		FTDFluidDebugDrawTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidDebugDrawTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidDebugDrawTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ATDFluidLiveActor* SpawnCpuActor(const FVector& Location) const
		{
			const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
			ATDFluidLiveActor* LiveActor = World->SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
			if (!LiveActor || !LiveActor->LiveComponent)
			{
				return nullptr;
			}
			LiveActor->LiveComponent->SimulationConfig.GridResolution = DebugDrawResolution;
			LiveActor->LiveComponent->SimulationConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
			LiveActor->FinishSpawning(SpawnTransform);
			return LiveActor;
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidDebugDrawSnapshotTest,
	"TDGame.FluidFX.DebugDraw.SnapshotListsActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidDebugDrawSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidDebugDrawTestPrivate;

	IConsoleVariable* DebugVariable = IConsoleManager::Get().FindConsoleVariable(DebugDrawVariableName);
	if (!TestNotNull(TEXT("Debug console variable exists"), DebugVariable))
	{
		return false;
	}
	const FString OriginalFlagsText = DebugVariable->GetString();
	ON_SCOPE_EXIT
	{
		DebugVariable->Set(*OriginalFlagsText, ECVF_SetByCode);
	};

	FTDFluidDebugDrawTestWorld TestWorld;
	UTDFluidDebugDrawSubsystem* DebugSubsystem = TestWorld.World->GetSubsystem<UTDFluidDebugDrawSubsystem>();
	if (!TestNotNull(TEXT("Debug draw subsystem exists in a game world"), DebugSubsystem))
	{
		return false;
	}
	for (int32 ActorIndex = 0; ActorIndex < DebugDrawActorCount; ++ActorIndex)
	{
		TestNotNull(TEXT("CPU live actor spawns"), TestWorld.SpawnCpuActor(FVector(DebugDrawSpacingCm * ActorIndex, 0.0, 0.0)));
	}

	DebugVariable->Set(0, ECVF_SetByCode);
	TestFalse(TEXT("Debug draw does not tick while the flags are 0"), DebugSubsystem->IsTickable());
	DebugVariable->Set(DebugDrawAllFlags, ECVF_SetByCode);
	TestTrue(TEXT("Debug draw ticks while a flag is set"), DebugSubsystem->IsTickable());
	DebugSubsystem->Tick(DebugDrawTickSeconds);

	TArray<TSharedPtr<FJsonValue>> Entries;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DebugSubsystem->BuildDebugSnapshotJson());
	if (!TestTrue(TEXT("Snapshot is a JSON array"), FJsonSerializer::Deserialize(Reader, Entries)))
	{
		return false;
	}
	TestEqual(TEXT("Snapshot lists each live actor"), Entries.Num(), DebugDrawActorCount);
	const TCHAR* const RequiredKeys[] = {
		TEXT("label"), TEXT("kind"), TEXT("backend"), TEXT("resolution"), TEXT("awake"), TEXT("generation"), TEXT("center_cm"), TEXT("extent_cm"),
		TEXT("combat_bindings"), TEXT("stamps_last"), TEXT("stamps_max"), TEXT("mass_pending"), TEXT("mass_uploaded"), TEXT("mass_saturated"), TEXT("step_rate_hz")};
	for (const TSharedPtr<FJsonValue>& Entry : Entries)
	{
		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!TestTrue(TEXT("Snapshot entry is an object"), Entry.IsValid() && Entry->TryGetObject(EntryObject)))
		{
			continue;
		}
		for (const TCHAR* Key : RequiredKeys)
		{
			TestTrue(FString::Printf(TEXT("Snapshot entry has %s"), Key), (*EntryObject)->HasField(Key));
		}
		TestEqual(TEXT("Snapshot entry reports the CPU backend"), (*EntryObject)->GetStringField(TEXT("backend")), FString(TEXT("CPU")));
	}

	DebugVariable->Set(0, ECVF_SetByCode);
	TArray<TSharedPtr<FJsonValue>> EntriesWithoutFlags;
	const TSharedRef<TJsonReader<>> ReaderWithoutFlags = TJsonReaderFactory<>::Create(DebugSubsystem->BuildDebugSnapshotJson());
	FJsonSerializer::Deserialize(ReaderWithoutFlags, EntriesWithoutFlags);
	TestEqual(TEXT("Snapshot does not depend on the flags"), EntriesWithoutFlags.Num(), DebugDrawActorCount);
	return true;
}

#endif
