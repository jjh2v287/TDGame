#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraGPUProfilerInterface.h"
#include "NiagaraSimCache.h"
#include "NiagaraSimCacheCompare.h"
#include "NiagaraSimCacheJson.h"
#include "NiagaraSystem.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tickable.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace TDFluidNiagaraDriveTestPrivate
{
	const TCHAR* const DriveSpritePath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidDriveSprite.NS_TD_FluidDriveSprite");
	const TCHAR* const DriveSpriteLocalPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidDriveSpriteLocal.NS_TD_FluidDriveSpriteLocal");
	const TCHAR* const DriveDensityLocalPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidDriveDensityLocal.NS_TD_FluidDriveDensityLocal");
	const TCHAR* const DriveMeshFloatPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidDriveMeshFloat.NS_TD_FluidDriveMeshFloat");
	const TCHAR* const DriveMeshArrayPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidDriveMeshArray.NS_TD_FluidDriveMeshArray");
	const TCHAR* const EmberHeatPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidEmberHeat.NS_TD_FluidEmberHeat");
	const FName DriveEmitterName(TEXT("Drive"));
	const FName EmberEmitterName(TEXT("Ember"));
	const FName PositionAttributeName(TEXT("Position"));
	const FName UniqueIdAttributeName(TEXT("UniqueID"));
	const FName NormalizedAgeAttributeName(TEXT("NormalizedAge"));
	const FName ScaleAttributeName(TEXT("Scale"));
	const FName DynamicParameterAttributeName(TEXT("DynamicMaterialParameter"));
	const FVector DriveAnchorLocation(310.0, -145.0, 100.0);
	constexpr float DriveStepSeconds = 1.0f / 60.0f;
	constexpr float ActiveDurationSeconds = 600.0f;
	constexpr int32 MaxSetupStepCount = 400;
	constexpr int32 MaxActivationStepCount = 120;
	constexpr int32 LookSettleStepCount = 4;
	constexpr int32 CaptureStepCount = 120;
	constexpr int32 RepeatCaptureStepCount = 90;
	constexpr int32 CouplingTailFrameCount = 30;
	constexpr int32 RestTailStepCount = 30;
	constexpr int32 CostFrameCount = 120;
	constexpr float StampRadiusCm = 90.0f;
	constexpr float StampDensityStrength = 0.9f;
	constexpr float CouplingStampSpeedCmPerSec = 300.0f;
	constexpr float HalfStampOffsetCm = 150.0f;
	constexpr float LocalYawDegrees = 30.0f;
	constexpr double LocalCountTolerance = 0.1;
	constexpr double LocalCentroidFloorCm = 5.0;
	constexpr double LocalCentroidSigmaCount = 3.0;
	constexpr double HeightMarginCm = 1.0;
	constexpr double EmberBoxHalfExtentCm = 600.0;
	constexpr float RepeatFloatTolerance = 0.01f;
	constexpr float RepeatPositionToleranceCm = 0.5f;
	constexpr int32 FalsificationSeedOffset = 1;
	constexpr float CostSpawnRatePerSec = 600.0f;

	struct FTDDriveWorld
	{
		FTDDriveWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidNiagaraDriveWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDDriveWorld()
		{
			if (!World)
			{
				return;
			}

			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			FlushRenderingCommands();
		}

		void StepWorld(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			ENQUEUE_RENDER_COMMAND(TDFluidNiagaraDriveWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	struct FTDDriveRunSettings
	{
		const TCHAR* SystemPath = DriveSpritePath;
		FName EmitterName = DriveEmitterName;
		ETDFluidPresetKind PresetKind = ETDFluidPresetKind::Smoke;
		ETDFluidBrushChannel StampChannel = ETDFluidBrushChannel::ChannelR_Mud;
		bool bStamps = true;
		float StampDensity = StampDensityStrength;
		float StampHeat = 0.0f;
		float StampVelocityXCmPerSec = 0.0f;
		FVector StampOffset = FVector::ZeroVector;
		int32 StampStepCount = MAX_int32;
		bool bStopsSpawnWhenStampsStop = false;
		FName SpawnRateName;
		float ActorYawDegrees = 0.0f;
		bool bDisablesBuoyancy = false;
		int32 SeedOffset = 0;
		int32 StepCount = CaptureStepCount;
		TMap<FName, float> FloatOverrides;
	};

	struct FTDDriveRunResult
	{
		TStrongObjectPtr<UNiagaraSimCache> SimCache;
		int32 EmitterIndex = INDEX_NONE;
		int32 ActivationStepCount = -1;
		int32 FinalParticleCount = 0;
	};

	bool CanRunDriveTest(FAutomationTestBase& Test, const TCHAR* TestLabel)
	{
		if (!GUsingNullRHI)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI."), TestLabel));
		return false;
	}

	FTDFluidBrushStamp MakeStamp(const FTDDriveRunSettings& Settings)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = DriveAnchorLocation + Settings.StampOffset;
		Stamp.WorldVelocity = FVector(Settings.StampVelocityXCmPerSec, 0.0, 0.0);
		Stamp.RadiusCm = StampRadiusCm;
		Stamp.DensityStrength = Settings.StampDensity;
		Stamp.VelocityStrength = Settings.StampVelocityXCmPerSec != 0.0f ? 1.0f : 0.0f;
		Stamp.HeatStrength = Settings.StampHeat;
		Stamp.Channel = Settings.StampChannel;
		return Stamp;
	}

	FString MakeDumpPath(const TCHAR* TestLabel, const FString& RunLabel)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("niagara"), FString::Printf(TEXT("Drive-%s-%s.json"), TestLabel, *RunLabel));
	}

	UNiagaraComponent* WaitForOwnedComponent(FTDDriveWorld& TestWorld, const ATDFluidLiveActor& LiveActor)
	{
		UNiagaraComponent* NiagaraComp = LiveActor.LiveComponent->GetOwnedNiagaraComponent();
		for (int32 StepIndex = 0; StepIndex < MaxSetupStepCount && !NiagaraComp; ++StepIndex)
		{
			FlushAsyncLoading();
			TestWorld.StepWorld(DriveStepSeconds);
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, DriveStepSeconds);
			NiagaraComp = LiveActor.LiveComponent->GetOwnedNiagaraComponent();
		}
		return NiagaraComp;
	}

	void ApplyFloatOverrides(UNiagaraComponent& NiagaraComp, const TMap<FName, float>& Overrides)
	{
		for (const TPair<FName, float>& Override : Overrides)
		{
			NiagaraComp.SetVariableFloat(Override.Key, Override.Value);
		}
	}

	ATDFluidLiveActor* SpawnDriveActor(FAutomationTestBase& Test, FTDDriveWorld& TestWorld, const FString& RunLabel, const FTDDriveRunSettings& Settings)
	{
		UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
		if (!Test.TestNotNull(*(RunLabel + TEXT(": UTDFluidLiveSubsystem exists")), Subsystem))
		{
			return nullptr;
		}

		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, Settings.PresetKind);
		Preset->SimulationConfig.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Preset->SimulationConfig.AmbientNoiseAmount = 0.0f;
		Preset->SimulationConfig.AmbientDensity = 0.0f;
		if (Settings.bDisablesBuoyancy)
		{
			Preset->SimulationConfig.BuoyancyCmPerSec2 = 0.0f;
		}
		Preset->ExternalNiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(Settings.SystemPath));
		ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, DriveAnchorLocation, ActiveDurationSeconds);
		if (!Test.TestNotNull(*(RunLabel + TEXT(": spawned the fluid actor")), LiveActor))
		{
			return nullptr;
		}
		LiveActor->SetActorRotation(FRotator(0.0, Settings.ActorYawDegrees, 0.0));
		return LiveActor;
	}

	bool RunDriveCapture(FAutomationTestBase& Test, const TCHAR* TestLabel, const FString& RunLabel, const FTDDriveRunSettings& Settings, FTDDriveRunResult& OutResult)
	{
		FTDDriveWorld TestWorld;
		ATDFluidLiveActor* LiveActor = SpawnDriveActor(Test, TestWorld, RunLabel, Settings);
		if (!LiveActor)
		{
			return false;
		}

		UNiagaraComponent* NiagaraComp = WaitForOwnedComponent(TestWorld, *LiveActor);
		if (!NiagaraComp)
		{
			Test.AddError(FString::Printf(TEXT("%s: the owned Niagara component never appeared; %s is missing or failed to load"), *RunLabel, Settings.SystemPath));
			return false;
		}
		if (!Test.TestTrue(*(RunLabel + TEXT(": the fluid actor runs the GPU solver")), LiveActor->LiveComponent->IsUsingGpuSolver()))
		{
			return false;
		}

		for (int32 StepIndex = 0; StepIndex < LookSettleStepCount; ++StepIndex)
		{
			TestWorld.StepWorld(DriveStepSeconds);
		}
		NiagaraComp->SetRandomSeedOffset(Settings.SeedOffset);
		ApplyFloatOverrides(*NiagaraComp, Settings.FloatOverrides);
		NiagaraComp->Activate(true);
		int32 ActivationStepCount = 0;
		while (ActivationStepCount < MaxActivationStepCount && !NiagaraComp->IsActive())
		{
			ApplyFloatOverrides(*NiagaraComp, Settings.FloatOverrides);
			TestWorld.StepWorld(DriveStepSeconds);
			++ActivationStepCount;
		}
		OutResult.ActivationStepCount = ActivationStepCount;
		if (!Test.TestTrue(*(RunLabel + TEXT(": the driven system activates within 120 steps")), NiagaraComp->IsActive()))
		{
			return false;
		}

		UNiagaraSimCache* SimCache = NewObject<UNiagaraSimCache>(GetTransientPackage());
		OutResult.SimCache.Reset(SimCache);
		if (!Test.TestTrue(*(RunLabel + TEXT(": begins the sim cache write")), SimCache->BeginWrite(FNiagaraSimCacheCreateParameters(), NiagaraComp)))
		{
			return false;
		}

		TMap<FName, float> StepOverrides = Settings.FloatOverrides;
		for (int32 StepIndex = 0; StepIndex < Settings.StepCount; ++StepIndex)
		{
			const bool bStampsThisStep = Settings.bStamps && StepIndex < Settings.StampStepCount;
			if (bStampsThisStep)
			{
				LiveActor->LiveComponent->QueueBrushStamp(MakeStamp(Settings));
			}
			if (!bStampsThisStep && Settings.bStopsSpawnWhenStampsStop && !Settings.SpawnRateName.IsNone())
			{
				StepOverrides.Add(Settings.SpawnRateName, 0.0f);
			}
			ApplyFloatOverrides(*NiagaraComp, StepOverrides);
			TestWorld.StepWorld(DriveStepSeconds);
			const bool bWroteFrame = SimCache->WriteFrame(NiagaraComp);
			if (!Test.TestTrue(FString::Printf(TEXT("%s: step %d adds one cache frame"), *RunLabel, StepIndex), bWroteFrame && SimCache->GetNumFrames() == StepIndex + 1))
			{
				SimCache->EndWrite();
				return false;
			}
		}

		if (!Test.TestTrue(*(RunLabel + TEXT(": ends the sim cache write")), SimCache->EndWrite()))
		{
			return false;
		}

		OutResult.EmitterIndex = SimCache->GetEmitterIndex(Settings.EmitterName);
		if (!Test.TestTrue(*(RunLabel + TEXT(": the cache holds the driven emitter")), OutResult.EmitterIndex != INDEX_NONE))
		{
			return false;
		}

		OutResult.FinalParticleCount = SimCache->GetEmitterNumInstances(OutResult.EmitterIndex, SimCache->GetNumFrames() - 1);
		const FString DumpPath = MakeDumpPath(TestLabel, RunLabel);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(DumpPath), true);
		Test.TestTrue(*(RunLabel + TEXT(": dumps the sim cache JSON")), FNiagaraSimCacheJson::DumpToFile(*SimCache, DumpPath));
		Test.AddInfo(FString::Printf(TEXT("%s: activation steps %d, final particles %d, dump %s"), *RunLabel, OutResult.ActivationStepCount, OutResult.FinalParticleCount, *DumpPath));
		return true;
	}

	TArray<FVector> ReadFinalPositions(const FTDDriveRunResult& Result, FName EmitterName)
	{
		TArray<FVector> Positions;
		Result.SimCache->ReadPositionAttribute(Positions, PositionAttributeName, EmitterName, true, Result.SimCache->GetNumFrames() - 1);
		return Positions;
	}

	FVector ComputeCentroid(const TArray<FVector>& Positions)
	{
		FVector Sum = FVector::ZeroVector;
		for (const FVector& Position : Positions)
		{
			Sum += Position;
		}
		return Positions.IsEmpty() ? Sum : Sum / Positions.Num();
	}

	double ComputeCentroidVariance(const TArray<FVector>& Positions, const FVector& Centroid)
	{
		if (Positions.Num() < 2)
		{
			return 0.0;
		}

		double SquaredSum = 0.0;
		for (const FVector& Position : Positions)
		{
			SquaredSum += FVector::DistSquared(Position, Centroid);
		}
		return SquaredSum / (Positions.Num() - 1);
	}

	double ComputeMeanStepDisplacementX(const FTDDriveRunResult& Result, FName EmitterName, int32& OutPairCount)
	{
		const UNiagaraSimCache& SimCache = *Result.SimCache;
		const int32 LastFrame = SimCache.GetNumFrames() - 1;
		const int32 FirstFrame = FMath::Max(LastFrame - CouplingTailFrameCount, 0);
		double DisplacementSumCm = 0.0;
		OutPairCount = 0;
		for (int32 FrameIndex = FirstFrame; FrameIndex < LastFrame; ++FrameIndex)
		{
			TArray<int32> EarlierIds;
			TArray<int32> LaterIds;
			TArray<FVector> EarlierPositions;
			TArray<FVector> LaterPositions;
			SimCache.ReadIntAttribute(EarlierIds, UniqueIdAttributeName, EmitterName, FrameIndex);
			SimCache.ReadIntAttribute(LaterIds, UniqueIdAttributeName, EmitterName, FrameIndex + 1);
			SimCache.ReadPositionAttribute(EarlierPositions, PositionAttributeName, EmitterName, true, FrameIndex);
			SimCache.ReadPositionAttribute(LaterPositions, PositionAttributeName, EmitterName, true, FrameIndex + 1);
			if (EarlierIds.Num() != EarlierPositions.Num() || LaterIds.Num() != LaterPositions.Num())
			{
				continue;
			}

			TMap<int32, int32> LaterIndexById;
			for (int32 LaterIndex = 0; LaterIndex < LaterIds.Num(); ++LaterIndex)
			{
				LaterIndexById.Add(LaterIds[LaterIndex], LaterIndex);
			}

			for (int32 EarlierIndex = 0; EarlierIndex < EarlierIds.Num(); ++EarlierIndex)
			{
				if (const int32* LaterIndex = LaterIndexById.Find(EarlierIds[EarlierIndex]))
				{
					DisplacementSumCm += LaterPositions[*LaterIndex].X - EarlierPositions[EarlierIndex].X;
					++OutPairCount;
				}
			}
		}
		return OutPairCount > 0 ? DisplacementSumCm / OutPairCount : 0.0;
	}

	bool ExpectUniqueIdsCoverParticles(FAutomationTestBase& Test, const FString& RunLabel, const FTDDriveRunResult& Result, FName EmitterName)
	{
		const UNiagaraSimCache& SimCache = *Result.SimCache;
		bool bAllFramesCovered = true;
		for (int32 FrameIndex = 0; FrameIndex < SimCache.GetNumFrames(); ++FrameIndex)
		{
			TArray<int32> UniqueIds;
			SimCache.ReadIntAttribute(UniqueIds, UniqueIdAttributeName, EmitterName, FrameIndex);
			const int32 ParticleCount = SimCache.GetEmitterNumInstances(Result.EmitterIndex, FrameIndex);
			TSet<int32> DistinctIds;
			DistinctIds.Append(UniqueIds);
			bAllFramesCovered &= UniqueIds.Num() == ParticleCount && DistinctIds.Num() == ParticleCount;
		}
		return Test.TestTrue(*(RunLabel + TEXT(": every frame has one distinct UniqueID per particle")), bAllFramesCovered);
	}

	FTDDriveRunSettings MakeEmberSettings()
	{
		FTDDriveRunSettings Settings;
		Settings.SystemPath = EmberHeatPath;
		Settings.EmitterName = EmberEmitterName;
		Settings.PresetKind = ETDFluidPresetKind::Explosion;
		Settings.StampChannel = ETDFluidBrushChannel::ChannelB_Fire;
		Settings.StampHeat = 1.0f;
		Settings.SpawnRateName = FName(TEXT("User.EmberSpawnPerHeatM2PerSec"));
		return Settings;
	}

	bool WriteCostRecord(FAutomationTestBase& Test, const FString& SystemLabel, const TArray<double>& FrameMicroseconds, const TMap<FName, float>& FloatOverrides)
	{
		TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> FrameValues;
		double Sum = 0.0;
		bool bAllFinite = true;
		for (const double Microseconds : FrameMicroseconds)
		{
			FrameValues.Add(MakeShared<FJsonValueNumber>(Microseconds));
			Sum += Microseconds;
			bAllFinite &= FMath::IsFinite(Microseconds) && Microseconds >= 0.0;
		}
		Record->SetStringField(TEXT("system"), SystemLabel);
		Record->SetNumberField(TEXT("frames"), FrameMicroseconds.Num());
		Record->SetNumberField(TEXT("mean_dispatch_microseconds"), FrameMicroseconds.IsEmpty() ? 0.0 : Sum / FrameMicroseconds.Num());
		TSharedRef<FJsonObject> OverrideObject = MakeShared<FJsonObject>();
		for (const TPair<FName, float>& Override : FloatOverrides)
		{
			OverrideObject->SetNumberField(Override.Key.ToString(), Override.Value);
		}
		Record->SetObjectField(TEXT("float_overrides"), OverrideObject);
		Record->SetArrayField(TEXT("dispatch_microseconds_per_frame"), FrameValues);
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Record, Writer);
		const FString RecordPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("niagara"), FString::Printf(TEXT("cost-%s.json"), *SystemLabel));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(RecordPath), true);
		Test.TestTrue(*(SystemLabel + TEXT(": writes the cost record")), FFileHelper::SaveStringToFile(Text, *RecordPath));
		Test.AddInfo(FString::Printf(TEXT("%s: %d profiled frames, mean %.2f us, record %s"), *SystemLabel, FrameMicroseconds.Num(), FrameMicroseconds.IsEmpty() ? 0.0 : Sum / FrameMicroseconds.Num(), *RecordPath));
		return Test.TestTrue(*(SystemLabel + TEXT(": profiled frames exist and every value is finite")), !FrameMicroseconds.IsEmpty() && bAllFinite);
	}

