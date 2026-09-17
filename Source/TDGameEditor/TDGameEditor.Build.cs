using UnrealBuildTool;

public class TDGameEditor : ModuleRules
{
	public TDGameEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"TDGame",
			"TDWorldGen"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorSubsystem",
			"Landscape",
			"LandscapeEditor",
			"Json",
			"JsonUtilities",
			"NavigationSystem",
			"MessageLog",
			"ToolsetRegistry",
			"AssetRegistry",
			"AnimationDataController",
			"AnimationCore",
			"LevelSequence",
			"LevelSequenceEditor",
			"MovieScene",
			"MovieSceneTracks",
			"MovieSceneTools",
			"ControlRig",
			"ControlRigEditor",
			"SequencerScripting",
			"SequencerScriptingEditor",
			"PCG"
		});
	}
}
