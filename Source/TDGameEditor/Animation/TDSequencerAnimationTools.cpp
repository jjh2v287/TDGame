#include "Animation/TDSequencerAnimationTools.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/TDAnimationAuthoringCommon.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Exporters/AnimSeqExportOption.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "LevelSequencePlayer.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneToolHelpers.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	FString WriteResult(const TSharedRef<FJsonObject>& Result)
	{
		FString Json;
		FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
		return Json;
	}

	FString Fail(const FString& Error)
	{
		UE_LOG(LogTDAnimAuthoring, Warning, TEXT("%s failed: %s"), TDActiveAuthoringTool(), *Error);
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), Error);
		return WriteResult(Result);
	}

	// 읽기 경로도 /Game으로 제한한다. 이 검증이 없으면 /Engine이나 /Script 경로를 로드할 수 있다.
	bool CheckReadPath(const FString& Path, const TCHAR* Field, FString& Error)
	{
		const FString PackagePath = FPackageName::ObjectPathToPackageName(Path);
		if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackagePath))
		{
			Error = FString::Printf(TEXT("%s must be a valid /Game package path or object path."), Field);
			return false;
		}
		return true;
	}

	bool CheckDestination(const FString& AssetPath, FString& Error)
	{
		FText Reason;
		if (!AssetPath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(AssetPath, false, &Reason))
		{
			Error = TEXT("asset_path must be a valid new /Game package path without an object suffix.");
			return false;
		}
		if (!FName(*FPackageName::GetLongPackageAssetName(AssetPath)).IsValidObjectName(Reason))
		{
			Error = Reason.ToString();
			return false;
		}
		if (FPackageName::DoesPackageExist(AssetPath) || FindPackage(nullptr, *AssetPath))
		{
			Error = TEXT("Destination package already exists. Choose a new asset_path; overwriting is not supported.");
			return false;
		}
		return true;
	}

	void CollectSkeletalComponents(UObject* BoundObject, TArray<USkeletalMeshComponent*>& Components)
	{
		if (AActor* Actor = Cast<AActor>(BoundObject))
		{
			TArray<USkeletalMeshComponent*> ActorComponents;
			Actor->GetComponents(ActorComponents);
			for (USkeletalMeshComponent* Component : ActorComponents)
			{
				Components.AddUnique(Component);
			}
			return;
		}
		if (USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(BoundObject))
		{
			Components.AddUnique(Component);
		}
	}

	TArray<USkeletalMeshComponent*> ResolveSkeletalComponents(ULevelSequence* Sequence, const FGuid& BindingId)
	{
		TArray<USkeletalMeshComponent*> Components;
		if (FMovieSceneSpawnable* Spawnable = Sequence->GetMovieScene()->FindSpawnable(BindingId))
		{
			CollectSkeletalComponents(Spawnable->GetObjectTemplate(), Components);
			return Components;
		}
		if (ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence() != Sequence)
		{
			return Components;
		}
		const FMovieSceneObjectBindingID ObjectBinding{UE::MovieScene::FRelativeObjectBindingID(BindingId)};
		for (UObject* BoundObject : ULevelSequenceEditorBlueprintLibrary::GetBoundObjects(ObjectBinding))
		{
			CollectSkeletalComponents(BoundObject, Components);
		}
		return Components;
	}

	TSharedRef<FJsonObject> DescribeComponent(const USkeletalMeshComponent* Component)
	{
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("component"), Component->GetPathName());
		Result->SetStringField(TEXT("skeletal_mesh"), GetPathNameSafe(Component->GetSkeletalMeshAsset()));
		return Result;
	}

	bool ExportAnimation(UWorld* World, ULevelSequence* Sequence, const FGuid& BindingId, USkeletalMesh* SkeletalMesh,
		UAnimSequence* Animation, FFrameNumber StartFrame, FFrameNumber EndFrame, FString& Error)
	{
		ALevelSequenceActor* PlaybackActor = nullptr;
		FMovieSceneSequencePlaybackSettings PlaybackSettings;
		PlaybackSettings.FinishCompletionStateOverride = EMovieSceneCompletionModeOverride::ForceRestoreState;
		PlaybackSettings.bDisableCameraCuts = true;
		ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, PlaybackSettings, PlaybackActor);
		ON_SCOPE_EXIT
		{
			if (Player)
			{
				Player->Stop();
				Player->GetEvaluationTemplate().TearDown();
			}
			if (IsValid(PlaybackActor))
			{
				World->DestroyActor(PlaybackActor, false, false);
			}
		};
		if (!Player)
		{
			Error = TEXT("The editor could not create a temporary sequence player.");
			return false;
		}
		Player->GetEvaluationTemplate().EnableGlobalPreAnimatedStateCapture();
		Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(FFrameTime(StartFrame), EUpdatePositionMethod::Jump));
		TArray<USkeletalMeshComponent*> Components;
		for (const TWeakObjectPtr<UObject>& BoundObject : Player->FindBoundObjects(BindingId, MovieSceneSequenceID::Root))
		{
			CollectSkeletalComponents(BoundObject.Get(), Components);
		}
		if (Components.Num() != 1 || Components[0]->GetSkeletalMeshAsset() != SkeletalMesh)
		{
			Error = TEXT("The evaluated binding must resolve to exactly one component using the requested skeletal_mesh at the first bake frame.");
			return false;
		}
		TStrongObjectPtr<UAnimSeqExportOption> ExportOptions(NewObject<UAnimSeqExportOption>());
		ExportOptions->bTransactRecording = false;
		ExportOptions->bRecordInWorldSpace = false;
		ExportOptions->bExportTransforms = true;
		ExportOptions->bUseCustomTimeRange = true;
		ExportOptions->CustomDisplayRate = Sequence->GetMovieScene()->GetDisplayRate();
		ExportOptions->CustomStartFrame = StartFrame;
		ExportOptions->CustomEndFrame = EndFrame;
		FAnimExportSequenceParameters ExportParameters;
		ExportParameters.Player = Player;
		ExportParameters.MovieSceneSequence = Sequence;
		ExportParameters.RootMovieSceneSequence = Sequence;
		ExportParameters.bForceUseOfMovieScenePlaybackRange = true;
		if (!MovieSceneToolHelpers::ExportToAnimSequence(Animation, ExportOptions.Get(), ExportParameters, Components[0]))
		{
			Error = TEXT("Native Sequencer export failed. No destination asset was created. Check the binding's spawn track, Control Rig evaluation and editor log.");
			return false;
		}
		return true;
	}
}

