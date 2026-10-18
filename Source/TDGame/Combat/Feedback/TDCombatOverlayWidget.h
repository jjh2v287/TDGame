#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Combat/Feedback/TDDamageNumberPool.h"
#include "Core/TDGameplayMessages.h"
#include "TDCombatOverlayWidget.generated.h"

class APawn;
class APlayerController;
class ATDCombatCharacter;
class UCanvasPanel;
class UImage;
class UMaterialInstanceDynamic;
class UProgressBar;
class UTDCombatFeedbackSettings;
class UTextBlock;

USTRUCT()
struct FTDDamageNumberWidgetSlot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text;

	uint32 AppliedSerial = 0;
	bool bIsVisible = false;
};

USTRUCT()
struct FTDMonsterHealthBarWidgetSlot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> DelayedBar;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> CurrentBar;

	UPROPERTY(Transient)
	TWeakObjectPtr<ATDCombatCharacter> Character;

	double ExpireTime = 0.;
	double DelayedHoldUntil = 0.;
	float DelayedFraction = 0.f;
	bool bIsAssigned = false;
	bool bIsShowingKill = false;
	bool bIsVisible = false;
};

UCLASS()
class TDGAME_API UTDCombatOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static float ComputeHitVignetteIntensity(float DamageRatio, bool bIsStatus, const UTDCombatFeedbackSettings& FeedbackSettings);
	static float ComputeLowHealthVignetteIntensity(float HealthFraction, double Now, const UTDCombatFeedbackSettings& FeedbackSettings);
	static float ComputeFallbackTintAlpha(float VignetteIntensity, const UTDCombatFeedbackSettings& FeedbackSettings);

	int32 ShowDamageNumber(const FTDDamageNumberRequest& Request);
	bool CanMergeDamageNumber(const FTDDamageNumberRequest& Request) const;
	void ShowMonsterHealthBar(ATDCombatCharacter* Character, float HealthFractionBeforeHit, bool bWasKilled);
	void PulseDamageVignette(float Intensity);
	void ShowDeathScreen(APawn* DeadPawn, float RespawnDelaySeconds);
	void ShowBossHealthBar(ATDCombatCharacter* Boss, const FText& DisplayName, const TArray<float>& PhaseHealthMarks);
	void HideBossHealthBar();
	void PulseBossPhase(int32 PhaseIndex);
	void ShowEncounterBanner(const FText& Title, const FText& Subtitle, ETDEncounterBannerStyle Style, float DisplaySeconds);

	int32 CountActiveDamageNumbers() const { return NumberPool.CountActive(); }
	bool IsDeathScreenShown() const { return bIsDeathScreenShown; }
	bool IsBossHealthBarShown() const { return bIsBossHealthBarShown; }
	ATDCombatCharacter* GetBossHealthBarTarget() const;
	float GetBossHealthBarFraction() const { return BossHealthFraction; }
	int32 GetBossPhaseMarkCount() const { return ShownBossPhaseMarkCount; }
	bool IsEncounterBannerShown() const { return bIsEncounterBannerShown; }
	FText GetEncounterBannerTitle() const { return EncounterBannerTitle; }
	ETDEncounterBannerStyle GetEncounterBannerStyle() const { return EncounterBannerStyle; }

protected:
	virtual void NativeOnInitialized() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildWidgetTree();
	void BuildDamageVignette();
	void BuildMonsterHealthBars(int32 BarCount);
	void BuildDamageNumbers(int32 NumberCount);
	void BuildPlayerHud();
	void BuildBossHealthBar();
	void BuildEncounterBanner();
	void BuildDeathScreen();

	void UpdateDamageNumbers(const APlayerController& Controller, double Now);
	void UpdateMonsterHealthBars(const APlayerController& Controller, double Now, float DeltaSeconds);
	void UpdatePlayerHud(APawn* Pawn, double Now, float DeltaSeconds);
	void UpdateBossHealthBar(double Now, float DeltaSeconds);
	void UpdateEncounterBanner(double Now);
	void UpdateDamageVignette(double Now);
	void EnsureDamageVignetteMaterial(const UTDCombatFeedbackSettings& FeedbackSettings);
	float ComputeRemainingHitVignette(double Now) const;
	void UpdateDeathScreen(APawn* Pawn, double Now);

	void RefreshDamageNumberText(FTDDamageNumberWidgetSlot& NumberSlot, const FTDDamageNumberEntry& Entry) const;
	void SetDamageNumberVisible(FTDDamageNumberWidgetSlot& NumberSlot, bool bShouldBeVisible) const;
	void SetMonsterHealthBarVisible(FTDMonsterHealthBarWidgetSlot& BarSlot, bool bShouldBeVisible) const;
	void ReleaseMonsterHealthBar(FTDMonsterHealthBarWidgetSlot& BarSlot) const;
	void SetPlayerHudVisible(bool bShouldBeVisible);
	void SetBossHealthBarVisible(bool bShouldBeVisible);
	void ApplyBossPhaseMarks(const TArray<float>& PhaseHealthMarks);
	void FoldBossHealthBar();
	void HideEncounterBanner();
	bool ProjectToOverlay(const APlayerController& Controller, const FVector& WorldLocation, float EdgeMargin, FVector2D& OutPosition) const;
	double GetOverlayTime() const;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient)
	TObjectPtr<UImage> DamageVignetteImage;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DamageVignetteMaterialInstance;

	UPROPERTY(Transient)
	TArray<FTDMonsterHealthBarWidgetSlot> MonsterHealthBars;

	UPROPERTY(Transient)
	TArray<FTDDamageNumberWidgetSlot> DamageNumberSlots;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> PlayerHealthDelayedBar;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> PlayerHealthBar;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> PlayerStaminaBar;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PlayerHealthText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DeathTitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DeathCountdownText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> BossNameText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> BossHealthDelayedBar;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> BossHealthBar;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> BossPhaseMarkImages;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EncounterBannerTitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EncounterBannerSubtitleText;

	TWeakObjectPtr<APawn> HudPawn;
	TWeakObjectPtr<APawn> DeathPawn;
	TWeakObjectPtr<ATDCombatCharacter> BossHealthBarTarget;
	FTDDamageNumberPool NumberPool;
	FText BossDisplayName;
	FText EncounterBannerTitle;
	FVector2D OverlayViewportSize = FVector2D::ZeroVector;
	double PlayerHealthHoldUntil = 0.;
	double DamageVignetteStartTime = 0.;
	double DeathScreenStartTime = 0.;
	double RespawnTime = 0.;
	double BossHealthHoldUntil = 0.;
	double BossFoldTime = 0.;
	double EncounterBannerStartTime = 0.;
	float PlayerHealthFraction = 1.f;
	float PlayerHealthDelayedFraction = 1.f;
	float DamageVignettePeakIntensity = 0.f;
	float BossHealthFraction = 1.f;
	float BossHealthDelayedFraction = 1.f;
	float EncounterBannerSeconds = 0.f;
	int32 ShownHealthValue = INDEX_NONE;
	int32 ShownMaxHealthValue = INDEX_NONE;
	int32 ShownCountdownSeconds = INDEX_NONE;
	int32 ShownBossPhaseMarkCount = 0;
	ETDEncounterBannerStyle EncounterBannerStyle = ETDEncounterBannerStyle::Wave;
	bool bIsPlayerHudVisible = false;
	bool bIsDamageVignetteVisible = false;
	bool bIsHudPawnAlive = false;
	bool bIsDeathScreenShown = false;
	bool bIsBossHealthBarShown = false;
	bool bIsBossHealthBarFolding = false;
	bool bIsEncounterBannerShown = false;
};
