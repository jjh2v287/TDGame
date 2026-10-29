#include "FluidFX/TDFluidLiveActor.h"
#include "CollisionQueryParams.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/Scene.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidBrushModel.h"
#include "FluidFX/TDFluidHeightCacheAsset.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTrackSourceComponent.h"
#include "FluidFX/TDFluidVolumeDriverComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(TDFluid);

namespace
{
	constexpr int32 MaxQueuedTerrainTraceBatches = 2;
	constexpr float PlaneMeshBaseSizeCm = 100.0f;
	constexpr float ActivationVolumeHalfHeightCm = 1000.0f;
	constexpr float UnitlessLightIntensityReference = 1000.0f;
	constexpr float CandelasLightIntensityReference = 10.0f;
	constexpr float DirectionalLightIntensityReference = 10.0f;
	constexpr float MaxVolumeLightIntensityScale = 8.0f;
	constexpr float FullSphereSteradians = static_cast<float>(4.0 * UE_PI);
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

	bool IsStampableVelocity(const FVector& Velocity, float MinSpeedCmPerSec, float TeleportSpeedCmPerSec)
	{
		const float PlanarSpeed = Velocity.Size2D();
		return PlanarSpeed >= MinSpeedCmPerSec && PlanarSpeed <= TeleportSpeedCmPerSec;
	}

	void ApplyTDFluidTrackGroundPlane(FTDFluidBrushStamp& Stamp, const FTDFluidTrackGroundPlane& GroundPlane)
	{
		if (!GroundPlane.bIsValid)
		{
			return;
		}
		Stamp.bHasGroundElevation = true;
		Stamp.GroundElevationCm = GroundPlane.Point.Z - (GroundPlane.Normal.X * (Stamp.WorldLocation.X - GroundPlane.Point.X) + GroundPlane.Normal.Y * (Stamp.WorldLocation.Y - GroundPlane.Point.Y)) / GroundPlane.Normal.Z;
	}

	bool IsCellOutsideGrid(const FIntPoint& Cell, int32 Resolution)
	{
		return Cell.X < 0 || Cell.X >= Resolution || Cell.Y < 0 || Cell.Y >= Resolution;
	}

	void ApplyRingBrushSettings(FTDFluidBrushStamp& Stamp, const FTDFluidInputBrushSettings& BrushSettings)
	{
		Stamp.RingInnerRadiusRatio = BrushSettings.RingInnerRadiusRatio;
		Stamp.RingSideArcWeight = BrushSettings.RingSideArcWeight;
		Stamp.RingInnerEraseRatePerSec = BrushSettings.RingInnerEraseRatePerSec;
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
	InteractionVolume->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);

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
		if (WetnessSourceActor && WetnessSourceActor->LiveComponent)
		{
			LiveComponent->SetWetnessSourceComponent(WetnessSourceActor->LiveComponent);
		}
	}

	SyncVolumeExtentsWithSimulation();
	SyncVolumeDriver();

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
	DiscardTerrainTraceSlots();
	if (TerrainHeightCacheLoadHandle.IsValid())
	{
		TerrainHeightCacheLoadHandle->CancelHandle();
		TerrainHeightCacheLoadHandle.Reset();
	}
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
	SyncVolumeDriver();
}

void ATDFluidLiveActor::ReapplyPresetTuning(FTDFluidPresetReapplyReport& InOutReport)
{
	if (!LiveComponent)
	{
		return;
	}

	LiveComponent->ReapplyPresetTuning(InOutReport);
	SyncVolumeExtentsWithSimulation();
	SyncVolumeDriver();
	if (VolumeDriver)
	{
		VolumeDriver->ReapplyMaterialLooks(InOutReport);
	}
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

	const float ThresholdCm = LiveComponent->IsSimulationAwake() ? ActivationDistanceCm * LiveComponent->GetRuntimeOpsSettings().SleepDistanceRatio : ActivationDistanceCm;
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
	ApplyDeliveredTerrainTraceResults();

	const FVector2D DesiredCenter = ResolveDesiredSimulationCenter();
	CenterBeforeSolverStep = LiveComponent->GetSimulationCenterWorld();
	bHasPreparedSolverStep = LiveComponent->PrepareSimulationStep(DeltaTime, DesiredCenter, bShouldFollowPlayerPawn);

	if (bShouldFollowPlayerPawn)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ActorWindowMove);

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
		LiveComponent->SubmitNearRippleStep(ResolveNearRippleCenter(), LastStepDeltaSeconds);
		if (VolumeDriver)
		{
			VolumeDriver->DriveTargets(*LiveComponent, LastStepDeltaSeconds);
		}
	}
	else
	{
		LiveComponent->DiscardNearRippleSources();
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
	if (VolumeDriver)
	{
		VolumeDriver->SetDriverHidden(bInactive);
	}
	bHasSteppedWhileAwake = false;
	bHasPreparedSolverStep = false;
	bIsTerrainQueueCurrent = false;
	PendingTerrainCells.Reset();
	DiscardTerrainTraceSlots();

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
			? LiveComponent->GetRuntimeOpsSettings().InteractionVolumeHalfHeightCm
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
	const int32 OverlapCandidateCount = CandidateActors.Num();

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
	CurrentTrackPointLocations.Reset();
	GroundTracesThisTick = 0;

	AActor* OwnerCandidate = ResolveInteractionOwnerCandidate();
	for (int32 CandidateIndex = 0; CandidateIndex < CandidateActors.Num(); ++CandidateIndex)
	{
		AActor* Candidate = CandidateActors[CandidateIndex];
		if (!Candidate || Candidate == this || Candidate == OwnerCandidate || Candidate->IsA<ATDFluidLiveActor>())
		{
			continue;
		}
		const FBox CandidateBounds = ResolveHarvestBounds(*Candidate);
		if (!PassesHarvestFilter(*Candidate, CandidateIndex < OverlapCandidateCount, CandidateBounds))
		{
			continue;
		}
		HarvestActorInteractionStamps(Candidate, DeltaTime, CandidateBounds, false);
	}
	HarvestOwnerInteraction(OwnerCandidate, DeltaTime);

	PreviousActorLocations = MoveTemp(CurrentActorLocations);
	PreviousBoneLocations = MoveTemp(CurrentBoneLocations);
	PreviousTrackPointLocations = MoveTemp(CurrentTrackPointLocations);
}

