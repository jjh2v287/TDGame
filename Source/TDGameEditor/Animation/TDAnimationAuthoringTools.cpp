#include "Animation/TDAnimationAuthoringTools.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/TDAnimationAuthoringCommon.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/AnimSequenceFactory.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	struct FTDAnimationKey
	{
		int32 Frame = 0;
		FTransform Transform = FTransform::Identity;
	};

	struct FTDAnimationTrack
	{
		int32 BoneIndex = INDEX_NONE;
		TArray<FTDAnimationKey> Keys;
	};

	struct FTDMontageSection
	{
		FName Name;
		float Time = 0.0f;
		FName Next;
	};

	FString WriteAnimationResult(const TSharedRef<FJsonObject>& Json)
	{
		FString Result;
		FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Result));
		return Result;
	}

	FString Fail(const FString& Message)
	{
		UE_LOG(LogTDAnimAuthoring, Warning, TEXT("%s failed: %s"), TDActiveAuthoringTool(), *Message);
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("success"), false);
		Json->SetStringField(TEXT("error"), Message);
		return WriteAnimationResult(Json);
	}

	bool CheckEditor(FString& Error)
	{
		if (!IsInGameThread() || !GEditor || GEditor->PlayWorld)
		{
			Error = TEXT("Animation authoring requires the editor game thread with PIE stopped.");
			return false;
		}
		return true;
	}

	bool ParseRequest(const FString& Text, TSharedPtr<FJsonObject>& Json, FString& Error)
	{
		if (Text.Len() > 4 * 1024 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
		{
			Error = TEXT("RequestJson must be a JSON object of at most four million characters.");
			return false;
		}
		return true;
	}

	bool CheckFields(const FJsonObject& Json, std::initializer_list<const TCHAR*> Fields, FString& Error)
	{
		for (const auto& Pair : Json.Values)
		{
			bool bIsKnown = false;
			for (const TCHAR* Field : Fields)
			{
				bIsKnown |= Pair.Key == Field;
			}
			if (!bIsKnown)
			{
				Error = FString::Printf(TEXT("Unknown field: %s"), *Pair.Key);
				return false;
			}
		}
		return true;
	}

	bool ReadString(const FJsonObject& Json, const TCHAR* Field, FString& Value, FString& Error)
	{
		if (!Json.TryGetStringField(Field, Value) || Value.IsEmpty())
		{
			Error = FString::Printf(TEXT("%s must be a nonempty string."), Field);
			return false;
		}
		return true;
	}

	bool ReadNumber(const FJsonObject& Json, const TCHAR* Field, double& Value, double Minimum, double Maximum, bool bIsRequired, FString& Error)
	{
		if (!Json.HasField(Field) && !bIsRequired)
		{
			return true;
		}
		const TSharedPtr<FJsonValue> JsonValue = Json.TryGetField(Field);
		if (!JsonValue.IsValid() || JsonValue->Type != EJson::Number || !JsonValue->TryGetNumber(Value) || !FMath::IsFinite(Value) || Value < Minimum || Value > Maximum)
		{
			Error = FString::Printf(TEXT("%s must be a finite JSON number in [%g, %g]."), Field, Minimum, Maximum);
			return false;
		}
		return true;
	}

	bool ReadInteger(const FJsonObject& Json, const TCHAR* Field, int32& Value, int32 Minimum, int32 Maximum, bool bIsRequired, FString& Error)
	{
		double Number = Value;
		if (!ReadNumber(Json, Field, Number, Minimum, Maximum, bIsRequired, Error))
		{
			return false;
		}
		if (Number != FMath::FloorToDouble(Number))
		{
			Error = FString::Printf(TEXT("%s must be an integer; fractional values are not rounded."), Field);
			return false;
		}
		Value = static_cast<int32>(Number);
		return true;
	}

	bool ReadVector(const FJsonObject& Json, const TCHAR* Field, FVector& Value, double Minimum, double Maximum, FString& Error)
	{
		if (!Json.HasField(Field))
		{
			return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
		if (!Json.TryGetArrayField(Field, Components) || Components->Num() != 3)
		{
			Error = FString::Printf(TEXT("%s must be an array of three numbers."), Field);
			return false;
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			double Number = 0.0;
			if ((*Components)[Index]->Type != EJson::Number || !(*Components)[Index]->TryGetNumber(Number) || !FMath::IsFinite(Number) || Number < Minimum || Number > Maximum)
			{
				Error = FString::Printf(TEXT("%s components must be finite numbers in [%g, %g]."), Field, Minimum, Maximum);
				return false;
			}
			Value[Index] = Number;
		}
		return true;
	}

	bool ReadSave(const FJsonObject& Json, bool& bShouldSave, FString& Error)
	{
		if (Json.HasField(TEXT("save")) && !Json.TryGetBoolField(TEXT("save"), bShouldSave))
		{
			Error = TEXT("save must be a JSON boolean.");
			return false;
		}
		return true;
	}

	bool NormalizeReadPath(const FString& Path, FString& ObjectPath, FString& Error)
	{
		const FString PackagePath = FPackageName::ObjectPathToPackageName(Path);
		if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackagePath))
		{
			Error = TEXT("Asset paths must be valid /Game package paths or object paths.");
			return false;
		}
		ObjectPath = PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath);
		if (Path != PackagePath && Path != ObjectPath)
		{
			Error = TEXT("Object path must identify the top-level asset with the same name as its package.");
			return false;
		}
		return true;
	}

	bool CheckDestination(const FJsonObject& Json, FString& PackagePath, FString& Error)
	{
		if (!ReadString(Json, TEXT("asset_path"), PackagePath, Error))
		{
			return false;
		}
		FString ObjectPath;
		if (!NormalizeReadPath(PackagePath, ObjectPath, Error) || PackagePath.Contains(TEXT(".")))
		{
			Error = TEXT("asset_path must be a /Game package path without an object suffix, extension, or subobject.");
			return false;
		}
		if (FPackageName::DoesPackageExist(PackagePath) || FindPackage(nullptr, *PackagePath) || FindObject<UObject>(nullptr, *ObjectPath))
		{
			Error = FString::Printf(TEXT("Destination already exists on disk or in memory; overwriting is forbidden: %s"), *PackagePath);
			return false;
		}
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& Vector)
	{
		return {MakeShared<FJsonValueNumber>(Vector.X), MakeShared<FJsonValueNumber>(Vector.Y), MakeShared<FJsonValueNumber>(Vector.Z)};
	}

	TSharedRef<FJsonObject> TransformJson(const FTransform& Transform)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		const FQuat Rotation = Transform.GetRotation();
		const FRotator Euler = Rotation.Rotator();
		Json->SetArrayField(TEXT("translation"), VectorJson(Transform.GetTranslation()));
		Json->SetArrayField(TEXT("rotation"), VectorJson(FVector(Euler.Pitch, Euler.Yaw, Euler.Roll)));
		Json->SetArrayField(TEXT("rotation_quaternion"), {MakeShared<FJsonValueNumber>(Rotation.X), MakeShared<FJsonValueNumber>(Rotation.Y), MakeShared<FJsonValueNumber>(Rotation.Z), MakeShared<FJsonValueNumber>(Rotation.W)});
		Json->SetArrayField(TEXT("scale"), VectorJson(Transform.GetScale3D()));
		return Json;
	}

	TSharedRef<FJsonObject> AnimationSummary(UAnimationAsset* Asset)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("success"), true);
		Json->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		Json->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Json->SetStringField(TEXT("skeleton"), GetPathNameSafe(Asset->GetSkeleton()));
		Json->SetStringField(TEXT("preview_mesh"), GetPathNameSafe(Asset->GetPreviewMesh()));
		Json->SetBoolField(TEXT("dirty"), Asset->GetOutermost()->IsDirty());
		if (const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Asset))
		{
			Json->SetNumberField(TEXT("duration"), Sequence->GetPlayLength());
		}
		if (const UAnimSequence* AnimSequence = Cast<UAnimSequence>(Asset))
		{
			Json->SetBoolField(TEXT("root_motion_enabled"), AnimSequence->bEnableRootMotion);
		}
		return Json;
	}

	FString FinishCreation(UAnimationAsset* Asset, bool bShouldSave)
	{
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();
		if (UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
		{
			Sequence->WaitOnExistingCompression();
		}
		bool bIsSaved = false;
		if (bShouldSave)
		{
			UPackage* Package = Asset->GetOutermost();
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			bIsSaved = IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true) && UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
		}
		const TSharedRef<FJsonObject> Json = AnimationSummary(Asset);
		Json->SetBoolField(TEXT("saved"), bIsSaved);
		if (bShouldSave && !bIsSaved)
		{
			Json->SetBoolField(TEXT("success"), false);
			Json->SetStringField(TEXT("error"), TEXT("Asset was created but saving failed. The unsaved asset remains at asset_path; inspect the editor log and save it manually."));
			UE_LOG(LogTDAnimAuthoring, Warning, TEXT("%s: created %s but saving it failed."), TDActiveAuthoringTool(), *Asset->GetPathName());
		}
		else
		{
			UE_LOG(LogTDAnimAuthoring, Log, TEXT("%s: created %s (saved: %s)."), TDActiveAuthoringTool(), *Asset->GetPathName(), bIsSaved ? TEXT("yes") : TEXT("no"));
		}
		return WriteAnimationResult(Json);
	}

	bool ParseRootMotion(const FJsonObject& Json, bool& bEnable, ERootMotionRootLock::Type& Lock, bool& bForceLock, FString& Error)
	{
		const TSharedPtr<FJsonObject>* RootMotionJson = nullptr;
		if (!Json.HasField(TEXT("root_motion")))
		{
			return true;
		}
		if (!Json.TryGetObjectField(TEXT("root_motion"), RootMotionJson) || !CheckFields(**RootMotionJson, {TEXT("enable"), TEXT("root_lock"), TEXT("force_root_lock")}, Error))
		{
			if (Error.IsEmpty()) Error = TEXT("root_motion must be an object.");
			return false;
		}
		if ((*RootMotionJson)->HasField(TEXT("enable")) && !(*RootMotionJson)->TryGetBoolField(TEXT("enable"), bEnable))
		{
			Error = TEXT("root_motion.enable must be a JSON boolean.");
			return false;
		}
		if ((*RootMotionJson)->HasField(TEXT("force_root_lock")) && !(*RootMotionJson)->TryGetBoolField(TEXT("force_root_lock"), bForceLock))
		{
			Error = TEXT("root_motion.force_root_lock must be a JSON boolean.");
			return false;
		}
		FString LockName;
		if ((*RootMotionJson)->HasField(TEXT("root_lock")))
		{
			if (!ReadString(**RootMotionJson, TEXT("root_lock"), LockName, Error))
			{
				return false;
			}
			if (LockName == TEXT("RefPose")) Lock = ERootMotionRootLock::RefPose;
			else if (LockName == TEXT("AnimFirstFrame")) Lock = ERootMotionRootLock::AnimFirstFrame;
			else if (LockName == TEXT("Zero")) Lock = ERootMotionRootLock::Zero;
			else
			{
				Error = TEXT("root_motion.root_lock must be RefPose, AnimFirstFrame, or Zero.");
				return false;
			}
		}
		return true;
	}

	bool ParseTracks(const FJsonObject& Json, const FReferenceSkeleton& Reference, int32 NumFrames, bool bIsReferenceOffset, TArray<FTDAnimationTrack>& Tracks, FString& Error)
	{
		const TArray<TSharedPtr<FJsonValue>>* TrackValues = nullptr;
		if (!Json.TryGetArrayField(TEXT("tracks"), TrackValues) || TrackValues->IsEmpty() || TrackValues->Num() > Reference.GetRawBoneNum())
		{
			Error = TEXT("tracks must be a nonempty array with at most one entry per raw mesh bone.");
			return false;
		}
		TSet<FName> Bones;
		for (const TSharedPtr<FJsonValue>& TrackValue : *TrackValues)
		{
			const TSharedPtr<FJsonObject>* TrackJson = nullptr;
			if (!TrackValue->TryGetObject(TrackJson) || !CheckFields(**TrackJson, {TEXT("bone"), TEXT("keys")}, Error))
			{
				if (Error.IsEmpty()) Error = TEXT("Each track must be an object.");
				return false;
			}
			FString Bone;
			if (!ReadString(**TrackJson, TEXT("bone"), Bone, Error)) return false;
			FTDAnimationTrack Track;
			Track.BoneIndex = Reference.FindRawBoneIndex(FName(*Bone));
			if (Track.BoneIndex == INDEX_NONE || Bones.Contains(FName(*Bone)))
			{
				Error = FString::Printf(TEXT("Unknown or duplicate raw mesh bone: %s"), *Bone);
				return false;
			}
			Bones.Add(FName(*Bone));
			const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
			if (!(*TrackJson)->TryGetArrayField(TEXT("keys"), Keys) || Keys->IsEmpty() || Keys->Num() > NumFrames + 1)
			{
				Error = TEXT("Each track needs 1..num_frames+1 keys.");
				return false;
			}
			const FTransform& ReferencePose = Reference.GetRawRefBonePose()[Track.BoneIndex];
			int32 PreviousFrame = INDEX_NONE;
			for (const TSharedPtr<FJsonValue>& KeyValue : *Keys)
			{
				const TSharedPtr<FJsonObject>* KeyJson = nullptr;
				if (!KeyValue->TryGetObject(KeyJson) || !CheckFields(**KeyJson, {TEXT("frame"), TEXT("translation"), TEXT("rotation"), TEXT("scale")}, Error))
				{
					if (Error.IsEmpty()) Error = TEXT("Each key must be an object.");
					return false;
				}
				FTDAnimationKey Key;
				if (!ReadInteger(**KeyJson, TEXT("frame"), Key.Frame, 0, NumFrames, true, Error)) return false;
				if (Key.Frame <= PreviousFrame)
				{
					Error = TEXT("Key frames must strictly increase without duplicates.");
					return false;
				}
				PreviousFrame = Key.Frame;
				const FRotator ReferenceEuler = ReferencePose.Rotator();
				FVector Translation = bIsReferenceOffset ? FVector::ZeroVector : ReferencePose.GetTranslation();
				FVector Euler = bIsReferenceOffset ? FVector::ZeroVector : FVector(ReferenceEuler.Pitch, ReferenceEuler.Yaw, ReferenceEuler.Roll);
				FVector Scale = bIsReferenceOffset ? FVector::OneVector : ReferencePose.GetScale3D();
				if (!ReadVector(**KeyJson, TEXT("translation"), Translation, -10000000.0, 10000000.0, Error) || !ReadVector(**KeyJson, TEXT("rotation"), Euler, -360000.0, 360000.0, Error) || !ReadVector(**KeyJson, TEXT("scale"), Scale, 0.0001, 10000.0, Error)) return false;
				FQuat Rotation = FRotator(Euler.X, Euler.Y, Euler.Z).Quaternion();
				if (bIsReferenceOffset)
				{
					Translation += ReferencePose.GetTranslation();
					Rotation = ReferencePose.GetRotation() * Rotation;
					Scale *= ReferencePose.GetScale3D();
				}
				Key.Transform = FTransform(Rotation.GetNormalized(), Translation, Scale);
				if (Key.Transform.ContainsNaN())
				{
					Error = TEXT("Key composition produced an invalid transform.");
					return false;
				}
				Track.Keys.Add(Key);
			}
			Tracks.Add(MoveTemp(Track));
		}
		return true;
	}

	FTransform SampleTrack(const TArray<FTDAnimationKey>& Keys, int32 Frame, int32& UpperIndex)
	{
		while (UpperIndex < Keys.Num() && Keys[UpperIndex].Frame < Frame) ++UpperIndex;
		if (UpperIndex == 0) return Keys[0].Transform;
		if (UpperIndex == Keys.Num()) return Keys.Last().Transform;
		const FTDAnimationKey& Lower = Keys[UpperIndex - 1];
		const FTDAnimationKey& Upper = Keys[UpperIndex];
		const double Alpha = static_cast<double>(Frame - Lower.Frame) / (Upper.Frame - Lower.Frame);
		return FTransform(FQuat::Slerp(Lower.Transform.GetRotation(), Upper.Transform.GetRotation(), Alpha).GetNormalized(), FMath::Lerp(Lower.Transform.GetTranslation(), Upper.Transform.GetTranslation(), Alpha), FMath::Lerp(Lower.Transform.GetScale3D(), Upper.Transform.GetScale3D(), Alpha));
	}
}

