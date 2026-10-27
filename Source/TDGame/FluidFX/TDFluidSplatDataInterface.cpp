#include "FluidFX/TDFluidSplatDataInterface.h"

#include "Engine/World.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "NiagaraCompileHashVisitor.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraShaderParametersBuilder.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraTypes.h"
#include "RenderGraphBuilder.h"
#include "TDFluidGpuMassInput.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TDFluidSplatDataInterface)

namespace TDFluidSplatInterface
{
	const FName SplatPointFunctionName(TEXT("SplatPoint"));
	const TCHAR* TemplateShaderFile = TEXT("/TDFluidGpu/Private/TDFluidGpuSplatInterface.ush");
	const TCHAR* MassSplatShaderFile = TEXT("/TDFluidGpu/Private/TDFluidGpuMassSplat.ush");
	constexpr uint32 EnabledFlag = 1u;
	constexpr uint32 WrapsAreaFlag = 2u;
	constexpr uint32 WorldFacingFlag = 4u;
	constexpr uint32 RadiusCapShift = 8u;

	struct alignas(16) FTDFluidSplatInstanceData
	{
		TSharedPtr<FTDFluidMassInputHub, ESPMode::ThreadSafe> Hub;
		FTDFluidMassInputTarget Target;
		FVector LwcTileWorldCm = FVector::ZeroVector;
		bool bHasTarget = false;
	};

	struct FTDFluidSplatDataInterfaceProxy : public FNiagaraDataInterfaceProxy
	{
		virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
		{
			return sizeof(FTDFluidSplatInstanceData);
		}

		virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override
		{
			FTDFluidSplatInstanceData* SourceData = static_cast<FTDFluidSplatInstanceData*>(PerInstanceData);
			InstanceData.Add(Instance, *SourceData);
			SourceData->~FTDFluidSplatInstanceData();
		}

		TMap<FNiagaraSystemInstanceID, FTDFluidSplatInstanceData> InstanceData;
	};

	bool TryFindReadyView(const FTDFluidSplatInstanceData* InstanceData, FTDFluidGpuMassInputTargetView& OutView)
	{
		if (!InstanceData || !InstanceData->bHasTarget || !InstanceData->Hub.IsValid())
		{
			return false;
		}
		if (!InstanceData->Hub->GetRegistry().FindTarget(InstanceData->Target.ActorKey, OutView))
		{
			return false;
		}
		return OutView.bIsReady && OutView.Accumulation.IsValid() && OutView.Counters.IsValid() && OutView.InputResolution > 0;
	}

	uint32 MakeLayoutFlags(const FTDFluidMassInputTarget& Target, const FTDFluidGpuMassInputTargetView& View)
	{
		uint32 Flags = EnabledFlag;
		Flags |= View.bWrapsArea ? WrapsAreaFlag : 0u;
		Flags |= Target.bIsWorldFacing ? WorldFacingFlag : 0u;
		Flags |= static_cast<uint32>(FMath::Clamp(View.RadiusCapCells, 1, TDFluidGpuSolverConstants::MaximumMassSplatRadiusCapCells)) << RadiusCapShift;
		return Flags;
	}

	FVector4f MakeAxis(const FVector& Axis, double Offset)
	{
		return FVector4f(static_cast<float>(Axis.X), static_cast<float>(Axis.Y), static_cast<float>(Axis.Z), static_cast<float>(Offset));
	}

