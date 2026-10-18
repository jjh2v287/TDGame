#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "TDDamageDefinition.generated.h"

class UNiagaraSystem;
class UStaticMesh;
class UMaterialInterface;

UCLASS(BlueprintType)
class TDGAME_API UTDDamageDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Casting", meta=(ClampMin="0"))
	float Cooldown = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Casting", meta=(ClampMin="1"))
	float CastRange = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	ETDDamageEntityMode Mode = ETDDamageEntityMode::Area;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	ETDDamageTargetPolicy TargetPolicy = ETDDamageTargetPolicy::Enemies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.01"))
	float Lifetime = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0"))
	float ActivationDelay = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.02"))
	float PulseInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(ClampMin="1"))
	float Radius = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(ClampMin="0"))
	float InnerRadius = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(ClampMin="1"))
	float HalfHeight = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(ToolTip="Footprint of an Area entity. Circle fills Radius, Cone is a wedge along the facing, Line is a band along the facing, Ring excludes InnerRadius."))
	ETDDamageAreaShape AreaShape = ETDDamageAreaShape::Circle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(ClampMin="1", ClampMax="180", ToolTip="Half of the Cone opening angle in degrees, measured from the facing."))
	float ConeHalfAngleDegrees = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape", meta=(ClampMin="1", ToolTip="Half of the Line band width in cm. The band is Radius long."))
	float LineHalfWidth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shape")
	bool bProjectToGround = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0"))
	float ExpansionSpeed = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="1"))
	float ProjectileSpeed = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="1"))
	float ProjectileRadius = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hits", meta=(ClampMin="0"))
	int32 MaxHitsPerTarget = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hits", meta=(ClampMin="0.02"))
	float HitInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hits")
	bool bDestroyOnHit = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hits")
	bool bRequireLineOfSight = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Events")
	TArray<FTDDamageRule> Rules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	TSoftObjectPtr<UNiagaraSystem> VisualEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	TSoftObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	FVector VisualScale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	FVector VisualOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	FRotator VisualRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation", meta=(ClampMin="0", ClampMax="10"))
	float VisualTailSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	bool bShowDuringActivationDelay = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	bool bScaleVisualWithRadius = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	bool bDrawDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Presentation")
	FLinearColor DebugColor = FLinearColor::Red;

	bool ValidateDefinition(FString& OutError) const;
	void GatherPresentationAssetPaths(TArray<FSoftObjectPath>& OutPaths) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
