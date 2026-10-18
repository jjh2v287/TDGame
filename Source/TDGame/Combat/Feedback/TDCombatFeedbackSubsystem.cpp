#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDCombatCharacter.h"
#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Combat/Feedback/TDCombatOverlayWidget.h"
#include "Combat/Feedback/TDHitCameraShake.h"
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
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraComponentPoolMethodEnum.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "TDGame.h"
#include "TimerManager.h"

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

void UTDCombatFeedbackSubsystem::Deinitialize()
{
	UnregisterListeners();
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
	OverlayChangesThisFrame = 0;

	const double Now = GetFeedbackTime();
	UpdateOverlayOwner();
	DrainDeferredFeedback(Now);
	UpdateHitFlashes(Now);
	UpdateWobbles(Now);
}

void UTDCombatFeedbackSubsystem::RegisterListeners(UWorld& InWorld)
{
	UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(&InWorld);
	DamageAppliedListener = MessageSubsystem.RegisterListener<FTDDamageAppliedMessage>(TDGameplayTags::Event_Damage_Applied, this, &ThisClass::HandleDamageApplied);
	DamageAvoidedListener = MessageSubsystem.RegisterListener<FTDDamageAvoidedMessage>(TDGameplayTags::Event_Damage_Avoided, this, &ThisClass::HandleDamageAvoided);
	ActorDeathListener = MessageSubsystem.RegisterListener<FTDActorDeathMessage>(TDGameplayTags::Event_Actor_Death, this, &ThisClass::HandleActorDeath);
	MeleeSwingListener = MessageSubsystem.RegisterListener<FTDMeleeSwingMessage>(TDGameplayTags::Event_Combat_MeleeSwing, this, &ThisClass::HandleMeleeSwing);
	bIsListening = true;
}

void UTDCombatFeedbackSubsystem::UnregisterListeners()
{
	DamageAppliedListener.Unregister();
	DamageAvoidedListener.Unregister();
	ActorDeathListener.Unregister();
	MeleeSwingListener.Unregister();
	bIsListening = false;
}

void UTDCombatFeedbackSubsystem::CreateSoundSettings(const UTDCombatFeedbackSettings& FeedbackSettings)
{
	ImpactSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.ImpactSoundMaxCount);
	AccentSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.AccentSoundMaxCount);
	SwingSoundConcurrency = CreateSoundConcurrency(FeedbackSettings.SwingSoundMaxCount);

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
	default:
		return ImpactSoundConcurrency.Get();
	}
}

