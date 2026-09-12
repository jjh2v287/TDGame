#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TDCombatTokenSubsystem.generated.h"

class APawn;
struct FTDActorDeathMessage;

UENUM(BlueprintType)
enum class ETDCombatTokenState : uint8
{
	Available,
	InUse,
	OnCooldown
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDCombatToken
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	ETDCombatTokenState State = ETDCombatTokenState::Available;

	UPROPERTY(Transient)
	FTimerHandle InUseTimerHandle;

	UPROPERTY(Transient)
	FTimerHandle CooldownTimerHandle;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> TokenUser = nullptr;
};

USTRUCT()
struct FTDCombatTokenRuntimeData
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> MonsterPawn = nullptr;

	bool operator==(const APawn* OtherPawn) const
	{
		return MonsterPawn.Get() == OtherPawn;
	}
};

UCLASS(BlueprintType)
class TDGAME_API UTDCombatTokenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="CombatToken")
	void RegisterAggroMonster(APawn* MonsterPawn);

	UFUNCTION(BlueprintCallable, Category="CombatToken")
	void UnregisterAggroMonster(APawn* MonsterPawn);

	UFUNCTION(BlueprintPure, Category="CombatToken")
	bool IsTokenAvailable() const;

	UFUNCTION(BlueprintCallable, Category="CombatToken")
	int32 RequestToken(APawn* Requester);

	UFUNCTION(BlueprintCallable, Category="CombatToken")
	bool RequestThenRelease(APawn* Requester);

	UFUNCTION(BlueprintCallable, Category="CombatToken")
	void ReleaseTokenByUser(const APawn* Requester);

	UFUNCTION(BlueprintPure, Category="CombatToken")
	int32 GetAggroMonsterCount() const { return AggroMonsters.Num(); }

	UFUNCTION(BlueprintPure, Category="CombatToken")
	int32 GetTokenPoolSize() const { return TokenPool.Num(); }

	UFUNCTION(BlueprintPure, Category="CombatToken")
	int32 GetAvailableTokenCount() const;

protected:
	UPROPERTY(EditAnywhere, Category="CombatToken", meta=(ClampMin="0.0", ClampMax="1.0"))
	float TokenControlRatio = 1.0f;

	UPROPERTY(EditAnywhere, Category="CombatToken", meta=(ClampMin="0.0"))
	float TokenCooldownDuration = 10.0f;

	UPROPERTY(EditAnywhere, Category="CombatToken", meta=(ClampMin="0.0"))
	float TokenInUseTimeout = 0.0f;

private:
	void EnsureDeathListenerRegistered();
	void PruneInvalidRuntimeData();
	void UpdateTokenPoolByMonsterCount();
	void ReleaseToken(int32 TokenIndex);
	void OnTokenCooldownFinished(int32 TokenIndex);
	void OnInUseTimerFinished(APawn* Requester);
	void HandleActorDeathMessage(FGameplayTag Channel, const FTDActorDeathMessage& Message);
	UWorld* GetGameWorld() const;

	UPROPERTY(Transient)
	TArray<FTDCombatToken> TokenPool;

	UPROPERTY(Transient)
	TArray<FTDCombatTokenRuntimeData> AggroMonsters;

	FGameplayMessageListenerHandle DeathMessageListenerHandle;
};
