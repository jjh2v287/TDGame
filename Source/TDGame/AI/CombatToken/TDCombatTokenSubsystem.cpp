#include "AI/CombatToken/TDCombatTokenSubsystem.h"
#include "Algo/Count.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

void UTDCombatTokenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureDeathListenerRegistered();
}

void UTDCombatTokenSubsystem::Deinitialize()
{
	if (DeathMessageListenerHandle.IsValid())
	{
		DeathMessageListenerHandle.Unregister();
	}

	if (UWorld* World = GetGameWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		for (FTDCombatToken& Token : TokenPool)
		{
			TimerManager.ClearTimer(Token.InUseTimerHandle);
			TimerManager.ClearTimer(Token.CooldownTimerHandle);
		}
	}

	TokenPool.Empty();
	AggroMonsters.Empty();

	Super::Deinitialize();
}

void UTDCombatTokenSubsystem::RegisterAggroMonster(APawn* MonsterPawn)
{
	if (!MonsterPawn)
	{
		return;
	}

	EnsureDeathListenerRegistered();
	PruneInvalidRuntimeData();

	if (AggroMonsters.Contains(MonsterPawn))
	{
		return;
	}

	FTDCombatTokenRuntimeData RuntimeData;
	RuntimeData.MonsterPawn = MonsterPawn;
	AggroMonsters.Add(RuntimeData);
	UpdateTokenPoolByMonsterCount();
}

void UTDCombatTokenSubsystem::UnregisterAggroMonster(APawn* MonsterPawn)
{
	if (!MonsterPawn)
	{
		return;
	}

	PruneInvalidRuntimeData();
	AggroMonsters.RemoveAllSwap([MonsterPawn](const FTDCombatTokenRuntimeData& RuntimeData)
	{
		return RuntimeData.MonsterPawn.Get() == MonsterPawn;
	});

	ReleaseTokenByUser(MonsterPawn);
	UpdateTokenPoolByMonsterCount();
}

bool UTDCombatTokenSubsystem::IsTokenAvailable() const
{
	return TokenPool.ContainsByPredicate([](const FTDCombatToken& Token)
	{
		return Token.State == ETDCombatTokenState::Available;
	});
}

int32 UTDCombatTokenSubsystem::RequestToken(APawn* Requester)
{
	if (!Requester)
	{
		return INDEX_NONE;
	}

	EnsureDeathListenerRegistered();
	PruneInvalidRuntimeData();
	UpdateTokenPoolByMonsterCount();

	UWorld* World = GetGameWorld();
	if (!World)
	{
		return INDEX_NONE;
	}

	for (int32 TokenIndex = 0; TokenIndex < TokenPool.Num(); ++TokenIndex)
	{
		FTDCombatToken& Token = TokenPool[TokenIndex];
		if (Token.State != ETDCombatTokenState::Available)
		{
			continue;
		}

		World->GetTimerManager().ClearTimer(Token.InUseTimerHandle);
		World->GetTimerManager().ClearTimer(Token.CooldownTimerHandle);

		Token.State = ETDCombatTokenState::InUse;
		Token.TokenUser = Requester;

		if (TokenInUseTimeout > 0.0f)
		{
			FTimerDelegate TimerDelegate;
			TimerDelegate.BindUObject(this, &ThisClass::OnInUseTimerFinished, Requester);
			World->GetTimerManager().SetTimer(Token.InUseTimerHandle, TimerDelegate, TokenInUseTimeout, false);
		}

		return TokenIndex;
	}

	return INDEX_NONE;
}

bool UTDCombatTokenSubsystem::RequestThenRelease(APawn* Requester)
{
	const int32 TokenIndex = RequestToken(Requester);
	if (TokenIndex == INDEX_NONE)
	{
		return false;
	}

	ReleaseToken(TokenIndex);
	return true;
}

void UTDCombatTokenSubsystem::ReleaseTokenByUser(const APawn* Requester)
{
	if (!Requester)
	{
		return;
	}

	for (int32 TokenIndex = 0; TokenIndex < TokenPool.Num(); ++TokenIndex)
	{
		const FTDCombatToken& Token = TokenPool[TokenIndex];
		if (Token.State == ETDCombatTokenState::InUse && Token.TokenUser.Get() == Requester)
		{
			ReleaseToken(TokenIndex);
			return;
		}
	}
}

int32 UTDCombatTokenSubsystem::GetAvailableTokenCount() const
{
	return Algo::CountIf(TokenPool, [](const FTDCombatToken& Token)
	{
		return Token.State == ETDCombatTokenState::Available;
	});
}

