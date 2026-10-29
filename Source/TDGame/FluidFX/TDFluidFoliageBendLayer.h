#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "Math/Float16Color.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTDFluidFoliageBend, Log, All);

class FTDFluidGpuFoliageBendState;
class UTexture;
class UTextureRenderTarget2D;

struct FTDFluidFoliageBendParameters
{
	float RiseBlend = 0.0f;
	float RecoveryBlend = 0.0f;
	float PainterDensityWeight = 0.0f;
	float RippleSlopeWeight = 0.0f;
	FVector2f RippleGradientScale = FVector2f::ZeroVector;
	bool bUsesVelocity = true;
	bool bHasWaterSurface = false;
	bool bWrapsArea = false;
};

class TDGAME_API FTDFluidFoliageBendLayer
{
public:
	FTDFluidFoliageBendLayer() = default;
	~FTDFluidFoliageBendLayer();
	FTDFluidFoliageBendLayer(const FTDFluidFoliageBendLayer&) = delete;
	FTDFluidFoliageBendLayer& operator=(const FTDFluidFoliageBendLayer&) = delete;

	static FTDFluidFoliageBendParameters MakeParameters(const FTDFluidSimulationConfig& Config, float DeltaSeconds, const FVector2D& CellSizeCm, bool bHasWaterSurface);
	static FVector2f ComputeTarget(const FVector2f& Velocity, float Density, const FVector2f& DensityGradient, const FVector2f& RippleGradient, const FVector2f& PreviousBend, const FTDFluidFoliageBendParameters& Parameters);
	static FVector2f AdvanceBend(const FVector2f& PreviousBend, const FVector2f& Target, const FTDFluidFoliageBendParameters& Parameters);
	static bool ResolveShift(const FVector2D& PreviousCenterWorld, const FVector2D& CurrentCenterWorld, const FVector2D& CellSizeCm, int32 Resolution, FIntPoint& OutShiftCells);
	static bool ResolveEnabled(int32 ConsoleValue, bool bPresetEnabled);

	void RequestReset();
	void AdvanceCpu(TConstArrayView<FFloat16Color> VelocityPixels, TConstArrayView<FFloat16Color> DensityPressurePixels, TConstArrayView<FFloat16Color> WaterSurfacePixels, int32 Resolution, const FVector2D& CenterWorld, const FVector2D& CellSizeCm, const FTDFluidFoliageBendParameters& Parameters);
	void WriteCpuBendToAux(TArray<FFloat16Color>& InOutAuxPixels, int32 Resolution) const;
	void SubmitGpu(UTexture* VelocityTexture, UTexture* DensityPressureTexture, UTexture* WaterSurfaceTexture, UTextureRenderTarget2D* AuxTarget, bool bPreservesAuxRedGreen, int32 Resolution, const FVector2D& CenterWorld, const FVector2D& CellSizeCm, const FTDFluidFoliageBendParameters& Parameters);
	void Release();
	bool HasOutput() const { return bHasOutput; }
	uint32 ComputeStateChecksum() const;
	const TArray<FVector2f>& GetCpuBendState() const { return BendState; }

private:
	enum class ETDFluidFoliageBendBackend : uint8
	{
		None,
		Cpu,
		Gpu
	};

	bool ResolvesStateReset(ETDFluidFoliageBendBackend Backend, int32 Resolution, const FVector2D& CenterWorld, const FVector2D& CellSizeCm, FIntPoint& OutShiftCells) const;
	void CommitFrame(ETDFluidFoliageBendBackend Backend, int32 Resolution, const FVector2D& CenterWorld);

	TArray<FVector2f> BendState;
	TArray<FVector2f> ScratchBendState;
	TSharedPtr<FTDFluidGpuFoliageBendState, ESPMode::ThreadSafe> RenderState;
	FVector2D CurrentCenterWorld = FVector2D::ZeroVector;
	int32 CurrentResolution = 0;
	ETDFluidFoliageBendBackend CurrentBackend = ETDFluidFoliageBendBackend::None;
	bool bNeedsReset = true;
	bool bHasOutput = false;
};
