#include "FluidFX/TDFluidNiagaraModuleEditorLibrary.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_Niagara.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "NiagaraCommon.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelAsset.h"
#include "NiagaraDataChannelVariable.h"
#include "NiagaraDataChannel_Global.h"
#include "NiagaraDataInterface.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

class FNiagaraEmitterViewModel;
class FNiagaraSystemViewModel;
class UNiagaraNodeOp;
class UNiagaraNodeParameterMapGet;
class UNiagaraNodeParameterMapSet;
class UNiagaraStackEditorData;

namespace UE::Niagara::Wizard::Utilities
{
	NIAGARAEDITOR_API UEdGraphPin* AddReadParameterPin(const FNiagaraTypeDefinition& Type, const FName& Name, UNiagaraNodeParameterMapGet* MapGetNode);
	NIAGARAEDITOR_API UNiagaraNodeFunctionCall* CreateDataInterfaceFunctionNode(const TSubclassOf<UNiagaraDataInterface>& DataInterfaceClass, const FName& FunctionName, UNiagaraGraph* Graph);
}

namespace
{
	const TCHAR* ModuleScriptFactoryClassPath = TEXT("/Script/NiagaraEditor.NiagaraModuleScriptFactory");
	const TCHAR* DataChannelFactoryClassPath = TEXT("/Script/NiagaraEditor.NiagaraDataChannelAssetFactoryNew");
	const TCHAR* SplatDataInterfaceClassPath = TEXT("/Script/TDGame.TDFluidSplatDataInterface");
	const TCHAR* DataChannelWriteClassPath = TEXT("/Script/Niagara.NiagaraDataInterfaceDataChannelWrite");
	const TCHAR* ParameterMapGetClassName = TEXT("NiagaraNodeParameterMapGet");
	const TCHAR* ParameterMapSetClassName = TEXT("NiagaraNodeParameterMapSet");
	const TCHAR* ParameterMapInputClassName = TEXT("NiagaraNodeInput");
	const TCHAR* ChannelVariableStructName = TEXT("NiagaraDataChannelVariable");
	const FName SplatFunctionName(TEXT("SplatPoint"));
	const FName AppendFunctionName(TEXT("Append"));
	const FName AppendExecutePinName(TEXT("Emit"));
	const FName SplatExecutePinName(TEXT("bExecute"));
	const FName DynamicAddPinSubCategory(TEXT("DynamicAddPin"));
	const FName MiscPinCategory(TEXT("Misc"));
	const FString SplatInterfaceInputName(TEXT("SplatInterface"));
	const FString DataChannelWriterInputName(TEXT("DataChannelWriter"));
	const FString ExecuteInputName(TEXT("bExecute"));
	const FString ModuleNamespacePrefix(TEXT("Module."));
	constexpr float DefaultRadiusCm = 25.0f;
	constexpr float DefaultStrength = 1.0f;

	struct FTDMassModuleInput
	{
		FName FunctionPinName;
		FNiagaraTypeDefinition Type;
		TArray<uint8> DefaultBytes;
	};

