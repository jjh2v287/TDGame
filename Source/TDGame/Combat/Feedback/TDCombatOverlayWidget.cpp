#include "Combat/Feedback/TDCombatOverlayWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Characters/TDCombatCharacter.h"
#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Combat/TDCombatComponent.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CapsuleComponent.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

namespace TDCombatOverlayWidgetInternal
{
	const FVector2D MonsterBarSize(84.f, 7.f);
	const FVector2D PlayerHealthBarSize(360.f, 18.f);
	const FVector2D PlayerStaminaBarSize(360.f, 6.f);
	const FVector2D PlayerHealthBarPosition(0.f, -40.f);
	const FVector2D PlayerStaminaBarPosition(0.f, -30.f);
	const FVector2D PlayerHealthTextPosition(0.f, -49.f);
	const FVector2D DeathTitlePosition(0.f, -24.f);
	const FVector2D DeathCountdownPosition(0.f, 28.f);
	const FLinearColor BarBackgroundColor(0.f, 0.f, 0.f, 0.6f);
	const FLinearColor DelayedBarColor(1.f, 1.f, 1.f, 0.85f);
	const FLinearColor MonsterHealthColor(0.9f, 0.15f, 0.1f, 1.f);
	const FLinearColor PlayerHealthColor(0.8f, 0.08f, 0.08f, 1.f);
	const FLinearColor PlayerHealthPulseColor(1.f, 0.45f, 0.35f, 1.f);
	const FLinearColor PlayerStaminaColor(0.95f, 0.8f, 0.2f, 1.f);
	const FVector2D BossNamePosition(0.f, 30.f);
	const FVector2D BossBarPosition(0.f, 52.f);
	const FVector2D BossBarSize(640.f, 16.f);
	const FVector2D BossPhaseMarkSize(3.f, 24.f);
	const FVector2D EncounterBannerTitlePosition(0.f, 0.f);
	const FVector2D EncounterBannerSubtitlePosition(0.f, 46.f);
	const FLinearColor BossHealthColor(0.85f, 0.2f, 0.08f, 1.f);
	const FLinearColor BossPhaseMarkColor(1.f, 0.85f, 0.4f, 0.95f);
	const FLinearColor EncounterWaveColor(1.f, 1.f, 1.f, 1.f);
	const FLinearColor EncounterBossColor(1.f, 0.55f, 0.25f, 1.f);
	const FLinearColor EncounterVictoryColor(1.f, 0.85f, 0.3f, 1.f);
	const FLinearColor EncounterDefeatColor(0.95f, 0.25f, 0.2f, 1.f);
	constexpr float BossNameFontSize = 18.f;
	constexpr float BossFoldDelaySeconds = 1.f;
	constexpr float BossPhaseBannerSeconds = 2.5f;
	constexpr float EncounterBannerAnchorY = 0.28f;
	constexpr float EncounterBannerTitleFontSize = 40.f;
	constexpr float EncounterBannerSubtitleFontSize = 20.f;
	constexpr float EncounterBannerFadeInSeconds = 0.25f;
	constexpr float EncounterBannerFadeOutSeconds = 0.5f;
	constexpr float EncounterBannerMinSeconds = 0.5f;
	constexpr int32 BossPhaseMarkCapacity = 4;
	constexpr float MonsterBarHeadClearanceCm = 25.f;
	constexpr float DelayedBarHoldSeconds = 0.35f;
	constexpr float DelayedBarDrainPerSecond = 0.6f;
	constexpr float LowHealthFraction = 0.25f;
	constexpr float LowHealthPulseHz = 1.5f;
	constexpr float NumberEdgeMargin = 40.f;
	constexpr float BarEdgeMargin = 60.f;
	constexpr float DeathFadeSeconds = 0.5f;
	constexpr float DeathTitleFontSize = 44.f;
	constexpr float DeathCountdownFontSize = 22.f;
	constexpr float HudTextFontSize = 12.f;
	constexpr int32 TextOutlineSize = 2;

	FSlateFontInfo MakeOverlayFont(const float Size)
	{
		FFontOutlineSettings Outline;
		Outline.OutlineSize = TextOutlineSize;
		Outline.OutlineColor = FLinearColor::Black;
		return FCoreStyle::GetDefaultFontStyle(FName(TEXT("Bold")), Size, Outline);
	}

	FProgressBarStyle MakeOverlayBarStyle(const FLinearColor& BackgroundColor)
	{
		FProgressBarStyle Style = FProgressBarStyle::GetDefault();
		Style.SetBackgroundImage(FSlateColorBrush(BackgroundColor));
		Style.SetFillImage(FSlateColorBrush(FLinearColor::White));
		Style.SetMarqueeImage(FSlateColorBrush(FLinearColor::Transparent));
		Style.SetEnableFillAnimation(false);
		return Style;
	}

	UProgressBar* AddOverlayBar(UWidgetTree& Tree, UCanvasPanel& Canvas, const FProgressBarStyle& Style, const FLinearColor& FillColor, const FAnchors& Anchors, const FVector2D& Alignment, const FVector2D& Position, const FVector2D& Size)
	{
		UProgressBar* Bar = Tree.ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		Bar->SetWidgetStyle(Style);
		Bar->SetFillColorAndOpacity(FillColor);
		Bar->SetPercent(1.f);
		UCanvasPanelSlot* BarSlot = Canvas.AddChildToCanvas(Bar);
		BarSlot->SetAnchors(Anchors);
		BarSlot->SetAlignment(Alignment);
		BarSlot->SetPosition(Position);
		BarSlot->SetSize(Size);
		return Bar;
	}

	UTextBlock* AddOverlayText(UWidgetTree& Tree, UCanvasPanel& Canvas, const float FontSize, const FAnchors& Anchors, const FVector2D& Position)
	{
		UTextBlock* Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetFont(MakeOverlayFont(FontSize));
		Text->SetJustification(ETextJustify::Center);
		UCanvasPanelSlot* TextSlot = Canvas.AddChildToCanvas(Text);
		TextSlot->SetAnchors(Anchors);
		TextSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		TextSlot->SetPosition(Position);
		TextSlot->SetAutoSize(true);
		return Text;
	}

