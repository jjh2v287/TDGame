#include "FluidFX/TDFluidWaterParticleActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidMassInputDataChannelReader.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "FluidFX/TDFluidWaterSplashRules.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataChannelAsset.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(TDFluid);

static TAutoConsoleVariable<int32> CVarTDFluidWaterParticles(
	TEXT("TD.FluidFX.WaterParticles"),
	-1,
	TEXT("Water particle actors: -1 follows the preset settings, 0 forces debris, whitewater, splashes and contact whitewater off."),
	ECVF_Default);

namespace TDFluidWaterParticleActorPrivate
{
	constexpr int32 CoverageSampleCount = 16;
	constexpr double SurfaceSourceSearchIntervalSeconds = 1.0;
	constexpr double MillimetersPerCentimeter = 10.0;

	enum class ESurfaceParameterSource : uint8
	{
		Computed,
		MaterialScalar,
		MaterialVectorZ,
		MaterialVectorXY,
		MaterialTexture
	};

	struct FSurfaceParameterRow
	{
		const TCHAR* RuntimeName;
		const TCHAR* MaterialParameterName;
		ESurfaceParameterSource Source;
		float FTDFluidWaterSurfaceParameters::* FloatField;
		FVector2D FTDFluidWaterSurfaceParameters::* Vector2Field;
		FVector FTDFluidWaterSurfaceParameters::* Vector3Field;
		TWeakObjectPtr<UTexture> FTDFluidWaterSurfaceParameters::* TextureField;
	};

