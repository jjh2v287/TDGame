#if WITH_DEV_AUTOMATION_TESTS

#include "Components/DecalComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "FluidFX/TDFluidNearRippleLayer.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidRainActor.h"
#include "FluidFX/TDFluidRainImpacts.h"
#include "FluidFX/TDFluidRainTypes.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraGPUProfilerInterface.h"
#include "NiagaraSimCache.h"
#include "NiagaraSystem.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Tickable.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include <limits>

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidRainRhiTests, Log, All);

namespace TDFluidRainRhiTestPrivate
{
	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr int32 RainGridResolution = 64;
	constexpr double RainCellSizeCm = 10.0;
	constexpr int32 RainWarmupSteps = 8;
	constexpr int32 RainSteps = 60;
	constexpr float WaterSurfaceZCm = 0.0f;
	constexpr float FloorTopZCm = -100.0f;
	constexpr float WetnessDryingSeconds = 8.0f;
	constexpr int32 CostFrameCount = 120;
	constexpr int32 CostRainGenerateMaxCount = 128;
	constexpr float CostRainWindowExtentCm = 3000.0f;
	constexpr int32 NearCostResolution = 256;
	constexpr float NearCostExtentCm = 600.0f;
	constexpr int32 NearCostMaskSize = 4;
	constexpr double NearCostOpenWaterExtentCm = 100000.0;
	constexpr float NearCostDepthReferenceCm = 300.0f;
	constexpr float NearCostObstacleRangeCm = 400.0f;
	constexpr float NearCostImpulseCm = 0.15f;
	constexpr int32 ComponentReadyStepLimit = 240;
	constexpr int32 NiagaraSettleFrames = 180;
	constexpr double NiagaraCountTolerance = 0.1;
	constexpr int32 PuddleSteps = 600;
	const TCHAR* const WaterGpuVariableName = TEXT("TD.FluidFX.WaterGpu");
	const TCHAR* const SolverBackendVariableName = TEXT("TD.FluidFX.SolverBackend");
	const TCHAR* const RainVariableName = TEXT("TD.FluidFX.Rain");
	const TCHAR* const RainIntensityVariableName = TEXT("TD.FluidFX.Rain.Intensity");
	const TCHAR* const RainSystemPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidRain.NS_TD_FluidRain");
	const TCHAR* const PondPresetPath = TEXT("/Game/FluidFX/Presets/DA_TDFluid_RainPond.DA_TDFluid_RainPond");
	const TCHAR* const PuddleDecalMaterialPath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidRainPuddleDecal_Default.MI_TD_FluidRainPuddleDecal_Default");
	const FName RainEmitterName(TEXT("Rain"));
	const FName PositionAttributeName(TEXT("Position"));
	const FName PuddleLevelParameterName(TEXT("RainPuddleLevelCm"));

	struct FTDRainConsoleVariableScope
	{
		FTDRainConsoleVariableScope(const TCHAR* Name, const TCHAR* Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetString();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDRainConsoleVariableScope()
		{
			if (Variable)
			{
				Variable->Set(*PreviousValue, ECVF_SetByCode);
			}
		}

		FTDRainConsoleVariableScope(const FTDRainConsoleVariableScope&) = delete;
		FTDRainConsoleVariableScope& operator=(const FTDRainConsoleVariableScope&) = delete;

		IConsoleVariable* Variable = nullptr;
		FString PreviousValue;
	};

	struct FTDRainGpuSelectionScope
	{
		FTDRainGpuSelectionScope()
			: SolverBackendScope(SolverBackendVariableName, TEXT("0"))
			, WaterGpuScope(WaterGpuVariableName, TEXT("1"))
			, RainScope(RainVariableName, TEXT("-1"))
			, RainIntensityScope(RainIntensityVariableName, TEXT("-1"))
		{
		}

		bool IsValid() const
		{
			return SolverBackendScope.Variable && WaterGpuScope.Variable && RainScope.Variable && RainIntensityScope.Variable;
		}

		FTDRainConsoleVariableScope SolverBackendScope;
		FTDRainConsoleVariableScope WaterGpuScope;
		FTDRainConsoleVariableScope RainScope;
		FTDRainConsoleVariableScope RainIntensityScope;
	};

	bool CanRunRainRhiTest(FAutomationTestBase& Test, const FString& CaseLabel, const FTDRainGpuSelectionScope& GpuScope)
	{
		const bool bHasRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidRainRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), *CaseLabel, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (!bHasRenderingRhi)
		{
			Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
			return false;
		}
		if (!GpuScope.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s: a fluid or rain console variable is not registered"), *CaseLabel));
			return false;
		}
		return true;
	}

