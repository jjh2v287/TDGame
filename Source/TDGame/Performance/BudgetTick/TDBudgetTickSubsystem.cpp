#include "Performance/BudgetTick/TDBudgetTickSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Performance/BudgetTick/TDBudgetTickParticipantComponent.h"
#include "Performance/BudgetTick/TDBudgetTickable.h"

namespace TDBudgetTickCVars
{
	static TAutoConsoleVariable<float> CVarFrameBudgetMs(
		TEXT("td.BudgetTick.FrameBudgetMs"),
		1.5f,
		TEXT("Per-frame budget in milliseconds for non-near budget tick updates."));

	static TAutoConsoleVariable<float> CVarImportanceWeight(
		TEXT("td.BudgetTick.ImportanceWeight"),
		4.0f,
		TEXT("Weight applied to participant importance when sorting budget candidates."));

	static TAutoConsoleVariable<float> CVarDistanceWeight(
		TEXT("td.BudgetTick.DistanceWeight"),
		2.0f,
		TEXT("Weight applied to near-distance preference when sorting budget candidates."));

	static TAutoConsoleVariable<float> CVarNearBonusWeight(
		TEXT("td.BudgetTick.NearBonusWeight"),
		4.0f,
		TEXT("Additional score bonus for actors inside their near-distance range."));

	static TAutoConsoleVariable<float> CVarStarvationWeight(
		TEXT("td.BudgetTick.StarvationWeight"),
		3.0f,
		TEXT("Weight applied to overdue actors when sorting budget candidates."));

	static TAutoConsoleVariable<float> CVarOverdueEscalation(
		TEXT("td.BudgetTick.OverdueEscalation"),
		2.0f,
		TEXT("Extra starvation multiplier applied after MaxUpdateInterval is exceeded."));

	static TAutoConsoleVariable<float> CVarDistanceNormalization(
		TEXT("td.BudgetTick.DistanceNormalization"),
		12000.f,
		TEXT("Distance used to normalize candidate priority scores."));

	static TAutoConsoleVariable<float> CVarMinPredictedCostMs(
		TEXT("td.BudgetTick.MinPredictedCostMs"),
		0.05f,
		TEXT("Minimum predicted cost in milliseconds used by the scheduler."));

	static TAutoConsoleVariable<int32> CVarDebug(
		TEXT("td.BudgetTick.Debug"),
		0,
		TEXT("Budget tick debug. 0 = Off, 1 = Overlay, 2 = Overlay + actor labels."));
}

namespace
{
	ITDBudgetTickable* ResolveBudgetTickable(AActor* Actor)
	{
		return IsValid(Actor) ? Cast<ITDBudgetTickable>(Actor) : nullptr;
	}

	const ITDBudgetTickable* ResolveBudgetTickable(const AActor* Actor)
	{
		return IsValid(Actor) ? Cast<ITDBudgetTickable>(Actor) : nullptr;
	}
}

void UTDBudgetTickSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DebugMessageKey = reinterpret_cast<uint64>(this);
}

void UTDBudgetTickSubsystem::Deinitialize()
{
	for (const TSharedPtr<FTDBudgetTickRecord>& Record : ManagedRecords)
	{
		if (!Record.IsValid())
		{
			continue;
		}

		if (ITDBudgetTickable* Tickable = ResolveBudgetTickable(Record->Actor.Get()))
		{
			Tickable->OnBudgetTickManagedStateChanged(false);
		}
	}

	ManagedRecords.Reset();
	Super::Deinitialize();
}

TStatId UTDBudgetTickSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDBudgetTickSubsystem, STATGROUP_Tickables);
}

bool UTDBudgetTickSubsystem::RegisterParticipant(AActor* Actor, UTDBudgetTickParticipantComponent* Participant)
{
	ITDBudgetTickable* Tickable = ResolveBudgetTickable(Actor);
	if (!Tickable || !IsValid(Participant))
	{
		return false;
	}

	for (const TSharedPtr<FTDBudgetTickRecord>& Record : ManagedRecords)
	{
		if (Record.IsValid() && Record->Actor.Get() == Actor)
		{
			return false;
		}
	}

	TSharedPtr<FTDBudgetTickRecord> NewRecord = MakeShared<FTDBudgetTickRecord>(Actor, Participant);
	NewRecord->LastTickTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	ManagedRecords.Add(NewRecord);

	Tickable->OnBudgetTickManagedStateChanged(true);
	return true;
}

void UTDBudgetTickSubsystem::UnregisterParticipant(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	const int32 FoundIndex = ManagedRecords.IndexOfByPredicate(
		[Actor](const TSharedPtr<FTDBudgetTickRecord>& Record)
		{
			return Record.IsValid() && Record->Actor.Get() == Actor;
		});

	if (FoundIndex == INDEX_NONE)
	{
		return;
	}

	if (ITDBudgetTickable* Tickable = ResolveBudgetTickable(Actor))
	{
		Tickable->OnBudgetTickManagedStateChanged(false);
	}

	ManagedRecords.RemoveAtSwap(FoundIndex);
}

