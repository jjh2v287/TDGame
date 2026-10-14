#include "Framework/TDTopDownSettings.h"

#include "Materials/MaterialParameterCollection.h"

namespace
{
	const TCHAR* const TDTopDownDefaultParameterCollectionPath = TEXT("/Game/World/Materials/TopDown/Parameters/MPC_TD_TopDown.MPC_TD_TopDown");
}

UTDTopDownSettings::UTDTopDownSettings()
{
	TopDownParameterCollection = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TDTopDownDefaultParameterCollectionPath));
}
