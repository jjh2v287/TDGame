#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDTexturePrestreamSubsystem.generated.h"

class APlayerController;

UCLASS()
class TDGAME_API UTDTexturePrestreamSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category="Streaming", meta=(WorldContext="WorldContextObject"))
	static void RequestPrestreamAtPawnLocation(const UObject* WorldContextObject, FVector PawnLocation, float Seconds);

	void PrestreamAtPawnLocation(const FVector& PawnLocation, float Seconds) const;

private:
	const APlayerController* FindLocalPlayerController() const;
	FVector FindCameraOffsetFromPawn() const;
	void AddLeadViewLocation(float DeltaTime);

	FVector LastCameraLocation = FVector::ZeroVector;
	FVector SmoothedCameraVelocity = FVector::ZeroVector;
	bool bHasLastCameraLocation = false;
};