void UTDBudgetTickSubsystem::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TD_BudgetTickSubsystem);

	CleanupInvalidEntries();
	if (ManagedRecords.IsEmpty())
	{
		return;
	}

	const FVector ReferenceLocation = ResolveReferenceLocation();
	const double CurrentTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const float FrameBudgetMs = FMath::Max(0.f, TDBudgetTickCVars::CVarFrameBudgetMs.GetValueOnGameThread());
	const float MinPredictedCostMs = TDBudgetTickCVars::CVarMinPredictedCostMs.GetValueOnGameThread();

	TArray<FPriorityCandidate> Candidates;
	Candidates.Reserve(ManagedRecords.Num());

	int32 NearCount = 0;
	int32 OverdueCount = 0;

	for (const TSharedPtr<FTDBudgetTickRecord>& RecordPtr : ManagedRecords)
	{
		if (!RecordPtr.IsValid())
		{
			continue;
		}

		FTDBudgetTickRecord& Record = *RecordPtr;
		AActor* Actor = Record.Actor.Get();
		UTDBudgetTickParticipantComponent* Participant = Record.Participant.Get();
		if (!IsValid(Actor) || !IsValid(Participant) || !Participant->IsBudgetTickEnabled())
		{
			continue;
		}

		Record.AccumulatedDeltaTime += DeltaTime;

		ITDBudgetTickable* Tickable = ResolveBudgetTickable(Actor);
		if (!Tickable || !Tickable->CanBudgetTick())
		{
			continue;
		}

		const float Distance = FVector::Distance(ReferenceLocation, Actor->GetActorLocation());
		const float TimeSinceLastTick = FMath::Max(0.f, static_cast<float>(CurrentTimeSeconds - Record.LastTickTimeSeconds));
		const float PredictedCostMs = FMath::Max(Record.EstimatedCostMs, MinPredictedCostMs);
		const bool bIsNear = Participant->ShouldAlwaysTickWhenNear() && Distance <= Participant->GetNearDistance();
		const bool bIsOverdue = TimeSinceLastTick >= Participant->GetMaxUpdateInterval();

		Record.LastDistance = Distance;
		Record.LastScore = CalculatePriorityScore(Record, Distance, TimeSinceLastTick);

		FPriorityCandidate Candidate;
		Candidate.Record = &Record;
		Candidate.Distance = Distance;
		Candidate.TimeSinceLastTick = TimeSinceLastTick;
		Candidate.Score = Record.LastScore;
		Candidate.PriorityPerCost = Candidate.Score / PredictedCostMs;
		Candidate.bIsNear = bIsNear;
		Candidate.bIsOverdue = bIsOverdue;

		if (bIsNear)
		{
			++NearCount;
		}

		if (bIsOverdue)
		{
			++OverdueCount;
		}

		if (bIsNear || TimeSinceLastTick >= Participant->GetMinUpdateInterval())
		{
			Candidates.Add(Candidate);
		}
	}

	Candidates.Sort(
		[](const FPriorityCandidate& A, const FPriorityCandidate& B)
		{
			if (A.bIsOverdue != B.bIsOverdue)
			{
				return A.bIsOverdue > B.bIsOverdue;
			}

			if (A.bIsNear != B.bIsNear)
			{
				return A.bIsNear > B.bIsNear;
			}

			if (!FMath::IsNearlyEqual(A.PriorityPerCost, B.PriorityPerCost))
			{
				return A.PriorityPerCost > B.PriorityPerCost;
			}

			if (!FMath::IsNearlyEqual(A.Score, B.Score))
			{
				return A.Score > B.Score;
			}

			return A.Distance < B.Distance;
		});

	double BudgetSpentMs = 0.0;
	int32 BudgetedCount = 0;

	for (const FPriorityCandidate& Candidate : Candidates)
	{
		if (!Candidate.Record)
		{
			continue;
		}

		const float PredictedCostMs = FMath::Max(Candidate.Record->EstimatedCostMs, MinPredictedCostMs);
		if (FrameBudgetMs > 0.f && (BudgetSpentMs + PredictedCostMs) > FrameBudgetMs)
		{
			continue;
		}

		BudgetSpentMs += ExecuteBudgetTick(*Candidate.Record, CurrentTimeSeconds);
		++BudgetedCount;
	}

	DrawDebugOverlay(Candidates.Num(), NearCount, OverdueCount, BudgetedCount, BudgetSpentMs);
	DrawDebugActorLabels();
}

void UTDBudgetTickSubsystem::CleanupInvalidEntries()
{
	ManagedRecords.RemoveAll(
		[](const TSharedPtr<FTDBudgetTickRecord>& Record)
		{
			return !Record.IsValid() || !IsValid(Record->Actor.Get()) || !IsValid(Record->Participant.Get());
		});
}

