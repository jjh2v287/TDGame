#include "FluidFX/TDFluidCloudPainterActor.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidCloudLayersActor.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidCloudPainter, Log, All);

static TAutoConsoleVariable<int32> CVarTDFluidCloudPainter(
	TEXT("TD.FluidFX.CloudPainter"),
	-1,
	TEXT("Cloud painter: -1 uses the actor value, 0 disables every cloud painter, 1 enables every cloud painter."),
	ECVF_Default);

namespace TDFluidCloudSourcePainterPrivate
{
	constexpr double CentimetersPerKilometer = 100000.0;
	constexpr double MinimumPatternScaleKm = 0.1;
	constexpr double TilingFootprintMaxKm = 1e-6;
	constexpr double MinimumRayVerticalComponent = 1e-4;
	constexpr double SeamCrossingUvDelta = 0.5;
	constexpr double TargetSearchIntervalSeconds = 1.0;
	constexpr double MinimumFootprintSizeCm = 1.0;
	constexpr float MinimumBrushRadiusCm = 1.0f;
	constexpr int32 MinimumScriptedStrokeArgumentCount = 5;
	const TCHAR* const TerrainHeightRangeParameterName = TEXT("CloudTerrainHeightRangeKm");

	bool HasFootprint(const FTDFluidCloudPaintFootprint& Footprint)
	{
		return Footprint.SizeKm.X > TilingFootprintMaxKm;
	}

	double ResolvePatternScaleCm(const FTDFluidCloudPaintFootprint& Footprint)
	{
		return FMath::Max(static_cast<double>(Footprint.PatternScaleKm), MinimumPatternScaleKm) * CentimetersPerKilometer;
	}

	double ComputeFraction(double Value)
	{
		return Value - FMath::FloorToDouble(Value);
	}

	FVector2D ComputeFootprintUv(const FVector2D& DisplayPointCm, const FTDFluidCloudPaintFootprint& Footprint)
	{
		const FVector2D SizeCm(
			FMath::Max(Footprint.SizeKm.X * CentimetersPerKilometer, MinimumFootprintSizeCm),
			FMath::Max(Footprint.SizeKm.Y * CentimetersPerKilometer, MinimumFootprintSizeCm));
		return (DisplayPointCm - Footprint.CenterKm * CentimetersPerKilometer) / SizeCm + FVector2D(0.5, 0.5);
	}

	FVector2D ComputeTilingUv(const FVector2D& DisplayPointCm, const FTDFluidCloudPaintFootprint& Footprint)
	{
		const double PatternScaleCm = ResolvePatternScaleCm(Footprint);
		return FVector2D(ComputeFraction(DisplayPointCm.X / PatternScaleCm), ComputeFraction(DisplayPointCm.Y / PatternScaleCm));
	}

	bool IsInsideUnitSquare(const FVector2D& Uv)
	{
		return Uv.X >= 0.0 && Uv.X <= 1.0 && Uv.Y >= 0.0 && Uv.Y <= 1.0;
	}

	float ReadSlotScalar(UMaterialInstanceDynamic& Material, int32 SlotIndex, const TCHAR* Suffix)
	{
		return Material.K2_GetScalarParameterValue(ATDFluidCloudLayersActor::MakeSlotParameterName(SlotIndex, Suffix));
	}

	FLinearColor ReadSlotVector(UMaterialInstanceDynamic& Material, int32 SlotIndex, const TCHAR* Suffix)
	{
		return Material.K2_GetVectorParameterValue(ATDFluidCloudLayersActor::MakeSlotParameterName(SlotIndex, Suffix));
	}

	FTDFluidCloudPaintFootprint ReadFootprint(UMaterialInstanceDynamic& Material, int32 SlotIndex)
	{
		const FLinearColor CenterKm = ReadSlotVector(Material, SlotIndex, TEXT("FootprintCenterKm"));
		const FLinearColor SizeKm = ReadSlotVector(Material, SlotIndex, TEXT("FootprintSizeKm"));
		const FLinearColor TerrainHeightRangeKm = Material.K2_GetVectorParameterValue(FName(TerrainHeightRangeParameterName));
		const double LayerMidKm = ReadSlotScalar(Material, SlotIndex, TEXT("BottomKm"))
			+ 0.5 * ReadSlotScalar(Material, SlotIndex, TEXT("ThicknessKm"))
			+ ReadSlotScalar(Material, SlotIndex, TEXT("TerrainFollow")) * TerrainHeightRangeKm.R;

		FTDFluidCloudPaintFootprint Footprint;
		Footprint.CenterKm = FVector2D(CenterKm.R, CenterKm.G);
		Footprint.SizeKm = FVector2D(SizeKm.R, SizeKm.G);
		Footprint.PatternScaleKm = FMath::Max(ReadSlotScalar(Material, SlotIndex, TEXT("PatternScaleKm")), static_cast<float>(MinimumPatternScaleKm));
		Footprint.LayerAltitudeCm = static_cast<float>(LayerMidKm * CentimetersPerKilometer);
		return Footprint;
	}

