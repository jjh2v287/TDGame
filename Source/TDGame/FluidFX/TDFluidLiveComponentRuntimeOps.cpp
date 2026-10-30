#include "FluidFX/TDFluidLiveComponent.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TDGame.h"
#include "TDFluidGpuInputs.h"
#include "TextureResource.h"

namespace
{
	const FLinearColor TDFluidOpsEmptyDensityPressureColor(0.0f, 0.5f, 0.0f, 0.0f);
	const FLinearColor TDFluidOpsEmptyVelocityColor(0.5f, 0.5f, 0.0f, 0.0f);
	const FLinearColor TDFluidOpsEmptyWaterSurfaceColor(0.0f, 0.0f, 0.5f, TDFluidGpuSolverConstants::WaterTerrainHeightLimit);

	struct FTDFluidOpsCpuCopySlot
	{
		UTexture2D* SourceTexture = nullptr;
		const TSoftObjectPtr<UTextureRenderTarget2D>* ExternalTarget = nullptr;
		FLinearColor ExpectedClearColor;
		TextureAddress ExpectedAddress = TA_Clamp;
	};

	void TDFluidOpsEnqueueCopyToExternalTarget(UTexture2D& SourceTexture, UTextureRenderTarget2D& Target)
	{
		FTextureResource* SourceResource = SourceTexture.GetResource();
		FTextureRenderTargetResource* TargetResource = Target.GameThread_GetRenderTargetResource();
		if (!SourceResource || !TargetResource)
		{
			return;
		}

		ENQUEUE_RENDER_COMMAND(TDFluidOpsCopyCpuOutputToExternalTarget)(
			[SourceResource, TargetResource](FRHICommandListImmediate& RHICmdList)
			{
				FRHITexture* SourceRHI = SourceResource->GetTextureRHI();
				FRHITexture* TargetRHI = TargetResource->GetRenderTargetTexture();
				if (!SourceRHI || !TargetRHI)
				{
					return;
				}

				FRDGBuilder GraphBuilder(RHICmdList);
				const FRDGTextureRef SourceTextureRef = RegisterExternalTexture(GraphBuilder, SourceRHI, TEXT("TDFluidOpsCpuOutput"));
				const FRDGTextureRef TargetTextureRef = RegisterExternalTexture(GraphBuilder, TargetRHI, TEXT("TDFluidOpsExternalOutputTarget"));
				AddCopyTexturePass(GraphBuilder, SourceTextureRef, TargetTextureRef);
				GraphBuilder.Execute();
			});
	}
}

bool UTDFluidLiveComponent::IsEligibleForVisibilityPause() const
{
	if (SimulationConfig.SimMode == ETDFluidSimMode::SimplePainter || SimulationConfig.bEnableWaterLayer || AcceptsMassInput())
	{
		return false;
	}

	if (!TileMapTargets.IsEmpty() || !DriveNiagaraComponentInTaggedActors.IsNone() || !ParameterCollection.IsNull() || ExternalOutputTargets.HasAnyTarget())
	{
		return false;
	}

	const AActor* OwnerActor = GetOwner();
	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& ExternalTarget : ExternalMaterialTargets)
	{
		const UMaterialInstanceDynamic* TargetMID = ExternalTarget.Get();
		if (TargetMID && TargetMID->GetOuter() != OwnerActor)
		{
			return false;
		}
	}
	return true;
}

void UTDFluidLiveComponent::DiscardPendingInputs()
{
	PendingBrushStamps.Reset();
	PendingMassPoints.Reset();
	DiscardNearRippleSources();
}

FTDFluidSimulationConfig UTDFluidLiveComponent::MakeGpuSolverConfig(int32 GpuResolution) const
{
	return TDFluidRuntimeOps::MakeCellSizeNormalizedConfig(SimulationConfig, GpuResolution, RuntimeOpsSettings, false);
}

bool UTDFluidLiveComponent::CanKeepSolverStateOnReinit(const FTDFluidSimulationConfig& PreviousConfig, bool bWasInitialized) const
{
	if (!RuntimeOpsSettings.bKeepBuffersOnReinit || !bWasInitialized)
	{
		return false;
	}

	const bool bHasSameLayout = PreviousConfig.GridResolution == SimulationConfig.GridResolution
		&& PreviousConfig.SimExtentCm == SimulationConfig.SimExtentCm
		&& PreviousConfig.SolverBackend == SimulationConfig.SolverBackend
		&& PreviousConfig.CpuFallbackResolution == SimulationConfig.CpuFallbackResolution
		&& PreviousConfig.AreaAddressMode == SimulationConfig.AreaAddressMode
		&& PreviousConfig.SimMode == SimulationConfig.SimMode
		&& PreviousConfig.bEnableWaterLayer == SimulationConfig.bEnableWaterLayer;
	const int32 Resolution = GetSimulationResolution();
	if (!bHasSameLayout || Resolution <= 0)
	{
		return false;
	}

	const FVector2D ExtentCm = GetSimulationExtentCm();
	const double MinCellSizeCm = FMath::Min(ExtentCm.X, ExtentCm.Y) / Resolution;
	return FVector2D::Distance(ResolveOwnerCenterWorld(), GetSimulationCenterWorld()) <= MinCellSizeCm;
}

