#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "MonsterAI/TDCombatTactics.h"
#include "MonsterAI/TDMonsterBrainSlot.h"
#include "MonsterAI/TDCircleAvoidance.h"
#include "MonsterAI/TDNeighborGrid.h"
#include "TDMonsterThinkSubsystem.generated.h"

class ITDMonsterBody;
class UTDMonsterThinkSubsystem;
struct FTDMonsterStepContext;

USTRUCT()
struct FTDMonsterThinkTickFunction : public FTickFunction
{
	GENERATED_BODY()

	UTDMonsterThinkSubsystem* Owner = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template<>
struct TStructOpsTypeTraits<FTDMonsterThinkTickFunction> : public TStructOpsTypeTraitsBase2<FTDMonsterThinkTickFunction>
{
	enum
	{
		WithCopy = false
	};
};

struct FTDMonsterSlotHandle
{
	int32 Index = INDEX_NONE;
	uint32 Generation = 0;

	bool IsValid() const
	{
		return Index != INDEX_NONE;
	}
};

UCLASS()
class TDGAME_API UTDMonsterThinkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxStepsPerFrame = 4;
	static constexpr float EngageRadius = 1500.f;
	static constexpr float LoseTargetRadius = 3500.f;
	static constexpr float LeashRadius = 3000.f;
	static constexpr float ReturningReengageRadius = 2500.f;
	static constexpr float MaxChaseDistanceFromHome = 4500.f;
	static constexpr float AlertRadius = 900.f;
	static constexpr float NeighborRadius = 1200.f;
	static constexpr float DamageEngageSeconds = 6.f;
	static constexpr float StaggerHealthRatio = 0.12f;
	static constexpr int32 MaxConcurrentAttackersPerTarget = 3;
	static constexpr float AvoidanceLookAhead = 90.f;
	static constexpr float AvoidanceTurnDegrees = 65.f;
	static constexpr float ReengageBlockSeconds = 6.f;
	static constexpr float HomeArrivalRadius = 120.f;
	static constexpr float ReturnHomeSpeedScale = 1.f;
	static constexpr float GiveUpSeconds = 2.f;
	static constexpr float ReturnStallSeconds = 1.f;
	static constexpr float ArrivalSettleSeconds = 1.f;
	static constexpr float AvoidanceWaitGraceSeconds = 3.f;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	FTDMonsterSlotHandle RegisterMonster(AActor* BodyActor, TSharedPtr<const FTDResolvedMonsterDefinition> Definition);
	void UnregisterMonster(FTDMonsterSlotHandle Handle);
	void NotifyMonsterDamaged(FTDMonsterSlotHandle Handle, float Damage, const FVector& SourceLocation);
	void NotifyMonsterFrozen(FTDMonsterSlotHandle Handle, bool bIsFrozen);

	ETDMonsterFsmState GetMonsterState(FTDMonsterSlotHandle Handle) const;
	FName GetMonsterActionId(FTDMonsterSlotHandle Handle) const;
	bool IsMonsterEngaged(FTDMonsterSlotHandle Handle) const;
	uint64 GetStepIndex() const { return StepIndex; }

	UFUNCTION(BlueprintCallable, Category="MonsterAI", meta=(WorldContext="WorldContextObject"))
	static UTDMonsterThinkSubsystem* GetMonsterThinkSubsystem(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category="MonsterAI")
	int32 GetActiveMonsterCount() const;

	UFUNCTION(BlueprintCallable, Category="MonsterAI")
	TArray<FString> GetMonsterDebugLines() const;

	UFUNCTION(BlueprintCallable, Category="MonsterAI")
	TArray<FString> GetTacticsDebugLines() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend struct FTDMonsterThinkTickFunction;
	friend struct FTDMonsterThinkSubsystemTestAccess;
	friend struct FTDCombatTacticsTestAccess;

