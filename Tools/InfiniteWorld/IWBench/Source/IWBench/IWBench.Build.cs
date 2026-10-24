using UnrealBuildTool;

public class IWBench : ModuleRules
{
	public IWBench(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GeometryCore", "GeometryFramework", "PhysicsCore", "Chaos", "Json", "NavigationSystem"
		});
	}
}