FString UTDAnimationAuthoringTools::InspectSkeleton(const FString& SkeletalMeshPath)
{
	const FTDAuthoringToolScope ToolScope(TEXT("InspectSkeleton"), SkeletalMeshPath);
	FString Error;
	FString ObjectPath;
	if (!CheckEditor(Error) || !NormalizeReadPath(SkeletalMeshPath, ObjectPath, Error)) return Fail(Error);
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *ObjectPath);
	if (!Mesh || !Mesh->GetSkeleton()) return Fail(TEXT("A skeletal mesh with a skeleton was not found at the requested path."));
	const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("success"), true);
	Json->SetStringField(TEXT("skeletal_mesh"), Mesh->GetPathName());
	Json->SetStringField(TEXT("skeleton"), Mesh->GetSkeleton()->GetPathName());
	Json->SetNumberField(TEXT("bone_count"), Reference.GetRawBoneNum());
	TArray<TSharedPtr<FJsonValue>> Bones;
	for (int32 Index = 0; Index < Reference.GetRawBoneNum(); ++Index)
	{
		const TSharedRef<FJsonObject> Bone = TransformJson(Reference.GetRawRefBonePose()[Index]);
		const int32 ParentIndex = Reference.GetRawParentIndex(Index);
		Bone->SetStringField(TEXT("name"), Reference.GetBoneName(Index).ToString());
		Bone->SetNumberField(TEXT("index"), Index);
		Bone->SetNumberField(TEXT("parent_index"), ParentIndex);
		Bone->SetStringField(TEXT("parent"), ParentIndex == INDEX_NONE ? FString() : Reference.GetBoneName(ParentIndex).ToString());
		Bones.Add(MakeShared<FJsonValueObject>(Bone));
	}
	Json->SetArrayField(TEXT("bones"), Bones);
	TArray<TSharedPtr<FJsonValue>> Slots;
	for (const FAnimSlotGroup& Group : Mesh->GetSkeleton()->GetSlotGroups())
	{
		for (FName SlotName : Group.SlotNames)
		{
			const TSharedRef<FJsonObject> Slot = MakeShared<FJsonObject>();
			Slot->SetStringField(TEXT("group"), Group.GroupName.ToString());
			Slot->SetStringField(TEXT("slot"), SlotName.ToString());
			Slots.Add(MakeShared<FJsonValueObject>(Slot));
		}
	}
	Json->SetArrayField(TEXT("slots"), Slots);
	return WriteAnimationResult(Json);
}

