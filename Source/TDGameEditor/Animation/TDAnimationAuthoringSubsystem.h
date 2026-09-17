#pragma once

#include "EditorSubsystem.h"
#include "TDAnimationAuthoringSubsystem.generated.h"

UCLASS()
class UTDAnimationAuthoringSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
};