USoundConcurrency* UTDCombatFeedbackSubsystem::CreateSoundConcurrency(const int32 MaxCount)
{
	USoundConcurrency* SoundConcurrency = NewObject<USoundConcurrency>(this);
	SoundConcurrency->Concurrency.MaxCount = FMath::Max(1, MaxCount);
	SoundConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
	SoundConcurrency->Concurrency.RetriggerTime = 0.f;
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
		StartHitFlash(*Victim, Presentation, Message.bWasCritical, bIsLocalVictim);
		if (Presentation.bWobblesVictim)
		{
			StartWobble(*Victim, Presentation.Tier, Direction);
		}
		if (Presentation.bStopsVictim)
		{
			StartVictimHitStop(*Victim, Presentation.Tier);
		}
	}
	if (Presentation.bStopsAttacker)
	{
		StartAttackerHitStop(Message.Instigator.Get());
	}
	if (LocalController && Presentation.bPlaysCameraFeedback)
	{
		PlayLocalCameraFeedback(*LocalController, Presentation.Tier, Message.DamageRatio, bIsLocalAttacker, bIsLocalVictim);
	}
	if (bIsLocalVictim && OverlayWidget)
	{
		OverlayWidget->PulseDamageVignette(UTDCombatOverlayWidget::ComputeHitVignetteIntensity(Message.DamageRatio, Presentation.bIsStatus, FeedbackSettings));
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
	const APlayerController* LocalController = FindLocalPlayerController();
	const APawn* LocalPawn = LocalController ? LocalController->GetPawn() : nullptr;
	if (!Attacker || Attacker != LocalPawn)
	{
		return;
	}

	const FVector Location = Message.Location.IsNearlyZero() ? Attacker->GetActorLocation() : Message.Location;
	if (PlayFeedbackSound(GetDefault<UTDCombatFeedbackSettings>()->MeleeSwingSound, Location, 1.f, ETDFeedbackSoundChannel::Swing))
	{
		++Stats.SwingsPlayed;
	}
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
	if (Presentation.Tier == ETDHitFeedbackTier::Kill)
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
	const float VolumeScale = Presentation.bIsStatus ? FeedbackSettings.StatusSoundVolumeScale : 1.f;
	if (const FTDFeedbackSoundSet* ImpactSound = FeedbackSettings.ImpactSounds.Find(Message.Element))
	{
		PlayFeedbackSound(*ImpactSound, Location, VolumeScale, ETDFeedbackSoundChannel::Impact);
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

void UTDCombatFeedbackSubsystem::StartHitFlash(ATDCombatCharacter& Victim, const FTDHitPresentation& Presentation, const bool bWasCritical, const bool bIsLocalVictim)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float PeakIntensity = Presentation.bIsStatus ? FeedbackSettings.StatusFlashIntensity : 1.f;
	if (PeakIntensity <= 0.f || FeedbackSettings.FlashSeconds <= 0.f || FeedbackSettings.HitFlashMaterial.IsNull())
	{
		return;
	}

	FTDHitFlashRequest Request;
	Request.Victim = &Victim;
	Request.Color = bIsLocalVictim ? FeedbackSettings.PlayerFlashColor : (bWasCritical ? FeedbackSettings.CriticalFlashColor : FeedbackSettings.MonsterFlashColor);
	Request.PeakIntensity = PeakIntensity;
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
	FlashState.PeakIntensity = Request.PeakIntensity;
	FlashState.bIsFading = true;
	++Stats.FlashesStarted;
}

void UTDCombatFeedbackSubsystem::StartWobble(ATDCombatCharacter& Victim, const ETDHitFeedbackTier Tier, const FVector& Direction)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float Amplitude = Tier == ETDHitFeedbackTier::Light ? FeedbackSettings.WobbleLightCm : FeedbackSettings.WobbleHeavyCm;
	if (Direction.IsNearlyZero() || Amplitude <= 0.f || FeedbackSettings.WobbleSeconds <= 0.f || Victim.IsRagdolling())
	{
		return;
	}

	FTDWobbleState* WobbleState = Wobbles.FindByPredicate([&Victim](const FTDWobbleState& WobbleEntry)
	{
		return WobbleEntry.Character.Get() == &Victim;
	});
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

void UTDCombatFeedbackSubsystem::StartVictimHitStop(ATDCombatCharacter& Victim, const ETDHitFeedbackTier Tier)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float Seconds = Tier == ETDHitFeedbackTier::Heavy ? FeedbackSettings.HeavyHitStopSeconds : FeedbackSettings.LightHitStopSeconds;
	StartCharacterHitStop(Victim, Seconds);
}

void UTDCombatFeedbackSubsystem::StartAttackerHitStop(AActor* Instigator)
{
	const float Seconds = GetDefault<UTDCombatFeedbackSettings>()->AttackerHitStopSeconds;
	ATDCombatCharacter* Attacker = Cast<ATDCombatCharacter>(Instigator);
	if (!Attacker || Seconds <= 0.f)
	{
		return;
	}
	StartCharacterHitStop(*Attacker, Seconds);
}

bool UTDCombatFeedbackSubsystem::StartCharacterHitStop(ATDCombatCharacter& Character, const float Seconds)
{
	const UTDCombatComponent* Combat = Character.GetCombatComponent();
	UTDCharacterAnimationComponent* Animation = Character.GetCharacterAnimation();
	if (Seconds <= 0.f || !Combat || !Combat->IsAlive() || !Animation || Animation->IsPlayingRootMotionAction())
	{
		return false;
	}

	Animation->StartHitStop(Seconds);
	if (!Animation->IsHitStopped())
	{
		return false;
	}
	++Stats.HitStopsStarted;
	return true;
}

