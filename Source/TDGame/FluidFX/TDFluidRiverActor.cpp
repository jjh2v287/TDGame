#include "FluidFX/TDFluidRiverActor.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "Materials/MaterialInterface.h"

namespace
{
	constexpr float RiverMinimumWidthScale = 0.25f;
	constexpr float RiverMinimumGuideSpacingCm = 10.0f;
	constexpr float RiverMinimumRibbonSegmentLengthCm = 50.0f;
	constexpr float RiverMinimumSplineLengthCm = 1.0f;
	constexpr float RiverMinimumMeshWidthCm = 1.0f;
	constexpr float RiverSegmentCountTolerance = 0.01f;
	constexpr int32 RiverRibbonFlowSpeedDataIndex = 0;
	const TCHAR* const RiverWaterSurfaceTagName = TEXT("TDFluidWaterSurface");
}

ATDFluidRiverActor::ATDFluidRiverActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RiverSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RiverSpline"));
	SetRootComponent(RiverSpline);

	Tags.AddUnique(FName(RiverWaterSurfaceTagName));
}

void ATDFluidRiverActor::BeginPlay()
{
	Super::BeginPlay();

	const UWorld* World = GetWorld();
	if (UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr)
	{
		FluidSubsystem->RegisterFlowGuideSource(this);
	}

	if (!bBuildRibbonSurface || RibbonMesh.IsNull())
	{
		return;
	}

	TArray<FSoftObjectPath> AssetsToLoad;
	AssetsToLoad.Add(RibbonMesh.ToSoftObjectPath());
	if (!RibbonMaterial.IsNull())
	{
		AssetsToLoad.Add(RibbonMaterial.ToSoftObjectPath());
	}

	RibbonLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &ATDFluidRiverActor::HandleRibbonAssetsLoaded));
}

void ATDFluidRiverActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RibbonLoadHandle.IsValid())
	{
		RibbonLoadHandle->CancelHandle();
		RibbonLoadHandle.Reset();
	}

	const UWorld* World = GetWorld();
	if (UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr)
	{
		FluidSubsystem->UnregisterFlowGuideSource(this);
	}

	Super::EndPlay(EndPlayReason);
}

void ATDFluidRiverActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!bBuildRibbonSurface)
	{
		ClearRibbonSegments();
		return;
	}

	UStaticMesh* PreviewMesh = RibbonMesh.Get();
	if (!PreviewMesh)
	{
		return;
	}

	RebuildRibbonSegments(*PreviewMesh, RibbonMaterial.Get());
}

void ATDFluidRiverActor::BuildFlowGuide(FTDFluidFlowGuide& OutGuide) const
{
	OutGuide.Points.Reset();
	if (!RiverSpline)
	{
		return;
	}

	const float SplineLengthCm = RiverSpline->GetSplineLength();
	if (SplineLengthCm < RiverMinimumSplineLengthCm)
	{
		return;
	}

	const float SpacingCm = FMath::Max(GuideSampleSpacingCm, RiverMinimumGuideSpacingCm);
	const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(SplineLengthCm / SpacingCm - RiverSegmentCountTolerance));
	OutGuide.Points.Reserve(SegmentCount + 1);
	for (int32 PointIndex = 0; PointIndex <= SegmentCount; ++PointIndex)
	{
		const float DistanceCm = PointIndex == SegmentCount ? SplineLengthCm : SpacingCm * PointIndex;
		const float WidthScale = RiverSpline->GetScaleAtDistanceAlongSpline(DistanceCm).Y;

		FTDFluidFlowGuidePoint& Point = OutGuide.Points.AddDefaulted_GetRef();
		Point.WorldLocation = RiverSpline->GetLocationAtDistanceAlongSpline(DistanceCm, ESplineCoordinateSpace::World);
		Point.HalfWidthCm = RiverWidthCm * WidthScale * 0.5f;
		Point.FlowSpeedCmPerSec = ResolveFlowSpeedForWidthScale(WidthScale);
	}
}

float ATDFluidRiverActor::ResolveFlowSpeedForWidthScale(float WidthScale) const
{
	if (!bConserveDischargeAcrossWidth)
	{
		return FlowSpeedCmPerSec;
	}

	return FlowSpeedCmPerSec / FMath::Max(WidthScale, RiverMinimumWidthScale);
}

