#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterface.h"
#include "TDFluidSplatDataInterface.generated.h"

UCLASS(EditInlineNew, Category = "FluidFX", meta = (DisplayName = "TD Fluid Splat"))
class TDGAME_API UTDFluidSplatDataInterface : public UNiagaraDataInterface
{
	GENERATED_BODY()

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Accumulation)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Counters)
		SHADER_PARAMETER(FUintVector4, Layout)
		SHADER_PARAMETER(FVector4f, AxisU)
		SHADER_PARAMETER(FVector4f, AxisV)
		SHADER_PARAMETER(FVector4f, AxisN)
		SHADER_PARAMETER(FVector4f, Scales)
		SHADER_PARAMETER(FVector4f, Strengths)
	END_SHADER_PARAMETER_STRUCT()

	UTDFluidSplatDataInterface(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Category = "FluidFX")
	FName TargetChannelName = TEXT("Default");

	virtual void PostInitProperties() override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
#if WITH_EDITORONLY_DATA
	virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	virtual void GetCommonHLSL(FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
#endif
	virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

protected:
#if WITH_EDITORONLY_DATA
	virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const override;
#endif
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};
