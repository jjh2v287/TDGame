#pragma once

#include "CoreMinimal.h"
#include "TDFluidPlaneTypes.generated.h"

UENUM(BlueprintType)
enum class ETDFluidPlaneOrientation : uint8
{
	WorldFacing,
	CameraFacing,
	VerticalBillboard
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidPlaneSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidPlaneOrientation Orientation = ETDFluidPlaneOrientation::WorldFacing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float BillboardLeanBackDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "300.0"))
	float CameraDepthBiasCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	float InteractionDepthCm = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.49"))
	float CenterOffsetAlongUpRatio = 0.0f;
};