void ATDFluidLiveActor::HarvestActorInteractionStamps(AActor* TargetActor, float DeltaTime, const FBox& TargetBounds, bool bIsOwnerCandidate)
{
	BindCombatComponentIfPresent(TargetActor);

	const FObjectKey ActorKey(TargetActor);
	const FVector CurrentLocation = ResolveHarvestPivotLocation(*TargetActor, TargetBounds);
	const FTDFluidInputBrushSettings& InputBrushSettings = LiveComponent->GetInputBrushSettings();
	const FTDFluidTrackGroundPlane GroundPlane = LiveComponent->NeedsStampGroundElevation() ? TraceTrackGroundPlane(*TargetActor) : FTDFluidTrackGroundPlane();
	FVector OwnerPlanarVelocity = FVector::ZeroVector;

	if (const FVector* PreviousLocation = PreviousActorLocations.Find(ActorKey))
	{
		const FVector ComputedVelocity = (CurrentLocation - *PreviousLocation) / DeltaTime;
		OwnerPlanarVelocity = FVector(ComputedVelocity.X, ComputedVelocity.Y, 0.0);
		if (IsStampableVelocity(ComputedVelocity, MinInteractionSpeedCmPerSec, InputBrushSettings.TeleportSpeedCmPerSec))
		{
			FTDFluidBrushStamp PivotStamp;
			PivotStamp.WorldLocation = CurrentLocation;
			PivotStamp.WorldVelocity = ComputedVelocity;
			PivotStamp.RadiusCm = InputBrushSettings.PivotBrushRadiusOverrideCm > 0.0f ? InputBrushSettings.PivotBrushRadiusOverrideCm : ActorPivotBrushRadiusCm;
			PivotStamp.DensityStrength = InputBrushSettings.PivotDensityStrength;
			PivotStamp.VelocityStrength = 1.0f;
			PivotStamp.Channel = InteractionBrushChannel;
			ApplyActorBrushSettings(PivotStamp, LiveComponent->SimulationConfig);
			ApplyRingBrushSettings(PivotStamp, InputBrushSettings);
			PivotStamp.ShapeHardness = InputBrushSettings.BrushHardness;
			PivotStamp.Source = ETDFluidBrushSource::Pivot;
			ApplyHarvestObjectSize(PivotStamp, TargetActor->GetActorScale3D(), &TargetBounds);
			RecordLingeringInteractionStamp(FTDFluidInputSourceKey{ActorKey, NAME_None, INDEX_NONE}, PivotStamp);
			if (InputBrushSettings.bInterpolatePivotAndPointerStrokes)
			{
				QueueTrackStroke(PivotStamp, *PreviousLocation, InputBrushSettings, GroundPlane, true);
			}
			else
			{
				ApplyTDFluidTrackGroundPlane(PivotStamp, GroundPlane);
				LiveComponent->QueueBrushStamp(PivotStamp);
			}
		}
		else if (ComputedVelocity.Size2D() < MinInteractionSpeedCmPerSec && TargetActor->IsA<APawn>())
		{
			LiveComponent->QueueNearRippleIdleContact(CurrentLocation);
		}
	}
	CurrentActorLocations.Add(ActorKey, CurrentLocation);
	HarvestTrackSourceStamps(*TargetActor, DeltaTime, InputBrushSettings, GroundPlane);

	const TArray<FName>& HarvestBoneNames = bIsOwnerCandidate && !OwnerBoneNames.IsEmpty() ? OwnerBoneNames : TrackedBoneNames;
	if (HarvestBoneNames.IsEmpty())
	{
		return;
	}

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalComponents(TargetActor);
	for (USkeletalMeshComponent* SkelMesh : SkeletalComponents)
	{
		if (!SkelMesh || (bIsOwnerCandidate && !OwnerComponentNames.IsEmpty() && !OwnerComponentNames.Contains(SkelMesh->GetFName())))
		{
			continue;
		}

		const FObjectKey MeshKey(SkelMesh);
		for (const FName& BoneName : HarvestBoneNames)
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
				if (IsTrackContactStampable(BoneVelocity, MinInteractionSpeedCmPerSec, InputBrushSettings.BoneStampMaxSpeedCmPerSec, InputBrushSettings))
				{
					FTDFluidBrushStamp BoneStamp;
					BoneStamp.WorldLocation = BoneLocation;
					BoneStamp.WorldVelocity = BoneVelocity;
					BoneStamp.RadiusCm = InputBrushSettings.BoneBrushRadiusOverrideCm > 0.0f ? InputBrushSettings.BoneBrushRadiusOverrideCm : BoneBrushRadiusCm;
					BoneStamp.DensityStrength = InputBrushSettings.BoneDensityStrength;
					BoneStamp.VelocityStrength = 1.0f;
					BoneStamp.Channel = InteractionBrushChannel;
					ApplyActorBrushSettings(BoneStamp, LiveComponent->SimulationConfig);
					ApplyRingBrushSettings(BoneStamp, InputBrushSettings);
					BoneStamp.ShapeAspectRatio = InputBrushSettings.BoneStampAspectRatio;
					BoneStamp.ShapeHardness = InputBrushSettings.BrushHardness;
					BoneStamp.ShapeMajorAxis = OwnerPlanarVelocity;
					BoneStamp.Source = ETDFluidBrushSource::Bone;
					ApplyHarvestObjectSize(BoneStamp, SkelMesh->GetComponentScale(), nullptr);
					RecordLingeringInteractionStamp(FTDFluidInputSourceKey{MeshKey, BoneName, INDEX_NONE}, BoneStamp);
					QueueTrackStroke(BoneStamp, *PrevBoneLoc, InputBrushSettings, GroundPlane, InputBrushSettings.bInterpolateStrokes);
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

void ATDFluidLiveActor::PollMouseBrush(float DeltaTime)
{
	PollPointerInput(DeltaTime);
	EmitLingeringInteractionStamps(DeltaTime);
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
	PreviousTrackPointLocations.Reset();
	CurrentActorLocations.Reset();
	CurrentBoneLocations.Reset();
	InputPersistence.Reset();
	PreviousPointerLocations.Reset();
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

	const int32 SimulationResolution = LiveComponent->GetTerrainSampleResolution();
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
	CSV_SCOPED_TIMING_STAT(TDFluid, TerrainQueueShift);
	if (!ShouldSampleTerrainElevation() || !bIsTerrainQueueCurrent)
	{
		return;
	}

	const int32 Resolution = LiveComponent->GetTerrainSampleResolution();
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

	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ShiftTerrainQueue);

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

	for (FTDFluidTerrainTraceSlot& Slot : TerrainTraceSlots)
	{
		if (Slot.bIsSettled)
		{
			continue;
		}

		Slot.Cell -= ShiftCells;
		Slot.bIsSettled = IsCellOutsideGrid(Slot.Cell, Resolution);
	}

	TerrainCacheMissPrefixCount = 0;
	if (TerrainHeightSettings.bUseHeightAsCollisionMask)
	{
		TerrainCollisionMask.Shift(ShiftCells);
	}

	const int32 NewColumnBegin = ShiftCells.X > 0 ? Resolution - ShiftCells.X : 0;
	const int32 NewColumnEnd = ShiftCells.X > 0 ? Resolution : -ShiftCells.X;
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		const bool bIsNewRow = ShiftCells.Y > 0 ? CellY >= Resolution - ShiftCells.Y : CellY < -ShiftCells.Y;
		const int32 FirstCellX = bIsNewRow ? 0 : NewColumnBegin;
		const int32 EndCellX = bIsNewRow ? Resolution : NewColumnEnd;
		for (int32 CellX = FirstCellX; CellX < EndCellX; ++CellX)
		{
			PendingTerrainCells.Add(FIntPoint(CellX, CellY));
		}
	}
}

bool ATDFluidLiveActor::ShouldSampleTerrainElevation() const
{
	return bSampleTerrainElevation && !(LiveComponent && LiveComponent->IsAreaWrapped());
}

void ATDFluidLiveActor::SampleQueuedTerrainWithinBudget()
{
	CSV_SCOPED_TIMING_STAT(TDFluid, TerrainQueueSample);
	if (!ShouldSampleTerrainElevation())
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

	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_SampleTerrainQueue);

	if (!bIsTerrainQueueCurrent)
	{
		DiscardTerrainTraceSlots();
		const int32 Resolution = LiveComponent->GetTerrainSampleResolution();
		PendingTerrainCells.Reset(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				PendingTerrainCells.Add(FIntPoint(CellX, CellY));
			}
		}
		bIsTerrainQueueCurrent = true;
		TerrainCacheMissPrefixCount = 0;
		if (TerrainHeightSettings.bUseHeightAsCollisionMask)
		{
			TerrainCollisionMask.Reset(Resolution);
		}
	}

	if (IsTerrainHeightCacheLoading())
	{
		return;
	}

	if (FillPendingTerrainCellsFromCache() > 0)
	{
		bShouldCompleteTerrainSampling = true;
	}
	const bool bHasUnscannedCacheCells = IsTerrainHeightCacheSourceActive() && PendingTerrainCells.Num() > TerrainCacheMissPrefixCount;

	if (bUseAsyncTerrainTraces)
	{
		if (!bHasUnscannedCacheCells)
		{
			RequestQueuedTerrainTraces(*World);
			TerrainCacheMissPrefixCount = FMath::Min(TerrainCacheMissPrefixCount, PendingTerrainCells.Num());
		}
		CompleteTerrainSamplingIfSettled();
		return;
	}

	if (bHasUnscannedCacheCells)
	{
		return;
	}

	if (PendingTerrainCells.IsEmpty())
	{
		CompleteTerrainSamplingIfSettled();
		return;
	}

	const FCollisionQueryParams QueryParams = BuildTerrainTraceQueryParams(*World);
	const FCollisionObjectQueryParams ObjectQueryParams(ECC_WorldStatic);
	const float BaseHeightCm = GetActorLocation().Z;

	int32 RemainingTraces = FMath::Max(MaxTerrainTracesPerTick, 1);
	while (RemainingTraces > 0 && !PendingTerrainCells.IsEmpty())
	{
		const FIntPoint Cell = PendingTerrainCells.Pop(EAllowShrinking::No);
		const FVector2D CellCenter = LiveComponent->TerrainSampleCellToPlaneCenter(Cell.X, Cell.Y);
		const FVector TraceStart(CellCenter.X, CellCenter.Y, BaseHeightCm + TerrainHeightSettings.TraceStartHeightCm);
		const FVector TraceEnd(CellCenter.X, CellCenter.Y, BaseHeightCm - TerrainHeightSettings.TraceEndDepthCm);

		FHitResult Hit;
		const bool bDidHit = World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjectQueryParams, QueryParams);
		++TerrainTraceRequestCount;
		RecordTerrainCellElevation(Cell.X, Cell.Y, bDidHit ? ApplyTerrainHeightOps(Hit.ImpactPoint.Z, TerrainHeightSettings) : ResolveTerrainMissElevationCm(Cell.X, Cell.Y));
		--RemainingTraces;
	}
	TerrainCacheMissPrefixCount = FMath::Min(TerrainCacheMissPrefixCount, PendingTerrainCells.Num());

	if (PendingTerrainCells.IsEmpty())
	{
		bShouldCompleteTerrainSampling = true;
		CompleteTerrainSamplingIfSettled();
	}
}

