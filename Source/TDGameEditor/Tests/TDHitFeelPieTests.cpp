#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDCombatCharacter.h"
#include "Characters/TDGameCharacter.h"
#include "Characters/TDMonsterCharacter.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"
#include "Combat/Feedback/TDCombatOverlayWidget.h"
#include "Combat/Feedback/TDHitCameraShake.h"
#include "Combat/GAS/TDCombatAttributeSet.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TDGameplayTags.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/AssetManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Framework/TDGamePlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/TDPieWaitUntilCommand.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* const TDHitFeelPieMapPackagePath = TEXT("/Game/Combat/Maps/LV_TDMegaMagicArena");
	const TCHAR* const TDHitFeelPieGoblinSpeciesPath = TEXT("/Game/MonsterAI/Species/DA_TDMonster_Goblin.DA_TDMonster_Goblin");
	const TCHAR* const TDHitFeelPieBossSpeciesPath = TEXT("/Game/MonsterAI/Species/DA_TDMonster_CentaurHunter.DA_TDMonster_CentaurHunter");
	const TCHAR* const TDHitFeelPieOutputRootFolder = TEXT("AgentOps");
	const TCHAR* const TDHitFeelPieOutputLeafFolder = TEXT("hitfeel/pie");
	const TCHAR* const TDHitFeelPieDateFormat = TEXT("%Y%m%d");
	const TCHAR* const TDHitFeelPieReportFileName = TEXT("hitfeel.json");
	const TCHAR* const TDHitFeelPieFramesFileName = TEXT("hitfeel-frames.csv");
	const TCHAR* const TDHitFeelPieShakeScaleVariableName = TEXT("TD.Feedback.ShakeScale");
	const TCHAR* const TDHitFeelPieVignettePropertyName = TEXT("DamageVignetteMaterialInstance");
	const TCHAR* const TDHitFeelPieCalibrationPhase = TEXT("calibration");
	const TCHAR* const TDHitFeelPieMeasurePhase = TEXT("measure");
	const TCHAR* const TDHitFeelPieArenaPhase = TEXT("arena");
	const TCHAR* const TDHitFeelPieControlPhase = TEXT("control");
	const TCHAR* const TDHitFeelPieEventPhase = TEXT("event");
	const TCHAR* const TDHitFeelPieScreenshotPhase = TEXT("screenshot");
	const TCHAR* const TDHitFeelPieTimelineEnsureExpression = TEXT("TimelineTrait.GetState(Context, ChildState)");
	const TCHAR* const TDHitFeelPieEnsureHeadingPattern = TEXT("^LogOutputDevice: === Handled ensure: ===\\s*$");
	const TCHAR* const TDHitFeelPieEnsureBlankLinePattern = TEXT("^LogOutputDevice:\\s*$");
	const TCHAR* const TDHitFeelPieEnsureStackHeadingPattern = TEXT("^LogOutputDevice: Stack:\\s*$");
	const TCHAR* const TDHitFeelPieEnsureCallstackLinePattern = TEXT("^LogOutputDevice: \\[Callstack\\] 0x");
	const FName TDHitFeelPieUafComponentClassName(TEXT("UAFComponent"));

	constexpr double TDHitFeelPieSetupTimeoutSeconds = 20.0;
	constexpr double TDHitFeelPieAssetLoadTimeoutSeconds = 20.0;
	constexpr double TDHitFeelPieRunTimeoutSeconds = 60.0;
	constexpr double TDHitFeelPieScreenshotTimeoutSeconds = 5.0;
	constexpr double TDHitFeelPieFixedDeltaSeconds = 1.0 / 60.0;
	constexpr double TDHitFeelPieDeltaToleranceSeconds = 1.0e-4;
	constexpr int32 TDHitFeelPieWarmupFrames = 30;
	constexpr int32 TDHitFeelPieSettleFrames = 45;
	constexpr int32 TDHitFeelPieSpawnSettleFrames = 60;
	constexpr int32 TDHitFeelPieQuietWaitLimitFrames = 120;
	constexpr int32 TDHitFeelPieArmSettleFrames = 30;
	constexpr int32 TDHitFeelPieObserveFrames = 45;
	constexpr int32 TDHitFeelPieSurroundObserveFrames = 60;
	constexpr int32 TDHitFeelPieSurroundHitCount = 6;
	constexpr int32 TDHitFeelPieSurroundHitIntervalFrames = 9;
	constexpr int32 TDHitFeelPieBossKillDelayFrames = 1;
	constexpr int32 TDHitFeelPieBossObserveFrames = 120;
	constexpr int32 TDHitFeelPieBossDilationCheckFrames = 90;
	constexpr int32 TDHitFeelPieComboFrameLimit = 420;
	constexpr int32 TDHitFeelPieComboAttackIntervalFrames = 6;
	constexpr int32 TDHitFeelPieComboStageCount = 4;
	constexpr int32 TDHitFeelPieComboTailFrames = 20;
	constexpr int32 TDHitFeelPieVictimStopStartFrames = 3;
	constexpr int32 TDHitFeelPieRepetitions = 5;
	constexpr int32 TDHitFeelPieScreenshotBurstFrames = 8;
	constexpr int32 TDHitFeelPieRingMonsterCount = 6;
	constexpr int32 TDHitFeelPieMultiTargetCount = 4;
	constexpr int32 TDHitFeelPiePlayerCombatLevel = 10;
	constexpr int32 TDHitFeelPieSurroundFlashLimit = 3;
	constexpr int32 TDHitFeelPieSurroundKickLimit = 4;
	constexpr float TDHitFeelPieRingRadiusCm = 260.f;
	constexpr float TDHitFeelPieKillSpawnDistanceCm = 220.f;
	constexpr float TDHitFeelPieKillSpawnYawDegrees = 30.f;
	constexpr float TDHitFeelPieBossSpawnDistanceCm = 450.f;
	constexpr float TDHitFeelPieComboSurfaceGapCm = 40.f;
	constexpr float TDHitFeelPieLethalDamage = 1000000.f;
	constexpr float TDHitFeelPieDealtLightRatio = 0.05f;
	constexpr float TDHitFeelPieDealtHeavyRatio = 0.3f;
	constexpr float TDHitFeelPieReceivedSmallRatio = 0.05f;
	constexpr float TDHitFeelPieReceivedMediumRatio = 0.15f;
	constexpr float TDHitFeelPieReceivedLargeRatio = 0.25f;
	constexpr float TDHitFeelPieReceivedTinyRatio = 0.02f;
	constexpr float TDHitFeelPieStatusRatio = 0.05f;
	constexpr float TDHitFeelPieSurroundRatio = 0.05f;
	constexpr float TDHitFeelPieHeavyTraumaRatio = 0.25f;
	constexpr float TDHitFeelPieMonsterHealthRefillRatio = 0.5f;
	constexpr float TDHitFeelPieTraumaDecayPerSecond = 2.2f;
	constexpr float TDHitFeelPieTraumaTolerance = 0.01f;
	constexpr float TDHitFeelPieQuietTrauma = 0.001f;
	constexpr float TDHitFeelPieIntensityExponent = 1.5f;
	constexpr float TDHitFeelPieReceivedToDealtIntensityMinimum = 1.6f;
	constexpr float TDHitFeelPieMultiTargetTraumaAllowance = 0.095f;
	constexpr float TDHitFeelPieMaxShakePx = 29.f;
	constexpr float TDHitFeelPieZeroShakePxLimit = 0.5f;
	constexpr float TDHitFeelPieBaselineShakePxWarning = 0.5f;
	constexpr float TDHitFeelPieArenaArmLengthCm = 1540.f;
	constexpr float TDHitFeelPieArmLengthToleranceCm = 1.f;
	constexpr float TDHitFeelPieArenaPxTolerance = 0.03f;
	constexpr float TDHitFeelPieArenaLocationRatio = 1.4f;
	constexpr float TDHitFeelPieArenaLocationTolerance = 0.02f;
	constexpr float TDHitFeelPieReferenceWidthPx = 1920.f;
	constexpr float TDHitFeelPieNormalShakeScale = 1.f;
	constexpr float TDHitFeelPieVictimLightStopSeconds = 0.067f;
	constexpr float TDHitFeelPieVictimHeavyStopSeconds = 0.11f;
	constexpr float TDHitFeelPieAttackerLightStopSeconds = 0.033f;
	constexpr float TDHitFeelPieAttackerHeavyStopSeconds = 0.067f;
	constexpr float TDHitFeelPieAttackerSlowPlayRate = 0.05f;
	constexpr float TDHitFeelPieActionClockEpsilon = 1.0e-4f;
	constexpr float TDHitFeelPieRootMotionToleranceCm = 3.f;
	constexpr float TDHitFeelPieComboRootMotionCm[] = { 34.f, 36.f, 38.f, 54.f };
	constexpr float TDHitFeelPieBossAttackerStopLimitSeconds = 0.06f;
	constexpr float TDHitFeelPieBossShakeLimitSeconds = 0.5f;
	constexpr float TDHitFeelPieBossSlowMotionLimitSeconds = 1.f;
	constexpr float TDHitFeelPieDilationTolerance = 0.001f;
	constexpr float TDHitFeelPieNoiseGainOffsets[] = { -0.6f, -0.3f, 0.f, 0.3f, 0.6f };
	constexpr float TDHitFeelPieMinimumNoiseGain = 0.1f;
	constexpr float TDHitFeelPieNoiseGainTolerance = 0.3f;
	constexpr float TDHitFeelPieNoiseGainEffectPx = 0.1f;
	constexpr float TDHitFeelPieNoisePhaseStep = 0.618f;
	constexpr float TDHitFeelPieNoisePhasePeriod = 1.f;
	constexpr float TDHitFeelPieTraumaDecaySlack = 0.003f;
	constexpr float TDHitFeelPieKillPoseHoldSeconds = 0.07f;
	constexpr float TDHitFeelPieVictimCriticalBonusSeconds = 0.033f;
	constexpr float TDHitFeelPieVictimCriticalStopSeconds = TDHitFeelPieVictimHeavyStopSeconds + TDHitFeelPieVictimCriticalBonusSeconds;
	constexpr float TDHitFeelPieMinimumSpeedDropCmPerSecond = 1.f;
	constexpr double TDHitFeelPiePhaseRunTimeoutSeconds = 180.0;
	constexpr int32 TDHitFeelPiePhaseEntryWaitLimitFrames = 3600;
	constexpr float TDHitFeelPiePlayerStopSeconds = 0.04f;
	constexpr float TDHitFeelPieKnockbackLightDistanceCm = 25.f;
	constexpr float TDHitFeelPieKnockbackLightDistanceToleranceCm = 6.f;
	constexpr float TDHitFeelPieKnockbackFinisherDistanceCm = 85.f;
	constexpr float TDHitFeelPieKnockbackFinisherDistanceToleranceCm = 12.f;
	constexpr float TDHitFeelPieComboPinGapCm = 30.f;
	constexpr int32 TDHitFeelPieKnockbackDistanceWindowFrames = 30;
	constexpr float TDHitFeelPieKnockbackDefaultSpeed = 450.f;
	constexpr float TDHitFeelPieKnockbackTolerance = 0.1f;
	constexpr float TDHitFeelPieSpellKnockbackRatio = 0.3f;
	constexpr float TDHitFeelPieApproachSurfaceGapCm = 160.f;
	constexpr float TDHitFeelPieApproachExpectedCm = 100.f;
	constexpr float TDHitFeelPieApproachToleranceCm = 10.f;
	constexpr float TDHitFeelPieBossApproachLimitCm = 40.f;
	constexpr float TDHitFeelPieBossCommittedHitRatio = 0.02f;
	constexpr float TDHitFeelPiePhaseDamageMargin = 0.02f;
	constexpr float TDHitFeelPieSlowMotionScaleTolerance = 0.001f;
	constexpr int32 TDHitFeelPieCalibrationRepetitions = 2;
	constexpr int32 TDHitFeelPieCriticalRepetitions = 3;
	constexpr int32 TDHitFeelPieVictimStopStartLimitFrames = 1;
	constexpr int32 TDHitFeelPieCommittedMinionStopLimitFrames = 5;
	constexpr int32 TDHitFeelPieKillDeathDelayMinimumFrames = 4;
	constexpr int32 TDHitFeelPieKnockbackWindowFrames = 9;
	constexpr int32 TDHitFeelPieKnockbackWatchLimitFrames = 50;
	constexpr int32 TDHitFeelPieApproachSampleFrames = 12;
	constexpr int32 TDHitFeelPieApproachObserveFrames = 40;
	constexpr int32 TDHitFeelPiePhaseObserveFrames = 90;
	constexpr int32 TDHitFeelPieBossSettleFrames = 60;
	constexpr int32 TDHitFeelPieBossCommittedWaitLimitFrames = 600;
	constexpr int32 TDHitFeelPieSlowMotionCooldownMarginFrames = 30;
	constexpr int32 TDHitFeelPieFrameTolerance = 1;

	enum class ETDHitFeelPieScenario : uint8
	{
		DealtLight,
		DealtHeavy,
		DealtKill,
		DealtMultiTarget,
		ReceivedSmall,
		ReceivedMedium,
		ReceivedLarge,
		ReceivedTiny,
		ReceivedStatus,
		ReceivedZeroShake,
		Surrounded,
		Combo,
		BossKill,
		ScreenshotBurst,
		DealtCritical,
		DealtSpellKnockback,
		ApproachBaseline,
		ApproachStep,
		BossApproach,
		BossPhase,
		BossCommittedHit
	};

	const ETDHitFeelPieScenario TDHitFeelPieCalibrationScenarios[] = {
		ETDHitFeelPieScenario::DealtLight,
		ETDHitFeelPieScenario::DealtHeavy,
		ETDHitFeelPieScenario::ReceivedSmall,
		ETDHitFeelPieScenario::ReceivedMedium,
		ETDHitFeelPieScenario::ReceivedLarge
	};

	const ETDHitFeelPieScenario TDHitFeelPieMeasuredScenarios[] = {
		ETDHitFeelPieScenario::DealtLight,
		ETDHitFeelPieScenario::DealtHeavy,
		ETDHitFeelPieScenario::ReceivedSmall,
		ETDHitFeelPieScenario::ReceivedMedium,
		ETDHitFeelPieScenario::ReceivedLarge,
		ETDHitFeelPieScenario::DealtKill
	};

	const ETDHitFeelPieScenario TDHitFeelPieArenaScenarios[] = {
		ETDHitFeelPieScenario::DealtHeavy,
		ETDHitFeelPieScenario::ReceivedMedium
	};

	const ETDHitFeelPieScenario TDHitFeelPieShakeScenarios[] = {
		ETDHitFeelPieScenario::DealtLight,
		ETDHitFeelPieScenario::DealtHeavy,
		ETDHitFeelPieScenario::DealtKill,
		ETDHitFeelPieScenario::DealtMultiTarget,
		ETDHitFeelPieScenario::ReceivedSmall,
		ETDHitFeelPieScenario::ReceivedMedium,
		ETDHitFeelPieScenario::ReceivedLarge,
		ETDHitFeelPieScenario::Surrounded,
		ETDHitFeelPieScenario::Combo,
		ETDHitFeelPieScenario::BossKill,
		ETDHitFeelPieScenario::DealtCritical,
		ETDHitFeelPieScenario::BossPhase
	};

	const ETDHitFeelPieScenario TDHitFeelPieTraumaScenarios[] = {
		ETDHitFeelPieScenario::DealtLight,
		ETDHitFeelPieScenario::DealtHeavy,
		ETDHitFeelPieScenario::DealtCritical,
		ETDHitFeelPieScenario::DealtKill,
		ETDHitFeelPieScenario::ReceivedSmall,
		ETDHitFeelPieScenario::ReceivedMedium,
		ETDHitFeelPieScenario::ReceivedLarge,
		ETDHitFeelPieScenario::BossKill
	};

	const ETDHitFeelPieScenario TDHitFeelPieVictimStopScenarios[] = {
		ETDHitFeelPieScenario::DealtLight,
		ETDHitFeelPieScenario::DealtHeavy,
		ETDHitFeelPieScenario::DealtCritical
	};

	const ETDHitFeelPieScenario TDHitFeelPieKillScenarios[] = {
		ETDHitFeelPieScenario::DealtKill,
		ETDHitFeelPieScenario::BossKill
	};

	const ETDHitFeelPieScenario TDHitFeelPieQuietScenarios[] = {
		ETDHitFeelPieScenario::ReceivedStatus,
		ETDHitFeelPieScenario::ReceivedTiny
	};

	struct FTDHitFeelPieEnvironmentRecord
	{
		bool bIsApplied = false;
		bool bPreviousUseFixedTimeStep = false;
		double PreviousFixedDeltaSeconds = 0.0;
		bool bPreviousRandomizeShakePhase = true;
		float PreviousNoiseGain = 0.f;
		float PreviousCameraShakeScale = 1.f;
		bool bHasShakeScaleVariable = false;
		float PreviousShakeScaleVariable = 1.f;
	};

	FTDHitFeelPieEnvironmentRecord& GetHitFeelPieEnvironmentRecord()
	{
		static FTDHitFeelPieEnvironmentRecord Record;
		return Record;
	}

	IConsoleVariable* FindHitFeelPieConsoleVariable(const TCHAR* VariableName)
	{
		return IConsoleManager::Get().FindConsoleVariable(VariableName);
	}

	void RestoreHitFeelPieEnvironmentRecord()
	{
		FTDHitFeelPieEnvironmentRecord& Record = GetHitFeelPieEnvironmentRecord();
		if (!Record.bIsApplied)
		{
			return;
		}

		FApp::SetUseFixedTimeStep(Record.bPreviousUseFixedTimeStep);
		FApp::SetFixedDeltaTime(Record.PreviousFixedDeltaSeconds);
		UTDCombatFeedbackSettings* Settings = GetMutableDefault<UTDCombatFeedbackSettings>();
		Settings->bRandomizeShakePhase = Record.bPreviousRandomizeShakePhase;
		Settings->ShakeNoiseGain = Record.PreviousNoiseGain;
		Settings->CameraShakeScale = Record.PreviousCameraShakeScale;
		IConsoleVariable* ShakeScaleVariable = FindHitFeelPieConsoleVariable(TDHitFeelPieShakeScaleVariableName);
		if (Record.bHasShakeScaleVariable && ShakeScaleVariable)
		{
			ShakeScaleVariable->Set(Record.PreviousShakeScaleVariable, ECVF_SetByCode);
		}
		Record.bIsApplied = false;
	}

	class FTDHitFeelPieEnvironmentGuard
	{
	public:
		FTDHitFeelPieEnvironmentGuard() = default;
		FTDHitFeelPieEnvironmentGuard(const FTDHitFeelPieEnvironmentGuard&) = delete;
		FTDHitFeelPieEnvironmentGuard& operator=(const FTDHitFeelPieEnvironmentGuard&) = delete;

		~FTDHitFeelPieEnvironmentGuard()
		{
			Restore();
		}

		static bool RecoverLeakedEnvironment()
		{
			if (!GetHitFeelPieEnvironmentRecord().bIsApplied)
			{
				return false;
			}
			RestoreHitFeelPieEnvironmentRecord();
			return true;
		}

		bool Apply()
		{
			FTDHitFeelPieEnvironmentRecord& Record = GetHitFeelPieEnvironmentRecord();
			if (Record.bIsApplied)
			{
				return bOwnsRecord;
			}

			const UTDCombatFeedbackSettings* Settings = GetDefault<UTDCombatFeedbackSettings>();
			const IConsoleVariable* ShakeScaleVariable = FindHitFeelPieConsoleVariable(TDHitFeelPieShakeScaleVariableName);
			Record.bPreviousUseFixedTimeStep = FApp::UseFixedTimeStep();
			Record.PreviousFixedDeltaSeconds = FApp::GetFixedDeltaTime();
			Record.bPreviousRandomizeShakePhase = Settings->bRandomizeShakePhase;
			Record.PreviousNoiseGain = Settings->ShakeNoiseGain;
			Record.PreviousCameraShakeScale = Settings->CameraShakeScale;
			Record.bHasShakeScaleVariable = ShakeScaleVariable != nullptr;
			Record.PreviousShakeScaleVariable = ShakeScaleVariable ? ShakeScaleVariable->GetFloat() : TDHitFeelPieNormalShakeScale;
			Record.bIsApplied = true;
			bOwnsRecord = true;

			FApp::SetUseFixedTimeStep(true);
			FApp::SetFixedDeltaTime(TDHitFeelPieFixedDeltaSeconds);
			GetMutableDefault<UTDCombatFeedbackSettings>()->bRandomizeShakePhase = false;
			SetShakeMuted(false);
			return true;
		}

		void Restore()
		{
			if (!bOwnsRecord)
			{
				return;
			}
			bOwnsRecord = false;
			RestoreHitFeelPieEnvironmentRecord();
		}

		bool IsApplied() const { return bOwnsRecord; }

		void SetNoiseGain(float NoiseGain)
		{
			if (!bOwnsRecord)
			{
				return;
			}
			GetMutableDefault<UTDCombatFeedbackSettings>()->ShakeNoiseGain = NoiseGain;
		}

		void SetShakeMuted(bool bIsMuted)
		{
			if (!bOwnsRecord)
			{
				return;
			}
			const float ShakeScale = bIsMuted ? 0.f : TDHitFeelPieNormalShakeScale;
			GetMutableDefault<UTDCombatFeedbackSettings>()->CameraShakeScale = ShakeScale;
			IConsoleVariable* ShakeScaleVariable = FindHitFeelPieConsoleVariable(TDHitFeelPieShakeScaleVariableName);
			if (!GetHitFeelPieEnvironmentRecord().bHasShakeScaleVariable || !ShakeScaleVariable)
			{
				return;
			}
			ShakeScaleVariable->Set(ShakeScale, ECVF_SetByCode);
		}

	private:
		bool bOwnsRecord = false;
	};

	class FTDHitFeelPieTickHook
	{
	public:
		FTDHitFeelPieTickHook() = default;
		FTDHitFeelPieTickHook(const FTDHitFeelPieTickHook&) = delete;
		FTDHitFeelPieTickHook& operator=(const FTDHitFeelPieTickHook&) = delete;

		~FTDHitFeelPieTickHook()
		{
			Stop();
		}

		void Start(TFunction<void(UWorld&, float)> InSampleFrame)
		{
			if (Handle.IsValid())
			{
				return;
			}
			SampleFrame = MoveTemp(InSampleFrame);
			Handle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FTDHitFeelPieTickHook::HandleWorldPostActorTick);
		}

		void Stop()
		{
			if (!Handle.IsValid())
			{
				return;
			}
			FWorldDelegates::OnWorldPostActorTick.Remove(Handle);
			Handle.Reset();
			SampleFrame.Reset();
		}

	private:
		void HandleWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
		{
			if (!World || World->WorldType != EWorldType::PIE || !SampleFrame)
			{
				return;
			}
			SampleFrame(*World, DeltaSeconds);
		}

		FDelegateHandle Handle;
		TFunction<void(UWorld&, float)> SampleFrame;
	};

	struct FTDHitFeelPieBand
	{
		float MinPx = 0.f;
		float MaxPx = 0.f;
	};

	struct FTDHitFeelPieSpan
	{
		double FirstSeconds = 0.0;
		double LastSeconds = 0.0;
		int32 FrameCount = 0;

		void Add(bool bIsActive, double Seconds)
		{
			if (!bIsActive)
			{
				return;
			}
			if (FrameCount == 0)
			{
				FirstSeconds = Seconds;
			}
			LastSeconds = Seconds;
			++FrameCount;
		}

		double GetSeconds() const
		{
			return FrameCount * TDHitFeelPieFixedDeltaSeconds;
		}
	};

	struct FTDHitFeelPieFrameRow
	{
		int32 Frame = 0;
		int32 RunIndex = INDEX_NONE;
		double WorldSeconds = 0.0;
		double RealSeconds = 0.0;
		double WorldDeltaSeconds = 0.0;
		double RealDeltaSeconds = 0.0;
		float TimeDilation = 1.f;
		bool bIsFixedStep = false;
		float Trauma = 0.f;
		float ShakePx = 0.f;
		float ShakeRotationDegrees = 0.f;
		float ShakeLocationCm = 0.f;
		FVector2D ReportedShakeOffset = FVector2D::ZeroVector;
		float FovDegrees = 0.f;
		float ArmLengthCm = 0.f;
		float CameraDistanceCm = 0.f;
		int32 ShakesStarted = 0;
		int32 FlashCount = 0;
		int32 KickCount = 0;
		int32 BossPhasePulses = 0;
		float FlashIntensity = -1.f;
		float VignetteIntensity = -1.f;
		float VictimStopRemainingSeconds = 0.f;
		bool bIsVictimStopPending = false;
		bool bIsAttackerStopActive = false;
		float PlayerStopRemainingSeconds = 0.f;
		bool bIsPlayerPlayingAction = false;
		float PlayerActionSeconds = 0.f;
		int32 ComboStageIndex = INDEX_NONE;
		int32 PrimaryComboStep = 0;
		bool bIsUafActive = false;
		FVector PlayerLocation = FVector::ZeroVector;
		float PlayerAttackerStopRemainingSeconds = 0.f;
		float ChannelTrauma[TDHitCameraShakeMath::ShakeChannelCount] = {};
		bool bIsVictimUafActive = true;
		bool bIsVictimRootMotion = false;
		float VictimActionSeconds = 0.f;
		bool bIsVictimPlayingDeath = false;
		bool bIsVictimRagdolling = false;
		FVector VictimLocation = FVector::ZeroVector;
	};

	struct FTDHitFeelPieComboHit
	{
		int32 Frame = 0;
		int32 StageCount = 0;
		bool bWasCritical = false;
		bool bWasFromPlayer = false;
		bool bWasVictimCommitted = false;
		bool bWasVictimDangerWindup = false;
		float DamageRatio = 0.f;
	};

	struct FTDHitFeelPieComboStage
	{
		int32 StageIndexValue = INDEX_NONE;
		int32 FirstFrame = INDEX_NONE;
		int32 LastFrame = INDEX_NONE;
		FVector StartLocation = FVector::ZeroVector;
		FVector EndLocation = FVector::ZeroVector;
		int32 HitCount = 0;
		bool bHadCriticalHit = false;
		bool bWasVictimCommitted = false;
		bool bWasVictimDangerWindup = false;
		int32 SlowedFrames = 0;
		int32 SlowedFramesWithUafInactive = 0;
		int32 AttackerActiveFrames = 0;
		int32 VictimStopFrames = INDEX_NONE;
		int32 VictimStopStartDelayFrames = 0;
		int32 FirstHitFrame = INDEX_NONE;
		float KnockbackSpeed = -1.f;
		float KnockbackPeakSpeed = -1.f;
		float KnockbackDistanceCm = -1.f;
		float RootMotionCm = 0.f;
	};

	struct FTDHitFeelPieKnockbackSample
	{
		float PeakSpeed = -1.f;
		float LaunchSpeed = -1.f;
		float BrakingFactor = -1.f;
	};

	struct FTDHitFeelPieRun
	{
		int32 RunIndex = INDEX_NONE;
		ETDHitFeelPieScenario Scenario = ETDHitFeelPieScenario::DealtLight;
		FString Phase;
		int32 Repetition = 0;
		float NoiseGain = 0.f;
		float ArmLengthCm = 0.f;
		float TargetRatio = 0.f;
		float AppliedRatio = 0.f;
		float ExpectedTrauma = -1.f;
		bool bIsInjected = false;
		bool bDidInjectionApply = false;
		bool bIsFinished = false;
		bool bIsValid = false;
		bool bIsContaminated = false;
		bool bIsVictimCommitted = false;
		bool bIsBossVictim = false;
		bool bWasQuietAtInject = true;
		bool bDidStartPlayerSwing = false;
		int32 InjectFrame = INDEX_NONE;
		int32 KillFrame = INDEX_NONE;
		int32 EndFrame = INDEX_NONE;
		int32 FirstRowIndex = 0;
		int32 RowCount = 0;
		int32 InvalidRowCount = 0;
		int32 ShakesBefore = 0;
		int32 FlashesBefore = 0;
		int32 KicksBefore = 0;
		int32 PhasePulsesBefore = 0;
		int32 ShakesStartedDelta = 0;
		int32 FlashDelta = 0;
		int32 KickDelta = 0;
		int32 PhasePulseDelta = 0;
		float BaselineShakePx = 0.f;
		float InjectTrauma = -1.f;
		bool bDidShakeStartAtInject = false;
		float FirstFrameTrauma = -1.f;
		double FirstFrameRealDelta = 0.0;
		float StartTrauma = -1.f;
		FString StartTraumaSource;
		float MaxTrauma = 0.f;
		float MaxShakePx = 0.f;
		int32 VictimStopFrames = INDEX_NONE;
		int32 VictimPendingFrames = 0;
		FTDHitFeelPieSpan AttackerStopSpan;
		FTDHitFeelPieSpan ShakeSpan;
		FTDHitFeelPieSpan SlowMotionSpan;
		float DilationAtCheck = -1.f;
		int32 SurroundHitsDone = 0;
		int32 ScreenshotsRequested = 0;
		int32 ComboAttackCommands = 0;
		int32 ComboIdleFrames = 0;
		TArray<int32> ComboStageValues;
		TArray<FTDHitFeelPieComboStage> ComboStages;
		float NoisePhase = 0.f;
		bool bDidSetNoisePhase = false;
		bool bIsVictimDangerWindup = false;
		bool bIsVictimEliteOrBoss = false;
		float MaxShakeLocationCm = 0.f;
		int32 TraumaDecayViolations = 0;
		float WorstTraumaDecayExcess = 0.f;
		int32 WorstTraumaDecayFrame = INDEX_NONE;
		int32 VictimStopStartDelayFrames = 0;
		int32 KillPoseHoldFrames = INDEX_NONE;
		int32 DeathStartFrame = INDEX_NONE;
		int32 AttackerStopLastFrame = INDEX_NONE;
		int32 SlowMotionFirstFrame = INDEX_NONE;
		float SlowMotionMinDilation = 1.f;
		float PeakEventTrauma = 0.f;
		float PhaseMark = -1.f;
		int32 PhaseEntryFrame = INDEX_NONE;
		float EventStartTrauma = -1.f;
		float KnockbackSpeed = -1.f;
		float KnockbackPeakSpeed = -1.f;
		float KnockbackBrakingFactor = -1.f;
		int32 KnockbackWatchStartFrame = INDEX_NONE;
		int32 KnockbackStopEndFrame = INDEX_NONE;
		TArray<int32> WatchedKnockbackStages;
		FVector PlayerStartLocation = FVector::ZeroVector;
		FVector TargetPlacedLocation = FVector::ZeroVector;
		float ApproachDisplacementCm = -1.f;
		int32 CommittedWaitFrames = 0;
	};

	struct FTDHitFeelPieState
	{
		bool bIsAborted = false;
		bool bIsPlayerInvulnerableForTest = false;
		bool bWasLeakRecovered = false;
		bool bHasNoiseGainEffect = false;
		FString OutputDirectory;
		TWeakObjectPtr<ATDGamePlayerController> Controller;
		TWeakObjectPtr<ATDGameCharacter> Player;
		TWeakObjectPtr<UTDCombatFeedbackSubsystem> Feedback;
		TWeakObjectPtr<UActorComponent> PlayerUafComponent;
		FObjectProperty* VignetteProperty = nullptr;
		FName FlashIntensityParameterName;
		FName VignetteIntensityParameterName;
		TSharedPtr<FStreamableHandle> AssetHandle;
		TArray<TWeakObjectPtr<ATDMonsterCharacter>> RingMonsters;
		TWeakObjectPtr<ATDMonsterCharacter> BossMonster;
		TWeakObjectPtr<ATDMonsterCharacter> FocusMonster;
		TWeakObjectPtr<ATDCombatCharacter> FocusVictim;
		TWeakObjectPtr<UActorComponent> FocusVictimUafComponent;
		TWeakObjectPtr<UTDTraumaCameraShakePattern> ShakePattern;
		TArray<float> NoiseGainCandidates;
		int32 ComboLiveStageCount = 0;
		FVector PlayerHomeLocation = FVector::ZeroVector;
		FVector PlayerHomeForward = FVector::ForwardVector;
		FVector ComboForward = FVector::ForwardVector;
		float OriginalArmLengthCm = 0.f;
		float ConfiguredNoiseGain = 0.f;
		float RecommendedNoiseGain = 0.f;
		int32 FrameIndex = 0;
		double RealSecondsElapsed = 0.0;
		float PreviousTimeDilation = 1.f;
		int32 ActiveRunIndex = INDEX_NONE;
		bool bIsInjectingDamage = false;
		TArray<int32> ContaminationFrames;
		TArray<FTDHitFeelPieComboHit> ComboHits;
		TWeakObjectPtr<UTDCombatComponent> PlayerObservedCombat;
		FDelegateHandle PlayerDamageHandle;
		TWeakObjectPtr<UTDCombatComponent> ComboObservedCombat;
		FDelegateHandle ComboDamageHandle;
		TArray<FTDHitFeelPieFrameRow> Rows;
		TArray<FTDHitFeelPieRun> Runs;
		TArray<FString> RequestedScreenshots;
		TArray<TSharedPtr<FJsonValue>> ScreenshotRows;
		TArray<TSharedPtr<FJsonValue>> TimedOutSteps;
		TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		FTDHitFeelPieEnvironmentGuard Environment;
		FTDHitFeelPieTickHook TickHook;
	};

	const TCHAR* GetHitFeelPieScenarioName(ETDHitFeelPieScenario Scenario)
	{
		switch (Scenario)
		{
		case ETDHitFeelPieScenario::DealtLight: return TEXT("dealt_light");
		case ETDHitFeelPieScenario::DealtHeavy: return TEXT("dealt_heavy");
		case ETDHitFeelPieScenario::DealtKill: return TEXT("dealt_kill");
		case ETDHitFeelPieScenario::DealtMultiTarget: return TEXT("dealt_four_targets");
		case ETDHitFeelPieScenario::ReceivedSmall: return TEXT("received_5_percent");
		case ETDHitFeelPieScenario::ReceivedMedium: return TEXT("received_15_percent");
		case ETDHitFeelPieScenario::ReceivedLarge: return TEXT("received_25_percent");
		case ETDHitFeelPieScenario::ReceivedTiny: return TEXT("received_2_percent");
		case ETDHitFeelPieScenario::ReceivedStatus: return TEXT("received_status");
		case ETDHitFeelPieScenario::ReceivedZeroShake: return TEXT("received_shake_scale_zero");
		case ETDHitFeelPieScenario::Surrounded: return TEXT("surrounded_six");
		case ETDHitFeelPieScenario::Combo: return TEXT("combo_four_stages");
		case ETDHitFeelPieScenario::BossKill: return TEXT("boss_kill");
		case ETDHitFeelPieScenario::ScreenshotBurst: return TEXT("screenshot_burst");
		case ETDHitFeelPieScenario::DealtCritical: return TEXT("dealt_critical");
		case ETDHitFeelPieScenario::DealtSpellKnockback: return TEXT("dealt_spell_knockback");
		case ETDHitFeelPieScenario::ApproachBaseline: return TEXT("approach_baseline_40cm");
		case ETDHitFeelPieScenario::ApproachStep: return TEXT("approach_step_160cm");
		case ETDHitFeelPieScenario::BossApproach: return TEXT("boss_approach_160cm");
		case ETDHitFeelPieScenario::BossPhase: return TEXT("boss_phase_entry");
		case ETDHitFeelPieScenario::BossCommittedHit: return TEXT("boss_committed_hit");
		}
		return TEXT("unknown");
	}

	bool TryGetHitFeelPieBand(ETDHitFeelPieScenario Scenario, FTDHitFeelPieBand& OutBand)
	{
		switch (Scenario)
		{
		case ETDHitFeelPieScenario::DealtLight:
			OutBand = { 2.f, 4.f };
			return true;
		case ETDHitFeelPieScenario::DealtHeavy:
			OutBand = { 6.f, 9.f };
			return true;
		case ETDHitFeelPieScenario::DealtKill:
			OutBand = { 8.f, 11.f };
			return true;
		case ETDHitFeelPieScenario::ReceivedSmall:
			OutBand = { 7.f, 9.f };
			return true;
		case ETDHitFeelPieScenario::ReceivedMedium:
			OutBand = { 10.f, 13.f };
			return true;
		case ETDHitFeelPieScenario::ReceivedLarge:
			OutBand = { 15.f, 19.f };
			return true;
		default:
			return false;
		}
	}

	float GetHitFeelPieTargetRatio(ETDHitFeelPieScenario Scenario)
	{
		switch (Scenario)
		{
		case ETDHitFeelPieScenario::DealtLight:
		case ETDHitFeelPieScenario::DealtMultiTarget:
		case ETDHitFeelPieScenario::DealtCritical:
			return TDHitFeelPieDealtLightRatio;
		case ETDHitFeelPieScenario::DealtSpellKnockback:
			return TDHitFeelPieSpellKnockbackRatio;
		case ETDHitFeelPieScenario::BossCommittedHit:
			return TDHitFeelPieBossCommittedHitRatio;
		case ETDHitFeelPieScenario::DealtHeavy:
			return TDHitFeelPieDealtHeavyRatio;
		case ETDHitFeelPieScenario::ReceivedSmall:
			return TDHitFeelPieReceivedSmallRatio;
		case ETDHitFeelPieScenario::ReceivedMedium:
		case ETDHitFeelPieScenario::ReceivedZeroShake:
		case ETDHitFeelPieScenario::ScreenshotBurst:
			return TDHitFeelPieReceivedMediumRatio;
		case ETDHitFeelPieScenario::ReceivedLarge:
			return TDHitFeelPieReceivedLargeRatio;
		case ETDHitFeelPieScenario::ReceivedTiny:
			return TDHitFeelPieReceivedTinyRatio;
		case ETDHitFeelPieScenario::ReceivedStatus:
			return TDHitFeelPieStatusRatio;
		case ETDHitFeelPieScenario::Surrounded:
			return TDHitFeelPieSurroundRatio;
		default:
			return 0.f;
		}
	}

	bool IsHitFeelPieReceivedScenario(ETDHitFeelPieScenario Scenario)
	{
		switch (Scenario)
		{
		case ETDHitFeelPieScenario::ReceivedSmall:
		case ETDHitFeelPieScenario::ReceivedMedium:
		case ETDHitFeelPieScenario::ReceivedLarge:
		case ETDHitFeelPieScenario::ReceivedTiny:
		case ETDHitFeelPieScenario::ReceivedStatus:
		case ETDHitFeelPieScenario::ReceivedZeroShake:
		case ETDHitFeelPieScenario::Surrounded:
		case ETDHitFeelPieScenario::ScreenshotBurst:
			return true;
		default:
			return false;
		}
	}

	bool IsHitFeelPieQuietScenario(ETDHitFeelPieScenario Scenario)
	{
		return Scenario == ETDHitFeelPieScenario::ReceivedStatus || Scenario == ETDHitFeelPieScenario::ReceivedTiny;
	}

	int32 GetHitFeelPieRequiredRingCount(ETDHitFeelPieScenario Scenario)
	{
		switch (Scenario)
		{
		case ETDHitFeelPieScenario::DealtKill:
		case ETDHitFeelPieScenario::BossKill:
		case ETDHitFeelPieScenario::BossApproach:
		case ETDHitFeelPieScenario::BossPhase:
		case ETDHitFeelPieScenario::BossCommittedHit:
			return 0;
		case ETDHitFeelPieScenario::DealtMultiTarget:
			return TDHitFeelPieMultiTargetCount;
		case ETDHitFeelPieScenario::Surrounded:
			return TDHitFeelPieRingMonsterCount;
		default:
			return 1;
		}
	}

	int32 GetHitFeelPieSettleFrames(ETDHitFeelPieScenario Scenario)
	{
		return Scenario == ETDHitFeelPieScenario::DealtKill || Scenario == ETDHitFeelPieScenario::BossKill ? TDHitFeelPieSpawnSettleFrames : TDHitFeelPieSettleFrames;
	}

	int32 GetHitFeelPieObserveFrames(ETDHitFeelPieScenario Scenario)
	{
		switch (Scenario)
		{
		case ETDHitFeelPieScenario::Surrounded:
			return TDHitFeelPieSurroundObserveFrames;
		case ETDHitFeelPieScenario::BossKill:
			return TDHitFeelPieBossObserveFrames;
		case ETDHitFeelPieScenario::ScreenshotBurst:
			return TDHitFeelPieScreenshotBurstFrames;
		case ETDHitFeelPieScenario::ApproachBaseline:
		case ETDHitFeelPieScenario::ApproachStep:
		case ETDHitFeelPieScenario::BossApproach:
			return TDHitFeelPieApproachObserveFrames;
		case ETDHitFeelPieScenario::BossPhase:
			return TDHitFeelPiePhaseObserveFrames;
		default:
			return TDHitFeelPieObserveFrames;
		}
	}

	bool IsHitFeelPieApproachScenario(ETDHitFeelPieScenario Scenario)
	{
		return Scenario == ETDHitFeelPieScenario::ApproachBaseline || Scenario == ETDHitFeelPieScenario::ApproachStep || Scenario == ETDHitFeelPieScenario::BossApproach;
	}

	bool IsHitFeelPieBossScenario(ETDHitFeelPieScenario Scenario)
	{
		return Scenario == ETDHitFeelPieScenario::BossKill || Scenario == ETDHitFeelPieScenario::BossApproach || Scenario == ETDHitFeelPieScenario::BossPhase || Scenario == ETDHitFeelPieScenario::BossCommittedHit;
	}

	float GetHitFeelPieExpectedVictimStopSeconds(ETDHitFeelPieScenario Scenario)
	{
		return Scenario == ETDHitFeelPieScenario::DealtCritical ? TDHitFeelPieVictimCriticalStopSeconds : TDHitFeelPieVictimLightStopSeconds;
	}

	int32 ComputeHitFeelPieFrames(float Seconds)
	{
		return FMath::RoundToInt32(Seconds / TDHitFeelPieFixedDeltaSeconds);
	}

	FString MakeHitFeelPieOutputDirectory()
	{
		const FString DateStamp = FDateTime::Now().ToString(TDHitFeelPieDateFormat);
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TDHitFeelPieOutputRootFolder / DateStamp / TDHitFeelPieOutputLeafFolder);
	}

	void RequestHitFeelPieScreenshot(FTDHitFeelPieState& State, const FString& FileName)
	{
		if (State.RequestedScreenshots.Contains(FileName))
		{
			return;
		}

		const FString ScreenshotPath = State.OutputDirectory / FileName;
		IFileManager::Get().Delete(*ScreenshotPath, false, true, true);
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false, false, FIntRect(), true);
		State.RequestedScreenshots.Add(FileName);
		const UWorld* World = TDPieTestUtils::GetPieWorld();
		UGameViewportClient* GameViewport = World ? World->GetGameViewport() : nullptr;
		if (GameViewport && GameViewport->Viewport)
		{
			GameViewport->ProcessScreenShots(GameViewport->Viewport);
		}
	}

	bool AreHitFeelPieScreenshotsSaved(const FTDHitFeelPieState& State)
	{
		for (const FString& FileName : State.RequestedScreenshots)
		{
			if (!IFileManager::Get().FileExists(*(State.OutputDirectory / FileName)))
			{
				return false;
			}
		}
		return true;
	}

	void RecordHitFeelPieScreenshots(FTDHitFeelPieState& State)
	{
		State.ScreenshotRows.Reset();
		for (const FString& FileName : State.RequestedScreenshots)
		{
			const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("file"), FileName);
			Row->SetBoolField(TEXT("saved"), IFileManager::Get().FileExists(*(State.OutputDirectory / FileName)));
			State.ScreenshotRows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	FVector2D ProjectHitFeelPieReferenceScreen(const FVector& WorldLocation, const FVector& ViewLocation, const FRotator& ViewRotation, float FovDegrees)
	{
		const FVector ViewSpace = ViewRotation.UnrotateVector(WorldLocation - ViewLocation);
		if (ViewSpace.X <= KINDA_SMALL_NUMBER)
		{
			return FVector2D::ZeroVector;
		}

		const double HalfFovRadians = FMath::DegreesToRadians(FMath::Clamp(FovDegrees, 1.f, 170.f) * 0.5);
		const double FocalLengthPx = TDHitFeelPieReferenceWidthPx * 0.5 / FMath::Tan(HalfFovRadians);
		return FVector2D(FocalLengthPx * ViewSpace.Y / ViewSpace.X, -FocalLengthPx * ViewSpace.Z / ViewSpace.X);
	}

	void MeasureHitFeelPieShake(FTDHitFeelPieFrameRow& Row, const APlayerCameraManager& CameraManager, const UCameraComponent& Camera, const FVector& FocusLocation)
	{
		const FVector ShakenLocation = CameraManager.GetCameraLocation();
		const FRotator ShakenRotation = CameraManager.GetCameraRotation();
		const float ShakenFovDegrees = CameraManager.GetFOVAngle();
		const FVector BaseLocation = Camera.GetComponentLocation();
		const FRotator BaseRotation = Camera.GetComponentRotation();
		const FVector2D ShakenScreen = ProjectHitFeelPieReferenceScreen(FocusLocation, ShakenLocation, ShakenRotation, ShakenFovDegrees);
		const FVector2D BaseScreen = ProjectHitFeelPieReferenceScreen(FocusLocation, BaseLocation, BaseRotation, Camera.FieldOfView);
		Row.ShakePx = FVector2D::Distance(ShakenScreen, BaseScreen);
		Row.ShakeRotationDegrees = (ShakenRotation - BaseRotation).GetNormalized().Euler().GetAbsMax();
		Row.ShakeLocationCm = FVector::Dist(ShakenLocation, BaseLocation);
		Row.FovDegrees = ShakenFovDegrees;
		Row.CameraDistanceCm = FVector::Dist(BaseLocation, FocusLocation);
	}

	float MeasureHitFeelPieCurrentShakePx(const FTDHitFeelPieState& State)
	{
		const ATDGamePlayerController* Controller = State.Controller.Get();
		const APlayerCameraManager* CameraManager = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
		const ATDGameCharacter* Player = State.Player.Get();
		const UCameraComponent* Camera = Player ? Player->GetTopDownCameraComponent() : nullptr;
		if (!CameraManager || !Camera)
		{
			return 0.f;
		}

		FTDHitFeelPieFrameRow Row;
		MeasureHitFeelPieShake(Row, *CameraManager, *Camera, Player->GetActorLocation());
		return Row.ShakePx;
	}

	float ReadHitFeelPieScalar(UMaterialInterface* Material, FName ParameterName)
	{
		UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(Material);
		if (!MaterialInstance || ParameterName.IsNone())
		{
			return -1.f;
		}
		return MaterialInstance->K2_GetScalarParameterValue(ParameterName);
	}

	float ReadHitFeelPieVignette(const FTDHitFeelPieState& State, const UTDCombatFeedbackSubsystem& Feedback)
	{
		const UTDCombatOverlayWidget* Overlay = Feedback.GetOverlayWidget();
		if (!State.VignetteProperty || !Overlay)
		{
			return -1.f;
		}
		UObject* VignetteMaterial = State.VignetteProperty->GetObjectPropertyValue_InContainer(Overlay);
		return ReadHitFeelPieScalar(Cast<UMaterialInterface>(VignetteMaterial), State.VignetteIntensityParameterName);
	}

	UActorComponent* FindHitFeelPieUafComponent(const AActor& Actor)
	{
		for (UActorComponent* Component : Actor.GetComponents())
		{
			for (const UClass* ComponentClass = Component ? Component->GetClass() : nullptr; ComponentClass; ComponentClass = ComponentClass->GetSuperClass())
			{
				if (ComponentClass->GetFName() == TDHitFeelPieUafComponentClassName)
				{
					return Component;
				}
			}
		}
		return nullptr;
	}

	bool IsHitFeelPieFixedStep(double RealDeltaSeconds, double WorldDeltaSeconds, float TimeDilation, float PreviousTimeDilation)
	{
		if (!FMath::IsNearlyEqual(RealDeltaSeconds, TDHitFeelPieFixedDeltaSeconds, TDHitFeelPieDeltaToleranceSeconds))
		{
			return false;
		}
		return FMath::IsNearlyEqual(WorldDeltaSeconds, TDHitFeelPieFixedDeltaSeconds * TimeDilation, TDHitFeelPieDeltaToleranceSeconds)
			|| FMath::IsNearlyEqual(WorldDeltaSeconds, TDHitFeelPieFixedDeltaSeconds * PreviousTimeDilation, TDHitFeelPieDeltaToleranceSeconds);
	}

	void SampleHitFeelPieVictim(const FTDHitFeelPieState& State, FTDHitFeelPieFrameRow& Row)
	{
		const ATDCombatCharacter* Victim = State.FocusVictim.Get();
		if (!Victim)
		{
			return;
		}

		const USkeletalMeshComponent* VictimMesh = Victim->GetMesh();
		Row.FlashIntensity = VictimMesh ? ReadHitFeelPieScalar(VictimMesh->GetOverlayMaterial(), State.FlashIntensityParameterName) : -1.f;
		const UActorComponent* VictimUafComponent = State.FocusVictimUafComponent.Get();
		Row.bIsVictimUafActive = !VictimUafComponent || VictimUafComponent->IsActive();
		Row.bIsVictimRagdolling = Victim->IsRagdolling();
		Row.VictimLocation = Victim->GetActorLocation();
		const UTDCharacterAnimationComponent* VictimAnimation = Victim->GetCharacterAnimation();
		if (!VictimAnimation)
		{
			return;
		}
		Row.VictimStopRemainingSeconds = VictimAnimation->GetVictimHitStopRemainingSeconds();
		Row.bIsVictimStopPending = VictimAnimation->IsHitStopPending();
		Row.bIsVictimRootMotion = VictimAnimation->IsPlayingRootMotionAction();
		Row.VictimActionSeconds = VictimAnimation->GetActionAnimationTime();
		Row.bIsVictimPlayingDeath = VictimAnimation->IsPlayingDeath();
	}

	void SetHitFeelPieFocusVictim(FTDHitFeelPieState& State, ATDCombatCharacter* Victim)
	{
		State.FocusVictim = Victim;
		State.FocusVictimUafComponent = Victim ? FindHitFeelPieUafComponent(*Victim) : nullptr;
	}

	UTDTraumaCameraShakePattern* ResolveHitFeelPieShakePattern(FTDHitFeelPieState& State)
	{
		if (UTDTraumaCameraShakePattern* CachedPattern = State.ShakePattern.Get())
		{
			return CachedPattern;
		}

		const ATDGamePlayerController* Controller = State.Controller.Get();
		APlayerCameraManager* CameraManager = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
		const UCameraModifier_CameraShake* ShakeModifier = CameraManager ? Cast<UCameraModifier_CameraShake>(CameraManager->FindCameraModifierByClass(UCameraModifier_CameraShake::StaticClass())) : nullptr;
		if (!ShakeModifier)
		{
			return nullptr;
		}

		TArray<FActiveCameraShakeInfo> ActiveShakes;
		ShakeModifier->GetActiveCameraShakes(ActiveShakes);
		for (const FActiveCameraShakeInfo& ActiveShake : ActiveShakes)
		{
			UCameraShakeBase* ShakeInstance = ActiveShake.ShakeInstance.Get();
			UTDTraumaCameraShakePattern* Pattern = ShakeInstance ? Cast<UTDTraumaCameraShakePattern>(ShakeInstance->GetRootShakePattern()) : nullptr;
			if (Pattern)
			{
				State.ShakePattern = Pattern;
				return Pattern;
			}
		}
		return nullptr;
	}

	void SampleHitFeelPiePlayer(const FTDHitFeelPieState& State, FTDHitFeelPieFrameRow& Row)
	{
		const ATDGameCharacter* Player = State.Player.Get();
		if (!Player)
		{
			return;
		}

		Row.PlayerLocation = Player->GetActorLocation();
		const UActorComponent* UafComponent = State.PlayerUafComponent.Get();
		Row.bIsUafActive = UafComponent && UafComponent->IsActive();
		const UTDSkillComponent* Skill = Player->GetSkillComponent();
		Row.ComboStageIndex = Skill ? Skill->GetActivePrimaryComboStageIndex() : INDEX_NONE;
		Row.PrimaryComboStep = Skill ? Skill->GetPrimaryComboStep() : 0;
		const UTDCharacterAnimationComponent* Animation = Player->GetCharacterAnimation();
		if (!Animation)
		{
			return;
		}
		Row.bIsAttackerStopActive = Animation->IsAttackerHitStopActive();
		Row.PlayerAttackerStopRemainingSeconds = Animation->GetAttackerHitStopRemainingSeconds();
		Row.PlayerStopRemainingSeconds = Animation->GetHitStopRemainingSeconds();
		Row.bIsPlayerPlayingAction = Animation->IsPlayingAction();
		Row.PlayerActionSeconds = Animation->GetActionAnimationTime();
	}

	void SampleHitFeelPieFeedback(FTDHitFeelPieState& State, FTDHitFeelPieFrameRow& Row)
	{
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!Feedback)
		{
			return;
		}

		const UTDTraumaCameraShakePattern* CachedPattern = State.ShakePattern.Get();
		if (CachedPattern && !FMath::IsNearlyEqual(CachedPattern->GetTrauma(), Feedback->GetCameraTraumaForTest(), KINDA_SMALL_NUMBER))
		{
			State.ShakePattern.Reset();
		}
		if (const UTDTraumaCameraShakePattern* ShakePattern = ResolveHitFeelPieShakePattern(State))
		{
			for (int32 ChannelIndex = 0; ChannelIndex < TDHitCameraShakeMath::ShakeChannelCount; ++ChannelIndex)
			{
				Row.ChannelTrauma[ChannelIndex] = ShakePattern->GetChannelTrauma(static_cast<ETDShakeChannel>(ChannelIndex));
			}
		}
		Row.Trauma = Feedback->GetCameraTraumaForTest();
		Row.ReportedShakeOffset = Feedback->GetLastShakeScreenOffsetForTest();
		Row.ShakesStarted = Feedback->GetStats().ShakesStarted;
		Row.FlashCount = Feedback->GetFlashCountForTest();
		Row.KickCount = Feedback->GetKickCountForTest();
		Row.BossPhasePulses = Feedback->GetStats().BossPhasePulses;
		Row.VignetteIntensity = ReadHitFeelPieVignette(State, *Feedback);
	}

	void SampleHitFeelPieFrame(FTDHitFeelPieState& State, UWorld& World, float DeltaSeconds)
	{
		++State.FrameIndex;
		State.RealSecondsElapsed += FApp::GetDeltaTime();
		const AWorldSettings* WorldSettings = World.GetWorldSettings();
		const float TimeDilation = WorldSettings ? WorldSettings->GetEffectiveTimeDilation() : 1.f;
		const float PreviousTimeDilation = State.PreviousTimeDilation;
		State.PreviousTimeDilation = TimeDilation;
		if (State.ActiveRunIndex == INDEX_NONE)
		{
			return;
		}

		FTDHitFeelPieFrameRow Row;
		Row.Frame = State.FrameIndex;
		Row.RunIndex = State.ActiveRunIndex;
		Row.WorldSeconds = World.GetTimeSeconds();
		Row.RealSeconds = State.RealSecondsElapsed;
		Row.WorldDeltaSeconds = DeltaSeconds;
		Row.RealDeltaSeconds = FApp::GetDeltaTime();
		Row.TimeDilation = TimeDilation;
		Row.bIsFixedStep = IsHitFeelPieFixedStep(Row.RealDeltaSeconds, Row.WorldDeltaSeconds, TimeDilation, PreviousTimeDilation);

		const APlayerController* PlayerController = World.GetFirstPlayerController();
		const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
		const ATDGameCharacter* Player = State.Player.Get();
		const UCameraComponent* Camera = Player ? Player->GetTopDownCameraComponent() : nullptr;
		const USpringArmComponent* CameraBoom = Player ? Player->GetCameraBoom() : nullptr;
		if (CameraManager && Camera)
		{
			MeasureHitFeelPieShake(Row, *CameraManager, *Camera, Player->GetActorLocation());
		}
		Row.ArmLengthCm = CameraBoom ? CameraBoom->TargetArmLength : 0.f;
		SampleHitFeelPieFeedback(State, Row);
		SampleHitFeelPieVictim(State, Row);
		SampleHitFeelPiePlayer(State, Row);
		State.Rows.Add(Row);
	}

	float ComputeHitFeelPieRawDamage(const UTDCombatComponent& Combat, float DamageRatio)
	{
		const float Armor = FMath::Max(0.f, static_cast<float>(Combat.GetNumericAttribute(UTDCombatAttributeSet::GetArmorAttribute())));
		return Combat.GetMaxHealth() * DamageRatio * (100.f + Armor) / 100.f;
	}

	void RefillHitFeelPieHealth(UTDCombatComponent& Combat)
	{
		if (Combat.GetCurrentHealth() >= Combat.GetMaxHealth())
		{
			return;
		}
		Combat.SetStats(Combat.GetStats(), true);
	}

	ETDHitFeelTier ResolveHitFeelPieDealtTier(const FTDDamageResult& Result)
	{
		return Result.bWasCritical || Result.DamageRatio >= TDHitFeelPieHeavyTraumaRatio ? ETDHitFeelTier::Heavy : ETDHitFeelTier::Light;
	}

	FTDDamageResult InjectHitFeelPieDealtDamage(ATDGameCharacter& Player, ATDMonsterCharacter& Victim, float DamageRatio, bool bIsLethal, ETDDamageDelivery Delivery = ETDDamageDelivery::Melee, bool bIsCritical = false)
	{
		UTDCombatComponent* VictimCombat = Victim.GetCombatComponent();
		if (!VictimCombat || !VictimCombat->IsAlive())
		{
			return FTDDamageResult();
		}

		if (!bIsLethal && !Victim.IsBossMonster())
		{
			RefillHitFeelPieHealth(*VictimCombat);
		}
		FTDDamageContext Context = UTDCombatLibrary::MakeDamageContext(&Player, Victim.GetActorLocation());
		Context.Delivery = Delivery;
		if (bIsCritical)
		{
			Context.Stats.CriticalChance = 1.f;
		}
		const float RawDamage = bIsLethal ? TDHitFeelPieLethalDamage : ComputeHitFeelPieRawDamage(*VictimCombat, DamageRatio);
		return VictimCombat->ReceiveDamage(RawDamage, ETDDamageElement::Physical, bIsCritical, Context);
	}

	FTDDamageResult InjectHitFeelPieReceivedDamage(FTDHitFeelPieState& State, AActor* Caster, float DamageRatio, ETDDamageDelivery Delivery)
	{
		ATDGameCharacter* Player = State.Player.Get();
		UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		if (!PlayerCombat)
		{
			return FTDDamageResult();
		}

		RefillHitFeelPieHealth(*PlayerCombat);
		FTDDamageContext Context = UTDCombatLibrary::MakeDamageContext(Caster, Player->GetActorLocation());
		Context.Delivery = Delivery;
		const float RawDamage = ComputeHitFeelPieRawDamage(*PlayerCombat, DamageRatio);
		State.bIsInjectingDamage = true;
		if (State.bIsPlayerInvulnerableForTest)
		{
			PlayerCombat->RemoveLooseGameplayTag(TDGameplayTags::State_Invulnerable);
		}
		const FTDDamageResult Result = PlayerCombat->ReceiveDamage(RawDamage, ETDDamageElement::Physical, false, Context);
		if (State.bIsPlayerInvulnerableForTest)
		{
			PlayerCombat->AddLooseGameplayTag(TDGameplayTags::State_Invulnerable);
		}
		State.bIsInjectingDamage = false;
		return Result;
	}

	ATDMonsterCharacter* SpawnHitFeelPieMonster(const FTDHitFeelPieState& State, const TCHAR* SpeciesPath, float DistanceCm, float YawOffsetDegrees)
	{
		ATDGamePlayerController* Controller = State.Controller.Get();
		UTDMonsterSpeciesAsset* Species = Cast<UTDMonsterSpeciesAsset>(FSoftObjectPath(SpeciesPath).ResolveObject());
		if (!Controller || !Species)
		{
			return nullptr;
		}

		const FVector Direction = State.PlayerHomeForward.RotateAngleAxis(YawOffsetDegrees, FVector::UpVector);
		const FVector SpawnLocation = State.PlayerHomeLocation + Direction * DistanceCm;
		const FRotator FacingPlayer(0.f, (-Direction).Rotation().Yaw, 0.f);
		return Controller->SpawnMonsterForTest(Species, SpawnLocation, FacingPlayer);
	}

	FVector PlaceHitFeelPieTargetInFront(const ATDGameCharacter& Player, ATDMonsterCharacter& Target, const FVector& Forward, float SurfaceGapCm = TDHitFeelPieComboSurfaceGapCm)
	{
		const UCapsuleComponent* PlayerCapsule = Player.GetCapsuleComponent();
		const UCapsuleComponent* TargetCapsule = Target.GetCapsuleComponent();
		const float GapCm = (PlayerCapsule ? PlayerCapsule->GetScaledCapsuleRadius() : 0.f) + (TargetCapsule ? TargetCapsule->GetScaledCapsuleRadius() : 0.f) + SurfaceGapCm;
		const FVector PlayerLocation = Player.GetActorLocation();
		const FVector PinnedLocation(PlayerLocation.X + Forward.X * GapCm, PlayerLocation.Y + Forward.Y * GapCm, Target.GetActorLocation().Z);
		Target.SetActorLocationAndRotation(PinnedLocation, FRotator(0.f, (-Forward).Rotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		return PinnedLocation;
	}

	FVector GetHitFeelPiePlayerForward(const FTDHitFeelPieState& State, const ATDGameCharacter& Player)
	{
		const FVector Forward = Player.GetActorForwardVector().GetSafeNormal2D();
		return Forward.IsNearlyZero() ? State.PlayerHomeForward : Forward;
	}

	void DestroyHitFeelPieMonster(TWeakObjectPtr<ATDMonsterCharacter>& Monster)
	{
		if (ATDMonsterCharacter* MonsterActor = Monster.Get())
		{
			MonsterActor->Destroy();
		}
		Monster.Reset();
	}

	bool HasHitFeelPieRingMonsters(const FTDHitFeelPieState& State, int32 RequiredCount)
	{
		if (State.RingMonsters.Num() < RequiredCount)
		{
			return false;
		}
		for (int32 MonsterIndex = 0; MonsterIndex < RequiredCount; ++MonsterIndex)
		{
			if (!State.RingMonsters[MonsterIndex].IsValid())
			{
				return false;
			}
		}
		return true;
	}

	void CaptureHitFeelPieCounters(const UTDCombatFeedbackSubsystem& Feedback, int32& OutShakes, int32& OutFlashes, int32& OutKicks, int32& OutPhasePulses)
	{
		OutShakes = Feedback.GetStats().ShakesStarted;
		OutFlashes = Feedback.GetFlashCountForTest();
		OutKicks = Feedback.GetKickCountForTest();
		OutPhasePulses = Feedback.GetStats().BossPhasePulses;
	}

	void BindHitFeelPiePlayerObserver(const TSharedRef<FTDHitFeelPieState>& State, UTDCombatComponent& PlayerCombat)
	{
		const TWeakPtr<FTDHitFeelPieState> WeakState = State;
		State->PlayerObservedCombat = &PlayerCombat;
		State->PlayerDamageHandle = PlayerCombat.OnDamaged.AddLambda([WeakState](const FTDDamageResult& Result, const FTDDamageContext& Context)
		{
			const TSharedPtr<FTDHitFeelPieState> PinnedState = WeakState.Pin();
			if (!PinnedState || PinnedState->bIsInjectingDamage || Result.AppliedDamage <= 0.f)
			{
				return;
			}
			PinnedState->ContaminationFrames.Add(PinnedState->FrameIndex + 1);
		});
	}

	void UnbindHitFeelPiePlayerObserver(FTDHitFeelPieState& State)
	{
		if (UTDCombatComponent* PlayerCombat = State.PlayerObservedCombat.Get())
		{
			PlayerCombat->OnDamaged.Remove(State.PlayerDamageHandle);
		}
		State.PlayerObservedCombat.Reset();
		State.PlayerDamageHandle.Reset();
	}

	void BindHitFeelPieComboHits(const TSharedRef<FTDHitFeelPieState>& State)
	{
		ATDMonsterCharacter* Target = State->FocusMonster.Get();
		UTDCombatComponent* TargetCombat = Target ? Target->GetCombatComponent() : nullptr;
		if (!TargetCombat)
		{
			return;
		}

		const TWeakPtr<FTDHitFeelPieState> WeakState = State;
		const TWeakObjectPtr<ATDMonsterCharacter> WeakTarget = Target;
		State->ComboHits.Reset();
		State->ComboObservedCombat = TargetCombat;
		State->ComboDamageHandle = TargetCombat->OnDamaged.AddLambda([WeakState, WeakTarget](const FTDDamageResult& Result, const FTDDamageContext& Context)
		{
			const TSharedPtr<FTDHitFeelPieState> PinnedState = WeakState.Pin();
			if (!PinnedState)
			{
				return;
			}
			const ATDMonsterCharacter* TargetActor = WeakTarget.Get();
			FTDHitFeelPieComboHit& Hit = PinnedState->ComboHits.AddDefaulted_GetRef();
			Hit.Frame = PinnedState->FrameIndex + 1;
			Hit.StageCount = PinnedState->ComboLiveStageCount;
			Hit.bWasCritical = Result.bWasCritical;
			Hit.bWasFromPlayer = Context.Caster.Get() == PinnedState->Player.Get();
			Hit.bWasVictimCommitted = TargetActor && TargetActor->IsInCommittedAttack();
			Hit.bWasVictimDangerWindup = TargetActor && TargetActor->IsInDangerWindup();
			Hit.DamageRatio = Result.DamageRatio;
		});
	}

	void UnbindHitFeelPieComboHits(FTDHitFeelPieState& State)
	{
		if (UTDCombatComponent* TargetCombat = State.ComboObservedCombat.Get())
		{
			TargetCombat->OnDamaged.Remove(State.ComboDamageHandle);
		}
		State.ComboObservedCombat.Reset();
		State.ComboDamageHandle.Reset();
	}

	void RemoveHitFeelPiePlayerInvulnerability(FTDHitFeelPieState& State)
	{
		ATDGameCharacter* Player = State.Player.Get();
		UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		if (!State.bIsPlayerInvulnerableForTest || !PlayerCombat)
		{
			return;
		}
		PlayerCombat->RemoveLooseGameplayTag(TDGameplayTags::State_Invulnerable);
		State.bIsPlayerInvulnerableForTest = false;
	}

	void SetHitFeelPieArmLength(FTDHitFeelPieState& State, float ArmLengthCm)
	{
		ATDGameCharacter* Player = State.Player.Get();
		USpringArmComponent* CameraBoom = Player ? Player->GetCameraBoom() : nullptr;
		if (State.bIsAborted || !CameraBoom)
		{
			return;
		}
		CameraBoom->TargetArmLength = ArmLengthCm;
	}

	void RestoreHitFeelPieArmLength(FTDHitFeelPieState& State)
	{
		ATDGameCharacter* Player = State.Player.Get();
		USpringArmComponent* CameraBoom = Player ? Player->GetCameraBoom() : nullptr;
		if (!CameraBoom || State.OriginalArmLengthCm <= 0.f)
		{
			return;
		}
		CameraBoom->TargetArmLength = State.OriginalArmLengthCm;
	}

	TSharedRef<FJsonObject> MakeHitFeelPieComboStageJson(const FTDHitFeelPieComboStage& Stage)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("stage_index_value"), Stage.StageIndexValue);
		Json->SetNumberField(TEXT("first_frame"), Stage.FirstFrame);
		Json->SetNumberField(TEXT("last_frame"), Stage.LastFrame);
		Json->SetNumberField(TEXT("hits"), Stage.HitCount);
		Json->SetBoolField(TEXT("critical_hit"), Stage.bHadCriticalHit);
		Json->SetBoolField(TEXT("victim_committed_attack"), Stage.bWasVictimCommitted);
		Json->SetNumberField(TEXT("slowed_action_clock_frames"), Stage.SlowedFrames);
		Json->SetNumberField(TEXT("slowed_frames_with_uaf_inactive"), Stage.SlowedFramesWithUafInactive);
		Json->SetNumberField(TEXT("attacker_stop_active_frames"), Stage.AttackerActiveFrames);
		Json->SetNumberField(TEXT("victim_stop_frames"), Stage.VictimStopFrames);
		Json->SetNumberField(TEXT("victim_stop_start_delay_frames"), Stage.VictimStopStartDelayFrames);
		Json->SetBoolField(TEXT("victim_danger_windup"), Stage.bWasVictimDangerWindup);
		Json->SetNumberField(TEXT("first_hit_frame"), Stage.FirstHitFrame);
		Json->SetNumberField(TEXT("knockback_speed"), Stage.KnockbackSpeed);
		Json->SetNumberField(TEXT("knockback_peak_speed"), Stage.KnockbackPeakSpeed);
		Json->SetNumberField(TEXT("knockback_distance_cm"), Stage.KnockbackDistanceCm);
		Json->SetNumberField(TEXT("root_motion_cm"), Stage.RootMotionCm);
		return Json;
	}

	TSharedRef<FJsonObject> MakeHitFeelPieRunJson(const FTDHitFeelPieRun& Run)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("run"), Run.RunIndex);
		Json->SetStringField(TEXT("scenario"), GetHitFeelPieScenarioName(Run.Scenario));
		Json->SetStringField(TEXT("phase"), Run.Phase);
		Json->SetNumberField(TEXT("repetition"), Run.Repetition);
		Json->SetNumberField(TEXT("noise_gain"), Run.NoiseGain);
		Json->SetNumberField(TEXT("arm_length_cm"), Run.ArmLengthCm);
		Json->SetNumberField(TEXT("target_ratio"), Run.TargetRatio);
		Json->SetNumberField(TEXT("applied_ratio"), Run.AppliedRatio);
		Json->SetBoolField(TEXT("injection_applied"), Run.bDidInjectionApply);
		Json->SetBoolField(TEXT("finished"), Run.bIsFinished);
		Json->SetBoolField(TEXT("valid"), Run.bIsValid);
		Json->SetNumberField(TEXT("invalid_rows"), Run.InvalidRowCount);
		Json->SetBoolField(TEXT("contaminated"), Run.bIsContaminated);
		Json->SetBoolField(TEXT("quiet_at_inject"), Run.bWasQuietAtInject);
		Json->SetBoolField(TEXT("victim_committed_attack"), Run.bIsVictimCommitted);
		Json->SetBoolField(TEXT("boss_victim"), Run.bIsBossVictim);
		Json->SetBoolField(TEXT("player_swing_started"), Run.bDidStartPlayerSwing);
		Json->SetNumberField(TEXT("inject_frame"), Run.InjectFrame);
		Json->SetNumberField(TEXT("kill_frame"), Run.KillFrame);
		Json->SetNumberField(TEXT("end_frame"), Run.EndFrame);
		Json->SetNumberField(TEXT("rows"), Run.RowCount);
		Json->SetNumberField(TEXT("shakes_started"), Run.ShakesStartedDelta);
		Json->SetNumberField(TEXT("flashes"), Run.FlashDelta);
		Json->SetNumberField(TEXT("kicks"), Run.KickDelta);
		Json->SetNumberField(TEXT("boss_phase_pulses"), Run.PhasePulseDelta);
		Json->SetNumberField(TEXT("expected_trauma"), Run.ExpectedTrauma);
		Json->SetNumberField(TEXT("inject_trauma"), Run.InjectTrauma);
		Json->SetBoolField(TEXT("shake_started_at_inject"), Run.bDidShakeStartAtInject);
		Json->SetNumberField(TEXT("first_frame_trauma"), Run.FirstFrameTrauma);
		Json->SetNumberField(TEXT("start_trauma"), Run.StartTrauma);
		Json->SetStringField(TEXT("start_trauma_source"), Run.StartTraumaSource);
		Json->SetNumberField(TEXT("peak_trauma"), Run.MaxTrauma);
		Json->SetNumberField(TEXT("baseline_shake_px"), Run.BaselineShakePx);
		Json->SetNumberField(TEXT("peak_shake_px"), Run.MaxShakePx);
		Json->SetNumberField(TEXT("victim_stop_frames"), Run.VictimStopFrames);
		Json->SetNumberField(TEXT("victim_pending_frames"), Run.VictimPendingFrames);
		Json->SetNumberField(TEXT("attacker_stop_frames"), Run.AttackerStopSpan.FrameCount);
		Json->SetNumberField(TEXT("attacker_stop_real_seconds"), Run.AttackerStopSpan.GetSeconds());
		Json->SetNumberField(TEXT("shake_frames"), Run.ShakeSpan.FrameCount);
		Json->SetNumberField(TEXT("shake_real_seconds"), Run.ShakeSpan.GetSeconds());
		Json->SetNumberField(TEXT("slow_motion_frames"), Run.SlowMotionSpan.FrameCount);
		Json->SetNumberField(TEXT("slow_motion_real_seconds"), Run.SlowMotionSpan.GetSeconds());
		Json->SetNumberField(TEXT("time_dilation_at_check"), Run.DilationAtCheck);
		Json->SetNumberField(TEXT("noise_phase"), Run.NoisePhase);
		Json->SetBoolField(TEXT("noise_phase_applied"), Run.bDidSetNoisePhase);
		Json->SetBoolField(TEXT("victim_danger_windup"), Run.bIsVictimDangerWindup);
		Json->SetBoolField(TEXT("victim_elite_or_boss"), Run.bIsVictimEliteOrBoss);
		Json->SetNumberField(TEXT("peak_shake_location_cm"), Run.MaxShakeLocationCm);
		Json->SetNumberField(TEXT("trauma_decay_violations"), Run.TraumaDecayViolations);
		Json->SetNumberField(TEXT("worst_trauma_decay_excess"), Run.WorstTraumaDecayExcess);
		Json->SetNumberField(TEXT("worst_trauma_decay_frame"), Run.WorstTraumaDecayFrame);
		Json->SetNumberField(TEXT("victim_stop_start_delay_frames"), Run.VictimStopStartDelayFrames);
		Json->SetNumberField(TEXT("kill_pose_hold_frames"), Run.KillPoseHoldFrames);
		Json->SetNumberField(TEXT("death_start_frame"), Run.DeathStartFrame);
		Json->SetNumberField(TEXT("attacker_stop_last_frame"), Run.AttackerStopLastFrame);
		Json->SetNumberField(TEXT("slow_motion_first_frame"), Run.SlowMotionFirstFrame);
		Json->SetNumberField(TEXT("slow_motion_min_dilation"), Run.SlowMotionMinDilation);
		Json->SetNumberField(TEXT("peak_event_trauma"), Run.PeakEventTrauma);
		Json->SetNumberField(TEXT("phase_mark"), Run.PhaseMark);
		Json->SetNumberField(TEXT("knockback_speed"), Run.KnockbackSpeed);
		Json->SetNumberField(TEXT("knockback_peak_speed"), Run.KnockbackPeakSpeed);
		Json->SetNumberField(TEXT("knockback_braking_factor"), Run.KnockbackBrakingFactor);
		Json->SetNumberField(TEXT("phase_entry_frame"), Run.PhaseEntryFrame);
		Json->SetNumberField(TEXT("event_start_trauma"), Run.EventStartTrauma);
		Json->SetNumberField(TEXT("approach_displacement_cm"), Run.ApproachDisplacementCm);
		Json->SetNumberField(TEXT("committed_wait_frames"), Run.CommittedWaitFrames);
		if (Run.Scenario != ETDHitFeelPieScenario::Combo)
		{
			return Json;
		}

		TArray<TSharedPtr<FJsonValue>> StageValues;
		for (const int32 StageValue : Run.ComboStageValues)
		{
			StageValues.Add(MakeShared<FJsonValueNumber>(StageValue));
		}
		TArray<TSharedPtr<FJsonValue>> StageRows;
		for (const FTDHitFeelPieComboStage& Stage : Run.ComboStages)
		{
			StageRows.Add(MakeShared<FJsonValueObject>(MakeHitFeelPieComboStageJson(Stage)));
		}
		Json->SetNumberField(TEXT("attack_commands"), Run.ComboAttackCommands);
		Json->SetArrayField(TEXT("live_stage_values"), StageValues);
		Json->SetArrayField(TEXT("stages"), StageRows);
		return Json;
	}

	FString MakeHitFeelPieFramesCsv(const FTDHitFeelPieState& State)
	{
		FString Csv = TEXT("frame,run,scenario,phase,world_seconds,real_seconds,world_delta,real_delta,time_dilation,fixed_step,")
			TEXT("trauma,shake_px,shake_rotation_deg,shake_location_cm,reported_offset_x,reported_offset_y,fov_degrees,arm_length_cm,camera_distance_cm,")
			TEXT("shakes_started,flash_count,kick_count,boss_phase_pulses,flash_intensity,vignette_intensity,victim_stop_remaining,victim_stop_pending,attacker_stop_active,player_stop_remaining,")
			TEXT("player_action_playing,player_action_seconds,combo_stage_index,primary_combo_step,uaf_active,player_x,player_y,")
			TEXT("player_attacker_stop_remaining,dealt_trauma,received_trauma,event_trauma,victim_uaf_active,victim_root_motion,victim_action_seconds,victim_playing_death,victim_ragdolling,victim_x,victim_y\n");
		Csv.Reserve(Csv.Len() + State.Rows.Num() * 280);
		for (const FTDHitFeelPieFrameRow& Row : State.Rows)
		{
			const FTDHitFeelPieRun* Run = State.Runs.IsValidIndex(Row.RunIndex) ? &State.Runs[Row.RunIndex] : nullptr;
			const TCHAR* ScenarioName = Run ? GetHitFeelPieScenarioName(Run->Scenario) : TEXT("none");
			const TCHAR* PhaseName = Run ? *Run->Phase : TEXT("none");
			Csv += FString::Printf(TEXT("%d,%d,%s,%s,%.5f,%.5f,%.6f,%.6f,%.4f,%d,"), Row.Frame, Row.RunIndex, ScenarioName, PhaseName, Row.WorldSeconds, Row.RealSeconds, Row.WorldDeltaSeconds, Row.RealDeltaSeconds, Row.TimeDilation, Row.bIsFixedStep ? 1 : 0);
			Csv += FString::Printf(TEXT("%.4f,%.3f,%.4f,%.3f,%.4f,%.4f,%.2f,%.1f,%.1f,"), Row.Trauma, Row.ShakePx, Row.ShakeRotationDegrees, Row.ShakeLocationCm, Row.ReportedShakeOffset.X, Row.ReportedShakeOffset.Y, Row.FovDegrees, Row.ArmLengthCm, Row.CameraDistanceCm);
			Csv += FString::Printf(TEXT("%d,%d,%d,%d,%.4f,%.4f,%.4f,%d,%d,%.4f,"), Row.ShakesStarted, Row.FlashCount, Row.KickCount, Row.BossPhasePulses, Row.FlashIntensity, Row.VignetteIntensity, Row.VictimStopRemainingSeconds, Row.bIsVictimStopPending ? 1 : 0, Row.bIsAttackerStopActive ? 1 : 0, Row.PlayerStopRemainingSeconds);
			Csv += FString::Printf(TEXT("%d,%.5f,%d,%d,%d,%.2f,%.2f,"), Row.bIsPlayerPlayingAction ? 1 : 0, Row.PlayerActionSeconds, Row.ComboStageIndex, Row.PrimaryComboStep, Row.bIsUafActive ? 1 : 0, Row.PlayerLocation.X, Row.PlayerLocation.Y);
			Csv += FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f,%d,%d,%.5f,%d,%d,%.2f,%.2f\n"), Row.PlayerAttackerStopRemainingSeconds, Row.ChannelTrauma[0], Row.ChannelTrauma[1], Row.ChannelTrauma[2], Row.bIsVictimUafActive ? 1 : 0, Row.bIsVictimRootMotion ? 1 : 0, Row.VictimActionSeconds, Row.bIsVictimPlayingDeath ? 1 : 0, Row.bIsVictimRagdolling ? 1 : 0, Row.VictimLocation.X, Row.VictimLocation.Y);
		}
		return Csv;
	}

	void WriteHitFeelPieOutputs(FTDHitFeelPieState& State)
	{
		TArray<TSharedPtr<FJsonValue>> RunRows;
		for (const FTDHitFeelPieRun& Run : State.Runs)
		{
			RunRows.Add(MakeShared<FJsonValueObject>(MakeHitFeelPieRunJson(Run)));
		}
		State.Report->SetStringField(TEXT("output_directory"), State.OutputDirectory);
		State.Report->SetBoolField(TEXT("aborted"), State.bIsAborted);
		State.Report->SetArrayField(TEXT("timed_out_steps"), State.TimedOutSteps);
		State.Report->SetNumberField(TEXT("recommended_noise_gain"), State.RecommendedNoiseGain);
		State.Report->SetNumberField(TEXT("configured_noise_gain"), State.ConfiguredNoiseGain);
		State.Report->SetBoolField(TEXT("noise_gain_has_effect"), State.bHasNoiseGainEffect);
		State.Report->SetNumberField(TEXT("frames_sampled"), State.FrameIndex);
		State.Report->SetNumberField(TEXT("rows_recorded"), State.Rows.Num());
		State.Report->SetNumberField(TEXT("contamination_frames"), State.ContaminationFrames.Num());
		State.Report->SetArrayField(TEXT("screenshots"), State.ScreenshotRows);
		State.Report->SetObjectField(TEXT("summary"), State.Summary);
		State.Report->SetArrayField(TEXT("runs"), RunRows);

		FString ReportText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportText);
		if (FJsonSerializer::Serialize(State.Report, Writer))
		{
			FFileHelper::SaveStringToFile(ReportText, *(State.OutputDirectory / TDHitFeelPieReportFileName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		FFileHelper::SaveStringToFile(MakeHitFeelPieFramesCsv(State), *(State.OutputDirectory / TDHitFeelPieFramesFileName), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void RestoreHitFeelPieEnvironment(FTDHitFeelPieState& State)
	{
		State.ActiveRunIndex = INDEX_NONE;
		State.Environment.Restore();
		RestoreHitFeelPieArmLength(State);
	}

	void AbortHitFeelPie(FTDHitFeelPieState& State, const FString& StepName)
	{
		State.TimedOutSteps.Add(MakeShared<FJsonValueString>(StepName));
		State.bIsAborted = true;
		State.TickHook.Stop();
		RestoreHitFeelPieEnvironment(State);
		WriteHitFeelPieOutputs(State);
	}

	bool PrepareHitFeelPieRunActors(FTDHitFeelPieState& State, ETDHitFeelPieScenario Scenario, ATDGameCharacter& Player)
	{
		if (Scenario == ETDHitFeelPieScenario::DealtKill)
		{
			ATDMonsterCharacter* Monster = SpawnHitFeelPieMonster(State, TDHitFeelPieGoblinSpeciesPath, TDHitFeelPieKillSpawnDistanceCm, TDHitFeelPieKillSpawnYawDegrees);
			State.FocusMonster = Monster;
			SetHitFeelPieFocusVictim(State, Monster);
			return Monster != nullptr;
		}
		if (IsHitFeelPieBossScenario(Scenario))
		{
			ATDMonsterCharacter* Boss = State.BossMonster.Get();
			State.FocusMonster = Boss;
			SetHitFeelPieFocusVictim(State, Boss);
			return Boss != nullptr;
		}

		ATDMonsterCharacter* FirstRingMonster = State.RingMonsters.Num() > 0 ? State.RingMonsters[0].Get() : nullptr;
		State.FocusMonster = FirstRingMonster;
		SetHitFeelPieFocusVictim(State, IsHitFeelPieReceivedScenario(Scenario) ? static_cast<ATDCombatCharacter*>(&Player) : static_cast<ATDCombatCharacter*>(FirstRingMonster));
		if (Scenario == ETDHitFeelPieScenario::Combo)
		{
			State.ComboForward = GetHitFeelPiePlayerForward(State, Player);
		}
		return FirstRingMonster != nullptr;
	}

	int32 BeginHitFeelPieRun(FTDHitFeelPieState& State, ETDHitFeelPieScenario Scenario, const FString& Phase, int32 Repetition)
	{
		ATDGameCharacter* Player = State.Player.Get();
		if (!Player || !State.Feedback.IsValid() || !HasHitFeelPieRingMonsters(State, GetHitFeelPieRequiredRingCount(Scenario)))
		{
			return INDEX_NONE;
		}
		if (!PrepareHitFeelPieRunActors(State, Scenario, *Player))
		{
			return INDEX_NONE;
		}

		FTDHitFeelPieRun& Run = State.Runs.AddDefaulted_GetRef();
		Run.RunIndex = State.Runs.Num() - 1;
		Run.Scenario = Scenario;
		Run.Phase = Phase;
		Run.Repetition = Repetition;
		Run.NoiseGain = GetDefault<UTDCombatFeedbackSettings>()->ShakeNoiseGain;
		Run.NoisePhase = Repetition * TDHitFeelPieNoisePhaseStep * TDHitFeelPieNoisePhasePeriod;
		const USpringArmComponent* CameraBoom = Player->GetCameraBoom();
		Run.ArmLengthCm = CameraBoom ? CameraBoom->TargetArmLength : 0.f;
		Run.TargetRatio = GetHitFeelPieTargetRatio(Scenario);
		return Run.RunIndex;
	}

	bool IsHitFeelPieQuiet(const FTDHitFeelPieState& State)
	{
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		const UWorld* World = TDPieTestUtils::GetPieWorld();
		const AWorldSettings* WorldSettings = World ? World->GetWorldSettings() : nullptr;
		if (!Feedback || !WorldSettings)
		{
			return false;
		}
		if (Feedback->GetCameraTraumaForTest() > TDHitFeelPieQuietTrauma || !FMath::IsNearlyEqual(WorldSettings->GetEffectiveTimeDilation(), 1.f, TDHitFeelPieDilationTolerance))
		{
			return false;
		}

		const ATDCombatCharacter* Victim = State.FocusVictim.Get();
		const UTDCharacterAnimationComponent* VictimAnimation = Victim ? Victim->GetCharacterAnimation() : nullptr;
		return !VictimAnimation || (VictimAnimation->GetHitStopRemainingSeconds() <= 0.f && !VictimAnimation->IsHitStopPending());
	}

	bool IsHitFeelPieFocusCommitted(const FTDHitFeelPieState& State)
	{
		const ATDMonsterCharacter* FocusMonster = State.FocusMonster.Get();
		return FocusMonster && FocusMonster->IsInCommittedAttack();
	}

	bool ApplyHitFeelPieDealtInjection(FTDHitFeelPieRun& Run, ATDGameCharacter& Player, ATDMonsterCharacter& Victim, int32 KillCount, ETDKillRank KillRank, bool bIsLethal, ETDDamageDelivery Delivery = ETDDamageDelivery::Melee, bool bIsCritical = false)
	{
		Run.bIsVictimCommitted = Victim.IsInCommittedAttack();
		Run.bIsVictimDangerWindup = Victim.IsInDangerWindup();
		Run.bIsVictimEliteOrBoss = Victim.IsEliteMonster() || Victim.IsBossMonster();
		const FTDDamageResult Result = InjectHitFeelPieDealtDamage(Player, Victim, Run.TargetRatio, bIsLethal, Delivery, bIsCritical);
		Run.AppliedRatio = Result.DamageRatio;
		Run.ExpectedTrauma = UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ResolveHitFeelPieDealtTier(Result), KillCount, KillRank);
		if (bIsLethal)
		{
			return Result.bWasKilled;
		}
		return Result.AppliedDamage > 0.f && (!bIsCritical || Result.bWasCritical);
	}

	bool ApplyHitFeelPieMultiTargetInjection(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, ATDGameCharacter& Player)
	{
		bool bDidAllApply = true;
		for (int32 TargetIndex = 0; TargetIndex < TDHitFeelPieMultiTargetCount; ++TargetIndex)
		{
			ATDMonsterCharacter* Victim = State.RingMonsters[TargetIndex].Get();
			if (!Victim)
			{
				bDidAllApply = false;
				continue;
			}
			const FTDDamageResult Result = InjectHitFeelPieDealtDamage(Player, *Victim, Run.TargetRatio, false);
			bDidAllApply = bDidAllApply && Result.AppliedDamage > 0.f;
			Run.AppliedRatio = FMath::Max(Run.AppliedRatio, Result.DamageRatio);
		}
		Run.ExpectedTrauma = UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, 0, ETDKillRank::None);
		return bDidAllApply;
	}

	bool ApplyHitFeelPieReceivedInjection(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, AActor* Caster, ETDDamageDelivery Delivery)
	{
		const FTDDamageResult Result = InjectHitFeelPieReceivedDamage(State, Caster, Run.TargetRatio, Delivery);
		Run.AppliedRatio = Result.DamageRatio;
		Run.ExpectedTrauma = IsHitFeelPieQuietScenario(Run.Scenario) ? 0.f : UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(Result.DamageRatio, false);
		return Result.AppliedDamage > 0.f;
	}

	bool ApplyHitFeelPieApproachInjection(const FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, ATDGameCharacter& Player, ATDMonsterCharacter& Target)
	{
		UTDSkillComponent* Skill = Player.GetSkillComponent();
		if (!Skill)
		{
			return false;
		}

		Skill->ResetPrimaryCombo();
		const float SurfaceGapCm = Run.Scenario == ETDHitFeelPieScenario::ApproachBaseline ? TDHitFeelPieComboSurfaceGapCm : TDHitFeelPieApproachSurfaceGapCm;
		Run.PlayerStartLocation = Player.GetActorLocation();
		Run.TargetPlacedLocation = PlaceHitFeelPieTargetInFront(Player, Target, GetHitFeelPiePlayerForward(State, Player), SurfaceGapCm);
		Run.bIsVictimEliteOrBoss = Target.IsBossMonster();
		Run.bDidStartPlayerSwing = Player.ExecutePrimaryAttack(&Target);
		return Run.bDidStartPlayerSwing;
	}

	bool ApplyHitFeelPiePhaseInjection(FTDHitFeelPieRun& Run, ATDGameCharacter& Player, ATDMonsterCharacter& Boss)
	{
		UTDCombatComponent* BossCombat = Boss.GetCombatComponent();
		if (!BossCombat || BossCombat->GetMaxHealth() <= 0.f)
		{
			return false;
		}

		const float HealthRatio = BossCombat->GetCurrentHealth() / BossCombat->GetMaxHealth();
		for (const float HealthMark : Boss.GetPhaseHealthMarks())
		{
			if (HealthMark < HealthRatio && HealthMark > Run.PhaseMark)
			{
				Run.PhaseMark = HealthMark;
			}
		}
		if (Run.PhaseMark <= 0.f)
		{
			return false;
		}

		Run.TargetRatio = HealthRatio - Run.PhaseMark + TDHitFeelPiePhaseDamageMargin;
		FTDDamageContext Context = UTDCombatLibrary::MakeDamageContext(&Player, Boss.GetActorLocation());
		Context.Delivery = ETDDamageDelivery::Status;
		const FTDDamageResult Result = BossCombat->ReceiveDamage(ComputeHitFeelPieRawDamage(*BossCombat, Run.TargetRatio), ETDDamageElement::Physical, false, Context);
		Run.AppliedRatio = Result.DamageRatio;
		Run.ExpectedTrauma = GetDefault<UTDCombatFeedbackSettings>()->PhaseEventTrauma;
		return Result.AppliedDamage > 0.f && !Result.bWasKilled;
	}

	bool ApplyHitFeelPieInjection(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, ATDGameCharacter& Player)
	{
		ATDMonsterCharacter* FocusMonster = State.FocusMonster.Get();
		if (!FocusMonster)
		{
			return false;
		}

		switch (Run.Scenario)
		{
		case ETDHitFeelPieScenario::DealtLight:
		case ETDHitFeelPieScenario::DealtHeavy:
		case ETDHitFeelPieScenario::BossCommittedHit:
			return ApplyHitFeelPieDealtInjection(Run, Player, *FocusMonster, 0, ETDKillRank::None, false);
		case ETDHitFeelPieScenario::DealtCritical:
			return ApplyHitFeelPieDealtInjection(Run, Player, *FocusMonster, 0, ETDKillRank::None, false, ETDDamageDelivery::Melee, true);
		case ETDHitFeelPieScenario::DealtSpellKnockback:
			return ApplyHitFeelPieDealtInjection(Run, Player, *FocusMonster, 0, ETDKillRank::None, false, ETDDamageDelivery::Spell);
		case ETDHitFeelPieScenario::DealtKill:
			return ApplyHitFeelPieDealtInjection(Run, Player, *FocusMonster, 1, ETDKillRank::Normal, true);
		case ETDHitFeelPieScenario::DealtMultiTarget:
			return ApplyHitFeelPieMultiTargetInjection(State, Run, Player);
		case ETDHitFeelPieScenario::ReceivedZeroShake:
			State.Environment.SetShakeMuted(true);
			return ApplyHitFeelPieReceivedInjection(State, Run, FocusMonster, ETDDamageDelivery::Melee);
		case ETDHitFeelPieScenario::ReceivedStatus:
			return ApplyHitFeelPieReceivedInjection(State, Run, FocusMonster, ETDDamageDelivery::Status);
		case ETDHitFeelPieScenario::Surrounded:
			Run.SurroundHitsDone = 1;
			return ApplyHitFeelPieReceivedInjection(State, Run, FocusMonster, ETDDamageDelivery::Melee);
		case ETDHitFeelPieScenario::ApproachBaseline:
		case ETDHitFeelPieScenario::ApproachStep:
		case ETDHitFeelPieScenario::BossApproach:
			return ApplyHitFeelPieApproachInjection(State, Run, Player, *FocusMonster);
		case ETDHitFeelPieScenario::BossPhase:
			return ApplyHitFeelPiePhaseInjection(Run, Player, *FocusMonster);
		case ETDHitFeelPieScenario::Combo:
			if (UTDSkillComponent* Skill = Player.GetSkillComponent())
			{
				Skill->ResetPrimaryCombo();
			}
			PlaceHitFeelPieTargetInFront(Player, *FocusMonster, State.ComboForward, TDHitFeelPieComboPinGapCm);
			Run.ComboAttackCommands = 1;
			Run.bDidStartPlayerSwing = Player.ExecutePrimaryAttack(FocusMonster);
			return Run.bDidStartPlayerSwing;
		case ETDHitFeelPieScenario::BossKill:
			PlaceHitFeelPieTargetInFront(Player, *FocusMonster, State.PlayerHomeForward);
			Run.bIsBossVictim = FocusMonster->IsBossMonster();
			Run.bDidStartPlayerSwing = Player.ExecutePrimaryAttack(FocusMonster);
			return true;
		default:
			return ApplyHitFeelPieReceivedInjection(State, Run, FocusMonster, ETDDamageDelivery::Melee);
		}
	}

	void ApplyHitFeelPieNoisePhase(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run)
	{
		State.ShakePattern.Reset();
		UTDTraumaCameraShakePattern* ShakePattern = ResolveHitFeelPieShakePattern(State);
		if (!ShakePattern)
		{
			return;
		}
		ShakePattern->SetNoisePhase(Run.NoisePhase);
		Run.bDidSetNoisePhase = true;
	}

	bool InjectHitFeelPieRun(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, bool bIsQuiet)
	{
		ATDGameCharacter* Player = State.Player.Get();
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!Player || !Feedback)
		{
			return false;
		}

		SetHitFeelPieFocusVictim(State, State.FocusVictim.Get());
		CaptureHitFeelPieCounters(*Feedback, Run.ShakesBefore, Run.FlashesBefore, Run.KicksBefore, Run.PhasePulsesBefore);
		Run.bWasQuietAtInject = bIsQuiet;
		Run.BaselineShakePx = MeasureHitFeelPieCurrentShakePx(State);
		Run.InjectFrame = State.FrameIndex;
		Run.FirstRowIndex = State.Rows.Num();
		Run.bIsInjected = true;
		State.ActiveRunIndex = Run.RunIndex;
		Run.bDidInjectionApply = ApplyHitFeelPieInjection(State, Run, *Player);
		Run.InjectTrauma = Feedback->GetCameraTraumaForTest();
		Run.bDidShakeStartAtInject = Feedback->GetStats().ShakesStarted > Run.ShakesBefore;
		if (Run.bDidShakeStartAtInject)
		{
			ApplyHitFeelPieNoisePhase(State, Run);
		}
		return true;
	}

	bool AdvanceHitFeelPieSurrounded(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, int32 ElapsedFrames)
	{
		if (Run.SurroundHitsDone < TDHitFeelPieSurroundHitCount && ElapsedFrames >= Run.SurroundHitsDone * TDHitFeelPieSurroundHitIntervalFrames)
		{
			ATDMonsterCharacter* Caster = State.RingMonsters[Run.SurroundHitsDone].Get();
			const FTDDamageResult Result = InjectHitFeelPieReceivedDamage(State, Caster, Run.TargetRatio, ETDDamageDelivery::Melee);
			Run.bDidInjectionApply = Run.bDidInjectionApply && Result.AppliedDamage > 0.f;
			++Run.SurroundHitsDone;
		}
		return ElapsedFrames >= GetHitFeelPieObserveFrames(Run.Scenario);
	}

	void UpdateHitFeelPieKnockbackWatch(const FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, const ATDMonsterCharacter& Target)
	{
		if (Run.KnockbackWatchStartFrame != INDEX_NONE)
		{
			const UTDCharacterAnimationComponent* TargetAnimation = Target.GetCharacterAnimation();
			const bool bIsStopped = TargetAnimation && (TargetAnimation->GetVictimHitStopRemainingSeconds() > 0.f || TargetAnimation->IsHitStopPending());
			if (bIsStopped)
			{
				Run.KnockbackStopEndFrame = INDEX_NONE;
			}
			else if (Run.KnockbackStopEndFrame == INDEX_NONE)
			{
				Run.KnockbackStopEndFrame = State.FrameIndex;
			}
			const bool bHasSampled = Run.KnockbackStopEndFrame != INDEX_NONE && State.FrameIndex - Run.KnockbackStopEndFrame >= TDHitFeelPieKnockbackDistanceWindowFrames;
			const bool bHasTimedOut = State.FrameIndex - Run.KnockbackWatchStartFrame >= TDHitFeelPieKnockbackWatchLimitFrames;
			if (bHasSampled || bHasTimedOut)
			{
				Run.KnockbackWatchStartFrame = INDEX_NONE;
				Run.KnockbackStopEndFrame = INDEX_NONE;
			}
			return;
		}

		for (const FTDHitFeelPieComboHit& Hit : State.ComboHits)
		{
			const bool bIsWatchedStage = Hit.StageCount == 1 || Hit.StageCount == TDHitFeelPieComboStageCount;
			if (!Hit.bWasFromPlayer || !bIsWatchedStage || Run.WatchedKnockbackStages.Contains(Hit.StageCount))
			{
				continue;
			}
			Run.WatchedKnockbackStages.Add(Hit.StageCount);
			Run.KnockbackWatchStartFrame = Hit.Frame;
			return;
		}
	}

	bool AdvanceHitFeelPieCombo(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, int32 ElapsedFrames)
	{
		ATDGameCharacter* Player = State.Player.Get();
		ATDMonsterCharacter* Target = State.FocusMonster.Get();
		const UTDSkillComponent* Skill = Player ? Player->GetSkillComponent() : nullptr;
		const UTDCharacterAnimationComponent* Animation = Player ? Player->GetCharacterAnimation() : nullptr;
		UTDCombatComponent* TargetCombat = Target ? Target->GetCombatComponent() : nullptr;
		if (!Skill || !Animation || !TargetCombat)
		{
			return true;
		}

		UpdateHitFeelPieKnockbackWatch(State, Run, *Target);
		if (Run.KnockbackWatchStartFrame == INDEX_NONE)
		{
			PlaceHitFeelPieTargetInFront(*Player, *Target, State.ComboForward, TDHitFeelPieComboPinGapCm);
		}
		if (TargetCombat->GetCurrentHealth() < TargetCombat->GetMaxHealth() * TDHitFeelPieMonsterHealthRefillRatio)
		{
			TargetCombat->SetStats(TargetCombat->GetStats(), true);
		}

		const int32 StageIndex = Skill->GetActivePrimaryComboStageIndex();
		if (StageIndex >= 0 && (Run.ComboStageValues.Num() == 0 || Run.ComboStageValues.Last() != StageIndex))
		{
			Run.ComboStageValues.Add(StageIndex);
		}
		State.ComboLiveStageCount = Run.ComboStageValues.Num();

		const bool bHasAllStages = Run.ComboStageValues.Num() >= TDHitFeelPieComboStageCount;
		if (!bHasAllStages && ElapsedFrames % TDHitFeelPieComboAttackIntervalFrames == 0)
		{
			Player->ExecutePrimaryAttack(Target);
			++Run.ComboAttackCommands;
		}

		const bool bIsActionOver = StageIndex < 0 || !Animation->IsPlayingAction();
		Run.ComboIdleFrames = bHasAllStages && bIsActionOver && Run.KnockbackWatchStartFrame == INDEX_NONE ? Run.ComboIdleFrames + 1 : 0;
		return Run.ComboIdleFrames >= TDHitFeelPieComboTailFrames || ElapsedFrames >= TDHitFeelPieComboFrameLimit;
	}

	bool AdvanceHitFeelPieApproach(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, int32 ElapsedFrames)
	{
		ATDMonsterCharacter* Target = State.FocusMonster.Get();
		if (Target && ElapsedFrames <= TDHitFeelPieApproachSampleFrames)
		{
			Target->SetActorLocation(Run.TargetPlacedLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		return ElapsedFrames >= GetHitFeelPieObserveFrames(Run.Scenario);
	}

	bool AdvanceHitFeelPieBossKill(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, int32 ElapsedFrames)
	{
		if (Run.KillFrame != INDEX_NONE)
		{
			return State.FrameIndex - Run.KillFrame >= GetHitFeelPieObserveFrames(Run.Scenario);
		}

		ATDGameCharacter* Player = State.Player.Get();
		ATDMonsterCharacter* Boss = State.FocusMonster.Get();
		const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get();
		if (!Player || !Boss || !Feedback)
		{
			Run.bDidInjectionApply = false;
			return true;
		}
		if (ElapsedFrames < TDHitFeelPieBossKillDelayFrames)
		{
			return false;
		}

		CaptureHitFeelPieCounters(*Feedback, Run.ShakesBefore, Run.FlashesBefore, Run.KicksBefore, Run.PhasePulsesBefore);
		Run.KillFrame = State.FrameIndex;
		Run.bIsVictimCommitted = Boss->IsInCommittedAttack();
		const FTDDamageResult Result = InjectHitFeelPieDealtDamage(*Player, *Boss, 0.f, true);
		Run.AppliedRatio = Result.DamageRatio;
		Run.ExpectedTrauma = UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ResolveHitFeelPieDealtTier(Result), 1, ETDKillRank::Boss);
		Run.bDidInjectionApply = Result.bWasKilled;
		Run.InjectTrauma = Feedback->GetCameraTraumaForTest();
		Run.bDidShakeStartAtInject = Feedback->GetStats().ShakesStarted > Run.ShakesBefore;
		if (Run.bDidShakeStartAtInject)
		{
			ApplyHitFeelPieNoisePhase(State, Run);
		}
		return false;
	}

	bool AdvanceHitFeelPieScreenshotBurst(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run)
	{
		if (Run.ScreenshotsRequested >= TDHitFeelPieScreenshotBurstFrames)
		{
			return true;
		}
		RequestHitFeelPieScreenshot(State, FString::Printf(TEXT("burst_received_15_percent_%02d.png"), Run.ScreenshotsRequested));
		++Run.ScreenshotsRequested;
		return false;
	}

	bool AdvanceHitFeelPieBossPhase(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run, int32 ElapsedFrames)
	{
		if (Run.PhaseEntryFrame != INDEX_NONE)
		{
			return State.FrameIndex - Run.PhaseEntryFrame >= GetHitFeelPieObserveFrames(Run.Scenario);
		}

		const FTDHitFeelPieFrameRow* LastRow = State.Rows.Num() > 0 ? &State.Rows.Last() : nullptr;
		const bool bHasEnteredPhase = LastRow && LastRow->RunIndex == Run.RunIndex && LastRow->ChannelTrauma[static_cast<int32>(ETDShakeChannel::Event)] > 0.f;
		if (bHasEnteredPhase)
		{
			Run.PhaseEntryFrame = LastRow->Frame;
			return false;
		}
		return ElapsedFrames >= TDHitFeelPiePhaseEntryWaitLimitFrames;
	}

	bool AdvanceHitFeelPieRun(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run)
	{
		const int32 ElapsedFrames = State.FrameIndex - Run.InjectFrame;
		switch (Run.Scenario)
		{
		case ETDHitFeelPieScenario::BossPhase:
			return AdvanceHitFeelPieBossPhase(State, Run, ElapsedFrames);
		case ETDHitFeelPieScenario::Surrounded:
			return AdvanceHitFeelPieSurrounded(State, Run, ElapsedFrames);
		case ETDHitFeelPieScenario::Combo:
			return AdvanceHitFeelPieCombo(State, Run, ElapsedFrames);
		case ETDHitFeelPieScenario::BossKill:
			return AdvanceHitFeelPieBossKill(State, Run, ElapsedFrames);
		case ETDHitFeelPieScenario::ScreenshotBurst:
			return AdvanceHitFeelPieScreenshotBurst(State, Run);
		case ETDHitFeelPieScenario::ApproachBaseline:
		case ETDHitFeelPieScenario::ApproachStep:
		case ETDHitFeelPieScenario::BossApproach:
			return AdvanceHitFeelPieApproach(State, Run, ElapsedFrames);
		default:
			return ElapsedFrames >= GetHitFeelPieObserveFrames(Run.Scenario);
		}
	}

	void ResolveHitFeelPieStartTrauma(FTDHitFeelPieRun& Run)
	{
		if (Run.bDidShakeStartAtInject)
		{
			Run.StartTrauma = Run.InjectTrauma;
			Run.StartTraumaSource = TEXT("inject_call");
			return;
		}
		if (Run.FirstFrameTrauma < 0.f)
		{
			return;
		}

		const float DecayCorrectedTrauma = Run.FirstFrameTrauma + TDHitFeelPieTraumaDecayPerSecond * static_cast<float>(Run.FirstFrameRealDelta);
		const bool bIsRawCloser = Run.ExpectedTrauma >= 0.f && FMath::Abs(Run.FirstFrameTrauma - Run.ExpectedTrauma) <= FMath::Abs(DecayCorrectedTrauma - Run.ExpectedTrauma);
		Run.StartTrauma = bIsRawCloser ? Run.FirstFrameTrauma : DecayCorrectedTrauma;
		Run.StartTraumaSource = bIsRawCloser ? TEXT("first_frame") : TEXT("first_frame_decay_corrected");
	}

	int32 GetHitFeelPieEventFrame(const FTDHitFeelPieRun& Run)
	{
		if (Run.KillFrame != INDEX_NONE)
		{
			return Run.KillFrame;
		}
		return Run.PhaseEntryFrame != INDEX_NONE ? Run.PhaseEntryFrame - 1 : Run.InjectFrame;
	}

	void CheckHitFeelPieTraumaDecay(FTDHitFeelPieRun& Run, const FTDHitFeelPieFrameRow& Row, const FTDHitFeelPieFrameRow* PreviousRow, float DecayPerSecond)
	{
		if (!PreviousRow || PreviousRow->Frame + 1 != Row.Frame || PreviousRow->ShakesStarted != Row.ShakesStarted || PreviousRow->KickCount != Row.KickCount)
		{
			return;
		}

		const float AllowedDecrease = DecayPerSecond * static_cast<float>(Row.RealDeltaSeconds) + TDHitFeelPieTraumaDecaySlack;
		for (int32 ChannelIndex = 0; ChannelIndex < TDHitCameraShakeMath::ShakeChannelCount; ++ChannelIndex)
		{
			const float DecayExcess = PreviousRow->ChannelTrauma[ChannelIndex] - Row.ChannelTrauma[ChannelIndex] - AllowedDecrease;
			if (DecayExcess <= 0.f)
			{
				continue;
			}
			++Run.TraumaDecayViolations;
			if (DecayExcess <= Run.WorstTraumaDecayExcess)
			{
				continue;
			}
			Run.WorstTraumaDecayExcess = DecayExcess;
			Run.WorstTraumaDecayFrame = Row.Frame;
		}
	}

	void AnalyzeHitFeelPieRows(const FTDHitFeelPieState& State, FTDHitFeelPieRun& Run)
	{
		const int32 EventFrame = GetHitFeelPieEventFrame(Run);
		const float DecayPerSecond = GetDefault<UTDCombatFeedbackSettings>()->ShakeTraumaDecayPerSecond;
		const int32 EventChannelIndex = static_cast<int32>(ETDShakeChannel::Event);
		const FTDHitFeelPieFrameRow* PreviousRow = nullptr;
		bool bHasFoundShakeStart = false;
		for (int32 RowIndex = Run.FirstRowIndex; RowIndex < State.Rows.Num(); ++RowIndex)
		{
			const FTDHitFeelPieFrameRow& Row = State.Rows[RowIndex];
			if (Row.RunIndex != Run.RunIndex)
			{
				continue;
			}

			++Run.RowCount;
			Run.InvalidRowCount += Row.bIsFixedStep ? 0 : 1;
			Run.MaxShakePx = FMath::Max(Run.MaxShakePx, Row.ShakePx);
			Run.MaxShakeLocationCm = FMath::Max(Run.MaxShakeLocationCm, Row.ShakeLocationCm);
			Run.VictimPendingFrames += Row.bIsVictimStopPending ? 1 : 0;
			CheckHitFeelPieTraumaDecay(Run, Row, PreviousRow, DecayPerSecond);
			PreviousRow = &Row;
			if (Row.Frame <= EventFrame)
			{
				continue;
			}

			Run.MaxTrauma = FMath::Max(Run.MaxTrauma, Row.Trauma);
			Run.PeakEventTrauma = FMath::Max(Run.PeakEventTrauma, Row.ChannelTrauma[EventChannelIndex]);
			if (Run.EventStartTrauma < 0.f && Row.ChannelTrauma[EventChannelIndex] > 0.f)
			{
				Run.EventStartTrauma = Row.ChannelTrauma[EventChannelIndex] + DecayPerSecond * static_cast<float>(Row.RealDeltaSeconds);
			}
			if (!bHasFoundShakeStart && Row.ShakesStarted > Run.ShakesBefore)
			{
				bHasFoundShakeStart = true;
				Run.FirstFrameTrauma = Row.Trauma;
				Run.FirstFrameRealDelta = Row.RealDeltaSeconds;
			}
			const bool bIsSlowMotion = Row.TimeDilation < 1.f - TDHitFeelPieDilationTolerance;
			if (bIsSlowMotion && Run.SlowMotionFirstFrame == INDEX_NONE)
			{
				Run.SlowMotionFirstFrame = Row.Frame;
			}
			if (bIsSlowMotion)
			{
				Run.SlowMotionMinDilation = FMath::Min(Run.SlowMotionMinDilation, Row.TimeDilation);
			}
			if (Row.bIsAttackerStopActive)
			{
				Run.AttackerStopLastFrame = Row.Frame;
			}
			Run.AttackerStopSpan.Add(Row.bIsAttackerStopActive, Row.RealSeconds);
			Run.ShakeSpan.Add(Row.Trauma > TDHitFeelPieQuietTrauma, Row.RealSeconds);
			Run.SlowMotionSpan.Add(bIsSlowMotion, Row.RealSeconds);
			if (Row.Frame == EventFrame + TDHitFeelPieBossDilationCheckFrames)
			{
				Run.DilationAtCheck = Row.TimeDilation;
			}
		}
		Run.bIsValid = Run.RowCount > 0 && Run.InvalidRowCount == 0;
		ResolveHitFeelPieStartTrauma(Run);
	}

	bool IsHitFeelPieVictimPoseStopped(const FTDHitFeelPieFrameRow& Row, const FTDHitFeelPieFrameRow* PreviousRow)
	{
		if (!Row.bIsVictimUafActive)
		{
			return true;
		}
		if (!Row.bIsVictimRootMotion || !PreviousRow || PreviousRow->Frame + 1 != Row.Frame || !PreviousRow->bIsVictimRootMotion)
		{
			return false;
		}
		const float ClockIncrement = Row.VictimActionSeconds - PreviousRow->VictimActionSeconds;
		return FMath::Abs(ClockIncrement - TDHitFeelPieAttackerSlowPlayRate * Row.WorldDeltaSeconds) <= TDHitFeelPieActionClockEpsilon;
	}

	int32 CountHitFeelPieVictimStopFrames(const FTDHitFeelPieState& State, const FTDHitFeelPieRun& Run, int32 FirstFrame, int32& OutStartDelayFrames)
	{
		int32 StopFrames = 0;
		OutStartDelayFrames = 0;
		const FTDHitFeelPieFrameRow* PreviousRow = nullptr;
		for (int32 RowIndex = Run.FirstRowIndex; RowIndex < State.Rows.Num(); ++RowIndex)
		{
			const FTDHitFeelPieFrameRow& Row = State.Rows[RowIndex];
			if (Row.RunIndex != Run.RunIndex)
			{
				continue;
			}
			const bool bIsStopped = IsHitFeelPieVictimPoseStopped(Row, PreviousRow);
			PreviousRow = &Row;
			if (Row.Frame < FirstFrame)
			{
				continue;
			}
			if (bIsStopped)
			{
				++StopFrames;
				continue;
			}
			if (StopFrames > 0)
			{
				break;
			}
			++OutStartDelayFrames;
			if (OutStartDelayFrames > TDHitFeelPieVictimStopStartFrames)
			{
				break;
			}
		}
		return StopFrames;
	}

	FTDHitFeelPieKnockbackSample EstimateHitFeelPieKnockbackLaunch(const TArray<float>& Speeds)
	{
		FTDHitFeelPieKnockbackSample Sample;
		int32 PeakIndex = INDEX_NONE;
		for (int32 SpeedIndex = 0; SpeedIndex < Speeds.Num(); ++SpeedIndex)
		{
			if (Speeds[SpeedIndex] > Sample.PeakSpeed)
			{
				Sample.PeakSpeed = Speeds[SpeedIndex];
				PeakIndex = SpeedIndex;
			}
		}
		Sample.LaunchSpeed = Sample.PeakSpeed;
		if (PeakIndex == INDEX_NONE || PeakIndex + 2 >= Speeds.Num())
		{
			return Sample;
		}

		const float FirstDrop = Speeds[PeakIndex] - Speeds[PeakIndex + 1];
		const float SecondDrop = Speeds[PeakIndex + 1] - Speeds[PeakIndex + 2];
		if (FirstDrop <= TDHitFeelPieMinimumSpeedDropCmPerSecond || SecondDrop <= TDHitFeelPieMinimumSpeedDropCmPerSecond || SecondDrop > FirstDrop)
		{
			return Sample;
		}
		Sample.BrakingFactor = 1.f - SecondDrop / FirstDrop;
		Sample.LaunchSpeed = Sample.PeakSpeed + FirstDrop * FirstDrop / SecondDrop;
		return Sample;
	}

	FTDHitFeelPieKnockbackSample MeasureHitFeelPieKnockback(const FTDHitFeelPieState& State, const FTDHitFeelPieRun& Run, int32 HitFrame)
	{
		TArray<float> Speeds;
		const FTDHitFeelPieFrameRow* PreviousRow = nullptr;
		bool bHasStopEnded = false;
		for (int32 RowIndex = Run.FirstRowIndex; RowIndex < State.Rows.Num(); ++RowIndex)
		{
			const FTDHitFeelPieFrameRow& Row = State.Rows[RowIndex];
			if (Row.RunIndex != Run.RunIndex || Row.Frame < HitFrame)
			{
				continue;
			}
			const bool bIsStopped = Row.VictimStopRemainingSeconds > 0.f || Row.bIsVictimStopPending || !Row.bIsVictimUafActive;
			if (!bHasStopEnded && bIsStopped)
			{
				PreviousRow = &Row;
				continue;
			}
			bHasStopEnded = true;
			const bool bIsConsecutive = PreviousRow && PreviousRow->Frame + 1 == Row.Frame && Row.WorldDeltaSeconds > 0.0;
			Speeds.Add(bIsConsecutive ? static_cast<float>(FVector::Dist2D(Row.VictimLocation, PreviousRow->VictimLocation) / Row.WorldDeltaSeconds) : 0.f);
			PreviousRow = &Row;
			if (Speeds.Num() >= TDHitFeelPieKnockbackWindowFrames)
			{
				break;
			}
		}
		return EstimateHitFeelPieKnockbackLaunch(Speeds);
	}

	float MeasureHitFeelPieKnockbackDistance(const FTDHitFeelPieState& State, const FTDHitFeelPieRun& Run, int32 HitFrame)
	{
		FVector StartLocation = FVector::ZeroVector;
		bool bHasStartLocation = false;
		bool bHasStopEnded = false;
		int32 WindowFrames = 0;
		float MaxDistanceCm = -1.f;
		for (int32 RowIndex = Run.FirstRowIndex; RowIndex < State.Rows.Num(); ++RowIndex)
		{
			const FTDHitFeelPieFrameRow& Row = State.Rows[RowIndex];
			if (Row.RunIndex != Run.RunIndex || Row.Frame < HitFrame)
			{
				continue;
			}
			const bool bIsStopped = Row.VictimStopRemainingSeconds > 0.f || Row.bIsVictimStopPending || !Row.bIsVictimUafActive;
			if (!bHasStopEnded && bIsStopped)
			{
				StartLocation = Row.VictimLocation;
				bHasStartLocation = true;
				continue;
			}
			bHasStopEnded = true;
			if (!bHasStartLocation)
			{
				StartLocation = Row.VictimLocation;
				bHasStartLocation = true;
			}
			MaxDistanceCm = FMath::Max(MaxDistanceCm, static_cast<float>(FVector::Dist2D(Row.VictimLocation, StartLocation)));
			++WindowFrames;
			if (WindowFrames >= TDHitFeelPieKnockbackDistanceWindowFrames)
			{
				break;
			}
		}
		return MaxDistanceCm;
	}

	void AnalyzeHitFeelPieKillPose(const FTDHitFeelPieState& State, FTDHitFeelPieRun& Run)
	{
		const int32 EventFrame = GetHitFeelPieEventFrame(Run);
		Run.KillPoseHoldFrames = 0;
		for (int32 RowIndex = Run.FirstRowIndex; RowIndex < State.Rows.Num(); ++RowIndex)
		{
			const FTDHitFeelPieFrameRow& Row = State.Rows[RowIndex];
			if (Row.RunIndex != Run.RunIndex || Row.Frame <= EventFrame)
			{
				continue;
			}
			if (Row.bIsVictimPlayingDeath || Row.bIsVictimRagdolling)
			{
				Run.DeathStartFrame = Row.Frame;
				return;
			}
			Run.KillPoseHoldFrames += !Row.bIsVictimUafActive || Row.VictimStopRemainingSeconds > 0.f ? 1 : 0;
		}
	}

	float MeasureHitFeelPieApproach(const FTDHitFeelPieState& State, const FTDHitFeelPieRun& Run)
	{
		for (int32 RowIndex = Run.FirstRowIndex; RowIndex < State.Rows.Num(); ++RowIndex)
		{
			const FTDHitFeelPieFrameRow& Row = State.Rows[RowIndex];
			if (Row.RunIndex == Run.RunIndex && Row.Frame == Run.InjectFrame + TDHitFeelPieApproachSampleFrames)
			{
				return FVector::Dist2D(Row.PlayerLocation, Run.PlayerStartLocation);
			}
		}
		return -1.f;
	}

	bool HasHitFeelPieContamination(const FTDHitFeelPieState& State, const FTDHitFeelPieRun& Run)
	{
		for (const int32 ContaminationFrame : State.ContaminationFrames)
		{
			if (ContaminationFrame > Run.InjectFrame && ContaminationFrame <= Run.EndFrame)
			{
				return true;
			}
		}
		return false;
	}

	void AccumulateHitFeelPieComboStage(FTDHitFeelPieComboStage& Stage, const FTDHitFeelPieFrameRow& Row, const FTDHitFeelPieFrameRow* PreviousRow)
	{
		Stage.LastFrame = Row.Frame;
		Stage.EndLocation = Row.PlayerLocation;
		Stage.AttackerActiveFrames += Row.bIsAttackerStopActive ? 1 : 0;
		if (!PreviousRow || PreviousRow->ComboStageIndex != Row.ComboStageIndex || !PreviousRow->bIsPlayerPlayingAction || !Row.bIsPlayerPlayingAction)
		{
			return;
		}

		const float ClockIncrement = Row.PlayerActionSeconds - PreviousRow->PlayerActionSeconds;
		if (FMath::Abs(ClockIncrement - TDHitFeelPieAttackerSlowPlayRate * Row.WorldDeltaSeconds) > TDHitFeelPieActionClockEpsilon)
		{
			return;
		}
		++Stage.SlowedFrames;
		Stage.SlowedFramesWithUafInactive += Row.bIsUafActive ? 0 : 1;
	}

	int32 FindHitFeelPieComboStageIndex(const FTDHitFeelPieRun& Run, int32 Frame)
	{
		for (int32 StageIndex = 0; StageIndex < Run.ComboStages.Num(); ++StageIndex)
		{
			const FTDHitFeelPieComboStage& Stage = Run.ComboStages[StageIndex];
			if (Frame >= Stage.FirstFrame && Frame <= Stage.LastFrame)
			{
				return StageIndex;
			}
		}
		return INDEX_NONE;
	}

	void AssignHitFeelPieComboHits(const FTDHitFeelPieState& State, FTDHitFeelPieRun& Run)
	{
		for (const FTDHitFeelPieComboHit& Hit : State.ComboHits)
		{
			const int32 StageIndex = Hit.bWasFromPlayer ? FindHitFeelPieComboStageIndex(Run, Hit.Frame) : INDEX_NONE;
			if (StageIndex == INDEX_NONE)
			{
				continue;
			}
			FTDHitFeelPieComboStage& Stage = Run.ComboStages[StageIndex];
			++Stage.HitCount;
			Stage.bHadCriticalHit = Stage.bHadCriticalHit || Hit.bWasCritical;
			Stage.bWasVictimCommitted = Stage.bWasVictimCommitted || Hit.bWasVictimCommitted;
			Stage.bWasVictimDangerWindup = Stage.bWasVictimDangerWindup || Hit.bWasVictimDangerWindup;
			if (Stage.FirstHitFrame != INDEX_NONE)
			{
				continue;
			}
			Stage.FirstHitFrame = Hit.Frame;
			Stage.VictimStopFrames = CountHitFeelPieVictimStopFrames(State, Run, Hit.Frame + 1, Stage.VictimStopStartDelayFrames);
			const bool bIsKnockbackStage = StageIndex == 0 || StageIndex == TDHitFeelPieComboStageCount - 1;
			if (!bIsKnockbackStage)
			{
				continue;
			}
			const FTDHitFeelPieKnockbackSample Knockback = MeasureHitFeelPieKnockback(State, Run, Hit.Frame);
			Stage.KnockbackSpeed = Knockback.LaunchSpeed;
			Stage.KnockbackPeakSpeed = Knockback.PeakSpeed;
			Stage.KnockbackDistanceCm = MeasureHitFeelPieKnockbackDistance(State, Run, Hit.Frame);
		}
	}

	void AnalyzeHitFeelPieCombo(const FTDHitFeelPieState& State, FTDHitFeelPieRun& Run)
	{
		Run.ComboStages.Reset();
		const FTDHitFeelPieFrameRow* PreviousRow = nullptr;
		for (int32 RowIndex = Run.FirstRowIndex; RowIndex < State.Rows.Num(); ++RowIndex)
		{
			const FTDHitFeelPieFrameRow& Row = State.Rows[RowIndex];
			if (Row.RunIndex != Run.RunIndex)
			{
				continue;
			}

			const bool bStartsStage = Row.ComboStageIndex >= 0 && (Run.ComboStages.Num() == 0 || Run.ComboStages.Last().StageIndexValue != Row.ComboStageIndex);
			if (bStartsStage)
			{
				FTDHitFeelPieComboStage& NewStage = Run.ComboStages.AddDefaulted_GetRef();
				NewStage.StageIndexValue = Row.ComboStageIndex;
				NewStage.FirstFrame = Row.Frame;
				NewStage.StartLocation = Row.PlayerLocation;
			}
			if (Run.ComboStages.Num() > 0 && Run.ComboStages.Last().StageIndexValue == Row.ComboStageIndex)
			{
				AccumulateHitFeelPieComboStage(Run.ComboStages.Last(), Row, PreviousRow);
			}
			PreviousRow = &Row;
		}

		for (FTDHitFeelPieComboStage& Stage : Run.ComboStages)
		{
			Stage.RootMotionCm = FVector::Dist2D(Stage.StartLocation, Stage.EndLocation);
		}
		AssignHitFeelPieComboHits(State, Run);
	}

	void FinishHitFeelPieRun(FTDHitFeelPieState& State, FTDHitFeelPieRun& Run)
	{
		State.ActiveRunIndex = INDEX_NONE;
		Run.EndFrame = State.FrameIndex;
		Run.bIsFinished = true;
		if (Run.Scenario == ETDHitFeelPieScenario::ReceivedZeroShake)
		{
			State.Environment.SetShakeMuted(false);
		}
		if (const UTDCombatFeedbackSubsystem* Feedback = State.Feedback.Get())
		{
			int32 ShakesAfter = 0;
			int32 FlashesAfter = 0;
			int32 KicksAfter = 0;
			int32 PhasePulsesAfter = 0;
			CaptureHitFeelPieCounters(*Feedback, ShakesAfter, FlashesAfter, KicksAfter, PhasePulsesAfter);
			Run.ShakesStartedDelta = ShakesAfter - Run.ShakesBefore;
			Run.FlashDelta = FlashesAfter - Run.FlashesBefore;
			Run.KickDelta = KicksAfter - Run.KicksBefore;
			Run.PhasePulseDelta = PhasePulsesAfter - Run.PhasePulsesBefore;
		}

		AnalyzeHitFeelPieRows(State, Run);
		Run.bIsContaminated = HasHitFeelPieContamination(State, Run);
		if (Run.Scenario == ETDHitFeelPieScenario::Combo)
		{
			UnbindHitFeelPieComboHits(State);
			AnalyzeHitFeelPieCombo(State, Run);
			State.ComboLiveStageCount = 0;
			return;
		}

		Run.VictimStopFrames = CountHitFeelPieVictimStopFrames(State, Run, Run.InjectFrame + 1, Run.VictimStopStartDelayFrames);
		if (Run.Scenario == ETDHitFeelPieScenario::DealtSpellKnockback)
		{
			const FTDHitFeelPieKnockbackSample Knockback = MeasureHitFeelPieKnockback(State, Run, Run.InjectFrame + 1);
			Run.KnockbackSpeed = Knockback.LaunchSpeed;
			Run.KnockbackPeakSpeed = Knockback.PeakSpeed;
			Run.KnockbackBrakingFactor = Knockback.BrakingFactor;
		}
		if (IsHitFeelPieApproachScenario(Run.Scenario))
		{
			Run.ApproachDisplacementCm = MeasureHitFeelPieApproach(State, Run);
		}
		if (Run.Scenario == ETDHitFeelPieScenario::DealtKill || Run.Scenario == ETDHitFeelPieScenario::BossKill)
		{
			AnalyzeHitFeelPieKillPose(State, Run);
		}
		if (Run.Scenario == ETDHitFeelPieScenario::DealtKill)
		{
			DestroyHitFeelPieMonster(State.FocusMonster);
		}
	}

	bool IsHitFeelPieRunUsable(const FTDHitFeelPieRun& Run)
	{
		return Run.bIsFinished && Run.bIsValid && !Run.bIsContaminated && Run.bDidInjectionApply;
	}

	TArray<const FTDHitFeelPieRun*> CollectHitFeelPieRuns(const FTDHitFeelPieState& State, ETDHitFeelPieScenario Scenario, const TCHAR* Phase)
	{
		TArray<const FTDHitFeelPieRun*> Runs;
		for (const FTDHitFeelPieRun& Run : State.Runs)
		{
			if (Run.Scenario != Scenario || !IsHitFeelPieRunUsable(Run) || (Phase && Run.Phase != Phase))
			{
				continue;
			}
			Runs.Add(&Run);
		}
		return Runs;
	}

	float ComputeHitFeelPieMedian(TArray<float> Values)
	{
		if (Values.Num() == 0)
		{
			return -1.f;
		}
		Values.Sort();
		const int32 MiddleIndex = Values.Num() / 2;
		return Values.Num() % 2 == 1 ? Values[MiddleIndex] : (Values[MiddleIndex - 1] + Values[MiddleIndex]) * 0.5f;
	}

	float ComputeHitFeelPieMedianPx(const TArray<const FTDHitFeelPieRun*>& Runs)
	{
		TArray<float> Values;
		for (const FTDHitFeelPieRun* Run : Runs)
		{
			Values.Add(Run->MaxShakePx);
		}
		return ComputeHitFeelPieMedian(Values);
	}

	float ComputeHitFeelPieMedianStartTrauma(const TArray<const FTDHitFeelPieRun*>& Runs)
	{
		TArray<float> Values;
		for (const FTDHitFeelPieRun* Run : Runs)
		{
			if (Run->StartTrauma >= 0.f)
			{
				Values.Add(Run->StartTrauma);
			}
		}
		return ComputeHitFeelPieMedian(Values);
	}

	void ChooseHitFeelPieNoiseGain(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		TArray<TSharedPtr<FJsonValue>> CandidateRows;
		TMap<int32, FVector2D> PxRangeByScenario;
		float BestNoiseGain = State.ConfiguredNoiseGain;
		int32 BestInBandCount = -1;
		double BestBandDistance = TNumericLimits<double>::Max();
		for (const float NoiseGain : State.NoiseGainCandidates)
		{
			const TSharedRef<FJsonObject> CandidateJson = MakeShared<FJsonObject>();
			CandidateJson->SetNumberField(TEXT("noise_gain"), NoiseGain);
			int32 InBandCount = 0;
			double BandDistance = 0.0;
			for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieCalibrationScenarios)
			{
				FTDHitFeelPieBand Band;
				TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, Scenario, TDHitFeelPieCalibrationPhase);
				Runs.RemoveAll([NoiseGain](const FTDHitFeelPieRun* CandidateRun)
				{
					return !FMath::IsNearlyEqual(CandidateRun->NoiseGain, NoiseGain);
				});
				if (Runs.Num() == 0 || !TryGetHitFeelPieBand(Scenario, Band))
				{
					continue;
				}
				const float MedianPx = ComputeHitFeelPieMedianPx(Runs);
				const float CenterPx = (Band.MinPx + Band.MaxPx) * 0.5f;
				const float HalfWidthPx = FMath::Max((Band.MaxPx - Band.MinPx) * 0.5f, 0.01f);
				InBandCount += MedianPx >= Band.MinPx && MedianPx <= Band.MaxPx ? 1 : 0;
				BandDistance += FMath::Abs(MedianPx - CenterPx) / HalfWidthPx;
				CandidateJson->SetNumberField(GetHitFeelPieScenarioName(Scenario), MedianPx);
				FVector2D& PxRange = PxRangeByScenario.FindOrAdd(static_cast<int32>(Scenario), FVector2D(MedianPx, MedianPx));
				PxRange.X = FMath::Min<double>(PxRange.X, MedianPx);
				PxRange.Y = FMath::Max<double>(PxRange.Y, MedianPx);
			}
			CandidateJson->SetNumberField(TEXT("in_band"), InBandCount);
			CandidateJson->SetNumberField(TEXT("band_distance"), BandDistance);
			CandidateRows.Add(MakeShared<FJsonValueObject>(CandidateJson));
			if (InBandCount < BestInBandCount || (InBandCount == BestInBandCount && BandDistance >= BestBandDistance))
			{
				continue;
			}
			BestNoiseGain = NoiseGain;
			BestInBandCount = InBandCount;
			BestBandDistance = BandDistance;
		}

		State.bHasNoiseGainEffect = false;
		for (const TPair<int32, FVector2D>& PxRange : PxRangeByScenario)
		{
			State.bHasNoiseGainEffect = State.bHasNoiseGainEffect || PxRange.Value.Y - PxRange.Value.X >= TDHitFeelPieNoiseGainEffectPx;
		}
		State.RecommendedNoiseGain = BestNoiseGain;
		State.Environment.SetNoiseGain(State.ConfiguredNoiseGain);
		State.Report->SetArrayField(TEXT("noise_gain_calibration"), CandidateRows);
		State.Report->SetNumberField(TEXT("recommended_noise_gain"), State.RecommendedNoiseGain);
		Test.AddInfo(FString::Printf(TEXT("recommended_noise_gain = %.2f (%d of %d calibration bands hit); measurements use the shipped ShakeNoiseGain %.2f"), State.RecommendedNoiseGain, BestInBandCount, static_cast<int32>(UE_ARRAY_COUNT(TDHitFeelPieCalibrationScenarios)), State.ConfiguredNoiseGain));
	}

	void VerifyHitFeelPieNoiseGain(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		Test.TestTrue(*FString::Printf(TEXT("Calibration sweep around ShakeNoiseGain %.2f changes the median peak shake by at least %.1f px"), State.ConfiguredNoiseGain, TDHitFeelPieNoiseGainEffectPx), State.bHasNoiseGainEffect);
		Test.TestTrue(*FString::Printf(TEXT("Calibrated NoiseGain %.2f is within %.1f of the shipped ShakeNoiseGain %.2f"), State.RecommendedNoiseGain, TDHitFeelPieNoiseGainTolerance, State.ConfiguredNoiseGain), FMath::Abs(State.RecommendedNoiseGain - State.ConfiguredNoiseGain) <= TDHitFeelPieNoiseGainTolerance + KINDA_SMALL_NUMBER);
	}

	void VerifyHitFeelPieFixedStep(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		int32 FinishedRunCount = 0;
		int32 InvalidRunCount = 0;
		int32 InvalidRowCount = 0;
		int32 ContaminatedRunCount = 0;
		for (const FTDHitFeelPieRun& Run : State.Runs)
		{
			if (!Run.bIsFinished)
			{
				continue;
			}
			++FinishedRunCount;
			InvalidRunCount += Run.bIsValid ? 0 : 1;
			InvalidRowCount += Run.InvalidRowCount;
			ContaminatedRunCount += Run.bIsContaminated ? 1 : 0;
		}
		State.Summary->SetNumberField(TEXT("finished_runs"), FinishedRunCount);
		State.Summary->SetNumberField(TEXT("invalid_runs"), InvalidRunCount);
		State.Summary->SetNumberField(TEXT("invalid_rows"), InvalidRowCount);
		State.Summary->SetNumberField(TEXT("contaminated_runs"), ContaminatedRunCount);
		Test.TestTrue(*FString::Printf(TEXT("HitFeel finished %d measurement runs"), FinishedRunCount), FinishedRunCount > 0);
		Test.TestEqual(*FString::Printf(TEXT("Every run advances in fixed 1/60 s real frames (%d invalid rows; invalid runs are excluded from value checks)"), InvalidRowCount), InvalidRunCount, 0);
	}

	void VerifyHitFeelPieTraumaDecay(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		int32 ViolationCount = 0;
		float WorstExcess = 0.f;
		FString WorstRunName = TEXT("none");
		for (const FTDHitFeelPieRun& Run : State.Runs)
		{
			if (!IsHitFeelPieRunUsable(Run))
			{
				continue;
			}
			ViolationCount += Run.TraumaDecayViolations;
			if (Run.WorstTraumaDecayExcess <= WorstExcess)
			{
				continue;
			}
			WorstExcess = Run.WorstTraumaDecayExcess;
			WorstRunName = FString::Printf(TEXT("%s #%d frame %d"), GetHitFeelPieScenarioName(Run.Scenario), Run.RunIndex, Run.WorstTraumaDecayFrame);
		}
		State.Summary->SetNumberField(TEXT("trauma_decay_violations"), ViolationCount);
		State.Summary->SetNumberField(TEXT("worst_trauma_decay_excess"), WorstExcess);
		Test.TestEqual(*FString::Printf(TEXT("No shake channel loses more than %.1f x real dt + 0.003 trauma between frames without a new shake (worst excess %.4f at %s)"), GetDefault<UTDCombatFeedbackSettings>()->ShakeTraumaDecayPerSecond, WorstExcess, *WorstRunName), ViolationCount, 0);
	}

	void VerifyHitFeelPieShakeStarts(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieShakeScenarios)
		{
			const TCHAR* ScenarioName = GetHitFeelPieScenarioName(Scenario);
			const TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, Scenario, nullptr);
			int32 MissingShakeCount = 0;
			for (const FTDHitFeelPieRun* Run : Runs)
			{
				MissingShakeCount += Run->ShakesStartedDelta >= 1 ? 0 : 1;
			}
			Test.TestTrue(*FString::Printf(TEXT("%s has clean fixed-step runs (%d)"), ScenarioName, Runs.Num()), Runs.Num() > 0);
			Test.TestEqual(*FString::Printf(TEXT("Every %s run starts at least one camera shake"), ScenarioName), MissingShakeCount, 0);
		}
	}

	void VerifyHitFeelPieStartTrauma(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const TSharedRef<FJsonObject> DeviationJson = MakeShared<FJsonObject>();
		for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieTraumaScenarios)
		{
			const TCHAR* ScenarioName = GetHitFeelPieScenarioName(Scenario);
			float WorstDeviation = 0.f;
			int32 CheckedCount = 0;
			int32 MissingCount = 0;
			for (const FTDHitFeelPieRun* Run : CollectHitFeelPieRuns(State, Scenario, nullptr))
			{
				if (Run->PhasePulseDelta > 0)
				{
					Test.AddInfo(FString::Printf(TEXT("%s run %d also entered a boss phase; its start trauma is recorded only."), ScenarioName, Run->RunIndex));
					continue;
				}
				if (Run->StartTrauma < 0.f)
				{
					++MissingCount;
					continue;
				}
				WorstDeviation = FMath::Max(WorstDeviation, FMath::Abs(Run->StartTrauma - Run->ExpectedTrauma));
				++CheckedCount;
			}
			DeviationJson->SetNumberField(ScenarioName, WorstDeviation);
			Test.TestTrue(*FString::Printf(TEXT("%s start-frame trauma equals Compute* within 0.01 (worst %.4f over %d runs, %d without a shake start)"), ScenarioName, WorstDeviation, CheckedCount, MissingCount), CheckedCount > 0 && MissingCount == 0 && WorstDeviation <= TDHitFeelPieTraumaTolerance);
		}
		State.Summary->SetObjectField(TEXT("start_trauma_worst_deviation"), DeviationJson);
	}

	void VerifyHitFeelPieIntensity(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const float DealtLightTrauma = ComputeHitFeelPieMedianStartTrauma(CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::DealtLight, TDHitFeelPieMeasurePhase));
		const float ReceivedSmallTrauma = ComputeHitFeelPieMedianStartTrauma(CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::ReceivedSmall, TDHitFeelPieMeasurePhase));
		const float IntensityRatio = DealtLightTrauma > 0.f && ReceivedSmallTrauma > 0.f ? FMath::Pow(ReceivedSmallTrauma / DealtLightTrauma, TDHitFeelPieIntensityExponent) : 0.f;
		State.Summary->SetNumberField(TEXT("dealt_light_start_trauma"), DealtLightTrauma);
		State.Summary->SetNumberField(TEXT("received_5_percent_start_trauma"), ReceivedSmallTrauma);
		State.Summary->SetNumberField(TEXT("received_to_dealt_light_intensity"), IntensityRatio);
		Test.TestTrue(*FString::Printf(TEXT("Shake intensity of a 5%% hit on the player is at least 1.6 x a light player hit (%.3f)"), IntensityRatio), IntensityRatio >= TDHitFeelPieReceivedToDealtIntensityMinimum);

		const TArray<const FTDHitFeelPieRun*> MultiTargetRuns = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::DealtMultiTarget, nullptr);
		Test.TestTrue(TEXT("Four-target swing has a clean run"), MultiTargetRuns.Num() > 0);
		for (const FTDHitFeelPieRun* Run : MultiTargetRuns)
		{
			const bool bIsBounded = Run->StartTrauma >= DealtLightTrauma - TDHitFeelPieTraumaTolerance && Run->StartTrauma <= DealtLightTrauma + TDHitFeelPieMultiTargetTraumaAllowance;
			Test.TestTrue(*FString::Printf(TEXT("Four targets hit in one frame start trauma %.4f, within single %.4f + 0.09 + 0.005"), Run->StartTrauma, DealtLightTrauma), DealtLightTrauma > 0.f && bIsBounded);
		}
	}

	float ComputeHitFeelPieMedianLocationCm(const TArray<const FTDHitFeelPieRun*>& Runs)
	{
		TArray<float> Values;
		for (const FTDHitFeelPieRun* Run : Runs)
		{
			Values.Add(Run->MaxShakeLocationCm);
		}
		return ComputeHitFeelPieMedian(Values);
	}

	void VerifyHitFeelPieArena(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const TSharedRef<FJsonObject> ArenaJson = MakeShared<FJsonObject>();
		for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieArenaScenarios)
		{
			const TCHAR* ScenarioName = GetHitFeelPieScenarioName(Scenario);
			const TArray<const FTDHitFeelPieRun*> BaseRuns = CollectHitFeelPieRuns(State, Scenario, TDHitFeelPieMeasurePhase);
			const TArray<const FTDHitFeelPieRun*> ArenaRuns = CollectHitFeelPieRuns(State, Scenario, TDHitFeelPieArenaPhase);
			const float BasePx = ComputeHitFeelPieMedianPx(BaseRuns);
			const float ArenaPx = ComputeHitFeelPieMedianPx(ArenaRuns);
			const float BaseLocationCm = ComputeHitFeelPieMedianLocationCm(BaseRuns);
			const float ArenaLocationCm = ComputeHitFeelPieMedianLocationCm(ArenaRuns);
			const float PxRatio = BasePx > 0.f ? ArenaPx / BasePx : 0.f;
			const float LocationRatio = BaseLocationCm > 0.f ? ArenaLocationCm / BaseLocationCm : 0.f;
			bool bAreArenaArmsApplied = ArenaRuns.Num() > 0;
			for (const FTDHitFeelPieRun* Run : ArenaRuns)
			{
				bAreArenaArmsApplied = bAreArenaArmsApplied && FMath::IsNearlyEqual(Run->ArmLengthCm, TDHitFeelPieArenaArmLengthCm, TDHitFeelPieArmLengthToleranceCm);
			}
			const TSharedRef<FJsonObject> ScenarioJson = MakeShared<FJsonObject>();
			ScenarioJson->SetNumberField(TEXT("px_ratio"), PxRatio);
			ScenarioJson->SetNumberField(TEXT("location_ratio"), LocationRatio);
			ArenaJson->SetObjectField(ScenarioName, ScenarioJson);
			Test.TestTrue(*FString::Printf(TEXT("%s runs on the 1540 cm arena arm"), ScenarioName), bAreArenaArmsApplied);
			Test.TestTrue(*FString::Printf(TEXT("%s camera translation on the 1540 cm arm is 1.4 +- 0.02 x the 1100 cm arm (%.3f cm vs %.3f cm, ratio %.3f)"), ScenarioName, ArenaLocationCm, BaseLocationCm, LocationRatio), BaseLocationCm > 0.f && FMath::Abs(LocationRatio - TDHitFeelPieArenaLocationRatio) <= TDHitFeelPieArenaLocationTolerance);
			Test.TestTrue(*FString::Printf(TEXT("%s screen shake on the 1540 cm arm stays within 3%% of the 1100 cm arm (%.2f px vs %.2f px)"), ScenarioName, ArenaPx, BasePx), BasePx > 0.f && FMath::Abs(PxRatio - 1.f) <= TDHitFeelPieArenaPxTolerance);
		}
		State.Summary->SetObjectField(TEXT("arena_to_base_ratio"), ArenaJson);
	}

	void VerifyHitFeelPiePixels(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const TSharedRef<FJsonObject> MedianJson = MakeShared<FJsonObject>();
		for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieMeasuredScenarios)
		{
			FTDHitFeelPieBand Band;
			if (!TryGetHitFeelPieBand(Scenario, Band))
			{
				continue;
			}
			const TCHAR* ScenarioName = GetHitFeelPieScenarioName(Scenario);
			const TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, Scenario, TDHitFeelPieMeasurePhase);
			const float MedianPx = ComputeHitFeelPieMedianPx(Runs);
			MedianJson->SetNumberField(ScenarioName, MedianPx);
			Test.TestTrue(*FString::Printf(TEXT("%s median peak shake %.2f px over %d phase-stepped runs at the shipped ShakeNoiseGain %.2f is inside %.0f-%.0f px at 1080p"), ScenarioName, MedianPx, Runs.Num(), State.ConfiguredNoiseGain, Band.MinPx, Band.MaxPx), Runs.Num() > 0 && MedianPx >= Band.MinPx && MedianPx <= Band.MaxPx);
		}
		State.Summary->SetObjectField(TEXT("median_peak_shake_px"), MedianJson);

		float MaxShakePx = 0.f;
		FString MaxShakeRunName = TEXT("none");
		for (const FTDHitFeelPieRun& Run : State.Runs)
		{
			if (!IsHitFeelPieRunUsable(Run) || Run.Phase == TDHitFeelPieCalibrationPhase)
			{
				continue;
			}
			if (Run.BaselineShakePx > TDHitFeelPieBaselineShakePxWarning)
			{
				Test.AddWarning(FString::Printf(TEXT("Run %d had %.2f px camera offset before its hit."), Run.RunIndex, Run.BaselineShakePx));
			}
			if (Run.MaxShakePx <= MaxShakePx)
			{
				continue;
			}
			MaxShakePx = Run.MaxShakePx;
			MaxShakeRunName = FString::Printf(TEXT("%s #%d phase %.3f"), GetHitFeelPieScenarioName(Run.Scenario), Run.RunIndex, Run.NoisePhase);
		}
		State.Summary->SetNumberField(TEXT("max_peak_shake_px"), MaxShakePx);
		Test.TestTrue(*FString::Printf(TEXT("No measured event at any noise phase shakes the camera more than 29 px (max %.2f px in %s)"), MaxShakePx, *MaxShakeRunName), MaxShakePx <= TDHitFeelPieMaxShakePx);
	}

	void VerifyHitFeelPieNegativeControls(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const TArray<const FTDHitFeelPieRun*> ZeroShakeRuns = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::ReceivedZeroShake, nullptr);
		Test.TestTrue(TEXT("Shake scale 0 control has a clean run"), ZeroShakeRuns.Num() > 0);
		for (const FTDHitFeelPieRun* Run : ZeroShakeRuns)
		{
			Test.TestTrue(*FString::Printf(TEXT("Shake scale 0 keeps the camera still including the kick (peak %.3f px, %d kicks, trauma %.3f)"), Run->MaxShakePx, Run->KickDelta, Run->MaxTrauma), Run->MaxShakePx < TDHitFeelPieZeroShakePxLimit);
		}

		for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieQuietScenarios)
		{
			const TCHAR* ScenarioName = GetHitFeelPieScenarioName(Scenario);
			const TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, Scenario, nullptr);
			Test.TestTrue(*FString::Printf(TEXT("%s control has a clean run"), ScenarioName), Runs.Num() > 0);
			for (const FTDHitFeelPieRun* Run : Runs)
			{
				Test.TestTrue(*FString::Printf(TEXT("%s starts no shake and leaves trauma at 0 (shakes %d, peak trauma %.4f, ratio %.3f)"), ScenarioName, Run->ShakesStartedDelta, Run->MaxTrauma, Run->AppliedRatio), Run->ShakesStartedDelta == 0 && Run->MaxTrauma <= TDHitFeelPieQuietTrauma);
			}
		}
	}

	void VerifyHitFeelPieSurrounded(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		for (const FTDHitFeelPieRun* Run : CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::Surrounded, nullptr))
		{
			State.Summary->SetNumberField(TEXT("surrounded_flashes"), Run->FlashDelta);
			State.Summary->SetNumberField(TEXT("surrounded_kicks"), Run->KickDelta);
			Test.TestTrue(*FString::Printf(TEXT("Six monsters hitting every 0.15 s flash the player 1-3 times in 1 s (%d)"), Run->FlashDelta), Run->FlashDelta >= 1 && Run->FlashDelta <= TDHitFeelPieSurroundFlashLimit);
			Test.TestTrue(*FString::Printf(TEXT("Six monsters hitting every 0.15 s kick the camera 1-4 times in 1 s (%d)"), Run->KickDelta), Run->KickDelta >= 1 && Run->KickDelta <= TDHitFeelPieSurroundKickLimit);
		}
	}

	void GetHitFeelPieStopFrameRange(float Seconds, int32& OutMinFrames, int32& OutMaxFrames)
	{
		const double ExactFrames = Seconds / TDHitFeelPieFixedDeltaSeconds;
		OutMinFrames = FMath::FloorToInt32(ExactFrames);
		OutMaxFrames = FMath::CeilToInt32(ExactFrames);
	}

	bool IsHitFeelPieVictimStopExpected(int32 StopFrames, int32 MinFrames, int32 MaxFrames, bool bIsCommitted, bool bIsDangerWindup, bool bIsEliteOrBoss)
	{
		if (bIsCommitted && (bIsDangerWindup || bIsEliteOrBoss))
		{
			return StopFrames == 0;
		}
		if (bIsCommitted)
		{
			return StopFrames <= TDHitFeelPieCommittedMinionStopLimitFrames;
		}
		return StopFrames >= MinFrames && StopFrames <= MaxFrames;
	}

	void VerifyHitFeelPieVictimStops(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieVictimStopScenarios)
		{
			const TCHAR* ScenarioName = GetHitFeelPieScenarioName(Scenario);
			int32 MinFrames = 0;
			int32 MaxFrames = 0;
			GetHitFeelPieStopFrameRange(GetHitFeelPieExpectedVictimStopSeconds(Scenario), MinFrames, MaxFrames);
			int32 CheckedCount = 0;
			int32 MismatchCount = 0;
			int32 LateStartCount = 0;
			FString FrameList;
			for (const FTDHitFeelPieRun* Run : CollectHitFeelPieRuns(State, Scenario, nullptr))
			{
				++CheckedCount;
				MismatchCount += IsHitFeelPieVictimStopExpected(Run->VictimStopFrames, MinFrames, MaxFrames, Run->bIsVictimCommitted, Run->bIsVictimDangerWindup, Run->bIsVictimEliteOrBoss) ? 0 : 1;
				LateStartCount += Run->VictimStopFrames > 0 && Run->VictimStopStartDelayFrames > TDHitFeelPieVictimStopStartLimitFrames ? 1 : 0;
				FrameList += FString::Printf(TEXT("%d%s "), Run->VictimStopFrames, Run->bIsVictimCommitted ? TEXT("c") : TEXT(""));
			}
			Test.TestTrue(*FString::Printf(TEXT("%s monster pose stop lasts %d-%d frames, committed minions <= 5, committed elite/boss/danger 0 (frames: %s)"), ScenarioName, MinFrames, MaxFrames, *FrameList), CheckedCount > 0 && MismatchCount == 0);
			Test.TestEqual(*FString::Printf(TEXT("%s monster pose stop starts within 1 frame of the hit"), ScenarioName), LateStartCount, 0);
		}
	}

	void VerifyHitFeelPiePlayerStops(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const ETDHitFeelPieScenario StoppedScenarios[] = { ETDHitFeelPieScenario::ReceivedMedium, ETDHitFeelPieScenario::ReceivedLarge };
		const int32 ExpectedFrames = ComputeHitFeelPieFrames(TDHitFeelPiePlayerStopSeconds);
		for (const ETDHitFeelPieScenario Scenario : StoppedScenarios)
		{
			const TCHAR* ScenarioName = GetHitFeelPieScenarioName(Scenario);
			const TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, Scenario, nullptr);
			int32 MismatchCount = 0;
			FString FrameList;
			for (const FTDHitFeelPieRun* Run : Runs)
			{
				MismatchCount += Run->VictimStopFrames == ExpectedFrames || Run->VictimStopFrames == ExpectedFrames + TDHitFeelPieFrameTolerance ? 0 : 1;
				FrameList += FString::Printf(TEXT("%d "), Run->VictimStopFrames);
			}
			Test.TestTrue(*FString::Printf(TEXT("%s stops the player %d-%d frames (frames: %s)"), ScenarioName, ExpectedFrames, ExpectedFrames + 1, *FrameList), Runs.Num() > 0 && MismatchCount == 0);
		}

		const TArray<const FTDHitFeelPieRun*> SmallRuns = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::ReceivedSmall, nullptr);
		int32 StoppedSmallRunCount = 0;
		for (const FTDHitFeelPieRun* Run : SmallRuns)
		{
			StoppedSmallRunCount += Run->VictimStopFrames == 0 ? 0 : 1;
		}
		Test.TestTrue(*FString::Printf(TEXT("A 5%% hit does not stop the player (%d of %d runs stopped)"), StoppedSmallRunCount, SmallRuns.Num()), SmallRuns.Num() > 0 && StoppedSmallRunCount == 0);
	}

	void VerifyHitFeelPieKillPose(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const int32 ExpectedFrames = ComputeHitFeelPieFrames(TDHitFeelPieKillPoseHoldSeconds);
		for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieKillScenarios)
		{
			const TCHAR* ScenarioName = GetHitFeelPieScenarioName(Scenario);
			const TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, Scenario, nullptr);
			Test.TestTrue(*FString::Printf(TEXT("%s has a clean run for the kill pose hold"), ScenarioName), Runs.Num() > 0);
			for (const FTDHitFeelPieRun* Run : Runs)
			{
				const int32 DeathDelayFrames = Run->DeathStartFrame == INDEX_NONE ? INDEX_NONE : Run->DeathStartFrame - GetHitFeelPieEventFrame(*Run);
				Test.TestTrue(*FString::Printf(TEXT("%s run %d holds the kill pose %d frames (expected %d or %d)"), ScenarioName, Run->RunIndex, Run->KillPoseHoldFrames, ExpectedFrames, ExpectedFrames + 1), Run->KillPoseHoldFrames == ExpectedFrames || Run->KillPoseHoldFrames == ExpectedFrames + TDHitFeelPieFrameTolerance);
				Test.TestTrue(*FString::Printf(TEXT("%s run %d starts the death clip or ragdoll %d frames after the kill (>= %d)"), ScenarioName, Run->RunIndex, DeathDelayFrames, TDHitFeelPieKillDeathDelayMinimumFrames), DeathDelayFrames >= TDHitFeelPieKillDeathDelayMinimumFrames);
			}
		}
	}

	bool IsHitFeelPieSpeedNear(float Speed, float ExpectedSpeed)
	{
		return Speed >= ExpectedSpeed * (1.f - TDHitFeelPieKnockbackTolerance) && Speed <= ExpectedSpeed * (1.f + TDHitFeelPieKnockbackTolerance);
	}

	void VerifyHitFeelPieKnockback(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const TArray<const FTDHitFeelPieRun*> ComboRuns = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::Combo, nullptr);
		if (Test.TestTrue(TEXT("Combo knockback samples come from a clean four-stage run"), ComboRuns.Num() > 0 && ComboRuns[0]->ComboStages.Num() >= TDHitFeelPieComboStageCount))
		{
			const FTDHitFeelPieComboStage& FirstStage = ComboRuns[0]->ComboStages[0];
			const FTDHitFeelPieComboStage& FinisherStage = ComboRuns[0]->ComboStages[TDHitFeelPieComboStageCount - 1];
			State.Summary->SetNumberField(TEXT("knockback_stage_one_distance_cm"), FirstStage.KnockbackDistanceCm);
			State.Summary->SetNumberField(TEXT("knockback_finisher_distance_cm"), FinisherStage.KnockbackDistanceCm);
			Test.TestTrue(*FString::Printf(TEXT("Combo stage 1 pushes the monster %.1f cm within 0.5 s after its hit stop (expected 25 +- 6 cm; launch %.0f cm/s)"), FirstStage.KnockbackDistanceCm, FirstStage.KnockbackSpeed), FMath::Abs(FirstStage.KnockbackDistanceCm - TDHitFeelPieKnockbackLightDistanceCm) <= TDHitFeelPieKnockbackLightDistanceToleranceCm);
			Test.TestTrue(*FString::Printf(TEXT("Combo finisher pushes the monster %.1f cm within 0.5 s after its hit stop (expected 85 +- 12 cm; launch %.0f cm/s)"), FinisherStage.KnockbackDistanceCm, FinisherStage.KnockbackSpeed), FMath::Abs(FinisherStage.KnockbackDistanceCm - TDHitFeelPieKnockbackFinisherDistanceCm) <= TDHitFeelPieKnockbackFinisherDistanceToleranceCm);
		}

		const TArray<const FTDHitFeelPieRun*> SpellRuns = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::DealtSpellKnockback, nullptr);
		Test.TestTrue(TEXT("Player spell knockback has a clean run"), SpellRuns.Num() > 0);
		for (const FTDHitFeelPieRun* Run : SpellRuns)
		{
			State.Summary->SetNumberField(TEXT("knockback_spell_speed"), Run->KnockbackSpeed);
			Test.TestTrue(*FString::Printf(TEXT("A player spell keeps the legacy %.0f cm/s push (launch %.0f cm/s, first-frame peak %.0f cm/s, braking %.3f per frame; expected 450 +- 10%%)"), TDHitFeelPieKnockbackDefaultSpeed, Run->KnockbackSpeed, Run->KnockbackPeakSpeed, Run->KnockbackBrakingFactor), IsHitFeelPieSpeedNear(Run->KnockbackSpeed, TDHitFeelPieKnockbackDefaultSpeed));
		}
	}

	void VerifyHitFeelPieApproach(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const TArray<const FTDHitFeelPieRun*> BaselineRuns = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::ApproachBaseline, nullptr);
		if (!Test.TestTrue(TEXT("Approach baseline (40 cm surface gap) has a clean run"), BaselineRuns.Num() > 0 && BaselineRuns[0]->ApproachDisplacementCm >= 0.f))
		{
			return;
		}

		const float BaselineCm = BaselineRuns[0]->ApproachDisplacementCm;
		State.Summary->SetNumberField(TEXT("approach_baseline_cm"), BaselineCm);
		const TArray<const FTDHitFeelPieRun*> StepRuns = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::ApproachStep, nullptr);
		Test.TestTrue(TEXT("Approach step (160 cm surface gap) has a clean run"), StepRuns.Num() > 0);
		for (const FTDHitFeelPieRun* Run : StepRuns)
		{
			const float StepCm = Run->ApproachDisplacementCm - BaselineCm;
			State.Summary->SetNumberField(TEXT("approach_step_cm"), StepCm);
			Test.TestTrue(*FString::Printf(TEXT("A 160 cm surface gap adds %.1f cm of approach in 0.2 s over the root-motion baseline %.1f cm (expected 100 +- 10)"), StepCm, BaselineCm), Run->ApproachDisplacementCm >= 0.f && FMath::Abs(StepCm - TDHitFeelPieApproachExpectedCm) <= TDHitFeelPieApproachToleranceCm);
		}

		const TArray<const FTDHitFeelPieRun*> BossRuns = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::BossApproach, nullptr);
		Test.TestTrue(TEXT("Boss approach (160 cm surface gap) has a clean run"), BossRuns.Num() > 0);
		for (const FTDHitFeelPieRun* Run : BossRuns)
		{
			const float StepCm = Run->ApproachDisplacementCm - BaselineCm;
			State.Summary->SetNumberField(TEXT("boss_approach_step_cm"), StepCm);
			Test.TestTrue(*FString::Printf(TEXT("Approach toward a boss adds at most 40 cm (%.1f cm)"), StepCm), Run->ApproachDisplacementCm >= 0.f && StepCm <= TDHitFeelPieBossApproachLimitCm);
		}
	}

	float GetHitFeelPieExpectedComboVictimSeconds(const FTDHitFeelPieComboStage& Stage, bool bIsFinisher)
	{
		if (Stage.bHadCriticalHit)
		{
			return TDHitFeelPieVictimCriticalStopSeconds;
		}
		return bIsFinisher ? TDHitFeelPieVictimHeavyStopSeconds : TDHitFeelPieVictimLightStopSeconds;
	}

	void VerifyHitFeelPieComboStage(FAutomationTestBase& Test, const FTDHitFeelPieComboStage& Stage, int32 StageOffset)
	{
		const int32 StageNumber = StageOffset + 1;
		const bool bIsFinisher = StageNumber == TDHitFeelPieComboStageCount;
		const float ExpectedRootMotionCm = TDHitFeelPieComboRootMotionCm[StageOffset];
		const int32 ExpectedSlowedFrames = ComputeHitFeelPieFrames(bIsFinisher ? TDHitFeelPieAttackerHeavyStopSeconds : TDHitFeelPieAttackerLightStopSeconds);
		int32 MinVictimFrames = 0;
		int32 MaxVictimFrames = 0;
		GetHitFeelPieStopFrameRange(GetHitFeelPieExpectedComboVictimSeconds(Stage, bIsFinisher), MinVictimFrames, MaxVictimFrames);
		Test.TestTrue(*FString::Printf(TEXT("Combo stage %d hits the pinned target (%d hits)"), StageNumber, Stage.HitCount), Stage.HitCount > 0);
		Test.TestTrue(*FString::Printf(TEXT("Combo stage %d root motion advances %.1f cm (expected %.0f +- 3 cm)"), StageNumber, Stage.RootMotionCm, ExpectedRootMotionCm), FMath::Abs(Stage.RootMotionCm - ExpectedRootMotionCm) <= TDHitFeelPieRootMotionToleranceCm);
		Test.TestEqual(*FString::Printf(TEXT("Combo stage %d keeps UAF active on every slowed frame"), StageNumber), Stage.SlowedFramesWithUafInactive, 0);
		Test.TestTrue(*FString::Printf(TEXT("Combo stage %d action clock runs at 0.05 x dt +- eps for %d frames (expected %d +- 1)"), StageNumber, Stage.SlowedFrames, ExpectedSlowedFrames), FMath::Abs(Stage.SlowedFrames - ExpectedSlowedFrames) <= TDHitFeelPieFrameTolerance);
		Test.TestTrue(*FString::Printf(TEXT("Combo stage %d victim pose stop lasts %d frames (expected %d-%d, committed rules apply)"), StageNumber, Stage.VictimStopFrames, MinVictimFrames, MaxVictimFrames), IsHitFeelPieVictimStopExpected(Stage.VictimStopFrames, MinVictimFrames, MaxVictimFrames, Stage.bWasVictimCommitted, Stage.bWasVictimDangerWindup, false));
		Test.TestTrue(*FString::Printf(TEXT("Combo stage %d victim pose stop starts within 1 frame of the hit (%d)"), StageNumber, Stage.VictimStopStartDelayFrames), Stage.VictimStopFrames <= 0 || Stage.VictimStopStartDelayFrames <= TDHitFeelPieVictimStopStartLimitFrames);
	}

	void VerifyHitFeelPieCombo(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::Combo, nullptr);
		if (!Test.TestTrue(TEXT("Real four-stage combo has a clean fixed-step run"), Runs.Num() > 0))
		{
			return;
		}

		const FTDHitFeelPieRun& Run = *Runs[0];
		const int32 StageCount = FMath::Min(Run.ComboStages.Num(), TDHitFeelPieComboStageCount);
		TSet<int32> DistinctStageValues;
		for (int32 StageOffset = 0; StageOffset < StageCount; ++StageOffset)
		{
			DistinctStageValues.Add(Run.ComboStages[StageOffset].StageIndexValue);
		}
		Test.TestTrue(*FString::Printf(TEXT("ExecutePrimaryAttack drives four distinct combo stages (%d stages, %d distinct, %d commands)"), Run.ComboStages.Num(), DistinctStageValues.Num(), Run.ComboAttackCommands), DistinctStageValues.Num() == TDHitFeelPieComboStageCount);
		for (int32 StageOffset = 0; StageOffset < StageCount; ++StageOffset)
		{
			VerifyHitFeelPieComboStage(Test, Run.ComboStages[StageOffset], StageOffset);
		}
	}

	void VerifyHitFeelPieBossPhase(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const UTDCombatFeedbackSettings* Settings = GetDefault<UTDCombatFeedbackSettings>();
		const TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::BossPhase, nullptr);
		if (!Test.TestTrue(TEXT("Boss phase entry has a clean run"), Runs.Num() > 0))
		{
			return;
		}

		const FTDHitFeelPieRun& Run = *Runs[0];
		const int32 ExpectedSlowFrames = ComputeHitFeelPieFrames(Settings->PhaseSlowMotionSeconds);
		const int32 EntryDelayFrames = Run.PhaseEntryFrame != INDEX_NONE ? Run.PhaseEntryFrame - Run.InjectFrame : INDEX_NONE;
		Test.TestTrue(*FString::Printf(TEXT("Boss crosses its %.2f health mark and enters the phase within 60 s (entry %d frames after the hit, %d bar pulses)"), Run.PhaseMark, EntryDelayFrames, Run.PhasePulseDelta), Run.PhaseEntryFrame != INDEX_NONE);
		Test.TestTrue(*FString::Printf(TEXT("Phase entry event-channel trauma %.3f at its start frame (sampled %.3f plus one frame of decay) equals %.2f +- 0.01"), Run.EventStartTrauma, Run.PeakEventTrauma, Run.ExpectedTrauma), Run.EventStartTrauma >= 0.f && FMath::Abs(Run.EventStartTrauma - Run.ExpectedTrauma) <= TDHitFeelPieTraumaTolerance);
		if (!Settings->IsSlowMotionEnabled())
		{
			Test.AddWarning(TEXT("Feedback slow motion is disabled; the phase entry slow motion checks were skipped."));
			return;
		}
		Test.TestTrue(*FString::Printf(TEXT("Phase entry slow motion scale %.4f equals %.2f +- 0.001"), Run.SlowMotionMinDilation, Settings->PhaseSlowMotionScale), Run.SlowMotionSpan.FrameCount > 0 && FMath::Abs(Run.SlowMotionMinDilation - Settings->PhaseSlowMotionScale) <= TDHitFeelPieSlowMotionScaleTolerance);
		Test.TestTrue(*FString::Printf(TEXT("Phase entry slow motion lasts %d frames (expected %d +- 1)"), Run.SlowMotionSpan.FrameCount, ExpectedSlowFrames), FMath::Abs(Run.SlowMotionSpan.FrameCount - ExpectedSlowFrames) <= TDHitFeelPieFrameTolerance);
	}

	void VerifyHitFeelPieBossCommittedHit(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::BossCommittedHit, nullptr);
		if (!Test.TestTrue(TEXT("Boss committed-attack hit has a clean run"), Runs.Num() > 0))
		{
			return;
		}

		const FTDHitFeelPieRun& Run = *Runs[0];
		if (!Run.bIsVictimCommitted)
		{
			Test.AddWarning(FString::Printf(TEXT("The boss did not enter a committed attack within %d frames; the boss pose-stop skip was not exercised."), TDHitFeelPieBossCommittedWaitLimitFrames));
			return;
		}
		Test.TestEqual(*FString::Printf(TEXT("A boss hit during its committed attack gets no pose stop (waited %d frames)"), Run.CommittedWaitFrames), Run.VictimStopFrames, 0);
	}

	void VerifyHitFeelPieBossKill(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		const UTDCombatFeedbackSettings* Settings = GetDefault<UTDCombatFeedbackSettings>();
		const TArray<const FTDHitFeelPieRun*> Runs = CollectHitFeelPieRuns(State, ETDHitFeelPieScenario::BossKill, nullptr);
		if (!Test.TestTrue(TEXT("Boss kill has a clean fixed-step run"), Runs.Num() > 0))
		{
			return;
		}

		const FTDHitFeelPieRun& Run = *Runs[0];
		const double AttackerStopSeconds = Run.AttackerStopSpan.GetSeconds();
		const double ShakeSeconds = Run.ShakeSpan.GetSeconds();
		const double SlowMotionSeconds = Run.SlowMotionSpan.GetSeconds();
		State.Summary->SetNumberField(TEXT("boss_attacker_stop_real_seconds"), AttackerStopSeconds);
		State.Summary->SetNumberField(TEXT("boss_shake_real_seconds"), ShakeSeconds);
		State.Summary->SetNumberField(TEXT("boss_slow_motion_real_seconds"), SlowMotionSeconds);
		Test.TestTrue(TEXT("Boss kill victim is a boss monster"), Run.bIsBossVictim);
		Test.TestTrue(*FString::Printf(TEXT("Boss kill stops the attacker (%d frames)"), Run.AttackerStopSpan.FrameCount), Run.AttackerStopSpan.FrameCount > 0);
		Test.TestTrue(*FString::Printf(TEXT("Boss kill attacker stop lasts <= 0.06 s of real time (%d frames = %.3f s)"), Run.AttackerStopSpan.FrameCount, AttackerStopSeconds), AttackerStopSeconds <= TDHitFeelPieBossAttackerStopLimitSeconds);
		Test.TestTrue(*FString::Printf(TEXT("Boss kill shake lasts > 0 and <= 0.5 s of real time (%d frames = %.3f s)"), Run.ShakeSpan.FrameCount, ShakeSeconds), Run.ShakeSpan.FrameCount > 0 && ShakeSeconds <= TDHitFeelPieBossShakeLimitSeconds);
		Test.TestTrue(*FString::Printf(TEXT("Boss kill slow motion lasts <= 1.0 s of real time (%d frames = %.3f s)"), Run.SlowMotionSpan.FrameCount, SlowMotionSeconds), SlowMotionSeconds <= TDHitFeelPieBossSlowMotionLimitSeconds);
		Test.TestTrue(*FString::Printf(TEXT("Time dilation is back to 1 within 1.5 s of real time after the boss kill (%.3f)"), Run.DilationAtCheck), Run.DilationAtCheck >= 0.f && FMath::IsNearlyEqual(Run.DilationAtCheck, 1.f, TDHitFeelPieDilationTolerance));
		if (!Settings->IsSlowMotionEnabled())
		{
			Test.AddWarning(TEXT("Feedback slow motion is disabled; the boss kill slow motion checks were skipped."));
			return;
		}

		const int32 ExpectedSlowFrames = ComputeHitFeelPieFrames(Settings->BossKillSlowMotionSeconds);
		const int32 ExpectedDelayFrames = ComputeHitFeelPieFrames(Settings->BossKillSlowMotionDelaySeconds);
		const int32 DelayFrames = Run.SlowMotionFirstFrame != INDEX_NONE && Run.AttackerStopLastFrame != INDEX_NONE ? Run.SlowMotionFirstFrame - Run.AttackerStopLastFrame : INDEX_NONE;
		Test.TestTrue(*FString::Printf(TEXT("Boss kill slow motion scale %.4f equals %.2f +- 0.001"), Run.SlowMotionMinDilation, Settings->BossKillSlowMotionScale), Run.SlowMotionSpan.FrameCount > 0 && FMath::Abs(Run.SlowMotionMinDilation - Settings->BossKillSlowMotionScale) <= TDHitFeelPieSlowMotionScaleTolerance);
		Test.TestTrue(*FString::Printf(TEXT("Boss kill slow motion lasts %d frames (expected %d +- 1)"), Run.SlowMotionSpan.FrameCount, ExpectedSlowFrames), FMath::Abs(Run.SlowMotionSpan.FrameCount - ExpectedSlowFrames) <= TDHitFeelPieFrameTolerance);
		Test.TestTrue(*FString::Printf(TEXT("Boss kill slow motion starts %d frames after the kill stop ends (expected %d +- 1)"), DelayFrames, ExpectedDelayFrames), DelayFrames != INDEX_NONE && FMath::Abs(DelayFrames - ExpectedDelayFrames) <= TDHitFeelPieFrameTolerance);
	}

	void VerifyHitFeelPie(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		VerifyHitFeelPieFixedStep(Test, State);
		VerifyHitFeelPieNoiseGain(Test, State);
		VerifyHitFeelPieTraumaDecay(Test, State);
		VerifyHitFeelPieShakeStarts(Test, State);
		VerifyHitFeelPieStartTrauma(Test, State);
		VerifyHitFeelPieIntensity(Test, State);
		VerifyHitFeelPiePixels(Test, State);
		VerifyHitFeelPieArena(Test, State);
		VerifyHitFeelPieNegativeControls(Test, State);
		VerifyHitFeelPieSurrounded(Test, State);
		VerifyHitFeelPieVictimStops(Test, State);
		VerifyHitFeelPiePlayerStops(Test, State);
		VerifyHitFeelPieKillPose(Test, State);
		VerifyHitFeelPieKnockback(Test, State);
		VerifyHitFeelPieApproach(Test, State);
		VerifyHitFeelPieCombo(Test, State);
		VerifyHitFeelPieBossPhase(Test, State);
		VerifyHitFeelPieBossCommittedHit(Test, State);
		VerifyHitFeelPieBossKill(Test, State);
	}

	FString DescribeHitFeelPieReadiness()
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		const APlayerController* FirstController = World ? World->GetFirstPlayerController() : nullptr;
		const UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
		return FString::Printf(TEXT("PIE world %s, controller %s, pawn %s, feedback subsystem %s (listening %s, assets %s)."),
			World ? TEXT("yes") : TEXT("no"),
			*GetNameSafe(FirstController),
			FirstController ? *GetNameSafe(FirstController->GetPawn()) : TEXT("None"),
			Feedback ? TEXT("yes") : TEXT("no"),
			Feedback && Feedback->IsListening() ? TEXT("yes") : TEXT("no"),
			Feedback && Feedback->AreFeedbackAssetsLoaded() ? TEXT("loaded") : TEXT("pending"));
	}

	bool TryBindHitFeelPieActors(FTDHitFeelPieState& State)
	{
		UWorld* World = TDPieTestUtils::GetPieWorld();
		ATDGamePlayerController* Controller = World ? Cast<ATDGamePlayerController>(World->GetFirstPlayerController()) : nullptr;
		ATDGameCharacter* Player = Controller ? Cast<ATDGameCharacter>(Controller->GetPawn()) : nullptr;
		const UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		UTDCombatFeedbackSubsystem* Feedback = World ? World->GetSubsystem<UTDCombatFeedbackSubsystem>() : nullptr;
		if (!PlayerCombat || !PlayerCombat->IsAlive() || !Feedback || !Feedback->IsListening() || !Feedback->AreFeedbackAssetsLoaded())
		{
			return false;
		}

		State.Controller = Controller;
		State.Player = Player;
		State.Feedback = Feedback;
		return true;
	}

	void WriteHitFeelPieSetup(FTDHitFeelPieState& State, const ATDGameCharacter& Player, const UTDCombatFeedbackSettings& Settings)
	{
		const TSharedRef<FJsonObject> Setup = MakeShared<FJsonObject>();
		const UCameraComponent* Camera = Player.GetTopDownCameraComponent();
		int32 ViewportWidth = 0;
		int32 ViewportHeight = 0;
		if (const ATDGamePlayerController* Controller = State.Controller.Get())
		{
			Controller->GetViewportSize(ViewportWidth, ViewportHeight);
		}
		Setup->SetStringField(TEXT("map"), TDHitFeelPieMapPackagePath);
		Setup->SetStringField(TEXT("player_class"), GetNameSafe(Player.GetClass()));
		Setup->SetNumberField(TEXT("arm_length_cm"), State.OriginalArmLengthCm);
		Setup->SetNumberField(TEXT("camera_fov_degrees"), Camera ? Camera->FieldOfView : 0.f);
		Setup->SetNumberField(TEXT("viewport_width"), ViewportWidth);
		Setup->SetNumberField(TEXT("viewport_height"), ViewportHeight);
		Setup->SetNumberField(TEXT("reference_width_px"), TDHitFeelPieReferenceWidthPx);
		Setup->SetNumberField(TEXT("configured_noise_gain"), Settings.ShakeNoiseGain);
		Setup->SetBoolField(TEXT("configured_randomize_shake_phase"), Settings.bRandomizeShakePhase);
		Setup->SetNumberField(TEXT("configured_camera_shake_scale"), Settings.CameraShakeScale);
		Setup->SetNumberField(TEXT("configured_light_hit_stop_seconds"), Settings.LightHitStopSeconds);
		Setup->SetNumberField(TEXT("configured_heavy_hit_stop_seconds"), Settings.HeavyHitStopSeconds);
		Setup->SetBoolField(TEXT("uaf_component_found"), State.PlayerUafComponent.IsValid());
		Setup->SetBoolField(TEXT("vignette_property_found"), State.VignetteProperty != nullptr);
		Setup->SetBoolField(TEXT("fixed_step_leak_recovered"), State.bWasLeakRecovered);
		Setup->SetNumberField(TEXT("noise_phase_step"), TDHitFeelPieNoisePhaseStep);
		Setup->SetNumberField(TEXT("noise_phase_period"), TDHitFeelPieNoisePhasePeriod);
		Setup->SetNumberField(TEXT("trauma_decay_per_second"), Settings.ShakeTraumaDecayPerSecond);
		TArray<TSharedPtr<FJsonValue>> CandidateValues;
		for (const float NoiseGain : State.NoiseGainCandidates)
		{
			CandidateValues.Add(MakeShared<FJsonValueNumber>(NoiseGain));
		}
		Setup->SetArrayField(TEXT("noise_gain_candidates"), CandidateValues);

		const TSharedRef<FJsonObject> ExpectedTrauma = MakeShared<FJsonObject>();
		ExpectedTrauma->SetNumberField(TEXT("dealt_light"), UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, 0, ETDKillRank::None));
		ExpectedTrauma->SetNumberField(TEXT("dealt_heavy"), UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Heavy, 0, ETDKillRank::None));
		ExpectedTrauma->SetNumberField(TEXT("kill_normal"), UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Heavy, 1, ETDKillRank::Normal));
		ExpectedTrauma->SetNumberField(TEXT("kill_elite"), UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Heavy, 1, ETDKillRank::Elite));
		ExpectedTrauma->SetNumberField(TEXT("kill_boss"), UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Heavy, 1, ETDKillRank::Boss));
		ExpectedTrauma->SetNumberField(TEXT("received_5_percent"), UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(TDHitFeelPieReceivedSmallRatio, false));
		ExpectedTrauma->SetNumberField(TEXT("received_15_percent"), UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(TDHitFeelPieReceivedMediumRatio, false));
		ExpectedTrauma->SetNumberField(TEXT("received_25_percent"), UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(TDHitFeelPieReceivedLargeRatio, false));
		Setup->SetObjectField(TEXT("expected_trauma"), ExpectedTrauma);
		State.Report->SetObjectField(TEXT("setup"), Setup);
	}

	void PrepareHitFeelPie(FAutomationTestBase& Test, const TSharedRef<FTDHitFeelPieState>& State)
	{
		if (State->bIsAborted)
		{
			return;
		}

		ATDGameCharacter* Player = State->Player.Get();
		UTDCombatComponent* PlayerCombat = Player ? Player->GetCombatComponent() : nullptr;
		if (!Test.TestNotNull(TEXT("Player combat component before the hit feel measurements"), PlayerCombat))
		{
			AbortHitFeelPie(*State, TEXT("prepare"));
			return;
		}

		const UTDCombatFeedbackSettings* Settings = GetDefault<UTDCombatFeedbackSettings>();
		PlayerCombat->SetCombatLevel(TDHitFeelPiePlayerCombatLevel);
		PlayerCombat->AddLooseGameplayTag(TDGameplayTags::State_Invulnerable);
		State->bIsPlayerInvulnerableForTest = true;
		State->PlayerUafComponent = FindHitFeelPieUafComponent(*Player);
		State->VignetteProperty = FindFProperty<FObjectProperty>(UTDCombatOverlayWidget::StaticClass(), TDHitFeelPieVignettePropertyName);
		State->FlashIntensityParameterName = Settings->FlashIntensityParameterName;
		State->VignetteIntensityParameterName = Settings->VignetteIntensityParameterName;
		State->PlayerHomeLocation = Player->GetActorLocation();
		const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
		State->PlayerHomeForward = Forward.IsNearlyZero() ? FVector::ForwardVector : Forward;
		const USpringArmComponent* CameraBoom = Player->GetCameraBoom();
		State->OriginalArmLengthCm = CameraBoom ? CameraBoom->TargetArmLength : 0.f;
		State->ConfiguredNoiseGain = Settings->ShakeNoiseGain;
		State->RecommendedNoiseGain = Settings->ShakeNoiseGain;
		BindHitFeelPiePlayerObserver(State, *PlayerCombat);
		WriteHitFeelPieSetup(*State, *Player, *Settings);

		TArray<FSoftObjectPath> AssetPaths;
		AssetPaths.Add(FSoftObjectPath(TDHitFeelPieGoblinSpeciesPath));
		AssetPaths.Add(FSoftObjectPath(TDHitFeelPieBossSpeciesPath));
		State->AssetHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(AssetPaths, FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority);
	}

	bool IsHitFeelPieAssetLoadFinished(const FTDHitFeelPieState& State)
	{
		return State.bIsAborted || !State.AssetHandle.IsValid() || State.AssetHandle->HasLoadCompleted() || State.AssetHandle->WasCanceled();
	}

	void StartHitFeelPieMeasurements(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		for (int32 MonsterIndex = 0; MonsterIndex < TDHitFeelPieRingMonsterCount; ++MonsterIndex)
		{
			const float YawOffsetDegrees = 360.f * MonsterIndex / TDHitFeelPieRingMonsterCount;
			ATDMonsterCharacter* Monster = SpawnHitFeelPieMonster(State, TDHitFeelPieGoblinSpeciesPath, TDHitFeelPieRingRadiusCm, YawOffsetDegrees);
			if (!Test.TestNotNull(*FString::Printf(TEXT("Measurement goblin %d spawns %.0f cm around the player"), MonsterIndex, TDHitFeelPieRingRadiusCm), Monster))
			{
				AbortHitFeelPie(State, TEXT("spawn_ring"));
				return;
			}
			State.RingMonsters.Add(Monster);
		}

		const TSharedRef<FJsonObject> FixedStepJson = MakeShared<FJsonObject>();
		FixedStepJson->SetBoolField(TEXT("previous_use_fixed_time_step"), FApp::UseFixedTimeStep());
		FixedStepJson->SetNumberField(TEXT("previous_fixed_delta_seconds"), FApp::GetFixedDeltaTime());
		if (!Test.TestTrue(TEXT("HitFeel environment guard applies the fixed 1/60 s step and the fixed shake phase"), State.Environment.Apply()))
		{
			AbortHitFeelPie(State, TEXT("environment_guard"));
			return;
		}
		FixedStepJson->SetNumberField(TEXT("applied_fixed_delta_seconds"), FApp::GetFixedDeltaTime());
		FixedStepJson->SetBoolField(TEXT("leak_recovered_at_start"), State.bWasLeakRecovered);
		State.Report->SetObjectField(TEXT("fixed_step"), FixedStepJson);

		FTDHitFeelPieState* StatePointer = &State;
		State.TickHook.Start([StatePointer](UWorld& World, float DeltaSeconds)
		{
			SampleHitFeelPieFrame(*StatePointer, World, DeltaSeconds);
		});
	}

	void DestroyHitFeelPieRingMonsters(FTDHitFeelPieState& State, int32 FirstIndex, int32 EndIndex)
	{
		for (int32 MonsterIndex = FirstIndex; MonsterIndex < EndIndex && MonsterIndex < State.RingMonsters.Num(); ++MonsterIndex)
		{
			DestroyHitFeelPieMonster(State.RingMonsters[MonsterIndex]);
		}
	}

	void SpawnHitFeelPieBoss(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		if (State.bIsAborted)
		{
			return;
		}

		ATDMonsterCharacter* Boss = SpawnHitFeelPieMonster(State, TDHitFeelPieBossSpeciesPath, TDHitFeelPieBossSpawnDistanceCm, 0.f);
		if (!Test.TestNotNull(TEXT("Centaur hunter boss spawns in front of the player"), Boss))
		{
			AbortHitFeelPie(State, TEXT("spawn_boss"));
			return;
		}
		State.BossMonster = Boss;
	}

	void FinishHitFeelPie(FAutomationTestBase& Test, FTDHitFeelPieState& State)
	{
		State.TickHook.Stop();
		RestoreHitFeelPieEnvironment(State);
		UnbindHitFeelPieComboHits(State);
		UnbindHitFeelPiePlayerObserver(State);
		RemoveHitFeelPiePlayerInvulnerability(State);
		Test.TestFalse(TEXT("HitFeel restores the fixed time step and the feedback settings before PIE ends"), GetHitFeelPieEnvironmentRecord().bIsApplied);
		if (!State.bIsAborted)
		{
			VerifyHitFeelPie(Test, State);
		}
		State.Report->SetBoolField(TEXT("completed"), !State.bIsAborted);
		WriteHitFeelPieOutputs(State);
		Test.AddInfo(FString::Printf(TEXT("HitFeel results: %s"), *State.OutputDirectory));
	}

	class FTDHitFeelPieRunCommand : public IAutomationLatentCommand
	{
	public:
		FTDHitFeelPieRunCommand(FAutomationTestBase& InTest, const TSharedRef<FTDHitFeelPieState>& InState, ETDHitFeelPieScenario InScenario, const FString& InPhase, int32 InRepetition)
			: Test(InTest)
			, State(InState)
			, Scenario(InScenario)
			, Phase(InPhase)
			, Repetition(InRepetition)
		{
		}

		virtual bool Update() override
		{
			FTDHitFeelPieState& RunState = *State;
			if (RunState.bIsAborted)
			{
				return true;
			}
			const double RunTimeoutSeconds = Scenario == ETDHitFeelPieScenario::BossPhase ? TDHitFeelPiePhaseRunTimeoutSeconds : TDHitFeelPieRunTimeoutSeconds;
			if (GetCurrentRunTime() > RunTimeoutSeconds)
			{
				Test.AddError(FString::Printf(TEXT("%s timed out after %.0f s."), *DescribeRun(), RunTimeoutSeconds));
				AbortHitFeelPie(RunState, DescribeRun());
				return true;
			}
			if (RunIndex == INDEX_NONE)
			{
				return BeginRun(RunState);
			}

			FTDHitFeelPieRun& Run = RunState.Runs[RunIndex];
			if (!Run.bIsInjected)
			{
				return TryInject(RunState, Run);
			}
			if (RunState.FrameIndex == LastAdvancedFrame)
			{
				return false;
			}
			LastAdvancedFrame = RunState.FrameIndex;
			if (!AdvanceHitFeelPieRun(RunState, Run))
			{
				return false;
			}
			FinishHitFeelPieRun(RunState, Run);
			return true;
		}

	private:
		FString DescribeRun() const
		{
			return FString::Printf(TEXT("%s %s #%d"), *Phase, GetHitFeelPieScenarioName(Scenario), Repetition);
		}

		bool BeginRun(FTDHitFeelPieState& RunState)
		{
			RunIndex = BeginHitFeelPieRun(RunState, Scenario, Phase, Repetition);
			if (RunIndex == INDEX_NONE)
			{
				Test.AddError(FString::Printf(TEXT("%s could not find its player, feedback subsystem or monsters."), *DescribeRun()));
				AbortHitFeelPie(RunState, DescribeRun());
				return true;
			}
			if (Scenario == ETDHitFeelPieScenario::Combo)
			{
				BindHitFeelPieComboHits(State);
			}
			SettleUntilFrame = RunState.FrameIndex + GetHitFeelPieSettleFrames(Scenario);
			return false;
		}

		bool TryInject(FTDHitFeelPieState& RunState, FTDHitFeelPieRun& Run)
		{
			if (RunState.FrameIndex < SettleUntilFrame)
			{
				return false;
			}
			const bool bIsQuiet = IsHitFeelPieQuiet(RunState);
			if (!bIsQuiet && RunState.FrameIndex < SettleUntilFrame + TDHitFeelPieQuietWaitLimitFrames)
			{
				return false;
			}
			const bool bIsWaitingForCommittedBoss = Scenario == ETDHitFeelPieScenario::BossCommittedHit && !IsHitFeelPieFocusCommitted(RunState);
			if (bIsWaitingForCommittedBoss && RunState.FrameIndex < SettleUntilFrame + TDHitFeelPieBossCommittedWaitLimitFrames)
			{
				return false;
			}
			Run.CommittedWaitFrames = RunState.FrameIndex - SettleUntilFrame;
			if (!InjectHitFeelPieRun(RunState, Run, bIsQuiet))
			{
				Test.AddError(FString::Printf(TEXT("%s lost the player or the feedback subsystem before its hit."), *DescribeRun()));
				AbortHitFeelPie(RunState, DescribeRun());
				return true;
			}
			LastAdvancedFrame = RunState.FrameIndex;
			return false;
		}

		FAutomationTestBase& Test;
		TSharedRef<FTDHitFeelPieState> State;
		ETDHitFeelPieScenario Scenario;
		FString Phase;
		int32 Repetition = 0;
		int32 RunIndex = INDEX_NONE;
		int32 SettleUntilFrame = 0;
		int32 LastAdvancedFrame = INDEX_NONE;
	};

	void EnqueueHitFeelPieRun(FAutomationTestBase& Test, const TSharedRef<FTDHitFeelPieState>& State, ETDHitFeelPieScenario Scenario, const TCHAR* Phase, int32 Repetition)
	{
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FTDHitFeelPieRunCommand(Test, State, Scenario, Phase, Repetition)));
	}

	void EnqueueHitFeelPieFrameWait(FAutomationTestBase& Test, const TSharedRef<FTDHitFeelPieState>& State, int32 FrameCount, const FString& StepName)
	{
		const TSharedRef<int32> TargetFrame = MakeShared<int32>(INDEX_NONE);
		TDPieTestUtils::EnqueueWaitUntil(Test, FString::Printf(TEXT("%d PIE world frames pass (%s)"), FrameCount, *StepName), [State, TargetFrame, FrameCount]()
		{
			if (State->bIsAborted)
			{
				return true;
			}
			if (*TargetFrame == INDEX_NONE)
			{
				*TargetFrame = State->FrameIndex + FrameCount;
			}
			return State->FrameIndex >= *TargetFrame;
		}, TDHitFeelPieRunTimeoutSeconds, [State, StepName]()
		{
			AbortHitFeelPie(*State, StepName);
		});
	}

	void EnqueueHitFeelPieScreenshotWait(FAutomationTestBase& Test, const TSharedRef<FTDHitFeelPieState>& State)
	{
		const TSharedRef<double> StartSeconds = MakeShared<double>(-1.0);
		TDPieTestUtils::EnqueueStep([&Test, State, StartSeconds]()
		{
			if (State->bIsAborted)
			{
				return true;
			}
			if (*StartSeconds < 0.0)
			{
				*StartSeconds = FPlatformTime::Seconds();
			}
			const bool bAreSaved = AreHitFeelPieScreenshotsSaved(*State);
			if (!bAreSaved && FPlatformTime::Seconds() - *StartSeconds < TDHitFeelPieScreenshotTimeoutSeconds)
			{
				return false;
			}
			RecordHitFeelPieScreenshots(*State);
			if (!bAreSaved)
			{
				Test.AddWarning(FString::Printf(TEXT("HitFeel screenshot burst was not fully written within %.0f s; keep the editor in the foreground."), TDHitFeelPieScreenshotTimeoutSeconds));
			}
			return true;
		});
	}

	void AllowHitFeelPieInjectionEnsure(FAutomationTestBase& Test)
	{
		Test.AddExpectedErrorPlain(TDHitFeelPieTimelineEnsureExpression, EAutomationExpectedErrorFlags::Contains, -1);
		Test.AddExpectedError(TDHitFeelPieEnsureHeadingPattern, EAutomationExpectedErrorFlags::Contains, -1, true);
		Test.AddExpectedError(TDHitFeelPieEnsureBlankLinePattern, EAutomationExpectedErrorFlags::Contains, -1, true);
		Test.AddExpectedError(TDHitFeelPieEnsureStackHeadingPattern, EAutomationExpectedErrorFlags::Contains, -1, true);
		Test.AddExpectedError(TDHitFeelPieEnsureCallstackLinePattern, EAutomationExpectedErrorFlags::Contains, -1, true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDCombatHitFeelPieTest, "TDGame.PIE.Combat.HitFeel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDCombatHitFeelPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("TDGame.PIE.Combat.HitFeel needs a rendering editor and was skipped; run it in the GUI editor with check_automation_tests.py --filter TDGame.PIE.Combat.HitFeel."));
		return true;
	}

	const bool bWasLeakRecovered = FTDHitFeelPieEnvironmentGuard::RecoverLeakedEnvironment();
	if (bWasLeakRecovered)
	{
		AddWarning(TEXT("A previous TDGame.PIE.Combat.HitFeel run left the fixed 1/60 s step or the feedback settings applied; they were restored before this run."));
	}
	if (FApp::UseFixedTimeStep())
	{
		AddWarning(TEXT("Fixed time step was already on before HitFeel started; the guard restores that state at the end."));
	}

	AllowHitFeelPieInjectionEnsure(*this);
	TSharedRef<FTDHitFeelPieState> State = MakeShared<FTDHitFeelPieState>();
	State->bWasLeakRecovered = bWasLeakRecovered;
	State->OutputDirectory = MakeHitFeelPieOutputDirectory();
	IFileManager::Get().MakeDirectory(*State->OutputDirectory, true);
	const float ConfiguredNoiseGain = GetDefault<UTDCombatFeedbackSettings>()->ShakeNoiseGain;
	for (const float NoiseGainOffset : TDHitFeelPieNoiseGainOffsets)
	{
		State->NoiseGainCandidates.Add(FMath::Max(TDHitFeelPieMinimumNoiseGain, ConfiguredNoiseGain + NoiseGainOffset));
	}
	const int32 SlowMotionCooldownFrames = ComputeHitFeelPieFrames(GetDefault<UTDCombatFeedbackSettings>()->SlowMotionCooldownSeconds) + TDHitFeelPieSlowMotionCooldownMarginFrames;
	if (!OpenMapAndStartPie(*this, TDHitFeelPieMapPackagePath))
	{
		return false;
	}

	EnqueueWaitUntil(*this, TEXT("PIE player, controller and listening combat feedback subsystem are ready"), [State]()
	{
		return TryBindHitFeelPieActors(*State);
	}, TDHitFeelPieSetupTimeoutSeconds, [this, State]()
	{
		AddError(DescribeHitFeelPieReadiness());
		AbortHitFeelPie(*State, TEXT("bind_actors"));
	});

	EnqueueStep([this, State]()
	{
		PrepareHitFeelPie(*this, State);
		return true;
	});

	EnqueueWaitUntil(*this, TEXT("Goblin and centaur hunter species finish async loading"), [State]()
	{
		return IsHitFeelPieAssetLoadFinished(*State);
	}, TDHitFeelPieAssetLoadTimeoutSeconds, [State]()
	{
		AbortHitFeelPie(*State, TEXT("asset_load"));
	});

	EnqueueStep([this, State]()
	{
		StartHitFeelPieMeasurements(*this, *State);
		return true;
	});

	EnqueueHitFeelPieFrameWait(*this, State, TDHitFeelPieWarmupFrames, TEXT("warmup"));

	for (const float NoiseGain : State->NoiseGainCandidates)
	{
		EnqueueStep([State, NoiseGain]()
		{
			State->Environment.SetNoiseGain(NoiseGain);
			return true;
		});
		for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieCalibrationScenarios)
		{
			for (int32 Repetition = 0; Repetition < TDHitFeelPieCalibrationRepetitions; ++Repetition)
			{
				EnqueueHitFeelPieRun(*this, State, Scenario, TDHitFeelPieCalibrationPhase, Repetition);
			}
		}
	}

	EnqueueStep([this, State]()
	{
		ChooseHitFeelPieNoiseGain(*this, *State);
		return true;
	});

	for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieMeasuredScenarios)
	{
		for (int32 Repetition = 0; Repetition < TDHitFeelPieRepetitions; ++Repetition)
		{
			EnqueueHitFeelPieRun(*this, State, Scenario, TDHitFeelPieMeasurePhase, Repetition);
		}
	}
	for (int32 Repetition = 0; Repetition < TDHitFeelPieCriticalRepetitions; ++Repetition)
	{
		EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::DealtCritical, TDHitFeelPieEventPhase, Repetition);
	}
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::DealtMultiTarget, TDHitFeelPieEventPhase, 0);

	EnqueueStep([State]()
	{
		SetHitFeelPieArmLength(*State, TDHitFeelPieArenaArmLengthCm);
		return true;
	});
	EnqueueHitFeelPieFrameWait(*this, State, TDHitFeelPieArmSettleFrames, TEXT("arena_arm_settle"));
	for (const ETDHitFeelPieScenario Scenario : TDHitFeelPieArenaScenarios)
	{
		for (int32 Repetition = 0; Repetition < TDHitFeelPieRepetitions; ++Repetition)
		{
			EnqueueHitFeelPieRun(*this, State, Scenario, TDHitFeelPieArenaPhase, Repetition);
		}
	}
	EnqueueStep([State]()
	{
		RestoreHitFeelPieArmLength(*State);
		return true;
	});
	EnqueueHitFeelPieFrameWait(*this, State, TDHitFeelPieArmSettleFrames, TEXT("base_arm_settle"));

	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::ReceivedZeroShake, TDHitFeelPieControlPhase, 0);
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::ReceivedStatus, TDHitFeelPieControlPhase, 0);
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::ReceivedTiny, TDHitFeelPieControlPhase, 0);
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::ScreenshotBurst, TDHitFeelPieScreenshotPhase, 0);
	EnqueueHitFeelPieScreenshotWait(*this, State);
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::Surrounded, TDHitFeelPieEventPhase, 0);

	EnqueueStep([State]()
	{
		DestroyHitFeelPieRingMonsters(*State, 1, TDHitFeelPieRingMonsterCount);
		return true;
	});
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::DealtSpellKnockback, TDHitFeelPieEventPhase, 0);
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::ApproachBaseline, TDHitFeelPieEventPhase, 0);
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::ApproachStep, TDHitFeelPieEventPhase, 0);
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::Combo, TDHitFeelPieEventPhase, 0);

	EnqueueStep([State]()
	{
		DestroyHitFeelPieRingMonsters(*State, 0, 1);
		return true;
	});
	EnqueueStep([this, State]()
	{
		SpawnHitFeelPieBoss(*this, *State);
		return true;
	});
	EnqueueHitFeelPieFrameWait(*this, State, TDHitFeelPieBossSettleFrames, TEXT("boss_settle"));
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::BossApproach, TDHitFeelPieEventPhase, 0);
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::BossPhase, TDHitFeelPieEventPhase, 0);
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::BossCommittedHit, TDHitFeelPieEventPhase, 0);
	EnqueueHitFeelPieFrameWait(*this, State, SlowMotionCooldownFrames, TEXT("slow_motion_cooldown"));
	EnqueueHitFeelPieRun(*this, State, ETDHitFeelPieScenario::BossKill, TDHitFeelPieEventPhase, 0);

	EnqueueStep([this, State]()
	{
		FinishHitFeelPie(*this, *State);
		return true;
	});

	EnqueueEndPie();
	return true;
}

#endif
