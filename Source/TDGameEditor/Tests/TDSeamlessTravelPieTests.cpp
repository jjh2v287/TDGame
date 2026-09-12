#if WITH_DEV_AUTOMATION_TESTS

#include "Dungeon/TDDungeonDefinitions.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "TDWorldGenSettings.h"
#include "Tests/TDPieWaitUntilCommand.h"
#include "Tests/TDTravelStateRecorder.h"
#include "World/Generation/TDInstancedMeshActor.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"

namespace
{
	const FName TDPieTestDungeonId(TEXT("MainCrypt"));
	constexpr double TDDungeonRegionMinX = 250000.0;
	constexpr double TDFieldRegionMaxX = 100000.0;
	constexpr float TDPieWarmupSeconds = 10.0f;
	constexpr double TDTravelTimeoutSeconds = 60.0;
	constexpr double TDDungeonUnloadTimeoutSeconds = 30.0;

	struct FTDSeamlessTravelPieTestState
	{
		bool bIsAborted = false;
		TWeakObjectPtr<UTDSeamlessTravelSubsystem> Travel;
		TStrongObjectPtr<UTDTravelStateRecorder> Recorder;
		int32 DungeonMeshActorCountWhileInside = 0;

		bool IsTravelIdle() const
		{
			return !Travel.IsValid() || Travel->GetTravelState() == ETDSeamlessTravelState::Idle;
		}
	};

	FString DescribeDefaultAtlasSlots()
	{
		const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
		const UTDDungeonAtlasDefinition* Atlas = Settings ? Settings->DefaultDungeonAtlas.LoadSynchronous() : nullptr;
		if (!Atlas)
		{
			return TEXT("no default dungeon atlas");
		}

		FString Description = FString::Printf(TEXT("atlas '%s' slots=%d"), *Atlas->GetPathName(), Atlas->Slots.Num());
		for (const FTDDungeonSlot& Slot : Atlas->Slots)
		{
			Description += FString::Printf(TEXT(" | [%d] %s world=%s entry=%s exit=%s fieldReturn=%s validated=%d"),
				Slot.SlotIndex,
				*Slot.DungeonId.ToString(),
				*Slot.WorldTransform.GetLocation().ToCompactString(),
				*Slot.EntryTransform.GetLocation().ToCompactString(),
				*Slot.ExitTransform.GetLocation().ToCompactString(),
				*Slot.FieldReturnTransform.GetLocation().ToCompactString(),
				Slot.bValidationPassed ? 1 : 0);
		}
		return Description;
	}

	bool IsDungeonEntryInsideDungeonRegion(FName DungeonId, FVector& OutEntryLocation)
	{
		const UTDWorldGenSettings* Settings = UTDWorldGenSettings::Get();
		const UTDDungeonAtlasDefinition* Atlas = Settings ? Settings->DefaultDungeonAtlas.LoadSynchronous() : nullptr;
		const FTDDungeonSlot* Slot = Atlas ? Atlas->FindSlotPtr(DungeonId) : nullptr;
		if (!Slot)
		{
			return false;
		}
		OutEntryLocation = Slot->EntryTransform.GetLocation();
		return OutEntryLocation.X > TDDungeonRegionMinX;
	}

