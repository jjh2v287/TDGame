#include "FluidFX/TDFog3DZoneActor.h"

#include "Characters/TDCombatCharacter.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CoreGlobals.h"
#include "Engine/AssetManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFluidTerrainCapture.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RenderingThread.h"
#include "SceneInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFog3DZone, Log, All);
CSV_DECLARE_CATEGORY_MODULE_EXTERN(TDFLUIDGPU_API, TDFog3D);

namespace TDFog3DZoneActorPrivate
{
	TAutoConsoleVariable<int32> CVarTDFog3DMode(
		TEXT("TD.Fog3D.Mode"),
		-1,
		TEXT("3D fog mode override. -1 uses Globals.TDFog3DSettings.Mode, 0 off (base fog only), 1 windless, 2 full."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarTDFog3DQuality(
		TEXT("TD.Fog3D.Quality"),
		-1,
		TEXT("3D fog quality override. -1 uses Globals.TDFog3DSettings.DefaultQualityLevel, 0 low, 1 medium, 2 high."),
		ECVF_Default);

#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarTDFog3DGroundForceRecapture(
		TEXT("TD.Fog3D.GroundForceRecapture"),
		0,
		TEXT("Development: 1 recaptures and reconverts the 3D fog ground picture every frame, 0 recaptures only when the window needs it."),
		ECVF_Default);

	constexpr int32 GroundReadbackArgumentCount = 5;

	void RequestGroundRecaptureInWorld(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		for (TActorIterator<ATDFog3DZoneActor> It(World); It; ++It)
		{
			It->RequestGroundRecapture();
		}
	}

	void RequestGroundReadbackInWorld(const TArray<FString>& Arguments, UWorld* World)
	{
		if (!World || Arguments.Num() < GroundReadbackArgumentCount)
		{
			UE_LOG(LogTDFog3DZone, Warning, TEXT("TD.Fog3D.GroundReadback needs <x0> <y0> <x1> <y1> <count> in world cm."));
			return;
		}
		const FVector2D StartWorld(FCString::Atod(*Arguments[0]), FCString::Atod(*Arguments[1]));
		const FVector2D EndWorld(FCString::Atod(*Arguments[2]), FCString::Atod(*Arguments[3]));
		const int32 SampleCount = FCString::Atoi(*Arguments[4]);
		for (TActorIterator<ATDFog3DZoneActor> It(World); It; ++It)
		{
			It->RequestGroundReadback(StartWorld, EndWorld, SampleCount);
		}
	}

	FAutoConsoleCommandWithWorld GroundRecaptureNowCommand(
		TEXT("TD.Fog3D.GroundRecaptureNow"),
		TEXT("Development: recaptures and reconverts the 3D fog ground picture once on the next frame."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RequestGroundRecaptureInWorld));

	FAutoConsoleCommandWithWorldAndArgs GroundReadbackCommand(
		TEXT("TD.Fog3D.GroundReadback"),
		TEXT("Development: TD.Fog3D.GroundReadback <x0> <y0> <x1> <y1> <count> logs the canonical 3D fog ground height at count evenly spaced world points."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RequestGroundReadbackInWorld));
#endif

	constexpr int32 GroundReadbackMaximumSampleCount = 1024;
	constexpr float GroundReadbackInvalidHeightCm = -9999.0f;
	constexpr float HeightFogSearchIntervalSeconds = 1.0f;
	constexpr double MeshExtentEpsilonCm = 1.0e-3;
	constexpr uint8 MissingBladeSocketBit = 1 << 0;
	constexpr uint8 MissingHandBit = 1 << 1;
	constexpr uint8 MissingLeftFootBit = 1 << 2;
	constexpr uint8 MissingRightFootBit = 1 << 3;
	constexpr uint8 MissingPelvisBit = 1 << 4;
	constexpr float GroundCaptureClearDepthCm = 1.0e9f;
	const FRotator GroundCaptureRotation(-90.0, -90.0, 0.0);
	const TCHAR* MainRendererCaptureConsoleName = TEXT("r.SceneCapture.AllowRenderInMainRenderer");

	bool IsMainRendererCaptureAllowed()
	{
		const IConsoleVariable* AllowVariable = IConsoleManager::Get().FindConsoleVariable(MainRendererCaptureConsoleName);
		return !AllowVariable || AllowVariable->GetBool();
	}

	UTextureRenderTarget2D* CreateGroundPlaneTarget(UObject& Outer, int32 Size, const FLinearColor& ClearColor)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(&Outer, NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_R32f;
		Target->ClearColor = ClearColor;
		Target->Filter = TF_Bilinear;
		Target->AddressX = TA_Clamp;
		Target->AddressY = TA_Clamp;
		Target->InitAutoFormat(Size, Size);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	bool ResetGroundTextureToDefault(UMaterialInstanceDynamic& MaterialInstance)
	{
		UTexture* DefaultTexture = nullptr;
		const bool bHasDefault = MaterialInstance.Parent
			&& MaterialInstance.Parent->GetTextureParameterDefaultValue(FHashedMaterialParameterInfo(TEXT("Fog3DGroundHeight")), DefaultTexture)
			&& DefaultTexture;
		if (!bHasDefault)
		{
			return false;
		}
		MaterialInstance.SetTextureParameterValue(TEXT("Fog3DGroundHeight"), DefaultTexture);
		return true;
	}

	int32 ReadConsoleVolumetricFog()
	{
		static const IConsoleVariable* const VolumetricFogVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricFog"));
		return VolumetricFogVariable ? VolumetricFogVariable->GetInt() : 1;
	}

	FTDFog3DHeightFogState ReadHeightFogState(const UExponentialHeightFogComponent* Component)
	{
		FTDFog3DHeightFogState State;
		if (!Component)
		{
			return State;
		}
		State.bIsRegistered = Component->IsRegistered();
		State.bRenders = Component->ShouldRender();
		State.bEnablesVolumetricFog = Component->bEnableVolumetricFog;
		State.FogDensity = Component->FogDensity;
		State.SecondFogDensity = Component->SecondFogData.FogDensity;
		State.FogMaxOpacity = Component->FogMaxOpacity;
		State.VolumetricFogDistance = Component->VolumetricFogDistance;
		return State;
	}

	double ComputeScaleAxis(double HalfExtentCm, double MeshExtentCm)
	{
		if (!FMath::IsFinite(HalfExtentCm) || HalfExtentCm <= 0.0 || !FMath::IsFinite(MeshExtentCm) || MeshExtentCm <= MeshExtentEpsilonCm)
		{
			return 1.0;
		}
		return HalfExtentCm / MeshExtentCm;
	}

	void AppendBodySample(int32 SlotId, const TOptional<FVector>& BoneWorld, float RadiusCm, float WindStrength, float WindGain, float ErosionPerSec, TArray<FTDFog3DInputSample>& InOutSamples)
	{
		if (!BoneWorld.IsSet())
		{
			return;
		}
		FTDFog3DInputSample& Sample = InOutSamples.AddDefaulted_GetRef();
		Sample.SlotId = SlotId;
		Sample.CurrentWorld = BoneWorld.GetValue();
		Sample.RadiusCm = RadiusCm;
		Sample.WindStrength = WindStrength;
		Sample.WindGain = WindGain;
		Sample.ErosionPerSec = ErosionPerSec;
	}

	void AssignOcclusionAnchor(const FTDFog3DInputPose& Pose, TArray<FTDFog3DInputSample>& InOutSamples)
	{
		const TOptional<FVector>& AnchorWorld = Pose.PelvisWorld.IsSet() ? Pose.PelvisWorld : Pose.PawnWorld;
		if (!AnchorWorld.IsSet())
		{
			return;
		}
		for (FTDFog3DInputSample& Sample : InOutSamples)
		{
			Sample.OcclusionAnchorWorld = AnchorWorld.GetValue();
			Sample.bHasOcclusionAnchor = true;
		}
	}

	FLinearColor ToLinearColor(const FVector4f& Value)
	{
		return FLinearColor(Value.X, Value.Y, Value.Z, Value.W);
	}

	void WriteVectorParameter(UMaterialInstanceDynamic& MID, const TCHAR* ParameterName, const FVector4f& Desired, const FVector4f& Written, bool bWritesAll)
	{
		if (!bWritesAll && Desired == Written)
		{
			return;
		}
		MID.SetVectorParameterValue(FName(ParameterName), ToLinearColor(Desired));
	}
}

ATDFog3DZoneActor::ATDFog3DZoneActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	ZoneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ZoneRoot"));
	SetRootComponent(ZoneRoot);

	ZoneVolume = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ZoneVolume"));
	ZoneVolume->SetupAttachment(ZoneRoot);
	ZoneVolume->SetUsingAbsoluteRotation(true);
	ZoneVolume->SetUsingAbsoluteScale(true);
	ZoneVolume->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	ZoneVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneVolume->SetGenerateOverlapEvents(false);
	ZoneVolume->SetCanEverAffectNavigation(false);
	ZoneVolume->SetCastShadow(false);
	ZoneVolume->SetVisibleInRayTracing(false);
	ZoneVolume->SetVisibility(false);
}

void ATDFog3DZoneActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateZoneVolumeScale();
}

