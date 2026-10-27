#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "FluidFX/TDFluidObstacleComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPlaneTypes.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidSplatDataInterface.h"
#include "NiagaraShared.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "NiagaraDataChannelAccessor.h"
#include "NiagaraDataChannelAsset.h"
#include "NiagaraDataChannel_Global.h"
#include "NiagaraDataChannelVariable.h"
#include "UObject/StrongObjectPtr.h"

namespace TDFluidMassInputTestPrivate
{
	constexpr ETDFluidPresetKind LastLegacyPresetKind = ETDFluidPresetKind::FloorSmokeCapture;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputDefaultsKeepLegacyTest,
	"TDGame.FluidFX.MassInput.DefaultsKeepLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputDefaultsKeepLegacyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidMassInputTestPrivate;
	const FTDFluidSimulationConfig DefaultConfig;
	TestEqual(TEXT("Default GpuMaxStampsPerTick"), DefaultConfig.GpuMaxStampsPerTick, 16000);
	TestEqual(TEXT("Default GpuMaxStampsPerBin"), DefaultConfig.GpuMaxStampsPerBin, 256);
	TestEqual(TEXT("Default MassInputBufferDivisor"), DefaultConfig.MassInputBufferDivisor, 2);
	TestEqual(TEXT("Default MassSplatRadiusCapCells"), DefaultConfig.MassSplatRadiusCapCells, 8);

	const FTDFluidInputBrushSettings DefaultBrush;
	TestEqual(TEXT("Default MassPointDensityStrength"), DefaultBrush.MassPointDensityStrength, 0.85f);
	TestEqual(TEXT("Default MassPointVelocityStrength"), DefaultBrush.MassPointVelocityStrength, 1.0f);
	TestEqual(TEXT("Default MassPointHeatStrength"), DefaultBrush.MassPointHeatStrength, 0.0f);
	TestEqual(TEXT("Default MassPointRadiusScale"), DefaultBrush.MassPointRadiusScale, 1.0f);

	const FTDFluidMassInputSettings DefaultMassInput;
	TestFalse(TEXT("Mass input is off by default"), DefaultMassInput.bAcceptMassInput);
	TestFalse(TEXT("Data channel reading is off by default"), DefaultMassInput.bReadDataChannel);
	TestTrue(TEXT("Data channel path is empty by default"), DefaultMassInput.DataChannel.IsNull());
	TestEqual(TEXT("Default channel name"), DefaultMassInput.MassInputChannelName, FName(TEXT("Default")));
	TestEqual(TEXT("Default MaxDataChannelPointsPerTick"), DefaultMassInput.MaxDataChannelPointsPerTick, 10000);

	const FTDFluidPlaneSettings DefaultPlane;
	TestFalse(TEXT("Oriented obstacle projection is off by default"), DefaultPlane.bProjectOrientedObstacles);
	TestEqual(TEXT("Default obstacle contour padding"), DefaultPlane.ObstacleContourPaddingCm, 0.0f);

	const FTDFluidSdfObstacle DefaultObstacle;
	TestTrue(TEXT("Obstacle 3D half extents default to zero"), DefaultObstacle.BoxHalfExtents3DCm.IsZero());
	TestTrue(TEXT("Obstacle rotation defaults to zero"), DefaultObstacle.WorldRotation.IsZero());

