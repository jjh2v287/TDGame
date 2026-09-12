#pragma once

#include "CoreMinimal.h"
#include "Field/FieldSystemTypes.h"
#include "GameFramework/Actor.h"
#include "Physics/Experimental/ChaosEventType.h"
#include "TDBreakableActor.generated.h"

class UFieldSystemComponent;
class UGeometryCollectionComponent;
class USphereComponent;

UCLASS(Blueprintable)
class TDGAME_API ATDBreakableActor : public AActor
{
	GENERATED_BODY()

public:
	explicit ATDBreakableActor(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category="Breakable")
	void StartBreak(const FVector& HitLocation);

protected:
	UFUNCTION(BlueprintImplementableEvent, Category="Breakable")
	void OnBreak(const FVector& Location);

private:
	void ApplyRadialBreakForce(const FVector& HitLocation);
	void ApplyImpulseBreakForce(const FVector& HitLocation);
	void ScheduleAutoDestroy();

	UFUNCTION()
	void OnChaosBreakEvent(const FChaosBreakEvent& BreakEvent);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UGeometryCollectionComponent> GeometryCollectionComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UFieldSystemComponent> FieldSystemComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Debug", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USphereComponent> DebugSphereComponent;
#endif

	UPROPERTY(EditDefaultsOnly, Category="Breakable")
	bool bAutoDestroyActor = true;

	UPROPERTY(EditDefaultsOnly, Category="Breakable")
	float DestroyDelay = 3.0f;

	UPROPERTY(EditDefaultsOnly, Category="Breakable|Force")
	bool bUseRadialForce = true;

	UPROPERTY(EditAnywhere, Category="Breakable|Force", meta=(EditCondition="bUseRadialForce", ClampMin="0.0"))
	float BreakForce = 1000.0f;

	UPROPERTY(EditAnywhere, Category="Breakable|Force", meta=(EditCondition="bUseRadialForce", ClampMin="0.0"))
	float BreakForceRadius = 500.0f;

	UPROPERTY(EditAnywhere, Category="Breakable|Optimization")
	bool bUseObjectTypeFilter = true;

	UPROPERTY(EditAnywhere, Category="Breakable|Optimization", meta=(EditCondition="bUseObjectTypeFilter"))
	TEnumAsByte<EFieldObjectType> FieldObjectType = EFieldObjectType::Field_Object_Destruction;

	UPROPERTY(EditDefaultsOnly, Category="Breakable|Collision")
	TEnumAsByte<ECollisionChannel> FragmentObjectChannel = ECC_PhysicsBody;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category="Debug")
	bool bShowDebugSphere = true;

	UPROPERTY(EditAnywhere, Category="Debug", meta=(EditCondition="bShowDebugSphere"))
	FColor DebugSphereColor = FColor::Yellow;
#endif

	bool bIsBroken = false;
};