void ATDFog3DZoneActor::BeginPlay()
{
	Super::BeginPlay();
	UpdateZoneVolumeScale();
	SetZoneVisible(false);
	ClaimRepresentative();
	if (!bIsRepresentative)
	{
		return;
	}

	SecondsSinceHeightFogSearch = TDFog3DZoneActorPrivate::HeightFogSearchIntervalSeconds;
	FindHeightFog(0.0f);
	RefreshSettings();
	RequestVolumeMaterial();
}

void ATDFog3DZoneActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelVolumeMaterialLoad();
	if (bIsRepresentative)
	{
		LogSummary();
	}
	ReleaseGroundResources();
	GroundSeenRejectedStepCount = 0;
	GroundReadbackWorldPoints.Reset();
	bIsGroundRecaptureRequested = false;
	bIsGroundRestartedBySettings = false;
	RequestedVolumeMaterial.Reset();
	bHasRequestedVolumeMaterial = false;
	Layer.Release();
	VolumeMID = nullptr;
	StateTarget = nullptr;
	HeightFogComponent.Reset();
	LastTickPawn.Reset();
	bIsRepresentative = false;
	bHasReadSettings = false;
	bHasValidSettings = false;
	bHasWrittenMaterialParameters = false;
	SettingsGeneration = INDEX_NONE;
	AppliedMode = INDEX_NONE;
	AppliedQualityLevel = INDEX_NONE;
	ReportedRejectedStepCount = 0;
	Super::EndPlay(EndPlayReason);
}

void ATDFog3DZoneActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bIsRepresentative)
	{
		return;
	}

	LogFinishedGroundReadback();
	if (RefreshSettings())
	{
		RequestVolumeMaterial();
	}
	if (bHasValidSettings && Settings.bEnabled)
	{
		ConfigureLayer(false);
	}
	FindHeightFog(DeltaSeconds);

	APawn* Pawn = FindPlayerPawn();
	ForgetInputHistoryWhenPawnChanges(Pawn);
	const FTDFog3DZoneTickGates Gates = GatherTickGates(Pawn);
	const ETDFog3DZoneTickOutcome Outcome = DecideTickOutcome(Gates);
	SetZoneVisible(Outcome != ETDFog3DZoneTickOutcome::Hidden);
	TrackResetCauses(Gates, Outcome);
	if (Outcome != ETDFog3DZoneTickOutcome::Simulate || !Pawn)
	{
		SetActiveParameter(false);
		GroundTracker.Reset();
		return;
	}
	SimulateStep(*Pawn, DeltaSeconds);
}

bool ATDFog3DZoneActor::IsPointInsideZone(const FVector& PointWorld, const FVector& ZoneCenterWorld, const FVector& InZoneHalfExtentCm, double VerticalMarginCm)
{
	if (PointWorld.ContainsNaN() || ZoneCenterWorld.ContainsNaN() || InZoneHalfExtentCm.ContainsNaN())
	{
		return false;
	}
	const FVector Offset = PointWorld - ZoneCenterWorld;
	const double Margin = FMath::IsFinite(VerticalMarginCm) ? FMath::Max(VerticalMarginCm, 0.0) : 0.0;
	return FMath::Abs(Offset.X) <= InZoneHalfExtentCm.X
		&& FMath::Abs(Offset.Y) <= InZoneHalfExtentCm.Y
		&& FMath::Abs(Offset.Z) <= InZoneHalfExtentCm.Z + Margin;
}

bool ATDFog3DZoneActor::IsVolumetricFogDrawn(const FTDFog3DZoneTickGates& Gates)
{
	return Gates.ConsoleVolumetricFog != 0 && Gates.bHasHeightFog && Gates.bHeightFogEnablesVolumetricFog && Gates.bHeightFogRenders;
}

bool ATDFog3DZoneActor::IsHeightFogDrawingVolumetricFog(const FTDFog3DHeightFogState& State)
{
	return State.bIsRegistered
		&& State.bRenders
		&& State.bEnablesVolumetricFog
		&& (State.FogDensity + State.SecondFogDensity) * 1000.0f > UE_DELTA
		&& State.FogMaxOpacity > UE_DELTA
		&& State.VolumetricFogDistance > 0.0f;
}

ETDFog3DZoneTickOutcome ATDFog3DZoneActor::DecideTickOutcome(const FTDFog3DZoneTickGates& Gates)
{
	if (!Gates.bIsRepresentative || !Gates.bHasValidSettings || !Gates.bIsEnabled || !Gates.bIsMaterialReady)
	{
		return ETDFog3DZoneTickOutcome::Hidden;
	}
	if (!Gates.bHasPawn || !Gates.bIsPawnInsideZone)
	{
		return ETDFog3DZoneTickOutcome::Inactive;
	}
	if (!IsVolumetricFogDrawn(Gates) || Gates.Mode <= 0)
	{
		return ETDFog3DZoneTickOutcome::Inactive;
	}
	return ETDFog3DZoneTickOutcome::Simulate;
}