	const FTDFluidCloudSlotSource* FindPaintSlot(const FTDFluidCloudSetup& Setup, const TArray<FName>& TargetLiveActorTags)
	{
		for (const FTDFluidCloudSlotSource& SlotSource : Setup.SlotSources)
		{
			if (SlotSource.Role == ETDFluidCloudSlotTextureRole::Coverage && TargetLiveActorTags.Contains(SlotSource.LiveActorTag))
			{
				return &SlotSource;
			}
		}
		return nullptr;
	}

	ATDFluidCloudLayersActor* FindFirstActiveCloudActor(UWorld& World)
	{
		for (TActorIterator<ATDFluidCloudLayersActor> It(&World); It; ++It)
		{
			if (!It->IsYieldingSceneCloud())
			{
				return *It;
			}
		}
		return nullptr;
	}

	ATDFluidLiveActor* FindFirstLiveActorWithTag(UWorld& World, FName LiveActorTag)
	{
		ATDFluidLiveActor* FoundActor = nullptr;
		for (TActorIterator<ATDFluidLiveActor> It(&World); It; ++It)
		{
			if (!It->ActorHasTag(LiveActorTag))
			{
				continue;
			}
			if (!FoundActor || It->GetFName().LexicalLess(FoundActor->GetFName()))
			{
				FoundActor = *It;
			}
		}
		return FoundActor;
	}

	void QueueScriptedStrokeOnPainters(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < MinimumScriptedStrokeArgumentCount)
		{
			UE_LOG(LogTDFluidCloudPainter, Warning, TEXT("usage: TD.FluidFX.CloudPaintStroke <startXKm> <startYKm> <endXKm> <endYKm> <seconds> [erase 0|1]"));
			return;
		}

		const FVector2D StartKm(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]));
		const FVector2D EndKm(FCString::Atod(*Args[2]), FCString::Atod(*Args[3]));
		const float Seconds = FCString::Atof(*Args[4]);
		const bool bErase = Args.Num() > MinimumScriptedStrokeArgumentCount && FCString::Atoi(*Args[MinimumScriptedStrokeArgumentCount]) != 0;
		int32 PainterCount = 0;
		for (TActorIterator<ATDFluidCloudPainterActor> It(World); It; ++It)
		{
			It->QueueScriptedStroke(StartKm, EndKm, Seconds, bErase);
			++PainterCount;
		}
		UE_LOG(LogTDFluidCloudPainter, Log, TEXT("queued a scripted cloud stroke on %d painters"), PainterCount);
	}
}

static FAutoConsoleCommandWithWorldAndArgs CommandTDFluidCloudPaintStroke(
	TEXT("TD.FluidFX.CloudPaintStroke"),
	TEXT("Queues a scripted cloud paint stroke on every cloud painter: <startXKm> <startYKm> <endXKm> <endYKm> <seconds> [erase 0|1]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TDFluidCloudSourcePainterPrivate::QueueScriptedStrokeOnPainters));

ATDFluidCloudPainterActor::ATDFluidCloudPainterActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	TargetLiveActorTags.Add(FName(TEXT("TDFluidCloud_Paint")));
}

void ATDFluidCloudPainterActor::BeginPlay()
{
	Super::BeginPlay();
	ReadSceneSettings();
	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr)
	{
		PresetTuningReloadedHandle = TuningSubsystem->OnPresetTuningReloaded.AddUObject(this, &ATDFluidCloudPainterActor::HandlePresetTuningReloaded);
	}
	ShowMouseCursor();
}

void ATDFluidCloudPainterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr)
	{
		TuningSubsystem->OnPresetTuningReloaded.Remove(PresetTuningReloadedHandle);
	}
	PresetTuningReloadedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATDFluidCloudPainterActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!ResolvePainterEnabled(CVarTDFluidCloudPainter.GetValueOnGameThread(), bIsPainterEnabled))
	{
		EndStroke();
		return;
	}

	APlayerController* PlayerController = GetFirstLocalPlayerController();
	const bool bIsPaintKeyDown = PlayerController && PlayerController->IsInputKeyDown(PaintKey);
	if (!bHasScriptedStroke && !bIsPaintKeyDown)
	{
		EndStroke();
		return;
	}

	if (bHasScriptedStroke)
	{
		ScriptedElapsedSeconds += DeltaSeconds;
	}

	const bool bErase = bHasScriptedStroke ? bIsScriptedErase : PlayerController->IsInputKeyDown(EraseModifierKey);
	ATDFluidLiveActor* LiveActor = nullptr;
	FTDFluidCloudPaintFootprint Footprint;
	FVector DisplayPointCm = FVector::ZeroVector;
	if (ResolvePaintTarget(LiveActor, Footprint) && ResolveDisplayPoint(PlayerController, Footprint, DisplayPointCm))
	{
		PaintAtDisplayPoint(*LiveActor, Footprint, DisplayPointCm, bErase, DeltaSeconds);
	}
	else
	{
		EndStroke();
	}
	FinishScriptedStrokeIfElapsed();
}

bool ATDFluidCloudPainterActor::IntersectRayWithAltitude(const FVector& RayOrigin, const FVector& RayDirection, double AltitudeCm, FVector& OutPoint)
{
	if (FMath::Abs(RayDirection.Z) <= TDFluidCloudSourcePainterPrivate::MinimumRayVerticalComponent)
	{
		return false;
	}

	const double RayDistance = (AltitudeCm - RayOrigin.Z) / RayDirection.Z;
	if (RayDistance <= 0.0)
	{
		return false;
	}

	OutPoint = RayOrigin + RayDirection * RayDistance;
	return true;
}

bool ATDFluidCloudPainterActor::MapDisplayPointToSimulation(const FVector2D& DisplayPointCm, const FTDFluidCloudPaintFootprint& Footprint, const FVector2D& SimCenterPlaneCm, const FVector2D& SimExtentCm, FVector2D& OutPlanePoint, FVector2D& OutUv)
{
	using namespace TDFluidCloudSourcePainterPrivate;
	OutUv = HasFootprint(Footprint) ? ComputeFootprintUv(DisplayPointCm, Footprint) : ComputeTilingUv(DisplayPointCm, Footprint);
	if (!IsInsideUnitSquare(OutUv))
	{
		return false;
	}

	OutPlanePoint = SimCenterPlaneCm + (OutUv - FVector2D(0.5, 0.5)) * SimExtentCm;
	return true;
}

double ATDFluidCloudPainterActor::ComputeDisplayToSimulationScale(const FTDFluidCloudPaintFootprint& Footprint, const FVector2D& SimExtentCm)
{
	using namespace TDFluidCloudSourcePainterPrivate;
	const double DisplaySpanCm = HasFootprint(Footprint) ? Footprint.SizeKm.X * CentimetersPerKilometer : ResolvePatternScaleCm(Footprint);
	return SimExtentCm.X / DisplaySpanCm;
}

float ATDFluidCloudPainterActor::ComputeSimulationBrushRadiusCm(float BrushRadiusKm, double DisplayToSimulationScale)
{
	const double RadiusCm = BrushRadiusKm * TDFluidCloudSourcePainterPrivate::CentimetersPerKilometer * DisplayToSimulationScale;
	return FMath::Max(static_cast<float>(RadiusCm), TDFluidCloudSourcePainterPrivate::MinimumBrushRadiusCm);
}

int32 ATDFluidCloudPainterActor::ComputePaintStampCount(double SegmentLengthCm, float RadiusCm, float SpacingRatio, int32 MaxStampsPerTick, bool bCrossesSeam)
{
	if (bCrossesSeam)
	{
		return 1;
	}

	return FMath::Clamp(ATDFluidLiveActor::ComputeStrokeStampCount(SegmentLengthCm, RadiusCm, SpacingRatio), 1, FMath::Max(MaxStampsPerTick, 1));
}

bool ATDFluidCloudPainterActor::IsSeamCrossing(const FVector2D& PreviousUv, const FVector2D& Uv)
{
	const FVector2D UvDelta = Uv - PreviousUv;
	return FMath::Abs(UvDelta.X) > TDFluidCloudSourcePainterPrivate::SeamCrossingUvDelta || FMath::Abs(UvDelta.Y) > TDFluidCloudSourcePainterPrivate::SeamCrossingUvDelta;
}

bool ATDFluidCloudPainterActor::ResolvePainterEnabled(int32 CVarValue, bool bActorValue)
{
	return CVarValue < 0 ? bActorValue : CVarValue > 0;
}

