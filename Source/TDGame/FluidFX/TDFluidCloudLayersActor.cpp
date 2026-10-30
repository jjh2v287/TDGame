#include "FluidFX/TDFluidCloudLayersActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidCloud, Log, All);

static TAutoConsoleVariable<int32> CVarTDFluidCloudSetup(
	TEXT("TD.FluidFX.CloudSetup"),
	-1,
	TEXT("Cloud layers setup index: -1 uses the actor default, 0 or more selects that setup."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarTDFluidCloudViewSampleScale(
	TEXT("TD.FluidFX.CloudViewSampleScale"),
	-1.0f,
	TEXT("Cloud layers view sample count scale: negative picks the near or far value automatically, 0 or more forces that value."),
	ECVF_Default);

namespace TDFluidCloudLayersPrivate
{
	constexpr int32 CloudSlotCount = 4;
	constexpr float MinimumLayerHeightKm = 0.1f;
	constexpr float MinimumViewSampleCountScale = 0.05f;
	constexpr float ActiveSourceThreshold = 0.5f;
	constexpr float MammatusPatternIndex = 2.0f;
	constexpr float PatternIndexTolerance = 0.5f;
	constexpr float SimulatedCoverageSource = 1.0f;
	constexpr float SourceIndexTolerance = 0.5f;
	constexpr float TilingFootprintMaxKm = 1e-6f;
	constexpr double CentimetersPerKilometer = 100000.0;
	constexpr double SourceRetryIntervalSeconds = 1.0;
	const TCHAR* const CoverageTextureSuffix = TEXT("CoverageTexture");
	const TCHAR* const SectionTextureSuffix = TEXT("SectionTexture");
	const TCHAR* const FootprintSizeSuffix = TEXT("FootprintSizeKm");
	const TCHAR* const TerrainHeightRangeParameterName = TEXT("CloudTerrainHeightRangeKm");

	const TCHAR* ResolveRoleSuffix(ETDFluidCloudSlotTextureRole Role)
	{
		return Role == ETDFluidCloudSlotTextureRole::Section ? SectionTextureSuffix : CoverageTextureSuffix;
	}

	float ReadSlotScalar(UMaterialInstanceDynamic& Material, int32 SlotIndex, const TCHAR* Suffix)
	{
		return Material.K2_GetScalarParameterValue(ATDFluidCloudLayersActor::MakeSlotParameterName(SlotIndex, Suffix));
	}

	TArray<FTDFluidCloudSlotBand> ReadSlotBands(UMaterialInstanceDynamic& Material)
	{
		TArray<FTDFluidCloudSlotBand> Bands;
		Bands.Reserve(CloudSlotCount);
		for (int32 SlotIndex = 0; SlotIndex < CloudSlotCount; ++SlotIndex)
		{
			FTDFluidCloudSlotBand& Band = Bands.AddDefaulted_GetRef();
			Band.bIsActive = ReadSlotScalar(Material, SlotIndex, TEXT("Source")) > ActiveSourceThreshold;
			Band.BottomKm = ReadSlotScalar(Material, SlotIndex, TEXT("BottomKm"));
			Band.ThicknessKm = ReadSlotScalar(Material, SlotIndex, TEXT("ThicknessKm"));
			Band.TerrainFollow = ReadSlotScalar(Material, SlotIndex, TEXT("TerrainFollow"));
			const bool bIsMammatus = FMath::Abs(ReadSlotScalar(Material, SlotIndex, TEXT("Pattern")) - MammatusPatternIndex) < PatternIndexTolerance;
			Band.LobeDepthRatio = bIsMammatus ? ReadSlotScalar(Material, SlotIndex, TEXT("LobeDepthRatio")) : 0.0f;
		}
		return Bands;
	}

	ATDFluidLiveActor* PickLiveActorByName(ATDFluidLiveActor* Current, ATDFluidLiveActor* Candidate)
	{
		if (!Current)
		{
			return Candidate;
		}
		return Candidate->GetFName().LexicalLess(Current->GetFName()) ? Candidate : Current;
	}
}

ATDFluidCloudLayersActor::ATDFluidCloudLayersActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	CloudComponent = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("CloudComponent"));
	CloudComponent->Mobility = EComponentMobility::Movable;
	CloudComponent->Material.Reset();
	SetRootComponent(CloudComponent);
}