void UTDCombatFeedbackSubsystem::PlayLocalCameraFeedback(APlayerController& Controller, const ETDHitFeedbackTier Tier, const float DamageRatio, const bool bIsLocalAttacker, const bool bIsLocalVictim)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	float Trauma = 0.f;
	float RumbleIntensity = 0.f;
	float RumbleSeconds = 0.f;
	bool bUsesLargeMotors = true;
	if (bIsLocalVictim)
	{
		Trauma = FMath::Clamp(FeedbackSettings.TraumaReceivedMin + FeedbackSettings.TraumaReceivedPerDamageRatio * DamageRatio, FeedbackSettings.TraumaReceivedMin, FeedbackSettings.TraumaReceivedMax);
		RumbleIntensity = FeedbackSettings.RumbleReceivedIntensity;
		RumbleSeconds = FeedbackSettings.RumbleReceivedSeconds;
	}
	else if (bIsLocalAttacker && Tier == ETDHitFeedbackTier::Kill)
	{
		Trauma = FeedbackSettings.TraumaKill;
		RumbleIntensity = FeedbackSettings.RumbleKillIntensity;
		RumbleSeconds = FeedbackSettings.RumbleKillSeconds;
	}
	else if (bIsLocalAttacker)
	{
		Trauma = Tier == ETDHitFeedbackTier::Heavy ? FeedbackSettings.TraumaDealtHeavy : FeedbackSettings.TraumaDealt;
		RumbleIntensity = FeedbackSettings.RumbleDealtIntensity;
		RumbleSeconds = FeedbackSettings.RumbleDealtSeconds;
		bUsesLargeMotors = false;
	}
	else
	{
		return;
	}

	if (FeedbackSettings.bEnableCameraShake && Controller.PlayerCameraManager && Trauma > 0.f)
	{
		Controller.PlayerCameraManager->StartCameraShake(UTDHitCameraShake::StaticClass(), Trauma);
		++Stats.ShakesStarted;
	}
	if (FeedbackSettings.bEnableForceFeedback && RumbleIntensity > 0.f && RumbleSeconds > 0.f)
	{
		Controller.PlayDynamicForceFeedback(RumbleIntensity, RumbleSeconds, bUsesLargeMotors, true, bUsesLargeMotors, true);
	}
}

void UTDCombatFeedbackSubsystem::ShowMonsterHealthBar(ATDCombatCharacter& Victim, const FTDDamageAppliedMessage& Message)
{
	const UTDCombatComponent* Combat = Victim.GetCombatComponent();
	if (!OverlayWidget || !Combat)
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
	if (Channel != ETDFeedbackSoundChannel::Accent && SoundsThisFrame >= GetDefault<UTDCombatFeedbackSettings>()->MaxNewSoundsPerFrame)
	{
		return false;
	}

	USoundBase* Sound = PickSound(SoundSet);
	if (!Sound)
	{
		return false;
	}

	++SoundsThisFrame;
	const float Pitch = static_cast<float>(FeedbackRandom.FRandRange(SoundSet.PitchMin, SoundSet.PitchMax));
	const UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Location, FRotator::ZeroRotator, SoundSet.Volume * VolumeScale, Pitch, 0.f, SoundAttenuation, GetSoundConcurrency(Channel), true);
	if (!Audio)
	{
		return false;
	}
	++Stats.SoundsPlayed;
	return true;
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
		if (Elapsed < FeedbackSettings.FlashSeconds)
		{
			const float Intensity = FlashState.PeakIntensity * (1.f - Elapsed / FeedbackSettings.FlashSeconds);
			FlashState.Material->SetScalarParameterValue(FeedbackSettings.FlashIntensityParameterName, Intensity);
			continue;
		}

		if (FlashState.bIsFading)
		{
			FlashState.Material->SetScalarParameterValue(FeedbackSettings.FlashIntensityParameterName, 0.f);
			FlashState.bIsFading = false;
		}
		if (Elapsed < FeedbackSettings.FlashSeconds + FeedbackSettings.FlashOverlayHoldSeconds || OverlayChangesThisFrame >= FeedbackSettings.MaxOverlayChangesPerFrame)
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
	for (int32 Index = Wobbles.Num() - 1; Index >= 0; --Index)
	{
		FTDWobbleState& WobbleState = Wobbles[Index];
		ATDCombatCharacter* Character = WobbleState.Character.Get();
		if (!Character)
		{
			Wobbles.RemoveAtSwap(Index);
			continue;
		}

		const float Elapsed = static_cast<float>(Now - WobbleState.StartTime);
		if (Elapsed >= FeedbackSettings.WobbleSeconds || Character->IsRagdolling())
		{
			Character->SetPresentationOffset(FVector::ZeroVector);
			Wobbles.RemoveAtSwap(Index);
			continue;
		}

		const float Envelope = FMath::Exp(-Elapsed / DecaySeconds) * FMath::Cos(static_cast<float>(UE_TWO_PI) * FeedbackSettings.WobbleFrequencyHz * Elapsed);
		Character->SetPresentationOffset(WobbleState.Direction * (WobbleState.Amplitude * Envelope));
	}
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

APlayerController* UTDCombatFeedbackSubsystem::FindLocalPlayerController() const
{
	const UWorld* CurrentWorld = GetWorld();
	APlayerController* Controller = CurrentWorld ? CurrentWorld->GetFirstPlayerController() : nullptr;
	return Controller && Controller->IsLocalController() ? Controller : nullptr;
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
