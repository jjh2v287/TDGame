#include "Animation/TDAnimationAuthoringSubsystem.h"

#include "Animation/TDAnimationAuthoringCommon.h"
#include "Animation/TDAnimationAuthoringTools.h"
#include "Animation/TDControlRigTools.h"
#include "Animation/TDSequencerAnimationTools.h"
#include "Subsystems/SubsystemCollection.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

DEFINE_LOG_CATEGORY(LogTDAnimAuthoring);

namespace
{
	// RegisterToolsetClass는 레지스트리가 준비되지 않았으면 조용히 아무 일도 하지 않는다.
	// 등록 여부를 확인해 남겨야 도구가 보이지 않을 때 원인을 추적할 수 있다.
	void RegisterAuthoringToolset(TSubclassOf<UToolsetDefinition> ToolsetClass)
	{
		UToolsetRegistry::RegisterToolsetClass(ToolsetClass);
		if (UToolsetRegistry::IsToolsetClassRegistered(ToolsetClass))
		{
			UE_LOG(LogTDAnimAuthoring, Log, TEXT("Registered animation authoring toolset: %s"), *GetNameSafe(ToolsetClass));
		}
		else
		{
			UE_LOG(LogTDAnimAuthoring, Error, TEXT("Failed to register animation authoring toolset: %s. The toolset registry reported it as unregistered, so its tools will not appear over MCP."), *GetNameSafe(ToolsetClass));
		}
	}
}

void UTDAnimationAuthoringSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UToolsetRegistrySubsystem>();
	if (!UToolsetRegistry::IsAvailable())
	{
		UE_LOG(LogTDAnimAuthoring, Error, TEXT("The toolset registry is unavailable; animation authoring tools will not be exposed over MCP."));
		return;
	}
	RegisterAuthoringToolset(UTDAnimationAuthoringTools::StaticClass());
	RegisterAuthoringToolset(UTDControlRigTools::StaticClass());
	RegisterAuthoringToolset(UTDSequencerAnimationTools::StaticClass());
}

void UTDAnimationAuthoringSubsystem::Deinitialize()
{
	UToolsetRegistry::UnregisterToolsetClass(UTDSequencerAnimationTools::StaticClass());
	UToolsetRegistry::UnregisterToolsetClass(UTDControlRigTools::StaticClass());
	UToolsetRegistry::UnregisterToolsetClass(UTDAnimationAuthoringTools::StaticClass());
	Super::Deinitialize();
}
