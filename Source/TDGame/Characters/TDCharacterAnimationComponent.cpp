#include "Characters/TDCharacterAnimationComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Backends/MoverStandaloneLiaison.h"
#include "Characters/TDCombatCharacter.h"
#include "Component/AnimNextComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/LayeredMoves/RootMotionAttributeLayeredMove.h"
#include "Engine/World.h"
#include "Injection/InjectionUtils.h"
#include "InstanceTaskContext.h"
#include "Module/UAFWeakSystemReference.h"
#include "MoverComponent.h"
#include "TimerManager.h"
#include "Traits/BlendSpacePlayerTraitData.h"
#include "Traits/SequencePlayerTraitData.h"
#include "UAF/UAFAssetData.h"
#include "UAF/UAFAssetFactory.h"
#include "Variables/AnimNextVariableReference.h"

namespace TDCharacterAnimationPlayback
{
	constexpr float IdleSpeedThreshold = 15.f;
	constexpr float RunHysteresisRatio = 0.12f;
	constexpr float MinimumLocomotionPlayRate = 0.5f;
	constexpr float MaximumLocomotionPlayRate = 2.f;
	constexpr float LocomotionPlayRateTolerance = 0.15f;
	constexpr float MinimumActionPlayRate = 0.05f;
	constexpr float MinimumDeathSeconds = 0.01f;
	constexpr int32 MaximumActionStopAttempts = 2;
	constexpr float LocomotionBlendSeconds = 0.2f;
	constexpr float ActionBlendInSeconds = 0.1f;
	constexpr float ActionBlendOutSeconds = 0.15f;
	constexpr float DeathBlendSeconds = 0.1f;

	const FName& GetAnimationPrePhysicsEventName()
	{
		static const FName EventName(TEXT("PrePhysics"));
		return EventName;
	}

	FAnimNextVariableReference MakeStructVariable(const UScriptStruct* Struct, const FName PropertyName)
	{
		const FProperty* Property = Struct->FindPropertyByName(PropertyName);
		return Property ? FAnimNextVariableReference::FromProperty(Property, Struct) : FAnimNextVariableReference();
	}

	const FAnimNextVariableReference& GetBlendSpaceXVariable()
	{
		static const FAnimNextVariableReference Variable = MakeStructVariable(FAnimNextBlendSpacePlayerTraitSharedData::StaticStruct(), GET_MEMBER_NAME_CHECKED(FAnimNextBlendSpacePlayerTraitSharedData, XAxisSamplePoint));
		return Variable;
	}

	const FAnimNextVariableReference& GetBlendSpaceYVariable()
	{
		static const FAnimNextVariableReference Variable = MakeStructVariable(FAnimNextBlendSpacePlayerTraitSharedData::StaticStruct(), GET_MEMBER_NAME_CHECKED(FAnimNextBlendSpacePlayerTraitSharedData, YAxisSamplePoint));
		return Variable;
	}

	const FAnimNextVariableReference& GetSequencePlayRateVariable()
	{
		static const FAnimNextVariableReference Variable = MakeStructVariable(FAnimNextSequencePlayerTraitSharedData::StaticStruct(), GET_MEMBER_NAME_CHECKED(FAnimNextSequencePlayerTraitSharedData, PlayRate));
		return Variable;
	}

	const UE::UAF::FInjectionSite& GetDefaultInjectionSite()
	{
		static const UScriptStruct* InjectionSiteStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/UAFAnimGraph.AnimNextInjectionSiteTraitSharedData"));
		static const UE::UAF::FInjectionSite InjectionSite(InjectionSiteStruct ? MakeStructVariable(InjectionSiteStruct, TEXT("Graph")) : FAnimNextVariableReference());
		return InjectionSite;
	}

	UE::UAF::FInjectionBlendSettings MakeBlendSettings(const float BlendSeconds)
	{
		UE::UAF::FInjectionBlendSettings BlendSettings;
		BlendSettings.Blend = FAlphaBlendArgs(BlendSeconds);
		BlendSettings.BlendMode = UE::UAF::EInjectionBlendMode::Standard;
		return BlendSettings;
	}

