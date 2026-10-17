#include "World/Streaming/TDRealmShaftLiner.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "TDGame.h"
#include "World/Streaming/TDRealmLiftDeck.h"
#include "World/TDRealmDefinitions.h"

namespace
{
	constexpr double TDShaftLinerFaceVCm = 345.0;
	constexpr double TDShaftLinerFaceWidthCm = 1600.0;
	constexpr double TDShaftLinerSideWCm = 700.0;
	constexpr double TDShaftLinerSideBackVCm = -1100.0;
	constexpr double TDShaftLinerPanelThicknessCm = 1.0;
	constexpr double TDShaftLinerCrossbarVCm = 330.0;
	constexpr double TDShaftLinerCrossbarLengthCm = 1400.0;
	constexpr double TDShaftLinerCrossbarThicknessCm = 30.0;
	constexpr double TDShaftLinerLightVCm = 260.0;
	constexpr double TDShaftLinerLightWCm = 500.0;
	constexpr float TDShaftLinerLightIntensityCandelas = 8.8f;
	constexpr float TDShaftLinerLightRadiusCm = 1400.0f;
	constexpr float TDShaftLinerLightSourceRadiusCm = 20.0f;
	constexpr float TDShaftLinerLightVolumetricScattering = 1.5f;
	const FLinearColor TDShaftLinerLightColor(0.55f, 0.68f, 1.0f);
	const TCHAR* TDShaftLinerPanelMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* TDShaftLinerCrossbarMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* TDShaftLinerDefaultMaterialPath = TEXT("/Game/World/Materials/TopDown/Instances/MI_TD_RockLocal_ShaftLiner.MI_TD_RockLocal_ShaftLiner");

	UStaticMesh* ResolveTDShaftLinerMesh(const TSoftObjectPtr<UStaticMesh>& MeshReference, const TCHAR* FallbackMeshPath)
	{
		UStaticMesh* LoadedMesh = MeshReference.IsNull() ? nullptr : MeshReference.Get();
		return LoadedMesh ? LoadedMesh : Cast<UStaticMesh>(FSoftObjectPath(FallbackMeshPath).ResolveObject());
	}

	UMaterialInterface* ResolveTDShaftLinerMaterial(const TSoftObjectPtr<UMaterialInterface>& MaterialReference)
	{
		UMaterialInterface* LoadedMaterial = MaterialReference.IsNull() ? nullptr : MaterialReference.Get();
		if (LoadedMaterial)
		{
			return LoadedMaterial;
		}

		const TSoftObjectPtr<UMaterialInterface> DefaultMaterial{FSoftObjectPath(TDShaftLinerDefaultMaterialPath)};
		if (!FPackageName::DoesPackageExist(DefaultMaterial.ToSoftObjectPath().GetLongPackageName()))
		{
			return nullptr;
		}
		return DefaultMaterial.Get();
	}

	int32 WrapTDShaftLinerModuleIndex(int32 ModuleIndex, int32 ModuleCount)
	{
		const int32 Remainder = ModuleIndex % ModuleCount;
		return Remainder < 0 ? Remainder + ModuleCount : Remainder;
	}

	void ConfigureTDShaftLinerMeshComponent(UStaticMeshComponent& Component, USceneComponent& Parent)
	{
		Component.SetupAttachment(&Parent);
		Component.SetMobility(EComponentMobility::Movable);
		Component.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Component.SetCanEverAffectNavigation(false);
		Component.SetGenerateOverlapEvents(false);
		Component.SetCastShadow(false);
		Component.SetLightingChannels(false, true, false);
	}

	void FitTDShaftLinerMesh(UStaticMeshComponent& Component, UStaticMesh* Mesh, const FVector& CenterCm, const FRotator& Rotation, const FVector& SizeCm, UMaterialInterface* Material)
	{
		Component.SetStaticMesh(Mesh);
		if (!Mesh)
		{
			return;
		}

		const FBox MeshBounds = Mesh->GetBoundingBox();
		const FVector MeshSize = MeshBounds.GetSize().ComponentMax(FVector(1.0));
		const FVector Scale = SizeCm / MeshSize;
		Component.SetRelativeScale3D(Scale);
		Component.SetRelativeRotation(Rotation);
		Component.SetRelativeLocation(CenterCm - Rotation.RotateVector(MeshBounds.GetCenter() * Scale));
		if (!Material)
		{
			return;
		}

		for (int32 MaterialIndex = 0; MaterialIndex < Component.GetNumMaterials(); ++MaterialIndex)
		{
			Component.SetMaterial(MaterialIndex, Material);
		}
	}
}

ATDRealmShaftLiner::ATDRealmShaftLiner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetCanBeDamaged(false);

	LinerRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LinerRoot"));
	LinerRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = LinerRoot;
}

