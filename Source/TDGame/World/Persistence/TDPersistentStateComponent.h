#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TDPersistentStateComponent.generated.h"

class UTDWorldStateSubsystem;

UCLASS(ClassGroup = (TD), meta = (BlueprintSpawnableComponent))
class TDGAME_API UTDPersistentStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame, VisibleAnywhere, BlueprintReadOnly, Category = "TD|Persistence")
	FGuid StableId;

	UTDPersistentStateComponent();

	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool CaptureOwnerState();
	bool ApplyStoredStateToOwner();

private:
	void ResolveStableId();
	UTDWorldStateSubsystem* FindWorldStateSubsystem() const;
};
