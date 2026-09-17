#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "TDControlRigTools.generated.h"

UCLASS()
class UTDControlRigTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	UFUNCTION(meta = (AICallable, ToolTip = "Create a NEW LevelSequence with a skeletal mesh spawnable and a native FK Control Rig, ready for official SequencerControlRigTools keying. RequestJson object: {asset_path: new /Game package path without extension, skeletal_mesh: existing SkeletalMesh package or object path, fps: integer 1..240 (default 30), num_frames: integer 2..18000 (default 30), save: boolean (default true)}. Unknown fields and existing output packages are rejected. Playback covers frames 0 through num_frames-1. No persistent level actors or Blueprint graphs are changed. Returns JSON with success, asset_path, sequence, binding, control_rig_asset_path, control_rig, controls, control_count, fps, num_frames, saved. Use returned control_rig_asset_path /Script/ControlRig.FKControlRig with native get_controls_info and set_transform tools; FK transform values are local offsets from reference pose. Open the returned sequence for preview and keying, then bake its binding with TDSequencerAnimationTools.BakeAnimation."))
	static FString CreateFKSequence(const FString& RequestJson);
};