double ATDRealmShaftLiner::ComputeRingTopDepthCm(int32 RingIndex, double PhaseCm, double ModuleHeightCm, int32 ModuleCount, double RingTopDepthCm, bool bIsDescending)
{
	if (ModuleHeightCm <= 0.0 || ModuleCount <= 0)
	{
		return RingTopDepthCm;
	}

	const double LoopLengthCm = ModuleHeightCm * ModuleCount;
	const double OffsetCm = RingIndex * ModuleHeightCm + (bIsDescending ? -PhaseCm : PhaseCm);
	const double LoopCount = FMath::CeilToDouble((OffsetCm + ModuleHeightCm) / LoopLengthCm) - 1.0;
	return RingTopDepthCm + OffsetCm - LoopCount * LoopLengthCm;
}

void ATDRealmShaftLiner::GatherRequiredAssetPaths(const FTDRealmGate& Gate, TArray<FSoftObjectPath>& OutPaths)
{
	const FSoftObjectPath GateAssetPaths[] =
	{
		Gate.Lift.LinerPanelMesh.ToSoftObjectPath(),
		Gate.Lift.LinerMaterial.ToSoftObjectPath()
	};
	for (const FSoftObjectPath& GateAssetPath : GateAssetPaths)
	{
		if (!GateAssetPath.IsNull())
		{
			OutPaths.AddUnique(GateAssetPath);
		}
	}
	OutPaths.AddUnique(FSoftObjectPath(TDShaftLinerPanelMeshPath));
	OutPaths.AddUnique(FSoftObjectPath(TDShaftLinerCrossbarMeshPath));

	const FSoftObjectPath DefaultMaterialPath(TDShaftLinerDefaultMaterialPath);
	if (!FPackageName::DoesPackageExist(DefaultMaterialPath.GetLongPackageName()))
	{
		return;
	}
	OutPaths.AddUnique(DefaultMaterialPath);
}

double ATDRealmShaftLiner::GetRingTopDepthCm(int32 RingIndex) const
{
	if (!RingRoots.IsValidIndex(RingIndex) || !RingRoots[RingIndex])
	{
		return 0.0;
	}
	return -RingRoots[RingIndex]->GetRelativeLocation().Z;
}

void ATDRealmShaftLiner::InitializeLiner(const FTDRealmGate& Gate, ATDRealmLiftDeck& Deck)
{
	if (LiftDeck.IsValid())
	{
		UE_LOG(LogTDGame, Warning, TEXT("ShaftLiner '%s': already initialized, ignoring a second initialization."), *Gate.GateId.ToString());
		return;
	}

	const FTDRealmLift& Lift = Gate.Lift;
	if (Lift.RingModuleHeightCm <= 0.f || Lift.RingModuleCount <= 0)
	{
		UE_LOG(LogTDGame, Warning, TEXT("ShaftLiner '%s': ring module height %.1f cm and count %d must be positive, no rings built."), *Gate.GateId.ToString(), Lift.RingModuleHeightCm, Lift.RingModuleCount);
		return;
	}

	LiftDeck = &Deck;
	ModuleHeightCm = Lift.RingModuleHeightCm;
	ModuleCount = Lift.RingModuleCount;
	RingTopDepthCm = Lift.RingTopDepthCm;
	SetActorTransform(ATDRealmLiftDeck::MakeShaftFrameTransform(Gate));
	AddTickPrerequisiteActor(&Deck);

	const FTDRealmLiftMotion& Motion = Deck.GetMotion();
	BasePhaseCm = Motion.RingPhaseCm;
	BaseModuleShift = 0;
	bIsBaseDescending = Motion.Direction == ETDRealmLiftDirection::Down;

	UStaticMesh* PanelMesh = ResolveTDShaftLinerMesh(Lift.LinerPanelMesh, TDShaftLinerPanelMeshPath);
	UStaticMesh* CrossbarMesh = Cast<UStaticMesh>(FSoftObjectPath(TDShaftLinerCrossbarMeshPath).ResolveObject());
	UMaterialInterface* LinerMaterial = ResolveTDShaftLinerMaterial(Lift.LinerMaterial);
	for (int32 RingIndex = 0; RingIndex < ModuleCount; ++RingIndex)
	{
		BuildRing(RingIndex, PanelMesh, CrossbarMesh, LinerMaterial);
	}
	PlaceRings(Motion.RingPhaseCm);
	UE_LOG(LogTDGame, Log, TEXT("ShaftLiner '%s': %d rings of %.1f cm from depth %.1f cm, panel mesh '%s', material '%s'."),
		*Gate.GateId.ToString(), ModuleCount, ModuleHeightCm, RingTopDepthCm, PanelMesh ? *PanelMesh->GetName() : TEXT("<none>"), LinerMaterial ? *LinerMaterial->GetName() : TEXT("<mesh default>"));
}

