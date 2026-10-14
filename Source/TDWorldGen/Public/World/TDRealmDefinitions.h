#pragma once

#include "Engine/DataAsset.h"
#include "TDRealmDefinitions.generated.h"

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDRealmAtmosphere
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun")
	FRotator SunRotation = FRotator(-55.0, 38.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "0"))
	float SunIntensityLux = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun")
	FLinearColor SunColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky", meta = (ClampMin = "0"))
	float SkyLightIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	FLinearColor SkyLightColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	FLinearColor SkyLuminanceFactor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	FLinearColor RayleighScattering = FLinearColor(0.175287f, 0.409607f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky", meta = (ClampMin = "0"))
	float MieScatteringScale = 0.003996f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0"))
	float FogDensity = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0"))
	float FogHeightFalloff = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog")
	float FogHeightCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog")
	FLinearColor FogInscatteringColor = FLinearColor(0.447f, 0.638f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0", ClampMax = "1"))
	float FogMaxOpacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0"))
	float FogStartDistanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0"))
	float SecondFogDensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0"))
	float SecondFogHeightFalloff = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog")
	float SecondFogHeightOffsetCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog")
	FLinearColor VolumetricFogAlbedo = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog")
	FLinearColor VolumetricFogEmissive = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0"))
	float VolumetricFogExtinctionScale = 1.0f;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDRealmGate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	FName GateId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	FTransform ArrivalTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	FTransform FieldReturnTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float ApproachLengthCm = 9000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float DepartureLengthCm = 4500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float SeamBufferCm = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float PassageHalfWidthCm = 1600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float PreloadLeadCm = 4000.f;

	FVector GetPassageDirection() const;
	FVector GetSeamOffsetCm() const;
};

UCLASS(BlueprintType)
class TDWORLDGEN_API UTDRealmDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Realm")
	FName RealmId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Realm")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Realm")
	FVector CenterCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Realm")
	FVector HalfExtentCm = FVector(25200.0, 25200.0, 20000.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Realm")
	float FallRecoveryHeightCm = -100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel")
	TArray<FTDRealmGate> Gates;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Travel")
	FLinearColor TransitionFadeColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float SeamMistDensity = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float SeamMistRadiusCm = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	FTDRealmAtmosphere Atmosphere;

	FBox GetBoundsCm() const;
	bool ContainsLocationXY(const FVector& LocationCm) const;
	const FTDRealmGate* FindGate(FName GateId) const;
	const FTDRealmGate* FindNearestGate(const FVector& LocationCm) const;

	static const UTDRealmDefinition* FindRealmByGateId(FName GateId);
	static const UTDRealmDefinition* FindRealmContainingLocation(const FVector& LocationCm);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
