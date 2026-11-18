#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidTerrainCapture.h"
#include "FluidFX/TDFog3DGround.h"
#include "FluidFX/TDFog3DLayer.h"
#include "FluidFX/TDFog3DZoneActor.h"
#include "FluidFX/Tests/TDFog3DZoneActorGroundTestAccess.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TDFluidGpuFog3D.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFog3DGroundZoneRhiTestPrivate
{
	const TCHAR* CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* MissingDepthOutputRelativePath = TEXT("AgentOps/20261115/fog3d/stage2a/impl/probe-missing-depth.json");
	const FVector ZoneLocation(0.0, 0.0, 150.0);
	const FVector PlayerOffset(10.0, -20.0, 0.0);
	constexpr double CubeEdgeCm = 100.0;
	constexpr double SlabMarginColumns = 8.0;
	constexpr double SlabThicknessCm = 100.0;
	constexpr double HighEndColumns = 80.25;
	constexpr double LowEndColumns = 160.25;
	constexpr double HighTopAboveFloorCm = 40.0;
	constexpr double LowTopAboveFloorCm = -1000.0;
	constexpr float TestMissingHeightCm = -7.0f;
	constexpr double FoundToleranceCm = 0.5;
	constexpr double FarPlaneDepthCm = 1048576.0 / 4.0;
	constexpr double LoadTimeoutSeconds = 60.0;
	constexpr int32 SettleFrameCount = 3;
	constexpr float SettleFrameSeconds = 1.0f / 60.0f;
	constexpr int32 ReadbackSubmitLimit = 64;
	constexpr float TinyDeltaSeconds = 1.0e-4f;

	struct FTDGroundZoneWorld
	{
		UWorld* World = nullptr;

		FTDGroundZoneWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFog3DGroundZoneRhiWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDGroundZoneWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, SettleFrameSeconds);
			FlushRenderingCommands();
		}
	};

	enum class ETDGroundRegion : uint8
	{
		High,
		Low,
		Empty
	};

	struct FTDGroundRangeStats
	{
		int32 Count = 0;
		double Min = TNumericLimits<double>::Max();
		double Max = TNumericLimits<double>::Lowest();
		int32 NonFiniteCount = 0;

		void Add(double Value)
		{
			++Count;
			if (!FMath::IsFinite(Value))
			{
				++NonFiniteCount;
				return;
			}
			Min = FMath::Min(Min, Value);
			Max = FMath::Max(Max, Value);
		}
	};

	struct FTDGroundConvertOutcome
	{
		bool bAccepted = false;
		bool bReadBack = false;
		TArray<float> Heights;
	};

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	ETDGroundRegion FindRegion(int32 TexelX)
	{
		const double ColumnCenter = TexelX + 0.5;
		if (ColumnCenter < HighEndColumns)
		{
			return ETDGroundRegion::High;
		}
		return ColumnCenter < LowEndColumns ? ETDGroundRegion::Low : ETDGroundRegion::Empty;
	}

	void SpawnTerrainBox(UWorld& World, UStaticMesh& CubeMesh, const FVector& MinCm, const FVector& MaxCm)
	{
		AActor* BoxActor = World.SpawnActor<AActor>();
		UStaticMeshComponent* BoxMesh = NewObject<UStaticMeshComponent>(BoxActor);
		BoxMesh->SetStaticMesh(&CubeMesh);
		BoxMesh->SetMobility(EComponentMobility::Movable);
		BoxActor->SetRootComponent(BoxMesh);
		BoxMesh->RegisterComponent();
		BoxActor->SetActorScale3D((MaxCm - MinCm) / CubeEdgeCm);
		BoxActor->SetActorLocation(0.5 * (MinCm + MaxCm));
		BoxActor->Tags.Add(FTDFluidTerrainCapture::TerrainActorTag);
	}

	FVector ToWorldCm(const FTDFog3DGroundRect& Rect, double CellSizeCm, double ColumnX, double ColumnY, double ZCm)
	{
		return FVector((Rect.MinCell.X + ColumnX) * CellSizeCm, (Rect.MinCell.Y + ColumnY) * CellSizeCm, ZCm);
	}

	void SpawnPartialTerrain(UWorld& World, UStaticMesh& CubeMesh, const FTDFog3DGroundRect& Rect, double CellSizeCm, double FloorZ)
	{
		const double LowColumn = -SlabMarginColumns;
		const double HighColumn = Rect.Size + SlabMarginColumns;
		const double HighTopZ = FloorZ + HighTopAboveFloorCm;
		const double LowTopZ = FloorZ + LowTopAboveFloorCm;
		SpawnTerrainBox(World, CubeMesh, ToWorldCm(Rect, CellSizeCm, LowColumn, LowColumn, HighTopZ - SlabThicknessCm), ToWorldCm(Rect, CellSizeCm, HighEndColumns, HighColumn, HighTopZ));
		SpawnTerrainBox(World, CubeMesh, ToWorldCm(Rect, CellSizeCm, HighEndColumns, LowColumn, LowTopZ - SlabThicknessCm), ToWorldCm(Rect, CellSizeCm, LowEndColumns, HighColumn, LowTopZ));
	}

	bool ReadRawDepths(FAutomationTestBase& Test, UTextureRenderTarget2D& Target, TArray<float>& OutDepths)
	{
		FTextureRenderTargetResource* Resource = Target.GameThread_GetRenderTargetResource();
		TArray<FLinearColor> Pixels;
		if (!Test.TestNotNull(TEXT("The zone capture target has a resource"), Resource) || !Test.TestTrue(TEXT("Read back the zone depth capture"), Resource->ReadLinearColorPixels(Pixels)))
		{
			return false;
		}
		OutDepths.Reset(Pixels.Num());
		for (const FLinearColor& Pixel : Pixels)
		{
			OutDepths.Add(Pixel.R);
		}
		return true;
	}

	bool ReadHeightPlane(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& Plane, int32 Size, TArray<float>& OutValues)
	{
		if (!Plane.IsValid())
		{
			return false;
		}
		FRHIGPUTextureReadback Readback(TEXT("TDFog3DGroundZoneHeightReadback"));
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFog3DGroundZoneHeightReadback"));
			FRDGTextureRef Source = GraphBuilder.RegisterExternalTexture(Plane);
			AddEnqueueCopyPass(GraphBuilder, &Readback, Source);
			GraphBuilder.SetTextureAccessFinal(Source, ERHIAccess::SRVMask);
			GraphBuilder.Execute();
		}
		for (int32 SubmitIndex = 0; SubmitIndex < ReadbackSubmitLimit && !Readback.IsReady(); ++SubmitIndex)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			FPlatformProcess::Sleep(0.001f);
		}
		if (!Readback.IsReady())
		{
			return false;
		}
		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const float* Values = static_cast<const float*>(Readback.Lock(RowPitchInPixels, &BufferHeight));
		if (!Values || RowPitchInPixels < Size || BufferHeight < Size)
		{
			Readback.Unlock();
			return false;
		}
		OutValues.SetNumUninitialized(Size * Size);
		for (int32 RowIndex = 0; RowIndex < Size; ++RowIndex)
		{
			FMemory::Memcpy(OutValues.GetData() + RowIndex * Size, Values + static_cast<int64>(RowIndex) * RowPitchInPixels, Size * sizeof(float));
		}
		Readback.Unlock();
		return true;
	}

	TSharedRef<FTDGroundConvertOutcome, ESPMode::ThreadSafe> RunConvertStep(const FTDFluidGpuFog3DFrameInput& FrameInput, int32 Size)
	{
		TSharedRef<FTDGroundConvertOutcome, ESPMode::ThreadSafe> Outcome = MakeShared<FTDGroundConvertOutcome, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFog3DGroundZoneConvert)(
			[Outcome, FrameInput, Size](FRHICommandListImmediate& RHICmdList)
			{
				FTDFluidGpuFog3DState State;
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFog3DGroundZoneConvertStep"));
					Outcome->bAccepted = AddTDFluidGpuFog3DPasses(GraphBuilder, State, FrameInput);
					GraphBuilder.Execute();
				}
				Outcome->bReadBack = ReadHeightPlane(RHICmdList, State.GroundHeightTarget, Size, Outcome->Heights);
				State.Release();
			});
		FlushRenderingCommands();
		return Outcome;
	}

	FTDFog3DClockDecision MakeStepClock()
	{
		FTDFog3DClockDecision Clock;
		Clock.bStepsNow = true;
		Clock.StepDeltaSeconds = 1.0f / 60.0f;
		Clock.ElapsedSeconds = 1.0f / 60.0f;
		return Clock;
	}

	TSharedRef<FJsonObject> MakeRangeJson(const FTDGroundRangeStats& Stats)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("count"), Stats.Count);
		Json->SetNumberField(TEXT("nonFiniteCount"), Stats.NonFiniteCount);
		const bool bHasFinite = Stats.Count > Stats.NonFiniteCount;
		Json->SetNumberField(TEXT("min"), bHasFinite ? Stats.Min : 0.0);
		Json->SetNumberField(TEXT("max"), bHasFinite ? Stats.Max : 0.0);
		return Json;
	}

	FString WriteMissingDepthJson(const TSharedRef<FJsonObject>& Root)
	{
		FString JsonText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
		FJsonSerializer::Serialize(Root, Writer);
		const FString OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), MissingDepthOutputRelativePath);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
		FFileHelper::SaveStringToFile(JsonText, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		return OutputPath;
	}

	FTDFog3DGroundDecision AdvanceUntilConvert(ATDFog3DZoneActor& Zone, const FVector& PlayerWorld, int32 AdvanceLimit)
	{
		FTDFog3DGroundDecision Decision;
		for (int32 AdvanceIndex = 0; AdvanceIndex < AdvanceLimit && !Decision.bConvertsCapture; ++AdvanceIndex)
		{
			Decision = FTDFog3DZoneActorGroundTestAccess::TrackGround(Zone, PlayerWorld);
			FlushRenderingCommands();
		}
		return Decision;
	}

	void RunMissingTexels(FAutomationTestBase& Test, UStaticMesh& CubeMesh)
	{
		FTDGroundZoneWorld ZoneWorld;
		UWorld& World = *ZoneWorld.World;
		ATDFog3DZoneActor* Zone = World.SpawnActor<ATDFog3DZoneActor>(ZoneLocation, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("The zone spawns"), Zone))
		{
			return;
		}
		FTDFog3DSettings& Settings = FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone);
		Settings.bFollowsGround = true;
		Settings.GroundMissingHeightCm = TestMissingHeightCm;
		FTDFog3DZoneActorGroundTestAccess::UpdateGroundResources(*Zone);
		UTextureRenderTarget2D* CaptureTarget = FTDFog3DZoneActorGroundTestAccess::GetGroundCaptureTarget(*Zone);
		USceneCaptureComponent2D* Capture = FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*Zone);
		if (!Test.TestNotNull(TEXT("Ground on creates the capture target"), CaptureTarget) || !Test.TestNotNull(TEXT("Ground on creates the capture"), Capture))
		{
			return;
		}

		const FTDFog3DLayer& Layer = FTDFog3DZoneActorGroundTestAccess::GetLayer(*Zone);
		const FVector PlayerWorld = FTDFog3DZoneActorGroundTestAccess::GetZoneReferenceWorld(*Zone) + PlayerOffset;
		const double FloorZ = FTDFog3DZoneActorGroundTestAccess::GetZoneFloorZ(*Zone);
		const FIntPoint NextWindowMinCell = Layer.PreviewNextWindowMinCell(PlayerWorld);
		const FTDFog3DGroundRect Rect = FTDFog3DGroundLayout::ComputeCaptureRect(NextWindowMinCell, Layer.GetResolution(), Settings.GroundCaptureBorderCells);
		if (!Test.TestTrue(TEXT("The empty region is inside the capture"), Rect.IsValid() && Rect.Size > LowEndColumns + 1.0))
		{
			return;
		}
		SpawnPartialTerrain(World, CubeMesh, Rect, Settings.CellSizeCm, FloorZ);
		for (int32 FrameIndex = 0; FrameIndex < SettleFrameCount; ++FrameIndex)
		{
			ZoneWorld.StepWorld();
		}
		FTDFog3DZoneActorGroundTestAccess::GetGroundTracker(*Zone).Reset();

		const FTDFog3DGroundDecision Decision = AdvanceUntilConvert(*Zone, PlayerWorld, Settings.GroundCaptureLatencyFrames + 1);
		if (!Test.TestTrue(TEXT("The zone converts its capture after the latency"), Decision.bConvertsCapture && Decision.StepRect == Rect))
		{
			return;
		}
		TArray<float> RawDepths;
		if (!ReadRawDepths(Test, *CaptureTarget, RawDepths) || !Test.TestEqual(TEXT("Raw depth texel count is S x S"), RawDepths.Num(), Rect.Size * Rect.Size))
		{
			return;
		}

		FTDFog3DStepRequest Request;
		Request.PlayerWorld = PlayerWorld;
		Request.FloorZ = FloorZ;
		Request.Clock = MakeStepClock();
		Request.Ground = FTDFog3DZoneActorGroundTestAccess::MakeGroundStep(*Zone, Decision);
		const FTDFluidGpuFog3DFrameInput FrameInput = Layer.BuildFrameInput(Request);
		Test.TestTrue(TEXT("The game path asks the GPU to convert the capture"), FrameInput.bFollowsGround && FrameInput.bGroundChanged && FrameInput.GroundCapture.DepthTarget != nullptr);
		Test.TestEqual(TEXT("The game path carries the below floor margin"), FrameInput.GroundCapture.MissingBelowFloorCm, Settings.GroundMissingBelowFloorCm);
		const TSharedRef<FTDGroundConvertOutcome, ESPMode::ThreadSafe> Outcome = RunConvertStep(FrameInput, Rect.Size);
		Test.TestTrue(TEXT("The GPU accepts the converting step"), Outcome->bAccepted);
		if (!Test.TestTrue(TEXT("The converted ground reads back"), Outcome->bReadBack && Outcome->Heights.Num() == Rect.Size * Rect.Size))
		{
			return;
		}

		FTDGroundRangeStats EmptyDepths;
		FTDGroundRangeStats HighDepths;
		FTDGroundRangeStats LowDepths;
		FTDGroundRangeStats HighHeights;
		FTDGroundRangeStats LowHeights;
		int32 HighMismatchCount = 0;
		int32 LowMismatchCount = 0;
		int32 EmptyMismatchCount = 0;
		for (int32 TexelY = 0; TexelY < Rect.Size; ++TexelY)
		{
			for (int32 TexelX = 0; TexelX < Rect.Size; ++TexelX)
			{
				const int32 TexelIndex = TexelY * Rect.Size + TexelX;
				const double RawDepth = RawDepths[TexelIndex];
				const double Height = Outcome->Heights[TexelIndex];
				switch (FindRegion(TexelX))
				{
				case ETDGroundRegion::High:
					HighDepths.Add(RawDepth);
					HighHeights.Add(Height);
					HighMismatchCount += FMath::Abs(Height - HighTopAboveFloorCm) <= FoundToleranceCm ? 0 : 1;
					break;
				case ETDGroundRegion::Low:
					LowDepths.Add(RawDepth);
					LowHeights.Add(Height);
					LowMismatchCount += FMath::Abs(Height - LowTopAboveFloorCm) <= FoundToleranceCm ? 0 : 1;
					break;
				default:
					EmptyDepths.Add(RawDepth);
					EmptyMismatchCount += Outcome->Heights[TexelIndex] == TestMissingHeightCm ? 0 : 1;
					break;
				}
			}
		}
		Test.TestTrue(TEXT("The scene has empty texels"), EmptyDepths.Count > 0);
		Test.TestEqual(TEXT("Texels on the high slab hold its height"), HighMismatchCount, 0);
		Test.TestEqual(TEXT("Texels on the slab 10 m under the floor keep that height"), LowMismatchCount, 0);
		Test.TestEqual(TEXT("Texels with no terrain hold the missing height"), EmptyMismatchCount, 0);

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("test"), TEXT("TDGame.FluidFXRhi.Fog3D.GroundCaptureMissingTexels"));
		Root->SetStringField(TEXT("rhi"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"));
		Root->SetNumberField(TEXT("captureSize"), Rect.Size);
		Root->SetNumberField(TEXT("decodeOffsetCm"), FTDFog3DZoneActorGroundTestAccess::GetGroundCaptureDecodeOffset(*Zone));
		Root->SetNumberField(TEXT("orthoWidthCm"), Capture->OrthoWidth);
		Root->SetBoolField(TEXT("renderInMainRenderer"), Capture->bRenderInMainRenderer);
		Root->SetBoolField(TEXT("autoCalculateOrthoPlanes"), Capture->bAutoCalculateOrthoPlanes);
		Root->SetBoolField(TEXT("updateOrthoPlanes"), Capture->bUpdateOrthoPlanes);
		Root->SetNumberField(TEXT("maxViewDistanceOverride"), Capture->MaxViewDistanceOverride);
		Root->SetNumberField(TEXT("clearColor"), CaptureTarget->ClearColor.R);
		Root->SetNumberField(TEXT("expectedFarPlaneDepthCm"), FarPlaneDepthCm);
		Root->SetObjectField(TEXT("emptyTexelRawDepth"), MakeRangeJson(EmptyDepths));
		Root->SetObjectField(TEXT("highSlabRawDepth"), MakeRangeJson(HighDepths));
		Root->SetObjectField(TEXT("lowSlabRawDepth"), MakeRangeJson(LowDepths));
		Root->SetObjectField(TEXT("highSlabHeightCm"), MakeRangeJson(HighHeights));
		Root->SetObjectField(TEXT("lowSlabHeightCm"), MakeRangeJson(LowHeights));
		Root->SetNumberField(TEXT("highExpectedCm"), HighTopAboveFloorCm);
		Root->SetNumberField(TEXT("lowExpectedCm"), LowTopAboveFloorCm);
		Root->SetNumberField(TEXT("missingHeightCm"), TestMissingHeightCm);
		Root->SetNumberField(TEXT("missingBelowFloorCm"), Settings.GroundMissingBelowFloorCm);
		Root->SetNumberField(TEXT("highMismatchCount"), HighMismatchCount);
		Root->SetNumberField(TEXT("lowMismatchCount"), LowMismatchCount);
		Root->SetNumberField(TEXT("emptyMismatchCount"), EmptyMismatchCount);
		const FString OutputPath = WriteMissingDepthJson(Root);
		Test.AddInfo(FString::Printf(TEXT("Ground capture missing texels: empty raw depth %.4f..%.4f over %d texels, wrote %s"), EmptyDepths.Min, EmptyDepths.Max, EmptyDepths.Count, *OutputPath));
		World.DestroyActor(Zone);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundCaptureMissingTexelsTest,
	"TDGame.FluidFXRhi.Fog3D.GroundCaptureMissingTexels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundCaptureMissingTexelsTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DGroundZoneRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundCaptureMissingTexels needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	AddExpectedMessagePlain(TEXT("has no VolumeMaterial and stays hidden"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("Fog3D zone summary:"), ELogVerbosity::Display, EAutomationExpectedMessageFlags::Contains, 1);

	const FSoftObjectPath CubePath(CubeMeshPath);
	TSharedPtr<FStreamableHandle> LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(TArray<FSoftObjectPath>{ CubePath });
	const double LoadStartSeconds = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, LoadHandle, CubePath, LoadStartSeconds]()
	{
		UStaticMesh* CubeMesh = Cast<UStaticMesh>(CubePath.ResolveObject());
		const bool bHasLoadEnded = !LoadHandle.IsValid() || LoadHandle->HasLoadCompletedOrStalled();
		if (!CubeMesh && !bHasLoadEnded && FPlatformTime::Seconds() - LoadStartSeconds < LoadTimeoutSeconds)
		{
			return false;
		}
		if (!TestNotNull(TEXT("Engine cube mesh finished its asynchronous load"), CubeMesh))
		{
			return true;
		}
		RunMissingTexels(*this, *CubeMesh);
		return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundZoneSimulateHoldTest,
	"TDGame.FluidFXRhi.Fog3D.GroundZoneSimulateHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundZoneSimulateHoldTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DGroundZoneRhiTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundZoneSimulateHold needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	AddExpectedMessagePlain(TEXT("has no VolumeMaterial and stays hidden"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("found no skeletal mesh on pawn"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("Fog3D zone summary:"), ELogVerbosity::Display, EAutomationExpectedMessageFlags::Contains, 1);

	FTDGroundZoneWorld ZoneWorld;
	UWorld& World = *ZoneWorld.World;
	ATDFog3DZoneActor* Zone = World.SpawnActor<ATDFog3DZoneActor>(ZoneLocation, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The zone spawns"), Zone))
	{
		return false;
	}
	FTDFog3DSettings& Settings = FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone);
	const FVector PlayerWorld = FTDFog3DZoneActorGroundTestAccess::GetZoneReferenceWorld(*Zone) + PlayerOffset;
	APawn* Pawn = World.SpawnActor<APawn>(PlayerWorld, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The pawn spawns"), Pawn))
	{
		World.DestroyActor(Zone);
		return false;
	}
	Settings.bFollowsGround = true;
	FTDFog3DZoneActorGroundTestAccess::UpdateGroundResources(*Zone);
	if (!TestNotNull(TEXT("Ground on creates the capture"), FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*Zone)))
	{
		World.DestroyActor(Zone);
		return false;
	}

	const FTDFog3DLayer& Layer = FTDFog3DZoneActorGroundTestAccess::GetLayer(*Zone);
	FTDFog3DMaterialParameters& MaterialParameters = FTDFog3DZoneActorGroundTestAccess::GetDesiredMaterialParameters(*Zone);
	const FVector4f EmptyGround(0.0f, 0.0f, 0.0f, 0.0f);
	const auto Simulate = [Zone, Pawn](float DeltaSeconds)
	{
		FTDFog3DZoneActorGroundTestAccess::SimulateStep(*Zone, *Pawn, DeltaSeconds);
		FlushRenderingCommands();
	};
	const auto CheckConversion = [this, &Layer, &Settings, &MaterialParameters, Zone, PlayerWorld](const TCHAR* Phase, uint32 StepsBefore, FVector4f GroundBefore, float FirstDeltaSeconds, const auto& SimulateFunction)
	{
		const float IntervalSeconds = 1.0f / Layer.GetStepsPerSecond();
		SimulateFunction(FMath::Max(FirstDeltaSeconds, IntervalSeconds));
		TestEqual(*FString::Printf(TEXT("%s: the request tick holds the step"), Phase), Layer.GetSubmittedStepCount(), StepsBefore);
		TestTrue(*FString::Printf(TEXT("%s: the request tick leaves Fog3DGround"), Phase), MaterialParameters.Ground == GroundBefore);
		for (int32 WaitIndex = 1; WaitIndex < Settings.GroundCaptureLatencyFrames; ++WaitIndex)
		{
			SimulateFunction(TinyDeltaSeconds);
			TestEqual(*FString::Printf(TEXT("%s: the step still holds while the capture waits"), Phase), Layer.GetSubmittedStepCount(), StepsBefore);
			TestTrue(*FString::Printf(TEXT("%s: Fog3DGround waits for the converting step"), Phase), MaterialParameters.Ground == GroundBefore);
		}
		SimulateFunction(TinyDeltaSeconds);
		TestEqual(*FString::Printf(TEXT("%s: the kept clock share steps once the capture is ready"), Phase), Layer.GetSubmittedStepCount(), StepsBefore + 1);
		const FTDFog3DGroundRect ExpectedRect = FTDFog3DGroundLayout::ComputeCaptureRect(Layer.GetSubmittedWindowMinCell(), Layer.GetResolution(), Settings.GroundCaptureBorderCells);
		const FVector4f ExpectedGround = FTDFog3DGroundLayout::ComputeMaterialGround(ExpectedRect, Settings.CellSizeCm, FTDFog3DZoneActorGroundTestAccess::GetZoneReferenceWorld(*Zone));
		TestTrue(*FString::Printf(TEXT("%s: the converting step writes Fog3DGround for the captured rect"), Phase), MaterialParameters.Ground == ExpectedGround);
		TestTrue(*FString::Printf(TEXT("%s: the tracker holds the converted rect"), Phase), FTDFog3DZoneActorGroundTestAccess::GetGroundTracker(*Zone).GetConvertedRect() == ExpectedRect);
	};

	const int32 FirstResolution = Layer.GetResolution();
	CheckConversion(TEXT("first entry"), 0, EmptyGround, 0.0f, Simulate);
	const FVector4f FirstGround = MaterialParameters.Ground;
	TestEqual(TEXT("first entry: Fog3DGround size is N + 2B"), FirstGround.Z, static_cast<float>(FirstResolution + 2 * Settings.GroundCaptureBorderCells));

	Settings.DefaultQualityLevel = Settings.DefaultQualityLevel == 0 ? 2 : 0;
	FTDFog3DZoneActorGroundTestAccess::ConfigureLayer(*Zone);
	TestNotEqual(TEXT("The quality switch changes the window size"), Layer.GetResolution(), FirstResolution);
	TestTrue(TEXT("The quality switch forgets the image"), FTDFog3DZoneActorGroundTestAccess::GetGroundTracker(*Zone).GetState() == ETDFog3DGroundTrackerState::NoImage);
	TestTrue(TEXT("The quality switch keeps Fog3DGround until a converting step"), MaterialParameters.Ground == FirstGround);
	CheckConversion(TEXT("after the quality switch"), Layer.GetSubmittedStepCount(), MaterialParameters.Ground, 2.0f / Layer.GetStepsPerSecond(), Simulate);
	TestEqual(TEXT("after the quality switch: Fog3DGround size is the new N + 2B"), MaterialParameters.Ground.Z, static_cast<float>(Layer.GetResolution() + 2 * Settings.GroundCaptureBorderCells));

	World.DestroyActor(Pawn);
	World.DestroyActor(Zone);
	return true;
}

#endif
