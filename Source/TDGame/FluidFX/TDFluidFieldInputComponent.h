#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidFieldInputComponent.generated.h"

struct FTDFluidFieldInputPlacement
{
	UTexture* Texture = nullptr;
	FVector WorldCenter = FVector::ZeroVector;
	FVector WorldAxisX = FVector::ForwardVector;
	FVector2D SizeCm = FVector2D(400.0, 400.0);
	FVector WorldVelocity = FVector::ZeroVector;
	ETDFluidFieldInputLayout Layout = ETDFluidFieldInputLayout::Density;
	bool bSignedVelocityEncoding = true;
	bool bTiled = false;
	float UVTileCount = 1.0f;
	FVector2D UVOffset = FVector2D::ZeroVector;
	float UVRotationRadians = 0.0f;
};

UCLASS(ClassGroup = (FluidFX), meta = (BlueprintSpawnableComponent))
class TDGAME_API UTDFluidFieldInputComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTDFluidFieldInputComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|FieldInput")
	TSoftObjectPtr<UTexture> FieldTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|FieldInput")
	FVector2D FieldSizeCm = FVector2D(400.0, 400.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|FieldInput")
	ETDFluidFieldInputLayout Layout = ETDFluidFieldInputLayout::Density;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|FieldInput")
	bool bUsesSignedVelocityEncoding = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|FieldInput", meta = (ClampMin = "0.01", ClampMax = "64.0"))
	float UVTileCount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|FieldInput")
	FVector2D UVScrollPerSec = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|FieldInput")
	float UVRotationDegrees = 0.0f;

	bool BuildPlacement(double WorldTimeSeconds, FTDFluidFieldInputPlacement& OutPlacement) const;
	void UpdateMotion(float DeltaTime);
	UTexture* GetLoadedFieldTexture() const { return LoadedFieldTexture; }
	void SetLoadedFieldTextureForTests(UTexture* InTexture) { LoadedFieldTexture = InTexture; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RequestFieldTextureLoad();
	void HandleFieldTextureLoaded();

	UPROPERTY(Transient)
	TObjectPtr<UTexture> LoadedFieldTexture;

	TSharedPtr<FStreamableHandle> FieldTextureLoadHandle;
	FVector PreviousWorldLocation = FVector::ZeroVector;
	FVector WorldVelocity = FVector::ZeroVector;
	bool bHasPreviousWorldLocation = false;
};