	float StepDelayedFraction(const float Displayed, const float Current, const double Now, const double HoldUntil, const float DeltaSeconds)
	{
		if (Current >= Displayed)
		{
			return Current;
		}
		if (Now < HoldUntil)
		{
			return Displayed;
		}
		return FMath::Max(Current, Displayed - DelayedBarDrainPerSecond * FMath::Max(DeltaSeconds, 0.f));
	}

	float ComputeHealthFraction(const UTDCombatComponent& Combat)
	{
		const float MaxHealth = Combat.GetMaxHealth();
		if (MaxHealth <= 0.f)
		{
			return 0.f;
		}
		return FMath::Clamp(Combat.GetCurrentHealth() / MaxHealth, 0.f, 1.f);
	}

	float ComputeCharacterHealthFraction(const ATDCombatCharacter* Character)
	{
		const UTDCombatComponent* Combat = Character ? Character->GetCombatComponent() : nullptr;
		return Combat ? ComputeHealthFraction(*Combat) : 0.f;
	}

	FLinearColor GetEncounterBannerColor(const ETDEncounterBannerStyle Style)
	{
		switch (Style)
		{
		case ETDEncounterBannerStyle::Boss:
			return EncounterBossColor;
		case ETDEncounterBannerStyle::Victory:
			return EncounterVictoryColor;
		case ETDEncounterBannerStyle::Defeat:
			return EncounterDefeatColor;
		default:
			return EncounterWaveColor;
		}
	}

	bool IsPawnAlive(const APawn* Pawn)
	{
		const ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(Pawn);
		const UTDCombatComponent* Combat = Character ? Character->GetCombatComponent() : nullptr;
		return !Combat || Combat->IsAlive();
	}

	float GetNumberFontSize(const UTDCombatFeedbackSettings& Settings, const ETDDamageNumberStyle Style)
	{
		switch (Style)
		{
		case ETDDamageNumberStyle::Critical:
			return Settings.CriticalNumberFontSize;
		case ETDDamageNumberStyle::Kill:
			return Settings.KillNumberFontSize;
		case ETDDamageNumberStyle::Status:
			return Settings.StatusNumberFontSize;
		case ETDDamageNumberStyle::PlayerDamage:
			return Settings.PlayerDamageNumberFontSize;
		default:
			return Settings.NormalNumberFontSize;
		}
	}

	FLinearColor GetNumberColor(const UTDCombatFeedbackSettings& Settings, const FTDDamageNumberEntry& Entry)
	{
		if (Entry.Style == ETDDamageNumberStyle::Avoided)
		{
			return Settings.AvoidedNumberColor;
		}
		if (Entry.bIsPlayerTarget)
		{
			return Settings.PlayerDamageNumberColor;
		}
		if (Entry.bWasCritical)
		{
			return Settings.CriticalNumberColor;
		}
		return Settings.GetElementNumberColor(Entry.Element);
	}

	FText MakeNumberLabel(const FTDDamageNumberEntry& Entry)
	{
		if (Entry.Style == ETDDamageNumberStyle::Avoided)
		{
			return FText::FromString(TEXT("회피"));
		}
		return FText::AsNumber(FMath::Max(1, FMath::RoundToInt(Entry.Amount)));
	}
}

float UTDCombatOverlayWidget::ComputeHitVignetteIntensity(const float DamageRatio, const bool bIsStatus, const UTDCombatFeedbackSettings& FeedbackSettings)
{
	const float FullRatio = FMath::Max(FeedbackSettings.DamageVignetteFullRatio, KINDA_SMALL_NUMBER);
	const float Strength = FMath::Clamp(FMath::Max(DamageRatio, 0.f) / FullRatio, FeedbackSettings.DamageVignetteMinStrength, 1.f);
	const float StatusScale = bIsStatus ? FeedbackSettings.StatusVignetteScale : 1.f;
	return Strength * FeedbackSettings.DamageVignetteMaxIntensity * StatusScale;
}

float UTDCombatOverlayWidget::ComputeLowHealthVignetteIntensity(const float HealthFraction, const double Now, const UTDCombatFeedbackSettings& FeedbackSettings)
{
	if (HealthFraction <= 0.f || HealthFraction >= FeedbackSettings.LowHealthVignetteFraction)
	{
		return 0.f;
	}

	const double PeriodSeconds = FMath::Max(FeedbackSettings.LowHealthVignettePeriodSeconds, 0.1f);
	const double PhaseRadians = UE_DOUBLE_TWO_PI * FMath::Fmod(FMath::Max(Now, 0.), PeriodSeconds) / PeriodSeconds;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(PhaseRadians));
	return FMath::Lerp(FeedbackSettings.LowHealthVignetteMinIntensity, FeedbackSettings.LowHealthVignetteMaxIntensity, Pulse);
}