FVector ATDFog3DZoneActor::ComputeZoneVolumeScale(const FVector& InZoneHalfExtentCm, const FVector& MeshBoxExtentCm)
{
	using namespace TDFog3DZoneActorPrivate;
	return FVector(
		ComputeScaleAxis(InZoneHalfExtentCm.X, MeshBoxExtentCm.X),
		ComputeScaleAxis(InZoneHalfExtentCm.Y, MeshBoxExtentCm.Y),
		ComputeScaleAxis(InZoneHalfExtentCm.Z, MeshBoxExtentCm.Z));
}

void ATDFog3DZoneActor::AppendBladeSamples(
	const FTransform& SocketWorld,
	const FVector& BaseLocalCm,
	const FVector& TipLocalCm,
	float RadiusCm,
	float WindStrength,
	float WindGain,
	float ErosionPerSec,
	TArray<FTDFog3DInputSample>& InOutSamples)
{
	TArray<FVector> Centers;
	FTDFog3DLayer::ComputeBladeSphereCenters(SocketWorld.TransformPosition(BaseLocalCm), SocketWorld.TransformPosition(TipLocalCm), RadiusCm, Centers);
	for (int32 SphereIndex = 0; SphereIndex < Centers.Num(); ++SphereIndex)
	{
		FTDFog3DInputSample& Sample = InOutSamples.AddDefaulted_GetRef();
		Sample.SlotId = SphereIndex;
		Sample.CurrentWorld = Centers[SphereIndex];
		Sample.RadiusCm = RadiusCm;
		Sample.WindStrength = WindStrength;
		Sample.WindGain = WindGain;
		Sample.ErosionPerSec = ErosionPerSec;
	}
}

void ATDFog3DZoneActor::BuildInputSamples(const FTDFog3DInputPose& Pose, const FTDFog3DSettings& InSettings, TArray<FTDFog3DInputSample>& OutSamples)
{
	using namespace TDFog3DZoneActorPrivate;
	OutSamples.Reset();
	if (Pose.BladeSocketWorld.IsSet())
	{
		AppendBladeSamples(Pose.BladeSocketWorld.GetValue(), InSettings.BladeBaseLocalCm, InSettings.BladeTipLocalCm, InSettings.BladeRadiusCm, InSettings.BladeWindStrength, InSettings.BladeWindGain, InSettings.BladeErosionPerSec, OutSamples);
	}
	AppendBodySample(HandSlotId, Pose.HandWorld, InSettings.HandRadiusCm, InSettings.BodyWindStrength, InSettings.BodyWindGain, InSettings.BodyErosionPerSec, OutSamples);
	AppendBodySample(LeftFootSlotId, Pose.LeftFootWorld, InSettings.FootRadiusCm, InSettings.BodyWindStrength, InSettings.BodyWindGain, InSettings.BodyErosionPerSec, OutSamples);
	AppendBodySample(RightFootSlotId, Pose.RightFootWorld, InSettings.FootRadiusCm, InSettings.BodyWindStrength, InSettings.BodyWindGain, InSettings.BodyErosionPerSec, OutSamples);
	AppendBodySample(PelvisSlotId, Pose.PelvisWorld, InSettings.PelvisRadiusCm, InSettings.BodyWindStrength, InSettings.BodyWindGain, InSettings.BodyErosionPerSec, OutSamples);
	AssignOcclusionAnchor(Pose, OutSamples);
}

FString ATDFog3DZoneActor::MakeSettingsLogText(const FTDFog3DSettings& InSettings, int32 QualityLevel, int32 Resolution, float StepsPerSecond)
{
	return FString::Printf(TEXT("Fog3D settings: quality=%d resolution=%d stepsPerSecond=%g clampedTimeWindBlend=%d bodyWindGain=%g bladeWindGain=%g followsGround=%d occludesInput=%d"),
		QualityLevel, Resolution, StepsPerSecond, InSettings.bUsesClampedTimeWindBlend ? 1 : 0, InSettings.BodyWindGain, InSettings.BladeWindGain, InSettings.bFollowsGround ? 1 : 0,
		InSettings.bOccludesInputBehindObstacles ? 1 : 0);
}

void ATDFog3DZoneActor::ComputeConstantMaterialParameters(const FTDFog3DSettings& InSettings, const FVector& InZoneHalfExtentCm, int32 WindowCells, FTDFog3DMaterialParameters& InOutParameters)
{
	InOutParameters.Window.Z = static_cast<float>(-InZoneHalfExtentCm.Z);
	InOutParameters.Window.W = static_cast<float>(WindowCells);
	const int32 EdgeBandCells = FMath::Clamp(InSettings.EdgeBandCells, 0, TDFluidGpuFog3DMaximumEdgeBandCells);
	InOutParameters.Grid = FVector4f(InSettings.CellSizeCm, InSettings.LayerThicknessCm, static_cast<float>(TDFluidGpuFog3DLayerCount), static_cast<float>(EdgeBandCells));
	InOutParameters.BaseShape = FVector4f(InSettings.BaseDensity, InSettings.BaseThicknessCm, InSettings.BaseTopBlurCm, 0.0f);
	InOutParameters.Zone = FVector4f(static_cast<float>(InZoneHalfExtentCm.X), static_cast<float>(InZoneHalfExtentCm.Y), static_cast<float>(InZoneHalfExtentCm.Z), InSettings.ZoneEdgeFadeCm);
	InOutParameters.FineNoise = FVector4f(InSettings.bFineNoiseEnabled ? InSettings.FineNoiseAmplitude : 0.0f, InSettings.FineNoiseSpacingCm, static_cast<float>(InSettings.FineNoiseSeed), 0.0f);
	InOutParameters.StainNoise = FVector4f(InSettings.bStainNoiseEnabled ? InSettings.StainNoiseAmplitude : 0.0f, InSettings.StainNoiseSpacingCm, static_cast<float>(InSettings.StainNoiseSeed), 0.0f);
}

TSoftObjectPtr<UMaterialInterface> ATDFog3DZoneActor::ChooseVolumeMaterial(const FTDFog3DSettings& InSettings, const TSoftObjectPtr<UMaterialInterface>& ActorVolumeMaterial)
{
	if (InSettings.bFollowsGround && !InSettings.GroundFollowVolumeMaterial.IsNull())
	{
		return InSettings.GroundFollowVolumeMaterial;
	}
	return ActorVolumeMaterial;
}

void ATDFog3DZoneActor::ClaimRepresentative()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ATDFog3DZoneActor> It(World); It; ++It)
	{
		const ATDFog3DZoneActor* Other = *It;
		if (Other == this || !Other->bIsRepresentative)
		{
			continue;
		}
		if (!bHasWarnedDuplicate)
		{
			bHasWarnedDuplicate = true;
			UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D zone %s rests because %s already simulates this world; only one 3D fog zone is supported."), *GetName(), *Other->GetName());
		}
		return;
	}
	bIsRepresentative = true;
}

void ATDFog3DZoneActor::UpdateZoneVolumeScale()
{
	const UStaticMesh* Mesh = ZoneVolume ? ZoneVolume->GetStaticMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}
	ZoneVolume->SetRelativeScale3D(ComputeZoneVolumeScale(ZoneHalfExtentCm, Mesh->GetBounds().BoxExtent));
}

