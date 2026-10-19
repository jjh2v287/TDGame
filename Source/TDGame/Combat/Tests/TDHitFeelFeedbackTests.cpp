#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraShakeBase.h"
#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"
#include "Combat/Feedback/TDHitCameraShake.h"
#include "Combat/Feedback/TDHitFeelRules.h"
#include "Core/TDGameplayMessages.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDHitFeelFeedbackTestsInternal
{
	constexpr float TraumaTolerance = 0.001f;
	constexpr double FrameSeconds = 1. / 60.;

	float DecayChannel(const float ChannelTrauma, const double ElapsedSeconds)
	{
		const float DecayPerSecond = GetDefault<UTDCombatFeedbackSettings>()->ShakeTraumaDecayPerSecond;
		return FMath::Max(0.f, ChannelTrauma - DecayPerSecond * static_cast<float>(ElapsedSeconds));
	}

	struct FTDDealtHitSimulation
	{
		float Hit(const double Now, const int32 StageIndex, const ETDHitFeelTier Tier, const ETDKillRank KillRank, const float AttackerStopSeconds, const bool bIsSlowMotionActive = false)
		{
			ChannelTrauma = DecayChannel(ChannelTrauma, Now - LastTime);
			LastTime = Now;
			const float Increment = Window.AddHit(Now, AttackerKey, StageIndex, Tier, KillRank, AttackerStopSeconds, bIsSlowMotionActive, ChannelTrauma);
			ChannelTrauma = FMath::Max(ChannelTrauma, FMath::Min(ChannelTrauma + Increment, Window.ComputeChannelCap()));
			return Increment;
		}

		FTDTraumaWindow Window;
		FObjectKey AttackerKey = FObjectKey(GetTransientPackage());
		double LastTime = 0.;
		float ChannelTrauma = 0.f;
	};

	struct FTDReceivedHitSimulation
	{
		FTDReceivedTraumaResult Hit(const double Now, const float DamageRatio, const bool bIsStrongSource = false, const bool bIsDeath = false, const FVector& Direction = FVector::ForwardVector)
		{
			ChannelTrauma = DecayChannel(ChannelTrauma, Now - LastTime);
			LastTime = Now;
			const FTDReceivedTraumaResult Result = Accumulator.AddHit(Now, DamageRatio, bIsStrongSource, bIsDeath, ChannelTrauma, Direction);
			ChannelTrauma += Result.TraumaIncrement;
			InjectedTrauma += Result.TraumaIncrement;
			ShakeStarts += Result.bStartsShake ? 1 : 0;
			Kicks += Result.bShouldKick ? 1 : 0;
			return Result;
		}

		FTDReceivedHitAccumulator Accumulator;
		double LastTime = 0.;
		float ChannelTrauma = 0.f;
		float InjectedTrauma = 0.f;
		int32 ShakeStarts = 0;
		int32 Kicks = 0;
	};

	struct FTDScopedConsoleValue
	{
		FTDScopedConsoleValue(const TCHAR* VariableName, const float Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(VariableName))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetFloat();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDScopedConsoleValue()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float PreviousValue = 0.f;
	};

	UTDTraumaCameraShakePattern* MakePattern(const UTDCombatFeedbackSettings* FeedbackSettings)
	{
		UTDHitCameraShake* Shake = NewObject<UTDHitCameraShake>(GetTransientPackage());
		UTDTraumaCameraShakePattern* Pattern = Cast<UTDTraumaCameraShakePattern>(Shake->GetRootShakePattern());
		if (Pattern)
		{
			Pattern->SetFeedbackSettings(FeedbackSettings);
			Pattern->SetNoisePhase(0.f);
		}
		return Pattern;
	}

	UTDCombatFeedbackSettings* MakeSteadySettings(const float NoiseGain)
	{
		UTDCombatFeedbackSettings* FeedbackSettings = NewObject<UTDCombatFeedbackSettings>(GetTransientPackage());
		FeedbackSettings->ShakeNoiseGain = NoiseGain;
		FeedbackSettings->ShakeTraumaDecayPerSecond = 0.f;
		FeedbackSettings->CameraShakeScale = 1.f;
		return FeedbackSettings;
	}

	bool IsPatternSilent(UTDTraumaCameraShakePattern& Pattern)
	{
		Pattern.AddChannelTrauma(ETDShakeChannel::Received, 0.75f, 1.f);
		Pattern.AddKick(FRotator(0.5f, 0.5f, 0.f));
		bool bIsSilent = true;
		FCameraShakePatternUpdateParams UpdateParams;
		UpdateParams.DeltaTime = static_cast<float>(FrameSeconds);
		for (int32 Step = 0; Step < 20; ++Step)
		{
			FCameraShakePatternUpdateResult Result;
			Pattern.UpdateShakePattern(UpdateParams, Result);
			bIsSilent = bIsSilent && Result.Location.IsNearlyZero() && Result.Rotation.IsNearlyZero() && Pattern.GetLastScreenOffset().IsNearlyZero();
		}
		return bIsSilent;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelDealtTraumaTableTest, "TDGame.Combat.HitFeel.Feedback.DealtTraumaTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelDealtTraumaTableTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelFeedbackTestsInternal;

	TestTrue(TEXT("A light hit deals 0.25"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, 0, ETDKillRank::None), 0.25f, TraumaTolerance));
	TestTrue(TEXT("A heavy hit deals 0.5"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Heavy, 0, ETDKillRank::None), 0.5f, TraumaTolerance));
	TestTrue(TEXT("A normal kill deals 0.55"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, 1, ETDKillRank::Normal), 0.55f, TraumaTolerance));
	TestTrue(TEXT("An elite kill deals 0.65"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, 1, ETDKillRank::Elite), 0.65f, TraumaTolerance));
	TestTrue(TEXT("A boss kill deals 0.88"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, 1, ETDKillRank::Boss), 0.88f, TraumaTolerance));
	TestTrue(TEXT("A second kill in a swing adds 0.05"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, 2, ETDKillRank::Normal), 0.6f, TraumaTolerance));
	TestTrue(TEXT("Normal multi kills stop at 0.7"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, 9, ETDKillRank::Normal), 0.7f, TraumaTolerance));
	TestTrue(TEXT("Elite multi kills stop at 0.75"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, 9, ETDKillRank::Elite), 0.75f, TraumaTolerance));

	TestTrue(TEXT("Received trauma starts at 0.35"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(0.f, false), 0.35f, TraumaTolerance));
	TestTrue(TEXT("Five percent received is 0.43"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(0.05f, false), 0.43f, TraumaTolerance));
	TestTrue(TEXT("Fifteen percent received is 0.59"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(0.15f, false), 0.59f, TraumaTolerance));
	TestTrue(TEXT("Received trauma stops at 0.75"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(0.6f, false), 0.75f, TraumaTolerance));
	TestTrue(TEXT("Player death is 0.8"), FMath::IsNearlyEqual(UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(0.05f, true), 0.8f, TraumaTolerance));

	FTDDamageAppliedMessage StatusTick;
	StatusTick.DamageAmount = 3.f;
	StatusTick.DamageRatio = 0.2f;
	StatusTick.Delivery = ETDDamageDelivery::Status;
	TestFalse(TEXT("Status damage adds no camera trauma"), UTDCombatFeedbackSubsystem::DescribeHitPresentation(StatusTick, 0.12f).bPlaysCameraFeedback);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelDealtSwingMergeTest, "TDGame.Combat.HitFeel.Feedback.DealtSwingMerge", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelDealtSwingMergeTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelFeedbackTestsInternal;

	FTDDealtHitSimulation Swing;
	for (int32 TargetIndex = 0; TargetIndex < 4; ++TargetIndex)
	{
		Swing.Hit(1.0, 0, ETDHitFeelTier::Light, ETDKillRank::None, 0.033f);
	}
	TestTrue(TEXT("Four targets reach 0.34"), FMath::IsNearlyEqual(Swing.ChannelTrauma, 0.34f, 0.005f));
	Swing.Hit(1.0, 0, ETDHitFeelTier::Light, ETDKillRank::None, 0.033f);
	TestTrue(TEXT("Extra targets stop adding after 0.09"), FMath::IsNearlyEqual(Swing.ChannelTrauma, 0.34f, 0.005f));
	TestTrue(TEXT("A crowd swing shortens the attacker stop"), FMath::IsNearlyEqual(Swing.Window.ComputeDesiredAttackerStopSeconds(), 0.033f * 0.6f, TraumaTolerance));
	Swing.Hit(1.1, 1, ETDHitFeelTier::Light, ETDKillRank::None, 0.033f);
	TestTrue(TEXT("A new combo stage raises the channel to a single hit"), FMath::IsNearlyEqual(Swing.ChannelTrauma, 0.25f, TraumaTolerance) && Swing.Window.TargetCount == 1);
	Swing.Hit(1.6, 1, ETDHitFeelTier::Light, ETDKillRank::None, 0.033f);
	TestTrue(TEXT("The same stage after 0.45 seconds is a new swing"), FMath::IsNearlyEqual(Swing.ChannelTrauma, 0.25f, TraumaTolerance) && Swing.Window.TargetCount == 1);
	Swing.Hit(1.6, 1, ETDHitFeelTier::Light, ETDKillRank::None, 0.033f);
	TestTrue(TEXT("Two targets add one extra target step"), FMath::IsNearlyEqual(Swing.ChannelTrauma, 0.28f, TraumaTolerance));
	TestTrue(TEXT("Two targets keep the full attacker stop"), FMath::IsNearlyEqual(Swing.Window.ComputeDesiredAttackerStopSeconds(), 0.033f, TraumaTolerance));

	FTDDealtHitSimulation LateKill;
	LateKill.Hit(2.0, INDEX_NONE, ETDHitFeelTier::Light, ETDKillRank::None, 0.f);
	LateKill.Hit(2.3, INDEX_NONE, ETDHitFeelTier::Light, ETDKillRank::Normal, 0.f);
	TestTrue(TEXT("A late kill in the same swing raises the decayed channel to the kill trauma"), FMath::IsNearlyEqual(LateKill.ChannelTrauma, 0.55f, TraumaTolerance));

	FTDDealtHitSimulation HeavySwing;
	for (int32 TargetIndex = 0; TargetIndex < 5; ++TargetIndex)
	{
		HeavySwing.Hit(2.0, 3, ETDHitFeelTier::Heavy, ETDKillRank::None, 0.067f);
	}
	TestTrue(TEXT("Heavy multi hits stay under the dealt channel cap"), FMath::IsNearlyEqual(HeavySwing.ChannelTrauma, 0.59f, TraumaTolerance));
	TestTrue(TEXT("The dealt channel cap is 0.6 without kills"), FMath::IsNearlyEqual(HeavySwing.Window.ComputeChannelCap(), 0.6f, TraumaTolerance));

	FTDDealtHitSimulation KillSwing;
	KillSwing.Hit(3.0, 0, ETDHitFeelTier::Light, ETDKillRank::Normal, 0.05f);
	TestTrue(TEXT("A kill starts at the kill trauma"), FMath::IsNearlyEqual(KillSwing.ChannelTrauma, 0.55f, TraumaTolerance));
	KillSwing.Hit(3.0, 0, ETDHitFeelTier::Light, ETDKillRank::Normal, 0.05f);
	TestTrue(TEXT("A second kill in the swing adds 0.05"), FMath::IsNearlyEqual(KillSwing.ChannelTrauma, 0.6f, TraumaTolerance));
	TestTrue(TEXT("A normal kill raises the channel cap to 0.7"), FMath::IsNearlyEqual(KillSwing.Window.ComputeChannelCap(), 0.7f, TraumaTolerance));

	FTDDealtHitSimulation SlowSwing;
	SlowSwing.Hit(4.0, 3, ETDHitFeelTier::Heavy, ETDKillRank::None, 0.067f, true);
	TestTrue(TEXT("A heavy hit during slow motion deals 0.30"), FMath::IsNearlyEqual(SlowSwing.ChannelTrauma, 0.3f, 0.01f));

	TestTrue(TEXT("The first rumble in a swing plays"), FMath::IsNearlyEqual(Swing.Window.MergeRumble(0.2f), 0.2f));
	TestTrue(TEXT("An equal rumble in the same swing is merged"), FMath::IsNearlyZero(Swing.Window.MergeRumble(0.2f)));
	TestTrue(TEXT("A stronger rumble in the same swing plays"), FMath::IsNearlyEqual(Swing.Window.MergeRumble(0.8f), 0.8f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelReceivedAccumulationTest, "TDGame.Combat.HitFeel.Feedback.ReceivedAccumulation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelReceivedAccumulationTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelFeedbackTestsInternal;

	FTDReceivedHitSimulation SingleSmallHit;
	SingleSmallHit.Hit(1.0, 0.02f);
	TestEqual(TEXT("A single two percent hit does not shake"), SingleSmallHit.ShakeStarts, 0);
	TestTrue(TEXT("A single two percent hit adds no trauma"), FMath::IsNearlyZero(SingleSmallHit.InjectedTrauma));

	FTDReceivedHitSimulation SmallHitStream;
	for (int32 HitIndex = 0; HitIndex < 5; ++HitIndex)
	{
		SmallHitStream.Hit(1.0 + 0.1 * HitIndex, 0.02f);
	}
	TestEqual(TEXT("Five two percent hits in half a second shake once"), SmallHitStream.ShakeStarts, 1);
	TestEqual(TEXT("Five two percent hits in half a second kick once"), SmallHitStream.Kicks, 1);

	FTDReceivedHitSimulation FirstShake;
	const FTDReceivedTraumaResult FirstResult = FirstShake.Hit(1.0, 0.05f);
	TestTrue(TEXT("A five percent hit starts at the received formula"), FirstResult.bStartsShake && FMath::IsNearlyEqual(FirstResult.TraumaIncrement, 0.43f, TraumaTolerance));
	const FTDReceivedTraumaResult RefractoryResult = FirstShake.Hit(1.1, 0.05f);
	TestFalse(TEXT("A hit inside the refractory window does not start a new shake"), RefractoryResult.bStartsShake);
	TestFalse(TEXT("A hit inside the refractory window does not kick"), RefractoryResult.bShouldKick);
	TestTrue(TEXT("A hit inside the refractory window raises to max plus 0.03"), FMath::IsNearlyEqual(FirstShake.ChannelTrauma, 0.54f, TraumaTolerance));

	FTDReceivedHitSimulation Death;
	const FTDReceivedTraumaResult DeathResult = Death.Hit(1.0, 0.05f, false, true);
	TestTrue(TEXT("A player death shakes at 0.8"), DeathResult.bStartsShake && FMath::IsNearlyEqual(DeathResult.TraumaIncrement, 0.8f, TraumaTolerance));

	FTDReceivedHitSimulation BossTicks;
	for (int32 TickIndex = 0; TickIndex < 10; ++TickIndex)
	{
		BossTicks.Hit(1.0 + FrameSeconds * TickIndex, 0.01f, true);
	}
	TestEqual(TEXT("Ten one percent boss ticks shake once inside the refractory window"), BossTicks.ShakeStarts, 1);
	TestEqual(TEXT("Ten one percent boss ticks kick once"), BossTicks.Kicks, 1);

	FTDReceivedHitSimulation TinyBossTicks;
	for (int32 TickIndex = 0; TickIndex < 10; ++TickIndex)
	{
		TinyBossTicks.Hit(1.0 + FrameSeconds * TickIndex, 0.005f, true);
	}
	TestEqual(TEXT("Boss hits under one percent are ignored"), TinyBossTicks.ShakeStarts, 0);

	FTDReceivedHitSimulation HitFlood;
	for (int32 TickIndex = 0; TickIndex < 60; ++TickIndex)
	{
		HitFlood.Hit(1.0 + FrameSeconds * TickIndex, 0.05f);
	}
	TestTrue(TEXT("Received trauma injected in one second stays at or under 1.0"), HitFlood.InjectedTrauma <= 1.f + TraumaTolerance);

	FTDReceivedHitSimulation Reversal;
	Reversal.Hit(1.0, 0.05f, false, false, FVector::ForwardVector);
	const FTDReceivedTraumaResult ReversedResult = Reversal.Hit(1.35, 0.05f, false, false, -FVector::ForwardVector);
	TestTrue(TEXT("A reversed hit after the refractory window shakes"), ReversedResult.bStartsShake);
	TestFalse(TEXT("A reversed kick inside half a second is skipped"), ReversedResult.bShouldKick);
	const FTDReceivedTraumaResult LateResult = Reversal.Hit(2.0, 0.05f, false, false, -FVector::ForwardVector);
	TestTrue(TEXT("A later hit kicks again"), LateResult.bShouldKick);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelSlowMotionArbitrationTest, "TDGame.Combat.HitFeel.Feedback.SlowMotionArbitration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelSlowMotionArbitrationTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelFeedbackTestsInternal;

	FTDSlowMotionArbiter Arbiter;
	TestTrue(TEXT("A phase accent starts slow motion"), Arbiter.Request(10.0, 0.5f, 0.25f, false, false));
	TestTrue(TEXT("A boss kill joins the active slow motion"), Arbiter.Request(10.1, 0.2f, 0.6f, true, true));
	TestTrue(TEXT("The slowest active scale wins"), FMath::IsNearlyEqual(Arbiter.GetScale(), 0.2f));
	Arbiter.RemoveExpired(10.3);
	TestTrue(TEXT("The latest end keeps slow motion after the shorter request"), Arbiter.IsActive() && FMath::IsNearlyEqual(Arbiter.GetScale(), 0.2f));
	TestTrue(TEXT("A long request joins the same session"), Arbiter.Request(10.3, 0.3f, 2.f, false, true));
	Arbiter.RemoveExpired(10.99);
	TestTrue(TEXT("Slow motion is still active before one second"), Arbiter.IsActive());
	Arbiter.RemoveExpired(11.0);
	TestFalse(TEXT("A session never lasts more than one real second"), Arbiter.IsActive());
	TestFalse(TEXT("A new request inside the five second cooldown is ignored"), Arbiter.Request(12.0, 0.3f, 0.3f, false, true));
	TestTrue(TEXT("A boss kill ignores the cooldown"), Arbiter.Request(12.0, 0.2f, 0.6f, true, true));

	FTDSlowMotionArbiter AccentArbiter;
	TestTrue(TEXT("A phase accent plays"), AccentArbiter.Request(0.0, 0.5f, 0.25f, false, false));
	AccentArbiter.RemoveExpired(0.3);
	TestTrue(TEXT("A phase accent does not start the cooldown"), AccentArbiter.Request(1.0, 0.3f, 0.3f, false, true));
	AccentArbiter.RemoveExpired(1.4);
	TestFalse(TEXT("A victory slow motion starts the cooldown"), AccentArbiter.Request(2.0, 0.3f, 0.3f, false, true));
	TestTrue(TEXT("Slow motion is allowed again after five seconds"), AccentArbiter.Request(6.0, 0.3f, 0.3f, false, true));

	{
		const FTDScopedConsoleValue DisabledSlowMotion(TEXT("TD.Feedback.SlowMo"), 0.f);
		FTDSlowMotionArbiter DisabledArbiter;
		TestFalse(TEXT("TD.Feedback.SlowMo 0 ignores slow motion"), DisabledArbiter.Request(0.0, 0.2f, 0.6f, true, true));
		TestFalse(TEXT("Disabled slow motion stays inactive"), DisabledArbiter.IsActive());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelFlashAndSoundLimitersTest, "TDGame.Combat.HitFeel.Feedback.FlashAndSoundLimiters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelFlashAndSoundLimitersTest::RunTest(const FString& Parameters)
{
	FTDFlashRateLimiter FlashLimiter;
	TestTrue(TEXT("The first player flash may start"), FlashLimiter.CanStart(0.0));
	FlashLimiter.MarkStarted(0.0);
	TestFalse(TEXT("A player flash inside 0.33 seconds is merged"), FlashLimiter.CanStart(0.32));
	TestTrue(TEXT("A player flash after 0.33 seconds may start"), FlashLimiter.CanStart(0.34));

	FTDFlashRateLimiter SurroundedLimiter;
	int32 FlashStarts = 0;
	for (int32 HitIndex = 0; HitIndex < 7; ++HitIndex)
	{
		const double Now = 0.15 * HitIndex;
		if (!SurroundedLimiter.CanStart(Now))
		{
			continue;
		}
		SurroundedLimiter.MarkStarted(Now);
		++FlashStarts;
	}
	TestEqual(TEXT("Hits every 0.15 seconds flash three times in one second"), FlashStarts, 3);

	FTDSoundBurstLimiter SoundLimiter;
	TestTrue(TEXT("The first impact sound plays at full volume"), FMath::IsNearlyEqual(SoundLimiter.TryPlay(1.0), 1.f));
	TestTrue(TEXT("The second impact sound plays at one over root two"), FMath::IsNearlyEqual(SoundLimiter.TryPlay(1.01), 1.f / FMath::Sqrt(2.f), 0.001f));
	TestTrue(TEXT("The third impact sound plays at one over root three"), FMath::IsNearlyEqual(SoundLimiter.TryPlay(1.02), 1.f / FMath::Sqrt(3.f), 0.001f));
	TestTrue(TEXT("A fourth impact sound inside 50 ms is dropped"), FMath::IsNearlyZero(SoundLimiter.TryPlay(1.03)));
	TestTrue(TEXT("A sound after 50 ms starts a new burst"), FMath::IsNearlyEqual(SoundLimiter.TryPlay(1.06), 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelChannelCombineTest, "TDGame.Combat.HitFeel.Feedback.ChannelCombine", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelChannelCombineTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelFeedbackTestsInternal;

	const float MixedChannels[] = { 0.5f, 0.4f, 0.f };
	const float FullChannels[] = { 0.8f, 0.8f, 0.8f };
	TestTrue(TEXT("The strongest channel plus a quarter of the rest"), FMath::IsNearlyEqual(TDHitCameraShakeMath::CombineChannelTrauma(MakeArrayView(MixedChannels), 0.25f, 0.85f), 0.6f, TraumaTolerance));
	TestTrue(TEXT("Combined trauma stops at 0.85"), FMath::IsNearlyEqual(TDHitCameraShakeMath::CombineChannelTrauma(MakeArrayView(FullChannels), 0.25f, 0.85f), 0.85f, TraumaTolerance));
	TestTrue(TEXT("A boss kill may reach 1.0"), FMath::IsNearlyEqual(TDHitCameraShakeMath::CombineChannelTrauma(MakeArrayView(FullChannels), 0.25f, 1.f), 1.f, TraumaTolerance));

	UTDTraumaCameraShakePattern* Pattern = MakePattern(MakeSteadySettings(1.8f));
	if (!TestNotNull(TEXT("The hit shake owns a trauma pattern"), Pattern))
	{
		return false;
	}
	Pattern->AddChannelTrauma(ETDShakeChannel::Dealt, 0.5f, 0.6f);
	Pattern->AddChannelTrauma(ETDShakeChannel::Received, 0.4f, 0.85f);
	TestTrue(TEXT("The pattern combines its channels"), FMath::IsNearlyEqual(Pattern->GetTrauma(), 0.6f, TraumaTolerance));
	Pattern->AddChannelTrauma(ETDShakeChannel::Dealt, 0.5f, 0.6f);
	TestTrue(TEXT("A channel stops at its own cap"), FMath::IsNearlyEqual(Pattern->GetChannelTrauma(ETDShakeChannel::Dealt), 0.6f, TraumaTolerance));
	Pattern->AddChannelTrauma(ETDShakeChannel::Dealt, 0.5f, 1.f);
	TestTrue(TEXT("Without a boss kill the pattern stops at 0.85"), FMath::IsNearlyEqual(Pattern->GetTrauma(), 0.85f, TraumaTolerance));
	Pattern->AllowFullTrauma();
	TestTrue(TEXT("A boss kill lifts the pattern cap to 1.0"), FMath::IsNearlyEqual(Pattern->GetTrauma(), 1.f, TraumaTolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelNoiseGainNormalizesTest, "TDGame.Combat.HitFeel.Feedback.NoiseGainNormalizes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelNoiseGainNormalizesTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelFeedbackTestsInternal;

	TestTrue(TEXT("Noise gain scales small noise"), FMath::IsNearlyEqual(TDHitCameraShakeMath::NormalizeNoise(0.2f, 2.8f), 0.56f, TraumaTolerance));
	TestTrue(TEXT("Normalized noise is clipped at one"), FMath::IsNearlyEqual(TDHitCameraShakeMath::NormalizeNoise(0.5f, 2.8f), 1.f));
	TestTrue(TEXT("Normalized noise is clipped at minus one"), FMath::IsNearlyEqual(TDHitCameraShakeMath::NormalizeNoise(-0.9f, 2.8f), -1.f));

	UTDTraumaCameraShakePattern* RawPattern = MakePattern(MakeSteadySettings(1.f));
	UTDTraumaCameraShakePattern* GainPattern = MakePattern(MakeSteadySettings(2.8f));
	if (!TestNotNull(TEXT("The raw pattern exists"), RawPattern) || !TestNotNull(TEXT("The gain pattern exists"), GainPattern))
	{
		return false;
	}
	RawPattern->AddChannelTrauma(ETDShakeChannel::Dealt, 0.6f, 1.f);
	GainPattern->AddChannelTrauma(ETDShakeChannel::Dealt, 0.6f, 1.f);

	const float PitchLimit = static_cast<float>(GetDefault<UTDCombatFeedbackSettings>()->ShakeMaxRotationDegrees.Pitch);
	double RawPitchSum = 0.;
	double GainPitchSum = 0.;
	bool bStaysInsideLimit = true;
	FCameraShakePatternUpdateParams UpdateParams;
	UpdateParams.DeltaTime = static_cast<float>(FrameSeconds);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		FCameraShakePatternUpdateResult RawResult;
		FCameraShakePatternUpdateResult GainResult;
		RawPattern->UpdateShakePattern(UpdateParams, RawResult);
		GainPattern->UpdateShakePattern(UpdateParams, GainResult);
		RawPitchSum += FMath::Abs(RawResult.Rotation.Pitch);
		GainPitchSum += FMath::Abs(GainResult.Rotation.Pitch);
		bStaysInsideLimit = bStaysInsideLimit && FMath::Abs(GainResult.Rotation.Pitch) <= PitchLimit + KINDA_SMALL_NUMBER;
	}
	TestTrue(TEXT("Noise gain makes the shake clearly larger"), GainPitchSum > RawPitchSum * 1.5);
	TestTrue(TEXT("Normalized shake stays inside the rotation limit"), bStaysInsideLimit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelScreenOffsetBoundTest, "TDGame.Combat.HitFeel.Feedback.ScreenOffsetBound", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelScreenOffsetBoundTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelFeedbackTestsInternal;

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const FRotator& MaxRotation = FeedbackSettings.ShakeMaxRotationDegrees;
	const FRotator SaturatedRotation(MaxRotation.Pitch + FeedbackSettings.KickMaxDegrees, MaxRotation.Yaw, MaxRotation.Roll);
	const FVector SaturatedLocation(0.f, FeedbackSettings.ShakeMaxLocationCm, FeedbackSettings.ShakeMaxLocationCm);
	const double UnboundedOffsetPx = TDHitCameraShakeMath::ProjectShakeOffsetPx(SaturatedRotation, SaturatedLocation, 90.f, 1100.f).Size();
	AddInfo(FString::Printf(TEXT("Saturated noise, full trauma and a 0.5 degree kick project to %.1f px before the screen limit."), UnboundedOffsetPx));
	TestTrue(TEXT("One degree projects to about 16.76 px at 1080p and 90 degrees"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ProjectShakeOffsetPx(FRotator(0.f, 1.f, 0.f), FVector::ZeroVector, 90.f, 1100.f).X, 16.76, 0.05));

	UTDCombatFeedbackSettings* SaturatedSettings = MakeSteadySettings(1000.f);
	UTDTraumaCameraShakePattern* Pattern = MakePattern(SaturatedSettings);
	if (!TestNotNull(TEXT("The saturated pattern exists"), Pattern))
	{
		return false;
	}
	Pattern->SetCameraArmLength(1100.f);
	Pattern->AllowFullTrauma();
	Pattern->AddChannelTrauma(ETDShakeChannel::Event, 1.f, 1.f);
	Pattern->AddKick(FRotator(FeedbackSettings.KickMaxDegrees, 0.f, 0.f));

	double LargestOffsetPx = 0.;
	FCameraShakePatternUpdateParams UpdateParams;
	UpdateParams.DeltaTime = FeedbackSettings.KickRiseSeconds;
	UpdateParams.POV.FOV = 90.f;
	for (int32 Step = 0; Step < 3; ++Step)
	{
		FCameraShakePatternUpdateResult Result;
		Pattern->UpdateShakePattern(UpdateParams, Result);
		LargestOffsetPx = FMath::Max(LargestOffsetPx, TDHitCameraShakeMath::ProjectShakeOffsetPx(Result.Rotation, Result.Location, 90.f, 1100.f).Size());
	}
	TestTrue(TEXT("The worst projected shake stays at or under 29 px"), LargestOffsetPx <= FeedbackSettings.MaxShakeScreenOffsetPx + 0.01);
	TestTrue(TEXT("The worst projected shake reaches the screen limit"), LargestOffsetPx >= FMath::Min(UnboundedOffsetPx, static_cast<double>(FeedbackSettings.MaxShakeScreenOffsetPx)) - 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelArmScaleAndKickTest, "TDGame.Combat.HitFeel.Feedback.ArmScaleAndKick", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelArmScaleAndKickTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelFeedbackTestsInternal;

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	TestTrue(TEXT("The reference arm has scale one"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeArmLocationScale(1100.f, FeedbackSettings), 1.f, TraumaTolerance));
	TestTrue(TEXT("The arena arm moves the camera further"), TDHitCameraShakeMath::ComputeArmLocationScale(1540.f, FeedbackSettings) > TDHitCameraShakeMath::ComputeArmLocationScale(1100.f, FeedbackSettings));
	TestTrue(TEXT("A very long arm is clamped"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeArmLocationScale(5000.f, FeedbackSettings), 1.5f, TraumaTolerance));
	TestTrue(TEXT("A very short arm is clamped"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeArmLocationScale(100.f, FeedbackSettings), 0.5f, TraumaTolerance));

	UTDTraumaCameraShakePattern* NearPattern = MakePattern(MakeSteadySettings(1.8f));
	UTDTraumaCameraShakePattern* FarPattern = MakePattern(MakeSteadySettings(1.8f));
	if (!TestNotNull(TEXT("The near pattern exists"), NearPattern) || !TestNotNull(TEXT("The far pattern exists"), FarPattern))
	{
		return false;
	}
	NearPattern->SetCameraArmLength(1100.f);
	FarPattern->SetCameraArmLength(1540.f);
	NearPattern->AddChannelTrauma(ETDShakeChannel::Received, 0.6f, 1.f);
	FarPattern->AddChannelTrauma(ETDShakeChannel::Received, 0.6f, 1.f);
	double NearOffsetSum = 0.;
	double FarOffsetSum = 0.;
	FCameraShakePatternUpdateParams UpdateParams;
	UpdateParams.DeltaTime = static_cast<float>(FrameSeconds);
	for (int32 Step = 0; Step < 30; ++Step)
	{
		FCameraShakePatternUpdateResult NearResult;
		FCameraShakePatternUpdateResult FarResult;
		NearPattern->UpdateShakePattern(UpdateParams, NearResult);
		FarPattern->UpdateShakePattern(UpdateParams, FarResult);
		NearOffsetSum += FMath::Abs(NearResult.Location.Y) + FMath::Abs(NearResult.Location.Z);
		FarOffsetSum += FMath::Abs(FarResult.Location.Y) + FMath::Abs(FarResult.Location.Z);
	}
	TestTrue(TEXT("A longer arm uses a 1.4 times larger camera offset"), NearOffsetSum > 0. && FMath::IsNearlyEqual(FarOffsetSum / NearOffsetSum, 1.4, 0.02));

	TestTrue(TEXT("Small hits kick 0.25 degrees"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeKickDegrees(0.05f, FeedbackSettings), 0.25f, TraumaTolerance));
	TestTrue(TEXT("Mid hits kick in between"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeKickDegrees(0.15f, FeedbackSettings), 0.375f, TraumaTolerance));
	TestTrue(TEXT("Large hits kick 0.5 degrees"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeKickDegrees(0.3f, FeedbackSettings), 0.5f, TraumaTolerance));
	TestTrue(TEXT("The kick starts at rest"), FMath::IsNearlyZero(TDHitCameraShakeMath::ComputeKickEnvelope(0.f, FeedbackSettings)));
	TestTrue(TEXT("The kick peaks after its rise"), FMath::IsNearlyEqual(TDHitCameraShakeMath::ComputeKickEnvelope(0.03f, FeedbackSettings), 1.f, TraumaTolerance));
	TestTrue(TEXT("The kick eases back"), TDHitCameraShakeMath::ComputeKickEnvelope(0.1f, FeedbackSettings) < 1.f && TDHitCameraShakeMath::ComputeKickEnvelope(0.1f, FeedbackSettings) > 0.f);
	TestTrue(TEXT("The kick is gone after rise and return"), FMath::IsNearlyZero(TDHitCameraShakeMath::ComputeKickEnvelope(0.2f, FeedbackSettings)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDHitFeelShakeScaleZeroSilencesTest, "TDGame.Combat.HitFeel.Feedback.ShakeScaleZeroSilences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDHitFeelShakeScaleZeroSilencesTest::RunTest(const FString& Parameters)
{
	using namespace TDHitFeelFeedbackTestsInternal;

	{
		const FTDScopedConsoleValue FullShakeScale(TEXT("TD.Feedback.ShakeScale"), 1.f);
		UTDCombatFeedbackSettings* SettingsMuted = MakeSteadySettings(1.8f);
		SettingsMuted->CameraShakeScale = 0.f;
		TestTrue(TEXT("A zero settings scale alone disables the camera output"), FMath::IsNearlyZero(SettingsMuted->GetEffectiveShakeScale()));
		UTDTraumaCameraShakePattern* SettingsMutedPattern = MakePattern(SettingsMuted);
		TestTrue(TEXT("A zero settings scale alone moves the camera zero pixels including the kick"), SettingsMutedPattern && IsPatternSilent(*SettingsMutedPattern));
	}

	{
		const FTDScopedConsoleValue MutedShakeScale(TEXT("TD.Feedback.ShakeScale"), 0.f);
		UTDCombatFeedbackSettings* ConsoleMuted = MakeSteadySettings(1.8f);
		TestTrue(TEXT("The console variable alone disables the camera output"), FMath::IsNearlyZero(ConsoleMuted->GetEffectiveShakeScale()));
		UTDTraumaCameraShakePattern* ConsoleMutedPattern = MakePattern(ConsoleMuted);
		TestTrue(TEXT("The console variable alone moves the camera zero pixels including the kick"), ConsoleMutedPattern && IsPatternSilent(*ConsoleMutedPattern));
	}

	const FTDScopedConsoleValue FullShakeScale(TEXT("TD.Feedback.ShakeScale"), 1.f);
	UTDTraumaCameraShakePattern* AudiblePattern = MakePattern(MakeSteadySettings(1.8f));
	TestTrue(TEXT("With both scales at one the camera moves"), AudiblePattern && !IsPatternSilent(*AudiblePattern));
	return true;
}

#endif