FVector UTDBudgetTickSubsystem::ResolveReferenceLocation() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return FVector::ZeroVector;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	return ViewLocation;
}

float UTDBudgetTickSubsystem::CalculatePriorityScore(const FTDBudgetTickRecord& Record, const float Distance, const float TimeSinceLastTick) const
{
	const AActor* Actor = Record.Actor.Get();
	const UTDBudgetTickParticipantComponent* Participant = Record.Participant.Get();
	const ITDBudgetTickable* Tickable = ResolveBudgetTickable(Actor);
	if (!Tickable || !IsValid(Participant))
	{
		return 0.f;
	}

	const float EffectiveImportance = Participant->GetEffectiveImportance() + Tickable->GetBudgetImportanceBias();
	const float DistanceNormalization = FMath::Max(1.f, TDBudgetTickCVars::CVarDistanceNormalization.GetValueOnGameThread());
	const float DistanceScore = 1.f - FMath::Clamp(Distance / DistanceNormalization, 0.f, 1.f);
	const float MaxUpdateInterval = FMath::Max(0.001f, Participant->GetMaxUpdateInterval());
	const float OverdueRatio = TimeSinceLastTick / MaxUpdateInterval;
	const float StarvationScore = FMath::Clamp(OverdueRatio, 0.f, 1.f)
		+ FMath::Max(0.f, OverdueRatio - 1.f) * TDBudgetTickCVars::CVarOverdueEscalation.GetValueOnGameThread();
	const float NearBonus = (Participant->ShouldAlwaysTickWhenNear() && Distance <= Participant->GetNearDistance())
		? TDBudgetTickCVars::CVarNearBonusWeight.GetValueOnGameThread()
		: 0.f;

	return (EffectiveImportance * TDBudgetTickCVars::CVarImportanceWeight.GetValueOnGameThread())
		+ (DistanceScore * TDBudgetTickCVars::CVarDistanceWeight.GetValueOnGameThread())
		+ NearBonus
		+ (StarvationScore * TDBudgetTickCVars::CVarStarvationWeight.GetValueOnGameThread());
}

double UTDBudgetTickSubsystem::ExecuteBudgetTick(FTDBudgetTickRecord& Record, const double CurrentTimeSeconds) const
{
	ITDBudgetTickable* Tickable = ResolveBudgetTickable(Record.Actor.Get());
	if (!Tickable)
	{
		return 0.0;
	}

	const float DeltaToProcess = FMath::Max(0.f, Record.AccumulatedDeltaTime);
	if (DeltaToProcess <= KINDA_SMALL_NUMBER)
	{
		return 0.0;
	}

	const double StartTimeSeconds = FPlatformTime::Seconds();
	Tickable->BudgetTick(DeltaToProcess);
	const double CostMs = (FPlatformTime::Seconds() - StartTimeSeconds) * 1000.0;

	Record.LastTickTimeSeconds = CurrentTimeSeconds;
	Record.AccumulatedDeltaTime = 0.f;
	Record.EstimatedCostMs = FMath::Lerp(Record.EstimatedCostMs, static_cast<float>(CostMs), 0.25f);

	return CostMs;
}

void UTDBudgetTickSubsystem::DrawDebugOverlay(const int32 CandidateCount, const int32 NearCount, const int32 OverdueCount, const int32 BudgetedCount, const double BudgetSpentMs) const
{
	if (TDBudgetTickCVars::CVarDebug.GetValueOnGameThread() <= 0 || !GEngine)
	{
		return;
	}

	const FString Message = FString::Printf(
		TEXT("BudgetTick | Registered: %d | Candidates: %d | Near: %d | Overdue: %d | Budgeted: %d | Budget: %.2f / %.2f ms"),
		ManagedRecords.Num(),
		CandidateCount,
		NearCount,
		OverdueCount,
		BudgetedCount,
		BudgetSpentMs,
		TDBudgetTickCVars::CVarFrameBudgetMs.GetValueOnGameThread());

	GEngine->AddOnScreenDebugMessage(DebugMessageKey, 0.f, FColor::Yellow, Message);
}

void UTDBudgetTickSubsystem::DrawDebugActorLabels() const
{
	if (TDBudgetTickCVars::CVarDebug.GetValueOnGameThread() <= 1)
	{
		return;
	}

	for (const TSharedPtr<FTDBudgetTickRecord>& Record : ManagedRecords)
	{
		if (!Record.IsValid())
		{
			continue;
		}

		AActor* Actor = Record->Actor.Get();
		if (!Actor)
		{
			continue;
		}

		const FString Label = FString::Printf(
			TEXT("Dist %.0f | Score %.2f | Cost %.2f ms"),
			Record->LastDistance,
			Record->LastScore,
			Record->EstimatedCostMs);

		DrawDebugString(GetWorld(), Actor->GetActorLocation() + FVector(0.f, 0.f, 120.f), Label, nullptr, FColor::Green, 0.f, true);
	}
}
