#include "FluidFX/TDFluidWaterLodGridComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
	constexpr double LodBudgetShrinkFactor = 0.85;
	constexpr int32 LodBudgetShrinkAttemptLimit = 4;
	constexpr double LodMinimumDivisor = 0.01;

	TAutoConsoleVariable<int32> CVarTDFluidWaterLodGrid(
		TEXT("TD.FluidFX.WaterLodGrid"),
		-1,
		TEXT("Water LOD grid: -1 follows the preset, 0 forces off, 1 forces on."),
		ECVF_Default);

	FTDFluidWaterLodGridSettings ClampLodGridSettingsToRanges(const FTDFluidWaterLodGridSettings& InSettings)
	{
		FTDFluidWaterLodGridSettings Settings = InSettings;
		Settings.BaseQuadSizeCm = FMath::Clamp(Settings.BaseQuadSizeCm, 10.0f, 40.0f);
		Settings.BaseRangeCm = FMath::Clamp(Settings.BaseRangeCm, 1000.0f, 8000.0f);
		Settings.LevelCount = FMath::Clamp(Settings.LevelCount, 1, 10);
		Settings.PatchQuads = FMath::Clamp(Settings.PatchQuads + (Settings.PatchQuads & 1), 4, 64);
		Settings.MorphStartRatio = FMath::Clamp(Settings.MorphStartRatio, 0.55f, 0.9f);
		Settings.VertexWaveFilterRatio = FMath::Clamp(Settings.VertexWaveFilterRatio, 0.0f, 8.0f);
		Settings.TotalVertexBudget = FMath::Clamp(Settings.TotalVertexBudget, 20000, 1000000);
		Settings.ReselectDistanceCm = FMath::Clamp(Settings.ReselectDistanceCm, 1.0f, 500.0f);
		Settings.CoverageMarginCm = FMath::Clamp(Settings.CoverageMarginCm, 0.0f, 500000.0f);
		return Settings;
	}

	double ComputeNodeDistanceCm(const FVector& ObserverWorld, double SurfaceHeightCm, const FVector2D& MinCornerCm, double SizeCm)
	{
		const double DeltaX = FMath::Max3(MinCornerCm.X - ObserverWorld.X, 0.0, ObserverWorld.X - (MinCornerCm.X + SizeCm));
		const double DeltaY = FMath::Max3(MinCornerCm.Y - ObserverWorld.Y, 0.0, ObserverWorld.Y - (MinCornerCm.Y + SizeCm));
		const double DeltaZ = ObserverWorld.Z - SurfaceHeightCm;
		return FMath::Sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
	}

	bool DoesNodeOverlapCoverage(const FBox2D& CoverageCm, const FVector2D& MinCornerCm, double SizeCm)
	{
		return MinCornerCm.X < CoverageCm.Max.X && MinCornerCm.X + SizeCm > CoverageCm.Min.X
			&& MinCornerCm.Y < CoverageCm.Max.Y && MinCornerCm.Y + SizeCm > CoverageCm.Min.Y;
	}

	struct FTDFluidLodSelectionContext
	{
		FVector ObserverWorld = FVector::ZeroVector;
		FBox2D CoverageCm = FBox2D(ForceInit);
		double SurfaceHeightCm = 0.0;
		double BaseRangeCm = 0.0;
		TArray<FTDFluidWaterLodPatch>* OutPatches = nullptr;
	};

	void SelectNodeRecursive(const FTDFluidLodSelectionContext& Context, const FVector2D& MinCornerCm, double SizeCm, int32 Level)
	{
		if (!DoesNodeOverlapCoverage(Context.CoverageCm, MinCornerCm, SizeCm))
		{
			return;
		}

		const double ChildRangeCm = Context.BaseRangeCm * FMath::Pow(2.0, static_cast<double>(Level - 1));
		if (Level == 0 || ComputeNodeDistanceCm(Context.ObserverWorld, Context.SurfaceHeightCm, MinCornerCm, SizeCm) > ChildRangeCm)
		{
			FTDFluidWaterLodPatch& Patch = Context.OutPatches->AddDefaulted_GetRef();
			Patch.MinCornerCm = MinCornerCm;
			Patch.SizeCm = SizeCm;
			Patch.Level = Level;
			return;
		}

		const double HalfSizeCm = 0.5 * SizeCm;
		SelectNodeRecursive(Context, MinCornerCm, HalfSizeCm, Level - 1);
		SelectNodeRecursive(Context, MinCornerCm + FVector2D(HalfSizeCm, 0.0), HalfSizeCm, Level - 1);
		SelectNodeRecursive(Context, MinCornerCm + FVector2D(0.0, HalfSizeCm), HalfSizeCm, Level - 1);
		SelectNodeRecursive(Context, MinCornerCm + FVector2D(HalfSizeCm, HalfSizeCm), HalfSizeCm, Level - 1);
	}

	void SelectPatchesAtRange(const FTDFluidWaterLodSelectionInput& Input, const FTDFluidWaterLodGridSettings& Settings, const FBox2D& CoverageCm, double BaseRangeCm, TArray<FTDFluidWaterLodPatch>& OutPatches)
	{
		OutPatches.Reset();
		const int32 RootLevel = Settings.LevelCount - 1;
		const double RootSizeCm = static_cast<double>(Settings.PatchQuads) * Settings.BaseQuadSizeCm * FMath::Pow(2.0, static_cast<double>(RootLevel));
		const int64 FirstRootX = FMath::FloorToInt64(CoverageCm.Min.X / RootSizeCm);
		const int64 LastRootX = FMath::CeilToInt64(CoverageCm.Max.X / RootSizeCm) - 1;
		const int64 FirstRootY = FMath::FloorToInt64(CoverageCm.Min.Y / RootSizeCm);
		const int64 LastRootY = FMath::CeilToInt64(CoverageCm.Max.Y / RootSizeCm) - 1;

		FTDFluidLodSelectionContext Context;
		Context.ObserverWorld = Input.ObserverWorld;
		Context.CoverageCm = CoverageCm;
		Context.SurfaceHeightCm = Input.SurfaceHeightCm;
		Context.BaseRangeCm = BaseRangeCm;
		Context.OutPatches = &OutPatches;
		for (int64 RootY = FirstRootY; RootY <= LastRootY; ++RootY)
		{
			for (int64 RootX = FirstRootX; RootX <= LastRootX; ++RootX)
			{
				SelectNodeRecursive(Context, FVector2D(static_cast<double>(RootX) * RootSizeCm, static_cast<double>(RootY) * RootSizeCm), RootSizeCm, RootLevel);
			}
		}
	}
}