	template <typename TValue>
	TArray<uint8> MakeDefaultBytes(const TValue& Value)
	{
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(TValue));
		return Bytes;
	}

	struct FTDMassModuleGraph
	{
		UNiagaraScript* Script = nullptr;
		UNiagaraGraph* Graph = nullptr;
		UEdGraphNode* InputNode = nullptr;
		UEdGraphNode* MapGetNode = nullptr;
		UEdGraphNode* MapSetNode = nullptr;
	};

	bool CanCreateAssetAt(const FString& PackagePath, FString& OutReport)
	{
		if (!FPackageName::IsValidLongPackageName(PackagePath))
		{
			OutReport = FString::Printf(TEXT("invalid package path: %s"), *PackagePath);
			return false;
		}
		if (FindPackage(nullptr, *PackagePath) || FPackageName::DoesPackageExist(PackagePath))
		{
			OutReport = FString::Printf(TEXT("asset already exists (no overwrite): %s"), *PackagePath);
			return false;
		}
		return true;
	}

	UObject* CreateAssetWithFactory(const FString& PackagePath, UClass* AssetClass, const TCHAR* FactoryClassPath, FString& OutReport)
	{
		UClass* FactoryClass = FindObject<UClass>(nullptr, FactoryClassPath);
		if (!FactoryClass || !FactoryClass->IsChildOf(UFactory::StaticClass()))
		{
			OutReport = FString::Printf(TEXT("factory class not found: %s"), FactoryClassPath);
			return nullptr;
		}
		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* CreatedAsset = AssetTools.CreateAsset(FPackageName::GetShortName(PackagePath), FPackageName::GetLongPackagePath(PackagePath), AssetClass, Factory);
		if (!CreatedAsset)
		{
			OutReport = FString::Printf(TEXT("asset creation failed: %s"), *PackagePath);
		}
		return CreatedAsset;
	}

	UClass* FindDataInterfaceClass(const TCHAR* ClassPath, FString& OutReport)
	{
		UClass* DataInterfaceClass = FindObject<UClass>(nullptr, ClassPath);
		if (!DataInterfaceClass || !DataInterfaceClass->IsChildOf(UNiagaraDataInterface::StaticClass()))
		{
			OutReport = FString::Printf(TEXT("data interface class not found: %s"), ClassPath);
			return nullptr;
		}
		return DataInterfaceClass;
	}

	UEdGraphNode* FindSingleNodeByClassName(UEdGraph* Graph, const TCHAR* ClassName)
	{
		UEdGraphNode* FoundNode = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || Node->GetClass()->GetName() != ClassName)
			{
				continue;
			}
			if (FoundNode)
			{
				return nullptr;
			}
			FoundNode = Node;
		}
		return FoundNode;
	}

	UEdGraphPin* FindParameterMapPin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindDynamicAddPin(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == MiscPinCategory && Pin->PinType.PinSubCategory == DynamicAddPinSubCategory)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	bool ConnectPins(UEdGraph* Graph, UEdGraphPin* FromPin, UEdGraphPin* ToPin)
	{
		return FromPin && ToPin && Graph->GetSchema()->TryCreateConnection(FromPin, ToPin);
	}

	bool OpenNewModuleGraph(const FString& PackagePath, FTDMassModuleGraph& OutGraph, FString& OutReport)
	{
		OutGraph.Script = Cast<UNiagaraScript>(CreateAssetWithFactory(PackagePath, UNiagaraScript::StaticClass(), ModuleScriptFactoryClassPath, OutReport));
		if (!OutGraph.Script)
		{
			return false;
		}
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(OutGraph.Script->GetLatestSource());
		if (!Source || !Source->NodeGraph)
		{
			OutReport = FString::Printf(TEXT("module script has no graph: %s"), *PackagePath);
			return false;
		}
		OutGraph.Graph = Source->NodeGraph;
		OutGraph.InputNode = FindSingleNodeByClassName(OutGraph.Graph, ParameterMapInputClassName);
		OutGraph.MapGetNode = FindSingleNodeByClassName(OutGraph.Graph, ParameterMapGetClassName);
		OutGraph.MapSetNode = FindSingleNodeByClassName(OutGraph.Graph, ParameterMapSetClassName);
		if (!OutGraph.InputNode || !OutGraph.MapGetNode || !OutGraph.MapSetNode)
		{
			OutReport = FString::Printf(TEXT("default module graph is not input+map get+map set (one each): %s"), *PackagePath);
			return false;
		}
		if (FVersionedNiagaraScriptData* ScriptData = OutGraph.Script->GetLatestScriptData())
		{
			ScriptData->ModuleUsageBitmask = ENiagaraScriptUsageMask::Particle;
		}
		return true;
	}

	UEdGraphPin* AddModuleInputPin(FTDMassModuleGraph& ModuleGraph, const FNiagaraTypeDefinition& Type, const FString& InputName)
	{
		UNiagaraNodeParameterMapGet* MapGetNode = reinterpret_cast<UNiagaraNodeParameterMapGet*>(ModuleGraph.MapGetNode);
		return UE::Niagara::Wizard::Utilities::AddReadParameterPin(Type, FName(*(ModuleNamespacePrefix + InputName)), MapGetNode);
	}

	bool SetModuleInputDefault(FTDMassModuleGraph& ModuleGraph, const FString& ModuleInputName, const TArray<uint8>& DefaultBytes)
	{
		UNiagaraScriptVariable* ScriptVariable = ModuleGraph.Graph->GetScriptVariable(FName(*(ModuleNamespacePrefix + ModuleInputName)));
		if (!ScriptVariable || DefaultBytes.IsEmpty() || ScriptVariable->Variable.GetSizeInBytes() != DefaultBytes.Num())
		{
			return false;
		}
		ScriptVariable->Modify();
		ScriptVariable->DefaultMode = ENiagaraDefaultMode::Value;
		ScriptVariable->SetDefaultValueData(DefaultBytes.GetData());
		FPropertyChangedEvent DefaultModeChangedEvent(FindFProperty<FProperty>(UNiagaraScriptVariable::StaticClass(), GET_MEMBER_NAME_CHECKED(UNiagaraScriptVariable, DefaultMode)));
		ScriptVariable->PostEditChangeProperty(DefaultModeChangedEvent);
		const uint8* StoredBytes = ScriptVariable->GetDefaultValueData();
		return StoredBytes && FMemory::Memcmp(StoredBytes, DefaultBytes.GetData(), DefaultBytes.Num()) == 0;
	}

	UNiagaraNodeFunctionCall* AddDataInterfaceCall(FTDMassModuleGraph& ModuleGraph, UClass* DataInterfaceClass, const FName& FunctionName, const FString& DataInterfaceInputName, FString& OutReport)
	{
		UNiagaraNodeFunctionCall* FunctionNode = UE::Niagara::Wizard::Utilities::CreateDataInterfaceFunctionNode(DataInterfaceClass, FunctionName, ModuleGraph.Graph);
		if (!FunctionNode)
		{
			OutReport = FString::Printf(TEXT("function %s not found on %s"), *FunctionName.ToString(), *DataInterfaceClass->GetName());
			return nullptr;
		}
		UEdGraphPin* DataInterfacePin = AddModuleInputPin(ModuleGraph, FNiagaraTypeDefinition(DataInterfaceClass), DataInterfaceInputName);
		FunctionNode->AutowireNewNode(DataInterfacePin);
		UEdGraphPin* InputMapPin = FindParameterMapPin(ModuleGraph.InputNode, EGPD_Output);
		UEdGraphPin* FunctionMapInPin = FindParameterMapPin(FunctionNode, EGPD_Input);
		UEdGraphPin* FunctionMapOutPin = FindParameterMapPin(FunctionNode, EGPD_Output);
		UEdGraphPin* MapSetInPin = FindParameterMapPin(ModuleGraph.MapSetNode, EGPD_Input);
		if (!ConnectPins(ModuleGraph.Graph, InputMapPin, FunctionMapInPin) || !ConnectPins(ModuleGraph.Graph, FunctionMapOutPin, MapSetInPin))
		{
			OutReport = FString::Printf(TEXT("could not chain %s between parameter map input and map set"), *FunctionName.ToString());
			return nullptr;
		}
		if (!DataInterfacePin || DataInterfacePin->LinkedTo.Num() == 0)
		{
			OutReport = FString::Printf(TEXT("data interface input %s did not wire to %s"), *DataInterfaceInputName, *FunctionName.ToString());
			return nullptr;
		}
		return FunctionNode;
	}

	bool ConnectModuleInput(FTDMassModuleGraph& ModuleGraph, UEdGraphNode* FunctionNode, const FTDMassModuleInput& Input, const FString& ModuleInputName, TArray<FString>& OutDefaultsNotSet, FString& OutReport)
	{
		UEdGraphPin* FunctionPin = FunctionNode->FindPin(Input.FunctionPinName, EGPD_Input);
		if (!FunctionPin)
		{
			OutReport = FString::Printf(TEXT("function input pin missing: %s"), *Input.FunctionPinName.ToString());
			return false;
		}
		UEdGraphPin* ModulePin = AddModuleInputPin(ModuleGraph, Input.Type, ModuleInputName);
		if (!ConnectPins(ModuleGraph.Graph, ModulePin, FunctionPin))
		{
			OutReport = FString::Printf(TEXT("could not connect Module.%s to %s"), *ModuleInputName, *Input.FunctionPinName.ToString());
			return false;
		}
		if (!Input.DefaultBytes.IsEmpty() && !SetModuleInputDefault(ModuleGraph, ModuleInputName, Input.DefaultBytes))
		{
			OutDefaultsNotSet.Add(ModuleInputName);
		}
		return true;
	}

	FString DescribeDefaultsNotSet(const TArray<FString>& DefaultsNotSet)
	{
		return DefaultsNotSet.IsEmpty() ? FString(TEXT("none")) : FString::Join(DefaultsNotSet, TEXT(","));
	}

	void FinishModuleGraph(FTDMassModuleGraph& ModuleGraph)
	{
		ModuleGraph.Graph->NotifyGraphChanged();
		ModuleGraph.Script->MarkPackageDirty();
	}

	TArray<uint8> DefaultBytesForChannelInput(const FName& VariableName)
	{
		if (VariableName == FName(TEXT("RadiusCm")))
		{
			return MakeDefaultBytes(DefaultRadiusCm);
		}
		if (VariableName == FName(TEXT("DensityStrength")) || VariableName == FName(TEXT("HeatStrength")))
		{
			return MakeDefaultBytes(DefaultStrength);
		}
		return TArray<uint8>();
	}

	void AddChannelVariable(TArray<FNiagaraDataChannelVariable>& Variables, const TCHAR* Name, const FNiagaraTypeDefinition& Type)
	{
		FNiagaraDataChannelVariable Variable;
		Variable.SetName(FName(Name));
		Variable.SetType(FNiagaraDataChannelVariable::ToDataChannelType(Type));
		Variables.Add(Variable);
	}
}

