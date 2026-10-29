#include "TDFluidGpuFieldInput.h"

#include "TDFluidGpuBatch.h"

#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "RHIStaticStates.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "TextureResource.h"

#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuFieldInput, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarTDFluidFieldInput(
		TEXT("TD.FluidFX.FieldInput"),
		1,
		TEXT("0 ignores every texture and mesh field input on the GPU solver, including its collision."),
		ECVF_Default);

	constexpr double FieldInputMinimumHalfSizeCm = 0.001;
	constexpr float FieldInputMinimumCollisionEdgeCm = 0.01f;
	constexpr float FieldInputMinimumNoiseScaleCm = 0.001f;

	std::atomic<bool> bFieldInputHasWarnedDropped(false);

	bool FieldInputWritesCollisionLayout(uint32 Layout)
	{
		return Layout == static_cast<uint32>(ETDFluidGpuFieldLayout::CollisionMask) || Layout == static_cast<uint32>(ETDFluidGpuFieldLayout::MeshDistance);
	}

	uint32 FieldInputResolveLayout(uint8 LayoutRequest)
	{
		return FMath::Min<uint32>(LayoutRequest, static_cast<uint32>(ETDFluidGpuFieldLayout::MeshDistance));
	}

	FVector2D FieldInputRotate(const FVector2D& Value, double Radians)
	{
		const double SinAngle = FMath::Sin(Radians);
		const double CosAngle = FMath::Cos(Radians);
		return FVector2D(CosAngle * Value.X - SinAngle * Value.Y, SinAngle * Value.X + CosAngle * Value.Y);
	}

	ETDFluidGpuFieldFlags FieldInputResolveFlags(const FTDFluidGpuFieldInput& Field, uint32 Layout)
	{
		ETDFluidGpuFieldFlags Flags = ETDFluidGpuFieldFlags::None;
		Flags |= Field.bSignedVelocity ? ETDFluidGpuFieldFlags::SignedVelocity : ETDFluidGpuFieldFlags::None;
		Flags |= Field.bWritesCollision && FieldInputWritesCollisionLayout(Layout) ? ETDFluidGpuFieldFlags::WritesCollision : ETDFluidGpuFieldFlags::None;
		Flags |= Field.bFillsInterior ? ETDFluidGpuFieldFlags::FillsInterior : ETDFluidGpuFieldFlags::None;
		Flags |= Field.bErasesDensity ? ETDFluidGpuFieldFlags::ErasesDensity : ETDFluidGpuFieldFlags::None;
		Flags |= Field.bTiled ? ETDFluidGpuFieldFlags::Tiled : ETDFluidGpuFieldFlags::None;
		return Flags;
	}

	int32 FieldInputFindOrAddTextureSlot(FTextureResource* Texture, TArray<FTextureResource*>& InOutTextures)
	{
		const int32 ExistingSlot = InOutTextures.Find(Texture);
		if (ExistingSlot != INDEX_NONE)
		{
			return ExistingSlot;
		}
		if (InOutTextures.Num() >= TDFluidGpuSolverConstants::MaximumFieldTexturesPerBatch)
		{
			return INDEX_NONE;
		}
		return InOutTextures.Add(Texture);
	}

	void FieldInputAppendActorFields(const FTDFluidGpuActorInput& Actor, FTDFluidGpuFieldInputBatchPlan& InOutPlan, FUintVector2& OutRange)
	{
		const FVector2D WindowCenter = Actor.WindowCenterPlaneCm.ContainsNaN() ? FVector2D::ZeroVector : Actor.WindowCenterPlaneCm;
		OutRange = FUintVector2(static_cast<uint32>(InOutPlan.Fields.Num()), 0u);
		for (const FTDFluidGpuFieldInput& Field : Actor.FieldInputs)
		{
			if (!Field.Texture)
			{
				continue;
			}
			if (OutRange.Y >= static_cast<uint32>(TDFluidGpuSolverConstants::MaximumFieldInputsPerActor))
			{
				++InOutPlan.DroppedCount;
				continue;
			}

			const int32 Slot = FieldInputFindOrAddTextureSlot(Field.Texture, InOutPlan.Textures);
			if (Slot == INDEX_NONE)
			{
				++InOutPlan.DroppedCount;
				continue;
			}

			InOutPlan.Fields.Add(TDFluidGpuFieldInput::PackField(Field, WindowCenter, static_cast<uint32>(Slot)));
			++OutRange.Y;
		}
	}

	void FieldInputWarnDroppedOnce(int32 DroppedCount)
	{
		if (DroppedCount <= 0 || bFieldInputHasWarnedDropped.exchange(true))
		{
			return;
		}
		UE_LOG(LogTDFluidGpuFieldInput, Warning, TEXT("A GPU fluid batch dropped %d field input(s): at most %d textures per batch and %d fields per actor."), DroppedCount, TDFluidGpuSolverConstants::MaximumFieldTexturesPerBatch, TDFluidGpuSolverConstants::MaximumFieldInputsPerActor);
	}

	FRHITexture* FieldInputResolveSlotTexture(const FTDFluidGpuFieldInputBatchPlan& Plan, int32 Slot)
	{
		FTextureResource* Resource = Plan.Textures.IsValidIndex(Slot) ? Plan.Textures[Slot] : nullptr;
		if (Resource && Resource->TextureRHI.IsValid())
		{
			return Resource->TextureRHI.GetReference();
		}
		return GBlackTexture->TextureRHI.GetReference();
	}
}

