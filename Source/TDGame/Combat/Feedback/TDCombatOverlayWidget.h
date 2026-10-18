#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Combat/Feedback/TDDamageNumberPool.h"
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

	int32 CountActiveDamageNumbers() const { return NumberPool.CountActive(); }
	bool IsDeathScreenShown() const { return bIsDeathScreenShown; }

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
	void BuildDeathScreen();

	void UpdateDamageNumbers(const APlayerController& Controller, double Now);
	void UpdateMonsterHealthBars(const APlayerController& Controller, double Now, float DeltaSeconds);
	void UpdatePlayerHud(APawn* Pawn, double Now, float DeltaSeconds);
	void UpdateDamageVignette(double Now);
	void EnsureDamageVignetteMaterial(const UTDCombatFeedbackSettings& FeedbackSettings);
	float ComputeRemainingHitVignette(double Now) const;
	void UpdateDeathScreen(APawn* Pawn, double Now);

	void RefreshDamageNumberText(FTDDamageNumberWidgetSlot& NumberSlot, const FTDDamageNumberEntry& Entry) const;
	void SetDamageNumberVisible(FTDDamageNumberWidgetSlot& NumberSlot, bool bShouldBeVisible) const;
	void SetMonsterHealthBarVisible(FTDMonsterHealthBarWidgetSlot& BarSlot, bool bShouldBeVisible) const;
	void ReleaseMonsterHealthBar(FTDMonsterHealthBarWidgetSlot& BarSlot) const;
	void SetPlayerHudVisible(bool bShouldBeVisible);
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

	TWeakObjectPtr<APawn> HudPawn;
	TWeakObjectPtr<APawn> DeathPawn;
	FTDDamageNumberPool NumberPool;
	FVector2D OverlayViewportSize = FVector2D::ZeroVector;
	double PlayerHealthHoldUntil = 0.;
	double DamageVignetteStartTime = 0.;
	double DeathScreenStartTime = 0.;
	double RespawnTime = 0.;
	float PlayerHealthFraction = 1.f;
	float PlayerHealthDelayedFraction = 1.f;
	float DamageVignettePeakIntensity = 0.f;
	int32 ShownHealthValue = INDEX_NONE;
	int32 ShownMaxHealthValue = INDEX_NONE;
	int32 ShownCountdownSeconds = INDEX_NONE;
	bool bIsPlayerHudVisible = false;
	bool bIsDamageVignetteVisible = false;
	bool bIsHudPawnAlive = false;
	bool bIsDeathScreenShown = false;
};