void ATDFluidCloudPainterActor::QueueScriptedStroke(const FVector2D& StartKm, const FVector2D& EndKm, float Seconds, bool bErase)
{
	EndStroke();
	ScriptedStartKm = StartKm;
	ScriptedEndKm = EndKm;
	ScriptedDurationSeconds = FMath::Max(Seconds, 0.0f);
	ScriptedElapsedSeconds = 0.0f;
	bIsScriptedErase = bErase;
	bHasScriptedStroke = true;
}

void ATDFluidCloudPainterActor::PaintAtDisplayPoint(ATDFluidLiveActor& LiveActor, const FTDFluidCloudPaintFootprint& Footprint, const FVector& DisplayPointCm, bool bErase, float DeltaSeconds)
{
	UTDFluidLiveComponent* LiveComponent = LiveActor.LiveComponent;
	if (!LiveComponent || !LiveComponent->IsSimulationAwake() || !LiveComponent->IsSimulationInitialized() || DeltaSeconds <= 0.0f)
	{
		EndStroke();
		return;
	}

	const FVector2D SimExtentCm = LiveComponent->GetSimulationExtentCm();
	FVector2D PlanePoint;
	FVector2D Uv;
	if (!MapDisplayPointToSimulation(FVector2D(DisplayPointCm.X, DisplayPointCm.Y), Footprint, LiveComponent->GetSimulationCenterWorld(), SimExtentCm, PlanePoint, Uv))
	{
		EndStroke();
		return;
	}

	const bool bCrossesSeam = bHasPreviousPoint && IsSeamCrossing(PreviousUv, Uv);
	const bool bHasMotion = bHasPreviousPoint && !bCrossesSeam;
	const FVector2D StartPoint = bHasMotion ? PreviousPlanePoint : PlanePoint;
	const FVector2D PlaneVelocity = bHasMotion ? (PlanePoint - PreviousPlanePoint) * (SceneSettings.PainterVelocityScale / DeltaSeconds) : FVector2D::ZeroVector;
	const FTDFluidInputBrushSettings& InputBrushSettings = LiveComponent->GetInputBrushSettings();
	const float RadiusCm = ComputeSimulationBrushRadiusCm(SceneSettings.PainterBrushRadiusKm, ComputeDisplayToSimulationScale(Footprint, SimExtentCm));
	const int32 StampCount = ComputePaintStampCount(FVector2D::Distance(StartPoint, PlanePoint), RadiusCm, InputBrushSettings.StrokeSpacingRatio, SceneSettings.PainterMaxStampsPerTick, bCrossesSeam);
	const float DensityMagnitude = FMath::Abs(InputBrushSettings.MouseBrushDensityStrength);

	FTDFluidBrushStamp Stamp;
	Stamp.WorldVelocity = LiveComponent->GetPlaneBasis().UnprojectVector(PlaneVelocity);
	Stamp.RadiusCm = RadiusCm;
	Stamp.DensityStrength = bErase ? -DensityMagnitude : DensityMagnitude;
	Stamp.VelocityStrength = 1.0f;
	Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
	for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
	{
		const double Alpha = static_cast<double>(StampIndex + 1) / static_cast<double>(StampCount);
		Stamp.WorldLocation = LiveComponent->UnprojectSolverPlaneToWorld(StartPoint + (PlanePoint - StartPoint) * Alpha);
		LiveComponent->QueueBrushStamp(Stamp);
	}

	PreviousPlanePoint = PlanePoint;
	PreviousUv = Uv;
	bHasPreviousPoint = true;
}

void ATDFluidCloudPainterActor::EndStroke()
{
	bHasPreviousPoint = false;
}

void ATDFluidCloudPainterActor::HandlePresetTuningReloaded(int32 TableGeneration)
{
	ReadSceneSettings();
}

void ATDFluidCloudPainterActor::ReadSceneSettings()
{
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFluidCloudSceneSettings::StaticStruct(), &SceneSettings);
}

void ATDFluidCloudPainterActor::ShowMouseCursor() const
{
	if (!bShowsMouseCursor || !ResolvePainterEnabled(CVarTDFluidCloudPainter.GetValueOnGameThread(), bIsPainterEnabled))
	{
		return;
	}

	if (APlayerController* PlayerController = GetFirstLocalPlayerController())
	{
		PlayerController->bShowMouseCursor = true;
	}
}

void ATDFluidCloudPainterActor::FinishScriptedStrokeIfElapsed()
{
	if (!bHasScriptedStroke || ScriptedElapsedSeconds < ScriptedDurationSeconds)
	{
		return;
	}

	bHasScriptedStroke = false;
	EndStroke();
}

