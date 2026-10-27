#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTrackSourceComponent.h"
#include "FluidFX/TDFluidTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidTrackInputTestPrivate
{
	constexpr float TrackRampPitchDegrees = 15.0f;
	constexpr double TrackGroundTraceHalfSpanCm = 2000.0;
	constexpr double TrackActorHoverCm = 100.0;

	struct FTDTrackInputTestWorld
	{
		FTDTrackInputTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidTrackInputWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDTrackInputTestWorld()
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

		void TickSteps(int32 StepCount, float StepSeconds = 0.05f)
		{
			for (int32 Step = 0; Step < StepCount; ++Step)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, StepSeconds);
			}
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	AActor* SpawnStaticBlock(UWorld* World, UStaticMesh* Mesh, const FVector& Center, const FRotator& Rotation, const FVector& Scale)
	{
		AActor* Block = World->SpawnActor<AActor>();
		if (!Block)
		{
			return nullptr;
		}

		UStaticMeshComponent* BlockMesh = NewObject<UStaticMeshComponent>(Block);
		Block->AddInstanceComponent(BlockMesh);
		Block->SetRootComponent(BlockMesh);
		BlockMesh->SetMobility(EComponentMobility::Static);
		BlockMesh->SetStaticMesh(Mesh);
		BlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BlockMesh->SetCollisionObjectType(ECC_WorldStatic);
		BlockMesh->SetCollisionResponseToAllChannels(ECR_Block);
		BlockMesh->SetWorldTransform(FTransform(Rotation, Center, Scale));
		BlockMesh->RegisterComponent();
		return Block;
	}

	bool TraceGroundHeightCm(UWorld* World, const FVector2D& PlanarLocation, double& OutGroundCm)
	{
		FHitResult Hit;
		const FVector TraceStart(PlanarLocation.X, PlanarLocation.Y, TrackGroundTraceHalfSpanCm);
		const FVector TraceEnd(PlanarLocation.X, PlanarLocation.Y, -TrackGroundTraceHalfSpanCm);
		if (!World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, FCollisionObjectQueryParams(ECC_WorldStatic)))
		{
			return false;
		}
		OutGroundCm = Hit.ImpactPoint.Z;
		return true;
	}

	AActor* SpawnTrackSourceActor(UWorld* World, const FVector2D& ActorPlanarLocation, const TArray<FVector2D>& PointPlanarOffsets, const TArray<double>& PointHeightsAboveGroundCm)
	{
		double ActorGroundCm = 0.0;
		if (!TraceGroundHeightCm(World, ActorPlanarLocation, ActorGroundCm))
		{
			return nullptr;
		}

		AActor* SourceActor = World->SpawnActor<AActor>();
		if (!SourceActor)
		{
			return nullptr;
		}

		const FVector ActorLocation(ActorPlanarLocation.X, ActorPlanarLocation.Y, ActorGroundCm + TrackActorHoverCm);
		USceneComponent* Root = NewObject<USceneComponent>(SourceActor);
		SourceActor->AddInstanceComponent(Root);
		SourceActor->SetRootComponent(Root);
		Root->RegisterComponent();
		Root->SetWorldLocation(ActorLocation);

		UTDFluidTrackSourceComponent* TrackSource = NewObject<UTDFluidTrackSourceComponent>(SourceActor);
		SourceActor->AddInstanceComponent(TrackSource);
		TrackSource->SetupAttachment(Root);
		TrackSource->RegisterComponent();
		for (int32 PointIndex = 0; PointIndex < PointPlanarOffsets.Num(); ++PointIndex)
		{
			const FVector2D PointPlanarLocation = ActorPlanarLocation + PointPlanarOffsets[PointIndex];
			double PointGroundCm = 0.0;
			if (!TraceGroundHeightCm(World, PointPlanarLocation, PointGroundCm))
			{
				return nullptr;
			}
			FTDFluidTrackSourcePoint& Point = TrackSource->Points.AddDefaulted_GetRef();
			Point.LocalOffsetCm = FVector(PointPlanarOffsets[PointIndex].X, PointPlanarOffsets[PointIndex].Y, PointGroundCm + PointHeightsAboveGroundCm[PointIndex] - ActorLocation.Z);
		}
		return SourceActor;
	}

	int32 CountKeptTrackStamps(FTDTrackInputTestWorld& TestWorld, ATDFluidLiveActor& LiveActor, AActor& SourceActor)
	{
		LiveActor.RegisterInteractingActor(&SourceActor);
		TestWorld.TickSteps(3);
		const int32 KeptCount = LiveActor.LiveComponent->GetLastProcessedPointCount();
		LiveActor.UnregisterInteractingActor(&SourceActor);
		TestWorld.TickSteps(1);
		return KeptCount;
	}

	ATDFluidLiveActor* SpawnConfiguredLiveActor(UWorld& TargetWorld, const FVector& Location, const FTDFluidSimulationConfig& Config)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		ATDFluidLiveActor* LiveActor = TargetWorld.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}
		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);
		return LiveActor;
	}

	int32 CountMaterialParameters(const UMaterialInstanceDynamic& TargetMID)
	{
		return TargetMID.ScalarParameterValues.Num() + TargetMID.VectorParameterValues.Num() + TargetMID.TextureParameterValues.Num();
	}

	bool ReadScalarParameter(const UMaterialInstanceDynamic& TargetMID, const TCHAR* ParameterName, float& OutValue)
	{
		return TargetMID.GetScalarParameterValue(FHashedMaterialParameterInfo(ParameterName), OutValue, true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackInputGroundSlabOnRampTest,
	"TDGame.FluidFX.Track.Input.GroundSlabOnRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackInputGroundSlabOnRampTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackInputTestPrivate;

	FTDTrackInputTestWorld TestWorld;
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube mesh loads"), CubeMesh))
	{
		return false;
	}

	const FVector2D FlatCenter(-1000.0, 0.0);
	const FVector2D RampCenter(1000.0, 0.0);
	AActor* Floor = SpawnStaticBlock(TestWorld.World, CubeMesh, FVector(FlatCenter.X, FlatCenter.Y, -50.0), FRotator::ZeroRotator, FVector(16.0, 16.0, 1.0));
	AActor* Ramp = SpawnStaticBlock(TestWorld.World, CubeMesh, FVector(RampCenter.X, RampCenter.Y, -50.0), FRotator(TrackRampPitchDegrees, 0.0f, 0.0f), FVector(16.0, 16.0, 1.0));
	if (!TestNotNull(TEXT("Spawned the flat floor"), Floor) || !TestNotNull(TEXT("Spawned the ramp"), Ramp))
	{
		return false;
	}
	TestWorld.TickSteps(2);

	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	UTDFluidPresetAsset* TrackPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::SnowTrack);
	ATDFluidLiveActor* LiveActor = Subsystem ? Subsystem->SpawnPooledFluidActor(TrackPreset, FVector::ZeroVector, 120.0f) : nullptr;
	if (!TestNotNull(TEXT("Spawned the snow track actor"), LiveActor))
	{
		return false;
	}
	TestWorld.TickSteps(2);

	const float SlabThicknessCm = LiveActor->LiveComponent->GetInputBrushSettings().GroundSlabThicknessCm;
	if (!TestTrue(*FString::Printf(TEXT("Snow track slab %.1f cm keeps +5 cm and drops +40 cm"), SlabThicknessCm), SlabThicknessCm > 5.0f && SlabThicknessCm < 40.0f))
	{
		return false;
	}

	AActor* KeptFlat = SpawnTrackSourceActor(TestWorld.World, FlatCenter + FVector2D(0.0, -300.0), { FVector2D(60.0, 0.0) }, { 5.0 });
	AActor* DroppedFlat = SpawnTrackSourceActor(TestWorld.World, FlatCenter + FVector2D(0.0, 300.0), { FVector2D(60.0, 0.0), FVector2D(-60.0, 0.0) }, { 40.0, 50.0 });
	AActor* KeptRamp = SpawnTrackSourceActor(TestWorld.World, RampCenter, { FVector2D(100.0, 0.0) }, { 5.0 });
	if (!TestNotNull(TEXT("Spawned the flat kept source"), KeptFlat) || !TestNotNull(TEXT("Spawned the flat dropped source"), DroppedFlat) || !TestNotNull(TEXT("Spawned the ramp source"), KeptRamp))
	{
		return false;
	}

	TestEqual(TEXT("Flat +5 cm wheel point is kept"), CountKeptTrackStamps(TestWorld, *LiveActor, *KeptFlat), 1);
	TestEqual(TEXT("Flat +40 and +50 cm wheel points are dropped"), CountKeptTrackStamps(TestWorld, *LiveActor, *DroppedFlat), 0);
	TestEqual(TEXT("Ramp +5 cm wheel point 100 cm downslope of the trace is kept"), CountKeptTrackStamps(TestWorld, *LiveActor, *KeptRamp), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackInputSlowBonesAndSpeedCapTest,
	"TDGame.FluidFX.Track.Input.SlowBonesAndSpeedCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackInputSlowBonesAndSpeedCapTest::RunTest(const FString& Parameters)
{
	constexpr float MinimumSpeedCmPerSec = 10.0f;
	constexpr float SpeedCapCmPerSec = 250.0f;
	FTDFluidInputBrushSettings Settings;
	const FVector Resting = FVector::ZeroVector;
	const FVector Walking(120.0, 0.0, 0.0);

	Settings.bStampsSlowBones = false;
	TestFalse(TEXT("Resting point is skipped while slow bones are off"), ATDFluidLiveActor::IsTrackContactStampable(Resting, MinimumSpeedCmPerSec, 0.0f, Settings));
	TestTrue(TEXT("Moving point is stamped while slow bones are off"), ATDFluidLiveActor::IsTrackContactStampable(Walking, MinimumSpeedCmPerSec, 0.0f, Settings));

	Settings.bStampsSlowBones = true;
	TestTrue(TEXT("Resting point is stamped while slow bones are on"), ATDFluidLiveActor::IsTrackContactStampable(Resting, MinimumSpeedCmPerSec, 0.0f, Settings));
	TestFalse(TEXT("Teleport speed is still rejected"), ATDFluidLiveActor::IsTrackContactStampable(FVector(Settings.TeleportSpeedCmPerSec * 2.0f, 0.0f, 0.0f), MinimumSpeedCmPerSec, 0.0f, Settings));
	TestTrue(TEXT("Speed under the cap is stamped"), ATDFluidLiveActor::IsTrackContactStampable(FVector(SpeedCapCmPerSec * 0.5f, 0.0f, 0.0f), MinimumSpeedCmPerSec, SpeedCapCmPerSec, Settings));

	int32 StampedOverCapCount = 0;
	for (const bool bStampsSlowBones : { false, true })
	{
		Settings.bStampsSlowBones = bStampsSlowBones;
		for (const float SpeedRatio : { 1.01f, 1.5f, 2.0f, 4.0f })
		{
			const FVector Velocity = FVector(0.6, 0.8, 0.0) * SpeedCapCmPerSec * SpeedRatio;
			StampedOverCapCount += ATDFluidLiveActor::IsTrackContactStampable(Velocity, MinimumSpeedCmPerSec, SpeedCapCmPerSec, Settings) ? 1 : 0;
		}
	}
	TestEqual(TEXT("No point over the speed cap is stamped"), StampedOverCapCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackInputWheelSocketStampsTest,
	"TDGame.FluidFX.Track.Input.WheelSocketStamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackInputWheelSocketStampsTest::RunTest(const FString& Parameters)
{
	const FName WheelSocketName(TEXT("WheelFrontLeft"));
	const FVector SocketLocation(120.0, -80.0, 10.0);
	const FRotator SocketRotation(0.0, 90.0, 0.0);
	UStaticMesh* BodyMesh = NewObject<UStaticMesh>(GetTransientPackage());
	UStaticMeshSocket* WheelSocket = NewObject<UStaticMeshSocket>(BodyMesh);
	WheelSocket->SocketName = WheelSocketName;
	WheelSocket->RelativeLocation = SocketLocation;
	WheelSocket->RelativeRotation = SocketRotation;
	BodyMesh->Sockets.Add(WheelSocket);

	const FTransform BodyTransform(FRotator(0.0, 30.0, 0.0), FVector(500.0, 200.0, 50.0));
	UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(GetTransientPackage());
	Body->SetStaticMesh(BodyMesh);
	Body->SetWorldTransform(BodyTransform);

	const FVector SourceRelativeLocation(0.0, 0.0, -40.0);
	UTDFluidTrackSourceComponent* TrackSource = NewObject<UTDFluidTrackSourceComponent>(GetTransientPackage());
	TrackSource->SetupAttachment(Body);
	TrackSource->SetRelativeLocation(SourceRelativeLocation);

	const FVector SocketOffset(0.0, 0.0, -30.0);
	const FVector PlainOffset(50.0, 0.0, 0.0);
	FTDFluidTrackSourcePoint& SocketPoint = TrackSource->Points.AddDefaulted_GetRef();
	SocketPoint.SocketName = WheelSocketName;
	SocketPoint.LocalOffsetCm = SocketOffset;
	SocketPoint.RadiusCm = 20.0f;
	FTDFluidTrackSourcePoint& PlainPoint = TrackSource->Points.AddDefaulted_GetRef();
	PlainPoint.LocalOffsetCm = PlainOffset;
	PlainPoint.RadiusCm = 10.0f;
	FTDFluidTrackSourcePoint& MissingSocketPoint = TrackSource->Points.AddDefaulted_GetRef();
	MissingSocketPoint.SocketName = TEXT("MissingSocket");
	MissingSocketPoint.LocalOffsetCm = PlainOffset;

	const FVector ExpectedSocketPoint = (FTransform(SocketRotation, SocketLocation) * BodyTransform).TransformPosition(SocketOffset);
	const FVector ExpectedPlainPoint = BodyTransform.TransformPosition(SourceRelativeLocation + PlainOffset);
	FVector ResolvedLocation;
	TestTrue(TEXT("Socket point resolves"), TrackSource->ResolvePointWorldLocation(0, ResolvedLocation));
	TestTrue(*FString::Printf(TEXT("Socket point follows the socket and offset (%s vs %s)"), *ResolvedLocation.ToString(), *ExpectedSocketPoint.ToString()), ResolvedLocation.Equals(ExpectedSocketPoint, 1.0e-3));
	TestTrue(TEXT("Plain point resolves"), TrackSource->ResolvePointWorldLocation(1, ResolvedLocation));
	TestTrue(*FString::Printf(TEXT("Plain point follows the component and offset (%s vs %s)"), *ResolvedLocation.ToString(), *ExpectedPlainPoint.ToString()), ResolvedLocation.Equals(ExpectedPlainPoint, 1.0e-3));
	TestTrue(TEXT("Missing socket point resolves"), TrackSource->ResolvePointWorldLocation(2, ResolvedLocation));
	TestTrue(TEXT("Missing socket falls back to the component"), ResolvedLocation.Equals(ExpectedPlainPoint, 1.0e-3));
	TestFalse(TEXT("Out of range point does not resolve"), TrackSource->ResolvePointWorldLocation(3, ResolvedLocation));

	TestNearlyEqual(TEXT("Scale 1 keeps the socket radius"), TrackSource->ResolvePointRadiusCm(0, 1.0f), 20.0f, 1.0e-6f);
	TestNearlyEqual(TEXT("Scale 2 doubles the socket radius"), TrackSource->ResolvePointRadiusCm(0, 2.0f), 40.0f, 1.0e-6f);
	TestNearlyEqual(TEXT("Scale 2 doubles the plain radius"), TrackSource->ResolvePointRadiusCm(1, 2.0f), 20.0f, 1.0e-6f);
	TestNearlyEqual(TEXT("Out of range radius is zero"), TrackSource->ResolvePointRadiusCm(3, 2.0f), 0.0f, 1.0e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTrackWetnessSourceBindingRhiTest,
	"TDGame.FluidFXRhi.Track.Wetness.SourceBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidTrackWetnessSourceBindingRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidTrackInputTestPrivate;

	FTDTrackInputTestWorld TestWorld;
	FTDFluidSimulationConfig TrackConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::SandTrack);
	TrackConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	UTDFluidPresetAsset* SeaPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Sea);
	ATDFluidLiveActor* SeaActor = Subsystem ? Subsystem->SpawnPooledFluidActor(SeaPreset, FVector(-200.0, 0.0, 0.0), 120.0f) : nullptr;
	ATDFluidLiveActor* BoundTrackActor = SpawnConfiguredLiveActor(*TestWorld.World, FVector(300.0, 0.0, 0.0), TrackConfig);
	ATDFluidLiveActor* UnboundTrackActor = SpawnConfiguredLiveActor(*TestWorld.World, FVector(300.0, 900.0, 0.0), TrackConfig);
	UMaterialInterface* BaseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	UMaterialInstanceDynamic* BoundMID = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
	UMaterialInstanceDynamic* UnboundMID = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
	if (!TestNotNull(TEXT("Spawned the sea source"), SeaActor) || !TestNotNull(TEXT("Spawned the bound sand track"), BoundTrackActor) || !TestNotNull(TEXT("Spawned the unbound sand track"), UnboundTrackActor)
		|| !TestNotNull(TEXT("Created the bound MID"), BoundMID) || !TestNotNull(TEXT("Created the unbound MID"), UnboundMID))
	{
		return false;
	}

	BoundTrackActor->LiveComponent->RegisterExternalMaterialTarget(BoundMID);
	UnboundTrackActor->LiveComponent->RegisterExternalMaterialTarget(UnboundMID);
	TestWorld.TickSteps(8);

	UTDFluidLiveComponent* SeaComponent = SeaActor->LiveComponent;
	constexpr int32 MaximumSourceWaitSteps = 60;
	for (int32 Step = 0; Step < MaximumSourceWaitSteps && !SeaComponent->GetWaterSurfaceOutputTexture(); ++Step)
	{
		TestWorld.TickSteps(1);
	}
	UTexture* SeaSurfaceTexture = SeaComponent->GetWaterSurfaceOutputTexture();
	if (!TestNotNull(TEXT("Sea source has a water surface output"), SeaSurfaceTexture))
	{
		return false;
	}

	const int32 UnboundCountBefore = CountMaterialParameters(*UnboundMID);
	UnboundTrackActor->LiveComponent->SetWetnessSourceComponent(nullptr);
	TestWorld.TickSteps(2);
	float ScalarValue = -1.0f;
	TestEqual(TEXT("Without a source the MID parameter count stays"), CountMaterialParameters(*UnboundMID), UnboundCountBefore);
	TestFalse(TEXT("Without a source WetSourceActive is never written"), ReadScalarParameter(*UnboundMID, TEXT("WetSourceActive"), ScalarValue));

	BoundTrackActor->LiveComponent->SetWetnessSourceComponent(SeaComponent);
	TestTrue(TEXT("Binding writes WetSourceActive"), ReadScalarParameter(*BoundMID, TEXT("WetSourceActive"), ScalarValue));
	TestNearlyEqual(TEXT("Binding sets WetSourceActive to 1"), ScalarValue, 1.0f, 1.0e-6f);
	FLinearColor SourceWindow = FLinearColor::Black;
	TestTrue(TEXT("Binding writes WetSourceWindow"), BoundMID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("WetSourceWindow")), SourceWindow, true));
	const FVector2D SeaCenter = SeaComponent->GetSimulationCenterWorld();
	const FVector2D SeaExtent = SeaComponent->GetSimulationExtentCm();
	TestTrue(*FString::Printf(TEXT("WetSourceWindow holds the sea center and full size (%s)"), *SourceWindow.ToString()),
		FMath::IsNearlyEqual(SourceWindow.R, static_cast<float>(SeaCenter.X), 1.0e-2f) && FMath::IsNearlyEqual(SourceWindow.G, static_cast<float>(SeaCenter.Y), 1.0e-2f)
		&& FMath::IsNearlyEqual(SourceWindow.B, static_cast<float>(SeaExtent.X), 1.0e-2f) && FMath::IsNearlyEqual(SourceWindow.A, static_cast<float>(SeaExtent.Y), 1.0e-2f));
	UTexture* BoundTexture = nullptr;
	TestTrue(TEXT("Binding writes WetSourceTexture"), BoundMID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("WetSourceTexture")), BoundTexture, true));
	TestTrue(TEXT("WetSourceTexture is the sea water surface output"), BoundTexture == SeaSurfaceTexture);

	TestWorld.TickSteps(2);
	TestTrue(TEXT("WetSourceActive survives ticking"), ReadScalarParameter(*BoundMID, TEXT("WetSourceActive"), ScalarValue) && FMath::IsNearlyEqual(ScalarValue, 1.0f));

	BoundTrackActor->LiveComponent->SetWetnessSourceComponent(nullptr);
	TestTrue(TEXT("Releasing writes WetSourceActive"), ReadScalarParameter(*BoundMID, TEXT("WetSourceActive"), ScalarValue));
	TestNearlyEqual(TEXT("Releasing sets WetSourceActive to 0"), ScalarValue, 0.0f, 1.0e-6f);
	TestWorld.TickSteps(2);
	TestTrue(TEXT("WetSourceActive stays 0 after release"), ReadScalarParameter(*BoundMID, TEXT("WetSourceActive"), ScalarValue) && FMath::IsNearlyEqual(ScalarValue, 0.0f));
	return true;
}

#endif
