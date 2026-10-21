#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Internationalization/Regex.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/OutputDeviceRedirector.h"
#include "NiagaraComponent.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TDGame.h"
#include "Tickable.h"

namespace TDFluidPerfNiagaraTest
{
	const TCHAR* const WaterTrailSystemPath = TEXT("/Game/ArrowTrail/FX/NS_ArrowTrail_Water.NS_ArrowTrail_Water");
	const TCHAR* const FireTrailSystemPath = TEXT("/Game/ArrowTrail/FX/NS_ArrowTrail_Fire.NS_ArrowTrail_Fire");
	const TCHAR* const TickCostVariableName = TEXT("TD.FluidFX.LogTickCost");
	const TCHAR* const ParallelActorStepsVariableName = TEXT("TD.FluidFX.ParallelActorSteps");
	const FName DriveTagName(TEXT("TDFluidNiagaraDriveTag"));

	struct FPerfTestWorld
	{
		FPerfTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidPerfNiagaraWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FPerfTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		void TickWorld(float TotalSeconds, float StepSeconds = 0.05f)
		{
			for (float Remaining = TotalSeconds; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, StepSeconds);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		UWorld* World = nullptr;
	};

	class FPerfLogCapture : public FOutputDevice
	{
	public:
		FPerfLogCapture()
		{
			GLog->AddOutputDevice(this);
		}

		virtual ~FPerfLogCapture() override
		{
			GLog->RemoveOutputDevice(this);
		}

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const FString Line(Message);
			if (Line.StartsWith(TEXT("TDFluidPerf:")))
			{
				CapturedLines.Add(Line);
			}
		}

		TArray<FString> CapturedLines;
	};

	struct FTickCostVariableScope
	{
		FTickCostVariableScope()
			: Variable(IConsoleManager::Get().FindConsoleVariable(TickCostVariableName))
		{
			if (Variable)
			{
				PreviousValue = Variable->GetInt();
			}
		}

		~FTickCostVariableScope()
		{
			SetValue(PreviousValue);
		}

