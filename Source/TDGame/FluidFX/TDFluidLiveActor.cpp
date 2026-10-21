#include "FluidFX/TDFluidLiveActor.h"
#include "CollisionQueryParams.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr float TeleportSpeedCmPerSec = 3000.0f;
	constexpr float SleepDistanceRatio = 1.1f;
	constexpr float TerrainTraceStartHeightCm = 2000.0f;
	constexpr float TerrainTraceEndDepthCm = 4000.0f;
	constexpr float PlaneMeshBaseSizeCm = 100.0f;
	constexpr float InteractionVolumeHalfHeightCm = 300.0f;
	constexpr float ActivationVolumeHalfHeightCm = 1000.0f;
	constexpr float UnitlessLightIntensityReference = 1000.0f;
	constexpr float CandelasLightIntensityReference = 10.0f;
	constexpr float DirectionalLightIntensityReference = 10.0f;
	constexpr float MaxVolumeLightIntensityScale = 8.0f;
	constexpr float FullSphereSteradians = static_cast<float>(4.0 * UE_PI);
	constexpr float MouseBrushBaseDensityStrength = 1.0f;
	constexpr int32 MaximumActorShadowCapsuleCount = 8;

	FTDFluidShadowCapsuleBonePair MakeShadowCapsuleBonePair(const TCHAR* StartBoneName, const TCHAR* EndBoneName, float RadiusCm)
	{
		FTDFluidShadowCapsuleBonePair BonePair;
		BonePair.StartBoneName = FName(StartBoneName);
		BonePair.EndBoneName = FName(EndBoneName);
		BonePair.RadiusCm = RadiusCm;
		return BonePair;
	}

	void AppendActorShadowCapsules(const AActor& SourceActor, TConstArrayView<FTDFluidShadowCapsuleBonePair> BonePairs, TArray<FTDFluidShadowCapsule>& InOutCapsules)
	{
		const USkeletalMeshComponent* SkeletalMesh = SourceActor.FindComponentByClass<USkeletalMeshComponent>();
		if (!SkeletalMesh)
		{
			return;
		}

		for (const FTDFluidShadowCapsuleBonePair& BonePair : BonePairs)
		{
			if (InOutCapsules.Num() >= MaximumActorShadowCapsuleCount)
			{
				return;
			}

			if (!SkeletalMesh->DoesSocketExist(BonePair.StartBoneName) || !SkeletalMesh->DoesSocketExist(BonePair.EndBoneName))
			{
				continue;
			}

			FTDFluidShadowCapsule& Capsule = InOutCapsules.AddDefaulted_GetRef();
			Capsule.StartWorld = SkeletalMesh->GetSocketLocation(BonePair.StartBoneName);
			Capsule.EndWorld = SkeletalMesh->GetSocketLocation(BonePair.EndBoneName);
			Capsule.RadiusCm = BonePair.RadiusCm;
		}
	}

	struct FTDFluidVolumeLightParameters
	{
		float Mode = 0.0f;
		float Radius = 0.0f;
		FVector Position = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		FLinearColor Color = FLinearColor::Black;
	};

	bool IsStampableVelocity(const FVector& Velocity, float MinSpeedCmPerSec)
	{
		const float PlanarSpeed = Velocity.Size2D();
		return PlanarSpeed >= MinSpeedCmPerSec && PlanarSpeed <= TeleportSpeedCmPerSec;
	}

	bool IsCellOutsideGrid(const FIntPoint& Cell, int32 Resolution)
	{
		return Cell.X < 0 || Cell.X >= Resolution || Cell.Y < 0 || Cell.Y >= Resolution;
	}

	void ApplyActorBrushSettings(FTDFluidBrushStamp& Stamp, const FTDFluidSimulationConfig& Config)
	{
		Stamp.DensityStrength *= Config.ActorBrushDensityStrength;
		Stamp.RadialVelocityCmPerSec = Config.ActorBrushPunctureCmPerSec;
		Stamp.RippleImpulseCm = Config.ActorBrushRippleImpulseCm;
	}

	void PlaceComponentIfChanged(USceneComponent& TargetComponent, const FVector& WorldLocation, const FQuat& WorldRotation)
	{
		if (TargetComponent.GetComponentLocation().Equals(WorldLocation, 0.01) && TargetComponent.GetComponentQuat().Equals(WorldRotation, 0.0001))
		{
			return;
		}

		TargetComponent.SetWorldLocationAndRotation(WorldLocation, WorldRotation);
	}

	float ResolvePointLightIntensityScale(const UPointLightComponent& PointLight)
	{
		switch (PointLight.IntensityUnits)
		{
		case ELightUnits::Candelas:
			return PointLight.Intensity / CandelasLightIntensityReference;
		case ELightUnits::Lumens:
			return PointLight.Intensity / (CandelasLightIntensityReference * FullSphereSteradians);
		case ELightUnits::EV:
			return FMath::Pow(2.0f, PointLight.Intensity) / CandelasLightIntensityReference;
		default:
			return PointLight.Intensity / UnitlessLightIntensityReference;
		}
	}

	FTDFluidVolumeLightParameters ResolveVolumeLightParameters(const AActor* LightActor)
	{
		FTDFluidVolumeLightParameters Parameters;
		ULightComponent* LightComponent = LightActor ? LightActor->FindComponentByClass<ULightComponent>() : nullptr;
		if (!LightComponent || !LightComponent->GetVisibleFlag())
		{
			return Parameters;
		}

		const UDirectionalLightComponent* DirectionalLight = Cast<UDirectionalLightComponent>(LightComponent);
		const UPointLightComponent* PointLight = Cast<UPointLightComponent>(LightComponent);
		if (!DirectionalLight && !PointLight)
		{
			return Parameters;
		}

		const float RawIntensityScale = DirectionalLight ? DirectionalLight->Intensity / DirectionalLightIntensityReference : ResolvePointLightIntensityScale(*PointLight);
		const float IntensityScale = FMath::Clamp(RawIntensityScale, 0.0f, MaxVolumeLightIntensityScale);
		const FLinearColor BaseColor = LightComponent->GetLightColor();
		Parameters.Mode = DirectionalLight ? 2.0f : 1.0f;
		Parameters.Radius = PointLight ? PointLight->AttenuationRadius : 0.0f;
		Parameters.Position = LightComponent->GetComponentLocation();
		Parameters.Direction = LightComponent->GetForwardVector();
		Parameters.Color = FLinearColor(BaseColor.R * IntensityScale, BaseColor.G * IntensityScale, BaseColor.B * IntensityScale, 1.0f);
		return Parameters;
	}
}