void ATDRealmShaftLiner::BuildRing(int32 RingIndex, UStaticMesh* PanelMesh, UStaticMesh* CrossbarMesh, UMaterialInterface* Material)
{
	USceneComponent* RingRoot = NewObject<USceneComponent>(this, *FString::Printf(TEXT("Ring%d"), RingIndex));
	RingRoot->SetupAttachment(LinerRoot);
	RingRoot->SetMobility(EComponentMobility::Movable);
	RingRoot->RegisterComponent();
	AddInstanceComponent(RingRoot);
	RingRoots.Add(RingRoot);

	const double HalfModuleCm = ModuleHeightCm * 0.5;
	const double SideLengthCm = TDShaftLinerFaceVCm - TDShaftLinerSideBackVCm;
	const double SideCenterVCm = (TDShaftLinerFaceVCm + TDShaftLinerSideBackVCm) * 0.5;
	auto AddMeshPart = [&](const TCHAR* PartName, UStaticMesh* Mesh, const FVector& CenterCm, const FRotator& Rotation, const FVector& SizeCm)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Ring%d%s"), RingIndex, PartName));
		ConfigureTDShaftLinerMeshComponent(*Part, *RingRoot);
		FitTDShaftLinerMesh(*Part, Mesh, CenterCm, Rotation, SizeCm, Material);
		Part->RegisterComponent();
		AddInstanceComponent(Part);
	};

	AddMeshPart(TEXT("Face"), PanelMesh, FVector(TDShaftLinerFaceVCm, 0.0, -HalfModuleCm), FRotator(90.0, 0.0, 0.0), FVector(ModuleHeightCm, TDShaftLinerFaceWidthCm, TDShaftLinerPanelThicknessCm));
	AddMeshPart(TEXT("SideLeft"), PanelMesh, FVector(SideCenterVCm, -TDShaftLinerSideWCm, -HalfModuleCm), FRotator(90.0, -90.0, 0.0), FVector(ModuleHeightCm, SideLengthCm, TDShaftLinerPanelThicknessCm));
	AddMeshPart(TEXT("SideRight"), PanelMesh, FVector(SideCenterVCm, TDShaftLinerSideWCm, -HalfModuleCm), FRotator(90.0, 90.0, 0.0), FVector(ModuleHeightCm, SideLengthCm, TDShaftLinerPanelThicknessCm));
	AddMeshPart(TEXT("Crossbar"), CrossbarMesh, FVector(TDShaftLinerCrossbarVCm, 0.0, -TDShaftLinerCrossbarThicknessCm * 0.5), FRotator::ZeroRotator,
		FVector(TDShaftLinerCrossbarThicknessCm, TDShaftLinerCrossbarLengthCm, TDShaftLinerCrossbarThicknessCm));

	UPointLightComponent* Light = NewObject<UPointLightComponent>(this, *FString::Printf(TEXT("Ring%dLight"), RingIndex));
	Light->SetupAttachment(RingRoot);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetRelativeLocation(FVector(TDShaftLinerLightVCm, RingIndex % 2 == 0 ? TDShaftLinerLightWCm : -TDShaftLinerLightWCm, -HalfModuleCm));
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(TDShaftLinerLightIntensityCandelas);
	Light->SetAttenuationRadius(TDShaftLinerLightRadiusCm);
	Light->SetSourceRadius(TDShaftLinerLightSourceRadiusCm);
	Light->SetLightColor(TDShaftLinerLightColor);
	Light->SetCastShadows(false);
	Light->SetVolumetricScatteringIntensity(TDShaftLinerLightVolumetricScattering);
	Light->SetLightingChannels(true, true, false);
	Light->RegisterComponent();
	AddInstanceComponent(Light);
}

void ATDRealmShaftLiner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ATDRealmLiftDeck* Deck = LiftDeck.Get();
	if (!Deck)
	{
		return;
	}

	const FTDRealmLiftMotion& Motion = Deck->GetMotion();
	const bool bIsDescending = Motion.Direction == ETDRealmLiftDirection::Down;
	if (bIsDescending != bIsBaseDescending)
	{
		RebaseRings(Motion.RingPhaseCm, bIsDescending);
	}
	if (bHasPlacedRings && Motion.RingPhaseCm == AppliedPhaseCm)
	{
		return;
	}
	PlaceRings(Motion.RingPhaseCm);
}

void ATDRealmShaftLiner::RebaseRings(double DeckRingPhaseCm, bool bIsDescending)
{
	const int32 MovedModules = FMath::RoundToInt((DeckRingPhaseCm - BasePhaseCm) / ModuleHeightCm);
	BaseModuleShift = WrapTDShaftLinerModuleIndex(BaseModuleShift + (bIsBaseDescending ? -MovedModules : MovedModules), ModuleCount);
	BasePhaseCm = DeckRingPhaseCm;
	bIsBaseDescending = bIsDescending;
}

void ATDRealmShaftLiner::PlaceRings(double DeckRingPhaseCm)
{
	const double PhaseSinceBaseCm = DeckRingPhaseCm - BasePhaseCm;
	for (int32 RingIndex = 0; RingIndex < RingRoots.Num(); ++RingIndex)
	{
		const int32 ModuleIndex = WrapTDShaftLinerModuleIndex(RingIndex + BaseModuleShift, ModuleCount);
		const double TopDepthCm = ComputeRingTopDepthCm(ModuleIndex, PhaseSinceBaseCm, ModuleHeightCm, ModuleCount, RingTopDepthCm, bIsBaseDescending);
		RingRoots[RingIndex]->SetRelativeLocation(FVector(0.0, 0.0, -TopDepthCm));
	}
	AppliedPhaseCm = DeckRingPhaseCm;
	bHasPlacedRings = true;
}