bool ATDFluidLiveActor::IsTrackContactStampable(const FVector& Velocity, float MinSpeedCmPerSec, float MaxSpeedCmPerSec, const FTDFluidInputBrushSettings& Settings)
{
	const double PlanarSpeed = Velocity.Size2D();
	if (MaxSpeedCmPerSec > 0.0f && PlanarSpeed > MaxSpeedCmPerSec)
	{
		return false;
	}
	if (Settings.bStampsSlowBones)
	{
		return PlanarSpeed <= Settings.TeleportSpeedCmPerSec;
	}
	return IsStampableVelocity(Velocity, MinSpeedCmPerSec, Settings.TeleportSpeedCmPerSec);
}

int32 ATDFluidLiveActor::ComputeStrokeStampCount(double SegmentLengthCm, float RadiusCm, float SpacingRatio)
{
	const double SpacingCm = FMath::Max(static_cast<double>(RadiusCm) * static_cast<double>(SpacingRatio), 1.0);
	return FMath::Clamp(FMath::CeilToInt(SegmentLengthCm / SpacingCm), 1, 16);
}

FTDFluidTrackGroundPlane ATDFluidLiveActor::TraceTrackGroundPlane(const AActor& TargetActor)
{
	FTDFluidTrackGroundPlane GroundPlane;
	UWorld* World = GetWorld();
	const FTDFluidRuntimeOpsSettings& RuntimeOps = LiveComponent->GetRuntimeOpsSettings();
	if (!World || GroundTracesThisTick >= RuntimeOps.MaxGroundTracesPerTick)
	{
		return GroundPlane;
	}

	++GroundTracesThisTick;
	FCollisionQueryParams QueryParams = BuildTerrainTraceQueryParams(*World);
	QueryParams.AddIgnoredActor(&TargetActor);
	const FVector TraceStart = TargetActor.GetActorLocation();
	const FVector TraceEnd = TraceStart - FVector(0.0, 0.0, RuntimeOps.GroundTraceDepthCm);
	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams) || Hit.ImpactNormal.Z < 0.05)
	{
		return GroundPlane;
	}

	GroundPlane.Point = Hit.ImpactPoint;
	GroundPlane.Normal = Hit.ImpactNormal;
	GroundPlane.bIsValid = true;
	return GroundPlane;
}

