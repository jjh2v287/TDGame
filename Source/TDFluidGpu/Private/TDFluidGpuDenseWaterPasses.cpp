#include "TDFluidGpuDenseWaterPasses.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuDenseWater.h"
#include "TDFluidGpuInputs.h"

CSV_DECLARE_CATEGORY_EXTERN(TDFluidGpu);

class FTDFluidGpuDenseWaterShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FTDFluidGpuDenseWaterShader() {}
	FTDFluidGpuDenseWaterShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

class FTDFluidGpuDenseSeedCS : public FTDFluidGpuDenseWaterShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuDenseSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuDenseSeedCS, FTDFluidGpuDenseWaterShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuDenseWaterParameters>, DenseWaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DenseSeedTexels)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DenseSeedEntryIndices)
		SHADER_PARAMETER(uint32, DenseSeedEntryCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, DenseDepthOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DenseFaceVelocityOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuDenseSeedCS, "/TDFluidGpu/Private/TDFluidGpuDenseWater.usf", "DenseSeedCS", SF_Compute);

class FTDFluidGpuDenseMomentumCS : public FTDFluidGpuDenseWaterShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuDenseMomentumCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuDenseMomentumCS, FTDFluidGpuDenseWaterShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuDenseWaterParameters>, DenseWaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterTerrainField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, PreviousVelocity)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, StampedVelocity)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, DenseDepthIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DenseFaceVelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DenseFaceVelocityOut)
		SHADER_PARAMETER(uint32, SubstepIndex)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuDenseMomentumCS, "/TDFluidGpu/Private/TDFluidGpuDenseWater.usf", "DenseMomentumCS", SF_Compute);

class FTDFluidGpuDenseDepthCS : public FTDFluidGpuDenseWaterShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuDenseDepthCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuDenseDepthCS, FTDFluidGpuDenseWaterShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuDenseWaterParameters>, DenseWaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterTerrainField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, DenseDepthIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DenseFaceVelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, DenseDepthOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DenseCellVelocityOut)
		SHADER_PARAMETER(uint32, SubstepIndex)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuDenseDepthCS, "/TDFluidGpu/Private/TDFluidGpuDenseWater.usf", "DenseDepthCS", SF_Compute);

class FTDFluidGpuDenseExpandCS : public FTDFluidGpuDenseWaterShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuDenseExpandCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuDenseExpandCS, FTDFluidGpuDenseWaterShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuDenseWaterParameters>, DenseWaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterTerrainField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, DenseDepthIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DenseCellVelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, DenseSurfaceOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuDenseExpandCS, "/TDFluidGpu/Private/TDFluidGpuDenseWater.usf", "DenseExpandCS", SF_Compute);

namespace
{
	constexpr int32 DenseCellGroupSize = 8;
	constexpr EPixelFormat DenseCellVelocityFormat = PF_G32R32F;
	constexpr EPixelFormat DenseSurfaceHeightFormat = PF_R32_FLOAT;

	FRDGTextureDesc DenseMakeArrayDesc(int32 Resolution, int32 SliceCount, EPixelFormat Format)
	{
		return FRDGTextureDesc::Create2DArray(FIntPoint(Resolution, Resolution), Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, static_cast<uint16>(FMath::Max(SliceCount, 1)));
	}

	FIntVector DenseMakeGroupCount(int32 Resolution, int32 Count)
	{
		return FIntVector(FMath::DivideAndRoundUp(Resolution, DenseCellGroupSize), FMath::DivideAndRoundUp(Resolution, DenseCellGroupSize), FMath::Max(Count, 1));
	}

	bool DenseHasUsableSeed(const FTDFluidGpuActorInput& Actor)
	{
		return Actor.DenseSeedResolution > 0 && Actor.DenseSeedTexels.Num() == Actor.DenseSeedResolution * Actor.DenseSeedResolution;
	}

