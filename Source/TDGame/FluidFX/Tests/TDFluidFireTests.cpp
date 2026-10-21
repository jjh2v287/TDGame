#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidBandLimitedNoise.h"
#include "FluidFX/TDFluidEmberComponent.h"
#include "FluidFX/TDFluidFireActor.h"
#include "FluidFX/TDFluidFireLightComponent.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidFireCalibration, Display, All);

namespace TDFluidFireTestPrivate
{
	constexpr float FixedStepSeconds = 1.0f / 60.0f;
	constexpr int32 FramesPerSecond = 60;
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	struct FTDFluidFireTestWorld
	{
		FTDFluidFireTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidFireTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidFireTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		void TickFrames(int32 FrameCount)
		{
			for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, FixedStepSeconds);
			}
		}

		UWorld* World = nullptr;
	};

	void ConfigureFlamePlane(UTDFluidPresetAsset& Preset, float BiasCm, float InteractionDepthCm, float CenterOffsetRatio)
	{
		Preset.PlaneSettings.Orientation = ETDFluidPlaneOrientation::CameraFacing;
		Preset.PlaneSettings.CameraDepthBiasCm = BiasCm;
		Preset.PlaneSettings.InteractionDepthCm = InteractionDepthCm;
		Preset.PlaneSettings.CenterOffsetAlongUpRatio = CenterOffsetRatio;
	}

	struct FTDFluidTorchTuning
	{
		float FlowNoiseAcceleration = 400.0f;
		float EmitSpeed = 450.0f;
		float Dissipation = 1.0f;
		float EmitRadius = 11.0f;
	};

	UTDFluidPresetAsset* MakeTorchPreset(UObject* Outer, float BuoyancyCmPerSec2, const FTDFluidTorchTuning& Tuning = FTDFluidTorchTuning())
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, ETDFluidPresetKind::Custom);
		FTDFluidSimulationConfig& Config = Preset->SimulationConfig;
		Config.GridResolution = 96;
		Config.SimExtentCm = FVector2D(120.0, 160.0);
		Config.DensityDissipation = Tuning.Dissipation;
		Config.VorticityStrength = 3.0f;
		Config.PressureIterations = 10;
		Config.BuoyancyCmPerSec2 = BuoyancyCmPerSec2;
		Config.OutflowBandRatio = 0.15f;
		Config.OutflowRatePerSec = 6.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = Tuning.FlowNoiseAcceleration;
		Config.FlowNoiseScaleCm = 14.0f;
		Config.FlowNoiseSpeed = 1.5f;
		Config.BorderFadeRatio = 0.08f;
		ConfigureFlamePlane(*Preset, 35.0f, 150.0f, 0.3f);
		return Preset;
	}

	UTDFluidPresetAsset* MakeCampfireFlamePreset(UObject* Outer)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, ETDFluidPresetKind::Custom);
		FTDFluidSimulationConfig& Config = Preset->SimulationConfig;
		Config.GridResolution = 112;
		Config.SimExtentCm = FVector2D(180.0, 240.0);
		Config.DensityDissipation = 1.2f;
		Config.VorticityStrength = 2.0f;
		Config.BuoyancyCmPerSec2 = 1000.0f;
		Config.OutflowBandRatio = 0.15f;
		Config.OutflowRatePerSec = 6.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 700.0f;
		Config.FlowNoiseScaleCm = 35.0f;
		Config.FlowNoiseSpeed = 1.5f;
		Config.BorderFadeRatio = 0.08f;
		ConfigureFlamePlane(*Preset, 40.0f, 150.0f, 0.3f);
		return Preset;
	}

	UTDFluidPresetAsset* MakeCampfireSmokePreset(UObject* Outer)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, ETDFluidPresetKind::Custom);
		FTDFluidSimulationConfig& Config = Preset->SimulationConfig;
		Config.GridResolution = 96;
		Config.SimExtentCm = FVector2D(360.0, 480.0);
		Config.DensityDissipation = 0.25f;
		Config.VorticityStrength = 2.0f;
		Config.BuoyancyCmPerSec2 = 180.0f;
		Config.OutflowBandRatio = 0.2f;
		Config.OutflowRatePerSec = 3.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 300.0f;
		Config.FlowNoiseScaleCm = 60.0f;
		Config.FlowNoiseSpeed = 0.5f;
		ConfigureFlamePlane(*Preset, 0.0f, 200.0f, 0.4f);
		return Preset;
	}

	UTDFluidPresetAsset* MakeExplosionFirePreset(UObject* Outer)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, ETDFluidPresetKind::Custom);
		FTDFluidSimulationConfig& Config = Preset->SimulationConfig;
		Config.GridResolution = 128;
		Config.SimExtentCm = FVector2D(700.0, 700.0);
		Config.DensityDissipation = 1.3f;
		Config.VorticityStrength = 5.0f;
		Config.BuoyancyCmPerSec2 = 800.0f;
		Config.OutflowBandRatio = 0.15f;
		Config.OutflowRatePerSec = 6.0f;
		Config.MaxPointsPerTick = 256;
		ConfigureFlamePlane(*Preset, 60.0f, 400.0f, 0.15f);
		return Preset;
	}

	FTDFluidSourceEmitterSettings MakeTorchEmitter(const FTDFluidTorchTuning& Tuning = FTDFluidTorchTuning())
	{
		FTDFluidSourceEmitterSettings Emitter;
		Emitter.bIsEnabled = true;
		Emitter.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		Emitter.RadiusCm = Tuning.EmitRadius;
		Emitter.DensityStrength = 0.7f;
		Emitter.EmitSpeedCmPerSec = Tuning.EmitSpeed;
		Emitter.MotionInertiaScale = 0.35f;
		Emitter.FlickerAmplitude = 0.25f;
		Emitter.FlickerFrequencyHz = 5.0f;
		Emitter.SourceStampCount = 1;
		Emitter.TrailLeadSeconds = 0.06f;
		Emitter.MaxTrailLeadRatio = 0.25f;
		Emitter.RandomSeed = 1;
		return Emitter;
	}

	FTDFluidSourceEmitterSettings MakeCampfireEmitter()
	{
		FTDFluidSourceEmitterSettings Emitter = MakeTorchEmitter();
		Emitter.RadiusCm = 20.0f;
		Emitter.DensityStrength = 0.72f;
		Emitter.EmitSpeedCmPerSec = 270.0f;
		Emitter.MotionInertiaScale = 0.3f;
		Emitter.FlickerAmplitude = 0.3f;
		Emitter.FlickerFrequencyHz = 4.0f;
		Emitter.SourceStampCount = 5;
		Emitter.SourceSpreadCm = 45.0f;
		return Emitter;
	}

	ATDFluidFireActor* SpawnFireActor(FTDFluidFireTestWorld& TestWorld, const FVector& Location, UTDFluidPresetAsset* Preset, const FTDFluidSourceEmitterSettings& Emitter)
	{
		ATDFluidFireActor* FireActor = TestWorld.World->SpawnActor<ATDFluidFireActor>(ATDFluidFireActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (FireActor)
		{
			FireActor->SourceEmitter = Emitter;
			FireActor->InitializeFromPreset(Preset, 0.0f);
		}
		return FireActor;
	}

	AActor* SpawnMovableAnchor(UWorld* World, const FVector& Location)
	{
		AActor* Anchor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Anchor)
		{
			return nullptr;
		}

		USceneComponent* AnchorRoot = NewObject<USceneComponent>(Anchor);
		Anchor->AddInstanceComponent(AnchorRoot);
		Anchor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();
		Anchor->SetActorLocation(Location);
		return Anchor;
	}

	FTDFluidPlaneBasis MakeDefaultCameraBasis()
	{
		return FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FTDFluidPlaneBasis::GetDefaultViewRotation(), 0.0f);
	}

	struct FTDFluidFlameMetrics
	{
		bool bHasDensity = false;
		float RiseCm = 0.0f;
		float LagAlongUCm = 0.0f;
		float AreaCm2 = 0.0f;
	};

	enum class ETDFluidTorchMotion : uint8
	{
		Still,
		AlongU,
		AlongV
	};

	FTDFluidFlameMetrics RunTorchScenario(float BuoyancyCmPerSec2, ETDFluidTorchMotion Motion, float SpeedCmPerSec, int32 FrameCount, const FTDFluidTorchTuning& Tuning = FTDFluidTorchTuning())
	{
		FTDFluidFireTestWorld TestWorld;
		UTDFluidPresetAsset* Preset = MakeTorchPreset(GetTransientPackage(), BuoyancyCmPerSec2, Tuning);
		const FTDFluidPlaneBasis Basis = MakeDefaultCameraBasis();
		const FVector StartLocation(0.0, 0.0, 100.0);
		AActor* Anchor = SpawnMovableAnchor(TestWorld.World, StartLocation);
		ATDFluidFireActor* FireActor = SpawnFireActor(TestWorld, StartLocation, Preset, MakeTorchEmitter(Tuning));
		FTDFluidFlameMetrics Metrics;
		if (!Anchor || !FireActor)
		{
			return Metrics;
		}

		const bool bIsMoving = Motion != ETDFluidTorchMotion::Still;
		if (bIsMoving)
		{
			FireActor->bShouldFollowPlayerPawn = true;
			FireActor->FollowTargetActor = Anchor;
		}

		const FVector MoveDirection = Motion == ETDFluidTorchMotion::AlongU ? Basis.U : (Motion == ETDFluidTorchMotion::AlongV ? Basis.V : FVector::ZeroVector);
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Anchor->SetActorLocation(StartLocation + MoveDirection * (SpeedCmPerSec * FixedStepSeconds * static_cast<float>(FrameIndex + 1)));
			TestWorld.TickFrames(1);
		}

		FVector2D Centroid;
		Metrics.bHasDensity = FireActor->LiveComponent->ComputeDensityCentroidPlane(true, Centroid, Metrics.AreaCm2);
		if (!Metrics.bHasDensity)
		{
			return Metrics;
		}

		const FVector2D EmitterPlane = FireActor->LiveComponent->ProjectWorldToSolverPlane(Anchor->GetActorLocation());
		Metrics.RiseCm = static_cast<float>(Centroid.Y - EmitterPlane.Y);
		Metrics.LagAlongUCm = static_cast<float>(Centroid.X - EmitterPlane.X);
		return Metrics;
	}

	float ComputePercentile(TArray<float>& Values, float Quantile)
	{
		if (Values.IsEmpty())
		{
			return 0.0f;
		}

		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::FloorToInt(Quantile * static_cast<float>(Values.Num() - 1)), 0, Values.Num() - 1);
		return Values[Index];
	}

	void CollectSelectiveDensities(const FTDFluidGrid2DSolver& Solver, TArray<float>& InOutDensities)
	{
		const int32 Resolution = Solver.GetResolution();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const float Density = Solver.SampleAtCell(CellX, CellY).SelectiveDensity;
				if (Density > 0.001f)
				{
					InOutDensities.Add(Density);
				}
			}
		}
	}

	int32 CountDensityPeaks(const TArray<float>& Row, float MinPeakDensity, float ValleyRatio)
	{
		TArray<int32> PeakIndices;
		for (int32 Index = 1; Index < Row.Num() - 1; ++Index)
		{
			if (Row[Index] >= MinPeakDensity && Row[Index] > Row[Index - 1] && Row[Index] >= Row[Index + 1])
			{
				PeakIndices.Add(Index);
			}
		}

		if (PeakIndices.IsEmpty())
		{
			return 0;
		}

		int32 PeakCount = 1;
		int32 LastPeak = PeakIndices[0];
		for (int32 PeakSlot = 1; PeakSlot < PeakIndices.Num(); ++PeakSlot)
		{
			const int32 CandidatePeak = PeakIndices[PeakSlot];
			float ValleyDensity = Row[LastPeak];
			for (int32 Index = LastPeak; Index <= CandidatePeak; ++Index)
			{
				ValleyDensity = FMath::Min(ValleyDensity, Row[Index]);
			}

			if (ValleyDensity <= ValleyRatio * FMath::Min(Row[LastPeak], Row[CandidatePeak]))
			{
				++PeakCount;
				LastPeak = CandidatePeak;
			}
			else if (Row[CandidatePeak] > Row[LastPeak])
			{
				LastPeak = CandidatePeak;
			}
		}

		return PeakCount;
	}

	int32 CountTonguesAtSixtyPercentHeight(const ATDFluidFireActor* FireActor, const FVector& AnchorWorld)
	{
		const FTDFluidGrid2DSolver& Solver = FireActor->LiveComponent->GetSolver();
		const int32 Resolution = Solver.GetResolution();
		FVector2D AnchorUV;
		Solver.WorldToGridUV(FireActor->LiveComponent->ProjectWorldToSolverPlane(AnchorWorld), AnchorUV);
		const int32 BaseRow = FMath::Clamp(FMath::FloorToInt(static_cast<float>(AnchorUV.Y) * Resolution), 0, Resolution - 1);

		int32 TopRow = BaseRow;
		for (int32 CellY = BaseRow; CellY < Resolution; ++CellY)
		{
			float RowMaximum = 0.0f;
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				RowMaximum = FMath::Max(RowMaximum, Solver.SampleAtCell(CellX, CellY).SelectiveDensity);
			}
			if (RowMaximum >= 0.15f)
			{
				TopRow = CellY;
			}
		}

		if (TopRow <= BaseRow + 3)
		{
			return 0;
		}

		const int32 SampleRow = BaseRow + FMath::RoundToInt(0.6f * static_cast<float>(TopRow - BaseRow));
		TArray<float> Row;
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			Row.Add(Solver.SampleAtCell(CellX, SampleRow).SelectiveDensity);
		}
		return CountDensityPeaks(Row, 0.12f, 0.85f);
	}

	double ComputeHighBandEnergyRatio(const TArray<float>& Samples, double SampleRateHz, double CutoffHz)
	{
		const int32 Count = Samples.Num();
		double Mean = 0.0;
		for (const float Sample : Samples)
		{
			Mean += Sample;
		}
		Mean /= static_cast<double>(Count);

		TArray<double> Windowed;
		Windowed.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Window = 0.5 * (1.0 - FMath::Cos(UE_DOUBLE_TWO_PI * Index / static_cast<double>(Count - 1)));
			Windowed[Index] = (Samples[Index] - Mean) * Window;
		}

		double TotalEnergy = 0.0;
		double HighEnergy = 0.0;
		for (int32 Bin = 1; Bin <= Count / 2; ++Bin)
		{
			double Real = 0.0;
			double Imaginary = 0.0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const double Angle = UE_DOUBLE_TWO_PI * static_cast<double>(Bin) * static_cast<double>(Index) / static_cast<double>(Count);
				Real += Windowed[Index] * FMath::Cos(Angle);
				Imaginary -= Windowed[Index] * FMath::Sin(Angle);
			}

			const double Energy = Real * Real + Imaginary * Imaginary;
			TotalEnergy += Energy;
			if (static_cast<double>(Bin) * SampleRateHz / static_cast<double>(Count) > CutoffHz)
			{
				HighEnergy += Energy;
			}
		}

		return TotalEnergy > 0.0 ? HighEnergy / TotalEnergy : 0.0;
	}

	struct FTDFluidLightSeries
	{
		TArray<float> Intensities;
		double MeanAreaCm2 = 0.0;
	};

	FTDFluidLightSeries RunSteadyLightSeries(bool bIsCampfire, int32 FrameCount, float ReferenceAreaCm2)
	{
		FTDFluidFireTestWorld TestWorld;
		UTDFluidPresetAsset* Preset = bIsCampfire ? MakeCampfireFlamePreset(GetTransientPackage()) : MakeTorchPreset(GetTransientPackage(), 700.0f);
		ATDFluidFireActor* FireActor = SpawnFireActor(TestWorld, FVector(0.0, 0.0, 100.0), Preset, bIsCampfire ? MakeCampfireEmitter() : MakeTorchEmitter());
		FTDFluidLightSeries Series;
		if (!FireActor)
		{
			return Series;
		}

		FireActor->FireLightComponent->MaxIntensityCandelas = bIsCampfire ? 160.0f : 60.0f;
		FireActor->FireLightComponent->ReferenceDensityAreaCm2 = ReferenceAreaCm2;
		const int32 WarmupFrames = 2 * FramesPerSecond;
		double AreaSum = 0.0;
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			TestWorld.TickFrames(1);
			if (FrameIndex < WarmupFrames)
			{
				continue;
			}

			Series.Intensities.Add(FireActor->FireLightComponent->GetLastIntensityCandelas());
			AreaSum += FireActor->FireLightComponent->GetLastDensityAreaCm2();
		}

		Series.MeanAreaCm2 = Series.Intensities.IsEmpty() ? 0.0 : AreaSum / static_cast<double>(Series.Intensities.Num());
		return Series;
	}

	double ComputeCoefficientOfVariation(const TArray<float>& Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}

		double Mean = 0.0;
		for (const float Value : Values)
		{
			Mean += Value;
		}
		Mean /= static_cast<double>(Values.Num());

		double Variance = 0.0;
		for (const float Value : Values)
		{
			Variance += (Value - Mean) * (Value - Mean);
		}
		Variance /= static_cast<double>(Values.Num());
		return Mean > UE_DOUBLE_SMALL_NUMBER ? FMath::Sqrt(Variance) / Mean : 0.0;
	}

	void QueueExplosionBurst(ATDFluidFireActor* FireActor, const FVector& CenterWorld)
	{
		const FTDFluidFragmentTrailSettings Trail;
		const FTDFluidPlaneBasis& Basis = FireActor->LiveComponent->GetPlaneBasis();

		FTDFluidBrushStamp CoreStamp;
		CoreStamp.WorldLocation = CenterWorld;
		CoreStamp.RadiusCm = Trail.BurstCoreRadiusCm;
		CoreStamp.DensityStrength = Trail.FireDensityStrength;
		CoreStamp.VelocityStrength = 0.0f;
		CoreStamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		FireActor->LiveComponent->QueueBrushStamp(CoreStamp);

		for (int32 RingIndex = 0; RingIndex < Trail.BurstRingStampCount; ++RingIndex)
		{
			const double Angle = UE_DOUBLE_TWO_PI * RingIndex / static_cast<double>(Trail.BurstRingStampCount);
			const FVector Outward = Basis.U * FMath::Cos(Angle) + Basis.V * FMath::Sin(Angle);
			FTDFluidBrushStamp RingStamp;
			RingStamp.WorldLocation = CenterWorld + Outward * Trail.BurstRingRadiusCm;
			RingStamp.WorldVelocity = Outward * Trail.BurstRingSpeedCmPerSec;
			RingStamp.RadiusCm = Trail.BurstRingRadiusCm * 0.5f;
			RingStamp.DensityStrength = Trail.FireDensityStrength;
			RingStamp.VelocityStrength = 1.0f;
			RingStamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
			FireActor->LiveComponent->QueueBrushStamp(RingStamp);
		}
	}

	void QueueSyntheticFragmentTrail(ATDFluidFireActor* FireActor, const FVector& CenterWorld, int32 FrameIndex)
	{
		constexpr int32 FragmentCount = 12;
		constexpr float FragmentSpeedCmPerSec = 600.0f;
		const FTDFluidFragmentTrailSettings Trail;
		const FTDFluidPlaneBasis& Basis = FireActor->LiveComponent->GetPlaneBasis();
		for (int32 FragmentIndex = 0; FragmentIndex < FragmentCount; ++FragmentIndex)
		{
			const double Angle = UE_DOUBLE_TWO_PI * FragmentIndex / static_cast<double>(FragmentCount);
			const FVector Direction = Basis.U * FMath::Cos(Angle) + Basis.V * FMath::Sin(Angle);
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = CenterWorld + Direction * (FragmentSpeedCmPerSec * FixedStepSeconds * static_cast<float>(FrameIndex + 1));
			Stamp.WorldVelocity = Direction * FragmentSpeedCmPerSec * Trail.VelocityInheritance;
			Stamp.RadiusCm = Trail.FireStampRadiusCm;
			Stamp.DensityStrength = Trail.FireDensityStrength;
			Stamp.VelocityStrength = 1.0f;
			Stamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
			FireActor->LiveComponent->QueueBrushStamp(Stamp);
		}
	}

	struct FTDFluidEmberRig
	{
		explicit FTDFluidEmberRig(bool bInjectsVelocity, int32 InRandomSeed)
			: bInjectsFieldVelocity(bInjectsVelocity)
		{
			UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::Custom);
			Preset->SimulationConfig.SimMode = ETDFluidSimMode::SimplePainter;
			Preset->SimulationConfig.GridResolution = 64;
			Preset->SimulationConfig.SimExtentCm = FVector2D(400.0, 400.0);
			Preset->SimulationConfig.VelocityDamping = 0.0f;
			Preset->SimulationConfig.DensityDissipation = 0.0f;
			Preset->SimulationConfig.BorderFadeRatio = 0.0f;
			LiveActor = TestWorld.World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
			LiveActor->InitializeFromPreset(Preset, 0.0f);

			AActor* Host = TestWorld.World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
			Ember = NewObject<UTDFluidEmberComponent>(Host);
			Host->AddInstanceComponent(Ember);
			Host->SetRootComponent(Ember);
			Ember->MaxEmbers = 64;
			Ember->SpawnPerSecond = 120.0f;
			Ember->BuoyancyCmPerSec2 = 0.0f;
			Ember->GravityCmPerSec2 = 0.0f;
			Ember->RandomSeed = InRandomSeed;
			Ember->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, CubeMeshPath));
			Ember->RegisterComponent();
		}

		void RunFrames(int32 FrameCount)
		{
			for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
			{
				FTDFluidBrushStamp FieldStamp;
				FieldStamp.WorldLocation = FVector::ZeroVector;
				FieldStamp.WorldVelocity = bInjectsFieldVelocity ? FVector(0.0, 300.0, 0.0) : FVector::ZeroVector;
				FieldStamp.RadiusCm = 600.0f;
				FieldStamp.DensityStrength = 0.7f;
				FieldStamp.VelocityStrength = bInjectsFieldVelocity ? 1.0f : 0.0f;
				FieldStamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
				LiveActor->LiveComponent->QueueBrushStamp(FieldStamp);
				TestWorld.TickFrames(1);
				Ember->StepEmbers(*LiveActor->LiveComponent, FixedStepSeconds);
			}
		}

		FTDFluidFireTestWorld TestWorld;
		ATDFluidLiveActor* LiveActor = nullptr;
		UTDFluidEmberComponent* Ember = nullptr;
		bool bInjectsFieldVelocity = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBuoyancyRaisesDensityTest,
	"TDGame.FluidFX.Solver.BuoyancyRaisesDensityAlongUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBuoyancyRaisesDensityTest::RunTest(const FString& Parameters)
{
	const auto MeasureCentroidShiftCm = [](float BuoyancyCmPerSec2)
	{
		FTDFluidSimulationConfig Config;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(400.0, 400.0);
		Config.DensityDissipation = 0.0f;
		Config.BuoyancyCmPerSec2 = BuoyancyCmPerSec2;

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));

		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(0.0, -100.0, 0.0);
		Stamp.RadiusCm = 40.0f;
		Stamp.DensityStrength = 0.7f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		Solver.ApplyBrushStamps({ Stamp }, TDFluidFireTestPrivate::FixedStepSeconds);

		const auto ComputeCentroidY = [&Solver]()
		{
			double WeightedY = 0.0;
			double Weight = 0.0;
			for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
			{
				for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
				{
					const double Density = Solver.SampleAtCell(CellX, CellY).SelectiveDensity;
					WeightedY += Density * Solver.CellToWorldCenter(CellX, CellY).Y;
					Weight += Density;
				}
			}
			return Weight > 0.0 ? WeightedY / Weight : 0.0;
		};

		const double StartCentroidY = ComputeCentroidY();
		for (int32 FrameIndex = 0; FrameIndex < TDFluidFireTestPrivate::FramesPerSecond; ++FrameIndex)
		{
			Solver.StepSimulation(TDFluidFireTestPrivate::FixedStepSeconds);
		}
		return ComputeCentroidY() - StartCentroidY;
	};

	const double RisingShiftCm = MeasureCentroidShiftCm(700.0f);
	const double BaselineShiftCm = MeasureCentroidShiftCm(0.0f);
	TestTrue(*FString::Printf(TEXT("Buoyancy lifts the density centroid by 30cm or more in one second (%.1f)"), RisingShiftCm), RisingShiftCm >= 30.0);
	TestTrue(*FString::Printf(TEXT("Without buoyancy the centroid stays put (%.1f)"), BaselineShiftCm), FMath::Abs(BaselineShiftCm) < 5.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidBuoyancyAndOutflowZeroKeepChecksumTest,
	"TDGame.FluidFX.Solver.BuoyancyAndOutflowZeroKeepChecksum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidBuoyancyAndOutflowZeroKeepChecksumTest::RunTest(const FString& Parameters)
{
	const auto RunScenario = [](bool bSetsExplicitZeros)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Fire);
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(800.0, 600.0);
		Config.FlowNoiseAccelerationCmPerSec2 = 600.0f;
		if (bSetsExplicitZeros)
		{
			Config.BuoyancyCmPerSec2 = 0.0f;
			Config.OutflowBandRatio = 0.0f;
			Config.OutflowRatePerSec = 0.0f;
		}

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		if (bSetsExplicitZeros)
		{
			Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));
		}

		for (int32 FrameIndex = 0; FrameIndex < 90; ++FrameIndex)
		{
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(-120.0 + FrameIndex * 2.0, -80.0, 0.0);
			Stamp.WorldVelocity = FVector(60.0, 300.0, 0.0);
			Stamp.RadiusCm = 50.0f;
			Stamp.DensityStrength = 0.8f;
			Stamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
			Solver.ApplyBrushStamps({ Stamp }, TDFluidFireTestPrivate::FixedStepSeconds);
			Solver.StepSimulation(TDFluidFireTestPrivate::FixedStepSeconds);
		}
		return Solver.ComputeStateChecksum();
	};

	const uint32 PlainChecksum = RunScenario(false);
	const uint32 ExplicitZeroChecksum = RunScenario(true);
	TestNotEqual(TEXT("The scenario produces a non-trivial state"), PlainChecksum, 0u);
	TestEqual(TEXT("Zero buoyancy and zero outflow keep the state checksum bit for bit"), ExplicitZeroChecksum, PlainChecksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOutflowPreventsTopCapTest,
	"TDGame.FluidFX.Solver.OutflowPreventsTopCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOutflowPreventsTopCapTest::RunTest(const FString& Parameters)
{
	FTDFluidSimulationConfig Config;
	Config.GridResolution = 96;
	Config.SimExtentCm = FVector2D(360.0, 480.0);
	Config.DensityDissipation = 0.25f;
	Config.VorticityStrength = 2.0f;
	Config.BuoyancyCmPerSec2 = 180.0f;
	Config.OutflowBandRatio = 0.2f;
	Config.OutflowRatePerSec = 3.0f;
	Config.FlowNoiseAccelerationCmPerSec2 = 300.0f;
	Config.FlowNoiseScaleCm = 60.0f;
	Config.FlowNoiseSpeed = 0.5f;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	Solver.SetPlaneUpDirection(FVector2D(0.0, 1.0));
	for (int32 FrameIndex = 0; FrameIndex < 10 * TDFluidFireTestPrivate::FramesPerSecond; ++FrameIndex)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(0.0, -150.0, 0.0);
		Stamp.WorldVelocity = FVector(0.0, 100.0, 0.0);
		Stamp.RadiusCm = 30.0f;
		Stamp.DensityStrength = 0.7f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		Solver.ApplyBrushStamps({ Stamp }, TDFluidFireTestPrivate::FixedStepSeconds);
		Solver.StepSimulation(TDFluidFireTestPrivate::FixedStepSeconds);
	}

	const int32 Resolution = Solver.GetResolution();
	const auto MeasureBandMean = [&Solver, Resolution](float StartRatio, float EndRatio)
	{
		const int32 FirstRow = FMath::FloorToInt(StartRatio * Resolution);
		const int32 LastRow = FMath::Min(FMath::CeilToInt(EndRatio * Resolution), Resolution);
		double DensitySum = 0.0;
		int32 CellCount = 0;
		for (int32 CellY = FirstRow; CellY < LastRow; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				DensitySum += Solver.SampleAtCell(CellX, CellY).PrimaryDensity;
				++CellCount;
			}
		}
		return CellCount > 0 ? DensitySum / CellCount : 0.0;
	};

	const double TopBandMean = MeasureBandMean(0.9f, 1.0f);
	const double MiddleBandMean = MeasureBandMean(0.4f, 0.6f);
	TestTrue(*FString::Printf(TEXT("The middle band holds smoke (%.4f)"), MiddleBandMean), MiddleBandMean > 0.001);
	TestTrue(*FString::Printf(TEXT("The top 10%% band holds at most half of the middle band (top %.4f, middle %.4f)"), TopBandMean, MiddleBandMean), TopBandMean <= 0.5 * MiddleBandMean);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireEmitterRisesTest,
	"TDGame.FluidFX.Fire.EmitterRisesWithoutBuoyancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireEmitterRisesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	const FTDFluidFlameMetrics Metrics = RunTorchScenario(0.0f, ETDFluidTorchMotion::Still, 0.0f, FramesPerSecond);
	TestTrue(TEXT("The emitter leaves density in the grid"), Metrics.bHasDensity);
	TestTrue(*FString::Printf(TEXT("Emitter velocity alone lifts the flame centroid 20cm above the emitter (%.1f)"), Metrics.RiseCm), Metrics.RiseCm >= 20.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireBuoyancyIncreasesRiseTest,
	"TDGame.FluidFX.Fire.BuoyancyIncreasesRise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireBuoyancyIncreasesRiseTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	const FTDFluidFlameMetrics Buoyant = RunTorchScenario(700.0f, ETDFluidTorchMotion::Still, 0.0f, FramesPerSecond);
	const FTDFluidFlameMetrics Plain = RunTorchScenario(0.0f, ETDFluidTorchMotion::Still, 0.0f, FramesPerSecond);
	TestTrue(*FString::Printf(TEXT("Buoyancy 700 lifts the centroid 35cm or more (%.1f)"), Buoyant.RiseCm), Buoyant.RiseCm >= 35.0f);
	TestTrue(*FString::Printf(TEXT("Buoyancy adds 10cm over the buoyancy free flame (%.1f vs %.1f)"), Buoyant.RiseCm, Plain.RiseCm), Buoyant.RiseCm >= Plain.RiseCm + 10.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireMotionDragsFlameBehindTest,
	"TDGame.FluidFX.Fire.MotionDragsFlameBehind600",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireMotionDragsFlameBehindTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	const FTDFluidFlameMetrics Moving = RunTorchScenario(700.0f, ETDFluidTorchMotion::AlongU, 600.0f, FramesPerSecond);
	const FTDFluidFlameMetrics Still = RunTorchScenario(700.0f, ETDFluidTorchMotion::Still, 0.0f, FramesPerSecond);
	UE_LOG(LogTDFluidFireCalibration, Display, TEXT("TorchDrag600 lag %.1f cm, moving area %.0f cm2, still area %.0f cm2"), -Moving.LagAlongUCm, Moving.AreaCm2, Still.AreaCm2);
	TestTrue(TEXT("The moving flame leaves density in the grid"), Moving.bHasDensity);
	TestTrue(*FString::Printf(TEXT("The flame centroid trails the emitter by 15cm along U (%.1f)"), Moving.LagAlongUCm), Moving.LagAlongUCm <= -15.0f);
	TestTrue(*FString::Printf(TEXT("The moving flame keeps half of the still density area (%.0f vs %.0f)"), Moving.AreaCm2, Still.AreaCm2), Moving.AreaCm2 >= 0.5f * Still.AreaCm2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireForwardMotionDragsDownTest,
	"TDGame.FluidFX.Fire.ForwardMotionDragsDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireForwardMotionDragsDownTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	const FTDFluidFlameMetrics Metrics = RunTorchScenario(700.0f, ETDFluidTorchMotion::AlongV, 520.0f, FramesPerSecond);
	TestTrue(TEXT("The advancing flame leaves density in the grid"), Metrics.bHasDensity);
	TestTrue(*FString::Printf(TEXT("Moving along V puts the centroid below the emitter (%.1f)"), Metrics.RiseCm), Metrics.RiseCm < 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireCoreDensityCappedTest,
	"TDGame.FluidFX.Fire.CoreDensityCapped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireCoreDensityCappedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	for (const bool bIsCampfire : { false, true })
	{
		FTDFluidFireTestWorld TestWorld;
		UTDFluidPresetAsset* Preset = bIsCampfire ? MakeCampfireFlamePreset(GetTransientPackage()) : MakeTorchPreset(GetTransientPackage(), 700.0f);
		ATDFluidFireActor* FireActor = SpawnFireActor(TestWorld, FVector(0.0, 0.0, 100.0), Preset, bIsCampfire ? MakeCampfireEmitter() : MakeTorchEmitter());
		if (!TestNotNull(TEXT("Fire actor spawns"), FireActor))
		{
			return false;
		}

		TestWorld.TickFrames(3 * FramesPerSecond);
		TArray<float> Densities;
		CollectSelectiveDensities(FireActor->LiveComponent->GetSolver(), Densities);
		const float CorePercentile = ComputePercentile(Densities, 0.99f);
		TestTrue(*FString::Printf(TEXT("%s flame has density cells"), bIsCampfire ? TEXT("Campfire") : TEXT("Torch")), Densities.Num() > 0);
		TestTrue(*FString::Printf(TEXT("%s 99th percentile selective density stays at the emitter cap (%.3f)"), bIsCampfire ? TEXT("Campfire") : TEXT("Torch"), CorePercentile), CorePercentile <= 0.72f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireTongueCountCampfireTest,
	"TDGame.FluidFX.Fire.TongueCountCampfire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireTongueCountCampfireTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	FTDFluidFireTestWorld TestWorld;
	const FVector AnchorLocation(0.0, 0.0, 100.0);
	ATDFluidFireActor* FireActor = SpawnFireActor(TestWorld, AnchorLocation, MakeCampfireFlamePreset(GetTransientPackage()), MakeCampfireEmitter());
	if (!TestNotNull(TEXT("Campfire actor spawns"), FireActor))
	{
		return false;
	}

	TestWorld.TickFrames(3 * FramesPerSecond);
	int32 SampleCount = 0;
	int32 TongueTotal = 0;
	for (int32 FrameIndex = 0; FrameIndex < 2 * FramesPerSecond; ++FrameIndex)
	{
		TestWorld.TickFrames(1);
		if (FrameIndex % 4 != 0)
		{
			continue;
		}

		TongueTotal += CountTonguesAtSixtyPercentHeight(FireActor, AnchorLocation);
		++SampleCount;
	}

	const float MeanTongueCount = static_cast<float>(TongueTotal) / static_cast<float>(FMath::Max(SampleCount, 1));
	TestTrue(*FString::Printf(TEXT("The campfire splits into two or more tongues on average (%.2f)"), MeanTongueCount), MeanTongueCount >= 2.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireFlickerBandLimitedTest,
	"TDGame.FluidFX.Fire.FlickerIsDeterministicBandLimited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireFlickerBandLimitedTest::RunTest(const FString& Parameters)
{
	constexpr double SampleRateHz = 240.0;
	constexpr int32 SampleCount = 1920;
	const auto BuildSeries = [](int32 Seed, int32 StreamIndex)
	{
		TArray<float> Series;
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			Series.Add(TDFluidDeterministic::EvaluateBandLimitedNoise(Seed, StreamIndex, static_cast<double>(SampleIndex) / SampleRateHz, 5.0));
		}
		return Series;
	};

	const TArray<float> FirstSeries = BuildSeries(1, 0);
	const TArray<float> SecondSeries = BuildSeries(1, 0);
	const TArray<float> OtherSeedSeries = BuildSeries(2, 0);
	const TArray<float> OtherStampSeries = BuildSeries(1, 1);
	TestTrue(TEXT("Two evaluations with the same seed are identical"), FirstSeries == SecondSeries);
	TestTrue(TEXT("A different seed gives a different sequence"), FirstSeries != OtherSeedSeries);
	TestTrue(TEXT("A different stamp index gives a different sequence"), FirstSeries != OtherStampSeries);

	float MinValue = 0.0f;
	float MaxValue = 0.0f;
	for (const float Value : FirstSeries)
	{
		MinValue = FMath::Min(MinValue, Value);
		MaxValue = FMath::Max(MaxValue, Value);
	}
	TestTrue(*FString::Printf(TEXT("The flicker stays inside the unit range (%.3f..%.3f)"), MinValue, MaxValue), MinValue >= -1.0f && MaxValue <= 1.0f && MaxValue - MinValue > 0.5f);

	const double HighBandRatio = TDFluidFireTestPrivate::ComputeHighBandEnergyRatio(FirstSeries, SampleRateHz, 10.0);
	TestTrue(*FString::Printf(TEXT("At most 20%% of the flicker energy lies above 10Hz (%.3f)"), HighBandRatio), HighBandRatio <= 0.2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireLightFlickerCoefficientTest,
	"TDGame.FluidFX.Fire.LightFlickerCoefficient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireLightFlickerCoefficientTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	for (const bool bIsCampfire : { false, true })
	{
		const TCHAR* const PresetName = bIsCampfire ? TEXT("Campfire") : TEXT("Torch");
		const FTDFluidLightSeries CalibrationRun = RunSteadyLightSeries(bIsCampfire, 10 * FramesPerSecond, 1000.0f);
		const float ReferenceAreaCm2 = static_cast<float>(FMath::Max(CalibrationRun.MeanAreaCm2, 1.0));
		const FTDFluidLightSeries FirstRun = RunSteadyLightSeries(bIsCampfire, 10 * FramesPerSecond, ReferenceAreaCm2);
		const FTDFluidLightSeries SecondRun = RunSteadyLightSeries(bIsCampfire, 10 * FramesPerSecond, ReferenceAreaCm2);
		const double Coefficient = ComputeCoefficientOfVariation(FirstRun.Intensities);
		UE_LOG(LogTDFluidFireCalibration, Display, TEXT("SteadyStateMeanArea %s = %.1f cm2, light CoV = %.3f"), PresetName, FirstRun.MeanAreaCm2, Coefficient);
		AddInfo(FString::Printf(TEXT("SteadyStateMeanArea %s = %.1f cm2, light CoV = %.3f"), PresetName, FirstRun.MeanAreaCm2, Coefficient));
		TestTrue(*FString::Printf(TEXT("%s light has samples"), PresetName), FirstRun.Intensities.Num() > 0);
		TestTrue(*FString::Printf(TEXT("%s light coefficient of variation is within 0.05..0.30 (%.3f)"), PresetName, Coefficient), Coefficient >= 0.05 && Coefficient <= 0.30);
		TestTrue(*FString::Printf(TEXT("%s light sequence repeats exactly with the same seed"), PresetName), FirstRun.Intensities == SecondRun.Intensities);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireExplosionLightDecayTest,
	"TDGame.FluidFX.Fire.ExplosionLightDecay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireExplosionLightDecayTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	const auto RunExplosion = [](float ReferenceAreaCm2, float& OutPeakAreaCm2)
	{
		FTDFluidFireTestWorld TestWorld;
		const FVector CenterLocation(0.0, 0.0, 100.0);
		ATDFluidFireActor* FireActor = SpawnFireActor(TestWorld, CenterLocation, MakeExplosionFirePreset(GetTransientPackage()), FTDFluidSourceEmitterSettings());
		TArray<float> Intensities;
		if (!FireActor)
		{
			return Intensities;
		}

		FireActor->FireLightComponent->MaxIntensityCandelas = 120.0f;
		FireActor->FireLightComponent->ReferenceDensityAreaCm2 = ReferenceAreaCm2;
		TestWorld.TickFrames(2);

		OutPeakAreaCm2 = 0.0f;
		const int32 TrailFrames = FramesPerSecond;
		const int32 TotalFrames = TrailFrames + 4 * FramesPerSecond;
		for (int32 FrameIndex = 0; FrameIndex < TotalFrames; ++FrameIndex)
		{
			if (FrameIndex == 0)
			{
				QueueExplosionBurst(FireActor, CenterLocation);
			}
			if (FrameIndex < TrailFrames)
			{
				QueueSyntheticFragmentTrail(FireActor, CenterLocation, FrameIndex);
			}

			TestWorld.TickFrames(1);
			Intensities.Add(FireActor->FireLightComponent->GetLastIntensityCandelas());
			OutPeakAreaCm2 = FMath::Max(OutPeakAreaCm2, FireActor->FireLightComponent->GetLastDensityAreaCm2());
		}
		return Intensities;
	};

	float CalibrationPeakAreaCm2 = 0.0f;
	RunExplosion(8000.0f, CalibrationPeakAreaCm2);
	const float ReferenceAreaCm2 = FMath::Max(0.5f * CalibrationPeakAreaCm2, 1.0f);
	float PeakAreaCm2 = 0.0f;
	const TArray<float> Intensities = RunExplosion(ReferenceAreaCm2, PeakAreaCm2);
	if (!TestTrue(TEXT("Explosion fire actor spawns and records the light"), Intensities.Num() > 0))
	{
		return false;
	}

	int32 PeakFrame = 0;
	for (int32 FrameIndex = 0; FrameIndex < Intensities.Num(); ++FrameIndex)
	{
		if (Intensities[FrameIndex] > Intensities[PeakFrame])
		{
			PeakFrame = FrameIndex;
		}
	}

	const float PeakIntensity = Intensities[PeakFrame];
	float LowestAfterPeak = PeakIntensity;
	const int32 WindowEnd = FMath::Min(PeakFrame + static_cast<int32>(2.5f * FramesPerSecond), Intensities.Num() - 1);
	for (int32 FrameIndex = PeakFrame + 1; FrameIndex <= WindowEnd; ++FrameIndex)
	{
		LowestAfterPeak = FMath::Min(LowestAfterPeak, Intensities[FrameIndex]);
	}

	UE_LOG(LogTDFluidFireCalibration, Display, TEXT("ExplosionLight peak area %.0f cm2, reference %.0f cm2, peak %.1f cd at frame %d, lowest in 2.5s %.1f cd"), PeakAreaCm2, ReferenceAreaCm2, PeakIntensity, PeakFrame, LowestAfterPeak);
	AddInfo(FString::Printf(TEXT("ExplosionLight peak area %.0f cm2, half of it %.0f cm2"), PeakAreaCm2, ReferenceAreaCm2));
	TestTrue(*FString::Printf(TEXT("The burst lights the scene (%.1f cd)"), PeakIntensity), PeakIntensity > 0.1f * 120.0f);
	TestTrue(*FString::Printf(TEXT("The light drops to 30%% of its peak within 2.5s (peak %.1f, lowest %.1f)"), PeakIntensity, LowestAfterPeak), LowestAfterPeak <= 0.3f * PeakIntensity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireCampfireSmokeSteadyStateTest,
	"TDGame.FluidFX.Fire.CampfireSmokeSteadyState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireCampfireSmokeSteadyStateTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	FTDFluidFireTestWorld TestWorld;
	ATDFluidFireActor* FireActor = SpawnFireActor(TestWorld, FVector(0.0, 0.0, 0.0), MakeCampfireFlamePreset(GetTransientPackage()), MakeCampfireEmitter());
	ATDFluidLiveActor* SmokeActor = TestWorld.World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), FVector(0.0, 0.0, 80.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Campfire actors spawn"), FireActor) || !TestNotNull(TEXT("Smoke actor spawns"), SmokeActor))
	{
		return false;
	}

	SmokeActor->InitializeFromPreset(MakeCampfireSmokePreset(GetTransientPackage()), 0.0f);
	FireActor->DownstreamSmokeActor = SmokeActor;
	FireActor->DownstreamFeed.bIsEnabled = true;

	const auto MeasureSmokeArea = [SmokeActor]()
	{
		FVector2D Centroid;
		float AreaCm2 = 0.0f;
		SmokeActor->LiveComponent->ComputeDensityCentroidPlane(false, Centroid, AreaCm2);
		return AreaCm2;
	};

	double EarlyWindowSum = 0.0;
	double LateWindowSum = 0.0;
	int32 WindowSampleCount = 0;
	for (int32 FrameIndex = 0; FrameIndex < 12 * FramesPerSecond; ++FrameIndex)
	{
		TestWorld.TickFrames(1);
		const bool bInEarlyWindow = FrameIndex >= static_cast<int32>(9.5f * FramesPerSecond) && FrameIndex < 10 * FramesPerSecond;
		const bool bInLateWindow = FrameIndex >= static_cast<int32>(11.5f * FramesPerSecond);
		if (!bInEarlyWindow && !bInLateWindow)
		{
			continue;
		}

		const float AreaCm2 = MeasureSmokeArea();
		EarlyWindowSum += bInEarlyWindow ? AreaCm2 : 0.0;
		LateWindowSum += bInLateWindow ? AreaCm2 : 0.0;
		WindowSampleCount += bInEarlyWindow ? 1 : 0;
	}

	const double EarlyMeanArea = EarlyWindowSum / FMath::Max(WindowSampleCount, 1);
	const double LateMeanArea = LateWindowSum / FMath::Max(WindowSampleCount, 1);
	const double RelativeChangePerSecond = EarlyMeanArea > 0.0 ? FMath::Abs(LateMeanArea - EarlyMeanArea) / EarlyMeanArea / 2.0 : 1.0;
	UE_LOG(LogTDFluidFireCalibration, Display, TEXT("CampfireSmoke area %.1f -> %.1f cm2, change %.3f per second"), EarlyMeanArea, LateMeanArea, RelativeChangePerSecond);
	AddInfo(FString::Printf(TEXT("CampfireSmoke steady area %.1f cm2"), LateMeanArea));
	TestTrue(*FString::Printf(TEXT("The downstream feed fills the smoke (%.1f cm2)"), LateMeanArea), LateMeanArea > 100.0);
	TestTrue(*FString::Printf(TEXT("Smoke area drifts under 5%% per second in the last two seconds (%.3f)"), RelativeChangePerSecond), RelativeChangePerSecond < 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFireDownstreamFeedUsesFadedDensityTest,
	"TDGame.FluidFX.Fire.DownstreamFeedUsesFadedDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFireDownstreamFeedUsesFadedDensityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	FTDFluidFireTestWorld TestWorld;
	ATDFluidFireActor* FireActor = SpawnFireActor(TestWorld, FVector(0.0, 0.0, 100.0), MakeCampfireFlamePreset(GetTransientPackage()), MakeCampfireEmitter());
	if (!TestNotNull(TEXT("Campfire actor spawns"), FireActor))
	{
		return false;
	}

	TestWorld.TickFrames(6 * FramesPerSecond);
	const FTDFluidGrid2DSolver& Solver = FireActor->LiveComponent->GetSolver();
	const FTDFluidDownstreamFeedSettings Settings;
	TArray<FIntPoint> FeedCells;
	ATDFluidFireActor::SelectDownstreamFeedCells(Solver, Settings, FeedCells);

	const int32 Resolution = Solver.GetResolution();
	const int32 FirstBandRow = FMath::CeilToInt(Settings.BandStartRatio * Resolution);
	const auto ComputeWeight = [&Settings](float Density)
	{
		return Density < Settings.MinFeedDensity ? 0.0f : Density * (1.0f - FMath::SmoothStep(Settings.MinFeedDensity, Settings.FadedDensityThreshold, Density));
	};

	TestTrue(TEXT("The campfire feeds at least one stamp"), FeedCells.Num() > 0);
	TestTrue(*FString::Printf(TEXT("At most 24 stamps per step (%d)"), FeedCells.Num()), FeedCells.Num() <= Settings.MaxStampsPerStep);

	float LowestSelectedWeight = TNumericLimits<float>::Max();
	bool bAllInsideBand = true;
	bool bAllFaded = true;
	TSet<int32> SelectedIndices;
	for (const FIntPoint& Cell : FeedCells)
	{
		const float Density = Solver.SampleAtCell(Cell.X, Cell.Y).SelectiveDensity;
		bAllInsideBand &= Cell.Y >= FirstBandRow;
		bAllFaded &= Density >= Settings.MinFeedDensity && Density <= Settings.FadedDensityThreshold;
		LowestSelectedWeight = FMath::Min(LowestSelectedWeight, ComputeWeight(Density));
		SelectedIndices.Add(Cell.Y * Resolution + Cell.X);
	}

	const int32 Stride = FMath::Max(1, Resolution / 32);
	float HighestUnselectedWeight = 0.0f;
	for (int32 CellY = FirstBandRow; CellY < Resolution; CellY += Stride)
	{
		for (int32 CellX = 0; CellX < Resolution; CellX += Stride)
		{
			if (!SelectedIndices.Contains(CellY * Resolution + CellX))
			{
				HighestUnselectedWeight = FMath::Max(HighestUnselectedWeight, ComputeWeight(Solver.SampleAtCell(CellX, CellY).SelectiveDensity));
			}
		}
	}

	TestTrue(TEXT("Every stamp lies in the upper band"), bAllInsideBand);
	TestTrue(TEXT("Every stamp comes from density that has faded below the visible threshold"), bAllFaded);
	TestTrue(*FString::Printf(TEXT("Selected cells outweigh the unselected ones (%.4f vs %.4f)"), LowestSelectedWeight, HighestUnselectedWeight), FeedCells.IsEmpty() || LowestSelectedWeight >= HighestUnselectedWeight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidEmberSpawnAdvectKillTest,
	"TDGame.FluidFX.Ember.SpawnAdvectKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidEmberSpawnAdvectKillTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	{
		FTDFluidEmberRig Rig(true, 1);
		Rig.RunFrames(40);
		Rig.Ember->SpawnBurst(FVector::ZeroVector, 40, 400.0f);
		Rig.RunFrames(45);

		TestEqual(TEXT("The ember pool keeps a constant instance count"), Rig.Ember->GetInstanceCount(), 64);
		TestTrue(TEXT("Embers are alive in the moving field"), Rig.Ember->GetAliveEmberCount() > 0);

		int32 MeasuredCount = 0;
		int32 OutOfToleranceCount = 0;
		double FieldSpeedSum = 0.0;
		for (int32 InstanceIndex = 0; InstanceIndex < Rig.Ember->GetInstanceCount(); ++InstanceIndex)
		{
			FTransform InstanceTransform;
			Rig.Ember->GetInstanceTransform(InstanceIndex, InstanceTransform, true);
			const FVector Scale = InstanceTransform.GetScale3D();
			const FVector Position = InstanceTransform.GetLocation();
			if (Scale.X <= UE_KINDA_SMALL_NUMBER || FMath::Abs(Position.X) > 150.0 || FMath::Abs(Position.Y) > 150.0)
			{
				continue;
			}

			const double Speed = (Scale.X / Rig.Ember->BaseScale - 1.0) / Rig.Ember->StretchPerCmPerSec;
			const FVector Velocity = InstanceTransform.GetRotation().GetForwardVector() * Speed;
			FTDFluidCellSample FieldSample;
			Rig.LiveActor->LiveComponent->SampleFluidAtWorldLocation(Position, FieldSample);
			FieldSpeedSum += FieldSample.VelocityCmPerSec.Y;
			++MeasuredCount;
			if (FMath::Abs(Velocity.Y - 300.0) > 30.0 || FMath::Abs(Velocity.X) > 30.0)
			{
				++OutOfToleranceCount;
			}
		}

		const double MeanFieldSpeed = MeasuredCount > 0 ? FieldSpeedSum / MeasuredCount : 0.0;
		TestTrue(*FString::Printf(TEXT("The test field runs at 300cm/s (%.1f over %d embers)"), MeanFieldSpeed, MeasuredCount), MeasuredCount > 0 && FMath::Abs(MeanFieldSpeed - 300.0) <= 15.0);
		TestEqual(TEXT("Every measured ember follows the field within 10 percent"), OutOfToleranceCount, 0);
	}

	{
		FTDFluidEmberRig Rig(false, 1);
		Rig.RunFrames(90);
		TestEqual(TEXT("The idle pool keeps a constant instance count"), Rig.Ember->GetInstanceCount(), 64);
		TestTrue(*FString::Printf(TEXT("Embers in still air are removed after the slow timeout (%d alive)"), Rig.Ember->GetAliveEmberCount()), Rig.Ember->GetAliveEmberCount() <= 45);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidEmberDeterministicReplayTest,
	"TDGame.FluidFX.Ember.DeterministicReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidEmberDeterministicReplayTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	const auto RunReplay = [](int32 Seed)
	{
		FTDFluidEmberRig Rig(true, Seed);
		Rig.RunFrames(30);
		Rig.Ember->SpawnBurst(FVector::ZeroVector, 20, 400.0f);
		Rig.RunFrames(60);
		return TPair<int32, int32>(Rig.Ember->ComputeEmberStateHash(), Rig.Ember->GetAliveEmberCount());
	};

	const TPair<int32, int32> FirstRun = RunReplay(7);
	const TPair<int32, int32> SecondRun = RunReplay(7);
	const TPair<int32, int32> OtherSeedRun = RunReplay(8);
	TestTrue(TEXT("The replay leaves embers alive"), FirstRun.Value > 0);
	TestEqual(TEXT("The same seed reproduces the ember state hash"), SecondRun.Key, FirstRun.Key);
	TestEqual(TEXT("The same seed reproduces the alive ember count"), SecondRun.Value, FirstRun.Value);
	TestNotEqual(TEXT("A different seed changes the ember state hash"), OtherSeedRun.Key, FirstRun.Key);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidEmberBurstDefaultHashTest,
	"TDGame.FluidFX.Ember.BurstDefaultMatchesHeadHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidEmberBurstDefaultHashTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	constexpr int32 ExpectedHeadHash = -1141597509;
	constexpr int32 ExpectedHeadAliveCount = 127;
	constexpr int32 BurstReplayMaxEmbers = 128;

	FTDFluidEmberRig Rig(true, 7);
	Rig.Ember->MaxEmbers = BurstReplayMaxEmbers;
	Rig.RunFrames(30);
	const int32 AliveBeforeBurst = Rig.Ember->GetAliveEmberCount();
	Rig.Ember->SpawnBurst(FVector::ZeroVector, 20, 400.0f);
	const int32 BurstAddedCount = Rig.Ember->GetAliveEmberCount() - AliveBeforeBurst;
	Rig.RunFrames(60);
	const int32 EmberStateHash = Rig.Ember->ComputeEmberStateHash();
	const int32 AliveEmberCount = Rig.Ember->GetAliveEmberCount();
	AddInfo(FString::Printf(TEXT("Ember burst replay seed 7: hash %d, alive %d, burst added %d"), EmberStateHash, AliveEmberCount, BurstAddedCount));
	TestTrue(TEXT("The burst adds at least one ember so the hash sees burst directions"), BurstAddedCount >= 1);
	TestEqual(TEXT("The default burst keeps the HEAD ember state hash"), EmberStateHash, ExpectedHeadHash);
	TestEqual(TEXT("The default burst keeps the HEAD alive ember count"), AliveEmberCount, ExpectedHeadAliveCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidEmberBurstDirectionZeroRatioTest,
	"TDGame.FluidFX.Ember.BurstDirectionRatioZeroMatchesSphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidEmberBurstDirectionZeroRatioTest::RunTest(const FString& Parameters)
{
	const float DirectionZValues[] = {-1.0f, -0.5f, -0.1f, 0.0f, 0.3f, 1.0f};
	const float DirectionAngles[] = {0.0f, 1.0f, 2.5f, 5.0f};
	for (const float DirectionZ : DirectionZValues)
	{
		for (const float DirectionAngle : DirectionAngles)
		{
			const float RingRadius = FMath::Sqrt(FMath::Max(0.0f, 1.0f - DirectionZ * DirectionZ));
			const FVector SphereDirection(RingRadius * FMath::Cos(DirectionAngle), RingRadius * FMath::Sin(DirectionAngle), DirectionZ);
			const FVector UpwardDirection(SphereDirection.X, SphereDirection.Y, FMath::Abs(DirectionZ));
			const FVector HalfDirection(SphereDirection.X, SphereDirection.Y, (DirectionZ + FMath::Abs(DirectionZ)) * 0.5f);
			TestTrue(FString::Printf(TEXT("Ratio 0 keeps the sphere direction (z %.2f, angle %.2f)"), DirectionZ, DirectionAngle), UTDFluidEmberComponent::MakeBurstDirection(DirectionZ, DirectionAngle, 0.0f).Equals(SphereDirection, 1.0e-6));
			TestTrue(FString::Printf(TEXT("Ratio 1 mirrors the downward half up (z %.2f, angle %.2f)"), DirectionZ, DirectionAngle), UTDFluidEmberComponent::MakeBurstDirection(DirectionZ, DirectionAngle, 1.0f).Equals(UpwardDirection, 1.0e-6));
			TestTrue(FString::Printf(TEXT("Ratio 0.5 flattens the downward half (z %.2f, angle %.2f)"), DirectionZ, DirectionAngle), UTDFluidEmberComponent::MakeBurstDirection(DirectionZ, DirectionAngle, 0.5f).Equals(HalfDirection, 1.0e-6));
		}
	}
	return true;
}

namespace TDFluidFireTestPrivate
{
	constexpr int32 UpwardBurstEmberCount = 40;
	constexpr int32 MinVisibleUpwardBurstEmbers = 30;
	constexpr double UpwardBurstFloorCm = -0.01;
	constexpr double SphereBurstDropCm = -1.0;
	constexpr double MinDroppingBurstShare = 0.25;

	struct FTDFluidEmberBurstSpread
	{
		int32 VisibleEmberCount = 0;
		int32 BelowFloorCount = 0;
		int32 DroppedCount = 0;
	};

	bool MeasureEmberBurstSpread(FAutomationTestBase& Test, float UpwardRatio, FTDFluidEmberBurstSpread& OutSpread)
	{
		FTDFluidEmberRig Rig(false, 3);
		Rig.Ember->SpawnPerSecond = 0.0f;
		FFloatProperty* Property = FindFProperty<FFloatProperty>(UTDFluidEmberComponent::StaticClass(), TEXT("BurstUpwardRatio"));
		if (!Property)
		{
			Test.AddError(TEXT("UTDFluidEmberComponent has no float property BurstUpwardRatio"));
			return false;
		}
		Property->SetPropertyValue_InContainer(Rig.Ember, UpwardRatio);

		Rig.RunFrames(2);
		Rig.Ember->SpawnBurst(FVector::ZeroVector, UpwardBurstEmberCount, 400.0f);
		Rig.RunFrames(6);
		for (int32 InstanceIndex = 0; InstanceIndex < Rig.Ember->GetInstanceCount(); ++InstanceIndex)
		{
			FTransform InstanceTransform;
			Rig.Ember->GetInstanceTransform(InstanceIndex, InstanceTransform, true);
			if (InstanceTransform.GetScale3D().X <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const double HeightCm = InstanceTransform.GetLocation().Z;
			++OutSpread.VisibleEmberCount;
			OutSpread.BelowFloorCount += HeightCm < UpwardBurstFloorCm ? 1 : 0;
			OutSpread.DroppedCount += HeightCm < SphereBurstDropCm ? 1 : 0;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidEmberBurstUpwardRatioTest,
	"TDGame.FluidFX.Ember.BurstUpwardRatioLiftsBurst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidEmberBurstUpwardRatioTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFireTestPrivate;

	FTDFluidEmberBurstSpread UpwardSpread;
	FTDFluidEmberBurstSpread SphereSpread;
	if (!MeasureEmberBurstSpread(*this, 1.0f, UpwardSpread) || !MeasureEmberBurstSpread(*this, 0.0f, SphereSpread))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("Upward burst: %d visible, %d below the center. Sphere burst: %d visible, %d dropped"), UpwardSpread.VisibleEmberCount, UpwardSpread.BelowFloorCount, SphereSpread.VisibleEmberCount, SphereSpread.DroppedCount));
	TestTrue(TEXT("The upward burst keeps enough embers visible"), UpwardSpread.VisibleEmberCount >= MinVisibleUpwardBurstEmbers);
	TestEqual(TEXT("No upward burst ember sinks below the burst center"), UpwardSpread.BelowFloorCount, 0);
	TestTrue(TEXT("The sphere burst keeps enough embers visible"), SphereSpread.VisibleEmberCount >= MinVisibleUpwardBurstEmbers);
	TestTrue(TEXT("At least a quarter of the sphere burst falls below the center"), SphereSpread.DroppedCount >= MinDroppingBurstShare * SphereSpread.VisibleEmberCount);
	return true;
}

#endif