bool ATDFog3DZoneActor::RefreshSettings()
{
	const UTDFluidPresetTuningSubsystem* Tuning = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	const int32 Generation = Tuning ? Tuning->GetTableGeneration() : INDEX_NONE;
	if (bHasReadSettings && Generation == SettingsGeneration)
	{
		return false;
	}

	bHasReadSettings = true;
	SettingsGeneration = Generation;
	const FTDFog3DSettings PreviousSettings = Settings;
	Settings = FTDFog3DSettings();
	if (Tuning)
	{
		UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFog3DSettings::StaticStruct(), &Settings);
	}

	TArray<FString> Problems;
	CollectTDFog3DSettingsProblems(Settings, Problems);
	CollectTDFog3DZoneProblems(ZoneHalfExtentCm, Settings, Problems);
	bHasValidSettings = Problems.IsEmpty();
	if (!bHasValidSettings)
	{
		UE_LOG(LogTDFog3DZone, Error, TEXT("Fog3D zone %s rests until the tuning table changes: %s"), *GetName(), *FString::Join(Problems, TEXT("; ")));
		SetActiveParameter(false);
		ReleaseGroundResources();
		return true;
	}
	if (!Settings.bEnabled)
	{
		SetActiveParameter(false);
		ReleaseGroundResources();
		return true;
	}

	ConfigureLayer(true);
	RestartGroundWhenSettingsChange(PreviousSettings);
	return true;
}

void ATDFog3DZoneActor::ConfigureLayer(bool bForceConfigure)
{
	using namespace TDFog3DZoneActorPrivate;
	const int32 Mode = FTDFog3DLayer::ResolveMode(CVarTDFog3DMode.GetValueOnGameThread(), Settings.Mode);
	const int32 QualityLevel = FTDFog3DLayer::ResolveQualityLevel(CVarTDFog3DQuality.GetValueOnGameThread(), Settings.DefaultQualityLevel);
	if (!bForceConfigure && Mode == AppliedMode && QualityLevel == AppliedQualityLevel)
	{
		return;
	}
	AppliedMode = Mode;
	AppliedQualityLevel = QualityLevel;
	Layer.Configure(Settings, QualityLevel, Mode);
	UE_LOG(LogTDFog3DZone, Log, TEXT("%s"), *MakeSettingsLogText(Settings, QualityLevel, Layer.GetResolution(), Layer.GetStepsPerSecond()));
	EnsureStateTarget();
	ComputeConstantMaterialParameters(Settings, ZoneHalfExtentCm, Layer.GetResolution(), DesiredMaterialParameters);
	WriteMaterialParameters();
	UpdateGroundResources();
}

void ATDFog3DZoneActor::EnsureStateTarget()
{
	const int32 Resolution = Layer.GetResolution();
	if (Resolution <= 0)
	{
		return;
	}
	const bool bHasMatchingTarget = StateTarget && StateTarget->SizeX == Resolution && StateTarget->SizeY == Resolution && StateTarget->SizeZ == TDFluidGpuFog3DLayerCount;
	if (bHasMatchingTarget)
	{
		return;
	}

	UTextureRenderTargetVolume* NewTarget = NewObject<UTextureRenderTargetVolume>(this, NAME_None, RF_Transient);
	NewTarget->bSupportsUAV = true;
	NewTarget->Filter = TF_Bilinear;
	NewTarget->ClearColor = FLinearColor::Transparent;
	NewTarget->Init(Resolution, Resolution, TDFluidGpuFog3DLayerCount, PF_FloatRGBA);
	NewTarget->UpdateResourceImmediate(true);
	StateTarget = NewTarget;
	SetActiveParameter(false);
	if (VolumeMID)
	{
		VolumeMID->SetTextureParameterValue(TEXT("Fog3DStateVolume"), StateTarget);
	}
}

void ATDFog3DZoneActor::RequestVolumeMaterial()
{
	const TSoftObjectPtr<UMaterialInterface> ChosenMaterial = ChooseVolumeMaterial(Settings, VolumeMaterial);
	if (bHasRequestedVolumeMaterial && ChosenMaterial == RequestedVolumeMaterial)
	{
		return;
	}
	bHasRequestedVolumeMaterial = true;
	RequestedVolumeMaterial = ChosenMaterial;
	CancelVolumeMaterialLoad();
	if (ChosenMaterial.IsNull())
	{
		if (!bHasWarnedMissingMaterial)
		{
			bHasWarnedMissingMaterial = true;
			UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D zone %s has no VolumeMaterial and stays hidden."), *GetName());
		}
		return;
	}
	if (UMaterialInterface* LoadedMaterial = ChosenMaterial.Get())
	{
		ApplyVolumeMaterial(*LoadedMaterial);
		return;
	}
	VolumeMaterialLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		ChosenMaterial.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &ATDFog3DZoneActor::HandleVolumeMaterialLoaded));
}

void ATDFog3DZoneActor::HandleVolumeMaterialLoaded()
{
	VolumeMaterialLoadHandle.Reset();
	UMaterialInterface* LoadedMaterial = RequestedVolumeMaterial.Get();
	if (!LoadedMaterial)
	{
		if (!bHasWarnedMissingMaterial)
		{
			bHasWarnedMissingMaterial = true;
			UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D zone %s could not load VolumeMaterial %s and stays hidden."), *GetName(), *RequestedVolumeMaterial.ToString());
		}
		return;
	}
	ApplyVolumeMaterial(*LoadedMaterial);
}

void ATDFog3DZoneActor::CancelVolumeMaterialLoad()
{
	if (!VolumeMaterialLoadHandle.IsValid())
	{
		return;
	}
	VolumeMaterialLoadHandle->CancelHandle();
	VolumeMaterialLoadHandle.Reset();
}

void ATDFog3DZoneActor::ApplyVolumeMaterial(UMaterialInterface& Material)
{
	if (!bIsRepresentative || !ZoneVolume)
	{
		return;
	}
	const UMaterial* BaseMaterial = Material.GetMaterial();
	if (!BaseMaterial || BaseMaterial->MaterialDomain != MD_Volume)
	{
		if (!bHasWarnedMaterialDomain)
		{
			bHasWarnedMaterialDomain = true;
			UE_LOG(LogTDFog3DZone, Error, TEXT("Fog3D zone %s needs a Volume domain material but %s is not one; the zone stays hidden."), *GetName(), *Material.GetPathName());
		}
		return;
	}

	VolumeMID = UMaterialInstanceDynamic::Create(&Material, this);
	if (!VolumeMID)
	{
		return;
	}
	if (StateTarget)
	{
		VolumeMID->SetTextureParameterValue(TEXT("Fog3DStateVolume"), StateTarget);
	}
	ApplyGroundTextureParameter();
	ZoneVolume->SetMaterial(0, VolumeMID);
	bHasWrittenMaterialParameters = false;
	WriteMaterialParameters();
}

void ATDFog3DZoneActor::SetZoneVisible(bool bIsVisible)
{
	if (!ZoneVolume || ZoneVolume->IsVisible() == bIsVisible)
	{
		return;
	}
	ZoneVolume->SetVisibility(bIsVisible);
}

void ATDFog3DZoneActor::SetActiveParameter(bool bIsActive)
{
	DesiredMaterialParameters.Active = bIsActive ? 1.0f : 0.0f;
	WriteMaterialParameters();
}