void ATDFluidLiveActor::QueueTrackStroke(FTDFluidBrushStamp Stamp, const FVector& PreviousLocation, const FTDFluidInputBrushSettings& Settings, const FTDFluidTrackGroundPlane& GroundPlane, bool bInterpolates)
{
	const FVector EndLocation = Stamp.WorldLocation;
	const float SpacingRadiusCm = Stamp.RadiusCm * Settings.GlobalBrushRadiusScale * FTDFluidBrushModel::ResolveSourceRadiusScale(Settings, Stamp.Source);
	const int32 StampCount = bInterpolates ? ComputeStrokeStampCount((EndLocation - PreviousLocation).Size2D(), SpacingRadiusCm, Settings.StrokeSpacingRatio) : 1;
	for (int32 StampIndex = 1; StampIndex <= StampCount; ++StampIndex)
	{
		Stamp.WorldLocation = StampIndex == StampCount ? EndLocation : FMath::Lerp(PreviousLocation, EndLocation, static_cast<double>(StampIndex) / static_cast<double>(StampCount));
		ApplyTDFluidTrackGroundPlane(Stamp, GroundPlane);
		LiveComponent->QueueBrushStamp(Stamp);
	}
}

void ATDFluidLiveActor::HarvestTrackSourceStamps(AActor& TargetActor, float DeltaTime, const FTDFluidInputBrushSettings& Settings, const FTDFluidTrackGroundPlane& GroundPlane)
{
	TInlineComponentArray<UTDFluidTrackSourceComponent*> TrackSources(&TargetActor);
	for (const UTDFluidTrackSourceComponent* TrackSource : TrackSources)
	{
		if (!TrackSource)
		{
			continue;
		}

		const FObjectKey SourceKey(TrackSource);
		for (int32 PointIndex = 0; PointIndex < TrackSource->Points.Num(); ++PointIndex)
		{
			FVector PointLocation;
			if (!TrackSource->ResolvePointWorldLocation(PointIndex, PointLocation))
			{
				continue;
			}

			const TPair<FObjectKey, int32> PointKey(SourceKey, PointIndex);
			CurrentTrackPointLocations.Add(PointKey, PointLocation);
			const FVector* PreviousPointLocation = PreviousTrackPointLocations.Find(PointKey);
			if (!PreviousPointLocation)
			{
				continue;
			}

			const FVector PointVelocity = (PointLocation - *PreviousPointLocation) / DeltaTime;
			if (!IsTrackContactStampable(PointVelocity, MinInteractionSpeedCmPerSec, 0.0f, Settings))
			{
				continue;
			}

			FTDFluidBrushStamp WheelStamp;
			WheelStamp.WorldLocation = PointLocation;
			WheelStamp.WorldVelocity = PointVelocity;
			WheelStamp.RadiusCm = TrackSource->ResolvePointRadiusCm(PointIndex, Settings.VehicleBrushRadiusScale);
			WheelStamp.DensityStrength = Settings.BoneDensityStrength;
			WheelStamp.VelocityStrength = 1.0f;
			WheelStamp.Channel = InteractionBrushChannel;
			ApplyActorBrushSettings(WheelStamp, LiveComponent->SimulationConfig);
			WheelStamp.ShapeAspectRatio = Settings.VehicleStampAspectRatio;
			WheelStamp.ShapeHardness = Settings.BrushHardness;
			WheelStamp.Source = ETDFluidBrushSource::TrackPoint;
			RecordLingeringInteractionStamp(FTDFluidInputSourceKey{SourceKey, NAME_None, PointIndex}, WheelStamp);
			QueueTrackStroke(WheelStamp, *PreviousPointLocation, Settings, GroundPlane, Settings.bInterpolateStrokes);
		}
	}
}