ATDFluidLiveActor::ATDFluidLiveActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ActivationVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ActivationVolume"));
	ActivationVolume->SetupAttachment(SceneRoot);
	ActivationVolume->SetBoxExtent(FVector(5000.0f, 5000.0f, 1000.0f));
	ActivationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	InteractionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionVolume"));
	InteractionVolume->SetupAttachment(SceneRoot);
	InteractionVolume->SetBoxExtent(FVector(2000.0f, 2000.0f, 300.0f));
	InteractionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionVolume->SetCollisionResponseToAllChannels(ECR_Overlap);

	InternalSurfaceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InternalSurfaceMesh"));
	InternalSurfaceMesh->SetupAttachment(SceneRoot);
	InternalSurfaceMesh->SetMobility(EComponentMobility::Movable);
	InternalSurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InternalSurfaceMesh->SetCastShadow(false);

	VolumeMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VolumeMeshComponent"));
	VolumeMeshComponent->SetupAttachment(SceneRoot);
	VolumeMeshComponent->SetMobility(EComponentMobility::Movable);
	VolumeMeshComponent->SetAbsolute(true, true, true);
	VolumeMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VolumeMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	VolumeMeshComponent->SetGenerateOverlapEvents(false);
	VolumeMeshComponent->SetCastShadow(false);
	VolumeMeshComponent->SetVisibility(false);

	ShadowCapsuleBones = {
		MakeShadowCapsuleBonePair(TEXT("pelvis"), TEXT("spine_05"), 18.0f),
		MakeShadowCapsuleBonePair(TEXT("spine_05"), TEXT("head"), 11.0f),
		MakeShadowCapsuleBonePair(TEXT("thigh_l"), TEXT("calf_l"), 8.0f),
		MakeShadowCapsuleBonePair(TEXT("calf_l"), TEXT("foot_l"), 7.0f),
		MakeShadowCapsuleBonePair(TEXT("thigh_r"), TEXT("calf_r"), 8.0f),
		MakeShadowCapsuleBonePair(TEXT("calf_r"), TEXT("foot_r"), 7.0f),
		MakeShadowCapsuleBonePair(TEXT("upperarm_l"), TEXT("hand_l"), 6.0f),
		MakeShadowCapsuleBonePair(TEXT("upperarm_r"), TEXT("hand_r"), 6.0f)
	};

	LiveComponent = CreateDefaultSubobject<UTDFluidLiveComponent>(TEXT("LiveComponent"));

	TrackedBoneNames = {
		FName(TEXT("foot_l")),
		FName(TEXT("foot_r")),
		FName(TEXT("hand_r")),
		FName(TEXT("pelvis"))
	};
}

void ATDFluidLiveActor::BeginPlay()
{
	Super::BeginPlay();

	if (LiveComponent)
	{
		LiveComponent->BindInternalMeshComponent(InternalSurfaceMesh);
		LiveComponent->BindVolumeMeshComponent(VolumeMeshComponent);
	}

	SyncVolumeExtentsWithSimulation();

	if (UWorld* World = GetWorld())
	{
		if (UTDFluidLiveSubsystem* FluidSubsystem = World->GetSubsystem<UTDFluidLiveSubsystem>())
		{
			FluidSubsystem->RegisterLiveActor(this);
		}
	}
}

void ATDFluidLiveActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UTDFluidLiveSubsystem* FluidSubsystem = World->GetSubsystem<UTDFluidLiveSubsystem>())
		{
			FluidSubsystem->UnregisterLiveActor(this);
		}
	}

	ClearInteractionState();
	PendingTerrainCells.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATDFluidLiveActor::InitializeFromPreset(UTDFluidPresetAsset* InPreset, float InActiveDurationSeconds)
{
	bHasFiniteLifetime = InActiveDurationSeconds > 0.0f;
	RemainingLifetimeSeconds = FMath::Max(InActiveDurationSeconds, 0.0f);
	bIsPooledInactive = false;

	if (InPreset)
	{
		InteractionBrushChannel = InPreset->DefaultChannel;
	}

	if (LiveComponent)
	{
		LiveComponent->SetSimulationAwake(true);
		LiveComponent->InitSystemStateFromPreset(InPreset);
	}

	ResetInteractionHistory();
	bIsTerrainQueueCurrent = false;
	bHasSteppedWhileAwake = false;
	SyncVolumeExtentsWithSimulation();
}

void ATDFluidLiveActor::RegisterInteractingActor(AActor* InteractingActor)
{
	if (!InteractingActor || InteractingActor == this)
	{
		return;
	}

	BindCombatComponentIfPresent(InteractingActor);

	for (const TWeakObjectPtr<AActor>& Existing : RegisteredInteractingActors)
	{
		if (Existing.Get() == InteractingActor)
		{
			return;
		}
	}

	RegisteredInteractingActors.Add(InteractingActor);
}

void ATDFluidLiveActor::UnregisterInteractingActor(AActor* InteractingActor)
{
	if (!InteractingActor)
	{
		return;
	}

	RegisteredInteractingActors.RemoveAllSwap([InteractingActor](const TWeakObjectPtr<AActor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == InteractingActor;
	});
	PreviousActorLocations.Remove(FObjectKey(InteractingActor));
}

void ATDFluidLiveActor::EvaluateActivationProximity(const FVector& ObserverWorldLocation)
{
	if (!bShouldUseActivationVolume || !LiveComponent)
	{
		return;
	}

	const float ThresholdCm = LiveComponent->IsSimulationAwake() ? ActivationDistanceCm * SleepDistanceRatio : ActivationDistanceCm;
	const float DistanceSq = FVector::DistSquared2D(GetActorLocation(), ObserverWorldLocation);
	LiveComponent->SetSimulationAwake(DistanceSq <= FMath::Square(ThresholdCm));
}

void ATDFluidLiveActor::StepLiveActor(float DeltaTime)
{
	if (!BeginLiveActorStep(DeltaTime))
	{
		return;
	}

	RunLiveActorSolverStep(DeltaTime);
	FinishLiveActorStep();
}

bool ATDFluidLiveActor::BeginLiveActorStep(float DeltaTime)
{
	bHasPreparedSolverStep = false;
	if (bIsPooledInactive || !LiveComponent || DeltaTime <= 0.0f)
	{
		return false;
	}

	if (bHasFiniteLifetime)
	{
		RemainingLifetimeSeconds -= DeltaTime;
		if (RemainingLifetimeSeconds <= 0.0f)
		{
			return false;
		}
	}

	UpdatePlaneBasis();
	SyncVolumeExtentsWithSimulation();

	if (bShouldUseActivationVolume)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
			{
				EvaluateActivationProximity(PlayerPawn->GetActorLocation());
			}
		}
	}

	if (!LiveComponent->IsSimulationAwake())
	{
		bHasSteppedWhileAwake = false;
		return false;
	}

	if (!bHasSteppedWhileAwake)
	{
		bHasSteppedWhileAwake = true;
		ResetInteractionHistory();
		bIsTerrainQueueCurrent = false;
	}

	LastStepDeltaSeconds = DeltaTime;
	PrepareActorSpecificInputs(DeltaTime);
	if (ShouldHarvestInteractions())
	{
		CollectOverlappingAndRegisteredInputs(DeltaTime);
	}
	PollMouseBrush(DeltaTime);
	InvalidateTerrainQueueIfSolverGridChanged();

	const FVector2D DesiredCenter = ResolveDesiredSimulationCenter();
	CenterBeforeSolverStep = LiveComponent->GetSimulationCenterWorld();
	bHasPreparedSolverStep = LiveComponent->PrepareSimulationStep(DeltaTime, DesiredCenter, bShouldFollowPlayerPawn);

	if (bShouldFollowPlayerPawn)
	{
		if (LiveComponent->GetPlaneBasis().IsWorldFacing())
		{
			const FVector2D SnappedCenter = LiveComponent->GetSimulationCenterWorld();
			const FVector CurrentLoc = GetActorLocation();
			SetActorLocation(FVector(SnappedCenter.X, SnappedCenter.Y, CurrentLoc.Z));
		}
		else
		{
			SetActorLocation(ResolvePlaneAnchorWorldLocation());
		}
	}

	PlaceChildrenOnSolverPlane();
	if (bHasPreparedSolverStep)
	{
		PushVolumeTransmittanceSources();
	}
	return true;
}

void ATDFluidLiveActor::RunLiveActorSolverStep(float DeltaTime)
{
	if (!bHasPreparedSolverStep)
	{
		return;
	}

	LiveComponent->RunPreparedSolverStep(DeltaTime);
}

