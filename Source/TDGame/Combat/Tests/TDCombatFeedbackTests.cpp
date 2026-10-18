#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraShakeBase.h"
#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"
#include "Combat/Feedback/TDCombatOverlayWidget.h"
#include "Combat/Feedback/TDDamageNumberPool.h"
#include "Combat/Feedback/TDDeferredFeedbackQueue.h"
#include "Combat/Feedback/TDHitCameraShake.h"
#include "Core/TDGameplayMessages.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundConcurrency.h"
#include "UObject/Package.h"

namespace TDCombatFeedbackTestsInternal
{
	FTDDamageNumberRequest MakeNumberRequest(const UObject* Target, const float Amount, const ETDDamageNumberStyle Style)
	{
		FTDDamageNumberRequest Request;
		Request.WorldLocation = FVector(100.f, 200.f, 50.f);
		Request.TargetKey = FObjectKey(Target);
		Request.Amount = Amount;
		Request.Style = Style;
		return Request;
	}

	FTDDamageNumberPoolSettings MakePoolSettings(const int32 Capacity)
	{
		FTDDamageNumberPoolSettings PoolSettings;
		PoolSettings.Capacity = Capacity;
		PoolSettings.LifetimeSeconds = 1.f;
		PoolSettings.RiseCm = 70.f;
		PoolSettings.PopSeconds = 0.12f;
		PoolSettings.PopStartScale = 1.35f;
		PoolSettings.FadeFraction = 0.4f;
		PoolSettings.StatusMergeSeconds = 0.3f;
		PoolSettings.StackWindowSeconds = 0.06f;
		PoolSettings.StackOffset = 20.f;
		return PoolSettings;
	}

	FTDDamageAppliedMessage MakeAppliedMessage(const float AppliedDamage, const float UnclampedDamage, const bool bWasKilled, const bool bWasCritical, const float DamageRatio, const ETDDamageDelivery Delivery)
	{
		FTDDamageAppliedMessage Message;
		Message.DamageAmount = AppliedDamage;
		Message.UnclampedDamage = UnclampedDamage;
		Message.bWasKilled = bWasKilled;
		Message.bWasCritical = bWasCritical;
		Message.DamageRatio = DamageRatio;
		Message.Delivery = Delivery;
		return Message;
	}

