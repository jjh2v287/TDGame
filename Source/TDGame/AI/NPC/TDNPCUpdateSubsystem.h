#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDNPCUpdateSubsystem.generated.h"

class AActor;

UENUM()
enum class ETDNPCUpdateMode : uint8
{
	Unbudgeted = 0,
	Budgeted = 1
};

USTRUCT()
struct FTDManagedNPCEntry
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> OwnerActor;

	UPROPERTY(Transient)
	float AccumulatedTime = 0.f;

	UPROPERTY(Transient)
	float Distance = 0.f;

	UPROPERTY(Transient)
	float Significance = 0.f;

	explicit FTDManagedNPCEntry(AActor* InActor = nullptr)
		: OwnerActor(InActor)
	{
	}
};

UCLASS()
class TDGAME_API UTDNPCUpdateSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return false; }

	void Register(AActor* InActor);
	void Unregister(AActor* InActor);

private:
	void UpdateSignificanceViewPoint() const;
	void CleanupInvalidEntries();
	void RebuildPriorityGroups();
	void TickAllActors();
	void TickBudgetedActors(double TickStartTime);
	void TickEntry(FTDManagedNPCEntry& Entry) const;
	void TickActor(AActor* Actor, float DeltaTime) const;
	ETDNPCUpdateMode GetUpdateMode() const;
	void DrawDebugOverlay(double TotalTickTimeMs) const;

	UFUNCTION()
	void OnActorDestroyed(AActor* DestroyedActor);

	TArray<TSharedPtr<FTDManagedNPCEntry>> ManagedActors;
	TArray<FTDManagedNPCEntry*> HighPriorityActors;
	TArray<FTDManagedNPCEntry*> LowPriorityActors;

	float RebalanceTimer = 0.f;
	int32 LowPriorityCursor = 0;
	mutable uint64 DebugMessageKey = 0;
};