	for (uint8 KindValue = 0; KindValue <= static_cast<uint8>(LastLegacyPresetKind); ++KindValue)
	{
		const ETDFluidPresetKind Kind = static_cast<ETDFluidPresetKind>(KindValue);
		const FTDFluidMassInputSettings MassInput = UTDFluidPresetAsset::BuildMassInputForPresetKind(Kind);
		const FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		const FTDFluidPlaneSettings Plane = UTDFluidPresetAsset::BuildPlaneSettingsForPresetKind(Kind);
		TestFalse(FString::Printf(TEXT("Legacy kind %d does not accept mass input"), KindValue), MassInput.bAcceptMassInput);
		TestFalse(FString::Printf(TEXT("Legacy kind %d does not collect obstacle components"), KindValue), MassInput.bCollectObstacleComponents);
		TestEqual(FString::Printf(TEXT("Legacy kind %d keeps 16000 stamps per tick"), KindValue), Config.GpuMaxStampsPerTick, 16000);
		TestEqual(FString::Printf(TEXT("Legacy kind %d keeps 256 stamps per bin"), KindValue), Config.GpuMaxStampsPerBin, 256);
		TestFalse(FString::Printf(TEXT("Legacy kind %d keeps legacy obstacle projection"), KindValue), Plane.bProjectOrientedObstacles);
	}

