using UnrealBuildTool;

public class TDFluidGpu : ModuleRules
{
	public TDFluidGpu(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RenderCore",
			"RHI"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects"
		});
	}
}
