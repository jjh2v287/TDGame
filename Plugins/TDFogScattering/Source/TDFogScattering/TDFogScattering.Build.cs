using System.IO;
using UnrealBuildTool;

public class TDFogScattering : ModuleRules
{
	public TDFogScattering(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		string RendererDirectory = GetModuleDirectory("Renderer");
		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(RendererDirectory, "Private"),
			Path.Combine(RendererDirectory, "Internal")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"Renderer",
			"RHI",
			"Projects"
		});
	}
}