	UE::UAF::FInjectionRequestPtr InjectSequence(UUAFComponent& AnimationSystem, UAnimSequence* Animation, const float PlayRate, const float StartSeconds, const bool bShouldLoop, const EAnimNextInjectionLifetimeType LifetimeType, const float BlendInSeconds, const float BlendOutSeconds)
	{
		UE::UAF::FPlayAnimArgs PlayArgs;
		PlayArgs.PlayRate = PlayRate;
		PlayArgs.StartPosition = StartSeconds;
		PlayArgs.LoopMode = bShouldLoop ? UE::UAF::EAnimAssetLoopMode::ForceLoop : UE::UAF::EAnimAssetLoopMode::ForceNonLoop;
		PlayArgs.LifetimeType = LifetimeType;
		return UE::UAF::FInjectionUtils::PlayAnim(&AnimationSystem, GetDefaultInjectionSite(), Animation, MoveTemp(PlayArgs), MakeBlendSettings(BlendInSeconds), MakeBlendSettings(BlendOutSeconds));
	}

	bool IsInjectionPending(const UE::UAF::FInjectionRequest& Request)
	{
		return !Request.IsPlaying() && !Request.HasCompleted() && !Request.HasExpired();
	}

	bool CanUninject(const UE::UAF::FInjectionRequest& Request)
	{
		return Request.IsPlaying() && !Request.WasInterrupted() && !Request.IsBlendingOut();
	}

	void UninjectAndReset(UE::UAF::FInjectionRequestPtr& Request)
	{
		if (Request && CanUninject(*Request))
		{
			UE::UAF::FInjectionUtils::Uninject(Request);
		}
		Request.Reset();
	}

	void SetInjectedSequencePlayRate(UE::UAF::FInjectionRequest& Request, const float PlayRate)
	{
		Request.QueueTask([PlayRate](const UE::UAF::FInstanceTaskContext& Context)
		{
			Context.AccessVariablesStruct<FAnimNextSequencePlayerTraitSharedData>([PlayRate](FAnimNextSequencePlayerTraitSharedData& SequencePlayer)
			{
				SequencePlayer.PlayRate = PlayRate;
			});
		});
	}

	float ComputeSignedMoveDirection(const FVector& Velocity, const FRotator& FacingRotation)
	{
		const FVector PlanarDirection = Velocity.GetSafeNormal2D();
		if (PlanarDirection.IsZero())
		{
			return 0.f;
		}
		const FRotationMatrix FacingMatrix(FRotator(0.f, FacingRotation.Yaw, 0.f));
		const float ForwardAmount = FVector::DotProduct(PlanarDirection, FacingMatrix.GetUnitAxis(EAxis::X));
		const float RightAmount = FVector::DotProduct(PlanarDirection, FacingMatrix.GetUnitAxis(EAxis::Y));
		return FMath::RadiansToDegrees(FMath::Atan2(RightAmount, ForwardAmount));
	}

	float ResolveBlendAxisValue(const FString& AxisName, const float Speed, const float Direction)
	{
		if (AxisName == TEXT("Speed"))
		{
			return Speed;
		}
		if (AxisName == TEXT("Direction"))
		{
			return Direction;
		}
		return 0.f;
	}

	UMoverComponent* FindMoverComponent(const AActor* Owner)
	{
		if (const ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(Owner))
		{
			return Character->GetMoverComponent();
		}
		return Owner ? Owner->FindComponentByClass<UMoverComponent>() : nullptr;
	}

	void LinkMovementAfterAnimation(UUAFComponent& AnimationSystem, UActorComponent& AnimationDriver)
	{
		const AActor* Owner = AnimationSystem.GetOwner();
		UMoverStandaloneLiaisonComponent* MovementBackend = Owner ? Owner->FindComponentByClass<UMoverStandaloneLiaisonComponent>() : nullptr;
		FTickFunction* SimulateMovementTick = MovementBackend ? MovementBackend->FindTickFunction(EMoverTickPhase::SimulateMovement) : nullptr;
		if (!SimulateMovementTick || !AnimationSystem.GetSystemReference().IsValid())
		{
			return;
		}
		AnimationSystem.AddSubsequent(MovementBackend, *SimulateMovementTick, GetAnimationPrePhysicsEventName());
		AnimationSystem.AddComponentPrerequisite(&AnimationDriver, GetAnimationPrePhysicsEventName());
	}
}

UTDCharacterAnimationComponent::UTDCharacterAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UTDCharacterAnimationComponent::BeginPlay()
{
	Super::BeginPlay();
	bWasAirborne = IsOwnerAirborne();
	ApplyBaseAsset();
}

void UTDCharacterAnimationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TDCharacterAnimationPlayback::UninjectAndReset(ActionRequest);
	TDCharacterAnimationPlayback::UninjectAndReset(LocomotionRequest);
	ActiveAction = FTDActiveAction();
	CurrentLocomotionPose = ETDLocomotionPose::None;
	bIsRootMotionEnabled = false;
	Super::EndPlay(EndPlayReason);
}

void UTDCharacterAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	CancelPendingRootMotion();
	if (bIsPlayingDeath || bIsAnimationFrozen || !bHasBaseAsset)
	{
		return;
	}

	if (IsPlayingAction())
	{
		bWasAirborne = IsOwnerAirborne();
		AdvanceAction(DeltaTime);
		if (IsPlayingAction() || bIsPlayingDeath)
		{
			return;
		}
	}

	LandingSecondsRemaining = FMath::Max(LandingSecondsRemaining - DeltaTime, 0.f);
	UpdateAirborneClips();
	if (bUsesBlendSpace)
	{
		UpdateBlendSpaceInputs();
	}

	const bool bIsAirbornePoseActive = CurrentLocomotionPose == ETDLocomotionPose::JumpStart
		|| CurrentLocomotionPose == ETDLocomotionPose::FallLoop
		|| CurrentLocomotionPose == ETDLocomotionPose::Land;
	if (bIsAirbornePoseActive)
	{
		return;
	}
	if (bUsesBlendSpace)
	{
		ClearLocomotionInjection();
		return;
	}
	UpdateLocomotionClips(DeltaTime);
}

void UTDCharacterAnimationComponent::UseLocomotionBlendSpace(UBlendSpace* BlendSpace)
{
	if (!BlendSpace)
	{
		return;
	}
	LocomotionBlendSpace = BlendSpace;
	bUsesBlendSpace = true;
	if (HasBegunPlay())
	{
		ApplyBaseAsset();
	}
}

void UTDCharacterAnimationComponent::UseLocomotionClips(const FTDLocomotionClipSet& Clips)
{
	LocomotionClips = Clips;
	LocomotionBlendSpace = nullptr;
	bUsesBlendSpace = false;
	if (HasBegunPlay())
	{
		ApplyBaseAsset();
	}
}

void UTDCharacterAnimationComponent::UseAirborneClips(const FTDAirborneClipSet& Clips)
{
	AirborneClips = Clips;
}

float UTDCharacterAnimationComponent::PlayAction(UAnimSequence* Animation, float PlayRate, float StartSeconds, float EndSeconds, bool bUseRootMotion)
{
	UUAFComponent* AnimationSystem = ResolveUAFComponent();
	if (!Animation || !AnimationSystem || !bHasBaseAsset || bIsPlayingDeath || bIsAnimationFrozen)
	{
		return 0.f;
	}

	const float SafePlayRate = FMath::Max(PlayRate, TDCharacterAnimationPlayback::MinimumActionPlayRate);
	const float EffectivePlayRate = FMath::Max(SafePlayRate * Animation->RateScale, TDCharacterAnimationPlayback::MinimumActionPlayRate);
	const float AnimationLength = Animation->GetPlayLength();
	const float StartTime = FMath::Clamp(StartSeconds, 0.f, AnimationLength);
	const float EndTime = EndSeconds > StartTime ? FMath::Min(EndSeconds, AnimationLength) : AnimationLength;
	if (EndTime <= StartTime)
	{
		return 0.f;
	}

	for (int32 StopAttempt = 0; StopAttempt < TDCharacterAnimationPlayback::MaximumActionStopAttempts && IsPlayingAction(); ++StopAttempt)
	{
		StopAction(TDCharacterAnimationPlayback::ActionBlendOutSeconds);
	}
	if (IsPlayingAction() || bIsPlayingDeath || bIsAnimationFrozen)
	{
		return 0.f;
	}

	ActionRequest = TDCharacterAnimationPlayback::InjectSequence(*AnimationSystem, Animation, SafePlayRate, StartTime, false, EAnimNextInjectionLifetimeType::Auto, TDCharacterAnimationPlayback::ActionBlendInSeconds, TDCharacterAnimationPlayback::ActionBlendOutSeconds);
	if (!ActionRequest)
	{
		return 0.f;
	}

	LocomotionRequest.Reset();
	CurrentLocomotionPose = ETDLocomotionPose::None;
	LandingSecondsRemaining = 0.f;

	ActiveAction.Animation = Animation;
	ActiveAction.PlayRate = EffectivePlayRate;
	ActiveAction.StartSeconds = StartTime;
	ActiveAction.EndSeconds = EndTime;
	ActiveAction.AnimationTime = StartTime;
	ActiveAction.PreviousAnimationTime = StartTime;
	ActiveAction.bUsesRootMotion = bUseRootMotion;
	SetRootMotionEnabled(bUseRootMotion);
	return (EndTime - StartTime) / EffectivePlayRate;
}