namespace TDFluidGpuFieldInput
{
	bool IsAllowedByConsole()
	{
		return CVarTDFluidFieldInput.GetValueOnAnyThread() != 0;
	}

	bool HasCollisionField(const FTDFluidGpuActorInput& Actor)
	{
		if (Actor.FieldInputs.IsEmpty() || !IsAllowedByConsole())
		{
			return false;
		}
		return Actor.FieldInputs.ContainsByPredicate([](const FTDFluidGpuFieldInput& Field)
		{
			return Field.Texture && Field.bWritesCollision && FieldInputWritesCollisionLayout(FieldInputResolveLayout(Field.LayoutRequest));
		});
	}

	FTDFluidGpuPackedFieldInput PackField(const FTDFluidGpuFieldInput& Field, const FVector2D& WindowCenterPlaneCm, uint32 TextureSlot)
	{
		const FVector2D AxisX = Field.AxisXPlane.GetSafeNormal();
		const FVector2D HalfSizeCm(FMath::Max(Field.HalfSizeCm.X, FieldInputMinimumHalfSizeCm), FMath::Max(Field.HalfSizeCm.Y, FieldInputMinimumHalfSizeCm));
		const uint32 Layout = FieldInputResolveLayout(Field.LayoutRequest);

		FTDFluidGpuPackedFieldInput Packed;
		Packed.CenterFromWindowCenterCm = FVector2f(Field.CenterPlaneCm - WindowCenterPlaneCm);
		Packed.AxisX = AxisX.IsNearlyZero() ? FVector2f(1.0f, 0.0f) : FVector2f(AxisX);
		Packed.InvHalfSizeCm = FVector2f(static_cast<float>(1.0 / HalfSizeCm.X), static_cast<float>(1.0 / HalfSizeCm.Y));
		Packed.MotionPlaneCmPerSec = FVector2f(Field.MotionPlaneCmPerSec);
		Packed.UVOffset = FVector2f(Field.UVOffset);
		Packed.UVTileCount = Field.UVTileCount;
		Packed.UVRotationRadians = Field.UVRotationRadians;
		Packed.TextureSlot = TextureSlot;
		Packed.Layout = Layout;
		Packed.Flags = static_cast<uint32>(FieldInputResolveFlags(Field, Layout));
		Packed.EraseRatePerSec = Field.EraseRatePerSec;
		Packed.DensityScale = Field.DensityScale;
		Packed.VelocityScale = Field.VelocityScale;
		Packed.VelocityRangeCmPerSec = Field.VelocityRangeCmPerSec;
		Packed.CollisionEdgeCm = FMath::Max(Field.CollisionEdgeCm, FieldInputMinimumCollisionEdgeCm);
		Packed.ContourBandCm = Field.ContourBandCm;
		Packed.ContourDensityStrength = Field.ContourDensityStrength;
		Packed.OutwardVelocityCmPerSec = Field.OutwardVelocityCmPerSec;
		Packed.MotionVelocityStrength = Field.MotionVelocityStrength;
		Packed.ContourNoiseAmount = Field.ContourNoiseAmount;
		Packed.InvContourNoiseScaleCm = 1.0f / FMath::Max(Field.ContourNoiseScaleCm, FieldInputMinimumNoiseScaleCm);
		Packed.HalfSizeXCm = static_cast<float>(HalfSizeCm.X);
		Packed.HalfSizeYCm = static_cast<float>(HalfSizeCm.Y);
		return Packed;
	}