void ATDFluidLiveActor::FinishLiveActorStep()
{
	if (bHasPreparedSolverStep)
	{
		bHasPreparedSolverStep = false;
		LiveComponent->FinishSimulationStep();
		ApplyVolumeLightToMaterial();
		FinishActorSpecificOutputs(LastStepDeltaSeconds);
	}

	if (!LiveComponent->GetPlaneBasis().IsWorldFacing())
	{
		return;
	}

	ShiftTerrainQueueWithWindow(CenterBeforeSolverStep);
	SampleQueuedTerrainWithinBudget();
}

void ATDFluidLiveActor::SetPooledInactive(bool bInactive)
{
	const bool bWasPooledInactive = bIsPooledInactive;
	bIsPooledInactive = bInactive;
	SetActorHiddenInGame(bInactive);
	bHasSteppedWhileAwake = false;
	bHasPreparedSolverStep = false;
	bIsTerrainQueueCurrent = false;
	PendingTerrainCells.Reset();

	if (bInactive)
	{
		bHasFiniteLifetime = false;
		RemainingLifetimeSeconds = 0.0f;
		ClearInteractionState();

		if (LiveComponent)
		{
			LiveComponent->SetSimulationAwake(false);
			LiveComponent->SetSdfObstacles(TArray<FTDFluidSdfObstacle>());
			LiveComponent->ResetSimulationState();
			LiveComponent->PublishCurrentOutputs();
		}
		return;
	}

	if (!LiveComponent)
	{
		return;
	}

	if (bWasPooledInactive)
	{
		ClearInteractionState();
		LiveComponent->SetSdfObstacles(TArray<FTDFluidSdfObstacle>());
	}
	LiveComponent->SetSimulationAwake(true);
}

void ATDFluidLiveActor::SyncVolumeExtentsWithSimulation()
{
	if (!LiveComponent)
	{
		return;
	}

	if (!LiveComponent->IsSimulationInitialized())
	{
		return;
	}

	const FVector2D ExtentCm = LiveComponent->GetSimulationExtentCm();
	if (InteractionVolume)
	{
		const float InteractionHalfDepthCm = LiveComponent->GetPlaneBasis().IsWorldFacing()
			? InteractionVolumeHalfHeightCm
			: LiveComponent->GetPlaneSettings().InteractionDepthCm;
		const FVector DesiredExtent(ExtentCm.X * 0.5f, ExtentCm.Y * 0.5f, InteractionHalfDepthCm);
		if (!InteractionVolume->GetUnscaledBoxExtent().Equals(DesiredExtent, 0.01f))
		{
			InteractionVolume->SetBoxExtent(DesiredExtent);
		}
	}
	if (ActivationVolume)
	{
		const FVector DesiredExtent(ActivationDistanceCm, ActivationDistanceCm, ActivationVolumeHalfHeightCm);
		if (!ActivationVolume->GetUnscaledBoxExtent().Equals(DesiredExtent, 0.01f))
		{
			ActivationVolume->SetBoxExtent(DesiredExtent);
		}
	}
	if (InternalSurfaceMesh)
	{
		const FVector DesiredScale(ExtentCm.X / PlaneMeshBaseSizeCm, ExtentCm.Y / PlaneMeshBaseSizeCm, 1.0f);
		if (!InternalSurfaceMesh->GetRelativeScale3D().Equals(DesiredScale, 0.0001f))
		{
			InternalSurfaceMesh->SetRelativeScale3D(DesiredScale);
		}
	}
}

void ATDFluidLiveActor::CollectOverlappingAndRegisteredInputs(float DeltaTime)
{
	TArray<AActor*> CandidateActors;
	if (InteractionVolume)
	{
		InteractionVolume->GetOverlappingActors(CandidateActors);
	}

	for (int32 Idx = RegisteredInteractingActors.Num() - 1; Idx >= 0; --Idx)
	{
		AActor* RegisteredActor = RegisteredInteractingActors[Idx].Get();
		if (!RegisteredActor)
		{
			RegisteredInteractingActors.RemoveAtSwap(Idx);
			continue;
		}
		CandidateActors.AddUnique(RegisteredActor);
	}

	PruneInvalidCombatBindings();
	CurrentActorLocations.Reset();
	CurrentBoneLocations.Reset();

	for (AActor* Candidate : CandidateActors)
	{
		if (!Candidate || Candidate == this || Candidate->IsA<ATDFluidLiveActor>())
		{
			continue;
		}
		HarvestActorInteractionStamps(Candidate, DeltaTime);
	}

	PreviousActorLocations = MoveTemp(CurrentActorLocations);
	PreviousBoneLocations = MoveTemp(CurrentBoneLocations);
}

