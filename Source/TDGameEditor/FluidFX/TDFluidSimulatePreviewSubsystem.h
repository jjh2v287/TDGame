#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDFluidSimulatePreviewSubsystem.generated.h"

UCLASS()
class TDGAMEEDITOR_API UTDFluidSimulatePreviewSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override;

	static int32 EvaluatePreviewActivation(UWorld& World, const FVector& ViewLocation);
	static bool IsPreviewConsoleEnabled();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
};
