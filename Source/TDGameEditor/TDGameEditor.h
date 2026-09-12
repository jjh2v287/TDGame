#pragma once

#include "Modules/ModuleManager.h"

class FTDGameEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