FString UTDSequencerAnimationTools::InspectSequence(const FString& SequencePath)
{
	const FTDAuthoringToolScope ToolScope(TEXT("InspectSequence"), SequencePath);
	if (!IsInGameThread() || !GEditor)
	{
		return Fail(TEXT("This tool requires the Unreal Editor game thread."));
	}
	FString Error;
	if (!CheckReadPath(SequencePath, TEXT("sequence"), Error))
	{
		return Fail(Error);
	}
	ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
	if (!Sequence || !Sequence->GetMovieScene())
	{
		return Fail(TEXT("sequence does not resolve to a LevelSequence with a MovieScene."));
	}
	const UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate FrameRate = MovieScene->GetDisplayRate();
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
	Result->SetBoolField(TEXT("focused_in_sequencer"), ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence() == Sequence);
	Result->SetNumberField(TEXT("fps_numerator"), FrameRate.Numerator);
	Result->SetNumberField(TEXT("fps_denominator"), FrameRate.Denominator);
	const TRange<FFrameNumber> Range = MovieScene->GetPlaybackRange();
	if (Range.HasLowerBound() && Range.HasUpperBound())
	{
		Result->SetNumberField(TEXT("start_frame"), FFrameRate::TransformTime(Range.GetLowerBoundValue(), MovieScene->GetTickResolution(), FrameRate).AsDecimal());
		Result->SetNumberField(TEXT("end_frame_exclusive"), FFrameRate::TransformTime(Range.GetUpperBoundValue(), MovieScene->GetTickResolution(), FrameRate).AsDecimal());
	}
	TArray<TSharedPtr<FJsonValue>> Bindings;
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		const TSharedRef<FJsonObject> BindingResult = MakeShared<FJsonObject>();
		BindingResult->SetStringField(TEXT("binding"), Binding.GetObjectGuid().ToString(EGuidFormats::DigitsWithHyphens));
		BindingResult->SetStringField(TEXT("name"), MovieScene->GetObjectDisplayName(Binding.GetObjectGuid()).ToString());
		TArray<TSharedPtr<FJsonValue>> Components;
		for (const USkeletalMeshComponent* Component : ResolveSkeletalComponents(Sequence, Binding.GetObjectGuid()))
		{
			Components.Add(MakeShared<FJsonValueObject>(DescribeComponent(Component)));
		}
		BindingResult->SetArrayField(TEXT("skeletal_components"), Components);
		Bindings.Add(MakeShared<FJsonValueObject>(BindingResult));
	}
	Result->SetArrayField(TEXT("bindings"), Bindings);
	return WriteResult(Result);
}