void ATDFluidRiverActor::HandleRibbonAssetsLoaded()
{
	UStaticMesh* LoadedMesh = RibbonMesh.Get();
	if (!LoadedMesh)
	{
		return;
	}

	RebuildRibbonSegments(*LoadedMesh, RibbonMaterial.Get());

	const UWorld* World = GetWorld();
	if (UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr)
	{
		FluidSubsystem->NotifyWaterSurfacesChanged();
	}
}

void ATDFluidRiverActor::RebuildRibbonSegments(UStaticMesh& Mesh, UMaterialInterface* Material)
{
	ClearRibbonSegments();

	const float SplineLengthCm = RiverSpline ? RiverSpline->GetSplineLength() : 0.0f;
	if (SplineLengthCm < RiverMinimumSplineLengthCm)
	{
		return;
	}

	const float TargetLengthCm = FMath::Max(RibbonSegmentLengthCm, RiverMinimumRibbonSegmentLengthCm);
	const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(SplineLengthCm / TargetLengthCm - RiverSegmentCountTolerance));
	const float SegmentLengthCm = SplineLengthCm / SegmentCount;
	const float MeshWidthCm = FMath::Max(static_cast<float>(Mesh.GetBounds().BoxExtent.Y * 2.0), RiverMinimumMeshWidthCm);

	RibbonSegments.Reserve(SegmentCount);
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const float StartDistanceCm = SegmentLengthCm * SegmentIndex;
		const float EndDistanceCm = SegmentLengthCm * (SegmentIndex + 1);
		const float StartWidthScale = RiverSpline->GetScaleAtDistanceAlongSpline(StartDistanceCm).Y;
		const float EndWidthScale = RiverSpline->GetScaleAtDistanceAlongSpline(EndDistanceCm).Y;
		const float MiddleWidthScale = RiverSpline->GetScaleAtDistanceAlongSpline((StartDistanceCm + EndDistanceCm) * 0.5f).Y;

		USplineMeshComponent* Segment = NewObject<USplineMeshComponent>(this, NAME_None, RF_Transient);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetupAttachment(RiverSpline);
		Segment->SetForwardAxis(ESplineMeshAxis::X, false);
		Segment->SetStaticMesh(&Mesh);
		Segment->SetStartAndEnd(
			RiverSpline->GetLocationAtDistanceAlongSpline(StartDistanceCm, ESplineCoordinateSpace::Local),
			RiverSpline->GetDirectionAtDistanceAlongSpline(StartDistanceCm, ESplineCoordinateSpace::Local) * SegmentLengthCm,
			RiverSpline->GetLocationAtDistanceAlongSpline(EndDistanceCm, ESplineCoordinateSpace::Local),
			RiverSpline->GetDirectionAtDistanceAlongSpline(EndDistanceCm, ESplineCoordinateSpace::Local) * SegmentLengthCm,
			false);
		Segment->SetStartScale(FVector2D(RiverWidthCm * StartWidthScale / MeshWidthCm, 1.0), false);
		Segment->SetEndScale(FVector2D(RiverWidthCm * EndWidthScale / MeshWidthCm, 1.0), false);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCastShadow(false);
		Segment->SetDefaultCustomPrimitiveDataFloat(RiverRibbonFlowSpeedDataIndex, ResolveFlowSpeedForWidthScale(MiddleWidthScale));
		if (Material)
		{
			Segment->SetMaterial(0, Material);
		}
		Segment->RegisterComponent();
		Segment->UpdateMesh();
		RibbonSegments.Add(Segment);
	}
}

void ATDFluidRiverActor::ClearRibbonSegments()
{
	TInlineComponentArray<USplineMeshComponent*> OwnedSplineMeshes(this);
	for (USplineMeshComponent* Segment : OwnedSplineMeshes)
	{
		if (Segment && (Segment->HasAnyFlags(RF_Transient) || RibbonSegments.Contains(Segment)))
		{
			Segment->DestroyComponent();
		}
	}
	RibbonSegments.Reset();
}
