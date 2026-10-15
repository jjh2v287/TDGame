#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TDRealmShaftLiner.generated.h"

class ATDRealmLiftDeck;
class UMaterialInterface;
class UStaticMesh;
struct FTDRealmGate;

UCLASS(NotPlaceable, Transient)
class TDGAME_API ATDRealmShaftLiner : public AActor
{
	GENERATED_BODY()

public:
	ATDRealmShaftLiner();

	void InitializeLiner(const FTDRealmGate& Gate, ATDRealmLiftDeck& Deck);

	virtual void Tick(float DeltaSeconds) override;

	static double ComputeRingTopDepthCm(int32 RingIndex, double PhaseCm, double ModuleHeightCm, int32 ModuleCount, double RingTopDepthCm, bool bIsDescending);

	int32 GetRingCount() const { return RingRoots.Num(); }
	double GetRingTopDepthCm(int32 RingIndex) const;

private:
	void BuildRing(int32 RingIndex, UStaticMesh* PanelMesh, UStaticMesh* CrossbarMesh, UMaterialInterface* Material);
	void RebaseRings(double DeckRingPhaseCm, bool bIsDescending);
	void PlaceRings(double DeckRingPhaseCm);

	UPROPERTY(VisibleAnywhere, Category = "Liner")
	TObjectPtr<USceneComponent> LinerRoot;

	UPROPERTY(VisibleAnywhere, Category = "Liner")
	TArray<TObjectPtr<USceneComponent>> RingRoots;

	TWeakObjectPtr<ATDRealmLiftDeck> LiftDeck;
	double ModuleHeightCm = 450.0;
	int32 ModuleCount = 7;
	double RingTopDepthCm = 600.0;
	double BasePhaseCm = 0.0;
	int32 BaseModuleShift = 0;
	bool bIsBaseDescending = true;
	double AppliedPhaseCm = 0.0;
	bool bHasPlacedRings = false;
};