bool UTDFluidNiagaraModuleEditorLibrary::CreateMassInputDataChannel(const FString& PackagePath, FString& OutReport)
{
	if (!CanCreateAssetAt(PackagePath, OutReport))
	{
		return false;
	}
	FObjectProperty* ChannelProperty = FindFProperty<FObjectProperty>(UNiagaraDataChannelAsset::StaticClass(), TEXT("DataChannel"));
	FArrayProperty* VariablesProperty = FindFProperty<FArrayProperty>(UNiagaraDataChannel::StaticClass(), TEXT("ChannelVariables"));
	FStructProperty* VariableStructProperty = VariablesProperty ? CastField<FStructProperty>(VariablesProperty->Inner) : nullptr;
	if (!ChannelProperty || !VariableStructProperty || VariableStructProperty->Struct->GetName() != ChannelVariableStructName)
	{
		OutReport = TEXT("data channel reflection layout changed (DataChannel or ChannelVariables)");
		return false;
	}
	UNiagaraDataChannelAsset* ChannelAsset = Cast<UNiagaraDataChannelAsset>(CreateAssetWithFactory(PackagePath, UNiagaraDataChannelAsset::StaticClass(), DataChannelFactoryClassPath, OutReport));
	if (!ChannelAsset)
	{
		return false;
	}
	ChannelAsset->Modify();
	UNiagaraDataChannel_Global* Channel = NewObject<UNiagaraDataChannel_Global>(ChannelAsset, NAME_None, RF_Transactional);
	ChannelProperty->SetObjectPropertyValue_InContainer(ChannelAsset, Channel);
	TArray<FNiagaraDataChannelVariable>& Variables = *VariablesProperty->ContainerPtrToValuePtr<TArray<FNiagaraDataChannelVariable>>(Channel);
	Variables.Reset();
	AddChannelVariable(Variables, TEXT("Position"), FNiagaraTypeDefinition::GetPositionDef());
	AddChannelVariable(Variables, TEXT("Velocity"), FNiagaraTypeDefinition::GetVec3Def());
	AddChannelVariable(Variables, TEXT("RadiusCm"), FNiagaraTypeDefinition::GetFloatDef());
	AddChannelVariable(Variables, TEXT("DensityStrength"), FNiagaraTypeDefinition::GetFloatDef());
	AddChannelVariable(Variables, TEXT("HeatStrength"), FNiagaraTypeDefinition::GetFloatDef());
	Channel->PostEditChange();
	ChannelAsset->PostEditChange();
	ChannelAsset->MarkPackageDirty();
	if (ChannelAsset->Get() != Channel || Channel->GetVariables().Num() != 5)
	{
		OutReport = FString::Printf(TEXT("data channel read back mismatch: channel=%d variables=%d"), ChannelAsset->Get() == Channel ? 1 : 0, Channel->GetVariables().Num());
		return false;
	}
	TArray<FString> VariableDescriptions;
	for (const FNiagaraDataChannelVariable& Variable : Channel->GetVariables())
	{
		VariableDescriptions.Add(FString::Printf(TEXT("%s:%s"), *Variable.GetName().ToString(), *Variable.GetType().GetName()));
	}
	OutReport = FString::Printf(TEXT("created %s class=%s keep_previous_frame=%d variables=%s"), *PackagePath, *Channel->GetClass()->GetName(),
		Channel->KeepPreviousFrameData() ? 1 : 0, *FString::Join(VariableDescriptions, TEXT(",")));
	return true;
}

