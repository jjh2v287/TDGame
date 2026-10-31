#include "FluidFX/TDFluidCloudWindCurtainActor.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidCloudWind, Log, All);

static TAutoConsoleVariable<int32> CVarTDFluidCloudWindCurtain(
	TEXT("TD.FluidFX.CloudWindCurtain"),
	-1,
	TEXT("Cloud wind curtain: -1 uses the actor value, 0 disables every wind curtain, 1 enables every wind curtain."),
	ECVF_Default);

namespace TDFluidCloudSourceWindPrivate
{
	constexpr int32 MaximumCurtainStampCount = 64;
	constexpr double GoldenRatioConjugate = 0.6180339887;
	constexpr double MinimumGustPeriodSeconds = 0.5;
	constexpr double TargetSearchIntervalSeconds = 1.0;
	constexpr float MinimumStampRadiusCm = 1.0f;
	constexpr float CurtainVelocityStrength = 1.0f;
	constexpr float EyeSinkVelocityStrength = 0.0f;

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

	UTDFluidLiveComponent* FindSteppingComponent(ATDFluidLiveActor* LiveActor)
	{
		UTDFluidLiveComponent* LiveComponent = LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
		return LiveComponent && LiveComponent->IsSimulationAwake() && LiveComponent->IsSimulationInitialized() ? LiveComponent : nullptr;
	}

	void QueuePlaneStamp(UTDFluidLiveComponent& LiveComponent, const FTDFluidCloudWindCurtainStamp& PlaneStamp, float VelocityStrength)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = LiveComponent.UnprojectSolverPlaneToWorld(PlaneStamp.PlanePointCm);
		Stamp.WorldVelocity = LiveComponent.GetPlaneBasis().UnprojectVector(PlaneStamp.PlaneVelocityCmPerSec);
		Stamp.RadiusCm = PlaneStamp.RadiusCm;
		Stamp.DensityStrength = PlaneStamp.DensityStrength;
		Stamp.VelocityStrength = VelocityStrength;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		LiveComponent.QueueBrushStamp(Stamp);
	}
}

ATDFluidCloudWindCurtainActor::ATDFluidCloudWindCurtainActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ATDFluidCloudWindCurtainActor::BeginPlay()
{
	Super::BeginPlay();
	ReadSceneSettings();
	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr)
	{
		PresetTuningReloadedHandle = TuningSubsystem->OnPresetTuningReloaded.AddUObject(this, &ATDFluidCloudWindCurtainActor::HandlePresetTuningReloaded);
	}
}

void ATDFluidCloudWindCurtainActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr)
	{
		TuningSubsystem->OnPresetTuningReloaded.Remove(PresetTuningReloadedHandle);
	}
	PresetTuningReloadedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATDFluidCloudWindCurtainActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World || !ResolveWindCurtainEnabled(CVarTDFluidCloudWindCurtain.GetValueOnGameThread(), bIsWindCurtainEnabled))
	{
		return;
	}

	QueueWindCurtain();
	QueueEyeSink();
}

void ATDFluidCloudWindCurtainActor::BuildWindCurtainStamps(const FTDFluidCloudSceneSettings& Settings, const FVector2D& SimCenterPlaneCm, const FVector2D& SimExtentCm, double TimeSeconds, float ActorBrushDensityStrength, TArray<FTDFluidCloudWindCurtainStamp>& OutStamps)
{
	using namespace TDFluidCloudSourceWindPrivate;
	OutStamps.Reset();
	const int32 StampCount = FMath::Clamp(Settings.WindCurtainStampCount, 1, MaximumCurtainStampCount);
	const double SpanCm = Settings.WindCurtainSpanRatio * SimExtentCm.X;
	const double RowCm = SimCenterPlaneCm.Y - 0.5 * SimExtentCm.Y + Settings.WindCurtainHeightRatio * SimExtentCm.Y;
	const float RadiusCm = FMath::Max(static_cast<float>(0.5 * Settings.WindCurtainThicknessRatio * SimExtentCm.Y), MinimumStampRadiusCm);
	const double GustPeriodSeconds = FMath::Max(static_cast<double>(Settings.WindCurtainGustPeriodSeconds), MinimumGustPeriodSeconds);
	const float DensityStrength = FMath::IsFinite(ActorBrushDensityStrength) ? ActorBrushDensityStrength : 0.0f;
	OutStamps.Reserve(StampCount);
	for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
	{
		const double GustPhase = 2.0 * UE_DOUBLE_PI * (TimeSeconds / GustPeriodSeconds + GoldenRatioConjugate * StampIndex);
		const double SpeedCmPerSec = Settings.WindCurtainSpeedCmPerSec * (1.0 + Settings.WindCurtainGustAmplitude * FMath::Sin(GustPhase));

		FTDFluidCloudWindCurtainStamp& Stamp = OutStamps.AddDefaulted_GetRef();
		Stamp.PlanePointCm = FVector2D(SimCenterPlaneCm.X - 0.5 * SpanCm + SpanCm * (StampIndex + 0.5) / StampCount, RowCm);
		Stamp.PlaneVelocityCmPerSec = FVector2D(SpeedCmPerSec, SpeedCmPerSec * Settings.WindCurtainLiftRatio);
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = DensityStrength;
	}
}

