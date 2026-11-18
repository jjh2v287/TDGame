#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "Tests/TDFluidGpuFog3DDumpFiles.h"
#include "Tests/TDFluidGpuFog3DReplay.h"

namespace TDFluidGpuFog3DInputOcclusionRhiTestPrivate
{
	const TCHAR* const OcclusionReplayFolders[] = { TEXT("scenes"), TEXT("r5"), TEXT("b4"), TEXT("stage1") };

	const TCHAR* const OcclusionUnchangedSceneNames[] = {
		TEXT("b4_walk_stand_h60_g100"), TEXT("b4_walk_stand_h60_g125"), TEXT("b4_walk_stand_h30_g100"), TEXT("b4_walk_stand_h30_g125"), TEXT("b4_walk_stand_h144_g100"),
		TEXT("b4_blade_h60_g100"), TEXT("b4_blade_h60_g125"), TEXT("b4_blade_h30_g100"), TEXT("b4_blade_h30_g125"),
		TEXT("b4_turn_stand_h60_g100"), TEXT("b4_turn_stand_h60_g125"), TEXT("b4_turn_stand_h30_g100"), TEXT("b4_turn_stand_h30_g125"),
		TEXT("b4_slope_walk_up_h60_g125"),
		TEXT("s2a_flat_offset"), TEXT("s2a_slope_walk_up"), TEXT("s2a_slope_walk_down"), TEXT("s2a_slope_y_walk"), TEXT("s2a_frame_at_edge") };

	struct FTDFog3DOcclusionDump
	{
		FString Name;
		TArray<float> Values;
	};

	struct FTDFog3DOcclusionRun
	{
		FTDFog3DReplayRunResult Result;
		TArray<FTDFog3DOcclusionDump> Dumps;
		int32 CheckedDumpCount = 0;
		int32 ReachDumpCount = 0;
		int32 NonFiniteCount = 0;
	};

