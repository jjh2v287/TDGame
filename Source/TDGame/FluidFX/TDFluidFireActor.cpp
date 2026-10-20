#include "FluidFX/TDFluidFireActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidBandLimitedNoise.h"
#include "FluidFX/TDFluidEmberComponent.h"
#include "FluidFX/TDFluidFireLightComponent.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Templates/Function.h"

namespace
{
	constexpr float MaxEmitterSpeedCmPerSec = 3000.0f;
	constexpr float FlickerVelocityRatio = 0.5f;
	constexpr int32 MaxSourceStampCount = 8;
	constexpr int32 MaxSourceSubstepCount = 4;
	constexpr int32 MaxExtraFlameCardCount = 2;
	constexpr int32 FeedSampleDivisor = 32;
	constexpr int32 MinFeedSampleStride = 1;

	struct FTDFluidFeedCandidate
	{
		FIntPoint Cell = FIntPoint::ZeroValue;
		float Weight = 0.0f;
		int32 CellIndex = 0;
	};

	float ComputeFeedWeight(float Density, const FTDFluidDownstreamFeedSettings& Settings)
	{
		if (Density < Settings.MinFeedDensity)
		{
			return 0.0f;
		}

		return Density * (1.0f - FMath::SmoothStep(Settings.MinFeedDensity, Settings.FadedDensityThreshold, Density));
	}

	void CollectDownstreamFeedCells(
		int32 Resolution,
		const FTDFluidDownstreamFeedSettings& Settings,
		TFunctionRef<float(int32, int32)> SampleSelectiveDensity,
		TArray<FIntPoint>& OutCells)
	{
		const int32 Stride = FMath::Max(MinFeedSampleStride, Resolution / FeedSampleDivisor);
		const int32 FirstRow = FMath::Clamp(FMath::CeilToInt(Settings.BandStartRatio * Resolution), 0, Resolution - 1);

		TArray<FTDFluidFeedCandidate> Candidates;
		for (int32 CellY = FirstRow; CellY < Resolution; CellY += Stride)
		{
			for (int32 CellX = 0; CellX < Resolution; CellX += Stride)
			{
				const float Weight = ComputeFeedWeight(SampleSelectiveDensity(CellX, CellY), Settings);
				if (Weight <= 0.0f)
				{
					continue;
				}

				FTDFluidFeedCandidate& Candidate = Candidates.AddDefaulted_GetRef();
				Candidate.Cell = FIntPoint(CellX, CellY);
				Candidate.Weight = Weight;
				Candidate.CellIndex = CellY * Resolution + CellX;
			}
		}

		Candidates.Sort([](const FTDFluidFeedCandidate& Left, const FTDFluidFeedCandidate& Right)
		{
			if (Left.Weight != Right.Weight)
			{
				return Left.Weight > Right.Weight;
			}
			return Left.CellIndex < Right.CellIndex;
		});

		const int32 SelectedCount = FMath::Min(Candidates.Num(), Settings.MaxStampsPerStep);
		for (int32 CandidateIndex = 0; CandidateIndex < SelectedCount; ++CandidateIndex)
		{
			OutCells.Add(Candidates[CandidateIndex].Cell);
		}
	}
}

ATDFluidFireActor::ATDFluidFireActor()
{
	FireLightComponent = CreateDefaultSubobject<UTDFluidFireLightComponent>(TEXT("FireLightComponent"));
	FireLightComponent->SetupAttachment(SceneRoot);
	FireLightComponent->bIsDrivenByFluid = true;

	EmberComponent = CreateDefaultSubobject<UTDFluidEmberComponent>(TEXT("EmberComponent"));
	EmberComponent->SetupAttachment(SceneRoot);

	SourcePropMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SourcePropMeshComponent"));
	SourcePropMeshComponent->SetupAttachment(SceneRoot);
	SourcePropMeshComponent->SetMobility(EComponentMobility::Movable);
	SourcePropMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SourcePropMeshComponent->SetGenerateOverlapEvents(false);
	SourcePropMeshComponent->SetCastShadow(false);
}