float UTDCombatOverlayWidget::ComputeFallbackTintAlpha(const float VignetteIntensity, const UTDCombatFeedbackSettings& FeedbackSettings)
{
	if (FeedbackSettings.DamageVignetteMaxIntensity <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	return FMath::Clamp(VignetteIntensity / FeedbackSettings.DamageVignetteMaxIntensity, 0.f, 1.f) * FeedbackSettings.FallbackDamageTintAlpha;
}

int32 UTDCombatOverlayWidget::ShowDamageNumber(const FTDDamageNumberRequest& Request)
{
	if (DamageNumberSlots.IsEmpty())
	{
		return INDEX_NONE;
	}
	return NumberPool.Add(Request, GetOverlayTime());
}

bool UTDCombatOverlayWidget::CanMergeDamageNumber(const FTDDamageNumberRequest& Request) const
{
	return !DamageNumberSlots.IsEmpty() && NumberPool.CanMergeStatus(Request, GetOverlayTime());
}

void UTDCombatOverlayWidget::ShowMonsterHealthBar(ATDCombatCharacter* Character, const float HealthFractionBeforeHit, const bool bWasKilled)
{
	if (!Character || MonsterHealthBars.IsEmpty())
	{
		return;
	}

	FTDMonsterHealthBarWidgetSlot* ChosenBar = nullptr;
	FTDMonsterHealthBarWidgetSlot* FreeBar = nullptr;
	FTDMonsterHealthBarWidgetSlot* OldestBar = nullptr;
	for (FTDMonsterHealthBarWidgetSlot& BarSlot : MonsterHealthBars)
	{
		if (BarSlot.bIsAssigned && BarSlot.Character.Get() == Character)
		{
			ChosenBar = &BarSlot;
			break;
		}
		if (!BarSlot.bIsAssigned)
		{
			FreeBar = FreeBar ? FreeBar : &BarSlot;
			continue;
		}
		if (!OldestBar || BarSlot.ExpireTime < OldestBar->ExpireTime)
		{
			OldestBar = &BarSlot;
		}
	}

	if (!ChosenBar)
	{
		if (bWasKilled)
		{
			return;
		}
		ChosenBar = FreeBar ? FreeBar : OldestBar;
		if (!ChosenBar)
		{
			return;
		}
		ChosenBar->Character = Character;
		ChosenBar->bIsAssigned = true;
		ChosenBar->bIsShowingKill = false;
		ChosenBar->DelayedFraction = 0.f;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const double Now = GetOverlayTime();
	ChosenBar->DelayedFraction = FMath::Max(ChosenBar->DelayedFraction, FMath::Clamp(HealthFractionBeforeHit, 0.f, 1.f));
	ChosenBar->DelayedHoldUntil = Now + TDCombatOverlayWidgetInternal::DelayedBarHoldSeconds;
	ChosenBar->bIsShowingKill = bWasKilled;
	ChosenBar->ExpireTime = Now + (bWasKilled ? FeedbackSettings.KilledMonsterBarSeconds : FeedbackSettings.MonsterBarSeconds);
}

void UTDCombatOverlayWidget::PulseDamageVignette(const float Intensity)
{
	const double Now = GetOverlayTime();
	DamageVignettePeakIntensity = FMath::Max(ComputeRemainingHitVignette(Now), FMath::Clamp(Intensity, 0.f, 1.f));
	DamageVignetteStartTime = Now;
}

void UTDCombatOverlayWidget::ShowDeathScreen(APawn* DeadPawn, const float RespawnDelaySeconds)
{
	if (!DeathTitleText || !DeathCountdownText)
	{
		return;
	}

	const double Now = GetOverlayTime();
	DeathPawn = DeadPawn;
	DeathScreenStartTime = Now;
	RespawnTime = Now + FMath::Max(RespawnDelaySeconds, 0.f);
	ShownCountdownSeconds = INDEX_NONE;
	bIsDeathScreenShown = true;
	DeathTitleText->SetRenderOpacity(0.f);
	DeathCountdownText->SetRenderOpacity(0.f);
	DeathTitleText->SetVisibility(ESlateVisibility::HitTestInvisible);
	DeathCountdownText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTDCombatOverlayWidget::ShowBossHealthBar(ATDCombatCharacter* Boss, const FText& DisplayName, const TArray<float>& PhaseHealthMarks)
{
	if (!Boss || !BossNameText || !BossHealthBar || !BossHealthDelayedBar)
	{
		return;
	}

	const bool bIsSameBoss = BossHealthBarTarget.Get() == Boss;
	BossHealthBarTarget = Boss;
	BossDisplayName = DisplayName;
	BossNameText->SetText(DisplayName);
	ApplyBossPhaseMarks(PhaseHealthMarks);
	bIsBossHealthBarFolding = false;
	if (!bIsSameBoss)
	{
		BossHealthFraction = TDCombatOverlayWidgetInternal::ComputeCharacterHealthFraction(Boss);
		BossHealthDelayedFraction = BossHealthFraction;
		BossHealthHoldUntil = 0.;
		BossHealthBar->SetPercent(BossHealthFraction);
		BossHealthDelayedBar->SetPercent(BossHealthDelayedFraction);
	}
	SetBossHealthBarVisible(true);
}

void UTDCombatOverlayWidget::HideBossHealthBar()
{
	if (!bIsBossHealthBarShown || bIsBossHealthBarFolding)
	{
		return;
	}

	bIsBossHealthBarFolding = true;
	BossFoldTime = GetOverlayTime() + TDCombatOverlayWidgetInternal::BossFoldDelaySeconds;
}

void UTDCombatOverlayWidget::PulseBossPhase(const int32 PhaseIndex)
{
	if (!bIsBossHealthBarShown)
	{
		return;
	}

	const FText PhaseLabel = FText::Format(FText::FromString(TEXT("{0}단계")), PhaseIndex + 1);
	ShowEncounterBanner(BossDisplayName, PhaseLabel, ETDEncounterBannerStyle::Boss, TDCombatOverlayWidgetInternal::BossPhaseBannerSeconds);
}

void UTDCombatOverlayWidget::ShowEncounterBanner(const FText& Title, const FText& Subtitle, const ETDEncounterBannerStyle Style, const float DisplaySeconds)
{
	using namespace TDCombatOverlayWidgetInternal;

	if (!EncounterBannerTitleText || !EncounterBannerSubtitleText)
	{
		return;
	}

	EncounterBannerStartTime = GetOverlayTime();
	EncounterBannerSeconds = FMath::Max(DisplaySeconds, EncounterBannerMinSeconds);
	EncounterBannerStyle = Style;
	EncounterBannerTitle = Title;
	bIsEncounterBannerShown = true;

	const FSlateColor BannerColor(GetEncounterBannerColor(Style));
	EncounterBannerTitleText->SetText(Title);
	EncounterBannerTitleText->SetColorAndOpacity(BannerColor);
	EncounterBannerTitleText->SetRenderOpacity(0.f);
	EncounterBannerTitleText->SetVisibility(ESlateVisibility::HitTestInvisible);
	EncounterBannerSubtitleText->SetText(Subtitle);
	EncounterBannerSubtitleText->SetColorAndOpacity(BannerColor);
	EncounterBannerSubtitleText->SetRenderOpacity(0.f);
	EncounterBannerSubtitleText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

ATDCombatCharacter* UTDCombatOverlayWidget::GetBossHealthBarTarget() const
{
	return BossHealthBarTarget.Get();
}

void UTDCombatOverlayWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

TSharedRef<SWidget> UTDCombatOverlayWidget::RebuildWidget()
{
	BuildWidgetTree();
	return Super::RebuildWidget();
}

void UTDCombatOverlayWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	APlayerController* Controller = GetOwningPlayer();
	if (!Controller || !RootCanvas)
	{
		return;
	}

	OverlayViewportSize = FVector2D(MyGeometry.GetLocalSize());
	const double Now = GetOverlayTime();
	APawn* Pawn = Controller->GetPawn();
	UpdateDamageNumbers(*Controller, Now);
	UpdateMonsterHealthBars(*Controller, Now, InDeltaTime);
	UpdatePlayerHud(Pawn, Now, InDeltaTime);
	UpdateBossHealthBar(Now, InDeltaTime);
	UpdateEncounterBanner(Now);
	UpdateDamageVignette(Now);
	UpdateDeathScreen(Pawn, Now);
}

void UTDCombatOverlayWidget::BuildWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	RootCanvas->SetVisibility(ESlateVisibility::HitTestInvisible);
	WidgetTree->RootWidget = RootCanvas.Get();

	FTDDamageNumberPoolSettings PoolSettings;
	PoolSettings.Capacity = FeedbackSettings.MaxActiveNumbers;
	PoolSettings.LifetimeSeconds = FeedbackSettings.NumberLifetimeSeconds;
	PoolSettings.RiseCm = FeedbackSettings.NumberRiseCm;
	PoolSettings.StatusMergeSeconds = FeedbackSettings.StatusNumberMergeSeconds;
	PoolSettings.StackWindowSeconds = FeedbackSettings.StackedNumberWindowSeconds;
	PoolSettings.StackOffset = FeedbackSettings.StackedNumberOffset;
	PoolSettings.ScatterRange = FeedbackSettings.NumberScatterRange;
	NumberPool.Configure(PoolSettings);

	BuildDamageVignette();
	BuildMonsterHealthBars(FeedbackSettings.MaxMonsterBars);
	BuildDamageNumbers(NumberPool.GetCapacity());
	BuildPlayerHud();
	BuildBossHealthBar();
	BuildEncounterBanner();
	BuildDeathScreen();
}

void UTDCombatOverlayWidget::BuildDamageVignette()
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	DamageVignetteImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	DamageVignetteImage->SetBrush(FSlateColorBrush(FLinearColor::White));
	DamageVignetteImage->SetColorAndOpacity(FeedbackSettings.DamageVignetteColor.CopyWithNewOpacity(0.f));
	DamageVignetteImage->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* VignetteSlot = RootCanvas->AddChildToCanvas(DamageVignetteImage.Get());
	VignetteSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	VignetteSlot->SetOffsets(FMargin(0.f));
	DamageVignetteMaterialInstance = nullptr;
	bIsDamageVignetteVisible = false;
}

void UTDCombatOverlayWidget::BuildMonsterHealthBars(const int32 BarCount)
{
	using namespace TDCombatOverlayWidgetInternal;

	const FProgressBarStyle DelayedStyle = MakeOverlayBarStyle(BarBackgroundColor);
	const FProgressBarStyle CurrentStyle = MakeOverlayBarStyle(FLinearColor::Transparent);
	const FAnchors TopLeft(0.f, 0.f);
	const FVector2D BottomCenter(0.5f, 1.f);
	MonsterHealthBars.SetNum(FMath::Max(BarCount, 0));
	for (FTDMonsterHealthBarWidgetSlot& BarSlot : MonsterHealthBars)
	{
		BarSlot.DelayedBar = AddOverlayBar(*WidgetTree, *RootCanvas, DelayedStyle, DelayedBarColor, TopLeft, BottomCenter, FVector2D::ZeroVector, MonsterBarSize);
		BarSlot.CurrentBar = AddOverlayBar(*WidgetTree, *RootCanvas, CurrentStyle, MonsterHealthColor, TopLeft, BottomCenter, FVector2D::ZeroVector, MonsterBarSize);
		BarSlot.bIsVisible = true;
		SetMonsterHealthBarVisible(BarSlot, false);
	}
}

void UTDCombatOverlayWidget::BuildDamageNumbers(const int32 NumberCount)
{
	using namespace TDCombatOverlayWidgetInternal;

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	DamageNumberSlots.SetNum(FMath::Max(NumberCount, 0));
	for (FTDDamageNumberWidgetSlot& NumberSlot : DamageNumberSlots)
	{
		NumberSlot.Text = AddOverlayText(*WidgetTree, *RootCanvas, FeedbackSettings.NormalNumberFontSize, FAnchors(0.f, 0.f), FVector2D::ZeroVector);
		NumberSlot.bIsVisible = true;
		SetDamageNumberVisible(NumberSlot, false);
	}
}

void UTDCombatOverlayWidget::BuildPlayerHud()
{
	using namespace TDCombatOverlayWidgetInternal;

	const FAnchors BottomAnchor(0.5f, 1.f);
	const FVector2D BottomCenter(0.5f, 1.f);
	PlayerHealthDelayedBar = AddOverlayBar(*WidgetTree, *RootCanvas, MakeOverlayBarStyle(BarBackgroundColor), DelayedBarColor, BottomAnchor, BottomCenter, PlayerHealthBarPosition, PlayerHealthBarSize);
	PlayerHealthBar = AddOverlayBar(*WidgetTree, *RootCanvas, MakeOverlayBarStyle(FLinearColor::Transparent), PlayerHealthColor, BottomAnchor, BottomCenter, PlayerHealthBarPosition, PlayerHealthBarSize);
	PlayerStaminaBar = AddOverlayBar(*WidgetTree, *RootCanvas, MakeOverlayBarStyle(BarBackgroundColor), PlayerStaminaColor, BottomAnchor, BottomCenter, PlayerStaminaBarPosition, PlayerStaminaBarSize);
	PlayerHealthText = AddOverlayText(*WidgetTree, *RootCanvas, HudTextFontSize, BottomAnchor, PlayerHealthTextPosition);
	bIsPlayerHudVisible = true;
	SetPlayerHudVisible(false);
}

void UTDCombatOverlayWidget::BuildBossHealthBar()
{
	using namespace TDCombatOverlayWidgetInternal;

	const FAnchors TopAnchor(0.5f, 0.f);
	const FVector2D TopCenter(0.5f, 0.f);
	BossNameText = AddOverlayText(*WidgetTree, *RootCanvas, BossNameFontSize, TopAnchor, BossNamePosition);
	BossHealthDelayedBar = AddOverlayBar(*WidgetTree, *RootCanvas, MakeOverlayBarStyle(BarBackgroundColor), DelayedBarColor, TopAnchor, TopCenter, BossBarPosition, BossBarSize);
	BossHealthBar = AddOverlayBar(*WidgetTree, *RootCanvas, MakeOverlayBarStyle(FLinearColor::Transparent), BossHealthColor, TopAnchor, TopCenter, BossBarPosition, BossBarSize);

	BossPhaseMarkImages.Reset();
	for (int32 MarkIndex = 0; MarkIndex < BossPhaseMarkCapacity; ++MarkIndex)
	{
		UImage* MarkImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		MarkImage->SetBrush(FSlateColorBrush(FLinearColor::White));
		MarkImage->SetColorAndOpacity(BossPhaseMarkColor);
		UCanvasPanelSlot* MarkSlot = RootCanvas->AddChildToCanvas(MarkImage);
		MarkSlot->SetAnchors(TopAnchor);
		MarkSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		MarkSlot->SetPosition(FVector2D(0.f, BossBarPosition.Y + BossBarSize.Y * 0.5f));
		MarkSlot->SetSize(BossPhaseMarkSize);
		BossPhaseMarkImages.Add(MarkImage);
	}

	BossHealthBarTarget.Reset();
	BossHealthFraction = 1.f;
	BossHealthDelayedFraction = 1.f;
	ShownBossPhaseMarkCount = 0;
	bIsBossHealthBarFolding = false;
	SetBossHealthBarVisible(false);
}

void UTDCombatOverlayWidget::BuildEncounterBanner()
{
	using namespace TDCombatOverlayWidgetInternal;

	const FAnchors BannerAnchor(0.5f, EncounterBannerAnchorY);
	EncounterBannerTitleText = AddOverlayText(*WidgetTree, *RootCanvas, EncounterBannerTitleFontSize, BannerAnchor, EncounterBannerTitlePosition);
	EncounterBannerTitleText->SetVisibility(ESlateVisibility::Collapsed);
	EncounterBannerSubtitleText = AddOverlayText(*WidgetTree, *RootCanvas, EncounterBannerSubtitleFontSize, BannerAnchor, EncounterBannerSubtitlePosition);
	EncounterBannerSubtitleText->SetVisibility(ESlateVisibility::Collapsed);
	bIsEncounterBannerShown = false;
}

void UTDCombatOverlayWidget::BuildDeathScreen()
{
	using namespace TDCombatOverlayWidgetInternal;

	const FAnchors CenterAnchor(0.5f, 0.5f);
	DeathTitleText = AddOverlayText(*WidgetTree, *RootCanvas, DeathTitleFontSize, CenterAnchor, DeathTitlePosition);
	DeathTitleText->SetText(FText::FromString(TEXT("쓰러졌습니다")));
	DeathTitleText->SetVisibility(ESlateVisibility::Collapsed);
	DeathCountdownText = AddOverlayText(*WidgetTree, *RootCanvas, DeathCountdownFontSize, CenterAnchor, DeathCountdownPosition);
	DeathCountdownText->SetVisibility(ESlateVisibility::Collapsed);
	bIsDeathScreenShown = false;
}

void UTDCombatOverlayWidget::UpdateDamageNumbers(const APlayerController& Controller, const double Now)
{
	NumberPool.ExpireFinished(Now);
	for (int32 Index = 0; Index < DamageNumberSlots.Num(); ++Index)
	{
		FTDDamageNumberWidgetSlot& NumberSlot = DamageNumberSlots[Index];
		FTDDamageNumberVisual Visual;
		if (!NumberSlot.Text || !NumberPool.ComputeVisual(Index, Now, Visual))
		{
			SetDamageNumberVisible(NumberSlot, false);
			continue;
		}

		const FTDDamageNumberEntry& Entry = NumberPool.GetEntry(Index);
		if (NumberSlot.AppliedSerial != Entry.Serial)
		{
			RefreshDamageNumberText(NumberSlot, Entry);
		}

		FVector2D ScreenPosition;
		if (!ProjectToOverlay(Controller, Visual.WorldLocation, TDCombatOverlayWidgetInternal::NumberEdgeMargin, ScreenPosition))
		{
			SetDamageNumberVisible(NumberSlot, false);
			continue;
		}

		NumberSlot.Text->SetRenderTranslation(ScreenPosition + Visual.ScreenOffset);
		NumberSlot.Text->SetRenderScale(FVector2D(Visual.Scale, Visual.Scale));
		NumberSlot.Text->SetRenderOpacity(Visual.Opacity);
		SetDamageNumberVisible(NumberSlot, true);
	}
}

void UTDCombatOverlayWidget::UpdateMonsterHealthBars(const APlayerController& Controller, const double Now, const float DeltaSeconds)
{
	using namespace TDCombatOverlayWidgetInternal;

	for (FTDMonsterHealthBarWidgetSlot& BarSlot : MonsterHealthBars)
	{
		if (!BarSlot.bIsAssigned)
		{
			continue;
		}

		const ATDCombatCharacter* Character = BarSlot.Character.Get();
		const UTDCombatComponent* Combat = Character ? Character->GetCombatComponent() : nullptr;
		const bool bIsDeadWithoutKillDisplay = Combat && !Combat->IsAlive() && !BarSlot.bIsShowingKill;
		if (!Combat || bIsDeadWithoutKillDisplay || Now >= BarSlot.ExpireTime || !BarSlot.CurrentBar || !BarSlot.DelayedBar)
		{
			ReleaseMonsterHealthBar(BarSlot);
			continue;
		}

		const float HealthFraction = BarSlot.bIsShowingKill ? 0.f : ComputeHealthFraction(*Combat);
		BarSlot.DelayedFraction = StepDelayedFraction(BarSlot.DelayedFraction, HealthFraction, Now, BarSlot.DelayedHoldUntil, DeltaSeconds);

		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
		const FVector HeadLocation = Character->GetActorLocation() + FVector(0.f, 0.f, HalfHeight + MonsterBarHeadClearanceCm);
		FVector2D ScreenPosition;
		if (!ProjectToOverlay(Controller, HeadLocation, BarEdgeMargin, ScreenPosition))
		{
			SetMonsterHealthBarVisible(BarSlot, false);
			continue;
		}

		BarSlot.CurrentBar->SetPercent(HealthFraction);
		BarSlot.DelayedBar->SetPercent(BarSlot.DelayedFraction);
		BarSlot.CurrentBar->SetRenderTranslation(ScreenPosition);
		BarSlot.DelayedBar->SetRenderTranslation(ScreenPosition);
		SetMonsterHealthBarVisible(BarSlot, true);
	}
}

void UTDCombatOverlayWidget::UpdatePlayerHud(APawn* Pawn, const double Now, const float DeltaSeconds)
{
	using namespace TDCombatOverlayWidgetInternal;

	const ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(Pawn);
	const UTDCombatComponent* Combat = Character ? Character->GetCombatComponent() : nullptr;
	if (!Combat || !PlayerHealthBar || !PlayerHealthDelayedBar || !PlayerStaminaBar || !PlayerHealthText)
	{
		HudPawn.Reset();
		bIsHudPawnAlive = false;
		SetPlayerHudVisible(false);
		return;
	}

	const float HealthFraction = ComputeHealthFraction(*Combat);
	bIsHudPawnAlive = Combat->IsAlive();
	if (HudPawn.Get() != Pawn)
	{
		HudPawn = Pawn;
		PlayerHealthFraction = HealthFraction;
		PlayerHealthDelayedFraction = HealthFraction;
		PlayerHealthHoldUntil = 0.;
	}
	if (HealthFraction < PlayerHealthFraction)
	{
		PlayerHealthHoldUntil = Now + DelayedBarHoldSeconds;
	}
	PlayerHealthFraction = HealthFraction;
	PlayerHealthDelayedFraction = StepDelayedFraction(PlayerHealthDelayedFraction, HealthFraction, Now, PlayerHealthHoldUntil, DeltaSeconds);

	PlayerHealthBar->SetPercent(HealthFraction);
	PlayerHealthDelayedBar->SetPercent(PlayerHealthDelayedFraction);
	const float MaxStamina = Combat->GetMaxStamina();
	PlayerStaminaBar->SetPercent(MaxStamina > 0.f ? FMath::Clamp(Combat->GetCurrentStamina() / MaxStamina, 0.f, 1.f) : 0.f);

	FLinearColor HealthColor = PlayerHealthColor;
	if (HealthFraction < LowHealthFraction)
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now * UE_TWO_PI * LowHealthPulseHz));
		HealthColor = FMath::Lerp(PlayerHealthColor, PlayerHealthPulseColor, Pulse);
	}
	PlayerHealthBar->SetFillColorAndOpacity(HealthColor);

	const int32 HealthValue = FMath::CeilToInt(Combat->GetCurrentHealth());
	const int32 MaxHealthValue = FMath::RoundToInt(Combat->GetMaxHealth());
	if (HealthValue != ShownHealthValue || MaxHealthValue != ShownMaxHealthValue)
	{
		ShownHealthValue = HealthValue;
		ShownMaxHealthValue = MaxHealthValue;
		PlayerHealthText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), HealthValue, MaxHealthValue)));
	}
	SetPlayerHudVisible(true);
}

