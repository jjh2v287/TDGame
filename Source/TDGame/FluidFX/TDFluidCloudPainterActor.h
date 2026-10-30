#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "FluidFX/TDFluidCloudTypes.h"
#include "TDFluidCloudPainterActor.generated.h"

class APlayerController;
class ATDFluidCloudLayersActor;
class ATDFluidLiveActor;

struct FTDFluidCloudPaintFootprint
{
	FVector2D CenterKm = FVector2D::ZeroVector;
	FVector2D SizeKm = FVector2D::ZeroVector;
	float PatternScaleKm = 0.0f;
	float LayerAltitudeCm = 0.0f;
};

UCLASS()
class TDGAME_API ATDFluidCloudPainterActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidCloudPainterActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudPainter")
	TArray<FName> TargetLiveActorTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudPainter")
	FKey PaintKey = EKeys::MiddleMouseButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudPainter")
	FKey EraseModifierKey = EKeys::LeftShift;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudPainter")
	bool bIsPainterEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudPainter")
	bool bShowsMouseCursor = true;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	static bool IntersectRayWithAltitude(const FVector& RayOrigin, const FVector& RayDirection, double AltitudeCm, FVector& OutPoint);
	static bool MapDisplayPointToSimulation(const FVector2D& DisplayPointCm, const FTDFluidCloudPaintFootprint& Footprint, const FVector2D& SimCenterPlaneCm, const FVector2D& SimExtentCm, FVector2D& OutPlanePoint, FVector2D& OutUv);
	static double ComputeDisplayToSimulationScale(const FTDFluidCloudPaintFootprint& Footprint, const FVector2D& SimExtentCm);
	static float ComputeSimulationBrushRadiusCm(float BrushRadiusKm, double DisplayToSimulationScale);
	static int32 ComputePaintStampCount(double SegmentLengthCm, float RadiusCm, float SpacingRatio, int32 MaxStampsPerTick, bool bCrossesSeam);
	static bool IsSeamCrossing(const FVector2D& PreviousUv, const FVector2D& Uv);
	static bool ResolvePainterEnabled(int32 CVarValue, bool bActorValue);

	void QueueScriptedStroke(const FVector2D& StartKm, const FVector2D& EndKm, float Seconds, bool bErase);
	void PaintAtDisplayPoint(ATDFluidLiveActor& LiveActor, const FTDFluidCloudPaintFootprint& Footprint, const FVector& DisplayPointCm, bool bErase, float DeltaSeconds);
	void EndStroke();

	bool HasScriptedStroke() const { return bHasScriptedStroke; }

private:
	void HandlePresetTuningReloaded(int32 TableGeneration);
	void ReadSceneSettings();
	void ShowMouseCursor() const;
	void FinishScriptedStrokeIfElapsed();
	bool ResolvePaintTarget(ATDFluidLiveActor*& OutLiveActor, FTDFluidCloudPaintFootprint& OutFootprint);
	ATDFluidCloudLayersActor* ResolveCloudActor();
	ATDFluidLiveActor* ResolveLiveActor(FName LiveActorTag);
	bool ResolveDisplayPoint(const APlayerController* PlayerController, const FTDFluidCloudPaintFootprint& Footprint, FVector& OutDisplayPointCm) const;
	bool TryGetCursorDisplayPoint(const APlayerController& PlayerController, float LayerAltitudeCm, FVector& OutDisplayPointCm) const;
	APlayerController* GetFirstLocalPlayerController() const;

	FTDFluidCloudSceneSettings SceneSettings;
	TWeakObjectPtr<ATDFluidCloudLayersActor> CachedCloudActor;
	TWeakObjectPtr<ATDFluidLiveActor> CachedLiveActor;
	FName CachedLiveActorTag;
	FDelegateHandle PresetTuningReloadedHandle;
	FVector2D ScriptedStartKm = FVector2D::ZeroVector;
	FVector2D ScriptedEndKm = FVector2D::ZeroVector;
	FVector2D PreviousPlanePoint = FVector2D::ZeroVector;
	FVector2D PreviousUv = FVector2D::ZeroVector;
	double LastTargetSearchSeconds = -1.0e9;
	float ScriptedDurationSeconds = 0.0f;
	float ScriptedElapsedSeconds = 0.0f;
	bool bHasScriptedStroke = false;
	bool bIsScriptedErase = false;
	bool bHasPreviousPoint = false;
};
