#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Feedback/TDDamageNumberPool.h"
#include "Combat/Feedback/TDDeferredFeedbackQueue.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDCombatFeedbackSubsystem.generated.h"

class AActor;
class APlayerController;
class ATDCombatCharacter;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class USoundBase;
class USoundAttenuation;
class USoundConcurrency;
class UTDCombatFeedbackSettings;
class UTDCombatOverlayWidget;
struct FStreamableHandle;
struct FTDActorDeathMessage;
struct FTDDamageAppliedMessage;
struct FTDDamageAvoidedMessage;
struct FTDFeedbackSoundSet;
struct FTDMeleeSwingMessage;

enum class ETDHitFeedbackTier : uint8
{
	Light,
	Heavy,
	Kill
};

enum class ETDFeedbackSoundChannel : uint8
{
	Impact,
	Accent,
	Swing
};

struct FTDHitPresentation
{
	ETDHitFeedbackTier Tier = ETDHitFeedbackTier::Light;
	float NumberAmount = 0.f;
	bool bIsStatus = false;
	bool bShowsImpactEffects = false;
	bool bWobblesVictim = false;
	bool bStopsVictim = false;
	bool bStopsAttacker = false;
	bool bPlaysCameraFeedback = false;
	bool bPlaysAccentSounds = false;
};

struct FTDHitFlashRequest
{
	TWeakObjectPtr<ATDCombatCharacter> Victim;
	FLinearColor Color = FLinearColor::White;
	float PeakIntensity = 1.f;
};

USTRUCT()
struct TDGAME_API FTDCombatFeedbackStats
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NumbersShown = 0;

	UPROPERTY()
	int32 EffectsRequested = 0;

	UPROPERTY()
	int32 EffectsSpawned = 0;

	UPROPERTY()
	int32 EffectsSkippedNotLoaded = 0;

	UPROPERTY()
	int32 EffectsSkippedFrameCap = 0;

	UPROPERTY()
	int32 EffectsSpawnFailed = 0;

	UPROPERTY()
	int32 MonsterHitsSeen = 0;

	UPROPERTY()
	int32 MonsterHitEffectsSpawned = 0;

	UPROPERTY()
	int32 SoundsPlayed = 0;

	UPROPERTY()
	int32 FlashesStarted = 0;

	UPROPERTY()
	int32 HitStopsStarted = 0;

	UPROPERTY()
	int32 WobblesStarted = 0;

	UPROPERTY()
	int32 ShakesStarted = 0;

	UPROPERTY()
	int32 AvoidedShown = 0;

	UPROPERTY()
	int32 DeathScreensShown = 0;

	UPROPERTY()
	int32 SwingsPlayed = 0;

	UPROPERTY()
	int32 PlayerDamageNumbersShown = 0;
};

USTRUCT()
struct FTDHitFlashState
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<ATDCombatCharacter> Character;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PreviousOverlay;

	double StartTime = 0.;
	float PeakIntensity = 1.f;
	bool bIsFading = false;
	bool bIsOverlayApplied = false;
};

UCLASS()
class TDGAME_API UTDCombatFeedbackSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static bool ShouldEnableFeedback(bool bCanEverRender, EWorldType::Type WorldType, bool bHasGameInstance);
	static ETDHitFeedbackTier ClassifyHitTier(bool bWasKilled, bool bWasCritical, float DamageRatio, float HeavyDamageRatio);
	static FTDHitPresentation DescribeHitPresentation(const FTDDamageAppliedMessage& Message, float HeavyDamageRatio);

	void CreateSoundSettings(const UTDCombatFeedbackSettings& FeedbackSettings);
	USoundConcurrency* GetSoundConcurrency(ETDFeedbackSoundChannel Channel) const;
	const FTDCombatFeedbackStats& GetStats() const { return Stats; }
	bool IsListening() const { return bIsListening; }
	bool AreFeedbackAssetsLoaded() const { return bAreFeedbackAssetsLoaded; }
	UTDCombatOverlayWidget* GetOverlayWidget() const { return OverlayWidget.Get(); }

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

