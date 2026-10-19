#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDCombatCharacter.h"
#include "Characters/TDGameCharacter.h"
#include "Characters/TDMonsterCharacter.h"
#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Combat/Feedback/TDCombatOverlayWidget.h"
#include "Combat/Feedback/TDDeathLaunchRules.h"
#include "Combat/Feedback/TDHitCameraShake.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Framework/TDGameGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraComponentPoolMethodEnum.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "TDGame.h"
#include "TimerManager.h"

namespace
{
	constexpr float CorpseFriction = 1.f;
	constexpr float CorpseRestitution = 0.f;
	constexpr float MaxCorpseLandVolumeScale = 2.f;
	constexpr float MinCorpseLandPitchScale = 0.5f;
	constexpr float MaxCorpseLandPitchScale = 2.f;
	constexpr double MultiKillWindowMarginSeconds = 0.05;

	float ResolveUnclampedDamageRatio(const FTDDamageAppliedMessage& Message)
	{
		if (Message.DamageAmount <= 0.f || Message.UnclampedDamage <= Message.DamageAmount)
		{
			return Message.DamageRatio;
		}
		return Message.DamageRatio * Message.UnclampedDamage / Message.DamageAmount;
	}

	ETDKillRank ResolveKillRank(const FTDDamageAppliedMessage& Message)
	{
		if (!Message.bWasKilled)
		{
			return ETDKillRank::None;
		}
		const ATDMonsterCharacter* Monster = Cast<ATDMonsterCharacter>(Message.Target.Get());
		if (!Monster)
		{
			return ETDKillRank::Normal;
		}
		if (Monster->IsBossMonster())
		{
			return ETDKillRank::Boss;
		}
		return Monster->IsEliteMonster() ? ETDKillRank::Elite : ETDKillRank::Normal;
	}

	float ResolveFlashSeconds(const UTDCombatFeedbackSettings& FeedbackSettings, const FTDHitFeel& HitFeel, const bool bWasCritical, const bool bIsLocalVictim)
	{
		if (bIsLocalVictim)
		{
			return FeedbackSettings.PlayerFlashSeconds;
		}
		if (bWasCritical)
		{
			return FeedbackSettings.CriticalFlashSeconds;
		}
		return HitFeel.Tier == ETDHitFeelTier::Heavy ? FeedbackSettings.HeavyFlashSeconds : FeedbackSettings.LightFlashSeconds;
	}

	float ResolveAttackerStopSeconds(const UTDCombatFeedbackSettings& FeedbackSettings, const FTDHitFeel& HitFeel)
	{
		if (!HitFeel.bIsPlayerMelee)
		{
			return 0.f;
		}
		if (HitFeel.KillRank != ETDKillRank::None)
		{
			return FeedbackSettings.AttackerKillHitStopSeconds;
		}
		return HitFeel.Tier == ETDHitFeelTier::Heavy ? FeedbackSettings.AttackerHeavyHitStopSeconds : FeedbackSettings.AttackerHitStopSeconds;
	}
}

bool UTDCombatFeedbackSubsystem::ShouldEnableFeedback(const bool bCanEverRender, const EWorldType::Type WorldType, const bool bHasGameInstance)
{
	if (!bCanEverRender || !bHasGameInstance)
	{
		return false;
	}
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

ETDHitFeedbackTier UTDCombatFeedbackSubsystem::ClassifyHitTier(const bool bWasKilled, const bool bWasCritical, const float DamageRatio, const float HeavyDamageRatio)
{
	if (bWasKilled)
	{
		return ETDHitFeedbackTier::Kill;
	}
	if (bWasCritical || DamageRatio >= HeavyDamageRatio)
	{
		return ETDHitFeedbackTier::Heavy;
	}
	return ETDHitFeedbackTier::Light;
}

FTDHitPresentation UTDCombatFeedbackSubsystem::DescribeHitPresentation(const FTDDamageAppliedMessage& Message, const float HeavyDamageRatio)
{
	FTDHitPresentation Presentation;
	Presentation.Tier = ClassifyHitTier(Message.bWasKilled, Message.bWasCritical, Message.DamageRatio, HeavyDamageRatio);
	Presentation.bIsStatus = Message.Delivery == ETDDamageDelivery::Status;
	Presentation.NumberAmount = Presentation.Tier == ETDHitFeedbackTier::Kill ? FMath::Max(Message.DamageAmount, Message.UnclampedDamage) : Message.DamageAmount;

	const bool bIsDirectHit = !Presentation.bIsStatus;
	Presentation.bShowsImpactEffects = bIsDirectHit;
	Presentation.bWobblesVictim = bIsDirectHit;
	Presentation.bStopsVictim = bIsDirectHit && Presentation.Tier != ETDHitFeedbackTier::Kill;
	Presentation.bStopsAttacker = bIsDirectHit;
	Presentation.bPlaysCameraFeedback = bIsDirectHit;
	Presentation.bPlaysAccentSounds = bIsDirectHit;
	return Presentation;
}

float UTDCombatFeedbackSubsystem::ComputeDealtTrauma(const ETDHitFeelTier Tier, const int32 KillCountInSwing, const ETDKillRank KillRank)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	if (KillRank == ETDKillRank::None)
	{
		return Tier == ETDHitFeelTier::Heavy ? FeedbackSettings.TraumaDealtHeavy : FeedbackSettings.TraumaDealt;
	}

	const float ExtraKillTrauma = FeedbackSettings.KillExtraTrauma * FMath::Max(KillCountInSwing - 1, 0);
	switch (KillRank)
	{
	case ETDKillRank::Boss:
		return FMath::Min(FeedbackSettings.TraumaKillBoss + ExtraKillTrauma, FMath::Max(FeedbackSettings.BossKillShakeTraumaCap, FeedbackSettings.TraumaKillBoss));
	case ETDKillRank::Elite:
		return FMath::Min(FeedbackSettings.TraumaKillElite + ExtraKillTrauma, FMath::Max(FeedbackSettings.EliteKillTraumaCap, FeedbackSettings.TraumaKillElite));
	default:
		return FMath::Min(FeedbackSettings.TraumaKill + ExtraKillTrauma, FMath::Max(FeedbackSettings.KillTraumaCap, FeedbackSettings.TraumaKill));
	}
}

float UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(const float AccumulatedRatio, const bool bIsDeath)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	if (bIsDeath)
	{
		return FeedbackSettings.TraumaReceivedDeath;
	}
	const float Trauma = FeedbackSettings.TraumaReceivedMin + FeedbackSettings.TraumaReceivedPerDamageRatio * FMath::Max(AccumulatedRatio, 0.f);
	return FMath::Clamp(Trauma, FeedbackSettings.TraumaReceivedMin, FeedbackSettings.TraumaReceivedMax);
}

bool UTDCombatFeedbackSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* OuterWorld = Cast<UWorld>(Outer);
	if (!OuterWorld)
	{
		return false;
	}
	return ShouldEnableFeedback(FApp::CanEverRender(), OuterWorld->WorldType, OuterWorld->GetGameInstance() != nullptr);
}

void UTDCombatFeedbackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RequestFeedbackAssets(*GetDefault<UTDCombatFeedbackSettings>());
}

void UTDCombatFeedbackSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!UGameplayMessageSubsystem::HasInstance(&InWorld))
	{
		return;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	CreateSoundSettings(FeedbackSettings);
	if (!FeedbackAssetsHandle.IsValid() && !bAreFeedbackAssetsLoaded)
	{
		RequestFeedbackAssets(FeedbackSettings);
	}
	RegisterListeners(InWorld);
}

void UTDCombatFeedbackSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	Super::OnWorldEndPlay(InWorld);
	RestoreSlowMotion();
}

void UTDCombatFeedbackSubsystem::Deinitialize()
{
	UnregisterListeners();
	RestoreSlowMotion();
	RestorePresentation();
	if (OverlayWidget)
	{
		OverlayWidget->RemoveFromParent();
	}
	OverlayWidget = nullptr;
	OverlayOwner.Reset();
	if (FeedbackAssetsHandle.IsValid())
	{
		FeedbackAssetsHandle->CancelHandle();
	}
	FeedbackAssetsHandle.Reset();
	Super::Deinitialize();
}

bool UTDCombatFeedbackSubsystem::IsTickable() const
{
	return bIsListening;
}

TStatId UTDCombatFeedbackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDCombatFeedbackSubsystem, STATGROUP_Tickables);
}

void UTDCombatFeedbackSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);

	NumbersThisFrame = 0;
	EffectsThisFrame = 0;
	SoundsThisFrame = 0;
	StatusSoundsThisFrame = 0;
	OverlayChangesThisFrame = 0;

	const double Now = GetFeedbackTime();
	UpdateOverlayOwner();
	DrainDeferredFeedback(Now);
	UpdateHitFlashes(Now);
	UpdateWobbles(Now);
	ApplyPendingAttackerHitStop();
	UpdateSlowMotion();
}

void UTDCombatFeedbackSubsystem::RequestSlowMotion(const float Scale, const float RealSeconds)
{
	RequestSlowMotionWithRule(Scale, RealSeconds, false, true);
}

void UTDCombatFeedbackSubsystem::NotifyRagdollEntryScheduled(const ATDCombatCharacter& Character)
{
	RemoveInvalidRagdollEntryWaiters();
	RagdollEntryWaiters.Add(&Character);
}

void UTDCombatFeedbackSubsystem::NotifyRagdollEntered(const ATDCombatCharacter& Character)
{
	RagdollEntryWaiters.Remove(&Character);
	RaiseLatestRagdollEntry(GetFeedbackRealTime());
}