	int32 CountInstancedMeshActorsBeyondX(UWorld* World, double MinX)
	{
		int32 Count = 0;
		for (TActorIterator<ATDInstancedMeshActor> It(World); It; ++It)
		{
			if (It->GetActorLocation().X > MinX)
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDSeamlessTravelRoundTripPieTest, "TDGame.SeamlessTravel.RoundTripCompletesWithoutLoadingGap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDSeamlessTravelRoundTripPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	TSharedRef<FTDSeamlessTravelPieTestState> State = MakeShared<FTDSeamlessTravelPieTestState>();
	if (!OpenMapAndStartPie(*this, OpenWorldMapPackagePath))
	{
		return false;
	}

	const auto Abort = [State]() { State->bIsAborted = true; };
	const auto WaitForTravelIdle = [this, State, Abort](const FString& Description)
	{
		EnqueueWaitUntil(*this, Description, [State]() { return State->bIsAborted || State->IsTravelIdle(); }, TDTravelTimeoutSeconds, Abort);
	};

	EnqueueWaitSeconds(TDPieWarmupSeconds);

	EnqueueStep([this, State]()
	{
		UWorld* World = GetPieWorld();
		UTDSeamlessTravelSubsystem* Travel = World ? World->GetSubsystem<UTDSeamlessTravelSubsystem>() : nullptr;
		if (!TestNotNull(TEXT("PIE world has a seamless travel subsystem"), Travel))
		{
			State->bIsAborted = true;
			return true;
		}

		APawn* Pawn = FindPlayerPawn(World);
		if (!TestNotNull(TEXT("PIE world has a player pawn"), Pawn))
		{
			State->bIsAborted = true;
			return true;
		}
		AddInfo(FString::Printf(TEXT("Pawn starts at %s, travel state %d."), *Pawn->GetActorLocation().ToCompactString(), static_cast<int32>(Travel->GetTravelState())));
		AddInfo(DescribeDefaultAtlasSlots());

		FVector EntryLocation = FVector::ZeroVector;
		if (!IsDungeonEntryInsideDungeonRegion(TDPieTestDungeonId, EntryLocation))
		{
			AddError(FString::Printf(TEXT("Atlas slot '%s' EntryTransform %s is not inside the dungeon region (X > %.0f); bake and save the dungeon atlas before running this test."), *TDPieTestDungeonId.ToString(), *EntryLocation.ToCompactString(), TDDungeonRegionMinX));
			State->bIsAborted = true;
			return true;
		}

		State->Travel = Travel;
		State->Recorder.Reset(NewObject<UTDTravelStateRecorder>(GetTransientPackage()));
		State->Recorder->BindTo(Travel);

		if (!TestTrue(TEXT("RequestTravel to dungeon entry is accepted"), Travel->RequestTravel(TDPieTestDungeonId, true)))
		{
			State->bIsAborted = true;
			return true;
		}
		TestTrue(TEXT("Travel leaves Idle immediately after request"), Travel->GetTravelState() != ETDSeamlessTravelState::Idle);
		return true;
	});

	WaitForTravelIdle(TEXT("Travel to dungeon entry returns to Idle"));

	EnqueueStep([this, State]()
	{
		if (State->bIsAborted)
		{
			return true;
		}

		UWorld* World = GetPieWorld();
		APawn* Pawn = FindPlayerPawn(World);
		if (!TestNotNull(TEXT("Player pawn exists after entering dungeon"), Pawn) || !State->Travel.IsValid())
		{
			State->bIsAborted = true;
			return true;
		}

		const FVector PawnLocation = Pawn->GetActorLocation();
		AddInfo(FString::Printf(TEXT("Pawn after entry travel: %s. Transitions: %s"), *PawnLocation.ToCompactString(), *State->Recorder->DescribeTransitions()));
		TestTrue(TEXT("Pawn X is inside the dungeon atlas region after entry"), PawnLocation.X > TDDungeonRegionMinX);
		TestEqual(TEXT("Last traveled dungeon id"), State->Travel->GetLastTraveledDungeonId(), TDPieTestDungeonId);
		TestFalse(TEXT("Entry travel never reached Traveling without Preloading"), State->Recorder->HasTravelingWithoutPreloading());
		TestEqual(TEXT("Entry travel produced exactly one Traveling transition"), State->Recorder->CountTransitionsTo(ETDSeamlessTravelState::Traveling), 1);

		State->DungeonMeshActorCountWhileInside = CountInstancedMeshActorsBeyondX(World, TDDungeonRegionMinX);
		AddInfo(FString::Printf(TEXT("Instanced mesh actors in dungeon region while inside: %d"), State->DungeonMeshActorCountWhileInside));

		if (!TestTrue(TEXT("RequestTravel back to field is accepted"), State->Travel->RequestTravel(TDPieTestDungeonId, false)))
		{
			State->bIsAborted = true;
		}
		return true;
	});

	WaitForTravelIdle(TEXT("Travel back to field returns to Idle"));

	EnqueueStep([this, State]()
	{
		if (State->bIsAborted)
		{
			return true;
		}

		UWorld* World = GetPieWorld();
		APawn* Pawn = FindPlayerPawn(World);
		if (!TestNotNull(TEXT("Player pawn exists after returning to field"), Pawn))
		{
			State->bIsAborted = true;
			return true;
		}

		const FVector PawnLocation = Pawn->GetActorLocation();
		AddInfo(FString::Printf(TEXT("Pawn after field return: %s. Transitions: %s"), *PawnLocation.ToCompactString(), *State->Recorder->DescribeTransitions()));
		TestTrue(TEXT("Pawn X is back in the field region after return"), PawnLocation.X < TDFieldRegionMaxX);
		TestFalse(TEXT("Round trip never reached Traveling without Preloading"), State->Recorder->HasTravelingWithoutPreloading());
		TestEqual(TEXT("Round trip produced exactly two Traveling transitions"), State->Recorder->CountTransitionsTo(ETDSeamlessTravelState::Traveling), 2);
		TestEqual(TEXT("Round trip produced two Settling transitions"), State->Recorder->CountTransitionsTo(ETDSeamlessTravelState::Settling), 2);
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Dungeon region instanced mesh actors unload after field return"), [State]()
	{
		if (State->bIsAborted)
		{
			return true;
		}
		UWorld* World = GetPieWorld();
		return World == nullptr || CountInstancedMeshActorsBeyondX(World, TDDungeonRegionMinX) == 0;
	}, TDDungeonUnloadTimeoutSeconds, Abort);

	EnqueueStep([this, State]()
	{
		if (State->Recorder.IsValid())
		{
			State->Recorder->UnbindFrom(State->Travel.Get());
			State->Recorder.Reset();
		}
		return true;
	});

	EnqueueEndPie();
	return true;
}

#endif
