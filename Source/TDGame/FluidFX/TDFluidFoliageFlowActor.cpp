#include "FluidFX/TDFluidFoliageFlowActor.h"
#include "Components/SceneComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidFoliage, Log, All);

static TAutoConsoleVariable<int32> CVarTDFluidFoliageFlow(
	TEXT("TD.FluidFX.FoliageFlow"),
	-1,
	TEXT("Foliage flow copy: -1 follows the actor setting, 0 forces it off, 1 forces it on."),
	ECVF_Default);

namespace TDFluidFoliageFlowActorPrivate
{
	const FLinearColor FlowClearColor(0.5f, 0.5f, 0.0f, 1.0f);
	const FName CenterParameterName(TEXT("SimCenterWorld"));
	const FName ExtentsParameterName(TEXT("SimExtentsCm"));
	const FName StateParameterName(TEXT("FoliageFlowState"));
	const FName SpeedReferenceParameterName(TEXT("VelocitySpeedReferenceCmPerSec"));
}

ATDFluidFoliageFlowActor::ATDFluidFoliageFlowActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
}

FTDFluidFoliageWindowVectors ATDFluidFoliageFlowActor::MakeWindowVectors(const FVector2D& Center, const FVector2D& Extent, float BorderFadeRatio, bool bIsActive, float SpeedReferenceCmPerSec)
{
	FTDFluidFoliageWindowVectors Vectors;
	Vectors.Center = FLinearColor(Center.X, Center.Y, 0.0f, 1.0f);
	Vectors.Extents = FLinearColor(Extent.X, Extent.Y, BorderFadeRatio, 0.0f);
	Vectors.State = FLinearColor(bIsActive ? 1.0f : 0.0f, SpeedReferenceCmPerSec, 0.0f, 0.0f);
	return Vectors;
}

int32 ATDFluidFoliageFlowActor::SelectSourceIndex(const TArray<FTDFluidFoliageSourceCandidate>& Candidates)
{
	int32 BestIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FTDFluidFoliageSourceCandidate& Candidate = Candidates[Index];
		if (!Candidate.bIsAwake || !Candidate.bIsInitialized)
		{
			continue;
		}
		if (BestIndex == INDEX_NONE || Candidate.Name.Compare(Candidates[BestIndex].Name) < 0)
		{
			BestIndex = Index;
		}
	}
	return BestIndex;
}

bool ATDFluidFoliageFlowActor::ResolveFlowEnabled(int32 ConsoleValue, bool bActorWritesFlow)
{
	if (ConsoleValue < 0)
	{
		return bActorWritesFlow;
	}
	return ConsoleValue > 0;
}

void ATDFluidFoliageFlowActor::BeginPlay()
{
	Super::BeginPlay();

	const FSoftObjectPath AssetPaths[] = {FlowRenderTarget.ToSoftObjectPath(), FlowCopyMaterial.ToSoftObjectPath(), FoliageWindowCollection.ToSoftObjectPath()};
	TArray<FSoftObjectPath> AssetsToLoad;
	for (const FSoftObjectPath& AssetPath : AssetPaths)
	{
		if (!AssetPath.IsNull())
		{
			AssetsToLoad.Add(AssetPath);
		}
	}
	if (AssetsToLoad.Num() != UE_ARRAY_COUNT(AssetPaths))
	{
		UE_LOG(LogTDFluidFoliage, Warning, TEXT("%s: flow render target, copy material and window collection must all be set."), *GetName());
		return;
	}

	AssetLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(this, &ATDFluidFoliageFlowActor::HandleAssetsLoaded));
}

void ATDFluidFoliageFlowActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AssetLoadHandle.IsValid())
	{
		AssetLoadHandle->CancelHandle();
		AssetLoadHandle.Reset();
	}
	if (bAreAssetsReady)
	{
		WriteInactiveWindow();
		if (UTextureRenderTarget2D* RenderTarget = FlowRenderTarget.Get())
		{
			UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, TDFluidFoliageFlowActorPrivate::FlowClearColor);
		}
	}
	bAreAssetsReady = false;
	bIsWritingFlow = false;
	SourceLiveActor.Reset();
	FlowCopyMaterialInstance = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ATDFluidFoliageFlowActor::HandleAssetsLoaded()
{
	UTextureRenderTarget2D* RenderTarget = FlowRenderTarget.Get();
	if (!RenderTarget || !FlowCopyMaterial.Get() || !FoliageWindowCollection.Get())
	{
		UE_LOG(LogTDFluidFoliage, Warning, TEXT("%s: flow assets failed to load."), *GetName());
		return;
	}

	UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, TDFluidFoliageFlowActorPrivate::FlowClearColor);
	FlowCopyMaterialInstance = CreateFlowCopyMaterialInstance();
	bAreAssetsReady = FlowCopyMaterialInstance != nullptr;
	if (!bAreAssetsReady)
	{
		return;
	}

	WriteInactiveWindow();
	RefreshSourceLiveActor();
}

