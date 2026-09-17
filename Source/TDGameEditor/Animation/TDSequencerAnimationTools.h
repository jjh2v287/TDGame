#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "TDSequencerAnimationTools.generated.h"

UCLASS()
class TDGAMEEDITOR_API UTDSequencerAnimationTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	UFUNCTION(meta = (AICallable, ToolTip = "Inspect a LevelSequence's binding GUIDs, skeletal meshes, frame rate and playback range before baking animation."))
	static FString InspectSequence(const FString& SequencePath);

	UFUNCTION(meta = (AICallable, ToolTip = "Bake an explicit skeletal binding into a new AnimSequence. JSON fields: sequence, binding (GUID), skeletal_mesh, asset_path (/Game package path), save (optional, default true). Existing assets are never overwritten. Native spawnables work without opening Sequencer; other bindings require the sequence focused in Sequencer."))
	static FString BakeAnimation(const FString& RequestJson);
};