	const FSurfaceParameterRow SurfaceParameterRows[] =
	{
		{ TEXT("User.TDWaterBaseHeightCm"), nullptr, ESurfaceParameterSource::Computed, &FTDFluidWaterSurfaceParameters::BaseHeightCm, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterUsesTerrainBase"), nullptr, ESurfaceParameterSource::Computed, &FTDFluidWaterSurfaceParameters::UsesTerrainBase, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterTerrainReferenceZCm"), TEXT("TerrainReferenceZCm"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::TerrainReferenceZCm, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterDepthReferenceCm"), TEXT("WaterDepthReferenceCm"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::DepthReferenceCm, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterRippleHeightReferenceCm"), TEXT("RippleHeightReferenceCm"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::RippleHeightReferenceCm, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterLayerActive"), TEXT("FluidWaterLayerActive"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::LayerActive, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterBorderFadeRatio"), TEXT("SimExtentsCm"), ESurfaceParameterSource::MaterialVectorZ, &FTDFluidWaterSurfaceParameters::BorderFadeRatio, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterPressureNormalStrength"), TEXT("PressureNormalStrength"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::PressureNormalStrength, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterPassiveWaveAmplitudeCm"), TEXT("PassiveWaveAmplitudeCm"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::PassiveWaveAmplitudeCm, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterPassiveWaveDirectionDegrees"), TEXT("PassiveWaveDirectionDegrees"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::PassiveWaveDirectionDegrees, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterTimeSeconds"), nullptr, ESurfaceParameterSource::Computed, &FTDFluidWaterSurfaceParameters::TimeSeconds, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterTileMapHeightCm"), TEXT("TileMap1HeightCm"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::TileMapHeightCm, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterTileMapDensityHeightCm"), TEXT("TileMap1DensityHeightCm"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::TileMapDensityHeightCm, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterTileMapFadeStartCm"), TEXT("TileMap1FadeStartCm"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::TileMapFadeStartCm, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterTileMapFadeLengthCm"), TEXT("TileMap1FadeLengthCm"), ESurfaceParameterSource::MaterialScalar, &FTDFluidWaterSurfaceParameters::TileMapFadeLengthCm, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterCoverage"), nullptr, ESurfaceParameterSource::Computed, &FTDFluidWaterSurfaceParameters::Coverage, nullptr, nullptr, nullptr },
		{ TEXT("User.TDWaterTileMapOriginCm"), TEXT("TileMap1OriginCm"), ESurfaceParameterSource::MaterialVectorXY, nullptr, &FTDFluidWaterSurfaceParameters::TileMapOriginCm, nullptr, nullptr },
		{ TEXT("User.TDWaterTileMapPeriodCm"), TEXT("TileMap1PeriodCm"), ESurfaceParameterSource::MaterialVectorXY, nullptr, &FTDFluidWaterSurfaceParameters::TileMapPeriodCm, nullptr, nullptr },
		{ TEXT("User.TDWaterCameraWorld"), nullptr, ESurfaceParameterSource::Computed, nullptr, nullptr, &FTDFluidWaterSurfaceParameters::CameraWorld, nullptr },
		{ TEXT("User.TDWaterTileMapTexture"), TEXT("TileMap1Texture"), ESurfaceParameterSource::MaterialTexture, nullptr, nullptr, nullptr, &FTDFluidWaterSurfaceParameters::TileMapTexture },
	};

	struct FSplashTrackedPoint
	{
		TPair<FObjectKey, int32> Key;
		FVector Location = FVector::ZeroVector;
		float RadiusCm = 0.0f;
		int64 DistanceSquaredKey = 0;
		int64 XMillimeters = 0;
		int64 YMillimeters = 0;
		int64 ZMillimeters = 0;
	};

	UTDFluidPresetTuningSubsystem* GetTuningSubsystem()
	{
		return GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	}

	int32 GetTuningTableGeneration()
	{
		const UTDFluidPresetTuningSubsystem* TuningSubsystem = GetTuningSubsystem();
		return TuningSubsystem ? TuningSubsystem->GetTableGeneration() : INDEX_NONE;
	}

	int64 ToMillimeters(double ValueCm)
	{
		return FMath::IsFinite(ValueCm) ? FMath::RoundToInt64(FMath::Clamp(ValueCm * MillimetersPerCentimeter, -9.0e15, 9.0e15)) : MAX_int64;
	}

	bool IsValidSurfaceHeight(float SurfaceHeightCm)
	{
		return SurfaceHeightCm != FTDFluidGrid2DSolver::NoWaterSurfaceHeight && FMath::IsFinite(SurfaceHeightCm);
	}

	bool ApplyNiagaraLook(UNiagaraComponent& Component)
	{
		const UFXSystemAsset* System = Component.GetFXSystemAsset();
		const FTDFluidNiagaraLookEntry* Look = System ? UTDFluidPresetTuningSubsystem::FindNiagaraLook(System->GetPackage()->GetFName()) : nullptr;
		if (!Look)
		{
			return false;
		}

		for (const TPair<FName, float>& Variable : Look->Floats)
		{
			Component.SetVariableFloat(Variable.Key, Variable.Value);
		}
		for (const TPair<FName, int32>& Variable : Look->Ints)
		{
			Component.SetVariableInt(Variable.Key, Variable.Value);
		}
		for (const TPair<FName, bool>& Variable : Look->Bools)
		{
			Component.SetVariableBool(Variable.Key, Variable.Value);
		}
		for (const TPair<FName, FVector2D>& Variable : Look->Vectors2)
		{
			Component.SetVariableVec2(Variable.Key, Variable.Value);
		}
		for (const TPair<FName, FVector>& Variable : Look->Vectors3)
		{
			Component.SetVariableVec3(Variable.Key, Variable.Value);
		}
		for (const TPair<FName, FLinearColor>& Variable : Look->Colors)
		{
			Component.SetVariableLinearColor(Variable.Key, Variable.Value);
		}
		return true;
	}

	void ReadMaterialSurfaceParameters(const UMaterialInterface& Material, FTDFluidWaterSurfaceParameters& OutParameters)
	{
		for (const FSurfaceParameterRow& Row : SurfaceParameterRows)
		{
			if (!Row.MaterialParameterName)
			{
				continue;
			}

			const FHashedMaterialParameterInfo ParameterInfo(FName(Row.MaterialParameterName));
			float ScalarValue = 0.0f;
			FLinearColor VectorValue = FLinearColor::Black;
			UTexture* TextureValue = nullptr;
			switch (Row.Source)
			{
			case ESurfaceParameterSource::MaterialScalar:
				if (Material.GetScalarParameterValue(ParameterInfo, ScalarValue))
				{
					OutParameters.*Row.FloatField = ScalarValue;
				}
				break;
			case ESurfaceParameterSource::MaterialVectorZ:
				if (Material.GetVectorParameterValue(ParameterInfo, VectorValue))
				{
					OutParameters.*Row.FloatField = VectorValue.B;
				}
				break;
			case ESurfaceParameterSource::MaterialVectorXY:
				if (Material.GetVectorParameterValue(ParameterInfo, VectorValue))
				{
					OutParameters.*Row.Vector2Field = FVector2D(VectorValue.R, VectorValue.G);
				}
				break;
			case ESurfaceParameterSource::MaterialTexture:
				if (Material.GetTextureParameterValue(ParameterInfo, TextureValue))
				{
					OutParameters.*Row.TextureField = TextureValue;
				}
				break;
			default:
				break;
			}
		}
	}

	void FillFallbackSurfaceParameters(const UTDFluidLiveComponent& LiveComponent, FTDFluidWaterSurfaceParameters& OutParameters)
	{
		const FTDFluidSimulationConfig& SolverConfig = LiveComponent.IsUsingGpuSolver() ? LiveComponent.SimulationConfig : LiveComponent.GetSolver().GetConfig();
		OutParameters.RippleHeightReferenceCm = SolverConfig.RippleHeightReferenceCm;
		OutParameters.DepthReferenceCm = SolverConfig.WaterDepthReferenceCm;
		OutParameters.LayerActive = LiveComponent.IsSimulationAwake() ? LiveComponent.GetWaterLayerDisplayAlpha() : 0.0f;
		OutParameters.BorderFadeRatio = LiveComponent.SimulationConfig.BorderFadeRatio;
		OutParameters.UsesTerrainBase = 0.0f;
	}

	void PushSurfaceParameters(UNiagaraComponent& Component, const FTDFluidWaterSurfaceParameters& Parameters)
	{
		for (const FSurfaceParameterRow& Row : SurfaceParameterRows)
		{
			const FName VariableName(Row.RuntimeName);
			if (Row.FloatField)
			{
				Component.SetVariableFloat(VariableName, Parameters.*Row.FloatField);
				continue;
			}
			if (Row.Vector2Field)
			{
				Component.SetVariableVec2(VariableName, Parameters.*Row.Vector2Field);
				continue;
			}
			if (Row.Vector3Field)
			{
				Component.SetVariableVec3(VariableName, Parameters.*Row.Vector3Field);
				continue;
			}

			UTexture* Texture = Row.TextureField ? (Parameters.*Row.TextureField).Get() : nullptr;
			if (Texture)
			{
				Component.SetVariableTexture(VariableName, Texture);
			}
		}
	}

	FVector ResolveCameraWorld(const UWorld& World, const FVector& FallbackLocation)
	{
		const APlayerController* PlayerController = World.GetFirstPlayerController();
		const APlayerCameraManager* CameraManager = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
		return CameraManager ? CameraManager->GetCameraLocation() : FallbackLocation;
	}

	bool IsTrackableSplashComponent(const UPrimitiveComponent* Component, const AActor& WaterActor, const AActor& ParticleActor)
	{
		if (!Component || !Component->IsRegistered() || Component->GetMobility() == EComponentMobility::Static)
		{
			return false;
		}

		const AActor* ComponentOwner = Component->GetOwner();
		return ComponentOwner != &WaterActor && ComponentOwner != &ParticleActor;
	}

	void AddTrackedPoint(const UPrimitiveComponent& Component, int32 BoneIndex, const FVector& Location, float RadiusCm, const FVector& CenterWorld, TArray<FSplashTrackedPoint>& OutPoints)
	{
		if (Location.ContainsNaN())
		{
			return;
		}

		const double DistanceSquared = FVector::DistSquared(Location, CenterWorld);
		FSplashTrackedPoint& Point = OutPoints.AddDefaulted_GetRef();
		Point.Key = TPair<FObjectKey, int32>(FObjectKey(&Component), BoneIndex);
		Point.Location = Location;
		Point.RadiusCm = RadiusCm;
		Point.DistanceSquaredKey = FMath::IsFinite(DistanceSquared) ? FMath::RoundToInt64(FMath::Min(DistanceSquared, 9.0e15)) : MAX_int64;
		Point.XMillimeters = ToMillimeters(Location.X);
		Point.YMillimeters = ToMillimeters(Location.Y);
		Point.ZMillimeters = ToMillimeters(Location.Z);
	}

	int32 AddTrackedBonePoints(const USkinnedMeshComponent& SkinnedComponent, const TArray<FName>& TrackedBoneNames, float BoneRadiusCm, const FVector& CenterWorld, TArray<FSplashTrackedPoint>& OutPoints)
	{
		int32 AddedCount = 0;
		for (const FName& BoneName : TrackedBoneNames)
		{
			const int32 BoneIndex = SkinnedComponent.GetBoneIndex(BoneName);
			if (BoneIndex == INDEX_NONE)
			{
				continue;
			}

			const int32 PointCountBefore = OutPoints.Num();
			AddTrackedPoint(SkinnedComponent, BoneIndex, SkinnedComponent.GetBoneLocation(BoneName), BoneRadiusCm, CenterWorld, OutPoints);
			AddedCount += OutPoints.Num() - PointCountBefore;
		}
		return AddedCount;
	}

	void GatherSplashPoints(const ATDFluidWaterActor& WaterActor, const AActor& ParticleActor, const FTDFluidWaterParticleSettings& Settings, const FVector& CenterWorld, TArray<FSplashTrackedPoint>& OutPoints)
	{
		OutPoints.Reset();
		TArray<UPrimitiveComponent*> OverlappingComponents;
		WaterActor.InteractionVolume->GetOverlappingComponents(OverlappingComponents);
		for (const UPrimitiveComponent* Component : OverlappingComponents)
		{
			if (!IsTrackableSplashComponent(Component, WaterActor, ParticleActor))
			{
				continue;
			}

			const USkinnedMeshComponent* SkinnedComponent = Cast<USkinnedMeshComponent>(Component);
			if (SkinnedComponent && AddTrackedBonePoints(*SkinnedComponent, WaterActor.TrackedBoneNames, Settings.SplashBoneRadiusCm, CenterWorld, OutPoints) > 0)
			{
				continue;
			}

			const float RadiusCm = FTDFluidWaterSplashRules::ResolveSplashRadiusCm(Settings, static_cast<float>(Component->Bounds.SphereRadius));
			AddTrackedPoint(*Component, INDEX_NONE, Component->Bounds.Origin, RadiusCm, CenterWorld, OutPoints);
		}

		OutPoints.StableSort([](const FSplashTrackedPoint& Left, const FSplashTrackedPoint& Right)
		{
			if (Left.DistanceSquaredKey != Right.DistanceSquaredKey)
			{
				return Left.DistanceSquaredKey < Right.DistanceSquaredKey;
			}
			if (Left.XMillimeters != Right.XMillimeters)
			{
				return Left.XMillimeters < Right.XMillimeters;
			}
			if (Left.YMillimeters != Right.YMillimeters)
			{
				return Left.YMillimeters < Right.YMillimeters;
			}
			if (Left.ZMillimeters != Right.ZMillimeters)
			{
				return Left.ZMillimeters < Right.ZMillimeters;
			}
			return Left.Key.Value < Right.Key.Value;
		});

		const int32 MaxTrackedPoints = FMath::Max(0, Settings.MaxTrackedSplashPoints);
		if (OutPoints.Num() > MaxTrackedPoints)
		{
			OutPoints.SetNum(MaxTrackedPoints);
		}
	}

	void PublishCsvStats(int32 SplashEventCount, int32 ActiveSplashCount, int32 ContactPointCount, int32 ContactStampCount)
	{
		CSV_CUSTOM_STAT(TDFluid, WaterSplashEvents, SplashEventCount, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(TDFluid, WaterSplashActive, ActiveSplashCount, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(TDFluid, WaterContactPoints, ContactPointCount, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(TDFluid, WaterContactStamps, ContactStampCount, ECsvCustomStatOp::Set);
	}
}

ATDFluidWaterParticleActor::ATDFluidWaterParticleActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ATDFluidWaterParticleActor::BeginPlay()
{
	using namespace TDFluidWaterParticleActorPrivate;

	Super::BeginPlay();
	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GetTuningSubsystem())
	{
		PresetTuningReloadedHandle = TuningSubsystem->OnPresetTuningReloaded.AddUObject(this, &ATDFluidWaterParticleActor::HandlePresetTuningReloaded);
	}
	ResolveSettingsIfStale();
	RequestMissingAssetLoads();
}

void ATDFluidWaterParticleActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	using namespace TDFluidWaterParticleActorPrivate;

	if (AssetLoadHandle.IsValid())
	{
		AssetLoadHandle->CancelHandle();
		AssetLoadHandle.Reset();
	}
	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GetTuningSubsystem())
	{
		TuningSubsystem->OnPresetTuningReloaded.Remove(PresetTuningReloadedHandle);
	}
	PresetTuningReloadedHandle.Reset();

	if (DebrisComponent)
	{
		DebrisComponent->DestroyComponent();
		DebrisComponent = nullptr;
	}
	if (WhitewaterComponent)
	{
		WhitewaterComponent->DestroyComponent();
		WhitewaterComponent = nullptr;
	}
	ActiveSplashComponents.Reset();
	SplashPointStates.Reset();
	bHasRequestedAssetLoad = false;
	Super::EndPlay(EndPlayReason);
}

