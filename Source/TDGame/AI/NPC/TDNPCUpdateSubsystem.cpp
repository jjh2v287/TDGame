#include "AI/NPC/TDNPCUpdateSubsystem.h"
#include "AI/NPC/TDNPCUpdatable.h"
#include "AI/NPC/TDSignificanceComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "SignificanceManager.h"

namespace TDNPCUpdateCVars
{
	static TAutoConsoleVariable<int32> CVarMode(
		TEXT("td.NPCUpdate.Mode"),
		0,
		TEXT("NPC update mode. 0 = Unbudgeted manual updates, 1 = Budgeted manual updates."));

	static TAutoConsoleVariable<int32> CVarHighPriorityCount(
		TEXT("td.NPCUpdate.HighPriorityCount"),
		10,
		TEXT("Number of nearest NPCs updated every frame in budgeted mode."));

	static TAutoConsoleVariable<float> CVarFrameBudgetMs(
		TEXT("td.NPCUpdate.FrameBudgetMs"),
		2.0f,
		TEXT("Per-frame time budget in milliseconds for low-priority NPC updates."));

	static TAutoConsoleVariable<float> CVarRebalanceInterval(
		TEXT("td.NPCUpdate.RebalanceInterval"),
		0.5f,
		TEXT("Seconds between priority group rebuilds."));

	static TAutoConsoleVariable<int32> CVarDebug(
		TEXT("td.NPCUpdate.Debug"),
		0,
		TEXT("Draw NPC update scheduler debug overlay. 0 = Off, 1 = On."));
}

void UTDNPCUpdateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DebugMessageKey = reinterpret_cast<uint64>(this);
}

void UTDNPCUpdateSubsystem::Deinitialize()
{
	for (const TSharedPtr<FTDManagedNPCEntry>& Entry : ManagedActors)
	{
		if (!Entry.IsValid())
		{
			continue;
		}

		if (ITDNPCUpdatable* Updatable = Cast<ITDNPCUpdatable>(Entry->OwnerActor.Get()))
		{
			Updatable->SetManagedByNPCUpdateSubsystem(false);
		}
	}

	ManagedActors.Reset();
	HighPriorityActors.Reset();
	LowPriorityActors.Reset();

	Super::Deinitialize();
}

TStatId UTDNPCUpdateSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDNPCUpdateSubsystem, STATGROUP_Tickables);
}

void UTDNPCUpdateSubsystem::Register(AActor* InActor)
{
	if (!IsValid(InActor) || !InActor->Implements<UTDNPCUpdatable>())
	{
		return;
	}

	for (const TSharedPtr<FTDManagedNPCEntry>& Entry : ManagedActors)
	{
		if (Entry.IsValid() && Entry->OwnerActor.Get() == InActor)
		{
			return;
		}
	}

	TSharedPtr<FTDManagedNPCEntry> NewEntry = MakeShared<FTDManagedNPCEntry>(InActor);
	NewEntry->AccumulatedTime = -FMath::FRandRange(0.f, 0.03f);
	ManagedActors.Add(NewEntry);
	InActor->OnDestroyed.AddDynamic(this, &ThisClass::OnActorDestroyed);

	if (ITDNPCUpdatable* Updatable = Cast<ITDNPCUpdatable>(InActor))
	{
		Updatable->SetManagedByNPCUpdateSubsystem(true);
	}
}

void UTDNPCUpdateSubsystem::Unregister(AActor* InActor)
{
	if (!InActor)
	{
		return;
	}

	const int32 FoundIndex = ManagedActors.IndexOfByPredicate(
		[InActor](const TSharedPtr<FTDManagedNPCEntry>& Entry)
		{
			return Entry.IsValid() && Entry->OwnerActor.Get() == InActor;
		});

	if (FoundIndex == INDEX_NONE)
	{
		return;
	}

	if (ITDNPCUpdatable* Updatable = Cast<ITDNPCUpdatable>(InActor))
	{
		Updatable->SetManagedByNPCUpdateSubsystem(false);
	}

	ManagedActors.RemoveAtSwap(FoundIndex);
	RebuildPriorityGroups();
}