FCollisionQueryParams ATDFluidLiveActor::BuildTerrainTraceQueryParams(const UWorld& World) const
{
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDFluidTerrainSample), false, this);
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(&World, 0))
	{
		QueryParams.AddIgnoredActor(PlayerPawn);
	}
	AddTerrainTraceIgnoredActors(QueryParams);
	return QueryParams;
}

void ATDFluidLiveActor::RequestQueuedTerrainTraces(UWorld& World)
{
	const int32 TraceBudget = FMath::Max(MaxTerrainTracesPerTick, 1);
	int32 RemainingRequests = FMath::Min(TraceBudget, TraceBudget * MaxQueuedTerrainTraceBatches - AwaitedTerrainTraceCount);

	if (RemainingRequests > 0 && !PendingTerrainCells.IsEmpty())
	{
		const FCollisionQueryParams QueryParams = BuildTerrainTraceQueryParams(World);
		const FCollisionObjectQueryParams ObjectQueryParams(ECC_WorldStatic);
		const FTraceDelegate TraceDelegate = FTraceDelegate::CreateUObject(this, &ATDFluidLiveActor::OnTerrainTraceFinished);
		const float BaseHeightCm = GetActorLocation().Z;

		TerrainTraceSlots.Reserve(TerrainTraceSlots.Num() + RemainingRequests);
		while (RemainingRequests > 0 && !PendingTerrainCells.IsEmpty())
		{
			const FIntPoint Cell = PendingTerrainCells.Pop(EAllowShrinking::No);
			const FVector2D CellCenter = LiveComponent->TerrainSampleCellToPlaneCenter(Cell.X, Cell.Y);
			const FVector TraceStart(CellCenter.X, CellCenter.Y, BaseHeightCm + TerrainHeightSettings.TraceStartHeightCm);
			const FVector TraceEnd(CellCenter.X, CellCenter.Y, BaseHeightCm - TerrainHeightSettings.TraceEndDepthCm);
			const uint32 Serial = TerrainTraceFirstSerial + static_cast<uint32>(TerrainTraceSlots.Num());

			TerrainTraceSlots.AddDefaulted_GetRef().Cell = Cell;
			++AwaitedTerrainTraceCount;
			++TerrainTraceRequestCount;
			World.AsyncLineTraceByObjectType(EAsyncTraceType::Single, TraceStart, TraceEnd, ObjectQueryParams, QueryParams, &TraceDelegate, Serial);
			--RemainingRequests;
		}
		bShouldCompleteTerrainSampling = true;
	}
}

