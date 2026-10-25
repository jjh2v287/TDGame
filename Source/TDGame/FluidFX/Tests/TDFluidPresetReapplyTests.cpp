#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidPresetReapplyTestPrivate
{
	constexpr float ReapplyStepSeconds = 0.05f;
	constexpr int32 StepsBeforeReload = 4;
	constexpr int32 StepsAfterReload = 6;
	constexpr float ChangedVorticityStrength = 0.45f;
	constexpr int32 ChangedGridResolution = 128;

	enum class ETDFluidReapplyTable : uint8
	{
		None,
		SameBase,
		ChangedVorticity,
		ChangedResolution
	};

	struct FTDFluidReapplyTestWorld
	{
		explicit FTDFluidReapplyTestWorld(EWorldType::Type WorldType)
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidReapplyTestWorld"));
			World = UWorld::CreateWorld(WorldType, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(WorldType).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidReapplyTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, ReapplyStepSeconds);
		}

		UWorld* World = nullptr;
	};

	struct FTDFluidReapplyRun
	{
		FTDFluidPresetReapplyReport Report;
		uint32 StateChecksum = 0;
		int32 GenerationBeforeReload = 0;
		int32 GenerationAfterReload = 0;
		int32 ResolutionAfterReload = 0;
		float VorticityStrengthAfterReload = 0.0f;
		bool bIsValid = false;
	};

	UTDFluidPresetTuningSubsystem* GetTuningSubsystem()
	{
		return GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	}

	FString GetVariantTuningPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/PresetReapplyTests/TDFluidPresetReapplyVariant.json")));
	}

	bool WriteVariantTuningFile(ETDFluidReapplyTable Variant)
	{
		FString BaseText;
		if (!FFileHelper::LoadFileToString(BaseText, *UTDFluidPresetTuningSubsystem::GetTuningFilePath()))
		{
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BaseText), Root) || !Root.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* Kinds = nullptr;
		const TSharedPtr<FJsonObject>* MudLayers = nullptr;
		const TSharedPtr<FJsonObject>* MudConfig = nullptr;
		if (!Root->TryGetObjectField(TEXT("Kinds"), Kinds) || !(*Kinds)->TryGetObjectField(TEXT("Mud"), MudLayers) || !(*MudLayers)->TryGetObjectField(TEXT("TDFluidSimulationConfig"), MudConfig))
		{
			return false;
		}

		if (Variant == ETDFluidReapplyTable::ChangedVorticity)
		{
			(*MudConfig)->SetNumberField(TEXT("VorticityStrength"), ChangedVorticityStrength);
		}
		if (Variant == ETDFluidReapplyTable::ChangedResolution)
		{
			(*MudConfig)->SetNumberField(TEXT("GridResolution"), ChangedGridResolution);
		}

		FString VariantText;
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&VariantText)))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(VariantText, *GetVariantTuningPath());
	}

	bool ReloadTable(UTDFluidPresetTuningSubsystem& TuningSubsystem, ETDFluidReapplyTable Variant)
	{
		if (Variant == ETDFluidReapplyTable::SameBase)
		{
			return TuningSubsystem.ReloadPresetTuning() == 0;
		}
		return WriteVariantTuningFile(Variant) && TuningSubsystem.ReloadPresetTuningFromPaths(GetVariantTuningPath(), FString()) == 0;
	}

	void QueueScenarioStamp(UTDFluidLiveComponent& Component, int32 StepIndex)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(-120.0 + 20.0 * StepIndex, 10.0, 0.0);
		Stamp.WorldVelocity = FVector(240.0, 60.0, 0.0);
		Stamp.RadiusCm = 45.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		Component.QueueBrushStamp(Stamp);
	}

	FTDFluidReapplyRun RunReapplyScenario(ETDFluidReapplyTable Variant)
	{
		FTDFluidReapplyRun Run;
		UTDFluidPresetTuningSubsystem* TuningSubsystem = GetTuningSubsystem();
		FTDFluidReapplyTestWorld TestWorld(EWorldType::Game);
		UTDFluidLiveSubsystem* LiveSubsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
		if (!TuningSubsystem || !LiveSubsystem)
		{
			return Run;
		}

		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Mud);
		ATDFluidLiveActor* LiveActor = LiveSubsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 0.0f);
		UTDFluidLiveComponent* Component = LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
		if (!Component || Component->IsUsingGpuSolver())
		{
			return Run;
		}

		FTDFluidSdfObstacle Obstacle;
		Obstacle.WorldCenter = FVector(80.0, -40.0, 0.0);
		Obstacle.RadiusCm = 30.0f;
		Component->SetSdfObstacles({ Obstacle });

		for (int32 StepIndex = 0; StepIndex < StepsBeforeReload; ++StepIndex)
		{
			QueueScenarioStamp(*Component, StepIndex);
			TestWorld.StepWorld();
		}

		Run.GenerationBeforeReload = Component->GetSimulationGeneration();
		if (Variant != ETDFluidReapplyTable::None && !ReloadTable(*TuningSubsystem, Variant))
		{
			return Run;
		}

		for (int32 StepIndex = StepsBeforeReload; StepIndex < StepsBeforeReload + StepsAfterReload; ++StepIndex)
		{
			QueueScenarioStamp(*Component, StepIndex);
			TestWorld.StepWorld();
			if (StepIndex == StepsBeforeReload)
			{
				Run.GenerationAfterReload = Component->GetSimulationGeneration();
				Run.ResolutionAfterReload = Component->GetSimulationResolution();
				Run.VorticityStrengthAfterReload = Component->SimulationConfig.VorticityStrength;
			}
		}

		Run.Report = LiveSubsystem->GetLastReapplyReport();
		Run.StateChecksum = Component->GetSolver().ComputeStateChecksum();
		Run.bIsValid = true;
		return Run;
	}

	void RestoreBaseTable()
	{
		if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GetTuningSubsystem())
		{
			TuningSubsystem->ReloadPresetTuning();
		}
		IFileManager::Get().Delete(*GetVariantTuningPath());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetReapplySameTableTest,
	"TDGame.FluidFX.PresetReapply.SameTableMatchesNoReapply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetReapplySameTableTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPresetReapplyTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreBaseTable();
	};
	RestoreBaseTable();

	const FTDFluidReapplyRun Untouched = RunReapplyScenario(ETDFluidReapplyTable::None);
	const FTDFluidReapplyRun Reapplied = RunReapplyScenario(ETDFluidReapplyTable::SameBase);
	if (!TestTrue(TEXT("Both CPU scenario runs completed"), Untouched.bIsValid && Reapplied.bIsValid))
	{
		return false;
	}

	TestEqual(TEXT("A same-table reapply keeps the solver checksum bit-identical, obstacles included"), Reapplied.StateChecksum, Untouched.StateChecksum);
	TestEqual(TEXT("The reapply reached the live actor"), Reapplied.Report.ReappliedActorCount, 1);
	TestEqual(TEXT("A same-table reapply resets no state"), Reapplied.Report.StateResetActorCount, 0);
	TestEqual(TEXT("A same-table reapply changes no field"), Reapplied.Report.ChangedFieldCount, 0);
	TestEqual(TEXT("A same-table reapply keeps the solver generation"), Reapplied.GenerationAfterReload, Reapplied.GenerationBeforeReload);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetReapplyNonStructuralChangeTest,
	"TDGame.FluidFX.PresetReapply.NonStructuralChangeKeepsStateAndRepeats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetReapplyNonStructuralChangeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPresetReapplyTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreBaseTable();
	};
	RestoreBaseTable();

	const FTDFluidReapplyRun Untouched = RunReapplyScenario(ETDFluidReapplyTable::None);
	const FTDFluidReapplyRun FirstRun = RunReapplyScenario(ETDFluidReapplyTable::ChangedVorticity);
	RestoreBaseTable();
	const FTDFluidReapplyRun SecondRun = RunReapplyScenario(ETDFluidReapplyTable::ChangedVorticity);
	if (!TestTrue(TEXT("All CPU scenario runs completed"), Untouched.bIsValid && FirstRun.bIsValid && SecondRun.bIsValid))
	{
		return false;
	}

	TestEqual(TEXT("The new vorticity reached the running component"), FirstRun.VorticityStrengthAfterReload, ChangedVorticityStrength);
	TestEqual(TEXT("A non-structural change keeps the solver generation"), FirstRun.GenerationAfterReload, FirstRun.GenerationBeforeReload);
	TestEqual(TEXT("A non-structural change resets no state"), FirstRun.Report.StateResetActorCount, 0);
	TestTrue(TEXT("The report counts the changed field"), FirstRun.Report.ChangedFieldCount >= 1);
	TestEqual(TEXT("The report names the override table"), FirstRun.Report.TuningSourcePath, GetVariantTuningPath());
	TestNotEqual(TEXT("The changed vorticity changes the evolution"), FirstRun.StateChecksum, Untouched.StateChecksum);
	TestEqual(TEXT("Two reapplies at the same step give the same checksum"), SecondRun.StateChecksum, FirstRun.StateChecksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetReapplyResolutionChangeTest,
	"TDGame.FluidFX.PresetReapply.ResolutionChangeResetsAndCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetReapplyResolutionChangeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPresetReapplyTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreBaseTable();
	};
	RestoreBaseTable();

	const FTDFluidReapplyRun Run = RunReapplyScenario(ETDFluidReapplyTable::ChangedResolution);
	if (!TestTrue(TEXT("The CPU scenario run completed"), Run.bIsValid))
	{
		return false;
	}

	TestEqual(TEXT("The new grid resolution reached the running solver"), Run.ResolutionAfterReload, ChangedGridResolution);
	TestEqual(TEXT("The report counts the reset actor"), Run.Report.StateResetActorCount, 1);
	TestNotEqual(TEXT("The reset raises the solver generation"), Run.GenerationAfterReload, Run.GenerationBeforeReload);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPresetReapplyEditorPreviewWorldTest,
	"TDGame.FluidFX.PresetReapply.NonGameWorldIsUntouched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPresetReapplyEditorPreviewWorldTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPresetReapplyTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreBaseTable();
	};
	RestoreBaseTable();

	UTDFluidPresetTuningSubsystem* TuningSubsystem = GetTuningSubsystem();
	FTDFluidReapplyTestWorld TestWorld(EWorldType::EditorPreview);
	UTDFluidLiveSubsystem* LiveSubsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	if (!TestNotNull(TEXT("Preset tuning subsystem exists"), TuningSubsystem) || !TestNotNull(TEXT("The preview world has a live subsystem"), LiveSubsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Mud);
	ATDFluidLiveActor* LiveActor = LiveSubsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 0.0f);
	UTDFluidLiveComponent* Component = LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
	if (!TestNotNull(TEXT("Spawned the preview live actor"), Component))
	{
		return false;
	}

	const float VorticityBeforeReload = Component->SimulationConfig.VorticityStrength;
	if (!TestTrue(TEXT("Loaded the changed table"), ReloadTable(*TuningSubsystem, ETDFluidReapplyTable::ChangedVorticity)))
	{
		return false;
	}

	LiveSubsystem->Tick(ReapplyStepSeconds);
	TestEqual(TEXT("A non-game world keeps its configuration"), Component->SimulationConfig.VorticityStrength, VorticityBeforeReload);
	TestEqual(TEXT("A non-game world records no reapply"), LiveSubsystem->GetLastReapplyReport().ReappliedActorCount, 0);
	return true;
}

#endif