void ATDFluidFireActor::BeginPlay()
{
	Super::BeginPlay();

	if (SourcePropMeshAsset.IsNull())
	{
		return;
	}

	SourcePropLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		SourcePropMeshAsset.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &ATDFluidFireActor::HandleSourcePropLoaded));
}

void ATDFluidFireActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SourcePropLoadHandle.IsValid())
	{
		SourcePropLoadHandle->CancelHandle();
		SourcePropLoadHandle.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void ATDFluidFireActor::HandleSourcePropLoaded()
{
	if (UStaticMesh* LoadedMesh = SourcePropMeshAsset.Get())
	{
		SourcePropMeshComponent->SetStaticMesh(LoadedMesh);
	}
}

const AActor* ATDFluidFireActor::ResolveFollowTarget() const
{
	if (const AActor* ExplicitTarget = FollowTargetActor.Get())
	{
		return ExplicitTarget;
	}

	const UWorld* World = GetWorld();
	return World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
}

bool ATDFluidFireActor::ResolveSourceSocketTransform(FTransform& OutSocketTransform) const
{
	if (!bShouldFollowPlayerPawn || FollowSocketName.IsNone())
	{
		return false;
	}

	const AActor* FollowTarget = ResolveFollowTarget();
	if (!FollowTarget)
	{
		return false;
	}

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshComponents;
	FollowTarget->GetComponents(SkeletalMeshComponents);
	for (const USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
	{
		if (SkeletalMeshComponent && SkeletalMeshComponent->DoesSocketExist(FollowSocketName))
		{
			OutSocketTransform = SkeletalMeshComponent->GetSocketTransform(FollowSocketName);
			return true;
		}
	}

	return false;
}

FVector ATDFluidFireActor::ResolvePlaneAnchorWorldLocation() const
{
	FTransform SocketTransform;
	if (!ResolveSourceSocketTransform(SocketTransform))
	{
		return Super::ResolvePlaneAnchorWorldLocation();
	}

	return SocketTransform.TransformPosition(SourceEmitter.SourceOffsetCm);
}

FVector2D ATDFluidFireActor::GetEmitterPlaneVelocity() const
{
	if (!LiveComponent)
	{
		return FVector2D::ZeroVector;
	}

	return LiveComponent->GetPlaneBasis().ProjectVector(EmitterWorldVelocity);
}

FVector2D ATDFluidFireActor::GetDynamicCenterOffsetPlane() const
{
	if (!LiveComponent || !SourceEmitter.bIsEnabled || !LiveComponent->IsSimulationInitialized())
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D ExtentCm = LiveComponent->GetSimulationExtentCm();
	const FVector2D MaxLeadCm = ExtentCm * SourceEmitter.MaxTrailLeadRatio;
	const FVector2D LeadCm = GetEmitterPlaneVelocity() * SourceEmitter.TrailLeadSeconds;
	return -FVector2D(FMath::Clamp(LeadCm.X, -MaxLeadCm.X, MaxLeadCm.X), FMath::Clamp(LeadCm.Y, -MaxLeadCm.Y, MaxLeadCm.Y));
}

void ATDFluidFireActor::PrepareActorSpecificInputs(float DeltaTime)
{
	UpdateSourcePropTransform();
	if (!SourceEmitter.bIsEnabled)
	{
		return;
	}

	const FVector AnchorWorld = ResolvePlaneAnchorWorldLocation();
	const FVector LastAnchorWorld = bHasPreviousAnchor ? PreviousAnchorWorld : AnchorWorld;
	EmitterWorldVelocity = bHasPreviousAnchor ? (AnchorWorld - PreviousAnchorWorld) / DeltaTime : FVector::ZeroVector;
	if (EmitterWorldVelocity.SizeSquared() > FMath::Square(MaxEmitterSpeedCmPerSec))
	{
		EmitterWorldVelocity = FVector::ZeroVector;
	}

	PreviousAnchorWorld = AnchorWorld;
	bHasPreviousAnchor = true;
	FireSimulationTimeSeconds += DeltaTime;
	QueueSourceEmitterStamps(AnchorWorld, LastAnchorWorld);
	QueueSourceCoreStamp(AnchorWorld);
}

void ATDFluidFireActor::QueueSourceEmitterStamps(const FVector& AnchorWorld, const FVector& LastAnchorWorld)
{
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent->GetPlaneBasis();
	const int32 StampCount = FMath::Clamp(SourceEmitter.SourceStampCount, 1, MaxSourceStampCount);
	const int32 SubstepCount = FMath::Clamp(SourceEmitter.SubstepCount, 1, MaxSourceSubstepCount);
	const FVector EmitDirection = PlaneBasis.UnprojectVector(FVector2D(0.0, 1.0));
	for (int32 SubstepIndex = 1; SubstepIndex <= SubstepCount; ++SubstepIndex)
	{
		const FVector EmitAnchorWorld = SubstepIndex == SubstepCount
			? AnchorWorld
			: FMath::Lerp(LastAnchorWorld, AnchorWorld, static_cast<double>(SubstepIndex) / static_cast<double>(SubstepCount));
		for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
		{
			const double LateralRatio = StampCount == 1 ? 0.0 : static_cast<double>(StampIndex) / static_cast<double>(StampCount - 1) * 2.0 - 1.0;
			const float Flicker = TDFluidDeterministic::EvaluateBandLimitedNoise(SourceEmitter.RandomSeed, StampIndex, FireSimulationTimeSeconds, SourceEmitter.FlickerFrequencyHz);
			const float Multiplier = FMath::Max(0.0f, 1.0f + SourceEmitter.FlickerAmplitude * Flicker);

			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = EmitAnchorWorld + PlaneBasis.U * (LateralRatio * SourceEmitter.SourceSpreadCm);
			Stamp.WorldVelocity = EmitDirection * SourceEmitter.EmitSpeedCmPerSec * (1.0f + FlickerVelocityRatio * Flicker) - EmitterWorldVelocity * SourceEmitter.MotionInertiaScale;
			Stamp.RadiusCm = FMath::Max(SourceEmitter.RadiusCm * Multiplier, 1.0f);
			Stamp.DensityStrength = SourceEmitter.DensityStrength * FMath::Min(1.0f, Multiplier);
			Stamp.VelocityStrength = 1.0f;
			Stamp.Channel = SourceEmitter.Channel;
			Stamp.HeatStrength = SourceEmitter.HeatStrength;
			LiveComponent->QueueBrushStamp(Stamp);
		}
	}
}

void ATDFluidFireActor::QueueSourceCoreStamp(const FVector& AnchorWorld)
{
	if (SourceEmitter.CoreRadiusCm <= 0.0f)
	{
		return;
	}

	FTDFluidBrushStamp CoreStamp;
	CoreStamp.WorldLocation = AnchorWorld;
	CoreStamp.WorldVelocity = FVector::ZeroVector;
	CoreStamp.RadiusCm = FMath::Max(SourceEmitter.CoreRadiusCm, 1.0f);
	CoreStamp.DensityStrength = SourceEmitter.CoreDensityStrength;
	CoreStamp.VelocityStrength = 0.0f;
	CoreStamp.Channel = SourceEmitter.Channel;
	CoreStamp.HeatStrength = SourceEmitter.CoreHeatStrength;
	LiveComponent->QueueBrushStamp(CoreStamp);
}

int32 ATDFluidFireActor::GetExtraFlameCardCount() const
{
	return FMath::Clamp(FlameCardLayers.ExtraCardCount, 0, MaxExtraFlameCardCount);
}

void ATDFluidFireActor::EnsureExtraFlameCards()
{
	const int32 DesiredCardCount = GetExtraFlameCardCount();
	if (ExtraFlameCardComponents.Num() >= DesiredCardCount || !InternalSurfaceMesh || !LiveComponent)
	{
		return;
	}

	UStaticMesh* CardMesh = InternalSurfaceMesh->GetStaticMesh();
	UMaterialInterface* CardMaterial = LiveComponent->OutputMaterial.Get();
	if (!CardMesh || !CardMaterial)
	{
		return;
	}

	while (ExtraFlameCardComponents.Num() < DesiredCardCount)
	{
		const int32 LayerIndex = ExtraFlameCardComponents.Num() + 1;
		UStaticMeshComponent* CardComponent = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		CardComponent->SetStaticMesh(CardMesh);
		CardComponent->SetMobility(EComponentMobility::Movable);
		CardComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CardComponent->SetGenerateOverlapEvents(false);
		CardComponent->SetCastShadow(false);
		CardComponent->SetupAttachment(SceneRoot);
		CardComponent->RegisterComponent();
		CardComponent->SetTranslucentSortPriority(InternalSurfaceMesh->TranslucencySortPriority);

		UMaterialInstanceDynamic* CardMaterialInstance = UMaterialInstanceDynamic::Create(CardMaterial, this);
		CardMaterialInstance->SetScalarParameterValue(TEXT("CardLayerIndex"), static_cast<float>(LayerIndex));
		CardMaterialInstance->SetScalarParameterValue(TEXT("CardIntensityScale"), FlameCardLayers.CardIntensityScale);
		CardComponent->SetMaterial(0, CardMaterialInstance);
		LiveComponent->RegisterExternalMaterialTarget(CardMaterialInstance);

		ExtraFlameCardComponents.Add(CardComponent);
		ExtraFlameCardMaterials.Add(CardMaterialInstance);
	}
}

void ATDFluidFireActor::PlaceExtraFlameCards()
{
	if (ExtraFlameCardComponents.IsEmpty() || !InternalSurfaceMesh || !LiveComponent)
	{
		return;
	}

	const FTransform BaseTransform = InternalSurfaceMesh->GetComponentTransform();
	const FVector PlaneNormal = LiveComponent->GetPlaneBasis().N;
	const bool bIsBaseVisible = InternalSurfaceMesh->IsVisible();
	for (int32 CardIndex = 0; CardIndex < ExtraFlameCardComponents.Num(); ++CardIndex)
	{
		UStaticMeshComponent* CardComponent = ExtraFlameCardComponents[CardIndex];
		if (!CardComponent)
		{
			continue;
		}

		const int32 LayerIndex = CardIndex + 1;
		const double DepthSide = LayerIndex % 2 == 1 ? 1.0 : -1.0;
		const double DepthOffsetCm = DepthSide * FlameCardLayers.CardDepthSpacingCm * static_cast<double>((LayerIndex + 1) / 2);
		FTransform CardTransform = BaseTransform;
		CardTransform.AddToTranslation(PlaneNormal * DepthOffsetCm);
		CardTransform.SetScale3D(BaseTransform.GetScale3D() * FMath::Pow(static_cast<double>(FlameCardLayers.CardScaleStep), static_cast<double>(LayerIndex)));
		CardComponent->SetWorldTransform(CardTransform);
		CardComponent->SetVisibility(bIsBaseVisible);
	}
}

void ATDFluidFireActor::UpdateSourcePropTransform()
{
	if (!SourcePropMeshComponent || !SourcePropMeshComponent->GetStaticMesh())
	{
		return;
	}

	FTransform ParentTransform = GetActorTransform();
	ResolveSourceSocketTransform(ParentTransform);
	SourcePropMeshComponent->SetWorldTransform(SourcePropRelativeTransform * ParentTransform);
}

void ATDFluidFireActor::FinishActorSpecificOutputs(float DeltaTime)
{
	const FVector AnchorWorld = ResolvePlaneAnchorWorldLocation();
	EnsureExtraFlameCards();
	PlaceExtraFlameCards();
	if (FireLightComponent)
	{
		FireLightComponent->UpdateFromFluid(*LiveComponent, AnchorWorld, DeltaTime);
	}

	if (EmberComponent)
	{
		EmberComponent->StepEmbers(*LiveComponent, DeltaTime);
	}

	FeedDownstreamSmoke();
}

void ATDFluidFireActor::SelectDownstreamFeedCells(const FTDFluidGrid2DSolver& Solver, const FTDFluidDownstreamFeedSettings& Settings, TArray<FIntPoint>& OutCells)
{
	OutCells.Reset();
	if (!Solver.IsInitialized())
	{
		return;
	}

	CollectDownstreamFeedCells(Solver.GetResolution(), Settings, [&Solver](int32 CellX, int32 CellY)
	{
		return Solver.SampleAtCell(CellX, CellY).SelectiveDensity;
	}, OutCells);
}

void ATDFluidFireActor::SelectDownstreamFeedCells(const UTDFluidLiveComponent& FluidComponent, const FTDFluidDownstreamFeedSettings& Settings, TArray<FIntPoint>& OutCells)
{
	OutCells.Reset();
	if (!FluidComponent.IsSimulationInitialized())
	{
		return;
	}

	CollectDownstreamFeedCells(FluidComponent.GetSimulationResolution(), Settings, [&FluidComponent](int32 CellX, int32 CellY)
	{
		return FluidComponent.SampleSimulationCell(CellX, CellY).SelectiveDensity;
	}, OutCells);
}

void ATDFluidFireActor::FeedDownstreamSmoke()
{
	if (!DownstreamFeed.bIsEnabled || !DownstreamSmokeActor || !DownstreamSmokeActor->LiveComponent)
	{
		return;
	}

	UTDFluidLiveComponent* SmokeComponent = DownstreamSmokeActor->LiveComponent;
	TArray<FIntPoint> FeedCells;
	SelectDownstreamFeedCells(*LiveComponent, DownstreamFeed, FeedCells);

	const FTDFluidPlaneBasis& FireBasis = LiveComponent->GetPlaneBasis();
	const int32 Resolution = LiveComponent->GetSimulationResolution();
	const int32 Stride = FMath::Max(MinFeedSampleStride, Resolution / FeedSampleDivisor);
	const float FeedRadiusCm = DownstreamFeed.RadiusScale * static_cast<float>(Stride) * static_cast<float>(LiveComponent->GetSimulationExtentCm().X / Resolution);
	for (const FIntPoint& Cell : FeedCells)
	{
		const FTDFluidCellSample Sample = LiveComponent->SampleSimulationCell(Cell.X, Cell.Y);
		const FVector CellWorld = LiveComponent->UnprojectSolverPlaneToWorld(LiveComponent->CellToPlaneCenter(Cell.X, Cell.Y));
		const FVector2D SmokePlanePoint = SmokeComponent->ProjectWorldToSolverPlane(CellWorld);

		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = SmokeComponent->UnprojectSolverPlaneToWorld(SmokePlanePoint, 0.0);
		Stamp.WorldVelocity = FireBasis.UnprojectVector(Sample.VelocityCmPerSec) * DownstreamFeed.VelocityInheritance;
		Stamp.RadiusCm = FMath::Max(FeedRadiusCm, 1.0f);
		Stamp.DensityStrength = DownstreamFeed.DensityScale * FMath::Clamp(Sample.SelectiveDensity / FMath::Max(DownstreamFeed.FadedDensityThreshold, UE_KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		Stamp.VelocityStrength = 1.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		Stamp.HeatStrength = FMath::Clamp(Sample.Heat * DownstreamFeed.HeatScale, 0.0f, 1.0f);
		SmokeComponent->QueueBrushStamp(Stamp);
	}
}