void UTDCombatTokenSubsystem::EnsureDeathListenerRegistered()
{
	if (DeathMessageListenerHandle.IsValid())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance || !GameInstance->GetWorld())
	{
		return;
	}

	DeathMessageListenerHandle = UGameplayMessageSubsystem::Get(GameInstance).RegisterListener<FTDActorDeathMessage>(
		TDGameplayTags::Event_Actor_Death,
		this,
		&ThisClass::HandleActorDeathMessage);
}

void UTDCombatTokenSubsystem::PruneInvalidRuntimeData()
{
	AggroMonsters.RemoveAllSwap([](const FTDCombatTokenRuntimeData& RuntimeData)
	{
		return !RuntimeData.MonsterPawn.IsValid();
	});

	for (int32 TokenIndex = 0; TokenIndex < TokenPool.Num(); ++TokenIndex)
	{
		const FTDCombatToken& Token = TokenPool[TokenIndex];
		if (Token.State == ETDCombatTokenState::InUse && !Token.TokenUser.IsValid())
		{
			ReleaseToken(TokenIndex);
		}
	}
}

void UTDCombatTokenSubsystem::UpdateTokenPoolByMonsterCount()
{
	const int32 MonsterCount = AggroMonsters.Num();
	int32 NewMaxTokens = 0;
	if (MonsterCount > 0)
	{
		NewMaxTokens = FMath::Max(FMath::FloorToInt(static_cast<float>(MonsterCount) * TokenControlRatio), 1);
	}

	const int32 CurrentTokenCount = TokenPool.Num();
	if (NewMaxTokens == CurrentTokenCount)
	{
		return;
	}

	if (NewMaxTokens > CurrentTokenCount)
	{
		TokenPool.AddDefaulted(NewMaxTokens - CurrentTokenCount);
		return;
	}

	UWorld* World = GetGameWorld();
	const int32 TargetRemovals = CurrentTokenCount - NewMaxTokens;
	int32 RemovedCount = 0;

	for (int32 TokenIndex = TokenPool.Num() - 1; TokenIndex >= 0 && RemovedCount < TargetRemovals; --TokenIndex)
	{
		if (TokenPool[TokenIndex].State == ETDCombatTokenState::InUse)
		{
			continue;
		}

		if (World)
		{
			World->GetTimerManager().ClearTimer(TokenPool[TokenIndex].InUseTimerHandle);
			World->GetTimerManager().ClearTimer(TokenPool[TokenIndex].CooldownTimerHandle);
		}

		TokenPool.RemoveAt(TokenIndex);
		++RemovedCount;
	}
}

void UTDCombatTokenSubsystem::ReleaseToken(const int32 TokenIndex)
{
	if (!TokenPool.IsValidIndex(TokenIndex))
	{
		return;
	}

	FTDCombatToken& Token = TokenPool[TokenIndex];
	if (Token.State == ETDCombatTokenState::Available)
	{
		return;
	}

	UWorld* World = GetGameWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(Token.InUseTimerHandle);
		World->GetTimerManager().ClearTimer(Token.CooldownTimerHandle);
	}

	Token.TokenUser.Reset();

	if (TokenCooldownDuration <= 0.0f)
	{
		Token.State = ETDCombatTokenState::Available;
		Token.InUseTimerHandle.Invalidate();
		Token.CooldownTimerHandle.Invalidate();
		return;
	}

	Token.State = ETDCombatTokenState::OnCooldown;
	if (!World)
	{
		return;
	}

	FTimerDelegate TimerDelegate;
	TimerDelegate.BindUObject(this, &ThisClass::OnTokenCooldownFinished, TokenIndex);
	World->GetTimerManager().SetTimer(Token.CooldownTimerHandle, TimerDelegate, TokenCooldownDuration, false);
}

void UTDCombatTokenSubsystem::OnTokenCooldownFinished(const int32 TokenIndex)
{
	if (!TokenPool.IsValidIndex(TokenIndex))
	{
		return;
	}

	FTDCombatToken& Token = TokenPool[TokenIndex];
	Token.State = ETDCombatTokenState::Available;
	Token.TokenUser.Reset();
	Token.InUseTimerHandle.Invalidate();
	Token.CooldownTimerHandle.Invalidate();
}

void UTDCombatTokenSubsystem::OnInUseTimerFinished(APawn* Requester)
{
	ReleaseTokenByUser(Requester);
}

void UTDCombatTokenSubsystem::HandleActorDeathMessage(FGameplayTag Channel, const FTDActorDeathMessage& Message)
{
	if (APawn* DeadPawn = Cast<APawn>(Message.DeadActor))
	{
		UnregisterAggroMonster(DeadPawn);
	}
}

UWorld* UTDCombatTokenSubsystem::GetGameWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}