FString UTDAnimationAuthoringTools::CreateBoneAnimation(const FString& RequestJson)
{
	const FTDAuthoringToolScope ToolScope(TEXT("CreateBoneAnimation"), RequestJson);
	FString Error;
	TSharedPtr<FJsonObject> Json;
	FString PackagePath;
	if (!CheckEditor(Error) || !ParseRequest(RequestJson, Json, Error) || !CheckFields(*Json, {TEXT("asset_path"), TEXT("skeletal_mesh"), TEXT("fps"), TEXT("num_frames"), TEXT("mode"), TEXT("save"), TEXT("tracks"), TEXT("root_motion")}, Error) || !CheckDestination(*Json, PackagePath, Error)) return Fail(Error);
	FString MeshPath;
	FString ObjectPath;
	int32 Fps = 30;
	int32 NumFrames = 0;
	bool bShouldSave = true;
	if (!ReadString(*Json, TEXT("skeletal_mesh"), MeshPath, Error) || !NormalizeReadPath(MeshPath, ObjectPath, Error) || !ReadInteger(*Json, TEXT("fps"), Fps, 1, 120, true, Error) || !ReadInteger(*Json, TEXT("num_frames"), NumFrames, 1, 36000, true, Error) || !ReadSave(*Json, bShouldSave, Error)) return Fail(Error);
	FString Mode = TEXT("reference_offset");
	if (Json->HasField(TEXT("mode")) && !ReadString(*Json, TEXT("mode"), Mode, Error)) return Fail(Error);
	if (Mode != TEXT("reference_offset") && Mode != TEXT("local_absolute")) return Fail(TEXT("mode must be reference_offset or local_absolute."));
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *ObjectPath);
	if (!Mesh || !Mesh->GetSkeleton()) return Fail(TEXT("skeletal_mesh must reference a skeletal mesh with a skeleton."));
	const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
	if (Reference.GetRawBoneNum() == 0 || static_cast<int64>(Reference.GetRawBoneNum()) * (NumFrames + 1) > 2000000 || static_cast<double>(NumFrames) / Fps > 600.0) return Fail(TEXT("Animation exceeds the 600-second/two-million-dense-sample budget, or mesh has no raw bones."));
	for (int32 Index = 0; Index < Reference.GetRawBoneNum(); ++Index)
	{
		if (Reference.GetRawRefBonePose()[Index].ContainsNaN() || Mesh->GetSkeleton()->GetReferenceSkeleton().FindRawBoneIndex(Reference.GetBoneName(Index)) == INDEX_NONE) return Fail(TEXT("Mesh has an invalid reference pose or a bone absent from its skeleton."));
	}
	bool bEnableRootMotion = false;
	ERootMotionRootLock::Type RootLock = ERootMotionRootLock::RefPose;
	bool bForceRootLock = false;
	if (!ParseRootMotion(*Json, bEnableRootMotion, RootLock, bForceRootLock, Error)) return Fail(Error);
	TArray<FTDAnimationTrack> Tracks;
	if (!ParseTracks(*Json, Reference, NumFrames, Mode == TEXT("reference_offset"), Tracks, Error)) return Fail(Error);
	FTDAuthoringPackageScope PackageScope(PackagePath);
	UAnimSequenceFactory* Factory = NewObject<UAnimSequenceFactory>();
	Factory->TargetSkeleton = Mesh->GetSkeleton();
	Factory->PreviewSkeletalMesh = Mesh;
	UFactory* AssetFactory = Factory;
	UAnimSequence* Sequence = Cast<UAnimSequence>(AssetFactory->FactoryCreateNew(UAnimSequence::StaticClass(), PackageScope.Get(), FName(*FPackageName::GetLongPackageAssetName(PackagePath)), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (!Sequence) return Fail(TEXT("AnimSequence factory failed."));
	PackageScope.Track(Sequence);
	IAnimationDataController& Controller = Sequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Create TD bone animation")), false);
	Controller.SetFrameRate(FFrameRate(Fps, 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(NumFrames), false);
	Controller.RemoveAllBoneTracks(false);
	bool bAreTracksValid = true;
	for (int32 BoneIndex = 0; BoneIndex < Reference.GetRawBoneNum() && bAreTracksValid; ++BoneIndex)
	{
		const FName BoneName = Reference.GetBoneName(BoneIndex);
		const FTDAnimationTrack* Track = Tracks.FindByPredicate([BoneIndex](const FTDAnimationTrack& Candidate) { return Candidate.BoneIndex == BoneIndex; });
		TArray<FVector3f> Positions;
		TArray<FQuat4f> Rotations;
		TArray<FVector3f> Scales;
		Positions.Reserve(NumFrames + 1);
		Rotations.Reserve(NumFrames + 1);
		Scales.Reserve(NumFrames + 1);
		int32 UpperIndex = 0;
		for (int32 Frame = 0; Frame <= NumFrames; ++Frame)
		{
			const FTransform Transform = Track ? SampleTrack(Track->Keys, Frame, UpperIndex) : Reference.GetRawRefBonePose()[BoneIndex];
			Positions.Add(FVector3f(Transform.GetTranslation()));
			Rotations.Add(FQuat4f(Transform.GetRotation()));
			Scales.Add(FVector3f(Transform.GetScale3D()));
		}
		bAreTracksValid = Controller.AddBoneCurve(BoneName, false) && Controller.SetBoneTrackKeys(BoneName, Positions, Rotations, Scales, false);
	}
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);
	if (!bAreTracksValid || Sequence->GetDataModel()->GetNumberOfFrames() != NumFrames || Sequence->GetDataModel()->GetNumBoneTracks() != Reference.GetRawBoneNum())
	{
		return Fail(TEXT("Animation data controller rejected tracks or frame count; the incomplete asset was discarded."));
	}
	Sequence->bEnableRootMotion = bEnableRootMotion;
	Sequence->RootMotionRootLock = RootLock;
	Sequence->bForceRootLock = bForceRootLock;
	Sequence->PostEditChange();
	PackageScope.Commit();
	return FinishCreation(Sequence, bShouldSave);
}

FString UTDAnimationAuthoringTools::CreateMontage(const FString& RequestJson)
{
	const FTDAuthoringToolScope ToolScope(TEXT("CreateMontage"), RequestJson);
	FString Error;
	TSharedPtr<FJsonObject> Json;
	FString PackagePath;
	if (!CheckEditor(Error) || !ParseRequest(RequestJson, Json, Error) || !CheckFields(*Json, {TEXT("asset_path"), TEXT("slot"), TEXT("save"), TEXT("blend_in"), TEXT("blend_out"), TEXT("segments"), TEXT("sections")}, Error) || !CheckDestination(*Json, PackagePath, Error)) return Fail(Error);
	FString Slot;
	bool bShouldSave = true;
	double BlendIn = 0.25;
	double BlendOut = 0.25;
	if (!ReadString(*Json, TEXT("slot"), Slot, Error) || !ReadSave(*Json, bShouldSave, Error) || !ReadNumber(*Json, TEXT("blend_in"), BlendIn, 0.0, 60.0, false, Error) || !ReadNumber(*Json, TEXT("blend_out"), BlendOut, 0.0, 60.0, false, Error)) return Fail(Error);
	const TArray<TSharedPtr<FJsonValue>>* SegmentValues = nullptr;
	if (!Json->TryGetArrayField(TEXT("segments"), SegmentValues) || SegmentValues->IsEmpty() || SegmentValues->Num() > 128) return Fail(TEXT("segments must contain 1..128 sequence segment objects."));
	TArray<FAnimSegment> Segments;
	USkeleton* Skeleton = nullptr;
	USkeletalMesh* PreviewMesh = nullptr;
	float Duration = 0.0f;
	for (const TSharedPtr<FJsonValue>& SegmentValue : *SegmentValues)
	{
		const TSharedPtr<FJsonObject>* SegmentJson = nullptr;
		if (!SegmentValue->TryGetObject(SegmentJson)) return Fail(TEXT("Each segment must be an object."));
		if (!CheckFields(**SegmentJson, {TEXT("sequence"), TEXT("start_time"), TEXT("end_time"), TEXT("play_rate"), TEXT("loop_count")}, Error)) return Fail(Error);
		FString SequencePath;
		FString ObjectPath;
		if (!ReadString(**SegmentJson, TEXT("sequence"), SequencePath, Error) || !NormalizeReadPath(SequencePath, ObjectPath, Error)) return Fail(Error);
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *ObjectPath);
		if (!Sequence || !Sequence->GetSkeleton() || !FMath::IsFinite(Sequence->GetPlayLength()) || Sequence->GetPlayLength() <= 0.0f) return Fail(TEXT("Each sequence must be a nonempty AnimSequence with a skeleton."));
		if (Skeleton && Skeleton != Sequence->GetSkeleton()) return Fail(TEXT("All sequences must use the exact same skeleton; retarget-compatible skeletons are not accepted."));
		if (Sequence->IsValidAdditive()) return Fail(TEXT("Additive sequences are not supported by this montage authoring tool."));
		if (!FMath::IsFinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f) return Fail(TEXT("Source sequence RateScale must be finite and positive."));
		Skeleton = Sequence->GetSkeleton();
		if (!PreviewMesh) PreviewMesh = Sequence->GetPreviewMesh();
		double StartTime = 0.0;
		double EndTime = Sequence->GetPlayLength();
		double PlayRate = 1.0;
		int32 LoopCount = 1;
		if (!ReadNumber(**SegmentJson, TEXT("start_time"), StartTime, 0.0, Sequence->GetPlayLength(), false, Error) || !ReadNumber(**SegmentJson, TEXT("end_time"), EndTime, 0.0, Sequence->GetPlayLength(), false, Error) || !ReadNumber(**SegmentJson, TEXT("play_rate"), PlayRate, 0.001, 100.0, false, Error) || !ReadInteger(**SegmentJson, TEXT("loop_count"), LoopCount, 1, 100, false, Error)) return Fail(Error);
		if (EndTime <= StartTime || static_cast<float>(EndTime) <= static_cast<float>(StartTime)) return Fail(TEXT("Segment end_time must exceed start_time at engine float precision."));
		FAnimSegment Segment;
		Segment.SetAnimReference(Sequence, true);
		Segment.StartPos = Duration;
		Segment.AnimStartTime = static_cast<float>(StartTime);
		Segment.AnimEndTime = static_cast<float>(EndTime);
		Segment.AnimPlayRate = static_cast<float>(PlayRate);
		Segment.LoopingCount = LoopCount;
		const float SegmentLength = Segment.GetLength();
		if (!FMath::IsFinite(SegmentLength) || SegmentLength <= 0.0f || !FMath::IsFinite(Duration + SegmentLength) || Duration + SegmentLength > 600.0f || Duration + SegmentLength <= Duration) return Fail(TEXT("Montage segment duration is invalid or total duration exceeds 600 seconds."));
		Duration += SegmentLength;
		Segments.Add(Segment);
	}
	const FName SlotName(*Slot);
	if (!Skeleton->ContainsSlotName(SlotName)) return Fail(TEXT("slot must already exist in the source skeleton; inspect the skeleton slots first."));
	TArray<FTDMontageSection> Sections;
	if (Json->HasField(TEXT("sections")))
	{
		const TArray<TSharedPtr<FJsonValue>>* SectionValues = nullptr;
		if (!Json->TryGetArrayField(TEXT("sections"), SectionValues) || SectionValues->IsEmpty() || SectionValues->Num() > 128) return Fail(TEXT("sections must contain 1..128 section objects when provided."));
		TSet<FName> SectionNames;
		for (const TSharedPtr<FJsonValue>& SectionValue : *SectionValues)
		{
			const TSharedPtr<FJsonObject>* SectionJson = nullptr;
			if (!SectionValue->TryGetObject(SectionJson)) return Fail(TEXT("Each section must be an object."));
			if (!CheckFields(**SectionJson, {TEXT("name"), TEXT("time"), TEXT("next")}, Error)) return Fail(Error);
			FString Name;
			FString Next;
			double Time = 0.0;
			if (!ReadString(**SectionJson, TEXT("name"), Name, Error) || !ReadNumber(**SectionJson, TEXT("time"), Time, 0.0, Duration, true, Error)) return Fail(Error);
			if ((*SectionJson)->HasField(TEXT("next")) && !ReadString(**SectionJson, TEXT("next"), Next, Error)) return Fail(Error);
			FTDMontageSection Section;
			Section.Name = FName(*Name);
			Section.Next = Next.IsEmpty() ? NAME_None : FName(*Next);
			Section.Time = static_cast<float>(Time);
			if (Section.Name.IsNone() || SectionNames.Contains(Section.Name) || Section.Time >= Duration || (Sections.IsEmpty() ? Section.Time != 0.0f : Section.Time <= Sections.Last().Time)) return Fail(TEXT("Section names must be unique and not None; times must strictly increase from zero and remain below montage duration."));
			SectionNames.Add(Section.Name);
			Sections.Add(Section);
		}
		for (const FTDMontageSection& Section : Sections)
		{
			if (!Section.Next.IsNone() && !SectionNames.Contains(Section.Next)) return Fail(TEXT("Every section next link must name an existing section."));
		}
	}
	else
	{
		FTDMontageSection Section;
		Section.Name = TEXT("Start");
		Sections.Add(Section);
	}
	FTDAuthoringPackageScope PackageScope(PackagePath);
	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->PreviewSkeletalMesh = PreviewMesh;
	UFactory* AssetFactory = Factory;
	UAnimMontage* Montage = Cast<UAnimMontage>(AssetFactory->FactoryCreateNew(UAnimMontage::StaticClass(), PackageScope.Get(), FName(*FPackageName::GetLongPackageAssetName(PackagePath)), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (!Montage) return Fail(TEXT("AnimMontage factory failed."));
	PackageScope.Track(Montage);
	Montage->SlotAnimTracks.SetNum(1);
	Montage->SlotAnimTracks[0].SlotName = SlotName;
	Montage->SlotAnimTracks[0].AnimTrack.AnimSegments = MoveTemp(Segments);
	Montage->SetCompositeLength(Duration);
	Montage->CompositeSections.Reset();
	for (const FTDMontageSection& Section : Sections)
	{
		const int32 Index = Montage->AddAnimCompositeSection(Section.Name, Section.Time);
		if (!Montage->CompositeSections.IsValidIndex(Index))
		{
			return Fail(TEXT("Montage rejected a section; the incomplete asset was discarded."));
		}
	}
	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		Montage->CompositeSections[Index].NextSectionName = Sections[Index].Next;
	}
	Montage->BlendIn.SetBlendTime(static_cast<float>(BlendIn));
	Montage->BlendOut.SetBlendTime(static_cast<float>(BlendOut));
	Montage->PostEditChange();
	PackageScope.Commit();
	return FinishCreation(Montage, bShouldSave);
}

FString UTDAnimationAuthoringTools::InspectAnimation(const FString& AssetPath)
{
	const FTDAuthoringToolScope ToolScope(TEXT("InspectAnimation"), AssetPath);
	FString Error;
	FString ObjectPath;
	if (!CheckEditor(Error) || !NormalizeReadPath(AssetPath, ObjectPath, Error)) return Fail(Error);
	UAnimationAsset* Asset = LoadObject<UAnimationAsset>(nullptr, *ObjectPath);
	if (!Asset) return Fail(TEXT("Animation asset was not found at the requested path."));
	const TSharedRef<FJsonObject> Json = AnimationSummary(Asset);
	if (const UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
	{
		const IAnimationDataModel* Model = Sequence->GetDataModel();
		if (!Model) return Fail(TEXT("Sequence has no animation data model."));
		Json->SetNumberField(TEXT("fps"), Model->GetFrameRate().AsDecimal());
		Json->SetNumberField(TEXT("num_frames"), Model->GetNumberOfFrames());
		Json->SetNumberField(TEXT("num_keys"), Model->GetNumberOfKeys());
		Json->SetNumberField(TEXT("track_count"), Model->GetNumBoneTracks());
		Json->SetNumberField(TEXT("rate_scale"), Sequence->RateScale);
		TArray<FName> Names;
		Model->GetBoneTrackNames(Names);
		TArray<TSharedPtr<FJsonValue>> Tracks;
		for (FName Name : Names)
		{
			const TSharedRef<FJsonObject> Track = MakeShared<FJsonObject>();
			Track->SetStringField(TEXT("bone"), Name.ToString());
			TArray<TSharedPtr<FJsonValue>> Samples;
			TArray<int32> Frames;
			Frames.AddUnique(0);
			Frames.AddUnique(Model->GetNumberOfFrames() / 2);
			Frames.AddUnique(Model->GetNumberOfFrames());
			for (int32 Frame : Frames)
			{
				const TSharedRef<FJsonObject> Sample = TransformJson(Model->GetBoneTrackTransform(Name, FFrameNumber(Frame)));
				Sample->SetNumberField(TEXT("frame"), Frame);
				Samples.Add(MakeShared<FJsonValueObject>(Sample));
			}
			Track->SetArrayField(TEXT("samples"), Samples);
			Tracks.Add(MakeShared<FJsonValueObject>(Track));
		}
		Json->SetArrayField(TEXT("tracks"), Tracks);
		return WriteAnimationResult(Json);
	}
	if (const UAnimMontage* Montage = Cast<UAnimMontage>(Asset))
	{
		Json->SetNumberField(TEXT("blend_in"), Montage->GetDefaultBlendInTime());
		Json->SetNumberField(TEXT("blend_out"), Montage->GetDefaultBlendOutTime());
		TArray<TSharedPtr<FJsonValue>> Slots;
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
		{
			const TSharedRef<FJsonObject> SlotJson = MakeShared<FJsonObject>();
			SlotJson->SetStringField(TEXT("slot"), Slot.SlotName.ToString());
			TArray<TSharedPtr<FJsonValue>> Segments;
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
			{
				const TSharedRef<FJsonObject> SegmentJson = MakeShared<FJsonObject>();
				SegmentJson->SetStringField(TEXT("sequence"), GetPathNameSafe(Segment.GetAnimReference()));
				SegmentJson->SetNumberField(TEXT("start_position"), Segment.StartPos);
				SegmentJson->SetNumberField(TEXT("start_time"), Segment.AnimStartTime);
				SegmentJson->SetNumberField(TEXT("end_time"), Segment.AnimEndTime);
				SegmentJson->SetNumberField(TEXT("play_rate"), Segment.AnimPlayRate);
				SegmentJson->SetNumberField(TEXT("loop_count"), Segment.LoopingCount);
				SegmentJson->SetNumberField(TEXT("duration"), Segment.GetLength());
				Segments.Add(MakeShared<FJsonValueObject>(SegmentJson));
			}
			SlotJson->SetArrayField(TEXT("segments"), Segments);
			Slots.Add(MakeShared<FJsonValueObject>(SlotJson));
		}
		Json->SetArrayField(TEXT("slots"), Slots);
		TArray<TSharedPtr<FJsonValue>> Sections;
		for (const FCompositeSection& Section : Montage->CompositeSections)
		{
			const TSharedRef<FJsonObject> SectionJson = MakeShared<FJsonObject>();
			SectionJson->SetStringField(TEXT("name"), Section.SectionName.ToString());
			SectionJson->SetNumberField(TEXT("time"), Section.GetTime());
			SectionJson->SetStringField(TEXT("next"), Section.NextSectionName.IsNone() ? FString() : Section.NextSectionName.ToString());
			Sections.Add(MakeShared<FJsonValueObject>(SectionJson));
		}
		Json->SetArrayField(TEXT("sections"), Sections);
		return WriteAnimationResult(Json);
	}
	return Fail(TEXT("Only AnimSequence and AnimMontage assets are supported by InspectAnimation."));
}