bool UTDFluidNiagaraModuleEditorLibrary::CreateMassSplatModuleScript(const FString& PackagePath, FString& OutReport)
{
	if (!CanCreateAssetAt(PackagePath, OutReport))
	{
		return false;
	}
	UClass* SplatInterfaceClass = FindDataInterfaceClass(SplatDataInterfaceClassPath, OutReport);
	if (!SplatInterfaceClass)
	{
		return false;
	}
	FTDMassModuleGraph ModuleGraph;
	if (!OpenNewModuleGraph(PackagePath, ModuleGraph, OutReport))
	{
		return false;
	}
	UNiagaraNodeFunctionCall* FunctionNode = AddDataInterfaceCall(ModuleGraph, SplatInterfaceClass, SplatFunctionName, SplatInterfaceInputName, OutReport);
	if (!FunctionNode)
	{
		return false;
	}
	const TArray<FTDMassModuleInput> Inputs = {
		{SplatExecutePinName, FNiagaraTypeDefinition::GetBoolDef(), MakeDefaultBytes(FNiagaraBool(true))},
		{FName(TEXT("Position")), FNiagaraTypeDefinition::GetPositionDef(), TArray<uint8>()},
		{FName(TEXT("Velocity")), FNiagaraTypeDefinition::GetVec3Def(), TArray<uint8>()},
		{FName(TEXT("RadiusCm")), FNiagaraTypeDefinition::GetFloatDef(), MakeDefaultBytes(DefaultRadiusCm)},
		{FName(TEXT("DensityStrength")), FNiagaraTypeDefinition::GetFloatDef(), MakeDefaultBytes(DefaultStrength)},
		{FName(TEXT("HeatStrength")), FNiagaraTypeDefinition::GetFloatDef(), MakeDefaultBytes(DefaultStrength)},
	};
	TArray<FString> DefaultsNotSet;
	for (const FTDMassModuleInput& Input : Inputs)
	{
		if (!ConnectModuleInput(ModuleGraph, FunctionNode, Input, Input.FunctionPinName.ToString(), DefaultsNotSet, OutReport))
		{
			return false;
		}
	}
	FinishModuleGraph(ModuleGraph);
	OutReport = FString::Printf(TEXT("created %s function=%s module_inputs=%d defaults_not_set=%s"), *PackagePath, *SplatFunctionName.ToString(),
		Inputs.Num() + 1, *DescribeDefaultsNotSet(DefaultsNotSet));
	return true;
}