void ATDFluidWaterParticleActor::Tick(float DeltaSeconds)
{
	using namespace TDFluidWaterParticleActorPrivate;

	Super::Tick(DeltaSeconds);
	CSV_SCOPED_TIMING_STAT(TDFluid, WaterParticleTick);

	UTDFluidLiveComponent* LiveComponent = GetWaterLiveComponent();
	ResolveSettingsIfStale();
	RequestMissingAssetLoads();
	if (!LiveComponent || CVarTDFluidWaterParticles.GetValueOnGameThread() == 0 || !LiveComponent->IsSimulationInitialized())
	{
		DeactivateParticleLayers();
		SplashPointStates.Reset();
		LastSplashEventCount = 0;
		LastContactPointCount = 0;
		LastContactStampCount = 0;
		PublishCsvStats(LastSplashEventCount, GetActiveSplashCount(), LastContactPointCount, LastContactStampCount);
		return;
	}

	const FTDFluidWaterSurfaceParameters SurfaceParameters = ResolveWaterSurfaceParameters();
	DriveParticleLayer(DebrisComponent, DebrisSystem.Get(), DebrisLookGeneration, DebrisActivationSeconds, SurfaceParameters);
	DriveParticleLayer(WhitewaterComponent, WhitewaterSystem.Get(), WhitewaterLookGeneration, WhitewaterActivationSeconds, SurfaceParameters);
	EmitSplashEvents(DeltaSeconds);
	ApplyContactWhitewater();
	PublishCsvStats(LastSplashEventCount, GetActiveSplashCount(), LastContactPointCount, LastContactStampCount);
}

