#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"

class AActor;
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class UWorld;

struct FTDFluidTerrainCapture
{
	static const FName TerrainActorTag;

	static bool IsLandscapeSurfaceComponent(const UPrimitiveComponent& Component);
	static bool ShouldRenderInMainRenderer(ETDFluidHeightCaptureContent Content, bool bHasGameViewport);
	static void ApplyTerrainOnlyList(USceneCaptureComponent2D& Capture, UWorld& World);
	static void HideOwnerAndPawns(USceneCaptureComponent2D& Capture, const AActor* Owner, bool bIgnoresPawns, UWorld& World);
};