	void AdvanceFrame(float DeltaSeconds);
	bool IsHandleCurrent(FTDMonsterSlotHandle Handle) const;
	void RebuildOrder();
	void RunKernelStep();
	void SenseSlot(int32 SlotIndex);
	void UpdateEngagement();
	void ThinkAndAct(int32 SlotIndex);
	void ApplyLocalAvoidance();
	void ApplySeparation();
	void ApplyCircleAvoidance();
	FVector2D ApplyCircleAvoidanceToSlot(int32 SlotIndex, TConstArrayView<FTDAvoidanceObstacle> ExternalBodies, TConstArrayView<const AActor*> ExternalActors);
	void GatherExternalBodies(TArray<FTDAvoidanceObstacle>& OutBodies, TArray<const AActor*>& OutActors) const;
	void GatherMonsterObstacles(int32 SlotIndex, FTDAvoidanceObstacleList& OutObstacles) const;
	bool IsHoldingEngagedAttacker(int32 SlotIndex) const;
	bool IsReturnStallExcusedByAvoidance(int32 SlotIndex) const;
	void RebuildNeighborGrid();
	void RefreshNeighborGridIfStale();
	FVector2D SteerAroundBlockingAlly(int32 SlotIndex, const FVector2D& Direction) const;
	void CountAttackersByTarget();
	bool HasAttackToken(int32 SlotIndex) const;
	bool RefreshTarget(int32 SlotIndex);
	void PresentFrame(float DeltaSeconds);
	AActor* AcquireTarget(int32 SlotIndex) const;
	FTDBrainInputs BuildInputs(int32 SlotIndex, const FTDMonsterStepContext& Context) const;
	FTDMonsterStepContext MakeContext(int32 SlotIndex) const;
	void Engage(int32 SlotIndex);
	void BeginReturn(int32 SlotIndex);
	void FinishReturn(int32 SlotIndex);
	FTDMonsterMoveIntent MakeReturnHomeIntent(int32 SlotIndex) const;
	void DrawDebugState(int32 SlotIndex) const;
	void SnapshotTacticsSettings();
	void ResetTactics();
	void UpdateTactics();
	void ApplyPendingKills();
	bool LinkTacticsRecords();
	int32 CreateTacticsRecord(AActor* Target);
	void CountTacticsEngaged(FTDTargetTacticsRecord& Record);
	bool SyncTacticsSlots();
	bool ReturnFinishedTacticsToken(int32 SlotIndex);
	void RevokeInvalidReservation(int32 SlotIndex);
	void DetectBlockedTacticsSector(int32 SlotIndex);
	bool MeasureTacticsSlotProgress(int32 SlotIndex, ETDTacticsSlotKind Kind, const FTDMonsterStepContext& Context, int32& OutProgressValue, int32& OutProgressThreshold) const;
	void UpdateTacticsDirectors(bool bIsRingPass);
	int32 PollTargetHealth(FTDTargetTacticsRecord& Record, const AActor* Target) const;
	void AssignWaitAndRangedSectors();
	void AssignWaitSectors(FTDTargetTacticsRecord& Record, const FVector& TargetLocation, const FTDSectorSearch& Search);
	void ChooseWaitSector(int32 SlotIndex, FTDTargetTacticsRecord& Record, const FTDSectorSearch& Search);
	void AssignRangedSectors(FTDTargetTacticsRecord& Record, const FVector& TargetLocation, const FTDSectorSearch& Search);
	void AssignInnerSectors(FTDTargetTacticsRecord& Record, const FVector& TargetLocation);
	void RunTokenPass(bool bIsPeriodic);
	void GrantPoolTokens(FTDTargetTacticsRecord& Record, int32 PoolIndex, const FVector& TargetLocation);
	void InterceptForRetaliation(FTDTargetTacticsRecord& Record, int32 PoolIndex, const FVector& TargetLocation, TConstArrayView<FTDTacticsCandidateKey> RemainingCandidates);
	int32 FindRetaliationVictim(const FTDTargetTacticsRecord& Record, int32 PoolIndex, const FVector& TargetLocation, int64 CandidateDistanceSquared) const;
	void GrantTacticsToken(int32 SlotIndex, uint32 RecordId);
	void RemoveFinishedTacticsRecords();
	void ConfirmTacticsAttackStart(int32 SlotIndex);
	void DetachTacticsSlot(int32 SlotIndex);
	bool IsTokenCandidate(int32 SlotIndex, const FTDTargetTacticsRecord& Record) const;
	bool CanKeepTacticsToken(int32 SlotIndex) const;
	bool HasOtherTokenCandidate(int32 SlotIndex, const FTDTargetTacticsRecord& Record) const;
	FTDTacticsCandidateKey MakeTacticsCandidateKey(int32 SlotIndex, const FVector& TargetLocation) const;
	TArray<FTDTacticsCandidateKey> GatherOrderedTacticsMembers(const FTDTargetTacticsRecord& Record, const FVector& TargetLocation, TFunctionRef<bool(int32)> Filter) const;
	int32 CountHeldQuarters(uint32 RecordId, int32 PoolIndex) const;
	int32 FindTacticsRecordIndex(const AActor* Target) const;
	int32 FindTacticsRecordIndexById(uint32 RecordId) const;
	bool HasTacticsAttackToken(int32 SlotIndex) const;
	bool IsStepElapsed(uint64 SinceStep, int32 Steps) const;
	bool IsAvoidingTacticsTarget(int32 SlotIndex) const;
	ETDTacticsSlotKind ResolveTacticsSlotKind(int32 SlotIndex, const FTDTargetTacticsRecord*& OutRecord) const;
	void FillTacticsContext(int32 SlotIndex, FTDMonsterStepContext& Context) const;
	FString MakeTacticsDebugSuffix(int32 SlotIndex) const;
	void DrawTacticsDebugState(int32 SlotIndex) const;

	FTDMonsterThinkTickFunction TickFunction;
	TArray<FTDMonsterBodyFragment> Bodies;
	TArray<FTDMonsterBrainFragment> Brains;
	TArray<FTDMonsterMoveIntent> Intents;
	TArray<FTDMonsterAvoidanceFragment> Avoidances;
	TArray<FTDMonsterTacticsFragment> Tactics;
	TArray<FTDTargetTacticsRecord> TacticsRecords;
	TArray<FVector> PendingKillLocations;
	TArray<int32> FreeSlots;
	TArray<int32> OrderedSlots;
	TArray<int32> PendingAlertSlots;
	TMap<TObjectKey<AActor>, int32> AttackersByTarget;
	FTDNeighborGrid NeighborGrid;
	bool bIsNeighborGridStale = true;
	uint64 StepIndex = 0;
	uint32 NextSimulationId = 1;
	uint32 NextTacticsRecordId = 1;
	bool bIsTacticsActive = true;
	bool bShouldDrawTacticsDebug = false;
	float AccumulatedSeconds = 0.f;
};