UMaterialInstanceDynamic* ATDFluidFoliageFlowActor::CreateFlowCopyMaterialInstance()
{
	UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(FlowCopyMaterial.Get(), this);
	if (!MaterialInstance)
	{
		return nullptr;
	}

	UTDFluidPresetTuningSubsystem::ApplyMaterialLook(*MaterialInstance);
	MaterialInstance->SetScalarParameterValue(TDFluidFoliageFlowActorPrivate::SpeedReferenceParameterName, FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec());
	return MaterialInstance;
}

void ATDFluidFoliageFlowActor::RefreshSourceLiveActor()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<ATDFluidLiveActor*> TaggedActors;
	TArray<FTDFluidFoliageSourceCandidate> Candidates;
	for (TActorIterator<ATDFluidLiveActor> Iterator(World); Iterator; ++Iterator)
	{
		ATDFluidLiveActor* LiveActor = *Iterator;
		UTDFluidLiveComponent* LiveComponent = LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
		if (!LiveComponent || !LiveActor->ActorHasTag(SourceLiveActorTag))
		{
			continue;
		}

		FTDFluidFoliageSourceCandidate Candidate;
		Candidate.Name = LiveActor->GetFName();
		Candidate.bIsAwake = LiveComponent->IsSimulationAwake();
		Candidate.bIsInitialized = LiveComponent->IsSimulationInitialized();
		Candidates.Add(Candidate);
		TaggedActors.Add(LiveActor);
	}

	const int32 SelectedIndex = SelectSourceIndex(Candidates);
	BindSourceLiveActor(TaggedActors.IsValidIndex(SelectedIndex) ? TaggedActors[SelectedIndex] : nullptr);
}

void ATDFluidFoliageFlowActor::BindSourceLiveActor(ATDFluidLiveActor* NewSource)
{
	if (!NewSource || !NewSource->LiveComponent || SourceLiveActor.Get() == NewSource)
	{
		return;
	}

	if (SourceLiveActor.IsValid())
	{
		UMaterialInstanceDynamic* ReplacementInstance = CreateFlowCopyMaterialInstance();
		if (!ReplacementInstance)
		{
			return;
		}
		FlowCopyMaterialInstance = ReplacementInstance;
	}

	SourceLiveActor = NewSource;
	NewSource->LiveComponent->RegisterExternalMaterialTarget(FlowCopyMaterialInstance);
}

UTDFluidLiveComponent* ATDFluidFoliageFlowActor::FindReadySourceComponent() const
{
	const ATDFluidLiveActor* Source = SourceLiveActor.Get();
	UTDFluidLiveComponent* SourceComponent = Source ? Source->LiveComponent.Get() : nullptr;
	if (!SourceComponent || !SourceComponent->IsSimulationAwake() || !SourceComponent->IsSimulationInitialized())
	{
		return nullptr;
	}
	return SourceComponent;
}

void ATDFluidFoliageFlowActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAreAssetsReady)
	{
		return;
	}

	UTDFluidLiveComponent* SourceComponent = FindReadySourceComponent();
	if (!SourceComponent)
	{
		RefreshSourceLiveActor();
		SourceComponent = FindReadySourceComponent();
	}

	const bool bShouldWrite = SourceComponent && ResolveFlowEnabled(CVarTDFluidFoliageFlow.GetValueOnGameThread(), bWritesFlow);
	if (!bShouldWrite)
	{
		if (bIsWritingFlow)
		{
			WriteInactiveWindow();
			bIsWritingFlow = false;
		}
		return;
	}

	const float SpeedReferenceCmPerSec = FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec();
	FlowCopyMaterialInstance->SetScalarParameterValue(TDFluidFoliageFlowActorPrivate::SpeedReferenceParameterName, SpeedReferenceCmPerSec);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, FlowRenderTarget.Get(), FlowCopyMaterialInstance);
	WriteWindowVectors(MakeWindowVectors(
		SourceComponent->GetSimulationCenterWorld(),
		SourceComponent->GetSimulationExtentCm(),
		SourceComponent->SimulationConfig.BorderFadeRatio,
		true,
		SpeedReferenceCmPerSec));
	bIsWritingFlow = true;
}

void ATDFluidFoliageFlowActor::WriteWindowVectors(const FTDFluidFoliageWindowVectors& Vectors) const
{
	UWorld* World = GetWorld();
	UMaterialParameterCollection* Collection = FoliageWindowCollection.Get();
	UMaterialParameterCollectionInstance* CollectionInstance = World && Collection ? World->GetParameterCollectionInstance(Collection) : nullptr;
	if (!CollectionInstance)
	{
		return;
	}

	CollectionInstance->SetVectorParameterValue(TDFluidFoliageFlowActorPrivate::CenterParameterName, Vectors.Center);
	CollectionInstance->SetVectorParameterValue(TDFluidFoliageFlowActorPrivate::ExtentsParameterName, Vectors.Extents);
	CollectionInstance->SetVectorParameterValue(TDFluidFoliageFlowActorPrivate::StateParameterName, Vectors.State);
}

void ATDFluidFoliageFlowActor::WriteInactiveWindow() const
{
	WriteWindowVectors(MakeWindowVectors(FVector2D::ZeroVector, FVector2D::ZeroVector, 0.0f, false, FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec()));
}
