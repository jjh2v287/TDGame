#pragma once

#include "CoreMinimal.h"
#include "Performance/BudgetTick/TDBudgetTickParticipantComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDBudgetTickSubsystem.generated.h"

class AActor;

struct FTDBudgetTickRecord
{
	explicit FTDBudgetTickRecord(AActor* InActor = nullptr, UTDBudgetTickParticipantComponent* InParticipant = nullptr)
		: Actor(InActor)
		, Participant(InParticipant)
	{
	}

	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UTDBudgetTickParticipantComponent> Participant;
	float AccumulatedDeltaTime = 0.f;
	float LastDistance = 0.f;
	float LastScore = 0.f;
	float EstimatedCostMs = 0.05f;
	double LastTickTimeSeconds = 0.0;
};

UCLASS()
class TDGAME_API UTDBudgetTickSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }

	bool RegisterParticipant(AActor* Actor, UTDBudgetTickParticipantComponent* Participant);
	void UnregisterParticipant(AActor* Actor);

private:
	struct FPriorityCandidate
	{
		FTDBudgetTickRecord* Record = nullptr;
		float Distance = 0.f;
		float TimeSinceLastTick = 0.f;
		float Score = 0.f;
		float PriorityPerCost = 0.f;
		bool bIsNear = false;
		bool bIsOverdue = false;
	};

	void CleanupInvalidEntries();
	FVector ResolveReferenceLocation() const;
	float CalculatePriorityScore(const FTDBudgetTickRecord& Record, float Distance, float TimeSinceLastTick) const;
	double ExecuteBudgetTick(FTDBudgetTickRecord& Record, double CurrentTimeSeconds) const;
	void DrawDebugOverlay(int32 CandidateCount, int32 NearCount, int32 OverdueCount, int32 BudgetedCount, double BudgetSpentMs) const;
	void DrawDebugActorLabels() const;

	TArray<TSharedPtr<FTDBudgetTickRecord>> ManagedRecords;
	mutable uint64 DebugMessageKey = 0;
};