void ATDFog3DZoneActor::WriteMaterialParameters()
{
	using namespace TDFog3DZoneActorPrivate;
	if (!VolumeMID)
	{
		return;
	}
	const bool bWritesAll = !bHasWrittenMaterialParameters;
	const FTDFog3DMaterialParameters& Desired = DesiredMaterialParameters;
	const FTDFog3DMaterialParameters& Written = WrittenMaterialParameters;
	WriteVectorParameter(*VolumeMID, TEXT("Fog3DWindow"), Desired.Window, Written.Window, bWritesAll);
	WriteVectorParameter(*VolumeMID, TEXT("Fog3DGrid"), Desired.Grid, Written.Grid, bWritesAll);
	WriteVectorParameter(*VolumeMID, TEXT("Fog3DBaseShape"), Desired.BaseShape, Written.BaseShape, bWritesAll);
	WriteVectorParameter(*VolumeMID, TEXT("Fog3DZone"), Desired.Zone, Written.Zone, bWritesAll);
	WriteVectorParameter(*VolumeMID, TEXT("Fog3DFineNoise"), Desired.FineNoise, Written.FineNoise, bWritesAll);
	WriteVectorParameter(*VolumeMID, TEXT("Fog3DStainNoise"), Desired.StainNoise, Written.StainNoise, bWritesAll);
	WriteVectorParameter(*VolumeMID, TEXT("Fog3DGround"), Desired.Ground, Written.Ground, bWritesAll);
	if (bWritesAll || Desired.Active != Written.Active)
	{
		VolumeMID->SetScalarParameterValue(TEXT("Fog3DActive"), Desired.Active);
	}
	WrittenMaterialParameters = Desired;
	bHasWrittenMaterialParameters = true;
}

FTDFog3DZoneTickGates ATDFog3DZoneActor::GatherTickGates(const APawn* Pawn)
{
	FTDFog3DZoneTickGates Gates;
	Gates.bIsRepresentative = bIsRepresentative;
	Gates.bHasValidSettings = bHasValidSettings;
	Gates.bIsEnabled = Settings.bEnabled;
	Gates.bIsMaterialReady = VolumeMID != nullptr && StateTarget != nullptr;
	Gates.bHasPawn = Pawn != nullptr;
	Gates.bIsPawnInsideZone = Pawn && IsPointInsideZone(Pawn->GetActorLocation(), GetZoneReferenceWorld(), ZoneHalfExtentCm, Settings.ZoneEntryVerticalMarginCm);
	Gates.ConsoleVolumetricFog = TDFog3DZoneActorPrivate::ReadConsoleVolumetricFog();
	const UExponentialHeightFogComponent* HeldHeightFog = HeightFogComponent.Get();
	const FTDFog3DHeightFogState HeightFogState = TDFog3DZoneActorPrivate::ReadHeightFogState(HeldHeightFog);
	Gates.bHasHeightFog = HeldHeightFog != nullptr;
	Gates.bHeightFogEnablesVolumetricFog = HeightFogState.bEnablesVolumetricFog;
	Gates.bHeightFogRenders = IsHeightFogDrawingVolumetricFog(HeightFogState);
	Gates.Mode = AppliedMode;
	return Gates;
}

void ATDFog3DZoneActor::TrackResetCauses(const FTDFog3DZoneTickGates& Gates, ETDFog3DZoneTickOutcome Outcome)
{
	const bool bIsPawnOutsideZone = Gates.bHasPawn && !Gates.bIsPawnInsideZone;
	if (bIsPawnOutsideZone || Outcome == ETDFog3DZoneTickOutcome::Hidden)
	{
		bWasOutsideZone = true;
	}
	if (!IsVolumetricFogDrawn(Gates))
	{
		bWasVolumetricFogOff = true;
	}
	if (Outcome != ETDFog3DZoneTickOutcome::Simulate)
	{
		return;
	}
	if (bWasOutsideZone)
	{
		bWasOutsideZone = false;
		Layer.RequestReset(ETDFog3DResetCause::ZoneReentered);
	}
	if (bWasVolumetricFogOff)
	{
		bWasVolumetricFogOff = false;
		Layer.RequestReset(ETDFog3DResetCause::VolumetricFogReenabled);
	}
}

void ATDFog3DZoneActor::FindHeightFog(float DeltaSeconds)
{
	using namespace TDFog3DZoneActorPrivate;
	SecondsSinceHeightFogSearch = FMath::Min(SecondsSinceHeightFogSearch + FMath::Max(DeltaSeconds, 0.0f), HeightFogSearchIntervalSeconds);
	if (IsHeightFogDrawingVolumetricFog(ReadHeightFogState(HeightFogComponent.Get())))
	{
		return;
	}
	if (SecondsSinceHeightFogSearch < HeightFogSearchIntervalSeconds)
	{
		return;
	}
	SecondsSinceHeightFogSearch = 0.0f;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		UExponentialHeightFogComponent* Candidate = It->GetComponent();
		if (IsHeightFogDrawingVolumetricFog(ReadHeightFogState(Candidate)))
		{
			HeightFogComponent = Candidate;
			return;
		}
	}
}

void ATDFog3DZoneActor::ForgetInputHistoryWhenPawnChanges(APawn* Pawn)
{
	const bool bPawnChanged = Pawn != nullptr && Pawn != LastTickPawn.Get();
	LastTickPawn = Pawn;
	if (bPawnChanged)
	{
		Layer.ForgetInputHistory();
	}
}

void ATDFog3DZoneActor::LogSummary()
{
	FlushRenderingCommands();
	RejectedStepTotal += FMath::Max(Layer.GetSeenRejectedStepCount(), Layer.ReadRejectedStepCount());
	UE_LOG(LogTDFog3DZone, Display, TEXT("Fog3D zone summary: submitted=%u rejected=%d deferred=%d"), SubmittedStepTotal, RejectedStepTotal, DeferredGroundStepTotal);
}

APawn* ATDFog3DZoneActor::FindPlayerPawn() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return PlayerController ? PlayerController->GetPawn() : nullptr;
}

USkeletalMeshComponent* ATDFog3DZoneActor::FindPawnMesh(const APawn& Pawn) const
{
	if (const ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(&Pawn))
	{
		if (USkeletalMeshComponent* CombatMesh = CombatCharacter->GetMesh())
		{
			return CombatMesh;
		}
	}
	if (const ACharacter* Character = Cast<ACharacter>(&Pawn))
	{
		if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
		{
			return CharacterMesh;
		}
	}
	return Pawn.FindComponentByClass<USkeletalMeshComponent>();
}