private:
	struct FTDWobbleState
	{
		TWeakObjectPtr<ATDCombatCharacter> Character;
		FVector Direction = FVector::ZeroVector;
		double StartTime = 0.;
		float Amplitude = 0.f;
	};

	void RegisterListeners(UWorld& InWorld);
	void UnregisterListeners();
	USoundConcurrency* CreateSoundConcurrency(int32 MaxCount);
	void RequestFeedbackAssets(const UTDCombatFeedbackSettings& FeedbackSettings);
	void HandleFeedbackAssetsLoaded();

	void HandleDamageApplied(FGameplayTag Channel, const FTDDamageAppliedMessage& Message);
	void HandleDamageAvoided(FGameplayTag Channel, const FTDDamageAvoidedMessage& Message);
	void HandleActorDeath(FGameplayTag Channel, const FTDActorDeathMessage& Message);
	void HandleMeleeSwing(FGameplayTag Channel, const FTDMeleeSwingMessage& Message);

	void ShowHitNumber(const FTDDamageAppliedMessage& Message, const FTDHitPresentation& Presentation, const FVector& Location, bool bIsLocalVictim);
	bool SpawnImpactEffects(const FTDDamageAppliedMessage& Message, const FVector& Location, const FVector& Direction);
	void DeactivateLoopingEffectLater(UNiagaraComponent& Effect, float DelaySeconds);
	void PlayImpactSounds(const FTDDamageAppliedMessage& Message, const FTDHitPresentation& Presentation, const FVector& Location, bool bIsLocalVictim);
	void StartHitFlash(ATDCombatCharacter& Victim, const FTDHitPresentation& Presentation, bool bWasCritical, bool bIsLocalVictim);
	void StartWobble(ATDCombatCharacter& Victim, ETDHitFeedbackTier Tier, const FVector& Direction);
	void StartVictimHitStop(ATDCombatCharacter& Victim, ETDHitFeedbackTier Tier);
	void StartAttackerHitStop(AActor* Instigator);
	void PlayLocalCameraFeedback(APlayerController& Controller, ETDHitFeedbackTier Tier, float DamageRatio, bool bIsLocalAttacker, bool bIsLocalVictim);
	void ShowMonsterHealthBar(ATDCombatCharacter& Victim, const FTDDamageAppliedMessage& Message);

	void AddDamageNumber(const FTDDamageNumberRequest& Request);
	bool TryShowDamageNumber(const FTDDamageNumberRequest& Request);
	bool TryStartHitFlash(const FTDHitFlashRequest& Request);
	void RefreshHitFlash(FTDHitFlashState& FlashState, const FTDHitFlashRequest& Request);
	void DrainDeferredFeedback(double Now);
	bool PlayFeedbackSound(const FTDFeedbackSoundSet& SoundSet, const FVector& Location, float VolumeScale, ETDFeedbackSoundChannel Channel);
	USoundBase* PickSound(const FTDFeedbackSoundSet& SoundSet);
	bool StartCharacterHitStop(ATDCombatCharacter& Character, float Seconds);

	void UpdateOverlayOwner();
	void UpdateHitFlashes(double Now);
	void UpdateWobbles(double Now);
	void ReleaseHitFlash(FTDHitFlashState& FlashState);
	void RestorePresentation();

	FTDHitFlashState* FindHitFlash(const ATDCombatCharacter& Character);
	APlayerController* FindLocalPlayerController() const;
	FVector ResolveHitLocation(const FTDDamageAppliedMessage& Message) const;
	float ResolveRespawnDelaySeconds() const;
	double GetFeedbackTime() const;

	UPROPERTY(Transient)
	TObjectPtr<UTDCombatOverlayWidget> OverlayWidget;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> OverlayOwner;

	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> ImpactSoundConcurrency;

	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> AccentSoundConcurrency;

	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> SwingSoundConcurrency;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SoundAttenuation;

	UPROPERTY(Transient)
	TArray<FTDHitFlashState> HitFlashes;

	TArray<FTDWobbleState> Wobbles;
	TTDDeferredFeedbackQueue<FTDDamageNumberRequest> DeferredNumbers;
	TTDDeferredFeedbackQueue<FTDHitFlashRequest> DeferredFlashes;
	TSharedPtr<FStreamableHandle> FeedbackAssetsHandle;
	FGameplayMessageListenerHandle DamageAppliedListener;
	FGameplayMessageListenerHandle DamageAvoidedListener;
	FGameplayMessageListenerHandle ActorDeathListener;
	FGameplayMessageListenerHandle MeleeSwingListener;
	FTDCombatFeedbackStats Stats;
	FRandomStream FeedbackRandom{ 0x7D1A };
	int32 NumbersThisFrame = 0;
	int32 EffectsThisFrame = 0;
	int32 SoundsThisFrame = 0;
	int32 OverlayChangesThisFrame = 0;
	bool bIsListening = false;
	bool bAreFeedbackAssetsLoaded = false;
};