bool UTDFluidNiagaraModuleEditorLibrary::CreateDataChannelAppendModuleScript(const FString& PackagePath, const FString& DataChannelPath, FString& OutReport)
{
	if (!CanCreateAssetAt(PackagePath, OutReport))
	{
		return false;
	}
	UClass* WriterClass = FindDataInterfaceClass(DataChannelWriteClassPath, OutReport);
	if (!WriterClass)
	{
		return false;
	}
	const UNiagaraDataChannelAsset* ChannelAsset = Cast<UNiagaraDataChannelAsset>(FSoftObjectPath(DataChannelPath).ResolveObject());
	const UNiagaraDataChannel* Channel = ChannelAsset ? ChannelAsset->Get() : nullptr;
	if (!Channel || Channel->GetVariables().IsEmpty())
	{
		OutReport = FString::Printf(TEXT("data channel not loaded or empty (load it before calling): %s"), *DataChannelPath);
		return false;
	}
	FTDMassModuleGraph ModuleGraph;
	if (!OpenNewModuleGraph(PackagePath, ModuleGraph, OutReport))
	{
		return false;
	}
	UNiagaraNodeFunctionCall* FunctionNode = AddDataInterfaceCall(ModuleGraph, WriterClass, AppendFunctionName, DataChannelWriterInputName, OutReport);
	if (!FunctionNode)
	{
		return false;
	}
	TArray<FString> DefaultsNotSet;
	const FTDMassModuleInput ExecuteInput{AppendExecutePinName, FNiagaraTypeDefinition::GetBoolDef(), MakeDefaultBytes(FNiagaraBool(true))};
	if (!ConnectModuleInput(ModuleGraph, FunctionNode, ExecuteInput, ExecuteInputName, DefaultsNotSet, OutReport))
	{
		return false;
	}
	for (const FNiagaraDataChannelVariable& Variable : Channel->GetVariables())
	{
		const FNiagaraTypeDefinition SimulationType = Variable.GetType().IsEnum()
			? Variable.GetType()
			: FNiagaraTypeDefinition(FNiagaraTypeHelper::GetSWCStruct(Variable.GetType().GetScriptStruct()));
		UEdGraphPin* ModulePin = AddModuleInputPin(ModuleGraph, SimulationType, Variable.GetName().ToString());
		UEdGraphPin* AddPin = FindDynamicAddPin(FunctionNode);
		if (!ConnectPins(ModuleGraph.Graph, ModulePin, AddPin) || !FunctionNode->FindPin(Variable.GetName(), EGPD_Input))
		{
			OutReport = FString::Printf(TEXT("could not add Append pin for %s"), *Variable.GetName().ToString());
			return false;
		}
		const TArray<uint8> DefaultBytes = DefaultBytesForChannelInput(Variable.GetName());
		if (!DefaultBytes.IsEmpty() && !SetModuleInputDefault(ModuleGraph, Variable.GetName().ToString(), DefaultBytes))
		{
			DefaultsNotSet.Add(Variable.GetName().ToString());
		}
	}
	FinishModuleGraph(ModuleGraph);
	OutReport = FString::Printf(TEXT("created %s function=%s channel=%s module_inputs=%d defaults_not_set=%s"), *PackagePath, *AppendFunctionName.ToString(),
		*DataChannelPath, Channel->GetVariables().Num() + 2, *DescribeDefaultsNotSet(DefaultsNotSet));
	return true;
}

FString UTDFluidNiagaraModuleEditorLibrary::RecompileNiagaraSystemFully(UNiagaraSystem* System)
{
	if (!System || !System->GetSystemSpawnScript() || !System->GetSystemUpdateScript())
	{
		return TEXT("attributes_match=0 ready_to_run=0 error=missing_system_or_system_scripts");
	}

	System->RequestCompile(true);
	System->WaitForCompilationComplete(true, false);
	const TArray<FNiagaraVariableBase>& SpawnAttributes = System->GetSystemSpawnScript()->GetVMExecutableData().Attributes;
	const TArray<FNiagaraVariableBase>& UpdateAttributes = System->GetSystemUpdateScript()->GetVMExecutableData().Attributes;
	return FString::Printf(TEXT("attributes_match=%d ready_to_run=%d spawn_attributes=%d update_attributes=%d"),
		SpawnAttributes == UpdateAttributes ? 1 : 0, System->IsReadyToRun() ? 1 : 0, SpawnAttributes.Num(), UpdateAttributes.Num());
}