void ATDFluidLiveActor::HarvestActorInteractionStamps(AActor* TargetActor, float DeltaTime)
{
	BindCombatComponentIfPresent(TargetActor);

	const FObjectKey ActorKey(TargetActor);
	const FVector CurrentLocation = TargetActor->GetActorLocation();

	if (const FVector* PreviousLocation = PreviousActorLocations.Find(ActorKey))
	{
		const FVector ComputedVelocity = (CurrentLocation - *PreviousLocation) / DeltaTime;
		if (IsStampableVelocity(ComputedVelocity, MinInteractionSpeedCmPerSec))
		{
			FTDFluidBrushStamp PivotStamp;
			PivotStamp.WorldLocation = CurrentLocation;
			PivotStamp.WorldVelocity = ComputedVelocity;
			PivotStamp.RadiusCm = ActorPivotBrushRadiusCm;
			PivotStamp.DensityStrength = 0.75f;
			PivotStamp.VelocityStrength = 1.0f;
			PivotStamp.Channel = InteractionBrushChannel;
			ApplyActorBrushSettings(PivotStamp, LiveComponent->SimulationConfig);
			LiveComponent->QueueBrushStamp(PivotStamp);
		}
	}
	CurrentActorLocations.Add(ActorKey, CurrentLocation);

	if (TrackedBoneNames.IsEmpty())
	{
		return;
	}

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalComponents(TargetActor);
	for (USkeletalMeshComponent* SkelMesh : SkeletalComponents)
	{
		if (!SkelMesh)
		{
			continue;
		}

		const FObjectKey MeshKey(SkelMesh);
		for (const FName& BoneName : TrackedBoneNames)
		{
			if (BoneName.IsNone() || SkelMesh->GetBoneIndex(BoneName) == INDEX_NONE)
			{
				continue;
			}

			const FVector BoneLocation = SkelMesh->GetBoneLocation(BoneName);
			const TPair<FObjectKey, FName> BoneKey(MeshKey, BoneName);

			if (const FVector* PrevBoneLoc = PreviousBoneLocations.Find(BoneKey))
			{
				const FVector BoneVelocity = (BoneLocation - *PrevBoneLoc) / DeltaTime;
				if (IsStampableVelocity(BoneVelocity, MinInteractionSpeedCmPerSec))
				{
					FTDFluidBrushStamp BoneStamp;
					BoneStamp.WorldLocation = BoneLocation;
					BoneStamp.WorldVelocity = BoneVelocity;
					BoneStamp.RadiusCm = BoneBrushRadiusCm;
					BoneStamp.DensityStrength = 0.9f;
					BoneStamp.VelocityStrength = 1.0f;
					BoneStamp.Channel = InteractionBrushChannel;
					ApplyActorBrushSettings(BoneStamp, LiveComponent->SimulationConfig);
					LiveComponent->QueueBrushStamp(BoneStamp);
				}
			}

			CurrentBoneLocations.Add(BoneKey, BoneLocation);
		}
	}
}

void ATDFluidLiveActor::BindCombatComponentIfPresent(AActor* TargetActor)
{
	if (!TargetActor)
	{
		return;
	}

	UTDCombatComponent* CombatComp = TargetActor->FindComponentByClass<UTDCombatComponent>();
	if (!CombatComp)
	{
		return;
	}

	const FObjectKey CompKey(CombatComp);
	if (CombatBindings.Contains(CompKey))
	{
		return;
	}

	TWeakObjectPtr<ATDFluidLiveActor> WeakThis(this);
	TWeakObjectPtr<AActor> WeakTarget(TargetActor);

	FTDFluidCombatBinding& Binding = CombatBindings.Add(CompKey);
	Binding.CombatComponent = CombatComp;

	Binding.DamageHandle = CombatComp->OnDamaged.AddLambda([WeakThis, WeakTarget](const FTDDamageResult& Result, const FTDDamageContext& Context)
	{
		const ATDFluidLiveActor* Self = WeakThis.Get();
		AActor* DamagedTarget = WeakTarget.Get();
		if (!Self || !DamagedTarget || Result.AppliedDamage <= 0.0f)
		{
			return;
		}

		const UWorld* World = Self->GetWorld();
		UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		if (!FluidSubsystem)
		{
			return;
		}

		FluidSubsystem->QueueCombatDamage(
			Context.Caster.Get(),
			DamagedTarget,
			Result.AppliedDamage,
			Result.Element,
			true,
			Result.bWasCritical,
			Result.Delivery,
			Result.bHasHitLocation,
			Result.HitLocation,
			Result.HitDirection);
	});

	Binding.DeathHandle = CombatComp->OnDeath.AddLambda([WeakThis, WeakTarget](const FTDDamageContext& Context)
	{
		const ATDFluidLiveActor* Self = WeakThis.Get();
		AActor* DeadTarget = WeakTarget.Get();
		if (!Self || !DeadTarget)
		{
			return;
		}

		const UWorld* World = Self->GetWorld();
		UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		if (!FluidSubsystem)
		{
			return;
		}

		FluidSubsystem->QueueCombatDeath(DeadTarget, Context.Caster.Get(), DeadTarget->GetActorLocation());
	});
}

void ATDFluidLiveActor::PruneInvalidCombatBindings()
{
	for (auto BindingIterator = CombatBindings.CreateIterator(); BindingIterator; ++BindingIterator)
	{
		if (!BindingIterator.Value().CombatComponent.IsValid())
		{
			BindingIterator.RemoveCurrent();
		}
	}
}