void UTDCharacterAnimationComponent::StopAction(float BlendOutSeconds)
{
	if (!IsPlayingAction())
	{
		return;
	}
	FinishAction(true, BlendOutSeconds);
}

float UTDCharacterAnimationComponent::PlayDeath(UAnimSequence* Animation, float PlayRate, float EndSeconds)
{
	UUAFComponent* AnimationSystem = ResolveUAFComponent();
	if (!Animation || !AnimationSystem || !bHasBaseAsset || bIsPlayingDeath)
	{
		return 0.f;
	}

	bIsPlayingDeath = true;
	StopAction(TDCharacterAnimationPlayback::ActionBlendOutSeconds);

	const float SafePlayRate = FMath::Max(PlayRate, TDCharacterAnimationPlayback::MinimumActionPlayRate);
	const float AnimationLength = Animation->GetPlayLength();
	const float EndTime = EndSeconds > 0.f ? FMath::Min(EndSeconds, AnimationLength) : AnimationLength;
	UE::UAF::FInjectionRequestPtr DeathRequest = TDCharacterAnimationPlayback::InjectSequence(*AnimationSystem, Animation, SafePlayRate, 0.f, false, EAnimNextInjectionLifetimeType::ForcePersistent, TDCharacterAnimationPlayback::DeathBlendSeconds, TDCharacterAnimationPlayback::DeathBlendSeconds);
	if (!DeathRequest)
	{
		bIsPlayingDeath = false;
		return 0.f;
	}

	LocomotionRequest.Reset();
	CurrentLocomotionPose = ETDLocomotionPose::None;
	LandingSecondsRemaining = 0.f;
	ActionRequest = MoveTemp(DeathRequest);
	DeathAnimation = Animation;

	const float EffectivePlayRate = FMath::Max(SafePlayRate * Animation->RateScale, TDCharacterAnimationPlayback::MinimumActionPlayRate);
	const float DeathSeconds = FMath::Max(EndTime / EffectivePlayRate, TDCharacterAnimationPlayback::MinimumDeathSeconds);
	UWorld* World = GetWorld();
	if (EndTime >= AnimationLength || !World)
	{
		return DeathSeconds;
	}

	FTimerHandle HoldDeathPoseTimer;
	World->GetTimerManager().SetTimer(HoldDeathPoseTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (bIsPlayingDeath && ActionRequest)
		{
			TDCharacterAnimationPlayback::SetInjectedSequencePlayRate(*ActionRequest, 0.f);
		}
	}), DeathSeconds, false);
	return DeathSeconds;
}

void UTDCharacterAnimationComponent::SetAnimationFrozen(bool bIsFrozen)
{
	if (bIsAnimationFrozen == bIsFrozen)
	{
		return;
	}
	bIsAnimationFrozen = bIsFrozen;
	UUAFComponent* AnimationSystem = ResolveUAFComponent();
	if (!AnimationSystem)
	{
		return;
	}
	AnimationSystem->SetActive(!bIsFrozen);
}

UUAFComponent* UTDCharacterAnimationComponent::ResolveUAFComponent() const
{
	const AActor* Owner = GetOwner();
	if (const ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(Owner))
	{
		return Character->GetUAFComponent();
	}
	return Owner ? Owner->FindComponentByClass<UUAFComponent>() : nullptr;
}

