#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/TDFluidGpuFog3DDumpFiles.h"
#include "Tests/TDFluidGpuFog3DReplay.h"

namespace TDFluidGpuFog3DClampedTimeBlendRhiTestPrivate
{
	constexpr int32 TwinComparedFieldCount = 8;
	const int32 TwinComparedFieldIndices[TwinComparedFieldCount] = { 0, 1, 2, 3, 4, 5, 6, TDFog3DReplayFluidFieldIndex };
	const TCHAR* const TwinComparedFieldNames[TwinComparedFieldCount] = { TEXT("u"), TEXT("v"), TEXT("w"), TEXT("dx"), TEXT("dy"), TEXT("dz"), TEXT("g"), TEXT("fluid") };

	struct FTDFog3DClampedTimeBlendReplayEntry
	{
		FString FileName;
		bool bHasGroundOffTwin = false;
	};

	struct FTDFog3DClampedTimeBlendDump
	{
		FString Name;
		TArray<float> Values;
	};

	struct FTDFog3DClampedTimeBlendRun
	{
		FTDFog3DReplayRunResult Result;
		TArray<FTDFog3DClampedTimeBlendDump> Dumps;
		int32 CheckedDumpCount = 0;
		int32 NonFiniteCount = 0;
	};

	struct FTDFog3DGroundOffTwinCompare
	{
		int32 DifferentCellCounts[TwinComparedFieldCount] = {};
		int32 TotalDifferentCellCount = 0;
	};

	FString GetClampedTimeBlendReplayDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/FluidFX/fog3d_ref/replays_blend_b4")));
	}

	FString GetClampedTimeBlendDumpDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/Fog3D/gpu_blend_b4")));
	}

	bool CanRenderClampedTimeBlend()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	bool ReadClampedTimeBlendManifestEntry(const FJsonObject& FileObject, const FString& ManifestPath, TArray<FTDFog3DClampedTimeBlendReplayEntry>& OutEntries, FString& OutError)
	{
		FString Name;
		if (!FileObject.TryGetStringField(TEXT("name"), Name))
		{
			OutError = FString::Printf(TEXT("%s lists a file without a name"), *ManifestPath);
			return false;
		}
		const FString Extension = FPaths::GetExtension(Name);
		if (!Extension.IsEmpty() && Extension != TEXT("json"))
		{
			return true;
		}
		FTDFog3DClampedTimeBlendReplayEntry& Entry = OutEntries.AddDefaulted_GetRef();
		Entry.FileName = Extension.IsEmpty() ? Name + TEXT(".json") : Name;
		if (FileObject.HasField(TEXT("ground_off_twin")) && !FileObject.TryGetBoolField(TEXT("ground_off_twin"), Entry.bHasGroundOffTwin))
		{
			OutError = FString::Printf(TEXT("%s gives %s a ground_off_twin that is not true or false"), *ManifestPath, *Name);
			return false;
		}
		return true;
	}

	bool LoadClampedTimeBlendManifest(const FString& Directory, TArray<FTDFog3DClampedTimeBlendReplayEntry>& OutEntries, FString& OutError)
	{
		OutEntries.Reset();
		const FString ManifestPath = FPaths::Combine(Directory, TEXT("manifest.json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *ManifestPath))
		{
			OutError = FString::Printf(TEXT("no replay bundle: cannot read %s"), *ManifestPath);
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("files"), Files))
		{
			OutError = FString::Printf(TEXT("cannot parse the file list of %s"), *ManifestPath);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& File : *Files)
		{
			const TSharedPtr<FJsonObject>* FileObject = nullptr;
			if (!File.IsValid() || !File->TryGetObject(FileObject))
			{
				OutError = FString::Printf(TEXT("%s lists a file that is not an object"), *ManifestPath);
				return false;
			}
			if (!ReadClampedTimeBlendManifestEntry(**FileObject, ManifestPath, OutEntries, OutError))
			{
				return false;
			}
		}
		return true;
	}

	int32 CountClampedTimeBlendNonFiniteValues(const TArray<float>* Values)
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

	FTDFog3DClampedTimeBlendRun RunClampedTimeBlendReplay(const FTDFog3DReplay& Replay, bool bWritesDumpFiles, bool bKeepsDumps)
	{
		FTDFog3DClampedTimeBlendRun Run;
		FTDFog3DReplayRunOptions Options;
		Options.bWritesDumpFiles = bWritesDumpFiles;
		Options.DumpDirectory = GetClampedTimeBlendDumpDirectory();
		Options.OnDump = [&Run, bKeepsDumps](const FTDFog3DReplayDump& Dump)
		{
			++Run.CheckedDumpCount;
			Run.NonFiniteCount += CountClampedTimeBlendNonFiniteValues(Dump.Values);
			if (!bKeepsDumps || !Dump.Values)
			{
				return;
			}
			FTDFog3DClampedTimeBlendDump& Kept = Run.Dumps.AddDefaulted_GetRef();
			Kept.Name = Dump.Name;
			Kept.Values = *Dump.Values;
		};
		Run.Result = RunTDFog3DReplay(Replay, Options);
		return Run;
	}

	FTDFog3DReplay MakeGroundOffTwinReplay(const FTDFog3DReplay& Replay)
	{
		FTDFog3DReplay Twin = Replay;
		Twin.bHasGround = false;
		Twin.GroundSourceSize = FIntPoint::ZeroValue;
		Twin.GroundUpload = FTDFluidGpuFog3DGroundUpload();
		return Twin;
	}

	FTDFog3DGroundOffTwinCompare CompareGroundOffTwinDump(const TArray<float>& GroundOnValues, const TArray<float>& GroundOffValues, int32 CellCount)
	{
		FTDFog3DGroundOffTwinCompare Compare;
		for (int32 ListIndex = 0; ListIndex < TwinComparedFieldCount; ++ListIndex)
		{
			const int64 FieldOffset = static_cast<int64>(CellCount) * TwinComparedFieldIndices[ListIndex];
			for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
			{
				const bool bIsSame = FMemory::Memcmp(&GroundOnValues[FieldOffset + CellIndex], &GroundOffValues[FieldOffset + CellIndex], sizeof(float)) == 0;
				Compare.DifferentCellCounts[ListIndex] += bIsSame ? 0 : 1;
			}
			Compare.TotalDifferentCellCount += Compare.DifferentCellCounts[ListIndex];
		}
		return Compare;
	}

	FString DescribeGroundOffTwinCompare(const FTDFog3DGroundOffTwinCompare& Compare)
	{
		FString Description;
		for (int32 ListIndex = 0; ListIndex < TwinComparedFieldCount; ++ListIndex)
		{
			Description += FString::Printf(TEXT("%s%s %d"), ListIndex == 0 ? TEXT("") : TEXT(", "), TwinComparedFieldNames[ListIndex], Compare.DifferentCellCounts[ListIndex]);
		}
		return Description;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFog3DClampedTimeBlendReplayDumpsTest,
	"TDGame.FluidFXRhi.Fog3D.ClampedTimeBlendReplayDumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFog3DClampedTimeBlendReplayDumpsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFog3DClampedTimeBlendRhiTestPrivate;

	if (!CanRenderClampedTimeBlend())
	{
		AddError(TEXT("Fog3D ClampedTimeBlendReplayDumps needs a rendering RHI."));
		return false;
	}

	const FString ReplayDirectory = GetClampedTimeBlendReplayDirectory();
	const FString DumpDirectory = GetClampedTimeBlendDumpDirectory();
	IFileManager::Get().DeleteDirectory(*DumpDirectory, false, true);
	TArray<FTDFog3DClampedTimeBlendReplayEntry> Entries;
	FString ManifestError;
	if (!TestTrue(TEXT("Clamped time blend replay manifest read"), LoadClampedTimeBlendManifest(ReplayDirectory, Entries, ManifestError)))
	{
		AddError(ManifestError);
		return false;
	}
	TestTrue(TEXT("Clamped time blend manifest lists replays"), Entries.Num() > 0);

	int32 FailedReplayCount = 0;
	int32 TotalDumpCount = 0;
	int32 TwinReplayCount = 0;
	for (const FTDFog3DClampedTimeBlendReplayEntry& Entry : Entries)
	{
		FTDFog3DReplay Replay;
		FString Error;
		if (!LoadTDFog3DReplay(FPaths::Combine(ReplayDirectory, Entry.FileName), Replay, Error))
		{
			AddError(Error);
			++FailedReplayCount;
			continue;
		}

		const FTDFog3DClampedTimeBlendRun Run = RunClampedTimeBlendReplay(Replay, true, Entry.bHasGroundOffTwin);
		TestTrue(*FString::Printf(TEXT("%s: replay uses wind_blend B4"), *Replay.Name), Replay.bUsesClampedTimeWindBlend);
		TestEqual(*FString::Printf(TEXT("%s: every compute accepted"), *Replay.Name), Run.Result.RejectedComputeCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: computes run"), *Replay.Name), Run.Result.ComputeCount, Replay.ComputeCount);
		TestEqual(*FString::Printf(TEXT("%s: every listed dump written"), *Replay.Name), Run.Result.WrittenFileCount, Replay.DumpCount);
		TestEqual(*FString::Printf(TEXT("%s: dump files without write errors"), *Replay.Name), Run.Result.FailedFileCount, 0);
		TestEqual(*FString::Printf(TEXT("%s: every dump checked"), *Replay.Name), Run.CheckedDumpCount, Replay.DumpCount);
		TestEqual(*FString::Printf(TEXT("%s: fields are finite"), *Replay.Name), Run.NonFiniteCount, 0);
		bool bIsClean = Replay.bUsesClampedTimeWindBlend && Run.Result.RejectedComputeCount == 0 && Run.Result.ComputeCount == Replay.ComputeCount && Run.Result.WrittenFileCount == Replay.DumpCount
			&& Run.Result.FailedFileCount == 0 && Run.CheckedDumpCount == Replay.DumpCount && Run.NonFiniteCount == 0;
		TotalDumpCount += Run.Result.WrittenFileCount;
		AddInfo(FString::Printf(TEXT("Fog3D ClampedTimeBlendReplayDumps %s: %d computes, %d dumps, %.1f s"), *Replay.Name, Run.Result.ComputeCount, Run.Result.WrittenFileCount, Run.Result.Seconds));

		if (Entry.bHasGroundOffTwin)
		{
			++TwinReplayCount;
			TestTrue(*FString::Printf(TEXT("%s: the ground off twin starts from a replay that follows ground"), *Replay.Name), Replay.bHasGround);
			const FTDFog3DClampedTimeBlendRun TwinRun = RunClampedTimeBlendReplay(MakeGroundOffTwinReplay(Replay), false, true);
			const int32 CellCount = Replay.Resolution * Replay.Resolution * TDFluidGpuFog3DLayerCount;
			const int32 DumpValueCount = CellCount * TDFog3DReplayDumpFieldCount;
			TestEqual(*FString::Printf(TEXT("%s: ground off twin accepts every compute"), *Replay.Name), TwinRun.Result.RejectedComputeCount, 0);
			TestEqual(*FString::Printf(TEXT("%s: ground off twin takes every dump"), *Replay.Name), TwinRun.Dumps.Num(), Run.Dumps.Num());
			int32 TwinDifferentCellCount = 0;
			bool bHasTwinShapeError = !Replay.bHasGround || TwinRun.Result.RejectedComputeCount != 0 || TwinRun.Dumps.Num() != Run.Dumps.Num() || Run.Dumps.Num() != Replay.DumpCount;
			for (int32 DumpIndex = 0; DumpIndex < FMath::Min(Run.Dumps.Num(), TwinRun.Dumps.Num()); ++DumpIndex)
			{
				const FTDFog3DClampedTimeBlendDump& GroundOnDump = Run.Dumps[DumpIndex];
				const FTDFog3DClampedTimeBlendDump& GroundOffDump = TwinRun.Dumps[DumpIndex];
				if (GroundOnDump.Name != GroundOffDump.Name || GroundOnDump.Values.Num() != DumpValueCount || GroundOffDump.Values.Num() != DumpValueCount)
				{
					AddError(FString::Printf(TEXT("%s: ground off twin dump %d does not line up (%s, %s)"), *Replay.Name, DumpIndex, *GroundOnDump.Name, *GroundOffDump.Name));
					bHasTwinShapeError = true;
					continue;
				}
				const FTDFog3DGroundOffTwinCompare Compare = CompareGroundOffTwinDump(GroundOnDump.Values, GroundOffDump.Values, CellCount);
				TwinDifferentCellCount += Compare.TotalDifferentCellCount;
				AddInfo(FString::Printf(TEXT("Fog3D ClampedTimeBlendReplayDumps %s ground off twin %s: different cells %s"), *Replay.Name, *GroundOnDump.Name, *DescribeGroundOffTwinCompare(Compare)));
			}
			TestEqual(*FString::Printf(TEXT("%s: ground off twin matches ground on bit for bit"), *Replay.Name), TwinDifferentCellCount, 0);
			bIsClean = bIsClean && !bHasTwinShapeError && TwinDifferentCellCount == 0;
		}
		FailedReplayCount += bIsClean ? 0 : 1;
	}

	AddInfo(FString::Printf(TEXT("Fog3D ClampedTimeBlendReplayDumps: %d replays, %d with a ground off twin, failed %d, %d dumps written to %s"), Entries.Num(), TwinReplayCount, FailedReplayCount, TotalDumpCount, *DumpDirectory));
	TestEqual(TEXT("Every clamped time blend replay ran and dumped cleanly"), FailedReplayCount, 0);
	return true;
}

#endif