void ATDFluidLiveActor::ReleaseCombatBindings()
{
	for (const TPair<FObjectKey, FTDFluidCombatBinding>& Entry : CombatBindings)
	{
		UTDCombatComponent* CombatComp = Entry.Value.CombatComponent.Get();
		if (!CombatComp)
		{
			continue;
		}

		CombatComp->OnDamaged.Remove(Entry.Value.DamageHandle);
		CombatComp->OnDeath.Remove(Entry.Value.DeathHandle);
	}
	CombatBindings.Reset();
}

void ATDFluidLiveActor::QueueMouseBrushAt(const FVector& CursorWorldLocation, float DeltaTime)
{
	if (!LiveComponent || DeltaTime <= 0.0f)
	{
		return;
	}

	FVector2D CursorPlanePoint;
	if (!LiveComponent->TryProjectWorldPointIntoSlab(CursorWorldLocation, CursorPlanePoint))
	{
		ResetMouseBrushStroke();
		return;
	}

	const bool bHadPreviousLocation = bHasPreviousMouseBrushLocation;
	const FVector PreviousLocation = PreviousMouseBrushLocation;
	PreviousMouseBrushLocation = CursorWorldLocation;
	bHasPreviousMouseBrushLocation = true;
	if (!bHadPreviousLocation)
	{
		return;
	}

	const FVector CursorDelta = CursorWorldLocation - PreviousLocation;
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent->GetPlaneBasis();
	const FVector2D PlaneDelta = PlaneBasis.ProjectVector(CursorDelta);
	const FVector2D PlaneVelocity(PlaneDelta.X / DeltaTime, PlaneDelta.Y / DeltaTime);
	const FVector PlanarVelocity(PlaneVelocity.X, PlaneVelocity.Y, 0.0f);
	if (!IsStampableVelocity(PlanarVelocity, MinInteractionSpeedCmPerSec))
	{
		return;
	}

	FTDFluidBrushStamp MouseStamp;
	MouseStamp.WorldLocation = CursorWorldLocation;
	MouseStamp.WorldVelocity = PlaneBasis.UnprojectVector(PlaneVelocity);
	MouseStamp.RadiusCm = MouseBrushRadiusCm;
	MouseStamp.DensityStrength = MouseBrushBaseDensityStrength;
	MouseStamp.VelocityStrength = 1.0f;
	MouseStamp.Channel = InteractionBrushChannel;
	ApplyActorBrushSettings(MouseStamp, LiveComponent->SimulationConfig);
	LiveComponent->QueueBrushStamp(MouseStamp);
}

void ATDFluidLiveActor::PollMouseBrush(float DeltaTime)
{
	if (!bAcceptMouseBrush)
	{
		ResetMouseBrushStroke();
		return;
	}

	const APlayerController* Controller = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	if (!Controller || !Controller->IsInputKeyDown(EKeys::MiddleMouseButton))
	{
		ResetMouseBrushStroke();
		return;
	}

	FVector CursorWorldLocation;
	if (!ResolveMouseBrushWorldLocation(*Controller, CursorWorldLocation))
	{
		ResetMouseBrushStroke();
		return;
	}

	QueueMouseBrushAt(CursorWorldLocation, DeltaTime);
}

bool ATDFluidLiveActor::ResolveMouseBrushWorldLocation(const APlayerController& Controller, FVector& OutLocation) const
{
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent->GetPlaneBasis();
	if (PlaneBasis.IsWorldFacing())
	{
		FHitResult CursorHit;
		if (!Controller.GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), false, CursorHit))
		{
			return false;
		}

		OutLocation = CursorHit.ImpactPoint;
		return true;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!Controller.DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	return PlaneBasis.IntersectRay(RayOrigin, RayDirection, LiveComponent->GetPlaneDepthCm(), OutLocation);
}

void ATDFluidLiveActor::ResetMouseBrushStroke()
{
	bHasPreviousMouseBrushLocation = false;
}

void ATDFluidLiveActor::ApplyVolumeLightToMaterial()
{
	UMaterialInstanceDynamic* VolumeMaterialInstance = VolumeMeshComponent ? Cast<UMaterialInstanceDynamic>(VolumeMeshComponent->GetMaterial(0)) : nullptr;
	if (!VolumeMaterialInstance)
	{
		return;
	}

	const FTDFluidVolumeLightParameters Light = ResolveVolumeLightParameters(VolumeLightActor);
	VolumeMaterialInstance->SetScalarParameterValue(TEXT("VolumeLightMode"), Light.Mode);
	VolumeMaterialInstance->SetScalarParameterValue(TEXT("VolumeLightRadius"), Light.Radius);
	VolumeMaterialInstance->SetVectorParameterValue(TEXT("VolumeLightPosition"), FLinearColor(Light.Position.X, Light.Position.Y, Light.Position.Z, 1.0f));
	VolumeMaterialInstance->SetVectorParameterValue(TEXT("VolumeLightDirection"), FLinearColor(Light.Direction.X, Light.Direction.Y, Light.Direction.Z, 0.0f));
	VolumeMaterialInstance->SetVectorParameterValue(TEXT("VolumeLightColor"), Light.Color);
}