void UTDCharacterAnimationComponent::ApplyBaseAsset()
{
	UAnimSequence* FallbackIdleClip = LocomotionClips.Idle ? LocomotionClips.Idle.Get() : (LocomotionClips.Walk ? LocomotionClips.Walk.Get() : LocomotionClips.Run.Get());
	const UObject* BaseAsset = bUsesBlendSpace ? static_cast<const UObject*>(LocomotionBlendSpace.Get()) : FallbackIdleClip;
	UUAFComponent* AnimationSystem = ResolveUAFComponent();
	if (!BaseAsset || !AnimationSystem || bIsPlayingDeath)
	{
		return;
	}

	TInstancedStruct<FUAFSystemFactoryAsset> SystemAsset = UE::UAF::FAssetDataFactory::CreateUAFAssetDataFromObject<FUAFSystemFactoryAsset>(BaseAsset);
	if (!SystemAsset.IsValid())
	{
		return;
	}

	StopAction(TDCharacterAnimationPlayback::ActionBlendOutSeconds);
	ActionRequest.Reset();
	LocomotionRequest.Reset();
	CurrentLocomotionPose = ETDLocomotionPose::None;
	CurrentLocomotionPlayRate = 1.f;
	LandingSecondsRemaining = 0.f;

	if (AnimationSystem->IsRegistered())
	{
		AnimationSystem->UnregisterComponent();
	}
	AnimationSystem->SetAsset(MoveTemp(SystemAsset));
	AnimationSystem->RegisterComponent();

	bHasBaseAsset = AnimationSystem->GetSystemReference().IsValid();
	if (!bHasBaseAsset)
	{
		return;
	}
	TDCharacterAnimationPlayback::LinkMovementAfterAnimation(*AnimationSystem, *this);
	if (!bUsesBlendSpace)
	{
		AnimationSystem->SetVariable(TDCharacterAnimationPlayback::GetSequencePlayRateVariable(), LocomotionClips.IdlePlayRate);
	}
}

void UTDCharacterAnimationComponent::UpdateBlendSpaceInputs() const
{
	UUAFComponent* AnimationSystem = ResolveUAFComponent();
	const AActor* Owner = GetOwner();
	if (!AnimationSystem || !Owner || !LocomotionBlendSpace)
	{
		return;
	}

	const FVector Velocity = GetOwnerVelocity();
	const float Speed = Velocity.Size2D();
	const float Direction = TDCharacterAnimationPlayback::ComputeSignedMoveDirection(Velocity, Owner->GetActorRotation());
	const float XAxisValue = TDCharacterAnimationPlayback::ResolveBlendAxisValue(LocomotionBlendSpace->GetBlendParameter(0).DisplayName, Speed, Direction);
	const float YAxisValue = TDCharacterAnimationPlayback::ResolveBlendAxisValue(LocomotionBlendSpace->GetBlendParameter(1).DisplayName, Speed, Direction);
	AnimationSystem->SetVariable(TDCharacterAnimationPlayback::GetBlendSpaceXVariable(), XAxisValue);
	AnimationSystem->SetVariable(TDCharacterAnimationPlayback::GetBlendSpaceYVariable(), YAxisValue);
}

void UTDCharacterAnimationComponent::UpdateLocomotionClips(float DeltaTime)
{
	const float PlanarSpeed = GetOwnerVelocity().Size2D();
	ETDLocomotionPose Pose = ChooseGroundPose(PlanarSpeed);
	if (Pose == ETDLocomotionPose::Run && !LocomotionClips.Run)
	{
		Pose = ETDLocomotionPose::Walk;
	}
	if (Pose == ETDLocomotionPose::Walk && !LocomotionClips.Walk)
	{
		Pose = ETDLocomotionPose::Idle;
	}
	if (Pose == ETDLocomotionPose::Idle)
	{
		CurrentLocomotionPose = ETDLocomotionPose::Idle;
		ClearLocomotionInjection();
		return;
	}

	UAnimSequence* Animation = Pose == ETDLocomotionPose::Run ? LocomotionClips.Run.Get() : LocomotionClips.Walk.Get();
	const float PlayRate = ComputeLocomotionPlayRate(Pose, PlanarSpeed);
	if (Pose != CurrentLocomotionPose)
	{
		InjectLocomotionPose(Pose, Animation, PlayRate, true);
		return;
	}
	if (!LocomotionRequest || FMath::Abs(PlayRate - CurrentLocomotionPlayRate) <= CurrentLocomotionPlayRate * TDCharacterAnimationPlayback::LocomotionPlayRateTolerance)
	{
		return;
	}
	if (!LocomotionRequest->IsPlaying())
	{
		InjectLocomotionPose(Pose, Animation, PlayRate, true);
		return;
	}
	TDCharacterAnimationPlayback::SetInjectedSequencePlayRate(*LocomotionRequest, PlayRate);
	CurrentLocomotionPlayRate = PlayRate;
}