UTDFluidWaterLodGridComponent::UTDFluidWaterLodGridComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Mobility = EComponentMobility::Movable;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	CastShadow = false;
	bReceivesDecals = false;
	bEvaluateWorldPositionOffsetInRayTracing = false;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
	NumCustomDataFloats = 1;
}

FTDFluidWaterLodGridSettings UTDFluidWaterLodGridComponent::SanitizeSettings(const FTDFluidWaterLodGridSettings& InSettings)
{
	FTDFluidWaterLodGridSettings Settings = ClampLodGridSettingsToRanges(InSettings);
	Settings.BaseRangeCm = FMath::Max(Settings.BaseRangeCm, static_cast<float>(ComputeCrackFreeMinimumRangeCm(Settings)));
	return Settings;
}

double UTDFluidWaterLodGridComponent::ComputeCrackFreeMinimumRangeCm(const FTDFluidWaterLodGridSettings& Settings)
{
	const double MorphDenominator = FMath::Max(2.0 * Settings.MorphStartRatio - 1.0, LodMinimumDivisor);
	return 2.0 * UE_DOUBLE_SQRT_2 * static_cast<double>(Settings.PatchQuads) * Settings.BaseQuadSizeCm / MorphDenominator;
}

FTDFluidWaterLodSelectionResult UTDFluidWaterLodGridComponent::SelectPatches(const FTDFluidWaterLodSelectionInput& Input)
{
	FTDFluidWaterLodSelectionResult Result;
	const FTDFluidWaterLodGridSettings Settings = Input.bSkipsCrackFreeRange ? ClampLodGridSettingsToRanges(Input.Settings) : SanitizeSettings(Input.Settings);
	const double RangeFloorCm = Input.bSkipsCrackFreeRange ? 0.0 : ComputeCrackFreeMinimumRangeCm(Settings);
	if (!Input.CoverageCm.bIsValid)
	{
		Result.EffectiveBaseRangeCm = Settings.BaseRangeCm;
		return Result;
	}

	const FBox2D CoverageCm = Input.CoverageCm.ExpandBy(static_cast<double>(Settings.CoverageMarginCm));
	const int32 PatchVertexCount = (Settings.PatchQuads + 1) * (Settings.PatchQuads + 1);
	const int32 MaximumPatchCount = FMath::Max(1, Settings.TotalVertexBudget / PatchVertexCount);
	double BaseRangeCm = Input.bSkipsCrackFreeRange ? Input.Settings.BaseRangeCm : Settings.BaseRangeCm;
	for (int32 AttemptIndex = 0; AttemptIndex <= LodBudgetShrinkAttemptLimit; ++AttemptIndex)
	{
		SelectPatchesAtRange(Input, Settings, CoverageCm, BaseRangeCm, Result.Patches);
		Result.EffectiveBaseRangeCm = BaseRangeCm;
		if (Result.Patches.Num() <= MaximumPatchCount)
		{
			return Result;
		}

		if (AttemptIndex == LodBudgetShrinkAttemptLimit)
		{
			break;
		}
		BaseRangeCm = FMath::Max(RangeFloorCm, LodBudgetShrinkFactor * BaseRangeCm);
	}

	Result.bReachedBudget = true;
	return Result;
}