	void ReportLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidRainRhiTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	void WaitForGpuIdle()
	{
		ENQUEUE_RENDER_COMMAND(TDFluidRainRhiWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
		});
		FlushRenderingCommands();
	}

	struct FTDRainRhiWorld
	{
		FTDRainRhiWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidRainRhiWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDRainRhiWorld()
		{
			if (!World)
			{
				return;
			}

			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}

		FTDRainRhiWorld(const FTDRainRhiWorld&) = delete;
		FTDRainRhiWorld& operator=(const FTDRainRhiWorld&) = delete;

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, StepSeconds);
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, StepSeconds);
			WaitForGpuIdle();
		}

		UTDFluidLiveSubsystem* GetSubsystem() const
		{
			return World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		}

		UWorld* World = nullptr;
	};

	bool ReadRenderTargetPixels(UTexture* OutputTexture, TArray<FFloat16Color>& OutPixels)
	{
		OutPixels.Reset();
		UTextureRenderTarget2D* OutputTarget = Cast<UTextureRenderTarget2D>(OutputTexture);
		FTextureRenderTargetResource* TargetResource = OutputTarget ? OutputTarget->GameThread_GetRenderTargetResource() : nullptr;
		if (!TargetResource)
		{
			return false;
		}

		FRenderTarget* ReadableTarget = TargetResource;
		return ReadableTarget->ReadFloat16Pixels(OutPixels) && !OutPixels.IsEmpty();
	}

	bool ArePixelsBitIdentical(const TArray<FFloat16Color>& FirstPixels, const TArray<FFloat16Color>& SecondPixels)
	{
		return FirstPixels.Num() == SecondPixels.Num()
			&& !FirstPixels.IsEmpty()
			&& FMemory::Memcmp(FirstPixels.GetData(), SecondPixels.GetData(), FirstPixels.Num() * sizeof(FFloat16Color)) == 0;
	}

	FTDFluidSimulationConfig MakeRainGpuConfig(bool bUsesWaterLayer)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.SolverBackend = ETDFluidSolverBackend::Gpu;
		Config.GridResolution = RainGridResolution;
		Config.SimExtentCm = FVector2D(RainGridResolution * RainCellSizeCm, RainGridResolution * RainCellSizeCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = bUsesWaterLayer ? 0.35f : 0.0f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = bUsesWaterLayer;
		Config.bEnableWaterWetness = bUsesWaterLayer;
		Config.WetnessDryingSeconds = WetnessDryingSeconds;
		Config.WetnessFromWhitewater = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.AmbientDensity = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.SwashHeightCm = 0.0f;
		Config.BankWetHeightCm = 0.0f;
		return Config;
	}

	FTDFluidRainSettings MakeRainGpuSettings(bool bEnabled, float DropDensityStrength)
	{
		FTDFluidRainSettings Settings;
		Settings.bEnabled = bEnabled;
		Settings.DropsPerSquareMeterPerSec = 400.0f;
		Settings.DropRadiusCm = 15.0f;
		Settings.DropRippleImpulseCm = 1.0f;
		Settings.DropDensityStrength = DropDensityStrength;
		Settings.MaxDropsPerTick = 64;
		Settings.WetnessFromRainPerSec = 0.5f;
		Settings.RandomSeed = 7;
		return Settings;
	}

	FTDFluidMassInputSettings MakeMassInputSettings(bool bAcceptMassInput)
	{
		FTDFluidMassInputSettings Settings;
		Settings.bAcceptMassInput = bAcceptMassInput;
		return Settings;
	}

	void ApplyHalfWaterTerrain(UTDFluidLiveComponent& Component, const FTDFluidSimulationConfig& Config)
	{
		const double HalfExtentCm = 0.5 * Config.SimExtentCm.X + 2.0 * RainCellSizeCm;
		FTDFluidWaterRegion Region;
		Region.MinXY = FVector2D(-HalfExtentCm, -HalfExtentCm);
		Region.MaxXY = FVector2D(0.0, HalfExtentCm);
		Region.SurfaceHeightCm = WaterSurfaceZCm;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(Region);
		Component.SetWaterGuidesAndRegions(TArray<FTDFluidFlowGuide>(), Regions);
		const int32 Resolution = Component.GetSimulationResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Component.SetTerrainElevationAtCell(CellX, CellY, FloorTopZCm);
			}
		}
		Component.CompleteTerrainElevationSampling();
	}

	struct FTDRainGpuArmOptions
	{
		FTDFluidRainSettings RainSettings;
		float Intensity = 0.0f;
		bool bUsesWaterLayer = true;
		bool bAcceptsMassInput = false;
		int32 StepCount = RainSteps;
	};

	struct FTDRainGpuArmResult
	{
		int32 Resolution = 0;
		TArray<FFloat16Color> WaterSurfacePixels;
		TArray<FFloat16Color> DensityPixels;
		int32 MaximumUploadedPointCount = 0;
		double MeanStepMilliseconds = 0.0;
	};

	bool RunRainGpuArm(FAutomationTestBase& Test, const FString& ArmLabel, const FTDRainGpuArmOptions& Options, FTDRainGpuArmResult& OutResult)
	{
		FTDRainRhiWorld ArmWorld;
		UTDFluidLiveSubsystem* Subsystem = ArmWorld.GetSubsystem();
		if (!Test.TestNotNull(*(ArmLabel + TEXT(" has the fluid subsystem")), Subsystem))
		{
			return false;
		}

		const FTDFluidSimulationConfig Config = MakeRainGpuConfig(Options.bUsesWaterLayer);
		const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
		ATDFluidLiveActor* LiveActor = ArmWorld.World->SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!Test.TestTrue(*(ArmLabel + TEXT(" spawned the GPU live actor")), LiveActor && LiveActor->LiveComponent))
		{
			return false;
		}
		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);

		UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
		OutResult.Resolution = Component.GetSimulationResolution();
		if (Options.bUsesWaterLayer)
		{
			ApplyHalfWaterTerrain(Component, Config);
		}
		for (int32 StepIndex = 0; StepIndex < RainWarmupSteps; ++StepIndex)
		{
			ArmWorld.StepWorld();
		}
		if (!Component.IsSimulationInitialized() || !Component.IsUsingGpuSolver())
		{
			Test.AddError(FString::Printf(TEXT("%s: the live actor did not start the GPU solver"), *ArmLabel));
			return false;
		}

		Component.SetMassInputSettingsForTests(MakeMassInputSettings(Options.bAcceptsMassInput));
		Component.SetRainSettingsForTests(Options.RainSettings);
		Subsystem->SetRainIntensity(Options.Intensity);
		double TotalStepSeconds = 0.0;
		for (int32 StepIndex = 0; StepIndex < Options.StepCount; ++StepIndex)
		{
			const double StepStartSeconds = FPlatformTime::Seconds();
			ArmWorld.StepWorld();
			TotalStepSeconds += FPlatformTime::Seconds() - StepStartSeconds;
			OutResult.MaximumUploadedPointCount = FMath::Max(OutResult.MaximumUploadedPointCount, Component.GetLastUploadedMassPointCount());
		}
		OutResult.MeanStepMilliseconds = Options.StepCount > 0 ? 1000.0 * TotalStepSeconds / Options.StepCount : std::numeric_limits<double>::quiet_NaN();

		const int32 CellCount = OutResult.Resolution * OutResult.Resolution;
		const bool bHasDensity = ReadRenderTargetPixels(Component.GetDensityPressureTexture(), OutResult.DensityPixels) && OutResult.DensityPixels.Num() == CellCount;
		if (!Test.TestTrue(*(ArmLabel + TEXT(" read the GPU density output for every cell")), bHasDensity))
		{
			return false;
		}
		if (!Options.bUsesWaterLayer)
		{
			return true;
		}

		const bool bHasSurface = ReadRenderTargetPixels(Component.GetWaterSurfaceOutputTexture(), OutResult.WaterSurfacePixels) && OutResult.WaterSurfacePixels.Num() == CellCount;
		return Test.TestTrue(*(ArmLabel + TEXT(" read the GPU water surface output for every cell")), bHasSurface);
	}

	FTDRainGpuArmOptions MakeArmOptions(const FTDFluidRainSettings& RainSettings, float Intensity, bool bUsesWaterLayer, bool bAcceptsMassInput, int32 StepCount)
	{
		FTDRainGpuArmOptions Options;
		Options.RainSettings = RainSettings;
		Options.Intensity = Intensity;
		Options.bUsesWaterLayer = bUsesWaterLayer;
		Options.bAcceptsMassInput = bAcceptsMassInput;
		Options.StepCount = StepCount;
		return Options;
	}

	struct FTDRainSurfaceStatistics
	{
		int32 WaterCellCount = 0;
		int32 DryCellCount = 0;
		double RippleVariance = 0.0;
		double DryWetnessMean = 0.0;
	};

	FTDRainSurfaceStatistics MeasureSurface(const TArray<FFloat16Color>& ClassifyingPixels, const TArray<FFloat16Color>& MeasuredPixels)
	{
		FTDRainSurfaceStatistics Statistics;
		double RippleSum = 0.0;
		double RippleSquareSum = 0.0;
		double DryWetnessSum = 0.0;
		for (int32 PixelIndex = 0; PixelIndex < ClassifyingPixels.Num() && PixelIndex < MeasuredPixels.Num(); ++PixelIndex)
		{
			const FFloat16Color& Pixel = MeasuredPixels[PixelIndex];
			if (ClassifyingPixels[PixelIndex].R.GetFloat() > 0.0f)
			{
				const double Ripple = Pixel.B.GetFloat();
				RippleSum += Ripple;
				RippleSquareSum += Ripple * Ripple;
				++Statistics.WaterCellCount;
				continue;
			}

			DryWetnessSum += Pixel.G.GetFloat();
			++Statistics.DryCellCount;
		}
		if (Statistics.WaterCellCount > 0)
		{
			const double RippleMean = RippleSum / Statistics.WaterCellCount;
			Statistics.RippleVariance = FMath::Max(RippleSquareSum / Statistics.WaterCellCount - RippleMean * RippleMean, 0.0);
		}
		if (Statistics.DryCellCount > 0)
		{
			Statistics.DryWetnessMean = DryWetnessSum / Statistics.DryCellCount;
		}
		return Statistics;
	}

	double SumDensityChannels(const TArray<FFloat16Color>& Pixels)
	{
		double Sum = 0.0;
		for (const FFloat16Color& Pixel : Pixels)
		{
			Sum += FMath::Abs(Pixel.R.GetFloat()) + FMath::Abs(Pixel.B.GetFloat());
		}
		return Sum;
	}

	bool WriteRecord(FAutomationTestBase& Test, const FString& FileName, const TArray<TPair<FString, double>>& Fields)
	{
		const FString RecordPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("w3"), FileName);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(RecordPath), true);
		TArray<FString> Lines;
		for (const TPair<FString, double>& Field : Fields)
		{
			const FString ValueText = FMath::IsFinite(Field.Value) ? FString::Printf(TEXT("%.6f"), Field.Value) : FString(TEXT("null"));
			Lines.Add(FString::Printf(TEXT("  \"%s\": %s"), *Field.Key, *ValueText));
		}
		const FString RecordText = FString::Printf(TEXT("{\n%s\n}\n"), *FString::Join(Lines, TEXT(",\n")));
		const bool bHasWritten = FFileHelper::SaveStringToFile(RecordText, *RecordPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Test.AddInfo(FString::Printf(TEXT("record %s"), *RecordPath));
		return Test.TestTrue(*(FileName + TEXT(" record is written")), bHasWritten);
	}

	double MeasureRainGenerateMicroseconds()
	{
		const FTDFluidRainSettings Settings = MakeRainGpuSettings(true, 0.0f);
		FTDFluidRainImpactState State;
		TArray<FTDFluidMassInputPoint> Points;
		double TotalSeconds = 0.0;
		for (int32 FrameIndex = 0; FrameIndex < CostFrameCount; ++FrameIndex)
		{
			Points.Reset();
			const double StartSeconds = FPlatformTime::Seconds();
			FTDFluidRainImpacts::GenerateImpacts(Settings, 1.0f, StepSeconds, FVector::ZeroVector, FVector2D(CostRainWindowExtentCm, CostRainWindowExtentCm), CostRainGenerateMaxCount, State, Points);
			TotalSeconds += FPlatformTime::Seconds() - StartSeconds;
		}
		return 1.0e6 * TotalSeconds / CostFrameCount;
	}

	UTextureRenderTarget2D* CreateNearOutputTarget(int32 Resolution)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = FLinearColor::Transparent;
		Target->bAutoGenerateMips = false;
		Target->bSupportsUAV = true;
		Target->bCanCreateUAV = true;
		Target->Filter = TF_Bilinear;
		Target->AddressX = TA_Wrap;
		Target->AddressY = TA_Wrap;
		Target->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	UTexture2D* CreateOpenWaterSurfaceTexture()
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(NearCostMaskSize, NearCostMaskSize, PF_FloatRGBA);
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}

		Texture->SRGB = false;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FFloat16Color* Pixels = static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 PixelIndex = 0; PixelIndex < NearCostMaskSize * NearCostMaskSize; ++PixelIndex)
		{
			Pixels[PixelIndex] = FFloat16Color(FLinearColor(1.0f, 0.0f, 1.0f, 0.0f));
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	double MeasureNearSubmitMilliseconds(UTexture& SurfaceTexture, float DropsPerSquareMeterPerSec)
	{
		FTDFluidWaterNearRippleSettings Settings;
		Settings.bEnabled = true;
		Settings.Resolution = NearCostResolution;
		Settings.ExtentCm = NearCostExtentCm;
		TStrongObjectPtr<UTextureRenderTarget2D> Output(CreateNearOutputTarget(FTDFluidNearRippleLayer::SanitizeResolution(Settings.Resolution)));
		FTDFluidNearRippleLayer Layer;
		Layer.Configure(Settings);
		FTDFluidNearRippleRainInput RainInput;
		RainInput.DropsPerSquareMeterPerSec = DropsPerSquareMeterPerSec;
		RainInput.DropImpulseCm = NearCostImpulseCm;
		RainInput.DropRadiusCm = 4.0f;
		RainInput.RandomSeed = 29u;
		FTDFluidNearRippleMaskInputs Masks;
		Masks.WaterSurfaceTexture = &SurfaceTexture;
		Masks.MainWindowExtentCm = FVector2D(NearCostOpenWaterExtentCm, NearCostOpenWaterExtentCm);
		Masks.WaterDepthReferenceCm = NearCostDepthReferenceCm;
		Masks.ObstacleDistanceOutputRangeCm = NearCostObstacleRangeCm;

		double TotalSeconds = 0.0;
		for (int32 FrameIndex = 0; FrameIndex < CostFrameCount; ++FrameIndex)
		{
			Layer.SetRainInput(RainInput);
			const double StartSeconds = FPlatformTime::Seconds();
			Layer.SubmitStep(Masks, FVector2D::ZeroVector, StepSeconds, *Output);
			WaitForGpuIdle();
			TotalSeconds += FPlatformTime::Seconds() - StartSeconds;
		}
		Layer.Release();
		WaitForGpuIdle();
		return 1000.0 * TotalSeconds / CostFrameCount;
	}

	bool ExpectLoaded(FAutomationTestBase& Test, const FString& CaseLabel, const UObject* LoadedObject, const TCHAR* ObjectPath)
	{
		if (LoadedObject)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s: %s is missing; build the W-3 assets (spec-w3 section 6) before this asset group"), *CaseLabel, ObjectPath));
		return false;
	}

	bool ReadExposedFloat(const UNiagaraSystem& System, const TCHAR* VariableName, float& OutValue)
	{
		const FNiagaraVariable Variable(FNiagaraTypeDefinition::GetFloatDef(), FName(VariableName));
		const FNiagaraUserRedirectionParameterStore& ExposedParameters = System.GetExposedParameters();
		if (!ExposedParameters.FindParameterOffset(Variable))
		{
			return false;
		}

		OutValue = ExposedParameters.GetParameterValue<float>(Variable);
		return FMath::IsFinite(OutValue);
	}

	UNiagaraComponent* WaitForActiveRainVisual(FTDRainRhiWorld& SceneWorld, const ATDFluidRainActor& RainActor)
	{
		for (int32 StepIndex = 0; StepIndex < ComponentReadyStepLimit; ++StepIndex)
		{
			if (RainActor.RainVisual && RainActor.RainVisual->IsActive())
			{
				return RainActor.RainVisual;
			}
			FlushAsyncLoading();
			SceneWorld.StepWorld();
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainRhiGpuWaterRippleAndWetnessTest,
	"TDGame.FluidFXRhi.Rain.GpuWaterRippleAndWetness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainRhiGpuWaterRippleAndWetnessTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Rain.GpuWaterRippleAndWetness");
	FTDRainGpuSelectionScope GpuScope;
	if (!CanRunRainRhiTest(*this, CaseLabel, GpuScope))
	{
		return false;
	}

	FTDRainGpuArmResult NoRainArm;
	FTDRainGpuArmResult ZeroIntensityArm;
	FTDRainGpuArmResult RainArm;
	if (!RunRainGpuArm(*this, CaseLabel + TEXT(" no rain"), MakeArmOptions(MakeRainGpuSettings(false, 0.0f), 1.0f, true, false, RainSteps), NoRainArm)
		|| !RunRainGpuArm(*this, CaseLabel + TEXT(" zero intensity"), MakeArmOptions(MakeRainGpuSettings(true, 0.0f), 0.0f, true, false, RainSteps), ZeroIntensityArm)
		|| !RunRainGpuArm(*this, CaseLabel + TEXT(" rain"), MakeArmOptions(MakeRainGpuSettings(true, 0.0f), 1.0f, true, false, RainSteps), RainArm))
	{
		return false;
	}

	TestTrue(TEXT("Zero rain intensity keeps the water surface readback bit-identical to no rain"), ArePixelsBitIdentical(NoRainArm.WaterSurfacePixels, ZeroIntensityArm.WaterSurfacePixels));
	TestTrue(TEXT("Zero rain intensity keeps the density readback bit-identical to no rain"), ArePixelsBitIdentical(NoRainArm.DensityPixels, ZeroIntensityArm.DensityPixels));

	const FTDRainSurfaceStatistics NoRainStatistics = MeasureSurface(NoRainArm.WaterSurfacePixels, NoRainArm.WaterSurfacePixels);
	const FTDRainSurfaceStatistics RainStatistics = MeasureSurface(NoRainArm.WaterSurfacePixels, RainArm.WaterSurfacePixels);
	ReportLine(*this, FString::Printf(TEXT("%s: water_cells=%d dry_cells=%d ripple_variance no_rain=%.9g rain=%.9g dry_wetness no_rain=%.6f rain=%.6f"),
		*CaseLabel, NoRainStatistics.WaterCellCount, NoRainStatistics.DryCellCount, NoRainStatistics.RippleVariance, RainStatistics.RippleVariance, NoRainStatistics.DryWetnessMean, RainStatistics.DryWetnessMean));
	if (!TestTrue(TEXT("The fixture has both water and dry cells"), NoRainStatistics.WaterCellCount > 0 && NoRainStatistics.DryCellCount > 0))
	{
		return false;
	}
	TestTrue(TEXT("Rain raises the ripple variance over water cells"), RainStatistics.RippleVariance > NoRainStatistics.RippleVariance);
	TestTrue(TEXT("Rain raises the wetness of dry cells"), RainStatistics.DryWetnessMean > NoRainStatistics.DryWetnessMean);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainRhiGpuNonAcceptingRoutesToStampsTest,
	"TDGame.FluidFXRhi.Rain.GpuNonAcceptingRoutesToStamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainRhiGpuNonAcceptingRoutesToStampsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Rain.GpuNonAcceptingRoutesToStamps");
	FTDRainGpuSelectionScope GpuScope;
	if (!CanRunRainRhiTest(*this, CaseLabel, GpuScope))
	{
		return false;
	}

	const FTDFluidRainSettings DensityRain = MakeRainGpuSettings(true, 1.0f);
	FTDRainGpuArmResult RefusingArm;
	FTDRainGpuArmResult AcceptingArm;
	if (!RunRainGpuArm(*this, CaseLabel + TEXT(" refusing mass input"), MakeArmOptions(DensityRain, 1.0f, false, false, RainSteps), RefusingArm)
		|| !RunRainGpuArm(*this, CaseLabel + TEXT(" accepting mass input"), MakeArmOptions(DensityRain, 1.0f, false, true, RainSteps), AcceptingArm))
	{
		return false;
	}

	const double RefusingDensity = SumDensityChannels(RefusingArm.DensityPixels);
	ReportLine(*this, FString::Printf(TEXT("%s: refusing density_sum=%.6f uploaded_max=%d, accepting uploaded_max=%d"), *CaseLabel, RefusingDensity, RefusingArm.MaximumUploadedPointCount, AcceptingArm.MaximumUploadedPointCount));
	TestTrue(TEXT("Rain on a GPU solver that refuses mass input lands as density stamps"), RefusingDensity > 0.0);
	TestEqual(TEXT("Rain on a GPU solver that refuses mass input uploads no points"), RefusingArm.MaximumUploadedPointCount, 0);
	TestTrue(TEXT("Rain on a GPU solver that accepts mass input uploads points"), AcceptingArm.MaximumUploadedPointCount > 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainRhiOffscreenCostTest,
	"TDGame.FluidFXRhi.RainCost.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainRhiOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.RainCost.OffscreenCost");
	FTDRainGpuSelectionScope GpuScope;
	if (!CanRunRainRhiTest(*this, CaseLabel, GpuScope))
	{
		return false;
	}

	FTDRainGpuArmResult WaterOffArm;
	FTDRainGpuArmResult WaterOnArm;
	if (!RunRainGpuArm(*this, CaseLabel + TEXT(" water rain off"), MakeArmOptions(MakeRainGpuSettings(false, 0.0f), 1.0f, true, false, CostFrameCount), WaterOffArm)
		|| !RunRainGpuArm(*this, CaseLabel + TEXT(" water rain on"), MakeArmOptions(MakeRainGpuSettings(true, 0.0f), 1.0f, true, false, CostFrameCount), WaterOnArm))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> SurfaceTexture(CreateOpenWaterSurfaceTexture());
	FlushRenderingCommands();
	if (!TestNotNull(*(CaseLabel + TEXT(" created the near layer water mask")), SurfaceTexture.Get()))
	{
		return false;
	}
	const double NearOffMilliseconds = MeasureNearSubmitMilliseconds(*SurfaceTexture, 0.0f);
	const double NearOnMilliseconds = MeasureNearSubmitMilliseconds(*SurfaceTexture, MakeRainGpuSettings(true, 0.0f).DropsPerSquareMeterPerSec);
	const double RainGenerateMicroseconds = MeasureRainGenerateMicroseconds();

	TArray<TPair<FString, double>> Fields;
	Fields.Add({ TEXT("frames"), static_cast<double>(CostFrameCount) });
	Fields.Add({ TEXT("near_rain_off_submit_ms_mean"), NearOffMilliseconds });
	Fields.Add({ TEXT("near_rain_on_submit_ms_mean"), NearOnMilliseconds });
	Fields.Add({ TEXT("near_rain_delta_ms"), NearOnMilliseconds - NearOffMilliseconds });
	Fields.Add({ TEXT("water_gpu_rain_off_step_ms_mean"), WaterOffArm.MeanStepMilliseconds });
	Fields.Add({ TEXT("water_gpu_rain_on_step_ms_mean"), WaterOnArm.MeanStepMilliseconds });
	Fields.Add({ TEXT("water_gpu_rain_delta_ms"), WaterOnArm.MeanStepMilliseconds - WaterOffArm.MeanStepMilliseconds });
	Fields.Add({ TEXT("rain_generate_us_mean"), RainGenerateMicroseconds });
	WriteRecord(*this, TEXT("cost-rain.json"), Fields);
	bool bAreFinite = true;
	for (const TPair<FString, double>& Field : Fields)
	{
		bAreFinite &= FMath::IsFinite(Field.Value);
	}
	TestTrue(TEXT("Every recorded rain cost is finite"), bAreFinite);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainRhiNiagaraRainCostTest,
	"TDGame.FluidFXRhi.RainAssets.NiagaraRainCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainRhiNiagaraRainCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.RainAssets.NiagaraRainCost");