void UTDCharacterAnimationComponent::UpdateAirborneClips()
{
	const bool bIsAirborne = IsOwnerAirborne();
	const bool bJustLeftGround = bIsAirborne && !bWasAirborne;
	const bool bJustLanded = !bIsAirborne && bWasAirborne;
	bWasAirborne = bIsAirborne;
	if (!AirborneClips.JumpStart && !AirborneClips.FallLoop && !AirborneClips.Land)
	{
		return;
	}

	if (bIsAirborne)
	{
		const float VerticalSpeed = GetOwnerVelocity().Z;
		if (bJustLeftGround && VerticalSpeed > 0.f && AirborneClips.JumpStart)
		{
			InjectLocomotionPose(ETDLocomotionPose::JumpStart, AirborneClips.JumpStart.Get(), 1.f, false);
			return;
		}
		const bool bIsJumpStartPlaying = CurrentLocomotionPose == ETDLocomotionPose::JumpStart
			&& VerticalSpeed > 0.f
			&& LocomotionRequest
			&& !LocomotionRequest->IsBlendingOut()
			&& !LocomotionRequest->HasCompleted();
		if (bIsJumpStartPlaying || CurrentLocomotionPose == ETDLocomotionPose::FallLoop || !AirborneClips.FallLoop)
		{
			return;
		}
		InjectLocomotionPose(ETDLocomotionPose::FallLoop, AirborneClips.FallLoop.Get(), 1.f, true);
		return;
	}

	if (bJustLanded)
	{
		if (AirborneClips.Land)
		{
			InjectLocomotionPose(ETDLocomotionPose::Land, AirborneClips.Land.Get(), 1.f, false);
			LandingSecondsRemaining = AirborneClips.Land->GetPlayLength();
			return;
		}
		CurrentLocomotionPose = ETDLocomotionPose::None;
		return;
	}

	if (CurrentLocomotionPose != ETDLocomotionPose::Land)
	{
		return;
	}
	const bool bIsMovingOnGround = GetOwnerVelocity().Size2D() >= TDCharacterAnimationPlayback::IdleSpeedThreshold;
	if (LandingSecondsRemaining > 0.f && !bIsMovingOnGround)
	{
		return;
	}
	LandingSecondsRemaining = 0.f;
	CurrentLocomotionPose = ETDLocomotionPose::None;
}

void UTDCharacterAnimationComponent::AdvanceAction(float DeltaTime)
{
	ActiveAction.PreviousAnimationTime = ActiveAction.AnimationTime;
	ActiveAction.AnimationTime = FMath::Min(ActiveAction.AnimationTime + DeltaTime * ActiveAction.PlayRate, ActiveAction.EndSeconds);
	if (ActiveAction.AnimationTime < ActiveAction.EndSeconds)
	{
		return;
	}
	FinishAction(false, TDCharacterAnimationPlayback::ActionBlendOutSeconds);
}

void UTDCharacterAnimationComponent::FinishAction(bool bWasInterrupted, float BlendOutSeconds)
{
	if (!ActiveAction.Animation)
	{
		return;
	}
	const UAnimSequence* EndedAnimation = ActiveAction.Animation;

	if (ActionRequest && TDCharacterAnimationPlayback::IsInjectionPending(*ActionRequest))
	{
		LocomotionRequest = MoveTemp(ActionRequest);
	}
	TDCharacterAnimationPlayback::UninjectAndReset(ActionRequest);
	SetRootMotionEnabled(false);
	ActiveAction = FTDActiveAction();
	CurrentLocomotionPose = ETDLocomotionPose::None;
	LandingSecondsRemaining = 0.f;

	OnActionAnimationEnded.Broadcast(EndedAnimation, bWasInterrupted);
}