void UTDNPCUpdateSubsystem::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TD_NPCUpdateSubsystem);

	CleanupInvalidEntries();
	if (ManagedActors.IsEmpty())
	{
		return;
	}

	UpdateSignificanceViewPoint();

	for (const TSharedPtr<FTDManagedNPCEntry>& Entry : ManagedActors)
	{
		if (Entry.IsValid())
		{
			Entry->AccumulatedTime += DeltaTime;
		}
	}

	RebalanceTimer += DeltaTime;
	if (RebalanceTimer >= TDNPCUpdateCVars::CVarRebalanceInterval.GetValueOnGameThread())
	{
		RebuildPriorityGroups();
		RebalanceTimer = 0.f;
	}

	const double TickStartTime = FPlatformTime::Seconds();
	if (GetUpdateMode() == ETDNPCUpdateMode::Unbudgeted)
	{
		TickAllActors();
	}
	else
	{
		TickBudgetedActors(TickStartTime);
	}

	DrawDebugOverlay((FPlatformTime::Seconds() - TickStartTime) * 1000.0);
}

void UTDNPCUpdateSubsystem::UpdateSignificanceViewPoint() const
{
	UWorld* World = GetWorld();
	USignificanceManager* SignificanceManager = World ? USignificanceManager::Get(World) : nullptr;
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!SignificanceManager || !PlayerController)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	TArray<FTransform> ViewPoints;
	ViewPoints.Emplace(ViewRotation, ViewLocation, FVector::OneVector);
	SignificanceManager->Update(ViewPoints);
}

void UTDNPCUpdateSubsystem::TickAllActors()
{
	for (const TSharedPtr<FTDManagedNPCEntry>& Entry : ManagedActors)
	{
		if (Entry.IsValid())
		{
			TickEntry(*Entry);
		}
	}
}

void UTDNPCUpdateSubsystem::TickBudgetedActors(const double TickStartTime)
{
	for (FTDManagedNPCEntry* Entry : HighPriorityActors)
	{
		if (Entry)
		{
			TickEntry(*Entry);
		}
	}

	const double FrameBudgetSeconds = TDNPCUpdateCVars::CVarFrameBudgetMs.GetValueOnGameThread() / 1000.0;
	const int32 NumLowPriorityActors = LowPriorityActors.Num();
	int32 ProcessedCount = 0;

	while (NumLowPriorityActors > 0 && ProcessedCount < NumLowPriorityActors)
	{
		if ((FPlatformTime::Seconds() - TickStartTime) > FrameBudgetSeconds)
		{
			break;
		}

		LowPriorityCursor = (LowPriorityCursor + 1) % NumLowPriorityActors;
		FTDManagedNPCEntry* Entry = LowPriorityActors[LowPriorityCursor];
		ProcessedCount++;

		if (Entry)
		{
			TickEntry(*Entry);
		}
	}
}

void UTDNPCUpdateSubsystem::TickEntry(FTDManagedNPCEntry& Entry) const
{
	AActor* Actor = Entry.OwnerActor.Get();
	if (!IsValid(Actor) || Entry.AccumulatedTime <= 0.f)
	{
		return;
	}

	TickActor(Actor, Entry.AccumulatedTime);
	Entry.AccumulatedTime = 0.f;
}

void UTDNPCUpdateSubsystem::CleanupInvalidEntries()
{
	const int32 RemovedCount = ManagedActors.RemoveAll(
		[](const TSharedPtr<FTDManagedNPCEntry>& Entry)
		{
			return !Entry.IsValid() || !IsValid(Entry->OwnerActor.Get());
		});

	if (RemovedCount > 0)
	{
		RebuildPriorityGroups();
	}
}