int32 ATDFluidWaterParticleActor::GetActiveSplashCount() const
{
	int32 ActiveCount = 0;
	for (const TWeakObjectPtr<UNiagaraComponent>& SplashComponent : ActiveSplashComponents)
	{
		if (SplashComponent.IsValid() && SplashComponent->IsActive())
		{
			++ActiveCount;
		}
	}
	return ActiveCount;
}

bool ATDFluidWaterParticleActor::SpawnSplashEvent(const FTDFluidWaterSplashEvent& SplashEvent)
{
	using namespace TDFluidWaterParticleActorPrivate;

	UNiagaraSystem* LoadedSplashSystem = SplashSystem.Get();
	if (!LoadedSplashSystem || !GetWorld() || SplashEvent.WorldLocation.ContainsNaN())
	{
		return false;
	}

	UNiagaraComponent* SplashComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, LoadedSplashSystem, SplashEvent.WorldLocation, FRotator::ZeroRotator, FVector::OneVector, true, false, ENCPoolMethod::AutoRelease);
	if (!SplashComponent)
	{
		return false;
	}

	SplashComponent->SetRandomSeedOffset(SplashSerial++);
	ApplyNiagaraLook(*SplashComponent);
	SplashComponent->SetVariableFloat(FName(TEXT("User.TDSplashImpactSpeedCmPerSec")), SplashEvent.ImpactSpeedCmPerSec);
	SplashComponent->SetVariableFloat(FName(TEXT("User.TDSplashImpactRadiusCm")), SplashEvent.RadiusCm);
	SplashComponent->SetVariableFloat(FName(TEXT("User.TDSplashSurfaceHeightCm")), SplashEvent.SurfaceHeightCm);
	SplashComponent->SetVariableVec3(FName(TEXT("User.TDSplashImpactVelocity")), SplashEvent.ImpactVelocity);
	SplashComponent->Activate(true);
	ActiveSplashComponents.AddUnique(SplashComponent);
	++EmittedSplashEventCount;
	return true;
}

