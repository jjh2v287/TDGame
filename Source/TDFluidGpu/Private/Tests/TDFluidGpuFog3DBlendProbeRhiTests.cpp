#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "Tests/TDFluidGpuFog3DDumpFiles.h"
#include "Tests/TDFluidGpuFog3DReplay.h"

namespace TDFluidGpuFog3DBlendProbeRhiTestPrivate
{
	FString GetBlendProbeReplayDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/FluidFX/fog3d_ref/replays_blend")));
	}

	FString GetBlendProbeDumpDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/Fog3D/gpu_blend")));
	}

	bool CanRenderBlendProbe()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	int32 CountBlendProbeNonFiniteValues(const TArray<float>* Values)
	{
		if (!Values)
		{
			return 1;
		}
		int32 NonFiniteCount = 0;
		for (const float Value : *Values)
		{
			NonFiniteCount += FMath::IsFinite(Value) ? 0 : 1;
		}
		return NonFiniteCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DBlendProbeReplayDumpsTest,
	"TDGame.FluidFXRhi.Fog3D.BlendProbeReplayDumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DBlendProbeReplayDumpsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DBlendProbeRhiTestPrivate;

	if (!CanRenderBlendProbe())
	{
		AddError(TEXT("Fog3D BlendProbeReplayDumps needs a rendering RHI."));
		return false;
	}

	const FString ReplayDirectory = GetBlendProbeReplayDirectory();
	const FString DumpDirectory = GetBlendProbeDumpDirectory();
	TArray<FString> FileNames;
	FString ManifestError;
	if (!TestTrue(TEXT("Blend probe replay manifest read"), LoadTDFog3DReplayManifestNames(ReplayDirectory, FileNames, ManifestError)))
	{
		AddError(ManifestError);
		return false;
	}
	TestTrue(TEXT("Blend probe manifest lists replays"), FileNames.Num() > 0);
	IFileManager::Get().DeleteDirectory(*DumpDirectory, false, true);

	int32 FailedReplayCount = 0;
	int32 TotalDumpCount = 0;
	for (const FString& FileName : FileNames)
	{
		FTDFog3DReplay Replay;
		FString Error;
		if (!LoadTDFog3DReplay(FPaths::Combine(ReplayDirectory, FileName), Replay, Error))
		{
			AddError(Error);
			++FailedReplayCount;
			continue;
		}

		int32 NonFiniteCount = 0;
		int32 CheckedDumpCount = 0;
		FTDFog3DReplayRunOptions Options;
		Options.bWritesDumpFiles = true;
		Options.DumpDirectory = DumpDirectory;
		Options.OnDump = [&](const FTDFog3DReplayDump& Dump)
		{
			++CheckedDumpCount;
			NonFiniteCount += CountBlendProbeNonFiniteValues(Dump.Values);
		};
		const FTDFog3DReplayRunResult Result = RunTDFog3DReplay(Replay, Options);
		TestEqual(*FString::Printf(TEXT("%s: every compute accepted"), *Replay.Name), Result.RejectedComputeCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: computes run"), *Replay.Name), Result.ComputeCount, Replay.ComputeCount);
		TestEqual(*FString::Printf(TEXT("%s: every listed dump written"), *Replay.Name), Result.WrittenFileCount, Replay.DumpCount);
		TestEqual(*FString::Printf(TEXT("%s: dump files without write errors"), *Replay.Name), Result.FailedFileCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: every dump checked"), *Replay.Name), CheckedDumpCount, Replay.DumpCount);
		TestEqual(*FString::Printf(TEXT("%s: fields are finite"), *Replay.Name), NonFiniteCount, 0);

		const bool bIsClean = Result.RejectedComputeCount == 0 && Result.ComputeCount == Replay.ComputeCount && Result.WrittenFileCount == Replay.DumpCount
			&& Result.FailedFileCount == 0 && CheckedDumpCount == Replay.DumpCount && NonFiniteCount == 0;
		FailedReplayCount += bIsClean ? 0 : 1;
		TotalDumpCount += Result.WrittenFileCount;
		AddInfo(FString::Printf(TEXT("Fog3D BlendProbeReplayDumps %s: %d computes, %d dumps, %.1f s"), *Replay.Name, Result.ComputeCount, Result.WrittenFileCount, Result.Seconds));
	}

	AddInfo(FString::Printf(TEXT("Fog3D BlendProbeReplayDumps: %d replays, failed %d, %d dumps written to %s"), FileNames.Num(), FailedReplayCount, TotalDumpCount, *DumpDirectory));
	TestEqual(TEXT("Every blend probe replay ran and dumped cleanly"), FailedReplayCount, 0);
	return true;
}

#endif