bool UTDFluidLiveComponent::TryBindExternalOutputTarget(TObjectPtr<UTextureRenderTarget2D>& InOutTarget, const TSoftObjectPtr<UTextureRenderTarget2D>& ExternalTarget, int32 Resolution, const FLinearColor& ExpectedClearColor, TextureAddress ExpectedAddress)
{
	UTextureRenderTarget2D* LoadedTarget = ExternalTarget.Get();
	if (IsUsableClaimedExternalTarget(LoadedTarget, Resolution, ExpectedClearColor, ExpectedAddress))
	{
		InOutTarget = LoadedTarget;
		return true;
	}

	if (IsExternalOutputTarget(InOutTarget))
	{
		InOutTarget = nullptr;
	}
	return false;
}

bool UTDFluidLiveComponent::IsUsableClaimedExternalTarget(UTextureRenderTarget2D* Target, int32 Resolution, const FLinearColor& ExpectedClearColor, TextureAddress ExpectedAddress)
{
	if (!Target)
	{
		return false;
	}

	if (TDFluidRuntimeOps::IsUsableExternalOutputTarget(Target, Resolution, ExpectedClearColor, ExpectedAddress))
	{
		return ClaimSharedOutput(Target);
	}

	bool bWasAlreadyWarned = false;
	WarnedExternalOutputTargets.Add(FObjectKey(Target), &bWasAlreadyWarned);
	if (!bWasAlreadyWarned)
	{
		UE_LOG(LogTDGame, Warning, TEXT("FluidFX external output target %s on %s does not match the fluid output (needs RGBA16f %dx%d, UAV, clear color %s, address %d, no mips); using the internal target"),
			*Target->GetName(), *GetNameSafe(GetOwner()), Resolution, Resolution, *ExpectedClearColor.ToString(), static_cast<int32>(ExpectedAddress));
	}
	return false;
}

void UTDFluidLiveComponent::CopyCpuOutputsToExternalTargets()
{
	if (bUsesGpuSolver || !ExternalOutputTargets.HasAnyTarget())
	{
		return;
	}

	const int32 Resolution = Solver.GetResolution();
	const TextureAddress OutputAddress = ResolveOutputTextureAddress();
	const FTDFluidOpsCpuCopySlot CopySlots[] = {
		{ DensityPressureTexture.Get(), &ExternalOutputTargets.DensityPressureTarget, TDFluidOpsEmptyDensityPressureColor, OutputAddress },
		{ VelocityTexture.Get(), &ExternalOutputTargets.VelocityTarget, TDFluidOpsEmptyVelocityColor, OutputAddress },
		{ AuxTexture.Get(), &ExternalOutputTargets.AuxTarget, FLinearColor::Transparent, OutputAddress },
		{ WaterSurfaceTexture.Get(), &ExternalOutputTargets.WaterSurfaceTarget, TDFluidOpsEmptyWaterSurfaceColor, TA_Clamp }
	};

	for (const FTDFluidOpsCpuCopySlot& CopySlot : CopySlots)
	{
		UTexture2D* SourceTexture = CopySlot.SourceTexture;
		if (!SourceTexture || SourceTexture->GetSizeX() != Resolution || SourceTexture->GetSizeY() != Resolution)
		{
			continue;
		}

		UTextureRenderTarget2D* Target = CopySlot.ExternalTarget->Get();
		if (!IsUsableClaimedExternalTarget(Target, Resolution, CopySlot.ExpectedClearColor, CopySlot.ExpectedAddress))
		{
			continue;
		}

		TDFluidOpsEnqueueCopyToExternalTarget(*SourceTexture, *Target);
	}
}

bool UTDFluidLiveComponent::ClaimSharedOutput(const UObject* SharedOutput)
{
	UTDFluidLiveSubsystem* FluidSubsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(this);
	return !FluidSubsystem || FluidSubsystem->ClaimSharedOutput(SharedOutput, this);
}

bool UTDFluidLiveComponent::ShouldWriteSharedParameterCollection(const UObject* SharedParameterCollection)
{
	return !TDFluidRuntimeOps::IsSharedMpcArbitrationEnabled() || ClaimSharedOutput(SharedParameterCollection);
}

bool UTDFluidLiveComponent::IsExternalOutputTarget(const UTextureRenderTarget2D* Target) const
{
	return Target && Target->GetOuter() != this;
}

bool UTDFluidLiveComponent::CanClearGpuOutputTarget(const UTextureRenderTarget2D* Target) const
{
	if (!Target)
	{
		return false;
	}

	if (!IsExternalOutputTarget(Target))
	{
		return true;
	}

	const UTDFluidLiveSubsystem* FluidSubsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(this);
	return !FluidSubsystem || FluidSubsystem->IsSharedOutputClaimedBy(Target, this);
}

void UTDFluidLiveComponent::AppendExternalOutputTargetPaths(TArray<FSoftObjectPath>& InOutPaths) const
{
	const TSoftObjectPtr<UTextureRenderTarget2D>* ExternalTargets[] = {
		&ExternalOutputTargets.DensityPressureTarget,
		&ExternalOutputTargets.VelocityTarget,
		&ExternalOutputTargets.AuxTarget,
		&ExternalOutputTargets.WaterSurfaceTarget
	};

	for (const TSoftObjectPtr<UTextureRenderTarget2D>* ExternalTarget : ExternalTargets)
	{
		if (!ExternalTarget->IsNull())
		{
			InOutPaths.Add(ExternalTarget->ToSoftObjectPath());
		}
	}
}