	TestTrue(TEXT("Flamethrower is fire family"), UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind::Flamethrower));
	TestFalse(TEXT("SnowAvalanche is not fire family"), UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind::SnowAvalanche));
	TestFalse(TEXT("MassInputBench is not fire family"), UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind::MassInputBench));

	const ETDFluidPresetKind NewKinds[] = { ETDFluidPresetKind::Flamethrower, ETDFluidPresetKind::SnowAvalanche, ETDFluidPresetKind::MassInputBench };
	const ETDFluidBrushChannel ExpectedChannels[] = { ETDFluidBrushChannel::ChannelB_Fire, ETDFluidBrushChannel::ChannelR_Mud, ETDFluidBrushChannel::ChannelA_Water };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(NewKinds); ++Index)
	{
		const UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(nullptr, NewKinds[Index]);
		TestNotNull(TEXT("Transient preset exists"), Preset);
		if (Preset)
		{
			TestEqual(FString::Printf(TEXT("Transient preset channel for new kind %d"), Index), Preset->DefaultChannel, ExpectedChannels[Index]);
		}
		TestTrue(FString::Printf(TEXT("New kind %d accepts mass input"), Index), UTDFluidPresetAsset::BuildMassInputForPresetKind(NewKinds[Index]).bAcceptMassInput);
	}

	const FTDFluidMassInputSettings Bench = UTDFluidPresetAsset::BuildMassInputForPresetKind(ETDFluidPresetKind::MassInputBench);
	TestTrue(TEXT("MassInputBench reads the data channel"), Bench.bReadDataChannel);
	TestTrue(TEXT("MassInputBench data channel path stays empty until the asset exists"), Bench.DataChannel.IsNull());
	TestTrue(TEXT("Flamethrower projects oriented obstacles"), UTDFluidPresetAsset::BuildPlaneSettingsForPresetKind(ETDFluidPresetKind::Flamethrower).bProjectOrientedObstacles);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputCpuBackendClampsDeterministicallyTest,
	"TDGame.FluidFX.MassInput.CpuBackendClampsDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputCpuBackendClampsDeterministicallyTest::RunTest(const FString& Parameters)
{
	TArray<FTDFluidMassInputPoint> ForwardPoints;
	for (int32 PointIndex = 0; PointIndex < 600; ++PointIndex)
	{
		const int32 GridX = PointIndex % 30;
		const int32 GridY = PointIndex / 30;
		FTDFluidMassInputPoint& Point = ForwardPoints.AddDefaulted_GetRef();
		Point.WorldLocation = FVector((GridX - 15) * 20.0, (GridY - 10) * 20.0, 0.0);
		Point.WorldVelocity = FVector(PointIndex % 7, PointIndex % 11, 0.0);
		Point.RadiusCm = 25.0f;
		Point.DensityStrength = 1.0f;
	}

	TArray<FTDFluidMassInputPoint> ReversedPoints = ForwardPoints;
	Algo::Reverse(ReversedPoints);
	TArray<FTDFluidMassInputPoint> InterleavedPoints;
	for (int32 PointIndex = 0; PointIndex < ForwardPoints.Num(); PointIndex += 2)
	{
		InterleavedPoints.Add(ForwardPoints[PointIndex]);
	}
	for (int32 PointIndex = 1; PointIndex < ForwardPoints.Num(); PointIndex += 2)
	{
		InterleavedPoints.Add(ForwardPoints[PointIndex]);
	}

	UTDFluidLiveComponent::SortAndClampMassPoints(ForwardPoints, FVector::ZeroVector, 512);
	UTDFluidLiveComponent::SortAndClampMassPoints(ReversedPoints, FVector::ZeroVector, 512);
	UTDFluidLiveComponent::SortAndClampMassPoints(InterleavedPoints, FVector::ZeroVector, 512);
	TestEqual(TEXT("Clamp keeps 512 points"), ForwardPoints.Num(), 512);
	TestEqual(TEXT("Reversed order keeps 512 points"), ReversedPoints.Num(), 512);
	TestEqual(TEXT("Interleaved order keeps 512 points"), InterleavedPoints.Num(), 512);
	bool bIsSameQueue = ForwardPoints.Num() == ReversedPoints.Num() && ForwardPoints.Num() == InterleavedPoints.Num();
	for (int32 PointIndex = 0; bIsSameQueue && PointIndex < ForwardPoints.Num(); ++PointIndex)
	{
		bIsSameQueue = ForwardPoints[PointIndex].WorldLocation == ReversedPoints[PointIndex].WorldLocation
			&& ForwardPoints[PointIndex].WorldVelocity == ReversedPoints[PointIndex].WorldVelocity
			&& ForwardPoints[PointIndex].WorldLocation == InterleavedPoints[PointIndex].WorldLocation
			&& ForwardPoints[PointIndex].WorldVelocity == InterleavedPoints[PointIndex].WorldVelocity;
	}
	TestTrue(TEXT("Input order does not change the clamped queue"), bIsSameQueue);
	TestTrue(TEXT("Nearest point comes first"), ForwardPoints.Num() > 0 && ForwardPoints[0].WorldLocation.IsNearlyZero());

	TArray<FTDFluidMassInputPoint> EmptyBudgetPoints = ReversedPoints;
	UTDFluidLiveComponent::SortAndClampMassPoints(EmptyBudgetPoints, FVector::ZeroVector, 0);
	TestEqual(TEXT("Zero budget drops every point"), EmptyBudgetPoints.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputOrientedObstacleProjectionTest,
	"TDGame.FluidFX.MassInput.OrientedObstacleProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputOrientedObstacleProjectionTest::RunTest(const FString& Parameters)
{
	FTDFluidPlaneBasis CameraBasis;
	CameraBasis.Orientation = ETDFluidPlaneOrientation::CameraFacing;
	CameraBasis.U = FVector(0.0, 1.0, 0.0);
	CameraBasis.V = FVector(0.0, 0.0, 1.0);
	CameraBasis.N = FVector(1.0, 0.0, 0.0);

	const FTDFluidSdfObstacle Cube = UTDFluidObstacleComponent::MakeOrientedBoxObstacle(FVector::ZeroVector, FVector(50.0, 50.0, 50.0), FRotator(0.0, 45.0, 0.0));
	FTDFluidPlaneSettings ProjectingSettings;
	ProjectingSettings.InteractionDepthCm = 150.0f;
	ProjectingSettings.bProjectOrientedObstacles = true;
	ProjectingSettings.ObstacleContourPaddingCm = 10.0f;

	FTDFluidSdfObstacle Projected;
	TestTrue(TEXT("Rotated cube inside the slab projects"), UTDFluidLiveComponent::ProjectObstacleToPlane(CameraBasis, 0.0, ProjectingSettings, Cube, Projected));
	const double ExpectedHalfU = 50.0 * UE_DOUBLE_SQRT_2 + 10.0;
	TestEqual(TEXT("Projected half width follows the rotated corners"), Projected.BoxHalfExtentsCm.X, ExpectedHalfU, 0.01);
	TestEqual(TEXT("Projected half height follows the vertical extent"), Projected.BoxHalfExtentsCm.Y, 60.0, 0.01);
	TestEqual(TEXT("Projected center U"), Projected.WorldCenter.X, CameraBasis.ProjectPoint(FVector::ZeroVector).X, 0.01);
	TestEqual(TEXT("Projected center V"), Projected.WorldCenter.Y, CameraBasis.ProjectPoint(FVector::ZeroVector).Y, 0.01);

	FTDFluidPlaneSettings LegacySettings = ProjectingSettings;
	LegacySettings.bProjectOrientedObstacles = false;
	FTDFluidSdfObstacle Legacy;
	TestTrue(TEXT("Legacy projection keeps the obstacle"), UTDFluidLiveComponent::ProjectObstacleToPlane(CameraBasis, 0.0, LegacySettings, Cube, Legacy));
	TestTrue(TEXT("Legacy projection keeps the world half extents"), Legacy.BoxHalfExtentsCm.Equals(Cube.BoxHalfExtentsCm, 0.001));

	FTDFluidSdfObstacle FarCube = Cube;
	FarCube.WorldCenter = FVector(1000.0, 0.0, 0.0);
	FTDFluidSdfObstacle Dropped;
	TestFalse(TEXT("Cube outside the slab is dropped"), UTDFluidLiveComponent::ProjectObstacleToPlane(CameraBasis, 0.0, ProjectingSettings, FarCube, Dropped));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputObstacleComponentRegistrationTest,
	"TDGame.FluidFX.MassInput.ObstacleComponentRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputObstacleComponentRegistrationTest::RunTest(const FString& Parameters)
{
	UTDFluidLiveSubsystem* FluidSubsystem = NewObject<UTDFluidLiveSubsystem>(GetTransientPackage());
	UTDFluidObstacleComponent* FirstSource = NewObject<UTDFluidObstacleComponent>(GetTransientPackage());
	UTDFluidObstacleComponent* SecondSource = NewObject<UTDFluidObstacleComponent>(GetTransientPackage());
	FluidSubsystem->RegisterObstacleSource(FirstSource);
	FluidSubsystem->RegisterObstacleSource(SecondSource);
	FluidSubsystem->RegisterObstacleSource(SecondSource);
	TestEqual(TEXT("Two distinct sources are registered"), FluidSubsystem->GetObstacleSourceCount(), 2);
	FluidSubsystem->UnregisterObstacleSource(FirstSource);
	TestEqual(TEXT("Unregister removes one source"), FluidSubsystem->GetObstacleSourceCount(), 1);
	FluidSubsystem->UnregisterObstacleSource(SecondSource);
	TestEqual(TEXT("Unregister removes the last source"), FluidSubsystem->GetObstacleSourceCount(), 0);

	TArray<FTDFluidSdfObstacle> Sources;
	for (int32 SourceIndex = 0; SourceIndex < 5; ++SourceIndex)
	{
		Sources.Add(UTDFluidObstacleComponent::MakeOrientedBoxObstacle(FVector((4 - SourceIndex) * 100.0, 0.0, 0.0), FVector(20.0), FRotator::ZeroRotator));
	}
	TArray<FTDFluidSdfObstacle> Selected;
	UTDFluidLiveSubsystem::SelectNearestObstacles(Sources, FVector::ZeroVector, 3, Selected);
	TestEqual(TEXT("Selection respects the collected obstacle limit"), Selected.Num(), 3);
	TestTrue(TEXT("Nearest obstacle is collected first"), Selected.Num() == 3 && Selected[0].WorldCenter.IsNearlyZero());
	TestTrue(TEXT("Third nearest obstacle is at 200 cm"), Selected.Num() == 3 && FMath::IsNearlyEqual(Selected[2].WorldCenter.X, 200.0));
	UTDFluidLiveSubsystem::SelectNearestObstacles(Sources, FVector::ZeroVector, 0, Selected);
	TestEqual(TEXT("Zero limit collects nothing"), Selected.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputSplatInterfaceContractTest,
	"TDGame.FluidFX.MassInput.SplatInterfaceContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputSplatInterfaceContractTest::RunTest(const FString& Parameters)
{
	UTDFluidSplatDataInterface* Source = NewObject<UTDFluidSplatDataInterface>();
	Source->TargetChannelName = TEXT("ChannelA");
	UTDFluidSplatDataInterface* Copy = NewObject<UTDFluidSplatDataInterface>();
	TestTrue(TEXT("Copy succeeds"), Source->CopyTo(Copy));
	TestEqual(TEXT("Channel survives copy"), Copy->TargetChannelName, FName(TEXT("ChannelA")));
	TestTrue(TEXT("Copies are equal"), Source->Equals(Copy));
	Copy->TargetChannelName = TEXT("ChannelB");
	TestFalse(TEXT("Different channels are not equal"), Source->Equals(Copy));
	TestTrue(TEXT("Runs on GPU only"), Source->CanExecuteOnTarget(ENiagaraSimTarget::GPUComputeSim) && !Source->CanExecuteOnTarget(ENiagaraSimTarget::CPUSim));
#if WITH_EDITORONLY_DATA
	FNiagaraDataInterfaceGPUParamInfo ParamInfo;
	ParamInfo.DataInterfaceHLSLSymbol = TEXT("TDSplat");
	FString ParameterHlsl;
	Source->GetParameterDefinitionHLSL(ParamInfo, ParameterHlsl);
	TestTrue(TEXT("Template defines the impure splat call"), ParameterHlsl.Contains(TEXT("SplatPoint_TDSplat_UEImpureCall")));
	TestTrue(TEXT("Template declares the accumulation buffer"), ParameterHlsl.Contains(TEXT("TDSplat_Accumulation")));
	FString CommonHlsl;
	Source->GetCommonHLSL(CommonHlsl);
	TestTrue(TEXT("Common HLSL includes the shared splat"), CommonHlsl.Contains(TEXT("/TDFluidGpu/Private/TDFluidGpuMassSplat.ush")));
#endif
	return true;
}

namespace TDFluidMassInputDataChannelTestPrivate
{
	struct FTDFluidMassInputTestWorldScope
	{
		FTDFluidMassInputTestWorldScope()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidMassInputTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidMassInputTestWorldScope()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void TickWorldOnce(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
		}

		UWorld* World = nullptr;
	};

	struct FTDFluidMassInputTestWriter : public FNDCWriterBase
	{
		NDCVarWriter(FNiagaraPosition, Position);
	};

	UNiagaraDataChannelAsset* CreateTransientGlobalDataChannel(const TCHAR* BaseName)
	{
		FArrayProperty* VariablesProperty = CastField<FArrayProperty>(UNiagaraDataChannel::StaticClass()->FindPropertyByName(TEXT("ChannelVariables")));
		FObjectProperty* DataChannelProperty = CastField<FObjectProperty>(UNiagaraDataChannelAsset::StaticClass()->FindPropertyByName(TEXT("DataChannel")));
		if (!VariablesProperty || !DataChannelProperty)
		{
			return nullptr;
		}

		const FName AssetName = MakeUniqueObjectName(GetTransientPackage(), UNiagaraDataChannelAsset::StaticClass(), BaseName);
		UNiagaraDataChannelAsset* DataChannelAsset = NewObject<UNiagaraDataChannelAsset>(GetTransientPackage(), AssetName, RF_Transient);
		UNiagaraDataChannel_Global* DataChannel = NewObject<UNiagaraDataChannel_Global>(DataChannelAsset, NAME_None, RF_Transient);
		TArray<FNiagaraDataChannelVariable>* Variables = VariablesProperty->ContainerPtrToValuePtr<TArray<FNiagaraDataChannelVariable>>(DataChannel);
		FNiagaraDataChannelVariable& PositionVariable = Variables->AddDefaulted_GetRef();
		PositionVariable.SetName(TEXT("Position"));
		PositionVariable.SetType(FNiagaraDataChannelVariable::ToDataChannelType(FNiagaraTypeDefinition::GetPositionDef()));
		DataChannelProperty->SetObjectPropertyValue_InContainer(DataChannelAsset, DataChannel);
		DataChannel->PostLoad();
		return DataChannelAsset;
	}

	FTDFluidMassInputSettings MakeDataChannelReadSettings(UNiagaraDataChannelAsset* DataChannelAsset)
	{
		FTDFluidMassInputSettings Settings;
		Settings.bAcceptMassInput = true;
		Settings.bReadDataChannel = true;
		Settings.DataChannel = DataChannelAsset;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMassInputDataChannelRoutesByChannelTest,
	"TDGame.FluidFX.MassInput.DataChannelRoutesByChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMassInputDataChannelRoutesByChannelTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidMassInputDataChannelTestPrivate;

	TStrongObjectPtr<UNiagaraDataChannelAsset> FirstChannelAsset(CreateTransientGlobalDataChannel(TEXT("TDFluidMassInputChannelA")));
	TStrongObjectPtr<UNiagaraDataChannelAsset> SecondChannelAsset(CreateTransientGlobalDataChannel(TEXT("TDFluidMassInputChannelB")));
	const bool bHasValidChannels = FirstChannelAsset && SecondChannelAsset
		&& FirstChannelAsset->Get() && FirstChannelAsset->Get()->IsValid()
		&& SecondChannelAsset->Get() && SecondChannelAsset->Get()->IsValid();
	if (!TestTrue(TEXT("Both transient data channels are valid"), bHasValidChannels))
	{
		return false;
	}

	FTDFluidMassInputTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	if (!TestNotNull(TEXT("Fluid subsystem exists in the test world"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* FirstActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	ATDFluidLiveActor* SecondActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	if (!TestTrue(TEXT("Two distinct fluid actors are spawned"), FirstActor && SecondActor && FirstActor != SecondActor))
	{
		return false;
	}

	TestWorld.TickWorldOnce(0.05f);
	FirstActor->LiveComponent->SetMassInputSettingsForTests(MakeDataChannelReadSettings(FirstChannelAsset.Get()));
	SecondActor->LiveComponent->SetMassInputSettingsForTests(MakeDataChannelReadSettings(SecondChannelAsset.Get()));

	const TArray<FVector> WrittenLocations = { FVector(0.0, 0.0, 0.0), FVector(20.0, 0.0, 0.0), FVector(0.0, 20.0, 0.0) };
	FTDFluidMassInputTestWriter Writer;
	const bool bBeganWrite = Writer.BeginWrite(TestWorld.World, FirstChannelAsset->Get(), FNiagaraDataChannelSearchParameters(), WrittenLocations.Num(), true, true, false);
	if (!TestTrue(TEXT("Writer opens channel A"), bBeganWrite))
	{
		return false;
	}

	for (int32 PointIndex = 0; PointIndex < WrittenLocations.Num(); ++PointIndex)
	{
		Writer.WritePosition(PointIndex, WrittenLocations[PointIndex]);
	}
	Writer.EndWrite();

	int32 FirstProcessedPointCount = 0;
	int32 SecondProcessedPointCount = 0;
	for (int32 FrameIndex = 0; FrameIndex < 4; ++FrameIndex)
	{
		TestWorld.TickWorldOnce(0.05f);
		FirstProcessedPointCount += FirstActor->LiveComponent->GetLastProcessedPointCount();
		SecondProcessedPointCount += SecondActor->LiveComponent->GetLastProcessedPointCount();
	}

	TestEqual(TEXT("Channel A actor receives the three written points exactly once"), FirstProcessedPointCount, 3);
	TestEqual(TEXT("Channel B actor receives no points"), SecondProcessedPointCount, 0);
	return true;
}

#endif