		void SetValue(int32 Value)
		{
			if (Variable)
			{
				Variable->Set(Value, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
	};

	AActor* SpawnTaggedNiagaraActor(UWorld* World, const FVector& Location, UNiagaraComponent*& OutNiagaraComponent)
	{
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Actor)
		{
			return nullptr;
		}

		USceneComponent* ActorRoot = NewObject<USceneComponent>(Actor);
		Actor->AddInstanceComponent(ActorRoot);
		Actor->SetRootComponent(ActorRoot);
		ActorRoot->RegisterComponent();
		Actor->SetActorLocation(Location);
		Actor->Tags.Add(DriveTagName);

		OutNiagaraComponent = NewObject<UNiagaraComponent>(Actor);
		OutNiagaraComponent->SetAutoActivate(false);
		Actor->AddInstanceComponent(OutNiagaraComponent);
		OutNiagaraComponent->SetupAttachment(ActorRoot);
		OutNiagaraComponent->RegisterComponent();
		return Actor;
	}

	UTDFluidPresetAsset* CreateWaterPreset(UObject* Outer, const TCHAR* SystemPath, FName RendererTag = NAME_None)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, ETDFluidPresetKind::Water);
		Preset->ExternalRendererActorTag = RendererTag;
		if (SystemPath)
		{
			Preset->ExternalNiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(SystemPath));
		}
		return Preset;
	}

	UNiagaraComponent* WaitForOwnedNiagaraComponent(FPerfTestWorld& TestWorld, const ATDFluidLiveActor* LiveActor)
	{
		for (int32 Attempt = 0; Attempt < 400 && !LiveActor->LiveComponent->GetOwnedNiagaraComponent(); ++Attempt)
		{
			FlushAsyncLoading();
			TestWorld.TickWorld(0.05f, 0.05f);
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, 0.05f);
		}
		return LiveActor->LiveComponent->GetOwnedNiagaraComponent();
	}

	int32 CountValidNiagaraComponents(const AActor* Actor)
	{
		TInlineComponentArray<UNiagaraComponent*> NiagaraComponents(Actor);
		int32 ValidCount = 0;
		for (const UNiagaraComponent* NiagaraComp : NiagaraComponents)
		{
			if (IsValid(NiagaraComp))
			{
				++ValidCount;
			}
		}
		return ValidCount;
	}

	FNiagaraVariable MakeCenterVariable()
	{
		return FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), FName(TEXT("User.SimCenterWorld")));
	}

	FNiagaraVariable MakeExtentsVariable()
	{
		return FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), FName(TEXT("User.SimExtentsCm")));
	}

	FNiagaraVariable MakeDensityTextureVariable()
	{
		return FNiagaraVariable(FNiagaraTypeDefinition::GetUTextureDef(), FName(TEXT("User.DensityPressureTexture")));
	}

	FNiagaraVariable MakeVelocityTextureVariable()
	{
		return FNiagaraVariable(FNiagaraTypeDefinition::GetUTextureDef(), FName(TEXT("User.VelocityTexture")));
	}

	bool HasDriveParameter(const UNiagaraComponent& NiagaraComp, const FNiagaraVariable& Variable)
	{
		return NiagaraComp.GetOverrideParameters().IndexOf(Variable) != INDEX_NONE;
	}

	EPixelFormat ResolveOutputPixelFormat(const UTexture* OutputTexture)
	{
		if (const UTexture2D* OutputTexture2D = Cast<UTexture2D>(OutputTexture))
		{
			return OutputTexture2D->GetPixelFormat();
		}
		if (const UTextureRenderTarget2D* OutputRenderTarget = Cast<UTextureRenderTarget2D>(OutputTexture))
		{
			return OutputRenderTarget->GetFormat();
		}
		return PF_Unknown;
	}

	void ExpectDrivenParameters(
		FAutomationTestBase& Test,
		const UNiagaraComponent& NiagaraComp,
		const UTDFluidLiveComponent& FluidComponent,
		float ExpectedHeightCm,
		const FString& Label)
	{
		const FNiagaraParameterStore& Parameters = NiagaraComp.GetOverrideParameters();
		const FVector2D ExpectedCenter = FluidComponent.GetSolver().GetCenterWorld();
		const FVector2D ExpectedExtent = FluidComponent.GetSolver().GetExtentCm();

		Test.TestTrue(*(Label + TEXT(" has User.SimCenterWorld")), HasDriveParameter(NiagaraComp, MakeCenterVariable()));
		const FVector3f Center = Parameters.GetParameterValue<FVector3f>(MakeCenterVariable());
		Test.TestNearlyEqual(*(Label + TEXT(" SimCenterWorld X is the solver center X")), Center.X, static_cast<float>(ExpectedCenter.X), 0.01f);
		Test.TestNearlyEqual(*(Label + TEXT(" SimCenterWorld Y is the solver center Y")), Center.Y, static_cast<float>(ExpectedCenter.Y), 0.01f);
		Test.TestNearlyEqual(*(Label + TEXT(" SimCenterWorld Z is the height of the driven actor")), Center.Z, ExpectedHeightCm, 0.01f);

		Test.TestTrue(*(Label + TEXT(" has User.SimExtentsCm")), HasDriveParameter(NiagaraComp, MakeExtentsVariable()));
		const FVector2f Extent = Parameters.GetParameterValue<FVector2f>(MakeExtentsVariable());
		Test.TestNearlyEqual(*(Label + TEXT(" SimExtentsCm X is the solver extent X")), Extent.X, static_cast<float>(ExpectedExtent.X), 0.01f);
		Test.TestNearlyEqual(*(Label + TEXT(" SimExtentsCm Y is the solver extent Y")), Extent.Y, static_cast<float>(ExpectedExtent.Y), 0.01f);

		const UTexture* DensityTexture = FluidComponent.GetDensityPressureTexture();
		const UTexture* VelocityTexture = FluidComponent.GetVelocityTexture();
		const bool bCanCreateTextures = !IsRunningCommandlet() && FApp::CanEverRender();
		Test.TestTrue(*(Label + TEXT(" output textures exist only when rendering is possible")), (DensityTexture != nullptr && VelocityTexture != nullptr) == bCanCreateTextures);

		if (DensityTexture && VelocityTexture)
		{
			Test.TestTrue(*(Label + TEXT(" DensityPressureTexture is half float RGBA")), ResolveOutputPixelFormat(DensityTexture) == PF_FloatRGBA);
			Test.TestTrue(*(Label + TEXT(" VelocityTexture is half float RGBA")), ResolveOutputPixelFormat(VelocityTexture) == PF_FloatRGBA);
			Test.TestTrue(*(Label + TEXT(" DensityPressureTexture parameter is the output texture")), Parameters.GetUObject(MakeDensityTextureVariable()).Get() == static_cast<const UObject*>(DensityTexture));
			Test.TestTrue(*(Label + TEXT(" VelocityTexture parameter is the output texture")), Parameters.GetUObject(MakeVelocityTextureVariable()).Get() == static_cast<const UObject*>(VelocityTexture));
			return;
		}

		Test.TestFalse(*(Label + TEXT(" has no DensityPressureTexture parameter without an output texture")), HasDriveParameter(NiagaraComp, MakeDensityTextureVariable()));
		Test.TestFalse(*(Label + TEXT(" has no VelocityTexture parameter without an output texture")), HasDriveParameter(NiagaraComp, MakeVelocityTextureVariable()));
	}

	void ExpectActiveState(FAutomationTestBase& Test, const UNiagaraComponent& NiagaraComp, bool bExpectActive, const FString& Label)
	{
		if (!FApp::CanEverRender())
		{
			Test.AddInfo(Label + TEXT(": 헤드리스에서는 활성 토글 미판별, PIE 도구가 검증"));
			return;
		}
		Test.TestTrue(*Label, NiagaraComp.IsActive() == bExpectActive);
	}

	constexpr float ExpectedVelocityReferenceCmPerSec = 600.0f;
	const TCHAR* const PlaneBasisVariableNames[] = {
		TEXT("SimCenterPlane"),
		TEXT("PlaneAxisU"),
		TEXT("PlaneAxisV"),
		TEXT("PlaneAxisN"),
		TEXT("PlaneCoordinateOffsetCm"),
		TEXT("VelocityReferenceCmPerSec"),
	};

	class FTDNiagaraDriveWarningCapture : public FOutputDevice
	{
	public:
		FTDNiagaraDriveWarningCapture()
		{
			GLog->AddOutputDevice(this);
		}

		virtual ~FTDNiagaraDriveWarningCapture() override
		{
			GLog->RemoveOutputDevice(this);
		}

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if ((Verbosity & ELogVerbosity::VerbosityMask) > ELogVerbosity::Warning)
			{
				return;
			}

			const FString Line(Message);
			for (const TCHAR* VariableName : PlaneBasisVariableNames)
			{
				if (Line.Contains(VariableName))
				{
					MatchedLines.Add(Line);
					return;
				}
			}
		}

		TArray<FString> MatchedLines;
	};

	void ExpectVec2Parameter(FAutomationTestBase& Test, const UNiagaraComponent& NiagaraComp, const TCHAR* VariableName, const FVector2D& Expected, const FString& Label)
	{
		const FNiagaraVariable Variable(FNiagaraTypeDefinition::GetVec2Def(), FName(VariableName));
		if (!Test.TestTrue(*FString::Printf(TEXT("%s has %s"), *Label, VariableName), HasDriveParameter(NiagaraComp, Variable)))
		{
			return;
		}

		const FVector2f Value = NiagaraComp.GetOverrideParameters().GetParameterValue<FVector2f>(Variable);
		Test.TestNearlyEqual(*FString::Printf(TEXT("%s %s X"), *Label, VariableName), Value.X, static_cast<float>(Expected.X), 0.01f);
		Test.TestNearlyEqual(*FString::Printf(TEXT("%s %s Y"), *Label, VariableName), Value.Y, static_cast<float>(Expected.Y), 0.01f);
	}

	void ExpectVec3Parameter(FAutomationTestBase& Test, const UNiagaraComponent& NiagaraComp, const TCHAR* VariableName, const FVector& Expected, const FString& Label)
	{
		const FNiagaraVariable Variable(FNiagaraTypeDefinition::GetVec3Def(), FName(VariableName));
		if (!Test.TestTrue(*FString::Printf(TEXT("%s has %s"), *Label, VariableName), HasDriveParameter(NiagaraComp, Variable)))
		{
			return;
		}

		const FVector3f Value = NiagaraComp.GetOverrideParameters().GetParameterValue<FVector3f>(Variable);
		Test.TestNearlyEqual(*FString::Printf(TEXT("%s %s X"), *Label, VariableName), Value.X, static_cast<float>(Expected.X), 1e-4f);
		Test.TestNearlyEqual(*FString::Printf(TEXT("%s %s Y"), *Label, VariableName), Value.Y, static_cast<float>(Expected.Y), 1e-4f);
		Test.TestNearlyEqual(*FString::Printf(TEXT("%s %s Z"), *Label, VariableName), Value.Z, static_cast<float>(Expected.Z), 1e-4f);
	}

	void ExpectPlaneBasisParameters(FAutomationTestBase& Test, const UNiagaraComponent& NiagaraComp, const UTDFluidLiveComponent& FluidComponent, const FString& Label)
	{
		const FTDFluidPlaneBasis& Basis = FluidComponent.GetPlaneBasis();
		ExpectVec2Parameter(Test, NiagaraComp, TEXT("User.SimCenterPlane"), FluidComponent.GetSimulationCenterWorld(), Label);
		ExpectVec3Parameter(Test, NiagaraComp, TEXT("User.PlaneAxisU"), Basis.U, Label);
		ExpectVec3Parameter(Test, NiagaraComp, TEXT("User.PlaneAxisV"), Basis.V, Label);
		ExpectVec3Parameter(Test, NiagaraComp, TEXT("User.PlaneAxisN"), Basis.N, Label);
		ExpectVec2Parameter(Test, NiagaraComp, TEXT("User.PlaneCoordinateOffsetCm"), Basis.PlaneCoordinateOffsetCm, Label);

		const FNiagaraVariable ReferenceVariable(FNiagaraTypeDefinition::GetFloatDef(), FName(TEXT("User.VelocityReferenceCmPerSec")));
		if (Test.TestTrue(*(Label + TEXT(" has User.VelocityReferenceCmPerSec")), HasDriveParameter(NiagaraComp, ReferenceVariable)))
		{
			Test.TestNearlyEqual(*(Label + TEXT(" VelocityReferenceCmPerSec is the velocity texture encoding reference")), NiagaraComp.GetOverrideParameters().GetParameterValue<float>(ReferenceVariable), ExpectedVelocityReferenceCmPerSec, 1e-4f);
		}
	}

	struct FStepCostResult
	{
		double AverageMilliseconds = 0.0;
		double MaximumMilliseconds = 0.0;
		int32 StepCount = 0;
	};

	FStepCostResult MeasureStepCost(UTDFluidLiveComponent& FluidComponent, const FTDFluidSimulationConfig& Config)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TDFluidBench_Case);

		constexpr int32 WarmupStepCount = 30;
		constexpr int32 MeasuredStepCount = 120;
		constexpr int32 BrushCount = 4;
		constexpr float StepSeconds = 1.0f / 60.0f;
		constexpr float BrushOrbitRadiusCm = 700.0f;
		constexpr float BrushSpeedCmPerSec = 600.0f;

		FluidComponent.ApplySimulationConfig(Config);
		const ETDFluidBrushChannel Channel = Config.SimMode == ETDFluidSimMode::FluidSim ? ETDFluidBrushChannel::ChannelA_Water : ETDFluidBrushChannel::ChannelR_Mud;

		FStepCostResult Result;
		double TotalSeconds = 0.0;
		double MaximumSeconds = 0.0;
		for (int32 Step = 0; Step < WarmupStepCount + MeasuredStepCount; ++Step)
		{
			for (int32 BrushIndex = 0; BrushIndex < BrushCount; ++BrushIndex)
			{
				const float Angle = static_cast<float>(Step * 0.07 + BrushIndex * UE_HALF_PI);
				FTDFluidBrushStamp Stamp;
				Stamp.WorldLocation = FVector(FMath::Cos(Angle) * BrushOrbitRadiusCm, FMath::Sin(Angle) * BrushOrbitRadiusCm, 0.0f);
				Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0f) * BrushSpeedCmPerSec;
				Stamp.RadiusCm = 150.0f;
				Stamp.DensityStrength = 1.0f;
				Stamp.VelocityStrength = 1.0f;
				Stamp.Channel = Channel;
				FluidComponent.QueueBrushStamp(Stamp);
			}

			const double StartSeconds = FPlatformTime::Seconds();
			FluidComponent.AdvanceSimulationStep(StepSeconds, FVector2D::ZeroVector, false);
			const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			if (Step < WarmupStepCount)
			{
				continue;
			}

			TotalSeconds += ElapsedSeconds;
			MaximumSeconds = FMath::Max(MaximumSeconds, ElapsedSeconds);
			++Result.StepCount;
		}

		Result.AverageMilliseconds = Result.StepCount > 0 ? TotalSeconds * 1000.0 / Result.StepCount : 0.0;
		Result.MaximumMilliseconds = MaximumSeconds * 1000.0;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraTaggedDriveTest,
	"TDGame.FluidFX.Niagara.TaggedActorReceivesCenterExtentsAndTextures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraTaggedDriveTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* Preset = CreateWaterPreset(TestWorld.World, nullptr, DriveTagName);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, FVector(500.0f, -300.0f, 120.0f), 600.0f);
	UNiagaraComponent* TaggedNiagara = nullptr;
	AActor* TaggedActor = SpawnTaggedNiagaraActor(TestWorld.World, FVector(1200.0f, 800.0f, 777.0f), TaggedNiagara);
	if (!TestNotNull(TEXT("Spawned LiveActor"), LiveActor) || !TestNotNull(TEXT("Spawned tagged actor"), TaggedActor) || !TestNotNull(TEXT("Tagged actor has a Niagara component"), TaggedNiagara))
	{
		return false;
	}

	TestFalse(TEXT("The tagged component has no center parameter before the first step"), HasDriveParameter(*TaggedNiagara, MakeCenterVariable()));

	TestWorld.TickWorld(0.15f);
	ExpectDrivenParameters(*this, *TaggedNiagara, *LiveActor->LiveComponent, 777.0f, TEXT("Tagged component"));

	TaggedActor->SetActorLocation(FVector(1200.0f, 800.0f, 905.0f));
	TestWorld.TickWorld(0.1f);
	ExpectDrivenParameters(*this, *TaggedNiagara, *LiveActor->LiveComponent, 905.0f, TEXT("Tagged component after moving"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraPlaneBasisDriveTest,
	"TDGame.FluidFX.Niagara.DrivesPlaneBasisAndVelocityReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraPlaneBasisDriveTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	FTDNiagaraDriveWarningCapture WarningCapture;
	UTDFluidPresetAsset* TaggedPreset = CreateWaterPreset(TestWorld.World, nullptr, DriveTagName);
	ATDFluidLiveActor* TaggedLiveActor = Subsystem->SpawnPooledFluidActor(TaggedPreset, FVector(500.0f, -300.0f, 120.0f), 600.0f);
	UNiagaraComponent* TaggedNiagara = nullptr;
	AActor* TaggedActor = SpawnTaggedNiagaraActor(TestWorld.World, FVector(1200.0f, 800.0f, 777.0f), TaggedNiagara);
	if (!TestNotNull(TEXT("Spawned the tagged-drive LiveActor"), TaggedLiveActor) || !TestNotNull(TEXT("Spawned tagged actor"), TaggedActor) || !TestNotNull(TEXT("Tagged actor has a Niagara component"), TaggedNiagara))
	{
		return false;
	}

	TestWorld.TickWorld(0.15f);
	ExpectPlaneBasisParameters(*this, *TaggedNiagara, *TaggedLiveActor->LiveComponent, TEXT("Tagged component without a system"));

	UTDFluidPresetAsset* OwnedPreset = CreateWaterPreset(TestWorld.World, WaterTrailSystemPath);
	ATDFluidLiveActor* OwnedLiveActor = Subsystem->SpawnPooledFluidActor(OwnedPreset, FVector(-900.0f, 400.0f, 250.0f), 600.0f);
	if (!TestNotNull(TEXT("Spawned the owned-system LiveActor"), OwnedLiveActor))
	{
		return false;
	}

	UNiagaraComponent* OwnedNiagara = WaitForOwnedNiagaraComponent(TestWorld, OwnedLiveActor);
	if (!TestNotNull(TEXT("The preset Niagara system spawns an owned component after the async load"), OwnedNiagara))
	{
		return false;
	}

	TestWorld.TickWorld(0.15f);
	GLog->FlushThreadedLogs();
	for (const FString& Line : WarningCapture.MatchedLines)
	{
		AddInfo(TEXT("Captured plane user variable log: ") + Line);
	}
	TestEqual(TEXT("Setting plane user variables that a system lacks logs no warning or error"), WarningCapture.MatchedLines.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraTagAddedLaterTest,
	"TDGame.FluidFX.Niagara.TagAddedLaterIsDrivenAfterCacheRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraTagAddedLaterTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* Preset = CreateWaterPreset(TestWorld.World, nullptr, DriveTagName);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, FVector(0.0f, 0.0f, 50.0f), 600.0f);
	if (!TestNotNull(TEXT("Spawned LiveActor"), LiveActor))
	{
		return false;
	}

	TestWorld.TickWorld(0.15f);

	UNiagaraComponent* LateNiagara = nullptr;
	AActor* LateActor = SpawnTaggedNiagaraActor(TestWorld.World, FVector(300.0f, 300.0f, 410.0f), LateNiagara);
	if (!TestNotNull(TEXT("Spawned late tagged actor"), LateActor) || !TestNotNull(TEXT("Late tagged actor has a Niagara component"), LateNiagara))
	{
		return false;
	}

	TestWorld.TickWorld(0.5f);
	TestFalse(TEXT("A tag added after the cache refresh is not driven before the refresh interval"), HasDriveParameter(*LateNiagara, MakeCenterVariable()));

	TestWorld.TickWorld(0.8f);
	ExpectDrivenParameters(*this, *LateNiagara, *LiveActor->LiveComponent, 410.0f, TEXT("Late tagged component"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraTagDestroyedTest,
	"TDGame.FluidFX.Niagara.DestroyedTaggedActorOrComponentKeepsDriving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraTagDestroyedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* Preset = CreateWaterPreset(TestWorld.World, nullptr, DriveTagName);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 600.0f);
	UNiagaraComponent* FirstNiagara = nullptr;
	UNiagaraComponent* SecondNiagara = nullptr;
	AActor* FirstActor = SpawnTaggedNiagaraActor(TestWorld.World, FVector(100.0f, 0.0f, 10.0f), FirstNiagara);
	AActor* SecondActor = SpawnTaggedNiagaraActor(TestWorld.World, FVector(200.0f, 0.0f, 20.0f), SecondNiagara);
	if (!TestNotNull(TEXT("Spawned LiveActor"), LiveActor) || !TestNotNull(TEXT("Spawned first tagged actor"), FirstActor) || !TestNotNull(TEXT("Spawned second tagged actor"), SecondActor))
	{
		return false;
	}

	TestWorld.TickWorld(0.15f);
	TestTrue(TEXT("The first tagged component is driven before it is destroyed"), HasDriveParameter(*FirstNiagara, MakeCenterVariable()));

	FirstActor->Destroy();
	TestWorld.TickWorld(1.5f);
	ExpectDrivenParameters(*this, *SecondNiagara, *LiveActor->LiveComponent, 20.0f, TEXT("Second tagged component after the first actor was destroyed"));

	SecondNiagara->DestroyComponent();
	TestWorld.TickWorld(1.5f);

	UNiagaraComponent* ThirdNiagara = nullptr;
	AActor* ThirdActor = SpawnTaggedNiagaraActor(TestWorld.World, FVector(300.0f, 0.0f, 30.0f), ThirdNiagara);
	if (!TestNotNull(TEXT("Spawned third tagged actor"), ThirdActor) || !TestNotNull(TEXT("Third tagged actor has a Niagara component"), ThirdNiagara))
	{
		return false;
	}

	TestWorld.TickWorld(1.2f);
	ExpectDrivenParameters(*this, *ThirdNiagara, *LiveActor->LiveComponent, 30.0f, TEXT("Third tagged component after a component was destroyed"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraPresetSpawnTest,
	"TDGame.FluidFX.Niagara.PresetSystemSpawnsAttachedOwnedComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraPresetSpawnTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* Preset = CreateWaterPreset(TestWorld.World, WaterTrailSystemPath);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, FVector(0.0f, 0.0f, 250.0f), 600.0f);
	if (!TestNotNull(TEXT("Spawned LiveActor"), LiveActor))
	{
		return false;
	}

	UNiagaraComponent* OwnedNiagara = WaitForOwnedNiagaraComponent(TestWorld, LiveActor);
	if (!TestNotNull(TEXT("The preset Niagara system spawns an owned component after the async load"), OwnedNiagara))
	{
		return false;
	}

	TestTrue(TEXT("The owned component belongs to the live actor"), OwnedNiagara->GetOwner() == LiveActor);
	TestTrue(TEXT("The owned component attaches to the live actor root"), OwnedNiagara->GetAttachParent() == LiveActor->GetRootComponent());
	TestEqual(TEXT("Exactly one Niagara component exists on the live actor"), CountValidNiagaraComponents(LiveActor), 1);
	if (TestNotNull(TEXT("The owned component has the preset system"), OwnedNiagara->GetAsset()))
	{
		TestEqual(TEXT("The owned component runs the preset system"), OwnedNiagara->GetAsset()->GetPathName(), FSoftObjectPath(WaterTrailSystemPath).ToString());
	}

	TestWorld.TickWorld(0.15f);
	ExpectDrivenParameters(*this, *OwnedNiagara, *LiveActor->LiveComponent, 250.0f, TEXT("Owned component"));
	TestTrue(TEXT("The owned component is reused while the system stays the same"), LiveActor->LiveComponent->GetOwnedNiagaraComponent() == OwnedNiagara);

	LiveActor->SetActorLocation(FVector(0.0f, 0.0f, 480.0f));
	TestWorld.TickWorld(0.1f);
	ExpectDrivenParameters(*this, *OwnedNiagara, *LiveActor->LiveComponent, 480.0f, TEXT("Owned component after moving"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraPresetSleepWakeTest,
	"TDGame.FluidFX.Niagara.PresetSystemSleepWakeAndPoolToggleActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraPresetSleepWakeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* Preset = CreateWaterPreset(TestWorld.World, WaterTrailSystemPath);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 600.0f);
	if (!TestNotNull(TEXT("Spawned LiveActor"), LiveActor))
	{
		return false;
	}

	UNiagaraComponent* OwnedNiagara = WaitForOwnedNiagaraComponent(TestWorld, LiveActor);
	if (!TestNotNull(TEXT("The preset Niagara system spawns an owned component"), OwnedNiagara))
	{
		return false;
	}

	ExpectActiveState(*this, *OwnedNiagara, true, TEXT("The owned component is active while the simulation is awake"));

	LiveActor->LiveComponent->SetSimulationAwake(false);
	ExpectActiveState(*this, *OwnedNiagara, false, TEXT("The owned component is inactive while the simulation sleeps"));
	TestTrue(TEXT("Sleeping keeps the owned component"), LiveActor->LiveComponent->GetOwnedNiagaraComponent() == OwnedNiagara);

	LiveActor->LiveComponent->SetSimulationAwake(true);
	ExpectActiveState(*this, *OwnedNiagara, true, TEXT("The owned component is active again after waking"));

	Subsystem->ReleaseFluidActorToPool(LiveActor);
	ExpectActiveState(*this, *OwnedNiagara, false, TEXT("The owned component is inactive after the pool return"));

	ATDFluidLiveActor* ReusedActor = Subsystem->SpawnPooledFluidActor(Preset, FVector(40.0f, 0.0f, 0.0f), 600.0f);
	TestTrue(TEXT("The pooled actor is reused"), ReusedActor == LiveActor);
	ExpectActiveState(*this, *OwnedNiagara, true, TEXT("The owned component is active after the pooled actor is reused"));
	TestTrue(TEXT("Reusing the pooled actor with the same system keeps the owned component"), LiveActor->LiveComponent->GetOwnedNiagaraComponent() == OwnedNiagara);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraPresetChangeTest,
	"TDGame.FluidFX.Niagara.PresetSystemChangeOrNullDestroysOldComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraPresetChangeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* WaterPreset = CreateWaterPreset(TestWorld.World, WaterTrailSystemPath);
	UTDFluidPresetAsset* FirePreset = CreateWaterPreset(TestWorld.World, FireTrailSystemPath);
	UTDFluidPresetAsset* EmptyPreset = CreateWaterPreset(TestWorld.World, nullptr);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(WaterPreset, FVector::ZeroVector, 600.0f);
	if (!TestNotNull(TEXT("Spawned LiveActor"), LiveActor))
	{
		return false;
	}

	UNiagaraComponent* WaterNiagara = WaitForOwnedNiagaraComponent(TestWorld, LiveActor);
	if (!TestNotNull(TEXT("The water system spawns an owned component"), WaterNiagara))
	{
		return false;
	}
	const TWeakObjectPtr<UNiagaraComponent> WeakWaterNiagara(WaterNiagara);

	LiveActor->LiveComponent->InitSystemStateFromPreset(FirePreset);
	TestNull(TEXT("Switching to a different system drops the old component immediately"), LiveActor->LiveComponent->GetOwnedNiagaraComponent());
	TestFalse(TEXT("The old component is destroyed after switching systems"), WeakWaterNiagara.IsValid());

	UNiagaraComponent* FireNiagara = WaitForOwnedNiagaraComponent(TestWorld, LiveActor);
	if (!TestNotNull(TEXT("The fire system spawns a new owned component"), FireNiagara))
	{
		return false;
	}
	if (TestNotNull(TEXT("The new component has a system"), FireNiagara->GetAsset()))
	{
		TestEqual(TEXT("The new component runs the fire system"), FireNiagara->GetAsset()->GetPathName(), FSoftObjectPath(FireTrailSystemPath).ToString());
	}
	TestEqual(TEXT("Exactly one Niagara component exists after the switch"), CountValidNiagaraComponents(LiveActor), 1);
	const TWeakObjectPtr<UNiagaraComponent> WeakFireNiagara(FireNiagara);

	LiveActor->LiveComponent->InitSystemStateFromPreset(EmptyPreset);
	TestNull(TEXT("A preset without a system clears the owned component"), LiveActor->LiveComponent->GetOwnedNiagaraComponent());
	TestFalse(TEXT("The fire component is destroyed after the empty preset"), WeakFireNiagara.IsValid());
	TestEqual(TEXT("No Niagara component remains on the live actor"), CountValidNiagaraComponents(LiveActor), 0);

	TestWorld.TickWorld(0.2f);
	TestNull(TEXT("Ticking after the empty preset does not recreate a component"), LiveActor->LiveComponent->GetOwnedNiagaraComponent());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNiagaraActorDestroyTest,
	"TDGame.FluidFX.Niagara.PresetSystemComponentIsCleanedUpWithActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNiagaraActorDestroyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* Preset = CreateWaterPreset(TestWorld.World, WaterTrailSystemPath);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 600.0f);
	if (!TestNotNull(TEXT("Spawned LiveActor"), LiveActor))
	{
		return false;
	}

	UNiagaraComponent* OwnedNiagara = WaitForOwnedNiagaraComponent(TestWorld, LiveActor);
	if (!TestNotNull(TEXT("The preset Niagara system spawns an owned component"), OwnedNiagara))
	{
		return false;
	}
	const TWeakObjectPtr<UNiagaraComponent> WeakOwnedNiagara(OwnedNiagara);

	LiveActor->Destroy();
	TestWorld.TickWorld(0.1f);
	TestFalse(TEXT("The owned component is destroyed together with the actor"), WeakOwnedNiagara.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPerfStepCostTest,
	"TDGame.FluidFX.Perf.StepCostByResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPerfStepCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;

	struct FModeCase
	{
		ETDFluidPresetKind PresetKind;
		const TCHAR* ModeName;
		double MaximumAverageMillisecondsAt64;
		double MaximumAverageMillisecondsAt128;
		double MaximumAverageMillisecondsAt192;
		double MaximumAverageMillisecondsAt256;
	};

	const FModeCase ModeCases[] = {
		{ ETDFluidPresetKind::Water, TEXT("FluidSim"), 1.0, 3.0, 7.0, 12.0 },
		{ ETDFluidPresetKind::FootprintPainter, TEXT("SimplePainter"), 0.5, 1.5, 2.5, 4.0 },
	};
	const int32 Resolutions[] = { 64, 128, 192, 256 };

	for (const FModeCase& ModeCase : ModeCases)
	{
		double AverageAt64 = 0.0;
		double AverageAt128 = 0.0;
		double AverageAt256 = 0.0;
		for (const int32 Resolution : Resolutions)
		{
			ATDFluidLiveActor* LiveActor = TestWorld.World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
			if (!TestNotNull(TEXT("Spawned benchmark LiveActor"), LiveActor))
			{
				return false;
			}

			FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ModeCase.PresetKind);
			Config.GridResolution = Resolution;
			const FStepCostResult Cost = MeasureStepCost(*LiveActor->LiveComponent, Config);
			TestEqual(*FString::Printf(TEXT("%s benchmark solver resolution"), ModeCase.ModeName), LiveActor->LiveComponent->GetSolver().GetResolution(), Resolution);
			TestEqual(*FString::Printf(TEXT("%s %d measured step count"), ModeCase.ModeName, Resolution), Cost.StepCount, 120);

			const FString BenchLine = FString::Printf(
				TEXT("TDFluidPerfBench: mode=%s resolution=%d avg_step_ms=%.4f max_step_ms=%.4f steps=%d"),
				ModeCase.ModeName,
				Resolution,
				Cost.AverageMilliseconds,
				Cost.MaximumMilliseconds,
				Cost.StepCount);
			UE_LOG(LogTDGame, Display, TEXT("%s"), *BenchLine);
			AddInfo(BenchLine);

			if (Resolution == 64)
			{
				AverageAt64 = Cost.AverageMilliseconds;
				if (ModeCase.MaximumAverageMillisecondsAt64 > 0.0)
				{
					TestTrue(*FString::Printf(TEXT("%s 64 average step cost stays under %.1f ms"), ModeCase.ModeName, ModeCase.MaximumAverageMillisecondsAt64), Cost.AverageMilliseconds < ModeCase.MaximumAverageMillisecondsAt64);
				}
			}
			else if (Resolution == 128)
			{
				AverageAt128 = Cost.AverageMilliseconds;
				if (ModeCase.MaximumAverageMillisecondsAt128 > 0.0)
				{
					TestTrue(*FString::Printf(TEXT("%s 128 average step cost stays under %.1f ms"), ModeCase.ModeName, ModeCase.MaximumAverageMillisecondsAt128), Cost.AverageMilliseconds < ModeCase.MaximumAverageMillisecondsAt128);
				}
			}
			else if (Resolution == 192)
			{
				if (ModeCase.MaximumAverageMillisecondsAt192 > 0.0)
				{
					TestTrue(*FString::Printf(TEXT("%s 192 average step cost stays under %.1f ms"), ModeCase.ModeName, ModeCase.MaximumAverageMillisecondsAt192), Cost.AverageMilliseconds < ModeCase.MaximumAverageMillisecondsAt192);
				}
			}
			else if (Resolution == 256)
			{
				AverageAt256 = Cost.AverageMilliseconds;
				if (ModeCase.MaximumAverageMillisecondsAt256 > 0.0)
				{
					TestTrue(*FString::Printf(TEXT("%s 256 average step cost stays under %.1f ms"), ModeCase.ModeName, ModeCase.MaximumAverageMillisecondsAt256), Cost.AverageMilliseconds < ModeCase.MaximumAverageMillisecondsAt256);
				}
			}

			LiveActor->Destroy();
		}

		TestTrue(*FString::Printf(TEXT("%s step cost grows with resolution (128 / 64 ratio > 1)"), ModeCase.ModeName), AverageAt64 > 0.0 && AverageAt128 / AverageAt64 > 1.0);
		TestTrue(*FString::Printf(TEXT("%s step cost grows with resolution (256 > 64)"), ModeCase.ModeName), AverageAt256 > AverageAt64);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidPerfTickCostLogTest,
	"TDGame.FluidFX.Perf.TickCostLogFollowsConsoleVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidPerfTickCostLogTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	FPerfTestWorld TestWorld;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
	{
		return false;
	}

	FTickCostVariableScope TickCostVariable;
	if (!TestNotNull(TEXT("TD.FluidFX.LogTickCost console variable exists"), TickCostVariable.Variable))
	{
		return false;
	}
	TestEqual(TEXT("TD.FluidFX.LogTickCost defaults to 0"), TickCostVariable.PreviousValue, 0);
	TickCostVariable.SetValue(0);

	UTDFluidPresetAsset* Preset = CreateWaterPreset(TestWorld.World, nullptr);
	ATDFluidLiveActor* AwakeActor = Subsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 600.0f);
	ATDFluidLiveActor* SleepingActor = Subsystem->SpawnPooledFluidActor(Preset, FVector(9000.0f, 0.0f, 0.0f), 600.0f);
	ATDFluidLiveActor* PooledActor = Subsystem->SpawnPooledFluidActor(Preset, FVector(-9000.0f, 0.0f, 0.0f), 600.0f);
	if (!TestNotNull(TEXT("Spawned awake actor"), AwakeActor) || !TestNotNull(TEXT("Spawned sleeping actor"), SleepingActor) || !TestNotNull(TEXT("Spawned pooled actor"), PooledActor))
	{
		return false;
	}
	SleepingActor->LiveComponent->SetSimulationAwake(false);
	Subsystem->ReleaseFluidActorToPool(PooledActor);

	FPerfLogCapture Capture;

	TestWorld.TickWorld(2.6f);
	GLog->FlushThreadedLogs();
	TestEqual(TEXT("No tick cost line is logged while the console variable is 0"), Capture.CapturedLines.Num(), 0);

	TickCostVariable.SetValue(1);
	TestWorld.TickWorld(2.6f);
	GLog->FlushThreadedLogs();
	if (!TestEqual(TEXT("Exactly one tick cost line is logged after 2 seconds of world time"), Capture.CapturedLines.Num(), 1))
	{
		return false;
	}

	const FRegexPattern LinePattern(TEXT("^TDFluidPerf: frames=(\\d+) avg_tick_ms=(\\d+\\.\\d{4}) max_tick_ms=(\\d+\\.\\d{4}) avg_frame_ms=(\\d+\\.\\d{3}) awake_actors=(\\d+) total_cells=(\\d+)$"));
	FRegexMatcher LineMatcher(LinePattern, Capture.CapturedLines[0]);
	if (!TestTrue(TEXT("The tick cost line matches the contract format"), LineMatcher.FindNext()))
	{
		AddError(Capture.CapturedLines[0]);
		return false;
	}
	AddInfo(Capture.CapturedLines[0]);

	const int32 FrameCount = FCString::Atoi(*LineMatcher.GetCaptureGroup(1));
	const double AverageTickMilliseconds = FCString::Atod(*LineMatcher.GetCaptureGroup(2));
	const double MaximumTickMilliseconds = FCString::Atod(*LineMatcher.GetCaptureGroup(3));
	const double AverageFrameMilliseconds = FCString::Atod(*LineMatcher.GetCaptureGroup(4));
	const int32 AwakeActorCount = FCString::Atoi(*LineMatcher.GetCaptureGroup(5));
	const int32 TotalCellCount = FCString::Atoi(*LineMatcher.GetCaptureGroup(6));
	const int32 AwakeResolution = AwakeActor->LiveComponent->GetSolver().GetResolution();

	TestTrue(TEXT("The line covers about 2 seconds of 50 ms ticks"), FrameCount >= 39 && FrameCount <= 41);
	TestTrue(TEXT("The tick cost is not negative and the maximum is not below the average"), AverageTickMilliseconds >= 0.0 && MaximumTickMilliseconds >= AverageTickMilliseconds);
	TestTrue(TEXT("The average frame time is the 50 ms world delta"), AverageFrameMilliseconds > 49.0 && AverageFrameMilliseconds < 51.0);
	TestEqual(TEXT("Only the awake non-pooled actor is counted"), AwakeActorCount, 1);
	TestEqual(TEXT("Total cells is the squared resolution of the awake actor"), TotalCellCount, AwakeResolution * AwakeResolution);

	Capture.CapturedLines.Reset();
	TickCostVariable.SetValue(0);
	TestWorld.TickWorld(0.1f);
	TickCostVariable.SetValue(1);
	TestWorld.TickWorld(1.6f);
	GLog->FlushThreadedLogs();
	TestEqual(TEXT("Switching the variable off clears the partial window, so 1.6 s after re-enabling logs nothing"), Capture.CapturedLines.Num(), 0);

	TickCostVariable.SetValue(0);
	TestWorld.TickWorld(2.6f);
	GLog->FlushThreadedLogs();
	TestEqual(TEXT("No tick cost line is logged after switching the variable back to 0"), Capture.CapturedLines.Num(), 0);

	return true;
}

namespace TDFluidPerfNiagaraTest
{
	struct FTDFluidActorStepChecksums
	{
		TArray<uint32> StateChecksums;
		TArray<uint32> PixelChecksums;
		int32 ProcessedStampCount = 0;
	};

	FTDFluidActorStepChecksums RunFluidWorldWithParallelActorSteps(IConsoleVariable& ParallelVariable, int32 ParallelValue)
	{
		constexpr ETDFluidPresetKind PresetKinds[] = { ETDFluidPresetKind::Water, ETDFluidPresetKind::Mud, ETDFluidPresetKind::Fire, ETDFluidPresetKind::FootprintPainter };
		constexpr int32 ActorCount = UE_ARRAY_COUNT(PresetKinds);
		constexpr int32 FrameCount = 30;
		constexpr float FrameSeconds = 0.05f;

		ParallelVariable.Set(ParallelValue, ECVF_SetByCode);

		FTDFluidActorStepChecksums Result;
		FPerfTestWorld TestWorld;
		UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
		if (!Subsystem)
		{
			return Result;
		}

		TArray<ATDFluidLiveActor*> LiveActors;
		for (int32 ActorIndex = 0; ActorIndex < ActorCount; ++ActorIndex)
		{
			UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, PresetKinds[ActorIndex]);
			ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, FVector(ActorIndex * 6000.0, 0.0, 0.0), 600.0f);
			if (!LiveActor)
			{
				return Result;
			}
			LiveActors.Add(LiveActor);
		}

		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			for (int32 ActorIndex = 0; ActorIndex < ActorCount; ++ActorIndex)
			{
				const double Angle = Frame * 0.2 + ActorIndex;
				FTDFluidBrushStamp Stamp;
				Stamp.WorldLocation = LiveActors[ActorIndex]->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * 500.0;
				Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 700.0;
				Stamp.RadiusCm = 200.0f;
				Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
				LiveActors[ActorIndex]->LiveComponent->QueueBrushStamp(Stamp);
			}

			TestWorld.TickWorld(FrameSeconds, FrameSeconds);
			for (const ATDFluidLiveActor* LiveActor : LiveActors)
			{
				Result.ProcessedStampCount += LiveActor->LiveComponent->GetLastProcessedPointCount();
			}
		}

		for (const ATDFluidLiveActor* LiveActor : LiveActors)
		{
			const UTDFluidLiveComponent& FluidComponent = *LiveActor->LiveComponent;
			const TArray<FFloat16Color>& DensityPressurePixels = FluidComponent.GetPackedDensityPressurePixels();
			const TArray<FFloat16Color>& VelocityPixels = FluidComponent.GetPackedVelocityPixels();
			const uint32 DensityPressureChecksum = FCrc::MemCrc32(DensityPressurePixels.GetData(), DensityPressurePixels.Num() * sizeof(FFloat16Color));
			Result.StateChecksums.Add(FluidComponent.GetSolver().ComputeStateChecksum());
			Result.PixelChecksums.Add(FCrc::MemCrc32(VelocityPixels.GetData(), VelocityPixels.Num() * sizeof(FFloat16Color), DensityPressureChecksum));
		}
		return Result;
	}
}

namespace TDFluidPerfNiagaraTest
{
	AActor* SpawnMovableMarkerActor(UWorld* World)
	{
		AActor* MarkerActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!MarkerActor)
		{
			return nullptr;
		}

		USceneComponent* MarkerRoot = NewObject<USceneComponent>(MarkerActor);
		MarkerActor->AddInstanceComponent(MarkerRoot);
		MarkerActor->SetRootComponent(MarkerRoot);
		MarkerRoot->RegisterComponent();
		return MarkerActor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFollowChainTest,
	"TDGame.FluidFX.Perf.FollowChainSeesSameFrameMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFollowChainTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	constexpr int32 FrameCount = 8;
	constexpr double TargetStepCm = 400.0;

	IConsoleVariable* ParallelVariable = IConsoleManager::Get().FindConsoleVariable(ParallelActorStepsVariableName);
	if (!TestNotNull(TEXT("TD.FluidFX.ParallelActorSteps console variable exists"), ParallelVariable))
	{
		return false;
	}
	const int32 PreviousValue = ParallelVariable->GetInt();

	for (const int32 ParallelValue : { 0, 1 })
	{
		ParallelVariable->Set(ParallelValue, ECVF_SetByCode);

		FPerfTestWorld TestWorld;
		UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
		AActor* MovingTarget = SpawnMovableMarkerActor(TestWorld.World);
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
		ATDFluidLiveActor* ChainFollower = Subsystem ? Subsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 600.0f) : nullptr;
		ATDFluidLiveActor* LeadFollower = Subsystem ? Subsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 600.0f) : nullptr;
		if (!TestNotNull(TEXT("Moving target"), MovingTarget) || !TestNotNull(TEXT("Chain follower"), ChainFollower) || !TestNotNull(TEXT("Lead follower"), LeadFollower))
		{
			ParallelVariable->Set(PreviousValue, ECVF_SetByCode);
			return false;
		}

		LeadFollower->bShouldFollowPlayerPawn = true;
		LeadFollower->FollowTargetActor = MovingTarget;
		ChainFollower->bShouldFollowPlayerPawn = true;
		ChainFollower->FollowTargetActor = LeadFollower;

		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			MovingTarget->SetActorLocation(FVector((Frame + 1) * TargetStepCm, 0.0, 0.0));
			TestWorld.TickWorld(0.05f, 0.05f);

			const double LeadLocationX = LeadFollower->GetActorLocation().X;
			const double ChainCenterX = ChainFollower->LiveComponent->GetSolver().GetCenterWorld().X;
			const double ChainCellSizeCm = ChainFollower->LiveComponent->GetSolver().GetExtentCm().X / ChainFollower->LiveComponent->GetSolver().GetResolution();
			TestTrue(
				*FString::Printf(TEXT("ParallelActorSteps=%d frame %d: the lead moved toward the target"), ParallelValue, Frame),
				LeadLocationX > Frame * TargetStepCm);
			TestTrue(
				*FString::Printf(TEXT("ParallelActorSteps=%d frame %d: the chained follower centers on the lead's location of the same frame (lead %.1f, chain %.1f)"), ParallelValue, Frame, LeadLocationX, ChainCenterX),
				FMath::Abs(LeadLocationX - ChainCenterX) <= ChainCellSizeCm * 0.5 + 0.01);
		}
	}

	ParallelVariable->Set(PreviousValue, ECVF_SetByCode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidParallelActorStepsTest,
	"TDGame.FluidFX.Perf.ParallelActorStepsMatchSerialChecksums",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidParallelActorStepsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidPerfNiagaraTest;

	constexpr int32 ExpectedActorCount = 4;
	constexpr int32 ExpectedStampCount = 4 * 30;

	IConsoleVariable* ParallelVariable = IConsoleManager::Get().FindConsoleVariable(ParallelActorStepsVariableName);
	if (!TestNotNull(TEXT("TD.FluidFX.ParallelActorSteps console variable exists"), ParallelVariable))
	{
		return false;
	}
	const int32 PreviousValue = ParallelVariable->GetInt();
	TestEqual(TEXT("TD.FluidFX.ParallelActorSteps defaults to 1"), PreviousValue, 1);

	const FTDFluidActorStepChecksums SerialChecksums = RunFluidWorldWithParallelActorSteps(*ParallelVariable, 0);
	const FTDFluidActorStepChecksums ParallelChecksums = RunFluidWorldWithParallelActorSteps(*ParallelVariable, 1);
	ParallelVariable->Set(PreviousValue, ECVF_SetByCode);

	if (!TestEqual(TEXT("The serial run stepped four actors"), SerialChecksums.StateChecksums.Num(), ExpectedActorCount)
		|| !TestEqual(TEXT("The parallel run stepped four actors"), ParallelChecksums.StateChecksums.Num(), ExpectedActorCount))
	{
		return false;
	}

	TestEqual(TEXT("The serial run processed every queued stamp"), SerialChecksums.ProcessedStampCount, ExpectedStampCount);
	TestEqual(TEXT("The parallel run processed every queued stamp"), ParallelChecksums.ProcessedStampCount, ExpectedStampCount);
	for (int32 ActorIndex = 0; ActorIndex < ExpectedActorCount; ++ActorIndex)
	{
		TestEqual(*FString::Printf(TEXT("Actor %d solver state is bit-identical between serial and parallel steps"), ActorIndex), ParallelChecksums.StateChecksums[ActorIndex], SerialChecksums.StateChecksums[ActorIndex]);
		TestEqual(*FString::Printf(TEXT("Actor %d packed pixels are bit-identical between serial and parallel steps"), ActorIndex), ParallelChecksums.PixelChecksums[ActorIndex], SerialChecksums.PixelChecksums[ActorIndex]);
	}

	return true;
}

#endif