FString UTDSequencerAnimationTools::BakeAnimation(const FString& RequestJson)
{
	const FTDAuthoringToolScope ToolScope(TEXT("BakeAnimation"), RequestJson);
	if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
	{
		return Fail(TEXT("Baking requires the Unreal Editor game thread with PIE stopped."));
	}
	if (RequestJson.Len() > 65536)
	{
		return Fail(TEXT("RequestJson exceeds the 64 KiB limit."));
	}
	TSharedPtr<FJsonObject> Request;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RequestJson), Request) || !Request.IsValid())
	{
		return Fail(TEXT("RequestJson must be a JSON object."));
	}
	const TSet<FString> AllowedFields{TEXT("sequence"), TEXT("binding"), TEXT("skeletal_mesh"), TEXT("asset_path"), TEXT("save")};
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Request->Values)
	{
		if (!AllowedFields.Contains(Field.Key))
		{
			return Fail(FString::Printf(TEXT("Unknown request field: %s"), *Field.Key));
		}
	}
	FString SequencePath;
	FString BindingText;
	FString SkeletalMeshPath;
	FString AssetPath;
	if (!Request->TryGetStringField(TEXT("sequence"), SequencePath) || SequencePath.IsEmpty()
		|| !Request->TryGetStringField(TEXT("binding"), BindingText)
		|| !Request->TryGetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath) || SkeletalMeshPath.IsEmpty()
		|| !Request->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return Fail(TEXT("Required string fields: sequence, binding, skeletal_mesh, asset_path."));
	}
	bool bShouldSave = true;
	if (Request->HasField(TEXT("save")) && !Request->TryGetBoolField(TEXT("save"), bShouldSave))
	{
		return Fail(TEXT("save must be a boolean."));
	}
	FString Error;
	if (!CheckDestination(AssetPath, Error))
	{
		return Fail(Error);
	}
	FGuid BindingId;
	if (!FGuid::Parse(BindingText, BindingId) || !BindingId.IsValid())
	{
		return Fail(TEXT("binding must be an explicit valid GUID returned by InspectSequence."));
	}
	if (!CheckReadPath(SequencePath, TEXT("sequence"), Error) || !CheckReadPath(SkeletalMeshPath, TEXT("skeletal_mesh"), Error))
	{
		return Fail(Error);
	}
	ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
	USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || World->bIsTearingDown || !Sequence || !Sequence->GetMovieScene() || !SkeletalMesh || !SkeletalMesh->GetSkeleton())
	{
		return Fail(TEXT("Expected an editor world, a valid LevelSequence and a skeletal_mesh with a Skeleton."));
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene->FindBinding(BindingId))
	{
		return Fail(TEXT("binding is not present in the requested sequence."));
	}
	const FFrameRate FrameRate = MovieScene->GetDisplayRate();
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const TRange<FFrameNumber> Range = MovieScene->GetPlaybackRange();
	if (FrameRate.Numerator <= 0 || FrameRate.Denominator <= 0 || TickResolution.Numerator <= 0 || TickResolution.Denominator <= 0
		|| !Range.HasLowerBound() || !Range.HasUpperBound() || Range.IsEmpty())
	{
		return Fail(TEXT("The sequence must have positive frame rates and a finite nonempty playback range."));
	}
	const double Duration = (static_cast<double>(Range.GetUpperBoundValue().Value) - Range.GetLowerBoundValue().Value) / TickResolution.AsDecimal();
	if (Duration <= 0.0 || Duration > 600.0 || Duration * FrameRate.AsDecimal() > 100000.0)
	{
		return Fail(TEXT("The bake range must be positive, at most 600 seconds and at most 100000 output frames."));
	}
	const FFrameNumber StartFrame = FFrameRate::TransformTime(Range.GetLowerBoundValue(), TickResolution, FrameRate).CeilToFrame();
	const FFrameNumber EndFrame = FFrameRate::TransformTime(Range.GetUpperBoundValue(), TickResolution, FrameRate).CeilToFrame() - 1;
	if (EndFrame <= StartFrame)
	{
		return Fail(TEXT("The playback range must contain at least two display-rate sample frames."));
	}
	const TArray<USkeletalMeshComponent*> Components = ResolveSkeletalComponents(Sequence, BindingId);
	if (Components.Num() != 1)
	{
		return Fail(TEXT("binding must resolve to exactly one skeletal component. Native spawnables resolve from their templates; otherwise open/focus this sequence and evaluate a frame where the bound character exists. For actors with multiple skeletal components, use an explicit component binding."));
	}
	if (Components[0]->GetSkeletalMeshAsset() != SkeletalMesh)
	{
		return Fail(FString::Printf(TEXT("skeletal_mesh does not match the binding's mesh: %s"), *GetPathNameSafe(Components[0]->GetSkeletalMeshAsset())));
	}
	TStrongObjectPtr<UAnimSequence> Animation(NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Transient));
	Animation->SetSkeleton(SkeletalMesh->GetSkeleton());
	Animation->SetPreviewMesh(SkeletalMesh);
	Animation->GetController().InitializeModel();
	if (!ExportAnimation(World, Sequence, BindingId, SkeletalMesh, Animation.Get(), StartFrame, EndFrame, Error))
	{
		return Fail(Error);
	}
	const IAnimationDataModel* Model = Animation->GetDataModel();
	if (!Model || Model->GetNumBoneTracks() == 0 || Model->GetNumberOfKeys() < 2 || Animation->GetPlayLength() <= 0.0
		|| Animation->GetSkeleton() != SkeletalMesh->GetSkeleton())
	{
		return Fail(TEXT("Export did not produce valid skeletal animation samples for the requested Skeleton. No destination asset was created."));
	}
	if (!CheckDestination(AssetPath, Error))
	{
		return Fail(Error);
	}
	FTDAuthoringPackageScope PackageScope(AssetPath);
	const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
	if (!Animation->Rename(*AssetName, PackageScope.Get(), REN_DontCreateRedirectors | REN_NonTransactional))
	{
		return Fail(TEXT("The baked animation could not be assigned to the new package."));
	}
	PackageScope.Track(Animation.Get());
	Animation->ClearFlags(RF_Transient);
	Animation->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	PackageScope.Get()->MarkPackageDirty();
	if (bShouldSave)
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(PackageScope.Get(), Animation.Get(), *Filename, SaveArgs))
		{
			Animation->SetFlags(RF_Transient);
			return Fail(TEXT("Saving the new animation package failed. Check the editor log and output directory before retrying."));
		}
	}
	PackageScope.Commit();
	FAssetRegistryModule::AssetCreated(Animation.Get());
	UE_LOG(LogTDAnimAuthoring, Log, TEXT("BakeAnimation: baked %s from %s (saved: %s)."), *Animation->GetPathName(), *Sequence->GetPathName(), bShouldSave ? TEXT("yes") : TEXT("no"));
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Animation->GetPathName());
	Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
	Result->SetStringField(TEXT("binding"), BindingId.ToString(EGuidFormats::DigitsWithHyphens));
	Result->SetBoolField(TEXT("saved"), bShouldSave);
	Result->SetNumberField(TEXT("duration_seconds"), Animation->GetPlayLength());
	Result->SetNumberField(TEXT("sample_count"), Model->GetNumberOfKeys());
	Result->SetNumberField(TEXT("bone_track_count"), Model->GetNumBoneTracks());
	Result->SetNumberField(TEXT("source_start_frame"), StartFrame.Value);
	Result->SetNumberField(TEXT("source_end_frame_inclusive"), EndFrame.Value);
	return WriteResult(Result);
}
