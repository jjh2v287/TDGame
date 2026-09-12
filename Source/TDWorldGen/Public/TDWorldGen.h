#pragma once

#include "Modules/ModuleManager.h"

TDWORLDGEN_API DECLARE_LOG_CATEGORY_EXTERN(LogTDWorldGen, Log, All);

class FTDWorldGenModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