void ATDFog3DZoneActor::ReadInputPose(const APawn& Pawn, FTDFog3DInputPose& OutPose)
{
	using namespace TDFog3DZoneActorPrivate;
	OutPose.PawnWorld = Pawn.GetActorLocation();
	const USkeletalMeshComponent* Mesh = FindPawnMesh(Pawn);
	if (!Mesh)
	{
		if (!bHasWarnedMissingMesh)
		{
			bHasWarnedMissingMesh = true;
			UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D zone %s found no skeletal mesh on pawn %s; body and blade inputs are skipped."), *GetName(), *Pawn.GetName());
		}
		return;
	}

	if (!Settings.BladeSocketName.IsNone())
	{
		if (Mesh->DoesSocketExist(Settings.BladeSocketName))
		{
			OutPose.BladeSocketWorld = Mesh->GetSocketTransform(Settings.BladeSocketName, RTS_World);
		}
		else
		{
			WarnMissingInput(MissingBladeSocketBit, Settings.BladeSocketName);
		}
	}
	ReadBoneLocation(*Mesh, Settings.HandBoneName, MissingHandBit, OutPose.HandWorld);
	ReadBoneLocation(*Mesh, Settings.LeftFootBoneName, MissingLeftFootBit, OutPose.LeftFootWorld);
	ReadBoneLocation(*Mesh, Settings.RightFootBoneName, MissingRightFootBit, OutPose.RightFootWorld);
	ReadBoneLocation(*Mesh, Settings.PelvisBoneName, MissingPelvisBit, OutPose.PelvisWorld);
}

void ATDFog3DZoneActor::ReadBoneLocation(const USkeletalMeshComponent& Mesh, FName BoneName, uint8 MissingInputBit, TOptional<FVector>& OutLocation)
{
	if (BoneName.IsNone())
	{
		return;
	}
	if (!Mesh.DoesSocketExist(BoneName))
	{
		WarnMissingInput(MissingInputBit, BoneName);
		return;
	}
	OutLocation = Mesh.GetSocketLocation(BoneName);
}

void ATDFog3DZoneActor::WarnMissingInput(uint8 MissingInputBit, FName InputName)
{
	if ((WarnedMissingInputMask & MissingInputBit) != 0)
	{
		return;
	}
	WarnedMissingInputMask |= MissingInputBit;
	UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D zone %s skips input %s because the player mesh has no such socket or bone."), *GetName(), *InputName.ToString());
}

void ATDFog3DZoneActor::SimulateStep(const APawn& Pawn, float DeltaSeconds)
{
	const FTDFog3DClockDecision Clock = Layer.AdvanceClock(DeltaSeconds);
	const FVector PlayerWorld = Pawn.GetActorLocation();
	const FTDFog3DGroundDecision GroundDecision = TrackGround(PlayerWorld);
	if (!Clock.bStepsNow || !StateTarget)
	{
		return;
	}
	if (!GroundDecision.bAllowsStep)
	{
		CountDeferredGroundStep(PlayerWorld);
		return;
	}

	FTDFog3DStepRequest Request;
	Request.PlayerWorld = PlayerWorld;
	Request.FloorZ = GetZoneFloorZ();
	Request.Clock = Clock;
	Request.Ground = MakeGroundStep(GroundDecision);
	FTDFog3DInputPose Pose;
	ReadInputPose(Pawn, Pose);
	BuildInputSamples(Pose, Settings, Request.Inputs);
	if (const UWorld* World = GetWorld())
	{
		if (const UTDFluidLiveSubsystem* LiveSubsystem = World->GetSubsystem<UTDFluidLiveSubsystem>())
		{
			LiveSubsystem->CollectRegisteredObstacles(Request.RegisteredObstacles);
		}
	}

	if (!Layer.SubmitStep(Request, *StateTarget))
	{
		if (!bHasWarnedSubmitRefused)
		{
			bHasWarnedSubmitRefused = true;
			UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D zone %s could not submit a step (resolution %d, target %dx%dx%d)."), *GetName(), Layer.GetResolution(), StateTarget->SizeX, StateTarget->SizeY, StateTarget->SizeZ);
		}
		return;
	}

	++SubmittedStepTotal;
	ConfirmGroundStep(GroundDecision);
	WarnAfterSubmit();
	DesiredMaterialParameters.Window = FTDFog3DLayer::ComputeMaterialWindow(Layer.GetSubmittedWindowMinCell(), Settings.CellSizeCm, GetZoneReferenceWorld(), GetZoneFloorZ(), Layer.GetResolution());
	DesiredMaterialParameters.Active = 1.0f;
	WriteMaterialParameters();
}

void ATDFog3DZoneActor::WarnAfterSubmit()
{
	if (Layer.GetLastDroppedSphereCount() > 0 && !bHasWarnedDroppedSpheres)
	{
		bHasWarnedDroppedSpheres = true;
		UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D zone %s dropped %d input spheres beyond the limit of %d."), *GetName(), Layer.GetLastDroppedSphereCount(), Settings.MaximumInputSphereCount);
	}
	if (Layer.GetLastSkippedObstacleCount() > 0 && !bHasWarnedSkippedObstacles)
	{
		bHasWarnedSkippedObstacles = true;
		UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D zone %s skipped %d obstacles that are not upright boxes."), *GetName(), Layer.GetLastSkippedObstacleCount());
	}
	const int32 RejectedStepCount = Layer.GetSeenRejectedStepCount();
	if (RejectedStepCount > ReportedRejectedStepCount)
	{
		ReportedRejectedStepCount = RejectedStepCount;
		UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D zone %s saw %d rejected GPU steps in total; the layer restarted the state."), *GetName(), RejectedStepCount);
	}
}

FVector ATDFog3DZoneActor::GetZoneReferenceWorld() const
{
	return ZoneVolume ? ZoneVolume->Bounds.Origin : GetActorLocation();
}

double ATDFog3DZoneActor::GetZoneFloorZ() const
{
	return GetZoneReferenceWorld().Z - ZoneHalfExtentCm.Z;
}

bool ATDFog3DZoneActor::ShouldFollowGround() const
{
	return bIsRepresentative && bHasValidSettings && Settings.bEnabled && Settings.bFollowsGround;
}

bool ATDFog3DZoneActor::HasGroundResources() const
{
	return GroundCapture && GroundCaptureTarget && GroundMaterialTarget;
}

void ATDFog3DZoneActor::UpdateGroundResources()
{
	const int32 CaptureSize = FTDFog3DGroundLayout::ComputeCaptureSize(Layer.GetResolution(), Settings.GroundCaptureBorderCells);
	if (!ShouldFollowGround() || CaptureSize <= 0 || !GetWorld())
	{
		ReleaseGroundResources();
		return;
	}
	EnsureGroundCapture();
	EnsureGroundTargets(CaptureSize);
}

void ATDFog3DZoneActor::EnsureGroundCapture()
{
	if (GroundCapture)
	{
		return;
	}
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(this, NAME_None, RF_Transient);
	Capture->SetUsingAbsoluteLocation(true);
	Capture->SetUsingAbsoluteRotation(true);
	Capture->SetUsingAbsoluteScale(true);
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->TextureTarget = GroundCaptureTarget;
	Capture->SetupAttachment(ZoneRoot);
	Capture->RegisterComponent();
	GroundCapture = Capture;
	GroundTracker.Reset();
}