FVector2D ATDFluidCloudLayersActor::ComputeSlabKm(const TArray<FTDFluidCloudSlotBand>& Bands, const FVector2D& TerrainHeightRangeKm)
{
	double BottomKm = TNumericLimits<double>::Max();
	double TopKm = -TNumericLimits<double>::Max();
	bool bHasActiveBand = false;
	for (const FTDFluidCloudSlotBand& Band : Bands)
	{
		if (!Band.bIsActive)
		{
			continue;
		}

		const double ThicknessKm = FMath::Max(static_cast<double>(Band.ThicknessKm), 0.0);
		BottomKm = FMath::Min(BottomKm, Band.BottomKm + Band.TerrainFollow * TerrainHeightRangeKm.X - Band.LobeDepthRatio * ThicknessKm);
		TopKm = FMath::Max(TopKm, Band.BottomKm + ThicknessKm + Band.TerrainFollow * TerrainHeightRangeKm.Y);
		bHasActiveBand = true;
	}

	if (!bHasActiveBand)
	{
		return FVector2D::ZeroVector;
	}

	const double SlabBottomKm = FMath::Max(BottomKm, 0.0);
	return FVector2D(SlabBottomKm, FMath::Max(TopKm - SlabBottomKm, static_cast<double>(TDFluidCloudLayersPrivate::MinimumLayerHeightKm)));
}

bool ATDFluidCloudLayersActor::ResolveNearSampling(float CameraAltitudeKm, const FVector2D& SlabKm, float EnterMarginKm, float ExitMarginKm, bool bWasNear)
{
	if (SlabKm.Y <= 0.0)
	{
		return false;
	}

	const double MarginKm = bWasNear ? FMath::Max(ExitMarginKm, EnterMarginKm) : EnterMarginKm;
	const double LowerKm = SlabKm.X - MarginKm;
	const double UpperKm = SlabKm.X + SlabKm.Y + MarginKm;
	return CameraAltitudeKm >= LowerKm && CameraAltitudeKm <= UpperKm;
}

int32 ATDFluidCloudLayersActor::ResolveSetupIndex(int32 CVarValue, int32 DefaultIndex, int32 SetupCount)
{
	if (SetupCount <= 0)
	{
		return INDEX_NONE;
	}

	if (CVarValue >= 0 && CVarValue < SetupCount)
	{
		return CVarValue;
	}

	return DefaultIndex >= 0 && DefaultIndex < SetupCount ? DefaultIndex : 0;
}

FName ATDFluidCloudLayersActor::MakeSlotParameterName(int32 SlotIndex, const FString& Suffix)
{
	return FName(*FString::Printf(TEXT("CloudSlot%d%s"), SlotIndex, *Suffix));
}

bool ATDFluidCloudLayersActor::ShouldWarnWrapMismatch(float Source, float FootprintSizeKmX, bool bIsSourceWrapped)
{
	if (bIsSourceWrapped)
	{
		return false;
	}

	const bool bIsSimulatedCoverage = FMath::Abs(Source - TDFluidCloudLayersPrivate::SimulatedCoverageSource) < TDFluidCloudLayersPrivate::SourceIndexTolerance;
	return bIsSimulatedCoverage && FootprintSizeKmX <= TDFluidCloudLayersPrivate::TilingFootprintMaxKm;
}