void ATDFluidLiveActor::OnTerrainTraceFinished(const FTraceHandle& TraceHandle, FTraceDatum& TraceDatum)
{
	const uint32 SlotIndex = TraceDatum.UserData - TerrainTraceFirstSerial;
	if (SlotIndex >= static_cast<uint32>(TerrainTraceSlots.Num()))
	{
		return;
	}

	FTDFluidTerrainTraceSlot& Slot = TerrainTraceSlots[SlotIndex];
	if (Slot.bIsDelivered)
	{
		return;
	}

	Slot.bIsDelivered = true;
	--AwaitedTerrainTraceCount;
	if (Slot.bIsSettled)
	{
		return;
	}

	Slot.bDidHit = !TraceDatum.OutHits.IsEmpty();
	Slot.ImpactZ = Slot.bDidHit ? TraceDatum.OutHits[0].ImpactPoint.Z : 0.0f;
}

void ATDFluidLiveActor::ApplyDeliveredTerrainTraceResults()
{
	CSV_SCOPED_TIMING_STAT(TDFluid, TerrainQueueApply);
	if (TerrainTraceSlots.IsEmpty())
	{
		return;
	}

	if (!ShouldSampleTerrainElevation() || !bIsTerrainQueueCurrent)
	{
		DiscardTerrainTraceSlots();
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TDFluid_ApplyTerrainTraceResults);

	for (FTDFluidTerrainTraceSlot& Slot : TerrainTraceSlots)
	{
		if (!Slot.bIsDelivered || Slot.bIsSettled)
		{
			continue;
		}

		Slot.bIsSettled = true;
		RecordTerrainCellElevation(Slot.Cell.X, Slot.Cell.Y, Slot.bDidHit ? ApplyTerrainHeightOps(Slot.ImpactZ, TerrainHeightSettings) : ResolveTerrainMissElevationCm(Slot.Cell.X, Slot.Cell.Y));
	}

	if (AwaitedTerrainTraceCount > 0)
	{
		return;
	}

	TerrainTraceFirstSerial += static_cast<uint32>(TerrainTraceSlots.Num());
	TerrainTraceSlots.Reset();
	CompleteTerrainSamplingIfSettled();
}

void ATDFluidLiveActor::DiscardTerrainTraceSlots()
{
	TerrainTraceFirstSerial += static_cast<uint32>(TerrainTraceSlots.Num());
	TerrainTraceSlots.Reset();
	AwaitedTerrainTraceCount = 0;
	bShouldCompleteTerrainSampling = false;
}

void ATDFluidLiveActor::CompleteTerrainSamplingIfSettled()
{
	if (!bShouldCompleteTerrainSampling || !PendingTerrainCells.IsEmpty() || !TerrainTraceSlots.IsEmpty())
	{
		return;
	}

	bShouldCompleteTerrainSampling = false;
	++TerrainSamplingCompletionCount;
	LiveComponent->CompleteTerrainElevationSampling();
	RebuildTerrainCollisionObstacles();
	OnTerrainSamplingCompleted();
}

float ATDFluidLiveActor::ApplyTerrainHeightOps(float ElevationCm, const FTDFluidTerrainHeightSettings& Settings)
{
	float AdjustedElevationCm = ElevationCm;
	if (Settings.bClampHeightLowerValues)
	{
		AdjustedElevationCm = FMath::Max(AdjustedElevationCm, Settings.ClampingValueCm);
	}
	if (Settings.HeightOffsetCm != 0.0f)
	{
		AdjustedElevationCm += Settings.HeightOffsetCm;
	}
	return AdjustedElevationCm;
}

void ATDFluidLiveActor::RecordTerrainCellElevation(int32 CellX, int32 CellY, float ElevationCm)
{
	LiveComponent->SetTerrainElevationAtCell(CellX, CellY, ElevationCm);
	if (TerrainHeightSettings.bUseHeightAsCollisionMask)
	{
		TerrainCollisionMask.SetCellElevation(CellX, CellY, ElevationCm);
	}
}

