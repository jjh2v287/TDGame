#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Feedback/TDDamageNumberPool.h"
#include "Combat/Feedback/TDDeferredFeedbackQueue.h"
#include "Combat/Feedback/TDHitCameraShake.h"
#include "Combat/Feedback/TDHitFeelRules.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDCombatFeedbackSubsystem.generated.h"

class AActor;
class APawn;
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
class UTDTraumaCameraShakePattern;
struct FStreamableHandle;
struct FTDActorDeathMessage;
struct FTDBossHealthBarMessage;
struct FTDDamageAppliedMessage;
struct FTDDamageAvoidedMessage;
struct FTDEncounterBannerMessage;
struct FTDFeedbackSoundSet;
struct FTDMeleeSwingMessage;
struct FTDMonsterPhaseMessage;

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
	Swing,
	MonsterSwing,
	PlayerHit,
	StatusTick
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

struct FTDHitFeel
{
	ETDHitFeelTier Tier = ETDHitFeelTier::Light;
	ETDHitFeelTier TraumaTier = ETDHitFeelTier::Light;
	ETDKillRank KillRank = ETDKillRank::None;
	float UnclampedDamageRatio = 0.f;
	int32 ComboStageIndex = INDEX_NONE;
	bool bIsPlayerMelee = false;
	bool bIsStrongSource = false;
};

struct FTDHitFlashRequest
{
	TWeakObjectPtr<ATDCombatCharacter> Victim;
	FLinearColor Color = FLinearColor::White;
	float PeakIntensity = 1.f;
	float DurationSeconds = 0.f;
	bool bIsLocalVictim = false;
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

	UPROPERTY()
	int32 BossHealthBarsShown = 0;

	UPROPERTY()
	int32 EncounterBannersShown = 0;

	UPROPERTY()
	int32 BossPhasePulses = 0;
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
	float DurationSeconds = 0.f;
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
	static float ComputeDealtTrauma(ETDHitFeelTier Tier, int32 KillCountInSwing, ETDKillRank KillRank);
	static float ComputeReceivedTrauma(float AccumulatedRatio, bool bIsDeath);

