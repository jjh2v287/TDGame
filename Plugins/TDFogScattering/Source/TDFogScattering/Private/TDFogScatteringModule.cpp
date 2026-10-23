#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FTDFogScatteringModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};

void FTDFogScatteringModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TDFogScattering"));
	if (!Plugin.IsValid())
	{
		return;
	}

	const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
	if (!FPaths::DirectoryExists(ShaderDirectory))
	{
		return;
	}

	AddShaderSourceDirectoryMapping(TEXT("/Plugin/TDFogScattering"), ShaderDirectory);
}

IMPLEMENT_MODULE(FTDFogScatteringModule, TDFogScattering)