void UTDCombatFeedbackSubsystem::CancelRagdollEntryWait(const ATDCombatCharacter& Character)
{
	RagdollEntryWaiters.Remove(&Character);
}

void UTDCombatFeedbackSubsystem::RaiseLatestRagdollEntry(const double EntryRealSeconds)
{
	if (LatestRagdollEntryRealTime.IsSet() && EntryRealSeconds <= LatestRagdollEntryRealTime.GetValue())
	{
		return;
	}
	LatestRagdollEntryRealTime = EntryRealSeconds;
}

bool UTDCombatFeedbackSubsystem::HasRagdollAwaitingEntry() const
{
	for (const TWeakObjectPtr<const ATDCombatCharacter>& Waiter : RagdollEntryWaiters)
	{
		if (Waiter.IsValid())
		{
			return true;
		}
	}
	return false;
}

void UTDCombatFeedbackSubsystem::RemoveInvalidRagdollEntryWaiters()
{
	for (auto WaiterIt = RagdollEntryWaiters.CreateIterator(); WaiterIt; ++WaiterIt)
	{
		if (!WaiterIt->IsValid())
		{
			WaiterIt.RemoveCurrent();
		}
	}
}

int32 UTDCombatFeedbackSubsystem::RecordAttackerKill(const AActor& Attacker)
{
	const double RealNow = GetFeedbackRealTime();
	const double WindowSeconds = FMath::Max(GetDefault<UTDCombatFeedbackSettings>()->AttackerKillHitStopSeconds, 0.f) + MultiKillWindowMarginSeconds;
	AttackerKillWindows.RemoveAll([RealNow, WindowSeconds](const FTDAttackerKillWindow& Window)
	{
		return !Window.Attacker.IsValid() || RealNow - Window.LastKillRealTime > WindowSeconds;
	});
	FTDAttackerKillWindow* AttackerWindow = AttackerKillWindows.FindByPredicate([&Attacker](const FTDAttackerKillWindow& Window)
	{
		return Window.Attacker.Get() == &Attacker;
	});
	if (!AttackerWindow)
	{
		AttackerKillWindows.Add(FTDAttackerKillWindow{ &Attacker, RealNow, 1 });
		return 0;
	}
	AttackerWindow->LastKillRealTime = RealNow;
	const int32 KillIndex = AttackerWindow->KillCount;
	++AttackerWindow->KillCount;
	return KillIndex;
}

float UTDCombatFeedbackSubsystem::GetSecondsUntilPendingSlowMotion() const
{
	const double RealNow = GetFeedbackRealTime();
	float SecondsUntilStart = TNumericLimits<float>::Max();
	for (const FTDPendingSlowMotion& PendingSlowMotion : PendingSlowMotions)
	{
		const double StartRealTime = ResolveRagdollSlowMotionStartRealTime(PendingSlowMotion.RequestedStartRealTime);
		SecondsUntilStart = FMath::Min(SecondsUntilStart, static_cast<float>(FMath::Max(StartRealTime - RealNow, 0.)));
	}
	return SecondsUntilStart;
}

UPhysicalMaterial* UTDCombatFeedbackSubsystem::GetCorpsePhysicalMaterial()
{
	if (CorpsePhysicalMaterial)
	{
		return CorpsePhysicalMaterial;
	}

	CorpsePhysicalMaterial = NewObject<UPhysicalMaterial>(this, TEXT("TDCorpsePhysicalMaterial"));
	CorpsePhysicalMaterial->Friction = CorpseFriction;
	CorpsePhysicalMaterial->StaticFriction = CorpseFriction;
	CorpsePhysicalMaterial->FrictionCombineMode = EFrictionCombineMode::Max;
	CorpsePhysicalMaterial->bOverrideFrictionCombineMode = true;
	CorpsePhysicalMaterial->Restitution = CorpseRestitution;
	CorpsePhysicalMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
	CorpsePhysicalMaterial->bOverrideRestitutionCombineMode = true;
	return CorpsePhysicalMaterial;
}

void UTDCombatFeedbackSubsystem::PlayCorpseLanding(const FVector& Location, const float Strength, const float PitchScale, const ETDKillRank KillRank, const float CapsuleHalfHeight)
{
	if (!FMath::IsFinite(Strength) || !FMath::IsFinite(PitchScale) || Location.ContainsNaN())
	{
		return;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const double RealNow = GetFeedbackRealTime();
	AddCorpseLandTrauma(RealNow, Location, KillRank, CapsuleHalfHeight);
	const float VolumeScale = FMath::Min(Strength, MaxCorpseLandVolumeScale);
	if (VolumeScale <= 0.f)
	{
		return;
	}
	if (CorpseLandGroup.LandingCount == 0 || RealNow - CorpseLandGroup.WindowStartRealTime >= FeedbackSettings.CorpseLandGroupWindowSeconds)
	{
		CorpseLandGroup = FTDCorpseLandGroup();
		CorpseLandGroup.WindowStartRealTime = RealNow;
	}

	++CorpseLandGroup.LandingCount;
	if (CorpseLandGroup.LandingCount >= FeedbackSettings.CorpseLandGroupStartCount)
	{
		RaiseFirstCorpseLandSound();
		return;
	}

	const float SafePitchScale = FMath::Clamp(PitchScale, MinCorpseLandPitchScale, MaxCorpseLandPitchScale);
	UAudioComponent* Audio = SpawnFeedbackSound(FeedbackSettings.CorpseLandSound, Location, VolumeScale, SafePitchScale, ETDFeedbackSoundChannel::CorpseLand);
	if (CorpseLandGroup.LandingCount == 1)
	{
		CorpseLandGroup.FirstAudio = Audio;
	}
}

float UTDCombatFeedbackSubsystem::GetCameraTraumaForTest() const
{
	const UTDTraumaCameraShakePattern* ShakePattern = ActiveShakePattern.Get();
	return ShakePattern ? ShakePattern->GetTrauma() : 0.f;
}

FVector2D UTDCombatFeedbackSubsystem::GetLastShakeScreenOffsetForTest() const
{
	const UTDTraumaCameraShakePattern* ShakePattern = ActiveShakePattern.Get();
	return ShakePattern ? ShakePattern->GetLastScreenOffset() : FVector2D::ZeroVector;
}

void UTDCombatFeedbackSubsystem::RegisterListeners(UWorld& InWorld)
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(&InWorld);
	DamageAppliedListener = MessageSubsystem.RegisterListener<FTDDamageAppliedMessage>(TDGameplayTags::Event_Damage_Applied, this, &ThisClass::HandleDamageApplied);
	DamageAvoidedListener = MessageSubsystem.RegisterListener<FTDDamageAvoidedMessage>(TDGameplayTags::Event_Damage_Avoided, this, &ThisClass::HandleDamageAvoided);
	ActorDeathListener = MessageSubsystem.RegisterListener<FTDActorDeathMessage>(TDGameplayTags::Event_Actor_Death, this, &ThisClass::HandleActorDeath);
	MeleeSwingListener = MessageSubsystem.RegisterListener<FTDMeleeSwingMessage>(TDGameplayTags::Event_Combat_MeleeSwing, this, &ThisClass::HandleMeleeSwing);
	EncounterBannerListener = MessageSubsystem.RegisterListener<FTDEncounterBannerMessage>(TDGameplayTags::Event_Encounter_Banner, this, &ThisClass::HandleEncounterBanner);
	BossHealthBarListener = MessageSubsystem.RegisterListener<FTDBossHealthBarMessage>(TDGameplayTags::Event_Encounter_BossHealthBar, this, &ThisClass::HandleBossHealthBar);
	MonsterPhaseListener = MessageSubsystem.RegisterListener<FTDMonsterPhaseMessage>(TDGameplayTags::Event_Monster_PhaseEntered, this, &ThisClass::HandleMonsterPhaseEntered);
	bIsListening = true;
}

void UTDCombatFeedbackSubsystem::UnregisterListeners()
{
	DamageAppliedListener.Unregister();
	DamageAvoidedListener.Unregister();
	ActorDeathListener.Unregister();
	MeleeSwingListener.Unregister();
	EncounterBannerListener.Unregister();
	BossHealthBarListener.Unregister();
	MonsterPhaseListener.Unregister();
	bIsListening = false;
}

void UTDCombatFeedbackSubsystem::CreateSoundSettings(const UTDCombatFeedbackSettings& FeedbackSettings)
{
	ImpactSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.ImpactSoundMaxCount, 0.f);
	AccentSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.AccentSoundMaxCount, 0.f);
	SwingSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.SwingSoundMaxCount, 0.f);
	MonsterSwingSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.MonsterSwingSoundMaxCount, 0.f);
	StatusTickSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.StatusTickSoundMaxCount, 0.f);
	PlayerHitSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.PlayerHitSoundMaxCount, FeedbackSettings.PlayerHitSoundRetriggerSeconds);
	CorpseLandSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.CorpseLandSoundMaxCount, 0.f);

	SoundAttenuation = NewObject<USoundAttenuation>(this);
	SoundAttenuation->Attenuation.bAttenuate = true;
	SoundAttenuation->Attenuation.bSpatialize = true;
	SoundAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	SoundAttenuation->Attenuation.AttenuationShapeExtents = FVector(FeedbackSettings.SoundInnerRadiusCm, 0.f, 0.f);
	SoundAttenuation->Attenuation.FalloffDistance = FeedbackSettings.SoundFalloffDistanceCm;
}

