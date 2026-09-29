#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "MonsterAI/TDMonsterBrainSlot.h"
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
	static constexpr float DisengageRadius = 3500.f;
	static constexpr float AlertRadius = 900.f;
	static constexpr float NeighborRadius = 1200.f;
	static constexpr float DamageEngageSeconds = 6.f;
	static constexpr float StaggerHealthRatio = 0.12f;
	static constexpr int32 MaxConcurrentAttackersPerTarget = 3;
	static constexpr float AvoidanceLookAhead = 90.f;
	static constexpr float AvoidanceTurnDegrees = 65.f;
	static constexpr float ReengageBlockSeconds = 6.f;
	static constexpr float HomeArrivalRadius = 120.f;
	static constexpr float ReturnHomeSpeedScale = 0.7f;

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

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend struct FTDMonsterThinkTickFunction;

	void AdvanceFrame(float DeltaSeconds);
	bool IsHandleCurrent(FTDMonsterSlotHandle Handle) const;
	void RebuildOrder();
	void RunKernelStep();
	void SenseSlot(int32 SlotIndex);
	void UpdateEngagement();
	void ThinkAndAct(int32 SlotIndex);
	void ApplySeparation();
	FVector2D SteerAroundBlockingAlly(int32 SlotIndex, const FVector2D& Direction) const;
	void CountAttackersByTarget();
	bool HasAttackToken(int32 SlotIndex) const;
	bool RefreshTarget(int32 SlotIndex);
	void PresentFrame(float DeltaSeconds);
	AActor* AcquireTarget(int32 SlotIndex) const;
	FTDBrainInputs BuildInputs(int32 SlotIndex, const FTDMonsterStepContext& Context) const;
	FTDMonsterStepContext MakeContext(int32 SlotIndex) const;
	void Engage(int32 SlotIndex);
	void Disengage(int32 SlotIndex);
	FTDMonsterMoveIntent MakeReturnHomeIntent(int32 SlotIndex) const;
	void DrawDebugState(int32 SlotIndex) const;

	FTDMonsterThinkTickFunction TickFunction;
	TArray<FTDMonsterBodyFragment> Bodies;
	TArray<FTDMonsterBrainFragment> Brains;
	TArray<FTDMonsterMoveIntent> Intents;
	TArray<int32> FreeSlots;
	TArray<int32> OrderedSlots;
	TArray<int32> PendingAlertSlots;
	TMap<TObjectKey<AActor>, int32> AttackersByTarget;
	uint64 StepIndex = 0;
	uint32 NextSimulationId = 1;
	float AccumulatedSeconds = 0.f;
};