void ATDFluidCloudLayersActor::BeginPlay()
{
	Super::BeginPlay();

	ReadSceneSettings();
	HideCloudLayer();
	if (!ResolveSceneCloudOwnership())
	{
		bIsYieldingSceneCloud = true;
		SetActorTickEnabled(false);
		return;
	}

	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr)
	{
		PresetTuningReloadedHandle = TuningSubsystem->OnPresetTuningReloaded.AddUObject(this, &ATDFluidCloudLayersActor::HandlePresetTuningReloaded);
	}

	TArray<FSoftObjectPath> MaterialPaths;
	for (const FTDFluidCloudSetup& Setup : Setups)
	{
		if (!Setup.CloudMaterial.IsNull())
		{
			MaterialPaths.AddUnique(Setup.CloudMaterial.ToSoftObjectPath());
		}
	}

	if (MaterialPaths.IsEmpty())
	{
		HandleCloudMaterialsLoaded();
		return;
	}

	MaterialLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		MaterialPaths,
		FStreamableDelegate::CreateUObject(this, &ATDFluidCloudLayersActor::HandleCloudMaterialsLoaded));
}

void ATDFluidCloudLayersActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MaterialLoadHandle.IsValid())
	{
		MaterialLoadHandle->CancelHandle();
		MaterialLoadHandle.Reset();
	}

	if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr)
	{
		TuningSubsystem->OnPresetTuningReloaded.Remove(PresetTuningReloadedHandle);
	}
	PresetTuningReloadedHandle.Reset();

	RestoreHiddenSceneClouds();
	SetupMaterials.Reset();
	ActiveMaterial = nullptr;
	SourceLiveActors.Reset();
	BoundSlotTextures.Reset();
	ActiveSetupIndex = INDEX_NONE;
	bHasLoadedCloudMaterials = false;

	Super::EndPlay(EndPlayReason);
}

void ATDFluidCloudLayersActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsYieldingSceneCloud || !bHasLoadedCloudMaterials)
	{
		return;
	}

	const int32 ConsoleValue = CVarTDFluidCloudSetup.GetValueOnGameThread();
	const int32 DesiredSetupIndex = ResolveSetupIndex(ConsoleValue, DefaultSetupIndex, Setups.Num());
	WarnIfSetupConsoleValueIsOutOfRange(ConsoleValue);
	if (DesiredSetupIndex != ActiveSetupIndex)
	{
		ActivateSetup(DesiredSetupIndex);
	}

	if (!ActiveMaterial)
	{
		return;
	}

	RetrySourceResolution();
	BindSlotTextures(false);
	UpdateViewSampling(false);
}

bool ATDFluidCloudLayersActor::ResolveSceneCloudOwnership()
{
	const UWorld* World = GetWorld();
	TArray<UVolumetricCloudComponent*> OtherClouds;
	for (TObjectIterator<UVolumetricCloudComponent> It; It; ++It)
	{
		UVolumetricCloudComponent* OtherCloud = *It;
		if (OtherCloud == CloudComponent || OtherCloud->GetWorld() != World || !OtherCloud->IsRegistered() || !OtherCloud->IsVisible())
		{
			continue;
		}
		OtherClouds.Add(OtherCloud);
	}

	if (OtherClouds.IsEmpty())
	{
		return true;
	}

	if (!bTakesOverSceneCloud)
	{
		UE_LOG(LogTDFluidCloud, Warning, TEXT("%s yields to %d visible volumetric cloud(s) already in the scene and stays hidden."), *GetName(), OtherClouds.Num());
		return false;
	}

	for (UVolumetricCloudComponent* OtherCloud : OtherClouds)
	{
		OtherCloud->SetVisibility(false);
		HiddenSceneClouds.Add(OtherCloud);
	}
	UE_LOG(LogTDFluidCloud, Log, TEXT("%s takes over the scene cloud and hides %d other volumetric cloud(s)."), *GetName(), OtherClouds.Num());
	return true;
}

void ATDFluidCloudLayersActor::RestoreHiddenSceneClouds()
{
	for (const TWeakObjectPtr<UVolumetricCloudComponent>& HiddenCloud : HiddenSceneClouds)
	{
		if (UVolumetricCloudComponent* Cloud = HiddenCloud.Get())
		{
			Cloud->SetVisibility(true);
		}
	}
	HiddenSceneClouds.Reset();
}

