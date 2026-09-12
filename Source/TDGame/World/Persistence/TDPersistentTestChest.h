#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Persistence/TDPersistentActor.h"
#include "TDPersistentTestChest.generated.h"

class UStaticMeshComponent;
class UTDPersistentStateComponent;

UCLASS()
class TDGAME_API ATDPersistentTestChest : public AActor, public ITDPersistentActor
{
	GENERATED_BODY()

public:
	ATDPersistentTestChest();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|Chest")
	bool bIsOpened = false;

	UFUNCTION(BlueprintCallable, Exec, Category = "TD|Chest")
	void Open();

	virtual void WriteState(FTDActorStateRecord& OutRecord) const override;
	virtual void ReadState(const FTDActorStateRecord& Record) override;

	UTDPersistentStateComponent* GetPersistentStateComponent() const { return PersistentState; }

private:
	static const FName OpenedStateKey;

	UPROPERTY(VisibleAnywhere, Category = "TD|Chest")
	TObjectPtr<UStaticMeshComponent> ChestMesh;

	UPROPERTY(VisibleAnywhere, Category = "TD|Chest")
	TObjectPtr<UTDPersistentStateComponent> PersistentState;
};
