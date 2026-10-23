#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FTDFogScatteringViewExtension : public FSceneViewExtensionBase
{
public:
	explicit FTDFogScatteringViewExtension(const FAutoRegister& AutoRegister);

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
};