	UTDCombatFeedbackSettings* MakeVignetteSettings()
	{
		UTDCombatFeedbackSettings* FeedbackSettings = NewObject<UTDCombatFeedbackSettings>(GetTransientPackage());
		FeedbackSettings->DamageVignetteFullRatio = 0.15f;
		FeedbackSettings->DamageVignetteMinStrength = 0.2f;
		FeedbackSettings->DamageVignetteMaxIntensity = 0.6f;
		FeedbackSettings->StatusVignetteScale = 0.3f;
		FeedbackSettings->FallbackDamageTintAlpha = 0.1f;
		FeedbackSettings->LowHealthVignetteFraction = 0.25f;
		FeedbackSettings->LowHealthVignetteMinIntensity = 0.25f;
		FeedbackSettings->LowHealthVignetteMaxIntensity = 0.4f;
		FeedbackSettings->LowHealthVignettePeriodSeconds = 1.2f;
		return FeedbackSettings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackNumberPoolRecyclesOldestTest, "TDGame.Combat.Feedback.NumberPoolRecyclesOldest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackNumberPoolRecyclesOldestTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatFeedbackTestsInternal;

	const UObject* FirstTarget = GetTransientPackage();
	const UObject* SecondTarget = UTDCombatFeedbackSubsystem::StaticClass();
	FTDDamageNumberPool Pool;
	Pool.Configure(MakePoolSettings(3));

	const int32 FirstIndex = Pool.Add(MakeNumberRequest(FirstTarget, 10.f, ETDDamageNumberStyle::Normal), 0.0);
	Pool.Add(MakeNumberRequest(SecondTarget, 11.f, ETDDamageNumberStyle::Normal), 0.1);
	Pool.Add(MakeNumberRequest(FirstTarget, 12.f, ETDDamageNumberStyle::Normal), 0.2);
	TestEqual(TEXT("Three numbers fill the pool"), Pool.CountActive(), 3);

	const int32 RecycledIndex = Pool.Add(MakeNumberRequest(SecondTarget, 13.f, ETDDamageNumberStyle::Normal), 0.3);
	TestEqual(TEXT("A full pool reuses the oldest slot"), RecycledIndex, FirstIndex);
	TestEqual(TEXT("Reuse keeps the active count at capacity"), Pool.CountActive(), 3);
	TestEqual(TEXT("The reused slot carries the new amount"), Pool.GetEntry(RecycledIndex).Amount, 13.f);
	TestTrue(TEXT("The reused slot gets a new start time"), FMath::IsNearlyEqual(Pool.GetEntry(RecycledIndex).StartTime, 0.3));

	FTDDamageNumberPool StackPool;
	StackPool.Configure(MakePoolSettings(8));
	const int32 BaseIndex = StackPool.Add(MakeNumberRequest(FirstTarget, 5.f, ETDDamageNumberStyle::Normal), 1.0);
	const int32 StackedIndex = StackPool.Add(MakeNumberRequest(FirstTarget, 6.f, ETDDamageNumberStyle::Normal), 1.03);
	const int32 LateIndex = StackPool.Add(MakeNumberRequest(FirstTarget, 7.f, ETDDamageNumberStyle::Normal), 1.2);
	TestTrue(TEXT("A second hit inside the stack window is raised above the first"), StackPool.GetEntry(StackedIndex).StackY < StackPool.GetEntry(BaseIndex).StackY);
	TestEqual(TEXT("A hit after the stack window starts at the base row"), StackPool.GetEntry(LateIndex).StackY, 0.f);

	FTDDamageNumberPool StatusPool;
	StatusPool.Configure(MakePoolSettings(8));
	const int32 StatusIndex = StatusPool.Add(MakeNumberRequest(FirstTarget, 2.f, ETDDamageNumberStyle::Status), 0.0);
	TestTrue(TEXT("Status damage on the same target can merge inside the window"), StatusPool.CanMergeStatus(MakeNumberRequest(FirstTarget, 1.f, ETDDamageNumberStyle::Status), 0.25));
	TestFalse(TEXT("A direct hit never merges into a status number"), StatusPool.CanMergeStatus(MakeNumberRequest(FirstTarget, 1.f, ETDDamageNumberStyle::Normal), 0.25));
	TestFalse(TEXT("Status damage on another target cannot merge"), StatusPool.CanMergeStatus(MakeNumberRequest(SecondTarget, 1.f, ETDDamageNumberStyle::Status), 0.25));
	TestFalse(TEXT("Status damage after the window cannot merge"), StatusPool.CanMergeStatus(MakeNumberRequest(FirstTarget, 1.f, ETDDamageNumberStyle::Status), 0.4));
	const int32 MergedIndex = StatusPool.Add(MakeNumberRequest(FirstTarget, 3.f, ETDDamageNumberStyle::Status), 0.25);
	const int32 OtherTargetIndex = StatusPool.Add(MakeNumberRequest(SecondTarget, 4.f, ETDDamageNumberStyle::Status), 0.25);
	const int32 NextWindowIndex = StatusPool.Add(MakeNumberRequest(FirstTarget, 1.f, ETDDamageNumberStyle::Status), 0.4);
	TestEqual(TEXT("Status damage on the same target merges inside the window"), MergedIndex, StatusIndex);
	TestEqual(TEXT("Merged status damage accumulates"), StatusPool.GetEntry(StatusIndex).Amount, 5.f);
	TestNotEqual(TEXT("Status damage on another target gets its own number"), OtherTargetIndex, StatusIndex);
	TestNotEqual(TEXT("Status damage after the window starts a new number"), NextWindowIndex, StatusIndex);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackNumberPoolLifetimeAndFadeTest, "TDGame.Combat.Feedback.NumberPoolLifetimeAndFade", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackNumberPoolLifetimeAndFadeTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatFeedbackTestsInternal;

	TestTrue(TEXT("Pop starts enlarged"), FMath::IsNearlyEqual(FTDDamageNumberPool::ComputePopScale(0.f, 0.12f, 1.35f), 1.35f));
	TestTrue(TEXT("Pop settles at normal size"), FMath::IsNearlyEqual(FTDDamageNumberPool::ComputePopScale(0.12f, 0.12f, 1.35f), 1.f));
	TestTrue(TEXT("Opacity holds before the fade"), FMath::IsNearlyEqual(FTDDamageNumberPool::ComputeOpacity(0.5f, 1.f, 0.4f), 1.f));
	TestTrue(TEXT("Opacity is halfway through the fade"), FMath::IsNearlyEqual(FTDDamageNumberPool::ComputeOpacity(0.8f, 1.f, 0.4f), 0.5f, 0.001f));
	TestTrue(TEXT("Opacity reaches zero at the end of life"), FMath::IsNearlyEqual(FTDDamageNumberPool::ComputeOpacity(1.f, 1.f, 0.4f), 0.f));
	TestTrue(TEXT("Rise starts at zero"), FMath::IsNearlyEqual(FTDDamageNumberPool::ComputeRiseFraction(0.f, 1.f), 0.f));
	TestTrue(TEXT("Rise eases out"), FTDDamageNumberPool::ComputeRiseFraction(0.5f, 1.f) > 0.5f);
	TestTrue(TEXT("Rise ends at one"), FMath::IsNearlyEqual(FTDDamageNumberPool::ComputeRiseFraction(1.f, 1.f), 1.f));

	const UObject* Target = GetTransientPackage();
	FTDDamageNumberPool Pool;
	Pool.Configure(MakePoolSettings(4));
	const int32 Index = Pool.Add(MakeNumberRequest(Target, 30.f, ETDDamageNumberStyle::Critical), 2.0);

	FTDDamageNumberVisual StartVisual;
	FTDDamageNumberVisual MiddleVisual;
	TestTrue(TEXT("A new number has a visual"), Pool.ComputeVisual(Index, 2.0, StartVisual));
	TestTrue(TEXT("A number keeps a visual during its life"), Pool.ComputeVisual(Index, 2.5, MiddleVisual));
	TestTrue(TEXT("The number rises over time"), MiddleVisual.WorldLocation.Z > StartVisual.WorldLocation.Z);
	TestTrue(TEXT("The start visual is popped"), StartVisual.Scale > MiddleVisual.Scale);

	Pool.ExpireFinished(2.99);
	TestEqual(TEXT("A number before its lifetime stays active"), Pool.CountActive(), 1);
	Pool.ExpireFinished(3.0);
	TestEqual(TEXT("A number at its lifetime expires"), Pool.CountActive(), 0);
	FTDDamageNumberVisual ExpiredVisual;
	TestFalse(TEXT("An expired number has no visual"), Pool.ComputeVisual(Index, 3.0, ExpiredVisual));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackClassifyHitTierTest, "TDGame.Combat.Feedback.ClassifyHitTier", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackClassifyHitTierTest::RunTest(const FString& Parameters)
{
	constexpr float HeavyRatio = 0.12f;
	TestTrue(TEXT("A killing blow is a kill"), UTDCombatFeedbackSubsystem::ClassifyHitTier(true, false, 0.01f, HeavyRatio) == ETDHitFeedbackTier::Kill);
	TestTrue(TEXT("A critical killing blow is still a kill"), UTDCombatFeedbackSubsystem::ClassifyHitTier(true, true, 0.5f, HeavyRatio) == ETDHitFeedbackTier::Kill);
	TestTrue(TEXT("A critical hit is heavy"), UTDCombatFeedbackSubsystem::ClassifyHitTier(false, true, 0.01f, HeavyRatio) == ETDHitFeedbackTier::Heavy);
	TestTrue(TEXT("A hit at the heavy ratio is heavy"), UTDCombatFeedbackSubsystem::ClassifyHitTier(false, false, HeavyRatio, HeavyRatio) == ETDHitFeedbackTier::Heavy);
	TestTrue(TEXT("A small hit is light"), UTDCombatFeedbackSubsystem::ClassifyHitTier(false, false, 0.05f, HeavyRatio) == ETDHitFeedbackTier::Light);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackStatusKillIsQuietTest, "TDGame.Combat.Feedback.StatusKillIsQuiet", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackStatusKillIsQuietTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatFeedbackTestsInternal;

	constexpr float HeavyRatio = 0.12f;
	const FTDHitPresentation StatusKill = UTDCombatFeedbackSubsystem::DescribeHitPresentation(MakeAppliedMessage(5.f, 22.f, true, false, 0.02f, ETDDamageDelivery::Status), HeavyRatio);
	TestTrue(TEXT("A status kill keeps the kill tier for its number"), StatusKill.Tier == ETDHitFeedbackTier::Kill);
	TestTrue(TEXT("A status kill keeps the status flag"), StatusKill.bIsStatus);
	TestEqual(TEXT("A status kill shows the overflowing damage"), StatusKill.NumberAmount, 22.f);
	TestFalse(TEXT("A status kill spawns no impact effect"), StatusKill.bShowsImpactEffects);
	TestFalse(TEXT("A status kill does not wobble the victim"), StatusKill.bWobblesVictim);
	TestFalse(TEXT("A status kill does not stop the victim"), StatusKill.bStopsVictim);
	TestFalse(TEXT("A status kill does not stop the attacker"), StatusKill.bStopsAttacker);
	TestFalse(TEXT("A status kill plays no camera shake or rumble"), StatusKill.bPlaysCameraFeedback);
	TestFalse(TEXT("A status kill plays no accent sound"), StatusKill.bPlaysAccentSounds);

	const FTDHitPresentation MeleeKill = UTDCombatFeedbackSubsystem::DescribeHitPresentation(MakeAppliedMessage(5.f, 22.f, true, false, 0.02f, ETDDamageDelivery::Melee), HeavyRatio);
	TestTrue(TEXT("A melee kill is a kill"), MeleeKill.Tier == ETDHitFeedbackTier::Kill);
	TestFalse(TEXT("A melee kill is not status"), MeleeKill.bIsStatus);
	TestEqual(TEXT("A melee kill shows the overflowing damage"), MeleeKill.NumberAmount, 22.f);
	TestTrue(TEXT("A melee kill spawns impact effects"), MeleeKill.bShowsImpactEffects);
	TestTrue(TEXT("A melee kill wobbles the victim"), MeleeKill.bWobblesVictim);
	TestFalse(TEXT("A melee kill does not stop the dying victim"), MeleeKill.bStopsVictim);
	TestTrue(TEXT("A melee kill may stop the attacker"), MeleeKill.bStopsAttacker);
	TestTrue(TEXT("A melee kill plays camera feedback"), MeleeKill.bPlaysCameraFeedback);
	TestTrue(TEXT("A melee kill plays accent sounds"), MeleeKill.bPlaysAccentSounds);

	const FTDHitPresentation KillWithoutOverflow = UTDCombatFeedbackSubsystem::DescribeHitPresentation(MakeAppliedMessage(5.f, 0.f, true, false, 0.02f, ETDDamageDelivery::Melee), HeavyRatio);
	TestEqual(TEXT("A kill without overflow data shows the applied damage"), KillWithoutOverflow.NumberAmount, 5.f);

	const FTDHitPresentation CriticalHit = UTDCombatFeedbackSubsystem::DescribeHitPresentation(MakeAppliedMessage(10.f, 30.f, false, true, 0.05f, ETDDamageDelivery::Melee), HeavyRatio);
	TestTrue(TEXT("A critical hit is heavy"), CriticalHit.Tier == ETDHitFeedbackTier::Heavy);
	TestEqual(TEXT("A non-lethal hit shows the applied damage"), CriticalHit.NumberAmount, 10.f);
	TestTrue(TEXT("A non-lethal direct hit stops the victim"), CriticalHit.bStopsVictim);

	const FTDHitPresentation StatusTick = UTDCombatFeedbackSubsystem::DescribeHitPresentation(MakeAppliedMessage(4.f, 4.f, false, true, 0.5f, ETDDamageDelivery::Status), HeavyRatio);
	TestTrue(TEXT("A status tick keeps the status flag even when critical"), StatusTick.bIsStatus);
	TestFalse(TEXT("A status tick spawns no impact effect"), StatusTick.bShowsImpactEffects);
	TestFalse(TEXT("A status tick does not stop the victim"), StatusTick.bStopsVictim);
	TestFalse(TEXT("A status tick plays no camera feedback"), StatusTick.bPlaysCameraFeedback);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackSoundConcurrencyIsSplitTest, "TDGame.Combat.Feedback.SoundConcurrencyIsSplit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackSoundConcurrencyIsSplitTest::RunTest(const FString& Parameters)
{
	UTDCombatFeedbackSettings* FeedbackSettings = NewObject<UTDCombatFeedbackSettings>(GetTransientPackage());
	FeedbackSettings->ImpactSoundMaxCount = 10;
	FeedbackSettings->AccentSoundMaxCount = 4;
	FeedbackSettings->SwingSoundMaxCount = 3;
	UTDCombatFeedbackSubsystem* Feedback = NewObject<UTDCombatFeedbackSubsystem>(GetTransientPackage());
	Feedback->CreateSoundSettings(*FeedbackSettings);

	const USoundConcurrency* Impact = Feedback->GetSoundConcurrency(ETDFeedbackSoundChannel::Impact);
	const USoundConcurrency* Accent = Feedback->GetSoundConcurrency(ETDFeedbackSoundChannel::Accent);
	const USoundConcurrency* Swing = Feedback->GetSoundConcurrency(ETDFeedbackSoundChannel::Swing);
	if (!TestNotNull(TEXT("Impact sounds have a concurrency group"), Impact)
		|| !TestNotNull(TEXT("Accent sounds have a concurrency group"), Accent)
		|| !TestNotNull(TEXT("Swing sounds have a concurrency group"), Swing))
	{
		return false;
	}

	TestTrue(TEXT("Impact and accent sounds use different groups"), Impact != Accent);
	TestTrue(TEXT("Impact and swing sounds use different groups"), Impact != Swing);
	TestTrue(TEXT("Accent and swing sounds use different groups"), Accent != Swing);
	TestEqual(TEXT("Impact sounds allow ten voices"), Impact->Concurrency.MaxCount, 10);
	TestEqual(TEXT("Accent sounds allow four voices"), Accent->Concurrency.MaxCount, 4);
	TestEqual(TEXT("Swing sounds allow three voices"), Swing->Concurrency.MaxCount, 3);
	for (const USoundConcurrency* SoundConcurrency : { Impact, Accent, Swing })
	{
		TestTrue(TEXT("Feedback sounds never wait for a retrigger interval"), FMath::IsNearlyZero(SoundConcurrency->Concurrency.RetriggerTime));
		TestTrue(TEXT("Feedback sounds stop the oldest voice when full"), SoundConcurrency->Concurrency.ResolutionRule == EMaxConcurrentResolutionRule::StopOldest);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackShakeAmountCurveTest, "TDGame.Combat.Feedback.ShakeAmountCurve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackShakeAmountCurveTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("No trauma means no shake"), FMath::IsNearlyZero(TDHitCameraShakeMath::ComputeShakeAmount(0.f)));
	TestTrue(TEXT("Full trauma means full shake"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeShakeAmount(1.f), 1.f));
	TestTrue(TEXT("Quarter trauma follows the power of one and a half"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeShakeAmount(0.25f), 0.125f, 0.0001f));
	TestTrue(TEXT("The curve is stronger than the old square curve for small trauma"), TDHitCameraShakeMath::ComputeShakeAmount(0.25f) > FMath::Square(0.25f));
	TestTrue(TEXT("Trauma above one is clamped"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeShakeAmount(1.5f), 1.f));
	TestTrue(TEXT("Negative trauma is clamped"), FMath::IsNearlyZero(TDHitCameraShakeMath::ComputeShakeAmount(-0.5f)));

	float PreviousAmount = -1.f;
	bool bIsIncreasing = true;
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const float Amount = TDHitCameraShakeMath::ComputeShakeAmount(static_cast<float>(Step) / 10.f);
		bIsIncreasing = bIsIncreasing && Amount > PreviousAmount;
		PreviousAmount = Amount;
	}
	TestTrue(TEXT("Shake grows with trauma"), bIsIncreasing);

	const UTDHitCameraShake* Shake = NewObject<UTDHitCameraShake>(GetTransientPackage());
	const UTDTraumaCameraShakePattern* Pattern = Cast<UTDTraumaCameraShakePattern>(Shake->GetRootShakePattern());
	if (!TestNotNull(TEXT("The hit shake owns a trauma pattern"), Pattern))
	{
		return false;
	}
	TestTrue(TEXT("The lateral offset limit is 28 cm"), FMath::IsNearlyEqual(Pattern->MaxLocationOffset.Y, 28.0));
	TestTrue(TEXT("The vertical offset limit is 20 cm"), FMath::IsNearlyEqual(Pattern->MaxLocationOffset.Z, 20.0));
	TestTrue(TEXT("The pitch limit is 1.2 degrees"), FMath::IsNearlyEqual(Pattern->MaxRotationOffset.Pitch, 1.2, 0.0001));
	TestTrue(TEXT("The roll limit is 0.8 degrees"), FMath::IsNearlyEqual(Pattern->MaxRotationOffset.Roll, 0.8, 0.0001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackDeferredQueueKeepsOrderTest, "TDGame.Combat.Feedback.DeferredQueueKeepsOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackDeferredQueueKeepsOrderTest::RunTest(const FString& Parameters)
{
	constexpr double MaxQueuedSeconds = 0.1;
	TTDDeferredFeedbackQueue<int32> Queue;
	TArray<int32> ProcessedItems;
	int32 RemainingBudget = 2;
	auto TryProcess = [&ProcessedItems, &RemainingBudget](const int32 Item)
	{
		if (RemainingBudget <= 0)
		{
			return false;
		}
		--RemainingBudget;
		ProcessedItems.Add(Item);
		return true;
	};

	Queue.Push(1, 0.0);
	Queue.Push(2, 0.0);
	Queue.Push(3, 0.02);
	TestEqual(TEXT("A full budget processes only what it allows"), Queue.Drain(0.05, MaxQueuedSeconds, TryProcess), 2);
	TestEqual(TEXT("Items over budget stay queued"), Queue.Num(), 1);
	TestEqual(TEXT("The oldest item goes first"), ProcessedItems.Num() >= 1 ? ProcessedItems[0] : INDEX_NONE, 1);
	TestEqual(TEXT("The second item goes second"), ProcessedItems.Num() >= 2 ? ProcessedItems[1] : INDEX_NONE, 2);

	RemainingBudget = 0;
	TestEqual(TEXT("An empty budget processes nothing"), Queue.Drain(0.06, MaxQueuedSeconds, TryProcess), 0);
	TestEqual(TEXT("An empty budget keeps the queue"), Queue.Num(), 1);

	RemainingBudget = 5;
	Queue.Push(4, 0.07);
	TestEqual(TEXT("A fresh budget drains the backlog"), Queue.Drain(0.08, MaxQueuedSeconds, TryProcess), 2);
	TestEqual(TEXT("The backlog item keeps its place before newer items"), ProcessedItems.Num() >= 3 ? ProcessedItems[2] : INDEX_NONE, 3);
	TestEqual(TEXT("The newer item follows the backlog"), ProcessedItems.Num() >= 4 ? ProcessedItems[3] : INDEX_NONE, 4);
	TestEqual(TEXT("A drained queue is empty"), Queue.Num(), 0);

	Queue.Push(5, 0.0);
	Queue.Push(6, 0.15);
	TestEqual(TEXT("Only items younger than the limit are processed"), Queue.Drain(0.2, MaxQueuedSeconds, TryProcess), 1);
	TestEqual(TEXT("The young item is processed"), ProcessedItems.Num() >= 5 ? ProcessedItems[4] : INDEX_NONE, 6);
	TestEqual(TEXT("Expired items are dropped"), Queue.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackDamageVignetteIntensityTest, "TDGame.Combat.Feedback.DamageVignetteIntensity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackDamageVignetteIntensityTest::RunTest(const FString& Parameters)
{
	using namespace TDCombatFeedbackTestsInternal;

	const UTDCombatFeedbackSettings& FeedbackSettings = *MakeVignetteSettings();
	TestTrue(TEXT("A tiny hit still shows the minimum vignette"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeHitVignetteIntensity(0.f, false, FeedbackSettings), 0.12f, 0.001f));
	TestTrue(TEXT("A small hit scales with its damage ratio"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeHitVignetteIntensity(0.036f, false, FeedbackSettings), 0.144f, 0.001f));
	TestTrue(TEXT("A hit at the full ratio reaches the maximum"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeHitVignetteIntensity(0.15f, false, FeedbackSettings), 0.6f, 0.001f));
	TestTrue(TEXT("A huge hit is capped at the maximum"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeHitVignetteIntensity(0.8f, false, FeedbackSettings), 0.6f, 0.001f));
	TestTrue(TEXT("Status damage is scaled down"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeHitVignetteIntensity(0.15f, true, FeedbackSettings), 0.18f, 0.001f));

	TestTrue(TEXT("Healthy players get no low health pulse"), FMath::IsNearlyZero(UTDCombatOverlayWidget::ComputeLowHealthVignetteIntensity(0.5f, 0.3, FeedbackSettings)));
	TestTrue(TEXT("Dead players get no low health pulse"), FMath::IsNearlyZero(UTDCombatOverlayWidget::ComputeLowHealthVignetteIntensity(0.f, 0.3, FeedbackSettings)));
	TestTrue(TEXT("The pulse peaks a quarter period in"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeLowHealthVignetteIntensity(0.2f, 0.3, FeedbackSettings), 0.4f, 0.001f));
	TestTrue(TEXT("The pulse bottoms out three quarters in"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeLowHealthVignetteIntensity(0.2f, 0.9, FeedbackSettings), 0.25f, 0.001f));
	TestTrue(TEXT("The pulse repeats every period"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeLowHealthVignetteIntensity(0.2f, 1.5, FeedbackSettings), 0.4f, 0.001f));

	TestTrue(TEXT("The fallback tint peaks at its alpha"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeFallbackTintAlpha(0.6f, FeedbackSettings), 0.1f, 0.001f));
	TestTrue(TEXT("The fallback tint scales with intensity"), FMath::IsNearlyEqual(UTDCombatOverlayWidget::ComputeFallbackTintAlpha(0.3f, FeedbackSettings), 0.05f, 0.001f));
	TestTrue(TEXT("No intensity means no fallback tint"), FMath::IsNearlyZero(UTDCombatOverlayWidget::ComputeFallbackTintAlpha(0.f, FeedbackSettings)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackShouldEnableRulesTest, "TDGame.Combat.Feedback.ShouldEnableFeedbackRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackShouldEnableRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A rendering game world with a game instance gets feedback"), UTDCombatFeedbackSubsystem::ShouldEnableFeedback(true, EWorldType::Game, true));
	TestTrue(TEXT("A rendering play-in-editor world with a game instance gets feedback"), UTDCombatFeedbackSubsystem::ShouldEnableFeedback(true, EWorldType::PIE, true));
	TestFalse(TEXT("A world that cannot render gets no feedback"), UTDCombatFeedbackSubsystem::ShouldEnableFeedback(false, EWorldType::Game, true));
	TestFalse(TEXT("A world without a game instance gets no feedback"), UTDCombatFeedbackSubsystem::ShouldEnableFeedback(true, EWorldType::Game, false));
	TestFalse(TEXT("An editor world gets no feedback"), UTDCombatFeedbackSubsystem::ShouldEnableFeedback(true, EWorldType::Editor, true));
	TestFalse(TEXT("An editor preview world gets no feedback"), UTDCombatFeedbackSubsystem::ShouldEnableFeedback(true, EWorldType::EditorPreview, true));
	TestFalse(TEXT("An inactive world gets no feedback"), UTDCombatFeedbackSubsystem::ShouldEnableFeedback(true, EWorldType::Inactive, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackSubsystemSkippedWithoutRenderingTest, "TDGame.Combat.Feedback.SubsystemSkippedWithoutRendering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackSubsystemSkippedWithoutRenderingTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Headless rendering disables feedback for a full game world"), UTDCombatFeedbackSubsystem::ShouldEnableFeedback(false, EWorldType::Game, true));
	TestFalse(TEXT("This process disables feedback for a world without a game instance"), UTDCombatFeedbackSubsystem::ShouldEnableFeedback(FApp::CanEverRender(), EWorldType::Game, false));

	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDCombatFeedbackTestWorld"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	World->SetBegunPlay(true);

	TestNull(TEXT("The test world has no game instance"), World->GetGameInstance());
	const UTDCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UTDCombatFeedbackSubsystem>();
	TestNull(TEXT("The feedback subsystem is not created without a game instance"), Feedback);
	TestFalse(TEXT("No feedback listener is registered without a game instance"), Feedback && Feedback->IsListening());

	World->EndPlay(EEndPlayReason::Quit);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFeedbackTraumaPatternDecaysTest, "TDGame.Combat.Feedback.TraumaPatternDecays", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFeedbackTraumaPatternDecaysTest::RunTest(const FString& Parameters)
{
	UTDHitCameraShake* Shake = NewObject<UTDHitCameraShake>(GetTransientPackage());
	UTDTraumaCameraShakePattern* Pattern = Cast<UTDTraumaCameraShakePattern>(Shake->GetRootShakePattern());
	if (!TestNotNull(TEXT("The hit shake owns a trauma pattern"), Pattern))
	{
		return false;
	}
	TestTrue(TEXT("The hit shake restarts a single instance"), Shake->bSingleInstance);

	const FCameraShakePatternStartParams StartParams;
	Shake->ShakeScale = 0.4f;
	Pattern->StartShakePattern(StartParams);
	TestTrue(TEXT("Starting adds the shake scale as trauma"), FMath::IsNearlyEqual(Pattern->GetTrauma(), 0.4f));
	Shake->ShakeScale = 0.8f;
	Pattern->StartShakePattern(StartParams);
	TestTrue(TEXT("Trauma accumulates and clamps at one"), FMath::IsNearlyEqual(Pattern->GetTrauma(), 1.f));
	TestFalse(TEXT("A pattern with trauma is not finished"), Pattern->IsFinished());

	FCameraShakePatternUpdateParams UpdateParams;
	UpdateParams.DeltaTime = 0.1f;
	FCameraShakePatternUpdateResult Result;
	Pattern->UpdateShakePattern(UpdateParams, Result);
	TestTrue(TEXT("Trauma decays by the decay rate"), FMath::IsNearlyEqual(Pattern->GetTrauma(), 1.f - Pattern->TraumaDecayPerSecond * 0.1f, 0.001f));
	TestTrue(TEXT("The pattern scales its own result"), EnumHasAnyFlags(Result.Flags, ECameraShakePatternUpdateResultFlags::SkipAutoScale));
	TestTrue(TEXT("The lateral offset stays inside its limit"), FMath::Abs(Result.Location.Y) <= Pattern->MaxLocationOffset.Y + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("The vertical offset stays inside its limit"), FMath::Abs(Result.Location.Z) <= Pattern->MaxLocationOffset.Z + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("The roll offset stays inside its limit"), FMath::Abs(Result.Rotation.Roll) <= Pattern->MaxRotationOffset.Roll + KINDA_SMALL_NUMBER);

	for (int32 Step = 0; Step < 100 && !Pattern->IsFinished(); ++Step)
	{
		FCameraShakePatternUpdateResult StepResult;
		Pattern->UpdateShakePattern(UpdateParams, StepResult);
	}
	TestTrue(TEXT("The pattern finishes once trauma is gone"), Pattern->IsFinished());
	TestTrue(TEXT("Trauma ends at zero"), FMath::IsNearlyZero(Pattern->GetTrauma()));
	return true;
}

#endif
