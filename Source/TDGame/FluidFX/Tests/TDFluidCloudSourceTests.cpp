#if WITH_DEV_AUTOMATION_TESTS

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidCloudLayersActor.h"
#include "FluidFX/TDFluidCloudPainterActor.h"
#include "FluidFX/TDFluidCloudWindCurtainActor.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/Float16Color.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tickable.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace TDFluidCloudSourceTestPrivate
{
	constexpr double CentimetersPerKilometer = 100000.0;
	constexpr double PositionToleranceCm = 1e-3;
	constexpr double UvTolerance = 1e-6;
	constexpr double VelocityToleranceCmPerSec = 1e-3;
	constexpr double SimulationClockToleranceSeconds = 1e-6;
	constexpr float PaintStepSeconds = 1.0f / 30.0f;
	constexpr float SolverStepSeconds = 1.0f / 30.0f;
	constexpr float LiveActiveSeconds = 600.0f;
	constexpr float LayerAltitudeCm = 150000.0f;
	constexpr int32 TestResolution = 128;
	constexpr int32 MaxInitializeStepCount = 60;
	constexpr int32 StrokeStepCount = 30;
	constexpr int32 SimulationClockStepCount = 10;
	constexpr int32 SimulationClockRestartStepCount = 3;
	constexpr int32 CycloneStepCount = 900;
	constexpr int32 StormWallStepCount = 600;
	constexpr int32 RoundTripFrameCount = 20;
	constexpr int32 MaxLoadStepCount = 400;
	constexpr int32 SettleStepCount = 8;
	constexpr int32 CaptureWarmupCount = 5;
	constexpr int32 CaptureMeasuredCount = 30;
	const TCHAR* const SetupConsoleName = TEXT("TD.FluidFX.CloudSetup");
	const TCHAR* const CycloneLiveMaterialPath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidCloud_CycloneLive.MI_TD_FluidCloud_CycloneLive");
	const TCHAR* const StormWallMaterialPath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidCloud_StormWall.MI_TD_FluidCloud_StormWall");
	const TCHAR* const WrapFieldMaterialPath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidCloud_WrapField.MI_TD_FluidCloud_WrapField");
	const TCHAR* const CyclonePresetPath = TEXT("/Game/FluidFX/Presets/DA_TDFluid_CloudCyclone.DA_TDFluid_CloudCyclone");
	const TCHAR* const StormWallPresetPath = TEXT("/Game/FluidFX/Presets/DA_TDFluid_CloudStormWall.DA_TDFluid_CloudStormWall");
	const TCHAR* const WrapFieldPresetPath = TEXT("/Game/FluidFX/Presets/DA_TDFluid_CloudWrapField.DA_TDFluid_CloudWrapField");
	const FName CycloneTag(TEXT("TDFluidCloud_Cyclone"));
	const FName StormWallTag(TEXT("TDFluidCloud_StormWall"));
	const FName WrapFieldTag(TEXT("TDFluidCloud_WrapField"));

	struct FTDCloudSourceTestWorld
	{
		FTDCloudSourceTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidCloudSourceWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDCloudSourceTestWorld()
		{
			if (!World)
			{
				return;
			}

			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			FlushRenderingCommands();
		}

		void StepWorld(float StepSeconds)
		{
			++GFrameCounter;
			FlushAsyncLoading();
			World->Tick(LEVELTICK_All, StepSeconds);
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, StepSeconds);
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	struct FTDCloudSourceConsoleRestore
	{
		explicit FTDCloudSourceConsoleRestore(const TCHAR* InName)
			: Variable(IConsoleManager::Get().FindConsoleVariable(InName))
		{
			if (Variable)
			{
				OriginalValue = Variable->GetString();
			}
		}

		~FTDCloudSourceConsoleRestore()
		{
			if (Variable)
			{
				Variable->Set(*OriginalValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		FString OriginalValue;
	};

	struct FTDCycloneResult
	{
		double EyeMeanDensity = 0.0;
		double RingMeanDensity = 0.0;
		double RingMeanTangentialCmPerSec = 0.0;
		uint32 StateChecksum = 0;
	};

	struct FTDStormWallResult
	{
		double CentroidVCm = 0.0;
		double CurtainVCm = 0.0;
		double TopEdgeStdDevCells = 0.0;
		uint32 StateChecksum = 0;
	};

	FTDFluidCloudPaintFootprint MakeFootprint(const FVector2D& CenterKm, const FVector2D& SizeKm, float PatternScaleKm)
	{
		FTDFluidCloudPaintFootprint Footprint;
		Footprint.CenterKm = CenterKm;
		Footprint.SizeKm = SizeKm;
		Footprint.PatternScaleKm = PatternScaleKm;
		Footprint.LayerAltitudeCm = LayerAltitudeCm;
		return Footprint;
	}

	FTDFluidCloudSceneSettings LoadSceneSettings()
	{
		FTDFluidCloudSceneSettings Settings;
		UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFluidCloudSceneSettings::StaticStruct(), &Settings);
		return Settings;
	}

	bool IsNear(const FVector2D& Actual, const FVector2D& Expected, double Tolerance)
	{
		return FMath::Abs(Actual.X - Expected.X) <= Tolerance && FMath::Abs(Actual.Y - Expected.Y) <= Tolerance;
	}

	void TestDisplayMapping(FAutomationTestBase& Test, const TCHAR* Label, const FVector2D& DisplayKm, const FTDFluidCloudPaintFootprint& Footprint, const FVector2D& ExpectedUv, const FVector2D& ExpectedPlaneCm)
	{
		FVector2D PlanePoint;
		FVector2D Uv;
		const bool bMapped = ATDFluidCloudPainterActor::MapDisplayPointToSimulation(DisplayKm * CentimetersPerKilometer, Footprint, FVector2D(100.0, -50.0), FVector2D(3000.0, 3000.0), PlanePoint, Uv);
		Test.TestTrue(*FString::Printf(TEXT("%s maps inside the footprint"), Label), bMapped);
		Test.TestTrue(*FString::Printf(TEXT("%s uv (%.6f, %.6f) equals (%.6f, %.6f)"), Label, Uv.X, Uv.Y, ExpectedUv.X, ExpectedUv.Y), IsNear(Uv, ExpectedUv, UvTolerance));
		Test.TestTrue(*FString::Printf(TEXT("%s plane (%.4f, %.4f) equals (%.4f, %.4f)"), Label, PlanePoint.X, PlanePoint.Y, ExpectedPlaneCm.X, ExpectedPlaneCm.Y), IsNear(PlanePoint, ExpectedPlaneCm, PositionToleranceCm));
	}

	FTDFluidBrushStamp MakeSolverStamp(const FTDFluidCloudWindCurtainStamp& PlaneStamp, float VelocityStrength)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(PlaneStamp.PlanePointCm.X, PlaneStamp.PlanePointCm.Y, 0.0);
		Stamp.WorldVelocity = FVector(PlaneStamp.PlaneVelocityCmPerSec.X, PlaneStamp.PlaneVelocityCmPerSec.Y, 0.0);
		Stamp.RadiusCm = PlaneStamp.RadiusCm;
		Stamp.DensityStrength = PlaneStamp.DensityStrength;
		Stamp.VelocityStrength = VelocityStrength;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		return Stamp;
	}

	FTDFluidSimulationConfig MakeCpuConfig(ETDFluidPresetKind Kind)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		Config.SolverBackend = ETDFluidSolverBackend::Cpu;
		Config.GridResolution = TestResolution;
		Config.CpuFallbackResolution = TestResolution;
		return Config;
	}

	FVector2D ComputeCellCenter(const FTDFluidSimulationConfig& Config, int32 Resolution, int32 CellX, int32 CellY)
	{
		const FVector2D CellSizeCm = Config.SimExtentCm / static_cast<double>(Resolution);
		return -0.5 * Config.SimExtentCm + FVector2D(CellX + 0.5, CellY + 0.5) * CellSizeCm;
	}

	FTDCycloneResult RunCyclone(bool bUsesEyeSink)
	{
		const FTDFluidSimulationConfig Config = MakeCpuConfig(ETDFluidPresetKind::CloudCyclone);
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);

		TArray<FTDFluidBrushStamp> Stamps;
		FTDFluidCloudWindCurtainStamp EyeSinkStamp;
		if (bUsesEyeSink && ATDFluidCloudWindCurtainActor::BuildEyeSinkStamp(LoadSceneSettings(), FVector2D::ZeroVector, Config.SimExtentCm, EyeSinkStamp))
		{
			Stamps.Add(MakeSolverStamp(EyeSinkStamp, 0.0f));
		}
		for (int32 StepIndex = 0; StepIndex < CycloneStepCount; ++StepIndex)
		{
			Solver.ApplyBrushStamps(Stamps, SolverStepSeconds);
			Solver.StepSimulation(SolverStepSeconds);
		}

		const double MinExtentCm = FMath::Min(Config.SimExtentCm.X, Config.SimExtentCm.Y);
		double EyeSum = 0.0;
		double RingSum = 0.0;
		double TangentialSum = 0.0;
		int32 EyeCount = 0;
		int32 RingCount = 0;
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FVector2D CellCenter = ComputeCellCenter(Config, Resolution, CellX, CellY);
				const double RadiusCm = CellCenter.Size();
				const FTDFluidCellSample Sample = Solver.SampleAtCell(CellX, CellY);
				if (RadiusCm < 0.04 * MinExtentCm)
				{
					EyeSum += Sample.PrimaryDensity;
					++EyeCount;
				}
				if (RadiusCm >= 0.12 * MinExtentCm && RadiusCm <= 0.3 * MinExtentCm)
				{
					RingSum += Sample.PrimaryDensity;
					TangentialSum += (CellCenter.X * Sample.VelocityCmPerSec.Y - CellCenter.Y * Sample.VelocityCmPerSec.X) / RadiusCm;
					++RingCount;
				}
			}
		}

		FTDCycloneResult Result;
		Result.EyeMeanDensity = EyeSum / FMath::Max(EyeCount, 1);
		Result.RingMeanDensity = RingSum / FMath::Max(RingCount, 1);
		Result.RingMeanTangentialCmPerSec = TangentialSum / FMath::Max(RingCount, 1);
		Result.StateChecksum = Solver.ComputeStateChecksum();
		return Result;
	}

	double ComputeStandardDeviation(const TArray<double>& Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}

		double Mean = 0.0;
		for (const double Value : Values)
		{
			Mean += Value;
		}
		Mean /= Values.Num();
		double Variance = 0.0;
		for (const double Value : Values)
		{
			Variance += FMath::Square(Value - Mean);
		}
		return FMath::Sqrt(Variance / Values.Num());
	}

	FTDStormWallResult RunStormWall()
	{
		const FTDFluidSimulationConfig Config = MakeCpuConfig(ETDFluidPresetKind::CloudStormWall);
		const FTDFluidCloudSceneSettings Settings = LoadSceneSettings();
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));

		TArray<FTDFluidCloudWindCurtainStamp> CurtainStamps;
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StepIndex = 0; StepIndex < StormWallStepCount; ++StepIndex)
		{
			ATDFluidCloudWindCurtainActor::BuildWindCurtainStamps(Settings, FVector2D::ZeroVector, Config.SimExtentCm, StepIndex * static_cast<double>(SolverStepSeconds), Config.ActorBrushDensityStrength, CurtainStamps);
			Stamps.Reset();
			for (const FTDFluidCloudWindCurtainStamp& CurtainStamp : CurtainStamps)
			{
				Stamps.Add(MakeSolverStamp(CurtainStamp, 1.0f));
			}
			Solver.ApplyBrushStamps(Stamps, SolverStepSeconds);
			Solver.StepSimulation(SolverStepSeconds);
		}

		const int32 Resolution = Solver.GetResolution();
		double DensitySum = 0.0;
		double WeightedVSum = 0.0;
		TArray<double> TopEdgeCells;
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			int32 TopCellY = INDEX_NONE;
			for (int32 CellY = 0; CellY < Resolution; ++CellY)
			{
				const float Density = Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
				DensitySum += Density;
				WeightedVSum += Density * ComputeCellCenter(Config, Resolution, CellX, CellY).Y;
				TopCellY = Density > 0.2f ? CellY : TopCellY;
			}
			if (TopCellY != INDEX_NONE)
			{
				TopEdgeCells.Add(TopCellY);
			}
		}

		FTDStormWallResult Result;
		Result.CentroidVCm = WeightedVSum / FMath::Max(DensitySum, UE_DOUBLE_SMALL_NUMBER);
		Result.CurtainVCm = -0.5 * Config.SimExtentCm.Y + Settings.WindCurtainHeightRatio * Config.SimExtentCm.Y;
		Result.TopEdgeStdDevCells = ComputeStandardDeviation(TopEdgeCells);
		Result.StateChecksum = Solver.ComputeStateChecksum();
		return Result;
	}

	ATDFluidLiveActor* SpawnLive(FAutomationTestBase& Test, UWorld& World, UTDFluidPresetAsset* Preset, const FVector& Location, const FName& LiveActorTag)
	{
		UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(&World);
		if (!Test.TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem) || !Test.TestNotNull(TEXT("the source preset exists"), Preset))
		{
			return nullptr;
		}

		ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, Location, LiveActiveSeconds);
		if (!Test.TestNotNull(*FString::Printf(TEXT("spawned the source live %s"), *LiveActorTag.ToString()), LiveActor))
		{
			return nullptr;
		}

		LiveActor->Tags.AddUnique(LiveActorTag);
		return LiveActor;
	}

	ATDFluidLiveActor* SpawnPaintLive(FAutomationTestBase& Test, FTDCloudSourceTestWorld& TestWorld, ETDFluidSolverBackend Backend)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::CloudPaint);
		if (Preset && Backend == ETDFluidSolverBackend::Cpu)
		{
			Preset->bOverridePresetDefaults = true;
			Preset->SimulationConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
			Preset->SimulationConfig.GridResolution = TestResolution;
			Preset->SimulationConfig.CpuFallbackResolution = TestResolution;
		}

		ATDFluidLiveActor* LiveActor = SpawnLive(Test, *TestWorld.World, Preset, FVector::ZeroVector, FName(TEXT("TDFluidCloud_Paint")));
		for (int32 StepIndex = 0; LiveActor && StepIndex < MaxInitializeStepCount && !LiveActor->LiveComponent->IsSimulationInitialized(); ++StepIndex)
		{
			TestWorld.StepWorld(PaintStepSeconds);
		}

		if (!LiveActor || !Test.TestTrue(TEXT("the paint live initialized"), LiveActor->LiveComponent->IsSimulationInitialized()))
		{
			return nullptr;
		}
		return LiveActor;
	}

	ATDFluidCloudPainterActor* SpawnDetachedPainter(UWorld& World)
	{
		ATDFluidCloudPainterActor* Painter = World.SpawnActor<ATDFluidCloudPainterActor>();
		if (Painter)
		{
			Painter->SetActorTickEnabled(false);
		}
		return Painter;
	}

	float SamplePlaneDensity(const UTDFluidLiveComponent& LiveComponent, const FVector2D& PlanePoint)
	{
		FTDFluidCellSample Sample;
		return LiveComponent.SampleFluidAtWorldLocation(LiveComponent.UnprojectSolverPlaneToWorld(PlanePoint), Sample) ? Sample.PrimaryDensity : -1.0f;
	}

	void RunScriptedPaintPass(FTDCloudSourceTestWorld& TestWorld, ATDFluidCloudPainterActor& Painter, ATDFluidLiveActor& LiveActor, const FTDFluidCloudPaintFootprint& Footprint, bool bErase)
	{
		for (int32 StepIndex = 0; StepIndex < StrokeStepCount; ++StepIndex)
		{
			const double StrokeAlpha = static_cast<double>(StepIndex) / (StrokeStepCount - 1);
			const double DisplayXCm = FMath::Lerp(-2.0, 2.0, StrokeAlpha) * CentimetersPerKilometer;
			Painter.PaintAtDisplayPoint(LiveActor, Footprint, FVector(DisplayXCm, 0.0, Footprint.LayerAltitudeCm), bErase, PaintStepSeconds);
			TestWorld.StepWorld(PaintStepSeconds);
		}
		Painter.EndStroke();
	}

	bool CanRunCloudSourceRhiTest(FAutomationTestBase& Test, const TCHAR* Label)
	{
		if (!GUsingNullRHI)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI."), Label));
		return false;
	}

	bool ReadRenderTargetPixels(UTexture* OutputTexture, TArray<FFloat16Color>& OutPixels, FIntPoint& OutSize)
	{
		OutPixels.Reset();
		UTextureRenderTarget2D* OutputTarget = Cast<UTextureRenderTarget2D>(OutputTexture);
		FTextureRenderTargetResource* TargetResource = OutputTarget ? OutputTarget->GameThread_GetRenderTargetResource() : nullptr;
		if (!TargetResource)
		{
			return false;
		}

		OutSize = FIntPoint(OutputTarget->SizeX, OutputTarget->SizeY);
		FRenderTarget* ReadableTarget = TargetResource;
		return ReadableTarget->ReadFloat16Pixels(OutPixels) && OutPixels.Num() == OutSize.X * OutSize.Y;
	}

	float ReadTexelRed(const TArray<FFloat16Color>& Pixels, const FIntPoint& Size, const FVector2D& Uv)
	{
		const int32 TexelX = FMath::Clamp(FMath::FloorToInt(Uv.X * Size.X), 0, Size.X - 1);
		const int32 TexelY = FMath::Clamp(FMath::FloorToInt(Uv.Y * Size.Y), 0, Size.Y - 1);
		return Pixels[TexelY * Size.X + TexelX].R.GetFloat();
	}

	FTDFluidCloudSetup MakeSourceSetup(const TCHAR* SetupName, const TCHAR* MaterialPath, const FName& LiveActorTag, ETDFluidCloudSlotTextureRole Role)
	{
		FTDFluidCloudSetup Setup;
		Setup.SetupName = FName(SetupName);
		Setup.CloudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(MaterialPath));
		FTDFluidCloudSlotSource& SlotSource = Setup.SlotSources.AddDefaulted_GetRef();
		SlotSource.SlotIndex = 1;
		SlotSource.Role = Role;
		SlotSource.LiveActorTag = LiveActorTag;
		return Setup;
	}

	struct FTDCloudSourceStage
	{
		TStrongObjectPtr<UTDFluidPresetAsset> CyclonePreset;
		TStrongObjectPtr<UTDFluidPresetAsset> StormWallPreset;
		TStrongObjectPtr<UTDFluidPresetAsset> WrapFieldPreset;
		ATDFluidLiveActor* CycloneLive = nullptr;
		ATDFluidLiveActor* StormWallLive = nullptr;
		ATDFluidLiveActor* WrapFieldLive = nullptr;
		ATDFluidCloudLayersActor* CloudActor = nullptr;
	};

	bool BuildSourceStage(FAutomationTestBase& Test, FTDCloudSourceTestWorld& TestWorld, FTDCloudSourceStage& OutStage)
	{
		for (const TCHAR* MaterialPath : { CycloneLiveMaterialPath, StormWallMaterialPath, WrapFieldMaterialPath })
		{
			if (!LoadObject<UMaterialInterface>(nullptr, MaterialPath))
			{
				Test.AddError(FString::Printf(TEXT("%s is missing (run editor_make_fluid_cloud_material.py)"), MaterialPath));
				return false;
			}
		}

		OutStage.CyclonePreset.Reset(LoadObject<UTDFluidPresetAsset>(nullptr, CyclonePresetPath));
		OutStage.StormWallPreset.Reset(LoadObject<UTDFluidPresetAsset>(nullptr, StormWallPresetPath));
		OutStage.WrapFieldPreset.Reset(LoadObject<UTDFluidPresetAsset>(nullptr, WrapFieldPresetPath));
		if (!OutStage.CyclonePreset || !OutStage.StormWallPreset || !OutStage.WrapFieldPreset)
		{
			Test.AddError(TEXT("cloud source presets are missing (run editor_make_fluid_cloud_stage.py)"));
			return false;
		}

		OutStage.CycloneLive = SpawnLive(Test, *TestWorld.World, OutStage.CyclonePreset.Get(), FVector(0.0, 0.0, -20000.0), CycloneTag);
		OutStage.StormWallLive = SpawnLive(Test, *TestWorld.World, OutStage.StormWallPreset.Get(), FVector(3000.0, 0.0, -20000.0), StormWallTag);
		OutStage.WrapFieldLive = SpawnLive(Test, *TestWorld.World, OutStage.WrapFieldPreset.Get(), FVector(6000.0, 0.0, -20000.0), WrapFieldTag);
		if (!OutStage.CycloneLive || !OutStage.StormWallLive || !OutStage.WrapFieldLive)
		{
			return false;
		}

		ATDFluidCloudLayersActor* CloudActor = TestWorld.World->SpawnActorDeferred<ATDFluidCloudLayersActor>(ATDFluidCloudLayersActor::StaticClass(), FTransform::Identity);
		if (!Test.TestNotNull(TEXT("spawned the cloud layers actor"), CloudActor))
		{
			return false;
		}

		CloudActor->Setups = {
			MakeSourceSetup(TEXT("CycloneLive"), CycloneLiveMaterialPath, CycloneTag, ETDFluidCloudSlotTextureRole::Coverage),
			MakeSourceSetup(TEXT("StormWall"), StormWallMaterialPath, StormWallTag, ETDFluidCloudSlotTextureRole::Section),
			MakeSourceSetup(TEXT("WrapField"), WrapFieldMaterialPath, WrapFieldTag, ETDFluidCloudSlotTextureRole::Coverage),
		};
		CloudActor->DefaultSetupIndex = 0;
		CloudActor->FinishSpawning(FTransform::Identity);
		OutStage.CloudActor = CloudActor;
		for (int32 StepIndex = 0; StepIndex < MaxLoadStepCount && !CloudActor->GetActiveMaterial(); ++StepIndex)
		{
			TestWorld.StepWorld(PaintStepSeconds);
		}
		return Test.TestNotNull(TEXT("the cloud source material activated"), CloudActor->GetActiveMaterial());
	}

	void ActivateSourceSetup(FTDCloudSourceTestWorld& TestWorld, IConsoleVariable& SetupVariable, int32 SetupIndex)
	{
		SetupVariable.Set(SetupIndex, ECVF_SetByCode);
		for (int32 StepIndex = 0; StepIndex < SettleStepCount; ++StepIndex)
		{
			TestWorld.StepWorld(PaintStepSeconds);
		}
	}

	void WriteReportJson(const FString& Path, const TSharedRef<FJsonObject>& Report)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Report, Writer);
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	double MedianOf(TArray<double> Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}

		Values.Sort();
		return Values[Values.Num() / 2];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudLayersWrapMismatchRuleTest,
	"TDGame.FluidFX.CloudLayers.WrapMismatchRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudLayersWrapMismatchRuleTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("simulated coverage tiling a clamped source warns"), ATDFluidCloudLayersActor::ShouldWarnWrapMismatch(1.0f, 0.0f, false));
	TestFalse(TEXT("simulated coverage tiling a wrapped source is quiet"), ATDFluidCloudLayersActor::ShouldWarnWrapMismatch(1.0f, 0.0f, true));
	TestFalse(TEXT("a finite footprint does not tile"), ATDFluidCloudLayersActor::ShouldWarnWrapMismatch(1.0f, 20.0f, false));
	TestFalse(TEXT("source 2 is not simulated coverage"), ATDFluidCloudLayersActor::ShouldWarnWrapMismatch(2.0f, 0.0f, false));
	TestFalse(TEXT("an inactive slot is quiet"), ATDFluidCloudLayersActor::ShouldWarnWrapMismatch(0.0f, 0.0f, false));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourcePainterRayToAltitudeTest,
	"TDGame.FluidFX.CloudSource.PainterRayToAltitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourcePainterRayToAltitudeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	FVector Hit;
	TestTrue(TEXT("a downward ray reaches the layer"), ATDFluidCloudPainterActor::IntersectRayWithAltitude(FVector(0.0, 0.0, 500000.0), FVector(0.0, 0.0, -1.0), 150000.0, Hit));
	TestTrue(*FString::Printf(TEXT("the downward hit (%.3f, %.3f, %.3f) is (0, 0, 150000)"), Hit.X, Hit.Y, Hit.Z), Hit.Equals(FVector(0.0, 0.0, 150000.0), PositionToleranceCm));
	TestTrue(TEXT("an upward ray from the ground reaches the layer"), ATDFluidCloudPainterActor::IntersectRayWithAltitude(FVector::ZeroVector, FVector(0.0, 0.0, 1.0), 150000.0, Hit));
	TestFalse(TEXT("a horizontal ray misses"), ATDFluidCloudPainterActor::IntersectRayWithAltitude(FVector(0.0, 0.0, 500000.0), FVector(1.0, 0.0, 0.0), 150000.0, Hit));
	TestFalse(TEXT("a ray pointing away misses"), ATDFluidCloudPainterActor::IntersectRayWithAltitude(FVector(0.0, 0.0, 500000.0), FVector(0.0, 0.0, 1.0), 150000.0, Hit));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourcePainterDisplayToSimulationTest,
	"TDGame.FluidFX.CloudSource.PainterDisplayToSimulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourcePainterDisplayToSimulationTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	const FTDFluidCloudPaintFootprint Footprint = MakeFootprint(FVector2D::ZeroVector, FVector2D(20.0, 20.0), 20.0f);
	TestDisplayMapping(*this, TEXT("footprint center"), FVector2D::ZeroVector, Footprint, FVector2D(0.5, 0.5), FVector2D(100.0, -50.0));
	TestDisplayMapping(*this, TEXT("footprint (5, -5) km"), FVector2D(5.0, -5.0), Footprint, FVector2D(0.75, 0.25), FVector2D(850.0, -800.0));

	FVector2D PlanePoint;
	FVector2D Uv;
	TestFalse(TEXT("(11, 0) km is outside the 20 km footprint"), ATDFluidCloudPainterActor::MapDisplayPointToSimulation(FVector2D(11.0, 0.0) * CentimetersPerKilometer, Footprint, FVector2D(100.0, -50.0), FVector2D(3000.0, 3000.0), PlanePoint, Uv));

	const FTDFluidCloudPaintFootprint TilingFootprint = MakeFootprint(FVector2D::ZeroVector, FVector2D::ZeroVector, 20.0f);
	TestDisplayMapping(*this, TEXT("tiling (25, -5) km"), FVector2D(25.0, -5.0), TilingFootprint, FVector2D(0.25, 0.75), FVector2D(-650.0, 700.0));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourcePainterScaleAndRadiusTest,
	"TDGame.FluidFX.CloudSource.PainterScaleAndRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourcePainterScaleAndRadiusTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	const FVector2D SimExtentCm(3000.0, 3000.0);
	const double FootprintScale = ATDFluidCloudPainterActor::ComputeDisplayToSimulationScale(MakeFootprint(FVector2D::ZeroVector, FVector2D(20.0, 20.0), 5.0f), SimExtentCm);
	const double TilingScale = ATDFluidCloudPainterActor::ComputeDisplayToSimulationScale(MakeFootprint(FVector2D::ZeroVector, FVector2D::ZeroVector, 20.0f), SimExtentCm);
	TestTrue(*FString::Printf(TEXT("footprint scale %.8f is 0.0015"), FootprintScale), FMath::IsNearlyEqual(FootprintScale, 0.0015, 1e-9));
	TestTrue(*FString::Printf(TEXT("tiling scale %.8f matches the footprint scale"), TilingScale), FMath::IsNearlyEqual(TilingScale, FootprintScale, 1e-9));

	const float RadiusCm = ATDFluidCloudPainterActor::ComputeSimulationBrushRadiusCm(0.5f, FootprintScale);
	TestTrue(*FString::Printf(TEXT("a 0.5 km brush is %.4f cm, expected 75"), RadiusCm), FMath::IsNearlyEqual(RadiusCm, 75.0f, 1e-3f));
	TestEqual(TEXT("the brush radius never drops below 1 cm"), ATDFluidCloudPainterActor::ComputeSimulationBrushRadiusCm(0.01f, 1e-6), 1.0f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourcePainterSeamCrossingTest,
	"TDGame.FluidFX.CloudSource.PainterSeamCrossing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourcePainterSeamCrossingTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("0.95 to 0.05 crosses the seam"), ATDFluidCloudPainterActor::IsSeamCrossing(FVector2D(0.95, 0.5), FVector2D(0.05, 0.5)));
	TestTrue(TEXT("a vertical wrap crosses the seam"), ATDFluidCloudPainterActor::IsSeamCrossing(FVector2D(0.5, 0.02), FVector2D(0.5, 0.97)));
	TestFalse(TEXT("0.4 to 0.6 stays inside"), ATDFluidCloudPainterActor::IsSeamCrossing(FVector2D(0.4, 0.5), FVector2D(0.6, 0.5)));

	for (const double SegmentCm : { 0.0, 20.0, 75.0, 400.0, 5000.0 })
	{
		const int32 StrokeCount = ATDFluidLiveActor::ComputeStrokeStampCount(SegmentCm, 75.0f, 0.5f);
		const int32 PaintCount = ATDFluidCloudPainterActor::ComputePaintStampCount(SegmentCm, 75.0f, 0.5f, 64, false);
		TestEqual(*FString::Printf(TEXT("segment %.0f cm uses the live stroke count"), SegmentCm), PaintCount, StrokeCount);
		TestTrue(*FString::Printf(TEXT("segment %.0f cm stays within 16 stamps"), SegmentCm), PaintCount >= 1 && PaintCount <= 16);
	}
	TestEqual(TEXT("the per-tick cap limits the stamp count"), ATDFluidCloudPainterActor::ComputePaintStampCount(5000.0, 75.0f, 0.5f, 4, false), 4);
	TestEqual(TEXT("a seam crossing uses one stamp"), ATDFluidCloudPainterActor::ComputePaintStampCount(5000.0, 75.0f, 0.5f, 64, true), 1);
	TestTrue(TEXT("console -1 keeps the actor value"), ATDFluidCloudPainterActor::ResolvePainterEnabled(-1, true) && !ATDFluidCloudPainterActor::ResolvePainterEnabled(-1, false));
	TestTrue(TEXT("console 0 and 1 override the actor"), !ATDFluidCloudPainterActor::ResolvePainterEnabled(0, true) && ATDFluidCloudPainterActor::ResolvePainterEnabled(1, false));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourcePainterPaintsAndErasesTest,
	"TDGame.FluidFX.CloudSource.PainterPaintsAndErases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourcePainterPaintsAndErasesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	FTDCloudSourceTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnPaintLive(*this, TestWorld, ETDFluidSolverBackend::Cpu);
	ATDFluidCloudPainterActor* Painter = SpawnDetachedPainter(*TestWorld.World);
	if (!LiveActor || !TestNotNull(TEXT("spawned the cloud painter"), Painter))
	{
		return false;
	}

	UTDFluidLiveComponent& LiveComponent = *LiveActor->LiveComponent;
	TestFalse(TEXT("the paint live runs on the CPU solver"), LiveComponent.IsUsingGpuSolver());
	const FVector2D CenterPlaneCm = LiveComponent.GetSimulationCenterWorld();
	const FTDFluidCloudPaintFootprint Footprint = MakeFootprint(FVector2D::ZeroVector, FVector2D(20.0, 20.0), 20.0f);
	const TArray<FVector2D> PathProbes = { CenterPlaneCm + FVector2D(-150.0, 0.0), CenterPlaneCm, CenterPlaneCm + FVector2D(150.0, 0.0) };
	const TArray<FVector2D> FarProbes = { CenterPlaneCm + FVector2D(0.0, 450.0), CenterPlaneCm + FVector2D(0.0, -450.0) };

	RunScriptedPaintPass(TestWorld, *Painter, *LiveActor, Footprint, false);
	TArray<float> PaintedDensities;
	for (const FVector2D& Probe : PathProbes)
	{
		const float Density = SamplePlaneDensity(LiveComponent, Probe);
		PaintedDensities.Add(Density);
		TestTrue(*FString::Printf(TEXT("path probe (%.0f, %.0f) density %.4f is at least 0.5"), Probe.X, Probe.Y, Density), Density >= 0.5f);
	}
	for (const FVector2D& Probe : FarProbes)
	{
		const float Density = SamplePlaneDensity(LiveComponent, Probe);
		TestTrue(*FString::Printf(TEXT("probe 3 km off the path (%.0f, %.0f) density %.4f stays below 0.05"), Probe.X, Probe.Y, Density), Density >= 0.0f && Density < 0.05f);
	}

	RunScriptedPaintPass(TestWorld, *Painter, *LiveActor, Footprint, true);
	for (int32 ProbeIndex = 0; ProbeIndex < PathProbes.Num(); ++ProbeIndex)
	{
		const float Density = SamplePlaneDensity(LiveComponent, PathProbes[ProbeIndex]);
		TestTrue(*FString::Printf(TEXT("erased probe %d density %.4f is at most 0.6 x painted %.4f"), ProbeIndex, Density, PaintedDensities[ProbeIndex]), Density >= 0.0f && Density <= 0.6f * PaintedDensities[ProbeIndex]);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourceSimulationClockRestartsTest,
	"TDGame.FluidFX.CloudSource.SimulationClockRestarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourceSimulationClockRestartsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	FTDCloudSourceTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnPaintLive(*this, TestWorld, ETDFluidSolverBackend::Cpu);
	if (!LiveActor)
	{
		return false;
	}

	UTDFluidLiveComponent& LiveComponent = *LiveActor->LiveComponent;
	TestFalse(TEXT("the paint live runs on the CPU solver"), LiveComponent.IsUsingGpuSolver());
	const double StartSeconds = LiveComponent.GetSimulationTimeSeconds();
	for (int32 StepIndex = 0; StepIndex < SimulationClockStepCount; ++StepIndex)
	{
		TestWorld.StepWorld(PaintStepSeconds);
	}
	const double SteppedSeconds = LiveComponent.GetSimulationTimeSeconds();
	const double AdvancedSeconds = SteppedSeconds - StartSeconds;
	TestTrue(*FString::Printf(TEXT("%d steps advance the simulation clock by %.6f s"), SimulationClockStepCount, AdvancedSeconds), AdvancedSeconds > 0.0);
	TestTrue(*FString::Printf(TEXT("the simulation clock advance %.6f s stays within %d world steps"), AdvancedSeconds, SimulationClockStepCount), AdvancedSeconds <= SimulationClockStepCount * static_cast<double>(PaintStepSeconds) + SimulationClockToleranceSeconds);
	TestEqual(TEXT("the component clock is the CPU solver clock"), SteppedSeconds, LiveComponent.GetSolver().GetSimulationTimeSeconds());

	LiveComponent.ResetSimulationState();
	TestEqual(TEXT("ResetSimulationState puts the simulation clock at 0"), LiveComponent.GetSimulationTimeSeconds(), 0.0);
	for (int32 StepIndex = 0; StepIndex < SimulationClockRestartStepCount; ++StepIndex)
	{
		TestWorld.StepWorld(PaintStepSeconds);
	}
	TestTrue(*FString::Printf(TEXT("the simulation clock runs again after the reset (%.6f s)"), LiveComponent.GetSimulationTimeSeconds()), LiveComponent.GetSimulationTimeSeconds() > 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourceWindCurtainStampsTest,
	"TDGame.FluidFX.CloudSource.WindCurtainStamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourceWindCurtainStampsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	FTDFluidCloudSceneSettings Settings;
	const FVector2D SimExtentCm(4000.0, 4000.0);
	TArray<FTDFluidCloudWindCurtainStamp> Stamps;
	ATDFluidCloudWindCurtainActor::BuildWindCurtainStamps(Settings, FVector2D::ZeroVector, SimExtentCm, 0.0, 1.0f, Stamps);
	if (!TestEqual(TEXT("the default curtain has 12 stamps"), Stamps.Num(), 12))
	{
		return false;
	}

	TestTrue(*FString::Printf(TEXT("first stamp x %.4f is -1650"), Stamps[0].PlanePointCm.X), FMath::IsNearlyEqual(Stamps[0].PlanePointCm.X, -1650.0, PositionToleranceCm));
	TestTrue(*FString::Printf(TEXT("last stamp x %.4f is 1650"), Stamps.Last().PlanePointCm.X), FMath::IsNearlyEqual(Stamps.Last().PlanePointCm.X, 1650.0, PositionToleranceCm));
	for (const FTDFluidCloudWindCurtainStamp& Stamp : Stamps)
	{
		TestTrue(*FString::Printf(TEXT("stamp y %.4f is -1520"), Stamp.PlanePointCm.Y), FMath::IsNearlyEqual(Stamp.PlanePointCm.Y, -1520.0, PositionToleranceCm));
		TestTrue(*FString::Printf(TEXT("stamp radius %.4f is 200"), Stamp.RadiusCm), FMath::IsNearlyEqual(Stamp.RadiusCm, 200.0f, 1e-3f));
		TestTrue(*FString::Printf(TEXT("stamp speed %.4f is at least 105"), Stamp.PlaneVelocityCmPerSec.X), Stamp.PlaneVelocityCmPerSec.X >= 105.0 - VelocityToleranceCmPerSec);
		TestTrue(*FString::Printf(TEXT("stamp x %.2f stays inside the span"), Stamp.PlanePointCm.X), FMath::Abs(Stamp.PlanePointCm.X) <= 1800.0);
	}

	FTDFluidCloudSceneSettings CalmSettings;
	CalmSettings.WindCurtainGustAmplitude = 0.0f;
	ATDFluidCloudWindCurtainActor::BuildWindCurtainStamps(CalmSettings, FVector2D::ZeroVector, SimExtentCm, 3.7, 1.0f, Stamps);
	for (const FTDFluidCloudWindCurtainStamp& Stamp : Stamps)
	{
		TestTrue(*FString::Printf(TEXT("calm stamp velocity (%.4f, %.4f) is (150, 30)"), Stamp.PlaneVelocityCmPerSec.X, Stamp.PlaneVelocityCmPerSec.Y), IsNear(Stamp.PlaneVelocityCmPerSec, FVector2D(150.0, 30.0), VelocityToleranceCmPerSec));
	}

	TArray<FTDFluidCloudWindCurtainStamp> LaterStamps;
	ATDFluidCloudWindCurtainActor::BuildWindCurtainStamps(Settings, FVector2D::ZeroVector, SimExtentCm, 2.5, 1.0f, Stamps);
	ATDFluidCloudWindCurtainActor::BuildWindCurtainStamps(Settings, FVector2D::ZeroVector, SimExtentCm, 2.5 + Settings.WindCurtainGustPeriodSeconds, 1.0f, LaterStamps);
	for (int32 StampIndex = 0; StampIndex < Stamps.Num() && StampIndex < LaterStamps.Num(); ++StampIndex)
	{
		TestTrue(*FString::Printf(TEXT("stamp %d repeats after one gust period"), StampIndex), IsNear(Stamps[StampIndex].PlaneVelocityCmPerSec, LaterStamps[StampIndex].PlaneVelocityCmPerSec, VelocityToleranceCmPerSec));
	}

	ATDFluidCloudWindCurtainActor::BuildWindCurtainStamps(Settings, FVector2D::ZeroVector, SimExtentCm, 0.0, std::numeric_limits<float>::quiet_NaN(), Stamps);
	TestTrue(TEXT("a non-finite brush density becomes 0"), !Stamps.IsEmpty() && Stamps[0].DensityStrength == 0.0f);

	FTDFluidCloudWindCurtainStamp EyeSinkStamp;
	TestTrue(TEXT("the default eye sink is active"), ATDFluidCloudWindCurtainActor::BuildEyeSinkStamp(Settings, FVector2D(10.0, -20.0), FVector2D(6000.0, 5000.0), EyeSinkStamp));
	TestTrue(TEXT("the eye sink sits at the window center"), IsNear(EyeSinkStamp.PlanePointCm, FVector2D(10.0, -20.0), PositionToleranceCm));
	TestTrue(TEXT("the eye sink radius follows the smaller extent"), FMath::IsNearlyEqual(EyeSinkStamp.RadiusCm, Settings.CycloneEyeRadiusRatio * 5000.0f, 1e-2f));
	TestTrue(TEXT("the eye sink erases"), EyeSinkStamp.DensityStrength < 0.0f && EyeSinkStamp.PlaneVelocityCmPerSec.IsZero());
	TestTrue(TEXT("console -1 keeps the curtain actor value"), ATDFluidCloudWindCurtainActor::ResolveWindCurtainEnabled(-1, true) && !ATDFluidCloudWindCurtainActor::ResolveWindCurtainEnabled(0, true));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourceCycloneEyeFormsTest,
	"TDGame.FluidFX.CloudSource.CycloneEyeForms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourceCycloneEyeFormsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	const FTDCycloneResult DriftOnly = RunCyclone(false);
	AddInfo(FString::Printf(TEXT("TDFluidCloudCyclone radial drift only: eye %.4f ring %.4f ratio %.4f tangential %.4f"), DriftOnly.EyeMeanDensity, DriftOnly.RingMeanDensity, DriftOnly.EyeMeanDensity / FMath::Max(DriftOnly.RingMeanDensity, UE_DOUBLE_SMALL_NUMBER), DriftOnly.RingMeanTangentialCmPerSec));

	const FTDCycloneResult First = RunCyclone(true);
	const FTDCycloneResult Second = RunCyclone(true);
	AddInfo(FString::Printf(TEXT("TDFluidCloudCyclone eye sink: eye %.4f ring %.4f tangential %.4f checksum %u"), First.EyeMeanDensity, First.RingMeanDensity, First.RingMeanTangentialCmPerSec, First.StateChecksum));
	TestTrue(*FString::Printf(TEXT("eye mean %.4f is at most half the ring mean %.4f"), First.EyeMeanDensity, First.RingMeanDensity), First.RingMeanDensity > 0.0 && First.EyeMeanDensity <= 0.5 * First.RingMeanDensity);
	TestTrue(*FString::Printf(TEXT("ring tangential mean %.4f turns counterclockwise"), First.RingMeanTangentialCmPerSec), First.RingMeanTangentialCmPerSec > 0.0);
	TestEqual(TEXT("the cyclone checksum repeats"), Second.StateChecksum, First.StateChecksum);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourceStormWallRisesAndCurlsTest,
	"TDGame.FluidFX.CloudSource.StormWallRisesAndCurls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourceStormWallRisesAndCurlsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	const FTDFluidSimulationConfig Config = MakeCpuConfig(ETDFluidPresetKind::CloudStormWall);
	const FTDStormWallResult First = RunStormWall();
	const FTDStormWallResult Second = RunStormWall();
	AddInfo(FString::Printf(TEXT("TDFluidCloudStormWall centroid %.2f curtain %.2f top edge std %.3f cells (recorded only) checksum %u"), First.CentroidVCm, First.CurtainVCm, First.TopEdgeStdDevCells, First.StateChecksum));
	const double TargetCentroidCm = First.CurtainVCm + 0.15 * Config.SimExtentCm.Y;
	AddInfo(FString::Printf(TEXT("TDFluidCloudStormWall rise %.2f cm, calibration target centroid %.2f reached=%d (recorded only)"), First.CentroidVCm - First.CurtainVCm, TargetCentroidCm, First.CentroidVCm > TargetCentroidCm ? 1 : 0));
	TestTrue(*FString::Printf(TEXT("density centroid %.2f rises above the curtain row %.2f"), First.CentroidVCm, First.CurtainVCm), First.CentroidVCm > First.CurtainVCm);
	TestEqual(TEXT("the storm wall checksum repeats"), Second.StateChecksum, First.StateChecksum);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourceRhiPainterTexelRoundTripTest,
	"TDGame.FluidFXRhi.CloudSource.PainterTexelRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourceRhiPainterTexelRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	if (!CanRunCloudSourceRhiTest(*this, TEXT("TDGame.FluidFXRhi.CloudSource.PainterTexelRoundTrip")))
	{
		return false;
	}

	FTDCloudSourceTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnPaintLive(*this, TestWorld, ETDFluidSolverBackend::Gpu);
	ATDFluidCloudPainterActor* Painter = SpawnDetachedPainter(*TestWorld.World);
	if (!LiveActor || !TestNotNull(TEXT("spawned the cloud painter"), Painter))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("TDFluidCloudPainterRoundTrip gpu_solver=%d"), LiveActor->LiveComponent->IsUsingGpuSolver() ? 1 : 0));
	const FTDFluidCloudPaintFootprint Footprint = MakeFootprint(FVector2D::ZeroVector, FVector2D(20.0, 20.0), 20.0f);
	const FVector2D PaintUv(0.25, 0.75);
	const FVector DisplayPointCm(FVector2D(PaintUv - FVector2D(0.5, 0.5)) * Footprint.SizeKm * CentimetersPerKilometer, Footprint.LayerAltitudeCm);
	for (int32 FrameIndex = 0; FrameIndex < RoundTripFrameCount; ++FrameIndex)
	{
		Painter->PaintAtDisplayPoint(*LiveActor, Footprint, DisplayPointCm, false, PaintStepSeconds);
		TestWorld.StepWorld(PaintStepSeconds);
	}
	Painter->EndStroke();
	FlushRenderingCommands();

	TArray<FFloat16Color> Pixels;
	FIntPoint Size = FIntPoint::ZeroValue;
	if (!TestTrue(TEXT("read back the density render target"), ReadRenderTargetPixels(LiveActor->LiveComponent->GetDensityPressureTexture(), Pixels, Size)))
	{
		return false;
	}

	const float PaintedRed = ReadTexelRed(Pixels, Size, PaintUv);
	const float FlippedRed = ReadTexelRed(Pixels, Size, FVector2D(PaintUv.X, 1.0 - PaintUv.Y));
	TestTrue(*FString::Printf(TEXT("painted texel (0.25, 0.75) red %.4f is at least 0.4"), PaintedRed), PaintedRed >= 0.4f);
	TestTrue(*FString::Printf(TEXT("flipped texel (0.25, 0.25) red %.4f stays below 0.05"), FlippedRed), FlippedRed < 0.05f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourceAssetRhiLiveSlotsBindTest,
	"TDGame.FluidFXRhi.CloudSourceAsset.LiveSlotsBind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourceAssetRhiLiveSlotsBindTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	if (!CanRunCloudSourceRhiTest(*this, TEXT("TDGame.FluidFXRhi.CloudSourceAsset.LiveSlotsBind")))
	{
		return false;
	}

	FTDCloudSourceConsoleRestore SetupRestore(SetupConsoleName);
	if (!TestNotNull(TEXT("TD.FluidFX.CloudSetup exists"), SetupRestore.Variable))
	{
		return false;
	}
	SetupRestore.Variable->Set(-1, ECVF_SetByCode);

	FTDCloudSourceTestWorld TestWorld;
	FTDCloudSourceStage Stage;
	if (!BuildSourceStage(*this, TestWorld, Stage))
	{
		return false;
	}

	ActivateSourceSetup(TestWorld, *SetupRestore.Variable, 0);
	UMaterialInstanceDynamic* CycloneMaterial = Stage.CloudActor->GetActiveMaterial();
	TestTrue(TEXT("CycloneLive coverage is the cyclone live texture"), CycloneMaterial && CycloneMaterial->K2_GetTextureParameterValue(FName(TEXT("CloudSlot1CoverageTexture"))) == Stage.CycloneLive->LiveComponent->GetDensityPressureTexture());
	TestTrue(TEXT("only the cyclone source wakes in CycloneLive"), Stage.CycloneLive->LiveComponent->IsSimulationAwake() && !Stage.StormWallLive->LiveComponent->IsSimulationAwake() && !Stage.WrapFieldLive->LiveComponent->IsSimulationAwake());

	ActivateSourceSetup(TestWorld, *SetupRestore.Variable, 1);
	UMaterialInstanceDynamic* StormWallMaterial = Stage.CloudActor->GetActiveMaterial();
	TestTrue(TEXT("StormWall section is the wall live texture"), StormWallMaterial && StormWallMaterial->K2_GetTextureParameterValue(FName(TEXT("CloudSlot1SectionTexture"))) == Stage.StormWallLive->LiveComponent->GetDensityPressureTexture());
	TestTrue(TEXT("the storm wall plane is not horizontal"), FMath::Abs(Stage.StormWallLive->LiveComponent->GetPlaneBasis().N.Z) < 0.5);
	TestTrue(TEXT("only the storm wall source wakes in StormWall"), Stage.StormWallLive->LiveComponent->IsSimulationAwake() && !Stage.CycloneLive->LiveComponent->IsSimulationAwake());

	ActivateSourceSetup(TestWorld, *SetupRestore.Variable, 2);
	UMaterialInstanceDynamic* WrapMaterial = Stage.CloudActor->GetActiveMaterial();
	TestTrue(TEXT("WrapField coverage is the wrap live texture"), WrapMaterial && WrapMaterial->K2_GetTextureParameterValue(FName(TEXT("CloudSlot1CoverageTexture"))) == Stage.WrapFieldLive->LiveComponent->GetDensityPressureTexture());
	TestTrue(TEXT("the wrap field source is wrapped"), Stage.WrapFieldLive->LiveComponent->IsAreaWrapped());
	if (WrapMaterial)
	{
		const float Source = WrapMaterial->K2_GetScalarParameterValue(FName(TEXT("CloudSlot1Source")));
		const FLinearColor FootprintSizeKm = WrapMaterial->K2_GetVectorParameterValue(FName(TEXT("CloudSlot1FootprintSizeKm")));
		TestFalse(TEXT("the wrap field slot raises no wrap warning"), ATDFluidCloudLayersActor::ShouldWarnWrapMismatch(Source, FootprintSizeKm.R, Stage.WrapFieldLive->LiveComponent->IsAreaWrapped()));
	}
	TestTrue(TEXT("only the wrap source wakes in WrapField"), Stage.WrapFieldLive->LiveComponent->IsSimulationAwake() && !Stage.StormWallLive->LiveComponent->IsSimulationAwake());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudSourceAssetRhiOffscreenCostTest,
	"TDGame.FluidFXRhi.CloudSourceAsset.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudSourceAssetRhiOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudSourceTestPrivate;

	if (!CanRunCloudSourceRhiTest(*this, TEXT("TDGame.FluidFXRhi.CloudSourceAsset.OffscreenCost")))
	{
		return false;
	}

	FTDCloudSourceConsoleRestore SetupRestore(SetupConsoleName);
	if (!TestNotNull(TEXT("TD.FluidFX.CloudSetup exists"), SetupRestore.Variable))
	{
		return false;
	}
	SetupRestore.Variable->Set(-1, ECVF_SetByCode);

	FTDCloudSourceTestWorld TestWorld;
	AActor* RigActor = TestWorld.World->SpawnActor<AActor>();
	USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(RigActor);
	Atmosphere->RegisterComponent();
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(RigActor);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetWorldRotation(FRotator(-25.0, 30.0, 0.0));
	Sun->RegisterComponent();
	UTextureRenderTarget2D* CaptureTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	CaptureTarget->InitAutoFormat(640, 360);
	CaptureTarget->UpdateResourceImmediate(true);
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(RigActor);
	Capture->TextureTarget = CaptureTarget;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->SetWorldLocationAndRotation(FVector(0.0, 0.0, 200.0), FRotator(15.0, 0.0, 0.0));
	Capture->RegisterComponent();

	FTDCloudSourceStage Stage;
	if (!BuildSourceStage(*this, TestWorld, Stage))
	{
		return false;
	}

	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Cases;
	const TArray<ATDFluidLiveActor*> SetupSources = { Stage.CycloneLive, Stage.StormWallLive, Stage.WrapFieldLive };
	const TArray<FString> SetupNames = { TEXT("CycloneLive"), TEXT("StormWall"), TEXT("WrapField") };
	for (int32 SetupIndex = 0; SetupIndex < SetupSources.Num(); ++SetupIndex)
	{
		ActivateSourceSetup(TestWorld, *SetupRestore.Variable, SetupIndex);
		for (const bool bIsAwake : { true, false })
		{
			SetupSources[SetupIndex]->LiveComponent->SetSimulationAwake(bIsAwake);
			TArray<double> Milliseconds;
			for (int32 CaptureIndex = 0; CaptureIndex < CaptureWarmupCount + CaptureMeasuredCount; ++CaptureIndex)
			{
				const double StartSeconds = FPlatformTime::Seconds();
				TestWorld.StepWorld(PaintStepSeconds);
				Capture->CaptureScene();
				ENQUEUE_RENDER_COMMAND(TDFluidCloudSourceCaptureWait)([](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.SubmitAndBlockUntilGPUIdle();
				});
				FlushRenderingCommands();
				if (CaptureIndex >= CaptureWarmupCount)
				{
					Milliseconds.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
				}
			}

			const FString CaseName = FString::Printf(TEXT("%s_%s"), *SetupNames[SetupIndex], bIsAwake ? TEXT("awake") : TEXT("asleep"));
			const double MedianMilliseconds = MedianOf(Milliseconds);
			const TSharedRef<FJsonObject> CaseObject = MakeShared<FJsonObject>();
			CaseObject->SetStringField(TEXT("case"), CaseName);
			CaseObject->SetNumberField(TEXT("samples"), Milliseconds.Num());
			CaseObject->SetNumberField(TEXT("median_frame_ms"), MedianMilliseconds);
			Cases.Add(MakeShared<FJsonValueObject>(CaseObject));
			AddInfo(FString::Printf(TEXT("TDFluidCloudSourceCost: %s median_ms=%.4f samples=%d"), *CaseName, MedianMilliseconds, Milliseconds.Num()));
		}
		SetupSources[SetupIndex]->LiveComponent->SetSimulationAwake(true);
	}

	Report->SetArrayField(TEXT("cases"), Cases);
	Report->SetStringField(TEXT("note"), TEXT("Wall-clock world step plus capture plus GPU idle, recorded only."));
	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("fx11c"), TEXT("cloud_source_cost.json"));
	WriteReportJson(ReportPath, Report);
	AddInfo(FString::Printf(TEXT("TDFluidCloudSourceCost report: %s"), *ReportPath));
	return !HasAnyErrors();
}

#endif
