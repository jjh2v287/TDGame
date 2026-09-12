#include "TDGameEditor.h"

#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "TDWorldGenEditorBridge.h"
#include "WorldGen/TDWorldGenEditorBridgeImpl.h"

#define LOCTEXT_NAMESPACE "TDGameEditor"

void FTDGameEditorModule::StartupModule()
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions Options;
	Options.bShowFilters = true;
	Options.bShowPages = true;
	Options.bAllowClear = true;
	MessageLogModule.RegisterLogListing(TEXT("TDWorldGen"), LOCTEXT("TDWorldGenLogLabel", "TD World Generation"), Options);
	FTDWorldGenEditorBridge::Set(MakeShared<FTDWorldGenEditorBridgeImpl>());
}

void FTDGameEditorModule::ShutdownModule()
{
	FTDWorldGenEditorBridge::Set(nullptr);
	if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
	{
		FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog");
		MessageLogModule.UnregisterLogListing(TEXT("TDWorldGen"));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTDGameEditorModule, TDGameEditor)