void ATDFluidCloudLayersActor::HandleCloudMaterialsLoaded()
{
	bHasLoadedCloudMaterials = true;
	SetupMaterials.SetNum(Setups.Num());
	ResolveSourceLiveActors();
	ActivateSetup(ResolveSetupIndex(CVarTDFluidCloudSetup.GetValueOnGameThread(), DefaultSetupIndex, Setups.Num()));
}

void ATDFluidCloudLayersActor::HandlePresetTuningReloaded(int32)
{
	ReadSceneSettings();
	for (UMaterialInstanceDynamic* SetupMaterial : SetupMaterials)
	{
		if (SetupMaterial)
		{
			UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*SetupMaterial);
		}
	}

	if (!ActiveMaterial)
	{
		return;
	}

	ApplyTracingSettings();
	RefreshActiveLayer();
	BindSlotTextures(true);
	ApplySourceAwake();
	UpdateViewSampling(true);
}

void ATDFluidCloudLayersActor::ReadSceneSettings()
{
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFluidCloudSceneSettings::StaticStruct(), &SceneSettings);
}

void ATDFluidCloudLayersActor::ActivateSetup(int32 SetupIndex)
{
	ActiveSetupIndex = SetupIndex;
	ActiveMaterial = nullptr;
	BoundSlotTextures.Reset();
	WrapMismatchWarnedSlots.Reset();
	if (!Setups.IsValidIndex(SetupIndex))
	{
		HideCloudLayer();
		ApplySourceAwake();
		return;
	}

	UMaterialInterface* CloudMaterial = Setups[SetupIndex].CloudMaterial.Get();
	if (!CloudMaterial)
	{
		UE_LOG(LogTDFluidCloud, Warning, TEXT("%s setup %d (%s) has no loaded cloud material: %s"), *GetName(), SetupIndex, *Setups[SetupIndex].SetupName.ToString(), *Setups[SetupIndex].CloudMaterial.ToString());
		HideCloudLayer();
		ApplySourceAwake();
		return;
	}

	if (SetupMaterials.Num() != Setups.Num())
	{
		SetupMaterials.SetNum(Setups.Num());
	}

	TObjectPtr<UMaterialInstanceDynamic>& SetupMaterial = SetupMaterials[SetupIndex];
	if (!SetupMaterial)
	{
		SetupMaterial = UMaterialInstanceDynamic::Create(CloudMaterial, this);
		UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*SetupMaterial);
	}

	ActiveMaterial = SetupMaterial;
	CloudComponent->SetMaterial(ActiveMaterial);
	ApplyTracingSettings();
	RefreshActiveLayer();
	BindSlotTextures(true);
	ApplySourceAwake();
	UpdateViewSampling(true);
}

void ATDFluidCloudLayersActor::RefreshActiveLayer()
{
	if (!ActiveMaterial)
	{
		HideCloudLayer();
		return;
	}

	const FLinearColor TerrainRangeKm = ActiveMaterial->K2_GetVectorParameterValue(FName(TDFluidCloudLayersPrivate::TerrainHeightRangeParameterName));
	ActiveSlabKm = ComputeSlabKm(TDFluidCloudLayersPrivate::ReadSlotBands(*ActiveMaterial), FVector2D(TerrainRangeKm.R, TerrainRangeKm.G));
	if (ActiveSlabKm.Y <= 0.0)
	{
		HideCloudLayer();
		return;
	}

	CloudComponent->SetLayerBottomAltitude(ActiveSlabKm.X);
	CloudComponent->SetLayerHeight(ActiveSlabKm.Y);
	CloudComponent->SetVisibility(true);
}

void ATDFluidCloudLayersActor::HideCloudLayer()
{
	ActiveSlabKm = FVector2D::ZeroVector;
	CloudComponent->SetVisibility(false);
}

void ATDFluidCloudLayersActor::ApplyTracingSettings()
{
	CloudComponent->SetTracingMaxDistance(SceneSettings.TracingMaxDistanceKm);
	CloudComponent->SetStopTracingTransmittanceThreshold(SceneSettings.StopTracingTransmittanceThreshold);
	CloudComponent->SetShadowViewSampleCountScale(SceneSettings.ShadowViewSampleCountScale);
}