#if WITH_NIAGARA_GPU_PROFILER
	FTDRainGpuSelectionScope GpuScope;
	if (!CanRunRainRhiTest(*this, CaseLabel, GpuScope))
	{
		return false;
	}

	TStrongObjectPtr<UNiagaraSystem> RainSystem(TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(RainSystemPath)).LoadSynchronous());
	if (!ExpectLoaded(*this, CaseLabel, RainSystem.Get(), RainSystemPath))
	{
		return false;
	}

	float SpawnRatePerSec = 0.0f;
	float SpawnHeightCm = 0.0f;
	float FallSpeedCmPerSec = 0.0f;
	float SplashSeconds = 0.0f;
	const bool bHasRainVariables = ReadExposedFloat(*RainSystem, TEXT("User.TDRainSpawnRatePerSec"), SpawnRatePerSec)
		&& ReadExposedFloat(*RainSystem, TEXT("User.TDRainSpawnHeightCm"), SpawnHeightCm)
		&& ReadExposedFloat(*RainSystem, TEXT("User.TDRainFallSpeedCmPerSec"), FallSpeedCmPerSec)
		&& ReadExposedFloat(*RainSystem, TEXT("User.TDRainSplashSeconds"), SplashSeconds);
	if (!TestTrue(TEXT("The rain system exposes its spawn rate, height, fall speed and splash time"), bHasRainVariables))
	{
		return false;
	}
	const double MeanLifetimeSeconds = 0.75 * SpawnHeightCm / FMath::Max(FallSpeedCmPerSec, 1.0f) + FMath::Max(SplashSeconds, 0.01f);
	const double ExpectedParticleCount = SpawnRatePerSec * MeanLifetimeSeconds;

	FTDRainRhiWorld SceneWorld;
	const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	ATDFluidRainActor* RainActor = SceneWorld.World->SpawnActorDeferred<ATDFluidRainActor>(ATDFluidRainActor::StaticClass(), SpawnTransform);
	if (!TestNotNull(TEXT("The rain actor is spawned"), RainActor))
	{
		return false;
	}
	RainActor->RainSystem = RainSystem.Get();
	RainActor->bShouldFollowViewTarget = false;
	RainActor->SetRainIntensity(1.0f);
	RainActor->FinishSpawning(SpawnTransform);

	UNiagaraComponent* RainComponent = WaitForActiveRainVisual(SceneWorld, *RainActor);
	if (!TestNotNull(TEXT("The rain visual activates"), RainComponent))
	{
		return false;
	}

	TWeakObjectPtr<UNiagaraComponent> WeakRainComponent(RainComponent);
	double FrameRainMicroseconds = 0.0;
	FNiagaraGpuProfilerListener Listener;
	Listener.SetHandler([&](const FNiagaraGpuFrameResultsPtr& Results)
	{
		if (!Results.IsValid())
		{
			return;
		}

		for (const FNiagaraGpuFrameResults::FDispatchResults& Dispatch : Results->DispatchResults)
		{
			if (Dispatch.OwnerComponent.Get() == WeakRainComponent.Get())
			{
				FrameRainMicroseconds += static_cast<double>(Dispatch.DurationMicroseconds);
			}
		}
	});

	TArray<double> RainMicroseconds;
	for (int32 FrameIndex = 0; FrameIndex < NiagaraSettleFrames; ++FrameIndex)
	{
		const bool bIsMeasuring = FrameIndex >= NiagaraSettleFrames - CostFrameCount;
		Listener.SetEnabled(bIsMeasuring);
		FrameRainMicroseconds = 0.0;
		SceneWorld.StepWorld();
		if (bIsMeasuring)
		{
			RainMicroseconds.Add(FrameRainMicroseconds);
		}
	}
	Listener.SetEnabled(false);

	UNiagaraSimCache* SimCache = NewObject<UNiagaraSimCache>(GetTransientPackage());
	TStrongObjectPtr<UNiagaraSimCache> SimCacheOwner(SimCache);
	if (!TestTrue(TEXT("The rain sim cache write begins"), SimCache->BeginWrite(FNiagaraSimCacheCreateParameters(), RainComponent)))
	{
		return false;
	}
	const bool bHasFrame = SimCache->WriteFrame(RainComponent);
	SimCache->EndWrite();
	if (!TestTrue(TEXT("The rain sim cache captures one frame"), bHasFrame))
	{
		return false;
	}
	TArray<FVector> Positions;
	SimCache->ReadPositionAttribute(Positions, PositionAttributeName, RainEmitterName, true, SimCache->GetNumFrames() - 1);

	double MeanRainMicroseconds = 0.0;
	for (const double Value : RainMicroseconds)
	{
		MeanRainMicroseconds += Value;
	}
	MeanRainMicroseconds = RainMicroseconds.IsEmpty() ? std::numeric_limits<double>::quiet_NaN() : MeanRainMicroseconds / RainMicroseconds.Num();

	TArray<TPair<FString, double>> Fields;
	Fields.Add({ TEXT("frames"), static_cast<double>(CostFrameCount) });
	Fields.Add({ TEXT("expected_live_particles"), ExpectedParticleCount });
	Fields.Add({ TEXT("live_particles"), static_cast<double>(Positions.Num()) });
	Fields.Add({ TEXT("rain_gpu_us_mean"), MeanRainMicroseconds });
	WriteRecord(*this, TEXT("cost-rain-niagara.json"), Fields);
	ReportLine(*this, FString::Printf(TEXT("%s: live=%d expected=%.1f (rate %.1f x mean lifetime %.4f s) gpu_us=%.3f"), *CaseLabel, Positions.Num(), ExpectedParticleCount, SpawnRatePerSec, MeanLifetimeSeconds, MeanRainMicroseconds));
	TestTrue(TEXT("Live rain particles match rate times mean lifetime within 10 percent"), FMath::Abs(Positions.Num() - ExpectedParticleCount) <= NiagaraCountTolerance * ExpectedParticleCount);
	TestTrue(TEXT("The rain GPU cost is finite"), FMath::IsFinite(MeanRainMicroseconds));
	return !HasAnyErrors();