	FTDFluidGpuDenseWaterStepPlan DenseMakePlanForSubsteps(const FTDFluidGpuDenseWaterSettings& Settings, float DeltaSeconds, float SmallestCellCm, int32 SubstepCount)
	{
		FTDFluidGpuDenseWaterStepPlan Plan = FTDFluidGpuDenseWaterRules::PlanStep(Settings, DeltaSeconds, SmallestCellCm);
		if (SubstepCount <= Plan.SubstepCount || DeltaSeconds <= 0.0f || SmallestCellCm <= 0.0f)
		{
			return Plan;
		}

		Plan.SubstepCount = SubstepCount;
		Plan.SubstepSeconds = DeltaSeconds / static_cast<float>(SubstepCount);
		const float GravityDepth = FMath::Max(Settings.GravityCmPerSec2 * Settings.MaxFlowDepthCm, 0.0f);
		if (Plan.bReachedCap && GravityDepth > 0.0f)
		{
			const float WaveSpeedLimit = FTDFluidGpuDenseWaterRules::GravityWaveCourantLimit * SmallestCellCm / Plan.SubstepSeconds;
			Plan.GravityScale = FMath::Min(1.0f, WaveSpeedLimit * WaveSpeedLimit / GravityDepth);
		}
		Plan.MaxSpeedCmPerSec = FMath::Min(Settings.MaxSpeedCmPerSec, Settings.CourantNumber * SmallestCellCm / Plan.SubstepSeconds);
		return Plan;
	}

	struct FTDFluidGpuDenseEntryPlan
	{
		FTDFluidGpuDenseWaterSettings Settings;
		float DeltaSeconds = 0.0f;
		float SmallestCellCm = 0.0f;
	};

	FTDFluidGpuDenseEntryPlan DenseMakeEntryPlan(const FTDFluidGpuActorInput& Actor, int32 DenseResolution)
	{
		FTDFluidGpuDenseEntryPlan EntryPlan;
		EntryPlan.Settings = Actor.Settings.DenseWater;
		FTDFluidGpuDenseWaterRules::Sanitize(EntryPlan.Settings);
		EntryPlan.DeltaSeconds = FMath::Clamp(Actor.DeltaTimeSeconds, 0.0f, TDFluidGpuSolverConstants::MaximumStepDeltaTimeSeconds);
		EntryPlan.SmallestCellCm = static_cast<float>(FMath::Min(Actor.Settings.ExtentCm.X, Actor.Settings.ExtentCm.Y)) / static_cast<float>(FMath::Max(DenseResolution, 1));
		return EntryPlan;
	}

	FRDGTextureSRVRef DenseMakeSRV(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
	{
		return GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Texture));
	}

	FRDGTextureUAVRef DenseMakeUAV(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
	{
		return GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Texture));
	}
}