void ATDFluidCloudLayersActor::ResolveSourceLiveActors()
{
	LastSourceResolveSeconds = FPlatformTime::Seconds();
	TSet<FName> SourceTags;
	for (const FTDFluidCloudSetup& Setup : Setups)
	{
		for (const FTDFluidCloudSlotSource& SlotSource : Setup.SlotSources)
		{
			if (!SlotSource.LiveActorTag.IsNone())
			{
				SourceTags.Add(SlotSource.LiveActorTag);
			}
		}
	}

	TMap<FName, ATDFluidLiveActor*> FoundActors;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ATDFluidLiveActor> It(World); It; ++It)
		{
			ATDFluidLiveActor* LiveActor = *It;
			for (const FName& SourceTag : SourceTags)
			{
				if (LiveActor->ActorHasTag(SourceTag))
				{
					FoundActors.Add(SourceTag, TDFluidCloudLayersPrivate::PickLiveActorByName(FoundActors.FindRef(SourceTag), LiveActor));
				}
			}
		}
	}

	SourceLiveActors.Reset();
	for (const FName& SourceTag : SourceTags)
	{
		SourceLiveActors.Add(SourceTag, FoundActors.FindRef(SourceTag));
	}
}

bool ATDFluidCloudLayersActor::HasUnresolvedSources() const
{
	for (const TPair<FName, TWeakObjectPtr<ATDFluidLiveActor>>& Entry : SourceLiveActors)
	{
		if (!Entry.Value.IsValid())
		{
			return true;
		}
	}
	return false;
}

void ATDFluidCloudLayersActor::RetrySourceResolution()
{
	if (!HasUnresolvedSources() || FPlatformTime::Seconds() - LastSourceResolveSeconds < TDFluidCloudLayersPrivate::SourceRetryIntervalSeconds)
	{
		return;
	}

	ResolveSourceLiveActors();
	ApplySourceAwake();
}

void ATDFluidCloudLayersActor::BindSlotTextures(bool bForce)
{
	if (!ActiveMaterial || !Setups.IsValidIndex(ActiveSetupIndex))
	{
		return;
	}

	for (const FTDFluidCloudSlotSource& SlotSource : Setups[ActiveSetupIndex].SlotSources)
	{
		if (SlotSource.SlotIndex < 0 || SlotSource.SlotIndex >= TDFluidCloudLayersPrivate::CloudSlotCount)
		{
			continue;
		}

		const TWeakObjectPtr<ATDFluidLiveActor>* FoundActor = SourceLiveActors.Find(SlotSource.LiveActorTag);
		const ATDFluidLiveActor* LiveActor = FoundActor ? FoundActor->Get() : nullptr;
		UTexture* SourceTexture = LiveActor && LiveActor->LiveComponent ? LiveActor->LiveComponent->GetDensityPressureTexture() : nullptr;
		if (!SourceTexture)
		{
			continue;
		}

		const FName ParameterName = MakeSlotParameterName(SlotSource.SlotIndex, TDFluidCloudLayersPrivate::ResolveRoleSuffix(SlotSource.Role));
		TWeakObjectPtr<UTexture>& BoundTexture = BoundSlotTextures.FindOrAdd(ParameterName);
		if (!bForce && BoundTexture.Get() == SourceTexture)
		{
			continue;
		}

		WarnIfSlotTilesNonWrappedSource(SlotSource.SlotIndex, *LiveActor);
		ActiveMaterial->SetTextureParameterValue(ParameterName, SourceTexture);
		BoundTexture = SourceTexture;
	}
}