USoundConcurrency* UTDCombatFeedbackSubsystem::GetSoundConcurrency(const ETDFeedbackSoundChannel Channel) const
{
	switch (Channel)
	{
	case ETDFeedbackSoundChannel::Accent:
		return AccentSoundConcurrency.Get();
	case ETDFeedbackSoundChannel::Swing:
		return SwingSoundConcurrency.Get();
	case ETDFeedbackSoundChannel::MonsterSwing:
		return MonsterSwingSoundConcurrency.Get();
	case ETDFeedbackSoundChannel::PlayerHit:
		return PlayerHitSoundConcurrency.Get();
	case ETDFeedbackSoundChannel::StatusTick:
		return StatusTickSoundConcurrency.Get();
	case ETDFeedbackSoundChannel::CorpseLand:
		return CorpseLandSoundConcurrency.Get();
	default:
		return ImpactSoundConcurrency.Get();
	}
}

USoundConcurrency* UTDCombatFeedbackSubsystem::CreateSoundConcurrency(const int32 MaxCount, const float RetriggerSeconds)
{
	USoundConcurrency* SoundConcurrency = NewObject<USoundConcurrency>(this);
	SoundConcurrency->Concurrency.MaxCount = FMath::Max(1, MaxCount);
	SoundConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
	SoundConcurrency->Concurrency.RetriggerTime = FMath::Max(RetriggerSeconds, 0.f);
	return SoundConcurrency;
}

void UTDCombatFeedbackSubsystem::RequestFeedbackAssets(const UTDCombatFeedbackSettings& FeedbackSettings)
{
	TArray<FSoftObjectPath> RequestedPaths;
	FeedbackSettings.CollectSoftObjectPaths(RequestedPaths);
	RequestedPaths.RemoveAll([](const FSoftObjectPath& Path)
	{
		return !FPackageName::DoesPackageExist(Path.GetLongPackageName());
	});
	if (RequestedPaths.IsEmpty())
	{
		bAreFeedbackAssetsLoaded = true;
		return;
	}

	FeedbackAssetsHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		MoveTemp(RequestedPaths), FStreamableDelegate::CreateUObject(this, &ThisClass::HandleFeedbackAssetsLoaded));
	if (!FeedbackAssetsHandle.IsValid())
	{
		HandleFeedbackAssetsLoaded();
	}
}

void UTDCombatFeedbackSubsystem::HandleFeedbackAssetsLoaded()
{
	bAreFeedbackAssetsLoaded = true;
	UE_LOG(LogTDGame, Verbose, TEXT("CombatFeedback: presentation assets finished loading."));
}

void UTDCombatFeedbackSubsystem::HandleDamageApplied(FGameplayTag Channel, const FTDDamageAppliedMessage& Message)
{
	AActor* Target = Message.Target.Get();
	if (!Target || Message.DamageAmount <= 0.f)
	{
		return;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const FTDHitPresentation Presentation = DescribeHitPresentation(Message, FeedbackSettings.HeavyDamageRatio);
	const FTDHitFeel HitFeel = DescribeHitFeel(Message);
	ATDCombatCharacter* Victim = Cast<ATDCombatCharacter>(Target);
	APlayerController* LocalController = FindLocalPlayerController();
	const APawn* LocalPawn = LocalController ? LocalController->GetPawn() : nullptr;
	const bool bIsLocalVictim = LocalPawn && Target == LocalPawn;
	const bool bIsLocalAttacker = LocalPawn && Message.Instigator.Get() == LocalPawn;
	const FVector Location = ResolveHitLocation(Message);
	const FVector Direction = Message.HitDirection.GetSafeNormal2D();

	ShowHitNumber(Message, Presentation, Location, bIsLocalVictim);
	if (Presentation.bShowsImpactEffects)
	{
		const bool bSpawnedImpactEffect = SpawnImpactEffects(Message, Location, Direction);
		if (!bIsLocalVictim)
		{
			++Stats.MonsterHitsSeen;
			Stats.MonsterHitEffectsSpawned += bSpawnedImpactEffect ? 1 : 0;
		}
	}
	PlayImpactSounds(Message, Presentation, Location, bIsLocalVictim);
	if (Victim)
	{
		StartHitFlash(*Victim, Presentation, HitFeel, Message.bWasCritical, bIsLocalVictim);
		if (Presentation.bWobblesVictim)
		{
			StartWobble(*Victim, Presentation.Tier, Direction);
		}
	}
	ApplyHitStops(Message, Presentation, HitFeel, Victim, bIsLocalVictim);
	if (LocalController && Presentation.bPlaysCameraFeedback)
	{
		if (bIsLocalVictim)
		{
			PlayReceivedCameraFeedback(*LocalController, Message, HitFeel);
		}
		else if (bIsLocalAttacker)
		{
			PlayDealtCameraFeedback(*LocalController, Message, HitFeel);
		}
	}
	if (bIsLocalVictim && OverlayWidget && !Presentation.bIsStatus)
	{
		const float VignetteIntensity = UTDCombatOverlayWidget::ComputeHitVignetteIntensity(Message.DamageRatio, Presentation.bIsStatus, FeedbackSettings);
		OverlayWidget->PulseDamageVignette(VignetteIntensity * FeedbackSettings.GetFlashIntensityScale());
	}
	if (HitFeel.KillRank == ETDKillRank::Boss)
	{
		ScheduleBossKillSlowMotion(bIsLocalAttacker && HitFeel.bIsPlayerMelee ? DealtWindow.ComputeDesiredAttackerStopSeconds() : 0.f);
	}
	if (Victim && !bIsLocalVictim)
	{
		ShowMonsterHealthBar(*Victim, Message);
	}
}

void UTDCombatFeedbackSubsystem::HandleDamageAvoided(FGameplayTag Channel, const FTDDamageAvoidedMessage& Message)
{
	const AActor* Target = Message.Target.Get();
	if (!Target)
	{
		return;
	}

	const APlayerController* LocalController = FindLocalPlayerController();
	const APawn* LocalPawn = LocalController ? LocalController->GetPawn() : nullptr;
	FTDDamageNumberRequest Request;
	Request.WorldLocation = Message.Location.IsNearlyZero() ? Target->GetActorLocation() : Message.Location;
	Request.TargetKey = FObjectKey(Target);
	Request.Style = ETDDamageNumberStyle::Avoided;
	Request.bIsPlayerTarget = LocalPawn && Target == LocalPawn;
	AddDamageNumber(Request);
}

void UTDCombatFeedbackSubsystem::HandleActorDeath(FGameplayTag Channel, const FTDActorDeathMessage& Message)
{
	APawn* DeadPawn = Cast<APawn>(Message.DeadActor.Get());
	const APlayerController* LocalController = FindLocalPlayerController();
	if (!DeadPawn || !OverlayWidget || !LocalController || LocalController->GetPawn() != DeadPawn)
	{
		return;
	}

	OverlayWidget->ShowDeathScreen(DeadPawn, ResolveRespawnDelaySeconds());
	++Stats.DeathScreensShown;
}

void UTDCombatFeedbackSubsystem::HandleMeleeSwing(FGameplayTag Channel, const FTDMeleeSwingMessage& Message)
{
	const AActor* Attacker = Message.Attacker.Get();
	if (!Attacker)
	{
		return;
	}

	const APlayerController* LocalController = FindLocalPlayerController();
	const APawn* LocalPawn = LocalController ? LocalController->GetPawn() : nullptr;
	const FVector Location = Message.Location.IsNearlyZero() ? Attacker->GetActorLocation() : Message.Location;
	if (Attacker != LocalPawn)
	{
		PlayMonsterSwingSound(*Attacker, LocalPawn, Location);
		return;
	}

	if (PlayFeedbackSound(GetDefault<UTDCombatFeedbackSettings>()->MeleeSwingSound, Location, 1.f, ETDFeedbackSoundChannel::Swing))
	{
		++Stats.SwingsPlayed;
	}
}

void UTDCombatFeedbackSubsystem::HandleEncounterBanner(FGameplayTag Channel, const FTDEncounterBannerMessage& Message)
{
	if (Message.Style == ETDEncounterBannerStyle::Victory)
	{
		RequestVictorySlowMotion();
	}
	if (!OverlayWidget)
	{
		return;
	}

	OverlayWidget->ShowEncounterBanner(Message.Title, Message.Subtitle, Message.Style, Message.DisplaySeconds);
	++Stats.EncounterBannersShown;
}

void UTDCombatFeedbackSubsystem::HandleBossHealthBar(FGameplayTag Channel, const FTDBossHealthBarMessage& Message)
{
	if (!Message.bIsShown)
	{
		PendingBossHealthBar = FTDPendingBossHealthBar();
		if (OverlayWidget)
		{
			OverlayWidget->HideBossHealthBar();
		}
		return;
	}

	ATDCombatCharacter* Boss = Cast<ATDCombatCharacter>(Message.Boss.Get());
	if (!Boss)
	{
		return;
	}

	PendingBossHealthBar.Boss = Boss;
	PendingBossHealthBar.DisplayName = Message.DisplayName;
	PendingBossHealthBar.PhaseHealthMarks = Message.PhaseHealthMarks;
	if (!OverlayWidget)
	{
		return;
	}

	OverlayWidget->ShowBossHealthBar(Boss, Message.DisplayName, Message.PhaseHealthMarks);
	++Stats.BossHealthBarsShown;
}

void UTDCombatFeedbackSubsystem::HandleMonsterPhaseEntered(FGameplayTag Channel, const FTDMonsterPhaseMessage& Message)
{
	const AActor* Monster = Message.Monster.Get();
	if (!Monster)
	{
		return;
	}

	PlayBossPhaseAccent(*Monster);
	if (!OverlayWidget || OverlayWidget->GetBossHealthBarTarget() != Monster)
	{
		return;
	}

	OverlayWidget->PulseBossPhase(Message.PhaseIndex);
	++Stats.BossPhasePulses;
}

FTDHitFeel UTDCombatFeedbackSubsystem::DescribeHitFeel(const FTDDamageAppliedMessage& Message) const
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const ATDGameCharacter* PlayerAttacker = Cast<ATDGameCharacter>(Message.Instigator.Get());
	const UTDSkillComponent* AttackerSkills = PlayerAttacker ? PlayerAttacker->GetSkillComponent() : nullptr;
	const ATDMonsterCharacter* MonsterAttacker = Cast<ATDMonsterCharacter>(Message.Instigator.Get());

	FTDHitFeel HitFeel;
	HitFeel.bIsPlayerMelee = AttackerSkills && Message.Delivery == ETDDamageDelivery::Melee;
	HitFeel.ComboStageIndex = AttackerSkills ? AttackerSkills->GetActivePrimaryComboStageIndex() : INDEX_NONE;
	const bool bIsComboFinisher = HitFeel.bIsPlayerMelee && AttackerSkills->IsPrimaryComboFinisherActive();
	HitFeel.Tier = bIsComboFinisher || Message.bWasCritical ? ETDHitFeelTier::Heavy : ETDHitFeelTier::Light;
	const bool bIsHeavyForTrauma = HitFeel.Tier == ETDHitFeelTier::Heavy || Message.DamageRatio >= FeedbackSettings.HeavyTraumaDamageRatio;
	HitFeel.TraumaTier = bIsHeavyForTrauma ? ETDHitFeelTier::Heavy : ETDHitFeelTier::Light;
	HitFeel.KillRank = ResolveKillRank(Message);
	HitFeel.UnclampedDamageRatio = ResolveUnclampedDamageRatio(Message);
	HitFeel.bIsStrongSource = MonsterAttacker && (MonsterAttacker->IsBossMonster() || MonsterAttacker->IsEliteMonster());
	return HitFeel;
}