void UTDNPCUpdateSubsystem::RebuildPriorityGroups()
{
	HighPriorityActors.Reset();
	LowPriorityActors.Reset();

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn))
	{
		for (const TSharedPtr<FTDManagedNPCEntry>& Entry : ManagedActors)
		{
			if (Entry.IsValid() && IsValid(Entry->OwnerActor.Get()))
			{
				HighPriorityActors.Add(Entry.Get());
			}
		}
		return;
	}

	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	TArray<FTDManagedNPCEntry*> SortedActors;
	SortedActors.Reserve(ManagedActors.Num());

	for (const TSharedPtr<FTDManagedNPCEntry>& Entry : ManagedActors)
	{
		if (!Entry.IsValid())
		{
			continue;
		}

		AActor* Actor = Entry->OwnerActor.Get();
		if (!IsValid(Actor))
		{
			continue;
		}

		Entry->Distance = FVector::Distance(PlayerLocation, Actor->GetActorLocation());
		const UTDSignificanceComponent* SignificanceComponent = Actor->FindComponentByClass<UTDSignificanceComponent>();
		Entry->Significance = SignificanceComponent ? SignificanceComponent->GetCurrentSignificance() : Entry->Distance;
		SortedActors.Add(Entry.Get());
	}

	SortedActors.Sort(
		[](const FTDManagedNPCEntry& A, const FTDManagedNPCEntry& B)
		{
			if (!FMath::IsNearlyEqual(A.Significance, B.Significance))
			{
				return A.Significance < B.Significance;
			}

			return A.Distance < B.Distance;
		});

	const int32 HighPriorityCount = FMath::Max(0, TDNPCUpdateCVars::CVarHighPriorityCount.GetValueOnGameThread());
	for (int32 Index = 0; Index < SortedActors.Num(); ++Index)
	{
		if (Index < HighPriorityCount)
		{
			HighPriorityActors.Add(SortedActors[Index]);
		}
		else
		{
			LowPriorityActors.Add(SortedActors[Index]);
		}
	}

	LowPriorityCursor = LowPriorityActors.IsEmpty() ? 0 : LowPriorityCursor % LowPriorityActors.Num();
}

void UTDNPCUpdateSubsystem::TickActor(AActor* Actor, float DeltaTime) const
{
	if (!IsValid(Actor))
	{
		return;
	}

	ITDNPCUpdatable* Updatable = Cast<ITDNPCUpdatable>(Actor);
	if (!Updatable)
	{
		return;
	}

	Updatable->ManualUpdateMovement(DeltaTime);
	Updatable->ManualUpdateAnimation(DeltaTime);
}

ETDNPCUpdateMode UTDNPCUpdateSubsystem::GetUpdateMode() const
{
	return TDNPCUpdateCVars::CVarMode.GetValueOnGameThread() == 0
		? ETDNPCUpdateMode::Unbudgeted
		: ETDNPCUpdateMode::Budgeted;
}

void UTDNPCUpdateSubsystem::DrawDebugOverlay(const double TotalTickTimeMs) const
{
	if (TDNPCUpdateCVars::CVarDebug.GetValueOnGameThread() <= 0 || !GEngine)
	{
		return;
	}

	const TCHAR* ModeText = GetUpdateMode() == ETDNPCUpdateMode::Budgeted ? TEXT("Budgeted") : TEXT("Unbudgeted");
	const FString Message = FString::Printf(
		TEXT("NPC Update | Mode: %s | Registered: %d | High: %d | Low: %d | Tick: %.2f ms | Budget: %.2f ms | Sort: Significance+Distance"),
		ModeText,
		ManagedActors.Num(),
		HighPriorityActors.Num(),
		LowPriorityActors.Num(),
		TotalTickTimeMs,
		TDNPCUpdateCVars::CVarFrameBudgetMs.GetValueOnGameThread());

	GEngine->AddOnScreenDebugMessage(DebugMessageKey, 0.f, FColor::Cyan, Message);
}

void UTDNPCUpdateSubsystem::OnActorDestroyed(AActor* DestroyedActor)
{
	Unregister(DestroyedActor);
}