void ATDFluidCloudLayersActor::WarnIfSlotTilesNonWrappedSource(int32 SlotIndex, const ATDFluidLiveActor& LiveActor)
{
	if (!ActiveMaterial || !LiveActor.LiveComponent || WrapMismatchWarnedSlots.Contains(SlotIndex))
	{
		return;
	}

	const float Source = TDFluidCloudLayersPrivate::ReadSlotScalar(*ActiveMaterial, SlotIndex, TEXT("Source"));
	const FLinearColor FootprintSizeKm = ActiveMaterial->K2_GetVectorParameterValue(MakeSlotParameterName(SlotIndex, TDFluidCloudLayersPrivate::FootprintSizeSuffix));
	if (!ShouldWarnWrapMismatch(Source, FootprintSizeKm.R, LiveActor.LiveComponent->IsAreaWrapped()))
	{
		return;
	}

	WrapMismatchWarnedSlots.Add(SlotIndex);
	UE_LOG(LogTDFluidCloud, Warning, TEXT("slot %d tiles non-wrapped source %s"), SlotIndex, *LiveActor.GetName());
}

void ATDFluidCloudLayersActor::ApplySourceAwake()
{
	TSet<FName> ActiveTags;
	if (Setups.IsValidIndex(ActiveSetupIndex))
	{
		for (const FTDFluidCloudSlotSource& SlotSource : Setups[ActiveSetupIndex].SlotSources)
		{
			ActiveTags.Add(SlotSource.LiveActorTag);
		}
	}

	TSet<ATDFluidLiveActor*> AwakeActors;
	TSet<ATDFluidLiveActor*> KnownActors;
	for (const TPair<FName, TWeakObjectPtr<ATDFluidLiveActor>>& Entry : SourceLiveActors)
	{
		ATDFluidLiveActor* LiveActor = Entry.Value.Get();
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			continue;
		}
		KnownActors.Add(LiveActor);
		if (ActiveTags.Contains(Entry.Key))
		{
			AwakeActors.Add(LiveActor);
		}
	}

	for (ATDFluidLiveActor* LiveActor : KnownActors)
	{
		LiveActor->LiveComponent->SetSimulationAwake(AwakeActors.Contains(LiveActor));
	}
}

void ATDFluidCloudLayersActor::UpdateViewSampling(bool bForce)
{
	bool bIsNear = bIsUsingNearSampling;
	FVector CameraLocation = FVector::ZeroVector;
	if (TryGetFirstLocalCameraLocation(CameraLocation))
	{
		bIsNear = ResolveNearSampling(static_cast<float>(CameraLocation.Z / TDFluidCloudLayersPrivate::CentimetersPerKilometer), ActiveSlabKm, SceneSettings.NearEnterMarginKm, SceneSettings.NearExitMarginKm, bIsUsingNearSampling);
	}
	bIsUsingNearSampling = bIsNear;

	const float ForcedScale = CVarTDFluidCloudViewSampleScale.GetValueOnGameThread();
	const float AutomaticScale = bIsNear ? SceneSettings.ViewSampleCountScaleNear : SceneSettings.ViewSampleCountScaleFar;
	const float DesiredScale = FMath::Max(ForcedScale >= 0.0f ? ForcedScale : AutomaticScale, TDFluidCloudLayersPrivate::MinimumViewSampleCountScale);
	if (!bForce && FMath::IsNearlyEqual(DesiredScale, AppliedViewSampleCountScale))
	{
		return;
	}

	CloudComponent->SetViewSampleCountScale(DesiredScale);
	AppliedViewSampleCountScale = DesiredScale;
}

void ATDFluidCloudLayersActor::WarnIfSetupConsoleValueIsOutOfRange(int32 ConsoleValue)
{
	if (ConsoleValue == LastWarnedSetupConsoleValue)
	{
		return;
	}

	LastWarnedSetupConsoleValue = ConsoleValue;
	if (ConsoleValue < 0 || ConsoleValue < Setups.Num())
	{
		return;
	}

	UE_LOG(LogTDFluidCloud, Warning, TEXT("TD.FluidFX.CloudSetup %d is outside the %d setups of %s; the default setup stays active."), ConsoleValue, Setups.Num(), *GetName());
}

bool ATDFluidCloudLayersActor::TryGetFirstLocalCameraLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController || !PlayerController->IsLocalController() || !PlayerController->PlayerCameraManager)
	{
		return false;
	}

	OutLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
	return true;
}
