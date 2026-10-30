#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidUnderwaterSubsystem.h"
#include "FluidFX/TDFluidWaterPropTypes.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace TDFluidUnderwaterTestPrivate
{
	constexpr float FrameSeconds = 1.0f / 60.0f;
	constexpr int32 SettleFrameCount = 120;
	constexpr float RegionSurfaceHeightCm = 100.0f;
	constexpr float BlendTolerance = 1.0e-6f;
	constexpr int32 CostTickCount = 1000;
	constexpr int32 CostRegionsPerAxis = 4;
	const TCHAR* const SwitchConsoleName = TEXT("TD.FluidFX.UnderwaterSwitch");

	struct FTDFluidUnderwaterTestWorld
	{
		FTDFluidUnderwaterTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidUnderwaterTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			Subsystem = World->GetSubsystem<UTDFluidUnderwaterSubsystem>();
		}

		~FTDFluidUnderwaterTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		void TickSubsystem(int32 FrameCount)
		{
			for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
			{
				Subsystem->Tick(FrameSeconds);
			}
		}

		UWorld* World = nullptr;
		UTDFluidUnderwaterSubsystem* Subsystem = nullptr;
	};

	FTDFluidWaterRegion MakeUnderwaterRegion(const FVector2D& MinXY, const FVector2D& MaxXY, float SurfaceHeightCm)
	{
		FTDFluidWaterRegion Region;
		Region.MinXY = MinXY;
		Region.MaxXY = MaxXY;
		Region.SurfaceHeightCm = SurfaceHeightCm;
		return Region;
	}

	AActor* SpawnUnderwaterRegionActor(UWorld& World, const FVector& Center, const FVector& HalfExtentCm)
	{
		AActor* RegionActor = World.SpawnActor<AActor>();
		if (!RegionActor)
		{
			return nullptr;
		}

		UBoxComponent* Box = NewObject<UBoxComponent>(RegionActor);
		Box->SetBoxExtent(HalfExtentCm);
		RegionActor->SetRootComponent(Box);
		Box->RegisterComponent();
		RegionActor->SetActorLocation(Center);
		RegionActor->Tags.Add(UTDFluidUnderwaterSubsystem::WaterRegionTag);
		return RegionActor;
	}

	APostProcessVolume* SpawnUnderwaterUnboundVolume(UWorld& World, UMaterialInterface& Blendable, bool bIsTagged)
	{
		APostProcessVolume* Volume = World.SpawnActor<APostProcessVolume>();
		if (!Volume)
		{
			return nullptr;
		}

		Volume->bUnbound = true;
		Volume->BlendWeight = 1.0f;
		Volume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, &Blendable));
		if (bIsTagged)
		{
			Volume->Tags.Add(UTDFluidUnderwaterSubsystem::UnderwaterVolumeTag);
		}
		return Volume;
	}

	UMaterial* CreateUnderwaterTransientMaterial()
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Material->MaterialDomain = MD_PostProcess;
		return Material;
	}

	UObject* ReadFirstBlendable(const APostProcessVolume& Volume)
	{
		const TArray<FWeightedBlendable>& Blendables = Volume.Settings.WeightedBlendables.Array;
		return Blendables.IsEmpty() ? nullptr : ToRawPtr(Blendables[0].Object);
	}

	double ReadUnderwaterPercentile(TArray<double> Samples, double Fraction)
	{
		if (Samples.IsEmpty())
		{
			return 0.0;
		}

		Samples.Sort();
		const int32 SampleIndex = FMath::Clamp(FMath::CeilToInt32(Fraction * Samples.Num()) - 1, 0, Samples.Num() - 1);
		return Samples[SampleIndex];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidUnderwaterSurfaceAndHysteresisTest,
	"TDGame.FluidFX.Underwater.SurfaceAndHysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidUnderwaterSurfaceAndHysteresisTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidUnderwaterTestPrivate;

	const TArray<FTDFluidWaterRegion> Regions = {
		MakeUnderwaterRegion(FVector2D(-100.0, -100.0), FVector2D(100.0, 100.0), 100.0f),
		MakeUnderwaterRegion(FVector2D(0.0, 0.0), FVector2D(200.0, 200.0), 140.0f)
	};

	float SurfaceHeightCm = 0.0f;
	TestTrue(TEXT("overlap has a surface"), UTDFluidUnderwaterSubsystem::ResolveSurfaceHeightAt(Regions, FVector2D(50.0, 50.0), SurfaceHeightCm));
	TestEqual(TEXT("overlap takes the highest surface"), SurfaceHeightCm, 140.0f);
	TestTrue(TEXT("single region has a surface"), UTDFluidUnderwaterSubsystem::ResolveSurfaceHeightAt(Regions, FVector2D(-50.0, -50.0), SurfaceHeightCm));
	TestEqual(TEXT("single region surface"), SurfaceHeightCm, 100.0f);
	TestFalse(TEXT("outside every region has no surface"), UTDFluidUnderwaterSubsystem::ResolveSurfaceHeightAt(Regions, FVector2D(500.0, 500.0), SurfaceHeightCm));
	TestFalse(TEXT("no surface is never underwater"), UTDFluidUnderwaterSubsystem::ResolveUnderwaterState(true, -1000.0, false, 0.0f, 5.0f));

	constexpr float MarginCm = 5.0f;
	TestFalse(TEXT("98 keeps above"), UTDFluidUnderwaterSubsystem::ResolveUnderwaterState(false, 98.0, true, RegionSurfaceHeightCm, MarginCm));
	TestTrue(TEXT("98 keeps under"), UTDFluidUnderwaterSubsystem::ResolveUnderwaterState(true, 98.0, true, RegionSurfaceHeightCm, MarginCm));
	TestFalse(TEXT("102 keeps above"), UTDFluidUnderwaterSubsystem::ResolveUnderwaterState(false, 102.0, true, RegionSurfaceHeightCm, MarginCm));
	TestTrue(TEXT("102 keeps under"), UTDFluidUnderwaterSubsystem::ResolveUnderwaterState(true, 102.0, true, RegionSurfaceHeightCm, MarginCm));
	TestTrue(TEXT("94 goes under"), UTDFluidUnderwaterSubsystem::ResolveUnderwaterState(false, 94.0, true, RegionSurfaceHeightCm, MarginCm));
	TestFalse(TEXT("106 goes above"), UTDFluidUnderwaterSubsystem::ResolveUnderwaterState(true, 106.0, true, RegionSurfaceHeightCm, MarginCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidUnderwaterBlendWeightStepTest,
	"TDGame.FluidFX.Underwater.BlendWeightStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidUnderwaterBlendWeightStepTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidUnderwaterTestPrivate;

	constexpr float BlendSeconds = 0.1f;
	float Weight = 0.0f;
	for (int32 StepIndex = 0; StepIndex < 5; ++StepIndex)
	{
		Weight = UTDFluidUnderwaterSubsystem::StepBlendWeight(Weight, true, FrameSeconds, BlendSeconds);
	}
	TestTrue(TEXT("five steps stay below 1"), Weight < 1.0f - BlendTolerance);
	Weight = UTDFluidUnderwaterSubsystem::StepBlendWeight(Weight, true, FrameSeconds, BlendSeconds);
	TestTrue(TEXT("six steps reach 1"), Weight >= 1.0f - BlendTolerance && Weight <= 1.0f);

	for (int32 StepIndex = 0; StepIndex < 6; ++StepIndex)
	{
		Weight = UTDFluidUnderwaterSubsystem::StepBlendWeight(Weight, false, FrameSeconds, BlendSeconds);
	}
	TestTrue(TEXT("six steps return to 0"), Weight >= 0.0f && Weight <= BlendTolerance);

	TestEqual(TEXT("zero blend jumps under"), UTDFluidUnderwaterSubsystem::StepBlendWeight(0.3f, true, FrameSeconds, 0.0f), 1.0f);
	TestEqual(TEXT("zero blend jumps above"), UTDFluidUnderwaterSubsystem::StepBlendWeight(0.3f, false, FrameSeconds, 0.0f), 0.0f);
	TestEqual(TEXT("large step clamps to 1"), UTDFluidUnderwaterSubsystem::StepBlendWeight(1.5f, true, 10.0f, BlendSeconds), 1.0f);
	TestEqual(TEXT("large step clamps to 0"), UTDFluidUnderwaterSubsystem::StepBlendWeight(-0.5f, false, 10.0f, BlendSeconds), 0.0f);

	TestFalse(TEXT("console 0 forces off"), UTDFluidUnderwaterSubsystem::ResolveSwitchEnabled(0, true));
	TestTrue(TEXT("console 1 forces on"), UTDFluidUnderwaterSubsystem::ResolveSwitchEnabled(1, false));
	TestTrue(TEXT("console -1 follows setting on"), UTDFluidUnderwaterSubsystem::ResolveSwitchEnabled(-1, true));
	TestFalse(TEXT("console -1 follows setting off"), UTDFluidUnderwaterSubsystem::ResolveSwitchEnabled(-1, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidUnderwaterSwitchDrivesTaggedUnboundVolumeTest,
	"TDGame.FluidFX.Underwater.SwitchDrivesTaggedUnboundVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidUnderwaterSwitchDrivesTaggedUnboundVolumeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidUnderwaterTestPrivate;

	IConsoleVariable* SwitchVariable = IConsoleManager::Get().FindConsoleVariable(SwitchConsoleName);
	if (!TestNotNull(TEXT("underwater switch console variable"), SwitchVariable))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		SwitchVariable->Set(-1, ECVF_SetByCode);
	};
	SwitchVariable->Set(1, ECVF_SetByCode);

	FTDFluidUnderwaterTestWorld TestWorld;
	if (!TestNotNull(TEXT("underwater subsystem in a game world"), TestWorld.Subsystem))
	{
		return false;
	}

	UTDFluidUnderwaterSubsystem& Subsystem = *TestWorld.Subsystem;
	UMaterial* Material = CreateUnderwaterTransientMaterial();
	AActor* RegionActor = SpawnUnderwaterRegionActor(*TestWorld.World, FVector(0.0, 0.0, RegionSurfaceHeightCm - 50.0f), FVector(1000.0, 1000.0, 50.0));
	APostProcessVolume* Volume = SpawnUnderwaterUnboundVolume(*TestWorld.World, *Material, false);
	if (!TestNotNull(TEXT("region actor"), RegionActor) || !TestNotNull(TEXT("post process volume"), Volume))
	{
		return false;
	}

	Subsystem.NotifyUnderwaterVolumesChanged();
	TestFalse(TEXT("untagged volume leaves the subsystem idle"), Subsystem.IsTickable());
	TestTrue(TEXT("untagged blendable is untouched"), ReadFirstBlendable(*Volume) == Material);

	Volume->Tags.Add(UTDFluidUnderwaterSubsystem::UnderwaterVolumeTag);
	Subsystem.NotifyUnderwaterVolumesChanged();
	TestTrue(TEXT("tagged volume makes the subsystem tick"), Subsystem.IsTickable());
	TestEqual(TEXT("one driven volume"), Subsystem.GetUnderwaterStats().DrivenVolumeCount, 1);
	UMaterialInstanceDynamic* DrivenMaterial = Cast<UMaterialInstanceDynamic>(ReadFirstBlendable(*Volume));
	if (!TestNotNull(TEXT("tagged blendable became a MID"), DrivenMaterial))
	{
		return false;
	}
	TestTrue(TEXT("MID parent is the original blendable"), DrivenMaterial->Parent == Material);
	TestEqual(TEXT("blend weight starts above water"), Volume->BlendWeight, 0.0f);

	Subsystem.SetCameraLocationOverrideForTests(FVector(0.0, 0.0, 50.0));
	TestWorld.TickSubsystem(SettleFrameCount);
	TestTrue(TEXT("camera at 50 is underwater"), Subsystem.GetUnderwaterStats().bIsCameraUnderwater);
	TestEqual(TEXT("camera at 50 drives weight 1"), Volume->BlendWeight, 1.0f, BlendTolerance);
	TestEqual(TEXT("MID receives the surface height"), DrivenMaterial->K2_GetScalarParameterValue(UTDFluidUnderwaterSubsystem::SurfaceHeightParameterName), RegionSurfaceHeightCm, 1.0e-3f);

	Subsystem.SetCameraLocationOverrideForTests(FVector(0.0, 0.0, 150.0));
	TestWorld.TickSubsystem(SettleFrameCount);
	TestFalse(TEXT("camera at 150 is above water"), Subsystem.GetUnderwaterStats().bIsCameraUnderwater);
	TestEqual(TEXT("camera at 150 drives weight 0"), Volume->BlendWeight, 0.0f, BlendTolerance);

	Subsystem.SetCameraLocationOverrideForTests(FVector(0.0, 0.0, 50.0));
	TestWorld.TickSubsystem(SettleFrameCount);
	TestEqual(TEXT("camera back under drives weight 1"), Volume->BlendWeight, 1.0f, BlendTolerance);
	SwitchVariable->Set(0, ECVF_SetByCode);
	TestWorld.TickSubsystem(1);
	TestEqual(TEXT("console 0 writes weight 0 at once"), Volume->BlendWeight, 0.0f);
	TestFalse(TEXT("console 0 clears the underwater state"), Subsystem.GetUnderwaterStats().bIsCameraUnderwater);
	TestTrue(TEXT("switch counted transitions"), Subsystem.GetUnderwaterStats().SwitchCount >= 2);

	Subsystem.RestoreDrivenVolumesForTests();
	TestTrue(TEXT("restore puts the original blendable back"), ReadFirstBlendable(*Volume) == Material);
	TestEqual(TEXT("restore puts the original blend weight back"), Volume->BlendWeight, 1.0f);
	TestFalse(TEXT("restored subsystem stops ticking"), Subsystem.IsTickable());
	Subsystem.ClearCameraLocationOverrideForTests();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidUnderwaterGameThreadCostTest,
	"TDGame.FluidFX.Underwater.GameThreadCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidUnderwaterGameThreadCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidUnderwaterTestPrivate;

	IConsoleVariable* SwitchVariable = IConsoleManager::Get().FindConsoleVariable(SwitchConsoleName);
	if (!TestNotNull(TEXT("underwater switch console variable"), SwitchVariable))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		SwitchVariable->Set(-1, ECVF_SetByCode);
	};
	SwitchVariable->Set(1, ECVF_SetByCode);

	FTDFluidUnderwaterTestWorld TestWorld;
	if (!TestNotNull(TEXT("underwater subsystem in a game world"), TestWorld.Subsystem))
	{
		return false;
	}

	constexpr double RegionSizeCm = 500.0;
	for (int32 RegionY = 0; RegionY < CostRegionsPerAxis; ++RegionY)
	{
		for (int32 RegionX = 0; RegionX < CostRegionsPerAxis; ++RegionX)
		{
			const double SurfaceHeightCm = 80.0 + 10.0 * static_cast<double>(RegionX + RegionY);
			const FVector Center((RegionX + 0.5) * RegionSizeCm, (RegionY + 0.5) * RegionSizeCm, SurfaceHeightCm - 100.0);
			SpawnUnderwaterRegionActor(*TestWorld.World, Center, FVector(RegionSizeCm * 0.5, RegionSizeCm * 0.5, 100.0));
		}
	}
	SpawnUnderwaterUnboundVolume(*TestWorld.World, *CreateUnderwaterTransientMaterial(), true);

	UTDFluidUnderwaterSubsystem& Subsystem = *TestWorld.Subsystem;
	Subsystem.NotifyUnderwaterVolumesChanged();
	TestEqual(TEXT("one driven volume"), Subsystem.GetUnderwaterStats().DrivenVolumeCount, 1);

	const double SpanCm = RegionSizeCm * CostRegionsPerAxis;
	TArray<double> TickMilliseconds;
	TickMilliseconds.Reserve(CostTickCount);
	for (int32 TickIndex = 0; TickIndex < CostTickCount; ++TickIndex)
	{
		const double Phase = static_cast<double>(TickIndex) / static_cast<double>(CostTickCount);
		const FVector CameraLocation(Phase * SpanCm, FMath::Frac(Phase * 3.0) * SpanCm, 110.0 + 60.0 * FMath::Sin(Phase * 40.0));
		Subsystem.SetCameraLocationOverrideForTests(CameraLocation);
		const uint64 StartCycles = FPlatformTime::Cycles64();
		Subsystem.Tick(FrameSeconds);
		TickMilliseconds.Add(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles));
	}
	Subsystem.ClearCameraLocationOverrideForTests();

	double TotalMilliseconds = 0.0;
	for (const double Milliseconds : TickMilliseconds)
	{
		TotalMilliseconds += Milliseconds;
	}
	const double MeanMilliseconds = TotalMilliseconds / static_cast<double>(FMath::Max(TickMilliseconds.Num(), 1));
	const double P95Milliseconds = ReadUnderwaterPercentile(TickMilliseconds, 0.95);
	const FTDFluidUnderwaterStats Stats = Subsystem.GetUnderwaterStats();

	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("test"), TEXT("TDGame.FluidFX.Underwater.GameThreadCost"));
	Report->SetNumberField(TEXT("regions"), CostRegionsPerAxis * CostRegionsPerAxis);
	Report->SetNumberField(TEXT("volumes"), Stats.DrivenVolumeCount);
	Report->SetNumberField(TEXT("ticks"), TickMilliseconds.Num());
	Report->SetNumberField(TEXT("mean_ms"), MeanMilliseconds);
	Report->SetNumberField(TEXT("p95_ms"), P95Milliseconds);
	Report->SetNumberField(TEXT("subsystem_max_ms"), Stats.MaxTickMilliseconds);
	Report->SetNumberField(TEXT("queries"), Stats.QueryCount);
	Report->SetNumberField(TEXT("switches"), Stats.SwitchCount);
	FString ReportText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportText);
	FJsonSerializer::Serialize(Report, Writer);
	const FString CostPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("w5-underwater-cost.json"));
	TestTrue(TEXT("wrote the underwater cost report"), FFileHelper::SaveStringToFile(ReportText, *CostPath));
	AddInfo(FString::Printf(TEXT("TDFluidUnderwaterCost: mean %.5f ms, p95 %.5f ms, queries %d, switches %d -> %s"), MeanMilliseconds, P95Milliseconds, Stats.QueryCount, Stats.SwitchCount, *CostPath));
	TestFalse(TEXT("mean cost is a number"), FMath::IsNaN(MeanMilliseconds));
	TestFalse(TEXT("p95 cost is a number"), FMath::IsNaN(P95Milliseconds));
	return true;
}

#endif