bool ATDFluidCloudWindCurtainActor::BuildEyeSinkStamp(const FTDFluidCloudSceneSettings& Settings, const FVector2D& SimCenterPlaneCm, const FVector2D& SimExtentCm, FTDFluidCloudWindCurtainStamp& OutStamp)
{
	OutStamp = FTDFluidCloudWindCurtainStamp();
	OutStamp.PlanePointCm = SimCenterPlaneCm;
	OutStamp.RadiusCm = FMath::Max(static_cast<float>(Settings.CycloneEyeRadiusRatio * FMath::Min(SimExtentCm.X, SimExtentCm.Y)), TDFluidCloudSourceWindPrivate::MinimumStampRadiusCm);
	OutStamp.DensityStrength = -Settings.CycloneEyeEraseStrength;
	return Settings.CycloneEyeEraseStrength > 0.0f;
}

bool ATDFluidCloudWindCurtainActor::ResolveWindCurtainEnabled(int32 CVarValue, bool bActorValue)
{
	return CVarValue < 0 ? bActorValue : CVarValue > 0;
}

void ATDFluidCloudWindCurtainActor::HandlePresetTuningReloaded(int32 TableGeneration)
{
	ReadSceneSettings();
}

void ATDFluidCloudWindCurtainActor::ReadSceneSettings()
{
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFluidCloudSceneSettings::StaticStruct(), &SceneSettings);
}

void ATDFluidCloudWindCurtainActor::QueueWindCurtain()
{
	using namespace TDFluidCloudSourceWindPrivate;
	UTDFluidLiveComponent* LiveComponent = FindSteppingComponent(ResolveLiveActor(TargetLiveActorTag, CachedCurtainLiveActor, LastCurtainSearchSeconds));
	if (!LiveComponent)
	{
		return;
	}

	BuildWindCurtainStamps(SceneSettings, LiveComponent->GetSimulationCenterWorld(), LiveComponent->GetSimulationExtentCm(), LiveComponent->GetSimulationTimeSeconds(), LiveComponent->SimulationConfig.ActorBrushDensityStrength, CurtainStamps);
	for (const FTDFluidCloudWindCurtainStamp& CurtainStamp : CurtainStamps)
	{
		QueuePlaneStamp(*LiveComponent, CurtainStamp, CurtainVelocityStrength);
	}
}

void ATDFluidCloudWindCurtainActor::QueueEyeSink()
{
	using namespace TDFluidCloudSourceWindPrivate;
	UTDFluidLiveComponent* LiveComponent = FindSteppingComponent(ResolveLiveActor(EyeSinkTargetLiveActorTag, CachedEyeSinkLiveActor, LastEyeSinkSearchSeconds));
	FTDFluidCloudWindCurtainStamp EyeSinkStamp;
	if (!LiveComponent || !BuildEyeSinkStamp(SceneSettings, LiveComponent->GetSimulationCenterWorld(), LiveComponent->GetSimulationExtentCm(), EyeSinkStamp))
	{
		return;
	}

	QueuePlaneStamp(*LiveComponent, EyeSinkStamp, EyeSinkVelocityStrength);
}

ATDFluidLiveActor* ATDFluidCloudWindCurtainActor::ResolveLiveActor(FName LiveActorTag, TWeakObjectPtr<ATDFluidLiveActor>& CachedActor, double& LastSearchSeconds) const
{
	ATDFluidLiveActor* LiveActor = CachedActor.Get();
	if (LiveActor && LiveActor->ActorHasTag(LiveActorTag))
	{
		return LiveActor;
	}

	UWorld* World = GetWorld();
	if (LiveActorTag.IsNone() || !World || World->GetTimeSeconds() - LastSearchSeconds < TDFluidCloudSourceWindPrivate::TargetSearchIntervalSeconds)
	{
		return nullptr;
	}

	LastSearchSeconds = World->GetTimeSeconds();
	CachedActor = TDFluidCloudSourceWindPrivate::FindFirstLiveActorWithTag(*World, LiveActorTag);
	UE_CLOG(!CachedActor.IsValid(), LogTDFluidCloudWind, Verbose, TEXT("wind curtain target %s not found"), *LiveActorTag.ToString());
	return CachedActor.Get();
}
