#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidOpsComponentTestPrivate
{
	constexpr float OpsComponentStepSeconds = 1.0f / 60.0f;
	constexpr int32 OpsComponentStampSteps = 6;
	const FVector OpsComponentMovedLocation(1000.0, 0.0, 0.0);
	const FVector OpsComponentFarLocation(100000.0, 0.0, 0.0);

	struct FTDFluidOpsComponentTestWorld
	{
		FTDFluidOpsComponentTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidOpsComponentTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidOpsComponentTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, OpsComponentStepSeconds);
		}

		UTDFluidLiveSubsystem* GetFluidSubsystem() const
		{
			return World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		}

		UTDFluidLiveComponent* SpawnMudComponent(const FVector& Location) const
		{
			UTDFluidLiveSubsystem* FluidSubsystem = GetFluidSubsystem();
			UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(World, ETDFluidPresetKind::Mud);
			ATDFluidLiveActor* LiveActor = FluidSubsystem ? FluidSubsystem->SpawnPooledFluidActor(Preset, Location, 0.0f) : nullptr;
			return LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
		}

		UWorld* World = nullptr;
	};

	UTDFluidPresetTuningSubsystem* GetTuningSubsystem()
	{
		return GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	}

	FString GetKeepBuffersTuningPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/OpsComponentTests/TDFluidOpsKeepBuffersVariant.json")));
	}

	bool WriteKeepBuffersTuningFile()
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
		if (!Root->TryGetObjectField(TEXT("Kinds"), Kinds) || !(*Kinds)->TryGetObjectField(TEXT("Mud"), MudLayers))
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* ExistingRuntimeOps = nullptr;
		TSharedPtr<FJsonObject> MudRuntimeOps = (*MudLayers)->TryGetObjectField(TEXT("TDFluidRuntimeOpsSettings"), ExistingRuntimeOps) ? *ExistingRuntimeOps : MakeShared<FJsonObject>();
		MudRuntimeOps->SetBoolField(TEXT("bKeepBuffersOnReinit"), true);
		(*MudLayers)->SetObjectField(TEXT("TDFluidRuntimeOpsSettings"), MudRuntimeOps);

		FString VariantText;
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&VariantText)))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(VariantText, *GetKeepBuffersTuningPath());
	}

	void RestoreBaseTuning()
	{
		if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GetTuningSubsystem())
		{
			TuningSubsystem->ReloadPresetTuning();
		}
		IFileManager::Get().Delete(*GetKeepBuffersTuningPath());
	}

	double SumDensity(const UTDFluidLiveComponent& Component)
	{
		const int32 Resolution = Component.GetSimulationResolution();
		double DensitySum = 0.0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FTDFluidCellSample CellSample = Component.SampleSimulationCell(CellX, CellY);
				DensitySum += CellSample.PrimaryDensity + CellSample.SelectiveDensity;
			}
		}
		return DensitySum;
	}

	void QueueCenterStamp(UTDFluidLiveComponent& Component, const FVector& Location)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Location;
		Stamp.WorldVelocity = FVector(120.0, 40.0, 0.0);
		Stamp.RadiusCm = 45.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		Component.QueueBrushStamp(Stamp);
	}

	struct FTDFluidKeepBuffersRun
	{
		double FreshDensity = 0.0;
		double StampedDensity = 0.0;
		double ReinitializedDensity = 0.0;
		bool bIsValid = false;
	};

	FTDFluidKeepBuffersRun RunKeepBuffersScenario(bool bShouldMoveBeforeReinit)
	{
		FTDFluidKeepBuffersRun Run;
		FTDFluidOpsComponentTestWorld TestWorld;
		UTDFluidLiveComponent* Component = TestWorld.SpawnMudComponent(FVector::ZeroVector);
		if (!Component || Component->IsUsingGpuSolver() || !Component->IsSimulationInitialized())
		{
			return Run;
		}

		Run.FreshDensity = SumDensity(*Component);
		for (int32 StepIndex = 0; StepIndex < OpsComponentStampSteps; ++StepIndex)
		{
			QueueCenterStamp(*Component, FVector::ZeroVector);
			TestWorld.StepWorld();
		}
		Run.StampedDensity = SumDensity(*Component);

		if (bShouldMoveBeforeReinit)
		{
			Component->GetOwner()->SetActorLocation(OpsComponentMovedLocation);
		}
		Component->InitSystemStateFromPreset(Component->ActivePreset);
		Run.ReinitializedDensity = SumDensity(*Component);
		Run.bIsValid = true;
		return Run;
	}

	bool IsNearlySameDensity(double Left, double Right)
	{
		return FMath::IsNearlyEqual(Left, Right, FMath::Max(1.0e-6, FMath::Abs(Right) * 1.0e-6));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsKeepBuffersOnReinitTest,
	"TDGame.FluidFX.Ops.KeepBuffersOnReinit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsKeepBuffersOnReinitTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsComponentTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreBaseTuning();
	};
	RestoreBaseTuning();

	const FTDFluidKeepBuffersRun CanonicalRun = RunKeepBuffersScenario(false);
	if (!TestTrue(TEXT("The canonical-table CPU mud run completed"), CanonicalRun.bIsValid))
	{
		return false;
	}
	TestTrue(TEXT("The stamps added density before the reinit"), CanonicalRun.StampedDensity > CanonicalRun.FreshDensity);
	TestTrue(TEXT("The canonical table resets the buffers on a reinit"), IsNearlySameDensity(CanonicalRun.ReinitializedDensity, CanonicalRun.FreshDensity));

	UTDFluidPresetTuningSubsystem* TuningSubsystem = GetTuningSubsystem();
	if (!TestNotNull(TEXT("Preset tuning subsystem"), TuningSubsystem)
		|| !TestTrue(TEXT("Wrote the keep-buffers variant table"), WriteKeepBuffersTuningFile())
		|| !TestEqual(TEXT("The keep-buffers variant table loads without errors"), TuningSubsystem->ReloadPresetTuningFromPaths(GetKeepBuffersTuningPath(), FString()), 0))
	{
		return false;
	}
	TestTrue(TEXT("The variant table turns the mud keep-buffers flag on"), UTDFluidPresetAsset::BuildRuntimeOpsForPresetKind(ETDFluidPresetKind::Mud).bKeepBuffersOnReinit);

	const FTDFluidKeepBuffersRun KeptRun = RunKeepBuffersScenario(false);
	const FTDFluidKeepBuffersRun MovedRun = RunKeepBuffersScenario(true);
	if (!TestTrue(TEXT("Both variant-table CPU mud runs completed"), KeptRun.bIsValid && MovedRun.bIsValid))
	{
		return false;
	}

	TestTrue(TEXT("The kept run had stamped density"), KeptRun.StampedDensity > KeptRun.FreshDensity);
	TestTrue(TEXT("A same-preset reinit in place keeps the density"), IsNearlySameDensity(KeptRun.ReinitializedDensity, KeptRun.StampedDensity));
	TestTrue(TEXT("A reinit after moving the actor resets the buffers"), IsNearlySameDensity(MovedRun.ReinitializedDensity, MovedRun.FreshDensity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsSharedOutputClaimsTest,
	"TDGame.FluidFX.Ops.SharedOutputClaims",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsSharedOutputClaimsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsComponentTestPrivate;
	FTDFluidOpsComponentTestWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
	UTDFluidLiveComponent* FirstComponent = TestWorld.SpawnMudComponent(FVector::ZeroVector);
	UTDFluidLiveComponent* SecondComponent = TestWorld.SpawnMudComponent(OpsComponentMovedLocation);
	if (!TestNotNull(TEXT("Fluid subsystem"), FluidSubsystem) || !TestTrue(TEXT("Spawned two mud components"), FirstComponent && SecondComponent))
	{
		return false;
	}

	TStrongObjectPtr<UMaterialParameterCollection> SharedCollection(NewObject<UMaterialParameterCollection>(GetTransientPackage(), NAME_None, RF_Transient));
	AddExpectedMessage(TEXT("does not write it"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);

	TestTrue(TEXT("The first awake component claims a free output"), FluidSubsystem->ClaimSharedOutput(SharedCollection.Get(), FirstComponent));
	TestTrue(TEXT("The first claimant may claim again"), FluidSubsystem->ClaimSharedOutput(SharedCollection.Get(), FirstComponent));
	TestFalse(TEXT("A second component is refused while the claimant is awake"), FluidSubsystem->ClaimSharedOutput(SharedCollection.Get(), SecondComponent));
	TestTrue(TEXT("The refused claim leaves the first claimant"), FluidSubsystem->IsSharedOutputClaimedBy(SharedCollection.Get(), FirstComponent));

	FirstComponent->SetSimulationAwake(false);
	TestTrue(TEXT("The second component takes over from a sleeping claimant"), FluidSubsystem->ClaimSharedOutput(SharedCollection.Get(), SecondComponent));

	FirstComponent->SetSimulationAwake(true);
	TestFalse(TEXT("The woken first component does not take the output back"), FluidSubsystem->ClaimSharedOutput(SharedCollection.Get(), FirstComponent));
	TestTrue(TEXT("The second component keeps the output"), FluidSubsystem->IsSharedOutputClaimedBy(SharedCollection.Get(), SecondComponent));
	TestFalse(TEXT("The first component is no longer the claimant"), FluidSubsystem->IsSharedOutputClaimedBy(SharedCollection.Get(), FirstComponent));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsFindFluidSampleTest,
	"TDGame.FluidFX.Ops.FindFluidSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsFindFluidSampleTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsComponentTestPrivate;
	FTDFluidOpsComponentTestWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
	UTDFluidLiveComponent* Component = TestWorld.SpawnMudComponent(FVector::ZeroVector);
	if (!TestNotNull(TEXT("Fluid subsystem"), FluidSubsystem) || !TestNotNull(TEXT("Spawned a mud component"), Component))
	{
		return false;
	}
	if (!TestFalse(TEXT("Mud runs on the CPU solver"), Component->IsUsingGpuSolver()) || !TestTrue(TEXT("Mud uses a world-facing plane"), Component->GetPlaneBasis().IsWorldFacing()))
	{
		return false;
	}

	for (int32 StepIndex = 0; StepIndex < OpsComponentStampSteps; ++StepIndex)
	{
		QueueCenterStamp(*Component, FVector::ZeroVector);
		TestWorld.StepWorld();
	}

	const FVector QueryLocation(10.0, -5.0, 0.0);
	FTDFluidCellSample ComponentSample;
	FTDFluidCellSample SubsystemSample;
	TestTrue(TEXT("The component samples inside its window"), Component->SampleFluidAtWorldLocation(QueryLocation, ComponentSample));
	TestTrue(TEXT("The subsystem finds a sample inside the window"), FluidSubsystem->FindFluidSampleAtWorldLocation(QueryLocation, SubsystemSample));
	TestEqual(TEXT("The subsystem sample matches the component primary density"), SubsystemSample.PrimaryDensity, ComponentSample.PrimaryDensity);
	TestEqual(TEXT("The subsystem sample matches the component pressure"), SubsystemSample.Pressure, ComponentSample.Pressure);
	TestTrue(TEXT("The stamped location has density"), ComponentSample.PrimaryDensity > 0.0f);

	FTDFluidCellSample FarSample;
	TestFalse(TEXT("No sample outside every window"), FluidSubsystem->FindFluidSampleAtWorldLocation(OpsComponentFarLocation, FarSample));

	Component->SetSimulationAwake(false);
	FTDFluidCellSample SleepingSample;
	TestFalse(TEXT("A sleeping actor is not sampled"), FluidSubsystem->FindFluidSampleAtWorldLocation(QueryLocation, SleepingSample));
	return true;
}

#endif