#else
	AddError(FString::Printf(TEXT("%s needs WITH_NIAGARA_GPU_PROFILER"), *CaseLabel));
	return false;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainRhiPuddleDecalParametersTest,
	"TDGame.FluidFXRhi.RainAssets.PuddleDecalParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainRhiPuddleDecalParametersTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.RainAssets.PuddleDecalParameters");
	FTDRainGpuSelectionScope GpuScope;
	if (!CanRunRainRhiTest(*this, CaseLabel, GpuScope))
	{
		return false;
	}

	TStrongObjectPtr<UTDFluidPresetAsset> PondPreset(TSoftObjectPtr<UTDFluidPresetAsset>(FSoftObjectPath(PondPresetPath)).LoadSynchronous());
	TStrongObjectPtr<UMaterialInterface> PuddleMaterial(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(PuddleDecalMaterialPath)).LoadSynchronous());
	const bool bHasPreset = ExpectLoaded(*this, CaseLabel, PondPreset.Get(), PondPresetPath);
	const bool bHasMaterial = ExpectLoaded(*this, CaseLabel, PuddleMaterial.Get(), PuddleDecalMaterialPath);
	if (!bHasPreset || !bHasMaterial)
	{
		return false;
	}

	FTDRainRhiWorld SceneWorld;
	UTDFluidLiveSubsystem* Subsystem = SceneWorld.GetSubsystem();
	AActor* Anchor = SceneWorld.World->SpawnActor<AActor>(AActor::StaticClass(), FVector(0.0, 0.0, WaterSurfaceZCm), FRotator::ZeroRotator);
	const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	ATDFluidWaterActor* WaterActor = SceneWorld.World->SpawnActorDeferred<ATDFluidWaterActor>(ATDFluidWaterActor::StaticClass(), SpawnTransform);
	if (!TestNotNull(TEXT("The fluid subsystem exists"), Subsystem) || !TestTrue(TEXT("The water actor is spawned"), WaterActor && WaterActor->LiveComponent))
	{
		return false;
	}
	WaterActor->bEnableRainPuddleDecal = true;
	WaterActor->RainPuddleDecalMaterial = PuddleMaterial.Get();
	WaterActor->bSleepWhenNoWaterInWindow = false;
	WaterActor->bSampleTerrainElevation = false;
	WaterActor->ViewLookAheadCm = 0.0f;
	WaterActor->FollowTargetActor = Anchor;
	WaterActor->FinishSpawning(SpawnTransform);
	WaterActor->InitializeFromPreset(PondPreset.Get(), 0.0f);
	Subsystem->SetRainIntensity(1.0f);

	UTDFluidLiveComponent& WaterComponent = *WaterActor->LiveComponent;
	for (int32 StepIndex = 0; StepIndex < ComponentReadyStepLimit && !WaterComponent.IsSimulationInitialized(); ++StepIndex)
	{
		SceneWorld.StepWorld();
	}
	if (!TestTrue(TEXT("The pond simulation initializes"), WaterComponent.IsSimulationInitialized()))
	{
		return false;
	}
	const int32 Resolution = WaterComponent.GetSimulationResolution();
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			WaterComponent.SetTerrainElevationAtCell(CellX, CellY, FloorTopZCm);
		}
	}
	WaterComponent.CompleteTerrainElevationSampling();

	for (int32 StepIndex = 0; StepIndex < PuddleSteps; ++StepIndex)
	{
		SceneWorld.StepWorld();
	}

	UMaterialInstanceDynamic* PuddleMID = WaterActor->GetRainPuddleDecalMaterialInstance();
	if (!TestNotNull(TEXT("The puddle decal material instance exists"), PuddleMID))
	{
		return false;
	}
	float PuddleLevelCm = 0.0f;
	const bool bHasLevel = PuddleMID->GetScalarParameterValue(FHashedMaterialParameterInfo(PuddleLevelParameterName), PuddleLevelCm);
	ReportLine(*this, FString::Printf(TEXT("%s: component_level=%.4f mid_level=%.4f visible=%d"), *CaseLabel, WaterComponent.GetRainPuddleLevelCm(), PuddleLevelCm, WaterActor->IsRainPuddleDecalVisible() ? 1 : 0));
	TestTrue(TEXT("The puddle decal receives a positive RainPuddleLevelCm after 10 s of rain"), bHasLevel && PuddleLevelCm > 0.0f);
	TestTrue(TEXT("The puddle decal is visible"), WaterActor->IsRainPuddleDecalVisible());
	return !HasAnyErrors();
}

#endif