	void BindTarget(FRDGBuilder& GraphBuilder, const FTDFluidSplatInstanceData& InstanceData, const FTDFluidGpuMassInputTargetView& View, UTDFluidSplatDataInterface::FShaderParameters& OutParameters)
	{
		const FTDFluidMassInputTarget& Target = InstanceData.Target;
		const FVector& Tile = InstanceData.LwcTileWorldCm;
		const double OffsetU = FVector::DotProduct(Tile, Target.AxisU) + Target.PlaneCoordinateOffsetCm.X - View.OriginPlaneCm.X;
		const double OffsetV = FVector::DotProduct(Tile, Target.AxisV) + Target.PlaneCoordinateOffsetCm.Y - View.OriginPlaneCm.Y;
		const double OffsetN = FVector::DotProduct(Tile, Target.AxisN) - Target.PlaneDepthCm;
		OutParameters.Accumulation = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(View.Accumulation), PF_R32_UINT);
		OutParameters.Counters = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(View.Counters), PF_R32_UINT);
		OutParameters.Layout = FUintVector4(View.AccumulationBase, View.CounterBase, static_cast<uint32>(View.InputResolution), MakeLayoutFlags(Target, View));
		OutParameters.AxisU = MakeAxis(Target.AxisU, OffsetU);
		OutParameters.AxisV = MakeAxis(Target.AxisV, OffsetV);
		OutParameters.AxisN = MakeAxis(Target.AxisN, OffsetN);
		OutParameters.Scales = FVector4f(static_cast<float>(View.InputCellCm.X), static_cast<float>(View.InputCellCm.Y), static_cast<float>(Target.SlabHalfDepthCm), Target.MaxCellSpeedCmPerSec);
		OutParameters.Strengths = FVector4f(Target.DensityStrength, Target.VelocityStrength, Target.bUsesHeat ? Target.HeatStrength : 0.0f, Target.RadiusScale);
	}

	void BindDisabled(const FNiagaraDataInterfaceSetShaderParametersContext& Context, UTDFluidSplatDataInterface::FShaderParameters& OutParameters)
	{
		FRDGBuilder& GraphBuilder = Context.GetGraphBuilder();
		OutParameters.Accumulation = Context.GetComputeDispatchInterface().GetEmptyBufferUAV(GraphBuilder, PF_R32_UINT);
		OutParameters.Counters = Context.GetComputeDispatchInterface().GetEmptyBufferUAV(GraphBuilder, PF_R32_UINT);
		OutParameters.Layout = FUintVector4(0u, 0u, 0u, 0u);
		OutParameters.AxisU = FVector4f::Zero();
		OutParameters.AxisV = FVector4f::Zero();
		OutParameters.AxisN = FVector4f::Zero();
		OutParameters.Scales = FVector4f::Zero();
		OutParameters.Strengths = FVector4f::Zero();
	}
}

UTDFluidSplatDataInterface::UTDFluidSplatDataInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new TDFluidSplatInterface::FTDFluidSplatDataInterfaceProxy());
}

void UTDFluidSplatDataInterface::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}
	const ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
	FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
}

bool UTDFluidSplatDataInterface::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UTDFluidSplatDataInterface* OtherSplat = CastChecked<const UTDFluidSplatDataInterface>(Other);
	return OtherSplat->TargetChannelName == TargetChannelName;
}

bool UTDFluidSplatDataInterface::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	CastChecked<UTDFluidSplatDataInterface>(Destination)->TargetChannelName = TargetChannelName;
	return true;
}

bool UTDFluidSplatDataInterface::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	TDFluidSplatInterface::FTDFluidSplatInstanceData* InstanceData = new (PerInstanceData) TDFluidSplatInterface::FTDFluidSplatInstanceData();
	UWorld* World = SystemInstance ? SystemInstance->GetWorld() : nullptr;
	UTDFluidLiveSubsystem* LiveSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	if (LiveSubsystem)
	{
		InstanceData->Hub = LiveSubsystem->GetMassInputHub();
	}
	return true;
}

void UTDFluidSplatDataInterface::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	static_cast<TDFluidSplatInterface::FTDFluidSplatInstanceData*>(PerInstanceData)->~FTDFluidSplatInstanceData();
	TDFluidSplatInterface::FTDFluidSplatDataInterfaceProxy* RenderProxy = GetProxyAs<TDFluidSplatInterface::FTDFluidSplatDataInterfaceProxy>();
	ENQUEUE_RENDER_COMMAND(TDFluidSplatRemoveInstance)(
		[RenderProxy, InstanceId = SystemInstance->GetId()](FRHICommandListImmediate&)
		{
			RenderProxy->InstanceData.Remove(InstanceId);
		});
}

