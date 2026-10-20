#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "GameFramework/Actor.h"
#include "TDFluidFragmentEmitterActor.generated.h"

class ATDBreakableActor;
class ATDFluidFireActor;
class ATDFluidLiveActor;
class UGeometryCollectionComponent;
struct FTDFluidPlaneBasis;

struct TDGAME_API FTDFluidFragmentPositionHistory
{
	static constexpr int32 FrameCapacity = 8;

	void Reset(int32 InLeafCount);
	void PushFrame(const TArray<FVector>& FramePositions);
	void GetLaggedFrame(int32 LagFrames, TArray<FVector>& OutPositions) const;
	int32 GetWrittenFrameCount() const { return WrittenFrameCount; }

private:
	TArray<FVector> Positions;
	int32 LeafCount = 0;
	int32 WrittenFrameCount = 0;
};

UCLASS()
class TDGAME_API ATDFluidFragmentEmitterActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidFragmentEmitterActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentSource")
	TObjectPtr<ATDBreakableActor> SourceBreakableActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTargets")
	TObjectPtr<ATDFluidFireActor> FireTargetActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTargets")
	TObjectPtr<ATDFluidLiveActor> SmokeTargetActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTrail")
	FTDFluidFragmentTrailSettings TrailSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FragmentTargets")
	TArray<FTDFluidFragmentTarget> Targets;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "FragmentTrail")
	void TriggerBreakAt(const FVector& HitLocation);

	UFUNCTION(BlueprintPure, Category = "FragmentTrail")
	bool IsTrackingFragments() const { return bIsTracking; }

	UFUNCTION(BlueprintPure, Category = "FragmentTrail")
	int32 GetTrackedLeafCount() const { return LeafTransformIndices.Num(); }

	UFUNCTION(BlueprintPure, Category = "FragmentTrail")
	int32 GetMovedLeafCount() const { return MovedLeafCount; }

	UFUNCTION(BlueprintPure, Category = "FragmentTrail")
	int32 GetStampedLeafCount() const { return StampedLeafCount; }

	UFUNCTION(BlueprintPure, Category = "FragmentTrail")
	int32 GetTotalStampCount() const { return TotalStampCount; }

	static void BuildFragmentTrailStamps(
		const TArray<FVector>& PreviousPositions,
		const TArray<FVector>& CurrentPositions,
		const TArray<FVector>& LaggedPositions,
		float DeltaTime,
		const FTDFluidFragmentTrailSettings& Settings,
		TArray<FTDFluidBrushStamp>& OutFireStamps,
		TArray<FTDFluidBrushStamp>& OutSmokeStamps,
		TArray<int32>* OutStampedLeafIndices = nullptr);

	static void BuildBurstStamps(
		const FVector& CenterWorld,
		const FTDFluidPlaneBasis& PlaneBasis,
		const FTDFluidFragmentTrailSettings& Settings,
		ETDFluidBrushChannel Channel,
		float RadiusScale,
		float DensityStrength,
		TArray<FTDFluidBrushStamp>& OutStamps);

	static void BuildBurstStamps(
		const FVector& CenterWorld,
		const FTDFluidPlaneBasis& PlaneBasis,
		const FTDFluidFragmentTrailSettings& Settings,
		ETDFluidBrushChannel Channel,
		float RadiusScale,
		float DensityStrength,
		float HeatStrength,
		TArray<FTDFluidBrushStamp>& OutStamps);

	static void BuildLegacyTargets(ATDFluidFireActor* FireTarget, ATDFluidLiveActor* SmokeTarget, TArray<FTDFluidFragmentTarget>& OutTargets);

	static void BuildTargetTrailStamps(
		const FTDFluidFragmentTarget& Target,
		const TArray<FVector>& PreviousPositions,
		const TArray<FVector>& CurrentPositions,
		const TArray<FVector>& LaggedPositions,
		float DeltaTime,
		float BurstCenterZ,
		const FTDFluidFragmentTrailSettings& Settings,
		TArray<FTDFluidBrushStamp>& OutStamps,
		TArray<int32>* OutStampedLeafIndices = nullptr);

	static void BuildRestingHeatStamps(
		const FTDFluidFragmentTarget& Target,
		const TArray<FVector>& PreviousPositions,
		const TArray<FVector>& CurrentPositions,
		float DeltaTime,
		float ElapsedSeconds,
		const FTDFluidFragmentTrailSettings& Settings,
		TArray<FTDFluidBrushStamp>& OutStamps);

	static float ComputeTargetHeightWeight(const FTDFluidFragmentTarget& Target, double StampZ, double BurstCenterZ);

	void ResolveActiveTargets(TArray<FTDFluidFragmentTarget>& OutTargets) const;
	FVector GetBurstCenterWorld() const { return BurstCenterWorld; }

private:
	UGeometryCollectionComponent* ResolveGeometryCollectionComponent();
	void PollForRootBreak();
	void BeginFragmentTracking(const FVector& CenterWorld);
	void CollectLeafTransformIndices(const UGeometryCollectionComponent& Component);
	void CaptureLeafWorldPositions(UGeometryCollectionComponent& Component, TArray<FVector>& OutPositions) const;
	void StampBurst(const FVector& CenterWorld);
	void SustainBurst();
	void QueueBurstOnTargets(float DensityRatio, float HeatRatio);
	void HandleSourceBroken(const FVector& BreakLocation);
	void StepFragmentTracking(float DeltaSeconds);
	void QueueStampsOnTarget(ATDFluidLiveActor* TargetActor, const TArray<FTDFluidBrushStamp>& Stamps);
	void RecordStampedLeaves(const TArray<int32>& StampedLeafIndices);
	void UpdateMovedLeafCount(const TArray<FVector>& CurrentPositions);
	void StopFragmentTracking();

	TWeakObjectPtr<UGeometryCollectionComponent> GeometryCollectionComponent;
	TArray<int32> LeafTransformIndices;
	TArray<FVector> InitialLeafPositions;
	TArray<bool> LeafStampedFlags;
	FTDFluidFragmentPositionHistory PositionHistory;
	float TrackingElapsedSeconds = 0.0f;
	FVector BurstCenterWorld = FVector::ZeroVector;
	int32 MovedLeafCount = 0;
	int32 StampedLeafCount = 0;
	int32 TotalStampCount = 0;
	FDelegateHandle SourceBrokenDelegateHandle;
	bool bHasStartedTracking = false;
	bool bIsTracking = false;
	bool bIsTriggeringBreak = false;
};