TArray<FTDFluidGpuDenseWaterParameters> MakeTDFluidGpuDenseWaterParameters(const FTDFluidGpuBatch& Batch, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FVector4f>& OutSeedTexels, TArray<uint32>& OutSeedEntryIndices, int32& OutSubstepCapHitCount)
{
	OutSeedTexels.Reset();
	OutSeedEntryIndices.Reset();
	OutSubstepCapHitCount = 0;
	const FTDFluidGpuBatchFrame& Frame = Batch.GetFrame();
	const int32 ResolutionTier = Batch.GetKey().ResolutionTier;
	const int32 DenseDivisor = FTDFluidGpuDenseWaterRules::ResolveDenseDivisor(ResolutionTier);
	const int32 DenseResolution = FMath::Max(ResolutionTier / DenseDivisor, 1);

	TArray<FTDFluidGpuDenseEntryPlan> EntryPlans;
	EntryPlans.Reserve(Frame.Entries.Num());
	int32 BatchSubstepCount = 1;
	for (const FTDFluidGpuSliceEntry& Entry : Frame.Entries)
	{
		const FTDFluidGpuDenseEntryPlan& EntryPlan = EntryPlans.Add_GetRef(DenseMakeEntryPlan(FrameInputs.Actors[Entry.ActorInputIndex], DenseResolution));
		const FTDFluidGpuDenseWaterStepPlan OwnPlan = FTDFluidGpuDenseWaterRules::PlanStep(EntryPlan.Settings, EntryPlan.DeltaSeconds, EntryPlan.SmallestCellCm);
		BatchSubstepCount = FMath::Max(BatchSubstepCount, OwnPlan.SubstepCount);
	}

	TArray<FTDFluidGpuDenseWaterParameters> Parameters;
	Parameters.Reserve(Frame.Entries.Num());
	for (int32 EntryIndex = 0; EntryIndex < Frame.Entries.Num(); ++EntryIndex)
	{
		const FTDFluidGpuSliceEntry& Entry = Frame.Entries[EntryIndex];
		const FTDFluidGpuActorInput& Actor = FrameInputs.Actors[Entry.ActorInputIndex];
		const FTDFluidGpuDenseEntryPlan& EntryPlan = EntryPlans[EntryIndex];
		const FTDFluidGpuDenseWaterSettings& Settings = EntryPlan.Settings;
		const FTDFluidGpuDenseWaterStepPlan Plan = DenseMakePlanForSubsteps(Settings, EntryPlan.DeltaSeconds, EntryPlan.SmallestCellCm, BatchSubstepCount);
		OutSubstepCapHitCount += Plan.bReachedCap ? 1 : 0;

		FTDFluidGpuDenseWaterParameters& EntryParameters = Parameters.AddDefaulted_GetRef();
		EntryParameters.DenseResolution = DenseResolution;
		EntryParameters.DenseDivisor = DenseDivisor;
		EntryParameters.SubstepCount = static_cast<uint32>(BatchSubstepCount);
		EntryParameters.SubstepSeconds = Plan.SubstepSeconds;
		EntryParameters.GravityCmPerSec2 = Settings.GravityCmPerSec2 * Plan.GravityScale;
		EntryParameters.GapFillTendency = Settings.GapFillTendency;
		EntryParameters.PeakAvoidance = Settings.PeakAvoidance;
		EntryParameters.PeakSoftnessCm = Settings.PeakSoftnessCm;
		EntryParameters.SdfRepelCmPerSec2 = Settings.SdfRepelCmPerSec2;
		EntryParameters.SdfRepelBandCm = Settings.SdfRepelBandCm;
		EntryParameters.LinearDragPerSec = Settings.LinearDragPerSec;
		EntryParameters.BedFriction = Settings.BedFriction;
		EntryParameters.WetThresholdCm = Settings.WetThresholdCm;
		EntryParameters.DryDepthCm = Settings.DryDepthCm;
		EntryParameters.FilmDrainCmPerSec = Settings.FilmDrainCmPerSec;
		EntryParameters.MaxFlowDepthCm = Settings.MaxFlowDepthCm;
		EntryParameters.MaxSpeedCmPerSec = Plan.MaxSpeedCmPerSec;
		EntryParameters.MomentumAdvection = Settings.MomentumAdvection;
		EntryParameters.ReservoirBlend = FTDFluidGpuDenseWaterRules::ComputeReservoirBlend(Settings.ReservoirRelaxPerSec, Plan.SubstepSeconds);

		ETDFluidGpuDenseWaterFlags Flags = ETDFluidGpuDenseWaterFlags::None;
		if (Actor.Settings.bOpenWaterBorders)
		{
			Flags |= ETDFluidGpuDenseWaterFlags::UsesOpenBorders;
		}
		const bool bHasSeed = DenseHasUsableSeed(Actor);
		if (bHasSeed)
		{
			Flags |= ETDFluidGpuDenseWaterFlags::HasSeed;
			EntryParameters.SeedTexelOffset = static_cast<uint32>(OutSeedTexels.Num());
			EntryParameters.SeedResolution = Actor.DenseSeedResolution;
			OutSeedTexels.Append(Actor.DenseSeedTexels);
		}
		if (Entry.bNeedsReset || bHasSeed)
		{
			OutSeedEntryIndices.Add(static_cast<uint32>(EntryIndex));
		}
		else
		{
			Flags |= ETDFluidGpuDenseWaterFlags::AppliesStampDelta;
		}
		EntryParameters.Flags = static_cast<uint32>(Flags);
	}
	return Parameters;
}