	FVector2D ResolveFieldUV(const FTDFluidGpuPackedFieldInput& Field, const FVector2D& LocalUnit)
	{
		const FVector2D UV = FieldInputRotate(LocalUnit * 0.5, Field.UVRotationRadians) * Field.UVTileCount + FVector2D(0.5, 0.5) + FVector2D(Field.UVOffset);
		if (!EnumHasAnyFlags(static_cast<ETDFluidGpuFieldFlags>(Field.Flags), ETDFluidGpuFieldFlags::Tiled))
		{
			return UV;
		}
		return FVector2D(UV.X - FMath::FloorToDouble(UV.X), UV.Y - FMath::FloorToDouble(UV.Y));
	}

	FTDFluidGpuFieldInputBatchPlan BuildBatchPlan(TConstArrayView<const FTDFluidGpuActorInput*> EntryActors)
	{
		FTDFluidGpuFieldInputBatchPlan Plan;
		Plan.EntryRanges.SetNumZeroed(EntryActors.Num());
		if (!IsAllowedByConsole())
		{
			return Plan;
		}

		for (int32 EntryIndex = 0; EntryIndex < EntryActors.Num(); ++EntryIndex)
		{
			const FTDFluidGpuActorInput* Actor = EntryActors[EntryIndex];
			if (!Actor || Actor->FieldInputs.IsEmpty())
			{
				continue;
			}
			FieldInputAppendActorFields(*Actor, Plan, Plan.EntryRanges[EntryIndex]);
		}
		return Plan;
	}

	bool PrepareGraph(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, const FTDFluidGpuBatch& Batch, FTDFluidGpuFieldInputGraph& OutGraph)
	{
		const TArray<FTDFluidGpuSliceEntry>& Entries = Batch.GetFrame().Entries;
		TArray<const FTDFluidGpuActorInput*> EntryActors;
		EntryActors.Reserve(Entries.Num());
		bool bHasAnyField = false;
		for (const FTDFluidGpuSliceEntry& Entry : Entries)
		{
			const FTDFluidGpuActorInput* Actor = FrameInputs.Actors.IsValidIndex(Entry.ActorInputIndex) ? &FrameInputs.Actors[Entry.ActorInputIndex] : nullptr;
			bHasAnyField |= Actor && !Actor->FieldInputs.IsEmpty();
			EntryActors.Add(Actor);
		}
		if (!bHasAnyField)
		{
			return false;
		}

		const FTDFluidGpuFieldInputBatchPlan Plan = BuildBatchPlan(EntryActors);
		FieldInputWarnDroppedOnce(Plan.DroppedCount);
		if (Plan.Fields.IsEmpty())
		{
			return false;
		}

		OutGraph.Fields = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.FieldInputs"), Plan.Fields));
		OutGraph.EntryRanges = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.FieldEntryRanges"), Plan.EntryRanges));
		for (int32 Slot = 0; Slot < TDFluidGpuSolverConstants::MaximumFieldTexturesPerBatch; ++Slot)
		{
			OutGraph.Textures[Slot] = FieldInputResolveSlotTexture(Plan, Slot);
		}
		OutGraph.Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		return true;
	}
}
