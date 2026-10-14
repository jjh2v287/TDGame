#include "Framework/TDTopDownOcclusionSubsystem.h"

#include "Engine/World.h"
#include "Framework/TDTopDownSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/PackageName.h"
#include "TDGame.h"

namespace TDTopDownOcclusionCVars
{
	static TAutoConsoleVariable<int32> CVarOcclusionCutout(
		TEXT("td.TopDown.OcclusionCutout"),
		1,
		TEXT("Top down occlusion cutout around the local player pawn. 0 = Off, 1 = On."));
}

namespace TDTopDownOcclusionParameterNames
{
	const FName Focus(TEXT("OcclusionFocus"));
	const FName Radius(TEXT("OcclusionRadius"));
	const FName Softness(TEXT("OcclusionSoftness"));
	const FName KeepBase(TEXT("OcclusionKeepBase"));
	const FName MaxCut(TEXT("OcclusionMaxCut"));
	const FName FeetZ(TEXT("OcclusionFeetZ"));
}

namespace
{
	UMaterialParameterCollection* LoadTopDownParameterCollection(const UTDTopDownSettings& Settings)
	{
		if (Settings.TopDownParameterCollection.IsNull())
		{
			return nullptr;
		}
		if (!FPackageName::DoesPackageExist(Settings.TopDownParameterCollection.GetLongPackageName()))
		{
			return nullptr;
		}
		return Settings.TopDownParameterCollection.LoadSynchronous();
	}

	const APawn* FindTopDownLocalPlayerPawn(const UWorld* World)
	{
		const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		return PlayerController ? PlayerController->GetPawn() : nullptr;
	}

	bool IsTopDownOcclusionCutoutEnabled(const UTDTopDownSettings& Settings, const APawn* Pawn)
	{
		if (!Pawn || !Settings.bEnableOcclusionCutout)
		{
			return false;
		}
		return TDTopDownOcclusionCVars::CVarOcclusionCutout.GetValueOnGameThread() != 0;
	}

	FLinearColor MakeTopDownFocusValue(const FVector& Location, float Activation)
	{
		return FLinearColor(static_cast<float>(Location.X), static_cast<float>(Location.Y), static_cast<float>(Location.Z), Activation);
	}
}

bool UTDTopDownOcclusionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UMaterialParameterCollection* UTDTopDownOcclusionSubsystem::GetParameterCollection() const
{
	return ParameterCollection.Get();
}

void UTDTopDownOcclusionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UTDTopDownSettings& Settings = *GetDefault<UTDTopDownSettings>();
	ParameterCollection = LoadTopDownParameterCollection(Settings);
	if (ParameterCollection)
	{
		return;
	}

	UE_LOG(LogTDGame, Warning, TEXT("TopDownOcclusion: parameter collection '%s' is missing or failed to load; the occlusion cutout stays off."), *Settings.TopDownParameterCollection.ToString());
}

void UTDTopDownOcclusionSubsystem::Deinitialize()
{
	ParameterCollection = nullptr;
	Super::Deinitialize();
}

bool UTDTopDownOcclusionSubsystem::IsTickable() const
{
	return ParameterCollection != nullptr;
}

TStatId UTDTopDownOcclusionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDTopDownOcclusionSubsystem, STATGROUP_Tickables);
}

void UTDTopDownOcclusionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UWorld* World = GetWorld();
	UMaterialParameterCollectionInstance* Instance = World ? World->GetParameterCollectionInstance(ParameterCollection.Get()) : nullptr;
	if (!Instance)
	{
		return;
	}

	const UTDTopDownSettings& Settings = *GetDefault<UTDTopDownSettings>();
	const APawn* Pawn = FindTopDownLocalPlayerPawn(World);
	if (!IsTopDownOcclusionCutoutEnabled(Settings, Pawn))
	{
		ApplyDisabledFocus(*Instance);
		return;
	}

	ApplyShape(*Instance, Settings);
	ApplyFocus(*Instance, Settings, *Pawn);
}

void UTDTopDownOcclusionSubsystem::ApplyShape(UMaterialParameterCollectionInstance& Instance, const UTDTopDownSettings& Settings)
{
	FTDOcclusionShape Shape;
	Shape.RadiusCm = Settings.OcclusionRadiusCm;
	Shape.SoftnessCm = Settings.OcclusionSoftnessCm;
	Shape.KeepBaseCm = Settings.OcclusionKeepBaseCm;
	Shape.MaxCut = Settings.OcclusionMaxCut;
	if (bHasAppliedShape && Shape == AppliedShape)
	{
		return;
	}

	Instance.SetScalarParameterValue(TDTopDownOcclusionParameterNames::Radius, Shape.RadiusCm);
	Instance.SetScalarParameterValue(TDTopDownOcclusionParameterNames::Softness, Shape.SoftnessCm);
	Instance.SetScalarParameterValue(TDTopDownOcclusionParameterNames::KeepBase, Shape.KeepBaseCm);
	Instance.SetScalarParameterValue(TDTopDownOcclusionParameterNames::MaxCut, Shape.MaxCut);
	AppliedShape = Shape;
	bHasAppliedShape = true;
}

void UTDTopDownOcclusionSubsystem::ApplyFocus(UMaterialParameterCollectionInstance& Instance, const UTDTopDownSettings& Settings, const APawn& Pawn)
{
	const FVector PawnLocation = Pawn.GetActorLocation();
	const FVector FocusLocation = PawnLocation + FVector(0.0, 0.0, Settings.OcclusionFocusHeightCm);
	const float FeetZ = static_cast<float>(PawnLocation.Z - Pawn.GetSimpleCollisionHalfHeight());

	Instance.SetVectorParameterValue(TDTopDownOcclusionParameterNames::Focus, MakeTopDownFocusValue(FocusLocation, 1.0f));
	Instance.SetScalarParameterValue(TDTopDownOcclusionParameterNames::FeetZ, FeetZ);
	bHasAppliedDisabledFocus = false;
}

void UTDTopDownOcclusionSubsystem::ApplyDisabledFocus(UMaterialParameterCollectionInstance& Instance)
{
	if (bHasAppliedDisabledFocus)
	{
		return;
	}

	Instance.SetVectorParameterValue(TDTopDownOcclusionParameterNames::Focus, MakeTopDownFocusValue(FVector::ZeroVector, 0.0f));
	bHasAppliedDisabledFocus = true;
}
