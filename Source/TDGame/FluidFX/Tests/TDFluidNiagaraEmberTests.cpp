#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NiagaraComponent.h"
#include "NiagaraSimCache.h"
#include "NiagaraSimCacheCompare.h"
#include "NiagaraSimCacheJson.h"
#include "NiagaraSystem.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Tickable.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

namespace TDFluidNiagaraEmberTestPrivate
{
	const TCHAR* const EmberSystemPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidEmber.NS_TD_FluidEmber");
	const FName EmberEmitterName(TEXT("Ember"));
	const FName PositionAttributeName(TEXT("Position"));
	const FName UniqueIdAttributeName(TEXT("UniqueID"));
	const FName NormalizedAgeAttributeName(TEXT("NormalizedAge"));
	const FName ScaleAttributeName(TEXT("Scale"));
	const FName DynamicParameterAttributeName(TEXT("DynamicMaterialParameter"));
	const FVector EmberAnchorLocation(310.0, -145.0, 100.0);
	constexpr float EmberStepSeconds = 1.0f / 60.0f;
	constexpr float ActiveDurationSeconds = 600.0f;
	constexpr int32 MaxSetupStepCount = 400;
	constexpr int32 MaxActivationStepCount = 120;
	constexpr int32 CaptureStepCount = 120;
	constexpr int32 RepeatCaptureStepCount = 90;
	constexpr int32 CouplingTailFrameCount = 30;
	constexpr float FireStampRadiusCm = 90.0f;
	constexpr float FireStampDensityStrength = 0.9f;
	constexpr float TestEmberSpawnRatePerSec = 1000.0f;
	constexpr float CouplingStampSpeedCmPerSec = 300.0f;
	constexpr double ParticleBoxHalfExtentCm = 600.0;
	constexpr float RepeatFloatTolerance = 0.01f;
	constexpr float RepeatPositionToleranceCm = 0.5f;
	constexpr int32 FalsificationSeedOffset = 1;

	struct FTDNiagaraEmberWorld
	{
		FTDNiagaraEmberWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidNiagaraEmberWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDNiagaraEmberWorld()
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
			ENQUEUE_RENDER_COMMAND(TDFluidNiagaraEmberWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	struct FTDEmberRunSettings
	{
		bool bStampsFire = true;
		float StampVelocityXCmPerSec = 0.0f;
		int32 SeedOffset = 0;
		int32 StepCount = CaptureStepCount;
	};

	struct FTDEmberRunResult
	{
		TStrongObjectPtr<UNiagaraSimCache> SimCache;
		TArray<FString> VelocityHashes;
		int32 EmitterIndex = INDEX_NONE;
		int32 ActivationStepCount = -1;
		int32 FinalParticleCount = 0;
	};

	bool CanRunEmberTest(FAutomationTestBase& Test, const TCHAR* TestLabel)
	{
		if (!GUsingNullRHI)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI."), TestLabel));
		return false;
	}

	FTDFluidBrushStamp MakeFireStamp(float VelocityXCmPerSec)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = EmberAnchorLocation;
		Stamp.WorldVelocity = FVector(VelocityXCmPerSec, 0.0, 0.0);
		Stamp.RadiusCm = FireStampRadiusCm;
		Stamp.DensityStrength = FireStampDensityStrength;
		Stamp.VelocityStrength = VelocityXCmPerSec != 0.0f ? 1.0f : 0.0f;
		Stamp.HeatStrength = 1.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		return Stamp;
	}

	FString HashRenderTarget(UTexture* Texture)
	{
		UTextureRenderTarget2D* Target = Cast<UTextureRenderTarget2D>(Texture);
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		TArray<FFloat16Color> Pixels;
		if (!Resource || !static_cast<FRenderTarget*>(Resource)->ReadFloat16Pixels(Pixels))
		{
			return FString();
		}

		FSHAHash Hash;
		FSHA1::HashBuffer(Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color), Hash.Hash);
		return Hash.ToString();
	}