FVector2D UTDFluidWaterLodGridComponent::ComputeMorphedVertexXY(const FVector2D& VertexWorldXY, double SpacingCm, const FVector& ObserverWorld, double SurfaceHeightCm, double BaseRangeCm, const FTDFluidWaterLodGridSettings& Settings)
{
	if (SpacingCm <= 0.0)
	{
		return VertexWorldXY;
	}

	const double MorphStartRatio = Settings.MorphStartRatio;
	const double LevelRangeCm = BaseRangeCm * SpacingCm / FMath::Max(static_cast<double>(Settings.BaseQuadSizeCm), LodMinimumDivisor);
	const double DistanceCm = FVector::Dist(FVector(VertexWorldXY.X, VertexWorldXY.Y, SurfaceHeightCm), ObserverWorld);
	const double MorphAmount = FMath::Clamp((DistanceCm - MorphStartRatio * LevelRangeCm) / FMath::Max((1.0 - MorphStartRatio) * LevelRangeCm, LodMinimumDivisor), 0.0, 1.0);
	const FVector2D VertexIndex(FMath::RoundToDouble(VertexWorldXY.X / SpacingCm), FMath::RoundToDouble(VertexWorldXY.Y / SpacingCm));
	const FVector2D OddIndex(VertexIndex.X - 2.0 * FMath::FloorToDouble(VertexIndex.X * 0.5), VertexIndex.Y - 2.0 * FMath::FloorToDouble(VertexIndex.Y * 0.5));
	return VertexWorldXY - OddIndex * SpacingCm * MorphAmount;
}

bool UTDFluidWaterLodGridComponent::ResolveLodGridEnabled(bool bIsPresetEnabled)
{
	const int32 ConsoleValue = CVarTDFluidWaterLodGrid.GetValueOnGameThread();
	if (ConsoleValue < 0)
	{
		return bIsPresetEnabled;
	}

	return ConsoleValue > 0;
}

void UTDFluidWaterLodGridComponent::ApplyPatches(const FTDFluidWaterLodSelectionResult& Result, double SurfaceHeightCm, int32 PatchQuads)
{
	TArray<FTransform> PatchTransforms;
	PatchTransforms.Reserve(Result.Patches.Num());
	for (const FTDFluidWaterLodPatch& Patch : Result.Patches)
	{
		const double PatchScale = Patch.SizeCm / PatchMeshSizeCm;
		const FVector PatchCenter(Patch.MinCornerCm.X + 0.5 * Patch.SizeCm, Patch.MinCornerCm.Y + 0.5 * Patch.SizeCm, SurfaceHeightCm);
		PatchTransforms.Add(FTransform(FQuat::Identity, PatchCenter, FVector(PatchScale, PatchScale, 1.0)));
	}

	const int32 ExistingCount = GetInstanceCount();
	const int32 ReusedCount = FMath::Min(ExistingCount, PatchTransforms.Num());
	if (ReusedCount > 0)
	{
		BatchUpdateInstancesTransforms(0, TArrayView<const FTransform>(PatchTransforms.GetData(), ReusedCount), true, false, true);
	}

	if (PatchTransforms.Num() > ExistingCount)
	{
		AddInstances(TArray<FTransform>(PatchTransforms.GetData() + ExistingCount, PatchTransforms.Num() - ExistingCount), false, true, false);
	}
	else if (ExistingCount > PatchTransforms.Num())
	{
		TArray<int32> RemovedIndices;
		for (int32 InstanceIndex = ExistingCount - 1; InstanceIndex >= PatchTransforms.Num(); --InstanceIndex)
		{
			RemovedIndices.Add(InstanceIndex);
		}
		RemoveInstances(RemovedIndices, true);
	}

	const double QuadCount = FMath::Max(PatchQuads, 1);
	for (int32 PatchIndex = 0; PatchIndex < Result.Patches.Num(); ++PatchIndex)
	{
		SetCustomDataValue(PatchIndex, 0, static_cast<float>(Result.Patches[PatchIndex].SizeCm / QuadCount), false);
	}
	MarkRenderStateDirty();
}

void UTDFluidWaterLodGridComponent::ClearPatches()
{
	if (GetInstanceCount() == 0)
	{
		return;
	}

	ClearInstances();
	MarkRenderStateDirty();
}
