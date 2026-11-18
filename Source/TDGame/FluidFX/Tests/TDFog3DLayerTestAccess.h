#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFog3DLayer.h"

#if WITH_DEV_AUTOMATION_TESTS

struct FTDFog3DLayerTestAccess
{
	static void CommitWithoutRendering(FTDFog3DLayer& Layer, const FTDFog3DStepRequest& Request)
	{
		Layer.PackAndCommitStep(Request);
	}

	static void AddRenderRejection(FTDFog3DLayer& Layer)
	{
		Layer.EnsureRenderSlot();
		Layer.RenderSlot->RejectedStepCount.fetch_add(1);
	}

	static bool IsZoneReentryPending(const FTDFog3DLayer& Layer)
	{
		return Layer.bZoneReentryPending;
	}

	static bool IsVolumetricFogReenablePending(const FTDFog3DLayer& Layer)
	{
		return Layer.bVolumetricFogReenablePending;
	}
};

#endif