void ATDFog3DZoneActor::EnsureGroundTargets(int32 CaptureSize)
{
	using namespace TDFog3DZoneActorPrivate;
	const bool bHasMatchingTargets = GroundCaptureTarget && GroundMaterialTarget
		&& GroundCaptureTarget->SizeX == CaptureSize && GroundCaptureTarget->SizeY == CaptureSize
		&& GroundMaterialTarget->SizeX == CaptureSize && GroundMaterialTarget->SizeY == CaptureSize;
	if (bHasMatchingTargets)
	{
		return;
	}
	GroundCaptureTarget = CreateGroundPlaneTarget(*this, CaptureSize, FLinearColor(GroundCaptureClearDepthCm, 0.0f, 0.0f, 0.0f));
	GroundMaterialTarget = CreateGroundPlaneTarget(*this, CaptureSize, FLinearColor::Transparent);
	if (GroundCapture)
	{
		GroundCapture->TextureTarget = GroundCaptureTarget;
	}
	GroundTracker.Reset();
	ApplyGroundTextureParameter();
}

void ATDFog3DZoneActor::ReleaseGroundResources()
{
	if (GroundCapture)
	{
		GroundCapture->DestroyComponent();
		GroundCapture = nullptr;
	}
	GroundCaptureTarget = nullptr;
	const bool bDropsMaterialTexture = VolumeMID && GroundMaterialTarget;
	GroundMaterialTarget = nullptr;
	GroundTracker.Reset();
	DesiredMaterialParameters.Ground = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	if (!bDropsMaterialTexture || TDFog3DZoneActorPrivate::ResetGroundTextureToDefault(*VolumeMID))
	{
		return;
	}
	VolumeMID->ClearParameterValues();
	if (StateTarget)
	{
		VolumeMID->SetTextureParameterValue(TEXT("Fog3DStateVolume"), StateTarget);
	}
	bHasWrittenMaterialParameters = false;
	WriteMaterialParameters();
}

void ATDFog3DZoneActor::ApplyGroundTextureParameter()
{
	if (!VolumeMID || !GroundMaterialTarget)
	{
		return;
	}
	VolumeMID->SetTextureParameterValue(TEXT("Fog3DGroundHeight"), GroundMaterialTarget);
}

void ATDFog3DZoneActor::RestartGroundAfterRejection()
{
	const int32 RejectedStepCount = Layer.ReadRejectedStepCount();
	if (RejectedStepCount <= GroundSeenRejectedStepCount)
	{
		return;
	}
	GroundSeenRejectedStepCount = RejectedStepCount;
	GroundTracker.Reset();
}

void ATDFog3DZoneActor::RestartGroundWhenSettingsChange(const FTDFog3DSettings& PreviousSettings)
{
	if (!HasGroundResources() || !HaveGroundSettingsChanged(PreviousSettings, Settings))
	{
		return;
	}
	GroundTracker.Reset();
	bIsGroundRestartedBySettings = true;
}

bool ATDFog3DZoneActor::HaveGroundSettingsChanged(const FTDFog3DSettings& PreviousSettings, const FTDFog3DSettings& CurrentSettings)
{
	return PreviousSettings.bFollowsGround != CurrentSettings.bFollowsGround
		|| PreviousSettings.GroundWallStepCm != CurrentSettings.GroundWallStepCm
		|| PreviousSettings.GroundCaptureBorderCells != CurrentSettings.GroundCaptureBorderCells
		|| PreviousSettings.GroundCaptureHeightAboveZoneCm != CurrentSettings.GroundCaptureHeightAboveZoneCm
		|| PreviousSettings.GroundCaptureLatencyFrames != CurrentSettings.GroundCaptureLatencyFrames
		|| PreviousSettings.GroundMissingHeightCm != CurrentSettings.GroundMissingHeightCm
		|| PreviousSettings.GroundMissingBelowFloorCm != CurrentSettings.GroundMissingBelowFloorCm;
}

bool ATDFog3DZoneActor::IsGroundCaptureDrawn(bool bIsDrawnByMainRenderer, uint32 SceneFrameNumber, uint32 LastSeenSceneFrameNumber)
{
	return !bIsDrawnByMainRenderer || SceneFrameNumber != LastSeenSceneFrameNumber;
}

bool ATDFog3DZoneActor::ReadGroundSceneDrawSignal()
{
	using namespace TDFog3DZoneActorPrivate;
	const UWorld* World = GetWorld();
	const uint32 SceneFrameNumber = World && World->Scene ? World->Scene->GetFrameNumber() : 0;
	const bool bIsDrawnByMainRenderer = GroundCapture && GroundCapture->ShouldRenderInMainRenderer() && IsMainRendererCaptureAllowed();
	const bool bWasDrawn = IsGroundCaptureDrawn(bIsDrawnByMainRenderer, SceneFrameNumber, LastGroundSceneFrameNumber);
	LastGroundSceneFrameNumber = SceneFrameNumber;
	return bWasDrawn;
}

FTDFog3DGroundDecision ATDFog3DZoneActor::TrackGround(const FVector& PlayerWorld)
{
	if (!HasGroundResources())
	{
		FTDFog3DGroundDecision StageOneDecision;
		StageOneDecision.bAllowsStep = true;
		return StageOneDecision;
	}

	RestartGroundAfterRejection();
	const int32 Resolution = Layer.GetResolution();
	const FIntPoint NextWindowMinCell = Layer.PreviewNextWindowMinCell(PlayerWorld);
	const FTDFog3DGroundRect DesiredRect = FTDFog3DGroundLayout::ComputeCaptureRect(NextWindowMinCell, Resolution, Settings.GroundCaptureBorderCells);
	const bool bWasSceneDrawn = ReadGroundSceneDrawSignal();
	const bool bIsForced = ConsumeForcedGroundRecapture();
	if (bIsForced)
	{
		GroundTracker.RequestReconversion();
	}
	const ETDFog3DGroundTrackerState PreviousState = GroundTracker.GetState();
	const FTDFog3DGroundDecision Decision = GroundTracker.Advance(DesiredRect, NextWindowMinCell, Resolution, Settings.GroundCaptureLatencyFrames, GFrameCounter, bWasSceneDrawn);
	if (Decision.bRequestsCapture)
	{
		RequestGroundCapture(NextWindowMinCell);
		LogGroundRecapture(TakeGroundRecaptureReason(PreviousState), NextWindowMinCell);
		return Decision;
	}
	if (bIsForced && PreviousState != ETDFog3DGroundTrackerState::NoImage)
	{
		RequestGroundCapture(NextWindowMinCell);
		LogGroundRecapture(TEXT("forced"), NextWindowMinCell);
	}
	return Decision;
}

void ATDFog3DZoneActor::RequestGroundRecapture()
{
	bIsGroundRecaptureRequested = true;
}

bool ATDFog3DZoneActor::ConsumeForcedGroundRecapture()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const bool bIsForced = bIsGroundRecaptureRequested || TDFog3DZoneActorPrivate::CVarTDFog3DGroundForceRecapture.GetValueOnGameThread() != 0;
	bIsGroundRecaptureRequested = false;
	return bIsForced;
#endif
}

const TCHAR* ATDFog3DZoneActor::TakeGroundRecaptureReason(ETDFog3DGroundTrackerState PreviousState)
{
	if (PreviousState != ETDFog3DGroundTrackerState::NoImage)
	{
		return TEXT("window");
	}
	const TCHAR* Reason = bIsGroundRestartedBySettings ? TEXT("settings") : TEXT("enter");
	bIsGroundRestartedBySettings = false;
	return Reason;
}