void UTDCombatFeedbackSubsystem::ShowHitNumber(const FTDDamageAppliedMessage& Message, const FTDHitPresentation& Presentation, const FVector& Location, const bool bIsLocalVictim)
{
	FTDDamageNumberRequest Request;
	Request.WorldLocation = Location;
	Request.TargetKey = FObjectKey(Message.Target.Get());
	Request.Amount = Presentation.NumberAmount;
	Request.Element = Message.Element;
	Request.bIsPlayerTarget = bIsLocalVictim;
	Request.bWasCritical = Message.bWasCritical;
	if (Presentation.Tier == ETDHitFeedbackTier::Kill && !bIsLocalVictim)
	{
		Request.Style = ETDDamageNumberStyle::Kill;
	}
	else if (Presentation.bIsStatus)
	{
		Request.Style = ETDDamageNumberStyle::Status;
	}
	else
	{
		Request.Style = bIsLocalVictim ? ETDDamageNumberStyle::PlayerDamage : (Message.bWasCritical ? ETDDamageNumberStyle::Critical : ETDDamageNumberStyle::Normal);
	}
	AddDamageNumber(Request);
}

bool UTDCombatFeedbackSubsystem::SpawnImpactEffects(const FTDDamageAppliedMessage& Message, const FVector& Location, const FVector& Direction)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const FRotator Rotation = Direction.IsNearlyZero() ? FRotator::ZeroRotator : Direction.Rotation();
	TArray<const TSoftObjectPtr<UNiagaraSystem>*, TInlineAllocator<2>> RequestedEffects;
	if (const TSoftObjectPtr<UNiagaraSystem>* ImpactEffect = FeedbackSettings.ImpactEffects.Find(Message.Element))
	{
		RequestedEffects.Add(ImpactEffect);
	}
	if (Message.bWasCritical)
	{
		RequestedEffects.Add(&FeedbackSettings.CriticalImpactEffect);
	}

	bool bSpawnedAnyEffect = false;
	for (const TSoftObjectPtr<UNiagaraSystem>* RequestedEffect : RequestedEffects)
	{
		if (RequestedEffect->IsNull())
		{
			continue;
		}
		++Stats.EffectsRequested;

		UNiagaraSystem* System = RequestedEffect->Get();
		if (!System)
		{
			++Stats.EffectsSkippedNotLoaded;
			UE_LOG(LogTDGame, Log, TEXT("CombatFeedback: impact effect %s is not loaded yet."), *RequestedEffect->ToString());
			continue;
		}
		if (EffectsThisFrame >= FeedbackSettings.MaxNewEffectsPerFrame)
		{
			++Stats.EffectsSkippedFrameCap;
			continue;
		}

		++EffectsThisFrame;
		UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Location, Rotation, FVector(1.f), true, true, ENCPoolMethod::AutoRelease, false);
		if (!Effect)
		{
			++Stats.EffectsSpawnFailed;
			UE_LOG(LogTDGame, Warning, TEXT("CombatFeedback: SpawnSystemAtLocation returned null for %s at %s."), *System->GetPathName(), *Location.ToString());
			continue;
		}
		++Stats.EffectsSpawned;
		bSpawnedAnyEffect = true;
		if (System->IsLooping())
		{
			DeactivateLoopingEffectLater(*Effect, FeedbackSettings.LoopingEffectSeconds);
		}
	}
	return bSpawnedAnyEffect;
}

void UTDCombatFeedbackSubsystem::DeactivateLoopingEffectLater(UNiagaraComponent& Effect, const float DelaySeconds)
{
	UWorld* CurrentWorld = GetWorld();
	if (!CurrentWorld)
	{
		return;
	}
	if (DelaySeconds <= 0.f)
	{
		Effect.Deactivate();
		return;
	}

	const TWeakObjectPtr<UNiagaraComponent> WeakEffect(&Effect);
	FTimerHandle DeactivateHandle;
	CurrentWorld->GetTimerManager().SetTimer(DeactivateHandle, FTimerDelegate::CreateWeakLambda(this, [WeakEffect]()
	{
		if (UNiagaraComponent* LoopingEffect = WeakEffect.Get())
		{
			LoopingEffect->Deactivate();
		}
	}), DelaySeconds, false);
}

void UTDCombatFeedbackSubsystem::PlayImpactSounds(const FTDDamageAppliedMessage& Message, const FTDHitPresentation& Presentation, const FVector& Location, const bool bIsLocalVictim)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	if (Presentation.bIsStatus)
	{
		PlayFeedbackSound(FeedbackSettings.StatusTickSounds, Location, FeedbackSettings.StatusSoundVolumeScale, ETDFeedbackSoundChannel::StatusTick);
		return;
	}
	if (bIsLocalVictim)
	{
		PlayFeedbackSound(FeedbackSettings.PlayerHitSounds, Location, 1.f, ETDFeedbackSoundChannel::PlayerHit);
	}
	else if (const FTDFeedbackSoundSet* ImpactSound = FeedbackSettings.ImpactSounds.Find(Message.Element))
	{
		PlayFeedbackSound(*ImpactSound, Location, 1.f, ETDFeedbackSoundChannel::Impact);
	}
	if (!Presentation.bPlaysAccentSounds)
	{
		return;
	}

	if (Message.bWasCritical)
	{
		PlayFeedbackSound(FeedbackSettings.CriticalSound, Location, 1.f, ETDFeedbackSoundChannel::Accent);
	}
	if (Presentation.Tier == ETDHitFeedbackTier::Kill)
	{
		PlayFeedbackSound(FeedbackSettings.KillSound, Location, 1.f, ETDFeedbackSoundChannel::Accent);
	}
	if (bIsLocalVictim)
	{
		PlayFeedbackSound(FeedbackSettings.PlayerHurtSound, Location, 1.f, ETDFeedbackSoundChannel::Accent);
	}
}

