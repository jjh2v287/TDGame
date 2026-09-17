#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"
#include "TDAnimationAuthoringTools.generated.h"

UCLASS()
class TDGAMEEDITOR_API UTDAnimationAuthoringTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	UFUNCTION(meta = (AICallable, ToolTip = "Inspect a /Game skeletal mesh before animation authoring. Returns JSON success/error, exact skeleton, raw bone names, parents, reference local transforms in centimeters and degrees [pitch,yaw,roll], and existing montage slots."))
	static FString InspectSkeleton(const FString& SkeletalMeshPath);

	UFUNCTION(meta = (AICallable, ToolTip = "Create a new AnimSequence without overwriting. RequestJson: {asset_path:'/Game/Folder/AS_Name',skeletal_mesh:'/Game/Folder/SK_Name',fps:30,num_frames:30,mode:'reference_offset',save:true,root_motion:{enable:false,root_lock:'RefPose',force_root_lock:false},tracks:[{bone:'root',keys:[{frame:0,translation:[0,0,0],rotation:[0,0,0],scale:[1,1,1]}]}]}. fps and num_frames are integers; frames include 0 and num_frames; duration=num_frames/fps. Keys must strictly increase. Translation is centimeters in parent axes, rotation degrees [pitch,yaw,roll] composed reference*offset, scale multiplies reference. Omitted channels default to identity offset. mode local_absolute uses local transforms. Translation interpolates linearly, rotation slerps. Untracked bones use the reference pose. root_lock is RefPose, AnimFirstFrame or Zero; key the root bone translation to drive movement. Limits: fps 1..120, frames 1..36000, 600 s, four MB JSON. Returns JSON success/error with root_motion_enabled."))
	static FString CreateBoneAnimation(const FString& RequestJson);

	UFUNCTION(meta = (AICallable, ToolTip = "Create a new AnimMontage without overwriting. RequestJson: {asset_path:'/Game/Folder/AM_Name',slot:'DefaultSlot',save:true,blend_in:0.25,blend_out:0.25,segments:[{sequence:'/Game/Folder/AS_Name',start_time:0,end_time:1,play_rate:1,loop_count:1}],sections:[{name:'Start',time:0,next:'End'},{name:'End',time:0.5}]}. Segment trims default to full sequence; positive rate defaults 1, integer loops default 1. Sequences must share the exact skeleton and use an existing slot; skeleton is never modified. Segments concatenate; section times strictly increase from zero and next links name existing sections (omitted means stop at section end). Omitted sections creates Start. Times and blends are seconds. Limits: 128 segments, 128 sections, 600 seconds. save defaults true; false creates unsaved asset. Returns JSON success/error."))
	static FString CreateMontage(const FString& RequestJson);

	UFUNCTION(meta = (AICallable, ToolTip = "Inspect a /Game AnimSequence or AnimMontage. Returns JSON success/error with asset path, skeleton and duration. Sequences include fps, num_frames, key count and each bone's raw local first/middle/last samples with translation, rotation [pitch,yaw,roll], quaternion [x,y,z,w], and scale. Montages include slots, sequential segments, blend durations and section links. Read-only; sample rotations are local absolute transforms."))
	static FString InspectAnimation(const FString& AssetPath);
};
