#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDSyncLoadWarningSubsystem.generated.h"

UCLASS()
class TDGAME_API UTDSyncLoadWarningSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	void HandleSyncLoadPackage(const FString& PackageName);

	FDelegateHandle SyncLoadDelegateHandle;
	TSet<FString> ReportedPackageNames;
};