int32 UTDFluidSplatDataInterface::PerInstanceDataSize() const
{
	return sizeof(TDFluidSplatInterface::FTDFluidSplatInstanceData);
}

bool UTDFluidSplatDataInterface::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	TDFluidSplatInterface::FTDFluidSplatInstanceData* InstanceData = static_cast<TDFluidSplatInterface::FTDFluidSplatInstanceData*>(PerInstanceData);
	InstanceData->LwcTileWorldCm = FVector(SystemInstance->GetLWCTile()) * FLargeWorldRenderScalar::GetTileSize();
	InstanceData->bHasTarget = InstanceData->Hub.IsValid()
		&& InstanceData->Hub->FindNearestTarget(TargetChannelName, SystemInstance->GetWorldTransform().GetLocation(), InstanceData->Target);
	return false;
}

void UTDFluidSplatDataInterface::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	new (DataForRenderThread) TDFluidSplatInterface::FTDFluidSplatInstanceData(*static_cast<const TDFluidSplatInterface::FTDFluidSplatInstanceData*>(PerInstanceData));
}

#if WITH_EDITORONLY_DATA
void UTDFluidSplatDataInterface::GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const
{
	Super::GetFunctionsInternal(OutFunctions);
	FNiagaraFunctionSignature& Signature = OutFunctions.AddDefaulted_GetRef();
	Signature.Name = TDFluidSplatInterface::SplatPointFunctionName;
	Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SplatInterface")));
	Signature.Inputs.Add_GetRef(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("bExecute"))).SetValue(true);
	Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Position")));
	Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
	Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("RadiusCm")));
	Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("DensityStrength")));
	Signature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("HeatStrength")));
	Signature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Accepted")));
	Signature.bMemberFunction = true;
	Signature.bRequiresExecPin = true;
	Signature.bRequiresContext = false;
	Signature.bWriteFunction = true;
	Signature.bSupportsCPU = false;
	Signature.bSupportsGPU = true;
}

bool UTDFluidSplatDataInterface::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	bool bSucceeded = Super::AppendCompileHash(InVisitor);
	bSucceeded &= InVisitor->UpdateShaderFile(TDFluidSplatInterface::TemplateShaderFile);
	bSucceeded &= InVisitor->UpdateShaderFile(TDFluidSplatInterface::MassSplatShaderFile);
	bSucceeded &= InVisitor->UpdateShaderParameters<FShaderParameters>();
	return bSucceeded;
}

void UTDFluidSplatDataInterface::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += FString::Printf(TEXT("#include \"%s\"\n"), TDFluidSplatInterface::MassSplatShaderFile);
}

void UTDFluidSplatDataInterface::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	const TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("ParameterName"), ParamInfo.DataInterfaceHLSLSymbol },
	};
	AppendTemplateHLSL(OutHLSL, TDFluidSplatInterface::TemplateShaderFile, TemplateArgs);
}

bool UTDFluidSplatDataInterface::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	return FunctionInfo.DefinitionName == TDFluidSplatInterface::SplatPointFunctionName;
}
#endif

void UTDFluidSplatDataInterface::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UTDFluidSplatDataInterface::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	FShaderParameters* Parameters = Context.GetParameterNestedStruct<FShaderParameters>();
	const TDFluidSplatInterface::FTDFluidSplatDataInterfaceProxy& RenderProxy = Context.GetProxy<TDFluidSplatInterface::FTDFluidSplatDataInterfaceProxy>();
	const TDFluidSplatInterface::FTDFluidSplatInstanceData* InstanceData = RenderProxy.InstanceData.Find(Context.GetSystemInstanceID());
	FTDFluidGpuMassInputTargetView View;
	if (!TDFluidSplatInterface::TryFindReadyView(InstanceData, View))
	{
		TDFluidSplatInterface::BindDisabled(Context, *Parameters);
		return;
	}
	TDFluidSplatInterface::BindTarget(Context.GetGraphBuilder(), *InstanceData, View, *Parameters);
}
