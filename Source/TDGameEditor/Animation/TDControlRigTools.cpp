#include "Animation/TDControlRigTools.h"

#include "AnimationDataSource.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "ControlRigObjectBinding.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/TDAnimationAuthoringCommon.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "Rigs/FKControlRig.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	FString SerializeTDControlRigResult(const TSharedRef<FJsonObject>& Result)
	{
		FString Json;
		FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
		return Json;
	}

	FString FailTDControlRigRequest(const FString& Error)
	{
		UE_LOG(LogTDAnimAuthoring, Warning, TEXT("%s failed: %s"), TDActiveAuthoringTool(), *Error);
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), Error);
		return SerializeTDControlRigResult(Result);
	}

	bool ReadTDControlRigInteger(const FJsonObject& Request, const FString& Field, int32 Minimum, int32 Maximum, int32& Value)
	{
		if (!Request.HasField(Field))
		{
			return true;
		}

		double Number = 0.0;
		if (!Request.TryGetNumberField(Field, Number) || !FMath::IsFinite(Number)
			|| Number < Minimum || Number > Maximum || Number != FMath::FloorToDouble(Number))
		{
			return false;
		}

		Value = static_cast<int32>(Number);
		return true;
	}
}

FString UTDControlRigTools::CreateFKSequence(const FString& RequestJson)
{
	const FTDAuthoringToolScope ToolScope(TEXT("CreateFKSequence"), RequestJson);
	if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
	{
		return FailTDControlRigRequest(TEXT("CreateFKSequence requires the editor game thread with PIE stopped."));
	}

	if (RequestJson.Len() > 16384)
	{
		return FailTDControlRigRequest(TEXT("RequestJson exceeds the 16384-character limit."));
	}

	TSharedPtr<FJsonObject> Request;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RequestJson), Request) || !Request.IsValid())
	{
		return FailTDControlRigRequest(TEXT("RequestJson must be a valid JSON object."));
	}

	const TSet<FString> AllowedFields = {TEXT("asset_path"), TEXT("skeletal_mesh"), TEXT("fps"), TEXT("num_frames"), TEXT("save")};
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Request->Values)
	{
		if (!AllowedFields.Contains(Field.Key))
		{
			return FailTDControlRigRequest(FString::Printf(TEXT("Unknown field: %s"), *Field.Key));
		}
	}

	FString AssetPath;
	if (!Request->TryGetStringField(TEXT("asset_path"), AssetPath) || !AssetPath.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive)
		|| !FPackageName::IsValidLongPackageName(AssetPath) || AssetPath.Contains(TEXT(".")))
	{
		return FailTDControlRigRequest(TEXT("asset_path must be a new /Game package path without an extension or object suffix."));
	}

	const FString AssetObjectPath = AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
	if (FindPackage(nullptr, *AssetPath) || FPackageName::DoesPackageExist(AssetPath) || FindObject<UObject>(nullptr, *AssetObjectPath))
	{
		return FailTDControlRigRequest(TEXT("The output package already exists; choose a new asset_path. Overwrites are not supported."));
	}

	int32 FramesPerSecond = 30;
	int32 FrameCount = 30;
	if (!ReadTDControlRigInteger(*Request, TEXT("fps"), 1, 240, FramesPerSecond)
		|| !ReadTDControlRigInteger(*Request, TEXT("num_frames"), 2, 18000, FrameCount))
	{
		return FailTDControlRigRequest(TEXT("fps must be an integer from 1 to 240 and num_frames an integer from 2 to 18000."));
	}

	bool bShouldSave = true;
	if (Request->HasField(TEXT("save")) && !Request->TryGetBoolField(TEXT("save"), bShouldSave))
	{
		return FailTDControlRigRequest(TEXT("save must be a JSON boolean."));
	}

	FString SkeletalMeshPath;
	if (!Request->TryGetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath))
	{
		return FailTDControlRigRequest(TEXT("skeletal_mesh must be an existing SkeletalMesh package or object path."));
	}

	if (FPackageName::IsValidLongPackageName(SkeletalMeshPath, true))
	{
		SkeletalMeshPath += TEXT(".") + FPackageName::GetLongPackageAssetName(SkeletalMeshPath);
	}

	if (!FPackageName::IsValidObjectPath(SkeletalMeshPath) || SkeletalMeshPath.Contains(TEXT(":")))
	{
		return FailTDControlRigRequest(TEXT("skeletal_mesh is not a valid asset object path."));
	}

	USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
	if (!SkeletalMesh || !SkeletalMesh->GetSkeleton() || SkeletalMesh->GetRefSkeleton().GetNum() == 0)
	{
		return FailTDControlRigRequest(TEXT("skeletal_mesh must resolve to a SkeletalMesh with a skeleton and at least one bone."));
	}

	const TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	Sequence->Initialize();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate DisplayRate(FramesPerSecond, 1);
	const FFrameRate TickResolution(FramesPerSecond * 1000, 1);
	MovieScene->SetDisplayRate(DisplayRate);
	MovieScene->SetTickResolutionDirectly(TickResolution);
	MovieScene->SetPlaybackRange(0, FrameCount * 1000);
	const double Duration = static_cast<double>(FrameCount) / FramesPerSecond;
	MovieScene->SetWorkingRange(0.0, Duration);
	MovieScene->SetViewRange(0.0, Duration);

	ASkeletalMeshActor* ActorTemplate = NewObject<ASkeletalMeshActor>(MovieScene, TEXT("TDAnimationCharacter"), RF_Transactional);
	USkeletalMeshComponent* SkeletalMeshComponent = ActorTemplate->GetSkeletalMeshComponent();
	SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
	SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalMeshComponent->SetSimulatePhysics(false);
	SkeletalMeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	const FGuid Binding = MovieScene->AddSpawnable(SkeletalMesh->GetName(), *ActorTemplate);

	UMovieSceneSpawnTrack* SpawnTrack = MovieScene->AddTrack<UMovieSceneSpawnTrack>(Binding);
	UMovieSceneControlRigParameterTrack* RigTrack = MovieScene->AddTrack<UMovieSceneControlRigParameterTrack>(Binding);
	if (!SpawnTrack || !RigTrack)
	{
		return FailTDControlRigRequest(TEXT("The engine could not create the spawn and Control Rig tracks. No output asset was created."));
	}

	UMovieSceneSpawnSection* SpawnSection = Cast<UMovieSceneSpawnSection>(SpawnTrack->CreateNewSection());
	if (!SpawnSection)
	{
		return FailTDControlRigRequest(TEXT("The engine could not create the spawn section. No output asset was created."));
	}

	SpawnSection->GetChannel().SetDefault(true);
	SpawnSection->SetRange(MovieScene->GetPlaybackRange());
	SpawnTrack->AddSection(*SpawnSection);

	UFKControlRig* ControlRig = NewObject<UFKControlRig>(RigTrack, TEXT("FKControlRig"), RF_Transactional);
	ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
	ControlRig->GetObjectBinding()->BindToObject(SkeletalMeshComponent);
	ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, SkeletalMeshComponent);
	ControlRig->Initialize();
	ControlRig->Evaluate_AnyThread();
	const TArray<FName> ControlNames = ControlRig->GetControlNames();
	if (ControlNames.IsEmpty())
	{
		return FailTDControlRigRequest(TEXT("The native FK Control Rig generated no controls for this mesh. No output asset was created."));
	}

	UMovieSceneSection* RigSection = RigTrack->CreateControlRigSection(0, ControlRig, true);
	if (!RigSection)
	{
		return FailTDControlRigRequest(TEXT("The engine could not create the Control Rig section. No output asset was created."));
	}

	RigSection->SetRange(MovieScene->GetPlaybackRange());
	RigTrack->SetTrackName(TEXT("FKControlRig"));
	RigTrack->SetDisplayName(FText::FromString(TEXT("FK Control Rig")));

	FTDAuthoringPackageScope PackageScope(AssetPath);
	if (!Sequence->Rename(*FPackageName::GetLongPackageAssetName(AssetPath), PackageScope.Get(), REN_DontCreateRedirectors | REN_NonTransactional))
	{
		return FailTDControlRigRequest(TEXT("The engine could not assign the output package."));
	}
	PackageScope.Track(Sequence.Get());
	PackageScope.Commit();

	Sequence->SetFlags(RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Sequence.Get());
	Sequence->MarkPackageDirty();

	bool bIsSaved = false;
	if (bShouldSave)
	{
		FSavePackageArgs SaveArguments;
		SaveArguments.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArguments.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
		bIsSaved = UPackage::SavePackage(PackageScope.Get(), Sequence.Get(), *Filename, SaveArguments);
	}

	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), !bShouldSave || bIsSaved);
	Result->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
	Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
	Result->SetStringField(TEXT("binding"), Binding.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetStringField(TEXT("skeletal_mesh"), SkeletalMesh->GetPathName());
	Result->SetStringField(TEXT("control_rig_asset_path"), ControlRig->GetClass()->GetPathName());
	Result->SetStringField(TEXT("control_rig"), ControlRig->GetPathName());
	Result->SetNumberField(TEXT("fps"), FramesPerSecond);
	Result->SetNumberField(TEXT("num_frames"), FrameCount);
	Result->SetNumberField(TEXT("control_count"), ControlNames.Num());
	Result->SetBoolField(TEXT("saved"), bIsSaved);
	TArray<TSharedPtr<FJsonValue>> Controls;
	Controls.Reserve(ControlNames.Num());
	for (const FName ControlName : ControlNames)
	{
		Controls.Add(MakeShared<FJsonValueString>(ControlName.ToString()));
	}
	Result->SetArrayField(TEXT("controls"), Controls);
	if (bShouldSave && !bIsSaved)
	{
		Result->SetStringField(TEXT("error"), TEXT("The sequence was created in memory, but saving failed. Inspect asset_path in the editor; retry with a new path only if a separate asset is intended."));
		UE_LOG(LogTDAnimAuthoring, Warning, TEXT("CreateFKSequence: created %s but saving it failed."), *Sequence->GetPathName());
	}
	else
	{
		UE_LOG(LogTDAnimAuthoring, Log, TEXT("CreateFKSequence: created %s with %d controls (saved: %s)."), *Sequence->GetPathName(), ControlNames.Num(), bIsSaved ? TEXT("yes") : TEXT("no"));
	}
	return SerializeTDControlRigResult(Result);
}