void ATDFluidLiveActor::RebuildTerrainCollisionObstacles()
{
	if (!TerrainHeightSettings.bUseHeightAsCollisionMask)
	{
		return;
	}

	const int32 Resolution = LiveComponent->GetTerrainSampleResolution();
	if (Resolution <= 0 || TerrainCollisionMask.GetResolution() != Resolution)
	{
		return;
	}

	const UTDFluidLiveComponent* SamplingComponent = LiveComponent;
	const FVector2D CellSizeCm = LiveComponent->GetSimulationExtentCm() / static_cast<double>(Resolution);
	TArray<FTDFluidSdfObstacle> TerrainBoxes;
	TerrainCollisionMask.BuildBoxes(static_cast<float>(GetActorLocation().Z), TerrainHeightSettings.CollisionHeightAboveBaseCm, TerrainHeightSettings.MaxCollisionBoxes, [SamplingComponent](int32 CellX, int32 CellY)
	{
		return SamplingComponent->TerrainSampleCellToPlaneCenter(CellX, CellY);
	}, CellSizeCm, TerrainBoxes);
	LiveComponent->SetTerrainCollisionObstacles(TerrainBoxes);
}

bool ATDFluidLiveActor::IsTerrainHeightCacheSourceActive() const
{
	return TerrainHeightSettings.HeightSource == ETDFluidTerrainHeightSource::HeightCache && LoadedTerrainHeightCache && LoadedTerrainHeightCache->IsUsable();
}

bool ATDFluidLiveActor::IsTerrainHeightCacheLoading() const
{
	return TerrainHeightSettings.HeightSource == ETDFluidTerrainHeightSource::HeightCache && !LoadedTerrainHeightCache && TerrainHeightCacheLoadHandle.IsValid() && TerrainHeightCacheLoadHandle->IsLoadingInProgress();
}

int32 ATDFluidLiveActor::FillPendingTerrainCellsFromCache()
{
	if (!IsTerrainHeightCacheSourceActive())
	{
		return 0;
	}

	int32 RemainingSamples = FMath::Max(TerrainHeightSettings.MaxCacheSamplesPerTick, 1);
	int32 InsertedCellCount = 0;
	while (RemainingSamples > 0 && PendingTerrainCells.Num() > TerrainCacheMissPrefixCount)
	{
		--RemainingSamples;
		const FIntPoint Cell = PendingTerrainCells.Last();
		const FVector2D CellCenter = LiveComponent->TerrainSampleCellToPlaneCenter(Cell.X, Cell.Y);
		double CachedHeightCm = 0.0;
		if (LoadedTerrainHeightCache->SampleHeightCm(CellCenter, CachedHeightCm))
		{
			PendingTerrainCells.Pop(EAllowShrinking::No);
			RecordTerrainCellElevation(Cell.X, Cell.Y, ApplyTerrainHeightOps(static_cast<float>(CachedHeightCm), TerrainHeightSettings));
			++InsertedCellCount;
			continue;
		}

		PendingTerrainCells.Swap(PendingTerrainCells.Num() - 1, TerrainCacheMissPrefixCount);
		++TerrainCacheMissPrefixCount;
	}
	TerrainCacheSampleCount += InsertedCellCount;
	return InsertedCellCount;
}

void ATDFluidLiveActor::SyncVolumeDriver()
{
	if (!LiveComponent)
	{
		return;
	}

	const UTDFluidPresetAsset* Preset = LiveComponent->ActivePreset.Get();
	const ETDFluidPresetKind Kind = Preset ? Preset->PresetKind : ETDFluidPresetKind::Custom;
	SetTerrainHeightSettings(UTDFluidPresetAsset::BuildTerrainHeightForPresetKind(Kind));
	const FTDFluidVolumeDriverSettings DriverSettings = UTDFluidPresetAsset::BuildVolumeDriverForPresetKind(Kind);
	if (!UTDFluidVolumeDriverComponent::IsDriverNeeded(DriverSettings, TerrainHeightSettings, Preset))
	{
		if (VolumeDriver)
		{
			VolumeDriver->DestroyComponent();
			VolumeDriver = nullptr;
		}
		return;
	}

	if (!VolumeDriver)
	{
		VolumeDriver = NewObject<UTDFluidVolumeDriverComponent>(this, NAME_None, RF_Transient);
		VolumeDriver->RegisterComponent();
	}
	VolumeDriver->ConfigureFromLiveComponent(*LiveComponent);
	VolumeDriver->SetDriverHidden(bIsPooledInactive);
	PushTerrainHeightCacheToDriver();
}