void ATDFog3DZoneActor::LogGroundRecapture(const TCHAR* Reason, const FIntPoint& NextWindowMinCell) const
{
	UE_LOG(LogTDFog3DZone, Verbose, TEXT("Fog3D ground recapture: frame=%llu reason=%s windowMinCell=(%d,%d)"), GFrameCounter, Reason, NextWindowMinCell.X, NextWindowMinCell.Y);
}

void ATDFog3DZoneActor::CountDeferredGroundStep(const FVector& PlayerWorld)
{
	++DeferredGroundStepTotal;
	UE_LOG(LogTDFog3DZone, Verbose, TEXT("Fog3D ground deferred: frame=%llu steps=1 total=%d reason=%s"), GFrameCounter, DeferredGroundStepTotal, ChooseGroundDeferralReason(PlayerWorld));
}

const TCHAR* ATDFog3DZoneActor::ChooseGroundDeferralReason(const FVector& PlayerWorld) const
{
	if (!GroundTracker.GetConvertedRect().IsValid())
	{
		return TEXT("entering");
	}
	const FIntPoint NextWindowMinCell = Layer.PreviewNextWindowMinCell(PlayerWorld);
	if (Layer.HasSubmittedWindow() && FTDFog3DLayer::IsWindowJumpTooFar(Layer.GetSubmittedWindowMinCell(), NextWindowMinCell, Layer.GetResolution()))
	{
		return TEXT("teleport");
	}
	return TEXT("waiting_capture");
}

void ATDFog3DZoneActor::RequestGroundReadback(const FVector2D& StartWorld, const FVector2D& EndWorld, int32 SampleCount)
{
	using namespace TDFog3DZoneActorPrivate;
	if (!bIsRepresentative)
	{
		return;
	}
	if (SampleCount <= 0 || SampleCount > GroundReadbackMaximumSampleCount || StartWorld.ContainsNaN() || EndWorld.ContainsNaN() || !(Settings.CellSizeCm > 0.0f))
	{
		UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D ground readback refused: count %d must be 1 to %d with finite points and a positive cell size."), SampleCount, GroundReadbackMaximumSampleCount);
		return;
	}

	TArray<FVector2D> WorldPoints;
	TArray<FIntPoint> Cells;
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const double Ratio = SampleCount == 1 ? 0.0 : static_cast<double>(SampleIndex) / static_cast<double>(SampleCount - 1);
		const FVector2D WorldPoint = StartWorld + (EndWorld - StartWorld) * Ratio;
		WorldPoints.Add(WorldPoint);
		Cells.Add(FTDFog3DLayer::ComputeWorldCell(WorldPoint, Settings.CellSizeCm));
	}
	if (!Layer.RequestGroundReadback(Cells, Settings.GroundMissingHeightCm))
	{
		UE_LOG(LogTDFog3DZone, Warning, TEXT("Fog3D ground readback refused: the fog has not stepped yet or an earlier readback is still pending."));
		return;
	}
	GroundReadbackWorldPoints = MoveTemp(WorldPoints);
}

void ATDFog3DZoneActor::LogFinishedGroundReadback()
{
	using namespace TDFog3DZoneActorPrivate;
	TArray<FTDFog3DGroundReadbackSample> Samples;
	if (!Layer.TakeGroundReadback(Samples))
	{
		return;
	}
	const int32 SampleCount = FMath::Min(Samples.Num(), GroundReadbackWorldPoints.Num());
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const FTDFog3DGroundReadbackSample& Sample = Samples[SampleIndex];
		const FVector2D& WorldPoint = GroundReadbackWorldPoints[SampleIndex];
		UE_LOG(LogTDFog3DZone, Log, TEXT("Fog3D ground readback: frame=%llu index=%d world=(%.1f,%.1f) height_cm=%.2f valid=%d"),
			GFrameCounter, SampleIndex, WorldPoint.X, WorldPoint.Y, Sample.bIsValid ? Sample.HeightCm : GroundReadbackInvalidHeightCm, Sample.bIsValid ? 1 : 0);
	}
	GroundReadbackWorldPoints.Reset();
}

void ATDFog3DZoneActor::ApplyGroundCaptureList(UWorld& World)
{
	CSV_SCOPED_TIMING_STAT(TDFog3D, GroundCaptureList);
	FTDFluidTerrainCapture::ApplyTerrainOnlyList(*GroundCapture, World);
}

void ATDFog3DZoneActor::RequestGroundCapture(const FIntPoint& NextWindowMinCell)
{
	using namespace TDFog3DZoneActorPrivate;
	UWorld* World = GetWorld();
	if (!World || !GroundCapture)
	{
		return;
	}
	CSV_SCOPED_TIMING_STAT(TDFog3D, GroundCaptureRequest);
	const FTDFog3DGroundCapturePlacement Placement = FTDFog3DGroundLayout::ComputeCapturePlacement(
		NextWindowMinCell,
		Layer.GetResolution(),
		Settings.GroundCaptureBorderCells,
		Settings.CellSizeCm,
		GetZoneFloorZ(),
		ZoneHalfExtentCm.Z,
		Settings.GroundCaptureHeightAboveZoneCm);
	GroundCapture->OrthoWidth = static_cast<float>(Placement.OrthoWidthCm);
	GroundCapture->SetWorldLocationAndRotation(Placement.CaptureWorld, GroundCaptureRotation);
	ApplyGroundCaptureList(*World);
	FTDFluidTerrainCapture::HideOwnerAndPawns(*GroundCapture, this, true, *World);
	GroundCapture->CaptureScene();
	GroundCaptureDecodeOffsetCm = Placement.DecodeOffsetCm;
}

FTDFog3DGroundStep ATDFog3DZoneActor::MakeGroundStep(const FTDFog3DGroundDecision& Decision) const
{
	FTDFog3DGroundStep Ground;
	if (!HasGroundResources())
	{
		return Ground;
	}
	Ground.bFollowsGround = true;
	Ground.bGroundChanged = Decision.bConvertsCapture;
	if (!Ground.bGroundChanged)
	{
		return Ground;
	}
	Ground.CaptureDepthTarget = GroundCaptureTarget->GameThread_GetRenderTargetResource();
	Ground.CaptureRect = Decision.StepRect;
	Ground.DecodeOffsetCm = GroundCaptureDecodeOffsetCm;
	Ground.MissingHeightCm = Settings.GroundMissingHeightCm;
	Ground.MaterialTarget = GroundMaterialTarget->GameThread_GetRenderTargetResource();
	return Ground;
}

void ATDFog3DZoneActor::ConfirmGroundStep(const FTDFog3DGroundDecision& Decision)
{
	GroundTracker.ConfirmStep(Decision);
	if (!Decision.bConvertsCapture)
	{
		return;
	}
	DesiredMaterialParameters.Ground = FTDFog3DGroundLayout::ComputeMaterialGround(Decision.StepRect, Settings.CellSizeCm, GetZoneReferenceWorld());
	UE_LOG(LogTDFog3DZone, Verbose, TEXT("Fog3D zone %s converted the ground capture requested at frame %llu in frame %llu (min cell %d, %d, size %d)."),
		*GetName(), GroundTracker.GetRequestFrame(), GFrameCounter, Decision.StepRect.MinCell.X, Decision.StepRect.MinCell.Y, Decision.StepRect.Size);
}