void UTDCombatFeedbackSubsystem::PlayMonsterSwingSound(const AActor& Attacker, const APawn* LocalPawn, const FVector& Location)
{
	const ATDMonsterCharacter* Monster = Cast<ATDMonsterCharacter>(&Attacker);
	if (!Monster || !LocalPawn || Monster->GetCurrentTarget() != LocalPawn)
	{
		return;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	if (FVector::DistSquared(Monster->GetActorLocation(), LocalPawn->GetActorLocation()) > FMath::Square(FeedbackSettings.MonsterSwingSoundRadiusCm))
	{
		return;
	}
	PlayFeedbackSound(FeedbackSettings.MonsterSwingSounds, Location, 1.f, ETDFeedbackSoundChannel::MonsterSwing);
}

void UTDCombatFeedbackSubsystem::PlayBossPhaseAccent(const AActor& Monster)
{
	const ATDMonsterCharacter* BossMonster = Cast<ATDMonsterCharacter>(&Monster);
	if (!BossMonster || !BossMonster->IsBossMonster())
	{
		return;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	if (APlayerController* LocalController = FindLocalPlayerController())
	{
		AddCameraTrauma(*LocalController, ETDShakeChannel::Event, FeedbackSettings.PhaseEventTrauma, FeedbackSettings.PhaseEventTrauma, false, FRotator::ZeroRotator);
	}
	PlayFeedbackSound(FeedbackSettings.PhaseAccentSound, Monster.GetActorLocation(), 1.f, ETDFeedbackSoundChannel::Accent);
	RequestSlowMotionWithRule(FeedbackSettings.PhaseSlowMotionScale, FeedbackSettings.PhaseSlowMotionSeconds, false, false);
}

void UTDCombatFeedbackSubsystem::StartHitFlash(ATDCombatCharacter& Victim, const FTDHitPresentation& Presentation, const FTDHitFeel& HitFeel, const bool bWasCritical, const bool bIsLocalVictim)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float PeakIntensity = (Presentation.bIsStatus ? FeedbackSettings.StatusFlashIntensity : 1.f) * FeedbackSettings.GetFlashIntensityScale();
	const float DurationSeconds = ResolveFlashSeconds(FeedbackSettings, HitFeel, bWasCritical, bIsLocalVictim);
	if (PeakIntensity <= 0.f || DurationSeconds <= 0.f || FeedbackSettings.HitFlashMaterial.IsNull())
	{
		return;
	}

	FTDHitFlashRequest Request;
	Request.Victim = &Victim;
	Request.Color = bIsLocalVictim ? FeedbackSettings.PlayerFlashColor : (bWasCritical ? FeedbackSettings.CriticalFlashColor : FeedbackSettings.MonsterFlashColor);
	Request.PeakIntensity = PeakIntensity;
	Request.DurationSeconds = DurationSeconds;
	Request.bIsLocalVictim = bIsLocalVictim;
	if (TryStartHitFlash(Request))
	{
		return;
	}
	DeferredFlashes.Push(Request, GetFeedbackTime());
}

bool UTDCombatFeedbackSubsystem::TryStartHitFlash(const FTDHitFlashRequest& Request)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	ATDCombatCharacter* Victim = Request.Victim.Get();
	const USkeletalMeshComponent* VictimMesh = Victim ? Victim->GetMesh() : nullptr;
	UMaterialInterface* FlashMaterial = FeedbackSettings.HitFlashMaterial.Get();
	if (!VictimMesh || !FlashMaterial)
	{
		return true;
	}

	FTDHitFlashState* FlashState = FindHitFlash(*Victim);
	if (Request.bIsLocalVictim && !PlayerFlashLimiter.CanStart(GetFeedbackRealTime()))
	{
		if (FlashState && FlashState->bIsOverlayApplied)
		{
			FlashState->PeakIntensity = FMath::Max(FlashState->PeakIntensity, Request.PeakIntensity);
		}
		return true;
	}
	if (FlashState && FlashState->bIsOverlayApplied)
	{
		RefreshHitFlash(*FlashState, Request);
		return true;
	}
	if (OverlayChangesThisFrame >= FeedbackSettings.MaxOverlayChangesPerFrame)
	{
		return false;
	}

	if (!FlashState)
	{
		UMaterialInstanceDynamic* FlashInstance = UMaterialInstanceDynamic::Create(FlashMaterial, this);
		if (!FlashInstance)
		{
			return true;
		}
		FlashState = &HitFlashes.AddDefaulted_GetRef();
		FlashState->Character = Victim;
		FlashState->Material = FlashInstance;
	}

	UMaterialInterface* CurrentOverlay = VictimMesh->GetOverlayMaterial();
	FlashState->PreviousOverlay = CurrentOverlay == FlashState->Material.Get() ? nullptr : CurrentOverlay;
	Victim->SetHitFlashOverlay(FlashState->Material.Get());
	FlashState->bIsOverlayApplied = true;
	++OverlayChangesThisFrame;
	RefreshHitFlash(*FlashState, Request);
	return true;
}

void UTDCombatFeedbackSubsystem::RefreshHitFlash(FTDHitFlashState& FlashState, const FTDHitFlashRequest& Request)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	FlashState.Material->SetVectorParameterValue(FeedbackSettings.FlashColorParameterName, Request.Color);
	FlashState.Material->SetScalarParameterValue(FeedbackSettings.FlashIntensityParameterName, Request.PeakIntensity);
	FlashState.StartTime = GetFeedbackTime();
	if (Request.bIsLocalVictim)
	{
		PlayerFlashLimiter.MarkStarted(GetFeedbackRealTime());
	}
	FlashState.PeakIntensity = Request.PeakIntensity;
	FlashState.DurationSeconds = Request.DurationSeconds;
	FlashState.bIsFading = true;
	++Stats.FlashesStarted;
}

void UTDCombatFeedbackSubsystem::StartWobble(ATDCombatCharacter& Victim, const ETDHitFeedbackTier Tier, const FVector& Direction)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float Amplitude = Tier == ETDHitFeedbackTier::Light ? FeedbackSettings.WobbleLightCm : FeedbackSettings.WobbleHeavyCm;
	if (Direction.IsNearlyZero() || Amplitude <= 0.f || FeedbackSettings.WobbleSeconds <= 0.f || Victim.IsRagdollPendingOrActive())
	{
		return;
	}

	FTDWobbleState* WobbleState = FindWobble(Victim);
	if (!WobbleState)
	{
		WobbleState = &Wobbles.AddDefaulted_GetRef();
		WobbleState->Character = &Victim;
	}
	WobbleState->Direction = Direction;
	WobbleState->Amplitude = Amplitude;
	WobbleState->StartTime = GetFeedbackTime();
	++Stats.WobblesStarted;
}

void UTDCombatFeedbackSubsystem::ApplyHitStops(const FTDDamageAppliedMessage& Message, const FTDHitPresentation& Presentation, const FTDHitFeel& HitFeel, ATDCombatCharacter* Victim, const bool bIsLocalVictim)
{
	if (Presentation.bIsStatus || !Victim)
	{
		return;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	if (bIsLocalVictim)
	{
		if (HitFeel.UnclampedDamageRatio < FeedbackSettings.PlayerHitStopDamageRatio && !Message.bWasCritical)
		{
			return;
		}
		if (ATDCombatCharacter* Attacker = Cast<ATDCombatCharacter>(Message.Instigator.Get()))
		{
			StartAttackerPlayRateStop(*Attacker, FeedbackSettings.PlayerHitStopSeconds);
		}
		if (Presentation.bStopsVictim)
		{
			StartVictimPoseStop(*Victim, FeedbackSettings.PlayerHitStopSeconds);
		}
		return;
	}
	if (!Presentation.bStopsVictim)
	{
		return;
	}

	float RealSeconds = HitFeel.Tier == ETDHitFeelTier::Heavy ? FeedbackSettings.HeavyHitStopSeconds : FeedbackSettings.LightHitStopSeconds;
	if (Message.bWasCritical)
	{
		RealSeconds = FMath::Min(RealSeconds + FeedbackSettings.CriticalHitStopBonusSeconds, FeedbackSettings.MaxVictimHitStopSeconds);
	}
	const ATDMonsterCharacter* Monster = Cast<ATDMonsterCharacter>(Victim);
	const bool bIsCommitted = Monster && Monster->IsInCommittedAttack();
	const bool bIsStrongCommitted = bIsCommitted && (Monster->IsBossMonster() || Monster->IsEliteMonster());
	if (bIsStrongCommitted || (Monster && Monster->IsInDangerWindup()))
	{
		StartHitStopJitter(*Victim, RealSeconds, false);
		return;
	}
	if (bIsCommitted)
	{
		RealSeconds = FMath::Min(RealSeconds, FeedbackSettings.CommittedMinionMaxHitStopSeconds);
	}
	StartVictimPoseStop(*Victim, RealSeconds);
}

void UTDCombatFeedbackSubsystem::StartVictimPoseStop(ATDCombatCharacter& Victim, const float RealSeconds)
{
	const UTDCombatComponent* Combat = Victim.GetCombatComponent();
	UTDCharacterAnimationComponent* Animation = Victim.GetCharacterAnimation();
	if (RealSeconds <= 0.f || !Combat || !Combat->IsAlive() || !Animation || !Animation->StartVictimHitStop(RealSeconds))
	{
		return;
	}
	++Stats.HitStopsStarted;
	StartHitStopJitter(Victim, RealSeconds, true);
}

void UTDCombatFeedbackSubsystem::StartAttackerPlayRateStop(ATDCombatCharacter& Attacker, const float RealSeconds)
{
	const UTDCombatComponent* Combat = Attacker.GetCombatComponent();
	UTDCharacterAnimationComponent* Animation = Attacker.GetCharacterAnimation();
	if (RealSeconds <= 0.f || !Combat || !Combat->IsAlive() || !Animation)
	{
		return;
	}
	if (Animation->IsAttackerHitStopActive() && Animation->GetAttackerHitStopRemainingSeconds() >= RealSeconds)
	{
		return;
	}
	if (!Animation->StartAttackerHitStop(RealSeconds))
	{
		return;
	}
	++Stats.HitStopsStarted;
}

void UTDCombatFeedbackSubsystem::StartHitStopJitter(ATDCombatCharacter& Victim, const float RealSeconds, const bool bFollowsHitStop)
{
	FTDWobbleState* WobbleState = FindWobble(Victim);
	if (!WobbleState || RealSeconds <= 0.f)
	{
		return;
	}

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float ArmScale = TDHitCameraShakeMath::ComputeArmLocationScale(ResolveCameraArmLength(FindLocalPlayerController()), FeedbackSettings);
	const float JitterAmplitude = FeedbackSettings.HitStopJitterCm * ArmScale * FeedbackSettings.GetEffectiveShakeScale();
	WobbleState->JitterAmplitude = bFollowsHitStop ? JitterAmplitude : FMath::Min(JitterAmplitude, FeedbackSettings.CommittedJitterMaxCm);
	WobbleState->JitterSeconds = RealSeconds;
	WobbleState->JitterStartRealTime = GetFeedbackRealTime();
	WobbleState->bFollowsHitStop = bFollowsHitStop;
}

void UTDCombatFeedbackSubsystem::ApplyPendingAttackerHitStop()
{
	ATDCombatCharacter* Attacker = SwingAttacker.Get();
	if (!Attacker)
	{
		return;
	}

	const float DesiredSeconds = DealtWindow.ComputeDesiredAttackerStopSeconds();
	if (DesiredSeconds <= DealtWindow.GrantedAttackerStopSeconds + KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double RealNow = GetFeedbackRealTime();
	if (DealtWindow.GrantedAttackerStopSeconds <= 0.f)
	{
		DealtWindow.AttackerStopStartTime = RealNow;
	}
	DealtWindow.GrantedAttackerStopSeconds = DesiredSeconds;
	const float RemainingSeconds = DesiredSeconds - static_cast<float>(RealNow - DealtWindow.AttackerStopStartTime);
	StartAttackerPlayRateStop(*Attacker, RemainingSeconds);
}

void UTDCombatFeedbackSubsystem::PlayReceivedCameraFeedback(APlayerController& Controller, const FTDDamageAppliedMessage& Message, const FTDHitFeel& HitFeel)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const UTDTraumaCameraShakePattern* ShakePattern = ActiveShakePattern.Get();
	const float CurrentTrauma = ShakePattern ? ShakePattern->GetChannelTrauma(ETDShakeChannel::Received) : 0.f;
	const FTDReceivedTraumaResult Result = ReceivedHits.AddHit(GetFeedbackRealTime(), HitFeel.UnclampedDamageRatio, HitFeel.bIsStrongSource, Message.bWasKilled, CurrentTrauma, Message.HitDirection);
	if (Result.TraumaIncrement <= 0.f)
	{
		return;
	}

	const FRotator KickRotation = Result.bShouldKick ? ComputeKickRotation(Controller, Message.HitDirection, ReceivedHits.ComputeAccumulatedRatio()) : FRotator::ZeroRotator;
	AddCameraTrauma(Controller, ETDShakeChannel::Received, Result.TraumaIncrement, FeedbackSettings.ShakeTraumaCap, false, KickRotation);
	if (Result.bStartsShake)
	{
		PlayRumble(Controller, FeedbackSettings.RumbleReceivedIntensity, FeedbackSettings.RumbleReceivedSeconds, true);
	}
}

void UTDCombatFeedbackSubsystem::PlayDealtCameraFeedback(APlayerController& Controller, const FTDDamageAppliedMessage& Message, const FTDHitFeel& HitFeel)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float AttackerStopSeconds = ResolveAttackerStopSeconds(FeedbackSettings, HitFeel);
	const UTDTraumaCameraShakePattern* ShakePattern = ActiveShakePattern.Get();
	const float CurrentTrauma = ShakePattern ? ShakePattern->GetChannelTrauma(ETDShakeChannel::Dealt) : 0.f;
	const float TraumaIncrement = DealtWindow.AddHit(GetFeedbackRealTime(), FObjectKey(Message.Instigator.Get()), HitFeel.ComboStageIndex, HitFeel.TraumaTier, HitFeel.KillRank, AttackerStopSeconds, IsSlowMotionActive(), CurrentTrauma);
	if (HitFeel.bIsPlayerMelee)
	{
		SwingAttacker = Cast<ATDCombatCharacter>(Message.Instigator.Get());
	}
	AddCameraTrauma(Controller, ETDShakeChannel::Dealt, TraumaIncrement, DealtWindow.ComputeChannelCap(), HitFeel.KillRank == ETDKillRank::Boss, FRotator::ZeroRotator);

	const bool bIsKill = HitFeel.KillRank != ETDKillRank::None;
	const float RumbleIntensity = DealtWindow.MergeRumble(bIsKill ? FeedbackSettings.RumbleKillIntensity : FeedbackSettings.RumbleDealtIntensity);
	PlayRumble(Controller, RumbleIntensity, bIsKill ? FeedbackSettings.RumbleKillSeconds : FeedbackSettings.RumbleDealtSeconds, bIsKill);
}

