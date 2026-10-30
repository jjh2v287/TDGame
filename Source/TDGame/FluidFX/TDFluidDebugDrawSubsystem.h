#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDFluidDebugDrawSubsystem.generated.h"

class ATDFluidLiveActor;

UCLASS()
class TDGAME_API UTDFluidDebugDrawSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	FString BuildDebugSnapshotJson() const;

	static int32 GetDebugFlags();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TArray<ATDFluidLiveActor*> CollectDebugActors() const;
	void DrawActorDebug(const ATDFluidLiveActor& LiveActor, int32 DebugFlags) const;
};
