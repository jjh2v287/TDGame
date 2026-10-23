class FTDFogScatteringFixtureCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTDFogScatteringFixtureCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFogScatteringFixtureCS, FGlobalShader);

	class FLuminanceWeightingDimension : SHADER_PERMUTATION_BOOL("LUMINANCE_WEIGHTING");
	using FPermutationDomain = TShaderPermutationDomain<FLuminanceWeightingDimension>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTDFogScatteringFixtureCS, "/Plugin/TDFogScattering/Private/TDFogScatteringFixture.usf", "MainFixtureCS", SF_Compute);