void UTDCombatFeedbackSubsystem::AddCameraTrauma(APlayerController& Controller, const ETDShakeChannel Channel, const float Amount, const float ChannelCap, const bool bAllowsFullTrauma, const FRotator& KickRotation)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const bool bHasKick = !KickRotation.IsNearlyZero();
	if (!FeedbackSettings.bEnableCameraShake || !Controller.PlayerCameraManager || (Amount <= 0.f && !bHasKick))
	{
		return;
	}

	UCameraShakeBase* Shake = Controller.PlayerCameraManager->StartCameraShake(UTDHitCameraShake::StaticClass(), 1.f);
	UTDTraumaCameraShakePattern* ShakePattern = Shake ? Cast<UTDTraumaCameraShakePattern>(Shake->GetRootShakePattern()) : nullptr;
	if (!ShakePattern)
	{
		return;
	}

	if (ShakePattern->GetTrauma() <= KINDA_SMALL_NUMBER && !ShakePattern->IsKicking())
	{
		ShakePattern->SetNoisePhase(FeedbackSettings.bRandomizeShakePhase ? static_cast<float>(FeedbackRandom.FRandRange(0.f, 1000.f)) : 0.f);
	}
	ShakePattern->SetCameraArmLength(ResolveCameraArmLength(&Controller));
	if (bAllowsFullTrauma)
	{
		ShakePattern->AllowFullTrauma();
	}
	ShakePattern->AddChannelTrauma(Channel, Amount, ChannelCap);
	if (bHasKick)
	{
		ShakePattern->AddKick(KickRotation);
		++KickCount;
	}
	ActiveShakePattern = ShakePattern;
	++Stats.ShakesStarted;
}

void UTDCombatFeedbackSubsystem::PlayRumble(APlayerController& Controller, const float Intensity, const float Seconds, const bool bUsesLargeMotors)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float ScaledIntensity = Intensity * FeedbackSettings.GetEffectiveShakeScale();
	if (!FeedbackSettings.bEnableForceFeedback || ScaledIntensity <= 0.f || Seconds <= 0.f)
	{
		return;
	}
	Controller.PlayDynamicForceFeedback(ScaledIntensity, Seconds, bUsesLargeMotors, true, bUsesLargeMotors, true);
}

void UTDCombatFeedbackSubsystem::ScheduleBossKillSlowMotion(const float AttackerStopSeconds)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const double RequestedStartRealTime = GetFeedbackRealTime() + FMath::Max(AttackerStopSeconds, 0.f) + FeedbackSettings.BossKillSlowMotionDelaySeconds;
	AddPendingSlowMotion(RequestedStartRealTime, FeedbackSettings.BossKillSlowMotionScale, FeedbackSettings.BossKillSlowMotionSeconds, true);
}

void UTDCombatFeedbackSubsystem::RequestVictorySlowMotion()
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const double RealNow = GetFeedbackRealTime();
	if (ResolveRagdollSlowMotionStartRealTime(RealNow) <= RealNow)
	{
		RequestSlowMotion(FeedbackSettings.VictorySlowMotionScale, FeedbackSettings.VictorySlowMotionSeconds);
		return;
	}

	AddPendingSlowMotion(RealNow, FeedbackSettings.VictorySlowMotionScale, FeedbackSettings.VictorySlowMotionSeconds, false);
}

void UTDCombatFeedbackSubsystem::AddPendingSlowMotion(const double RequestedStartRealTime, const float Scale, const float RealSeconds, const bool bIgnoresCooldown)
{
	FTDPendingSlowMotion& PendingSlowMotion = PendingSlowMotions.AddDefaulted_GetRef();
	PendingSlowMotion.RequestedStartRealTime = RequestedStartRealTime;
	PendingSlowMotion.Scale = Scale;
	PendingSlowMotion.RealSeconds = RealSeconds;
	PendingSlowMotion.bIgnoresCooldown = bIgnoresCooldown;
}

double UTDCombatFeedbackSubsystem::ResolveRagdollSlowMotionStartRealTime(const double RequestedStartRealTime) const
{
	FTDSlowMotionStartInput StartInput;
	StartInput.RequestedStartRealSeconds = RequestedStartRealTime;
	StartInput.NowRealSeconds = GetFeedbackRealTime();
	StartInput.MaxRagdollDelaySeconds = GetDefault<UTDCombatFeedbackSettings>()->SlowMotionMaxRagdollDelaySeconds;
	StartInput.LatestRagdollEntryRealSeconds = LatestRagdollEntryRealTime;
	StartInput.bHasRagdollAwaitingEntry = HasRagdollAwaitingEntry();
	return TDDeathLaunchRules::ResolveSlowMotionStartRealSeconds(StartInput);
}

void UTDCombatFeedbackSubsystem::UpdateSlowMotion()
{
	const double RealNow = GetFeedbackRealTime();
	RemoveInvalidRagdollEntryWaiters();
	TArray<FTDPendingSlowMotion, TInlineAllocator<2>> DueSlowMotions;
	for (int32 Index = PendingSlowMotions.Num() - 1; Index >= 0; --Index)
	{
		if (ResolveRagdollSlowMotionStartRealTime(PendingSlowMotions[Index].RequestedStartRealTime) > RealNow)
		{
			continue;
		}
		DueSlowMotions.Add(PendingSlowMotions[Index]);
		PendingSlowMotions.RemoveAtSwap(Index);
	}
	for (const FTDPendingSlowMotion& DueSlowMotion : DueSlowMotions)
	{
		RequestSlowMotionWithRule(DueSlowMotion.Scale, DueSlowMotion.RealSeconds, DueSlowMotion.bIgnoresCooldown, true);
	}
	if (SlowMotion.RemoveExpired(RealNow))
	{
		ApplySlowMotionDilation();
	}
}

void UTDCombatFeedbackSubsystem::RequestSlowMotionWithRule(const float Scale, const float RealSeconds, const bool bIgnoresCooldown, const bool bStartsCooldown)
{
	if (!GetWorld() || !SlowMotion.Request(GetFeedbackRealTime(), Scale, RealSeconds, bIgnoresCooldown, bStartsCooldown))
	{
		return;
	}
	ApplySlowMotionDilation();
}