UTDFluidLiveComponent* ATDFluidWaterParticleActor::GetWaterLiveComponent() const
{
	return WaterActor ? WaterActor->LiveComponent.Get() : nullptr;
}

void ATDFluidWaterParticleActor::ResolveSettingsIfStale()
{
	const UTDFluidLiveComponent* LiveComponent = GetWaterLiveComponent();
	const ETDFluidPresetKind PresetKind = LiveComponent && LiveComponent->ActivePreset ? LiveComponent->ActivePreset->PresetKind : ETDFluidPresetKind::Water;
	if (ResolvedPresetKind.IsSet() && ResolvedPresetKind.GetValue() == PresetKind)
	{
		return;
	}

	ResolvedPresetKind = PresetKind;
	Settings = UTDFluidPresetAsset::BuildWaterParticleForPresetKind(PresetKind);
}

void ATDFluidWaterParticleActor::HandlePresetTuningReloaded(int32 TableGeneration)
{
	ResolvedPresetKind.Reset();
	ResolveSettingsIfStale();
	DebrisLookGeneration = INDEX_NONE;
	WhitewaterLookGeneration = INDEX_NONE;
}

void ATDFluidWaterParticleActor::RequestMissingAssetLoads()
{
	if (bHasRequestedAssetLoad)
	{
		return;
	}

	bHasRequestedAssetLoad = true;
	TArray<FSoftObjectPath> UnloadedAssetPaths;
	if (!DebrisSystem.IsNull() && !DebrisSystem.Get())
	{
		UnloadedAssetPaths.Add(DebrisSystem.ToSoftObjectPath());
	}
	if (!WhitewaterSystem.IsNull() && !WhitewaterSystem.Get())
	{
		UnloadedAssetPaths.Add(WhitewaterSystem.ToSoftObjectPath());
	}
	if (!SplashSystem.IsNull() && !SplashSystem.Get())
	{
		UnloadedAssetPaths.Add(SplashSystem.ToSoftObjectPath());
	}
	if (!ContactDataChannel.IsNull() && !ContactDataChannel.Get())
	{
		UnloadedAssetPaths.Add(ContactDataChannel.ToSoftObjectPath());
	}
	if (UnloadedAssetPaths.IsEmpty())
	{
		return;
	}

	AssetLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(UnloadedAssetPaths);
}

