#include "Framework/TDSyncLoadWarningSubsystem.h"

#include "Engine/World.h"
#include "Misc/CoreDelegates.h"
#include "TDGame.h"

bool UTDSyncLoadWarningSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !UE_BUILD_SHIPPING && Super::ShouldCreateSubsystem(Outer);
}

void UTDSyncLoadWarningSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	SyncLoadDelegateHandle = FCoreDelegates::OnSyncLoadPackage.AddUObject(this, &ThisClass::HandleSyncLoadPackage);
}

void UTDSyncLoadWarningSubsystem::Deinitialize()
{
	FCoreDelegates::OnSyncLoadPackage.Remove(SyncLoadDelegateHandle);
	Super::Deinitialize();
}

void UTDSyncLoadWarningSubsystem::HandleSyncLoadPackage(const FString& PackageName)
{
	if (GIsAutomationTesting)
	{
		return;
	}

	if (ReportedPackageNames.Contains(PackageName))
	{
		return;
	}

	ReportedPackageNames.Add(PackageName);
	UE_LOG(LogTDGame, Warning, TEXT("SyncLoad: package '%s' was loaded synchronously during gameplay in world '%s'."), *PackageName, *GetWorld()->GetName());
}