	FString MakeDumpPath(const TCHAR* TestLabel, const FString& RunLabel)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("niagara"), FString::Printf(TEXT("%s-%s.json"), TestLabel, *RunLabel));
	}

	UNiagaraComponent* WaitForEmberComponent(FTDNiagaraEmberWorld& TestWorld, const ATDFluidLiveActor& LiveActor)
	{
		UNiagaraComponent* NiagaraComp = LiveActor.LiveComponent->GetOwnedNiagaraComponent();
		for (int32 StepIndex = 0; StepIndex < MaxSetupStepCount && !NiagaraComp; ++StepIndex)
		{
			FlushAsyncLoading();
			TestWorld.StepWorld(EmberStepSeconds);
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, EmberStepSeconds);
			NiagaraComp = LiveActor.LiveComponent->GetOwnedNiagaraComponent();
		}
		return NiagaraComp;
	}

	bool RunEmberCapture(FAutomationTestBase& Test, const TCHAR* TestLabel, const FString& RunLabel, const FTDEmberRunSettings& Settings, FTDEmberRunResult& OutResult)
	{
		FTDNiagaraEmberWorld TestWorld;
		UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
		if (!Test.TestNotNull(*(RunLabel + TEXT(": UTDFluidLiveSubsystem exists")), Subsystem))
		{
			return false;
		}

		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Explosion);
		Preset->SimulationConfig.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Preset->SimulationConfig.AmbientNoiseAmount = 0.0f;
		Preset->ExternalNiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(EmberSystemPath));
		ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, EmberAnchorLocation, ActiveDurationSeconds);
		if (!Test.TestNotNull(*(RunLabel + TEXT(": spawned the fluid actor")), LiveActor))
		{
			return false;
		}

		UNiagaraComponent* NiagaraComp = WaitForEmberComponent(TestWorld, *LiveActor);
		if (!NiagaraComp)
		{
			Test.AddError(FString::Printf(TEXT("%s: the owned Niagara component never appeared; %s is missing or failed to load"), *RunLabel, EmberSystemPath));
			return false;
		}
		if (!Test.TestTrue(*(RunLabel + TEXT(": the fluid actor runs the GPU solver")), LiveActor->LiveComponent->IsUsingGpuSolver()))
		{
			return false;
		}

		NiagaraComp->SetRandomSeedOffset(Settings.SeedOffset);
		NiagaraComp->SetVariableFloat(FName(TEXT("User.EmberSpawnRate")), TestEmberSpawnRatePerSec);
		NiagaraComp->Activate(true);
		int32 ActivationStepCount = 0;
		while (ActivationStepCount < MaxActivationStepCount && !NiagaraComp->IsActive())
		{
			TestWorld.StepWorld(EmberStepSeconds);
			++ActivationStepCount;
		}
		OutResult.ActivationStepCount = ActivationStepCount;
		if (!Test.TestTrue(*(RunLabel + TEXT(": the ember system activates within 120 steps")), NiagaraComp->IsActive()))
		{
			return false;
		}

		UNiagaraSimCache* SimCache = NewObject<UNiagaraSimCache>(GetTransientPackage());
		OutResult.SimCache.Reset(SimCache);
		if (!Test.TestTrue(*(RunLabel + TEXT(": begins the sim cache write")), SimCache->BeginWrite(FNiagaraSimCacheCreateParameters(), NiagaraComp)))
		{
			return false;
		}

		for (int32 StepIndex = 0; StepIndex < Settings.StepCount; ++StepIndex)
		{
			if (Settings.bStampsFire)
			{
				LiveActor->LiveComponent->QueueBrushStamp(MakeFireStamp(Settings.StampVelocityXCmPerSec));
			}
			TestWorld.StepWorld(EmberStepSeconds);
			OutResult.VelocityHashes.Add(HashRenderTarget(LiveActor->LiveComponent->GetVelocityTexture()));
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

		OutResult.EmitterIndex = SimCache->GetEmitterIndex(EmberEmitterName);
		if (!Test.TestTrue(*(RunLabel + TEXT(": the cache holds the Ember emitter")), OutResult.EmitterIndex != INDEX_NONE))
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

	bool ExpectUniqueIdsCoverParticles(FAutomationTestBase& Test, const FString& RunLabel, const FTDEmberRunResult& Result)
	{
		const UNiagaraSimCache& SimCache = *Result.SimCache;
		bool bAllFramesCovered = true;
		for (int32 FrameIndex = 0; FrameIndex < SimCache.GetNumFrames(); ++FrameIndex)
		{
			TArray<int32> UniqueIds;
			SimCache.ReadIntAttribute(UniqueIds, UniqueIdAttributeName, EmberEmitterName, FrameIndex);
			const int32 ParticleCount = SimCache.GetEmitterNumInstances(Result.EmitterIndex, FrameIndex);
			TSet<int32> DistinctIds;
			DistinctIds.Append(UniqueIds);
			bAllFramesCovered &= UniqueIds.Num() == ParticleCount && DistinctIds.Num() == ParticleCount;
		}
		return Test.TestTrue(*(RunLabel + TEXT(": every frame has one distinct UniqueID per particle")), bAllFramesCovered);
	}

	bool HasEveryVelocityHash(const FTDEmberRunResult& Result)
	{
		return !Result.VelocityHashes.IsEmpty() && !Result.VelocityHashes.ContainsByPredicate([](const FString& Hash) { return Hash.IsEmpty(); });
	}

	double ComputeMeanStepDisplacementX(const FTDEmberRunResult& Result, int32& OutPairCount)
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
			SimCache.ReadIntAttribute(EarlierIds, UniqueIdAttributeName, EmberEmitterName, FrameIndex);
			SimCache.ReadIntAttribute(LaterIds, UniqueIdAttributeName, EmberEmitterName, FrameIndex + 1);
			SimCache.ReadPositionAttribute(EarlierPositions, PositionAttributeName, EmberEmitterName, true, FrameIndex);
			SimCache.ReadPositionAttribute(LaterPositions, PositionAttributeName, EmberEmitterName, true, FrameIndex + 1);
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

	void ExpectFinalAttributeRanges(FAutomationTestBase& Test, const FString& RunLabel, const FTDEmberRunResult& Result)
	{
		const UNiagaraSimCache& SimCache = *Result.SimCache;
		const int32 LastFrame = SimCache.GetNumFrames() - 1;
		const int32 ParticleCount = Result.FinalParticleCount;
		TArray<FVector> Positions;
		TArray<float> NormalizedAges;
		TArray<FVector> Scales;
		TArray<FVector4> DynamicParameters;
		SimCache.ReadPositionAttribute(Positions, PositionAttributeName, EmberEmitterName, true, LastFrame);
		SimCache.ReadFloatAttribute(NormalizedAges, NormalizedAgeAttributeName, EmberEmitterName, LastFrame);
		SimCache.ReadVectorAttribute(Scales, ScaleAttributeName, EmberEmitterName, LastFrame);
		SimCache.ReadVector4Attribute(DynamicParameters, DynamicParameterAttributeName, EmberEmitterName, LastFrame);
		const bool bHasAllAttributes = Positions.Num() == ParticleCount && NormalizedAges.Num() == ParticleCount && Scales.Num() == ParticleCount && DynamicParameters.Num() == ParticleCount;
		if (!Test.TestTrue(*(RunLabel + TEXT(": Position, NormalizedAge, Scale and DynamicMaterialParameter cover every particle")), ParticleCount > 0 && bHasAllAttributes))
		{
			return;
		}

		bool bPositionsInBox = true;
		bool bAgesInRange = true;
		bool bScalesPositive = true;
		bool bDynamicParametersInRange = true;
		for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
		{
			const FVector Offset = Positions[ParticleIndex] - EmberAnchorLocation;
			bPositionsInBox &= !Offset.ContainsNaN() && FMath::Abs(Offset.X) <= ParticleBoxHalfExtentCm && FMath::Abs(Offset.Y) <= ParticleBoxHalfExtentCm && FMath::Abs(Offset.Z) <= ParticleBoxHalfExtentCm;
			bAgesInRange &= NormalizedAges[ParticleIndex] >= 0.0f && NormalizedAges[ParticleIndex] <= 1.0f;
			bScalesPositive &= Scales[ParticleIndex].X > 0.0 && Scales[ParticleIndex].Y > 0.0 && Scales[ParticleIndex].Z > 0.0;
			bDynamicParametersInRange &= DynamicParameters[ParticleIndex].X >= 0.0 && DynamicParameters[ParticleIndex].X <= 1.0 && DynamicParameters[ParticleIndex].Y >= 0.0 && DynamicParameters[ParticleIndex].Y <= 1.0;
		}
		Test.TestTrue(*(RunLabel + TEXT(": positions are finite and inside the 600 cm fixed bounds")), bPositionsInBox);
		Test.TestTrue(*(RunLabel + TEXT(": NormalizedAge stays in [0, 1]")), bAgesInRange);
		Test.TestTrue(*(RunLabel + TEXT(": Scale is positive")), bScalesPositive);
		Test.TestTrue(*(RunLabel + TEXT(": heat and fade dynamic parameters stay in [0, 1]")), bDynamicParametersInRange);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraEmberActivatesTest,
	"TDGame.FluidFXRhi.Niagara.Ember.ActivatesAndCaptures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraEmberActivatesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraEmberTestPrivate;
	const TCHAR* const TestLabel = TEXT("ActivatesAndCaptures");
	if (!CanRunEmberTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.Ember.ActivatesAndCaptures")))
	{
		return false;
	}

	FTDEmberRunResult StampedRun;
	if (!RunEmberCapture(*this, TestLabel, TEXT("stamped"), FTDEmberRunSettings(), StampedRun))
	{
		return false;
	}
	TestTrue(TEXT("Two seconds of fire stamps leave live embers"), StampedRun.FinalParticleCount > 0);

	FTDEmberRunSettings UnstampedSettings;
	UnstampedSettings.bStampsFire = false;
	FTDEmberRunResult UnstampedRun;
	if (!RunEmberCapture(*this, TestLabel, TEXT("unstamped"), UnstampedSettings, UnstampedRun))
	{
		return false;
	}
	TestEqual(TEXT("Without fire stamps the density rejection leaves no embers"), UnstampedRun.FinalParticleCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraEmberAttributeRangesTest,
	"TDGame.FluidFXRhi.Niagara.Ember.AttributeRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraEmberAttributeRangesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraEmberTestPrivate;
	const TCHAR* const TestLabel = TEXT("AttributeRanges");
	if (!CanRunEmberTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.Ember.AttributeRanges")))
	{
		return false;
	}

	FTDEmberRunSettings PositiveSettings;
	PositiveSettings.StampVelocityXCmPerSec = CouplingStampSpeedCmPerSec;
	FTDEmberRunResult PositiveRun;
	if (!RunEmberCapture(*this, TestLabel, TEXT("plus-x"), PositiveSettings, PositiveRun))
	{
		return false;
	}
	ExpectFinalAttributeRanges(*this, TEXT("plus-x"), PositiveRun);

	FTDEmberRunSettings NegativeSettings;
	NegativeSettings.StampVelocityXCmPerSec = -CouplingStampSpeedCmPerSec;
	FTDEmberRunResult NegativeRun;
	if (!RunEmberCapture(*this, TestLabel, TEXT("minus-x"), NegativeSettings, NegativeRun))
	{
		return false;
	}

	int32 PositivePairCount = 0;
	int32 NegativePairCount = 0;
	const double PositiveDisplacementCm = ComputeMeanStepDisplacementX(PositiveRun, PositivePairCount);
	const double NegativeDisplacementCm = ComputeMeanStepDisplacementX(NegativeRun, NegativePairCount);
	AddInfo(FString::Printf(TEXT("Ember fluid coupling: mean X step %.4f cm over %d pairs with +X stamps, %.4f cm over %d pairs with -X stamps"), PositiveDisplacementCm, PositivePairCount, NegativeDisplacementCm, NegativePairCount));
	TestTrue(TEXT("Both coupling runs match particles by UniqueID across frames"), PositivePairCount > 0 && NegativePairCount > 0);
	TestTrue(TEXT("Embers drift +X when the fluid is pushed +X"), PositiveDisplacementCm > 0.0);
	TestTrue(TEXT("Embers drift -X when the fluid is pushed -X"), NegativeDisplacementCm < 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraEmberRepeatRunTest,
	"TDGame.FluidFXRhi.Niagara.Ember.RepeatRunMatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraEmberRepeatRunTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidNiagaraEmberTestPrivate;
	const TCHAR* const TestLabel = TEXT("RepeatRunMatches");
	if (!CanRunEmberTest(*this, TEXT("TDGame.FluidFXRhi.Niagara.Ember.RepeatRunMatches")))
	{
		return false;
	}

	FTDEmberRunSettings RepeatSettings;
	RepeatSettings.StepCount = RepeatCaptureStepCount;
	FTDEmberRunSettings ShiftedSeedSettings = RepeatSettings;
	ShiftedSeedSettings.SeedOffset = FalsificationSeedOffset;
	FTDEmberRunResult FirstRun;
	FTDEmberRunResult SecondRun;
	FTDEmberRunResult ShiftedSeedRun;
	if (!RunEmberCapture(*this, TestLabel, TEXT("run-a"), RepeatSettings, FirstRun)
		|| !RunEmberCapture(*this, TestLabel, TEXT("run-b"), RepeatSettings, SecondRun)
		|| !RunEmberCapture(*this, TestLabel, TEXT("run-c-seed"), ShiftedSeedSettings, ShiftedSeedRun))
	{
		return false;
	}

	const bool bFirstCovered = ExpectUniqueIdsCoverParticles(*this, TEXT("run-a"), FirstRun);
	const bool bSecondCovered = ExpectUniqueIdsCoverParticles(*this, TEXT("run-b"), SecondRun);
	const bool bShiftedCovered = ExpectUniqueIdsCoverParticles(*this, TEXT("run-c-seed"), ShiftedSeedRun);
	if (!bFirstCovered || !bSecondCovered || !bShiftedCovered)
	{
		return false;
	}

	TestTrue(TEXT("Every captured step read back the velocity texture, so the input comparison has content"), HasEveryVelocityHash(FirstRun) && HasEveryVelocityHash(SecondRun));
	const bool bInputsMatch = FirstRun.VelocityHashes == SecondRun.VelocityHashes && FirstRun.ActivationStepCount == SecondRun.ActivationStepCount;
	AddInfo(FString::Printf(TEXT("Ember repeat: velocity texture hashes %s, activation steps %d and %d"), bInputsMatch ? TEXT("match") : TEXT("differ (input nondeterminism)"), FirstRun.ActivationStepCount, SecondRun.ActivationStepCount));

	FNiagaraSimCacheCompare RepeatCompare;
	RepeatCompare.DefaultFloatTolerance = RepeatFloatTolerance;
	RepeatCompare.bIncludeDataInterfaces = false;
	RepeatCompare.SetToleranceByName(*FirstRun.SimCache, PositionAttributeName.ToString(), RepeatPositionToleranceCm);
	FString RepeatDifferences;
	const bool bRepeatMatches = RepeatCompare.Compare(*FirstRun.SimCache, *SecondRun.SimCache, RepeatDifferences);
	if (!bRepeatMatches)
	{
		AddInfo(TEXT("Ember repeat differences: ") + RepeatDifferences);
	}
	TestTrue(bInputsMatch ? TEXT("Run B matches run A within tolerance with identical fluid input") : TEXT("Run B matches run A within tolerance despite input nondeterminism"), bRepeatMatches);

	FNiagaraSimCacheCompare SeedCompare;
	SeedCompare.DefaultFloatTolerance = RepeatFloatTolerance;
	SeedCompare.bIncludeDataInterfaces = false;
	SeedCompare.SetToleranceByName(*FirstRun.SimCache, PositionAttributeName.ToString(), RepeatPositionToleranceCm);
	FString SeedDifferences;
	TestFalse(TEXT("Changing only the seed offset makes the caches differ, so the comparison is not vacuous"), SeedCompare.Compare(*FirstRun.SimCache, *ShiftedSeedRun.SimCache, SeedDifferences));
	return true;
}

#endif