void ATDFluidWaterParticleActor::DeactivateParticleLayers()
{
	if (DebrisComponent && DebrisComponent->IsActive())
	{
		DebrisComponent->Deactivate();
	}
	if (WhitewaterComponent && WhitewaterComponent->IsActive())
	{
		WhitewaterComponent->Deactivate();
	}
}

void ATDFluidWaterParticleActor::DriveParticleLayer(TObjectPtr<UNiagaraComponent>& Component, UNiagaraSystem* System, int32& AppliedLookGeneration, double& ActivationSeconds, const FTDFluidWaterSurfaceParameters& Parameters)
{
	using namespace TDFluidWaterParticleActorPrivate;

	UTDFluidLiveComponent* LiveComponent = GetWaterLiveComponent();
	const UWorld* World = GetWorld();
	if (!System || !LiveComponent || !World)
	{
		return;
	}

	if (!Component)
	{
		Component = NewObject<UNiagaraComponent>(this);
		Component->SetAutoActivate(false);
		Component->SetAsset(System);
		Component->SetUsingAbsoluteLocation(true);
		Component->SetUsingAbsoluteRotation(true);
		Component->SetupAttachment(GetRootComponent());
		Component->RegisterComponent();
		AppliedLookGeneration = INDEX_NONE;
	}

	Component->SetWorldLocation(LiveComponent->GetSimulationWindowCenterWorld());
	const int32 TableGeneration = GetTuningTableGeneration();
	if (AppliedLookGeneration != TableGeneration)
	{
		AppliedLookGeneration = TableGeneration;
		ApplyNiagaraLook(*Component);
	}

	const double NowSeconds = World->GetTimeSeconds();
	const bool bNeedsActivation = !Component->IsActive();
	if (bNeedsActivation)
	{
		ActivationSeconds = NowSeconds;
	}

	LiveComponent->DriveNiagaraComponent(*Component, *WaterActor);
	PushSurfaceParameters(*Component, Parameters);
	const double PrefillSeconds = Settings.DebrisPrefillSeconds;
	const bool bIsPrefilling = PrefillSeconds > 0.0 && NowSeconds - ActivationSeconds < PrefillSeconds;
	Component->SetVariableFloat(FName(TEXT("User.TDDebrisPrefillRateScale")), bIsPrefilling ? static_cast<float>(1.0 / PrefillSeconds) : 0.0f);
	if (bNeedsActivation)
	{
		Component->Activate(true);
	}
}

FTDFluidWaterSurfaceParameters ATDFluidWaterParticleActor::ResolveWaterSurfaceParameters()
{
	using namespace TDFluidWaterParticleActorPrivate;

	if (SurfaceParametersOverride.IsSet())
	{
		return SurfaceParametersOverride.GetValue();
	}

	FTDFluidWaterSurfaceParameters Parameters;
	const UTDFluidLiveComponent* LiveComponent = GetWaterLiveComponent();
	const UWorld* World = GetWorld();
	if (!LiveComponent || !World)
	{
		return Parameters;
	}

	if (const UMaterialInterface* SurfaceMaterial = ResolveSurfaceMaterial())
	{
		ReadMaterialSurfaceParameters(*SurfaceMaterial, Parameters);
		Parameters.UsesTerrainBase = Settings.bUseTerrainDepthSurfaceBase && Parameters.LayerActive > 0.0f ? 1.0f : 0.0f;
	}
	else
	{
		FillFallbackSurfaceParameters(*LiveComponent, Parameters);
	}

	const FVector WindowCenterWorld = LiveComponent->GetSimulationWindowCenterWorld();
	Parameters.TimeSeconds = static_cast<float>(World->GetTimeSeconds());
	Parameters.CameraWorld = ResolveCameraWorld(*World, WindowCenterWorld);

	float CenterSurfaceHeightCm = 0.0f;
	const AActor* SurfaceSourceActor = ResolveSurfaceSourceActor();
	if (TryGetSurfaceHeightAt(WindowCenterWorld, CenterSurfaceHeightCm))
	{
		Parameters.BaseHeightCm = CenterSurfaceHeightCm;
	}
	else if (SurfaceSourceActor)
	{
		Parameters.BaseHeightCm = static_cast<float>(SurfaceSourceActor->GetComponentsBoundingBox(true).Max.Z);
	}
	else
	{
		Parameters.BaseHeightCm = static_cast<float>(WaterActor->GetActorLocation().Z);
	}

	const double NowSeconds = World->GetTimeSeconds();
	const bool bIsCoverageStale = LastCoverageRefreshSeconds < 0.0
		|| NowSeconds < LastCoverageRefreshSeconds
		|| NowSeconds - LastCoverageRefreshSeconds >= Settings.CoverageRefreshSeconds;
	if (bIsCoverageStale)
	{
		CachedWaterCoverage = ComputeWaterCoverage();
		LastCoverageRefreshSeconds = NowSeconds;
	}
	Parameters.Coverage = CachedWaterCoverage;
	return Parameters;
}