	void CreateSoundSettings(const UTDCombatFeedbackSettings& FeedbackSettings);
	USoundConcurrency* GetSoundConcurrency(ETDFeedbackSoundChannel Channel) const;
	const FTDCombatFeedbackStats& GetStats() const { return Stats; }
	bool IsListening() const { return bIsListening; }
	bool AreFeedbackAssetsLoaded() const { return bAreFeedbackAssetsLoaded; }
	UTDCombatOverlayWidget* GetOverlayWidget() const { return OverlayWidget.Get(); }
	void RequestSlowMotion(float Scale, float RealSeconds);
	bool IsSlowMotionActive() const { return SlowMotion.IsActive(); }
	float GetCameraTraumaForTest() const;
	FVector2D GetLastShakeScreenOffsetForTest() const;
	int32 GetFlashCountForTest() const { return Stats.FlashesStarted; }
	int32 GetKickCountForTest() const { return KickCount; }

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
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
		double JitterStartRealTime = 0.;
		float Amplitude = 0.f;
		float JitterAmplitude = 0.f;
		float JitterSeconds = 0.f;
		bool bFollowsHitStop = false;
	};

	struct FTDPendingBossHealthBar
	{
		TWeakObjectPtr<ATDCombatCharacter> Boss;
		FText DisplayName;
		TArray<float> PhaseHealthMarks;
	};

	struct FTDPendingSlowMotion
	{
		double StartRealTime = 0.;
		float Scale = 1.f;
		float RealSeconds = 0.f;
	};

	void RegisterListeners(UWorld& InWorld);
	void UnregisterListeners();
	USoundConcurrency* CreateSoundConcurrency(int32 MaxCount, float RetriggerSeconds);
	void RequestFeedbackAssets(const UTDCombatFeedbackSettings& FeedbackSettings);
	void HandleFeedbackAssetsLoaded();

	void HandleDamageApplied(FGameplayTag Channel, const FTDDamageAppliedMessage& Message);
	void HandleDamageAvoided(FGameplayTag Channel, const FTDDamageAvoidedMessage& Message);
	void HandleActorDeath(FGameplayTag Channel, const FTDActorDeathMessage& Message);
	void HandleMeleeSwing(FGameplayTag Channel, const FTDMeleeSwingMessage& Message);
	void HandleEncounterBanner(FGameplayTag Channel, const FTDEncounterBannerMessage& Message);
	void HandleBossHealthBar(FGameplayTag Channel, const FTDBossHealthBarMessage& Message);
	void HandleMonsterPhaseEntered(FGameplayTag Channel, const FTDMonsterPhaseMessage& Message);

	FTDHitFeel DescribeHitFeel(const FTDDamageAppliedMessage& Message) const;
	void ShowHitNumber(const FTDDamageAppliedMessage& Message, const FTDHitPresentation& Presentation, const FVector& Location, bool bIsLocalVictim);
	bool SpawnImpactEffects(const FTDDamageAppliedMessage& Message, const FVector& Location, const FVector& Direction);
	void DeactivateLoopingEffectLater(UNiagaraComponent& Effect, float DelaySeconds);
	void PlayImpactSounds(const FTDDamageAppliedMessage& Message, const FTDHitPresentation& Presentation, const FVector& Location, bool bIsLocalVictim);
	void PlayMonsterSwingSound(const AActor& Attacker, const APawn* LocalPawn, const FVector& Location);
	void PlayBossPhaseAccent(const AActor& Monster);
	void StartHitFlash(ATDCombatCharacter& Victim, const FTDHitPresentation& Presentation, const FTDHitFeel& HitFeel, bool bWasCritical, bool bIsLocalVictim);
	void StartWobble(ATDCombatCharacter& Victim, ETDHitFeedbackTier Tier, const FVector& Direction);
	void ApplyHitStops(const FTDDamageAppliedMessage& Message, const FTDHitPresentation& Presentation, const FTDHitFeel& HitFeel, ATDCombatCharacter* Victim, bool bIsLocalVictim);
	void StartVictimPoseStop(ATDCombatCharacter& Victim, float RealSeconds);
	void StartAttackerPlayRateStop(ATDCombatCharacter& Attacker, float RealSeconds);
	void StartHitStopJitter(ATDCombatCharacter& Victim, float RealSeconds, bool bFollowsHitStop);
	void ApplyPendingAttackerHitStop();
	void PlayReceivedCameraFeedback(APlayerController& Controller, const FTDDamageAppliedMessage& Message, const FTDHitFeel& HitFeel);
	void PlayDealtCameraFeedback(APlayerController& Controller, const FTDDamageAppliedMessage& Message, const FTDHitFeel& HitFeel);
	void AddCameraTrauma(APlayerController& Controller, ETDShakeChannel Channel, float Amount, float ChannelCap, bool bAllowsFullTrauma, const FRotator& KickRotation);
	void PlayRumble(APlayerController& Controller, float Intensity, float Seconds, bool bUsesLargeMotors);
	void ScheduleBossKillSlowMotion(float AttackerStopSeconds);
	void RequestSlowMotionWithRule(float Scale, float RealSeconds, bool bIgnoresCooldown, bool bStartsCooldown);
	void UpdateSlowMotion();
	void ApplySlowMotionDilation();
	void RestoreSlowMotion();
	void ShowMonsterHealthBar(ATDCombatCharacter& Victim, const FTDDamageAppliedMessage& Message);

	void AddDamageNumber(const FTDDamageNumberRequest& Request);
	bool TryShowDamageNumber(const FTDDamageNumberRequest& Request);
	bool TryStartHitFlash(const FTDHitFlashRequest& Request);
	void RefreshHitFlash(FTDHitFlashState& FlashState, const FTDHitFlashRequest& Request);
	void DrainDeferredFeedback(double Now);
	bool PlayFeedbackSound(const FTDFeedbackSoundSet& SoundSet, const FVector& Location, float VolumeScale, ETDFeedbackSoundChannel Channel);
	USoundBase* PickSound(const FTDFeedbackSoundSet& SoundSet);
	float ResolveBurstVolumeScale(const FTDFeedbackSoundSet& SoundSet);

	void UpdateOverlayOwner();
	void RestorePendingBossHealthBar();
	void UpdateHitFlashes(double Now);
	void UpdateWobbles(double Now);
	bool UpdateHitStopJitter(FTDWobbleState& WobbleState, ATDCombatCharacter& Character, double RealNow);
	void ReleaseHitFlash(FTDHitFlashState& FlashState);
	void RestorePresentation();

	FTDHitFlashState* FindHitFlash(const ATDCombatCharacter& Character);
	FTDWobbleState* FindWobble(const ATDCombatCharacter& Character);
	APlayerController* FindLocalPlayerController() const;
	FRotator ComputeKickRotation(const APlayerController& Controller, const FVector& HitDirection, float DamageRatio) const;
	float ResolveCameraArmLength(const APlayerController* Controller) const;
	FVector ResolveHitLocation(const FTDDamageAppliedMessage& Message) const;
	float ResolveRespawnDelaySeconds() const;
	double GetFeedbackTime() const;
	double GetFeedbackRealTime() const;

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
	TObjectPtr<USoundConcurrency> MonsterSwingSoundConcurrency;

	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> PlayerHitSoundConcurrency;

	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> StatusTickSoundConcurrency;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SoundAttenuation;

	UPROPERTY(Transient)
	TArray<FTDHitFlashState> HitFlashes;

	TArray<FTDWobbleState> Wobbles;
	TArray<FTDPendingSlowMotion> PendingSlowMotions;
	TMap<const FTDFeedbackSoundSet*, FTDSoundBurstLimiter> SoundBursts;
	FTDSlowMotionArbiter SlowMotion;
	FTDFlashRateLimiter PlayerFlashLimiter;
	TWeakObjectPtr<UTDTraumaCameraShakePattern> ActiveShakePattern;
	TWeakObjectPtr<ATDCombatCharacter> SwingAttacker;
	FTDTraumaWindow DealtWindow;
	FTDReceivedHitAccumulator ReceivedHits;
	TTDDeferredFeedbackQueue<FTDDamageNumberRequest> DeferredNumbers;
	TTDDeferredFeedbackQueue<FTDHitFlashRequest> DeferredFlashes;
	TSharedPtr<FStreamableHandle> FeedbackAssetsHandle;
	FGameplayMessageListenerHandle DamageAppliedListener;
	FGameplayMessageListenerHandle DamageAvoidedListener;
	FGameplayMessageListenerHandle ActorDeathListener;
	FGameplayMessageListenerHandle MeleeSwingListener;
	FGameplayMessageListenerHandle EncounterBannerListener;
	FGameplayMessageListenerHandle BossHealthBarListener;
	FGameplayMessageListenerHandle MonsterPhaseListener;
	FTDPendingBossHealthBar PendingBossHealthBar;
	FTDCombatFeedbackStats Stats;
	FRandomStream FeedbackRandom{ 0x7D1A };
	int32 KickCount = 0;
	int32 StatusSoundsThisFrame = 0;
	int32 NumbersThisFrame = 0;
	int32 EffectsThisFrame = 0;
	int32 SoundsThisFrame = 0;
	int32 OverlayChangesThisFrame = 0;
	bool bIsListening = false;
	bool bAreFeedbackAssetsLoaded = false;
};