#if WITH_NIAGARA_GPU_PROFILER
	bool RecordDispatchCost(FAutomationTestBase& Test, const FString& SystemLabel, const FTDDriveRunSettings& Settings)
	{
		FTDDriveWorld TestWorld;
		ATDFluidLiveActor* LiveActor = SpawnDriveActor(Test, TestWorld, SystemLabel, Settings);
		UNiagaraComponent* NiagaraComp = LiveActor ? WaitForOwnedComponent(TestWorld, *LiveActor) : nullptr;
		if (!NiagaraComp)
		{
			Test.AddError(FString::Printf(TEXT("%s: the owned Niagara component never appeared; %s is missing or failed to load"), *SystemLabel, Settings.SystemPath));
			return false;
		}

		for (int32 StepIndex = 0; StepIndex < LookSettleStepCount; ++StepIndex)
		{
			TestWorld.StepWorld(DriveStepSeconds);
		}
		ApplyFloatOverrides(*NiagaraComp, Settings.FloatOverrides);
		NiagaraComp->Activate(true);

		TArray<double> FrameMicroseconds;
		TWeakObjectPtr<UNiagaraComponent> WeakComponent(NiagaraComp);
		FNiagaraGpuProfilerListener Listener;
		Listener.SetHandler([&FrameMicroseconds, WeakComponent](const FNiagaraGpuFrameResultsPtr& Results)
		{
			if (!Results.IsValid())
			{
				return;
			}

			double ComponentMicroseconds = 0.0;
			for (const FNiagaraGpuFrameResults::FDispatchResults& Dispatch : Results->DispatchResults)
			{
				if (Dispatch.OwnerComponent.Get() == WeakComponent.Get())
				{
					ComponentMicroseconds += static_cast<double>(Dispatch.DurationMicroseconds);
				}
			}
			FrameMicroseconds.Add(ComponentMicroseconds);
		});
		Listener.SetEnabled(true);

		for (int32 StepIndex = 0; StepIndex < Settings.StepCount + CostFrameCount; ++StepIndex)
		{
			LiveActor->LiveComponent->QueueBrushStamp(MakeStamp(Settings));
			ApplyFloatOverrides(*NiagaraComp, Settings.FloatOverrides);
			TestWorld.StepWorld(DriveStepSeconds);
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, DriveStepSeconds);
			if (StepIndex < Settings.StepCount)
			{
				FrameMicroseconds.Reset();
			}
		}
		Listener.SetEnabled(false);
		return WriteCostRecord(Test, SystemLabel, FrameMicroseconds, Settings.FloatOverrides);
	}
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraDriveSpawnFollowsDensityTest,
	"TDGame.FluidFXRhi.Niagara.Drive.SpawnFollowsDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraDriveSpawnFollowsDensityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraDriveTestPrivate;
	const TCHAR* const TestLabel = TEXT("SpawnFollowsDensity");
	if (!CanRunDriveTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.Drive.SpawnFollowsDensity")))
	{
		return false;
	}

	FTDDriveRunSettings StampedSettings;
	StampedSettings.SystemPath = DriveDensityLocalPath;
	FTDDriveRunResult StampedRun;
	if (!RunDriveCapture(*this, TestLabel, TEXT("stamped"), StampedSettings, StampedRun))
	{
		return false;
	}
	TestTrue(TEXT("Density stamps spawn driven particles"), StampedRun.FinalParticleCount > 0);

	FTDDriveRunSettings UnstampedSettings = StampedSettings;
	UnstampedSettings.bStamps = false;
	FTDDriveRunResult UnstampedRun;
	if (!RunDriveCapture(*this, TestLabel, TEXT("unstamped"), UnstampedSettings, UnstampedRun))
	{
		return false;
	}
	TestEqual(TEXT("Without density the rejection leaves no particles"), UnstampedRun.FinalParticleCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraDriveVelocityCouplingTest,
	"TDGame.FluidFXRhi.Niagara.Drive.VelocityCouplingSign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraDriveVelocityCouplingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraDriveTestPrivate;
	const TCHAR* const TestLabel = TEXT("VelocityCouplingSign");
	if (!CanRunDriveTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.Drive.VelocityCouplingSign")))
	{
		return false;
	}

	FTDDriveRunSettings PositiveSettings;
	PositiveSettings.StampVelocityXCmPerSec = CouplingStampSpeedCmPerSec;
	FTDDriveRunSettings NegativeSettings = PositiveSettings;
	NegativeSettings.StampVelocityXCmPerSec = -CouplingStampSpeedCmPerSec;
	FTDDriveRunResult PositiveRun;
	FTDDriveRunResult NegativeRun;
	if (!RunDriveCapture(*this, TestLabel, TEXT("plus-x"), PositiveSettings, PositiveRun)
		|| !RunDriveCapture(*this, TestLabel, TEXT("minus-x"), NegativeSettings, NegativeRun))
	{
		return false;
	}

	int32 PositivePairCount = 0;
	int32 NegativePairCount = 0;
	const double PositiveDisplacementCm = ComputeMeanStepDisplacementX(PositiveRun, DriveEmitterName, PositivePairCount);
	const double NegativeDisplacementCm = ComputeMeanStepDisplacementX(NegativeRun, DriveEmitterName, NegativePairCount);
	const double StampStepCm = CouplingStampSpeedCmPerSec * DriveStepSeconds;
	AddInfo(FString::Printf(TEXT("C12-v drive coupling: mean X step %.4f cm (+X, %d pairs), %.4f cm (-X, %d pairs), stamp step %.4f cm, ratios %.3f and %.3f"),
		PositiveDisplacementCm, PositivePairCount, NegativeDisplacementCm, NegativePairCount, StampStepCm, PositiveDisplacementCm / StampStepCm, -NegativeDisplacementCm / StampStepCm));
	TestTrue(TEXT("Both coupling runs match particles by UniqueID across frames"), PositivePairCount > 0 && NegativePairCount > 0);
	TestTrue(TEXT("Particles drift +X when the fluid is pushed +X"), PositiveDisplacementCm > 0.0);
	TestTrue(TEXT("Particles drift -X when the fluid is pushed -X"), NegativeDisplacementCm < 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraDriveLocalMatchesWorldTest,
	"TDGame.FluidFXRhi.Niagara.Drive.LocalMatchesWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraDriveLocalMatchesWorldTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraDriveTestPrivate;
	const TCHAR* const TestLabel = TEXT("LocalMatchesWorld");
	if (!CanRunDriveTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.Drive.LocalMatchesWorld")))
	{
		return false;
	}

	FTDDriveRunSettings WorldSettings;
	WorldSettings.ActorYawDegrees = LocalYawDegrees;
	FTDDriveRunSettings LocalSettings = WorldSettings;
	LocalSettings.SystemPath = DriveSpriteLocalPath;
	FTDDriveRunResult WorldRun;
	FTDDriveRunResult LocalRun;
	if (!RunDriveCapture(*this, TestLabel, TEXT("world"), WorldSettings, WorldRun)
		|| !RunDriveCapture(*this, TestLabel, TEXT("local"), LocalSettings, LocalRun))
	{
		return false;
	}

	const TArray<FVector> WorldPositions = ReadFinalPositions(WorldRun, DriveEmitterName);
	const TArray<FVector> LocalPositions = ReadFinalPositions(LocalRun, DriveEmitterName);
	if (!TestTrue(TEXT("Both runs leave particles"), !WorldPositions.IsEmpty() && !LocalPositions.IsEmpty()))
	{
		return false;
	}

	const double CountRatio = static_cast<double>(LocalPositions.Num()) / WorldPositions.Num();
	const FVector WorldCentroid = ComputeCentroid(WorldPositions);
	const FVector LocalCentroid = ComputeCentroid(LocalPositions);
	const double StandardError = FMath::Sqrt(ComputeCentroidVariance(WorldPositions, WorldCentroid) / WorldPositions.Num() + ComputeCentroidVariance(LocalPositions, LocalCentroid) / LocalPositions.Num());
	const double CentroidToleranceCm = FMath::Max(LocalCentroidFloorCm, LocalCentroidSigmaCount * StandardError);
	const double CentroidDistanceCm = FVector::Dist(WorldCentroid, LocalCentroid);
	AddInfo(FString::Printf(TEXT("Local vs world at yaw %.0f: counts %d and %d, centroid distance %.2f cm, tolerance %.2f cm"), LocalYawDegrees, LocalPositions.Num(), WorldPositions.Num(), CentroidDistanceCm, CentroidToleranceCm));
	TestTrue(TEXT("Local and world particle counts agree within 10 percent"), FMath::Abs(CountRatio - 1.0) <= LocalCountTolerance);
	TestTrue(TEXT("Local and world centroids agree within the larger of 5 cm and three standard errors"), CentroidDistanceCm <= CentroidToleranceCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraDriveHeightFollowsDensityTest,
	"TDGame.FluidFXRhi.Niagara.Drive.HeightFollowsDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraDriveHeightFollowsDensityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraDriveTestPrivate;
	const TCHAR* const TestLabel = TEXT("HeightFollowsDensity");
	if (!CanRunDriveTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.Drive.HeightFollowsDensity")))
	{
		return false;
	}

	FTDDriveRunSettings Settings;
	Settings.SystemPath = DriveMeshArrayPath;
	Settings.StampOffset = FVector(HalfStampOffsetCm, 0.0, 0.0);
	FTDDriveRunResult Run;
	if (!RunDriveCapture(*this, TestLabel, TEXT("plus-x-half"), Settings, Run))
	{
		return false;
	}

	double PlusHeightSum = 0.0;
	double MinusHeightSum = 0.0;
	int32 PlusCount = 0;
	int32 MinusCount = 0;
	for (const FVector& Position : ReadFinalPositions(Run, DriveEmitterName))
	{
		const bool bIsPlusSide = Position.X > DriveAnchorLocation.X;
		(bIsPlusSide ? PlusHeightSum : MinusHeightSum) += Position.Z;
		++(bIsPlusSide ? PlusCount : MinusCount);
	}
	if (!TestTrue(TEXT("The grid covers both halves"), PlusCount > 0 && MinusCount > 0))
	{
		return false;
	}

	const double PlusMeanZ = PlusHeightSum / PlusCount;
	const double MinusMeanZ = MinusHeightSum / MinusCount;
	AddInfo(FString::Printf(TEXT("Mesh array height: +X half %.2f cm over %d, -X half %.2f cm over %d"), PlusMeanZ, PlusCount, MinusMeanZ, MinusCount));
	TestTrue(TEXT("The dense half rises at least 1 cm above the empty half"), PlusMeanZ >= MinusMeanZ + HeightMarginCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraDriveRepeatRunTest,
	"TDGame.FluidFXRhi.Niagara.Drive.RepeatRunMatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraDriveRepeatRunTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraDriveTestPrivate;
	const TCHAR* const TestLabel = TEXT("RepeatRunMatches");
	if (!CanRunDriveTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.Drive.RepeatRunMatches")))
	{
		return false;
	}

	FTDDriveRunSettings RepeatSettings;
	RepeatSettings.StepCount = RepeatCaptureStepCount;
	FTDDriveRunSettings ShiftedSeedSettings = RepeatSettings;
	ShiftedSeedSettings.SeedOffset = FalsificationSeedOffset;
	FTDDriveRunResult FirstRun;
	FTDDriveRunResult SecondRun;
	FTDDriveRunResult ShiftedSeedRun;
	if (!RunDriveCapture(*this, TestLabel, TEXT("run-a"), RepeatSettings, FirstRun)
		|| !RunDriveCapture(*this, TestLabel, TEXT("run-b"), RepeatSettings, SecondRun)
		|| !RunDriveCapture(*this, TestLabel, TEXT("run-c-seed"), ShiftedSeedSettings, ShiftedSeedRun))
	{
		return false;
	}

	if (!ExpectUniqueIdsCoverParticles(*this, TEXT("run-a"), FirstRun, DriveEmitterName)
		|| !ExpectUniqueIdsCoverParticles(*this, TEXT("run-b"), SecondRun, DriveEmitterName)
		|| !ExpectUniqueIdsCoverParticles(*this, TEXT("run-c-seed"), ShiftedSeedRun, DriveEmitterName))
	{
		return false;
	}

	FNiagaraSimCacheCompare RepeatCompare;
	RepeatCompare.DefaultFloatTolerance = RepeatFloatTolerance;
	RepeatCompare.bIncludeDataInterfaces = false;
	RepeatCompare.SetToleranceByName(*FirstRun.SimCache, PositionAttributeName.ToString(), RepeatPositionToleranceCm);
	FString RepeatDifferences;
	const bool bRepeatMatches = RepeatCompare.Compare(*FirstRun.SimCache, *SecondRun.SimCache, RepeatDifferences);
	if (!bRepeatMatches)
	{
		AddInfo(TEXT("Drive repeat differences: ") + RepeatDifferences);
	}
	TestTrue(TEXT("Run B matches run A within tolerance"), bRepeatMatches);

	FNiagaraSimCacheCompare SeedCompare;
	SeedCompare.DefaultFloatTolerance = RepeatFloatTolerance;
	SeedCompare.bIncludeDataInterfaces = false;
	SeedCompare.SetToleranceByName(*FirstRun.SimCache, PositionAttributeName.ToString(), RepeatPositionToleranceCm);
	FString SeedDifferences;
	TestFalse(TEXT("Changing only the seed offset makes the caches differ, so the comparison is not vacuous"), SeedCompare.Compare(*FirstRun.SimCache, *ShiftedSeedRun.SimCache, SeedDifferences));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraEmberHeatSpawnTest,
	"TDGame.FluidFXRhi.Niagara.EmberHeat.SpawnsFromHeatOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraEmberHeatSpawnTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraDriveTestPrivate;
	const TCHAR* const TestLabel = TEXT("SpawnsFromHeatOnly");
	if (!CanRunDriveTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.EmberHeat.SpawnsFromHeatOnly")))
	{
		return false;
	}

	const FName DensityWeightName(TEXT("User.EmberDensitySpawnWeight"));
	FTDDriveRunSettings HeatSettings = MakeEmberSettings();
	FTDDriveRunSettings NoHeatSettings = HeatSettings;
	NoHeatSettings.StampHeat = 0.0f;
	NoHeatSettings.FloatOverrides.Add(DensityWeightName, 0.0f);
	FTDDriveRunSettings DensityFallbackSettings = NoHeatSettings;
	DensityFallbackSettings.FloatOverrides.Add(DensityWeightName, 1.0f);
	FTDDriveRunResult HeatRun;
	FTDDriveRunResult NoHeatRun;
	FTDDriveRunResult DensityFallbackRun;
	if (!RunDriveCapture(*this, TestLabel, TEXT("heat"), HeatSettings, HeatRun)
		|| !RunDriveCapture(*this, TestLabel, TEXT("no-heat"), NoHeatSettings, NoHeatRun)
		|| !RunDriveCapture(*this, TestLabel, TEXT("density-fallback"), DensityFallbackSettings, DensityFallbackRun))
	{
		return false;
	}

	TestTrue(TEXT("Heat stamps spawn embers"), HeatRun.FinalParticleCount > 0);
	TestEqual(TEXT("Without heat and with no density fallback weight there are no embers"), NoHeatRun.FinalParticleCount, 0);
	TestTrue(TEXT("The density fallback weight spawns embers without heat"), DensityFallbackRun.FinalParticleCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraEmberHeatAttributeRangesTest,
	"TDGame.FluidFXRhi.Niagara.EmberHeat.AttributeRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraEmberHeatAttributeRangesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraDriveTestPrivate;
	const TCHAR* const TestLabel = TEXT("EmberHeatAttributeRanges");
	if (!CanRunDriveTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.EmberHeat.AttributeRanges")))
	{
		return false;
	}

	FTDDriveRunSettings Settings = MakeEmberSettings();
	Settings.StampVelocityXCmPerSec = CouplingStampSpeedCmPerSec;
	FTDDriveRunResult Run;
	if (!RunDriveCapture(*this, TestLabel, TEXT("plus-x"), Settings, Run))
	{
		return false;
	}

	const UNiagaraSimCache& SimCache = *Run.SimCache;
	const int32 LastFrame = SimCache.GetNumFrames() - 1;
	TArray<FVector> Positions;
	TArray<float> NormalizedAges;
	TArray<FVector> Scales;
	TArray<FVector4> DynamicParameters;
	SimCache.ReadPositionAttribute(Positions, PositionAttributeName, EmberEmitterName, true, LastFrame);
	SimCache.ReadFloatAttribute(NormalizedAges, NormalizedAgeAttributeName, EmberEmitterName, LastFrame);
	SimCache.ReadVectorAttribute(Scales, ScaleAttributeName, EmberEmitterName, LastFrame);
	SimCache.ReadVector4Attribute(DynamicParameters, DynamicParameterAttributeName, EmberEmitterName, LastFrame);
	const int32 ParticleCount = Run.FinalParticleCount;
	const bool bHasAllAttributes = Positions.Num() == ParticleCount && NormalizedAges.Num() == ParticleCount && Scales.Num() == ParticleCount && DynamicParameters.Num() == ParticleCount;
	if (!TestTrue(TEXT("Position, NormalizedAge, Scale and DynamicMaterialParameter cover every ember"), ParticleCount > 0 && bHasAllAttributes))
	{
		return false;
	}

	bool bPositionsInBox = true;
	bool bAgesInRange = true;
	bool bScalesPositive = true;
	bool bDynamicParametersInRange = true;
	for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
	{
		const FVector Offset = Positions[ParticleIndex] - DriveAnchorLocation;
		bPositionsInBox &= !Offset.ContainsNaN() && FMath::Abs(Offset.X) <= EmberBoxHalfExtentCm && FMath::Abs(Offset.Y) <= EmberBoxHalfExtentCm && FMath::Abs(Offset.Z) <= EmberBoxHalfExtentCm;
		bAgesInRange &= NormalizedAges[ParticleIndex] >= 0.0f && NormalizedAges[ParticleIndex] <= 1.0f;
		bScalesPositive &= Scales[ParticleIndex].X > 0.0 && Scales[ParticleIndex].Y > 0.0 && Scales[ParticleIndex].Z > 0.0;
		bDynamicParametersInRange &= DynamicParameters[ParticleIndex].X >= 0.0 && DynamicParameters[ParticleIndex].X <= 1.0 && DynamicParameters[ParticleIndex].Y >= 0.0 && DynamicParameters[ParticleIndex].Y <= 1.0;
	}
	TestTrue(TEXT("Positions are finite and inside the 600 cm fixed bounds"), bPositionsInBox);
	TestTrue(TEXT("NormalizedAge stays in [0, 1]"), bAgesInRange);
	TestTrue(TEXT("Scale is positive"), bScalesPositive);
	TestTrue(TEXT("Heat and fade dynamic parameters stay in [0, 1]"), bDynamicParametersInRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraEmberHeatRestTest,
	"TDGame.FluidFXRhi.Niagara.EmberHeat.RestKeepsSlowEmbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraEmberHeatRestTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraDriveTestPrivate;
	const TCHAR* const TestLabel = TEXT("RestKeepsSlowEmbers");
	if (!CanRunDriveTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.EmberHeat.RestKeepsSlowEmbers")))
	{
		return false;
	}

	const FName RestName(TEXT("User.EmberGroundRestSeconds"));
	FTDDriveRunSettings NoRestSettings = MakeEmberSettings();
	NoRestSettings.bDisablesBuoyancy = true;
	NoRestSettings.bStopsSpawnWhenStampsStop = true;
	NoRestSettings.StampStepCount = CaptureStepCount;
	NoRestSettings.StepCount = CaptureStepCount + RestTailStepCount;
	NoRestSettings.FloatOverrides.Add(FName(TEXT("User.EmberBuoyancyCmPerSec2")), 0.0f);
	NoRestSettings.FloatOverrides.Add(FName(TEXT("User.EmberGravityCmPerSec2")), 0.0f);
	NoRestSettings.FloatOverrides.Add(FName(TEXT("User.EmberDragPerSec")), 50.0f);
	NoRestSettings.FloatOverrides.Add(RestName, 0.0f);
	FTDDriveRunSettings RestSettings = NoRestSettings;
	RestSettings.FloatOverrides.Add(RestName, 1.0f);
	FTDDriveRunResult NoRestRun;
	FTDDriveRunResult RestRun;
	if (!RunDriveCapture(*this, TestLabel, TEXT("rest-0"), NoRestSettings, NoRestRun)
		|| !RunDriveCapture(*this, TestLabel, TEXT("rest-1"), RestSettings, RestRun))
	{
		return false;
	}

	TestEqual(TEXT("Without ground rest every slow ember dies within 0.5 s after spawning stops"), NoRestRun.FinalParticleCount, 0);
	TestTrue(TEXT("A one second ground rest keeps slow embers alive 0.5 s after spawning stops"), RestRun.FinalParticleCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraDriveOffscreenCostTest,
	"TDGame.FluidFXRhi.Niagara.Drive.OffscreenCostRecord",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraDriveOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraDriveTestPrivate;
	if (!CanRunDriveTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.Drive.OffscreenCostRecord")))
	{
		return false;
	}

#if WITH_NIAGARA_GPU_PROFILER
	FTDDriveRunSettings MeshSettings;
	MeshSettings.SystemPath = DriveMeshFloatPath;
	MeshSettings.FloatOverrides.Add(FName(TEXT("User.DriveSpawnRatePerSec")), CostSpawnRatePerSec);
	MeshSettings.FloatOverrides.Add(FName(TEXT("User.DriveSpawnPerM2PerSec")), 0.0f);
	MeshSettings.FloatOverrides.Add(FName(TEXT("User.DriveSlowKillSeconds")), 0.0f);
	const bool bMeshRecorded = RecordDispatchCost(*this, TEXT("NS_TD_FluidDriveMeshFloat"), MeshSettings);

	FTDDriveRunSettings EmberSettings = MakeEmberSettings();
	const bool bEmberRecorded = RecordDispatchCost(*this, TEXT("NS_TD_FluidEmberHeat"), EmberSettings);
	return bMeshRecorded && bEmberRecorded;
#else
	AddError(TEXT("WITH_NIAGARA_GPU_PROFILER is off in this build, so the dispatch cost cannot be recorded."));
	return false;
#endif
}

#endif
