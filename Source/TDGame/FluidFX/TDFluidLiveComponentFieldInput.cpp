#include "FluidFX/TDFluidLiveComponent.h"

#include "Engine/Texture.h"
#include "FluidFX/TDFluidInputBrushRules.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidLiveFieldInput, Log, All);

void UTDFluidLiveComponent::SetFieldInputPlacements(TArray<FTDFluidFieldInputPlacement>&& InPlacements)
{
	FieldInputTextures.Reset();
	for (const FTDFluidFieldInputPlacement& Placement : InPlacements)
	{
		if (Placement.Texture)
		{
			FieldInputTextures.AddUnique(Placement.Texture);
		}
	}
	PendingFieldInputPlacements = MoveTemp(InPlacements);
}

void UTDFluidLiveComponent::RecordIgnoredFieldInputs()
{
	if (PendingFieldInputPlacements.IsEmpty())
	{
		return;
	}

	IgnoredFieldInputCount += PendingFieldInputPlacements.Num();
	PendingFieldInputPlacements.Reset();
	if (bHasWarnedIgnoredFieldInputs)
	{
		return;
	}

	bHasWarnedIgnoredFieldInputs = true;
	UE_LOG(LogTDFluidLiveFieldInput, Warning, TEXT("%s ignores field inputs because field inputs need the GPU fluid solver."), *GetPathNameSafe(GetOwner()));
}

TArray<FTDFluidGpuFieldInput> UTDFluidLiveComponent::BuildGpuFieldInputs()
{
	TArray<FTDFluidGpuFieldInput> FieldInputs;
	if (PendingFieldInputPlacements.IsEmpty())
	{
		return FieldInputs;
	}

	const bool bIsVelocityOnly = FTDFluidInputBrushRules::IsVelocityOnlySource(InputBrushSettings.VelocityOnlySources, ETDFluidBrushSource::Field);
	const float DensityScale = bIsVelocityOnly ? 0.0f : FieldInputSettings.FieldDensityScale;
	const bool bErasesDensity = InputBrushSettings.bShouldInvertInputDensity;
	FieldInputs.Reserve(PendingFieldInputPlacements.Num());
	for (const FTDFluidFieldInputPlacement& Placement : PendingFieldInputPlacements)
	{
		FTextureResource* TextureResource = Placement.Texture ? Placement.Texture->GetResource() : nullptr;
		if (!TextureResource)
		{
			continue;
		}

		const FVector2D AxisXPlane = PlaneBasis.ProjectVector(Placement.WorldAxisX).GetSafeNormal();
		const bool bIsCollisionLayout = Placement.Layout == ETDFluidFieldInputLayout::CollisionMask || Placement.Layout == ETDFluidFieldInputLayout::MeshDistance;
		FTDFluidGpuFieldInput& Field = FieldInputs.AddDefaulted_GetRef();
		Field.Texture = TextureResource;
		Field.CenterPlaneCm = ProjectWorldToSolverPlane(Placement.WorldCenter);
		Field.AxisXPlane = AxisXPlane.IsNearlyZero() ? FVector2D(1.0, 0.0) : AxisXPlane;
		Field.HalfSizeCm = Placement.SizeCm * 0.5;
		Field.MotionPlaneCmPerSec = PlaneBasis.ProjectVector(Placement.WorldVelocity);
		Field.UVOffset = Placement.UVOffset;
		Field.UVTileCount = Placement.UVTileCount;
		Field.UVRotationRadians = Placement.UVRotationRadians;
		Field.LayoutRequest = static_cast<uint8>(Placement.Layout);
		Field.bSignedVelocity = Placement.bSignedVelocityEncoding;
		Field.bWritesCollision = FieldInputSettings.bFieldCollisionEnabled && bIsCollisionLayout;
		Field.bFillsInterior = FieldInputSettings.bMeshFieldFillsInterior;
		Field.bErasesDensity = bErasesDensity;
		Field.bTiled = Placement.bTiled;
		Field.EraseRatePerSec = bErasesDensity ? FMath::Abs(DensityScale) * SimulationConfig.BrushEraseRatePerSec : 0.0f;
		Field.DensityScale = DensityScale;
		Field.VelocityScale = FieldInputSettings.FieldVelocityScale;
		Field.VelocityRangeCmPerSec = FieldInputSettings.FieldVelocityRangeCmPerSec;
		Field.CollisionEdgeCm = FieldInputSettings.FieldCollisionEdgeCm;
		Field.ContourBandCm = FieldInputSettings.MeshContourBandCm;
		Field.ContourDensityStrength = FieldInputSettings.MeshFieldDensityStrength;
		Field.OutwardVelocityCmPerSec = FieldInputSettings.MeshFieldOutwardVelocityCmPerSec;
		Field.MotionVelocityStrength = FieldInputSettings.MeshFieldMotionVelocityStrength;
		Field.ContourNoiseAmount = FieldInputSettings.MeshContourNoiseAmount;
		Field.ContourNoiseScaleCm = FieldInputSettings.MeshContourNoiseScaleCm;
	}
	PendingFieldInputPlacements.Reset();
	return FieldInputs;
}
