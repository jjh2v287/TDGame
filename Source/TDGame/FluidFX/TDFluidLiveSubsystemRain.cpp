#include "FluidFX/TDFluidLiveSubsystem.h"

#include "FluidFX/TDFluidRainImpacts.h"
#include "HAL/IConsoleManager.h"

namespace TDFluidLiveSubsystemRainPrivate
{
	TAutoConsoleVariable<float> CVarTDFluidRainIntensity(
		TEXT("TD.FluidFX.Rain.Intensity"),
		-1.0f,
		TEXT("Rain intensity override for measurements: -1 uses the subsystem value, 0 to 1 replaces it."),
		ECVF_Default);
}

void UTDFluidLiveSubsystem::SetRainIntensity(float InRainIntensity)
{
	RainIntensity = FTDFluidRainImpacts::SanitizeIntensity(InRainIntensity);
}

float UTDFluidLiveSubsystem::GetEffectiveRainIntensity() const
{
	const float ConsoleIntensity = TDFluidLiveSubsystemRainPrivate::CVarTDFluidRainIntensity.GetValueOnGameThread();
	if (ConsoleIntensity >= 0.0f)
	{
		return FTDFluidRainImpacts::SanitizeIntensity(ConsoleIntensity);
	}
	return RainIntensity;
}