void UTDCharacterAnimationComponent::InjectLocomotionPose(ETDLocomotionPose Pose, UAnimSequence* Animation, float PlayRate, bool bShouldLoop)
{
	UUAFComponent* AnimationSystem = ResolveUAFComponent();
	if (!AnimationSystem || !Animation || !bHasBaseAsset)
	{
		return;
	}
	LocomotionRequest = TDCharacterAnimationPlayback::InjectSequence(*AnimationSystem, Animation, PlayRate, 0.f, bShouldLoop, EAnimNextInjectionLifetimeType::Auto, TDCharacterAnimationPlayback::LocomotionBlendSeconds, TDCharacterAnimationPlayback::LocomotionBlendSeconds);
	CurrentLocomotionPose = Pose;
	CurrentLocomotionPlayRate = PlayRate;
}

void UTDCharacterAnimationComponent::ClearLocomotionInjection()
{
	if (!LocomotionRequest || TDCharacterAnimationPlayback::IsInjectionPending(*LocomotionRequest))
	{
		return;
	}
	TDCharacterAnimationPlayback::UninjectAndReset(LocomotionRequest);
}

void UTDCharacterAnimationComponent::SetRootMotionEnabled(bool bIsEnabled)
{
	if (bIsRootMotionEnabled == bIsEnabled)
	{
		return;
	}
	UMoverComponent* MoverComponent = TDCharacterAnimationPlayback::FindMoverComponent(GetOwner());
	if (!MoverComponent || !MoverComponent->HasBegunPlay())
	{
		bIsRootMotionEnabled = false;
		return;
	}

	bIsRootMotionEnabled = bIsEnabled;
	if (bIsEnabled)
	{
		bHasPendingRootMotionCancel = false;
		MoverComponent->QueueLayeredMove(MakeShared<FLayeredMove_RootMotionAttribute>());
		return;
	}
	MoverComponent->CancelFeaturesWithTag(Mover_AnimRootMotion_MeshAttribute);
	bHasPendingRootMotionCancel = true;
}

void UTDCharacterAnimationComponent::CancelPendingRootMotion()
{
	if (!bHasPendingRootMotionCancel || bIsRootMotionEnabled)
	{
		return;
	}
	bHasPendingRootMotionCancel = false;
	if (UMoverComponent* MoverComponent = TDCharacterAnimationPlayback::FindMoverComponent(GetOwner()))
	{
		MoverComponent->CancelFeaturesWithTag(Mover_AnimRootMotion_MeshAttribute);
	}
}

float UTDCharacterAnimationComponent::ComputeLocomotionPlayRate(ETDLocomotionPose Pose, float PlanarSpeed) const
{
	if (Pose == ETDLocomotionPose::Idle)
	{
		return LocomotionClips.IdlePlayRate;
	}
	const float ReferenceSpeed = Pose == ETDLocomotionPose::Run ? LocomotionClips.RunClipSpeed : LocomotionClips.WalkClipSpeed;
	return FMath::Clamp(PlanarSpeed / FMath::Max(ReferenceSpeed, 1.f), TDCharacterAnimationPlayback::MinimumLocomotionPlayRate, TDCharacterAnimationPlayback::MaximumLocomotionPlayRate);
}

UTDCharacterAnimationComponent::ETDLocomotionPose UTDCharacterAnimationComponent::ChooseGroundPose(float PlanarSpeed) const
{
	if (PlanarSpeed < TDCharacterAnimationPlayback::IdleSpeedThreshold)
	{
		return ETDLocomotionPose::Idle;
	}
	const float RunThreshold = (LocomotionClips.WalkClipSpeed + LocomotionClips.RunClipSpeed) * 0.5f;
	const float RunMargin = RunThreshold * TDCharacterAnimationPlayback::RunHysteresisRatio;
	const float ActiveRunThreshold = CurrentLocomotionPose == ETDLocomotionPose::Run ? RunThreshold - RunMargin : RunThreshold + RunMargin;
	return PlanarSpeed > ActiveRunThreshold ? ETDLocomotionPose::Run : ETDLocomotionPose::Walk;
}

FVector UTDCharacterAnimationComponent::GetOwnerVelocity() const
{
	const AActor* Owner = GetOwner();
	if (const UMoverComponent* MoverComponent = TDCharacterAnimationPlayback::FindMoverComponent(Owner))
	{
		return MoverComponent->GetVelocity();
	}
	return Owner ? Owner->GetVelocity() : FVector::ZeroVector;
}

bool UTDCharacterAnimationComponent::IsOwnerAirborne() const
{
	const ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(GetOwner());
	return Character && Character->IsAirborne();
}