bool ATDFluidCloudPainterActor::ResolvePaintTarget(ATDFluidLiveActor*& OutLiveActor, FTDFluidCloudPaintFootprint& OutFootprint)
{
	ATDFluidCloudLayersActor* CloudActor = ResolveCloudActor();
	UMaterialInstanceDynamic* ActiveMaterial = CloudActor ? CloudActor->GetActiveMaterial() : nullptr;
	if (!ActiveMaterial || !CloudActor->Setups.IsValidIndex(CloudActor->GetActiveSetupIndex()))
	{
		return false;
	}

	const FTDFluidCloudSlotSource* PaintSlot = TDFluidCloudSourcePainterPrivate::FindPaintSlot(CloudActor->Setups[CloudActor->GetActiveSetupIndex()], TargetLiveActorTags);
	OutLiveActor = PaintSlot ? ResolveLiveActor(PaintSlot->LiveActorTag) : nullptr;
	if (!OutLiveActor)
	{
		return false;
	}

	OutFootprint = TDFluidCloudSourcePainterPrivate::ReadFootprint(*ActiveMaterial, PaintSlot->SlotIndex);
	return true;
}

ATDFluidCloudLayersActor* ATDFluidCloudPainterActor::ResolveCloudActor()
{
	ATDFluidCloudLayersActor* CloudActor = CachedCloudActor.Get();
	if (CloudActor && !CloudActor->IsYieldingSceneCloud())
	{
		return CloudActor;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() - LastTargetSearchSeconds < TDFluidCloudSourcePainterPrivate::TargetSearchIntervalSeconds)
	{
		return nullptr;
	}

	LastTargetSearchSeconds = World->GetTimeSeconds();
	CachedCloudActor = TDFluidCloudSourcePainterPrivate::FindFirstActiveCloudActor(*World);
	return CachedCloudActor.Get();
}

ATDFluidLiveActor* ATDFluidCloudPainterActor::ResolveLiveActor(FName LiveActorTag)
{
	ATDFluidLiveActor* LiveActor = CachedLiveActor.Get();
	if (LiveActor && CachedLiveActorTag == LiveActorTag && LiveActor->ActorHasTag(LiveActorTag))
	{
		return LiveActor;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	CachedLiveActorTag = LiveActorTag;
	CachedLiveActor = TDFluidCloudSourcePainterPrivate::FindFirstLiveActorWithTag(*World, LiveActorTag);
	return CachedLiveActor.Get();
}

bool ATDFluidCloudPainterActor::ResolveDisplayPoint(const APlayerController* PlayerController, const FTDFluidCloudPaintFootprint& Footprint, FVector& OutDisplayPointCm) const
{
	if (!bHasScriptedStroke)
	{
		return PlayerController && TryGetCursorDisplayPoint(*PlayerController, Footprint.LayerAltitudeCm, OutDisplayPointCm);
	}

	const double StrokeAlpha = ScriptedDurationSeconds > 0.0f ? FMath::Clamp(static_cast<double>(ScriptedElapsedSeconds) / ScriptedDurationSeconds, 0.0, 1.0) : 1.0;
	const FVector2D StrokePointKm = ScriptedStartKm + (ScriptedEndKm - ScriptedStartKm) * StrokeAlpha;
	OutDisplayPointCm = FVector(StrokePointKm * TDFluidCloudSourcePainterPrivate::CentimetersPerKilometer, Footprint.LayerAltitudeCm);
	return true;
}

bool ATDFluidCloudPainterActor::TryGetCursorDisplayPoint(const APlayerController& PlayerController, float LayerAltitudeCm, FVector& OutDisplayPointCm) const
{
	FVector RayOrigin;
	FVector RayDirection;
	if (!PlayerController.DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	if (SceneSettings.PainterProjection == ETDFluidCloudPainterProjection::LayerMidHeight)
	{
		return IntersectRayWithAltitude(RayOrigin, RayDirection, LayerAltitudeCm, OutDisplayPointCm);
	}

	FHitResult CursorHit;
	if (PlayerController.GetHitResultUnderCursor(ECC_Visibility, false, CursorHit) && CursorHit.bBlockingHit)
	{
		OutDisplayPointCm = CursorHit.Location;
		return true;
	}
	return IntersectRayWithAltitude(RayOrigin, RayDirection, 0.0, OutDisplayPointCm);
}

APlayerController* ATDFluidCloudPainterActor::GetFirstLocalPlayerController() const
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return PlayerController && PlayerController->IsLocalController() ? PlayerController : nullptr;
}