UMaterialInterface* ATDFluidWaterParticleActor::ResolveSurfaceMaterial()
{
	const AActor* SurfaceSourceActor = ResolveSurfaceSourceActor();
	if (!SurfaceSourceActor)
	{
		return nullptr;
	}

	TInlineComponentArray<UStaticMeshComponent*> MeshComponents(SurfaceSourceActor);
	for (const UStaticMeshComponent* MeshComponent : MeshComponents)
	{
		if (UMaterialInterface* SurfaceMaterial = MeshComponent ? MeshComponent->GetMaterial(0) : nullptr)
		{
			return SurfaceMaterial;
		}
	}
	return nullptr;
}

AActor* ATDFluidWaterParticleActor::ResolveSurfaceSourceActor()
{
	using namespace TDFluidWaterParticleActorPrivate;

	if (SurfaceMaterialSourceActor)
	{
		return SurfaceMaterialSourceActor;
	}
	if (AActor* CachedActor = CachedSurfaceSourceActor.Get())
	{
		return CachedActor;
	}

	UWorld* World = GetWorld();
	if (!World || !WaterActor || WaterActor->WaterSurfaceActorTag.IsNone())
	{
		return nullptr;
	}

	const double NowSeconds = World->GetTimeSeconds();
	const bool bSearchedRecently = LastSurfaceSourceSearchSeconds >= 0.0
		&& NowSeconds >= LastSurfaceSourceSearchSeconds
		&& NowSeconds - LastSurfaceSourceSearchSeconds < SurfaceSourceSearchIntervalSeconds;
	if (bSearchedRecently)
	{
		return nullptr;
	}

	LastSurfaceSourceSearchSeconds = NowSeconds;
	for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator)
	{
		if (ActorIterator->ActorHasTag(WaterActor->WaterSurfaceActorTag))
		{
			CachedSurfaceSourceActor = *ActorIterator;
			return *ActorIterator;
		}
	}
	return nullptr;
}

bool ATDFluidWaterParticleActor::TryGetSurfaceHeightAt(const FVector& WorldLocation, float& OutSurfaceHeightCm) const
{
	using namespace TDFluidWaterParticleActorPrivate;

	const UTDFluidLiveComponent* LiveComponent = GetWaterLiveComponent();
	const int32 Resolution = LiveComponent ? LiveComponent->GetTerrainSampleResolution() : 0;
	if (Resolution <= 1)
	{
		return false;
	}

	const FVector2D FirstCellCenterPlane = LiveComponent->TerrainSampleCellToPlaneCenter(0, 0);
	const double CellSizeCm = FVector2D::Distance(LiveComponent->TerrainSampleCellToPlaneCenter(1, 0), FirstCellCenterPlane);
	FIntPoint Cell;
	if (!FTDFluidWaterSplashRules::TryResolveFieldCell(LiveComponent->ProjectWorldToSolverPlane(WorldLocation), FirstCellCenterPlane, CellSizeCm, Resolution, Cell))
	{
		return false;
	}

	const float SurfaceHeightCm = LiveComponent->GetWaterSurfaceHeightAtTerrainSampleCell(Cell.X, Cell.Y);
	if (!IsValidSurfaceHeight(SurfaceHeightCm))
	{
		return false;
	}

	OutSurfaceHeightCm = SurfaceHeightCm;
	return true;
}

float ATDFluidWaterParticleActor::ComputeWaterCoverage() const
{
	using namespace TDFluidWaterParticleActorPrivate;

	const UTDFluidLiveComponent* LiveComponent = GetWaterLiveComponent();
	const int32 Resolution = LiveComponent ? LiveComponent->GetTerrainSampleResolution() : 0;
	if (Resolution <= 0)
	{
		return 0.0f;
	}

	int32 WaterSampleCount = 0;
	for (int32 SampleY = 0; SampleY < CoverageSampleCount; ++SampleY)
	{
		const int32 CellY = FMath::Min(Resolution - 1, (2 * SampleY + 1) * Resolution / (2 * CoverageSampleCount));
		for (int32 SampleX = 0; SampleX < CoverageSampleCount; ++SampleX)
		{
			const int32 CellX = FMath::Min(Resolution - 1, (2 * SampleX + 1) * Resolution / (2 * CoverageSampleCount));
			if (IsValidSurfaceHeight(LiveComponent->GetWaterSurfaceHeightAtTerrainSampleCell(CellX, CellY)))
			{
				++WaterSampleCount;
			}
		}
	}
	return static_cast<float>(WaterSampleCount) / static_cast<float>(CoverageSampleCount * CoverageSampleCount);
}