FTDFluidGpuDenseWaterPassResult AddTDFluidGpuDenseWaterSolvePasses(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const FTDFluidGpuDenseWaterPassInputs& Inputs)
{
	FTDFluidGpuDenseWaterPassResult Result;
	if (!Inputs.Batch || !Inputs.FrameInputs || !ShaderMap)
	{
		return Result;
	}

	const FTDFluidGpuBatch& Batch = *Inputs.Batch;
	const int32 EntryCount = Batch.GetFrame().Entries.Num();
	if (EntryCount == 0)
	{
		return Result;
	}

	const int32 Resolution = Batch.GetKey().ResolutionTier;
	const int32 DenseResolution = FMath::Max(Resolution / FTDFluidGpuDenseWaterRules::ResolveDenseDivisor(Resolution), 1);
	TArray<FVector4f> SeedTexels;
	TArray<uint32> SeedEntryIndices;
	int32 SubstepCapHitCount = 0;
	const TArray<FTDFluidGpuDenseWaterParameters> Parameters = MakeTDFluidGpuDenseWaterParameters(Batch, *Inputs.FrameInputs, SeedTexels, SeedEntryIndices, SubstepCapHitCount);
	const int32 SubstepCount = static_cast<int32>(Parameters[0].SubstepCount);
	Result.SubstepCount = SubstepCount;
	Result.SubstepCapHitCount = SubstepCapHitCount;
	CSV_CUSTOM_STAT(TDFluidGpu, WaterDenseSubsteps, SubstepCount, ECsvCustomStatOp::Max);
	CSV_CUSTOM_STAT(TDFluidGpu, WaterDenseSubstepCapHits, SubstepCapHitCount, ECsvCustomStatOp::Accumulate);

	FRDGBufferSRVRef DenseParameters = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.DenseWaterParameters"), Parameters));
	if (!SeedEntryIndices.IsEmpty())
	{
		if (SeedTexels.IsEmpty())
		{
			SeedTexels.Add(FVector4f::Zero());
		}
		FTDFluidGpuDenseSeedCS::FParameters* SeedParameters = GraphBuilder.AllocParameters<FTDFluidGpuDenseSeedCS::FParameters>();
		SeedParameters->ActorParameters = Inputs.ActorParameters;
		SeedParameters->DenseWaterParameters = DenseParameters;
		SeedParameters->DenseSeedTexels = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.DenseSeedTexels"), SeedTexels));
		SeedParameters->DenseSeedEntryIndices = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.DenseSeedEntryIndices"), SeedEntryIndices));
		SeedParameters->DenseSeedEntryCount = static_cast<uint32>(SeedEntryIndices.Num());
		SeedParameters->DenseDepthOut = DenseMakeUAV(GraphBuilder, Inputs.DenseDepth[0]);
		SeedParameters->DenseFaceVelocityOut = DenseMakeUAV(GraphBuilder, Inputs.DenseFaceVelocity[0]);
		TShaderMapRef<FTDFluidGpuDenseSeedCS> SeedShader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.DenseSeed"), SeedShader, SeedParameters, DenseMakeGroupCount(DenseResolution, SeedEntryIndices.Num()));
	}

	FRDGTextureRef DenseCellVelocity = GraphBuilder.CreateTexture(DenseMakeArrayDesc(DenseResolution, Inputs.SliceCapacity, DenseCellVelocityFormat), TEXT("TDFluidGpu.DenseCellVelocity"));
	FRDGTextureSRVRef CompositeSdfSRV = DenseMakeSRV(GraphBuilder, Inputs.CompositeSdf);
	FRDGTextureSRVRef TerrainFieldSRV = DenseMakeSRV(GraphBuilder, Inputs.TerrainField);
	FRDGTextureSRVRef SurfaceHeightFieldSRV = DenseMakeSRV(GraphBuilder, Inputs.SurfaceHeightField);
	FRDGTextureSRVRef PublishedVelocitySRV = DenseMakeSRV(GraphBuilder, Inputs.PublishedVelocity);
	FRDGTextureSRVRef StampedVelocitySRV = DenseMakeSRV(GraphBuilder, Inputs.StampedVelocity);
	TShaderMapRef<FTDFluidGpuDenseMomentumCS> MomentumShader(ShaderMap);
	TShaderMapRef<FTDFluidGpuDenseDepthCS> DepthShader(ShaderMap);
	const FIntVector DenseGroupCount = DenseMakeGroupCount(DenseResolution, EntryCount);
	int32 StateIndex = 0;
	for (int32 SubstepIndex = 0; SubstepIndex < SubstepCount; ++SubstepIndex)
	{
		const int32 NextStateIndex = 1 - StateIndex;
		FTDFluidGpuDenseMomentumCS::FParameters* MomentumParameters = GraphBuilder.AllocParameters<FTDFluidGpuDenseMomentumCS::FParameters>();
		MomentumParameters->ActorParameters = Inputs.ActorParameters;
		MomentumParameters->WaterParameters = Inputs.WaterParameters;
		MomentumParameters->DenseWaterParameters = DenseParameters;
		MomentumParameters->CompositeSdf = CompositeSdfSRV;
		MomentumParameters->WaterTerrainField = TerrainFieldSRV;
		MomentumParameters->PreviousVelocity = PublishedVelocitySRV;
		MomentumParameters->StampedVelocity = StampedVelocitySRV;
		MomentumParameters->DenseDepthIn = DenseMakeSRV(GraphBuilder, Inputs.DenseDepth[StateIndex]);
		MomentumParameters->DenseFaceVelocityIn = DenseMakeSRV(GraphBuilder, Inputs.DenseFaceVelocity[StateIndex]);
		MomentumParameters->DenseFaceVelocityOut = DenseMakeUAV(GraphBuilder, Inputs.DenseFaceVelocity[NextStateIndex]);
		MomentumParameters->SubstepIndex = static_cast<uint32>(SubstepIndex);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.DenseMomentum"), MomentumShader, MomentumParameters, DenseGroupCount);

		FTDFluidGpuDenseDepthCS::FParameters* DepthParameters = GraphBuilder.AllocParameters<FTDFluidGpuDenseDepthCS::FParameters>();
		DepthParameters->ActorParameters = Inputs.ActorParameters;
		DepthParameters->WaterParameters = Inputs.WaterParameters;
		DepthParameters->DenseWaterParameters = DenseParameters;
		DepthParameters->CompositeSdf = CompositeSdfSRV;
		DepthParameters->WaterTerrainField = TerrainFieldSRV;
		DepthParameters->WaterSurfaceHeightField = SurfaceHeightFieldSRV;
		DepthParameters->DenseDepthIn = DenseMakeSRV(GraphBuilder, Inputs.DenseDepth[StateIndex]);
		DepthParameters->DenseFaceVelocityIn = DenseMakeSRV(GraphBuilder, Inputs.DenseFaceVelocity[NextStateIndex]);
		DepthParameters->DenseDepthOut = DenseMakeUAV(GraphBuilder, Inputs.DenseDepth[NextStateIndex]);
		DepthParameters->DenseCellVelocityOut = DenseMakeUAV(GraphBuilder, DenseCellVelocity);
		DepthParameters->SubstepIndex = static_cast<uint32>(SubstepIndex);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.DenseDepth"), DepthShader, DepthParameters, DenseGroupCount);
		StateIndex = NextStateIndex;
	}

	Result.DenseSurfaceHeight = GraphBuilder.CreateTexture(DenseMakeArrayDesc(Resolution, Inputs.SliceCapacity, DenseSurfaceHeightFormat), TEXT("TDFluidGpu.DenseSurfaceHeight"));
	FTDFluidGpuDenseExpandCS::FParameters* ExpandParameters = GraphBuilder.AllocParameters<FTDFluidGpuDenseExpandCS::FParameters>();
	ExpandParameters->ActorParameters = Inputs.ActorParameters;
	ExpandParameters->WaterParameters = Inputs.WaterParameters;
	ExpandParameters->DenseWaterParameters = DenseParameters;
	ExpandParameters->WaterTerrainField = TerrainFieldSRV;
	ExpandParameters->DenseDepthIn = DenseMakeSRV(GraphBuilder, Inputs.DenseDepth[StateIndex]);
	ExpandParameters->DenseCellVelocityIn = DenseMakeSRV(GraphBuilder, DenseCellVelocity);
	ExpandParameters->VelocityOut = DenseMakeUAV(GraphBuilder, Inputs.PublishedVelocity);
	ExpandParameters->DenseSurfaceOut = DenseMakeUAV(GraphBuilder, Result.DenseSurfaceHeight);
	TShaderMapRef<FTDFluidGpuDenseExpandCS> ExpandShader(ShaderMap);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.DenseExpand"), ExpandShader, ExpandParameters, DenseMakeGroupCount(Resolution, EntryCount));
	Result.FinalStateIndex = StateIndex;
	return Result;
}

void AddTDFluidGpuDenseWaterDepthProbePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuBatch& Batch, int32 EntryIndex, TUniquePtr<FRHIGPUTextureReadback>& OutDepthReadback)
{
	const FTDFluidGpuBatchFrame& Frame = Batch.GetFrame();
	const TRefCountPtr<IPooledRenderTarget>& DenseDepth = Batch.StateTextures.DenseDepth[0];
	if (!Batch.GetKey().bUsesDenseWater || !DenseDepth.IsValid() || !Frame.Parameters.IsValidIndex(EntryIndex))
	{
		return;
	}

	OutDepthReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.DenseDepthProbeReadback"));
	AddEnqueueCopyPass(GraphBuilder, OutDepthReadback.Get(), GraphBuilder.RegisterExternalTexture(DenseDepth, TEXT("TDFluidGpu.DenseDepthProbe")), FResolveRect(), static_cast<uint32>(Frame.Parameters[EntryIndex].SliceIndex));
}
