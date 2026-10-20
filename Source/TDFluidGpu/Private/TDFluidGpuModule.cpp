#include "TDFluidGpuModule.h"

#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

void FTDFluidGpuModule::StartupModule()
{
	const FString ShaderDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("TDFluidGpu"), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/TDFluidGpu"), ShaderDirectory);
}

IMPLEMENT_MODULE(FTDFluidGpuModule, TDFluidGpu)