void ATDFluidLiveActor::SetTerrainHeightSettings(const FTDFluidTerrainHeightSettings& InSettings)
{
	const bool bHasChanged = !FTDFluidTerrainHeightSettings::StaticStruct()->CompareScriptStruct(&TerrainHeightSettings, &InSettings, PPF_None);
	const bool bHadCollisionMask = TerrainHeightSettings.bUseHeightAsCollisionMask;
	TerrainHeightSettings = InSettings;
	SyncTerrainHeightCache();
	if (!bHasChanged)
	{
		return;
	}

	bIsTerrainQueueCurrent = false;
	if (bHadCollisionMask && !TerrainHeightSettings.bUseHeightAsCollisionMask && LiveComponent)
	{
		LiveComponent->SetTerrainCollisionObstacles(TArray<FTDFluidSdfObstacle>());
	}
}

void ATDFluidLiveActor::SyncTerrainHeightCache()
{
	const bool bNeedsCache = TerrainHeightSettings.HeightSource == ETDFluidTerrainHeightSource::HeightCache || TerrainHeightSettings.VolumeHeightSource == ETDFluidVolumeHeightSource::HeightCache;
	if (!bNeedsCache || TerrainHeightCache.IsNull())
	{
		if (bNeedsCache && !bHasWarnedTerrainHeightCacheUnavailable)
		{
			bHasWarnedTerrainHeightCacheUnavailable = true;
			UE_LOG(LogTemp, Warning, TEXT("%s: terrain height cache source is selected but no cache asset is assigned, falling back to line traces"), *GetName());
		}
		if (TerrainHeightCacheLoadHandle.IsValid())
		{
			TerrainHeightCacheLoadHandle->CancelHandle();
			TerrainHeightCacheLoadHandle.Reset();
		}
		LoadedTerrainHeightCache = nullptr;
		TerrainHeightCacheTexture = nullptr;
		return;
	}

	UTDFluidHeightCacheAsset* ResidentCache = TerrainHeightCache.Get();
	if (ResidentCache && ResidentCache == LoadedTerrainHeightCache)
	{
		PushTerrainHeightCacheToDriver();
		return;
	}
	if (ResidentCache)
	{
		HandleTerrainHeightCacheLoaded();
		return;
	}
	if (TerrainHeightCacheLoadHandle.IsValid() && TerrainHeightCacheLoadHandle->IsLoadingInProgress())
	{
		return;
	}

	LoadedTerrainHeightCache = nullptr;
	TerrainHeightCacheTexture = nullptr;
	TerrainHeightCacheLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		TerrainHeightCache.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &ATDFluidLiveActor::HandleTerrainHeightCacheLoaded));
}

void ATDFluidLiveActor::HandleTerrainHeightCacheLoaded()
{
	TerrainHeightCacheLoadHandle.Reset();
	UTDFluidHeightCacheAsset* ResolvedCache = TerrainHeightCache.Get();
	if (!ResolvedCache || !ResolvedCache->IsUsable())
	{
		if (!bHasWarnedTerrainHeightCacheUnavailable)
		{
			bHasWarnedTerrainHeightCacheUnavailable = true;
			UE_LOG(LogTemp, Warning, TEXT("%s: terrain height cache %s failed to load or is empty, falling back to line traces"), *GetName(), *TerrainHeightCache.ToString());
		}
		LoadedTerrainHeightCache = nullptr;
		TerrainHeightCacheTexture = nullptr;
		return;
	}

	LoadedTerrainHeightCache = ResolvedCache;
	TerrainCacheMissPrefixCount = 0;
	TerrainHeightCacheTexture = FApp::CanEverRender() ? ResolvedCache->CreateTransientHeightTexture() : nullptr;
	PushTerrainHeightCacheToDriver();
}

void ATDFluidLiveActor::PushTerrainHeightCacheToDriver()
{
	if (!VolumeDriver || !LoadedTerrainHeightCache || TerrainHeightSettings.VolumeHeightSource != ETDFluidVolumeHeightSource::HeightCache)
	{
		return;
	}

	const float ValidFlag = TerrainHeightCacheTexture ? 1.0f : 0.0f;
	VolumeDriver->SetTerrainHeightTexture(TerrainHeightCacheTexture, LoadedTerrainHeightCache->GetHeightWindow(), FVector4f(1.0f, 0.0f, ValidFlag, 0.0f));
}

bool ATDFluidLiveActor::TrySampleTerrainHeightCm(const FVector2D& WorldXY, double& OutHeightCm) const
{
	if (!LoadedTerrainHeightCache)
	{
		return false;
	}

	double CachedHeightCm = 0.0;
	if (!LoadedTerrainHeightCache->SampleHeightCm(WorldXY, CachedHeightCm))
	{
		return false;
	}
	OutHeightCm = ApplyTerrainHeightOps(static_cast<float>(CachedHeightCm), TerrainHeightSettings);
	return true;
}

bool ATDFluidLiveActor::GetTerrainHeightCacheWindow(FVector4f& OutWindow) const
{
	if (!LoadedTerrainHeightCache)
	{
		return false;
	}
	OutWindow = LoadedTerrainHeightCache->GetHeightWindow();
	return true;
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

FVector2D ATDFluidLiveActor::ResolveNearRippleCenter() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		return ResolveDesiredSimulationCenter();
	}

	const FVector PawnLocation = PlayerPawn->GetActorLocation();
	return FVector2D(PawnLocation.X, PawnLocation.Y);
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