void UTDCombatFeedbackSubsystem::ApplySlowMotionDilation()
{
	UWorld* CurrentWorld = GetWorld();
	AWorldSettings* WorldSettings = CurrentWorld ? CurrentWorld->GetWorldSettings(false, false) : nullptr;
	if (!WorldSettings)
	{
		return;
	}

	WorldSettings->SetTimeDilation(SlowMotion.GetScale());
}

void UTDCombatFeedbackSubsystem::RestoreSlowMotion()
{
	PendingSlowMotions.Reset();
	LatestRagdollEntryRealTime.Reset();
	RagdollEntryWaiters.Reset();
	if (!SlowMotion.IsActive())
	{
		return;
	}
	SlowMotion.ClearRequests();
	ApplySlowMotionDilation();
}

void UTDCombatFeedbackSubsystem::ShowMonsterHealthBar(ATDCombatCharacter& Victim, const FTDDamageAppliedMessage& Message)
{
	const UTDCombatComponent* Combat = Victim.GetCombatComponent();
	if (!OverlayWidget || !Combat || OverlayWidget->GetBossHealthBarTarget() == &Victim)
	{
		return;
	}

	const float MaxHealth = Combat->GetMaxHealth();
	const float FractionBeforeHit = MaxHealth > 0.f ? (Combat->GetCurrentHealth() + Message.DamageAmount) / MaxHealth : 0.f;
	OverlayWidget->ShowMonsterHealthBar(&Victim, FractionBeforeHit, Message.bWasKilled);
}

void UTDCombatFeedbackSubsystem::AddDamageNumber(const FTDDamageNumberRequest& Request)
{
	if (TryShowDamageNumber(Request))
	{
		return;
	}
	DeferredNumbers.Push(Request, GetFeedbackTime());
}

bool UTDCombatFeedbackSubsystem::TryShowDamageNumber(const FTDDamageNumberRequest& Request)
{
	if (!OverlayWidget)
	{
		return true;
	}

	const bool bNeedsNewNumber = !OverlayWidget->CanMergeDamageNumber(Request);
	if (bNeedsNewNumber && NumbersThisFrame >= GetDefault<UTDCombatFeedbackSettings>()->MaxNewNumbersPerFrame)
	{
		return false;
	}

	NumbersThisFrame += bNeedsNewNumber ? 1 : 0;
	if (OverlayWidget->ShowDamageNumber(Request) == INDEX_NONE)
	{
		return true;
	}

	++Stats.NumbersShown;
	if (Request.Style == ETDDamageNumberStyle::Avoided)
	{
		++Stats.AvoidedShown;
	}
	else if (Request.bIsPlayerTarget)
	{
		++Stats.PlayerDamageNumbersShown;
	}
	return true;
}

void UTDCombatFeedbackSubsystem::DrainDeferredFeedback(const double Now)
{
	const double MaxQueuedSeconds = GetDefault<UTDCombatFeedbackSettings>()->MaxDeferredFeedbackSeconds;
	DeferredNumbers.Drain(Now, MaxQueuedSeconds, [this](const FTDDamageNumberRequest& Request)
	{
		return TryShowDamageNumber(Request);
	});
	DeferredFlashes.Drain(Now, MaxQueuedSeconds, [this](const FTDHitFlashRequest& Request)
	{
		return TryStartHitFlash(Request);
	});
}

bool UTDCombatFeedbackSubsystem::PlayFeedbackSound(const FTDFeedbackSoundSet& SoundSet, const FVector& Location, const float VolumeScale, const ETDFeedbackSoundChannel Channel)
{
	return SpawnFeedbackSound(SoundSet, Location, VolumeScale, 1.f, Channel) != nullptr;
}

UAudioComponent* UTDCombatFeedbackSubsystem::SpawnFeedbackSound(const FTDFeedbackSoundSet& SoundSet, const FVector& Location, const float VolumeScale, const float PitchScale, const ETDFeedbackSoundChannel Channel)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const bool bIsStatusTick = Channel == ETDFeedbackSoundChannel::StatusTick;
	const bool bIsBudgetExempt = Channel == ETDFeedbackSoundChannel::Accent || Channel == ETDFeedbackSoundChannel::PlayerHit || Channel == ETDFeedbackSoundChannel::CorpseLand;
	if (bIsStatusTick && StatusSoundsThisFrame >= FeedbackSettings.MaxStatusSoundsPerFrame)
	{
		return nullptr;
	}
	if (!bIsStatusTick && !bIsBudgetExempt && SoundsThisFrame >= FeedbackSettings.MaxNewSoundsPerFrame)
	{
		return nullptr;
	}

	USoundBase* Sound = PickSound(SoundSet);
	if (!Sound)
	{
		return nullptr;
	}
	const float BurstVolumeScale = Channel == ETDFeedbackSoundChannel::Impact ? ResolveBurstVolumeScale(SoundSet) : 1.f;
	if (BurstVolumeScale <= 0.f)
	{
		return nullptr;
	}

	if (bIsStatusTick)
	{
		++StatusSoundsThisFrame;
	}
	else if (Channel != ETDFeedbackSoundChannel::PlayerHit)
	{
		++SoundsThisFrame;
	}
	const float Pitch = static_cast<float>(FeedbackRandom.FRandRange(SoundSet.PitchMin, SoundSet.PitchMax)) * PitchScale;
	UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Location, FRotator::ZeroRotator, SoundSet.Volume * VolumeScale * BurstVolumeScale, Pitch, 0.f, SoundAttenuation, GetSoundConcurrency(Channel), true);
	if (!Audio)
	{
		return nullptr;
	}
	++Stats.SoundsPlayed;
	return Audio;
}

USoundBase* UTDCombatFeedbackSubsystem::PickSound(const FTDFeedbackSoundSet& SoundSet)
{
	TArray<USoundBase*, TInlineAllocator<8>> LoadedSounds;
	for (const TSoftObjectPtr<USoundBase>& SoftSound : SoundSet.Sounds)
	{
		if (USoundBase* Sound = SoftSound.Get())
		{
			LoadedSounds.Add(Sound);
		}
	}
	if (LoadedSounds.IsEmpty())
	{
		return nullptr;
	}
	return LoadedSounds[FeedbackRandom.RandRange(0, LoadedSounds.Num() - 1)];
}

float UTDCombatFeedbackSubsystem::ResolveBurstVolumeScale(const FTDFeedbackSoundSet& SoundSet)
{
	return SoundBursts.FindOrAdd(&SoundSet).TryPlay(GetFeedbackRealTime());
}

void UTDCombatFeedbackSubsystem::RaiseFirstCorpseLandSound()
{
	UAudioComponent* FirstAudio = CorpseLandGroup.FirstAudio.Get();
	if (CorpseLandGroup.bHasRaisedFirstAudio || !FirstAudio)
	{
		return;
	}

	FirstAudio->SetVolumeMultiplier(FirstAudio->VolumeMultiplier * GetDefault<UTDCombatFeedbackSettings>()->CorpseLandGroupVolumeScale);
	CorpseLandGroup.bHasRaisedFirstAudio = true;
}

void UTDCombatFeedbackSubsystem::AddCorpseLandTrauma(const double RealNow, const FVector& Location, const ETDKillRank KillRank, const float CapsuleHalfHeight)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	APlayerController* LocalController = FindLocalPlayerController();
	const AActor* ViewTarget = LocalController ? LocalController->GetViewTarget() : nullptr;
	if (!ViewTarget)
	{
		return;
	}
	const float ViewTargetDistance = static_cast<float>(FVector::Dist(Location, ViewTarget->GetActorLocation()));
	const bool bIsTooSoon = LastCorpseLandTraumaRealTime.IsSet() && RealNow - LastCorpseLandTraumaRealTime.GetValue() < FeedbackSettings.CorpseLandTraumaIntervalSeconds;
	if (bIsTooSoon || !TDDeathLaunchRules::ShouldAddCorpseLandTrauma(KillRank, CapsuleHalfHeight, ViewTargetDistance))
	{
		return;
	}

	LastCorpseLandTraumaRealTime = RealNow;
	AddCameraTrauma(*LocalController, ETDShakeChannel::Event, FeedbackSettings.CorpseLandTrauma, FeedbackSettings.ShakeTraumaCap, false, FRotator::ZeroRotator);
}

void UTDCombatFeedbackSubsystem::UpdateOverlayOwner()
{
	APlayerController* LocalController = FindLocalPlayerController();
	if (!LocalController || (OverlayWidget && OverlayOwner.Get() == LocalController))
	{
		return;
	}

	if (OverlayWidget)
	{
		OverlayWidget->RemoveFromParent();
		OverlayWidget = nullptr;
	}

	OverlayWidget = CreateWidget<UTDCombatOverlayWidget>(LocalController, UTDCombatOverlayWidget::StaticClass());
	OverlayOwner = LocalController;
	if (!OverlayWidget)
	{
		return;
	}
	OverlayWidget->AddToViewport(10);
	RestorePendingBossHealthBar();
}

void UTDCombatFeedbackSubsystem::RestorePendingBossHealthBar()
{
	ATDCombatCharacter* Boss = PendingBossHealthBar.Boss.Get();
	const UTDCombatComponent* Combat = Boss ? Boss->GetCombatComponent() : nullptr;
	if (!OverlayWidget || !Combat || !Combat->IsAlive())
	{
		return;
	}

	OverlayWidget->ShowBossHealthBar(Boss, PendingBossHealthBar.DisplayName, PendingBossHealthBar.PhaseHealthMarks);
	++Stats.BossHealthBarsShown;
}