void ATDFluidLiveActor::ResetInteractionHistory()
{
	ResetMouseBrushStroke();
	PreviousActorLocations.Reset();
	PreviousBoneLocations.Reset();
	CurrentActorLocations.Reset();
	CurrentBoneLocations.Reset();
}

void ATDFluidLiveActor::ClearInteractionState()
{
	RegisteredInteractingActors.Reset();
	ResetInteractionHistory();
	ReleaseCombatBindings();
}

void ATDFluidLiveActor::InvalidateTerrainQueueIfSolverGridChanged()
{
	if (!LiveComponent->IsSimulationInitialized())
	{
		return;
	}

	const int32 SimulationResolution = LiveComponent->GetSimulationResolution();
	const FVector2D SimulationExtentCm = LiveComponent->GetSimulationExtentCm();
	const int32 SimulationGeneration = LiveComponent->GetSimulationGeneration();
	const bool bHasSameLayout = SimulationResolution == TerrainQueuedResolution && SimulationExtentCm.Equals(TerrainQueuedExtentCm, 0.01);
	const bool bHasSameGeneration = SimulationGeneration == TerrainQueuedGeneration;
	if (bHasSameLayout && bHasSameGeneration)
	{
		return;
	}

	TerrainQueuedResolution = SimulationResolution;
	TerrainQueuedExtentCm = SimulationExtentCm;
	TerrainQueuedGeneration = SimulationGeneration;
	bIsTerrainQueueCurrent = false;
}

void ATDFluidLiveActor::ShiftTerrainQueueWithWindow(const FVector2D& CenterBeforeStep)
{
	if (!bSampleTerrainElevation || !bIsTerrainQueueCurrent)
	{
		return;
	}

	const int32 Resolution = LiveComponent->GetSimulationResolution();
	if (Resolution <= 0)
	{
		return;
	}

	const FVector2D CellSizeCm = LiveComponent->GetSimulationExtentCm() / static_cast<double>(Resolution);
	const FVector2D CenterShift = LiveComponent->GetSimulationCenterWorld() - CenterBeforeStep;
	const FIntPoint ShiftCells(FMath::RoundToInt(CenterShift.X / CellSizeCm.X), FMath::RoundToInt(CenterShift.Y / CellSizeCm.Y));
	if (ShiftCells == FIntPoint::ZeroValue)
	{
		return;
	}

	if (FMath::Abs(ShiftCells.X) >= Resolution || FMath::Abs(ShiftCells.Y) >= Resolution)
	{
		bIsTerrainQueueCurrent = false;
		return;
	}

	for (int32 Index = PendingTerrainCells.Num() - 1; Index >= 0; --Index)
	{
		const FIntPoint ShiftedCell = PendingTerrainCells[Index] - ShiftCells;
		if (IsCellOutsideGrid(ShiftedCell, Resolution))
		{
			PendingTerrainCells.RemoveAtSwap(Index);
			continue;
		}
		PendingTerrainCells[Index] = ShiftedCell;
	}

	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		const bool bIsNewRow = ShiftCells.Y > 0 ? CellY >= Resolution - ShiftCells.Y : CellY < -ShiftCells.Y;
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const bool bIsNewColumn = ShiftCells.X > 0 ? CellX >= Resolution - ShiftCells.X : CellX < -ShiftCells.X;
			if (bIsNewRow || bIsNewColumn)
			{
				PendingTerrainCells.Add(FIntPoint(CellX, CellY));
			}
		}
	}
}

void ATDFluidLiveActor::SampleQueuedTerrainWithinBudget()
{
	if (!bSampleTerrainElevation)
	{
		PendingTerrainCells.Reset();
		bIsTerrainQueueCurrent = false;
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !LiveComponent->IsSimulationInitialized())
	{
		return;
	}

	if (!bIsTerrainQueueCurrent)
	{
		const int32 Resolution = LiveComponent->GetSimulationResolution();
		PendingTerrainCells.Reset(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				PendingTerrainCells.Add(FIntPoint(CellX, CellY));
			}
		}
		bIsTerrainQueueCurrent = true;
	}

	if (PendingTerrainCells.IsEmpty())
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDFluidTerrainSample), false, this);
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		QueryParams.AddIgnoredActor(PlayerPawn);
	}
	AddTerrainTraceIgnoredActors(QueryParams);
	const FCollisionObjectQueryParams ObjectQueryParams(ECC_WorldStatic);
	const float BaseHeightCm = GetActorLocation().Z;

	int32 RemainingTraces = FMath::Max(MaxTerrainTracesPerTick, 1);
	while (RemainingTraces > 0 && !PendingTerrainCells.IsEmpty())
	{
		const FIntPoint Cell = PendingTerrainCells.Pop(EAllowShrinking::No);
		const FVector2D CellCenter = LiveComponent->CellToPlaneCenter(Cell.X, Cell.Y);
		const FVector TraceStart(CellCenter.X, CellCenter.Y, BaseHeightCm + TerrainTraceStartHeightCm);
		const FVector TraceEnd(CellCenter.X, CellCenter.Y, BaseHeightCm - TerrainTraceEndDepthCm);

		FHitResult Hit;
		const bool bDidHit = World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjectQueryParams, QueryParams);
		LiveComponent->SetTerrainElevationAtCell(Cell.X, Cell.Y, bDidHit ? Hit.ImpactPoint.Z : ResolveTerrainMissElevationCm(Cell.X, Cell.Y));
		--RemainingTraces;
	}

	if (PendingTerrainCells.IsEmpty())
	{
		LiveComponent->CompleteTerrainElevationSampling();
		OnTerrainSamplingCompleted();
	}
}