void UTDCombatOverlayWidget::UpdateBossHealthBar(const double Now, const float DeltaSeconds)
{
	using namespace TDCombatOverlayWidgetInternal;

	if (!bIsBossHealthBarShown || !BossHealthBar || !BossHealthDelayedBar)
	{
		return;
	}
	if (bIsBossHealthBarFolding && Now >= BossFoldTime)
	{
		FoldBossHealthBar();
		return;
	}

	const ATDCombatCharacter* Boss = BossHealthBarTarget.Get();
	if (!Boss || !Boss->GetCombatComponent())
	{
		HideBossHealthBar();
	}

	const float HealthFraction = ComputeCharacterHealthFraction(Boss);
	if (HealthFraction < BossHealthFraction)
	{
		BossHealthHoldUntil = Now + DelayedBarHoldSeconds;
	}
	BossHealthFraction = HealthFraction;
	BossHealthDelayedFraction = StepDelayedFraction(BossHealthDelayedFraction, HealthFraction, Now, BossHealthHoldUntil, DeltaSeconds);
	BossHealthBar->SetPercent(BossHealthFraction);
	BossHealthDelayedBar->SetPercent(BossHealthDelayedFraction);
}

void UTDCombatOverlayWidget::UpdateEncounterBanner(const double Now)
{
	using namespace TDCombatOverlayWidgetInternal;

	if (!bIsEncounterBannerShown || !EncounterBannerTitleText || !EncounterBannerSubtitleText)
	{
		return;
	}

	const float Elapsed = static_cast<float>(Now - EncounterBannerStartTime);
	if (Elapsed >= EncounterBannerSeconds)
	{
		HideEncounterBanner();
		return;
	}

	const float FadeIn = FMath::Clamp(Elapsed / EncounterBannerFadeInSeconds, 0.f, 1.f);
	const float FadeOut = FMath::Clamp((EncounterBannerSeconds - Elapsed) / EncounterBannerFadeOutSeconds, 0.f, 1.f);
	const float Opacity = FMath::Min(FadeIn, FadeOut);
	EncounterBannerTitleText->SetRenderOpacity(Opacity);
	EncounterBannerSubtitleText->SetRenderOpacity(Opacity);
}