	FString GetOcclusionReplayDirectory(const TCHAR* Folder)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/FluidFX/fog3d_ref/replays_occ"), Folder));
	}

	FString GetOcclusionDumpRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/Fog3D/gpu_occ")));
	}

	FString GetOcclusionDumpDirectory(const TCHAR* Folder)
	{
		return FPaths::Combine(GetOcclusionDumpRoot(), Folder, TEXT("on"));
	}

	bool CanRenderOcclusion()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	bool IsOcclusionUnchangedScene(const FString& SceneName)
	{
		for (const TCHAR* UnchangedName : OcclusionUnchangedSceneNames)
		{
			if (SceneName.Equals(UnchangedName, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	int32 CountSpheresWithoutAnchor(const FTDFog3DReplay& Replay)
	{
		int32 MissingCount = 0;
		for (const FTDFog3DReplayStep& Step : Replay.Steps)
		{
			for (const FTDFog3DReplaySphere& Sphere : Step.Spheres)
			{
				MissingCount += Sphere.bHasOcclusionAnchor ? 0 : 1;
			}
		}
		return MissingCount;
	}

	int32 CountOcclusionNonFiniteValues(const TArray<float>* Values)
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

	FTDFog3DOcclusionRun RunOcclusionReplay(const FTDFog3DReplay& Replay, const FString& DumpDirectory, bool bWritesDumpFiles, bool bKeepsDumps)
	{
		FTDFog3DOcclusionRun Run;
		const int32 CellCount = Replay.Resolution * Replay.Resolution * TDFluidGpuFog3DLayerCount;
		FTDFog3DReplayRunOptions Options;
		Options.bWritesDumpFiles = bWritesDumpFiles;
		Options.DumpDirectory = DumpDirectory;
		Options.OnDump = [&Run, bKeepsDumps, CellCount](const FTDFog3DReplayDump& Dump)
		{
			++Run.CheckedDumpCount;
			Run.NonFiniteCount += CountOcclusionNonFiniteValues(Dump.Values);
			Run.ReachDumpCount += Dump.ReachValues && Dump.ReachValues->Num() == CellCount ? 1 : 0;
			if (!bKeepsDumps || !Dump.Values)
			{
				return;
			}
			FTDFog3DOcclusionDump& Kept = Run.Dumps.AddDefaulted_GetRef();
			Kept.Name = Dump.Name;
			Kept.Values = *Dump.Values;
		};
		Run.Result = RunTDFog3DReplay(Replay, Options);
		return Run;
	}

	int32 CountDifferentOcclusionDumpValues(const FTDFog3DOcclusionDump& OnDump, const FTDFog3DOcclusionDump& OffDump)
	{
		int32 DifferentCount = 0;
		for (int32 ValueIndex = 0; ValueIndex < OnDump.Values.Num(); ++ValueIndex)
		{
			DifferentCount += FMemory::Memcmp(&OnDump.Values[ValueIndex], &OffDump.Values[ValueIndex], sizeof(float)) == 0 ? 0 : 1;
		}
		return DifferentCount;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DInputOcclusionReplayDumpsTest,
	"TDGame.FluidFXRhi.Fog3D.InputOcclusionReplayDumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DInputOcclusionReplayDumpsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DInputOcclusionRhiTestPrivate;

	if (!CanRenderOcclusion())
	{
		AddError(TEXT("Fog3D InputOcclusionReplayDumps needs a rendering RHI."));
		return false;
	}

	IFileManager::Get().DeleteDirectory(*GetOcclusionDumpRoot(), false, true);
	int32 ReplayCount = 0;
	int32 FailedReplayCount = 0;
	int32 TotalDumpCount = 0;
	int32 TotalReachDumpCount = 0;
	int32 UnchangedSceneCount = 0;
	int32 UnchangedDifferentValueCount = 0;
	for (const TCHAR* Folder : OcclusionReplayFolders)
	{
		const FString ReplayDirectory = GetOcclusionReplayDirectory(Folder);
		const FString DumpDirectory = GetOcclusionDumpDirectory(Folder);
		TArray<FString> ReplayFileNames;
		FString ManifestError;
		if (!TestTrue(*FString::Printf(TEXT("%s: occlusion replay manifest read"), Folder), LoadTDFog3DReplayManifestNames(ReplayDirectory, ReplayFileNames, ManifestError)))
		{
			AddError(ManifestError);
			++FailedReplayCount;
			continue;
		}
		if (!TestTrue(*FString::Printf(TEXT("%s: occlusion manifest lists replays"), Folder), ReplayFileNames.Num() > 0))
		{
			++FailedReplayCount;
			continue;
		}

		for (const FString& ReplayFileName : ReplayFileNames)
		{
			++ReplayCount;
			FTDFog3DReplay Replay;
			FString Error;
			if (!LoadTDFog3DReplay(FPaths::Combine(ReplayDirectory, ReplayFileName), Replay, Error))
			{
				AddError(Error);
				++FailedReplayCount;
				continue;
			}

			const bool bIsUnchangedScene = IsOcclusionUnchangedScene(Replay.Name);
			const int32 MissingAnchorCount = CountSpheresWithoutAnchor(Replay);
			const FTDFog3DOcclusionRun Run = RunOcclusionReplay(Replay, DumpDirectory, true, bIsUnchangedScene);
			TestTrue(*FString::Printf(TEXT("%s: replay turns input occlusion on"), *Replay.Name), Replay.bOccludesInputBehindObstacles);
			TestEqual(*FString::Printf(TEXT("%s: every sphere has an occlusion anchor"), *Replay.Name), MissingAnchorCount, 0);
			TestEqual(*FString::Printf(TEXT("%s: every compute accepted"), *Replay.Name), Run.Result.RejectedComputeCount, 0);
			TestEqual(*FString::Printf(TEXT("%s: computes run"), *Replay.Name), Run.Result.ComputeCount, Replay.ComputeCount);
			TestEqual(*FString::Printf(TEXT("%s: every dump read back"), *Replay.Name), Run.Result.FailedDumpCount, 0);
			TestEqual(*FString::Printf(TEXT("%s: every listed dump written"), *Replay.Name), Run.Result.WrittenFileCount, Replay.DumpCount);
			TestEqual(*FString::Printf(TEXT("%s: dump files without write errors"), *Replay.Name), Run.Result.FailedFileCount, 0);
			TestEqual(*FString::Printf(TEXT("%s: every dump checked"), *Replay.Name), Run.CheckedDumpCount, Replay.DumpCount);
			TestEqual(*FString::Printf(TEXT("%s: every dump has reach bits"), *Replay.Name), Run.ReachDumpCount, Replay.DumpCount);
			TestEqual(*FString::Printf(TEXT("%s: fields are finite"), *Replay.Name), Run.NonFiniteCount, 0);
			bool bIsClean = Replay.bOccludesInputBehindObstacles && MissingAnchorCount == 0 && Run.Result.RejectedComputeCount == 0 && Run.Result.ComputeCount == Replay.ComputeCount
				&& Run.Result.FailedDumpCount == 0 && Run.Result.WrittenFileCount == Replay.DumpCount && Run.Result.FailedFileCount == 0 && Run.CheckedDumpCount == Replay.DumpCount
				&& Run.ReachDumpCount == Replay.DumpCount && Run.NonFiniteCount == 0;
			TotalDumpCount += Run.Result.WrittenFileCount;
			TotalReachDumpCount += Run.ReachDumpCount;
			AddInfo(FString::Printf(TEXT("Fog3D InputOcclusionReplayDumps %s/%s: %d computes, %d dumps, %.1f s"), Folder, *Replay.Name, Run.Result.ComputeCount, Run.Result.WrittenFileCount, Run.Result.Seconds));

			if (bIsUnchangedScene)
			{
				++UnchangedSceneCount;
				FTDFog3DReplay OffReplay = Replay;
				OffReplay.bOccludesInputBehindObstacles = false;
				const FTDFog3DOcclusionRun OffRun = RunOcclusionReplay(OffReplay, DumpDirectory, false, true);
				TestEqual(*FString::Printf(TEXT("%s: occlusion off accepts every compute"), *Replay.Name), OffRun.Result.RejectedComputeCount, 0);
				TestEqual(*FString::Printf(TEXT("%s: occlusion off takes every dump"), *Replay.Name), OffRun.Dumps.Num(), Run.Dumps.Num());
				bool bHasShapeError = OffRun.Result.RejectedComputeCount != 0 || OffRun.Dumps.Num() != Run.Dumps.Num() || Run.Dumps.Num() != Replay.DumpCount;
				int32 DifferentValueCount = 0;
				for (int32 DumpIndex = 0; DumpIndex < FMath::Min(Run.Dumps.Num(), OffRun.Dumps.Num()); ++DumpIndex)
				{
					const FTDFog3DOcclusionDump& OnDump = Run.Dumps[DumpIndex];
					const FTDFog3DOcclusionDump& OffDump = OffRun.Dumps[DumpIndex];
					if (OnDump.Name != OffDump.Name || OnDump.Values.Num() != OffDump.Values.Num())
					{
						AddError(FString::Printf(TEXT("%s: occlusion off dump %d does not line up (%s, %s)"), *Replay.Name, DumpIndex, *OnDump.Name, *OffDump.Name));
						bHasShapeError = true;
						continue;
					}
					const int32 DumpDifferentCount = CountDifferentOcclusionDumpValues(OnDump, OffDump);
					DifferentValueCount += DumpDifferentCount;
					AddInfo(FString::Printf(TEXT("Fog3D InputOcclusionReplayDumps %s occlusion on vs off %s: different values %d"), *Replay.Name, *OnDump.Name, DumpDifferentCount));
				}
				TestEqual(*FString::Printf(TEXT("%s: occlusion on matches occlusion off bit for bit"), *Replay.Name), DifferentValueCount, 0);
				UnchangedDifferentValueCount += DifferentValueCount;
				bIsClean = bIsClean && !bHasShapeError && DifferentValueCount == 0;
			}
			FailedReplayCount += bIsClean ? 0 : 1;
		}
	}

	TestEqual(TEXT("Every unchanged group scene was found and run with occlusion off too"), UnchangedSceneCount, static_cast<int32>(UE_ARRAY_COUNT(OcclusionUnchangedSceneNames)));
	AddInfo(FString::Printf(TEXT("Fog3D InputOcclusionReplayDumps: %d replays, failed %d, %d dumps and %d reach dumps written to %s, unchanged group %d scenes with %d different values"),
		ReplayCount, FailedReplayCount, TotalDumpCount, TotalReachDumpCount, *GetOcclusionDumpRoot(), UnchangedSceneCount, UnchangedDifferentValueCount));
	TestEqual(TEXT("Every input occlusion replay ran and dumped cleanly"), FailedReplayCount, 0);
	return true;
}

#endif