FVector ATDFluidLiveActor::ResolvePlaneAnchorWorldLocation() const
{
	if (!bShouldFollowPlayerPawn)
	{
		return GetActorLocation();
	}

	if (const AActor* ExplicitFollow = FollowTargetActor.Get())
	{
		return ExplicitFollow->GetActorLocation();
	}

	const UWorld* World = GetWorld();
	const APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	return PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation();
}

FVector2D ATDFluidLiveActor::ResolveDesiredSimulationCenter() const
{
	const FVector AnchorLocation = ResolvePlaneAnchorWorldLocation();
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent->GetPlaneBasis();
	if (PlaneBasis.IsWorldFacing())
	{
		return FVector2D(AnchorLocation.X, AnchorLocation.Y);
	}

	const double CenterOffsetAlongUpCm = LiveComponent->GetPlaneSettings().CenterOffsetAlongUpRatio * LiveComponent->GetSimulationExtentCm().Y;
	return PlaneBasis.ProjectPoint(AnchorLocation) + FVector2D(0.0, CenterOffsetAlongUpCm) + GetDynamicCenterOffsetPlane();
}

void ATDFluidLiveActor::UpdatePlaneBasis()
{
	const FTDFluidPlaneSettings PlaneSettings = LiveComponent->GetPlaneSettings();
	const UWorld* World = GetWorld();
	const UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	const FRotator ViewRotation = FluidSubsystem ? FluidSubsystem->GetPlaneViewRotation() : FTDFluidPlaneBasis::GetDefaultViewRotation();
	const FTDFluidPlaneBasis PlaneBasis = FTDFluidPlaneBasis::MakeFromView(PlaneSettings.Orientation, ViewRotation, PlaneSettings.BillboardLeanBackDegrees);
	LiveComponent->SetPlaneBasis(PlaneBasis, ResolvePlaneAnchorWorldLocation(), PlaneSettings.CameraDepthBiasCm);
}

void ATDFluidLiveActor::PlaceChildrenOnSolverPlane()
{
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent->GetPlaneBasis();
	if (PlaneBasis.IsWorldFacing() || !LiveComponent->IsSimulationInitialized())
	{
		return;
	}

	const FVector PlaneCenter = LiveComponent->UnprojectSolverPlaneToWorld(LiveComponent->GetSimulationCenterWorld());
	const FQuat PlaneRotation = PlaneBasis.GetRotation();
	if (InteractionVolume)
	{
		PlaceComponentIfChanged(*InteractionVolume, PlaneCenter, PlaneRotation);
	}
	if (InternalSurfaceMesh)
	{
		PlaceComponentIfChanged(*InternalSurfaceMesh, PlaneCenter, PlaneRotation);
	}
}

void ATDFluidLiveActor::GatherShadowCapsules(TArray<FTDFluidShadowCapsule>& OutCapsules) const
{
	OutCapsules.Reset();
	if (!bCastPawnCapsuleShadows)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (const APawn* PlayerPawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr)
	{
		AppendActorShadowCapsules(*PlayerPawn, ShadowCapsuleBones, OutCapsules);
	}

	for (const TWeakObjectPtr<AActor>& RegisteredActor : RegisteredInteractingActors)
	{
		const AActor* InteractingActor = RegisteredActor.Get();
		if (!InteractingActor || OutCapsules.Num() >= MaximumActorShadowCapsuleCount)
		{
			continue;
		}

		AppendActorShadowCapsules(*InteractingActor, ShadowCapsuleBones, OutCapsules);
	}
}

void ATDFluidLiveActor::PushVolumeTransmittanceSources()
{
	if (!LiveComponent)
	{
		return;
	}

	const FTDFluidVolumeLightParameters Light = ResolveVolumeLightParameters(VolumeLightActor);
	FTDFluidVolumeLightState LightState;
	LightState.Mode = FMath::RoundToInt(Light.Mode);
	LightState.RadiusCm = Light.Radius;
	LightState.PositionWorld = Light.Position;
	LightState.DirectionWorld = Light.Direction;
	LightState.Color = Light.Color;

	TArray<FTDFluidShadowCapsule> Capsules;
	GatherShadowCapsules(Capsules);
	LiveComponent->SetVolumeTransmittanceSources(LightState, Capsules);
}