void UTDCombatOverlayWidget::UpdateDamageVignette(const double Now)
{
	if (!DamageVignetteImage)
	{
		return;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float LowHealthIntensity = bIsHudPawnAlive ? ComputeLowHealthVignetteIntensity(PlayerHealthFraction, Now, FeedbackSettings) : 0.f;
	const float Intensity = FMath::Max(ComputeRemainingHitVignette(Now), LowHealthIntensity);
	if (Intensity <= KINDA_SMALL_NUMBER)
	{
		if (bIsDamageVignetteVisible)
		{
			DamageVignetteImage->SetVisibility(ESlateVisibility::Collapsed);
			bIsDamageVignetteVisible = false;
		}
		return;
	}

	EnsureDamageVignetteMaterial(FeedbackSettings);
	if (DamageVignetteMaterialInstance)
	{
		DamageVignetteMaterialInstance->SetScalarParameterValue(FeedbackSettings.VignetteIntensityParameterName, Intensity);
	}
	else
	{
		DamageVignetteImage->SetColorAndOpacity(FeedbackSettings.DamageVignetteColor.CopyWithNewOpacity(ComputeFallbackTintAlpha(Intensity, FeedbackSettings)));
	}
	if (!bIsDamageVignetteVisible)
	{
		DamageVignetteImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		bIsDamageVignetteVisible = true;
	}
}

void UTDCombatOverlayWidget::EnsureDamageVignetteMaterial(const UTDCombatFeedbackSettings& FeedbackSettings)
{
	if (DamageVignetteMaterialInstance || !DamageVignetteImage || FeedbackSettings.DamageVignetteMaterial.IsNull())
	{
		return;
	}

	UMaterialInterface* VignetteMaterial = FeedbackSettings.DamageVignetteMaterial.Get();
	if (!VignetteMaterial)
	{
		return;
	}

	DamageVignetteMaterialInstance = UMaterialInstanceDynamic::Create(VignetteMaterial, this);
	if (!DamageVignetteMaterialInstance)
	{
		return;
	}
	DamageVignetteMaterialInstance->SetVectorParameterValue(FeedbackSettings.VignetteColorParameterName, FeedbackSettings.DamageVignetteColor);
	DamageVignetteMaterialInstance->SetScalarParameterValue(FeedbackSettings.VignetteIntensityParameterName, 0.f);
	DamageVignetteImage->SetBrushFromMaterial(DamageVignetteMaterialInstance.Get());
	DamageVignetteImage->SetColorAndOpacity(FLinearColor::White);
}

float UTDCombatOverlayWidget::ComputeRemainingHitVignette(const double Now) const
{
	const float VignetteSeconds = GetDefault<UTDCombatFeedbackSettings>()->DamageVignetteSeconds;
	const float Elapsed = static_cast<float>(Now - DamageVignetteStartTime);
	if (VignetteSeconds <= 0.f || Elapsed >= VignetteSeconds)
	{
		return 0.f;
	}
	return DamageVignettePeakIntensity * (1.f - FMath::Max(Elapsed, 0.f) / VignetteSeconds);
}

void UTDCombatOverlayWidget::UpdateDeathScreen(APawn* Pawn, const double Now)
{
	if (!bIsDeathScreenShown || !DeathTitleText || !DeathCountdownText)
	{
		return;
	}

	if (Pawn && Pawn != DeathPawn.Get() && TDCombatOverlayWidgetInternal::IsPawnAlive(Pawn))
	{
		bIsDeathScreenShown = false;
		DeathPawn.Reset();
		DeathTitleText->SetVisibility(ESlateVisibility::Collapsed);
		DeathCountdownText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	const float Opacity = FMath::Clamp(static_cast<float>(Now - DeathScreenStartTime) / TDCombatOverlayWidgetInternal::DeathFadeSeconds, 0.f, 1.f);
	DeathTitleText->SetRenderOpacity(Opacity);
	DeathCountdownText->SetRenderOpacity(Opacity);

	const int32 RemainingSeconds = FMath::Max(0, FMath::CeilToInt(static_cast<float>(RespawnTime - Now)));
	if (RemainingSeconds == ShownCountdownSeconds)
	{
		return;
	}
	ShownCountdownSeconds = RemainingSeconds;
	DeathCountdownText->SetText(FText::FromString(FString::Printf(TEXT("%d초 후 부활"), RemainingSeconds)));
}

void UTDCombatOverlayWidget::RefreshDamageNumberText(FTDDamageNumberWidgetSlot& NumberSlot, const FTDDamageNumberEntry& Entry) const
{
	using namespace TDCombatOverlayWidgetInternal;

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	NumberSlot.AppliedSerial = Entry.Serial;
	NumberSlot.Text->SetFont(MakeOverlayFont(GetNumberFontSize(FeedbackSettings, Entry.Style)));
	NumberSlot.Text->SetColorAndOpacity(FSlateColor(GetNumberColor(FeedbackSettings, Entry)));
	NumberSlot.Text->SetText(MakeNumberLabel(Entry));
}

void UTDCombatOverlayWidget::SetDamageNumberVisible(FTDDamageNumberWidgetSlot& NumberSlot, const bool bShouldBeVisible) const
{
	if (!NumberSlot.Text || NumberSlot.bIsVisible == bShouldBeVisible)
	{
		return;
	}
	NumberSlot.bIsVisible = bShouldBeVisible;
	NumberSlot.Text->SetVisibility(bShouldBeVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UTDCombatOverlayWidget::SetMonsterHealthBarVisible(FTDMonsterHealthBarWidgetSlot& BarSlot, const bool bShouldBeVisible) const
{
	if (BarSlot.bIsVisible == bShouldBeVisible)
	{
		return;
	}
	BarSlot.bIsVisible = bShouldBeVisible;
	const ESlateVisibility NewVisibility = bShouldBeVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (BarSlot.DelayedBar)
	{
		BarSlot.DelayedBar->SetVisibility(NewVisibility);
	}
	if (BarSlot.CurrentBar)
	{
		BarSlot.CurrentBar->SetVisibility(NewVisibility);
	}
}

void UTDCombatOverlayWidget::ReleaseMonsterHealthBar(FTDMonsterHealthBarWidgetSlot& BarSlot) const
{
	BarSlot.bIsAssigned = false;
	BarSlot.bIsShowingKill = false;
	BarSlot.Character.Reset();
	BarSlot.DelayedFraction = 0.f;
	SetMonsterHealthBarVisible(BarSlot, false);
}

void UTDCombatOverlayWidget::SetPlayerHudVisible(const bool bShouldBeVisible)
{
	if (bIsPlayerHudVisible == bShouldBeVisible)
	{
		return;
	}
	bIsPlayerHudVisible = bShouldBeVisible;
	const ESlateVisibility NewVisibility = bShouldBeVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	for (UWidget* HudWidget : { static_cast<UWidget*>(PlayerHealthDelayedBar.Get()), static_cast<UWidget*>(PlayerHealthBar.Get()), static_cast<UWidget*>(PlayerStaminaBar.Get()), static_cast<UWidget*>(PlayerHealthText.Get()) })
	{
		if (HudWidget)
		{
			HudWidget->SetVisibility(NewVisibility);
		}
	}
}

void UTDCombatOverlayWidget::SetBossHealthBarVisible(const bool bShouldBeVisible)
{
	bIsBossHealthBarShown = bShouldBeVisible;
	const ESlateVisibility NewVisibility = bShouldBeVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	for (UWidget* BossWidget : { static_cast<UWidget*>(BossNameText.Get()), static_cast<UWidget*>(BossHealthDelayedBar.Get()), static_cast<UWidget*>(BossHealthBar.Get()) })
	{
		if (BossWidget)
		{
			BossWidget->SetVisibility(NewVisibility);
		}
	}
	for (int32 MarkIndex = 0; MarkIndex < BossPhaseMarkImages.Num(); ++MarkIndex)
	{
		if (BossPhaseMarkImages[MarkIndex])
		{
			BossPhaseMarkImages[MarkIndex]->SetVisibility(bShouldBeVisible && MarkIndex < ShownBossPhaseMarkCount ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
}

void UTDCombatOverlayWidget::ApplyBossPhaseMarks(const TArray<float>& PhaseHealthMarks)
{
	using namespace TDCombatOverlayWidgetInternal;

	int32 MarkCount = 0;
	for (const float MarkValue : PhaseHealthMarks)
	{
		if (MarkCount >= BossPhaseMarkImages.Num())
		{
			break;
		}
		if (!(MarkValue > 0.f && MarkValue < 1.f))
		{
			continue;
		}

		UImage* MarkImage = BossPhaseMarkImages[MarkCount];
		UCanvasPanelSlot* MarkSlot = MarkImage ? Cast<UCanvasPanelSlot>(MarkImage->Slot) : nullptr;
		if (!MarkSlot)
		{
			continue;
		}
		MarkSlot->SetPosition(FVector2D((MarkValue - 0.5f) * BossBarSize.X, BossBarPosition.Y + BossBarSize.Y * 0.5f));
		++MarkCount;
	}
	ShownBossPhaseMarkCount = MarkCount;
}

void UTDCombatOverlayWidget::FoldBossHealthBar()
{
	bIsBossHealthBarFolding = false;
	BossHealthBarTarget.Reset();
	ShownBossPhaseMarkCount = 0;
	SetBossHealthBarVisible(false);
}

void UTDCombatOverlayWidget::HideEncounterBanner()
{
	bIsEncounterBannerShown = false;
	if (EncounterBannerTitleText)
	{
		EncounterBannerTitleText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (EncounterBannerSubtitleText)
	{
		EncounterBannerSubtitleText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UTDCombatOverlayWidget::ProjectToOverlay(const APlayerController& Controller, const FVector& WorldLocation, const float EdgeMargin, FVector2D& OutPosition) const
{
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(&Controller, WorldLocation, OutPosition, false))
	{
		return false;
	}
	if (OverlayViewportSize.X <= 0. || OverlayViewportSize.Y <= 0.)
	{
		return true;
	}
	return OutPosition.X >= -EdgeMargin && OutPosition.Y >= -EdgeMargin
		&& OutPosition.X <= OverlayViewportSize.X + EdgeMargin && OutPosition.Y <= OverlayViewportSize.Y + EdgeMargin;
}

double UTDCombatOverlayWidget::GetOverlayTime() const
{
	const UWorld* CurrentWorld = GetWorld();
	return CurrentWorld ? CurrentWorld->GetTimeSeconds() : 0.;
}
