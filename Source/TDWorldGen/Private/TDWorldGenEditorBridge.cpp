#include "TDWorldGenEditorBridge.h"

namespace
{
	TSharedPtr<ITDWorldGenEditorBridge> GTDWorldGenEditorBridge;
}

void FTDWorldGenEditorBridge::Set(TSharedPtr<ITDWorldGenEditorBridge> InBridge)
{
	GTDWorldGenEditorBridge = InBridge;
}

ITDWorldGenEditorBridge* FTDWorldGenEditorBridge::Get()
{
	return GTDWorldGenEditorBridge.Get();
}