void ATDFluidWaterParticleActor::EmitSplashEvents(float DeltaSeconds)
{
	using namespace TDFluidWaterParticleActorPrivate;

	LastSplashEventCount = 0;
	const UTDFluidLiveComponent* LiveComponent = GetWaterLiveComponent();
	if (!Settings.bEmitSplashEvents || !SplashSystem.Get() || !LiveComponent || !WaterActor->InteractionVolume)
	{
		SplashPointStates.Reset();
		return;
	}

	const FVector CenterWorld = LiveComponent->GetSimulationWindowCenterWorld();
	TArray<FSplashTrackedPoint> TrackedPoints;
	GatherSplashPoints(*WaterActor, *this, Settings, CenterWorld, TrackedPoints);

	TMap<TPair<FObjectKey, int32>, FTDFluidWaterSplashPointState> NextPointStates;
	NextPointStates.Reserve(TrackedPoints.Num());
	TArray<FTDFluidWaterSplashEvent> SplashEvents;
	for (const FSplashTrackedPoint& TrackedPoint : TrackedPoints)
	{
		float SurfaceHeightCm = 0.0f;
		if (!TryGetSurfaceHeightAt(TrackedPoint.Location, SurfaceHeightCm))
		{
			continue;
		}

		FTDFluidWaterSplashPointState PointState = SplashPointStates.FindRef(TrackedPoint.Key);
		FTDFluidWaterSplashEvent SplashEvent;
		if (FTDFluidWaterSplashRules::EvaluateSplashPoint(Settings, TrackedPoint.Location, TrackedPoint.RadiusCm, SurfaceHeightCm, DeltaSeconds, PointState, SplashEvent))
		{
			SplashEvents.Add(SplashEvent);
		}
		NextPointStates.Add(TrackedPoint.Key, PointState);
	}
	SplashPointStates = MoveTemp(NextPointStates);

	PruneInactiveSplashes();
	const int32 AvailableSplashSlots = FMath::Max(0, Settings.MaxConcurrentSplashes - ActiveSplashComponents.Num());
	FTDFluidWaterSplashRules::SortAndClampSplashEvents(SplashEvents, CenterWorld, AvailableSplashSlots);
	for (const FTDFluidWaterSplashEvent& SplashEvent : SplashEvents)
	{
		if (SpawnSplashEvent(SplashEvent))
		{
			++LastSplashEventCount;
		}
	}
}

void ATDFluidWaterParticleActor::ApplyContactWhitewater()
{
	LastContactPointCount = 0;
	LastContactStampCount = 0;
	UTDFluidLiveComponent* LiveComponent = GetWaterLiveComponent();
	const UNiagaraDataChannelAsset* ContactChannelAsset = ContactDataChannel.Get();
	const UNiagaraDataChannel* ContactChannel = ContactChannelAsset ? ContactChannelAsset->Get() : nullptr;
	if (!Settings.bApplyContactWhitewater || !ContactChannel || !LiveComponent)
	{
		return;
	}

	const FVector CenterWorld = LiveComponent->GetSimulationWindowCenterWorld();
	TArray<FTDFluidMassInputPoint> ContactPoints;
	FTDFluidMassInputDataChannelReader ContactReader;
	LastContactPointCount = ContactReader.ReadPoints(
		this,
		ContactChannel,
		FNiagaraDataChannelSearchParameters(CenterWorld),
		Settings.bReadContactPreviousFrame,
		Settings.MaxContactPointsPerTick,
		Settings.ContactReadFallbackRadiusCm,
		ContactPoints);

	const int32 StampBudget = FTDFluidWaterSplashRules::ResolveContactStampBudget(Settings, LiveComponent->SimulationConfig.MaxPointsPerTick, LiveComponent->GetPendingBrushStamps().Num());
	TArray<FTDFluidBrushStamp> ContactStamps;
	FTDFluidWaterSplashRules::BuildContactWhitewaterStamps(ContactPoints, CenterWorld, StampBudget, Settings.ContactReadFallbackRadiusCm, ContactStamps);
	for (const FTDFluidBrushStamp& ContactStamp : ContactStamps)
	{
		LiveComponent->QueueBrushStamp(ContactStamp);
	}
	LastContactStampCount = ContactStamps.Num();
}

void ATDFluidWaterParticleActor::PruneInactiveSplashes()
{
	ActiveSplashComponents.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& SplashComponent)
	{
		return !SplashComponent.IsValid() || !SplashComponent->IsActive();
	});
}