void UTDCombatFeedbackSubsystem::UpdateHitFlashes(const double Now)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	for (int32 Index = HitFlashes.Num() - 1; Index >= 0; --Index)
	{
		FTDHitFlashState& FlashState = HitFlashes[Index];
		if (!FlashState.Character.IsValid() || !FlashState.Material)
		{
			HitFlashes.RemoveAtSwap(Index);
			continue;
		}
		if (!FlashState.bIsOverlayApplied)
		{
			continue;
		}

		const float Elapsed = static_cast<float>(Now - FlashState.StartTime);
		if (Elapsed < FlashState.DurationSeconds)
		{
			const float Intensity = FlashState.PeakIntensity * (1.f - Elapsed / FlashState.DurationSeconds);
			FlashState.Material->SetScalarParameterValue(FeedbackSettings.FlashIntensityParameterName, Intensity);
			continue;
		}

		if (FlashState.bIsFading)
		{
			FlashState.Material->SetScalarParameterValue(FeedbackSettings.FlashIntensityParameterName, 0.f);
			FlashState.bIsFading = false;
		}
		if (Elapsed < FlashState.DurationSeconds + FeedbackSettings.FlashOverlayHoldSeconds || OverlayChangesThisFrame >= FeedbackSettings.MaxOverlayChangesPerFrame)
		{
			continue;
		}

		ReleaseHitFlash(FlashState);
		++OverlayChangesThisFrame;
	}
}

void UTDCombatFeedbackSubsystem::UpdateWobbles(const double Now)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float DecaySeconds = FMath::Max(FeedbackSettings.WobbleDecaySeconds, KINDA_SMALL_NUMBER);
	const double RealNow = GetFeedbackRealTime();
	for (int32 Index = Wobbles.Num() - 1; Index >= 0; --Index)
	{
		FTDWobbleState& WobbleState = Wobbles[Index];
		ATDCombatCharacter* Character = WobbleState.Character.Get();
		if (!Character)
		{
			Wobbles.RemoveAtSwap(Index);
			continue;
		}
		if (Character->IsRagdollPendingOrActive())
		{
			Character->SetPresentationOffset(FVector::ZeroVector);
			Wobbles.RemoveAtSwap(Index);
			continue;
		}
		if (UpdateHitStopJitter(WobbleState, *Character, RealNow))
		{
			WobbleState.StartTime = Now;
			continue;
		}

		const float Elapsed = static_cast<float>(Now - WobbleState.StartTime);
		if (Elapsed >= FeedbackSettings.WobbleSeconds)
		{
			Character->SetPresentationOffset(FVector::ZeroVector);
			Wobbles.RemoveAtSwap(Index);
			continue;
		}

		const float Envelope = FMath::Exp(-Elapsed / DecaySeconds) * FMath::Cos(static_cast<float>(UE_TWO_PI) * FeedbackSettings.WobbleFrequencyHz * Elapsed);
		Character->SetPresentationOffset(WobbleState.Direction * (WobbleState.Amplitude * Envelope));
	}
}

bool UTDCombatFeedbackSubsystem::UpdateHitStopJitter(FTDWobbleState& WobbleState, ATDCombatCharacter& Character, const double RealNow)
{
	if (WobbleState.JitterSeconds <= 0.f)
	{
		return false;
	}

	const float Elapsed = static_cast<float>(RealNow - WobbleState.JitterStartRealTime);
	float RemainingSeconds = WobbleState.JitterSeconds - Elapsed;
	if (WobbleState.bFollowsHitStop)
	{
		const UTDCharacterAnimationComponent* Animation = Character.GetCharacterAnimation();
		if (Animation && Animation->IsHitStopPending() && RemainingSeconds > 0.f)
		{
			Character.SetPresentationOffset(FVector::ZeroVector);
			return true;
		}
		RemainingSeconds = Animation ? Animation->GetVictimHitStopRemainingSeconds() : 0.f;
	}
	if (RemainingSeconds <= 0.f)
	{
		WobbleState.JitterSeconds = 0.f;
		return false;
	}

	const float JitterFrequencyHz = GetDefault<UTDCombatFeedbackSettings>()->HitStopJitterFrequencyHz;
	const float Fraction = FMath::Clamp(RemainingSeconds / WobbleState.JitterSeconds, 0.f, 1.f);
	const float SquareWave = FMath::Sin(static_cast<float>(UE_TWO_PI) * JitterFrequencyHz * Elapsed) >= 0.f ? 1.f : -1.f;
	Character.SetPresentationOffset(WobbleState.Direction * (WobbleState.JitterAmplitude * Fraction * SquareWave));
	return true;
}

void UTDCombatFeedbackSubsystem::ReleaseHitFlash(FTDHitFlashState& FlashState)
{
	ATDCombatCharacter* Character = FlashState.Character.Get();
	const USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	UMaterialInterface* PreviousOverlay = FlashState.PreviousOverlay.Get();
	FlashState.PreviousOverlay = nullptr;
	FlashState.bIsOverlayApplied = false;
	FlashState.bIsFading = false;
	if (!CharacterMesh || CharacterMesh->GetOverlayMaterial() != FlashState.Material.Get())
	{
		return;
	}
	Character->SetHitFlashOverlay(PreviousOverlay);
}

void UTDCombatFeedbackSubsystem::RestorePresentation()
{
	for (FTDHitFlashState& FlashState : HitFlashes)
	{
		if (FlashState.bIsOverlayApplied)
		{
			ReleaseHitFlash(FlashState);
		}
	}
	HitFlashes.Reset();
	DeferredFlashes.Reset();
	DeferredNumbers.Reset();

	for (const FTDWobbleState& WobbleState : Wobbles)
	{
		if (ATDCombatCharacter* Character = WobbleState.Character.Get())
		{
			Character->SetPresentationOffset(FVector::ZeroVector);
		}
	}
	Wobbles.Reset();
}

FTDHitFlashState* UTDCombatFeedbackSubsystem::FindHitFlash(const ATDCombatCharacter& Character)
{
	return HitFlashes.FindByPredicate([&Character](const FTDHitFlashState& FlashState)
	{
		return FlashState.Character.Get() == &Character;
	});
}

UTDCombatFeedbackSubsystem::FTDWobbleState* UTDCombatFeedbackSubsystem::FindWobble(const ATDCombatCharacter& Character)
{
	return Wobbles.FindByPredicate([&Character](const FTDWobbleState& WobbleState)
	{
		return WobbleState.Character.Get() == &Character;
	});
}

APlayerController* UTDCombatFeedbackSubsystem::FindLocalPlayerController() const
{
	const UWorld* CurrentWorld = GetWorld();
	APlayerController* Controller = CurrentWorld ? CurrentWorld->GetFirstPlayerController() : nullptr;
	return Controller && Controller->IsLocalController() ? Controller : nullptr;
}

FRotator UTDCombatFeedbackSubsystem::ComputeKickRotation(const APlayerController& Controller, const FVector& HitDirection, const float DamageRatio) const
{
	const FVector Direction = HitDirection.GetSafeNormal2D();
	if (Direction.IsNearlyZero() || !Controller.PlayerCameraManager)
	{
		return FRotator::ZeroRotator;
	}

	const float KickDegrees = TDHitCameraShakeMath::ComputeKickDegrees(DamageRatio, *GetDefault<UTDCombatFeedbackSettings>());
	const FRotationMatrix CameraBasis(FRotator(0.f, Controller.PlayerCameraManager->GetCameraRotation().Yaw, 0.f));
	const double ForwardAmount = Direction | CameraBasis.GetUnitAxis(EAxis::X);
	const double RightAmount = Direction | CameraBasis.GetUnitAxis(EAxis::Y);
	return FRotator(KickDegrees * ForwardAmount, KickDegrees * RightAmount, 0.);
}

float UTDCombatFeedbackSubsystem::ResolveCameraArmLength(const APlayerController* Controller) const
{
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const USpringArmComponent* CameraBoom = Pawn ? Pawn->FindComponentByClass<USpringArmComponent>() : nullptr;
	return CameraBoom ? CameraBoom->TargetArmLength : GetDefault<UTDCombatFeedbackSettings>()->ReferenceCameraArmLengthCm;
}

FVector UTDCombatFeedbackSubsystem::ResolveHitLocation(const FTDDamageAppliedMessage& Message) const
{
	if (Message.bHasHitLocation)
	{
		return Message.HitLocation;
	}

	const AActor* Target = Message.Target.Get();
	const ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(Target);
	const USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	if (CharacterMesh)
	{
		return CharacterMesh->Bounds.Origin;
	}
	return Target ? Target->GetActorLocation() : FVector::ZeroVector;
}

float UTDCombatFeedbackSubsystem::ResolveRespawnDelaySeconds() const
{
	const UWorld* CurrentWorld = GetWorld();
	const ATDGameGameMode* GameMode = CurrentWorld ? CurrentWorld->GetAuthGameMode<ATDGameGameMode>() : nullptr;
	if (GameMode)
	{
		return GameMode->GetPlayerRespawnDelaySeconds();
	}
	return GetDefault<UTDCombatFeedbackSettings>()->FallbackRespawnDelaySeconds;
}

double UTDCombatFeedbackSubsystem::GetFeedbackTime() const
{
	const UWorld* CurrentWorld = GetWorld();
	return CurrentWorld ? CurrentWorld->GetTimeSeconds() : 0.;
}

double UTDCombatFeedbackSubsystem::GetFeedbackRealTime() const
{
	const UWorld* CurrentWorld = GetWorld();
	return CurrentWorld ? CurrentWorld->GetRealTimeSeconds() : 0.;
}
