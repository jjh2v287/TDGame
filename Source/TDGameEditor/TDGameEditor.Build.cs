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
			"Landscape",
			"LandscapeEditor",
			"Json",
			"JsonUtilities",
			"NavigationSystem",
			"MessageLog",
			"PCG"
		});
	}
}
