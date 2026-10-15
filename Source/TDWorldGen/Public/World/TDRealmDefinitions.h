#pragma once

#include "Engine/DataAsset.h"
#include "TDRealmDefinitions.generated.h"

class UDataLayerAsset;
class UMaterialInterface;
class UStaticMesh;

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

UENUM(BlueprintType)
enum class ETDRealmGateKind : uint8
{
	Passage,
	Lift
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDRealmLift
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing")
	FTransform FieldLandingTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing")
	FTransform RealmLandingTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = "0"))
	float CruiseSpeedCmPerSecond = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = "0"))
	float AccelerationCmPerSecondSquared = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = "0"))
	float BrakeDecelerationCmPerSecondSquared = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boarding", meta = (ClampMin = "0"))
	float BoardDwellSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boarding", meta = (ClampMin = "0"))
	float ApproachRadiusCm = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = "0"))
	float CallSpeedCmPerSecond = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Treadmill", meta = (ClampMin = "0"))
	float HoldDepthCm = 1912.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Treadmill", meta = (ClampMin = "0"))
	float MaxTreadmillSeconds = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Liner", meta = (ClampMin = "0"))
	float RingModuleHeightCm = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Liner", meta = (ClampMin = "1"))
	int32 RingModuleCount = 7;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Liner", meta = (ClampMin = "0"))
	float RingTopDepthCm = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Liner", meta = (ClampMin = "0"))
	float AbyssScreenDepthCm = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck")
	FVector DeckHalfExtentCm = FVector(300.0, 300.0, 15.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck")
	TSoftObjectPtr<UStaticMesh> DeckMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck")
	TSoftObjectPtr<UStaticMesh> RailMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck")
	TSoftObjectPtr<UMaterialInterface> DeckMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Liner")
	TSoftObjectPtr<UStaticMesh> LinerPanelMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Liner")
	TSoftObjectPtr<UMaterialInterface> LinerMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Liner")
	TSoftObjectPtr<UMaterialInterface> AbyssScreenMaterial;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDRealmGate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	FName GateId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	ETDRealmGateKind GateKind = ETDRealmGateKind::Passage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	FTransform ArrivalTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	FTransform FieldReturnTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float ApproachLengthCm = 9000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float DepartureLengthCm = 4500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float PassageHalfWidthCm = 1600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (ClampMin = "0"))
	float PreloadLeadCm = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lift")
	FTDRealmLift Lift;

	FVector GetPassageDirection() const;
	FVector GetBoundaryCrossingCm() const;
	FVector GetPreloadLocation(bool bToEntry) const;
	FTransform GetTravelDestination(bool bToEntry) const;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming")
	TSoftObjectPtr<UDataLayerAsset> GameplayDataLayer;

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
